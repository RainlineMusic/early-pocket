#include "PluginEditor.h"
#include <cmath>
#include <complex>
#include <algorithm>
#include <limits>

namespace {
constexpr float designWidth=1672.f, expandedHeight=1065.f, collapsedHeight=755.f;
constexpr int minimumEditorWidth=1120, maximumEditorWidth=1500;
juce::Font uiFont(float size){
    static const juce::String family=[] {const auto fonts=juce::Font::findAllTypefaceNames();
#if JUCE_WINDOWS
        for(auto name:{"Segoe UI Variable Text","Segoe UI Variable","Segoe UI"})if(fonts.contains(name))return juce::String(name);
#elif JUCE_MAC
        for(auto name:{"SF Pro Text","SF Pro",".AppleSystemUIFont"})if(fonts.contains(name))return juce::String(name);
#else
        if(fonts.contains("Liberation Sans"))return juce::String("Liberation Sans");
#endif
        return juce::Font::getDefaultSansSerifFontName();}();
    return juce::Font(juce::FontOptions(family,size,juce::Font::plain));
}
void text(juce::Graphics& g,const juce::String& s,juce::Rectangle<float> r,float size,juce::Colour c,int align=juce::Justification::centredLeft){g.setFont(uiFont(size));g.setColour(c);g.drawText(s,r,align);}
void stroke(juce::Graphics& g,const juce::Path& p,juce::Colour c,float width){g.setColour(c);g.strokePath(p,juce::PathStrokeType(width,juce::PathStrokeType::curved,juce::PathStrokeType::rounded));}
void panel(juce::Graphics& g,juce::Rectangle<float> r){g.setColour(juce::Colour(0xff202020));g.fillRoundedRectangle(r,16);g.setColour(juce::Colour(0xff505050));g.drawRoundedRectangle(r,16,1.f);}
void drawDialBody(juce::Graphics& g,juce::Rectangle<float> area,PocketLook& look,const juce::String& title,const juce::String& unit,float value,float proportion,bool integer){
    const float s=area.getWidth()/200.f,r=77.f*s,ring=r+8.f*s;const auto c=area.getCentre();const auto face=juce::Rectangle<float>(2*r,2*r).withCentre(c);
    g.setColour(juce::Colour(0xff252525));g.fillEllipse(face);g.setColour(juce::Colour(0xff555555));g.drawEllipse(face,s);
    const float start=juce::MathConstants<float>::pi*1.25f,end=start+juce::MathConstants<float>::pi*1.5f*proportion;juce::Path track,arc;track.addCentredArc(c.x,c.y,ring,ring,0,start,juce::MathConstants<float>::pi*2.75f,true);stroke(g,track,juce::Colour(0xff101010),6.f*s);if(proportion>0.f){arc.addCentredArc(c.x,c.y,ring,ring,0,start,end,true);stroke(g,arc,look.accent(),5.f*s);}
    const auto marker=juce::Point<float>(c.x+ring*std::sin(end),c.y-ring*std::cos(end));g.setColour(juce::Colour(0xfff4f4f4));g.fillEllipse(marker.x-5.5f*s,marker.y-5.5f*s,11.f*s,11.f*s);
    const juce::String shown=integer?juce::String(juce::roundToInt(value)):juce::String(juce::roundToInt(value))+unit;text(g,title,{c.x-r,c.y-34.f*s,2*r,25.f*s},18.f*s,look.ink(),juce::Justification::centred);text(g,shown,{c.x-r,c.y-8.f*s,2*r,43.f*s},(unit=="%"?33.f:30.f)*s,look.ink(),juce::Justification::centred);
}
void drawControlButton(juce::Graphics& g,juce::Rectangle<float> area,const juce::String& icon,const juce::String& caption,bool toggled,PocketLook& look){
    auto r=area.reduced(2.f);g.setColour(juce::Colour(0xff292929));g.fillRoundedRectangle(r,8.f);g.setColour(juce::Colour(0xff555555));g.drawRoundedRectangle(r,8.f,.8f);const auto c=r.getCentre();const float s=juce::jmin(r.getWidth(),r.getHeight())/40.f;juce::Path p;
    if(icon=="power"){p.addCentredArc(0,1,8,8,0,.65f,juce::MathConstants<float>::twoPi-.65f,true);p.startNewSubPath(0,-10);p.lineTo(0,-1);p.applyTransform(juce::AffineTransform::scale(s).translated(c.x,c.y));stroke(g,p,look.ink(),1.7f*s);return;}
    if(icon=="panel"){const float dir=toggled?-1.f:1.f;for(float y:{-4.f,3.f}){p.startNewSubPath(-5,y-2*dir);p.lineTo(0,y+2*dir);p.lineTo(5,y-2*dir);}p.applyTransform(juce::AffineTransform::scale(s).translated(c.x,c.y));stroke(g,p,look.ink(),1.7f*s);return;}
    if(icon=="settings"){constexpr int teeth=10;for(int i=0;i<teeth*4;++i){const float a=float(i)*juce::MathConstants<float>::twoPi/float(teeth*4)-juce::MathConstants<float>::halfPi;const float radius=(i%4==1||i%4==2)?10.f:7.7f;const auto pt=juce::Point<float>(std::cos(a)*radius,std::sin(a)*radius);if(i==0)p.startNewSubPath(pt);else p.lineTo(pt);}p.closeSubPath();p.applyTransform(juce::AffineTransform::scale(s).translated(c.x,c.y));stroke(g,p,look.ink(),1.65f*s);g.setColour(look.ink());g.drawEllipse(c.x-3.2f*s,c.y-3.2f*s,6.4f*s,6.4f*s,1.65f*s);return;}
    text(g,caption,r,15.f,look.ink(),juce::Justification::centred);
}


struct ResponseBiquad {
    double b0=1.0,b1=0.0,b2=0.0,a1=0.0,a2=0.0;
};

ResponseBiquad responseCoefficients(double sampleRate,float frequency,float q,float gainDb,int type){
    const double sr=juce::jmax(1.0,sampleRate);
    const double hz=juce::jlimit(10.0,sr*.48,double(frequency));
    const double qq=juce::jmax(.1,double(q));
    const double w=juce::MathConstants<double>::twoPi*hz/sr,c=std::cos(w),sn=std::sin(w),A=std::pow(10.0,double(gainDb)/40.0);
    double b0,b1,b2,a0,a1,a2;
    if(type==0){const double alpha=sn/(2.0*qq);b0=(1.0+c)*.5;b1=-(1.0+c);b2=b0;a0=1.0+alpha;a1=-2.0*c;a2=1.0-alpha;}
    else if(type==1){const double alpha=sn/(2.0*qq);b0=(1.0-c)*.5;b1=1.0-c;b2=b0;a0=1.0+alpha;a1=-2.0*c;a2=1.0-alpha;}
    else if(type==2){const double alpha=sn/(2.0*qq);b0=1.0+alpha*A;b1=-2.0*c;b2=1.0-alpha*A;a0=1.0+alpha/A;a1=-2.0*c;a2=1.0-alpha/A;}
    else {const double alpha=sn*.5*std::sqrt(2.0),beta=2.0*std::sqrt(A)*alpha;
        if(type==3){b0=A*((A+1.0)-(A-1.0)*c+beta);b1=2.0*A*((A-1.0)-(A+1.0)*c);b2=A*((A+1.0)-(A-1.0)*c-beta);a0=(A+1.0)+(A-1.0)*c+beta;a1=-2.0*((A-1.0)+(A+1.0)*c);a2=(A+1.0)+(A-1.0)*c-beta;}
        else {b0=A*((A+1.0)+(A-1.0)*c+beta);b1=-2.0*A*((A-1.0)+(A+1.0)*c);b2=A*((A+1.0)+(A-1.0)*c-beta);a0=(A+1.0)-(A-1.0)*c+beta;a1=2.0*((A-1.0)-(A+1.0)*c);a2=(A+1.0)-(A-1.0)*c-beta;}}
    return {b0/a0,b1/a0,b2/a0,a1/a0,a2/a0};
}

double responseMagnitude(const ResponseBiquad& c,double frequency,double sampleRate){
    const auto z=std::polar(1.0,-juce::MathConstants<double>::twoPi*frequency/sampleRate);
    const auto z2=z*z;
    const auto numerator=c.b0+c.b1*z+c.b2*z2;
    const auto denominator=1.0+c.a1*z+c.a2*z2;
    return std::abs(numerator/denominator);
}

float exactEqResponseDb(double sampleRate,float frequency,float hp,float f1,float g1,float f2,float g2,float f3,float g3,float lp){
    const double sr=sampleRate>1000.0?sampleRate:48000.0;
    const double f=juce::jlimit(1.0,sr*.499,double(frequency));
    double magnitude=1.0;
    magnitude*=responseMagnitude(responseCoefficients(sr,hp,.707f,0.f,0),f,sr);
    magnitude*=responseMagnitude(responseCoefficients(sr,f1,.707f,g1,3),f,sr);
    magnitude*=responseMagnitude(responseCoefficients(sr,f2,.7f,g2,2),f,sr);
    magnitude*=responseMagnitude(responseCoefficients(sr,f3,.707f,g3,4),f,sr);
    magnitude*=responseMagnitude(responseCoefficients(sr,lp,.707f,0.f,1),f,sr);
    return float(20.0*std::log10(juce::jmax(1.0e-9,magnitude)));
}
}

