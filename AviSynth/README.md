#### File

    LSMASHSource.dll    : A source plugin for AviSynth+

##### Functions

###### LSMASHVideoSource

* `LSMASHVideoSource(string source, int track = 0, int threads = 0, int seek_mode = 0, int seek_threshold = 10,
                    bool dr = false, int fpsnum = 0, int fpsden = 1, string format = "", string decoder = "",
                    int prefer_hw = 0, int ff_loglevel = 0, string ff_options = "")`

        * This function uses libavcodec as video decoder and L-SMASH as demuxer.
        * RAP is an abbreviation of random accessible point.
        [Arguments]
            + source
                The path of the source file.
            + track (default : 0)
                The track number to open in the source file.
                The value 0 means trying to get the first detected video stream.
            + threads (default : 0)
                The number of threads to decode a stream by libavcodec.
                The value 0 means the number of threads is determined automatically and then the maximum value will be up to 16.
            + seek_mode (default : 0)
                How to process when any error occurs during decoding a video frame.
                    - 0 : Normal
                        This mode retries sequential decoding from the next closest RAP up to 3 cycles when any decoding error occurs.
                        If all 3 trial failed, retry sequential decoding from the last RAP by ignoring trivial errors.
                        Still error occurs, then return the last returned frame.
                    - 1 : Unsafe
                        This mode retries sequential decoding from the next closest RAP up to 3 cycles when any fatal decoding error occurs.
                        If all 3 trial failed, then return the last returned frame.
                    - 2 : Aggressive
                        This mode returns the last returned frame when any fatal decoding error occurs.
            + seek_threshold (default : 10)
                The threshold to decide whether a decoding starts from the closest RAP to get the requested video frame or doesn't.
                Let's say
                - the threshold is T,
                and
                - you request to seek the M-th frame called f(M) from the N-th frame called f(N).
                If M > N and M - N <= T, then
                    the decoder tries to get f(M) by decoding frames from f(N) sequentially.
                If M < N or M - N > T, then
                    check the closest RAP at the first.
                    After the check, if the closest RAP is identical with the last RAP, do the same as the case M > N and M - N <= T.
                    Otherwise, the decoder tries to get f(M) by decoding frames from the frame which is the closest RAP sequentially.
            + dr (default : false)
                Try direct rendering from the video decoder if 'dr' is set to true and 'format' is unspecfied.
                The output resolution will be aligned to be mod16-width and mod32-height by assuming two vertical 16x16 macroblock.
                For H.264 streams, in addition, 2 lines could be added because of the optimized chroma MC.
            + fpsnum (default : 0)
                Output frame rate numerator for VFR->CFR (Variable Frame Rate to Constant Frame Rate) conversion.
                If frame rate is set to a valid value, the conversion is achieved by padding and/or dropping frames at the specified frame rate.
                Otherwise, output frame rate is set to a computed average frame rate and the output process is performed by actual frame-by-frame.

				NOTE: You must explicitly set this if the source is an AVI file that contains null/drop frames that you would like to keep. For
				example, AVI files captured using VirtualDub commonly contain null/drop frames that were inserted during the capture process.
				Unless you provide this parameter, these null frames will be discarded, commonly resulting in loss of audio/video sync.
            + fpsden (default : 1)
                Output frame rate denominator for VFR->CFR (Variable Frame Rate to Constant Frame Rate) conversion.
                See 'fpsnum' in details.
            + format (default : "")
                Force specified output pixel format if 'format' is specified.
                The following formats are available currently.
                    "YUV420P8"
                    "YUV422P8"
                    "YUV444P8"
                    "YUV410P8"
                    "YUV411P8"
                    "YUV420P9"
                    "YUV422P9"
                    "YUV444P9"
                    "YUV420P10"
                    "YUV422P10"
                    "YUV444P10"
                    "YUV420P12"
                    "YUV422P12"
                    "YUV444P12"
                    "YUV420P14"
                    "YUV422P14"
                    "YUV444P14"
                    "YUV420P16"
                    "YUV422P16"
                    "YUV444P16"
                    "YUVA420P8"
                    "YUVA422P8"
                    "YUVA444P8"
                    "YUVA420P10"
                    "YUVA422P10"
                    "YUVA444P10"
                    "YUVA422P12"
                    "YUVA444P12"
                    "YUVA420P16"
                    "YUVA422P16"
                    "YUVA444P16"
                    "YUY2"
                    "Y8"
                    "Y10"
                    "Y12"
                    "Y14"
                    "Y16"
                    "RGB24"
                    "RGB32"
                    "RGB48"
                    "RGB64"
                    "GBRP8"
                    "GBRP10"
                    "GBRP12"
                    "GBRP14"
                    "GBRP16"
                    "GBRAP8"
                    "GBRAP10"
                    "GBRAP12"
                    "GBRAP16"
                    "XYZ12LE"
            + decoder (defalut : "")
                Names of preferred decoder candidates separated by comma.
                For instance, if you prefer to use the 'h264_qsv' and 'mpeg2_qsv' decoders instead of the generally
                used 'h264' and 'mpeg2video' decoder, then specify as "h264_qsv,mpeg2_qsv". The evaluations are done
                in the written order and the first matched decoder is used if any.
                The LWLDECODER variable can be used to see what decoder is used.
            + prefer_hw (default : 0)
                Whether to prefer hardware accelerated decoder to software decoder.
                Have no effect if 'decoder' is specified.
                    - 0 : Use default software decoder.
                    - 1 : Use NVIDIA CUVID acceleration for supported codec, otherwise use default software decoder.
                    - 2 : Use Intel Quick Sync Video acceleration for supported codec, otherwise use default software decoder.
                    - 3 : Try hardware decoder in the order of CUVID->QSV->DXVA2->D3D11VA->VULKAN. If none is available then use default software decoder.
                    - 4 : Use DXVA2 hardware acceleration for supported codec, otherwise use default software decoder.
                    - 5 : Use D3D11 hardware acceleration for supported codec, otherwise use default software decoder.
                    - 6 : Use VULKAN hardware acceleration for supported codec, otherwise use default software decoder.
                The LWLDECODER variable can be used to see what decoder is used.
            + ff_loglevel (default : 0)
                Set the log level in FFmpeg.
                    - 0 : AV_LOG_QUIET
                        Print no output.
                    - 1 : AV_LOG_PANIC
                        Something went really wrong and we will crash now.
                    - 2 : AV_LOG_FATAL
                        Something went wrong and recovery is not possible.
                    - 3 : AV_LOG_ERROR
                        Something went wrong and cannot losslessly be recovered. However, not all future data is affected.
                    - 4 : AV_LOG_WARNING
                        Something somehow does not look correct. This may or may not lead to problems.
                    - 5 : AV_LOG_INFO
                        Standard information.
                    - 6 : AV_LOG_VERBOSE
                        Detailed information.
                    - 7 : AV_LOG_DEBUG
                        Stuff which is only useful for libav* developers.
                    - 8 : AV_LOG_TRACE
                        Extremely verbose debugging, useful for libav* development.
            + ff_options (default : "")
                Set the decoder options in FFmpeg.
                The format is `key=value` separated by " ". (e.g. "drc_scale=0 auto_convert=0").

