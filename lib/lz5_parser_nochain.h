#define LZ5_NOCHAIN_MIN_OFFSET 8

/**************************************
*  Hash Functions
**************************************/
static size_t LZ5_hashPosition(const void* p, int hashLog) 
{
    if (MEM_64bits())
        return LZ5_hash5Ptr(p, hashLog);
    return LZ5_hash4Ptr(p, hashLog);
}

static void LZ5_putPositionOnHash(const BYTE* p, size_t h, U32* hashTable, const BYTE* srcBase)
{
    hashTable[h] = (U32)(p-srcBase);
}

static void LZ5_putPosition(const BYTE* p, U32* hashTable, const BYTE* srcBase, int hashLog)
{
    size_t const h = LZ5_hashPosition(p, hashLog);
    LZ5_putPositionOnHash(p, h, hashTable, srcBase);
}

static U32 LZ5_getPositionOnHash(size_t h, U32* hashTable)
{
    return hashTable[h];
}

static U32 LZ5_getPosition(const BYTE* p, U32* hashTable, int hashLog)
{
    size_t const h = LZ5_hashPosition(p, hashLog);
    return LZ5_getPositionOnHash(h, hashTable);
}


static const U32 LZ5_skipTrigger = 6;  /* Increase this value ==> compression run slower on incompressible data */
static const int LZ5_minLength = (MFLIMIT+1);


