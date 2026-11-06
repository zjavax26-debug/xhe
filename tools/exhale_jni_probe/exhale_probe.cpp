#include <jni.h>
#include <cstdint>
#include <vector>
#include <string>
#include <cstdio>
#include "exhaleDecl.h"

extern "C" JNIEXPORT jstring JNICALL
Java_com_xhe_audioconverter_MainActivity_nativeExhaleProbe(JNIEnv* env, jclass) {
    constexpr unsigned sampleRate = 48000;
    constexpr unsigned channels = 2;
    constexpr unsigned frameLength = 1024;
    constexpr unsigned bitrateMode = 5;
    constexpr unsigned indepPeriod = 50;

    std::vector<int32_t> pcm(frameLength * channels * 4, 0);
    std::vector<unsigned char> au(1536 * channels, 0);
    std::vector<unsigned char> asc(108, 0);

    ExhaleEncAPI* encoder = exhaleCreate(
        pcm.data(), au.data(), sampleRate, channels, frameLength,
        indepPeriod, bitrateMode, true, false);
    if (encoder == nullptr) {
        return env->NewStringUTF("Exhale: no se pudo crear el codificador");
    }

    uint32_t ascBytes = 0;
    unsigned initResult = exhaleInitEncoder(encoder, asc.data(), &ascBytes);
    if (initResult != 0 || ascBytes == 0 || ascBytes > asc.size()) {
        exhaleDelete(encoder);
        return env->NewStringUTF("Exhale: falló la inicialización USAC");
    }

    unsigned lookaheadBytes = exhaleEncodeLookahead(encoder);
    unsigned frameBytes = 0;
    if (lookaheadBytes >= 3) {
        frameBytes = exhaleEncodeFrame(encoder);
    }
    exhaleDelete(encoder);

    if (lookaheadBytes < 3 || frameBytes < 3) {
        return env->NewStringUTF("Exhale: la prueba de codificación no produjo tramas");
    }

    char message[160];
    snprintf(message, sizeof(message),
             "Exhale JNI OK: ASC=%u bytes, lookahead=%u bytes, trama=%u bytes",
             ascBytes, lookaheadBytes, frameBytes);
    return env->NewStringUTF(message);
}
