#ifndef _BLFLOW_DIAGNOSTICS
#define _BLFLOW_DIAGNOSTICS

#include <atomic>
#include <esp_system.h>
#include <esp_timer.h>
#include <ArduinoJson.h>
#include "mqttmanager.h"

constexpr uint32_t healthLogIntervalMs = 5000;
char diagnosticBootId[9] = {};
uint32_t diagnosticLastLog = 0, diagnosticLastLoop = 0, diagnosticMaxLoopGap = 0;
bool diagnosticLoopSeen = false;
uint32_t diagnosticLastFanLog = 0;
bool diagnosticFansSeen = false;
int diagnosticLastFanSpeeds[2] = {-1, -1};
const char *diagnosticLastFanModes[2] = {"", ""};
// Wi-Fi callbacks run on the Arduino event task, independently of loop().
std::atomic<uint32_t> diagnosticWifiDisconnects{0}, diagnosticWifiReason{0};

const char *resetReasonName() {
    switch (esp_reset_reason()) {
    case ESP_RST_POWERON: return "power_on";
    case ESP_RST_EXT: return "external";
    case ESP_RST_SW: return "software";
    case ESP_RST_PANIC: return "panic";
    case ESP_RST_INT_WDT: return "interrupt_watchdog";
    case ESP_RST_TASK_WDT: return "task_watchdog";
    case ESP_RST_WDT: return "watchdog";
    case ESP_RST_DEEPSLEEP: return "deep_sleep";
    case ESP_RST_BROWNOUT: return "brownout";
    case ESP_RST_SDIO: return "sdio";
    default: return "unknown";
    }
}

void diagnosticWifiEvent(arduino_event_id_t event, arduino_event_info_t info) {
    const auto seconds = static_cast<unsigned long long>(esp_timer_get_time() / 1000000);
    if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
        const auto &lost = info.wifi_sta_disconnected;
        diagnosticWifiReason.store(lost.reason);
        ++diagnosticWifiDisconnects;
        if (!debugLoggingEnabled()) return;
        Serial.printf("[wifi uptime=%llus] Disconnected reason=%u rssi=%d BSSID=%02X:%02X:%02X:%02X:%02X:%02X\n",
            seconds, lost.reason, lost.rssi, lost.bssid[0], lost.bssid[1], lost.bssid[2],
            lost.bssid[3], lost.bssid[4], lost.bssid[5]);
    } else if (!debugLoggingEnabled()) {
        return;
    } else if (event == ARDUINO_EVENT_WIFI_STA_CONNECTED) {
        const auto &ap = info.wifi_sta_connected;
        Serial.printf("[wifi uptime=%llus] Associated channel=%u BSSID=%02X:%02X:%02X:%02X:%02X:%02X\n",
            seconds, ap.channel, ap.bssid[0], ap.bssid[1], ap.bssid[2], ap.bssid[3], ap.bssid[4], ap.bssid[5]);
    } else if (event == ARDUINO_EVENT_WIFI_STA_GOT_IP) {
        Serial.printf("[wifi uptime=%llus] Got IP\n", seconds);
    } else if (event == ARDUINO_EVENT_WIFI_STA_LOST_IP) {
        Serial.printf("[wifi uptime=%llus] Lost IP\n", seconds);
    } else if (event == ARDUINO_EVENT_WIFI_STA_STOP) {
        Serial.printf("[wifi uptime=%llus] Station stopped\n", seconds);
    }
}

void setupDiagnostics() {
    applyDebugLogging();
    snprintf(diagnosticBootId, sizeof(diagnosticBootId), "%08lx", static_cast<unsigned long>(esp_random()));
    Serial.printf("[boot] id=%s reset=%s (%d)\n", diagnosticBootId, resetReasonName(), esp_reset_reason());
    WiFi.onEvent(diagnosticWifiEvent);
}

const char *mqttOperationName() {
    if (!mqttWorkInFlight) return "idle";
    switch (mqttPendingOperation) {
    case MqttOperation::Connect: return "connect";
    case MqttOperation::Disconnect: return "disconnect";
    default: return "poll";
    }
}

// Describe applied outputs without running fanloop() or expiring timers from a
// diagnostic read. Fan 2 uses Fan 1's mode and inputs when synchronized.
int diagnosticSettingsFan(int fan) {
    return printerConfig.fansSynchronized ? 0 : fan;
}

