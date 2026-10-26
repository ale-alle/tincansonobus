// SPDX-License-Identifier: GPLv3-or-later WITH Appstore-exception
// Copyright (C) 2026 Tin Can Studios

#include "TinCanTest.h"
#include "../Source/TinCanBeatClock.h"

#include <cmath>
#include <cstdint>
#include <vector>

using namespace TinCan;

struct Seen
{
    int64_t sample;
    BeatClock::Click click;
};

// runs the clock for totalSamples in blocks of blockSize, collecting clicks with absolute times
static std::vector<Seen> run(BeatClock & clock, int64_t totalSamples, int blockSize)
{
    std::vector<Seen> out;
    BeatClock::Click buf[64];
    for (int64_t done = 0; done < totalSamples; done += blockSize) {
        int n = (int) std::min<int64_t>(blockSize, totalSamples - done);
        int c = clock.process(n, buf, 64);
        for (int i = 0; i < c; ++i) out.push_back({ done + buf[i].offset, buf[i] });
    }
    return out;
}

static void testBasicBeats()
{
    BeatClock clock;
    clock.setSampleRate(48000);
    clock.setTempo(120);  // a beat every 24000 samples
    clock.restart();
    auto seen = run(clock, 96000, 512);
    CHECK(seen.size() == 4);
    for (size_t i = 0; i < seen.size(); ++i) {
        CHECK(seen[i].sample == (int64_t) i * 24000);
        CHECK(seen[i].click.kind == BeatClock::Click::Beat);  // one beat per bar: no accent (old behaviour)
        CHECK(!seen[i].click.countIn);
    }
}

static void testBarsAndSubdivisions()
{
    BeatClock clock;
    clock.setSampleRate(44100);
    clock.setTempo(90);
    clock.setBeatsPerBar(3);
    clock.setSubdivision(2);
    clock.restart();
    // 6 beats = 4 s at 90 bpm
    auto seen = run(clock, 4 * 44100, 300);
    CHECK(seen.size() == 12);
    if (seen.size() != 12) return;
    // pattern: Bar Sub Beat Sub Beat Sub | Bar ...
    const BeatClock::Click::Kind expected[6] = { BeatClock::Click::Bar, BeatClock::Click::Sub, BeatClock::Click::Beat,
                                                 BeatClock::Click::Sub, BeatClock::Click::Beat, BeatClock::Click::Sub };
    for (int i = 0; i < 12; ++i) {
        CHECK(seen[(size_t) i].click.kind == expected[i % 6]);
        // half-beat spacing: 60/90/2 s = 14700 samples exactly
        CHECK(seen[(size_t) i].sample == (int64_t) i * 14700);
    }
    CHECK(seen[2].click.beatInBar == 1 && seen[4].click.beatInBar == 2 && seen[6].click.beatInBar == 0);
}

static void testCountIn()
{
    BeatClock clock;
    clock.setSampleRate(48000);
    clock.setTempo(60);  // 48000 samples a beat
    clock.setBeatsPerBar(4);
    clock.setCountInBars(1);
    clock.restart();
    CHECK(clock.getCountInRemaining() == 4.0);
    auto seen = run(clock, 8 * 48000, 1024);
    CHECK(seen.size() == 8);
    if (seen.size() != 8) return;
    for (int i = 0; i < 4; ++i) {
        CHECK(seen[(size_t) i].click.countIn);
        CHECK(seen[(size_t) i].click.beatInBar == i);
    }
    CHECK(!seen[4].click.countIn && seen[4].click.kind == BeatClock::Click::Bar && seen[4].click.beatInBar == 0);
    CHECK(seen[5].click.kind == BeatClock::Click::Beat);
    CHECK(clock.getCountInRemaining() == 0.0);

    // restarting brings the count-in back; following an external position has none
    clock.restart();
    CHECK(clock.getCountInRemaining() == 4.0);
    clock.setPosition(10.0);
    CHECK(clock.getCountInRemaining() == 0.0);
}

static void testNoDriftOverAnHour()
{
    // 113 bpm at 48 kHz: 25486.7 samples a beat, which the old schedule rounded down to 25486
    BeatClock clock;
    clock.setSampleRate(48000);
    clock.setTempo(113);
    clock.restart();
    const int64_t hour = 3600LL * 48000;
    auto seen = run(clock, hour, 256);
    const double samplesPerBeat = 60.0 * 48000 / 113;
    int64_t expectedBeats = (int64_t) std::floor((hour - 0.5) / samplesPerBeat) + 1;
    CHECK((int64_t) seen.size() == expectedBeats);
    double worst = 0;
    for (size_t i = 0; i < seen.size(); ++i) {
        double exact = i * samplesPerBeat;
        worst = std::max(worst, std::abs(seen[i].sample - exact));
    }
    CHECK(worst <= 1.0);  // every click within a sample of its exact time, even after an hour
}

static void testBlockSizesAgree()
{
    // the same clicks whatever the block size: nothing doubled or lost at block edges
    for (int block : { 1, 7, 64, 333, 4096 }) {
        BeatClock clock;
        clock.setSampleRate(44100);
        clock.setTempo(137);
        clock.setSubdivision(3);
        clock.restart();
        auto seen = run(clock, 10 * 44100, block);
        const double perClick = 60.0 * 44100 / 137 / 3;
        CHECK((int64_t) seen.size() == (int64_t) std::floor((10 * 44100 - 0.5) / perClick) + 1);
        bool ok = true;
        for (size_t i = 0; i < seen.size(); ++i) {
            if (seen[i].sample != (int64_t) std::floor(i * perClick + 0.5)) ok = false;
        }
        CHECK(ok);
    }
}

static void testTempoChangeAndLimits()
{
    BeatClock clock;
    clock.setSampleRate(48000);
    clock.setTempo(60);
    clock.restart();
    auto first = run(clock, 48000, 480);   // beat 0 at 0
    CHECK(first.size() == 1);
    clock.setTempo(120);                    // now at beat 1: next clicks every 24000 samples
    auto second = run(clock, 48000, 480);
    CHECK(second.size() == 2 && second[0].sample == 0 && second[1].sample == 24000);

    BeatClock::Click buf[2];
    clock.setTempo(0);
    CHECK(clock.process(512, buf, 2) == 0);
    clock.setTempo(400);
    clock.setSubdivision(8);
    clock.restart();
    CHECK(clock.process(48000, buf, 2) == 2);  // never writes more than asked

    clock.setBeatsPerBar(0);
    CHECK(clock.getBeatsPerBar() == 1);
    clock.setSubdivision(99);
    CHECK(clock.getSubdivision() == 8);
}

void runBeatClockTests()
{
    testBasicBeats();
    testBarsAndSubdivisions();
    testCountIn();
    testNoDriftOverAnHour();
    testBlockSizesAgree();
    testTempoChangeAndLimits();
}
