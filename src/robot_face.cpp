#include "robot_face.h"
#include <math.h>
#include <TFT_eSPI.h>

// Static member initialization
DisplayMode RobotFace::_displayMode = MODE_NORMAL;
bool RobotFace::_needsFullRedraw = true;

RobotFace::RobotFace(Display& display)
    : _display(display)
    , _tft(display.getTft())
    , _framebuffer(nullptr)
    , _useDoubleBuffer(false)
    , _framebufferDirty(false)
    , _lastDrawnExpression(FACE_IDLE)
    , _expression(FACE_IDLE)
    , _speaking(false)
    , _listening(false)
    , _thinking(false)
    , _lastBlink(0)
    , _lastMouthUpdate(0)
    , _lastEyeMove(0)
    , _lastBreathe(0)
    , _isBlinking(false)
    , _blinkStart(0)
    , _eyeMovePhase(0)
{
    // Framebuffer allocation moved to init() - call from setup()
    // This ensures PSRAM is initialized before allocation

    // Initialize detailed face parameters
    _leftEye.x = 140;
    _leftEye.y = 130;
    _leftEye.radius = 55;
    _leftEye.pupilX = 0;
    _leftEye.pupilY = 0;
    _leftEye.pupilTargetX = 0;
    _leftEye.pupilTargetY = 0;
    _leftEye.color = TFT_CYAN;
    _leftEye.squint = false;
    _leftEye.lastRadius = 55;
    _leftEye.lastPupilX = 0;
    _leftEye.lastPupilY = 0;
    _leftEye.lastSquint = false;

    _rightEye.x = 340;
    _rightEye.y = 130;
    _rightEye.radius = 55;
    _rightEye.pupilX = 0;
    _rightEye.pupilY = 0;
    _rightEye.pupilTargetX = 0;
    _rightEye.pupilTargetY = 0;
    _rightEye.color = TFT_CYAN;
    _rightEye.squint = false;
    _rightEye.lastRadius = 55;
    _rightEye.lastPupilX = 0;
    _rightEye.lastPupilY = 0;
    _rightEye.lastSquint = false;

    _mouth.x = 240;
    _mouth.y = 250;
    _mouth.width = 140;
    _mouth.height = 12;
    _mouth.currentHeight = 12;
    _mouth.color = TFT_GREEN;
    _mouth.lastHeight = 12;
    _mouth.lastWidth = 140;

    _lastBlink = millis();

    // Initialize dirty rectangles
    _dirtyLeftEye = {140 - 70, 130 - 70, 140, 140, false};
    _dirtyRightEye = {340 - 70, 130 - 70, 140, 140, false};
    _dirtyMouth = {240 - 80, 250 - 50, 160, 100, false};
    _dirtyStatus = {0, 280, 480, 40, false};
    _dirtyDecorations = {0, 70, 480, 180, false};
}

RobotFace::~RobotFace() {
    if (_framebuffer) {
        free(_framebuffer);
    }
}

void RobotFace::init() {
    // Allocate framebuffer in PSRAM (call from setup after PSRAM is initialized)
    if (psramFound()) {
        size_t fbSize = SCREEN_WIDTH * SCREEN_HEIGHT * sizeof(uint16_t);
        _framebuffer = (uint16_t*)ps_malloc(fbSize);
        if (_framebuffer) {
            _useDoubleBuffer = true;
            Serial.printf("[RobotFace] Double buffering enabled (PSRAM: %d KB)\n", fbSize / 1024);
        } else {
            Serial.println("[RobotFace] PSRAM malloc failed, using direct draw");
        }
    } else {
        Serial.println("[RobotFace] No PSRAM found, using direct draw");
    }
}

// ============================================================================
// DIAGNOSTICS
// ============================================================================

void RobotFace::printStatus() {
    Serial.println("========================================");
    Serial.println("  Robot Face Status");
    Serial.println("========================================");
    Serial.printf("Double buffering: %s\n", _useDoubleBuffer ? "ENABLED" : "DISABLED");
    Serial.printf("Framebuffer allocated: %s\n", _framebuffer != nullptr ? "YES" : "NO");
    if (_framebuffer) {
        Serial.printf("Framebuffer size: %d bytes (%.1f KB)\n",
                      SCREEN_WIDTH * SCREEN_HEIGHT * 2,
                      (SCREEN_WIDTH * SCREEN_HEIGHT * 2) / 1024.0);
    }
    Serial.printf("Display mode: %s\n", _displayMode == MODE_ROBOT ? "ROBOT" : "NORMAL");
    Serial.println("========================================");
}

// ============================================================================
// STATE CONTROL
// ============================================================================

void RobotFace::setExpression(FaceExpression expr) {
    if (_expression != expr) {
        _expression = expr;
        _needsFullRedraw = true;
    }
}

