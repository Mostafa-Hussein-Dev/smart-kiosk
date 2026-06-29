# ESP32 Hardware Firmware — Full Implementation Plan

This document details all remaining phases for completing the ESP32 Smart University Assistant firmware. Phase 1 (skeleton + Wi-Fi + health check) and Phase 2 (RFID authentication) are already implemented.

---

## Phase 3: Audio Input (I2S Microphone)

**Goal:** Record voice from the INMP441 MEMS microphone into a WAV buffer ready to send to the backend.

**Files to modify:** `audio_input.h`, `audio_input.cpp`

### Implementation Details

1. **I2S driver configuration (ESP-IDF)**
   - Port: `I2S_NUM_0`
   - Mode: `I2S_MODE_MASTER | I2S_MODE_RX`
   - Sample rate: 16000 Hz
   - Bits per sample: 16 (but INMP441 sends 32-bit frames — must right-shift by 16)
   - Channel: mono (left channel only, `I2S_CHANNEL_FMT_ONLY_LEFT`)
   - DMA buffer: 8 buffers × 256 samples each

2. **PCM buffer allocation**
   - Size: `MAX_RECORD_SECONDS * 16000 * 2` bytes (224 KB for 7 seconds)
   - Allocate with `ps_malloc()` if PSRAM detected, else `malloc()`
   - Single contiguous buffer with a write position index

3. **Recording flow**
   - `startRecording()`: reset write position to 0, set `_recording = true`
   - `loop()`: if recording, call `i2s_read()` in chunks of 512 bytes, convert each 32-bit sample to 16-bit (right-shift 16), append to PCM buffer, advance write position
   - `stopRecording()`: set `_recording = false`, finalize byte count
   - `maxDurationReached()`: return true when write position >= buffer size

4. **WAV header generation**
   - `getWavData()`: allocate a combined buffer (44 + PCM bytes), write standard RIFF/WAV header (1 channel, 16000 Hz, 16-bit PCM), copy PCM data after header, return pointer and total size
   - WAV header fields: ChunkID="RIFF", Format="WAVE", Subchunk1ID="fmt ", AudioFormat=1 (PCM), NumChannels=1, SampleRate=16000, BitsPerSample=16, Subchunk2ID="data"

### Verification
- Flash, hold button, speak, release button
- Serial prints: "Recording started", buffer fill progress, "Recording stopped, X bytes captured"
- Export the WAV buffer over serial (base64) or save to SPIFFS, then manually verify it plays correctly on a PC
- Alternatively: POST the WAV to the backend's `/api/voice/stt` endpoint and verify the transcription in serial output

---

## Phase 4: Voice Chat API Client (Public, No Auth)

**Goal:** Send recorded WAV audio to the backend `/api/voice/chat/public` endpoint and receive MP3 audio bytes in response.

**Files to modify:** `api_client.h`, `api_client.cpp`

### Implementation Details

1. **Multipart form-data construction**
   - The `/api/voice/chat` and `/api/voice/chat/public` endpoints expect `multipart/form-data`
   - Build the multipart body manually:
     ```
     --boundary\r\n
     Content-Disposition: form-data; name="audio"; filename="recording.wav"\r\n
     Content-Type: audio/wav\r\n\r\n
     <WAV bytes>\r\n
     --boundary\r\n
     Content-Disposition: form-data; name="session_id"\r\n\r\n
     <session_id>\r\n
     --boundary--\r\n
     ```
   - Generate a unique boundary string (e.g., `"----ESP32Boundary"`)
   - Set header: `Content-Type: multipart/form-data; boundary=----ESP32Boundary`

2. **New method: `VoiceChatResult postVoiceChat(...)`**
   - Parameters: `const uint8_t* wavData, size_t wavLen, const String& sessionId, const String& bearerToken`
   - If `bearerToken` is non-empty: POST to `/api/voice/chat` with `Authorization: Bearer <token>`
   - If empty: POST to `/api/voice/chat/public`
   - Response is `audio/mpeg` (MP3 bytes)
   - Extract headers: `X-Session-Id`, `X-Transcription`

