/*! LZ5_decompress_LZ4() :
 *  This generic decompression function cover all use cases.
 *  It shall be instantiated several times, using different sets of directives
 *  Note that it is important this generic function is really inlined,
 *  in order to remove useless branches during compilation optimization.
 */
FORCE_INLINE int LZ5_decompress_LZ4(
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
    BYTE* oexit = op + targetOutputSize;
    const BYTE* const lowLimit = lowPrefix - dictSize;
    const BYTE* const dictEnd = (const BYTE*)dictStart + dictSize;

    const int safeDecode = (endOnInput==endOnInputSize);
    const int checkOffset = ((safeDecode) && (dictSize < (int)(LZ5_DICT_SIZE)));

    intptr_t length;
    ip++; // skip compression level
    (void)compressionLevel;
    (void)LZ5_wildCopy;

    /* Special cases */
    if (partialDecoding) { if (oexit > oend-MFLIMIT) oexit = oend-MFLIMIT; }  /* targetOutputSize too high => decode everything */
    else oexit = oend - WILDCOPYLENGTH;
    if ((endOnInput) && (unlikely(outputSize==0))) return ((inputSize==2) && (*ip==0)) ? 0 : -1;  /* Empty output buffer */
    if ((!endOnInput) && (unlikely(outputSize==0))) return (*ip==0?1:-1);

    /* Main Loop : decode sequences */
    while (1) {
        unsigned token;
        const BYTE* match;
        size_t offset;

        /* get literal length */
        token = *ip++;
        if ((length=(token & RUN_MASK_LZ4)) == RUN_MASK_LZ4) {
            if ((endOnInput) && unlikely(ip > iend - 5) ) { LZ5_LOG_DECOMPRESS_LZ4("0"); goto _output_error; } 
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
            length += RUN_MASK_LZ4;
            ip++;
            if ((safeDecode) && unlikely((size_t)(op+length)<(size_t)(op))) { LZ5_LOG_DECOMPRESS_LZ4("1"); goto _output_error; }  /* overflow detection */
            if ((safeDecode) && unlikely((size_t)(ip+length)<(size_t)(ip))) { LZ5_LOG_DECOMPRESS_LZ4("2"); goto _output_error; }   /* overflow detection */
        }

        /* copy literals */
        cpy = op + length;
#if 0
        if ( ((endOnInput) && ((cpy>oexit) || (ip + length > iend - (2 + WILDCOPYLENGTH))) )
            || ((!endOnInput) && (cpy>oexit)) )
#else
        if ((cpy>oexit) || ((endOnInput) && (ip + length > iend - (2 + WILDCOPYLENGTH))))
#endif
        {
            if (partialDecoding) {
                if (cpy > oend) { LZ5_LOG_DECOMPRESS_LZ4("3"); goto _output_error; }                           /* Error : write attempt beyond end of output buffer */
                if ((endOnInput) && (ip+length > iend)) { LZ5_LOG_DECOMPRESS_LZ4("4"); goto _output_error; }   /* Error : read attempt beyond end of input buffer */
            } else {
                if ((!endOnInput) && (cpy != oend)) { LZ5_LOG_DECOMPRESS_LZ4("5"); goto _output_error; }       /* Error : block decoding must stop exactly there */
                if ((endOnInput) && ((ip+length != iend) || (cpy > oend))) { LZ5_LOG_DECOMPRESS_LZ4("6"); goto _output_error; }   /* Error : input must be consumed */
            }
            memcpy(op, ip, length);
            ip += length;
            op += length;
            break;     /* Necessarily EOF, due to parsing restrictions */
        }

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
        offset = MEM_readLE16(ip); 
        ip += 2;

        match = op - offset;
        if ((checkOffset) && (unlikely(match < lowLimit))) { LZ5_LOG_DECOMPRESS_LZ4("lowPrefix[%p]-dictSize[%d]=lowLimit[%p] match[%p]=op[%p]-offset[%d]\n", lowPrefix, (int)dictSize, lowLimit, match, op, (int)offset); goto _output_error; }  /* Error : offset outside buffers */

        /* get matchlength */
        length = token >> RUN_BITS_LZ4;
        if (length == ML_MASK_LZ4) {
            if ((endOnInput) && unlikely(ip > iend - 5) ) { LZ5_LOG_DECOMPRESS_LZ4("0"); goto _output_error; } 
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
            length += ML_MASK_LZ4;
            ip++;
            if ((safeDecode) && unlikely((size_t)(op+length)<(size_t)(op))) { LZ5_LOG_DECOMPRESS_LZ4("9"); goto _output_error; }  /* overflow detection */
        }
        length += MINMATCH;

        /* check external dictionary */
        if ((dict==usingExtDict) && (match < lowPrefix)) {
            if (unlikely(op + length > oend - WILDCOPYLENGTH)) { LZ5_LOG_DECOMPRESS_LZ4("10"); goto _output_error; }  /* doesn't respect parsing restriction */

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
        if (unlikely(cpy > oend - WILDCOPYLENGTH)) { LZ5_LOG_DECOMPRESS_LZ4("1match=%p lowLimit=%p\n", match, lowLimit); goto _output_error; }   /* Error : offset outside buffers */
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
    LZ5_LOG_DECOMPRESS_LZ4("_output_error=%d\n", (int) (-(((const char*)ip)-source))-1);
    LZ5_LOG_DECOMPRESS_LZ4("cpy=%p oend=%p ip+length[%d]=%p iend=%p\n", cpy, oend, (int)length, ip+length, iend);
    return (int) (-(((const char*)ip)-source))-1;
}