void RobotFace::setSpeaking(bool speaking) {
    _speaking = speaking;
    if (speaking) {
        _expression = FACE_SPEAKING;
        _needsFullRedraw = true;
    } else if (_expression == FACE_SPEAKING) {
        _expression = FACE_IDLE;
        _needsFullRedraw = true;
    }
}

void RobotFace::setListening(bool listening) {
    _listening = listening;
    if (listening && _expression == FACE_IDLE) {
        _expression = FACE_LISTENING;
        _needsFullRedraw = true;
    } else if (!listening && _expression == FACE_LISTENING) {
        _expression = FACE_IDLE;
        _needsFullRedraw = true;
    }
}

void RobotFace::setThinking(bool thinking) {
    _thinking = thinking;
    if (thinking && _expression == FACE_IDLE) {
        _expression = FACE_THINKING;
        _needsFullRedraw = true;
    } else if (!thinking && _expression == FACE_THINKING) {
        _expression = FACE_IDLE;
        _needsFullRedraw = true;
    }
}

void RobotFace::toggleDisplayMode() {
    _displayMode = (_displayMode == MODE_NORMAL) ? MODE_ROBOT : MODE_NORMAL;
    if (_displayMode == MODE_ROBOT) {
        _needsFullRedraw = true;
    }
}

void RobotFace::setDisplayMode(DisplayMode m) {
    if (_displayMode != m) {
        _displayMode = m;
        if (m == MODE_ROBOT) _needsFullRedraw = true;
    }
}

void RobotFace::setStatusText(const String& text) {
    if (_statusOverride != text) {
        _statusOverride = text;
        _needsFullRedraw = true;   // status line only redraws on a full draw
    }
}

// ============================================================================
// UPDATE ANIMATIONS
// ============================================================================

void RobotFace::update() {
    if (_displayMode != MODE_ROBOT) return;

    unsigned long now = millis();

    updateBlink();
    updateEyes();
    updateMouth();
    updateBreathing();
}

void RobotFace::updateBlink() {
    unsigned long now = millis();

    unsigned long blinkInterval = BLINK_INTERVAL_MIN + 2000;
    if (_expression == FACE_THINKING) blinkInterval = 5000;
    if (_expression == FACE_LISTENING) blinkInterval = 2000;

    if (!_isBlinking && now - _lastBlink > blinkInterval) {
        _isBlinking = true;
        _blinkStart = now;
        _lastBlink = now;
        // Mark eyes dirty when blink starts
        markDirty(_leftEye.x - _leftEye.radius - 10, _leftEye.y - 20,
                  _leftEye.radius * 2 + 20, 40);
        markDirty(_rightEye.x - _rightEye.radius - 10, _rightEye.y - 20,
                  _rightEye.radius * 2 + 20, 40);
    }

    if (_isBlinking && now - _blinkStart > BLINK_DURATION) {
        _isBlinking = false;
    }
}

void RobotFace::updateEyes() {
    unsigned long now = millis();

    // Store previous state for dirty tracking
    _leftEye.lastRadius = _leftEye.radius;
    _leftEye.lastPupilX = _leftEye.pupilX;
    _leftEye.lastPupilY = _leftEye.pupilY;
    _leftEye.lastSquint = _leftEye.squint;

    _rightEye.lastRadius = _rightEye.radius;
    _rightEye.lastPupilX = _rightEye.pupilX;
    _rightEye.lastPupilY = _rightEye.pupilY;
    _rightEye.lastSquint = _rightEye.squint;

    if (_expression == FACE_LISTENING) {
        _leftEye.pupilX = lerp(_leftEye.pupilX, 0, 0.3f);
        _leftEye.pupilY = lerp(_leftEye.pupilY, 0, 0.3f);
        _rightEye.pupilX = lerp(_rightEye.pupilX, 0, 0.3f);
        _rightEye.pupilY = lerp(_rightEye.pupilY, 0, 0.3f);
        _leftEye.radius = 60;
        _rightEye.radius = 60;
    } else if (now - _lastEyeMove > 2000 + random(3000)) {
        _eyeMovePhase = (_eyeMovePhase + 1) % 4;
        _leftEye.pupilTargetX = random(-20, 20);
        _leftEye.pupilTargetY = random(-15, 15);
        _rightEye.pupilTargetX = random(-20, 20);
        _rightEye.pupilTargetY = random(-15, 15);
        _lastEyeMove = now;
    }

    float t = 0.15f;
    _leftEye.pupilX = lerp(_leftEye.pupilX, _leftEye.pupilTargetX, t);
    _leftEye.pupilY = lerp(_leftEye.pupilY, _leftEye.pupilTargetY, t);
    _rightEye.pupilX = lerp(_rightEye.pupilX, _rightEye.pupilTargetX, t);
    _rightEye.pupilY = lerp(_rightEye.pupilY, _rightEye.pupilTargetY, t);

    switch (_expression) {
        case FACE_THINKING:
            _leftEye.squint = true;
            _leftEye.radius = 52;
            _rightEye.radius = 48;
            break;
        case FACE_HAPPY:
            _leftEye.pupilY = -8;
            _rightEye.pupilY = -8;
            _leftEye.radius = 55;
            _rightEye.radius = 55;
            break;
        case FACE_SAD:
            _leftEye.pupilY = 8;
            _rightEye.pupilY = 8;
            _leftEye.radius = 48;
            _rightEye.radius = 48;
            break;
        case FACE_ERROR:
            _leftEye.radius = 50;
            _rightEye.radius = 50;
            break;
        default:
            _leftEye.squint = false;
            _leftEye.radius = 55;
            _rightEye.radius = 55;
            break;
    }

    // Mark dirty if something changed
    if (_leftEye.pupilX != _leftEye.lastPupilX || _leftEye.pupilY != _leftEye.lastPupilY ||
        _leftEye.radius != _leftEye.lastRadius || _leftEye.squint != _leftEye.lastSquint) {
        markDirty(_leftEye.x - _leftEye.radius - 15, _leftEye.y - _leftEye.radius - 20,
                  (_leftEye.radius + 15) * 2, (_leftEye.radius + 20) * 2);
    }
    if (_rightEye.pupilX != _rightEye.lastPupilX || _rightEye.pupilY != _rightEye.lastPupilY ||
        _rightEye.radius != _rightEye.lastRadius || _rightEye.squint != _rightEye.lastSquint) {
        markDirty(_rightEye.x - _rightEye.radius - 15, _rightEye.y - _rightEye.radius - 20,
                  (_rightEye.radius + 15) * 2, (_rightEye.radius + 20) * 2);
    }
}

