#define LZ5_MAX_16BIT_OFFSET (1<<16)

FORCE_INLINE int LZ5_encodeSequence_LZ5v2 (
    LZ5_stream_t* ctx,
    const BYTE** ip,
    BYTE** op,
    const BYTE** anchor,
    size_t matchLength,
    const BYTE* const match,
    limitedOutput_directive limitedOutputBuffer,
    BYTE* oend)
{
    BYTE* token = (*op)++;
    U32 offset = (U32)(*ip - match);
    size_t length = (int)(*ip - *anchor);


    if (length > 0 || offset < LZ5_MAX_16BIT_OFFSET)
    {
        /* Encode Literal length */
        if ((limitedOutputBuffer) && ((*op + length + (2 + 1 + LASTLITERALS)) > oend)) return 1;   /* Check output limit */
        if (length >= RUN_MASK_LZ5v2) 
        {   size_t len; 
            *token = RUN_MASK_LZ5v2; 
            len = length - RUN_MASK_LZ5v2; 
            if (len >= 255) { *(*op) = 255;  MEM_writeLE16(*op+1, (U16)(len - 255));  *op += 3; }
            else *(*op)++ = (BYTE)len;
        }
        else *token = (BYTE)length;

        /* Copy Literals */
        LZ5_wildCopy(*op, *anchor, (*op) + length);
        *op += length;
        
        if (offset >= LZ5_MAX_16BIT_OFFSET)
        {
            *token+=(1<<ML_RUN_BITS);
            token = (*op)++;
        }
    }

    /* Encode Offset */
    if (offset >= LZ5_MAX_16BIT_OFFSET)  // 24-bit offset
    {
        if (matchLength < MM_LONGOFF) printf("ERROR matchLength=%d\n", (int)matchLength);
    
        if (matchLength - MM_LONGOFF >= 31) 
        {
          //  printf("T2 enc 24-bit length=%d matchLength=%d offset=%u\n", length, matchLength, offset);
            size_t len = matchLength - MM_LONGOFF - 31;
            *token = 31;
            if (len >= (1<<16) + 255) printf("ERROR2 matchLength=%d\n", (int)len), exit(0);
            if (len >= 255) { *(*op) = 255;  MEM_writeLE16(*op+1, (U16)(len - 255));  *op += 3; }
            else *(*op)++ = (BYTE)len; 
        }
        else
        {
            *token = (BYTE)(matchLength - MM_LONGOFF);
        }

        MEM_writeLE24(*op, offset); 
        *op += 3;
        ctx->last_off = offset;
    }
    else
    {
        if (offset == 0)
        {
            *token+=(1<<ML_RUN_BITS);
        }
        else
        {
            if (offset < 8) printf("ERROR offset=%d\n", (int)offset);

            ctx->last_off = offset;
            if (matchLength < 3) { printf("matchLength[%d] < 3  offset=%d\n", (int)matchLength, (int)ctx->last_off); exit(1); }
            MEM_writeLE16(*op, (U16)ctx->last_off); *op += 2;
        }

     //   if (matchLength == 0) return 0;  /* end of file */


        /* Encode MatchLength */
        length = matchLength;
        if ((limitedOutputBuffer) && (*op + (3 + LASTLITERALS) > oend)) return 1;   /* Check output limit */
        if (length >= ML_MASK_LZ5v2) 
        {   size_t len; 
            *token += ML_MASK_LZ5v2<<RUN_BITS_LZ5v2;
            len = length - ML_MASK_LZ5v2; 
            if (len >= 255) { *(*op) = 255;  MEM_writeLE16(*op+1, (U16)(len - 255));  *op += 3; }
            else *(*op)++ = (BYTE)len;
        }
        else *token += (BYTE)(length<<RUN_BITS_LZ5v2);
    }
    
//    printf("%u: ENCODE literals=%u off=%u mlen=%u\n", (U32)(*ip - ctx->inputBuffer), (U32)(*ip - *anchor), (U32)(*ip-match), (U32)matchLength);

    /* Prepare next loop */
    *ip += matchLength;
    *anchor = *ip;

    return 0;
}


FORCE_INLINE int LZ5_encodeLastLiterals_LZ5v2 (
    LZ5_stream_t* ctx,
    const BYTE** ip,
    BYTE** op,
    const BYTE** anchor,
    limitedOutput_directive limitedOutputBuffer,
    BYTE* oend)
{
    return LZ5_encodeSequence_LZ5v2(ctx, ip, op, anchor, 0, *ip, limitedOutputBuffer, oend);
}



