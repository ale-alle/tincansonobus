// SPDX-License-Identifier: GPLv3-or-later WITH Appstore-exception
// Copyright (C) 2026 Tin Can Studios

#pragma once

#include "JuceHeader.h"
#include "TinCanInstruments.h"
#include "TinCanNotes.h"

#include <functional>

class SonobusAudioProcessor;

// Tin Can teaching tools window (T-034 onwards): on-screen instrument showing the notes played
// on the MIDI input or with the mouse. A window of its own so the lesson controls stay visible.
// Nothing here runs on the audio thread: the processor copies MIDI into a lock-free queue and
// the instrument panel drains it on the message thread while the window is open.
namespace TinCan {

// colours shared by every instrument view
struct NoteColours
{
    static Colour midi()  { return Colour(0xff47b0f8); }  // from the MIDI input
    static Colour local() { return Colour(0xfff0a030); }  // played with the mouse

    // fill for a sounding note, or transparent if silent; sustained-only notes are paler
    static Colour forNote(const NoteState & state, int note);
};

// how notes are named in every view; transpose > 0 names notes at written pitch
struct NoteNaming
{
    bool allNames = false;
    bool useFlats = false;
    int middleCOctave = 4;
    int transpose = 0;

    String name(int concertNote) const { return String(noteName(concertNote + transpose, useFlats)); }
    String label(int concertNote) const { return String(noteLabel(concertNote + transpose, useFlats, middleCOctave)); }
};

// piano keyboard drawn from a NoteState; mouse clicks play into the given MidiKeyboardState
class KeyboardView : public MidiKeyboardComponent
{
public:
    KeyboardView(MidiKeyboardState & mouseState, const NoteState & notes);

    void setNaming(const NoteNaming & naming);
    // fit the range into the current width without scrolling
    void setRangeToFit(int lowNote, int highNote);
    void resized() override;

protected:
    void drawWhiteNote(int note, Graphics & g, juce::Rectangle<float> area, bool isDown, bool isOver,
                       Colour lineColour, Colour textColour) override;
    void drawBlackNote(int note, Graphics & g, juce::Rectangle<float> area, bool isDown, bool isOver,
                       Colour noteFillColour) override;
    String getWhiteNoteText(int note) override;

private:
    const NoteState & notes;
    NoteNaming naming;
    int fitLow = 48, fitHigh = 84;
};

// guitar/bass neck seen from the player, highest string at the top; any tuning or string count
class FretboardView : public Component
{
public:
    explicit FretboardView(const NoteState & notes);

    void setInstrument(const std::vector<int> & strings, int frets);
    void setNaming(const NoteNaming & naming);

    std::function<void(int note, bool on)> onMousePlay;  // concert pitch

    void paint(Graphics & g) override;
    void mouseDown(const MouseEvent & e) override;
    void mouseUp(const MouseEvent & e) override;

private:
    juce::Rectangle<float> neckArea() const;
    float fretX(int fret) const;           // x of the fret wire (0 = nut)
    float noteX(int fret) const;           // where a note on this fret is drawn
    float stringY(int string) const;
    int stringAt(float y) const;
    int fretAt(float x) const;

    const NoteState & notes;
    NoteNaming naming;
    std::vector<int> strings { 40, 45, 50, 55, 59, 64 };
    int frets = 22;
    int mouseNote = -1;
};

class InstrumentPanel : public Component, private Timer, private MidiKeyboardState::Listener,
                        private ChangeListener
{
public:
    InstrumentPanel(SonobusAudioProcessor & processor, std::function<AudioDeviceManager*()> getDeviceManager);
    ~InstrumentPanel() override;

    void resized() override;
    void paint(Graphics & g) override;

private:
    void timerCallback() override;
    void handleNoteOn(MidiKeyboardState *, int channel, int note, float velocity) override;
    void handleNoteOff(MidiKeyboardState *, int channel, int note, float velocity) override;
    void changeListenerCallback(ChangeBroadcaster *) override;  // MIDI inputs turned on/off

    void playLocal(int note, bool on, int channel, float velocity);
    void notesChanged();
    void selectInstrument(int index);
    void selectTuning(int index);
    void applyCustomTuning();
    bool showTuningValidity();
    void updateRange();
    void updateNaming();
    void updateMidiStatus();
    bool isFretted() const;

    SonobusAudioProcessor & processor;
    std::function<AudioDeviceManager*()> getDeviceManager;

    NoteState notes;
    MidiKeyboardState mouseState;
    KeyboardView keyboard;
    FretboardView fretboard;
    NoteNaming naming;

    ComboBox instrumentChoice;
    // piano
    ComboBox rangeChoice;
    TextButton octaveDown { "<" }, octaveUp { ">" };
    // guitar and bass
    ComboBox tuningChoice;
    TextEditor tuningEditor;
    // all
    ComboBox transposeChoice;
    ComboBox middleCChoice;
    ToggleButton namesToggle { TRANS("All note names") }, flatsToggle { TRANS("Flats") };
    Label midiStatus;
    Label lastNoteLabel;
    juce::Rectangle<int> legendArea;

    int instrumentIndex = 0;
    int lowNote = 48;   // C3
    int numKeys = 37;
    int lastNote = -1;  // most recent note-on, concert pitch

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (InstrumentPanel)
};

class ToolsWindow : public DocumentWindow
{
public:
    ToolsWindow(SonobusAudioProcessor & processor, std::function<AudioDeviceManager*()> getDeviceManager,
                std::function<void()> openWorkspace, std::function<void()> onClose);
    ~ToolsWindow() override;

    void closeButtonPressed() override;

private:
    std::function<void()> onClose;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ToolsWindow)
};

}
