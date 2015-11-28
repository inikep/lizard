/*
    LZ5 HC - High Compression Mode of LZ5
    Copyright (C) 2011-2015, Yann Collet.
    Copyright (C) 2015, Przemyslaw Skibinski <inikep@gmail.com>

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
       - LZ5 public forum : https://groups.google.com/forum/#!forum/lz5c
*/




/* *************************************
*  Tuning Parameter
***************************************/
static const int LZ5HC_compressionLevel_default = 9;

/*!
 * HEAPMODE :
 * Select how default compression function will allocate workplace memory,
 * in stack (0:fastest), or in heap (1:requires malloc()).
 * Since workplace is rather large, heap mode is recommended.
 */
#define LZ5HC_HEAPMODE 0


/* *************************************
*  Includes
***************************************/
#include "lz5hc.h"
#include <stdio.h>


/* *************************************
*  Local Compiler Options
***************************************/
#if defined(__GNUC__)
#  pragma GCC diagnostic ignored "-Wunused-function"
#endif

#if defined (__clang__)
#  pragma clang diagnostic ignored "-Wunused-function"
#endif



/* *************************************
*  Common LZ5 definition
***************************************/
#define LZ5_COMMONDEFS_ONLY
#include "lz5.c"


/* *************************************
*  Local Constants
***************************************/
#define DICTIONARY_LOGSIZE 22
#define MAXD (1<<DICTIONARY_LOGSIZE)
#define MAXD_MASK (MAXD - 1)

#define HASH_LOG (DICTIONARY_LOGSIZE+1)
#define HASH_LOG3 16
#define HASHTABLESIZE (1 << HASH_LOG)
#define HASHTABLESIZE3 (1 << HASH_LOG3)

#define LZ5_SHORT_LITERALS          ((1<<RUN_BITS2)-1)
#define LZ5_LITERALS                ((1<<RUN_BITS)-1)

static const int g_maxCompressionLevel = 12;



/**************************************
*  Local Types
**************************************/
struct LZ5HC_Data_s
{
    U32*   hashTable;
    U32*   hashTable3;
    U32*   chainTable;
    const BYTE* end;        /* next block here to continue on current prefix */
    const BYTE* base;       /* All index relative to this position */
    const BYTE* dictBase;   /* alternate base for extDict */
    BYTE* inputBuffer;      /* deprecated */
    BYTE* outputBuffer;     /* deprecated */
    U32   dictLimit;        /* below that point, need extDict */
    U32   lowLimit;         /* below that point, no more dict */
    U32   nextToUpdate;     /* index from which to continue dictionary update */
    U32   compressionLevel;
    U32   last_off;
};

/**************************************
*  Local Macros
**************************************/
#if MINMATCH == 3
    #define LZ5_read24(ptr) (uint32_t)(LZ5_read32(ptr)<<8) 
#else
    #define LZ5_read24(ptr) (uint32_t)(LZ5_read32(ptr)) 
#endif

#define MAX(a,b) ((a)>(b))?(a):(b)
#define HASH_FUNCTION(i)       (((i) * 2654435761U) >> ((32)-HASH_LOG))
#define HASH_FUNCTION3(i)       (((i) * 506832829U) >> ((32)-HASH_LOG3))

static U32 LZ5HC_hashPtr(const void* ptr) { return HASH_FUNCTION(LZ5_read32(ptr)); }
static U32 LZ5HC_hashPtr3(const void* ptr) { return HASH_FUNCTION3(LZ5_read24(ptr)); }

#define LZ5HC_LIMIT (1<<(DICTIONARY_LOGSIZE))


#define LZ5HC_DEBUG(fmt, args...) ; //printf(fmt, ##args)

#define LZ5_SHORT_LITLEN_COST(len)  (len<LZ5_SHORT_LITERALS ? 0 : (len-LZ5_SHORT_LITERALS < 255 ? 1 : (len-LZ5_SHORT_LITERALS-255 < (1<<7) ? 2 : 3)))
#define LZ5_LEN_COST(len)           (len<LZ5_LITERALS ? 0 : (len-LZ5_LITERALS < 255 ? 1 : (len-LZ5_LITERALS-255 < (1<<7) ? 2 : 3)))

#define LZ5_LIT_COST(len,offset)                ((len)+((((offset) > LZ5_MID_OFFSET_DISTANCE) || ((offset)<LZ5_SHORT_OFFSET_DISTANCE)) ? LZ5_SHORT_LITLEN_COST(len) : LZ5_LEN_COST(len)))
#define LZ5_MATCH_COST(mlen,offset)             (LZ5_LEN_COST(mlen) + (((offset) == 0) ? 1 : ((offset)<LZ5_SHORT_OFFSET_DISTANCE ? 2 : ((offset)<(1 << 16) ? 3 : 4))))
#define LZ5_CODEWORD_COST(litlen,offset,mlen)   (LZ5_MATCH_COST(mlen,offset) + LZ5_LIT_COST(litlen,offset))
#define LZ5_LIT_ONLY_COST(len)                  ((len)+(LZ5_LEN_COST(len)))

