#pragma once

void diagnosticsTests() {
    check("fan diagnostics report applied speeds and independent input sources without driving outputs", [] {
        putConfig(legacyConfig); reboot();
        printerConfig.fansSynchronized = false;
        printerConfig.followPrinterFan = true;
        printerConfig.followGraph = {{0,0},{1,35},{100,100}};
        printerConfig.staticFan2 = true; printerConfig.staticFanSpeed2 = 37;
        report(R"({"print":{"fan_gear":1174405120,"nozzle_temper":40}})");
        // Select a known chamber input directly; packed decoding is tested separately.
        auto &chamber = printerVariables.fans[static_cast<int>(PrinterFanSource::Chamber)];
        chamber.known = true; chamber.percent = 70;
        fanloop(); const auto beforePWM = pwmValues;
        handleDiagnostics(); const auto state = response();
        assert(state["fans"]["fan1"]["speedPercent"] == 80);
        assert(state["fans"]["fan1"]["mode"] == "follow");
        assert(state["fans"]["fan1"]["printerFanSource"] == "chamber");
        assert(state["fans"]["fan1"]["printerFanPercent"] == 70);
        assert(state["fans"]["fan2"]["speedPercent"] == 37 && state["fans"]["fan2"]["mode"] == "static");
        assert(state["nozzleTemperature"] == 40 && state["fansSynchronized"] == false);
        diagnosticsLoop(15000);
        assert(Serial.output.find("fan1=80%(follow:chamber=70%) fan2=37%(static)") != std::string::npos);
        assert(pwmValues == beforePWM && savedConfig() == legacyConfig);
        chamber.known = false; handleDiagnostics();
        assert(response()["fans"]["fan1"]["printerFanPercent"].isNull());
    });
    check("fan diagnostics respect synchronization and Post Print without expiring timers", [] {
        printerConfig.staticFan = true; printerConfig.staticFanSpeed = 42;
        printerConfig.staticFan2 = true; printerConfig.staticFanSpeed2 = 13;
        printerConfig.postPrint.enabled = true;
        printerConfig.postPrint.seconds = 10; printerConfig.postPrint.speed = 100;
        printerVariables.online = true;
        startPostPrint(0, printerConfig.postPrint, 0); fanloop(1000);
        mockMillis = 1000; handleDiagnostics(); const auto state = response();
        for (const auto *fan : {"fan1", "fan2"}) {
            assert(state["fans"][fan]["mode"] == "post_print");
            assert(state["fans"][fan]["speedPercent"] == 100);
            assert(state["fans"][fan]["postPrintRemainingSeconds"] == 9);
        }
        mockMillis = 11000; handleDiagnostics();
        assert(response()["fans"]["fan1"]["postPrintRemainingSeconds"] == 0);
        assert(printerVariables.postPrint[0].active && globalVariables.fanSpeed == 100);
        fanloop(); handleDiagnostics();
        assert(response()["fans"]["fan2"]["mode"] == "static");
        assert(response()["fans"]["fan2"]["speedPercent"] == 42);
    });
    check("fan output changes are logged between health messages with bounded frequency", [] {
        diagnosticsLoop(0); Serial.output.clear();
        printerConfig.staticFan = true; printerConfig.staticFanSpeed = 100; fanloop();
        diagnosticsLoop(1000);
        assert(Serial.output.find("[fans") != std::string::npos);
        assert(Serial.output.find("fan1=100%(static) fan2=100%(static)") != std::string::npos);
        assert(Serial.output.find("[health") == std::string::npos);
        const auto logged = Serial.output;
        printerConfig.staticFanSpeed = 0; fanloop(); diagnosticsLoop(1500);
        assert(Serial.output == logged);
        diagnosticsLoop(2000); assert(Serial.output.find("fan1=0%(static)") != std::string::npos);
        const auto stopped = Serial.output;
        diagnosticsLoop(3000); assert(Serial.output == stopped);
    });
    check("diagnostics expose boot identity and uptime without credentials or config changes", [] {
        putConfig(legacyConfig); reboot(); setupDiagnostics();
        mockMillis = 45000;
        handleDiagnostics(); const auto state = response();
        assert(state["uptimeSeconds"] == 45 && state["resetReason"] == "power_on");
        assert(state["wifi"]["mac"] == "10:20:30:AB:CD:EF");
        assert(state["wifi"]["rssi"] == -50 && state["wifi"]["channel"] == 7);
        assert(state["mqtt"]["lastReportAgeMs"].isNull());
        assert(webServer.headers.at("Cache-Control") == "no-store");
        for (const auto *secret : {"test-password", "TEST1234", "TESTSERIAL12345"})
            assert(webServer.lastBody.find(secret) == String::npos && Serial.output.find(secret) == std::string::npos);
        assert(savedConfig() == legacyConfig && WiFi.beginCalls == 0 && ESP.restarts == 0);
        const auto firstBoot = std::string(diagnosticBootId);
        setupDiagnostics(); mockResetReason = ESP_RST_BROWNOUT; mockMillis = 500;
        handleDiagnostics(); const auto afterReset = response();
        assert(firstBoot != diagnosticBootId && afterReset["resetReason"] == "brownout");
        assert(afterReset["uptimeSeconds"] == 0);
    });
    check("brief Wi-Fi loss is logged even if loop never observes a disconnected status", [] {
        setupDiagnostics();
        arduino_event_info_t info{};
        info.wifi_sta_disconnected.reason = 200; // Beacon timeout in ESP-IDF.
        info.wifi_sta_disconnected.rssi = -82;
        WiFi.eventHandler(ARDUINO_EVENT_WIFI_STA_DISCONNECTED, info);
        WiFi.eventHandler(ARDUINO_EVENT_WIFI_STA_CONNECTED, info);
        assert(WiFi.status() == WL_CONNECTED);
        handleDiagnostics(); const auto state = response();
        assert(state["wifi"]["disconnects"] == 1 && state["wifi"]["lastDisconnectReason"] == 200);
        assert(Serial.output.find("Disconnected reason=200 rssi=-82") != std::string::npos);
        WiFi.connectionStatus = WL_DISCONNECTED;
        handleDiagnostics(); assert(response()["wifi"]["rssi"].isNull());
    });
    check("health messages continue during stalled MQTT work without touching its socket", [] {
        putConfig(legacyConfig); reboot(); setupDiagnostics();
        mqttClient.isConnected = false;
        BlockedMqttOperation blocked;
        mqttClient.beforeConnect = [&] { blocked.block(); };
        setupMqtt(); mqttloop(); blocked.wait();
        mockMillis = 15000;
        diagnosticsLoop(); handleDiagnostics();
        const auto state = response();
        assert(state["mqtt"]["operation"] == "connect" && state["mqtt"]["pendingMs"] == 15000);
        assert(state["mqtt"]["connectAttempts"] == 1 && state["mqtt"]["connections"] == 0);
        assert(Serial.output.find("work=connect pendingMs=15000") != std::string::npos);
        assert(WiFi.beginCalls == 0 && mqttClient.connectCalls == 1);
        blocked.release(); waitForMqttResult(); mqttloop();
    });
    check("MQTT diagnostics distinguish reconnects from continued report delivery", [] {
        connectTestMqtt();
        mqttClient.reportPayload = R"({"print":{"nozzle_temper":200}})";
        mockMillis = 1000;
        mqttloop(); waitForMqttResult(); mqttloop(); waitForMqttResult();
        mockMillis = 2500;
        handleDiagnostics(); const auto state = response();
        assert(state["mqtt"]["connectAttempts"] == 1 && state["mqtt"]["connections"] == 1);
        assert(state["mqtt"]["reports"] == 1 && state["mqtt"]["lastReportAgeMs"] == 1500);
        assert(state["mqtt"]["lastResultCode"] == 0);
    });
    check("health logging is rate limited and records loop gaps across millis rollover", [] {
        diagnosticLastLog = 0xfffffff0;
        diagnosticsLoop(0xfffffff0);
        const auto before = Serial.output;
        diagnosticsLoop(0xfffffff0u + 4999);
        assert(Serial.output == before);
        diagnosticsLoop(0xfffffff0u + 5000);
        assert(Serial.output.find("[health") != std::string::npos);
        assert(diagnosticMaxLoopGap == 4999);
        const auto after = Serial.output;
        diagnosticsLoop(0xfffffff0u + 5001);
        assert(Serial.output == after);
    });
}
