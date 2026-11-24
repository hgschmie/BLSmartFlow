#pragma once
// Minimal host substitutes for ESP32 I/O; production JSON and firmware code are used unchanged.
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>
#include "freertos-stubs.h"
#define F(x) x
#define PROGMEM
class String : public std::string {
public:
    using std::string::string;
    String() = default;
    String(const std::string& s) : std::string(s) {}
    String(int n) : std::string(std::to_string(n)) {}
    String(float n) : std::string(std::to_string(n)) {}
    String(double n, int = 2) : std::string(std::to_string(n)) {}
    String(long n, int) : std::string(std::to_string(n)) {}
    int indexOf(char c) const { auto p=find(c); return p==npos ? -1 : int(p); }
    int toInt() const { return std::atoi(c_str()); }
    size_t write(uint8_t c) { push_back(char(c)); return 1; }
    size_t write(const uint8_t* p, size_t n) { append((const char*)p,n); return n; }
    size_t cursor = 0;
    int read() { return cursor < size() ? uint8_t((*this)[cursor++]) : -1; }
    size_t readBytes(char* p, size_t n) { n=std::min(n,size()-cursor); memcpy(p,data()+cursor,n); cursor+=n; return n; }
};
struct SerialMock {
    std::string output;
    template<class T> void print(const T &value) {
        std::ostringstream text; text << value; output += text.str();
    }
    void println() { output += '\n'; }
    template<class T> void println(const T &value) { print(value); println(); }
    template<class... T> void printf(const char *format, T... args) {
        char buffer[1024]; snprintf(buffer, sizeof(buffer), format, args...); output += buffer;
    }
    size_t write(uint8_t value) { output += static_cast<char>(value); return 1; }
    size_t write(const uint8_t* data, size_t n) { output.append(reinterpret_cast<const char *>(data), n); return n; }
} Serial;
struct ESPMock {
    int restarts = 0;
    void restart() { ++restarts; }
    uint32_t getFreeHeap() { return 120000; }
    uint32_t getMinFreeHeap() { return 100000; }
    uint32_t getMaxAllocHeap() { return 60000; }
} ESP;
inline uint32_t mockMillis = 0;
inline unsigned long millis() { return mockMillis; }
enum esp_reset_reason_t { ESP_RST_UNKNOWN, ESP_RST_POWERON, ESP_RST_EXT, ESP_RST_SW,
    ESP_RST_PANIC, ESP_RST_INT_WDT, ESP_RST_TASK_WDT, ESP_RST_WDT,
    ESP_RST_DEEPSLEEP, ESP_RST_BROWNOUT, ESP_RST_SDIO };
inline esp_reset_reason_t mockResetReason = ESP_RST_POWERON;
inline esp_reset_reason_t esp_reset_reason() { return mockResetReason; }
inline uint32_t esp_random() { static uint32_t next = 0x12345678; return next++; }
inline int64_t esp_timer_get_time() { return static_cast<int64_t>(mockMillis) * 1000; }
inline unsigned long delayedMs = 0;
inline void delay(int ms) { delayedMs += ms; }

// Connectivity substitutes exercise the real MQTT loop's fallback paths.
using byte = uint8_t;
constexpr int WL_CONNECTED = 3, WL_DISCONNECTED = 6, HEX = 16;
constexpr int WIFI_STA = 1, WIFI_AP_STA = 3;
constexpr int WIFI_ALL_CHANNEL_SCAN = 1, WIFI_CONNECT_AP_BY_SIGNAL = 0;
struct IPAddress {
    String text;
    IPAddress(int a, int b, int c, int d) : text(std::to_string(a)+"."+std::to_string(b)+"."+std::to_string(c)+"."+std::to_string(d)) {}
    String toString() const { return text; }
    friend std::ostream &operator<<(std::ostream &out, const IPAddress &ip) { return out << ip.text; }
};
inline long random(long maximum) { return maximum - 1; }
enum arduino_event_id_t { ARDUINO_EVENT_WIFI_STA_CONNECTED, ARDUINO_EVENT_WIFI_STA_DISCONNECTED,
    ARDUINO_EVENT_WIFI_STA_GOT_IP, ARDUINO_EVENT_WIFI_STA_LOST_IP, ARDUINO_EVENT_WIFI_STA_STOP };