juce::Colour PocketLook::pick(juce::uint32,juce::uint32 dark,juce::uint32)const{return juce::Colour(dark);}
juce::Colour PocketLook::ink()const{return juce::Colour(0xfff0f0f0);}
juce::Colour PocketLook::muted()const{return juce::Colour(0xffa7a7a7);}
juce::Colour PocketLook::accent()const{return juce::Colour(0xffe8e8e8);}
juce::Font PocketLook::getTextButtonFont(juce::TextButton&,int){return uiFont(15);}
void PocketLook::drawButtonBackground(juce::Graphics& g,juce::Button&,const juce::Colour&,bool hover,bool down){auto r=g.getClipBounds().toFloat().reduced(2);auto fill=juce::Colour(hover?0xff3b3b3b:0xff292929);if(down)fill=fill.contrasting(.08f);g.setColour(fill);g.fillRoundedRectangle(r,8);g.setColour(juce::Colour(0xff555555));g.drawRoundedRectangle(r,8,.8f);}
void PocketLook::drawButtonText(juce::Graphics& g,juce::TextButton& b,bool,bool){
    auto r=b.getLocalBounds().toFloat();const auto name=b.getButtonText();const auto c=r.getCentre();const float s=juce::jmin(r.getWidth(),r.getHeight())/40.f;juce::Path p;
    if(name=="power"){p.addCentredArc(0,1,8,8,0,.65f,juce::MathConstants<float>::twoPi-.65f,true);p.startNewSubPath(0,-10);p.lineTo(0,-1);p.applyTransform(juce::AffineTransform::scale(s).translated(c.x,c.y));stroke(g,p,ink(),1.7f*s);return;}
    if(name=="panel"){const float dir=b.getToggleState()?-1.f:1.f;for(float y:{-4.f,3.f}){p.startNewSubPath(-5,y-2*dir);p.lineTo(0,y+2*dir);p.lineTo(5,y-2*dir);}p.applyTransform(juce::AffineTransform::scale(s).translated(c.x,c.y));stroke(g,p,ink().withAlpha(b.isEnabled()?1.f:.35f),1.7f*s);return;}
    if(name=="Learn"||name=="Listening"||name=="Try again"||name=="Done"){text(g,name,r,15.f,ink(),juce::Justification::centred);return;}
    constexpr int teeth=10;for(int i=0;i<teeth*4;++i){const float a=float(i)*juce::MathConstants<float>::twoPi/float(teeth*4)-juce::MathConstants<float>::halfPi;const float radius=(i%4==1||i%4==2)?10.f:7.7f;const auto pt=juce::Point<float>(std::cos(a)*radius,std::sin(a)*radius);if(i==0)p.startNewSubPath(pt);else p.lineTo(pt);}p.closeSubPath();p.applyTransform(juce::AffineTransform::scale(s).translated(c.x,c.y));stroke(g,p,ink(),1.65f*s);g.setColour(ink());g.drawEllipse(c.x-3.2f*s,c.y-3.2f*s,6.4f*s,6.4f*s,1.65f*s);
}

