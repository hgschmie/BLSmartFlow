#ifndef BLFLOW_DEBUG_LOG_H
#define BLFLOW_DEBUG_LOG_H

#include <atomic>
#include "types.h"

enum DebugLogFlag : uint8_t { DebugGeneral = 1, DebugChanges = 2, DebugMqtt = 4 };
// Publish a snapshot after loading/saving settings. Wi-Fi event callbacks must
// not read printerConfig while the main loop replaces it.
std::atomic<uint8_t> debugLogFlags{0};

void applyDebugLogging() {
    uint8_t flags = 0;
    if (printerConfig.debuging) {
        flags = DebugGeneral;
        if (printerConfig.debugingchange) flags |= DebugChanges;
        if (printerConfig.mqttdebug) flags |= DebugMqtt;
    }
    debugLogFlags.store(flags);
}

bool debugLoggingEnabled() { return (debugLogFlags.load() & DebugGeneral) != 0; }
bool changeLoggingEnabled() { return (debugLogFlags.load() & DebugChanges) != 0; }
bool mqttLoggingEnabled() { return (debugLogFlags.load() & DebugMqtt) != 0; }

#endif
