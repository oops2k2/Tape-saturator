#include "PluginEditor.h"

namespace
{
    const juce::Colour panelTop    { 0xff2b2b2e };
    const juce::Colour panelBottom { 0xff151517 };
    const juce::Colour ledRed      { 0xffff3b2f };
    const juce::Colour accent      { 0xffff9f1c };
    const juce::Colour cream       { 0xffe8dfc8 };

    const char* ids[]   = { "input", "bias", "bump", "wow", "flutter", "hiss", "output" };
    const char* names[] = { "INPUT", "BIAS", "HEAD BUMP", "WOW", "FLUTTER", "HISS", "OUTPUT" };

    constexpr float twoPi = juce::MathConstants<float>::twoPi;
}

//==============================================================================
RetroLookAndFeel::RetroLookAndFeel()
{
    setColour (juce::Slider::textBoxTextColourId, ledRed);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colour (0xff0a0a0a));
    setColour (juce::Label::textColourId, cream);

    setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff0a0a0a));
    setColour (juce::ComboBox::textColourId, ledRed);
    setColour (juce::ComboBox::outlineColourId, cream.withAlpha (0.4f));
    setColour (juce::ComboBox::arrowColourId, accent);
    setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xff151517));
    setColour (juce::PopupMenu::textColourId, cream);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, accent);
    setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::black);
}

void RetroLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h,
                                         float pos, float startAngle, float endAngle,
                                         juce::Slider&)
{
    auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (12.0f);
    const float r = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto c = bounds.getCentre();
    const float angle = startAngle + pos * (endAngle - startAngle);

    g.setColour (cream.withAlpha (0.7f));
    for (int i = 0; i <= 10; ++i)
    {
        const float a = startAngle + (float) i / 10.0f * (endAngle - startAngle);
        g.drawLine (juce::Line<float> (c.getPointOnCircumference (r + 3.0f, a),
                                       c.getPointOnCircumference (r + (i % 5 == 0 ? 9.0f : 6.0f), a)),
                    i % 5 == 0 ? 1.8f : 1.0f);
    }

    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.fillEllipse (c.x - r + 2.0f, c.y - r + 3.0f, r * 2.0f, r * 2.0f);

    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff6a6a6e), c.x - r * 0.5f, c.y - r * 0.6f,
                                             juce::Colour (0xff1e1e20), c.x + r * 0.6f, c.y + r * 0.8f, true));
    g.fillEllipse (c.x - r, c.y - r, r * 2.0f, r * 2.0f);

    g.setColour (juce::Colours::black);
    g.drawEllipse (c.x - r, c.y - r, r * 2.0f, r * 2.0f, 1.5f);

    const float ri = r * 0.72f;
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff3a3a3d), c.x, c.y - ri,
                                             juce::Colour (0xff222224), c.x, c.y + ri, false));
    g.fillEllipse (c.x - ri, c.y - ri, ri * 2.0f, ri * 2.0f);

    juce::Path p;
    p.addRoundedRectangle (-2.0f, -r * 0.92f, 4.0f, r * 0.5f, 1.5f);
    g.setColour (accent);
    g.fillPath (p, juce::AffineTransform::rotation (angle).translated (c.x, c.y));
}

//==============================================================================
TapeSatEditor::TapeSatEditor (TapeSatProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    setLookAndFeel (&laf);
    setSize (860, 440);

    for (int i = 0; i < numKnobs; ++i)
    {
        auto& k = knobs[(size_t) i];

        k.slider.setSliderStyle (juce::Slider::RotaryVerticalDrag);
        k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 18);
        k.slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f,
                                      juce::MathConstants<float>::pi * 2.75f, true);
        addAndMakeVisible (k.slider);

        k.label.setText (names[i], juce::dontSendNotification);
        k.label.setJustificationType (juce::Justification::centred);
        k.label.setFont (juce::Font (juce::FontOptions ("Consolas", 14.0f, juce::Font::bold)));
        addAndMakeVisible (k.label);

        k.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
            processor.apvts, ids[i], k.slider);
    }

    if (auto* sp = dynamic_cast<juce::AudioParameterChoice*> (processor.apvts.getParameter ("speed")))
        speedBox.addItemList (sp->choices, 1);
    addAndMakeVisible (speedBox);
    speedAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        processor.apvts, "speed", speedBox);

    speedLabel.setText ("TAPE SPEED", juce::dontSendNotification);
    speedLabel.setJustificationType (juce::Justification::centred);
    speedLabel.setFont (juce::Font (juce::FontOptions ("Consolas", 14.0f, juce::Font::bold)));
    addAndMakeVisible (speedLabel);

    startTimerHz (30);
}