ModernDial::ModernDial(PocketLook& l,juce::String t,juce::String u,bool isInteger):look(l),title(std::move(t)),unit(std::move(u)),integer(isInteger){setSliderStyle(juce::Slider::RotaryVerticalDrag);setTextBoxStyle(juce::Slider::NoTextBox,false,0,0);setName(title);setWantsKeyboardFocus(true);}
void ModernDial::paint(juce::Graphics& g){
    drawDialBody(g,getLocalBounds().toFloat(),look,title,unit,float(getValue()),float(valueToProportionOfLength(getValue())),integer);
}

void ReflectionsGraph::paint(juce::Graphics& g){
    const auto all=getLocalBounds().toFloat();g.setColour(juce::Colour(0xff111111));g.fillRoundedRectangle(all,14.f);
    const juce::Rectangle<float> p(all.getX()+76.f,all.getY()+58.f,all.getWidth()-102.f,all.getHeight()-112.f);
    text(g,"EARLY REFLECTIONS",{all.getX()+32,all.getY()+15,300,26},16,juce::Colour(0xfff0f0f0));text(g,"IMPULSE RESPONSE",{all.getRight()-330,all.getY()+15,300,26},14,juce::Colour(0xffa7a7a7),juce::Justification::centredRight);
    g.setColour(juce::Colour(0xff363636));for(int i=0;i<=6;++i){const float x=p.getX()+p.getWidth()*float(i)/6;g.drawVerticalLine(juce::roundToInt(x),p.getY(),p.getBottom());text(g,i==0?"0 ms":juce::String(i*50),{x-24,p.getBottom()+6,48,18},14,juce::Colour(0xffa7a7a7),juce::Justification::centred);}
    for(int i=0;i<=3;++i){const float y=p.getY()+p.getHeight()*float(i)/3;g.drawHorizontalLine(juce::roundToInt(y),p.getX(),p.getRight());text(g,i==0?"0 dB":juce::String(-i*20),{all.getX()+12,y-9,52,18},14,juce::Colour(0xffa7a7a7),juce::Justification::centredRight);}
    auto draw=[&](const early::TapModel& model,juce::Colour colour,float width,float scale){g.setColour(colour);for(int i=0;i<model.count;++i){const auto& t=model.taps[size_t(i)];const float db=juce::Decibels::gainToDecibels(t.gain*scale,-60.f);if(db<=-60.f)continue;const float x=p.getX()+p.getWidth()*juce::jlimit(0.f,1.f,t.delayMs/300.f);const float y=juce::jmap(juce::jlimit(-60.f,0.f,db),0.f,-60.f,p.getY(),p.getBottom());g.drawLine(x,p.getBottom(),x,y,width);}};
    const float m=processor.parameters.getRawParameterValue("mix")->load()*.01f;if(m<.999f){const float db=juce::Decibels::gainToDecibels(1.f-m,-60.f);const float y=juce::jmap(db,0.f,-60.f,p.getY(),p.getBottom());g.setColour(juce::Colour(0xfff0f0f0));g.drawLine(p.getX(),p.getBottom(),p.getX(),y,1.8f);}
    const auto target=processor.getTargetSummary();if(target.taps.count>0)draw(target.taps,juce::Colour(0x668f8f8f),1.f,m);draw(processor.getDisplayedModel(),juce::Colour(0xfff0f0f0),2.f,m);
}

