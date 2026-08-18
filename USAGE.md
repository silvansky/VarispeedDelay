# Varispeed Delay — usage

A delay where every repetition is replayed at a different tape speed. Pitch and duration
change together, and repetitions slower than the delay time overlap the following ones
instead of being cut short.

## Controls

| Control | What it does |
|---|---|
| **TIME** | The period between repetitions, 10 ms – 20 s. In SYNC it follows the division instead. |
| **FREE / SYNC** | SYNC locks the period to the host's ppq grid, so repetitions land where notes begin. |
| **division** | 1/32 up to 2 bars, straight, dotted and triplet. |
| **REGRID / BEND** | How the tail reacts to a time change — see below. |
| **GRID / TAPE** | When the next repetition starts — see below. |
| **SPEED** | Tape speed, 0.25× – 4×. Global: turning it bends every sounding repetition at once. |
| **speed grid** | 3x3 shortcuts: octaves at 1/4, 1/2, 1, 2, 4 and fourths/fifths in between. They write the speed parameter, so they glide rather than jump. |
| **FEEDBACK** | Recycle gain, 0 – 2. Above 1 the loop runs away by design. |
| **RAW / STABLE** | Whether the varispeed compounds each generation — see below. |
| **CLIP** | Soft clip in the recycle path. On by default. |
| **EQ + 7 bands** | ±12 dB at 63, 160, 400, 1k, 2.5k, 6.3k, 16k, applied once per repetition. |
| **DRY / WET** | Output mix. WET is the trim for overlapping repetitions. |

Hover any control for a one-line description in the footer.

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
  it. A 20 s → 200 ms jump glides over about 6.6 s. The bend is recorded into the recycle
  path, so the next generation inherits it.

## The two spacings

- **GRID** keeps repetitions arriving every `T` no matter what the speed is, so the delay
  stays in time. Slow repeats overlap the next ones.
- **TAPE** starts the next repetition when the previous one *ends*, which is what a tape
  loop with a detuned motor does. Repeats then accelerate at `r > 1` and decelerate at
  `r < 1` — `T`, `T/r`, `T/r²`, … — until they hit the buffer-size / 20 s clamp. The time knob
  sets only the *first* period; after that the grid runs away. Nothing overlaps in TAPE.
  At `r = 1` TAPE and GRID are identical.

## Things that are deliberate, not defects

- **The grid is `T`, but material inside a repetition is not.** An input event at offset
  `n` inside a period returns after `T + n(1/r − 1)`, not exactly `T`. At `r > 1` the
  material is compressed toward the boundary, at `r < 1` stretched away from it. This is
  inherent to chunked varispeed; the *spacing* of repetitions is still exactly `T`.
- **The delay time cannot go below one audio buffer.** The knob starts at 10 ms, but the
  engine floors the period at the host's buffer size, so a 2048-sample buffer at 44.1 kHz
  means a 46 ms minimum. Below that a single `processBlock` would open several generations
  at once. When the floor binds, the TIME readout shows the real period rather than the
  knob's value.
- **Overlap gain.** Four overlapping repeats at feedback 1 sum to roughly +12 dB. There is
  no automatic compensation, because a `1/sqrt(voices)` scaling would pump as voices come
  and go. The soft clip bounds the loop and the WET knob is the trim.
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
- **Reported tail length is a bound, not a decay estimate.** It is ten generations of the
  longest repetition the current settings allow. With feedback ≥ 1 the true tail is
  infinite, so a host that honours the number will truncate a long feedback wash on an
  offline bounce. Print through the wet path if that matters.

## Presets

In the plugin builds the host's own preset menu shows the factory programs. The standalone
adds a `PRESET [combo] [Save…]` row: dial in a sound, hit Save, and the preset is written
to `$VSPD_PRESET_DIR` (or `~/Documents/VarispeedDelay/Presets`). Applying a preset resets
every parameter to its default first, so nothing bleeds through from the previous one.
