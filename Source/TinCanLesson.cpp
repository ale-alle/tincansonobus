// SPDX-License-Identifier: GPLv3-or-later WITH Appstore-exception
// Copyright (C) 2026 Tin Can Studios

#include "TinCanLesson.h"
#include "TinCanLessonRules.h"
#include "TinCanTheme.h"
#include "TinCanVideo.h"

#include <cmath>

namespace TinCan {

static bool isLocalhostHttp(const String & url)
{
    if (!url.startsWithIgnoreCase("http://") || url.containsChar('@')) return false;
    auto host = url.substring(7).upToFirstOccurrenceOf("/", false, false).upToFirstOccurrenceOf(":", false, false);
    return host == "localhost" || host == "127.0.0.1";
}

String getLessonPage()
{
    auto page = SystemStats::getEnvironmentVariable("TINCAN_LESSON_PAGE", {}).trim();
    if (page.startsWithIgnoreCase("https://") || isLocalhostHttp(page))
        return page.upToFirstOccurrenceOf("#", false, false);

    // next to the video page: …/app/video/ -> …/app/video/lesson.html
    auto video = getVideoPage();
    return video.upToLastOccurrenceOf("/", true, false) + "lesson.html";
}

String getPracticePage()
{
    auto page = SystemStats::getEnvironmentVariable("TINCAN_PRACTICE_PAGE", {}).trim();
    if (page.startsWithIgnoreCase("https://") || isLocalhostHttp(page))
        return page.upToFirstOccurrenceOf("#", false, false);

    auto file = File::getSpecialLocation(File::currentExecutableFile).getSiblingFile("lesson").getChildFile("lesson.html");
    return file.existsAsFile() ? URL(file).toString(false) : String();
}

#if JUCE_WEB_BROWSER && JUCE_USE_WIN_WEBVIEW2

// The docked Tools panel: a borderless child window of the main window, laid exactly over the gap the
// page leaves for it. Created after the web view, so Windows draws it on top.
class LessonView::ToolsDock : public Component
{
public:
    explicit ToolsDock(std::unique_ptr<ToolsPanel> panel_) : panel(std::move(panel_))
    {
        setOpaque(true);
        addAndMakeVisible(panel.get());
    }

    void paint(Graphics & g) override
    {
        g.fillAll(TinCanTheme::surface);
        g.setColour(TinCanTheme::line);
        g.drawRect(getLocalBounds(), 1);
    }

    void resized() override { panel->setBounds(getLocalBounds().reduced(1)); }

