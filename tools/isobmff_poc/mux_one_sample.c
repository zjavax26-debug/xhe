/*
 * Structural MP4 audio-track proof of concept for MPEGGroup/isobmff.
 * Inputs: ASC bytes, one encoded access unit, output path, sample rate,
 * access-unit duration in samples, average bitrate.
 *
 * This intentionally does not invent an ASC or claim codec conformance.
 */
#include "MP4Movies.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned char *read_file(const char *path, unsigned long *size) {
    FILE *f = fopen(path, "rb");
    unsigned char *data;
    long n;
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) || (n = ftell(f)) <= 0 || fseek(f, 0, SEEK_SET)) {
        fclose(f); return NULL;
    }
    data = (unsigned char *)malloc((size_t)n);
    if (!data || fread(data, 1, (size_t)n, f) != (size_t)n) {
        free(data); fclose(f); return NULL;
    }
    fclose(f); *size = (unsigned long)n; return data;
}

#define CHECK(call) do { err = (call); if (err != MP4NoErr) { \
    fprintf(stderr, #call " failed: %d\\n", err); goto done; } } while (0)

int main(int argc, char **argv) {
    unsigned long asc_len = 0, au_len = 0;
    unsigned char *asc = NULL, *au = NULL, *dsi = NULL;
    MP4Movie movie = NULL;
    MP4Track track = NULL;
    MP4Media media = NULL;
    MP4Handle dsi_h = NULL, entry_h = NULL, sample_h = NULL;
    MP4Handle duration_h = NULL, size_h = NULL;
    MP4Err err = MP4NoErr;
    unsigned long sample_rate, duration, bitrate;
    unsigned int dsi_len;

    if (argc != 7) {
        fprintf(stderr, "usage: %s asc.bin access-unit.bin output.m4a rate duration_samples bitrate\\n", argv[0]);
        return 2;
    }
    sample_rate = strtoul(argv[4], NULL, 10);
    duration = strtoul(argv[5], NULL, 10);
    bitrate = strtoul(argv[6], NULL, 10);
    asc = read_file(argv[1], &asc_len);
    au = read_file(argv[2], &au_len);
    if (!asc || !au || asc_len == 0 || asc_len > 127 || au_len == 0 ||
        sample_rate == 0 || duration == 0 || bitrate == 0) {
        fprintf(stderr, "invalid ASC/access unit or numeric parameters\\n");
        err = MP4BadParamErr; goto done;
    }

    /* MPEG-4 DecoderSpecificInfo descriptor: tag 0x05 + 7-bit length + ASC. */
    dsi_len = (unsigned int)asc_len + 2U;
    dsi = (unsigned char *)malloc(dsi_len);
    if (!dsi) { err = MP4NoMemoryErr; goto done; }
    dsi[0] = 0x05; dsi[1] = (unsigned char)asc_len;
    memcpy(dsi + 2, asc, asc_len);

    CHECK(MP4NewMovie(&movie, 1, 0xff, 0xff, 0xff, 0xff, 0xff));
    CHECK(MP4NewMovieTrack(movie, MP4NewTrackIsAudio, &track));
    CHECK(MP4AddTrackToMovieIOD(track));
    CHECK(MP4NewTrackMedia(track, &media, MP4AudioHandlerType, (u32)sample_rate, NULL));
    CHECK(MP4SetMediaLanguage(media, "und"));
    CHECK(MP4BeginMediaEdits(media));

    CHECK(MP4NewHandle(dsi_len, &dsi_h));
    memcpy(*dsi_h, dsi, dsi_len);
    CHECK(MP4NewHandle(0, &entry_h));
    CHECK(MP4NewSampleDescription(track, entry_h, 1, 0x40, 0x05,
                                  2U * 6144U, (u32)bitrate, (u32)bitrate, dsi_h));

    CHECK(MP4NewHandle(au_len, &sample_h));
    memcpy(*sample_h, au, au_len);
    CHECK(MP4NewHandle(sizeof(u32), &duration_h));
    *(u32 *)(*duration_h) = (u32)duration;
    CHECK(MP4NewHandle(sizeof(u32), &size_h));
    *(u32 *)(*size_h) = (u32)au_len;
    CHECK(MP4AddMediaSamples(media, sample_h, 1, duration_h, size_h,
                              entry_h, NULL, NULL));
    CHECK(MP4EndMediaEdits(media));
    {
        u64 media_duration = 0;
        CHECK(MP4GetMediaDuration(media, &media_duration));
        CHECK(MP4InsertMediaIntoTrack(track, 0, 0, media_duration, 1));
    }
    CHECK(MP4WriteMovieToFile(movie, argv[3]));
    fprintf(stdout, "Wrote structural MP4 audio-track test: %s\\n", argv[3]);

done:
    if (size_h) MP4DisposeHandle(size_h);
    if (duration_h) MP4DisposeHandle(duration_h);
    if (sample_h) MP4DisposeHandle(sample_h);
    if (entry_h) MP4DisposeHandle(entry_h);
    if (dsi_h) MP4DisposeHandle(dsi_h);
    if (movie) MP4DisposeMovie(movie);
    free(dsi); free(asc); free(au);
    return err == MP4NoErr ? 0 : 1;
}
