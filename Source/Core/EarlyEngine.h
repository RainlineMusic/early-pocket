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

    double rate = 48000.0;
    std::vector<std::array<float, 2>> delay;
    int write = 0, fadeRemaining = 0, fadeLength = 1;
    TapModel current{}, previous{};
    std::array<StereoFilter, 5> filters{};
    float bypassMix = 0.0f;

    std::array<float, 2> render(const TapModel&) const noexcept;
    void updateFilter(int index, float frequency, float q, float gainDb, int type);
};

} // namespace early
