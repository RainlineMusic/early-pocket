#pragma once
#include <JuceHeader.h>
#include <juce_dsp/juce_dsp.h>
#include "PluginProcessor.h"

enum class PocketTheme { SolidDark };

class PocketLook final : public juce::LookAndFeel_V4 {
public:
    PocketTheme theme = PocketTheme::SolidDark;
    bool isDark() const { return true; }
    bool hasGlow() const { return false; }
    juce::Colour pick(juce::uint32 neon,juce::uint32 dark,juce::uint32 white) const;
    juce::Colour ink() const;
    juce::Colour muted() const;
    juce::Colour accent() const;
    juce::Font getTextButtonFont(juce::TextButton&,int) override;
    void drawButtonBackground(juce::Graphics&,juce::Button&,const juce::Colour&,bool,bool) override;
    void drawButtonText(juce::Graphics&,juce::TextButton&,bool,bool) override;
};

class ModernDial final : public juce::Slider {
public:
    ModernDial(PocketLook&,juce::String,juce::String,juce::String,bool integer=false);
    void paint(juce::Graphics&) override;
    float dialProportion() { return float(valueToProportionOfLength(getValue())); }
private:
    PocketLook& look;
    juce::String title,subtitle,unit;
    bool integer=false;
};

class ReflectionsGraph final : public juce::Component {
public:
    explicit ReflectionsGraph(EarlyPocketAudioProcessor& p):processor(p){}
    void paint(juce::Graphics&) override;
private: EarlyPocketAudioProcessor& processor;
};

class EqualizerGraph final : public juce::Component {
public:
    explicit EqualizerGraph(EarlyPocketAudioProcessor&);
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void refreshSpectrum();
private:
    static constexpr int fftOrder = 11;
    static constexpr int fftSize = 1 << fftOrder;
    EarlyPocketAudioProcessor& processor;
    juce::dsp::FFT fft{fftOrder};
    juce::dsp::WindowingFunction<float> windowing{fftSize,juce::dsp::WindowingFunction<float>::hann,false};
    // JUCE frequency-only FFT requires 2 * fftSize floats even though only
    // the first fftSize entries contain input samples.
    std::array<float,fftSize * 2> fftData{};
    std::array<float,128> spectrum{};
    std::array<float,128> smoothed{};
    int active=-1;
    juce::RangedAudioParameter* activeFrequencyParameter=nullptr;
    juce::RangedAudioParameter* activeGainParameter=nullptr;
    juce::Rectangle<float> plot() const;
    float xForHz(float) const;
    float yForDb(float) const;
    void moveNode(juce::Point<float>);
};

class EarlyPocketAudioProcessorEditor final : public juce::AudioProcessorEditor,private juce::Timer {
public:
    explicit EarlyPocketAudioProcessorEditor(EarlyPocketAudioProcessor&);
    ~EarlyPocketAudioProcessorEditor() override;
    void paint(juce::Graphics&) override;
    void paintOverChildren(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
private:
    EarlyPocketAudioProcessor& plugin;
    PocketLook look;
    ReflectionsGraph graph;
    EqualizerGraph eq;
    bool expanded=true,bypassTarget=false,capturingBlur=false;
    juce::Image blurredSnapshot;
    int learnState=-1;
    int activeDial=-1;
    float dragStartY=0.f,dragStartValue=0.f;
    juce::RangedAudioParameter* draggedParameter=nullptr;
    juce::Rectangle<int> blurArea;
    juce::Rectangle<int> scaled(float,float,float,float) const;
    void timerCallback() override;
    void captureBlurSnapshot();
    void showSettings();
    void setExpanded(bool);
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EarlyPocketAudioProcessorEditor)
};
