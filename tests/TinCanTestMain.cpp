// SPDX-License-Identifier: GPLv3-or-later WITH Appstore-exception
// Copyright (C) 2026 Tin Can Studios

#include "TinCanTest.h"

int failures = 0;

void runPacketCipherTests();
void runNotesTests();
void runInstrumentsTests();
void runBeatClockTests();
void runPitchTests();

int main()
{
    runPacketCipherTests();
    runNotesTests();
    runInstrumentsTests();
    runBeatClockTests();
    runPitchTests();

    if (failures == 0) {
        std::printf("TinCanTests: all checks passed\n");
        return 0;
    }
    std::printf("TinCanTests: %d failure(s)\n", failures);
    return 1;
}
