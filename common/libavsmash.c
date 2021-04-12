/*****************************************************************************
 * libavsmash.c / libavsmash.cpp
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

#include "cpp_compat.h"

#include <stdlib.h>
#include <string.h>

#ifdef __cplusplus
extern "C"
{
#endif  /* __cplusplus */
#include <lsmash.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/channel_layout.h>
#include <libavutil/mem.h>
#ifdef __cplusplus
}
#endif  /* __cplusplus */

#include "utils.h"
#include "libavsmash.h"
#include "qsv.h"
#include "decode.h"

#define BYTE_SWAP_16( x ) ((( x ) << 8 & 0xff00)  | (( x ) >> 8 & 0x00ff))
#define BYTE_SWAP_32( x ) (BYTE_SWAP_16( x ) << 16 | BYTE_SWAP_16(( x ) >> 16))

lsmash_root_t *libavsmash_open_file
(
    AVFormatContext          **p_format_ctx,
    const char                *file_name,
    lsmash_file_parameters_t  *file_param,
    lsmash_movie_parameters_t *movie_param,
    lw_log_handler_t          *lhp
)
{
    /* L-SMASH */
    lsmash_root_t *root = lsmash_create_root();
    if( !root )
        return NULL;
    char error_string[96] = { 0 };
    if( lsmash_open_file( file_name, 1, file_param ) < 0 )
    {
        strcpy( error_string, "Failed to open an input file.\n" );
        goto open_fail;
    }
    lsmash_file_t *fh = lsmash_set_file( root, file_param );
    if( !fh )
    {
        strcpy( error_string, "Failed to add an input file into a ROOT.\n" );
        goto open_fail;
    }
    if( lsmash_read_file( fh, file_param ) < 0 )
    {
        strcpy( error_string, "Failed to read an input file\n" );
        goto open_fail;
    }
    lsmash_initialize_movie_parameters( movie_param );
    lsmash_get_movie_parameters( root, movie_param );
    if( movie_param->number_of_tracks == 0 )
    {
        strcpy( error_string, "The number of tracks equals 0.\n" );
        goto open_fail;
    }
    /* libavformat */
    if( avformat_open_input( p_format_ctx, file_name, NULL, NULL ) )
    {
        strcpy( error_string, "Failed to avformat_open_input.\n" );
        goto open_fail;
    }
    if( avformat_find_stream_info( *p_format_ctx, NULL ) < 0 )
    {
        strcpy( error_string, "Failed to avformat_find_stream_info.\n" );
        goto open_fail;
    }
    return root;
open_fail:
    if( *p_format_ctx )
        avformat_close_input( p_format_ctx );
    lsmash_close_file( file_param );
    lsmash_destroy_root( root );
    lw_log_show( lhp, LW_LOG_FATAL, "%s", error_string );
    return NULL;
}

uint32_t libavsmash_get_track_by_media_type
(
    lsmash_root_t    *root,
    uint32_t          type,
    uint32_t          track_number,
    lw_log_handler_t *lhp
)
{
    char error_string[128] = { 0 };
    const char *media_type_str = type == ISOM_MEDIA_HANDLER_TYPE_VIDEO_TRACK ? "video" : "audio";
    uint32_t track_id;
    lsmash_media_parameters_t media_param;
    if( track_number == 0 )
    {
        /* Get the first track. */
        lsmash_movie_parameters_t movie_param;
        if( lsmash_get_movie_parameters( root, &movie_param ) < 0 )
        {
            strcpy( error_string, "Failed to get movie paramters.\n" );
            goto fail;
        }
        uint32_t i;
        for( i = 1; i <= movie_param.number_of_tracks; i++ )
        {
            track_id = lsmash_get_track_ID( root, i );
            if( track_id == 0 )
            {
                sprintf( error_string, "Failed to find %s track.\n", media_type_str );
                goto fail;
            }
            lsmash_initialize_media_parameters( &media_param );
            if( lsmash_get_media_parameters( root, track_id, &media_param ) < 0 )
            {
                strcpy( error_string, "Failed to get media parameters.\n" );
                goto fail;
            }
            if( media_param.handler_type == type )
                break;
        }
        if( i > movie_param.number_of_tracks )
        {
            sprintf( error_string, "Failed to find the first %s track.\n", media_type_str );
            goto fail;
        }
    }
    else
    {
        /* Get the desired track. */
        track_id = lsmash_get_track_ID( root, track_number );
        if( track_id == 0 )
        {
            sprintf( error_string, "Failed to find %s track %" PRIu32 ".\n", media_type_str, track_number );
            goto fail;
        }
        lsmash_initialize_media_parameters( &media_param );
        if( lsmash_get_media_parameters( root, track_id, &media_param ) < 0 )
        {
            strcpy( error_string, "Failed to get media parameters.\n" );
            goto fail;
        }
        if( media_param.handler_type != type )
        {
            sprintf( error_string, "the track you specified is not %s track.\n", media_type_str );
            goto fail;
        }
    }
    if( lsmash_construct_timeline( root, track_id ) < 0 )
    {
        sprintf( error_string, "Failed to get construct timeline of %s track.\n", media_type_str );
        goto fail;
    }
    return track_id;
fail:
    lw_log_show( lhp, LW_LOG_FATAL, "%s", error_string );
    return 0;
}

int get_summaries
(
    lsmash_root_t         *root,
    uint32_t               track_ID,
    codec_configuration_t *config
)
{
    char error_string[96] = { 0 };
    uint32_t summary_count = lsmash_count_summary( root, track_ID );
    if( summary_count == 0 )
    {
        strcpy( error_string, "Failed to find valid summaries.\n" );
        goto fail;
    }
    libavsmash_summary_t *summaries = (libavsmash_summary_t *)lw_malloc_zero( summary_count * sizeof(libavsmash_summary_t) );
    if( !summaries )
    {
        strcpy( error_string, "Failed to alloc input summaries.\n" );
        goto fail;
    }
    for( uint32_t i = 0; i < summary_count; i++ )
    {
        lsmash_summary_t *summary = lsmash_get_summary( root, track_ID, i + 1 );
        if( !summary )
            continue;
        summaries[i].summary = summary;
    }
    config->entries = summaries;
    config->count   = summary_count;
    return 0;
fail:
    config->error = 1;
    lw_log_show( &config->lh, LW_LOG_FATAL, "%s", error_string );
    return -1;
}

