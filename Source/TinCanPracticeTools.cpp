// SPDX-License-Identifier: GPLv3-or-later WITH Appstore-exception
// Copyright (C) 2026 Tin Can Studios

#include "TinCanPracticeTools.h"
#include "TinCanNotes.h"
#include "SonobusPluginProcessor.h"

namespace TinCan {

static const Colour panelBackground(0xff1c1c1c);
static const Colour accent(0xff47b0f8);
static const Colour accentBar(0xfff0a030);
static const Colour inTune(0xff5ccf72);

static void styleLabel(Label & l, const String & text)
{
    l.setText(text, dontSendNotification);
    l.setFont(Font(13.0f));
    l.setColour(Label::textColourId, Colour(0xccffffff));
    l.setJustificationType(Justification::centredLeft);
}

//==============================================================================

void BeatIndicator::update(double pos, int bpb, int sub, double countInRemaining, bool isRunning)
{
    position = pos;
    beatsPerBar = jmax(1, bpb);
    subdivision = jmax(1, sub);
    countIn = countInRemaining > 0.0;
    running = isRunning;
    repaint();
}

void BeatIndicator::paint(Graphics & g)
{
    auto r = getLocalBounds().toFloat();
    int beat = (int) std::floor(position);
    double frac = position - beat;
    int inBar = ((beat % beatsPerBar) + beatsPerBar) % beatsPerBar;

    float size = jmin(r.getHeight() * 0.7f, (r.getWidth() - 8.0f * beatsPerBar) / beatsPerBar);
    size = jmax(8.0f, size);
    float total = beatsPerBar * size + (beatsPerBar - 1) * 8.0f;
    float x = r.getCentreX() - total * 0.5f;
    float y = r.getY() + (r.getHeight() - size) * 0.5f - 8.0f;

    for (int b = 0; b < beatsPerBar; ++b) {
        auto circle = juce::Rectangle<float>(x + b * (size + 8.0f), y, size, size);
        bool current = running && b == inBar;
        Colour on = (b == 0 && beatsPerBar > 1) || countIn ? accentBar : accent;
        // the current beat flashes and fades over the beat
        float glow = current ? (float) jlimit(0.25, 1.0, 1.0 - frac * 0.75) : 0.0f;
        g.setColour(current ? on.withAlpha(glow) : Colour(0xff333333));
        g.fillEllipse(circle);
        g.setColour(Colour(0x55ffffff));
        g.drawEllipse(circle, 1.0f);
        g.setColour(current ? Colours::black : Colour(0x99ffffff));
        g.setFont(Font(jmin(22.0f, size * 0.45f), Font::bold));
        g.drawText(String(b + 1), circle, Justification::centred);
    }

    // subdivision ticks under the beats
    if (subdivision > 1 && running) {
        int subIndex = (int) std::floor(frac * subdivision);
        float sx = r.getCentreX() - (subdivision * 14.0f) * 0.5f;
        for (int s = 0; s < subdivision; ++s) {
            g.setColour(s == subIndex ? accent : Colour(0xff444444));
            g.fillRoundedRectangle(sx + s * 14.0f, y + size + 8.0f, 10.0f, 5.0f, 2.0f);
        }
    }

    if (countIn && running) {
        g.setColour(accentBar);
        g.setFont(Font(14.0f, Font::bold));
        g.drawText(TRANS("Count-in"), r.removeFromBottom(16.0f), Justification::centred);
    }
}

//==============================================================================

MetronomePanel::MetronomePanel(SonobusAudioProcessor & proc) : processor(proc)
{
    auto & state = processor.getValueTreeState();

    addAndMakeVisible(enableButton);
    enableAttachment = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment>(state, SonobusAudioProcessor::paramMetEnabled, enableButton);

    styleLabel(tempoLabel, TRANS("Tempo"));
    addAndMakeVisible(tempoLabel);
    tempoSlider.setTitle(TRANS("Tempo"));
    addAndMakeVisible(tempoSlider);
    tempoAttachment = std::make_unique<AudioProcessorValueTreeState::SliderAttachment>(state, SonobusAudioProcessor::paramMetTempo, tempoSlider);

    tapButton.setTooltip(TRANS("Tap a few times in time to set the tempo"));
    tapButton.onClick = [this]() { tapTempo(); };
    addAndMakeVisible(tapButton);

    styleLabel(beatsLabel, TRANS("Beats per bar"));
    for (int i = 1; i <= 12; ++i) beatsChoice.addItem(i == 1 ? TRANS("1 (no accent)") : String(i), i);
    beatsChoice.setTitle(TRANS("Beats per bar"));
    beatsChoice.onChange = [this]() { setIntParam(SonobusAudioProcessor::paramMetBeatsPerBar, beatsChoice.getSelectedId()); };

    styleLabel(subdivisionLabel, TRANS("Clicks per beat"));
    subdivisionChoice.addItem(TRANS("1"), 1);
    subdivisionChoice.addItem(TRANS("2 (eighths)"), 2);
    subdivisionChoice.addItem(TRANS("3 (triplets)"), 3);
    subdivisionChoice.addItem(TRANS("4 (sixteenths)"), 4);
    subdivisionChoice.setTitle(TRANS("Clicks per beat"));
    subdivisionChoice.onChange = [this]() { setIntParam(SonobusAudioProcessor::paramMetSubdivision, subdivisionChoice.getSelectedId()); };

    styleLabel(countInLabel, TRANS("Count-in"));
    countInChoice.addItem(TRANS("None"), 1);
    countInChoice.addItem(TRANS("1 bar"), 2);
    countInChoice.addItem(TRANS("2 bars"), 3);
    countInChoice.setTitle(TRANS("Count-in"));
    countInChoice.setTooltip(TRANS("Bars of accented clicks when the metronome starts"));
    countInChoice.onChange = [this]() { setIntParam(SonobusAudioProcessor::paramMetCountIn, countInChoice.getSelectedId() - 1); };

    for (auto * c : { &beatsLabel, &subdivisionLabel, &countInLabel }) addAndMakeVisible(c);
    for (auto * c : { &beatsChoice, &subdivisionChoice, &countInChoice }) addAndMakeVisible(c);

    styleLabel(volumeLabel, TRANS("Click volume"));
    addAndMakeVisible(volumeLabel);
    volumeSlider.setTitle(TRANS("Click volume"));
    addAndMakeVisible(volumeSlider);
    volumeAttachment = std::make_unique<AudioProcessorValueTreeState::SliderAttachment>(state, SonobusAudioProcessor::paramMetGain, volumeSlider);

    sendButton.setTooltip(TRANS("Off: only you hear the clicks. On: they are sent to the lesson too."));
    addAndMakeVisible(sendButton);
    sendAttachment = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment>(state, SonobusAudioProcessor::paramSendMetAudio, sendButton);

    styleLabel(infoLabel, TRANS("Same metronome as the toolbar. The clicks follow the audio clock, so they stay steady."));
    infoLabel.setColour(Label::textColourId, Colour(0x88ffffff));
    addAndMakeVisible(infoLabel);

    addAndMakeVisible(indicator);
    timerCallback();
}

MetronomePanel::~MetronomePanel()
{
    stopTimer();
}

void MetronomePanel::visibilityChanged()
{
    if (isShowing()) startTimerHz(30);
    else stopTimer();
}

void MetronomePanel::setIntParam(const String & id, int value)
{
    if (auto * p = processor.getValueTreeState().getParameter(id)) {
        p->setValueNotifyingHost(p->convertTo0to1((float) value));
    }
}

int MetronomePanel::getIntParam(const String & id) const
{
    if (auto * v = processor.getValueTreeState().getRawParameterValue(id)) return roundToInt(v->load());
    return 0;
}

void MetronomePanel::tapTempo()
{
    double now = Time::getMillisecondCounterHiRes();
    if (!taps.isEmpty() && now - taps.getLast() > 2000.0) taps.clear();  // a new run of taps
    taps.add(now);
    if (taps.size() > 6) taps.remove(0);
    if (taps.size() < 3) return;
    double avg = (taps.getLast() - taps.getFirst()) / (taps.size() - 1);
    if (auto * p = processor.getValueTreeState().getParameter(SonobusAudioProcessor::paramMetTempo)) {
        double bpm = jlimit(10.0, 400.0, 60000.0 / avg);
        p->setValueNotifyingHost(p->convertTo0to1((float) std::round(bpm)));
    }
}

void MetronomePanel::timerCallback()
{
    int bpb = getIntParam(SonobusAudioProcessor::paramMetBeatsPerBar);
    int sub = getIntParam(SonobusAudioProcessor::paramMetSubdivision);
    int countIn = getIntParam(SonobusAudioProcessor::paramMetCountIn);
    if (beatsChoice.getSelectedId() != bpb) beatsChoice.setSelectedId(bpb, dontSendNotification);
    if (subdivisionChoice.getSelectedId() != sub) subdivisionChoice.setSelectedId(sub, dontSendNotification);
    if (countInChoice.getSelectedId() != countIn + 1) countInChoice.setSelectedId(countIn + 1, dontSendNotification);

    bool running = enableButton.getToggleState();
    indicator.update(processor.getMetronomeBeatPosition(), bpb, sub, processor.getMetronomeCountInRemaining(), running);
}

void MetronomePanel::paint(Graphics & g)
{
    g.fillAll(panelBackground);
}

void MetronomePanel::resized()
{
    auto r = getLocalBounds().reduced(10);
    auto left = r.removeFromLeft(jmin(430, r.getWidth() / 2 + 60));

    auto row = left.removeFromTop(26);
    enableButton.setBounds(row.removeFromLeft(150));
    row.removeFromLeft(8);
    sendButton.setBounds(row);

    left.removeFromTop(8);
    row = left.removeFromTop(26);
    tempoLabel.setBounds(row.removeFromLeft(100));
    tapButton.setBounds(row.removeFromRight(50));
    row.removeFromRight(6);
    tempoSlider.setBounds(row);

    for (auto pair : { std::make_pair(&beatsLabel, &beatsChoice), std::make_pair(&subdivisionLabel, &subdivisionChoice),
                       std::make_pair(&countInLabel, &countInChoice) }) {
        left.removeFromTop(6);
        row = left.removeFromTop(26);
        pair.first->setBounds(row.removeFromLeft(100));
        pair.second->setBounds(row.removeFromLeft(160));
    }

    left.removeFromTop(6);
    row = left.removeFromTop(26);
    volumeLabel.setBounds(row.removeFromLeft(100));
    volumeSlider.setBounds(row);

    infoLabel.setBounds(r.removeFromBottom(22));
    r.removeFromLeft(10);
    indicator.setBounds(r);
}

//==============================================================================

void TunerDisplay::setResult(const PitchResult & newResult, double a4, bool flats)
{
    result = newResult;
    useFlats = flats;
    if (result.status == PitchResult::Pitched) {
        note = frequencyToNote(result.frequency, a4);
        double cents = (note - std::round(note)) * 100.0;
        // smooth the needle a little so it doesn't jitter, but follow big moves at once
        shownCents = std::abs(cents - shownCents) > 20.0 ? cents : shownCents * 0.6 + cents * 0.4;
    }
    repaint();
}

void TunerDisplay::paint(Graphics & g)
{
    auto r = getLocalBounds().toFloat().reduced(4.0f);
    bool pitched = result.status == PitchResult::Pitched;
    int nearest = (int) std::round(note);
    bool tuned = pitched && std::abs(shownCents) <= 5.0;
    Colour state = !pitched ? Colour(0xff666666) : tuned ? inTune : (shownCents > 0 ? accentBar : accent);

    // note name
    auto top = r.removeFromTop(r.getHeight() * 0.48f);
    g.setColour(pitched ? Colours::white : Colour(0xff555555));
    g.setFont(Font(jmin(64.0f, top.getHeight() * 0.9f), Font::bold));
    String name = pitched ? String(noteName(nearest, useFlats)) : String("-");
    g.drawText(name, top.withTrimmedRight(top.getWidth() * 0.5f - 6.0f), Justification::centredRight);
    g.setFont(Font(jmin(28.0f, top.getHeight() * 0.4f)));
    g.drawText(pitched ? String(noteOctave(nearest)) : String(), top.withTrimmedLeft(top.getWidth() * 0.5f + 2.0f).withTrimmedBottom(top.getHeight() * 0.18f),
               Justification::bottomLeft);

    // cents scale with a needle
    auto meter = r.removeFromTop(r.getHeight() * 0.55f).reduced(20.0f, 6.0f);
    g.setColour(Colour(0xff2a2a2a));
    g.fillRoundedRectangle(meter, 6.0f);
    auto centreX = meter.getCentreX();
    g.setColour(inTune.withAlpha(0.25f));
    g.fillRect(juce::Rectangle<float>(meter.getWidth() * 0.1f, meter.getHeight()).withCentre(meter.getCentre()));  // +-5 cents
    g.setFont(Font(11.0f));
    for (int c = -50; c <= 50; c += 10) {
        float x = centreX + meter.getWidth() * 0.5f * c / 50.0f;
        g.setColour(Colour(0x66ffffff));
        g.fillRect(x - 0.5f, meter.getY(), 1.0f, c == 0 ? meter.getHeight() : meter.getHeight() * 0.3f);
    }
    g.setColour(Colour(0x99ffffff));
    g.drawText(TRANS("flat"), meter.withTrimmedLeft(6.0f), Justification::bottomLeft);
    g.drawText(TRANS("sharp"), meter.withTrimmedRight(6.0f), Justification::bottomRight);
    if (pitched) {
        float x = centreX + meter.getWidth() * 0.5f * (float) jlimit(-50.0, 50.0, shownCents) / 50.0f;
        g.setColour(state);
        g.fillRoundedRectangle(x - 3.0f, meter.getY() - 4.0f, 6.0f, meter.getHeight() + 8.0f, 3.0f);
    }

    // status line
    String status;
    switch (result.status) {
        case PitchResult::NoSignal: status = TRANS("No sound on this input"); break;
        case PitchResult::TooQuiet: status = TRANS("Too quiet: play louder or move closer"); break;
        case PitchResult::Unclear:  status = TRANS("Can't hear a clear note"); break;
        case PitchResult::Pitched: {
            int cents = roundToInt(shownCents);
            String hz = String(result.frequency, 1) + " Hz   " + (cents > 0 ? "+" : "") + String(cents) + " " + TRANS("cents");
            status = (tuned ? TRANS("In tune") : cents > 0 ? TRANS("Sharp: tune down") : TRANS("Flat: tune up")) + "    " + hz;
            break;
        }
    }
    g.setColour(state == Colour(0xff666666) ? Colour(0xaaffffff) : state);
    g.setFont(Font(16.0f, Font::bold));
    g.drawText(status, r, Justification::centred);
}

//==============================================================================

TunerPanel::TunerPanel(SonobusAudioProcessor & proc) : Thread("CanDuet tuner"), processor(proc)
{
    styleLabel(inputLabel, TRANS("Input"));
    addAndMakeVisible(inputLabel);
    inputChoice.setTitle(TRANS("Tuner input"));
    inputChoice.setTooltip(TRANS("Which of your inputs the tuner listens to"));
    inputChoice.onChange = [this]() {
        if (isThreadRunning()) processor.setTunerChannel(inputChoice.getSelectedId() - 1);
    };
    addAndMakeVisible(inputChoice);

    styleLabel(refLabel, TRANS("A4 ="));
    refLabel.setJustificationType(Justification::centredRight);
    addAndMakeVisible(refLabel);
    refSlider.setRange(415.0, 466.0, 1.0);
    refSlider.setValue(440.0, dontSendNotification);
    refSlider.setTextValueSuffix(" Hz");
    refSlider.setTitle(TRANS("Reference pitch"));
    refSlider.setTooltip(TRANS("Reference pitch for A4 (usually 440 Hz)"));
    addAndMakeVisible(refSlider);

    addAndMakeVisible(flatsToggle);
    addAndMakeVisible(display);

    styleLabel(noteLabel, TRANS("Listens to your own input only. The tuner makes no sound and sends nothing to the lesson."));
    noteLabel.setColour(Label::textColourId, Colour(0x88ffffff));
    addAndMakeVisible(noteLabel);

    updateInputs();
}

TunerPanel::~TunerPanel()
{
    stopListening();
}

void TunerPanel::updateInputs()
{
    int n = processor.getTotalNumInputChannels();
    if (n == numInputs) return;
    numInputs = n;
    int sel = jmax(1, inputChoice.getSelectedId());
    inputChoice.clear(dontSendNotification);
    for (int i = 1; i <= n; ++i) inputChoice.addItem(TRANS("Input") + " " + String(i), i);
    inputChoice.setSelectedId(jmin(sel, jmax(1, n)), dontSendNotification);
    if (isThreadRunning() && n > 0) processor.setTunerChannel(inputChoice.getSelectedId() - 1);
}

void TunerPanel::visibilityChanged()
{
    if (isShowing()) startListening();
    else stopListening();
}

void TunerPanel::startListening()
{
    if (isThreadRunning()) return;
    updateInputs();
    processor.setTunerChannel(jmax(0, inputChoice.getSelectedId() - 1));
    startThread(Thread::Priority::low);  // the tuner never competes with audio or the UI
    startTimerHz(20);
}

void TunerPanel::stopListening()
{
    stopTimer();
    processor.setTunerChannel(-1);
    stopThread(1000);
}

void TunerPanel::run()
{
    PitchDetector detector;
    std::vector<float> window;
    double preparedRate = 0.0;

    while (!threadShouldExit()) {
        wait(50);
        double rate = processor.getSampleRate();
        if (rate <= 0.0) continue;
        if (rate != preparedRate) {
            // first run or the audio device changed: size the window for ~85 ms
            int size = jmax(1024, roundToInt(4096 * rate / 48000.0));
            detector.prepare(rate, size);
            window.assign((size_t) size, 0.0f);
            preparedRate = rate;
        }
        PitchResult r;
        if (processor.readTunerSamples(window.data(), (int) window.size())) {
            r = detector.analyse(window.data());
        }
        const SpinLock::ScopedLockType sl(resultLock);
        latest = r;
        hasNew = true;
    }
}

void TunerPanel::timerCallback()
{
    updateInputs();
    PitchResult r;
    {
        const SpinLock::ScopedLockType sl(resultLock);
        if (!hasNew) return;
        r = latest;
        hasNew = false;
    }
    display.setResult(r, refSlider.getValue(), flatsToggle.getToggleState());
}

void TunerPanel::paint(Graphics & g)
{
    g.fillAll(panelBackground);
}

void TunerPanel::resized()
{
    auto r = getLocalBounds().reduced(10);
    auto row = r.removeFromTop(26);
    inputLabel.setBounds(row.removeFromLeft(50));
    inputChoice.setBounds(row.removeFromLeft(140));
    row.removeFromLeft(20);
    refLabel.setBounds(row.removeFromLeft(50));
    refSlider.setBounds(row.removeFromLeft(130));
    row.removeFromLeft(12);
    flatsToggle.setBounds(row.removeFromLeft(80));

    noteLabel.setBounds(r.removeFromBottom(22));
    r.removeFromTop(6);
    display.setBounds(r);
}

}
