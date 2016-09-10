/*
    LZ5 - Fast LZ compression algorithm 
    Copyright (C) 2011-2015, Yann Collet.
    Copyright (C) 2015-2016, Przemyslaw Skibinski <inikep@gmail.com>

    BSD 2-Clause License (http://www.opensource.org/licenses/bsd-license.php)

    Redistribution and use in source and binary forms, with or without
    modification, are permitted provided that the following conditions are
    met:

    * Redistributions of source code must retain the above copyright
    notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above
    copyright notice, this list of conditions and the following disclaimer
    in the documentation and/or other materials provided with the
    distribution.

    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
    "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
    LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
    A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
    OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
    SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
    LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
    DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
    THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
    (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
    OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

    You can contact the author at :
       - LZ5 source repository : https://github.com/inikep/lz5
*/

/* *************************************
*  Includes
***************************************/
#include "lz5.h"
#define LZ5_MEM_FUNCTIONS
#include "lz5_common.h"
#include <stdio.h>


/* *************************************
*  Local Constants
***************************************/
#define OPTIMAL_ML (int)((ML_MASK-1)+MINMATCH)


/* *************************************
*  HC Inline functions and Macros
***************************************/
static const U32 prime4bytes = 2654435761U;
static const U64 prime5bytes = 889523592379ULL;
static const U64 prime6bytes = 227718039650203ULL;
static const U64 prime7bytes = 58295818150454627ULL;

#if MINMATCH == 3
static const U32 prime3bytes = 506832829U;
static U32 LZ5_hash3(U32 u, U32 h) { return (u * prime3bytes) << (32-24) >> (32-h) ; }
static size_t LZ5_hash3Ptr(const void* ptr, U32 h) { return LZ5_hash3(MEM_read32(ptr), h); }
#endif

static U32 LZ5_hash4(U32 u, U32 h) { return (u * prime4bytes) >> (32-h) ; }
static size_t LZ5_hash4Ptr(const void* ptr, U32 h) { return LZ5_hash4(MEM_read32(ptr), h); }

static size_t LZ5_hash5(U64 u, U32 h) { return (size_t)((u * prime5bytes) << (64-40) >> (64-h)) ; }
static size_t LZ5_hash5Ptr(const void* p, U32 h) { return LZ5_hash5(MEM_read64(p), h); }

static size_t LZ5_hash6(U64 u, U32 h) { return (size_t)((u * prime6bytes) << (64-48) >> (64-h)) ; }
static size_t LZ5_hash6Ptr(const void* p, U32 h) { return LZ5_hash6(MEM_read64(p), h); }

static size_t LZ5_hash7(U64 u, U32 h) { return (size_t)((u * prime7bytes) << (64-56) >> (64-h)) ; }
static size_t LZ5_hash7Ptr(const void* p, U32 h) { return LZ5_hash7(MEM_read64(p), h); }

static size_t LZ5_hashPtr(const void* p, U32 hBits, U32 mls)
{
    switch(mls)
    {
    default:
    case 4: return LZ5_hash4Ptr(p, hBits);
    case 5: return LZ5_hash5Ptr(p, hBits);
    case 6: return LZ5_hash6Ptr(p, hBits);
    case 7: return LZ5_hash7Ptr(p, hBits);
    }
} 

static size_t LZ5_hashPosition(const void* p, int hashLog) 
{
    if (MEM_64bits())
        return LZ5_hash5Ptr(p, hashLog);
    return LZ5_hash4Ptr(p, hashLog);
}



/**************************************
*  Local Macros
**************************************/
#define DELTANEXT(p)        chainTable[(p) & contentMask]
#define LZ5_TRANSFORM_LEVEL 1



/*-************************************
*  Local Utils
**************************************/
int LZ5_versionNumber (void) { return LZ5_VERSION_NUMBER; }
int LZ5_compressBound(int isize)  { return LZ5_COMPRESSBOUND(isize); }
int LZ5_sizeofState_Level1() { return LZ5_sizeofState(LZ5_TRANSFORM_LEVEL); }



int LZ5_compress_extState_Level1(void* state, const char* source, char* dest, int inputSize, int maxOutputSize)
{
    return LZ5_compress_extState(state, source, dest, inputSize, maxOutputSize, LZ5_TRANSFORM_LEVEL);
}

int LZ5_compress_Level1(const char* source, char* dest, int inputSize, int maxOutputSize)
{
    return LZ5_compress(source, dest, inputSize, maxOutputSize, LZ5_TRANSFORM_LEVEL);
}

