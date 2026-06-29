#include <Arduino.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"

#include "config.h"
#include "wifi_manager.h"
#include "api_client.h"
#include "rfid_reader.h"
#include "session_manager.h"
#include "status_led.h"
#include "audio_input.h"
#include "audio_output.h"  // Contains SpeakerOutput class
#include "display.h"        // TFT Display
#include "web_server.h"

// ─── State Machine ──────────────────────────────────────
enum State {
    STATE_IDLE_UNAUTH,
    STATE_IDLE_AUTH,
    STATE_AUTH_PENDING,
    STATE_RECORDING,
    STATE_SENDING,
    STATE_PLAYING,
    STATE_ERROR,
};

// ─── Module Instances ───────────────────────────────────
WifiManager    wifiManager;
ApiClient      apiClient;
RfidReader     rfidReader;
SessionManager sessionManager;
StatusLed      statusLed;
AudioInput     audioInput;
SpeakerOutput  audioOutput;
// Display is defined in display.cpp (extern declaration in display.h)
SimpleWebServer webServer;

State currentState = STATE_IDLE_UNAUTH;
unsigned long errorStartTime = 0;

// ─── Audio Data for Web Server ───────────────────────────
const uint8_t* lastWavData = nullptr;
size_t lastWavSize = 0;
const uint8_t* lastMp3Data = nullptr;
size_t lastMp3Size = 0;

// ─── Push-to-Talk Button ───────────────────────────────
unsigned long pttDebounceTime = 0;
const unsigned long PTT_DEBOUNCE_MS = 50;

// ─── Forward Declarations ───────────────────────────────
void handleRfidTap(const String& uid);
void transitionToError();
void handleVoiceChatResult(const VoiceChatResult& result, bool isAuthenticated);

// ─── Web Server Callbacks ───────────────────────────────
const uint8_t* getWavData() { return lastWavData; }
size_t getWavSize() { return lastWavSize; }
const uint8_t* getMp3Data() { return lastMp3Data; }
size_t getMp3Size() { return lastMp3Size; }

// ─── Setup ──────────────────────────────────────────────
void setup() {
    WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

    // IMPORTANT: Set backlight GPIO LOW before anything else
    // This prevents backpowering through the LED pin
    pinMode(TFT_LED_PIN, OUTPUT);
    digitalWrite(TFT_LED_PIN, LOW);  // Turn off backlight initially

    Serial.begin(115200);
    delay(1000);
    Serial.println();
    Serial.println("========================================");
    Serial.println("  Smart University Assistant - ESP32");
    Serial.println("========================================");
    Serial.flush();

    // Initialize modules
    Serial.println("[DBG] Initializing LED..."); Serial.flush();
    statusLed.begin();
    statusLed.setPattern(PATTERN_SLOW_BLINK);

    Serial.println("[DBG] Initializing WiFi..."); Serial.flush();
    wifiManager.begin();

    if (wifiManager.isConnected()) {
        if (apiClient.healthCheck()) {
            Serial.println("[Main] Backend is reachable.");
        } else {
            Serial.println("[Main] WARNING: Backend not reachable.");
        }
    }

    // Initialize TFT first to set up SPI bus (shared with RFID)
    Serial.println("[DBG] Initializing Display..."); Serial.flush();
    // TODO: TFT crashes - disabled temporarily
    // display.begin();
    // display.showSplashScreen();
    Serial.println("[DBG] Display SKIPPED (debugging)...");

    Serial.println("[DBG] Initializing RFID..."); Serial.flush();
    delay(200);  // Let power stabilize
    rfidReader.begin();

    Serial.println("[DBG] Initializing Audio Input..."); Serial.flush();
    audioInput.begin();

    Serial.println("[DBG] Initializing Audio Output..."); Serial.flush();
    audioOutput.begin();

    Serial.println("[DBG] Initializing Web Server..."); Serial.flush();
    webServer.setWavCallbacks(getWavData, getWavSize);
    webServer.setMp3Callbacks(getMp3Data, getMp3Size);
    webServer.begin();

    // Configure PTT button
    pinMode(PTT_BUTTON_PIN, INPUT_PULLUP);
    Serial.printf("[DBG] PTT button on GPIO %d (INPUT_PULLUP)\n", PTT_BUTTON_PIN);

    statusLed.setPattern(PATTERN_OFF);
    // display.showIdleScreen();  // TODO: Disabled - TFT crashes
    Serial.println("[Main] Ready. Waiting for RFID tap or PTT button...");
    Serial.println();
}

