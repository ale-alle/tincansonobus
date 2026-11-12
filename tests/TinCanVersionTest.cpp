// SPDX-License-Identifier: GPLv3-or-later WITH Appstore-exception
// Copyright (C) 2026 Tin Can Studios

#include "TinCanTest.h"
#include "../Source/TinCanVersion.h"

using TinCan::UpdateNeed;

void runVersionTests()
{
    int v[3];
    CHECK(TinCan::parseVersion("0.9.1", v) && v[0] == 0 && v[1] == 9 && v[2] == 1);
    CHECK(TinCan::parseVersion("2", v) && v[0] == 2 && v[1] == 0 && v[2] == 0);
    CHECK(TinCan::parseVersion("1.10", v) && v[1] == 10);
    CHECK(!TinCan::parseVersion("", v));
    CHECK(!TinCan::parseVersion("1.", v));
    CHECK(!TinCan::parseVersion(".1", v));
    CHECK(!TinCan::parseVersion("1..2", v));
    CHECK(!TinCan::parseVersion("1.2.3.4", v));
    CHECK(!TinCan::parseVersion("v1.2", v));
    CHECK(!TinCan::parseVersion("1.2-beta", v));
    CHECK(!TinCan::parseVersion("123456", v));

    CHECK(TinCan::compareVersions("0.9.0", "0.9.0") == 0);
    CHECK(TinCan::compareVersions("0.9", "0.9.0") == 0);
    CHECK(TinCan::compareVersions("0.9.0", "0.10.0") < 0);   // numeric, not text
    CHECK(TinCan::compareVersions("1.0.0", "0.99.99") > 0);
    CHECK(TinCan::compareVersions("junk", "0.0.1") < 0);

    CHECK(TinCan::updateNeed("0.9.0", "0.9.0", "0.9.0") == UpdateNeed::none);
    CHECK(TinCan::updateNeed("0.9.0", "0.9.1", "0.9.0") == UpdateNeed::optional);
    CHECK(TinCan::updateNeed("0.9.0", "0.9.2", "0.9.1") == UpdateNeed::required);
    CHECK(TinCan::updateNeed("0.9.3", "0.9.2", "0.9.1") == UpdateNeed::none);  // a newer local build
    CHECK(TinCan::updateNeed("0.9.0", "", "") == UpdateNeed::none);           // empty file
    CHECK(TinCan::updateNeed("0.9.0", "oops", "0.9.1") == UpdateNeed::required);
    CHECK(TinCan::updateNeed("0.9.0", "0.9.1", "oops") == UpdateNeed::optional);
    CHECK(TinCan::updateNeed("dev", "0.9.1", "0.9.1") == UpdateNeed::none);
}
