// SPDX-License-Identifier: GPLv3-or-later WITH Appstore-exception
// Copyright (C) 2026 Tin Can Studios

#include "TinCanTest.h"
#include "../Source/TinCanAudioCheck.h"

#include <cmath>

using TinCan::DelayRating;
using TinCan::LevelRating;

void runAudioCheckTests()
{
    CHECK(TinCan::rateDeviceDelay(8.0) == DelayRating::good);
    CHECK(TinCan::rateDeviceDelay(25.0) == DelayRating::good);
    CHECK(TinCan::rateDeviceDelay(30.0) == DelayRating::ok);
    CHECK(TinCan::rateDeviceDelay(80.0) == DelayRating::high);

    // reported latencies win; 480 + 480 samples at 48 kHz = 20 ms
    CHECK(std::abs(TinCan::deviceDelayMs(480, 480, 128, 48000.0) - 20.0) < 1e-9);
    // driver reports nothing: one buffer each way, 2 x 256 at 48 kHz
    CHECK(std::abs(TinCan::deviceDelayMs(0, 0, 256, 48000.0) - 10.6666667) < 1e-6);
    CHECK(TinCan::deviceDelayMs(100, 100, 128, 0.0) == 0.0);
    CHECK(TinCan::deviceDelayMs(-5, 0, 0, 48000.0) == 0.0);

    CHECK(TinCan::rateInputLevel(-100.0f) == LevelRating::silent);
    CHECK(TinCan::rateInputLevel(-45.0f) == LevelRating::quiet);
    CHECK(TinCan::rateInputLevel(-12.0f) == LevelRating::good);
    CHECK(TinCan::rateInputLevel(-0.5f) == LevelRating::tooLoud);
    CHECK(TinCan::rateInputLevel(0.0f) == LevelRating::tooLoud);

    CHECK(TinCan::looksLikeBluetooth("Headset (WH-1000XM4 Hands-Free AG Audio)"));
    CHECK(TinCan::looksLikeBluetooth("Headphones (AirPods Pro)"));
    CHECK(TinCan::looksLikeBluetooth("Speakers (Galaxy Buds2)"));
    CHECK(TinCan::looksLikeBluetooth("Bluetooth Speaker"));
    CHECK(!TinCan::looksLikeBluetooth("Speakers (Focusrite USB Audio)"));
    CHECK(!TinCan::looksLikeBluetooth("Focusrite USB ASIO"));
    CHECK(!TinCan::looksLikeBluetooth("Headphones (Realtek(R) Audio)"));
}
