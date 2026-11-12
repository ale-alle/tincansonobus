// SPDX-License-Identifier: GPLv3-or-later WITH Appstore-exception
// Copyright (C) 2026 Tin Can Studios

#pragma once

#include <string>
#include <vector>

// Chord names for the keys held on the on-screen instrument (T-074). No JUCE, so the unit tests
// (TinCanTests) can check it.
//
// Every held pitch class must belong to the chord: extra notes mean no name rather than a wrong one.
// When the same notes make two chords (C6 and Am7), the one built on the lowest note wins; otherwise
// the name gets a slash bass ("C/E"). A seventh or ninth chord may leave out its fifth, as players often do.
namespace TinCan {

struct Chord
{
    bool valid = false;
    int root = 0;          // pitch class 0-11, C = 0
    int bass = 0;          // pitch class of the lowest note
    std::string suffix;    // "", "m", "7", "maj7", "m7b5", "sus4", "5", …
};

// notes: MIDI note numbers, in any order, duplicates allowed; two different pitch classes make
// only a power chord ("5") or nothing
Chord identifyChord(const std::vector<int> & notes);

// "Am7", "G7/B", "Bbmaj7"; transpose (semitones) gives the written name for transposing instruments
std::string chordName(const Chord & chord, bool useFlats = false, int transpose = 0);

}
