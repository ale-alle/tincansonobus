// SPDX-License-Identifier: GPLv3-or-later WITH Appstore-exception
// Copyright (C) 2026 Tin Can Studios

#pragma once

#include "JuceHeader.h"

#include <functional>

class SonobusAudioProcessor;

// First-run audio setup (ROADMAP "audio setup wizard"; owner 2026-11-11): four short steps in a window
// of their own - sound devices, microphone check, headphones check, summary with the sound card's
// delay. Opens by itself the first time CanDuet starts and from the Setup Audio button; it never
// blocks joining a lesson. Step 1 is JUCE's device selector (same as Options > Audio) with the
// advanced settings folded away; Options keeps the full controls.
namespace TinCan {

class AudioSetupWindow : public DocumentWindow
{
public:
    AudioSetupWindow(SonobusAudioProcessor & processor, AudioDeviceManager & deviceManager,
                     std::function<void()> openFullSettings, std::function<void()> onClose);
    ~AudioSetupWindow() override;

    void closeButtonPressed() override;

    // true until the person has finished or skipped the setup once on this PC
    static bool shouldShowOnStartup();
    static void markDone();

private:
    std::function<void()> onClose;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioSetupWindow)
};

}