void RobotFace::updateMouth() {
    unsigned long now = millis();

    _mouth.lastHeight = _mouth.currentHeight;
    _mouth.lastWidth = _mouth.width;

    if (_expression == FACE_SPEAKING || _speaking) {
        if (now - _lastMouthUpdate > 75) {
            float wave = sin(now * 0.025) * 0.5 + 0.5;
            int16_t targetHeight = 8 + (int16_t)(wave * 45) + random(-8, 8);
            _mouth.currentHeight = constrain(targetHeight, 4, 55);
            _mouth.width = 140 + (int16_t)(sin(now * 0.01) * 20);
            _lastMouthUpdate = now;
        }
        _mouth.color = TFT_GREEN;
    } else if (_expression == FACE_HAPPY) {
        _mouth.currentHeight = 20;
        _mouth.width = 160;
        _mouth.color = TFT_YELLOW;
    } else if (_expression == FACE_SAD) {
        _mouth.currentHeight = 6;
        _mouth.width = 120;
        _mouth.color = TFT_BLUE;
    } else {
        _mouth.currentHeight = 12;
        _mouth.width = 140;
        _mouth.color = TFT_GREEN;
    }

    // Mark dirty if mouth changed
    if (_mouth.currentHeight != _mouth.lastHeight || _mouth.width != _mouth.lastWidth) {
        markDirty(_mouth.x - 100, _mouth.y - 60, 200, 120);
    }
}

void RobotFace::updateBreathing() {
    unsigned long now = millis();

    if (now - _lastBreathe > 60) {
        float breathe = sin(now * 0.002) * 0.08 + 1.0;

        if (_expression == FACE_IDLE) {
            _leftEye.radius = (int16_t)(55 * breathe);
            _rightEye.radius = (int16_t)(55 * breathe);
        }
        _lastBreathe = now;
    }
}

// ============================================================================
// MAIN DRAW
// ============================================================================

