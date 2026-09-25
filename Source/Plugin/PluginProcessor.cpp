#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace {
juce::NormalisableRange<float> logRange(float lo,float hi){return {lo,hi,0.0f,0.35f};}
}

EarlyPocketAudioProcessor::EarlyPocketAudioProcessor()
 :AudioProcessor(BusesProperties().withInput("Input",juce::AudioChannelSet::stereo(),true)
                                  .withInput("Sidechain",juce::AudioChannelSet::stereo(),false)
                                  .withOutput("Output",juce::AudioChannelSet::stereo(),true)),
  parameters(*this,nullptr,"EARLY_STATE",layout()),learnThread(*this){
 const char* ids[]={"roomSize","faces","roomShape","width","distance","mix","hp","eq1Freq","eq1Gain","eq2Freq","eq2Gain","eq3Freq","eq3Gain","lp","bypass","eqBypass","eq2Q"};
 for(size_t i=0;i<raw.size();++i)raw[i]=parameters.getRawParameterValue(ids[i]);
 startTimerHz(20);
}
EarlyPocketAudioProcessor::~EarlyPocketAudioProcessor(){stopTimer();}

juce::AudioProcessorValueTreeState::ParameterLayout EarlyPocketAudioProcessor::layout(){
 std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;
 p.push_back(std::make_unique<juce::AudioParameterFloat>("roomSize","Room Size",0.f,100.f,45.f));
 p.push_back(std::make_unique<juce::AudioParameterInt>("faces","Faces",1,16,6));
 p.push_back(std::make_unique<juce::AudioParameterFloat>("roomShape","Room Shape",0.f,100.f,20.f));
 p.push_back(std::make_unique<juce::AudioParameterFloat>("width","Width",0.f,200.f,100.f));
 p.push_back(std::make_unique<juce::AudioParameterFloat>("distance","Distance",0.f,100.f,35.f));
 p.push_back(std::make_unique<juce::AudioParameterFloat>("mix","Mix",0.f,100.f,100.f));
 p.push_back(std::make_unique<juce::AudioParameterFloat>("hp","ER High-pass",logRange(20,1000),20));
 p.push_back(std::make_unique<juce::AudioParameterFloat>("eq1Freq","EQ 1 Frequency",logRange(40,1000),180));
 p.push_back(std::make_unique<juce::AudioParameterFloat>("eq1Gain","EQ 1 Gain",-12.f,12.f,0.f));
 p.push_back(std::make_unique<juce::AudioParameterFloat>("eq2Freq","EQ 2 Frequency",logRange(200,5000),900));
 p.push_back(std::make_unique<juce::AudioParameterFloat>("eq2Gain","EQ 2 Gain",-12.f,12.f,0.f));
 p.push_back(std::make_unique<juce::AudioParameterFloat>("eq3Freq","EQ 3 Frequency",logRange(1000,16000),4500));
 p.push_back(std::make_unique<juce::AudioParameterFloat>("eq3Gain","EQ 3 Gain",-12.f,12.f,0.f));
 p.push_back(std::make_unique<juce::AudioParameterFloat>("lp","ER Low-pass",logRange(1000,20000),20000));
 p.push_back(std::make_unique<juce::AudioParameterBool>("bypass","Bypass",false));
 p.push_back(std::make_unique<juce::AudioParameterBool>("eqBypass","EQ Bypass",false));
 p.push_back(std::make_unique<juce::AudioParameterFloat>("eq2Q","EQ Mid Q",0.3f,6.0f,0.7f));
 return {p.begin(),p.end()};
}

early::Parameters EarlyPocketAudioProcessor::currentParameters() const noexcept {
    return {raw[0]->load()*0.01f, raw[2]->load()*0.01f,
            int(std::lround(raw[1]->load())), raw[3]->load()*0.01f, raw[4]->load()*0.01f};
}

