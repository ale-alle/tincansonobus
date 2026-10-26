// SPDX-License-Identifier: GPLv3-or-later WITH Appstore-exception
// Copyright (C) 2026 Tin Can Studios

#include "TinCanTest.h"
#include "../Source/TinCanNotes.h"

#include <thread>

using namespace TinCan;

static NoteEvent ev(NoteEvent::Type type, int note = 0, int value = 0, int channel = 0,
                    NoteSource source = NoteSource::Midi)
{
    NoteEvent e;
    e.type = type;
    e.note = (uint8_t) note;
    e.value = (uint8_t) value;
    e.channel = (uint8_t) channel;
    e.source = source;
    return e;
}

static void testDecode()
{
    NoteEvent e;
    const uint8_t on[] = { 0x93, 60, 100 };
    CHECK(decodeMidi(on, 3, NoteSource::Midi, e));
    CHECK(e.type == NoteEvent::NoteOn && e.channel == 3 && e.note == 60 && e.value == 100);

    const uint8_t onZero[] = { 0x90, 61, 0 };  // running-status style note-off
    CHECK(decodeMidi(onZero, 3, NoteSource::Midi, e) && e.type == NoteEvent::NoteOff && e.note == 61);

    const uint8_t off[] = { 0x8F, 62, 40 };
    CHECK(decodeMidi(off, 3, NoteSource::Midi, e) && e.type == NoteEvent::NoteOff && e.channel == 15);

    const uint8_t pedal[] = { 0xB0, 64, 127 };
    CHECK(decodeMidi(pedal, 3, NoteSource::Midi, e) && e.type == NoteEvent::Sustain && e.value == 127);

    const uint8_t allOff[] = { 0xB0, 123, 0 };
    CHECK(decodeMidi(allOff, 3, NoteSource::Midi, e) && e.type == NoteEvent::AllNotesOff);
    const uint8_t soundOff[] = { 0xB0, 120, 0 };
    CHECK(decodeMidi(soundOff, 3, NoteSource::Midi, e) && e.type == NoteEvent::AllSoundOff);
    const uint8_t resetCtl[] = { 0xB0, 121, 0 };
    CHECK(decodeMidi(resetCtl, 3, NoteSource::Midi, e) && e.type == NoteEvent::Sustain && e.value == 0);

    const uint8_t modWheel[] = { 0xB0, 1, 64 };
    CHECK(!decodeMidi(modWheel, 3, NoteSource::Midi, e));
    const uint8_t pitchBend[] = { 0xE0, 0, 64 };
    CHECK(!decodeMidi(pitchBend, 3, NoteSource::Midi, e));
    const uint8_t clock[] = { 0xF8 };
    CHECK(!decodeMidi(clock, 1, NoteSource::Midi, e));
    CHECK(!decodeMidi(nullptr, 3, NoteSource::Midi, e));
}

static void testOnOff()
{
    NoteState s;
    CHECK(s.countSounding() == 0);
    s.apply(ev(NoteEvent::NoteOn, 60, 90));
    CHECK(s.isSounding(60) && s.isHeld(60, NoteSource::Midi) && !s.isSustainedOnly(60));
    CHECK(s.getVelocity(60) == 90);
    CHECK(!s.isSounding(60, NoteSource::Local));
    s.apply(ev(NoteEvent::NoteOff, 60));
    CHECK(!s.isSounding(60) && s.countSounding() == 0);

    // note-off for a note that never started changes nothing
    s.apply(ev(NoteEvent::NoteOff, 64));
    CHECK(s.countSounding() == 0);
}

static void testOverlapAndRepeat()
{
    NoteState s;
    // chord
    s.apply(ev(NoteEvent::NoteOn, 60, 80));
    s.apply(ev(NoteEvent::NoteOn, 64, 80));
    s.apply(ev(NoteEvent::NoteOn, 67, 80));
    CHECK(s.countSounding() == 3);
    s.apply(ev(NoteEvent::NoteOff, 64));
    CHECK(s.isSounding(60) && !s.isSounding(64) && s.isSounding(67));

    // repeated note-on without a note-off: still one note, one note-off ends it
    s.apply(ev(NoteEvent::NoteOn, 60, 30));
    CHECK(s.isSounding(60) && s.getVelocity(60) == 30);
    s.apply(ev(NoteEvent::NoteOff, 60));
    CHECK(!s.isSounding(60));

    // same note on two channels: stops when both are released
    s.apply(ev(NoteEvent::NoteOn, 72, 80, 0));
    s.apply(ev(NoteEvent::NoteOn, 72, 80, 5));
    s.apply(ev(NoteEvent::NoteOff, 72, 0, 0));
    CHECK(s.isSounding(72));
    s.apply(ev(NoteEvent::NoteOff, 72, 0, 5));
    CHECK(!s.isSounding(72));

    // same note from MIDI and from the mouse: sources are separate
    s.apply(ev(NoteEvent::NoteOn, 48, 80, 0, NoteSource::Midi));
    s.apply(ev(NoteEvent::NoteOn, 48, 80, 0, NoteSource::Local));
    s.apply(ev(NoteEvent::NoteOff, 48, 0, 0, NoteSource::Midi));
    CHECK(s.isSounding(48) && s.isSounding(48, NoteSource::Local) && !s.isSounding(48, NoteSource::Midi));
}