void RobotFace::draw() {
    if (_displayMode != MODE_ROBOT) return;

    uint16_t bg = getBackgroundColor();

    if (_needsFullRedraw || _lastDrawnExpression != _expression) {
        // Full redraw on expression change
        _tft->fillScreen(bg);
        drawFaceFrame();
        drawDecorations();
        drawDetailedEye(_leftEye, true);
        drawDetailedEye(_rightEye, false);
        drawDetailedMouth();
        drawStatusText();
        _lastDrawnExpression = _expression;
        _needsFullRedraw = false;
        return;
    }

    // Fall back to direct if no PSRAM
    if (!_framebuffer) {
        _tft->startWrite();
        int er = _leftEye.radius + 10;
        _tft->fillCircle(_leftEye.x, _leftEye.y, er, bg);
        drawDetailedEye(_leftEye, true);
        er = _rightEye.radius + 10;
        _tft->fillCircle(_rightEye.x, _rightEye.y, er, bg);
        drawDetailedEye(_rightEye, false);
        _tft->fillRect(_mouth.x - 80, _mouth.y - 50, 160, 100, bg);
        drawDetailedMouth();
        _tft->endWrite();
        return;
    }

    // PSRAM FRAMEBUFFER UPDATE
    // Simple approach: clear regions, draw, push

    bool savedDoubleBuffer = _useDoubleBuffer;
    _useDoubleBuffer = true;

    // Region sizes
    const int16_t EYE_SIZE = 130;
    const int16_t MOUTH_W = 150;
    const int16_t MOUTH_H = 100;

    // Left eye region
    int16_t lx = _leftEye.x - EYE_SIZE/2;
    int16_t ly = _leftEye.y - EYE_SIZE/2;

    // Right eye region
    int16_t rx = _rightEye.x - EYE_SIZE/2;
    int16_t ry = _rightEye.y - EYE_SIZE/2;

    // Mouth region
    int16_t mx = _mouth.x - MOUTH_W/2;
    int16_t my = _mouth.y - MOUTH_H/2;

    // Clear and draw left eye
    for (int dy = 0; dy < EYE_SIZE; dy++) {
        for (int dx = 0; dx < EYE_SIZE; dx++) {
            int16_t px = lx + dx;
            int16_t py = ly + dy;
            if (px >= 0 && px < SCREEN_WIDTH && py >= 0 && py < SCREEN_HEIGHT) {
                _framebuffer[py * SCREEN_WIDTH + px] = bg;
            }
        }
    }
    drawDetailedEye(_leftEye, true);

    // Clear and draw right eye
    for (int dy = 0; dy < EYE_SIZE; dy++) {
        for (int dx = 0; dx < EYE_SIZE; dx++) {
            int16_t px = rx + dx;
            int16_t py = ry + dy;
            if (px >= 0 && px < SCREEN_WIDTH && py >= 0 && py < SCREEN_HEIGHT) {
                _framebuffer[py * SCREEN_WIDTH + px] = bg;
            }
        }
    }
    drawDetailedEye(_rightEye, false);

    // Clear and draw mouth
    for (int dy = 0; dy < MOUTH_H; dy++) {
        for (int dx = 0; dx < MOUTH_W; dx++) {
            int16_t px = mx + dx;
            int16_t py = my + dy;
            if (px >= 0 && px < SCREEN_WIDTH && py >= 0 && py < SCREEN_HEIGHT) {
                _framebuffer[py * SCREEN_WIDTH + px] = bg;
            }
        }
    }
    drawDetailedMouth();

    _useDoubleBuffer = savedDoubleBuffer;

    // Push regions row-by-row (handles stride correctly)
    _tft->startWrite();

    // Left eye
    for (int i = 0; i < EYE_SIZE; i++) {
        _tft->pushImage(lx, ly + i, EYE_SIZE, 1,
                       _framebuffer + (ly + i) * SCREEN_WIDTH + lx);
    }

    // Right eye
    for (int i = 0; i < EYE_SIZE; i++) {
        _tft->pushImage(rx, ry + i, EYE_SIZE, 1,
                       _framebuffer + (ry + i) * SCREEN_WIDTH + rx);
    }

    // Mouth
    for (int i = 0; i < MOUTH_H; i++) {
        _tft->pushImage(mx, my + i, MOUTH_W, 1,
                       _framebuffer + (my + i) * SCREEN_WIDTH + mx);
    }

    _tft->endWrite();
}

// ============================================================================
// FRAMEBUFFER PUSH
// ============================================================================

void RobotFace::pushFramebuffer() {
    if (!_framebuffer) return;

    // Use TFT_eSPI's pushImage for fast DMA transfer
    _tft->pushImage(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, _framebuffer);
}

// ============================================================================
// FRAMEBUFFER DRAWING PRIMITIVES
// ============================================================================

void RobotFace::fb_setPixel(int16_t x, int16_t y, uint16_t color) {
    if (x >= 0 && x < SCREEN_WIDTH && y >= 0 && y < SCREEN_HEIGHT) {
        _framebuffer[y * SCREEN_WIDTH + x] = color;
    }
}

void RobotFace::fb_drawFastHLine(int16_t x, int16_t y, int16_t w, uint16_t color) {
    if (y < 0 || y >= SCREEN_HEIGHT) return;
    if (x < 0) { w += x; x = 0; }
    if (x + w > SCREEN_WIDTH) w = SCREEN_WIDTH - x;
    if (w <= 0) return;

    uint16_t* p = _framebuffer + y * SCREEN_WIDTH + x;
    for (int i = 0; i < w; i++) {
        *p++ = color;
    }
}

void RobotFace::fb_fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
    for (int j = 0; j < h; j++) {
        fb_drawFastHLine(x, y + j, w, color);
    }
}

void RobotFace::fb_fillCircle(int16_t x, int16_t y, int16_t r, uint16_t color) {
    int16_t r2 = r * r;
    for (int dy = -r; dy <= r; dy++) {
        for (int dx = -r; dx <= r; dx++) {
            if (dx * dx + dy * dy <= r2) {
                fb_setPixel(x + dx, y + dy, color);
            }
        }
    }
}

