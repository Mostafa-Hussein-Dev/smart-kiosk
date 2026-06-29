#include "status_led.h"
#include "config.h"

// ESP32-S3 has neopixelWrite() built into the Arduino core
// neopixelWrite(pin, R, G, B) drives the onboard WS2812 RGB LED

void StatusLed::begin() {
    _pattern = PATTERN_OFF;
    _previousPattern = PATTERN_OFF;
    _ledOn = false;
    _lastToggle = 0;
    _flashCount = 0;
    _patternStart = 0;
    off();
}

void StatusLed::setColor(uint8_t r, uint8_t g, uint8_t b) {
    neopixelWrite(RGB_LED_PIN, r, g, b);
    _ledOn = (r > 0 || g > 0 || b > 0);
}

void StatusLed::off() {
    setColor(0, 0, 0);
}

void StatusLed::setPattern(LedPattern pattern) {
    if (pattern == _pattern) return;

    if (pattern == PATTERN_AUTH_SUCCESS || pattern == PATTERN_ERROR) {
        _previousPattern = _pattern;
    }

    _pattern = pattern;
    _flashCount = 0;
    _patternStart = millis();
    _lastToggle = millis();

    // Immediate state
    switch (pattern) {
        case PATTERN_OFF:
            off();
            break;
        case PATTERN_SOLID:
            setColor(0, 20, 0);  // dim green
            break;
        default:
            break;
    }
}

LedPattern StatusLed::getPattern() {
    return _pattern;
}

void StatusLed::update() {
    unsigned long now = millis();

    switch (_pattern) {
        case PATTERN_OFF:
        case PATTERN_SOLID:
            break;

        case PATTERN_SLOW_BLINK:
            // Blue slow blink (Wi-Fi connecting)
            if (now - _lastToggle >= 500) {
                if (_ledOn) {
                    off();
                } else {
                    setColor(0, 0, 20);  // blue
                }
                _lastToggle = now;
            }
            break;

        case PATTERN_FAST_BLINK:
            // Blue fast blink (processing)
            if (now - _lastToggle >= 150) {
                if (_ledOn) {
                    off();
                } else {
                    setColor(0, 0, 20);  // blue
                }
                _lastToggle = now;
            }
            break;

        case PATTERN_DOUBLE_BLINK:
            // Cyan double blink (playing audio)
            if (_flashCount < 4) {
                if (now - _lastToggle >= 100) {
                    if (_ledOn) {
                        off();
                    } else {
                        setColor(0, 15, 15);  // cyan
                    }
                    _lastToggle = now;
                    _flashCount++;
                }
            } else {
                if (now - _lastToggle >= 600) {
                    _flashCount = 0;
                    _lastToggle = now;
                }
            }
            break;

        case PATTERN_AUTH_SUCCESS:
            // Green triple flash, then solid green
            if (_flashCount < 6) {
                if (now - _lastToggle >= 100) {
                    if (_ledOn) {
                        off();
                    } else {
                        setColor(0, 30, 0);  // bright green
                    }
                    _lastToggle = now;
                    _flashCount++;
                }
            } else if (now - _patternStart >= 1000) {
                setPattern(PATTERN_SOLID);
            }
            break;

        case PATTERN_ERROR:
            // Red flash for 500ms, then off, restore after 2s
            if (now - _patternStart < 500) {
                if (!_ledOn) {
                    setColor(30, 0, 0);  // red
                }
            } else if (now - _patternStart < 2000) {
                if (_ledOn) {
                    off();
                }
            } else {
                setPattern(_previousPattern);
            }
            break;
    }
}
