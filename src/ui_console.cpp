#include "ui_console.h"
#include "config.h"
#include <TFT_eSPI.h>

// Font 1 (GLCD) at size 2 = 12 px wide x 16 px tall — a clean fixed-width
// terminal look on the ILI9488.
static const int16_t CH_W    = 12;    // font 1 @ size 2 = 12 px/char
static const int16_t LINE_H  = 22;
static const int16_t X_LABEL = 16;
static const int16_t X_TAG   = 330;   // where "[ .. ]" starts (tag is 6 chars = 72 px)
static const int16_t X_DETAIL= 408;   // detail printed here at size 1 (small)
static const int16_t SCR_W   = 480;   // ILI9488 landscape width

BootConsole::BootConsole(Display& display)
    : _display(display), _tft(display.getTft()), _y(64), _tagY(64) {}

void BootConsole::begin(const char* title) {
    _tft->fillScreen(UI_BG);

    _tft->setTextFont(1);
    _tft->setTextDatum(TL_DATUM);

    _tft->setTextSize(2);
    _tft->setTextColor(EYE_BLUE, UI_BG);
    _tft->drawString(title, X_LABEL, 12);

    _tft->setTextSize(1);
    _tft->setTextColor(UI_DIM, UI_BG);
    _tft->drawString("ESP32-S3  .  system boot", X_LABEL, 36);

    _tft->drawFastHLine(X_LABEL, 52, SCR_W - 2 * X_LABEL, RGB565(18, 40, 50));
    _y = 64;
    _tagY = 64;

    Serial.printf("\n===== %s =====\n", title);
}

void BootConsole::step(const char* label) {
    _tagY = _y;
    _tft->setTextFont(1);
    _tft->setTextSize(2);
    _tft->setTextDatum(TL_DATUM);

    _tft->setTextColor(UI_TXT, UI_BG);
    _tft->drawString(label, X_LABEL, _y);

    // dotted leader from end of label to the tag column
    int16_t lx = X_LABEL + (int16_t)strlen(label) * CH_W + CH_W;
    _tft->setTextColor(RGB565(32, 52, 61), UI_BG);
    String dots;
    for (int16_t x = lx; x < X_TAG - CH_W; x += CH_W) dots += '.';
    _tft->drawString(dots, lx, _y);

    // pending tag
    _tft->setTextColor(UI_PEND, UI_BG);
    _tft->drawString("[ .. ]", X_TAG, _y);

    _y += LINE_H;
    Serial.printf("%-24s ... ", label);
}

void BootConsole::tag(const char* label, uint16_t color, const char* detail) {
    // overwrite the pending tag + detail area on the remembered line
    _tft->fillRect(X_TAG - 2, _tagY, SCR_W - (X_TAG - 2), 18, UI_BG);
    _tft->setTextFont(1);
    _tft->setTextSize(2);
    _tft->setTextDatum(TL_DATUM);
    _tft->setTextColor(color, UI_BG);
    _tft->drawString(label, X_TAG, _tagY);
    if (detail && *detail) {
        _tft->setTextSize(1);   // small so IPs / sizes fit to the screen edge
        _tft->setTextColor(UI_DIM, UI_BG);
        _tft->drawString(detail, X_DETAIL, _tagY + 4);
    }
    Serial.printf("%s %s\n", label, (detail && *detail) ? detail : "");
}

void BootConsole::ok(const char* detail)   { tag("[ OK ]", UI_OK,   detail); }
void BootConsole::warn(const char* detail) { tag("[WARN]", UI_WARN, detail); }
void BootConsole::fail(const char* detail) { tag("[FAIL]", UI_FAIL, detail); }

void BootConsole::done(const char* msg) {
    _tft->setTextFont(1);
    _tft->setTextSize(2);
    _tft->setTextDatum(TL_DATUM);
    _tft->setTextColor(UI_OK, UI_BG);
    _tft->drawString(msg, X_LABEL, _y + 6);
    Serial.printf(">>> %s\n", msg);
}