3. **Response handling — two strategies (pick one):**

   **Strategy A — Buffer entire response (simpler, uses more RAM):**
   - Read `Content-Length` header, allocate buffer, `http.getStream().readBytes()` into it
   - Suitable if responses are small (< 80 KB for ~10 sec TTS)
   - Store in PSRAM if available

   **Strategy B — Stream to decoder (lower RAM, more complex):**
   - Pass `http.getStream()` (a `WiFiClient&`) directly to the audio output module
   - The MP3 decoder reads from the stream on-the-fly
   - Requires `AudioOutput` to accept a stream (see Phase 5)
   - Recommended for production; Strategy A is fine for initial testing

4. **VoiceChatResult struct:**
   ```cpp
   struct VoiceChatResult {
       bool    success;
       int     httpCode;
       String  sessionId;      // from X-Session-Id header
       String  transcription;  // from X-Transcription header
       uint8_t* mp3Data;       // buffered MP3 bytes (Strategy A)
       size_t  mp3Length;
       String  error;
   };
   ```

### Verification
- Record audio (Phase 3), send to `/api/voice/chat/public`
- Serial prints: transcription from `X-Transcription`, session ID, MP3 response size
- Verify HTTP 200 and non-zero MP3 data received

---

## Phase 5: Audio Output (I2S Speaker + MP3 Decode)

**Goal:** Play MP3 audio through the MAX98357A speaker using the ESP8266Audio library.

**Files to modify:** `audio_output.h`, `audio_output.cpp`

### Implementation Details

1. **ESP8266Audio library setup**
   - `AudioOutputI2S` configured for I2S port 1, pins: BCLK=26, LRC=25, DOUT=22
   - `AudioGeneratorMP3` for MP3 decoding
   - `AudioFileSourceBuffer` wrapping an `AudioFileSourcePROGMEM` or custom stream source

2. **Two playback methods:**

   **`playFromBuffer(const uint8_t* mp3Data, size_t length)`:**
   - Create `AudioFileSourcePROGMEM(mp3Data, length)`
   - Wrap in `AudioFileSourceBuffer` (2048 byte buffer for smooth playback)
   - Start `AudioGeneratorMP3` with source and output
   - Non-blocking: `loop()` must be called from main loop to pump the decoder

   **`playFromStream(WiFiClient* stream, size_t contentLength)` (for Strategy B):**
   - Create a custom `AudioFileSource` that reads from the WiFiClient
   - Or use `AudioFileSourceHTTPStream` if doing a separate HTTP request
   - More efficient but requires keeping the HTTP connection alive during playback

3. **Playback state**
   - `isPlaying()`: returns `_generator->isRunning()`
   - `stop()`: calls `_generator->stop()`
   - `loop()`: calls `_generator->loop()`; when returns false, playback is done

4. **I2S output configuration**
   - Sample rate: set to match MP3 (typically 24000 Hz for OpenAI TTS output)
   - The ESP8266Audio library handles sample rate conversion internally
   - Mono output (the MAX98357A is a mono amplifier)

### Verification
- Hardcode a small MP3 byte array (a beep or short TTS clip) in PROGMEM
- Call `playFromBuffer()` in setup, verify sound comes from speaker
- Then integrate with Phase 4: record → send → receive MP3 → play
- Full public voice loop should work end-to-end

---

## Phase 6: Authenticated Voice Pipeline

**Goal:** Connect the session manager to the voice pipeline so authenticated users get personalized responses.

**Files to modify:** `main.cpp`, `api_client.cpp`

### Implementation Details

1. **State machine additions in `main.cpp`:**
   - `STATE_RECORDING`: on button press → `audioInput.startRecording()`, LED fast blink
     - On button release (or max duration): `audioInput.stopRecording()` → `STATE_SENDING`
   - `STATE_SENDING`: call `apiClient.postVoiceChat()` with:
     - WAV data from `audioInput.getWavData()`
     - `sessionManager.getSessionId()` (may be empty for first message)
     - `sessionManager.getToken()` (empty string if unauthenticated → public endpoint)
     - On success: store `result.sessionId` in `sessionManager.setSessionId()`, pass MP3 data to audio output → `STATE_PLAYING`
     - On 401 (token expired): clear auth, retry as public → `STATE_PLAYING`
     - On other error → `STATE_ERROR`
   - `STATE_PLAYING`: call `audioOutput.loop()` each iteration
     - When `!audioOutput.isPlaying()`: free MP3 buffer, transition to `STATE_IDLE_AUTH` or `STATE_IDLE_UNAUTH`

