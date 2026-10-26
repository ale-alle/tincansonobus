// SPDX-License-Identifier: GPLv3-or-later WITH Appstore-exception
// Copyright (C) 2026 Tin Can Studios

#include "TinCanPitch.h"

#include <algorithm>
#include <cmath>

namespace TinCan {

void PitchDetector::prepare(double rate, int windowSize)
{
    sampleRate = rate > 0 ? rate : 48000.0;
    window = std::max(256, windowSize);
    // keep the decimated rate near 12 kHz: enough for fundamentals up to ~2 kHz
    decimation = std::max(1, (int) std::lround(sampleRate / 12000.0));
    low.assign((size_t) (window / decimation), 0.0f);
    diff.assign(low.size() / 2 + 1, 0.0);
    fineVals.assign((size_t) (2 * (decimation + 1) + 1), 0.0);
}

// YIN difference d(tau) at the full rate, for one tau
static double difference(const float * x, int n, int tau)
{
    double sum = 0.0;
    for (int i = 0; i + tau < n; ++i) {
        double d = (double) x[i] - x[i + tau];
        sum += d * d;
    }
    // normalise by the number of terms so different taus compare fairly
    return sum / std::max(1, n - tau);
}

PitchResult PitchDetector::analyse(const float * x)
{
    PitchResult result;

    double energy = 0.0;
    for (int i = 0; i < window; ++i) energy += (double) x[i] * x[i];
    result.level = std::sqrt(energy / window);
    if (result.level < 1e-6) { result.status = PitchResult::NoSignal; return result; }
    if (result.level < quietLevel) { result.status = PitchResult::TooQuiet; return result; }

    // decimate with a boxcar average (a crude low-pass, fine for finding the period)
    const int n = (int) low.size();
    for (int i = 0; i < n; ++i) {
        float s = 0.0f;
        for (int k = 0; k < decimation; ++k) s += x[i * decimation + k];
        low[(size_t) i] = s / decimation;
    }

    const double lowRate = sampleRate / decimation;
    const int tauMin = std::max(2, (int) std::floor(lowRate / maxFrequency));
    const int tauMax = std::min((int) diff.size() - 1, (int) std::ceil(lowRate / minFrequency));
    if (tauMax <= tauMin + 2) { result.status = PitchResult::Unclear; return result; }

    // difference function and cumulative mean normalised difference (CMND)
    const int half = n / 2;
    diff[0] = 1.0;
    double running = 0.0;
    for (int tau = 1; tau <= tauMax; ++tau) {
        double sum = 0.0;
        for (int i = 0; i < half; ++i) {
            double d = (double) low[(size_t) i] - low[(size_t) (i + tau)];
            sum += d * d;
        }
        running += sum;
        diff[(size_t) tau] = running > 0 ? sum * tau / running : 1.0;
    }

    // first dip under the threshold (then its local minimum); else the global minimum
    int best = -1;
    for (int tau = tauMin; tau <= tauMax; ++tau) {
        if (diff[(size_t) tau] < threshold) {
            while (tau + 1 <= tauMax && diff[(size_t) (tau + 1)] < diff[(size_t) tau]) ++tau;
            best = tau;
            break;
        }
    }
    if (best < 0) {
        best = tauMin;
        for (int tau = tauMin + 1; tau <= tauMax; ++tau) {
            if (diff[(size_t) tau] < diff[(size_t) best]) best = tau;
        }
    }

    double aperiodicity = diff[(size_t) best];
    result.clarity = std::max(0.0, 1.0 - aperiodicity);
    if (aperiodicity > maxAperiodicity) { result.status = PitchResult::Unclear; return result; }

    // refine at the full rate around the coarse period, then interpolate between samples
    const int centre = best * decimation;
    const int span = decimation + 1;
    const int lo = std::max(1, centre - span);
    const int hi = std::min(window / 2, centre + span);
    int fine = lo;
    double fineVal = 1e300;
    auto & vals = fineVals;
    for (int tau = lo; tau <= hi; ++tau) {
        double v = difference(x, window, tau);
        vals[(size_t) (tau - lo)] = v;
        if (v < fineVal) { fineVal = v; fine = tau; }
    }
    double period = fine;
    if (fine > lo && fine < hi) {
        double a = vals[(size_t) (fine - lo - 1)], b = vals[(size_t) (fine - lo)], c = vals[(size_t) (fine - lo + 1)];
        double denom = a - 2 * b + c;
        if (denom > 0) period += 0.5 * (a - c) / denom;
    }

    result.frequency = sampleRate / period;
    if (result.frequency < minFrequency || result.frequency > maxFrequency) {
        result.status = PitchResult::Unclear;
        return result;
    }
    result.status = PitchResult::Pitched;
    return result;
}

double frequencyToNote(double hz, double a4)
{
    return 69.0 + 12.0 * std::log2(hz / a4);
}

double noteToFrequency(double note, double a4)
{
    return a4 * std::pow(2.0, (note - 69.0) / 12.0);
}

SampleRing::SampleRing(int capacity)
{
    int cap = 1;
    while (cap < capacity) cap <<= 1;
    buffer.assign((size_t) cap, 0.0f);
    mask = (uint32_t) cap - 1;
}

void SampleRing::write(const float * data, int n)
{
    uint64_t w = written.load(std::memory_order_relaxed);
    for (int i = 0; i < n; ++i) buffer[(size_t) ((w + (uint64_t) i) & mask)] = data[i];
    written.store(w + (uint64_t) n, std::memory_order_release);
}

bool SampleRing::readLatest(float * dest, int n) const
{
    uint64_t w = written.load(std::memory_order_acquire);
    if (n > (int) buffer.size() || w < (uint64_t) n) return false;
    uint64_t start = w - (uint64_t) n;
    for (int i = 0; i < n; ++i) dest[i] = buffer[(size_t) ((start + (uint64_t) i) & mask)];
    // the writer may have lapped part of what we copied; with a ring 4x the window and reads
    // of the newest samples that would take most of a second, so it's not checked further
    return true;
}

}
