void printState(const char *state) {
    JsonDocument json;
    json["print"]["gcode_state"] = state;
    std::string payload;
    serializeJson(json, payload);
    report(payload.c_str());
}

void finishPrint(const char *end = "FINISH") {
    printerVariables.online = true;
    printState("RUNNING");
    printState(end);
}

void postPrintTests() {
    check("Post Print defaults off for old configurations and does not start from an end report alone", [] {
        putConfig(legacyConfig); reboot();
        assert(!printerConfig.postPrint.enabled && !printerConfig.postPrint2.enabled);
        handleGetOptions(); auto json = response();
        assert(json["postPrint"] == false && json["postPrint2"] == false);
        assert(json["postPrintSeconds"] == 600 && json["postPrintSpeed"] == 100);
        assert(options({{"postPrint","on"}}) == 200);
        printerVariables.online = true;
        for (const char *state : {"FINISH", "FAILED", "IDLE", "PREPARE", "PAUSE", "FINISH"}) printState(state);
        assertPWM(100,40,40);
        assert(postPrintSecondsRemaining(0,millis()) == 0);
    });
    check("each print-end state starts one timer; partial, paused, and repeated reports do not restart it", [] {
        assert(options({{"postPrint","on"},{"postPrintSeconds","10"},{"postPrintSpeed","87"},
                        {"staticfan","on"},{"staticfanspeed","23"}}) == 200);
        const auto disk = savedConfig();
        for (const char *end : {"FINISH", "FAILED", "IDLE"}) {
            printerVariables.online = true;
            printState("RUNNING");
            printState("PREPARE"); printState("PAUSE");
            report(R"({"print":{"nozzle_temper":210}})");
            assertPWM(200,23,23);
            printState(end);
            assertPWM(200,87,87);
            handleSensorData(); auto json = response();
            assert(json["postPrintRemaining"] == 10 && json["postPrintRemaining2"] == 10);
            mockMillis += 1001;
            printState(end); printState("IDLE");
            assert(postPrintSecondsRemaining(0,millis()) == 9);
            mockMillis += 8999;
            assertPWM(200,23,23);
            printState(end);
            assert(postPrintSecondsRemaining(0,millis()) == 0);
        }
        assert(savedConfig() == disk); // Runtime changes never write flash.
    });
    check("Post Print works independently alongside static and follow modes, then restores live base speeds", [] {
        assert(options({{"fansSynchronized","off"},{"staticfan","on"},{"staticfanspeed","31"},
            {"postPrint","on"},{"postPrintSeconds","2"},{"postPrintSpeed","100"},
            {"followPrinterFan2","on"},{"printerFanSource2","chamber"},
            {"postPrint2","on"},{"postPrintSeconds2","4"},{"postPrintSpeed2","55"}}) == 200);
        report(R"({"print":{"big_fan2_speed":9}})");
        assertPWM(200,31,60);
        finishPrint(); assertPWM(200,100,55);
        mockMillis = 1999;
        assert(postPrintSecondsRemaining(0,millis()) == 1);
        mockMillis = 2000; assertPWM(200,31,55);
        report(R"({"print":{"big_fan2_speed":12}})");
        mockMillis = 4000; assertPWM(200,31,80);
        assert(printerConfig.staticFan && printerConfig.followPrinterFan2);
    });
    check("zero-percent Post Print overrides a curve and timer expiration survives millis rollover", [] {
        assert(options({{"postPrint","on"},{"postPrintSeconds","2"},{"postPrintSpeed","0"}}) == 200);
        assert(curve(R"({"points":[{"temp":0,"speed":17},{"temp":400,"speed":97}]})") == 200);
        mockMillis = UINT32_MAX - 500;
        finishPrint(); assertPWM(200,0,0);
        mockMillis += 1000;
        assert(postPrintSecondsRemaining(0,millis()) == 1);
        assertPWM(300,0,0);
        mockMillis += 1000; assertPWM(300,77,77);
    });
    check("running, prepare, and pause do not change an active countdown", [] {
        assert(options({{"postPrint","on"},{"postPrintSeconds","10"}}) == 200);
        finishPrint();
        mockMillis = 2000;
        for (const char *state : {"PREPARE", "RUNNING", "PAUSE"}) {
            printState(state);
            assert(postPrintSecondsRemaining(0,millis()) == 8);
            assertPWM(0,100,100);
        }
    });
    check("Wi-Fi or MQTT loss clears both timers and restores outputs before reconnecting", [] {
        assert(options({{"fansSynchronized","off"},{"staticfan","on"},{"staticfanspeed","28"},
            {"followPrinterFan2","on"},{"postPrint","on"},{"postPrint2","on"}}) == 200);
        report(R"({"print":{"big_fan2_speed":9}})");
        for (int loss = 0; loss < 3; ++loss) {
            WiFi.connectionStatus = WL_CONNECTED;
            mqttClient.isConnected = true;
            mqttClient.dropDuringLoop = false;
            mockMillis = 5000;
            finishPrint(); assertPWM(0,100,100);
            if (loss == 0) WiFi.connectionStatus = 0;
            if (loss == 1) {
                mqttClient.isConnected = false;
                mqttClient.beforeConnect = [] {
                    assert(globalVariables.fanSpeed == 28 && globalVariables.fanSpeed2 == 60);
                };
            }
            if (loss == 2) mqttClient.dropDuringLoop = true;
            mqttloop();
            assert(!printerVariables.online && !printerVariables.printWasRunning);
            assert(globalVariables.fanSpeed == 28 && globalVariables.fanSpeed2 == 60);
            assert(postPrintSecondsRemaining(0,millis()) == 0 && postPrintSecondsRemaining(1,millis()) == 0);
            printerVariables.online = true;
            printState("FINISH"); printState("IDLE"); // Reconnection cannot revive the old timer.
            assertPWM(0,28,60);
        }
    });
    check("connection loss during printing cannot manufacture an end event after reconnection", [] {
        assert(options({{"postPrint","on"}}) == 200);
        printerVariables.online = true; printState("RUNNING");
        printerCommunicationLost();
        printerVariables.online = true; printState("FAILED");
        assertPWM(0,0,0);
        finishPrint(); assertPWM(0,100,100);
    });
    check("reboot keeps Post Print configuration but never restores a countdown or previous running state", [] {
        assert(options({{"postPrint","on"},{"postPrintSeconds","10"},
            {"staticfan","on"},{"staticfanspeed","19"}}) == 200);
        finishPrint(); assertPWM(200,100,100);
        reboot(); assertPWM(200,19,19);
        assert(printerConfig.postPrint.enabled);
        printerVariables.online = true; printState("FINISH");
        assertPWM(200,19,19);
        finishPrint(); assertPWM(200,100,100);
    });
    check("Post Print settings clone on first split, persist independently, and remain dormant in sync mode", [] {
        assert(options({{"postPrint","on"},{"postPrintSeconds","37"},{"postPrintSpeed","43"}}) == 200);
        assert(options({{"fansSynchronized","off"}}) == 200);
        assert(samePostPrintSettings(printerConfig.postPrint,printerConfig.postPrint2));
        assert(options({{"postPrint2","on"},{"postPrintSeconds2","85"},{"postPrintSpeed2","67"}}) == 200);
        reboot();
        assert(printerConfig.postPrint2.seconds == 85 && printerConfig.postPrint2.speed == 67);
        assert(options({{"fansSynchronized","on"},{"postPrint2","off"},{"postPrintSeconds2","1"}}) == 200);
        finishPrint(); assertPWM(0,43,43);
        handleSensorData(); assert(response()["postPrintRemaining2"] == 37);
        assert(options({{"fansSynchronized","off"}}) == 200);
        assertPWM(0,0,0); // Changing synchronization cancels any running override.
        assert(printerConfig.postPrint2.enabled && printerConfig.postPrint2.seconds == 85);
        finishPrint(); assertPWM(0,43,67);
    });
    check("invalid Post Print requests and failed writes preserve both configuration and running timers", [] {
        assert(options({{"fansSynchronized","off"},{"postPrint","on"},{"postPrintSeconds","10"},
            {"postPrint2","on"},{"postPrintSeconds2","20"}}) == 200);
        finishPrint(); mockMillis = 2000;
        const auto disk = savedConfig();
        for (const char *name : {"postPrintSeconds", "postPrintSeconds2"}) {
            for (const char *bad : {"", "0", "-1", "1.5", "86401", "999999999999", "10x"}) {
                assert(options({{name,bad}}) == 400);
                assert(savedConfig() == disk);
            }
        }
        for (const char *name : {"postPrintSpeed", "postPrintSpeed2"}) {
            for (const char *bad : {"", "-1", "100.5", "101", "999999999999", "20x"}) assert(options({{name,bad}}) == 400);
        }
        assert(options({{"postPrint","invalid"}}) == 400);
        assert(options({{"postPrint2","invalid"}}) == 400);
        failRename = true;
        assert(options({{"postPrint","off"},{"postPrint2","off"}}) == 500);
        assert(savedConfig() == disk && printerConfig.postPrint.enabled && printerConfig.postPrint2.enabled);
        assert(postPrintSecondsRemaining(0,millis()) == 8 && postPrintSecondsRemaining(1,millis()) == 18);
        failRename = false;
        assert(options({{"postPrint2","off"}}) == 200);
        assert(postPrintSecondsRemaining(0,millis()) == 8 && postPrintSecondsRemaining(1,millis()) == 0);
    });
    check("invalid saved Post Print values disable only that feature without losing printer configuration", [] {
        JsonDocument json; deserializeJson(json,legacyConfig);
        json["postPrint"] = true; json["postPrintSeconds"] = -1; json["postPrintSpeed"] = 100;
        std::string payload; serializeJson(json,payload); putConfig(payload); reboot();
        assert(!printerConfig.postPrint.enabled && printerConfig.postPrint.seconds == 600);
        assertCredentials(); assert(savedConfig() == payload);
    });
    check("malformed states cannot start Post Print and missing states preserve the observed running job", [] {
        assert(options({{"postPrint","on"}}) == 200);
        printerVariables.online = true;
        for (const char *bad : {"null", "true", "42", "[]", "{}", "\"UNKNOWN\""}) {
            std::string payload = std::string("{\"print\":{\"gcode_state\":") + bad + "}}";
            report(payload.c_str());
        }
        printState("FINISH"); assertPWM(0,0,0);
        printState("RUNNING");
        assert(!report("{\"print\":{\"gcode_state\":\"FINISH\""));
        report(R"({"print":{"big_fan2_speed":6}})");
        assertPWM(0,0,0);
        printState("FINISH"); assertPWM(0,100,100);
    });
}
