// Signed lesson groups (T-048): only the rooms-worker can name a group the server will create.
//   group name = <room>.<expires>.<signature>
//   room       = the lesson's random group name, [A-Za-z0-9-], 1-64 characters
//   expires    = Unix seconds (lesson end + 15 min)
//   signature  = base64url, no padding, of the first 16 bytes of
//                HMAC-SHA256(secret, "<room>.<expires>")
// The secret is shared only by the rooms-worker (RELAY_GROUP_SECRET) and the server.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace tincan {

void hmacSha256(const std::string & key, const std::string & message, uint8_t out[32]);

enum class GroupCheck { ok, malformed, badSignature, expired, tooFarAhead };

// maxAheadSeconds: how far in the future `expires` may be (a lesson's join window is a few hours)
GroupCheck checkSignedGroup(const std::string & name, const std::string & secret, int64_t nowSeconds,
                            int64_t maxAheadSeconds = 3 * 24 * 3600);

const char * groupCheckText(GroupCheck result);

} // namespace tincan
