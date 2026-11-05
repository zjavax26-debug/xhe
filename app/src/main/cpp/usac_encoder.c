#include <jni.h>
#include <stdint.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ixheaac_type_def.h"
#include "ixheaac_error_standards.h"
#include "ixheaace_api.h"
#include "ixheaace_loudness_measurement.h"
#include "ixheaac_constants.h"
#include "iusace_cnst.h"
#include "impd_drc_common_enc.h"
#include "impd_drc_uni_drc.h"
#include "impd_drc_tables.h"
#include "impd_drc_api.h"

static pVOID xhe_alloc(UWORD32 size, UWORD32 alignment) {
    void *ptr = NULL;
    size_t a = alignment < sizeof(void *) ? sizeof(void *) : alignment;
    if ((a & (a - 1)) != 0 || posix_memalign(&ptr, a, size ? size : 1) != 0) return NULL;
    memset(ptr, 0, size ? size : 1);
    return ptr;
}

static VOID xhe_free(pVOID ptr) { free(ptr); }

static IA_ERRORCODE measure_wav_loudness(FILE *pcm, ixheaace_input_config *in,
                                          ixheaace_output_config *out) {
    const UWORD32 samples_per_channel = in->i_samp_freq / 10;
    WORD16 **channels = NULL;
    pVOID handle = NULL;
    IA_ERRORCODE err = IA_NO_ERROR;
    WORD32 ch;
    if (samples_per_channel == 0 || in->aac_config.length < (WORD32)(samples_per_channel * in->i_channels * 2))
        return -1;

    handle = xhe_alloc((UWORD32)ixheaace_loudness_info_get_handle_size(), DEFAULT_MEM_ALIGN_8);
    channels = (WORD16 **)xhe_alloc((UWORD32)(in->i_channels * sizeof(*channels)), DEFAULT_MEM_ALIGN_8);
    if (!handle || !channels) { xhe_free(handle); xhe_free(channels); return -1; }

    err = ixheaace_loudness_init_params(handle, in, out);
    if (err != 0) { xhe_free(handle); xhe_free(channels); return err; }

    for (ch = 0; ch < in->i_channels; ch++) {
        channels[ch] = (WORD16 *)xhe_alloc(samples_per_channel * sizeof(WORD16), DEFAULT_MEM_ALIGN_8);
        if (!channels[ch]) {
            while (ch > 0) xhe_free(channels[--ch]);
            xhe_free(channels); xhe_free(handle); return -1;
        }
    }

    const UWORD32 interleaved_count = samples_per_channel * (UWORD32)in->i_channels;
    WORD16 *interleaved = (WORD16 *)xhe_alloc(interleaved_count * sizeof(WORD16), DEFAULT_MEM_ALIGN_8);
    if (!interleaved) {
        for (ch = 0; ch < in->i_channels; ch++) xhe_free(channels[ch]);
        xhe_free(channels); xhe_free(handle); return -1;
    }

    const UWORD32 total_samples_per_channel = (UWORD32)in->aac_config.length / (2U * (UWORD32)in->i_channels);
    UWORD32 processed = 0;
    while (total_samples_per_channel - processed >= samples_per_channel) {
        if (fread(interleaved, sizeof(WORD16), interleaved_count, pcm) != interleaved_count) {
            err = -1; break;
        }
        for (UWORD32 n = 0; n < samples_per_channel; n++)
            for (ch = 0; ch < in->i_channels; ch++)
                channels[ch][n] = interleaved[n * (UWORD32)in->i_channels + (UWORD32)ch];
        (void)ixheaace_measure_loudness(handle, channels);
        processed += samples_per_channel;
    }

    if (err == 0) {
        in->measured_loudness = ixheaace_measure_integrated_loudness(handle);
        in->sample_peak_level = ixheaace_measure_sample_peak_value(handle);
    }
    xhe_free(interleaved);
    for (ch = 0; ch < in->i_channels; ch++) xhe_free(channels[ch]);
    xhe_free(channels);
    xhe_free(handle);
    return err;
}

/*
 * Encodes a decoded PCM16 WAV to a raw USAC elementary stream and a frame-size
 * sidecar. This intentionally does not claim to create an M4A container.
 */
