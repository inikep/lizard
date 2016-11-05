#define LZ5_FREQ_DIV   5

FORCE_INLINE void LZ5_setLog2Prices(LZ5_stream_t* ctx)
{
    ctx->log2LitSum = LZ5_highbit32(ctx->litSum+1);
    ctx->log2FlagSum = LZ5_highbit32(ctx->flagSum+1);
}


MEM_STATIC void LZ5_rescaleFreqs(LZ5_stream_t* ctx)
{
    unsigned u;

    ctx->cachedLiterals = NULL;
    ctx->cachedPrice = ctx->cachedLitLength = 0;
    
    ctx->litPriceSum = 0;

    if (ctx->litSum == 0) {
        ctx->litSum = 2 * 256;
        ctx->flagSum = 2 * 256;

        for (u=0; u < 256; u++) {
            ctx->litFreq[u] = 2;
            ctx->flagFreq[u] = 2;
        }
    } else {
        ctx->litSum = 0;
        ctx->flagSum = 0;

        for (u=0; u < 256; u++) {
            ctx->litFreq[u] = 1 + (ctx->litFreq[u]>>LZ5_FREQ_DIV);
            ctx->litSum += ctx->litFreq[u];
            ctx->flagFreq[u] = 1 + (ctx->flagFreq[u]>>LZ5_FREQ_DIV);
            ctx->flagSum += ctx->flagFreq[u];
        }
    }

    LZ5_setLog2Prices(ctx);
}


