#pragma once

int saveWifi(const String &ssid, const String &password, const char *open = "false") {
    webServer.params = {{"ssid",ssid},{"password",password},{"open",open}};
    webServer.responses.clear();
    handleSaveWifi();
    assert(webServer.responses.size() == 1);
    return webServer.responses[0];
}

void wifiTests() {
    check("Wi-Fi legacy BSSID does not pin a reachable SSID or overwrite flash", [] {
        JsonDocument doc; deserializeJson(doc, legacyConfig);
        doc["bssi"] = "F0:9F:C2:24:13:21";
        std::string file; serializeJson(doc,file); putConfig(file); reboot();
        setupWifi();
        assert(WiFi.beginCalls == 1 && WiFi.lastSSID == "test-network");
        assert(WiFi.lastPassword == "test-password" && WiFi.lastBSSID == nullptr);
        assert(!WiFi.persistentValue && !WiFi.autoReconnect);
        assert(!wifiRuntime.apActive && savedConfig() == file);
        WiFi.connectionStatus = WL_CONNECTED;
        wifiLoop(10000);
        assert(!wifiRuntime.apActive && wifiRuntime.wasConnected);
        assertCredentials();
    });
    check("missing or malformed Wi-Fi settings open setup immediately", [] {
        setupWifi();
        assert(wifiRuntime.apActive && wifiDns.active && WiFi.beginCalls == 0);
        assert(WiFi.modeValue == WIFI_AP_STA);
        assert(std::string(wifiRuntime.apName) == "BL-SmartFlow-Setup-ABCDEF");
        assert(!LittleFS.exists(configPath));
        strcpy(globalVariables.SSID,"network");
        strcpy(globalVariables.APPW,"short");
        setupWifi();
        assert(wifiRuntime.apActive && WiFi.beginCalls == 0);
    });
    check("offline boot starts AP after 30 seconds and retains password on all retries", [] {
        putConfig(legacyConfig); reboot(); setupWifi();
        wifiLoop(29999); assert(!wifiRuntime.apActive && WiFi.beginCalls == 1);
        wifiLoop(30000); assert(wifiRuntime.apActive && WiFi.beginCalls == 2);
        wifiLoop(59999); assert(WiFi.beginCalls == 2 && wifiDns.requests == 1);
        wifiLoop(60000); assert(WiFi.beginCalls == 3 && WiFi.apCalls == 1);
        assert(WiFi.lastPassword == "test-password" && savedConfig() == legacyConfig);
        handleSetup(); assert(webServer.responses.back() == 200);
        fanloop();
    });
    check("network recovery closes AP after a full minute and later outages reopen it", [] {
        putConfig(legacyConfig); reboot(); setupWifi(); wifiLoop(30000);
        WiFi.connectionStatus=WL_CONNECTED; wifiLoop(31000);
        wifiLoop(90999); assert(wifiRuntime.apActive);
        wifiLoop(91000); assert(!wifiRuntime.apActive && !wifiDns.active && WiFi.apStops == 1);
        assert(WiFi.status() == WL_CONNECTED);
        WiFi.connectionStatus=WL_DISCONNECTED; wifiLoop(92000);
        wifiLoop(121999); assert(!wifiRuntime.apActive);
        wifiLoop(122000); assert(wifiRuntime.apActive && WiFi.apCalls == 2);
        assert(savedConfig() == legacyConfig);
    });
    check("short network flaps neither create AP nor cut an active portal short", [] {
        putConfig(legacyConfig); reboot(); setupWifi();
        WiFi.connectionStatus=WL_CONNECTED; wifiLoop(100);
        WiFi.connectionStatus=WL_DISCONNECTED; wifiLoop(5000);
        WiFi.connectionStatus=WL_CONNECTED; wifiLoop(10000);
        assert(!wifiRuntime.apActive);
        WiFi.connectionStatus=WL_DISCONNECTED; wifiLoop(20000); wifiLoop(50000);
        WiFi.connectionStatus=WL_CONNECTED; wifiLoop(51000);
        WiFi.connectionStatus=WL_DISCONNECTED; wifiLoop(90000);
        WiFi.connectionStatus=WL_CONNECTED; wifiLoop(95000);
        wifiLoop(111000); assert(wifiRuntime.apActive);
        wifiLoop(155000); assert(!wifiRuntime.apActive);
    });
    check("AP failure is retried on schedule and timers survive millis rollover", [] {
        WiFi.apResult=false; setupWifi(0xfffffff0);
        assert(!wifiRuntime.apActive && WiFi.apCalls == 1);
        wifiLoop(static_cast<uint32_t>(0xfffffff0u + 29999)); assert(WiFi.apCalls == 1);
        WiFi.apResult=true;
        wifiLoop(static_cast<uint32_t>(0xfffffff0u + 30000)); assert(wifiRuntime.apActive && WiFi.apCalls == 2);
    });
    check("Wi-Fi portal saves only Wi-Fi and retains both fans across reboot", [] {
        putConfig(legacyConfig); reboot();
        assert(options({{"fansSynchronized","off"},{"followPrinterFan","on"},{"printerFanSource","chamber"},
            {"postPrint","on"},{"postPrintSeconds","240"},{"postPrintSpeed","95"},
            {"staticfan2","on"},{"staticfanspeed2","43"}})==200);
        assert(curve(secondCurve,"2")==200);
        const auto curve1=printerConfig.fanGraph, curve2=printerConfig.fanGraph2;
        JsonDocument before; deserializeJson(before,savedConfig());
        setupWifi(); wifiLoop(30000);
        mockMillis=31000;
        assert(saveWifi("New network","new-password")==200);
        assert(wifiRuntime.apActive && wifiRuntime.reconnectPending);
        const int attempts=WiFi.beginCalls;
        wifiLoop(31499); assert(WiFi.beginCalls==attempts);
        wifiLoop(31500); assert(WiFi.beginCalls==attempts+1 && WiFi.lastSSID=="New network");
        JsonDocument after; deserializeJson(after,savedConfig());
        for (const char *key : {"ssid","appw","bssi"}) { before.remove(key); after.remove(key); }
        assert(before == after);
        reboot();
        assert(std::string(globalVariables.SSID)=="New network" && std::string(globalVariables.APPW)=="new-password");
        assert(std::string(printerConfig.accessCode)=="TEST1234");
        assert(printerConfig.followPrinterFan && printerConfig.staticFan2 && printerConfig.staticFanSpeed2==43);
        assert(printerConfig.postPrint.enabled && printerConfig.postPrint.seconds==240);
        assert(printerConfig.fanGraph==curve1 && printerConfig.fanGraph2==curve2);
    });
    for (int failure=0; failure<3; ++failure) {
        check("failed Wi-Fi save preserves credentials and flash and does not reconnect", [failure] {
            putConfig(legacyConfig); reboot();
            if (failure==0) failOpen=true;
            if (failure==1) writeBudget=5;
            if (failure==2) failRename=true;
            assert(saveWifi("New network","new-password")==500);
            assertCredentials(); assert(savedConfig()==legacyConfig);
            assert(!wifiRuntime.reconnectPending && !LittleFS.exists("/blledconfig.json.tmp"));
        });
    }
    check("Wi-Fi validation rejects malformed input and supports full-length credentials", [] {
        putConfig(legacyConfig); reboot();
        for (const auto &pair : std::vector<std::pair<String,String>>{
            {"","password"},{std::string(33,'s'),"password"},{"ssid","short"},
            {"ssid",std::string(64,'z')},{"ssid",std::string(65,'a')},
            {String(std::string("bad\0ssid",8)),"password"},
            {"ssid",String(std::string("password\0x",10))}}) {
            assert(saveWifi(pair.first,pair.second)==400);
            assertCredentials(); assert(savedConfig()==legacyConfig);
        }
        assert(saveWifi("network","password","invalid")==400);
        webServer.params={{"ssid","network"}}; handleSaveWifi(); assert(webServer.responses.back()==400);
        const String ssid(std::string(32,'s')), password(std::string(64,'a'));
        assert(saveWifi(ssid,password)==200); reboot();
        assert(std::string(globalVariables.SSID)==ssid && std::string(globalVariables.APPW)==password);
    });
    check("blank password retains same-network secret; open networks require an explicit choice", [] {
        putConfig(legacyConfig); reboot();
        assert(saveWifi("test-network","")==200);
        assertCredentials();
        assert(saveWifi("Other network","")==400);
        assert(saveWifi("Open network","","true")==200);
        reboot(); setupWifi();
        assert(WiFi.lastSSID=="Open network" && WiFi.lastPassword.empty());
        assert(!wifiRuntime.apActive && WiFi.beginCalls==1);
    });
    check("Wi-Fi status and portal do not reveal passwords or printer secrets", [] {
        putConfig(legacyConfig); reboot(); setupWifi(); wifiLoop(30000);
        handleWifiStatus(); const auto state=response();
        assert(state["ssid"]=="test-network" && state["hasPassword"]==true);
        assert(state["connected"]==false && state["apActive"]==true);
        assert(webServer.lastBody.find("test-password")==String::npos);
        assert(webServer.lastBody.find("TEST1234")==String::npos);
        WiFi.connectionStatus=WL_CONNECTED; handleWifiStatus();
        assert(response()["ip"]=="192.0.2.123");
        handleWifiPage(); assert(webServer.responses.back()==200);
    });
    check("error LED keeps its two-flash pattern without delaying recovery HTTP", [] {
        printerVariables.errorcode="no wifi";
        for (const auto &sample : std::vector<std::pair<uint32_t,int>>{
            {0,HIGH},{199,HIGH},{200,LOW},{400,HIGH},{600,LOW},{1599,LOW},{1600,HIGH}}) {
            mockMillis=sample.first; indicatorloop(); assert(digitalValues[LED_PIN]==sample.second);
        }
        assert(delayedMs==0);
    });
}
