# Exhale Android ARM64 evaluation

This experiment evaluates Exhale independently from the libxaac/isobmff POC.

Pinned source mirror: https://github.com/dtseto/exhale
Pinned revision: `fe9acb3f8739fc970dc6b86592f529bbe2f9036e`
Original GUI reference: https://github.com/moisespr123/exhale-gui

## Why evaluate it

Exhale is a separate USAC/xHE-AAC encoder and includes its own MPEG-4 writer. The GUI project is only a Windows Visual Basic wrapper that launches `exhale.exe`; it is not the encoder itself.

## Gates in CI

1. Build the upstream Exhale host executable without modifying its source.
2. Encode a 48 kHz stereo WAV using an existing preset (mode 5).
3. Check that the output is an MP4/M4A with `ftyp`, `moov`, `mdat`, `mp4a`, and `esds`.
4. Cross-compile the upstream `exhaleLib` static library for `arm64-v8a`, Android API 29.

A successful cross-compile proves only that the library can be built for the target ABI/API. It does not yet prove Android JNI integration, runtime correctness on the TECNO device, or playback compatibility. The host M4A structural test does not prove decoding by every player.

## Important source constraints

The upstream README documents supported input sampling rates of 32–48 kHz and asks users to use existing presets rather than modifying the encoder or forcing unsupported lower bitrates. The upstream license also contains a patent-rights disclaimer: no patent license is granted by the source license. Review licensing requirements before distributing an app that encodes standardized USAC bitstreams.

## Device/API context

The target app's Android 12 / API 29 baseline cannot rely on Android's built-in xHE-AAC encoder. Current Android platform documentation places the platform xHE-AAC encoder at API 37.1+. This experiment therefore evaluates Exhale as an app-bundled native encoder instead of assuming platform support.

## On-device JNI smoke test (TECNO / Android 12)

The user installed the ARM64 debug APK produced by workflow run 42365547621 and ran the in-app **PROBAR MOTOR EXHALE** action successfully. The device displayed:

- `Exhale JNI OK`
- AudioSpecificConfig (ASC): 7 bytes
- Lookahead access unit: 7 bytes
- Encoded access unit: 34 bytes

This confirms that the pinned Exhale library loads through Android JNI, initializes on the target device, and emits at least one USAC access unit from the test PCM buffer. It does **not** validate conversion of a selected source file, MP4 muxing on Android, duration/gapless metadata, or playback.

## Requirements before enabling conversion

The Android conversion path must follow the upstream Exhale application sequence rather than concatenating access units into a generic MP4:

1. Decode source media to PCM and normalize it to Exhale's signed 24-bit-in-`int32_t` input representation (PCM16 samples are multiplied by 256).
2. Respect Exhale's supported sample-rate/preset combinations; the upstream application currently documents 32–48 kHz input. The UI's 22.05 and 24 kHz options must not be advertised as supported until a valid resampling path is implemented and tested.
3. Reproduce the encoder's lookahead, pre-roll, frame-flush and finalization sequence.
4. Use Exhale's `BasicMP4Writer` (or a separately validated equivalent) to write the USAC sample entry, ASC, sample tables, random-access/pre-roll metadata and final presentation duration.
5. Validate output structure and decode/playback independently. FFmpeg's lack of AOT 42 decoding is not a valid USAC decode test.

The current Android app deliberately keeps **CONVERTIR A M4A** disabled until these requirements are met. The native probe remains a diagnostic only.

