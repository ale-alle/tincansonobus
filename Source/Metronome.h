// SPDX-License-Identifier: GPLv3-or-later WITH Appstore-exception
// Copyright (C) 2020 Jesse Chappell

#pragma once

#include "JuceHeader.h"
#include "TinCanBeatClock.h"

#include <atomic>


namespace SonoAudio
{

    class Metronome	{
    public:

        Metronome(double samplerate=44100.0);
        virtual ~Metronome();

        //void processData (int nframes, const signed short int *indata);

        void setSampleRate(double rate) { sampleRate = rate; }
        double getSampleRate() const { return sampleRate; }

        // the timestamp passed in should be relative to time zero for the current tempo
        // now beat-time instead!
        void processMix (int nframes, float * inOutDataL, float * inOutDataR, const double beatTime, bool relativeTime=false);

        void setTempo(double bpm);
        double getTempo() const { return mTempo; }

        void setBeatsPerBar(int num);
        int getBeatsPerBar() const { return mBeatsPerBar; }

        // Tin Can (T-036): clicks between beats (1 = none) and count-in bars played with the
        // accent sound when the metronome starts on its own (not when following a host or file)
        void setSubdivision(int num) { mSubdivision = num; }
        int getSubdivision() const { return mSubdivision; }
        void setCountInBars(int bars) { mCountInBars = bars; }
        int getCountInBars() const { return mCountInBars; }


        void resetRelativeStart(double startBeatPos = 0.0);
        void setRemainRatios(double barRemain, double beatRemain);

        // safe to read from any thread (the beat indicator reads it on the message thread)
        double getCurrBeatPos() const { return mCurrBeatPos.load(std::memory_order_relaxed); }
        double getCountInRemaining() const { return mCountInRemaining.load(std::memory_order_relaxed); }

        void setGain(float val, bool force=false);
        float getGain() const { return mPendingGain; }

        // loads the sampleset containing the samples to use for the click.
        // they must have labels corresponding to the interval they will sound
        //  "beat" and "bar" (optional)
        //bool loadSampleSet(const SampleSetInfo & sinfo);

        //const SampleSetInfo & getCurrentSamplesetInfo() const { return m_sampleSet.getInfo(); }

        void loadBeatSoundFromBinaryData(const void* data, size_t sizeBytes);
        void loadBarSoundFromBinaryData(const void* data, size_t sizeBytes);


    protected:

        int  blockSize;
        double sampleRate;

        double mTempo;
        int  mBeatsPerBar;
        int  mSubdivision = 1;
        int  mCountInBars = 0;
        float mGain;
        volatile float mPendingGain;

        // sample-accurate schedule; replaces the old per-block remain ratios, which rounded every
        // beat to a whole number of samples and so ran slightly fast
        TinCan::BeatClock mClock;
        std::atomic<double> mCurrBeatPos { 0.0 };
        std::atomic<double> mCountInRemaining { 0.0 };

        AudioSampleBuffer beatSoundBuffer;
        AudioSampleBuffer barSoundBuffer;

        AudioSampleBuffer tempBuffer;

        //SampleSet  m_sampleSet;
        //NonBlockingLock  mSampleLock;
        CriticalSection mSampleLock;

        struct SampleState
        {
            SampleState() : sampleData(0), samplePos(0), sampleRemain(0), sampleLength(0) {}
            ~SampleState() { }
            float * sampleData; // not owned by us
            long samplePos;
            long sampleRemain;
            long sampleLength;
            float gain = 1.0f;

            void start() { samplePos = 0; sampleRemain = sampleLength; }
            void render(float * dest, long n);
        };

        SampleState mBeatState;
        SampleState mBarState;
        SampleState mSubState;   // beat sound, quieter, between beats


        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Metronome)

    };


}
