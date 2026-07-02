// ════════════════════════════════════════════════════════════════
//  Smart University Assistant — ESP32-S3 Voice Kiosk
//  Full functional firmware (no demo modes).
//
//  Flow:
//    Idle robot face  ──tap card──►  greet by name (authenticated)
//         │  hold PTT
//         ▼
//    RECORD (listening) ──release──► SEND (thinking) ──► PLAY (speaking) ──► Idle
//
//  Unauthenticated users get public answers; RFID tap unlocks personal ones.
// ════════════════════════════════════════════════════════════════
#include <Arduino.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include "config.h"
#include "status_led.h"
#include "wifi_manager.h"
#include "api_client.h"
#include "rfid_reader.h"
#include "session_manager.h"
#include "audio_input.h"
#include "audio_output.h"
#include "display.h"
#include "robot_face.h"

// ─── State machine ───────────────────────────────────────
enum State {
    STATE_IDLE_UNAUTH,
    STATE_IDLE_AUTH,
    STATE_AUTH_PENDING,
    STATE_RECORDING,
    STATE_SENDING,
    STATE_PLAYING,
    STATE_ERROR,
};

// ─── Modules ─────────────────────────────────────────────
StatusLed      statusLed;
WifiManager    wifiManager;
ApiClient      apiClient;
RfidReader     rfidReader;
SessionManager sessionManager;
AudioInput     audioInput;
SpeakerOutput  audioOutput;
RobotFace      robotFace(display);     // 'display' is the global in display.cpp

State currentState = STATE_IDLE_UNAUTH;
unsigned long errorUntil = 0;
unsigned long greetUntil = 0;
unsigned long lastWifiCheck = 0;
unsigned long recordStart = 0;
unsigned long lastFaceDraw = 0;

// The TFT face redraw is slow (~40-60 ms over SPI). While recording or playing
// we must NOT run it every loop iteration or it starves the audio I2S DMA
// (dropped mic samples / stuttering speaker). Throttle it (~7 fps is plenty for
// the mouth animation) and let the audio pump run on every iteration instead.
#define FACE_FRAME_MS  150

// Idle prompt wording depends on how the user asks a question.
#if USE_SERIAL_TEXT_INPUT
  #define TALK_HINT "type to talk"
#else
  #define TALK_HINT "hold to talk"
#endif

// PTT edge detection (debounced)
bool pttPrev = false;

// Pending typed question (serial-text input mode), consumed in STATE_SENDING.
String pendingText;

// ─── Forward declarations ────────────────────────────────
void handleRfidTap(const String& uid);
void goIdle();
void goError(const char* msg);
bool pttPressed();
bool readSerialLine(String& out);
String shorten(const String& s, size_t n);

