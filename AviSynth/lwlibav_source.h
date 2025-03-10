/*****************************************************************************
 * lwlibav_source.h
 *****************************************************************************
 * Copyright (C) 2013-2015 L-SMASH Works project
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

#define NO_PROGRESS_HANDLER

#include "../common/progress.h"
#include "../common/lwlibav_dec.h"
#include "../common/lwlibav_video.h"
#include "../common/lwlibav_audio.h"
#include "../common/lwindex.h"

class LWLibavSource : public LSMASHSource
{
protected:
    lwlibav_file_handler_t lwh;
    std::unique_ptr< lwlibav_video_decode_handler_t, decltype( &lwlibav_video_free_decode_handler ) > vdhp;
    std::unique_ptr< lwlibav_video_output_handler_t, decltype( &lwlibav_video_free_output_handler ) > vohp;
    std::unique_ptr< lwlibav_audio_decode_handler_t, decltype( &lwlibav_audio_free_decode_handler ) > adhp;
    std::unique_ptr< lwlibav_audio_output_handler_t, decltype( &lwlibav_audio_free_output_handler ) > aohp;
    inline void free_video_decode_handler( void ) { vdhp.reset( nullptr ); }
    inline void free_video_output_handler( void ) { vohp.reset( nullptr ); }
    inline void free_audio_decode_handler( void ) { adhp.reset( nullptr ); }
    inline void free_audio_output_handler( void ) { aohp.reset( nullptr ); }
    LWLibavSource()
      : vdhp{ lwlibav_video_alloc_decode_handler(), lwlibav_video_free_decode_handler },
        vohp{ lwlibav_video_alloc_output_handler(), lwlibav_video_free_output_handler },
        adhp{ lwlibav_audio_alloc_decode_handler(), lwlibav_audio_free_decode_handler },
        aohp{ lwlibav_audio_alloc_output_handler(), lwlibav_audio_free_output_handler } {};
    ~LWLibavSource() = default;
    LWLibavSource( const LWLibavSource & ) = delete;
    LWLibavSource & operator= ( const LWLibavSource & ) = delete;
};

class LWLibavVideoSource : public LWLibavSource
{
private:
    LWLibavVideoSource() = default;
    bool has_at_least_v8;
    AVFrame* av_frame;
public:
    LWLibavVideoSource
    (
        lwlibav_option_t   *opt,
        int                 seek_mode,
        uint32_t            forward_seek_threshold,
        int                 direct_rendering,
        enum AVPixelFormat  pixel_format,
        const char         *preferred_decoder_names,
        int                 prefer_hw_decoder,
        bool                progress,
        const char         *ff_options,
        IScriptEnvironment *env
    );
    ~LWLibavVideoSource();
    PVideoFrame __stdcall GetFrame( int n, IScriptEnvironment *env );
    bool __stdcall GetParity( int n );
    void __stdcall GetAudio( void *buf, int64_t start, int64_t count, IScriptEnvironment *env ) {}
};

class LWLibavAudioSource : public LWLibavSource
{
private:
    LWLibavAudioSource() = default;
    int delay_audio( int64_t *start, int64_t wanted_length );
    int fill_audio_gaps(void* buf, int64_t* start, int64_t wanted_length, lwlibav_audio_decode_handler_t* adhp,
        lwlibav_audio_output_handler_t* aohp, VideoInfo& vi);
public:
    LWLibavAudioSource
    (
        lwlibav_option_t   *opt,
        const char         *channel_layout,
        int                 sample_rate,
        const char         *preferred_decoder_names,
        bool                progress,
        const double        drc,
        const char         *ff_options,
        int                 fill_audio_gaps,
        IScriptEnvironment *env
    );
    ~LWLibavAudioSource();
    PVideoFrame __stdcall GetFrame( int n, IScriptEnvironment *env ) { return NULL; }
    bool __stdcall GetParity( int n ) { return false; }
    void __stdcall GetAudio( void *buf, int64_t start, int64_t wanted_length, IScriptEnvironment *env );
};
