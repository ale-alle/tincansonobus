// SPDX-License-Identifier: GPLv3-or-later WITH Appstore-exception
// Copyright (C) 2026 Tin Can Studios

#include "TinCanAudioSetup.h"
#include "TinCanAudioCheck.h"
#include "TinCanPaths.h"
#include "TinCanTheme.h"
#include "SonobusPluginProcessor.h"

#include <atomic>

namespace TinCan {

namespace {

// Listens to the microphone without touching the lesson audio: a second callback on the device
// manager, which mixes callbacks' outputs (ours is silence).
class InputPeakTap : public AudioIODeviceCallback
{
public:
    void audioDeviceIOCallbackWithContext(const float * const * in, int numIn, float * const * out, int numOut,
                                          int numSamples, const AudioIODeviceCallbackContext &) override
    {
        float peak = 0.0f;
        for (int ch = 0; ch < numIn; ++ch) {
            if (in[ch] == nullptr) continue;
            for (int i = 0; i < numSamples; ++i) peak = jmax(peak, std::abs(in[ch][i]));
        }
        float prev = maxPeak.load();
        while (peak > prev && !maxPeak.compare_exchange_weak(prev, peak)) {}
        for (int ch = 0; ch < numOut; ++ch) {
            if (out[ch] != nullptr) FloatVectorOperations::clear(out[ch], numSamples);
        }
    }
    void audioDeviceAboutToStart(AudioIODevice *) override {}
    void audioDeviceStopped() override {}

    // loudest sample since the last call, as linear gain
    float takePeak() { return maxPeak.exchange(0.0f); }

private:
    std::atomic<float> maxPeak { 0.0f };
};

class LevelMeter : public Component
{
public:
    void setLevelDb(float db) { levelDb = db; repaint(); }

    void paint(Graphics & g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour(TinCanTheme::field);
        g.fillRoundedRectangle(r, 6.0f);
        const float frac = jlimit(0.0f, 1.0f, (levelDb + 60.0f) / 60.0f);
        auto bar = r.reduced(3.0f).withWidth((r.getWidth() - 6.0f) * frac);
        const auto rating = rateInputLevel(levelDb);
        g.setColour(rating == LevelRating::tooLoud ? Colour(0xffe5484d)
                    : rating == LevelRating::good  ? Colour(0xff3fbf6f)
                                                   : Colour(0xffd9a43a));
        g.fillRoundedRectangle(bar, 4.0f);
        // the "good" zone, -30 to -1 dBFS
        g.setColour(TinCanTheme::lineStrong);
        for (float mark : { -30.0f, -1.0f }) {
            const float x = r.getX() + 3.0f + (r.getWidth() - 6.0f) * (mark + 60.0f) / 60.0f;
            g.drawVerticalLine(roundToInt(x), r.getY(), r.getBottom());
        }
        g.setColour(TinCanTheme::line);
        g.drawRoundedRectangle(r.reduced(0.5f), 6.0f, 1.0f);
    }

private:
    float levelDb = -100.0f;
};

String delayRatingText(DelayRating rating)
{
    switch (rating) {
    case DelayRating::good: return TRANS("Good");
    case DelayRating::ok:   return TRANS("OK");
    case DelayRating::high: return TRANS("High");
    }
    return {};
}

File doneMarker() { return getDataFolder().getChildFile("audio-setup-done"); }

class AudioSetupContent : public Component, private Timer, private ChangeListener
{
public:
    AudioSetupContent(SonobusAudioProcessor & proc, AudioDeviceManager & adm, std::function<void()> openFullSettingsFunc,
                      std::function<void()> closeFunc)
        : processor(proc), deviceManager(adm), openFullSettings(std::move(openFullSettingsFunc)), close(std::move(closeFunc))
    {
        title.setFont(Font(22.0f, Font::bold));
        title.setColour(Label::textColourId, TinCanTheme::text);
        addAndMakeVisible(title);
        stepLabel.setColour(Label::textColourId, TinCanTheme::subtle);
        stepLabel.setJustificationType(Justification::centredRight);
        addAndMakeVisible(stepLabel);

        for (auto * l : { &intro, &status, &hint, &summary }) {
            l->setColour(Label::textColourId, TinCanTheme::muted);
            l->setFont(Font(15.0f));
            l->setJustificationType(Justification::topLeft);
            l->setMinimumHorizontalScale(1.0f);
            addChildComponent(l);
        }
        addAndMakeVisible(intro);
        status.setFont(Font(16.0f, Font::bold));
        summary.setColour(Label::textColourId, TinCanTheme::text);

        // step 1: JUCE's selector. The input channel list stays (on an interface, people tick the input
        // their microphone is plugged into); the output list is hidden, since JUCE shows it only while the
        // minimum is below the device's channel count, and lessons use outputs 1-2. Options has both.
        int maxIn = 2;
        if (auto * bus = processor.getBus(true, 0)) maxIn = jmax(1, bus->getMaxSupportedChannels(128));
        selector = std::make_unique<AudioDeviceSelectorComponent>(deviceManager, 0, maxIn, 256, 256,
                                                                  false, false, false, true);
        selectorViewport.setViewedComponent(selector.get(), false);
        selectorViewport.setScrollBarsShown(true, false);
        addChildComponent(selectorViewport);
        allSettingsButton.setButtonText(TRANS("All audio settings..."));
        allSettingsButton.onClick = [this] { if (openFullSettings) openFullSettings(); };
        addChildComponent(allSettingsButton);

        // step 2
        addChildComponent(meter);

        // step 3
        testSoundButton.setButtonText(TRANS("Play test sound"));
        testSoundButton.onClick = [this] { deviceManager.playTestSound(); };
        addChildComponent(testSoundButton);
        headphonesToggle.setButtonText(TRANS("I'm using headphones or earbuds"));
        headphonesToggle.setColour(ToggleButton::textColourId, TinCanTheme::text);
        addChildComponent(headphonesToggle);

        backButton.setButtonText(TRANS("Back"));
        backButton.onClick = [this] { showStep(step - 1); };
        nextButton.setButtonText(TRANS("Next"));
        nextButton.onClick = [this] {
            if (step < 3) showStep(step + 1);
            else if (close) close();
        };
        skipButton.setButtonText(TRANS("Skip setup"));
        skipButton.onClick = [this] { if (close) close(); };
        for (auto * b : { &backButton, &nextButton, &skipButton }) addAndMakeVisible(b);

        deviceManager.addAudioCallback(&tap);
        deviceManager.addChangeListener(this);
        showStep(0);
        startTimerHz(20);
        setSize(600, 560);
    }

