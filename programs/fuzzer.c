/*
    fuzzer.c - Fuzzer test tool for LZ5
    Copyright (C) Yann Collet 2012-2016

    GPL v2 License

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with this program; if not, write to the Free Software Foundation, Inc.,
    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

    You can contact the author at :
    - LZ5 source repo : https://github.com/inikep/lz5
*/

/*-************************************
*  Compiler options
**************************************/
#ifdef _MSC_VER    /* Visual Studio */
#  define _CRT_SECURE_NO_WARNINGS    /* fgets */
#  pragma warning(disable : 4127)    /* disable: C4127: conditional expression is constant */
#  pragma warning(disable : 4146)    /* disable: C4146: minus unsigned expression */
#  pragma warning(disable : 4310)    /* disable: C4310: constant char value > 127 */
#endif

/* S_ISREG & gettimeofday() are not supported by MSVC */
#if defined(_MSC_VER) || defined(_WIN32)
#  define FUZ_LEGACY_TIMER 1
#endif


/*-************************************
*  Includes
**************************************/
#include <stdlib.h>
#include <stdio.h>      /* fgets, sscanf */
#include <string.h>     /* strcmp */
#include <time.h>       /* clock_t, clock, CLOCKS_PER_SEC */
#include "lz5_compress.h"        /* LZ5_VERSION_STRING */
#include "lz5_decompress.h"
#include "lz5_common.h"
#define XXH_STATIC_LINKING_ONLY
#include "xxhash/xxhash.h"



/*-************************************
*  Constants
**************************************/
#define NB_ATTEMPTS (1<<16)
#define COMPRESSIBLE_NOISE_LENGTH (1 << 21)
#define FUZ_MAX_BLOCK_SIZE (1 << 17)
#define FUZ_MAX_DICT_SIZE  (1 << 15)
#define FUZ_COMPRESSIBILITY_DEFAULT 60
#define PRIME1   2654435761U
#define PRIME2   2246822519U
#define PRIME3   3266489917U


/*-***************************************
*  Macros
*****************************************/
#define DISPLAY(...)         fprintf(stderr, __VA_ARGS__)
#define DISPLAYLEVEL(l, ...) if (g_displayLevel>=l) { DISPLAY(__VA_ARGS__); }
static int g_displayLevel = 2;
static const clock_t g_refreshRate = CLOCKS_PER_SEC * 25 / 100;
static clock_t g_time = 0;


/*-*******************************************************
*  Fuzzer functions
*********************************************************/
static clock_t FUZ_GetClockSpan(clock_t clockStart)
{
    return clock() - clockStart;   /* works even if overflow; max span ~ 30mn */
}

static U32 FUZ_rotl32(U32 u32, U32 nbBits)
{
    return ((u32 << nbBits) | (u32 >> (32 - nbBits)));
}

static U32 FUZ_rand(U32* src)
{
    U32 rand32 = *src;
    rand32 *= PRIME1;
    rand32 ^= PRIME2;
    rand32  = FUZ_rotl32(rand32, 13);
    *src = rand32;
    return rand32;
}


#define FUZ_RAND15BITS  ((FUZ_rand(seed) >> 3) & 32767)
#define FUZ_RANDLENGTH  ( ((FUZ_rand(seed) >> 7) & 3) ? (FUZ_rand(seed) % 15) : (FUZ_rand(seed) % 510) + 15)
static void FUZ_fillCompressibleNoiseBuffer(void* buffer, size_t bufferSize, double proba, U32* seed)
{
    BYTE* BBuffer = (BYTE*)buffer;
    size_t pos = 0;
    U32 P32 = (U32)(32768 * proba);

    /* First Bytes */
    while (pos < 20)
        BBuffer[pos++] = (BYTE)(FUZ_rand(seed));

    while (pos < bufferSize)
    {
        /* Select : Literal (noise) or copy (within 64K) */
        if (FUZ_RAND15BITS < P32)
        {
            /* Copy (within 64K) */
            size_t match, d;
            size_t length = FUZ_RANDLENGTH + 4;
            size_t offset = FUZ_RAND15BITS + 1;
            while (offset > pos) offset >>= 1;
            d = pos + length;
            while (d > bufferSize) d = bufferSize;
            match = pos - offset;
            while (pos < d) BBuffer[pos++] = BBuffer[match++];
        }
        else
        {
            /* Literal (noise) */
            size_t d;
            size_t length = FUZ_RANDLENGTH;
            d = pos + length;
            if (d > bufferSize) d = bufferSize;
            while (pos < d) BBuffer[pos++] = (BYTE)(FUZ_rand(seed) >> 5);
        }
    }
}


#define MAX_NB_BUFF_I134 150
#define BLOCKSIZE_I134   (32 MB)
/*! FUZ_AddressOverflow() :
*   Aggressively pushes memory allocation limits,
*   and generates patterns which create address space overflow.
*   only possible in 32-bits mode */
static int FUZ_AddressOverflow(U32* seed)
{
    char* buffers[MAX_NB_BUFF_I134];
    int i, nbBuff=0;
    int highAddress = 0;

    DISPLAY("Overflow tests : ");

    /* Only possible in 32-bits */
 /*   if (sizeof(void*)==8)
    {
        DISPLAY("64 bits mode : no overflow \n");
        fflush(stdout);
        return 0;
    }*/

    buffers[0] = (char*)malloc(BLOCKSIZE_I134);
    buffers[1] = (char*)malloc(BLOCKSIZE_I134);
    if ((!buffers[0]) || (!buffers[1])) {
        DISPLAY("not enough memory for tests \n");
        return 0;
    }

    for (nbBuff=2; nbBuff < MAX_NB_BUFF_I134; nbBuff++) {
        DISPLAY("%3i \b\b\b\b", nbBuff);
        buffers[nbBuff] = (char*)malloc(BLOCKSIZE_I134);
        if (buffers[nbBuff]==NULL) goto _endOfTests;

        if (((size_t)buffers[nbBuff] > (size_t)0x80000000) && (!highAddress)) {
            DISPLAY("high address detected : ");
            fflush(stdout);
            highAddress=1;
        }

        {   int const nbOf255 = 1 + (FUZ_rand(seed) % (BLOCKSIZE_I134-1));
            char* const input = buffers[nbBuff-1];
            char* output = buffers[nbBuff];
            int r;
            BYTE cLevel = LZ5_MIN_CLEVEL + (FUZ_rand(seed) % (1+LZ5_MAX_CLEVEL-LZ5_MIN_CLEVEL));
            for(i = 5; i < nbOf255; i++) input[i] = (char)0xff;
            for(i = 5; i < nbOf255; i+=(FUZ_rand(seed) % 128)) input[i] = (BYTE)(FUZ_rand(seed)%256);

            input[0] = (char)cLevel; /* Compression Level */
            input[1] = (char)0xF0;   /* Literal length overflow */
            input[2] = (char)0xFF;
            input[3] = (char)0xFF;
            input[4] = (char)0xFF;
            r = LZ5_decompress_safe(input, output, nbOf255, BLOCKSIZE_I134);
            if (r>0 && r<nbOf255) goto _overflowError;

            input[0] = (char)cLevel; /* Compression Level */
            input[1] = (char)0x1F;   /* Match length overflow */
            input[2] = (char)0x01;
            input[3] = (char)0x01;
            input[4] = (char)0x00;
            r = LZ5_decompress_safe(input, output, nbOf255, BLOCKSIZE_I134);
            if (r>0 && r<nbOf255) goto _overflowError;

            output = buffers[nbBuff-2];   /* Reverse in/out pointer order */
            input[0] = (char)cLevel; /* Compression Level */
            input[1] = (char)0xF0;   /* Literal length overflow */
            input[2] = (char)0xFF;
            input[3] = (char)0xFF;
            input[4] = (char)0xFF;
            r = LZ5_decompress_safe(input, output, nbOf255, BLOCKSIZE_I134);
            if (r>0 && r<nbOf255) goto _overflowError;

            input[0] = (char)cLevel; /* Compression Level */
            input[1] = (char)0x1F;   /* Match length overflow */
            input[2] = (char)0x01;
            input[3] = (char)0x01;
            input[4] = (char)0x00;
            r = LZ5_decompress_safe(input, output, nbOf255, BLOCKSIZE_I134);
            if (r>0 && r<nbOf255) goto _overflowError;
        }
    }

_endOfTests:
    for (i=0 ; i<nbBuff; i++) free(buffers[i]);
    if (!highAddress) DISPLAY("high address not possible \n");
    else DISPLAY("all overflows correctly detected \n");
    return 0;

_overflowError:
    DISPLAY("Address space overflow error !! \n");
    exit(1);
}


