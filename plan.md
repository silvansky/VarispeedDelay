# Varispeed Delay — implementation plan

Delay where every repetition is re-played at a different tape speed: pitch and duration
change together. Two feedback topologies (cumulative / one-shot), a 7-band graphic EQ in
the recycle path, click protection via short fades at each repetition boundary.
Repetitions slower than the delay time overlap the following ones — they are never cut
short at the period boundary.

## Status

Project is initialized: CMake + JUCE 8.0.12 submodule, VST3/AU/Standalone targets, empty
passthrough processor + placeholder editor. All three formats build clean. No DSP yet.

```
CMakeLists.txt              juce_add_plugin, code VSil/Vspd, C++20
cmake/BuildDate.{cmake,h.in}
install-au.sh               ad-hoc sign + install to /Library + auval
libs/JUCE                   submodule @ 501c0767 (8.0.12-4)
src/PluginProcessor.{h,cpp} passthrough, empty APVTS
src/PluginEditor.{h,cpp}    title + build date
```

Build: `cd build && cmake .. -DCMAKE_BUILD_TYPE=Release && cmake --build . -j8`
Run: `open build/VarispeedDelay_artefacts/Release/Standalone/VarispeedDelay.app`

## Core model

A **period** = the delay time `T` (samples). At every period boundary the engine opens a
new **generation buffer** and spawns one **voice**. Voice `k` reads generation `G[k-1]`
at the varispeed rate and writes its result into `G[k]`. Repetitions are therefore
independent, overlapping playbacks — a repetition that lasts longer than `T` keeps
sounding while the next one starts.

```
period k-1          period k            period k+1          period k+2
─────────────────── ─────────────────── ─────────────────── ───────────────
input ─► G[k-1] ──► voice k ─► G[k] ──► voice k+1 ─► G[k+1] ──► …
                    (rate r)            (rate r)
                    wet out             wet out
r = 0.5:            ████████████████████████████ rep 1 (2T)
                                        ████████████████████████████ rep 2 (4T)
                                                            ██████… rep 3
```

State per voice:

| field | meaning |
|---|---|
| `src`, `dst` | generation buffer indices (`dst == src + 1`) |
| `pOut` | read position in `src`, advances by `r` — the audible tap |
| `w` | write position in `dst`, advances by 1 — the recycle tap |
| `eq` | own 7-band filter state |
| `env` | fade state (see below) |

Per sample, for each active voice:

```
v      = interp(src, pOut);  pOut += r          // this repetition, audible
rec    = (fbType == Raw ? v * env               // recycled = the varispeed signal
                        : src[w])               // recycled = unity copy, no resampling
dst[w] = softclip(eq(rec * feedback));  ++w
wetSum += v * env
```

and once per sample for the block:

```
G[newest][n] += x[n]                            // n = period counter, 0 ≤ n < T
out[n]        = x[n] * dryGain + wetSum * wetGain
```

At the boundary (`n == T`): open `G[k]`, spawn voice `k` (`src = k-1`, `dst = k`),
`n = 0`, latch new `T` / feedback type. Voices retire on their own schedule.

Why this works:

- Input enters the newest generation at rate 1 and is first heard through the next
  voice's rate-`r` tap → **repetition 1 is already varispeeded**, in both feedback modes.
- **Raw**: the recycled signal is the varispeed tap, so generation N is the previous one
  resampled again — pitch `r^N`, duration `T/r^N`, gain `fb^N`, EQ applied N times.
- **Stable**: the recycled signal is a unity-rate copy, so buffer content never
  accumulates resampling. Every repetition is the same audio at speed `r`, same duration
  `T/r`, only gain and EQ accumulate. Switching modes mid-flight is seamless — one
  branch differs, and repetition 1 sounds the same either way.
- Repetition spacing stays `T` regardless of speed, so the delay stays on the grid.
  `r > 1` → repeats are shorter than the period (gaps between blips, no overlap).
  `r < 1` → repeats are longer than the period and **overlap** the following ones.

### Chasing read

At `r < 1`, voice `k+1` starts reading `G[k]` at the boundary while voice `k` is still
writing it. This is safe: the reader advances at `r`, the writer at 1, and the reader
starts one full period behind, so the gap only grows (`gap(t) = (1-r)(t - kT) + rT`).
At `r > 1` the source is always complete before it is needed, because the writing voice
finished in `T/r < T`.

Guard for the one unsafe case — a large upward speed jump while a voice is mid-flight:
if `pOut` reaches the source's written length while the source is still open, the voice
**underruns**: fade out over `kXfadeMs` and retire it. Never read unwritten samples.

### Lengths and caps

`L[k]` = used length of `G[k]` = `max(T, samples written by voice k)`.
Voice duration `D[k] = L[k-1] / r`, clamped to `Dmax = min(4 * T, kMaxRepSeconds)`.

- The clamp factor 4 matches the minimum speed (0.25×), so the **first** repetition is
  never truncated at any speed. Only later generations at extreme slow settings hit it
  (0.25×: gen 1 = 4T natural, gen 2 would be 16T → clamped to 4T, i.e. each generation
  zooms further into the head of the previous one).
