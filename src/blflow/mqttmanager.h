#ifndef _MQTTMANAGER
#define _MQTTMANAGER

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include "AutoGrowBufferStream.h"
#include "types.h"
#include "mqtt-report.h"
#include "fans.h"

constexpr uint32_t mqttRetryMs = 3000;

struct MqttSettings {
    char ip[sizeof(printerConfig.printerIP)] = {};
    char code[sizeof(printerConfig.accessCode)] = {};
    char serial[sizeof(printerConfig.serialNumber)] = {};
};

enum class MqttOperation { Connect, Poll, Disconnect };
struct MqttRequest {
    MqttOperation operation = MqttOperation::Poll;
    MqttSettings settings;
};
struct MqttResult {
    MqttOperation operation;
    bool connected;
    int state;
    uint16_t reportLength;
};

// Only the worker accesses the socket/client. A single request/result exchange
// hands the report buffer back to loop(), where all configuration, printer
// state, JSON parsing and fan output changes remain serialized with HTTP saves.
WiFiClientSecure wifiSecureClient;
PubSubClient mqttClient(wifiSecureClient);
AutoGrowBufferStream stream;
QueueHandle_t mqttRequests = nullptr;
QueueHandle_t mqttResults = nullptr;
TaskHandle_t mqttWorker = nullptr;
char clientId[20] = {};
bool mqttReportReceived = false; // Worker only.
MqttSettings mqttConnectionSettings; // Main loop only.
bool mqttWorkInFlight = false;
bool mqttDiscardResult = false;
bool mqttTransportConnected = false;
MqttOperation mqttPendingOperation = MqttOperation::Poll; // Main loop only.
uint32_t mqttattempt = 0; // Time of the last completed attempt, not its start.

void ParseMQTTState(int code){
    switch (code)
    {
    case -4: // MQTT_CONNECTION_TIMEOUT
        Serial.println(F("MQTT TIMEOUT"));
        printerVariables.errorcode = "no mqtt";
        break;
    case -3: // MQTT_CONNECTION_LOST
        Serial.println(F("MQTT CONNECTION_LOST"));
        printerVariables.errorcode = "no mqtt";
        break;
    case -2: // MQTT_CONNECT_FAILED
        Serial.println(F("MQTT CONNECT_FAILED"));
        printerVariables.errorcode = "no mqtt";
        break;
    case -1: // MQTT_DISCONNECTED
        Serial.println(F("MQTT DISCONNECTED"));
        printerVariables.errorcode = "no mqtt";
        break;
    case 0:  // MQTT_CONNECTED
        Serial.println(F("MQTT CONNECTED"));
        if (printerVariables.errorcode == "no mqtt"){
            //if this routine set the error code, clear it
            printerVariables.errorcode = "";
        }
        break;
    case 1:  // MQTT_CONNECT_BAD_PROTOCOL
        Serial.println(F("MQTT BAD PROTOCOL"));
        printerVariables.errorcode = "no mqtt";
        break;
    case 2:  // MQTT_CONNECT_BAD_CLIENT_ID
        Serial.println(F("MQTT BAD CLIENT ID"));
        printerVariables.errorcode = "no mqtt";
        break;
    case 3:  // MQTT_CONNECT_UNAVAILABLE
        Serial.println(F("MQTT UNAVAILABLE"));
        printerVariables.errorcode = "no mqtt";
        break;
    case 4:  // MQTT_CONNECT_BAD_CREDENTIALS
        Serial.println(F("MQTT BAD CREDENTIALS"));
        printerVariables.errorcode = "no mqtt";
        break;
    case 5: // MQTT UNAUTHORIZED
        Serial.println(F("MQTT UNAUTHORIZED"));
        printerVariables.errorcode = "no mqtt";
        break;
    }
}

