#pragma once

#include <Arduino.h>
#include <driver/i2s.h>

// ════════════════════════════════════════════════════════════════
//  INMP441 I2S Microphone
//
//  Captures mono 16 kHz / 16-bit PCM into a PSRAM buffer and wraps it
//  in a RIFF/WAV container for upload to the backend STT endpoint.
//
//  Read at 32-BIT (the standard INMP441 method): the mic sends 24-bit data
//  left-justified in a 32-bit I2S word. We take (raw >> 8) as the signed
//  24-bit sample, run a DC blocker (removes the mic's DC offset and the
//  startup pop), apply MIC_GAIN_BITS of digital gain, and store 16-bit PCM.
//  (Reading in 16-bit mode mis-aligns the INMP441 frame and produces glitchy
//  full-scale spikes with no real speech — do not use it.)
//
//  Wiring (see config.h): SCK->MIC_SCK_PIN, WS->MIC_WS_PIN,
//  SD->MIC_SD_PIN, L/R->GND (left channel), VDD->3.3V, GND->GND.
// ════════════════════════════════════════════════════════════════

// Which I2S slot to read. Pair with the mic's L/R pin:
//   L/R -> GND  => mic drives LEFT  -> use 1 (ONLY_LEFT)
//   L/R -> 3.3V => mic drives RIGHT -> use 0 (ONLY_RIGHT)
#define MIC_LEFT_CHANNEL  1

// Digital gain in BITS (each +1 = 2x / +6 dB), applied during 24->16
// conversion. Tune from the [AudioIn] stats: raise if rms is only a couple %,
// lower if peak pins at 32767 (clipping). 5 keeps strong speech ~20-25% RMS
// without clipping; drop to 4 if it still pins peak.
#define MIC_GAIN_BITS     5

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

    // DC blocker state (first-order high-pass; removes INMP441 DC offset)
    int32_t   _dcPrevIn;
    int32_t   _dcPrevOut;

    // Convert one 32-bit INMP441 word -> 16-bit PCM (shift + gain + DC block)
    inline int16_t convertSample(int32_t raw);
};