#define LZ5_NORMAL_MATCH_COST(mlen,offset)  (LZ5_MATCH_COST(mlen,offset))
#define LZ5_NORMAL_LIT_COST(len)            (len)



/**************************************
*  HC Compression
**************************************/

FORCE_INLINE int LZ5_MORE_PROFITABLE(uint32_t best_off, uint32_t best_common, uint32_t off, uint32_t common, int literals, uint32_t last_off)
{
	int sum;
	
	if (literals > 0)
		sum = MAX(common + literals, best_common);
	else
		sum = MAX(common, best_common - literals);
	
//	return LZ5_CODEWORD_COST(sum - common, (off == last_off) ? 0 : (off), common - MINMATCH) <= LZ5_CODEWORD_COST(sum - best_common, (best_off == last_off) ? 0 : (best_off), best_common - MINMATCH);
	return LZ5_NORMAL_MATCH_COST(common - MINMATCH, (off == last_off) ? 0 : (off)) + LZ5_NORMAL_LIT_COST(sum - common) <= LZ5_NORMAL_MATCH_COST(best_common - MINMATCH, (best_off == last_off) ? 0 : (best_off)) + LZ5_NORMAL_LIT_COST(sum - best_common);
}


static void LZ5HC_init (LZ5HC_Data_Structure* ctx, const BYTE* start)
{
    MEM_INIT((void*)ctx->hashTable, 0, sizeof(U32)*HASHTABLESIZE);
    MEM_INIT((void*)ctx->hashTable3, 0, sizeof(U32)*HASHTABLESIZE3);
    MEM_INIT(ctx->chainTable, 0xFF, sizeof(U32)*MAXD);
    ctx->nextToUpdate = LZ5HC_LIMIT;
    ctx->base = start - LZ5HC_LIMIT;
    ctx->end = start;
    ctx->dictBase = start - LZ5HC_LIMIT;
    ctx->dictLimit = LZ5HC_LIMIT;
    ctx->lowLimit = LZ5HC_LIMIT;
    ctx->last_off = 1;
}


