#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace {
juce::NormalisableRange<float> logRange(float lo,float hi){return {lo,hi,0.0f,0.35f};}
}

EarlyPocketAudioProcessor::EarlyPocketAudioProcessor()
 :AudioProcessor(BusesProperties().withInput("Input",juce::AudioChannelSet::stereo(),true).withInput("Sidechain",juce::AudioChannelSet::stereo(),false).withOutput("Output",juce::AudioChannelSet::stereo(),true)),parameters(*this,nullptr,"EARLY_STATE",layout()),learnThread(*this){
 const char* ids[]={"roomSize","faces","roomShape","width","distance","mix","hp","eq1Freq","eq1Gain","eq2Freq","eq2Gain","eq3Freq","eq3Gain","lp","bypass"};for(size_t i=0;i<raw.size();++i)raw[i]=parameters.getRawParameterValue(ids[i]);startTimerHz(20);
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
 p.push_back(std::make_unique<juce::AudioParameterFloat>("eq1Freq","EQ 1 Frequency",logRange(40,1000),75));p.push_back(std::make_unique<juce::AudioParameterFloat>("eq1Gain","EQ 1 Gain",-12.f,12.f,-6.f));
 p.push_back(std::make_unique<juce::AudioParameterFloat>("eq2Freq","EQ 2 Frequency",logRange(200,5000),250));p.push_back(std::make_unique<juce::AudioParameterFloat>("eq2Gain","EQ 2 Gain",-12.f,12.f,5.f));
 p.push_back(std::make_unique<juce::AudioParameterFloat>("eq3Freq","EQ 3 Frequency",logRange(1000,16000),8500));p.push_back(std::make_unique<juce::AudioParameterFloat>("eq3Gain","EQ 3 Gain",-12.f,12.f,-6.f));
 p.push_back(std::make_unique<juce::AudioParameterFloat>("lp","ER Low-pass",logRange(1000,20000),20000));p.push_back(std::make_unique<juce::AudioParameterBool>("bypass","Bypass",false));return {p.begin(),p.end()};
}

void EarlyPocketAudioProcessor::prepareToPlay(double sr,int){currentRate=std::max(1.0,sr);engine.prepare(currentRate);captureLimit=int(currentRate*8.0);captureL.assign(size_t(captureLimit),0);captureR.assign(size_t(captureLimit),0);captureWrite=0;modelCacheValid=false;eqCacheValid=false;updateModelAndEq();setLatencySamples(0);}
void EarlyPocketAudioProcessor::reset(){engine.reset();captureWrite=0;}
bool EarlyPocketAudioProcessor::isBusesLayoutSupported(const BusesLayout& l)const{auto in=l.getMainInputChannelSet(),out=l.getMainOutputChannelSet();if((in!=juce::AudioChannelSet::mono()&&in!=juce::AudioChannelSet::stereo())||(out!=juce::AudioChannelSet::mono()&&out!=juce::AudioChannelSet::stereo())||in.size()>out.size())return false;auto key=l.getChannelSet(true,1);return key.isDisabled()||key==juce::AudioChannelSet::mono()||key==juce::AudioChannelSet::stereo();}
void EarlyPocketAudioProcessor::processBlock(juce::AudioBuffer<float>& b,juce::MidiBuffer&){process(b,false);}
void EarlyPocketAudioProcessor::processBlockBypassed(juce::AudioBuffer<float>& b,juce::MidiBuffer&){process(b,true);}

void EarlyPocketAudioProcessor::updateModelAndEq(){
    const std::array<float,5> modelValues{raw[0]->load(),raw[2]->load(),raw[1]->load(),raw[3]->load(),raw[4]->load()};
    if(!modelCacheValid||modelValues!=lastModelValues){
        early::Parameters p{modelValues[0]*.01f,modelValues[1]*.01f,int(std::lround(modelValues[2])),modelValues[3]*.01f,modelValues[4]*.01f};
        engine.setModel(early::buildModel(p));
        lastModelValues=modelValues;modelCacheValid=true;
    }
    const std::array<float,8> eqValues{raw[6]->load(),raw[7]->load(),raw[8]->load(),raw[9]->load(),raw[10]->load(),raw[11]->load(),raw[12]->load(),raw[13]->load()};
    if(!eqCacheValid||eqValues!=lastEqValues){
        early::EqSettings e;e.highPassHz=eqValues[0];e.frequency={eqValues[1],eqValues[3],eqValues[5]};e.gainDb={eqValues[2],eqValues[4],eqValues[6]};e.lowPassHz=eqValues[7];
        engine.setEq(e);
        lastEqValues=eqValues;eqCacheValid=true;
    }
}