###### LSMASHAudioSource

* `LSMASHAudioSource(string source, int track = 0, bool skip_priming = true, string layout = "", int rate = 0,
                    string decoder = "", int ff_loglevel = 0, float drc_scale = 1.0, string ff_options = "")`

        * This function uses libavcodec as audio decoder and L-SMASH as demuxer.
        [Arguments]
            + source
                The path of the source file.
            + track (default : 0)
                The track number to open in the source file.
                The value 0 means trying to get the first detected audio stream.
            + skip_priming (default : true)
                Whether skip priming samples or not.
                Priming samples is detected from iTunSMPB or the first non-empty edit.
                If any priming samples, do pre-roll whenever any seek of audio stream occurs.
            + layout (default : "")
                Output audio channel layout.
                If unspecified, audio stream is output to the buffer from the decoder via the resampler at the channel layout
                which is the first maximum number of channels in audio stream.
                You can specify channel layout by combination of the name of a channel layout with separator (+) as follows.
                    - the name or mask of a single channel.
                        FL   (0x1)           = Front Left
                        FR   (0x2)           = Front Right
                        FC   (0x4)           = Front Center
                        LFE  (0x8)           = Low Frequency Effect
                        BL   (0x10)          = Back Left
                        BR   (0x20)          = Back Right
                        FLC  (0x40)          = Front Left of Center
                        FRC  (0x80)          = Front Right of Center
                        BC   (0x100)         = Back Center
                        SL   (0x200)         = Side Left
                        SR   (0x400)         = Side Right
                        TC   (0x800)         = Top Center
                        TFL  (0x1000)        = Top Front Left
                        TFC  (0x2000)        = Top Front Center
                        TFR  (0x4000)        = Top Front Right
                        TBL  (0x8000)        = Top Back Left
                        TBC  (0x10000)       = Top Back Center
                        TBR  (0x20000)       = Top Back Right
                        DL   (0x20000000)    = Stereo Downmixed Left
                        DR   (0x40000000)    = Stereo Downmixed Right
                        WL   (0x80000000)    = Wide Left
                        WR   (0x100000000)   = Wide Right
                        SDL  (0x200000000)   = Surround Direct Left
                        SDR  (0x400000000)   = Surround Direct Right
                        LFE2 (0x800000000)   = Low Frequency Effect 2
                        TSL  (0x1000000000)  = Top Side Left
                        TSR  (0x2000000000)  = Top Side Right
                        BFC  (0x4000000000)  = Bottom Front Center
                        BFL  (0x8000000000)  = Bottom Front Left
                        BFR  (0x10000000000) = Bottom Front Right
                            $ Example: standard ffmpeg based 5.1ch surround layout : FL+FR+FC+LFE+BL+BR = 0x3f
                    - the name of an usual channel layout.
                                            ffmpeg
                        mono           = FC
                        stereo         = FL+FR
                        2.1            = FL+FR+LFE
                        3.0            = FL+FR+FC
                        3.0(back)      = FL+FR+BC
                        3.1            = FL+FR+FC+LFE
                        4.0            = FL+FR+FC+BC
                        quad           = FL+FR+BL+BR
                        quad(side)     = FL+FR+SL+SR
                        4.1            = FL+FR+FC+LFE+BC
                        5.0            = FL+FR+FC+BL+BR
                        5.0(side)      = FL+FR+FC+SL+SR
                        5.1            = FL+FR+FC+LFE+BL+BR
                        5.1(side)      = FL+FR+FC+LFE+SL+SR
                        6.0            = FL+FR+FC+BC+SL+SR
                        6.0(front)     = FL+FR+FLC+FRC+SL+SR
                        hexagonal      = FL+FR+FC+BL+BR+BC
                        6.1            = FL+FR+FC+LFE+BC+SL+SR
                        6.1(back)      = FL+FR+FC+LFE+BL+BR+BC
                        6.1(front)     = FL+FR+LFE+FLC+FRC+SL+SR
                        7.0            = FL+FR+FC+BL+BR+SL+SR
                        7.0(front)     = FL+FR+FC+FLC+FRC+SL+SR
                        7.1            = FL+FR+FC+LFE+BL+BR+SL+SR
                        7.1(wide)      = FL+FR+FC+LFE+BL+BR+FLC+FRC
                        7.1(wide-side) = FL+FR+FC+LFE+FLC+FRC+SL+SR
                        7.1(top)       = FL+FR+FC+LFE+BL+BR+TFL+TFR
                        octagonal      = FL+FR+FC+BL+BR+BC+SL+SR
                        cube           = FL+FR+BL+BR+TFL+TFR+TBL+TBR
                        hexadecagonal  = FL+FR+FC+BL+BR+BC+SL+SR+TFL+TFC+TFR+TBL+TBC+TBR+WL+WR
                        downmix        = DL+DR
                        22.2           = FL+FR+FC+LFE+BL+BR+FLC+FRC+BC+SL+SR+TC+TFL+TFC+TFR+TBL+TBC+TBR+LFE2+TSL+TSR+BFC+BFL+BFR
                Note: the above listed notations are the present things.
                    In the future, they might be changed.
            + rate (default : 0)
                Audio sampling rate or sampling frequency in units of Hz.
                The value 0 means audio stream is output to the buffer via the resampler at the maximum sampling rate in audio stream.
                Otherwise, audio stream is output to the buffer via the resampler at specified sampling rate.
            + decoder (defalut : "")
                Same as 'decoder' of LSMASHVideoSource().
            + ff_loglevel (default : 0)
                Same as 'ff_loglevel' of LSMASHVideoSource().
            + drc_scale (defalut: 1.0)
                Dynamic Range Scale Factor. The factor to apply to dynamic range values from the AC-3 stream. This factor is applied exponentially.
                0.0 : DRC disabled. Produces full range audio.
                0.0 < drc_scale <= 1.0 : DRC enabled. Applies a fraction of the stream DRC value. Audio reproduction is between full range and full compression.
                > 1.0 : DRC enabled. Applies drc_scale asymmetrically. Loud sounds are fully compressed. Soft sounds are enhanced.
                If `ff_options="drc_scale=x"` is used, `drc_scale` is ignored.
            + ff_options (defalut: "")
                Same as 'ff_options' of LSMASHVideoSource().

