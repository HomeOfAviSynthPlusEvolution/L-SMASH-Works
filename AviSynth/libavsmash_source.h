/*****************************************************************************
 * libavsmash_source.h
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

#include "../common/libavsmash.h"
#include "../common/libavsmash_video.h"
#include "../common/libavsmash_audio.h"

class LibavSMASHSource : public LSMASHSource
{
private:
    void (*free_format_ctx)( AVFormatContext *p )
        = []( AVFormatContext *p ){ avformat_close_input( &p ); };
protected:
    lsmash_file_parameters_t file_param;
    std::unique_ptr< AVFormatContext, decltype( free_format_ctx ) > format_ctx;
    LibavSMASHSource() : file_param{}, format_ctx{ nullptr, free_format_ctx } {}
    ~LibavSMASHSource() = default;
    LibavSMASHSource( const LibavSMASHSource & ) = delete;
    LibavSMASHSource & operator= ( const LibavSMASHSource & ) = delete;
};

class LSMASHVideoSource : public LibavSMASHSource
{
private:
    std::unique_ptr< libavsmash_video_decode_handler_t, decltype( &libavsmash_video_free_decode_handler ) > vdhp;
    std::unique_ptr< libavsmash_video_output_handler_t, decltype( &libavsmash_video_free_output_handler ) > vohp;
    LSMASHVideoSource()
      : LibavSMASHSource{},
        vdhp{ libavsmash_video_alloc_decode_handler(), libavsmash_video_free_decode_handler },
        vohp{ libavsmash_video_alloc_output_handler(), libavsmash_video_free_output_handler } {}
    uint32_t open_file
    (
        const char                        *source,
        IScriptEnvironment                *env
    );
    void get_video_track
    (
        const char                        *source,
        uint32_t                           track_number,
        IScriptEnvironment                *env
    );
public:
    LSMASHVideoSource
    (
        const char         *source,
        uint32_t            track_number,
        int                 threads,
        int                 seek_mode,
        uint32_t            forward_seek_threshold,
        int                 direct_rendering,
        int                 fps_num,
        int                 fps_den,
        enum AVPixelFormat  pixel_format,
        const char         *preferred_decoder_names,
        int                 prefer_hw_decoder,
        IScriptEnvironment *env
    );
    ~LSMASHVideoSource();
    PVideoFrame __stdcall GetFrame( int n, IScriptEnvironment *env );
    bool __stdcall GetParity( int n ) { return false; }
    void __stdcall GetAudio( void *buf, int64_t start, int64_t count, IScriptEnvironment *env ) {}
};

class LSMASHAudioSource : public LibavSMASHSource
{
private:
    std::unique_ptr< libavsmash_audio_decode_handler_t, decltype( &libavsmash_audio_free_decode_handler ) > adhp;
    std::unique_ptr< libavsmash_audio_output_handler_t, decltype( &libavsmash_audio_free_output_handler ) > aohp;
    LSMASHAudioSource()
      : LibavSMASHSource{},
        adhp{ libavsmash_audio_alloc_decode_handler(), libavsmash_audio_free_decode_handler },
        aohp{ libavsmash_audio_alloc_output_handler(), libavsmash_audio_free_output_handler } {}
    uint32_t open_file
    (
        const char         *source,
        IScriptEnvironment *env
    );
    void get_audio_track
    (
        const char         *source,
        uint32_t            track_number,
        IScriptEnvironment *env
    );
public:
    LSMASHAudioSource
    (
        const char         *source,
        uint32_t            track_number,
        bool                skip_priming,
        uint64_t            channel_layout,
        int                 sample_rate,
        const char         *preferred_decoder_names,
        IScriptEnvironment *env
    );
    ~LSMASHAudioSource();
    PVideoFrame __stdcall GetFrame( int n, IScriptEnvironment *env ) { return nullptr; }
    bool __stdcall GetParity( int n ) { return false; }
    void __stdcall GetAudio( void *buf, int64_t start, int64_t wanted_length, IScriptEnvironment *env );
};
