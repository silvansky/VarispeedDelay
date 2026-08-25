#include "LookAndFeel.h"

using namespace vspd;

namespace vspd
{
juce::Font uiFont (float pt, bool bold)
{
    auto o = juce::FontOptions().withPointHeight (pt);
    if (bold) o = o.withStyle ("Bold");
    return juce::Font (o);
}

juce::Font monoFont (float pt)
{
    return juce::Font (juce::FontOptions().withName (juce::Font::getDefaultMonospacedFontName())
                                          .withPointHeight (pt));
}

float knobArcRadius (juce::Rectangle<int> bounds)
{
    const auto b = bounds.toFloat().reduced (3.0f);
    return juce::jmin (b.getWidth(), b.getHeight()) * 0.5f - kKnobArcThickness * 0.5f;
}

void strokeArc (juce::Graphics& g, juce::Point<float> centre, float radius,
                float fromAngle, float toAngle, float thickness)
{
    if (toAngle - fromAngle < 1.0e-4f) return;
    juce::Path p;
    p.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, fromAngle, toAngle, true);
    g.strokePath (p, juce::PathStrokeType (thickness, juce::PathStrokeType::curved,
                                           juce::PathStrokeType::rounded));
}

float trackedWidth (const juce::Font& f, const juce::String& s, float tracking)
{
    return juce::GlyphArrangement::getStringWidth (f, s)
           + tracking * (float) juce::jmax (0, s.length() - 1);
}

void drawTracked (juce::Graphics& g, const juce::Font& f, const juce::String& s,
                  float x, float baseline, float tracking)
{
    g.setFont (f);
    for (int i = 0; i < s.length(); ++i)
    {
        const auto ch = s.substring (i, i + 1);
        g.drawSingleLineText (ch, juce::roundToInt (x), juce::roundToInt (baseline));
        x += juce::GlyphArrangement::getStringWidth (f, ch) + tracking;
    }
}

void drawTrackedCentred (juce::Graphics& g, const juce::Font& f, const juce::String& s,
                         float centreX, float baseline, float tracking)
{
    drawTracked (g, f, s, centreX - trackedWidth (f, s, tracking) * 0.5f, baseline, tracking);
}

namespace
{
void fillKnobFace (juce::Graphics& g, juce::Point<float> centre, float radius)
{
    const float fx = centre.x - radius * 0.24f;
    const float fy = centre.y - radius * 0.40f;
    g.setGradientFill (juce::ColourGradient (juce::Colour (col::knobTop), fx, fy,
                                             juce::Colour (col::knobBot), fx + radius * 1.4f, fy, true));
    g.fillEllipse (centre.x - radius, centre.y - radius, radius * 2.0f, radius * 2.0f);
    g.setColour (juce::Colour (col::knobEdge));
    g.drawEllipse (centre.x - radius, centre.y - radius, radius * 2.0f, radius * 2.0f, 1.0f);
}
} // namespace
} // namespace vspd