void RobotFace::fb_drawCircle(int16_t x, int16_t y, int16_t r, uint16_t color) {
    int16_t r2 = r * r;
    int16_t r_inner = (r - 1) * (r - 1);
    for (int dy = -r; dy <= r; dy++) {
        for (int dx = -r; dx <= r; dx++) {
            int d2 = dx * dx + dy * dy;
            if (d2 <= r2 && d2 > r_inner) {
                fb_setPixel(x + dx, y + dy, color);
            }
        }
    }
}

// ============================================================================
// DRAWING (works with both TFT and framebuffer)
// ============================================================================

void RobotFace::drawFaceFrame() {
    uint16_t frameColor = getFrameColor();
    uint16_t bg = getBackgroundColor();

    if (_useDoubleBuffer) {
        // Draw to framebuffer
        fb_drawCircle(240, 260, 200, frameColor);
        fb_drawCircle(240, 260, 195, dimColor(frameColor, 70));
        fb_fillRect(10, 100, 8, 120, frameColor);
        fb_fillRect(462, 100, 8, 120, frameColor);
        fb_drawFastHLine(180, 310, 120, frameColor);
        fb_drawFastHLine(175, 312, 130, frameColor);
    } else {
        _tft->drawCircle(240, 260, 200, frameColor);
        _tft->drawCircle(240, 260, 195, dimColor(frameColor, 70));
        _tft->fillRect(10, 100, 8, 120, frameColor);
        _tft->fillRect(462, 100, 8, 120, frameColor);
        _tft->drawFastHLine(180, 310, 120, frameColor);
        _tft->drawFastHLine(175, 312, 130, frameColor);
    }
}

void RobotFace::drawDetailedEye(const Eye& eye, bool isLeft) {
    int16_t x = eye.x;
    int16_t y = eye.y;
    int16_t r = eye.radius;

    if (_isBlinking) {
        if (_useDoubleBuffer) {
            fb_fillRect(x - r, y - 8, r * 2, 16, TFT_WHITE);
        } else {
            _tft->fillRect(x - r, y - 8, r * 2, 16, TFT_WHITE);
        }
        return;
    }

    if (eye.squint) {
        int16_t squintAmount = r * 0.4;
        if (_useDoubleBuffer) {
            fb_fillCircle(x, y, r, TFT_WHITE);
            // Clear top and bottom for squint
            for (int dy = -r; dy < -r + squintAmount; dy++) {
                for (int dx = -r; dx <= r; dx++) {
                    if (dx*dx + dy*dy <= r*r) fb_setPixel(x + dx, y + dy, getBackgroundColor());
                }
            }
            for (int dy = r - squintAmount; dy <= r; dy++) {
                for (int dx = -r; dx <= r; dx++) {
                    if (dx*dx + dy*dy <= r*r) fb_setPixel(x + dx, y + dy, getBackgroundColor());
                }
            }
        } else {
            uint16_t bg = getBackgroundColor();
            _tft->fillCircle(x, y, r, TFT_WHITE);
            _tft->fillRect(x - r, y - r, r * 2, squintAmount, bg);
            _tft->fillRect(x - r, y + r - squintAmount, r * 2, squintAmount, bg);
        }
        return;
    }

    // Outer ring
    uint16_t ringColor = eye.color;
    if (_useDoubleBuffer) {
        fb_drawCircle(x, y, r + 8, ringColor);
        fb_drawCircle(x, y, r + 5, dimColor(ringColor, 50));
        fb_fillCircle(x, y, r, TFT_WHITE);

        // Iris
        int16_t irisR = r * 0.65;
        fb_fillCircle(x + eye.pupilX, y + eye.pupilY, irisR + 3, dimColor(eye.color, 30));
        fb_fillCircle(x + eye.pupilX, y + eye.pupilY, irisR, eye.color);

        // Pupil
        int16_t pupilR = r * 0.28;
        if (_expression == FACE_THINKING || _expression == FACE_LISTENING) {
            fb_fillCircle(x + eye.pupilX, y + eye.pupilY, pupilR + 4, 0x4100);
        }
        fb_fillCircle(x + eye.pupilX, y + eye.pupilY, pupilR, TFT_BLACK);

        // Highlights
        fb_fillCircle(x + eye.pupilX - pupilR * 0.4, y + eye.pupilY - pupilR * 0.4, pupilR * 0.4, TFT_WHITE);
        fb_fillCircle(x + eye.pupilX + pupilR * 0.2, y + eye.pupilY + pupilR * 0.2, pupilR * 0.15, 0x8410);
    } else {
        _tft->drawCircle(x, y, r + 8, ringColor);
        _tft->drawCircle(x, y, r + 5, dimColor(ringColor, 50));
        _tft->fillCircle(x, y, r, TFT_WHITE);

        int16_t irisR = r * 0.65;
        _tft->fillCircle(x + eye.pupilX, y + eye.pupilY, irisR + 3, dimColor(eye.color, 30));
        _tft->fillCircle(x + eye.pupilX, y + eye.pupilY, irisR, eye.color);

        int16_t pupilR = r * 0.28;
        if (_expression == FACE_THINKING || _expression == FACE_LISTENING) {
            _tft->fillCircle(x + eye.pupilX, y + eye.pupilY, pupilR + 4, 0x4100);
        }
        _tft->fillCircle(x + eye.pupilX, y + eye.pupilY, pupilR, TFT_BLACK);

        _tft->fillCircle(x + eye.pupilX - pupilR * 0.4, y + eye.pupilY - pupilR * 0.4, pupilR * 0.4, TFT_WHITE);
        _tft->fillCircle(x + eye.pupilX + pupilR * 0.2, y + eye.pupilY + pupilR * 0.2, pupilR * 0.15, 0x8410);
    }

    drawEyebrow(x, y - r - 15, isLeft);
}

