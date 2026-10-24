#ifndef BLFLOW_FAN_CONFIG_H
#define BLFLOW_FAN_CONFIG_H

#include <ArduinoJson.h>
#include "types.h"

inline void writeFanCurve(JsonArray array, const FanCurve &curve) {
    for (const auto &point : curve) {
        JsonObject item = array.add<JsonObject>();
        item["temp"] = point.first;
        item["speed"] = point.second;
    }
}

inline bool readFanCurve(JsonVariantConst value, FanCurve &curve) {
    if (!value.is<JsonArrayConst>()) return false;
    FanCurve parsed;
    for (JsonVariantConst point : value.as<JsonArrayConst>()) {
        if (!point["temp"].is<float>() || !point["speed"].is<int>()) return false;
        parsed.emplace_back(point["temp"].as<float>(), point["speed"].as<int>());
    }
    if (!normalizeFanCurve(parsed)) return false;
    curve = std::move(parsed);
    return true;
}

inline void initializeSecondFan(PrinterConfig &config) {
    if (config.fanGraph2.empty()) {
        config.fanGraph2 = config.fanGraph;
        config.staticFan2 = config.staticFan;
        config.staticFanSpeed2 = config.staticFanSpeed;
    }
}

#endif