LZ5_stream_t* LZ5_createStream_Level1(void)
{
    return LZ5_createStream(LZ5_TRANSFORM_LEVEL);
}

LZ5_stream_t* LZ5_resetStream_Level1(LZ5_stream_t* LZ5_stream)
{
    return LZ5_resetStream (LZ5_stream, LZ5_TRANSFORM_LEVEL);
}



/**************************************
*  HC Compression
**************************************/
static void LZ5_init (LZ5_stream_t* ctx, const BYTE* start)
{
    MEM_INIT((void*)ctx->hashTable, 0, ctx->hashTableSize);
    MEM_INIT(ctx->chainTable, 0x01, ctx->chainTableSize);
    ctx->nextToUpdate = LZ5_DICT_SIZE;
    ctx->base = start - LZ5_DICT_SIZE;
    ctx->end = start;
    ctx->dictBase = start - LZ5_DICT_SIZE;
    ctx->dictLimit = LZ5_DICT_SIZE;
    ctx->lowLimit = LZ5_DICT_SIZE;
}


/* Update chains up to ip (excluded) */
FORCE_INLINE void LZ5_Insert (LZ5_stream_t* ctx, const BYTE* ip)
{
    U32* const chainTable = ctx->chainTable;
    U32* const hashTable  = ctx->hashTable;
    const BYTE* const base = ctx->base;
    U32 const target = (U32)(ip - base);
    U32 idx = ctx->nextToUpdate;
    const int hashLog = ctx->params.hashLog;
    const U32 contentMask = (1 << ctx->params.contentLog) - 1;
    const U32 maxDistance = (1 << ctx->params.windowLog) - 1;

    while (idx < target) {
        size_t const h = LZ5_hash4Ptr(base+idx, hashLog);
        size_t delta = idx - hashTable[h];
        if (delta>maxDistance) delta = maxDistance;
        DELTANEXT(idx) = (U32)delta;
        hashTable[h] = idx;
        idx++;
    }

    ctx->nextToUpdate = target;
}

/** LZ5_count_2segments() :
*   can count match length with `ip` & `match` in 2 different segments.
*   convention : on reaching mEnd, match count continue starting from iStart
*/
static size_t LZ5_count_2segments(const BYTE* ip, const BYTE* match, const BYTE* iEnd, const BYTE* mEnd, const BYTE* iStart)
{
    const BYTE* const vEnd = MIN( ip + (mEnd - match), iEnd);
    size_t const matchLength = LZ5_count(ip, match, vEnd);
    if (match + matchLength != mEnd) return matchLength;
    return matchLength + LZ5_count(ip+matchLength, iStart, iEnd);
}

FORCE_INLINE int LZ5_InsertAndFindBestMatch (LZ5_stream_t* ctx,   /* Index table will be updated */
                                               const BYTE* ip, const BYTE* const iLimit,
                                               const BYTE** matchpos)
{
    U32* const chainTable = ctx->chainTable;
    U32* const HashTable = ctx->hashTable;
    const BYTE* const base = ctx->base;
    const BYTE* const dictBase = ctx->dictBase;
    const U32 dictLimit = ctx->dictLimit;
    const BYTE* const lowPrefixPtr = base + dictLimit;
    const BYTE* const dictEnd = dictBase + dictLimit;
    U32 matchIndex, delta;
    const BYTE* match;
    int nbAttempts=ctx->params.searchNum;
    size_t ml=0;
    const int hashLog = ctx->params.hashLog;
    const U32 contentMask = (1 << ctx->params.contentLog) - 1;
    const U32 maxDistance = (1 << ctx->params.windowLog) - 1;
    const U32 lowLimit = (ctx->lowLimit + maxDistance >= (U32)(ip-base)) ? ctx->lowLimit : (U32)(ip - base) - maxDistance;

    /* HC4 match finder */
    LZ5_Insert(ctx, ip);
    matchIndex = HashTable[LZ5_hash4Ptr(ip, hashLog)];

    while ((matchIndex>=lowLimit) && (nbAttempts)) {
        nbAttempts--;
        if (matchIndex >= dictLimit) {
            match = base + matchIndex;
            if (*(match+ml) == *(ip+ml)
                && (MEM_read32(match) == MEM_read32(ip)))
            {
                size_t const mlt = LZ5_count(ip+MINMATCH, match+MINMATCH, iLimit) + MINMATCH;
                if (mlt > ml) { ml = mlt; *matchpos = match; }
            }
        } else {
            match = dictBase + matchIndex;
//            fprintf(stderr, "dictBase[%p]+matchIndex[%d]=match[%p] dictLimit=%d base=%p ip=%p iLimit=%p off=%d\n", dictBase, matchIndex, match, dictLimit, base, ip, iLimit, (U32)(ip-match));
            if ((U32)((dictLimit-1) - matchIndex) >= 3)  /* intentional overflow */
            if (MEM_read32(match) == MEM_read32(ip)) {
#if 1
                size_t mlt = LZ5_count_2segments(ip+MINMATCH, match+MINMATCH, iLimit, dictEnd, lowPrefixPtr) + MINMATCH;
#else
                size_t mlt;
                const BYTE* vLimit = ip + (dictLimit - matchIndex);
                if (vLimit > iLimit) vLimit = iLimit;
                mlt = LZ5_count(ip+MINMATCH, match+MINMATCH, vLimit) + MINMATCH;
                if ((ip+mlt == vLimit) && (vLimit < iLimit))
                    mlt += LZ5_count(ip+mlt, base+dictLimit, iLimit);
#endif
                if (mlt > ml) { ml = mlt; *matchpos = base + matchIndex; }   /* virtual matchpos */
            }
        }
        delta = DELTANEXT(matchIndex);
        if (delta > matchIndex) break;
        matchIndex -= delta;
    }

    return (int)ml;
}


