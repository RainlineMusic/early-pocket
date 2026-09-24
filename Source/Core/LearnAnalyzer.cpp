#include "LearnAnalyzer.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace early {
namespace {
constexpr float pi = 3.14159265358979323846f;
constexpr int profileMs = 260;
struct Peak { float timeMs = 0.0f, gain = 0.0f; };
float midSample(const float* left, const float* right, int i) noexcept {
    return right != nullptr ? 0.5f * (left[i] + right[i]) : left[i];
}
}

TargetSummary LearnAnalyzer::analyze(const float* left, const float* right, int samples, double sampleRate) const {
    TargetSummary result;
    if (left == nullptr || samples <= 0 || sampleRate < 8000.0) return result;
    const int frameSize = std::max(1, int(std::lround(sampleRate * 0.001)));
    const int frameCount = samples / frameSize;
    if (frameCount < 250) return result;

    std::vector<float> envelope(size_t(frameCount), 0.0f);
    float globalPeak = 0.0f;
    for (int frame = 0; frame < frameCount; ++frame) {
        double energy = 0.0;
        const int begin = frame * frameSize;
        for (int i = begin; i < begin + frameSize; ++i) {
            const float x = midSample(left, right, i);
            energy += double(x) * x;
        }
        envelope[size_t(frame)] = float(std::sqrt(energy / frameSize));
        globalPeak = std::max(globalPeak, envelope[size_t(frame)]);
    }
    if (globalPeak < 1.0e-5f) return result;

    // Use only strong attacks as anchors. A long refractory period prevents an
    // early reflection from being mistaken for a second source attack.
    std::vector<int> onsets;
    constexpr int history = 12;
    constexpr int refractory = 250;
    int lastOnset = -refractory;
    for (int i = history; i < frameCount - profileMs; ++i) {
        float prior = 0.0f;
        for (int j = i - history; j < i; ++j) prior += envelope[size_t(j)];
        prior /= float(history);
        const float current = envelope[size_t(i)];
        if (i - lastOnset >= refractory && current > globalPeak * 0.025f
            && current > prior * 1.55f && current > envelope[size_t(i - 1)]
            && current >= envelope[size_t(i + 1)]) {
            onsets.push_back(i);
            lastOnset = i;
            if (onsets.size() == 32) break;
        }
    }
    result.onsetCount = int(onsets.size());
    if (onsets.empty()) return result;

    // Delayed copies of a short transient retain waveform similarity even when
    // their overall envelope is obscured by the continuing source sound.
    std::array<double, profileMs> scoreSum{};
    int usableOnsets = 0;
    const int templateLength = std::clamp(int(std::lround(sampleRate * 0.004)), 64, 512);
    for (const int onset : onsets) {
        const int anchor = onset * frameSize - frameSize;
        if (anchor < 0 || anchor + int(sampleRate * 0.262) + templateLength >= samples) continue;
        double directEnergy = 0.0;
        for (int n = 0; n < templateLength; ++n) {
            const float x = midSample(left, right, anchor + n);
            directEnergy += double(x) * x;
        }
        if (directEnergy < 1.0e-10) continue;
        ++usableOnsets;
        for (int lagMs = 7; lagMs < profileMs; ++lagMs) {
            float bestScore = 0.0f;
            const int nominalLag = int(std::lround(sampleRate * lagMs * 0.001));
            const int shiftStep = std::max(1, frameSize / 24);
            for (int shift = -frameSize; shift <= frameSize; shift += shiftStep) {
                const int candidate = anchor + nominalLag + shift;
                if (candidate < 0 || candidate + templateLength > samples) continue;
                double dot = 0.0, reflectedEnergy = 0.0;
                for (int n = 0; n < templateLength; ++n) {
                    const float a = midSample(left, right, anchor + n);
                    const float b = midSample(left, right, candidate + n);
                    dot += double(a) * b;
                    reflectedEnergy += double(b) * b;
                }
                const double correlation = std::abs(dot) / std::sqrt(std::max(1.0e-20, directEnergy * reflectedEnergy));
                const double level = std::min(1.0, std::sqrt(reflectedEnergy / directEnergy));
                bestScore = std::max(bestScore, float(correlation * level));
            }
            scoreSum[size_t(lagMs)] += bestScore;
        }
    }
    if (usableOnsets == 0) return result;

    std::array<float, profileMs> profile{}, smooth{};
    for (int k = 7; k < profileMs; ++k)
        profile[size_t(k)] = float(scoreSum[size_t(k)] / usableOnsets);
    for (int k = 8; k < profileMs - 1; ++k)
        smooth[size_t(k)] = (profile[size_t(k - 1)] + 2.0f * profile[size_t(k)] + profile[size_t(k + 1)]) * 0.25f;
    const float maximum = *std::max_element(smooth.begin() + 8, smooth.end() - 8);
    std::vector<Peak> candidates;
    for (int k = 9; k < profileMs - 8; ++k) {
        const float value = smooth[size_t(k)];
        const float shoulder = 0.5f * (smooth[size_t(k - 6)] + smooth[size_t(k + 6)]);
        if (value >= smooth[size_t(k - 1)] && value > smooth[size_t(k + 1)]
            && value >= std::max(0.075f, maximum * 0.16f)
            && value - shoulder >= 0.025f)
            candidates.push_back({float(k), value});
    }
    std::sort(candidates.begin(), candidates.end(), [](const Peak& a, const Peak& b) { return a.gain > b.gain; });
    std::vector<Peak> selected;
    for (const auto& candidate : candidates) {
        if (std::none_of(selected.begin(), selected.end(), [&](const Peak& p) {
            return std::abs(p.timeMs - candidate.timeMs) < 4.0f;
        })) selected.push_back(candidate);
        if (selected.size() >= 16) break;
    }
    std::sort(selected.begin(), selected.end(), [](const Peak& a, const Peak& b) { return a.timeMs < b.timeMs; });
    result.taps.count = int(selected.size());
    for (int i = 0; i < result.taps.count; ++i)
        result.taps.taps[size_t(i)] = {selected[size_t(i)].timeMs,
            std::clamp(selected[size_t(i)].gain, 0.01f, 1.0f), 0.0f, i};

    if (right != nullptr) {
        double midEnergy = 0.0, sideEnergy = 0.0;
        for (const int onset : onsets) {
            const int start = onset * frameSize;
            const int end = std::min(samples, start + int(sampleRate * 0.22));
            for (int i = start; i < end; ++i) {
                const double mid = 0.5 * (double(left[i]) + right[i]);
                const double side = 0.5 * (double(left[i]) - right[i]);
                midEnergy += mid * mid;
                sideEnergy += side * side;
            }
        }
        const double mid = std::sqrt(midEnergy), side = std::sqrt(sideEnergy);
        result.stereoMeasured = true;
        result.stereoWidth = float(std::clamp(2.0 * side / std::max(1.0e-9, mid + side), 0.0, 2.0));
    }

    // Compare the spectral fractions of the initial attack and the following
    // reflection field. This estimates coloration rather than source timbre.
    std::vector<unsigned char> region(size_t(samples), 0);
    for (const int onset : onsets) {
        const int start = onset * frameSize;
        const int directEnd = std::min(samples, start + int(sampleRate * 0.012));
        const int lateStart = std::min(samples, start + int(sampleRate * 0.020));
        const int lateEnd = std::min(samples, start + int(sampleRate * 0.220));
        for (int i = start; i < directEnd; ++i) region[size_t(i)] = 1;
        for (int i = lateStart; i < lateEnd; ++i) region[size_t(i)] = 2;
    }
    const float lowAlpha = 1.0f - std::exp(-2.0f * pi * 250.0f / float(sampleRate));
    const float highAlpha = 1.0f - std::exp(-2.0f * pi * 2500.0f / float(sampleRate));
    float lowState = 0.0f, highState = 0.0f;
    double bandEnergy[2][3]{};
    for (int i = 0; i < samples; ++i) {
        const float x = midSample(left, right, i);
        lowState += lowAlpha * (x - lowState);
        highState += highAlpha * (x - highState);
        const float bands[3]{lowState, highState - lowState, x - highState};
        if (const auto section = region[size_t(i)])
            for (int b = 0; b < 3; ++b) bandEnergy[section - 1][b] += double(bands[b]) * bands[b];
    }
    double directTotal = 0.0, lateTotal = 0.0;
    for (int b = 0; b < 3; ++b) {
        directTotal += bandEnergy[0][b];
        lateTotal += bandEnergy[1][b];
    }
    if (directTotal > 1.0e-12 && lateTotal > 1.0e-12)
        for (int b = 0; b < 3; ++b) {
            const double directFraction = bandEnergy[0][b] / directTotal;
            const double lateFraction = bandEnergy[1][b] / lateTotal;
            result.toneDb[size_t(b)] = float(10.0 * std::log10((lateFraction + 1.0e-9) / (directFraction + 1.0e-9)));
        }

    const float topScore = candidates.empty() ? 0.0f : candidates.front().gain;
    if (result.taps.count >= 2 && usableOnsets >= 3 && topScore >= 0.12f)
        result.confidence = Confidence::good;
    else if (result.taps.count >= 1 && topScore >= 0.16f)
        result.confidence = Confidence::fair;
    return result;
}

