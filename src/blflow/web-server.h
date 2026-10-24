#ifndef _BLLEDWEB_SERVER
#define _BLLEDWEB_SERVER

#include <Arduino.h>
#include <ArduinoJson.h> 
#include <WiFi.h>
#include <WiFiClient.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Update.h>
#include "filesystem.h"


WebServer webServer(80);

#include "../www/fanpage.h"
#include "../www/updatePage.h"
#include "../www/favicon.h"

bool isAuthorized() {
  return true; //webServer.authenticate("BLLC", printerConfig.webpagePassword);
}

void handleSetup(){
    if (!isAuthorized()){
        webServer.requestAuthentication();
        return;
    }
    webServer.sendHeader(F("Content-Encoding"), F("gzip"));
    webServer.send_P(200, "text/html", (const char*)fanpage_html_gz, (int)fanpage_html_gz_len);
}

template <typename T>
String toJson(T val) {
    return String(val);
}

template <>
String toJson<bool>(bool val) {
    return val ? "true" : "false";
}

String obfuscate(const char* value) {
    String result(value);
    for (size_t i = 0; i + 3 < result.length(); ++i) result[i] = '*';
    return result;
}

void handleGetOptions(){
    if (!isAuthorized()){
        webServer.requestAuthentication();
        return;
    }

    JsonDocument doc;
    const char* firmwareVersionChar = globalVariables.FWVersion.c_str();
    doc["firmwareversion"] = firmwareVersionChar;

    doc["ip"] = printerConfig.printerIP;
    doc["code"] = obfuscate(printerConfig.accessCode);
    doc["id"] = obfuscate(printerConfig.serialNumber);

    doc["staticfans"] = printerConfig.staticFan;
    doc["staticfanspeed"] = printerConfig.staticFanSpeed;
    doc["followPrinterFan"] = printerConfig.followPrinterFan;
    doc["printerFanSource"] = printerFanSourceName(printerConfig.printerFanSource);
    doc["fansSynchronized"] = printerConfig.fansSynchronized;
    const bool fan2Configured = !printerConfig.fanGraph2.empty();
    doc["fan2Configured"] = fan2Configured;
    doc["staticfans2"] = fan2Configured ? printerConfig.staticFan2 : printerConfig.staticFan;
    doc["staticfanspeed2"] = fan2Configured ? printerConfig.staticFanSpeed2 : printerConfig.staticFanSpeed;
    doc["followPrinterFan2"] = fan2Configured ? printerConfig.followPrinterFan2 : printerConfig.followPrinterFan;
    doc["printerFanSource2"] = printerFanSourceName(fan2Configured ? printerConfig.printerFanSource2 : printerConfig.printerFanSource);

    // Debugging
    doc["debuging"] = printerConfig.debuging;
    doc["debugingchange"] = printerConfig.debugingchange;
    doc["mqttdebug"] = printerConfig.mqttdebug;


    String jsonString;
    serializeJson(doc, jsonString);
    webServer.send(200, "application/json", jsonString);

    Serial.println(F("Packet sent to setuppage"));
}

// All web updates are staged, persisted, then applied to the running controller.
bool readSpeedArgument(const char *name, int &speed) {
    if (!webServer.hasArg(name)) return true;
    const String value = webServer.arg(name);
    if (value.length() == 0 || value.length() > 3) return false;
    for (size_t i = 0; i < value.length(); ++i) {
        if (value[i] < '0' || value[i] > '9') return false;
    }
    const int parsed = value.toInt();
    if (parsed > 100) return false;
    speed = parsed;
    return true;
}

bool readBoolArgument(const char *name, bool &value) {
    if (!webServer.hasArg(name)) return true;
    const String arg = webServer.arg(name);
    if (arg == "on" || arg == "true") value = true;
    else if (arg == "off" || arg == "false") value = false;
    else return false;
    return true;
}

bool readFanModeArguments(const char *staticName, const char *followName, const char *sourceName,
                          bool &staticEnabled, bool &followEnabled, PrinterFanSource &source) {
    if (!readBoolArgument(staticName, staticEnabled) || !readBoolArgument(followName, followEnabled)) return false;
    if (webServer.hasArg(sourceName) && !readPrinterFanSource(webServer.arg(sourceName).c_str(), source)) return false;
    const bool enableStatic = webServer.hasArg(staticName) && staticEnabled;
    const bool enableFollow = webServer.hasArg(followName) && followEnabled;
    if (enableStatic && enableFollow) return false;
    if (enableStatic) followEnabled = false;
    if (enableFollow) staticEnabled = false;
    return true;
}

template <size_t N>
bool readStringArgument(const char *name, char (&value)[N]) {
    if (!webServer.hasArg(name)) return true;
    const String arg = webServer.arg(name);
    if (arg.indexOf('*') != -1) return true;
    if (arg.length() >= N) return false;
    strcpy(value, arg.c_str());
    return true;
}

