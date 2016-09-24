/*
  [0_MMMM_LLL] - 16-bit offset, 4-bit match length (4-15+), 3-bit literal length (0-7+)
  [1_MMMM_LLL] -   last offset, 4-bit match length (0-15+), 3-bit literal length (0-7+)
  flag 31      - 24-bit offset,        match length (47+),    no literal length
  flag 0-30    - 24-bit offset,  31 match lengths (16-46),    no literal length
*/

/*! LZ5_decompress_LZ5v2() :
 *  This generic decompression function cover all use cases.
 *  It shall be instantiated several times, using different sets of directives
 *  Note that it is important this generic function is really inlined,
 *  in order to remove useless branches during compilation optimization.
 */
FORCE_INLINE int LZ5_decompress_LZ5v2(
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
                 const size_t dictSize,         /* note : = 0 if noDict */
                 int compressionLevel
                 )
{
    /* Local Variables */
    const BYTE* ip = (const BYTE*) source;
    const BYTE* const iend = ip + inputSize;

    BYTE* op = (BYTE*) dest;
    BYTE* const oend = op + outputSize;
    BYTE* cpy = NULL;
    BYTE* oexit;
    const BYTE* const lowLimit = lowPrefix - dictSize;
    const BYTE* const dictEnd = (const BYTE*)dictStart + dictSize;

    const int safeDecode = (endOnInput==endOnInputSize);
    const int checkOffset = ((safeDecode) && (dictSize < (int)(LZ5_DICT_SIZE)));

    intptr_t last_off = -LZ5_INIT_LAST_OFFSET;
    intptr_t length = 0;
    ip++; // skip compression level
    (void)compressionLevel;
    (void)LZ5_wildCopy;

    /* Special cases */
    if (partialDecoding) {
        oexit = op + targetOutputSize;
        if (oexit > oend-MFLIMIT) oexit = oend-MFLIMIT;  /* targetOutputSize too high => decode everything */
    }
    else oexit = oend - WILDCOPYLENGTH;
    if ((endOnInput) && (unlikely(outputSize==0))) return ((inputSize==2) && (*ip==0)) ? 0 : -1;  /* Empty output buffer */
    if ((!endOnInput) && (unlikely(outputSize==0))) return (*ip==0?1:-1);

    /* Main Loop : decode sequences */
    while (1) {
        unsigned token;
        const BYTE* match;
    //    intptr_t litLength;

        /* get literal length */
        token = *ip++;
   //     LZ5_LOG_DECOMPRESS_LZ5v2("token : %u\n", (U32)token);
        if (token >= 32)
        {
            if ((length=(token & RUN_MASK_LZ5v2)) == RUN_MASK_LZ5v2) {
                if ((endOnInput) && unlikely(ip > iend - 5) ) { LZ5_LOG_DECOMPRESS_LZ5v2("0"); goto _output_error; } 
                length = *ip;
                if unlikely(length >= 254) {
                    if (length == 254) {
                        length = MEM_readLE16(ip+1);
                        ip += 2;
                    } else {
                        length = MEM_readLE32(ip+1);
                        ip += 4;
                    }
                }
                length += RUN_MASK_LZ5v2;
                ip++;
                if ((safeDecode) && unlikely((size_t)(op+length)<(size_t)(op))) { LZ5_LOG_DECOMPRESS_LZ5v2("1"); goto _output_error; }  /* overflow detection */
                if ((safeDecode) && unlikely((size_t)(ip+length)<(size_t)(ip))) { LZ5_LOG_DECOMPRESS_LZ5v2("2"); goto _output_error; }   /* overflow detection */
            }

            /* copy literals */
            cpy = op + length;
            if ( (cpy>oexit) || (endOnInput && (ip + length > iend - WILDCOPYLENGTH) ))
            {
                if (partialDecoding) {
                    if (cpy > oend) { LZ5_LOG_DECOMPRESS_LZ5v2("3"); goto _output_error; }                           /* Error : write attempt beyond end of output buffer */
                    if ((endOnInput) && (ip+length > iend)) { LZ5_LOG_DECOMPRESS_LZ5v2("4"); goto _output_error; }   /* Error : read attempt beyond end of input buffer */
                } else {
                    if ((!endOnInput) && (cpy != oend)) { LZ5_LOG_DECOMPRESS_LZ5v2("5"); goto _output_error; }       /* Error : block decoding must stop exactly there */
                    if ((endOnInput) && ((ip+length != iend) || (cpy > oend))) { LZ5_LOG_DECOMPRESS_LZ5v2("6"); goto _output_error; }   /* Error : input must be consumed */
                }
                memcpy(op, ip, length);
                ip += length;
                op += length;
                break;     /* Necessarily EOF, due to parsing restrictions */
            }

         //  litLength = length;

    #if 1
            LZ5_wildCopy16(op, ip, cpy);
            op = cpy;
            ip += length; 
    #else
            LZ5_copy8(op, ip);
            LZ5_copy8(op+8, ip+8);
            if (length > 16)
                LZ5_wildCopy16(op + 16, ip + 16, cpy);
            op = cpy;
            ip += length; 
    #endif

            /* get offset */
#if 1
            { /* branchless */
                intptr_t new_dist = MEM_readLE16(ip);
                uintptr_t use_distance = (uintptr_t)(token >> ML_RUN_BITS) - 1;
                last_off ^= use_distance & (last_off ^ -new_dist);
                ip = (BYTE*)((uintptr_t)ip + (use_distance & 2));
            }
#else
            if ((token >> ML_RUN_BITS_LZ5v2) == 0)
            {
                last_off = -(intptr_t)MEM_readLE16(ip); 
                ip += 2;
            //    LZ5v2_DEBUG("MEM_readLE16 offset=%d\n", (int)offset);
            }
#endif

            /* get matchlength */
            length = (token >> RUN_BITS_LZ5v2) & ML_MASK_LZ5v2;
          //  printf("length=%d token=%d\n", (int)length, (int)token);
            if (length == ML_MASK_LZ5v2) {
                if ((endOnInput) && unlikely(ip > iend - 5) ) { LZ5_LOG_DECOMPRESS_LZ5v2("7"); goto _output_error; } 
                length = *ip;
                if unlikely(length >= 254) {
                    if (length == 254) {
                        length = MEM_readLE16(ip+1);
                        ip += 2;
                    } else {
                        length = MEM_readLE32(ip+1);
                        ip += 4;
                    }
                }
                length += ML_MASK_LZ5v2;
                ip++;
                if ((safeDecode) && unlikely((size_t)(op+length)<(size_t)(op))) { LZ5_LOG_DECOMPRESS_LZ5v2("8"); goto _output_error; }  /* overflow detection */
            }

            DECOMPLOG_CODEWORDS_LZ5v2("T32+ literal=%u match=%u offset=%d ipos=%d opos=%d\n", (U32)litLength, (U32)length, (int)-last_off, (U32)((const char*)ip-source), (U32)((char*)op-dest));
        }
        else
        if (token < LAST_LONG_OFF)
        {
            if ((endOnInput) && unlikely(ip > iend - 3) ) { LZ5_LOG_DECOMPRESS_LZ5v2("9"); goto _output_error; } 
            length = token + MM_LONGOFF;
            last_off = -(intptr_t)MEM_readLE24(ip); 
            ip += 3;
            DECOMPLOG_CODEWORDS_LZ5v2("T0-30 literal=%u match=%u offset=%d\n", 0, (U32)length, (int)-last_off);
        }
        else 
#ifdef USE_8BIT_CODEWORDS
        if (token == LAST_LONG_OFF)
#endif
        { 
            if ((endOnInput) && unlikely(ip > iend - 8) ) { LZ5_LOG_DECOMPRESS_LZ5v2("10"); goto _output_error; } 
            length = *ip;
            if unlikely(length >= 254) {
                if (length == 254) {
                    length = MEM_readLE16(ip+1);
                    ip += 2;
                } else {
                    length = MEM_readLE32(ip+1);
                    ip += 4;
                }
                if ((safeDecode) && unlikely((size_t)(op+length)<(size_t)(op))) { LZ5_LOG_DECOMPRESS_LZ5v2("8"); goto _output_error; }  /* overflow detection */
            }
            ip++;
            length += LAST_LONG_OFF + MM_LONGOFF;

            last_off = -(intptr_t)MEM_readLE24(ip); 
            ip += 3;
            DECOMPLOG_CODEWORDS_LZ5v2("1match=%p lowLimit=%p\n", match, lowLimit);
        }
#ifdef USE_8BIT_CODEWORDS
        else
        if (token < LAST_SHORT_OFF)
        {
            if ((endOnInput) && unlikely(ip > iend - 1) ) { LZ5_LOG_DECOMPRESS_LZ5v2("9"); goto _output_error; } 
            length = token + MINMATCH - FIRST_SHORT_OFF;
            last_off = -(intptr_t)(BYTE)(*ip++);
            DECOMPLOG_CODEWORDS_LZ5v2("8BIT0-15 literal=%u match=%u offset=%d\n", 0, (U32)length, (int)-last_off);
        }
        else // if (token == LAST_SHORT_OFF)
        { 
            if ((endOnInput) && unlikely(ip > iend - 6) ) { LZ5_LOG_DECOMPRESS_LZ5v2("10"); goto _output_error; } 
            length = *ip;
            if unlikely(length >= 254) {
                if (length == 254) {
                    length = MEM_readLE16(ip+1);
                    ip += 2;
                } else {
                    length = MEM_readLE32(ip+1);
                    ip += 4;
                }
                if ((safeDecode) && unlikely((size_t)(op+length)<(size_t)(op))) { LZ5_LOG_DECOMPRESS_LZ5v2("8"); goto _output_error; }  /* overflow detection */
            }
            ip++;
            length += SHORT_OFF_COUNT + MINMATCH;
            last_off = -(intptr_t)(BYTE)(*ip++);
            DECOMPLOG_CODEWORDS_LZ5v2("8BIT16+ 1match=%p lowLimit=%p\n", match, lowLimit);
        }
#endif

        match = op + last_off;
        if ((checkOffset) && ((unlikely((uintptr_t)(-last_off) > (uintptr_t)op) || (match < lowLimit)))) { LZ5_LOG_DECOMPRESS_LZ5v2("lowPrefix[%p]-dictSize[%d]=lowLimit[%p] match[%p]=op[%p]-last_off[%d]\n", lowPrefix, (int)dictSize, lowLimit, match, op, (int)last_off); goto _output_error; }  /* Error : offset outside buffers */

        /* check external dictionary */
        if ((dict==usingExtDict) && (match < lowPrefix)) {
            if (unlikely(op + length > oend - WILDCOPYLENGTH)) { LZ5_LOG_DECOMPRESS_LZ5v2("11"); goto _output_error; }  /* doesn't respect parsing restriction */

            if (length <= (intptr_t)(lowPrefix - match)) {
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
        if (unlikely(cpy > oend - WILDCOPYLENGTH)) { LZ5_LOG_DECOMPRESS_LZ5v2("1match=%p lowLimit=%p\n", match, lowLimit); goto _output_error; }   /* Error : offset outside buffers */
        LZ5_copy8(op, match);
        LZ5_copy8(op+8, match+8);
        if (length > 16)
            LZ5_wildCopy16(op + 16, match + 16, cpy);
        op = cpy;
    }

    /* end of decoding */
    if (endOnInput)
       return (int) (((char*)op)-dest);     /* Nb of output bytes decoded */
    else
       return (int) (((const char*)ip)-source);   /* Nb of input bytes read */

    /* Overflow error detected */
_output_error:
    LZ5_LOG_DECOMPRESS_LZ5v2("_output_error=%d\n", (int) (-(((const char*)ip)-source))-1);
    LZ5_LOG_DECOMPRESS_LZ5v2("cpy=%p oend=%p ip+length[%d]=%p iend=%p\n", cpy, oend, (int)length, ip+length, iend);
    return (int) (-(((const char*)ip)-source))-1;
}
