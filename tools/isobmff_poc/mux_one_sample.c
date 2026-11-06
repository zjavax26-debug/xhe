/*
 * Real libxaac USAC elementary-stream -> MPEG-4 audio track proof of concept.
 * The first dec_info_init bytes are the encoder's AudioSpecificConfig.
 * The remaining bytes are split into one MP4 sample per metadata stsz entry.
 *
 * This validates muxer wiring and sample preservation, not independent decoder
 * conformance. The caller supplies the USAC frame duration obtained for the
 * selected encoder configuration.
 */
#include "MP4Movies.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

static unsigned char *read_file(const char *path, unsigned long *size) {
    FILE *f = fopen(path, "rb"); unsigned char *data; long n;
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) || (n = ftell(f)) <= 0 || fseek(f, 0, SEEK_SET)) { fclose(f); return NULL; }
    data = (unsigned char *)malloc((size_t)n);
    if (!data || fread(data, 1, (size_t)n, f) != (size_t)n) { free(data); fclose(f); return NULL; }
    fclose(f); *size = (unsigned long)n; return data;
}
static unsigned long meta_value(const char *meta, const char *key) {
    char needle[128]; const char *p; snprintf(needle, sizeof(needle), "-%s:", key);
    p = strstr(meta, needle); if (!p) return 0; p += strlen(needle);
    return strtoul(p, NULL, 10);
}
static unsigned long *meta_sizes(const char *meta, unsigned long count) {
    unsigned long *sizes = (unsigned long *)calloc(count, sizeof(unsigned long));
    const char *p = meta;
    for (unsigned long i=0; i<count; i++) {
        p = strstr(p, "-ia_mp4_stsz_size:"); if (!p) { free(sizes); return NULL; }
        p += strlen("-ia_mp4_stsz_size:"); sizes[i] = strtoul(p, (char **)&p, 10);
        if (!sizes[i]) { free(sizes); return NULL; }
    }
    return sizes;
}
#define CHECK(call) do { err=(call); if(err!=MP4NoErr){fprintf(stderr,#call " failed: %d\\n",err);goto done;} } while(0)
int main(int argc, char **argv) {
    unsigned long raw_len=0, meta_len=0, init, count, rate, play, bitrate, frame_duration;
    unsigned char *raw=NULL, *meta_bytes=NULL, *dsi=NULL;
    unsigned long *sizes=NULL, offset=0;
    char *meta=NULL;
    MP4Movie movie=NULL; MP4Track track=NULL; MP4Media media=NULL;
    MP4Handle dsi_h=NULL, entry_h=NULL, sample_h=NULL, durations_h=NULL, sizes_h=NULL;
    MP4Err err=MP4NoErr;
    if(argc!=7){fprintf(stderr,"usage: %s stream.usac metadata.txt output.m4a bitrate frame_duration_samples reserved\\n",argv[0]);return 2;}
    bitrate=strtoul(argv[4],NULL,10); frame_duration=strtoul(argv[5],NULL,10);
    raw=read_file(argv[1],&raw_len); meta_bytes=read_file(argv[2],&meta_len);
    if(!raw||!meta_bytes||!bitrate||!frame_duration){err=MP4BadParamErr;goto done;}
    meta=(char*)malloc(meta_len+1); if(!meta){err=MP4NoMemoryErr;goto done;}
    memcpy(meta,meta_bytes,meta_len);meta[meta_len]=0;
    init=meta_value(meta,"dec_info_init");count=meta_value(meta,"ia_mp4_stsz_entries");
    rate=meta_value(meta,"media_time_scale");play=meta_value(meta,"playTimeInSamples");
    if(!init||init>127||!count||!rate||!play||count>10000||init>=raw_len){err=MP4BadParamErr;goto done;}
    sizes=meta_sizes(meta,count); if(!sizes){err=MP4BadParamErr;goto done;}
    unsigned long payload=0; for(unsigned long i=0;i<count;i++) payload+=sizes[i];
    if(init+payload!=raw_len){fprintf(stderr,"metadata does not cover stream bytes\\n");err=MP4BadParamErr;goto done;}

    /* DecoderSpecificInfo descriptor: tag 0x05 + variable-length descriptor length + ASC. */
    unsigned int len_bytes=(init<128)?1:(init<16384?2:3), dsi_len=(unsigned int)init+1+len_bytes;
    dsi=(unsigned char*)malloc(dsi_len); if(!dsi){err=MP4NoMemoryErr;goto done;}
    dsi[0]=0x05;
    if(len_bytes==1)dsi[1]=(unsigned char)init;
    else if(len_bytes==2){dsi[1]=(unsigned char)(0x80|(init>>7));dsi[2]=(unsigned char)(init&0x7f);}
    else {dsi[1]=(unsigned char)(0x80|(init>>14));dsi[2]=(unsigned char)(0x80|((init>>7)&0x7f));dsi[3]=(unsigned char)(init&0x7f);}
    memcpy(dsi+1+len_bytes,raw,init);

    CHECK(MP4NewMovie(&movie,1,0xff,0xff,0xff,0xff,0xff));
    CHECK(MP4NewMovieTrack(movie,MP4NewTrackIsAudio,&track));
    CHECK(MP4AddTrackToMovieIOD(track));
    CHECK(MP4NewTrackMedia(track,&media,MP4AudioHandlerType,(u32)rate,NULL));
    CHECK(MP4SetMediaLanguage(media,"und"));
    CHECK(MP4BeginMediaEdits(media));
    CHECK(MP4NewHandle(dsi_len,&dsi_h)); memcpy(*dsi_h,dsi,dsi_len);
    CHECK(MP4NewHandle(0,&entry_h));
    CHECK(MP4NewSampleDescription(track,entry_h,1,0x40,0x05,2U*6144U,(u32)bitrate,(u32)bitrate,dsi_h));

    CHECK(MP4NewHandle(payload,&sample_h));
    CHECK(MP4NewHandle(count*sizeof(u32),&durations_h));
    CHECK(MP4NewHandle(count*sizeof(u32),&sizes_h));
    unsigned char *dst=(unsigned char*)*sample_h;
    u32 *dur=(u32*)*durations_h, *sz=(u32*)*sizes_h;
    offset=init;
    for(unsigned long i=0;i<count;i++){
        memcpy(dst,raw+offset,sizes[i]); dst+=sizes[i]; offset+=sizes[i];
        dur[i]=(u32)frame_duration; sz[i]=(u32)sizes[i];
    }
    CHECK(MP4AddMediaSamples(media,sample_h,(u32)count,durations_h,sizes_h,entry_h,NULL,NULL));
    CHECK(MP4EndMediaEdits(media));
    {
        u64 media_duration=0;
        CHECK(MP4GetMediaDuration(media,&media_duration));
        /* Clip encoder delay/padding to the exact playable sample count in libxaac metadata. */
        CHECK(MP4InsertMediaIntoTrack(track,0,0,(u64)play,1));
    }
    CHECK(MP4WriteMovieToFile(movie,argv[3]));
    printf("Muxed real USAC bytes: ASC=%lu, samples=%lu, payload=%lu, rate=%lu, playable=%lu, frame_duration=%lu\\n",init,count,payload,rate,play,frame_duration);
done:
    if(sizes_h)MP4DisposeHandle(sizes_h); if(durations_h)MP4DisposeHandle(durations_h);
    if(sample_h)MP4DisposeHandle(sample_h); if(entry_h)MP4DisposeHandle(entry_h);
    if(dsi_h)MP4DisposeHandle(dsi_h); if(movie)MP4DisposeMovie(movie);
    free(sizes);free(dsi);free(meta);free(meta_bytes);free(raw);
    return err==MP4NoErr?0:1;
}
