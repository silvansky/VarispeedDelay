#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace vspd::col
{
inline constexpr juce::uint32 background = 0xff1a1a1e;
inline constexpr juce::uint32 panel      = 0xff232329;
inline constexpr juce::uint32 panelEdge  = 0xff33333d;
inline constexpr juce::uint32 accent     = 0xff4fc3f7;
inline constexpr juce::uint32 accentDim  = 0xff2a6b85;
inline constexpr juce::uint32 text       = 0xffd0d0d8;
inline constexpr juce::uint32 dim        = 0xff888899;
inline constexpr juce::uint32 track      = 0xff3a3a44;
}

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

    juce::Font getLabelFont (juce::Label&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    juce::Font getPopupMenuFont() override;
};