void RobotFace::drawEyebrow(int16_t x, int16_t y, bool isLeft) {
    uint16_t browColor = TFT_WHITE;
    int16_t browWidth = 60;

    if (_useDoubleBuffer) {
        switch (_expression) {
            case FACE_HAPPY:
                for (int i = -browWidth/2; i < browWidth/2; i += 3) {
                    int yOffset = (i < 0) ? -10 + abs(i)/3 : 0;
                    fb_setPixel(x + i, y + yOffset, browColor);
                }
                break;
            case FACE_SAD:
                for (int i = -browWidth/2; i < browWidth/2; i += 3) {
                    int yOffset = (i < 0) ? 0 : 10 - abs(i)/3;
                    fb_setPixel(x + i, y + yOffset, browColor);
                }
                break;
            case FACE_THINKING:
                if (isLeft) {
                    fb_drawFastHLine(x - browWidth/2, y - 5, browWidth, browColor);
                } else {
                    fb_drawFastHLine(x - browWidth/2, y, browWidth, browColor);
                }
                break;
            default:
                fb_drawFastHLine(x - browWidth/2, y, browWidth, browColor);
                break;
        }
    } else {
        switch (_expression) {
            case FACE_HAPPY:
                for (int i = -browWidth/2; i < browWidth/2; i += 3) {
                    int yOffset = (i < 0) ? -10 + abs(i)/3 : 0;
                    _tft->drawPixel(x + i, y + yOffset, browColor);
                }
                break;
            case FACE_SAD:
                for (int i = -browWidth/2; i < browWidth/2; i += 3) {
                    int yOffset = (i < 0) ? 0 : 10 - abs(i)/3;
                    _tft->drawPixel(x + i, y + yOffset, browColor);
                }
                break;
            case FACE_THINKING:
                if (isLeft) {
                    _tft->drawFastHLine(x - browWidth/2, y - 5, browWidth, browColor);
                } else {
                    _tft->drawFastHLine(x - browWidth/2, y, browWidth, browColor);
                }
                break;
            default:
                _tft->drawFastHLine(x - browWidth/2, y, browWidth, browColor);
                break;
        }
    }
}

