#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace vspd
{
namespace col
{
inline constexpr juce::uint32 background = 0xff2c3d50;
inline constexpr juce::uint32 panel      = 0xff3b4f66;
inline constexpr juce::uint32 panelEdge  = 0xff54697f;
inline constexpr juce::uint32 divider    = 0xff46596e;
inline constexpr juce::uint32 accent     = 0xff6bdd97;
inline constexpr juce::uint32 onAccent   = 0xff12211a;
inline constexpr juce::uint32 heading    = 0xff6fc5e8;
inline constexpr juce::uint32 warn       = 0xfff0705a;
inline constexpr juce::uint32 text       = 0xffeef4f8;
inline constexpr juce::uint32 mid        = 0xffa7bac9;
inline constexpr juce::uint32 dim        = 0xff7f93a5;
inline constexpr juce::uint32 track      = 0xff26323f;
inline constexpr juce::uint32 knobTop    = 0xff5a7189;
inline constexpr juce::uint32 knobBot    = 0xff374a5d;
inline constexpr juce::uint32 knobEdge   = 0xff22303e;
inline constexpr juce::uint32 pointer    = 0xffe2ecf3;
}

inline constexpr float kRotaryStart = juce::MathConstants<float>::pi * 1.25f;
inline constexpr float kRotaryEnd   = juce::MathConstants<float>::pi * 2.75f;
inline constexpr float kKnobArcThickness = 3.5f;

/** Point height, not JUCE height, so the sizes match the design sheet one to one. */
juce::Font uiFont (float pt, bool bold = false);
juce::Font monoFont (float pt);

/** Radius of the value arc a knob of these bounds draws, so callers can ring it. */
float knobArcRadius (juce::Rectangle<int> bounds);

void strokeArc (juce::Graphics&, juce::Point<float> centre, float radius,
                float fromAngle, float toAngle, float thickness);

float trackedWidth (const juce::Font&, const juce::String&, float tracking);
void  drawTracked (juce::Graphics&, const juce::Font&, const juce::String&,
                   float x, float baseline, float tracking);
void  drawTrackedCentred (juce::Graphics&, const juce::Font&, const juce::String&,
                          float centreX, float baseline, float tracking);
} // namespace vspd

class VarispeedLookAndFeel : public juce::LookAndFeel_V4
{
public:
    VarispeedLookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPos, float startAngle, float endAngle,
                           juce::Slider&) override;

    void drawLinearSlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPos, float minSliderPos, float maxSliderPos,
                           juce::Slider::SliderStyle, juce::Slider&) override;

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&,
                               bool highlighted, bool down) override;

    void drawButtonText (juce::Graphics&, juce::TextButton&, bool highlighted, bool down) override;

    void drawComboBox (juce::Graphics&, int width, int height, bool down,
                       int buttonX, int buttonY, int buttonW, int buttonH, juce::ComboBox&) override;

    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;

    void drawCornerResizer (juce::Graphics&, int width, int height,
                            bool highlighted, bool down) override;

    juce::Font getLabelFont (juce::Label&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    juce::Font getPopupMenuFont() override;
};
