#include "EarlyModel.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace early {
namespace {
constexpr float speedOfSound = 343.0f;
constexpr float maximumEarlyDelayMs = 110.0f;

struct Vec3 { float x = 0.0f, y = 0.0f, z = 0.0f; };
struct Room { float width = 5.0f, depth = 7.0f, height = 3.0f; Vec3 source{}, listener{}; };
struct Candidate { Tap tap{}; float salience = 0.0f; };

float clamp01(float v) noexcept { return std::clamp(v, 0.0f, 1.0f); }
float reflectionSpanMs(float size) noexcept { return 18.0f + 92.0f * clamp01(size); }
float floorPresence(float distance) noexcept {
    const float near = 1.0f - clamp01(distance);
    return near * near;
}
float smoothstep(float low, float high, float value) noexcept {
    const float x = clamp01((value - low) / (high - low));
    return x * x * (3.0f - 2.0f * x);
}
float length(Vec3 v) noexcept { return std::sqrt(v.x*v.x + v.y*v.y + v.z*v.z); }
Vec3 subtract(Vec3 a, Vec3 b) noexcept { return {a.x-b.x, a.y-b.y, a.z-b.z}; }
std::uint64_t mixHash(std::uint64_t h, int v) noexcept {
    return (h ^ std::uint64_t(std::uint32_t(v) + 0x9e3779b9u)) * 1099511628211ull;
}

Room makeRoom(const Parameters& p) noexcept {
    const float size = clamp01(p.roomSize);
    const float shape = clamp01(p.roomShape);
    const float distance = clamp01(p.pattern);
    const float scale = 3.0f + 8.5f * std::pow(size, 1.25f);
    Room r;
    r.width  = scale * (0.88f + 0.45f * shape);
    r.depth  = scale * (1.12f + 0.65f * shape);
    r.height = 2.45f + 0.23f * scale * (1.0f + 0.30f * shape);
    r.listener = { r.width * (0.50f + 0.035f * shape), r.depth * 0.70f, 1.20f };
    const float directDistance = r.depth * (0.08f + 0.38f * distance);
    r.source = { r.width * (0.50f - 0.060f * shape),
                 std::max(r.depth * 0.12f, r.listener.y - directDistance),
                 1.20f };
    return r;
}

Vec3 reflect(Vec3 p, int wall, const Room& r) noexcept {
    switch (wall) {
        case 0: p.x = -p.x; break;
        case 1: p.x = 2.0f * r.width - p.x; break;
        case 2: p.y = -p.y; break;
        case 3: p.y = 2.0f * r.depth - p.y; break;
        case 4: p.z = -p.z; break;
        default:p.z = 2.0f * r.height - p.z; break;
    }
    return p;
}

float wallReflectivity(int wall, float shape) noexcept {
    constexpr std::array<float, 6> base{0.83f,0.81f,0.78f,0.76f,0.88f,0.72f};
    constexpr std::array<float, 6> variance{-0.08f,0.05f,-0.03f,0.07f,0.02f,-0.09f};
    return std::clamp(base[size_t(wall)] + variance[size_t(wall)] * shape, 0.48f, 0.94f);
}

float wallHighGain(int wall, float shape) noexcept {
    constexpr std::array<float, 6> base{0.90f,0.88f,0.84f,0.80f,0.94f,0.75f};
    constexpr std::array<float, 6> variance{-0.12f,0.05f,-0.08f,0.03f,0.02f,-0.10f};
    return std::clamp(base[size_t(wall)] + variance[size_t(wall)] * shape, 0.38f, 1.0f);
}

Tap makePath(Vec3 image, const Room& room, float coefficient, float highGain,
             int pathId, int order, const Parameters& p) noexcept {
    const float direct = std::max(0.10f, length(subtract(room.listener, room.source)));
    const Vec3 ray = subtract(image, room.listener);
    const float path = std::max(direct, length(ray));
    const float excess = std::max(0.0f, path - direct);
    const float delayMs = 1000.0f * excess / speedOfSound;
    const float distanceAtt = std::pow((direct + 0.75f) / (path + 0.75f), 0.62f);
    const float orderLoss = order == 1 ? 1.0f : 0.76f;
    const float absolutePathLoss = std::exp(-0.012f * excess);
    const float gain = coefficient * distanceAtt * orderLoss * absolutePathLoss;

    const float horizontal = std::sqrt(ray.x*ray.x + ray.y*ray.y);
    float pan = horizontal > 1.0e-5f ? ray.x / horizontal : 0.0f;
    const float width = std::clamp(p.width, 0.0f, 2.0f);
    pan = std::tanh(pan * (0.78f + 0.52f * width));

    const float distanceNorm = clamp01(p.pattern);
    const float airLoss = std::exp(-0.014f * path * (0.65f + 0.65f * distanceNorm));
    const float spectral = std::clamp(highGain * airLoss, 0.24f, 1.0f);
    const float lateness = clamp01(delayMs / 120.0f);
    const float diffusion = (0.05f + 0.95f * lateness * lateness)
                          * (order == 1 ? 1.0f : 1.35f)
                          * (0.65f + 0.55f * clamp01(p.roomShape))
                          * (0.78f + 0.50f * distanceNorm);
    const float spread = std::clamp((0.30f - 0.16f * distanceNorm)
                                  * (0.55f + 0.45f * width), 0.035f, 0.42f);
    const float decorrelation = diffusion * (0.16f + 0.24f * std::max(0.0f, width - 0.65f));

    Tap t;
    t.delayMs = std::clamp(delayMs, 0.75f, 300.0f);
    t.gain = gain;
    t.pan = pan;
    t.pathId = pathId;
    t.lowGain = std::clamp(0.96f + 0.04f * coefficient, 0.88f, 1.0f);
    t.highGain = spectral;
    t.diffusionMs = std::clamp(diffusion, 0.02f, 4.5f);
    t.stereoSpread = spread;
    t.decorrelationMs = std::clamp(decorrelation, 0.0f, 1.4f);
    return t;
}

void sortByDelay(TapModel& m) noexcept {
    for (int i = 1; i < m.count; ++i) {
        const Tap key = m.taps[size_t(i)];
        int j = i;
        while (j > 0 && key.delayMs < m.taps[size_t(j - 1)].delayMs) {
            m.taps[size_t(j)] = m.taps[size_t(j - 1)];
            --j;
        }
        m.taps[size_t(j)] = key;
    }
}

void fingerprint(TapModel& out) noexcept {
    std::uint64_t h = 1469598103934665603ull;
    h = mixHash(h, out.count);
    for (int i = 0; i < out.count; ++i) {
        const auto& t = out.taps[size_t(i)];
        h = mixHash(h, int(std::lround(t.delayMs * 1000.0f)));
        h = mixHash(h, int(std::lround(t.gain * 100000.0f)));
        h = mixHash(h, int(std::lround(t.pan * 10000.0f)));
        h = mixHash(h, int(std::lround(t.lowGain * 10000.0f)));
        h = mixHash(h, int(std::lround(t.highGain * 10000.0f)));
        h = mixHash(h, int(std::lround(t.diffusionMs * 10000.0f)));
        h = mixHash(h, int(std::lround(t.stereoSpread * 10000.0f)));
        h = mixHash(h, int(std::lround(t.decorrelationMs * 10000.0f)));
    }
    out.fingerprint = h;
}

float medianDelay(const TapModel& m) noexcept {
    if (m.count <= 0) return 20.0f;
    std::array<float, maxTaps> d{};
    for (int i=0;i<m.count;++i) d[size_t(i)] = m.taps[size_t(i)].delayMs;
    std::sort(d.begin(), d.begin()+m.count);
    const int mid = m.count/2;
    return (m.count & 1) ? d[size_t(mid)] : 0.5f*(d[size_t(mid-1)] + d[size_t(mid)]);
}

} // namespace