void EarlyPocketAudioProcessor::prepareToPlay(double sr,int){
    currentRate=std::max(1.0,sr); engine.prepare(currentRate);
    captureLimit=int(currentRate*8.0);
    captureL.assign(size_t(captureLimit),0.0f); captureR.assign(size_t(captureLimit),0.0f);
    captureDryL.assign(size_t(captureLimit),0.0f); captureDryR.assign(size_t(captureLimit),0.0f);
    captureWrite=0; captureHasReference=false; captureStereo=false; captureDryStereo=false; captureStopRequested=false;
    analysisStarted=false; fitPending=false; fitApplying=false;
    audioLearnedGeneration=std::numeric_limits<std::uint64_t>::max();
    modelCacheValid=false; eqCacheValid=false; updateModelAndEq(); setLatencySamples(0);
}
void EarlyPocketAudioProcessor::reset(){engine.reset();captureWrite=0;captureStopRequested=false;}
bool EarlyPocketAudioProcessor::isBusesLayoutSupported(const BusesLayout& l)const{
    auto in=l.getMainInputChannelSet(),out=l.getMainOutputChannelSet();
    if((in!=juce::AudioChannelSet::mono()&&in!=juce::AudioChannelSet::stereo())
       ||(out!=juce::AudioChannelSet::mono()&&out!=juce::AudioChannelSet::stereo())||in.size()>out.size())return false;
    auto key=l.getChannelSet(true,1);
    return key.isDisabled()||key==juce::AudioChannelSet::mono()||key==juce::AudioChannelSet::stereo();
}
void EarlyPocketAudioProcessor::processBlock(juce::AudioBuffer<float>& b,juce::MidiBuffer&){process(b,false);}
void EarlyPocketAudioProcessor::processBlockBypassed(juce::AudioBuffer<float>& b,juce::MidiBuffer&){process(b,true);}

void EarlyPocketAudioProcessor::updateModelAndEq(){
    if (fitApplying.load(std::memory_order_acquire)) return;
    std::uint64_t generation=0;
    bool useLearned=false;
    {
        const juce::SpinLock::ScopedLockType lock(dataLock);
        generation=learnedGeneration; useLearned=learnedActive;
    }
    if(generation!=audioLearnedGeneration){
        const juce::SpinLock::ScopedLockType lock(dataLock);
        audioLearnedTaps=target.taps;
        audioLearnedReference=learnedReference;
        audioLearnedHighToneDb=0.5f*(target.toneDb[4]+target.toneDb[5]);
        useLearned=learnedActive;
        audioLearnedGeneration=learnedGeneration;
        modelCacheValid=false;
    }

    const std::array<float,5> modelValues{raw[0]->load(),raw[2]->load(),raw[1]->load(),raw[3]->load(),raw[4]->load()};
    if(!modelCacheValid||modelValues!=lastModelValues){
        const auto p=currentParameters();
        engine.setModel(useLearned&&audioLearnedTaps.count>0
            ? early::transformLearnedModel(audioLearnedTaps,p,audioLearnedReference,audioLearnedHighToneDb)
            : early::buildModel(p));
        lastModelValues=modelValues;modelCacheValid=true;
    }
    const std::array<float,9> eqValues{raw[6]->load(),raw[7]->load(),raw[8]->load(),raw[9]->load(),raw[10]->load(),raw[11]->load(),raw[12]->load(),raw[13]->load(),raw[16]->load()};
    if(!eqCacheValid||eqValues!=lastEqValues){
        early::EqSettings e;e.highPassHz=eqValues[0];e.frequency={eqValues[1],eqValues[3],eqValues[5]};e.gainDb={eqValues[2],eqValues[4],eqValues[6]};e.lowPassHz=eqValues[7];e.q[1]=eqValues[8];
        engine.setEq(e);lastEqValues=eqValues;eqCacheValid=true;
    }
}