static void FUZ_displayUpdate(unsigned testNb)
{
    if ((FUZ_GetClockSpan(g_time) > g_refreshRate) | (g_displayLevel>=3)) {
        g_time = clock();
        DISPLAY("\r%5u   ", testNb);
        if (g_displayLevel>=3) fflush(stdout);
    }
}


/*! FUZ_findDiff() :
*   find the first different byte between buff1 and buff2.
*   presumes buff1 != buff2.
*   presumes a difference exists before end of either buffer.
*   Typically invoked after a checksum mismatch.
*/
static void FUZ_findDiff(const void* buff1, const void* buff2)
{
    const BYTE* const b1 = (const BYTE*)buff1;
    const BYTE* const b2 = (const BYTE*)buff2;
    size_t i=0;
    while (b1[i]==b2[i]) i++;
    DISPLAY("Wrong Byte at position %u\n", (unsigned)i);
}


static int FUZ_test(U32 seed, U32 nbCycles, const U32 startCycle, const double compressibility, U32 duration_s)
{
    unsigned long long bytes = 0;
    unsigned long long cbytes = 0;
    unsigned long long hcbytes = 0;
    unsigned long long ccbytes = 0;
    void* CNBuffer;
    char* compressedBuffer;
    char* decodedBuffer;
#   define FUZ_max   LZ5_COMPRESSBOUND(LEN)
    int ret;
    unsigned cycleNb;
#   define FUZ_CHECKTEST(cond, ...) if (cond) { printf("Test %u : ", testNb); printf(__VA_ARGS__); \
                                                printf(" (seed %u, cycle %u) \n", seed, cycleNb); goto _output_error; }
