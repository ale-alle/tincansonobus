// SPDX-License-Identifier: GPLv3-or-later WITH Appstore-exception
// Copyright (C) 2026 Tin Can Studios

#pragma once

#include "JuceHeader.h"
#include "TinCanVersion.h"

#include <atomic>

// Update check (T-025): once per start-up the app reads {apiBase}/app/version.json (TinCanVersion.h)
// and, if this version is older, offers to open the website's Download page. Nothing is downloaded or
// run by the app itself: the person installs the new version from the site (the installer is unsigned
// for the pilot, so it should come from a page they recognise). Replaces SonoBus's GitHub updater,
// which stays compiled out (TINCAN_SONOBUS_UPDATER).
namespace TinCan {

class UpdateChecker : private Thread
{
public:
    UpdateChecker();             // starts the check in the background
    ~UpdateChecker() override;

    // message thread: true once the check has finished (whatever the outcome)
    bool isDone() const { return done.load(); }
    UpdateNeed getNeed() const { return need; }
    String getLatestVersion() const { return latest; }

private:
    void run() override;

    std::atomic<bool> done { false };
    UpdateNeed need = UpdateNeed::none;  // written by run() before done is set
    String latest;
};

String getVersionFileURL();   // {apiBase}/app/version.json
String getDownloadPageURL();  // {apiBase}/download - fixed, never taken from the version file

// shows the notice in its own window (above the video tiles) with a button that opens the Download page
void showUpdateNotice(UpdateNeed need, const String & latestVersion);

}