void EarlyPocketAudioProcessor::process(juce::AudioBuffer<float>& b,bool hostBypass){
    juce::ScopedNoDenormals noDenormals;
    displayBypass.store(hostBypass,std::memory_order_relaxed);
    auto input=getBusBuffer(b,true,0);auto output=getBusBuffer(b,false,0);auto key=getBusBuffer(b,true,1);
    updateModelAndEq();
    const int inCh=input.getNumChannels(),outCh=output.getNumChannels();if(inCh==0||outCh==0)return;
    const bool capture=learnState.load()==LearnState::capturing;
    const bool useKey=key.getNumChannels()>0;
    const auto* inL=input.getReadPointer(0);const auto* inR=inCh>1?input.getReadPointer(1):nullptr;
    auto* outL=output.getWritePointer(0);auto* outR=outCh>1?output.getWritePointer(1):nullptr;
    const auto* kl=useKey?key.getReadPointer(0):nullptr;const auto* kr=useKey&&key.getNumChannels()>1?key.getReadPointer(1):kl;
    int pos=captureWrite.load();auto spectrumPos=spectrumWrite.load(std::memory_order_relaxed);
    if(capture&&pos==0){captureStereo=inCh>1;captureHasReference=useKey;captureDryStereo=useKey&&key.getNumChannels()>1;}
    for(int n=0;n<output.getNumSamples();++n){
        const float sourceL=inL[n],sourceR=inR?inR[n]:sourceL;
        spectrumRing[size_t(spectrumPos%spectrumSize)].store(0.5f*(sourceL+sourceR),std::memory_order_relaxed);++spectrumPos;
        if(capture&&pos<captureLimit){
            captureL[size_t(pos)]=sourceL;captureR[size_t(pos)]=sourceR;
            if(useKey){captureDryL[size_t(pos)]=kl[n];captureDryR[size_t(pos)]=kr[n];}
            ++pos;
        }
        auto y=engine.process(sourceL,sourceR,raw[5]->load()*0.01f,
                              hostBypass||raw[14]->load()>0.5f,raw[15]->load()>0.5f);
        if(outR){outL[n]=y[0];outR[n]=y[1];}else outL[n]=0.5f*(y[0]+y[1]);
    }
    spectrumWrite.store(spectrumPos,std::memory_order_release);
    if(capture){
        captureWrite.store(pos,std::memory_order_release);
        const bool reachedLimit=pos>=captureLimit;
        const bool stopRequested=captureStopRequested.exchange(false,std::memory_order_acq_rel);
        if(reachedLimit||stopRequested){
            if(pos>=int(currentRate*2.0)){learnState.store(LearnState::analyzing,std::memory_order_release);learnThread.wake();}
            else learnState.store(LearnState::insufficient,std::memory_order_release);
        }
    }
}

void EarlyPocketAudioProcessor::toggleLearn(){
    const auto s=learnState.load();
    if(s==LearnState::capturing){
        if(captureWrite.load(std::memory_order_acquire)>=int(currentRate*2.0)) captureStopRequested.store(true,std::memory_order_release);
        else learnState.store(LearnState::insufficient,std::memory_order_release);
        return;
    }
    if(s==LearnState::analyzing)return;
    fitPending=false;analysisStarted=false;captureWrite=0;captureHasReference=false;captureStereo=false;captureDryStereo=false;captureStopRequested=false;doneAtMs=0;learnState=LearnState::capturing;
}
void EarlyPocketAudioProcessor::LearnThread::run(){while(!threadShouldExit()){event.wait(500);if(threadShouldExit())break;if(owner.learnState.load()==LearnState::analyzing && !owner.analysisStarted.exchange(true))owner.analyzeCapture();}}
void EarlyPocketAudioProcessor::analyzeCapture(){
    try{
        const int n=captureWrite.load();
        const bool useDry=captureHasReference.load();
        auto summary=analyzer.analyze(captureL.data(),captureStereo.load()?captureR.data():nullptr,
                                      useDry?captureDryL.data():nullptr,
                                      useDry&&captureDryStereo.load()?captureDryR.data():nullptr,
                                      n,currentRate);
        auto fit=analyzer.fit(summary);
        {const juce::SpinLock::ScopedLockType lock(dataLock);pendingTarget=summary;pendingFit=fit;}
        fitPending=true;
    }catch(...){learnState=LearnState::error;}
}