FORCE_INLINE int LZ5_encodeSequence_LZ5v2 (
    LZ5_stream_t* ctx,
    const BYTE** ip,
    const BYTE** anchor,
    size_t matchLength,
    const BYTE* const match)
{
    U32 offset = (U32)(*ip - match);
    size_t length = (size_t)(*ip - *anchor);
    BYTE* token = (ctx->flagsPtr)++;

    if (length > 0 || offset < LZ5_MAX_16BIT_OFFSET) {
        /* Encode Literal length */
      //  if ((limitedOutputBuffer) && (ctx->literalsPtr > oend - length - LZ5_LENGTH_SIZE_LZ5v2(length) - WILDCOPYLENGTH)) { LZ5_LOG_COMPRESS_LZ5v2("encodeSequence overflow1\n"); return 1; }   /* Check output limit */
        if (length >= MAX_SHORT_LITLEN) 
        {   size_t len; 
            *token = MAX_SHORT_LITLEN; 
            len = length - MAX_SHORT_LITLEN;
            if (len >= (1<<16)) { *(ctx->literalsPtr) = 255;  MEM_writeLE24(ctx->literalsPtr+1, (U32)(len));  ctx->literalsPtr += 4; }
            else if (len >= 254) { *(ctx->literalsPtr) = 254;  MEM_writeLE16(ctx->literalsPtr+1, (U16)(len));  ctx->literalsPtr += 3; }
            else *(ctx->literalsPtr)++ = (BYTE)len;
        }
        else *token = (BYTE)length;

        /* Copy Literals */
        LZ5_wildCopy(ctx->literalsPtr, *anchor, (ctx->literalsPtr) + length);
#ifdef LZ5_USE_HUFFMAN
        ctx->litSum += length;
        ctx->litPriceSum += length * ctx->log2LitSum;
        {   U32 u;
            for (u=0; u < length; u++) {
                ctx->litPriceSum -= LZ5_highbit32(ctx->litFreq[ctx->literalsPtr[u]]+1);
                ctx->litFreq[ctx->literalsPtr[u]]++;
        }   }
#endif
        ctx->literalsPtr += length;


        if (offset >= LZ5_MAX_16BIT_OFFSET) {
            COMPLOG_CODEWORDS_LZ5v2("T32+ literal=%u match=%u offset=%d\n", (U32)length, 0, 0);
            *token+=(1<<ML_RUN_BITS);
#ifdef LZ5_USE_HUFFMAN
            ctx->flagFreq[*token]++;
            ctx->flagSum++;
#endif
            token = (ctx->flagsPtr)++;
        }
    }

    /* Encode Offset */
    if (offset >= LZ5_MAX_16BIT_OFFSET)  // 24-bit offset
    {
        if (matchLength < MM_LONGOFF) printf("ERROR matchLength=%d/%d\n", (int)matchLength, MM_LONGOFF), exit(0);

      //  if ((limitedOutputBuffer) && (ctx->literalsPtr > oend - 8 /*LZ5_LENGTH_SIZE_LZ5v2(length)*/)) { LZ5_LOG_COMPRESS_LZ5v2("encodeSequence overflow2\n"); return 1; }   /* Check output limit */
        if (matchLength - MM_LONGOFF >= LZ5_LAST_LONG_OFF) 
        {
            size_t len = matchLength - MM_LONGOFF - LZ5_LAST_LONG_OFF;
            *token = LZ5_LAST_LONG_OFF;
            if (len >= (1<<16)) { *(ctx->literalsPtr) = 255;  MEM_writeLE24(ctx->literalsPtr+1, (U32)(len));  ctx->literalsPtr += 4; }
            else if (len >= 254) { *(ctx->literalsPtr) = 254;  MEM_writeLE16(ctx->literalsPtr+1, (U16)(len));  ctx->literalsPtr += 3; }
            else *(ctx->literalsPtr)++ = (BYTE)len; 
            COMPLOG_CODEWORDS_LZ5v2("T31 literal=%u match=%u offset=%d\n", 0, (U32)matchLength, offset);
        }
        else
        {
            COMPLOG_CODEWORDS_LZ5v2("T0-30 literal=%u match=%u offset=%d\n", 0, (U32)matchLength, offset);
            *token = (BYTE)(matchLength - MM_LONGOFF);
        }

        MEM_writeLE24(ctx->offset24Ptr, offset); 
        ctx->offset24Ptr += 3;
        ctx->last_off = offset;
        ctx->off24pos = *ip;
    }
    else
    {
        COMPLOG_CODEWORDS_LZ5v2("T32+ literal=%u match=%u offset=%d\n", (U32)length, (U32)matchLength, offset);
        if (offset == 0)
        {
            *token+=(1<<ML_RUN_BITS);
        }
        else
        {
            if (offset < 8) printf("ERROR offset=%d\n", (int)offset);
            if (matchLength < MINMATCH) { printf("matchLength[%d] < MINMATCH  offset=%d\n", (int)matchLength, (int)ctx->last_off); exit(1); }
            
            ctx->last_off = offset;
            MEM_writeLE16(ctx->offset16Ptr, (U16)ctx->last_off); ctx->offset16Ptr += 2;
        }

        /* Encode MatchLength */
        length = matchLength;
       // if ((limitedOutputBuffer) && (ctx->literalsPtr > oend - 5 /*LZ5_LENGTH_SIZE_LZ5v2(length)*/)) { LZ5_LOG_COMPRESS_LZ5v2("encodeSequence overflow2\n"); return 1; }   /* Check output limit */
        if (length >= MAX_SHORT_MATCHLEN) {
            *token += (BYTE)(MAX_SHORT_MATCHLEN<<RUN_BITS_LZ5v2);
            length -= MAX_SHORT_MATCHLEN;
            if (length >= (1<<16)) { *(ctx->literalsPtr) = 255;  MEM_writeLE24(ctx->literalsPtr+1, (U32)(length));  ctx->literalsPtr += 4; }
            else if (length >= 254) { *(ctx->literalsPtr) = 254;  MEM_writeLE16(ctx->literalsPtr+1, (U16)(length));  ctx->literalsPtr += 3; }
            else *(ctx->literalsPtr)++ = (BYTE)length;
        }
        else *token += (BYTE)(length<<RUN_BITS_LZ5v2);
    }

#ifdef LZ5_USE_HUFFMAN
    ctx->flagFreq[*token]++;
    ctx->flagSum++;
    LZ5_setLog2Prices(ctx);
#endif

    /* Prepare next loop */
    *ip += matchLength;
    *anchor = *ip;

    return 0;
}


FORCE_INLINE int LZ5_encodeLastLiterals_LZ5v2 (
    LZ5_stream_t* ctx,
    const BYTE** ip,
    const BYTE** anchor)
{
    size_t length = (int)(*ip - *anchor);
    (void)ctx;

    memcpy(ctx->literalsPtr, *anchor, length);
    ctx->literalsPtr += length;
    return 0;
}