void EarlyPocketAudioProcessor::process(juce::AudioBuffer<float>& b,bool hostBypass){juce::ScopedNoDenormals noDenormals;displayBypass.store(hostBypass,std::memory_order_relaxed);auto input=getBusBuffer(b,true,0);auto output=getBusBuffer(b,false,0);auto key=getBusBuffer(b,true,1);updateModelAndEq();const int inCh=input.getNumChannels(),outCh=output.getNumChannels();if(inCh==0||outCh==0)return;const bool capture=learnState.load()==LearnState::capturing;const bool useKey=key.getNumChannels()>0;const auto* inL=input.getReadPointer(0);const auto* inR=inCh>1?input.getReadPointer(1):nullptr;auto* outL=output.getWritePointer(0);auto* outR=outCh>1?output.getWritePointer(1):nullptr;const auto* kl=useKey?key.getReadPointer(0):nullptr;const auto* kr=useKey&&key.getNumChannels()>1?key.getReadPointer(1):kl;int pos=captureWrite.load();auto spectrumPos=spectrumWrite.load(std::memory_order_relaxed);if(capture&&pos==0)captureStereo=useKey?key.getNumChannels()>1:inCh>1;for(int n=0;n<output.getNumSamples();++n){const float sourceL=inL[n],sourceR=inR?inR[n]:sourceL;spectrumRing[size_t(spectrumPos%spectrumSize)].store(.5f*(sourceL+sourceR),std::memory_order_relaxed);++spectrumPos;if(capture&&pos<captureLimit){captureL[size_t(pos)]=kl?kl[n]:sourceL;captureR[size_t(pos)]=kr?kr[n]:sourceR;++pos;}auto y=engine.process(sourceL,sourceR,raw[5]->load()*.01f,hostBypass||raw[14]->load()>.5f);if(outR){outL[n]=y[0];outR[n]=y[1];}else outL[n]=.5f*(y[0]+y[1]);}spectrumWrite.store(spectrumPos,std::memory_order_release);if(capture){captureWrite.store(pos);if(pos>=captureLimit){learnState=LearnState::analyzing;learnThread.wake();}}}

void EarlyPocketAudioProcessor::toggleLearn(){auto s=learnState.load();if(s==LearnState::capturing){if(captureWrite.load()>=int(currentRate*2)){learnState=LearnState::analyzing;learnThread.wake();}else learnState=LearnState::insufficient;return;}if(s==LearnState::analyzing)return;captureWrite=0;doneAtMs=0;learnState=LearnState::capturing;}
void EarlyPocketAudioProcessor::LearnThread::run(){while(!threadShouldExit()){event.wait(500);if(threadShouldExit())break;if(owner.learnState.load()==LearnState::analyzing)owner.analyzeCapture();}}
void EarlyPocketAudioProcessor::analyzeCapture(){try{const int n=captureWrite.load();auto summary=analyzer.analyze(captureL.data(),captureStereo.load()?captureR.data():nullptr,n,currentRate);auto fit=analyzer.fit(summary);{const juce::SpinLock::ScopedLockType lock(dataLock);pendingTarget=summary;pendingFit=fit;}fitPending=true;}catch(...){learnState=LearnState::error;}}

void EarlyPocketAudioProcessor::timerCallback(){if(fitPending.exchange(false))applyFit();if(learnState.load()==LearnState::ready&&doneAtMs.load()>0&&juce::Time::getMillisecondCounterHiRes()-doneAtMs.load()>3000)learnState=LearnState::idle;}
void EarlyPocketAudioProcessor::applyFit(){early::FitResult fit;early::TargetSummary t;{const juce::SpinLock::ScopedLockType lock(dataLock);fit=pendingFit;t=pendingTarget;}if(!fit.valid){learnState=LearnState::insufficient;return;}const std::array<const char*,8> ids{"roomSize","roomShape","width","distance","eq1Gain","eq2Gain","eq3Gain","faces"};const std::array<float,8> values{fit.parameters.roomSize*100,fit.parameters.roomShape*100,fit.parameters.width*100,fit.parameters.pattern*100,fit.eqGainDb[0],fit.eqGainDb[1],fit.eqGainDb[2],float(fit.parameters.faces)};for(size_t i=0;i<ids.size();++i)if(auto* p=parameters.getParameter(ids[i])){p->beginChangeGesture();p->setValueNotifyingHost(p->convertTo0to1(values[i]));p->endChangeGesture();}{const juce::SpinLock::ScopedLockType lock(dataLock);target=t;}doneAtMs=juce::Time::getMillisecondCounterHiRes();learnState=LearnState::ready;}

