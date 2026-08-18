#include "RepetitionView.h"

using namespace vspd;

RepetitionView::RepetitionView (DelayEngine& e) : engine (e)
{
    setInterceptsMouseClicks (true, false);   // needed for hover help; nothing acts on clicks
    getProperties().set ("help", "Live repetitions. Bar length is the repeat's duration, the marker is its position");
}

void RepetitionView::refresh()
{
    count = engine.getVoiceSnapshot (info, kMaxVoices);
    periodMs = (float) engine.getPeriodMs();
    repaint();
}

void RepetitionView::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    g.setColour (juce::Colour (col::panel));
    g.fillRoundedRectangle (b, 3.0f);

    b = b.reduced (6.0f);

    float span = periodMs * 4.0f;
    for (int i = 0; i < count; ++i) span = juce::jmax (span, info[i].duration);
    if (span <= 1.0f) span = 1.0f;

    // grid lines every period
    g.setColour (juce::Colour (col::track));
    for (float t = 0.0f; t <= span; t += juce::jmax (1.0f, periodMs))
        g.drawVerticalLine ((int) (b.getX() + b.getWidth() * t / span), b.getY(), b.getBottom());

    if (count == 0) return;

    const float rowH = juce::jmin (14.0f, b.getHeight() / (float) juce::jmax (1, count));
    for (int i = 0; i < count; ++i)
    {
        const float y = b.getY() + i * rowH;
        const float w = b.getWidth() * juce::jlimit (0.0f, 1.0f, info[i].duration / span);
        const auto bar = juce::Rectangle<float> (b.getX(), y + 1.0f, juce::jmax (2.0f, w), rowH - 3.0f);

        g.setColour (juce::Colour (col::accent).withAlpha (0.25f + 0.55f * info[i].env));
        g.fillRoundedRectangle (bar, 2.0f);

        const float pos = juce::jlimit (0.0f, 1.0f, info[i].duration > 0.0f
                                                     ? info[i].elapsed / info[i].duration : 0.0f);
        g.setColour (juce::Colour (col::text));
        g.fillRect (bar.getX() + pos * bar.getWidth() - 1.0f, bar.getY(), 2.0f, bar.getHeight());
    }
}
