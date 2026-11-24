#pragma once

void debugLoggingTests() {
    check("Debugging off silences all optional logs while diagnostics and MQTT control continue", [] {
        putConfig(legacyConfig); reboot(); setupDiagnostics();
        assert(options({{"debuging","off"},{"debugingchange","on"},{"mqttdebug","on"},
                        {"followPrinterFan","on"},{"printerFanSource","chamber"}}) == 200);
        connectTestMqtt();
        Serial.output.clear();
        mqttClient.reportPayload = R"({"print":{"big_fan2_speed":12,"nozzle_temper":210}})";
        mqttloop(); waitForMqttResult(); mqttloop(); waitForMqttResult();
        fanloop();
        assert(globalVariables.fanSpeed == 80 && printerVariables.nozzletemp == 210);
        arduino_event_info_t info{}; info.wifi_sta_disconnected.reason = 200;
        WiFi.eventHandler(ARDUINO_EVENT_WIFI_STA_DISCONNECTED, info);
        WiFi.eventHandler(ARDUINO_EVENT_WIFI_STA_GOT_IP, info);
        diagnosticsLoop(5000); diagnosticsLoop(10000); handleDiagnostics();
        assert(Serial.output.empty());
        assert(response()["wifi"]["disconnects"] == 1 && response()["mqtt"]["reports"] == 1);
        assert(response()["fans"]["fan1"]["speedPercent"] == 80);
        ParseMQTTState(-4);
        assert(printerVariables.errorcode == "no mqtt" && Serial.output.empty());
        ParseMQTTState(0);
        assert(printerVariables.errorcode == "" && Serial.output.empty());
    });
    check("OnChange logs require both switches and do not disable health reporting", [] {
        printerConfig.debugingchange = false; applyDebugLogging();
        diagnosticsLoop(5000);
        assert(Serial.output.find("[health") != std::string::npos);
        assert(Serial.output.find("[fans") == std::string::npos);
        printerConfig.debugingchange = true; applyDebugLogging();
        Serial.output.clear(); diagnosticsLoop(6000);
        assert(Serial.output.find("[fans") != std::string::npos);
        printerConfig.debuging = false; applyDebugLogging();
        Serial.output.clear(); diagnosticsLoop(11000);
        assert(Serial.output.empty());
        printerConfig.debuging = true; applyDebugLogging(); diagnosticsLoop(12000);
        assert(Serial.output.find("[fans") != std::string::npos && Serial.output.find("[health") != std::string::npos);
    });
    check("MQTT payload logging requires the master and filters unrelated fields", [] {
        const char *payload = R"({"print":{"nozzle_temper":200,"big_fan2_speed":6,"access_code":"SECRET"},"password":"SECRET"})";
        for (bool master : {false, true}) for (bool detail : {false, true}) {
            printerConfig.debuging = master; printerConfig.mqttdebug = detail; applyDebugLogging();
            Serial.output.clear(); assert(report(payload));
            assert(printerVariables.nozzletemp == 200);
            assert((Serial.output.find("(Filtered) MQTT payload:") != std::string::npos) == (master && detail));
            if (master && detail) assert(Serial.output.find("nozzle_temper") != std::string::npos);
            else assert(Serial.output.empty());
            assert(Serial.output.find("SECRET") == std::string::npos);
        }
    });
    check("saved debug controls apply immediately, survive reboot, and ignore failed saves", [] {
        putConfig(legacyConfig); reboot();
        assert(options({{"debuging","off"},{"debugingchange","on"},{"mqttdebug","on"}}) == 200);
        assert(!debugLoggingEnabled() && !changeLoggingEnabled() && !mqttLoggingEnabled());
        reboot();
        assert(!debugLoggingEnabled() && printerConfig.debugingchange && printerConfig.mqttdebug);
        const auto before = savedConfig(); failRename = true;
        assert(options({{"debuging","on"},{"debugingchange","on"},{"mqttdebug","on"}}) == 500);
        assert(!debugLoggingEnabled() && savedConfig() == before);
        failRename = false;
        assert(options({{"debuging","on"},{"debugingchange","on"},{"mqttdebug","on"}}) == 200);
        assert(debugLoggingEnabled() && changeLoggingEnabled() && mqttLoggingEnabled());
        reboot(); assert(debugLoggingEnabled() && changeLoggingEnabled() && mqttLoggingEnabled());
    });
    check("debugging off keeps real failures and the USB setup IP marker visible", [] {
        putConfig(legacyConfig); reboot(); printerConfig.debuging = false; applyDebugLogging();
        Serial.output.clear(); setupWifi(); WiFi.connectionStatus = WL_CONNECTED; wifiLoop(1000);
        assert(Serial.output.find("IP_ADDRESS:192.0.2.123") != std::string::npos);
        assert(Serial.output.find("Connecting to Wi-Fi") == std::string::npos);
        Serial.output.clear(); failOpen = true; assert(!saveFileSystem());
        assert(Serial.output.find("Failed to save config") != std::string::npos);
    });
}
