# Smart University Assistant — Hardware PRD / System Design

**Scope:** ESP32-S3 voice kiosk firmware + circuit. Hardware only.
**Status:** DRAFT — pending approval before implementation.
**Source of truth for pins:** `src/config.h` (all docs are updated to match it, not the reverse).

---

## 1. Purpose & Goals

A standalone campus kiosk that lets a student:
1. Walk up and **ask questions by voice** (public mode), or
2. **Tap an RFID card** to unlock personalized answers (GPA, schedule, etc.),

while an **animated robot face** on the TFT gives the device personality and visual feedback. The kiosk talks to the existing (finished) backend over Wi-Fi: it uploads recorded voice as WAV and plays back the MP3 answer.

### Goals
- Full, **always-on functional product** — no demo/feature-flag modes.
- Reliable **voice input** (the current module is rewritten from scratch).
- Clean **voice output** through MAX98357A → speaker.
- Robot face integrated into the live interaction (idle / listening / thinking / speaking).
- Robust enough to run unattended (watchdog **enabled**, Wi-Fi recovery, memory hygiene).

### Non-Goals (descoped for v1)
- Touch input on the TFT (touch is **not used** — pins freed).
- The local web-server WAV/MP3 test harness (was a bring-up aid; removed from the product path).
- Backend changes (backend is finished).

---

## 2. Bill of Materials (final)

| # | Component | Model | Bus | Notes |
|---|-----------|-------|-----|-------|
| 1 | MCU | ESP32-S3-DevKitC-1 **N16R8** | — | 16 MB flash, 8 MB **Octal** PSRAM (GPIO 33–37 reserved by PSRAM — do not use) |
| 2 | RFID reader | RC522 (MFRC522) | SPI | Student card auth |
| 3 | Microphone | INMP441 MEMS | I2S0 | 16 kHz mono capture — **module rewritten** |
| 4 | Audio amp (primary) | **MAX98357A** | I2S1 | I2S DAC + 3 W class-D in one; drives speaker directly |
| 4b| Audio DAC (alt) | PCM5102A (GY-PCM5102) | I2S1 | Line-level only → needs a separate amp; held as backup (see §9) |
| 5 | Speaker | 3 W 4 Ω | — | Wired to MAX98357A output |
| 6 | Display | 3.5" ILI9488 480×320 | SPI | UI + robot face; **touch unused** |
| 7 | Status LED | onboard WS2812 | 1-wire | State indication |
| 8 | Push-to-talk button | momentary | GPIO | Hold to record |
| 9 | I2C IO expander | (PCF8574-class) | I2C | **Optional** — see §8; not required for v1 |
| 10| Power | 5 V ≥ 2 A + bulk capacitor | — | Cap absorbs Wi-Fi/audio current spikes (see §7) |

---

## 3. Authoritative Pinout (from `config.h`, with the agreed fixes)

> The doc pinouts (`pinout.txt`, `component*.txt`) put RFID on GPIO 35/36/37 — **those pins are consumed by the N16R8 Octal PSRAM** and would never work. `config.h` already avoids them. This is why `config.h` is the source of truth.

| Function | Signal | GPIO |
|----------|--------|------|
| **RFID (RC522, SPI bus A)** | SCK | 12 |
| | MOSI | 11 |
| | MISO | 13 |
| | SS/CS | 10 |
| | RST | 14 |
| **TFT (ILI9488, SPI bus B)** | SCK | 4 |
| | MOSI | 9 |
| | MISO | -1 (disabled) |
| | CS | 5 |
| | DC | 6 |
| | RST | 7 |
| | Backlight (LED) | 16 |
| **Touch** | — | **REMOVED** (was CS 16 / IRQ 17 — freed; touch not used) |
| **Mic (INMP441, I2S0)** | BCLK/SCK | 8 |
| | WS/LRCL | 15 |
| | SD (data in) | 18 |
| | L/R | GND (left) |
| **Speaker (MAX98357A, I2S1)** | BCLK | 21 |
| | LRC | 47 |
| | DIN/DOUT | 48 |
| **Status LED (WS2812)** | DIN | 41 |
| **PTT button** | signal | 42 (INPUT_PULLUP, active LOW) |
| **(optional) BOOT button** | signal | 0 |
| **(optional) I2C expander** | SDA / SCL | 1 / 2 (free) |

**Agreed change #2:** `TOUCH_CS_PIN` no longer occupies GPIO 16. GPIO 16 is now **backlight only**, resolving the conflict. Touch (CS 16 / IRQ 17) is disabled.

Free GPIOs after this layout: 1, 2, 17, 19–20 (USB), 22, plus the reserved/strapping set to avoid (0 boot, 19/20 USB, 43/44 UART, 45/46 strap, 33–37 PSRAM).

---

## 4. System Architecture

```
                ┌──────────────────────── ESP32-S3 (N16R8) ────────────────────────┐
   RFID card →  │  RC522 (SPI-A) ─┐                                                  │
   Voice     →  │  INMP441 (I2S0) ─┼─► [ App State Machine ] ─► Wi-Fi ─► Backend API │ → cloud
                │  PTT button ─────┘            │      ▲                             │
   Speaker   ←  │  MAX98357A (I2S1) ◄───────────┘      │                             │
   Screen    ←  │  ILI9488 (SPI-B) ◄── Display + RobotFace                           │
   LED       ←  │  WS2812 ◄── StatusLed                                              │
                └───────────────────────────────────────────────────────────────────┘
```