/* Update chains up to ip (excluded) */
FORCE_INLINE void LZ5HC_Insert (LZ5HC_Data_Structure* ctx, const BYTE* ip)
{
    U32* chainTable = ctx->chainTable;
    U32* HashTable  = ctx->hashTable;
#if MINMATCH == 3
    U32* HashTable3  = ctx->hashTable3;
#endif 
    const BYTE* const base = ctx->base;
    const U32 target = (U32)(ip - base);
    U32 idx = ctx->nextToUpdate;

    while(idx < target)
    {
        U32 h = LZ5HC_hashPtr(base+idx);
        chainTable[idx & MAXD_MASK] = (U32)(idx - HashTable[h]);
        HashTable[h] = idx;
#if MINMATCH == 3
        HashTable3[LZ5HC_hashPtr3(base+idx)] = idx;
#endif 
       idx++;
    }

    ctx->nextToUpdate = target;
}


    
FORCE_INLINE int LZ5HC_InsertAndFindBestMatch (LZ5HC_Data_Structure* ctx,   /* Index table will be updated */
                                               const BYTE* ip, const BYTE* const iLimit,
                                               const BYTE** matchpos,
                                               const int maxNbAttempts)
{
    U32* const chainTable = ctx->chainTable;
    U32* const HashTable = ctx->hashTable;
    const BYTE* const base = ctx->base;
    const BYTE* const dictBase = ctx->dictBase;
    const U32 dictLimit = ctx->dictLimit;
    const U32 lowLimit = (ctx->lowLimit + LZ5HC_LIMIT > (U32)(ip-base)) ? ctx->lowLimit : (U32)(ip - base) - (LZ5HC_LIMIT - 1);
    U32 matchIndex;
    const BYTE* match;
    int nbAttempts=maxNbAttempts;
    size_t ml=0, mlt;

    /* HC4 match finder */
    LZ5HC_Insert(ctx, ip);
    matchIndex = HashTable[LZ5HC_hashPtr(ip)];

    match = ip - ctx->last_off;
    if (LZ5_read24(match) == LZ5_read24(ip))
    {
        ml = LZ5_count(ip+MINMATCH, match+MINMATCH, iLimit) + MINMATCH;
        *matchpos = match;
        return (int)ml;
    }

#if MINMATCH == 3
    size_t offset = ip - base - ctx->hashTable3[LZ5HC_hashPtr3(ip)];
    if (offset > 0 && offset < LZ5_SHORT_OFFSET_DISTANCE)
    {
        match = ip - offset;
        if (match > base && LZ5_read24(ip) == LZ5_read24(match))
        {
            ml = 3;//LZ5_count(ip+MINMATCH, match+MINMATCH, iLimit) + MINMATCH;
            *matchpos = match;
        }
    }
#endif

    while ((matchIndex>=lowLimit) && (nbAttempts))
    {
        nbAttempts--;
        if (matchIndex >= dictLimit)
        {
            match = base + matchIndex;
            if (match < ip && *(match+ml) == *(ip+ml) && (LZ5_read32(match) == LZ5_read32(ip)))
            {
                mlt = LZ5_count(ip+MINMATCH, match+MINMATCH, iLimit) + MINMATCH;
                if (mlt > ml)
                if (LZ5_NORMAL_MATCH_COST(mlt - MINMATCH, (ip - match == ctx->last_off) ? 0 : (ip - match)) < LZ5_NORMAL_MATCH_COST(ml - MINMATCH, (ip - *matchpos == ctx->last_off) ? 0 : (ip - *matchpos)) + (LZ5_NORMAL_LIT_COST(mlt - ml)))
                { ml = mlt; *matchpos = match; }
            }
        }
        else
        {
            match = dictBase + matchIndex;
            if (LZ5_read32(match) == LZ5_read32(ip))
            {
                const BYTE* vLimit = ip + (dictLimit - matchIndex);
                if (vLimit > iLimit) vLimit = iLimit;
                mlt = LZ5_count(ip+MINMATCH, match+MINMATCH, vLimit) + MINMATCH;
                if ((ip+mlt == vLimit) && (vLimit < iLimit))
                    mlt += LZ5_count(ip+mlt, base+dictLimit, iLimit);
                if (mlt > ml) 
                if (LZ5_NORMAL_MATCH_COST(mlt - MINMATCH, (ip - match == ctx->last_off) ? 0 : (ip - match)) < LZ5_NORMAL_MATCH_COST(ml - MINMATCH, (ip - *matchpos == ctx->last_off) ? 0 : (ip - *matchpos)) + (LZ5_NORMAL_LIT_COST(mlt - ml)))
                { ml = mlt; *matchpos = base + matchIndex; }   /* virtual matchpos */
            }
        }
        matchIndex -= chainTable[matchIndex & MAXD_MASK];
    }

    return (int)ml;
}



