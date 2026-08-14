# Varispeed Delay — implementation plan

Delay where every repetition is re-played at a different tape speed: pitch and duration
change together. Two feedback topologies (cumulative / one-shot), a 7-band graphic EQ in
the repetition path, click protection via short fades at each repetition boundary.
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
| `eqOut`, `eqRec` | own 7-band filter state, one per tap (`eqRec` unused in Raw) |
| `env` | fade state (see below) |

Per sample, for each active voice:

```
v      = eqOut(interp(src, pOut));  pOut += r   // EQ'd once, this repetition, audible
rec    = (fbType == Raw ? v * env               // recycled = the varispeed signal
                        : eqRec(src[w]))        // recycled = unity copy, EQ'd, not resampled
dst[w] = softclip(rec * feedback);  ++w         // assignment — see below
wetSum += v * env
```

and once per sample for the block, on the newest generation only:

```
G[k][n] += x[n]                                 // n = period counter, 0 ≤ n < T
out[n]   = x[n] * dryGain + wetSum * wetGain
```

At the boundary: open `G[k]`, spawn voice `k` (`src = k-1`, `dst = k`), `n = 0`, latch
the new feedback type. Voices retire on their own schedule. The very first period has no
source, so it only records input; the first voice spawns at the second boundary.

**Generations are never memset.** Each generation buffer has exactly one writing voice,
and its `w` advances in lockstep with the period counter `n`, so `dst[w] = …` is an
assignment and the input `+=` lands on a slot the voice just wrote (or on `= x[n]` when
the voice is inactive). Readers are bounded by `writtenLen[k]`, so stale samples past
the write head are never audible. Clearing instead would mean a 3.8 MB memset on the
audio thread at every boundary — at `T = 10 ms` that is a memset every 10 ms.

Timing note to document in USAGE: an input event at offset `n` inside a period returns
after `T + n(1/r - 1)`, not exactly `T`. The *grid* is `T`; within a repetition the
material is compressed toward the boundary at `r > 1` and stretched away from it at
`r < 1`. Inherent to chunked varispeed.

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

Guard for the one unsafe case — an upward speed move (or a BEND shorten) while a voice is
mid-flight: `pOut` is clamped to `writtenLen(src) - 1` whenever the source is still open.
Never read unwritten samples. The clamp is self-resolving, not fatal — see
*Speed changes*.

### Lengths and caps

`L[k]` = used length of `G[k]` = `max(T, samples written by voice k)`.
Voice duration `D[k] = L[k-1] / r`, clamped to `Dmax = min(4 * T, kMaxRepSeconds)`.

- The clamp factor 4 matches the minimum speed (0.25×), so the **first** repetition is
  never truncated at any speed. Only later generations at extreme slow settings hit it
  (0.25×: gen 1 = 4T natural, gen 2 would be 16T → clamped to 4T, i.e. each generation
  zooms further into the head of the previous one).
- Truncation at `Dmax` fades out over the voice's `xfade`; it is not a click and not a boundary
  cut.
- Buffer `G[k]` is live from `kT` until voice `k+1` finishes reading it, at most
  `(k+5)T` → a ring of `kNumGenBuffers = 6` covers it with a spare. That margin holds
  only while `T` is constant — see *Delay time changes*.
- Concurrent voices ≤ `Dmax / T + 1` = 5 → `kMaxVoices = 5`. If the pool is full, steal
  the oldest voice with a fast fade.
- `fb > 1` grows without bound by design (tape runaway), and overlapping repeats sum on
  top of that. The soft clip below keeps the recycle path bounded and musical.

### Soft clip

A bare `tanh` is not an option: it already compresses ~2.4 dB at full scale, so it would
colour every setting rather than only runaway ones. The clipper is **linear below a
threshold** and tanh-shaped above it, joined so that both value and slope are continuous:

```
t = kClipThreshold (0.5, −6 dBFS), c = kClipCeiling (1.0)
|x| <= t :  y = x                                             // exactly transparent
|x| >  t :  y = sign(x) * (t + (c - t) * tanh((|x| - t) / (c - t)))
```