2. **Activity tracking:**
   - Call `sessionManager.recordActivity()` when the button is pressed (start of interaction)
   - This resets the 5-minute inactivity timer

3. **Endpoint selection logic (already in SessionManager):**
   - `getVoiceEndpoint()` returns `/api/voice/chat` if authenticated, `/api/voice/chat/public` if not
   - `ApiClient` uses the token to decide: non-empty token → add Bearer header + use auth endpoint

### Verification
- Without RFID: press button, speak, get public response played back
- Tap RFID card (RFID1001): LED solid, press button, speak "What is my GPA?" → get personalized response
- Wait 5 minutes → auto-logout → next query returns public response
- Re-tap card → "logout" in serial → LED off

---

## Phase 7: Polish & Edge Cases

**Goal:** Production-ready firmware with robust error handling and good UX.

**Files to modify:** all modules

### Implementation Details

1. **Wi-Fi resilience**
   - If Wi-Fi disconnects mid-request: detect via `HTTPClient` error code, transition to `STATE_ERROR`
   - In error state: `wifiManager.ensureConnected()` retries
   - If Wi-Fi not available at boot: stay in `PATTERN_SLOW_BLINK`, retry every 5 seconds, skip RFID/button until connected

2. **Backend unreachable handling**
   - Health check failure at boot: log warning but continue (backend might come up later)
   - HTTP timeout on voice request: transition to error, show LED pattern
   - Consider a "backend status" check every 30 seconds in idle state (optional, low priority)

3. **Memory management**
   - Free WAV buffer after sending
   - Free MP3 buffer after playback
   - Monitor free heap with `ESP.getFreeHeap()` logged periodically to serial
   - If heap is low (<20 KB), skip recording and show error

4. **Button debounce**
   - Add software debounce (50ms) for the push-to-talk button
   - Consider requiring button held for 200ms before starting recording (avoids accidental taps)

5. **Audio quality improvements**
   - Add a short beep at recording start and stop for user feedback (pre-record a tiny WAV in PROGMEM)
   - Add a small delay after recording stops before sending (100ms, to avoid cutting off the last word)
   - Volume control: MAX98357A gain can be set via a resistor or the GAIN pin

6. **Serial logging levels**
   - Add a `LOG_LEVEL` define in config.h (0=none, 1=errors, 2=info, 3=debug)
   - Wrap Serial.print calls in macros: `LOG_INFO(...)`, `LOG_DEBUG(...)`, `LOG_ERROR(...)`

7. **OTA updates (optional, stretch goal)**
   - Use `ArduinoOTA` library for over-the-air firmware updates
   - Only during idle states
   - Useful for updating Wi-Fi creds or backend URL without re-flashing

8. **Watchdog timer**
   - ESP32 has a hardware watchdog. Ensure `delay(10)` or `yield()` in the main loop prevents watchdog resets
   - For long HTTP requests, the HTTPClient internally yields, so no issue there

### Verification
- Unplug backend → press button → get error LED, then plug back in → next request succeeds
- Disconnect Wi-Fi (turn off router) → see reconnection attempts in serial → reconnect → system recovers
- Run for 30+ minutes continuously → monitor serial for memory leaks (heap should stay stable)
- Test rapid button presses → no crashes or double-sends
- Test RFID tap during recording → should be ignored (only processed in idle states)
- Test RFID tap during playback → should be ignored (only processed in idle states)

---

## Summary Timeline

| Phase | Description | Key Deliverable |
|-------|-------------|-----------------|
| ~~1~~ | ~~Skeleton + Wi-Fi + Health Check~~ | ~~Done~~ |
| ~~2~~ | ~~RFID Authentication~~ | ~~Done~~ |
| 3 | Audio Input (I2S Mic) | Record voice to WAV buffer |
| 4 | Voice API Client | Send WAV, receive MP3 from backend |
| 5 | Audio Output (I2S Speaker) | Play MP3 through speaker |
| 6 | Authenticated Voice Pipeline | Full end-to-end: RFID → voice → personalized response |
| 7 | Polish & Edge Cases | Error handling, UX feedback, memory management |

Phases 3-5 can be tested incrementally. Phase 6 ties everything together. Phase 7 is iterative refinement.
