#pragma once

#include "GraphicEQ.h"

namespace vspd
{

enum class FbType { Raw = 0, Stable };

/** How the audible tap reads its source. Reverse keeps the buffer's orientation by
    recycling a forward read; Alternate recycles what it hears, so orientation flips. */
enum class Direction { Forward = 0, Reverse, Alternate };

/** One repetition in flight: reads generation `src` at the varispeed rate and
    writes its recycled result into generation `dst`. */
struct Voice
{
    bool   active      = false;
    bool   writing     = false;
    bool   retiring    = false;
    bool   fadeIn      = false;   // start of this repetition is a splice
    bool   fadeOut     = false;   // end of it is too
    bool   sourceLost  = false;   // src slot was recycled under us — hold last sample
    bool   contiguous  = false;   // source is exactly one period, so the unity bypass is safe
    bool   silent      = false;   // audible tap done, unity recycle copy still running
    FbType fbType      = FbType::Raw;
    Direction dir      = Direction::Forward;

    int    src = 0, dst = 0;
    int    w = 0;                 // write head in dst, advances by 1
    int    order = 0;             // spawn sequence, for stealing

    double pOut = 0.0;            // read head in src, advances by the effective rate
    double srcEnd = 0.0;          // source length latched at spawn, reverse only
    double elapsed = 0.0;         // output samples since spawn
    double dmax = 0.0;            // cap, output samples
    double xfade = 1.0;           // fade length, output samples, latched at spawn
    double predDur = 0.0;         // predicted audible duration, output samples
    double predWriteEnd = 0.0;    // predicted final value of w
    double retireLeft = 0.0;
    double retireEnv = 1.0;

    float  env = 0.0f;
    float  last[2] { 0.0f, 0.0f };

    EqState eqOut, eqRec;         // eqRec: Stable's unity copy, or Reverse Raw's mirror read
};

} // namespace vspd