- **Two separate SPI buses**: RC522 and ILI9488 each get their own pins (no sharing → no CS contention).
- **Two I2S peripherals**: mic on I2S0, amp on I2S1 (independent; used half-duplex — record, then play).
- **PSRAM** holds the 224 KB record buffer and the MP3 response buffer.

### Software modules

| Module | File(s) | Responsibility | Current state |
|--------|---------|----------------|---------------|
| `config.h` | config.h | Pins, constants, Wi-Fi/backend | exists — to be cleaned (pins, flags) |
| `StatusLed` | status_led.* | WS2812 patterns | ✅ done — review |
| `Display` | display.* | ILI9488 driver + UI screens | ✅ done — review, drop touch |
| `RobotFace` | robot_face.* | Animated face (PSRAM framebuffer) | ✅ done — integrate into states |
| `RfidReader` | rfid_reader.* | RC522 poll + UID | ✅ done — review |
| `WifiManager` | wifi_manager.* | Connect + reconnect | 🟡 written, disabled — re-enable |
| `SessionManager` | session_manager.* | Token, role, timeouts, endpoint pick | ✅ done — review |
| `ApiClient` | api_client.* | RFID auth + multipart voice POST + MP3 recv | 🟡 written, disabled — re-enable/verify |
| `AudioInput` | audio_input.* | INMP441 capture → WAV | 🔴 **rewrite from scratch** (§6) |
| `SpeakerOutput` | audio_output.* | MP3 decode → I2S1 → MAX98357A | 🟡 written, lib removed — re-enable + verify |
| `main` | main.cpp | Orchestration / state machine | 🔴 strip demo mode, wire full flow |
| `WebServer` | web_server.* | local test harness | ❌ remove from product |

---

## 5. Features & how they work

### F1 — Boot & connect
On power-up: init PSRAM check → LED slow-blink → bring up display + splash → connect Wi-Fi → health-check backend → robot face idle. If Wi-Fi fails, keep retrying in the background; the kiosk still shows the idle face and a "connecting" hint.

### F2 — Idle / attract
Robot face plays its idle animation (blink, breathing, eye movement). Screen shows a short prompt ("Tap card or hold button to talk"). LED off (unauth) / solid green (authed).

### F3 — Voice query (public)
Hold **PTT** → state `RECORDING`, face = **listening**, mic captures to PCM buffer. Release (or 7 s max) → state `SENDING`, face = **thinking** → `ApiClient` POSTs WAV (multipart) to `/api/voice/chat/public` → receives MP3 + transcription header → state `PLAYING`, face = **speaking**, screen shows the transcription/answer → MP3 plays via MAX98357A → back to idle.

### F4 — RFID authentication
Tap card → `RECORDING`-independent: `ApiClient` POSTs UID to the RFID auth endpoint → on success store JWT+role+UID in `SessionManager`, greet by name (face = happy), LED solid green. Re-tap same card = logout. Different card = switch user. Session auto-expires after 5 min inactivity (token lifetime 15 min).

### F5 — Voice query (authenticated)
Same as F3 but `SessionManager` supplies the bearer token → `ApiClient` uses `/api/voice/chat` → personalized answers. On HTTP 401 (expired) it clears auth and retries once as public.

### F6 — Robot face as unified UI
The face is the persistent visual. State drives expression: idle → `FACE_IDLE`, recording → `FACE_LISTENING`, sending → `FACE_THINKING`, playing → `FACE_SPEAKING`, error → `FACE_ERROR`, auth success → `FACE_HAPPY`. Key text (name, prompt, transcription) overlays top/bottom. No separate "robot mode" toggle — it's always the face.

### F7 — Status LED
OFF idle-unauth · solid green idle-auth · slow blue = connecting · fast blue = recording/sending · cyan double = playing · green flash = auth ok · red = error.

### F8 — Resilience
Watchdog **enabled**; main loop yields. Wi-Fi drop → error state → `WifiManager` reconnects. Buffers freed after each turn; heap logged; low-heap guard skips recording. Button debounced.

### State machine
```
IDLE_UNAUTH ─tap─► AUTH_PENDING ─ok─► IDLE_AUTH
   │ ▲                                   │ │
 PTT│ └────────── logout/expire ─────────┘ │PTT
   ▼                                        ▼
RECORDING ─release─► SENDING ─200─► PLAYING ─done─► IDLE_(UN)AUTH
                        └─error─► ERROR ─timeout─► IDLE_(UN)AUTH
```

---

## 6. Audio Input — rewrite plan (the static-noise fix)