// ─── Setup ───────────────────────────────────────────────
void setup() {
    // Disable the brownout DETECTOR (not the watchdogs). This board's onboard
    // 3.3V LDO dips during the Wi-Fi radio calibration spike and trips the
    // detector even on a good 5V supply — same workaround the original firmware
    // used. Watchdogs stay enabled.
    WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

    Serial.begin(115200);
    for (int i = 0; i < 8; i++) { delay(100); yield(); }

    Serial.println("\n========================================");
    Serial.println("  Smart University Assistant - ESP32-S3");
    Serial.println("========================================");
    Serial.printf("PSRAM: %s", psramFound() ? "yes" : "NO");
    if (psramFound()) Serial.printf("  free=%u KB", ESP.getFreePsram() / 1024);
    Serial.printf("\nFree heap: %u KB\n", ESP.getFreeHeap() / 1024);

    // LED first (connecting)
    statusLed.begin();
    statusLed.setPattern(PATTERN_SLOW_BLINK);

    // Keep the TFT panel + backlight OFF for now so they don't load the rail
    // during the Wi-Fi radio power-on spike.
    pinMode(TFT_LED_PIN, OUTPUT);
    digitalWrite(TFT_LED_PIN, LOW);

    // ── Bring up Wi-Fi FIRST, with the display still off and the CPU throttled
    //    to 80 MHz, so almost nothing else is drawing during the radio inrush.
    //    Let the supply/caps settle first. ──
    delay(800);
    setCpuFrequencyMhz(80);
    wifiManager.begin();            // blocks up to ~30s
    setCpuFrequencyMhz(240);
    if (wifiManager.isConnected()) {
        apiClient.healthCheck();    // best effort; wakes the Render backend
    }

    // ── Now the radio is up and steady; bring up the display + face ──
    display.begin();
    for (int i = 0; i < 6; i++) { delay(100); yield(); }
    digitalWrite(TFT_LED_PIN, TFT_BACKLIGHT_ON);
    display.showSplashScreen();
    robotFace.init();

    // Peripherals
    rfidReader.begin();
#if !USE_SERIAL_TEXT_INPUT
    if (!audioInput.begin())  Serial.println("[Main] WARNING: mic init failed");
#endif
    audioOutput.begin();

    pinMode(PTT_BUTTON_PIN, INPUT_PULLUP);

    // Hand the screen to the robot face for the rest of runtime
    RobotFace::setDisplayMode(MODE_ROBOT);
    goIdle();

    Serial.println("[Main] Ready.");
#if USE_SERIAL_TEXT_INPUT
    Serial.println("[Main] TEXT MODE: type your question here + Enter to ask.\n");
#else
    Serial.println("[Main] Hold the PTT button and speak.\n");
#endif
}

