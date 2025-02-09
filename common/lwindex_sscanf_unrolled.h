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

#include <ctype.h>
#include <limits.h>

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

    const char* min_str = "-9223372036854775808";
    if (strncmp(s, min_str, strlen(min_str)) == 0) {
        *endptr = (char*)s + strlen(min_str);
        return LLONG_MIN;
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
        if (isdigit(c))
            c -= '0';
        else
            break;
        if (c >= 10)
            break;
        if (acc > (LLONG_MAX - c) / 10) {
            acc = LLONG_MAX;
            errno = ERANGE;
            return neg ? LLONG_MIN : LLONG_MAX;
        }
        acc *= 10;
        acc += c;
    }
    if (endptr != NULL)
        *endptr = (char *)s;
    return (neg ? -acc : acc);
}

static inline int my_strto_int(const char *nptr, char **endptr) {
    const char *s = nptr;
    int acc;
    int c;
    int neg = 0;

    const char* min_str = "-2147483648";
    if (strncmp(s, min_str, strlen(min_str)) == 0) {
        *endptr = (char*)s + strlen(min_str);
        return INT_MIN;
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
        if (isdigit(c))
            c -= '0';
        else
            break;
        if (c >= 10)
            break;
        if (acc > (INT_MAX - c) / 10) {
            acc = INT_MAX;
            errno = ERANGE;
            return neg ? INT_MIN : INT_MAX;
        }
        acc *= 10;
        acc += c;
    }
    if (endptr != NULL)
        *endptr = (char *)s;
    return (neg ? -acc : acc);
}

/*
    Unroll the following sscanf:
    sscanf( buf, "Index=%d,POS=%" SCNd64 ",PTS=%" SCNd64 ",DTS=%" SCNd64 ",EDI=%d",
            &stream_index, &pos, &pts, &dts, &extradata_index )
*/
static inline int sscanf_unrolled1( const char *buf, int *stream_index, int64_t *pos, int64_t *pts, int64_t *dts, int *extradata_index )
{
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
static inline int sscanf_unrolled2( const char *buf, int *key, int *pict_type, int *poc, int *repeat_pict, int *field_info )
{
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
static inline int sscanf_unrolled3( const char *buf, int *frame_length )
{
    char *p = (char *)buf;
    int parsed_count = 0;

    PARSE_OR_RETURN(p, "Length=", *frame_length, int);

    return parsed_count;
}