#   define FUZ_DISPLAYTEST          { testNb++; g_displayLevel<3 ? 0 : printf("%2u\b\b", testNb); if (g_displayLevel==4) fflush(stdout); }
    void* stateLZ5   = malloc(LZ5_sizeofState_Level1());
    void* stateLZ5HC = malloc(LZ5_sizeofState(0));
    LZ5_stream_t* LZ5dict;
    LZ5_stream_t* LZ5_streamHCPtr;
    U32 crcOrig, crcCheck;
    U32 coreRandState = seed;
    U32 randState = coreRandState ^ PRIME3;
    int result = 0;
    clock_t const clockStart = clock();
    clock_t const clockDuration = (clock_t)duration_s * CLOCKS_PER_SEC;


    /* init */
    LZ5_streamHCPtr = LZ5_createStream(0);
    LZ5dict = LZ5_createStream_Level1();

    /* Create compressible test buffer */
    CNBuffer = malloc(COMPRESSIBLE_NOISE_LENGTH);
    compressedBuffer = (char*)malloc(LZ5_compressBound(FUZ_MAX_BLOCK_SIZE));
    decodedBuffer = (char*)malloc(FUZ_MAX_DICT_SIZE + FUZ_MAX_BLOCK_SIZE);

    if (!stateLZ5 || !stateLZ5HC || !LZ5dict || !LZ5_streamHCPtr || !CNBuffer || !compressedBuffer || !decodedBuffer) goto _output_error;

    FUZ_fillCompressibleNoiseBuffer(CNBuffer, COMPRESSIBLE_NOISE_LENGTH, compressibility, &randState);

    /* move to startCycle */
    for (cycleNb = 0; cycleNb < startCycle; cycleNb++) {
        (void)FUZ_rand(&coreRandState);

        if (0) {   /* some problems can be related to dictionary re-use; in this case, enable this loop */
            int dictSize, blockSize, blockStart;
            char* dict;
            char* block;
            FUZ_displayUpdate(cycleNb);
            randState = coreRandState ^ PRIME3;
            blockSize  = FUZ_rand(&randState) % FUZ_MAX_BLOCK_SIZE;
            blockStart = FUZ_rand(&randState) % (COMPRESSIBLE_NOISE_LENGTH - blockSize);
            dictSize   = FUZ_rand(&randState) % FUZ_MAX_DICT_SIZE;
            if (dictSize > blockStart) dictSize = blockStart;
            block = ((char*)CNBuffer) + blockStart;
            dict = block - dictSize;
            LZ5_loadDict(LZ5dict, dict, dictSize);
            LZ5_compress_continue(LZ5dict, block, compressedBuffer, blockSize, LZ5_compressBound(blockSize));
            LZ5_loadDict(LZ5dict, dict, dictSize);
            LZ5_compress_continue(LZ5dict, block, compressedBuffer, blockSize, LZ5_compressBound(blockSize));
            LZ5_loadDict(LZ5dict, dict, dictSize);
            LZ5_compress_continue(LZ5dict, block, compressedBuffer, blockSize, LZ5_compressBound(blockSize));
    }   }

    /* Main test loop */
    for (cycleNb = startCycle; (cycleNb < nbCycles) || (FUZ_GetClockSpan(clockStart) < clockDuration) ; cycleNb++) {
        U32 testNb = 0;
        char* dict;
        char* block;
        int dictSize, blockSize, blockStart, compressedSize, HCcompressedSize;
        int blockContinueCompressedSize;

        FUZ_displayUpdate(cycleNb);
        (void)FUZ_rand(&coreRandState);
        randState = coreRandState ^ PRIME3;

        /* Select block to test */
        blockSize  = (FUZ_rand(&randState) % (FUZ_MAX_BLOCK_SIZE-1)) + 1;
        blockStart = FUZ_rand(&randState) % (COMPRESSIBLE_NOISE_LENGTH - blockSize);
        dictSize   = FUZ_rand(&randState) % FUZ_MAX_DICT_SIZE;
        if (dictSize > blockStart) dictSize = blockStart;
        block = ((char*)CNBuffer) + blockStart;
        dict = block - dictSize;

        /* Compression tests */

        /* Test compression HC */
        FUZ_DISPLAYTEST;
        ret = LZ5_compress(block, compressedBuffer, blockSize, LZ5_compressBound(blockSize), 0);
        FUZ_CHECKTEST(ret==0, "LZ5_compress() failed");
        HCcompressedSize = ret;

        /* Test compression HC using external state */
        FUZ_DISPLAYTEST;
        ret = LZ5_compress_extState(stateLZ5HC, block, compressedBuffer, blockSize, LZ5_compressBound(blockSize), 0);
        FUZ_CHECKTEST(ret==0, "LZ5_compress_extState() failed");

        /* Test compression using external state */
        FUZ_DISPLAYTEST;
        ret = LZ5_compress_extState_Level1(stateLZ5, block, compressedBuffer, blockSize, LZ5_compressBound(blockSize));
        FUZ_CHECKTEST(ret==0, "LZ5_compress_extState_Level1(1) failed");

        /* Test compression */
        FUZ_DISPLAYTEST;
        ret = LZ5_compress_Level1(block, compressedBuffer, blockSize, LZ5_compressBound(blockSize));
        FUZ_CHECKTEST(ret==0, "LZ5_compress_Level1() failed");
        compressedSize = ret;

        /* Decompression tests */

        crcOrig = XXH32(block, blockSize, 0);

        /* Test decoding with output size exactly what's necessary => must work */
        FUZ_DISPLAYTEST;
        decodedBuffer[blockSize] = 0;
        ret = LZ5_decompress_safe(compressedBuffer, decodedBuffer, compressedSize, blockSize);
        FUZ_CHECKTEST(ret<0, "LZ5_decompress_safe failed despite sufficient space");
        FUZ_CHECKTEST(ret!=blockSize, "LZ5_decompress_safe did not regenerate original data");
        FUZ_CHECKTEST(decodedBuffer[blockSize], "LZ5_decompress_safe overrun specified output buffer size");
        crcCheck = XXH32(decodedBuffer, blockSize, 0);
        FUZ_CHECKTEST(crcCheck!=crcOrig, "LZ5_decompress_safe corrupted decoded data");

        // Test decoding with more than enough output size => must work
        FUZ_DISPLAYTEST;
        decodedBuffer[blockSize] = 0;
        decodedBuffer[blockSize+1] = 0;
        ret = LZ5_decompress_safe(compressedBuffer, decodedBuffer, compressedSize, blockSize+1);
        FUZ_CHECKTEST(ret<0, "LZ5_decompress_safe failed despite amply sufficient space");
        FUZ_CHECKTEST(ret!=blockSize, "LZ5_decompress_safe did not regenerate original data");
        //FUZ_CHECKTEST(decodedBuffer[blockSize], "LZ5_decompress_safe wrote more than (unknown) target size");   // well, is that an issue ?
        FUZ_CHECKTEST(decodedBuffer[blockSize+1], "LZ5_decompress_safe overrun specified output buffer size");
        crcCheck = XXH32(decodedBuffer, blockSize, 0);
        FUZ_CHECKTEST(crcCheck!=crcOrig, "LZ5_decompress_safe corrupted decoded data");

        // Test decoding with output size being one byte too short => must fail
        FUZ_DISPLAYTEST;
        decodedBuffer[blockSize-1] = 0;
        ret = LZ5_decompress_safe(compressedBuffer, decodedBuffer, compressedSize, blockSize-1);
        FUZ_CHECKTEST(ret>=0, "LZ5_decompress_safe should have failed, due to Output Size being one byte too short");
        FUZ_CHECKTEST(decodedBuffer[blockSize-1], "LZ5_decompress_safe overrun specified output buffer size");

        // Test decoding with output size being 10 bytes too short => must fail
        FUZ_DISPLAYTEST;
        if (blockSize>10)
        {
            decodedBuffer[blockSize-10] = 0;
            ret = LZ5_decompress_safe(compressedBuffer, decodedBuffer, compressedSize, blockSize-10);
            FUZ_CHECKTEST(ret>=0, "LZ5_decompress_safe should have failed, due to Output Size being 10 bytes too short");
            FUZ_CHECKTEST(decodedBuffer[blockSize-10], "LZ5_decompress_safe overrun specified output buffer size");
        }

        // Test decoding with input size being one byte too short => must fail
        FUZ_DISPLAYTEST;
        ret = LZ5_decompress_safe(compressedBuffer, decodedBuffer, compressedSize-1, blockSize);
        FUZ_CHECKTEST(ret>=0, "LZ5_decompress_safe should have failed, due to input size being one byte too short (blockSize=%i, ret=%i, compressedSize=%i)", blockSize, ret, compressedSize);

        // Test decoding with input size being one byte too large => must fail
        FUZ_DISPLAYTEST;
        decodedBuffer[blockSize] = 0;
        compressedBuffer[compressedSize] = 0; /* valgrind */
        ret = LZ5_decompress_safe(compressedBuffer, decodedBuffer, compressedSize+1, blockSize);
        FUZ_CHECKTEST(ret>=0, "LZ5_decompress_safe should have failed, due to input size being too large");
        FUZ_CHECKTEST(decodedBuffer[blockSize], "LZ5_decompress_safe overrun specified output buffer size");

        // Test partial decoding with target output size being max/2 => must work
        FUZ_DISPLAYTEST;
        ret = LZ5_decompress_safe_partial(compressedBuffer, decodedBuffer, compressedSize, blockSize/2, blockSize);
        FUZ_CHECKTEST(ret<0, "LZ5_decompress_safe_partial failed despite sufficient space");

        // Test partial decoding with target output size being just below max => must work
        FUZ_DISPLAYTEST;
        ret = LZ5_decompress_safe_partial(compressedBuffer, decodedBuffer, compressedSize, blockSize-3, blockSize);
        FUZ_CHECKTEST(ret<0, "LZ5_decompress_safe_partial failed despite sufficient space");

        /* Test Compression with limited output size */

        /* Test compression with output size being exactly what's necessary (should work) */
        FUZ_DISPLAYTEST;
        ret = LZ5_compress_Level1(block, compressedBuffer, blockSize, compressedSize);
        FUZ_CHECKTEST(ret==0, "LZ5_compress_Level1() failed despite sufficient space");

        /* Test compression with output size being exactly what's necessary and external state (should work) */
        FUZ_DISPLAYTEST;
        ret = LZ5_compress_extState_Level1(stateLZ5, block, compressedBuffer, blockSize, compressedSize);
        FUZ_CHECKTEST(ret==0, "LZ5_compress_extState_Level1() failed despite sufficient space");

        /* Test HC compression with output size being exactly what's necessary (should work) */
        FUZ_DISPLAYTEST;
        ret = LZ5_compress(block, compressedBuffer, blockSize, HCcompressedSize, 0);
        FUZ_CHECKTEST(ret==0, "LZ5_compress() (limitedOutput) failed despite sufficient space");

        /* Test HC compression with output size being exactly what's necessary (should work) */
        FUZ_DISPLAYTEST;
        ret = LZ5_compress_extState(stateLZ5HC, block, compressedBuffer, blockSize, HCcompressedSize, 0);
        FUZ_CHECKTEST(ret==0, "LZ5_compress_extState() failed despite sufficient space");

        /* Test compression with missing bytes into output buffer => must fail */
        FUZ_DISPLAYTEST;
        {   int missingBytes = (FUZ_rand(&randState) % 0x3F) + 1;
            if (missingBytes >= compressedSize) missingBytes = compressedSize-1;
            missingBytes += !missingBytes;   /* avoid special case missingBytes==0 */
            compressedBuffer[compressedSize-missingBytes] = 0;
            ret = LZ5_compress_Level1(block, compressedBuffer, blockSize, compressedSize-missingBytes);
            FUZ_CHECKTEST(ret, "LZ5_compress_Level1 should have failed (output buffer too small by %i byte)", missingBytes);
            FUZ_CHECKTEST(compressedBuffer[compressedSize-missingBytes], "LZ5_compress_Level1 overran output buffer ! (%i missingBytes)", missingBytes)
        }

        /* Test HC compression with missing bytes into output buffer => must fail */
        FUZ_DISPLAYTEST;
        {   int missingBytes = (FUZ_rand(&randState) % 0x3F) + 1;
            if (missingBytes >= HCcompressedSize) missingBytes = HCcompressedSize-1;
            missingBytes += !missingBytes;   /* avoid special case missingBytes==0 */
            compressedBuffer[HCcompressedSize-missingBytes] = 0;
            ret = LZ5_compress(block, compressedBuffer, blockSize, HCcompressedSize-missingBytes, 0);
            FUZ_CHECKTEST(ret, "LZ5_compress(limitedOutput) should have failed (output buffer too small by %i byte)", missingBytes);
            FUZ_CHECKTEST(compressedBuffer[HCcompressedSize-missingBytes], "LZ5_compress overran output buffer ! (%i missingBytes)", missingBytes)
        }


        /*-******************/
        /* Dictionary tests */
        /*-******************/

        /* Compress using dictionary */
        FUZ_DISPLAYTEST;
        {   LZ5_stream_t* LZ5_stream = LZ5_createStream_Level1();
            FUZ_CHECKTEST(LZ5_stream==NULL, "LZ5_createStream_Level1() allocation failed");
            LZ5_stream = LZ5_resetStream_Level1(LZ5_stream);
            FUZ_CHECKTEST(LZ5_stream==NULL, "LZ5_resetStream_Level1() failed");
            LZ5_compress_continue (LZ5_stream, dict, compressedBuffer, dictSize, LZ5_compressBound(dictSize));   /* Just to fill hash tables */
            blockContinueCompressedSize = LZ5_compress_continue (LZ5_stream, block, compressedBuffer, blockSize, LZ5_compressBound(blockSize));
            FUZ_CHECKTEST(blockContinueCompressedSize==0, "LZ5_compress_continue failed");
            LZ5_freeStream(LZ5_stream);
        }

        /* Compress using External dictionary */
        FUZ_DISPLAYTEST;
        dict -= (FUZ_rand(&randState) & 0xF) + 1;   /* Separation, so it is an ExtDict */
        if (dict < (char*)CNBuffer) dict = (char*)CNBuffer;
        LZ5_loadDict(LZ5dict, dict, dictSize);
        blockContinueCompressedSize = LZ5_compress_continue(LZ5dict, block, compressedBuffer, blockSize, LZ5_compressBound(blockSize));
        FUZ_CHECKTEST(blockContinueCompressedSize==0, "LZ5_compress_continue failed");

        FUZ_DISPLAYTEST;
        LZ5_loadDict(LZ5dict, dict, dictSize);
        ret = LZ5_compress_continue(LZ5dict, block, compressedBuffer, blockSize, blockContinueCompressedSize-1);
        FUZ_CHECKTEST(ret>0, "LZ5_compress_continue using ExtDict should fail : one missing byte for output buffer : %i written, %i buffer", ret, blockContinueCompressedSize);

        FUZ_DISPLAYTEST;
        LZ5_loadDict(LZ5dict, dict, dictSize);
        ret = LZ5_compress_continue(LZ5dict, block, compressedBuffer, blockSize, blockContinueCompressedSize);
        FUZ_CHECKTEST(ret!=blockContinueCompressedSize, "LZ5_compress_limitedOutput_compressed size is different (%i != %i)", ret, blockContinueCompressedSize);
        FUZ_CHECKTEST(ret<=0, "LZ5_compress_continue should work : enough size available within output buffer");

        FUZ_DISPLAYTEST;
        decodedBuffer[blockSize] = 0;
        ret = LZ5_decompress_safe_usingDict(compressedBuffer, decodedBuffer, blockContinueCompressedSize, blockSize, dict, dictSize);
        FUZ_CHECKTEST(ret!=blockSize, "2LZ5_decompress_safe_usingDict did not regenerate original data ret[%d]!=blockSize[%d]", (int)ret, (int)blockSize);
        FUZ_CHECKTEST(decodedBuffer[blockSize], "LZ5_decompress_safe_usingDict overrun specified output buffer size")
            crcCheck = XXH32(decodedBuffer, blockSize, 0);
        FUZ_CHECKTEST(crcCheck!=crcOrig, "LZ5_decompress_safe_usingDict corrupted decoded data");

        FUZ_DISPLAYTEST;
        decodedBuffer[blockSize-1] = 0;
        ret = LZ5_decompress_safe_usingDict(compressedBuffer, decodedBuffer, blockContinueCompressedSize, blockSize-1, dict, dictSize);
        FUZ_CHECKTEST(ret>=0, "LZ5_decompress_safe_usingDict should have failed : not enough output size (-1 byte)");
        FUZ_CHECKTEST(decodedBuffer[blockSize-1], "LZ5_decompress_safe_usingDict overrun specified output buffer size");

        FUZ_DISPLAYTEST;
        {   U32 const missingBytes = (FUZ_rand(&randState) & 0xF) + 2;
            if ((U32)blockSize > missingBytes) {
                decodedBuffer[blockSize-missingBytes] = 0;
                ret = LZ5_decompress_safe_usingDict(compressedBuffer, decodedBuffer, blockContinueCompressedSize, blockSize-missingBytes, dict, dictSize);
                FUZ_CHECKTEST(ret>=0, "LZ5_decompress_safe_usingDict should have failed : output buffer too small (-%u byte)", missingBytes);
                FUZ_CHECKTEST(decodedBuffer[blockSize-missingBytes], "LZ5_decompress_safe_usingDict overrun specified output buffer size (-%u byte) (blockSize=%i)", missingBytes, blockSize);
        }   }

        /* Compress HC using External dictionary */
        FUZ_DISPLAYTEST;
        dict -= (FUZ_rand(&randState) & 7);    /* even bigger separation */
        if (dict < (char*)CNBuffer) dict = (char*)CNBuffer;
        LZ5_streamHCPtr = LZ5_resetStream (LZ5_streamHCPtr, FUZ_rand(&randState) & 0x7);
        FUZ_CHECKTEST(LZ5_streamHCPtr==NULL, "LZ5_resetStream failed");
        LZ5_loadDict(LZ5_streamHCPtr, dict, dictSize);
        blockContinueCompressedSize = LZ5_compress_continue(LZ5_streamHCPtr, block, compressedBuffer, blockSize, LZ5_compressBound(blockSize));
        FUZ_CHECKTEST(blockContinueCompressedSize==0, "LZ5_compress_continue failed");

        FUZ_DISPLAYTEST;
        LZ5_loadDict(LZ5_streamHCPtr, dict, dictSize);
        ret = LZ5_compress_continue(LZ5_streamHCPtr, block, compressedBuffer, blockSize, blockContinueCompressedSize-1);
        FUZ_CHECKTEST(ret>0, "LZ5_compress_continue using ExtDict should fail : one missing byte for output buffer");

        FUZ_DISPLAYTEST;
        LZ5_loadDict(LZ5_streamHCPtr, dict, dictSize);
        ret = LZ5_compress_continue(LZ5_streamHCPtr, block, compressedBuffer, blockSize, blockContinueCompressedSize);
        FUZ_CHECKTEST(ret!=blockContinueCompressedSize, "LZ5_compress_limitedOutput_compressed size is different (%i != %i)", ret, blockContinueCompressedSize);
        FUZ_CHECKTEST(ret<=0, "LZ5_compress_continue should work : enough size available within output buffer");

        FUZ_DISPLAYTEST;
        decodedBuffer[blockSize] = 0;
        ret = LZ5_decompress_safe_usingDict(compressedBuffer, decodedBuffer, blockContinueCompressedSize, blockSize, dict, dictSize);
        FUZ_CHECKTEST(ret!=blockSize, "3LZ5_decompress_safe_usingDict did not regenerate original data ret[%d]!=blockSize[%d]", (int)ret, (int)blockSize);
        FUZ_CHECKTEST(decodedBuffer[blockSize], "LZ5_decompress_safe_usingDict overrun specified output buffer size")
            crcCheck = XXH32(decodedBuffer, blockSize, 0);
        if (crcCheck!=crcOrig)
            FUZ_findDiff(block, decodedBuffer);
        FUZ_CHECKTEST(crcCheck!=crcOrig, "LZ5_decompress_safe_usingDict corrupted decoded data");

        /* ***** End of tests *** */
        /* Fill stats */
        bytes += blockSize;
        cbytes += compressedSize;
        hcbytes += HCcompressedSize;
        ccbytes += blockContinueCompressedSize;
    }

    if (nbCycles<=1) nbCycles = cycleNb;   /* end by time */
    bytes += !bytes;   /* avoid division by 0 */
    printf("\r%7u /%7u   - ", cycleNb, nbCycles);
    printf("all tests completed successfully \n");
    printf("compression ratio: %0.3f%%\n", (double)cbytes/bytes*100);
    printf("HC compression ratio: %0.3f%%\n", (double)hcbytes/bytes*100);
    printf("ratio with dict: %0.3f%%\n", (double)ccbytes/bytes*100);

    /* release memory */
    {
_exit:
        free(CNBuffer);
        free(compressedBuffer);
        free(decodedBuffer);
        free(stateLZ5);
        free(stateLZ5HC);
        LZ5_freeStream(LZ5_streamHCPtr);
        LZ5_freeStream(LZ5dict);
        return result;

_output_error:
        result = 1;
        goto _exit;
    }
}


