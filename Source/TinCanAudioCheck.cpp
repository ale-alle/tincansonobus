// SPDX-License-Identifier: GPLv3-or-later WITH Appstore-exception
// Copyright (C) 2026 Tin Can Studios

#include "TinCanAudioCheck.h"

#include <algorithm>
#include <cctype>

namespace TinCan {

DelayRating rateDeviceDelay(double ms)
{
    if (ms <= 25.0) return DelayRating::good;
    if (ms <= 45.0) return DelayRating::ok;
    return DelayRating::high;
}

double deviceDelayMs(int inputLatencySamples, int outputLatencySamples, int bufferSamples, double sampleRate)
{
    if (sampleRate <= 0.0) return 0.0;
    int samples = std::max(0, inputLatencySamples) + std::max(0, outputLatencySamples);
    if (samples == 0) samples = 2 * std::max(0, bufferSamples);
    return 1000.0 * samples / sampleRate;
}

LevelRating rateInputLevel(float peakDb)
{
    if (peakDb >= -1.0f) return LevelRating::tooLoud;
    if (peakDb >= -30.0f) return LevelRating::good;
    if (peakDb >= -60.0f) return LevelRating::quiet;
    return LevelRating::silent;
}

bool looksLikeBluetooth(const std::string & deviceName)
{
    std::string name(deviceName);
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return (char) std::tolower(c); });
    for (const char * hint : {"bluetooth", "hands-free", "handsfree", "airpods", "buds", "bt "}) {
        if (name.find(hint) != std::string::npos) return true;
    }
    return false;
}

}