FORCE_INLINE int LZ5_InsertAndGetWiderMatch (
    LZ5_stream_t* ctx,
    const BYTE* const ip,
    const BYTE* const iLowLimit,
    const BYTE* const iHighLimit,
    int longest,
    const BYTE** matchpos,
    const BYTE** startpos)
{
    U32* const chainTable = ctx->chainTable;
    U32* const HashTable = ctx->hashTable;
    const BYTE* const base = ctx->base;
    const U32 dictLimit = ctx->dictLimit;
    const BYTE* const lowPrefixPtr = base + dictLimit;
    const BYTE* const dictBase = ctx->dictBase;
    const BYTE* const dictEnd = dictBase + dictLimit;
    U32   matchIndex, delta;
    int nbAttempts = ctx->params.searchNum;
    int LLdelta = (int)(ip-iLowLimit);
    const int hashLog = ctx->params.hashLog;
    const U32 contentMask = (1 << ctx->params.contentLog) - 1;
    const U32 maxDistance = (1 << ctx->params.windowLog) - 1;
    const U32 lowLimit = (ctx->lowLimit + maxDistance >= (U32)(ip-base)) ? ctx->lowLimit : (U32)(ip - base) - maxDistance;
    
    /* First Match */
    LZ5_Insert(ctx, ip);
    matchIndex = HashTable[LZ5_hash4Ptr(ip, hashLog)];

    while ((matchIndex>=lowLimit) && (nbAttempts)) {
        nbAttempts--;
        if (matchIndex >= dictLimit) {
            const BYTE* matchPtr = base + matchIndex;
            if (*(iLowLimit + longest) == *(matchPtr - LLdelta + longest)) {
                if (MEM_read32(matchPtr) == MEM_read32(ip)) {
                    int mlt = MINMATCH + LZ5_count(ip+MINMATCH, matchPtr+MINMATCH, iHighLimit);
                    int back = 0;

                    while ((ip+back > iLowLimit)
                           && (matchPtr+back > lowPrefixPtr)
                           && (ip[back-1] == matchPtr[back-1]))
                            back--;

                    mlt -= back;

                    if (mlt > longest) {
                        longest = (int)mlt;
                        *matchpos = matchPtr+back;
                        *startpos = ip+back;
                    }
                }
            }
        } else {
            const BYTE* matchPtr = dictBase + matchIndex;
            if ((U32)((dictLimit-1) - matchIndex) >= 3)  /* intentional overflow */
            if (MEM_read32(matchPtr) == MEM_read32(ip)) {
                int back=0;
#if 1
                size_t mlt = LZ5_count_2segments(ip+MINMATCH, matchPtr+MINMATCH, iHighLimit, dictEnd, lowPrefixPtr) + MINMATCH;
#else
                size_t mlt;
                const BYTE* vLimit = ip + (dictLimit - matchIndex);
                if (vLimit > iHighLimit) vLimit = iHighLimit;
                mlt = LZ5_count(ip+MINMATCH, matchPtr+MINMATCH, vLimit) + MINMATCH;
                if ((ip+mlt == vLimit) && (vLimit < iHighLimit))
                    mlt += LZ5_count(ip+mlt, base+dictLimit, iHighLimit);
#endif
                while ((ip+back > iLowLimit) && (matchIndex+back > lowLimit) && (ip[back-1] == matchPtr[back-1])) back--;
                mlt -= back;
                if ((int)mlt > longest) { longest = (int)mlt; *matchpos = base + matchIndex + back; *startpos = ip+back; }
            }
        }
        delta = DELTANEXT(matchIndex);
        if (delta > matchIndex) break;
        matchIndex -= delta;
    }

    return longest;
}


