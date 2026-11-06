// SPDX-License-Identifier: GPLv3-or-later WITH Appstore-exception
// Copyright (C) 2026 Tin Can Studios

#pragma once

#include "JuceHeader.h"
#include "TinCanRecording.h"

#include <functional>

// "Summaries" tab of the Tools window (T-046): recorded lessons on this computer, each with its
// highlights. Picking a highlight plays the recording from there through CanDuet's own player
// (the file area in the main window), so a teacher can also replay a moment to the student.
namespace TinCan {

struct SummaryHooks
{
    std::function<File()> getFolder;                         // where recordings are saved
    std::function<void(const File & audio, double seconds)> play;
    std::function<void()> stop;
    std::function<void(const File & audio)> unload;          // release the file if the player has it open
};

class SummariesPanel : public Component
{
public:
    explicit SummariesPanel(SummaryHooks hooks);
    ~SummariesPanel() override;

    // reload from disk and select this summary (or keep the current one)
    void refresh(const File & select = {});

    void paint(Graphics & g) override;
    void resized() override;
    void visibilityChanged() override;

private:
    class LessonList;
    class HighlightList;

    void showSelected();
    void playHighlight(int row);
    void deleteSelected();
    const LessonSummary * selected() const;

    SummaryHooks hooks;
    Array<LessonSummary> summaries;

    std::unique_ptr<LessonList> lessonModel;
    std::unique_ptr<HighlightList> highlightModel;
    ListBox lessons, highlights;
    Label emptyLabel, titleLabel, infoLabel, highlightsHeading;
    TextButton playStartButton { TRANS("Play from start") }, playHighlightButton { TRANS("Play highlight") },
               stopButton { TRANS("Stop") }, folderButton { TRANS("Show in folder") }, deleteButton { TRANS("Delete...") };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SummariesPanel)
};

}
