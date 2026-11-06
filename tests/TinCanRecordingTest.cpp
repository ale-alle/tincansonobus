// SPDX-License-Identifier: GPLv3-or-later WITH Appstore-exception
// Copyright (C) 2026 Tin Can Studios

// Recording consent, highlights and summaries (T-045, T-046). Part of TinCanAudioTests.

#include <JuceHeader.h>
#include "../Source/TinCanRecording.h"

#include <cstdio>

using namespace TinCan;
using Action = RecordingConsent::Action;
using Phase = RecordingConsent::Phase;

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); ++failures; } } while (0)

// deliver every message one side sends to the other, as the chat channel would (through JSON)
static Array<RecordingConsent::Result> deliver(const RecordingConsent::Result & r, const String & from,
                                               std::initializer_list<RecordingConsent *> to)
{
    Array<RecordingConsent::Result> results;
    for (auto & m : r.out) {
        auto parsed = RecordingMessage::parse(m.toJSON());
        CHECK(parsed.type == m.type);
        for (auto * c : to) results.add(c->receive(from, parsed));
    }
    return results;
}

static void testMessages()
{
    auto id = makeRecordingId();
    CHECK(id.length() == 16);
    CHECK(id != makeRecordingId());

    auto m = RecordingMessage::parse(RecordingMessage::mark(id, "  wrist\ttoo high\n ", 2.5).toJSON());
    CHECK(m.type == RecordingMessage::Mark && m.id == id && m.label == "wrist too high" && m.ago == 2.5);

    auto s = RecordingMessage::parse(RecordingMessage::start(id, StringArray("Alex", "Sam")).toJSON());
    CHECK(s.type == RecordingMessage::Start && s.people.size() == 2);

    auto a = RecordingMessage::parse(RecordingMessage::answer(id, false).toJSON());
    CHECK(a.type == RecordingMessage::Answer && !a.ok);

    auto longLabel = RecordingMessage::mark(id, String::repeatedString("x", 300), 9999);
    CHECK(longLabel.label.length() == RecordingMessage::kMaxLabel && longLabel.ago == 600.0);

    // anything malformed is ignored
    CHECK(RecordingMessage::parse("").type == RecordingMessage::Invalid);
    CHECK(RecordingMessage::parse("hello there").type == RecordingMessage::Invalid);
    CHECK(RecordingMessage::parse("{\"v\":1,\"t\":\"request\",\"id\":\"short\"}").type == RecordingMessage::Invalid);
    CHECK(RecordingMessage::parse("{\"v\":1,\"t\":\"request\",\"id\":\"abc/../defgh\"}").type == RecordingMessage::Invalid);
    CHECK(RecordingMessage::parse("{\"v\":2,\"t\":\"request\",\"id\":\"abcdefgh12\"}").type == RecordingMessage::Invalid);
    CHECK(RecordingMessage::parse("{\"v\":1,\"t\":\"answer\",\"id\":\"abcdefgh12\"}").type == RecordingMessage::Invalid);
    CHECK(RecordingMessage::parse("{\"v\":1,\"t\":\"mark\",\"id\":\"abcdefgh12\",\"label\":\"x\",\"ago\":-1}").type == RecordingMessage::Invalid);
    CHECK(RecordingMessage::parse("{\"v\":1,\"t\":\"launch\",\"id\":\"abcdefgh12\"}").type == RecordingMessage::Invalid);
    CHECK(RecordingMessage::parse("{\"v\":1,\"t\":\"request\",\"id\":\"abcdefgh12\"}").type == RecordingMessage::Request);
    CHECK(RecordingMessage::parse(RecordingMessage::alive(id).toJSON()).type == RecordingMessage::Alive);
}