#define LZ5_DEBUG 0
#if LZ5_DEBUG
static unsigned debug = 0;
#endif

FORCE_INLINE int LZ5_encodeLastLiterals (
    const BYTE** ip,
    BYTE** op,
    const BYTE** anchor,
    limitedOutput_directive limitedOutputBuffer,
    BYTE* oend)
{
    int length = (int)(*ip - *anchor);
    BYTE* token = (*op)++;

//    printf("LZ5_encodeLastLiterals length=%d (length>>8)=%d (length+255-RUN_MASK)/255=%d oend-op=%d\n", (int)length, (int)(length>>8), (int)(length+255-RUN_MASK)/255, (int)(oend-*op));
    if ((limitedOutputBuffer) && (*op + length + ((length+255-RUN_MASK)/255) > oend)) return 1; /* Check output buffer overflow */
    if (length>=(int)RUN_MASK) { int len; *token=(RUN_MASK<<ML_BITS); len = length-RUN_MASK; for(; len > 254 ; len-=255) *(*op)++ = 255;  *(*op)++ = (BYTE)len; }
    else *token = (BYTE)(length<<ML_BITS);
    memcpy(*op, *anchor, length);
    *op += length;
    return 0;
}

FORCE_INLINE int LZ5_encodeSequence (
    LZ5_stream_t* ctx,
    const BYTE** ip,
    BYTE** op,
    const BYTE** anchor,
    size_t matchLength,
    const BYTE* const match,
    limitedOutput_directive limitedOutputBuffer,
    BYTE* oend)
{
    int length = (int)(*ip - *anchor);
    BYTE* token = (*op)++;

#if LZ5_DEBUG
    if (debug) printf("literal : %u  --  match : %u  --  offset : %u\n", (U32)(*ip - *anchor), (U32)matchLength, (U32)(*ip-match));
#endif
  
    /* Encode Literal length */
    if ((limitedOutputBuffer) && ((*op + (length>>8) + length + (2 + 1 + LASTLITERALS)) > oend)) return 1;   /* Check output limit */
    if (length>=(int)RUN_MASK) { int len; *token=(RUN_MASK<<ML_BITS); len = length-RUN_MASK; for(; len > 254 ; len-=255) *(*op)++ = 255;  *(*op)++ = (BYTE)len; }
    else *token = (BYTE)(length<<ML_BITS);

    /* Copy Literals */
    LZ5_wildCopy(*op, *anchor, (*op) + length);
    *op += length;

    /* Encode Offset */
    if (ctx->params.windowLog <= 16) {
        MEM_writeLE16(*op, (U16)(*ip-match));
        *op+=2;
    } else {
        MEM_writeLE24(*op, (U32)(*ip-match));
        *op+=3;
    }

    /* Encode MatchLength */
    length = (int)(matchLength-MINMATCH);
    if ((limitedOutputBuffer) && (*op + (length>>8) + (1 + LASTLITERALS) > oend)) return 1;   /* Check output limit */
    if (length>=(int)ML_MASK) {
        *token += ML_MASK;
        length -= ML_MASK;
#if 1
        MEM_write32(*op, 0xFFFFFFFF);
        while (length >= 4*255) *op+=4, MEM_write32(*op, 0xFFFFFFFF), length -= 4*255;
        *op += length / 255;
        *(*op)++ = (BYTE)(length % 255);
#else        
        for(; length > 509 ; length-=510) { *(*op)++ = 255; *(*op)++ = 255; }
        if (length > 254) { length-=255; *(*op)++ = 255; }
        *(*op)++ = (BYTE)length;
#endif
    } else {
        *token += (BYTE)(length);
    }

    /* Prepare next loop */
    *ip += matchLength;
    *anchor = *ip;

    return 0;
}


/*-******************************
*  Compression functions
********************************/
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


