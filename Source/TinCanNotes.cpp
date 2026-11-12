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

// ---- sharing keys with the other person (T-074) ----

namespace NoteWire {

const char * const Tag = "canduet-notes-1";

static const char * hexDigits = "0123456789abcdef";

static void putHex(std::string & s, unsigned v, int digits)
{
    for (int i = digits - 1; i >= 0; --i) s += hexDigits[(v >> (4 * i)) & 0xF];
}

static bool getHex(const std::string & s, size_t pos, int digits, unsigned & out)
{
    if (pos + (size_t) digits > s.size()) return false;
    out = 0;
    for (int i = 0; i < digits; ++i) {
        char c = s[pos + (size_t) i];
        unsigned d;
        if (c >= '0' && c <= '9') d = (unsigned) (c - '0');
        else if (c >= 'a' && c <= 'f') d = (unsigned) (c - 'a' + 10);
        else return false;
        out = (out << 4) | d;
    }
    return true;
}

std::string encodeEvents(const NoteEvent * events, int count)
{
    std::string s = "e:";
    for (int i = 0; i < count && i < MaxEvents; ++i) {
        const auto & ev = events[i];
        putHex(s, (unsigned) ev.type, 1);
        putHex(s, ev.channel & 0x0Fu, 1);
        putHex(s, ev.note & 0x7Fu, 2);
        putHex(s, ev.value & 0x7Fu, 2);
    }
    return s;
}

std::string encodeSnapshot(const NoteState & state)
{
    bool pedal = false;
    for (int src = 0; src < NumNoteSources; ++src) pedal = pedal || state.isPedalDown((NoteSource) src);
    std::string held, sus;
    for (int n = 0; n < 128; ++n) {
        if (!state.isSounding(n)) continue;
        if (state.isSustainedOnly(n)) putHex(sus, (unsigned) n, 2);
        else putHex(held, (unsigned) n, 2);
    }
    return std::string("s:") + (pedal ? "1" : "0") + "|" + held + "|" + sus;
}

std::string encodeStop()
{
    return "x";
}

static NoteEvent remote(NoteEvent::Type type, int note = 0, int value = 0, int channel = 0)
{
    NoteEvent ev;
    ev.type = type;
    ev.channel = (uint8_t) channel;
    ev.note = (uint8_t) note;
    ev.value = (uint8_t) value;
    ev.source = NoteSource::Remote;
    return ev;
}

// a list of notes, 2 hex digits each, between pos and end
static bool getNotes(const std::string & s, size_t pos, size_t end, std::vector<int> & notes)
{
    if ((end - pos) % 2 != 0 || (end - pos) / 2 > 128) return false;
    for (size_t i = pos; i < end; i += 2) {
        unsigned n;
        if (!getHex(s, i, 2, n) || n > 127) return false;
        notes.push_back((int) n);
    }
    return true;
}

bool decode(const std::string & payload, std::vector<NoteEvent> & out)
{
    out.clear();
    if (payload.empty() || payload.size() > (size_t) MaxPayload) return false;

    if (payload == "x") {
        out.push_back(remote(NoteEvent::Reset));
        return true;
    }

    if (payload.compare(0, 2, "e:") == 0) {
        size_t len = payload.size() - 2;
        if (len == 0 || len % 6 != 0 || len / 6 > (size_t) MaxEvents) return false;
        std::vector<NoteEvent> events;
        for (size_t pos = 2; pos < payload.size(); pos += 6) {
            unsigned type, channel, note, value;
            if (!getHex(payload, pos, 1, type) || !getHex(payload, pos + 1, 1, channel)
                || !getHex(payload, pos + 2, 2, note) || !getHex(payload, pos + 4, 2, value)) return false;
            if (type > (unsigned) NoteEvent::Reset || note > 127 || value > 127) return false;
            events.push_back(remote((NoteEvent::Type) type, (int) note, (int) value, (int) channel));
        }
        out = std::move(events);
        return true;
    }

    if (payload.compare(0, 2, "s:") == 0) {
        // s:<p>|<held>|<sus>
        if (payload.size() < 5 || (payload[2] != '0' && payload[2] != '1') || payload[3] != '|') return false;
        size_t bar = payload.find('|', 4);
        if (bar == std::string::npos || payload.find('|', bar + 1) != std::string::npos) return false;
        std::vector<int> held, sus;
        if (!getNotes(payload, 4, bar, held) || !getNotes(payload, bar + 1, payload.size(), sus)) return false;
        bool pedal = payload[2] == '1';

        std::vector<NoteEvent> events;
        events.push_back(remote(NoteEvent::Reset));
        if (pedal || !sus.empty()) events.push_back(remote(NoteEvent::Sustain, 0, 127));
        for (int n : sus) {                       // pressed and let go while the pedal is down
            events.push_back(remote(NoteEvent::NoteOn, n, 64));
            events.push_back(remote(NoteEvent::NoteOff, n));
        }
        for (int n : held) events.push_back(remote(NoteEvent::NoteOn, n, 64));
        out = std::move(events);
        return true;
    }
    return false;
}

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