#define testInputSize (192 KB)
#define testCompressedSize (128 KB)
#define ringBufferSize (8 KB)

static void FUZ_unitTests(U32 seed)
{
    const unsigned testNb = 0;
    const unsigned cycleNb= 0;
    char testInput[testInputSize];
    char testCompressed[testCompressedSize];
    char testVerify[testInputSize];
    char ringBuffer[ringBufferSize];
    U32 randState = seed ^ PRIME3;

    /* Init */
    FUZ_fillCompressibleNoiseBuffer(testInput, testInputSize, 0.50, &randState);

    /* 32-bits address space overflow test */
    FUZ_AddressOverflow(&randState);

    /* LZ5 streaming tests */
    {   LZ5_stream_t* statePtr;
        LZ5_stream_t* streamingState;
        U64 crcOrig;
        U64 crcNew;
        int result;

        /* Allocation test */
        statePtr = LZ5_createStream_Level1();
        FUZ_CHECKTEST(statePtr==NULL, "LZ5_createStream_Level1() allocation failed");
        LZ5_freeStream(statePtr);

        streamingState = LZ5_createStream_Level1();
        FUZ_CHECKTEST(streamingState==NULL, "LZ5_createStream_Level1() allocation failed");

        /* simple compression test */
        crcOrig = XXH64(testInput, testCompressedSize, 0);
        streamingState = LZ5_resetStream_Level1(streamingState);
        FUZ_CHECKTEST(streamingState==NULL, "LZ5_resetStream_Level1() failed");
        result = LZ5_compress_continue(streamingState, testInput, testCompressed, testCompressedSize, testCompressedSize-1);
        FUZ_CHECKTEST(result==0, "LZ5_compress_continue() compression failed");

        result = LZ5_decompress_safe(testCompressed, testVerify, result, testCompressedSize);
        FUZ_CHECKTEST(result!=(int)testCompressedSize, "LZ5_decompress_safe() decompression failed Level 1 (result=%d testCompressedSize=%d)", (int)result, (int)testCompressedSize);
        crcNew = XXH64(testVerify, testCompressedSize, 0);
        FUZ_CHECKTEST(crcOrig!=crcNew, "LZ5_decompress_safe() decompression corruption");

        /* ring buffer test */
        {   XXH64_state_t xxhOrig;
            XXH64_state_t xxhNew;
            LZ5_streamDecode_t decodeState;
            const U32 maxMessageSizeLog = 10;
            const U32 maxMessageSizeMask = (1<<maxMessageSizeLog) - 1;
            U32 messageSize = (FUZ_rand(&randState) & maxMessageSizeMask) + 1;
            U32 iNext = 0;
            U32 rNext = 0;
            U32 dNext = 0;
            const U32 dBufferSize = ringBufferSize + maxMessageSizeMask;

            XXH64_reset(&xxhOrig, 0);
            XXH64_reset(&xxhNew, 0);
            streamingState = LZ5_resetStream_Level1(streamingState);
            FUZ_CHECKTEST(streamingState==NULL, "LZ5_resetStream_Level1() failed");
            LZ5_setStreamDecode(&decodeState, NULL, 0);

            while (iNext + messageSize < testCompressedSize) {
                XXH64_update(&xxhOrig, testInput + iNext, messageSize);
                crcOrig = XXH64_digest(&xxhOrig);

                memcpy (ringBuffer + rNext, testInput + iNext, messageSize);
                result = LZ5_compress_continue(streamingState, ringBuffer + rNext, testCompressed, messageSize, testCompressedSize-ringBufferSize);
                FUZ_CHECKTEST(result==0, "LZ5_compress_continue() compression failed");

                result = LZ5_decompress_safe_continue(&decodeState, testCompressed, testVerify + dNext, result, messageSize);
                FUZ_CHECKTEST(result!=(int)messageSize, "ringBuffer : LZ5_decompress_safe() test failed");

                XXH64_update(&xxhNew, testVerify + dNext, messageSize);
                crcNew = XXH64_digest(&xxhNew);
                FUZ_CHECKTEST(crcOrig!=crcNew, "LZ5_decompress_safe() decompression corruption");

                /* prepare next message */
                iNext += messageSize;
                rNext += messageSize;
                dNext += messageSize;
                messageSize = (FUZ_rand(&randState) & maxMessageSizeMask) + 1;
                if (rNext + messageSize > ringBufferSize) rNext = 0;
                if (dNext + messageSize > dBufferSize) dNext = 0;
            }
        }
        LZ5_freeStream(streamingState);
    }

    /* LZ5 streaming tests */
    {   LZ5_stream_t* streamPtr;
        U64 crcOrig;
        U64 crcNew;
        int result;

        /* Allocation test */
        streamPtr = LZ5_createStream(0);
        FUZ_CHECKTEST(streamPtr==NULL, "LZ5_createStream() allocation failed");

        /* simple HC compression test */
        crcOrig = XXH64(testInput, testCompressedSize, 0);
        streamPtr = LZ5_resetStream(streamPtr, 0);
        FUZ_CHECKTEST(streamPtr==NULL, "LZ5_resetStream failed");
        result = LZ5_compress_continue(streamPtr, testInput, testCompressed, testCompressedSize, testCompressedSize-1);
        FUZ_CHECKTEST(result==0, "LZ5_compress_continue() compression failed");

        result = LZ5_decompress_safe(testCompressed, testVerify, result, testCompressedSize);
        FUZ_CHECKTEST(result!=(int)testCompressedSize, "LZ5_decompress_safe() decompression failed Level 0 (result=%d testCompressedSize=%d)", (int)result, (int)testCompressedSize);
        crcNew = XXH64(testVerify, testCompressedSize, 0);
        FUZ_CHECKTEST(crcOrig!=crcNew, "LZ5_decompress_safe() decompression corruption");

        /* simple dictionary HC compression test */
        crcOrig = XXH64(testInput + testInputSize - testCompressedSize, testCompressedSize, 0);
        streamPtr = LZ5_resetStream(streamPtr, 0);
        FUZ_CHECKTEST(streamPtr==NULL, "LZ5_resetStream failed");
        LZ5_loadDict(streamPtr, testInput, testInputSize - testCompressedSize);
        result = LZ5_compress_continue(streamPtr, testInput + testInputSize - testCompressedSize, testCompressed, testCompressedSize, testCompressedSize-1);
        FUZ_CHECKTEST(result==0, "LZ5_compress_continue() dictionary compression failed : result = %i", result);

        result = LZ5_decompress_safe_usingDict(testCompressed, testVerify, result, testCompressedSize, testInput, testInputSize - testCompressedSize);
        FUZ_CHECKTEST(result!=(int)testCompressedSize, "LZ5_decompress_safe() simple dictionary decompression test failed");
        crcNew = XXH64(testVerify, testCompressedSize, 0);
        FUZ_CHECKTEST(crcOrig!=crcNew, "LZ5_decompress_safe() simple dictionary decompression test : corruption");

        /* multiple HC compression test with dictionary */
        {   int result1, result2;
            int segSize = testCompressedSize / 2;
            crcOrig = XXH64(testInput + segSize, testCompressedSize, 0);
            streamPtr = LZ5_resetStream(streamPtr, 0);
            FUZ_CHECKTEST(streamPtr==NULL, "LZ5_resetStream failed");
            LZ5_loadDict(streamPtr, testInput, segSize);
            result1 = LZ5_compress_continue(streamPtr, testInput + segSize, testCompressed, segSize, segSize -1);
            FUZ_CHECKTEST(result1==0, "LZ5_compress_continue() dictionary compression failed : result = %i", result1);
            result2 = LZ5_compress_continue(streamPtr, testInput + 2*segSize, testCompressed+result1, segSize, segSize-1);
            FUZ_CHECKTEST(result2==0, "LZ5_compress_continue() dictionary compression failed : result = %i", result2);

            result = LZ5_decompress_safe_usingDict(testCompressed, testVerify, result1, segSize, testInput, segSize);
            FUZ_CHECKTEST(result!=segSize, "LZ5_decompress_safe() dictionary decompression part 1 failed");
            result = LZ5_decompress_safe_usingDict(testCompressed+result1, testVerify+segSize, result2, segSize, testInput, 2*segSize);
            FUZ_CHECKTEST(result!=segSize, "LZ5_decompress_safe() dictionary decompression part 2 failed");
            crcNew = XXH64(testVerify, testCompressedSize, 0);
            FUZ_CHECKTEST(crcOrig!=crcNew, "LZ5_decompress_safe() dictionary decompression corruption");
        }

        /* remote dictionary HC compression test */
        crcOrig = XXH64(testInput + testInputSize - testCompressedSize, testCompressedSize, 0);
        streamPtr = LZ5_resetStream(streamPtr, 0);
        FUZ_CHECKTEST(streamPtr==NULL, "LZ5_resetStream failed");
        LZ5_loadDict(streamPtr, testInput, 32 KB);
        result = LZ5_compress_continue(streamPtr, testInput + testInputSize - testCompressedSize, testCompressed, testCompressedSize, testCompressedSize-1);
        FUZ_CHECKTEST(result==0, "LZ5_compress_continue() remote dictionary failed : result = %i", result);

        result = LZ5_decompress_safe_usingDict(testCompressed, testVerify, result, testCompressedSize, testInput, 32 KB);
        FUZ_CHECKTEST(result!=(int)testCompressedSize, "LZ5_decompress_safe_usingDict() decompression failed following remote dictionary HC compression test");
        crcNew = XXH64(testVerify, testCompressedSize, 0);
        FUZ_CHECKTEST(crcOrig!=crcNew, "LZ5_decompress_safe_usingDict() decompression corruption");

        /* multiple HC compression with ext. dictionary */
        {   XXH64_state_t crcOrigState;
            XXH64_state_t crcNewState;
            const char* dict = testInput + 3;
            int dictSize = (FUZ_rand(&randState) & 8191);
            char* dst = testVerify;

            size_t segStart = dictSize + 7;
            int segSize = (FUZ_rand(&randState) & 8191);
            int segNb = 1;

            streamPtr = LZ5_resetStream(streamPtr, 0);
            FUZ_CHECKTEST(streamPtr==NULL, "LZ5_resetStream failed");
            LZ5_loadDict(streamPtr, dict, dictSize);

            XXH64_reset(&crcOrigState, 0);
            XXH64_reset(&crcNewState, 0);

            while (segStart + segSize < testInputSize) {
                XXH64_update(&crcOrigState, testInput + segStart, segSize);
                crcOrig = XXH64_digest(&crcOrigState);
                result = LZ5_compress_continue(streamPtr, testInput + segStart, testCompressed, segSize, LZ5_compressBound(segSize));
                FUZ_CHECKTEST(result==0, "LZ5_compress_continue() dictionary compression failed : result = %i", result);

                result = LZ5_decompress_safe_usingDict(testCompressed, dst, result, segSize, dict, dictSize);
                FUZ_CHECKTEST(result!=segSize, "LZ5_decompress_safe_usingDict() dictionary decompression part %i failed", segNb);
                XXH64_update(&crcNewState, dst, segSize);
                crcNew = XXH64_digest(&crcNewState);
                if (crcOrig!=crcNew) {
                    size_t c=0;
                    while (dst[c] == testInput[segStart+c]) c++;
                    DISPLAY("Bad decompression at %u / %u \n", (U32)c, (U32)segSize);
                }
                FUZ_CHECKTEST(crcOrig!=crcNew, "LZ5_decompress_safe_usingDict() part %i corruption", segNb);

                dict = dst;
                //dict = testInput + segStart;
                dictSize = segSize;

                dst += segSize + 1;
                segNb ++;

                segStart += segSize + (FUZ_rand(&randState) & 0xF) + 1;
                segSize = (FUZ_rand(&randState) & 8191);
            }
        }

        /* ring buffer test */
        {   XXH64_state_t xxhOrig;
            XXH64_state_t xxhNew;
            LZ5_streamDecode_t decodeState;
            const U32 maxMessageSizeLog = 10;
            const U32 maxMessageSizeMask = (1<<maxMessageSizeLog) - 1;
            U32 messageSize = (FUZ_rand(&randState) & maxMessageSizeMask) + 1;
            U32 iNext = 0;
            U32 rNext = 0;
            U32 dNext = 0;
            const U32 dBufferSize = ringBufferSize + maxMessageSizeMask;

            XXH64_reset(&xxhOrig, 0);
            XXH64_reset(&xxhNew, 0);
            streamPtr = LZ5_resetStream(streamPtr, 0);
            FUZ_CHECKTEST(streamPtr==NULL, "LZ5_resetStream failed");
            LZ5_setStreamDecode(&decodeState, NULL, 0);

            while (iNext + messageSize < testCompressedSize) {
                XXH64_update(&xxhOrig, testInput + iNext, messageSize);
                crcOrig = XXH64_digest(&xxhOrig);

                memcpy (ringBuffer + rNext, testInput + iNext, messageSize);
                result = LZ5_compress_continue(streamPtr, ringBuffer + rNext, testCompressed, messageSize, testCompressedSize-ringBufferSize);
                FUZ_CHECKTEST(result==0, "LZ5_compress_continue() compression failed");

                result = LZ5_decompress_safe_continue(&decodeState, testCompressed, testVerify + dNext, result, messageSize);
                FUZ_CHECKTEST(result!=(int)messageSize, "ringBuffer : LZ5_decompress_safe() test failed");

                XXH64_update(&xxhNew, testVerify + dNext, messageSize);
                crcNew = XXH64_digest(&xxhNew);
                FUZ_CHECKTEST(crcOrig!=crcNew, "LZ5_decompress_safe() decompression corruption");

                /* prepare next message */
                iNext += messageSize;
                rNext += messageSize;
                dNext += messageSize;
                messageSize = (FUZ_rand(&randState) & maxMessageSizeMask) + 1;
                if (rNext + messageSize > ringBufferSize) rNext = 0;
                if (dNext + messageSize > dBufferSize) dNext = 0;
            }
        }

        /* small decoder-side ring buffer test */
        {   XXH64_state_t xxhOrig;
            XXH64_state_t xxhNew;
            LZ5_streamDecode_t decodeState;
            const U32 maxMessageSizeLog = 12;
            const U32 maxMessageSizeMask = (1<<maxMessageSizeLog) - 1;
            U32 messageSize;
            U32 totalMessageSize = 0;
            U32 iNext = 0;
            U32 dNext = 0;
            const U32 dBufferSize = 64 KB;

            XXH64_reset(&xxhOrig, 0);
            XXH64_reset(&xxhNew, 0);
            streamPtr = LZ5_resetStream(streamPtr, 0);
            FUZ_CHECKTEST(streamPtr==NULL, "LZ5_resetStream failed");
            LZ5_setStreamDecode(&decodeState, NULL, 0);

#define BSIZE1 65537
#define BSIZE2 16435

            /* first block */
            messageSize = BSIZE1;
            XXH64_update(&xxhOrig, testInput + iNext, messageSize);
            crcOrig = XXH64_digest(&xxhOrig);

            result = LZ5_compress_continue(streamPtr, testInput + iNext, testCompressed, messageSize, testCompressedSize-ringBufferSize);
            FUZ_CHECKTEST(result==0, "LZ5_compress_continue() compression failed");

            result = LZ5_decompress_safe_continue(&decodeState, testCompressed, testVerify + dNext, result, messageSize);
            FUZ_CHECKTEST(result!=(int)messageSize, "64K D.ringBuffer : LZ5_decompress_safe() test failed");

            XXH64_update(&xxhNew, testVerify + dNext, messageSize);
            crcNew = XXH64_digest(&xxhNew);
            FUZ_CHECKTEST(crcOrig!=crcNew, "LZ5_decompress_safe() decompression corruption");

            /* prepare next message */
            totalMessageSize += messageSize;
            messageSize = BSIZE2;
            dNext = 0;
            iNext = 132000;
            memcpy(testInput + iNext, testInput + WILDCOPYLENGTH, messageSize);

            while (totalMessageSize < 9 MB) {
                XXH64_update(&xxhOrig, testInput + iNext, messageSize);
                crcOrig = XXH64_digest(&xxhOrig);

                result = LZ5_compress_continue(streamPtr, testInput + iNext, testCompressed, messageSize, testCompressedSize-ringBufferSize);
                FUZ_CHECKTEST(result==0, "LZ5_compress_continue() compression failed");

                result = LZ5_decompress_safe_continue(&decodeState, testCompressed, testVerify + dNext, result, messageSize);
                FUZ_CHECKTEST(result!=(int)messageSize, "64K D.ringBuffer : LZ5_decompress_safe() test failed");

                XXH64_update(&xxhNew, testVerify + dNext, messageSize);
                crcNew = XXH64_digest(&xxhNew);
                if (crcOrig != crcNew)
                    FUZ_findDiff(testInput + iNext, testVerify + dNext);
                FUZ_CHECKTEST(crcOrig!=crcNew, "LZ5_decompress_safe() decompression corruption during small decoder-side ring buffer test");

                /* prepare next message */
                dNext += messageSize;
                totalMessageSize += messageSize;
                messageSize = (FUZ_rand(&randState) & maxMessageSizeMask) + 1;
                iNext = (FUZ_rand(&randState) & 65535);
                if (dNext > dBufferSize) dNext = 0;
            }
        }
        
        LZ5_freeStream(streamPtr);
    }

    printf("All unit tests completed successfully \n");
    return;
_output_error:
    exit(1);
}


