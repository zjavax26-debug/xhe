#include <jni.h>
#include <cstdint>
#include <vector>
#include <cstdio>
#include <cstring>
#include <string>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include "exhaleDecl.h"
#include "basicMP4Writer.h"

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
    if (lookaheadBytes >= 3) frameBytes = exhaleEncodeFrame(encoder);
    exhaleDelete(encoder);

    if (lookaheadBytes < 3 || frameBytes < 3) {
        return env->NewStringUTF("Exhale: la prueba no produjo tramas válidas");
    }

    char message[160];
    std::snprintf(message, sizeof(message),
        "Exhale JNI OK: ASC=%u bytes, lookahead=%u bytes, trama=%u bytes",
        ascBytes, lookaheadBytes, frameBytes);
    return env->NewStringUTF(message);
}

// Creates a short silent USAC sample using Exhale's own MP4 writer. This is a
// writer integration smoke test, not the production source-audio conversion path.
extern "C" JNIEXPORT jstring JNICALL
Java_com_xhe_audioconverter_MainActivity_nativeExhaleMp4Probe(
        JNIEnv* env, jclass, jstring outputPath) {
    if (outputPath == nullptr) return env->NewStringUTF("M4A: ruta de salida vacía");
    const char* pathChars = env->GetStringUTFChars(outputPath, nullptr);
    if (pathChars == nullptr) return env->NewStringUTF("M4A: no se pudo leer la ruta");
    const std::string path(pathChars);
    env->ReleaseStringUTFChars(outputPath, pathChars);

    constexpr unsigned sampleRate = 48000;
    constexpr unsigned channels = 2;
    constexpr unsigned frameLength = 1024;
    constexpr unsigned bitrateMode = 5;
    constexpr unsigned indepPeriod = 50;
    constexpr unsigned audioSamples = frameLength * 2;

    // Four PCM frames give the encoder enough lookahead/input for this fixture.
    std::vector<int32_t> pcm(frameLength * channels * 4, 0);
    std::vector<unsigned char> au(1536 * channels, 0);
    std::vector<unsigned char> asc(108, 0);
    ExhaleEncAPI* encoder = exhaleCreate(
        pcm.data(), au.data(), sampleRate, channels, frameLength,
        indepPeriod, bitrateMode, true, false);
    if (encoder == nullptr) return env->NewStringUTF("M4A: exhaleCreate falló");

    uint32_t ascBytes = 0;
    const unsigned initResult = exhaleInitEncoder(encoder, asc.data(), &ascBytes);
    if (initResult != 0 || ascBytes < 5 || ascBytes > asc.size()) {
        exhaleDelete(encoder);
        return env->NewStringUTF("M4A: ASC inválida");
    }

    const int fd = ::open(path.c_str(), O_CREAT | O_TRUNC | O_RDWR, 0600);
    if (fd < 0) {
        exhaleDelete(encoder);
        return env->NewStringUTF("M4A: no se pudo crear el archivo");
    }

    BasicMP4Writer writer;
    int result = 0;
    if (writer.open(fd, sampleRate, channels, 24, frameLength, 0,
                    indepPeriod, asc.data(), ascBytes) != 0) {
        result = -1;
    }
    if (result == 0 && writer.initHeader(audioSamples, 0) < 666) result = -2;

    unsigned frameCount = 0;
    if (result == 0) {
        unsigned bytes = exhaleEncodeLookahead(encoder);
        if (bytes < 3 || bytes > au.size() ||
            writer.addFrameAU(au.data(), bytes) != static_cast<int>(bytes)) {
            result = -3;
        } else {
            ++frameCount;
        }
    }
    // Two following frames cover the 2,048-sample fixture plus writer post-roll.
    for (unsigned i = 0; result == 0 && i < 2; ++i) {
        const unsigned bytes = exhaleEncodeFrame(encoder);
        if (bytes < 3 || bytes > au.size() ||
            writer.addFrameAU(au.data(), bytes) != static_cast<int>(bytes)) {
            result = -4;
        } else {
            ++frameCount;
        }
    }

    if (result == 0 &&
        writer.finishFile(64000, 64000, audioSamples) <= 0) result = -5;

    exhaleDelete(encoder);
    struct stat st {};
    if (::fstat(fd, &st) != 0 || st.st_size <= 600) result = -6;
    ::close(fd);

    if (result != 0) {
        ::unlink(path.c_str());
        char error[128];
        std::snprintf(error, sizeof(error), "M4A writer falló (etapa %d)", result);
        return env->NewStringUTF(error);
    }

    // Verify the top-level MP4 boxes and Exhale's USAC sample-entry descriptors.
    const int checkFd = ::open(path.c_str(), O_RDONLY);
    std::vector<unsigned char> bytes(static_cast<size_t>(st.st_size));
    const ssize_t readBytes = checkFd >= 0
        ? ::read(checkFd, bytes.data(), bytes.size()) : -1;
    if (checkFd >= 0) ::close(checkFd);
    const auto contains = [&bytes](const char* token) {
        return std::search(bytes.begin(), bytes.end(), token, token + 4) != bytes.end();
    };
    if (readBytes != st.st_size || !contains("ftyp") || !contains("moov") ||
        !contains("mdat") || !contains("mp4a") || !contains("esds")) {
        ::unlink(path.c_str());
        return env->NewStringUTF("M4A: archivo escrito, pero falló la verificación de cajas MP4");
    }

    char message[256];
    std::snprintf(message, sizeof(message),
        "M4A writer OK: %lld bytes, %u tramas USAC, ftyp/moov/mdat + mp4a/esds",
        static_cast<long long>(st.st_size), frameCount);
    return env->NewStringUTF(message);
}
