#!/usr/bin/env python3
"""
Decode ESP32 audio from serial monitor output.

Extracts WAV data between [WAV_START] and [WAV_END] markers.
Ignores all other logs and debug output.
"""

import sys
import re
import base64
from pathlib import Path


def extract_wav(content):
    """Extract and decode base64 WAV between WAV_START and WAV_END markers"""
    # Extract content between markers (handles multi-line with re.DOTALL)
    match = re.search(r'\[WAV_START\](.*?)\[WAV_END\]', content, re.DOTALL)
    if not match:
        print("ERROR: [WAV_START]...[WAV_END] markers not found in file!")
        print("Make sure your ESP32 printed these markers in the serial output.")
        return None

    base64_data = match.group(1).strip()
    print(f"Found {len(base64_data)} characters of base64 data")

    try:
        raw_wav = base64.b64decode(base64_data)
        if raw_wav[:4] == b'RIFF':
            print(f"✓ Valid WAV file detected ({len(raw_wav)} bytes)")
            return raw_wav
        else:
            print(f"ERROR: Decoded data is not a WAV file (starts with: {raw_wav[:8]})")
            return None
    except Exception as e:
        print(f"ERROR: Failed to decode base64: {e}")
        return None


def main():
    if len(sys.argv) < 2:
        print("Usage: python decode_audio.py <input_file> [output_wav]")
        print("\nIf output_wav not specified, defaults to 'output.wav'")
        sys.exit(1)

    input_file = Path(sys.argv[1])
    output_wav = sys.argv[2] if len(sys.argv) > 2 else 'output.wav'

    if not input_file.exists():
        print(f"ERROR: {input_file} not found")
        sys.exit(1)

    # Read entire file (including all logs, debug output, etc.)
    with open(input_file, 'r', encoding='utf-8', errors='ignore') as f:
        content = f.read()

    print(f"Reading {input_file.name} ({len(content)} characters)")

    # Extract and decode WAV (ignores everything else)
    wav_data = extract_wav(content)

    if wav_data:
        with open(output_wav, 'wb') as f:
            f.write(wav_data)
        print(f"\n✓ Saved to {output_wav}")
        print(f"  Duration: ~{len(wav_data) / 32000:.1f} seconds at 16kHz mono")
    else:
        sys.exit(1)


if __name__ == '__main__':
    main()
