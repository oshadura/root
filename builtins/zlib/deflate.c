/* deflate.c -- compress data using the deflation algorithm
 * Copyright (C) 1995-2013 Jean-loup Gailly and Mark Adler
 * For conditions of distribution and use, see copyright notice in zlib.h
 */

/*
 *  ALGORITHM
 *
 *      The "deflation" process depends on being able to identify portions
 *      of the input text which are identical to earlier input (within a
 *      sliding window trailing behind the input currently being processed).
 *
 *      The most straightforward technique turns out to be the fastest for
 *      most input files: try all possible matches and select the longest.
 *      The key feature of this algorithm is that insertions into the string
 *      dictionary are very simple and thus fast, and deletions are avoided
 *      completely. Insertions are performed at each input character, whereas
 *      string matches are performed only when the previous match ends. So it
 *      is preferable to spend more time in matches to allow very fast string
 *      insertions and avoid deletions. The matching algorithm for small
 *      strings is inspired from that of Rabin & Karp. A brute force approach
 *      is used to find longer strings when a small match has been found.
 *      A similar algorithm is used in comic (by Jan-Mark Wams) and freeze
 *      (by Leonid Broukhis).
 *         A previous version of this file used a more sophisticated algorithm
 *      (by Fiala and Greene) which is guaranteed to run in linear amortized
 *      time, but has a larger average cost, uses more memory and is patented.
 *      However the F&G algorithm may be faster for some highly redundant
 *      files if the parameter max_chain_length (described below) is too large.
 *
 *  ACKNOWLEDGEMENTS
 *
 *      The idea of lazy evaluation of matches is due to Jan-Mark Wams, and
 *      I found it in 'freeze' written by Leonid Broukhis.
 *      Thanks to many people for bug reports and testing.
 *
 *  REFERENCES
 *
 *      Deutsch, L.P.,"DEFLATE Compressed Data Format Specification".
 *      Available in http://tools.ietf.org/html/rfc1951
 *
 *      A description of the Rabin and Karp algorithm is given in the book
 *         "Algorithms" by R. Sedgewick, Addison-Wesley, p252.
 *
 *      Fiala,E.R., and Greene,D.H.
 *         Data Compression with Finite Windows, Comm.ACM, 32,4 (1989) 490-595
 *
 */

#if defined(__cplusplus)
#define UNUSED(x)       // = nothing
#elif defined(__GNUC__)
#define UNUSED(x)       x##_UNUSED __attribute__((unused))
#else
#define UNUSED(x)       x##_UNUSED
#endif

/* @(#) $Id$ */

#include "deflate.h"
#include "trees.c"

#ifdef __x86_64__
#include "cpuid.h"
#include <immintrin.h>
#endif

const char deflate_copyright[] =
   " deflate 1.2.8 Copyright 1995-2013 Jean-loup Gailly and Mark Adler ";
/*
  If you use the zlib library in a product, an acknowledgment is welcome
  in the documentation of your product. If for some reason you cannot
  include such an acknowledgment, I would appreciate that you keep this
  copyright string in the executable of your product.
 */

/* ===========================================================================
 *  Function prototypes.
 */

typedef block_state (*compress_func)(deflate_state *s, int flush);
/* Compression function. Returns the block state after the call. */

static void fill_window(deflate_state *s);
static block_state deflate_stored(deflate_state *s, int flush);
static block_state deflate_fast(deflate_state *s, int flush);
static block_state deflate_quick(deflate_state *s, int flush);
static block_state deflate_slow(deflate_state *s, int flush);
static block_state deflate_rle(deflate_state *s, int flush);
static block_state deflate_huff(deflate_state *s, int flush);
static void lm_init(deflate_state *s);
static void putShortMSB(deflate_state *s, uint32_t b);
static void flush_pending(z_streamp strm);
static int read_buf(z_streamp strm, uint8_t  *buf, uint32_t  size);

#ifdef DEBUG
static  void check_match(deflate_state *s, IPos start, IPos match,
                            int length);
#endif

/* ===========================================================================
 * Local data
 */

#define NIL 0
/* Tail of hash chains */
#define ACTUAL_MIN_MATCH 4
/* Values for max_lazy_match, good_match and max_chain_length, depending on
 * the desired pack level (0..9). The values given below have been tuned to
 * exclude worst case performance for pathological files. Better values may be
 * found for specific files.
 */
typedef struct config_s {
   uint16_t good_length; /* reduce lazy search above this match length */
   uint16_t max_lazy;    /* do not perform lazy search above this match length */
   uint16_t nice_length; /* quit search above this match length */
   uint16_t max_chain;
   compress_func func;
} config;

#if defined(__x86_64__) && !defined(__linux__)
static const config configuration_table[10] = {
/*      good lazy nice chain */
/* 0 */ {0,    0,  0,    0, deflate_stored},  /* store only */
/* 1 */ {4,    4,  8,    4, deflate_fast}, /* max speed, no lazy matches */
/* 2 */ {4,    5, 16,    8, deflate_fast},
/* 3 */ {4,    6, 32,   32, deflate_fast},

/* 4 */ {4,    4, 16,   16, deflate_slow},  /* lazy matches */
/* 5 */ {8,   16, 32,   32, deflate_slow},
/* 6 */ {8,   16, 128, 128, deflate_slow},
/* 7 */ {8,   32, 128, 256, deflate_slow},
/* 8 */ {32, 128, 258, 1024, deflate_slow},
/* 9 */ {32, 258, 258, 4096, deflate_slow}}; /* max compression */
#else
static const config configuration_table[10] = {
/*      good lazy nice chain */
/* 0 */ {0,    0,  0,    0, deflate_stored},  /* store only */
/* 1 */ {4,    4,  8,    4, deflate_slow}, /* max speed, no lazy matches */
/* 2 */ {4,    5, 16,    8, deflate_fast},
/* 3 */ {4,    6, 32,   32, deflate_fast},

/* 4 */ {4,    4, 16,   16, deflate_slow},  /* lazy matches */
/* 5 */ {8,   16, 32,   32, deflate_slow},
/* 6 */ {8,   16, 128, 128, deflate_slow},
/* 7 */ {8,   32, 128, 256, deflate_slow},
/* 8 */ {32, 128, 258, 1024, deflate_slow},
/* 9 */ {32, 258, 258, 4096, deflate_slow}}; /* max compression */
#endif

/* Note: the deflate() code requires max_lazy >= MIN_MATCH and max_chain >= 4
 * For deflate_fast() (levels <= 3) good is ignored and lazy has a different
 * meaning.
 */

#define EQUAL 0
/* result of memcmp for equal strings */

/* rank Z_BLOCK between Z_NO_FLUSH and Z_PARTIAL_FLUSH */
#define RANK(f) (((f) << 1) - ((f) > 4 ? 9 : 0))

static uint32_t hash_func_default(deflate_state *s, uint32_t h, void* str) {
    return ((h << s->hash_shift) ^ (*(uint32_t*)str)) & s->hash_mask;
    // return ((h << s->hash_shift) ^ (s->window[*(uint32_t*)str + (ACTUAL_MIN_MATCH-1)])) & s->hash_mask;
}

#if defined (__aarch64__)
#include <arm_neon.h>

#pragma GCC push_options
#if __ARM_ARCH >= 8
#pragma GCC target ("arch=armv8-a+crc")
#endif

#if defined (__ARM_FEATURE_CRC32)
#include <arm_acle.h>

static uint32_t hash_func(deflate_state *s, uint32_t UNUSED(h), void* str) {
    return __crc32cw(0, *(uint32_t*)str) & s->hash_mask;
}

#else // ARMv8 without crc32 support

static uint32_t hash_func(deflate_state *s, uint32_t h, void* str) {
    return hash_func_default(s, h, str);
}

#endif // ARMv8 without crc32 support

#elif defined (__x86_64__) && !defined (__linux__) // only for 64bit systems

#include <immintrin.h>

static uint32_t hash_func_sse42(deflate_state *s, uint32_t UNUSED(h), void* str) __attribute__ ((__target__ ("sse4.2")));

static uint32_t hash_func_sse42(deflate_state *s, uint32_t UNUSED(h), void* str) {
    return _mm_crc32_u32(0, *(uint32_t*)str) & s->hash_mask;
}

static uint32_t hash_func(deflate_state *s, uint32_t UNUSED(h), void* str) __attribute__ ((ifunc ("resolve_hash_func")));

void *resolve_hash_func(void)
{
  unsigned int eax, ebx, ecx, edx;
  if (!__get_cpuid (1, &eax, &ebx, &ecx, &edx))
    return hash_func_default;
  /* We need SSE4.2 ISA support */
  if (!(ecx & bit_SSE4_2))
    return hash_func_default;
  return hash_func_sse42;
}

#else

static uint32_t hash_func(deflate_state *s, uint32_t h, void* str) {
    return hash_func_default(s, h, str);
}

#endif


/* ===========================================================================
 * Insert string str in the dictionary and return the previous head
 * of the hash chain (the most recent string with same hash key).
 * IN  assertion: ACTUAL_MIN_MATCH bytes of str are valid
 *    (except for the last ACTUAL_MIN_MATCH-1 bytes of the input file).
 */
static Pos insert_string(deflate_state *s, Pos str) {
    Pos match_head = 0;
    s->ins_h = hash_func(s, s->ins_h, &s->window[str]);
    match_head = s->prev[(str) & s->w_mask] = s->head[s->ins_h];
    s->head[s->ins_h] = (Pos)str;
    return match_head;
}

static void bulk_insert_str(deflate_state *s, Pos startpos, uint32_t count) {
    uint32_t idx = 0;
    for (idx = 0; idx < count; idx++) {
        s->ins_h = hash_func(s, s->ins_h, &s->window[startpos + idx]);
        s->prev[(startpos + idx) & s->w_mask] = s->head[s->ins_h];
        s->head[s->ins_h] = (Pos)(startpos + idx);
    }
}

static int _tr_tally_lit(deflate_state *s, uint8_t cc) {
    s->d_buf[s->last_lit] = 0;
    s->l_buf[s->last_lit++] = cc;
    s->dyn_ltree[cc].Freq++;
    return (s->last_lit == s->lit_bufsize-1);
}

static int _tr_tally_dist(deflate_state *s, uint16_t dist, uint8_t len) {
    s->d_buf[s->last_lit] = dist;
    s->l_buf[s->last_lit++] = len;
    dist--;
    s->dyn_ltree[_length_code[len]+LITERALS+1].Freq++;
    s->dyn_dtree[d_code(dist)].Freq++;
    return (s->last_lit == s->lit_bufsize-1);
}
/* ===========================================================================
 * Initialize the hash table prev[] will be initialized on the fly.
 */
#define CLEAR_HASH(s) \
    zmemzero((uint8_t *)s->head, (unsigned)(s->hash_size)*sizeof(*s->head));

/* ========================================================================= */
int ZEXPORT deflateInit_(strm, level, version, stream_size)
    z_streamp strm;
    int level;
    const char *version;
    int stream_size;
{
    return deflateInit2_(strm, level, Z_DEFLATED, MAX_WBITS, DEF_MEM_LEVEL,
                         Z_DEFAULT_STRATEGY, version, stream_size);
    /* To do: ignore strm->next_in if we use it as window */
}

/* ========================================================================= */
int ZEXPORT deflateInit2_(strm, level, method, windowBits, memLevel, strategy,
                  version, stream_size)
    z_streamp strm;
    int  level;
    int  method;
    int  windowBits;
    int  memLevel;
    int  strategy;
    const char *version;
    int stream_size;
{
    deflate_state *s;
    int wrap = 1;
    static const char my_version[] = ZLIB_VERSION;

    uint16_t *overlay;
    /* We overlay pending_buf and d_buf+l_buf. This works since the average
     * output size for (length,distance) codes is <= 24 bits.
     */

    if (version == Z_NULL || version[0] != my_version[0] ||
        stream_size != sizeof(z_stream)) {
        return Z_VERSION_ERROR;
    }
    if (strm == Z_NULL) return Z_STREAM_ERROR;

    strm->msg = Z_NULL;
    if (strm->zalloc == (alloc_func)0) {
#ifdef Z_SOLO
        return Z_STREAM_ERROR;
#else
        strm->zalloc = zcalloc;
        strm->opaque = (voidpf)0;
#endif
    }
    if (strm->zfree == (free_func)0)
#ifdef Z_SOLO
        return Z_STREAM_ERROR;
#else
        strm->zfree = zcfree;
#endif

    if (level == Z_DEFAULT_COMPRESSION) level = 6;

    if (windowBits < 0) { /* suppress zlib wrapper */
        wrap = 0;
        windowBits = -windowBits;
    }
    else if (windowBits > 15) {
        wrap = 2;       /* write gzip wrapper instead */
        windowBits -= 16;
    }
    if (memLevel < 1 || memLevel > MAX_MEM_LEVEL || method != Z_DEFLATED ||
        windowBits < 8 || windowBits > 15 || level < 0 || level > 9 ||
        strategy < 0 || strategy > Z_FIXED) {
        return Z_STREAM_ERROR;
    }
    if (windowBits == 8) windowBits = 9;  /* until 256-byte window bug fixed */
    s = (deflate_state *) ZALLOC(strm, 1, sizeof(deflate_state));
    if (s == Z_NULL) return Z_MEM_ERROR;
    strm->state = (struct internal_state *)s;
    s->strm = strm;

    s->wrap = wrap;
    s->gzhead = Z_NULL;
    s->w_bits = windowBits;
    s->w_size = 1 << s->w_bits;
    s->w_mask = s->w_size - 1;

    s->hash_bits = memLevel + 7;
    s->hash_size = 1 << s->hash_bits;
    s->hash_mask = s->hash_size - 1;

    s->window = (uint8_t *) ZALLOC(strm, s->w_size, 2*sizeof(uint8_t));
    s->prev   = (Pos *)  ZALLOC(strm, s->w_size, sizeof(Pos));
    s->head   = (Pos *)  ZALLOC(strm, s->hash_size, sizeof(Pos));

    s->high_water = 0;      /* nothing written to s->window yet */

    s->lit_bufsize = 1 << (memLevel + 6); /* 16K elements by default */

    overlay = (uint16_t *) ZALLOC(strm, s->lit_bufsize, sizeof(uint16_t)+2);
    s->pending_buf = (uint8_t *) overlay;
    s->pending_buf_size = (uint64_t)s->lit_bufsize * (sizeof(uint16_t)+2L);

    if (s->window == Z_NULL || s->prev == Z_NULL || s->head == Z_NULL ||
        s->pending_buf == Z_NULL) {
        s->status = FINISH_STATE;
        strm->msg = ERR_MSG(Z_MEM_ERROR);
        deflateEnd (strm);
        return Z_MEM_ERROR;
    }
    s->d_buf = overlay + s->lit_bufsize/sizeof(uint16_t);
    s->l_buf = s->pending_buf + (1+sizeof(uint16_t))*s->lit_bufsize;

    s->level = level;
    s->strategy = strategy;
    s->method = (uint8_t)method;
    s->block_open = 0;

    return deflateReset(strm);
}

/* ========================================================================= */
int ZEXPORT deflateSetDictionary (strm, dictionary, dictLength)
    z_streamp strm;
    const uint8_t  *dictionary;
    uint32_t  dictLength;
{
    deflate_state *s;
    uint32_t str, n;
    int wrap;
    uint32_t  avail;
    z_const uint8_t *next;

    if (strm == Z_NULL || strm->state == Z_NULL || dictionary == Z_NULL)
        return Z_STREAM_ERROR;
    s = strm->state;
    wrap = s->wrap;
    if (wrap == 2 || (wrap == 1 && s->status != INIT_STATE) || s->lookahead)
        return Z_STREAM_ERROR;

    /* when using zlib wrappers, compute Adler-32 for provided dictionary */
    if (wrap == 1)
        strm->adler = adler32(strm->adler, dictionary, dictLength);
    s->wrap = 0;                    /* avoid computing Adler-32 in read_buf */

    /* if dictionary would fill window, just replace the history */
    if (dictLength >= s->w_size) {
        if (wrap == 0) {            /* already empty otherwise */
            CLEAR_HASH(s);
            s->strstart = 0;
            s->block_start = 0L;
            s->insert = 0;
        }
        dictionary += dictLength - s->w_size;  /* use the tail */
        dictLength = s->w_size;
    }

    /* insert dictionary into window and hash */
    avail = strm->avail_in;
    next = strm->next_in;
    strm->avail_in = dictLength;
    strm->next_in = (z_const uint8_t*)dictionary;
    fill_window(s);
    while (s->lookahead >= ACTUAL_MIN_MATCH) {
        str = s->strstart;
        n = s->lookahead - (ACTUAL_MIN_MATCH-1);
        bulk_insert_str(s, str, n);
        s->strstart = str + n;
        s->lookahead = ACTUAL_MIN_MATCH-1;
        fill_window(s);
    }
    s->strstart += s->lookahead;
    s->block_start = (long)s->strstart;
    s->insert = s->lookahead;
    s->lookahead = 0;
    s->match_length = s->prev_length = ACTUAL_MIN_MATCH-1;
    s->match_available = 0;
    strm->next_in = next;
    strm->avail_in = avail;
    s->wrap = wrap;
    return Z_OK;
}

/* ========================================================================= */
int ZEXPORT deflateResetKeep (strm)
    z_streamp strm;
{
    deflate_state *s;

    if (strm == Z_NULL || strm->state == Z_NULL ||
        strm->zalloc == (alloc_func)0 || strm->zfree == (free_func)0) {
        return Z_STREAM_ERROR;
    }

    strm->total_in = strm->total_out = 0;
    strm->msg = Z_NULL; /* use zfree if we ever allocate msg dynamically */
    strm->data_type = Z_UNKNOWN;

    s = (deflate_state *)strm->state;
    s->pending = 0;
    s->pending_out = s->pending_buf;

    if (s->wrap < 0) {
        s->wrap = -s->wrap; /* was made negative by deflate(..., Z_FINISH); */
    }
    s->status = s->wrap ? INIT_STATE : BUSY_STATE;
    strm->adler =
        s->wrap == 2 ? crc32(0L, Z_NULL, 0) :
        adler32(0L, Z_NULL, 0);
    s->last_flush = Z_NO_FLUSH;

    _tr_init(s);

    return Z_OK;
}

/* ========================================================================= */
int ZEXPORT deflateReset (strm)
    z_streamp strm;
{
    int ret;

    ret = deflateResetKeep(strm);
    if (ret == Z_OK)
        lm_init(strm->state);
    return ret;
}

/* ========================================================================= */
int ZEXPORT deflateSetHeader (strm, head)
    z_streamp strm;
    gz_headerp head;
{
    if (strm == Z_NULL || strm->state == Z_NULL) return Z_STREAM_ERROR;
    if (strm->state->wrap != 2) return Z_STREAM_ERROR;
    strm->state->gzhead = head;
    return Z_OK;
}

/* ========================================================================= */
int ZEXPORT deflatePending (strm, pending, bits)
    uint32_t  *pending;
    int *bits;
    z_streamp strm;
{
    if (strm == Z_NULL || strm->state == Z_NULL) return Z_STREAM_ERROR;
    if (pending != Z_NULL)
        *pending = strm->state->pending;
    if (bits != Z_NULL)
        *bits = strm->state->bi_valid;
    return Z_OK;
}

/* ========================================================================= */
int ZEXPORT deflatePrime (strm, bits, value)
    z_streamp strm;
    int bits;
    int value;
{
    deflate_state *s;
    int put;

    if (strm == Z_NULL || strm->state == Z_NULL) return Z_STREAM_ERROR;
    s = strm->state;
    if ((uint8_t *)(s->d_buf) < s->pending_out + ((Buf_size + 7) >> 3))
        return Z_BUF_ERROR;
    do {
        put = Buf_size - s->bi_valid;
        if (put > bits)
            put = bits;
        s->bi_buf |= (uint16_t)((value & ((1 << put) - 1)) << s->bi_valid);
        s->bi_valid += put;
        _tr_flush_bits(s);
        value >>= put;
        bits -= put;
    } while (bits);
    return Z_OK;
}

/* ========================================================================= */
int ZEXPORT deflateParams(strm, level, strategy)
    z_streamp strm;
    int level;
    int strategy;
{
    deflate_state *s;
    compress_func func;
    int err = Z_OK;

    if (strm == Z_NULL || strm->state == Z_NULL) return Z_STREAM_ERROR;
    s = strm->state;

    if (level == Z_DEFAULT_COMPRESSION) level = 6;
    if (level < 0 || level > 9 || strategy < 0 || strategy > Z_FIXED) {
        return Z_STREAM_ERROR;
    }
    func = configuration_table[s->level].func;

    if ((strategy != s->strategy || func != configuration_table[level].func) &&
        strm->total_in != 0) {
        /* Flush the last buffer: */
        err = deflate(strm, Z_BLOCK);
        if (err == Z_BUF_ERROR && s->pending == 0)
            err = Z_OK;
    }
    if (s->level != level) {
        s->level = level;
        s->max_lazy_match   = configuration_table[level].max_lazy;
        s->good_match       = configuration_table[level].good_length;
        s->nice_match       = configuration_table[level].nice_length;
        s->max_chain_length = configuration_table[level].max_chain;
    }
    s->strategy = strategy;
    return err;
}

/* ========================================================================= */
int ZEXPORT deflateTune(strm, good_length, max_lazy, nice_length, max_chain)
    z_streamp strm;
    int good_length;
    int max_lazy;
    int nice_length;
    int max_chain;
{
    deflate_state *s;

    if (strm == Z_NULL || strm->state == Z_NULL) return Z_STREAM_ERROR;
    s = strm->state;
    s->good_match = good_length;
    s->max_lazy_match = max_lazy;
    s->nice_match = nice_length;
    s->max_chain_length = max_chain;
    return Z_OK;
}

/* =========================================================================
 * For the default windowBits of 15 and memLevel of 8, this function returns
 * a close to exact, as well as small, upper bound on the compressed size.
 * They are coded as constants here for a reason--if the #define's are
 * changed, then this function needs to be changed as well.  The return
 * value for 15 and 8 only works for those exact settings.
 *
 * For any setting other than those defaults for windowBits and memLevel,
 * the value returned is a conservative worst case for the maximum expansion
 * resulting from using fixed blocks instead of stored blocks, which deflate
 * can emit on compressed data for some combinations of the parameters.
 *
 * This function could be more sophisticated to provide closer upper bounds for
 * every combination of windowBits and memLevel.  But even the conservative
 * upper bound of about 14% expansion does not seem onerous for output buffer
 * allocation.
 */
uint64_t ZEXPORT deflateBound(strm, sourceLen)
    z_streamp strm;
    uint64_t sourceLen;
{
    deflate_state *s;
    uint64_t complen, wraplen;
    uint8_t  *str;

    /* conservative upper bound for compressed data */
    complen = sourceLen +
              ((sourceLen + 7) >> 3) + ((sourceLen + 63) >> 6) + 5;

    /* if can't get parameters, return conservative bound plus zlib wrapper */
    if (strm == Z_NULL || strm->state == Z_NULL)
        return complen + 6;

    /* compute wrapper length */
    s = strm->state;
    switch (s->wrap) {
    case 0:                                 /* raw deflate */
        wraplen = 0;
        break;
    case 1:                                 /* zlib wrapper */
        wraplen = 6 + (s->strstart ? 4 : 0);
        break;
    case 2:                                 /* gzip wrapper */
        wraplen = 18;
        if (s->gzhead != Z_NULL) {          /* user-supplied gzip header */
            if (s->gzhead->extra != Z_NULL)
                wraplen += 2 + s->gzhead->extra_len;
            str = s->gzhead->name;
            if (str != Z_NULL)
                do {
                    wraplen++;
                } while (*str++);
            str = s->gzhead->comment;
            if (str != Z_NULL)
                do {
                    wraplen++;
                } while (*str++);
            if (s->gzhead->hcrc)
                wraplen += 2;
        }
        break;
    default:                                /* for compiler happiness */
        wraplen = 6;
    }

    /* if not default parameters, return conservative bound */
    if (s->w_bits != 15 || s->hash_bits != 8 + 7)
        return complen + wraplen;

    /* default settings: return tight bound for that case */
    return sourceLen + (sourceLen >> 12) + (sourceLen >> 14) +
           (sourceLen >> 25) + 13 - 6 + wraplen;
}

/* =========================================================================
 * Put a short in the pending buffer. The 16-bit value is put in MSB order.
 * IN assertion: the stream state is correct and there is enough room in
 * pending_buf.
 */
static void putShortMSB (s, b)
    deflate_state *s;
    uint32_t b;
{
    put_byte(s, (uint8_t)(b >> 8));
    put_byte(s, (uint8_t)(b & 0xff));
}

/* =========================================================================
 * Flush as much pending output as possible. All deflate() output goes
 * through this function so some applications may wish to modify it
 * to avoid allocating a large strm->next_out buffer and copying into it.
 * (See also read_buf()).
 */
static void flush_pending(strm)
    z_streamp strm;
{
    uint32_t  len;
    deflate_state *s = strm->state;

    _tr_flush_bits(s);
    len = s->pending;
    if (len > strm->avail_out) len = strm->avail_out;
    if (len == 0) return;

    zmemcpy(strm->next_out, s->pending_out, len);
    strm->next_out  += len;
    s->pending_out  += len;
    strm->total_out += len;
    strm->avail_out  -= len;
    s->pending -= len;
    if (s->pending == 0) {
        s->pending_out = s->pending_buf;
    }
}

/* ========================================================================= */
int ZEXPORT deflate (strm, flush)
    z_streamp strm;
    int flush;
{
    int old_flush; /* value of flush param for previous deflate call */
    deflate_state *s;

    if (strm == Z_NULL || strm->state == Z_NULL ||
        flush > Z_BLOCK || flush < 0) {
        return Z_STREAM_ERROR;
    }
    s = strm->state;

    if (strm->next_out == Z_NULL ||
        (strm->next_in == Z_NULL && strm->avail_in != 0) ||
        (s->status == FINISH_STATE && flush != Z_FINISH)) {
        ERR_RETURN(strm, Z_STREAM_ERROR);
    }
    if (strm->avail_out == 0) ERR_RETURN(strm, Z_BUF_ERROR);

    s->strm = strm; /* just in case */
    old_flush = s->last_flush;
    s->last_flush = flush;

    /* Write the header */
    if (s->status == INIT_STATE) {
        if (s->wrap == 2) {
            strm->adler = crc32(0L, Z_NULL, 0);
            put_byte(s, 31);
            put_byte(s, 139);
            put_byte(s, 8);
            if (s->gzhead == Z_NULL) {
                put_byte(s, 0);
                put_byte(s, 0);
                put_byte(s, 0);
                put_byte(s, 0);
                put_byte(s, 0);
                put_byte(s, s->level == 9 ? 2 :
                            (s->strategy >= Z_HUFFMAN_ONLY || s->level < 2 ?
                             4 : 0));
                put_byte(s, OS_CODE);
                s->status = BUSY_STATE;
            }
            else {
                put_byte(s, (s->gzhead->text ? 1 : 0) +
                            (s->gzhead->hcrc ? 2 : 0) +
                            (s->gzhead->extra == Z_NULL ? 0 : 4) +
                            (s->gzhead->name == Z_NULL ? 0 : 8) +
                            (s->gzhead->comment == Z_NULL ? 0 : 16)
                        );
                put_byte(s, (uint8_t)(s->gzhead->time & 0xff));
                put_byte(s, (uint8_t)((s->gzhead->time >> 8) & 0xff));
                put_byte(s, (uint8_t)((s->gzhead->time >> 16) & 0xff));
                put_byte(s, (uint8_t)((s->gzhead->time >> 24) & 0xff));
                put_byte(s, s->level == 9 ? 2 :
                            (s->strategy >= Z_HUFFMAN_ONLY || s->level < 2 ?
                             4 : 0));
                put_byte(s, s->gzhead->os & 0xff);
                if (s->gzhead->extra != Z_NULL) {
                    put_byte(s, s->gzhead->extra_len & 0xff);
                    put_byte(s, (s->gzhead->extra_len >> 8) & 0xff);
                }
                if (s->gzhead->hcrc)
                    strm->adler = crc32(strm->adler, s->pending_buf,
                                        s->pending);
                s->gzindex = 0;
                s->status = EXTRA_STATE;
            }
        }
        else
        {
            uint32_t header = (Z_DEFLATED + ((s->w_bits-8)<<4)) << 8;
            uint32_t level_flags;

            if (s->strategy >= Z_HUFFMAN_ONLY || s->level < 2)
                level_flags = 0;
            else if (s->level < 6)
                level_flags = 1;
            else if (s->level == 6)
                level_flags = 2;
            else
                level_flags = 3;
            header |= (level_flags << 6);
            if (s->strstart != 0) header |= PRESET_DICT;
            header += 31 - (header % 31);

            s->status = BUSY_STATE;
            putShortMSB(s, header);

            /* Save the adler32 of the preset dictionary: */
            if (s->strstart != 0) {
                putShortMSB(s, (uint32_t)(strm->adler >> 16));
                putShortMSB(s, (uint32_t)(strm->adler & 0xffff));
            }
            strm->adler = adler32(0L, Z_NULL, 0);
        }
    }
    if (s->status == EXTRA_STATE) {
        if (s->gzhead->extra != Z_NULL) {
            uint32_t beg = s->pending;  /* start of bytes to update crc */

            while (s->gzindex < (s->gzhead->extra_len & 0xffff)) {
                if (s->pending == s->pending_buf_size) {
                    if (s->gzhead->hcrc && s->pending > beg)
                        strm->adler = crc32(strm->adler, s->pending_buf + beg,
                                            s->pending - beg);
                    flush_pending(strm);
                    beg = s->pending;
                    if (s->pending == s->pending_buf_size)
                        break;
                }
                put_byte(s, s->gzhead->extra[s->gzindex]);
                s->gzindex++;
            }
            if (s->gzhead->hcrc && s->pending > beg)
                strm->adler = crc32(strm->adler, s->pending_buf + beg,
                                    s->pending - beg);
            if (s->gzindex == s->gzhead->extra_len) {
                s->gzindex = 0;
                s->status = NAME_STATE;
            }
        }
        else
            s->status = NAME_STATE;
    }
    if (s->status == NAME_STATE) {
        if (s->gzhead->name != Z_NULL) {
            uint32_t beg = s->pending;  /* start of bytes to update crc */
            int val;

            do {
                if (s->pending == s->pending_buf_size) {
                    if (s->gzhead->hcrc && s->pending > beg)
                        strm->adler = crc32(strm->adler, s->pending_buf + beg,
                                            s->pending - beg);
                    flush_pending(strm);
                    beg = s->pending;
                    if (s->pending == s->pending_buf_size) {
                        val = 1;
                        break;
                    }
                }
                val = s->gzhead->name[s->gzindex++];
                put_byte(s, val);
            } while (val != 0);
            if (s->gzhead->hcrc && s->pending > beg)
                strm->adler = crc32(strm->adler, s->pending_buf + beg,
                                    s->pending - beg);
            if (val == 0) {
                s->gzindex = 0;
                s->status = COMMENT_STATE;
            }
        }
        else
            s->status = COMMENT_STATE;
    }
    if (s->status == COMMENT_STATE) {
        if (s->gzhead->comment != Z_NULL) {
            uint32_t beg = s->pending;  /* start of bytes to update crc */
            int val;

            do {
                if (s->pending == s->pending_buf_size) {
                    if (s->gzhead->hcrc && s->pending > beg)
                        strm->adler = crc32(strm->adler, s->pending_buf + beg,
                                            s->pending - beg);
                    flush_pending(strm);
                    beg = s->pending;
                    if (s->pending == s->pending_buf_size) {
                        val = 1;
                        break;
                    }
                }
                val = s->gzhead->comment[s->gzindex++];
                put_byte(s, val);
            } while (val != 0);
            if (s->gzhead->hcrc && s->pending > beg)
                strm->adler = crc32(strm->adler, s->pending_buf + beg,
                                    s->pending - beg);
            if (val == 0)
                s->status = HCRC_STATE;
        }
        else
            s->status = HCRC_STATE;
    }
    if (s->status == HCRC_STATE) {
        if (s->gzhead->hcrc) {
            if (s->pending + 2 > s->pending_buf_size)
                flush_pending(strm);
            if (s->pending + 2 <= s->pending_buf_size) {
                put_byte(s, (uint8_t)(strm->adler & 0xff));
                put_byte(s, (uint8_t)((strm->adler >> 8) & 0xff));
                strm->adler = crc32(0L, Z_NULL, 0);
                s->status = BUSY_STATE;
            }
        }
        else
            s->status = BUSY_STATE;
    }

    /* Flush as much pending output as possible */
    if (s->pending != 0) {
        flush_pending(strm);
        if (strm->avail_out == 0) {
            /* Since avail_out is 0, deflate will be called again with
             * more output space, but possibly with both pending and
             * avail_in equal to zero. There won't be anything to do,
             * but this is not an error situation so make sure we
             * return OK instead of BUF_ERROR at next call of deflate:
             */
            s->last_flush = -1;
            return Z_OK;
        }

    /* Make sure there is something to do and avoid duplicate consecutive
     * flushes. For repeated and useless calls with Z_FINISH, we keep
     * returning Z_STREAM_END instead of Z_BUF_ERROR.
     */
    } else if (strm->avail_in == 0 && RANK(flush) <= RANK(old_flush) &&
               flush != Z_FINISH) {
        ERR_RETURN(strm, Z_BUF_ERROR);
    }

    /* User must not provide more input after the first FINISH: */
    if (s->status == FINISH_STATE && strm->avail_in != 0) {
        ERR_RETURN(strm, Z_BUF_ERROR);
    }

    /* Start a new block or continue the current one.
     */
    if (strm->avail_in != 0 || s->lookahead != 0 ||
        (flush != Z_NO_FLUSH && s->status != FINISH_STATE)) {
        block_state bstate;

        bstate = s->strategy == Z_HUFFMAN_ONLY ? deflate_huff(s, flush) :
                    (s->strategy == Z_RLE ? deflate_rle(s, flush) :
                        (*(configuration_table[s->level].func))(s, flush));

        if (bstate == finish_started || bstate == finish_done) {
            s->status = FINISH_STATE;
        }
        if (bstate == need_more || bstate == finish_started) {
            if (strm->avail_out == 0) {
                s->last_flush = -1; /* avoid BUF_ERROR next call, see above */
            }
            return Z_OK;
            /* If flush != Z_NO_FLUSH && avail_out == 0, the next call
             * of deflate should use the same flush parameter to make sure
             * that the flush is complete. So we don't have to output an
             * empty block here, this will be done at next call. This also
             * ensures that for a very small output buffer, we emit at most
             * one empty block.
             */
        }
        if (bstate == block_done) {
            if (flush == Z_PARTIAL_FLUSH) {
                _tr_align(s);
            } else if (flush != Z_BLOCK) { /* FULL_FLUSH or SYNC_FLUSH */
                _tr_stored_block(s, (uint8_t*)0, 0L, 0);
                /* For a full flush, this empty block will be recognized
                 * as a special marker by inflate_sync().
                 */
                if (flush == Z_FULL_FLUSH) {
                    CLEAR_HASH(s);             /* forget history */
                    if (s->lookahead == 0) {
                        s->strstart = 0;
                        s->block_start = 0L;
                        s->insert = 0;
                    }
                }
            }
            flush_pending(strm);
            if (strm->avail_out == 0) {
              s->last_flush = -1; /* avoid BUF_ERROR at next call, see above */
              return Z_OK;
            }
        }
    }
    Assert(strm->avail_out > 0, "bug2");

    if (flush != Z_FINISH) return Z_OK;
    if (s->wrap <= 0) return Z_STREAM_END;

    /* Write the trailer */
    if (s->wrap == 2) {
        put_byte(s, (uint8_t)(strm->adler & 0xff));
        put_byte(s, (uint8_t)((strm->adler >> 8) & 0xff));
        put_byte(s, (uint8_t)((strm->adler >> 16) & 0xff));
        put_byte(s, (uint8_t)((strm->adler >> 24) & 0xff));
        put_byte(s, (uint8_t)(strm->total_in & 0xff));
        put_byte(s, (uint8_t)((strm->total_in >> 8) & 0xff));
        put_byte(s, (uint8_t)((strm->total_in >> 16) & 0xff));
        put_byte(s, (uint8_t)((strm->total_in >> 24) & 0xff));
    }
    else
    {
        putShortMSB(s, (uint32_t)(strm->adler >> 16));
        putShortMSB(s, (uint32_t)(strm->adler & 0xffff));
    }
    flush_pending(strm);
    /* If avail_out is zero, the application will call deflate again
     * to flush the rest.
     */
    if (s->wrap > 0) s->wrap = -s->wrap; /* write the trailer only once! */
    return s->pending != 0 ? Z_OK : Z_STREAM_END;
}

