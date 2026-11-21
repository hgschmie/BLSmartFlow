#ifndef _BLLEDWIFI_MANAGER
#define _BLLEDWIFI_MANAGER

#include <Arduino.h>
#include <WiFi.h>
#include <DNSServer.h>
#include "filesystem.h"

constexpr uint32_t wifiRetryMs = 30000;
constexpr uint32_t wifiPortalGraceMs = 60000;
const char *wifiSetupPassword = "smartflow-setup";
DNSServer wifiDns;
struct WifiRuntime {
    bool apActive = false;
    bool wasConnected = false;
    bool apAttempted = false;
    bool reconnectPending = false;
    uint32_t offlineSince = 0;
    uint32_t attemptStarted = 0;
    uint32_t apAttemptStarted = 0;
    uint32_t connectedSince = 0;
    uint32_t reconnectRequested = 0;
    char apName[33] = {};
} wifiRuntime;

bool validWifiCredentials(const String &ssid, const String &password) {
    if (ssid.length() == 0 || ssid.length() > 32 || strlen(ssid.c_str()) != ssid.length() ||
        strlen(password.c_str()) != password.length()) return false;
    if (password.length() == 0) return true; // Explicitly selected open network.
    if (password.length() == 64) {
        for (size_t i = 0; i < password.length(); ++i)
            if (!isxdigit(static_cast<unsigned char>(password[i]))) return false;
        return true;
    }
    return password.length() >= 8 && password.length() <= 63;
}

void startWifiPortal(uint32_t now) {
    if (wifiRuntime.apActive || (wifiRuntime.apAttempted &&
        static_cast<uint32_t>(now - wifiRuntime.apAttemptStarted) < wifiRetryMs)) return;
    wifiRuntime.apAttempted = true;
    wifiRuntime.apAttemptStarted = now;
    WiFi.mode(WIFI_AP_STA);
    const IPAddress ip(192, 168, 4, 1);
    if (!WiFi.softAPConfig(ip, ip, IPAddress(255, 255, 255, 0)) ||
        !WiFi.softAP(wifiRuntime.apName, wifiSetupPassword)) {
        Serial.println(F("Could not start Wi-Fi setup hotspot; will retry"));
        return;
    }
    wifiRuntime.apActive = true;
    if (!wifiDns.start(53, "*", ip))
        Serial.println(F("Setup DNS unavailable; use http://192.168.4.1/"));
    Serial.print(F("Wi-Fi setup hotspot: "));
    Serial.println(wifiRuntime.apName);
    Serial.println(F("Setup password: smartflow-setup | Open http://192.168.4.1/"));
}

void startWifiAttempt(uint32_t now) {
    wifiRuntime.attemptStarted = now;
    // Do not bind to a stored BSSID: routers and mesh nodes can change their MAC.
    // Never retry a secured network with its password silently removed.
    WiFi.disconnect();
    WiFi.begin(globalVariables.SSID, globalVariables.APPW);
    Serial.print(F("Connecting to Wi-Fi network: "));
    Serial.println(globalVariables.SSID);
}

void requestWifiReconnect(uint32_t now = millis()) {
    // Leave time for the HTTP save response to reach the browser first.
    wifiRuntime.reconnectPending = true;
    wifiRuntime.reconnectRequested = now;
}

void setupWifi(uint32_t now = millis()) {
    wifiRuntime = WifiRuntime{};
    wifiRuntime.offlineSince = now;
    WiFi.persistent(false); // LittleFS is the sole source of saved credentials.
    WiFi.setAutoReconnect(false); // Retry on our bounded schedule.
    WiFi.setScanMethod(WIFI_ALL_CHANNEL_SCAN);
    WiFi.setSortMethod(WIFI_CONNECT_AP_BY_SIGNAL);
    WiFi.setHostname(globalVariables.Host.c_str());
    WiFi.mode(WIFI_STA);
    uint8_t mac[6];
    WiFi.macAddress(mac);
    snprintf(wifiRuntime.apName, sizeof(wifiRuntime.apName), "BL-SmartFlow-Setup-%02X%02X%02X",
             mac[3], mac[4], mac[5]);
    printerConfig.BSSID[0] = '\0'; // Retain legacy files, but ignore their AP pin.
    if (validWifiCredentials(globalVariables.SSID, globalVariables.APPW)) {
        startWifiAttempt(now);
    } else {
        WiFi.disconnect();
        printerVariables.errorcode = "no config";
        Serial.println(F("No usable saved Wi-Fi settings; starting setup hotspot"));
        startWifiPortal(now);
    }
}

void wifiLoop(uint32_t now = millis()) {
    if (wifiRuntime.apActive) wifiDns.processNextRequest();
    if (printerConfig.rescanWiFiNetwork) {
        printerConfig.rescanWiFiNetwork = false;
        requestWifiReconnect(now);
    }
    if (wifiRuntime.reconnectPending) {
        if (static_cast<uint32_t>(now - wifiRuntime.reconnectRequested) < 500) return;
        wifiRuntime.reconnectPending = false;
        wifiRuntime.wasConnected = false;
        wifiRuntime.offlineSince = now;
        startWifiAttempt(now);
        return; // Let MQTT observe the disconnect before accepting the next connection.
    }
    if (WiFi.status() == WL_CONNECTED) {
        if (!wifiRuntime.wasConnected) {
            wifiRuntime.wasConnected = true;
            wifiRuntime.connectedSince = now;
            Serial.print(F("IP_ADDRESS:")); // Used by the USB setup page.
            Serial.println(WiFi.localIP());
            Serial.print(F("Connected Wi-Fi access point: "));
            Serial.println(WiFi.BSSIDstr());
        }
        if (printerVariables.errorcode == "no wifi" || printerVariables.errorcode == "no config")
            printerVariables.errorcode = "";
        // Allow the portal to show the new LAN address before closing the hotspot.
        if (wifiRuntime.apActive && static_cast<uint32_t>(now - wifiRuntime.connectedSince) >= wifiPortalGraceMs) {
            wifiDns.stop();
            WiFi.softAPdisconnect(true); // Disables AP only; station remains connected.
            wifiRuntime.apActive = false;
            wifiRuntime.apAttempted = false;
        }
        return;
    }
    if (wifiRuntime.wasConnected) {
        wifiRuntime.wasConnected = false;
        wifiRuntime.offlineSince = now;
        Serial.println(F("Wi-Fi connection lost; retrying saved network"));
        startWifiAttempt(now);
    }
    const bool configured = validWifiCredentials(globalVariables.SSID, globalVariables.APPW);
    printerVariables.errorcode = configured ? "no wifi" : "no config";
    if (!configured || static_cast<uint32_t>(now - wifiRuntime.offlineSince) >= wifiRetryMs)
        startWifiPortal(now);
    if (configured && static_cast<uint32_t>(now - wifiRuntime.attemptStarted) >= wifiRetryMs) {
        Serial.println(F("Wi-Fi unavailable; check network availability, signal and password. Retrying by SSID."));
        startWifiAttempt(now);
    }
}

#endif
