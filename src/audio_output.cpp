#include "audio_output.h"
#include "config.h"

#include <AudioGeneratorMP3.h>
#include <AudioFileSourcePROGMEM.h>
#include <AudioFileSourceBuffer.h>
#include <AudioOutputI2S.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

// ESP8266Audio objects (kept file-static so the header stays library-free).
// Owned by the decode task while playing; only touched under _mutex.
static AudioGeneratorMP3*      s_mp3  = nullptr;
static AudioFileSourcePROGMEM* s_src  = nullptr;
static AudioFileSourceBuffer*  s_fbuf = nullptr;
static AudioOutputI2S*         s_out  = nullptr;

// The decode task lives on the Arduino core (core 1) at a priority ABOVE
// loopTask (which runs at priority 1). That's the whole fix: when the ~40 ms
// TFT face redraw is blocking loopTask, this task still gets scheduled the
// moment the I2S DMA needs more samples, so the ring never drains empty.
// The task self-paces because s_mp3->loop() blocks inside i2s_write() whenever
// the DMA ring is full, which hands the CPU back to loopTask for drawing.
#define AUDIO_TASK_CORE   1
#define AUDIO_TASK_PRIO   2
#define AUDIO_TASK_STACK  8192   // libhelix MP3 decode; bump if the task stack-overflows

SpeakerOutput::SpeakerOutput()
    : _initialized(false)
    , _playing(false)
    , _stopReq(false)
    , _mp3Buffer(nullptr)
    , _mp3Length(0)
    , _taskHandle(nullptr)
    , _mutex(nullptr)
{
}

SpeakerOutput::~SpeakerOutput() {
    stop();
    if (_taskHandle) { vTaskDelete((TaskHandle_t)_taskHandle); _taskHandle = nullptr; }
    if (s_out) { delete s_out; s_out = nullptr; }
    if (_mutex) { vSemaphoreDelete((SemaphoreHandle_t)_mutex); _mutex = nullptr; }
}

void SpeakerOutput::begin() {
    if (_initialized) return;

    // I2S port 1 for the speaker (port 0 is the microphone).
    // EXTERNAL_I2S (MAX98357A), dma_buf_count=32 (default is 8). OpenAI TTS MP3
    // is 24 kHz mono, so 32 x 128 samples = ~170 ms of buffered audio.
    s_out = new AudioOutputI2S(1 /* I2S_NUM_1 */, AudioOutputI2S::EXTERNAL_I2S, 32);
    s_out->SetPinout(SPK_BCLK_PIN, SPK_LRC_PIN, SPK_DOUT_PIN);
    s_out->SetOutputModeMono(true);   // MAX98357A is a mono amp
    // Digital gain: 1.0 = full-scale (loudest CLEAN level). Values >1.0 clip.
    // Get extra loudness from the amp's analog GAIN pin instead.
    s_out->SetGain(1.0f);             // 0.0..4.0

    // ── Warm up the amp at boot to kill the first-PTT "pop" ──
    // The MAX98357A's SD pin is tied to VIN (always enabled), so we can't mute
    // it. It emits a DC "pop" the first time its I2S clock starts from cold.
    // Start I2S1 now and clock ~120 ms of silence so that one-time cold-start
    // transient happens here during boot, not at the user's first playback.
    // The i2sOn guard inside AudioOutputI2S::begin() makes the later
    // s_mp3->begin() re-begin harmless.
    s_out->begin();
    int16_t silence[2] = {0, 0};
    for (int i = 0; i < 5300; i++) s_out->ConsumeSample(silence);  // ~120 ms @ 44.1 kHz

    _mutex = xSemaphoreCreateMutex();

    TaskHandle_t h = nullptr;
    xTaskCreatePinnedToCore(audioTaskThunk, "audioOut", AUDIO_TASK_STACK,
                            this, AUDIO_TASK_PRIO, &h, AUDIO_TASK_CORE);
    _taskHandle = h;

    _initialized = true;
    Serial.printf("[SpeakerOut] MAX98357A ready (I2S1 BCLK=%d LRC=%d DOUT=%d), "
                  "decode task on core %d prio %d\n",
                  SPK_BCLK_PIN, SPK_LRC_PIN, SPK_DOUT_PIN,
                  AUDIO_TASK_CORE, AUDIO_TASK_PRIO);
}

