#include "wifi_manager.h"
#include "config.h"
#include <WiFi.h>
#include <esp_wifi.h>

// Static member initialization
bool WifiManager::_everConnected = false;
bool WifiManager::_initialized = false;
unsigned long WifiManager::_lastReconnectAttempt = 0;

// WiFi event handler - called by the WiFi driver
void WifiManager::onWiFiEvent(WiFiEvent_t event) {
    switch (event) {
        case ARDUINO_EVENT_WIFI_STA_START:
            Serial.println("[WiFi] Station started");
            break;

        case ARDUINO_EVENT_WIFI_STA_CONNECTED:
            Serial.println("[WiFi] Connected to AP!");
            break;

        case ARDUINO_EVENT_WIFI_STA_GOT_IP:
            Serial.printf("[WiFi] Got IP: %s\n", WiFi.localIP().toString().c_str());
            Serial.printf("[WiFi] Gateway: %s  Signal: %d dBm\n",
                         WiFi.gatewayIP().toString().c_str(), WiFi.RSSI());

            // DISABLE WiFi low power mode (needed for reliable web server)
            WiFi.setSleep(false);
            Serial.println("[WiFi] Low power mode DISABLED (web server needs responsive WiFi)");
            break;

        case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
            Serial.println("[WiFi] Disconnected from AP");
            break;

        case ARDUINO_EVENT_WIFI_STA_STOP:
            Serial.println("[WiFi] Station stopped");
            break;

        default:
            break;
    }
}

void WifiManager::begin() {
    Serial.println("[WiFi] Initializing...");

    // ─── FIX #2: Pre-configure WiFi BEFORE any RF operations ─────────────

    // 1. Set WiFi mode FIRST (but no RF yet)
    WiFi.mode(WIFI_STA);
    delay(200);

    // 1b. Unlock 2.4GHz channels 12-13. The default country policy limits the
    //     radio to channels 1-11, so an AP that auto-selected ch 12/13 shows up
    //     as NO_AP_FOUND even though it's a valid 2.4GHz network. MANUAL policy
    //     with nchan=13 lets the board scan/join the full 1-13 range.
    wifi_country_t country = { .cc = "01", .schan = 1, .nchan = 13,
                               .max_tx_power = 84,
                               .policy = WIFI_COUNTRY_POLICY_MANUAL };
    esp_wifi_set_country(&country);
    delay(50);

    // 2. Disable persistence (faster, no flash wear)
    WiFi.persistent(false);
    delay(50);

    // 3. Disconnect any previous connection
    WiFi.disconnect(false);
    delay(100);

    // 4. Set TX power BEFORE RF starts. 2dBm (minimum) is too weak to reliably
    //    scan/associate at distance (NO_AP_FOUND); 8.5dBm is a safe middle
    //    ground still gentle on the 3V3 LDO.
    WiFi.setTxPower(WIFI_POWER_8_5dBm);
    delay(100);

    // 5. Disable low power mode (web server needs responsive WiFi)
    WiFi.setSleep(false);
    delay(100);

    // 6. Register event handler
    WiFi.onEvent(onWiFiEvent);

    Serial.printf("[WiFi] Pre-configured: TX=8.5dBm, Sleep=DISABLED, Mode=STA\n");
    Serial.printf("[WiFi] SSID: %s\n", WIFI_SSID);

    // Start connection (with minimal power spike due to pre-configuration)
    startConnection();
    _initialized = true;
}

bool WifiManager::isConnected() {
    return (WiFi.status() == WL_CONNECTED);
}

String WifiManager::getIP() {
    if (isConnected()) {
        return WiFi.localIP().toString();
    }
    return "0.0.0.0";
}

void WifiManager::startConnection() {
    Serial.print("[WiFi] Connecting to ");
    Serial.print(WIFI_SSID);
    Serial.println("...");

    // ─── FIX #2: Pre-configure WiFi power settings BEFORE begin() ──────────
    // These settings MUST be applied BEFORE WiFi.begin() to reduce initial power spike

    // Set TX power first (reduces RF power during calibration)
    WiFi.setTxPower(WIFI_POWER_8_5dBm);
    delay(50);

    // Disable low power mode (web server needs responsive WiFi)
    WiFi.setSleep(false);
    delay(50);

    // NOW start connection (with minimal power spike)
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    _lastReconnectAttempt = millis();

    // Block until connected (with timeout)
    unsigned long timeout = 30000; // 30 second timeout
    unsigned long start = millis();

    while (WiFi.status() != WL_CONNECTED && millis() - start < timeout) {
        delay(100);
        yield();
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("[WiFi] Connected! IP: %s, RSSI: %d dBm\n",
                     WiFi.localIP().toString().c_str(), WiFi.RSSI());
        _everConnected = true;

        // Ensure low power mode is disabled for web server
        WiFi.setSleep(false);
        Serial.println("[WiFi] Low power mode DISABLED (web server needs responsive WiFi)");
    } else {
        Serial.println("[WiFi] Connection timeout!");
    }
}

void WifiManager::ensureConnected() {
    wl_status_t status = WiFi.status();

    // If connected, all good
    if (status == WL_CONNECTED) {
        if (!_everConnected) {
            Serial.printf("[WiFi] Connected! IP: %s, RSSI: %d dBm\n",
                         getIP().c_str(), WiFi.RSSI());
            _everConnected = true;
        }
        return;
    }

    // Print status occasionally (every 30s, not 10s - less spam)
    static unsigned long lastStatusPrint = 0;
    if (millis() - lastStatusPrint > 30000) {
        const char* statusStr = "Unknown";
        switch (status) {
            case WL_IDLE_STATUS: statusStr = "Idle"; break;
            case WL_NO_SSID_AVAIL: statusStr = "No SSID"; break;
            case WL_SCAN_COMPLETED: statusStr = "Scan complete"; break;
            case WL_CONNECT_FAILED: statusStr = "Connect failed"; break;
            case WL_CONNECTION_LOST: statusStr = "Connection lost"; break;
            case WL_DISCONNECTED: statusStr = "Disconnected"; break;
            default: statusStr = "Unknown"; break;
        }
        Serial.printf("[WiFi] Status: %s (%d) - retrying...\n", statusStr, status);
        lastStatusPrint = millis();
    }

    // If disconnected and enough time passed since last attempt, try reconnecting
    unsigned long now = millis();
    if (now - _lastReconnectAttempt >= RECONNECT_INTERVAL) {
        Serial.println("[WiFi] Reconnecting...");
        startConnection();  // This now blocks until connected or timeout
    }
}
