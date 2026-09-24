#include "LearnAnalyzer.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace early {
namespace {

constexpr float pi = 3.14159265358979323846f;

struct Peak {
    float timeMs = 0.0f;
    float gain = 0.0f;
};

} // namespace

TargetSummary LearnAnalyzer::analyze(const float* left, const float* right, int samples, double sampleRate) const {
    TargetSummary result;
    if (left == nullptr || samples <= 0 || sampleRate < 8000.0) return result;

    const int frameSize = std::max(1, int(std::lround(sampleRate * 0.001)));
    const int frameCount = samples / frameSize;
    if (frameCount < 250) return result;

    std::vector<float> envelope(size_t(frameCount), 0.0f);
    std::vector<float> frameMid(size_t(frameCount), 0.0f);
    std::vector<float> frameSide(size_t(frameCount), 0.0f);
    float globalPeak = 0.0f;
    for (int frame = 0; frame < frameCount; ++frame) {
        const int begin = frame * frameSize;
        const int end = std::min(samples, begin + frameSize);
        double energy = 0.0, midEnergy = 0.0, sideEnergy = 0.0;
        for (int i = begin; i < end; ++i) {
            const float l = left[i];
            const float r = right != nullptr ? right[i] : l;
            const float mid = 0.5f * (l + r);
            const float side = 0.5f * (l - r);
            energy += 0.5 * (double(l) * l + double(r) * r);
            midEnergy += double(mid) * mid;
            sideEnergy += double(side) * side;
        }
        const double length = double(std::max(1, end - begin));
        envelope[size_t(frame)] = float(std::sqrt(energy / length));
        frameMid[size_t(frame)] = float(std::sqrt(midEnergy / length));
        frameSide[size_t(frame)] = float(std::sqrt(sideEnergy / length));
        globalPeak = std::max(globalPeak, envelope[size_t(frame)]);
    }
    if (globalPeak < 1.0e-5f) return result;

    // Detect rising energy windows and enforce a short refractory period so a
    // single transient is not counted more than once.
    std::vector<int> onsets;
    const int history = 12;
    const int refractory = std::max(35, int(std::lround(sampleRate * 0.070 / frameSize)));
    int lastOnset = -refractory;
    for (int i = history; i < frameCount - 4; ++i) {
        float prior = 0.0f;
        for (int j = i - history; j < i; ++j) prior += envelope[size_t(j)];
        prior /= float(history);
        const float current = envelope[size_t(i)];
        const float threshold = std::max(globalPeak * 0.012f, prior * 1.9f);
        if (i - lastOnset >= refractory && current >= threshold && current > envelope[size_t(i - 1)] && current >= envelope[size_t(i + 1)]) {
            onsets.push_back(i);
            lastOnset = i;
        }
    }
    result.onsetCount = int(onsets.size());
    if (onsets.empty()) return result;

    constexpr int profileMs = 260;
    std::array<double, profileMs> profileSum{};
    std::array<int, profileMs> profileCount{};
    double midEnergy = 0.0, sideEnergy = 0.0;
    int stereoFrames = 0;
    for (const int onset : onsets) {
        double direct = 0.0;
        int directCount = 0;
        for (int k = 0; k < 6 && onset + k < frameCount; ++k) {
            direct += envelope[size_t(onset + k)];
            ++directCount;
        }
        const float directLevel = directCount > 0 ? float(direct / double(directCount)) : 0.0f;
        if (directLevel < globalPeak * 0.008f) continue;

        for (int k = 0; k < profileMs && onset + k < frameCount; ++k) {
            const double normalized = double(envelope[size_t(onset + k)]) / double(std::max(1.0e-6f, directLevel));
            profileSum[size_t(k)] += std::min(3.0, normalized);
            ++profileCount[size_t(k)];
            if (right != nullptr && k >= 2 && k < 220) {
                const double m = frameMid[size_t(onset + k)];
                const double s = frameSide[size_t(onset + k)];
                midEnergy += m * m;
                sideEnergy += s * s;
                ++stereoFrames;
            }
        }
    }

    std::array<float, profileMs> profile{};
    for (int k = 0; k < profileMs; ++k) {
        if (profileCount[size_t(k)] > 0)
            profile[size_t(k)] = float(profileSum[size_t(k)] / double(profileCount[size_t(k)]));
    }
    std::array<float, profileMs> smooth{};
    for (int k = 0; k < profileMs; ++k) {
        float sum = 0.0f;
        int count = 0;
        for (int j = std::max(0, k - 2); j <= std::min(profileMs - 1, k + 2); ++j) {
            sum += profile[size_t(j)];
            ++count;
        }
        smooth[size_t(k)] = count > 0 ? sum / float(count) : 0.0f;
    }

    std::vector<Peak> candidates;
    for (int k = 8; k < profileMs - 8; ++k) {
        if (profileCount[size_t(k)] < std::max(1, int(onsets.size() / 3))) continue;
        const float value = smooth[size_t(k)];
        const float shoulder = 0.5f * (smooth[size_t(k - 8)] + smooth[size_t(k + 8)]);
        if (value >= smooth[size_t(k - 1)] && value > smooth[size_t(k + 1)]
            && value > std::max(0.10f, shoulder * 1.22f)
            && value - shoulder > 0.045f) {
            candidates.push_back({float(k), std::clamp(value, 0.01f, 1.0f)});
        }
    }

    // Keep the most prominent peaks, then restore chronological order.
    std::sort(candidates.begin(), candidates.end(), [](const Peak& a, const Peak& b) { return a.gain > b.gain; });
    std::vector<Peak> selected;
    for (const auto& candidate : candidates) {
        const bool tooClose = std::any_of(selected.begin(), selected.end(), [&](const Peak& p) {
            return std::abs(p.timeMs - candidate.timeMs) < 5.0f;
        });
        if (!tooClose) selected.push_back(candidate);
        if (selected.size() >= size_t(maxTaps)) break;
    }
    std::sort(selected.begin(), selected.end(), [](const Peak& a, const Peak& b) { return a.timeMs < b.timeMs; });
    result.taps.count = int(selected.size());
    for (int i = 0; i < result.taps.count; ++i) {
        const auto& peak = selected[size_t(i)];
        result.taps.taps[size_t(i)] = {peak.timeMs, peak.gain, 0.0f, i};
    }

    if (right != nullptr && stereoFrames > 0) {
        const double mid = std::sqrt(midEnergy / double(stereoFrames));
        const double side = std::sqrt(sideEnergy / double(stereoFrames));
        result.stereoMeasured = true;
        result.stereoWidth = float(std::clamp(2.0 * side / std::max(1.0e-9, mid + side), 0.0, 2.0));
    }

    // Broad low/mid/high balance is used only for a small corrective EQ move.
    const float lowAlpha = 1.0f - std::exp(-2.0f * pi * 250.0f / float(sampleRate));
    const float highAlpha = 1.0f - std::exp(-2.0f * pi * 2500.0f / float(sampleRate));
    float lowState = 0.0f, highState = 0.0f;
    double bandEnergy[3]{};
    for (int i = 0; i < samples; ++i) {
        const float x = right != nullptr ? 0.5f * (left[i] + right[i]) : left[i];
        lowState += lowAlpha * (x - lowState);
        highState += highAlpha * (x - highState);
        const float band[3]{lowState, highState - lowState, x - highState};
        for (int b = 0; b < 3; ++b) bandEnergy[b] += double(band[b]) * band[b];
    }
    const double low = std::sqrt(bandEnergy[0] / double(samples));
    const double mid = std::sqrt(bandEnergy[1] / double(samples));
    const double high = std::sqrt(bandEnergy[2] / double(samples));
    const auto toDb = [](double a, double b) {
        return float(20.0 * std::log10(std::max(1.0e-8, a) / std::max(1.0e-8, b)));
    };
    result.toneDb = {toDb(low, mid), 0.0f, toDb(high, mid)};

    if (result.taps.count >= 3 && result.onsetCount >= 8)
        result.confidence = Confidence::good;
    else if (result.taps.count >= 2 && result.onsetCount >= 3)
        result.confidence = Confidence::fair;
    else
        result.confidence = Confidence::weak;
    return result;
}