VarispeedLookAndFeel::VarispeedLookAndFeel()
{
    setColour (juce::ResizableWindow::backgroundColourId, juce::Colour (col::background));
    setColour (juce::Label::textColourId,                 juce::Colour (col::text));
    setColour (juce::Label::backgroundWhenEditingColourId, juce::Colour (col::track));
    setColour (juce::Label::textWhenEditingColourId,      juce::Colour (col::text));
    setColour (juce::Label::outlineWhenEditingColourId,   juce::Colour (col::accent));
    setColour (juce::Slider::textBoxTextColourId,         juce::Colour (col::text));
    setColour (juce::Slider::textBoxOutlineColourId,      juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId,   juce::Colours::transparentBlack);
    setColour (juce::ComboBox::backgroundColourId,        juce::Colour (col::panel));
    setColour (juce::ComboBox::textColourId,              juce::Colour (col::text));
    setColour (juce::ComboBox::outlineColourId,           juce::Colour (col::panelEdge));
    setColour (juce::ComboBox::arrowColourId,             juce::Colour (col::mid));
    setColour (juce::PopupMenu::backgroundColourId,       juce::Colour (col::panel));
    setColour (juce::PopupMenu::textColourId,             juce::Colour (col::text));
    setColour (juce::PopupMenu::highlightedBackgroundColourId, juce::Colour (col::accent));
    setColour (juce::PopupMenu::highlightedTextColourId,  juce::Colour (col::onAccent));
    setColour (juce::TextButton::buttonColourId,          juce::Colour (col::panel));
    setColour (juce::TextButton::buttonOnColourId,        juce::Colour (col::accent));
    setColour (juce::TextButton::textColourOffId,         juce::Colour (col::mid));
    setColour (juce::TextButton::textColourOnId,          juce::Colour (col::onAccent));
    setColour (juce::AlertWindow::backgroundColourId,     juce::Colour (col::background));
    setColour (juce::AlertWindow::textColourId,           juce::Colour (col::text));
    setColour (juce::AlertWindow::outlineColourId,        juce::Colour (col::divider));
    setColour (juce::TextEditor::backgroundColourId,      juce::Colour (col::track));
    setColour (juce::TextEditor::textColourId,            juce::Colour (col::text));
    setColour (juce::TextEditor::highlightColourId,       juce::Colour (col::accent).withAlpha (0.3f));
    setColour (juce::TextEditor::focusedOutlineColourId,  juce::Colour (col::accent));
    setColour (juce::CaretComponent::caretColourId,       juce::Colour (col::accent));
}

void VarispeedLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                             float sliderPos, float startAngle, float endAngle,
                                             juce::Slider& s)
{
    const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (3.0f);
    const auto centre = bounds.getCentre();
    const float arcR = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f
                       - kKnobArcThickness * 0.5f;
    const float sweep = endAngle - startAngle;
    const float angle = startAngle + sliderPos * sweep;

    // the feedback knob marks where the loop starts running away; everything past that
    // point on the arc is the overshoot, in the warning colour
    const auto& props = s.getProperties();
    const float split = props.contains ("split") ? juce::jlimit (0.0f, 1.0f, (float) props["split"])
                                                 : 1.0f;

    g.setColour (juce::Colour (col::track));
    strokeArc (g, centre, arcR, startAngle, endAngle, kKnobArcThickness);

    if (split < 1.0f)
    {
        g.setColour (juce::Colour (col::warn).withAlpha (0.55f));
        strokeArc (g, centre, arcR + 4.5f, startAngle + split * sweep, endAngle, 2.0f);
    }

    const float green = juce::jmin (sliderPos, split);
    if (green > 0.001f)
    {
        g.setColour (juce::Colour (col::accent));
        strokeArc (g, centre, arcR, startAngle, startAngle + green * sweep, kKnobArcThickness);
    }
    if (sliderPos > split)
    {
        g.setColour (juce::Colour (col::warn));
        strokeArc (g, centre, arcR, startAngle + split * sweep, angle, kKnobArcThickness);
    }

    const float bodyR = arcR * 0.77f;
    fillKnobFace (g, centre, bodyR);

    juce::Path pointer;
    pointer.startNewSubPath (centre.x, centre.y - bodyR * 0.22f);
    pointer.lineTo (centre.x, centre.y - bodyR * 0.86f);
    pointer.applyTransform (juce::AffineTransform::rotation (angle, centre.x, centre.y));
    g.setColour (juce::Colour (col::pointer));
    g.strokePath (pointer, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved,
                                                 juce::PathStrokeType::rounded));

    if (! s.isEnabled())
    {
        g.setColour (juce::Colour (col::background).withAlpha (0.55f));
        g.fillEllipse (centre.x - arcR - 3.0f, centre.y - arcR - 3.0f,
                       (arcR + 3.0f) * 2.0f, (arcR + 3.0f) * 2.0f);
    }
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
    const float trackW = 3.0f;

    g.setColour (juce::Colour (col::track));
    g.fillRoundedRectangle (cx - trackW * 0.5f, b.getY(), trackW, b.getHeight(), trackW * 0.5f);

    // EQ bands read from the centre detent, output trims read from the bottom
    const float baseY = s.getMinimum() < -1.0e-4 ? b.getCentreY() : b.getBottom();
    const float top = juce::jmin (baseY, sliderPos);
    const float bot = juce::jmax (baseY, sliderPos);
    g.setColour (juce::Colour (col::accent).withAlpha (s.isEnabled() ? 1.0f : 0.4f));
    g.fillRoundedRectangle (cx - trackW * 0.5f, top, trackW, juce::jmax (1.0f, bot - top),
                            trackW * 0.5f);

    const auto thumb = juce::Rectangle<float> (juce::jmin (b.getWidth(), 18.0f), 7.0f)
                         .withCentre ({ cx, sliderPos });
    g.setGradientFill (juce::ColourGradient (juce::Colour (col::knobTop),
                                             thumb.getX(), thumb.getY(),
                                             juce::Colour (col::knobBot),
                                             thumb.getRight(), thumb.getBottom(), false));
    g.fillRoundedRectangle (thumb, 2.0f);
    g.setColour (juce::Colour (col::knobEdge));
    g.drawRoundedRectangle (thumb.reduced (0.5f), 2.0f, 1.0f);
    g.setColour (juce::Colour (col::pointer).withAlpha (0.7f));
    g.fillRect (thumb.getX() + 4.0f, thumb.getCentreY() - 0.5f, thumb.getWidth() - 8.0f, 1.0f);
}

void VarispeedLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b,
                                                 const juce::Colour&, bool highlighted, bool down)
{
    const bool on = b.getToggleState();
    const auto r = b.getLocalBounds().toFloat();
    auto base = juce::Colour (on ? col::accent : col::panel);
    if (down) base = base.brighter (0.15f);
    else if (highlighted) base = base.brighter (0.08f);

    g.setColour (base);
    g.fillRoundedRectangle (r, 3.0f);

    if (! on)
    {
        g.setColour (juce::Colour (col::panelEdge));
        g.drawRoundedRectangle (r.reduced (0.5f), 3.0f, 1.0f);
    }
}

void VarispeedLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool)
{
    const bool on = b.getToggleState();
    g.setFont (uiFont (9.0f, on));
    g.setColour (juce::Colour (on ? col::onAccent : col::mid)
                   .withAlpha (b.isEnabled() ? 1.0f : 0.45f));
    g.drawText (b.getButtonText(), b.getLocalBounds(), juce::Justification::centred, false);
}

void VarispeedLookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool,
                                         int, int, int, int, juce::ComboBox& box)
{
    const auto r = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height);
    g.setColour (juce::Colour (col::panel));
    g.fillRoundedRectangle (r, 3.0f);
    g.setColour (juce::Colour (col::panelEdge));
    g.drawRoundedRectangle (r.reduced (0.5f), 3.0f, 1.0f);

    juce::Path chevron;
    const float cx = (float) width - 12.0f, cy = (float) height * 0.5f;
    chevron.startNewSubPath (cx - 4.0f, cy - 2.0f);
    chevron.lineTo (cx, cy + 2.0f);
    chevron.lineTo (cx + 4.0f, cy - 2.0f);
    g.setColour (juce::Colour (col::mid).withAlpha (box.isEnabled() ? 1.0f : 0.4f));
    g.strokePath (chevron, juce::PathStrokeType (1.3f, juce::PathStrokeType::curved,
                                                 juce::PathStrokeType::rounded));
}

void VarispeedLookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (8, 0, box.getWidth() - 24, box.getHeight());
    label.setFont (getComboBoxFont (box));
}

void VarispeedLookAndFeel::drawCornerResizer (juce::Graphics& g, int width, int height, bool, bool)
{
    g.setColour (juce::Colour (col::dim).withAlpha (0.5f));
    const float w = (float) width, h = (float) height;
    for (int i = 1; i <= 3; ++i)
    {
        const float inset = (float) i * 4.0f;
        g.drawLine (w - inset, h - 2.0f, w - 2.0f, h - inset, 1.2f);
    }
}

juce::Font VarispeedLookAndFeel::getLabelFont (juce::Label& l)
{
    return l.getFont();
}

juce::Font VarispeedLookAndFeel::getComboBoxFont (juce::ComboBox&)
{
    return uiFont (10.0f);
}

juce::Font VarispeedLookAndFeel::getPopupMenuFont()
{
    return uiFont (11.0f);
}