MqttSettings currentMqttSettings() {
    MqttSettings settings;
    memcpy(settings.ip, printerConfig.printerIP, sizeof(settings.ip));
    memcpy(settings.code, printerConfig.accessCode, sizeof(settings.code));
    memcpy(settings.serial, printerConfig.serialNumber, sizeof(settings.serial));
    return settings;
}

bool sameMqttSettings(const MqttSettings &a, const MqttSettings &b) {
    return strcmp(a.ip, b.ip) == 0 && strcmp(a.code, b.code) == 0 && strcmp(a.serial, b.serial) == 0;
}

struct MqttConnectionStatus {
    const char *state;
    uint32_t retryInSeconds;
};

MqttConnectionStatus mqttConnectionStatus(uint32_t now) {
    // Read main-loop state only; HTTP must never inspect the worker's socket.
    if (WiFi.status() != WL_CONNECTED) return {"wifi_disconnected", 0};
    const MqttSettings settings = currentMqttSettings();
    if (!settings.ip[0] || !settings.code[0] || !settings.serial[0]) return {"not_configured", 0};
    if (mqttDiscardResult || (!sameMqttSettings(settings, mqttConnectionSettings) &&
                             (mqttWorkInFlight || mqttTransportConnected))) return {"reconnecting", 0};
    if (printerVariables.online && mqttTransportConnected) return {"connected", 0};
    if (mqttWorkInFlight) {
        return {mqttPendingOperation == MqttOperation::Connect ? "connecting" : "reconnecting", 0};
    }
    const uint32_t elapsed = now - mqttattempt;
    const uint32_t remaining = elapsed < mqttRetryMs ? mqttRetryMs - elapsed : 0;
    return {"retrying", (remaining + 999) / 1000};
}

void mqttCallback(char *, byte *, unsigned int) {
    // PubSubClient has streamed the full payload, including reports larger than
    // its packet buffer. Do not touch shared printer/configuration state here.
    mqttReportReceived = true;
}

void mqttTask(void *) {
    // WiFiClientSecure's TCP/socket and handshake timeouts are in seconds.
    wifiSecureClient.setInsecure();
    wifiSecureClient.setTimeout(5);
    wifiSecureClient.setHandshakeTimeout(5);
    mqttClient.setBufferSize(1024);
    mqttClient.setStream(stream);
    mqttClient.setCallback(mqttCallback);
    mqttClient.setSocketTimeout(3);
    MqttSettings connectionSettings; // Stable storage for setServer's pointer.
    MqttRequest request;
    for (;;) {
        if (xQueueReceive(mqttRequests, &request, portMAX_DELAY) != pdTRUE) continue;
        stream.flush();
        mqttReportReceived = false;
        int state = -1;
        if (request.operation == MqttOperation::Disconnect) {
            mqttClient.disconnect();
            wifiSecureClient.stop();
        } else if (request.operation == MqttOperation::Connect) {
            connectionSettings = request.settings;
            mqttClient.setServer(connectionSettings.ip, 8883);
            char topic[sizeof(connectionSettings.serial) + 15];
            snprintf(topic, sizeof(topic), "device/%s/report", connectionSettings.serial);
            if (mqttClient.connect(clientId, "bblp", connectionSettings.code)) {
                if (!mqttClient.subscribe(topic)) {
                    state = -2;
                    mqttClient.disconnect();
                }
            } else {
                state = mqttClient.state();
            }
            if (state != -1) wifiSecureClient.stop();
        } else {
            mqttClient.loop();
        }
        const bool connected = mqttClient.connected();
        if (state == -1) state = mqttClient.state();
        MqttResult result{request.operation, connected, state,
            static_cast<uint16_t>(connected && mqttReportReceived ? stream.current_length() : 0)};
        xQueueSend(mqttResults, &result, portMAX_DELAY);
        // The worker waits here until loop() consumes the result and payload.
    }
}

