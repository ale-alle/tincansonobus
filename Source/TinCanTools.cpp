// SPDX-License-Identifier: GPLv3-or-later WITH Appstore-exception
// Copyright (C) 2026 Tin Can Studios

#include "TinCanTools.h"
#include "TinCanPracticeTools.h"
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
}

void KeyboardView::setNaming(const NoteNaming & newNaming)
{
    naming = newNaming;
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

    if (naming.allNames && area.getWidth() >= 11.0f) {
        g.setColour(fill.isTransparent() ? Colours::white.withAlpha(0.8f) : Colours::black);
        g.setFont(Font(jmin(11.0f, area.getWidth() * 0.8f)).withHorizontalScale(0.8f));
        g.drawText(naming.name(note), area.withTrimmedBottom(3.0f), Justification::centredBottom, false);
    }
}

String KeyboardView::getWhiteNoteText(int note)
{
    // C keys always carry an octave label; with a transposition the written name is shown
    if (note % 12 == 0) return naming.label(note);
    if (naming.allNames && getKeyWidth() >= 14.0f) return naming.name(note);
    return {};
}

//==============================================================================

FretboardView::FretboardView(const NoteState & notesToShow) : notes(notesToShow)
{
    setOpaque(true);
}

void FretboardView::setInstrument(const std::vector<int> & newStrings, int newFrets)
{
    strings = newStrings;
    frets = jmax(1, newFrets);
    repaint();
}

void FretboardView::setNaming(const NoteNaming & newNaming)
{
    naming = newNaming;
    repaint();
}

juce::Rectangle<float> FretboardView::neckArea() const
{
    // left margin holds the open-string names and open-string notes
    return getLocalBounds().toFloat().withTrimmedLeft(52.0f).withTrimmedRight(10.0f).reduced(0, 14.0f);
}

float FretboardView::fretX(int fret) const
{
    // real fret spacing (each fret 2^(-1/12) of the one before), scaled to fit the last fret
    auto neck = neckArea();
    auto pos = [](int f) { return 1.0 - std::pow(2.0, -f / 12.0); };
    return neck.getX() + (float) (pos(fret) / pos(frets)) * neck.getWidth();
}

float FretboardView::noteX(int fret) const
{
    if (fret == 0) return neckArea().getX() - 16.0f;
    return (fretX(fret - 1) + fretX(fret)) * 0.5f;
}

float FretboardView::stringY(int string) const
{
    // highest-pitched string at the top
    auto neck = neckArea();
    int n = (int) strings.size();
    if (n <= 1) return neck.getCentreY();
    int row = n - 1 - string;
    return neck.getY() + neck.getHeight() * (row + 0.5f) / (float) n;
}

int FretboardView::stringAt(float y) const
{
    int best = -1;
    float bestDist = 1e9f;
    for (int s = 0; s < (int) strings.size(); ++s) {
        float d = std::abs(stringY(s) - y);
        if (d < bestDist) { bestDist = d; best = s; }
    }
    float spacing = neckArea().getHeight() / (float) jmax(1, (int) strings.size());
    return bestDist <= spacing * 0.5f ? best : -1;
}

int FretboardView::fretAt(float x) const
{
    auto neck = neckArea();
    if (x < neck.getX()) return x >= neck.getX() - 32.0f ? 0 : -1;
    for (int f = 1; f <= frets; ++f) {
        if (x <= fretX(f)) return f;
    }
    return -1;
}

