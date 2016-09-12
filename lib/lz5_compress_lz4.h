#define LZ5_DEBUG 0
#if LZ5_DEBUG
static unsigned debug = 0;
#endif

FORCE_INLINE int LZ5_encodeSequence_LZ4 (
    LZ5_stream_t* ctx,
    const BYTE** ip,
    BYTE** op,
    const BYTE** anchor,
    size_t matchLength,
    const BYTE* const match,
    limitedOutput_directive limitedOutputBuffer,
    BYTE* oend)
{
    size_t length = (size_t)(*ip - *anchor);
    BYTE* token = (*op)++;
    (void) ctx;

#if LZ5_DEBUG
    if (debug) printf("literal : %u  --  match : %u  --  offset : %u\n", (U32)(*ip - *anchor), (U32)matchLength, (U32)(*ip-match));
#endif
  
    /* Encode Literal length */
    if ((limitedOutputBuffer) && ((*op + (length>>8) + length + (2 + 1 + LASTLITERALS)) > oend)) return 1;   /* Check output limit */
#ifdef OLD_LENGTH_ENCODING
    if (length >= RUN_MASK_LZ4) 
    {   size_t len; 
        *token=(RUN_MASK_LZ4); 
        len = length-RUN_MASK_LZ4; 
        for(; len > 254 ; len-=255) *(*op)++ = 255;
        *(*op)++ = (BYTE)len; 
    }
    else *token = (BYTE)(length);
#else
    if (length >= RUN_MASK_LZ4) 
    {   size_t len; 
        *token = RUN_MASK_LZ4; 
        len = length - RUN_MASK_LZ4;
        if (len >= (1<<16)) { *(*op) = 255;  MEM_writeLE32(*op+1, (U32)(len));  *op += 5; }
        else if (len >= 254) { *(*op) = 254;  MEM_writeLE16(*op+1, (U16)(len));  *op += 3; }
        else *(*op)++ = (BYTE)len;
    }
    else *token = (BYTE)length;
#endif
    /* Copy Literals */
    LZ5_wildCopy(*op, *anchor, (*op) + length);
    *op += length;

    /* Encode Offset */
//    if (match > *ip) printf("match > *ip\n"), exit(1);
//    if ((U32)(*ip-match) >= (1<<16)) printf("off=%d\n", (U32)(*ip-match)), exit(1);
    MEM_writeLE16(*op, (U16)(*ip-match));
    *op+=2;

    /* Encode MatchLength */
    length = matchLength - MINMATCH;
    if ((limitedOutputBuffer) && (*op + (length>>8) + (1 + LASTLITERALS) > oend)) return 1;   /* Check output limit */
#if 1//def OLD_LENGTH_ENCODING
    if (length >= ML_MASK_LZ4) {
        *token += (BYTE)(ML_MASK_LZ4<<RUN_BITS_LZ4);
        length -= ML_MASK_LZ4;
        while (length > 254) { length-=255; *(*op)++ = 255; }
        *(*op)++ = (BYTE)length;
    } else {
        *token += (BYTE)(length<<RUN_BITS_LZ4);
    }
#else
    if (length >= ML_MASK_LZ4) {
        *token += (BYTE)(ML_MASK_LZ4<<RUN_BITS_LZ4);
        length -= ML_MASK_LZ4;
        if (length >= (1<<16)) { *(*op) = 255;  MEM_writeLE32(*op+1, (U32)(length));  *op += 5; }
        else if (length >= 254) { *(*op) = 254;  MEM_writeLE16(*op+1, (U16)(length));  *op += 3; }
        else *(*op)++ = (BYTE)length;
    }
    else *token += (BYTE)(length<<RUN_BITS_LZ4);
#endif
    /* Prepare next loop */
    *ip += matchLength;
    *anchor = *ip;

    return 0;
}


FORCE_INLINE int LZ5_encodeLastLiterals_LZ4 (
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

//    printf("LZ5_encodeLastLiterals_LZ4 length=%d (length>>8)=%d (length+255-RUN_MASK_LZ4)/255=%d oend-op=%d\n", (int)length, (int)(length>>8), (int)(length+255-RUN_MASK_LZ4)/255, (int)(oend-*op));
    if ((limitedOutputBuffer) && (*op + length + ((length+255-RUN_MASK_LZ4)/255) > oend)) return 1; /* Check output buffer overflow */
#ifdef OLD_LENGTH_ENCODING
    if (length >= RUN_MASK_LZ4) 
    {   size_t len; 
        *token=(RUN_MASK_LZ4); 
        len = length-RUN_MASK_LZ4; 
        for(; len > 254 ; len-=255) *(*op)++ = 255;
        *(*op)++ = (BYTE)len; 
    }
    else *token = (BYTE)(length);
#else
    if (length >= RUN_MASK_LZ4) 
    {   size_t len; 
        *token = RUN_MASK_LZ4; 
        len = length - RUN_MASK_LZ4;
        if (len >= (1<<16)) { *(*op) = 255;  MEM_writeLE32(*op+1, (U32)(len));  *op += 5; }
        else if (len >= 254) { *(*op) = 254;  MEM_writeLE16(*op+1, (U16)(len));  *op += 3; }
        else *(*op)++ = (BYTE)len;
    }
    else *token = (BYTE)length;
#endif
    memcpy(*op, *anchor, length);
    *op += length;
    return 0;
}