###### LWLibavVideoSource

* `LWLibavVideoSource(string source, int stream_index = -1, int threads = 0, bool cache = true, string cachefile = source + ".lwi",
                    int seek_mode = 0, int seek_threshold = 10, bool dr = false, int fpsnum = 0, int fpsden = 1,
                    bool repeat = unspecified, int dominance = 0, string format = "", string decoder = "", int prefer_hw = 0,
                    int ff_loglevel = 0, string cachedir = "", string ff_options = "")`

        * This function uses libavcodec as video decoder and libavformat as demuxer.
        [Arguments]
            + source
                The path of the source file.
            + stream_index (default : -1)
                The stream index to open in the source file.
                The value -1 means trying to get the video stream which has the largest resolution.
            + threads (default : 0)
                Same as 'threads' of LSMASHVideoSource().
            + cache (default : true)
                Create the index file (.lwi) to the same directory as the source file if set to true.
                The index file avoids parsing all frames in the source file at the next or later access.
                Parsing all frames is very important for frame accurate seek.
            + cachefile (default : source + ".lwi")
                The filename of the index file (where the indexing data is saved).
            + seek_mode (default : 0)
                Same as 'seek_mode' of LSMASHVideoSource().
            + seek_threshold (default : 10)
                Same as 'seek_threshold' of LSMASHVideoSource().
            + dr (default : false)
                Same as 'dr' of LSMASHVideoSource().
            + fpsnum (default : 0)
                Same as 'fpsnum' of LSMASHVideoSource().
            + fpsden (default : 1)
                Same as 'fpsden' of LSMASHVideoSource().
            + repeat (default : unspecified)
                Reconstruct frames by the flags specified in video stream if set to true.
                If set to true, and source file requested repeat and the filter is unable to obey the request, this filter will fail explicitly to eliminate any guesswork.
                If unspecified, and source file requested repeat and the filter is unable to obey the request, silently returning a VFR clip with a constant (but wrong) fps.
                Note that this option is ignored when VFR->CFR conversion is enabled.
            + dominance : (default : 0)
                Which field, top or bottom, is displayed first.
                    - 0 : Obey source flags
                    - 1 : TFF i.e. Top -> Bottom
                    - 2 : BFF i.e. Bottom -> Top
                This option is enabled only if one or more of the following conditions is true.
                    - 'repeat' is set to true.
                    - There is a video frame consisting of two separated field coded pictures.
            + format (default : "")
                Same as 'format' of LSMASHVideoSource().
            + decoder (defalut : "")
                Same as 'decoder' of LSMASHVideoSource().
            + prefer_hw (default : 0)
                Same as 'prefer_hw' of LSMASHVideoSource().
            + ff_loglevel (default : 0)
                Same as 'ff_loglevel' of LSMASHVideoSource().
            + cachedir (defalut: "")
                Create *.lwi file under this directory with names encoding the full path to avoid collisions. Set to "" to restore the previous behavior (storing *.lwi along side the source video file).
            + indexingpr (default: true)
                Whether to print indexing progress to stderr.
            + ff_options (defalut: "")
                Same as 'ff_options' of LSMASHVideoSource().

