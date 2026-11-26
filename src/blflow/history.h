#pragma once

#include <cstdint>

// All three series share each slot. The bucket number distinguishes unwritten
// intervals from old slots after a long pause or a ring wrap.
struct HistoryBucket {
    uint32_t number;
    float temperatureSum;
    uint16_t fan1Sum, fan2Sum, count;
    bool temperatureMissing;
};

struct HistoryRange {
    uint16_t offset, capacity, seconds;
    const char *name;
};

constexpr HistoryRange historyRanges[] = {
    {0,               300,   1, "5m"},
    {300,             240,  15, "1h"},
    {300 + 240,       360,  60, "6h"},
    {300 + 240 + 360, 288, 300, "24h"}
};

struct TelemetryHistory {
    HistoryBucket slots[300 + 240 + 360 + 288]{};
    uint32_t latest = 0;
    bool started = false;

    void sample(uint32_t time, bool known, double temperature, int fan1, int fan2) {
        if (started && time == latest) return;
        latest = time;
        started = true;
        for (const auto &range : historyRanges) {
            const uint32_t number = time / range.seconds;
            auto &bucket = slots[range.offset + number % range.capacity];
            if (!bucket.count || bucket.number != number) {
                bucket = {};
                bucket.number = number;
            }
            if (known) bucket.temperatureSum += temperature;
            else bucket.temperatureMissing = true;
            // The longest bucket contains at most 300 readings of at most 100%.
            bucket.fan1Sum += fan1;
            bucket.fan2Sum += fan2;
            ++bucket.count;
        }
    }
};
static_assert(sizeof(TelemetryHistory) <= 24 * 1024, "Dashboard history RAM budget");
TelemetryHistory telemetryHistory;
