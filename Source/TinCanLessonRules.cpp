// SPDX-License-Identifier: GPLv3-or-later WITH Appstore-exception
// Copyright (C) 2026 Tin Can Studios

#include "TinCanLessonRules.h"

namespace TinCan { namespace LessonRules {

int bufferModeFor(BufferChoice choice)
{
    switch (choice) {
        case BufferChoice::Faster:   return ModeOff;
        case BufferChoice::Smoother: return ModeIncreaseOnly;
        case BufferChoice::Auto:     break;
    }
    return ModeAutoFull;
}

BufferChoice bufferChoiceFor(int mode)
{
    if (mode == ModeOff) return BufferChoice::Faster;
    if (mode == ModeIncreaseOnly) return BufferChoice::Smoother;
    return BufferChoice::Auto;   // AutoFull, and InitAuto (auto until it settles)
}

const char * bufferChoiceName(BufferChoice choice)
{
    switch (choice) {
        case BufferChoice::Faster:   return "faster";
        case BufferChoice::Smoother: return "smoother";
        case BufferChoice::Auto:     break;
    }
    return "auto";
}

bool parseBufferChoice(const std::string & name, BufferChoice & out)
{
    if (name == "auto")     { out = BufferChoice::Auto; return true; }
    if (name == "faster")   { out = BufferChoice::Faster; return true; }
    if (name == "smoother") { out = BufferChoice::Smoother; return true; }
    return false;
}

Quality qualityFor(int requestedFormat, bool isOpus, int bitrate)
{
    if (requestedFormat < 0) return Quality::Auto;
    if (isOpus && bitrate == BestBitrate) return Quality::Best;
    if (isOpus && bitrate == SaveBitrate) return Quality::Save;
    return Quality::Custom;
}

const char * qualityName(Quality quality)
{
    switch (quality) {
        case Quality::Best:   return "best";
        case Quality::Save:   return "save";
        case Quality::Custom: return "custom";
        case Quality::Auto:   break;
    }
    return "auto";
}

bool parseQuality(const std::string & name, Quality & out)
{
    if (name == "auto") { out = Quality::Auto; return true; }
    if (name == "best") { out = Quality::Best; return true; }
    if (name == "save") { out = Quality::Save; return true; }
    return false;   // "custom" can't be chosen from the page
}

int bitrateFor(Quality quality)
{
    if (quality == Quality::Best) return BestBitrate;
    if (quality == Quality::Save) return SaveBitrate;
    return 0;
}

float oneWayDelayMs(float outgoingMs, float roundTripMs)
{
    if (outgoingMs > 0.0f) return outgoingMs;
    if (roundTripMs > 0.0f) return roundTripMs * 0.5f;
    return 0.0f;
}

}}
