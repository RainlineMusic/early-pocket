#include "EarlyEngine.h"

#include <algorithm>
#include <cmath>

namespace early {
namespace {
constexpr float halfPi = 1.57079632679489661923f;
constexpr float invSqrt2 = 0.70710678118654752440f;
constexpr std::array<float, 7> clusterPosition{-1.0f, -0.56f, -0.22f, 0.0f, 0.29f, 0.61f, 1.0f};
constexpr std::array<float, 7> clusterWeight{0.12f, 0.22f, 0.38f, 1.0f, 0.34f, 0.20f, 0.10f};

int clusterSize(float diffusionMs) noexcept {
    if (diffusionMs <= 0.0f) return 1;
    if (diffusionMs < 0.18f) return 3;
    if (diffusionMs < 0.75f) return 5;
    return 7;
}

float channelPanGain(float pan, bool right) noexcept {
    const float angle = (std::clamp(pan, -1.0f, 1.0f) + 1.0f) * 0.25f * 3.14159265358979323846f;
    return right ? std::sin(angle) : std::cos(angle);
}
} // namespace

float Engine::Biquad::tick(float x) noexcept {
    const float y = b0*x + z1;
    z1 = b1*x - a1*y + z2;
    z2 = b2*x - a2*y;
    return y;
}

void Engine::prepare(double sr, int maximumDelayMs) {
    rate = std::max(1.0, sr);
    delay.assign(size_t(std::ceil(rate * maximumDelayMs * 0.001)) + 16u, {});
    fadeLength = std::max(1, int(std::lround(rate * 0.025)));
    eqFadeLength = std::max(1, int(std::lround(rate * 0.010)));
    mixSmoothingCoeff = float(std::exp(-1.0 / (rate * 0.008)));
    eqBypassCoeff = float(std::exp(-1.0 / (rate * 0.010)));
    reset();
    setEq({});
}

void Engine::reset() {
    std::fill(delay.begin(), delay.end(), std::array<float, 2>{});
    write = 0;
    fadeRemaining = 0;
    eqFadeRemaining = 0;
    bypassMix = 0.0f;
    eqBypassMix = 0.0f;
    mixSmoothed = 0.0f;
    mixInitialised = false;
    currentState = {};
    previousState = {};
    for (auto& f : filters) f.l.z1=f.l.z2=f.r.z1=f.r.z2=0.0f;
    for (auto& f : previousFilters) f.l.z1=f.l.z2=f.r.z1=f.r.z2=0.0f;
}

Engine::DelayRead Engine::makeDelayRead(float delaySamples) const noexcept {
    DelayRead d;
    if (delay.empty()) return d;
    const float maximum = float(std::max(2, int(delay.size()) - 5));
    const float value = std::clamp(delaySamples, 1.0f, maximum);
    d.whole = int(std::floor(value));
    const float t = value - float(d.whole);
    const float t2 = t*t, t3 = t2*t;
    d.cm1 = -0.5f*t + t2 - 0.5f*t3;
    d.c0  = 1.0f - 2.5f*t2 + 1.5f*t3;
    d.c1  = 0.5f*t + 2.0f*t2 - 1.5f*t3;
    d.c2  = -0.5f*t2 + 0.5f*t3;
    return d;
}

float Engine::readDelay(int channel, const DelayRead& d) const noexcept {
    const int n = int(delay.size());
    if (n <= 0) return 0.0f;
    auto wrap = [n](int i) noexcept { while (i < 0) i += n; while (i >= n) i -= n; return i; };
    const int i0 = wrap(write - d.whole);
    const int im1 = wrap(i0 + 1);
    const int i1 = wrap(i0 - 1);
    const int i2 = wrap(i0 - 2);
    return d.cm1*delay[size_t(im1)][size_t(channel)]
         + d.c0 *delay[size_t(i0 )][size_t(channel)]
         + d.c1 *delay[size_t(i1 )][size_t(channel)]
         + d.c2 *delay[size_t(i2 )][size_t(channel)];
}

Engine::RenderModel Engine::makeRenderModel(const TapModel& model) const noexcept {
    RenderModel out;
    out.count = std::clamp(model.count, 0, maxTaps);
    out.fingerprint = model.fingerprint;

    for (int i = 0; i < out.count; ++i) {
        const auto& source = model.taps[size_t(i)];
        auto& tap = out.taps[size_t(i)];
        tap.clusterCount = clusterSize(source.diffusionMs);
        tap.gain = source.gain;
        tap.lowGain = std::clamp(source.lowGain, 0.0f, 1.5f);
        tap.highGain = std::clamp(source.highGain, 0.0f, 1.5f);
        tap.lowAlpha = 1.0f - std::exp(-2.0f*3.14159265358979323846f*1850.0f/float(rate));
        tap.dispersive = source.pathId < 300;
        if (tap.dispersive) {
            const unsigned id = unsigned(source.pathId + 47);
            const unsigned hash = (id * 1664525u + 1013904223u) ^ (id * 2246822519u);
            const float hz = 480.0f + float(hash % 5500u);
            const float tangent = std::tan(3.14159265358979323846f * hz / float(rate));
            tap.allpass = (tangent - 1.0f) / (tangent + 1.0f);
        }

        const float spread = std::clamp(source.stereoSpread, 0.0f, 0.48f);
        const float panL = std::clamp(source.pan - spread, -1.0f, 1.0f);
        const float panR = std::clamp(source.pan + spread, -1.0f, 1.0f);
        tap.gLL = channelPanGain(panL, false);
        tap.gLR = channelPanGain(panL, true);
        tap.gRL = channelPanGain(panR, false);
        tap.gRR = channelPanGain(panR, true);

        std::array<float, 7> preparedWeights{};
        std::array<float, 7> preparedPositions{};
        float weightSum = 0.0f;
        const int first = (7 - tap.clusterCount) / 2;
        const int last = first + tap.clusterCount;
        int prepared = 0;
        for (int j = first; j < last; ++j, ++prepared) {
            const float phase = float((source.pathId + 19) * (j + 7));
            const float jitter = 0.075f * std::sin(phase * 1.61803398875f);
            const float weightJitter = 1.0f + 0.055f * std::sin(phase * 0.754877666f + 0.7f);
            preparedPositions[size_t(prepared)] = std::clamp(clusterPosition[size_t(j)] + jitter, -1.08f, 1.08f);
            preparedWeights[size_t(prepared)] = clusterWeight[size_t(j)] * weightJitter;
            weightSum += preparedWeights[size_t(prepared)];
        }
        const float normalise = weightSum > 0.0f ? 1.0f/weightSum : 1.0f;

        for (int target = 0; target < tap.clusterCount; ++target) {
            const float position = preparedPositions[size_t(target)];
            const float offset = position * source.diffusionMs;
            const float stereoOffset = source.decorrelationMs * position * 0.5f;
            tap.weights[size_t(target)] = preparedWeights[size_t(target)] * normalise;
            tap.delayL[size_t(target)] = makeDelayRead((source.delayMs + offset - stereoOffset) * float(rate) * 0.001f);
            tap.delayR[size_t(target)] = makeDelayRead((source.delayMs + offset + stereoOffset) * float(rate) * 0.001f);
        }
        out.tailMs = std::max(out.tailMs, source.delayMs + source.diffusionMs + source.decorrelationMs);
    }
    return out;
}

void Engine::setModel(const TapModel& m) {
    if (m.fingerprint == current.fingerprint && m.count == current.count) return;
    const RenderModel next = makeRenderModel(m);
    if (current.count == 0) {
        current = next;
        currentState = {};
        fadeRemaining = 0;
        return;
    }
    previous = current;
    previousState = currentState;
    current = next;
    currentState = {};
    fadeRemaining = fadeLength;
}

std::array<float,2> Engine::render(const RenderModel& m,
                                   std::array<TapState, maxTaps>& states) noexcept {
    std::array<float,2> wet{};
    if (delay.empty()) return wet;
    for (int i=0; i<m.count; ++i) {
        const auto& t = m.taps[size_t(i)];
        float xL = 0.0f, xR = 0.0f;
        for (int j=0; j<t.clusterCount; ++j) {
            const float w = t.weights[size_t(j)];
            xL += w * readDelay(0, t.delayL[size_t(j)]);
            xR += w * readDelay(1, t.delayR[size_t(j)]);
        }

        auto& state = states[size_t(i)];
        state.lowL += t.lowAlpha * (xL - state.lowL);
        state.lowR += t.lowAlpha * (xR - state.lowR);
        float colouredL = state.lowL*t.lowGain + (xL-state.lowL)*t.highGain;
        float colouredR = state.lowR*t.lowGain + (xR-state.lowR)*t.highGain;
        if (t.dispersive) {
            const float outL = t.allpass*colouredL + state.apInL - t.allpass*state.apOutL;
            const float outR = t.allpass*colouredR + state.apInR - t.allpass*state.apOutR;
            state.apInL = colouredL; state.apInR = colouredR;
            state.apOutL = outL; state.apOutR = outR;
            colouredL = outL; colouredR = outR;
        }

        wet[0] += t.gain * (colouredL*t.gLL + colouredR*t.gRL);
        wet[1] += t.gain * (colouredL*t.gLR + colouredR*t.gRR);
    }
    wet[0] *= invSqrt2;
    wet[1] *= invSqrt2;
    return wet;
}

std::array<float,2> Engine::applyFilters(std::array<StereoFilter,5>& bank,
                                         std::array<float,2> x) noexcept {
    for (auto& f : bank) {
        x[0] = f.l.tick(x[0]);
        x[1] = f.r.tick(x[1]);
    }
    return x;
}

std::array<float,2> Engine::process(float l,float r,float mix,bool bypass,
                                    bool eqBypass) noexcept {
    if (delay.empty()) return {l,r};
    delay[size_t(write)] = {l,r};

    auto wet = render(current, currentState);
    if (fadeRemaining > 0) {
        auto old = render(previous, previousState);
        const float x = 1.0f - float(fadeRemaining)/float(fadeLength);
        wet[0] = old[0] + x*(wet[0]-old[0]);
        wet[1] = old[1] + x*(wet[1]-old[1]);
        --fadeRemaining;
    }

    auto filtered = applyFilters(filters, wet);
    if (eqFadeRemaining > 0) {
        const auto old = applyFilters(previousFilters, wet);
        const float x = 1.0f - float(eqFadeRemaining)/float(eqFadeLength);
        filtered[0] = old[0] + x*(filtered[0]-old[0]);
        filtered[1] = old[1] + x*(filtered[1]-old[1]);
        --eqFadeRemaining;
    }
    const float eqTarget = eqBypass ? 1.0f : 0.0f;
    eqBypassMix = eqTarget + eqBypassCoeff * (eqBypassMix - eqTarget);
    filtered[0] += eqBypassMix * (wet[0] - filtered[0]);
    filtered[1] += eqBypassMix * (wet[1] - filtered[1]);

    write = (write + 1) % int(delay.size());

    mix = std::clamp(mix,0.0f,1.0f);
    if (!mixInitialised) { mixSmoothed = mix; mixInitialised = true; }
    else mixSmoothed = mix + mixSmoothingCoeff*(mixSmoothed-mix);
    const float dryGain = std::cos(halfPi*mixSmoothed);
    const float wetGain = std::sin(halfPi*mixSmoothed);
    std::array<float,2> out{l*dryGain + filtered[0]*wetGain,
                            r*dryGain + filtered[1]*wetGain};

    const float target = bypass ? 1.0f : 0.0f;
    const float c = float(std::exp(-1.0/(rate*0.003)));
    bypassMix = target + c*(bypassMix-target);
    out[0] += bypassMix*(l-out[0]);
    out[1] += bypassMix*(r-out[1]);
    return out;
}

void Engine::updateFilter(std::array<StereoFilter,5>& bank, int i,
                          float hz,float q,float gainDb,int type) {
    hz=std::clamp(hz,10.0f,float(rate*0.48));
    q=std::max(0.1f,q);
    const float w=6.28318530717958647692f*hz/float(rate), c=std::cos(w), s=std::sin(w);
    const float A=std::pow(10.0f,gainDb/40.0f);
    float b0,b1,b2,a0,a1,a2;
    if(type==0){const float alpha=s/(2*q);b0=(1+c)/2;b1=-(1+c);b2=b0;a0=1+alpha;a1=-2*c;a2=1-alpha;}
    else if(type==1){const float alpha=s/(2*q);b0=(1-c)/2;b1=1-c;b2=b0;a0=1+alpha;a1=-2*c;a2=1-alpha;}
    else if(type==2){const float alpha=s/(2*q);b0=1+alpha*A;b1=-2*c;b2=1-alpha*A;a0=1+alpha/A;a1=-2*c;a2=1-alpha/A;}
    else {
        const float alpha=s*0.5f*std::sqrt(2.0f), beta=2.0f*std::sqrt(A)*alpha;
        if(type==3){b0=A*((A+1)-(A-1)*c+beta);b1=2*A*((A-1)-(A+1)*c);b2=A*((A+1)-(A-1)*c-beta);a0=(A+1)+(A-1)*c+beta;a1=-2*((A-1)+(A+1)*c);a2=(A+1)+(A-1)*c-beta;}
        else {b0=A*((A+1)+(A-1)*c+beta);b1=-2*A*((A-1)+(A+1)*c);b2=A*((A+1)+(A-1)*c-beta);a0=(A+1)-(A-1)*c+beta;a1=2*((A-1)-(A+1)*c);a2=(A+1)-(A-1)*c-beta;}
    }
    auto set=[&](Biquad& v){v.b0=b0/a0;v.b1=b1/a0;v.b2=b2/a0;v.a1=a1/a0;v.a2=a2/a0;};
    set(bank[size_t(i)].l); set(bank[size_t(i)].r);
}

void Engine::setEq(const EqSettings& e) {
    if (eqInitialised) previousFilters = filters;
    updateFilter(filters,0,e.highPassHz,0.707f,0.0f,0);
    updateFilter(filters,1,e.frequency[0],0.707f,e.gainDb[0],3);
    updateFilter(filters,2,e.frequency[1],e.q[1],e.gainDb[1],2);
    updateFilter(filters,3,e.frequency[2],0.707f,e.gainDb[2],4);
    updateFilter(filters,4,e.lowPassHz,0.707f,0.0f,1);
    if (eqInitialised) eqFadeRemaining = eqFadeLength;
    else { previousFilters = filters; eqInitialised = true; eqFadeRemaining = 0; }
}

int Engine::tailSamples() const noexcept {
    return int(std::ceil(current.tailMs*float(rate)*0.001f)) + int(rate*0.060);
}

} // namespace early
