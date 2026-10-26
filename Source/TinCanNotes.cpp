// SPDX-License-Identifier: GPLv3-or-later WITH Appstore-exception
// Copyright (C) 2026 Tin Can Studios

#include "TinCanNotes.h"

namespace TinCan {

bool decodeMidi(const uint8_t * data, int size, NoteSource source, NoteEvent & out)
{
    if (data == nullptr || size < 3) return false;

    uint8_t status = data[0] & 0xF0;
    out.channel = data[0] & 0x0F;
    out.source = source;
    out.note = 0;
    out.value = 0;

    if (status == 0x90 && data[2] > 0) {
        out.type = NoteEvent::NoteOn;
        out.note = data[1] & 0x7F;
        out.value = data[2] & 0x7F;
        return true;
    }
    if (status == 0x80 || status == 0x90) {   // note-on with velocity 0 is a note-off
        out.type = NoteEvent::NoteOff;
        out.note = data[1] & 0x7F;
        return true;
    }
    if (status == 0xB0) {
        switch (data[1]) {
            case 64:  out.type = NoteEvent::Sustain; out.value = data[2] & 0x7F; return true;
            case 120: out.type = NoteEvent::AllSoundOff; return true;
            case 121: out.type = NoteEvent::Sustain; out.value = 0; return true;  // reset all controllers
            case 123: out.type = NoteEvent::AllNotesOff; return true;
            default: return false;
        }
    }
    return false;
}

void NoteState::apply(const NoteEvent & ev)
{
    int s = idx(ev.source);
    if (s < 0 || s >= NumNoteSources) return;
    int ch = ev.channel & 0x0F;
    int n = ev.note & 0x7F;

    switch (ev.type) {
        case NoteEvent::NoteOn:
            held[s][ch].set(n);
            sustained[s][ch].reset(n);
            velocity[n] = ev.value;
            break;
        case NoteEvent::NoteOff:
            if (held[s][ch].test(n) && pedal[s][ch]) sustained[s][ch].set(n);
            held[s][ch].reset(n);
            break;
        case NoteEvent::Sustain:
            pedal[s][ch] = ev.value >= 64;
            if (!pedal[s][ch]) sustained[s][ch].reset();
            break;
        case NoteEvent::AllNotesOff:
            if (pedal[s][ch]) sustained[s][ch] |= held[s][ch];
            held[s][ch].reset();
            break;
        case NoteEvent::AllSoundOff:
            held[s][ch].reset();
            sustained[s][ch].reset();
            break;
        case NoteEvent::Reset:
            for (int c = 0; c < 16; ++c) {
                held[s][c].reset();
                sustained[s][c].reset();
                pedal[s][c] = false;
            }
            break;
    }
}

void NoteState::clear()
{
    for (int s = 0; s < NumNoteSources; ++s) {
        NoteEvent ev;
        ev.type = NoteEvent::Reset;
        ev.source = (NoteSource) s;
        apply(ev);
    }
}

bool NoteState::isSounding(int note, NoteSource source) const
{
    if (note < 0 || note > 127) return false;
    int s = idx(source);
    for (int c = 0; c < 16; ++c) {
        if (held[s][c].test(note) || sustained[s][c].test(note)) return true;
    }
    return false;
}

bool NoteState::isSounding(int note) const
{
    for (int s = 0; s < NumNoteSources; ++s) {
        if (isSounding(note, (NoteSource) s)) return true;
    }
    return false;
}

bool NoteState::isHeld(int note, NoteSource source) const
{
    if (note < 0 || note > 127) return false;
    int s = idx(source);
    for (int c = 0; c < 16; ++c) {
        if (held[s][c].test(note)) return true;
    }
    return false;
}

bool NoteState::isSustainedOnly(int note) const
{
    if (!isSounding(note)) return false;
    for (int s = 0; s < NumNoteSources; ++s) {
        if (isHeld(note, (NoteSource) s)) return false;
    }
    return true;
}

bool NoteState::isPedalDown(NoteSource source) const
{
    for (int c = 0; c < 16; ++c) {
        if (pedal[idx(source)][c]) return true;
    }
    return false;
}

uint8_t NoteState::getVelocity(int note) const
{
    return (note < 0 || note > 127) ? 0 : velocity[note];
}

int NoteState::countSounding() const
{
    int count = 0;
    for (int n = 0; n < 128; ++n) {
        if (isSounding(n)) ++count;
    }
    return count;
}

std::string noteName(int note, bool useFlats)
{
    static const char * sharps[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    static const char * flats[12]  = { "C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B" };
    int pc = ((note % 12) + 12) % 12;
    return useFlats ? flats[pc] : sharps[pc];
}

int noteOctave(int note, int middleCOctave)
{
    // floor division so negative notes (after transposing) still get the right octave
    int oct = note >= 0 ? note / 12 : -((-note + 11) / 12);
    return oct - 5 + middleCOctave;
}

std::string noteLabel(int note, bool useFlats, int middleCOctave)
{
    return noteName(note, useFlats) + std::to_string(noteOctave(note, middleCOctave));
}

}
