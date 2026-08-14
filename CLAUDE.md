# CLAUDE.md

## Build

```bash
cd build && cmake --build . --config Release     # incremental
cmake .. -DCMAKE_BUILD_TYPE=Release              # reconfigure after CMakeLists changes
```

Outputs: `build/VarispeedDelay_artefacts/Release/{Standalone,AU,VST3}/`
Run: `open build/VarispeedDelay_artefacts/Release/Standalone/VarispeedDelay.app`
Install AU + auval: `./install-au.sh`

## Architecture

JUCE 8 plugin (VST3/AU/Standalone), C++20, JUCE submodule in `libs/JUCE`.
Delay where each repetition is replayed at a different tape speed.

See `plan.md` for the full design: period/ping-pong buffer model, raw vs stable feedback,
crossfade rules, parameter table, phases.

Current state: skeleton only (passthrough processor, placeholder editor).

## Conventions

- Zero warnings. Verify with a Release build before committing.
- No allocation, locks or I/O on the audio thread.
- Self-documenting code, no comments unless critical. Minimal diffs.
- Commits: imperative, short, no period.
- JUCE Font API: `juce::FontOptions(size)`, not the deprecated float constructor.

## Presets

Authored in the standalone, not in a DAW: run the binary with `VSPD_PRESET_DIR` pointing
at `presets/`, dial in a sound, hit Save.

```bash
VSPD_PRESET_DIR=$PWD/presets \
  build/VarispeedDelay_artefacts/Release/Standalone/VarispeedDelay.app/Contents/MacOS/VarispeedDelay
```

Run the binary directly — `open` does not pass environment variables through. Adding a
preset file needs a `cmake ..` before it is embedded (CMake globs at configure time).
