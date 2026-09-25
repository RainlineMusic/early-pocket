#pragma once

#include <array>
#include <cstdint>

namespace early {

constexpr int maxTaps = 80;

struct Parameters {
    float roomSize = 0.45f;
    float roomShape = 0.20f;
    int faces = 6;
    float width = 1.0f;
    float pattern = 0.35f; // UI label: Distance
};

struct Tap {
    float delayMs = 0.0f;
    float gain = 0.0f;
    float pan = 0.0f;
    int pathId = 0;
    float lowGain = 1.0f;
    float highGain = 1.0f;
    float diffusionMs = 0.0f;
    float stereoSpread = 0.16f;
    float decorrelationMs = 0.0f;
};

struct TapModel {
    std::array<Tap, maxTaps> taps{};
    int count = 0;
    std::uint64_t fingerprint = 0;
};

TapModel buildModel(const Parameters&) noexcept;
TapModel transformLearnedModel(const TapModel& learned,
                               const Parameters& current,
                               const Parameters& learnedReference,
                               float learnedHighToneDb = 0.0f) noexcept;
float modelDistance(const TapModel& target, const TapModel& candidate) noexcept;

} // namespace early