/* ========================================================================= */
int ZEXPORT deflateEnd (strm)
    z_streamp strm;
{
    int status;

    if (strm == Z_NULL || strm->state == Z_NULL) return Z_STREAM_ERROR;

    status = strm->state->status;
    if (status != INIT_STATE &&
        status != EXTRA_STATE &&
        status != NAME_STATE &&
        status != COMMENT_STATE &&
        status != HCRC_STATE &&
        status != BUSY_STATE &&
        status != FINISH_STATE) {
      return Z_STREAM_ERROR;
    }

    /* Deallocate in reverse order of allocations: */
    TRY_FREE(strm, strm->state->pending_buf);
    TRY_FREE(strm, strm->state->head);
    TRY_FREE(strm, strm->state->prev);
    TRY_FREE(strm, strm->state->window);

    ZFREE(strm, strm->state);
    strm->state = Z_NULL;

    return status == BUSY_STATE ? Z_DATA_ERROR : Z_OK;
}

/* =========================================================================
 * Copy the source state to the destination state.
 * To simplify the source, this is not supported for 16-bit MSDOS (which
 * doesn't have enough memory anyway to duplicate compression states).
 */
int ZEXPORT deflateCopy (dest, source)
    z_streamp dest;
    z_streamp source;
{
    deflate_state *ds;
    deflate_state *ss;
    uint16_t *overlay;


    if (source == Z_NULL || dest == Z_NULL || source->state == Z_NULL) {
        return Z_STREAM_ERROR;
    }

    ss = source->state;

    zmemcpy((voidpf)dest, (voidpf)source, sizeof(z_stream));

    ds = (deflate_state *) ZALLOC(dest, 1, sizeof(deflate_state));
    if (ds == Z_NULL) return Z_MEM_ERROR;
    dest->state = (struct internal_state *) ds;
    zmemcpy((voidpf)ds, (voidpf)ss, sizeof(deflate_state));
    ds->strm = dest;

    ds->window = (uint8_t *) ZALLOC(dest, ds->w_size, 2*sizeof(uint8_t));
    ds->prev   = (Pos *)  ZALLOC(dest, ds->w_size, sizeof(Pos));
    ds->head   = (Pos *)  ZALLOC(dest, ds->hash_size, sizeof(Pos));
    overlay = (uint16_t *) ZALLOC(dest, ds->lit_bufsize, sizeof(uint16_t)+2);
    ds->pending_buf = (uint8_t *) overlay;

    if (ds->window == Z_NULL || ds->prev == Z_NULL || ds->head == Z_NULL ||
        ds->pending_buf == Z_NULL) {
        deflateEnd (dest);
        return Z_MEM_ERROR;
    }
    /* following zmemcpy do not work for 16-bit MSDOS */
    zmemcpy(ds->window, ss->window, ds->w_size * 2 * sizeof(uint8_t));
    zmemcpy((voidpf)ds->prev, (voidpf)ss->prev, ds->w_size * sizeof(Pos));
    zmemcpy((voidpf)ds->head, (voidpf)ss->head, ds->hash_size * sizeof(Pos));
    zmemcpy(ds->pending_buf, ss->pending_buf, (uint32_t)ds->pending_buf_size);

    ds->pending_out = ds->pending_buf + (ss->pending_out - ss->pending_buf);
    ds->d_buf = overlay + ds->lit_bufsize/sizeof(uint16_t);
    ds->l_buf = ds->pending_buf + (1+sizeof(uint16_t))*ds->lit_bufsize;

    ds->l_desc.dyn_tree = ds->dyn_ltree;
    ds->d_desc.dyn_tree = ds->dyn_dtree;
    ds->bl_desc.dyn_tree = ds->bl_tree;

    return Z_OK;
}

/* ===========================================================================
 * Read a new buffer from the current input stream, update the adler32
 * and total number of bytes read.  All deflate() input goes through
 * this function so some applications may wish to modify it to avoid
 * allocating a large strm->next_in buffer and copying from it.
 * (See also flush_pending()).
 */
static int read_buf(strm, buf, size)
    z_streamp strm;
    uint8_t  *buf;
    uint32_t  size;
{
    uint32_t  len = strm->avail_in;

    if (len > size) len = size;
    if (len == 0) return 0;

    strm->avail_in  -= len;

    zmemcpy(buf, strm->next_in, len);
    if (strm->state->wrap == 1) {
        strm->adler = adler32(strm->adler, buf, len);
    }
    else if (strm->state->wrap == 2) {
        strm->adler = crc32(strm->adler, buf, len);
    }
    strm->next_in  += len;
    strm->total_in += len;

    return (int)len;
}

/* ===========================================================================
 * Initialize the "longest match" routines for a new zlib stream
 */
static void lm_init (s)
    deflate_state *s;
{
    s->window_size = (uint64_t)2L*s->w_size;

    CLEAR_HASH(s);

    /* Set the default configuration parameters:
     */
    s->max_lazy_match   = configuration_table[s->level].max_lazy;
    s->good_match       = configuration_table[s->level].good_length;
    s->nice_match       = configuration_table[s->level].nice_length;
    s->max_chain_length = configuration_table[s->level].max_chain;

    s->strstart = 0;
    s->block_start = 0L;
    s->lookahead = 0;
    s->insert = 0;
    s->match_length = s->prev_length = ACTUAL_MIN_MATCH-1;
    s->match_available = 0;
    s->ins_h = 0;
}

/* longest_match() with minor change to improve performance (in terms of
 * execution time).
 *
 * The pristine longest_match() function is sketched bellow (strip the
 * then-clause of the "#ifdef UNALIGNED_OK"-directive)
 *
 * ------------------------------------------------------------
 * uInt longest_match(...) {
 *    ...
 *    do {
 *        match = s->window + cur_match;                //s0
 *        if (*(ushf*)(match+best_len-1) != scan_end || //s1
 *            *(ushf*)match != scan_start) continue;    //s2
 *        ...
 *
 *        do {
 *        } while (*(ushf*)(scan+=2) == *(ushf*)(match+=2) &&
 *                 *(ushf*)(scan+=2) == *(ushf*)(match+=2) &&
 *                 *(ushf*)(scan+=2) == *(ushf*)(match+=2) &&
 *                 *(ushf*)(scan+=2) == *(ushf*)(match+=2) &&
 *                 scan < strend); //s3
 *
 *        ...
 *    } while(cond); //s4
 *
 * -------------------------------------------------------------
 *
 * The change include:
 *
 *  1) The hottest statements of the function is: s0, s1 and s4. Pull them
 *     together to form a new loop. The benefit is two-fold:
 *
 *    o. Ease the compiler to yield good code layout: the conditional-branch
 *       corresponding to s1 and its biased target s4 become very close (likely,
 *       fit in the same cache-line), hence improving instruction-fetching
 *       efficiency.
 *
 *    o. Ease the compiler to promote "s->window" into register. "s->window"
 *       is loop-invariant; it is supposed to be promoted into register and keep
 *       the value throughout the entire loop. However, there are many such
 *       loop-invariant, and x86-family has small register file; "s->window" is
 *       likely to be chosen as register-allocation victim such that its value
 *       is reloaded from memory in every single iteration. By forming a new loop,
 *       "s->window" is loop-invariant of that newly created tight loop. It is
 *       lot easier for compiler to promote this quantity to register and keep
 *       its value throughout the entire small loop.
 *
 * 2) Transfrom s3 such that it examines sizeof(long)-byte-match at a time.
 *    This is done by:
 *        ------------------------------------------------
 *        v1 = load from "scan" by sizeof(long) bytes
 *        v2 = load from "match" by sizeof(lnog) bytes
 *        v3 = v1 xor v2
 *        match-bit = little-endian-machine(yes-for-x86) ?
 *                     count-trailing-zero(v3) :
 *                     count-leading-zero(v3);
 *
 *        match-byte = match-bit/8
 *
 *        "scan" and "match" advance if necessary
 *       -------------------------------------------------
 */

static uint32_t longest_match(s, cur_match)
    deflate_state *s;
    IPos cur_match;                             /* current match */
{
    uint32_t chain_length = s->max_chain_length;      /* max hash chain length */
    register uint8_t *scan = s->window + s->strstart; /* current string */
    register uint8_t *match;                          /* matched string */
    register int len;                                 /* length of current match */
    int best_len = s->prev_length;                    /* best match length so far */
    int nice_match = s->nice_match;                   /* stop if match long enough */
    IPos limit = s->strstart > (IPos)MAX_DIST(s) ?
        s->strstart - (IPos)MAX_DIST(s) : NIL;
    /* Stop when cur_match becomes <= limit. To simplify the code,
     * we prevent matches with the string of window index 0.
     */
    Pos *prev = s->prev;
    uint32_t wmask = s->w_mask;

    register uint8_t *strend = s->window + s->strstart + MAX_MATCH;
    /* We optimize for a minimal match of four bytes */
    register uint32_t scan_start = *(uint32_t*)scan;
    register uint32_t scan_end   = *(uint32_t*)(scan+best_len-3);

    /* The code is optimized for HASH_BITS >= 8 and MAX_MATCH-2 multiple of 16.
     * It is easy to get rid of this optimization if necessary.
     */
    Assert(s->hash_bits >= 8 && MAX_MATCH == 258, "Code too clever");

    /* Do not waste too much time if we already have a good match: */
    if (s->prev_length >= s->good_match) {
        chain_length >>= 2;
    }
    /* Do not look for matches beyond the end of the input. This is necessary
     * to make deflate deterministic.
     */
    if ((uint32_t)nice_match > s->lookahead) nice_match = s->lookahead;

    Assert((uint64_t)s->strstart <= s->window_size-MIN_LOOKAHEAD, "need lookahead");

    do {
        int cont ;
        Assert(cur_match < s->strstart, "no future");

        /* Skip to next match if the match length cannot increase
         * or if the match length is less than 2.  Note that the checks below
         * for insufficient lookahead only occur occasionally for performance
         * reasons.  Therefore uninitialized memory will be accessed, and
         * conditional jumps will be made that depend on those values.
         * However the length of the match is limited to the lookahead, so
         * the output of deflate is not affected by the uninitialized values.
         */
        cont = 1;
        do {
            match = s->window + cur_match;
            if (likely(*(uint32_t*)(match+best_len-3) != scan_end) || (*(uint32_t*)match != scan_start)) {
                if ((cur_match = prev[cur_match & wmask]) > limit
                    && --chain_length != 0) {
                    continue;
                } else
                    cont = 0;
            }
            break;
        } while (1);

        if (!cont)
            break;

        scan += 4, match+=4;
        do {
            uint64_t sv = *(uint64_t*)(void*)scan;
            uint64_t mv = *(uint64_t*)(void*)match;
            uint64_t xor = sv ^ mv;
            if (xor) {
                int match_byte = __builtin_ctzl(xor) / 8;
                scan += match_byte;
                match += match_byte;
                break;
            } else {
                scan += 8;
                match += 8;
            }
        } while (scan < strend);

        if (scan > strend)
            scan = strend;

        Assert(scan <= s->window+(uint32_t)(s->window_size-1), "wild scan");

        len = MAX_MATCH - (int)(strend - scan);
        scan = strend - MAX_MATCH;

        if (len > best_len) {
            s->match_start = cur_match;
            best_len = len;
            if (len >= nice_match) break;
            scan_end = *(uint32_t*)(scan+best_len-3);
        }
    } while ((cur_match = prev[cur_match & wmask]) > limit
             && --chain_length != 0);

    if ((uint32_t)best_len <= s->lookahead) return (uint32_t)best_len;
    return s->lookahead;
}

#ifdef DEBUG
/* ===========================================================================
 * Check that the match at match_start is indeed a match.
 */
static void check_match(s, start, match, length)
    deflate_state *s;
    IPos start, match;
    int length;
{
    /* check that the match is indeed a match */
    if (zmemcmp(s->window + match,
                s->window + start, length) != EQUAL) {
        fprintf(stderr, " start %u, match %u, length %d\n",
                start, match, length);
        do {
            fprintf(stderr, "%c%c", s->window[match++], s->window[start++]);
        } while (--length != 0);
        z_error("invalid match");
    }
    if (z_verbose > 1) {
        fprintf(stderr,"\\[%d,%d]", start-match, length);
        do { putc(s->window[start++], stderr); } while (--length != 0);
    }
}
#else
#  define check_match(s, start, match, length)
#endif /* DEBUG */

/* ===========================================================================
 * Fill the window when the lookahead becomes insufficient.
 * Updates strstart and lookahead.
 *
 * IN assertion: lookahead < MIN_LOOKAHEAD
 * OUT assertions: strstart <= window_size-MIN_LOOKAHEAD
 *    At least one byte has been read, or avail_in == 0; reads are
 *    performed for at least two bytes (required for the zip translate_eol
 *    option -- not supported here).
 */
static void fill_window_default(s)
    deflate_state *s;
{
    register unsigned n, m;
    register Pos *p;
    unsigned more;    /* Amount of free space at the end of the window. */
    uInt wsize = s->w_size;

    Assert(s->lookahead < MIN_LOOKAHEAD, "already enough lookahead");

    do {
        more = (unsigned)(s->window_size -(ulg)s->lookahead -(ulg)s->strstart);

        /* Deal with !@#$% 64K limit: */
        if (sizeof(int) <= 2) {
            if (more == 0 && s->strstart == 0 && s->lookahead == 0) {
                more = wsize;

            } else if (more == (unsigned)(-1)) {
                /* Very unlikely, but possible on 16 bit machine if
                 * strstart == 0 && lookahead == 1 (input done a byte at time)
                 */
                more--;
            }
        }

        /* If the window is almost full and there is insufficient lookahead,
         * move the upper half to the lower one to make room in the upper half.
         */
        if (s->strstart >= wsize+MAX_DIST(s)) {

            zmemcpy(s->window, s->window+wsize, (unsigned)wsize);
            s->match_start -= wsize;
            s->strstart    -= wsize; /* we now have strstart >= MAX_DIST */
            s->block_start -= (long) wsize;

            /* Slide the hash table (could be avoided with 32 bit values
               at the expense of memory usage). We slide even when level == 0
               to keep the hash table consistent if we switch back to level > 0
               later. (Using level 0 permanently is not an optimal usage of
               zlib, so we don't care about this pathological case.)
             */
            n = s->hash_size;
            p = &s->head[n];
            do {
                m = *--p;
                *p = (Pos)(m >= wsize ? m-wsize : NIL);
            } while (--n);

            n = wsize;
            p = &s->prev[n];
            do {
                m = *--p;
                *p = (Pos)(m >= wsize ? m-wsize : NIL);
                /* If n is not on any hash chain, prev[n] is garbage but
                 * its value will never be used.
                 */
            } while (--n);
            more += wsize;
        }
        if (s->strm->avail_in == 0) break;

        /* If there was no sliding:
         *    strstart <= WSIZE+MAX_DIST-1 && lookahead <= MIN_LOOKAHEAD - 1 &&
         *    more == window_size - lookahead - strstart
         * => more >= window_size - (MIN_LOOKAHEAD-1 + WSIZE + MAX_DIST-1)
         * => more >= window_size - 2*WSIZE + 2
         * In the BIG_MEM or MMAP case (not yet supported),
         *   window_size == input_size + MIN_LOOKAHEAD  &&
         *   strstart + s->lookahead <= input_size => more >= MIN_LOOKAHEAD.
         * Otherwise, window_size == 2*WSIZE so more >= 2.
         * If there was sliding, more >= WSIZE. So in all cases, more >= 2.
         */
        Assert(more >= 2, "more < 2");

        n = read_buf(s->strm, s->window + s->strstart + s->lookahead, more);
        s->lookahead += n;

        /* Initialize the hash value now that we have some input: */
        if (s->lookahead + s->insert >= MIN_MATCH) {
            uInt str = s->strstart - s->insert;
            s->ins_h = s->window[str];
            s->ins_h = hash_func(s, s->ins_h, &s->window[str + 1]);
            while (s->insert) {
                s->ins_h = hash_func(s, s->ins_h, &s->window[str + MIN_MATCH-1]);
                s->prev[str & s->w_mask] = s->head[s->ins_h];
                s->head[s->ins_h] = (Pos)str;
                str++;
                s->insert--;
                if (s->lookahead + s->insert < MIN_MATCH)
                    break;
            }
        }
        /* If the whole input has less than MIN_MATCH bytes, ins_h is garbage,
         * but this is not important since only literal bytes will be emitted.
         */

    } while (s->lookahead < MIN_LOOKAHEAD && s->strm->avail_in != 0);

    /* If the WIN_INIT bytes after the end of the current data have never been
     * written, then zero those bytes in order to avoid memory check reports of
     * the use of uninitialized (or uninitialised as Julian writes) bytes by
     * the longest match routines.  Update the high water mark for the next
     * time through here.  WIN_INIT is set to MAX_MATCH since the longest match
     * routines allow scanning to strstart + MAX_MATCH, ignoring lookahead.
     */
    if (s->high_water < s->window_size) {
        ulg curr = s->strstart + (ulg)(s->lookahead);
        ulg init;

        if (s->high_water < curr) {
            /* Previous high water mark below current data -- zero WIN_INIT
             * bytes or up to end of window, whichever is less.
             */
            init = s->window_size - curr;
            if (init > WIN_INIT)
                init = WIN_INIT;
            zmemzero(s->window + curr, (unsigned)init);
            s->high_water = curr + init;
        }
        else if (s->high_water < (ulg)curr + WIN_INIT) {
            /* High water mark at or above current data, but below current data
             * plus WIN_INIT -- zero out to current data plus WIN_INIT, or up
             * to end of window, whichever is less.
             */
            init = (ulg)curr + WIN_INIT - s->high_water;
            if (init > s->window_size - s->high_water)
                init = s->window_size - s->high_water;
            zmemzero(s->window + s->high_water, (unsigned)init);
            s->high_water += init;
        }
    }

    Assert((ulg)s->strstart <= s->window_size - MIN_LOOKAHEAD,
           "not enough room for search");
}

#if defined (__x86_64__) && defined (__linux__)

/* ===========================================================================
 * Fill the window when the lookahead becomes insufficient.
 * Updates strstart and lookahead.
 *
 * IN assertion: lookahead < MIN_LOOKAHEAD
 * OUT assertions: strstart <= window_size-MIN_LOOKAHEAD
 *    At least one byte has been read, or avail_in == 0; reads are
 *    performed for at least two bytes (required for the zip translate_eol
 *    option -- not supported here).
 */

static void fill_window_sse42(deflate_state *) __attribute__ ((__target__ ("sse4.2")));

static void fill_window_sse42(s)
    deflate_state *s;
{
    register uint32_t n;
    uint32_t more;    /* Amount of free space at the end of the window. */
    uint32_t wsize = s->w_size;

    Assert(s->lookahead < MIN_LOOKAHEAD, "already enough lookahead");

    do {
        more = (unsigned)(s->window_size -(uint64_t)s->lookahead -(ulg)s->strstart);

        /* Deal with !@#$% 64K limit: */
        if (sizeof(int) <= 2) {
            if (more == 0 && s->strstart == 0 && s->lookahead == 0) {
                more = wsize;

            } else if (more == (unsigned)(-1)) {
                /* Very unlikely, but possible on 16 bit machine if
                 * strstart == 0 && lookahead == 1 (input done a byte at time)
                 */
                more--;
            }
        }

        /* If the window is almost full and there is insufficient lookahead,
         * move the upper half to the lower one to make room in the upper half.
         */

        if (s->strstart >= wsize+MAX_DIST(s)) {

            unsigned int i;
            zmemcpy(s->window, s->window+wsize, (unsigned)wsize);
            s->match_start -= wsize;
            s->strstart    -= wsize;
            s->block_start -= (int64_t) wsize;
            n = s->hash_size;
            __m128i  W;
            __m128i *q;
            W = _mm_set1_epi16(wsize);
            q = (__m128i*)s->head;

            for(i = 0; i < n/8; ++i) {
                _mm_storeu_si128(q, _mm_subs_epu16(_mm_loadu_si128(q), W));
                q++;
            }

            n = wsize;
            q = (__m128i*)s->prev;

            for(i = 0; i < n/8; ++i) {
                _mm_storeu_si128(q, _mm_subs_epu16(_mm_loadu_si128(q), W));
                q++;
            }
            more += wsize;
        }
        if (s->strm->avail_in == 0) break;

        /* If there was no sliding:
         *    strstart <= WSIZE+MAX_DIST-1 && lookahead <= MIN_LOOKAHEAD - 1 &&
         *    more == window_size - lookahead - strstart
         * => more >= window_size - (MIN_LOOKAHEAD-1 + WSIZE + MAX_DIST-1)
         * => more >= window_size - 2*WSIZE + 2
         * In the BIG_MEM or MMAP case (not yet supported),
         *   window_size == input_size + MIN_LOOKAHEAD  &&
         *   strstart + s->lookahead <= input_size => more >= MIN_LOOKAHEAD.
         * Otherwise, window_size == 2*WSIZE so more >= 2.
         * If there was sliding, more >= WSIZE. So in all cases, more >= 2.
         */
        Assert(more >= 2, "more < 2");

        n = read_buf(s->strm, s->window + s->strstart + s->lookahead, more);
        s->lookahead += n;

        /* Initialize the hash value now that we have some input: */
        if (s->lookahead + s->insert >= ACTUAL_MIN_MATCH) {
            uint32_t str = s->strstart - s->insert;
            uint32_t ins_h = s->window[str];
            while (s->insert) {
                ins_h = hash_func(s, ins_h, &s->window[str]);
                s->prev[str & s->w_mask] = s->head[ins_h];
                s->head[ins_h] = (Pos)str;
                str++;
                s->insert--;
                if (s->lookahead + s->insert < ACTUAL_MIN_MATCH)
                    break;
            }
            s->ins_h = ins_h;
        }
        /* If the whole input has less than ACTUAL_MIN_MATCH bytes, ins_h is garbage,
         * but this is not important since only literal bytes will be emitted.
         */

    } while (s->lookahead < MIN_LOOKAHEAD && s->strm->avail_in != 0);

    /* If the WIN_INIT bytes after the end of the current data have never been
     * written, then zero those bytes in order to avoid memory check reports of
     * the use of uninitialized (or uninitialised as Julian writes) bytes by
     * the longest match routines.  Update the high water mark for the next
     * time through here.  WIN_INIT is set to MAX_MATCH since the longest match
     * routines allow scanning to strstart + MAX_MATCH, ignoring lookahead.
     */
    if (s->high_water < s->window_size) {
        uint64_t curr = s->strstart + (ulg)(s->lookahead);
        uint64_t init;

        if (s->high_water < curr) {
            /* Previous high water mark below current data -- zero WIN_INIT
             * bytes or up to end of window, whichever is less.
             */
            init = s->window_size - curr;
            if (init > WIN_INIT)
                init = WIN_INIT;
            zmemzero(s->window + curr, (unsigned)init);
            s->high_water = curr + init;
        }
        else if (s->high_water < (uint64_t)curr + WIN_INIT) {
            /* High water mark at or above current data, but below current data
             * plus WIN_INIT -- zero out to current data plus WIN_INIT, or up
             * to end of window, whichever is less.
             */
            init = (uint64_t)curr + WIN_INIT - s->high_water;
            if (init > s->window_size - s->high_water)
                init = s->window_size - s->high_water;
            zmemzero(s->window + s->high_water, (unsigned)init);
            s->high_water += init;
        }
    }

    Assert((uint64_t)s->strstart <= s->window_size - MIN_LOOKAHEAD,
           "not enough room for search");
}

void *resolve_fill_window(void)
{
	unsigned int eax, ebx, ecx, edx;
	if (!__get_cpuid (1, &eax, &ebx, &ecx, &edx))
		return fill_window_default;
	/* We need SSE4.2 ISA support */
	if (!(ecx & bit_SSE4_2))
		return fill_window_default;
	return fill_window_sse42;
}

static void fill_window(deflate_state *) __attribute__ ((ifunc ("resolve_fill_window")));

#elif defined (__aarch64__)

static void fill_window_neon(s)
    deflate_state *s;
{
    register uint32_t n;
    uint32_t more;    /* Amount of free space at the end of the window. */
    uint32_t wsize = s->w_size;

    Assert(s->lookahead < MIN_LOOKAHEAD, "already enough lookahead");

    do {
        more = (unsigned)(s->window_size -(uint64_t)s->lookahead -(ulg)s->strstart);

        /* Deal with !@#$% 64K limit: */
        if (sizeof(int) <= 2) {
            if (more == 0 && s->strstart == 0 && s->lookahead == 0) {
                more = wsize;

            } else if (more == (unsigned)(-1)) {
                /* Very unlikely, but possible on 16 bit machine if
                 * strstart == 0 && lookahead == 1 (input done a byte at time)
                 */
                more--;
            }
        }

        /* If the window is almost full and there is insufficient lookahead,
         * move the upper half to the lower one to make room in the upper half.
         */

        if (s->strstart >= wsize+MAX_DIST(s)) {

            unsigned int i;
            zmemcpy(s->window, s->window+wsize, (unsigned)wsize);
            s->match_start -= wsize;
            s->strstart    -= wsize;
            s->block_start -= (int64_t) wsize;
            n = s->hash_size;
            uint16x8_t  W;
            uint16_t   *q ;
            W = vmovq_n_u16(wsize);
            q = (uint16_t*)s->head;

            for(i = 0; i < n/8; ++i) {
                vst1q_u16(q, vqsubq_u16(vld1q_u16(q), W));
                q+=8;
            }

            n = wsize;
            q = (uint16_t*)s->prev;

            for(i = 0; i < n/8; ++i) {
                vst1q_u16(q, vqsubq_u16(vld1q_u16(q), W));
                q+=8;
            }
            more += wsize;
        }
        if (s->strm->avail_in == 0) break;

        /* If there was no sliding:
         *    strstart <= WSIZE+MAX_DIST-1 && lookahead <= MIN_LOOKAHEAD - 1 &&
         *    more == window_size - lookahead - strstart
         * => more >= window_size - (MIN_LOOKAHEAD-1 + WSIZE + MAX_DIST-1)
         * => more >= window_size - 2*WSIZE + 2
         * In the BIG_MEM or MMAP case (not yet supported),
         *   window_size == input_size + MIN_LOOKAHEAD  &&
         *   strstart + s->lookahead <= input_size => more >= MIN_LOOKAHEAD.
         * Otherwise, window_size == 2*WSIZE so more >= 2.
         * If there was sliding, more >= WSIZE. So in all cases, more >= 2.
         */
        Assert(more >= 2, "more < 2");

        n = read_buf(s->strm, s->window + s->strstart + s->lookahead, more);
        s->lookahead += n;

        /* Initialize the hash value now that we have some input: */
        if (s->lookahead + s->insert >= ACTUAL_MIN_MATCH) {
            uint32_t str = s->strstart - s->insert;
            uint32_t ins_h = s->window[str];
            while (s->insert) {
                ins_h = hash_func(s, ins_h, &s->window[str]);
                s->prev[str & s->w_mask] = s->head[ins_h];
                s->head[ins_h] = (Pos)str;
                str++;
                s->insert--;
                if (s->lookahead + s->insert < ACTUAL_MIN_MATCH)
                    break;
            }
            s->ins_h = ins_h;
        }
        /* If the whole input has less than ACTUAL_MIN_MATCH bytes, ins_h is garbage,
         * but this is not important since only literal bytes will be emitted.
         */

    } while (s->lookahead < MIN_LOOKAHEAD && s->strm->avail_in != 0);

    /* If the WIN_INIT bytes after the end of the current data have never been
     * written, then zero those bytes in order to avoid memory check reports of
     * the use of uninitialized (or uninitialised as Julian writes) bytes by
     * the longest match routines.  Update the high water mark for the next
     * time through here.  WIN_INIT is set to MAX_MATCH since the longest match
     * routines allow scanning to strstart + MAX_MATCH, ignoring lookahead.
     */
    if (s->high_water < s->window_size) {
        uint64_t curr = s->strstart + (ulg)(s->lookahead);
        uint64_t init;

        if (s->high_water < curr) {
            /* Previous high water mark below current data -- zero WIN_INIT
             * bytes or up to end of window, whichever is less.
             */
            init = s->window_size - curr;
            if (init > WIN_INIT)
                init = WIN_INIT;
            zmemzero(s->window + curr, (unsigned)init);
            s->high_water = curr + init;
        }
        else if (s->high_water < (uint64_t)curr + WIN_INIT) {
            /* High water mark at or above current data, but below current data
             * plus WIN_INIT -- zero out to current data plus WIN_INIT, or up
             * to end of window, whichever is less.
             */
            init = (uint64_t)curr + WIN_INIT - s->high_water;
            if (init > s->window_size - s->high_water)
                init = s->window_size - s->high_water;
            zmemzero(s->window + s->high_water, (unsigned)init);
            s->high_water += init;
        }
    }

    Assert((uint64_t)s->strstart <= s->window_size - MIN_LOOKAHEAD,
           "not enough room for search");
}

void fill_window(deflate_state *s){
    return fill_window_neon(s);
}

#else

void fill_window(deflate_state *s){
    return fill_window_default(s);
}

#endif

/* ===========================================================================
 * Flush the current block, with given end-of-file flag.
 * IN assertion: strstart is set to the end of the current match.
 */
#define FLUSH_BLOCK_ONLY(s, last) { \
   _tr_flush_block(s, (s->block_start >= 0L ? \
                   (uint8_t *)&s->window[(uint64_t)s->block_start] : \
                   (uint8_t *)Z_NULL), \
                (uint64_t)((int64_t)s->strstart - s->block_start), \
                (last)); \
   s->block_start = s->strstart; \
   flush_pending(s->strm); \
   Tracev((stderr,"[FLUSH]")); \
}

/* Same but force premature exit if necessary. */
#define FLUSH_BLOCK(s, last) { \
   FLUSH_BLOCK_ONLY(s, last); \
   if (s->strm->avail_out == 0) return (last) ? finish_started : need_more; \
}