    ~AudioSetupContent() override
    {
        stopTimer();
        deviceManager.removeChangeListener(this);
        deviceManager.removeAudioCallback(&tap);
    }

    void paint(Graphics & g) override { g.fillAll(TinCanTheme::surface); }

    void resized() override
    {
        auto r = getLocalBounds().reduced(24, 18);
        auto top = r.removeFromTop(34);
        stepLabel.setBounds(top.removeFromRight(110));
        title.setBounds(top);
        r.removeFromTop(6);

        auto bottom = r.removeFromBottom(36);
        skipButton.setBounds(bottom.removeFromLeft(110));
        nextButton.setBounds(bottom.removeFromRight(110));
        bottom.removeFromRight(8);
        backButton.setBounds(bottom.removeFromRight(90));
        r.removeFromBottom(14);

        intro.setBounds(r.removeFromTop(step == 3 ? 0 : 64));
        auto body = r;
        if (step == 0) {
            allSettingsButton.setBounds(body.removeFromBottom(30).removeFromLeft(180));
            body.removeFromBottom(8);
            selectorViewport.setBounds(body);
            selector->setBounds(0, 0, body.getWidth() - selectorViewport.getScrollBarThickness(), selector->getHeight());
        } else if (step == 1) {
            meter.setBounds(body.removeFromTop(34));
            body.removeFromTop(12);
            status.setBounds(body.removeFromTop(28));
            hint.setBounds(body.removeFromTop(90));
        } else if (step == 2) {
            testSoundButton.setBounds(body.removeFromTop(36).removeFromLeft(180));
            body.removeFromTop(10);
            hint.setBounds(body.removeFromTop(70));
            body.removeFromTop(10);
            headphonesToggle.setBounds(body.removeFromTop(30));
            body.removeFromTop(6);
            status.setBounds(body.removeFromTop(90));
        } else {
            summary.setBounds(body);
        }
    }

private:
    void showStep(int newStep)
    {
        step = jlimit(0, 3, newStep);
        const String titles[] = { TRANS("Choose your sound devices"), TRANS("Check your microphone"),
                                  TRANS("Check your headphones"), TRANS("You're set") };
        title.setText(titles[step], dontSendNotification);
        stepLabel.setText(TRANS("Step %1 of 4").replace("%1", String(step + 1)), dontSendNotification);

        for (Component * c : std::initializer_list<Component *> { &selectorViewport, &allSettingsButton, &meter, &status, &hint,
                                                                 &testSoundButton, &headphonesToggle, &summary }) {
            c->setVisible(false);
        }
        status.setFont(Font(16.0f, Font::bold));
        status.setColour(Label::textColourId, TinCanTheme::text);

        if (step == 0) {
            intro.setText(TRANS("Pick the microphone (or audio interface) you'll play into, and what you'll listen on. "
                                "If your audio interface has an ASIO driver, choose ASIO at the top: it has the least delay. "
                                "Otherwise Windows Audio works with any device."), dontSendNotification);
            selectorViewport.setVisible(true);
            allSettingsButton.setVisible(true);
        } else if (step == 1) {
            intro.setText(TRANS("Say something or play a few notes at the volume you'll use in your lesson."),
                          dontSendNotification);
            meter.setVisible(true);
            status.setVisible(true);
            hint.setVisible(true);
        } else if (step == 2) {
            intro.setText(TRANS("Put on your headphones and play the test sound. You should hear a short beep."),
                          dontSendNotification);
            hint.setText(TRANS("No beep? Go back to step 1 and check the output device, and check your volume."),
                         dontSendNotification);
            status.setText(TRANS("Headphones matter: with speakers, your microphone picks up your teacher's sound "
                                 "and sends it back, so they hear an echo of themselves."), dontSendNotification);
            status.setFont(Font(15.0f));
            status.setColour(Label::textColourId, TinCanTheme::muted);
            testSoundButton.setVisible(true);
            hint.setVisible(true);
            headphonesToggle.setVisible(true);
            status.setVisible(true);
            if (!headphonesGuessed) {
                headphonesGuessed = true;
                const auto out = deviceManager.getAudioDeviceSetup().outputDeviceName.toLowerCase();
                headphonesToggle.setToggleState(out.contains("headphone") || out.contains("headset")
                                                || out.contains("earbud"), dontSendNotification);
            }
        } else {
            intro.setText({}, dontSendNotification);
            summary.setText(makeSummary(), dontSendNotification);
            summary.setVisible(true);
        }
        intro.setVisible(step != 3);
        backButton.setVisible(step > 0);
        skipButton.setVisible(step < 3);
        nextButton.setButtonText(step < 3 ? TRANS("Next") : TRANS("Done"));
        resized();
    }

