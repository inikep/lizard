
/**************************************
*  Tuning parameters
**************************************/
/*
 * HEAPMODE :
 * Select how default compression functions will allocate memory for their hash table,
 * in memory stack (0:default, fastest), or in memory heap (1:requires malloc()).
 */
#define HEAPMODE 0

/*
 * ACCELERATION_DEFAULT :
 * Select "acceleration" for LZ5_compress_fast() when parameter value <= 0
 */
#define ACCELERATION_DEFAULT 1


/**************************************
*  Includes
**************************************/
#include "lz5.h"


/**************************************
*  Compiler Options
**************************************/
#ifdef _MSC_VER    /* Visual Studio */
#  define FORCE_INLINE static __forceinline
#  include <intrin.h>
#  pragma warning(disable : 4127)        /* disable: C4127: conditional expression is constant */
#  pragma warning(disable : 4293)        /* disable: C4293: too large shift (32-bits) */
#else
#  if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 199901L)   /* C99 */
#    if defined(__GNUC__) || defined(__clang__)
#      define FORCE_INLINE static inline __attribute__((always_inline))
#    else
#      define FORCE_INLINE static inline
#    endif
#  else
#    define FORCE_INLINE static
#  endif   /* __STDC_VERSION__ */
#endif  /* _MSC_VER */

/* LZ5_GCC_VERSION is defined into lz5.h */
#if (LZ5_GCC_VERSION >= 302) || (__INTEL_COMPILER >= 800) || defined(__clang__)
#  define expect(expr,value)    (__builtin_expect ((expr),(value)) )
#else
#  define expect(expr,value)    (expr)
#endif

#define likely(expr)     expect((expr) != 0, 1)
#define unlikely(expr)   expect((expr) != 0, 0)


/**************************************
*  Memory routines
**************************************/
#include <stdlib.h>   /* malloc, calloc, free */
#define ALLOCATOR(n,s) calloc(n,s)
#define FREEMEM        free
#include <string.h>   /* memset, memcpy */
#define MEM_INIT       memset


/**************************************
*  Common Constants
**************************************/
#define MINMATCH 3

#define WILDCOPYLENGTH 8
#define LASTLITERALS 5
#define MFLIMIT (WILDCOPYLENGTH+MINMATCH)
static const int LZ5_minLength = (MFLIMIT+1);

#define KB *(1 <<10)
#define MB *(1 <<20)
#define GB *(1U<<30)

#define MAXD_LOG 22
#define MAX_DISTANCE ((1 << MAXD_LOG) - 1)
#define LZ5_DICT_SIZE (1 << MAXD_LOG)

#define ML_BITS  3
#define ML_MASK  ((1U<<ML_BITS)-1)
#define RUN_BITS 3
#define RUN_MASK ((1U<<RUN_BITS)-1)
#define RUN_BITS2 2
#define RUN_MASK2 ((1U<<RUN_BITS2)-1)
#define ML_RUN_BITS (ML_BITS + RUN_BITS)
#define ML_RUN_BITS2 (ML_BITS + RUN_BITS2)

#define LZ5_SHORT_OFFSET_BITS 10
#define LZ5_SHORT_OFFSET_DISTANCE (1<<LZ5_SHORT_OFFSET_BITS)
#define LZ5_MID_OFFSET_BITS 16
#define LZ5_MID_OFFSET_DISTANCE (1<<LZ5_MID_OFFSET_BITS)


/**************************************
*  Common Utils
**************************************/
#define LZ5_STATIC_ASSERT(c)    { enum { LZ5_static_assert = 1/(int)(!!(c)) }; }   /* use only *after* variable declarations */



/* *************************************
*  Types
***************************************/
/** from faster to stronger */
typedef enum { LZ5HC_fast, LZ5HC_greedy, LZ5HC_lazy, LZ5HC_lazy2, LZ5HC_lazymax, LZ5HC_btlazy2 } LZ5HC_strategy;

typedef struct
{
    U32 windowLog;     /* largest match distance : impact decompression buffer size */
    U32 contentLog;    /* full search segment : larger == more compression, slower, more memory (useless for fast) */
    U32 hashLog;       /* dispatch table : larger == more memory, faster*/
    U32 hashLog3;      /* dispatch table : larger == more memory, faster*/
    U32 searchNum;     /* nb of searches : larger == more compression, slower*/
    U32 searchLength;  /* size of matches : larger == faster decompression */
    LZ5HC_strategy strategy;
} LZ5HC_parameters;

/* *************************************
*  Pre-defined compression levels
***************************************/
#define LZ5HC_MAX_CLEVEL 4
static const LZ5HC_parameters LZ5HC_defaultParameters[LZ5HC_MAX_CLEVEL] =
{
    /* W,  C,  H, H3, S,  L, strat */
    { 17, 12, 12, 13,  1,  4, LZ5HC_fast    },  /* level  0 - never used */
    { 17, 12, 13, 13,  1,  6, LZ5HC_fast    },  /* level  1 */
    { 17, 15, 16, 13,  1,  5, LZ5HC_fast    },  /* level  2 */
    { 17, 16, 17, 16,  1,  5, LZ5HC_fast    }   /* level  3 */
};

