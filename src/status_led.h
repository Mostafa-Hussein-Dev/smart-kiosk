#pragma once

#include <Arduino.h>

enum LedPattern {
    PATTERN_OFF,            // LED off (idle, unauthenticated)
    PATTERN_SOLID,          // LED solid green (idle, authenticated)
    PATTERN_SLOW_BLINK,     // Slow blue blink (connecting Wi-Fi)
    PATTERN_FAST_BLINK,     // Fast blue blink (processing / recording)
    PATTERN_DOUBLE_BLINK,   // Double cyan blink (playing audio)
    PATTERN_AUTH_SUCCESS,    // Green flash then solid green
    PATTERN_ERROR,          // Red flash then off
};

class StatusLed {
public:
    void begin();
    void update();
    void setPattern(LedPattern pattern);
    LedPattern getPattern();

private:
    LedPattern _pattern;
    LedPattern _previousPattern;
    bool       _ledOn;
    unsigned long _lastToggle;
    uint8_t    _flashCount;
    unsigned long _patternStart;

    void setColor(uint8_t r, uint8_t g, uint8_t b);
    void off();
};
