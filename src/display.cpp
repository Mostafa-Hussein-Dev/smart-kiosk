#include "display.h"
#include <stdarg.h>
#include <TFT_eSPI.h>
#include <SPI.h>

// TFT_eSPI instance - configured in User_Setup.h
TFT_eSPI tft = TFT_eSPI();

Display::Display()
    : _initialized(false)
    , _width(480)   // Landscape width (rotation 1 swaps 320x480 to 480x320)
    , _height(320)  // Landscape height
    , _rotation(TFT_ROTATION)
    , _animationStartTime(0)
    , _animationFrame(0)
{
}

Display::~Display() {
}

void Display::begin() {
    if (_initialized) {
        return;
    }

    Serial.println("[Display] ========== TFT INIT START ==========");

    // Simple initialization matching working Arduino IDE sketch
    Serial.println("[Display] Initializing TFT (tft.init)...");
    tft.init();

    Serial.println("[Display] Setting rotation to 1 (landscape)...");
    tft.setRotation(_rotation);

    // Turn on backlight
    Serial.printf("[Display] Turning on backlight (GPIO %d)...\n", TFT_LED_PIN);
    pinMode(TFT_LED_PIN, OUTPUT);
    digitalWrite(TFT_LED_PIN, TFT_BACKLIGHT_ON);

    // Start on a clean black panel (the old RGB color test was removed).
    tft.fillScreen(TFT_BLACK);

    Serial.println("[Display] ========== TFT INIT COMPLETE ==========");

    _initialized = true;
    Serial.println("[Display] Display ready!");
}

void Display::clear(uint16_t color) {
    if (!_initialized) return;
    tft.fillScreen(color);
}

void Display::drawPixel(int16_t x, int16_t y, uint16_t color) {
    if (!_initialized) return;
    tft.drawPixel(x, y, color);
}

void Display::drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color) {
    if (!_initialized) return;
    tft.drawLine(x0, y0, x1, y1, color);
}

void Display::drawRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
    if (!_initialized) return;
    tft.drawRect(x, y, w, h, color);
}

void Display::fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
    if (!_initialized) return;
    tft.fillRect(x, y, w, h, color);
}

void Display::fillRoundRect(int16_t x, int16_t y, int16_t w, int16_t h, int16_t radius, uint16_t color) {
    if (!_initialized) return;
    tft.fillRoundRect(x, y, w, h, radius, color);
}

void Display::drawCircle(int16_t x, int16_t y, int16_t r, uint16_t color) {
    if (!_initialized) return;
    tft.drawCircle(x, y, r, color);
}

void Display::fillCircle(int16_t x, int16_t y, int16_t r, uint16_t color) {
    if (!_initialized) return;
    tft.fillCircle(x, y, r, color);
}

void Display::setCursor(int16_t x, int16_t y) {
    if (!_initialized) return;
    tft.setCursor(x, y);
}

void Display::setTextSize(uint8_t size) {
    if (!_initialized) return;
    tft.setTextSize(size);
}

void Display::setTextColor(uint16_t color, uint16_t bgcolor) {
    if (!_initialized) return;
    if (bgcolor == 0xFFFF) {
        tft.setTextColor(color);
    } else {
        tft.setTextColor(color, bgcolor);
    }
}

void Display::print(const char* text) {
    if (!_initialized) return;
    tft.print(text);
}

void Display::println(const char* text) {
    if (!_initialized) return;
    tft.println(text);
}

void Display::printf(const char* format, ...) {
    if (!_initialized) return;
    char buffer[256];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    tft.print(buffer);
}

int16_t Display::getTextWidth(const char* text) {
    if (!_initialized) return 0;
    return tft.textWidth(text);
}

int16_t Display::getTextHeight() {
    if (!_initialized) return 0;
    return tft.fontHeight();
}

// ==================== UI Screens ====================

void Display::showSplashScreen() {
    if (!_initialized) return;

    Serial.println("[Display] welcome screen");

    tft.fillScreen(UI_BG);

    // Two bright-blue rounded "eyes" peeking at the top — same identity as the
    // idle robot face.
    tft.fillRoundRect(214, 72, 44, 66, 22, EYE_BLUE);
    tft.fillRoundRect(266, 72, 44, 66, 22, EYE_BLUE);

    tft.setTextFont(1);
    tft.setTextDatum(MC_DATUM);

    tft.setTextSize(3);
    tft.setTextColor(UI_TXT, UI_BG);
    tft.drawString("Hello", _width / 2, 184);

    tft.setTextSize(2);
    tft.setTextColor(UI_ACCENT, UI_BG);
    tft.drawString("Smart University Assistant", _width / 2, 222);

    tft.setTextColor(UI_DIM, UI_BG);
    tft.drawString("Tap your card to sign in", _width / 2, 258);
    tft.setTextSize(1);
    tft.drawString("or hold the button to talk", _width / 2, 284);

    tft.setTextDatum(TL_DATUM);

    for (int i = 0; i < 16; i++) { delay(100); yield(); }   // ~1.6s
}