void FretboardView::paint(Graphics & g)
{
    g.fillAll(Colour(0xff1c1c1c));
    auto neck = neckArea();
    int n = (int) strings.size();
    if (n == 0) return;

    // wood, then inlays
    g.setColour(Colour(0xff3a2c22));
    g.fillRect(neck);
    g.setColour(Colour(0xffd8cfc0).withAlpha(0.55f));
    const int inlays[] = { 3, 5, 7, 9, 15, 17, 19, 21 };
    float dot = jmin(10.0f, neck.getHeight() / (float) n * 0.4f);
    for (int f : inlays) {
        if (f <= frets) g.fillEllipse(juce::Rectangle<float>(dot, dot).withCentre({ noteX(f), neck.getCentreY() }));
    }
    for (int f = 12; f <= frets; f += 12) {
        g.fillEllipse(juce::Rectangle<float>(dot, dot).withCentre({ noteX(f), neck.getY() + neck.getHeight() * 0.25f }));
        g.fillEllipse(juce::Rectangle<float>(dot, dot).withCentre({ noteX(f), neck.getY() + neck.getHeight() * 0.75f }));
    }

    // nut and frets, with numbers under the neck
    g.setColour(Colour(0xffe8e0d0));
    g.fillRect(neck.getX() - 3.0f, neck.getY(), 4.0f, neck.getHeight());
    g.setFont(Font(11.0f));
    for (int f = 1; f <= frets; ++f) {
        float x = fretX(f);
        g.setColour(Colour(0xffb0b0b0));
        g.fillRect(x - 1.0f, neck.getY(), 2.0f, neck.getHeight());
        if (f == 1 || f == 3 || f == 5 || f == 7 || f == 9 || f % 12 == 0 || f == 15 || f == 17 || f == 19 || f == 21) {
            g.setColour(Colour(0x99ffffff));
            g.drawText(String(f), juce::Rectangle<float>(noteX(f) - 12, neck.getBottom() + 1, 24, 12), Justification::centred);
        }
    }

    // strings (thicker for lower strings) and open-string names
    for (int s = 0; s < n; ++s) {
        float y = stringY(s);
        float thick = jmap((float) s, 0.0f, (float) jmax(1, n - 1), 3.0f, 1.0f);
        g.setColour(Colour(0xffcfcfcf));
        g.fillRect(neck.getX(), y - thick * 0.5f, neck.getWidth(), thick);
        g.setColour(Colour(0xccffffff));
        g.setFont(Font(12.0f));
        g.drawText(naming.label(strings[(size_t) s]), juce::Rectangle<float>(2, y - 8, 30, 16), Justification::centredLeft);
    }

    // sounding notes, every place they can be played
    float r = jmin(11.0f, neck.getHeight() / (float) n * 0.45f);
    g.setFont(Font(jmin(11.0f, r * 1.1f)).withHorizontalScale(0.85f));
    for (int note = 0; note < 128; ++note) {
        auto fill = NoteColours::forNote(notes, note);
        if (fill.isTransparent()) continue;
        for (auto & p : fretPositions(strings, frets, note)) {
            auto circle = juce::Rectangle<float>(r * 2, r * 2).withCentre({ noteX(p.fret), stringY(p.string) });
            g.setColour(fill);
            g.fillEllipse(circle);
            if (r >= 7.0f) {
                g.setColour(Colours::black);
                g.drawText(naming.name(note), circle, Justification::centred, false);
            }
        }
    }
}

void FretboardView::mouseDown(const MouseEvent & e)
{
    int s = stringAt(e.position.y);
    int f = fretAt(e.position.x);
    if (s < 0 || f < 0) return;
    mouseNote = strings[(size_t) s] + f;
    if (onMousePlay) onMousePlay(mouseNote, true);
}

