// SPDX-License-Identifier: GPLv3-or-later WITH Appstore-exception
// Copyright (C) 2026 Tin Can Studios

#pragma once

#include <string>

// Update check decision (T-025), kept free of JUCE so TinCanTests can cover it.
// The website publishes  /app/version.json  =  {"latest": "0.9.1", "minimum": "0.9.0"}
//   latest  - newest installer on the Download page; older apps get a "new version" notice
//   minimum - oldest app that still works with lessons; older apps are told to update first
namespace TinCan {

// "1", "1.2" or "1.2.3" (digits only, at most 3 parts, each < 100000); false otherwise
bool parseVersion(const std::string & text, int out[3]);

// -1, 0 or 1; an unparseable version sorts before every valid one
int compareVersions(const std::string & a, const std::string & b);

enum class UpdateNeed { none, optional, required };

// an unparseable latest/minimum is ignored, so a bad file on the site never nags anyone
UpdateNeed updateNeed(const std::string & current, const std::string & latest, const std::string & minimum);

}
