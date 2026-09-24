#pragma once
#include <JuceHeader.h>
#include "Core/EarlyEngine.h"
#include "Core/LearnAnalyzer.h"

class EarlyPocketAudioProcessor final : public juce::AudioProcessor, private juce::Timer {
public:
    enum class LearnState { idle, capturing, analyzing, ready, insufficient, error };
    EarlyPocketAudioProcessor();
    ~EarlyPocketAudioProcessor() override;
    void prepareToPlay(double,int) override;
    void releaseResources() override {}
    void reset() override;
    bool isBusesLayoutSupported(const BusesLayout&) const override;
    void processBlock(juce::AudioBuffer<float>&,juce::MidiBuffer&) override;
    void processBlockBypassed(juce::AudioBuffer<float>& b,juce::MidiBuffer& m) override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override{return true;}
    const juce::String getName() const override{return JucePlugin_Name;}
    bool acceptsMidi() const override{return false;} bool producesMidi() const override{return false;} bool isMidiEffect() const override{return false;}
    double getTailLengthSeconds() const override;
    int getNumPrograms() override{return 1;} int getCurrentProgram() override{return 0;} void setCurrentProgram(int) override{}
    const juce::String getProgramName(int) override{return{};} void changeProgramName(int,const juce::String&) override{}
    void getStateInformation(juce::MemoryBlock&) override; void setStateInformation(const void*,int) override;
    juce::AudioProcessorParameter* getBypassParameter() const override{return parameters.getParameter("bypass");}
    static juce::AudioProcessorValueTreeState::ParameterLayout layout();

    void toggleLearn();
    LearnState getLearnState() const noexcept{return learnState.load();}
    early::TapModel getDisplayedModel() const;
    early::TargetSummary getTargetSummary() const;
    juce::String getFitQuality() const;
    static constexpr int spectrumSize = 2048;
    void copySpectrumInput(float* destination, int count) const noexcept;
    juce::AudioProcessorValueTreeState parameters;
    // Mirrors processBlockBypassed() for hosts that bypass through the callback
    // instead of changing the exposed bypass parameter. Used by the editor only.
    std::atomic<bool> displayBypass{false};
    std::atomic<bool> editorExpanded{true}; std::atomic<int> editorWidth{1100};

private:
    class LearnThread final:public juce::Thread{
    public: explicit LearnThread(EarlyPocketAudioProcessor& p):Thread("Early Pocket Learn"),owner(p){startThread();}
      ~LearnThread() override{signalThreadShouldExit();event.signal();stopThread(3000);} void wake(){event.signal();} void run() override;
    private: EarlyPocketAudioProcessor& owner; juce::WaitableEvent event;
    };
    early::Engine engine; early::LearnAnalyzer analyzer;
    std::atomic<LearnState> learnState{LearnState::idle};
    std::vector<float> captureL,captureR; std::atomic<int> captureWrite{0}; std::atomic<bool> captureStereo{false}; int captureLimit=0; double currentRate=48000;
    std::array<std::atomic<float>,spectrumSize> spectrumRing{}; std::atomic<std::uint64_t> spectrumWrite{0};
    mutable juce::SpinLock dataLock; early::TargetSummary target{},pendingTarget{}; early::FitResult pendingFit{};
    std::atomic<bool> fitPending{false};
    std::atomic<double> doneAtMs{0.0};
    std::array<std::atomic<float>*,16> raw{};
    std::array<float,6> lastModelValues{};
    std::array<float,8> lastEqValues{};
    bool modelCacheValid=false,eqCacheValid=false;
    void process(juce::AudioBuffer<float>&,bool);
    void analyzeCapture(); void timerCallback() override; void updateModelAndEq(); void applyFit();
    LearnThread learnThread;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EarlyPocketAudioProcessor)
};
