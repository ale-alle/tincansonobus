// SPDX-License-Identifier: GPLv3-or-later WITH Appstore-exception
// Copyright (C) 2026 Tin Can Studios

#include "TinCanInstruments.h"
#include "TinCanNotes.h"

#include <cctype>
#include <sstream>

namespace TinCan {

const std::vector<InstrumentDef> & getInstruments()
{
    static const std::vector<InstrumentDef> instruments = {
        { "Piano", InstrumentDef::Keyboard, 0, {} },
        { "Guitar", InstrumentDef::Fretted, 22, {
            { "Standard (E A D G B E)", { 40, 45, 50, 55, 59, 64 } },
            { "Drop D (D A D G B E)",   { 38, 45, 50, 55, 59, 64 } },
            { "Half step down (Eb)",    { 39, 44, 49, 54, 58, 63 } },
            { "DADGAD",                 { 38, 45, 50, 55, 57, 62 } },
            { "Open G (D G D G B D)",   { 38, 43, 50, 55, 59, 62 } },
            { "Open D (D A D F# A D)",  { 38, 45, 50, 54, 57, 62 } },
        } },
        { "Bass (4 strings)", InstrumentDef::Fretted, 20, {
            { "Standard (E A D G)", { 28, 33, 38, 43 } },
            { "Drop D (D A D G)",   { 26, 33, 38, 43 } },
            { "Half step down (Eb)", { 27, 32, 37, 42 } },
        } },
        { "Bass (5 strings)", InstrumentDef::Fretted, 21, {
            { "Standard (B E A D G)", { 23, 28, 33, 38, 43 } },
        } },
    };
    return instruments;
}

std::vector<FretPosition> fretPositions(const std::vector<int> & strings, int frets, int note)
{
    std::vector<FretPosition> out;
    for (int s = 0; s < (int) strings.size(); ++s) {
        int fret = note - strings[(size_t) s];
        if (fret >= 0 && fret <= frets) out.push_back({ s, fret });
    }
    return out;
}

int parseNote(const std::string & text)
{
    static const int letterPc[7] = { 9, 11, 0, 2, 4, 5, 7 };  // A B C D E F G
    size_t i = 0;
    while (i < text.size() && std::isspace((unsigned char) text[i])) ++i;
    if (i >= text.size()) return -1;

    char letter = (char) std::toupper((unsigned char) text[i++]);
    if (letter < 'A' || letter > 'G') return -1;
    int pc = letterPc[letter - 'A'];

    if (i < text.size() && (text[i] == '#' || text[i] == 'b')) {
        pc += text[i] == '#' ? 1 : -1;
        ++i;
    }

    bool negative = false;
    if (i < text.size() && text[i] == '-') { negative = true; ++i; }
    if (i >= text.size() || !std::isdigit((unsigned char) text[i])) return -1;
    int octave = 0;
    while (i < text.size() && std::isdigit((unsigned char) text[i])) {
        octave = octave * 10 + (text[i++] - '0');
        if (octave > 10) return -1;
    }
    while (i < text.size() && std::isspace((unsigned char) text[i])) ++i;
    if (i != text.size()) return -1;

    if (negative) octave = -octave;
    int note = (octave + 1) * 12 + pc;
    return (note < 0 || note > 127) ? -1 : note;
}

std::vector<int> parseTuning(const std::string & text)
{
    std::string spaced = text;
    for (auto & c : spaced) {
        if (c == ',') c = ' ';
    }
    std::istringstream in(spaced);
    std::vector<int> strings;
    std::string token;
    while (in >> token) {
        int note = parseNote(token);
        if (note < 0) return {};
        strings.push_back(note);
    }
    if (strings.empty() || strings.size() > 12) return {};
    return strings;
}

std::string formatTuning(const std::vector<int> & strings, bool useFlats)
{
    std::string out;
    for (auto note : strings) {
        if (!out.empty()) out += ' ';
        out += noteLabel(note, useFlats);
    }
    return out;
}

const std::vector<Transposition> & getTranspositions()
{
    static const std::vector<Transposition> list = {
        { "Concert pitch", 0 },
        { "Bb (clarinet, trumpet, soprano sax)", 2 },
        { "Bb + octave (tenor sax, bass clarinet)", 14 },
        { "Eb (alto sax)", 9 },
        { "Eb + octave (baritone sax)", 21 },
        { "F (horn)", 7 },
        { "A (clarinet in A)", 3 },
        { "Octave (guitar, bass)", 12 },
    };
    return list;
}

}
