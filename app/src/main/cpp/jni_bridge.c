#include <jni.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ixheaac_type_def.h"
#include "ixheaace_api.h"

static pVOID xhe_aligned_alloc(UWORD32 size, UWORD32 alignment) {
    void *ptr = NULL;
    size_t a = alignment;
    if (a < sizeof(void *)) a = sizeof(void *);
    if ((a & (a - 1)) != 0) return NULL;
    if (posix_memalign(&ptr, a, size ? size : 1) != 0) return NULL;
    memset(ptr, 0, size ? size : 1);
    return ptr;
}

static VOID xhe_aligned_free(pVOID ptr) {
    free(ptr);
}

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

/*
 * Real one-frame API smoke test. This initializes the public libxaac encoder,
 * supplies one silent PCM frame and invokes ixheaace_process(). It deliberately
 * does not write an M4A file; MP4/USAC sample-entry packaging is a separate step.
 */
JNIEXPORT jstring JNICALL
Java_com_xhe_audioconverter_MainActivity_nativeEncoderProbe(
        JNIEnv *env, jclass clazz, jint sampleRate, jint channels, jint bitrate) {
    (void) clazz;
    if ((sampleRate != 22050 && sampleRate != 24000 && sampleRate != 32000 &&
         sampleRate != 44100 && sampleRate != 48000) ||
        (channels != 1 && channels != 2) || bitrate < 8000 || bitrate > 512000) {
        return (*env)->NewStringUTF(env, "Parámetros no admitidos");
    }

    ixheaace_user_config_struct config;
    memset(&config, 0, sizeof(config));
    ixheaace_input_config *in = &config.input_config;
    ixheaace_output_config *out = &config.output_config;

    in->ui_pcm_wd_sz = 16;
    in->i_bitrate = bitrate;
    in->aot = AOT_USAC;
    in->codec_mode = 1; /* USAC_ONLY_FD */
    in->i_channels = channels;
    in->i_samp_freq = sampleRate;
    in->i_native_samp_freq = sampleRate;
    in->i_channels_mask = channels == 1 ? 4 : 3;
    in->i_use_adts = 0;
    in->i_use_es = 1;
    in->usac_en = 1;
    in->esbr_flag = 1;
    in->aac_config.bitrate = bitrate;
    in->aac_config.sample_rate = sampleRate;
    in->aac_config.num_channels_in = channels;
    in->aac_config.num_channels_out = channels;
    in->aac_config.length = sampleRate * channels * 2;
    in->aac_config.use_tns = 1;
    in->aac_config.full_bandwidth = 1;
    in->aac_config.bandwidth = 0;
    in->random_access_interval = 1000;
    in->use_delay_adjustment = 0;
    in->method_def = 0;
    in->measured_loudness = -23.0;
    in->sample_peak_level = -1.0;

    out->malloc_xheaace = xhe_aligned_alloc;
    out->free_xheaace = xhe_aligned_free;

    IA_ERRORCODE err = ixheaace_create((pVOID)in, (pVOID)out);
    if (err != 0) {
        char msg[96];
        snprintf(msg, sizeof(msg), "No se pudo iniciar USAC (error %d)", (int)err);
        return (*env)->NewStringUTF(env, msg);
    }

    pVOID input = out->mem_info_table[IA_MEMTYPE_INPUT].mem_ptr;
    pVOID output = out->mem_info_table[IA_MEMTYPE_OUTPUT].mem_ptr;
    if (input == NULL || output == NULL || out->input_size <= 0) {
        ixheaace_delete((pVOID)out);
        return (*env)->NewStringUTF(env, "USAC inició sin buffers válidos");
    }

    memset(input, 0, (size_t)out->input_size);
    err = ixheaace_process(out->pv_ia_process_api_obj, (pVOID)in, (pVOID)out);
    int encodedBytes = out->i_out_bytes;
    ixheaace_delete((pVOID)out);

    if (err != 0 || encodedBytes <= 0) {
        char msg[112];
        snprintf(msg, sizeof(msg), "USAC inició, pero la trama falló (error %d)", (int)err);
        return (*env)->NewStringUTF(env, msg);
    }

    char msg[128];
    snprintf(msg, sizeof(msg), "Prueba USAC correcta: 1 trama codificada (%d bytes)", encodedBytes);
    return (*env)->NewStringUTF(env, msg);
}