- Truncation at `Dmax` fades out over `kXfadeMs`; it is not a click and not a boundary
  cut.
- Buffer `G[k]` is live from `kT` until voice `k+1` finishes reading it, at most
  `(k+5)T` → a ring of `kNumGenBuffers = 6` covers it with a spare.
- Concurrent voices ≤ `Dmax / T + 1` = 5 → `kMaxVoices = 5`. If a `T` change would
  exceed the pool, steal the oldest voice with a fast fade.
- `fb > 1` grows without bound by design (tape runaway), and overlapping repeats sum on
  top of that. A `tanh` soft clip per voice keeps the recycle path bounded and musical.

### Memory

`kNumGenBuffers (6) × kMaxRepSeconds (20 s) × 2 ch × 4 B` — 46 MB at 48 kHz, 92 MB at
96 kHz, allocated once in `prepareToPlay`. `kMaxRepSeconds` must be ≥ the maximum delay
time, so it is pinned to 20 s. (For comparison SiLooper allocates 8 × 60 s.) If this
proves too heavy, drop the overlap factor to 3× or the max free-mode delay to 10 s.

## Click protection

`kXfadeMs = 8` (constant; expose later if useful). Raised-cosine envelope per voice:

- fade **in** over the first `xfade` samples of the voice
- fade **out** over the last `xfade` samples before the voice's end, whichever comes
  first: source content end (`pOut → L[src]`), the `Dmax` clamp, an underrun, or a steal
- **skip entirely when `|r - 1| < 1e-4`** — at unity the next generation is contiguous
  with the current one and any fade would add periodic amplitude ripple

The envelope is applied to the wet tap always, and to the recycle tap **only in Raw
mode**, where the recycled signal is the resampled one and its edges must not click on
the next pass. The Stable recycle tap is a unity copy and needs no fade.

Optional refinement if the joins are still audible: overlap the retiring voice with the
new one by `xfade` ms (equal-power) instead of a plain fade — the voice pool already
supports two voices sounding at once.

## Parameters (APVTS)

| ID | Type | Range / choices | Notes |
|---|---|---|---|
| `time_ms` | Float | 10 – 20000 ms, skew ~0.3 | free mode |
| `time_sync` | Bool | off / on | |
| `time_div` | Choice | 1/32 … 1/4 … 1/1 … 2 bars (straight/dotted/triplet) | sync mode |
| `speed` | Float | 0.25 – 4.0, log2-symmetric skew (centre 1.0) | |
| `feedback` | Float | 0 – 2 | >1 = runaway |
| `fb_type` | Choice | Raw / Stable | |
| `eq_on` | Bool | | |
| `eq_b1` … `eq_b7` | Float | −12 … +12 dB | 63, 160, 400, 1k, 2.5k, 6.3k, 16k |
| `dry` | Float | 0 – 1 (gain) | |
| `wet` | Float | 0 – 1 (gain) | |

Speed preset buttons (1/4, 1/2, 1, 2, 4) write `speed` via `setValueNotifyingHost`, so
they are just shortcuts — no extra parameter.

Smoothing (`juce::SmoothedValue`, ~20 ms): speed (gives tape-style pitch glide),
feedback, dry, wet. `time` and `fb_type` latch at period boundaries only. A voice keeps
using the smoothed global rate, so a speed move bends all sounding repetitions together.

## Sync mode

`AudioPlayHead::PositionInfo` → bpm + time signature. `T = samplesPerBeat * beatsForDiv`,
recomputed per block, latched at the next boundary. No host transport (standalone) →
fall back to an internal BPM field like SiLooper's, or to free mode. Tempo changes take
effect at the next boundary, so no mid-period buffer resize is ever needed; voices
already in flight finish under their old cap.

## Graphic EQ

7 fixed ISO bands, ±12 dB each, RBJ peaking biquads, Q ≈ 1.4, TDF-II, stereo (two filter
states per band), one instance **per voice** (≤ 5 × 7 × 2 = 70 biquads worst case —
negligible). Hand-rolled POD biquad struct — **not** `juce::dsp::IIR::Coefficients`,
which is reference-counted and allocates on coefficient updates (forbidden on the audio
thread). Coefficients are computed once per block into a shared POD set (dirty flag per
band) and copied by value into each voice; only the filter *state* is per voice.

Placement: recycle path, after feedback gain, before the soft clip. Applied once per
repetition → the EQ curve accumulates across repeats, which is the point.

`eq_on == false` bypasses the chain; filter state resets when a voice is spawned.

## Files to add

```
src/DelayEngine.{h,cpp}    generation buffers, voice pool, period logic, fades, clip
src/Voice.h                per-voice state (header-only struct + inline tick)
src/GraphicEQ.{h,cpp}      7 × stereo biquad, coefficient math
src/LookAndFeel.{h,cpp}    dark knob/slider styling
src/RepetitionView.{h,cpp} optional: repeats on a timeline, showing overlap
```

`PluginProcessor` owns `DelayEngine`, builds the APVTS layout, caches raw parameter
pointers, reads the playhead, and pushes values into the engine once per block.

