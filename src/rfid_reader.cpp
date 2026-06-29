#include "rfid_reader.h"
#include "config.h"
#include <SPI.h>

void RfidReader::begin() {
    Serial.println("[RFID] Initializing...");

    // Ensure TFT is not selected
    digitalWrite(TFT_CS_PIN, HIGH);
    pinMode(TFT_CS_PIN, OUTPUT);
    digitalWrite(TFT_DC_PIN, HIGH);
    pinMode(TFT_DC_PIN, OUTPUT);
    delay(100);

    // Initialize FSPI for RC522
    SPI.begin(RFID_SCK_PIN, RFID_MISO_PIN, RFID_MOSI_PIN, RFID_SS_PIN);
    delay(100);

    // Configure SPI for RC522 (lower frequency for better clone compatibility)
    SPI.setFrequency(250000);
    SPI.setBitOrder(MSBFIRST);
    SPI.setDataMode(SPI_MODE0);

    // Configure pins
    pinMode(RFID_SS_PIN, OUTPUT);
    digitalWrite(RFID_SS_PIN, HIGH);
    pinMode(RFID_RST_PIN, OUTPUT);

    // Reset sequence for RC522
    digitalWrite(RFID_RST_PIN, LOW);
    delay(50);
    digitalWrite(RFID_RST_PIN, HIGH);
    delay(50);

    // Initialize MFRC522
    _mfrc522.PCD_Init(RFID_SS_PIN, RFID_RST_PIN);
    delay(100);

    // Read firmware version
    byte v = _mfrc522.PCD_ReadRegister(MFRC522::VersionReg);
    Serial.printf("[RFID] RC522 Version: 0x%02X ", v);

    if (v == 0x91 || v == 0x90) {
        Serial.println("(Original RC522)");
    } else if (v == 0x92) {
        Serial.println("(RC522 v2.0)");
    } else if (v == 0xB2 || v == 0xEE) {
        Serial.println("(Clone RC522)");
    } else if (v == 0x00 || v == 0xFF) {
        Serial.println("- NO COMMUNICATION! Check wiring.");
    } else {
        Serial.printf("(Unknown chip)\n");
    }

    _detected = false;
    _lastDetectTime = 0;
    _lastDetectedUID = "";

    delay(200);
    Serial.println("[RFID] Ready to scan cards!");
}

void RfidReader::poll() {
    _detected = false;

    // Ensure TFT is not selected BEFORE any RFID operations
    digitalWrite(TFT_CS_PIN, HIGH);

    // Debug: show polling activity every 5 seconds
    static unsigned long lastPollDebug = 0;
    static unsigned long cardDetectCount = 0;

    if (millis() - lastPollDebug > 5000) {
        byte rxCfg = _mfrc522.PCD_ReadRegister(MFRC522::RFCfgReg);
        byte txControl = _mfrc522.PCD_ReadRegister(MFRC522::TxControlReg);

        Serial.printf("[RFID] Polling... cards_seen=%lu, RFCfg=0x%02X, TxControl=0x%02X\n",
                     cardDetectCount, rxCfg, txControl);
        lastPollDebug = millis();

        // Ensure antenna is enabled
        if ((txControl & 0x03) == 0) {
            Serial.println("[RFID] Antenna disabled! Re-enabling...");
            _mfrc522.PCD_WriteRegister(MFRC522::TxControlReg, 0x83);
        }
    }

    // Check if a card is present
    if (!_mfrc522.PICC_IsNewCardPresent()) {
        return;
    }

    Serial.println("[RFID] Card detected! Reading...");

    // Try to read the card serial
    if (!_mfrc522.PICC_ReadCardSerial()) {
        Serial.println("[RFID] Failed to read card serial");
        _mfrc522.PICC_HaltA();
        return;
    }

    String uid = uidToHex(_mfrc522.uid);

    // Debounce
    if (uid == _lastDetectedUID && (millis() - _lastDetectTime) < RFID_DEBOUNCE_MS) {
        _mfrc522.PICC_HaltA();
        _mfrc522.PCD_StopCrypto1();
        return;
    }

    _lastUID = uid;
    _lastDetectedUID = uid;
    _lastDetectTime = millis();
    _detected = true;
    cardDetectCount++;

    Serial.printf("[RFID] Card: %s (size=%d, total=%lu)\n",
                 uid.c_str(), _mfrc522.uid.size, cardDetectCount);

    _mfrc522.PICC_HaltA();
    _mfrc522.PCD_StopCrypto1();
}

bool RfidReader::cardDetected() {
    return _detected;
}

String RfidReader::getLastUID() {
    return _lastUID;
}

String RfidReader::uidToHex(MFRC522::Uid& uid) {
    String hex = "";
    for (byte i = 0; i < uid.size; i++) {
        if (uid.uidByte[i] < 0x10) hex += "0";
        hex += String(uid.uidByte[i], HEX);
    }
    hex.toUpperCase();
    return hex;
}
