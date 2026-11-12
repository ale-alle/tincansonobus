// SPDX-License-Identifier: GPLv3-or-later WITH Appstore-exception
// Copyright (C) 2026 Tin Can Studios

#include "TinCanTest.h"
#include "../Source/TinCanChords.h"

using namespace TinCan;

static std::string name(std::vector<int> notes, bool flats = false, int transpose = 0)
{
    return chordName(identifyChord(notes), flats, transpose);
}

void runChordsTests()
{
    // triads in root position
    CHECK(name({ 60, 64, 67 }) == "C");
    CHECK(name({ 57, 60, 64 }) == "Am");
    CHECK(name({ 59, 62, 65 }) == "Bdim");
    CHECK(name({ 60, 64, 68 }) == "Caug");
    CHECK(name({ 62, 67, 69 }) == "Dsus4");
    CHECK(name({ 62, 64, 69 }) == "Dsus2");

    // spread over octaves, doubled notes, any order
    CHECK(name({ 67, 48, 76, 60, 64 }) == "C");

    // inversions: slash bass
    CHECK(name({ 64, 67, 72 }) == "C/E");
    CHECK(name({ 55, 60, 64 }) == "C/G");
    CHECK(name({ 59, 62, 67 }) == "G/B");

    // sevenths and more
    CHECK(name({ 55, 59, 62, 65 }) == "G7");
    CHECK(name({ 60, 64, 67, 71 }) == "Cmaj7");
    CHECK(name({ 62, 65, 69, 72 }) == "Dm7");
    CHECK(name({ 59, 62, 65, 69 }) == "Bm7b5");
    CHECK(name({ 59, 62, 65, 68 }) == "Bdim7");
    CHECK(name({ 60, 64, 67, 70, 74 }) == "C9");
    CHECK(name({ 53, 57, 60, 64 }) == "Fmaj7");
    CHECK(name({ 59, 65, 67, 74 }) == "G7/B");

    // the same notes as two chords: the lowest note decides
    CHECK(name({ 60, 64, 67, 69 }) == "C6");
    CHECK(name({ 57, 60, 64, 67 }) == "Am7");

    // first inversions that look like sixth chords without a fifth (QA): only sevenths drop the fifth
    CHECK(name({ 60, 64, 69 }) == "Am/C");
    CHECK(name({ 60, 63, 69 }) == "Adim/C");
    CHECK(name({ 60, 62, 64 }).empty());

    // a seventh without its fifth
    CHECK(name({ 48, 52, 58 }) == "C7");
    CHECK(name({ 60, 64, 71 }) == "Cmaj7");

    // power chord; other two-note shapes and single notes have no chord name
    CHECK(name({ 40, 47 }) == "E5");
    CHECK(name({ 40, 47, 52 }) == "E5");
    CHECK(name({ 60, 64 }).empty());
    CHECK(name({ 60 }).empty());
    CHECK(name({ 60, 72 }).empty());
    CHECK(name({}).empty());

    // notes that don't make a known chord: no name rather than a wrong one
    CHECK(name({ 60, 61, 62 }).empty());

    // sharps or flats, and written pitch for transposing instruments
    CHECK(name({ 58, 62, 65 }) == "A#");
    CHECK(name({ 58, 62, 65 }, true) == "Bb");
    CHECK(name({ 58, 62, 65, 69 }, true) == "Bbmaj7");
    CHECK(name({ 60, 64, 67 }, false, 2) == "D");     // concert C on a B-flat instrument reads as D
    CHECK(name({ 64, 67, 72 }, false, 2) == "D/F#");

    // out-of-range notes are ignored
    CHECK(name({ -1, 128, 60, 64, 67 }) == "C");
}