## UI

800 × 500, dark, SiLooper-style flat panels with accent lines.

```
┌─────────────────────────────────────────────────────────────┐
│  VARISPEED DELAY                                            │
├─────────────────────────────────────────────────────────────┤
│  ( TIME )   [FREE|SYNC] [div ▾]     ( SPEED )   ( FEEDBACK ) │
│   1.20 s                             1.00x       0.65        │
│                          [¼][½][1][2][4]   [RAW|STABLE]      │
├─────────────────────────────────────────────────────────────┤
│  EQ [ON]   ▮ ▮ ▮ ▮ ▮ ▮ ▮      (7 vertical sliders, ±12 dB)   │
│            63 160 400 1k 2k5 6k3 16k                        │
├─────────────────────────────────────────────────────────────┤
│  ( DRY )   ( WET )                             build date   │
└─────────────────────────────────────────────────────────────┘
```

Time knob shows ms/s in free mode and the division name in sync mode. Speed preset
buttons highlight when `speed` is within 1 cent of the preset. The optional repetition
view is the one place the overlap becomes legible — stacked bars, one per live voice,
length `D[k]`, spaced `T` apart, on a 30 Hz timer.

## Thread safety / RT rules

- All buffers allocated in `prepareToPlay`; the voice pool is fixed size, no allocation
  on spawn.
- No allocation, locks or file I/O in `processBlock`; EQ coefficients written in place.
- UI → audio communication only through APVTS raw pointers and `std::atomic` (relaxed).
- `setLatencySamples(0)` — the engine adds none.

## Phases

1. **Parameters** — full APVTS layout, generic UI via attachments, host automation and
   state save/restore verified. *Done when:* every parameter shows up and persists.
2. **Engine core** — generation ring, voice pool, period counter, varispeed tap,
   raw/stable recycle, dry/wet. Fixed free-mode time, no EQ, no fades. *Done when:* at 1×
   it is a clean digital delay; at 2× repeats pitch up cumulatively (raw) or stay put
   (stable); at 0.5× repeats overlap instead of cutting off.
3. **Caps + click protection + smoothing** — `Dmax` clamp, underrun guard, voice
   stealing, fades, unity-rate bypass, smoothed speed/feedback, `tanh` soft clip.
   *Done when:* no clicks at any speed, no zipper on knob sweeps, fast 0.25→4 sweeps
   never read unwritten samples, `fb = 2` saturates instead of exploding.
4. **Sync** — playhead, divisions up to 2 bars, boundary-latched `T`, standalone fallback
   BPM. *Done when:* repeats stay locked through tempo changes.
5. **EQ** — 7-band biquads, per-voice state, bypass, accumulation across repeats.
6. **UI** — LookAndFeel, layout, speed presets, readouts; optional repetition view.
7. **Validation** — `auval -v aumf Vspd VSil`, pluginval strictness 8, Logic/Reaper smoke
   test, sample rates 44.1/48/96, block sizes 32/512/2048, mono and stereo. Write
   README.md / USAGE.md / CLAUDE.md.

## Tests

Add a `VarispeedDelayTests` console target linking a small `DelayEngine`-only unit set
(the engine must not depend on `AudioProcessor`):

- unity speed, zero feedback → output equals input delayed by exactly `T`
- raw, `r = 2`, impulse in → impulses at `T`, `2T`, `3T`, lengths `T/2`, `T/4`, `T/8`
- raw, `r = 0.5`, impulse in → repetition k starts at `kT` and lasts `2^k · T` until the
  `Dmax` clamp; repetitions 1 and 2 are simultaneously non-zero (**overlap**)
- stable → every repetition identical apart from `fb^N` gain, duration `T/r` for all
- chasing read: `r = 0.25` for 60 s, assert no voice ever reads past its source's
  written length; speed jump 0.25 → 4 mid-repetition retires voices instead of reading
  stale samples
- voice count never exceeds `kMaxVoices`; buffer indices always in range
- `fb = 2` stays finite over 60 s of noise; no NaN/denormals after speed sweeps
- EQ: flat with all bands at 0 dB; +12 dB band raises that bin by ~12 dB per repetition

## Open questions

1. **Spacing at `r != 1`** — spacing is fixed at `T` (grid). The alternative (next
   repetition starts when the previous one ends, `T/r^N`) is real tape-runaway behavior
   and needs no overlap machinery at all. Worth a later `SPACING [GRID|TAPE]` switch.
2. **Overlap gain** — 4 overlapping repeats at `fb = 1` sum to ~+12 dB. Soft clip catches
   it, but consider scaling the wet tap by `1/sqrt(activeVoices)` or documenting the wet
   knob as the trim.
3. **Anti-aliasing at high `r`** — reading at 4× aliases. Cheap fix: one-pole LP at
   `sr/(2r)` on the recycle path when `r > 1`. Decide after listening.
4. **Interpolation** — start with linear; move to Catmull-Rom if 4× sounds gritty.
5. **Stereo** — one shared read pointer per voice for both channels (no width effect). A
   per-channel speed offset would be a nice later addition.