int LZ5_compress_nochain(
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

    (void)LZ5_hashPtr;
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
        if (LZ5_encodeSequence(ctx, &ip, &op, &anchor, matchLength+MINMATCH, match, outputLimited, oend)) goto _output_error;

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
                if (MEM_read32(match) == MEM_read32(ip))
                {
                    matchLength = LZ5_count(ip+MINMATCH, match+MINMATCH, matchlimit);
                   goto _next_match;
                }
            } else {
                match = dictBase + matchIndex;
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
    if (LZ5_encodeLastLiterals(&ip, &op, &anchor, outputLimited, oend)) goto _output_error;

    /* End */
    return (int) (((char*)op)-dest);
_output_error:
    return 0;
}


int LZ5_compress_HC (
     void* const ctxvoid,
     const char* const source,
     char* const dest,
     const int inputSize,
     const int maxOutputSize,
     const limitedOutput_directive outputLimited)
{
    LZ5_stream_t* ctx = (LZ5_stream_t*) ctxvoid;
    const BYTE* ip = (const BYTE*) source;
    const BYTE* anchor = ip;
    const BYTE* const iend = ip + inputSize;
    const BYTE* const mflimit = iend - MFLIMIT;
    const BYTE* const matchlimit = (iend - LASTLITERALS);

    BYTE* op = (BYTE*) dest;
    BYTE* const oend = op + maxOutputSize;

    int   ml, ml2, ml3, ml0;
    const BYTE* ref = NULL;
    const BYTE* start2 = NULL;
    const BYTE* ref2 = NULL;
    const BYTE* start3 = NULL;
    const BYTE* ref3 = NULL;
    const BYTE* start0;
    const BYTE* ref0;

    /* init */
    ctx->end += inputSize;

    ip++;

    /* Main Loop */
    while (ip < mflimit) {
        ml = LZ5_InsertAndFindBestMatch (ctx, ip, matchlimit, (&ref));
        if (!ml) { ip++; continue; }

        /* saved, in case we would skip too much */
        start0 = ip;
        ref0 = ref;
        ml0 = ml;

_Search2:
        if (ip+ml < mflimit)
            ml2 = LZ5_InsertAndGetWiderMatch(ctx, ip + ml - 2, ip + 1, matchlimit, ml, &ref2, &start2);
        else ml2 = ml;

        if (ml2 == ml) { /* No better match */
            if (LZ5_encodeSequence(ctx, &ip, &op, &anchor, ml, ref, outputLimited, oend)) return 0;
            continue;
        }

        if (start0 < ip) {
            if (start2 < ip + ml0) {  /* empirical */
                ip = start0;
                ref = ref0;
                ml = ml0;
            }
        }

        /* Here, start0==ip */
        if ((start2 - ip) < 3) {  /* First Match too small : removed */
            ml = ml2;
            ip = start2;
            ref =ref2;
            goto _Search2;
        }

_Search3:
        /*
        * Currently we have :
        * ml2 > ml1, and
        * ip1+3 <= ip2 (usually < ip1+ml1)
        */
        if ((start2 - ip) < OPTIMAL_ML) {
            int correction;
            int new_ml = ml;
            if (new_ml > OPTIMAL_ML) new_ml = OPTIMAL_ML;
            if (ip+new_ml > start2 + ml2 - MINMATCH) new_ml = (int)(start2 - ip) + ml2 - MINMATCH;
            correction = new_ml - (int)(start2 - ip);
            if (correction > 0) {
                start2 += correction;
                ref2 += correction;
                ml2 -= correction;
            }
        }
        /* Now, we have start2 = ip+new_ml, with new_ml = min(ml, OPTIMAL_ML=18) */

        if (start2 + ml2 < mflimit)
            ml3 = LZ5_InsertAndGetWiderMatch(ctx, start2 + ml2 - 3, start2, matchlimit, ml2, &ref3, &start3);
        else ml3 = ml2;

        if (ml3 == ml2) {  /* No better match : 2 sequences to encode */
            /* ip & ref are known; Now for ml */
            if (start2 < ip+ml)  ml = (int)(start2 - ip);
            /* Now, encode 2 sequences */
            if (LZ5_encodeSequence(ctx, &ip, &op, &anchor, ml, ref, outputLimited, oend)) return 0;
            ip = start2;
            if (LZ5_encodeSequence(ctx, &ip, &op, &anchor, ml2, ref2, outputLimited, oend)) return 0;
            continue;
        }

        if (start3 < ip+ml+3) {  /* Not enough space for match 2 : remove it */
            if (start3 >= (ip+ml)) {  /* can write Seq1 immediately ==> Seq2 is removed, so Seq3 becomes Seq1 */
                if (start2 < ip+ml) {
                    int correction = (int)(ip+ml - start2);
                    start2 += correction;
                    ref2 += correction;
                    ml2 -= correction;
                    if (ml2 < MINMATCH) {
                        start2 = start3;
                        ref2 = ref3;
                        ml2 = ml3;
                    }
                }

                if (LZ5_encodeSequence(ctx, &ip, &op, &anchor, ml, ref, outputLimited, oend)) return 0;
                ip  = start3;
                ref = ref3;
                ml  = ml3;

                start0 = start2;
                ref0 = ref2;
                ml0 = ml2;
                goto _Search2;
            }

            start2 = start3;
            ref2 = ref3;
            ml2 = ml3;
            goto _Search3;
        }

        /*
        * OK, now we have 3 ascending matches; let's write at least the first one
        * ip & ref are known; Now for ml
        */
        if (start2 < ip+ml) {
            if ((start2 - ip) < (int)ML_MASK) {
                int correction;
                if (ml > OPTIMAL_ML) ml = OPTIMAL_ML;
                if (ip + ml > start2 + ml2 - MINMATCH) ml = (int)(start2 - ip) + ml2 - MINMATCH;
                correction = ml - (int)(start2 - ip);
                if (correction > 0) {
                    start2 += correction;
                    ref2 += correction;
                    ml2 -= correction;
                }
            } else {
                ml = (int)(start2 - ip);
            }
        }
        if (LZ5_encodeSequence(ctx, &ip, &op, &anchor, ml, ref, outputLimited, oend)) return 0;

        ip = start2;
        ref = ref2;
        ml = ml2;

        start2 = start3;
        ref2 = ref3;
        ml2 = ml3;

        goto _Search3;
    }

    /* Encode Last Literals */
    {   int lastRun = (int)(iend - anchor);
        if ((outputLimited) && (((char*)op - dest) + lastRun + 1 + ((lastRun+255-RUN_MASK)/255) > (U32)maxOutputSize)) return 0;  /* Check output limit */
        if (lastRun>=(int)RUN_MASK) { *op++=(RUN_MASK<<ML_BITS); lastRun-=RUN_MASK; for(; lastRun > 254 ; lastRun-=255) *op++ = 255; *op++ = (BYTE) lastRun; }
        else *op++ = (BYTE)(lastRun<<ML_BITS);
        memcpy(op, anchor, iend - anchor);
        op += iend-anchor;
    }

    /* End */
    return (int) (((char*)op)-dest);
}


