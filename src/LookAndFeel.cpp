#include "LookAndFeel.h"

using namespace vspd;

VarispeedLookAndFeel::VarispeedLookAndFeel()
{
    setColour (juce::ResizableWindow::backgroundColourId, juce::Colour (col::background));
    setColour (juce::Label::textColourId,                 juce::Colour (col::text));
    setColour (juce::Slider::textBoxTextColourId,         juce::Colour (col::text));
    setColour (juce::Slider::textBoxOutlineColourId,      juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId,   juce::Colours::transparentBlack);
    setColour (juce::ComboBox::backgroundColourId,        juce::Colour (col::panelEdge));
    setColour (juce::ComboBox::textColourId,              juce::Colour (col::text));
    setColour (juce::ComboBox::outlineColourId,           juce::Colours::transparentBlack);
    setColour (juce::ComboBox::arrowColourId,             juce::Colour (col::dim));
    setColour (juce::PopupMenu::backgroundColourId,       juce::Colour (col::panel));
    setColour (juce::PopupMenu::textColourId,             juce::Colour (col::text));
    setColour (juce::PopupMenu::highlightedBackgroundColourId, juce::Colour (col::accentDim));
    setColour (juce::TextButton::buttonColourId,          juce::Colour (col::panelEdge));
    setColour (juce::TextButton::buttonOnColourId,        juce::Colour (col::accentDim));
    setColour (juce::TextButton::textColourOffId,         juce::Colour (col::dim));
    setColour (juce::TextButton::textColourOnId,          juce::Colour (col::text));
    setColour (juce::AlertWindow::backgroundColourId,     juce::Colour (col::panel));
    setColour (juce::AlertWindow::textColourId,           juce::Colour (col::text));
    setColour (juce::TextEditor::backgroundColourId,      juce::Colour (col::panelEdge));
    setColour (juce::TextEditor::textColourId,            juce::Colour (col::text));
}

void VarispeedLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                             float sliderPos, float startAngle, float endAngle,
                                             juce::Slider& s)
{
    const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (3.0f);
    const auto radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const auto angle = startAngle + sliderPos * (endAngle - startAngle);
    const float thickness = juce::jmax (3.0f, radius * 0.16f);

    juce::Path track;
    track.addCentredArc (centre.x, centre.y, radius - thickness * 0.5f, radius - thickness * 0.5f,
                         0.0f, startAngle, endAngle, true);
    g.setColour (juce::Colour (col::track));
    g.strokePath (track, juce::PathStrokeType (thickness, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));

    if (sliderPos > 0.001f)
    {
        juce::Path value;
        value.addCentredArc (centre.x, centre.y, radius - thickness * 0.5f, radius - thickness * 0.5f,
                             0.0f, startAngle, angle, true);
        g.setColour (juce::Colour (col::accent).withAlpha (s.isEnabled() ? 1.0f : 0.4f));
        g.strokePath (value, juce::PathStrokeType (thickness, juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));
    }

    juce::Path pointer;
    pointer.startNewSubPath (centre.x, centre.y - radius * 0.30f);
    pointer.lineTo (centre.x, centre.y - radius + thickness * 1.4f);
    pointer.applyTransform (juce::AffineTransform::rotation (angle, centre.x, centre.y));
    g.setColour (juce::Colour (col::text));
    g.strokePath (pointer, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved,
                                                 juce::PathStrokeType::rounded));
}

void VarispeedLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height,
                                             float sliderPos, float, float,
                                             juce::Slider::SliderStyle style, juce::Slider& s)
{
    if (style != juce::Slider::LinearVertical)
    {
        LookAndFeel_V4::drawLinearSlider (g, x, y, width, height, sliderPos, 0.0f, 0.0f, style, s);
        return;
    }

    const auto b = juce::Rectangle<int> (x, y, width, height).toFloat();
    const float cx = b.getCentreX();
    const float trackW = 4.0f;

    g.setColour (juce::Colour (col::track));
    g.fillRoundedRectangle (cx - trackW * 0.5f, b.getY(), trackW, b.getHeight(), trackW * 0.5f);

    const float zeroY = b.getY() + b.getHeight() * 0.5f;
    g.setColour (juce::Colour (col::accent));
    const float top = juce::jmin (zeroY, sliderPos);
    const float bot = juce::jmax (zeroY, sliderPos);
    g.fillRoundedRectangle (cx - trackW * 0.5f, top, trackW, juce::jmax (1.0f, bot - top), trackW * 0.5f);

    const float thumbW = juce::jmin (b.getWidth(), 16.0f);
    g.setColour (juce::Colour (col::text));
    g.fillRoundedRectangle (cx - thumbW * 0.5f, sliderPos - 3.0f, thumbW, 6.0f, 2.0f);
}

void VarispeedLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b,
                                                 const juce::Colour&, bool highlighted, bool down)
{
    const bool on = b.getToggleState();
    auto base = juce::Colour (on ? col::accentDim : col::panelEdge);
    if (down) base = base.brighter (0.2f);
    else if (highlighted) base = base.brighter (0.1f);

    g.setColour (base);
    g.fillRoundedRectangle (b.getLocalBounds().toFloat(), 3.0f);

    if (on)
    {
        g.setColour (juce::Colour (col::accent));
        g.drawRoundedRectangle (b.getLocalBounds().toFloat().reduced (0.5f), 3.0f, 1.0f);
    }
}

void VarispeedLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool)
{
    g.setFont (juce::FontOptions (11.0f));
    g.setColour (juce::Colour (b.getToggleState() ? col::text : col::dim));
    g.drawText (b.getButtonText(), b.getLocalBounds(), juce::Justification::centred, false);
}

void VarispeedLookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool,
                                         int, int, int, int, juce::ComboBox& box)
{
    g.setColour (juce::Colour (col::panelEdge));
    g.fillRoundedRectangle (juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height), 3.0f);

    juce::Path arrow;
    const float cx = (float) width - 12.0f, cy = (float) height * 0.5f;
    arrow.addTriangle (cx - 4.0f, cy - 2.0f, cx + 4.0f, cy - 2.0f, cx, cy + 3.0f);
    g.setColour (juce::Colour (col::dim).withAlpha (box.isEnabled() ? 1.0f : 0.4f));
    g.fillPath (arrow);
}

void VarispeedLookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (6, 0, box.getWidth() - 22, box.getHeight());
    label.setFont (getComboBoxFont (box));
}

juce::Font VarispeedLookAndFeel::getLabelFont (juce::Label& l)
{
    return juce::Font (juce::FontOptions (l.getFont().getHeight()));
}

juce::Font VarispeedLookAndFeel::getComboBoxFont (juce::ComboBox&)
{
    return juce::Font (juce::FontOptions (11.0f));
}

juce::Font VarispeedLookAndFeel::getPopupMenuFont()
{
    return juce::Font (juce::FontOptions (12.0f));
}