    String makeSummary() const
    {
        const auto setup = deviceManager.getAudioDeviceSetup();
        auto * device = deviceManager.getCurrentAudioDevice();
        String text;
        if (device == nullptr) {
            return TRANS("No sound device is working yet. Go back to step 1 and choose one, or ask for help on the "
                         "Tin Can Studios website.");
        }
        const String type = deviceManager.getCurrentAudioDeviceType();
        const bool asio = type.containsIgnoreCase("ASIO");
        text << TRANS("Sound system") << ": " << type << "\n";
        if (asio) {
            text << TRANS("Device") << ": " << device->getName() << "\n";
        } else {
            text << TRANS("Microphone") << ": " << (setup.inputDeviceName.isNotEmpty() ? setup.inputDeviceName : TRANS("none")) << "\n";
            text << TRANS("Listening on") << ": " << setup.outputDeviceName << "\n";
        }
        const double ms = deviceDelayMs(device->getInputLatencyInSamples(), device->getOutputLatencyInSamples(),
                                        device->getCurrentBufferSizeSamples(), device->getCurrentSampleRate());
        const auto rating = rateDeviceDelay(ms);
        text << TRANS("Sound card delay") << ": " << String(roundToInt(ms)) << " ms (" << delayRatingText(rating) << ")\n\n";

        StringArray tips;
        const bool bluetooth = looksLikeBluetooth(setup.inputDeviceName.toStdString())
                               || looksLikeBluetooth(setup.outputDeviceName.toStdString());
        if (bluetooth) {
            tips.add(TRANS("Bluetooth headphones add a lot of delay, and their microphone sounds like a phone call. "
                           "Use wired headphones for lessons if you can."));
        }
        if (rating == DelayRating::high && !bluetooth) {
            tips.add(asio ? TRANS("To lower the delay, go back to step 1, open the advanced settings and try a buffer of 128 or 256.")
                          : TRANS("If you have an audio interface, install its ASIO driver and choose ASIO in step 1."));
        }
        if (!micHeard) {
            tips.add(TRANS("We didn't hear your microphone. You can still join; Setup Audio brings this check back."));
        }
        if (!headphonesToggle.getToggleState()) {
            tips.add(TRANS("Use headphones or earbuds in lessons if you can, so your teacher doesn't hear an echo."));
        }
        if (tips.isEmpty()) {
            text << TRANS("Everything looks good. You're ready for your lesson!");
        } else {
            for (auto & tip : tips) text << "- " << tip << "\n";
        }
        text << "\n" << TRANS("You can change this any time with Setup Audio.");
        return text;
    }