###### LWLibavAudioSource

* `LWLibavAudioSource(string source, int stream_index = -1, bool cache = true, string cachefile = source + ".lwi", bool av_sync = false,
                    string layout = "", int rate = 0, string decoder = "", int ff_loglevel = 0, string cachedir = "",
                    float drc_scale = 1.0, string ff_options = "")`


        * This function uses libavcodec as audio decoder and libavformat as demuxer.
        * If audio stream can be coded as lossy, do pre-roll whenever any seek of audio stream occurs.
        [Arguments]
            + source
                The path of the source file.
            + stream_index (default : -1)
                The stream index to open in the source file.
                The value -1 means the defalut audio stream.
            + cache (default : true)
                Same as 'cache' of LWLibavVideoSource().
            + cachefile (default : source + ".lwi")
                Same as 'cachefile' of LWLibavVideoSource().
            + av_sync (default : false)
                Try Audio/Visual synchronization at the first video frame of the video stream activated in the index file if set to true.
            + layout (defalut : "")
                Same as 'layout' of LSMASHAudioSource().
            + rate (default : 0)
                Same as 'rate' of LSMASHAudioSource().
            + decoder (defalut : "")
                Same as 'decoder' of LSMASHVideoSource().
            + ff_loglevel (default : 0)
                Same as 'ff_loglevel' of LSMASHVideoSource().
            + cachedir (defalut: "")
                Create *.lwi file under this directory with names encoding the full path to avoid collisions. Set to "" to restore the previous behavior (storing *.lwi along side the source video file).
            + indexingpr (default: true)
                Whether to print indexing progress to stderr.
            + drc_scale (defalut: 1.0)
                Dynamic Range Scale Factor. The factor to apply to dynamic range values from the AC-3 stream. This factor is applied exponentially.
                0.0 : DRC disabled. Produces full range audio.
                0.0 < drc_scale <= 1.0 : DRC enabled. Applies a fraction of the stream DRC value. Audio reproduction is between full range and full compression.
                > 1.0 : DRC enabled. Applies drc_scale asymmetrically. Loud sounds are fully compressed. Soft sounds are enhanced.
                If `ff_options="drc_scale=x"` is used, `drc_scale` is ignored.
            + ff_options (defalut: "")
                Same as 'ff_options' of LSMASHVideoSource().
            + fill_agaps (defalut: 0)
                Simple filling of audio gaps with silence.
                This relies on PTS so the audio must have trustworthy PTS.
                Default `0` means this is disabled.
                The value is in AVStream->time_base units. For e.g., `fill_agaps=5` with `time_base={1, 1000}` means `5 ms`.
