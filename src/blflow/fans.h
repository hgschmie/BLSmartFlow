#ifndef _FAN
#define _FAN

#include "types.h"

const int FanPower1 = 17;
const int FanPower2 = 16;

void fansetup() {
    pinMode(FanPower1, OUTPUT);
    pinMode(FanPower2, OUTPUT);
}

void fanloop() {
    const float temperature = printerVariables.nozzletemp;
    const int speed1 = printerConfig.staticFan
        ? clampFanSpeed(printerConfig.staticFanSpeed)
        : calculateFanSpeed(printerConfig.fanGraph, temperature);
    int speed2 = speed1;
    if (!printerConfig.fansSynchronized) {
        speed2 = printerConfig.staticFan2
            ? clampFanSpeed(printerConfig.staticFanSpeed2)
            : calculateFanSpeed(printerConfig.fanGraph2, temperature);
    }
    globalVariables.fanSpeed = speed1;
    globalVariables.fanSpeed2 = speed2;
    analogWrite(FanPower1, map(speed1, 0, 100, 0, 255));
    analogWrite(FanPower2, map(speed2, 0, 100, 0, 255));
}

#endif