bool startMqttWorker() {
    if (mqttWorker) return true;
    if (!mqttRequests) mqttRequests = xQueueCreate(1, sizeof(MqttRequest));
    if (!mqttResults) mqttResults = xQueueCreate(1, sizeof(MqttResult));
    if (!mqttRequests || !mqttResults ||
        xTaskCreate(mqttTask, "printer-mqtt", 8192, nullptr, 1, &mqttWorker) != pdPASS) {
        Serial.println(F("Unable to start MQTT worker; web interface remains available"));
        printerVariables.errorcode = "no mqtt";
        return false;
    }
    return true;
}

void scheduleMqtt(MqttOperation operation, const MqttSettings &settings = MqttSettings{}) {
    MqttRequest request;
    request.operation = operation;
    request.settings = settings;
    // With one request in flight this queue has space. Never wait in loop().
    mqttWorkInFlight = xQueueSend(mqttRequests, &request, 0) == pdTRUE;
    if (mqttWorkInFlight) mqttPendingOperation = operation;
}

void setupMqtt() {
    snprintf(clientId, sizeof(clientId), "BLLED-%04lx", random(0xffff));
    mqttConnectionSettings = currentMqttSettings();
    mqttattempt = static_cast<uint32_t>(millis()) - mqttRetryMs;
    startMqttWorker();
    // No network connection in setup(): allow the HTTP loop to start first.
}

void mqttloop() {
    const uint32_t now = millis();
    const bool wifiConnected = WiFi.status() == WL_CONNECTED;
    const MqttSettings settings = currentMqttSettings();
    if (!wifiConnected || !sameMqttSettings(settings, mqttConnectionSettings)) {
        printerCommunicationLost();
        fanloop();
        // A connect/read already in progress owns its socket until it returns.
        // Discard its result even if Wi-Fi or the settings change back meanwhile.
        if (mqttWorkInFlight || mqttTransportConnected) mqttDiscardResult = true;
    }

    if (mqttWorkInFlight) {
        MqttResult result;
        if (xQueueReceive(mqttResults, &result, 0) != pdTRUE) return;
        mqttWorkInFlight = false;
        mqttTransportConnected = result.connected;
        if (result.operation == MqttOperation::Disconnect) {
            mqttDiscardResult = false;
            mqttTransportConnected = false;
            mqttattempt = now - mqttRetryMs;
        } else if (mqttDiscardResult) {
            scheduleMqtt(MqttOperation::Disconnect);
            return;
        } else if (result.connected) {
            printerVariables.online = true;
            printerVariables.disconnectMQTTms = 0;
            if (printerVariables.errorcode == "no mqtt") printerVariables.errorcode = "";
            if (result.operation == MqttOperation::Connect) ParseMQTTState(0);
            if (result.reportLength && parsePrinterReport(
                    reinterpret_cast<const uint8_t *>(stream.get_buffer()), result.reportLength) && printerConfig.debuging) {
                Serial.println(F("Change from MQTT"));
            }
        } else {
            printerCommunicationLost();
            fanloop();
            if (printerVariables.disconnectMQTTms == 0) printerVariables.disconnectMQTTms = now;
            ParseMQTTState(result.state);
            // Back off from completion, even if the connection took a long time.
            mqttattempt = now;
        }
    }

    if (mqttDiscardResult) {
        scheduleMqtt(MqttOperation::Disconnect);
        return;
    }
    if (!wifiConnected) return;
    if (mqttTransportConnected) {
        scheduleMqtt(MqttOperation::Poll);
        return;
    }
    if (!settings.ip[0] || !settings.code[0] || !settings.serial[0]) return;
    if (static_cast<uint32_t>(now - mqttattempt) < mqttRetryMs) return;
    mqttattempt = now; // Also rate-limit worker allocation failures.
    if (!startMqttWorker()) return;
    mqttConnectionSettings = settings;
    Serial.println(F("Connecting to mqtt in background..."));
    scheduleMqtt(MqttOperation::Connect, settings);
}

#endif
