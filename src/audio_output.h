#pragma once

#include <Arduino.h>

// Audio output wrapper for MAX98357A I2S amplifier
// Plays MP3 audio from memory buffer
class SpeakerOutput {
public:
    SpeakerOutput();
    ~SpeakerOutput();

    // Initialize I2S output
    void begin();

    // Play MP3 from buffer (takes ownership of buffer, will free after playback)
    void playFromBuffer(const uint8_t* mp3Data, size_t length);

    // Check if currently playing
    bool isPlaying() const;

    // Stop playback and free buffer
    void stop();

    // Call periodically to pump the decoder
    void loop();

private:
    bool _initialized;
    bool _playing;
    const uint8_t* _mp3Buffer;      // MP3 data buffer (owned, will be freed)
    size_t _mp3Length;               // Length of MP3 data

    // Free the MP3 buffer
    void freeBuffer();
};