// ─── Main Loop ──────────────────────────────────────────
void loop() {
    statusLed.update();
    sessionManager.checkTimeouts();
    webServer.loop();  // Handle web server requests

    // If session expired while in IDLE_AUTH, go back to IDLE_UNAUTH
    if (currentState == STATE_IDLE_AUTH && !sessionManager.isAuthenticated()) {
        currentState = STATE_IDLE_UNAUTH;
        statusLed.setPattern(PATTERN_OFF);
        Serial.println("[Main] Session expired -> IDLE_UNAUTH");
    }

    switch (currentState) {

        case STATE_IDLE_UNAUTH:
        case STATE_IDLE_AUTH: {
            // Check Wi-Fi
            wifiManager.ensureConnected();

            // Poll RFID
            rfidReader.poll();
            if (rfidReader.cardDetected()) {
                handleRfidTap(rfidReader.getLastUID());
                break;
            }

            // Check PTT button (for public queries or authenticated queries)
            int pttState = digitalRead(PTT_BUTTON_PIN);

            // Button is active LOW (pulled up, button connects to GND)
            if (pttState == LOW) {
                // Debounce check
                if (millis() - pttDebounceTime > PTT_DEBOUNCE_MS) {
                    // Start recording
                    currentState = STATE_RECORDING;
                    audioInput.startRecording();

                    // Show recording screen
                    display.showRecordingScreen();

                    // Record activity for session timeout
                    if (sessionManager.isAuthenticated()) {
                        sessionManager.recordActivity();
                    }

                    // LED feedback
                    statusLed.setPattern(PATTERN_FAST_BLINK);
                    Serial.println("[Main] PTT pressed -> RECORDING");
                }
            } else {
                pttDebounceTime = millis();
            }

            // Update display status
            display.showStatus(wifiManager.isConnected(), sessionManager.isAuthenticated());

            // Pump audio input even in idle (for I2S driver)
            audioInput.loop();
            break;
        }

        case STATE_AUTH_PENDING:
            // This state is handled synchronously in handleRfidTap()
            // so we should never actually be in this state during loop()
            break;

        case STATE_RECORDING: {
            // Pump audio input
            audioInput.loop();

            // Update recording animation on display
            display.updateRecordingAnimation();

            // Check if button was released or max duration reached
            int pttState = digitalRead(PTT_BUTTON_PIN);

            if (pttState == HIGH || audioInput.maxDurationReached()) {
                // Button released or max duration
                audioInput.stopRecording();

                size_t recordedBytes = audioInput.getRecordedBytes();
                Serial.printf("[Main] Recording stopped: %zu bytes\n", recordedBytes);

                if (recordedBytes > 1000) {  // At least 1KB of audio
                    currentState = STATE_SENDING;
                    statusLed.setPattern(PATTERN_FAST_BLINK);  // Fast blink while sending
                    Serial.println("[Main] -> SENDING");
                } else {
                    Serial.println("[Main] Recording too short, discarding");
                    currentState = sessionManager.isAuthenticated()
                        ? STATE_IDLE_AUTH : STATE_IDLE_UNAUTH;
                    statusLed.setPattern(
                        sessionManager.isAuthenticated() ? PATTERN_SOLID : PATTERN_OFF);
                }
            }
            break;
        }

        case STATE_SENDING: {
            // Show processing screen
            display.showProcessingScreen("Sending voice to AI...");

            // Get WAV data
            size_t wavSize;
            uint8_t* wavData = audioInput.getWavData(&wavSize);

            if (!wavData || wavSize == 0) {
                Serial.println("[Main] Failed to get WAV data");
                display.showErrorScreen("Failed to record audio");
                transitionToError();
                break;
            }

            Serial.printf("[Main] Sending %zu bytes to backend...\n", wavSize);

            // Save copy for web server
            if (lastWavData) free((void*)lastWavData);
            if (psramFound()) {
                lastWavData = (const uint8_t*)ps_malloc(wavSize);
            } else {
                lastWavData = (const uint8_t*)malloc(wavSize);
            }
            if (lastWavData) {
                memcpy((void*)lastWavData, wavData, wavSize);
                lastWavSize = wavSize;
                Serial.printf("[Main] Saved WAV for web: %zu bytes\n", lastWavSize);
            }

            // Prepare request
            String sessionId = sessionManager.getSessionId();
            String token = sessionManager.getToken();  // Empty if not authenticated

            // Send to backend
            VoiceChatResult result = apiClient.postVoiceChat(wavData, wavSize, sessionId, token);

            // Free WAV data (but keep the copy for web server)
            audioInput.freeWavData();

            // Handle result
            if (result.success) {
                // Update session ID if server provided one
                if (result.sessionId.length() > 0) {
                    sessionManager.setSessionId(result.sessionId);
                }

                // Print transcription
                if (result.transcription.length() > 0) {
                    Serial.printf("[Main] User said: \"%s\"\n", result.transcription.c_str());
                }

                // If we got MP3 data, save for web and go to playing state
                if (result.mp3Data && result.mp3Length > 0) {
                    // Save copy for web server (audioOutput takes ownership, so copy first)
                    if (lastMp3Data) free((void*)lastMp3Data);
                    if (psramFound()) {
                        lastMp3Data = (const uint8_t*)ps_malloc(result.mp3Length);
                    } else {
                        lastMp3Data = (const uint8_t*)malloc(result.mp3Length);
                    }
                    if (lastMp3Data) {
                        memcpy((void*)lastMp3Data, result.mp3Data, result.mp3Length);
                        lastMp3Size = result.mp3Length;
                        Serial.printf("[Main] Saved MP3 for web: %zu bytes\n", lastMp3Size);
                    }

                    // Start playback (audioOutput takes ownership of the original buffer)
                    audioOutput.playFromBuffer(result.mp3Data, result.mp3Length);
                    currentState = STATE_PLAYING;
                    statusLed.setPattern(PATTERN_AUTH_SUCCESS);  // Solid while playing
                    Serial.println("[Main] -> PLAYING");
                } else {
                    // No audio response, go back to idle
                    currentState = sessionManager.isAuthenticated()
                        ? STATE_IDLE_AUTH : STATE_IDLE_UNAUTH;
                    statusLed.setPattern(
                        sessionManager.isAuthenticated() ? PATTERN_SOLID : PATTERN_OFF);
                    Serial.println("[Main] No audio response -> IDLE");
                }
            } else {
                // Handle error
                if (result.httpCode == 401) {
                    // Token expired, clear auth and retry as public
                    Serial.println("[Main] Token expired, clearing auth");
                    sessionManager.clearAuth();

                    // Retry as public (one-time retry)
                    Serial.println("[Main] Retrying as public...");
                    VoiceChatResult retryResult = apiClient.postVoiceChat(wavData, wavSize, sessionId, "");

                    audioInput.freeWavData();  // Free after retry

                    if (retryResult.success) {
                        handleVoiceChatResult(retryResult, false);
                    } else {
                        Serial.printf("[Main] Public retry also failed: %s\n", retryResult.error.c_str());
                        transitionToError();
                    }
                } else {
                    Serial.printf("[Main] Voice chat failed: %s\n", result.error.c_str());
                    transitionToError();
                }
            }
            break;
        }

        case STATE_PLAYING: {
            // Pump audio output
            audioOutput.loop();

            // Check if playback finished
            if (!audioOutput.isPlaying()) {
                // Free MP3 buffer
                audioOutput.stop();

                // Go back to idle
                currentState = sessionManager.isAuthenticated()
                    ? STATE_IDLE_AUTH : STATE_IDLE_UNAUTH;
                statusLed.setPattern(
                    sessionManager.isAuthenticated() ? PATTERN_SOLID : PATTERN_OFF);
                Serial.println("[Main] Playback finished -> IDLE");
            }
            break;
        }

        case STATE_ERROR:
            if (millis() - errorStartTime > 2000) {
                currentState = sessionManager.isAuthenticated()
                    ? STATE_IDLE_AUTH : STATE_IDLE_UNAUTH;
                statusLed.setPattern(
                    sessionManager.isAuthenticated() ? PATTERN_SOLID : PATTERN_OFF);
            }
            break;
    }

    delay(10);  // small yield to prevent watchdog issues
}

