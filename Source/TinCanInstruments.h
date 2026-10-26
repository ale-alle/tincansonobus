// SPDX-License-Identifier: GPLv3-or-later WITH Appstore-exception
// Copyright (C) 2026 Tin Can Studios

#pragma once

#include <string>
#include <vector>

// Instrument data for the teaching tools (T-035): tunings, ranges and transpositions are data,
// not code, so a new tuning or instrument is one table entry. No JUCE here (unit tested in
// tests/TinCanInstrumentsTest.cpp).
namespace TinCan {

struct Tuning
{
    std::string name;
    std::vector<int> strings;  // open-string MIDI notes, lowest-pitched string first
};

struct InstrumentDef
{
    enum Kind { Keyboard, Fretted };

    std::string name;
    Kind kind = Keyboard;
    int frets = 0;                  // fretted only: highest fret
    std::vector<Tuning> tunings;    // fretted only: first is the standard tuning
};

// Piano, 6-string guitar, 4- and 5-string bass
const std::vector<InstrumentDef> & getInstruments();

struct FretPosition
{
    int string = 0;  // index into Tuning::strings
    int fret = 0;

    bool operator==(const FretPosition & o) const { return string == o.string && fret == o.fret; }
};

// every place a (concert pitch) note can be played, lowest string first; empty if out of range
std::vector<FretPosition> fretPositions(const std::vector<int> & strings, int frets, int note);

// "C4", "F#3", "Bb2", "e4" → MIDI note (middle C = C4); -1 if not a note
int parseNote(const std::string & text);

// "E2 A2 D3 G3 B3 E4" (spaces or commas) → strings; empty if anything is invalid or there are
// fewer than 1 or more than 12 strings
std::vector<int> parseTuning(const std::string & text);
std::string formatTuning(const std::vector<int> & strings, bool useFlats = false);

// Transposing instruments: written = concert + semitones.
struct Transposition
{
    std::string name;
    int semitones = 0;
};

// Concert pitch first, then Bb, Eb, F, A and octave entries (guitar and bass are written an
// octave above how they sound)
const std::vector<Transposition> & getTranspositions();

}
