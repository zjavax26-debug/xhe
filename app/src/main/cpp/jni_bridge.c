#include <jni.h>
#include <string.h>
#include "ixheaac_type_def.h"
#include "ixheaace_api.h"

JNIEXPORT jstring JNICALL
Java_com_xhe_audioconverter_MainActivity_nativeEncoderVersion(JNIEnv *env, jclass clazz) {
    ixheaace_version version = {0};
    (void) clazz;
    if (ixheaace_get_lib_id_strings((pVOID)&version) != 0 ||
        version.p_lib_name == NULL || version.p_version_num == NULL) {
        return (*env)->NewStringUTF(env, "libxaac no disponible");
    }
    char description[256];
    description[0] = '\0';
    strncat(description, (const char *)version.p_lib_name, sizeof(description) - 1);
    strncat(description, (const char *)version.p_version_num,
            sizeof(description) - strlen(description) - 1);
    return (*env)->NewStringUTF(env, description);
}
