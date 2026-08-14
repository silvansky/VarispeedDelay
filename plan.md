# Varispeed Delay — implementation plan

Delay where every repetition is re-played at a different tape speed: pitch and duration
change together. Two feedback topologies (cumulative / one-shot), a 7-band graphic EQ in
the recycle path, click protection via short fades at each repetition boundary.

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

A **period** = the delay time `T` (samples). The engine keeps two loop buffers of
`maxDelaySamples` and ping-pongs them once per period.

- `A` — content circulating now. Played back at rate `r` (the speed knob) by one voice.
- `B` — being filled during this period at rate 1. Becomes `A` at the next boundary.

Per sample `n` in `[0, T)`:

```
v      = interp(A, p);  p += r          // varispeed tap  (voice output)
u      = A[n]                           // unity tap      (stable recycle source)
env    = fade(p, n)                     // click protection, see below
wet    = v * env
src    = (fbType == Raw ? wet : u * env)
B[n]   = softclip(eq(src * feedback)) + x[n]
out[n] = x[n] * dryGain + wet * wetGain
```

At the boundary (`n == T`): `swap(A, B)`, clear `B`, `p = 0`, `n = 0`, latch new `T`/`r`.

Why this works:

- Input `x` enters `B` at rate 1 and is heard next period through the rate-`r` tap →
  **repetition 1 is already varispeeded**, in both feedback modes.
- Raw: the recycled signal is the varispeed tap, so repetition N is at `r^N`
  (cumulative pitch, duration `T/r^N`), gain `fb^N`, EQ applied N times.
- Stable: the recycled signal is the unity tap, so buffer content never accumulates
  resampling — every repetition is the same audio at speed `r`, only gain and EQ
  accumulate. Switching modes mid-flight is seamless (one line differs).
- Repetition spacing stays `T` regardless of `r`, so the delay stays in time with the
  track. `r > 1` → repeats are shorter than the period (gaps between blips).
  `r < 1` → repeats are longer than the period and are cut at the boundary.
- Memory is O(T), not O(T × repeats): all concurrent echo chains share one buffer,
  because `resample(a + b) == resample(a) + resample(b)`.

Consequences to accept (document in USAGE):

- At `r > 1` the content of each period is compressed toward the period start, so an
  event at offset `n` returns after `T - n(1 - 1/r)`, not exactly `T`. Inherent to
  chunked varispeed; the grid stays locked to `T`.
- At `r < 1` material past `T` in the buffer is never audible (slowing only pushes it
  later), so buffers can stay exactly `T` long with no perceptual loss.
- `fb > 1` grows without bound by design (tape runaway). A `tanh` soft clip in the
  recycle path keeps it bounded and musical.

## Click protection

`kXfadeMs = 8` (constant; expose later if useful). Raised-cosine envelope:

- fade **in** over the first `xfade` samples of the voice (`p < xfade`)
- fade **out** over the last `xfade` samples before whichever end comes first:
  content end (`p >= T - xfade * r`, hit when `r > 1`) or period boundary
  (`n >= T - xfade`, hit when `r < 1`)
- skip entirely when `|r - 1| < 1e-4` — at unity the new buffer is contiguous with the
  old one and any fade would add periodic amplitude ripple

The envelope is applied to the recycled signal too, so fades bake into the loop and no
discontinuity ever accumulates. Optional refinement if boundary gaps are audible at
`r < 1`: keep the retiring voice alive for `xfade` ms into the new period for a true
overlapping crossfade (2 voices, equal-power).

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
feedback, dry, wet. `time` and `fb_type` latch at period boundaries only.

## Sync mode

`AudioPlayHead::PositionInfo` → bpm + time signature. `T = samplesPerBeat * beatsForDiv`,
recomputed per block, latched at the next boundary. No host transport (standalone) →
fall back to an internal BPM field like SiLooper's, or to free mode. Tempo changes take
effect at the next boundary, so no mid-period buffer resize is ever needed.

`T` changes: buffers are pre-allocated at `maxDelaySamples`; only the used length changes.
If the new `T` is shorter than the current position, wrap immediately with a fade-out.

## Graphic EQ

