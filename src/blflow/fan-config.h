#ifndef BLFLOW_FAN_CONFIG_H
#define BLFLOW_FAN_CONFIG_H

#include <ArduinoJson.h>
#include "types.h"

inline void writeFanCurve(JsonArray array, const FanCurve &curve, const char *inputKey = "temp") {
    for (const auto &point : curve) {
        JsonObject item = array.add<JsonObject>();
        item[inputKey] = point.first;
        item["speed"] = point.second;
    }
}

inline bool readFanCurve(JsonVariantConst value, FanCurve &curve, bool following = false) {
    if (!value.is<JsonArrayConst>()) return false;
    FanCurve parsed;
    const char *inputKey = following ? "input" : "temp";
    for (JsonVariantConst point : value.as<JsonArrayConst>()) {
        if (!point[inputKey].is<float>() || !point["speed"].is<int>()) return false;
        parsed.emplace_back(point[inputKey].as<float>(), point["speed"].as<int>());
    }
    if (!(following ? normalizeFollowFanCurve(parsed) : normalizeFanCurve(parsed))) return false;
    curve = std::move(parsed);
    return true;
}

inline void initializeSecondFan(PrinterConfig &config) {
    if (config.fanGraph2.empty()) {
        config.fanGraph2 = config.fanGraph;
        config.staticFan2 = config.staticFan;
        config.staticFanSpeed2 = config.staticFanSpeed;
        config.followPrinterFan2 = config.followPrinterFan;
        config.printerFanSource2 = config.printerFanSource;
        config.postPrint2 = config.postPrint;
        config.followGraph2 = config.followGraph;
    }
}

inline void readPostPrintSettings(JsonVariantConst enabled, JsonVariantConst seconds,
                                  JsonVariantConst speed, PostPrintSettings &settings) {
    // Missing fields in older files keep the feature off. Invalid values cannot
    // activate a timer or produce an unsafe duration after a firmware upgrade.
    const bool validSeconds = seconds.is<int>() && seconds.as<int>() >= 1 && seconds.as<int>() <= postPrintMaxSeconds;
    const bool validSpeed = speed.is<int>() && speed.as<int>() >= 0 && speed.as<int>() <= 100;
    if (validSeconds) settings.seconds = seconds.as<int>();
    if (validSpeed) settings.speed = speed.as<int>();
    settings.enabled = enabled.is<bool>() && enabled.as<bool>() && validSeconds && validSpeed;
}

#endif
