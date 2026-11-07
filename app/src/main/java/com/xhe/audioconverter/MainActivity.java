package com.xhe.audioconverter;

import android.app.Activity;
import android.content.Intent;
import android.net.Uri;
import android.os.Bundle;
import android.text.Editable;
import android.text.InputType;
import android.text.TextWatcher;
import android.view.Gravity;
import android.view.View;
import android.widget.ArrayAdapter;
import android.widget.Button;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.SeekBar;
import android.widget.ScrollView;
import android.widget.Spinner;
import android.widget.TextView;
import android.widget.Toast;

import java.util.Locale;

public class MainActivity extends Activity {
    static { System.loadLibrary("xhe_jni"); System.loadLibrary("exhale_jni"); }
    private static native String nativeEncoderVersion();
    private static native String nativeEncoderProbe(int sampleRate, int channels, int bitrate);
    private static native String nativeExhaleProbe();
    private static native String nativeExhaleMp4Probe(String outputPath);
    private static final int MIN_BITRATE_KBPS = 8;   // Provisional; validate against libxaac.
    private static final int MAX_BITRATE_KBPS = 512; // Provisional; validate against libxaac.
    private static final int PICK_AUDIO_REQUEST = 1001;

    private int bitrateKbps = 64;
    private Uri selectedAudio;
    private TextView selectedFile;
    private TextView bitrateValue;
    private SeekBar bitrateSlider;
    private EditText bitrateInput;
    private Button convertButton;
    private TextView engineStatus;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        buildInterface();
        try {
            String engine = nativeEncoderVersion();
            engineStatus.setText("Motor: " + engine
                    + "\nPrueba de codificación USAC: pendiente");
        } catch (UnsatisfiedLinkError error) {
            engineStatus.setText("No se pudo cargar libxaac");
        }
    }

    private void buildInterface() {
        int pad = dp(20);
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setPadding(pad, pad, pad, pad);
        root.setBackgroundColor(0xFFF7F7F7);

        TextView title = text("xHE Audio Converter", 24, true);
        root.addView(title);

        TextView subtitle = text("Conversor personal a M4A · xHE-AAC", 14, false);
        subtitle.setTextColor(0xFF666666);
        root.addView(subtitle, params(LinearLayout.LayoutParams.MATCH_PARENT, dp(36)));

        Button pickButton = new Button(this);
        pickButton.setText("SELECCIONAR AUDIO");
        root.addView(pickButton, params(LinearLayout.LayoutParams.MATCH_PARENT, dp(52)));
        pickButton.setOnClickListener(v -> pickAudio());

        selectedFile = text("Ningún archivo seleccionado", 14, false);
        selectedFile.setTextColor(0xFF555555);
        root.addView(selectedFile, params(LinearLayout.LayoutParams.MATCH_PARENT, dp(52)));

        Button decodeTestButton = new Button(this);
        decodeTestButton.setText("PROBAR DECODIFICACIÓN DEL AUDIO");
        decodeTestButton.setEnabled(false);
        root.addView(decodeTestButton, params(LinearLayout.LayoutParams.MATCH_PARENT, dp(48)));
        decodeTestButton.setOnClickListener(v -> {
            if (selectedAudio == null) return;
            decodeTestButton.setEnabled(false);
            engineStatus.setText("Decodificando el archivo seleccionado a PCM…");
            new Thread(() -> {
                String result;
                java.io.File wav = new java.io.File(getCacheDir(), "selected-audio-pcm-test.wav");
                try {
                    AudioPcmDecoder.Result decoded = AudioPcmDecoder.decode(
                            MainActivity.this, selectedAudio, wav);
                    double seconds = decoded.pcmBytes
                            / (double) (decoded.sampleRate * decoded.channels * 2);
                    result = String.format(Locale.US,
                            "PCM de entrada OK: %d Hz, %d canales, %.3f s, %d bytes\\nWAV temporal: %s",
                            decoded.sampleRate, decoded.channels, seconds,
                            decoded.pcmBytes, decoded.wavFile.getAbsolutePath());
                } catch (Exception error) {
                    result = "No se pudo decodificar el audio: "
                            + error.getClass().getSimpleName() + ": " + error.getMessage();
                }
                final String message = result;
                runOnUiThread(() -> {
                    engineStatus.setText(message);
                    decodeTestButton.setEnabled(selectedAudio != null);
                });
            }).start();
        });

        root.addView(section("BITRATE"));

        bitrateValue = text("64 kbps", 28, true);
        bitrateValue.setGravity(Gravity.CENTER_HORIZONTAL);
        root.addView(bitrateValue, params(LinearLayout.LayoutParams.MATCH_PARENT, dp(44)));

        bitrateSlider = new SeekBar(this);
        bitrateSlider.setMax(MAX_BITRATE_KBPS - MIN_BITRATE_KBPS);
        bitrateSlider.setProgress(bitrateKbps - MIN_BITRATE_KBPS);
        root.addView(bitrateSlider, params(LinearLayout.LayoutParams.MATCH_PARENT, dp(48)));
        bitrateSlider.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override public void onProgressChanged(SeekBar seekBar, int progress, boolean fromUser) {
                if (fromUser) setBitrate(MIN_BITRATE_KBPS + progress, true);
            }
            @Override public void onStartTrackingTouch(SeekBar seekBar) { }
            @Override public void onStopTrackingTouch(SeekBar seekBar) { }
        });

        TextView range = text("8–512 kbps (rango provisional; pendiente de validar)", 12, false);
        range.setTextColor(0xFF777777);
        root.addView(range);

        LinearLayout manualRow = new LinearLayout(this);
        manualRow.setOrientation(LinearLayout.HORIZONTAL);
        manualRow.setGravity(Gravity.CENTER_VERTICAL);
        TextView manualLabel = text("Bitrate manual:", 14, true);
        manualRow.addView(manualLabel, new LinearLayout.LayoutParams(0, dp(52), 1));
        bitrateInput = new EditText(this);
        bitrateInput.setSingleLine(true);
        bitrateInput.setInputType(InputType.TYPE_CLASS_NUMBER);
        bitrateInput.setText(String.valueOf(bitrateKbps));
        bitrateInput.setSelectAllOnFocus(true);
        manualRow.addView(bitrateInput, params(dp(112), dp(52)));
        TextView kbps = text("kbps", 14, false);
        manualRow.addView(kbps, params(dp(48), dp(52)));
        root.addView(manualRow);

        bitrateInput.addTextChangedListener(new TextWatcher() {
            @Override public void beforeTextChanged(CharSequence s, int start, int count, int after) { }
            @Override public void onTextChanged(CharSequence s, int start, int before, int count) {
                try {
                    int value = Integer.parseInt(s.toString());
                    if (value >= MIN_BITRATE_KBPS && value <= MAX_BITRATE_KBPS) {
                        setBitrate(value, true);
                    }
                } catch (NumberFormatException ignored) { }
            }
            @Override public void afterTextChanged(Editable s) { }
        });

        root.addView(section("FRECUENCIA DE MUESTREO"));
        Spinner sampleRateSpinner = new Spinner(this);
        String[] sampleRates = {"22.05 kHz", "24 kHz", "32 kHz", "44.1 kHz", "48 kHz"};
        ArrayAdapter<String> adapter = new ArrayAdapter<>(this,
                android.R.layout.simple_spinner_item, sampleRates);
        adapter.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item);
        sampleRateSpinner.setAdapter(adapter);
        sampleRateSpinner.setSelection(3);
        root.addView(sampleRateSpinner, params(LinearLayout.LayoutParams.MATCH_PARENT, dp(52)));

        TextView note = text("La conversión xHE-AAC todavía está en desarrollo. Esta versión prepara la interfaz; no crea archivos AAC falsos ni cambia extensiones.", 13, false);
        note.setTextColor(0xFF8A4B00);
        note.setPadding(0, dp(16), 0, dp(12));
        root.addView(note);

        engineStatus = text("Comprobando motor xHE-AAC…", 13, false);
        engineStatus.setTextColor(0xFF555555);
        engineStatus.setPadding(0, dp(8), 0, dp(12));
        root.addView(engineStatus);

        Button exhaleTestButton = new Button(this);
        exhaleTestButton.setText("PROBAR MOTOR EXHALE");
        root.addView(exhaleTestButton, params(LinearLayout.LayoutParams.MATCH_PARENT, dp(48)));
        exhaleTestButton.setOnClickListener(v -> {
            exhaleTestButton.setEnabled(false);
            engineStatus.setText("Ejecutando prueba nativa Exhale…");
            new Thread(() -> {
                String result;
                try { result = nativeExhaleProbe(); }
                catch (Throwable error) {
                    result = "Error al ejecutar Exhale: " + error.getClass().getSimpleName()
                            + ": " + error.getMessage();
                }
                final String message = result;
                runOnUiThread(() -> {
                    engineStatus.setText(message);
                    exhaleTestButton.setEnabled(true);
                });
            }).start();
        });

        Button mp4TestButton = new Button(this);
        mp4TestButton.setText("PROBAR ESCRITURA M4A");
        root.addView(mp4TestButton, params(LinearLayout.LayoutParams.MATCH_PARENT, dp(48)));
        mp4TestButton.setOnClickListener(v -> {
            mp4TestButton.setEnabled(false);
            engineStatus.setText("Creando M4A de prueba con el escritor oficial de Exhale…");
            new Thread(() -> {
                String result;
                java.io.File testFile = new java.io.File(getCacheDir(), "exhale-writer-test.m4a");
                try {
                    result = nativeExhaleMp4Probe(testFile.getAbsolutePath());
                    if (result.startsWith("M4A writer OK")) {
                        result += "\\n" + inspectM4aWithAndroid(testFile);
                        result += "\\nArchivo de prueba: " + testFile.getAbsolutePath();
                    }
                } catch (Throwable error) {
                    result = "Error al probar el escritor M4A: "
                            + error.getClass().getSimpleName() + ": " + error.getMessage();
                }
                final String message = result;
                runOnUiThread(() -> {
                    engineStatus.setText(message);
                    mp4TestButton.setEnabled(true);
                });
            }).start();
        });

        convertButton = new Button(this);
        convertButton.setText("CONVERTIR A M4A");
        convertButton.setEnabled(false);
        root.addView(convertButton, params(LinearLayout.LayoutParams.MATCH_PARENT, dp(54)));

        ScrollView scrollView = new ScrollView(this);
        scrollView.setFillViewport(true);
        scrollView.addView(root);
        setContentView(scrollView);
    }

    // Ask Android's own ISO-BMFF parser and codec registry to inspect the
    // generated sample. A supported decoder is not assumed merely from MP4 boxes.
    private String inspectM4aWithAndroid(java.io.File file) {
        android.media.MediaExtractor extractor = new android.media.MediaExtractor();
        try {
            extractor.setDataSource(file.getAbsolutePath());
            int audioTrack = -1;
            android.media.MediaFormat format = null;
            for (int i = 0; i < extractor.getTrackCount(); i++) {
                android.media.MediaFormat candidate = extractor.getTrackFormat(i);
                String mime = candidate.getString(android.media.MediaFormat.KEY_MIME);
                if (mime != null && mime.startsWith("audio/")) {
                    audioTrack = i;
                    format = candidate;
                    break;
                }
            }
            if (audioTrack < 0 || format == null) {
                return "Android MediaExtractor: no encontró pista de audio";
            }
            String mime = format.getString(android.media.MediaFormat.KEY_MIME);
            String details = "Android MediaExtractor: pista " + mime;
            if (format.containsKey(android.media.MediaFormat.KEY_SAMPLE_RATE)) {
                details += ", " + format.getInteger(android.media.MediaFormat.KEY_SAMPLE_RATE) + " Hz";
            }
            if (format.containsKey(android.media.MediaFormat.KEY_CHANNEL_COUNT)) {
                details += ", " + format.getInteger(android.media.MediaFormat.KEY_CHANNEL_COUNT) + " canales";
            }
            boolean decoderFound = false;
            for (android.media.MediaCodecInfo info :
                    new android.media.MediaCodecList(android.media.MediaCodecList.REGULAR_CODECS)
                            .getCodecInfos()) {
                if (!info.isEncoder()) {
                    for (String type : info.getSupportedTypes()) {
                        if (mime.equalsIgnoreCase(type)) decoderFound = true;
                    }
                }
            }
            if (!decoderFound) {
                return details + "\\nAndroid no anuncia un decodificador para " + mime + ".";
            }
            return details + "\\n" + decodeM4aTrack(extractor, audioTrack, format, mime);
        } catch (Exception error) {
            return "Android MediaExtractor/MediaCodec: "
                    + error.getClass().getSimpleName() + ": " + error.getMessage();
        } finally {
            extractor.release();
        }
    }

    // Actually configure the platform decoder and drain PCM output. This is
    // stronger than MIME capability discovery; it validates this sample only.
    private String decodeM4aTrack(android.media.MediaExtractor extractor,
                                  int trackIndex, android.media.MediaFormat format,
                                  String mime) {
        android.media.MediaCodec decoder = null;
        boolean started = false;
        long decodedBytes = 0;
        boolean inputDone = false;
        boolean outputDone = false;
        try {
            extractor.selectTrack(trackIndex);
            decoder = android.media.MediaCodec.createDecoderByType(mime);
            decoder.configure(format, null, null, 0);
            decoder.start();
            started = true;
            android.media.MediaCodec.BufferInfo info = new android.media.MediaCodec.BufferInfo();
            int iterations = 0;
            while (!outputDone && iterations++ < 10000) {
                if (!inputDone) {
                    int inputIndex = decoder.dequeueInputBuffer(10000);
                    if (inputIndex >= 0) {
                        java.nio.ByteBuffer input = decoder.getInputBuffer(inputIndex);
                        if (input == null) throw new java.io.IOException("buffer de entrada nulo");
                        input.clear();
                        int size = extractor.readSampleData(input, 0);
                        if (size < 0) {
                            decoder.queueInputBuffer(inputIndex, 0, 0, 0,
                                    android.media.MediaCodec.BUFFER_FLAG_END_OF_STREAM);
                            inputDone = true;
                        } else {
                            long pts = extractor.getSampleTime();
                            decoder.queueInputBuffer(inputIndex, 0, size,
                                    Math.max(0, pts), 0);
                            extractor.advance();
                        }
                    }
                }
                int outputIndex = decoder.dequeueOutputBuffer(info, 10000);
                if (outputIndex >= 0) {
                    if (info.size > 0) decodedBytes += info.size;
                    boolean eos = (info.flags
                            & android.media.MediaCodec.BUFFER_FLAG_END_OF_STREAM) != 0;
                    decoder.releaseOutputBuffer(outputIndex, false);
                    if (eos) outputDone = true;
                } else if (outputIndex == android.media.MediaCodec.INFO_OUTPUT_FORMAT_CHANGED) {
                    // The decoded PCM format is available from decoder.getOutputFormat().
                }
            }
            if (!outputDone) return "MediaCodec no terminó la decodificación (límite de seguridad).";
            if (decodedBytes <= 0) {
                return "MediaCodec llegó al final, pero no produjo PCM.";
            }
            android.media.MediaFormat decoded = decoder.getOutputFormat();
            String pcm = "MediaCodec decodificó " + decodedBytes + " bytes PCM";
            if (decoded.containsKey(android.media.MediaFormat.KEY_SAMPLE_RATE)) {
                pcm += ", " + decoded.getInteger(android.media.MediaFormat.KEY_SAMPLE_RATE) + " Hz";
            }
            if (decoded.containsKey(android.media.MediaFormat.KEY_CHANNEL_COUNT)) {
                pcm += ", " + decoded.getInteger(android.media.MediaFormat.KEY_CHANNEL_COUNT) + " canales";
            }
            return pcm + ".";
        } catch (Exception error) {
            return "MediaCodec no pudo decodificar esta trama USAC: "
                    + error.getClass().getSimpleName() + ": " + error.getMessage();
        } finally {
            if (decoder != null) {
                if (started) {
                    try { decoder.stop(); } catch (RuntimeException ignored) { }
                }
                decoder.release();
            }
        }
    }

    private void setBitrate(int value, boolean updateInput) {
        bitrateKbps = value;
        bitrateValue.setText(String.format(Locale.US, "%d kbps", value));
        bitrateSlider.setProgress(value - MIN_BITRATE_KBPS);
        if (updateInput && !bitrateInput.getText().toString().equals(String.valueOf(value))) {
            bitrateInput.setText(String.valueOf(value));
            bitrateInput.setSelection(bitrateInput.length());
        }
    }

    private void pickAudio() {
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType("audio/*");
        startActivityForResult(intent, PICK_AUDIO_REQUEST);
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode == PICK_AUDIO_REQUEST && resultCode == RESULT_OK && data != null) {
            selectedAudio = data.getData();
            if (selectedAudio != null) {
                getContentResolver().takePersistableUriPermission(
                        selectedAudio, Intent.FLAG_GRANT_READ_URI_PERMISSION);
                selectedFile.setText(selectedAudio.getLastPathSegment());
                for (int i = 0; i < ((LinearLayout) selectedFile.getParent()).getChildCount(); i++) {
                    View child = ((LinearLayout) selectedFile.getParent()).getChildAt(i);
                    if (child instanceof Button
                            && "PROBAR DECODIFICACIÓN DEL AUDIO".contentEquals(((Button) child).getText())) {
                        child.setEnabled(true);
                    }
                }
                Toast.makeText(this, "Audio seleccionado", Toast.LENGTH_SHORT).show();
            }
        }
    }

    private TextView section(String value) {
        TextView view = text(value, 14, true);
        view.setPadding(0, dp(18), 0, dp(4));
        return view;
    }

    private TextView text(String value, int sp, boolean bold) {
        TextView view = new TextView(this);
        view.setText(value);
        view.setTextSize(sp);
        if (bold) view.setTypeface(null, android.graphics.Typeface.BOLD);
        view.setTextColor(0xFF202124);
        return view;
    }

    private LinearLayout.LayoutParams params(int width, int height) {
        return new LinearLayout.LayoutParams(width, height);
    }

    private int dp(int value) {
        return Math.round(value * getResources().getDisplayMetrics().density);
    }
}
