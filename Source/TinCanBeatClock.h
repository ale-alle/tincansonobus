// SPDX-License-Identifier: GPLv3-or-later WITH Appstore-exception
// Copyright (C) 2026 Tin Can Studios

#pragma once

#include <cstdint>

// Sample-accurate click schedule for the metronome (T-036). Runs on the audio thread: no
// allocation, no locks. The position is kept in beats as a double and each click lands on the
// sample nearest its exact time, so the tempo doesn't drift over a long lesson (the old
// schedule cut every beat to a whole number of samples). No JUCE, so it is unit tested in
// tests/TinCanBeatClockTest.cpp.
namespace TinCan {

class BeatClock
{
public:
    struct Click
    {
        enum Kind : uint8_t { Bar, Beat, Sub };

        int offset = 0;         // sample within the block
        Kind kind = Beat;
        bool countIn = false;   // part of the count-in bars
        int beatInBar = 0;      // 0-based
    };

    void setSampleRate(double rate) { sampleRate = rate > 0 ? rate : 48000.0; }
    void setTempo(double bpm) { tempo = bpm; }
    void setBeatsPerBar(int n) { beatsPerBar = n < 1 ? 1 : (n > 32 ? 32 : n); }
    void setSubdivision(int n) { subdivision = n < 1 ? 1 : (n > 8 ? 8 : n); }
    void setCountInBars(int n) { countInBars = n < 0 ? 0 : (n > 4 ? 4 : n); }

    int getBeatsPerBar() const { return beatsPerBar; }
    int getSubdivision() const { return subdivision; }

    // start again from beat 0, with the count-in first if one is set
    void restart();
    // follow an external beat position (host or file playback); no count-in
    void setPosition(double beat);

    // Finds the clicks in the next nframes samples and advances. Writes at most maxClicks;
    // returns how many were written.
    int process(int nframes, Click * clicks, int maxClicks);

    // beats since restart, counting the count-in (negative values never happen)
    double getPosition() const { return position; }
    // beats left in the count-in, 0 when it's over or there is none
    double getCountInRemaining() const;

private:
    double sampleRate = 48000.0;
    double tempo = 100.0;
    int beatsPerBar = 1;
    int subdivision = 1;
    int countInBars = 0;

    double position = 0.0;     // in beats
    double countInBeats = 0.0; // count-in length fixed at restart
};

}