static enum AVCodecID get_codec_id_from_description
(
    lsmash_summary_t *summary
)
{
    lsmash_codec_type_t sample_type = summary->sample_type;
#define ELSE_IF_GET_CODEC_ID_FROM_CODEC_TYPE( codec_id, codec_type )        \
    else if( lsmash_check_codec_type_identical( sample_type, codec_type ) ) \
        return codec_id
    if( summary->summary_type == LSMASH_SUMMARY_TYPE_VIDEO )
    {
        if( lsmash_check_codec_type_identical( sample_type, QT_CODEC_TYPE_RAW_VIDEO ) )
        {
            lsmash_video_summary_t *video = (lsmash_video_summary_t *)summary;
            if( video->depth == QT_VIDEO_DEPTH_24RGB
             || video->depth == QT_VIDEO_DEPTH_32ARGB
             || video->depth == QT_VIDEO_DEPTH_GRAYSCALE_1 )
                return AV_CODEC_ID_RAWVIDEO;
            return AV_CODEC_ID_NONE;
        }
        ELSE_IF_GET_CODEC_ID_FROM_CODEC_TYPE( AV_CODEC_ID_H264,     ISOM_CODEC_TYPE_AVC1_VIDEO );
        ELSE_IF_GET_CODEC_ID_FROM_CODEC_TYPE( AV_CODEC_ID_H264,     ISOM_CODEC_TYPE_AVC3_VIDEO );
        ELSE_IF_GET_CODEC_ID_FROM_CODEC_TYPE( AV_CODEC_ID_HEVC,     ISOM_CODEC_TYPE_HVC1_VIDEO );
        ELSE_IF_GET_CODEC_ID_FROM_CODEC_TYPE( AV_CODEC_ID_HEVC,     ISOM_CODEC_TYPE_HEV1_VIDEO );
        ELSE_IF_GET_CODEC_ID_FROM_CODEC_TYPE( AV_CODEC_ID_VC1,      ISOM_CODEC_TYPE_VC_1_VIDEO );
        ELSE_IF_GET_CODEC_ID_FROM_CODEC_TYPE( AV_CODEC_ID_DIRAC,    ISOM_CODEC_TYPE_DRAC_VIDEO );
        ELSE_IF_GET_CODEC_ID_FROM_CODEC_TYPE( AV_CODEC_ID_PRORES,     QT_CODEC_TYPE_APCH_VIDEO );
        ELSE_IF_GET_CODEC_ID_FROM_CODEC_TYPE( AV_CODEC_ID_PRORES,     QT_CODEC_TYPE_APCN_VIDEO );
        ELSE_IF_GET_CODEC_ID_FROM_CODEC_TYPE( AV_CODEC_ID_PRORES,     QT_CODEC_TYPE_APCS_VIDEO );
        ELSE_IF_GET_CODEC_ID_FROM_CODEC_TYPE( AV_CODEC_ID_PRORES,     QT_CODEC_TYPE_APCO_VIDEO );
        ELSE_IF_GET_CODEC_ID_FROM_CODEC_TYPE( AV_CODEC_ID_PRORES,     QT_CODEC_TYPE_AP4H_VIDEO );
        ELSE_IF_GET_CODEC_ID_FROM_CODEC_TYPE( AV_CODEC_ID_PRORES,     QT_CODEC_TYPE_AP4X_VIDEO );
        ELSE_IF_GET_CODEC_ID_FROM_CODEC_TYPE( AV_CODEC_ID_RAWVIDEO,   QT_CODEC_TYPE_DVOO_VIDEO );
        ELSE_IF_GET_CODEC_ID_FROM_CODEC_TYPE( AV_CODEC_ID_DVVIDEO,    QT_CODEC_TYPE_DVC_VIDEO );
        ELSE_IF_GET_CODEC_ID_FROM_CODEC_TYPE( AV_CODEC_ID_DVVIDEO,    QT_CODEC_TYPE_DVCP_VIDEO );
        ELSE_IF_GET_CODEC_ID_FROM_CODEC_TYPE( AV_CODEC_ID_DVVIDEO,    QT_CODEC_TYPE_DVPP_VIDEO );
        ELSE_IF_GET_CODEC_ID_FROM_CODEC_TYPE( AV_CODEC_ID_DVVIDEO,    QT_CODEC_TYPE_DV5N_VIDEO );
        ELSE_IF_GET_CODEC_ID_FROM_CODEC_TYPE( AV_CODEC_ID_DVVIDEO,    QT_CODEC_TYPE_DV5P_VIDEO );
        ELSE_IF_GET_CODEC_ID_FROM_CODEC_TYPE( AV_CODEC_ID_DVVIDEO,    QT_CODEC_TYPE_DVH2_VIDEO );
        ELSE_IF_GET_CODEC_ID_FROM_CODEC_TYPE( AV_CODEC_ID_DVVIDEO,    QT_CODEC_TYPE_DVH3_VIDEO );
        ELSE_IF_GET_CODEC_ID_FROM_CODEC_TYPE( AV_CODEC_ID_DVVIDEO,    QT_CODEC_TYPE_DVH5_VIDEO );
        ELSE_IF_GET_CODEC_ID_FROM_CODEC_TYPE( AV_CODEC_ID_DVVIDEO,    QT_CODEC_TYPE_DVH6_VIDEO );
        ELSE_IF_GET_CODEC_ID_FROM_CODEC_TYPE( AV_CODEC_ID_DVVIDEO,    QT_CODEC_TYPE_DVHP_VIDEO );
        ELSE_IF_GET_CODEC_ID_FROM_CODEC_TYPE( AV_CODEC_ID_DVVIDEO,    QT_CODEC_TYPE_DVHQ_VIDEO );
        ELSE_IF_GET_CODEC_ID_FROM_CODEC_TYPE( AV_CODEC_ID_FLIC,       QT_CODEC_TYPE_FLIC_VIDEO );
        ELSE_IF_GET_CODEC_ID_FROM_CODEC_TYPE( AV_CODEC_ID_H261,       QT_CODEC_TYPE_H261_VIDEO );
        ELSE_IF_GET_CODEC_ID_FROM_CODEC_TYPE( AV_CODEC_ID_H263,       QT_CODEC_TYPE_H263_VIDEO );
        ELSE_IF_GET_CODEC_ID_FROM_CODEC_TYPE( AV_CODEC_ID_MJPEG,      QT_CODEC_TYPE_JPEG_VIDEO );
        ELSE_IF_GET_CODEC_ID_FROM_CODEC_TYPE( AV_CODEC_ID_MJPEG,      QT_CODEC_TYPE_MJPA_VIDEO );
        ELSE_IF_GET_CODEC_ID_FROM_CODEC_TYPE( AV_CODEC_ID_MJPEGB,     QT_CODEC_TYPE_MJPB_VIDEO );
        ELSE_IF_GET_CODEC_ID_FROM_CODEC_TYPE( AV_CODEC_ID_PNG,        QT_CODEC_TYPE_PNG_VIDEO );
        ELSE_IF_GET_CODEC_ID_FROM_CODEC_TYPE( AV_CODEC_ID_QTRLE,      QT_CODEC_TYPE_RLE_VIDEO );
        ELSE_IF_GET_CODEC_ID_FROM_CODEC_TYPE( AV_CODEC_ID_RPZA,       QT_CODEC_TYPE_RPZA_VIDEO );
        ELSE_IF_GET_CODEC_ID_FROM_CODEC_TYPE( AV_CODEC_ID_TARGA,      QT_CODEC_TYPE_TGA_VIDEO );
        ELSE_IF_GET_CODEC_ID_FROM_CODEC_TYPE( AV_CODEC_ID_TIFF,       QT_CODEC_TYPE_TIFF_VIDEO );
        ELSE_IF_GET_CODEC_ID_FROM_CODEC_TYPE( AV_CODEC_ID_UTVIDEO,    QT_CODEC_TYPE_ULRA_VIDEO );
        ELSE_IF_GET_CODEC_ID_FROM_CODEC_TYPE( AV_CODEC_ID_UTVIDEO,    QT_CODEC_TYPE_ULRG_VIDEO );
        ELSE_IF_GET_CODEC_ID_FROM_CODEC_TYPE( AV_CODEC_ID_UTVIDEO,    QT_CODEC_TYPE_ULY0_VIDEO );
        ELSE_IF_GET_CODEC_ID_FROM_CODEC_TYPE( AV_CODEC_ID_UTVIDEO,    QT_CODEC_TYPE_ULY2_VIDEO );
        ELSE_IF_GET_CODEC_ID_FROM_CODEC_TYPE( AV_CODEC_ID_UTVIDEO,    QT_CODEC_TYPE_ULH0_VIDEO );
        ELSE_IF_GET_CODEC_ID_FROM_CODEC_TYPE( AV_CODEC_ID_UTVIDEO,    QT_CODEC_TYPE_ULH2_VIDEO );
        ELSE_IF_GET_CODEC_ID_FROM_CODEC_TYPE( AV_CODEC_ID_UTVIDEO,    QT_CODEC_TYPE_UQY2_VIDEO );
        ELSE_IF_GET_CODEC_ID_FROM_CODEC_TYPE( AV_CODEC_ID_V210,       QT_CODEC_TYPE_V210_VIDEO );
        ELSE_IF_GET_CODEC_ID_FROM_CODEC_TYPE( AV_CODEC_ID_V410,       QT_CODEC_TYPE_V410_VIDEO );
        ELSE_IF_GET_CODEC_ID_FROM_CODEC_TYPE( AV_CODEC_ID_RAWVIDEO,   QT_CODEC_TYPE_2VUY_VIDEO );
        ELSE_IF_GET_CODEC_ID_FROM_CODEC_TYPE( AV_CODEC_ID_RAWVIDEO,   QT_CODEC_TYPE_YUV2_VIDEO );
    }
    else if( summary->summary_type == LSMASH_SUMMARY_TYPE_AUDIO )
    {
        if( lsmash_check_codec_type_identical( sample_type, ISOM_CODEC_TYPE_MP4A_AUDIO )
         || lsmash_check_codec_type_identical( sample_type,   QT_CODEC_TYPE_MP4A_AUDIO ) )
        {
            uint32_t cs_count = lsmash_count_codec_specific_data( summary );
            lsmash_codec_specific_t *orig_cs = NULL;
            lsmash_codec_specific_t *cs      = NULL;
            for( uint32_t i = 1; i <= cs_count; i++ )
            {
                orig_cs = lsmash_get_codec_specific_data( summary, i );
                if( !orig_cs || orig_cs->type != LSMASH_CODEC_SPECIFIC_DATA_TYPE_MP4SYS_DECODER_CONFIG )
                    continue;
                cs = orig_cs->format == LSMASH_CODEC_SPECIFIC_FORMAT_STRUCTURED
                   ? orig_cs
                   : lsmash_convert_codec_specific_format( orig_cs, LSMASH_CODEC_SPECIFIC_FORMAT_STRUCTURED );
                break;
            }
            if( !cs )
                return AV_CODEC_ID_NONE;
            lsmash_mp4sys_decoder_parameters_t *mdp = (lsmash_mp4sys_decoder_parameters_t *)cs->data.structured;
            enum AVCodecID codec_id;
            switch( mdp->objectTypeIndication )
            {
                case MP4SYS_OBJECT_TYPE_Visual_ISO_14496_2 :
                    codec_id = AV_CODEC_ID_MPEG4;
                    break;
                case MP4SYS_OBJECT_TYPE_Audio_ISO_14496_3 :
                    switch( ((lsmash_audio_summary_t *)summary)->aot )
                    {
                        case MP4A_AUDIO_OBJECT_TYPE_AAC_MAIN :
                        case MP4A_AUDIO_OBJECT_TYPE_AAC_LC :
                        case MP4A_AUDIO_OBJECT_TYPE_AAC_LTP :
                            codec_id = AV_CODEC_ID_AAC;
                            break;
                        case MP4A_AUDIO_OBJECT_TYPE_ALS :
                            codec_id = AV_CODEC_ID_MP4ALS;
                            break;
                        default :
                            codec_id = AV_CODEC_ID_NONE;
                            break;
                    }
                    break;
                case MP4SYS_OBJECT_TYPE_Audio_ISO_13818_7_Main_Profile :
                case MP4SYS_OBJECT_TYPE_Audio_ISO_13818_7_LC_Profile :
                case MP4SYS_OBJECT_TYPE_Audio_ISO_13818_7_SSR_Profile :
                    codec_id = AV_CODEC_ID_AAC;
                    break;
                case MP4SYS_OBJECT_TYPE_Audio_ISO_13818_3 :
                case MP4SYS_OBJECT_TYPE_Audio_ISO_11172_3 :
                    /* Return temporal CODEC ID since we can't confirm what CODEC is used here. */
                    codec_id = AV_CODEC_ID_MP3;
                    break;
                case MP4SYS_OBJECT_TYPE_PNG :
                    codec_id = AV_CODEC_ID_PNG;
                    break;
                default :
                    codec_id = AV_CODEC_ID_NONE;
                    break;
            }
            if( orig_cs != cs )
                lsmash_destroy_codec_specific_data( cs );
            return codec_id;
        }
        ELSE_IF_GET_CODEC_ID_FROM_CODEC_TYPE( AV_CODEC_ID_AC3,         ISOM_CODEC_TYPE_AC_3_AUDIO );
        ELSE_IF_GET_CODEC_ID_FROM_CODEC_TYPE( AV_CODEC_ID_EAC3,        ISOM_CODEC_TYPE_EC_3_AUDIO );
        ELSE_IF_GET_CODEC_ID_FROM_CODEC_TYPE( AV_CODEC_ID_DTS,         ISOM_CODEC_TYPE_DTSC_AUDIO );
        ELSE_IF_GET_CODEC_ID_FROM_CODEC_TYPE( AV_CODEC_ID_DTS,         ISOM_CODEC_TYPE_DTSH_AUDIO );
        ELSE_IF_GET_CODEC_ID_FROM_CODEC_TYPE( AV_CODEC_ID_DTS,         ISOM_CODEC_TYPE_DTSL_AUDIO );
        ELSE_IF_GET_CODEC_ID_FROM_CODEC_TYPE( AV_CODEC_ID_AMR_NB,      ISOM_CODEC_TYPE_SAMR_AUDIO );
        ELSE_IF_GET_CODEC_ID_FROM_CODEC_TYPE( AV_CODEC_ID_AMR_WB,      ISOM_CODEC_TYPE_SAWB_AUDIO );
        ELSE_IF_GET_CODEC_ID_FROM_CODEC_TYPE( AV_CODEC_ID_ALAC,        ISOM_CODEC_TYPE_ALAC_AUDIO );
        ELSE_IF_GET_CODEC_ID_FROM_CODEC_TYPE( AV_CODEC_ID_ALAC,          QT_CODEC_TYPE_ALAC_AUDIO );
        ELSE_IF_GET_CODEC_ID_FROM_CODEC_TYPE( AV_CODEC_ID_MACE3,         QT_CODEC_TYPE_MAC3_AUDIO );
        ELSE_IF_GET_CODEC_ID_FROM_CODEC_TYPE( AV_CODEC_ID_MACE6,         QT_CODEC_TYPE_MAC6_AUDIO );
        ELSE_IF_GET_CODEC_ID_FROM_CODEC_TYPE( AV_CODEC_ID_QCELP,         QT_CODEC_TYPE_QCLP_AUDIO );
        ELSE_IF_GET_CODEC_ID_FROM_CODEC_TYPE( AV_CODEC_ID_GSM,           QT_CODEC_TYPE_AGSM_AUDIO );
        ELSE_IF_GET_CODEC_ID_FROM_CODEC_TYPE( AV_CODEC_ID_PCM_ALAW,      QT_CODEC_TYPE_ALAW_AUDIO );
        ELSE_IF_GET_CODEC_ID_FROM_CODEC_TYPE( AV_CODEC_ID_ADPCM_IMA_QT,  QT_CODEC_TYPE_IMA4_AUDIO );
        ELSE_IF_GET_CODEC_ID_FROM_CODEC_TYPE( AV_CODEC_ID_PCM_MULAW,     QT_CODEC_TYPE_ULAW_AUDIO );
        ELSE_IF_GET_CODEC_ID_FROM_CODEC_TYPE( AV_CODEC_ID_MP3,           QT_CODEC_TYPE_FULLMP3_AUDIO );
        ELSE_IF_GET_CODEC_ID_FROM_CODEC_TYPE( AV_CODEC_ID_ADPCM_MS,      QT_CODEC_TYPE_ADPCM2_AUDIO );
        ELSE_IF_GET_CODEC_ID_FROM_CODEC_TYPE( AV_CODEC_ID_ADPCM_IMA_WAV, QT_CODEC_TYPE_ADPCM17_AUDIO );
        ELSE_IF_GET_CODEC_ID_FROM_CODEC_TYPE( AV_CODEC_ID_GSM_MS,        QT_CODEC_TYPE_GSM49_AUDIO );
        else if( lsmash_check_codec_type_identical( sample_type, QT_CODEC_TYPE_LPCM_AUDIO )
              || lsmash_check_codec_type_identical( sample_type, QT_CODEC_TYPE_FL32_AUDIO )
              || lsmash_check_codec_type_identical( sample_type, QT_CODEC_TYPE_FL64_AUDIO )
              || lsmash_check_codec_type_identical( sample_type, QT_CODEC_TYPE_23NI_AUDIO )
              || lsmash_check_codec_type_identical( sample_type, QT_CODEC_TYPE_IN24_AUDIO )
              || lsmash_check_codec_type_identical( sample_type, QT_CODEC_TYPE_IN32_AUDIO )
              || lsmash_check_codec_type_identical( sample_type, QT_CODEC_TYPE_SOWT_AUDIO )
              || lsmash_check_codec_type_identical( sample_type, QT_CODEC_TYPE_TWOS_AUDIO )
              || lsmash_check_codec_type_identical( sample_type, QT_CODEC_TYPE_RAW_AUDIO )
              || lsmash_check_codec_type_identical( sample_type, QT_CODEC_TYPE_NONE_AUDIO )
              || lsmash_check_codec_type_identical( sample_type, QT_CODEC_TYPE_NOT_SPECIFIED ) )
        {
            uint32_t cs_count = lsmash_count_codec_specific_data( summary );
            lsmash_codec_specific_t *cs = NULL;
            for( uint32_t i = 1; i <= cs_count; i++ )
            {
                cs = lsmash_get_codec_specific_data( summary, i );
                if( cs
                 && cs->type == LSMASH_CODEC_SPECIFIC_DATA_TYPE_QT_AUDIO_FORMAT_SPECIFIC_FLAGS
                 && cs->format == LSMASH_CODEC_SPECIFIC_FORMAT_STRUCTURED )
                    break;
            }
            if( !cs )
                return AV_CODEC_ID_NONE;
            lsmash_audio_summary_t *audio = (lsmash_audio_summary_t *)summary;
            lsmash_qt_audio_format_specific_flags_t *data = (lsmash_qt_audio_format_specific_flags_t *)cs->data.structured;
            int is_int    = !(data->format_flags & QT_LPCM_FORMAT_FLAG_FLOAT);
            int is_signed = !!(data->format_flags & QT_LPCM_FORMAT_FLAG_SIGNED_INTEGER);
            int is_be     = !!(data->format_flags & QT_LPCM_FORMAT_FLAG_BIG_ENDIAN);
            switch( audio->sample_size )
            {
                case  8 :
                    return is_signed ? AV_CODEC_ID_PCM_S8 : AV_CODEC_ID_PCM_U8;
                case 16 :
                    if( is_signed )
                        return is_be ? AV_CODEC_ID_PCM_S16BE : AV_CODEC_ID_PCM_S16LE;
                    else
                        return is_be ? AV_CODEC_ID_PCM_U16BE : AV_CODEC_ID_PCM_U16LE;
                case 24 :
                    if( is_signed )
                        return is_be ? AV_CODEC_ID_PCM_S24BE : AV_CODEC_ID_PCM_S24LE;
                    else
                        return is_be ? AV_CODEC_ID_PCM_U24BE : AV_CODEC_ID_PCM_U24LE;
                case 32 :
                    if( is_int )
                    {
                        if( is_signed )
                            return is_be ? AV_CODEC_ID_PCM_S32BE : AV_CODEC_ID_PCM_S32LE;
                        else
                            return is_be ? AV_CODEC_ID_PCM_U32BE : AV_CODEC_ID_PCM_U32LE;
                    }
                    else
                        return is_be ? AV_CODEC_ID_PCM_F32BE : AV_CODEC_ID_PCM_F32LE;
                case 64 :
                    if( is_int )
                        return AV_CODEC_ID_NONE;
                    else
                        return is_be ? AV_CODEC_ID_PCM_F64BE : AV_CODEC_ID_PCM_F64LE;
                default :
                    return AV_CODEC_ID_NONE;
            }
        }
    }
    return AV_CODEC_ID_NONE;
#undef ELSE_IF_GET_CODEC_ID_FROM_CODEC_TYPE
}

