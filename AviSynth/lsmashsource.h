/*****************************************************************************
 * lsmashsource.h
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

/* This file is available under an ISC license.
 * However, when distributing its binary file, it will be under LGPL or GPL. */

#include <memory>
#include <string.h>

#include "../common/cpp_compat.h"
#include "../common/utils.h"

#include "avisynth.h"

class LSMASHSource : public IClip
{
protected:
    VideoInfo vi;
    char      preferred_decoder_names_buf[512];
    inline void set_preferred_decoder_names
    (
        const char *preferred_decoder_names
    )
    {
        memset( preferred_decoder_names_buf, 0, sizeof(preferred_decoder_names_buf) );
        if( preferred_decoder_names )
            memcpy( preferred_decoder_names_buf,
                    preferred_decoder_names,
                    MIN( sizeof(preferred_decoder_names_buf) - 1, strlen(preferred_decoder_names) ) );
    }
    inline const char **tokenize_preferred_decoder_names( void )
    {
        return lw_tokenize_string( preferred_decoder_names_buf, ',', nullptr );
    }
    int __stdcall SetCacheHints( int cachehints, int frame_range ) { return cachehints == CACHE_GET_MTMODE ? MT_SERIALIZED : 0; }
    const VideoInfo& __stdcall GetVideoInfo() { return vi; }
};

void throw_error
(
    lw_log_handler_t *lhp,
    lw_log_level      level,
    const char       *message
);
