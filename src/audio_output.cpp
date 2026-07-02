#include "audio_output.h"
#include "config.h"

#include <AudioGeneratorMP3.h>
#include <AudioFileSourcePROGMEM.h>
#include <AudioFileSourceBuffer.h>
#include <AudioOutputI2S.h>

// ESP8266Audio objects (kept file-static so the header stays library-free)
static AudioGeneratorMP3*      s_mp3 = nullptr;
static AudioFileSourcePROGMEM* s_src = nullptr;
static AudioFileSourceBuffer*  s_fbuf = nullptr;
static AudioOutputI2S*         s_out = nullptr;

SpeakerOutput::SpeakerOutput()
    : _initialized(false)
    , _playing(false)
    , _mp3Buffer(nullptr)
    , _mp3Length(0)
{
}

SpeakerOutput::~SpeakerOutput() {
    stop();
    if (s_out) { delete s_out; s_out = nullptr; }
}

void SpeakerOutput::begin() {
    if (_initialized) return;

    // I2S port 1 for the speaker (port 0 is the microphone).
    // EXTERNAL_I2S (MAX98357A), dma_buf_count=32 (default is 8). OpenAI TTS MP3
    // is 24 kHz mono, so 32 x 128 samples = ~170 ms of buffered audio. That
    // comfortably covers a ~50 ms TFT face redraw between loop() calls, so the
    // ring never underflows (underflow with tx_desc_auto_clear=true is what
    // made playback stutter on/off).
    s_out = new AudioOutputI2S(1 /* I2S_NUM_1 */, AudioOutputI2S::EXTERNAL_I2S, 32);
    s_out->SetPinout(SPK_BCLK_PIN, SPK_LRC_PIN, SPK_DOUT_PIN);
    s_out->SetOutputModeMono(true);   // MAX98357A is a mono amp
    // Digital gain: 1.0 = full-scale (loudest CLEAN level). Values >1.0 clip.
    // Get extra loudness from the amp's analog GAIN pin instead (tie GAIN->GND
    // via 100k for 15dB, or straight to GND for 12dB; floating = 9dB default).
    s_out->SetGain(1.0f);             // 0.0..4.0

    _initialized = true;
    Serial.printf("[SpeakerOut] MAX98357A ready (I2S1 BCLK=%d LRC=%d DOUT=%d)\n",
                  SPK_BCLK_PIN, SPK_LRC_PIN, SPK_DOUT_PIN);
}

void SpeakerOutput::playFromBuffer(const uint8_t* mp3Data, size_t length) {
    if (!_initialized) begin();
    stop();   // tear down any previous playback + free its buffer

    if (!mp3Data || length == 0) {
        if (mp3Data) free((void*)mp3Data);
        return;
    }

    // Take ownership of the MP3 buffer; freed in stop()/on finish.
    _mp3Buffer = mp3Data;
    _mp3Length = length;

    s_src  = new AudioFileSourcePROGMEM(mp3Data, length);
    s_fbuf = new AudioFileSourceBuffer(s_src, 2048);   // smoothing buffer
    s_mp3  = new AudioGeneratorMP3();

    if (!s_mp3->begin(s_fbuf, s_out)) {
        Serial.println("[SpeakerOut] ERROR: MP3 decode failed to start");
        stop();
        return;
    }
    _playing = true;
    Serial.printf("[SpeakerOut] playing %u bytes\n", (unsigned)length);
}

bool SpeakerOutput::isPlaying() const {
    return _playing;
}

void SpeakerOutput::loop() {
    if (!_playing || !s_mp3) return;

    if (s_mp3->isRunning()) {
        if (!s_mp3->loop()) {        // returns false when the stream ends
            Serial.println("[SpeakerOut] playback finished");
            stop();                  // cleanup + free buffer
        }
    } else {
        stop();
    }
}

void SpeakerOutput::stop() {
    if (s_mp3) {
        if (s_mp3->isRunning()) s_mp3->stop();
        delete s_mp3; s_mp3 = nullptr;
    }
    if (s_fbuf) { delete s_fbuf; s_fbuf = nullptr; }
    if (s_src)  { delete s_src;  s_src  = nullptr; }
    _playing = false;
    freeBuffer();
}

void SpeakerOutput::freeBuffer() {
    if (_mp3Buffer) {
        free((void*)_mp3Buffer);
        _mp3Buffer = nullptr;
        _mp3Length = 0;
    }
}