static const AVCodec *libavsmash_find_decoder
(
    codec_configuration_t *config,
    enum AVCodecID         codec_id
)
{
    if( codec_id == AV_CODEC_ID_NONE )
        /* Try to get any valid codec_id from summaries. */
        for( uint32_t i = 0; i < config->count && codec_id == AV_CODEC_ID_NONE; i++ )
            codec_id = get_codec_id_from_description( config->entries[i].summary );
    return find_decoder( codec_id, NULL, config->preferred_decoder_names, config->prefer_hw_decoder );
}

int libavsmash_find_and_open_decoder
(
    codec_configuration_t   *config,
    const AVCodecParameters *codecpar,
    const int                thread_count
)
{
    const AVCodec *codec = libavsmash_find_decoder( config, codecpar->codec_id );
    if( !codec )
        return -1;
    return open_decoder( &config->ctx, codecpar, codec, thread_count );
}

static lsmash_codec_specific_data_type get_codec_specific_data_type
(
    lsmash_codec_type_t           codec_type,
    lsmash_codec_specific_format *format1,
    lsmash_codec_specific_format *format2
)
{
    *format1 = LSMASH_CODEC_SPECIFIC_FORMAT_UNSPECIFIED;
    *format2 = LSMASH_CODEC_SPECIFIC_FORMAT_UNSPECIFIED;
    if( lsmash_check_codec_type_identical( codec_type, ISOM_CODEC_TYPE_AVC1_VIDEO )
     || lsmash_check_codec_type_identical( codec_type, ISOM_CODEC_TYPE_AVC3_VIDEO ) )
    {
        *format1 = LSMASH_CODEC_SPECIFIC_FORMAT_UNSTRUCTURED;
        return LSMASH_CODEC_SPECIFIC_DATA_TYPE_ISOM_VIDEO_H264;
    }
    else if( lsmash_check_codec_type_identical( codec_type, ISOM_CODEC_TYPE_HVC1_VIDEO )
          || lsmash_check_codec_type_identical( codec_type, ISOM_CODEC_TYPE_HEV1_VIDEO ) )
    {
        *format1 = LSMASH_CODEC_SPECIFIC_FORMAT_UNSTRUCTURED;
        return LSMASH_CODEC_SPECIFIC_DATA_TYPE_ISOM_VIDEO_HEVC;
    }
    else if( lsmash_check_codec_type_identical( codec_type, ISOM_CODEC_TYPE_MP4A_AUDIO )
          || lsmash_check_codec_type_identical( codec_type,   QT_CODEC_TYPE_MP4A_AUDIO )
          || lsmash_check_codec_type_identical( codec_type, ISOM_CODEC_TYPE_MP4V_VIDEO ) )
    {
        *format1 = LSMASH_CODEC_SPECIFIC_FORMAT_STRUCTURED;
        return LSMASH_CODEC_SPECIFIC_DATA_TYPE_MP4SYS_DECODER_CONFIG;
    }
    else if( lsmash_check_codec_type_identical( codec_type, ISOM_CODEC_TYPE_VC_1_VIDEO ) )
    {
        *format1 = LSMASH_CODEC_SPECIFIC_FORMAT_UNSTRUCTURED;
        return LSMASH_CODEC_SPECIFIC_DATA_TYPE_ISOM_VIDEO_VC_1;
    }
    else if( lsmash_check_codec_type_identical( codec_type, ISOM_CODEC_TYPE_AC_3_AUDIO ) )
    {
        *format2 = LSMASH_CODEC_SPECIFIC_FORMAT_STRUCTURED;
        return LSMASH_CODEC_SPECIFIC_DATA_TYPE_ISOM_AUDIO_AC_3;
    }
    else if( lsmash_check_codec_type_identical( codec_type, ISOM_CODEC_TYPE_ALAC_AUDIO )
          || lsmash_check_codec_type_identical( codec_type,   QT_CODEC_TYPE_ALAC_AUDIO ) )
    {
        *format1 = LSMASH_CODEC_SPECIFIC_FORMAT_UNSTRUCTURED;
        *format2 = LSMASH_CODEC_SPECIFIC_FORMAT_STRUCTURED;
        return LSMASH_CODEC_SPECIFIC_DATA_TYPE_ISOM_AUDIO_ALAC;
    }
    else if( lsmash_check_codec_type_identical( codec_type, ISOM_CODEC_TYPE_DTSC_AUDIO )
          || lsmash_check_codec_type_identical( codec_type, ISOM_CODEC_TYPE_DTSH_AUDIO )
          || lsmash_check_codec_type_identical( codec_type, ISOM_CODEC_TYPE_DTSL_AUDIO ) )
    {
        *format2 = LSMASH_CODEC_SPECIFIC_FORMAT_STRUCTURED;
        return LSMASH_CODEC_SPECIFIC_DATA_TYPE_ISOM_AUDIO_DTS;
    }
    else if( lsmash_check_codec_type_identical( codec_type, QT_CODEC_TYPE_ULRA_VIDEO )
          || lsmash_check_codec_type_identical( codec_type, QT_CODEC_TYPE_ULRG_VIDEO )
          || lsmash_check_codec_type_identical( codec_type, QT_CODEC_TYPE_ULY0_VIDEO )
          || lsmash_check_codec_type_identical( codec_type, QT_CODEC_TYPE_ULY2_VIDEO )
          || lsmash_check_codec_type_identical( codec_type, QT_CODEC_TYPE_ULH0_VIDEO )
          || lsmash_check_codec_type_identical( codec_type, QT_CODEC_TYPE_ULH2_VIDEO )
          || lsmash_check_codec_type_identical( codec_type, QT_CODEC_TYPE_UQY2_VIDEO ) )
    {
        *format1 = LSMASH_CODEC_SPECIFIC_FORMAT_STRUCTURED;
        return LSMASH_CODEC_SPECIFIC_DATA_TYPE_CODEC_GLOBAL_HEADER;
    }
    return LSMASH_CODEC_SPECIFIC_DATA_TYPE_UNSPECIFIED;
}

