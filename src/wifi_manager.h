#pragma once

#include <Arduino.h>
#include <WiFi.h>

class WifiManager {
public:
    // Initialize WiFi and start connection
    void begin();

    // Check if currently connected to WiFi
    bool isConnected();

    // Ensure we're connected, reconnect if needed (non-blocking)
    void ensureConnected();

    // Get current IP address as string
    String getIP();

    // Check if WiFi is physically initialized (even if not connected)
    bool isInitialized() { return _initialized; }

private:
    // Start WiFi connection (non-blocking, returns immediately)
    void startConnection();

    // Track if we've ever connected (for event handling)
    static bool _everConnected;
    static bool _initialized;
    static unsigned long _lastReconnectAttempt;
    static const unsigned long RECONNECT_INTERVAL = 5000;  // 5 seconds

    // WiFi event handler
    static void onWiFiEvent(WiFiEvent_t event);
};