static void testOneToOne()
{
    RecordingConsent teacher, student;
    teacher.setName("Alex"); student.setName("Sam");

    // nothing records until the student says yes
    auto r = teacher.ask(StringArray("Sam"), "aaaaaaaa11");
    CHECK(r.action == Action::None && teacher.getPhase() == Phase::Asking && r.out.size() == 1);
    auto got = deliver(r, "Alex", { &student });
    CHECK(got[0].action == Action::ShowQuestion && got[0].who == "Alex" && student.getPhase() == Phase::Asked);

    r = student.answer(true);
    CHECK(student.getPhase() == Phase::Agreed && !student.isRecording());
    got = deliver(r, "Sam", { &teacher });
    CHECK(got[0].action == Action::StartRecording && teacher.isRecording());
    got = deliver(got[0], "Alex", { &student });
    CHECK(got[0].action == Action::StartRecording && student.isRecording());
    CHECK(student.getPeople().size() == 2 && teacher.getPeople().contains("Sam"));

    // marks reach both sides
    r = student.mark("I keep rushing here", 1.0);
    CHECK(r.action == Action::AddMark && r.who == "Sam");
    got = deliver(r, "Sam", { &teacher });
    CHECK(got[0].action == Action::AddMark && got[0].who == "Sam" && got[0].label == "I keep rushing here" && got[0].ago == 1.0);

    // a stranger's mark or stop is ignored
    CHECK(teacher.receive("Mallory", RecordingMessage::stop("aaaaaaaa11")).action == Action::None && teacher.isRecording());
    CHECK(teacher.receive("Sam", RecordingMessage::stop("zzzzzzzz99")).action == Action::None && teacher.isRecording());

    // either one stops it for both
    r = student.stop();
    CHECK(r.action == Action::StopRecording && !student.isRecording());
    got = deliver(r, "Sam", { &teacher });
    CHECK(got[0].action == Action::StopRecording && got[0].who == "Sam" && teacher.getPhase() == Phase::Idle);
}

static void testDeclineAndEdgeCases()
{
    {
        RecordingConsent teacher, student;
        teacher.setName("Alex"); student.setName("Sam");
        auto got = deliver(teacher.ask(StringArray("Sam"), "bbbbbbbb22"), "Alex", { &student });
        got = deliver(student.answer(false), "Sam", { &teacher });
        CHECK(got[0].action == Action::Declined && got[0].who == "Sam" && teacher.getPhase() == Phase::Idle);
        CHECK(student.getPhase() == Phase::Idle);
    }
    {
        // the asker gives up (or times out): the question disappears
        RecordingConsent teacher, student;
        teacher.setName("Alex"); student.setName("Sam");
        deliver(teacher.ask(StringArray("Sam"), "cccccccc33"), "Alex", { &student });
        auto got = deliver(teacher.stop(), "Alex", { &student });
        CHECK(got[0].action == Action::Cancelled && student.getPhase() == Phase::Idle);
    }
    {
        // alone: records straight away, marks stay local
        RecordingConsent me;
        me.setName("Alex");
        auto r = me.ask(StringArray(), "dddddddd44");
        CHECK(r.action == Action::StartRecording && r.out.isEmpty() && me.isRecording());
        r = me.mark("", 0.0);
        CHECK(r.action == Action::AddMark && r.out.isEmpty());
        CHECK(me.stop().out.isEmpty());
    }
    {
        // someone who didn't agree joins: the recording stops everywhere
        RecordingConsent teacher, student;
        teacher.setName("Alex"); student.setName("Sam");
        auto got = deliver(teacher.ask(StringArray("Sam"), "eeeeeeee55"), "Alex", { &student });
        got = deliver(student.answer(true), "Sam", { &teacher });
        deliver(got[0], "Alex", { &student });
        auto r = teacher.peerJoined("Jo");
        CHECK(r.action == Action::StopRecording && r.who == "Jo" && !teacher.isRecording());
        got = deliver(r, "Alex", { &student });
        CHECK(got[0].action == Action::StopRecording && !student.isRecording());
    }
    {
        // the student leaves before answering
        RecordingConsent teacher;
        teacher.setName("Alex");
        teacher.ask(StringArray("Sam"), "ffffffff66");
        auto r = teacher.peerLeft("Sam");
        CHECK(r.action == Action::Declined && teacher.getPhase() == Phase::Idle);
    }
    {
        // both press Record at once: the smaller id wins and nobody gets asked twice
        RecordingConsent a, b;
        a.setName("Alex"); b.setName("Sam");
        auto ra = a.ask(StringArray("Sam"), "aaaaaaaa00");
        auto rb = b.ask(StringArray("Alex"), "bbbbbbbb00");
        auto atB = deliver(ra, "Alex", { &b });   // b yields, answers yes
        // b drops its own request (telling anyone who agreed to it) and says yes
        CHECK(atB[0].action == Action::None && b.getPhase() == Phase::Agreed && atB[0].out.size() == 2);
        auto atA = deliver(rb, "Sam", { &a });    // a ignores b's request
        CHECK(atA[0].action == Action::None && a.getPhase() == Phase::Asking);
        atA = deliver(atB[0], "Sam", { &a });
        CHECK(atA[0].action == Action::None && atA[1].action == Action::StartRecording);  // Stop for b's id is ignored
        atB = deliver(atA[1], "Alex", { &b });
        CHECK(atB[0].action == Action::StartRecording && a.isRecording() && b.isRecording());
    }
    {
        // group of three: starts only when both students agree
        RecordingConsent t, s1, s2;
        t.setName("Alex"); s1.setName("Sam"); s2.setName("Jo");
        auto r = t.ask(StringArray("Sam", "Jo"), "gggggggg77");
        deliver(r, "Alex", { &s1, &s2 });
        auto got = deliver(s1.answer(true), "Sam", { &t });
        CHECK(got[0].action == Action::None && t.getPhase() == Phase::Asking);
        got = deliver(s2.answer(true), "Jo", { &t });
        CHECK(got[0].action == Action::StartRecording);
        deliver(got[0], "Alex", { &s1, &s2 });
        CHECK(s1.isRecording() && s2.isRecording() && s2.getPeople().size() == 3);
    }
}