FitResult LearnAnalyzer::fit(const TargetSummary& target) const {
    FitResult best;
    if (target.taps.count < 1 || target.confidence == Confidence::weak) return best;
    float bestError = std::numeric_limits<float>::max();
    Parameters candidate;
    for (int faces = 1; faces <= 16; ++faces) {
        candidate.faces = faces;
        for (int shapeIndex = 0; shapeIndex <= 10; ++shapeIndex) {
            candidate.roomShape = float(shapeIndex) / 10.0f;
            for (int sizeIndex = 0; sizeIndex <= 20; ++sizeIndex) {
                candidate.roomSize = float(sizeIndex) / 20.0f;
                for (int patternIndex = 0; patternIndex <= 10; ++patternIndex) {
                    candidate.pattern = float(patternIndex) / 10.0f;
                    const float error = modelDistance(target.taps, buildModel(candidate));
                    if (error < bestError) {
                        bestError = error;
                        best.parameters = candidate;
                    }
                }
            }
        }
    }
    best.parameters.width = target.stereoMeasured ? std::clamp(target.stereoWidth, 0.0f, 2.0f) : 1.0f;
    for (int b = 0; b < 3; ++b)
        best.eqGainDb[size_t(b)] = std::clamp(0.35f * target.toneDb[size_t(b)], -6.0f, 6.0f);
    best.error = bestError;
    best.valid = std::isfinite(bestError) && bestError < 2.0f;
    return best;
}
} // namespace early
