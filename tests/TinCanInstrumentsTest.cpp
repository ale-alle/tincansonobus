// SPDX-License-Identifier: GPLv3-or-later WITH Appstore-exception
// Copyright (C) 2026 Tin Can Studios

#include "TinCanTest.h"
#include "../Source/TinCanInstruments.h"

using namespace TinCan;

static const InstrumentDef * findInstrument(const std::string & name)
{
    for (auto & i : getInstruments()) {
        if (i.name == name) return &i;
    }
    return nullptr;
}

static void testInstrumentData()
{
    auto * piano = findInstrument("Piano");
    auto * guitar = findInstrument("Guitar");
    auto * bass4 = findInstrument("Bass (4 strings)");
    auto * bass5 = findInstrument("Bass (5 strings)");
    CHECK(piano && piano->kind == InstrumentDef::Keyboard);
    CHECK(guitar && guitar->kind == InstrumentDef::Fretted && guitar->tunings.size() >= 4);
    CHECK(bass4 && bass4->tunings[0].strings.size() == 4);
    CHECK(bass5 && bass5->tunings[0].strings.size() == 5);
    if (!guitar || !bass4 || !bass5) return;

    CHECK(formatTuning(guitar->tunings[0].strings) == "E2 A2 D3 G3 B3 E4");
    CHECK(formatTuning(bass4->tunings[0].strings) == "E1 A1 D2 G2");
    CHECK(formatTuning(bass5->tunings[0].strings) == "B0 E1 A1 D2 G2");

    // every tuning: strings ascending from lowest, within MIDI range
    for (auto & inst : getInstruments()) {
        for (auto & t : inst.tunings) {
            CHECK(!t.strings.empty());
            for (size_t s = 0; s < t.strings.size(); ++s) {
                CHECK(t.strings[s] >= 0 && t.strings[s] + inst.frets <= 127);
                if (s > 0) CHECK(t.strings[s] > t.strings[s - 1]);
            }
        }
    }
}

static void testFretPositions()
{
    const std::vector<int> guitar = { 40, 45, 50, 55, 59, 64 };

    // low E only on the open low string
    auto p = fretPositions(guitar, 22, 40);
    CHECK(p.size() == 1 && p[0] == (FretPosition { 0, 0 }));

    // middle C: 20th fret low E, 15th A, 10th D, 5th G, 1st B; not on the high E
    p = fretPositions(guitar, 22, 60);
    CHECK(p.size() == 5);
    CHECK(p[0] == (FretPosition { 0, 20 }) && p[1] == (FretPosition { 1, 15 }) && p[2] == (FretPosition { 2, 10 }));
    CHECK(p[3] == (FretPosition { 3, 5 }) && p[4] == (FretPosition { 4, 1 }));

    // fret limit: E6 (88) is the 24th fret of the high E, beyond 22 frets
    CHECK(fretPositions(guitar, 22, 88).empty());
    CHECK(fretPositions(guitar, 24, 88).size() == 1);
    CHECK(fretPositions(guitar, 22, 39).empty());  // below the lowest string

    // drop D: the low D is now playable open
    const std::vector<int> dropD = { 38, 45, 50, 55, 59, 64 };
    p = fretPositions(dropD, 22, 38);
    CHECK(p.size() == 1 && p[0] == (FretPosition { 0, 0 }));
    p = fretPositions(dropD, 22, 40);  // E2 = 2nd fret in drop D
    CHECK(p.size() == 1 && p[0] == (FretPosition { 0, 2 }));

    // DADGAD: A3 (57) is the open 5th string, 2nd fret on G, 7th on D, 12th on A, 19th on low D
    p = fretPositions({ 38, 45, 50, 55, 57, 62 }, 22, 57);
    CHECK(p.size() == 5 && p[4] == (FretPosition { 4, 0 }) && p[3] == (FretPosition { 3, 2 }));

    // 5-string bass: low B open, and E1 on the B string 5th fret and the open E
    const std::vector<int> bass5 = { 23, 28, 33, 38, 43 };
    p = fretPositions(bass5, 21, 23);
    CHECK(p.size() == 1 && p[0] == (FretPosition { 0, 0 }));
    p = fretPositions(bass5, 21, 28);
    CHECK(p.size() == 2 && p[0] == (FretPosition { 0, 5 }) && p[1] == (FretPosition { 1, 0 }));

    // 4-string bass: G2 (43) open G, 5th fret D, 10th A, 15th E
    p = fretPositions({ 28, 33, 38, 43 }, 20, 43);
    CHECK(p.size() == 4 && p[0] == (FretPosition { 0, 15 }) && p[3] == (FretPosition { 3, 0 }));
}

static void testParsing()
{
    CHECK(parseNote("C4") == 60);
    CHECK(parseNote("A4") == 69);
    CHECK(parseNote("c#4") == 61);
    CHECK(parseNote("Bb2") == 46);
    CHECK(parseNote(" E2 ") == 40);
    CHECK(parseNote("B0") == 23);
    CHECK(parseNote("C-1") == 0);
    CHECK(parseNote("G9") == 127);
    CHECK(parseNote("Cb4") == 59);
    CHECK(parseNote("G#9") == -1);   // above MIDI range
    CHECK(parseNote("H2") == -1);
    CHECK(parseNote("E") == -1);     // octave required
    CHECK(parseNote("E2x") == -1);
    CHECK(parseNote("") == -1);
    CHECK(parseNote("E99999") == -1);

    auto t = parseTuning("E2 A2 D3 G3 B3 E4");
    CHECK(t == (std::vector<int> { 40, 45, 50, 55, 59, 64 }));
    t = parseTuning("D2, A2, D3, F#3, A3, D4");
    CHECK(t == (std::vector<int> { 38, 45, 50, 54, 57, 62 }));
    // 7-string guitar
    CHECK(parseTuning("B1 E2 A2 D3 G3 B3 E4").size() == 7);
    CHECK(parseTuning("").empty());
    CHECK(parseTuning("E2 A2 X3").empty());
    CHECK(parseTuning("C1 C1 C1 C1 C1 C1 C1 C1 C1 C1 C1 C1 C1").empty());  // 13 strings

    CHECK(formatTuning({ 38, 45, 50, 54, 57, 62 }) == "D2 A2 D3 F#3 A3 D4");
    CHECK(formatTuning({ 39, 44 }, true) == "Eb2 Ab2");
}

static void testTranspositions()
{
    auto & list = getTranspositions();
    CHECK(!list.empty() && list[0].semitones == 0);
    bool hasBb = false, hasEb = false, hasOctave = false;
    for (auto & t : list) {
        if (t.semitones == 2) hasBb = true;
        if (t.semitones == 9) hasEb = true;
        if (t.semitones == 12) hasOctave = true;
    }
    CHECK(hasBb && hasEb && hasOctave);
}

void runInstrumentsTests()
{
    testInstrumentData();
    testFretPositions();
    testParsing();
    testTranspositions();
}
