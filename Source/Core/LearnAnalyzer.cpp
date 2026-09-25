#include "LearnAnalyzer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numeric>
#include <vector>

namespace early {
namespace {
constexpr float pi = 3.14159265358979323846f;
constexpr float profileBinMs = 0.5f;
constexpr int profileBins = 520;
constexpr int maxOnsets = 32;

struct Peak { float timeMs = 0.0f, score = 0.0f; };

float stereoMid(const float* left, const float* right, int i) noexcept {
    return right != nullptr ? 0.5f * (left[i] + right[i]) : left[i];
}

float whitened(const float* left, const float* right, int i) noexcept {
    const float x = stereoMid(left, right, i);
    const float previous = i > 0 ? stereoMid(left, right, i - 1) : 0.0f;
    return x - 0.92f * previous;
}

float median(std::vector<float> values) {
    if (values.empty()) return 0.0f;
    const size_t middle = values.size()/2u;
    std::nth_element(values.begin(), values.begin()+std::ptrdiff_t(middle), values.end());
    float m = values[middle];
    if ((values.size() & 1u) == 0u) {
        const auto lower = std::max_element(values.begin(), values.begin()+std::ptrdiff_t(middle));
        m = 0.5f * (m + *lower);
    }
    return m;
}

float msToSamples(float ms, double sampleRate) noexcept {
    return ms * float(sampleRate) * 0.001f;
}

float interpolatePeak(const std::array<float, profileBins>& p, int k) noexcept {
    const float a = p[size_t(k-1)], b = p[size_t(k)], c = p[size_t(k+1)];
    const float denominator = a - 2.0f*b + c;
    if (std::abs(denominator) < 1.0e-8f) return 0.0f;
    return std::clamp(0.5f*(a-c)/denominator, -0.5f, 0.5f);
}

std::uint64_t tapFingerprint(const TapModel& model) noexcept {
    std::uint64_t h = 1469598103934665603ull;
    const auto add = [&](std::uint64_t value, std::uint64_t& hash) {
        hash = (hash ^ value) * 1099511628211ull;
    };
    add(std::uint64_t(model.count), h);
    for (int i=0; i<model.count; ++i) {
        const auto& t = model.taps[size_t(i)];
        add(std::uint64_t(std::llround(t.delayMs*1000.0f)), h);
        add(std::uint64_t(std::llround(t.gain*100000.0f)), h);
        add(std::uint64_t(std::llround((t.pan+1.0f)*10000.0f)), h);
    }
    return h;
}

void measureStereo(const float* left, const float* right, int samples, double sampleRate,
                   const std::vector<int>& onsetSamples, TargetSummary& result) {
    if (right == nullptr) return;
    double directMid=0.0, directSide=0.0, lateMid=0.0, lateSide=0.0;
    for (const int onset : onsetSamples) {
        const int directEnd = std::min(samples, onset + int(std::lround(sampleRate*0.012)));
        const int lateStart = std::min(samples, onset + int(std::lround(sampleRate*0.020)));
        const int lateEnd = std::min(samples, onset + int(std::lround(sampleRate*0.220)));
        for (int i=onset; i<directEnd; ++i) {
            const double m=0.5*(double(left[i])+right[i]), s=0.5*(double(left[i])-right[i]);
            directMid += m*m; directSide += s*s;
        }
        for (int i=lateStart; i<lateEnd; ++i) {
            const double m=0.5*(double(left[i])+right[i]), s=0.5*(double(left[i])-right[i]);
            lateMid += m*m; lateSide += s*s;
        }
    }
    const auto width = [](double m, double s) noexcept {
        const double rm=std::sqrt(m), rs=std::sqrt(s);
        return float(2.0*rs/std::max(1.0e-12,rm+rs));
    };
    const float directWidth = width(directMid,directSide);
    const float lateWidth = width(lateMid,lateSide);
    result.stereoWidth = std::clamp(1.0f + 0.85f*(lateWidth-directWidth), 0.0f, 2.0f);
    result.stereoMeasured = true;
}

void measureTone(const float* roomLeft, const float* roomRight,
                 int samples, double sampleRate, const std::vector<int>& onsetSamples,
                 TargetSummary& result) {
    std::vector<unsigned char> region(size_t(samples), 0u);
    for (const int onset : onsetSamples) {
        const int directEnd = std::min(samples,onset+int(std::lround(sampleRate*0.012)));
        const int lateStart = std::min(samples,onset+int(std::lround(sampleRate*0.020)));
        const int lateEnd = std::min(samples,onset+int(std::lround(sampleRate*0.220)));
        for(int i=onset;i<directEnd;++i) region[size_t(i)]=1u;
        for(int i=lateStart;i<lateEnd;++i) if(region[size_t(i)]==0u) region[size_t(i)]=2u;
    }

    constexpr std::array<float,5> crossover{180.0f,600.0f,1800.0f,5000.0f,11000.0f};
    std::array<float,5> alpha{};
    std::array<float,5> state{};
    for(size_t i=0;i<alpha.size();++i)
        alpha[i]=1.0f-std::exp(-2.0f*pi*crossover[i]/float(sampleRate));
    double energy[2][6]{};
    for(int i=0;i<samples;++i) {
        const float x=stereoMid(roomLeft,roomRight,i);
        for(size_t b=0;b<state.size();++b) state[b]+=alpha[b]*(x-state[b]);
        const std::array<float,6> bands{state[0], state[1]-state[0], state[2]-state[1],
                                        state[3]-state[2], state[4]-state[3], x-state[4]};
        const unsigned char section=region[size_t(i)];
        if(section==0u) continue;
        for(size_t b=0;b<bands.size();++b) energy[section-1u][b]+=double(bands[b])*bands[b];
    }
    double directTotal=0.0, lateTotal=0.0;
    for(int b=0;b<6;++b){directTotal+=energy[0][b];lateTotal+=energy[1][b];}
    if(directTotal<=1.0e-12||lateTotal<=1.0e-12) return;
    for(int b=0;b<6;++b) {
        const double d=energy[0][b]/directTotal, l=energy[1][b]/lateTotal;
        result.toneDb[size_t(b)]=float(std::clamp(10.0*std::log10((l+1.0e-10)/(d+1.0e-10)),-24.0,18.0));
    }
}
} // namespace

TargetSummary LearnAnalyzer::analyze(const float* roomLeft, const float* roomRight,
                                     const float* dryLeft, const float* dryRight,
                                     int samples, double sampleRate) const {
    TargetSummary result;
    if(roomLeft==nullptr||samples<=0||sampleRate<8000.0) return result;
    const bool hasDry=dryLeft!=nullptr;
    result.usedDryReference=hasDry;
    const float* onsetLeft=hasDry?dryLeft:roomLeft;
    const float* onsetRight=hasDry?dryRight:roomRight;

    const int frameSize=std::max(1,int(std::lround(sampleRate*0.0005)));
    const int frameCount=samples/frameSize;
    if(frameCount<520) return result;
    std::vector<float> envelope(size_t(frameCount),0.0f);
    float globalPeak=0.0f;
    for(int frame=0;frame<frameCount;++frame) {
        double e=0.0;
        const int begin=frame*frameSize;
        for(int i=begin;i<begin+frameSize;++i){const float x=stereoMid(onsetLeft,onsetRight,i);e+=double(x)*x;}
        envelope[size_t(frame)]=float(std::sqrt(e/double(frameSize)));
        globalPeak=std::max(globalPeak,envelope[size_t(frame)]);
    }
    if(globalPeak<1.0e-6f) return result;

    const int historyFrames=std::max(2,int(std::lround(0.008/(double(frameSize)/sampleRate))));
    const int refractoryFrames=std::max(1,int(std::lround(0.180/(double(frameSize)/sampleRate))));
    const int marginFrames=int(std::ceil(0.270/(double(frameSize)/sampleRate)));
    std::vector<int> onsetSamples;
    int last=-refractoryFrames;
    for(int f=historyFrames;f<frameCount-marginFrames;++f) {
        float prior=0.0f;
        for(int j=f-historyFrames;j<f;++j) prior+=envelope[size_t(j)];
        prior/=float(historyFrames);
        const float now=envelope[size_t(f)];
        if(f-last>=refractoryFrames && now>globalPeak*0.018f && now>prior*1.45f
           && now>=envelope[size_t(f-1)] && now>=envelope[size_t(f+1)]) {
            onsetSamples.push_back(f*frameSize); last=f;
            if(int(onsetSamples.size())>=maxOnsets) break;
        }
    }
    result.onsetCount=int(onsetSamples.size());
    if(onsetSamples.empty()) return result;

    std::array<std::vector<float>,profileBins> scores;
    const int templateLength=std::clamp(int(std::lround(sampleRate*0.0035)),48,512);
    const int searchRadius=frameSize;
    const int shiftStep=std::max(1,frameSize/12);
    int usableOnsets=0;
    for(const int onset:onsetSamples) {
        const int anchor=std::max(1,onset-frameSize);
        if(anchor+int(std::lround(sampleRate*0.265))+templateLength>=samples) continue;
        double directEnergy=0.0;
        for(int n=0;n<templateLength;++n){const float x=whitened(onsetLeft,onsetRight,anchor+n);directEnergy+=double(x)*x;}
        if(directEnergy<1.0e-10) continue;
        ++usableOnsets;
        for(int k=3;k<profileBins;++k) {
            const int nominal=int(std::lround(msToSamples(float(k)*profileBinMs,sampleRate)));
            float best=0.0f;
            for(int shift=-searchRadius;shift<=searchRadius;shift+=shiftStep) {
                const int candidate=anchor+nominal+shift;
                if(candidate<1||candidate+templateLength>=samples) continue;
                double dot=0.0, reflectedEnergy=0.0;
                for(int n=0;n<templateLength;++n) {
                    const float a=whitened(onsetLeft,onsetRight,anchor+n);
                    const float b=whitened(roomLeft,roomRight,candidate+n);
                    dot+=double(a)*b; reflectedEnergy+=double(b)*b;
                }
                const double corr=std::abs(dot)/std::sqrt(std::max(1.0e-20,directEnergy*reflectedEnergy));
                const double level=std::min(1.0,std::sqrt(reflectedEnergy/directEnergy));
                best=std::max(best,float(corr*std::sqrt(level)));
            }
            scores[size_t(k)].push_back(best);
        }
    }
    if(usableOnsets==0) return result;

    std::array<float,profileBins> profile{},smooth{};
    for(int k=3;k<profileBins;++k) {
        const auto& values=scores[size_t(k)];
        if(values.empty()) continue;
        const float mean=std::accumulate(values.begin(),values.end(),0.0f)/float(values.size());
        profile[size_t(k)]=0.72f*median(values)+0.28f*mean;
    }
    for(int k=16;k<profileBins-2;++k)
        smooth[size_t(k)]=(profile[size_t(k-2)]+2.0f*profile[size_t(k-1)]+3.0f*profile[size_t(k)]
                          +2.0f*profile[size_t(k+1)]+profile[size_t(k+2)])/9.0f;
    const float maximum=*std::max_element(smooth.begin()+3,smooth.end()-16);
    std::vector<Peak> candidates;
    const int startBin=3;
    for(int k=startBin+1;k<profileBins-12;++k) {
        const float value=smooth[size_t(k)];
        const int shoulderBins=std::min(k-2,int(std::lround(6.0f/profileBinMs)));
        const float shoulder=0.5f*(smooth[size_t(k-shoulderBins)]+smooth[size_t(k+shoulderBins)]);
        if(value>=smooth[size_t(k-1)]&&value>smooth[size_t(k+1)]
           &&value>=std::max(0.055f,maximum*0.15f)&&value-shoulder>=0.018f) {
            const float fraction=interpolatePeak(smooth,k);
            candidates.push_back({(float(k)+fraction)*profileBinMs,value});
        }
    }
    std::sort(candidates.begin(),candidates.end(),[](const Peak&a,const Peak&b){return a.score>b.score;});
    std::vector<Peak> selected;
    for(const auto& c:candidates) {
        if(std::none_of(selected.begin(),selected.end(),[&](const Peak&p){return std::abs(p.timeMs-c.timeMs)<(c.timeMs<10.0f?1.1f:3.2f);})) selected.push_back(c);
        if(selected.size()>=16u) break;
    }
    std::sort(selected.begin(),selected.end(),[](const Peak&a,const Peak&b){return a.timeMs<b.timeMs;});

    result.taps.count=int(selected.size());
    for(int index=0;index<result.taps.count;++index) {
        const Peak peak=selected[size_t(index)];
        float weightedLag=0.0f,weight=0.0f;
        double ratioSum=0.0,panWeighted=0.0,panWeight=0.0;
        for(const int onset:onsetSamples) {
            const int directStart=std::max(1,onset-frameSize);
            const int candidate=directStart+int(std::lround(msToSamples(peak.timeMs,sampleRate)));
            if(candidate<1||candidate+templateLength>=samples) continue;
            double de=0.0,re=0.0,leftE=0.0,rightE=0.0;
            for(int n=0;n<templateLength;++n) {
                const float a=stereoMid(onsetLeft,onsetRight,directStart+n);
                const float b=stereoMid(roomLeft,roomRight,candidate+n);
                de+=double(a)*a; re+=double(b)*b;
                const float rl=roomLeft[candidate+n]; leftE+=double(rl)*rl;
                const float rr=roomRight?roomRight[candidate+n]:rl; rightE+=double(rr)*rr;
            }
            if(de>1.0e-12&&re>1.0e-12) {
                const float ratio=float(std::sqrt(re/de));
                ratioSum+=std::min(2.0f,ratio);
                const double l=std::sqrt(leftE),r=std::sqrt(rightE);
                const float pan=float((r-l)/std::max(1.0e-12,l+r));
                panWeighted+=pan*ratio; panWeight+=ratio;
                weightedLag+=peak.timeMs*ratio; weight+=ratio;
            }
        }
        Tap t;
        t.delayMs=weight>0.0f?weightedLag/weight:peak.timeMs;
        t.gain=std::clamp(float(ratioSum/std::max<size_t>(1u,onsetSamples.size())),0.015f,1.25f);
        t.pan=panWeight>0.0?std::clamp(float(panWeighted/panWeight),-1.0f,1.0f):0.0f;
        t.pathId=index;
        result.taps.taps[size_t(index)]=t;
    }

    measureStereo(roomLeft,roomRight,samples,sampleRate,onsetSamples,result);
    measureTone(roomLeft,roomRight,samples,sampleRate,onsetSamples,result);
    const float highTone=0.5f*(result.toneDb[4]+result.toneDb[5]);
    const float highGain=std::pow(10.0f,std::clamp(highTone,-18.0f,6.0f)/20.0f);
    float energy=0.0f;
    for(int i=0;i<result.taps.count;++i) {
        auto& t=result.taps.taps[size_t(i)];
        const float late=std::clamp(t.delayMs/120.0f,0.0f,1.0f);
        t.lowGain=std::clamp(std::pow(10.0f,result.toneDb[0]/40.0f),0.70f,1.18f);
        t.highGain=std::clamp(highGain*std::exp(-0.14f*late),0.25f,1.0f);
        t.diffusionMs=std::clamp(0.04f+2.2f*late*late,0.04f,3.0f);
        t.stereoSpread=std::clamp(0.07f+0.13f*late+0.08f*std::max(0.0f,result.stereoWidth-1.0f),0.04f,0.34f);
        t.decorrelationMs=std::clamp(t.diffusionMs*(0.12f+0.16f*late),0.0f,0.85f);
        energy+=t.gain*t.gain;
    }
    if(energy>1.35f*1.35f) {
        const float scale=1.35f/std::sqrt(energy);
        for(int i=0;i<result.taps.count;++i) result.taps.taps[size_t(i)].gain*=scale;
    }
    result.taps.fingerprint=tapFingerprint(result.taps);

    const float topScore=candidates.empty()?0.0f:candidates.front().score;
    if(result.taps.count>=2&&usableOnsets>=3&&topScore>=0.10f) result.confidence=Confidence::good;
    else if(hasDry&&result.taps.count>=2&&topScore>=0.055f) result.confidence=Confidence::fair;
    else if(result.taps.count>=1&&topScore>=0.13f) result.confidence=Confidence::fair;
    return result;
}

FitResult LearnAnalyzer::fit(const TargetSummary& target) const {
    FitResult fit;
    if(target.taps.count<1||target.confidence==Confidence::weak) return fit;
    fit.parameters.width=target.stereoMeasured?std::clamp(target.stereoWidth,0.0f,2.0f):1.0f;
    const float high=0.5f*(target.toneDb[4]+target.toneDb[5]);
    const float tonalDistance=std::clamp((-high+2.0f)/14.0f,0.0f,1.0f);
    // Match the measured arrival pattern to the actual procedural model.
    // Quiet diffuse taps are excluded: Faces controls the prominent paths.
    const auto score=[&](const Parameters& p) {
        const auto model=buildModel(p);
        std::array<float,16> times{};
        int count=0;
        for(int i=0;i<model.count;++i)
            if(model.taps[size_t(i)].pathId<300) times[size_t(count++)]=model.taps[size_t(i)].delayMs;
        float cost=0.0f, weight=0.0f;
        float maxGain=0.0f, targetFirst=300.0f, targetLast=0.0f;
        for(int i=0;i<target.taps.count;++i){
            const auto& t=target.taps.taps[size_t(i)];
            maxGain=std::max(maxGain,std::abs(t.gain));
            targetFirst=std::min(targetFirst,t.delayMs);
            targetLast=std::max(targetLast,t.delayMs);
        }
        for(int i=0;i<target.taps.count;++i){
            const auto& t=target.taps.taps[size_t(i)];
            const float w=0.35f+0.65f*std::sqrt(std::abs(t.gain)/std::max(0.01f,maxGain));
            float best=300.0f;
            for(int j=0;j<count;++j) best=std::min(best,std::abs(t.delayMs-times[size_t(j)]));
            cost+=w*std::min(3.0f,best/(9.0f+0.22f*t.delayMs));weight+=w;
        }
        cost/=std::max(0.01f,weight);
        float reverse=0.0f;
        for(int j=0;j<count;++j){
            float best=300.0f;
            for(int i=0;i<target.taps.count;++i)
                best=std::min(best,std::abs(times[size_t(j)]-target.taps.taps[size_t(i)].delayMs));
            reverse+=std::min(3.0f,best/(9.0f+0.22f*times[size_t(j)]));
        }
        cost+=0.38f*reverse/float(std::max(1,count));
        cost+=0.045f*float(std::abs(count-target.taps.count));
        cost+=0.27f*std::abs(times[0]-targetFirst)/(10.0f+0.25f*targetFirst);
        cost+=0.20f*std::abs(times[size_t(count-1)]-targetLast)/(12.0f+0.25f*targetLast);
        cost+=0.035f*std::abs(p.pattern-tonalDistance);
        return cost;
    };
    fit.error=std::numeric_limits<float>::max();
    const int targetFaces=std::clamp(target.taps.count,1,16);
    const auto tryFit=[&](Parameters candidate){
        candidate.roomSize=std::clamp(candidate.roomSize,0.0f,1.0f);
        candidate.roomShape=std::clamp(candidate.roomShape,0.0f,1.0f);
        candidate.pattern=std::clamp(candidate.pattern,0.0f,1.0f);
        candidate.faces=std::clamp(candidate.faces,1,16);
        const float error=score(candidate);
        if(error<fit.error){fit.error=error;fit.parameters=candidate;}
    };
    for(int faces=std::max(1,targetFaces-2);faces<=std::min(16,targetFaces+2);++faces)
        for(int size=0;size<=10;++size)
            for(int shape=0;shape<=4;++shape)
                for(int distance=0;distance<=4;++distance){
                    Parameters p;p.faces=faces;p.width=fit.parameters.width;
                    p.roomSize=float(size)*0.1f;p.roomShape=float(shape)*0.25f;
                    p.pattern=float(distance)*0.25f;tryFit(p);
                }
    for(int step=0;step<2;++step){
        const Parameters best=fit.parameters;
        const float delta=step==0?0.05f:0.025f;
        for(int f=-1;f<=1;++f)for(int s=-1;s<=1;++s)
            for(int sh=-1;sh<=1;++sh)for(int d=-1;d<=1;++d){
                Parameters p=best;p.faces+=f;p.roomSize+=delta*float(s);
                p.roomShape+=delta*2.0f*float(sh);p.pattern+=delta*2.0f*float(d);
                tryFit(p);
            }
    }
    fit.eqGainDb[0]=std::clamp(0.20f*(target.toneDb[0]+target.toneDb[1]),-4.0f,4.0f);
    fit.eqGainDb[1]=std::clamp(0.18f*(target.toneDb[2]+target.toneDb[3]),-4.0f,4.0f);
    fit.eqGainDb[2]=std::clamp(0.20f*(target.toneDb[4]+target.toneDb[5]),-5.0f,4.0f);
    fit.valid=true;
    return fit;
}

} // namespace early