EqualizerGraph::EqualizerGraph(EarlyPocketAudioProcessor& p):processor(p){}
juce::Rectangle<float> EqualizerGraph::plot()const{
    const auto bounds=getLocalBounds().toFloat();
    const float scale=juce::jmax(.45f,bounds.getWidth()/1572.f);
    return bounds.reduced(60.f*scale,28.f*scale);
}
float EqualizerGraph::xForHz(float hz)const{const auto p=plot();return p.getX()+p.getWidth()*std::log(juce::jlimit(20.f,20000.f,hz)/20.f)/std::log(1000.f);}
float EqualizerGraph::yForDb(float db)const{const auto p=plot();return juce::jmap(juce::jlimit(-12.f,12.f,db),12.f,-12.f,p.getY(),p.getBottom());}
void EqualizerGraph::refreshSpectrum(){
    std::fill(fftData.begin(),fftData.end(),0.f);
    processor.copySpectrumInput(fftData.data(),fftSize);
    windowing.multiplyWithWindowingTable(fftData.data(),fftSize);
    fft.performFrequencyOnlyForwardTransform(fftData.data(),true);
    const float sr=float(juce::jmax(1.0,processor.getSampleRate()));
    for(int i=0;i<int(spectrum.size());++i){
        const float hz=20.f*std::pow(1000.f,float(i)/float(spectrum.size()-1));
        const int bin=juce::jlimit(1,fftSize/2,juce::roundToInt(hz*float(fftSize)/sr));
        // Hann coherent-gain compensation: a full-scale bin-centred sine reads close to 0 dBFS.
        const float mag=fftData[size_t(bin)]*(4.f/float(fftSize));
        const float db=juce::Decibels::gainToDecibels(mag,-90.f);
        const float norm=juce::jlimit(0.f,1.f,(db+84.f)/72.f);
        smoothed[size_t(i)]=.72f*smoothed[size_t(i)]+.28f*norm;
        spectrum[size_t(i)]=smoothed[size_t(i)];
    }
}
void EqualizerGraph::paint(juce::Graphics& g){
    const auto bounds=getLocalBounds().toFloat();const float ui=juce::jmax(.45f,bounds.getWidth()/1572.f);
    g.setColour(juce::Colour(0xff202020));g.fillRoundedRectangle(bounds,16.f*ui);g.setColour(juce::Colour(0xff505050));g.drawRoundedRectangle(bounds,16.f*ui,juce::jmax(.8f,ui));const auto p=plot();
    g.setColour(juce::Colour(0xff363636));for(float hz:{20.f,50.f,100.f,200.f,500.f,1000.f,2000.f,5000.f,10000.f,20000.f})g.drawVerticalLine(juce::roundToInt(xForHz(hz)),p.getY(),p.getBottom());for(float db:{-12.f,0.f,12.f})g.drawHorizontalLine(juce::roundToInt(yForDb(db)),p.getX(),p.getRight());
    juce::Path spec;const float specBase=p.getBottom()-4.f*ui,specHeight=p.getHeight()*.34f;for(int i=0;i<int(spectrum.size());++i){const float x=p.getX()+p.getWidth()*float(i)/float(spectrum.size()-1);const float y=specBase-spectrum[size_t(i)]*specHeight;if(i==0)spec.startNewSubPath(x,y);else spec.lineTo(x,y);}juce::Path fill=spec;fill.lineTo(p.getRight(),specBase);fill.lineTo(p.getX(),specBase);fill.closeSubPath();g.setColour(juce::Colour(0x1caaaaaa));g.fillPath(fill);g.setColour(juce::Colour(0x668f8f8f));g.strokePath(spec,juce::PathStrokeType(juce::jmax(.8f,ui)));
    const float hp=processor.parameters.getRawParameterValue("hp")->load(),lp=processor.parameters.getRawParameterValue("lp")->load();
    const float f1=processor.parameters.getRawParameterValue("eq1Freq")->load(),f2=processor.parameters.getRawParameterValue("eq2Freq")->load(),f3=processor.parameters.getRawParameterValue("eq3Freq")->load();const float a1=processor.parameters.getRawParameterValue("eq1Gain")->load(),a2=processor.parameters.getRawParameterValue("eq2Gain")->load(),a3=processor.parameters.getRawParameterValue("eq3Gain")->load();
    const double sr=processor.getSampleRate()>1000.0?processor.getSampleRate():48000.0;
    juce::Path curve;for(int x=0;x<=int(p.getWidth());++x){const float f=20.f*std::pow(1000.f,float(x)/p.getWidth());const float db=exactEqResponseDb(sr,f,hp,f1,a1,f2,a2,f3,a3,lp);const float y=yForDb(db);if(x==0)curve.startNewSubPath(p.getX(),y);else curve.lineTo(p.getX()+float(x),y);}juce::Path area=curve;area.lineTo(p.getRight(),yForDb(0));area.lineTo(p.getX(),yForDb(0));area.closeSubPath();g.setColour(juce::Colour(0x148f8f8f));g.fillPath(area);g.setColour(juce::Colour(0xffe8e8e8));g.strokePath(curve,juce::PathStrokeType(juce::jmax(1.2f,1.8f*ui)));
    const float frequencies[3]{f1,f2,f3},gains[3]{a1,a2,a3};const float node=juce::jmax(6.f,7.f*ui);for(int i=0;i<3;++i){const float x=xForHz(frequencies[i]),y=yForDb(gains[i]);g.setColour(juce::Colour(0xff111111));g.fillEllipse(x-node,y-node,node*2,node*2);g.setColour(active==i?juce::Colours::white:juce::Colour(0xfff0f0f0));g.drawEllipse(x-node,y-node,node*2,node*2,juce::jmax(1.2f,1.6f*ui));}
    for(auto [f,label]:{std::pair<float,const char*>{20,"20"},{50,"50"},{100,"100"},{200,"200"},{500,"500"},{1000,"1k"},{2000,"2k"},{5000,"5k"},{10000,"10k"},{20000,"20k"}})text(g,label,{xForHz(f)-22.f*ui,p.getBottom()+5.f*ui,44.f*ui,18.f*ui},juce::jmax(10.f,13.f*ui),juce::Colour(0xffa7a7a7),juce::Justification::centred);
    for(float db:{12.f,0.f,-12.f})text(g,juce::String(db>0?"+":"")+juce::String(juce::roundToInt(db)),{p.getX()-48.f*ui,yForDb(db)-9.f*ui,42.f*ui,18.f*ui},juce::jmax(10.f,12.f*ui),juce::Colour(0xffa7a7a7),juce::Justification::centredRight);
}
void EqualizerGraph::mouseDown(const juce::MouseEvent& e){
    if(activeFrequencyParameter)activeFrequencyParameter->endChangeGesture();
    if(activeGainParameter)activeGainParameter->endChangeGesture();
    activeFrequencyParameter=nullptr;activeGainParameter=nullptr;active=-1;
    float best=std::numeric_limits<float>::max();
    const char* ids[]={"eq1Freq","eq2Freq","eq3Freq"};const char* gains[]={"eq1Gain","eq2Gain","eq3Gain"};
    const float hitRadius=juce::jmax(18.f,28.f*juce::jmax(.45f,float(getWidth())/1572.f));
    for(int i=0;i<3;++i){const float f=processor.parameters.getRawParameterValue(ids[i])->load(),db=processor.parameters.getRawParameterValue(gains[i])->load();const float d=e.position.getDistanceFrom({xForHz(f),yForDb(db)});if(d<best){best=d;active=i;}}
    if(best>hitRadius){active=-1;return;}
    activeFrequencyParameter=processor.parameters.getParameter(ids[active]);
    activeGainParameter=processor.parameters.getParameter(gains[active]);
    if(activeFrequencyParameter)activeFrequencyParameter->beginChangeGesture();
    if(activeGainParameter)activeGainParameter->beginChangeGesture();
    repaint();
}
void EqualizerGraph::mouseDrag(const juce::MouseEvent& e){moveNode(e.position);}
void EqualizerGraph::mouseUp(const juce::MouseEvent&){
    if(activeFrequencyParameter)activeFrequencyParameter->endChangeGesture();
    if(activeGainParameter)activeGainParameter->endChangeGesture();
    activeFrequencyParameter=nullptr;activeGainParameter=nullptr;active=-1;repaint();
}
void EqualizerGraph::moveNode(juce::Point<float> pos){
    if(active<0||!activeFrequencyParameter||!activeGainParameter)return;
    const auto p=plot();
    const float x=juce::jlimit(0.f,1.f,(pos.x-p.getX())/p.getWidth());
    const float requestedFrequency=20.f*std::pow(1000.f,x);
    const float minFrequency=activeFrequencyParameter->convertFrom0to1(0.f),maxFrequency=activeFrequencyParameter->convertFrom0to1(1.f);
    const float frequency=juce::jlimit(juce::jmin(minFrequency,maxFrequency),juce::jmax(minFrequency,maxFrequency),requestedFrequency);
    const float requestedDb=juce::jmap(juce::jlimit(p.getY(),p.getBottom(),pos.y),p.getY(),p.getBottom(),12.f,-12.f);
    const float minGain=activeGainParameter->convertFrom0to1(0.f),maxGain=activeGainParameter->convertFrom0to1(1.f);
    const float db=juce::jlimit(juce::jmin(minGain,maxGain),juce::jmax(minGain,maxGain),requestedDb);
    activeFrequencyParameter->setValueNotifyingHost(activeFrequencyParameter->convertTo0to1(frequency));
    activeGainParameter->setValueNotifyingHost(activeGainParameter->convertTo0to1(db));
    repaint();
}

