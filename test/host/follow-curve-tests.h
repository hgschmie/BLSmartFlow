const char *exhaustCurve = R"({"points":[{"input":100,"speed":100},{"input":0,"speed":0},{"input":1,"speed":35}]})";
const FanCurve exhaustPoints = {{0,0},{1,35},{100,100}};

void followCurveTests() {
    check("old configurations retain identity following for every percentage", [] {
        putConfig(legacyConfig); reboot();
        assertCredentials();
        assert(printerConfig.followGraph == defaultFollowFanCurve());
        assert(options({{"followPrinterFan","on"},{"printerFanSource","chamber"}}) == 200);
        for (int percent = 0; percent <= 100; ++percent) {
            printerVariables.fans[2].percent = percent;
            assertPWM(250, percent, percent);
        }
        handleGetFanConfig(); auto json = response();
        assert(json["followPoints"].size() == 3 && json["followPoints2"].size() == 3);
        assert(json["followPoints"][1]["input"] == 1 && json["followPoints"][1]["speed"] == 1);
    });
    check("minimum starting speed maps positive inputs and zero always stops the output", [] {
        assert(options({{"followPrinterFan","on"},{"printerFanSource","chamber"}}) == 200);
        const auto temperature = printerConfig.fanGraph;
        assert(curve(exhaustCurve, "1", "follow") == 200);
        assert(printerConfig.fanGraph == temperature && printerConfig.followGraph == exhaustPoints);
        for (const auto &sample : FanCurve{{0,0},{1,35},{10,41},{50,67},{70,80},{100,100}}) {
            printerVariables.fans[2].percent = sample.first;
            assertPWM(0, sample.second, sample.second);
            assertPWM(400, sample.second, sample.second);
        }
        assert(report(R"({"print":{"fan_gear":11796480}})")); // chamber PWM 180 -> 70%
        assertPWM(200,80,80);
        handleSensorData(); auto json = response();
        assert(json["printerFans"]["chamber"] == 70 && json["speed"] == 80);
        printerVariables.online = false; assertPWM(0,80,80);
        assert(report(R"({"print":{"big_fan2_speed":0}})")); assertPWM(400,0,0);
    });
    check("both curve kinds persist independently through split sync and mode changes", [] {
        putConfig(legacyConfig); reboot();
        assert(curve(exhaustCurve, "1", "follow") == 200);
        assert(options({{"followPrinterFan","on"},{"fansSynchronized","off"}}) == 200);
        assert(printerConfig.followGraph2 == exhaustPoints);
        assert(curve(secondCurve,"2") == 200);
        assert(curve(R"({"points":[{"input":0,"speed":0},{"input":1,"speed":50},{"input":50,"speed":75},{"input":100,"speed":80}]})", "2", "follow") == 200);
        const auto follow2 = printerConfig.followGraph2;
        const auto temp2 = printerConfig.fanGraph2;
        reboot(); assertCredentials();
        assert(printerConfig.followGraph == exhaustPoints && printerConfig.followGraph2 == follow2);
        assert(report(R"({"print":{"big_fan2_speed":3}})")); // 20%
        assertPWM(0,47,60);
        assert(options({{"fansSynchronized","on"}}) == 200); reboot();
        assert(report(R"({"print":{"big_fan2_speed":3}})")); assertPWM(0,47,47);
        assert(options({{"fansSynchronized","off"},{"followPrinterFan","off"}}) == 200);
        assertPWM(200,80,60);
        assert(options({{"followPrinterFan2","off"}}) == 200); assertPWM(200,80,50);
        reboot();
        assert(printerConfig.followGraph == exhaustPoints && printerConfig.followGraph2 == follow2);
        assert(printerConfig.fanGraph2 == temp2);
        handleGetFanConfig(); auto json = response();
        assert(json["followPoints2"][2]["input"] == 50 && json["followPoints2"][2]["speed"] == 75);
        assert(json["points2"][0]["temp"] == 0 && json["points2"][0]["speed"] == 30);
    });
    check("additional mapping points interpolate and overrides keep their precedence", [] {
        assert(curve(R"({"points":[{"input":0,"speed":0},{"input":1,"speed":35},{"input":50,"speed":80},{"input":100,"speed":100}]})", "1", "follow") == 200);
        assert(options({{"followPrinterFan","on"},{"postPrint","on"},{"postPrintSeconds","10"},{"postPrintSpeed","93"}}) == 200);
        printerVariables.fans[2].percent = 25; assertPWM(400,57,57);
        printerVariables.fans[2].percent = 50; assertPWM(400,80,80);
        printerVariables.fans[2].percent = 75; assertPWM(400,90,90);
        finishPrint(); assertPWM(400,93,93);
        assert(cancelRun("1") == 200); assertPWM(400,90,90);
        finishPrint(); assertPWM(400,93,93);
        mockMillis = 10000; assertPWM(400,90,90);
        assert(options({{"staticfan","on"},{"staticfanspeed","23"}}) == 200); assertPWM(400,23,23);
        assert(options({{"followPrinterFan","on"}}) == 200); assertPWM(400,90,90);
        finishPrint(); assertPWM(400,93,93);
        printerVariables.online = false; assertPWM(400,90,90);
        reboot(); assertPWM(400,0,0);
    });
    check("invalid mappings and mode values cannot overwrite either saved curve", [] {
        assert(curve(exhaustCurve,"1","follow") == 200);
        const auto disk = savedConfig();
        for (const char *points : {
            "[]", R"([{"input":0,"speed":0},{"input":100,"speed":100}])",
            R"([{"input":0,"speed":1},{"input":1,"speed":35},{"input":100,"speed":100}])",
            R"([{"input":0,"speed":0},{"input":1,"speed":35},{"input":101,"speed":100}])",
            R"([{"input":0,"speed":0},{"input":1,"speed":35},{"input":1,"speed":70},{"input":100,"speed":100}])",
            R"([{"input":0,"speed":0},{"input":1,"speed":35},{"input":1.5,"speed":70},{"input":100,"speed":100}])",
            R"([{"input":0,"speed":0},{"input":1,"speed":35.5},{"input":100,"speed":100}])",
            R"([{"input":0,"speed":0},{"input":1,"speed":101},{"input":100,"speed":100}])",
            R"([{"input":0,"speed":0},{"input":"1","speed":35},{"input":100,"speed":100}])"
        }) {
            const auto payload = std::string("{\"points\":") + points + "}";
            assert(curve(payload.c_str(),"1","follow") == 400);
            assert(savedConfig() == disk && printerConfig.followGraph == exhaustPoints);
        }
        assert(curve(exhaustCurve,"1","invalid") == 400);
        assert(curve(exhaustCurve,"1","temperature") == 400);
        assert(curve(newCurve,"1","follow") == 400);
        assert(savedConfig() == disk);
    });
    check("failed mapping writes keep live output and stored settings intact", [] {
        putConfig(legacyConfig); reboot();
        assert(options({{"followPrinterFan","on"}}) == 200);
        const auto disk = savedConfig();
        failRename = true;
        assert(curve(exhaustCurve,"1","follow") == 500);
        failRename = false;
        assert(savedConfig() == disk && printerConfig.followGraph == defaultFollowFanCurve());
        printerVariables.fans[2].percent = 70; assertPWM(200,70,70);
        reboot(); assertCredentials();
        assert(printerConfig.followGraph == defaultFollowFanCurve());
    });
    check("invalid stored mappings fall back independently without losing credentials or temperature curves", [] {
        putConfig(legacyConfig); reboot();
        assert(curve(exhaustCurve,"1","follow") == 200);
        assert(options({{"fansSynchronized","off"}}) == 200);
        const auto temp = printerConfig.fanGraph;
        JsonDocument json; assert(!deserializeJson(json,savedConfig()));
        json["followPoints"][0]["speed"] = 20;
        std::string disk; serializeJson(json,disk); putConfig(disk); reboot();
        assertCredentials();
        assert(printerConfig.followGraph == defaultFollowFanCurve());
        assert(printerConfig.followGraph2 == exhaustPoints);
        assert(printerConfig.fanGraph == temp && printerConfig.fanGraph2 == temp);
    });
}
