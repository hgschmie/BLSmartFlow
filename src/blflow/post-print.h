#ifndef BLFLOW_POST_PRINT_H
#define BLFLOW_POST_PRINT_H

#include "types.h"

inline void printerCommunicationLost() {
    printerVariables.online = false;
    printerVariables.printWasRunning = false;
    for (auto &run : printerVariables.postPrint) run = PostPrintRun{};
}

inline void startPostPrint(int fan, const PostPrintSettings &settings, uint32_t now) {
    auto &run = printerVariables.postPrint[fan];
    run = PostPrintRun{};
    if (!settings.enabled) return;
    run.active = true;
    run.started = now;
    run.durationMs = static_cast<uint32_t>(settings.seconds) * 1000;
    run.speed = settings.speed;
}

inline bool recordPrintState(const char *state) {
    if (!state || !printerVariables.online) return false;
    if (strcmp(state, "RUNNING") == 0) {
        printerVariables.printWasRunning = true;
        return true;
    }
    if (strcmp(state, "FINISH") != 0 && strcmp(state, "FAILED") != 0 && strcmp(state, "IDLE") != 0) {
        // PREPARE, PAUSE and unrelated reports neither trigger nor cancel a timer.
        return false;
    }
    if (printerVariables.printWasRunning) {
        const uint32_t now = millis();
        startPostPrint(0, printerConfig.postPrint, now);
        if (!printerConfig.fansSynchronized) startPostPrint(1, printerConfig.postPrint2, now);
    }
    // Consume this job's end once, including FINISH followed by IDLE.
    printerVariables.printWasRunning = false;
    return true;
}

inline void updatePostPrint(uint32_t now) {
    if (!printerVariables.online) {
        printerCommunicationLost();
        return;
    }
    for (int fan = 0; fan < 2; ++fan) {
        auto &run = printerVariables.postPrint[fan];
        const auto &settings = fan == 0 ? printerConfig.postPrint : printerConfig.postPrint2;
        if (!settings.enabled || (fan == 1 && printerConfig.fansSynchronized) ||
            static_cast<uint32_t>(now - run.started) >= run.durationMs) run.active = false;
    }
}

inline uint32_t postPrintSecondsRemaining(int fan, uint32_t now) {
    updatePostPrint(now);
    const auto &run = printerVariables.postPrint[printerConfig.fansSynchronized ? 0 : fan];
    if (!run.active) return 0;
    return (run.durationMs - static_cast<uint32_t>(now - run.started) + 999) / 1000;
}

inline bool samePostPrintSettings(const PostPrintSettings &a, const PostPrintSettings &b) {
    return a.enabled == b.enabled && a.seconds == b.seconds && a.speed == b.speed;
}

inline void cancelChangedPostPrint(const PrinterConfig &before, const PrinterConfig &after) {
    const bool syncChanged = before.fansSynchronized != after.fansSynchronized;
    if (syncChanged || !samePostPrintSettings(before.postPrint, after.postPrint))
        printerVariables.postPrint[0] = PostPrintRun{};
    if (syncChanged || !samePostPrintSettings(before.postPrint2, after.postPrint2))
        printerVariables.postPrint[1] = PostPrintRun{};
}

#endif
