// SPDX-License-Identifier: GPLv3-or-later WITH Appstore-exception
// Copyright (C) 2026 Tin Can Studios

#pragma once

#include "JuceHeader.h"

#include <functional>
#include <map>

// Lesson recording with consent, highlights and summaries (T-045, T-046). See doc/TINCAN-RECORDING.md.
//
// Nobody is recorded without saying yes: pressing Record in a lesson asks everyone else first,
// and every computer records its own copy only once all have agreed. Anyone can stop it, and
// stopping stops it for everyone. While it records, anyone can press Mark to add a highlight;
// highlights are shared, so both copies get the same list. When it stops, each computer saves a
// summary (who, when, how long, highlights) next to its own recording. Nothing is uploaded.
//
// The messages travel on the lesson's encrypted peer chat channel with their own tag, and never
// appear in the chat. This file has no UI and no audio code, so it can be tested on its own.
namespace TinCan {

// chat tag that marks a recording control message
extern const char * const kRecordingTag;
inline bool isRecordingTag(const String & tags) { return tags == kRecordingTag; }

struct RecordingMessage
{
    enum Type { Invalid, Request, Answer, Start, Mark, Stop, Alive };

    Type type = Invalid;
    String id;          // the request this belongs to: 8-32 letters/digits
    bool ok = false;    // Answer only
    String label;       // Mark only, at most kMaxLabel characters
    double ago = 0.0;   // Mark only: seconds between pressing Mark and sending it (0-600)
    StringArray people; // Start only: everyone who agreed (at most kMaxPeople names)

    static constexpr int kMaxLabel = 80;
    static constexpr int kMaxPeople = 16;

    String toJSON() const;
    // Invalid unless every field is present and in range
    static RecordingMessage parse(const String & json);

    static RecordingMessage request(const String & id) { RecordingMessage m; m.type = Request; m.id = id; return m; }
    static RecordingMessage answer(const String & id, bool ok) { RecordingMessage m; m.type = Answer; m.id = id; m.ok = ok; return m; }
    static RecordingMessage start(const String & id, const StringArray & people) { RecordingMessage m; m.type = Start; m.id = id; m.people = people; return m; }
    static RecordingMessage mark(const String & id, const String & label, double ago);
    static RecordingMessage stop(const String & id) { RecordingMessage m; m.type = Stop; m.id = id; return m; }
    // sent every kAliveEvery seconds while recording; anyone not recording that id answers Stop
    static RecordingMessage alive(const String & id) { RecordingMessage m; m.type = Alive; m.id = id; return m; }
};

// a random request id
String makeRecordingId();

// The consent rules, one per computer. The UI sends whatever `out` holds after each call.
class RecordingConsent
{
public:
    enum class Phase { Idle, Asking, Asked, Agreed, Recording };

    enum class Action {
        None,
        ShowQuestion,     // `who` wants to record: ask this person, then call answer()
        StartRecording,   // everyone agreed: start recording now
        Declined,         // `who` said no (or isn't answering); nothing is recorded
        Cancelled,        // a question we were shown is no longer needed
        StopRecording,    // `who` stopped it, left, or hadn't agreed and joined
        ConnectionLost,   // nothing heard from `who` for kLostAfter seconds: recording stopped
        AddMark           // `who` added a highlight `label`, `ago` seconds before it arrived
    };

    static constexpr double kAliveEvery = 10.0;       // seconds between Alive messages
    static constexpr double kLostAfter = 30.0;        // stop when someone is silent this long
    static constexpr double kQuestionTimeout = 75.0;  // a question or agreement nobody follows up ends

    // seconds, steadily increasing; tests replace it
    std::function<double()> clock = []() { return Time::getMillisecondCounterHiRes() * 0.001; };

    struct Result
    {
        Action action = Action::None;
        String who;
        String label;
        double ago = 0.0;
        Array<RecordingMessage> out;   // to send to everyone in the lesson
    };

    // our own name in the lesson; set before anything else
    void setName(const String & name) { if (phase == Phase::Idle) me = name; }

    Phase getPhase() const { return phase; }
    bool isRecording() const { return phase == Phase::Recording; }
    String getAsker() const { return asker; }
    String getId() const { return id; }
    // everyone who agreed to this recording, including ourselves
    StringArray getPeople() const { return people; }

    // we pressed Record. With nobody else in the lesson, recording starts straight away.
    Result ask(const StringArray & others, const String & id = makeRecordingId());
    // our answer to the question shown after ShowQuestion
    Result answer(bool ok);
    // we pressed Stop, or our question timed out
    Result stop();
    // we pressed Mark
    Result mark(const String & label, double ago);

    // a message from someone in the lesson
    Result receive(const String & from, const RecordingMessage & m);
    // someone left: while asking they no longer need to answer; a recording stops (they must agree again)
    Result peerLeft(const String & name);
    // call about once a second: sends Alive, notices silence, ends stale questions
    Result tick();
    // someone joined: a recording they haven't agreed to stops
    Result peerJoined(const String & name);

private:
    Result startIfAllAgreed();
    void reset();
    void setPhase(Phase p);
    void beginRecording();

    Phase phase = Phase::Idle;
    double phaseSince = 0.0;
    double lastAliveSent = 0.0;
    std::map<String, double> lastHeard;   // Recording: when each other person was last heard
    String id;
    String me, asker;
    StringArray waitingFor;   // Asking: people who haven't answered yet
    StringArray people;
};

struct Highlight
{
    double seconds = 0.0;   // from the start of the recording
    String label;
    String by;
};

// what each computer saves next to its recording: "<recording>.canduet.json"
struct LessonSummary
{
    String title;               // lesson title, or empty outside lessons
    Time started;
    double durationSeconds = 0.0;
    StringArray people;
    String audioFileName;       // in the same folder as the summary
    Array<Highlight> highlights;

    File summaryFile;           // set by load(); where this summary lives

    var toVar() const;
    static bool fromVar(const var & v, LessonSummary & out);
    bool save(const File & file) const;
    static bool load(const File & file, LessonSummary & out);

    File getAudioFile() const { return summaryFile.getSiblingFile(audioFileName); }
    // a highlight label falls back to "Highlight N"
    String getHighlightLabel(int index) const;
};

File getSummaryFileFor(const File & audioFile);
// all summaries in a folder and its subfolders, newest first
Array<LessonSummary> findLessonSummaries(const File & folder);

// "1:05" or "1:02:03"
String formatTime(double seconds);

}
