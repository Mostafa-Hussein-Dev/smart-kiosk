# Setup — Smart University Kiosk (ESP32-S3 Firmware)

PlatformIO firmware for the ESP32-S3 kiosk. The backend it talks to lives in a
separate repo (`smart-university`) with its own `SETUP.md`.

- **Board:** ESP32-S3-DevKitC-1 N16R8 (16MB flash, 8MB Octal PSRAM)
- **Framework:** Arduino via PlatformIO (`espressif32@6.10.0`)
- **Display:** ILI9488 480×320 over HSPI (TFT_eSPI)

## Prerequisites

- **VS Code + PlatformIO IDE extension** (or PlatformIO Core CLI)
- **USB-serial driver** for the DevKitC-1 UART bridge (CP210x or CH340, per your board)

PlatformIO auto-downloads the platform, toolchain, and all `lib_deps` on the first
build — no manual library installs.

> Not committed / regenerated automatically: `.pio/` and `build/`. Do not copy them
> between machines; they are rebuilt locally.

## 1. Clone

```bash
git clone https://github.com/Mostafa-Hussein-Dev/smart-kiosk.git
cd smart-kiosk
```

Open the folder in VS Code (PlatformIO detects it), or use the CLI below.

## 2. First build

```bash
pio run
```

This downloads everything on first run. It will take a few minutes.

## 3. Restore the TFT display config (REQUIRED)

`platformio.ini` pulls TFT_eSPI fresh from GitHub `master`, so the first build
installs a **default** `User_Setup.h` that does NOT match this display — the screen
stays blank/garbled until you restore the project's config:

```bash
# Windows PowerShell
copy TFT_CONFIG\User_Setup.h .pio\libdeps\esp32s3\TFT_eSPI\User_Setup.h
# Git Bash / Unix
cp TFT_CONFIG/User_Setup.h .pio/libdeps/esp32s3/TFT_eSPI/User_Setup.h
```

Then rebuild. **Redo this after any `pio pkg clean` or after deleting `.pio/`.**

## 4. Set Wi-Fi and backend address

`src/config.h` is committed and clones in with the previous machine's values. Update:

```c
#define WIFI_SSID     "YourWiFi"
#define WIFI_PASSWORD "YourPassword"
// The backend PC's LAN IP (find via `ipconfig` → IPv4). NOT localhost.
#define BACKEND_URL   "http://192.168.x.x:8000"
// Deployed fallback (skips local networking):
// #define BACKEND_URL "https://smart-university-api-1wa7.onrender.com"
```

For local backend to work:

- Run the backend with `uvicorn app.main:app --host 0.0.0.0 --port 8000`
- Kiosk and laptop on the **same Wi-Fi**
- Allow inbound TCP **8000** through Windows Firewall

## 5. Upload

```bash
pio run -t upload
pio device monitor       # serial monitor at 115200 baud
```

## Input mode

`src/config.h` → `USE_SERIAL_TEXT_INPUT`:

- `1` — type the query into the USB serial monitor (mic not required)
- `0` — hold the PTT button and speak (live INMP441 microphone)

Everything else (Wi-Fi, RFID auth, robot face, TTS playback) is identical either way.

## Wiring reference

Pin assignments are documented at the top of `src/config.h` and in `pinout.txt`.
TFT pin map is in `TFT_CONFIG/README.md`. Note GPIO 33–37 are reserved by the Octal
PSRAM and must never be used.