At `|x| = t` the value is `t` and the derivative is exactly 1, so there is no kink to
generate harmonics; as `|x|` grows the output asymptotes to `c`. A 0 dBFS peak comes out
at 0.88 (−1.1 dB); anything under −6 dBFS is untouched sample for sample.

`clip_on` (Bool, default **on**) bypasses it. With the clipper off and `fb > 1` the loop
really does diverge, so a **hard safety clamp at ±8 (+18 dB) stays in the path
unconditionally** — inaudible unless you are already deep into runaway, and it keeps inf
and NaN out of the host's mix bus.

Threshold and ceiling are constants. Exposing the threshold as a float parameter is a
one-line addition if it turns out to want tuning per-patch; the switch is enough for now.

Placement is the recycle path only, after the feedback gain — its job is bounding the
loop, not limiting the output. The wet sum of up to 5 overlapping voices is a separate
issue (open question 2).

### Memory

`kNumGenBuffers (6) × kMaxRepSeconds (20 s) × 2 ch × 4 B` — 46 MB at 48 kHz, 92 MB at
96 kHz, allocated once in `prepareToPlay`. `kMaxRepSeconds` must be ≥ the maximum delay
time, so it is pinned to 20 s. (For comparison SiLooper allocates 8 × 60 s.) If this
proves too heavy, drop the overlap factor to 3× or the max free-mode delay to 10 s.

### Tail length

`getTailLengthSeconds()` returns `10 * Dmax` — ten generations of the longest repetition
the current settings allow. Since `Dmax = min(4T, kMaxRepSeconds) >= T`, that covers at
least ten repetition slots plus the duration of the last one, so it is a conservative
bound on the audible tail: 20 s at `T = 500 ms`, 200 s at `T = 20 s`.

Held in an atomic, recomputed whenever `T` changes. Note that hosts differ on when they
re-query — AU has a tail-time property change notification, VST3 asks on demand, and some
hosts cache the value from load — so treat it as a hint, not a contract.

It is deliberately a pragmatic bound rather than a decay estimate. With `fb >= 1` the true
tail is infinite, and even at `fb = 0.9` ten generations only reaches −9 dB, so a host
that honours this number will truncate a long feedback wash on an offline bounce. Printing
the tail through the wet path is the workaround.

## Delay time changes

`T` is read every block. Every source of change — free knob, sync division, host tempo
automation — goes through the same path, and the `time_mode` switch picks the character:
**REGRID** (default) snaps the grid and leaves the tail alone, **BEND** slews the time and
doppler-bends everything that is sounding, like a tape delay.

Both modes share these rules:

1. **The period ends at the current effective `T`.** If `n >= T_eff` the boundary fires
   immediately; otherwise the period runs to the new, longer `T_eff`. In sync mode the
   boundary comes from the PPQ grid instead (see *Sync mode*), which lands at the same
   place once locked and produces one irregular period while locking on. Response is bounded
   by one *new* period, so shortening a 20 s delay to 200 ms reacts in 200 ms — never
   latch to the old boundary. `L[k]` is `max(samples of input actually written, voice
   write length)`, so a short period just yields a short generation.
2. **Buffer-slot stealing is what makes a big shrink safe.** Opening generation `k`
   recycles slot `k mod kNumGenBuffers`; the `6T` lifetime margin only holds while `T` is
   constant. After a large shrink, a voice spawned under the old `T` can still be reading
   a slot that is due for reuse. Rule: opening a generation retires — with an
   `xfade` fade — every voice that reads or writes the slot being recycled. Voices
   never outlive their source; this subsumes the "pool full" steal.
3. **Convergence after a shrink.** At effective rate `>= 1` the recycle preserves content
   length, so circulating material longer than the new `Dmax = min(4·T_new,
   kMaxRepSeconds)` is truncated (with a fade) over the next generations; until it decays
   away, the old long repeats overlap the new short ones. At rate `< 1` the same clamp
   applies one generation later.