void Display::showIdleScreen(const char* message) {
    if (!_initialized) return;

    Serial.printf("[Display] showIdleScreen: %s\n", message);

    // Blue background
    tft.fillScreen(TFT_NAVY);
    Serial.println("[Display] Navy background");

    // White header bar
    tft.fillRect(0, 0, _width, 50, TFT_WHITE);
    tft.setTextColor(TFT_BLUE, TFT_WHITE);
    tft.setTextSize(2);
    tft.setCursor(20, 30);
    tft.println("AI ASSISTANT");
    Serial.println("[Display] Drew header");

    // Large center text
    tft.setTextColor(TFT_WHITE, TFT_NAVY);
    tft.setTextSize(3);
    int16_t msgWidth = getTextWidth(message);
    tft.setCursor((_width - msgWidth) / 2, 130);
    tft.println(message);
    Serial.println("[Display] Drew center message");

    // Draw RFID card icon
    tft.drawRect(160, 170, 160, 100, TFT_YELLOW);
    tft.setTextSize(2);
    tft.setTextColor(TFT_YELLOW, TFT_NAVY);
    tft.setCursor(190, 200);
    tft.println("TAP YOUR");
    tft.setCursor(200, 230);
    tft.println("CARD");
    Serial.println("[Display] Drew card icon");

    // Green footer
    tft.fillRect(0, _height - 40, _width, 40, TFT_GREEN);
    tft.setTextColor(TFT_BLACK, TFT_GREEN);
    tft.setTextSize(1);
    tft.setCursor(10, _height - 25);
    tft.println("Ready to scan");
    Serial.println("[Display] Drew footer");
}

void Display::showStudentScreen(const char* name, const char* role, const char* uid) {
    if (!_initialized) return;

    Serial.printf("[Display] showStudentScreen: %s (%s)\n", name, role);

    // Green background (success)
    tft.fillScreen(TFT_GREEN);
    tft.setTextColor(TFT_BLACK, TFT_GREEN);
    tft.setTextSize(3);
    tft.setCursor(80, 50);
    tft.println("WELCOME!");
    Serial.println("[Display] Drew welcome");

    // White card for info
    tft.fillRoundRect(30, 80, 420, 140, 15, TFT_WHITE);
    tft.setTextColor(TFT_BLACK, TFT_WHITE);
    Serial.println("[Display] Drew info card");

    tft.setTextSize(3);
    tft.setCursor(60, 120);
    tft.println(name);
    Serial.println("[Display] Drew name");

    tft.setTextSize(2);
    tft.setCursor(60, 160);
    tft.print("Role: ");
    tft.setTextColor(TFT_BLUE, TFT_WHITE);
    tft.println(role);
    Serial.println("[Display] Drew role");

    tft.setTextColor(TFT_BLACK, TFT_WHITE);
    tft.setTextSize(1);
    tft.setCursor(60, 190);
    tft.print("ID: ");
    tft.println(uid);
    Serial.println("[Display] Drew UID");

    // Instructions at bottom
    tft.setTextColor(TFT_WHITE, TFT_GREEN);
    tft.setTextSize(2);
    tft.setCursor(60, 260);
    tft.println("Tap card again to logout");
    Serial.println("[Display] Drew logout instruction");
}

void Display::showRecordingScreen() {
    if (!_initialized) return;

    clear(COLOR_BG);

    // Recording header
    fillRect(0, 0, _width, 60, COLOR_RECORDING);
    tft.setTextColor(COLOR_BG);
    tft.setTextSize(3);
    tft.setCursor(160, 40);
    tft.println("RECORDING");

    // Waveform placeholder
    tft.drawRect(40, 100, 400, 120, COLOR_RECORDING);

    // Instructions
    tft.setTextColor(COLOR_TEXT);
    tft.setTextSize(2);
    tft.setCursor(80, 250);
    tft.println("Speak now...");

    tft.setTextColor(COLOR_TEXT_DIM);
    tft.setTextSize(1);
    tft.setCursor(60, 280);
    tft.println("Release button when done");

    _animationStartTime = millis();
    _animationFrame = 0;
}

void Display::updateRecordingAnimation() {
    if (!_initialized) return;

    // Animate waveform
    int16_t baseX = 60;
    int16_t baseY = 160;
    int16_t barWidth = 10;
    int16_t gap = 5;
    int16_t numBars = 30;

    // Clear previous waveform area
    fillRect(50, 110, 380, 100, COLOR_BG);
    tft.drawRect(40, 100, 400, 120, COLOR_RECORDING);

    unsigned long elapsed = millis() - _animationStartTime;
    _animationFrame = (elapsed / 100) % numBars;

    for (int i = 0; i < numBars; i++) {
        int16_t x = 50 + i * (barWidth + gap);
        int16_t height = 20 + (sin((i + _animationFrame) * 0.5) * 30 + 30);
        int16_t y = baseY - height / 2;

        uint16_t color = (i == _animationFrame) ? COLOR_RECORDING : COLOR_ACCENT;
        fillRect(x, y, barWidth, height, color);
    }
}

