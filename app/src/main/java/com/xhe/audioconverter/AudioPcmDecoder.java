package com.xhe.audioconverter;

import android.content.Context;
import android.media.AudioFormat;
import android.media.MediaCodec;
import android.media.MediaExtractor;
import android.media.MediaFormat;
import android.net.Uri;

import java.io.File;
import java.io.IOException;
import java.io.RandomAccessFile;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;

/**
 * Decodes the selected audio track to a temporary, uncompressed PCM16 WAV file.
 * This is only the input stage; it does not encode AAC or create an M4A file.
 */
final class AudioPcmDecoder {
    static final class Result {
        final File wavFile;
        final int sampleRate;
        final int channels;
        final long pcmBytes;

        Result(File wavFile, int sampleRate, int channels, long pcmBytes) {
            this.wavFile = wavFile;
            this.sampleRate = sampleRate;
            this.channels = channels;
            this.pcmBytes = pcmBytes;
        }
    }

    private static final long CODEC_TIMEOUT_US = 10_000;

    private AudioPcmDecoder() { }

    static Result decode(Context context, Uri source, File destination)
            throws IOException {
        MediaExtractor extractor = new MediaExtractor();
        MediaCodec codec = null;
        RandomAccessFile wav = null;
        boolean completed = false;
        long pcmBytes = 0;

        try {
            extractor.setDataSource(context, source, null);
            int audioTrack = -1;
            MediaFormat inputFormat = null;
            for (int i = 0; i < extractor.getTrackCount(); i++) {
                MediaFormat candidate = extractor.getTrackFormat(i);
                String mime = candidate.getString(MediaFormat.KEY_MIME);
                if (mime != null && mime.startsWith("audio/")) {
                    audioTrack = i;
                    inputFormat = candidate;
                    break;
                }
            }
            if (audioTrack < 0 || inputFormat == null) {
                throw new IOException("El archivo no contiene una pista de audio");
            }

            extractor.selectTrack(audioTrack);
            String mime = inputFormat.getString(MediaFormat.KEY_MIME);
            codec = MediaCodec.createDecoderByType(mime);
            codec.configure(inputFormat, null, null, 0);
            codec.start();

            wav = new RandomAccessFile(destination, "rw");
            wav.setLength(0);
            writeWavHeader(wav, 0, 1, 1);

            MediaCodec.BufferInfo info = new MediaCodec.BufferInfo();
            boolean inputDone = false;
            boolean outputDone = false;
            int sampleRate = inputFormat.containsKey(MediaFormat.KEY_SAMPLE_RATE)
                    ? inputFormat.getInteger(MediaFormat.KEY_SAMPLE_RATE) : 0;
            int channels = inputFormat.containsKey(MediaFormat.KEY_CHANNEL_COUNT)
                    ? inputFormat.getInteger(MediaFormat.KEY_CHANNEL_COUNT) : 0;
            int pcmEncoding = AudioFormat.ENCODING_PCM_16BIT;

            while (!outputDone) {
                if (!inputDone) {
                    int inputIndex = codec.dequeueInputBuffer(CODEC_TIMEOUT_US);
                    if (inputIndex >= 0) {
                        ByteBuffer inputBuffer = codec.getInputBuffer(inputIndex);
                        if (inputBuffer == null) {
                            throw new IOException("Buffer de entrada de audio no disponible");
                        }
                        inputBuffer.clear();
                        int size = extractor.readSampleData(inputBuffer, 0);
                        if (size < 0) {
                            codec.queueInputBuffer(inputIndex, 0, 0, 0,
                                    MediaCodec.BUFFER_FLAG_END_OF_STREAM);
                            inputDone = true;
                        } else {
                            long presentationTimeUs = extractor.getSampleTime();
                            codec.queueInputBuffer(inputIndex, 0, size,
                                    Math.max(0, presentationTimeUs), 0);
                            extractor.advance();
                        }
                    }
                }

                int outputIndex = codec.dequeueOutputBuffer(info, CODEC_TIMEOUT_US);
                if (outputIndex == MediaCodec.INFO_OUTPUT_FORMAT_CHANGED) {
                    MediaFormat decoded = codec.getOutputFormat();
                    if (decoded.containsKey(MediaFormat.KEY_SAMPLE_RATE)) {
                        sampleRate = decoded.getInteger(MediaFormat.KEY_SAMPLE_RATE);
                    }
                    if (decoded.containsKey(MediaFormat.KEY_CHANNEL_COUNT)) {
                        channels = decoded.getInteger(MediaFormat.KEY_CHANNEL_COUNT);
                    }
                    if (decoded.containsKey(MediaFormat.KEY_PCM_ENCODING)) {
                        pcmEncoding = decoded.getInteger(MediaFormat.KEY_PCM_ENCODING);
                    }
                    validateFormat(sampleRate, channels, pcmEncoding);
                } else if (outputIndex >= 0) {
                    ByteBuffer output = codec.getOutputBuffer(outputIndex);
                    if (output == null && info.size > 0) {
                        throw new IOException("Buffer de salida de audio no disponible");
                    }
                    if (info.size > 0) {
                        if (sampleRate <= 0 || channels <= 0) {
                            MediaFormat decoded = codec.getOutputFormat();
                            sampleRate = decoded.getInteger(MediaFormat.KEY_SAMPLE_RATE);
                            channels = decoded.getInteger(MediaFormat.KEY_CHANNEL_COUNT);
                            if (decoded.containsKey(MediaFormat.KEY_PCM_ENCODING)) {
                                pcmEncoding = decoded.getInteger(MediaFormat.KEY_PCM_ENCODING);
                            }
                        }
                        validateFormat(sampleRate, channels, pcmEncoding);
                        output.position(info.offset);
                        output.limit(info.offset + info.size);
                        pcmBytes += writePcm16(wav, output.slice().order(ByteOrder.nativeOrder()),
                                pcmEncoding);
                    }
                    boolean eos = (info.flags & MediaCodec.BUFFER_FLAG_END_OF_STREAM) != 0;
                    codec.releaseOutputBuffer(outputIndex, false);
                    if (eos) outputDone = true;
                }
            }

            if (pcmBytes == 0) {
                throw new IOException("El decodificador no produjo audio PCM");
            }
            if (pcmBytes > 0xFFFFFFFFL - 36) {
                throw new IOException("El audio es demasiado grande para WAV estándar");
            }
            wav.seek(0);
            writeWavHeader(wav, pcmBytes, sampleRate, channels);
            wav.getFD().sync();
            completed = true;
            return new Result(destination, sampleRate, channels, pcmBytes);
        } catch (RuntimeException e) {
            throw new IOException("No se pudo decodificar el audio: " + e.getMessage(), e);
        } finally {
            if (codec != null) {
                try { codec.stop(); } catch (RuntimeException ignored) { }
                codec.release();
            }
            extractor.release();
            if (wav != null) {
                try { wav.close(); } catch (IOException ignored) { }
            }
            if (!completed && destination.exists()) {
                // Never leave a partial WAV that could be mistaken for a valid input.
                //noinspection ResultOfMethodCallIgnored
                destination.delete();
            }
        }
    }

