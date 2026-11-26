#pragma once

void historyTests() {
    check("all history ranges wrap, average, and refresh the last incomplete bucket", [] {
        // Use the actual response handler after a full 24 hours.
        for (uint32_t t = 0; t < 90012; ++t) {
            telemetryHistory.sample(t, t != 90005, 200, (t % 2) * 100, 100);
            telemetryHistory.sample(t, true, 0, 0, 0);
        }
        const char *names[] = {"5m","1h","6h","24h"};
        for (int i = 0; i < 4; ++i) {
            const auto &range = historyRanges[i];
            webServer.params = {{"range",names[i]}};
            handleHistory(); auto json = response();
            const auto points = json["points"].as<JsonArray>();
            assert(points.size() == range.capacity);
            assert(points[0][0] == (90011 / range.seconds - range.capacity + 1) * range.seconds);
            assert(json["full"] == true);
            webServer.params["boot"] = diagnosticBootId;
            webServer.params["since"] = std::to_string(90011 / range.seconds);
            handleHistory(); auto tail = response();
            assert(tail["full"] == false && tail["points"].size() == 1);
            assert(tail["points"][0][3] == 100);
            if (i) {
                assert(tail["points"][0][1].isNull());
                assert(tail["points"][0][2] == 50);
            }
            webServer.params["boot"] = "old-boot"; handleHistory();
            assert(response()["full"] == true);
        }
        telemetryHistory.sample(90016,true,202,35,80);
        webServer.params = {{"range","5m"},{"since","90011"},{"boot",diagnosticBootId}};
        handleHistory(); auto gap = response();
        assert(gap["points"].size()==6 && gap["points"][1][2].isNull() && gap["points"][5][2]==35);
        webServer.params = {{"range","invalid"}}; handleHistory();
        assert(webServer.responses.back()==400);
    });
    check("scoped saves isolate settings from fan modes and credentials", [] {
        putConfig(legacyConfig); reboot();
        assert(options({{"section","fans"},{"staticfan","on"},{"staticfanspeed","40"},{"ip","ignored"}})==200);
        assertCredentials(); assert(printerConfig.debuging && printerConfig.staticFan);
        assert(options({{"section","settings"},{"debuging","off"},{"staticfan","off"}})==200);
        reboot(); assertCredentials();
        assert(!printerConfig.debuging && printerConfig.staticFan && printerConfig.staticFanSpeed==40);
        assert(options({{"section","invalid"}})==400);
    });
}