TapeSatEditor::~TapeSatEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void TapeSatEditor::timerCallback()
{
    const float rawL = processor.levelL.exchange (0.0f);
    const float rawR = processor.levelR.exchange (0.0f);
    dispL = juce::jmax (rawL, dispL * 0.85f);
    dispR = juce::jmax (rawR, dispR * 0.85f);

    // Szpule kręcą się, gdy przez plugin płynie dźwięk, i hamują z bezwładnością, gdy cisza.
    // Prędkość zależy od wybranej prędkości taśmy.
    static const float speedFactor[4] = { 0.5f, 1.0f, 2.0f, 4.0f };
    const int si = juce::jlimit (0, 3, (int) processor.apvts.getRawParameterValue ("speed")->load());

    const bool active = (dispL > 0.002f || dispR > 0.002f);
    const float target = active ? 0.045f * speedFactor[si] : 0.0f;
    reelVel += (target - reelVel) * (active ? 0.08f : 0.03f);
    if (reelVel < 0.0005f && ! active) reelVel = 0.0f;

    reelAngle += reelVel;
    if (reelAngle > twoPi) reelAngle -= twoPi;

    tapePhase += reelVel * 0.01f;            // taśma powoli przewija się z jednej szpuli na drugą
    if (tapePhase > twoPi) tapePhase -= twoPi;

    tapeFlow += reelVel * 0.35f;             // przesuwające się błyski na taśmie
    if (tapeFlow > 1.0f) tapeFlow -= 1.0f;

    repaint (headerArea);
}

void TapeSatEditor::drawReel (juce::Graphics& g, juce::Point<float> c, float rr,
                              float packR, float angle, float vel)
{
    const float glow = juce::jlimit (0.0f, 1.0f, vel / 0.1f);

    // poświata podczas obracania
    if (glow > 0.01f)
    {
        g.setGradientFill (juce::ColourGradient (accent.withAlpha (0.22f * glow), c.x, c.y,
                                                 accent.withAlpha (0.0f), c.x + rr * 1.5f, c.y, true));
        g.fillEllipse (c.x - rr * 1.5f, c.y - rr * 1.5f, rr * 3.0f, rr * 3.0f);
    }

    const float hr = rr * 0.5f; // promień piasty

    // zwój taśmy (rozmiar zmienia się, gdy taśma przechodzi między szpulami)
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff6b4a2f), c.x - packR * 0.5f, c.y - packR * 0.5f,
                                             juce::Colour (0xff2a1a10), c.x + packR * 0.6f, c.y + packR * 0.6f, false));
    g.fillEllipse (c.x - packR, c.y - packR, packR * 2.0f, packR * 2.0f);

    g.setColour (juce::Colours::black.withAlpha (0.25f));
    for (int k = 1; k <= 3; ++k)
    {
        const float rk = hr + (packR - hr) * (float) k / 4.0f;
        g.drawEllipse (c.x - rk, c.y - rk, rk * 2.0f, rk * 2.0f, 0.8f);
    }

    // przezroczyste boczne tarcze szpuli
    g.setColour (juce::Colours::white.withAlpha (0.05f));
    g.fillEllipse (c.x - rr, c.y - rr, rr * 2.0f, rr * 2.0f);
    g.setColour (cream.withAlpha (0.4f));
    g.drawEllipse (c.x - rr, c.y - rr, rr * 2.0f, rr * 2.0f, 2.0f);

    // piasta
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xffd8d0bb), c.x - hr * 0.6f, c.y - hr * 0.6f,
                                             juce::Colour (0xff8a8472), c.x + hr * 0.7f, c.y + hr * 0.7f, false));
    g.fillEllipse (c.x - hr, c.y - hr, hr * 2.0f, hr * 2.0f);

    // otwory w piaście: ślad (rozmycie ruchu) i właściwe otwory
    const float holeR = hr * 0.2f;
    const float holeDist = hr * 0.62f;

    for (int j = 3; j >= 1; --j)
    {
        const float ghostAngle = angle - (float) j * vel * 0.9f;
        g.setColour (juce::Colours::black.withAlpha (0.12f * glow));
        for (int i = 0; i < 6; ++i)
        {
            const auto hp = c.getPointOnCircumference (holeDist, ghostAngle + (float) i * twoPi / 6.0f);
            g.fillEllipse (hp.x - holeR, hp.y - holeR, holeR * 2.0f, holeR * 2.0f);
        }
    }

    g.setColour (juce::Colour (0xff1a1a1c));
    for (int i = 0; i < 6; ++i)
    {
        const auto hp = c.getPointOnCircumference (holeDist, angle + (float) i * twoPi / 6.0f);
        g.fillEllipse (hp.x - holeR, hp.y - holeR, holeR * 2.0f, holeR * 2.0f);
    }

    // oś z trzema ząbkami
    const float sr2 = hr * 0.24f;
    g.setColour (juce::Colour (0xff1a1a1c));
    g.fillEllipse (c.x - sr2, c.y - sr2, sr2 * 2.0f, sr2 * 2.0f);
    g.setColour (accent);
    for (int i = 0; i < 3; ++i)
    {
        const float a = angle + (float) i * twoPi / 3.0f;
        g.drawLine (juce::Line<float> (c.getPointOnCircumference (sr2 * 0.3f, a),
                                       c.getPointOnCircumference (sr2 * 0.95f, a)), 2.0f);
    }
}

