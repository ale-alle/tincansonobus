// SPDX-License-Identifier: GPLv3-or-later WITH Appstore-exception
// Copyright (C) 2026 Tin Can Studios

#pragma once

#include "JuceHeader.h"

// Name of the folder (and settings file) under the user's application data, e.g. %APPDATA%\CanDuet.
// A test build can set TINCAN_DATA_FOLDER in CMake (e.g. "CanDuet-Tiles") so it never reads or
// resets the settings of the copy people use for lessons.
#ifndef TINCAN_DATA_FOLDER
 #define TINCAN_DATA_FOLDER "CanDuet"
#endif

namespace TinCan {

inline String getDataFolderName() { return TINCAN_DATA_FOLDER; }

inline File getDataFolder()
{
    return File::getSpecialLocation(File::userApplicationDataDirectory).getChildFile(getDataFolderName());
}

}