// ─── Main loop ───────────────────────────────────────────
void loop() {
    statusLed.update();
    sessionManager.checkTimeouts();

    // Session expired while authenticated -> drop to unauth idle
    if (currentState == STATE_IDLE_AUTH && !sessionManager.isAuthenticated()) {
        Serial.println("[Main] session expired");
        goIdle();
    }

    bool ptt = pttPressed();

    switch (currentState) {

        case STATE_IDLE_UNAUTH:
        case STATE_IDLE_AUTH: {
            // After a greeting, settle back to the neutral idle face
            if (greetUntil && millis() > greetUntil) {
                greetUntil = 0;
                robotFace.setExpression(FACE_IDLE);
                String n = sessionManager.getName();
                robotFace.setStatusText(n.length() ? n + " - " TALK_HINT : "");
            }

            // Animate face
            robotFace.update();
            robotFace.draw();

            // Periodic Wi-Fi keepalive (non-blocking-ish, only when idle)
            if (millis() - lastWifiCheck > 5000) {
                lastWifiCheck = millis();
                wifiManager.ensureConnected();
            }

            // RFID tap?
            rfidReader.poll();
            if (rfidReader.cardDetected()) {
                handleRfidTap(rfidReader.getLastUID());
                break;
            }

#if USE_SERIAL_TEXT_INPUT
            // Typed question over the USB serial monitor (microphone replacement).
            String line;
            if (readSerialLine(line)) {
                if (!wifiManager.isConnected()) {
                    goError("No Wi-Fi");
                } else {
                    sessionManager.recordActivity();
                    pendingText = line;
                    Serial.printf("[Main] You asked: \"%s\"\n", line.c_str());
                    robotFace.setStatusText("");
                    robotFace.setExpression(FACE_THINKING);
                    robotFace.draw();
                    statusLed.setPattern(PATTERN_FAST_BLINK);
                    currentState = STATE_SENDING;
                }
            }
#else
            // PTT press edge -> start recording
            if (ptt && !pttPrev) {
                if (!wifiManager.isConnected()) {
                    goError("No Wi-Fi");
                } else {
                    sessionManager.recordActivity();
                    audioInput.startRecording();
                    recordStart = millis();
                    robotFace.setStatusText("");
                    robotFace.setExpression(FACE_LISTENING);
                    statusLed.setPattern(PATTERN_FAST_BLINK);
                    currentState = STATE_RECORDING;
                }
            }
#endif
            break;
        }

#if !USE_SERIAL_TEXT_INPUT
        case STATE_RECORDING: {
            audioInput.loop();                 // pump capture EVERY iteration

            // Throttle the slow TFT redraw so it can't overflow the mic DMA.
            if (millis() - lastFaceDraw > FACE_FRAME_MS) {
                robotFace.update();
                robotFace.draw();
                lastFaceDraw = millis();
            }

            bool maxed = (millis() - recordStart) > (MAX_RECORD_SECONDS * 1000UL);
            if ((!ptt && pttPrev) || maxed) {  // release edge or timeout
                audioInput.loop();             // final drain of the DMA ring
                audioInput.stopRecording();
                size_t bytes = audioInput.getRecordedBytes();
                if (bytes >= MIN_RECORD_BYTES) {
                    robotFace.setExpression(FACE_THINKING);
                    robotFace.draw();
                    statusLed.setPattern(PATTERN_FAST_BLINK);
                    currentState = STATE_SENDING;
                } else {
                    Serial.println("[Main] recording too short, ignoring");
                    goIdle();
                }
            }
            break;
        }
#endif

        case STATE_SENDING: {
            String token = sessionManager.getToken();   // empty = public
#if USE_SERIAL_TEXT_INPUT
            VoiceChatResult r = apiClient.postTextChat(
                pendingText, sessionManager.getSessionId(), token);

            // Token expired -> retry once as public
            if (r.httpCode == 401 && !token.isEmpty()) {
                Serial.println("[Main] 401 -> retry public");
                sessionManager.clearAuth();
                r = apiClient.postTextChat(pendingText, "", "");
            }
#else
            size_t wavLen = 0;
            uint8_t* wav = audioInput.getWavData(&wavLen);
            if (!wav || wavLen == 0) { goError("No audio"); break; }

            VoiceChatResult r = apiClient.postVoiceChat(
                wav, wavLen, sessionManager.getSessionId(), token);
            free(wav);

            // Token expired -> retry once as public
            if (r.httpCode == 401 && !token.isEmpty()) {
                Serial.println("[Main] 401 -> retry public");
                sessionManager.clearAuth();
                wav = audioInput.getWavData(&wavLen);
                if (wav) {
                    r = apiClient.postVoiceChat(wav, wavLen, "", "");
                    free(wav);
                }
            }
#endif

            if (r.success && r.mp3Data && r.mp3Length > 0) {
                if (!r.sessionId.isEmpty()) sessionManager.setSessionId(r.sessionId);
                Serial.printf("[Main] Answering: \"%s\"\n", r.transcription.c_str());
                robotFace.setStatusText(shorten(r.transcription, 28));
                robotFace.setExpression(FACE_SPEAKING);
                robotFace.draw();            // do the costly FULL redraw now,
                lastFaceDraw = millis();     // before audio starts competing
                statusLed.setPattern(PATTERN_DOUBLE_BLINK);
                audioOutput.playFromBuffer(r.mp3Data, r.mp3Length);  // takes ownership
                currentState = STATE_PLAYING;
            } else {
                if (r.mp3Data) free(r.mp3Data);
                goError(r.error.length() ? r.error.c_str() : "Request failed");
            }
            break;
        }

        case STATE_PLAYING: {
            audioOutput.loop();                // pump decoder EVERY iteration

            // Throttle the slow TFT redraw so it can't underflow the speaker
            // DMA. Pump the decoder again right after the redraw to refill the
            // ring that drained while we were drawing.
            if (millis() - lastFaceDraw > FACE_FRAME_MS) {
                robotFace.update();
                robotFace.draw();
                lastFaceDraw = millis();
                audioOutput.loop();
            }
            if (!audioOutput.isPlaying()) {
                Serial.println("[Main] playback done");
                sessionManager.recordActivity();
                goIdle();
            }
            break;
        }

        case STATE_AUTH_PENDING:
            break;   // handled synchronously in handleRfidTap()

        case STATE_ERROR:
            robotFace.update();
            robotFace.draw();
            if (millis() > errorUntil) goIdle();
            break;
    }

    pttPrev = ptt;   // remember this frame's button state for edge detection
    delay(5);
}