void TapeSatEditor::drawMeter (juce::Graphics& g, juce::Rectangle<float> area, float level, const juce::String& name)
{
    constexpr int segs = 12;
    const float db = juce::Decibels::gainToDecibels (level, -60.0f);

    g.setColour (cream);
    g.setFont (juce::Font (juce::FontOptions ("Consolas", 13.0f, juce::Font::bold)));
    g.drawText (name, area.removeFromLeft (20.0f), juce::Justification::centred);

    const float w = area.getWidth() / (float) segs;
    for (int i = 0; i < segs; ++i)
    {
        const float segDb = -36.0f + (float) i * 3.0f; // od -36 dB do -3 dB
        const juce::Colour col = i < 8 ? juce::Colour (0xff3ddc4a)
                               : i < 11 ? juce::Colour (0xffffb020)
                                        : juce::Colour (0xffff3b2f);
        g.setColour (db >= segDb ? col : col.withAlpha (0.12f));
        g.fillRect (area.getX() + (float) i * w + 1.0f, area.getY(), w - 2.0f, area.getHeight());
    }
}

void TapeSatEditor::drawLogo (juce::Graphics& g, juce::Rectangle<float> area)
{
    // metalowa tabliczka znamionowa z logo producenta
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff3a3a3e), area.getX(), area.getY(),
                                             juce::Colour (0xff1b1b1d), area.getX(), area.getBottom(), false));
    g.fillRoundedRectangle (area, 4.0f);
    g.setColour (cream.withAlpha (0.5f));
    g.drawRoundedRectangle (area.reduced (0.5f), 4.0f, 1.2f);

    // okrągły znaczek "DT"
    const float d = area.getHeight() - 8.0f;
    const juce::Rectangle<float> badge (area.getX() + 6.0f, area.getY() + 4.0f, d, d);
    g.setColour (accent);
    g.fillEllipse (badge);
    g.setColour (juce::Colours::black);
    g.setFont (juce::Font (juce::FontOptions ("Impact", d * 0.62f, juce::Font::plain)));
    g.drawText ("DT", badge, juce::Justification::centred);

    g.setColour (cream);
    g.setFont (juce::Font (juce::FontOptions ("Impact", d * 0.8f, juce::Font::plain)));
    g.drawText ("DTSTUDIO", area.withTrimmedLeft (d + 14.0f), juce::Justification::centredLeft);
}