4. **Lengthening** leaves a gap: the last repetition finishes before the longer period is
   up, and the wet bus is silent until the next one starts.
5. **Fades cannot be skipped across a change.** The unity bypass assumes the next
   generation is contiguous with the current one, which stops being true the moment `T`
   moves. Voices spawned in the two periods following a `T` change carry a `forceFade`
   flag, released two generations after the last real change.
6. **What counts as a change — the deadband.** A host hands over an automated `time_ms`
   every block, and a flat lane can still arrive with float jitter (500.0 ms, then
   500.0000001 ms). Testing `T_new != T_previous_block` is therefore true almost always,
   `forceFade` never releases, and the bypass never re-engages. The symptom is narrow but
   real: **at `speed = 1` with time automation running, a clean digital delay grows an
   amplitude dip at every repetition** — tremolo at the delay rate. At any other speed the
   fades are on regardless (`r != 1` defeats the bypass), and in BEND `m != 1` during a
   ramp defeats it too, so this is a REGRID-at-unity problem only.

   Rule: compare against the **last latched** `T`, never the previous block's, so a slow
   ramp still accumulates into a detected change instead of hiding under the threshold
   forever. Treat `|ΔT| < max(0.5 %, 1 ms)` as no change at all — no re-latch, no
   `forceFade`. Anything larger is a real change and takes the path above.

### REGRID

`T_eff = T_target` immediately, bend factor `m = 1`.

Voices in flight are not retimed: they keep their rate, pitch and length and play out
under the cap they were spawned with. No doppler, no discontinuity. The old tail keeps
ringing on the old grid and decays under feedback while new repetitions arrive on the new
grid — the delay cross-fades between grids instead of dragging.

### BEND

`T_eff` slews toward `T_target` under a rate limit, and every live voice's read rate is
multiplied by the resulting bend factor:

```
dT = clamp(T_target - T_eff, -kBendDown, +kBendUp) per sample   // kBendDown 3, kBendUp 0.75
T_eff += dT
m      = 1 - dT                                                 // m ∈ [0.25, 4]
pOut  += r * m                                                  // all voices, all generations
```

This is the delay-line identity `y(t) = x(t - D(t))`, whose read pointer advances at
`1 - D'(t)`: shortening the time speeds the tail up and pitches it, lengthening slows and
drops it, and when the knob stops the factor returns to 1 and everything resumes at its
own speed in the new position. The bend is baked into the recycle path as well — the
write pointer keeps advancing at 1 while `pOut` moves at `r·m` — so the next generation
inherits the bend, exactly as a tape delay records its own wobble.

The rate limits are what make an instant knob jump musical rather than a click: they cap
the bend at the plugin's own speed range (`m ∈ [0.25, 4]`), so a 20 s → 200 ms jump glides
over ≈ 6.6 s. Constants tunable in phase 3; asymmetry (down faster than up) mirrors
`m = 1 - dT`.

Interactions:

- The unity-fade bypass tests the **effective** rate `r · m`, so fades engage
  automatically during a bend and switch off again when it settles.
- A fast shorten drives `m > 1`, which can make a chasing voice overtake a source that is
  still being written → the `pOut` clamp from *Speed changes* holds it at the writer's
  edge until the writer closes the buffer. No voice is lost to a time move.
- In sync mode BEND glides into a new division instead of locking to it instantly; that
  is the point of the mode, and REGRID remains the default for grid-accurate work.
- Tempo ramps in sync mode produce a continuous, correct tape-slide for free.

## Speed changes

The speed knob is the tape motor, so unlike time it has no two modes: **`r` is global and
every sounding repetition follows it**. Turn it while the delay rings and the whole tail
pitch-bends together, all generations at once.

- `r` is smoothed per **sample** (`kSpeedGlideMs = 20`), computed once in the engine loop
  and handed to every voice, so voices never diverge. Longer glide times read as motor
  inertia — worth trying 60–80 ms in phase 3. Preset buttons go through the same
  smoother, so `1 → 4` is a fast chirp rather than a step.