static int LZ5_compress_generic (
    void* ctxvoid,
    const char* source,
    char* dest,
    int inputSize,
    int maxOutputSize,
    limitedOutput_directive limit)
{
    LZ5_stream_t* ctx = (LZ5_stream_t*) ctxvoid;
    int res;
    
    *dest++ = (BYTE)ctx->compressionLevel;
    maxOutputSize--; // can be lower than 0
    res = ctx->params.compressFunc(ctxvoid, source, dest, inputSize, maxOutputSize, limit);
 //   printf("LZ5_compress_generic inputSize=%d maxOutputSize=%d res=%d compressionLevel=%d limit=%d\n", inputSize, maxOutputSize, res, ctx->compressionLevel, limit);
    return (res > 0) ? res+1 : res;
}


int LZ5_compress(const char* src, char* dst, int srcSize, int maxDstSize, int compressionLevel)
{
    int cSize;
    LZ5_stream_t* statePtr = LZ5_createStream(compressionLevel);

    if (!statePtr) return 0;
    cSize = LZ5_compress_extState(statePtr, src, dst, srcSize, maxDstSize, compressionLevel);

    LZ5_freeStream(statePtr);
    return cSize;
}


/**************************************
*  Streaming Functions
**************************************/
int LZ5_verifyCompressionLevel(int compressionLevel)
{
    if (compressionLevel > LZ5_MAX_CLEVEL) compressionLevel = LZ5_MAX_CLEVEL;
    if (compressionLevel < 1) compressionLevel = LZ5_DEFAULT_CLEVEL;
    return compressionLevel;
}


int LZ5_sizeofState(int compressionLevel) 
{ 
    LZ5_parameters params;
    U32 hashTableSize, chainTableSize;

    compressionLevel = LZ5_verifyCompressionLevel(compressionLevel);
    params = LZ5_defaultParameters[compressionLevel];
    hashTableSize = (U32)(sizeof(U32)*(((size_t)1 << params.hashLog3)+((size_t)1 << params.hashLog)));
    chainTableSize = (U32)(sizeof(U32)*((size_t)1 << params.contentLog));

    return sizeof(LZ5_stream_t) + hashTableSize + chainTableSize;
}