    void timerCallback() override
    {
        const float peak = tap.takePeak();
        const float db = Decibels::gainToDecibels(peak, -100.0f);
        // the shown level falls 30 dB a second; the rating uses the loudest moment of the last second or so
        shownDb = jmax(db, shownDb - 1.5f);
        recentDb = jmax(db, recentDb - 0.75f);
        if (step != 1) return;

        meter.setLevelDb(shownDb);
        auto * device = deviceManager.getCurrentAudioDevice();
        const bool hasInput = device != nullptr && device->getActiveInputChannels().countNumberOfSetBits() > 0;
        String s, h;
        if (!hasInput) {
            s = TRANS("No microphone selected");
            h = TRANS("Go back to step 1 and choose the microphone or audio interface input you'll use.");
        } else {
            switch (rateInputLevel(recentDb)) {
            case LevelRating::silent:
                s = micHeard ? TRANS("Sounds good") : TRANS("Waiting for sound...");
                h = micHeard ? String() : TRANS("Nothing yet? Check the microphone is plugged in and not muted, and that "
                                                "Windows lets apps use it (Settings > Privacy > Microphone).");
                break;
            case LevelRating::quiet:
                s = TRANS("A bit quiet");
                h = TRANS("Move closer, or turn up the input gain on your audio interface.");
                break;
            case LevelRating::good:
                micHeard = true;
                s = TRANS("Sounds good");
                break;
            case LevelRating::tooLoud:
                micHeard = true;
                s = TRANS("Too loud");
                h = TRANS("Your sound is distorting. Turn down the input gain on your audio interface, or move back a little.");
                break;
            }
        }
        status.setText(s, dontSendNotification);
        hint.setText(h, dontSendNotification);
    }

    void changeListenerCallback(ChangeBroadcaster *) override
    {
        // a new device: listen afresh, and refresh what the selector needs
        micHeard = false;
        if (step == 0) resized();
        if (step == 3) summary.setText(makeSummary(), dontSendNotification);
    }

    SonobusAudioProcessor & processor;
    AudioDeviceManager & deviceManager;
    std::function<void()> openFullSettings, close;
    InputPeakTap tap;

    int step = 0;
    bool micHeard = false;
    bool headphonesGuessed = false;
    float shownDb = -100.0f, recentDb = -100.0f;

    Label title, stepLabel, intro, status, hint, summary;
    std::unique_ptr<AudioDeviceSelectorComponent> selector;
    Viewport selectorViewport;
    TextButton allSettingsButton, testSoundButton, backButton, nextButton, skipButton;
    ToggleButton headphonesToggle;
    LevelMeter meter;
};

} // namespace

AudioSetupWindow::AudioSetupWindow(SonobusAudioProcessor & processor, AudioDeviceManager & deviceManager,
                                   std::function<void()> openFullSettings, std::function<void()> onCloseFunc)
    : DocumentWindow(TRANS("Set up your sound"), TinCanTheme::surface, DocumentWindow::closeButton),
      onClose(std::move(onCloseFunc))
{
    setUsingNativeTitleBar(true);
    setContentOwned(new AudioSetupContent(processor, deviceManager, std::move(openFullSettings), [this] { closeButtonPressed(); }),
                    true);
    setResizable(true, false);
    setResizeLimits(480, 460, 1200, 1000);
    centreWithSize(getWidth(), getHeight());
    setVisible(true);
    toFront(true);
}

AudioSetupWindow::~AudioSetupWindow()
{
    clearContentComponent();
}

void AudioSetupWindow::closeButtonPressed()
{
    // finishing, skipping or closing all count: it opens by itself only once
    markDone();
    if (onClose) onClose();
}

bool AudioSetupWindow::shouldShowOnStartup()
{
    return !doneMarker().existsAsFile();
}

void AudioSetupWindow::markDone()
{
    auto marker = doneMarker();
    if (!marker.existsAsFile()) {
        marker.getParentDirectory().createDirectory();
        marker.replaceWithText(String(ProjectInfo::versionString) + "\n");
    }
}

}
