// src/FirmwareUpdater.h

#ifndef FIRMWARE_UPDATER_H
#define FIRMWARE_UPDATER_H

#include "HardwareManager.h"
#include "app_common.h"

class FirmwareUpdater {
public:
    FirmwareUpdater(HardwareManager* hardware);
    void checkForUpdates();

private:
    HardwareManager* _hardware;
    const char* _manifestUrl = "https://raw.githubusercontent.com/Lopagg/Zulu-Game-System/main/firmware/firmware.json";
};

#endif