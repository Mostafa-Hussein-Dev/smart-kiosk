// ═══════════════════════════════════════════════════════════════
// TFT_eSPI User Setup for ILI9488 480x320 Display
// ESP32-S3 DevKitC-1
// ═══════════════════════════════════════════════════════════════

#define USER_SETUP_ID 100

// ───────────────────────────────────────────────────────────────
// Processor Configuration
// ───────────────────────────────────────────────────────────────
#ifndef ESP32
  #define ESP32
#endif

// ───────────────────────────────────────────────────────────────
// Display Driver
// ───────────────────────────────────────────────────────────────
#define ILI9488_DRIVER

// ───────────────────────────────────────────────────────────────
// Pin Definitions - MUST MATCH config.h
// ═══════════════════════════════════════════════════════════════
#define TFT_CS   5    // Chip select
#define TFT_DC   6    // Data/Command
#define TFT_RST  7    // Reset
#define TFT_MOSI 9    // SPI MOSI
#define TFT_MISO -1   // SPI MISO DISABLED
#define TFT_SCLK 4    // SPI SCK

// ───────────────────────────────────────────────────────────────
// SPI Port
// ───────────────────────────────────────────────────────────────
#define USE_HSPI_PORT

// ───────────────────────────────────────────────────────────────
// SPI Frequency - Maximum for ILI9488 on ESP32-S3
// ───────────────────────────────────────────────────────────────
#define SPI_FREQUENCY  55000000      // 55 MHz write
#define SPI_READ_FREQUENCY  27000000 // 27 MHz read

// ───────────────────────────────────────────────────────────────
// Font Settings - minimal to save memory
// ───────────────────────────────────────────────────────────────
#define LOAD_GLCD   // Font 1. Original Adafruit 8 pixel font
#define LOAD_FONT2  // Font 2. Small 16 pixel high font
#define LOAD_FONT4  // Font 4. Medium 26 pixel high font
#define LOAD_GFXFF  // FreeFonts

// #define SMOOTH_FONT  // DISABLED

// ───────────────────────────────────────────────────────────────
// Color Order
// ───────────────────────────────────────────────────────────────
#define TFT_RGB_ORDER TFT_RGB

// ───────────────────────────────────────────────────────────────
// Disable unused features
// ───────────────────────────────────────────────────────────────
#define DISABLE_ALL_LIBRARY_WARNINGS
