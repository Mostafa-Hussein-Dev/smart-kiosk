# TFT_eSPI Configuration Backup

Copy this file to restore TFT configuration after library deletion/clean.

## When to Use

After running any of these commands:
- `pio run -t clean`
- `pio pkg clean`
- Deleting `.pio` folder manually

## How to Restore

**Copy this file to:**
```
.pio/libdeps/esp32s3/TFT_eSPI/User_Setup.h
```

**Command (bash/Git Bash):**
```bash
cp TFT_CONFIG/User_Setup.h .pio/libdeps/esp32s3/TFT_eSPI/User_Setup.h
```

**Command (Windows PowerShell):**
```powershell
copy TFT_CONFIG\User_Setup.h .pio\libdeps\esp32s3\TFT_eSPI\User_Setup.h
```

## Configuration Details

- **Display**: ILI9488 480x320
- **SPI Bus**: HSPI
- **SPI Frequency**: 40 MHz write, 20 MHz read
- **MISO**: Disabled (-1) - ILI9488 SDO doesn't tristate

## Pin Mapping

| TFT Pin | ESP32 GPIO |
|---------|------------|
| CS | 5 |
| DC | 6 |
| RST | 7 |
| MOSI | 9 |
| SCLK | 4 |
| MISO | NOT CONNECTED |
| BL | 16 |