    private static void validateFormat(int sampleRate, int channels, int encoding)
            throws IOException {
        if (sampleRate <= 0 || (channels != 1 && channels != 2)) {
            throw new IOException("Solo se admite audio mono o estéreo con frecuencia válida");
        }
        if (encoding != AudioFormat.ENCODING_PCM_16BIT
                && encoding != AudioFormat.ENCODING_PCM_FLOAT) {
            throw new IOException("Formato PCM de salida no compatible: " + encoding);
        }
    }

    private static long writePcm16(RandomAccessFile out, ByteBuffer data, int encoding)
            throws IOException {
        if (encoding == AudioFormat.ENCODING_PCM_16BIT) {
            int length = data.remaining();
            byte[] buffer = new byte[Math.min(length, 32 * 1024)];
            while (data.hasRemaining()) {
                int count = Math.min(data.remaining(), buffer.length);
                data.get(buffer, 0, count);
                out.write(buffer, 0, count);
            }
            return length;
        }

        int samples = data.remaining() / 4;
        ByteBuffer converted = ByteBuffer.allocate(Math.min(samples, 8192) * 2)
                .order(ByteOrder.LITTLE_ENDIAN);
        long written = 0;
        while (data.remaining() >= 4) {
            converted.clear();
            int batch = Math.min(data.remaining() / 4, converted.capacity() / 2);
            for (int i = 0; i < batch; i++) {
                float value = data.getFloat();
                if (Float.isNaN(value)) value = 0;
                value = Math.max(-1.0f, Math.min(1.0f, value));
                int sample = value <= -1.0f ? -32768 : Math.round(value * 32767.0f);
                converted.putShort((short) sample);
            }
            out.write(converted.array(), 0, batch * 2);
            written += batch * 2L;
        }
        if (data.hasRemaining()) {
            throw new IOException("Bloque PCM flotante incompleto");
        }
        return written;
    }

    private static void writeWavHeader(RandomAccessFile out, long pcmBytes,
                                       int sampleRate, int channels) throws IOException {
        int blockAlign = channels * 2;
        long byteRate = (long) sampleRate * blockAlign;
        out.writeBytes("RIFF");
        writeLe32(out, pcmBytes + 36);
        out.writeBytes("WAVE");
        out.writeBytes("fmt ");
        writeLe32(out, 16);
        writeLe16(out, 1);
        writeLe16(out, channels);
        writeLe32(out, sampleRate);
        writeLe32(out, byteRate);
        writeLe16(out, blockAlign);
        writeLe16(out, 16);
        out.writeBytes("data");
        writeLe32(out, pcmBytes);
    }

    private static void writeLe16(RandomAccessFile out, int value) throws IOException {
        out.write(value & 0xFF);
        out.write((value >>> 8) & 0xFF);
    }

    private static void writeLe32(RandomAccessFile out, long value) throws IOException {
        out.write((int) (value & 0xFF));
        out.write((int) ((value >>> 8) & 0xFF));
        out.write((int) ((value >>> 16) & 0xFF));
        out.write((int) ((value >>> 24) & 0xFF));
    }
}
