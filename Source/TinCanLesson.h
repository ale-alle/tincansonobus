// SPDX-License-Identifier: GPLv3-or-later WITH Appstore-exception
// Copyright (C) 2026 Tin Can Studios

#pragma once

#include "JuceHeader.h"
#include "SonobusPluginProcessor.h"
#include "TinCanVideoTiles.h"
#include "TinCanTools.h"

#include <functional>
#include <map>

// The lesson window (T-070, D-028).
//
// During a lesson one web page (video-page/lesson.html, served next to the video page) covers the
// whole main window: the people as video tiles with the red string between them, each person's sound
// on hover and behind the tile, the lesson bar and all its popovers. There are no SonoBus audio rows.
// The only native part on screen is the Tools panel, docked into a gap the page leaves for it.
//
// WebView2 is a native window of its own, so nothing JUCE draws inside the main window can appear on
// top of it. That is why everything that floats lives in the page, and why the docked Tools panel is a
// borderless child window (created after the web view, so it sits above it).
//
// Messages (JSON, WebView2 postMessage):
//   app  -> page  {"t":"state","me":{…},"conn":"connecting|connected|lost","lesson":{…},
//                  "peers":[{…}],"rec":{"phase","secs"},"docks":{"tools","board"}}      ~10 a second
//                 {"t":"chat","items":[{"from","text","me","sys"}]}                    new messages
//                 {"t":"toast","text","ms"}                                           a short notice
//   page -> app   {"t":"ready"}
//                 {"t":"gain"|"pan","id","v"}                    a person's volume (0..2) or pan (-1..1)
//                 {"t":"peer","id","k":"mute|solo|quality|buffer|resetBuffer|remove","v"}
//                 {"t":"mic","off"} {"t":"inGain","v"} {"t":"volume","v"} {"t":"monitor","v"} {"t":"recvMute","v"}
//                 {"t":"tools"} {"t":"board"} {"t":"setup"} {"t":"settings"} {"t":"effects"} {"t":"help"} {"t":"leave"}
//                 {"t":"rec","a":"ask|stop"} {"t":"mark","a":"start|save","label"}
//                 {"t":"chat","text"}
//                 {"t":"dock","rect":[x,y,w,h]}                  the Tools gap, as fractions of the page ([] = closed)
namespace TinCan {

// lesson page: TINCAN_LESSON_PAGE (https, or http to localhost), else <video page>lesson.html
String getLessonPage();

struct LessonInfo
{
    String title;
    String myName;
    int64 startsAtMs = 0;   // 0 when the server didn't say
    int64 endsAtMs = 0;
    bool teacher = true;    // a missing role (older server) gets the full controls, as before
};

class LessonView : public Component, private Timer
{
public:
    struct Hooks
    {
        std::function<void()> toggleBoard, openSetup, openSettings, openEffects, openHelp, leave, shareVideoId;
        std::function<void()> recordAsk, recordStop, markStart;
        std::function<void(const String &)> markSave;
        std::function<String()> recordPhase;      // "idle", "asking", "asked" or "recording"
        std::function<double()> recordSeconds;
        std::function<bool()> boardOpen;
        std::function<void(String & in, String & out)> deviceNames;
        std::function<std::unique_ptr<ToolsPanel>()> makeTools;
    };

    // returns nullptr when WebView2 can't start on this computer
    static std::unique_ptr<LessonView> create(SonobusAudioProcessor & processor, const VideoInfo & video,
                                              const LessonInfo & info, Hooks hooks);
    ~LessonView() override;

    void resized() override;
    void paint(Graphics & g) override;

    // the other app's video identity, from its peer message (TinCanVideoTiles.h)
    void setPeerVideoId(const String & userName, const String & identity);

    void showToast(const String & text, int timeoutMs = 5000);

    bool isToolsOpen() const { return toolsOpen; }
    void setToolsOpen(bool open);
    ToolsPanel * getTools() const;

private:
    class ToolsDock;

    LessonView(SonobusAudioProcessor & processor, std::unique_ptr<VideoTilesView> page, const LessonInfo & info, Hooks hooks);

    void timerCallback() override;
    void handleMessage(const var & message);
    void handlePeerMessage(int index, const String & kind, const var & value);
    void sendState();
    void sendNewChat();
    void placeTools();
    void setParam(const String & id, float value);
    float getParam(const String & id) const;
    int findPeerIndexByName(const String & name) const;
    int findOpusFormat(int bitrate) const;

    SonobusAudioProcessor & processor;
    std::unique_ptr<VideoTilesView> page;
    LessonInfo info;
    Hooks hooks;

    std::unique_ptr<ToolsDock> tools;
    bool toolsOpen = false;
    juce::Rectangle<float> toolsRect;   // fractions of the page; empty: not laid out yet

    std::map<String, String> peerVideoIds;   // audio name -> video identity
    int lastPeerCount = -1;
    int ticks = 0;
    int chatIndex = 0;
    int64 joinedAtMs = 0;
    bool everConnected = false;
    float monitorLevel = 1.0f;   // "Hear myself" level to go back to

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LessonView)
};

}
