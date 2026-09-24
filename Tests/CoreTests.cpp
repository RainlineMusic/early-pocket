#include "Core/EarlyEngine.h"
#include "Core/EarlyModel.h"
#include "Core/LearnAnalyzer.h"

#include <algorithm>
#include <array>
#ifdef NDEBUG
#undef NDEBUG // Keep the regression checks active in Release CI builds.
#endif
#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>

namespace {

void testModel() {
    early::Parameters p;
    p.faces=16; p.roomSize=0.63f; p.roomShape=0.72f; p.width=1.25f; p.pattern=0.48f;
    const auto model=early::buildModel(p);
    assert(model.count==16);
    assert(model.fingerprint!=0);
    float previous=0.0f;
    for(int i=0;i<model.count;++i){
        const auto&t=model.taps[size_t(i)];
        assert(t.delayMs>=0.75f&&t.delayMs<=300.0f);
        assert(t.delayMs>=previous); previous=t.delayMs;
        assert(t.gain>0.0f&&std::isfinite(t.gain));
        assert(t.pan>=-1.0f&&t.pan<=1.0f);
        assert(t.highGain>0.0f&&t.highGain<=1.0f);
        assert(t.diffusionMs>=0.0f);
    }
}

float latestDelay(const early::TapModel& model) {
    float latest = 0.0f;
    for (int i=0;i<model.count;++i)
        if (model.taps[size_t(i)].gain > 0.0f)
            latest = std::max(latest, model.taps[size_t(i)].delayMs);
    return latest;
}

float pathGain(const early::TapModel& model, int pathId) {
    for (int i=0;i<model.count;++i)
        if (model.taps[size_t(i)].pathId == pathId)
            return model.taps[size_t(i)].gain;
    return -1.0f;
}

void testRoomSizeAndFloorReflection() {
    early::Parameters p;
    for (float shape : {0.0f, 0.5f, 1.0f}) {
        p.roomShape = shape;
        for (float distance : {0.0f, 0.5f, 1.0f}) {
            p.pattern = distance;
            for (int faces=6;faces<=16;++faces) {
                p.faces = faces;
                float previous = 0.0f;
                for (int step=0;step<=10;++step) {
                    p.roomSize = float(step) / 10.0f;
                    const auto model = early::buildModel(p);
                    const float latest = latestDelay(model);
                    assert(model.count == faces);
                    assert(std::abs(latest - (18.0f + 92.0f*p.roomSize)) < 0.2f);
                    assert(latest >= previous && latest <= 110.01f);
                    previous = latest;
                }
            }
            p.roomSize = 1.0f;
            for (int faces=1;faces<=16;++faces) {
                p.faces = faces;
                assert(latestDelay(early::buildModel(p)) <= 110.01f);
            }
        }
    }

    p.faces = 6;
    p.roomSize = 1.0f;
    p.roomShape = 1.0f;
    p.pattern = 0.0f;
    const auto near = early::buildModel(p);
    p.pattern = 0.5f;
    const auto middle = early::buildModel(p);
    p.pattern = 1.0f;
    const auto far = early::buildModel(p);
    assert(pathGain(near, 4) > pathGain(middle, 4));
    assert(pathGain(middle, 4) > pathGain(far, 4));
    assert(pathGain(far, 4) == 0.0f);
    for (int i=0;i<far.count;++i)
        assert(far.taps[size_t(i)].delayMs >= 3.0f || far.taps[size_t(i)].gain == 0.0f);
}

void testLearnedRoomSizeAndDistance() {
    early::TapModel learned;
    learned.count = 3;
    learned.taps[0].delayMs = 2.0f;  learned.taps[0].gain = 0.8f;
    learned.taps[1].delayMs = 25.0f; learned.taps[1].gain = 0.5f;
    learned.taps[2].delayMs = 50.0f; learned.taps[2].gain = 0.3f;
    early::Parameters reference;
    reference.faces = 3;
    reference.roomSize = 0.4f;
    reference.pattern = 0.2f;
    const auto matched = early::transformLearnedModel(learned, reference, reference);
    assert(std::abs(latestDelay(matched) - 50.0f) < 0.01f);
    auto changed = reference;
    changed.roomSize = 1.0f;
    const auto large = early::transformLearnedModel(learned, changed, reference);
    assert(std::abs(latestDelay(large) - 110.0f) < 0.01f);
    changed = reference;
    changed.pattern = 1.0f;
    const auto far = early::transformLearnedModel(learned, changed, reference);
    assert(pathGain(far, 100) == 0.0f);
    assert(pathGain(far, 101) > 0.0f);
}

std::array<double,2> renderImpulse(bool leftInput) {
    constexpr double sr=48000.0;
    early::Engine engine; engine.prepare(sr);
    early::TapModel model; model.count=1;
    model.taps[0].delayMs=12.35f; model.taps[0].gain=0.7f; model.taps[0].pan=0.0f;
    model.taps[0].highGain=1.0f; model.taps[0].lowGain=1.0f;
    model.taps[0].diffusionMs=0.0f; model.taps[0].stereoSpread=0.22f;
    model.fingerprint=12345;
    engine.setModel(model);
    std::array<double,2> energy{};
    for(int i=0;i<2500;++i){
        const float impulse=i==0?1.0f:0.0f;
        const auto y=engine.process(leftInput?impulse:0.0f,leftInput?0.0f:impulse,1.0f,false);
        energy[0]+=double(y[0])*y[0];energy[1]+=double(y[1])*y[1];
    }
    return energy;
}

void testStereoSymmetry() {
    const auto left=renderImpulse(true),right=renderImpulse(false);
    assert(left[0]>left[1]);
    assert(right[1]>right[0]);
    const double scale=std::max({left[0],left[1],right[0],right[1],1.0e-12});
    assert(std::abs(left[0]-right[1])/scale<1.0e-4);
    assert(std::abs(left[1]-right[0])/scale<1.0e-4);
}

void addEcho(const std::vector<float>& dry,std::vector<float>& left,std::vector<float>& right,
             int delay,float gain,float pan) {
    const float l=std::sqrt(0.5f*(1.0f-pan)),r=std::sqrt(0.5f*(1.0f+pan));
    for(size_t i=0;i<dry.size();++i){
        const size_t target=i+size_t(delay); if(target>=dry.size()) break;
        left[target]+=dry[i]*gain*l; right[target]+=dry[i]*gain*r;
    }
}

void testLearn() {
    constexpr double sr=48000.0; const int samples=int(sr*5.0);
    std::vector<float> dry(size_t(samples),0.0f),roomL(size_t(samples),0.0f),roomR(size_t(samples),0.0f);
    for(int burst=0;burst<9;++burst){
        const int start=int(sr*(0.25+0.50*burst));
        for(int n=0;n<180&&start+n<samples;++n){
            const float env=std::exp(-float(n)/32.0f);
            const float x=env*(0.72f*std::sin(0.071f*float(n))+0.28f*std::sin(0.173f*float(n)));
            dry[size_t(start+n)]+=x;
        }
    }
    for(int i=0;i<samples;++i){roomL[size_t(i)]+=dry[size_t(i)]*0.70710678f;roomR[size_t(i)]+=dry[size_t(i)]*0.70710678f;}
    const std::array<float,3> ms{12.35f,23.7f,41.2f};
    const std::array<float,3> gains{0.58f,0.40f,0.27f};
    const std::array<float,3> pans{-0.55f,0.35f,0.05f};
    for(size_t i=0;i<ms.size();++i)addEcho(dry,roomL,roomR,int(std::lround(ms[i]*float(sr)*0.001f)),gains[i],pans[i]);

    early::LearnAnalyzer analyzer;
    const auto summary=analyzer.analyze(roomL.data(),roomR.data(),dry.data(),nullptr,samples,sr);
    assert(summary.usedDryReference);
    assert(summary.confidence!=early::Confidence::weak);
    assert(summary.taps.count>=2);
    for(float expected:ms){
        float best=1000.0f;
        for(int i=0;i<summary.taps.count;++i)best=std::min(best,std::abs(summary.taps.taps[size_t(i)].delayMs-expected));
        assert(best<1.2f);
    }
    const auto fit=analyzer.fit(summary); assert(fit.valid);
    auto changed=fit.parameters;changed.faces=16;
    const auto transformed=early::transformLearnedModel(summary.taps,changed,fit.parameters,0.5f*(summary.toneDb[4]+summary.toneDb[5]));
    assert(transformed.count==16);
}

void testEngineFiniteAcrossRates() {
    for(double sr : {44100.0, 48000.0, 96000.0, 192000.0}) {
        early::Engine engine; engine.prepare(sr);
        early::Parameters p; p.faces=16; p.roomSize=0.95f; p.roomShape=0.85f; p.width=1.8f; p.pattern=0.9f;
        engine.setModel(early::buildModel(p));
        early::EqSettings eq; eq.highPassHz=35.0f; eq.lowPassHz=18000.0f; eq.gainDb={3.0f,-4.0f,2.0f};
        engine.setEq(eq);
        for(int i=0;i<int(sr*0.45);++i){
            const float l=i==0?1.0f:0.0f, r=i==17?-0.7f:0.0f;
            const auto y=engine.process(l,r,0.83f,false);
            assert(std::isfinite(y[0])&&std::isfinite(y[1]));
            assert(std::abs(y[0])<20.0f&&std::abs(y[1])<20.0f);
        }
    }
}
}

int main(){testModel();testRoomSizeAndFloorReflection();testLearnedRoomSizeAndDistance();testStereoSymmetry();testLearn();testEngineFiniteAcrossRates();std::cout<<"EarlyCore tests passed\n";return 0;}