const char *diagnosticFanMode(int fan) {
    fan = diagnosticSettingsFan(fan);
    if (printerVariables.postPrint[fan].active) return "post_print";
    if (fan == 0 ? printerConfig.staticFan : printerConfig.staticFan2) return "static";
    if (fan == 0 ? printerConfig.followPrinterFan : printerConfig.followPrinterFan2) return "follow";
    return "temperature_curve";
}

PrinterFanSource diagnosticFanSource(int fan) {
    return diagnosticSettingsFan(fan) == 0 ? printerConfig.printerFanSource : printerConfig.printerFanSource2;
}

uint32_t diagnosticPostPrintRemaining(int fan, uint32_t now) {
    const auto &run = printerVariables.postPrint[diagnosticSettingsFan(fan)];
    const uint32_t elapsed = now - run.started;
    return run.active && elapsed < run.durationMs ? (run.durationMs - elapsed + 999) / 1000 : 0;
}

void formatDiagnosticFan(char *buffer, size_t size, int fan, uint32_t now) {
    const int speed = fan == 0 ? globalVariables.fanSpeed : globalVariables.fanSpeed2;
    const char *mode = diagnosticFanMode(fan);
    if (strcmp(mode, "follow") == 0) {
        const auto source = diagnosticFanSource(fan);
        const auto &input = printerVariables.fans[static_cast<int>(source)];
        if (input.known)
            snprintf(buffer, size, "%d%%(follow:%s=%d%%)", speed, printerFanSourceName(source), input.percent);
        else snprintf(buffer, size, "%d%%(follow:%s=unknown)", speed, printerFanSourceName(source));
    } else if (strcmp(mode, "post_print") == 0) {
        snprintf(buffer, size, "%d%%(post_print:%lus)", speed,
            static_cast<unsigned long>(diagnosticPostPrintRemaining(fan, now)));
    } else snprintf(buffer, size, "%d%%(%s)", speed, mode);
}

void logDiagnosticFanChanges(uint32_t now) {
    if (!changeLoggingEnabled()) {
        diagnosticFansSeen = false;
        return;
    }
    const int speeds[2] = {globalVariables.fanSpeed, globalVariables.fanSpeed2};
    const char *modes[2] = {diagnosticFanMode(0), diagnosticFanMode(1)};
    const bool changed = speeds[0] != diagnosticLastFanSpeeds[0] || speeds[1] != diagnosticLastFanSpeeds[1] ||
        strcmp(modes[0], diagnosticLastFanModes[0]) != 0 || strcmp(modes[1], diagnosticLastFanModes[1]) != 0;
    if (diagnosticFansSeen && (!changed || static_cast<uint32_t>(now - diagnosticLastFanLog) < 1000)) return;
    diagnosticFansSeen = true;
    diagnosticLastFanLog = now;
    char fan1[80], fan2[80];
    formatDiagnosticFan(fan1, sizeof(fan1), 0, now);
    formatDiagnosticFan(fan2, sizeof(fan2), 1, now);
    Serial.printf("[fans uptime=%llus] fan1=%s fan2=%s nozzle=%.1f\n",
        static_cast<unsigned long long>(esp_timer_get_time() / 1000000), fan1, fan2, printerVariables.nozzletemp);
    for (int fan = 0; fan < 2; ++fan) {
        diagnosticLastFanSpeeds[fan] = speeds[fan];
        diagnosticLastFanModes[fan] = modes[fan];
    }
}

