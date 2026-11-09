// SPDX-License-Identifier: GPLv3-or-later WITH Appstore-exception
// Copyright (C) 2026 Tin Can Studios

#include "TinCanSummaries.h"
#include "TinCanTheme.h"

namespace TinCan {

static const Colour kBackground(TinCanTheme::surface);
static const Colour kText(0xffeeeeee);
static const Colour kMuted(0xff9aa3b2);
static const Colour kSelected(0xff2f4f6f);

// start a little before the highlight so the moment is heard in context
static constexpr double kLeadIn = 2.0;

static String describeDate(const Time & t)
{
    return t.formatted("%a %e %b %Y, %H:%M").trim();
}

class SummariesPanel::LessonList : public ListBoxModel
{
public:
    explicit LessonList(SummariesPanel & p) : panel(p) {}

    int getNumRows() override { return panel.summaries.size(); }

    void paintListBoxItem(int row, Graphics & g, int width, int height, bool isSelected) override
    {
        if (!isPositiveAndBelow(row, panel.summaries.size())) return;
        auto & s = panel.summaries.getReference(row);
        if (isSelected) g.fillAll(kSelected);
        auto r = juce::Rectangle<int>(0, 0, width, height).reduced(8, 3);
        g.setColour(kText);
        g.setFont(Font(14.0f));
        g.drawText(s.title.isNotEmpty() ? s.title : TRANS("Recording"), r.removeFromTop(r.getHeight() / 2), Justification::centredLeft, true);
        g.setColour(kMuted);
        g.setFont(Font(12.0f));
        String sub = describeDate(s.started) + "  ·  " + formatTime(s.durationSeconds);
        if (s.highlights.size() > 0) sub << "  ·  " << s.highlights.size() << " " << (s.highlights.size() == 1 ? TRANS("highlight") : TRANS("highlights"));
        g.drawText(sub, r, Justification::centredLeft, true);
    }

    void selectedRowsChanged(int) override { panel.showSelected(); }

    String getNameForRow(int row) override
    {
        if (!isPositiveAndBelow(row, panel.summaries.size())) return {};
        auto & s = panel.summaries.getReference(row);
        return s.title + ", " + describeDate(s.started);
    }

private:
    SummariesPanel & panel;
};

class SummariesPanel::HighlightList : public ListBoxModel
{
public:
    explicit HighlightList(SummariesPanel & p) : panel(p) {}

    int getNumRows() override
    {
        auto * s = panel.selected();
        return s ? s->highlights.size() : 0;
    }

    void paintListBoxItem(int row, Graphics & g, int width, int height, bool isSelected) override
    {
        auto * s = panel.selected();
        if (!s || !isPositiveAndBelow(row, s->highlights.size())) return;
        auto & h = s->highlights.getReference(row);
        if (isSelected) g.fillAll(kSelected);
        auto r = juce::Rectangle<int>(0, 0, width, height).reduced(8, 0);
        g.setFont(Font(14.0f));
        g.setColour(Colour(0xfff0a030));
        g.drawText(formatTime(h.seconds), r.removeFromLeft(64), Justification::centredLeft, false);
        g.setColour(kMuted);
        g.setFont(Font(12.0f));
        if (h.by.isNotEmpty()) g.drawText(h.by, r.removeFromRight(jmin(140, r.getWidth() / 3)), Justification::centredRight, true);
        g.setColour(kText);
        g.setFont(Font(14.0f));
        g.drawText(s->getHighlightLabel(row), r, Justification::centredLeft, true);
    }

    void listBoxItemDoubleClicked(int row, const MouseEvent &) override { panel.playHighlight(row); }
    void returnKeyPressed(int row) override { panel.playHighlight(row); }
    void selectedRowsChanged(int) override { panel.showSelected(); }

