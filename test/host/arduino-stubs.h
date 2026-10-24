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
#include <string>
#include <vector>
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
    template<class... T> void println(T...) {}
    template<class... T> void print(T...) {}
    template<class... T> void printf(T...) {}
    size_t write(uint8_t) { return 1; }
    size_t write(const uint8_t*, size_t n) { return n; }
} Serial;
struct ESPMock { void restart() {} } ESP;
inline uint32_t mockMillis = 0;
inline unsigned long millis() { return mockMillis; }
inline void delay(int) {}

// Connectivity substitutes exercise the real MQTT loop's fallback paths.
using byte = uint8_t;
constexpr int WL_CONNECTED = 3, HEX = 16;
inline long random(long maximum) { return maximum - 1; }
struct WiFiMock {
    int connectionStatus = WL_CONNECTED;
    int status() { return connectionStatus; }
} WiFi;
struct WiFiClientSecure { void setInsecure() {} };
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
    std::function<void()> beforeConnect;
    explicit PubSubClient(WiFiClientSecure &) {}
    bool connected() { return isConnected; }
    bool connect(const char *, const char *, const char *) {
        if (beforeConnect) beforeConnect();
        return isConnected = connectResult;
    }
    int state() { return isConnected ? 0 : -1; }
    void loop() { if (dropDuringLoop) isConnected = false; }
    template<class... T> void subscribe(T...) {}
    template<class... T> void setBufferSize(T...) {}
    template<class... T> void setServer(T...) {}
    template<class... T> void setStream(T...) {}
    template<class... T> void setCallback(T...) {}
    template<class... T> void setSocketTimeout(T...) {}
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
    bool begin() { return true; }
    void format() { files.clear(); }
} LittleFS;
constexpr int HTTP_POST=1, HTTP_GET=2;
constexpr int UPLOAD_FILE_START=1, UPLOAD_FILE_WRITE=2, UPLOAD_FILE_END=3;
constexpr size_t UPDATE_SIZE_UNKNOWN=0;
struct HTTPUpload { int status=0; String filename; uint8_t* buf=nullptr; size_t currentSize=0,totalSize=0; };
struct WebServer {
    std::map<std::string,String> params;
    std::vector<int> responses;
    WebServer(int) {}
    int args() { return params.size(); }
    String arg(const char* key) { auto it=params.find(key); return it==params.end() ? String() : it->second; }
    bool hasArg(const char* key) { return params.count(key); }
    String lastBody;
    void send(int status,const char* = "",const String& body = "") { responses.push_back(status); lastBody=body; }
    template<class... T> void sendHeader(T...) {}
    template<class... T> void send_P(T...) {}
    template<class... T> void on(T...) {}
    void requestAuthentication() {}
    void begin() {}
    void handleClient() {}
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
inline std::map<int,int> pwmValues;
inline void pinMode(int,int) {}
inline void analogWrite(int pin,int duty) { pwmValues[pin]=duty; }
inline long map(long value,long a,long b,long c,long d) { return (value-a)*(d-c)/(b-a)+c; }