// two people recording, both on a fake clock
struct Pair
{
    double now = 1000.0;
    RecordingConsent teacher, student;
    Pair(const String & id)
    {
        teacher.clock = [this]() { return now; };
        student.clock = [this]() { return now; };
        teacher.setName("Alex"); student.setName("Sam");
        auto got = deliver(teacher.ask(StringArray("Sam"), id), "Alex", { &student });
        got = deliver(student.answer(true), "Sam", { &teacher });
        deliver(got[0], "Alex", { &student });
    }
};

static void testReliability()
{
    {
        // leaving stops it: coming back under the same name needs a new yes
        Pair p("hhhhhhhh11");
        CHECK(p.teacher.isRecording() && p.student.isRecording());
        auto r = p.teacher.peerLeft("Sam");
        CHECK(r.action == Action::StopRecording && r.who == "Sam" && !p.teacher.isRecording());
        CHECK(p.teacher.peerJoined("Sam").action == Action::None);  // nothing left to stop
        CHECK(p.teacher.getPhase() == Phase::Idle);
    }
    {
        // a lost Stop: the side still recording finds out from its next Alive
        Pair p("iiiiiiii22");
        p.student.stop();                  // its Stop message never arrives
        CHECK(p.teacher.isRecording());
        p.now += RecordingConsent::kAliveEvery;
        auto tick = p.teacher.tick();
        CHECK(tick.out.size() == 1 && tick.out[0].type == RecordingMessage::Alive);
        auto atStudent = deliver(tick, "Alex", { &p.student });
        CHECK(atStudent[0].out.size() == 1 && atStudent[0].out[0].type == RecordingMessage::Stop);
        auto atTeacher = deliver(atStudent[0], "Sam", { &p.teacher });
        CHECK(atTeacher[0].action == Action::StopRecording && !p.teacher.isRecording());
    }
    {
        // Alive keeps it going; silence for kLostAfter stops it
        Pair p("jjjjjjjj33");
        for (int i = 0; i < 6; ++i) {
            p.now += RecordingConsent::kAliveEvery;
            deliver(p.student.tick(), "Sam", { &p.teacher });
            deliver(p.teacher.tick(), "Alex", { &p.student });
        }
        CHECK(p.teacher.isRecording() && p.student.isRecording());
        p.now += RecordingConsent::kLostAfter + 1.0;   // the student's PC goes quiet
        auto r = p.teacher.tick();
        CHECK(r.action == Action::ConnectionLost && r.who == "Sam" && !p.teacher.isRecording());
    }
    {
        // a lost Start: the agreed side starts when the first Alive arrives
        double now = 0.0;
        RecordingConsent teacher, student;
        teacher.clock = [&]() { return now; };
        student.clock = [&]() { return now; };
        teacher.setName("Alex"); student.setName("Sam");
        deliver(teacher.ask(StringArray("Sam"), "kkkkkkkk44"), "Alex", { &student });
        deliver(student.answer(true), "Sam", { &teacher });  // its Start is lost
        CHECK(teacher.isRecording() && student.getPhase() == Phase::Agreed);
        now += RecordingConsent::kAliveEvery;
        auto got = deliver(teacher.tick(), "Alex", { &student });
        CHECK(got[0].action == Action::StartRecording && student.isRecording());
    }
    {
        // a question whose follow-up never comes ends on its own, and doesn't block the next one
        double now = 0.0;
        RecordingConsent student;
        student.clock = [&]() { return now; };
        student.setName("Sam");
        CHECK(student.receive("Alex", RecordingMessage::request("llllllll55")).action == Action::ShowQuestion);
        student.answer(true);
        // the asker asks again with a new id (their first attempt went wrong): shown again
        CHECK(student.receive("Alex", RecordingMessage::request("mmmmmmmm66")).action == Action::ShowQuestion);
        now += RecordingConsent::kQuestionTimeout + 1.0;
        auto r = student.tick();
        CHECK(r.action == Action::Cancelled && student.getPhase() == Phase::Idle);
        CHECK(student.receive("Jo", RecordingMessage::request("nnnnnnnn77")).action == Action::ShowQuestion);
    }
    {
        // someone not in the recording sends Alive: ignored; an Alive for another id gets a Stop back
        Pair p("oooooooo88");
        CHECK(p.teacher.receive("Mallory", RecordingMessage::alive("oooooooo88")).out.isEmpty());
        auto r = p.teacher.receive("Sam", RecordingMessage::alive("pppppppp99"));
        CHECK(r.out.size() == 1 && r.out[0].type == RecordingMessage::Stop && p.teacher.isRecording());
    }
}

