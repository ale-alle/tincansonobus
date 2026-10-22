// SPDX-License-Identifier: GPLv3-or-later WITH Appstore-exception
// Copyright (C) 2026 Tin Can Studios

#include "TinCanJoin.h"

#ifndef TINCAN_API_BASE
#define TINCAN_API_BASE "https://tincanstudios.ca"
#endif

namespace TinCan {

String JoinResult::getUserMessage() const
{
    switch (status) {
        case Ok:           return {};
        case InvalidLink:  return TRANS("This lesson link isn't valid. Go back to My lessons and press Join again.");
        case Rejected:     return TRANS("This lesson link has expired or isn't for your account. Go back to My lessons and press Join again.");
        case NetworkError: return TRANS("Couldn't reach Tin Can to join your lesson. Check your internet connection and try again.");
        case BadResponse:  return TRANS("Tin Can sent an unexpected reply. Please try again, or contact support if it keeps happening.");
    }
    return {};
}

bool isTinCanURL(const String & url)
{
    return url.trim().startsWithIgnoreCase("tincan:");
}

static bool isTicketChar(juce_wchar c)
{
    return CharacterFunctions::isLetterOrDigit(c) || c == '-' || c == '_' || c == '.' || c == '~';
}

bool parseJoinURL(const String & urlstr, String & ticketOut)
{
    // Windows may hand us the URL wrapped in quotes
    String s = urlstr.trim().unquoted();

    if (!s.startsWithIgnoreCase("tincan://join")) return false;

    String rest = s.substring(String("tincan://join").length());
    if (rest.startsWithChar('/')) rest = rest.substring(1);
    if (!rest.startsWithChar('?')) return false;

    StringArray params;
    params.addTokens(rest.substring(1), "&", "");

    String ticket;
    for (auto & p : params) {
        if (p.upToFirstOccurrenceOf("=", false, false) == "ticket") {
            if (ticket.isNotEmpty()) return false; // ambiguous
            ticket = URL::removeEscapeChars(p.fromFirstOccurrenceOf("=", false, false));
        }
    }

    if (ticket.isEmpty() || ticket.length() > 4096) return false;
    for (auto c : ticket) {
        if (!isTicketChar(c)) return false;
    }

    ticketOut = ticket;
    return true;
}

static bool isAllowedApiBase(const String & base)
{
    URL url(base);
    if (base.startsWithIgnoreCase("https://")) return url.getDomain().isNotEmpty();
    if (base.startsWithIgnoreCase("http://")) {
        auto host = url.getDomain().upToFirstOccurrenceOf(":", false, false);
        return host == "localhost" || host == "127.0.0.1";
    }
    return false;
}

String getApiBase()
{
    String base = SystemStats::getEnvironmentVariable("TINCAN_API_BASE", {}).trim();
    if (base.isEmpty() || !isAllowedApiBase(base)) {
        base = TINCAN_API_BASE;
    }
    while (base.endsWithChar('/')) base = base.dropLastCharacters(1);
    return base;
}

JoinResult parseRedeemResponse(int httpStatus, const String & body)
{
    JoinResult result;

    if (httpStatus == 0) { result.status = JoinResult::NetworkError; return result; }
    if (httpStatus >= 400 && httpStatus < 500) { result.status = JoinResult::Rejected; return result; }
    if (httpStatus != 200) { result.status = httpStatus >= 500 ? JoinResult::NetworkError : JoinResult::BadResponse; return result; }

    result.status = JoinResult::BadResponse;

    var json;
    if (JSON::parse(body, json).failed() || !json.isObject()) return result;

    String server = json.getProperty("server", var()).toString().trim();
    String group  = json.getProperty("group", var()).toString();
    String pass   = json.getProperty("groupPassword", var()).toString();
    String key    = json.getProperty("lessonKey", var()).toString().trim();

    String host = server.upToFirstOccurrenceOf(":", false, false);
    int port = server.contains(":") ? server.fromFirstOccurrenceOf(":", false, false).getIntValue() : DEFAULT_SERVER_PORT;

    uint8_t keybytes[PacketCipher::KeySize];
    bool keyok = PacketCipher::decodeKey(key.toRawUTF8(), keybytes);
    PacketCipher::wipe(keybytes, sizeof(keybytes));

    if (host.isEmpty() || port <= 0 || port > 65535 || group.isEmpty() || !keyok) return result;

    result.info.serverHost = host;
    result.info.serverPort = port;
    result.info.groupName = group;
    result.info.groupPassword = pass;
    result.info.groupIsPublic = false;
    result.info.lessonKey = key;
    result.title = json.getProperty("title", var()).toString().trim().substring(0, 80);
    result.status = JoinResult::Ok;
    return result;
}

TicketRedeemer::TicketRedeemer(const String & ticket_, std::function<void(const JoinResult &)> onDone_)
    : Thread("TinCanTicketRedeemer"), ticket(ticket_), onDone(std::move(onDone_))
{
    startThread();
}

TicketRedeemer::~TicketRedeemer()
{
    stopThread(12000);
}

void TicketRedeemer::run()
{
    DynamicObject::Ptr req = new DynamicObject();
    req->setProperty("ticket", ticket);

    URL url = URL(getApiBase() + "/api/rooms/redeem").withPOSTData(JSON::toString(var(req.get()), true));

    int status = 0;
    String body;

    auto stream = url.createInputStream(URL::InputStreamOptions(URL::ParameterHandling::inPostData)
                                        .withHttpRequestCmd("POST")
                                        .withExtraHeaders("Content-Type: application/json\r\nAccept: application/json")
                                        .withConnectionTimeoutMs(10000)
                                        .withNumRedirectsToFollow(0)
                                        .withStatusCode(&status));
    if (stream != nullptr && !threadShouldExit()) {
        body = stream->readEntireStreamAsString();
    }
    else if (stream == nullptr) {
        status = 0;
    }

    ticket.clear();

    if (threadShouldExit()) return;

    auto result = parseRedeemResponse(status, body);
    auto callback = onDone;
    MessageManager::callAsync([callback, result]() {
        if (callback) callback(result);
    });
}

}