// ─── Return to the appropriate idle face ─────────────────
void goIdle() {
    bool authed = sessionManager.isAuthenticated();
    currentState = authed ? STATE_IDLE_AUTH : STATE_IDLE_UNAUTH;
    statusLed.setPattern(authed ? PATTERN_SOLID : PATTERN_OFF);
    robotFace.setExpression(FACE_IDLE);
    if (authed) {
        String n = sessionManager.getName();
        robotFace.setStatusText(n.length() ? n + " - " TALK_HINT : "");
    } else {
        robotFace.setStatusText("");   // default "How can I help?"
    }
    robotFace.forceRedraw();
}

// ─── Error face for 3s ───────────────────────────────────
void goError(const char* msg) {
    Serial.printf("[Main] ERROR: %s\n", msg);
    currentState = STATE_ERROR;
    errorUntil = millis() + 3000;
    statusLed.setPattern(PATTERN_ERROR);
    robotFace.setExpression(FACE_ERROR);
    robotFace.setStatusText(msg);
    robotFace.forceRedraw();
}

// ─── RFID tap handling ───────────────────────────────────
void handleRfidTap(const String& uid) {
    Serial.printf("[Main] card: %s\n", uid.c_str());

    if (sessionManager.isAuthenticated()) {
        if (uid == sessionManager.getAuthenticatedUid()) {
            Serial.println("[Main] same card -> logout");
            sessionManager.clearAuth();
            goIdle();
            return;
        }
        sessionManager.clearAuth();   // different card -> switch user
    }

    if (!wifiManager.isConnected()) { goError("No Wi-Fi"); return; }

    currentState = STATE_AUTH_PENDING;
    statusLed.setPattern(PATTERN_FAST_BLINK);
    robotFace.setExpression(FACE_THINKING);
    robotFace.setStatusText("Authenticating...");
    robotFace.forceRedraw();
    robotFace.draw();

    AuthResult res = apiClient.postRfidAuth(uid);
    if (!res.success) { goError(res.error.length() ? res.error.c_str() : "Auth failed"); return; }

    sessionManager.setAuth(res.token, res.role, uid);

    // Greet by name (students only have a profile name)
    String name;
    if (res.role == "student") name = apiClient.getStudentName(res.token);
    sessionManager.setName(name);

    statusLed.setPattern(PATTERN_AUTH_SUCCESS);
    robotFace.setExpression(FACE_HAPPY);
    robotFace.setStatusText(name.length() ? "Hi, " + name + "!" : "Welcome!");
    robotFace.forceRedraw();
    greetUntil = millis() + 2500;
    currentState = STATE_IDLE_AUTH;
    Serial.printf("[Main] authenticated as %s (%s)\n",
                  name.length() ? name.c_str() : "user", res.role.c_str());
}

// ─── Debounced PTT state (true while held). Does NOT touch pttPrev;
//     loop() updates pttPrev once per frame so edge tests stay valid. ──
bool pttPressed() {
    static bool stable = false;       // false = released
    static bool lastRaw = false;
    static unsigned long t = 0;
    bool raw = (digitalRead(PTT_BUTTON_PIN) == LOW);   // active LOW
    if (raw != lastRaw) { lastRaw = raw; t = millis(); }
    if (millis() - t > 30) stable = raw;
    return stable;
}

// ─── Non-blocking serial line reader ─────────────────────
// Accumulates characters from the USB serial monitor until Enter (\n). Returns
// true once when a complete, non-empty line is ready (trimmed) in `out`.
bool readSerialLine(String& out) {
    static String buf;
    while (Serial.available()) {
        char c = (char)Serial.read();
        if (c == '\r') continue;              // ignore CR (CRLF line endings)
        if (c == '\n') {
            out = buf;
            buf = "";
            out.trim();
            if (out.length() > 0) return true;
            return false;                     // blank line -> ignore
        }
        buf += c;
        if (buf.length() > 200) buf = "";     // guard against runaway input
    }
    return false;
}

// ─── Truncate a string for the status line ───────────────
String shorten(const String& s, size_t n) {
    if (s.length() <= n) return s;
    return s.substring(0, n) + "...";
}
