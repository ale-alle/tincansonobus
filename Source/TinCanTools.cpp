// SPDX-License-Identifier: GPLv3-or-later WITH Appstore-exception
// Copyright (C) 2026 Tin Can Studios

#include "TinCanTools.h"
#include "SonobusPluginProcessor.h"

namespace TinCan {

Colour NoteColours::forNote(const NoteState & state, int note)
{
    Colour c;
    if (state.isSounding(note, NoteSource::Midi)) c = midi();
    else if (state.isSounding(note, NoteSource::Local)) c = local();
    else return Colours::transparentBlack;
    return state.isSustainedOnly(note) ? c.withAlpha(0.45f) : c;
}

static bool isBlackKey(int note)
{
    switch (note % 12) { case 1: case 3: case 6: case 8: case 10: return true; default: return false; }
}

//==============================================================================

KeyboardView::KeyboardView(MidiKeyboardState & mouseState, const NoteState & notesToShow)
    : MidiKeyboardComponent(mouseState, MidiKeyboardComponent::horizontalKeyboard), notes(notesToShow)
{
    setScrollButtonsVisible(false);
    setWantsKeyboardFocus(false);  // computer-keyboard notes would clash with the app's own keys
    setColour(keyDownOverlayColourId, Colours::transparentBlack);  // our own colours show notes
    setColour(mouseOverKeyOverlayColourId, Colour(0x30f0a030));
    setOctaveForMiddleC(middleC);
}

void KeyboardView::setNoteNames(bool all, bool flats, int middleCOctave)
{
    allNames = all;
    useFlats = flats;
    middleC = middleCOctave;
    setOctaveForMiddleC(middleC);
    repaint();
}

void KeyboardView::setRangeToFit(int lowNote, int highNote)
{
    fitLow = lowNote;
    fitHigh = highNote;
    resized();
}

void KeyboardView::resized()
{
    int whites = 0;
    for (int n = fitLow; n <= fitHigh; ++n) {
        if (!isBlackKey(n)) ++whites;
    }
    if (whites > 0 && getWidth() > 0) setKeyWidth(getWidth() / (float) whites);
    setAvailableRange(fitLow, fitHigh);
    setLowestVisibleKey(fitLow);
    MidiKeyboardComponent::resized();
}

void KeyboardView::drawWhiteNote(int note, Graphics & g, juce::Rectangle<float> area, bool, bool isOver,
                                 Colour lineColour, Colour textColour)
{
    auto fill = NoteColours::forNote(notes, note);
    if (!fill.isTransparent()) {
        g.setColour(fill);
        g.fillRect(area);
        // both sources at once: a mouse-coloured band at the bottom
        if (notes.isSounding(note, NoteSource::Midi) && notes.isSounding(note, NoteSource::Local)) {
            g.setColour(NoteColours::local());
            g.fillRect(area.withTop(area.getBottom() - area.getHeight() * 0.25f));
        }
    }
    MidiKeyboardComponent::drawWhiteNote(note, g, area, false, isOver, lineColour,
                                         fill.isTransparent() ? textColour : Colours::black);
}

void KeyboardView::drawBlackNote(int note, Graphics & g, juce::Rectangle<float> area, bool, bool isOver,
                                 Colour noteFillColour)
{
    MidiKeyboardComponent::drawBlackNote(note, g, area, false, isOver, noteFillColour);

    auto fill = NoteColours::forNote(notes, note);
    if (!fill.isTransparent()) {
        g.setColour(fill);
        g.fillRect(area.reduced(area.getWidth() * 0.12f, 0).withTrimmedBottom(area.getWidth() * 0.12f));
    }

    if (allNames && area.getWidth() >= 11.0f) {
        g.setColour(fill.isTransparent() ? Colours::white.withAlpha(0.8f) : Colours::black);
        g.setFont(Font(jmin(11.0f, area.getWidth() * 0.8f)).withHorizontalScale(0.8f));
        g.drawText(String(noteName(note, useFlats)), area.withTrimmedBottom(3.0f), Justification::centredBottom, false);
    }
}

String KeyboardView::getWhiteNoteText(int note)
{
    if (note % 12 == 0) return String(noteLabel(note, useFlats, middleC));
    if (allNames && getKeyWidth() >= 14.0f) return String(noteName(note, useFlats));
    return {};
}

//==============================================================================

struct RangePreset { const char * name; int low; int keys; };

static const RangePreset rangePresets[] = {
    { "25 keys", 48, 25 },   // C3-C5
    { "37 keys", 48, 37 },   // C3-C6
    { "49 keys", 36, 49 },   // C2-C6
    { "61 keys", 36, 61 },   // C2-C7
    { "88 keys", 21, 88 },   // A0-C8, full piano
};

InstrumentPanel::InstrumentPanel(SonobusAudioProcessor & proc, std::function<AudioDeviceManager*()> getDM)
    : processor(proc), getDeviceManager(std::move(getDM)), keyboard(mouseState, notes)
{
    addAndMakeVisible(keyboard);
    mouseState.addListener(this);

    int id = 1;
    for (auto & p : rangePresets) rangeChoice.addItem(TRANS(p.name), id++);
    rangeChoice.setSelectedId(2, dontSendNotification);
    rangeChoice.setTooltip(TRANS("How many keys to show"));
    rangeChoice.onChange = [this]() {
        auto & p = rangePresets[jlimit(0, (int) std::size(rangePresets) - 1, rangeChoice.getSelectedId() - 1)];
        lowNote = p.low;
        numKeys = p.keys;
        updateRange();
    };
    addAndMakeVisible(rangeChoice);

    octaveDown.setTooltip(TRANS("Octave down"));
    octaveUp.setTooltip(TRANS("Octave up"));
    octaveDown.onClick = [this]() { lowNote -= 12; updateRange(); };
    octaveUp.onClick = [this]() { lowNote += 12; updateRange(); };
    addAndMakeVisible(octaveDown);
    addAndMakeVisible(octaveUp);

    namesToggle.onClick = [this]() { updateNames(); };
    flatsToggle.onClick = [this]() { updateNames(); };
    addAndMakeVisible(namesToggle);
    addAndMakeVisible(flatsToggle);

    middleCChoice.addItem(TRANS("Middle C = C4"), 4);
    middleCChoice.addItem(TRANS("Middle C = C3"), 3);
    middleCChoice.setSelectedId(4, dontSendNotification);
    middleCChoice.setTooltip(TRANS("Octave numbering. Notes are always named at concert pitch."));
    middleCChoice.onChange = [this]() { updateNames(); };
    addAndMakeVisible(middleCChoice);

    midiStatus.setFont(Font(13.0f));
    midiStatus.setColour(Label::textColourId, Colour(0xccffffff));
    addAndMakeVisible(midiStatus);

    if (auto * dm = getDeviceManager ? getDeviceManager() : nullptr) dm->addChangeListener(this);
    updateMidiStatus();
    updateRange();

    // drop anything left from an earlier session, then start copying MIDI
    NoteEvent stale;
    while (processor.popNoteEvent(stale)) {}
    processor.setNoteTapEnabled(true);
    startTimerHz(30);
}

InstrumentPanel::~InstrumentPanel()
{
    processor.setNoteTapEnabled(false);
    if (auto * dm = getDeviceManager ? getDeviceManager() : nullptr) dm->removeChangeListener(this);
    mouseState.removeListener(this);
}

void InstrumentPanel::paint(Graphics & g)
{
    g.fillAll(Colour(0xff1c1c1c));

    // legend: swatch + text for each kind of note
    struct Item { Colour colour; String text; };
    const Item items[] = {
        { NoteColours::midi(), TRANS("MIDI input") },
        { NoteColours::local(), TRANS("Mouse") },
        { NoteColours::midi().withAlpha(0.45f), TRANS("Held by pedal") },
    };
    Font font(12.0f);
    g.setFont(font);
    auto r = legendArea;
    for (int i = (int) std::size(items) - 1; i >= 0; --i) {
        int tw = font.getStringWidth(items[i].text) + 2;
        auto textArea = r.removeFromRight(tw);
        auto swatch = r.removeFromRight(14).withSizeKeepingCentre(10, 10);
        r.removeFromRight(10);
        g.setColour(items[i].colour);
        g.fillRoundedRectangle(swatch.toFloat(), 2.0f);
        g.setColour(Colour(0x99ffffff));
        g.drawText(items[i].text, textArea, Justification::centredLeft, false);
    }
}

void InstrumentPanel::resized()
{
    auto r = getLocalBounds().reduced(8);
    auto row = r.removeFromTop(26);
    rangeChoice.setBounds(row.removeFromLeft(100));
    row.removeFromLeft(4);
    octaveDown.setBounds(row.removeFromLeft(28));
    octaveUp.setBounds(row.removeFromLeft(28));
    row.removeFromLeft(8);
    middleCChoice.setBounds(row.removeFromLeft(130));
    row.removeFromLeft(8);
    namesToggle.setBounds(row.removeFromLeft(124));
    flatsToggle.setBounds(row.removeFromLeft(64));

    r.removeFromTop(4);
    auto status = r.removeFromBottom(22);
    legendArea = status.removeFromRight(280);
    midiStatus.setBounds(status);
    r.removeFromBottom(4);
    keyboard.setBounds(r);
}

void InstrumentPanel::updateRange()
{
    lowNote = jlimit(0, 128 - numKeys, lowNote);
    bool full = numKeys >= 88;
    octaveDown.setEnabled(!full && lowNote - 12 >= 0);
    octaveUp.setEnabled(!full && lowNote + 12 + numKeys <= 128);
    keyboard.setRangeToFit(lowNote, lowNote + numKeys - 1);
}

void InstrumentPanel::updateNames()
{
    keyboard.setNoteNames(namesToggle.getToggleState(), flatsToggle.getToggleState(), middleCChoice.getSelectedId());
}

void InstrumentPanel::updateMidiStatus()
{
    auto * dm = getDeviceManager ? getDeviceManager() : nullptr;
    if (dm == nullptr) {
        midiStatus.setText(TRANS("Notes come from the MIDI sent to this plugin's track."), dontSendNotification);
        return;
    }
    StringArray names;
    for (auto & d : MidiInput::getAvailableDevices()) {
        if (dm->isMidiInputDeviceEnabled(d.identifier)) names.add(d.name);
    }
    midiStatus.setText(names.isEmpty()
                       ? TRANS("No MIDI input on. Turn one on in Settings > Audio, or click the keys.")
                       : TRANS("MIDI input:") + " " + names.joinIntoString(", "),
                       dontSendNotification);
}

void InstrumentPanel::changeListenerCallback(ChangeBroadcaster *)
{
    // device settings changed: notes held on a device that went away would stay lit
    NoteEvent ev;
    ev.type = NoteEvent::Reset;
    ev.source = NoteSource::Midi;
    notes.apply(ev);
    keyboard.repaint();
    updateMidiStatus();
}

void InstrumentPanel::timerCallback()
{
    NoteEvent ev;
    bool changed = false;
    while (processor.popNoteEvent(ev)) {
        notes.apply(ev);
        changed = true;
    }
    if (changed) keyboard.repaint();
}

void InstrumentPanel::handleNoteOn(MidiKeyboardState *, int channel, int note, float velocity)
{
    NoteEvent ev;
    ev.type = NoteEvent::NoteOn;
    ev.channel = (uint8_t) jlimit(0, 15, channel - 1);
    ev.note = (uint8_t) jlimit(0, 127, note);
    ev.value = (uint8_t) jlimit(1, 127, roundToInt(velocity * 127.0f));
    ev.source = NoteSource::Local;
    notes.apply(ev);
    keyboard.repaint();
}

void InstrumentPanel::handleNoteOff(MidiKeyboardState *, int channel, int note, float)
{
    NoteEvent ev;
    ev.type = NoteEvent::NoteOff;
    ev.channel = (uint8_t) jlimit(0, 15, channel - 1);
    ev.note = (uint8_t) jlimit(0, 127, note);
    ev.source = NoteSource::Local;
    notes.apply(ev);
    keyboard.repaint();
}

//==============================================================================

ToolsWindow::ToolsWindow(SonobusAudioProcessor & processor, std::function<AudioDeviceManager*()> getDeviceManager,
                         std::function<void()> onCloseFunc)
    : DocumentWindow(TRANS("Teaching tools"), Colour(0xff1c1c1c), DocumentWindow::allButtons),
      onClose(std::move(onCloseFunc))
{
    auto * tabs = new TabbedComponent(TabbedButtonBar::TabsAtTop);
    tabs->setTabBarDepth(28);
    tabs->addTab(TRANS("Instrument"), Colour(0xff1c1c1c), new InstrumentPanel(processor, getDeviceManager), true);

    setUsingNativeTitleBar(true);
    setContentOwned(tabs, false);
    setResizable(true, false);
    setResizeLimits(560, 220, 4000, 1200);
    centreWithSize(900, 300);
    setVisible(true);
}

ToolsWindow::~ToolsWindow()
{
    clearContentComponent();
}

void ToolsWindow::closeButtonPressed()
{
    if (onClose) onClose();
}

}
