#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>

namespace
{
    constexpr float twoPi = juce::MathConstants<float>::twoPi;

    // wartości zależne od prędkości taśmy: 3.75 / 7.5 / 15 / 30 IPS
    const float hfCutoff[4]  = { 7000.0f, 11000.0f, 16000.0f, 20000.0f }; // ubytek wysokich tonów
    const float bumpFreq[4]  = { 40.0f, 55.0f, 75.0f, 110.0f };           // "head bump" (podbicie basu)
    const float modScale[4]  = { 2.0f, 1.4f, 1.0f, 0.6f };                // wow & flutter
    const float hissScale[4] = { 2.0f, 1.4f, 1.0f, 0.7f };                // szum taśmy
}

juce::AudioProcessorValueTreeState::ParameterLayout TapeSatProcessor::createLayout()
{
    using P = juce::AudioParameterFloat;
    using R = juce::NormalisableRange<float>;
    juce::AudioProcessorValueTreeState::ParameterLayout l;

    l.add (std::make_unique<P> (juce::ParameterID { "input", 1 }, "Input (dB)", R (-12.0f, 24.0f, 0.1f), 6.0f));
    l.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "speed", 1 }, "Tape Speed",
        juce::StringArray { "3.75 IPS", "7.5 IPS", "15 IPS", "30 IPS" }, 2));
    l.add (std::make_unique<P> (juce::ParameterID { "bias", 1 },    "Bias",      R (-1.0f, 1.0f, 0.01f), 0.0f));
    l.add (std::make_unique<P> (juce::ParameterID { "bump", 1 },    "Head Bump", R (0.0f, 1.0f, 0.01f), 0.5f));
    l.add (std::make_unique<P> (juce::ParameterID { "wow", 1 },     "Wow",       R (0.0f, 1.0f, 0.01f), 0.15f));
    l.add (std::make_unique<P> (juce::ParameterID { "flutter", 1 }, "Flutter",   R (0.0f, 1.0f, 0.01f), 0.15f));
    l.add (std::make_unique<P> (juce::ParameterID { "hiss", 1 },    "Hiss",      R (0.0f, 1.0f, 0.01f), 0.10f));
    l.add (std::make_unique<P> (juce::ParameterID { "output", 1 },  "Output (dB)", R (-24.0f, 12.0f, 0.1f), 0.0f));
    return l;
}

TapeSatProcessor::TapeSatProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createLayout())
{
}

bool TapeSatProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    auto out = l.getMainOutputChannelSet();
    return (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo())
           && out == l.getMainInputChannelSet();
}

void TapeSatProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sr = sampleRate;
    maxBlock = juce::jmax (1, samplesPerBlock);

    oversampler.initProcessing ((size_t) maxBlock);
    oversampler.reset();

    // stałe opóźnienie środkowe dla wow & flutter + opóźnienie oversamplingu zgłaszamy hostowi
    centerSamples = (int) std::lround (0.004 * sr);
    setLatencySamples ((int) std::lround (oversampler.getLatencyInSamples()) + centerSamples);

    const int lsize = (int) std::ceil (0.02 * sr) + 4;
    for (auto& d : wowLine) d.assign ((size_t) lsize, 0.0f);
    wowPos = 0;
    ph1 = ph2 = ph3 = ph4 = 0.0f;
    hissLp[0] = hissLp[1] = 0.0f;

    // filtry: pre/de-emfaza (kompensują się w torze liniowym), reszta ustawiana w processBlock
    using Coeffs = juce::dsp::IIR::Coefficients<float>;
    const float emph = juce::Decibels::decibelsToGain (6.0f);
    const float nyq  = (float) (0.45 * sr);

    for (int c = 0; c < 2; ++c)
    {
        preEmph[c].coefficients = Coeffs::makeHighShelf (sampleRate, 4000.0f, 0.707f, emph);
        deEmph[c].coefficients  = Coeffs::makeHighShelf (sampleRate, 4000.0f, 0.707f, 1.0f / emph);
        eqBump[c].coefficients  = Coeffs::makePeakFilter (sampleRate, 75.0f, 1.2f, 1.0f);
        hfLoss[c].coefficients  = Coeffs::makeLowPass (sampleRate, juce::jmin (20000.0f, nyq), 0.707f);
        dcBlock[c].coefficients = Coeffs::makeHighPass (sampleRate, 20.0f, 0.707f);

        preEmph[c].reset(); deEmph[c].reset(); eqBump[c].reset(); hfLoss[c].reset(); dcBlock[c].reset();
    }
    lastSpeed = -1; lastBias = 1000.0f; lastBump = 1000.0f; // wymusza przeliczenie

    inSmooth.reset (sampleRate, 0.02);
    inSmooth.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (apvts.getRawParameterValue ("input")->load()));
    outSmooth.reset (sampleRate, 0.02);
    outSmooth.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (apvts.getRawParameterValue ("output")->load()));
}