JNIEXPORT jstring JNICALL
Java_com_xhe_audioconverter_MainActivity_nativeEncodeWav(
        JNIEnv *env, jclass clazz, jstring inputPath, jstring outputPath,
        jint sampleRate, jint channels, jint bitrateKbps) {
    (void)clazz;
    if (!inputPath || !outputPath || sampleRate < 8000 || sampleRate > 96000 ||
        (channels != 1 && channels != 2) || bitrateKbps < 8 || bitrateKbps > 512)
        return (*env)->NewStringUTF(env, "Parámetros de codificación no válidos");

    const char *in_path = (*env)->GetStringUTFChars(env, inputPath, NULL);
    const char *out_path = (*env)->GetStringUTFChars(env, outputPath, NULL);
    if (!in_path || !out_path) {
        if (in_path) (*env)->ReleaseStringUTFChars(env, inputPath, in_path);
        if (out_path) (*env)->ReleaseStringUTFChars(env, outputPath, out_path);
        return (*env)->NewStringUTF(env, "No se pudieron leer las rutas");
    }

    FILE *wav = fopen(in_path, "rb");
    FILE *raw = NULL;
    FILE *meta = NULL;
    char meta_path[1024];
    char message[256] = "Error desconocido al codificar USAC";
    ixheaace_user_config_struct api;
    memset(&api, 0, sizeof(api));
    ixheaace_input_config *in = &api.input_config;
    ixheaace_output_config *out = &api.output_config;
    int created = 0;
    UWORD32 *frame_bytes = NULL;

    if (!wav) { snprintf(message, sizeof(message), "No se pudo abrir el WAV (%d)", errno); goto cleanup; }
    if (fseek(wav, 0, SEEK_END) != 0) { snprintf(message, sizeof(message), "No se pudo leer el WAV"); goto cleanup; }
    long file_size = ftell(wav);
    if (file_size < 44 || fseek(wav, 44, SEEK_SET) != 0) { snprintf(message, sizeof(message), "WAV inválido"); goto cleanup; }
    long pcm_size = file_size - 44;
    if (pcm_size <= 0 || pcm_size > INT32_MAX || pcm_size % (channels * 2) != 0) {
        snprintf(message, sizeof(message), "Tamaño PCM no válido"); goto cleanup;
    }

    in->pv_drc_cfg = xhe_alloc((UWORD32)sizeof(ia_drc_input_config), DEFAULT_MEM_ALIGN_8);
    if (!in->pv_drc_cfg) { snprintf(message, sizeof(message), "Memoria insuficiente para DRC"); goto cleanup; }
    in->aac_config.bitrate = bitrateKbps * 1000;
    in->aac_config.bandwidth = 0;
    in->aac_config.inv_quant = 2;
    in->aac_config.use_tns = 1;
    in->aac_config.noise_filling = 0;
    /* Match libxaac testbench default: APP_BITRES_SIZE_CONFIG_PARAM_DEF_VALUE_LC (768). */
    in->aac_config.bitreservoir_size = 768;
    in->aac_config.length = (WORD32)pcm_size;
    in->i_bitrate = bitrateKbps * 1000;
    in->ui_pcm_wd_sz = 16;
    in->aot = AOT_USAC;
    in->codec_mode = USAC_ONLY_FD;
    in->usac_en = 1;
    in->i_channels = channels;
    in->i_samp_freq = sampleRate;
    in->i_native_samp_freq = sampleRate;
    in->i_channels_mask = channels == 1 ? 4 : 3;
    in->i_use_mps = 0;
    in->i_mps_tree_config = -1;
    in->i_use_adts = 0;
    in->i_use_es = 1;
    in->esbr_flag = 1;
    in->cplx_pred = 0;
    in->ccfl_idx = NO_SBR_CCFL_1024;
    in->pvc_active = 0;
    in->harmonic_sbr = 0;
    in->inter_tes_active = 0;
    in->use_drc_element = 0;
    in->i_num_coupling_chan = 0;
    in->aac_config.full_bandwidth = 0;
    in->frame_cmd_flag = 0;
    in->out_bytes_flag = 0;
    in->user_tns_flag = 0;
    in->user_esbr_flag = 0;
    in->random_access_interval = DEFAULT_RAP_INTERVAL_IN_MS;
    in->method_def = METHOD_DEFINITION_PROGRAM_LOUDNESS;
    in->measurement_system = MEASUREMENT_SYSTEM_BS_1770_3;
    in->use_delay_adjustment = USAC_DEFAULT_DELAY_ADJUSTMENT_VALUE;
    out->malloc_xheaace = xhe_alloc;
    out->free_xheaace = xhe_free;

    if (measure_wav_loudness(wav, in, out) != 0) {
        snprintf(message, sizeof(message), "No se pudo medir la sonoridad del audio");
        goto cleanup;
    }
    if (fseek(wav, 44, SEEK_SET) != 0) { snprintf(message, sizeof(message), "No se pudo reposicionar el WAV"); goto cleanup; }

    if (ixheaace_create((pVOID)in, (pVOID)out) != 0) {
        snprintf(message, sizeof(message), "libxaac no pudo inicializar USAC"); goto cleanup;
    }
    created = 1;
    pVOID input = out->mem_info_table[IA_MEMTYPE_INPUT].mem_ptr;
    pVOID encoded = out->mem_info_table[IA_MEMTYPE_OUTPUT].mem_ptr;
    if (!input || !encoded || out->input_size <= 0) {
        snprintf(message, sizeof(message), "libxaac no reservó buffers válidos"); goto cleanup;
    }

    raw = fopen(out_path, "wb");
    if (!raw) { snprintf(message, sizeof(message), "No se pudo crear el flujo USAC"); goto cleanup; }
    const WORD32 decoder_config_bytes = out->i_out_bytes;
    if (decoder_config_bytes < 0 ||
        (decoder_config_bytes > 0 && fwrite(encoded, 1, (size_t)decoder_config_bytes, raw) != (size_t)decoder_config_bytes)) {
        snprintf(message, sizeof(message), "Error al escribir configuración inicial USAC"); goto cleanup;
    }
    const WORD32 media_timescale = out->samp_freq /
            (out->down_sampling_ratio > 0 ? out->down_sampling_ratio : 1);
    const UWORD32 play_time_samples = (UWORD32)pcm_size / (2U * (UWORD32)channels);
    snprintf(meta_path, sizeof(meta_path), "%s.txt", out_path);
    meta = fopen(meta_path, "w");
    if (!meta) { snprintf(message, sizeof(message), "No se pudo crear el índice de tramas"); goto cleanup; }

    UWORD32 frame_count = 0;
    frame_bytes = (UWORD32 *)calloc((size_t)out->expected_frame_count + 2U, sizeof(UWORD32));
    if (!frame_bytes) { snprintf(message, sizeof(message), "Memoria insuficiente para el índice"); goto cleanup; }
    /*
     * Match the official libxaac testbench input cadence. With delay
     * adjustment enabled, input is advanced on every process call; otherwise
     * it advances only after an output access unit is produced.
     */
    size_t read_bytes = fread(input, 1, (size_t)out->input_size, wav);
    while (read_bytes > 0 && frame_count < (UWORD32)out->expected_frame_count) {
        if (read_bytes != (size_t)out->input_size)
            memset((UWORD8 *)input + read_bytes, 0, (size_t)out->input_size - read_bytes);

        IA_ERRORCODE err = ixheaace_process(out->pv_ia_process_api_obj, (pVOID)in, (pVOID)out);
        if (err != 0) {
            snprintf(message, sizeof(message), "Error de libxaac durante la trama (%d)", (int)err);
            goto cleanup;
        }

        if (out->i_out_bytes > 0) {
            if (fwrite(encoded, 1, (size_t)out->i_out_bytes, raw) != (size_t)out->i_out_bytes) {
                snprintf(message, sizeof(message), "Error al guardar las tramas USAC");
                goto cleanup;
            }
            frame_bytes[frame_count++] = (UWORD32)out->i_out_bytes;
            if (!in->use_delay_adjustment)
                read_bytes = fread(input, 1, (size_t)out->input_size, wav);
        }
        if (in->use_delay_adjustment)
            read_bytes = fread(input, 1, (size_t)out->input_size, wav);
    }
    if (ferror(wav) || frame_count == 0 ||
        frame_count != (UWORD32)out->expected_frame_count) {
        snprintf(message, sizeof(message), "Codificación incompleta: %u de %d tramas",
                 frame_count, out->expected_frame_count);
        goto cleanup;
    }
    /* Match the metadata contract emitted by the official libxaac testbench. */
    fprintf(meta, "-dec_info_init:%d\n", decoder_config_bytes);
    fprintf(meta, "-g_track_count:%d\n", 1);
    fprintf(meta, "-ia_mp4_stsz_entries:%u\n", frame_count);
    fprintf(meta, "-movie_time_scale:%d\n", media_timescale);
    fprintf(meta, "-media_time_scale:%d\n", media_timescale);
    fprintf(meta, "-playTimeInSamples:%u\n", play_time_samples);
    fprintf(meta, "-startOffsetInSamples:%d\n-useEditlist:%d\n", 0, 1);
    for (UWORD32 i = 0; i < frame_count; i++)
        fprintf(meta, "-ia_mp4_stsz_size:%u\n", frame_bytes[i]);
    fflush(raw); fflush(meta);
    snprintf(message, sizeof(message), "Flujo USAC generado (%u tramas). Aún no es un archivo M4A.", frame_count);

cleanup:
    free(frame_bytes);
    if (created) ixheaace_delete((pVOID)out);
    if (in->pv_drc_cfg) xhe_free(in->pv_drc_cfg);
    if (meta) fclose(meta);
    if (raw) fclose(raw);
    if (wav) fclose(wav);
    (*env)->ReleaseStringUTFChars(env, inputPath, in_path);
    (*env)->ReleaseStringUTFChars(env, outputPath, out_path);
    return (*env)->NewStringUTF(env, message);
}
