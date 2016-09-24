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
    U32 offset = (U32)(*ip - match);
    size_t length = (size_t)(*ip - *anchor);
    BYTE* token = (*op)++;
#ifdef USE_8BIT_CODEWORDS
    int off8bit = (offset >= 8 && offset < LZ5_MAX_8BIT_OFFSET);
#else
    int off8bit = 0;
#endif
    (void) ctx;

    if (length > 0 || offset < LZ5_MAX_16BIT_OFFSET) {
        /* Encode Literal length */
        if ((limitedOutputBuffer) && (*op > oend - length - LZ5_LENGTH_SIZE_LZ5v2(length) - WILDCOPYLENGTH)) { LZ5_LOG_COMPRESS_LZ5v2("encodeSequence overflow1\n"); return 1; }   /* Check output limit */
        if (length >= RUN_MASK_LZ5v2) 
        {   size_t len; 
            *token = RUN_MASK_LZ5v2; 
            len = length - RUN_MASK_LZ5v2;
            if (len >= (1<<16)) { *(*op) = 255;  MEM_writeLE32(*op+1, (U32)(len));  *op += 5; }
            else if (len >= 254) { *(*op) = 254;  MEM_writeLE16(*op+1, (U16)(len));  *op += 3; }
            else *(*op)++ = (BYTE)len;
        }
        else *token = (BYTE)length;

        /* Copy Literals */
        LZ5_wildCopy(*op, *anchor, (*op) + length);
        *op += length;

        if (off8bit || offset >= LZ5_MAX_16BIT_OFFSET) {
            COMPLOG_CODEWORDS_LZ5v2("T32+ literal=%u match=%u offset=%d\n", (U32)length, 0, 0);
            *token+=(1<<ML_RUN_BITS);
            token = (*op)++;
        }
    }

    /* Encode Offset */
#ifdef USE_8BIT_CODEWORDS
    if (off8bit) 
    {
        if (matchLength < MINMATCH) { printf("matchLength[%d] < MINMATCH  offset=%d\n", (int)matchLength, (int)ctx->last_off); exit(1); }

        if ((limitedOutputBuffer) && (*op > oend - 6 /*LZ5_LENGTH_SIZE_LZ5v2(length)*/)) { LZ5_LOG_COMPRESS_LZ5v2("encodeSequence overflow2\n"); return 1; }   /* Check output limit */
        if (matchLength - MINMATCH >= SHORT_OFF_COUNT) 
        {
            size_t len = matchLength - MINMATCH - SHORT_OFF_COUNT;
            *token = LAST_SHORT_OFF;
            if (len >= (1<<16)) { *(*op) = 255;  MEM_writeLE32(*op+1, (U32)(len));  *op += 5; }
            else if (len >= 254) { *(*op) = 254;  MEM_writeLE16(*op+1, (U16)(len));  *op += 3; }
            else *(*op)++ = (BYTE)len; 
            COMPLOG_CODEWORDS_LZ5v2("8BIT16+ literal=%u match=%u offset=%d\n", 0, (U32)matchLength, offset);
        }
        else
        {
            COMPLOG_CODEWORDS_LZ5v2("8BIT0-15 literal=%u match=%u offset=%d\n", 0, (U32)matchLength, offset);
            *token = (BYTE)(FIRST_SHORT_OFF + matchLength - MINMATCH);
        }

        *(*op)++ = (BYTE)offset;
        ctx->last_off = offset;
    }
    else