FORCE_INLINE int LZ5_compress_nochain(
                 void* const ctxvoid,
                 const char* const source,
                 char* const dest,
                 const int inputSize,
                 const int maxOutputSize,
                 const limitedOutput_directive outputLimited)
{
    LZ5_stream_t* const ctx = (LZ5_stream_t*) ctxvoid;
    const U32 acceleration = 1;
    const BYTE* ip = (const BYTE*) source;
    const BYTE* base = ctx->base;
    const U32 lowLimit = ctx->lowLimit;
    const U32 dictLimit = ctx->dictLimit;
    const BYTE* const lowPrefixPtr = base + dictLimit;
    const BYTE* const dictBase = ctx->dictBase;
    const BYTE* const dictEnd = dictBase + dictLimit;
    const BYTE* anchor = (const BYTE*) source;
    const BYTE* const iend = ip + inputSize;
    const BYTE* const mflimit = iend - MFLIMIT;
    const BYTE* const matchlimit = iend - LASTLITERALS;
    BYTE* op = (BYTE*) dest;
    BYTE* const oend = op + maxOutputSize;

    size_t forwardH, matchIndex;
    const int hashLog = ctx->params.hashLog;
    const U32 maxDistance = (1 << ctx->params.windowLog) - 1;

  //  fprintf(stderr, "base=%p LZ5_stream_t=%d inputSize=%d maxOutputSize=%d\n", base, sizeof(LZ5_stream_t), inputSize, maxOutputSize);
 //   fprintf(stderr, "ip=%d base=%p lowPrefixPtr=%p dictBase=%d lowLimit=%p op=%p\n", ip, base, lowPrefixPtr, lowLimit, dictBase, op);

    /* Init conditions */
    ctx->end += inputSize;
    if ((U32)inputSize > (U32)LZ5_MAX_INPUT_SIZE) goto _output_error;   /* Unsupported inputSize, too large (or negative) */

    if (inputSize<LZ5_minLength) goto _last_literals;                  /* Input too small, no compression (all literals) */

    /* First Byte */
    LZ5_putPosition(ip, ctx->hashTable, base, hashLog);
    ip++; forwardH = LZ5_hashPosition(ip, hashLog);

    /* Main Loop */
    for ( ; ; ) {
        const BYTE* match;
      //  BYTE* token;
        size_t matchLength;

        /* Find a match */
        {   const BYTE* forwardIp = ip;
            unsigned step = 1;
            unsigned searchMatchNb = acceleration << LZ5_skipTrigger;
            while (1) {
                size_t const h = forwardH;
                ip = forwardIp;
                forwardIp += step;
                step = (searchMatchNb++ >> LZ5_skipTrigger);

                if (unlikely(forwardIp > mflimit)) goto _last_literals;

                matchIndex = LZ5_getPositionOnHash(h, ctx->hashTable);
                forwardH = LZ5_hashPosition(forwardIp, hashLog);
                LZ5_putPositionOnHash(ip, h, ctx->hashTable, base);

                if ((matchIndex < lowLimit) || (base + matchIndex + maxDistance < ip)) continue;

                if (matchIndex >= dictLimit) {
                    match = base + matchIndex;
#if LZ5_NOCHAIN_MIN_OFFSET > 0
                    if ((U32)(ip - match) >= LZ5_NOCHAIN_MIN_OFFSET)
#endif
                    if (MEM_read32(match) == MEM_read32(ip))
                    {
                        int back = 0;
                        matchLength = LZ5_count(ip+MINMATCH, match+MINMATCH, matchlimit);

                        while ((ip+back > anchor) && (match+back > lowPrefixPtr) && (ip[back-1] == match[back-1])) back--;
                        matchLength -= back;
                        ip += back;
                        match += back;
                        break;
                    }
                } else {
                    match = dictBase + matchIndex;
#if LZ5_NOCHAIN_MIN_OFFSET > 0
                    if ((U32)(ip - (base + matchIndex)) >= LZ5_NOCHAIN_MIN_OFFSET)
#endif
                    if ((U32)((dictLimit-1) - matchIndex) >= 3)  /* intentional overflow */
                    if (MEM_read32(match) == MEM_read32(ip)) {
                        const U32 newLowLimit = (lowLimit + maxDistance >= (U32)(ip-base)) ? lowLimit : (U32)(ip - base) - maxDistance;
                        int back = 0;
                        matchLength = LZ5_count_2segments(ip+MINMATCH, match+MINMATCH, matchlimit, dictEnd, lowPrefixPtr);

                        while ((ip+back > anchor) && (matchIndex+back > newLowLimit) && (ip[back-1] == match[back-1])) back--;
                        matchLength -= back;
                        ip += back;
                        match = base + matchIndex + back;
                        break;
                    }
                }
            } // while (1)
        }

_next_match:
        if (ctx->params.decompressType == LZ5_coderwords_LZ4) {
            if (LZ5_encodeSequence_LZ4(ctx, &ip, &op, &anchor, matchLength+MINMATCH, match, outputLimited, oend)) goto _output_error;
        } else {
            if (LZ5_encodeSequence_LZ5v2(ctx, &ip, &op, &anchor, matchLength+MINMATCH, match, outputLimited, oend)) goto _output_error;          
        }
        
        /* Test end of chunk */
        if (ip > mflimit) break;

        /* Fill table */
        LZ5_putPosition(ip-2, ctx->hashTable, base, hashLog);

        /* Test next position */
        matchIndex = LZ5_getPosition(ip, ctx->hashTable, hashLog);
        LZ5_putPosition(ip, ctx->hashTable, base, hashLog);
        if (matchIndex >= lowLimit && (base + matchIndex + maxDistance >= ip))
        {
            if (matchIndex >= dictLimit) {
                match = base + matchIndex;
#if LZ5_NOCHAIN_MIN_OFFSET > 0
                if ((U32)(ip - match) >= LZ5_NOCHAIN_MIN_OFFSET)
#endif
                if (MEM_read32(match) == MEM_read32(ip))
                {
                    matchLength = LZ5_count(ip+MINMATCH, match+MINMATCH, matchlimit);
                   goto _next_match;
                }
            } else {
                match = dictBase + matchIndex;
#if LZ5_NOCHAIN_MIN_OFFSET > 0
                if ((U32)(ip - (base + matchIndex)) >= LZ5_NOCHAIN_MIN_OFFSET)
#endif
                if ((U32)((dictLimit-1) - matchIndex) >= 3)  /* intentional overflow */
                if (MEM_read32(match) == MEM_read32(ip)) {
                    matchLength = LZ5_count_2segments(ip+MINMATCH, match+MINMATCH, matchlimit, dictEnd, lowPrefixPtr);
                    match = base + matchIndex;
                    goto _next_match;
                }
            }
        }

        /* Prepare next loop */
        forwardH = LZ5_hashPosition(++ip, hashLog);
    }

_last_literals:
    /* Encode Last Literals */
    ip = iend;
    if (ctx->params.decompressType == LZ5_coderwords_LZ4) {
        if (LZ5_encodeLastLiterals_LZ4(ctx, &ip, &op, &anchor, outputLimited, oend)) goto _output_error;
    } else {
        if (LZ5_encodeLastLiterals_LZ5v2(ctx, &ip, &op, &anchor, outputLimited, oend)) goto _output_error;      
    }

    /* End */
    return (int) (((char*)op)-dest);
_output_error:
    return 0;
}
