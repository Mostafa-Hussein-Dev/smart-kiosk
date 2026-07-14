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
#include "wifi_manager.h"
#include "api_client.h"
#include "rfid_reader.h"
#include "session_manager.h"
#include "audio_input.h"
#include "audio_output.h"
#include "display.h"
#include "robot_face.h"
#include "ui_console.h"

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

#define TALK_HINT "hold to talk"

// PTT edge detection (debounced)
bool pttPrev = false;

// ─── Forward declarations ────────────────────────────────
void handleRfidTap(const String& uid);
void goIdle();
void goError(const char* msg);
bool pttPressed();
String shorten(const String& s, size_t n);
String urlDecode(const String& s);

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

    // ── Bring the display up FIRST so the whole boot sequence is visible as an
    //    on-screen console. The CPU stays at 80 MHz through Wi-Fi bring-up to
    //    keep the 3.3 V rail calm during the radio inrush (the brownout DETECTOR
    //    is already disabled above; the display backlight is the only extra
    //    load, and that's fine on a good 5 V supply). ──
    setCpuFrequencyMhz(80);
    delay(400);
    display.begin();                          // clean black panel (no color test)
    digitalWrite(TFT_LED_PIN, TFT_BACKLIGHT_ON);

    BootConsole console(display);
    console.begin("SMART UNIVERSITY ASSISTANT");

    console.step("Display  ILI9488");
    console.ok("480x320");

    console.step("PSRAM  8MB OPI");
    if (psramFound()) {
        char d[16]; snprintf(d, sizeof(d), "%u KB", ESP.getFreePsram() / 1024);
        console.ok(d);
    } else {
        console.warn("none");
    }

    console.step("Wi-Fi");
    wifiManager.begin();                       // blocks up to ~30s
    if (wifiManager.isConnected()) console.ok(wifiManager.getIP().c_str());
    else                           console.fail("offline");

    setCpuFrequencyMhz(240);                   // radio settled — full speed now

    console.step("Backend");
    if (wifiManager.isConnected()) console.ok(apiClient.healthCheck() ? "online" : "waking");
    else                           console.warn("no wifi");

    console.step("RFID  RC522");
    rfidReader.begin();
    console.ok();

    console.step("Microphone  INMP441");
    if (audioInput.begin()) console.ok();
    else                    console.warn("init failed");

    console.step("Speaker  MAX98357A");
    audioOutput.begin();                       // warms up the amp (kills first-PTT pop)
    console.ok("warmup");

    console.done("ready.");
    robotFace.init();
    for (int i = 0; i < 10; i++) { delay(100); yield(); }   // ~1s to read the log

    // Welcome / home screen, then hand the panel to the robot face
    display.showSplashScreen();

    pinMode(PTT_BUTTON_PIN, INPUT_PULLUP);
    pinMode(PTT_LED_PIN, OUTPUT);
    digitalWrite(PTT_LED_PIN, !PTT_LED_ON);    // ring off until pressed

    robotFace.setWifiConnected(wifiManager.isConnected());
    RobotFace::setDisplayMode(MODE_ROBOT);
    goIdle();

    Serial.println("[Main] Ready.");
    Serial.println("[Main] Hold the PTT button and speak.\n");
}

// ─── Main loop ───────────────────────────────────────────
void loop() {
    sessionManager.checkTimeouts();

    // Session expired while authenticated -> drop to unauth idle
    if (currentState == STATE_IDLE_AUTH && !sessionManager.isAuthenticated()) {
        Serial.println("[Main] session expired");
        goIdle();
    }

    bool ptt = pttPressed();
    digitalWrite(PTT_LED_PIN, ptt ? PTT_LED_ON : !PTT_LED_ON);   // ring glows while held

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
                robotFace.setWifiConnected(wifiManager.isConnected());
            }

            // RFID tap?
            rfidReader.poll();
            if (rfidReader.cardDetected()) {
                handleRfidTap(rfidReader.getLastUID());
                break;
            }

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
                    currentState = STATE_RECORDING;
                }
            }
            break;
        }

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
                    currentState = STATE_SENDING;
                } else {
                    Serial.println("[Main] recording too short, ignoring");
                    goIdle();
                }
            }
            break;
        }

        case STATE_SENDING: {
            String token = sessionManager.getToken();   // empty = public
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

            if (r.success && r.mp3Data && r.mp3Length > 0) {
                if (!r.sessionId.isEmpty()) sessionManager.setSessionId(r.sessionId);
                String said = urlDecode(r.transcription);
                Serial.printf("[Main] Answering: \"%s\"\n", said.c_str());
                robotFace.setStatusText(shorten(said, 28));
                robotFace.setExpression(FACE_SPEAKING);
                robotFace.draw();            // do the costly FULL redraw now,
                lastFaceDraw = millis();     // before audio starts competing
                audioOutput.playFromBuffer(r.mp3Data, r.mp3Length);  // takes ownership
                currentState = STATE_PLAYING;
            } else {
                if (r.mp3Data) free(r.mp3Data);
                goError(r.error.length() ? r.error.c_str() : "Request failed");
            }
            break;
        }

        case STATE_PLAYING: {
            // The MP3 decoder now runs on its own high-priority task, so the
            // face redraw can no longer starve the speaker DMA. Just animate the
            // mouth here; the background task keeps the I2S ring fed even while
            // this ~40 ms TFT push is blocking loopTask.
            if (millis() - lastFaceDraw > FACE_FRAME_MS) {
                robotFace.update();
                robotFace.draw();
                lastFaceDraw = millis();
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
    robotFace.setExpression(FACE_IDLE);
    if (authed) {
        String n = sessionManager.getName();
        robotFace.setUser(n.length() ? n : String("Signed in"));
        robotFace.setStatusText(n.length() ? n + " - " TALK_HINT : "");
    } else {
        robotFace.setUser("");         // -> "Guest" in the status band
        robotFace.setStatusText("");   // -> default "How can I help?"
    }
    robotFace.forceRedraw();
}

// ─── Error face for 3s ───────────────────────────────────
void goError(const char* msg) {
    Serial.printf("[Main] ERROR: %s\n", msg);
    currentState = STATE_ERROR;
    errorUntil = millis() + 3000;
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

    robotFace.setUser(name.length() ? name : String("Signed in"));
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

// ─── Truncate a string for the status line ───────────────
String shorten(const String& s, size_t n) {
    if (s.length() <= n) return s;
    return s.substring(0, n) + "...";
}

// ─── Percent-decode the transcription header (spaces %20, UTF-8 %XX) ──
static int hexVal(char h) {
    if (h >= '0' && h <= '9') return h - '0';
    if (h >= 'a' && h <= 'f') return h - 'a' + 10;
    if (h >= 'A' && h <= 'F') return h - 'A' + 10;
    return -1;
}
String urlDecode(const String& s) {
    String out;
    out.reserve(s.length());
    for (size_t i = 0; i < s.length(); i++) {
        char c = s[i];
        if (c == '%' && i + 2 < s.length()) {
            int hi = hexVal(s[i + 1]), lo = hexVal(s[i + 2]);
            if (hi >= 0 && lo >= 0) { out += (char)((hi << 4) | lo); i += 2; continue; }
        }
        if (c == '+') out += ' ';
        else          out += c;
    }
    return out;
}