#define LZ5_24BIT_OFFSET_LOAD   price += LZ5_highbit32(offset)
#define LZ5_PRICE_MULT 2
#define LZ5_GET_TOKEN_PRICE(token)    (LZ5_PRICE_MULT * (ctx->log2FlagSum - LZ5_highbit32(ctx->flagFreq[token]+1)))

FORCE_INLINE size_t LZ5_get_price_LZ5v2(LZ5_stream_t* const ctx, int rep, const BYTE *ip, const BYTE *off24pos, size_t litLength, U32 offset, size_t matchLength) 
{
    int lz5_flag_weight = 8, lz5_literal_weight = 8, offset_load;
    size_t price = 0;

#if 0 //def LZ5_USE_HUFFMAN
    const BYTE* literals = ip - litLength;
    U32 u;

    if (ctx->cachedLiterals == literals) {
        U32 const additional = litLength - ctx->cachedLitLength;
    //    printf("%d ", (int)litLength - (int)ctx->cachedLitLength);
        const BYTE* literals2 = ctx->cachedLiterals + ctx->cachedLitLength;
        price = ctx->cachedPrice + LZ5_PRICE_MULT * additional * ctx->log2LitSum;
        for (u=0; u < additional; u++)
            price -= LZ5_PRICE_MULT * LZ5_highbit32(ctx->litFreq[literals2[u]]+1);
        ctx->cachedPrice = price;
        ctx->cachedLitLength = litLength;
    } else {
        price = LZ5_PRICE_MULT * litLength * ctx->log2LitSum;
        for (u=0; u < litLength; u++)
            price -= LZ5_PRICE_MULT * LZ5_highbit32(ctx->litFreq[literals[u]]+1);

        if (litLength >= 12) {
            ctx->cachedLiterals = literals;
            ctx->cachedPrice = price;
            ctx->cachedLitLength = litLength;
        }
    }
#else
    (void)ip;
#endif

    (void)off24pos;
    (void)rep;

    if (ctx) {
        if (ctx->huffType & LZ5_FLAG_FLAGS) lz5_flag_weight = 6;
        if (ctx->huffType & LZ5_FLAG_LITERALS) lz5_literal_weight = 7;
    }

    offset_load = 6;
    if (offset > 0 || matchLength > 0)
        price += lz5_flag_weight + offset_load + (matchLength==1);

    if (litLength > 0 || offset < LZ5_MAX_16BIT_OFFSET) {
        /* Encode Literal length */
        if (litLength>=(int)MAX_SHORT_LITLEN) {  
            size_t len = litLength - MAX_SHORT_LITLEN; 
            if (len >= (1<<16)) price += 32;
            else if (len >= 254) price += 24;
            else price += 8;
        }
    
        price += lz5_literal_weight*litLength;  /* Copy Literals */
        if (offset >= LZ5_MAX_16BIT_OFFSET)
            price += lz5_flag_weight;
    }

    /* Encode Offset */
    if (offset >= LZ5_MAX_16BIT_OFFSET) { // 24-bit offset
        if (matchLength < 16) return LZ5_MAX_PRICE; // error
        if (matchLength - MM_LONGOFF >= LZ5_LAST_LONG_OFF) {
            size_t len = matchLength - MM_LONGOFF - LZ5_LAST_LONG_OFF;
            if (len >= (1<<16)) price += 32;
            else if (len >= 254) price += 24;
            else price += 8;
        }
        price += 24; // ctx->literalsPtr += 3;
        LZ5_24BIT_OFFSET_LOAD;
    } else {
        if (offset) {
            if (matchLength < MINMATCH) return LZ5_MAX_PRICE; // error
            price += 16; // ctx->offset16Ptr += 2;
        }

        /* Encode MatchLength */
        if (matchLength>=(int)MAX_SHORT_MATCHLEN) 
        {   size_t len = matchLength - MAX_SHORT_MATCHLEN; 
            if (len >= (1<<16)) price += 32;
            else if (len >= 254) price += 24;
            else price += 8;
        }
    }
    return price;
}