- **The grid does not move.** Spacing stays `T`; only the pitch and the length of each
  repetition change. Raising `r` mid-repetition makes the current repeat finish early and
  opens a gap; lowering it stretches the repeat into the following ones. This is the main
  perceptual difference from a time change, which moves the grid and leaves pitch alone.
- **Raw mode records the sweep.** The bend is baked into the recycled signal, so later
  generations replay the swept material and sweep it again — a knob move leaves a
  permanent, compounding trace in the loop. In Stable mode the recycle tap is a unity copy
  and never accumulates: the sweep is heard on every repetition but leaves no trace.
- **Upward moves collide with the chasing read.** At `r < 1` voice `k+1` reads `G[k]`
  while voice `k` writes it; pushing `r` up makes the reader overtake the writer. Rather
  than kill the repetition, `pOut` clamps to the writer's edge: the voice rides the newest
  written sample at the writer's pace — no click (samples stay contiguous), just a
  temporary lock to unity rate. It resolves itself quickly, because the same speed
  increase makes the writing voice finish its own source sooner and close the buffer,
  after which the reader runs free. Only buffer-slot reuse ever retires a voice.
- **Envelopes must be positional, not counters.** With `r` moving, a latched "fade out in
  N samples" counter is wrong the moment the rate changes. Fades are computed per sample
  from the current state — see *Click protection*.
- **`Dmax` is measured in output samples** (elapsed voice duration), so it is unaffected
  by rate changes; only the content-end distance `(L[src] - pOut) / r` moves.
- After the rate settles at exactly 1.0, generation joins are still not contiguous (the
  content was recorded under a moving rate), so the unity-fade bypass stays suspended for
  two generations, same `forceFade` rule as a time change.

## Click protection

The fade length is **proportional to what is being faded**, not a fixed millisecond value,
so it stays sane from a 20 s wash down to a 10 ms flutter. Latched once per voice at spawn
(so a mid-flight rate change cannot make the envelope jump):

```
D_est  = L[src] / r                                   // this repetition's duration
xfade  = clamp(kXfadePct * min(T, D_est), kXfadeMinMs, kXfadeMaxMs)
xfade  = min(xfade, D_est / 2)                        // must fit twice, in and out
// kXfadePct 5 %, kXfadeMinMs 0.25 ms, kXfadeMaxMs 8 ms
```

The ceiling binds for anything longer than ~160 ms, so normal delays keep the 8 ms fade
they would have had. Below that it scales: `T = 10 ms` gives 0.5 ms, a real ramp instead
of a fade that swallows the whole period. `D_est` in the `min` handles the other end —
at `r = 4` the repetition is `T/4` long, and the fade tracks the repetition rather than
the period. The `D_est / 2` clamp guarantees the envelope still reaches unity; if a later
rate change invalidates it, the envelope degrades to a rounded blip that never quite hits
1, which is graceful rather than broken.

The minimum delay time stays at 10 ms. With the fade scaled it is mechanically fine, and
a 10 ms period under varispeed is a flutter/comb effect — odd, but a legitimate one.

Raised-cosine envelope per voice, evaluated per sample as a pure function of the voice's
current state — never a latched countdown, which would be wrong as soon as the rate moved:

```
inSamples  = elapsed                                  // output samples since spawn
toContent  = (L[src] - pOut) / max(rEff, tiny)        // output samples left in the source
toCap      = Dmax - elapsed                           // output samples left under the cap
env        = raisedCos(min(inSamples, toContent, toCap) / xfade)   // clamped to [0,1]
```

- fade **in** over the first `xfade` samples of the voice
- fade **out** over the last `xfade` samples before whichever end arrives first — source
  content end, the `Dmax` clamp, or a slot steal (which forces `env` down over `xfade`)
- because it is positional, a fade-out that started can un-fade if the rate drops and the
  end recedes; that is correct, not a glitch
