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

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        buildInterface();
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

        convertButton = new Button(this);
        convertButton.setText("CONVERTIR A M4A");
        convertButton.setEnabled(false);
        root.addView(convertButton, params(LinearLayout.LayoutParams.MATCH_PARENT, dp(54)));

        ScrollView scrollView = new ScrollView(this);
        scrollView.setFillViewport(true);
        scrollView.addView(root);
        setContentView(scrollView);
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
