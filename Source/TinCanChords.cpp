// SPDX-License-Identifier: GPLv3-or-later WITH Appstore-exception
// Copyright (C) 2026 Tin Can Studios

#include "TinCanChords.h"
#include "TinCanNotes.h"

#include <climits>
#include <initializer_list>

namespace TinCan {

namespace {

struct Shape
{
    const char * suffix;
    unsigned mask;   // intervals above the root, bit n = n semitones
};

constexpr unsigned bits(std::initializer_list<int> intervals)
{
    unsigned m = 0;
    for (int i : intervals) m |= 1u << i;
    return m;
}

// simplest first: on a tie the earlier shape wins
const Shape shapes[] = {
    { "",         bits({ 0, 4, 7 }) },
    { "m",        bits({ 0, 3, 7 }) },
    { "dim",      bits({ 0, 3, 6 }) },
    { "aug",      bits({ 0, 4, 8 }) },
    { "sus4",     bits({ 0, 5, 7 }) },
    { "sus2",     bits({ 0, 2, 7 }) },
    { "7",        bits({ 0, 4, 7, 10 }) },
    { "maj7",     bits({ 0, 4, 7, 11 }) },
    { "m7",       bits({ 0, 3, 7, 10 }) },
    { "6",        bits({ 0, 4, 7, 9 }) },
    { "m6",       bits({ 0, 3, 7, 9 }) },
    { "m7b5",     bits({ 0, 3, 6, 10 }) },
    { "dim7",     bits({ 0, 3, 6, 9 }) },
    { "m(maj7)",  bits({ 0, 3, 7, 11 }) },
    { "7sus4",    bits({ 0, 5, 7, 10 }) },
    { "7#5",      bits({ 0, 4, 8, 10 }) },
    { "maj7#5",   bits({ 0, 4, 8, 11 }) },
    { "add9",     bits({ 0, 2, 4, 7 }) },
    { "m(add9)",  bits({ 0, 2, 3, 7 }) },
    { "9",        bits({ 0, 2, 4, 7, 10 }) },
    { "maj9",     bits({ 0, 2, 4, 7, 11 }) },
    { "m9",       bits({ 0, 2, 3, 7, 10 }) },
    { "6/9",      bits({ 0, 2, 4, 7, 9 }) },
    { "7b9",      bits({ 0, 1, 4, 7, 10 }) },
    { "7#9",      bits({ 0, 3, 4, 7, 10 }) },
    { "11",       bits({ 0, 2, 4, 5, 7, 10 }) },
    { "m11",      bits({ 0, 2, 3, 5, 7, 10 }) },
    { "13",       bits({ 0, 2, 4, 7, 9, 10 }) },
};

constexpr unsigned fifth = 1u << 7;

int countBits(unsigned m) { int c = 0; for (; m; m &= m - 1) ++c; return c; }

}

Chord identifyChord(const std::vector<int> & notes)
{
    Chord best;
    unsigned pcs = 0;
    int lowest = INT_MAX;
    for (int n : notes) {
        if (n < 0 || n > 127) continue;
        pcs |= 1u << (n % 12);
        if (n < lowest) lowest = n;
    }
    int count = countBits(pcs);
    if (count < 2) return best;
    int bass = lowest % 12;

    // two notes: only a fifth (a power chord) has a usual chord name
    if (count == 2) {
        for (int root = 0; root < 12; ++root) {
            unsigned rel = ((pcs >> root) | (pcs << (12 - root))) & 0xFFFu;
            if (rel == (1u | fifth)) {
                best.valid = true; best.root = root; best.bass = bass; best.suffix = "5";
                return best;
            }
        }
        return best;
    }

    int bestScore = INT_MIN;
    for (int root = 0; root < 12; ++root) {
        if (!(pcs & (1u << root))) continue;
        unsigned rel = ((pcs >> root) | (pcs << (12 - root))) & 0xFFFu;   // intervals above this root
        for (int i = 0; i < (int) (sizeof(shapes) / sizeof(shapes[0])); ++i) {
            const auto & sh = shapes[i];
            bool exact = rel == sh.mask;
            // sevenths and up may leave out the fifth (C E Bb = C7)
            bool noFifth = !exact && countBits(sh.mask) >= 4 && (sh.mask & fifth) && rel == (sh.mask & ~fifth);
            if (!exact && !noFifth) continue;
            int score = (root == bass ? 1000 : 0) - (noFifth ? 50 : 0) - i;
            if (score > bestScore) {
                bestScore = score;
                best.valid = true; best.root = root; best.bass = bass; best.suffix = sh.suffix;
            }
        }
    }
    return best;
}

std::string chordName(const Chord & chord, bool useFlats, int transpose)
{
    if (!chord.valid) return {};
    auto name = [&](int pc) { return noteName(((pc + transpose) % 12 + 12) % 12, useFlats); };
    std::string s = name(chord.root) + chord.suffix;
    if (chord.bass != chord.root) s += "/" + name(chord.bass);
    return s;
}

}