- **skip entirely when `|rEff - 1| < 1e-4` and no `forceFade` flag is set** — at unity the
  next generation is contiguous with the current one and any fade would add periodic
  amplitude ripple

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
| `time_mode` | Choice | Regrid / Bend | how the tail reacts to a time change |
| `speed` | Float | 0.25 – 4.0, log2-symmetric skew (centre 1.0) | |
| `feedback` | Float | 0 – 2 | >1 = runaway |
| `fb_type` | Choice | Raw / Stable | |
| `clip_on` | Bool | default on | soft clip in the recycle path |
| `eq_on` | Bool | | |
| `eq_b1` … `eq_b7` | Float | −12 … +12 dB | 63, 160, 400, 1k, 2.5k, 6.3k, 16k |
| `dry` | Float | 0 – 1 (gain) | |
| `wet` | Float | 0 – 1 (gain) | |

Speed preset buttons (1/4, 1/2, 1, 2, 4) write `speed` via `setValueNotifyingHost`, so
they are just shortcuts — no extra parameter.

Smoothing (`juce::SmoothedValue`): speed per sample (`kSpeedGlideMs = 20`, see *Speed
changes*), feedback / dry / wet per block are fine at ~20 ms. `fb_type` latches at period
boundaries; `time` is handled by the `time_mode` path above, not by a generic smoother.

## Sync mode

`AudioPlayHead::PositionInfo` → bpm, time signature, `ppqPosition`, `isPlaying`.
`T = samplesPerBeat * beatsForDiv`, recomputed per block. A division change or a tempo
ramp is just a `T` change and takes the path in *Delay time changes*; buffers are never
resized (they are allocated at `kMaxRepSeconds`), only the period length moves.

### Boundary phase — anchored to the host grid

Periods of the right *length* are not the same as periods on the host's grid, and in this
engine the difference is audible: a repetition begins at its period boundary, and material
is compressed toward that boundary at `r > 1`. Boundaries at an arbitrary phase cut notes
mid-sustain, and each half then repeats a period apart. Boundaries on the musical grid
land where notes tend to *begin*, so a note stays whole inside one generation.

So in sync mode the boundary is derived from the transport, not from a sample counter: a
boundary fires when `floor(ppq / divPPQ)` increments, where `divPPQ` is the division in
quarter-notes. Each block spans `[ppqStart, ppqEnd)`; any crossing inside it is converted
to a sample offset and the block is split there — the same technique as SiLooper's
`checkBarBoundary()`. Free mode keeps the plain sample counter.

- **Anchor at ppq 0, not at the bar.** Divisions that tile the bar align with bar lines
  anyway; dotted and triplet divisions stay regular instead of getting a stunted period
  at every bar line.
- **Locking on** (transport start, division change, sync toggled on) ends the current
  period at the next grid crossing, which makes one short or long generation. This is the
  existing "period ends at the current `T_eff`" rule and the existing `forceFade` rule —
  no new machinery.
- **Locate, rewind, loop jump**: `ppq` moves discontinuously. Detect a jump (`|ppqStart −
  expected|` beyond a tolerance) and force a boundary immediately; voices in flight are
  untouched and ring out over the seam.
- **Transport stopped**: fall back to free-running at the last known tempo and re-anchor
  on the next start. Standalone with no host transport uses an internal BPM field like
  SiLooper's.
- **Tempo ramps** move the boundaries with the tempo map for free, and the resulting
  period-length wobble feeds BEND's `m = 1 - dT` as a correct tape slide.

Payoff beyond "it lines up": the result is repeatable. Playback from any bar, a loop, and
an offline bounce all produce the same repetitions, and two instances agree with each
other.

## Graphic EQ

7 fixed ISO bands, ±12 dB each, RBJ peaking biquads, Q ≈ 1.4, TDF-II, stereo (two filter
states per band). Hand-rolled POD biquad struct — **not** `juce::dsp::IIR::Coefficients`,
which is reference-counted and allocates on coefficient updates (forbidden on the audio
thread). Coefficients are computed once per block into a shared POD set (dirty flag per
band) and copied by value into each voice; only the filter *state* is per voice.