void RobotFace::drawDetailedMouth() {
    int16_t x = _mouth.x;
    int16_t y = _mouth.y;
    int16_t h = _mouth.currentHeight;

    if (_useDoubleBuffer) {
        switch (_expression) {
            case FACE_HAPPY:
                fb_fillCircle(x, y + 30, 90, TFT_YELLOW);
                fb_fillCircle(x, y + 30, 85, getBackgroundColor());
                break;
            case FACE_SAD:
                fb_fillCircle(x, y - 10, 70, TFT_BLUE);
                fb_fillCircle(x, y - 10, 65, getBackgroundColor());
                break;
            case FACE_SPEAKING: {
                int16_t segments = 20;
                int16_t segWidth = 8;
                for (int16_t i = 0; i < segments; i++) {
                    float phase = (millis() * 0.015) + (i * 0.4);
                    float wave = sin(phase) * 0.5 + 0.5;
                    int16_t barH = constrain((int16_t)(wave * 60), 3, 60);
                    int16_t barX = x - 90 + i * segWidth;
                    uint16_t barColor = _tft->color565((int16_t)(wave * 100), 200, 50);
                    fb_fillRect(barX, y - barH/2, segWidth - 1, barH, barColor);
                }
                break;
            }
            case FACE_LISTENING:
                fillEllipse(x, y, 25, 18, TFT_GREEN);
                break;
            case FACE_THINKING:
                fb_fillRect(x - 20, y - 3, 40, 6, TFT_YELLOW);
                for (int i = 0; i < 3; i++) {
                    fb_fillCircle(x - 12 + i * 12, y + 20, 4, TFT_YELLOW);
                }
                break;
            default:
                fb_fillRect(x - 70, y - h/2, 140, h, TFT_GREEN);
                break;
        }
    } else {
        switch (_expression) {
            case FACE_HAPPY:
                _tft->fillCircle(x, y + 30, 90, TFT_YELLOW);
                _tft->fillCircle(x, y + 30, 85, getBackgroundColor());
                break;
            case FACE_SAD:
                _tft->fillCircle(x, y - 10, 70, TFT_BLUE);
                _tft->fillCircle(x, y - 10, 65, getBackgroundColor());
                break;
            case FACE_SPEAKING: {
                int16_t segments = 20;
                int16_t segWidth = 8;
                for (int16_t i = 0; i < segments; i++) {
                    float phase = (millis() * 0.015) + (i * 0.4);
                    float wave = sin(phase) * 0.5 + 0.5;
                    int16_t barH = constrain((int16_t)(wave * 60), 3, 60);
                    int16_t barX = x - 90 + i * segWidth;
                    uint16_t barColor = _tft->color565((int16_t)(wave * 100), 200, 50);
                    _tft->fillRect(barX, y - barH/2, segWidth - 1, barH, barColor);
                }
                break;
            }
            case FACE_LISTENING:
                fillEllipse(x, y, 25, 18, TFT_GREEN);
                break;
            case FACE_THINKING:
                _tft->fillRect(x - 20, y - 3, 40, 6, TFT_YELLOW);
                for (int i = 0; i < 3; i++) {
                    _tft->fillCircle(x - 12 + i * 12, y + 20, 4, TFT_YELLOW);
                }
                break;
            default:
                _tft->fillRect(x - 70, y - h/2, 140, h, TFT_GREEN);
                break;
        }
    }
}

void RobotFace::drawDecorations() {
    unsigned long now = millis();
    uint16_t decoColor = getExpressionColor();
    uint16_t bg = getBackgroundColor();

    if (_useDoubleBuffer) {
        // Left side dots
        for (int i = 0; i < 3; i++) {
            int16_t yPos = 80 + i * 40;
            int16_t size = 5 + (int16_t)(sin(now * 0.005 + i) * 3);
            fb_fillCircle(30, yPos, size, decoColor);
            fb_fillCircle(30, yPos, size * 0.5, bg);
        }
        // Right side dots
        for (int i = 0; i < 3; i++) {
            int16_t yPos = 80 + i * 40;
            int16_t size = 5 + (int16_t)(sin(now * 0.005 + i + 1.5) * 3);
            fb_fillCircle(450, yPos, size, decoColor);
            fb_fillCircle(450, yPos, size * 0.5, bg);
        }
    } else {
        for (int i = 0; i < 3; i++) {
            int16_t yPos = 80 + i * 40;
            int16_t size = 5 + (int16_t)(sin(now * 0.005 + i) * 3);
            _tft->fillCircle(30, yPos, size, decoColor);
            _tft->fillCircle(30, yPos, size * 0.5, bg);
        }
        for (int i = 0; i < 3; i++) {
            int16_t yPos = 80 + i * 40;
            int16_t size = 5 + (int16_t)(sin(now * 0.005 + i + 1.5) * 3);
            _tft->fillCircle(450, yPos, size, decoColor);
            _tft->fillCircle(450, yPos, size * 0.5, bg);
        }
    }
}

void RobotFace::drawStatusText() {
    const char* statusText = getStatusText();

    if (_useDoubleBuffer) {
        // Clear status area at top
        uint16_t bg = getBackgroundColor();
        for (int y = 0; y < 40; y++) {
            for (int x = 0; x < 480; x++) {
                _framebuffer[y * SCREEN_WIDTH + x] = bg;
            }
        }
        // Use TFT for text (simpler than implementing font rendering to framebuffer)
        _tft->setTextColor(TFT_WHITE, bg);
        _tft->setTextSize(2);
        _tft->setTextDatum(MC_DATUM);
        _tft->drawString(statusText, 240, 20);
    } else {
        _tft->setTextColor(TFT_WHITE, getBackgroundColor());
        _tft->setTextSize(2);
        _tft->setTextDatum(MC_DATUM);
        _tft->drawString(statusText, 240, 20);
    }
}

// ============================================================================
// SHAPE HELPERS
// ============================================================================

