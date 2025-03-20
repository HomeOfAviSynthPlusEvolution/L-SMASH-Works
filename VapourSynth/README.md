#### File

    LSMASHSource.dll    : A source plugin for VapourSynth

##### Functions

###### lsmas.LibavSMASHSource

* `lsmas.LibavSMASHSource(string source, int track = 0, int threads = 0, int seek_mode = 0, int seek_threshold = 10,
                        int dr = 0, int fpsnum = 0, int fpsden = 1, int variable = 0, string format = "",
                        string decoder = "", int prefer_hw = 0, int ff_loglevel = 0, string ff_options = "")`

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
            + dr (default : 0)
                Try direct rendering from the video decoder if 'dr' is set to 1 and 'format' is unspecfied.
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
            + variable (default : 0)
                Treat format, width and height of the video stream as variable if set to 1.
            + format (default : "")
                Force specified output pixel format if 'format' is specified and 'variable' is set to 0.
                The following formats are available currently.
                    "YUV420P8"
                    "YUV422P8"
                    "YUV444P8"
                    "YUV410P8"
                    "YUV411P8"
                    "YUV440P8"
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
                    "Y8"
                    "Y16"
                    "RGB24"
                    "RGB27"
                    "RGB30"
                    "RGB48"
                    "RGB64BE"
                    "XYZ12LE"
            + decoder (defalut : "")
                Names of preferred decoder candidates separated by comma.
                For instance, if you prefer to use the 'h264_qsv' and 'mpeg2_qsv' decoders instead of the generally
                used 'h264' and 'mpeg2video' decoder, then specify as "h264_qsv,mpeg2_qsv". The evaluations are done
                in the written order and the first matched decoder is used if any.
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

###### lsmas.LWLibavSource

* `lsmas.LWLibavSource(string source, int stream_index = -1, int threads = 0, int cache = 1, string cachefile = source + ".lwi",
                        int seek_mode = 0, int seek_threshold = 10, int dr = 0, int fpsnum = 0, int fpsden = 1, int variable = 0,
                        string format = "", int repeat = 2, int dominance = 0, string decoder = "", int prefer_hw = 0, int ff_loglevel = 0,
                        string cachedir = "", string ff_options = "")`

        * This function uses libavcodec as video decoder and libavformat as demuxer.
        [Arguments]
            + source
                The path of the source file.
            + stream_index (default : -1)
                The stream index to open in the source file.
                The value -1 means trying to get the video stream which has the largest resolution.
            + threads (default : 0)
                Same as 'threads' of LibavSMASHSource().
            + cache (default : 1)
                Create the index file (.lwi) to the same directory as the source file if set to 1.
                The index file avoids parsing all frames in the source file at the next or later access.
                Parsing all frames is very important for frame accurate seek.
            + cachefile (default : source + ".lwi")
                The filename of the index file (where the indexing data is saved).
            + seek_mode (default : 0)
                Same as 'seek_mode' of LibavSMASHSource().
            + seek_threshold (default : 10)
                Same as 'seek_threshold' of LibavSMASHSource().
            + dr (default : 0)
                Same as 'dr' of LibavSMASHSource().
            + fpsnum (default : 0)
                Same as 'fpsnum' of LibavSMASHSource().
            + fpsden (default : 1)
                Same as 'fpsden' of LibavSMASHSource().
            + variable (default : 0)
                Same as 'variable' of LibavSMASHSource().
            + format (default : "")
                Same as 'format' of LibavSMASHSource().
            + repeat (default : 2)
                Reconstruct frames by the flags specified in video stream if set to non-zero value.
                If set to 1, and source file requested repeat and the filter is unable to obey the request, this filter will fail explicitly to eliminate any guesswork.
                If set to 2, and source file requested repeat and the filter is unable to obey the request, silently returning a VFR clip with a constant (but wrong) fps.
                Note that this option is ignored when VFR->CFR conversion is enabled.
            + dominance : (default : 0)
                Which field, top or bottom, is displayed first.
                    - 0 : Obey source flags
                    - 1 : TFF i.e. Top -> Bottom
                    - 2 : BFF i.e. Bottom -> Top
                This option is enabled only if one or more of the following conditions is true.
                    - 'repeat' is set to 1.
                    - There is a video frame consisting of two separated field coded pictures.
            + decoder (defalut : "")
                Same as 'decoder' of LibavSMASHSource().
            + prefer_hw (default : 0)
                Same as 'prefer_hw' of LibavSMASHSource().
            + ff_loglevel (default : 0)
                Same as 'ff_loglevel' of LibavSMASHSource().
            + cachedir (default : "")
                Create *.lwi file under this directory with names encoding the full path to avoid collisions.
            + ff_options (defalut: "")
                Same as 'ff_options' of LibavSMASHSource().
