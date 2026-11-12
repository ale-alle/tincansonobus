// SPDX-License-Identifier: GPLv3-or-later WITH Appstore-exception
// Copyright (C) 2026 Tin Can Studios

#include "TinCanVersion.h"

namespace TinCan {

bool parseVersion(const std::string & text, int out[3])
{
    out[0] = out[1] = out[2] = 0;
    int part = 0;
    int digits = 0;
    for (char ch : text) {
        if (ch == '.') {
            if (digits == 0 || part == 2) return false;
            ++part;
            digits = 0;
        } else if (ch >= '0' && ch <= '9') {
            if (++digits > 5) return false;
            out[part] = out[part] * 10 + (ch - '0');
        } else {
            return false;
        }
    }
    return digits > 0;
}

int compareVersions(const std::string & a, const std::string & b)
{
    int va[3], vb[3];
    const bool okA = parseVersion(a, va);
    const bool okB = parseVersion(b, vb);
    if (!okA || !okB) return okA == okB ? 0 : (okA ? 1 : -1);
    for (int i = 0; i < 3; ++i) {
        if (va[i] != vb[i]) return va[i] < vb[i] ? -1 : 1;
    }
    return 0;
}

UpdateNeed updateNeed(const std::string & current, const std::string & latest, const std::string & minimum)
{
    int v[3];
    if (!parseVersion(current, v)) return UpdateNeed::none;
    if (parseVersion(minimum, v) && compareVersions(current, minimum) < 0) return UpdateNeed::required;
    if (parseVersion(latest, v) && compareVersions(current, latest) < 0) return UpdateNeed::optional;
    return UpdateNeed::none;
}

}
