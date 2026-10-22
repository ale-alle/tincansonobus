// SPDX-License-Identifier: GPLv3-or-later WITH Appstore-exception
// Copyright (C) 2026 Tin Can Studios

#pragma once

#include "JuceHeader.h"
#include "SonobusPluginProcessor.h"

#include <functional>

// Tin Can lesson join (T-018 client side).
//
// The website's "Join lesson" button opens  tincan://join?ticket=<opaque>
// The app redeems the ticket with the app server and joins the room without
// ever showing or storing the group name, password or lesson key.
//
//   POST {apiBase}/api/rooms/redeem        Content-Type: application/json
//   {"ticket": "<ticket>"}
//   200 -> {"server": "host[:port]", "group": "...", "groupPassword": "...",
//           "lessonKey": "<base64 32 bytes>", "title": "Piano with Sam"}   (title optional)
//   4xx -> ticket expired, already used or not for this user
//
// The ticket is never written to logs or shown in the UI.
namespace TinCan {

struct JoinResult
{
    enum Status { Ok, InvalidLink, Rejected, NetworkError, BadResponse };

    Status status = InvalidLink;
    AooServerConnectionInfo info;
    String title;

    String getUserMessage() const;
};

// true if url uses the tincan:// scheme (whether or not it is valid)
bool isTinCanURL(const String & url);

// extracts the ticket from tincan://join?ticket=...; false if malformed
bool parseJoinURL(const String & url, String & ticketOut);

// app server base URL: TINCAN_API_BASE build define, overridable in development
// with the TINCAN_API_BASE environment variable. Only https (or http to localhost) is accepted.
String getApiBase();

// parses the redeem response body into result (exposed for tests)
JoinResult parseRedeemResponse(int httpStatus, const String & body);

// Redeems a ticket on a background thread and calls onDone on the message thread.
class TicketRedeemer : private Thread
{
public:
    TicketRedeemer(const String & ticket, std::function<void(const JoinResult &)> onDone);
    ~TicketRedeemer() override;

private:
    void run() override;

    String ticket;
    std::function<void(const JoinResult &)> onDone;
};

}
