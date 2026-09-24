#include "EarlyEngine.h"
#include <algorithm>
#include <cmath>

namespace early {
float Engine::Biquad::tick(float x) noexcept { const float y=b0*x+z1; z1=b1*x-a1*y+z2; z2=b2*x-a2*y; return y; }

void Engine::prepare(double sr, int maximumDelayMs) {
    rate = std::max(1.0, sr);
    delay.assign(size_t(std::ceil(rate * maximumDelayMs * .001)) + 4u, {});
    fadeLength = std::max(1, int(rate * .020));
    reset();
    setEq({});
}
void Engine::reset() { std::fill(delay.begin(),delay.end(),std::array<float,2>{}); write=0; fadeRemaining=0; bypassMix=0; for(auto& f:filters){f.l.z1=f.l.z2=f.r.z1=f.r.z2=0;} }
void Engine::setModel(const TapModel& m) { if(m.fingerprint==current.fingerprint)return; previous=current; current=m; fadeRemaining=fadeLength; }

std::array<float,2> Engine::render(const TapModel& m) const noexcept {
    std::array<float,2> wet{}; if(delay.empty()) return wet;
    const int n=int(delay.size());
    for(int i=0;i<m.count;++i){
        const auto& t=m.taps[size_t(i)]; const float ds=t.delayMs*float(rate)*.001f;
        const int whole=int(ds); const float frac=ds-float(whole);
        int a=write-whole; while(a<0)a+=n; int b=a-1; if(b<0)b+=n;
        const float xL=delay[size_t(a)][0]+frac*(delay[size_t(b)][0]-delay[size_t(a)][0]);
        const float xR=delay[size_t(a)][1]+frac*(delay[size_t(b)][1]-delay[size_t(a)][1]);
        const float mid=.5f*(xL+xR), side=.5f*(xL-xR);
        const float centre=mid + side*.35f; // retain some source stereo while taps own the image
        const float angle=(t.pan+1.0f)*.785398163f;
        wet[0]+=centre*t.gain*std::cos(angle); wet[1]+=centre*t.gain*std::sin(angle);
    }
    return wet;
}

std::array<float,2> Engine::process(float l,float r,float mix,bool bypass) noexcept {
    if(delay.empty()) return {l,r};
    delay[size_t(write)]={l,r};
    auto wet=render(current);
    if(fadeRemaining>0){auto old=render(previous);const float x=1.0f-float(fadeRemaining)/float(fadeLength);wet[0]=old[0]+x*(wet[0]-old[0]);wet[1]=old[1]+x*(wet[1]-old[1]);--fadeRemaining;}
    for(auto& f:filters){wet[0]=f.l.tick(wet[0]);wet[1]=f.r.tick(wet[1]);}
    write=(write+1)%int(delay.size()); mix=std::clamp(mix,0.0f,1.0f);
    std::array<float,2> out{l*(1-mix)+wet[0]*mix,r*(1-mix)+wet[1]*mix};
    const float target=bypass?1.0f:0.0f, c=float(std::exp(-1.0/(rate*.003)));
    bypassMix=target+c*(bypassMix-target); out[0]+=bypassMix*(l-out[0]);out[1]+=bypassMix*(r-out[1]);return out;
}

void Engine::updateFilter(int i,float hz,float q,float gainDb,int type){
    hz=std::clamp(hz,10.0f,float(rate*.48));q=std::max(.1f,q);const float w=6.283185307f*hz/float(rate),c=std::cos(w),s=std::sin(w),A=std::pow(10.0f,gainDb/40.0f);float b0,b1,b2,a0,a1,a2;
    if(type==0){const float alpha=s/(2*q);b0=(1+c)/2;b1=-(1+c);b2=b0;a0=1+alpha;a1=-2*c;a2=1-alpha;}
    else if(type==1){const float alpha=s/(2*q);b0=(1-c)/2;b1=1-c;b2=b0;a0=1+alpha;a1=-2*c;a2=1-alpha;}
    else if(type==2){const float alpha=s/(2*q);b0=1+alpha*A;b1=-2*c;b2=1-alpha*A;a0=1+alpha/A;a1=-2*c;a2=1-alpha/A;}
    else {
        const float alpha=s*.5f*std::sqrt((A+1/A)*0.f+2.f),beta=2.f*std::sqrt(A)*alpha;
        if(type==3){b0=A*((A+1)-(A-1)*c+beta);b1=2*A*((A-1)-(A+1)*c);b2=A*((A+1)-(A-1)*c-beta);a0=(A+1)+(A-1)*c+beta;a1=-2*((A-1)+(A+1)*c);a2=(A+1)+(A-1)*c-beta;}
        else {b0=A*((A+1)+(A-1)*c+beta);b1=-2*A*((A-1)+(A+1)*c);b2=A*((A+1)+(A-1)*c-beta);a0=(A+1)-(A-1)*c+beta;a1=2*((A-1)-(A+1)*c);a2=(A+1)-(A-1)*c-beta;}
    }
    auto setCoefficients=[&](Biquad& v){v.b0=b0/a0;v.b1=b1/a0;v.b2=b2/a0;v.a1=a1/a0;v.a2=a2/a0;};
    setCoefficients(filters[size_t(i)].l);setCoefficients(filters[size_t(i)].r);
}
void Engine::setEq(const EqSettings& e){updateFilter(0,e.highPassHz,.707f,0,0);updateFilter(1,e.frequency[0],.707f,e.gainDb[0],3);updateFilter(2,e.frequency[1],e.q[1],e.gainDb[1],2);updateFilter(3,e.frequency[2],.707f,e.gainDb[2],4);updateFilter(4,e.lowPassHz,.707f,0,1);}
int Engine::tailSamples() const noexcept {float ms=0;for(int i=0;i<current.count;++i)ms=std::max(ms,current.taps[size_t(i)].delayMs);return int(std::ceil(ms*float(rate)*.001f))+int(rate*.05);}
}