/* ===========================================================================
 * Copy without compression as much as possible from the input stream, return
 * the current block state.
 * This function does not insert new strings in the dictionary since
 * uncompressible data is probably not useful. This function is used
 * only for the level=0 compression option.
 * NOTE: this function should be optimized to avoid extra copying from
 * window to pending_buf.
 */
static block_state deflate_stored(s, flush)
    deflate_state *s;
    int flush;
{
    /* Stored blocks are limited to 0xffff bytes, pending_buf is limited
     * to pending_buf_size, and each stored block has a 5 byte header:
     */
    uint64_t max_block_size = 0xffff;
    uint64_t max_start;

    if (max_block_size > s->pending_buf_size - 5) {
        max_block_size = s->pending_buf_size - 5;
    }

    /* Copy as much as possible from input to output: */
    for (;;) {
        /* Fill the window as much as possible: */
        if (s->lookahead <= 1) {

            Assert(s->strstart < s->w_size+MAX_DIST(s) ||
                   s->block_start >= (int64_t)s->w_size, "slide too late");

            fill_window(s);
            if (s->lookahead == 0 && flush == Z_NO_FLUSH) return need_more;

            if (s->lookahead == 0) break; /* flush the current block */
        }
        Assert(s->block_start >= 0L, "block gone");

        s->strstart += s->lookahead;
        s->lookahead = 0;

        /* Emit a stored block if pending_buf will be full: */
        max_start = s->block_start + max_block_size;
        if (s->strstart == 0 || (uint64_t)s->strstart >= max_start) {
            /* strstart == 0 is possible when wraparound on 16-bit machine */
            s->lookahead = (uint32_t)(s->strstart - max_start);
            s->strstart = (uint32_t)max_start;
            FLUSH_BLOCK(s, 0);
        }
        /* Flush if we may have to slide, otherwise block_start may become
         * negative and the data will be gone:
         */
        if (s->strstart - (uint32_t)s->block_start >= MAX_DIST(s)) {
            FLUSH_BLOCK(s, 0);
        }
    }
    s->insert = 0;
    if (flush == Z_FINISH) {
        FLUSH_BLOCK(s, 1);
        return finish_done;
    }
    if ((int64_t)s->strstart > s->block_start)
        FLUSH_BLOCK(s, 0);
    return block_done;
}

/* ===========================================================================
 * Compress as much as possible from the input stream, return the current
 * block state.
 * This function does not perform lazy evaluation of matches and inserts
 * new strings in the dictionary only for unmatched strings or for short
 * matches. It is used only for the fast compression options.
 */
static block_state deflate_fast(s, flush)
    deflate_state *s;
    int flush;
{
    IPos hash_head;       /* head of the hash chain */
    int bflush;           /* set if current block must be flushed */

    for (;;) {
        /* Make sure that we always have enough lookahead, except
         * at the end of the input file. We need MAX_MATCH bytes
         * for the next match, plus ACTUAL_MIN_MATCH bytes to insert the
         * string following the next match.
         */
        if (s->lookahead < MIN_LOOKAHEAD) {
            fill_window(s);
            if (s->lookahead < MIN_LOOKAHEAD && flush == Z_NO_FLUSH) {
                return need_more;
            }
            if (s->lookahead == 0) break; /* flush the current block */
        }

        /* Insert the string window[strstart .. strstart+2] in the
         * dictionary, and set hash_head to the head of the hash chain:
         */
        hash_head = NIL;
        if (s->lookahead >= ACTUAL_MIN_MATCH) {
            hash_head = insert_string(s, s->strstart);
        }

        /* Find the longest match, discarding those <= prev_length.
         * At this point we have always match_length < ACTUAL_MIN_MATCH
         */
        if (hash_head != NIL && s->strstart - hash_head <= MAX_DIST(s)) {
            /* To simplify the code, we prevent matches with the string
             * of window index 0 (in particular we have to avoid a match
             * of the string with itself at the start of the input file).
             */
            s->match_length = longest_match (s, hash_head);
            /* longest_match() sets match_start */
        }
        if (s->match_length >= ACTUAL_MIN_MATCH) {
            check_match(s, s->strstart, s->match_start, s->match_length);

            bflush = _tr_tally_dist(s, s->strstart - s->match_start,
                           s->match_length - MIN_MATCH);

            s->lookahead -= s->match_length;

            /* Insert new strings in the hash table only if the match length
             * is not too large. This saves time but degrades compression.
             */
            if (s->match_length <= s->max_insert_length &&
                s->lookahead >= ACTUAL_MIN_MATCH) {
                s->match_length--; /* string at strstart already in table */
                do {
                    s->strstart++;
                    hash_head = insert_string(s, s->strstart);
                    /* strstart never exceeds WSIZE-MAX_MATCH, so there are
                     * always ACTUAL_MIN_MATCH bytes ahead.
                     */
                } while (--s->match_length != 0);
                s->strstart++;
            } else {
                s->strstart += s->match_length;
                s->match_length = 0;
                /* If lookahead < ACTUAL_MIN_MATCH, ins_h is garbage, but it does not
                 * matter since it will be recomputed at next deflate call.
                 */
            }
        } else {
            /* No match, output a literal byte */
            Tracevv((stderr,"%c", s->window[s->strstart]));
            bflush = _tr_tally_lit (s, s->window[s->strstart]);
            s->lookahead--;
            s->strstart++;
        }
        if (bflush) FLUSH_BLOCK(s, 0);
    }
    s->insert = s->strstart < ACTUAL_MIN_MATCH-1 ? s->strstart : ACTUAL_MIN_MATCH-1;
    if (flush == Z_FINISH) {
        FLUSH_BLOCK(s, 1);
        return finish_done;
    }
    if (s->last_lit)
        FLUSH_BLOCK(s, 0);
    return block_done;
}

/* ===========================================================================
 * Same as above, but achieves better compression. We use a lazy
 * evaluation for matches: a match is finally adopted only if there is
 * no better match at the next window position.
 */
static block_state deflate_slow(s, flush)
    deflate_state *s;
    int flush;
{
    IPos hash_head;          /* head of hash chain */
    int bflush;              /* set if current block must be flushed */

    /* Process the input block. */
    for (;;) {
        /* Make sure that we always have enough lookahead, except
         * at the end of the input file. We need MAX_MATCH bytes
         * for the next match, plus ACTUAL_MIN_MATCH bytes to insert the
         * string following the next match.
         */
        if (s->lookahead < MIN_LOOKAHEAD) {
            fill_window(s);
            if (s->lookahead < MIN_LOOKAHEAD && flush == Z_NO_FLUSH) {
                return need_more;
            }
            if (s->lookahead == 0) break; /* flush the current block */
        }

        /* Insert the string window[strstart .. strstart+3] in the
         * dictionary, and set hash_head to the head of the hash chain:
         */
        hash_head = NIL;
        if (s->lookahead >= ACTUAL_MIN_MATCH) {
            hash_head = insert_string(s, s->strstart);
        }

        /* Find the longest match, discarding those <= prev_length.
         */
        s->prev_length = s->match_length, s->prev_match = s->match_start;
        s->match_length = ACTUAL_MIN_MATCH-1;

        if (hash_head != NIL && s->prev_length < s->max_lazy_match &&
            s->strstart - hash_head <= MAX_DIST(s)) {
            /* To simplify the code, we prevent matches with the string
             * of window index 0 (in particular we have to avoid a match
             * of the string with itself at the start of the input file).
             */
            s->match_length = longest_match (s, hash_head);
            /* longest_match() sets match_start */

            if (s->match_length <= 5 && (s->strategy == Z_FILTERED )) {

                /* If prev_match is also ACTUAL_MIN_MATCH, match_start is garbage
                 * but we will ignore the current match anyway.
                 */
                s->match_length = ACTUAL_MIN_MATCH-1;
            }
        }
        /* If there was a match at the previous step and the current
         * match is not better, output the previous match:
         */
        if (s->prev_length >= ACTUAL_MIN_MATCH && s->match_length <= s->prev_length) {
            uint32_t mov_fwd ;
            uint32_t insert_cnt ;

            uint32_t max_insert = s->strstart + s->lookahead - ACTUAL_MIN_MATCH;
            /* Do not insert strings in hash table beyond this. */

            check_match(s, s->strstart-1, s->prev_match, s->prev_length);

            bflush = _tr_tally_dist(s, s->strstart -1 - s->prev_match,
                           s->prev_length - MIN_MATCH);

            /* Insert in hash table all strings up to the end of the match.
             * strstart-1 and strstart are already inserted. If there is not
             * enough lookahead, the last two strings are not inserted in
             * the hash table.
             */
            s->lookahead -= s->prev_length-1;

            mov_fwd = s->prev_length - 2;
            insert_cnt = mov_fwd;
            if (unlikely(insert_cnt > max_insert - s->strstart))
                insert_cnt = max_insert - s->strstart;

            bulk_insert_str(s, s->strstart + 1, insert_cnt);
            s->prev_length = 0;
            s->match_available = 0;
            s->match_length = ACTUAL_MIN_MATCH-1;
            s->strstart += mov_fwd + 1;

            if (bflush) FLUSH_BLOCK(s, 0);

        } else if (s->match_available) {
            /* If there was no match at the previous position, output a
             * single literal. If there was a match but the current match
             * is longer, truncate the previous match to a single literal.
             */
            Tracevv((stderr,"%c", s->window[s->strstart-1]));
            bflush = _tr_tally_lit(s, s->window[s->strstart-1]);
            if (bflush) {
                FLUSH_BLOCK_ONLY(s, 0);
            }
            s->strstart++;
            s->lookahead--;
            if (s->strm->avail_out == 0) return need_more;
        } else {
            /* There is no previous match to compare with, wait for
             * the next step to decide.
             */
            s->match_available = 1;
            s->strstart++;
            s->lookahead--;
        }
    }
    Assert (flush != Z_NO_FLUSH, "no flush?");
    if (s->match_available) {
        Tracevv((stderr,"%c", s->window[s->strstart-1]));
        bflush = _tr_tally_lit(s, s->window[s->strstart-1]);
        s->match_available = 0;
    }
    s->insert = s->strstart < ACTUAL_MIN_MATCH-1 ? s->strstart : ACTUAL_MIN_MATCH-1;
    if (flush == Z_FINISH) {
        FLUSH_BLOCK(s, 1);
        return finish_done;
    }
    if (s->last_lit)
        FLUSH_BLOCK(s, 0);
    return block_done;
}

/* ===========================================================================
 * For Z_RLE, simply look for runs of bytes, generate matches only of distance
 * one.  Do not maintain a hash table.  (It will be regenerated if this run of
 * deflate switches away from Z_RLE.)
 */
static block_state deflate_rle(s, flush)
    deflate_state *s;
    int flush;
{
    int bflush;                 /* set if current block must be flushed */
    uint32_t prev;              /* byte at distance one to match */
    uint8_t  *scan, *strend;    /* scan goes up to strend for length of run */

    for (;;) {
        /* Make sure that we always have enough lookahead, except
         * at the end of the input file. We need MAX_MATCH bytes
         * for the longest run, plus one for the unrolled loop.
         */
        if (s->lookahead <= MAX_MATCH) {
            fill_window(s);
            if (s->lookahead <= MAX_MATCH && flush == Z_NO_FLUSH) {
                return need_more;
            }
            if (s->lookahead == 0) break; /* flush the current block */
        }

        /* See how many times the previous byte repeats */
        s->match_length = 0;
        if (s->lookahead >= ACTUAL_MIN_MATCH && s->strstart > 0) {
            scan = s->window + s->strstart - 1;
            prev = *scan;
            if (prev == *++scan && prev == *++scan && prev == *++scan) {
                strend = s->window + s->strstart + MAX_MATCH;
                do {
                } while (prev == *++scan && prev == *++scan &&
                         prev == *++scan && prev == *++scan &&
                         prev == *++scan && prev == *++scan &&
                         prev == *++scan && prev == *++scan &&
                         scan < strend);
                s->match_length = MAX_MATCH - (int)(strend - scan);
                if (s->match_length > s->lookahead)
                    s->match_length = s->lookahead;
            }
            Assert(scan <= s->window+(uint32_t)(s->window_size-1), "wild scan");
        }

        /* Emit match if have run of ACTUAL_MIN_MATCH or longer, else emit literal */
        if (s->match_length >= ACTUAL_MIN_MATCH) {
            check_match(s, s->strstart, s->strstart - 1, s->match_length);

            bflush = _tr_tally_dist(s, 1, s->match_length - MIN_MATCH);

            s->lookahead -= s->match_length;
            s->strstart += s->match_length;
            s->match_length = 0;
        } else {
            /* No match, output a literal byte */
            Tracevv((stderr,"%c", s->window[s->strstart]));
            bflush = _tr_tally_lit (s, s->window[s->strstart]);
            s->lookahead--;
            s->strstart++;
        }
        if (bflush) FLUSH_BLOCK(s, 0);
    }
    s->insert = 0;
    if (flush == Z_FINISH) {
        FLUSH_BLOCK(s, 1);
        return finish_done;
    }
    if (s->last_lit)
        FLUSH_BLOCK(s, 0);
    return block_done;
}

/* ===========================================================================
 * For Z_HUFFMAN_ONLY, do not look for matches.  Do not maintain a hash table.
 * (It will be regenerated if this run of deflate switches away from Huffman.)
 */
static block_state deflate_huff(s, flush)
    deflate_state *s;
    int flush;
{
    int bflush;             /* set if current block must be flushed */

    for (;;) {
        /* Make sure that we have a literal to write. */
        if (s->lookahead == 0) {
            fill_window(s);
            if (s->lookahead == 0) {
                if (flush == Z_NO_FLUSH)
                    return need_more;
                break;      /* flush the current block */
            }
        }

        /* Output a literal byte */
        s->match_length = 0;
        Tracevv((stderr,"%c", s->window[s->strstart]));
        bflush = _tr_tally_lit (s, s->window[s->strstart]);
        s->lookahead--;
        s->strstart++;
        if (bflush) FLUSH_BLOCK(s, 0);
    }
    s->insert = 0;
    if (flush == Z_FINISH) {
        FLUSH_BLOCK(s, 1);
        return finish_done;
    }
    if (s->last_lit)
        FLUSH_BLOCK(s, 0);
    return block_done;
}

#define PREFIX3(x) z_ ## x
#define END_BLOCK 256

extern void flush_pending(PREFIX3(stream) *strm);

static inline long compare258(const unsigned char *const src0, const unsigned char *const src1) {
#ifdef _MSC_VER
    long cnt;

    cnt = 0;
    do {
#define mode  _SIDD_UBYTE_OPS | _SIDD_CMP_EQUAL_EACH | _SIDD_NEGATIVE_POLARITY

        int ret;
        __m128i xmm_src0, xmm_src1;

        xmm_src0 = _mm_loadu_si128((__m128i *)(src0 + cnt));
        xmm_src1 = _mm_loadu_si128((__m128i *)(src1 + cnt));
        ret = _mm_cmpestri(xmm_src0, 16, xmm_src1, 16, mode);
        if (_mm_cmpestrc(xmm_src0, 16, xmm_src1, 16, mode)) {
            cnt += ret;
        break;
        }
        cnt += 16;

        xmm_src0 = _mm_loadu_si128((__m128i *)(src0 + cnt));
        xmm_src1 = _mm_loadu_si128((__m128i *)(src1 + cnt));
        ret = _mm_cmpestri(xmm_src0, 16, xmm_src1, 16, mode);
        if (_mm_cmpestrc(xmm_src0, 16, xmm_src1, 16, mode)) {
            cnt += ret;
        break;
        }
        cnt += 16;
    } while (cnt < 256);

    if (*(unsigned short *)(src0 + cnt) == *(unsigned short *)(src1 + cnt)) {
        cnt += 2;
    } else if (*(src0 + cnt) == *(src1 + cnt)) {
        cnt++;
    }
    return cnt;
#else
    uintptr_t ax, dx, cx;
    __m128i xmm_src0;

    ax = 16;
    dx = 16;
    /* set cx to something, otherwise gcc thinks it's used
       uninitalised */
    cx = 0;

    __asm__ __volatile__ (
    "1:"
        "movdqu     -16(%[src0], %[ax]), %[xmm_src0]\n\t"
        "pcmpestri  $0x18, -16(%[src1], %[ax]), %[xmm_src0]\n\t"
        "jc         2f\n\t"
        "add        $16, %[ax]\n\t"

        "movdqu     -16(%[src0], %[ax]), %[xmm_src0]\n\t"
        "pcmpestri  $0x18, -16(%[src1], %[ax]), %[xmm_src0]\n\t"
        "jc         2f\n\t"
        "add        $16, %[ax]\n\t"

        "cmp        $256 + 16, %[ax]\n\t"
        "jb         1b\n\t"

#ifdef X86
        "movzwl     -16(%[src0], %[ax]), %[dx]\n\t"
#else
        "movzwq     -16(%[src0], %[ax]), %[dx]\n\t"
#endif
        "xorw       -16(%[src1], %[ax]), %%dx\n\t"
        "jnz        3f\n\t"

        "add        $2, %[ax]\n\t"
        "jmp        4f\n\t"
    "3:\n\t"
        "rep; bsf   %[dx], %[cx]\n\t"
        "shr        $3, %[cx]\n\t"
    "2:"
        "add        %[cx], %[ax]\n\t"
    "4:"
    : [ax] "+a" (ax),
      [cx] "+c" (cx),
      [dx] "+d" (dx),
      [xmm_src0] "=x" (xmm_src0)
    : [src0] "r" (src0),
      [src1] "r" (src1)
    : "cc"
    );
    return ax - 16;
#endif
}

static const unsigned quick_len_codes[MAX_MATCH-MIN_MATCH+1];
static const unsigned quick_dist_codes[8192];

static inline void quick_send_bits(deflate_state *const s, const int value, const int length) {
    unsigned out, width, bytes_out;

    /* Concatenate the new bits with the bits currently in the buffer */
    out = s->bi_buf | (value << s->bi_valid);
    width = s->bi_valid + length;

    /* Taking advantage of the fact that LSB comes first, write to output buffer */
    *(unsigned *)(s->pending_buf + s->pending) = out;

    bytes_out = width / 8;

    s->pending += bytes_out;

    /* Shift out the valid LSBs written out */
    s->bi_buf =  out >> (bytes_out * 8);
    s->bi_valid = width - (bytes_out * 8);
}

static inline void static_emit_ptr(deflate_state *const s, const int lc, const unsigned dist) {
    unsigned code, len;

    code = quick_len_codes[lc] >> 8;
    len =  quick_len_codes[lc] & 0xFF;
    quick_send_bits(s, code, len);

    code = quick_dist_codes[dist-1] >> 8;
    len  = quick_dist_codes[dist-1] & 0xFF;
    quick_send_bits(s, code, len);
}

static inline void static_emit_lit(deflate_state *const s, const int lit) {
    quick_send_bits(s, static_ltree[lit].Code, static_ltree[lit].Len);
    Tracecv(isgraph(lit), (stderr, " '%c' ", lit));
}

static void static_emit_tree(deflate_state *const s, const int flush) {
    unsigned last;

    last = flush == Z_FINISH ? 1 : 0;
    Tracev((stderr, "\n--- Emit Tree: Last: %u\n", last));
    send_bits(s, (STATIC_TREES << 1)+ last, 3);
}

static void static_emit_end_block(deflate_state *const s, int last) {
    send_code(s, END_BLOCK, static_ltree);
    Tracev((stderr, "\n+++ Emit End Block: Last: %u Pending: %u Total Out: %u\n", last, s->pending, s->strm->total_out));

    if (last)
        bi_windup(s);

    s->block_start = s->strstart;
    flush_pending(s->strm);
    s->block_open = 0;
}

static inline Pos quick_insert_string(deflate_state *const s, const Pos str) {
    Pos ret;
    unsigned h = 0;

#ifdef _MSC_VER
    h = _mm_crc32_u32(h, *(unsigned *)(s->window + str));
#else
    __asm__ __volatile__ (
        "crc32l (%[window], %[str], 1), %0\n\t"
    : "+r" (h)
    : [window] "r" (s->window),
      [str] "r" ((uintptr_t)str)
    );
#endif

    ret = s->head[h & s->hash_mask];
    s->head[h & s->hash_mask] = str;
    return ret;
}

ZLIB_INTERNAL block_state deflate_quick(deflate_state *s, int flush) {
    IPos hash_head;
    unsigned dist, match_len;

    if (s->block_open == 0) {
        static_emit_tree(s, flush);
        s->block_open = 1;
    }

    do {
        if (s->pending + 4 >= s->pending_buf_size) {
            flush_pending(s->strm);
            return need_more;
        }

        if (s->lookahead < MIN_LOOKAHEAD) {
            fill_window_sse42(s);
            if (s->lookahead < MIN_LOOKAHEAD && flush == Z_NO_FLUSH) {
                static_emit_end_block(s, 0);
                return need_more;
            }
            if (s->lookahead == 0)
                break;
        }

        if (s->lookahead >= MIN_MATCH) {
            hash_head = quick_insert_string(s, s->strstart);
            dist = s->strstart - hash_head;

            if ((dist-1) < (s->w_size - 1)) {
                match_len = compare258(s->window + s->strstart, s->window + s->strstart - dist);

                if (match_len >= MIN_MATCH) {
                    if (match_len > s->lookahead)
                        match_len = s->lookahead;

                    if (match_len > MAX_MATCH)
                        match_len = MAX_MATCH;

                    static_emit_ptr(s, match_len - MIN_MATCH, s->strstart - hash_head);
                    s->lookahead -= match_len;
                    s->strstart += match_len;
                    continue;
                }
            }
        }

        static_emit_lit(s, s->window[s->strstart]);
        s->strstart++;
        s->lookahead--;
    } while (s->strm->avail_out != 0);

    if (s->strm->avail_out == 0 && flush != Z_FINISH)
        return need_more;

    s->insert = s->strstart < MIN_MATCH - 1 ? s->strstart : MIN_MATCH-1;
    if (flush == Z_FINISH) {
        static_emit_end_block(s, 1);
        if (s->strm->avail_out == 0)
            return s->strm->avail_in == 0 ? finish_started : need_more;
        else
            return finish_done;
    }

    static_emit_end_block(s, 0);
    return block_done;
}

static const unsigned quick_len_codes[MAX_MATCH-MIN_MATCH+1] = {
    0x00004007, 0x00002007, 0x00006007, 0x00001007,
    0x00005007, 0x00003007, 0x00007007, 0x00000807,
    0x00004808, 0x0000c808, 0x00002808, 0x0000a808,
    0x00006808, 0x0000e808, 0x00001808, 0x00009808,
    0x00005809, 0x0000d809, 0x00015809, 0x0001d809,
    0x00003809, 0x0000b809, 0x00013809, 0x0001b809,
    0x00007809, 0x0000f809, 0x00017809, 0x0001f809,
    0x00000409, 0x00008409, 0x00010409, 0x00018409,
    0x0000440a, 0x0000c40a, 0x0001440a, 0x0001c40a,
    0x0002440a, 0x0002c40a, 0x0003440a, 0x0003c40a,
    0x0000240a, 0x0000a40a, 0x0001240a, 0x0001a40a,
    0x0002240a, 0x0002a40a, 0x0003240a, 0x0003a40a,
    0x0000640a, 0x0000e40a, 0x0001640a, 0x0001e40a,
    0x0002640a, 0x0002e40a, 0x0003640a, 0x0003e40a,
    0x0000140a, 0x0000940a, 0x0001140a, 0x0001940a,
    0x0002140a, 0x0002940a, 0x0003140a, 0x0003940a,
    0x0000540b, 0x0000d40b, 0x0001540b, 0x0001d40b,
    0x0002540b, 0x0002d40b, 0x0003540b, 0x0003d40b,
    0x0004540b, 0x0004d40b, 0x0005540b, 0x0005d40b,
    0x0006540b, 0x0006d40b, 0x0007540b, 0x0007d40b,
    0x0000340b, 0x0000b40b, 0x0001340b, 0x0001b40b,
    0x0002340b, 0x0002b40b, 0x0003340b, 0x0003b40b,
    0x0004340b, 0x0004b40b, 0x0005340b, 0x0005b40b,
    0x0006340b, 0x0006b40b, 0x0007340b, 0x0007b40b,
    0x0000740b, 0x0000f40b, 0x0001740b, 0x0001f40b,
    0x0002740b, 0x0002f40b, 0x0003740b, 0x0003f40b,
    0x0004740b, 0x0004f40b, 0x0005740b, 0x0005f40b,
    0x0006740b, 0x0006f40b, 0x0007740b, 0x0007f40b,
    0x0000030c, 0x0001030c, 0x0002030c, 0x0003030c,
    0x0004030c, 0x0005030c, 0x0006030c, 0x0007030c,
    0x0008030c, 0x0009030c, 0x000a030c, 0x000b030c,
    0x000c030c, 0x000d030c, 0x000e030c, 0x000f030c,
    0x0000830d, 0x0001830d, 0x0002830d, 0x0003830d,
    0x0004830d, 0x0005830d, 0x0006830d, 0x0007830d,
    0x0008830d, 0x0009830d, 0x000a830d, 0x000b830d,
    0x000c830d, 0x000d830d, 0x000e830d, 0x000f830d,
    0x0010830d, 0x0011830d, 0x0012830d, 0x0013830d,
    0x0014830d, 0x0015830d, 0x0016830d, 0x0017830d,
    0x0018830d, 0x0019830d, 0x001a830d, 0x001b830d,
    0x001c830d, 0x001d830d, 0x001e830d, 0x001f830d,
    0x0000430d, 0x0001430d, 0x0002430d, 0x0003430d,
    0x0004430d, 0x0005430d, 0x0006430d, 0x0007430d,
    0x0008430d, 0x0009430d, 0x000a430d, 0x000b430d,
    0x000c430d, 0x000d430d, 0x000e430d, 0x000f430d,
    0x0010430d, 0x0011430d, 0x0012430d, 0x0013430d,
    0x0014430d, 0x0015430d, 0x0016430d, 0x0017430d,
    0x0018430d, 0x0019430d, 0x001a430d, 0x001b430d,
    0x001c430d, 0x001d430d, 0x001e430d, 0x001f430d,
    0x0000c30d, 0x0001c30d, 0x0002c30d, 0x0003c30d,
    0x0004c30d, 0x0005c30d, 0x0006c30d, 0x0007c30d,
    0x0008c30d, 0x0009c30d, 0x000ac30d, 0x000bc30d,
    0x000cc30d, 0x000dc30d, 0x000ec30d, 0x000fc30d,
    0x0010c30d, 0x0011c30d, 0x0012c30d, 0x0013c30d,
    0x0014c30d, 0x0015c30d, 0x0016c30d, 0x0017c30d,
    0x0018c30d, 0x0019c30d, 0x001ac30d, 0x001bc30d,
    0x001cc30d, 0x001dc30d, 0x001ec30d, 0x001fc30d,
    0x0000230d, 0x0001230d, 0x0002230d, 0x0003230d,
    0x0004230d, 0x0005230d, 0x0006230d, 0x0007230d,
    0x0008230d, 0x0009230d, 0x000a230d, 0x000b230d,
    0x000c230d, 0x000d230d, 0x000e230d, 0x000f230d,
    0x0010230d, 0x0011230d, 0x0012230d, 0x0013230d,
    0x0014230d, 0x0015230d, 0x0016230d, 0x0017230d,
    0x0018230d, 0x0019230d, 0x001a230d, 0x001b230d,
    0x001c230d, 0x001d230d, 0x001e230d, 0x0000a308,
};

