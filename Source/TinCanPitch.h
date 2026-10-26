// SPDX-License-Identifier: GPLv3-or-later WITH Appstore-exception
// Copyright (C) 2026 Tin Can Studios

#pragma once

#include <atomic>
#include <cstdint>
#include <vector>

// Chromatic tuner pitch detection (T-036). YIN (de Cheveigné & Kawahara 2002) on a 4x
// decimated copy finds the period cheaply, then the period is refined at the full sample rate
// for cent accuracy. Runs on the tuner's own thread, never the audio or UI thread. No JUCE
// (unit tested in tests/TinCanPitchTest.cpp).
namespace TinCan {

struct PitchResult
{
    enum Status { NoSignal, TooQuiet, Unclear, Pitched };

    Status status = NoSignal;
    double frequency = 0.0;   // Hz, when Pitched
    double level = 0.0;       // RMS of the analysed window, 0..1
    double clarity = 0.0;     // 1 - YIN aperiodicity, 0..1
};

class PitchDetector
{
public:
    // window = samples analysed at the full rate (power of two not required); allocates
    void prepare(double sampleRate, int window = 4096);
    double getSampleRate() const { return sampleRate; }
    int getWindow() const { return window; }

    PitchResult analyse(const float * samples);  // exactly getWindow() samples

    double minFrequency = 27.0;   // below the lowest bass string (B0 = 30.9 Hz)
    double maxFrequency = 2100.0; // above the top of most instruments' fundamentals
    double quietLevel = 0.003;    // RMS, about -50 dBFS
    double threshold = 0.15;      // YIN absolute threshold
    double maxAperiodicity = 0.35;// above this the pitch is "unclear"

private:
    double sampleRate = 48000.0;
    int window = 4096;
    int decimation = 4;
    std::vector<float> low;       // decimated window
    std::vector<double> diff;     // YIN difference / CMND at the decimated rate
    std::vector<double> fineVals; // full-rate differences around the coarse period
};

// note maths with a configurable A4
double frequencyToNote(double hz, double a4 = 440.0);   // fractional MIDI note
double noteToFrequency(double note, double a4 = 440.0);

// Single-producer single-consumer float ring (audio thread writes, tuner thread reads the
// latest samples). Fixed size, never allocates or blocks after construction.
class SampleRing
{
public:
    explicit SampleRing(int capacityPowerOfTwo = 16384);

    void write(const float * data, int n);           // drops the oldest data when full
    // copies the most recent n samples (n <= capacity); false until that many were written
    bool readLatest(float * dest, int n) const;
    void reset() { written.store(0, std::memory_order_release); }

private:
    std::vector<float> buffer;
    uint32_t mask;
    std::atomic<uint64_t> written { 0 };
};

}