FORCE_INLINE int LZ5HC_InsertAndGetWiderMatch (
    LZ5HC_Data_Structure* ctx,
    const BYTE* const ip,
    const BYTE* const iLowLimit,
    const BYTE* const iHighLimit,
    int longest,
    const BYTE** matchpos,
    const BYTE** startpos,
    const int maxNbAttempts)
{
    U32* const chainTable = ctx->chainTable;
    U32* const HashTable = ctx->hashTable;
    const BYTE* const base = ctx->base;
    const U32 dictLimit = ctx->dictLimit;
    const BYTE* const lowPrefixPtr = base + dictLimit;
    const U32 lowLimit = (ctx->lowLimit + LZ5HC_LIMIT > (U32)(ip-base)) ? ctx->lowLimit : (U32)(ip - base) - (LZ5HC_LIMIT - 1);
    const BYTE* const dictBase = ctx->dictBase;
    const BYTE* match;
    U32   matchIndex;
    int nbAttempts = maxNbAttempts;


    /* First Match */
    LZ5HC_Insert(ctx, ip);
    matchIndex = HashTable[LZ5HC_hashPtr(ip)];

    match = ip - ctx->last_off;
    if (LZ5_read24(match) == LZ5_read24(ip))
    {
        int mlt = LZ5_count(ip+MINMATCH, match+MINMATCH, iHighLimit) + MINMATCH;
        
        int back = 0;
        while ((ip+back>iLowLimit) && (match+back > lowPrefixPtr) && (ip[back-1] == match[back-1])) back--;
        mlt -= back;

        if (mlt > longest)
        {
            *matchpos = match+back;
            *startpos = ip+back;
            longest = (int)mlt;
        }
    }


#if MINMATCH == 3
    size_t offset = ip - base - ctx->hashTable3[LZ5HC_hashPtr3(ip)];
    if (offset > 0 && offset < LZ5_SHORT_OFFSET_DISTANCE)
    {
        match = ip - offset;
        if (match > base && LZ5_read24(ip) == LZ5_read24(match))
        {
            int mlt = LZ5_count(ip+MINMATCH, match+MINMATCH, iHighLimit) + MINMATCH;

            int back = 0;
            while ((ip+back>iLowLimit) && (match+back > lowPrefixPtr) && (ip[back-1] == match[back-1])) back--;
            mlt -= back;

            if (mlt > longest)
            if (!longest || LZ5_NORMAL_MATCH_COST(mlt - MINMATCH, (ip - match == ctx->last_off) ? 0 : (ip - match)) < LZ5_NORMAL_MATCH_COST(longest - MINMATCH, (ip+back - *matchpos == ctx->last_off) ? 0 : (ip+back - *matchpos)) + LZ5_NORMAL_LIT_COST(mlt - longest))
            {
                *matchpos = match+back;
                *startpos = ip+back;
                longest = (int)mlt;
            }
        }
    }
#endif

    while ((matchIndex>=lowLimit) && (nbAttempts))
    {
        nbAttempts--;
        if (matchIndex >= dictLimit)
        {
            const BYTE* matchPtr = base + matchIndex;
         //   if (*(ip + longest) == *(matchPtr + longest))
                        
                if (matchPtr < ip && LZ5_read32(matchPtr) == LZ5_read32(ip))
                {
                    int mlt = MINMATCH + LZ5_count(ip+MINMATCH, matchPtr+MINMATCH, iHighLimit);
                    int back = 0;

                    while ((ip+back>iLowLimit)
                           && (matchPtr+back > lowPrefixPtr)
                           && (ip[back-1] == matchPtr[back-1]))
                            back--;

                    mlt -= back;

                    if (mlt > longest)
                    if (LZ5_NORMAL_MATCH_COST(mlt - MINMATCH, (ip - matchPtr == ctx->last_off) ? 0 : (ip - matchPtr)) < LZ5_NORMAL_MATCH_COST(longest - MINMATCH, (ip+back - *matchpos == ctx->last_off) ? 0 : (ip+back - *matchpos)) + (LZ5_NORMAL_LIT_COST(mlt - longest) ))
                    {
                        longest = (int)mlt;
                        *matchpos = matchPtr+back;
                        *startpos = ip+back;
                    }
                }
        }
        else
        {
            const BYTE* matchPtr = dictBase + matchIndex;
            if (LZ5_read32(matchPtr) == LZ5_read32(ip))
            {
                size_t mlt;
                int back=0;
                const BYTE* vLimit = ip + (dictLimit - matchIndex);
                if (vLimit > iHighLimit) vLimit = iHighLimit;
                mlt = LZ5_count(ip+MINMATCH, matchPtr+MINMATCH, vLimit) + MINMATCH;
                if ((ip+mlt == vLimit) && (vLimit < iHighLimit))
                    mlt += LZ5_count(ip+mlt, base+dictLimit, iHighLimit);
                while ((ip+back > iLowLimit) && (matchIndex+back > lowLimit) && (ip[back-1] == matchPtr[back-1])) back--;
                mlt -= back;
                if ((int)mlt > longest) { longest = (int)mlt; *matchpos = base + matchIndex + back; *startpos = ip+back; }
            }
        }
        matchIndex -= chainTable[matchIndex & MAXD_MASK];
    }


    return longest;
}


typedef enum { noLimit = 0, limitedOutput = 1 } limitedOutput_directive;

/*
LZ5 uses 3 types of codewords from 2 to 4 bytes long:
- 1_OO_LL_MMM OOOOOOOO - 10-bit offset, 3-bit match length, 2-bit literal length
- 00_LLL_MMM OOOOOOOO OOOOOOOO - 16-bit offset, 3-bit match length, 3-bit literal length
- 010_LL_MMM OOOOOOOO OOOOOOOO OOOOOOOO - 24-bit offset, 3-bit match length, 2-bit literal length 
- 011_LL_MMM - last offset, 3-bit match length, 2-bit literal length
*/

