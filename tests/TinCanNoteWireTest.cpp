// SPDX-License-Identifier: GPLv3-or-later WITH Appstore-exception
// Copyright (C) 2026 Tin Can Studios

#include "TinCanTest.h"
#include "../Source/TinCanNotes.h"

using namespace TinCan;

static NoteEvent ev(NoteEvent::Type t, int note, int value = 0, int channel = 0, NoteSource src = NoteSource::Midi)
{
    NoteEvent e;
    e.type = t; e.note = (uint8_t) note; e.value = (uint8_t) value; e.channel = (uint8_t) channel; e.source = src;
    return e;
}

static NoteState applyAll(const std::vector<NoteEvent> & events, NoteState st = {})
{
    for (auto & e : events) st.apply(e);
    return st;
}

void runNoteWireTests()
{
    std::vector<NoteEvent> out;

    // events round trip, arriving as the other person's notes
    NoteEvent sent[] = { ev(NoteEvent::NoteOn, 60, 100, 0), ev(NoteEvent::Sustain, 0, 127, 2), ev(NoteEvent::NoteOff, 127, 0, 15) };
    auto wire = NoteWire::encodeEvents(sent, 3);
    CHECK(wire == "e:003c642200" "7f" "1f7f00");
    CHECK(NoteWire::decode(wire, out) && out.size() == 3);
    CHECK(out[0].type == NoteEvent::NoteOn && out[0].note == 60 && out[0].value == 100 && out[0].source == NoteSource::Remote);
    CHECK(out[1].type == NoteEvent::Sustain && out[1].channel == 2 && out[1].value == 127);
    CHECK(out[2].type == NoteEvent::NoteOff && out[2].note == 127 && out[2].channel == 15);

    // at most MaxEvents per message
    std::vector<NoteEvent> many(100, ev(NoteEvent::NoteOn, 60, 1));
    CHECK(NoteWire::encodeEvents(many.data(), 100).size() == 2 + 6 * (size_t) NoteWire::MaxEvents);

    // snapshot: held, pedal-held and pedal state come back on the other side
    NoteState mine;
    mine.apply(ev(NoteEvent::Sustain, 0, 127));
    mine.apply(ev(NoteEvent::NoteOn, 48, 90));
    mine.apply(ev(NoteEvent::NoteOff, 48));               // held only by the pedal
    mine.apply(ev(NoteEvent::NoteOn, 64, 90, 0, NoteSource::Local));
    auto snap = NoteWire::encodeSnapshot(mine);
    CHECK(snap == "s:1|40|30");
    CHECK(NoteWire::decode(snap, out));
    NoteState theirs;
    theirs.apply(ev(NoteEvent::NoteOn, 70, 90, 0, NoteSource::Remote));   // stuck from a lost note-off
    theirs.apply(ev(NoteEvent::NoteOn, 72, 90));                          // their own MIDI stays
    theirs = applyAll(out, theirs);
    CHECK(theirs.isSounding(64, NoteSource::Remote) && theirs.isHeld(64, NoteSource::Remote));
    CHECK(theirs.isSounding(48, NoteSource::Remote) && !theirs.isHeld(48, NoteSource::Remote));
    CHECK(!theirs.isSounding(70, NoteSource::Remote));
    CHECK(theirs.isSounding(72, NoteSource::Midi));
    CHECK(theirs.isPedalDown(NoteSource::Remote));

    // nothing held, pedal up
    CHECK(NoteWire::encodeSnapshot(NoteState()) == "s:0||");
    CHECK(NoteWire::decode("s:0||", out) && out.size() == 1 && out[0].type == NoteEvent::Reset);

    // stop
    CHECK(NoteWire::decode(NoteWire::encodeStop(), out) && out.size() == 1 && out[0].type == NoteEvent::Reset
          && out[0].source == NoteSource::Remote);

    // anything malformed is ignored as a whole
    const char * bad[] = { "", "e:", "e:003c6", "e:003c6g", "e:00ff64", "e:003cff", "e:903c64", "E:003c64",
                           "s:", "s:2||", "s:1|4|", "s:1|80|", "s:1|40", "s:1|40|30|", "s:0|zz|", "y", "x " };
    for (auto * b : bad) {
        out.push_back(ev(NoteEvent::NoteOn, 1));
        CHECK(!NoteWire::decode(b, out) && out.empty());
    }
    std::string tooLong = "e:" + std::string(6 * (NoteWire::MaxEvents + 1), '0');
    CHECK(!NoteWire::decode(tooLong, out));
    CHECK(!NoteWire::decode(std::string(NoteWire::MaxPayload + 1, 'x'), out));
}
