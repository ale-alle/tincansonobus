// SPDX-License-Identifier: GPLv3-or-later WITH Appstore-exception
// Copyright (C) 2026 Tin Can Studios

#pragma once

#include <atomic>
#include <bitset>
#include <cstdint>
#include <string>
#include <vector>

// Tin Can note model for the teaching tools (T-034, T-035).
//
// MIDI from the audio thread becomes small NoteEvents in a lock-free queue; the UI drains the
// queue into a NoteState, which every instrument view (keyboard, fretboard) draws from.
// No JUCE here so the logic can be unit tested on its own (tests/TinCanNotesTest.cpp).
namespace TinCan {

// who played a note: an external MIDI input (keyboard, or another app through a loopback port),
// the mouse on the on-screen instrument, or the other person in the lesson (T-074, NoteWire below)
enum class NoteSource : uint8_t { Midi = 0, Local = 1, Remote = 2 };
constexpr int NumNoteSources = 3;

struct NoteEvent
{
    enum Type : uint8_t {
        NoteOn,       // note, value = velocity 1-127
        NoteOff,      // note
        Sustain,      // value >= 64 is pedal down
        AllNotesOff,  // CC 123: like a note-off for every held note on the channel
        AllSoundOff,  // CC 120: silence the channel, pedal or not
        Reset         // forget everything from this source (device change, window closed)
    };

    Type type = NoteOff;
    uint8_t channel = 0;  // 0-15
    uint8_t note = 0;     // 0-127
    uint8_t value = 0;
    NoteSource source = NoteSource::Midi;
};

// raw MIDI bytes → NoteEvent; false for messages the visualizer ignores
bool decodeMidi(const uint8_t * data, int size, NoteSource source, NoteEvent & out);

// Which notes are sounding, per source. A note is sounding while its key is held, or after its
// key was released while that channel's sustain pedal is down. A repeated note-on without a
// note-off keeps the note on (one note-off releases it), matching JUCE's MidiKeyboardState.
// The same note on two channels is tracked separately: it stops when both are released.
class NoteState
{
public:
    void apply(const NoteEvent & ev);
    void clear();

    bool isSounding(int note) const;                    // any source
    bool isSounding(int note, NoteSource source) const;
    bool isHeld(int note, NoteSource source) const;     // key down on some channel
    bool isSustainedOnly(int note) const;               // sounding only because of the pedal
    bool isPedalDown(NoteSource source) const;          // on any channel
    uint8_t getVelocity(int note) const;                // last note-on velocity, 0 if never
    int countSounding() const;

private:
    static int idx(NoteSource s) { return (int) s; }

    std::bitset<128> held[NumNoteSources][16];
    std::bitset<128> sustained[NumNoteSources][16];
    bool pedal[NumNoteSources][16] = {};
    uint8_t velocity[128] = {};
};

// Sharing your keys with the other person in a lesson (T-074). Notes travel as small text
// messages on the lesson's private peer channel, tagged NoteWire::Tag. They only light keys on the
// other side: the sound itself already arrives through the lesson audio.
//   "e:<events>"          notes as they happen: 6 hex digits each, type/channel/note/value
//   "s:<p>|<held>|<sus>"  about once a second: pedal (0/1), then held and pedal-held notes (2 hex
//                         digits each). The receiver replaces what it shows, so a lost message
//                         never leaves a key stuck.
//   "x"                   sharing stopped: forget everything
namespace NoteWire {
    extern const char * const Tag;   // "canduet-notes-1"
    constexpr int MaxEvents = 64;    // per message; more are sent in the next one
    constexpr int MaxPayload = 1024;

    std::string encodeEvents(const NoteEvent * events, int count);
    // snapshot of everything this state shows, from any source, as one player
    std::string encodeSnapshot(const NoteState & state);
    std::string encodeStop();

    // the events that bring the receiver's NoteSource::Remote notes up to date; false (and nothing
    // in out) for anything malformed
    bool decode(const std::string & payload, std::vector<NoteEvent> & out);
}

// note names, concert pitch; middle C (60) is "C4" with the default middleCOctave
std::string noteName(int note, bool useFlats = false);            // "C#" / "Db"
int noteOctave(int note, int middleCOctave = 4);
std::string noteLabel(int note, bool useFlats = false, int middleCOctave = 4);  // "C#4"

// Single-producer single-consumer queue with a fixed size: push from the audio thread, pop on
// the message thread. Never allocates or blocks; push fails (and the event is dropped) when full.
template <typename T, int Capacity>
class SpscQueue
{
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of two");

public:
    bool push(const T & item)
    {
        auto w = writePos.load(std::memory_order_relaxed);
        if (w - readPos.load(std::memory_order_acquire) >= (uint32_t) Capacity) return false;
        items[w & (Capacity - 1)] = item;
        writePos.store(w + 1, std::memory_order_release);
        return true;
    }

    bool pop(T & item)
    {
        auto r = readPos.load(std::memory_order_relaxed);
        if (r == writePos.load(std::memory_order_acquire)) return false;
        item = items[r & (Capacity - 1)];
        readPos.store(r + 1, std::memory_order_release);
        return true;
    }

private:
    T items[Capacity];
    std::atomic<uint32_t> writePos { 0 };
    std::atomic<uint32_t> readPos { 0 };
};

}