static int queue_extradata
(
    codec_configuration_t *config,
    uint8_t               *extradata,
    int                    extradata_size
)
{
    if( extradata && extradata_size > 0 )
    {
        uint8_t *temp = (uint8_t *)av_mallocz( extradata_size + AV_INPUT_BUFFER_PADDING_SIZE );
        if( !temp )
        {
            config->error = 1;
            if( config->lh.show_log )
                config->lh.show_log( &config->lh, LW_LOG_FATAL,
                                     "Failed to allocate memory for new extradata.\n"
                                     "It is recommended you reopen the file." );
            return -1;
        }
        memcpy( temp, extradata, extradata_size );
        config->queue.extradata      = temp;
        config->queue.extradata_size = extradata_size;
    }
    else
    {
        config->queue.extradata      = NULL;
        config->queue.extradata_size = 0;
    }
    return 0;
}

static int prepare_new_decoder_configuration
(
    codec_configuration_t *config,
    uint32_t               new_index
)
{
    if( new_index == 0 )
        new_index = 1;
    lsmash_summary_t *summary = new_index <= config->count ? config->entries[new_index - 1].summary : NULL;
    enum AVCodecID new_codec_id = summary ? get_codec_id_from_description( summary ) : AV_CODEC_ID_NONE;
    config->queue.codec_id    = new_codec_id;
    config->queue.delay_count = config->delay_count;
    if( new_codec_id == AV_CODEC_ID_NONE )
    {
        /* Don't update the decoder configuration if L-SMASH cannot recognize CODEC or extract its specific info correctly. */
        config->queue.index = new_index;
        return 0;
    }
    config->queue.bits_per_sample = av_get_bits_per_sample( new_codec_id );
    lsmash_codec_specific_format    cs_format1;
    lsmash_codec_specific_format    cs_format2;
    lsmash_codec_specific_data_type cs_type = get_codec_specific_data_type( summary->sample_type, &cs_format1, &cs_format2 );
    if( cs_type == LSMASH_CODEC_SPECIFIC_DATA_TYPE_UNSPECIFIED
     && cs_format1 == LSMASH_CODEC_SPECIFIC_FORMAT_UNSPECIFIED
     && cs_format2 == LSMASH_CODEC_SPECIFIC_FORMAT_UNSPECIFIED )
    {
        if( new_codec_id == AV_CODEC_ID_AMR_NB )
        {
            config->queue.sample_rate = 8000;
            config->queue.channels    = 1;
        }
        else if( new_codec_id == AV_CODEC_ID_AMR_WB )
        {
            config->queue.sample_rate = 16000;
            config->queue.channels    = 1;
        }
        queue_extradata( config, NULL, 0 );
        config->queue.index = new_index;
        return 0;
    }
    uint32_t cs_count = lsmash_count_codec_specific_data( summary );
    lsmash_codec_specific_t *orig_cs = NULL;
    lsmash_codec_specific_t *cs1     = NULL;
    lsmash_codec_specific_t *cs2     = NULL;
    for( uint32_t i = 1; i <= cs_count; i++ )
    {
        orig_cs = lsmash_get_codec_specific_data( summary, i );
        if( !orig_cs )
            continue;
        if( orig_cs->type == cs_type )
        {
            cs1 = orig_cs->format == cs_format1 ? orig_cs : lsmash_convert_codec_specific_format( orig_cs, cs_format1 );
            cs2 = orig_cs->format == cs_format2 ? orig_cs : lsmash_convert_codec_specific_format( orig_cs, cs_format2 );
            break;
        }
    }
    if( (cs_format1 != LSMASH_CODEC_SPECIFIC_FORMAT_UNSPECIFIED && !cs1)
     || (cs_format2 != LSMASH_CODEC_SPECIFIC_FORMAT_UNSPECIFIED && !cs2) )
        return -1;
    if( cs_format1 == LSMASH_CODEC_SPECIFIC_FORMAT_UNSTRUCTURED )
    {
        if( cs_type == LSMASH_CODEC_SPECIFIC_DATA_TYPE_ISOM_AUDIO_ALAC )
        {
            lsmash_alac_specific_parameters_t *alac = (lsmash_alac_specific_parameters_t *)cs2->data.structured;
            config->queue.bits_per_sample = alac->bitDepth;
            config->queue.channels        = alac->numChannels;
            config->queue.sample_rate     = alac->sampleRate;
        }
        int offset = cs_type == LSMASH_CODEC_SPECIFIC_DATA_TYPE_ISOM_VIDEO_H264 ? 8
                   : cs_type == LSMASH_CODEC_SPECIFIC_DATA_TYPE_ISOM_VIDEO_HEVC ? 8
                   : cs_type == LSMASH_CODEC_SPECIFIC_DATA_TYPE_ISOM_VIDEO_VC_1 ? 15
                   : 0;
        if( queue_extradata( config, cs1->data.unstructured + offset, cs1->size - offset ) )
            goto fail;
    }
    else if( cs_format1 == LSMASH_CODEC_SPECIFIC_FORMAT_STRUCTURED )
    {
        if( cs_type == LSMASH_CODEC_SPECIFIC_DATA_TYPE_MP4SYS_DECODER_CONFIG )
        {
            if( new_codec_id == AV_CODEC_ID_MP3 )
            {
                /* Confirm MPEG-1/2 Audio by libavcodec's parser. */
                AVCodecParserContext *parser = av_parser_init( new_codec_id );
                if( !parser )
                    goto fail;
                uint8_t *dummy_out;
                int      dummy_out_size;
                av_parser_parse2( parser, config->ctx, &dummy_out, &dummy_out_size,
                                  config->queue.packet.data, config->queue.packet.size,
                                  AV_NOPTS_VALUE, AV_NOPTS_VALUE, -1 );
                av_parser_close( parser );
                config->queue.codec_id = config->ctx->codec_id;
            }
            else
            {
                lsmash_mp4sys_decoder_parameters_t *mdp = (lsmash_mp4sys_decoder_parameters_t *)cs1->data.structured;
                uint8_t *cs_data;
                uint32_t cs_data_size;
                if( lsmash_get_mp4sys_decoder_specific_info( mdp, &cs_data, &cs_data_size ) )
                    goto fail;
                int ret = queue_extradata( config, cs_data, cs_data_size );
                lsmash_free( cs_data );
                if( ret )
                    goto fail;
            }
        }
        else    /* cs_type == LSMASH_CODEC_SPECIFIC_DATA_TYPE_CODEC_GLOBAL_HEADER */
        {
            lsmash_codec_global_header_t *gh = (lsmash_codec_global_header_t *)cs1->data.structured;
            if( !gh )
                goto fail;
            if( queue_extradata( config, gh->header_data, gh->header_size ) )
                goto fail;
        }
    }
    else
    {
        /* For enhanced AC-3, we don't assume whether libavcodec's enhanced AC-3 decoder does support
         * additional independent substreams and its associated dependent substreams or not.
         * For DTS audio, we don't assume whether libavcodec's DTS decoder does support X96, XXCH, LBR and XLL extensions or not.
         * Therefore for the above-mentioned audio formats, setup of 'sample_rate' and 'channels' is entrusted to actual decoding. */
        if( cs_type == LSMASH_CODEC_SPECIFIC_DATA_TYPE_ISOM_AUDIO_AC_3 )
        {
            lsmash_ac3_specific_parameters_t *ac3 = (lsmash_ac3_specific_parameters_t *)cs2->data.structured;
            if( ac3->acmod > 7 || ac3->fscod > 3 )
                goto fail;
            int channels   [] = { 2, 1, 2, 3, 3, 4, 4, 5 };
            int sample_rate[] = { 48000, 44100, 32000, 0 };
            config->queue.bits_per_sample = 0;
            config->queue.channels        = channels[ ac3->acmod ] + ac3->lfeon;
            config->queue.sample_rate     = sample_rate[ ac3->fscod ];
        }
        else if( cs_type == LSMASH_CODEC_SPECIFIC_DATA_TYPE_ISOM_AUDIO_DTS )
        {
            /* Here, assume that libavcodec's DTS decoder doesn't support X96, XXCH, LBR and XLL extensions. */
            lsmash_dts_specific_parameters_t *dts = (lsmash_dts_specific_parameters_t *)cs2->data.structured;
            config->queue.bits_per_sample = dts->pcmSampleDepth;
        }
    }
    config->queue.index = new_index;
    if( orig_cs != cs1 )
        lsmash_destroy_codec_specific_data( cs1 );
    if( orig_cs != cs2 )
        lsmash_destroy_codec_specific_data( cs2 );
    return 0;
fail:
    if( orig_cs != cs1 )
        lsmash_destroy_codec_specific_data( cs1 );
    if( orig_cs != cs2 )
        lsmash_destroy_codec_specific_data( cs2 );
    return -1;
}

