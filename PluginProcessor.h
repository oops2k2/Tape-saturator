#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>
#include <atomic>
#include <vector>

class TapeSatProcessor : public juce::AudioProcessor
{
public:
    TapeSatProcessor();

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "TapeSat"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorValueTreeState apvts;

    // poziomy wyjściowe (szczyt od ostatniego odczytu) do mierników w edytorze
    std::atomic<float> levelL { 0.0f }, levelR { 0.0f };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    using Filter = juce::dsp::IIR::Filter<float>;

    double sr = 44100.0;
    int maxBlock = 512;
    int centerSamples = 0;

    // nieliniowość pracuje na 4-krotnie zwiększonej częstotliwości próbkowania (mniej aliasingu)
    juce::dsp::Oversampling<float> oversampler { 2, 2,
        juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, true };

    Filter preEmph[2], deEmph[2], eqBump[2], hfLoss[2], dcBlock[2];
    int lastSpeed = -1;
    float lastBias = 1000.0f, lastBump = 1000.0f;

    // opóźnienie do wow & flutter (wspólne dla obu kanałów, bo taśma jedna)
    std::vector<float> wowLine[2];
    int wowPos = 0;
    float ph1 = 0.0f, ph2 = 0.0f, ph3 = 0.0f, ph4 = 0.0f;

    float hissLp[2] {};
    juce::Random rng;

    juce::SmoothedValue<float> inSmooth, outSmooth;
};
