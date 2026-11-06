# ISOBMFF muxer evaluation

This branch evaluates MPEGGroup/isobmff as a possible MP4/M4A container writer.
It deliberately does not change the Android app, enable conversion, or label raw
USAC elementary-stream bytes as an M4A file.

## Pinned upstream source

- Repository: https://github.com/MPEGGroup/isobmff
- Commit: `689ac096a8e485385089341785c10271cf0684a8`
- Build mode: `ISOBMFF_BUILD_LIB_ONLY=ON`

## What the CI experiment proves

The workflow checks that the upstream C library builds on Linux and exports the
movie, sample-description, sample-addition, and file-writing APIs needed by a
basic muxer. This is only a build/API check; it is not an Android ARM64 build,
an AOT 42 conformance test, or proof that a generated M4A decodes.

## Remaining technical gates

1. Confirm the exact decoder-specific configuration bytes emitted by libxaac
   and their relationship to its initial output bytes.
2. Confirm the required USAC AudioSpecificConfig (AOT 42) is stored in the
   MPEG-4 DecoderSpecificInfo descriptor without modification.
3. Confirm each libxaac output access unit is one MP4 sample and determine its
   exact duration, priming delay, and end trimming from encoder metadata.
4. Build the library for Android ARM64 and verify platform headers/toolchain.
5. Produce a test M4A and validate its boxes and decode it with an independent
   USAC-capable decoder before enabling the app's Convert button.

## Licensing note

The upstream source files carry MPEG/Apple-origin copyright and patent notices.
Review the upstream terms and intended MPEG-4-conforming use before bundling or
redistributing the library in a public APK.