EarlyPocketAudioProcessorEditor::EarlyPocketAudioProcessorEditor(EarlyPocketAudioProcessor& p):AudioProcessorEditor(&p),plugin(p),graph(p),eq(p){
    setLookAndFeel(&look);setOpaque(true);setResizable(true,true);
    addAndMakeVisible(graph);addAndMakeVisible(eq);
    expanded=p.editorExpanded.load();eq.setVisible(expanded);const float h=expanded?expandedHeight:collapsedHeight;const int w=juce::jlimit(minimumEditorWidth,maximumEditorWidth,p.editorWidth.load());getConstrainer()->setSizeLimits(minimumEditorWidth,juce::roundToInt(float(minimumEditorWidth)*h/designWidth),maximumEditorWidth,juce::roundToInt(float(maximumEditorWidth)*h/designWidth));getConstrainer()->setFixedAspectRatio(designWidth/h);setSize(w,juce::roundToInt(float(w)*h/designWidth));startTimerHz(20);resized();
}
EarlyPocketAudioProcessorEditor::~EarlyPocketAudioProcessorEditor(){stopTimer();setVisible(false);removeFromDesktop();removeAllChildren();setLookAndFeel(nullptr);}
juce::Rectangle<int> EarlyPocketAudioProcessorEditor::scaled(float x,float y,float w,float h)const{const float s=float(getWidth())/designWidth;return{juce::roundToInt(x*s),juce::roundToInt(y*s),juce::roundToInt(w*s),juce::roundToInt(h*s)};}
void EarlyPocketAudioProcessorEditor::resized(){graph.setBounds(scaled(39,98,1572,330));eq.setBounds(scaled(39,755,1572,286));eq.setVisible(expanded);blurArea=scaled(24,84,1624,expanded?expandedHeight-84:collapsedHeight-84);plugin.editorWidth.store(getWidth());}
void EarlyPocketAudioProcessorEditor::paint(juce::Graphics& g){const float s=float(getWidth())/designWidth;g.fillAll(juce::Colour(0xff171717));text(g,"EARLY POCKET",{580*s,12*s,512*s,58*s},46*s,look.ink(),juce::Justification::centred);g.setColour(juce::Colour(0xff444444));g.drawLine(40*s,83*s,1608*s,83*s,1*s);panel(g,scaled(39,441,1572,232).toFloat());text(g,"EQUALIZER",scaled(130,691,250,46).toFloat(),18*s,look.ink());if(!expanded)return;}

