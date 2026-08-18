#include "DelayEngine.h"

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <new>
#include <vector>

using namespace vspd;

// Replacing global new/delete lets the tests prove the audio path never allocates.
std::atomic<int>  gAllocs { 0 };
std::atomic<bool> gTrackAllocs { false };

void* operator new (std::size_t n)
{
    if (gTrackAllocs.load (std::memory_order_relaxed)) gAllocs.fetch_add (1, std::memory_order_relaxed);
    if (void* p = std::malloc (n != 0 ? n : 1)) return p;
    throw std::bad_alloc();
}
void* operator new[] (std::size_t n) { return operator new (n); }
void operator delete (void* p) noexcept { std::free (p); }
void operator delete[] (void* p) noexcept { std::free (p); }
void operator delete (void* p, std::size_t) noexcept { std::free (p); }
void operator delete[] (void* p, std::size_t) noexcept { std::free (p); }

namespace
{
int failures = 0;
int checks = 0;
const char* currentTest = "";

void check (bool ok, const char* what)
{
    ++checks;
    if (! ok)
    {
        ++failures;
        std::printf ("  FAIL [%s] %s\n", currentTest, what);
    }
}

void checkNear (double a, double b, double tol, const char* what)
{
    ++checks;
    if (! (std::abs (a - b) <= tol))
    {
        ++failures;
        std::printf ("  FAIL [%s] %s (%.6f vs %.6f, tol %.6f)\n", currentTest, what, a, b, tol);
    }
}

constexpr double kSr = 48000.0;
constexpr int    kBlock = 512;

struct Rig
{
    DelayEngine engine;
    DelayEngine::Settings s;
    std::vector<float> left, right;
    int written = 0;

    Rig()
    {
        s.dry = 0.0f;
        s.wet = 1.0f;
        s.feedback = 0.5f;
        s.timeMs = 100.0;
        engine.setSettings (s);
        engine.prepare (kSr, kBlock, 2);
    }

    void apply() { engine.setSettings (s); }

    int inputIndex = 0;

    /** Runs `numSamples` through the engine. The index handed to `input` continues across
        calls, so staged tests do not splice their own signal at every stage boundary. */
    void run (int numSamples, const std::function<float (int)>& input)
    {
        juce::AudioBuffer<float> buf (2, kBlock);
        int done = 0;
        while (done < numSamples)
        {
            const int nb = juce::jmin (kBlock, numSamples - done);
            buf.setSize (2, nb, false, false, true);
            for (int i = 0; i < nb; ++i)
            {
                const float x = input (inputIndex++);
                buf.setSample (0, i, x);
                buf.setSample (1, i, x);
            }
            engine.setSettings (s);
            engine.process (buf);
            for (int i = 0; i < nb; ++i)
            {
                left.push_back (buf.getSample (0, i));
                right.push_back (buf.getSample (1, i));
            }
            done += nb;
        }
        written = (int) left.size();
    }

    void runSilence (int numSamples) { run (numSamples, [] (int) { return 0.0f; }); }

    float peak (int from, int to) const
    {
        float p = 0.0f;
        for (int i = juce::jmax (0, from); i < juce::jmin ((int) left.size(), to); ++i)
            p = juce::jmax (p, std::abs (left[(size_t) i]));
        return p;
    }

    int firstNonZero (int from, int to, float thresh = 1.0e-6f) const
    {
        for (int i = juce::jmax (0, from); i < juce::jmin ((int) left.size(), to); ++i)
            if (std::abs (left[(size_t) i]) > thresh) return i;
        return -1;
    }