/* if ctx==NULL memory is allocated and returned as value */
LZ5_stream_t* LZ5_initStream(LZ5_stream_t* ctx, int compressionLevel) 
{ 
    LZ5_parameters params;
    U32 hashTableSize, chainTableSize;

    compressionLevel = LZ5_verifyCompressionLevel(compressionLevel);
    params = LZ5_defaultParameters[compressionLevel];
    hashTableSize = (U32)(sizeof(U32)*(((size_t)1 << params.hashLog3)+((size_t)1 << params.hashLog)));
    chainTableSize = (U32)(sizeof(U32)*((size_t)1 << params.contentLog));
    
    if (!ctx)
    {
        ctx = (LZ5_stream_t*)malloc(sizeof(LZ5_stream_t) + hashTableSize + chainTableSize);
        if (!ctx) return 0;
    }
    
    ctx->hashTable = (U32*)ctx + sizeof(LZ5_stream_t)/4;
    ctx->hashTableSize = hashTableSize;
    ctx->chainTable = ctx->hashTable + hashTableSize/4;
    ctx->chainTableSize = chainTableSize;
    ctx->params = params;
    ctx->compressionLevel = (unsigned)compressionLevel;

    return ctx;
}


int LZ5_compress_extState (void* state, const char* src, char* dst, int srcSize, int maxDstSize, int compressionLevel)
{
    LZ5_stream_t* ctx = (LZ5_stream_t*) state;
    if (((size_t)(state)&(sizeof(void*)-1)) != 0) return 0;   /* Error : state is not aligned for pointers (32 or 64 bits) */

    /* initialize stream */
    LZ5_initStream(ctx, compressionLevel);
    LZ5_init ((LZ5_stream_t*)state, (const BYTE*)src);

    if (maxDstSize < LZ5_compressBound(srcSize))
        return LZ5_compress_generic (state, src, dst, srcSize, maxDstSize, limitedOutput);
    else
        return LZ5_compress_generic (state, src, dst, srcSize, maxDstSize, noLimit);
}


LZ5_stream_t* LZ5_createStream(int compressionLevel) 
{ 
    LZ5_stream_t* ctx = LZ5_initStream(NULL, compressionLevel);
//    if (ctx) printf("LZ5_createStream ctx=%p ctx->compressionLevel=%d\n", ctx, ctx->compressionLevel);
    return ctx; 
}


/* initialization */
LZ5_stream_t* LZ5_resetStream (LZ5_stream_t* ctx, int compressionLevel)
{
    size_t wanted = LZ5_sizeofState(compressionLevel);
    size_t have = sizeof(LZ5_stream_t) + ctx->hashTableSize + ctx->chainTableSize;

//    printf("LZ5_resetStream ctx=%p cLevel=%d have=%d wanted=%d min=%d\n", ctx, compressionLevel, (int)have, (int)wanted, (int)sizeof(LZ5_stream_t));
    if (have < wanted)
    {
  //      printf("REALLOC ctx=%p cLevel=%d have=%d wanted=%d\n", ctx, compressionLevel, (int)have, (int)wanted);
        LZ5_freeStream(ctx);
        ctx = LZ5_createStream(compressionLevel);
    }
    else
    {
        ctx->base = NULL;
        ctx->compressionLevel = (unsigned)LZ5_verifyCompressionLevel(compressionLevel);
        ctx->params = LZ5_defaultParameters[ctx->compressionLevel];
    }

    if (ctx) ctx->base = NULL;
    return ctx;
}


int LZ5_freeStream (LZ5_stream_t* ctx) 
{ 
//    printf("LZ5_freeStream ctx=%p ctx->compressionLevel=%d\n", ctx, ctx->compressionLevel);
    free(ctx); 
    return 0; 
}


int LZ5_loadDict (LZ5_stream_t* LZ5_streamPtr, const char* dictionary, int dictSize)
{
    LZ5_stream_t* ctxPtr = (LZ5_stream_t*) LZ5_streamPtr;
    if (dictSize > LZ5_DICT_SIZE) {
        dictionary += dictSize - LZ5_DICT_SIZE;
        dictSize = LZ5_DICT_SIZE;
    }
    LZ5_init (ctxPtr, (const BYTE*)dictionary);
    if (dictSize >= 4) LZ5_Insert (ctxPtr, (const BYTE*)dictionary +(dictSize-3));
    ctxPtr->end = (const BYTE*)dictionary + dictSize;
    return dictSize;
}


