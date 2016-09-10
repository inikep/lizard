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
    BYTE* oexit = op + targetOutputSize;
    const BYTE* const lowLimit = lowPrefix - dictSize;
    const BYTE* const dictEnd = (const BYTE*)dictStart + dictSize;

    LZ5_parameters params = LZ5_defaultParameters[compressionLevel];
    intptr_t last_off = -1;

    unsigned temp = *ip++; // skip compression level
    (void)temp;
    (void)params;
    (void)dictEnd;
    (void)dict;

    /* Special cases */
    if ((partialDecoding) && (oexit > oend-MFLIMIT)) oexit = oend-MFLIMIT;                        /* targetOutputSize too high => decode everything */
    if ((endOnInput) && (unlikely(outputSize==0))) return ((inputSize==1) && (*ip==0)) ? 0 : -1;  /* Empty output buffer */
    if ((!endOnInput) && (unlikely(outputSize==0))) return (*ip==0?1:-1);

    /* Main Loop : decode sequences */
    while (1) {
        unsigned token;
        size_t length;
        const BYTE* match;

        /* get literal length */
        token = *ip++;

        if (token >= 32)
        {
            length = token & RUN_MASK_LZ5v2;
            if (length == RUN_MASK_LZ5v2)
            {
                if (unlikely(ip + LASTLITERALS > iend)) goto _output_error;
                length = *ip;
                if (length == 255) {
                    length += MEM_readLE16(ip+1);
                    ip += 2;
                }
                ip++;
                length += RUN_MASK_LZ5v2;
            }

            if (unlikely(ip + length + LASTLITERALS > iend || op + length + WILDCOPYLENGTH > oend)) { 
                if (op + length == oend) {
                    memcpy(op, ip, length);
                    op += length;
                    break;
                }
                printf("op=%p+length=%d %p oend=%p\n", op, (int)length, op + length + WILDCOPYLENGTH, oend); 
                printf("ip=%p+length=%d %p iend=%p token=%d\n", ip, (int)length, ip + length, iend, token); 
                goto _output_error; 
            } 
            do {
                LZ5_copy8(op, ip);
                LZ5_copy8(op+8, ip+8);
                op += 16;
                ip += 16;
                length -= 16;
            } while (length > 0);
            op += length; // negative length
            ip += length; 


#if 1
            { /* branchless */
                intptr_t new_dist = MEM_readLE16(ip);
                uintptr_t use_distance = (uintptr_t)(token >> ML_RUN_BITS) - 1;
                last_off ^= use_distance & (last_off ^ -new_dist);
                ip = (uint8_t*)((uintptr_t)ip + (use_distance & 2));
            }
#else
            if ((token >> ML_RUN_BITS_LZ5v2) == 0)
            {
                last_off = -(intptr_t)MEM_readLE16(ip); 
                ip += 2;
            //    printf("MEM_readLE16 offset=%d\n", (int)offset);
            }
#endif

        //    printf("DECODE literals=%u off=%u mlen=%u\n", length, offset, (token>>RUN_BITS_LZ5v2)&ML_MASK);

            /* get matchlength */
            length = (token>>RUN_BITS_LZ5v2)&ML_MASK_LZ5v2;
            if (length == ML_MASK_LZ5v2)
            {
                if (unlikely(ip + LASTLITERALS > iend)) goto _output_error;
                length = *ip;
                if (length == 255) {
                    length += MEM_readLE16(ip+1);
                    ip += 2;
                }
                ip++;
                length += ML_MASK_LZ5v2;
              //  length += MIN_OFF16_MATCHLEN;
            }
        }
        else
        if (token < 31)
        {
            if (unlikely(ip + LASTLITERALS > iend)) goto _output_error;
            length = token + MM_LONGOFF;
            last_off = -(intptr_t)MEM_readLE24(ip); 
            ip += 3;
         //   printf("T3 dec 24-bit matchLength=%d offset=%u\n", length, offset);
        }
        else // token == 0
        { 
            if (unlikely(ip + LASTLITERALS > iend)) goto _output_error;
            length = *ip;
            if (length == 255) {
                length += MEM_readLE16(ip+1);
                ip += 2;
            }
            ip++;
            length += 31 + MM_LONGOFF;

//                printf("T2 dec 24-bit matchLength=%d offset=%u\n", length, offset);
            last_off = -(intptr_t)MEM_readLE24(ip); 
            ip += 3;
        }


        match = op + last_off;

        if (unlikely(match < lowLimit || op + length + WILDCOPYLENGTH > oend)) { printf("1"); goto _output_error; }   /* Error : offset outside buffers */
        do {
            LZ5_copy8(op, match);
            LZ5_copy8(op+8, match+8);
            op += 16;
            match += 16;
            length -= 16;
        } while (length > 0);
        op += length; // negative length
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