The current module almost certainly records noise because of three classic INMP441-on-ESP32-S3 mistakes:
1. **Wrong I2S format** — uses `I2S_COMM_FORMAT_I2S_MSB`; INMP441 is standard Philips I2S (`I2S_COMM_FORMAT_STAND_I2S`). Misalignment → garbage/static.
2. **Channel slot** — with L/R tied to GND the valid data often lands on the slot opposite to what `ONLY_LEFT` reads on the S3 core; reading the empty slot → noise/silence.
3. **Bit shift/gain** — `raw >> 12` on a raw 32-bit word is arbitrary; INMP441 is 24-bit left-justified, so the correct extraction + gain (≈ `>>11…>>14`) and DC-offset removal matter.

**Rewrite will:**
- Use standard I2S (Philips) format, 32-bit slot, mono.
- Select the correct channel slot (validated empirically; expose a 1-line toggle).
- Correct 24→16-bit extraction with tuned gain + simple DC-offset/high-pass to kill rumble.
- Keep PSRAM buffer + WAV header generation; fix the `SAMPLE_RATE` vs `AUDIO_SAMPLE_RATE` mismatch.
- Add a **self-test**: log peak/RMS so we can confirm real audio (not static) over serial before involving the backend.

---

## 7. Power

- **5 V ≥ 2 A** supply (USB adapter, or battery chain 18650×2 → TP4056 → MT3608 → 5 V).
- **Bulk decoupling (your capacitor):** one **470–1000 µF** electrolytic across the **5 V rail** near the ESP32 + MAX98357A, plus **0.1 µF** ceramics at each module's VCC. This absorbs the Wi-Fi TX bursts (~brown-out risk) and audio transients that otherwise reset the board. (This is why the old code force-disabled the brown-out detector — the cap is the real fix; brown-out detector stays enabled.)
- MAX98357A peak ~500 mA; worst case = Wi-Fi TX + speaker + TFT simultaneously → the bulk cap is mandatory.

---

## 8. Optional: I2C IO expander

Not required — the S3 has enough GPIO for this BOM. **If** used (e.g., to simplify perfboard routing), put only **slow, non-timing-critical** lines on it: RC522 RST, MAX98357A SD/shutdown, the PTT button, spare LEDs. **Never** put SPI, I2S, or WS2812 on the expander (timing-critical). Default plan: **skip it for v1**, keep direct GPIO. Flag if you want it wired in.

---

## 9. Audio output decision

- **Primary: MAX98357A** — single board, I2S DAC + amp, drives the 4 Ω speaker directly. Matches `config.h` pins (21/47/48). Recommended.
- **Alternative: PCM5102A** — cleaner DAC but **line-level only** → needs an external amplifier to drive a passive speaker. Use only if you specifically want higher fidelity into a powered speaker/active monitor. Same I2S1 pins.

➡️ **Recommendation: ship MAX98357A.** Keep PCM5102A as a fidelity upgrade path.

---

## 10. Implementation plan (after approval)

1. **Config & cleanup** — finalize `config.h` pins, remove touch, fix `SAMPLE_RATE`, re-enable watchdog, delete demo flags (`ENABLE_WIFI 0`, `LOCAL_TEST_MODE`), drop `web_server`.
2. **Re-enable build** — restore audio includes/instances; add `earlephilhower/ESP8266Audio` back to `platformio.ini`.
3. **Deep-read & adjust existing modules** — display, robot_face, rfid_reader, wifi_manager, session_manager, api_client, audio_output: read fully, fix bugs, ensure they interlock.
4. **Rewrite `AudioInput`** (§6) + serial self-test.
5. **Rewrite `main.cpp` state machine** — full flow F1–F8, face integrated, no modes.
6. **Verify audio_output** with MAX98357A.
7. **Update docs** — rewrite `pinout.txt` / `component*.txt` to match `config.h` (Agreed change #1).
8. **Build instructions in chat** (Agreed item 6.4) + bring-up order.

### Acceptance criteria
- Boots, connects Wi-Fi, shows animated face, no watchdog resets over 30 min.
- PTT records **clean speech** (verified by RMS self-test + backend transcription).
- Public voice loop returns spoken answer end-to-end.
- RFID tap → greeted by name → authenticated voice loop returns personalized answer.
- Re-tap logs out; 5-min inactivity expires session.

---

## 11. Decisions — LOCKED
1. **Audio output = MAX98357A** (I2S DAC+amp). PCM5102A/PAM8403 kept as a documented backup. ✅
2. **I2C IO expander: skipped for v1** — direct GPIO. ✅
3. **Touch fully removed** — GPIO 16 is backlight-only, GPIO 17 freed. ✅
4. **BOOT button (GPIO 0): unused** — PTT (GPIO 42) is the only user control. ✅
5. **Speaker = 8 Ω 5 W.** MAX98357A drives 8 Ω at ~1.5 W — plenty for speech, lower current draw than 4 Ω. ✅

## 12. Implementation status — DONE
All firmware implemented: config cleaned, touch/web-server removed, watchdog re-enabled,
WiFi TX power restored, AudioInput rewritten, AudioOutput (MP3→MAX98357A) implemented,
ApiClient fixed (name bug + multipart overflow + cold-start timeouts), greet-by-name added,
robot face integrated into a full state machine. Docs (pinout.txt, component_updated.txt)
updated to match config.h. See the build + test guide in chat.
```
