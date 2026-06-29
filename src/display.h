#pragma once

#include <Arduino.h>
#include "config.h"

// TFT Display driver for ILI9488 480x320
// Using TFT_eSPI library

class Display {
public:
    Display();
    ~Display();

    // Initialize display and touch
    void begin();

    // Clear screen with color (RGB565)
    void clear(uint16_t color = 0x0000);

    // === Basic Drawing ===

    // Draw pixel at (x,y)
    void drawPixel(int16_t x, int16_t y, uint16_t color);

    // Draw line from (x0,y0) to (x1,y1)
    void drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color);

    // Draw rectangle
    void drawRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);
    void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);

    // Draw rounded rectangle
    void fillRoundRect(int16_t x, int16_t y, int16_t w, int16_t h, int16_t radius, uint16_t color);

    // Draw circle
    void drawCircle(int16_t x, int16_t y, int16_t r, uint16_t color);
    void fillCircle(int16_t x, int16_t y, int16_t r, uint16_t color);

    // === Text Rendering ===

    // Set text cursor position
    void setCursor(int16_t x, int16_t y);

    // Set text size (1-8)
    void setTextSize(uint8_t size);

    // Set text color (foreground, background)
    // Use 0xFFFF for transparent background
    void setTextColor(uint16_t color, uint16_t bgcolor = 0xFFFF);

    // Print text (supports printf-style formatting)
    void print(const char* text);
    void println(const char* text);
    void printf(const char* format, ...);

    // Get text bounds for alignment
    int16_t getTextWidth(const char* text);
    int16_t getTextHeight();

    // === UI Screens ===

    // Splash screen with project name and logo
    void showSplashScreen();

    // Main idle screen - "Tap your card"
    void showIdleScreen(const char* message = "Tap your RFID card");

    // Student info screen after authentication
    void showStudentScreen(const char* name, const char* role, const char* uid);

    // Recording screen with animation
    void showRecordingScreen();
    void updateRecordingAnimation();  // Call periodically for animation

    // Processing screen
    void showProcessingScreen(const char* message = "Processing...");

    // Conversation/result screen
    void showConversationScreen(const char* transcription, const char* response = nullptr);

    // Error screen
    void showErrorScreen(const char* message);

    // Status indicator (small corner indicator)
    void showStatus(bool connected, bool authenticated);

    // === Access to underlying TFT_eSPI instance ===
    // For advanced drawing operations (robot face, etc.)
    class TFT_eSPI* getTft();

    // === Color Definitions ===
    static constexpr uint16_t COLOR_BLACK   = 0x0000;
    static constexpr uint16_t COLOR_WHITE   = 0xFFFF;
    static constexpr uint16_t COLOR_RED     = 0xF800;
    static constexpr uint16_t COLOR_GREEN   = 0x07E0;
    static constexpr uint16_t COLOR_BLUE    = 0x001F;
    static constexpr uint16_t COLOR_YELLOW  = 0xFFE0;
    static constexpr uint16_t COLOR_CYAN    = 0x07FF;
    static constexpr uint16_t COLOR_MAGENTA = 0xF81F;
    static constexpr uint16_t COLOR_ORANGE  = 0xFD20;
    static constexpr uint16_t COLOR_GRAY    = 0x8410;
    static constexpr uint16_t COLOR_DARKGRAY = 0x4208;
    static constexpr uint16_t COLOR_NAVY    = 0x000F;
    static constexpr uint16_t COLOR_MAROON  = 0x7800;
    static constexpr uint16_t COLOR_PURPLE  = 0x780F;
    static constexpr uint16_t COLOR_OLIVE   = 0x7BE0;
    static constexpr uint16_t COLOR_LIME    = 0x07E0;
    static constexpr uint16_t COLOR_AQUA    = 0x07FF;

    // Custom colors for UI
    static constexpr uint16_t COLOR_BG          = 0x0000;      // Black background
    static constexpr uint16_t COLOR_CARD       = 0x001F;      // Dark blue card
    static constexpr uint16_t COLOR_TEXT       = 0xFFFF;      // White text
    static constexpr uint16_t COLOR_TEXT_DIM   = 0x8410;      // Dimmed text
    static constexpr uint16_t COLOR_ACCENT     = 0x07E0;      // Green accent
    static constexpr uint16_t COLOR_ERROR      = 0xF800;      // Red error
    static constexpr uint16_t COLOR_WARNING    = 0xFFE0;      // Yellow warning
    static constexpr uint16_t COLOR_HEADER     = 0x0010;      // Dark blue header
    static constexpr uint16_t COLOR_RECORDING  = 0xF800;      // Red for recording
    static constexpr uint16_t COLOR_SUCCESS    = 0x07E0;      // Green success

private:
    bool _initialized;
    int16_t _width;
    int16_t _height;
    uint8_t _rotation;

    // Animation state
    unsigned long _animationStartTime;
    int16_t _animationFrame;

    // Helper functions
    void initSPI();
    uint16_t color565(uint8_t r, uint8_t g, uint8_t b);
    void drawHeader(const char* title);
    void drawFooter(const char* status);
};

// Global display instance
extern Display display;