void EarlyPocketAudioProcessor::timerCallback(){if(fitPending.exchange(false))applyFit();if(learnState.load()==LearnState::ready&&doneAtMs.load()>0&&juce::Time::getMillisecondCounterHiRes()-doneAtMs.load()>3000)learnState=LearnState::idle;}
void EarlyPocketAudioProcessor::applyFit(){
    early::FitResult fit;early::TargetSummary t;
    {const juce::SpinLock::ScopedLockType lock(dataLock);fit=pendingFit;t=pendingTarget;}
    if(!fit.valid){learnState=LearnState::insufficient;return;}
    fitApplying.store(true,std::memory_order_release);
    const std::array<const char*,8> ids{"roomSize","roomShape","width","distance","eq1Gain","eq2Gain","eq3Gain","faces"};
    const std::array<float,8> values{fit.parameters.roomSize*100.0f,fit.parameters.roomShape*100.0f,fit.parameters.width*100.0f,fit.parameters.pattern*100.0f,fit.eqGainDb[0],fit.eqGainDb[1],fit.eqGainDb[2],float(fit.parameters.faces)};
    for(size_t i=0;i<ids.size();++i)if(auto* p=parameters.getParameter(ids[i])){p->beginChangeGesture();p->setValueNotifyingHost(p->convertTo0to1(values[i]));p->endChangeGesture();}
    {
        const juce::SpinLock::ScopedLockType lock(dataLock);
        target=t;learnedReference=fit.parameters;learnedActive=true;++learnedGeneration;
    }
    doneAtMs=juce::Time::getMillisecondCounterHiRes();learnState=LearnState::ready;
    fitApplying.store(false,std::memory_order_release);
}

early::TapModel EarlyPocketAudioProcessor::getDisplayedModel()const{
    early::TargetSummary localTarget;early::Parameters reference;bool active=false;
    {const juce::SpinLock::ScopedLockType lock(dataLock);localTarget=target;reference=learnedReference;active=learnedActive;}
    const auto p=currentParameters();
    return active&&localTarget.taps.count>0
        ? early::transformLearnedModel(localTarget.taps,p,reference,0.5f*(localTarget.toneDb[4]+localTarget.toneDb[5]))
        : early::buildModel(p);
}
early::TargetSummary EarlyPocketAudioProcessor::getTargetSummary()const{const juce::SpinLock::ScopedLockType lock(dataLock);return target;}
juce::String EarlyPocketAudioProcessor::getFitQuality()const{const auto t=getTargetSummary();if(t.taps.count==0)return{};return t.confidence==early::Confidence::good?"GOOD":(t.confidence==early::Confidence::fair?"FAIR":"WEAK");}
void EarlyPocketAudioProcessor::copySpectrumInput(float* dst,int count)const noexcept{if(!dst||count<=0)return;const auto end=spectrumWrite.load(std::memory_order_acquire);for(int i=0;i<count;++i){const auto index=end>=std::uint64_t(count-i)?end-std::uint64_t(count-i):0;dst[i]=spectrumRing[size_t(index%spectrumSize)].load(std::memory_order_relaxed);}}
double EarlyPocketAudioProcessor::getTailLengthSeconds()const{const auto m=getDisplayedModel();float ms=0.0f;for(int i=0;i<m.count;++i){const auto&t=m.taps[size_t(i)];ms=std::max(ms,t.delayMs+t.diffusionMs+t.decorrelationMs);}return (double(ms)+60.0)*0.001;}