int get_sample
(
    lsmash_root_t         *root,
    uint32_t               track_ID,
    uint32_t               sample_number,
    codec_configuration_t *config,
    AVPacket              *pkt
)
{
    if( !config->update_pending && config->dequeue_packet )
    {
        /* Dequeue the queued packet after the corresponding decoder configuration is activated. */
        config->dequeue_packet = 0;
        if( sample_number == config->queue.sample_number )
        {
            *pkt = config->queue.packet;
            return 0;
        }
    }
    av_packet_unref( pkt );
    if( config->update_pending || config->queue.delay_count )
    {
        /* Return NULL packet to flush data from the decoder until corresponding decoder configuration is activated. */
        pkt->data = NULL;
        pkt->size = 0;
        if( config->queue.delay_count && (-- config->queue.delay_count == 0) )
        {
            config->update_pending = 1;
            config->dequeue_packet = 1;
        }
        return 0;
    }
    lsmash_sample_t *sample = lsmash_get_sample_from_media_timeline( root, track_ID, sample_number );
    if( !sample )
    {
        /* Reached the end of this media timeline. */
        pkt->data = NULL;
        pkt->size = 0;
        return 1;
    }
    pkt->flags = sample->prop.ra_flags;     /* Set proper flags when feeding this packet into the decoder. */
    pkt->size  = sample->length;
    pkt->data  = config->input_buffer;
    pkt->pts   = sample->cts;               /* Set composition timestamp to presentation timestamp field. */
    pkt->dts   = sample->dts;
    /* Copy sample data from L-SMASH.
     * Set 0 to the end of the additional AV_INPUT_BUFFER_PADDING_SIZE bytes.
     * Without this, some decoders could cause wrong results. */
    memcpy( pkt->data, sample->data, sample->length );
    memset( pkt->data + sample->length, 0, AV_INPUT_BUFFER_PADDING_SIZE );
    /* TODO: add handling invalid indexes. */
    if( sample->index != config->index )
    {
        if( prepare_new_decoder_configuration( config, sample->index ) )
        {
            lsmash_delete_sample( sample );
            return -1;
        }
        /* Queue the current packet and, instead of this, return NULL packet.
         * The current packet will be dequeued and returned after the corresponding decoder configuration is activated. */
        config->queue.sample_number = sample_number;
        config->queue.packet        = *pkt;
        pkt->data = NULL;
        pkt->size = 0;
        if( config->queue.delay_count == 0 )
        {
            /* This NULL packet must not be sent to the decoder. */
            config->update_pending = 1;
            config->dequeue_packet = 1;
            lsmash_delete_sample( sample );
            return 2;
        }
        else
            config->dequeue_packet = 0;
    }
    lsmash_delete_sample( sample );
    return 0;
}

