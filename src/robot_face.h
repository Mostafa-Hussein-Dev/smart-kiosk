#pragma once

#include <Arduino.h>
#include "display.h"

// Forward declaration
class TFT_eSPI;

// ─── Face Expressions ───────────────────────────────────────
// Bright-blue rounded-bar eyes (Vector/Cozmo style). Expression is carried by
// the SHAPE of the two bars plus the waveform strip.
enum FaceExpression {
    FACE_IDLE,        // tall bars, breathing + slow drift + blink
    FACE_LISTENING,   // wider/alert bars + ambient waveform
    FACE_THINKING,    // narrowed bars, looking up & aside, animated dots
    FACE_SPEAKING,    // steady bars + pulsing waveform
    FACE_HAPPY,       // eyes curve up into "^ ^"
    FACE_SAD,         // droopy (kept for API completeness; maps to idle-ish)
    FACE_ERROR,       // flat red bars
    FACE_CONFUSED,    // maps to thinking
};

// ─── Display Modes ───────────────────────────────────────────
enum DisplayMode {
    MODE_NORMAL,      // console / welcome / other direct-draw screens
    MODE_ROBOT,       // animated eyes
};

// ─── Robot Face ─────────────────────────────────────────────
class RobotFace {
public:
    RobotFace(Display& display);
    ~RobotFace();

    // Allocate the PSRAM framebuffer (call from setup after PSRAM is up).
    void init();

    // State control
    void setExpression(FaceExpression expr);
    void setStatusText(const String& text);   // bottom line ("" = per-expression default)
    void setUser(const String& name);          // status-band user ("" = Guest)
    void setWifiConnected(bool connected);      // status-band Wi-Fi indicator

    // Per-frame animation + render
    void update();
    void draw();

    // Force a full repaint on the next draw() (e.g. after a non-face screen).
    void forceRedraw() { _needsFullRedraw = true; }

    // Display mode
    static DisplayMode getDisplayMode() { return _displayMode; }
    static void setDisplayMode(DisplayMode m);

private:
    Display& _display;
    TFT_eSPI* _tft;

    static const int16_t SCREEN_WIDTH  = 480;
    static const int16_t SCREEN_HEIGHT = 320;
    uint16_t* _framebuffer;      // full-screen RGB565 in PSRAM (nullptr = direct draw)

    FaceExpression _expression;
    FaceExpression _lastDrawnExpression;

    // ── animated eye model (all lerped toward the current expression's target) ──
    struct EyeShape { float w, h, r, dy; uint16_t color; uint8_t kind; }; // kind: 0=bar,1=happy
    EyeShape _cur;

    float _lookX, _lookY, _tgtLookX, _tgtLookY;
    unsigned long _nextLook;

    float _blink;                // 1.0 = open, ~0.12 = closed
    long  _blinkT;               // -1 = not blinking, else ms into blink
    unsigned long _nextBlink;

    // status band / text state
    String _statusOverride;
    String _user;
    bool   _wifi;
    bool   _bandDirty;
    String _lastBandActivity;
    int    _lastDotCount;
    unsigned long _t0;           // animation clock origin

    static DisplayMode _displayMode;
    static bool _needsFullRedraw;

    // ── rendering ──
    void targetFor(FaceExpression e, EyeShape& out);
    void drawEyeRegion(int16_t cx, const EyeShape& ds);   // clear + draw one eye's region + push
    void drawBarInto(int16_t cx, const EyeShape& s, float blink);
    void drawHappyInto(int16_t cx, const EyeShape& s, float blink);
    void drawWave(float amp);
    void drawBand();
    void drawBottomText(const char* str);
    void drawThinkingDots();
    const char* activityLabel();

    // framebuffer primitives
    void fb_fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);
    void fb_fillCircle(int16_t x, int16_t y, int16_t r, uint16_t color);
    void fb_fillRoundRect(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, uint16_t color);
    void pushRegion(int16_t x, int16_t y, int16_t w, int16_t h);

    static float lerp(float a, float b, float k) { return a + (b - a) * k; }
    static float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
};
