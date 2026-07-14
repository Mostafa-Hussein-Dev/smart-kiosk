#include "robot_face.h"
#include "config.h"
#include <math.h>
#include <TFT_eSPI.h>

// Static members
DisplayMode RobotFace::_displayMode = MODE_NORMAL;
bool RobotFace::_needsFullRedraw = true;

RobotFace::RobotFace(Display& display)
    : _display(display)
    , _tft(display.getTft())
    , _framebuffer(nullptr)
    , _expression(FACE_IDLE)
    , _lastDrawnExpression(FACE_IDLE)
    , _lookX(0), _lookY(0), _tgtLookX(0), _tgtLookY(0)
    , _nextLook(0)
    , _blink(1.0f), _blinkT(-1), _nextBlink(2000)
    , _wifi(false)
    , _bandDirty(true)
    , _lastDotCount(-1)
    , _t0(0)
{
    _cur = { (float)EYE_W, (float)EYE_H, (float)EYE_R, 0.0f, EYE_BLUE, 0 };
}

RobotFace::~RobotFace() {
    if (_framebuffer) free(_framebuffer);
}

void RobotFace::init() {
    _t0 = millis();
    if (psramFound()) {
        size_t fbSize = (size_t)SCREEN_WIDTH * SCREEN_HEIGHT * sizeof(uint16_t);
        _framebuffer = (uint16_t*)ps_malloc(fbSize);
        Serial.printf("[RobotFace] framebuffer %s (%u KB)\n",
                      _framebuffer ? "in PSRAM" : "ALLOC FAILED -> direct draw",
                      (unsigned)(fbSize / 1024));
    } else {
        Serial.println("[RobotFace] no PSRAM -> direct draw");
    }
}

// ── state control ──────────────────────────────────────────
void RobotFace::setExpression(FaceExpression expr) {
    if (_expression != expr) {
        _expression = expr;
        _needsFullRedraw = true;   // repaint clears old waveform / dots
    }
}

void RobotFace::setStatusText(const String& text) {
    if (_statusOverride != text) {
        _statusOverride = text;
        _needsFullRedraw = true;
    }
}

void RobotFace::setUser(const String& name) {
    if (_user != name) { _user = name; _bandDirty = true; }
}

void RobotFace::setWifiConnected(bool connected) {
    if (_wifi != connected) { _wifi = connected; _bandDirty = true; }
}

void RobotFace::setDisplayMode(DisplayMode m) {
    if (_displayMode != m) {
        _displayMode = m;
        if (m == MODE_ROBOT) _needsFullRedraw = true;
    }
}

// ── expression -> target eye shape ─────────────────────────
void RobotFace::targetFor(FaceExpression e, EyeShape& o) {
    switch (e) {
        case FACE_LISTENING: o = { 122, 146, 58, -2,  EYE_BLUE, 0 }; break;
        case FACE_THINKING:
        case FACE_CONFUSED:  o = { 82,  150, 40, -16, EYE_BLUE, 0 }; break;
        case FACE_SPEAKING:  o = { 112, 128, 54, 0,   EYE_BLUE, 0 }; break;
        case FACE_HAPPY:     o = { 120, 70,  34, -6,  EYE_BLUE, 1 }; break;
        case FACE_ERROR:     o = { 112, 34,  16, 0,   EYE_RED,  0 }; break;
        case FACE_SAD:       o = { 100, 96,  46, 10,  EYE_BLUE, 0 }; break;
        default:             o = { (float)EYE_W, (float)EYE_H, (float)EYE_R, 0, EYE_BLUE, 0 }; break;
    }
}

