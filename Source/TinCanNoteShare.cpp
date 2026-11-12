// SPDX-License-Identifier: GPLv3-or-later WITH Appstore-exception
// Copyright (C) 2026 Tin Can Studios

#include "TinCanNoteShare.h"
#include "SonobusPluginProcessor.h"

namespace TinCan {

static constexpr int tickHz = 30;
static constexpr int snapshotTicks = tickHz;        // a full picture of the held keys once a second
static constexpr int64 remoteTimeoutMs = 4000;      // three missed snapshots: they stopped or left

NoteShare::NoteShare(SonobusAudioProcessor & p) : processor(p)
{
}

NoteShare::~NoteShare()
{
    stopTimer();
    processor.setNoteTapEnabled(false);
}

void NoteShare::addListener(Listener * l)
{
    listeners.add(l);
    updateTap();
}

void NoteShare::removeListener(Listener * l)
{
    listeners.remove(l);
    updateTap();
}

void NoteShare::updateTap()
{
    // read the MIDI input only while someone uses it: the instrument is open, or we're sharing
    bool needed = !listeners.isEmpty() || sharing;
    if (needed && !tapOn) {
        NoteEvent stale;
        while (processor.popNoteEvent(stale)) {}   // nothing from before
        NoteEvent reset;                            // MIDI keys held back then were never seen let go
        reset.type = NoteEvent::Reset;
        reset.source = NoteSource::Midi;
        mine.apply(reset);
        processor.setNoteTapEnabled(true);
        tapOn = true;
    }
    else if (!needed && tapOn) {
        processor.setNoteTapEnabled(false);
        tapOn = false;
    }
    // the timer also watches for the others going quiet, so it runs while their notes are shown
    if (needed || remoteSharing) { if (!isTimerRunning()) startTimerHz(tickHz); }
    else stopTimer();
}

void NoteShare::deliver(const std::vector<NoteEvent> & events)
{
    if (events.empty()) return;
    listeners.call([&events](Listener & l) { l.notesArrived(events); });
}

void NoteShare::changed()
{
    listeners.call([](Listener & l) { l.noteShareChanged(); });
}

void NoteShare::send(const std::string & payload)
{
    if (sendToPeers) sendToPeers(String(payload));
}

void NoteShare::flush()
{
    for (size_t i = 0; i < pending.size(); i += (size_t) NoteWire::MaxEvents) {
        int count = (int) jmin(pending.size() - i, (size_t) NoteWire::MaxEvents);
        send(NoteWire::encodeEvents(pending.data() + i, count));
    }
    pending.clear();
}

void NoteShare::playLocal(const NoteEvent & ev)
{
    mine.apply(ev);
    if (sharing && pending.size() < 1024) pending.push_back(ev);
}

void NoteShare::setSharing(bool share)
{
    if (share && canShare && !canShare()) share = false;
    if (share == sharing) return;
    sharing = share;
    pending.clear();
    if (sharing) {
        ticks = snapshotTicks;   // send what's held right away
    }
    else {
        send(NoteWire::encodeStop());
    }
    updateTap();
    changed();
}

void NoteShare::setShowingRemote(bool show)
{
    if (show == showRemote) return;
    showRemote = show;
    if (!show) {
        // clear their keys now; they come back with the next snapshot when shown again
        NoteEvent reset;
        reset.type = NoteEvent::Reset;
        reset.source = NoteSource::Remote;
        deliver({ reset });
    }
    changed();
}

void NoteShare::forgetRemote()
{
    bool was = remoteSharing;
    remoteSharing = false;
    NoteEvent reset;
    reset.type = NoteEvent::Reset;
    reset.source = NoteSource::Remote;
    deliver({ reset });
    if (was) changed();
    updateTap();
}

void NoteShare::receive(const String & from, const String & payload)
{
    std::vector<NoteEvent> events;
    if (!NoteWire::decode(payload.toStdString(), events)) return;

    if (payload == NoteWire::encodeStop().c_str()) {
        if (from == remoteName || remoteName.isEmpty()) forgetRemote();
        return;
    }

    bool started = !remoteSharing || from != remoteName;
    remoteSharing = true;
    remoteName = from.substring(0, 80);
    lastHeardMs = Time::currentTimeMillis();
    if (started) {
        changed();
        if (onRemoteStarted) onRemoteStarted(remoteName);
        updateTap();
    }
    if (showRemote) deliver(events);
}

void NoteShare::lessonEnded()
{
    sharing = false;
    pending.clear();
    remoteName.clear();
    forgetRemote();
    changed();
    updateTap();
}

void NoteShare::timerCallback()
{
    // this computer's MIDI input
    std::vector<NoteEvent> local;
    NoteEvent ev;
    while (tapOn && local.size() < 1024 && processor.popNoteEvent(ev)) {
        local.push_back(ev);
        mine.apply(ev);
        if (sharing && pending.size() < 1024) pending.push_back(ev);
    }
    deliver(local);

    if (sharing) {
        if (canShare && !canShare()) {
            // the lesson ended or everyone left: nothing to share with
            sharing = false;
            pending.clear();
            changed();
            updateTap();
        }
        else {
            flush();
            if (++ticks >= snapshotTicks) {
                ticks = 0;
                send(NoteWire::encodeSnapshot(mine));
            }
        }
    }

    if (remoteSharing && Time::currentTimeMillis() - lastHeardMs > remoteTimeoutMs) forgetRemote();
}

}