TapModel buildModel(const Parameters& p) noexcept {
    TapModel out;
    const int faces = std::clamp(p.faces, 1, 16);
    const float shape = clamp01(p.roomShape);
    const float floorGain = floorPresence(p.pattern);
    const Room room = makeRoom(p);

    std::array<Candidate, 6> first{};
    for (int wall = 0; wall < 6; ++wall) {
        const float refl = wallReflectivity(wall, shape);
        first[size_t(wall)].tap = makePath(reflect(room.source, wall, room), room,
                                                refl, wallHighGain(wall, shape), wall, 1, p);
        if (wall == 4) first[size_t(wall)].tap.gain *= floorGain;
        first[size_t(wall)].salience = first[size_t(wall)].tap.gain
                                    / std::sqrt(1.0f + first[size_t(wall)].tap.delayMs * 0.018f);
    }

    if (faces < 6) {
        for (int i=1;i<6;++i) {
            const Candidate key = first[size_t(i)];
            int j=i;
            while (j>0 && key.salience > first[size_t(j-1)].salience) {
                first[size_t(j)] = first[size_t(j-1)]; --j;
            }
            first[size_t(j)] = key;
        }
        for (int i=0;i<faces;++i) out.taps[size_t(out.count++)] = first[size_t(i)].tap;
    } else {
        for (int i=0;i<6;++i) out.taps[size_t(out.count++)] = first[size_t(i)].tap;
    }

    if (faces > 6) {
        std::array<Candidate, 18> second{};
        int candidateCount = 0;
        for (int a=0;a<6;++a) for (int b=a+1;b<6;++b) {
            const float coeff = wallReflectivity(a,shape) * wallReflectivity(b,shape);
            const float hi = wallHighGain(a,shape) * wallHighGain(b,shape);
            const Vec3 image = reflect(reflect(room.source,a,room),b,room);
            auto t = makePath(image, room, coeff, hi, 6 + candidateCount, 2, p);
            if (a == 4 || b == 4) t.gain *= floorGain;
            second[size_t(candidateCount++)] = {t, t.gain / std::sqrt(1.0f + t.delayMs*0.022f)};
            if ((a==0&&b==1)||(a==2&&b==3)||(a==4&&b==5)) {
                const Vec3 reverseImage = reflect(reflect(room.source,b,room),a,room);
                auto rt = makePath(reverseImage, room, coeff, hi, 30 + candidateCount, 2, p);
                if (a == 4 || b == 4) rt.gain *= floorGain;
                second[size_t(candidateCount++)] = {rt, rt.gain / std::sqrt(1.0f + rt.delayMs*0.022f)};
            }
        }
        for (int i=1;i<candidateCount;++i) {
            const Candidate key=second[size_t(i)]; int j=i;
            while(j>0 && key.salience>second[size_t(j-1)].salience){second[size_t(j)]=second[size_t(j-1)];--j;}
            second[size_t(j)]=key;
        }
        const int wanted = faces - 6;
        // Reserve one slot for a quiet long path once the room has enough faces.
        // Ranking every path by level alone discards the paths beyond ~50 ms.
        int lateIndex = -1;
        if (faces >= 12) {
            for (int i=0;i<candidateCount;++i)
                if (lateIndex < 0 || second[size_t(i)].tap.delayMs > second[size_t(lateIndex)].tap.delayMs)
                    lateIndex = i;
        }
        const int strongCount = wanted - (lateIndex >= 0 ? 1 : 0);
        for (int i=0;i<candidateCount && out.count<6+strongCount;++i)
            if (i != lateIndex) out.taps[size_t(out.count++)] = second[size_t(i)].tap;
        if (lateIndex >= 0 && out.count < maxTaps)
            out.taps[size_t(out.count++)] = second[size_t(lateIndex)].tap;

    }

    if (faces >= 6) {
        // Room Size controls the complete time span linearly. The image-source
        // geometry still determines the relative spacing of the reflections.
        float longest = 0.0f;
        for (int i=0;i<out.count;++i)
            if (out.taps[size_t(i)].gain > 0.0f)
                longest = std::max(longest, out.taps[size_t(i)].delayMs);
        const float timeScale = reflectionSpanMs(p.roomSize) / std::max(1.0f, longest);
        for (int i=0;i<out.count;++i)
            out.taps[size_t(i)].delayMs = std::clamp(out.taps[size_t(i)].delayMs * timeScale,
                                                     0.75f, maximumEarlyDelayMs);
    }

    float energy = 0.0f;
    for (int i=0;i<out.count;++i) energy += out.taps[size_t(i)].gain*out.taps[size_t(i)].gain;
    const float scale = energy > 1.35f*1.35f ? 1.35f/std::sqrt(energy) : 1.0f;
    for (int i=0;i<out.count;++i) out.taps[size_t(i)].gain *= 0.62f * scale;

    sortByDelay(out);
    fingerprint(out);
    return out;
}