static void testSustain()
{
    NoteState s;
    s.apply(ev(NoteEvent::NoteOn, 60, 80));
    s.apply(ev(NoteEvent::Sustain, 0, 127));
    CHECK(s.isPedalDown(NoteSource::Midi));
    s.apply(ev(NoteEvent::NoteOff, 60));
    CHECK(s.isSounding(60) && s.isSustainedOnly(60));

    // a note played and released while the pedal is down is sustained too
    s.apply(ev(NoteEvent::NoteOn, 62, 80));
    CHECK(!s.isSustainedOnly(62));
    s.apply(ev(NoteEvent::NoteOff, 62));
    CHECK(s.isSustainedOnly(62));

    // replaying a sustained note makes it held again; pedal up then keeps only held notes
    s.apply(ev(NoteEvent::NoteOn, 60, 70));
    CHECK(s.isSounding(60) && !s.isSustainedOnly(60));
    s.apply(ev(NoteEvent::Sustain, 0, 0));
    CHECK(!s.isPedalDown(NoteSource::Midi));
    CHECK(s.isSounding(60) && !s.isSounding(62));
    s.apply(ev(NoteEvent::NoteOff, 60));
    CHECK(s.countSounding() == 0);

    // the pedal on one channel doesn't sustain notes on another
    s.apply(ev(NoteEvent::Sustain, 0, 127, 1));
    s.apply(ev(NoteEvent::NoteOn, 65, 80, 0));
    s.apply(ev(NoteEvent::NoteOff, 65, 0, 0));
    CHECK(!s.isSounding(65));
    // half-pedal values below 64 count as up
    s.apply(ev(NoteEvent::Sustain, 0, 63, 2));
    s.apply(ev(NoteEvent::NoteOn, 66, 80, 2));
    s.apply(ev(NoteEvent::NoteOff, 66, 0, 2));
    CHECK(!s.isSounding(66));
}

static void testAllOffAndReset()
{
    NoteState s;
    s.apply(ev(NoteEvent::NoteOn, 60, 80));
    s.apply(ev(NoteEvent::NoteOn, 64, 80));
    s.apply(ev(NoteEvent::NoteOn, 67, 80, 1));
    s.apply(ev(NoteEvent::AllNotesOff, 0, 0, 0));
    CHECK(!s.isSounding(60) && !s.isSounding(64));
    CHECK(s.isSounding(67));  // other channel untouched

    // all-notes-off with the pedal down behaves like note-offs: notes stay sustained
    s.apply(ev(NoteEvent::Sustain, 0, 127, 1));
    s.apply(ev(NoteEvent::AllNotesOff, 0, 0, 1));
    CHECK(s.isSustainedOnly(67));
    // all-sound-off silences regardless of the pedal
    s.apply(ev(NoteEvent::AllSoundOff, 0, 0, 1));
    CHECK(!s.isSounding(67));

    // reset clears one source only
    s.apply(ev(NoteEvent::NoteOn, 50, 80, 0, NoteSource::Midi));
    s.apply(ev(NoteEvent::NoteOn, 52, 80, 0, NoteSource::Local));
    s.apply(ev(NoteEvent::Sustain, 0, 127, 3, NoteSource::Midi));
    s.apply(ev(NoteEvent::Reset, 0, 0, 0, NoteSource::Midi));
    CHECK(!s.isSounding(50) && s.isSounding(52) && !s.isPedalDown(NoteSource::Midi));
    s.clear();
    CHECK(s.countSounding() == 0);

    // out-of-range queries are safe
    CHECK(!s.isSounding(-1) && !s.isSounding(128) && s.getVelocity(200) == 0);
}

static void testNames()
{
    CHECK(noteLabel(60) == "C4");
    CHECK(noteLabel(69) == "A4");
    CHECK(noteLabel(61) == "C#4");
    CHECK(noteLabel(61, true) == "Db4");
    CHECK(noteLabel(70, true) == "Bb4");
    CHECK(noteLabel(21) == "A0");    // lowest piano key
    CHECK(noteLabel(108) == "C8");   // highest piano key
    CHECK(noteLabel(0) == "C-1");
    CHECK(noteLabel(127) == "G9");
    CHECK(noteLabel(60, false, 3) == "C3");  // Yamaha-style middle C
    CHECK(noteLabel(-1) == "B-2");           // below MIDI range after transposing
    CHECK(noteName(-3) == "A");
}

static void testQueue()
{
    SpscQueue<NoteEvent, 8> q;
    NoteEvent out;
    CHECK(!q.pop(out));
    for (int i = 0; i < 8; ++i) CHECK(q.push(ev(NoteEvent::NoteOn, i, 1)));
    CHECK(!q.push(ev(NoteEvent::NoteOn, 99, 1)));  // full: dropped, not overwritten
    for (int i = 0; i < 8; ++i) {
        CHECK(q.pop(out) && out.note == i);
    }
    CHECK(!q.pop(out));

    // one producer thread, one consumer: everything arrives once, in order
    SpscQueue<int, 256> iq;
    constexpr int total = 200000;
    std::thread producer([&iq, total]() {
        for (int i = 0; i < total; ) {
            if (iq.push(i)) ++i;
        }
    });
    int expected = 0;
    bool inOrder = true;
    while (expected < total) {
        int v;
        if (iq.pop(v)) {
            if (v != expected) inOrder = false;
            ++expected;
        }
    }
    producer.join();
    CHECK(inOrder);
    int v;
    CHECK(!iq.pop(v));
}

void runNotesTests()
{
    testDecode();
    testOnOff();
    testOverlapAndRepeat();
    testSustain();
    testAllOffAndReset();
    testNames();
    testQueue();
}
