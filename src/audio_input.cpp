#include "audio_input.h"
#include "config.h"
#include <cstring>

// ─── WAV header (packed, 44 bytes) ───────────────────────
struct __attribute__((packed)) WAVHeader {
    char     riff[4];        // "RIFF"
    uint32_t fileSize;       // total - 8
    char     wave[4];        // "WAVE"
    char     fmt[4];         // "fmt "
    uint32_t fmtSize;        // 16
    uint16_t audioFormat;    // 1 = PCM
    uint16_t numChannels;    // 1
    uint32_t sampleRate;     // 16000
    uint32_t byteRate;       // sampleRate * blockAlign
    uint16_t blockAlign;     // numChannels * bytesPerSample
    uint16_t bitsPerSample;  // 16
    char     data[4];        // "data"
    uint32_t dataSize;       // PCM bytes
};
static_assert(sizeof(WAVHeader) == 44, "WAVHeader must be 44 bytes");

AudioInput::AudioInput()
    : _initialized(false)
    , _recording(false)
    , _pcmBuffer(nullptr)
    , _recordedBytes(0)
    , _maxBytes(0)
    , _i2sPort(MIC_I2S_PORT)
{
}

AudioInput::~AudioInput() {
    if (_initialized) {
        i2s_driver_uninstall(_i2sPort);
    }
    if (_pcmBuffer) {
        free(_pcmBuffer);
    }
}

bool AudioInput::begin() {
    if (_initialized) return true;

    Serial.println("[AudioIn] Initializing INMP441 (I2S0)...");

    // Allocate PCM capture buffer (prefer PSRAM)
    _maxBytes = PCM_BUFFER_SIZE;
    _pcmBuffer = psramFound() ? (uint8_t*)ps_malloc(_maxBytes)
                              : (uint8_t*)malloc(_maxBytes);
    if (!_pcmBuffer) {
        Serial.printf("[AudioIn] ERROR: cannot allocate %u-byte buffer\n", (unsigned)_maxBytes);
        return false;
    }
    Serial.printf("[AudioIn] PCM buffer: %u bytes in %s\n",
                  (unsigned)_maxBytes, psramFound() ? "PSRAM" : "RAM");

    // I2S RX config. 16-bit (not 32) so the peripheral hands us the top 16
    // bits of the INMP441's 24-bit frame, ready to use with no shifting.
    i2s_config_t cfg = {};
    cfg.mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX);
    cfg.sample_rate          = AUDIO_SAMPLE_RATE;
    cfg.bits_per_sample      = I2S_BITS_PER_SAMPLE_16BIT;
#if MIC_LEFT_CHANNEL
    cfg.channel_format       = I2S_CHANNEL_FMT_ONLY_LEFT;
#else
    cfg.channel_format       = I2S_CHANNEL_FMT_ONLY_RIGHT;
#endif
    cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    cfg.intr_alloc_flags     = ESP_INTR_FLAG_LEVEL1;
    cfg.dma_buf_count        = 8;
    cfg.dma_buf_len          = 1024;   // 8 x 1024 @ 16kHz = ~512 ms headroom
    cfg.use_apll             = false;
    cfg.tx_desc_auto_clear   = false;
    cfg.fixed_mclk           = 0;

    i2s_pin_config_t pins = {};
    pins.mck_io_num   = I2S_PIN_NO_CHANGE;
    pins.bck_io_num   = MIC_SCK_PIN;
    pins.ws_io_num    = MIC_WS_PIN;
    pins.data_out_num = I2S_PIN_NO_CHANGE;
    pins.data_in_num  = MIC_SD_PIN;

    esp_err_t err = i2s_driver_install(_i2sPort, &cfg, 0, NULL);
    if (err != ESP_OK) {
        Serial.printf("[AudioIn] ERROR: i2s_driver_install failed (%d)\n", err);
        free(_pcmBuffer); _pcmBuffer = nullptr;
        return false;
    }
    err = i2s_set_pin(_i2sPort, &pins);
    if (err != ESP_OK) {
        Serial.printf("[AudioIn] ERROR: i2s_set_pin failed (%d)\n", err);
        i2s_driver_uninstall(_i2sPort);
        free(_pcmBuffer); _pcmBuffer = nullptr;
        return false;
    }

    i2s_zero_dma_buffer(_i2sPort);

    Serial.printf("[AudioIn] Ready: SCK=%d WS=%d SD=%d, %d Hz mono 16-bit, slot=%s\n",
                  MIC_SCK_PIN, MIC_WS_PIN, MIC_SD_PIN, AUDIO_SAMPLE_RATE,
                  MIC_LEFT_CHANNEL ? "LEFT" : "RIGHT");
    _initialized = true;
    return true;
}

