# Varispeed Delay — UI style guide

The interface is the "tapedeck" sheet from `design/05-tapedeck.svg` (removed from the tree
once it was built; it is in git at `2b9c6e2`). This document is the sheet turned into
rules, so the UI can be extended without going back to the SVG.

## Canvas

Everything is laid out at **800 x 500**. `EditorContent` is that size and never changes;
`VarispeedDelayEditor` scales it with one `AffineTransform`, so the footer zoom box and the
corner resizer only resize the editor. Every coordinate in `resized()` and `paint()` is a
design-sheet coordinate — no percentages, no `removeFromLeft` chains.

Sections are separated by 1 px lines in `divider` at 60 % alpha, not by panels or boxes:

| Line | Extent |
|---|---|
| horizontal `y = 38` | full width, under the title row |
| horizontal `y = 262` | full width, under the three top sections |
| horizontal `y = 468` | full width, over the footer |
| vertical `x = 286`, `x = 556` | `y = 44 … 258`, splitting DELAY / SPEED / FEEDBACK |
| vertical `x = 392` | `y = 268 … 462`, splitting GRAPHIC EQ / OUTPUT |

## Palette

Names are the constants in `vspd::col` (`src/LookAndFeel.h`). Nothing outside
`LookAndFeel.cpp` should pick a colour by hex.

| Token | Hex | Used for |
|---|---|---|
| `background` | `#2C3D50` | the whole surface |
| `divider` | `#46596E` | section rules, at 60 % alpha |
| `track` | `#26323F` | unfilled slider and knob-arc track, editor background |
| `panel` | `#3B4F66` | unlit chips, buttons and combo faces |
| `panelEdge` | `#54697F` | 1 px border on those faces |
| `accent` | `#6BDD97` | value arcs, slider fills, lit chips, the current division tick |
| `onAccent` | `#12211A` | text on an `accent` fill |
| `heading` | `#6FC5E8` | section titles only |
| `warn` | `#F0705A` | loop-gain overshoot, the clip arc while clipping |
| `text` | `#EEF4F8` | value readouts |
| `mid` | `#A7BAC9` | captions, unlit chip labels, the product name |
| `dim` | `#7F93A5` | secondary readouts, footer, axis labels, inactive values |
| `knobFace` | `#485D73` | knob face |
| `knobTop` / `knobBot` | `#5A7189` / `#374A5D` | slider thumb face gradient |
| `knobEdge` | `#22303E` | 1 px ring around a knob face or thumb |
| `pointer` | `#E2ECF3` | knob pointer, thumb centre line |

Colour carries meaning, so do not decorate with it: **green is a value or an active
state, cyan is a section title, red is a warning about the signal.** A control that is
merely selected uses `accent`; a control that is telling you something is wrong uses
`warn`.

**Fills are flat.** Do not add a gradient to anything unless it is explicitly asked for.
The slider thumb is the one that exists, kept because it is small enough to read as a
bevel rather than as shading. Depth comes from the flat face plus its 1 px `knobEdge`
ring, not from a light source.

## Type

Two families, both addressed through `uiFont(pt)` / `monoFont(pt)`, which set
`withPointHeight` so a size in the code is the size on the sheet. `withHeight` would come
out roughly 15 % small — never use it.

| Role | Font | Notes |
|---|---|---|
| product name | `uiFont(10, bold)` in `mid` | tracking 3.4 |
| section title | `uiFont(14)` in `heading` | tracking 2.6, centred over its section |
| `GRAPHIC EQ` title | `uiFont(10)` in `heading` | tracking 2.4, left aligned at `x = 24` |
| control caption | `uiFont(9.5)` in `mid` | centred over its control |
| primary value | `uiFont(15)` in `text` | 14 for the smaller OUTPUT column |
| unit suffix | `uiFont(8.5)` in `dim` | drawn after the number, never the same size |
| chip label | `uiFont(8.5)`, bold when lit | `mid` off, `onAccent` on |
| button label | `uiFont(9)`, bold when lit | `TextButton`, same colours |
| secondary readout | `monoFont(7)` or `monoFont(7.5)` in `dim` | BPM, period, semitones, EQ axis |
| footer | `monoFont(8)` in `dim` at 80 % | |

Tracked text goes through `drawTracked` / `drawTrackedCentred`; JUCE has no letter spacing
of its own. Static text is drawn in `paint()` at an explicit baseline, not with `Label`s.

**ASCII only in string literals** — see the conventions in `CLAUDE.md`. The tempo-sync
chip draws its note glyph as a `Path` for exactly this reason.

## Knobs

A knob is a `ParamSlider` in `RotaryHorizontalVerticalDrag`, drawn by
`VarispeedLookAndFeel::drawRotarySlider`. The sweep is `kRotaryStart … kRotaryEnd`,
1.25π to 2.75π — 270°, gap at the bottom.

