// SPDX-License-Identifier: GPLv3-or-later WITH Appstore-exception
// Copyright (C) 2026 Tin Can Studios

#include "TinCanTest.h"
#include "../Source/TinCanPitch.h"

#include <cmath>
#include <cstdint>
#include <random>
#include <vector>

using namespace TinCan;

static const double pi = 3.14159265358979323846;

static std::vector<float> sine(double hz, double rate, int n, double amp = 0.5, double phase = 0.3)
{
    std::vector<float> v((size_t) n);
    for (int i = 0; i < n; ++i) v[(size_t) i] = (float) (amp * std::sin(2 * pi * hz * i / rate + phase));
    return v;
}

// harmonic-rich tone (like a plucked string or a reed): fundamental plus decaying harmonics
static std::vector<float> rich(double hz, double rate, int n, double amp = 0.4)
{
    std::vector<float> v((size_t) n, 0.0f);
    for (int h = 1; h <= 8 && hz * h < rate / 2; ++h) {
        double a = amp / h;
        if (h == 2) a = amp * 0.9;  // strong second harmonic: the classic octave-error trap
        for (int i = 0; i < n; ++i) v[(size_t) i] += (float) (a * std::sin(2 * pi * hz * h * i / rate + h));
    }
    return v;
}

static double centsOff(double measured, double actual)
{
    return 1200.0 * std::log2(measured / actual);
}

static void testCleanTones()
{
    for (double rate : { 44100.0, 48000.0, 96000.0 }) {
        PitchDetector det;
        det.prepare(rate, (int) std::lround(4096 * rate / 48000.0));
        double worst = 0.0;
        bool allPitched = true;
        // B0 (5-string bass) up to C7, in steps that land between semitones too
        for (double note = 23.0; note <= 96.0; note += 0.37) {
            double hz = noteToFrequency(note);
            auto x = sine(hz, rate, det.getWindow());
            auto r = det.analyse(x.data());
            if (r.status != PitchResult::Pitched) { allPitched = false; continue; }
            worst = std::max(worst, std::abs(centsOff(r.frequency, hz)));
        }
        CHECK(allPitched);
        CHECK(worst <= 1.0);
        std::printf("  tuner: worst error %.3f cents over B0-C7 at %.0f Hz\n", worst, rate);
    }
}

static void testRichTonesNoOctaveErrors()
{
    PitchDetector det;
    det.prepare(48000.0, 4096);
    for (double note : { 28.0, 40.0, 45.0, 52.0, 57.0, 64.0, 69.0, 76.0, 84.0 }) {
        double hz = noteToFrequency(note);
        auto x = rich(hz, 48000.0, det.getWindow());
        auto r = det.analyse(x.data());
        CHECK(r.status == PitchResult::Pitched);
        CHECK(std::abs(centsOff(r.frequency, hz)) <= 2.0);
    }
}

static void testSilenceQuietNoise()
{
    PitchDetector det;
    det.prepare(48000.0, 4096);
    std::vector<float> silence(4096, 0.0f);
    CHECK(det.analyse(silence.data()).status == PitchResult::NoSignal);

    auto quiet = sine(440.0, 48000.0, 4096, 0.001);  // about -63 dBFS
    auto r = det.analyse(quiet.data());
    CHECK(r.status == PitchResult::TooQuiet);

    std::mt19937 rng(42);
    std::normal_distribution<float> gauss(0.0f, 0.2f);
    std::vector<float> noise(4096);
    for (auto & s : noise) s = gauss(rng);
    r = det.analyse(noise.data());
    CHECK(r.status == PitchResult::Unclear);
    CHECK(r.clarity < 0.8);

    // a clear tone with some noise still reads correctly (about 20 dB signal to noise)
    auto tone = sine(196.0, 48000.0, 4096, 0.5);
    std::normal_distribution<float> hiss(0.0f, 0.035f);
    for (auto & s : tone) s += hiss(rng);
    r = det.analyse(tone.data());
    CHECK(r.status == PitchResult::Pitched);
    CHECK(std::abs(centsOff(r.frequency, 196.0)) <= 3.0);
}

static void testNoteMaths()
{
    CHECK(std::abs(frequencyToNote(440.0) - 69.0) < 1e-9);
    CHECK(std::abs(frequencyToNote(261.6255653) - 60.0) < 1e-6);
    CHECK(std::abs(frequencyToNote(442.0, 442.0) - 69.0) < 1e-9);
    // A4 = 440 sounds 7.85 cents flat against A4 = 442
    CHECK(std::abs((frequencyToNote(440.0, 442.0) - 69.0) * 100.0 + 7.85) < 0.01);
    CHECK(std::abs(noteToFrequency(57.0) - 220.0) < 1e-9);
}

static void testRing()
{
    SampleRing ring(8);   // rounds up to 8
    float out[8];
    CHECK(!ring.readLatest(out, 4));
    float a[3] = { 1, 2, 3 };
    ring.write(a, 3);
    CHECK(!ring.readLatest(out, 4));
    float b[5] = { 4, 5, 6, 7, 8 };
    ring.write(b, 5);
    CHECK(ring.readLatest(out, 4) && out[0] == 5 && out[3] == 8);
    float c[6] = { 9, 10, 11, 12, 13, 14 };  // wraps around, overwriting the oldest
    ring.write(c, 6);
    CHECK(ring.readLatest(out, 8) && out[0] == 7 && out[7] == 14);
    CHECK(!ring.readLatest(out, 9));
    ring.reset();
    CHECK(!ring.readLatest(out, 1));
}

void runPitchTests()
{
    testCleanTones();
    testRichTonesNoOctaveErrors();
    testSilenceQuietNoise();
    testNoteMaths();
    testRing();
}