void SpeakerOutput::playFromBuffer(const uint8_t* mp3Data, size_t length) {
    if (!_initialized) begin();
    stop();   // tear down any previous playback + free its buffer (blocks until idle)

    if (!mp3Data || length == 0) {
        if (mp3Data) free((void*)mp3Data);
        return;
    }

    SemaphoreHandle_t mtx = (SemaphoreHandle_t)_mutex;
    xSemaphoreTake(mtx, portMAX_DELAY);

    // Take ownership of the MP3 buffer; freed in teardownLocked().
    _mp3Buffer = mp3Data;
    _mp3Length = length;

    s_src  = new AudioFileSourcePROGMEM(mp3Data, length);
    s_fbuf = new AudioFileSourceBuffer(s_src, 2048);   // smoothing buffer
    s_mp3  = new AudioGeneratorMP3();

    if (s_mp3->begin(s_fbuf, s_out)) {
        _stopReq = false;
        _playing = true;   // set BEFORE releasing the lock so isPlaying() is true
                           // immediately — no window for the main loop to see !playing
        Serial.printf("[SpeakerOut] playing %u bytes\n", (unsigned)length);
    } else {
        Serial.println("[SpeakerOut] ERROR: MP3 decode failed to start");
        teardownLocked();
    }

    xSemaphoreGive(mtx);
}

bool SpeakerOutput::isPlaying() const {
    return _playing;   // plain aligned bool read — atomic on ESP32, no lock needed
}

void SpeakerOutput::loop() {
    // Intentionally empty: the decoder is pumped by the background task now.
}

void SpeakerOutput::stop() {
    if (!_playing) return;
    _stopReq = true;
    // Wait for the task to finish the current frame and tear down. Different
    // core / cooperative yield, so this can't deadlock.
    while (_playing) vTaskDelay(pdMS_TO_TICKS(1));
    _stopReq = false;
}

// Free the decoder chain + MP3 buffer. MUST be called with _mutex held.
void SpeakerOutput::teardownLocked() {
    if (s_mp3) {
        if (s_mp3->isRunning()) s_mp3->stop();
        delete s_mp3; s_mp3 = nullptr;
    }
    if (s_fbuf) { delete s_fbuf; s_fbuf = nullptr; }
    if (s_src)  { delete s_src;  s_src  = nullptr; }
    if (_mp3Buffer) {
        free((void*)_mp3Buffer);
        _mp3Buffer = nullptr;
        _mp3Length = 0;
    }
    // AudioGeneratorMP3::stop() above calls s_out->stop(), which UNINSTALLS I2S1.
    // Re-start it immediately so the idle keep-alive loop can resume clocking
    // silence and the amp's clock stays continuous (no end-of-clip pop). The amp
    // caps are already charged, so this warm restart is inaudible.
    if (s_out) s_out->begin();
    _playing = false;
}

void SpeakerOutput::audioTaskThunk(void* arg) {
    static_cast<SpeakerOutput*>(arg)->audioTaskRun();
}

void SpeakerOutput::audioTaskRun() {
    SemaphoreHandle_t mtx = (SemaphoreHandle_t)_mutex;
    int16_t silence[2] = {0, 0};
    for (;;) {
        // The mutex guards ALL access to s_out/s_mp3 so this task can never race
        // playFromBuffer()/stop() on the main thread (which set up/tear down the
        // decoder chain under the same lock).
        xSemaphoreTake(mtx, portMAX_DELAY);

        if (_playing) {
            bool cont = false;
            if (!_stopReq && s_mp3 && s_mp3->isRunning()) {
                cont = s_mp3->loop();   // decode a frame; blocks in i2s_write when DMA full
            }
            if (!cont) {                // stream ended, decode error, or stop requested
                teardownLocked();       // clears _playing and frees the buffer
                Serial.println("[SpeakerOut] playback finished");
            }
            xSemaphoreGive(mtx);
        } else {
            // Idle: keep I2S1 clocking silence so the amp never stops/starts its
            // clock between clips — that stop/start is what pops. Self-paces
            // because ConsumeSample blocks when the DMA ring is full.
            if (s_out) {
                for (int i = 0; i < 64; i++) s_out->ConsumeSample(silence);
            }
            xSemaphoreGive(mtx);
            vTaskDelay(pdMS_TO_TICKS(2));
        }
    }
}
