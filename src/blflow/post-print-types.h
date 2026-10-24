#ifndef BLFLOW_POST_PRINT_TYPES_H
#define BLFLOW_POST_PRINT_TYPES_H

#include <cstdint>

constexpr int postPrintMaxSeconds = 86400;

struct PostPrintSettings {
    bool enabled = false;
    int seconds = 600;
    int speed = 100;
};

struct PostPrintRun {
    bool active = false;
    uint32_t started = 0;
    uint32_t durationMs = 0;
    int speed = 0;
};

#endif
