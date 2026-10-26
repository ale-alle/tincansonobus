// SPDX-License-Identifier: GPLv3-or-later WITH Appstore-exception
// Copyright (C) 2020 Jesse Chappell


#include "Metronome.h"
#include <cmath>
//#include "utility.h"

using namespace SonoAudio;

Metronome::Metronome(double samplerate)
: sampleRate(samplerate), mTempo(0), mBeatsPerBar(1), mGain(1.0f), mPendingGain(1.0f)
{
    tempBuffer.setSize(1, 4096);
    mSubState.gain = 0.4f;
}

Metronome::~Metronome()
{
}

//void processData (int nframes, const signed short int *indata);

void Metronome::setGain(float val, bool force)
{
    mPendingGain = val;
    if (force) {
        mGain = val;
    }
}



void Metronome::SampleState::render(float * dest, long n)
{
    long count = std::min(n, sampleRemain);
    for (long i = 0; i < count; ++i)
    {
        dest[i] += gain * sampleData[samplePos + i];
    }
    sampleRemain -= count;
    samplePos += count;
}

// the timestamp passed in should be relative to time zero for the current tempo
void Metronome::processMix (int nframes, float * inOutDataL, float * inOutDataR, const double beatTime, bool relativeTime)
{
    const ScopedTryLock slock (mSampleLock);

    if (!slock.isLocked() || beatSoundBuffer.getNumSamples() == 0 || mTempo == 0.0) {
	    //cerr << "samples locked, not rendering" << endl;
	    return;
    }
    
    // just in case, shouldn't ever happen
    if (tempBuffer.getNumSamples() < nframes) {
        tempBuffer.setSize(1, nframes);
    }
    
    tempBuffer.clear(0, nframes);

    mClock.setSampleRate(sampleRate);
    mClock.setTempo(mTempo);
    mClock.setBeatsPerBar(mBeatsPerBar);
    mClock.setSubdivision(mSubdivision);
    if (!relativeTime) {
        // following the host or file playback position
        mClock.setPosition(beatTime);
    }

    TinCan::BeatClock::Click clicks[64];
    int numClicks = mClock.process(nframes, clicks, 64);

    float * metbuf = tempBuffer.getWritePointer(0);
    long pos = 0;

    for (int c = 0; c <= numClicks; ++c)
    {
        long until = c < numClicks ? clicks[c].offset : nframes;
        if (until > pos) {
            // run the sounds already playing up to the next click
            mBarState.render(metbuf + pos, until - pos);
            mBeatState.render(metbuf + pos, until - pos);
            mSubState.render(metbuf + pos, until - pos);
            pos = until;
        }
        if (c == numClicks) break;

        const auto & click = clicks[c];
        if (click.kind == TinCan::BeatClock::Click::Sub) {
            mSubState.start();
        }
        else if (click.kind == TinCan::BeatClock::Click::Bar || click.countIn) {
            // count-in beats all use the accent so they stand apart from the bars that follow
            mBarState.start();
            mBeatState.sampleRemain = 0;
        }
        else {
            mBeatState.start();
        }
    }
    
    // apply gain
    if (abs(mPendingGain - mGain) > 0.0001f) {
        tempBuffer.applyGainRamp(0, nframes, mGain, mPendingGain);
        mGain = mPendingGain;
    }
    else if (mGain != 1.0f){
        tempBuffer.applyGain(0, nframes, mGain);
    }
    
    // add to audio data going out
    FloatVectorOperations::add(inOutDataL, tempBuffer.getReadPointer(0), nframes);
    if (inOutDataR != inOutDataL) {
        FloatVectorOperations::add(inOutDataR, tempBuffer.getReadPointer(0), nframes);
    }

    mCurrBeatPos.store(mClock.getPosition(), std::memory_order_relaxed);
    mCountInRemaining.store(mClock.getCountInRemaining(), std::memory_order_relaxed);
}

void Metronome::resetRelativeStart(double startBeatPos)
{
    mClock.setBeatsPerBar(mBeatsPerBar);
    mClock.setCountInBars(startBeatPos == 0.0 ? mCountInBars : 0);
    mClock.restart();
    if (startBeatPos != 0.0) {
        mClock.setPosition(startBeatPos);
    }
    mCurrBeatPos.store(mClock.getPosition(), std::memory_order_relaxed);
    mCountInRemaining.store(mClock.getCountInRemaining(), std::memory_order_relaxed);
}

void Metronome::setRemainRatios(double barRemain, double beatRemain)
{
    // kept for compatibility: place the clock at the matching point of the current bar
    ignoreUnused(beatRemain);
    mClock.setPosition((1.0 - barRemain) * mBeatsPerBar);
}


void Metronome::setTempo(double bpm)
{
    mTempo = bpm;
}

void Metronome::setBeatsPerBar(int num)
{
    mBeatsPerBar = num;
}


void Metronome::loadBeatSoundFromBinaryData(const void* data, size_t sizeBytes)
{
    const ScopedLock slock (mSampleLock);

    WavAudioFormat wavFormat;
    std::unique_ptr<AudioFormatReader> reader (wavFormat.createReaderFor (new MemoryInputStream (data, sizeBytes, false), true));
    if (reader.get() != nullptr)
    {
        beatSoundBuffer.setSize((int)reader->numChannels, (int)reader->lengthInSamples);
        reader->read(&beatSoundBuffer, 0, (int)reader->lengthInSamples, 0, true, true);
        DBG("Read beat sound of " << beatSoundBuffer.getNumSamples() << " samples");
        mBeatState.sampleData = beatSoundBuffer.getWritePointer(0);
        mBeatState.sampleLength = beatSoundBuffer.getNumSamples();
        mBeatState.samplePos = 0;
        mBeatState.sampleRemain = 0;
        mSubState.sampleData = mBeatState.sampleData;
        mSubState.sampleLength = mBeatState.sampleLength;
        mSubState.samplePos = 0;
        mSubState.sampleRemain = 0;
    }
}

void Metronome::loadBarSoundFromBinaryData(const void* data, size_t sizeBytes)
{
    const ScopedLock slock (mSampleLock);

    WavAudioFormat wavFormat;
    std::unique_ptr<AudioFormatReader> reader (wavFormat.createReaderFor (new MemoryInputStream (data, sizeBytes, false), true));
    if (reader.get() != nullptr)
    {
        barSoundBuffer.setSize((int)reader->numChannels, (int)reader->lengthInSamples);
        reader->read(&barSoundBuffer, 0, (int)reader->lengthInSamples, 0, true, true);
        DBG("Read bar sound of " << barSoundBuffer.getNumSamples() << " samples");
        
        mBarState.sampleData = barSoundBuffer.getWritePointer(0);
        mBarState.sampleLength = barSoundBuffer.getNumSamples();
        mBarState.samplePos = 0;
        mBarState.sampleRemain = 0;
    }
}