FORCE_INLINE int LZ5HC_encodeSequence (
    LZ5HC_Data_Structure* ctx,
    const BYTE** ip,
    BYTE** op,
    const BYTE** anchor,
    int matchLength,
    const BYTE* const match,
    limitedOutput_directive limitedOutputBuffer,
    BYTE* oend)
{
    int length;
    BYTE* token;

    /* Encode Literal length */
    length = (int)(*ip - *anchor);
    token = (*op)++;
    if ((limitedOutputBuffer) && ((*op + (length>>8) + length + (2 + 1 + LASTLITERALS)) > oend)) return 1;   /* Check output limit */

    if (*ip-match >= LZ5_SHORT_OFFSET_DISTANCE && *ip-match < LZ5_MID_OFFSET_DISTANCE && *ip-match != ctx->last_off)
    {
        if (length>=(int)RUN_MASK) { int len; *token=(RUN_MASK<<ML_BITS); len = length-RUN_MASK; for(; len > 254 ; len-=255) *(*op)++ = 255;  *(*op)++ = (BYTE)len; }
        else *token = (BYTE)(length<<ML_BITS);
    }
    else
    {
        if (length>=(int)RUN_MASK2) { int len; *token=(RUN_MASK2<<ML_BITS); len = length-RUN_MASK2; for(; len > 254 ; len-=255) *(*op)++ = 255;  *(*op)++ = (BYTE)len; }
        else *token = (BYTE)(length<<ML_BITS);
        
    }

    /* Copy Literals */
    LZ5_wildCopy(*op, *anchor, (*op) + length);
    *op += length;

    /* Encode Offset */
    if (*ip-match == ctx->last_off)
    {
        *token+=(3<<ML_RUN_BITS2);
//            printf("2last_off=%d *token=%d\n", last_off, *token);
    }
    else
	if (*ip-match < LZ5_SHORT_OFFSET_DISTANCE)
	{
		*token+=((4+((*ip-match)>>8))<<ML_RUN_BITS2);
		**op=*ip-match; (*op)++;
	}
	else
	if (*ip-match < LZ5_MID_OFFSET_DISTANCE)
	{
		LZ5_writeLE16(*op, (U16)(*ip-match)); *op+=2;
	}
	else
	{
		*token+=(2<<ML_RUN_BITS2);
		LZ5_writeLE24(*op, (U32)(*ip-match)); *op+=3;
	}
    ctx->last_off = *ip-match;

    /* Encode MatchLength */
    length = (int)(matchLength-MINMATCH);
    if ((limitedOutputBuffer) && (*op + (length>>8) + (1 + LASTLITERALS) > oend)) return 1;   /* Check output limit */
    if (length>=(int)ML_MASK) { *token+=ML_MASK; length-=ML_MASK; for(; length > 509 ; length-=510) { *(*op)++ = 255; *(*op)++ = 255; } if (length > 254) { length-=255; *(*op)++ = 255; } *(*op)++ = (BYTE)length; }
    else *token += (BYTE)(length);

    LZ5HC_DEBUG("%u: ENCODE literals=%u off=%u mlen=%u out=%u\n", (U32)(*ip - ctx->inputBuffer), (U32)(*ip - *anchor), (U32)(*ip-match), (U32)matchLength, 2+(U32)(*op - ctx->outputBuffer));

    /* Prepare next loop */
    *ip += matchLength;
    *anchor = *ip;

    return 0;
}


