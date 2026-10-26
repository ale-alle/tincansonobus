// SPDX-License-Identifier: GPLv3-or-later WITH Appstore-exception
// Copyright (C) 2026 Tin Can Studios

#pragma once

#include "JuceHeader.h"
#include "TinCanPitch.h"

class SonobusAudioProcessor;

// Tuner and metronome tabs of the teaching tools window (T-036).
// The metronome is SonoBus's own (same on/off, tempo, volume and "send" settings as the toolbar
// metronome), with beats per bar, subdivisions, count-in and a beat indicator added.
// The tuner listens to one local input on its own thread while its tab is showing; it makes no
// sound and nothing it hears is sent anywhere.
namespace TinCan {

class BeatIndicator : public Component
{
public:
    void update(double beatPosition, int beatsPerBar, int subdivision, double countInRemaining, bool running);
    void paint(Graphics & g) override;

private:
    double position = 0.0;
    int beatsPerBar = 1;
    int subdivision = 1;
    bool countIn = false;
    bool running = false;
};

class MetronomePanel : public Component, private Timer
{
public:
    explicit MetronomePanel(SonobusAudioProcessor & processor);
    ~MetronomePanel() override;

    void resized() override;
    void paint(Graphics & g) override;
    void visibilityChanged() override;

private:
    void timerCallback() override;
    void setIntParam(const String & id, int value);
    int getIntParam(const String & id) const;
    void tapTempo();

    SonobusAudioProcessor & processor;

    ToggleButton enableButton { TRANS("Metronome on") };
    Slider tempoSlider { Slider::LinearHorizontal, Slider::TextBoxRight };
    TextButton tapButton { TRANS("Tap") };
    ComboBox beatsChoice, subdivisionChoice, countInChoice;
    Label beatsLabel, subdivisionLabel, countInLabel, tempoLabel, volumeLabel;
    Slider volumeSlider { Slider::LinearHorizontal, Slider::TextBoxRight };
    ToggleButton sendButton { TRANS("Let the other person hear it") };
    Label infoLabel;
    BeatIndicator indicator;

    std::unique_ptr<AudioProcessorValueTreeState::ButtonAttachment> enableAttachment, sendAttachment;
    std::unique_ptr<AudioProcessorValueTreeState::SliderAttachment> tempoAttachment, volumeAttachment;

    Array<double> taps;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MetronomePanel)
};

class TunerDisplay : public Component
{
public:
    void setResult(const PitchResult & result, double a4, bool useFlats);
    void paint(Graphics & g) override;

private:
    PitchResult result;
    double note = 0.0;      // fractional MIDI note
    double shownCents = 0.0; // smoothed needle
    bool useFlats = false;
};

class TunerPanel : public Component, private Timer, private Thread
{
public:
    explicit TunerPanel(SonobusAudioProcessor & processor);
    ~TunerPanel() override;

    void resized() override;
    void paint(Graphics & g) override;
    void visibilityChanged() override;

private:
    void timerCallback() override;
    void run() override;  // analysis thread
    void startListening();
    void stopListening();
    void updateInputs();

    SonobusAudioProcessor & processor;

    ComboBox inputChoice;
    Label inputLabel, refLabel;
    Slider refSlider { Slider::IncDecButtons, Slider::TextBoxLeft };
    ToggleButton flatsToggle { TRANS("Flats") };
    TunerDisplay display;
    Label noteLabel;

    // result handed from the analysis thread to the UI
    SpinLock resultLock;
    PitchResult latest;
    bool hasNew = false;
    int numInputs = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TunerPanel)
};

}