struct arduino_event_info_t {
    struct { uint8_t bssid[6] = {}; uint8_t reason = 0; int8_t rssi = 0; } wifi_sta_disconnected;
    struct { uint8_t bssid[6] = {}; uint8_t channel = 0; } wifi_sta_connected;
};
struct WiFiMock {
    std::atomic<int> connectionStatus{WL_CONNECTED};
    int beginCalls = 0, apCalls = 0, apStops = 0, modeValue = WIFI_STA;
    bool apResult = true, persistentValue = true, autoReconnect = true;
    std::string lastSSID, lastPassword;
    const uint8_t *lastBSSID = nullptr;
    std::function<void(arduino_event_id_t, arduino_event_info_t)> eventHandler;
    void onEvent(decltype(eventHandler) callback) { eventHandler = callback; }
    String macAddress() { return "10:20:30:AB:CD:EF"; }
    int32_t RSSI() { return -50; }
    int32_t channel() { return 7; }
    int status() { return connectionStatus.load(); }
    void persistent(bool value) { persistentValue = value; }
    void setAutoReconnect(bool value) { autoReconnect = value; }
    void setHostname(const char *) {}
    void setScanMethod(int) {}
    void setSortMethod(int) {}
    void mode(int value) { modeValue = value; }
    void macAddress(uint8_t *mac) { const uint8_t value[6] = {0x10,0x20,0x30,0xAB,0xCD,0xEF}; memcpy(mac,value,6); }
    void disconnect() { connectionStatus = WL_DISCONNECTED; }
    void begin(const char *ssid, const char *password, int = 0, const uint8_t *bssid = nullptr) {
        ++beginCalls; lastSSID=ssid; lastPassword=password; lastBSSID=bssid;
    }
    bool softAPConfig(IPAddress, IPAddress, IPAddress) { return true; }
    bool softAP(const char *, const char *) { ++apCalls; return apResult; }
    bool softAPdisconnect(bool) { ++apStops; modeValue=WIFI_STA; return true; }
    IPAddress localIP() { return IPAddress(192,0,2,123); }
    String BSSIDstr() { return "F0:9F:C2:24:20:1A"; }
    void reset() {
        beginCalls=apCalls=apStops=0; modeValue=WIFI_STA; apResult=true;
        persistentValue=autoReconnect=true; lastSSID.clear(); lastPassword.clear(); lastBSSID=nullptr;
        eventHandler = {};
    }
} WiFi;
struct DNSServer {
    bool active = false;
    int requests = 0;
    bool start(int, const String &, const IPAddress &) { active=true; return true; }
    void stop() { active=false; }
    void processNextRequest() { ++requests; }
};
struct WiFiClientSecure {
    void setInsecure() { assert(mockTaskThread); }
    void setTimeout(int) { assert(mockTaskThread); }
    void setHandshakeTimeout(int) { assert(mockTaskThread); }
    void stop() { assert(mockTaskThread); }
};
class Print {
public:
    virtual ~Print() = default;
    virtual size_t write(uint8_t) = 0;
};
class Stream : public Print {
public:
    virtual int read() = 0;
    virtual int available() = 0;
    virtual int peek() = 0;
    virtual void flush() = 0;
};
struct PubSubClient {
    bool isConnected = true, connectResult = false, dropDuringLoop = false;
    bool subscribeResult = true;
    int connectCalls = 0, loopCalls = 0, disconnectCalls = 0;
    std::function<void()> beforeConnect, beforeLoop;
    std::function<void(char *, byte *, unsigned int)> callback;
    Stream *output = nullptr;
    std::string host, lastCode, lastSubscription, reportPayload;
    explicit PubSubClient(WiFiClientSecure &) {}
    bool connected() { assert(mockTaskThread); return isConnected; }
    bool connect(const char *, const char *, const char *code) {
        assert(mockTaskThread);
        ++connectCalls;
        lastCode = code;
        if (beforeConnect) beforeConnect();
        return isConnected = connectResult;
    }
    int state() { assert(mockTaskThread); return isConnected ? 0 : -2; }
    void disconnect() { assert(mockTaskThread); ++disconnectCalls; isConnected = false; }
    void loop() {
        assert(mockTaskThread);
        ++loopCalls;
        if (beforeLoop) beforeLoop();
        if (dropDuringLoop) isConnected = false;
        if (isConnected && !reportPayload.empty()) {
            for (auto c : reportPayload) output->write(static_cast<uint8_t>(c));
            char topic[] = "device/test/report";
            callback(topic, nullptr, reportPayload.size());
            reportPayload.clear();
        }
    }
    bool subscribe(const char *topic) { assert(mockTaskThread); lastSubscription = topic; return subscribeResult; }
    void setBufferSize(int) { assert(mockTaskThread); }
    void setServer(const char *ip, int) { assert(mockTaskThread); host = ip; }
    void setStream(Stream &stream) { assert(mockTaskThread); output = &stream; }
    void setCallback(decltype(callback) handler) { assert(mockTaskThread); callback = handler; }
    void setSocketTimeout(int) { assert(mockTaskThread); }
};
inline bool failOpen=false, failRename=false, shortRead=false;
inline size_t writeBudget = SIZE_MAX;
class File {
    std::shared_ptr<std::string> contents;
    size_t cursor=0;
public:
    File() = default;
    File(std::shared_ptr<std::string> p) : contents(p) {}
    explicit operator bool() const { return bool(contents); }
    size_t size() { return contents->size(); }
    size_t readBytes(char* p,size_t n) { n=std::min(n,size()-cursor); if (shortRead && n) --n; memcpy(p,contents->data()+cursor,n); cursor+=n; return n; }
    size_t write(const uint8_t* p,size_t n) { n=std::min(n,writeBudget); contents->append((const char*)p,n); writeBudget-=n; return n; }
    size_t write(uint8_t c) { return write(&c,1); }
    void close() { contents.reset(); }
};
struct LittleFSMock {
    bool mountResult = true;
    std::map<std::string,std::shared_ptr<std::string>> files;
    File open(const char* path,const char* mode) {
        if (*mode=='w') {
            if (failOpen) return {};
            files[path]=std::make_shared<std::string>();
        }
        auto it=files.find(path);
        return it==files.end() ? File() : File(it->second);
    }
    bool rename(const char* from,const char* to) {
        if (failRename || !files.count(from)) return false;
        files[to]=files[from]; files.erase(from); return true;
    }
    bool remove(const char* p) { return files.erase(p)>0; }
    bool exists(const char* p) { return files.count(p)>0; }
    bool begin() { return mountResult; }
    void format() { files.clear(); }
} LittleFS;
constexpr int HTTP_POST=1, HTTP_GET=2;
constexpr int UPLOAD_FILE_START=1, UPLOAD_FILE_WRITE=2, UPLOAD_FILE_END=3;
constexpr size_t UPDATE_SIZE_UNKNOWN=0;
struct HTTPUpload { int status=0; String filename; uint8_t* buf=nullptr; size_t currentSize=0,totalSize=0; };
struct WebServer {
    std::map<std::string,String> params;
    std::map<std::string,String> headers;
    std::vector<int> responses;
    WebServer(int) {}
    int args() { return params.size(); }
    String arg(const char* key) { auto it=params.find(key); return it==params.end() ? String() : it->second; }
    bool hasArg(const char* key) { return params.count(key); }
    String lastBody;
    void send(int status,const char* = "",const String& body = "") { responses.push_back(status); lastBody=body; }
    void sendHeader(const String &name, const String &value, bool = false) { headers[name] = value; }
    int method() { return HTTP_GET; }
    String uri() { return "/unknown"; }
    void send_P(int status, const char *, const char *body, int length) {
        responses.push_back(status); lastBody.assign(body, length);
    }
    template<class... T> void on(T...) {}
    template<class T> void onNotFound(T) {}
    void requestAuthentication() {}
    void begin() {}
    std::function<void()> onHandleClient;
    void handleClient() { if (onHandleClient) onHandleClient(); }
    HTTPUpload& upload() { static HTTPUpload u; return u; }
};
struct MDNSMock { bool begin(const char*) { return true; } } MDNS;
struct UpdateMock {
    bool hasError() { return false; }
    bool begin(size_t) { return true; }
    size_t write(uint8_t*,size_t n) { return n; }
    bool end(bool) { return true; }
    template<class T> void printError(T&) {}
} Update;

constexpr int OUTPUT=1;
constexpr int HIGH=1, LOW=0;
inline std::map<int,int> pwmValues;
inline std::map<int,int> digitalValues;
inline void pinMode(int,int) {}
inline void digitalWrite(int pin,int level) { digitalValues[pin]=level; }
inline void analogWrite(int pin,int duty) { pwmValues[pin]=duty; }
inline long map(long value,long a,long b,long c,long d) { return (value-a)*(d-c)/(b-a)+c; }
