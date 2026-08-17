#include "GraphicEQ.h"

#include <algorithm>

namespace vspd
{

void GraphicEQ::prepare (double sampleRate)
{
    sr = sampleRate;
    for (int b = 0; b < kNumEqBands; ++b) dirty[b] = true;
    updateCoefficients();
}

void GraphicEQ::setGainDb (int band, float db) noexcept
{
    if (std::abs (gainDb[band] - db) > 0.0f) { gainDb[band] = db; dirty[band] = true; }
}

void GraphicEQ::updateCoefficients() noexcept
{
    for (int b = 0; b < kNumEqBands; ++b)
        if (dirty[b]) { computeBand (b); dirty[b] = false; }
}

void GraphicEQ::computeBand (int band) noexcept
{
    const double nyquist = sr * 0.5;
    const double f = std::min ((double) bandFreq[band], nyquist * 0.95);
    const double A = std::pow (10.0, gainDb[band] / 40.0);
    const double w0 = 2.0 * 3.14159265358979323846 * f / sr;
    const double alpha = std::sin (w0) / (2.0 * (double) bandQ);
    const double cw = std::cos (w0);

    const double b0 = 1.0 + alpha * A;
    const double b1 = -2.0 * cw;
    const double b2 = 1.0 - alpha * A;
    const double a0 = 1.0 + alpha / A;
    const double a1 = -2.0 * cw;
    const double a2 = 1.0 - alpha / A;

    Biquad& k = c[band];
    k.b0 = (float) (b0 / a0);
    k.b1 = (float) (b1 / a0);
    k.b2 = (float) (b2 / a0);
    k.a1 = (float) (a1 / a0);
    k.a2 = (float) (a2 / a0);
}

} // namespace vspd
