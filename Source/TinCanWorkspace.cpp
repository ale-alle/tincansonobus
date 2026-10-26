// SPDX-License-Identifier: GPLv3-or-later WITH Appstore-exception
// Copyright (C) 2026 Tin Can Studios

#include "TinCanWorkspace.h"
#include "TinCanVideo.h"
#include "BinaryData.h"

namespace TinCan {

String getWorkspacePageURL(const String & displayName)
{
    auto dev = SystemStats::getEnvironmentVariable("TINCAN_WORKSPACE_PAGE", {}).trim();
    File index;
    if (dev.isNotEmpty() && File::isAbsolutePath(dev) && File(dev).getChildFile("index.html").existsAsFile()) {
        index = File(dev).getChildFile("index.html");
    }
    else {
        auto folder = File::getSpecialLocation(File::userApplicationDataDirectory).getChildFile("CanDuet").getChildFile("workspace");
        folder.createDirectory();
        const struct { const char * name; const char * data; int size; } files[] = {
            { "index.html", BinaryData::index_html, BinaryData::index_htmlSize },
            { "workspace-model.js", BinaryData::workspacemodel_js, BinaryData::workspacemodel_jsSize },
            { "workspace-ui.js", BinaryData::workspaceui_js, BinaryData::workspaceui_jsSize },
        };
        for (auto & f : files) {
            folder.getChildFile(f.name).replaceWithData(f.data, (size_t) f.size);
        }
        index = folder.getChildFile("index.html");
    }
    // the fragment stays on this computer; the page reads role and name from it
    return URL(index).toString(false) + "#role=teacher&name=" + URL::addEscapeChars(displayName.substring(0, 80), true);
}

#if JUCE_WEB_BROWSER && JUCE_USE_WIN_WEBVIEW2

namespace {

// Stays on the workspace page; links and new windows never open.
class WorkspaceBrowser : public WebBrowserComponent
{
public:
    WorkspaceBrowser(const Options & options, const String & page_) : WebBrowserComponent(options), page(page_) {}

    bool pageAboutToLoad(const String & newURL) override
    {
        return newURL.upToFirstOccurrenceOf("#", false, false) == page || newURL == "about:blank";
    }

    void newWindowAttemptingToLoad(const String &) override {}

private:
    String page;
};

}

WorkspaceWindow::WorkspaceWindow(const String & displayName)
    : DocumentWindow(TRANS("Lesson workspace"), Colour(0xff111317), DocumentWindow::allButtons)
{
    setUsingNativeTitleBar(true);
    setResizable(true, false);
    setResizeLimits(480, 320, 10000, 10000);

    auto options = makeWebViewOptions();
    if (WebBrowserComponent::areOptionsSupported(options)) {
        auto url = getWorkspacePageURL(displayName);
        auto browser = std::make_unique<WorkspaceBrowser>(options, url.upToFirstOccurrenceOf("#", false, false));
        browser->goToURL(url);
        setContentOwned(browser.release(), false);
    }
    else {
        auto * label = new Label({}, TRANS("The workspace needs Microsoft Edge WebView2, which isn't installed on this computer."));
        label->setJustificationType(Justification::centred);
        setContentOwned(label, false);
    }

    centreWithSize(1200, 800);
    setVisible(true);
}

#else

WorkspaceWindow::WorkspaceWindow(const String &)
    : DocumentWindow(TRANS("Lesson workspace"), Colour(0xff111317), DocumentWindow::allButtons)
{
    setUsingNativeTitleBar(true);
    auto * label = new Label({}, TRANS("The workspace isn't available in this build."));
    label->setJustificationType(Justification::centred);
    setContentOwned(label, false);
    centreWithSize(480, 200);
    setVisible(true);
}

#endif

WorkspaceWindow::~WorkspaceWindow() = default;

}
