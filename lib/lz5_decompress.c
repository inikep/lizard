/*
   LZ5 - Fast LZ compression algorithm
   Copyright (C) 2011-2016, Yann Collet.
   Copyright (C) 2016, Przemyslaw Skibinski <inikep@gmail.com>

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


/**************************************
*  Includes
**************************************/
#include "lz5_compress.h"
#include "lz5_decompress.h"
#define LZ5_MEM_FUNCTIONS
#include "lz5_common.h"
#include <stdio.h> // printf
#include <stdint.h> // uintptr_t

/*-************************************
*  Local Structures and types
**************************************/
typedef enum { noDict = 0, withPrefix64k, usingExtDict } dict_directive;
typedef enum { endOnOutputSize = 0, endOnInputSize = 1 } endCondition_directive;
typedef enum { full = 0, partial = 1 } earlyEnd_directive;

#include "lz5_decompress_lz4.h"
#include "lz5_decompress_lz5v2.h"


/*-*****************************
*  Decompression functions
*******************************/
FORCE_INLINE int LZ5_decompress_generic(
                 const char* const source,
                 char* const dest,
                 int inputSize,
                 int outputSize,         /* If endOnInput==endOnInputSize, this value is the max size of Output Buffer. */
                 int endOnInput,         /* endOnOutputSize, endOnInputSize */
                 int partialDecoding,    /* full, partial */
                 int targetOutputSize,   /* only used if partialDecoding==partial */
                 int dict,               /* noDict, withPrefix64k, usingExtDict */
                 const BYTE* const lowPrefix,  /* == dest if dict == noDict */
                 const BYTE* const dictStart,  /* only if dict==usingExtDict */
                 const size_t dictSize         /* note : = 0 if noDict */
                 )
{
    /* Local Variables */
    LZ5_parameters params;
    int compressionLevel;

 //   if (inputSize < 1) return -1;
    compressionLevel = *source;
//    printf("compressionLevel=%d\n", compressionLevel);
     if (compressionLevel == 0 || compressionLevel > LZ5_MAX_CLEVEL) return -1;

    params = LZ5_defaultParameters[compressionLevel];
    if (params.decompressType == LZ5_coderwords_LZ4)
        return LZ5_decompress_LZ4(source, dest, inputSize, outputSize, endOnInput, partialDecoding, targetOutputSize, dict, lowPrefix, dictStart, dictSize, compressionLevel);

    return LZ5_decompress_LZ5v2(source, dest, inputSize, outputSize, endOnInput, partialDecoding, targetOutputSize, dict, lowPrefix, dictStart, dictSize, compressionLevel);
}


int LZ5_decompress_safe(const char* source, char* dest, int compressedSize, int maxDecompressedSize)
{
    return LZ5_decompress_generic(source, dest, compressedSize, maxDecompressedSize, endOnInputSize, full, 0, noDict, (BYTE*)dest, NULL, 0);
}

int LZ5_decompress_safe_partial(const char* source, char* dest, int compressedSize, int targetOutputSize, int maxDecompressedSize)
{
    return LZ5_decompress_generic(source, dest, compressedSize, maxDecompressedSize, endOnInputSize, partial, targetOutputSize, noDict, (BYTE*)dest, NULL, 0);
}

int LZ5_decompress_fast(const char* source, char* dest, int originalSize)
{
    return LZ5_decompress_generic(source, dest, 0, originalSize, endOnOutputSize, full, 0, withPrefix64k, (BYTE*)(dest - LZ5_DICT_SIZE), NULL, LZ5_DICT_SIZE);
}


/*===== streaming decompression functions =====*/


/*
 * If you prefer dynamic allocation methods,
 * LZ5_createStreamDecode()
 * provides a pointer (void*) towards an initialized LZ5_streamDecode_t structure.
 */
LZ5_streamDecode_t* LZ5_createStreamDecode(void)
{
    LZ5_streamDecode_t* lz5s = (LZ5_streamDecode_t*) ALLOCATOR(1, sizeof(LZ5_streamDecode_t));
    (void)LZ5_count; /* unused function 'LZ5_count' */
    return lz5s;
}

int LZ5_freeStreamDecode (LZ5_streamDecode_t* LZ5_stream)
{
    FREEMEM(LZ5_stream);
    return 0;
}

/*!
 * LZ5_setStreamDecode() :
 * Use this function to instruct where to find the dictionary.
 * This function is not necessary if previous data is still available where it was decoded.
 * Loading a size of 0 is allowed (same effect as no dictionary).
 * Return : 1 if OK, 0 if error
 */
int LZ5_setStreamDecode (LZ5_streamDecode_t* LZ5_streamDecode, const char* dictionary, int dictSize)
{
    LZ5_streamDecode_t* lz5sd = (LZ5_streamDecode_t*) LZ5_streamDecode;
    lz5sd->prefixSize = (size_t) dictSize;
    lz5sd->prefixEnd = (const BYTE*) dictionary + dictSize;
    lz5sd->externalDict = NULL;
    lz5sd->extDictSize  = 0;
    return 1;
}