7 fixed ISO bands, ±12 dB each, RBJ peaking biquads, Q ≈ 1.4, TDF-II, stereo (two filter
states per band). Hand-rolled POD biquad struct — **not** `juce::dsp::IIR::Coefficients`,
which is reference-counted and allocates on coefficient updates (forbidden on the audio
thread). Coefficients recomputed only when a band value changes (dirty flag per band,
compared against the cached APVTS value each block).

Placement: recycle path, after feedback gain, before the soft clip. Applied once per
repetition → the EQ curve accumulates across repeats, which is the point.

`eq_on == false` bypasses the whole chain (and resets filter state on re-enable).

## Files to add

```
src/DelayEngine.{h,cpp}    buffers, period logic, voice, fades, soft clip   (core)
src/GraphicEQ.{h,cpp}      7 × stereo biquad, coefficient math
src/LookAndFeel.{h,cpp}    dark knob/slider styling
src/RepetitionView.{h,cpp} optional: visualizes repeats shrinking/growing on a timeline
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

Time knob shows ms/s in free mode and the division name in sync mode. Speed preset buttons
highlight when `speed` is within 1 cent of the preset. 30 Hz timer only if the repetition
view is built; otherwise attachments alone suffice.

## Thread safety / RT rules

- All buffers allocated in `prepareToPlay` (`2 × 20 s × 2 ch × 4 B` ≈ 15 MB @ 48 kHz,
  31 MB @ 96 kHz — sized from `maxDelaySeconds * sampleRate`).
- No allocation, locks or file I/O in `processBlock`; EQ coefficients written in place.
- UI → audio communication only through APVTS raw pointers and `std::atomic` (relaxed).
- `setLatencySamples(0)` — the engine adds none.

## Phases

1. **Parameters** — full APVTS layout, generic UI via attachments, host automation and
   state save/restore verified. *Done when:* every parameter shows up and persists.
2. **Engine core** — ping-pong buffers, period counter, varispeed voice, raw/stable
   recycle, dry/wet. Fixed free-mode time, no EQ, no fades. *Done when:* at 1× it is a
   clean digital delay; at 2×/0.5× repeats pitch up/down cumulatively in raw and stay put
   in stable.
3. **Click protection + smoothing** — fades, unity-rate bypass, smoothed speed/feedback,
   `tanh` soft clip. *Done when:* no clicks at any speed, no zipper on knob sweeps, `fb=2`
   saturates instead of exploding.
4. **Sync** — playhead, divisions up to 2 bars, boundary-latched `T`, standalone fallback
   BPM. *Done when:* repeats stay locked through tempo changes.
5. **EQ** — 7-band biquads, bypass, accumulation across repeats.
6. **UI** — LookAndFeel, layout, speed presets, readouts; optional repetition view.
7. **Validation** — `auval -v aumf Vspd VSil`, pluginval strictness 8, Logic/Reaper smoke
   test, sample rates 44.1/48/96, block sizes 32/512/2048, mono and stereo. Write
   README.md / USAGE.md / CLAUDE.md.

## Tests

Add a `VarispeedDelayTests` console target linking a small `DelayEngine`-only unit set
(the engine must not depend on `AudioProcessor`):

- unity speed, zero feedback → output equals input delayed by exactly `T`
- raw mode, `r = 2`, impulse in → impulses at `T`, `2T`, `3T` with lengths `T/2`, `T/4`…
- stable mode → all repetitions bit-identical apart from `fb^N` gain
- `fb = 2` stays finite (soft clip) over 60 s of noise
- no NaN/denormal after speed sweeps; buffer indices never out of range
- EQ: flat with all bands at 0 dB; +12 dB band raises that bin by ~12 dB per repetition

## Open questions

1. **Spacing at `r != 1`** — plan fixes spacing at `T`. The alternative (spacing follows
   the repetition length, `T/r^N`) gives accelerating/decelerating tape runaway. Worth a
   later `SPACING [GRID|TAPE]` switch if the fixed grid feels tame.
2. **Anti-aliasing at high `r`** — reading at 4× aliases. Cheap fix: one-pole LP at
   `sr/(2r)` on the recycle path when `r > 1`. Decide after listening.
3. **Interpolation** — start with linear; move to Catmull-Rom if 4× sounds gritty.
4. **Stereo** — single shared read pointer for both channels (no width effect). A
   per-channel speed offset would be a nice later addition.
