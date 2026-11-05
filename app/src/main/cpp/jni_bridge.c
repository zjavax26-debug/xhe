#include <jni.h>
#include <stdint.h>

/*
 * Native integration checkpoint.
 *
 * The upstream libxaac encoder is linked into this Android library. The JNI
 * conversion entry point will be added after the encoder's public API memory
 * lifecycle and MP4/USAC sample-entry packaging are implemented and tested.
 * Do not report a successful conversion until that path is complete.
 */
JNIEXPORT jint JNICALL
Java_com_xhe_audioconverter_MainActivity_nativeEncoderStatus(JNIEnv *env, jclass clazz) {
    (void) env;
    (void) clazz;
    return 1; /* 1 = libxaac linked; conversion pipeline not yet enabled */
}
