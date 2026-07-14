#pragma once

// ════════════════════════════════════════════════════════════════
//  Smart University Assistant — ESP32-S3 Kiosk Configuration
//  Board: ESP32-S3-DevKitC-1 N16R8 (16MB flash, 8MB Octal PSRAM)
//  NOTE: GPIO 33–37 are consumed by the Octal PSRAM — never use them.
//        This is why RFID lives on 10–14, not 35–37 (old docs were wrong).
// ════════════════════════════════════════════════════════════════

// ─── Wi-Fi ───────────────────────────────────────────────
#define WIFI_SSID     "Honor"
#define WIFI_PASSWORD "12341234"

// ─── Backend ─────────────────────────────────────────────
// Deployed backend on Render (TLS auto-selected because it starts with https).
#define BACKEND_URL   "https://smart-university-ng5p.onrender.com"
// Local dev fallback (same Wi-Fi as the kiosk): "http://10.187.48.98:8000"
//   Run: uvicorn app.main:app --host 0.0.0.0 --port 8000

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
#define TFT_LED_PIN   16   // Backlight control (GPIO 16)
#define TFT_BACKLIGHT_ON HIGH
#define TFT_ROTATION  3    // Landscape (480x320), flipped 180

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

// ─── Onboard RGB LED (WS2812) — REMOVED ──────────────────
// The status-LED feature was removed; GPIO 38 is now free.

// ─── Push-to-Talk Button ─────────────────────────────────
#define PTT_BUTTON_PIN  42   // Momentary, active LOW (INPUT_PULLUP)
// LED ring on the PTT button. Outer pin -> GPIO 8 through ~220 ohm resistor,
// other outer pin -> GND. Driven HIGH while the button is held.
#define PTT_LED_PIN     8
#define PTT_LED_ON      HIGH

// ─── Timeouts (milliseconds) ─────────────────────────────
#define INACTIVITY_TIMEOUT_MS  300000   // 5 minutes auto-logout
#define TOKEN_LIFETIME_MS      900000   // 15 minutes hard token lifetime

// ─── RFID Debounce ───────────────────────────────────────
#define RFID_DEBOUNCE_MS       2000     // ignore same UID within 2s

// ─── Audio Constants ─────────────────────────────────────
#define AUDIO_SAMPLE_RATE      16000
#define AUDIO_BITS_PER_SAMPLE  16
#define MAX_RECORD_SECONDS     7
// 7 sec * 16000 Hz * 2 bytes = 224,000 bytes (allocated in PSRAM)
#define PCM_BUFFER_SIZE        (MAX_RECORD_SECONDS * AUDIO_SAMPLE_RATE * (AUDIO_BITS_PER_SAMPLE / 8))
#define WAV_HEADER_SIZE        44
// Minimum captured PCM bytes to bother sending (~0.25s) — rejects accidental taps
#define MIN_RECORD_BYTES       8000

// ─── UI / Robot face (480x320) ───────────────────────────
// Build an RGB565 color from 8-bit components.
#define RGB565(r, g, b) ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))

// Palette (mirrors the approved mockup)
#define EYE_BLUE   RGB565(0, 188, 255)   // bright blue eyes  (~0x05FF)
#define EYE_RED    RGB565(255, 59, 48)   // error eyes
#define UI_BG      RGB565(0, 0, 0)       // black panel
#define UI_TXT     RGB565(223, 233, 238)
#define UI_DIM     RGB565(111, 135, 148)
#define UI_ACCENT  EYE_BLUE
#define UI_OK      RGB565(61, 224, 122)  // boot console [ OK ]
#define UI_WARN    RGB565(255, 194, 75)  // [WARN]
#define UI_FAIL    RGB565(255, 92, 92)   // [FAIL]
#define UI_PEND    RGB565(79, 182, 201)  // [ .. ] pending
#define UI_BAND    RGB565(4, 20, 28)     // status band background

// Eye geometry (centers, base size, corner radius)
#define EYE_CY     168
#define EYE_LX     165
#define EYE_RX     315
#define EYE_W      110
#define EYE_H      132
#define EYE_R      52

// Waveform strip (speaking / listening)
#define WAVE_Y     262
#define WAVE_X0    116
#define WAVE_X1    364
#define WAVE_BARS  21

// Top status band height
#define BAND_H     26

// ─── Pins to AVOID on ESP32-S3 ───────────────────────────
// GPIO0  — BOOT strapping pin (left as boot/flash only)
// GPIO19/20 — USB_D-/D+ (USB-OTG)
// GPIO33–37 — Octal PSRAM (N16R8)
// GPIO43/44 — U0TXD/U0RXD (Serial)
// GPIO45/46 — strapping pins
