// SPDX-License-Identifier: GPLv3-or-later WITH Appstore-exception
// Copyright (C) 2026 Tin Can Studios

#include "TinCanBeatClock.h"

#include <cmath>

namespace TinCan {

void BeatClock::restart()
{
    position = 0.0;
    countInBeats = (double) countInBars * beatsPerBar;
}

void BeatClock::setPosition(double beat)
{
    position = beat;
    countInBeats = 0.0;
}

double BeatClock::getCountInRemaining() const
{
    return position < countInBeats ? countInBeats - position : 0.0;
}

int BeatClock::process(int nframes, Click * clicks, int maxClicks)
{
    if (nframes <= 0 || tempo <= 0.0) return 0;

    const double beatsPerSample = tempo / (60.0 * sampleRate);
    const double start = position;
    const double end = start + nframes * beatsPerSample;

    // A grid point (index k = beat k / subdivision) belongs to the block holding its nearest
    // sample, i.e. beats in [start - half a sample, end - half a sample). Consecutive blocks
    // share that boundary, so no click is played twice or skipped.
    const double half = 0.5 * beatsPerSample;
    const double from = start - half;
    const double to = end - half;
    int count = 0;
    const double sub = subdivision;
    int64_t k = (int64_t) std::ceil(from * sub);
    if (k / sub < from) ++k;
    for (; count < maxClicks; ++k) {
        double beat = k / sub;
        if (beat >= to) break;

        int offset = (int) std::floor((beat - from) / beatsPerSample);
        if (offset < 0) offset = 0;
        if (offset >= nframes) offset = nframes - 1;

        Click & c = clicks[count++];
        c.offset = offset;
        int64_t beatIndex = k >= 0 ? k / subdivision : -((-k + subdivision - 1) / subdivision);
        bool onBeat = (k % subdivision) == 0;
        int64_t inCount = beatIndex - (int64_t) countInBeats;  // beat number after the count-in
        c.countIn = beat < countInBeats;
        c.beatInBar = (int) (((c.countIn ? beatIndex : inCount) % beatsPerBar + beatsPerBar) % beatsPerBar);
        if (!onBeat) c.kind = Click::Sub;
        else if (c.beatInBar == 0 && beatsPerBar > 1) c.kind = Click::Bar;
        else c.kind = Click::Beat;
    }

    position = end;
    return count;
}

}