FitResult LearnAnalyzer::fit(const TargetSummary& target) const {
    FitResult best;
    if (target.taps.count < 2 || target.confidence == Confidence::weak) return best;

    float bestError = std::numeric_limits<float>::max();
    Parameters candidate;
    for (int faces = 1; faces <= 16; ++faces) {
        candidate.faces = faces;
        for (int shapeIndex = 0; shapeIndex <= 10; ++shapeIndex) {
            candidate.roomShape = float(shapeIndex) / 10.0f;
            const int available = availableTapCount(candidate.roomShape);
            for (int sizeIndex = 0; sizeIndex <= 20; ++sizeIndex) {
                candidate.roomSize = float(sizeIndex) / 20.0f;
                for (int count = 1; count <= available; ++count) {
                    candidate.count = count;
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
    }

    best.parameters.width = target.stereoMeasured ? std::clamp(target.stereoWidth, 0.0f, 2.0f) : 1.0f;
    best.eqGainDb = {
        std::clamp(-0.25f * target.toneDb[0], -3.0f, 3.0f),
        std::clamp(-0.10f * target.toneDb[1], -3.0f, 3.0f),
        std::clamp(-0.25f * target.toneDb[2], -3.0f, 3.0f)
    };
    best.error = bestError;
    best.valid = std::isfinite(bestError) && bestError < 2.0f;
    return best;
}

} // namespace early
