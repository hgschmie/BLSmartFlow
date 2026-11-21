#ifndef _INDICATOR
#define _INDICATOR

#include "types.h"
#include <Arduino.h>

const int baseInterval = 200;
const int LED_PIN = 21;

struct ErrorPattern {
    String error;
    int blinkCount;
};

ErrorPattern errors[] = {
    {"no config", 1},
    {"no wifi", 2},
    {"no mqtt", 3},
};

void indicatorloop() {
    if (printerVariables.errorcode != ""){
        int blinkTimes = 0;
        for (auto &err : errors) {
            if (err.error == printerVariables.errorcode) {
                blinkTimes = err.blinkCount;
                break;
            }
        }

        if (blinkTimes == 0) {
            digitalWrite(LED_PIN, HIGH);
            return;
        }

        // Keep the same blink pattern without delaying HTTP, DNS or fan control.
        const uint32_t flashingDuration = blinkTimes * 2 * baseInterval;
        const uint32_t cycle = flashingDuration + 4 * baseInterval;
        const uint32_t phase = static_cast<uint32_t>(millis()) % cycle;
        digitalWrite(LED_PIN, phase < flashingDuration &&
                     (phase / baseInterval) % 2 == 0 ? HIGH : LOW);
    }else{
        digitalWrite(LED_PIN, HIGH);
    }
}

void indicatorsetup(){
    pinMode(LED_PIN, OUTPUT);
}

#endif
