#!/usr/bin/env python3
"""
Capture the base64 WAV that the MAIN firmware dumps over serial after a PTT
recording, decode it, and save + open mic_test.wav.

Usage:   python listen_mic.py [COM_PORT]      (default COM6)

Steps:
  1) Close any serial monitor (this needs the port free).
  2) Run this script.
  3) Wait for the board to boot / connect Wi-Fi (logs scroll by).
  4) On the device: HOLD the PTT button and speak, then release.
  5) mic_test.wav is saved next to this script and opens automatically.

Requires: pip install pyserial
"""
import sys
import os
import base64

try:
    import serial  # pyserial
except ImportError:
    print("pyserial not installed. Run:  pip install pyserial")
    sys.exit(1)

PORT = sys.argv[1] if len(sys.argv) > 1 else "COM6"
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "mic_test.wav")


def main():
    print(f"Opening {PORT} @ 115200 ...")
    try:
        ser = serial.Serial(PORT, 115200, timeout=1)
    except Exception as e:
        print(f"ERROR opening {PORT}: {e}\nClose the serial monitor and retry.")
        sys.exit(1)

    print("Listening. On the device: HOLD PTT, speak, release.\n")
    collecting = False
    b64 = []
    while True:
        line = ser.readline().decode("utf-8", errors="ignore").rstrip("\r\n")
        if not line:
            continue
        if line == "[WAV_START]":
            collecting = True
            b64 = []
            print(">>> capturing WAV ...")
            continue
        if line == "[WAV_END]":
            break
        if collecting:
            b64.append(line)
        else:
            print("   " + line)   # echo firmware logs (stats, transcript, etc.)

    ser.close()

    raw = base64.b64decode("".join(b64))
    if raw[:4] != b"RIFF":
        print(f"ERROR: captured data is not a WAV (starts {raw[:8]!r})")
        sys.exit(1)

    with open(OUT, "wb") as f:
        f.write(raw)
    secs = (len(raw) - 44) / 32000.0
    print(f"\nSaved: {OUT}  ({len(raw)} bytes, ~{secs:.1f}s, 16kHz mono)")
    try:
        os.startfile(OUT)  # type: ignore[attr-defined]
        print("Opening in your default player...")
    except Exception:
        print("Open the file above to listen.")


if __name__ == "__main__":
    main()
