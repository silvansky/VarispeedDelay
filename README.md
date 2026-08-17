# Varispeed Delay

JUCE audio plugin (VST3 / AU / Standalone): a delay where every repetition is replayed at
a different tape speed — pitch and duration change together.

- time knob, free (up to 20 s) or tempo-synced (up to 2 bars)
- speed knob 1/4x – 4x with 1/4, 1/2, 1, 2, 4 preset buttons
- feedback 0–2, raw (cumulative varispeed) or stable (one-shot varispeed) recycling
- 7-band graphic EQ in the feedback path, ±12 dB, bypassable
- separate dry and wet knobs
- slow repetitions overlap the following ones, never cut off at the period boundary
- time mode switch: regrid the tail on a time change, or bend it like tape
- spacing switch: keep repetitions on the grid, or let them run away like a tape loop
- short crossfades at repetition edges for click protection
- soft clip in the recycle path, transparent below −6 dBFS

See `plan.md` for the design, `USAGE.md` for what the controls do, and `CLAUDE.md` for
build instructions.

## Build

```bash
cd build && cmake .. -DCMAKE_BUILD_TYPE=Release && cmake --build . -j8
ctest --output-on-failure          # engine + preset unit tests
```

Status: engine, UI, sync, EQ and preset machinery implemented. Factory preset *content*
is still to be authored (see `CLAUDE.md`).