Anatomy, outward from the centre:

- **face** — radius `0.77 * arcR`, flat `knobFace`, 1 px `knobEdge` ring.
- **pointer** — `pointer`, 2 px, rounded, from `0.22` to `0.86` of the face radius.
- **arc** — radius `arcR`, `kKnobArcThickness` (3.5 px). `track` for the whole sweep, then
  the value in `accent` over it.
- **overshoot ring** — radius `arcR + 4.5`, 2 px, `warn` at 55 %, only when the knob
  publishes a `"split"`.

Size a knob by choosing `arcR` and deriving the bounds:

```
side = 2 * arcR + kKnobArcThickness + 2 * kKnobInset      // = 2 * arcR + 17.5
```

`kKnobInset` (7 px) is the margin that keeps the overshoot ring inside the component. The
shipped radii are 31.25 (delay), 38.25 (speed, feedback) and 33.25 (clip threshold).

Two component properties let the editor change a knob's meaning without teaching it about
colour:

- `"split"` — normalised position where the arc stops being `accent` and becomes `warn`.
  The feedback knob sets it to the feedback at which loop gain reaches 1.
- `"alert"` — draws the whole value arc in `warn`. The clip knob sets it while clipping.

Add a third only if the same rule applies: **the editor publishes state, the look and feel
decides what colour that is.**

## Chips, buttons and combos

Corner radius 3 everywhere. Lit means an `accent` fill with `onAccent` text; unlit means a
`panel` fill with a 1 px `panelEdge` border and `mid` text. Disabled multiplies alpha by
0.45.

- **`ChoiceSwitch`** — one segment per choice, 3 px apart, so `NO CLIP | CLIP` reads as two
  buttons rather than one pill. Standard height 16; the tempo row uses 22.
- **`ToggleChip`** — a single latching chip, text or an icon painter.
- **`juce::TextButton`** — momentary (`TAP`, `SAVE`) or a shortcut whose toggle state is
  driven by the parameter (the speed grid).
- **`juce::ComboBox`** — same face, a stroked chevron 12 px from the right edge, text
  inset 8 px.

A switch group under a knob is **centred on the knob's centre**, not on its own column.

## Sliders

Vertical only. 3 px `track`, filled in `accent`; the fill starts at the centre when the
range is bipolar (EQ bands) and at the bottom when it is not (dry, wet). The thumb is
18 x 7, radius 2, `knobTop → knobBot` gradient, `knobEdge` border, `pointer` centre line at 70 %.

The EQ is a 7-band comb at `x = 52 + 48i`: value field at `y = 300`, slider `(cx-10, 318,
20, 84)`, frequency label baseline 418, zero line across `y = 360`, `dB` / `Hz` axis
captions on the baseline at 440.

## Value readouts

Every number is a `ValueField` — a `Label` that paints the number and a smaller `dim` unit,
and opens an editor on a click. Rules:

- The label's own text is what seeds the editor, so **it carries the unit**. A field whose
  unit the parameter's `getValueForText` cannot parse needs its own `onEdit`; delay time
  (ms vs s) and speed (ratio vs semitones) both have one.
- A readout that is not the target belongs somewhere else. `Delay Time` shows what you
  dialled; the `period` line under the knob shows what the engine is running.
- Grey a value to `dim` when something else is driving it — the delay time in sync, an EQ
  band at 0.

## Interaction

Uniform across every control, and advertised in the footer when nothing is hovered:

- **click a value to type it**
- **shift-drag for fine control** — rotaries raise drag sensitivity, linear sliders switch
  to velocity mode for the duration of the drag
- **double-click to reset** to the parameter default
- **hover for help** — `mouseEnter` walks up from the hovered component looking for a
  `"help"` property, so a child inherits its parent's text. Every control sets one.

The footer is one left-aligned status line, never two. It shows the standing hint, or the
hovered control's help, or - over the `?` only - that help followed by the technical
readout (rate, block, device, version, build).

## Painting

`paint()` reads a snapshot that the 30 Hz timer refreshes, and the timer calls `repaint()`
only when that snapshot changes. Adding a live readout means adding its value to that
snapshot tuple, not calling `repaint()` from the timer unconditionally.

## Adding a control

1. Place it in `resized()` in design-sheet coordinates; centre switch groups on the knob
   above them.
2. Caption in `paint()` with `drawCaption`, at the caption baseline for that section.
3. Give it a `"help"` string.
4. If it has a number, use a `ValueField` and check the parameter can parse its own unit.
5. If it needs a colour the look and feel does not already have, add a token to
   `vspd::col` and use it there — not a literal at the call site.
