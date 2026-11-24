#ifndef _TYPES
#define _TYPES

#include <Arduino.h>
#include "fan-curve.h"
#include "printer-fan.h"
#include "post-print-types.h"

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct PrinterVaraiblesStruct{
        PrinterFanReading fans[printerFanCount];
        double nozzletemp = 0;
        bool online = false;
        bool printWasRunning = false;
        PostPrintRun postPrint[2];
        String errorcode = "";
        //Time since
        unsigned long disconnectMQTTms = 0;        
    } PrinterVariables;
    PrinterVariables printerVariables;

    typedef struct GlobalVariablesStruct{
        char SSID[33]; // Up to 32 bytes plus terminator.
        char APPW[65]; // Up to 63 password bytes or a 64-digit hexadecimal PSK.
        String FWVersion = "Stable " STRVERSION " (" STRBUILDDATE ")";
        String Host = "BLFLOW";
        bool started = false;
        int fanSpeed = 0;
        int fanSpeed2 = 0;
    } GlobalVariables;
    GlobalVariables globalVariables;

    typedef struct PrinterConfigStruct
    {
        char printerIP[16];             //BBLP IP Address - used for MQTT reports
        char accessCode[9];             //BBLP Access Code - used for MQTT reports
        char serialNumber[16];          //BBLP Serial Number - used for MQTT reports

        char BSSID[18];                 // Legacy field; connections now select an AP by SSID.
        bool rescanWiFiNetwork = false; // Request a fresh connection by SSID.

        // Debugging
        bool debuging = true;          // Master switch for application debug logging.
        bool debugingchange = true;    // Fan output/mode changes; requires debuging.
        bool mqttdebug = false;        // Filtered printer reports; requires debuging.

        bool fansSynchronized = true;
        bool staticFan = false;
        int staticFanSpeed = 0;
        bool followPrinterFan = false;
        PrinterFanSource printerFanSource = PrinterFanSource::Chamber;
        bool staticFan2 = false;
        int staticFanSpeed2 = 0;
        bool followPrinterFan2 = false;
        PrinterFanSource printerFanSource2 = PrinterFanSource::Chamber;
        PostPrintSettings postPrint;
        PostPrintSettings postPrint2;

        FanCurve fanGraph = defaultFanCurve();
        // Empty until independent settings are first saved; then retained in sync mode.
        FanCurve fanGraph2;
        FanCurve followGraph = defaultFollowFanCurve();
        FanCurve followGraph2 = defaultFollowFanCurve();

    } PrinterConfig;
    PrinterConfig printerConfig;

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif
