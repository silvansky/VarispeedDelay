#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <atomic>

#include "Voice.h"

namespace vspd
{

inline constexpr int    kNumGenBuffers   = 6;
inline constexpr int    kMaxVoices       = 5;
inline constexpr double kMaxRepSeconds   = 20.0;
inline constexpr double kMinTimeParamMs  = 0.1;   // knob floor; the engine clamps to one buffer
inline constexpr double kMaxDelayMs      = 20000.0;
inline constexpr double kMinSpeed        = 0.25;
inline constexpr double kMaxSpeed        = 4.0;
inline constexpr double kOverlapFactor   = 4.0;
inline constexpr float  kClipThreshold   = 0.5f;   // engine default; the knob's -6 dB is 0.5012
inline constexpr float  kClipThreshMinDb = -36.0f;
inline constexpr float  kClipThreshMaxDb = -1.0f;
inline constexpr float  kClipThreshDefDb = -6.0f;
inline constexpr float  kClipCeiling     = 1.0f;
inline constexpr float  kSafetyClamp     = 8.0f;
inline constexpr double kSpeedGlideMs    = 20.0;
inline constexpr double kTimeGlideMs     = 50.0;
inline constexpr double kBendDown        = 3.0;
inline constexpr double kBendUp          = 0.75;
inline constexpr double kXfadePct        = 0.05;
inline constexpr double kXfadeMinMs      = 0.25;
inline constexpr double kXfadeMaxMs      = 8.0;
inline constexpr double kTailGenerations = 10.0;
inline constexpr double kUnityEpsilon    = 1.0e-4;
inline constexpr double kClipHoldMs      = 120.0;   // so a 30 Hz UI cannot miss a hit

enum class TimeMode { Regrid = 0, Bend };
enum class Spacing  { Grid = 0, Tape };

struct Division { const char* name; double quarters; bool isBar; };
int numDivisions();
const Division& division (int index);

float softClip (float x, bool on, float threshold = kClipThreshold) noexcept;

class DelayEngine
{
public:
    struct Settings
    {
        double   timeMs   = 500.0;
        bool     sync     = false;
        int      divIndex = 8;
        TimeMode timeMode = TimeMode::Regrid;
        double   speed    = 1.0;
        float    feedback = 0.5f;
        FbType   fbType   = FbType::Raw;
        bool     clip     = true;
        float    clipThresh = kClipThreshold;   // linear, not dB
        Spacing  spacing  = Spacing::Grid;
        bool     eqOn     = false;
        float    eqDb[kNumEqBands] {};
        float    dry      = 1.0f;
        float    wet      = 0.5f;
    };

    struct Transport
    {
        bool   valid   = false;
        bool   playing = false;
        double bpm     = 120.0;
        double ppq     = 0.0;
        int    tsNum   = 4;
        int    tsDen   = 4;
    };

    struct VoiceInfo { float elapsed = 0.0f, duration = 0.0f, env = 0.0f; };

    void prepare (double sampleRate, int maxBlockSize, int numChannels);
    void reset();

    void setSettings (const Settings& s) noexcept { settings = s; }
    void setTransport (const Transport& t) noexcept { transport = t; }
    void setFallbackBpm (double b) noexcept { fallbackBpm = b; }

    void process (juce::AudioBuffer<float>& buffer);

    double getTailSeconds()  const noexcept { return tailSeconds.load (std::memory_order_relaxed); }
    /** The period the engine is actually running: in TAPE the previous repetition's
        duration, not the time knob, and never shorter than one buffer. */
    double getPeriodMs()     const noexcept { return uiPeriodMs.load (std::memory_order_relaxed); }
    double getEffectiveTimeMs() const noexcept { return uiEffTimeMs.load (std::memory_order_relaxed); }
    double getRepetitionMs() const noexcept { return uiRepMs.load (std::memory_order_relaxed); }
    /** The tempo the sync grid is using: the host's when it has one, else the tapped fallback. */
    double getBpm()          const noexcept { return uiBpm.load (std::memory_order_relaxed); }
    int    getActiveVoices() const noexcept { return uiVoices.load (std::memory_order_relaxed); }
    bool   isClipping()      const noexcept { return uiClipping.load (std::memory_order_relaxed); }
    int    getVoiceSnapshot (VoiceInfo* dest, int maxCount) const;

    /** Shortest period the engine will run — the host's buffer size. */
    int    getMinPeriodSamples() const noexcept { return minPeriod; }
    double getMinPeriodMs() const noexcept { return minPeriod / sr * 1000.0; }

    // test hooks
    int  getGenWritten (int slot) const noexcept { return gens[slot].written; }
    const Voice& getVoice (int i) const noexcept { return voices[i]; }
    double getEffectiveTimeSamples() const noexcept { return tEff; }
    int    getPeriodSamples() const noexcept { return periodLen; }
    double getBendFactor() const noexcept { return lastBend; }
    float  getClipThreshold() const noexcept { return clipSm.getCurrentValue(); }
    bool   readOverrun() const noexcept { return overrun; }
    int    maxConcurrentVoices() const noexcept { return peakVoices; }

private:
    struct Gen
    {
        juce::AudioBuffer<float> buf;
        int inputLen   = 0;
        int written    = 0;
        int writer     = -1;
        int inputTaper = 0;   // crossfade into the recycled-only tail, 0 when there is none
        void resetState() noexcept { inputLen = 0; written = 0; writer = -1; inputTaper = 0; }
    };

    void   updateTiming (int numSamples);
    void   openGeneration (double rEff);
    void   retireVoice (int index);
    void   deactivateVoice (int index);
    int    allocVoice();
    double sourceLength (int slot) const;
    float  readInterp (int slot, double pos, int ch) const;
    float  readAt (int slot, int idx, int ch) const;
    void   updateTail();
    void   publishUi();

    Settings  settings;
    Transport transport;
    GraphicEQ eq;

    Gen   gens[kNumGenBuffers];
    Voice voices[kMaxVoices];

    double sr = 44100.0;
    int    numChannels = 2;
    int    maxLen = 0;
    int    minPeriod = 1;

    int    genCounter = 0;
    int    curSlot = 0;
    int    n = 0;
    int    periodLen = 1;
    int    spawnOrder = 0;

    double tLatched = 0.0, tTarget = 0.0, tEff = 0.0;
    double lastBend = 1.0;
    double fallbackBpm = 120.0;

    int    forceFadeCounter = 0;
    bool   nonUnitySeen = false;
    bool   lastSpawnFadedOut = false;
    bool   timeChangedSeen = false;
    bool   tapeAnchor = true;
    bool   forceBoundary = false;

    bool   syncActive = false;
    double divPpq = 1.0, lastDivPpq = 1.0;
    double ppqPerSample = 0.0, ppqBlockStart = 0.0, expectedPpq = 0.0, nextBoundaryPpq = 0.0;

    int    clipHold = 0;
    bool   overrun = false;
    int    peakVoices = 0;

    juce::SmoothedValue<double, juce::ValueSmoothingTypes::Multiplicative> speedSm { 1.0 };
    juce::SmoothedValue<double> bendGoal { 0.0 };
    juce::SmoothedValue<float>  fbSm { 0.5f }, drySm { 1.0f }, wetSm { 0.5f };
    juce::SmoothedValue<float>  clipSm { kClipThreshold };

    std::atomic<double> tailSeconds { 1.0 };
    std::atomic<double> uiPeriodMs { 500.0 };
    std::atomic<double> uiEffTimeMs { 500.0 };
    std::atomic<double> uiRepMs { 500.0 };
    std::atomic<double> uiBpm { 120.0 };
    std::atomic<int>    uiVoices { 0 };
    std::atomic<bool>   uiClipping { false };
    std::atomic<float>  uiVoiceElapsed[kMaxVoices] {};
    std::atomic<float>  uiVoiceDuration[kMaxVoices] {};
    std::atomic<float>  uiVoiceEnv[kMaxVoices] {};
};

} // namespace vspd