/* compression */

static void LZ5_setExternalDict(LZ5_stream_t* ctxPtr, const BYTE* newBlock)
{
    if (ctxPtr->end >= ctxPtr->base + 4) LZ5_Insert (ctxPtr, ctxPtr->end-3);   /* Referencing remaining dictionary content */
    /* Only one memory segment for extDict, so any previous extDict is lost at this stage */
    ctxPtr->lowLimit  = ctxPtr->dictLimit;
    ctxPtr->dictLimit = (U32)(ctxPtr->end - ctxPtr->base);
    ctxPtr->dictBase  = ctxPtr->base;
    ctxPtr->base = newBlock - ctxPtr->dictLimit;
    ctxPtr->end  = newBlock;
    ctxPtr->nextToUpdate = ctxPtr->dictLimit;   /* match referencing will resume from there */
}

static int LZ5_compress_continue_generic (LZ5_stream_t* ctxPtr,
                                            const char* source, char* dest,
                                            int inputSize, int maxOutputSize, limitedOutput_directive limit)
{
    /* auto-init if forgotten */
    if (ctxPtr->base == NULL) LZ5_init (ctxPtr, (const BYTE*) source);

    /* Check overflow */
    if ((size_t)(ctxPtr->end - ctxPtr->base) > 2 GB) {
        size_t dictSize = (size_t)(ctxPtr->end - ctxPtr->base) - ctxPtr->dictLimit;
        if (dictSize > LZ5_DICT_SIZE) dictSize = LZ5_DICT_SIZE;
        LZ5_loadDict((LZ5_stream_t*)ctxPtr, (const char*)(ctxPtr->end) - dictSize, (int)dictSize);
    }

    /* Check if blocks follow each other */
    if ((const BYTE*)source != ctxPtr->end) 
        LZ5_setExternalDict(ctxPtr, (const BYTE*)source);

    /* Check overlapping input/dictionary space */
    {   const BYTE* sourceEnd = (const BYTE*) source + inputSize;
        const BYTE* const dictBegin = ctxPtr->dictBase + ctxPtr->lowLimit;
        const BYTE* const dictEnd   = ctxPtr->dictBase + ctxPtr->dictLimit;
        if ((sourceEnd > dictBegin) && ((const BYTE*)source < dictEnd)) {
            if (sourceEnd > dictEnd) sourceEnd = dictEnd;
            ctxPtr->lowLimit = (U32)(sourceEnd - ctxPtr->dictBase);
            if (ctxPtr->dictLimit - ctxPtr->lowLimit < 4) ctxPtr->lowLimit = ctxPtr->dictLimit;
        }
    }

    return LZ5_compress_generic (ctxPtr, source, dest, inputSize, maxOutputSize, limit);
}

int LZ5_compress_continue (LZ5_stream_t* LZ5_streamPtr, const char* source, char* dest, int inputSize, int maxOutputSize)
{
    if (maxOutputSize < LZ5_compressBound(inputSize))
        return LZ5_compress_continue_generic ((LZ5_stream_t*)LZ5_streamPtr, source, dest, inputSize, maxOutputSize, limitedOutput);
    else
        return LZ5_compress_continue_generic ((LZ5_stream_t*)LZ5_streamPtr, source, dest, inputSize, maxOutputSize, noLimit);
}


/* dictionary saving */

int LZ5_saveDict (LZ5_stream_t* LZ5_streamPtr, char* safeBuffer, int dictSize)
{
    LZ5_stream_t* const streamPtr = (LZ5_stream_t*)LZ5_streamPtr;
    int const prefixSize = (int)(streamPtr->end - (streamPtr->base + streamPtr->dictLimit));
    if (dictSize > LZ5_DICT_SIZE) dictSize = LZ5_DICT_SIZE;
    if (dictSize < 4) dictSize = 0;
    if (dictSize > prefixSize) dictSize = prefixSize;
    memmove(safeBuffer, streamPtr->end - dictSize, dictSize);
    {   U32 const endIndex = (U32)(streamPtr->end - streamPtr->base);
        streamPtr->end = (const BYTE*)safeBuffer + dictSize;
        streamPtr->base = streamPtr->end - endIndex;
        streamPtr->dictLimit = endIndex - dictSize;
        streamPtr->lowLimit = endIndex - dictSize;
        if (streamPtr->nextToUpdate < streamPtr->dictLimit) streamPtr->nextToUpdate = streamPtr->dictLimit;
    }
    return dictSize;
}

