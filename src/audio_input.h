#pragma once

#include <Arduino.h>
#include <driver/i2s.h>

// ════════════════════════════════════════════════════════════════
//  INMP441 I2S Microphone
//
//  Captures mono 16 kHz / 16-bit PCM into a PSRAM buffer and wraps it
//  in a RIFF/WAV container for upload to the backend STT endpoint.
//
//  KEY: the mic is read at 16-BIT, not 32-bit. In 16-bit mode the ESP32
//  I2S captures the top 16 bits of the INMP441's 24-bit frame — already
//  MSB-aligned and correctly scaled — so samples are used AS-IS, with no
//  shift / gain / DC-blocker math. (This matches a known-working INMP441
//  project. The previous 32-bit + manual-shift path only ever produced a
//  flat noise floor that didn't track sound.)
//
//  Wiring (see config.h): SCK->MIC_SCK_PIN, WS->MIC_WS_PIN,
//  SD->MIC_SD_PIN, L/R->GND (left channel), VDD->3.3V, GND->GND.
// ════════════════════════════════════════════════════════════════

// Which I2S slot to read. Pair with the mic's L/R pin:
//   L/R -> GND  => mic drives LEFT  -> use 1 (ONLY_LEFT)
//   L/R -> 3.3V => mic drives RIGHT -> use 0 (ONLY_RIGHT)
#define MIC_LEFT_CHANNEL  1

// Optional software gain: a simple integer multiply (clamped). 1 = none
// (matches the reference). Raise to 2 or 4 only if recordings are too quiet.
#define MIC_GAIN          1

class AudioInput {
public:
    AudioInput();
    ~AudioInput();

    // Install + start the I2S RX driver. Returns false on failure.
    bool begin();

    // Recording control
    void startRecording();
    void stopRecording();

    // Pump I2S data into the PCM buffer; call frequently while recording.
    void loop();

    // State
    bool   isRecording()   const { return _recording; }
    bool   isInitialized() const { return _initialized; }
    size_t getRecordedBytes() const { return _recordedBytes; }

    // Raw PCM (no header). Valid until next startRecording().
    const uint8_t* getPcmData() const { return _pcmBuffer; }

    // Build a WAV (44-byte header + PCM). Returns a NEW buffer the caller
    // MUST free() with free(). *outSize gets the total size.
    uint8_t* getWavData(size_t* outSize);

    // Kept for interface compatibility (getWavData buffer is caller-owned).
    void freeWavData() {}

    // Print peak/RMS of the last recording so you can confirm real audio.
    void printStats();

private:
    bool      _initialized;
    bool      _recording;

    uint8_t*  _pcmBuffer;       // 16-bit PCM, PSRAM
    size_t    _recordedBytes;
    size_t    _maxBytes;

    i2s_port_t _i2sPort;
};
