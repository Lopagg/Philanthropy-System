/**
 * @file PN532_I2C.cpp
 * @brief Implementazione I2C ad alta efficienza per PN532 su ESP32-S3 (FreeRTOS).
 *
 * Ottimizzazioni rispetto alla libreria standard Seeed/Adafruit:
 * - Lock del mutex I2C confinato alle sole transazioni fisiche Wire (~0.8 - 2.2 ms).
 * - Rilascio completo del bus I2C durante l'attesa di scansione RF (zero blocchi su LCD/OLED).
 * - Eliminazione del doppio ciclo getResponseLength() + PN532_NACK: la risposta viene letta
 *   in un'unica transazione I2C non appena il bit RDY diventa 1.
 */

#include "PN532_I2C.h"
#include "PN532_debug.h"
#include "Arduino.h"

#define PN532_I2C_ADDRESS (0x48 >> 1)

PN532_I2C::PN532_I2C(TwoWire &wire)
    : _wire(&wire),
      _mutex(nullptr),
      command(0) {}

void PN532_I2C::begin() {
    _wire->begin();
}

void PN532_I2C::wakeup() {
    vTaskDelay(pdMS_TO_TICKS(500));
}

int8_t PN532_I2C::writeCommand(const uint8_t *header, uint8_t hlen, const uint8_t *body, uint8_t blen) {
    command = header[0];

    lockBus();
    _wire->beginTransmission(PN532_I2C_ADDRESS);

    write(PN532_PREAMBLE);
    write(PN532_STARTCODE1);
    write(PN532_STARTCODE2);

    uint8_t length = hlen + blen + 1; // TFI + DATA
    write(length);
    write(~length + 1);               // LCS (checksum di length)

    write(PN532_HOSTTOPN532);
    uint8_t sum = PN532_HOSTTOPN532;

    for (uint8_t i = 0; i < hlen; i++) {
        if (write(header[i])) {
            sum += header[i];
        } else {
            _wire->endTransmission();
            unlockBus();
            return PN532_INVALID_FRAME;
        }
    }

    for (uint8_t i = 0; i < blen; i++) {
        if (write(body[i])) {
            sum += body[i];
        } else {
            _wire->endTransmission();
            unlockBus();
            return PN532_INVALID_FRAME;
        }
    }

    uint8_t checksum = ~sum + 1;      // DCS (checksum di TFI + DATA)
    write(checksum);
    write(PN532_POSTAMBLE);

    _wire->endTransmission();
    unlockBus();

    return readAckFrame();
}

int8_t PN532_I2C::readAckFrame() {
    const uint8_t PN532_ACK[] = {0, 0, 0xFF, 0, 0xFF, 0};
    uint8_t ackBuf[sizeof(PN532_ACK)];

    // Concede 1 ms alla CPU 80C51 del PN532 per elaborare il comando e preparare l'ACK
    // (evita un primo polling I2C a vuoto)
    vTaskDelay(pdMS_TO_TICKS(1));

    uint16_t time = 0;
    do {
        lockBus();
        uint8_t got = _wire->requestFrom(PN532_I2C_ADDRESS, (int)sizeof(PN532_ACK) + 1);
        if (got > 0 && (read() & 1)) {
            for (uint8_t i = 0; i < sizeof(PN532_ACK); i++) {
                ackBuf[i] = read();
            }
            unlockBus();

            if (memcmp(ackBuf, PN532_ACK, sizeof(PN532_ACK)) != 0) {
                return PN532_INVALID_ACK;
            }
            return 0;
        }
        unlockBus();

        vTaskDelay(pdMS_TO_TICKS(2));
        time += 2;
        if (time > PN532_ACK_WAIT_TIME) {
            return PN532_TIMEOUT;
        }
    } while (1);
}

int16_t PN532_I2C::readResponse(uint8_t buf[], uint8_t len, uint16_t timeout) {
    uint16_t time = 0;

    // Numero massimo di byte da richiedere sul bus I2C in un'unica transazione:
    // [RDY(1)] + [00 00 FF(3)] + [LEN(1)] + [LCS(1)] + [TFI + CMD(2)] + [Payload] + [DCS(1)] + [00(1)]
    // Per tutte le operazioni del RAT-V2 (UID fino a 7 byte), il frame totale è <= 24 byte.
    uint8_t reqLen = (len > 22) ? 30 : (len + 8);

    // Breve attesa iniziale fuori dal mutex mentre il PN532 avvia la scansione RF
    vTaskDelay(pdMS_TO_TICKS(2));
    time += 2;

    uint8_t frameLen = 0;

    do {
        lockBus();
        uint8_t got = _wire->requestFrom(PN532_I2C_ADDRESS, (int)reqLen);
        if (got > 0 && (read() & 1)) {
            // Il PN532 ha completato l'operazione: leggiamo direttamente l'intero frame già nel buffer Wire!
            if (read() != 0x00 || read() != 0x00 || read() != 0xFF) {
                unlockBus();
                return PN532_INVALID_FRAME;
            }

            frameLen = read();
            if (0 != (uint8_t)(frameLen + read())) { // Verifica LCS
                unlockBus();
                return PN532_INVALID_FRAME;
            }

            uint8_t cmd = command + 1;
            if (PN532_PN532TOHOST != read() || cmd != read()) {
                unlockBus();
                return PN532_INVALID_FRAME;
            }

            if (frameLen < 2) {
                unlockBus();
                return PN532_INVALID_FRAME;
            }

            uint8_t payloadLen = frameLen - 2;
            if (payloadLen > len) {
                unlockBus();
                return PN532_NO_SPACE;
            }

            uint8_t sum = PN532_PN532TOHOST + cmd;
            for (uint8_t i = 0; i < payloadLen; i++) {
                buf[i] = read();
                sum += buf[i];
            }

            uint8_t checksum = read();
            read(); // POSTAMBLE
            unlockBus();

            if (0 != (uint8_t)(sum + checksum)) {
                return PN532_INVALID_FRAME;
            }
            return payloadLen;
        }
        unlockBus();

        // Il PN532 sta ancora interrogando il campo RF: dorme 3ms lasciando libero il bus I2C!
        vTaskDelay(pdMS_TO_TICKS(3));
        time += 3;

        if ((0 != timeout) && (time > timeout)) {
            return -1;
        }
    } while (1);
}