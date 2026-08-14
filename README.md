# Varispeed Delay

JUCE audio plugin (VST3 / AU / Standalone): a delay where every repetition is replayed at
a different tape speed — pitch and duration change together.

- time knob, free (up to 20 s) or tempo-synced (up to 2 bars)
- speed knob 1/4x – 4x with 1/4, 1/2, 1, 2, 4 preset buttons
- feedback 0–2, raw (cumulative varispeed) or stable (one-shot varispeed) recycling
- 7-band graphic EQ in the feedback path, ±12 dB, bypassable
- separate dry and wet knobs
- short crossfades at repetition boundaries for click protection

See `plan.md` for the design and `CLAUDE.md` for build instructions.

Status: project skeleton, DSP not implemented yet.