/* Close and open the new decoder to flush buffers in the decoder even if the decoder implements avcodec_flush_buffers().
 * It seems this brings about more stable composition when seeking.
 * Note that this function could reallocate AVCodecContext. */
void libavsmash_flush_buffers
(
    codec_configuration_t *config
)
{
    AVCodecContext    *ctx          = NULL;
    const AVCodec     *codec        = config->ctx->codec;
    void              *app_specific = config->ctx->opaque;
    AVCodecParameters *codecpar     = avcodec_parameters_alloc();
    if( !codecpar
     || avcodec_parameters_from_context( codecpar, config->ctx ) < 0
     || open_decoder( &ctx, codecpar, codec, config->ctx->thread_count ) < 0 )
    {
        avcodec_flush_buffers( config->ctx );
        config->error = 1;
        if( config->lh.show_log )
            config->lh.show_log( &config->lh, LW_LOG_FATAL,
                                 "Failed to flush buffers by a reliable way.\n"
                                 "It is recommended you reopen the file." );
    }
    else
    {
        config->ctx->opaque = NULL;
        avcodec_free_context( &config->ctx );
        config->ctx = ctx;
        config->ctx->opaque = app_specific;
    }
    avcodec_parameters_free( &codecpar );
    config->update_pending    = 0;
    config->delay_count       = 0;
    config->queue.delay_count = 0;
    config->queue.index       = config->index;
}