static int LZ5HC_compress_generic (
    void* ctxvoid,
    const char* source,
    char* dest,
    int inputSize,
    int maxOutputSize,
    int compressionLevel,
    limitedOutput_directive limit
    )
{
    LZ5HC_Data_Structure* ctx = (LZ5HC_Data_Structure*) ctxvoid;
    ctx->inputBuffer = (BYTE*) source;
    ctx->outputBuffer = (BYTE*) dest;
    const BYTE* ip = (const BYTE*) source;
    const BYTE* anchor = ip;
    const BYTE* const iend = ip + inputSize;
    const BYTE* const mflimit = iend - MFLIMIT;
    const BYTE* const matchlimit = (iend - LASTLITERALS);

    BYTE* op = (BYTE*) dest;
    BYTE* const oend = op + maxOutputSize;

    unsigned maxNbAttempts;
    int   ml, ml2, ml0;
    const BYTE* ref=NULL;
    const BYTE* start2=NULL;
    const BYTE* ref2=NULL;
    const BYTE* start0;
    const BYTE* ref0;
    const BYTE* lowPrefixPtr = ctx->base + ctx->dictLimit;

    /* init */
    if (compressionLevel > g_maxCompressionLevel) compressionLevel = g_maxCompressionLevel;
    if (compressionLevel < 1) compressionLevel = LZ5HC_compressionLevel_default;
    maxNbAttempts = 1 << (compressionLevel-1);
    ctx->end += inputSize;

    ip++;

    /* Main Loop */
    while (ip < mflimit)
    {
        ml = LZ5HC_InsertAndFindBestMatch (ctx, ip, matchlimit, (&ref), maxNbAttempts);
        if (!ml) { ip++; continue; }

        int back = 0;
        while ((ip+back>anchor) && (ref+back > lowPrefixPtr) && (ip[back-1] == ref[back-1])) back--;
        ml -= back;
        ip += back;
        ref += back;

        /* saved, in case we would skip too much */
        start0 = ip;
        ref0 = ref;
        ml0 = ml;

_Search:
        if (ip+ml >= mflimit) goto _Encode;

        ml2 = LZ5HC_InsertAndGetWiderMatch(ctx, ip + ml - 2, anchor, matchlimit, 0, &ref2, &start2, maxNbAttempts);
        if (ml2 == 0) goto _Encode;


        int price, best_price, off0=0, off1=0;
        uint8_t *pos, *best_pos;

    //	find the lowest price for encoding ml bytes
        best_pos = (uint8_t*)ip;
        best_price = 1<<30;
        off0 = (uint8_t*)ip - ref;
        off1 = start2 - ref2;

        for (pos = (uint8_t*)ip + ml; pos >= start2; pos--)
        {
            int common0 = pos - ip;
            if (common0 >= MINMATCH)
            {
                price = LZ5_CODEWORD_COST(ip - anchor, (off0 == ctx->last_off) ? 0 : off0, common0 - MINMATCH);
                
                int common1 = start2 + ml2 - pos;
                if (common1 >= MINMATCH)
                    price += LZ5_CODEWORD_COST(0, (off1 == off0) ? 0 : (off1), common1 - MINMATCH);
                else
                    price += LZ5_LIT_ONLY_COST(common1);

                if (price < best_price)
                {
                    best_price = price;
                    best_pos = pos;
                }
            }
            else
            {
                price = LZ5_CODEWORD_COST(start2 - anchor, (off1 == ctx->last_off) ? 0 : off1, ml2 - MINMATCH);

                if (price < best_price)
                {
                    best_price = price;
                    best_pos = pos;
                }

                break;
            }
        }

    //    LZ5HC_DEBUG("%u: TRY last_off=%d literals=%u off=%u mlen=%u literals2=%u off2=%u mlen2=%u best=%d\n", (U32)(ip - ctx->inputBuffer), ctx->last_off, (U32)(ip - anchor), off0, (U32)ml,  (U32)(start2 - anchor), off1, ml2, (U32)(best_pos - ip));
        
        ml = best_pos - ip;
        if (ml < MINMATCH)
        {
            ip = start2;
            ref = ref2;
            ml = ml2;
            goto _Search;
        }
        
_Encode:

        if (start0 < ip)
        {
            if (LZ5_MORE_PROFITABLE(ip - ref, ml, start0 - ref0, ml0, ref0 - ref, ctx->last_off))
            {
                ip = start0;
                ref = ref0;
                ml = ml0;
            }
        }

        if (LZ5HC_encodeSequence(ctx, &ip, &op, &anchor, ml, ref, limit, oend)) return 0;
    }

    /* Encode Last Literals */
    {
        int lastRun = (int)(iend - anchor);
        if ((limit) && (((char*)op - dest) + lastRun + 1 + ((lastRun+255-RUN_MASK)/255) > (U32)maxOutputSize)) return 0;  /* Check output limit */
        if (lastRun>=(int)RUN_MASK) { *op++=(RUN_MASK<<ML_BITS); lastRun-=RUN_MASK; for(; lastRun > 254 ; lastRun-=255) *op++ = 255; *op++ = (BYTE) lastRun; }
        else *op++ = (BYTE)(lastRun<<ML_BITS);
        memcpy(op, anchor, iend - anchor);
        op += iend-anchor;
    }

    /* End */
    return (int) (((char*)op)-dest);
}



int LZ5_sizeofStateHC(void) { return sizeof(LZ5HC_Data_Structure); }

int LZ5_compress_HC_extStateHC (void* state, const char* src, char* dst, int srcSize, int maxDstSize, int compressionLevel)
{
    if (((size_t)(state)&(sizeof(void*)-1)) != 0) return 0;   /* Error : state is not aligned for pointers (32 or 64 bits) */
    LZ5HC_init ((LZ5HC_Data_Structure*)state, (const BYTE*)src);
    if (maxDstSize < LZ5_compressBound(srcSize))
        return LZ5HC_compress_generic (state, src, dst, srcSize, maxDstSize, compressionLevel, limitedOutput);
    else
        return LZ5HC_compress_generic (state, src, dst, srcSize, maxDstSize, compressionLevel, noLimit);
}

int LZ5_alloc_mem_HC(LZ5HC_Data_Structure* statePtr)
{
    statePtr->hashTable = ALLOCATOR(1, sizeof(U32)*(HASHTABLESIZE3+HASHTABLESIZE));
    if (!statePtr->hashTable)
        return 0;

    statePtr->hashTable3 = statePtr->hashTable + HASHTABLESIZE;

    statePtr->chainTable = ALLOCATOR(1, sizeof(U32)*MAXD);
    if (!statePtr->chainTable)
    {
        FREEMEM(statePtr->hashTable);
        statePtr->hashTable = NULL;
        return 0;
    }
    
    return 1;
}

