#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"
#include <array>

class RetroLookAndFeel : public juce::LookAndFeel_V4
{
public:
    RetroLookAndFeel();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h,
                           float pos, float startAngle, float endAngle,
                           juce::Slider&) override;
};

class TapeSatEditor : public juce::AudioProcessorEditor,
                      private juce::Timer
{
public:
    explicit TapeSatEditor (TapeSatProcessor&);
    ~TapeSatEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    static void drawReel (juce::Graphics&, juce::Point<float> centre, float flangeR,
                          float packR, float angle, float vel);
    static void drawMeter (juce::Graphics&, juce::Rectangle<float> area, float level, const juce::String& name);
    static void drawLogo (juce::Graphics&, juce::Rectangle<float> area);

    static constexpr int numKnobs = 7;

    struct Knob
    {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment; // niszczony przed sliderem
    };

    TapeSatProcessor& processor;
    RetroLookAndFeel laf;
    std::array<Knob, numKnobs> knobs;

    juce::ComboBox speedBox;
    juce::Label speedLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> speedAttachment;

    // animacja szpul: kąt, prędkość kątowa (z bezwładnością), przepływ taśmy
    float reelAngle = 0.0f;
    float reelVel = 0.0f;
    float tapePhase = 0.0f;
    float tapeFlow = 0.0f;

    float dispL = 0.0f, dispR = 0.0f;
    juce::Rectangle<int> headerArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TapeSatEditor)
};
