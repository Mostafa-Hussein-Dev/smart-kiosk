# Phase 3 & 4 Implementation Summary

## Status: COMPLETED

Phases 3 (Audio Input) and 4 (Voice API Client) have been fully implemented with PTT button integration.

---

## Files Modified

### 1. `audio_input.h` & `audio_input.cpp` — Phase 3 Complete

**Implemented:**
- I2S driver configuration for INMP441 MEMS microphone
- Audio recording at 16kHz, 16-bit mono
- 224KB PCM buffer (7 seconds max recording)
- WAV header generation (RIFF/WAVE format)
- PSRAM-aware memory allocation
- 32-bit to 16-bit sample conversion (INMP441 sends 32-bit frames)

**Key Methods:**
```cpp
void begin()                          // Initialize I2S
void startRecording()                 // Start recording
void stopRecording()                  // Stop recording
void loop()                           // Pump audio data
bool isRecording()                    // Check state
size_t getRecordedBytes()             // Get bytes recorded
uint8_t* getWavData(size_t* outSize)  // Get WAV with header
void freeWavData()                    // Free WAV buffer
```

---

### 2. `api_client.h` & `api_client.cpp` — Phase 4 Complete

**Implemented:**
- Multipart/form-data HTTP POST for WAV upload
- Support for both `/api/voice/chat` (authenticated) and `/api/voice/chat/public`
- MP3 response buffering with PSRAM support
- Session ID and transcription extraction from response headers
- 401 token expiry handling with automatic retry as public

**Key Methods:**
```cpp
VoiceChatResult postVoiceChat(
    const uint8_t* wavData,    // WAV audio with header
    size_t wavLen,
    const String& sessionId,
    const String& bearerToken   // Empty = public endpoint
);
```

**VoiceChatResult Structure:**
```cpp
struct VoiceChatResult {
    bool    success;
    int     httpCode;
    String  sessionId;      // From X-Session-Id header
    String  transcription;  // From X-Transcription header
    uint8_t* mp3Data;      // MP3 response data
    size_t  mp3Length;
    String  error;
};
```

---

### 3. `main.cpp` — PTT Button Integration Complete

**Implemented:**
- PTT button handling on GPIO 9 (INPUT_PULLUP, active LOW)
- Recording while button pressed
- Automatic upload on button release
- State machine: IDLE → RECORDING → SENDING → PLAYING → IDLE
- LED feedback for each state
- Minimum 1KB recording threshold (prevents accidental triggers)
- Session timeout reset on PTT press (for authenticated users)
- Auto-retry as public if 401 token expired

**State Flow:**
```
IDLE_UNAUTH / IDLE_AUTH
        ↓
   [PTT button pressed]
        ↓
   RECORDING (LED fast blink)
        ↓
   [PTT released OR max duration]
        ↓
   SENDING (LED fast blink)
        ↓
   [Upload complete]
        ↓
   PLAYING (LED solid) ←→ IDLE (when done)
```

---

### 4. `audio_output.h` & `audio_output.cpp` — Phase 5 Complete

**Implemented:**
- ESP8266Audio library integration
- I2S output for MAX98357A amplifier
- MP3 playback from memory buffer
- Buffer ownership management
- Non-blocking playback with `loop()` method

**Note:** Speaker functionality cannot be tested until MAX98357A arrives, but implementation is complete.

---

### 5. `platformio.ini` — Library Dependency Added

**Added:**
```
earlephilhower/ESP8266Audio@^1.9.8
```

---

## How It Works

### Recording Flow (Button Held)
1. User presses PTT button (GPIO 9 goes LOW)
2. `audioInput.startRecording()` resets buffer
3. LED enters fast blink pattern
4. `audioInput.loop()` continuously reads from I2S and fills PCM buffer
5. Converts 32-bit INMP441 samples to 16-bit (right-shift by 16)
6. Stops when max duration (7 sec) reached