void EarlyPocketAudioProcessor::getStateInformation(juce::MemoryBlock& d){
    auto s=parameters.copyState();s.setProperty("profileVersion",3,nullptr);s.setProperty("uiWidth",editorWidth.load(),nullptr);s.setProperty("uiExpanded",editorExpanded.load(),nullptr);
    early::TargetSummary t;early::Parameters reference;bool active=false;
    {const juce::SpinLock::ScopedLockType lock(dataLock);t=target;reference=learnedReference;active=learnedActive;}
    s.setProperty("learnedActive",active,nullptr);s.setProperty("targetCount",t.taps.count,nullptr);s.setProperty("targetConfidence",int(t.confidence),nullptr);s.setProperty("targetOnsets",t.onsetCount,nullptr);
    s.setProperty("targetStereoMeasured",t.stereoMeasured,nullptr);s.setProperty("targetStereoWidth",t.stereoWidth,nullptr);s.setProperty("targetUsedDry",t.usedDryReference,nullptr);
    for(int k=0;k<6;++k)s.setProperty("targetTone"+juce::String(k),t.toneDb[size_t(k)],nullptr);
    for(int i=0;i<t.taps.count;++i){const auto&tap=t.taps.taps[size_t(i)];s.setProperty("targetDelay"+juce::String(i),tap.delayMs,nullptr);s.setProperty("targetGain"+juce::String(i),tap.gain,nullptr);s.setProperty("targetPan"+juce::String(i),tap.pan,nullptr);s.setProperty("targetLow"+juce::String(i),tap.lowGain,nullptr);s.setProperty("targetHigh"+juce::String(i),tap.highGain,nullptr);s.setProperty("targetDiffusion"+juce::String(i),tap.diffusionMs,nullptr);s.setProperty("targetSpread"+juce::String(i),tap.stereoSpread,nullptr);s.setProperty("targetDecor"+juce::String(i),tap.decorrelationMs,nullptr);}
    s.setProperty("learnRefSize",reference.roomSize,nullptr);s.setProperty("learnRefShape",reference.roomShape,nullptr);s.setProperty("learnRefFaces",reference.faces,nullptr);s.setProperty("learnRefWidth",reference.width,nullptr);s.setProperty("learnRefDistance",reference.pattern,nullptr);
    if(auto xml=s.createXml())copyXmlToBinary(*xml,d);
}
void EarlyPocketAudioProcessor::setStateInformation(const void* d,int n){
    if(auto x=getXmlFromBinary(d,n))if(x->hasTagName(parameters.state.getType())){
        auto s=juce::ValueTree::fromXml(*x);editorWidth=int(s.getProperty("uiWidth",1100));editorExpanded=bool(s.getProperty("uiExpanded",true));
        const int version=int(s.getProperty("profileVersion",0));early::TargetSummary t;early::Parameters reference{};bool active=false;
        if(version>=1){
            t.taps.count=juce::jlimit(0,early::maxTaps,int(s.getProperty("targetCount",0)));t.confidence=early::Confidence(juce::jlimit(0,2,int(s.getProperty("targetConfidence",0))));t.onsetCount=int(s.getProperty("targetOnsets",0));
            t.stereoMeasured=bool(s.getProperty("targetStereoMeasured",false));t.stereoWidth=float(s.getProperty("targetStereoWidth",1.0));t.usedDryReference=version>=2?bool(s.getProperty("targetUsedDry",false)):false;
            const int toneCount=version>=2?6:3;for(int k=0;k<toneCount;++k)t.toneDb[size_t(k)]=float(s.getProperty("targetTone"+juce::String(k),0.0));
            for(int i=0;i<t.taps.count;++i){auto&tap=t.taps.taps[size_t(i)];tap.delayMs=float(s.getProperty("targetDelay"+juce::String(i),0.0));tap.gain=float(s.getProperty("targetGain"+juce::String(i),0.0));tap.pathId=100+i;if(version>=2){tap.pan=float(s.getProperty("targetPan"+juce::String(i),0.0));tap.lowGain=float(s.getProperty("targetLow"+juce::String(i),1.0));tap.highGain=float(s.getProperty("targetHigh"+juce::String(i),1.0));tap.diffusionMs=float(s.getProperty("targetDiffusion"+juce::String(i),0.1));tap.stereoSpread=float(s.getProperty("targetSpread"+juce::String(i),0.16));tap.decorrelationMs=float(s.getProperty("targetDecor"+juce::String(i),0.0));}}
        }
        // Version 2's procedural Distance sounded backwards. Keep its saved
        // sound while exposing the corrected direction in new sessions.
        if(version==2 && !bool(s.getProperty("learnedActive",false))){
            auto distanceNode=s.getChildWithProperty("id","distance");
            if(distanceNode.isValid())distanceNode.setProperty("value",100.0f-float(distanceNode.getProperty("value",35.0f)),nullptr);
        }
        parameters.replaceState(s);
        if(version>=2){reference.roomSize=float(s.getProperty("learnRefSize",0.45));reference.roomShape=float(s.getProperty("learnRefShape",0.20));reference.faces=juce::jlimit(1,16,int(s.getProperty("learnRefFaces",6)));reference.width=float(s.getProperty("learnRefWidth",1.0));reference.pattern=float(s.getProperty("learnRefDistance",0.35));active=bool(s.getProperty("learnedActive",false))&&t.taps.count>0;}
        {
            const juce::SpinLock::ScopedLockType lock(dataLock);target=t;learnedReference=reference;learnedActive=active;++learnedGeneration;
        }
        if(t.taps.count>0)learnState=LearnState::ready;
    }
}
juce::AudioProcessorEditor* EarlyPocketAudioProcessor::createEditor(){return new EarlyPocketAudioProcessorEditor(*this);}
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter(){return new EarlyPocketAudioProcessor();}
