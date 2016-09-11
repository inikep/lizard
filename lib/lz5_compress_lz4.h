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
    int length = (int)(*ip - *anchor);
    BYTE* token = (*op)++;
    (void) ctx;

#if LZ5_DEBUG
    if (debug) printf("literal : %u  --  match : %u  --  offset : %u\n", (U32)(*ip - *anchor), (U32)matchLength, (U32)(*ip-match));
#endif
  
    /* Encode Literal length */
    if ((limitedOutputBuffer) && ((*op + (length>>8) + length + (2 + 1 + LASTLITERALS)) > oend)) return 1;   /* Check output limit */
    if (length>=(int)RUN_MASK_LZ4) { int len; *token=(RUN_MASK_LZ4<<ML_BITS_LZ4); len = length-RUN_MASK_LZ4; for(; len > 254 ; len-=255) *(*op)++ = 255;  *(*op)++ = (BYTE)len; }
    else *token = (BYTE)(length<<ML_BITS_LZ4);

    /* Copy Literals */
    LZ5_wildCopy(*op, *anchor, (*op) + length);
    *op += length;

    /* Encode Offset */
//    if (match > *ip) printf("match > *ip\n"), exit(1);
//    if ((U32)(*ip-match) >= (1<<16)) printf("off=%d\n", (U32)(*ip-match)), exit(1);
    MEM_writeLE16(*op, (U16)(*ip-match));
    *op+=2;

    /* Encode MatchLength */
    length = (int)(matchLength-MINMATCH);
    if ((limitedOutputBuffer) && (*op + (length>>8) + (1 + LASTLITERALS) > oend)) return 1;   /* Check output limit */
    if (length>=(int)ML_MASK_LZ4) {
        *token += ML_MASK_LZ4;
        length -= ML_MASK_LZ4;
#if 1
        MEM_write32(*op, 0xFFFFFFFF);
        while (length >= 4*255) *op+=4, MEM_write32(*op, 0xFFFFFFFF), length -= 4*255;
        *op += length / 255;
        *(*op)++ = (BYTE)(length % 255);
#else        
        for(; length > 509 ; length-=510) { *(*op)++ = 255; *(*op)++ = 255; }
        if (length > 254) { length-=255; *(*op)++ = 255; }
        *(*op)++ = (BYTE)length;
#endif
    } else {
        *token += (BYTE)(length);
    }

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
    int length = (int)(*ip - *anchor);
    BYTE* token = (*op)++;

    (void)ctx;

//    printf("LZ5_encodeLastLiterals_LZ4 length=%d (length>>8)=%d (length+255-RUN_MASK_LZ4)/255=%d oend-op=%d\n", (int)length, (int)(length>>8), (int)(length+255-RUN_MASK_LZ4)/255, (int)(oend-*op));
    if ((limitedOutputBuffer) && (*op + length + ((length+255-RUN_MASK_LZ4)/255) > oend)) return 1; /* Check output buffer overflow */
    if (length>=(int)RUN_MASK_LZ4) { int len; *token=(RUN_MASK_LZ4<<ML_BITS_LZ4); len = length-RUN_MASK_LZ4; for(; len > 254 ; len-=255) *(*op)++ = 255;  *(*op)++ = (BYTE)len; }
    else *token = (BYTE)(length<<ML_BITS_LZ4);
    memcpy(*op, *anchor, length);
    *op += length;
    return 0;
}
