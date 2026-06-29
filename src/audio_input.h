#pragma once

#include <Arduino.h>
#include <driver/i2s.h>

// ════════════════════════════════════════════════════════════════
//  INMP441 I2S Microphone — clean rewrite
//
//  Captures mono 16 kHz / 16-bit PCM into a PSRAM buffer and wraps it
//  in a RIFF/WAV container for upload to the backend STT endpoint.
//
//  Wiring (see config.h):
//    INMP441 SCK -> GPIO 8   (BCLK)
//    INMP441 WS  -> GPIO 15  (LRCL)
//    INMP441 SD  -> GPIO 18  (data out of mic -> data in of ESP32)
//    INMP441 L/R -> GND      (left channel)
//    INMP441 VDD -> 3.3V,  GND -> GND
//
//  Why the previous version recorded static:
//    1. It used I2S_COMM_FORMAT_I2S_MSB. The INMP441 is standard Philips
//       I2S -> use I2S_COMM_FORMAT_STAND_I2S, otherwise bits are
//       mis-aligned and you get noise.
//    2. The 32->16 bit conversion shift was arbitrary. The INMP441 puts
//       24-bit data left-justified in a 32-bit slot, so the correct
//       extraction is (raw >> 8) then scale, with a DC blocker.
//    3. Channel slot can be flipped on the S3 — toggle MIC_LEFT_CHANNEL
//       below if you capture silence.
// ════════════════════════════════════════════════════════════════

// INMP441 L/R -> GND => the mic drives the LEFT slot = ONLY_LEFT (=1).
// Verified empirically on THIS board: ch=1 carries the mic (its noise floor is
// present), ch=0 reads dead zeros (peak=0 rms=0). So this MUST stay 1.
#define MIC_LEFT_CHANNEL  1

// Extra digital gain in bits applied on top of the 24->16 down-shift.
// Each +1 doubles the level (+6 dB). NOTE: the old value of 9 was tuned while
// we were (wrongly) reading the empty slot's noise floor. On the correct slot
// (MIC_LEFT_CHANNEL 0) real speech is ~512x louder, so 9 would clip hard.
// At conversational level/distance ~5-6 puts speech near 30-55% peak.
// Tune with the RMS self-test in [AudioIn] stats: raise to 7 if too quiet,
// drop to 4 if peak pins at 32767 (clipping). With a healthy mic, TAPPING the
// mic body or talking right against it must spike peak into the thousands+;
// if it stays at the noise floor, the mic element is blocked/dead.
#define MIC_GAIN_BITS     6

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

    // Dump raw 16-bit samples to Serial (debug only).
    void setSerialDump(bool enable) { _serialDump = enable; }

private:
    bool      _initialized;
    bool      _recording;
    bool      _serialDump;

    uint8_t*  _pcmBuffer;       // 16-bit PCM, PSRAM
    size_t    _recordedBytes;
    size_t    _maxBytes;

    i2s_port_t _i2sPort;

    // DC blocker (first-order high-pass) state
    int32_t   _dcPrevIn;
    int32_t   _dcPrevOut;

    // Convert one 32-bit INMP441 word to a 16-bit PCM sample (+DC blocker)
    inline int16_t convertSample(int32_t raw);
};
