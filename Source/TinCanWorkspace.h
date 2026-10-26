// SPDX-License-Identifier: GPLv3-or-later WITH Appstore-exception
// Copyright (C) 2026 Tin Can Studios

#pragma once

#include "JuceHeader.h"

// Tin Can lesson workspace (T-037, D-019): the page in workspace-page/ (built into the app) shown
// in WebView2. Images and whiteboards on a board, with pen, highlighter, arrows and text.
// Stage 1 is local: files never leave this computer. See doc/TINCAN-WORKSPACE.md.
namespace TinCan {

// file:// address of the workspace page. The page is written from the app's resources to
// %APPDATA%/CanDuet/workspace/ each time (so it always matches this build); the
// TINCAN_WORKSPACE_PAGE environment variable can point at a folder with index.html instead,
// for working on the page without rebuilding.
String getWorkspacePageURL(const String & displayName);

// Closing only hides the window, so the board is still there when it's opened again.
class WorkspaceWindow : public DocumentWindow
{
public:
    explicit WorkspaceWindow(const String & displayName);
    ~WorkspaceWindow() override;

    void closeButtonPressed() override { setVisible(false); }

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WorkspaceWindow)
};

}
