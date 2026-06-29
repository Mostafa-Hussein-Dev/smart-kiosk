#include "api_client.h"
#include "config.h"
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <WiFiClientSecure.h>

// Network client — TLS for https:// backends, plain TCP for local http://.
static WiFiClient*       _plain  = nullptr;
static WiFiClientSecure* _secure = nullptr;

static WiFiClient& net() {
    bool useTls = String(BACKEND_URL).startsWith("https");
    if (useTls) {
        if (!_secure) {
            _secure = new WiFiClientSecure();
            _secure->setInsecure();   // skip cert validation (demo)
            _secure->setTimeout(20);
        }
        return *_secure;
    }
    if (!_plain) {
        _plain = new WiFiClient();
        _plain->setTimeout(20);
    }
    return *_plain;
}

bool ApiClient::healthCheck() {
    HTTPClient http;
    String url = String(BACKEND_URL) + "/health";
    http.begin(net(), url);
    http.setTimeout(8000);
    int code = http.GET();
    http.end();
    Serial.printf("[API] health -> %d\n", code);
    return code == 200;
}

AuthResult ApiClient::postRfidAuth(const String& rfidTag) {
    AuthResult result;
    result.success = false;
    result.httpCode = 0;

    HTTPClient http;
    String url = String(BACKEND_URL) + "/api/auth/rfid";
    http.begin(net(), url);
    http.setTimeout(12000);   // allow for backend cold-start
    http.addHeader("Content-Type", "application/json");

    JsonDocument req;
    req["rfid_tag"] = rfidTag;
    String body;
    serializeJson(req, body);

    Serial.printf("[API] POST /api/auth/rfid  tag=%s\n", rfidTag.c_str());
    int code = http.POST(body);
    result.httpCode = code;

    if (code == 200) {
        String resp = http.getString();
        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, resp);
        if (err) {
            result.error = "JSON parse error";
        } else if (doc["access_token"].is<const char*>()) {
            // TokenResponse: access_token + role (+ roles[]). No name here.
            result.success = true;
            result.token = doc["access_token"].as<String>();
            result.role  = doc["role"].as<String>();
            result.name  = "";   // filled later via getStudentName()
            Serial.printf("[API] auth OK role=%s\n", result.role.c_str());
        } else {
            // RoleSelectionResponse (multi-role) — not supported on the kiosk
            result.error = "Multiple roles - use web portal";
            Serial.println("[API] auth: multi-role user, kiosk unsupported");
        }
    } else if (code == 404) {
        result.error = "Card not registered";
    } else if (code == 401) {
        result.error = "Account deactivated";
    } else {
        result.error = "Server error " + String(code);
    }

    http.end();
    return result;
}

String ApiClient::getStudentName(const String& bearerToken) {
    if (bearerToken.isEmpty()) return "";

    HTTPClient http;
    String url = String(BACKEND_URL) + "/api/students/me/profile";
    http.begin(net(), url);
    http.setTimeout(10000);
    http.addHeader("Authorization", "Bearer " + bearerToken);

    int code = http.GET();
    String name = "";
    if (code == 200) {
        JsonDocument doc;
        if (!deserializeJson(doc, http.getString())) {
            String first = doc["first_name"].as<String>();
            String last  = doc["last_name"].as<String>();
            name = first;
            if (last.length()) { if (name.length()) name += " "; name += last; }
            name.trim();
        }
    }
    http.end();
    Serial.printf("[API] profile name -> '%s' (HTTP %d)\n", name.c_str(), code);
    return name;
}

VoiceChatResult ApiClient::postVoiceChat(
    const uint8_t* wavData,
    size_t wavLen,
    const String& sessionId,
    const String& bearerToken
) {
    VoiceChatResult result;
    result.success = false;
    result.httpCode = 0;
    result.mp3Data = nullptr;
    result.mp3Length = 0;

    bool useAuth = !bearerToken.isEmpty();
    String url = String(BACKEND_URL) + (useAuth ? "/api/voice/chat" : "/api/voice/chat/public");
    Serial.printf("[API] POST %s  wav=%u  auth=%d\n", url.c_str(), (unsigned)wavLen, useAuth);

    HTTPClient http;
    http.begin(net(), url);
    http.setTimeout(25000);   // STT + chat + TTS round trip (+cold start)
    if (useAuth) http.addHeader("Authorization", "Bearer " + bearerToken);

    const char* boundary = "----ESP32VoiceBoundary";

    // multipart head (audio file part) and tail (session_id part + close)
    String head;
    head  = "--"; head += boundary; head += "\r\n";
    head += "Content-Disposition: form-data; name=\"audio\"; filename=\"recording.wav\"\r\n";
    head += "Content-Type: audio/wav\r\n\r\n";

    String tail;
    tail  = "\r\n--"; tail += boundary; tail += "\r\n";
    tail += "Content-Disposition: form-data; name=\"session_id\"\r\n\r\n";
    tail += sessionId;
    tail += "\r\n--"; tail += boundary; tail += "--\r\n";

    // Exact-size body (fixes the previous fixed-100-byte footer overflow)
    size_t total = head.length() + wavLen + tail.length();
    uint8_t* buf = psramFound() ? (uint8_t*)ps_malloc(total) : (uint8_t*)malloc(total);
    if (!buf) {
        result.error = "OOM building request";
        http.end();
        return result;
    }
    size_t off = 0;
    memcpy(buf + off, head.c_str(), head.length()); off += head.length();
    memcpy(buf + off, wavData, wavLen);             off += wavLen;
    memcpy(buf + off, tail.c_str(), tail.length()); off += tail.length();

    String contentType = String("multipart/form-data; boundary=") + boundary;
    http.addHeader("Content-Type", contentType);

    // Capture response headers we care about
    const char* collect[] = { "X-Session-Id", "X-Transcription" };
    http.collectHeaders(collect, 2);

    int code = http.POST(buf, total);
    result.httpCode = code;
    free(buf);

    if (code == 200) {
        result.sessionId     = http.header("X-Session-Id");
        result.transcription = http.header("X-Transcription");
        int len = http.getSize();
        Serial.printf("[API] 200  mp3=%d  transcript='%s'\n", len, result.transcription.c_str());

        if (len > 0) {
            result.mp3Data = psramFound() ? (uint8_t*)ps_malloc(len) : (uint8_t*)malloc(len);
            if (result.mp3Data) {
                WiFiClient& stream = http.getStream();
                int got = 0;
                unsigned long start = millis();
                while (got < len && millis() - start < 25000) {
                    int n = stream.read(result.mp3Data + got, len - got);
                    if (n <= 0) { delay(5); continue; }
                    got += n;
                }
                result.mp3Length = got;
                result.success = (got == len);
                if (!result.success) {
                    Serial.printf("[API] short read %d/%d\n", got, len);
                    free(result.mp3Data); result.mp3Data = nullptr; result.mp3Length = 0;
                    result.error = "Incomplete audio";
                }
            } else {
                result.error = "OOM for MP3";
            }
        } else {
            result.success = true;   // 200 with no audio body (shouldn't happen)
        }
    } else if (code == 401) {
        result.error = "Unauthorized";
    } else {
        result.error = "HTTP " + String(code);
        Serial.printf("[API] voice failed: %s\n", result.error.c_str());
    }

    http.end();
    return result;
}
