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
#include "lz5.h"
#include "lz5_decompress.h"
#define LZ5_MEM_FUNCTIONS
#include "lz5_common.h"


/*-************************************
*  Local Structures and types
**************************************/
typedef enum { noDict = 0, withPrefix64k, usingExtDict } dict_directive;

typedef enum { endOnOutputSize = 0, endOnInputSize = 1 } endCondition_directive;
typedef enum { full = 0, partial = 1 } earlyEnd_directive;


/*-*****************************
*  Decompression functions
*******************************/
/*! LZ5_decompress_generic() :
 *  This generic decompression function cover all use cases.
 *  It shall be instantiated several times, using different sets of directives
 *  Note that it is important this generic function is really inlined,
 *  in order to remove useless branches during compilation optimization.
 */
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
    const BYTE* ip = (const BYTE*) source;
    const BYTE* const iend = ip + inputSize;

    BYTE* op = (BYTE*) dest;
    BYTE* const oend = op + outputSize;
    BYTE* cpy;
    BYTE* oexit = op + targetOutputSize;
    const BYTE* const lowLimit = lowPrefix - dictSize;

    const BYTE* const dictEnd = (const BYTE*)dictStart + dictSize;
    const unsigned dec32table[] = {4, 1, 2, 1, 4, 4, 4, 4};
    const int dec64table[] = {0, 0, 0, -1, 0, 1, 2, 3};

    const int safeDecode = (endOnInput==endOnInputSize);
    const int checkOffset = ((safeDecode) && (dictSize < (int)(LZ5_DICT_SIZE)));
    int compressionLevel;
    LZ5_parameters params;

    /* Special cases */
    if ((partialDecoding) && (oexit > oend-MFLIMIT)) oexit = oend-MFLIMIT;                        /* targetOutputSize too high => decode everything */
    if ((endOnInput) && (unlikely(outputSize==0))) return ((inputSize==1) && (*ip==0)) ? 0 : -1;  /* Empty output buffer */
    if ((!endOnInput) && (unlikely(outputSize==0))) return (*ip==0?1:-1);

    compressionLevel = *ip++;
    if (compressionLevel == 0 || compressionLevel > LZ5_MAX_CLEVEL) goto _output_error;
    params = LZ5_defaultParameters[compressionLevel];
//    printf("compressionLevel=%d\n", compressionLevel);

    /* Main Loop : decode sequences */
    while (1) {
        unsigned token;
        size_t length;
        const BYTE* match;
        size_t offset;

        /* get literal length */
        token = *ip++;
        if ((length=(token>>ML_BITS)) == RUN_MASK) {
            unsigned s;
            do {
                s = *ip++;
                length += s;
            } while ( likely(endOnInput ? ip<iend-RUN_MASK : 1) & (s==255) );
            if ((safeDecode) && unlikely((size_t)(op+length)<(size_t)(op))) goto _output_error;   /* overflow detection */
            if ((safeDecode) && unlikely((size_t)(ip+length)<(size_t)(ip))) goto _output_error;   /* overflow detection */
        }

        /* copy literals */
        cpy = op+length;
        if ( ((endOnInput) && ((cpy>(partialDecoding?oexit:oend-WILDCOPYLENGTH)) || (ip+length>iend-(2+1+LASTLITERALS))) )
            || ((!endOnInput) && (cpy>oend-WILDCOPYLENGTH)) )
        {
            if (partialDecoding) {
                if (cpy > oend) goto _output_error;                           /* Error : write attempt beyond end of output buffer */
                if ((endOnInput) && (ip+length > iend)) goto _output_error;   /* Error : read attempt beyond end of input buffer */
            } else {
                if ((!endOnInput) && (cpy != oend)) goto _output_error;       /* Error : block decoding must stop exactly there */
                if ((endOnInput) && ((ip+length != iend) || (cpy > oend))) goto _output_error;   /* Error : input must be consumed */
            }
            memcpy(op, ip, length);
            ip += length;
            op += length;
            break;     /* Necessarily EOF, due to parsing restrictions */
        }
        LZ5_wildCopy(op, ip, cpy);
        ip += length; op = cpy;

        /* get offset */
        if (params.windowLog <= 16) {
            offset = MEM_readLE16(ip); 
            ip+=2;
        } else {
            offset = MEM_readLE24(ip);
            ip+=3;
            if ((BYTE*)offset > op) goto _output_error;   /* Error : offset outside buffers */
        }

        match = op - offset;
        if ((checkOffset) && (unlikely(match < lowLimit))) goto _output_error;   /* Error : offset outside buffers */

        /* get matchlength */
        length = token & ML_MASK;
        if (length == ML_MASK) {
            unsigned s;
            do {
                s = *ip++;
                if ((endOnInput) && (ip > iend-LASTLITERALS)) goto _output_error;
                length += s;
            } while (s==255);
            if ((safeDecode) && unlikely((size_t)(op+length)<(size_t)op)) goto _output_error;   /* overflow detection */
        }
        length += MINMATCH;

        /* check external dictionary */
        if ((dict==usingExtDict) && (match < lowPrefix)) {
            if (unlikely(op+length > oend-LASTLITERALS)) goto _output_error;   /* doesn't respect parsing restriction */

            if (length <= (size_t)(lowPrefix-match)) {
                /* match can be copied as a single segment from external dictionary */
                memmove(op, dictEnd - (lowPrefix-match), length);
                op += length;
            } else {
                /* match encompass external dictionary and current block */
                size_t const copySize = (size_t)(lowPrefix-match);
                size_t const restSize = length - copySize;
                memcpy(op, dictEnd - copySize, copySize);
                op += copySize;
                if (restSize > (size_t)(op-lowPrefix)) {  /* overlap copy */
                    BYTE* const endOfMatch = op + restSize;
                    const BYTE* copyFrom = lowPrefix;
                    while (op < endOfMatch) *op++ = *copyFrom++;
                } else {
                    memcpy(op, lowPrefix, restSize);
                    op += restSize;
            }   }
            continue;
        }

        /* copy match within block */
        cpy = op + length;
        if (unlikely(offset<8)) {
            const int dec64 = dec64table[offset];
            op[0] = match[0];
            op[1] = match[1];
            op[2] = match[2];
            op[3] = match[3];
            match += dec32table[offset];
            memcpy(op+4, match, 4);
            match -= dec64;
        } else { LZ5_copy8(op, match); match+=8; }
        op += 8;

        if (unlikely(cpy>oend-12)) {
            BYTE* const oCopyLimit = oend-(WILDCOPYLENGTH-1);
            if (cpy > oend-LASTLITERALS) goto _output_error;    /* Error : last LASTLITERALS bytes must be literals (uncompressed) */
            if (op < oCopyLimit) {
                LZ5_wildCopy(op, match, oCopyLimit);
                match += oCopyLimit - op;
                op = oCopyLimit;
            }
            while (op<cpy) *op++ = *match++;
        } else {
            LZ5_copy8(op, match);
            if (length>16) LZ5_wildCopy(op+8, match+8, cpy);
        }
        op=cpy;   /* correction */
    }

    /* end of decoding */
    if (endOnInput)
       return (int) (((char*)op)-dest);     /* Nb of output bytes decoded */
    else
       return (int) (((const char*)ip)-source);   /* Nb of input bytes read */

    /* Overflow error detected */
_output_error:
 //   printf("cpy=%p oend=%p ip+length[%d]=%p iend=%p\n", cpy, oend, length, ip+length, iend);
 //   printf("_output_error=%d\n", (int) (-(((const char*)ip)-source))-1);
    return (int) (-(((const char*)ip)-source))-1;
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

