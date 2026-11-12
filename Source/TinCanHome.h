// SPDX-License-Identifier: GPLv3-or-later WITH Appstore-exception
// Copyright (C) 2026 Tin Can Studios

#pragma once

#include "JuceHeader.h"

#include <functional>

// The start menu (T-071, D-028 as amended 2026-11-12).
//
// Outside a lesson the main window shows the website's own pages for signing in, My lessons and
// booking (tincanstudios.ca, in its "app mode": no marketing menu or footer, plus Set up your sound
// and Help). Sign-in stays on the website, so the app never handles passwords; the web view keeps the
// session like a browser would. Join lesson asks the app to join right here, with the same one-time
// ticket the website already uses.
//
// The web view only shows pages of that site. Other links (Stripe checkout, emails, anything else)
// open in the person's normal browser. If the site can't be shown, a native screen (retrying every 30 s)
// offers Try again and Set up your sound.
//
// Messages from the site's pages (src/lib/app.ts in the website repo):
//   {"t":"join","url":"canduet://join?ticket=…"}   join this lesson
//   {"t":"setup"}                                  open Set up your sound
//   {"t":"open","url":…}   in the normal browser: a Stripe page, or a page of the site that isn't
//                             part of the start menu (contact, policies)
namespace TinCan {

// TINCAN_HOME_PAGE (https, or http to localhost, for development), else <api base>/my-lessons
String getHomePage();

// Stripe's own payment pages, the only addresses a page may ask the app to open
bool isAllowedPaymentURL(const String & url);

class HomeView : public Component
{
public:
    struct Hooks
    {
        std::function<void(const String & joinUrl)> join;   // a canduet://join link
        std::function<void()> setup;
    };

    // nullptr when WebView2 can't start on this computer (the app then keeps SonoBus's window)
    static std::unique_ptr<HomeView> create(Hooks hooks);
    ~HomeView() override;

    // load My lessons again (after a lesson, so Join and the times are fresh)
    void reload();

    void resized() override;
    void paint(Graphics & g) override;
    void visibilityChanged() override;

private:
    class Browser;
    class Offline;

    explicit HomeView(Hooks hooks);
    void showOffline(bool offline);

    Hooks hooks;
    String page;
    std::unique_ptr<Browser> browser;
    std::unique_ptr<Offline> offline;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HomeView)
};

}