void writeDiagnostics(JsonDocument &doc, uint32_t now) {
    doc["firmwareversion"] = globalVariables.FWVersion;
    doc["bootId"] = diagnosticBootId;
    doc["resetReason"] = resetReasonName();
    doc["uptimeSeconds"] = static_cast<uint64_t>(esp_timer_get_time() / 1000000);
    doc["maxLoopGapMs"] = diagnosticMaxLoopGap;
    doc["heap"]["free"] = ESP.getFreeHeap();
    doc["heap"]["minimumFree"] = ESP.getMinFreeHeap();
    doc["heap"]["largestBlock"] = ESP.getMaxAllocHeap();
    const bool connected = WiFi.status() == WL_CONNECTED;
    doc["wifi"]["status"] = WiFi.status();
    doc["wifi"]["mac"] = WiFi.macAddress();
    doc["wifi"]["ip"] = connected ? WiFi.localIP().toString() : String("");
    doc["wifi"]["bssid"] = connected ? WiFi.BSSIDstr() : String("");
    if (connected) {
        doc["wifi"]["rssi"] = WiFi.RSSI();
        doc["wifi"]["channel"] = WiFi.channel();
    } else {
        doc["wifi"]["rssi"] = nullptr;
        doc["wifi"]["channel"] = nullptr;
    }
    doc["wifi"]["disconnects"] = diagnosticWifiDisconnects.load();
    if (diagnosticWifiDisconnects.load()) doc["wifi"]["lastDisconnectReason"] = diagnosticWifiReason.load();
    else doc["wifi"]["lastDisconnectReason"] = nullptr;
    doc["mqtt"]["state"] = mqttConnectionStatus(now).state;
    doc["mqtt"]["lastResultCode"] = mqttLastState;
    doc["mqtt"]["operation"] = mqttOperationName();
    doc["mqtt"]["pendingMs"] = mqttWorkInFlight ? static_cast<uint32_t>(now - mqttWorkStarted) : 0;
    doc["mqtt"]["connectAttempts"] = mqttConnectAttempts;
    doc["mqtt"]["connections"] = mqttConnections;
    doc["mqtt"]["reports"] = mqttReports;
    if (mqttReports) doc["mqtt"]["lastReportAgeMs"] = static_cast<uint32_t>(now - mqttLastReportAt);
    else doc["mqtt"]["lastReportAgeMs"] = nullptr;
    doc["fansSynchronized"] = printerConfig.fansSynchronized;
    doc["nozzleTemperature"] = printerVariables.nozzletemp;
    for (int fan = 0; fan < 2; ++fan) {
        JsonObject output = doc["fans"][fan == 0 ? "fan1" : "fan2"].to<JsonObject>();
        output["speedPercent"] = fan == 0 ? globalVariables.fanSpeed : globalVariables.fanSpeed2;
        output["mode"] = diagnosticFanMode(fan);
        output["postPrintRemainingSeconds"] = diagnosticPostPrintRemaining(fan, now);
        if (strcmp(diagnosticFanMode(fan), "follow") == 0) {
            const auto source = diagnosticFanSource(fan);
            const auto &input = printerVariables.fans[static_cast<int>(source)];
            output["printerFanSource"] = printerFanSourceName(source);
            if (input.known) output["printerFanPercent"] = input.percent;
            else output["printerFanPercent"] = nullptr;
        }
    }
}

void diagnosticsLoop(uint32_t now = millis()) {
    if (diagnosticLoopSeen) {
        const uint32_t gap = now - diagnosticLastLoop;
        if (gap > diagnosticMaxLoopGap) diagnosticMaxLoopGap = gap;
    }
    diagnosticLoopSeen = true;
    diagnosticLastLoop = now;
    logDiagnosticFanChanges(now);
    if (!debugLoggingEnabled()) return;
    if (static_cast<uint32_t>(now - diagnosticLastLog) < healthLogIntervalMs) return;
    diagnosticLastLog = now;
    const bool connected = WiFi.status() == WL_CONNECTED;
    char fan1[80], fan2[80];
    formatDiagnosticFan(fan1, sizeof(fan1), 0, now);
    formatDiagnosticFan(fan2, sizeof(fan2), 1, now);
    Serial.printf("[health boot=%s uptime=%llus] wifi=%d ip=%s bssid=%s rssi=%ld channel=%ld drops=%lu "
                  "mqtt=%s work=%s pendingMs=%lu reports=%lu reportAgeMs=%lld heap=%lu minHeap=%lu loopMaxMs=%lu "
                  "fan1=%s fan2=%s nozzle=%.1f\n",
        diagnosticBootId, static_cast<unsigned long long>(esp_timer_get_time() / 1000000), WiFi.status(),
        connected ? WiFi.localIP().toString().c_str() : "-", connected ? WiFi.BSSIDstr().c_str() : "-",
        connected ? static_cast<long>(WiFi.RSSI()) : 0L, connected ? static_cast<long>(WiFi.channel()) : 0L,
        static_cast<unsigned long>(diagnosticWifiDisconnects.load()), mqttConnectionStatus(now).state,
        mqttOperationName(), static_cast<unsigned long>(mqttWorkInFlight ? now - mqttWorkStarted : 0),
        static_cast<unsigned long>(mqttReports), mqttReports ? static_cast<long long>(static_cast<uint32_t>(now - mqttLastReportAt)) : -1LL,
        static_cast<unsigned long>(ESP.getFreeHeap()), static_cast<unsigned long>(ESP.getMinFreeHeap()),
        static_cast<unsigned long>(diagnosticMaxLoopGap), fan1, fan2, printerVariables.nozzletemp);
}

#endif
