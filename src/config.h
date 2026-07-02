#pragma once

// ════════════════════════════════════════════════════════════════
//  Smart University Assistant — ESP32-S3 Kiosk Configuration
//  Board: ESP32-S3-DevKitC-1 N16R8 (16MB flash, 8MB Octal PSRAM)
//  NOTE: GPIO 33–37 are consumed by the Octal PSRAM — never use them.
//        This is why RFID lives on 10–14, not 35–37 (old docs were wrong).
// ════════════════════════════════════════════════════════════════

// ─── Wi-Fi ───────────────────────────────────────────────
#define WIFI_SSID     "Abo Arab"
#define WIFI_PASSWORD "98754321"

// ─── Backend ─────────────────────────────────────────────
// Local dev server on this PC (must be on the same Wi-Fi as the kiosk).
// Run: uvicorn app.main:app --host 0.0.0.0 --port 8000
#define BACKEND_URL   "http://192.168.10.196:8000"
// Deployed fallback: "https://smart-university-api-1wa7.onrender.com"

// ─── RC522 RFID (SPI — global FSPI bus) ──────────────────
#define RFID_SCK_PIN  12   // SPI SCK
#define RFID_MOSI_PIN 11   // SPI MOSI
#define RFID_MISO_PIN 13   // SPI MISO
#define RFID_SS_PIN   10   // Chip Select
#define RFID_RST_PIN  14   // Reset

// ─── TFT Display (ILI9488 480x320 — TFT_eSPI on HSPI) ────
// Pins MUST match TFT_CONFIG/User_Setup.h. MISO disabled (ILI9488 SDO
// does not tri-state on a shared bus).
#define TFT_CS_PIN     5
#define TFT_DC_PIN     6
#define TFT_RST_PIN    7
#define TFT_MOSI_PIN   9
#define TFT_SCK_PIN    4
#define TFT_MISO_PIN  -1
#define TFT_LED_PIN   16   // Backlight control (GPIO 16, now exclusive — touch removed)
#define TFT_BACKLIGHT_ON HIGH
#define TFT_ROTATION  1    // Landscape (480x320)

// Touch screen: NOT USED. Removed entirely to free GPIO 16/17.

// ─── I2S Microphone (INMP441) — I2S port 0 ───────────────
// On 40/39/41 — the pins the standalone test showed any life on.
#define MIC_I2S_PORT  I2S_NUM_0
#define MIC_SCK_PIN   40    // I2S bit clock (BCLK)
#define MIC_WS_PIN    39    // I2S word select (LRCL)
#define MIC_SD_PIN    41    // I2S data in (DOUT of mic)
// INMP441 L/R pin -> GND (left channel). VDD -> 3.3V.

// ─── I2S Speaker Amp (MAX98357A) — I2S port 1 ────────────
#define SPK_I2S_PORT  I2S_NUM_1
#define SPK_BCLK_PIN  21    // I2S bit clock
#define SPK_LRC_PIN   47    // I2S word select (LRC)
#define SPK_DOUT_PIN  48    // I2S data out (DIN of amp)
// MAX98357A: VIN -> 5V (NOT 3.3V — output power scales with supply),
// GND -> GND, SD -> VIN (always enabled). GAIN pin for loudness:
// 100k->GND = 15dB (loudest), direct->GND = 12dB, floating = 9dB.
// Speaker: 8 ohm 5W to amp output.

// ─── Onboard RGB LED (WS2812 NeoPixel) ───────────────────
// Moved off 41 (now used by the mic). 38 = onboard RGB on ESP32-S3-DevKitC-1
// v1.1 (the original board used 48, but that's our speaker DOUT). If the
// status LED doesn't light after flashing, your board's onboard LED is on a
// different pin — tell me and we'll pick a free one.
#define RGB_LED_PIN   38

// ─── Push-to-Talk Button ─────────────────────────────────
#define PTT_BUTTON_PIN  42   // Momentary, active LOW (INPUT_PULLUP)

// ─── Timeouts (milliseconds) ─────────────────────────────
#define INACTIVITY_TIMEOUT_MS  300000   // 5 minutes auto-logout
#define TOKEN_LIFETIME_MS      900000   // 15 minutes hard token lifetime

// ─── RFID Debounce ───────────────────────────────────────
#define RFID_DEBOUNCE_MS       2000     // ignore same UID within 2s

// ─── Input source ────────────────────────────────────────
// 1 = TEXT INPUT: the query is a sentence typed into the USB serial monitor
//     (115200 baud, send a line + Enter). The backend answers and the speaker
//     replies. Used while the I2S microphone is unavailable.
// 0 = MIC INPUT: hold the PTT button and speak (live microphone).
// Everything else (Wi-Fi, RFID auth, robot face, TTS playback) is identical
// either way — this only swaps how the question is captured.
#define USE_SERIAL_TEXT_INPUT  1

// ─── Audio Constants ─────────────────────────────────────
#define AUDIO_SAMPLE_RATE      16000
#define AUDIO_BITS_PER_SAMPLE  16
#define MAX_RECORD_SECONDS     7
// 7 sec * 16000 Hz * 2 bytes = 224,000 bytes (allocated in PSRAM)
#define PCM_BUFFER_SIZE        (MAX_RECORD_SECONDS * AUDIO_SAMPLE_RATE * (AUDIO_BITS_PER_SAMPLE / 8))
#define WAV_HEADER_SIZE        44
// Minimum captured PCM bytes to bother sending (~0.25s) — rejects accidental taps
#define MIN_RECORD_BYTES       8000

// ─── Pins to AVOID on ESP32-S3 ───────────────────────────
// GPIO0  — BOOT strapping pin (left as boot/flash only)
// GPIO19/20 — USB_D-/D+ (USB-OTG)
// GPIO33–37 — Octal PSRAM (N16R8)
// GPIO43/44 — U0TXD/U0RXD (Serial)
// GPIO45/46 — strapping pins