void update_configuration
(
    lsmash_root_t         *root,
    uint32_t               track_ID,
    codec_configuration_t *config
)
{
    uint32_t new_index = config->queue.index ? config->queue.index : 1;
    if( !config->update_pending || config->queue.codec_id == AV_CODEC_ID_NONE )
    {
        /* Don't update the decoder configuration if L-SMASH cannot recognize CODEC or extract its specific info correctly. */
        config->index = new_index;
        libavsmash_flush_buffers( config );
        /* Set up the maximum presentation width and height. */
        libavsmash_summary_t *entry   = config->index <= config->count ? &config->entries[ config->index - 1 ] : NULL;
        lsmash_summary_t     *summary = entry ? entry->summary : NULL;
        if( summary && summary->summary_type == LSMASH_SUMMARY_TYPE_VIDEO )
        {
            lsmash_video_summary_t *video = (lsmash_video_summary_t *)summary;
            entry->extended.width  = video->width;
            entry->extended.height = video->height;
        }
        return;
    }
    char               error_string[96]  = { 0 };
    void              *app_specific      = config->ctx->opaque;
    const int          thread_count      = config->ctx->thread_count;
    AVCodecParameters *codecpar          = avcodec_parameters_alloc();
    if( !codecpar || avcodec_parameters_from_context( codecpar, config->ctx ) < 0 )
    {
        strcpy( error_string, "Failed to get the CODEC parameters.\n" );
        goto fail;
    }
    /* Close the decoder here. */
    config->ctx->opaque = NULL;
    avcodec_free_context( &config->ctx );
    /* Find an appropriate decoder. */
    const AVCodec *codec = find_decoder( config->queue.codec_id, codecpar, config->preferred_decoder_names, config->prefer_hw_decoder );
    if( !codec )
    {
        strcpy( error_string, "Failed to find the decoder.\n" );
        goto fail;
    }
    /* Set up decoder basic settings. */
    lsmash_summary_t *summary = config->entries[new_index - 1].summary;
    if( codec->type == AVMEDIA_TYPE_VIDEO )
    {
        lsmash_video_summary_t *video = (lsmash_video_summary_t *)summary;
        codecpar->width  = video->width;
        codecpar->height = video->height;
        /* Here, expect appropriate pixel format will be picked in avcodec_open2(). */
        if( video->depth >= QT_VIDEO_DEPTH_GRAYSCALE_1 && video->depth <= QT_VIDEO_DEPTH_GRAYSCALE_8 )
            config->queue.bits_per_sample = video->depth & 0x1f;
        else
            config->queue.bits_per_sample = video->depth;
        if( config->queue.bits_per_sample > 0 )
            codecpar->bits_per_coded_sample = config->queue.bits_per_sample;
    }
    else
    {
        if( codec->id != AV_CODEC_ID_AAC && codec->id != AV_CODEC_ID_DTS && codec->id != AV_CODEC_ID_EAC3 )
        {
            lsmash_audio_summary_t *audio = (lsmash_audio_summary_t *)summary;
            codecpar->sample_rate           = config->queue.sample_rate     ? config->queue.sample_rate     : audio->frequency;
            codecpar->bits_per_coded_sample = config->queue.bits_per_sample ? config->queue.bits_per_sample : audio->sample_size;
            codecpar->channels              = config->queue.channels        ? config->queue.channels        : audio->channels;
        }
        if( codec->id == AV_CODEC_ID_DTS )
        {
            codecpar->bits_per_coded_sample = config->queue.bits_per_sample;
            codecpar->format                = (int)(config->queue.bits_per_sample == 16 ? AV_SAMPLE_FMT_S16 : AV_SAMPLE_FMT_FLT);
        }
    }
    /* This is needed by some CODECs such as UtVideo and raw video. */
    codecpar->codec_tag = BYTE_SWAP_32( summary->sample_type.fourcc );
    /* Update extradata. */
    codecpar->extradata      = config->queue.extradata;
    codecpar->extradata_size = config->queue.extradata_size;
    config->queue.extradata      = NULL;
    config->queue.extradata_size = 0;
    /* Open an appropriate decoder.
     * Here, we force single threaded decoding since some decoder doesn't do its proper initialization with multi-threaded decoding. */
    AVCodecContext *ctx = NULL;
    if( open_decoder( &ctx, codecpar, codec, 1 ) < 0 )
    {
        strcpy( error_string, "Failed to open decoder.\n" );
        goto fail;
    }
    avcodec_parameters_free( &codecpar );
    config->ctx               = ctx;
    config->index             = new_index;
    config->update_pending    = 0;
    config->delay_count       = 0;
    config->queue.delay_count = 0;
    /* Set up decoder basic settings by actual decoding. */
    AVFrame *picture = av_frame_alloc();
    if( !picture )
    {
        strcpy( error_string, "Failed to alloc AVFrame to set up a decoder configuration.\n" );
        goto fail;
    }
    uint32_t current_sample_number = config->queue.sample_number;
    extended_summary_t *extended = &config->entries[ config->index - 1 ].extended;
    if( ctx->codec_type == AVMEDIA_TYPE_VIDEO )
    {
        /* Set the maximum width and height in this sequence. */
        extended->width  = ctx->width;
        extended->height = ctx->height;
        /* Actual decoding */
        uint32_t i = current_sample_number;
        do
        {
            AVPacket pkt = { 0 };
            int ret = get_sample( root, track_ID, i++, config, &pkt );
            if( ret > 0 || config->index != config->queue.index )
                break;
            else if( ret < 0 )
            {
                if( ctx->pix_fmt == AV_PIX_FMT_NONE )
                    strcpy( error_string, "Failed to set up pixel format.\n" );
                else
                    strcpy( error_string, "Failed to set up resolution.\n" );
                av_frame_free( &picture );
                goto fail;
            }
            int dummy;
            decode_video_packet( ctx, picture, &dummy, &pkt );
        } while( ctx->width == 0 || ctx->height == 0 || ctx->pix_fmt == AV_PIX_FMT_NONE );
    }
    else
    {
        uint32_t i = current_sample_number;
        do
        {
            AVPacket pkt = { 0 };
            int ret = get_sample( root, track_ID, i++, config, &pkt );
            if( ret > 0 || config->index != config->queue.index )
                break;
            else if( ret < 0 )
            {
                if( ctx->sample_rate == 0 )
                    strcpy( error_string, "Failed to set up sample rate.\n" );
                else if( ctx->channel_layout == 0 && ctx->channels == 0 )
                    strcpy( error_string, "Failed to set up channels.\n" );
                else
                    strcpy( error_string, "Failed to set up sample format.\n" );
                av_frame_free( &picture );
                goto fail;
            }
            int dummy;
            decode_audio_packet( ctx, picture, &dummy, &pkt );
        } while( ctx->sample_rate == 0 || (ctx->channel_layout == 0 && ctx->channels == 0) || ctx->sample_fmt == AV_SAMPLE_FMT_NONE );
        extended->channel_layout = ctx->channel_layout ? ctx->channel_layout : av_get_default_channel_layout( ctx->channels );
        extended->sample_rate    = ctx->sample_rate;
        extended->sample_format  = ctx->sample_fmt;
        extended->frame_length   = ctx->frame_size;
    }
    av_frame_free( &picture );
    /* Reopen/flush with the requested number of threads. */
    ctx->thread_count = thread_count;
    libavsmash_flush_buffers( config ); /* Note that config->ctx could change here. */
    ctx = config->ctx;
    if( current_sample_number == config->queue.sample_number )
        config->dequeue_packet = 1;
    ctx->get_buffer2 = config->get_buffer;
    ctx->opaque      = app_specific;
    if( ctx->codec_type == AVMEDIA_TYPE_VIDEO )
    {
        /* avcodec_open2() may have changed resolution unexpectedly. */
        ctx->width  = extended->width;
        ctx->height = extended->height;
    }
    return;
fail:
    avcodec_parameters_free( &codecpar );
    config->update_pending    = 0;
    config->delay_count       = 0;
    config->queue.delay_count = 0;
    config->error             = 1;
    lw_log_show( &config->lh, LW_LOG_FATAL, "%sIt is recommended you reopen the file.", error_string );
}