early::TapModel EarlyPocketAudioProcessor::getDisplayedModel()const{early::Parameters p{raw[0]->load()*.01f,raw[2]->load()*.01f,int(std::lround(raw[1]->load())),raw[3]->load()*.01f,raw[4]->load()*.01f};return early::buildModel(p);}
early::TargetSummary EarlyPocketAudioProcessor::getTargetSummary()const{const juce::SpinLock::ScopedLockType lock(dataLock);return target;}
juce::String EarlyPocketAudioProcessor::getFitQuality()const{const auto t=getTargetSummary();if(t.taps.count==0)return{};return t.confidence==early::Confidence::good?"GOOD":(t.confidence==early::Confidence::fair?"FAIR":"WEAK");}
void EarlyPocketAudioProcessor::copySpectrumInput(float* dst,int count)const noexcept{if(!dst||count<=0)return;const auto end=spectrumWrite.load(std::memory_order_acquire);for(int i=0;i<count;++i){const auto index=end>=std::uint64_t(count-i)?end-std::uint64_t(count-i):0;dst[i]=spectrumRing[size_t(index%spectrumSize)].load(std::memory_order_relaxed);}}
double EarlyPocketAudioProcessor::getTailLengthSeconds()const{const auto m=getDisplayedModel();float ms=0;for(int i=0;i<m.count;++i)ms=std::max(ms,m.taps[size_t(i)].delayMs);return (double(ms)+50.0)*.001;}

void EarlyPocketAudioProcessor::getStateInformation(juce::MemoryBlock& d){auto s=parameters.copyState();s.setProperty("profileVersion",1,nullptr);s.setProperty("uiWidth",editorWidth.load(),nullptr);s.setProperty("uiExpanded",editorExpanded.load(),nullptr);const auto t=getTargetSummary();s.setProperty("targetCount",t.taps.count,nullptr);s.setProperty("targetConfidence",int(t.confidence),nullptr);s.setProperty("targetOnsets",t.onsetCount,nullptr);s.setProperty("targetStereoMeasured",t.stereoMeasured,nullptr);s.setProperty("targetStereoWidth",t.stereoWidth,nullptr);for(int k=0;k<3;++k)s.setProperty("targetTone"+juce::String(k),t.toneDb[size_t(k)],nullptr);for(int i=0;i<t.taps.count;++i){s.setProperty("targetDelay"+juce::String(i),t.taps.taps[size_t(i)].delayMs,nullptr);s.setProperty("targetGain"+juce::String(i),t.taps.taps[size_t(i)].gain,nullptr);}if(auto xml=s.createXml())copyXmlToBinary(*xml,d);}
void EarlyPocketAudioProcessor::setStateInformation(const void* d,int n){if(auto x=getXmlFromBinary(d,n))if(x->hasTagName(parameters.state.getType())){auto s=juce::ValueTree::fromXml(*x);editorWidth=int(s.getProperty("uiWidth",1100));editorExpanded=bool(s.getProperty("uiExpanded",true));early::TargetSummary t;if(int(s.getProperty("profileVersion",0))==1){t.taps.count=juce::jlimit(0,early::maxTaps,int(s.getProperty("targetCount",0)));t.confidence=early::Confidence(juce::jlimit(0,2,int(s.getProperty("targetConfidence",0))));t.onsetCount=int(s.getProperty("targetOnsets",0));t.stereoMeasured=bool(s.getProperty("targetStereoMeasured",false));t.stereoWidth=float(s.getProperty("targetStereoWidth",0));for(int k=0;k<3;++k)t.toneDb[size_t(k)]=float(s.getProperty("targetTone"+juce::String(k),0));for(int i=0;i<t.taps.count;++i){t.taps.taps[size_t(i)].delayMs=float(s.getProperty("targetDelay"+juce::String(i),0));t.taps.taps[size_t(i)].gain=float(s.getProperty("targetGain"+juce::String(i),0));}}{const juce::SpinLock::ScopedLockType lock(dataLock);target=t;}parameters.replaceState(s);if(t.taps.count>0)learnState=LearnState::ready;}}
juce::AudioProcessorEditor* EarlyPocketAudioProcessor::createEditor(){return new EarlyPocketAudioProcessorEditor(*this);}
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter(){return new EarlyPocketAudioProcessor();}