/*
*_continue() :
    These decoding functions allow decompression of multiple blocks in "streaming" mode.
    Previously decoded blocks must still be available at the memory position where they were decoded.
    If it's not possible, save the relevant part of decoded data into a safe buffer,
    and indicate where it stands using LZ5_setStreamDecode()
*/
int LZ5_decompress_safe_continue (LZ5_streamDecode_t* LZ5_streamDecode, const char* source, char* dest, int compressedSize, int maxOutputSize)
{
    LZ5_streamDecode_t* lz5sd = (LZ5_streamDecode_t*) LZ5_streamDecode;
    int result;

    if (lz5sd->prefixEnd == (BYTE*)dest) {
        result = LZ5_decompress_generic(source, dest, compressedSize, maxOutputSize,
                                        endOnInputSize, full, 0,
                                        usingExtDict, lz5sd->prefixEnd - lz5sd->prefixSize, lz5sd->externalDict, lz5sd->extDictSize);
        if (result <= 0) return result;
        lz5sd->prefixSize += result;
        lz5sd->prefixEnd  += result;
    } else {
        lz5sd->extDictSize = lz5sd->prefixSize;
        lz5sd->externalDict = lz5sd->prefixEnd - lz5sd->extDictSize;
        result = LZ5_decompress_generic(source, dest, compressedSize, maxOutputSize,
                                        endOnInputSize, full, 0,
                                        usingExtDict, (BYTE*)dest, lz5sd->externalDict, lz5sd->extDictSize);
        if (result <= 0) return result;
        lz5sd->prefixSize = result;
        lz5sd->prefixEnd  = (BYTE*)dest + result;
    }

    return result;
}

int LZ5_decompress_fast_continue (LZ5_streamDecode_t* LZ5_streamDecode, const char* source, char* dest, int originalSize)
{
    LZ5_streamDecode_t* lz5sd = (LZ5_streamDecode_t*) LZ5_streamDecode;
    int result;

    if (lz5sd->prefixEnd == (BYTE*)dest) {
        result = LZ5_decompress_generic(source, dest, 0, originalSize,
                                        endOnOutputSize, full, 0,
                                        usingExtDict, lz5sd->prefixEnd - lz5sd->prefixSize, lz5sd->externalDict, lz5sd->extDictSize);
        if (result <= 0) return result;
        lz5sd->prefixSize += originalSize;
        lz5sd->prefixEnd  += originalSize;
    } else {
        lz5sd->extDictSize = lz5sd->prefixSize;
        lz5sd->externalDict = (BYTE*)dest - lz5sd->extDictSize;
        result = LZ5_decompress_generic(source, dest, 0, originalSize,
                                        endOnOutputSize, full, 0,
                                        usingExtDict, (BYTE*)dest, lz5sd->externalDict, lz5sd->extDictSize);
        if (result <= 0) return result;
        lz5sd->prefixSize = originalSize;
        lz5sd->prefixEnd  = (BYTE*)dest + originalSize;
    }

    return result;
}


/*
Advanced decoding functions :
*_usingDict() :
    These decoding functions work the same as "_continue" ones,
    the dictionary must be explicitly provided within parameters
*/

FORCE_INLINE int LZ5_decompress_usingDict_generic(const char* source, char* dest, int compressedSize, int maxOutputSize, int safe, const char* dictStart, int dictSize)
{
    if (dictSize==0)
        return LZ5_decompress_generic(source, dest, compressedSize, maxOutputSize, safe, full, 0, noDict, (BYTE*)dest, NULL, 0);
    if (dictStart+dictSize == dest)
    {
        if (dictSize >= (int)(LZ5_DICT_SIZE - 1))
            return LZ5_decompress_generic(source, dest, compressedSize, maxOutputSize, safe, full, 0, withPrefix64k, (BYTE*)dest-LZ5_DICT_SIZE, NULL, 0);
        return LZ5_decompress_generic(source, dest, compressedSize, maxOutputSize, safe, full, 0, noDict, (BYTE*)dest-dictSize, NULL, 0);
    }
    return LZ5_decompress_generic(source, dest, compressedSize, maxOutputSize, safe, full, 0, usingExtDict, (BYTE*)dest, (const BYTE*)dictStart, dictSize);
}

int LZ5_decompress_safe_usingDict(const char* source, char* dest, int compressedSize, int maxOutputSize, const char* dictStart, int dictSize)
{
    return LZ5_decompress_usingDict_generic(source, dest, compressedSize, maxOutputSize, 1, dictStart, dictSize);
}

int LZ5_decompress_fast_usingDict(const char* source, char* dest, int originalSize, const char* dictStart, int dictSize)
{
    return LZ5_decompress_usingDict_generic(source, dest, 0, originalSize, 0, dictStart, dictSize);
}

/* debug function */
int LZ5_decompress_safe_forceExtDict(const char* source, char* dest, int compressedSize, int maxOutputSize, const char* dictStart, int dictSize)
{
    return LZ5_decompress_generic(source, dest, compressedSize, maxOutputSize, endOnInputSize, full, 0, usingExtDict, (BYTE*)dest, (const BYTE*)dictStart, dictSize);
}

