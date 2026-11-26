#include <Arduino.h>
#include "./blflow/web-server.h"
#include "./blflow/mqttmanager.h"
#include "./blflow/filesystem.h"
#include "./blflow/types.h"
#include "./blflow/fans.h"
#include "./blflow/serialmanager.h"
#include "./blflow/wifi-manager.h"
#include "./blflow/ssdp.h"
#include "./blflow/indicator.h"

bool networkDiscoveryStarted = false;

void setup(){
    Serial.begin(115200);
    delay(100);
    setupDiagnostics();
    Serial.println(F("Initializing"));
    Serial.println(ESP.getFreeHeap());
    Serial.println("");
    Serial.print(F("** Using firmware version: "));
    Serial.print(globalVariables.FWVersion);
    Serial.println(F(" **"));
    Serial.println("");
    
    fansetup();
    indicatorsetup();
    delay(1000);
    setupFileSystem();
    loadFileSystem();
    Serial.println(F(""));
    delay(500);

    setupSerial();

    setupWifi();
    setupWebserver();
    setupMqtt();

    Serial.println();
    Serial.print(F("** BLLED Controller started "));
    Serial.print(F("using firmware version: "));
    Serial.print(globalVariables.FWVersion);
    Serial.println(F(" **"));
    Serial.println();
    globalVariables.started = true;
}

void loop(){
    diagnosticsLoop();
    wifiLoop();
    fanloop();
    telemetryHistory.sample(esp_timer_get_time() / 1000000, printerVariables.online && printerVariables.nozzleKnown,
        printerVariables.nozzletemp, globalVariables.fanSpeed, globalVariables.fanSpeed2);
    webserverloop();
    mqttloop();
    indicatorloop();
    serialLoop();

    const bool connected = WiFi.status() == WL_CONNECTED;
    if (connected && !networkDiscoveryStarted) {
        // Discovery failure must never prevent HTTP or the recovery portal.
        if (!MDNS.begin(globalVariables.Host.c_str()))
            Serial.println(F("mDNS unavailable; use the controller IP address"));
        start_ssdp();
        networkDiscoveryStarted = true;
    } else if (!connected && networkDiscoveryStarted) {
        MDNS.end();
        SSDP.end();
        networkDiscoveryStarted = false;
    }
}