void submitOptions() {
    if (webServer.args() == 0) {
        webServer.send(204);
        return;
    }
    PrinterConfig candidate = printerConfig;
    if (!readStringArgument("ip", candidate.printerIP) ||
        !readStringArgument("code", candidate.accessCode) ||
        !readStringArgument("serial", candidate.serialNumber)) {
        webServer.send(400, "text/plain", "Printer field is too long");
        return;
    }
    for (size_t i = 0; i < strlen(candidate.serialNumber); ++i) {
        candidate.serialNumber[i] = toupper(static_cast<unsigned char>(candidate.serialNumber[i]));
    }
    // Legacy forms omit unchecked Fan 1 checkboxes.
    candidate.staticFan = false;
    if (!readBoolArgument("fansSynchronized", candidate.fansSynchronized) ||
        !readFanModeArguments("staticfan", "followPrinterFan", "printerFanSource",
            candidate.staticFan, candidate.followPrinterFan, candidate.printerFanSource) ||
        !readSpeedArgument("staticfanspeed", candidate.staticFanSpeed)) {
        webServer.send(400, "text/plain", "Invalid Fan 1 settings; choose one override, a valid printer fan, and an integer speed from 0 to 100");
        return;
    }
    if (!candidate.fansSynchronized) {
        initializeSecondFan(candidate);
        if (!readFanModeArguments("staticfan2", "followPrinterFan2", "printerFanSource2",
                candidate.staticFan2, candidate.followPrinterFan2, candidate.printerFanSource2) ||
            !readSpeedArgument("staticfanspeed2", candidate.staticFanSpeed2)) {
            webServer.send(400, "text/plain", "Invalid Fan 2 settings; choose one override, a valid printer fan, and an integer speed from 0 to 100");
            return;
        }
    }
    candidate.debuging = (webServer.arg("debuging") == "on");
    candidate.debugingchange = (webServer.arg("debugingchange") == "on");
    candidate.mqttdebug = (webServer.arg("mqttdebug") == "on");
    if (!saveFileSystem(candidate)) {
        webServer.send(500, "text/plain", "Failed to save settings to flash");
        return;
    }
    printerConfig = std::move(candidate);
    webServer.send(200, "text/plain", "Settings saved");
}

void handleGetFanConfig() {
    JsonDocument doc;
    doc["fansSynchronized"] = printerConfig.fansSynchronized;
    writeFanCurve(doc["points"].to<JsonArray>(), printerConfig.fanGraph);
    writeFanCurve(doc["points2"].to<JsonArray>(), printerConfig.fanGraph2.empty()
                 ? printerConfig.fanGraph : printerConfig.fanGraph2);
    String response;
    serializeJson(doc, response);
    webServer.send(200, "application/json", response);
}

void handleUpdateFanConfig() {
    const String fan = webServer.hasArg("fan") ? webServer.arg("fan") : String("1");
    if ((fan != "1" && fan != "2") || !webServer.hasArg("points")) {
        webServer.send(400, "text/plain", "Supply points and a fan number of 1 or 2");
        return;
    }
    JsonDocument doc;
    FanCurve curve;
    if (deserializeJson(doc, webServer.arg("points")) || !readFanCurve(doc["points"], curve)) {
        webServer.send(400, "text/plain", "Use unique temperatures from 0 to 400 and integer speeds from 0 to 100; at least one point is required");
        return;
    }
    PrinterConfig candidate = printerConfig;
    if (fan == "2") {
        initializeSecondFan(candidate);
        candidate.fanGraph2 = std::move(curve);
    } else {
        candidate.fanGraph = std::move(curve);
    }
    if (!saveFileSystem(candidate)) {
        webServer.send(500, "text/plain", "Failed to save fan curve to flash");
        return;
    }
    printerConfig = std::move(candidate);
    webServer.send(200, "text/plain", "Fan curve saved");
}

void handleSensorData() {
    JsonDocument doc;
    doc["temp"] = printerVariables.nozzletemp;
    doc["speed"] = globalVariables.fanSpeed;
    doc["speed2"] = globalVariables.fanSpeed2;
    for (int fan = 0; fan < printerFanCount; ++fan) {
        const char *name = printerFanSourceName(static_cast<PrinterFanSource>(fan));
        if (printerVariables.fans[fan].known) doc["printerFans"][name] = printerVariables.fans[fan].percent;
        else doc["printerFans"][name] = nullptr;
    }
    String response;
    serializeJson(doc, response);
    webServer.send(200, "application/json", response);
}

void setupWebserver(){
    if (!MDNS.begin(globalVariables.Host.c_str())) {
        Serial.println(F("Error setting up MDNS responder!"));
        while (1) {
        delay(1000);
        }
    }
    
    Serial.println(F("Setting up webserver"));
    
    webServer.on("/", handleSetup);
    webServer.on("/favicon.ico", HTTP_GET, []() {
        webServer.sendHeader(F("Content-Encoding"), F("gzip"));
        webServer.send_P(200, "image/x-icon", (const char*)favicon_ico_gz, (int)favicon_ico_gz_len);
    });
    webServer.on("/submitOptions",HTTP_POST,submitOptions);
    webServer.on("/getOptions", handleGetOptions);

    webServer.on("/update", HTTP_POST, []() { //OTA
        webServer.sendHeader("Connection", "close");
        webServer.send(200, "text/plain", (Update.hasError()) ? "FAIL" : "OK");
        Serial.println(F("Restarting Device"));
        delay(1000);
        ESP.restart();
    }, []() {
        HTTPUpload& upload = webServer.upload();
        if (upload.status == UPLOAD_FILE_START) {
            Serial.printf("Update: %s\n", upload.filename.c_str());
            if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
                Update.printError(Serial);
            }
        } else if (upload.status == UPLOAD_FILE_WRITE) {
        
            if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
                Update.printError(Serial);
            }
        } else if (upload.status == UPLOAD_FILE_END) {
            if (Update.end(true)) {
                Serial.printf("Update Success: %u\nRebooting...\n", upload.totalSize);
            } else {
                Update.printError(Serial);
            }
        }
    });

    webServer.on("/getFanConfig", HTTP_GET, handleGetFanConfig);
    webServer.on("/updateFanConfig", HTTP_POST, handleUpdateFanConfig);
    webServer.on("/sensorData", HTTP_GET, handleSensorData);

    webServer.begin();

    Serial.println(F("Webserver started"));
    Serial.println();
}

void webserverloop(){
    webServer.handleClient();
    delay(10);
}

#endif