#endif
    if (offset >= LZ5_MAX_16BIT_OFFSET)  // 24-bit offset
    {
        if (matchLength < MM_LONGOFF) printf("ERROR matchLength=%d/%d\n", (int)matchLength, MM_LONGOFF), exit(0);

        if ((limitedOutputBuffer) && (*op > oend - 8 /*LZ5_LENGTH_SIZE_LZ5v2(length)*/)) { LZ5_LOG_COMPRESS_LZ5v2("encodeSequence overflow2\n"); return 1; }   /* Check output limit */
        if (matchLength - MM_LONGOFF >= LAST_LONG_OFF) 
        {
            size_t len = matchLength - MM_LONGOFF - LAST_LONG_OFF;
            *token = LAST_LONG_OFF;
            if (len >= (1<<16)) { *(*op) = 255;  MEM_writeLE32(*op+1, (U32)(len));  *op += 5; }
            else if (len >= 254) { *(*op) = 254;  MEM_writeLE16(*op+1, (U16)(len));  *op += 3; }
            else *(*op)++ = (BYTE)len; 
            COMPLOG_CODEWORDS_LZ5v2("T31 literal=%u match=%u offset=%d\n", 0, (U32)matchLength, offset);
        }
        else
        {
            COMPLOG_CODEWORDS_LZ5v2("T0-30 literal=%u match=%u offset=%d\n", 0, (U32)matchLength, offset);
            *token = (BYTE)(matchLength - MM_LONGOFF);
        }

        MEM_writeLE24(*op, offset); 
        *op += 3;
        ctx->last_off = offset;
    }
    else
    {
        COMPLOG_CODEWORDS_LZ5v2("T32+ literal=%u match=%u offset=%d\n", (U32)length, (U32)matchLength, offset);
        if (offset == 0)
        {
            *token+=(1<<ML_RUN_BITS);
        }
        else
        {
            if (offset < 8) printf("ERROR offset=%d\n", (int)offset);
            if (matchLength < MINMATCH) { printf("matchLength[%d] < MINMATCH  offset=%d\n", (int)matchLength, (int)ctx->last_off); exit(1); }
            
            ctx->last_off = offset;
            MEM_writeLE16(*op, (U16)ctx->last_off); *op += 2;
        }

     //   if (matchLength == 0) return 0;  /* end of file */

        /* Encode MatchLength */
        length = matchLength;
        if ((limitedOutputBuffer) && (*op > oend - 5 /*LZ5_LENGTH_SIZE_LZ5v2(length)*/)) { LZ5_LOG_COMPRESS_LZ5v2("encodeSequence overflow2\n"); return 1; }   /* Check output limit */
        if (length >= ML_MASK_LZ5v2) {
            *token += (BYTE)(ML_MASK_LZ5v2<<RUN_BITS_LZ5v2);
            length -= ML_MASK_LZ5v2;
            if (length >= (1<<16)) { *(*op) = 255;  MEM_writeLE32(*op+1, (U32)(length));  *op += 5; }
            else if (length >= 254) { *(*op) = 254;  MEM_writeLE16(*op+1, (U16)(length));  *op += 3; }
            else *(*op)++ = (BYTE)length;
        }
        else *token += (BYTE)(length<<RUN_BITS_LZ5v2);
    }


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
    size_t length = (int)(*ip - *anchor);
    BYTE* token = (*op)++;

    (void)ctx;

    LZ5_LOG_COMPRESS_LZ5v2("LZ5_encodeLastLiterals_LZ5v2 length=%d LZ5_LENGTH_SIZE_LZ5v2=%d oend-op=%d\n", (int)length, (int)(LZ5_LENGTH_SIZE_LZ5v2(length)), (int)(oend-*op));
    if ((limitedOutputBuffer) && (*op > oend - length - LZ5_LENGTH_SIZE_LZ5v2(length))) { LZ5_LOG_COMPRESS_LZ5v2("LastLiterals overflow\n"); return 1; } /* Check output buffer overflow */
    if (length >= RUN_MASK_LZ5v2) 
    {   size_t len; 
        *token = RUN_MASK_LZ5v2; 
        len = length - RUN_MASK_LZ5v2;
        if (len >= (1<<16)) { *(*op) = 255;  MEM_writeLE32(*op+1, (U32)(len));  *op += 5; }
        else if (len >= 254) { *(*op) = 254;  MEM_writeLE16(*op+1, (U16)(len));  *op += 3; }
        else *(*op)++ = (BYTE)len;
    }
    else *token = (BYTE)length;
    *token+=(1<<ML_RUN_BITS); // useRep code with len=0

    memcpy(*op, *anchor, length);
    *op += length;
    return 0;
}


FORCE_INLINE size_t LZ5_get_price_LZ5v2(LZ5_stream_t* const ctx, size_t litLength, U32 offset, size_t matchLength)
{
#ifdef USE_8BIT_CODEWORDS
    int off8bit = (offset >= 8 && offset < LZ5_MAX_8BIT_OFFSET);
#else
    int off8bit = 0;
#endif
    size_t price = 8; // ctx->flagsPtr++;
    (void)ctx;

    if (litLength > 0 || offset < LZ5_MAX_16BIT_OFFSET) {
        /* Encode Literal length */
        if (litLength>=(int)RUN_MASK_LZ5v2) {  
            size_t len = litLength - RUN_MASK_LZ5v2; 
            if (len >= (1<<16)) price += 40;
            else if (len >= 254) price += 24;
            else price += 8;
        }
    
        price += 8*litLength;  /* Copy Literals */
        if (off8bit || offset >= LZ5_MAX_16BIT_OFFSET)
            price += 8;
    }

    /* Encode Offset */
#ifdef USE_8BIT_CODEWORDS
    if (off8bit) 
    {
        if (matchLength < MINMATCH) return LZ5_MAX_PRICE; // error
        if (matchLength - MINMATCH >= SHORT_OFF_COUNT) 
        {
            size_t len = matchLength - MINMATCH - SHORT_OFF_COUNT;
            if (len >= (1<<16)) price += 40;
            else if (len >= 254) price += 24;
            else price += 8;
        }
        price += 8; // *(*op)++ = (BYTE)offset;
    }
    else
#endif
    if (offset >= LZ5_MAX_16BIT_OFFSET) { // 24-bit offset
        if (matchLength < 16) return LZ5_MAX_PRICE; // error
        if (matchLength - MM_LONGOFF >= LAST_LONG_OFF) {
            size_t len = matchLength - MM_LONGOFF - LAST_LONG_OFF;
            if (len >= (1<<16)) price += 40;
            else if (len >= 254) price += 24;
            else price += 8;
        }
        price += 24; // *op += 3;
        LZ5_24BIT_OFFSET_LOAD;
    } else {
        if (offset) {
            if (matchLength < MINMATCH) return LZ5_MAX_PRICE; // error
            price += 16; // ctx->offset16Ptr += 2;
        }

        /* Encode MatchLength */
        if (matchLength>=(int)ML_MASK_LZ5v2) 
        {   size_t len = matchLength - ML_MASK_LZ5v2; 
            if (len >= (1<<16)) price += 40;
            else if (len >= 254) price += 24;
            else price += 8;
        }
    }
    return price;
}

