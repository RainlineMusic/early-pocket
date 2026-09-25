#pragma once

#include "EarlyModel.h"
#include <array>
#include <vector>

namespace early {

struct EqSettings {
    float highPassHz = 20.0f;
    float lowPassHz = 20000.0f;
    std::array<float, 3> frequency{180.0f, 900.0f, 4500.0f};
    std::array<float, 3> gainDb{};
    std::array<float, 3> q{0.7f, 0.7f, 0.7f};
};

class Engine {
public:
    void prepare(double sampleRate, int maximumDelayMs = 320);
    void reset();
    void setModel(const TapModel&);
    void setEq(const EqSettings&);
    std::array<float, 2> process(float left, float right, float mix, bool bypass) noexcept;
    int tailSamples() const noexcept;

private:
    struct Biquad {
        float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
        float z1 = 0.0f, z2 = 0.0f;
        float tick(float x) noexcept;
    };
    struct StereoFilter { Biquad l, r; };

    struct TapState { float lowL = 0.0f, lowR = 0.0f;
                      float apInL = 0.0f, apInR = 0.0f, apOutL = 0.0f, apOutR = 0.0f; };
    struct DelayRead {
        int whole = 1;
        float cm1 = 0.0f, c0 = 1.0f, c1 = 0.0f, c2 = 0.0f;
    };
    struct RenderTap {
        std::array<DelayRead, 7> delayL{};
        std::array<DelayRead, 7> delayR{};
        std::array<float, 7> weights{};
        int clusterCount = 1;
        float gain = 0.0f;
        float lowGain = 1.0f, highGain = 1.0f, lowAlpha = 0.0f;
        float allpass = 0.0f;
        bool dispersive = false;
        float gLL = 1.0f, gLR = 0.0f, gRL = 0.0f, gRR = 1.0f;
    };
    struct RenderModel {
        std::array<RenderTap, maxTaps> taps{};
        int count = 0;
        std::uint64_t fingerprint = 0;
        float tailMs = 0.0f;
    };

    double rate = 48000.0;
    std::vector<std::array<float, 2>> delay;
    int write = 0, fadeRemaining = 0, fadeLength = 1;
    RenderModel current{}, previous{};
    std::array<TapState, maxTaps> currentState{}, previousState{};

    std::array<StereoFilter, 5> filters{}, previousFilters{};
    int eqFadeRemaining = 0, eqFadeLength = 1;
    bool eqInitialised = false;

    float bypassMix = 0.0f;
    float mixSmoothed = 0.0f, mixSmoothingCoeff = 0.0f;
    bool mixInitialised = false;

    float readDelay(int channel, const DelayRead&) const noexcept;
    DelayRead makeDelayRead(float delaySamples) const noexcept;
    RenderModel makeRenderModel(const TapModel&) const noexcept;
    std::array<float, 2> render(const RenderModel&, std::array<TapState, maxTaps>&) noexcept;
    static std::array<float, 2> applyFilters(std::array<StereoFilter, 5>&,
                                              std::array<float, 2>) noexcept;
    void updateFilter(std::array<StereoFilter, 5>& bank, int index,
                      float frequency, float q, float gainDb, int type);
};

} // namespace early