void EarlyPocketAudioProcessorEditor::setExpanded(bool e){expanded=e;plugin.editorExpanded=e;eq.setVisible(e);const float h=e?expandedHeight:collapsedHeight;getConstrainer()->setSizeLimits(minimumEditorWidth,juce::roundToInt(float(minimumEditorWidth)*h/designWidth),maximumEditorWidth,juce::roundToInt(float(maximumEditorWidth)*h/designWidth));getConstrainer()->setFixedAspectRatio(designWidth/h);setSize(getWidth(),juce::roundToInt(float(getWidth())*h/designWidth));resized();repaint();}
void EarlyPocketAudioProcessorEditor::showSettings(){juce::PopupMenu root,themes;themes.addItem(1,"Solid Dark",true,true);root.addSubMenu("Темы",themes);auto area=scaled(1465,16,60,53);area.setPosition(localPointToGlobal(area.getPosition()));root.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this).withTargetScreenArea(area),[](int){});}
void EarlyPocketAudioProcessorEditor::captureBlurSnapshot(){if(capturingBlur||blurArea.isEmpty())return;capturingBlur=true;auto source=createComponentSnapshot(blurArea,true,1.f);capturingBlur=false;if(!source.isValid())return;const int w=juce::jmax(16,source.getWidth()/6),h=juce::jmax(16,source.getHeight()/6);juce::Image small(juce::Image::ARGB,w,h,true);{juce::Graphics sg(small);sg.setImageResamplingQuality(juce::Graphics::highResamplingQuality);sg.drawImage(source,juce::Rectangle<float>(0,0,float(w),float(h)),juce::RectanglePlacement::stretchToFit);}juce::Image soft(juce::Image::ARGB,w,h,true);juce::ImageConvolutionKernel kernel(9);kernel.createGaussianBlur(2.2f);kernel.applyToImage(soft,small,small.getBounds());blurredSnapshot=soft;}
void EarlyPocketAudioProcessorEditor::paintOverChildren(juce::Graphics& g){const char* ids[]{"roomSize","faces","roomShape","width","distance","mix"};const char* titles[]{"Room Size","Faces","Room Shape","Width","Distance","Mix"};const float positions[]{76.f,336.f,596.f,856.f,1116.f,1376.f};for(int i=0;i<6;++i){const float value=plugin.parameters.getRawParameterValue(ids[i])->load();const auto* parameter=plugin.parameters.getParameter(ids[i]);const float proportion=parameter!=nullptr?parameter->convertTo0to1(value):0.f;const bool integer=i==1;drawDialBody(g,scaled(positions[i],449,200,220).toFloat(),look,titles[i],integer?"":"%",value,proportion,integer);}const char* learnText="Learn";switch(plugin.getLearnState()){case EarlyPocketAudioProcessor::LearnState::capturing:case EarlyPocketAudioProcessor::LearnState::analyzing:learnText="Listening";break;case EarlyPocketAudioProcessor::LearnState::ready:learnText="Done";break;case EarlyPocketAudioProcessor::LearnState::insufficient:case EarlyPocketAudioProcessor::LearnState::error:learnText="Try again";break;default:break;}drawControlButton(g,scaled(1333,16,112,53).toFloat(),"",juce::String(learnText),false,look);drawControlButton(g,scaled(1465,16,60,53).toFloat(),"settings","",false,look);drawControlButton(g,scaled(1545,16,60,53).toFloat(),"power","",bypassTarget,look);drawControlButton(g,scaled(56,691,52,46).toFloat(),"panel","",expanded,look);if(capturingBlur)return;if(bypassTarget&&blurredSnapshot.isValid()){g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);g.drawImage(blurredSnapshot,blurArea.toFloat(),juce::RectanglePlacement::stretchToFit);g.setColour(juce::Colours::black.withAlpha(.18f));g.fillRoundedRectangle(blurArea.toFloat(),12.f);auto centre=blurArea.toFloat().translated(0,-float(blurArea.getHeight())*.05f);g.setColour(juce::Colours::black.withAlpha(.55f));g.setFont(uiFont(juce::jmax(34.f,float(getWidth())/18.f)));g.drawText("BYPASSED",centre.translated(0,2),juce::Justification::centred);text(g,"BYPASSED",centre,juce::jmax(34.f,float(getWidth())/18.f),juce::Colours::white,juce::Justification::centred);}}
void EarlyPocketAudioProcessorEditor::mouseDown(const juce::MouseEvent& e){const char* ids[]{"roomSize","faces","roomShape","width","distance","mix"};const float positions[]{76.f,336.f,596.f,856.f,1116.f,1376.f};for(int i=0;i<6;++i)if(scaled(positions[i],449,200,220).toFloat().contains(e.position)){activeDial=i;dragStartY=e.position.y;draggedParameter=plugin.parameters.getParameter(ids[i]);dragStartValue=plugin.parameters.getRawParameterValue(ids[i])->load();if(draggedParameter)draggedParameter->beginChangeGesture();return;}if(scaled(1333,16,112,53).toFloat().contains(e.position)){plugin.toggleLearn();return;}if(scaled(1465,16,60,53).toFloat().contains(e.position)){showSettings();return;}if(scaled(1545,16,60,53).toFloat().contains(e.position)){if(auto* p=plugin.parameters.getParameter("bypass")){p->beginChangeGesture();const auto* raw=plugin.parameters.getRawParameterValue("bypass");p->setValueNotifyingHost(p->convertTo0to1(raw->load()>.5f?0.f:1.f));p->endChangeGesture();}return;}if(scaled(56,691,52,46).toFloat().contains(e.position))setExpanded(!expanded);}
void EarlyPocketAudioProcessorEditor::mouseDrag(const juce::MouseEvent& e){if(activeDial<0||!draggedParameter)return;const float minimum=draggedParameter->convertFrom0to1(0.f),maximum=draggedParameter->convertFrom0to1(1.f),range=maximum-minimum;float sensitivity=range/juce::jmax(1.f,float(getHeight())*.32f);if(e.mods.isShiftDown())sensitivity*=.2f;const float value=juce::jlimit(minimum,maximum,dragStartValue+(dragStartY-e.position.y)*sensitivity);draggedParameter->setValueNotifyingHost(draggedParameter->convertTo0to1(value));repaint();}
void EarlyPocketAudioProcessorEditor::mouseUp(const juce::MouseEvent&){if(draggedParameter)draggedParameter->endChangeGesture();draggedParameter=nullptr;activeDial=-1;}
void EarlyPocketAudioProcessorEditor::timerCallback(){const int s=int(plugin.getLearnState());if(s!=learnState){learnState=s;repaint();}const bool nextBypass=plugin.parameters.getRawParameterValue("bypass")->load()>.5f||plugin.displayBypass.load(std::memory_order_relaxed);if(nextBypass!=bypassTarget){bypassTarget=nextBypass;if(bypassTarget)captureBlurSnapshot();else blurredSnapshot={};repaint();}eq.refreshSpectrum();if(expanded)eq.repaint();}
