// SPDX-License-Identifier: GPLv3-or-later WITH Appstore-exception
// Copyright (C) 2026 Tin Can Studios

#pragma once

#include "JuceHeader.h"
#include "TinCanNotes.h"

#include <functional>
#include <vector>

class SonobusAudioProcessor;

// Sharing keys in a lesson (T-074).
//
// One place reads the notes from this computer's MIDI input (the processor's note tap) and hands
// them to the on-screen instrument. When "Share my keys" is on, the same notes, and the ones clicked
// on the screen, also go to everyone in the lesson (NoteWire in TinCanNotes.h, on the lesson's
// private peer channel). Notes from the others are always received and shown in their own colour
// unless "Hide" is chosen. They only light keys: the sound already comes through the lesson audio.
namespace TinCan {

class NoteShare : private Timer
{
public:
    struct Listener
    {
        virtual ~Listener() = default;
        // this computer's MIDI input (NoteSource::Midi) and the others' notes (NoteSource::Remote)
        virtual void notesArrived(const std::vector<NoteEvent> & events) = 0;
        // sharing or showing switched on or off, or someone started or stopped sharing
        virtual void noteShareChanged() {}
    };

    explicit NoteShare(SonobusAudioProcessor & processor);
    ~NoteShare() override;

    // set by the editor
    std::function<void(const String & payload)> sendToPeers;      // NoteWire payload, to everyone
    std::function<bool()> canShare;                               // in a lesson with someone else
    std::function<void(const String & who)> onRemoteStarted;      // someone began sharing

    void addListener(Listener * l);
    void removeListener(Listener * l);

    // notes clicked on the on-screen instrument (NoteSource::Local); shared too when sharing
    void playLocal(const NoteEvent & ev);

    void setSharing(bool share);
    bool isSharing() const { return sharing; }

    void setShowingRemote(bool show);
    bool isShowingRemote() const { return showRemote; }

    bool isRemoteSharing() const { return remoteSharing; }
    String getRemoteName() const { return remoteName; }

    // a NoteWire message from someone in the lesson (message thread)
    void receive(const String & from, const String & payload);

    // the lesson ended: stop sharing (nobody left to tell) and forget the others' notes
    void lessonEnded();

private:
    void timerCallback() override;
    void updateTap();
    void deliver(const std::vector<NoteEvent> & events);
    void changed();
    void send(const std::string & payload);
    void flush();
    void forgetRemote();

    SonobusAudioProcessor & processor;
    ListenerList<Listener> listeners;

    bool sharing = false;
    bool showRemote = true;
    bool remoteSharing = false;
    String remoteName;
    int64 lastHeardMs = 0;

    NoteState mine;                  // what we share: MIDI and mouse notes
    std::vector<NoteEvent> pending;  // not sent yet
    int ticks = 0;
    bool tapOn = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NoteShare)
};

}