void TapeSatEditor::paint (juce::Graphics& g)
{
    g.setGradientFill (juce::ColourGradient (panelTop, 0.0f, 0.0f, panelBottom, 0.0f, (float) getHeight(), false));
    g.fillAll();

    g.setColour (juce::Colours::white.withAlpha (0.025f));
    for (int yy = 0; yy < getHeight(); yy += 3)
        g.drawHorizontalLine (yy, 0.0f, (float) getWidth());

    g.setColour (juce::Colours::black);
    g.drawRect (getLocalBounds(), 3);
    g.setColour (cream.withAlpha (0.25f));
    g.drawRect (getLocalBounds().reduced (6), 1);

    for (auto pt : { juce::Point<float> (18, 18), { (float) getWidth() - 18, 18 },
                     { 18, (float) getHeight() - 18 }, { (float) getWidth() - 18, (float) getHeight() - 18 } })
    {
        g.setColour (juce::Colour (0xff555558));
        g.fillEllipse (pt.x - 6, pt.y - 6, 12, 12);
        g.setColour (juce::Colours::black);
        g.drawLine (pt.x - 4, pt.y + 3, pt.x + 4, pt.y - 3, 1.5f);
    }

    // tytuł
    g.setColour (accent);
    g.setFont (juce::Font (juce::FontOptions ("Impact", 38.0f, juce::Font::plain)));
    g.drawText ("TAPE SAT", 36, 22, 260, 44, juce::Justification::centredLeft);
    g.setColour (cream.withAlpha (0.8f));
    g.setFont (juce::Font (juce::FontOptions ("Consolas", 13.0f, juce::Font::bold)));
    g.drawText ("MAGNETIC TAPE SATURATION  //  DTSTUDIO", 38, 64, 340, 18, juce::Justification::centredLeft);

    // szpule, taśma i głowica
    const float ry = 58.0f, rr = 34.0f;
    const juce::Point<float> c1 (400.0f, ry), c2 (510.0f, ry);
    const float midX = (c1.x + c2.x) * 0.5f;
    const float headY = ry + rr + 10.0f;

    // taśma przechodzi powoli z lewej szpuli na prawą i z powrotem
    const float rmin = rr * 0.6f, rmax = rr * 0.94f;
    const float p = 0.5f + 0.5f * std::sin (tapePhase);
    const float pack1 = rmin + (rmax - rmin) * (1.0f - p);
    const float pack2 = rmin + (rmax - rmin) * p;

    const juce::Point<float> a0 (c1.x, ry + pack1), a1 (midX - 14.0f, headY);
    const juce::Point<float> b0 (midX + 14.0f, headY), b1 (c2.x, ry + pack2);

    g.setColour (juce::Colour (0xff6b4a2f));
    juce::Path tape;
    tape.startNewSubPath (a0);
    tape.lineTo (a1);
    tape.lineTo (b0);
    tape.lineTo (b1);
    g.strokePath (tape, juce::PathStrokeType (2.0f));

    // błyski na przesuwającej się taśmie
    if (reelVel > 0.005f)
    {
        g.setColour (cream.withAlpha (0.55f));
        for (int i = 0; i < 4; ++i)
        {
            const float f = std::fmod (tapeFlow + (float) i * 0.25f, 1.0f);
            const auto pa = a0 + (a1 - a0) * f;
            const auto pb = b0 + (b1 - b0) * f;
            g.fillEllipse (pa.x - 1.5f, pa.y - 1.5f, 3.0f, 3.0f);
            g.fillEllipse (pb.x - 1.5f, pb.y - 1.5f, 3.0f, 3.0f);
        }
    }

    g.setColour (juce::Colour (0xff77777c));
    g.fillRoundedRectangle (midX - 14.0f, headY - 2.0f, 28.0f, 12.0f, 2.0f);

    drawReel (g, c1, rr, pack1, reelAngle, reelVel);
    drawReel (g, c2, rr, pack2, reelAngle, reelVel);

    // mierniki wyjścia
    const float mx = (float) getWidth() - 270.0f;
    drawMeter (g, { mx, 34.0f, 230.0f, 18.0f }, dispL, "L");
    drawMeter (g, { mx, 62.0f, 230.0f, 18.0f }, dispR, "R");
    g.setColour (cream.withAlpha (0.6f));
    g.setFont (juce::Font (juce::FontOptions ("Consolas", 11.0f, juce::Font::bold)));
    g.drawText ("OUTPUT LEVEL", (int) mx, 86, 250, 14, juce::Justification::centredLeft);

    // stopka: logo producenta i oznaczenie modelu
    const float fy = (float) getHeight() - 56.0f;
    drawLogo (g, { 44.0f, fy, 190.0f, 30.0f });
    g.setColour (cream.withAlpha (0.55f));
    g.setFont (juce::Font (juce::FontOptions ("Consolas", 12.0f, juce::Font::bold)));
    g.drawText ("TAPE SATURATION UNIT  //  MODEL TS-90", (int) ((float) getWidth() - 400.0f), (int) fy, 356, 30,
                juce::Justification::centredRight);
}

void TapeSatEditor::resized()
{
    headerArea = { 0, 0, getWidth(), 125 };

    auto area = getLocalBounds().reduced (24).withTrimmedTop (105).withTrimmedBottom (40);
    const int cw = area.getWidth() / 8;

    auto c0 = area.removeFromLeft (cw);
    speedLabel.setBounds (c0.removeFromTop (20));
    speedBox.setBounds (c0.getX() + 8, c0.getCentreY() - 24, c0.getWidth() - 16, 36);

    for (int i = 0; i < numKnobs; ++i)
    {
        auto cell = area.removeFromLeft (cw);
        auto& k = knobs[(size_t) i];
        k.label.setBounds (cell.removeFromTop (20));
        k.slider.setBounds (cell);
    }
}
