#pragma once

#include <Arduino.h>
#include "display.h"

// Forward declaration
class TFT_eSPI;

// ─── Face Expressions ───────────────────────────────────────
enum FaceExpression {
    FACE_IDLE,           // Neutral, slow blink
    FACE_LISTENING,      // Eyes wide, focused
    FACE_THINKING,       // One eye squint, pulsing color
    FACE_SPEAKING,       // Mouth animating
    FACE_HAPPY,          // Curved eyes/mouth, bounce
    FACE_SAD,            // Droopy, blue
    FACE_ERROR,          // Red, concerned
    FACE_CONFUSED        // Tilted, question mark
};

// ─── Display Modes ───────────────────────────────────────────
enum DisplayMode {
    MODE_NORMAL,         // Original UI screens
    MODE_ROBOT           // Robot face
};

// ─── Dirty Rectangle for Partial Updates ───────────────────
struct DirtyRect {
    int16_t x, y, w, h;
    bool dirty;
};

// ─── Robot Face Class ────────────────────────────────────────
class RobotFace {
public:
    RobotFace(Display& display);
    ~RobotFace();

    // Initialize PSRAM framebuffer (call from setup after Serial.begin)
    void init();

    // State control
    void setExpression(FaceExpression expr);
    void setSpeaking(bool speaking);
    void setListening(bool listening);
    void setThinking(bool thinking);

    // Main update and draw
    void update();
    void draw();

    // Display mode toggle
    static DisplayMode getDisplayMode() { return _displayMode; }
    static void toggleDisplayMode();
    static void setDisplayMode(DisplayMode m);

    // Force a full redraw on the next draw() (after returning from a text screen)
    void forceRedraw() { _needsFullRedraw = true; }

    // Override the status line (e.g. "Hi, Mostafa!"). Empty = default per-expression text.
    void setStatusText(const String& text);

    // Diagnostics
    void printStatus();

private:
    Display& _display;
    TFT_eSPI* _tft;  // Direct access for fast drawing

    // === DOUBLE BUFFERING (PSRAM) ===
    static const int16_t SCREEN_WIDTH = 480;
    static const int16_t SCREEN_HEIGHT = 320;
    uint16_t* _framebuffer;
    bool _useDoubleBuffer;
    bool _framebufferDirty;
    FaceExpression _lastDrawnExpression;

    // Current state
    FaceExpression _expression;
    bool _speaking;
    bool _listening;
    bool _thinking;

    // Animation timing
    unsigned long _lastBlink;
    unsigned long _lastMouthUpdate;
    unsigned long _lastEyeMove;
    unsigned long _lastBreathe;

    // Blink state
    bool _isBlinking;
    unsigned long _blinkStart;
    static const unsigned long BLINK_DURATION = 150;
    static const unsigned long BLINK_INTERVAL_MIN = 3000;
    static const unsigned long BLINK_INTERVAL_MAX = 5000;

    // Eye properties
    struct Eye {
        int16_t x, y;           // Center position
        int16_t radius;         // Eye size
        int16_t pupilX, pupilY;  // Pupil offset
        uint16_t color;         // Eye color
        bool squint;            // One eye squint (for thinking)
        int16_t pupilTargetX, pupilTargetY;  // For smooth eye movement

        // Previous state for partial updates
        int16_t lastRadius;
        int16_t lastPupilX, lastPupilY;
        bool lastSquint;
    } _leftEye, _rightEye;

    // Mouth properties
    struct Mouth {
        int16_t x, y;           // Center position
        int16_t width, height;  // Base size
        int16_t currentHeight;  // Animated height
        uint16_t color;

        // Previous state for partial updates
        int16_t lastHeight;
        int16_t lastWidth;
    } _mouth;

    // Animation helpers
    void updateBlink();
    void updateEyes();
    void updateMouth();
    void updateBreathing();

    // === DRAWING TO FRAMEBUFFER ===
    void drawToFramebuffer();
    void pushFramebuffer();

    // Drawing helpers (draw to framebuffer or TFT)
    void drawFaceFrame();
    void drawDetailedEye(const Eye& eye, bool isLeft);
    void drawEyebrow(int16_t x, int16_t y, bool isLeft);
    void drawDetailedMouth();
    void drawDecorations();
    void drawStatusText();

    // === PARTIAL UPDATE HELPERS ===
    void markDirty(int16_t x, int16_t y, int16_t w, int16_t h);
    void clearDirtyRect(const DirtyRect& rect, uint16_t bgColor);
    void pushDirtyRects();

    // Dirty rectangles for partial updates
    DirtyRect _dirtyLeftEye;
    DirtyRect _dirtyRightEye;
    DirtyRect _dirtyMouth;
    DirtyRect _dirtyStatus;
    DirtyRect _dirtyDecorations;

    // Shape helpers
    void drawEllipse(int16_t x, int16_t y, int16_t rx, int16_t ry, uint16_t color);
    void fillEllipse(int16_t x, int16_t y, int16_t rx, int16_t ry, uint16_t color);
    void drawArc(int16_t cx, int16_t cy, int16_t r, int16_t startAngle, int16_t endAngle,
                 int16_t thickness, uint16_t color);

    // === FRAMEBUFFER DRAWING PRIMITIVES ===
    void fb_setPixel(int16_t x, int16_t y, uint16_t color);
    void fb_drawFastHLine(int16_t x, int16_t y, int16_t w, uint16_t color);
    void fb_fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);
    void fb_fillCircle(int16_t x, int16_t y, int16_t r, uint16_t color);
    void fb_drawCircle(int16_t x, int16_t y, int16_t r, uint16_t color);

    // Color helpers
    uint16_t getBackgroundColor();
    uint16_t getFrameColor();
    uint16_t getExpressionColor();
    const char* getStatusText();
    uint16_t dimColor(uint16_t color, uint8_t percent);
    uint16_t brightenColor(uint16_t color, uint8_t percent);
    int16_t lerp(int16_t a, int16_t b, float t);

    // Status line override (empty = use per-expression default)
    String _statusOverride;

    // Static display mode
    static DisplayMode _displayMode;

    // Eye movement
    int16_t _eyeMovePhase;

    // Full redraw needed (static - shared for mode toggles)
    static bool _needsFullRedraw;
};