void RobotFace::drawEllipse(int16_t x, int16_t y, int16_t rx, int16_t ry, uint16_t color) {
    if (_useDoubleBuffer) {
        int32_t rx2 = rx * rx;
        int32_t ry2 = ry * ry;
        for (int16_t iy = -ry; iy <= ry; iy++) {
            int32_t dx = sqrt(rx2 * (ry2 - iy * iy) / ry2);
            fb_setPixel(x + dx, y + iy, color);
            fb_setPixel(x - dx, y + iy, color);
        }
    } else {
        int32_t rx2 = rx * rx;
        int32_t ry2 = ry * ry;
        for (int16_t iy = -ry; iy <= ry; iy++) {
            int32_t dx = sqrt(rx2 * (ry2 - iy * iy) / ry2);
            _tft->drawPixel(x + dx, y + iy, color);
            _tft->drawPixel(x - dx, y + iy, color);
        }
    }
}

void RobotFace::fillEllipse(int16_t x, int16_t y, int16_t rx, int16_t ry, uint16_t color) {
    if (_useDoubleBuffer) {
        for (int16_t iy = -ry; iy <= ry; iy++) {
            int32_t dx = (int32_t)rx * sqrt(1 - (float)(iy * iy) / (ry * ry));
            fb_drawFastHLine(x - dx, y + iy, dx * 2, color);
        }
    } else {
        for (int16_t iy = -ry; iy <= ry; iy++) {
            int32_t dx = (int32_t)rx * sqrt(1 - (float)(iy * iy) / (ry * ry));
            _tft->drawFastHLine(x - dx, y + iy, dx * 2, color);
        }
    }
}

void RobotFace::drawArc(int16_t cx, int16_t cy, int16_t r, int16_t startAngle,
                         int16_t endAngle, int16_t thickness, uint16_t color) {
    // Use TFT_eSPI's built-in drawArc
    _tft->drawArc(cx, cy, r, r - thickness, startAngle, endAngle, color, getBackgroundColor());
}

// ============================================================================
// DIRTY RECTANGLE TRACKING
// ============================================================================

void RobotFace::markDirty(int16_t x, int16_t y, int16_t w, int16_t h) {
    // Expand existing dirty rectangles or mark new ones
    // Simplified: just mark that something changed
    _framebufferDirty = true;
}

void RobotFace::clearDirtyRect(const DirtyRect& rect, uint16_t bgColor) {
    if (_useDoubleBuffer) {
        fb_fillRect(rect.x, rect.y, rect.w, rect.h, bgColor);
    } else {
        _tft->fillRect(rect.x, rect.y, rect.w, rect.h, bgColor);
    }
}

void RobotFace::pushDirtyRects() {
    // Would push only dirty regions - not fully implemented
    // Currently pushFramebuffer handles full frame
}

// ============================================================================
// COLOR HELPERS
// ============================================================================

uint16_t RobotFace::getBackgroundColor() {
    // Always black background - blends with partial updates
    return TFT_BLACK;
}

uint16_t RobotFace::getFrameColor() {
    return dimColor(getExpressionColor(), 40);
}

uint16_t RobotFace::getExpressionColor() {
    switch (_expression) {
        case FACE_IDLE:      return TFT_CYAN;
        case FACE_LISTENING: return TFT_GREEN;
        case FACE_THINKING:  return TFT_YELLOW;
        case FACE_SPEAKING:  return 0xFD20;
        case FACE_HAPPY:     return TFT_YELLOW;
        case FACE_SAD:       return 0x07FF;
        case FACE_ERROR:     return TFT_RED;
        case FACE_CONFUSED:  return TFT_PURPLE;
        default:             return TFT_CYAN;
    }
}

const char* RobotFace::getStatusText() {
    if (_statusOverride.length() > 0) return _statusOverride.c_str();
    switch (_expression) {
        case FACE_IDLE:      return "How can I help?";
        case FACE_LISTENING: return "Listening...";
        case FACE_THINKING:  return "Thinking...";
        case FACE_SPEAKING:  return "Speaking...";
        case FACE_HAPPY:     return "Great!";
        case FACE_SAD:       return "Sorry...";
        case FACE_ERROR:     return "Something went wrong";
        case FACE_CONFUSED:  return "Hmm...";
        default:             return "AI Assistant";
    }
}

uint16_t RobotFace::dimColor(uint16_t color, uint8_t percent) {
    uint8_t r = ((color >> 11) & 0x1F) * percent / 100;
    uint8_t g = ((color >> 5) & 0x3F) * percent / 100;
    uint8_t b = (color & 0x1F) * percent / 100;
    return (r << 11) | (g << 5) | b;
}

uint16_t RobotFace::brightenColor(uint16_t color, uint8_t percent) {
    uint8_t r = ((color >> 11) & 0x1F);
    uint8_t g = ((color >> 5) & 0x3F);
    uint8_t b = color & 0x1F;

    r = constrain(r + r * percent / 100, 0, 31);
    g = constrain(g + g * percent / 100, 0, 63);
    b = constrain(b + b * percent / 100, 0, 31);

    return (r << 11) | (g << 5) | b;
}

int16_t RobotFace::lerp(int16_t a, int16_t b, float t) {
    return a + (int16_t)((b - a) * t);
}