// ─── RFID Tap Handler ───────────────────────────────────
void handleRfidTap(const String& uid) {
    // If currently authenticated
    if (sessionManager.isAuthenticated()) {
        if (uid == sessionManager.getAuthenticatedUid()) {
            // Same card: logout
            Serial.printf("[Main] Same card re-tap -> logout (UID: %s)\n", uid.c_str());
            sessionManager.clearAuth();
            currentState = STATE_IDLE_UNAUTH;
            statusLed.setPattern(PATTERN_OFF);
            return;
        } else {
            // Different card: switch user
            Serial.printf("[Main] Different card -> switching user (new UID: %s)\n", uid.c_str());
            sessionManager.clearAuth();
        }
    }

    // Authenticate with backend
    currentState = STATE_AUTH_PENDING;
    statusLed.setPattern(PATTERN_FAST_BLINK);

    if (!wifiManager.isConnected()) {
        Serial.println("[Main] No Wi-Fi, cannot authenticate.");
        transitionToError();
        return;
    }

    AuthResult result = apiClient.postRfidAuth(uid);

    if (result.success) {
        sessionManager.setAuth(result.token, result.role, uid);
        currentState = STATE_IDLE_AUTH;
        statusLed.setPattern(PATTERN_AUTH_SUCCESS);

        // Show student screen on display
        display.showStudentScreen(result.name.c_str(), result.role.c_str(), uid.c_str());

        Serial.println("[Main] -> IDLE_AUTH");
    } else {
        Serial.printf("[Main] Auth failed: %s\n", result.error.c_str());
        transitionToError();
    }
}

