// SPDX-License-Identifier: GPLv3-or-later WITH Appstore-exception
// Copyright (C) 2026 Tin Can Studios

#include "TinCanTest.h"
#include "../Source/TinCanLessonRules.h"

using namespace TinCan::LessonRules;

void runLessonRulesTests()
{
    // "If the sound crackles" <-> jitter buffer mode, both ways
    CHECK(bufferModeFor(BufferChoice::Auto) == ModeAutoFull);
    CHECK(bufferModeFor(BufferChoice::Faster) == ModeOff);
    CHECK(bufferModeFor(BufferChoice::Smoother) == ModeIncreaseOnly);
    for (auto c : { BufferChoice::Auto, BufferChoice::Faster, BufferChoice::Smoother })
        CHECK(bufferChoiceFor(bufferModeFor(c)) == c);
    CHECK(bufferChoiceFor(ModeInitAuto) == BufferChoice::Auto);
    CHECK(bufferChoiceFor(42) == BufferChoice::Auto);

    BufferChoice b = BufferChoice::Auto;
    CHECK(parseBufferChoice("smoother", b) && b == BufferChoice::Smoother);
    CHECK(parseBufferChoice(bufferChoiceName(BufferChoice::Faster), b) && b == BufferChoice::Faster);
    CHECK(!parseBufferChoice("Smoother", b));
    CHECK(!parseBufferChoice("", b));

    // "Sound quality"
    CHECK(qualityFor(-1, true, 256000) == Quality::Auto);
    CHECK(qualityFor(7, true, BestBitrate) == Quality::Best);
    CHECK(qualityFor(3, true, SaveBitrate) == Quality::Save);
    CHECK(qualityFor(8, false, 0) == Quality::Custom);        // PCM
    CHECK(qualityFor(5, true, 128000) == Quality::Custom);
    Quality q = Quality::Auto;
    CHECK(parseQuality("best", q) && q == Quality::Best && bitrateFor(q) == BestBitrate);
    CHECK(parseQuality("save", q) && q == Quality::Save && bitrateFor(q) == SaveBitrate);
    CHECK(parseQuality("auto", q) && q == Quality::Auto && bitrateFor(q) == 0);
    CHECK(!parseQuality("custom", q));
    CHECK(std::string(qualityName(Quality::Custom)) == "custom");

    // delay on the string
    CHECK(oneWayDelayMs(18.0f, 50.0f) == 18.0f);
    CHECK(oneWayDelayMs(0.0f, 50.0f) == 25.0f);
    CHECK(oneWayDelayMs(0.0f, 0.0f) == 0.0f);
    CHECK(oneWayDelayMs(-1.0f, -1.0f) == 0.0f);
}