int initialize_decoder_configuration
(
    lsmash_root_t         *root,
    uint32_t               track_ID,
    codec_configuration_t *config
)
{
    /* Note: the input buffer for libavcodec's decoders must be AV_INPUT_BUFFER_PADDING_SIZE larger than the actual read bytes. */
    uint32_t input_buffer_size = lsmash_get_max_sample_size_in_media_timeline( root, track_ID );
    if( input_buffer_size == 0 )
        return -1;
    config->input_buffer = (uint8_t *)av_mallocz( input_buffer_size + AV_INPUT_BUFFER_PADDING_SIZE );
    if( !config->input_buffer )
        return -1;
    config->get_buffer = avcodec_default_get_buffer2;
    /* Initialize decoder configuration at the first valid sample. */
    AVPacket dummy = { 0 };
    for( uint32_t i = 1; get_sample( root, track_ID, i, config, &dummy ) < 0; i++ );
    update_configuration( root, track_ID, config );
    /* Decide preferred settings. */
    config->prefer.width           = config->ctx->width;
    config->prefer.height          = config->ctx->height;
    config->prefer.sample_rate     = config->ctx->sample_rate;
    config->prefer.sample_format   = config->ctx->sample_fmt;
    config->prefer.bits_per_sample = config->ctx->bits_per_raw_sample   > 0 ? config->ctx->bits_per_raw_sample
                                   : config->ctx->bits_per_coded_sample > 0 ? config->ctx->bits_per_coded_sample
                                   : av_get_bytes_per_sample( config->ctx->sample_fmt ) << 3;
    config->prefer.channel_layout  = config->ctx->channel_layout
                                   ? config->ctx->channel_layout
                                   : av_get_default_channel_layout( config->ctx->channels );
    if( config->count <= 1 )
        return config->error ? -1 : 0;
    /* Investigate other decoder configurations and pick preferred settings from them. */
    uint8_t *index_list = (uint8_t *)lw_malloc_zero( config->count );
    if( !index_list )
    {
        config->error = 1;
        return -1;
    }
    uint32_t valid_index_count = (config->index && config->index <= config->count);
    if( valid_index_count )
        index_list[ config->index - 1 ] = 1;
    uint32_t sample_count = lsmash_get_sample_count_in_media_timeline( root, track_ID );
    for( uint32_t i = 2; i <= sample_count && valid_index_count < config->count; i++ )
    {
        lsmash_sample_t sample;
        if( lsmash_get_sample_info_from_media_timeline( root, track_ID, i, &sample ) )
            continue;
        if( sample.index == config->index || sample.index == 0 )
            continue;
        if( sample.index <= config->count && !index_list[ sample.index - 1 ] )
        {
            for( uint32_t j = i; get_sample( root, track_ID, j, config, &dummy ) < 0; j++ );
            update_configuration( root, track_ID, config );
            index_list[ sample.index - 1 ] = 1;
            if( config->ctx->width > config->prefer.width )
                config->prefer.width = config->ctx->width;
            if( config->ctx->height > config->prefer.height )
                config->prefer.height = config->ctx->height;
            if( av_get_channel_layout_nb_channels( config->ctx->channel_layout )
              > av_get_channel_layout_nb_channels( config->prefer.channel_layout ) )
                config->prefer.channel_layout = config->ctx->channel_layout;
            if( config->ctx->sample_rate > config->prefer.sample_rate )
                config->prefer.sample_rate = config->ctx->sample_rate;
            switch( config->prefer.sample_format )
            {
                case AV_SAMPLE_FMT_NONE :
                    if( config->ctx->sample_fmt != AV_SAMPLE_FMT_NONE )
                        config->prefer.sample_format = config->ctx->sample_fmt;
                    break;
                case AV_SAMPLE_FMT_U8 :
                case AV_SAMPLE_FMT_U8P :
                    if( config->ctx->sample_fmt != AV_SAMPLE_FMT_U8 && config->ctx->sample_fmt != AV_SAMPLE_FMT_U8P )
                        config->prefer.sample_format = config->ctx->sample_fmt;
                    break;
                case AV_SAMPLE_FMT_S16 :
                case AV_SAMPLE_FMT_S16P :
                    if( config->ctx->sample_fmt != AV_SAMPLE_FMT_U8  && config->ctx->sample_fmt != AV_SAMPLE_FMT_U8P
                     && config->ctx->sample_fmt != AV_SAMPLE_FMT_S16 && config->ctx->sample_fmt != AV_SAMPLE_FMT_S16P )
                        config->prefer.sample_format = config->ctx->sample_fmt;
                    break;
                case AV_SAMPLE_FMT_S32 :
                case AV_SAMPLE_FMT_S32P :
                    if( config->ctx->sample_fmt != AV_SAMPLE_FMT_U8  && config->ctx->sample_fmt != AV_SAMPLE_FMT_U8P
                     && config->ctx->sample_fmt != AV_SAMPLE_FMT_S16 && config->ctx->sample_fmt != AV_SAMPLE_FMT_S16P
                     && config->ctx->sample_fmt != AV_SAMPLE_FMT_S32 && config->ctx->sample_fmt != AV_SAMPLE_FMT_S32P )
                        config->prefer.sample_format = config->ctx->sample_fmt;
                    break;
                case AV_SAMPLE_FMT_FLT :
                case AV_SAMPLE_FMT_FLTP :
                    if( config->ctx->sample_fmt == AV_SAMPLE_FMT_DBL || config->ctx->sample_fmt == AV_SAMPLE_FMT_DBLP )
                        config->prefer.sample_format = config->ctx->sample_fmt;
                    break;
                default :
                    break;
            }
            int bits_per_sample = config->ctx->bits_per_raw_sample   > 0 ? config->ctx->bits_per_raw_sample
                                : config->ctx->bits_per_coded_sample > 0 ? config->ctx->bits_per_coded_sample
                                : av_get_bytes_per_sample( config->ctx->sample_fmt ) << 3;
            if( bits_per_sample > config->prefer.bits_per_sample )
                config->prefer.bits_per_sample = bits_per_sample;
            ++valid_index_count;
        }
    }
    lw_free( index_list );
    /* Reinitialize decoder configuration at the first valid sample. */
    for( uint32_t i = 1; get_sample( root, track_ID, i, config, &dummy ) < 0; i++ );
    update_configuration( root, track_ID, config );
    return config->error ? -1 : 0;
}

void cleanup_configuration
(
    codec_configuration_t *config
)
{
    if( config->entries )
    {
        for( uint32_t i = 0; i < config->count; i++ )
            lsmash_cleanup_summary( config->entries[i].summary );
        free( config->entries );
    }
    av_freep( &config->queue.extradata );
    av_freep( &config->input_buffer );
    avcodec_free_context( &config->ctx );
}
