#pragma once

#include <Arduino.h>

// Audio output wrapper for MAX98357A I2S amplifier.
// Plays MP3 audio from a memory buffer.
//
// The MP3 decode loop runs on its OWN FreeRTOS task (see audio_output.cpp),
// pinned to the Arduino core at a priority ABOVE loopTask. i2s_write() blocks
// when the DMA ring is full, yielding the CPU back to loopTask for the (~40 ms,
// blocking) TFT face redraw; when DMA space frees, the higher-priority audio
// task preempts the redraw and refills the ring. The two interleave so the
// speaker never underruns — that periodic underrun was the source cracking.
class SpeakerOutput {
public:
    SpeakerOutput();
    ~SpeakerOutput();

    // Initialize I2S output and start the background decode task.
    void begin();

    // Play MP3 from buffer (takes ownership of buffer, frees it after playback).
    void playFromBuffer(const uint8_t* mp3Data, size_t length);

    // Check if currently playing (lock-free; safe to poll from the main loop).
    bool isPlaying() const;

    // Stop playback and free the buffer. Blocks until the decode task is idle.
    void stop();

    // No-op now that the decoder runs on its own task. Kept for source
    // compatibility with callers that used to pump the decoder from loop().
    void loop();

private:
    bool _initialized;
    volatile bool _playing;      // true from playFromBuffer() until stream end/stop
    volatile bool _stopReq;      // main -> task: tear down at the next frame boundary

    const uint8_t* _mp3Buffer;   // MP3 data buffer (owned, freed on teardown)
    size_t _mp3Length;

    void* _taskHandle;           // TaskHandle_t (kept void* so the header stays lib-free)
    void* _mutex;                // SemaphoreHandle_t guarding decoder setup/teardown

    static void audioTaskThunk(void* arg);
    void audioTaskRun();
    void teardownLocked();       // free decoder chain + buffer; call with _mutex held
};