    ToolsPanel * getPanel() const { return panel.get(); }

private:
    std::unique_ptr<ToolsPanel> panel;
};

std::unique_ptr<LessonView> LessonView::create(SonobusAudioProcessor & processor, const VideoInfo & video,
                                               const LessonInfo & info, Hooks hooks)
{
    auto address = info.practice ? getPracticePage() : getLessonPage();
    if (address.isEmpty()) return nullptr;
    auto page = VideoTilesView::create(address, info.practice ? VideoInfo() : video, info.myName,
                                       info.practice ? "&practice=1" : "");
    if (!page) return nullptr;
    return std::unique_ptr<LessonView>(new LessonView(processor, std::move(page), info, std::move(hooks)));
}

LessonView::LessonView(SonobusAudioProcessor & p, std::unique_ptr<VideoTilesView> page_, const LessonInfo & info_, Hooks hooks_)
    : processor(p), page(std::move(page_)), info(info_), hooks(std::move(hooks_))
{
    setOpaque(true);
    joinedAtMs = Time::currentTimeMillis();
    {
        // only messages from now on; older chat belongs to an earlier lesson
        auto & events = processor.getAllChatEvents();
        const ScopedLock sl(events.getLock());
        chatIndex = events.size();
    }
    page->onMessage = [this](const var & message) { handleMessage(message); };
    if (hooks.noteShare) hooks.noteShare->addListener(this);
    addAndMakeVisible(page.get());
    startTimer(100);
}

LessonView::~LessonView()
{
    if (hooks.noteShare) hooks.noteShare->removeListener(this);
    stopTimer();
    tools.reset();
    page.reset();
}

void LessonView::paint(Graphics & g)
{
    g.fillAll(TinCanTheme::night);
}

void LessonView::resized()
{
    page->setBounds(getLocalBounds());
    placeTools();
}

ToolsPanel * LessonView::getTools() const
{
    return tools ? tools->getPanel() : nullptr;
}

void LessonView::setToolsOpen(bool open)
{
    if (open == toolsOpen) return;
    toolsOpen = open;
    if (!open) {
        // closing destroys the panel, which stops the tuner and the MIDI copy
        tools.reset();
        toolsRect = {};
    }
    else if (hooks.makeTools) {
        if (auto panel = hooks.makeTools()) tools = std::make_unique<ToolsDock>(std::move(panel));
    }
    sendState();   // the page opens the gap, then tells us where it is
    placeTools();
}

void LessonView::placeTools()
{
    if (!tools) return;
    auto * top = getTopLevelComponent();
    auto * peer = top ? top->getPeer() : nullptr;
    if (peer == nullptr || toolsRect.isEmpty()) {
        tools->setVisible(false);
        return;
    }
    if (!tools->isOnDesktop()) {
        // a child of the main window's native window, created after the web view so it sits above it
        tools->addToDesktop(0, peer->getNativeHandle());
    }

    auto area = page->getLocalBounds().toFloat();
    juce::Rectangle<int> r = juce::Rectangle<float>(area.getWidth() * toolsRect.getX(), area.getHeight() * toolsRect.getY(),
                                                    area.getWidth() * toolsRect.getWidth(), area.getHeight() * toolsRect.getHeight())
                                 .getSmallestIntegerContainer();
    // child windows are placed in the main window's client coordinates
    auto bounds = top->getLocalArea(page.get(), r);
    if (tools->getBounds() != bounds) tools->setBounds(bounds);
    if (!tools->isVisible()) {
        tools->setVisible(true);
        tools->toFront(false);   // above the web view's own window
    }
}

void LessonView::setPeerVideoId(const String & userName, const String & identity)
{
    if (userName.isEmpty() || !isValidVideoIdentity(identity) || peerVideoIds.size() > 64) return;
    peerVideoIds[userName] = identity;
}

void LessonView::showToast(const String & text, int timeoutMs)
{
    DynamicObject::Ptr m = new DynamicObject();
    m->setProperty("t", "toast");
    m->setProperty("text", text.substring(0, 400));
    m->setProperty("ms", timeoutMs);
    page->send(var(m.get()));
}

void LessonView::timerCallback()
{
    // tell everyone our video identity when someone joins, and every 10 s for anyone who missed it
    int count = processor.getNumberRemotePeers();
    if (count != lastPeerCount || ++ticks >= 100) {
        lastPeerCount = count;
        ticks = 0;
        if (count > 0 && hooks.shareVideoId) hooks.shareVideoId();
        // forget video identities of people who left, so the table never fills up in a long lesson
        for (auto it = peerVideoIds.begin(); it != peerVideoIds.end(); ) {
            if (findPeerIndexByName(it->first) < 0) it = peerVideoIds.erase(it);
            else ++it;
        }
    }
    sendState();
    sendNewChat();
    placeTools();   // the main window may have moved or changed size
}

float LessonView::getParam(const String & id) const
{
    if (auto * prm = processor.getValueTreeState().getParameter(id))
        return prm->convertFrom0to1(prm->getValue());
    return 0.0f;
}

void LessonView::setParam(const String & id, float value)
{
    if (auto * prm = processor.getValueTreeState().getParameter(id))
        prm->setValueNotifyingHost(prm->convertTo0to1(value));
}

int LessonView::findPeerIndexByName(const String & name) const
{
    if (name.isEmpty()) return -1;
    for (int i = 0; i < processor.getNumberRemotePeers(); ++i) {
        if (processor.getRemotePeerUserName(i) == name) return i;
    }
    return -1;
}

int LessonView::findOpusFormat(int bitrate) const
{
    SonobusAudioProcessor::AudioCodecFormatInfo f;
    for (int i = 0; i < processor.getNumberAudioCodecFormats(); ++i) {
        if (processor.getAudioCodeFormatInfo(i, f) && f.codec == SonobusAudioProcessor::CodecOpus && f.bitrate == bitrate) return i;
    }
    return -1;
}

static float meterLevel(foleys::LevelMeterSource * meter)
{
    if (meter == nullptr) return 0.0f;
    float rms = 0.0f;
    for (int ch = 0; ch < meter->getNumChannels(); ++ch) rms = jmax(rms, meter->getRMSLevel(ch));
    float level = jlimit(0.0f, 1.0f, (Decibels::gainToDecibels(rms, -60.0f) + 60.0f) / 60.0f);
    return std::round(level * 100.0f) / 100.0f;
}

static float round1(float v) { return std::round(v * 10.0f) / 10.0f; }

void LessonView::sendState()
{
    using namespace LessonRules;

    Array<var> peers;
    for (int i = 0; i < processor.getNumberRemotePeers(); ++i) {
        auto name = processor.getRemotePeerUserName(i);
        if (name.isEmpty()) continue;

        int start = 0, count = 0;
        bool stereo = processor.getRemotePeerChannelGroupStartAndCount(i, 0, start, count) && count == 2;
        float pan = processor.getRemotePeerChannelPan(i, 0, 0);
        if (stereo) pan = 0.5f * (pan + processor.getRemotePeerChannelPan(i, 0, 1));

        DynamicObject::Ptr p = new DynamicObject();
        p->setProperty("id", name);
        auto vid = peerVideoIds.find(name);
        if (vid != peerVideoIds.end()) p->setProperty("vid", vid->second);
        p->setProperty("gain", processor.getRemotePeerLevelGain(i));
        p->setProperty("pan", pan);
        p->setProperty("stereo", stereo);
        p->setProperty("muted", !processor.getRemotePeerRecvAllow(i));
        p->setProperty("soloed", processor.getRemotePeerSoloed(i));
        p->setProperty("connected", processor.getRemotePeerConnected(i));
        p->setProperty("level", meterLevel(processor.getRemotePeerRecvMeterSource(i)));

        SonobusAudioProcessor::LatencyInfo lat;
        if (processor.getRemotePeerLatencyInfo(i, lat)) {
            p->setProperty("delayMs", round1(oneWayDelayMs(lat.outgoingMs, lat.totalRoundtripMs)));
            p->setProperty("pingMs", round1(lat.pingMs));
        }

        bool initDone = false;
        p->setProperty("bufferMs", round1(processor.getRemotePeerBufferTime(i)));
        p->setProperty("bufMode", String(bufferChoiceName(bufferChoiceFor((int) processor.getRemotePeerAutoresizeBufferMode(i, initDone)))));

        SonobusAudioProcessor::AudioCodecFormatInfo f;
        int requested = processor.getRequestRemotePeerSendAudioCodecFormat(i);
        bool haveRequested = requested >= 0 && processor.getAudioCodeFormatInfo(requested, f);
        p->setProperty("quality", String(qualityName(qualityFor(haveRequested ? requested : -1,
                                                                haveRequested && f.codec == SonobusAudioProcessor::CodecOpus,
                                                                haveRequested ? f.bitrate : 0))));
        if (processor.getRemotePeerReceiveAudioCodecFormat(i, f))
            p->setProperty("recvFormat", f.name + (processor.getRemotePeerRecvChannelCount(i) == 2 ? ", stereo" : ", mono"));
        if (processor.getAudioCodeFormatInfo(processor.getRemotePeerAudioCodecFormat(i), f))
            p->setProperty("sendFormat", f.name);

        auto received = processor.getRemotePeerPacketsReceived(i);
        if (received > 0) p->setProperty("dropped", round1(100.0f * (float) processor.getRemotePeerPacketsDropped(i) / (float) received));

        peers.add(var(p.get()));
    }

    DynamicObject::Ptr me = new DynamicObject();
    me->setProperty("name", info.myName);
    me->setProperty("role", info.teacher ? "teacher" : "learner");
    me->setProperty("micOff", getParam(SonobusAudioProcessor::paramMainSendMute) > 0.5f);
    me->setProperty("inGain", getParam(SonobusAudioProcessor::paramInGain));
    me->setProperty("monitor", getParam(SonobusAudioProcessor::paramDry) > 0.001f);
    me->setProperty("recvMute", getParam(SonobusAudioProcessor::paramMainRecvMute) > 0.5f);
    me->setProperty("volume", getParam(SonobusAudioProcessor::paramWet));
    me->setProperty("level", meterLevel(&processor.getInputMeterSource()));
    if (hooks.deviceNames) {
        String in, out;
        hooks.deviceNames(in, out);
        me->setProperty("inDevice", in.substring(0, 80));
        me->setProperty("outDevice", out.substring(0, 80));
    }

    // the lesson room: connecting at first, connected once joined, lost if it goes away
    bool connected = processor.isConnectedToServer() && processor.getCurrentJoinedGroup().isNotEmpty();
    if (connected) everConnected = true;
    String conn = info.practice ? "practice"
                : connected ? "connected"
                : (!everConnected && Time::currentTimeMillis() - joinedAtMs < 20000) ? "connecting" : "lost";

    DynamicObject::Ptr lesson = new DynamicObject();
    lesson->setProperty("title", info.title);
    lesson->setProperty("startsAt", info.startsAtMs);
    lesson->setProperty("endsAt", info.endsAtMs);
    lesson->setProperty("joinedAt", joinedAtMs);

    DynamicObject::Ptr rec = new DynamicObject();
    rec->setProperty("phase", hooks.recordPhase ? hooks.recordPhase() : String("idle"));
    rec->setProperty("secs", hooks.recordSeconds ? std::floor(hooks.recordSeconds()) : 0.0);

    DynamicObject::Ptr docks = new DynamicObject();
    docks->setProperty("tools", toolsOpen);
    docks->setProperty("board", hooks.boardOpen && hooks.boardOpen());

    DynamicObject::Ptr msg = new DynamicObject();
    msg->setProperty("t", "state");
    msg->setProperty("me", var(me.get()));
    msg->setProperty("conn", conn);
    msg->setProperty("lesson", var(lesson.get()));
    msg->setProperty("peers", peers);
    msg->setProperty("rec", var(rec.get()));
    msg->setProperty("docks", var(docks.get()));
    page->send(var(msg.get()));
}

void LessonView::notesArrived(const std::vector<NoteEvent> & events)
{
    // for lesson modules in the page (T-075): [type, channel, note, value, source]
    Array<var> list;
    for (auto & ev : events) {
        if (list.size() >= 256) break;
        Array<var> e;
        e.add((int) ev.type); e.add((int) ev.channel); e.add((int) ev.note); e.add((int) ev.value); e.add((int) ev.source);
        list.add(var(e));
    }
    if (list.isEmpty()) return;
    DynamicObject::Ptr msg = new DynamicObject();
    msg->setProperty("t", "notes");
    msg->setProperty("e", list);
    page->send(var(msg.get()));
}

void LessonView::sendNewChat()
{
    Array<var> items;
    {
        auto & events = processor.getAllChatEvents();
        const ScopedLock sl(events.getLock());
        if (chatIndex > events.size()) chatIndex = events.size();   // the list was cleared
        for (; chatIndex < events.size() && items.size() < 200; ++chatIndex) {
            const auto & ev = events.getReference(chatIndex);
            // people's messages only: no video ids or recording messages (tags), and none of SonoBus's
            // own "joined group" lines (the tiles already show who is here)
            if (ev.tags.isNotEmpty() || ev.message.isEmpty() || ev.type == SBChatEvent::SystemType) continue;
            DynamicObject::Ptr m = new DynamicObject();
            m->setProperty("from", ev.from.substring(0, 80));
            m->setProperty("text", ev.message.substring(0, 2000));
            if (ev.type == SBChatEvent::SelfType) m->setProperty("me", true);
            items.add(var(m.get()));
        }
    }
    if (items.isEmpty()) return;
    DynamicObject::Ptr msg = new DynamicObject();
    msg->setProperty("t", "chat");
    msg->setProperty("items", items);
    page->send(var(msg.get()));
}

void LessonView::handleMessage(const var & message)
{
    const auto type = message.getProperty("t", var()).toString();
    const auto value = message.getProperty("v", var());
    const bool isNumber = value.isDouble() || value.isInt();

    if (type == "ready") {
        sendState();
    }
    else if (type == "gain" || type == "pan") {
        int i = findPeerIndexByName(message.getProperty("id", var()).toString());
        if (i < 0 || !isNumber) return;
        if (type == "gain") {
            processor.setRemotePeerLevelGain(i, jlimit(0.0f, 2.0f, (float) value));
            return;
        }
        float centre = jlimit(-1.0f, 1.0f, (float) value);
        int start = 0, count = 0;
        if (processor.getRemotePeerChannelGroupStartAndCount(i, 0, start, count) && count == 2) {
            // stereo: move both sides together and keep their spread
            float p1 = processor.getRemotePeerChannelPan(i, 0, 0), p2 = processor.getRemotePeerChannelPan(i, 0, 1);
            float half = 0.5f * (p2 - p1);
            processor.setRemotePeerChannelPan(i, 0, 0, jlimit(-1.0f, 1.0f, centre - half));
            processor.setRemotePeerChannelPan(i, 0, 1, jlimit(-1.0f, 1.0f, centre + half));
        }
        else {
            processor.setRemotePeerChannelPan(i, 0, 0, centre);
        }
    }
    else if (type == "peer") {
        int i = findPeerIndexByName(message.getProperty("id", var()).toString());
        if (i >= 0) handlePeerMessage(i, message.getProperty("k", var()).toString(), value);
    }
    else if (type == "mic") {
        setParam(SonobusAudioProcessor::paramMainSendMute, (bool) message.getProperty("off", var()) ? 1.0f : 0.0f);
    }
    else if (type == "inGain" && isNumber) {
        setParam(SonobusAudioProcessor::paramInGain, jlimit(0.0f, 4.0f, (float) value));
    }
    else if (type == "volume" && isNumber) {
        setParam(SonobusAudioProcessor::paramWet, jlimit(0.0f, 2.0f, (float) value));
    }
    else if (type == "monitor") {
        // keep a monitor level set in All settings: off remembers it, on brings it back
        float now = getParam(SonobusAudioProcessor::paramDry);
        if ((bool) value) setParam(SonobusAudioProcessor::paramDry, now > 0.001f ? now : monitorLevel);
        else {
            if (now > 0.001f) monitorLevel = now;
            setParam(SonobusAudioProcessor::paramDry, 0.0f);
        }
    }
    else if (type == "recvMute") {
        setParam(SonobusAudioProcessor::paramMainRecvMute, (bool) value ? 1.0f : 0.0f);
    }
    else if (type == "tools") {
        setToolsOpen(!toolsOpen);
    }
    else if (type == "dock") {
        toolsRect = {};
        if (auto * a = message.getProperty("rect", var()).getArray(); a != nullptr && a->size() == 4) {
            float v[4];
            bool ok = true;
            for (int k = 0; k < 4; ++k) {
                const var & x = (*a)[k];
                v[k] = (x.isDouble() || x.isInt()) ? (float) x : NAN;
                ok = ok && std::isfinite(v[k]);
            }
            juce::Rectangle<float> r(v[0], v[1], v[2], v[3]);
            if (ok && r.getWidth() > 0.0f && r.getHeight() > 0.0f) toolsRect = r.getIntersection({ 0.0f, 0.0f, 1.0f, 1.0f });
        }
        placeTools();
    }
    else if (type == "chat") {
        auto text = message.getProperty("text", var()).toString().trim().substring(0, 2000);
        if (text.isEmpty() || processor.getCurrentJoinedGroup().isEmpty()) return;
        SBChatEvent ev;
        ev.from = processor.getCurrentUsername();   // the name we joined with ("Mia 2" after a clash)
        ev.group = processor.getCurrentJoinedGroup();
        ev.message = text;
        processor.sendChatEvent(ev);
        ev.type = SBChatEvent::SelfType;   // the processor only keeps what it receives
        processor.getAllChatEvents().add(ev);
        sendNewChat();
    }
    else if (type == "rec") {
        auto a = message.getProperty("a", var()).toString();
        if (a == "ask" && hooks.recordAsk) hooks.recordAsk();
        if (a == "stop" && hooks.recordStop) hooks.recordStop();
    }
    else if (type == "mark") {
        auto a = message.getProperty("a", var()).toString();
        if (a == "start" && hooks.markStart) hooks.markStart();
        if (a == "save" && hooks.markSave) hooks.markSave(message.getProperty("label", var()).toString().trim().substring(0, 80));
    }
    else if (type == "board")    { if (hooks.toggleBoard) hooks.toggleBoard(); }
    else if (type == "setup")    { if (hooks.openSetup) hooks.openSetup(); }
    else if (type == "settings") { if (hooks.openSettings) hooks.openSettings(); }
    else if (type == "effects")  { if (info.teacher && hooks.openEffects) hooks.openEffects(); }
    else if (type == "help")     { if (hooks.openHelp) hooks.openHelp(); }
    else if (type == "leave")    { if (hooks.leave) hooks.leave(); }
}

void LessonView::handlePeerMessage(int i, const String & kind, const var & value)
{
    using namespace LessonRules;

    if (kind == "mute") {
        processor.setRemotePeerRecvAllow(i, !(bool) value);
    }
    else if (kind == "solo") {
        if (info.teacher) processor.setRemotePeerSoloed(i, (bool) value);
    }
    else if (kind == "quality") {
        Quality q;
        if (!parseQuality(value.toString().toStdString(), q)) return;
        int format = q == Quality::Auto ? -1 : findOpusFormat(bitrateFor(q));
        if (q != Quality::Auto && format < 0) return;
        processor.setRequestRemotePeerSendAudioCodecFormat(i, format);
    }
    else if (kind == "buffer") {
        BufferChoice c;
        if (!parseBufferChoice(value.toString().toStdString(), c)) return;
        processor.setRemotePeerAutoresizeBufferMode(i, (SonobusAudioProcessor::AutoNetBufferMode) bufferModeFor(c));
    }
    else if (kind == "resetBuffer") {
        processor.setRemotePeerBufferTime(i, 0.0f);   // as the app's own reset: starts small and settles again
    }
    else if (kind == "remove") {
        if (info.teacher) processor.removeRemotePeer(i);
    }
}

#else

class LessonView::ToolsDock : public Component {};

std::unique_ptr<LessonView> LessonView::create(SonobusAudioProcessor &, const VideoInfo &, const LessonInfo &, Hooks) { return nullptr; }
LessonView::~LessonView() = default;
void LessonView::resized() {}
void LessonView::paint(Graphics &) {}
void LessonView::setPeerVideoId(const String &, const String &) {}
void LessonView::showToast(const String &, int) {}
void LessonView::setToolsOpen(bool) {}
ToolsPanel * LessonView::getTools() const { return nullptr; }
void LessonView::timerCallback() {}
void LessonView::notesArrived(const std::vector<NoteEvent> &) {}

#endif

}