    String getNameForRow(int row) override
    {
        auto * s = panel.selected();
        if (!s || !isPositiveAndBelow(row, s->highlights.size())) return {};
        auto & h = s->highlights.getReference(row);
        return formatTime(h.seconds) + ", " + s->getHighlightLabel(row) + (h.by.isNotEmpty() ? ", " + h.by : String());
    }

private:
    SummariesPanel & panel;
};

SummariesPanel::SummariesPanel(SummaryHooks h)
    : hooks(std::move(h))
{
    lessonModel = std::make_unique<LessonList>(*this);
    highlightModel = std::make_unique<HighlightList>(*this);

    for (auto * lb : { &lessons, &highlights }) {
        lb->setColour(ListBox::backgroundColourId, Colour(0xff151515));
        lb->setColour(ListBox::outlineColourId, TinCanTheme::line);
        lb->setOutlineThickness(1);
        addAndMakeVisible(lb);
    }
    lessons.setModel(lessonModel.get());
    lessons.setRowHeight(40);
    lessons.setTitle(TRANS("Recorded lessons"));
    highlights.setModel(highlightModel.get());
    highlights.setRowHeight(26);
    highlights.setTitle(TRANS("Highlights"));

    emptyLabel.setText(TRANS("No recorded lessons yet. Press Record during a lesson: everyone is asked first, and each of you gets a copy. "
                             "While it records, press Mark at moments worth coming back to. When it stops, the summary shows here."),
                       dontSendNotification);
    emptyLabel.setJustificationType(Justification::topLeft);
    emptyLabel.setColour(Label::textColourId, kMuted);
    addChildComponent(emptyLabel);

    titleLabel.setFont(Font(17.0f, Font::bold));
    titleLabel.setColour(Label::textColourId, kText);
    infoLabel.setColour(Label::textColourId, kMuted);
    infoLabel.setJustificationType(Justification::topLeft);
    highlightsHeading.setText(TRANS("Highlights (double-click to play)"), dontSendNotification);
    highlightsHeading.setColour(Label::textColourId, kMuted);
    for (auto * l : { &titleLabel, &infoLabel, &highlightsHeading }) addAndMakeVisible(l);

    playStartButton.onClick = [this]() {
        if (auto * s = selected()) if (hooks.play) hooks.play(s->getAudioFile(), 0.0);
    };
    playHighlightButton.onClick = [this]() { playHighlight(highlights.getSelectedRow()); };
    stopButton.onClick = [this]() { if (hooks.stop) hooks.stop(); };
    folderButton.onClick = [this]() {
        if (auto * s = selected()) {
            auto f = s->getAudioFile();
            (f.existsAsFile() ? f : s->summaryFile).revealToUser();
        }
    };
    deleteButton.onClick = [this]() { deleteSelected(); };
    for (auto * b : { &playStartButton, &playHighlightButton, &stopButton, &folderButton, &deleteButton }) addAndMakeVisible(b);

    refresh();
}

SummariesPanel::~SummariesPanel()
{
    lessons.setModel(nullptr);
    highlights.setModel(nullptr);
}

const LessonSummary * SummariesPanel::selected() const
{
    auto row = lessons.getSelectedRow();
    return isPositiveAndBelow(row, summaries.size()) ? &summaries.getReference(row) : nullptr;
}

void SummariesPanel::refresh(const File & select)
{
    File keep = select;
    if (keep == File() && selected()) keep = selected()->summaryFile;

    summaries = hooks.getFolder ? findLessonSummaries(hooks.getFolder()) : Array<LessonSummary>();
    lessons.updateContent();

    int row = summaries.isEmpty() ? -1 : 0;
    for (int i = 0; i < summaries.size(); ++i) {
        if (summaries.getReference(i).summaryFile == keep) row = i;
    }
    if (row >= 0) lessons.selectRow(row);
    else lessons.deselectAllRows();

    emptyLabel.setVisible(summaries.isEmpty());
    showSelected();
    resized();
}

void SummariesPanel::showSelected()
{
    auto * s = selected();
    highlights.updateContent();
    highlights.repaint();

    bool have = s != nullptr;
    bool audio = have && s->getAudioFile().existsAsFile();
    for (auto * c : std::initializer_list<Component *> { &lessons, &highlights, &titleLabel, &infoLabel, &highlightsHeading,
                                                         &playStartButton, &playHighlightButton, &stopButton, &folderButton, &deleteButton }) {
        c->setVisible(!summaries.isEmpty());
    }
    playStartButton.setEnabled(audio);
    playHighlightButton.setEnabled(audio && highlights.getSelectedRow() >= 0);
    folderButton.setEnabled(have);
    deleteButton.setEnabled(have);

    if (!have) {
        titleLabel.setText({}, dontSendNotification);
        infoLabel.setText({}, dontSendNotification);
        return;
    }

    titleLabel.setText(s->title.isNotEmpty() ? s->title : TRANS("Recording"), dontSendNotification);
    String info = describeDate(s->started) + "  ·  " + formatTime(s->durationSeconds);
    if (s->people.size() > 0) info << "\n" << TRANS("With:") << " " << s->people.joinIntoString(", ");
    if (!audio) info << "\n" << TRANS("The recording file is missing (moved or deleted). The highlights are still listed.");
    if (s->highlights.isEmpty()) info << "\n" << TRANS("No highlights were marked in this recording.");
    infoLabel.setText(info, dontSendNotification);
}

void SummariesPanel::playHighlight(int row)
{
    auto * s = selected();
    if (!s || !isPositiveAndBelow(row, s->highlights.size()) || !hooks.play) return;
    hooks.play(s->getAudioFile(), jmax(0.0, s->highlights.getReference(row).seconds - kLeadIn));
}

void SummariesPanel::deleteSelected()
{
    auto * s = selected();
    if (!s) return;
    auto summaryFile = s->summaryFile;
    auto audio = s->getAudioFile();

    Component::SafePointer<SummariesPanel> safeThis(this);
    // a plain AlertWindow: its buttons are real buttons for screen readers (the OK/Cancel box's aren't)
    auto * confirm = new AlertWindow(TRANS("Delete this recording?"),
                                     TRANS("This moves the recording and its highlights to the Recycle Bin. "
                                           "Other people's copies are not affected."),
                                     MessageBoxIconType::WarningIcon, this);
    confirm->addButton(TRANS("Delete"), 1);
    confirm->addButton(TRANS("Cancel"), 0, KeyPress(KeyPress::escapeKey));
    confirm->enterModalState(true, ModalCallbackFunction::create([safeThis, summaryFile, audio](int ok) {
        if (ok != 1 || !safeThis) return;  // 1 = Delete
        if (safeThis->hooks.stop) safeThis->hooks.stop();
        if (safeThis->hooks.unload) safeThis->hooks.unload(audio);
        // keep the highlights unless the recording really went: never leave a recording without its summary
        if (audio.existsAsFile() && !audio.moveToTrash()) {
            AlertWindow::showMessageBoxAsync(MessageBoxIconType::WarningIcon, TRANS("Couldn't delete the recording"),
                                             TRANS("The recording file is in use or can't be moved to the Recycle Bin. Close other apps using it and try again."),
                                             TRANS("OK"), safeThis.getComponent());
            safeThis->refresh();
            return;
        }
        summaryFile.moveToTrash();
        safeThis->refresh();
    }), true);  // deleted when dismissed
}

void SummariesPanel::visibilityChanged()
{
    if (isShowing()) refresh();
}

void SummariesPanel::paint(Graphics & g)
{
    g.fillAll(kBackground);
}

void SummariesPanel::resized()
{
    auto r = getLocalBounds().reduced(10);
    emptyLabel.setBounds(r.removeFromTop(80));
    r = getLocalBounds().reduced(10);

    lessons.setBounds(r.removeFromLeft(jlimit(220, 360, r.getWidth() / 3)));
    r.removeFromLeft(12);

    auto buttons = r.removeFromBottom(30);
    for (auto * b : { &playStartButton, &playHighlightButton, &stopButton }) {
        b->setBounds(buttons.removeFromLeft(jmin(120, buttons.getWidth() / 5)));
        buttons.removeFromLeft(6);
    }
    deleteButton.setBounds(buttons.removeFromRight(jmin(90, buttons.getWidth() / 2)));
    buttons.removeFromRight(6);
    folderButton.setBounds(buttons.removeFromRight(jmin(120, buttons.getWidth())));
    r.removeFromBottom(8);

    titleLabel.setBounds(r.removeFromTop(24));
    infoLabel.setBounds(r.removeFromTop(48));
    highlightsHeading.setBounds(r.removeFromTop(22));
    highlights.setBounds(r);
}

}
