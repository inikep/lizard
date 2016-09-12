#define LZ4_DEBUG(...) // printf(__VA_ARGS__)

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
#if 0
    const unsigned dec32table[] = {4, 1, 2, 1, 4, 4, 4, 4};
    const int dec64table[] = {0, 0, 0, -1, 0, 1, 2, 3};
#endif

    const int safeDecode = (endOnInput==endOnInputSize);
    const int checkOffset = ((safeDecode) && (dictSize < (int)(LZ5_DICT_SIZE)));

    intptr_t length;
    unsigned temp = *ip++; // skip compression level
    (void)temp;
    (void)compressionLevel;
    (void)LZ5_wildCopy;

    /* Special cases */
    if ((partialDecoding) && (oexit > oend-MFLIMIT)) oexit = oend-MFLIMIT;                        /* targetOutputSize too high => decode everything */
    if ((endOnInput) && (unlikely(outputSize==0))) return ((inputSize==1) && (*ip==0)) ? 0 : -1;  /* Empty output buffer */
    if ((!endOnInput) && (unlikely(outputSize==0))) return (*ip==0?1:-1);

    /* Main Loop : decode sequences */
    while (1) {
        unsigned token;
        const BYTE* match;
        size_t offset;

        /* get literal length */
        token = *ip++;
        if ((length=(token>>ML_BITS_LZ4)) == RUN_MASK_LZ4) {
            unsigned s;
            do {
                s = *ip++;
                length += s;
            } while ( likely(endOnInput ? ip<iend-RUN_MASK_LZ4 : 1) & (s==255) );
            if ((safeDecode) && unlikely((size_t)(op+length)<(size_t)(op))) { LZ4_DEBUG("1"); goto _output_error; }  /* overflow detection */
            if ((safeDecode) && unlikely((size_t)(ip+length)<(size_t)(ip))) { LZ4_DEBUG("2"); goto _output_error; }   /* overflow detection */
        }

        /* copy literals */
        cpy = op + length;
        if ( ((endOnInput) && ((cpy>(partialDecoding?oexit:oend-WILDCOPYLENGTH)) || (ip+length>iend-(2+1+LASTLITERALS))) )
            || ((!endOnInput) && (cpy>oend-WILDCOPYLENGTH)) )
        {
            if (partialDecoding) {
                if (cpy > oend) { LZ4_DEBUG("3"); goto _output_error; }                           /* Error : write attempt beyond end of output buffer */
                if ((endOnInput) && (ip+length > iend)) { LZ4_DEBUG("4"); goto _output_error; }   /* Error : read attempt beyond end of input buffer */
            } else {
                if ((!endOnInput) && (cpy != oend)) { LZ4_DEBUG("5"); goto _output_error; }       /* Error : block decoding must stop exactly there */
                if ((endOnInput) && ((ip+length != iend) || (cpy > oend))) { LZ4_DEBUG("6"); goto _output_error; }   /* Error : input must be consumed */
            }
            memcpy(op, ip, length);
            ip += length;
            op += length;
            break;     /* Necessarily EOF, due to parsing restrictions */
        }

#if 0
        LZ5_copy8(op, ip);
        if (length > 8)
            LZ5_wildCopy16(op + 8, ip + 8, cpy);
        ip += length; 
        op = cpy;
#else
    //    if (unlikely(ip > iend)) {  printf("6b ip=%p length=%d iend=%p\n", ip, (int)length, iend); goto _output_error; }   /* Error : offset outside buffers */
  //      if (unlikely(ip + length + WILDCOPYLENGTH > iend || op + length + WILDCOPYLENGTH > oend)) { printf("6c ip=%p length=%d %p/%p %p/%p\n", ip, (int)length, ip + length + WILDCOPYLENGTH, iend, op + length + WILDCOPYLENGTH, oend); goto _output_error; }   /* Error : offset outside buffers */
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
        if ((checkOffset) && (unlikely(match < lowLimit))) { LZ4_DEBUG("lowPrefix[%p]-dictSize[%d]=lowLimit[%p] match[%p]=op[%p]-offset[%d]\n", lowPrefix, (int)dictSize, lowLimit, match, op, (int)offset); goto _output_error; }  /* Error : offset outside buffers */

        /* get matchlength */
        length = token & ML_MASK_LZ4;
        if (length == ML_MASK_LZ4) {
            unsigned s;
            do {
                s = *ip++;
                if ((endOnInput) && (ip > iend-LASTLITERALS)) { LZ4_DEBUG("8"); goto _output_error; }
                length += s;
            } while (s==255);
            if ((safeDecode) && unlikely((size_t)(op+length)<(size_t)op)) { LZ4_DEBUG("9"); goto _output_error; }   /* overflow detection */
        }
        length += MINMATCH;

        /* check external dictionary */
        if ((dict==usingExtDict) && (match < lowPrefix)) {
            if (unlikely(op + length + LASTLITERALS > oend)) { LZ4_DEBUG("10"); goto _output_error; }  /* doesn't respect parsing restriction */

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
#if 0
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
            if (cpy > oend-LASTLITERALS) { LZ4_DEBUG("11"); goto _output_error; }   /* Error : last LASTLITERALS bytes must be literals (uncompressed) */
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
#else
        if (unlikely(match < lowLimit || op + length + WILDCOPYLENGTH > oend)) { LZ4_DEBUG("1match=%p lowLimit=%p\n", match, lowLimit); goto _output_error; }   /* Error : offset outside buffers */
        cpy = op + length;
        LZ5_copy8(op, match);
        LZ5_copy8(op+8, match+8);
        if (length > 16)
            LZ5_wildCopy16(op + 16, match + 16, cpy);
        op = cpy;
#endif
    }

    /* end of decoding */
    if (endOnInput)
       return (int) (((char*)op)-dest);     /* Nb of output bytes decoded */
    else
       return (int) (((const char*)ip)-source);   /* Nb of input bytes read */

    /* Overflow error detected */
_output_error:
    LZ4_DEBUG("_output_error=%d\n", (int) (-(((const char*)ip)-source))-1);
    LZ4_DEBUG("cpy=%p oend=%p ip+length[%d]=%p iend=%p\n", cpy, oend, (int)length, ip+length, iend);
    return (int) (-(((const char*)ip)-source))-1;
}
