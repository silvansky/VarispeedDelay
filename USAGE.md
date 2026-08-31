# Varispeed Delay — usage

A delay where every repetition is replayed at a different tape speed. Pitch and duration
change together, and repetitions slower than the delay time overlap the following ones
instead of being cut short.

## Controls

| Control | What it does |
|---|---|
| **Delay Time** | The period between repetitions, 0.1 ms – 20 s, floored at the audio buffer size. In sync it follows the note division instead and the readout greys out. The ring around the knob marks where each division falls at the current tempo. |
| **note chip** | Locks the period to the host's ppq grid, so repetitions land where notes begin. |
| **TAP** | Taps a tempo. Free running it writes the delay time; in sync it sets the tempo used when the host has none. |
| **Note** | 1/32 up to 2 bars, straight, dotted and triplet. |
| **REGRID / BEND** | How the tail reacts to a time change — see below. |
| **GRID / TAPE** | When the next repetition starts — see below. |
| **Speed** | Tape speed, 0.25× – 4×. Global: turning it bends every sounding repetition at once. The line under the knob is the same figure in semitones. |
| **speed grid** | 3x3 shortcuts: octaves at 1/4, 1/2, 1, 2, 4 and fourths/fifths in between. They write the speed parameter, so they glide rather than jump. |
| **Feedback** | Recycle gain, 0 – 2. Above 1 the loop runs away by design. The readout under the knob is the loop gain: feedback times the largest EQ boost, since that boost compounds too. The arc turns red past the point where the loop stops decaying. |
| **RAW / STABLE** | Whether the varispeed compounds each generation — see below. |
| **NO CLIP / CLIP** | Soft clip in the recycle path. On by default. |
| **Clip Threshold** | Where that clip starts, -36 to -1 dBFS. The recycle path is untouched below it and tanh-shaped above it, up to a fixed 0 dBFS ceiling. Low settings saturate a runaway tail like tape, high ones stay clean until the loop is nearly at the ceiling. Greyed out when the clip is off. The knob's arc turns red while the clip is actually working. |
| **GRAPHIC EQ, OFF / ON** | ±12 dB at 63, 160, 400, 1k, 2.5k, 6.3k, 16k, applied once per repetition. The sliders stay live while the EQ is off, so a curve can be dialled in before switching it in. |
| **Dry / Wet** | Output mix. Wet is the trim for overlapping repetitions. |

Hover any control for a one-line description in the footer. Hover the `?` at the
bottom right for the sample rate, block size, audio device and build.

Click any value to type it. The delay time takes milliseconds, or seconds with an `s`
suffix; the speed takes a ratio like `1.5` or semitones written `+7s` / `-1.5s`. Hold
shift while dragging for fine control, double-click to return a control to its default.
The footer's zoom box and the bottom-right corner both resize the window.

Two readouts are worth telling apart. **Delay Time**, above the knob, is the target you
dialled. **period**, under the knob, is what the engine is actually running — the buffer
floor, the BEND glide and the TAPE grid all show up there, and only there.

## The two feedback types

- **RAW** recycles the varispeed signal, so each generation is the previous one resampled
  again: pitch `r^N`, duration `T/r^N`, EQ applied N times. A speed move is recorded into
  the loop and re-swept on every later pass, which is what makes long RAW washes drift.
- **STABLE** recycles a unity-rate copy, so buffer content never accumulates resampling.
  Every repetition is the same audio at speed `r`, same duration; only gain and EQ
  accumulate. Switching modes while the delay rings is seamless — repetition 1 sounds the
  same either way.

## The two time modes

- **REGRID** snaps to the new time immediately. Repetitions already in flight keep their
  rate, pitch and length and play out; new ones arrive on the new grid. The delay
  cross-fades between grids instead of dragging.
- **BEND** slews the time under a rate limit and doppler-bends everything sounding, like a
  tape delay. Shortening speeds the tail up and pitches it; lengthening slows and drops
  it. Dragging the knob bends at the speed of the drag; an instant jump takes the rate
  limit instead, so 20 s → 200 ms glides over about 6.6 s. The bend is recorded into the
  recycle path, so the next generation inherits it.

## The two spacings

