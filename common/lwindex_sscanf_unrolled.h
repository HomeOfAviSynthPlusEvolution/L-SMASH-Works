/*****************************************************************************
 * lwindex_sscanf_unrolled.h
 *****************************************************************************
 * Copyright (C) 2012-2025 L-SMASH Works project
 *
 * Authors: Xinyue Lu <i@7086.in>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *****************************************************************************/

/* This file is available under an ISC license. */

#include <limits.h>
#include <errno.h>
#include <stdint.h>
#include <immintrin.h>

#define PARSE_OR_RETURN(buf, needle, out, type) \
    if (strncmp(buf, needle, strlen(needle)) != 0) \
        return parsed_count; \
    buf += strlen(needle); \
    out = my_strto_##type(buf, &buf); \
    parsed_count++;

#define CHECK_COMMA_OR_RETURN(p, parsed_count) \
    if (*p != ',') \
        return parsed_count; \
    p++;

static inline int64_t my_strto_int64_t(const char *nptr, char **endptr) {
    const char *s = nptr;
    int64_t acc;
    int c;
    int neg = 0;

    /* Process sign. */
    if (*s == '-') {
        neg = 1;
        s++;
    } else if (*s == '+')
        s++;

    acc = 0;

    #if defined(__SSE4_1__)
        __m128i zero = _mm_set1_epi8('0');
        __m128i data = _mm_loadu_si128((const __m128i *)s);
        __m128i add_208 = _mm_sub_epi8(data, _mm_set1_epi8('0'));
        __m128i min_9 = _mm_min_epu8(add_208, _mm_set1_epi8(9));
        __m128i is_digit = _mm_cmpeq_epi8(add_208, min_9);
        int mask = _mm_movemask_epi8(is_digit);

        if ((mask & 0xFF) == 0xFF) {
            __m128i digits = _mm_sub_epi8(data, zero);
            __m128i input = _mm_cvtepi8_epi32(digits);
            __m128i multipliers = _mm_set_epi32(10000, 100000, 1000000, 10000000);
            __m128i results = _mm_mullo_epi32(input, multipliers);
            __m128i sum_vec = _mm_hadd_epi32(results, results);
            sum_vec = _mm_hadd_epi32(sum_vec, sum_vec);
            acc -= _mm_extract_epi32(sum_vec, 0);
            input = _mm_cvtepi8_epi32(_mm_srli_si128(digits, 4));
            multipliers = _mm_set_epi32(1, 10, 100, 1000);
            results = _mm_mullo_epi32(input, multipliers);
            sum_vec = _mm_hadd_epi32(results, results);
            sum_vec = _mm_hadd_epi32(sum_vec, sum_vec);
            acc -= _mm_extract_epi32(sum_vec, 0);
            s += 8;
        }
        else if ((mask & 0xF) == 0xF) {
            __m128i digits = _mm_sub_epi8(data, zero);
            __m128i input = _mm_cvtepi8_epi32(digits);
            __m128i multipliers = _mm_set_epi32(1, 10, 100, 1000);
            __m128i results = _mm_mullo_epi32(input, multipliers);
            __m128i sum_vec = _mm_hadd_epi32(results, results);
            sum_vec = _mm_hadd_epi32(sum_vec, sum_vec);
            acc -= _mm_extract_epi32(sum_vec, 0);
            s += 4;
        }
    #endif

    for (;;s++) {
        c = *s;
        if (c >= '0' && c <= '9')
            c -= '0';
        else
            break;
        if (c >= 10)
            break;

        if (acc < LLONG_MIN / 10) {
            errno = ERANGE;
            return neg ? LLONG_MIN : LLONG_MAX;
        }
        acc *= 10;

        if (acc < LLONG_MIN + c) {
            errno = ERANGE;
            return neg ? LLONG_MIN : LLONG_MAX;
        }
        acc -= c;
    }
    if (endptr != NULL)
        *endptr = (char *)s;

    if (!neg && acc == LLONG_MIN) {
        errno = ERANGE;
        return LLONG_MAX;
    }

    return (neg ? acc : -acc);
}

