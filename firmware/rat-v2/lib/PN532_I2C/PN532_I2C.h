/**
 * @file PN532_I2C.h
 * @brief Driver I2C FreeRTOS-aware per PN532 con lock del mutex limitato alle sole transazioni Wire
 *        e lettura Single-Pass (senza doppio ciclo NACK).
 */

#ifndef __PN532_I2C_H__
#define __PN532_I2C_H__

#include <Wire.h>
#include "PN532Interface.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

class PN532_I2C : public PN532Interface {
public:
    PN532_I2C(TwoWire &wire);

    void begin();
    void wakeup();

    /**
     * @brief Associa il mutex del bus I2C condiviso. Quando impostato, il mutex viene
     *        acquisito solo durante i trasferimenti fisici Wire e rilasciato durante le attese RF.
     */
    void setMutex(SemaphoreHandle_t mutex) { _mutex = mutex; }

    virtual int8_t writeCommand(const uint8_t *header, uint8_t hlen, const uint8_t *body = 0, uint8_t blen = 0);
    int16_t readResponse(uint8_t buf[], uint8_t len, uint16_t timeout);

private:
    TwoWire* _wire;
    SemaphoreHandle_t _mutex;
    uint8_t command;

    int8_t readAckFrame();

    inline void lockBus() {
        if (_mutex) xSemaphoreTake(_mutex, portMAX_DELAY);
    }

    inline void unlockBus() {
        if (_mutex) xSemaphoreGive(_mutex);
    }

    inline uint8_t write(uint8_t data) {
        return _wire->write(data);
    }

    inline uint8_t read() {
        return _wire->read();
    }
};

#endif