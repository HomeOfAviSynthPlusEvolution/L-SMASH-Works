[AviSynth README](https://github.com/HomeOfAviSynthPlusEvolution/L-SMASH-Works/blob/master/AviSynth/README)

[VapourSynth README](https://github.com/HomeOfAviSynthPlusEvolution/L-SMASH-Works/blob/master/VapourSynth/README)

##### Note about LSMASHVideoSource and LibavSMASHSource

`LSMASHVideoSource`/`LibavSMASHSource` will/can result to worse performance compared to `LWLibavVideoSource`/`LWLibavSource` if it's used for anything than previewing. ([#41](https://github.com/HomeOfAviSynthPlusEvolution/L-SMASH-Works/issues/41))

##### Note about the file names (Windows only)

If you have issues about the file names here few tips:

- Try another previewing app. At the moment AvsPmod (<=2.7.5.5) has some issues. VirtualDub2 should work.
- Save the AviSynth script (`.avs`) as UTF-8 instead ANSI.
- Or just Enable UTF-8 code pages in Windows - `Windows Settings > Time & language > Language & region > Administrative language settings > Change system locale, and check Beta: Use Unicode UTF-8 for worldwide language support.`

##### CMake building options

|      Option      |          Description         | Default value |
|:---------------- |:---------------------------- |:-------------:|
| BUILD_AVS_PLUGIN | Build plugin for AviSynth    |       ON      |
| BUILD_VS_PLUGIN  | Build plugin for VapourSynth |       ON      |
| ENABLE_DAV1D     | Enable dav1d AV1 decoding    |       ON      |
| ENABLE_MFX       | Enable Intel HW decoding     |       ON      |
| ENABLE_XML2S     | Enable DNXHD support         |       ON      |
