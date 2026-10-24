#ifndef BLFLOW_PRINTER_FAN_H
#define BLFLOW_PRINTER_FAN_H

#include <cstring>

enum class PrinterFanSource { Part, Auxiliary, Chamber, Heatbreak };
constexpr int printerFanCount = 4;

inline const char *printerFanSourceName(PrinterFanSource source) {
    switch (source) {
        case PrinterFanSource::Part: return "part";
        case PrinterFanSource::Auxiliary: return "auxiliary";
        case PrinterFanSource::Chamber: return "chamber";
        case PrinterFanSource::Heatbreak: return "heatbreak";
    }
    return "chamber";
}

inline bool readPrinterFanSource(const char *name, PrinterFanSource &source) {
    if (!name) return false;
    for (int i = 0; i < printerFanCount; ++i) {
        const auto candidate = static_cast<PrinterFanSource>(i);
        if (strcmp(name, printerFanSourceName(candidate)) == 0) {
            source = candidate;
            return true;
        }
    }
    return false;
}

struct PrinterFanReading {
    int percent = 0;
    bool known = false;
};

#endif
