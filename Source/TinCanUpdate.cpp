// SPDX-License-Identifier: GPLv3-or-later WITH Appstore-exception
// Copyright (C) 2026 Tin Can Studios

#include "TinCanUpdate.h"
#include "TinCanJoin.h"

namespace TinCan {

String getVersionFileURL() { return getApiBase() + "/app/version.json"; }

String getDownloadPageURL() { return getApiBase() + "/download"; }

UpdateChecker::UpdateChecker() : Thread("TinCanUpdateChecker")
{
    startThread(Thread::Priority::low);
}

UpdateChecker::~UpdateChecker()
{
    // the progress callback below stops a slow download as soon as this is asked
    stopThread(4000);
}

void UpdateChecker::run()
{
    int status = 0;
    String body;

    auto stream = URL(getVersionFileURL())
        .createInputStream(URL::InputStreamOptions(URL::ParameterHandling::inAddress)
                               .withExtraHeaders("Accept: application/json\r\nCache-Control: no-cache")
                               .withConnectionTimeoutMs(5000)
                               .withNumRedirectsToFollow(2)
                               .withStatusCode(&status)
                               .withProgressCallback([this](int, int) { return !threadShouldExit(); }));
    if (stream != nullptr && status == 200 && !threadShouldExit()) {
        body = stream->readString().substring(0, 4096);
    }

    var json;
    if (body.isNotEmpty() && JSON::parse(body, json).wasOk() && json.isObject()) {
        latest = json.getProperty("latest", var()).toString().trim().substring(0, 20);
        const String minimum = json.getProperty("minimum", var()).toString().trim().substring(0, 20);
        need = updateNeed(ProjectInfo::versionString, latest.toStdString(), minimum.toStdString());
    }
    // offline, a missing file or a bad file all mean "say nothing": the check never blocks a lesson
    done = true;
}

void showUpdateNotice(UpdateNeed need, const String & latestVersion)
{
    if (need == UpdateNeed::none) return;

    const String current = ProjectInfo::versionString;
    String title, message;
    if (need == UpdateNeed::required) {
        title = TRANS("Please update CanDuet");
        message = TRANS("This version of CanDuet (%1) is too old for lessons.").replace("%1", current) + "\n\n"
                + TRANS("Download version %1 from the Tin Can Studios website, install it, then join your lesson again. "
                        "Your settings are kept.").replace("%1", latestVersion);
    } else {
        title = TRANS("A new version of CanDuet is ready");
        message = TRANS("Version %1 is available (you have %2). Installing it takes a minute and keeps your settings.")
                      .replace("%1", latestVersion).replace("%2", current);
    }

    // no associated component: the box gets its own window, so it shows above the WebView2 video tiles
    AlertWindow::showOkCancelBox(need == UpdateNeed::required ? MessageBoxIconType::WarningIcon
                                                              : MessageBoxIconType::InfoIcon,
                                 title, message, TRANS("Open Download page"),
                                 need == UpdateNeed::required ? TRANS("Not now") : TRANS("Later"), nullptr,
                                 ModalCallbackFunction::create([](int result) {
                                     if (result == 1) URL(getDownloadPageURL()).launchInDefaultBrowser();
                                 }));
}

}