void FretboardView::mouseUp(const MouseEvent &)
{
    if (mouseNote >= 0 && onMousePlay) onMousePlay(mouseNote, false);
    mouseNote = -1;
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

static constexpr int customTuningId = 1000;

InstrumentPanel::InstrumentPanel(SonobusAudioProcessor & proc, std::function<AudioDeviceManager*()> getDM)
    : processor(proc), getDeviceManager(std::move(getDM)), keyboard(mouseState, notes), fretboard(notes)
{
    addAndMakeVisible(keyboard);
    addChildComponent(fretboard);
    mouseState.addListener(this);
    fretboard.onMousePlay = [this](int note, bool on) { playLocal(note, on, 1, 0.8f); };

    int id = 1;
    for (auto & inst : getInstruments()) instrumentChoice.addItem(TRANS(String(inst.name)), id++);
    instrumentChoice.setTitle(TRANS("Instrument"));
    instrumentChoice.setTooltip(TRANS("Instrument to show. The MIDI input stays connected when you switch."));
    instrumentChoice.onChange = [this]() { selectInstrument(instrumentChoice.getSelectedId() - 1); };
    addAndMakeVisible(instrumentChoice);

    id = 1;
    for (auto & p : rangePresets) rangeChoice.addItem(TRANS(p.name), id++);
    rangeChoice.setSelectedId(2, dontSendNotification);
    rangeChoice.setTitle(TRANS("Keys"));
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

    tuningChoice.setTitle(TRANS("Tuning"));
    tuningChoice.setTooltip(TRANS("Tuning"));
    tuningChoice.onChange = [this]() { selectTuning(tuningChoice.getSelectedId() - 1); };
    addChildComponent(tuningChoice);

    tuningEditor.setTooltip(TRANS("Open strings from lowest to highest, e.g. D2 A2 D3 G3 B3 E4 (middle C = C4). Press Enter to use it."));
    tuningEditor.setTitle(TRANS("Custom tuning"));
    tuningEditor.onTextChange = [this]() { showTuningValidity(); };  // red while it isn't a tuning
    tuningEditor.onReturnKey = [this]() { applyCustomTuning(); };
    tuningEditor.onFocusLost = [this]() { applyCustomTuning(); };
    addChildComponent(tuningEditor);

    id = 1;
    for (auto & t : getTranspositions()) transposeChoice.addItem(TRANS(String(t.name)), id++);
    transposeChoice.setSelectedId(1, dontSendNotification);
    transposeChoice.setTitle(TRANS("Note names"));
    transposeChoice.setTooltip(TRANS("Name notes at concert pitch, or at written pitch for a transposing instrument"));
    transposeChoice.onChange = [this]() { updateNaming(); };
    addAndMakeVisible(transposeChoice);

    namesToggle.onClick = [this]() { updateNaming(); };
    flatsToggle.onClick = [this]() { updateNaming(); };
    addAndMakeVisible(namesToggle);
    addAndMakeVisible(flatsToggle);

    middleCChoice.addItem(TRANS("Middle C = C4"), 4);
    middleCChoice.addItem(TRANS("Middle C = C3"), 3);
    middleCChoice.setSelectedId(4, dontSendNotification);
    middleCChoice.setTitle(TRANS("Octave numbering"));
    middleCChoice.setTooltip(TRANS("Octave numbering"));
    middleCChoice.onChange = [this]() { updateNaming(); };
    addAndMakeVisible(middleCChoice);

    midiStatus.setFont(Font(13.0f));
    midiStatus.setColour(Label::textColourId, Colour(0xccffffff));
    addAndMakeVisible(midiStatus);

    lastNoteLabel.setFont(Font(15.0f, Font::bold));
    lastNoteLabel.setColour(Label::textColourId, Colours::white);
    lastNoteLabel.setJustificationType(Justification::centredRight);
    addAndMakeVisible(lastNoteLabel);

    if (auto * dm = getDeviceManager ? getDeviceManager() : nullptr) dm->addChangeListener(this);
    updateMidiStatus();
    instrumentChoice.setSelectedId(1, dontSendNotification);
    selectInstrument(0);

    // development aid: TINCAN_DEMO_NOTES="C4 E4 G4" shows those notes as held on the MIDI input
    for (int note : parseTuning(SystemStats::getEnvironmentVariable("TINCAN_DEMO_NOTES", {}).toStdString())) {
        NoteEvent ev;
        ev.type = NoteEvent::NoteOn;
        ev.note = (uint8_t) note;
        ev.value = 100;
        notes.apply(ev);
        lastNote = note;
    }
    notesChanged();

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

bool InstrumentPanel::isFretted() const
{
    return getInstruments()[(size_t) instrumentIndex].kind == InstrumentDef::Fretted;
}

void InstrumentPanel::selectInstrument(int index)
{
    auto & list = getInstruments();
    instrumentIndex = jlimit(0, (int) list.size() - 1, index);
    auto & inst = list[(size_t) instrumentIndex];
    bool fretted = inst.kind == InstrumentDef::Fretted;

    // only the view changes: the note state and the MIDI queue carry on
    keyboard.setVisible(!fretted);
    rangeChoice.setVisible(!fretted);
    octaveDown.setVisible(!fretted);
    octaveUp.setVisible(!fretted);
    fretboard.setVisible(fretted);
    tuningChoice.setVisible(fretted);
    tuningEditor.setVisible(fretted);

    if (fretted) {
        tuningChoice.clear(dontSendNotification);
        int id = 1;
        for (auto & t : inst.tunings) tuningChoice.addItem(TRANS(String(t.name)), id++);
        tuningChoice.addItem(TRANS("Custom"), customTuningId);
        tuningChoice.setSelectedId(1, dontSendNotification);
        selectTuning(0);
    }
    resized();
}

void InstrumentPanel::selectTuning(int index)
{
    auto & inst = getInstruments()[(size_t) instrumentIndex];
    if (index < 0 || index >= (int) inst.tunings.size()) {
        // "Custom": keep the current strings and let the text field change them
        tuningEditor.grabKeyboardFocus();
        return;
    }
    auto & strings = inst.tunings[(size_t) index].strings;
    fretboard.setInstrument(strings, inst.frets);
    tuningEditor.setText(String(formatTuning(strings, naming.useFlats)), dontSendNotification);
    showTuningValidity();
}

bool InstrumentPanel::showTuningValidity()
{
    bool valid = !parseTuning(tuningEditor.getText().toStdString()).empty();
    auto colour = valid ? Colours::white : Colour(0xffff6060);
    tuningEditor.setColour(TextEditor::textColourId, colour);
    tuningEditor.applyColourToAllText(colour, true);
    return valid;
}

void InstrumentPanel::applyCustomTuning()
{
    if (!isFretted() || !showTuningValidity()) return;
    auto strings = parseTuning(tuningEditor.getText().toStdString());

    // a preset typed by hand selects that preset; anything else is "Custom"
    auto & inst = getInstruments()[(size_t) instrumentIndex];
    int match = customTuningId;
    for (size_t i = 0; i < inst.tunings.size(); ++i) {
        if (inst.tunings[i].strings == strings) match = (int) i + 1;
    }
    tuningChoice.setSelectedId(match, dontSendNotification);
    fretboard.setInstrument(strings, inst.frets);
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
    instrumentChoice.setBounds(row.removeFromLeft(150));
    row.removeFromLeft(8);
    if (isFretted()) {
        tuningChoice.setBounds(row.removeFromLeft(190));
        row.removeFromLeft(4);
        tuningEditor.setBounds(row.removeFromLeft(jmin(220, jmax(120, row.getWidth() - 260))));
    }
    else {
        rangeChoice.setBounds(row.removeFromLeft(100));
        row.removeFromLeft(4);
        octaveDown.setBounds(row.removeFromLeft(28));
        octaveUp.setBounds(row.removeFromLeft(28));
    }
    lastNoteLabel.setBounds(row.removeFromRight(240));

    r.removeFromTop(6);
    row = r.removeFromTop(26);
    transposeChoice.setBounds(row.removeFromLeft(280));
    row.removeFromLeft(8);
    middleCChoice.setBounds(row.removeFromLeft(130));
    row.removeFromLeft(8);
    namesToggle.setBounds(row.removeFromLeft(124));
    flatsToggle.setBounds(row.removeFromLeft(64));

    r.removeFromTop(6);
    auto status = r.removeFromBottom(22);
    legendArea = status.removeFromRight(280);
    midiStatus.setBounds(status);
    r.removeFromBottom(4);
    keyboard.setBounds(r);
    fretboard.setBounds(r);
}

void InstrumentPanel::updateRange()
{
    lowNote = jlimit(0, 128 - numKeys, lowNote);
    bool full = numKeys >= 88;
    octaveDown.setEnabled(!full && lowNote - 12 >= 0);
    octaveUp.setEnabled(!full && lowNote + 12 + numKeys <= 128);
    keyboard.setRangeToFit(lowNote, lowNote + numKeys - 1);
}

void InstrumentPanel::updateNaming()
{
    auto & transpositions = getTranspositions();
    int t = jlimit(0, (int) transpositions.size() - 1, transposeChoice.getSelectedId() - 1);
    naming.allNames = namesToggle.getToggleState();
    naming.useFlats = flatsToggle.getToggleState();
    naming.middleCOctave = middleCChoice.getSelectedId();
    naming.transpose = transpositions[(size_t) t].semitones;
    keyboard.setNaming(naming);
    fretboard.setNaming(naming);
    if (isFretted()) {
        // re-spell the tuning text with sharps or flats (it stays at concert pitch, middle C = C4)
        auto strings = parseTuning(tuningEditor.getText().toStdString());
        if (!strings.empty()) tuningEditor.setText(String(formatTuning(strings, naming.useFlats)), dontSendNotification);
    }
    notesChanged();
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
                       ? TRANS("No MIDI input on. Turn one on in Settings > Audio, or click the instrument.")
                       : TRANS("MIDI input:") + " " + names.joinIntoString(", "),
                       dontSendNotification);
}

void InstrumentPanel::notesChanged()
{
    if (isFretted()) fretboard.repaint();
    else keyboard.repaint();

    // the last note played, with written and concert pitch told apart when transposing
    if (lastNote < 0) {
        lastNoteLabel.setText({}, dontSendNotification);
    }
    else if (naming.transpose == 0) {
        lastNoteLabel.setText(naming.label(lastNote), dontSendNotification);
    }
    else {
        NoteNaming concert = naming;
        concert.transpose = 0;
        lastNoteLabel.setText(TRANS("Written") + " " + naming.label(lastNote) + "   " + TRANS("Concert") + " " + concert.label(lastNote),
                              dontSendNotification);
    }
}

void InstrumentPanel::changeListenerCallback(ChangeBroadcaster *)
{
    // device settings changed: notes held on a device that went away would stay lit
    NoteEvent ev;
    ev.type = NoteEvent::Reset;
    ev.source = NoteSource::Midi;
    notes.apply(ev);
    notesChanged();
    updateMidiStatus();
}

void InstrumentPanel::timerCallback()
{
    NoteEvent ev;
    bool changed = false;
    while (processor.popNoteEvent(ev)) {
        notes.apply(ev);
        if (ev.type == NoteEvent::NoteOn) lastNote = ev.note;
        changed = true;
    }
    if (changed) notesChanged();
}

void InstrumentPanel::playLocal(int note, bool on, int channel, float velocity)
{
    NoteEvent ev;
    ev.type = on ? NoteEvent::NoteOn : NoteEvent::NoteOff;
    ev.channel = (uint8_t) jlimit(0, 15, channel - 1);
    ev.note = (uint8_t) jlimit(0, 127, note);
    ev.value = (uint8_t) jlimit(1, 127, roundToInt(velocity * 127.0f));
    ev.source = NoteSource::Local;
    notes.apply(ev);
    if (on) lastNote = ev.note;
    notesChanged();
}

void InstrumentPanel::handleNoteOn(MidiKeyboardState *, int channel, int note, float velocity)
{
    playLocal(note, true, channel, velocity);
}

void InstrumentPanel::handleNoteOff(MidiKeyboardState *, int channel, int note, float)
{
    playLocal(note, false, channel, 0.0f);
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
    tabs->addTab(TRANS("Tuner"), Colour(0xff1c1c1c), new TunerPanel(processor), true);
    tabs->addTab(TRANS("Metronome"), Colour(0xff1c1c1c), new MetronomePanel(processor), true);

    setUsingNativeTitleBar(true);
    setContentOwned(tabs, false);
    setResizable(true, false);
    setResizeLimits(640, 280, 4000, 1200);
    centreWithSize(960, 340);
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
