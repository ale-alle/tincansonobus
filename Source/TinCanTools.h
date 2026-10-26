// SPDX-License-Identifier: GPLv3-or-later WITH Appstore-exception
// Copyright (C) 2026 Tin Can Studios

#pragma once

#include "JuceHeader.h"
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

// piano keyboard drawn from a NoteState; mouse clicks play into the given MidiKeyboardState
class KeyboardView : public MidiKeyboardComponent
{
public:
    KeyboardView(MidiKeyboardState & mouseState, const NoteState & notes);

    void setNoteNames(bool all, bool flats, int middleCOctave);
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
    bool allNames = false;
    bool useFlats = false;
    int middleC = 4;
    int fitLow = 48, fitHigh = 84;
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

    void updateRange();
    void updateNames();
    void updateMidiStatus();

    SonobusAudioProcessor & processor;
    std::function<AudioDeviceManager*()> getDeviceManager;

    NoteState notes;
    MidiKeyboardState mouseState;
    KeyboardView keyboard;

    ComboBox rangeChoice;
    TextButton octaveDown { "<" }, octaveUp { ">" };
    ToggleButton namesToggle { TRANS("All note names") }, flatsToggle { TRANS("Flats") };
    ComboBox middleCChoice;
    Label midiStatus;
    juce::Rectangle<int> legendArea;

    int lowNote = 48;   // C3
    int numKeys = 37;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (InstrumentPanel)
};

class ToolsWindow : public DocumentWindow
{
public:
    ToolsWindow(SonobusAudioProcessor & processor, std::function<AudioDeviceManager*()> getDeviceManager,
                std::function<void()> onClose);
    ~ToolsWindow() override;

    void closeButtonPressed() override;

private:
    std::function<void()> onClose;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ToolsWindow)
};

}
