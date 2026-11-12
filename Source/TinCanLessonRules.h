// SPDX-License-Identifier: GPLv3-or-later WITH Appstore-exception
// Copyright (C) 2026 Tin Can Studios

#pragma once

#include <string>

// Plain words for audio settings in the lesson window (T-070): what each choice on the back of a
// tile means for the engine. No JUCE, so the unit tests (TinCanTests) can check it.
namespace TinCan { namespace LessonRules {

// "If the sound crackles": the person's jitter buffer.
//   auto     - grows and shrinks by itself (SonoBus AutoFull)
//   faster   - stays where it is now, never grows (Off: lowest delay, may crackle)
//   smoother - only grows (AutoIncreaseOnly: fewer crackles, a little more delay)
enum class BufferChoice { Auto, Faster, Smoother };

// SonobusAudioProcessor::AutoNetBufferMode values
enum BufferMode { ModeOff = 0, ModeIncreaseOnly = 1, ModeAutoFull = 2, ModeInitAuto = 3 };

int bufferModeFor(BufferChoice choice);
BufferChoice bufferChoiceFor(int mode);
const char * bufferChoiceName(BufferChoice choice);
bool parseBufferChoice(const std::string & name, BufferChoice & out);

// "Sound quality": the format we ask that person's computer to send.
//   auto - no request (-1), the sender's own choice
//   best - Opus 256 kbps (best for music)
//   save - Opus 64 kbps (saves data on a weak connection)
//   custom - anything else, set in the old per-person options (shown as no choice pressed)
enum class Quality { Auto, Best, Save, Custom };

constexpr int BestBitrate = 256000;
constexpr int SaveBitrate = 64000;

// requestedFormat: -1 for no request; isOpus/bitrate describe that format otherwise
Quality qualityFor(int requestedFormat, bool isOpus, int bitrate);
const char * qualityName(Quality quality);
bool parseQuality(const std::string & name, Quality & out);
int bitrateFor(Quality quality);   // 0 for Auto and Custom

// one-way sound delay shown on the string, in ms: our sound to their ears if measured, else half the
// round trip; 0 when unknown
float oneWayDelayMs(float outgoingMs, float roundTripMs);

}}