static void testSummaries()
{
    auto folder = File::getSpecialLocation(File::tempDirectory).getChildFile("canduet-summary-test-" + makeRecordingId());
    folder.createDirectory();

    auto audio = folder.getChildFile("Piano with Alex_2026-11-05_16.00.00.flac");
    audio.replaceWithText("not really audio");

    LessonSummary s;
    s.title = "Piano with Alex";
    s.started = Time(2026, 10, 5, 16, 0, 0);
    s.durationSeconds = 1800.0;
    s.people = StringArray("Alex", "Sam");
    s.audioFileName = audio.getFileName();
    s.highlights.add({ 600.5, "Left hand fingering", "Alex" });
    s.highlights.add({ 30.0, "", "Sam" });
    auto summaryFile = getSummaryFileFor(audio);
    CHECK(summaryFile.getFileName() == "Piano with Alex_2026-11-05_16.00.00.canduet.json");
    CHECK(s.save(summaryFile));

    LessonSummary older = s;
    older.started = Time(2026, 9, 29, 16, 0, 0);
    older.audioFileName = "older.flac";
    older.highlights.clear();
    CHECK(older.save(folder.getChildFile("older.canduet.json")));

    folder.getChildFile("broken.canduet.json").replaceWithText("{ not json");
    folder.getChildFile("escape.canduet.json").replaceWithText(
        "{\"v\":1,\"started\":\"2026-11-01T10:00:00Z\",\"duration\":10,\"audio\":\"..\\\\..\\\\secret.txt\"}");

    auto list = findLessonSummaries(folder);
    CHECK(list.size() == 2);  // broken and path-escaping summaries are skipped
    if (list.size() == 2) {
        auto & got = list.getReference(0);
        CHECK(got.title == "Piano with Alex" && got.durationSeconds == 1800.0 && got.people.size() == 2);
        CHECK(got.getAudioFile() == audio);
        CHECK(got.highlights.size() == 2 && got.highlights[0].seconds == 30.0);  // sorted by time
        CHECK(got.getHighlightLabel(0) == "Highlight 1" && got.getHighlightLabel(1) == "Left hand fingering");
        CHECK(list.getReference(1).audioFileName == "older.flac");
    }

    CHECK(formatTime(65.9) == "1:05");
    CHECK(formatTime(3723) == "1:02:03");
    CHECK(formatTime(-3) == "0:00");

    folder.deleteRecursively();
}

int runRecordingTests()
{
    testMessages();
    testOneToOne();
    testDeclineAndEdgeCases();
    testReliability();
    testSummaries();
    std::printf("  recording consent and summaries: %s\n", failures == 0 ? "ok" : "FAILED");
    return failures;
}