### Sending Flow (Button Released)
1. User releases PTT button (GPIO 9 goes HIGH)
2. `audioInput.stopRecording()` finalizes recording
3. `audioInput.getWavData()` generates WAV header + PCM data
4. Multipart/form-data body constructed
5. HTTP POST to `/api/voice/chat/public` or `/api/voice/chat`
6. MP3 response downloaded to PSRAM buffer
7. Session ID and transcription extracted from headers

### Playback Flow
1. `audioOutput.playFromBuffer()` starts MP3 decoder
2. `audioOutput.loop()` pumps decoder until finished
3. LED shows solid during playback
4. Returns to IDLE when done

---

## PTT Button Wiring

| ESP32-S3 Pin | PTT Button Pin | Connection |
|--------------|----------------|-------------|
| GPIO 9 | NO | Yellow wire |
| GND | C | Black wire |
| (unused) | NC | Not used |

**Behavior:**
- Button released = NO open circuit = GPIO reads HIGH (pullup)
- Button pressed = NO connects to C (GND) = GPIO reads LOW

---

## Current Pin Assignments

| Peripheral | Pin(s) |
|------------|----------|
| RC522 RFID | GPIO 4, 10, 11, 12, 13 |
| INMP441 Mic | GPIO 5, 6, 7 |
| MAX98357A Amp | GPIO 16, 17, 18 |
| PTT Button | GPIO 9 |
| Status LED | GPIO 38 (onboard) |

---

## Testing Instructions

### Current Hardware (Can Test Now)
- ✅ ESP32-S3 + RC522 RFID
- ✅ INMP441 Microphone
- ✅ PTT Button
- ✅ Power module

### Cannot Test Yet (Waiting for Hardware)
- ❌ MAX98357A Amplifier
- ❌ Speaker
- ❌ Display

### What You CAN Test Now

1. **RFID Authentication:**
   - Tap card → LED blinks → Serial shows auth result
   - Re-tap same card → logout
   - 5-minute auto-logout

2. **Voice Recording:**
   - Press PTT → LED fast blink → recording starts
   - Speak → recording fills buffer
   - Release PTT → WAV sent to backend
   - Serial shows transcription and response

3. **Public vs Authenticated Queries:**
   - No RFID tap: Uses `/api/voice/chat/public`
   - After RFID tap: Uses `/api/voice/chat` with token
   - "What is my GPA?" only works when authenticated

---

## Remaining Work

| Phase | Description | Status |
|-------|-------------|--------|
| 1-2 | Wi-Fi + RFID | ✅ Done |
| 3 | Audio Input | ✅ Done |
| 4 | Voice API Client | ✅ Done |
| 5 | Audio Output | ✅ Done (waiting for hardware) |
| 6 | Authenticated Pipeline | ✅ Done |
| 7 | Polish & Edge Cases | ⏳ Pending |

---

## Flash Instructions

```bash
cd hardware
pio run --target upload
# Monitor serial at 115200 baud
```

---

## Serial Output Examples

### Successful Voice Query
```
[Main] PTT pressed -> RECORDING
[Main] Recording stopped: 45000 bytes
[API] POST /api/voice/chat/public  wav_len=45056  session=  auth=0
[API] Response HTTP 200
[API] Session ID: abc123def456
[API] Transcription: What is my GPA?
[Main] User said: "What is my GPA?"
[AudioOut] Playing MP3: 12500 bytes
[Main] -> PLAYING
[AudioOut] Playback finished
[Main] Playback finished -> IDLE
```

### Authenticated Query
```
[Main] PTT pressed -> RECORDING
...
[API] POST /api/voice/chat  wav_len=45056  session=abc123  auth=1
[API] Response HTTP 200
[Main] User said: "What is my GPA?"
[AudioOut] Playing MP3: 15000 bytes
...
```

### Token Expired Handling
```
[API] 401 Unauthorized - token may have expired
[Main] Token expired, clearing auth
[Main] Retrying as public...
```
