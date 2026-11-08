#include <future>

void waitForMqttResult() {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!uxQueueMessagesWaiting(mqttResults) && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    assert(uxQueueMessagesWaiting(mqttResults) == 1);
}

struct BlockedMqttOperation {
    std::promise<void> entered, released;
    std::future<void> started = entered.get_future();
    std::shared_future<void> resume = released.get_future().share();
    void block() { entered.set_value(); resume.wait(); }
    void wait() { assert(started.wait_for(std::chrono::seconds(2)) == std::future_status::ready); }
    void release() { released.set_value(); }
};

void connectTestMqtt() {
    resetMqttTransport();
    strcpy(printerConfig.printerIP, "192.0.2.83");
    strcpy(printerConfig.accessCode, "TEST1234");
    strcpy(printerConfig.serialNumber, "TESTSERIAL12345");
    mqttClient.isConnected = false;
    mqttClient.connectResult = true;
    setupMqtt();
    mqttloop();
    waitForMqttResult();
    mqttloop();
    waitForMqttResult(); // The next poll is finished; worker awaits a new request.
    assert(printerVariables.online);
}

void mqttTests() {
    check("startup and HTTP handlers stay responsive throughout a stalled MQTT connection", [] {
        putConfig(legacyConfig); reboot();
        printerConfig.staticFan = true;
        printerConfig.staticFanSpeed = 37;
        mqttClient.isConnected = false;
        BlockedMqttOperation blocked;
        mqttClient.beforeConnect = [&] { blocked.block(); };
        setupMqtt();
        assert(mqttClient.connectCalls == 0); // setup only starts an idle worker.
        webServer.onHandleClient = handleSetup;
        webserverloop();
        assert(webServer.responses.back() == 200);
        mqttloop();
        blocked.wait();
        for (int request = 0; request < 20; ++request) {
            mockMillis += 1000;
            mqttloop();
            webServer.onHandleClient = request % 2 ? handleGetOptions : handleSensorData;
            webserverloop();
            assert(webServer.responses.back() == 200);
            assert(globalVariables.fanSpeed == 37);
        }
        assert(mqttClient.connectCalls == 1);
        blocked.release();
        waitForMqttResult();
        mqttClient.beforeConnect = {};
        mqttloop();
        assert(!printerVariables.online && !mqttWorkInFlight);
        mockMillis += mqttRetryMs - 1;
        mqttloop();
        assert(mqttClient.connectCalls == 1 && !mqttWorkInFlight);
        ++mockMillis;
        mqttloop();
        waitForMqttResult();
        assert(mqttClient.connectCalls == 2); // Backoff starts after completion.
    });
    check("MQTT recovers when the printer returns and applies large streamed reports on the main loop", [] {
        putConfig(legacyConfig); reboot();
        mqttClient.isConnected = false;
        setupMqtt(); mqttloop(); waitForMqttResult(); mqttloop();
        assert(!printerVariables.online);
        mqttClient.connectResult = true;
        mockMillis += mqttRetryMs;
        mqttloop(); waitForMqttResult();
        assert(!printerVariables.online); // Worker never changes shared state.
        assert(mqttClient.host == "192.0.2.83" && mqttClient.lastCode == "TEST1234");
        assert(mqttClient.lastSubscription == "device/TESTSERIAL12345/report");
        mqttClient.reportPayload = "{\"unused\":\"" + std::string(4096, 'x') +
            "\",\"print\":{\"gcode_state\":\"RUNNING\",\"nozzle_temper\":210,\"big_fan2_speed\":12}}";
        mqttloop(); waitForMqttResult();
        assert(printerVariables.online && printerVariables.nozzletemp == 0);
        assert(!printerVariables.printWasRunning);
        mqttloop(); waitForMqttResult();
        assert(printerVariables.nozzletemp == 210 && printerVariables.printWasRunning);
        assert(printerVariables.fans[2].percent == 80);
    });
    check("a stalled MQTT read does not block HTTP or Wi-Fi-loss fallback", [] {
        connectTestMqtt();
        printerConfig.staticFan = true; printerConfig.staticFanSpeed = 23;
        printerConfig.postPrint.enabled = true;
        printerVariables.printWasRunning = true;
        recordPrintState("FINISH"); fanloop();
        assert(globalVariables.fanSpeed == 100);
        BlockedMqttOperation blocked;
        mqttClient.beforeLoop = [&] { blocked.block(); };
        mqttloop(); blocked.wait();
        WiFi.connectionStatus = 0;
        mqttloop();
        assert(!printerVariables.online && globalVariables.fanSpeed == 23);
        webServer.onHandleClient = handleGetOptions;
        webserverloop(); assert(webServer.responses.back() == 200);
        WiFi.connectionStatus = WL_CONNECTED;
        mqttloop(); // Must still discard the pre-disconnect result.
        blocked.release(); waitForMqttResult();
        mqttClient.beforeLoop = {};
        mqttloop(); waitForMqttResult();
        assert(mqttClient.disconnectCalls == 1 && !printerVariables.online);
        mqttloop(); waitForMqttResult(); // Reconnect only after cleanup.
        mqttClient.reportPayload = R"({"print":{"gcode_state":"FINISH"}})";
        mqttloop(); waitForMqttResult(); mqttloop(); waitForMqttResult();
        assert(printerVariables.online && !printerVariables.postPrint[0].active);
    });
    check("saving printer credentials during connect discards the old connection and uses the new settings", [] {
        putConfig(legacyConfig); reboot();
        mqttClient.isConnected = false; mqttClient.connectResult = true;
        BlockedMqttOperation blocked;
        mqttClient.beforeConnect = [&] { blocked.block(); };
        setupMqtt(); mqttloop(); blocked.wait();
        assert(options({{"ip","192.0.2.84"},{"code","NEW12345"},{"serial","NEWPRINTER12345"}}) == 200);
        mqttloop();
        blocked.release(); waitForMqttResult();
        mqttClient.beforeConnect = {};
        assert(mqttClient.lastCode == "TEST1234");
        mqttloop(); waitForMqttResult(); // Close the stale connection.
        assert(!printerVariables.online && mqttClient.disconnectCalls == 1);
        mqttloop(); waitForMqttResult();
        assert(mqttClient.host == "192.0.2.84" && mqttClient.lastCode == "NEW12345");
        assert(mqttClient.lastSubscription == "device/NEWPRINTER12345/report");
        mqttloop(); waitForMqttResult();
        assert(printerVariables.online);
    });
    check("retry pacing survives millis rollover and skips unavailable Wi-Fi", [] {
        putConfig(legacyConfig); reboot();
        mqttClient.isConnected = false;
        mockMillis = UINT32_MAX - 1500;
        setupMqtt(); mqttloop(); waitForMqttResult(); mqttloop();
        mockMillis += mqttRetryMs - 1; mqttloop();
        assert(!mqttWorkInFlight && mqttClient.connectCalls == 1);
        WiFi.connectionStatus = 0;
        ++mockMillis; mqttloop();
        assert(!mqttWorkInFlight && mqttClient.connectCalls == 1);
        WiFi.connectionStatus = WL_CONNECTED;
        mqttloop(); waitForMqttResult();
        assert(mqttClient.connectCalls == 2);
    });
    check("missing printer settings and worker allocation failure keep the web interface usable", [] {
        setupMqtt();
        for (int i = 0; i < 5; ++i) { mockMillis += 10000; mqttloop(); }
        assert(!mqttWorkInFlight && mqttClient.connectCalls == 0);
        resetMqttTransport();
        putConfig(legacyConfig); reboot();
        mockFailQueueCreate = true;
        setupMqtt(); mqttloop();
        webServer.onHandleClient = handleGetOptions;
        webserverloop(); assert(webServer.responses.back() == 200);
        assert(!mqttWorkInFlight);
        mockFailQueueCreate = false; mockFailTaskCreate = true;
        mockMillis += mqttRetryMs; mqttloop();
        webserverloop(); assert(webServer.responses.back() == 200);
        assert(!mqttWorkInFlight);
        mockFailTaskCreate = false;
        mqttClient.isConnected = false;
        mockMillis += mqttRetryMs; mqttloop(); waitForMqttResult();
        assert(mqttClient.connectCalls == 1);
    });
    check("failed report subscription remains offline and is retried", [] {
        putConfig(legacyConfig); reboot();
        mqttClient.isConnected = false; mqttClient.connectResult = true;
        mqttClient.subscribeResult = false;
        setupMqtt(); mqttloop(); waitForMqttResult(); mqttloop();
        assert(!printerVariables.online && !mqttWorkInFlight);
        assert(mqttClient.disconnectCalls == 1);
        mockMillis += mqttRetryMs;
        mqttClient.subscribeResult = true;
        mqttloop(); waitForMqttResult(); mqttloop(); waitForMqttResult();
        assert(printerVariables.online);
    });
}
