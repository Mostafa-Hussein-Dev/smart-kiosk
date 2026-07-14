#pragma once

#include <Arduino.h>
#include "display.h"

// On-screen boot console: renders the startup sequence as a terminal-style log
// with [ OK ]/[WARN]/[FAIL] status tags, and mirrors every line to Serial. This
// is the "serial logs on screen" view shown while the kiosk boots.
class BootConsole {
public:
    BootConsole(Display& display);

    // Clear the screen and draw the header. Call once at the start of boot.
    void begin(const char* title);

    // Print "<label> ....... [ .. ]" (pending) and remember the line so the
    // matching ok()/warn()/fail() can fill in the tag. Also prints to Serial.
    void step(const char* label);

    // Resolve the current step's tag (with optional trailing detail text).
    void ok(const char* detail = "");
    void warn(const char* detail = "");
    void fail(const char* detail = "");

    // Final line, e.g. "ready.".
    void done(const char* msg = "ready.");

private:
    Display& _display;
    class TFT_eSPI* _tft;
    int16_t _y;        // y of the next line
    int16_t _tagY;     // y of the pending step's line
    void tag(const char* label, uint16_t color, const char* detail);
};