static const unsigned quick_dist_codes[8192] = {
    0x00000005, 0x00001005, 0x00000805, 0x00001805,
    0x00000406, 0x00002406, 0x00001406, 0x00003406,
    0x00000c07, 0x00002c07, 0x00004c07, 0x00006c07,
    0x00001c07, 0x00003c07, 0x00005c07, 0x00007c07,
    0x00000208, 0x00002208, 0x00004208, 0x00006208,
    0x00008208, 0x0000a208, 0x0000c208, 0x0000e208,
    0x00001208, 0x00003208, 0x00005208, 0x00007208,
    0x00009208, 0x0000b208, 0x0000d208, 0x0000f208,
    0x00000a09, 0x00002a09, 0x00004a09, 0x00006a09,
    0x00008a09, 0x0000aa09, 0x0000ca09, 0x0000ea09,
    0x00010a09, 0x00012a09, 0x00014a09, 0x00016a09,
    0x00018a09, 0x0001aa09, 0x0001ca09, 0x0001ea09,
    0x00001a09, 0x00003a09, 0x00005a09, 0x00007a09,
    0x00009a09, 0x0000ba09, 0x0000da09, 0x0000fa09,
    0x00011a09, 0x00013a09, 0x00015a09, 0x00017a09,
    0x00019a09, 0x0001ba09, 0x0001da09, 0x0001fa09,
    0x0000060a, 0x0000260a, 0x0000460a, 0x0000660a,
    0x0000860a, 0x0000a60a, 0x0000c60a, 0x0000e60a,
    0x0001060a, 0x0001260a, 0x0001460a, 0x0001660a,
    0x0001860a, 0x0001a60a, 0x0001c60a, 0x0001e60a,
    0x0002060a, 0x0002260a, 0x0002460a, 0x0002660a,
    0x0002860a, 0x0002a60a, 0x0002c60a, 0x0002e60a,
    0x0003060a, 0x0003260a, 0x0003460a, 0x0003660a,
    0x0003860a, 0x0003a60a, 0x0003c60a, 0x0003e60a,
    0x0000160a, 0x0000360a, 0x0000560a, 0x0000760a,
    0x0000960a, 0x0000b60a, 0x0000d60a, 0x0000f60a,
    0x0001160a, 0x0001360a, 0x0001560a, 0x0001760a,
    0x0001960a, 0x0001b60a, 0x0001d60a, 0x0001f60a,
    0x0002160a, 0x0002360a, 0x0002560a, 0x0002760a,
    0x0002960a, 0x0002b60a, 0x0002d60a, 0x0002f60a,
    0x0003160a, 0x0003360a, 0x0003560a, 0x0003760a,
    0x0003960a, 0x0003b60a, 0x0003d60a, 0x0003f60a,
    0x00000e0b, 0x00002e0b, 0x00004e0b, 0x00006e0b,
    0x00008e0b, 0x0000ae0b, 0x0000ce0b, 0x0000ee0b,
    0x00010e0b, 0x00012e0b, 0x00014e0b, 0x00016e0b,
    0x00018e0b, 0x0001ae0b, 0x0001ce0b, 0x0001ee0b,
    0x00020e0b, 0x00022e0b, 0x00024e0b, 0x00026e0b,
    0x00028e0b, 0x0002ae0b, 0x0002ce0b, 0x0002ee0b,
    0x00030e0b, 0x00032e0b, 0x00034e0b, 0x00036e0b,
    0x00038e0b, 0x0003ae0b, 0x0003ce0b, 0x0003ee0b,
    0x00040e0b, 0x00042e0b, 0x00044e0b, 0x00046e0b,
    0x00048e0b, 0x0004ae0b, 0x0004ce0b, 0x0004ee0b,
    0x00050e0b, 0x00052e0b, 0x00054e0b, 0x00056e0b,
    0x00058e0b, 0x0005ae0b, 0x0005ce0b, 0x0005ee0b,
    0x00060e0b, 0x00062e0b, 0x00064e0b, 0x00066e0b,
    0x00068e0b, 0x0006ae0b, 0x0006ce0b, 0x0006ee0b,
    0x00070e0b, 0x00072e0b, 0x00074e0b, 0x00076e0b,
    0x00078e0b, 0x0007ae0b, 0x0007ce0b, 0x0007ee0b,
    0x00001e0b, 0x00003e0b, 0x00005e0b, 0x00007e0b,
    0x00009e0b, 0x0000be0b, 0x0000de0b, 0x0000fe0b,
    0x00011e0b, 0x00013e0b, 0x00015e0b, 0x00017e0b,
    0x00019e0b, 0x0001be0b, 0x0001de0b, 0x0001fe0b,
    0x00021e0b, 0x00023e0b, 0x00025e0b, 0x00027e0b,
    0x00029e0b, 0x0002be0b, 0x0002de0b, 0x0002fe0b,
    0x00031e0b, 0x00033e0b, 0x00035e0b, 0x00037e0b,
    0x00039e0b, 0x0003be0b, 0x0003de0b, 0x0003fe0b,
    0x00041e0b, 0x00043e0b, 0x00045e0b, 0x00047e0b,
    0x00049e0b, 0x0004be0b, 0x0004de0b, 0x0004fe0b,
    0x00051e0b, 0x00053e0b, 0x00055e0b, 0x00057e0b,
    0x00059e0b, 0x0005be0b, 0x0005de0b, 0x0005fe0b,
    0x00061e0b, 0x00063e0b, 0x00065e0b, 0x00067e0b,
    0x00069e0b, 0x0006be0b, 0x0006de0b, 0x0006fe0b,
    0x00071e0b, 0x00073e0b, 0x00075e0b, 0x00077e0b,
    0x00079e0b, 0x0007be0b, 0x0007de0b, 0x0007fe0b,
    0x0000010c, 0x0000210c, 0x0000410c, 0x0000610c,
    0x0000810c, 0x0000a10c, 0x0000c10c, 0x0000e10c,
    0x0001010c, 0x0001210c, 0x0001410c, 0x0001610c,
    0x0001810c, 0x0001a10c, 0x0001c10c, 0x0001e10c,
    0x0002010c, 0x0002210c, 0x0002410c, 0x0002610c,
    0x0002810c, 0x0002a10c, 0x0002c10c, 0x0002e10c,
    0x0003010c, 0x0003210c, 0x0003410c, 0x0003610c,
    0x0003810c, 0x0003a10c, 0x0003c10c, 0x0003e10c,
    0x0004010c, 0x0004210c, 0x0004410c, 0x0004610c,
    0x0004810c, 0x0004a10c, 0x0004c10c, 0x0004e10c,
    0x0005010c, 0x0005210c, 0x0005410c, 0x0005610c,
    0x0005810c, 0x0005a10c, 0x0005c10c, 0x0005e10c,
    0x0006010c, 0x0006210c, 0x0006410c, 0x0006610c,
    0x0006810c, 0x0006a10c, 0x0006c10c, 0x0006e10c,
    0x0007010c, 0x0007210c, 0x0007410c, 0x0007610c,
    0x0007810c, 0x0007a10c, 0x0007c10c, 0x0007e10c,
    0x0008010c, 0x0008210c, 0x0008410c, 0x0008610c,
    0x0008810c, 0x0008a10c, 0x0008c10c, 0x0008e10c,
    0x0009010c, 0x0009210c, 0x0009410c, 0x0009610c,
    0x0009810c, 0x0009a10c, 0x0009c10c, 0x0009e10c,
    0x000a010c, 0x000a210c, 0x000a410c, 0x000a610c,
    0x000a810c, 0x000aa10c, 0x000ac10c, 0x000ae10c,
    0x000b010c, 0x000b210c, 0x000b410c, 0x000b610c,
    0x000b810c, 0x000ba10c, 0x000bc10c, 0x000be10c,
    0x000c010c, 0x000c210c, 0x000c410c, 0x000c610c,
    0x000c810c, 0x000ca10c, 0x000cc10c, 0x000ce10c,
    0x000d010c, 0x000d210c, 0x000d410c, 0x000d610c,
    0x000d810c, 0x000da10c, 0x000dc10c, 0x000de10c,
    0x000e010c, 0x000e210c, 0x000e410c, 0x000e610c,
    0x000e810c, 0x000ea10c, 0x000ec10c, 0x000ee10c,
    0x000f010c, 0x000f210c, 0x000f410c, 0x000f610c,
    0x000f810c, 0x000fa10c, 0x000fc10c, 0x000fe10c,
    0x0000110c, 0x0000310c, 0x0000510c, 0x0000710c,
    0x0000910c, 0x0000b10c, 0x0000d10c, 0x0000f10c,
    0x0001110c, 0x0001310c, 0x0001510c, 0x0001710c,
    0x0001910c, 0x0001b10c, 0x0001d10c, 0x0001f10c,
    0x0002110c, 0x0002310c, 0x0002510c, 0x0002710c,
    0x0002910c, 0x0002b10c, 0x0002d10c, 0x0002f10c,
    0x0003110c, 0x0003310c, 0x0003510c, 0x0003710c,
    0x0003910c, 0x0003b10c, 0x0003d10c, 0x0003f10c,
    0x0004110c, 0x0004310c, 0x0004510c, 0x0004710c,
    0x0004910c, 0x0004b10c, 0x0004d10c, 0x0004f10c,
    0x0005110c, 0x0005310c, 0x0005510c, 0x0005710c,
    0x0005910c, 0x0005b10c, 0x0005d10c, 0x0005f10c,
    0x0006110c, 0x0006310c, 0x0006510c, 0x0006710c,
    0x0006910c, 0x0006b10c, 0x0006d10c, 0x0006f10c,
    0x0007110c, 0x0007310c, 0x0007510c, 0x0007710c,
    0x0007910c, 0x0007b10c, 0x0007d10c, 0x0007f10c,
    0x0008110c, 0x0008310c, 0x0008510c, 0x0008710c,
    0x0008910c, 0x0008b10c, 0x0008d10c, 0x0008f10c,
    0x0009110c, 0x0009310c, 0x0009510c, 0x0009710c,
    0x0009910c, 0x0009b10c, 0x0009d10c, 0x0009f10c,
    0x000a110c, 0x000a310c, 0x000a510c, 0x000a710c,
    0x000a910c, 0x000ab10c, 0x000ad10c, 0x000af10c,
    0x000b110c, 0x000b310c, 0x000b510c, 0x000b710c,
    0x000b910c, 0x000bb10c, 0x000bd10c, 0x000bf10c,
    0x000c110c, 0x000c310c, 0x000c510c, 0x000c710c,
    0x000c910c, 0x000cb10c, 0x000cd10c, 0x000cf10c,
    0x000d110c, 0x000d310c, 0x000d510c, 0x000d710c,
    0x000d910c, 0x000db10c, 0x000dd10c, 0x000df10c,
    0x000e110c, 0x000e310c, 0x000e510c, 0x000e710c,
    0x000e910c, 0x000eb10c, 0x000ed10c, 0x000ef10c,
    0x000f110c, 0x000f310c, 0x000f510c, 0x000f710c,
    0x000f910c, 0x000fb10c, 0x000fd10c, 0x000ff10c,
    0x0000090d, 0x0000290d, 0x0000490d, 0x0000690d,
    0x0000890d, 0x0000a90d, 0x0000c90d, 0x0000e90d,
    0x0001090d, 0x0001290d, 0x0001490d, 0x0001690d,
    0x0001890d, 0x0001a90d, 0x0001c90d, 0x0001e90d,
    0x0002090d, 0x0002290d, 0x0002490d, 0x0002690d,
    0x0002890d, 0x0002a90d, 0x0002c90d, 0x0002e90d,
    0x0003090d, 0x0003290d, 0x0003490d, 0x0003690d,
    0x0003890d, 0x0003a90d, 0x0003c90d, 0x0003e90d,
    0x0004090d, 0x0004290d, 0x0004490d, 0x0004690d,
    0x0004890d, 0x0004a90d, 0x0004c90d, 0x0004e90d,
    0x0005090d, 0x0005290d, 0x0005490d, 0x0005690d,
    0x0005890d, 0x0005a90d, 0x0005c90d, 0x0005e90d,
    0x0006090d, 0x0006290d, 0x0006490d, 0x0006690d,
    0x0006890d, 0x0006a90d, 0x0006c90d, 0x0006e90d,
    0x0007090d, 0x0007290d, 0x0007490d, 0x0007690d,
    0x0007890d, 0x0007a90d, 0x0007c90d, 0x0007e90d,
    0x0008090d, 0x0008290d, 0x0008490d, 0x0008690d,
    0x0008890d, 0x0008a90d, 0x0008c90d, 0x0008e90d,
    0x0009090d, 0x0009290d, 0x0009490d, 0x0009690d,
    0x0009890d, 0x0009a90d, 0x0009c90d, 0x0009e90d,
    0x000a090d, 0x000a290d, 0x000a490d, 0x000a690d,
    0x000a890d, 0x000aa90d, 0x000ac90d, 0x000ae90d,
    0x000b090d, 0x000b290d, 0x000b490d, 0x000b690d,
    0x000b890d, 0x000ba90d, 0x000bc90d, 0x000be90d,
    0x000c090d, 0x000c290d, 0x000c490d, 0x000c690d,
    0x000c890d, 0x000ca90d, 0x000cc90d, 0x000ce90d,
    0x000d090d, 0x000d290d, 0x000d490d, 0x000d690d,
    0x000d890d, 0x000da90d, 0x000dc90d, 0x000de90d,
    0x000e090d, 0x000e290d, 0x000e490d, 0x000e690d,
    0x000e890d, 0x000ea90d, 0x000ec90d, 0x000ee90d,
    0x000f090d, 0x000f290d, 0x000f490d, 0x000f690d,
    0x000f890d, 0x000fa90d, 0x000fc90d, 0x000fe90d,
    0x0010090d, 0x0010290d, 0x0010490d, 0x0010690d,
    0x0010890d, 0x0010a90d, 0x0010c90d, 0x0010e90d,
    0x0011090d, 0x0011290d, 0x0011490d, 0x0011690d,
    0x0011890d, 0x0011a90d, 0x0011c90d, 0x0011e90d,
    0x0012090d, 0x0012290d, 0x0012490d, 0x0012690d,
    0x0012890d, 0x0012a90d, 0x0012c90d, 0x0012e90d,
    0x0013090d, 0x0013290d, 0x0013490d, 0x0013690d,
    0x0013890d, 0x0013a90d, 0x0013c90d, 0x0013e90d,
    0x0014090d, 0x0014290d, 0x0014490d, 0x0014690d,
    0x0014890d, 0x0014a90d, 0x0014c90d, 0x0014e90d,
    0x0015090d, 0x0015290d, 0x0015490d, 0x0015690d,
    0x0015890d, 0x0015a90d, 0x0015c90d, 0x0015e90d,
    0x0016090d, 0x0016290d, 0x0016490d, 0x0016690d,
    0x0016890d, 0x0016a90d, 0x0016c90d, 0x0016e90d,
    0x0017090d, 0x0017290d, 0x0017490d, 0x0017690d,
    0x0017890d, 0x0017a90d, 0x0017c90d, 0x0017e90d,
    0x0018090d, 0x0018290d, 0x0018490d, 0x0018690d,
    0x0018890d, 0x0018a90d, 0x0018c90d, 0x0018e90d,
    0x0019090d, 0x0019290d, 0x0019490d, 0x0019690d,
    0x0019890d, 0x0019a90d, 0x0019c90d, 0x0019e90d,
    0x001a090d, 0x001a290d, 0x001a490d, 0x001a690d,
    0x001a890d, 0x001aa90d, 0x001ac90d, 0x001ae90d,
    0x001b090d, 0x001b290d, 0x001b490d, 0x001b690d,
    0x001b890d, 0x001ba90d, 0x001bc90d, 0x001be90d,
    0x001c090d, 0x001c290d, 0x001c490d, 0x001c690d,
    0x001c890d, 0x001ca90d, 0x001cc90d, 0x001ce90d,
    0x001d090d, 0x001d290d, 0x001d490d, 0x001d690d,
    0x001d890d, 0x001da90d, 0x001dc90d, 0x001de90d,
    0x001e090d, 0x001e290d, 0x001e490d, 0x001e690d,
    0x001e890d, 0x001ea90d, 0x001ec90d, 0x001ee90d,
    0x001f090d, 0x001f290d, 0x001f490d, 0x001f690d,
    0x001f890d, 0x001fa90d, 0x001fc90d, 0x001fe90d,
    0x0000190d, 0x0000390d, 0x0000590d, 0x0000790d,
    0x0000990d, 0x0000b90d, 0x0000d90d, 0x0000f90d,
    0x0001190d, 0x0001390d, 0x0001590d, 0x0001790d,
    0x0001990d, 0x0001b90d, 0x0001d90d, 0x0001f90d,
    0x0002190d, 0x0002390d, 0x0002590d, 0x0002790d,
    0x0002990d, 0x0002b90d, 0x0002d90d, 0x0002f90d,
    0x0003190d, 0x0003390d, 0x0003590d, 0x0003790d,
    0x0003990d, 0x0003b90d, 0x0003d90d, 0x0003f90d,
    0x0004190d, 0x0004390d, 0x0004590d, 0x0004790d,
    0x0004990d, 0x0004b90d, 0x0004d90d, 0x0004f90d,
    0x0005190d, 0x0005390d, 0x0005590d, 0x0005790d,
    0x0005990d, 0x0005b90d, 0x0005d90d, 0x0005f90d,
    0x0006190d, 0x0006390d, 0x0006590d, 0x0006790d,
    0x0006990d, 0x0006b90d, 0x0006d90d, 0x0006f90d,
    0x0007190d, 0x0007390d, 0x0007590d, 0x0007790d,
    0x0007990d, 0x0007b90d, 0x0007d90d, 0x0007f90d,
    0x0008190d, 0x0008390d, 0x0008590d, 0x0008790d,
    0x0008990d, 0x0008b90d, 0x0008d90d, 0x0008f90d,
    0x0009190d, 0x0009390d, 0x0009590d, 0x0009790d,
    0x0009990d, 0x0009b90d, 0x0009d90d, 0x0009f90d,
    0x000a190d, 0x000a390d, 0x000a590d, 0x000a790d,
    0x000a990d, 0x000ab90d, 0x000ad90d, 0x000af90d,
    0x000b190d, 0x000b390d, 0x000b590d, 0x000b790d,
    0x000b990d, 0x000bb90d, 0x000bd90d, 0x000bf90d,
    0x000c190d, 0x000c390d, 0x000c590d, 0x000c790d,
    0x000c990d, 0x000cb90d, 0x000cd90d, 0x000cf90d,
    0x000d190d, 0x000d390d, 0x000d590d, 0x000d790d,
    0x000d990d, 0x000db90d, 0x000dd90d, 0x000df90d,
    0x000e190d, 0x000e390d, 0x000e590d, 0x000e790d,
    0x000e990d, 0x000eb90d, 0x000ed90d, 0x000ef90d,
    0x000f190d, 0x000f390d, 0x000f590d, 0x000f790d,
    0x000f990d, 0x000fb90d, 0x000fd90d, 0x000ff90d,
    0x0010190d, 0x0010390d, 0x0010590d, 0x0010790d,
    0x0010990d, 0x0010b90d, 0x0010d90d, 0x0010f90d,
    0x0011190d, 0x0011390d, 0x0011590d, 0x0011790d,
    0x0011990d, 0x0011b90d, 0x0011d90d, 0x0011f90d,
    0x0012190d, 0x0012390d, 0x0012590d, 0x0012790d,
    0x0012990d, 0x0012b90d, 0x0012d90d, 0x0012f90d,
    0x0013190d, 0x0013390d, 0x0013590d, 0x0013790d,
    0x0013990d, 0x0013b90d, 0x0013d90d, 0x0013f90d,
    0x0014190d, 0x0014390d, 0x0014590d, 0x0014790d,
    0x0014990d, 0x0014b90d, 0x0014d90d, 0x0014f90d,
    0x0015190d, 0x0015390d, 0x0015590d, 0x0015790d,
    0x0015990d, 0x0015b90d, 0x0015d90d, 0x0015f90d,
    0x0016190d, 0x0016390d, 0x0016590d, 0x0016790d,
    0x0016990d, 0x0016b90d, 0x0016d90d, 0x0016f90d,
    0x0017190d, 0x0017390d, 0x0017590d, 0x0017790d,
    0x0017990d, 0x0017b90d, 0x0017d90d, 0x0017f90d,
    0x0018190d, 0x0018390d, 0x0018590d, 0x0018790d,
    0x0018990d, 0x0018b90d, 0x0018d90d, 0x0018f90d,
    0x0019190d, 0x0019390d, 0x0019590d, 0x0019790d,
    0x0019990d, 0x0019b90d, 0x0019d90d, 0x0019f90d,
    0x001a190d, 0x001a390d, 0x001a590d, 0x001a790d,
    0x001a990d, 0x001ab90d, 0x001ad90d, 0x001af90d,
    0x001b190d, 0x001b390d, 0x001b590d, 0x001b790d,
    0x001b990d, 0x001bb90d, 0x001bd90d, 0x001bf90d,
    0x001c190d, 0x001c390d, 0x001c590d, 0x001c790d,
    0x001c990d, 0x001cb90d, 0x001cd90d, 0x001cf90d,
    0x001d190d, 0x001d390d, 0x001d590d, 0x001d790d,
    0x001d990d, 0x001db90d, 0x001dd90d, 0x001df90d,
    0x001e190d, 0x001e390d, 0x001e590d, 0x001e790d,
    0x001e990d, 0x001eb90d, 0x001ed90d, 0x001ef90d,
    0x001f190d, 0x001f390d, 0x001f590d, 0x001f790d,
    0x001f990d, 0x001fb90d, 0x001fd90d, 0x001ff90d,
    0x0000050e, 0x0000250e, 0x0000450e, 0x0000650e,
    0x0000850e, 0x0000a50e, 0x0000c50e, 0x0000e50e,
    0x0001050e, 0x0001250e, 0x0001450e, 0x0001650e,
    0x0001850e, 0x0001a50e, 0x0001c50e, 0x0001e50e,
    0x0002050e, 0x0002250e, 0x0002450e, 0x0002650e,
    0x0002850e, 0x0002a50e, 0x0002c50e, 0x0002e50e,
    0x0003050e, 0x0003250e, 0x0003450e, 0x0003650e,
    0x0003850e, 0x0003a50e, 0x0003c50e, 0x0003e50e,
    0x0004050e, 0x0004250e, 0x0004450e, 0x0004650e,
    0x0004850e, 0x0004a50e, 0x0004c50e, 0x0004e50e,
    0x0005050e, 0x0005250e, 0x0005450e, 0x0005650e,
    0x0005850e, 0x0005a50e, 0x0005c50e, 0x0005e50e,
    0x0006050e, 0x0006250e, 0x0006450e, 0x0006650e,
    0x0006850e, 0x0006a50e, 0x0006c50e, 0x0006e50e,
    0x0007050e, 0x0007250e, 0x0007450e, 0x0007650e,
    0x0007850e, 0x0007a50e, 0x0007c50e, 0x0007e50e,
    0x0008050e, 0x0008250e, 0x0008450e, 0x0008650e,
    0x0008850e, 0x0008a50e, 0x0008c50e, 0x0008e50e,
    0x0009050e, 0x0009250e, 0x0009450e, 0x0009650e,
    0x0009850e, 0x0009a50e, 0x0009c50e, 0x0009e50e,
    0x000a050e, 0x000a250e, 0x000a450e, 0x000a650e,
    0x000a850e, 0x000aa50e, 0x000ac50e, 0x000ae50e,
    0x000b050e, 0x000b250e, 0x000b450e, 0x000b650e,
    0x000b850e, 0x000ba50e, 0x000bc50e, 0x000be50e,
    0x000c050e, 0x000c250e, 0x000c450e, 0x000c650e,
    0x000c850e, 0x000ca50e, 0x000cc50e, 0x000ce50e,
    0x000d050e, 0x000d250e, 0x000d450e, 0x000d650e,
    0x000d850e, 0x000da50e, 0x000dc50e, 0x000de50e,
    0x000e050e, 0x000e250e, 0x000e450e, 0x000e650e,
    0x000e850e, 0x000ea50e, 0x000ec50e, 0x000ee50e,
    0x000f050e, 0x000f250e, 0x000f450e, 0x000f650e,
    0x000f850e, 0x000fa50e, 0x000fc50e, 0x000fe50e,
    0x0010050e, 0x0010250e, 0x0010450e, 0x0010650e,
    0x0010850e, 0x0010a50e, 0x0010c50e, 0x0010e50e,
    0x0011050e, 0x0011250e, 0x0011450e, 0x0011650e,
    0x0011850e, 0x0011a50e, 0x0011c50e, 0x0011e50e,
    0x0012050e, 0x0012250e, 0x0012450e, 0x0012650e,
    0x0012850e, 0x0012a50e, 0x0012c50e, 0x0012e50e,
    0x0013050e, 0x0013250e, 0x0013450e, 0x0013650e,
    0x0013850e, 0x0013a50e, 0x0013c50e, 0x0013e50e,
    0x0014050e, 0x0014250e, 0x0014450e, 0x0014650e,
    0x0014850e, 0x0014a50e, 0x0014c50e, 0x0014e50e,
    0x0015050e, 0x0015250e, 0x0015450e, 0x0015650e,
    0x0015850e, 0x0015a50e, 0x0015c50e, 0x0015e50e,
    0x0016050e, 0x0016250e, 0x0016450e, 0x0016650e,
    0x0016850e, 0x0016a50e, 0x0016c50e, 0x0016e50e,
    0x0017050e, 0x0017250e, 0x0017450e, 0x0017650e,
    0x0017850e, 0x0017a50e, 0x0017c50e, 0x0017e50e,
    0x0018050e, 0x0018250e, 0x0018450e, 0x0018650e,
    0x0018850e, 0x0018a50e, 0x0018c50e, 0x0018e50e,
    0x0019050e, 0x0019250e, 0x0019450e, 0x0019650e,
    0x0019850e, 0x0019a50e, 0x0019c50e, 0x0019e50e,
    0x001a050e, 0x001a250e, 0x001a450e, 0x001a650e,
    0x001a850e, 0x001aa50e, 0x001ac50e, 0x001ae50e,
    0x001b050e, 0x001b250e, 0x001b450e, 0x001b650e,
    0x001b850e, 0x001ba50e, 0x001bc50e, 0x001be50e,
    0x001c050e, 0x001c250e, 0x001c450e, 0x001c650e,
    0x001c850e, 0x001ca50e, 0x001cc50e, 0x001ce50e,
    0x001d050e, 0x001d250e, 0x001d450e, 0x001d650e,
    0x001d850e, 0x001da50e, 0x001dc50e, 0x001de50e,
    0x001e050e, 0x001e250e, 0x001e450e, 0x001e650e,
    0x001e850e, 0x001ea50e, 0x001ec50e, 0x001ee50e,
    0x001f050e, 0x001f250e, 0x001f450e, 0x001f650e,
    0x001f850e, 0x001fa50e, 0x001fc50e, 0x001fe50e,
    0x0020050e, 0x0020250e, 0x0020450e, 0x0020650e,
    0x0020850e, 0x0020a50e, 0x0020c50e, 0x0020e50e,
    0x0021050e, 0x0021250e, 0x0021450e, 0x0021650e,
    0x0021850e, 0x0021a50e, 0x0021c50e, 0x0021e50e,
    0x0022050e, 0x0022250e, 0x0022450e, 0x0022650e,
    0x0022850e, 0x0022a50e, 0x0022c50e, 0x0022e50e,
    0x0023050e, 0x0023250e, 0x0023450e, 0x0023650e,
    0x0023850e, 0x0023a50e, 0x0023c50e, 0x0023e50e,
    0x0024050e, 0x0024250e, 0x0024450e, 0x0024650e,
    0x0024850e, 0x0024a50e, 0x0024c50e, 0x0024e50e,
    0x0025050e, 0x0025250e, 0x0025450e, 0x0025650e,
    0x0025850e, 0x0025a50e, 0x0025c50e, 0x0025e50e,
    0x0026050e, 0x0026250e, 0x0026450e, 0x0026650e,
    0x0026850e, 0x0026a50e, 0x0026c50e, 0x0026e50e,
    0x0027050e, 0x0027250e, 0x0027450e, 0x0027650e,
    0x0027850e, 0x0027a50e, 0x0027c50e, 0x0027e50e,
    0x0028050e, 0x0028250e, 0x0028450e, 0x0028650e,
    0x0028850e, 0x0028a50e, 0x0028c50e, 0x0028e50e,
    0x0029050e, 0x0029250e, 0x0029450e, 0x0029650e,
    0x0029850e, 0x0029a50e, 0x0029c50e, 0x0029e50e,
    0x002a050e, 0x002a250e, 0x002a450e, 0x002a650e,
    0x002a850e, 0x002aa50e, 0x002ac50e, 0x002ae50e,
    0x002b050e, 0x002b250e, 0x002b450e, 0x002b650e,
    0x002b850e, 0x002ba50e, 0x002bc50e, 0x002be50e,
    0x002c050e, 0x002c250e, 0x002c450e, 0x002c650e,
    0x002c850e, 0x002ca50e, 0x002cc50e, 0x002ce50e,
    0x002d050e, 0x002d250e, 0x002d450e, 0x002d650e,
    0x002d850e, 0x002da50e, 0x002dc50e, 0x002de50e,
    0x002e050e, 0x002e250e, 0x002e450e, 0x002e650e,
    0x002e850e, 0x002ea50e, 0x002ec50e, 0x002ee50e,
    0x002f050e, 0x002f250e, 0x002f450e, 0x002f650e,
    0x002f850e, 0x002fa50e, 0x002fc50e, 0x002fe50e,
    0x0030050e, 0x0030250e, 0x0030450e, 0x0030650e,
    0x0030850e, 0x0030a50e, 0x0030c50e, 0x0030e50e,
    0x0031050e, 0x0031250e, 0x0031450e, 0x0031650e,
    0x0031850e, 0x0031a50e, 0x0031c50e, 0x0031e50e,
    0x0032050e, 0x0032250e, 0x0032450e, 0x0032650e,
    0x0032850e, 0x0032a50e, 0x0032c50e, 0x0032e50e,
    0x0033050e, 0x0033250e, 0x0033450e, 0x0033650e,
    0x0033850e, 0x0033a50e, 0x0033c50e, 0x0033e50e,
    0x0034050e, 0x0034250e, 0x0034450e, 0x0034650e,
    0x0034850e, 0x0034a50e, 0x0034c50e, 0x0034e50e,
    0x0035050e, 0x0035250e, 0x0035450e, 0x0035650e,
    0x0035850e, 0x0035a50e, 0x0035c50e, 0x0035e50e,
    0x0036050e, 0x0036250e, 0x0036450e, 0x0036650e,
    0x0036850e, 0x0036a50e, 0x0036c50e, 0x0036e50e,
    0x0037050e, 0x0037250e, 0x0037450e, 0x0037650e,
    0x0037850e, 0x0037a50e, 0x0037c50e, 0x0037e50e,
    0x0038050e, 0x0038250e, 0x0038450e, 0x0038650e,
    0x0038850e, 0x0038a50e, 0x0038c50e, 0x0038e50e,
    0x0039050e, 0x0039250e, 0x0039450e, 0x0039650e,
    0x0039850e, 0x0039a50e, 0x0039c50e, 0x0039e50e,
    0x003a050e, 0x003a250e, 0x003a450e, 0x003a650e,
    0x003a850e, 0x003aa50e, 0x003ac50e, 0x003ae50e,
    0x003b050e, 0x003b250e, 0x003b450e, 0x003b650e,
    0x003b850e, 0x003ba50e, 0x003bc50e, 0x003be50e,
    0x003c050e, 0x003c250e, 0x003c450e, 0x003c650e,
    0x003c850e, 0x003ca50e, 0x003cc50e, 0x003ce50e,
    0x003d050e, 0x003d250e, 0x003d450e, 0x003d650e,
    0x003d850e, 0x003da50e, 0x003dc50e, 0x003de50e,
    0x003e050e, 0x003e250e, 0x003e450e, 0x003e650e,
    0x003e850e, 0x003ea50e, 0x003ec50e, 0x003ee50e,
    0x003f050e, 0x003f250e, 0x003f450e, 0x003f650e,
    0x003f850e, 0x003fa50e, 0x003fc50e, 0x003fe50e,
    0x0000150e, 0x0000350e, 0x0000550e, 0x0000750e,
    0x0000950e, 0x0000b50e, 0x0000d50e, 0x0000f50e,
    0x0001150e, 0x0001350e, 0x0001550e, 0x0001750e,
    0x0001950e, 0x0001b50e, 0x0001d50e, 0x0001f50e,
    0x0002150e, 0x0002350e, 0x0002550e, 0x0002750e,
    0x0002950e, 0x0002b50e, 0x0002d50e, 0x0002f50e,
    0x0003150e, 0x0003350e, 0x0003550e, 0x0003750e,
    0x0003950e, 0x0003b50e, 0x0003d50e, 0x0003f50e,
    0x0004150e, 0x0004350e, 0x0004550e, 0x0004750e,
    0x0004950e, 0x0004b50e, 0x0004d50e, 0x0004f50e,
    0x0005150e, 0x0005350e, 0x0005550e, 0x0005750e,
    0x0005950e, 0x0005b50e, 0x0005d50e, 0x0005f50e,
    0x0006150e, 0x0006350e, 0x0006550e, 0x0006750e,
    0x0006950e, 0x0006b50e, 0x0006d50e, 0x0006f50e,
    0x0007150e, 0x0007350e, 0x0007550e, 0x0007750e,
    0x0007950e, 0x0007b50e, 0x0007d50e, 0x0007f50e,
    0x0008150e, 0x0008350e, 0x0008550e, 0x0008750e,
    0x0008950e, 0x0008b50e, 0x0008d50e, 0x0008f50e,
    0x0009150e, 0x0009350e, 0x0009550e, 0x0009750e,
    0x0009950e, 0x0009b50e, 0x0009d50e, 0x0009f50e,
    0x000a150e, 0x000a350e, 0x000a550e, 0x000a750e,
    0x000a950e, 0x000ab50e, 0x000ad50e, 0x000af50e,
    0x000b150e, 0x000b350e, 0x000b550e, 0x000b750e,
    0x000b950e, 0x000bb50e, 0x000bd50e, 0x000bf50e,
    0x000c150e, 0x000c350e, 0x000c550e, 0x000c750e,
    0x000c950e, 0x000cb50e, 0x000cd50e, 0x000cf50e,
    0x000d150e, 0x000d350e, 0x000d550e, 0x000d750e,
    0x000d950e, 0x000db50e, 0x000dd50e, 0x000df50e,
    0x000e150e, 0x000e350e, 0x000e550e, 0x000e750e,
    0x000e950e, 0x000eb50e, 0x000ed50e, 0x000ef50e,
    0x000f150e, 0x000f350e, 0x000f550e, 0x000f750e,
    0x000f950e, 0x000fb50e, 0x000fd50e, 0x000ff50e,
    0x0010150e, 0x0010350e, 0x0010550e, 0x0010750e,
    0x0010950e, 0x0010b50e, 0x0010d50e, 0x0010f50e,
    0x0011150e, 0x0011350e, 0x0011550e, 0x0011750e,
    0x0011950e, 0x0011b50e, 0x0011d50e, 0x0011f50e,
    0x0012150e, 0x0012350e, 0x0012550e, 0x0012750e,
    0x0012950e, 0x0012b50e, 0x0012d50e, 0x0012f50e,
    0x0013150e, 0x0013350e, 0x0013550e, 0x0013750e,
    0x0013950e, 0x0013b50e, 0x0013d50e, 0x0013f50e,
    0x0014150e, 0x0014350e, 0x0014550e, 0x0014750e,
    0x0014950e, 0x0014b50e, 0x0014d50e, 0x0014f50e,
    0x0015150e, 0x0015350e, 0x0015550e, 0x0015750e,
    0x0015950e, 0x0015b50e, 0x0015d50e, 0x0015f50e,
    0x0016150e, 0x0016350e, 0x0016550e, 0x0016750e,
    0x0016950e, 0x0016b50e, 0x0016d50e, 0x0016f50e,
    0x0017150e, 0x0017350e, 0x0017550e, 0x0017750e,
    0x0017950e, 0x0017b50e, 0x0017d50e, 0x0017f50e,
    0x0018150e, 0x0018350e, 0x0018550e, 0x0018750e,
    0x0018950e, 0x0018b50e, 0x0018d50e, 0x0018f50e,
    0x0019150e, 0x0019350e, 0x0019550e, 0x0019750e,
    0x0019950e, 0x0019b50e, 0x0019d50e, 0x0019f50e,
    0x001a150e, 0x001a350e, 0x001a550e, 0x001a750e,
    0x001a950e, 0x001ab50e, 0x001ad50e, 0x001af50e,
    0x001b150e, 0x001b350e, 0x001b550e, 0x001b750e,
    0x001b950e, 0x001bb50e, 0x001bd50e, 0x001bf50e,
    0x001c150e, 0x001c350e, 0x001c550e, 0x001c750e,
    0x001c950e, 0x001cb50e, 0x001cd50e, 0x001cf50e,
    0x001d150e, 0x001d350e, 0x001d550e, 0x001d750e,
    0x001d950e, 0x001db50e, 0x001dd50e, 0x001df50e,
    0x001e150e, 0x001e350e, 0x001e550e, 0x001e750e,
    0x001e950e, 0x001eb50e, 0x001ed50e, 0x001ef50e,
    0x001f150e, 0x001f350e, 0x001f550e, 0x001f750e,
    0x001f950e, 0x001fb50e, 0x001fd50e, 0x001ff50e,
    0x0020150e, 0x0020350e, 0x0020550e, 0x0020750e,
    0x0020950e, 0x0020b50e, 0x0020d50e, 0x0020f50e,
    0x0021150e, 0x0021350e, 0x0021550e, 0x0021750e,
    0x0021950e, 0x0021b50e, 0x0021d50e, 0x0021f50e,
    0x0022150e, 0x0022350e, 0x0022550e, 0x0022750e,
    0x0022950e, 0x0022b50e, 0x0022d50e, 0x0022f50e,
    0x0023150e, 0x0023350e, 0x0023550e, 0x0023750e,
    0x0023950e, 0x0023b50e, 0x0023d50e, 0x0023f50e,
    0x0024150e, 0x0024350e, 0x0024550e, 0x0024750e,
    0x0024950e, 0x0024b50e, 0x0024d50e, 0x0024f50e,
    0x0025150e, 0x0025350e, 0x0025550e, 0x0025750e,
    0x0025950e, 0x0025b50e, 0x0025d50e, 0x0025f50e,
    0x0026150e, 0x0026350e, 0x0026550e, 0x0026750e,
    0x0026950e, 0x0026b50e, 0x0026d50e, 0x0026f50e,
    0x0027150e, 0x0027350e, 0x0027550e, 0x0027750e,
    0x0027950e, 0x0027b50e, 0x0027d50e, 0x0027f50e,
    0x0028150e, 0x0028350e, 0x0028550e, 0x0028750e,
    0x0028950e, 0x0028b50e, 0x0028d50e, 0x0028f50e,
    0x0029150e, 0x0029350e, 0x0029550e, 0x0029750e,
    0x0029950e, 0x0029b50e, 0x0029d50e, 0x0029f50e,
    0x002a150e, 0x002a350e, 0x002a550e, 0x002a750e,
    0x002a950e, 0x002ab50e, 0x002ad50e, 0x002af50e,
    0x002b150e, 0x002b350e, 0x002b550e, 0x002b750e,
    0x002b950e, 0x002bb50e, 0x002bd50e, 0x002bf50e,
    0x002c150e, 0x002c350e, 0x002c550e, 0x002c750e,
    0x002c950e, 0x002cb50e, 0x002cd50e, 0x002cf50e,
    0x002d150e, 0x002d350e, 0x002d550e, 0x002d750e,
    0x002d950e, 0x002db50e, 0x002dd50e, 0x002df50e,
    0x002e150e, 0x002e350e, 0x002e550e, 0x002e750e,
    0x002e950e, 0x002eb50e, 0x002ed50e, 0x002ef50e,
    0x002f150e, 0x002f350e, 0x002f550e, 0x002f750e,
    0x002f950e, 0x002fb50e, 0x002fd50e, 0x002ff50e,
    0x0030150e, 0x0030350e, 0x0030550e, 0x0030750e,
    0x0030950e, 0x0030b50e, 0x0030d50e, 0x0030f50e,
    0x0031150e, 0x0031350e, 0x0031550e, 0x0031750e,
    0x0031950e, 0x0031b50e, 0x0031d50e, 0x0031f50e,
    0x0032150e, 0x0032350e, 0x0032550e, 0x0032750e,
    0x0032950e, 0x0032b50e, 0x0032d50e, 0x0032f50e,
    0x0033150e, 0x0033350e, 0x0033550e, 0x0033750e,
    0x0033950e, 0x0033b50e, 0x0033d50e, 0x0033f50e,
    0x0034150e, 0x0034350e, 0x0034550e, 0x0034750e,
    0x0034950e, 0x0034b50e, 0x0034d50e, 0x0034f50e,
    0x0035150e, 0x0035350e, 0x0035550e, 0x0035750e,
    0x0035950e, 0x0035b50e, 0x0035d50e, 0x0035f50e,
    0x0036150e, 0x0036350e, 0x0036550e, 0x0036750e,
    0x0036950e, 0x0036b50e, 0x0036d50e, 0x0036f50e,
    0x0037150e, 0x0037350e, 0x0037550e, 0x0037750e,
    0x0037950e, 0x0037b50e, 0x0037d50e, 0x0037f50e,
    0x0038150e, 0x0038350e, 0x0038550e, 0x0038750e,
    0x0038950e, 0x0038b50e, 0x0038d50e, 0x0038f50e,
    0x0039150e, 0x0039350e, 0x0039550e, 0x0039750e,
    0x0039950e, 0x0039b50e, 0x0039d50e, 0x0039f50e,
    0x003a150e, 0x003a350e, 0x003a550e, 0x003a750e,
    0x003a950e, 0x003ab50e, 0x003ad50e, 0x003af50e,
    0x003b150e, 0x003b350e, 0x003b550e, 0x003b750e,
    0x003b950e, 0x003bb50e, 0x003bd50e, 0x003bf50e,
    0x003c150e, 0x003c350e, 0x003c550e, 0x003c750e,
    0x003c950e, 0x003cb50e, 0x003cd50e, 0x003cf50e,
    0x003d150e, 0x003d350e, 0x003d550e, 0x003d750e,
    0x003d950e, 0x003db50e, 0x003dd50e, 0x003df50e,
    0x003e150e, 0x003e350e, 0x003e550e, 0x003e750e,
    0x003e950e, 0x003eb50e, 0x003ed50e, 0x003ef50e,
    0x003f150e, 0x003f350e, 0x003f550e, 0x003f750e,
    0x003f950e, 0x003fb50e, 0x003fd50e, 0x003ff50e,
    0x00000d0f, 0x00002d0f, 0x00004d0f, 0x00006d0f,
    0x00008d0f, 0x0000ad0f, 0x0000cd0f, 0x0000ed0f,
    0x00010d0f, 0x00012d0f, 0x00014d0f, 0x00016d0f,
    0x00018d0f, 0x0001ad0f, 0x0001cd0f, 0x0001ed0f,
    0x00020d0f, 0x00022d0f, 0x00024d0f, 0x00026d0f,
    0x00028d0f, 0x0002ad0f, 0x0002cd0f, 0x0002ed0f,
    0x00030d0f, 0x00032d0f, 0x00034d0f, 0x00036d0f,
    0x00038d0f, 0x0003ad0f, 0x0003cd0f, 0x0003ed0f,
    0x00040d0f, 0x00042d0f, 0x00044d0f, 0x00046d0f,
    0x00048d0f, 0x0004ad0f, 0x0004cd0f, 0x0004ed0f,
    0x00050d0f, 0x00052d0f, 0x00054d0f, 0x00056d0f,
    0x00058d0f, 0x0005ad0f, 0x0005cd0f, 0x0005ed0f,
    0x00060d0f, 0x00062d0f, 0x00064d0f, 0x00066d0f,
    0x00068d0f, 0x0006ad0f, 0x0006cd0f, 0x0006ed0f,
    0x00070d0f, 0x00072d0f, 0x00074d0f, 0x00076d0f,
    0x00078d0f, 0x0007ad0f, 0x0007cd0f, 0x0007ed0f,
    0x00080d0f, 0x00082d0f, 0x00084d0f, 0x00086d0f,
    0x00088d0f, 0x0008ad0f, 0x0008cd0f, 0x0008ed0f,
    0x00090d0f, 0x00092d0f, 0x00094d0f, 0x00096d0f,
    0x00098d0f, 0x0009ad0f, 0x0009cd0f, 0x0009ed0f,
    0x000a0d0f, 0x000a2d0f, 0x000a4d0f, 0x000a6d0f,
    0x000a8d0f, 0x000aad0f, 0x000acd0f, 0x000aed0f,
    0x000b0d0f, 0x000b2d0f, 0x000b4d0f, 0x000b6d0f,
    0x000b8d0f, 0x000bad0f, 0x000bcd0f, 0x000bed0f,
    0x000c0d0f, 0x000c2d0f, 0x000c4d0f, 0x000c6d0f,
    0x000c8d0f, 0x000cad0f, 0x000ccd0f, 0x000ced0f,
    0x000d0d0f, 0x000d2d0f, 0x000d4d0f, 0x000d6d0f,
    0x000d8d0f, 0x000dad0f, 0x000dcd0f, 0x000ded0f,
    0x000e0d0f, 0x000e2d0f, 0x000e4d0f, 0x000e6d0f,
    0x000e8d0f, 0x000ead0f, 0x000ecd0f, 0x000eed0f,
    0x000f0d0f, 0x000f2d0f, 0x000f4d0f, 0x000f6d0f,
    0x000f8d0f, 0x000fad0f, 0x000fcd0f, 0x000fed0f,
    0x00100d0f, 0x00102d0f, 0x00104d0f, 0x00106d0f,
    0x00108d0f, 0x0010ad0f, 0x0010cd0f, 0x0010ed0f,
    0x00110d0f, 0x00112d0f, 0x00114d0f, 0x00116d0f,
    0x00118d0f, 0x0011ad0f, 0x0011cd0f, 0x0011ed0f,
    0x00120d0f, 0x00122d0f, 0x00124d0f, 0x00126d0f,
    0x00128d0f, 0x0012ad0f, 0x0012cd0f, 0x0012ed0f,
    0x00130d0f, 0x00132d0f, 0x00134d0f, 0x00136d0f,
    0x00138d0f, 0x0013ad0f, 0x0013cd0f, 0x0013ed0f,
    0x00140d0f, 0x00142d0f, 0x00144d0f, 0x00146d0f,
    0x00148d0f, 0x0014ad0f, 0x0014cd0f, 0x0014ed0f,
    0x00150d0f, 0x00152d0f, 0x00154d0f, 0x00156d0f,
    0x00158d0f, 0x0015ad0f, 0x0015cd0f, 0x0015ed0f,
    0x00160d0f, 0x00162d0f, 0x00164d0f, 0x00166d0f,
    0x00168d0f, 0x0016ad0f, 0x0016cd0f, 0x0016ed0f,
    0x00170d0f, 0x00172d0f, 0x00174d0f, 0x00176d0f,
    0x00178d0f, 0x0017ad0f, 0x0017cd0f, 0x0017ed0f,
    0x00180d0f, 0x00182d0f, 0x00184d0f, 0x00186d0f,
    0x00188d0f, 0x0018ad0f, 0x0018cd0f, 0x0018ed0f,
    0x00190d0f, 0x00192d0f, 0x00194d0f, 0x00196d0f,
    0x00198d0f, 0x0019ad0f, 0x0019cd0f, 0x0019ed0f,
    0x001a0d0f, 0x001a2d0f, 0x001a4d0f, 0x001a6d0f,
    0x001a8d0f, 0x001aad0f, 0x001acd0f, 0x001aed0f,
    0x001b0d0f, 0x001b2d0f, 0x001b4d0f, 0x001b6d0f,
    0x001b8d0f, 0x001bad0f, 0x001bcd0f, 0x001bed0f,
    0x001c0d0f, 0x001c2d0f, 0x001c4d0f, 0x001c6d0f,
    0x001c8d0f, 0x001cad0f, 0x001ccd0f, 0x001ced0f,
    0x001d0d0f, 0x001d2d0f, 0x001d4d0f, 0x001d6d0f,
    0x001d8d0f, 0x001dad0f, 0x001dcd0f, 0x001ded0f,
    0x001e0d0f, 0x001e2d0f, 0x001e4d0f, 0x001e6d0f,
    0x001e8d0f, 0x001ead0f, 0x001ecd0f, 0x001eed0f,
    0x001f0d0f, 0x001f2d0f, 0x001f4d0f, 0x001f6d0f,
    0x001f8d0f, 0x001fad0f, 0x001fcd0f, 0x001fed0f,
    0x00200d0f, 0x00202d0f, 0x00204d0f, 0x00206d0f,
    0x00208d0f, 0x0020ad0f, 0x0020cd0f, 0x0020ed0f,
    0x00210d0f, 0x00212d0f, 0x00214d0f, 0x00216d0f,
    0x00218d0f, 0x0021ad0f, 0x0021cd0f, 0x0021ed0f,
    0x00220d0f, 0x00222d0f, 0x00224d0f, 0x00226d0f,
    0x00228d0f, 0x0022ad0f, 0x0022cd0f, 0x0022ed0f,
    0x00230d0f, 0x00232d0f, 0x00234d0f, 0x00236d0f,
    0x00238d0f, 0x0023ad0f, 0x0023cd0f, 0x0023ed0f,
    0x00240d0f, 0x00242d0f, 0x00244d0f, 0x00246d0f,
    0x00248d0f, 0x0024ad0f, 0x0024cd0f, 0x0024ed0f,
    0x00250d0f, 0x00252d0f, 0x00254d0f, 0x00256d0f,
    0x00258d0f, 0x0025ad0f, 0x0025cd0f, 0x0025ed0f,
    0x00260d0f, 0x00262d0f, 0x00264d0f, 0x00266d0f,
    0x00268d0f, 0x0026ad0f, 0x0026cd0f, 0x0026ed0f,
    0x00270d0f, 0x00272d0f, 0x00274d0f, 0x00276d0f,
    0x00278d0f, 0x0027ad0f, 0x0027cd0f, 0x0027ed0f,
    0x00280d0f, 0x00282d0f, 0x00284d0f, 0x00286d0f,
    0x00288d0f, 0x0028ad0f, 0x0028cd0f, 0x0028ed0f,
    0x00290d0f, 0x00292d0f, 0x00294d0f, 0x00296d0f,
    0x00298d0f, 0x0029ad0f, 0x0029cd0f, 0x0029ed0f,
    0x002a0d0f, 0x002a2d0f, 0x002a4d0f, 0x002a6d0f,
    0x002a8d0f, 0x002aad0f, 0x002acd0f, 0x002aed0f,
    0x002b0d0f, 0x002b2d0f, 0x002b4d0f, 0x002b6d0f,
    0x002b8d0f, 0x002bad0f, 0x002bcd0f, 0x002bed0f,
    0x002c0d0f, 0x002c2d0f, 0x002c4d0f, 0x002c6d0f,
    0x002c8d0f, 0x002cad0f, 0x002ccd0f, 0x002ced0f,
    0x002d0d0f, 0x002d2d0f, 0x002d4d0f, 0x002d6d0f,
    0x002d8d0f, 0x002dad0f, 0x002dcd0f, 0x002ded0f,
    0x002e0d0f, 0x002e2d0f, 0x002e4d0f, 0x002e6d0f,
    0x002e8d0f, 0x002ead0f, 0x002ecd0f, 0x002eed0f,
    0x002f0d0f, 0x002f2d0f, 0x002f4d0f, 0x002f6d0f,
    0x002f8d0f, 0x002fad0f, 0x002fcd0f, 0x002fed0f,
    0x00300d0f, 0x00302d0f, 0x00304d0f, 0x00306d0f,
    0x00308d0f, 0x0030ad0f, 0x0030cd0f, 0x0030ed0f,
    0x00310d0f, 0x00312d0f, 0x00314d0f, 0x00316d0f,
    0x00318d0f, 0x0031ad0f, 0x0031cd0f, 0x0031ed0f,
    0x00320d0f, 0x00322d0f, 0x00324d0f, 0x00326d0f,
    0x00328d0f, 0x0032ad0f, 0x0032cd0f, 0x0032ed0f,
    0x00330d0f, 0x00332d0f, 0x00334d0f, 0x00336d0f,
    0x00338d0f, 0x0033ad0f, 0x0033cd0f, 0x0033ed0f,
    0x00340d0f, 0x00342d0f, 0x00344d0f, 0x00346d0f,
    0x00348d0f, 0x0034ad0f, 0x0034cd0f, 0x0034ed0f,
    0x00350d0f, 0x00352d0f, 0x00354d0f, 0x00356d0f,
    0x00358d0f, 0x0035ad0f, 0x0035cd0f, 0x0035ed0f,
    0x00360d0f, 0x00362d0f, 0x00364d0f, 0x00366d0f,
    0x00368d0f, 0x0036ad0f, 0x0036cd0f, 0x0036ed0f,
    0x00370d0f, 0x00372d0f, 0x00374d0f, 0x00376d0f,
    0x00378d0f, 0x0037ad0f, 0x0037cd0f, 0x0037ed0f,
    0x00380d0f, 0x00382d0f, 0x00384d0f, 0x00386d0f,
    0x00388d0f, 0x0038ad0f, 0x0038cd0f, 0x0038ed0f,
    0x00390d0f, 0x00392d0f, 0x00394d0f, 0x00396d0f,
    0x00398d0f, 0x0039ad0f, 0x0039cd0f, 0x0039ed0f,
    0x003a0d0f, 0x003a2d0f, 0x003a4d0f, 0x003a6d0f,
    0x003a8d0f, 0x003aad0f, 0x003acd0f, 0x003aed0f,
    0x003b0d0f, 0x003b2d0f, 0x003b4d0f, 0x003b6d0f,
    0x003b8d0f, 0x003bad0f, 0x003bcd0f, 0x003bed0f,
    0x003c0d0f, 0x003c2d0f, 0x003c4d0f, 0x003c6d0f,
    0x003c8d0f, 0x003cad0f, 0x003ccd0f, 0x003ced0f,
    0x003d0d0f, 0x003d2d0f, 0x003d4d0f, 0x003d6d0f,
    0x003d8d0f, 0x003dad0f, 0x003dcd0f, 0x003ded0f,
    0x003e0d0f, 0x003e2d0f, 0x003e4d0f, 0x003e6d0f,
    0x003e8d0f, 0x003ead0f, 0x003ecd0f, 0x003eed0f,
    0x003f0d0f, 0x003f2d0f, 0x003f4d0f, 0x003f6d0f,
    0x003f8d0f, 0x003fad0f, 0x003fcd0f, 0x003fed0f,
    0x00400d0f, 0x00402d0f, 0x00404d0f, 0x00406d0f,
    0x00408d0f, 0x0040ad0f, 0x0040cd0f, 0x0040ed0f,
    0x00410d0f, 0x00412d0f, 0x00414d0f, 0x00416d0f,
    0x00418d0f, 0x0041ad0f, 0x0041cd0f, 0x0041ed0f,
    0x00420d0f, 0x00422d0f, 0x00424d0f, 0x00426d0f,
    0x00428d0f, 0x0042ad0f, 0x0042cd0f, 0x0042ed0f,
    0x00430d0f, 0x00432d0f, 0x00434d0f, 0x00436d0f,
    0x00438d0f, 0x0043ad0f, 0x0043cd0f, 0x0043ed0f,
    0x00440d0f, 0x00442d0f, 0x00444d0f, 0x00446d0f,
    0x00448d0f, 0x0044ad0f, 0x0044cd0f, 0x0044ed0f,
    0x00450d0f, 0x00452d0f, 0x00454d0f, 0x00456d0f,
    0x00458d0f, 0x0045ad0f, 0x0045cd0f, 0x0045ed0f,
    0x00460d0f, 0x00462d0f, 0x00464d0f, 0x00466d0f,
    0x00468d0f, 0x0046ad0f, 0x0046cd0f, 0x0046ed0f,
    0x00470d0f, 0x00472d0f, 0x00474d0f, 0x00476d0f,
    0x00478d0f, 0x0047ad0f, 0x0047cd0f, 0x0047ed0f,
    0x00480d0f, 0x00482d0f, 0x00484d0f, 0x00486d0f,
    0x00488d0f, 0x0048ad0f, 0x0048cd0f, 0x0048ed0f,
    0x00490d0f, 0x00492d0f, 0x00494d0f, 0x00496d0f,
    0x00498d0f, 0x0049ad0f, 0x0049cd0f, 0x0049ed0f,
    0x004a0d0f, 0x004a2d0f, 0x004a4d0f, 0x004a6d0f,
    0x004a8d0f, 0x004aad0f, 0x004acd0f, 0x004aed0f,
    0x004b0d0f, 0x004b2d0f, 0x004b4d0f, 0x004b6d0f,
    0x004b8d0f, 0x004bad0f, 0x004bcd0f, 0x004bed0f,
    0x004c0d0f, 0x004c2d0f, 0x004c4d0f, 0x004c6d0f,
    0x004c8d0f, 0x004cad0f, 0x004ccd0f, 0x004ced0f,
    0x004d0d0f, 0x004d2d0f, 0x004d4d0f, 0x004d6d0f,
    0x004d8d0f, 0x004dad0f, 0x004dcd0f, 0x004ded0f,
    0x004e0d0f, 0x004e2d0f, 0x004e4d0f, 0x004e6d0f,
    0x004e8d0f, 0x004ead0f, 0x004ecd0f, 0x004eed0f,
    0x004f0d0f, 0x004f2d0f, 0x004f4d0f, 0x004f6d0f,
    0x004f8d0f, 0x004fad0f, 0x004fcd0f, 0x004fed0f,
    0x00500d0f, 0x00502d0f, 0x00504d0f, 0x00506d0f,
    0x00508d0f, 0x0050ad0f, 0x0050cd0f, 0x0050ed0f,
    0x00510d0f, 0x00512d0f, 0x00514d0f, 0x00516d0f,
    0x00518d0f, 0x0051ad0f, 0x0051cd0f, 0x0051ed0f,
    0x00520d0f, 0x00522d0f, 0x00524d0f, 0x00526d0f,
    0x00528d0f, 0x0052ad0f, 0x0052cd0f, 0x0052ed0f,
    0x00530d0f, 0x00532d0f, 0x00534d0f, 0x00536d0f,
    0x00538d0f, 0x0053ad0f, 0x0053cd0f, 0x0053ed0f,
    0x00540d0f, 0x00542d0f, 0x00544d0f, 0x00546d0f,
    0x00548d0f, 0x0054ad0f, 0x0054cd0f, 0x0054ed0f,
    0x00550d0f, 0x00552d0f, 0x00554d0f, 0x00556d0f,
    0x00558d0f, 0x0055ad0f, 0x0055cd0f, 0x0055ed0f,
    0x00560d0f, 0x00562d0f, 0x00564d0f, 0x00566d0f,
    0x00568d0f, 0x0056ad0f, 0x0056cd0f, 0x0056ed0f,
    0x00570d0f, 0x00572d0f, 0x00574d0f, 0x00576d0f,
    0x00578d0f, 0x0057ad0f, 0x0057cd0f, 0x0057ed0f,
    0x00580d0f, 0x00582d0f, 0x00584d0f, 0x00586d0f,
    0x00588d0f, 0x0058ad0f, 0x0058cd0f, 0x0058ed0f,
    0x00590d0f, 0x00592d0f, 0x00594d0f, 0x00596d0f,
    0x00598d0f, 0x0059ad0f, 0x0059cd0f, 0x0059ed0f,
    0x005a0d0f, 0x005a2d0f, 0x005a4d0f, 0x005a6d0f,
    0x005a8d0f, 0x005aad0f, 0x005acd0f, 0x005aed0f,
    0x005b0d0f, 0x005b2d0f, 0x005b4d0f, 0x005b6d0f,
    0x005b8d0f, 0x005bad0f, 0x005bcd0f, 0x005bed0f,
    0x005c0d0f, 0x005c2d0f, 0x005c4d0f, 0x005c6d0f,
    0x005c8d0f, 0x005cad0f, 0x005ccd0f, 0x005ced0f,
    0x005d0d0f, 0x005d2d0f, 0x005d4d0f, 0x005d6d0f,
    0x005d8d0f, 0x005dad0f, 0x005dcd0f, 0x005ded0f,
    0x005e0d0f, 0x005e2d0f, 0x005e4d0f, 0x005e6d0f,
    0x005e8d0f, 0x005ead0f, 0x005ecd0f, 0x005eed0f,
    0x005f0d0f, 0x005f2d0f, 0x005f4d0f, 0x005f6d0f,
    0x005f8d0f, 0x005fad0f, 0x005fcd0f, 0x005fed0f,
    0x00600d0f, 0x00602d0f, 0x00604d0f, 0x00606d0f,
    0x00608d0f, 0x0060ad0f, 0x0060cd0f, 0x0060ed0f,
    0x00610d0f, 0x00612d0f, 0x00614d0f, 0x00616d0f,
    0x00618d0f, 0x0061ad0f, 0x0061cd0f, 0x0061ed0f,
    0x00620d0f, 0x00622d0f, 0x00624d0f, 0x00626d0f,
    0x00628d0f, 0x0062ad0f, 0x0062cd0f, 0x0062ed0f,
    0x00630d0f, 0x00632d0f, 0x00634d0f, 0x00636d0f,
    0x00638d0f, 0x0063ad0f, 0x0063cd0f, 0x0063ed0f,
    0x00640d0f, 0x00642d0f, 0x00644d0f, 0x00646d0f,
    0x00648d0f, 0x0064ad0f, 0x0064cd0f, 0x0064ed0f,
    0x00650d0f, 0x00652d0f, 0x00654d0f, 0x00656d0f,
    0x00658d0f, 0x0065ad0f, 0x0065cd0f, 0x0065ed0f,
    0x00660d0f, 0x00662d0f, 0x00664d0f, 0x00666d0f,
    0x00668d0f, 0x0066ad0f, 0x0066cd0f, 0x0066ed0f,
    0x00670d0f, 0x00672d0f, 0x00674d0f, 0x00676d0f,
    0x00678d0f, 0x0067ad0f, 0x0067cd0f, 0x0067ed0f,
    0x00680d0f, 0x00682d0f, 0x00684d0f, 0x00686d0f,
    0x00688d0f, 0x0068ad0f, 0x0068cd0f, 0x0068ed0f,
    0x00690d0f, 0x00692d0f, 0x00694d0f, 0x00696d0f,
    0x00698d0f, 0x0069ad0f, 0x0069cd0f, 0x0069ed0f,
    0x006a0d0f, 0x006a2d0f, 0x006a4d0f, 0x006a6d0f,
    0x006a8d0f, 0x006aad0f, 0x006acd0f, 0x006aed0f,
    0x006b0d0f, 0x006b2d0f, 0x006b4d0f, 0x006b6d0f,
    0x006b8d0f, 0x006bad0f, 0x006bcd0f, 0x006bed0f,
    0x006c0d0f, 0x006c2d0f, 0x006c4d0f, 0x006c6d0f,
    0x006c8d0f, 0x006cad0f, 0x006ccd0f, 0x006ced0f,
    0x006d0d0f, 0x006d2d0f, 0x006d4d0f, 0x006d6d0f,
    0x006d8d0f, 0x006dad0f, 0x006dcd0f, 0x006ded0f,
    0x006e0d0f, 0x006e2d0f, 0x006e4d0f, 0x006e6d0f,
    0x006e8d0f, 0x006ead0f, 0x006ecd0f, 0x006eed0f,
    0x006f0d0f, 0x006f2d0f, 0x006f4d0f, 0x006f6d0f,
    0x006f8d0f, 0x006fad0f, 0x006fcd0f, 0x006fed0f,
    0x00700d0f, 0x00702d0f, 0x00704d0f, 0x00706d0f,
    0x00708d0f, 0x0070ad0f, 0x0070cd0f, 0x0070ed0f,
    0x00710d0f, 0x00712d0f, 0x00714d0f, 0x00716d0f,
    0x00718d0f, 0x0071ad0f, 0x0071cd0f, 0x0071ed0f,
    0x00720d0f, 0x00722d0f, 0x00724d0f, 0x00726d0f,
    0x00728d0f, 0x0072ad0f, 0x0072cd0f, 0x0072ed0f,
    0x00730d0f, 0x00732d0f, 0x00734d0f, 0x00736d0f,
    0x00738d0f, 0x0073ad0f, 0x0073cd0f, 0x0073ed0f,
    0x00740d0f, 0x00742d0f, 0x00744d0f, 0x00746d0f,
    0x00748d0f, 0x0074ad0f, 0x0074cd0f, 0x0074ed0f,
    0x00750d0f, 0x00752d0f, 0x00754d0f, 0x00756d0f,
    0x00758d0f, 0x0075ad0f, 0x0075cd0f, 0x0075ed0f,
    0x00760d0f, 0x00762d0f, 0x00764d0f, 0x00766d0f,
    0x00768d0f, 0x0076ad0f, 0x0076cd0f, 0x0076ed0f,
    0x00770d0f, 0x00772d0f, 0x00774d0f, 0x00776d0f,
    0x00778d0f, 0x0077ad0f, 0x0077cd0f, 0x0077ed0f,
    0x00780d0f, 0x00782d0f, 0x00784d0f, 0x00786d0f,
    0x00788d0f, 0x0078ad0f, 0x0078cd0f, 0x0078ed0f,
    0x00790d0f, 0x00792d0f, 0x00794d0f, 0x00796d0f,
    0x00798d0f, 0x0079ad0f, 0x0079cd0f, 0x0079ed0f,
    0x007a0d0f, 0x007a2d0f, 0x007a4d0f, 0x007a6d0f,
    0x007a8d0f, 0x007aad0f, 0x007acd0f, 0x007aed0f,
    0x007b0d0f, 0x007b2d0f, 0x007b4d0f, 0x007b6d0f,
    0x007b8d0f, 0x007bad0f, 0x007bcd0f, 0x007bed0f,
    0x007c0d0f, 0x007c2d0f, 0x007c4d0f, 0x007c6d0f,
    0x007c8d0f, 0x007cad0f, 0x007ccd0f, 0x007ced0f,
    0x007d0d0f, 0x007d2d0f, 0x007d4d0f, 0x007d6d0f,
    0x007d8d0f, 0x007dad0f, 0x007dcd0f, 0x007ded0f,
    0x007e0d0f, 0x007e2d0f, 0x007e4d0f, 0x007e6d0f,
    0x007e8d0f, 0x007ead0f, 0x007ecd0f, 0x007eed0f,
    0x007f0d0f, 0x007f2d0f, 0x007f4d0f, 0x007f6d0f,
    0x007f8d0f, 0x007fad0f, 0x007fcd0f, 0x007fed0f,
    0x00001d0f, 0x00003d0f, 0x00005d0f, 0x00007d0f,
    0x00009d0f, 0x0000bd0f, 0x0000dd0f, 0x0000fd0f,
    0x00011d0f, 0x00013d0f, 0x00015d0f, 0x00017d0f,
    0x00019d0f, 0x0001bd0f, 0x0001dd0f, 0x0001fd0f,
    0x00021d0f, 0x00023d0f, 0x00025d0f, 0x00027d0f,
    0x00029d0f, 0x0002bd0f, 0x0002dd0f, 0x0002fd0f,
    0x00031d0f, 0x00033d0f, 0x00035d0f, 0x00037d0f,
    0x00039d0f, 0x0003bd0f, 0x0003dd0f, 0x0003fd0f,
    0x00041d0f, 0x00043d0f, 0x00045d0f, 0x00047d0f,
    0x00049d0f, 0x0004bd0f, 0x0004dd0f, 0x0004fd0f,
    0x00051d0f, 0x00053d0f, 0x00055d0f, 0x00057d0f,
    0x00059d0f, 0x0005bd0f, 0x0005dd0f, 0x0005fd0f,
    0x00061d0f, 0x00063d0f, 0x00065d0f, 0x00067d0f,
    0x00069d0f, 0x0006bd0f, 0x0006dd0f, 0x0006fd0f,
    0x00071d0f, 0x00073d0f, 0x00075d0f, 0x00077d0f,
    0x00079d0f, 0x0007bd0f, 0x0007dd0f, 0x0007fd0f,
    0x00081d0f, 0x00083d0f, 0x00085d0f, 0x00087d0f,
    0x00089d0f, 0x0008bd0f, 0x0008dd0f, 0x0008fd0f,
    0x00091d0f, 0x00093d0f, 0x00095d0f, 0x00097d0f,
    0x00099d0f, 0x0009bd0f, 0x0009dd0f, 0x0009fd0f,
    0x000a1d0f, 0x000a3d0f, 0x000a5d0f, 0x000a7d0f,
    0x000a9d0f, 0x000abd0f, 0x000add0f, 0x000afd0f,
    0x000b1d0f, 0x000b3d0f, 0x000b5d0f, 0x000b7d0f,
    0x000b9d0f, 0x000bbd0f, 0x000bdd0f, 0x000bfd0f,
    0x000c1d0f, 0x000c3d0f, 0x000c5d0f, 0x000c7d0f,
    0x000c9d0f, 0x000cbd0f, 0x000cdd0f, 0x000cfd0f,
    0x000d1d0f, 0x000d3d0f, 0x000d5d0f, 0x000d7d0f,
    0x000d9d0f, 0x000dbd0f, 0x000ddd0f, 0x000dfd0f,
    0x000e1d0f, 0x000e3d0f, 0x000e5d0f, 0x000e7d0f,
    0x000e9d0f, 0x000ebd0f, 0x000edd0f, 0x000efd0f,
    0x000f1d0f, 0x000f3d0f, 0x000f5d0f, 0x000f7d0f,
    0x000f9d0f, 0x000fbd0f, 0x000fdd0f, 0x000ffd0f,
    0x00101d0f, 0x00103d0f, 0x00105d0f, 0x00107d0f,
    0x00109d0f, 0x0010bd0f, 0x0010dd0f, 0x0010fd0f,
    0x00111d0f, 0x00113d0f, 0x00115d0f, 0x00117d0f,
    0x00119d0f, 0x0011bd0f, 0x0011dd0f, 0x0011fd0f,
    0x00121d0f, 0x00123d0f, 0x00125d0f, 0x00127d0f,
    0x00129d0f, 0x0012bd0f, 0x0012dd0f, 0x0012fd0f,
    0x00131d0f, 0x00133d0f, 0x00135d0f, 0x00137d0f,
    0x00139d0f, 0x0013bd0f, 0x0013dd0f, 0x0013fd0f,
    0x00141d0f, 0x00143d0f, 0x00145d0f, 0x00147d0f,
    0x00149d0f, 0x0014bd0f, 0x0014dd0f, 0x0014fd0f,
    0x00151d0f, 0x00153d0f, 0x00155d0f, 0x00157d0f,
    0x00159d0f, 0x0015bd0f, 0x0015dd0f, 0x0015fd0f,
    0x00161d0f, 0x00163d0f, 0x00165d0f, 0x00167d0f,
    0x00169d0f, 0x0016bd0f, 0x0016dd0f, 0x0016fd0f,
    0x00171d0f, 0x00173d0f, 0x00175d0f, 0x00177d0f,
    0x00179d0f, 0x0017bd0f, 0x0017dd0f, 0x0017fd0f,
    0x00181d0f, 0x00183d0f, 0x00185d0f, 0x00187d0f,
    0x00189d0f, 0x0018bd0f, 0x0018dd0f, 0x0018fd0f,
    0x00191d0f, 0x00193d0f, 0x00195d0f, 0x00197d0f,
    0x00199d0f, 0x0019bd0f, 0x0019dd0f, 0x0019fd0f,
    0x001a1d0f, 0x001a3d0f, 0x001a5d0f, 0x001a7d0f,
    0x001a9d0f, 0x001abd0f, 0x001add0f, 0x001afd0f,
    0x001b1d0f, 0x001b3d0f, 0x001b5d0f, 0x001b7d0f,
    0x001b9d0f, 0x001bbd0f, 0x001bdd0f, 0x001bfd0f,
    0x001c1d0f, 0x001c3d0f, 0x001c5d0f, 0x001c7d0f,
    0x001c9d0f, 0x001cbd0f, 0x001cdd0f, 0x001cfd0f,
    0x001d1d0f, 0x001d3d0f, 0x001d5d0f, 0x001d7d0f,
    0x001d9d0f, 0x001dbd0f, 0x001ddd0f, 0x001dfd0f,
    0x001e1d0f, 0x001e3d0f, 0x001e5d0f, 0x001e7d0f,
    0x001e9d0f, 0x001ebd0f, 0x001edd0f, 0x001efd0f,
    0x001f1d0f, 0x001f3d0f, 0x001f5d0f, 0x001f7d0f,
    0x001f9d0f, 0x001fbd0f, 0x001fdd0f, 0x001ffd0f,
    0x00201d0f, 0x00203d0f, 0x00205d0f, 0x00207d0f,
    0x00209d0f, 0x0020bd0f, 0x0020dd0f, 0x0020fd0f,
    0x00211d0f, 0x00213d0f, 0x00215d0f, 0x00217d0f,
    0x00219d0f, 0x0021bd0f, 0x0021dd0f, 0x0021fd0f,
    0x00221d0f, 0x00223d0f, 0x00225d0f, 0x00227d0f,
    0x00229d0f, 0x0022bd0f, 0x0022dd0f, 0x0022fd0f,
    0x00231d0f, 0x00233d0f, 0x00235d0f, 0x00237d0f,
    0x00239d0f, 0x0023bd0f, 0x0023dd0f, 0x0023fd0f,
    0x00241d0f, 0x00243d0f, 0x00245d0f, 0x00247d0f,
    0x00249d0f, 0x0024bd0f, 0x0024dd0f, 0x0024fd0f,
    0x00251d0f, 0x00253d0f, 0x00255d0f, 0x00257d0f,
    0x00259d0f, 0x0025bd0f, 0x0025dd0f, 0x0025fd0f,
    0x00261d0f, 0x00263d0f, 0x00265d0f, 0x00267d0f,
    0x00269d0f, 0x0026bd0f, 0x0026dd0f, 0x0026fd0f,
    0x00271d0f, 0x00273d0f, 0x00275d0f, 0x00277d0f,
    0x00279d0f, 0x0027bd0f, 0x0027dd0f, 0x0027fd0f,
    0x00281d0f, 0x00283d0f, 0x00285d0f, 0x00287d0f,
    0x00289d0f, 0x0028bd0f, 0x0028dd0f, 0x0028fd0f,
    0x00291d0f, 0x00293d0f, 0x00295d0f, 0x00297d0f,
    0x00299d0f, 0x0029bd0f, 0x0029dd0f, 0x0029fd0f,
    0x002a1d0f, 0x002a3d0f, 0x002a5d0f, 0x002a7d0f,
    0x002a9d0f, 0x002abd0f, 0x002add0f, 0x002afd0f,
    0x002b1d0f, 0x002b3d0f, 0x002b5d0f, 0x002b7d0f,
    0x002b9d0f, 0x002bbd0f, 0x002bdd0f, 0x002bfd0f,
    0x002c1d0f, 0x002c3d0f, 0x002c5d0f, 0x002c7d0f,
    0x002c9d0f, 0x002cbd0f, 0x002cdd0f, 0x002cfd0f,
    0x002d1d0f, 0x002d3d0f, 0x002d5d0f, 0x002d7d0f,
    0x002d9d0f, 0x002dbd0f, 0x002ddd0f, 0x002dfd0f,
    0x002e1d0f, 0x002e3d0f, 0x002e5d0f, 0x002e7d0f,
    0x002e9d0f, 0x002ebd0f, 0x002edd0f, 0x002efd0f,
    0x002f1d0f, 0x002f3d0f, 0x002f5d0f, 0x002f7d0f,
    0x002f9d0f, 0x002fbd0f, 0x002fdd0f, 0x002ffd0f,
    0x00301d0f, 0x00303d0f, 0x00305d0f, 0x00307d0f,
    0x00309d0f, 0x0030bd0f, 0x0030dd0f, 0x0030fd0f,
    0x00311d0f, 0x00313d0f, 0x00315d0f, 0x00317d0f,
    0x00319d0f, 0x0031bd0f, 0x0031dd0f, 0x0031fd0f,
    0x00321d0f, 0x00323d0f, 0x00325d0f, 0x00327d0f,
    0x00329d0f, 0x0032bd0f, 0x0032dd0f, 0x0032fd0f,
    0x00331d0f, 0x00333d0f, 0x00335d0f, 0x00337d0f,
    0x00339d0f, 0x0033bd0f, 0x0033dd0f, 0x0033fd0f,
    0x00341d0f, 0x00343d0f, 0x00345d0f, 0x00347d0f,
    0x00349d0f, 0x0034bd0f, 0x0034dd0f, 0x0034fd0f,
    0x00351d0f, 0x00353d0f, 0x00355d0f, 0x00357d0f,
    0x00359d0f, 0x0035bd0f, 0x0035dd0f, 0x0035fd0f,
    0x00361d0f, 0x00363d0f, 0x00365d0f, 0x00367d0f,
    0x00369d0f, 0x0036bd0f, 0x0036dd0f, 0x0036fd0f,
    0x00371d0f, 0x00373d0f, 0x00375d0f, 0x00377d0f,
    0x00379d0f, 0x0037bd0f, 0x0037dd0f, 0x0037fd0f,
    0x00381d0f, 0x00383d0f, 0x00385d0f, 0x00387d0f,
    0x00389d0f, 0x0038bd0f, 0x0038dd0f, 0x0038fd0f,
    0x00391d0f, 0x00393d0f, 0x00395d0f, 0x00397d0f,
    0x00399d0f, 0x0039bd0f, 0x0039dd0f, 0x0039fd0f,
    0x003a1d0f, 0x003a3d0f, 0x003a5d0f, 0x003a7d0f,
    0x003a9d0f, 0x003abd0f, 0x003add0f, 0x003afd0f,
    0x003b1d0f, 0x003b3d0f, 0x003b5d0f, 0x003b7d0f,
    0x003b9d0f, 0x003bbd0f, 0x003bdd0f, 0x003bfd0f,
    0x003c1d0f, 0x003c3d0f, 0x003c5d0f, 0x003c7d0f,
    0x003c9d0f, 0x003cbd0f, 0x003cdd0f, 0x003cfd0f,
    0x003d1d0f, 0x003d3d0f, 0x003d5d0f, 0x003d7d0f,
    0x003d9d0f, 0x003dbd0f, 0x003ddd0f, 0x003dfd0f,
    0x003e1d0f, 0x003e3d0f, 0x003e5d0f, 0x003e7d0f,
    0x003e9d0f, 0x003ebd0f, 0x003edd0f, 0x003efd0f,
    0x003f1d0f, 0x003f3d0f, 0x003f5d0f, 0x003f7d0f,
    0x003f9d0f, 0x003fbd0f, 0x003fdd0f, 0x003ffd0f,
    0x00401d0f, 0x00403d0f, 0x00405d0f, 0x00407d0f,
    0x00409d0f, 0x0040bd0f, 0x0040dd0f, 0x0040fd0f,
    0x00411d0f, 0x00413d0f, 0x00415d0f, 0x00417d0f,
    0x00419d0f, 0x0041bd0f, 0x0041dd0f, 0x0041fd0f,
    0x00421d0f, 0x00423d0f, 0x00425d0f, 0x00427d0f,
    0x00429d0f, 0x0042bd0f, 0x0042dd0f, 0x0042fd0f,
    0x00431d0f, 0x00433d0f, 0x00435d0f, 0x00437d0f,
    0x00439d0f, 0x0043bd0f, 0x0043dd0f, 0x0043fd0f,
    0x00441d0f, 0x00443d0f, 0x00445d0f, 0x00447d0f,
    0x00449d0f, 0x0044bd0f, 0x0044dd0f, 0x0044fd0f,
    0x00451d0f, 0x00453d0f, 0x00455d0f, 0x00457d0f,
    0x00459d0f, 0x0045bd0f, 0x0045dd0f, 0x0045fd0f,
    0x00461d0f, 0x00463d0f, 0x00465d0f, 0x00467d0f,
    0x00469d0f, 0x0046bd0f, 0x0046dd0f, 0x0046fd0f,
    0x00471d0f, 0x00473d0f, 0x00475d0f, 0x00477d0f,
    0x00479d0f, 0x0047bd0f, 0x0047dd0f, 0x0047fd0f,
    0x00481d0f, 0x00483d0f, 0x00485d0f, 0x00487d0f,
    0x00489d0f, 0x0048bd0f, 0x0048dd0f, 0x0048fd0f,
    0x00491d0f, 0x00493d0f, 0x00495d0f, 0x00497d0f,
    0x00499d0f, 0x0049bd0f, 0x0049dd0f, 0x0049fd0f,
    0x004a1d0f, 0x004a3d0f, 0x004a5d0f, 0x004a7d0f,
    0x004a9d0f, 0x004abd0f, 0x004add0f, 0x004afd0f,
    0x004b1d0f, 0x004b3d0f, 0x004b5d0f, 0x004b7d0f,
    0x004b9d0f, 0x004bbd0f, 0x004bdd0f, 0x004bfd0f,
    0x004c1d0f, 0x004c3d0f, 0x004c5d0f, 0x004c7d0f,
    0x004c9d0f, 0x004cbd0f, 0x004cdd0f, 0x004cfd0f,
    0x004d1d0f, 0x004d3d0f, 0x004d5d0f, 0x004d7d0f,
    0x004d9d0f, 0x004dbd0f, 0x004ddd0f, 0x004dfd0f,
    0x004e1d0f, 0x004e3d0f, 0x004e5d0f, 0x004e7d0f,
    0x004e9d0f, 0x004ebd0f, 0x004edd0f, 0x004efd0f,
    0x004f1d0f, 0x004f3d0f, 0x004f5d0f, 0x004f7d0f,
    0x004f9d0f, 0x004fbd0f, 0x004fdd0f, 0x004ffd0f,
    0x00501d0f, 0x00503d0f, 0x00505d0f, 0x00507d0f,
    0x00509d0f, 0x0050bd0f, 0x0050dd0f, 0x0050fd0f,
    0x00511d0f, 0x00513d0f, 0x00515d0f, 0x00517d0f,
    0x00519d0f, 0x0051bd0f, 0x0051dd0f, 0x0051fd0f,
    0x00521d0f, 0x00523d0f, 0x00525d0f, 0x00527d0f,
    0x00529d0f, 0x0052bd0f, 0x0052dd0f, 0x0052fd0f,
    0x00531d0f, 0x00533d0f, 0x00535d0f, 0x00537d0f,
    0x00539d0f, 0x0053bd0f, 0x0053dd0f, 0x0053fd0f,
    0x00541d0f, 0x00543d0f, 0x00545d0f, 0x00547d0f,
    0x00549d0f, 0x0054bd0f, 0x0054dd0f, 0x0054fd0f,
    0x00551d0f, 0x00553d0f, 0x00555d0f, 0x00557d0f,
    0x00559d0f, 0x0055bd0f, 0x0055dd0f, 0x0055fd0f,
    0x00561d0f, 0x00563d0f, 0x00565d0f, 0x00567d0f,
    0x00569d0f, 0x0056bd0f, 0x0056dd0f, 0x0056fd0f,
    0x00571d0f, 0x00573d0f, 0x00575d0f, 0x00577d0f,
    0x00579d0f, 0x0057bd0f, 0x0057dd0f, 0x0057fd0f,
    0x00581d0f, 0x00583d0f, 0x00585d0f, 0x00587d0f,
    0x00589d0f, 0x0058bd0f, 0x0058dd0f, 0x0058fd0f,
    0x00591d0f, 0x00593d0f, 0x00595d0f, 0x00597d0f,
    0x00599d0f, 0x0059bd0f, 0x0059dd0f, 0x0059fd0f,
    0x005a1d0f, 0x005a3d0f, 0x005a5d0f, 0x005a7d0f,
    0x005a9d0f, 0x005abd0f, 0x005add0f, 0x005afd0f,
    0x005b1d0f, 0x005b3d0f, 0x005b5d0f, 0x005b7d0f,
    0x005b9d0f, 0x005bbd0f, 0x005bdd0f, 0x005bfd0f,
    0x005c1d0f, 0x005c3d0f, 0x005c5d0f, 0x005c7d0f,
    0x005c9d0f, 0x005cbd0f, 0x005cdd0f, 0x005cfd0f,
    0x005d1d0f, 0x005d3d0f, 0x005d5d0f, 0x005d7d0f,
    0x005d9d0f, 0x005dbd0f, 0x005ddd0f, 0x005dfd0f,
    0x005e1d0f, 0x005e3d0f, 0x005e5d0f, 0x005e7d0f,
    0x005e9d0f, 0x005ebd0f, 0x005edd0f, 0x005efd0f,
    0x005f1d0f, 0x005f3d0f, 0x005f5d0f, 0x005f7d0f,
    0x005f9d0f, 0x005fbd0f, 0x005fdd0f, 0x005ffd0f,
    0x00601d0f, 0x00603d0f, 0x00605d0f, 0x00607d0f,
    0x00609d0f, 0x0060bd0f, 0x0060dd0f, 0x0060fd0f,
    0x00611d0f, 0x00613d0f, 0x00615d0f, 0x00617d0f,
    0x00619d0f, 0x0061bd0f, 0x0061dd0f, 0x0061fd0f,
    0x00621d0f, 0x00623d0f, 0x00625d0f, 0x00627d0f,
    0x00629d0f, 0x0062bd0f, 0x0062dd0f, 0x0062fd0f,
    0x00631d0f, 0x00633d0f, 0x00635d0f, 0x00637d0f,
    0x00639d0f, 0x0063bd0f, 0x0063dd0f, 0x0063fd0f,
    0x00641d0f, 0x00643d0f, 0x00645d0f, 0x00647d0f,
    0x00649d0f, 0x0064bd0f, 0x0064dd0f, 0x0064fd0f,
    0x00651d0f, 0x00653d0f, 0x00655d0f, 0x00657d0f,
    0x00659d0f, 0x0065bd0f, 0x0065dd0f, 0x0065fd0f,
    0x00661d0f, 0x00663d0f, 0x00665d0f, 0x00667d0f,
    0x00669d0f, 0x0066bd0f, 0x0066dd0f, 0x0066fd0f,
    0x00671d0f, 0x00673d0f, 0x00675d0f, 0x00677d0f,
    0x00679d0f, 0x0067bd0f, 0x0067dd0f, 0x0067fd0f,
    0x00681d0f, 0x00683d0f, 0x00685d0f, 0x00687d0f,
    0x00689d0f, 0x0068bd0f, 0x0068dd0f, 0x0068fd0f,
    0x00691d0f, 0x00693d0f, 0x00695d0f, 0x00697d0f,
    0x00699d0f, 0x0069bd0f, 0x0069dd0f, 0x0069fd0f,
    0x006a1d0f, 0x006a3d0f, 0x006a5d0f, 0x006a7d0f,
    0x006a9d0f, 0x006abd0f, 0x006add0f, 0x006afd0f,
    0x006b1d0f, 0x006b3d0f, 0x006b5d0f, 0x006b7d0f,
    0x006b9d0f, 0x006bbd0f, 0x006bdd0f, 0x006bfd0f,
    0x006c1d0f, 0x006c3d0f, 0x006c5d0f, 0x006c7d0f,
    0x006c9d0f, 0x006cbd0f, 0x006cdd0f, 0x006cfd0f,
    0x006d1d0f, 0x006d3d0f, 0x006d5d0f, 0x006d7d0f,
    0x006d9d0f, 0x006dbd0f, 0x006ddd0f, 0x006dfd0f,
    0x006e1d0f, 0x006e3d0f, 0x006e5d0f, 0x006e7d0f,
    0x006e9d0f, 0x006ebd0f, 0x006edd0f, 0x006efd0f,
    0x006f1d0f, 0x006f3d0f, 0x006f5d0f, 0x006f7d0f,
    0x006f9d0f, 0x006fbd0f, 0x006fdd0f, 0x006ffd0f,
    0x00701d0f, 0x00703d0f, 0x00705d0f, 0x00707d0f,
    0x00709d0f, 0x0070bd0f, 0x0070dd0f, 0x0070fd0f,
    0x00711d0f, 0x00713d0f, 0x00715d0f, 0x00717d0f,
    0x00719d0f, 0x0071bd0f, 0x0071dd0f, 0x0071fd0f,
    0x00721d0f, 0x00723d0f, 0x00725d0f, 0x00727d0f,
    0x00729d0f, 0x0072bd0f, 0x0072dd0f, 0x0072fd0f,
    0x00731d0f, 0x00733d0f, 0x00735d0f, 0x00737d0f,
    0x00739d0f, 0x0073bd0f, 0x0073dd0f, 0x0073fd0f,
    0x00741d0f, 0x00743d0f, 0x00745d0f, 0x00747d0f,
    0x00749d0f, 0x0074bd0f, 0x0074dd0f, 0x0074fd0f,
    0x00751d0f, 0x00753d0f, 0x00755d0f, 0x00757d0f,
    0x00759d0f, 0x0075bd0f, 0x0075dd0f, 0x0075fd0f,
    0x00761d0f, 0x00763d0f, 0x00765d0f, 0x00767d0f,
    0x00769d0f, 0x0076bd0f, 0x0076dd0f, 0x0076fd0f,
    0x00771d0f, 0x00773d0f, 0x00775d0f, 0x00777d0f,
    0x00779d0f, 0x0077bd0f, 0x0077dd0f, 0x0077fd0f,
    0x00781d0f, 0x00783d0f, 0x00785d0f, 0x00787d0f,
    0x00789d0f, 0x0078bd0f, 0x0078dd0f, 0x0078fd0f,
    0x00791d0f, 0x00793d0f, 0x00795d0f, 0x00797d0f,
    0x00799d0f, 0x0079bd0f, 0x0079dd0f, 0x0079fd0f,
    0x007a1d0f, 0x007a3d0f, 0x007a5d0f, 0x007a7d0f,
    0x007a9d0f, 0x007abd0f, 0x007add0f, 0x007afd0f,
    0x007b1d0f, 0x007b3d0f, 0x007b5d0f, 0x007b7d0f,
    0x007b9d0f, 0x007bbd0f, 0x007bdd0f, 0x007bfd0f,
    0x007c1d0f, 0x007c3d0f, 0x007c5d0f, 0x007c7d0f,
    0x007c9d0f, 0x007cbd0f, 0x007cdd0f, 0x007cfd0f,
    0x007d1d0f, 0x007d3d0f, 0x007d5d0f, 0x007d7d0f,
    0x007d9d0f, 0x007dbd0f, 0x007ddd0f, 0x007dfd0f,
    0x007e1d0f, 0x007e3d0f, 0x007e5d0f, 0x007e7d0f,
    0x007e9d0f, 0x007ebd0f, 0x007edd0f, 0x007efd0f,
    0x007f1d0f, 0x007f3d0f, 0x007f5d0f, 0x007f7d0f,
    0x007f9d0f, 0x007fbd0f, 0x007fdd0f, 0x007ffd0f,
    0x00000310, 0x00002310, 0x00004310, 0x00006310,
    0x00008310, 0x0000a310, 0x0000c310, 0x0000e310,
    0x00010310, 0x00012310, 0x00014310, 0x00016310,
    0x00018310, 0x0001a310, 0x0001c310, 0x0001e310,
    0x00020310, 0x00022310, 0x00024310, 0x00026310,
    0x00028310, 0x0002a310, 0x0002c310, 0x0002e310,
    0x00030310, 0x00032310, 0x00034310, 0x00036310,
    0x00038310, 0x0003a310, 0x0003c310, 0x0003e310,
    0x00040310, 0x00042310, 0x00044310, 0x00046310,
    0x00048310, 0x0004a310, 0x0004c310, 0x0004e310,
    0x00050310, 0x00052310, 0x00054310, 0x00056310,
    0x00058310, 0x0005a310, 0x0005c310, 0x0005e310,
    0x00060310, 0x00062310, 0x00064310, 0x00066310,
    0x00068310, 0x0006a310, 0x0006c310, 0x0006e310,
    0x00070310, 0x00072310, 0x00074310, 0x00076310,
    0x00078310, 0x0007a310, 0x0007c310, 0x0007e310,
    0x00080310, 0x00082310, 0x00084310, 0x00086310,
    0x00088310, 0x0008a310, 0x0008c310, 0x0008e310,
    0x00090310, 0x00092310, 0x00094310, 0x00096310,
    0x00098310, 0x0009a310, 0x0009c310, 0x0009e310,
    0x000a0310, 0x000a2310, 0x000a4310, 0x000a6310,
    0x000a8310, 0x000aa310, 0x000ac310, 0x000ae310,
    0x000b0310, 0x000b2310, 0x000b4310, 0x000b6310,
    0x000b8310, 0x000ba310, 0x000bc310, 0x000be310,
    0x000c0310, 0x000c2310, 0x000c4310, 0x000c6310,
    0x000c8310, 0x000ca310, 0x000cc310, 0x000ce310,
    0x000d0310, 0x000d2310, 0x000d4310, 0x000d6310,
    0x000d8310, 0x000da310, 0x000dc310, 0x000de310,
    0x000e0310, 0x000e2310, 0x000e4310, 0x000e6310,
    0x000e8310, 0x000ea310, 0x000ec310, 0x000ee310,
    0x000f0310, 0x000f2310, 0x000f4310, 0x000f6310,
    0x000f8310, 0x000fa310, 0x000fc310, 0x000fe310,
    0x00100310, 0x00102310, 0x00104310, 0x00106310,
    0x00108310, 0x0010a310, 0x0010c310, 0x0010e310,
    0x00110310, 0x00112310, 0x00114310, 0x00116310,
    0x00118310, 0x0011a310, 0x0011c310, 0x0011e310,
    0x00120310, 0x00122310, 0x00124310, 0x00126310,
    0x00128310, 0x0012a310, 0x0012c310, 0x0012e310,
    0x00130310, 0x00132310, 0x00134310, 0x00136310,
    0x00138310, 0x0013a310, 0x0013c310, 0x0013e310,
    0x00140310, 0x00142310, 0x00144310, 0x00146310,
    0x00148310, 0x0014a310, 0x0014c310, 0x0014e310,
    0x00150310, 0x00152310, 0x00154310, 0x00156310,
    0x00158310, 0x0015a310, 0x0015c310, 0x0015e310,
    0x00160310, 0x00162310, 0x00164310, 0x00166310,
    0x00168310, 0x0016a310, 0x0016c310, 0x0016e310,
    0x00170310, 0x00172310, 0x00174310, 0x00176310,
    0x00178310, 0x0017a310, 0x0017c310, 0x0017e310,
    0x00180310, 0x00182310, 0x00184310, 0x00186310,
    0x00188310, 0x0018a310, 0x0018c310, 0x0018e310,
    0x00190310, 0x00192310, 0x00194310, 0x00196310,
    0x00198310, 0x0019a310, 0x0019c310, 0x0019e310,
    0x001a0310, 0x001a2310, 0x001a4310, 0x001a6310,
    0x001a8310, 0x001aa310, 0x001ac310, 0x001ae310,
    0x001b0310, 0x001b2310, 0x001b4310, 0x001b6310,
    0x001b8310, 0x001ba310, 0x001bc310, 0x001be310,
    0x001c0310, 0x001c2310, 0x001c4310, 0x001c6310,
    0x001c8310, 0x001ca310, 0x001cc310, 0x001ce310,
    0x001d0310, 0x001d2310, 0x001d4310, 0x001d6310,
    0x001d8310, 0x001da310, 0x001dc310, 0x001de310,
    0x001e0310, 0x001e2310, 0x001e4310, 0x001e6310,
    0x001e8310, 0x001ea310, 0x001ec310, 0x001ee310,
    0x001f0310, 0x001f2310, 0x001f4310, 0x001f6310,
    0x001f8310, 0x001fa310, 0x001fc310, 0x001fe310,
    0x00200310, 0x00202310, 0x00204310, 0x00206310,
    0x00208310, 0x0020a310, 0x0020c310, 0x0020e310,
    0x00210310, 0x00212310, 0x00214310, 0x00216310,
    0x00218310, 0x0021a310, 0x0021c310, 0x0021e310,
    0x00220310, 0x00222310, 0x00224310, 0x00226310,
    0x00228310, 0x0022a310, 0x0022c310, 0x0022e310,
    0x00230310, 0x00232310, 0x00234310, 0x00236310,
    0x00238310, 0x0023a310, 0x0023c310, 0x0023e310,
    0x00240310, 0x00242310, 0x00244310, 0x00246310,
    0x00248310, 0x0024a310, 0x0024c310, 0x0024e310,
    0x00250310, 0x00252310, 0x00254310, 0x00256310,
    0x00258310, 0x0025a310, 0x0025c310, 0x0025e310,
    0x00260310, 0x00262310, 0x00264310, 0x00266310,
    0x00268310, 0x0026a310, 0x0026c310, 0x0026e310,
    0x00270310, 0x00272310, 0x00274310, 0x00276310,
    0x00278310, 0x0027a310, 0x0027c310, 0x0027e310,
    0x00280310, 0x00282310, 0x00284310, 0x00286310,
    0x00288310, 0x0028a310, 0x0028c310, 0x0028e310,
    0x00290310, 0x00292310, 0x00294310, 0x00296310,
    0x00298310, 0x0029a310, 0x0029c310, 0x0029e310,
    0x002a0310, 0x002a2310, 0x002a4310, 0x002a6310,
    0x002a8310, 0x002aa310, 0x002ac310, 0x002ae310,
    0x002b0310, 0x002b2310, 0x002b4310, 0x002b6310,
    0x002b8310, 0x002ba310, 0x002bc310, 0x002be310,
    0x002c0310, 0x002c2310, 0x002c4310, 0x002c6310,
    0x002c8310, 0x002ca310, 0x002cc310, 0x002ce310,
    0x002d0310, 0x002d2310, 0x002d4310, 0x002d6310,
    0x002d8310, 0x002da310, 0x002dc310, 0x002de310,
    0x002e0310, 0x002e2310, 0x002e4310, 0x002e6310,
    0x002e8310, 0x002ea310, 0x002ec310, 0x002ee310,
    0x002f0310, 0x002f2310, 0x002f4310, 0x002f6310,
    0x002f8310, 0x002fa310, 0x002fc310, 0x002fe310,
    0x00300310, 0x00302310, 0x00304310, 0x00306310,
    0x00308310, 0x0030a310, 0x0030c310, 0x0030e310,
    0x00310310, 0x00312310, 0x00314310, 0x00316310,
    0x00318310, 0x0031a310, 0x0031c310, 0x0031e310,
    0x00320310, 0x00322310, 0x00324310, 0x00326310,
    0x00328310, 0x0032a310, 0x0032c310, 0x0032e310,
    0x00330310, 0x00332310, 0x00334310, 0x00336310,
    0x00338310, 0x0033a310, 0x0033c310, 0x0033e310,
    0x00340310, 0x00342310, 0x00344310, 0x00346310,
    0x00348310, 0x0034a310, 0x0034c310, 0x0034e310,
    0x00350310, 0x00352310, 0x00354310, 0x00356310,
    0x00358310, 0x0035a310, 0x0035c310, 0x0035e310,
    0x00360310, 0x00362310, 0x00364310, 0x00366310,
    0x00368310, 0x0036a310, 0x0036c310, 0x0036e310,
    0x00370310, 0x00372310, 0x00374310, 0x00376310,
    0x00378310, 0x0037a310, 0x0037c310, 0x0037e310,
    0x00380310, 0x00382310, 0x00384310, 0x00386310,
    0x00388310, 0x0038a310, 0x0038c310, 0x0038e310,
    0x00390310, 0x00392310, 0x00394310, 0x00396310,
    0x00398310, 0x0039a310, 0x0039c310, 0x0039e310,
    0x003a0310, 0x003a2310, 0x003a4310, 0x003a6310,
    0x003a8310, 0x003aa310, 0x003ac310, 0x003ae310,
    0x003b0310, 0x003b2310, 0x003b4310, 0x003b6310,
    0x003b8310, 0x003ba310, 0x003bc310, 0x003be310,
    0x003c0310, 0x003c2310, 0x003c4310, 0x003c6310,
    0x003c8310, 0x003ca310, 0x003cc310, 0x003ce310,
    0x003d0310, 0x003d2310, 0x003d4310, 0x003d6310,
    0x003d8310, 0x003da310, 0x003dc310, 0x003de310,
    0x003e0310, 0x003e2310, 0x003e4310, 0x003e6310,
    0x003e8310, 0x003ea310, 0x003ec310, 0x003ee310,
    0x003f0310, 0x003f2310, 0x003f4310, 0x003f6310,
    0x003f8310, 0x003fa310, 0x003fc310, 0x003fe310,
    0x00400310, 0x00402310, 0x00404310, 0x00406310,
    0x00408310, 0x0040a310, 0x0040c310, 0x0040e310,
    0x00410310, 0x00412310, 0x00414310, 0x00416310,
    0x00418310, 0x0041a310, 0x0041c310, 0x0041e310,
    0x00420310, 0x00422310, 0x00424310, 0x00426310,
    0x00428310, 0x0042a310, 0x0042c310, 0x0042e310,
    0x00430310, 0x00432310, 0x00434310, 0x00436310,
    0x00438310, 0x0043a310, 0x0043c310, 0x0043e310,
    0x00440310, 0x00442310, 0x00444310, 0x00446310,
    0x00448310, 0x0044a310, 0x0044c310, 0x0044e310,
    0x00450310, 0x00452310, 0x00454310, 0x00456310,
    0x00458310, 0x0045a310, 0x0045c310, 0x0045e310,
    0x00460310, 0x00462310, 0x00464310, 0x00466310,
    0x00468310, 0x0046a310, 0x0046c310, 0x0046e310,
    0x00470310, 0x00472310, 0x00474310, 0x00476310,
    0x00478310, 0x0047a310, 0x0047c310, 0x0047e310,
    0x00480310, 0x00482310, 0x00484310, 0x00486310,
    0x00488310, 0x0048a310, 0x0048c310, 0x0048e310,
    0x00490310, 0x00492310, 0x00494310, 0x00496310,
    0x00498310, 0x0049a310, 0x0049c310, 0x0049e310,
    0x004a0310, 0x004a2310, 0x004a4310, 0x004a6310,
    0x004a8310, 0x004aa310, 0x004ac310, 0x004ae310,
    0x004b0310, 0x004b2310, 0x004b4310, 0x004b6310,
    0x004b8310, 0x004ba310, 0x004bc310, 0x004be310,
    0x004c0310, 0x004c2310, 0x004c4310, 0x004c6310,
    0x004c8310, 0x004ca310, 0x004cc310, 0x004ce310,
    0x004d0310, 0x004d2310, 0x004d4310, 0x004d6310,
    0x004d8310, 0x004da310, 0x004dc310, 0x004de310,
    0x004e0310, 0x004e2310, 0x004e4310, 0x004e6310,
    0x004e8310, 0x004ea310, 0x004ec310, 0x004ee310,
    0x004f0310, 0x004f2310, 0x004f4310, 0x004f6310,
    0x004f8310, 0x004fa310, 0x004fc310, 0x004fe310,
    0x00500310, 0x00502310, 0x00504310, 0x00506310,
    0x00508310, 0x0050a310, 0x0050c310, 0x0050e310,
    0x00510310, 0x00512310, 0x00514310, 0x00516310,
    0x00518310, 0x0051a310, 0x0051c310, 0x0051e310,
    0x00520310, 0x00522310, 0x00524310, 0x00526310,
    0x00528310, 0x0052a310, 0x0052c310, 0x0052e310,
    0x00530310, 0x00532310, 0x00534310, 0x00536310,
    0x00538310, 0x0053a310, 0x0053c310, 0x0053e310,
    0x00540310, 0x00542310, 0x00544310, 0x00546310,
    0x00548310, 0x0054a310, 0x0054c310, 0x0054e310,
    0x00550310, 0x00552310, 0x00554310, 0x00556310,
    0x00558310, 0x0055a310, 0x0055c310, 0x0055e310,
    0x00560310, 0x00562310, 0x00564310, 0x00566310,
    0x00568310, 0x0056a310, 0x0056c310, 0x0056e310,
    0x00570310, 0x00572310, 0x00574310, 0x00576310,
    0x00578310, 0x0057a310, 0x0057c310, 0x0057e310,
    0x00580310, 0x00582310, 0x00584310, 0x00586310,
    0x00588310, 0x0058a310, 0x0058c310, 0x0058e310,
    0x00590310, 0x00592310, 0x00594310, 0x00596310,
    0x00598310, 0x0059a310, 0x0059c310, 0x0059e310,
    0x005a0310, 0x005a2310, 0x005a4310, 0x005a6310,
    0x005a8310, 0x005aa310, 0x005ac310, 0x005ae310,
    0x005b0310, 0x005b2310, 0x005b4310, 0x005b6310,
    0x005b8310, 0x005ba310, 0x005bc310, 0x005be310,
    0x005c0310, 0x005c2310, 0x005c4310, 0x005c6310,
    0x005c8310, 0x005ca310, 0x005cc310, 0x005ce310,
    0x005d0310, 0x005d2310, 0x005d4310, 0x005d6310,
    0x005d8310, 0x005da310, 0x005dc310, 0x005de310,
    0x005e0310, 0x005e2310, 0x005e4310, 0x005e6310,
    0x005e8310, 0x005ea310, 0x005ec310, 0x005ee310,
    0x005f0310, 0x005f2310, 0x005f4310, 0x005f6310,
    0x005f8310, 0x005fa310, 0x005fc310, 0x005fe310,
    0x00600310, 0x00602310, 0x00604310, 0x00606310,
    0x00608310, 0x0060a310, 0x0060c310, 0x0060e310,
    0x00610310, 0x00612310, 0x00614310, 0x00616310,
    0x00618310, 0x0061a310, 0x0061c310, 0x0061e310,
    0x00620310, 0x00622310, 0x00624310, 0x00626310,
    0x00628310, 0x0062a310, 0x0062c310, 0x0062e310,
    0x00630310, 0x00632310, 0x00634310, 0x00636310,
    0x00638310, 0x0063a310, 0x0063c310, 0x0063e310,
    0x00640310, 0x00642310, 0x00644310, 0x00646310,
    0x00648310, 0x0064a310, 0x0064c310, 0x0064e310,
    0x00650310, 0x00652310, 0x00654310, 0x00656310,
    0x00658310, 0x0065a310, 0x0065c310, 0x0065e310,
    0x00660310, 0x00662310, 0x00664310, 0x00666310,
    0x00668310, 0x0066a310, 0x0066c310, 0x0066e310,
    0x00670310, 0x00672310, 0x00674310, 0x00676310,
    0x00678310, 0x0067a310, 0x0067c310, 0x0067e310,
    0x00680310, 0x00682310, 0x00684310, 0x00686310,
    0x00688310, 0x0068a310, 0x0068c310, 0x0068e310,
    0x00690310, 0x00692310, 0x00694310, 0x00696310,
    0x00698310, 0x0069a310, 0x0069c310, 0x0069e310,
    0x006a0310, 0x006a2310, 0x006a4310, 0x006a6310,
    0x006a8310, 0x006aa310, 0x006ac310, 0x006ae310,
    0x006b0310, 0x006b2310, 0x006b4310, 0x006b6310,
    0x006b8310, 0x006ba310, 0x006bc310, 0x006be310,
    0x006c0310, 0x006c2310, 0x006c4310, 0x006c6310,
    0x006c8310, 0x006ca310, 0x006cc310, 0x006ce310,
    0x006d0310, 0x006d2310, 0x006d4310, 0x006d6310,
    0x006d8310, 0x006da310, 0x006dc310, 0x006de310,
    0x006e0310, 0x006e2310, 0x006e4310, 0x006e6310,
    0x006e8310, 0x006ea310, 0x006ec310, 0x006ee310,
    0x006f0310, 0x006f2310, 0x006f4310, 0x006f6310,
    0x006f8310, 0x006fa310, 0x006fc310, 0x006fe310,
    0x00700310, 0x00702310, 0x00704310, 0x00706310,
    0x00708310, 0x0070a310, 0x0070c310, 0x0070e310,
    0x00710310, 0x00712310, 0x00714310, 0x00716310,
    0x00718310, 0x0071a310, 0x0071c310, 0x0071e310,
    0x00720310, 0x00722310, 0x00724310, 0x00726310,
    0x00728310, 0x0072a310, 0x0072c310, 0x0072e310,
    0x00730310, 0x00732310, 0x00734310, 0x00736310,
    0x00738310, 0x0073a310, 0x0073c310, 0x0073e310,
    0x00740310, 0x00742310, 0x00744310, 0x00746310,
    0x00748310, 0x0074a310, 0x0074c310, 0x0074e310,
    0x00750310, 0x00752310, 0x00754310, 0x00756310,
    0x00758310, 0x0075a310, 0x0075c310, 0x0075e310,
    0x00760310, 0x00762310, 0x00764310, 0x00766310,
    0x00768310, 0x0076a310, 0x0076c310, 0x0076e310,
    0x00770310, 0x00772310, 0x00774310, 0x00776310,
    0x00778310, 0x0077a310, 0x0077c310, 0x0077e310,
    0x00780310, 0x00782310, 0x00784310, 0x00786310,
    0x00788310, 0x0078a310, 0x0078c310, 0x0078e310,
    0x00790310, 0x00792310, 0x00794310, 0x00796310,
    0x00798310, 0x0079a310, 0x0079c310, 0x0079e310,
    0x007a0310, 0x007a2310, 0x007a4310, 0x007a6310,
    0x007a8310, 0x007aa310, 0x007ac310, 0x007ae310,
    0x007b0310, 0x007b2310, 0x007b4310, 0x007b6310,
    0x007b8310, 0x007ba310, 0x007bc310, 0x007be310,
    0x007c0310, 0x007c2310, 0x007c4310, 0x007c6310,
    0x007c8310, 0x007ca310, 0x007cc310, 0x007ce310,
    0x007d0310, 0x007d2310, 0x007d4310, 0x007d6310,
    0x007d8310, 0x007da310, 0x007dc310, 0x007de310,
    0x007e0310, 0x007e2310, 0x007e4310, 0x007e6310,
    0x007e8310, 0x007ea310, 0x007ec310, 0x007ee310,
    0x007f0310, 0x007f2310, 0x007f4310, 0x007f6310,
    0x007f8310, 0x007fa310, 0x007fc310, 0x007fe310,
    0x00800310, 0x00802310, 0x00804310, 0x00806310,
    0x00808310, 0x0080a310, 0x0080c310, 0x0080e310,
    0x00810310, 0x00812310, 0x00814310, 0x00816310,
    0x00818310, 0x0081a310, 0x0081c310, 0x0081e310,
    0x00820310, 0x00822310, 0x00824310, 0x00826310,
    0x00828310, 0x0082a310, 0x0082c310, 0x0082e310,
    0x00830310, 0x00832310, 0x00834310, 0x00836310,
    0x00838310, 0x0083a310, 0x0083c310, 0x0083e310,
    0x00840310, 0x00842310, 0x00844310, 0x00846310,
    0x00848310, 0x0084a310, 0x0084c310, 0x0084e310,
    0x00850310, 0x00852310, 0x00854310, 0x00856310,
    0x00858310, 0x0085a310, 0x0085c310, 0x0085e310,
    0x00860310, 0x00862310, 0x00864310, 0x00866310,
    0x00868310, 0x0086a310, 0x0086c310, 0x0086e310,
    0x00870310, 0x00872310, 0x00874310, 0x00876310,
    0x00878310, 0x0087a310, 0x0087c310, 0x0087e310,
    0x00880310, 0x00882310, 0x00884310, 0x00886310,
    0x00888310, 0x0088a310, 0x0088c310, 0x0088e310,
    0x00890310, 0x00892310, 0x00894310, 0x00896310,
    0x00898310, 0x0089a310, 0x0089c310, 0x0089e310,
    0x008a0310, 0x008a2310, 0x008a4310, 0x008a6310,
    0x008a8310, 0x008aa310, 0x008ac310, 0x008ae310,
    0x008b0310, 0x008b2310, 0x008b4310, 0x008b6310,
    0x008b8310, 0x008ba310, 0x008bc310, 0x008be310,
    0x008c0310, 0x008c2310, 0x008c4310, 0x008c6310,
    0x008c8310, 0x008ca310, 0x008cc310, 0x008ce310,
    0x008d0310, 0x008d2310, 0x008d4310, 0x008d6310,
    0x008d8310, 0x008da310, 0x008dc310, 0x008de310,
    0x008e0310, 0x008e2310, 0x008e4310, 0x008e6310,
    0x008e8310, 0x008ea310, 0x008ec310, 0x008ee310,
    0x008f0310, 0x008f2310, 0x008f4310, 0x008f6310,
    0x008f8310, 0x008fa310, 0x008fc310, 0x008fe310,
    0x00900310, 0x00902310, 0x00904310, 0x00906310,
    0x00908310, 0x0090a310, 0x0090c310, 0x0090e310,
    0x00910310, 0x00912310, 0x00914310, 0x00916310,
    0x00918310, 0x0091a310, 0x0091c310, 0x0091e310,
    0x00920310, 0x00922310, 0x00924310, 0x00926310,
    0x00928310, 0x0092a310, 0x0092c310, 0x0092e310,
    0x00930310, 0x00932310, 0x00934310, 0x00936310,
    0x00938310, 0x0093a310, 0x0093c310, 0x0093e310,
    0x00940310, 0x00942310, 0x00944310, 0x00946310,
    0x00948310, 0x0094a310, 0x0094c310, 0x0094e310,
    0x00950310, 0x00952310, 0x00954310, 0x00956310,
    0x00958310, 0x0095a310, 0x0095c310, 0x0095e310,
    0x00960310, 0x00962310, 0x00964310, 0x00966310,
    0x00968310, 0x0096a310, 0x0096c310, 0x0096e310,
    0x00970310, 0x00972310, 0x00974310, 0x00976310,
    0x00978310, 0x0097a310, 0x0097c310, 0x0097e310,
    0x00980310, 0x00982310, 0x00984310, 0x00986310,
    0x00988310, 0x0098a310, 0x0098c310, 0x0098e310,
    0x00990310, 0x00992310, 0x00994310, 0x00996310,
    0x00998310, 0x0099a310, 0x0099c310, 0x0099e310,
    0x009a0310, 0x009a2310, 0x009a4310, 0x009a6310,
    0x009a8310, 0x009aa310, 0x009ac310, 0x009ae310,
    0x009b0310, 0x009b2310, 0x009b4310, 0x009b6310,
    0x009b8310, 0x009ba310, 0x009bc310, 0x009be310,
    0x009c0310, 0x009c2310, 0x009c4310, 0x009c6310,
    0x009c8310, 0x009ca310, 0x009cc310, 0x009ce310,
    0x009d0310, 0x009d2310, 0x009d4310, 0x009d6310,
    0x009d8310, 0x009da310, 0x009dc310, 0x009de310,
    0x009e0310, 0x009e2310, 0x009e4310, 0x009e6310,
    0x009e8310, 0x009ea310, 0x009ec310, 0x009ee310,
    0x009f0310, 0x009f2310, 0x009f4310, 0x009f6310,
    0x009f8310, 0x009fa310, 0x009fc310, 0x009fe310,
    0x00a00310, 0x00a02310, 0x00a04310, 0x00a06310,
    0x00a08310, 0x00a0a310, 0x00a0c310, 0x00a0e310,
    0x00a10310, 0x00a12310, 0x00a14310, 0x00a16310,
    0x00a18310, 0x00a1a310, 0x00a1c310, 0x00a1e310,
    0x00a20310, 0x00a22310, 0x00a24310, 0x00a26310,
    0x00a28310, 0x00a2a310, 0x00a2c310, 0x00a2e310,
    0x00a30310, 0x00a32310, 0x00a34310, 0x00a36310,
    0x00a38310, 0x00a3a310, 0x00a3c310, 0x00a3e310,
    0x00a40310, 0x00a42310, 0x00a44310, 0x00a46310,
    0x00a48310, 0x00a4a310, 0x00a4c310, 0x00a4e310,
    0x00a50310, 0x00a52310, 0x00a54310, 0x00a56310,
    0x00a58310, 0x00a5a310, 0x00a5c310, 0x00a5e310,
    0x00a60310, 0x00a62310, 0x00a64310, 0x00a66310,
    0x00a68310, 0x00a6a310, 0x00a6c310, 0x00a6e310,
    0x00a70310, 0x00a72310, 0x00a74310, 0x00a76310,
    0x00a78310, 0x00a7a310, 0x00a7c310, 0x00a7e310,
    0x00a80310, 0x00a82310, 0x00a84310, 0x00a86310,
    0x00a88310, 0x00a8a310, 0x00a8c310, 0x00a8e310,
    0x00a90310, 0x00a92310, 0x00a94310, 0x00a96310,
    0x00a98310, 0x00a9a310, 0x00a9c310, 0x00a9e310,
    0x00aa0310, 0x00aa2310, 0x00aa4310, 0x00aa6310,
    0x00aa8310, 0x00aaa310, 0x00aac310, 0x00aae310,
    0x00ab0310, 0x00ab2310, 0x00ab4310, 0x00ab6310,
    0x00ab8310, 0x00aba310, 0x00abc310, 0x00abe310,
    0x00ac0310, 0x00ac2310, 0x00ac4310, 0x00ac6310,
    0x00ac8310, 0x00aca310, 0x00acc310, 0x00ace310,
    0x00ad0310, 0x00ad2310, 0x00ad4310, 0x00ad6310,
    0x00ad8310, 0x00ada310, 0x00adc310, 0x00ade310,
    0x00ae0310, 0x00ae2310, 0x00ae4310, 0x00ae6310,
    0x00ae8310, 0x00aea310, 0x00aec310, 0x00aee310,
    0x00af0310, 0x00af2310, 0x00af4310, 0x00af6310,
    0x00af8310, 0x00afa310, 0x00afc310, 0x00afe310,
    0x00b00310, 0x00b02310, 0x00b04310, 0x00b06310,
    0x00b08310, 0x00b0a310, 0x00b0c310, 0x00b0e310,
    0x00b10310, 0x00b12310, 0x00b14310, 0x00b16310,
    0x00b18310, 0x00b1a310, 0x00b1c310, 0x00b1e310,
    0x00b20310, 0x00b22310, 0x00b24310, 0x00b26310,
    0x00b28310, 0x00b2a310, 0x00b2c310, 0x00b2e310,
    0x00b30310, 0x00b32310, 0x00b34310, 0x00b36310,
    0x00b38310, 0x00b3a310, 0x00b3c310, 0x00b3e310,
    0x00b40310, 0x00b42310, 0x00b44310, 0x00b46310,
    0x00b48310, 0x00b4a310, 0x00b4c310, 0x00b4e310,
    0x00b50310, 0x00b52310, 0x00b54310, 0x00b56310,
    0x00b58310, 0x00b5a310, 0x00b5c310, 0x00b5e310,
    0x00b60310, 0x00b62310, 0x00b64310, 0x00b66310,
    0x00b68310, 0x00b6a310, 0x00b6c310, 0x00b6e310,
    0x00b70310, 0x00b72310, 0x00b74310, 0x00b76310,
    0x00b78310, 0x00b7a310, 0x00b7c310, 0x00b7e310,
    0x00b80310, 0x00b82310, 0x00b84310, 0x00b86310,
    0x00b88310, 0x00b8a310, 0x00b8c310, 0x00b8e310,
    0x00b90310, 0x00b92310, 0x00b94310, 0x00b96310,
    0x00b98310, 0x00b9a310, 0x00b9c310, 0x00b9e310,
    0x00ba0310, 0x00ba2310, 0x00ba4310, 0x00ba6310,
    0x00ba8310, 0x00baa310, 0x00bac310, 0x00bae310,
    0x00bb0310, 0x00bb2310, 0x00bb4310, 0x00bb6310,
    0x00bb8310, 0x00bba310, 0x00bbc310, 0x00bbe310,
    0x00bc0310, 0x00bc2310, 0x00bc4310, 0x00bc6310,
    0x00bc8310, 0x00bca310, 0x00bcc310, 0x00bce310,
    0x00bd0310, 0x00bd2310, 0x00bd4310, 0x00bd6310,
    0x00bd8310, 0x00bda310, 0x00bdc310, 0x00bde310,
    0x00be0310, 0x00be2310, 0x00be4310, 0x00be6310,
    0x00be8310, 0x00bea310, 0x00bec310, 0x00bee310,
    0x00bf0310, 0x00bf2310, 0x00bf4310, 0x00bf6310,
    0x00bf8310, 0x00bfa310, 0x00bfc310, 0x00bfe310,
    0x00c00310, 0x00c02310, 0x00c04310, 0x00c06310,
    0x00c08310, 0x00c0a310, 0x00c0c310, 0x00c0e310,
    0x00c10310, 0x00c12310, 0x00c14310, 0x00c16310,
    0x00c18310, 0x00c1a310, 0x00c1c310, 0x00c1e310,
    0x00c20310, 0x00c22310, 0x00c24310, 0x00c26310,
    0x00c28310, 0x00c2a310, 0x00c2c310, 0x00c2e310,
    0x00c30310, 0x00c32310, 0x00c34310, 0x00c36310,
    0x00c38310, 0x00c3a310, 0x00c3c310, 0x00c3e310,
    0x00c40310, 0x00c42310, 0x00c44310, 0x00c46310,
    0x00c48310, 0x00c4a310, 0x00c4c310, 0x00c4e310,
    0x00c50310, 0x00c52310, 0x00c54310, 0x00c56310,
    0x00c58310, 0x00c5a310, 0x00c5c310, 0x00c5e310,
    0x00c60310, 0x00c62310, 0x00c64310, 0x00c66310,
    0x00c68310, 0x00c6a310, 0x00c6c310, 0x00c6e310,
    0x00c70310, 0x00c72310, 0x00c74310, 0x00c76310,
    0x00c78310, 0x00c7a310, 0x00c7c310, 0x00c7e310,
    0x00c80310, 0x00c82310, 0x00c84310, 0x00c86310,
    0x00c88310, 0x00c8a310, 0x00c8c310, 0x00c8e310,
    0x00c90310, 0x00c92310, 0x00c94310, 0x00c96310,
    0x00c98310, 0x00c9a310, 0x00c9c310, 0x00c9e310,
    0x00ca0310, 0x00ca2310, 0x00ca4310, 0x00ca6310,
    0x00ca8310, 0x00caa310, 0x00cac310, 0x00cae310,
    0x00cb0310, 0x00cb2310, 0x00cb4310, 0x00cb6310,
    0x00cb8310, 0x00cba310, 0x00cbc310, 0x00cbe310,
    0x00cc0310, 0x00cc2310, 0x00cc4310, 0x00cc6310,
    0x00cc8310, 0x00cca310, 0x00ccc310, 0x00cce310,
    0x00cd0310, 0x00cd2310, 0x00cd4310, 0x00cd6310,
    0x00cd8310, 0x00cda310, 0x00cdc310, 0x00cde310,
    0x00ce0310, 0x00ce2310, 0x00ce4310, 0x00ce6310,
    0x00ce8310, 0x00cea310, 0x00cec310, 0x00cee310,
    0x00cf0310, 0x00cf2310, 0x00cf4310, 0x00cf6310,
    0x00cf8310, 0x00cfa310, 0x00cfc310, 0x00cfe310,
    0x00d00310, 0x00d02310, 0x00d04310, 0x00d06310,
    0x00d08310, 0x00d0a310, 0x00d0c310, 0x00d0e310,
    0x00d10310, 0x00d12310, 0x00d14310, 0x00d16310,
    0x00d18310, 0x00d1a310, 0x00d1c310, 0x00d1e310,
    0x00d20310, 0x00d22310, 0x00d24310, 0x00d26310,
    0x00d28310, 0x00d2a310, 0x00d2c310, 0x00d2e310,
    0x00d30310, 0x00d32310, 0x00d34310, 0x00d36310,
    0x00d38310, 0x00d3a310, 0x00d3c310, 0x00d3e310,
    0x00d40310, 0x00d42310, 0x00d44310, 0x00d46310,
    0x00d48310, 0x00d4a310, 0x00d4c310, 0x00d4e310,
    0x00d50310, 0x00d52310, 0x00d54310, 0x00d56310,
    0x00d58310, 0x00d5a310, 0x00d5c310, 0x00d5e310,
    0x00d60310, 0x00d62310, 0x00d64310, 0x00d66310,
    0x00d68310, 0x00d6a310, 0x00d6c310, 0x00d6e310,
    0x00d70310, 0x00d72310, 0x00d74310, 0x00d76310,
    0x00d78310, 0x00d7a310, 0x00d7c310, 0x00d7e310,
    0x00d80310, 0x00d82310, 0x00d84310, 0x00d86310,
    0x00d88310, 0x00d8a310, 0x00d8c310, 0x00d8e310,
    0x00d90310, 0x00d92310, 0x00d94310, 0x00d96310,
    0x00d98310, 0x00d9a310, 0x00d9c310, 0x00d9e310,
    0x00da0310, 0x00da2310, 0x00da4310, 0x00da6310,
    0x00da8310, 0x00daa310, 0x00dac310, 0x00dae310,
    0x00db0310, 0x00db2310, 0x00db4310, 0x00db6310,
    0x00db8310, 0x00dba310, 0x00dbc310, 0x00dbe310,
    0x00dc0310, 0x00dc2310, 0x00dc4310, 0x00dc6310,
    0x00dc8310, 0x00dca310, 0x00dcc310, 0x00dce310,
    0x00dd0310, 0x00dd2310, 0x00dd4310, 0x00dd6310,
    0x00dd8310, 0x00dda310, 0x00ddc310, 0x00dde310,
    0x00de0310, 0x00de2310, 0x00de4310, 0x00de6310,
    0x00de8310, 0x00dea310, 0x00dec310, 0x00dee310,
    0x00df0310, 0x00df2310, 0x00df4310, 0x00df6310,
    0x00df8310, 0x00dfa310, 0x00dfc310, 0x00dfe310,
    0x00e00310, 0x00e02310, 0x00e04310, 0x00e06310,
    0x00e08310, 0x00e0a310, 0x00e0c310, 0x00e0e310,
    0x00e10310, 0x00e12310, 0x00e14310, 0x00e16310,
    0x00e18310, 0x00e1a310, 0x00e1c310, 0x00e1e310,
    0x00e20310, 0x00e22310, 0x00e24310, 0x00e26310,
    0x00e28310, 0x00e2a310, 0x00e2c310, 0x00e2e310,
    0x00e30310, 0x00e32310, 0x00e34310, 0x00e36310,
    0x00e38310, 0x00e3a310, 0x00e3c310, 0x00e3e310,
    0x00e40310, 0x00e42310, 0x00e44310, 0x00e46310,
    0x00e48310, 0x00e4a310, 0x00e4c310, 0x00e4e310,
    0x00e50310, 0x00e52310, 0x00e54310, 0x00e56310,
    0x00e58310, 0x00e5a310, 0x00e5c310, 0x00e5e310,
    0x00e60310, 0x00e62310, 0x00e64310, 0x00e66310,
    0x00e68310, 0x00e6a310, 0x00e6c310, 0x00e6e310,
    0x00e70310, 0x00e72310, 0x00e74310, 0x00e76310,
    0x00e78310, 0x00e7a310, 0x00e7c310, 0x00e7e310,
    0x00e80310, 0x00e82310, 0x00e84310, 0x00e86310,
    0x00e88310, 0x00e8a310, 0x00e8c310, 0x00e8e310,
    0x00e90310, 0x00e92310, 0x00e94310, 0x00e96310,
    0x00e98310, 0x00e9a310, 0x00e9c310, 0x00e9e310,
    0x00ea0310, 0x00ea2310, 0x00ea4310, 0x00ea6310,
    0x00ea8310, 0x00eaa310, 0x00eac310, 0x00eae310,
    0x00eb0310, 0x00eb2310, 0x00eb4310, 0x00eb6310,
    0x00eb8310, 0x00eba310, 0x00ebc310, 0x00ebe310,
    0x00ec0310, 0x00ec2310, 0x00ec4310, 0x00ec6310,
    0x00ec8310, 0x00eca310, 0x00ecc310, 0x00ece310,
    0x00ed0310, 0x00ed2310, 0x00ed4310, 0x00ed6310,
    0x00ed8310, 0x00eda310, 0x00edc310, 0x00ede310,
    0x00ee0310, 0x00ee2310, 0x00ee4310, 0x00ee6310,
    0x00ee8310, 0x00eea310, 0x00eec310, 0x00eee310,
    0x00ef0310, 0x00ef2310, 0x00ef4310, 0x00ef6310,
    0x00ef8310, 0x00efa310, 0x00efc310, 0x00efe310,
    0x00f00310, 0x00f02310, 0x00f04310, 0x00f06310,
    0x00f08310, 0x00f0a310, 0x00f0c310, 0x00f0e310,
    0x00f10310, 0x00f12310, 0x00f14310, 0x00f16310,
    0x00f18310, 0x00f1a310, 0x00f1c310, 0x00f1e310,
    0x00f20310, 0x00f22310, 0x00f24310, 0x00f26310,
    0x00f28310, 0x00f2a310, 0x00f2c310, 0x00f2e310,
    0x00f30310, 0x00f32310, 0x00f34310, 0x00f36310,
    0x00f38310, 0x00f3a310, 0x00f3c310, 0x00f3e310,
    0x00f40310, 0x00f42310, 0x00f44310, 0x00f46310,
    0x00f48310, 0x00f4a310, 0x00f4c310, 0x00f4e310,
    0x00f50310, 0x00f52310, 0x00f54310, 0x00f56310,
    0x00f58310, 0x00f5a310, 0x00f5c310, 0x00f5e310,
    0x00f60310, 0x00f62310, 0x00f64310, 0x00f66310,
    0x00f68310, 0x00f6a310, 0x00f6c310, 0x00f6e310,
    0x00f70310, 0x00f72310, 0x00f74310, 0x00f76310,
    0x00f78310, 0x00f7a310, 0x00f7c310, 0x00f7e310,
    0x00f80310, 0x00f82310, 0x00f84310, 0x00f86310,
    0x00f88310, 0x00f8a310, 0x00f8c310, 0x00f8e310,
    0x00f90310, 0x00f92310, 0x00f94310, 0x00f96310,
    0x00f98310, 0x00f9a310, 0x00f9c310, 0x00f9e310,
    0x00fa0310, 0x00fa2310, 0x00fa4310, 0x00fa6310,
    0x00fa8310, 0x00faa310, 0x00fac310, 0x00fae310,
    0x00fb0310, 0x00fb2310, 0x00fb4310, 0x00fb6310,
    0x00fb8310, 0x00fba310, 0x00fbc310, 0x00fbe310,
    0x00fc0310, 0x00fc2310, 0x00fc4310, 0x00fc6310,
    0x00fc8310, 0x00fca310, 0x00fcc310, 0x00fce310,
    0x00fd0310, 0x00fd2310, 0x00fd4310, 0x00fd6310,
    0x00fd8310, 0x00fda310, 0x00fdc310, 0x00fde310,
    0x00fe0310, 0x00fe2310, 0x00fe4310, 0x00fe6310,
    0x00fe8310, 0x00fea310, 0x00fec310, 0x00fee310,
    0x00ff0310, 0x00ff2310, 0x00ff4310, 0x00ff6310,
    0x00ff8310, 0x00ffa310, 0x00ffc310, 0x00ffe310,
    0x00001310, 0x00003310, 0x00005310, 0x00007310,
    0x00009310, 0x0000b310, 0x0000d310, 0x0000f310,
    0x00011310, 0x00013310, 0x00015310, 0x00017310,
    0x00019310, 0x0001b310, 0x0001d310, 0x0001f310,
    0x00021310, 0x00023310, 0x00025310, 0x00027310,
    0x00029310, 0x0002b310, 0x0002d310, 0x0002f310,
    0x00031310, 0x00033310, 0x00035310, 0x00037310,
    0x00039310, 0x0003b310, 0x0003d310, 0x0003f310,
    0x00041310, 0x00043310, 0x00045310, 0x00047310,
    0x00049310, 0x0004b310, 0x0004d310, 0x0004f310,
    0x00051310, 0x00053310, 0x00055310, 0x00057310,
    0x00059310, 0x0005b310, 0x0005d310, 0x0005f310,
    0x00061310, 0x00063310, 0x00065310, 0x00067310,
    0x00069310, 0x0006b310, 0x0006d310, 0x0006f310,
    0x00071310, 0x00073310, 0x00075310, 0x00077310,
    0x00079310, 0x0007b310, 0x0007d310, 0x0007f310,
    0x00081310, 0x00083310, 0x00085310, 0x00087310,
    0x00089310, 0x0008b310, 0x0008d310, 0x0008f310,
    0x00091310, 0x00093310, 0x00095310, 0x00097310,
    0x00099310, 0x0009b310, 0x0009d310, 0x0009f310,
    0x000a1310, 0x000a3310, 0x000a5310, 0x000a7310,
    0x000a9310, 0x000ab310, 0x000ad310, 0x000af310,
    0x000b1310, 0x000b3310, 0x000b5310, 0x000b7310,
    0x000b9310, 0x000bb310, 0x000bd310, 0x000bf310,
    0x000c1310, 0x000c3310, 0x000c5310, 0x000c7310,
    0x000c9310, 0x000cb310, 0x000cd310, 0x000cf310,
    0x000d1310, 0x000d3310, 0x000d5310, 0x000d7310,
    0x000d9310, 0x000db310, 0x000dd310, 0x000df310,
    0x000e1310, 0x000e3310, 0x000e5310, 0x000e7310,
    0x000e9310, 0x000eb310, 0x000ed310, 0x000ef310,
    0x000f1310, 0x000f3310, 0x000f5310, 0x000f7310,
    0x000f9310, 0x000fb310, 0x000fd310, 0x000ff310,
    0x00101310, 0x00103310, 0x00105310, 0x00107310,
    0x00109310, 0x0010b310, 0x0010d310, 0x0010f310,
    0x00111310, 0x00113310, 0x00115310, 0x00117310,
    0x00119310, 0x0011b310, 0x0011d310, 0x0011f310,
    0x00121310, 0x00123310, 0x00125310, 0x00127310,
    0x00129310, 0x0012b310, 0x0012d310, 0x0012f310,
    0x00131310, 0x00133310, 0x00135310, 0x00137310,
    0x00139310, 0x0013b310, 0x0013d310, 0x0013f310,
    0x00141310, 0x00143310, 0x00145310, 0x00147310,
    0x00149310, 0x0014b310, 0x0014d310, 0x0014f310,
    0x00151310, 0x00153310, 0x00155310, 0x00157310,
    0x00159310, 0x0015b310, 0x0015d310, 0x0015f310,
    0x00161310, 0x00163310, 0x00165310, 0x00167310,
    0x00169310, 0x0016b310, 0x0016d310, 0x0016f310,
    0x00171310, 0x00173310, 0x00175310, 0x00177310,
    0x00179310, 0x0017b310, 0x0017d310, 0x0017f310,
    0x00181310, 0x00183310, 0x00185310, 0x00187310,
    0x00189310, 0x0018b310, 0x0018d310, 0x0018f310,
    0x00191310, 0x00193310, 0x00195310, 0x00197310,
    0x00199310, 0x0019b310, 0x0019d310, 0x0019f310,
    0x001a1310, 0x001a3310, 0x001a5310, 0x001a7310,
    0x001a9310, 0x001ab310, 0x001ad310, 0x001af310,
    0x001b1310, 0x001b3310, 0x001b5310, 0x001b7310,
    0x001b9310, 0x001bb310, 0x001bd310, 0x001bf310,
    0x001c1310, 0x001c3310, 0x001c5310, 0x001c7310,
    0x001c9310, 0x001cb310, 0x001cd310, 0x001cf310,
    0x001d1310, 0x001d3310, 0x001d5310, 0x001d7310,
    0x001d9310, 0x001db310, 0x001dd310, 0x001df310,
    0x001e1310, 0x001e3310, 0x001e5310, 0x001e7310,
    0x001e9310, 0x001eb310, 0x001ed310, 0x001ef310,
    0x001f1310, 0x001f3310, 0x001f5310, 0x001f7310,
    0x001f9310, 0x001fb310, 0x001fd310, 0x001ff310,
    0x00201310, 0x00203310, 0x00205310, 0x00207310,
    0x00209310, 0x0020b310, 0x0020d310, 0x0020f310,
    0x00211310, 0x00213310, 0x00215310, 0x00217310,
    0x00219310, 0x0021b310, 0x0021d310, 0x0021f310,
    0x00221310, 0x00223310, 0x00225310, 0x00227310,
    0x00229310, 0x0022b310, 0x0022d310, 0x0022f310,
    0x00231310, 0x00233310, 0x00235310, 0x00237310,
    0x00239310, 0x0023b310, 0x0023d310, 0x0023f310,
    0x00241310, 0x00243310, 0x00245310, 0x00247310,
    0x00249310, 0x0024b310, 0x0024d310, 0x0024f310,
    0x00251310, 0x00253310, 0x00255310, 0x00257310,
    0x00259310, 0x0025b310, 0x0025d310, 0x0025f310,
    0x00261310, 0x00263310, 0x00265310, 0x00267310,
    0x00269310, 0x0026b310, 0x0026d310, 0x0026f310,
    0x00271310, 0x00273310, 0x00275310, 0x00277310,
    0x00279310, 0x0027b310, 0x0027d310, 0x0027f310,
    0x00281310, 0x00283310, 0x00285310, 0x00287310,
    0x00289310, 0x0028b310, 0x0028d310, 0x0028f310,
    0x00291310, 0x00293310, 0x00295310, 0x00297310,
    0x00299310, 0x0029b310, 0x0029d310, 0x0029f310,
    0x002a1310, 0x002a3310, 0x002a5310, 0x002a7310,
    0x002a9310, 0x002ab310, 0x002ad310, 0x002af310,
    0x002b1310, 0x002b3310, 0x002b5310, 0x002b7310,
    0x002b9310, 0x002bb310, 0x002bd310, 0x002bf310,
    0x002c1310, 0x002c3310, 0x002c5310, 0x002c7310,
    0x002c9310, 0x002cb310, 0x002cd310, 0x002cf310,
    0x002d1310, 0x002d3310, 0x002d5310, 0x002d7310,
    0x002d9310, 0x002db310, 0x002dd310, 0x002df310,
    0x002e1310, 0x002e3310, 0x002e5310, 0x002e7310,
    0x002e9310, 0x002eb310, 0x002ed310, 0x002ef310,
    0x002f1310, 0x002f3310, 0x002f5310, 0x002f7310,
    0x002f9310, 0x002fb310, 0x002fd310, 0x002ff310,
    0x00301310, 0x00303310, 0x00305310, 0x00307310,
    0x00309310, 0x0030b310, 0x0030d310, 0x0030f310,
    0x00311310, 0x00313310, 0x00315310, 0x00317310,
    0x00319310, 0x0031b310, 0x0031d310, 0x0031f310,
    0x00321310, 0x00323310, 0x00325310, 0x00327310,
    0x00329310, 0x0032b310, 0x0032d310, 0x0032f310,
    0x00331310, 0x00333310, 0x00335310, 0x00337310,
    0x00339310, 0x0033b310, 0x0033d310, 0x0033f310,
    0x00341310, 0x00343310, 0x00345310, 0x00347310,
    0x00349310, 0x0034b310, 0x0034d310, 0x0034f310,
    0x00351310, 0x00353310, 0x00355310, 0x00357310,
    0x00359310, 0x0035b310, 0x0035d310, 0x0035f310,
    0x00361310, 0x00363310, 0x00365310, 0x00367310,
    0x00369310, 0x0036b310, 0x0036d310, 0x0036f310,
    0x00371310, 0x00373310, 0x00375310, 0x00377310,
    0x00379310, 0x0037b310, 0x0037d310, 0x0037f310,
    0x00381310, 0x00383310, 0x00385310, 0x00387310,
    0x00389310, 0x0038b310, 0x0038d310, 0x0038f310,
    0x00391310, 0x00393310, 0x00395310, 0x00397310,
    0x00399310, 0x0039b310, 0x0039d310, 0x0039f310,
    0x003a1310, 0x003a3310, 0x003a5310, 0x003a7310,
    0x003a9310, 0x003ab310, 0x003ad310, 0x003af310,
    0x003b1310, 0x003b3310, 0x003b5310, 0x003b7310,
    0x003b9310, 0x003bb310, 0x003bd310, 0x003bf310,
    0x003c1310, 0x003c3310, 0x003c5310, 0x003c7310,
    0x003c9310, 0x003cb310, 0x003cd310, 0x003cf310,
    0x003d1310, 0x003d3310, 0x003d5310, 0x003d7310,
    0x003d9310, 0x003db310, 0x003dd310, 0x003df310,
    0x003e1310, 0x003e3310, 0x003e5310, 0x003e7310,
    0x003e9310, 0x003eb310, 0x003ed310, 0x003ef310,
    0x003f1310, 0x003f3310, 0x003f5310, 0x003f7310,
    0x003f9310, 0x003fb310, 0x003fd310, 0x003ff310,
    0x00401310, 0x00403310, 0x00405310, 0x00407310,
    0x00409310, 0x0040b310, 0x0040d310, 0x0040f310,
    0x00411310, 0x00413310, 0x00415310, 0x00417310,
    0x00419310, 0x0041b310, 0x0041d310, 0x0041f310,
    0x00421310, 0x00423310, 0x00425310, 0x00427310,
    0x00429310, 0x0042b310, 0x0042d310, 0x0042f310,
    0x00431310, 0x00433310, 0x00435310, 0x00437310,
    0x00439310, 0x0043b310, 0x0043d310, 0x0043f310,
    0x00441310, 0x00443310, 0x00445310, 0x00447310,
    0x00449310, 0x0044b310, 0x0044d310, 0x0044f310,
    0x00451310, 0x00453310, 0x00455310, 0x00457310,
    0x00459310, 0x0045b310, 0x0045d310, 0x0045f310,
    0x00461310, 0x00463310, 0x00465310, 0x00467310,
    0x00469310, 0x0046b310, 0x0046d310, 0x0046f310,
    0x00471310, 0x00473310, 0x00475310, 0x00477310,
    0x00479310, 0x0047b310, 0x0047d310, 0x0047f310,
    0x00481310, 0x00483310, 0x00485310, 0x00487310,
    0x00489310, 0x0048b310, 0x0048d310, 0x0048f310,
    0x00491310, 0x00493310, 0x00495310, 0x00497310,
    0x00499310, 0x0049b310, 0x0049d310, 0x0049f310,
    0x004a1310, 0x004a3310, 0x004a5310, 0x004a7310,
    0x004a9310, 0x004ab310, 0x004ad310, 0x004af310,
    0x004b1310, 0x004b3310, 0x004b5310, 0x004b7310,
    0x004b9310, 0x004bb310, 0x004bd310, 0x004bf310,
    0x004c1310, 0x004c3310, 0x004c5310, 0x004c7310,
    0x004c9310, 0x004cb310, 0x004cd310, 0x004cf310,
    0x004d1310, 0x004d3310, 0x004d5310, 0x004d7310,
    0x004d9310, 0x004db310, 0x004dd310, 0x004df310,
    0x004e1310, 0x004e3310, 0x004e5310, 0x004e7310,
    0x004e9310, 0x004eb310, 0x004ed310, 0x004ef310,
    0x004f1310, 0x004f3310, 0x004f5310, 0x004f7310,
    0x004f9310, 0x004fb310, 0x004fd310, 0x004ff310,
    0x00501310, 0x00503310, 0x00505310, 0x00507310,
    0x00509310, 0x0050b310, 0x0050d310, 0x0050f310,
    0x00511310, 0x00513310, 0x00515310, 0x00517310,
    0x00519310, 0x0051b310, 0x0051d310, 0x0051f310,
    0x00521310, 0x00523310, 0x00525310, 0x00527310,
    0x00529310, 0x0052b310, 0x0052d310, 0x0052f310,
    0x00531310, 0x00533310, 0x00535310, 0x00537310,
    0x00539310, 0x0053b310, 0x0053d310, 0x0053f310,
    0x00541310, 0x00543310, 0x00545310, 0x00547310,
    0x00549310, 0x0054b310, 0x0054d310, 0x0054f310,
    0x00551310, 0x00553310, 0x00555310, 0x00557310,
    0x00559310, 0x0055b310, 0x0055d310, 0x0055f310,
    0x00561310, 0x00563310, 0x00565310, 0x00567310,
    0x00569310, 0x0056b310, 0x0056d310, 0x0056f310,
    0x00571310, 0x00573310, 0x00575310, 0x00577310,
    0x00579310, 0x0057b310, 0x0057d310, 0x0057f310,
    0x00581310, 0x00583310, 0x00585310, 0x00587310,
    0x00589310, 0x0058b310, 0x0058d310, 0x0058f310,
    0x00591310, 0x00593310, 0x00595310, 0x00597310,
    0x00599310, 0x0059b310, 0x0059d310, 0x0059f310,
    0x005a1310, 0x005a3310, 0x005a5310, 0x005a7310,
    0x005a9310, 0x005ab310, 0x005ad310, 0x005af310,
    0x005b1310, 0x005b3310, 0x005b5310, 0x005b7310,
    0x005b9310, 0x005bb310, 0x005bd310, 0x005bf310,
    0x005c1310, 0x005c3310, 0x005c5310, 0x005c7310,
    0x005c9310, 0x005cb310, 0x005cd310, 0x005cf310,
    0x005d1310, 0x005d3310, 0x005d5310, 0x005d7310,
    0x005d9310, 0x005db310, 0x005dd310, 0x005df310,
    0x005e1310, 0x005e3310, 0x005e5310, 0x005e7310,
    0x005e9310, 0x005eb310, 0x005ed310, 0x005ef310,
    0x005f1310, 0x005f3310, 0x005f5310, 0x005f7310,
    0x005f9310, 0x005fb310, 0x005fd310, 0x005ff310,
    0x00601310, 0x00603310, 0x00605310, 0x00607310,
    0x00609310, 0x0060b310, 0x0060d310, 0x0060f310,
    0x00611310, 0x00613310, 0x00615310, 0x00617310,
    0x00619310, 0x0061b310, 0x0061d310, 0x0061f310,
    0x00621310, 0x00623310, 0x00625310, 0x00627310,
    0x00629310, 0x0062b310, 0x0062d310, 0x0062f310,
    0x00631310, 0x00633310, 0x00635310, 0x00637310,
    0x00639310, 0x0063b310, 0x0063d310, 0x0063f310,
    0x00641310, 0x00643310, 0x00645310, 0x00647310,
    0x00649310, 0x0064b310, 0x0064d310, 0x0064f310,
    0x00651310, 0x00653310, 0x00655310, 0x00657310,
    0x00659310, 0x0065b310, 0x0065d310, 0x0065f310,
    0x00661310, 0x00663310, 0x00665310, 0x00667310,
    0x00669310, 0x0066b310, 0x0066d310, 0x0066f310,
    0x00671310, 0x00673310, 0x00675310, 0x00677310,
    0x00679310, 0x0067b310, 0x0067d310, 0x0067f310,
    0x00681310, 0x00683310, 0x00685310, 0x00687310,
    0x00689310, 0x0068b310, 0x0068d310, 0x0068f310,
    0x00691310, 0x00693310, 0x00695310, 0x00697310,
    0x00699310, 0x0069b310, 0x0069d310, 0x0069f310,
    0x006a1310, 0x006a3310, 0x006a5310, 0x006a7310,
    0x006a9310, 0x006ab310, 0x006ad310, 0x006af310,
    0x006b1310, 0x006b3310, 0x006b5310, 0x006b7310,
    0x006b9310, 0x006bb310, 0x006bd310, 0x006bf310,
    0x006c1310, 0x006c3310, 0x006c5310, 0x006c7310,
    0x006c9310, 0x006cb310, 0x006cd310, 0x006cf310,
    0x006d1310, 0x006d3310, 0x006d5310, 0x006d7310,
    0x006d9310, 0x006db310, 0x006dd310, 0x006df310,
    0x006e1310, 0x006e3310, 0x006e5310, 0x006e7310,
    0x006e9310, 0x006eb310, 0x006ed310, 0x006ef310,
    0x006f1310, 0x006f3310, 0x006f5310, 0x006f7310,
    0x006f9310, 0x006fb310, 0x006fd310, 0x006ff310,
    0x00701310, 0x00703310, 0x00705310, 0x00707310,
    0x00709310, 0x0070b310, 0x0070d310, 0x0070f310,
    0x00711310, 0x00713310, 0x00715310, 0x00717310,
    0x00719310, 0x0071b310, 0x0071d310, 0x0071f310,
    0x00721310, 0x00723310, 0x00725310, 0x00727310,
    0x00729310, 0x0072b310, 0x0072d310, 0x0072f310,
    0x00731310, 0x00733310, 0x00735310, 0x00737310,
    0x00739310, 0x0073b310, 0x0073d310, 0x0073f310,
    0x00741310, 0x00743310, 0x00745310, 0x00747310,
    0x00749310, 0x0074b310, 0x0074d310, 0x0074f310,
    0x00751310, 0x00753310, 0x00755310, 0x00757310,
    0x00759310, 0x0075b310, 0x0075d310, 0x0075f310,
    0x00761310, 0x00763310, 0x00765310, 0x00767310,
    0x00769310, 0x0076b310, 0x0076d310, 0x0076f310,
    0x00771310, 0x00773310, 0x00775310, 0x00777310,
    0x00779310, 0x0077b310, 0x0077d310, 0x0077f310,
    0x00781310, 0x00783310, 0x00785310, 0x00787310,
    0x00789310, 0x0078b310, 0x0078d310, 0x0078f310,
    0x00791310, 0x00793310, 0x00795310, 0x00797310,
    0x00799310, 0x0079b310, 0x0079d310, 0x0079f310,
    0x007a1310, 0x007a3310, 0x007a5310, 0x007a7310,
    0x007a9310, 0x007ab310, 0x007ad310, 0x007af310,
    0x007b1310, 0x007b3310, 0x007b5310, 0x007b7310,
    0x007b9310, 0x007bb310, 0x007bd310, 0x007bf310,
    0x007c1310, 0x007c3310, 0x007c5310, 0x007c7310,
    0x007c9310, 0x007cb310, 0x007cd310, 0x007cf310,
    0x007d1310, 0x007d3310, 0x007d5310, 0x007d7310,
    0x007d9310, 0x007db310, 0x007dd310, 0x007df310,
    0x007e1310, 0x007e3310, 0x007e5310, 0x007e7310,
    0x007e9310, 0x007eb310, 0x007ed310, 0x007ef310,
    0x007f1310, 0x007f3310, 0x007f5310, 0x007f7310,
    0x007f9310, 0x007fb310, 0x007fd310, 0x007ff310,
    0x00801310, 0x00803310, 0x00805310, 0x00807310,
    0x00809310, 0x0080b310, 0x0080d310, 0x0080f310,
    0x00811310, 0x00813310, 0x00815310, 0x00817310,
    0x00819310, 0x0081b310, 0x0081d310, 0x0081f310,
    0x00821310, 0x00823310, 0x00825310, 0x00827310,
    0x00829310, 0x0082b310, 0x0082d310, 0x0082f310,
    0x00831310, 0x00833310, 0x00835310, 0x00837310,
    0x00839310, 0x0083b310, 0x0083d310, 0x0083f310,
    0x00841310, 0x00843310, 0x00845310, 0x00847310,
    0x00849310, 0x0084b310, 0x0084d310, 0x0084f310,
    0x00851310, 0x00853310, 0x00855310, 0x00857310,
    0x00859310, 0x0085b310, 0x0085d310, 0x0085f310,
    0x00861310, 0x00863310, 0x00865310, 0x00867310,
    0x00869310, 0x0086b310, 0x0086d310, 0x0086f310,
    0x00871310, 0x00873310, 0x00875310, 0x00877310,
    0x00879310, 0x0087b310, 0x0087d310, 0x0087f310,
    0x00881310, 0x00883310, 0x00885310, 0x00887310,
    0x00889310, 0x0088b310, 0x0088d310, 0x0088f310,
    0x00891310, 0x00893310, 0x00895310, 0x00897310,
    0x00899310, 0x0089b310, 0x0089d310, 0x0089f310,
    0x008a1310, 0x008a3310, 0x008a5310, 0x008a7310,
    0x008a9310, 0x008ab310, 0x008ad310, 0x008af310,
    0x008b1310, 0x008b3310, 0x008b5310, 0x008b7310,
    0x008b9310, 0x008bb310, 0x008bd310, 0x008bf310,
    0x008c1310, 0x008c3310, 0x008c5310, 0x008c7310,
    0x008c9310, 0x008cb310, 0x008cd310, 0x008cf310,
    0x008d1310, 0x008d3310, 0x008d5310, 0x008d7310,
    0x008d9310, 0x008db310, 0x008dd310, 0x008df310,
    0x008e1310, 0x008e3310, 0x008e5310, 0x008e7310,
    0x008e9310, 0x008eb310, 0x008ed310, 0x008ef310,
    0x008f1310, 0x008f3310, 0x008f5310, 0x008f7310,
    0x008f9310, 0x008fb310, 0x008fd310, 0x008ff310,
    0x00901310, 0x00903310, 0x00905310, 0x00907310,
    0x00909310, 0x0090b310, 0x0090d310, 0x0090f310,
    0x00911310, 0x00913310, 0x00915310, 0x00917310,
    0x00919310, 0x0091b310, 0x0091d310, 0x0091f310,
    0x00921310, 0x00923310, 0x00925310, 0x00927310,
    0x00929310, 0x0092b310, 0x0092d310, 0x0092f310,
    0x00931310, 0x00933310, 0x00935310, 0x00937310,
    0x00939310, 0x0093b310, 0x0093d310, 0x0093f310,
    0x00941310, 0x00943310, 0x00945310, 0x00947310,
    0x00949310, 0x0094b310, 0x0094d310, 0x0094f310,
    0x00951310, 0x00953310, 0x00955310, 0x00957310,
    0x00959310, 0x0095b310, 0x0095d310, 0x0095f310,
    0x00961310, 0x00963310, 0x00965310, 0x00967310,
    0x00969310, 0x0096b310, 0x0096d310, 0x0096f310,
    0x00971310, 0x00973310, 0x00975310, 0x00977310,
    0x00979310, 0x0097b310, 0x0097d310, 0x0097f310,
    0x00981310, 0x00983310, 0x00985310, 0x00987310,
    0x00989310, 0x0098b310, 0x0098d310, 0x0098f310,
    0x00991310, 0x00993310, 0x00995310, 0x00997310,
    0x00999310, 0x0099b310, 0x0099d310, 0x0099f310,
    0x009a1310, 0x009a3310, 0x009a5310, 0x009a7310,
    0x009a9310, 0x009ab310, 0x009ad310, 0x009af310,
    0x009b1310, 0x009b3310, 0x009b5310, 0x009b7310,
    0x009b9310, 0x009bb310, 0x009bd310, 0x009bf310,
    0x009c1310, 0x009c3310, 0x009c5310, 0x009c7310,
    0x009c9310, 0x009cb310, 0x009cd310, 0x009cf310,
    0x009d1310, 0x009d3310, 0x009d5310, 0x009d7310,
    0x009d9310, 0x009db310, 0x009dd310, 0x009df310,
    0x009e1310, 0x009e3310, 0x009e5310, 0x009e7310,
    0x009e9310, 0x009eb310, 0x009ed310, 0x009ef310,
    0x009f1310, 0x009f3310, 0x009f5310, 0x009f7310,
    0x009f9310, 0x009fb310, 0x009fd310, 0x009ff310,
    0x00a01310, 0x00a03310, 0x00a05310, 0x00a07310,
    0x00a09310, 0x00a0b310, 0x00a0d310, 0x00a0f310,
    0x00a11310, 0x00a13310, 0x00a15310, 0x00a17310,
    0x00a19310, 0x00a1b310, 0x00a1d310, 0x00a1f310,
    0x00a21310, 0x00a23310, 0x00a25310, 0x00a27310,
    0x00a29310, 0x00a2b310, 0x00a2d310, 0x00a2f310,
    0x00a31310, 0x00a33310, 0x00a35310, 0x00a37310,
    0x00a39310, 0x00a3b310, 0x00a3d310, 0x00a3f310,
    0x00a41310, 0x00a43310, 0x00a45310, 0x00a47310,
    0x00a49310, 0x00a4b310, 0x00a4d310, 0x00a4f310,
    0x00a51310, 0x00a53310, 0x00a55310, 0x00a57310,
    0x00a59310, 0x00a5b310, 0x00a5d310, 0x00a5f310,
    0x00a61310, 0x00a63310, 0x00a65310, 0x00a67310,
    0x00a69310, 0x00a6b310, 0x00a6d310, 0x00a6f310,
    0x00a71310, 0x00a73310, 0x00a75310, 0x00a77310,
    0x00a79310, 0x00a7b310, 0x00a7d310, 0x00a7f310,
    0x00a81310, 0x00a83310, 0x00a85310, 0x00a87310,
    0x00a89310, 0x00a8b310, 0x00a8d310, 0x00a8f310,
    0x00a91310, 0x00a93310, 0x00a95310, 0x00a97310,
    0x00a99310, 0x00a9b310, 0x00a9d310, 0x00a9f310,
    0x00aa1310, 0x00aa3310, 0x00aa5310, 0x00aa7310,
    0x00aa9310, 0x00aab310, 0x00aad310, 0x00aaf310,
    0x00ab1310, 0x00ab3310, 0x00ab5310, 0x00ab7310,
    0x00ab9310, 0x00abb310, 0x00abd310, 0x00abf310,
    0x00ac1310, 0x00ac3310, 0x00ac5310, 0x00ac7310,
    0x00ac9310, 0x00acb310, 0x00acd310, 0x00acf310,
    0x00ad1310, 0x00ad3310, 0x00ad5310, 0x00ad7310,
    0x00ad9310, 0x00adb310, 0x00add310, 0x00adf310,
    0x00ae1310, 0x00ae3310, 0x00ae5310, 0x00ae7310,
    0x00ae9310, 0x00aeb310, 0x00aed310, 0x00aef310,
    0x00af1310, 0x00af3310, 0x00af5310, 0x00af7310,
    0x00af9310, 0x00afb310, 0x00afd310, 0x00aff310,
    0x00b01310, 0x00b03310, 0x00b05310, 0x00b07310,
    0x00b09310, 0x00b0b310, 0x00b0d310, 0x00b0f310,
    0x00b11310, 0x00b13310, 0x00b15310, 0x00b17310,
    0x00b19310, 0x00b1b310, 0x00b1d310, 0x00b1f310,
    0x00b21310, 0x00b23310, 0x00b25310, 0x00b27310,
    0x00b29310, 0x00b2b310, 0x00b2d310, 0x00b2f310,
    0x00b31310, 0x00b33310, 0x00b35310, 0x00b37310,
    0x00b39310, 0x00b3b310, 0x00b3d310, 0x00b3f310,
    0x00b41310, 0x00b43310, 0x00b45310, 0x00b47310,
    0x00b49310, 0x00b4b310, 0x00b4d310, 0x00b4f310,
    0x00b51310, 0x00b53310, 0x00b55310, 0x00b57310,
    0x00b59310, 0x00b5b310, 0x00b5d310, 0x00b5f310,
    0x00b61310, 0x00b63310, 0x00b65310, 0x00b67310,
    0x00b69310, 0x00b6b310, 0x00b6d310, 0x00b6f310,
    0x00b71310, 0x00b73310, 0x00b75310, 0x00b77310,
    0x00b79310, 0x00b7b310, 0x00b7d310, 0x00b7f310,
    0x00b81310, 0x00b83310, 0x00b85310, 0x00b87310,
    0x00b89310, 0x00b8b310, 0x00b8d310, 0x00b8f310,
    0x00b91310, 0x00b93310, 0x00b95310, 0x00b97310,
    0x00b99310, 0x00b9b310, 0x00b9d310, 0x00b9f310,
    0x00ba1310, 0x00ba3310, 0x00ba5310, 0x00ba7310,
    0x00ba9310, 0x00bab310, 0x00bad310, 0x00baf310,
    0x00bb1310, 0x00bb3310, 0x00bb5310, 0x00bb7310,
    0x00bb9310, 0x00bbb310, 0x00bbd310, 0x00bbf310,
    0x00bc1310, 0x00bc3310, 0x00bc5310, 0x00bc7310,
    0x00bc9310, 0x00bcb310, 0x00bcd310, 0x00bcf310,
    0x00bd1310, 0x00bd3310, 0x00bd5310, 0x00bd7310,
    0x00bd9310, 0x00bdb310, 0x00bdd310, 0x00bdf310,
    0x00be1310, 0x00be3310, 0x00be5310, 0x00be7310,
    0x00be9310, 0x00beb310, 0x00bed310, 0x00bef310,
    0x00bf1310, 0x00bf3310, 0x00bf5310, 0x00bf7310,
    0x00bf9310, 0x00bfb310, 0x00bfd310, 0x00bff310,
    0x00c01310, 0x00c03310, 0x00c05310, 0x00c07310,
    0x00c09310, 0x00c0b310, 0x00c0d310, 0x00c0f310,
    0x00c11310, 0x00c13310, 0x00c15310, 0x00c17310,
    0x00c19310, 0x00c1b310, 0x00c1d310, 0x00c1f310,
    0x00c21310, 0x00c23310, 0x00c25310, 0x00c27310,
    0x00c29310, 0x00c2b310, 0x00c2d310, 0x00c2f310,
    0x00c31310, 0x00c33310, 0x00c35310, 0x00c37310,
    0x00c39310, 0x00c3b310, 0x00c3d310, 0x00c3f310,
    0x00c41310, 0x00c43310, 0x00c45310, 0x00c47310,
    0x00c49310, 0x00c4b310, 0x00c4d310, 0x00c4f310,
    0x00c51310, 0x00c53310, 0x00c55310, 0x00c57310,
    0x00c59310, 0x00c5b310, 0x00c5d310, 0x00c5f310,
    0x00c61310, 0x00c63310, 0x00c65310, 0x00c67310,
    0x00c69310, 0x00c6b310, 0x00c6d310, 0x00c6f310,
    0x00c71310, 0x00c73310, 0x00c75310, 0x00c77310,
    0x00c79310, 0x00c7b310, 0x00c7d310, 0x00c7f310,
    0x00c81310, 0x00c83310, 0x00c85310, 0x00c87310,
    0x00c89310, 0x00c8b310, 0x00c8d310, 0x00c8f310,
    0x00c91310, 0x00c93310, 0x00c95310, 0x00c97310,
    0x00c99310, 0x00c9b310, 0x00c9d310, 0x00c9f310,
    0x00ca1310, 0x00ca3310, 0x00ca5310, 0x00ca7310,
    0x00ca9310, 0x00cab310, 0x00cad310, 0x00caf310,
    0x00cb1310, 0x00cb3310, 0x00cb5310, 0x00cb7310,
    0x00cb9310, 0x00cbb310, 0x00cbd310, 0x00cbf310,
    0x00cc1310, 0x00cc3310, 0x00cc5310, 0x00cc7310,
    0x00cc9310, 0x00ccb310, 0x00ccd310, 0x00ccf310,
    0x00cd1310, 0x00cd3310, 0x00cd5310, 0x00cd7310,
    0x00cd9310, 0x00cdb310, 0x00cdd310, 0x00cdf310,
    0x00ce1310, 0x00ce3310, 0x00ce5310, 0x00ce7310,
    0x00ce9310, 0x00ceb310, 0x00ced310, 0x00cef310,
    0x00cf1310, 0x00cf3310, 0x00cf5310, 0x00cf7310,
    0x00cf9310, 0x00cfb310, 0x00cfd310, 0x00cff310,
    0x00d01310, 0x00d03310, 0x00d05310, 0x00d07310,
    0x00d09310, 0x00d0b310, 0x00d0d310, 0x00d0f310,
    0x00d11310, 0x00d13310, 0x00d15310, 0x00d17310,
    0x00d19310, 0x00d1b310, 0x00d1d310, 0x00d1f310,
    0x00d21310, 0x00d23310, 0x00d25310, 0x00d27310,
    0x00d29310, 0x00d2b310, 0x00d2d310, 0x00d2f310,
    0x00d31310, 0x00d33310, 0x00d35310, 0x00d37310,
    0x00d39310, 0x00d3b310, 0x00d3d310, 0x00d3f310,
    0x00d41310, 0x00d43310, 0x00d45310, 0x00d47310,
    0x00d49310, 0x00d4b310, 0x00d4d310, 0x00d4f310,
    0x00d51310, 0x00d53310, 0x00d55310, 0x00d57310,
    0x00d59310, 0x00d5b310, 0x00d5d310, 0x00d5f310,
    0x00d61310, 0x00d63310, 0x00d65310, 0x00d67310,
    0x00d69310, 0x00d6b310, 0x00d6d310, 0x00d6f310,
    0x00d71310, 0x00d73310, 0x00d75310, 0x00d77310,
    0x00d79310, 0x00d7b310, 0x00d7d310, 0x00d7f310,
    0x00d81310, 0x00d83310, 0x00d85310, 0x00d87310,
    0x00d89310, 0x00d8b310, 0x00d8d310, 0x00d8f310,
    0x00d91310, 0x00d93310, 0x00d95310, 0x00d97310,
    0x00d99310, 0x00d9b310, 0x00d9d310, 0x00d9f310,
    0x00da1310, 0x00da3310, 0x00da5310, 0x00da7310,
    0x00da9310, 0x00dab310, 0x00dad310, 0x00daf310,
    0x00db1310, 0x00db3310, 0x00db5310, 0x00db7310,
    0x00db9310, 0x00dbb310, 0x00dbd310, 0x00dbf310,
    0x00dc1310, 0x00dc3310, 0x00dc5310, 0x00dc7310,
    0x00dc9310, 0x00dcb310, 0x00dcd310, 0x00dcf310,
    0x00dd1310, 0x00dd3310, 0x00dd5310, 0x00dd7310,
    0x00dd9310, 0x00ddb310, 0x00ddd310, 0x00ddf310,
    0x00de1310, 0x00de3310, 0x00de5310, 0x00de7310,
    0x00de9310, 0x00deb310, 0x00ded310, 0x00def310,
    0x00df1310, 0x00df3310, 0x00df5310, 0x00df7310,
    0x00df9310, 0x00dfb310, 0x00dfd310, 0x00dff310,
    0x00e01310, 0x00e03310, 0x00e05310, 0x00e07310,
    0x00e09310, 0x00e0b310, 0x00e0d310, 0x00e0f310,
    0x00e11310, 0x00e13310, 0x00e15310, 0x00e17310,
    0x00e19310, 0x00e1b310, 0x00e1d310, 0x00e1f310,
    0x00e21310, 0x00e23310, 0x00e25310, 0x00e27310,
    0x00e29310, 0x00e2b310, 0x00e2d310, 0x00e2f310,
    0x00e31310, 0x00e33310, 0x00e35310, 0x00e37310,
    0x00e39310, 0x00e3b310, 0x00e3d310, 0x00e3f310,
    0x00e41310, 0x00e43310, 0x00e45310, 0x00e47310,
    0x00e49310, 0x00e4b310, 0x00e4d310, 0x00e4f310,
    0x00e51310, 0x00e53310, 0x00e55310, 0x00e57310,
    0x00e59310, 0x00e5b310, 0x00e5d310, 0x00e5f310,
    0x00e61310, 0x00e63310, 0x00e65310, 0x00e67310,
    0x00e69310, 0x00e6b310, 0x00e6d310, 0x00e6f310,
    0x00e71310, 0x00e73310, 0x00e75310, 0x00e77310,
    0x00e79310, 0x00e7b310, 0x00e7d310, 0x00e7f310,
    0x00e81310, 0x00e83310, 0x00e85310, 0x00e87310,
    0x00e89310, 0x00e8b310, 0x00e8d310, 0x00e8f310,
    0x00e91310, 0x00e93310, 0x00e95310, 0x00e97310,
    0x00e99310, 0x00e9b310, 0x00e9d310, 0x00e9f310,
    0x00ea1310, 0x00ea3310, 0x00ea5310, 0x00ea7310,
    0x00ea9310, 0x00eab310, 0x00ead310, 0x00eaf310,
    0x00eb1310, 0x00eb3310, 0x00eb5310, 0x00eb7310,
    0x00eb9310, 0x00ebb310, 0x00ebd310, 0x00ebf310,
    0x00ec1310, 0x00ec3310, 0x00ec5310, 0x00ec7310,
    0x00ec9310, 0x00ecb310, 0x00ecd310, 0x00ecf310,
    0x00ed1310, 0x00ed3310, 0x00ed5310, 0x00ed7310,
    0x00ed9310, 0x00edb310, 0x00edd310, 0x00edf310,
    0x00ee1310, 0x00ee3310, 0x00ee5310, 0x00ee7310,
    0x00ee9310, 0x00eeb310, 0x00eed310, 0x00eef310,
    0x00ef1310, 0x00ef3310, 0x00ef5310, 0x00ef7310,
    0x00ef9310, 0x00efb310, 0x00efd310, 0x00eff310,
    0x00f01310, 0x00f03310, 0x00f05310, 0x00f07310,
    0x00f09310, 0x00f0b310, 0x00f0d310, 0x00f0f310,
    0x00f11310, 0x00f13310, 0x00f15310, 0x00f17310,
    0x00f19310, 0x00f1b310, 0x00f1d310, 0x00f1f310,
    0x00f21310, 0x00f23310, 0x00f25310, 0x00f27310,
    0x00f29310, 0x00f2b310, 0x00f2d310, 0x00f2f310,
    0x00f31310, 0x00f33310, 0x00f35310, 0x00f37310,
    0x00f39310, 0x00f3b310, 0x00f3d310, 0x00f3f310,
    0x00f41310, 0x00f43310, 0x00f45310, 0x00f47310,
    0x00f49310, 0x00f4b310, 0x00f4d310, 0x00f4f310,
    0x00f51310, 0x00f53310, 0x00f55310, 0x00f57310,
    0x00f59310, 0x00f5b310, 0x00f5d310, 0x00f5f310,
    0x00f61310, 0x00f63310, 0x00f65310, 0x00f67310,
    0x00f69310, 0x00f6b310, 0x00f6d310, 0x00f6f310,
    0x00f71310, 0x00f73310, 0x00f75310, 0x00f77310,
    0x00f79310, 0x00f7b310, 0x00f7d310, 0x00f7f310,
    0x00f81310, 0x00f83310, 0x00f85310, 0x00f87310,
    0x00f89310, 0x00f8b310, 0x00f8d310, 0x00f8f310,
    0x00f91310, 0x00f93310, 0x00f95310, 0x00f97310,
    0x00f99310, 0x00f9b310, 0x00f9d310, 0x00f9f310,
    0x00fa1310, 0x00fa3310, 0x00fa5310, 0x00fa7310,
    0x00fa9310, 0x00fab310, 0x00fad310, 0x00faf310,
    0x00fb1310, 0x00fb3310, 0x00fb5310, 0x00fb7310,
    0x00fb9310, 0x00fbb310, 0x00fbd310, 0x00fbf310,
    0x00fc1310, 0x00fc3310, 0x00fc5310, 0x00fc7310,
    0x00fc9310, 0x00fcb310, 0x00fcd310, 0x00fcf310,
    0x00fd1310, 0x00fd3310, 0x00fd5310, 0x00fd7310,
    0x00fd9310, 0x00fdb310, 0x00fdd310, 0x00fdf310,
    0x00fe1310, 0x00fe3310, 0x00fe5310, 0x00fe7310,
    0x00fe9310, 0x00feb310, 0x00fed310, 0x00fef310,
    0x00ff1310, 0x00ff3310, 0x00ff5310, 0x00ff7310,
    0x00ff9310, 0x00ffb310, 0x00ffd310, 0x00fff310,
};
