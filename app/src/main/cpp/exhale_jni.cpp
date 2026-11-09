#include <jni.h>
#include <cstdint>
#include <vector>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <cstdlib>
#include <climits>
#include <string>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include "exhaleDecl.h"
#include "basicMP4Writer.h"

static uint16_t readLe16(const unsigned char* p) {
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}
static uint32_t readLe32(const unsigned char* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8)
        | (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}
static bool readAt(int fd, void* data, size_t size, off_t offset) {
    auto* p = static_cast<unsigned char*>(data);
    size_t done = 0;
    while (done < size) {
        ssize_t n = pread(fd, p + done, size - done, offset + static_cast<off_t>(done));
        if (n <= 0) return false;
        done += static_cast<size_t>(n);
    }
    return true;
}
static bool writeAu(BasicMP4Writer& writer, const std::vector<unsigned char>& au,
                    unsigned bytes) {
    return bytes >= 3 && bytes <= au.size()
        && writer.addFrameAU(au.data(), bytes) == static_cast<int>(bytes);
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_xhe_audioconverter_MainActivity_nativeExhaleProbe(JNIEnv* env, jclass) {
    constexpr unsigned sampleRate = 48000, channels = 2, frameLength = 1024;
    constexpr unsigned bitrateMode = 5, indepPeriod = 50;
    std::vector<int32_t> pcm(frameLength * channels * 4, 0);
    std::vector<unsigned char> au(1536 * channels, 0), asc(108, 0);
    ExhaleEncAPI* encoder = exhaleCreate(pcm.data(), au.data(), sampleRate, channels,
        frameLength, indepPeriod, bitrateMode, true, false);
    if (!encoder) return env->NewStringUTF("Exhale: no se pudo crear el codificador");
    uint32_t ascBytes = 0;
    unsigned initResult = exhaleInitEncoder(encoder, asc.data(), &ascBytes);
    // Exhale returns 0 on success; audioConfigBytes carries the ASC byte count.
    unsigned lookaheadBytes = initResult == 0 && ascBytes > 0 ? exhaleEncodeLookahead(encoder) : 0;
    unsigned frameBytes = lookaheadBytes >= 3 ? exhaleEncodeFrame(encoder) : 0;
    exhaleDelete(encoder);
    if (initResult != 0 || ascBytes == 0 || lookaheadBytes < 3 || frameBytes < 3)
        return env->NewStringUTF("Exhale: la prueba no produjo tramas válidas");
    char message[160];
    std::snprintf(message, sizeof(message),
        "Exhale JNI OK: ASC=%u bytes, lookahead=%u bytes, trama=%u bytes",
        ascBytes, lookaheadBytes, frameBytes);
    return env->NewStringUTF(message);
}

// Silent short fixture: validates the bundled Exhale MP4 writer, not source conversion.
extern "C" JNIEXPORT jstring JNICALL
Java_com_xhe_audioconverter_MainActivity_nativeExhaleMp4Probe(
        JNIEnv* env, jclass, jstring outputPath) {
    if (!outputPath) return env->NewStringUTF("M4A: ruta de salida vacía");
    const char* chars = env->GetStringUTFChars(outputPath, nullptr);
    if (!chars) return env->NewStringUTF("M4A: ruta no disponible");
    std::string path(chars); env->ReleaseStringUTFChars(outputPath, chars);
    constexpr unsigned rate=48000, ch=2, frame=1024, mode=5, period=50, samples=2048;
    std::vector<int32_t> pcm(frame*ch*4,0);
    std::vector<unsigned char> au(1536*ch,0), asc(108,0);
    ExhaleEncAPI* enc=exhaleCreate(pcm.data(),au.data(),rate,ch,frame,period,mode,true,false);
    if(!enc) return env->NewStringUTF("M4A: exhaleCreate falló");
    uint32_t ascSize=0;
    unsigned initCode=exhaleInitEncoder(enc,asc.data(),&ascSize);
    if(initCode!=0 || ascSize<5 || ascSize>asc.size()){
        exhaleDelete(enc); char m[96];
        std::snprintf(m,sizeof(m),"M4A: ASC inválida (init=%u, bytes=%u)",initCode,ascSize);
        return env->NewStringUTF(m);
    }
    int fd=open(path.c_str(),O_CREAT|O_TRUNC|O_RDWR,0600);
    if(fd<0){exhaleDelete(enc);return env->NewStringUTF("M4A: no se pudo crear");}
    BasicMP4Writer writer; int rc=0; unsigned count=0;
    if(writer.open(fd,rate,ch,24,frame,0,period,asc.data(),ascSize)!=0) rc=-1;
    if(!rc && writer.initHeader(samples,0)<666) rc=-2;
    unsigned n=rc?0:exhaleEncodeLookahead(enc);
    if(!rc && writeAu(writer,au,n)) ++count; else if(!rc) rc=-3;
    for(unsigned i=0;!rc && i<2;++i){
        n=exhaleEncodeFrame(enc);
        if(writeAu(writer,au,n)) ++count; else rc=-4;
    }
    if(!rc && writer.finishFile(64000,64000,samples)<=0) rc=-5;
    exhaleDelete(enc); struct stat st{};
    if(fstat(fd,&st)!=0 || st.st_size<=600) rc=-6;
    close(fd);
    if(rc){unlink(path.c_str());char m[96];snprintf(m,sizeof(m),"M4A writer falló (etapa %d)",rc);return env->NewStringUTF(m);}
    int check=open(path.c_str(),O_RDONLY);std::vector<unsigned char>b(st.st_size);
    ssize_t got=check>=0?read(check,b.data(),b.size()):-1;if(check>=0)close(check);
    auto has=[&](const char*t){return std::search(b.begin(),b.end(),t,t+4)!=b.end();};
    if(got!=st.st_size||!has("ftyp")||!has("moov")||!has("mdat")||!has("mp4a")||!has("esds")){
        unlink(path.c_str());return env->NewStringUTF("M4A: falló la verificación de cajas MP4");
    }
    char msg[180];snprintf(msg,sizeof(msg),"M4A writer OK: %lld bytes, %u tramas USAC, ftyp/moov/mdat + mp4a/esds",(long long)st.st_size,count);
    return env->NewStringUTF(msg);
}

// Experimental source path: PCM16 WAV (from Android MediaCodec) -> Exhale -> M4A.
// It intentionally accepts only 44.1/48 kHz mono/stereo until resampling and
// upstream gapless priming/flush behavior have been ported and verified.
extern "C" JNIEXPORT jstring JNICALL
Java_com_xhe_audioconverter_MainActivity_nativeExhaleEncodeWav(
        JNIEnv* env, jclass, jstring inputPath, jstring outputPath, jint requestedBitrateKbps) {
    if(!inputPath||!outputPath) return env->NewStringUTF("Conversión: ruta vacía");
    const char* inChars=env->GetStringUTFChars(inputPath,nullptr);
    const char* outChars=env->GetStringUTFChars(outputPath,nullptr);
    if(!inChars||!outChars){if(inChars)env->ReleaseStringUTFChars(inputPath,inChars);if(outChars)env->ReleaseStringUTFChars(outputPath,outChars);return env->NewStringUTF("Conversión: no se pudieron leer las rutas");}
    std::string inPath(inChars),outPath(outChars);
    env->ReleaseStringUTFChars(inputPath,inChars);env->ReleaseStringUTFChars(outputPath,outChars);
    int inFd=open(inPath.c_str(),O_RDONLY);
    if(inFd<0)return env->NewStringUTF("Conversión: no se pudo abrir el WAV temporal");
    struct stat inputStat{}; unsigned char header[12];
    if(fstat(inFd,&inputStat)!=0||inputStat.st_size<44||!readAt(inFd,header,12,0)
       ||memcmp(header,"RIFF",4)||memcmp(header+8,"WAVE",4)){
        close(inFd);return env->NewStringUTF("Conversión: WAV RIFF no válido");
    }
    uint32_t rate=0,dataBytes=0;uint16_t channels=0,bits=0,format=0;
    off_t dataOffset=12;bool haveFmt=false,haveData=false;
    while(dataOffset+8<=inputStat.st_size){
        unsigned char chunk[8];if(!readAt(inFd,chunk,8,dataOffset))break;
        uint32_t size=readLe32(chunk+4);off_t payload=dataOffset+8;
        if(payload+size>inputStat.st_size)break;
        if(!memcmp(chunk,"fmt ",4)&&size>=16){
            unsigned char fmt[16];if(!readAt(inFd,fmt,16,payload))break;
            format=readLe16(fmt);channels=readLe16(fmt+2);rate=readLe32(fmt+4);bits=readLe16(fmt+14);haveFmt=true;
        }else if(!memcmp(chunk,"data",4)){dataOffset=payload;dataBytes=size;haveData=true;break;}
        dataOffset=payload+size+(size&1);
    }
    if(!haveFmt||!haveData||format!=1||bits!=16||(channels!=1&&channels!=2)
       ||(rate!=44100&&rate!=48000)||dataBytes<static_cast<uint32_t>(channels*2*1024)){
        close(inFd);return env->NewStringUTF("Conversión: se requiere WAV PCM16 mono/estéreo a 44.1 o 48 kHz y al menos 768 muestras");
    }
    const unsigned frame=1024;
    if(requestedBitrateKbps<8||requestedBitrateKbps>512){close(inFd);return env->NewStringUTF("Conversión: bitrate solicitado fuera del rango de 8 a 512 kbps");}
    unsigned mode=0;
    unsigned nominalBitrate=channels*24;
    unsigned bestDifference=UINT_MAX;
    for(unsigned candidate=0;candidate<=9;++candidate){
        const unsigned candidateBitrate=channels*(24+candidate*8);
        const unsigned difference=static_cast<unsigned>(std::abs(static_cast<int>(candidateBitrate)-requestedBitrateKbps));
        if(difference<bestDifference){bestDifference=difference;mode=candidate;nominalBitrate=candidateBitrate;}
    }
    const unsigned period=(rate<48000?(rate-320u):(50u<<10u))/frame;
    const unsigned bytesPerFrame=channels*2;
    const uint64_t totalSamples=dataBytes/bytesPerFrame;
    dataBytes=static_cast<uint32_t>(totalSamples*bytesPerFrame);
    std::vector<int32_t> pcm(frame*channels*4,0);
    std::vector<unsigned char> raw(frame*bytesPerFrame),au(1536*channels,0),asc(108,0);
    auto loadFrame=[&](uint64_t sampleStart)->bool{
        std::fill(pcm.begin(),pcm.end(),0);
        uint64_t remain=sampleStart<totalSamples?totalSamples-sampleStart:0;
        unsigned take=static_cast<unsigned>(std::min<uint64_t>(frame,remain));
        if(!take)return false;
        if(!readAt(inFd,raw.data(),take*bytesPerFrame,dataOffset+sampleStart*bytesPerFrame))return false;
        for(unsigned i=0;i<take*channels;++i){
            int16_t v=static_cast<int16_t>(readLe16(raw.data()+i*2));
            pcm[i]=static_cast<int32_t>(v)*256;
        }
        return true;
    };
    if(!loadFrame(0)){close(inFd);return env->NewStringUTF("Conversión: no se pudo leer el primer bloque PCM");}
    ExhaleEncAPI* enc=exhaleCreate(pcm.data(),au.data(),rate,channels,frame,period,mode,true,false);
    if(!enc){close(inFd);return env->NewStringUTF("Conversión: exhaleCreate falló");}
    uint32_t ascSize=0;
    unsigned initCode=exhaleInitEncoder(enc,asc.data(),&ascSize);
    if(initCode!=0||ascSize<5||ascSize>asc.size()){
        exhaleDelete(enc);close(inFd);
        char detail[160];
        std::snprintf(detail,sizeof(detail),
            "Conversión: Exhale rechazó la configuración (código %u, ASC %u bytes, %u Hz, %u canales, frame %u, modo %u)",
            initCode,ascSize,rate,channels,frame,mode);
        return env->NewStringUTF(detail);
    }
    int outFd=open(outPath.c_str(),O_CREAT|O_TRUNC|O_RDWR,0600);
    if(outFd<0){exhaleDelete(enc);close(inFd);return env->NewStringUTF("Conversión: no se pudo crear el M4A");}
    BasicMP4Writer writer;int rc=0;uint64_t written=0;unsigned frames=0;
    // The encoder's 25/16 look-ahead is retained; this initial experimental
    // writer uses the tested writer API but is not yet a gapless-finalized path.
    if(writer.open(outFd,rate,channels,24,frame,0,period,asc.data(),ascSize,0,static_cast<char>(mode))!=0)rc=-1;
    if(!rc&&writer.initHeader(static_cast<uint32_t>(std::min<uint64_t>(totalSamples,0xFFFFFFFFu)),0)<666)rc=-2;
    unsigned n=0,previousAuBytes=0,maxSmoothedAuBytes=0;
    uint64_t byteCount=0;
    auto writeEncodedFrame=[&](unsigned stage)->bool{
        const unsigned bytes=exhaleEncodeFrame(enc);
        if(bytes<3||bytes>au.size()||writer.addFrameAU(au.data(),bytes)!=static_cast<int>(bytes)){rc=stage;return false;}
        const unsigned smoothed=(previousAuBytes==0?bytes:(previousAuBytes+bytes)>>1);
        maxSmoothedAuBytes=std::max(maxSmoothedAuBytes,smoothed);
        previousAuBytes=bytes;byteCount+=bytes;++frames;return true;
    };
    if(!rc){n=exhaleEncodeLookahead(enc);if(n<3||n>au.size())rc=-3;}
    // The look-ahead call primes Exhale. Subsequent real PCM blocks are
    // supplied to encodeFrame; the last block is submitted once more to drain.
    for(uint64_t pos=frame;!rc&&pos<totalSamples;pos+=frame){
        if(!loadFrame(pos)){rc=-4;break;}
        if(!writeEncodedFrame(-5))break;
    }
    if(!rc){
        if(!writeEncodedFrame(-6)){}
    }
    const uint64_t actualLength=totalSamples;
    const uint64_t avgRate=(actualLength>0?((actualLength>>1)+8*(byteCount+4ULL*frames)*rate)/actualLength:0);
    const uint64_t maxRate=((frame>>1)+8ULL*(maxSmoothedAuBytes+4)*rate)/frame;
    if(!rc&&writer.finishFile(static_cast<unsigned>(std::min<uint64_t>(avgRate,UINT_MAX)),
                              static_cast<unsigned>(std::min<uint64_t>(maxRate,UINT_MAX)),
                              static_cast<uint32_t>(std::min<uint64_t>(totalSamples,0xFFFFFFFFu)))<=0)rc=-7;
    exhaleDelete(enc);close(inFd);struct stat outStat{};
    if(fstat(outFd,&outStat)!=0||outStat.st_size<=600)rc=-7;
    close(outFd);
    if(rc){unlink(outPath.c_str());char msg[128];snprintf(msg,sizeof(msg),"Codificación experimental falló (etapa %d)",rc);return env->NewStringUTF(msg);}
    char msg[320];snprintf(msg,sizeof(msg),
        "M4A experimental generado: %lld bytes, %u tramas USAC, modo %u (nominal %u kbps), promedio medido %llu kbps, pico estimado %llu kbps. Duración declarada verificada; retardo, alineación y calidad aún pendientes.",
        (long long)outStat.st_size,frames,mode,nominalBitrate,
        static_cast<unsigned long long>((avgRate+500)/1000),
        static_cast<unsigned long long>((maxRate+500)/1000));
    return env->NewStringUTF(msg);
}