// ─── Voice Chat Result Handler ──────────────────────────
void handleVoiceChatResult(const VoiceChatResult& result, bool isAuthenticated) {
    if (result.success) {
        // Update session ID if server provided one
        if (result.sessionId.length() > 0) {
            sessionManager.setSessionId(result.sessionId);
        }

        // Print transcription
        if (result.transcription.length() > 0) {
            Serial.printf("[Main] User said: \"%s\"\n", result.transcription.c_str());
        }

        // If we got MP3 data, save for web and go to playing state
        if (result.mp3Data && result.mp3Length > 0) {
            // Save copy for web server
            if (lastMp3Data) free((void*)lastMp3Data);
            if (psramFound()) {
                lastMp3Data = (const uint8_t*)ps_malloc(result.mp3Length);
            } else {
                lastMp3Data = (const uint8_t*)malloc(result.mp3Length);
            }
            if (lastMp3Data) {
                memcpy((void*)lastMp3Data, result.mp3Data, result.mp3Length);
                lastMp3Size = result.mp3Length;
                Serial.printf("[Main] Saved MP3 for web: %zu bytes\n", lastMp3Size);
            }

            // Start playback
            audioOutput.playFromBuffer(result.mp3Data, result.mp3Length);
            currentState = STATE_PLAYING;
            statusLed.setPattern(PATTERN_AUTH_SUCCESS);
            Serial.println("[Main] -> PLAYING");
        } else {
            // No audio response, go back to idle
            currentState = isAuthenticated ? STATE_IDLE_AUTH : STATE_IDLE_UNAUTH;
            statusLed.setPattern(isAuthenticated ? PATTERN_SOLID : PATTERN_OFF);
            Serial.println("[Main] No audio response -> IDLE");
        }
    } else {
        Serial.printf("[Main] Voice chat failed: %s\n", result.error.c_str());
        transitionToError();
    }
}

// ─── Helpers ────────────────────────────────────────────
void transitionToError() {
    currentState = STATE_ERROR;
    errorStartTime = millis();
    statusLed.setPattern(PATTERN_ERROR);
    display.showErrorScreen("An error occurred");
}
