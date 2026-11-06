// SPDX-License-Identifier: GPLv3-or-later WITH Appstore-exception
// Copyright (C) 2026 Tin Can Studios

#include "TinCanRecording.h"

namespace TinCan {

const char * const kRecordingTag = "canduet-recording-1";

static const char * typeName(RecordingMessage::Type t)
{
    switch (t) {
        case RecordingMessage::Request: return "request";
        case RecordingMessage::Answer:  return "answer";
        case RecordingMessage::Start:   return "start";
        case RecordingMessage::Mark:    return "mark";
        case RecordingMessage::Stop:    return "stop";
        default:                        return "";
    }
}

static bool isValidId(const String & id)
{
    if (id.length() < 8 || id.length() > 32) return false;
    for (auto c : id) {
        if (!CharacterFunctions::isLetterOrDigit(c) || c > 127) return false;
    }
    return true;
}

// one line of plain text: control characters become spaces, trimmed, at most maxLen characters
static String cleanText(const String & s, int maxLen)
{
    String out;
    for (auto c : s) out << (c < 32 || c == 127 ? juce_wchar(' ') : c);
    return out.trim().substring(0, maxLen).trim();
}

RecordingMessage RecordingMessage::mark(const String & id, const String & label, double ago)
{
    RecordingMessage m;
    m.type = Mark;
    m.id = id;
    m.label = cleanText(label, kMaxLabel);
    m.ago = jlimit(0.0, 600.0, ago);
    return m;
}

String RecordingMessage::toJSON() const
{
    auto * obj = new DynamicObject();
    obj->setProperty("v", 1);
    obj->setProperty("t", typeName(type));
    obj->setProperty("id", id);
    if (type == Answer) obj->setProperty("ok", ok);
    if (type == Mark) {
        obj->setProperty("label", label);
        obj->setProperty("ago", ago);
    }
    if (type == Start) {
        Array<var> names;
        for (auto & n : people) names.add(n);
        obj->setProperty("people", names);
    }
    return JSON::toString(var(obj), true);
}

RecordingMessage RecordingMessage::parse(const String & json)
{
    RecordingMessage bad;
    if (json.length() > 4000) return bad;

    var v;
    if (JSON::parse(json, v).failed() || !v.isObject()) return bad;
    if (!v.getProperty("v", var()).isInt() || (int) v.getProperty("v", var()) != 1) return bad;

    auto idv = v.getProperty("id", var());
    if (!idv.isString() || !isValidId(idv.toString())) return bad;

    RecordingMessage m;
    m.id = idv.toString();

    auto t = v.getProperty("t", var()).toString();
    if (t == "request") m.type = Request;
    else if (t == "answer") {
        auto ok = v.getProperty("ok", var());
        if (!ok.isBool()) return bad;
        m.type = Answer;
        m.ok = (bool) ok;
    }
    else if (t == "start") {
        auto names = v.getProperty("people", var());
        if (!names.isArray() || names.size() > kMaxPeople) return bad;
        for (auto & n : *names.getArray()) {
            if (!n.isString()) return bad;
            auto name = cleanText(n.toString(), kMaxLabel);
            if (name.isEmpty()) return bad;
            m.people.addIfNotAlreadyThere(name);
        }
        m.type = Start;
    }
    else if (t == "mark") {
        auto label = v.getProperty("label", var());
        auto ago = v.getProperty("ago", var());
        if (!label.isString() || !(ago.isDouble() || ago.isInt() || ago.isInt64())) return bad;
        double a = (double) ago;
        if (!(a >= 0.0 && a <= 600.0)) return bad;
        m.type = Mark;
        m.label = cleanText(label.toString(), kMaxLabel);
        m.ago = a;
    }
    else if (t == "stop") m.type = Stop;
    else return bad;

    return m;
}

String makeRecordingId()
{
    static const char * chars = "abcdefghijklmnopqrstuvwxyz0123456789";
    Random r;  // seeded from the system's entropy; only needs to be unique per lesson
    r.setSeedRandomly();
    String id;
    for (int i = 0; i < 16; ++i) id << chars[r.nextInt(36)];
    return id;
}

//==============================================================================

void RecordingConsent::reset()
{
    phase = Phase::Idle;
    id.clear();
    asker.clear();
    waitingFor.clear();
    people.clear();
}

RecordingConsent::Result RecordingConsent::startIfAllAgreed()
{
    Result r;
    if (phase != Phase::Asking || !waitingFor.isEmpty()) return r;
    phase = Phase::Recording;
    r.action = Action::StartRecording;
    r.who = me;
    r.out.add(RecordingMessage::start(id, people));
    return r;
}

RecordingConsent::Result RecordingConsent::ask(const StringArray & others, const String & newId)
{
    Result r;
    if (phase != Phase::Idle || me.isEmpty()) return r;

    id = newId;
    asker = me;
    people = StringArray(me);
    waitingFor.clear();
    for (auto & o : others) {
        if (o.isNotEmpty() && o != me) waitingFor.addIfNotAlreadyThere(o);
    }

    if (waitingFor.isEmpty()) {
        // practising alone: nobody else to ask
        phase = Phase::Recording;
        r.action = Action::StartRecording;
        r.who = me;
        return r;
    }

    phase = Phase::Asking;
    r.out.add(RecordingMessage::request(id));
    return r;
}

RecordingConsent::Result RecordingConsent::answer(bool ok)
{
    Result r;
    if (phase != Phase::Asked) return r;
    r.out.add(RecordingMessage::answer(id, ok));
    if (ok) phase = Phase::Agreed;
    else reset();
    return r;
}

RecordingConsent::Result RecordingConsent::stop()
{
    Result r;
    switch (phase) {
        case Phase::Idle:
            return r;
        case Phase::Asked:
            return answer(false);
        case Phase::Recording:
            r.action = Action::StopRecording;
            r.who = me;
            break;
        default:
            break;
    }
    if (people.size() > 1 || phase != Phase::Recording) r.out.add(RecordingMessage::stop(id));
    reset();
    return r;
}

RecordingConsent::Result RecordingConsent::mark(const String & label, double ago)
{
    Result r;
    if (phase != Phase::Recording) return r;
    auto m = RecordingMessage::mark(id, label, ago);
    r.action = Action::AddMark;
    r.who = me;
    r.label = m.label;
    r.ago = m.ago;
    if (people.size() > 1) r.out.add(m);
    return r;
}

RecordingConsent::Result RecordingConsent::receive(const String & from, const RecordingMessage & m)
{
    Result r;
    if (m.type == RecordingMessage::Invalid || me.isEmpty() || from.isEmpty() || from == me) return r;

    switch (m.type) {
        case RecordingMessage::Request:
            if (phase == Phase::Idle) {
                phase = Phase::Asked;
                id = m.id;
                asker = from;
                r.action = Action::ShowQuestion;
                r.who = from;
            }
            else if (phase == Phase::Asking && m.id.compare(id) < 0) {
                // we both pressed Record: the smaller id wins, and we already want to record
                waitingFor.clear();
                people.clear();
                phase = Phase::Agreed;
                id = m.id;
                asker = from;
                r.out.add(RecordingMessage::answer(id, true));
            }
            break;

        case RecordingMessage::Answer:
            if (phase == Phase::Asking && m.id == id && waitingFor.contains(from)) {
                if (m.ok) {
                    waitingFor.removeString(from);
                    people.addIfNotAlreadyThere(from);
                    return startIfAllAgreed();
                }
                r.action = Action::Declined;
                r.who = from;
                r.out.add(RecordingMessage::stop(id));
                reset();
            }
            break;

        case RecordingMessage::Start:
            if (phase == Phase::Agreed && m.id == id && from == asker) {
                phase = Phase::Recording;
                people = m.people;
                people.addIfNotAlreadyThere(asker);
                people.addIfNotAlreadyThere(me);
                r.action = Action::StartRecording;
                r.who = from;
            }
            break;

        case RecordingMessage::Mark:
            if (phase == Phase::Recording && m.id == id && people.contains(from)) {
                r.action = Action::AddMark;
                r.who = from;
                r.label = m.label;
                r.ago = m.ago;
            }
            break;

        case RecordingMessage::Stop:
            if (m.id != id) break;
            if (phase == Phase::Recording && people.contains(from)) {
                r.action = Action::StopRecording;
                r.who = from;
                reset();
            }
            else if ((phase == Phase::Asked || phase == Phase::Agreed) && from == asker) {
                r.action = Action::Cancelled;
                r.who = from;
                reset();
            }
            else if (phase == Phase::Asking && (waitingFor.contains(from) || people.contains(from))) {
                // someone changed their mind before everyone had answered
                r.action = Action::Declined;
                r.who = from;
                r.out.add(RecordingMessage::stop(id));
                reset();
            }
            break;

        default:
            break;
    }
    return r;
}

RecordingConsent::Result RecordingConsent::peerLeft(const String & name)
{
    Result r;
    if (phase == Phase::Asking && waitingFor.contains(name)) {
        waitingFor.removeString(name);
        if (people.size() > 1) return startIfAllAgreed();
        if (waitingFor.isEmpty()) {
            // the only other person left before answering
            r.action = Action::Declined;
            r.who = name;
            reset();
        }
    }
    else if ((phase == Phase::Asked || phase == Phase::Agreed) && name == asker) {
        r.action = Action::Cancelled;
        r.who = name;
        reset();
    }
    return r;
}

RecordingConsent::Result RecordingConsent::peerJoined(const String & name)
{
    Result r;
    if (name.isEmpty() || name == me) return r;
    if (phase == Phase::Recording && !people.contains(name)) {
        r.action = Action::StopRecording;
        r.who = name;
        r.out.add(RecordingMessage::stop(id));
        reset();
    }
    else if (phase == Phase::Asking) {
        // they weren't asked; ask everyone again
        r.action = Action::Declined;
        r.who = name;
        r.out.add(RecordingMessage::stop(id));
        reset();
    }
    return r;
}

//==============================================================================

var LessonSummary::toVar() const
{
    auto * obj = new DynamicObject();
    obj->setProperty("v", 1);
    obj->setProperty("title", title);
    obj->setProperty("started", started.toISO8601(true));
    obj->setProperty("duration", durationSeconds);
    Array<var> names;
    for (auto & p : people) names.add(p);
    obj->setProperty("people", names);
    obj->setProperty("audio", audioFileName);
    Array<var> hl;
    for (auto & h : highlights) {
        auto * o = new DynamicObject();
        o->setProperty("t", h.seconds);
        o->setProperty("label", h.label);
        o->setProperty("by", h.by);
        hl.add(var(o));
    }
    obj->setProperty("highlights", hl);
    return var(obj);
}

static bool isPlainFileName(const String & name)
{
    return name.isNotEmpty() && name.length() <= 255 && !name.containsAnyOf("/\\:") && name != "." && name != "..";
}

bool LessonSummary::fromVar(const var & v, LessonSummary & out)
{
    if (!v.isObject() || (int) v.getProperty("v", 0) != 1) return false;

    LessonSummary s;
    s.title = cleanText(v.getProperty("title", "").toString(), 200);
    s.started = Time::fromISO8601(v.getProperty("started", "").toString());
    if (s.started.toMilliseconds() == 0) return false;
    s.durationSeconds = jmax(0.0, (double) v.getProperty("duration", 0.0));
    s.audioFileName = v.getProperty("audio", "").toString();
    if (!isPlainFileName(s.audioFileName)) return false;

    if (auto * names = v.getProperty("people", var()).getArray()) {
        for (auto & n : *names) {
            auto name = cleanText(n.toString(), RecordingMessage::kMaxLabel);
            if (name.isNotEmpty()) s.people.addIfNotAlreadyThere(name);
        }
    }
    if (auto * hl = v.getProperty("highlights", var()).getArray()) {
        for (auto & h : *hl) {
            if (!h.isObject()) continue;
            Highlight x;
            x.seconds = jlimit(0.0, jmax(0.0, s.durationSeconds), (double) h.getProperty("t", 0.0));
            x.label = cleanText(h.getProperty("label", "").toString(), RecordingMessage::kMaxLabel);
            x.by = cleanText(h.getProperty("by", "").toString(), RecordingMessage::kMaxLabel);
            s.highlights.add(x);
        }
    }
    std::stable_sort(s.highlights.begin(), s.highlights.end(),
                     [](const Highlight & a, const Highlight & b) { return a.seconds < b.seconds; });
    out = s;
    return true;
}

bool LessonSummary::save(const File & file) const
{
    return file.replaceWithText(JSON::toString(toVar(), false));
}

bool LessonSummary::load(const File & file, LessonSummary & out)
{
    if (!file.existsAsFile() || file.getSize() > 1024 * 1024) return false;
    var v;
    if (JSON::parse(file.loadFileAsString(), v).failed()) return false;
    if (!fromVar(v, out)) return false;
    out.summaryFile = file;
    return true;
}

String LessonSummary::getHighlightLabel(int index) const
{
    auto & h = highlights.getReference(index);
    return h.label.isNotEmpty() ? h.label : TRANS("Highlight") + " " + String(index + 1);
}

File getSummaryFileFor(const File & audioFile)
{
    return audioFile.getSiblingFile(audioFile.getFileNameWithoutExtension() + ".canduet.json");
}

Array<LessonSummary> findLessonSummaries(const File & folder)
{
    Array<LessonSummary> list;
    if (!folder.isDirectory()) return list;
    // multi-track recordings sit in a folder of their own, so look in subfolders too
    for (auto & f : folder.findChildFiles(File::findFiles, true, "*.canduet.json")) {
        LessonSummary s;
        if (LessonSummary::load(f, s)) list.add(s);
    }
    std::stable_sort(list.begin(), list.end(), [](const LessonSummary & a, const LessonSummary & b) {
        return a.started > b.started;
    });
    return list;
}

String formatTime(double seconds)
{
    auto total = (int) std::floor(jmax(0.0, seconds));
    int h = total / 3600, m = (total / 60) % 60, s = total % 60;
    if (h > 0) return String(h) + ":" + String(m).paddedLeft('0', 2) + ":" + String(s).paddedLeft('0', 2);
    return String(m) + ":" + String(s).paddedLeft('0', 2);
}

}
