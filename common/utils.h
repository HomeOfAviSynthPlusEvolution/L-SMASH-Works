/*****************************************************************************
 * utils.h
 *****************************************************************************
 * Copyright (C) 2012-2015 L-SMASH Works project
 *
 * Authors: Yusuke Nakamura <muken.the.vfrmaniac@gmail.com>
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

#ifndef UTILS_H
#define UTILS_H

#include <stdlib.h>
#include <stdint.h>
#include <assert.h>

#define MIN( a, b ) ((a) < (b) ? (a) : (b))
#define MAX( a, b ) ((a) > (b) ? (a) : (b))
#define CLIP_VALUE( value, min, max ) ((value) > (max) ? (max) : (value) < (min) ? (min) : (value))

#ifndef _countof
#define _countof( _Array ) ( sizeof(_Array) / sizeof(_Array[0]) )
#endif

#define LW_STRINGFY( s ) #s

typedef enum
{
    LW_LOG_INFO = 0,
    LW_LOG_WARNING,
    LW_LOG_ERROR,
    LW_LOG_FATAL,
    LW_LOG_QUIET,
} lw_log_level;

typedef struct lw_log_handler_tag lw_log_handler_t;

struct lw_log_handler_tag
{
    const char  *name;
    lw_log_level level;
    void        *priv;
    void (*show_log)( lw_log_handler_t *, lw_log_level, const char *message );
};

#ifdef __cplusplus
extern "C"
{
#endif  /* __cplusplus */

void *lw_malloc_zero
(
    size_t size
);

void  lw_free
(
    void *pointer
);

void  lw_freep
(
    void *pointer
);

void *lw_memdup
(
    void  *src,
    size_t size
);

const char **lw_tokenize_string
(
    char  *str,         /* null-terminated string: separator charactors will be replaced with '\0'. */
    char   separator,   /* separator */
    char **bufs         /* If NULL, allocate memory block internally, which can be deallocated by lw_freep(). */
);

/*****************************************************************************
 * Non-public functions
 *****************************************************************************/
void lw_log_show
(
    lw_log_handler_t *lhp,
    lw_log_level      level,
    const char       *format,
    ...
);

static inline uint64_t get_gcd
(
    uint64_t a,
    uint64_t b
)
{
    if( !b )
        return a;
    while( 1 )
    {
        uint64_t c = a % b;
        if( !c )
            return b;
        a = b;
        b = c;
    }
}

static inline uint64_t reduce_fraction
(
    uint64_t *a,
    uint64_t *b
)
{
    uint64_t reduce = get_gcd( *a, *b );
    *a /= reduce;
    *b /= reduce;
    return reduce;
}

int lw_check_file_extension
(
    const char *file_name,
    const char *extension
);

int lw_try_rational_framerate
(
    double   framerate,
    int64_t *framerate_num,
    int64_t *framerate_den,
    uint64_t timebase
);

#ifdef __cplusplus
}
#endif  /* __cplusplus */

#endif // !UTILS_H