void LZ5_free_mem_HC(LZ5HC_Data_Structure* statePtr)
{
    if (statePtr->chainTable) FREEMEM(statePtr->chainTable);
    if (statePtr->hashTable) FREEMEM(statePtr->hashTable);    
}

int LZ5_compress_HC(const char* src, char* dst, int srcSize, int maxDstSize, int compressionLevel)
{
#if LZ5HC_HEAPMODE==1
    LZ5HC_Data_Structure* statePtr = malloc(sizeof(LZ5HC_Data_Structure));
#else
    LZ5HC_Data_Structure state;
    LZ5HC_Data_Structure* const statePtr = &state;
#endif

    int cSize = 0;
    
    if (!LZ5_alloc_mem_HC(statePtr))
        return 0;
        
    cSize = LZ5_compress_HC_extStateHC(statePtr, src, dst, srcSize, maxDstSize, compressionLevel);

    LZ5_free_mem_HC(statePtr);

#if LZ5HC_HEAPMODE==1
    free(statePtr);
#endif
    return cSize;
}



/**************************************
*  Streaming Functions
**************************************/
/* allocation */
LZ5_streamHC_t* LZ5_createStreamHC(void) 
{ 
    LZ5HC_Data_Structure* statePtr = (LZ5HC_Data_Structure*)malloc(sizeof(LZ5_streamHC_t));
    if (!statePtr)
        return NULL;

    if (!LZ5_alloc_mem_HC(statePtr))
    {
        FREEMEM(statePtr);
        return NULL;
    }
    
    return (LZ5_streamHC_t*) statePtr; 
}

int LZ5_freeStreamHC (LZ5_streamHC_t* LZ5_streamHCPtr)
{
    LZ5HC_Data_Structure* statePtr = (LZ5HC_Data_Structure*)LZ5_streamHCPtr;
    LZ5_free_mem_HC(statePtr);
    free(LZ5_streamHCPtr); 
    return 0; 
}


/* initialization */
void LZ5_resetStreamHC (LZ5_streamHC_t* LZ5_streamHCPtr, int compressionLevel)
{
    LZ5_STATIC_ASSERT(sizeof(LZ5HC_Data_Structure) <= sizeof(LZ5_streamHC_t));   /* if compilation fails here, LZ5_STREAMHCSIZE must be increased */
    ((LZ5HC_Data_Structure*)LZ5_streamHCPtr)->base = NULL;
    ((LZ5HC_Data_Structure*)LZ5_streamHCPtr)->compressionLevel = (unsigned)compressionLevel;
}

int LZ5_loadDictHC (LZ5_streamHC_t* LZ5_streamHCPtr, const char* dictionary, int dictSize)
{
    LZ5HC_Data_Structure* ctxPtr = (LZ5HC_Data_Structure*) LZ5_streamHCPtr;
    if (dictSize > LZ5_DICT_SIZE)
    {
        dictionary += dictSize - LZ5_DICT_SIZE;
        dictSize = LZ5_DICT_SIZE;
    }
    LZ5HC_init (ctxPtr, (const BYTE*)dictionary);
    if (dictSize >= 4) LZ5HC_Insert (ctxPtr, (const BYTE*)dictionary +(dictSize-3));
    ctxPtr->end = (const BYTE*)dictionary + dictSize;
    return dictSize;
}


/* compression */

static void LZ5HC_setExternalDict(LZ5HC_Data_Structure* ctxPtr, const BYTE* newBlock)
{
    if (ctxPtr->end >= ctxPtr->base + 4)
        LZ5HC_Insert (ctxPtr, ctxPtr->end-3);   /* Referencing remaining dictionary content */
    /* Only one memory segment for extDict, so any previous extDict is lost at this stage */
    ctxPtr->lowLimit  = ctxPtr->dictLimit;
    ctxPtr->dictLimit = (U32)(ctxPtr->end - ctxPtr->base);
    ctxPtr->dictBase  = ctxPtr->base;
    ctxPtr->base = newBlock - ctxPtr->dictLimit;
    ctxPtr->end  = newBlock;
    ctxPtr->nextToUpdate = ctxPtr->dictLimit;   /* match referencing will resume from there */
}

