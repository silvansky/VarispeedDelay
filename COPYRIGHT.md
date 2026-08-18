# Licensing

VarispeedDelay links the [JUCE](https://juce.com) framework, which is dual-licensed under
the AGPLv3 and a commercial JUCE licence. This project uses JUCE under the **AGPLv3**.

## The project as a whole

Any binary built from this repository is a combined work with JUCE and is therefore
distributed under the **GNU Affero General Public License v3.0** — see `LICENSE`. If you
distribute such a binary, you must make the complete corresponding source available to
its recipients under the same terms.

Copyright (c) 2026 Valentine Silvansky.

## The code in this repository

The files written for this project — everything under `src/` and `tests/`, plus
`CMakeLists.txt` and the `cmake/` helpers — are additionally offered under the **MIT
License** (see `LICENSE-MIT`), at your option, when used independently of JUCE.

In other words: reuse the delay engine, the graphic EQ or anything else here in your own
project under MIT terms; but a plugin binary built against AGPL-licensed JUCE, whether by
you or by us, carries the AGPLv3.

## Third-party components

JUCE is included as a git submodule under `libs/JUCE` and carries its own licence and
dependency notices — see `libs/JUCE/LICENSE.md`.