TapModel transformLearnedModel(const TapModel& learned,
                               const Parameters& current,
                               const Parameters& learnedReference,
                               float learnedHighToneDb) noexcept {
    if (learned.count <= 0) return buildModel(current);

    TapModel out;
    const int desiredCount = std::clamp(current.faces,1,16);
    out.count = std::min(desiredCount, learned.count);
    const float referenceMedian = std::max(1.0f, medianDelay(learned));
    const float shapeDelta = current.roomShape - learnedReference.roomShape;
    const float distanceDelta = current.pattern - learnedReference.pattern;
    const float farther = clamp01(distanceDelta / std::max(0.01f, 1.0f - clamp01(learnedReference.pattern)));
    const float widthRatio = (0.20f + std::clamp(current.width,0.0f,2.0f))
                           / (0.20f + std::clamp(learnedReference.width,0.0f,2.0f));

    std::array<int,maxTaps> ids{};
    for(int i=0;i<learned.count;++i) ids[size_t(i)] = i;
    for(int i=1;i<learned.count;++i){const int key=ids[size_t(i)];int j=i;while(j>0&&learned.taps[size_t(key)].gain>learned.taps[size_t(ids[size_t(j-1)])].gain){ids[size_t(j)]=ids[size_t(j-1)];--j;}ids[size_t(j)]=key;}

    float learnedLast = 0.0f;
    for (int i=0;i<out.count;++i)
        learnedLast = std::max(learnedLast, learned.taps[size_t(ids[size_t(i)])].delayMs);
    const float referenceLast = std::min(learnedLast, maximumEarlyDelayMs);
    const float referenceSize = clamp01(learnedReference.roomSize);
    const float currentSize = clamp01(current.roomSize);
    const float smallestLast = std::min(18.0f, referenceLast * 0.4f);
    float desiredLast = referenceLast;
    if (currentSize < referenceSize && referenceSize > 0.0f)
        desiredLast = smallestLast + (referenceLast - smallestLast) * currentSize / referenceSize;
    else if (currentSize > referenceSize && referenceSize < 1.0f)
        desiredLast = referenceLast + (maximumEarlyDelayMs - referenceLast)
                                 * (currentSize - referenceSize) / (1.0f - referenceSize);
    const float timeScale = desiredLast / std::max(0.75f, learnedLast);

    const float learnedToneGain = std::pow(10.0f, std::clamp(learnedHighToneDb,-18.0f,12.0f)/20.0f);
    for(int n=0;n<out.count;++n){
        Tap t=learned.taps[size_t(ids[size_t(n)])];
        const float normalized = (t.delayMs-referenceMedian)/referenceMedian;
        const float warp = 1.0f + shapeDelta * 0.22f * std::tanh(normalized);
        t.delayMs = std::clamp(t.delayMs*timeScale*warp,0.75f,maximumEarlyDelayMs);
        t.gain *= std::pow(10.0f, -2.2f*distanceDelta/20.0f)
                * std::pow(std::max(0.45f,timeScale), -0.28f);
        // A measured short tap cannot be identified as a floor path with certainty.
        // Fade it only when Distance is moved beyond the learned reference position.
        t.gain *= 1.0f - farther * (1.0f - smoothstep(5.0f, 12.0f, learned.taps[size_t(ids[size_t(n)])].delayMs));
        t.pan = std::tanh(t.pan * widthRatio);
        const float baseHigh = t.highGain > 0.0f ? t.highGain : learnedToneGain;
        t.highGain = std::clamp(baseHigh
                              * std::pow(10.0f,-4.0f*std::max(0.0f,distanceDelta)/20.0f),0.22f,1.0f);
        const float late = clamp01(t.delayMs/100.0f);
        t.diffusionMs = std::clamp(std::max(0.04f,t.diffusionMs)
                                  *(1.0f+0.90f*std::max(0.0f,distanceDelta)+0.45f*late),0.02f,5.0f);
        t.stereoSpread = std::clamp(std::max(0.05f,t.stereoSpread)*widthRatio
                                  *(1.0f-0.35f*std::max(0.0f,distanceDelta)),0.02f,0.45f);
        t.decorrelationMs = std::clamp(std::max(0.0f,t.decorrelationMs)
                                     + 0.20f*t.diffusionMs*std::max(0.0f,current.width-0.7f),0.0f,1.5f);
        t.pathId = 100 + ids[size_t(n)];
        out.taps[size_t(n)] = t;
    }

    float transformedLast = 0.0f;
    for (int i=0;i<out.count;++i) transformedLast = std::max(transformedLast, out.taps[size_t(i)].delayMs);
    if (transformedLast > 0.0f) {
        const float correction = desiredLast / transformedLast;
        for (int i=0;i<out.count;++i)
            out.taps[size_t(i)].delayMs = std::clamp(out.taps[size_t(i)].delayMs * correction,
                                                     0.75f, maximumEarlyDelayMs);
    }

    if(out.count < desiredCount){
        const auto procedural=buildModel(current);
        std::array<bool,maxTaps> used{};
        for(int i=0;i<procedural.count && out.count<desiredCount;++i){
            Tap extra=procedural.taps[size_t(i)];
            bool duplicate=false;
            for(int j=0;j<out.count;++j)duplicate|=std::abs(out.taps[size_t(j)].delayMs-extra.delayMs)<2.5f;
            if(duplicate)continue;
            extra.gain*=0.68f;extra.pathId=200+i;used[size_t(i)]=true;
            out.taps[size_t(out.count++)]=extra;
        }
        for(int i=0;i<procedural.count && out.count<desiredCount;++i){
            if(used[size_t(i)])continue;
            Tap extra=procedural.taps[size_t(i)];extra.gain*=0.52f;extra.pathId=240+i;
            out.taps[size_t(out.count++)]=extra;
        }
    }
    sortByDelay(out);
    fingerprint(out);
    return out;
}

float modelDistance(const TapModel& target, const TapModel& candidate) noexcept {
    if (target.count == 0 || candidate.count == 0) return 1.0e6f;
    float cost = 0.25f * float(std::abs(target.count - candidate.count));
    std::array<bool, maxTaps> used{};
    for (int i = 0; i < target.count; ++i) {
        const auto& a = target.taps[size_t(i)];
        int best = -1; float bestCost = std::numeric_limits<float>::max();
        for (int j = 0; j < candidate.count; ++j) if (!used[size_t(j)]) {
            const auto& b = candidate.taps[size_t(j)];
            const float time = std::abs(std::log((a.delayMs + 1.0f) / (b.delayMs + 1.0f)));
            const float level = std::abs(a.gain - b.gain);
            const float spatial = 0.12f * std::abs(a.pan-b.pan);
            const float c = 2.8f * time + 0.7f * level + spatial;
            if (c < bestCost) { bestCost = c; best = j; }
        }
        if (best >= 0) { used[size_t(best)] = true; cost += bestCost; } else cost += 1.0f;
    }
    return cost / float(std::max(target.count, candidate.count));
}

} // namespace early
