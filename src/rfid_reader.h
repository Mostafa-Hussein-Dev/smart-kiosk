#pragma once

#include <Arduino.h>
#include "config.h"
#include <MFRC522.h>
#include <SPI.h>

class RfidReader {
public:
    RfidReader() : _mfrc522(RFID_SS_PIN, RFID_RST_PIN) {}

    void begin();
    void poll();
    bool cardDetected();
    String getLastUID();

private:
    MFRC522 _mfrc522;
    String  _lastUID;
    bool    _detected;
    unsigned long _lastDetectTime;
    String  _lastDetectedUID;

    String uidToHex(MFRC522::Uid& uid);
};
