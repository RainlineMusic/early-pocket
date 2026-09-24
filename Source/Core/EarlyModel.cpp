#include "EarlyModel.h"
#include <algorithm>
#include <cmath>

namespace early {
namespace {
constexpr std::array<float, maxTaps> baseTime{ 1.00f,1.37f,1.91f,2.42f,3.08f,3.83f,
  4.05f,4.28f,4.52f,4.76f,5.00f,5.22f,5.42f,5.62f,5.82f,6.00f,
  18.31f,20.45f,22.90f,25.47f,28.31f,31.56f,35.11f,39.04f };
constexpr std::array<float, maxTaps> irregular{ 0.00f,.19f,-.11f,.27f,-.21f,.13f,
  -.28f,.31f,-.17f,.24f,-.32f,.11f,.29f,-.23f,.17f,-.27f,.34f,-.12f,
  .21f,-.31f,.15f,.28f,-.20f,.09f };
constexpr std::array<float, maxTaps> pans{ -.62f,.58f,-.25f,.31f,-.83f,.79f,
  .12f,-.48f,.68f,-.72f,.42f,-.09f,.88f,-.36f,.24f,-.91f,.53f,-.57f,
  .76f,-.18f,.35f,-.69f,.94f,-.44f };

float clamp01(float v) { return std::clamp(v, 0.0f, 1.0f); }
std::uint64_t mixHash(std::uint64_t h, int v) { return (h ^ std::uint64_t(v + 0x9e3779b9)) * 1099511628211ull; }
}

TapModel buildModel(const Parameters& p) noexcept {
    TapModel out;
    const float size = clamp01(p.roomSize), shape = clamp01(p.roomShape);
    const float pattern = clamp01(p.pattern), width = std::clamp(p.width, 0.0f, 2.0f);
    const int faces = std::clamp(p.faces, 1, 16);
    out.count = faces;
    const float timeScale = 5.0f * std::pow(7.0f, size); // about 5..35 ms base scale
    const float baseEnd = timeScale * baseTime[5]
        * (1.0f + pattern * (0.16f + 0.018f * 5.0f))
        * (1.0f + shape * irregular[5]);
    const float remainingSpan = 300.0f - baseEnd;

    // Six paths form the rectangular room. Fewer faces remove the least
    // prominent of those paths; additional faces introduce new surfaces.
    std::array<int, maxTaps> ids{};
    for (int i = 0; i < 6; ++i) ids[size_t(i)] = i;
    const auto salience = [pattern](int i) {
        return std::exp(-0.075f * float(i)) * (1.0f + 0.16f * std::sin(float(i) * 2.31f + pattern));
    };
    // Stable insertion sort keeps this tiny fixed-size path allocation-free on
    // the audio thread. std::stable_sort is allowed to request a temp buffer.
    for (int i = 1; i < 6; ++i) {
        const int key = ids[size_t(i)];
        const float keyScore = salience(key);
        int j = i;
        while (j > 0 && keyScore > salience(ids[size_t(j - 1)])) {
            ids[size_t(j)] = ids[size_t(j - 1)];
            --j;
        }
        ids[size_t(j)] = key;
    }
    if (faces < 6) {
        std::sort(ids.begin(), ids.begin() + faces);
    } else {
        for (int i = 0; i < faces; ++i) ids[size_t(i)] = i;
    }

    float energy = 0.0f;
    for (int n = 0; n < out.count; ++n) {
        const int i = ids[size_t(n)];
        const float nearFar = 1.0f + pattern * (0.16f + 0.018f * float(i));
        const float shapeWarp = 1.0f + shape * irregular[size_t(i)];
        const float rawDelay = timeScale * baseTime[size_t(i)] * nearFar * shapeWarp;
        const float delay = i < 6 ? rawDelay
            : baseEnd + remainingSpan * std::tanh((rawDelay - baseEnd) / remainingSpan);
        const float envelope = std::pow(std::max(delay, 1.0f) / timeScale, -0.72f);
        const float accent = 0.84f + 0.16f * std::cos(float(i) * 1.73f + pattern * 2.0f);
        auto& t = out.taps[size_t(n)];
        t = { std::clamp(delay, 2.0f, 300.0f), envelope * accent,
              std::clamp(pans[size_t(i)] * width, -1.0f, 1.0f), i };
        energy += t.gain * t.gain;
    }
    // Removing reflections does not boost the remaining rectangular paths.
    if (faces < 6) {
        for (int i = 0; i < 6; ++i) {
            bool selected = false;
            for (int n = 0; n < faces; ++n) selected |= ids[size_t(n)] == i;
            if (selected) continue;
            const float nearFar = 1.0f + pattern * (0.16f + 0.018f * float(i));
            const float delay = timeScale * baseTime[size_t(i)] * nearFar
                * (1.0f + shape * irregular[size_t(i)]);
            const float envelope = std::pow(std::max(delay, 1.0f) / timeScale, -0.72f);
            const float accent = 0.84f + 0.16f * std::cos(float(i) * 1.73f + pattern * 2.0f);
            const float gain = envelope * accent;
            energy += gain * gain;
        }
    }
    const float norm = energy > 0.0f ? 0.72f / std::sqrt(energy) : 0.0f;
    std::uint64_t h = 1469598103934665603ull;
    for (int i = 0; i < out.count; ++i) {
        out.taps[size_t(i)].gain *= norm;
        const auto& t = out.taps[size_t(i)];
        h = mixHash(h, int(t.delayMs * 100.0f));
        h = mixHash(h, int(t.gain * 100000.0f));
        h = mixHash(h, int(t.pan * 10000.0f));
    }
    out.fingerprint = h;
    return out;
}

float modelDistance(const TapModel& target, const TapModel& candidate) noexcept {
    if (target.count == 0 || candidate.count == 0) return 1.0e6f;
    float cost = 0.25f * std::abs(target.count - candidate.count);
    std::array<bool, maxTaps> used{};
    for (int i = 0; i < target.count; ++i) {
        const auto& a = target.taps[size_t(i)];
        int best = -1; float bestCost = 1.0e6f;
        for (int j = 0; j < candidate.count; ++j) if (!used[size_t(j)]) {
            const auto& b = candidate.taps[size_t(j)];
            const float time = std::abs(std::log((a.delayMs + 1.0f) / (b.delayMs + 1.0f)));
            const float level = std::abs(a.gain - b.gain);
            const float c = 2.8f * time + 0.7f * level;
            if (c < bestCost) { bestCost = c; best = j; }
        }
        if (best >= 0) { used[size_t(best)] = true; cost += bestCost; } else cost += 1.0f;
    }
    return cost / float(std::max(target.count, candidate.count));
}
}