static inline int my_strto_int(const char *nptr, char **endptr) {
    const char *s = nptr;
    int acc;
    int c;
    int neg = 0;

    if (s[0] >= '0' && s[0] <= '9' && s[1] == ',') {
        *endptr = (char *)s + 1;
        return s[0] - '0';
    }

    /* Process sign. */
    if (*s == '-') {
        neg = 1;
        s++;
    } else if (*s == '+')
        s++;

    acc = 0;
    for (;;s++) {
        c = *s;
        if (c >= '0' && c <= '9')
            c -= '0';
        else
            break;
        if (c >= 10)
            break;

        if (acc < INT_MIN / 10) {
            errno = ERANGE;
            return neg ? INT_MIN : INT_MAX;
        }
        acc *= 10;

        if (acc < INT_MIN + c) {
            errno = ERANGE;
            return neg ? INT_MIN : INT_MAX;
        }
        acc -= c;
    }
    if (endptr != NULL)
        *endptr = (char *)s;

    if (!neg && acc == INT_MIN) {
        errno = ERANGE;
        return INT_MAX;
    }

    return (neg ? acc : -acc);
}

/*
    Unroll the following sscanf:
    sscanf( buf, "Index=%d,POS=%" SCNd64 ",PTS=%" SCNd64 ",DTS=%" SCNd64 ",EDI=%d",
            &stream_index, &pos, &pts, &dts, &extradata_index )
*/
static inline int sscanf_unrolled_main_index(const char *buf, int *stream_index, int64_t *pos, int64_t *pts, int64_t *dts, int *extradata_index) {
    char *p = (char *)buf;
    int parsed_count = 0;

    PARSE_OR_RETURN(p, "Index=", *stream_index, int);
    CHECK_COMMA_OR_RETURN(p, parsed_count);
    PARSE_OR_RETURN(p, "POS=", *pos, int64_t);
    CHECK_COMMA_OR_RETURN(p, parsed_count);
    PARSE_OR_RETURN(p, "PTS=", *pts, int64_t);
    CHECK_COMMA_OR_RETURN(p, parsed_count);
    PARSE_OR_RETURN(p, "DTS=", *dts, int64_t);
    CHECK_COMMA_OR_RETURN(p, parsed_count);
    PARSE_OR_RETURN(p, "EDI=", *extradata_index, int);

    return parsed_count;
}

/*
    Unroll the following sscanf:
    sscanf( buf, "Key=%d,Pic=%d,POC=%d,Repeat=%d,Field=%d",
            &key, &pict_type, &poc, &repeat_pict, &field_info )
*/
static inline int sscanf_unrolled_video_index(const char *buf, int *key, int *pict_type, int *poc, int *repeat_pict, int *field_info) {
    char *p = (char *)buf;
    int parsed_count = 0;

    PARSE_OR_RETURN(p, "Key=", *key, int);
    CHECK_COMMA_OR_RETURN(p, parsed_count);
    PARSE_OR_RETURN(p, "Pic=", *pict_type, int);
    CHECK_COMMA_OR_RETURN(p, parsed_count);
    PARSE_OR_RETURN(p, "POC=", *poc, int);
    CHECK_COMMA_OR_RETURN(p, parsed_count);
    PARSE_OR_RETURN(p, "Repeat=", *repeat_pict, int);
    CHECK_COMMA_OR_RETURN(p, parsed_count);
    PARSE_OR_RETURN(p, "Field=", *field_info, int);

    return parsed_count;
}

/*
    Unroll the following sscanf:
    sscanf( buf, "Length=%d", &frame_length )
*/
static inline int sscanf_unrolled_audio_index(const char *buf, int *frame_length) {
    char *p = (char *)buf;
    int parsed_count = 0;

    PARSE_OR_RETURN(p, "Length=", *frame_length, int);

    return parsed_count;
}