void AudioInput::startRecording() {
    if (!_initialized) return;
    _recordedBytes = 0;
    i2s_zero_dma_buffer(_i2sPort);   // drop stale samples / startup pop
    _recording = true;
    Serial.println("[AudioIn] >>> recording");
}

void AudioInput::stopRecording() {
    if (!_recording) return;
    _recording = false;
    Serial.printf("[AudioIn] <<< stopped: %u bytes (%.2fs)\n",
                  (unsigned)_recordedBytes,
                  _recordedBytes / (float)(AUDIO_SAMPLE_RATE * 2));
    printStats();
}

void AudioInput::loop() {
    if (!_initialized || !_recording) return;

    // Drain the I2S RX DMA on every call (0-tick timeout). This keeps a slow
    // TFT face redraw between calls from letting the ring overflow. Samples
    // are read straight into the PCM buffer as 16-bit — no conversion.
    for (;;) {
        if (_recordedBytes >= _maxBytes) {       // buffer full -> auto-stop
            Serial.println("[AudioIn] buffer full");
            _recording = false;
            return;
        }

        size_t room   = _maxBytes - _recordedBytes;
        size_t toRead  = room < 2048 ? room : 2048;
        size_t bytesRead = 0;
        esp_err_t err = i2s_read(_i2sPort, _pcmBuffer + _recordedBytes,
                                 toRead, &bytesRead, 0);
        if (err != ESP_OK || bytesRead == 0) return;   // ring empty -> done

#if MIC_GAIN > 1
        int16_t* s = (int16_t*)(_pcmBuffer + _recordedBytes);
        size_t n = bytesRead / 2;
        for (size_t i = 0; i < n; i++) {
            int32_t v = (int32_t)s[i] * MIC_GAIN;
            if (v >  32767) v =  32767;
            if (v < -32768) v = -32768;
            s[i] = (int16_t)v;
        }
#endif
        _recordedBytes += bytesRead;

        if (bytesRead < toRead) return;   // partial read -> ring drained
    }
}

void AudioInput::printStats() {
    if (_recordedBytes == 0) { Serial.println("[AudioIn] stats: no data"); return; }
    const int16_t* s = (const int16_t*)_pcmBuffer;
    size_t n = _recordedBytes / 2;
    int32_t peak = 0;
    uint64_t sumSq = 0;
    for (size_t i = 0; i < n; i++) {
        int32_t v = s[i];
        if (v < 0) v = -v;
        if (v > peak) peak = v;
        sumSq += (uint64_t)((int32_t)s[i] * (int32_t)s[i]);
    }
    double rms = sqrt((double)sumSq / (double)n);
    Serial.printf("[AudioIn] stats: peak=%d rms=%.0f (%.0f%% full scale)\n",
                  (int)peak, rms, (rms / 32768.0) * 100.0);
    if (peak < 200) {
        Serial.println("[AudioIn] WARNING: very low level — check wiring / mic");
    }
}

uint8_t* AudioInput::getWavData(size_t* outSize) {
    if (_recordedBytes == 0) { if (outSize) *outSize = 0; return nullptr; }

    size_t wavSize = sizeof(WAVHeader) + _recordedBytes;
    uint8_t* wav = psramFound() ? (uint8_t*)ps_malloc(wavSize)
                                : (uint8_t*)malloc(wavSize);
    if (!wav) { if (outSize) *outSize = 0; return nullptr; }

    WAVHeader* h = (WAVHeader*)wav;
    memcpy(h->riff, "RIFF", 4);
    h->fileSize = _recordedBytes + 36;
    memcpy(h->wave, "WAVE", 4);
    memcpy(h->fmt, "fmt ", 4);
    h->fmtSize = 16;
    h->audioFormat = 1;
    h->numChannels = 1;
    h->sampleRate = AUDIO_SAMPLE_RATE;
    h->bitsPerSample = 16;
    h->blockAlign = h->numChannels * (h->bitsPerSample / 8);
    h->byteRate = h->sampleRate * h->blockAlign;
    memcpy(h->data, "data", 4);
    h->dataSize = _recordedBytes;

    memcpy(wav + sizeof(WAVHeader), _pcmBuffer, _recordedBytes);
    if (outSize) *outSize = wavSize;
    return wav;
}