    bool allFinite() const
    {
        for (float v : left) if (! std::isfinite (v)) return false;
        return true;
    }
};

void test (const char* name, const std::function<void()>& body)
{
    currentTest = name;
    const int before = failures;
    body();
    std::printf ("%s %s\n", failures == before ? "ok  " : "FAIL", name);
}

//==============================================================================
void testUnityDelay()
{
    Rig r;
    r.s.speed = 1.0;
    r.s.feedback = 0.0f;
    r.s.timeMs = 100.0;
    r.apply();

    const int T = (int) std::round (0.1 * kSr);
    r.run (T, [] (int i) { return i == 100 ? 1.0f : 0.0f; });
    r.runSilence (T * 3);

    const int hit = r.firstNonZero (T / 2, T * 3);
    check (hit >= 0, "impulse repeats");
    checkNear (hit, T + 100, 2.0, "delayed by exactly T");
    checkNear (r.peak (T, T * 2), 1.0f, 0.02, "unity gain at fb 0 wet 1");
    check (r.peak (T * 2, T * 3) < 1.0e-5f, "no second repeat at fb 0");
}

void testRawSpeedUp()
{
    Rig r;
    r.s.speed = 2.0;
    r.s.feedback = 1.0f;
    r.s.timeMs = 100.0;
    r.apply();

    const int T = (int) std::round (0.1 * kSr);
    r.run (T, [T] (int i) { return i == T / 2 ? 1.0f : 0.0f; });
    r.runSilence (T * 5);

    // rep k starts at kT and the event lands at T/2 / 2^k inside it
    for (int k = 1; k <= 3; ++k)
    {
        const int hit = r.firstNonZero (k * T + 1, (k + 1) * T, 1.0e-4f);
        const int expected = k * T + (T / 2) / (1 << k);
        check (hit >= 0, "repetition present");
        if (hit >= 0) checkNear (hit, expected, 4.0, "raw r=2 compresses toward the boundary");
    }
}

void testStableConstantDuration()
{
    Rig r;
    r.s.speed = 2.0;
    r.s.fbType = FbType::Stable;
    r.s.feedback = 1.0f;
    r.s.timeMs = 100.0;
    r.apply();

    const int T = (int) std::round (0.1 * kSr);
    r.run (T, [T] (int i) { return i == T / 2 ? 1.0f : 0.0f; });
    r.runSilence (T * 5);

    // every repetition plays the same buffer at the same rate: same offset every time
    const int expected = T / 2 / 2;
    for (int k = 1; k <= 3; ++k)
    {
        const int hit = r.firstNonZero (k * T + 1, (k + 1) * T, 1.0e-4f);
        check (hit >= 0, "repetition present");
        if (hit >= 0) checkNear (hit - k * T, expected, 4.0, "stable keeps the same offset");
    }
}

void testSlowOverlap()
{
    Rig r;
    r.s.speed = 0.5;
    r.s.feedback = 1.0f;
    r.s.timeMs = 100.0;
    r.apply();

    const int T = (int) std::round (0.1 * kSr);
    r.run (T, [] (int) { return 0.3f; });          // one period of DC
    r.runSilence (T * 6);

    // rep 1 lasts 2T, so it is still sounding when rep 2 starts at 2T
    check (r.peak (T * 2 + 100, T * 2 + 1000) > 0.05f, "repeats overlap at r < 1");
    check (r.engine.maxConcurrentVoices() >= 2, "two voices sound at once");
    check (r.allFinite(), "finite");
}

void testVoiceAndBufferLimits()
{
    Rig r;
    r.s.speed = 0.25;
    r.s.feedback = 0.9f;
    r.s.timeMs = 60.0;
    r.apply();

    r.run ((int) (kSr * 20), [] (int i) { return 0.2f * std::sin (i * 0.01f); });

    check (r.engine.maxConcurrentVoices() <= kMaxVoices, "voice count never exceeds kMaxVoices");
    check (! r.engine.readOverrun(), "no read past written length");
    check (r.allFinite(), "finite over 20 s at 0.25x");
}

void testSpeedSweep()
{
    Rig r;
    r.s.feedback = 0.9f;
    r.s.timeMs = 250.0;
    r.s.speed = 0.25;
    r.apply();

    r.run ((int) (kSr * 2), [] (int i) { return 0.3f * std::sin (i * 0.02f); });

    // sweep 0.25 -> 4 over 4 seconds
    const int sweepLen = (int) (kSr * 4);
    const int base = r.written;
    for (int done = 0; done < sweepLen; done += kBlock)
    {
        const double p = (double) done / sweepLen;
        r.s.speed = std::pow (2.0, -2.0 + 4.0 * p);
        r.apply();
        r.run (juce::jmin (kBlock, sweepLen - done),
               [] (int i) { return 0.3f * std::sin (i * 0.02f); });
    }

    check (r.allFinite(), "no NaN after a 0.25 -> 4 sweep");
    check (r.engine.maxConcurrentVoices() <= kMaxVoices, "voice pool respected during the sweep");

    // no step discontinuity beyond what the signal itself can produce
    float maxStep = 0.0f;
    for (size_t i = (size_t) base + 1; i < r.left.size(); ++i)
        maxStep = juce::jmax (maxStep, std::abs (r.left[i] - r.left[i - 1]));
    check (maxStep < 1.0f, "smoothed sweep produces no click-sized step");
}

void testRegridShrink()
{
    Rig r;
    r.s.timeMs = 20000.0;
    r.s.feedback = 0.9f;
    r.s.timeMode = TimeMode::Regrid;
    r.apply();

    r.run ((int) (kSr * 1.0), [] (int i) { return 0.3f * std::sin (i * 0.01f); });

    r.s.timeMs = 200.0;
    r.apply();
    const int before = r.written;
    r.run ((int) (kSr * 2.0), [] (int i) { return 0.3f * std::sin (i * 0.01f); });

    // a boundary must have happened within one new period (200 ms)
    checkNear (r.engine.getEffectiveTimeSamples(), 0.2 * kSr, 1.0, "regrid snaps T immediately");
    check (r.engine.getActiveVoices() > 0, "repetitions arrive on the new grid");
    check (r.peak (before, r.written) < 8.0f, "output stays bounded after a big shrink");
    check (r.allFinite(), "finite after regrid shrink");
}

void testBendGlide()
{
    Rig r;
    r.s.timeMs = 20000.0;
    r.s.timeMode = TimeMode::Regrid;   // snap to 20 s first
    r.s.feedback = 0.5f;
    r.apply();
    r.run (kBlock, [] (int) { return 0.0f; });

    r.s.timeMode = TimeMode::Bend;
    r.s.timeMs = 200.0;
    r.apply();

    const double start = r.engine.getEffectiveTimeSamples();
    double minM = 10.0, maxM = -10.0;
    int samples = 0;
    const int limit = (int) (kSr * 10);
    while (samples < limit && r.engine.getEffectiveTimeSamples() > 0.2 * kSr + 1.0)
    {
        r.run (kBlock, [] (int) { return 0.0f; });
        samples += kBlock;
        minM = juce::jmin (minM, r.engine.getBendFactor());
        maxM = juce::jmax (maxM, r.engine.getBendFactor());
    }

    const double expected = (start - 0.2 * kSr) / kBendDown;
    checkNear (samples / kSr, expected / kSr, 0.1, "20 s -> 200 ms glides in ~6.6 s");
    check (minM >= 0.25 - 1.0e-9, "bend factor >= 0.25");
    check (maxM <= 4.0 + 1.0e-9, "bend factor <= 4");

    r.run (kBlock * 4, [] (int) { return 0.0f; });
    checkNear (r.engine.getBendFactor(), 1.0, 1.0e-9, "bend returns to exactly 1 when settled");
}

void testDeadband()
{
    Rig r;
    r.s.speed = 1.0;
    r.s.feedback = 0.0f;
    r.s.timeMs = 500.0;
    r.s.dry = 0.0f;
    r.s.wet = 1.0f;
    r.apply();

    // flat automation lane with float jitter: the unity bypass must stay engaged, so a
    // constant input comes back out constant, with no dip at the repetition boundary.
    int block = 0;
    const int T = (int) std::round (0.5 * kSr);
    const int total = T * 8;
    for (int done = 0; done < total; done += kBlock)
    {
        r.s.timeMs = 500.0 + ((block++ % 2) ? 1.0e-5 : -1.0e-5);
        r.apply();
        r.run (juce::jmin (kBlock, total - done), [] (int) { return 0.5f; });
    }

    // measure well past the forceFade window that the initial time latch opens
    float lo = 10.0f, hi = -10.0f;
    for (int i = T * 5; i < T * 7; ++i)
    {
        lo = juce::jmin (lo, r.left[(size_t) i]);
        hi = juce::jmax (hi, r.left[(size_t) i]);
    }
    checkNear (hi - lo, 0.0, 1.0e-4, "no periodic dip under jittered automation at speed 1");
    checkNear (hi, 0.5, 1.0e-4, "and the level is right");
}

void testTailTracksTime()
{
    Rig r;
    r.s.timeMs = 500.0;
    r.apply();
    r.run (kBlock, [] (int) { return 0.0f; });
    checkNear (r.engine.getTailSeconds(), 10.0 * 4.0 * 0.5, 0.01, "tail = 10 x Dmax");

    r.s.timeMs = 20000.0;
    r.apply();
    r.run (kBlock, [] (int) { return 0.0f; });
    checkNear (r.engine.getTailSeconds(), 10.0 * kMaxRepSeconds, 0.01, "tail clamps at kMaxRepSeconds");
}

void testSoftClip()
{
    // transparent below the threshold, monotonic and continuous above it
    for (float x = -0.5f; x <= 0.5f; x += 0.01f)
        checkNear (softClip (x, true), x, 1.0e-6, "transparent below -6 dBFS");

    float prev = softClip (-8.0f, true);
    for (float x = -8.0f; x <= 8.0f; x += 0.001f)
    {
        const float y = softClip (x, true);
        check (y >= prev - 1.0e-6f, "monotonic");
        prev = y;
    }
    checkNear (softClip (1.0f, true), 0.8808f, 0.001, "0 dBFS peak comes out at 0.88 (-1.1 dB)");
    check (softClip (1.0e30f, true) <= kSafetyClamp, "clipped output bounded");
    check (softClip (1.0e30f, false) <= kSafetyClamp, "safety clamp holds with clip off");
    check (std::isfinite (softClip (1.0e30f, false)), "no inf reaches the bus");

    // slope is continuous at the threshold (no kink -> no harmonics)
    const float d = 1.0e-4f;
    const float slopeBelow = (softClip (kClipThreshold - d, true) - softClip (kClipThreshold - 2 * d, true)) / d;
    const float slopeAbove = (softClip (kClipThreshold + 2 * d, true) - softClip (kClipThreshold + d, true)) / d;
    checkNear (slopeBelow, slopeAbove, 0.01, "derivative continuous at the threshold");
}

void testRunawayBounded()
{
    Rig r;
    r.s.feedback = 2.0f;
    r.s.clip = true;
    r.s.timeMs = 120.0;
    r.s.speed = 0.75;
    r.apply();
    r.run ((int) (kSr * 30), [] (int i) { return 0.4f * std::sin (i * 0.017f); });

    check (r.allFinite(), "fb = 2 stays finite over 30 s");
    check (r.peak (0, r.written) <= kSafetyClamp * (float) kMaxVoices + 1.0f, "runaway stays bounded");
}

void testTapeSpacing()
{
    // at r = 1 TAPE and GRID must be identical
    auto render = [] (Spacing sp)
    {
        Rig r;
        r.s.spacing = sp;
        r.s.speed = 1.0;
        r.s.feedback = 0.7f;
        r.s.timeMs = 120.0;
        r.apply();
        r.run ((int) (kSr * 2), [] (int i) { return 0.3f * std::sin (i * 0.01f); });
        return r.left;
    };

    const auto grid = render (Spacing::Grid);
    const auto tape = render (Spacing::Tape);
    check (grid.size() == tape.size(), "same length");

    double worst = 0.0;
    for (size_t i = 0; i < grid.size(); ++i) worst = juce::jmax (worst, (double) std::abs (grid[i] - tape[i]));
    checkNear (worst, 0.0, 1.0e-5, "TAPE == GRID at r = 1");

    // at r = 2 the repetitions accelerate: more boundaries in the same time
    Rig fast;
    fast.s.spacing = Spacing::Tape;
    fast.s.speed = 2.0;
    fast.s.feedback = 0.8f;
    fast.s.timeMs = 400.0;
    fast.apply();
    fast.run ((int) (kSr * 3), [] (int i) { return i < 100 ? 0.5f : 0.0f; });
    check (fast.allFinite(), "tape runaway stays finite");
    check (! fast.engine.readOverrun(), "tape never chases a source still being written");
}

void testSyncBoundaries()
{
    Rig r;
    r.s.sync = true;
    r.s.divIndex = 8;         // 1/4
    r.s.feedback = 0.0f;
    r.s.speed = 1.0;
    r.apply();

    DelayEngine::Transport t;
    t.valid = true;
    t.playing = true;
    t.bpm = 143.0;            // fractional period
    t.ppq = 0.0;

    const double ppqPerSample = t.bpm / 60.0 / kSr;
    const int total = (int) (kSr * 4);
    juce::AudioBuffer<float> buf (2, kBlock);

    std::vector<float> out;
    for (int done = 0; done < total; done += kBlock)
    {
        const int nb = juce::jmin (kBlock, total - done);
        buf.setSize (2, nb, false, false, true);
        buf.clear();
        for (int i = 0; i < nb; ++i)
        {
            const float x = (done + i) % 64 == 0 ? 1.0f : 0.0f;
            buf.setSample (0, i, x);
            buf.setSample (1, i, x);
        }
        t.ppq = done * ppqPerSample;
        r.engine.setTransport (t);
        r.engine.setSettings (r.s);
        r.engine.process (buf);
        for (int i = 0; i < nb; ++i) out.push_back (buf.getSample (0, i));
    }

    const double beatSamples = 60.0 / t.bpm * kSr;
    checkNear (r.engine.getEffectiveTimeSamples(), beatSamples, 1.0, "sync T = one beat at 143 bpm");

    bool finite = true;
    for (float v : out) finite = finite && std::isfinite (v);
    check (finite, "sync output finite");
    check (! r.engine.readOverrun(), "sync: no read past written length");

    // division change mid-flight must not blow up
    r.s.divIndex = 11;        // 1/2
    r.apply();
    r.run ((int) (kSr * 2), [] (int) { return 0.2f; });
    check (r.allFinite(), "division change is clean");
}

void testEqAccumulates()
{
    auto repGain = [] (bool eqOn, FbType type)
    {
        Rig r;
        r.s.eqOn = eqOn;
        r.s.fbType = type;
        r.s.feedback = 1.0f;
        r.s.speed = 1.0;
        r.s.timeMs = 100.0;
        for (auto& b : r.s.eqDb) b = 0.0f;
        r.s.eqDb[3] = 12.0f;      // 1 kHz
        r.apply();

        const int T = (int) std::round (0.1 * kSr);
        const double w = 2.0 * juce::MathConstants<double>::pi * 1000.0 / kSr;
        r.run (T, [w] (int i) { return 0.1f * (float) std::sin (w * i); });
        r.runSilence (T * 4);

        std::vector<float> peaks;
        for (int k = 1; k <= 3; ++k) peaks.push_back (r.peak (k * T + T / 4, (k + 1) * T - T / 4));
        return peaks;
    };

    const auto flat = repGain (false, FbType::Raw);
    checkNear (flat[0], 0.1, 0.01, "EQ off: repetition 1 is unity");

    for (auto type : { FbType::Raw, FbType::Stable })
    {
        const auto boosted = repGain (true, type);
        checkNear (20.0 * std::log10 (boosted[0] / 0.1f), 12.0, 1.5, "rep 1 already carries +12 dB");
        checkNear (20.0 * std::log10 (boosted[1] / 0.1f), 24.0, 2.5, "rep 2 carries +24 dB");
    }
}

void testIntegerRatesAreLossless()
{
    // At r = 2 the read positions are integers, so a 20th-generation repeat keeps its
    // spectrum. Compare rep 1 and rep 8 peaks of a fixed tone, gain-normalised.
    Rig r;
    r.s.speed = 2.0;
    r.s.feedback = 1.0f;
    r.s.timeMs = 100.0;
    r.s.fbType = FbType::Stable;
    r.apply();

    const int T = (int) std::round (0.1 * kSr);
    const double w = 2.0 * juce::MathConstants<double>::pi * 8000.0 / kSr;
    r.run (T, [w] (int i) { return 0.2f * (float) std::sin (w * i); });
    r.runSilence (T * 10);

    const float rep1 = r.peak (T + 200, T + T / 2 - 200);
    const float rep8 = r.peak (8 * T + 200, 8 * T + T / 2 - 200);
    check (rep1 > 0.05f, "rep 1 audible");
    checkNear (rep8 / rep1, 1.0, 0.08, "integer rate: no generational HF loss");
}

void testPositionalEnvelope()
{
    // a rate drop while a repetition is fading out must un-fade it, not latch to silence
    Rig r;
    r.s.speed = 1.5;
    r.s.feedback = 0.0f;
    r.s.timeMs = 500.0;
    r.apply();

    const int T = (int) std::round (0.5 * kSr);
    r.run (T, [] (int) { return 0.4f; });
    r.runSilence (T / 2);       // into the tail of rep 1 (which lasts T/1.5)

    r.s.speed = 0.5;
    r.apply();
    const int before = r.written;
    r.runSilence (T);

    check (r.peak (before, before + T / 2) > 0.05f, "slowing down extends the repetition");
    check (r.allFinite(), "finite");
}

void testNoSpliceInsideGeneration()
{
    // A generation records input for one period, but its writing voice recycles past that
    // at any rate below 1, leaving a butt join at index periodLen. DC exposes it: a tone
    // at an exact multiple of 1/T would hide the step at a zero crossing.
    for (auto type : { FbType::Raw, FbType::Stable })
    {
        for (double speed : { 0.25, 0.5, 0.8, 1.0, 2.0 })
        {
            Rig r;
            r.s.speed = speed;
            r.s.feedback = 0.0f;
            r.s.fbType = type;
            r.s.timeMs = 200.0;
            r.apply();

            const int T = (int) std::round (0.2 * kSr);
            r.run (T * 8, [] (int) { return 0.4f; });

            // skip the forceFade transient that the initial time latch opens
            float maxStep = 0.0f;
            int at = 0;
            for (size_t i = (size_t) T * 4; i < r.left.size(); ++i)
            {
                const float d = std::abs (r.left[i] - r.left[i - 1]);
                if (d > maxStep) { maxStep = d; at = (int) i; }
            }

            // DC in, so every legitimate change is a raised-cosine fade of 8 ms at most
            check (maxStep < 0.01f, "no step at the input/recycle join");
            if (maxStep >= 0.01f)
                std::printf ("     %-6s speed %.2f step %.4f at n/T=%.3f\n",
                             type == FbType::Raw ? "raw" : "stable", speed, maxStep,
                             (double) at / T);
        }
    }
}

void testUnityAfterSpeedChange()
{
    // Returning to exactly 1x leaves generations that are longer than a period behind, so
    // repetitions overlap and no longer tile the grid. The unity bypass must not assume
    // they do, and the fade policy either side of a join must agree.
    for (double from : { 0.25, 0.5, 0.8, 2.0, 4.0, 1.0 })
    {
        Rig r;
        r.s.speed = from;
        r.s.feedback = 0.0f;
        r.s.eqOn = false;
        r.s.timeMs = 200.0;
        r.apply();

        const int T = (int) std::round (0.2 * kSr);
        auto tone = [] (int i) { return 0.4f * (float) std::sin (i * 0.004); };
        r.run (T * 6, tone);

        r.s.speed = 1.0;
        r.apply();
        const int mark = r.written;
        r.run (T * 10, tone);

        // the input's own slope bounds any legitimate step; a splice is ~0.4
        float maxStep = 0.0f;
        int at = 0;
        for (size_t i = (size_t) mark + (size_t) T; i < r.left.size(); ++i)
        {
            const float d = std::abs (r.left[i] - r.left[i - 1]);
            if (d > maxStep) { maxStep = d; at = (int) i; }
        }

        check (maxStep < 0.02f, "no step after returning to unity");
        if (maxStep >= 0.02f)
            std::printf ("     %.2f -> 1.0 step %.4f at %.2f T after the switch\n",
                         from, maxStep, (double) (at - mark) / T);
        check (r.engine.maxConcurrentVoices() <= kMaxVoices, "voice pool respected");
    }
}

void testNoAllocationInProcess()
{
    DelayEngine e;
    DelayEngine::Settings s;
    s.timeMs = 150.0;
    s.feedback = 0.8f;
    s.wet = 1.0f;
    e.setSettings (s);
    e.prepare (kSr, kBlock, 2);

    juce::AudioBuffer<float> buf (2, kBlock);
    DelayEngine::Transport t;
    t.valid = true;
    t.playing = true;
    t.bpm = 128.0;

    for (int i = 0; i < kBlock; ++i) { buf.setSample (0, i, 0.2f); buf.setSample (1, i, 0.2f); }
    e.process (buf);          // warm up outside the tracked window

    gAllocs.store (0);
    gTrackAllocs.store (true);

    for (int b = 0; b < 400; ++b)
    {
        // exercise every branch: boundaries, glides, sync, EQ, mode and spacing switches
        s.speed    = 0.25 + 3.5 * (b % 100) / 100.0;
        s.timeMs   = 40.0 + (b % 7) * 30.0;
        s.eqOn     = (b % 3) == 0;
        s.sync     = (b % 2) == 0;
        s.divIndex = b % numDivisions();
        s.spacing  = (b % 11) == 0 ? Spacing::Tape : Spacing::Grid;
        s.timeMode = (b % 13) == 0 ? TimeMode::Bend : TimeMode::Regrid;
        s.fbType   = (b % 17) == 0 ? FbType::Stable : FbType::Raw;
        for (auto& g : s.eqDb) g = (float) ((b % 5) - 2) * 3.0f;
        e.setSettings (s);

        t.ppq = b * kBlock * t.bpm / 60.0 / kSr;
        e.setTransport (t);

        for (int i = 0; i < kBlock; ++i)
        {
            const float x = 0.3f * std::sin ((b * kBlock + i) * 0.01f);
            buf.setSample (0, i, x);
            buf.setSample (1, i, x);
        }
        e.process (buf);

        DelayEngine::VoiceInfo info[kMaxVoices];
        e.getVoiceSnapshot (info, kMaxVoices);
        (void) e.getTailSeconds();
    }

    gTrackAllocs.store (false);
    const int n = gAllocs.load();
    check (n == 0, "the audio path allocates nothing");
    if (n != 0) std::printf ("     %d allocations during process()\n", n);
}

void testMinimumPeriodIsOneBuffer()
{
    // asking for less than a buffer must give a period of exactly one buffer
    for (int block : { 16, 64, 512, 2048 })
    {
        DelayEngine e;
        DelayEngine::Settings s;
        s.timeMs = kMinTimeParamMs;  // the knob floor, below every buffer size here
        s.speed = 1.0;
        s.feedback = 0.0f;
        s.dry = 0.0f;
        s.wet = 1.0f;
        e.setSettings (s);
        e.prepare (kSr, block, 2);

        check (e.getMinPeriodSamples() == block, "minimum period equals the buffer size");

        const int expected = juce::jmax (block, (int) std::round (kMinTimeParamMs * 0.001 * kSr));
        checkNear (e.getEffectiveTimeSamples(), expected, 1.0, "period floored at one buffer");

        // an impulse comes back no earlier than one buffer later
        std::vector<float> out;
        juce::AudioBuffer<float> buf (2, block);
        for (int b = 0; b < 40; ++b)
        {
            buf.clear();
            if (b == 0) { buf.setSample (0, 0, 1.0f); buf.setSample (1, 0, 1.0f); }
            e.process (buf);
            for (int i = 0; i < block; ++i) out.push_back (buf.getSample (0, i));
        }

        int hit = -1;
        for (size_t i = 0; i < out.size(); ++i)
            if (std::abs (out[i]) > 1.0e-4f) { hit = (int) i; break; }

        check (hit >= expected - 1, "first repeat is never earlier than the floor");
    }
}

void testMonoAndBlockSizes()
{
    for (int nch : { 1, 2 })
    {
        for (int block : { 32, 512, 2048 })
        {
            DelayEngine e;
            DelayEngine::Settings s;
            s.timeMs = 80.0;
            s.feedback = 0.8f;
            s.speed = 0.6;
            s.wet = 1.0f;
            e.setSettings (s);
            e.prepare (44100.0, block, nch);

            juce::AudioBuffer<float> buf (nch, block);
            bool finite = true;
            for (int b = 0; b < 200; ++b)
            {
                for (int ch = 0; ch < nch; ++ch)
                    for (int i = 0; i < block; ++i)
                        buf.setSample (ch, i, 0.3f * std::sin ((b * block + i) * 0.01f));
                e.process (buf);
                for (int ch = 0; ch < nch; ++ch)
                    for (int i = 0; i < block; ++i)
                        finite = finite && std::isfinite (buf.getSample (ch, i));
            }
            check (finite, "finite across channel counts and block sizes");
            check (e.maxConcurrentVoices() <= kMaxVoices, "voice pool respected");
        }
    }
}
} // namespace