void TapeSatProcessor::processBlock (juce::AudioBuffer<float>& buf, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int n = buf.getNumSamples();
    const int chs = juce::jmin (buf.getNumChannels(), 2);
    if (chs == 0 || n == 0 || wowLine[0].empty()) return;

    const float inDb     = apvts.getRawParameterValue ("input")->load();
    const int   si       = juce::jlimit (0, 3, (int) apvts.getRawParameterValue ("speed")->load());
    const float bias     = apvts.getRawParameterValue ("bias")->load();
    const float bumpAmt  = apvts.getRawParameterValue ("bump")->load();
    const float wow      = apvts.getRawParameterValue ("wow")->load();
    const float flutter  = apvts.getRawParameterValue ("flutter")->load();
    const float hiss     = apvts.getRawParameterValue ("hiss")->load();
    const float outDb    = apvts.getRawParameterValue ("output")->load();

    inSmooth.setTargetValue  (juce::Decibels::decibelsToGain (inDb));
    outSmooth.setTargetValue (juce::Decibels::decibelsToGain (outDb));

    // filtry zależne od prędkości, biasu i head bump (przeliczane tylko przy zmianie)
    if (si != lastSpeed || bias != lastBias || bumpAmt != lastBump)
    {
        using Coeffs = juce::dsp::IIR::Coefficients<float>;
        auto bc = Coeffs::makePeakFilter (sr, bumpFreq[si], 1.2f, juce::Decibels::decibelsToGain (bumpAmt * 4.0f));

        // zbyt duży bias (+) przyciemnia dźwięk
        float cutoff = hfCutoff[si] * (1.0f - 0.45f * juce::jmax (0.0f, bias));
        cutoff = juce::jlimit (1000.0f, (float) (0.45 * sr), cutoff);
        auto lc = Coeffs::makeLowPass (sr, cutoff, 0.707f);

        for (int c = 0; c < 2; ++c)
        {
            eqBump[c].coefficients = bc;
            hfLoss[c].coefficients = lc;
        }
        lastSpeed = si; lastBias = bias; lastBump = bumpAmt;
    }

    // 1) wejście + pre-emfaza (wysokie tony nasycają się mocniej, jak na prawdziwej taśmie)
    for (int i = 0; i < n; ++i)
    {
        const float g = inSmooth.getNextValue();
        for (int c = 0; c < chs; ++c)
        {
            float* x = buf.getWritePointer (c);
            x[i] = preEmph[c].processSample (x[i] * g);
        }
    }

    // 2) nasycenie taśmy (4x oversampling)
    //    bias < 0 (niedobiasowanie): mocniejsze nasycenie i asymetria (parzyste harmoniczne)
    //    bias > 0 (przebiasowanie): łagodniej, ciemniej
    const float k          = 1.0f - 0.35f * bias;
    const float asym       = 0.3f * juce::jmax (0.0f, -bias);
    const float asymOffset = std::tanh (asym);
    const float invK       = 1.0f / k;

    juce::dsp::AudioBlock<float> fullBlock (buf);
    for (int start = 0; start < n; start += maxBlock)
    {
        const int len = juce::jmin (maxBlock, n - start);
        auto sub = fullBlock.getSubsetChannelBlock (0, (size_t) chs)
                            .getSubBlock ((size_t) start, (size_t) len);

        auto up = oversampler.processSamplesUp (sub);
        for (size_t c = 0; c < up.getNumChannels(); ++c)
        {
            float* s = up.getChannelPointer (c);
            for (size_t i = 0; i < up.getNumSamples(); ++i)
                s[i] = (std::tanh (k * s[i] + asym) - asymOffset) * invK;
        }
        oversampler.processSamplesDown (sub);
    }

    // 3) de-emfaza, head bump, ubytek wysokich tonów, wow & flutter, szum
    const float wowMs  = 1.5f * modScale[si] * wow * wow;
    const float flMs   = 0.1f * modScale[si] * flutter * flutter;
    const float hissAmp = hiss * hiss * 0.01f * hissScale[si];

    const float inc1 = twoPi * 0.7f  / (float) sr;
    const float inc2 = twoPi * 1.3f  / (float) sr;
    const float inc3 = twoPi * 9.0f  / (float) sr;
    const float inc4 = twoPi * 13.7f / (float) sr;

    const int lsize = (int) wowLine[0].size();
    float peak[2] = { 0.0f, 0.0f };

    for (int i = 0; i < n; ++i)
    {
        const float og = outSmooth.getNextValue();

        const float wl = (std::sin (ph1) + 0.4f * std::sin (ph2)) / 1.4f;
        const float fl = (std::sin (ph3) + 0.5f * std::sin (ph4)) / 1.5f;
        ph1 += inc1; if (ph1 > twoPi) ph1 -= twoPi;
        ph2 += inc2; if (ph2 > twoPi) ph2 -= twoPi;
        ph3 += inc3; if (ph3 > twoPi) ph3 -= twoPi;
        ph4 += inc4; if (ph4 > twoPi) ph4 -= twoPi;

        const float delaySamples = (float) centerSamples
                                 + (wowMs * wl + flMs * fl) * 0.001f * (float) sr;

        for (int c = 0; c < chs; ++c)
        {
            float* x = buf.getWritePointer (c);

            float s = deEmph[c].processSample (x[i]);
            s = eqBump[c].processSample (s);
            s = hfLoss[c].processSample (s);
            s = dcBlock[c].processSample (s);

            auto& line = wowLine[c];
            line[(size_t) wowPos] = s;

            float rp = (float) wowPos - delaySamples;
            if (rp < 0.0f) rp += (float) lsize;
            const int i0 = (int) rp;
            const int i1 = (i0 + 1) % lsize;
            const float fr = rp - (float) i0;
            float out = line[(size_t) i0] + (line[(size_t) i1] - line[(size_t) i0]) * fr;

            // szum taśmy: biały szum z odfiltrowanymi niskimi (zostają wysokie)
            const float wn = rng.nextFloat() * 2.0f - 1.0f;
            hissLp[c] += 0.1f * (wn - hissLp[c]);
            out += (wn - hissLp[c]) * hissAmp;

            out *= og;
            x[i] = out;
            peak[c] = juce::jmax (peak[c], std::abs (out));
        }

        if (++wowPos >= lsize) wowPos = 0;
    }

    levelL.store (juce::jmax (levelL.load(), peak[0]));
    levelR.store (juce::jmax (levelR.load(), chs == 2 ? peak[1] : peak[0]));
}

juce::AudioProcessorEditor* TapeSatProcessor::createEditor()
{
    return new TapeSatEditor (*this);
}

void TapeSatProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, dest);
}

void TapeSatProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new TapeSatProcessor();
}