// ── per-frame animation ────────────────────────────────────
void RobotFace::update() {
    if (_displayMode != MODE_ROBOT) return;
    unsigned long now = millis();

    EyeShape T; targetFor(_expression, T);
    const float k = 0.16f;
    _cur.w  = lerp(_cur.w,  T.w,  k);
    _cur.h  = lerp(_cur.h,  T.h,  k);
    _cur.r  = lerp(_cur.r,  T.r,  k);
    _cur.dy = lerp(_cur.dy, T.dy, k);
    _cur.color = T.color;
    _cur.kind  = T.kind;

    // look-around drift
    if (_expression == FACE_IDLE || _expression == FACE_SPEAKING) {
        if (now > _nextLook) {
            _tgtLookX = (random(0, 2000) / 1000.0f - 1.0f) * 16.0f;
            _tgtLookY = (random(0, 2000) / 1000.0f - 1.0f) * 8.0f;
            _nextLook = now + 1400 + random(0, 2600);
        }
    } else if (_expression == FACE_THINKING || _expression == FACE_CONFUSED) {
        _tgtLookX = 14; _tgtLookY = -6;
    } else {
        _tgtLookX = 0; _tgtLookY = 0;
    }
    _lookX = lerp(_lookX, _tgtLookX, 0.06f);
    _lookY = lerp(_lookY, _tgtLookY, 0.06f);

    // blink (only for the "open-eye" states)
    bool canBlink = (_expression == FACE_IDLE || _expression == FACE_LISTENING ||
                     _expression == FACE_SPEAKING);
    if (canBlink) {
        if (_blinkT < 0 && now >= _nextBlink) _blinkT = (long)now;
        if (_blinkT >= 0) {
            float p = (now - (unsigned long)_blinkT) / 150.0f;
            if (p >= 1.0f) {
                _blink = 1.0f; _blinkT = -1;
                _nextBlink = now + 2200 + random(0, 3200);
            } else {
                _blink = p < 0.5f ? 1.0f - p * 1.76f : 0.12f + (p - 0.5f) * 1.76f;
            }
        }
    } else {
        _blink = 1.0f;
    }
}

// ── main draw ──────────────────────────────────────────────
void RobotFace::draw() {
    if (_displayMode != MODE_ROBOT) return;
    unsigned long now = millis();
    float t = (float)(now - _t0);

    if (_needsFullRedraw) {
        if (_framebuffer) {
            for (int i = 0; i < SCREEN_WIDTH * SCREEN_HEIGHT; i++) _framebuffer[i] = UI_BG;
        }
        _tft->fillScreen(UI_BG);
        _bandDirty = true;
        _lastBandActivity = "";
        _lastDotCount = -1;
        drawBand();
        const char* bt = _statusOverride.length() ? _statusOverride.c_str()
                       : (_expression == FACE_IDLE ? "How can I help?" : "");
        drawBottomText(bt);
        _lastDrawnExpression = _expression;
        _needsFullRedraw = false;
    }

    if (_bandDirty || _lastBandActivity != activityLabel()) drawBand();

    // breathing on idle
    float breathe = (_expression == FACE_IDLE) ? (1.0f + sinf(t * 0.0021f) * 0.05f) : 1.0f;
    EyeShape ds = _cur; ds.h *= breathe;

    drawEyeRegion(EYE_LX, ds);
    drawEyeRegion(EYE_RX, ds);

    if (_expression == FACE_SPEAKING)       drawWave(1.0f);
    else if (_expression == FACE_LISTENING) drawWave(0.34f);
    else if (_expression == FACE_THINKING || _expression == FACE_CONFUSED) drawThinkingDots();
}

// ── one eye's fixed region: clear -> shape -> push ─────────
void RobotFace::drawEyeRegion(int16_t cx, const EyeShape& ds) {
    int16_t rx = cx - 82, ry = EYE_CY - 104, rw = 164, rh = 208;
    if (rx < 0) { rw += rx; rx = 0; }
    if (rx + rw > SCREEN_WIDTH) rw = SCREEN_WIDTH - rx;

    fb_fillRect(rx, ry, rw, rh, UI_BG);
    if (ds.kind == 1) drawHappyInto(cx, ds, _blink);
    else              drawBarInto(cx, ds, _blink);
    pushRegion(rx, ry, rw, rh);
}

