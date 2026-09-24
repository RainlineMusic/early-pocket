#pragma once

#include "EarlyModel.h"
#include <array>

namespace early {

enum class Confidence { weak, fair, good };

struct TargetSummary {
    TapModel taps;
    std::array<float, 3> toneDb{};
    float stereoWidth = 0.0f;
    bool stereoMeasured = false;
    Confidence confidence = Confidence::weak;
    int onsetCount = 0;
};

struct FitResult {
    Parameters parameters;
    std::array<float, 3> eqGainDb{};
    float error = 1.0e6f;
    bool valid = false;
};

class LearnAnalyzer {
public:
    TargetSummary analyze(const float* left, const float* right, int samples, double sampleRate) const;
    FitResult fit(const TargetSummary&) const;
};

} // namespace early