static int FUZ_usage(char* programName)
{
    DISPLAY( "Usage :\n");
    DISPLAY( "      %s [args]\n", programName);
    DISPLAY( "\n");
    DISPLAY( "Arguments :\n");
    DISPLAY( " -i#    : Nb of tests (default:%i) \n", NB_ATTEMPTS);
    DISPLAY( " -T#    : Duration of tests, in seconds (default: use Nb of tests) \n");
    DISPLAY( " -s#    : Select seed (default:prompt user)\n");
    DISPLAY( " -t#    : Select starting test number (default:0)\n");
    DISPLAY( " -P#    : Select compressibility in %% (default:%i%%)\n", FUZ_COMPRESSIBILITY_DEFAULT);
    DISPLAY( " -v     : verbose\n");
    DISPLAY( " -p     : pause at the end\n");
    DISPLAY( " -h     : display help and exit\n");
    return 0;
}


int main(int argc, char** argv)
{
    U32 seed=0;
    int seedset=0;
    int argNb;
    int nbTests = NB_ATTEMPTS;
    int testNb = 0;
    int proba = FUZ_COMPRESSIBILITY_DEFAULT;
    int pause = 0;
    char* programName = argv[0];
    U32 duration = 0;

    /* Check command line */
    for(argNb=1; argNb<argc; argNb++) {
        char* argument = argv[argNb];

        if(!argument) continue;   // Protection if argument empty

        // Decode command (note : aggregated commands are allowed)
        if (argument[0]=='-') {
            if (!strcmp(argument, "--no-prompt")) { pause=0; seedset=1; g_displayLevel=1; continue; }
            argument++;

            while (*argument!=0) {
                switch(*argument)
                {
                case 'h':   /* display help */
                    return FUZ_usage(programName);

                case 'v':   /* verbose mode */
                    argument++;
                    g_displayLevel=4;
                    break;

                case 'p':   /* pause at the end */
                    argument++;
                    pause=1;
                    break;

                case 'i':
                    argument++;
                    nbTests = 0; duration = 0;
                    while ((*argument>='0') && (*argument<='9')) {
                        nbTests *= 10;
                        nbTests += *argument - '0';
                        argument++;
                    }
                    break;

                case 'T':
                    argument++;
                    nbTests = 0; duration = 0;
                    for (;;) {
                        switch(*argument)
                        {
                            case 'm': duration *= 60; argument++; continue;
                            case 's':
                            case 'n': argument++; continue;
                            case '0':
                            case '1':
                            case '2':
                            case '3':
                            case '4':
                            case '5':
                            case '6':
                            case '7':
                            case '8':
                            case '9': duration *= 10; duration += *argument++ - '0'; continue;
                        }
                        break;
                    }
                    break;

                case 's':
                    argument++;
                    seed=0; seedset=1;
                    while ((*argument>='0') && (*argument<='9')) {
                        seed *= 10;
                        seed += *argument - '0';
                        argument++;
                    }
                    break;

                case 't':   /* select starting test nb */
                    argument++;
                    testNb=0;
                    while ((*argument>='0') && (*argument<='9')) {
                        testNb *= 10;
                        testNb += *argument - '0';
                        argument++;
                    }
                    break;

                case 'P':  /* change probability */
                    argument++;
                    proba=0;
                    while ((*argument>='0') && (*argument<='9')) {
                        proba *= 10;
                        proba += *argument - '0';
                        argument++;
                    }
                    if (proba<0) proba=0;
                    if (proba>100) proba=100;
                    break;
                default: ;
                }
            }
        }
    }

    printf("Starting LZ5 fuzzer (%i-bits, v%s)\n", (int)(sizeof(size_t)*8), LZ5_VERSION_STRING);

    if (!seedset) {
        time_t const t = time(NULL);
        U32 const h = XXH32(&t, sizeof(t), 1);
        seed = h % 10000;
    }
    printf("Seed = %u\n", seed);

    if (proba!=FUZ_COMPRESSIBILITY_DEFAULT) printf("Compressibility : %i%%\n", proba);

    if (testNb==0) FUZ_unitTests(seed);

    if (nbTests<=0) nbTests=1;

    {   int const result = FUZ_test(seed, nbTests, testNb, ((double)proba) / 100, duration);
        if (pause) {
            DISPLAY("press enter ... \n");
            (void)getchar();
        }
        return result;
    }
}
