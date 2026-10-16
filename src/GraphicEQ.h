#pragma once

#include <cmath>

namespace vspd
{

inline constexpr int kNumEqBands = 7;

/** CHAIN synthesises repetition j from the dry window instead of recycling, so the j passes
    of EQ it would have collected are baked into one coefficient set per repetition index:
    stage s carries (s + 1) x the band gain. Exact at each band centre, and the same shape
    either side of it - a cascade of s identical peakings would have narrower skirts. */
inline constexpr int kNumEqStages = 12;

struct Biquad { float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f; };
struct BiquadState { float s1 = 0.0f, s2 = 0.0f; };

struct EqState
{
    BiquadState s[kNumEqBands][2];
    void reset() noexcept { for (auto& band : s) for (auto& st : band) st = {}; }
};

/** 7 fixed ISO bands of RBJ peaking biquads, TDF-II, stereo.
    Coefficients live here and are shared; only the state is per voice/tap. */
class GraphicEQ
{
public:
    static constexpr float bandFreq[kNumEqBands] { 63.0f, 160.0f, 400.0f, 1000.0f, 2500.0f, 6300.0f, 16000.0f };
    static constexpr float bandQ = 1.4f;

    void prepare (double sampleRate);
    void setGainDb (int band, float db) noexcept;
    void updateCoefficients() noexcept;

    float process (EqState& st, float x, int ch, int stage = 0) const noexcept
    {
        const int st8 = stage < 0 ? 0 : (stage >= kNumEqStages ? kNumEqStages - 1 : stage);
        for (int b = 0; b < kNumEqBands; ++b)
        {
            const Biquad& k = c[st8][b];
            BiquadState& s = st.s[b][ch];
            const float y = k.b0 * x + s.s1;
            s.s1 = k.b1 * x - k.a1 * y + s.s2;
            s.s2 = k.b2 * x - k.a2 * y;
            x = y;
        }
        return x;
    }

private:
    void computeBand (int band, int stage) noexcept;

    double sr = 44100.0;
    float gainDb[kNumEqBands] {};
    bool dirty[kNumEqBands] { true, true, true, true, true, true, true };
    Biquad c[kNumEqStages][kNumEqBands];
};

} // namespace vspd