- **GRID** keeps repetitions arriving every `T` no matter what the speed is, so the delay
  stays in time. Slow repeats overlap the next ones.
- **TAPE** starts the next repetition when the previous one *ends*, which is what a tape
  loop with a detuned motor does. Repeats then accelerate at `r > 1` and decelerate at
  `r < 1` — `T`, `T/r`, `T/r²`, … — until they hit the buffer-size / 20 s clamp. The time knob
  sets only the *first* period; after that the grid runs away. Nothing overlaps in TAPE.
  At `r = 1` TAPE and GRID are identical.

  **Below 1x the mode outruns itself.** Each generation records the previous repetition's
  duration, but replaying that recording takes `1/r` times as long again, so the windows
  double (at 0.5x) every generation: 2 s, 4 s, 8 s, 16 s... Once a repetition would exceed
  the 20 s ceiling it is truncated, and from then on you hear only the first part of each
  window — at 0.5x, half of it. That is arithmetic, not a defect: a tape that keeps slowing
  down can never replay everything it recorded. If you want every repetition complete, use
  **GRID**, where a 2 s window is always replayed in full over 4 s at 0.5x.

## Things that are deliberate, not defects

- **The grid is `T`, but material inside a repetition is not.** An input event at offset
  `n` inside a period returns after `T + n(1/r − 1)`, not exactly `T`. At `r > 1` the
  material is compressed toward the boundary, at `r < 1` stretched away from it. This is
  inherent to chunked varispeed; the *spacing* of repetitions is still exactly `T`.
- **The delay time cannot go below one audio buffer.** The knob reaches 0.1 ms, but the
  engine floors the period at the host's buffer size, because below that a single
  `processBlock` would open several generations at once. So the real minimum follows your
  buffer setting: 16 samples at 48 kHz gives 0.33 ms, 2048 at 44.1 kHz gives 46 ms. When
  the floor binds, the Delay Time readout still shows what you asked for and the `period`
  line under the knob shows what you get. Periods this short are a flutter/comb effect
  rather than a delay — which is a legitimate use, just not a rhythmic one.
- **Overlap gain.** Four overlapping repeats at feedback 1 sum to roughly +12 dB. There is
  no automatic compensation, because a `1/sqrt(voices)` scaling would pump as voices come
  and go. The soft clip bounds the loop and the Wet knob is the trim.
- **Generational HF loss at fractional speeds only.** The varispeed read interpolates, and
  each repetition resamples the previous one, so the tail darkens. But at 1×, 2× and 4×
  every read position is an integer and nothing is interpolated — those settings stay
  clean. 0.5×, 0.25× and anything off a preset darken progressively. Push the 6.3k / 16k
  bands to compensate. Turning the EQ off changes none of this: the interpolator is
  upstream of the filters.
- **EQ zipper.** Band gains are not smoothed, so a fast drag will zipper. Dragging a
  graphic EQ during a wash is a performance gesture.
- **Lengthening the time leaves a gap.** The last repetition finishes before the longer
  period is up, and the wet bus is silent until the next one starts.
- **Truncation at extreme slow settings.** A repetition is capped at 4× the delay time (or
  20 s). At 0.25× the first repeat is never truncated; later generations are, with a fade.
- **The transport clears the delay.** Starting playback, rewinding, locating, or wrapping
  a cycle drops every buffer and voice, so each pass of a loop and each take of a recording
  sound identical. The engine watches the host's timeline position, so it works with sync
  off and in hosts with no tempo. Stopping does *not* clear anything — the tail rings out,
  and it is the next start that kills it, ramped out over 2 ms so the cut is not a click.
- **Reported tail length is a bound, not a decay estimate.** It is ten generations of the
  longest repetition the current settings allow. With feedback ≥ 1 the true tail is
  infinite, so a host that honours the number will truncate a long feedback wash on an
  offline bounce. Print through the wet path if that matters.

## Presets

In the plugin builds the host's own preset menu shows the factory programs. The standalone
adds a preset combo and a `SAVE` button in the title row: dial in a sound, hit SAVE, and
the preset is written
to `$VSPD_PRESET_DIR` (or `~/Documents/VarispeedDelay/Presets`). Applying a preset resets
every parameter to its default first, so nothing bleeds through from the previous one.
