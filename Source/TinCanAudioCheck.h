// SPDX-License-Identifier: GPLv3-or-later WITH Appstore-exception
// Copyright (C) 2026 Tin Can Studios

#pragma once

#include <string>

// Ratings shown by the first-run audio setup (TinCanAudioSetup.h), kept free of JUCE so TinCanTests
// can cover them.
namespace TinCan {

enum class DelayRating { good, ok, high };

// the sound card's own delay (input + output, ms); network delay to the teacher comes on top
DelayRating rateDeviceDelay(double milliseconds);

// device delay in ms from what the driver reports; when it reports nothing, one buffer each way
double deviceDelayMs(int inputLatencySamples, int outputLatencySamples, int bufferSamples, double sampleRate);

enum class LevelRating { silent, quiet, good, tooLoud };

// peakDb: the loudest moment over the last second or so, in dBFS
LevelRating rateInputLevel(float peakDb);

// Bluetooth headsets add 100-200 ms and, with a microphone in use, drop to phone-call quality
bool looksLikeBluetooth(const std::string & deviceName);

}
