#ifndef BLFLOW_FAN_CURVE_H
#define BLFLOW_FAN_CURVE_H

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

using FanCurve = std::vector<std::pair<float, int>>;

inline FanCurve defaultFanCurve() {
    return {{0, 0}, {50, 0}, {180, 50}, {245, 80}, {350, 100}, {400, 100}};
}

inline int clampFanSpeed(int speed) {
    return std::max(0, std::min(100, speed));
}

inline bool normalizeFanCurve(FanCurve &curve) {
    if (curve.empty()) return false;
    for (const auto &point : curve) {
        if (!std::isfinite(point.first) || point.first < 0 || point.first > 400 ||
            point.second < 0 || point.second > 100) return false;
    }
    std::sort(curve.begin(), curve.end());
    for (size_t i = 1; i < curve.size(); ++i) {
        if (curve[i - 1].first == curve[i].first) return false;
    }
    return true;
}

inline int calculateFanSpeed(const FanCurve &curve, float temperature) {
    if (curve.empty() || !std::isfinite(temperature)) return 0;
    if (temperature <= curve.front().first) return clampFanSpeed(curve.front().second);
    for (size_t i = 1; i < curve.size(); ++i) {
        if (temperature <= curve[i].first) {
            const auto &a = curve[i - 1];
            const auto &b = curve[i];
            if (b.first <= a.first) return clampFanSpeed(b.second);
            return clampFanSpeed(a.second + (temperature - a.first) *
                                 (b.second - a.second) / (b.first - a.first));
        }
    }
    return clampFanSpeed(curve.back().second);
}

#endif
