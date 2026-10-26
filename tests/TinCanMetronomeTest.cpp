// SPDX-License-Identifier: GPLv3-or-later WITH Appstore-exception
// Copyright (C) 2026 Tin Can Studios

// Renders SonoBus's metronome (with the T-036 beat clock) using the real click sounds and checks
// the output sample for sample against the clicks placed by hand.
// cmake --build build --target TinCanAudioTests --config Release

#include <JuceHeader.h>
#include "../Source/Metronome.h"

#include <cstdio>
#include <vector>

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); ++failures; } } while (0)

static AudioBuffer<float> loadWav(const File & f, MemoryBlock & raw)
{
    f.loadFileAsData(raw);
    WavAudioFormat wav;
    std::unique_ptr<AudioFormatReader> reader(wav.createReaderFor(new MemoryInputStream(raw, false), true));
    AudioBuffer<float> buf;
    if (reader) {
        buf.setSize((int) reader->numChannels, (int) reader->lengthInSamples);
        reader->read(&buf, 0, (int) reader->lengthInSamples, 0, true, true);
    }
    return buf;
}

struct Expected { int64 sample; const AudioBuffer<float> * sound; float gain; };

static void addClick(std::vector<float> & out, const Expected & e)
{
    for (int i = 0; i < e.sound->getNumSamples() && e.sample + i < (int64) out.size(); ++i)
        out[(size_t) (e.sample + i)] += e.gain * e.sound->getSample(0, i);
}

// renders `seconds` in blocks; syncToPosition feeds the beat position like the host/file sync does
static std::vector<float> render(SonoAudio::Metronome & met, double rate, double seconds, int block, bool syncToPosition)
{
    std::vector<float> out((size_t) (rate * seconds), 0.0f);
    std::vector<float> right(out.size(), 0.0f);
    for (size_t pos = 0; pos < out.size(); pos += (size_t) block) {
        int n = (int) std::min<size_t>((size_t) block, out.size() - pos);
        double beat = pos * met.getTempo() / (60.0 * rate);
        met.processMix(n, out.data() + pos, right.data() + pos, beat, !syncToPosition);
    }
    for (size_t i = 0; i < out.size(); ++i) CHECK(out[i] == right[i]);  // same clicks on both sides
    return out;
}

static float maxDiff(const std::vector<float> & a, const std::vector<float> & b)
{
    float worst = 0.0f;
    for (size_t i = 0; i < a.size(); ++i) worst = std::max(worst, std::abs(a[i] - b[i]));
    return worst;
}

int main()
{
    ScopedJuceInitialiser_GUI init;  // message manager not needed, but harmless; keeps JUCE happy

    File images(TINCAN_TEST_IMAGES);
    MemoryBlock barRaw, beatRaw;
    auto bar = loadWav(images.getChildFile("bar_click.wav"), barRaw);
    auto beat = loadWav(images.getChildFile("beat_click.wav"), beatRaw);
    CHECK(bar.getNumSamples() > 0 && beat.getNumSamples() > 0);
    if (failures) return 1;

    const double rate = 48000.0;
    auto makeMet = [&]() {
        auto m = std::make_unique<SonoAudio::Metronome>(rate);
        m->loadBarSoundFromBinaryData(barRaw.getData(), barRaw.getSize());
        m->loadBeatSoundFromBinaryData(beatRaw.getData(), beatRaw.getSize());
        m->setTempo(120.0);   // 24000 samples a beat
        m->setGain(1.0f, true);
        return m;
    };

    // 1. SonoBus default: one beat per bar, beat sound only, a click every beat
    {
        auto m = makeMet();
        m->resetRelativeStart();
        auto out = render(*m, rate, 5.0, 512, false);
        std::vector<float> want(out.size(), 0.0f);
        for (int b = 0; b < 10; ++b) addClick(want, { b * 24000LL, &beat, 1.0f });
        float d = maxDiff(out, want);
        std::printf("  default metronome: max difference %g\n", d);
        CHECK(d < 1e-6f);
    }

    // 2. 4/4 with eighths: accent on 1, beat sound on 2-4, quieter clicks between
    {
        auto m = makeMet();
        m->setBeatsPerBar(4);
        m->setSubdivision(2);
        m->resetRelativeStart();
        auto out = render(*m, rate, 4.0, 480, false);
        std::vector<float> want(out.size(), 0.0f);
        for (int b = 0; b < 8; ++b) {
            addClick(want, { b * 24000LL, b % 4 == 0 ? &bar : &beat, 1.0f });
            addClick(want, { b * 24000LL + 12000, &beat, 0.4f });
        }
        float d = maxDiff(out, want);
        std::printf("  4/4 with eighths: max difference %g\n", d);
        CHECK(d < 1e-5f);
    }

    // 3. one bar of count-in: the first four beats all accented, then 4/4
    {
        auto m = makeMet();
        m->setBeatsPerBar(4);
        m->setCountInBars(1);
        m->resetRelativeStart();
        CHECK(m->getCountInRemaining() == 4.0);
        auto out = render(*m, rate, 4.0, 256, false);
        std::vector<float> want(out.size(), 0.0f);
        for (int b = 0; b < 8; ++b) addClick(want, { b * 24000LL, (b < 4 || b == 4) ? &bar : &beat, 1.0f });
        float d = maxDiff(out, want);
        std::printf("  count-in: max difference %g\n", d);
        CHECK(d < 1e-6f);
        CHECK(m->getCountInRemaining() == 0.0);
        CHECK(std::abs(m->getCurrBeatPos() - 8.0) < 1e-9);
    }

    // 4. following a host/file position gives the same clicks (no count-in there)
    {
        auto m = makeMet();
        m->setBeatsPerBar(3);
        m->setCountInBars(2);
        auto out = render(*m, rate, 3.0, 333, true);
        std::vector<float> want(out.size(), 0.0f);
        for (int b = 0; b < 6; ++b) addClick(want, { b * 24000LL, b % 3 == 0 ? &bar : &beat, 1.0f });
        float d = maxDiff(out, want);
        std::printf("  synced to position: max difference %g\n", d);
        CHECK(d < 1e-6f);
    }

    // 5. gain 0 is silent (what the processor does when the metronome is switched off)
    {
        auto m = makeMet();
        m->setGain(0.0f, true);
        m->resetRelativeStart();
        auto out = render(*m, rate, 1.0, 512, false);
        CHECK(maxDiff(out, std::vector<float>(out.size(), 0.0f)) == 0.0f);
    }

    if (failures == 0) {
        std::printf("TinCanAudioTests: all checks passed\n");
        return 0;
    }
    std::printf("TinCanAudioTests: %d failure(s)\n", failures);
    return 1;
}
