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
    SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOTLZ5_hash4Ptr
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
#include "lz5_compress.h"
#define LZ5_MEM_FUNCTIONS
#include "lz5_common.h"
#include <stdio.h>
  

/* *************************************
*  Local Macros
***************************************/
#define LZ5_TRANSFORM_LEVEL 1
#define DELTANEXT(p)        chainTable[(p) & contentMask]


/*-************************************
*  Local Utils
**************************************/
int LZ5_versionNumber (void) { return LZ5_VERSION_NUMBER; }
int LZ5_compressBound(int isize)  { return LZ5_COMPRESSBOUND(isize); }
int LZ5_sizeofState_Level1() { return LZ5_sizeofState(LZ5_TRANSFORM_LEVEL); }



/* *************************************
*  Hash functions
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




/**************************************
*  Internal functions
**************************************/
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


/**************************************
*  Include parsers
**************************************/
#include "lz5_compress_lz4.h"
//#include "lz5_compress_lz5v2.h"
#include "lz5_parser_hc.h"
#include "lz5_parser_nochain.h"


int LZ5_verifyCompressionLevel(int compressionLevel)
{
    (void)LZ5_hashPtr;
    (void)LZ5_wildCopy16;
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


static void LZ5_init(LZ5_stream_t* ctx, const BYTE* start)
{
    MEM_INIT((void*)ctx->hashTable, 0, ctx->hashTableSize);
    MEM_INIT(ctx->chainTable, 0x01, ctx->chainTableSize);
    ctx->nextToUpdate = LZ5_DICT_SIZE;
    ctx->base = start - LZ5_DICT_SIZE;
    ctx->end = start;
    ctx->dictBase = start - LZ5_DICT_SIZE;
    ctx->dictLimit = LZ5_DICT_SIZE;
    ctx->lowLimit = LZ5_DICT_SIZE;
    ctx->last_off = 0;
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


LZ5_stream_t* LZ5_createStream(int compressionLevel) 
{ 
    LZ5_stream_t* ctx = LZ5_initStream(NULL, compressionLevel);
//    if (ctx) printf("LZ5_createStream ctx=%p ctx->compressionLevel=%d\n", ctx, ctx->compressionLevel);
    return ctx; 
}


/* initialization */
LZ5_stream_t* LZ5_resetStream(LZ5_stream_t* ctx, int compressionLevel)
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


int LZ5_freeStream(LZ5_stream_t* ctx) 
{ 
//    printf("LZ5_freeStream ctx=%p ctx->compressionLevel=%d\n", ctx, ctx->compressionLevel);
    free(ctx); 
    return 0; 
}


int LZ5_loadDict(LZ5_stream_t* LZ5_streamPtr, const char* dictionary, int dictSize)
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

FORCE_INLINE int LZ5_compress_generic (
    void* ctxvoid,
    const char* source,
    char* dest,
    int inputSize,
    int maxOutputSize,
    limitedOutput_directive limit)
{
    LZ5_stream_t* ctx = (LZ5_stream_t*) ctxvoid;
    int res;
    
 //   printf("LZ5_compress_generic source=%p inputSize=%d dest=%p maxOutputSize=%d cLevel=%d limit=%d dictBase=%p dictSize=%d\n", source, inputSize, dest, maxOutputSize, ctx->compressionLevel, limit, ctx->dictBase, ctx->dictLimit); 
    *dest++ = (BYTE)ctx->compressionLevel;
    maxOutputSize--; // can be lower than 0
    
    if (ctx->params.compressType == LZ5_parser_nochain)
        res = LZ5_compress_nochain(ctxvoid, source, dest, inputSize, maxOutputSize, limit);
    else
        res = LZ5_compress_HC(ctxvoid, source, dest, inputSize, maxOutputSize, limit);

 //   printf("LZ5_compress_generic res=%d\n", res);
    return (res > 0) ? res+1 : res;
}


FORCE_INLINE int LZ5_compress_continue_generic (LZ5_stream_t* ctxPtr,
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
*  Level1 functions
**************************************/
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
