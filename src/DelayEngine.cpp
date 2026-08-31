#include "DelayEngine.h"

#include <algorithm>
#include <cmath>

namespace vspd
{

namespace
{
constexpr Division kDivs[] = {
    { "1/32",   0.125,     false },
    { "1/16T",  1.0 / 6.0, false },
    { "1/16",   0.25,      false },
    { "1/16D",  0.375,     false },
    { "1/8T",   1.0 / 3.0, false },
    { "1/8",    0.5,       false },
    { "1/8D",   0.75,      false },
    { "1/4T",   2.0 / 3.0, false },
    { "1/4",    1.0,       false },
    { "1/4D",   1.5,       false },
    { "1/2T",   4.0 / 3.0, false },
    { "1/2",    2.0,       false },
    { "1/2D",   3.0,       false },
    { "1 bar",  4.0,       true  },
    { "2 bars", 8.0,       true  },
};

inline double raisedCos (double x) noexcept
{
    x = juce::jlimit (0.0, 1.0, x);
    return 0.5 - 0.5 * std::cos (juce::MathConstants<double>::pi * x);
}
} // namespace

int numDivisions() { return (int) (sizeof (kDivs) / sizeof (kDivs[0])); }
const Division& division (int i) { return kDivs[juce::jlimit (0, numDivisions() - 1, i)]; }

float softClip (float x, bool on, float threshold) noexcept
{
    if (on)
    {
        // a threshold at the ceiling would divide by zero, so pin it just below
        const float t = juce::jlimit (0.0f, kClipCeiling - 1.0e-3f, threshold);
        const float a = std::abs (x);
        if (a > t)
        {
            constexpr float c = kClipCeiling;
            x = std::copysign (t + (c - t) * std::tanh ((a - t) / (c - t)), x);
        }
    }
    return juce::jlimit (-kSafetyClamp, kSafetyClamp, x);
}

//==============================================================================
void DelayEngine::prepare (double sampleRate, int maxBlockSize, int nch)
{
    sr = sampleRate;
    numChannels = juce::jlimit (1, 2, nch);
    maxLen = (int) std::ceil (kMaxRepSeconds * sr);
    // A period shorter than one block would open several generations inside a single
    // process call, so the floor is the host's buffer size.
    minPeriod = juce::jmax (1, maxBlockSize);

    for (auto& g : gens)
        g.buf.setSize (numChannels, maxLen, false, true, false);

    eq.prepare (sr);
    speedSm.reset (sr, kSpeedGlideMs * 0.001);
    bendGoal.reset (sr, kTimeGlideMs * 0.001);
    fbSm.reset (sr, 0.02);
    clipSm.reset (sr, 0.02);
    drySm.reset (sr, 0.02);
    wetSm.reset (sr, 0.02);

    wasRunning = false;
    posValid = false;
    expectedTimeSec = 0.0;

    reset();
}

void DelayEngine::reset()
{
    for (auto& g : gens) { g.buf.clear(); g.resetState(); }
    for (auto& v : voices) v = Voice{};

    genCounter = 0;
    curSlot = 0;
    n = 0;
    spawnOrder = 0;
    peakVoices = 0;
    clipHold = 0;
    overrun = false;
    declickLeft = 0;
    for (auto& v : lastWetOut) v = 0.0f;

    tLatched = juce::jlimit ((double) minPeriod, (double) maxLen, settings.timeMs * 0.001 * sr);
    tTarget = tEff = tLatched;
    bendGoal.setCurrentAndTargetValue (tLatched);
    periodLen = juce::jmax (minPeriod, (int) std::round (tEff));
    lastBend = 1.0;

    forceFadeCounter = 0;
    nonUnitySeen = false;
    lastSpawnFadedOut = false;
    timeChangedSeen = false;
    tapeAnchor = true;
    forceBoundary = false;

    syncActive = false;
    divPpq = lastDivPpq = 1.0;
    ppqPerSample = ppqBlockStart = expectedPpq = nextBoundaryPpq = 0.0;

    speedSm.setCurrentAndTargetValue (juce::jlimit (kMinSpeed, kMaxSpeed, settings.speed));
    fbSm.setCurrentAndTargetValue (settings.feedback);
    clipSm.setCurrentAndTargetValue (settings.clipThresh);
    drySm.setCurrentAndTargetValue (settings.dry);
    wetSm.setCurrentAndTargetValue (settings.wet);

    updateTail();
    publishUi();
}

//==============================================================================
/** A host restart has to sound like a fresh start: stop/rewind/play and every pass of a
    cycle must replay identically, so the engine drops its buffers whenever the transport
    starts or the timeline jumps. Position is read in seconds, not ppq, so it works in
    hosts with no tempo and while sync is off. */
bool DelayEngine::transportRestarted (int numSamples)
{
    const bool running = transport.valid && transport.running;
    bool restart = false;

    if (running)
    {
        // one block start is never more than a few samples from where the last one ended
        const double tol = std::max (1.0e-4, 4.0 / sr);
        if (! wasRunning)
            restart = true;
        else if (posValid && transport.timeValid && std::abs (transport.timeSec - expectedTimeSec) > tol)
            restart = true;

        expectedTimeSec = transport.timeSec + numSamples / sr;
        posValid = transport.timeValid;
    }
    else
    {
        posValid = false;
    }

    wasRunning = running;
    return restart;
}

void DelayEngine::updateTail()
{
    const double dmax = settings.spacing == Spacing::Tape
                          ? (double) maxLen
                          : std::min (kOverlapFactor * tLatched, (double) maxLen);
    tailSeconds.store (kTailGenerations * dmax / sr, std::memory_order_relaxed);
}

//==============================================================================
void DelayEngine::updateTiming (int numSamples)
{
    const bool wantSync = settings.sync;
    const bool transportRunning = transport.valid && transport.playing;
    const double bpm = (transport.valid && transport.bpm > 1.0) ? transport.bpm : fallbackBpm;
    uiBpm.store (bpm, std::memory_order_relaxed);

    const Division& d = division (settings.divIndex);
    double quarters = d.quarters;
    if (d.isBar)
    {
        const double barQuarters = 4.0 * (double) juce::jmax (1, transport.tsNum)
                                       / (double) juce::jmax (1, transport.tsDen);
        quarters = d.quarters * barQuarters / 4.0;
    }
    divPpq = juce::jmax (1.0e-6, quarters);

    double newT = wantSync ? (60.0 / bpm * quarters * sr) : (settings.timeMs * 0.001 * sr);
    newT = juce::jlimit ((double) minPeriod, (double) maxLen, newT);

    // REGRID re-splices on every latch, so it needs the relative deadband; anything finer
    // than a sample is float noise, and a fixed millisecond floor would make sub-millisecond
    // periods unreachable. BEND glides instead of splicing, so it tracks the knob directly -
    // quantising its target to 0.5 % steps is what turns a drag into a staircase.
    const bool bendMode = settings.timeMode == TimeMode::Bend;
    const double deadband = bendMode ? 1.0 : std::max (0.005 * tLatched, 1.0);
    if (std::abs (newT - tLatched) >= deadband)
    {
        tLatched = newT;
        timeChangedSeen = true;
        tapeAnchor = true;
        updateTail();
    }
    tTarget = tLatched;
    // The knob arrives as one step per block. Ramping the goal spreads each step over the
    // interval it covers, so the bend factor follows the drag rate instead of pulsing to
    // the rate limit and back between blocks.
    if (bendMode) bendGoal.setTargetValue (tTarget);
    else          bendGoal.setCurrentAndTargetValue (tTarget);

    const bool nowSync = wantSync && transportRunning;
    if (nowSync)
    {
        ppqPerSample = bpm / 60.0 / sr;
        const double ppqStart = transport.ppq;
        const bool relock = ! syncActive || std::abs (divPpq - lastDivPpq) > 0.0;
        const bool jumped = syncActive && ! relock
                            && std::abs (ppqStart - expectedPpq) > std::max (1.0e-4, ppqPerSample * 4.0);

        if (relock)
        {
            nextBoundaryPpq = std::ceil (ppqStart / divPpq - 1.0e-9) * divPpq;
            tapeAnchor = true;
        }
        else if (jumped)
        {
            nextBoundaryPpq = std::ceil (ppqStart / divPpq - 1.0e-9) * divPpq;
            forceBoundary = true;
            tapeAnchor = true;
        }

        ppqBlockStart = ppqStart;
        expectedPpq = ppqStart + numSamples * ppqPerSample;
    }
    else if (syncActive)
    {
        tapeAnchor = true;   // re-anchor when the transport comes back
    }

    lastDivPpq = divPpq;
    syncActive = nowSync;
}

//==============================================================================
double DelayEngine::sourceLength (int slot) const
{
    const Gen& g = gens[slot];
    double len = std::max ((double) g.written, (double) g.inputLen);

    if (g.writer >= 0)
    {
        const Voice& wv = voices[g.writer];
        if (wv.active && wv.writing)
            len = std::max (len, wv.predWriteEnd);
    }
    return len;
}

float DelayEngine::readInterp (int slot, double pos, int ch) const
{
    const Gen& g = gens[slot];
    if (g.written <= 0 || pos < 0.0) return 0.0f;

    const float* d = g.buf.getReadPointer (ch);
    const int last = g.written - 1;
    const int i0 = (int) pos;
    if (i0 >= last) return d[last];
    return d[i0] + (float) (pos - i0) * (d[i0 + 1] - d[i0]);
}

float DelayEngine::readAt (int slot, int idx, int ch) const
{
    const Gen& g = gens[slot];
    if (idx < 0 || idx >= g.written) return 0.0f;
    return g.buf.getReadPointer (ch)[idx];
}

//==============================================================================
int DelayEngine::allocVoice()
{
    for (int i = 0; i < kMaxVoices; ++i)
        if (! voices[i].active) return i;

    int best = 0;
    for (int i = 1; i < kMaxVoices; ++i)
    {
        const Voice& a = voices[best];
        const Voice& b = voices[i];
        if (a.retiring != b.retiring) { if (b.retiring) best = i; }
        else if (b.order < a.order) best = i;
    }
    deactivateVoice (best);
    return best;
}

void DelayEngine::retireVoice (int index)
{
    Voice& v = voices[index];
    v.retiring = true;
    v.retireEnv = v.env;
    v.retireLeft = v.xfade;
    if (v.writing)
    {
        v.writing = false;
        if (gens[v.dst].writer == index) gens[v.dst].writer = -1;
    }
}

void DelayEngine::deactivateVoice (int index)
{
    Voice& v = voices[index];
    if (v.writing && gens[v.dst].writer == index) gens[v.dst].writer = -1;
    v.active = false;
    v.writing = false;
    v.env = 0.0f;
}

void DelayEngine::openGeneration (double rEff)
{
    const int newGen = genCounter + 1;
    const int slot = newGen % kNumGenBuffers;
    const int srcSlot = (newGen - 1) % kNumGenBuffers;

    // Voices never outlive their source: retire everything touching the recycled slot.
    for (int i = 0; i < kMaxVoices; ++i)
    {
        Voice& v = voices[i];
        if (v.active && ! v.retiring && (v.src == slot || v.dst == slot))
        {
            retireVoice (i);
            if (v.src == slot) v.sourceLost = true;
        }
    }

    if (nonUnitySeen || timeChangedSeen) forceFadeCounter = 2;
    else if (forceFadeCounter > 0)       --forceFadeCounter;
    nonUnitySeen = false;
    timeChangedSeen = false;

    const double srcLen = sourceLength (srcSlot);

    genCounter = newGen;
    curSlot = slot;
    n = 0;
    gens[slot].resetState();

    bool spawned = false;
    int spawnedIndex = -1;
    double spawnDur = 0.0, spawnWriteEnd = 0.0, spawnXfade = 0.0;

    if (srcLen > 0.5)
    {
        const int vi = allocVoice();
        Voice& v = voices[vi];
        v = Voice{};
        v.active = true;
        v.writing = true;
        v.src = srcSlot;
        v.dst = slot;
        v.fbType = settings.fbType;
        v.order = ++spawnOrder;
        // The 4x factor exists to bound how many repetitions can sound at once, which only
        // happens in GRID. TAPE repetitions are back to back, so capping them at 4T buys
        // nothing and truncates the runaway long before the buffer is full: at 0.5x it would
        // record 8 s windows and replay only the first 4 s of each.
        v.dmax = settings.spacing == Spacing::Tape
                   ? (double) maxLen
                   : std::min (kOverlapFactor * tEff, (double) maxLen);

        const double dEst = srcLen / std::max (rEff, 1.0e-6);
        v.predDur = std::min (dEst, v.dmax);
        // Raw recycles the varispeed tap, so the write ends with the audible tap.
        // Stable recycles a unity copy, which is exactly as long as its source.
        v.predWriteEnd = v.fbType == FbType::Raw ? v.predDur : std::min (srcLen, v.dmax);

        const double xf = juce::jlimit (kXfadeMinMs * 0.001 * sr,
                                        kXfadeMaxMs * 0.001 * sr,
                                        kXfadePct * std::min (tEff, dEst));
        v.xfade = std::max (1.0, std::min (xf, dEst * 0.5));

        gens[slot].writer = vi;
        spawnedIndex = vi;
        spawned = true;
        spawnDur = v.predDur;
        spawnWriteEnd = v.predWriteEnd;
        spawnXfade = v.xfade;
    }

    if (settings.spacing == Spacing::Tape && spawned && ! tapeAnchor)
        periodLen = (int) juce::jlimit ((double) minPeriod, (double) maxLen, std::round (spawnDur));
    else
        periodLen = (int) juce::jlimit ((double) minPeriod, (double) maxLen, std::round (tEff));

    // A generation records input for one period, but its writing voice keeps recycling
   // past that whenever the repetition outlasts the period (any rate below 1). That leaves
   // a butt join at index periodLen which no voice envelope covers, so crossfade the
   // recorded input out into the recycled-only tail. At rate 1 and above there is no join
   // and no taper, which keeps unity sample-exact.
    if (spawned && spawnWriteEnd > (double) periodLen + 1.0)
        gens[slot].inputTaper = (int) std::max (1.0, std::min ((double) periodLen * 0.5, spawnXfade));

    // The unity bypass assumes this repetition tiles the grid: that it starts where the
    // previous one ended. That only holds when the source is exactly one period long.
    // A source left over from a slower setting is longer, so the voice is mid-buffer when
    // its successor starts a fresh one and the join is a real splice — fade it.
    if (spawnedIndex >= 0)
    {
        Voice& v = voices[spawnedIndex];
        v.contiguous = srcLen <= (double) periodLen + 1.0;

        // Fade policy has to agree across a join: if the outgoing repetition fades out and
        // the incoming one does not fade in, the seam is a cliff rather than a dip. So a
        // voice inherits a fade-in from its predecessor's fade-out, and the bypass
        // re-engages one generation after everything is clean again.
        v.fadeOut = forceFadeCounter > 0 || ! v.contiguous;
        v.fadeIn = v.fadeOut || lastSpawnFadedOut;
        lastSpawnFadedOut = v.fadeOut;
    }

    tapeAnchor = false;
}

//==============================================================================
void DelayEngine::process (juce::AudioBuffer<float>& buffer)
{
    const int ns = buffer.getNumSamples();
    if (ns <= 0) return;
    const int nch = juce::jmin (numChannels, buffer.getNumChannels());
    if (nch <= 0) return;

    if (transportRestarted (ns))
    {
        // The ramp is added to the output only, never to a generation buffer, so the delay
        // content after a restart is identical every pass.
        float held[2] { lastWetOut[0], lastWetOut[1] };
        reset();
        declickLen = juce::jmax (1, (int) (kResetDeclickMs * 0.001 * sr));
        declickLeft = declickLen;
        for (int ch = 0; ch < 2; ++ch) declickVal[ch] = held[ch];
    }

    updateTiming (ns);

    for (int b = 0; b < kNumEqBands; ++b) eq.setGainDb (b, settings.eqDb[b]);
    eq.updateCoefficients();

    speedSm.setTargetValue (juce::jlimit (kMinSpeed, kMaxSpeed, settings.speed));
    fbSm.setTargetValue (settings.feedback);
    clipSm.setTargetValue (settings.clipThresh);
    drySm.setTargetValue (settings.dry);
    wetSm.setTargetValue (settings.wet);

    const bool eqOn = settings.eqOn;
    const bool clipOn = settings.clip;
    const bool bend = settings.timeMode == TimeMode::Bend;
    const bool syncGrid = syncActive && settings.spacing == Spacing::Grid;

    float* out[2] { buffer.getWritePointer (0), nch > 1 ? buffer.getWritePointer (1) : nullptr };

    for (int i = 0; i < ns; ++i)
    {
        const double r = speedSm.getNextValue();
        double m = 1.0;
        if (bend)
        {
            const double dT = juce::jlimit (-kBendDown, kBendUp, bendGoal.getNextValue() - tEff);
            tEff += dT;
            m = 1.0 - dT;
        }
        else
        {
            tEff = tTarget;
        }
        lastBend = m;

        const double rEff = juce::jlimit (0.01, 64.0, r * m);
        if (std::abs (rEff - 1.0) >= kUnityEpsilon) nonUnitySeen = true;

        // The period always ends at the *current* effective T, so a big shrink reacts
        // within one new period instead of latching to the old boundary. Only TAPE, whose
        // period is the previous repetition's duration, keeps its latched length.
        auto effPeriod = [&]
        {
            return (settings.spacing == Spacing::Tape && ! tapeAnchor)
                     ? periodLen
                     : juce::jmax (minPeriod, (int) std::round (tEff));
        };

        bool boundary = forceBoundary;
        forceBoundary = false;
        const double ppqNow = syncGrid ? ppqBlockStart + i * ppqPerSample : 0.0;

        if (syncGrid) { if (ppqNow >= nextBoundaryPpq - 1.0e-12) boundary = true; }
        else          { if (n >= effPeriod()) boundary = true; }

        if (boundary)
        {
            if (syncGrid)
                nextBoundaryPpq = (std::floor (ppqNow / divPpq + 1.0e-9) + 1.0) * divPpq;
            openGeneration (rEff);
        }

        const double samplesToBoundary = syncGrid
            ? (ppqPerSample > 0.0 ? (nextBoundaryPpq - ppqNow) / ppqPerSample : (double) maxLen)
            : (double) (effPeriod() - n);

        float x[2] { 0.0f, 0.0f };
        for (int ch = 0; ch < nch; ++ch) x[ch] = out[ch][i];

        const float fb = fbSm.getNextValue();
        const float clipThresh = clipSm.getNextValue();
        const float dryG = drySm.getNextValue();
        const float wetG = wetSm.getNextValue();

        float wetSum[2] { 0.0f, 0.0f };
        int live = 0;
        bool clipped = false;

        for (int vi = 0; vi < kMaxVoices; ++vi)
        {
            Voice& v = voices[vi];
            if (! v.active) continue;

            Gen& gs = gens[v.src];
            const bool srcOpen = (gs.writer >= 0) || (v.src == curSlot);
            if (! v.retiring && srcOpen && gs.written > 0 && v.pOut > (double) gs.written - 1.0)
            {
                v.pOut = (double) gs.written - 1.0;   // chasing read: ride the writer's edge
                overrun = true;
            }

            double envd = 0.0;
            bool retireDone = false;

            if (v.retiring)
            {
                envd = raisedCos (v.retireLeft / v.xfade) * v.retireEnv;
                v.retireLeft -= 1.0;
                if (v.retireLeft <= 0.0) retireDone = true;
            }
            else if (! v.silent)
            {
                const double srcL = sourceLength (v.src);
                const double toContent = (srcL - v.pOut) / rEff;
                const double toCap = v.dmax - v.elapsed;

                if (toContent <= 0.0 || toCap <= 0.0)
                {
                    // Stable's unity copy outlives its audible tap at rates above 1
                    if (v.fbType == FbType::Stable && v.writing) v.silent = true;
                    else { deactivateVoice (vi); continue; }
                }
                else
                {
                    // At unity a repetition can butt-join its neighbours sample-exactly, so
                    // only the sides that are genuine splices get an envelope. The Dmax cap
                    // is always a truncation and always fades.
                    const bool unity = std::abs (rEff - 1.0) < kUnityEpsilon;
                    double a = toCap;
                    if (v.fadeOut || ! unity) a = std::min (a, toContent);
                    if (v.fadeIn  || ! unity) a = std::min (a, v.elapsed);
                    envd = raisedCos (a / v.xfade);
                }
            }

            v.env = (float) envd;

            if (v.silent && ! v.writing) { deactivateVoice (vi); continue; }

            ++live;

            for (int ch = 0; ch < nch; ++ch)
            {
                if (! v.silent)
                {
                    float s;
                    if (v.sourceLost)
                    {
                        s = v.last[ch];
                    }
                    else
                    {
                        s = readInterp (v.src, v.pOut, ch);
                        if (eqOn) s = eq.process (v.eqOut, s, ch);
                        v.last[ch] = s;
                    }

                    const float audible = s * (float) envd;
                    wetSum[ch] += audible;

                    if (v.writing && v.fbType == FbType::Raw)
                    {
                        const float pre = audible * fb;
                        clipped = clipped || std::abs (pre) > clipThresh;
                        gens[v.dst].buf.getWritePointer (ch)[v.w] = softClip (pre, clipOn, clipThresh);
                    }
                }

                if (v.writing && v.fbType == FbType::Stable)
                {
                    const float u = readAt (v.src, v.w, ch);
                    const float rec = eqOn ? eq.process (v.eqRec, u, ch) : u;
                    const float pre = rec * fb;
                    clipped = clipped || std::abs (pre) > clipThresh;
                    gens[v.dst].buf.getWritePointer (ch)[v.w] = softClip (pre, clipOn, clipThresh);
                }
            }

            if (v.writing)
            {
                Gen& gd = gens[v.dst];
                ++v.w;
                if (v.w > gd.written) gd.written = v.w;

                // Raw writes for as long as it sounds; Stable's copy ends with its source
                if (v.w >= maxLen
                    || (v.fbType == FbType::Stable && (double) v.w >= v.predWriteEnd))
                {
                    v.writing = false;
                    if (gd.writer == vi) gd.writer = -1;
                }
            }

            if (! v.silent)
            {
                v.pOut += rEff;
                v.elapsed += 1.0;
            }

            if (retireDone) deactivateVoice (vi);
        }

        if (live > peakVoices) peakVoices = live;

        if (clipped && clipOn) clipHold = (int) (kClipHoldMs * 0.001 * sr);
        else if (clipHold > 0) --clipHold;

        if (n < maxLen)
        {
            Gen& cg = gens[curSlot];
            float inGain = 1.0f;
            if (cg.inputTaper > 0 && samplesToBoundary < (double) cg.inputTaper)
                inGain = (float) raisedCos (std::max (0.0, samplesToBoundary) / cg.inputTaper);

            const bool add = cg.written > n;
            for (int ch = 0; ch < nch; ++ch)
            {
                float* d = cg.buf.getWritePointer (ch);
                if (add) d[n] += x[ch] * inGain;
                else     d[n]  = x[ch] * inGain;
            }
            if (! add) cg.written = n + 1;
            cg.inputLen = n + 1;
        }

        for (int ch = 0; ch < nch; ++ch)
        {
            lastWetOut[ch] = wetSum[ch] * wetG;
            out[ch][i] = x[ch] * dryG + lastWetOut[ch];
        }

        if (declickLeft > 0)
        {
            const float g = (float) raisedCos ((double) declickLeft / declickLen);
            for (int ch = 0; ch < nch; ++ch) out[ch][i] += declickVal[ch] * g;
            --declickLeft;
        }

        ++n;
    }

    publishUi();
}

//==============================================================================
void DelayEngine::publishUi()
{
    int count = 0;
    double repMs = 0.0;
    for (int i = 0; i < kMaxVoices; ++i)
    {
        const Voice& v = voices[i];
        if (v.active)
        {
            uiVoiceElapsed[count].store ((float) (v.elapsed / sr * 1000.0), std::memory_order_relaxed);
            uiVoiceDuration[count].store ((float) (v.predDur / sr * 1000.0), std::memory_order_relaxed);
            uiVoiceEnv[count].store (v.env, std::memory_order_relaxed);
            repMs = std::max (repMs, v.predDur / sr * 1000.0);
            ++count;
        }
    }
    uiVoices.store (count, std::memory_order_relaxed);
    uiClipping.store (clipHold > 0, std::memory_order_relaxed);
    uiPeriodMs.store (periodLen / sr * 1000.0, std::memory_order_relaxed);
    uiEffTimeMs.store (tEff / sr * 1000.0, std::memory_order_relaxed);
    if (repMs > 0.0) uiRepMs.store (repMs, std::memory_order_relaxed);
}

int DelayEngine::getVoiceSnapshot (VoiceInfo* dest, int maxCount) const
{
    const int count = juce::jmin (maxCount, uiVoices.load (std::memory_order_relaxed));
    for (int i = 0; i < count; ++i)
    {
        dest[i].elapsed  = uiVoiceElapsed[i].load (std::memory_order_relaxed);
        dest[i].duration = uiVoiceDuration[i].load (std::memory_order_relaxed);
        dest[i].env      = uiVoiceEnv[i].load (std::memory_order_relaxed);
    }
    return count;
}

} // namespace vspd