void Display::showProcessingScreen(const char* message) {
    if (!_initialized) return;

    clear(COLOR_BG);

    // Header
    fillRect(0, 0, _width, 50, COLOR_HEADER);
    tft.setTextColor(COLOR_ACCENT);
    tft.setTextSize(2);
    tft.setCursor(140, 35);
    tft.println("Processing");

    // Spinner
    int16_t centerX = _width / 2;
    int16_t centerY = _height / 2;
    int16_t radius = 40;

    // Draw animated spinner arc
    unsigned long elapsed = millis() % 2000;
    float angle = (elapsed / 2000.0) * 360;

    for (int i = 0; i < 8; i++) {
        float a = angle + i * 45;
        float rad = a * 3.14159 / 180;
        int16_t x1 = centerX + cos(rad) * (radius - 10);
        int16_t y1 = centerY + sin(rad) * (radius - 10);
        int16_t x2 = centerX + cos(rad) * radius;
        int16_t y2 = centerY + sin(rad) * radius;

        uint16_t color = COLOR_ACCENT;
        tft.drawLine(x1, y1, x2, y2, color);
    }

    // Message
    tft.setTextColor(COLOR_TEXT);
    tft.setTextSize(1);
    int16_t msgWidth = getTextWidth(message);
    tft.setCursor((_width - msgWidth) / 2, 260);
    tft.println(message);
}

void Display::showConversationScreen(const char* transcription, const char* response) {
    if (!_initialized) return;

    clear(COLOR_BG);

    // Header
    fillRect(0, 0, _width, 40, COLOR_HEADER);
    tft.setTextColor(COLOR_ACCENT);
    tft.setTextSize(1);
    tft.setCursor(10, 25);
    tft.println("Conversation");

    // User message
    if (transcription && strlen(transcription) > 0) {
        tft.setTextColor(COLOR_TEXT_DIM);
        tft.setTextSize(1);
        tft.setCursor(10, 60);
        tft.println("You:");

        tft.setTextColor(COLOR_TEXT);
        tft.setCursor(10, 80);

        // Word wrap for long messages
        char buffer[512];
        strncpy(buffer, transcription, sizeof(buffer) - 1);
        buffer[sizeof(buffer) - 1] = '\0';

        char* word = strtok(buffer, " ");
        int16_t x = 10;
        int16_t y = 80;
        int16_t maxWidth = _width - 20;

        while (word != nullptr && y < 200) {
            int16_t wordWidth = getTextWidth(word);

            if (x + wordWidth > maxWidth) {
                x = 10;
                y += 20;
            }

            tft.setCursor(x, y);
            tft.print(word);
            tft.print(" ");
            x += wordWidth + getTextWidth(" ");

            word = strtok(nullptr, " ");
        }
    }

    // Response
    if (response && strlen(response) > 0) {
        tft.setTextColor(COLOR_ACCENT);
        tft.setTextSize(1);
        tft.setCursor(10, 210);
        tft.println("Assistant:");

        tft.setTextColor(COLOR_TEXT);
        tft.setCursor(10, 230);
        tft.println(response);
    }
}

void Display::showErrorScreen(const char* message) {
    if (!_initialized) return;

    clear(COLOR_BG);

    // Error header
    fillRect(0, 0, _width, 60, COLOR_ERROR);
    tft.setTextColor(COLOR_BG);
    tft.setTextSize(3);
    tft.setCursor(150, 40);
    tft.println("ERROR");

    // Error icon
    fillCircle(240, 140, 50, COLOR_ERROR);
    tft.setTextColor(COLOR_BG);
    tft.setTextSize(4);
    tft.setCursor(220, 150);
    tft.println("!");

    // Message
    tft.setTextColor(COLOR_TEXT);
    tft.setTextSize(1);
    int16_t msgWidth = getTextWidth(message);
    tft.setCursor((_width - msgWidth) / 2, 220);
    tft.println(message);
}

void Display::showStatus(bool connected, bool authenticated) {
    if (!_initialized) return;

    // Status indicators in corners
    int16_t x = _width - 20;
    int16_t y = 20;
    int16_t radius = 6;

    // Connection status
    fillCircle(x, y, radius, connected ? COLOR_SUCCESS : COLOR_ERROR);

    // Auth status (below)
    fillCircle(x, y + 20, radius, authenticated ? COLOR_SUCCESS : COLOR_GRAY);
}

// ==================== Helper Functions ====================

void Display::initSPI() {
    // SPI is initialized by TFT_eSPI library
}

uint16_t Display::color565(uint8_t r, uint8_t g, uint8_t b) {
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

void Display::drawHeader(const char* title) {
    fillRect(0, 0, _width, 40, COLOR_HEADER);
    tft.setTextColor(COLOR_ACCENT);
    tft.setTextSize(2);
    tft.setCursor(10, 25);
    tft.println(title);
}

void Display::drawFooter(const char* status) {
    fillRect(0, _height - 30, _width, 30, COLOR_DARKGRAY);
    tft.setTextColor(COLOR_TEXT_DIM);
    tft.setTextSize(1);
    tft.setCursor(10, _height - 20);
    tft.println(status);
}

// Global instance
Display display;

// === TFT_eSPI Access ===
TFT_eSPI* Display::getTft() {
    return &tft;
}
