# CLAUDE.md

## Build

```bash
cd build && cmake --build . --config Release     # incremental
cmake .. -DCMAKE_BUILD_TYPE=Release              # reconfigure after CMakeLists changes
```

Outputs: `build/VarispeedDelay_artefacts/Release/{Standalone,AU,VST3}/`
Run: `open build/VarispeedDelay_artefacts/Release/Standalone/VarispeedDelay.app`
Install AU + auval: `./install-au.sh`
Tests: `cd build && ctest --output-on-failure` (engine unit tests + preset/parameter tests)

## Architecture

JUCE 8 plugin (VST3/AU/Standalone), C++20, JUCE submodule in `libs/JUCE`.
Delay where each repetition is replayed at a different tape speed.

See `plan.md` for the full design: period/ping-pong buffer model, raw vs stable feedback,
crossfade rules, parameter table, phases. `USAGE.md` documents the accepted behaviours.

```
src/DelayEngine.{h,cpp}    generation ring, voice pool, periods, fades, bend, sync, clip
src/Voice.h                per-voice state
src/GraphicEQ.{h,cpp}      7 x stereo RBJ peaking biquad, POD coefficients
src/Presets.{h,cpp}        embedded + user presets, reset-then-overlay apply
src/LookAndFeel.{h,cpp}    tapedeck palette, knobs, switches, fonts
src/PluginProcessor.*      APVTS layout, raw pointers, playhead, programs
src/PluginEditor.*         fixed 800x500 content the editor scales, hover help footer
tests/EngineTests.cpp      DelayEngine only, no AudioProcessor dependency
tests/PresetTests.cpp      preset staleness + parameter round trips
```

Engine invariants worth knowing before changing it:

- Generation buffers are never memset. Each has one writing voice whose `w` advances in
  lockstep with the period counter `n`, so the input `+=` lands on a slot just written and
  `=` otherwise. Readers are bounded by `written`.
- The period ends at the *current* effective T, not the length latched at the boundary —
  that is what makes a 20 s to 200 ms shrink react in 200 ms. TAPE spacing is the one
  exception, where the latched voice duration is the period.
- The shortest period is the host's buffer size, not `kMinDelayMs` — that constant is only
  the `time_ms` parameter floor. The UI shows the real period when the clamp binds.
- A generation records input for one period but its writing voice recycles past that at
  any rate below 1, so the buffer has a join at index `periodLen` that no voice envelope
  covers. `Gen::inputTaper` crossfades the recorded input into the recycled-only tail
  there. It is zero at rate 1 and above, which keeps unity sample-exact.
- The unity bypass needs more than `rEff == 1`. It assumes the repetition tiles the grid,
  which is false once a source buffer is longer than a period (left over from a slower
  setting) — `Voice::contiguous` gates it. And the policy either side of a join must
  agree: a voice inherits `fadeIn` from its predecessor's `fadeOut`, otherwise a fading
  voice hands over to a bypassing one and the seam is a cliff.
- Fades are positional (computed from the voice's state each sample), never latched
  countdowns, so a rate change mid-fade un-fades correctly.
- Raw's recycle write ends with the audible tap; Stable's is a unity copy exactly as long
  as its source, so it outlives the audible tap at speeds above 1.

## Conventions

- Zero warnings. Verify with a Release build before committing.
- No allocation, locks or I/O on the audio thread.
- Self-documenting code, no comments unless critical. Minimal diffs.
- Commits: imperative, short, no period.
- JUCE Font API: `juce::FontOptions(size)`, not the deprecated float constructor.
- **ASCII only in string literals.** `juce::String (const char*)` decodes as Latin-1, not
  UTF-8, so an em-dash or arrow in a UI string arrives as mojibake. Write `-`, not `—`.
  Comments are unaffected.

## Presets

The `PRESET [combo] [Save...]` row appears in **every debug build** (all formats) and in
the release standalone. Release plugin builds hide it and leave preset management to the
host; `-DVSPD_PRESET_AUTHORING=ON` opts a release build back in.

Authoring in the standalone:

```bash
VSPD_PRESET_DIR=$PWD/presets \
  build/VarispeedDelay_artefacts/Release/Standalone/VarispeedDelay.app/Contents/MacOS/VarispeedDelay
```

Authoring in a DAW — build Debug and point the DAW at `build-debug/.../Debug/VST3`:

```bash
cmake -B build-debug -DCMAKE_BUILD_TYPE=Debug && cmake --build build-debug -j8
```

A DAW launched from Finder will not have inherited `VSPD_PRESET_DIR`, so presets land in
`~/Documents/VarispeedDelay/Presets` unless the DAW itself is started from a shell that
sets it. The Save dialog prints the destination directory. Copy the files into `presets/`
to commit them.

Run the standalone binary directly — `open` does not pass environment variables through.
Adding a preset file needs a `cmake ..` before it is embedded (CMake globs at configure
time).
