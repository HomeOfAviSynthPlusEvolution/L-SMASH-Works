[AviSynth README](https://github.com/HomeOfAviSynthPlusEvolution/L-SMASH-Works/blob/master/AviSynth/README.md)

[VapourSynth README](https://github.com/HomeOfAviSynthPlusEvolution/L-SMASH-Works/blob/master/VapourSynth/README.md)

##### Note about LSMASHVideoSource and LibavSMASHSource

`LSMASHVideoSource`/`LibavSMASHSource` will/can result to worse performance compared to `LWLibavVideoSource`/`LWLibavSource` if it's used for anything than previewing. ([#41](https://github.com/HomeOfAviSynthPlusEvolution/L-SMASH-Works/issues/41))

##### Note about the file names (Windows only)

If you have issues about the file names save the AviSynth script (`.avs`) as UTF-8 or just enable UTF-8 code pages in Windows (if available) - `Windows Settings > Time & language > Language & region > Administrative language settings > Change system locale, and check Beta: Use Unicode UTF-8 for worldwide language support.`

##### Note about decoding audio and video from the same file in the same script

Always prefer this order

```
audio = LWLibavAudioSource()
video = LWLibavVideoSource()
```

over this order

```
video = LWLibavVideoSource()
audio = LWLibavAudioSource()
```

In the second case the file will be scanned twice and the video info will be written twice (overwritted) in the index file.

##### FFmpeg

[This](https://github.com/HomeOfAviSynthPlusEvolution/FFmpeg/tree/custom-patches-for-lsmashsource) is the recommended FFmpeg version.

##### CMake building options

|        Option         |          Description                                            |    Default    |
|:----------------------|:----------------------------------------------------------------|:--------------|
| BUILD_AVS_PLUGIN      | Build plugin for AviSynth                                       |       ON      |
| BUILD_VS_PLUGIN       | Build plugin for VapourSynth                                    |       ON      |
| ENABLE_DAV1D          | Enable dav1d AV1 decoding                                       |       ON      |
| dav1d_USE_STATIC_LIBS | Look for static dav1d libraries                                 |       ON      |
| ENABLE_MFX            | Enable Intel HW decoding                                        |       ON      |
| ENABLE_XML2           | Enable DNXHD support                                            |       ON      |
| ENABLE_VPX            | Enable libvpx decoding                                          |       ON      |
| VPX_USE_STATIC_LIBS   | Look for static libvpx libraries                                |       ON      |
| ENABLE_SSE2           | Force SSE2                                                      |       ON      |
| BUILD_INDEXING_TOOL   | Build indexing tool                                             |       OFF     |
| ENABLE_VULKAN         | Enable Vulkan decoding                                          |       ON      |
| ZLIB_USE_STATIC_LIBS  | Look for static zlib libraries                                  |       ON      |
| BUILD_SHARED_LIBS     | Build shared dependencies libraries (xxHash, obuparse, l-smash) |       OFF     |