static int LZ5_compressHC_continue_generic (LZ5HC_Data_Structure* ctxPtr,
                                            const char* source, char* dest,
                                            int inputSize, int maxOutputSize, limitedOutput_directive limit)
{
    /* auto-init if forgotten */
    if (ctxPtr->base == NULL)
        LZ5HC_init (ctxPtr, (const BYTE*) source);

    /* Check overflow */
    if ((size_t)(ctxPtr->end - ctxPtr->base) > 2 GB)
    {
        size_t dictSize = (size_t)(ctxPtr->end - ctxPtr->base) - ctxPtr->dictLimit;
        if (dictSize > LZ5_DICT_SIZE) dictSize = LZ5_DICT_SIZE;

        LZ5_loadDictHC((LZ5_streamHC_t*)ctxPtr, (const char*)(ctxPtr->end) - dictSize, (int)dictSize);
    }

    /* Check if blocks follow each other */
    if ((const BYTE*)source != ctxPtr->end)
        LZ5HC_setExternalDict(ctxPtr, (const BYTE*)source);

    /* Check overlapping input/dictionary space */
    {
        const BYTE* sourceEnd = (const BYTE*) source + inputSize;
        const BYTE* dictBegin = ctxPtr->dictBase + ctxPtr->lowLimit;
        const BYTE* dictEnd   = ctxPtr->dictBase + ctxPtr->dictLimit;
        if ((sourceEnd > dictBegin) && ((const BYTE*)source < dictEnd))
        {
            if (sourceEnd > dictEnd) sourceEnd = dictEnd;
            ctxPtr->lowLimit = (U32)(sourceEnd - ctxPtr->dictBase);
            if (ctxPtr->dictLimit - ctxPtr->lowLimit < 4) ctxPtr->lowLimit = ctxPtr->dictLimit;
        }
    }

    return LZ5HC_compress_generic (ctxPtr, source, dest, inputSize, maxOutputSize, ctxPtr->compressionLevel, limit);
}

int LZ5_compress_HC_continue (LZ5_streamHC_t* LZ5_streamHCPtr, const char* source, char* dest, int inputSize, int maxOutputSize)
{
    if (maxOutputSize < LZ5_compressBound(inputSize))
        return LZ5_compressHC_continue_generic ((LZ5HC_Data_Structure*)LZ5_streamHCPtr, source, dest, inputSize, maxOutputSize, limitedOutput);
    else
        return LZ5_compressHC_continue_generic ((LZ5HC_Data_Structure*)LZ5_streamHCPtr, source, dest, inputSize, maxOutputSize, noLimit);
}


/* dictionary saving */

int LZ5_saveDictHC (LZ5_streamHC_t* LZ5_streamHCPtr, char* safeBuffer, int dictSize)
{
    LZ5HC_Data_Structure* streamPtr = (LZ5HC_Data_Structure*)LZ5_streamHCPtr;
    int prefixSize = (int)(streamPtr->end - (streamPtr->base + streamPtr->dictLimit));
    if (dictSize > LZ5_DICT_SIZE) dictSize = LZ5_DICT_SIZE;
    if (dictSize < 4) dictSize = 0;
    if (dictSize > prefixSize) dictSize = prefixSize;
    memmove(safeBuffer, streamPtr->end - dictSize, dictSize);
    {
        U32 endIndex = (U32)(streamPtr->end - streamPtr->base);
        streamPtr->end = (const BYTE*)safeBuffer + dictSize;
        streamPtr->base = streamPtr->end - endIndex;
        streamPtr->dictLimit = endIndex - dictSize;
        streamPtr->lowLimit = endIndex - dictSize;
        if (streamPtr->nextToUpdate < streamPtr->dictLimit) streamPtr->nextToUpdate = streamPtr->dictLimit;
    }
    return dictSize;
}

/***********************************
*  Deprecated Functions
***********************************/
/* Deprecated compression functions */
/* These functions are planned to start generate warnings by r131 approximately */
int LZ5_compressHC(const char* src, char* dst, int srcSize) { return LZ5_compress_HC (src, dst, srcSize, LZ5_compressBound(srcSize), 0); }
int LZ5_compressHC_limitedOutput(const char* src, char* dst, int srcSize, int maxDstSize) { return LZ5_compress_HC(src, dst, srcSize, maxDstSize, 0); }
int LZ5_compressHC_continue (LZ5_streamHC_t* ctx, const char* src, char* dst, int srcSize) { return LZ5_compress_HC_continue (ctx, src, dst, srcSize, LZ5_compressBound(srcSize)); }
int LZ5_compressHC_limitedOutput_continue (LZ5_streamHC_t* ctx, const char* src, char* dst, int srcSize, int maxDstSize) { return LZ5_compress_HC_continue (ctx, src, dst, srcSize, maxDstSize); } 
int LZ5_compressHC_withStateHC (void* state, const char* src, char* dst, int srcSize) { return LZ5_compress_HC_extStateHC (state, src, dst, srcSize, LZ5_compressBound(srcSize), 0); }
int LZ5_compressHC_limitedOutput_withStateHC (void* state, const char* src, char* dst, int srcSize, int maxDstSize) { return LZ5_compress_HC_extStateHC (state, src, dst, srcSize, maxDstSize, 0); } 