**Placement: on the voice's read taps**, before the feedback gain and the soft clip — not
on the recycle write. The rule is *every repetition is filtered exactly once as it is
played*, so repetition N carries EQ^N:

- Repetition 1 is filtered. Input enters its generation buffer un-EQ'd; the voice that
  plays it applies the EQ on the way out, so the first repeat already has the curve. (On
  the recycle write it did not — the input went in unfiltered and only got EQ'd on the
  *next* pass, so rep 1 was flat and rep N carried EQ^(N-1).)
- The recycled part of a buffer is one repetition older and has one more pass baked in,
  so the accounting stays exact at every generation.
- Turning a band moves the tone of everything currently sounding, not just of material
  entering the loop from now on.

**The Raw/Stable switch governs varispeed only — EQ behaves identically in both.** In Raw
the wet and recycle taps are the same signal, so one EQ instance per voice feeds both. In
Stable they are different streams (varispeed tap for the ear, unity tap for the loop), so
a voice carries two instances with independent state and the same curve. Worst case
5 voices × 2 taps × 7 bands × 2 ch = 140 biquads — still negligible.

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
│   1.20 s    [REGRID|BEND]            1.00x       0.65        │
│                          [¼][½][1][2][4]   [RAW|STABLE][CLIP]│
├─────────────────────────────────────────────────────────────┤
│  EQ [ON]   ▮ ▮ ▮ ▮ ▮ ▮ ▮      (7 vertical sliders, ±12 dB)   │
│            63 160 400 1k 2k5 6k3 16k                        │
├─────────────────────────────────────────────────────────────┤
│  ( DRY )   ( WET )                             build date   │
└─────────────────────────────────────────────────────────────┘
```

Time knob shows ms/s in free mode and the division name in sync mode; in BEND mode the
readout follows `T_eff` while it slews, so the glide is visible. Speed preset buttons
highlight when `speed` is within 1 cent of the preset. The optional repetition
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
3. **Caps + click protection + live knob moves** — `Dmax` clamp, `pOut` clamp against the
   writer, buffer-slot stealing, `T` changes in both time modes (REGRID snap, BEND slew +
   factor `m`), per-sample speed smoothing, positional fade envelope, unity bypass +
   `forceFade`, threshold soft clip + safety clamp. *Done when:* no clicks at any speed,
   no zipper on knob sweeps, a 0.25→4 speed sweep bends the whole tail without dropping a
   repetition, a full time-knob sweep stays clean in both modes and audibly bends in BEND,
   `fb = 1` is transparent and `fb = 2` saturates instead of exploding. Tune
   `kSpeedGlideMs` / `kBendUp` / `kBendDown` / `kClipThreshold` by ear here.
4. **Sync** — playhead, divisions up to 2 bars, PPQ-anchored boundaries with sample-
   accurate block splitting, jump/stop handling, standalone fallback BPM. *Done when:*
   repeats land on the grid, survive division changes and tempo ramps, and a bounce
   matches what was auditioned from any start point.
5. **EQ** — 7-band biquads, per-tap voice state, bypass, EQ^N accumulation with rep 1
   already filtered, identical in both feedback modes.
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
- reported tail equals 10 × Dmax and tracks T through free, sync and division changes
- voice count never exceeds `kMaxVoices`; buffer indices always in range
- REGRID time change while sounding: `T` 20 s → 200 ms mid-repetition produces a boundary
  within 200 ms, no voice reads a recycled slot, output stays finite and click-free; `T`
  200 ms → 20 s leaves a clean gap, not a stuck buffer
- BEND: `T_eff` never moves faster than the rate limits, `m` stays inside [0.25, 4], and
  `m` returns to exactly 1 once `T_eff == T_target`; a 20 s → 200 ms jump takes the
  predicted ≈ 6.6 s and stays click-free throughout
- speed sweep 0.25 → 4 with repeats overlapping: no voice reads past its source's written
  length, no voice is retired by the clamp, and every repetition that was sounding before
  the sweep is still sounding after it
- speed sweep is sample-smoothed: all voices see the same `r` at the same sample index;
  output has no step discontinuity larger than the signal itself
- positional envelope: a rate drop mid-fade-out un-fades instead of latching to silence
- fade scaling: at T = 10 ms the envelope still reaches unity and the repetition has a
  real ramp at both ends; at T = 1 s the fade is the 8 ms ceiling; at r = 4 it tracks the
  repetition, not the period
- BEND with `speed = 4` (effective rate up to 16) never reads past a source's written
  length — the clamp holds it at the writer's edge
- sync: a division change and a continuous tempo ramp both re-grid (REGRID) and glide
  (BEND) without clicks
- sync phase: boundaries fall on `ppq = k · divPPQ` within a sample, for straight, dotted
  and triplet divisions, at 120 and 143 bpm (fractional period), across a bar line
- deadband: a flat automation lane with float jitter never sets forceFade, so at speed 1
  the output has no periodic dip; a slow ramp of 0.1 %/block is still detected once it
  accumulates past the threshold
- sync repeatability: rendering bars 5–9 in isolation produces the same samples as
  rendering bars 1–9 and slicing; a loop jump forces a boundary and drops no voice
- soft clip: below −6 dBFS the recycle path is sample-exact (fb = 1, EQ off, r = 1 → a
  clean delay); above it the curve is monotonic and its first difference has no step at
  the threshold; `fb = 2` stays finite over 60 s of noise, and with `clip_on` off the
  ±8 safety clamp still keeps inf and NaN out
- no NaN/denormals after speed sweeps
- EQ: flat with all bands at 0 dB; a +12 dB band raises that bin by ~12 dB on repetition
  **1** and ~N × 12 dB on repetition N, identically in Raw and Stable

## Open questions

Behaviour — these change what the plugin does and should be settled before phase 2:

1. **Spacing at `r != 1`** — spacing is fixed at `T` (grid). The alternative (next
   repetition starts when the previous one ends, `T/r^N`) is real tape-runaway behavior
   and needs no overlap machinery at all. Worth a later `SPACING [GRID|TAPE]` switch.
2. **Overlap gain** — 4 overlapping repeats at `fb = 1` sum to ~+12 dB. Soft clip catches
   it, but consider scaling the wet tap by `1/sqrt(activeVoices)` or documenting the wet
   knob as the trim.
Quality and tuning — decide by ear during phases 3–5:

3. **Anti-aliasing at high `r`** — reading at 4× aliases. Cheap fix: one-pole LP at
   `sr/(2r)` on the recycle path when `r > 1`. Decide after listening.
4. **Interpolation and generational loss.** Linear interpolation loses HF on every pass,
    and this engine resamples the *same material* once per repetition, so the loss
    compounds — the tail darkens even with a flat EQ. That may be a feature (tape) or a
    defect (mud). Catmull-Rom if it is the latter.
5. **EQ gain smoothing** — dragging a band recomputes coefficients per block; check for
    zipper at ±12 dB and smooth the dB values if needed.
6. **Stereo** — one shared read pointer per voice for both channels (no width effect). A
    per-channel speed offset would be a nice later addition.

Scope:

7. **Memory shape** — allocate per actual channel count (halves it in mono) and decide
    what to do at 96/192 kHz, where the fixed 6 × 20 s pool reaches 92/184 MB. Clamping
    the max free-mode delay above 48 kHz is the cheap answer.
8. **Factory presets** — none planned. The parameter set is small enough that a handful
    of APVTS states (clean slapback, octave-down wash, runaway tape) would carry the
    plugin's character better than the defaults alone.
9. **Freeze / hold** — not requested, but the architecture gives it almost free: mute the
    input write and pin feedback at 1. Worth a button later.