void RobotFace::drawBarInto(int16_t cx, const EyeShape& s, float blink) {
    float w = s.w;
    float h = s.h * blink;
    int16_t x = (int16_t)(cx - w / 2 + _lookX);
    int16_t y = (int16_t)(EYE_CY - h / 2 + s.dy + _lookY);
    if (h < 6) h = 6;
    fb_fillRoundRect(x, y, (int16_t)w, (int16_t)h, (int16_t)s.r, s.color);
}

// upward crescent "^ ^" — thick round curve, stamped from circles along an arc
void RobotFace::drawHappyInto(int16_t cx, const EyeShape& s, float blink) {
    float rad = s.w * 0.62f;
    int16_t ccx = (int16_t)(cx + _lookX);
    int16_t ccy = (int16_t)(EYE_CY + s.dy + 18 + _lookY);
    int16_t thickR = (int16_t)(13 * (blink < 0.5f ? 0.5f : blink));
    const float a0 = (float)M_PI * 1.18f, a1 = (float)M_PI * 1.82f;
    for (int i = 0; i <= 22; i++) {
        float a = a0 + (a1 - a0) * (i / 22.0f);
        int16_t px = (int16_t)(ccx + cosf(a) * rad);
        int16_t py = (int16_t)(ccy + sinf(a) * rad);
        fb_fillCircle(px, py, thickR, s.color);
    }
}

// ── waveform strip ─────────────────────────────────────────
void RobotFace::drawWave(float amp) {
    float t = (float)(millis() - _t0);
    // Kept clear of the eyes above (bottoms ~y239) and the status line below.
    int16_t rx = WAVE_X0 - 4, ry = WAVE_Y - 24, rw = (WAVE_X1 - WAVE_X0) + 8, rh = 48;
    fb_fillRect(rx, ry, rw, rh, UI_BG);

    const int bars = WAVE_BARS;
    float gap = (float)(WAVE_X1 - WAVE_X0) / bars;
    for (int i = 0; i < bars; i++) {
        float d = fabsf(i - (bars - 1) / 2.0f) / ((bars - 1) / 2.0f);
        float env = 1.0f - d * 0.75f;
        float sv = sinf(t * 0.008f + i * 0.7f) * 0.5f + 0.5f;
        float n = amp > 0.5f ? (random(0, 250) / 1000.0f) : 0.0f;
        float h = amp * env * (5.0f + (sv + n) * 30.0f);   // max ~42 px -> fits the strip
        if (h < 3) h = 3;
        int16_t bx = (int16_t)(WAVE_X0 + i * gap);
        fb_fillRoundRect(bx, (int16_t)(WAVE_Y - h / 2), (int16_t)(gap * 0.6f), (int16_t)h, 3, EYE_BLUE);
    }
    pushRegion(rx, ry, rw, rh);
}

// ── status band (direct to TFT; changes rarely) ────────────
const char* RobotFace::activityLabel() {
    switch (_expression) {
        case FACE_LISTENING: return "LISTENING";
        case FACE_THINKING:
        case FACE_CONFUSED:  return "THINKING";
        case FACE_SPEAKING:  return "SPEAKING";
        case FACE_HAPPY:     return "WELCOME";
        case FACE_ERROR:     return "ERROR";
        default:             return "IDLE";
    }
}

void RobotFace::drawBand() {
    _tft->fillRect(0, 0, SCREEN_WIDTH, BAND_H, UI_BAND);
    _tft->drawFastHLine(0, BAND_H, SCREEN_WIDTH, RGB565(18, 40, 50));

    // Wi-Fi signal bars
    uint16_t wc = _wifi ? UI_OK : UI_DIM;
    for (int i = 0; i < 3; i++) {
        int bh = 4 + i * 4;
        _tft->fillRect(12 + i * 6, 18 - bh, 4, bh, wc);
    }

    _tft->setTextFont(1);
    _tft->setTextSize(2);
    _tft->setTextColor(UI_TXT, UI_BAND);
    _tft->setTextDatum(ML_DATUM);
    _tft->drawString(_user.length() ? _user.c_str() : "Guest", 36, BAND_H / 2);

    _tft->setTextColor(UI_ACCENT, UI_BAND);
    _tft->setTextDatum(MR_DATUM);
    _tft->drawString(activityLabel(), SCREEN_WIDTH - 10, BAND_H / 2);

    _tft->setTextDatum(TL_DATUM);
    _bandDirty = false;
    _lastBandActivity = activityLabel();
}