int main()
{
    std::printf ("VarispeedDelay engine tests\n");

    test ("unity speed, zero feedback -> clean delay of exactly T", testUnityDelay);
    test ("raw r=2 compresses each generation toward the boundary", testRawSpeedUp);
    test ("stable keeps every repetition at the same speed", testStableConstantDuration);
    test ("r=0.5 repetitions overlap", testSlowOverlap);
    test ("voice pool and buffer bounds hold", testVoiceAndBufferLimits);
    test ("speed sweep 0.25 -> 4 is smooth and safe", testSpeedSweep);
    test ("REGRID shrink 20 s -> 200 ms", testRegridShrink);
    test ("BEND glide rate limits and settle", testBendGlide);
    test ("time deadband kills the unity tremolo", testDeadband);
    test ("tail = 10 x Dmax and tracks T", testTailTracksTime);
    test ("soft clip shape", testSoftClip);
    test ("fb = 2 saturates instead of exploding", testRunawayBounded);
    test ("TAPE spacing", testTapeSpacing);
    test ("sync boundaries at fractional bpm", testSyncBoundaries);
    test ("EQ accumulates, rep 1 already filtered", testEqAccumulates);
    test ("integer rates are lossless", testIntegerRatesAreLossless);
    test ("positional envelope un-fades", testPositionalEnvelope);
    test ("no splice at the input/recycle join", testNoSpliceInsideGeneration);
    test ("unity is clean after a speed change", testUnityAfterSpeedChange);
    test ("no allocation on the audio thread", testNoAllocationInProcess);
    test ("minimum period is one buffer", testMinimumPeriodIsOneBuffer);
    test ("mono and odd block sizes", testMonoAndBlockSizes);

    std::printf ("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