void RobotFace::drawBottomText(const char* str) {
    _tft->fillRect(0, 292, SCREEN_WIDTH, 26, UI_BG);
    if (!str || !*str) return;
    _tft->setTextFont(1);
    _tft->setTextSize(2);
    _tft->setTextColor(UI_DIM, UI_BG);
    _tft->setTextDatum(MC_DATUM);
    _tft->drawString(str, SCREEN_WIDTH / 2, 305);
    _tft->setTextDatum(TL_DATUM);
}

void RobotFace::drawThinkingDots() {
    // Drawn every frame AFTER the eyes, because the per-frame eye-region push
    // repaints (and would otherwise erase) this gap between the eyes.
    float t = (float)(millis() - _t0);
    int n = ((int)(t / 350)) % 4;
    _lastDotCount = n;
    _tft->fillRect(206, 236, 68, 26, UI_BG);
    char dots[4] = {0};
    for (int i = 0; i < n; i++) dots[i] = '.';
    _tft->setTextFont(1);
    _tft->setTextSize(3);
    _tft->setTextColor(EYE_BLUE, UI_BG);
    _tft->setTextDatum(MC_DATUM);
    _tft->drawString(dots, SCREEN_WIDTH / 2, 249);
    _tft->setTextDatum(TL_DATUM);
}

// ── framebuffer primitives (fall back to direct TFT if no PSRAM) ──
void RobotFace::fb_fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
    if (!_framebuffer) { _tft->fillRect(x, y, w, h, color); return; }
    if (w <= 0 || h <= 0) return;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > SCREEN_WIDTH)  w = SCREEN_WIDTH  - x;
    if (y + h > SCREEN_HEIGHT) h = SCREEN_HEIGHT - y;
    if (w <= 0 || h <= 0) return;
    for (int j = 0; j < h; j++) {
        uint16_t* p = _framebuffer + (size_t)(y + j) * SCREEN_WIDTH + x;
        for (int i = 0; i < w; i++) p[i] = color;
    }
}

void RobotFace::fb_fillCircle(int16_t x, int16_t y, int16_t r, uint16_t color) {
    if (!_framebuffer) { _tft->fillCircle(x, y, r, color); return; }
    for (int16_t dy = -r; dy <= r; dy++) {
        int16_t dx = (int16_t)(sqrtf((float)(r * r - dy * dy)) + 0.5f);
        fb_fillRect(x - dx, y + dy, 2 * dx + 1, 1, color);
    }
}

void RobotFace::fb_fillRoundRect(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, uint16_t color) {
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;
    if (r < 0) r = 0;
    if (!_framebuffer) { _tft->fillRoundRect(x, y, w, h, r, color); return; }
    fb_fillRect(x + r, y, w - 2 * r, h, color);
    fb_fillRect(x, y + r, r, h - 2 * r, color);
    fb_fillRect(x + w - r, y + r, r, h - 2 * r, color);
    fb_fillCircle(x + r, y + r, r, color);
    fb_fillCircle(x + w - r - 1, y + r, r, color);
    fb_fillCircle(x + r, y + h - r - 1, r, color);
    fb_fillCircle(x + w - r - 1, y + h - r - 1, r, color);
}

void RobotFace::pushRegion(int16_t x, int16_t y, int16_t w, int16_t h) {
    if (!_framebuffer) return;   // already drawn directly
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > SCREEN_WIDTH)  w = SCREEN_WIDTH  - x;
    if (y + h > SCREEN_HEIGHT) h = SCREEN_HEIGHT - y;
    if (w <= 0 || h <= 0) return;
    _tft->startWrite();
    for (int16_t i = 0; i < h; i++) {
        _tft->pushImage(x, y + i, w, 1, _framebuffer + (size_t)(y + i) * SCREEN_WIDTH + x);
    }
    _tft->endWrite();
}
