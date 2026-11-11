// CanDuet connection server with the lesson audio relay (doc/TINCAN-RELAY.md).
//   canduet-server --relay-ip <public IPv4> [--port 10998] [--relay-ports 40000-40999]
//                  [--group-secret-file <path>] [--allow-unsigned-groups] [--max-per-group 6]
//   canduet-server --self-test
// Without a relay IP it refuses to start, so it can never share participants' addresses by mistake.
// Without a group secret (file, or CANDUET_GROUP_SECRET) it refuses too, unless
// --allow-unsigned-groups is given (local tests): only rooms-worker-signed lesson groups (T-048).
#include "relay.hpp"
#include "group_auth.hpp"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>

static std::atomic<bool> stopRequested{false};
static void onSignal(int) { stopRequested = true; }

static int32_t ignoreEvents(void *, const aoo_event **, int32_t) { return 1; }

static std::string trim(const std::string & s)
{
    const char * space = " \t\r\n";
    const auto first = s.find_first_not_of(space);
    if (first == std::string::npos) return {};
    return s.substr(first, s.find_last_not_of(space) - first + 1);
}

static std::string hex(const uint8_t * data, size_t size)
{
    static const char * digits = "0123456789abcdef";
    std::string out;
    for (size_t i = 0; i < size; ++i) {
        out += digits[data[i] >> 4];
        out += digits[data[i] & 15];
    }
    return out;
}

// RFC 4231 test cases 1, 2 and 6, plus the signed-group format, so a build on a new machine
// proves its HMAC before it guards real lessons
static int selfTest()
{
    struct Case { std::string key, data, mac; };
    const Case cases[] = {
        {std::string(20, '\x0b'), "Hi There", "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7"},
        {"Jefe", "what do ya want for nothing?", "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843"},
        {std::string(131, '\xaa'), "Test Using Larger Than Block-Size Key - Hash Key First",
         "60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54"},
    };
    int failed = 0;
    for (const auto & c : cases) {
        uint8_t mac[32];
        tincan::hmacSha256(c.key, c.data, mac);
        if (hex(mac, 32) != c.mac) {
            std::fprintf(stderr, "HMAC-SHA256 test vector failed\n");
            ++failed;
        }
    }

    using tincan::GroupCheck;
    // made with rooms-worker's algorithm: secret "test-secret", room "lesson-abc", expires 2000000000
    const std::string sig = "VZtU2xjYcnFtVclHHx5x8w";
    const std::string good = "lesson-abc.2000000000." + sig;
    const struct { std::string name; int64_t now; GroupCheck want; } groups[] = {
        {good, 1999999000, GroupCheck::ok},
        {good, 2000000001, GroupCheck::expired},
        {good, 1000000000, GroupCheck::tooFarAhead},
        {"lesson-abd.2000000000." + sig, 1999999000, GroupCheck::badSignature},
        {"lesson-abc.2000000001." + sig, 1999999000, GroupCheck::badSignature},
        {"lesson-abc", 1999999000, GroupCheck::malformed},
        {"a b.2000000000." + sig, 1999999000, GroupCheck::malformed},
        {"lesson-abc.2000000000." + sig + "x", 1999999000, GroupCheck::malformed},
    };
    for (const auto & g : groups) {
        if (tincan::checkSignedGroup(g.name, "test-secret", g.now) != g.want) {
            std::fprintf(stderr, "group check failed for a case expecting \"%s\"\n", tincan::groupCheckText(g.want));
            ++failed;
        }
    }
    std::printf("%s\n", failed ? "self-test FAILED" : "self-test passed");
    return failed ? 1 : 0;
}

int main(int argc, char ** argv)
{
    int port = 10998;
    std::string relayIp;
    std::string secretFile;
    bool allowUnsigned = false;
    tincan::RelayLimits limits;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        const bool hasValue = i + 1 < argc;
        if (arg == "--self-test") {
            return selfTest();
        } else if (arg == "--group-secret-file" && hasValue) {
            secretFile = argv[++i];
        } else if (arg == "--allow-unsigned-groups") {
            allowUnsigned = true;
        } else if (arg == "--max-per-group" && hasValue) {
            limits.maxPerGroup = std::atoi(argv[++i]);
            if (limits.maxPerGroup < 2 || limits.maxPerGroup > 32) {
                std::fprintf(stderr, "bad --max-per-group (2-32)\n");
                return 2;
            }
        } else if (arg == "--port" && hasValue) {
            port = std::atoi(argv[++i]);
            if (port < 1 || port > 65535) {
                std::fprintf(stderr, "bad --port\n");
                return 2;
            }
        } else if (arg == "--relay-ip" && hasValue) {
            relayIp = argv[++i];
        } else if (arg == "--relay-ports" && hasValue) {
            if (std::sscanf(argv[++i], "%d-%d", &limits.firstPort, &limits.lastPort) != 2
                || limits.firstPort < 1024 || limits.lastPort > 65535 || limits.firstPort > limits.lastPort
                || limits.lastPort - limits.firstPort > 20000) {
                std::fprintf(stderr, "bad --relay-ports (1024-65535, at most 20000 ports)\n");
                return 2;
            }
        } else {
            std::fprintf(stderr,
                         "usage: canduet-server --relay-ip <public IPv4> [--port 10998] [--relay-ports 40000-40999]\n"
                         "                      [--group-secret-file <path>] [--allow-unsigned-groups] [--max-per-group 6]\n"
                         "       canduet-server --self-test\n");
            return 2;
        }
    }
    if (relayIp.empty()) {
        std::fprintf(stderr, "--relay-ip is required\n");
        return 2;
    }

    std::string groupSecret;
    if (!secretFile.empty()) {
        std::ifstream in(secretFile, std::ios::binary);
        if (!in) {
            std::fprintf(stderr, "can't read --group-secret-file\n");
            return 2;
        }
        std::stringstream text;
        text << in.rdbuf();
        groupSecret = trim(text.str());
    } else if (const char * env = std::getenv("CANDUET_GROUP_SECRET")) {
        groupSecret = trim(env);
    }
    if (groupSecret.empty() && !allowUnsigned) {
        std::fprintf(stderr, "a group secret is required (--group-secret-file or CANDUET_GROUP_SECRET); "
                             "--allow-unsigned-groups is for local tests only\n");
        return 2;
    }
    if (!groupSecret.empty() && groupSecret.size() < 32) {
        std::fprintf(stderr, "the group secret must be at least 32 characters\n");
        return 2;
    }

    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);

#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif

    tincan::Relay relay(relayIp, limits, groupSecret);
    aoo::net::set_relay_hook(&relay);

    int32_t err = 0;
    aoo::net::iserver::pointer server(aoo::net::iserver::create(port, &err));
    if (!server || err != 0) {
        std::fprintf(stderr, "couldn't start the connection server on port %d (error %d)\n", port, err);
        return 1;
    }

    relay.start();
    std::thread serverThread([&server] { server->run(); });

    std::printf("canduet-server: connection server on %d, relay ports %d-%d, %s, at most %d per lesson\n", port,
                limits.firstPort, limits.lastPort,
                groupSecret.empty() ? "ANY GROUP NAME (test mode)" : "signed lesson groups only", limits.maxPerGroup);
    std::fflush(stdout);

    auto lastStats = std::chrono::steady_clock::now();
    tincan::RelayStats previous;
    while (!stopRequested) {
        server->handle_events(ignoreEvents, nullptr);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        auto now = std::chrono::steady_clock::now();
        if (now - lastStats >= std::chrono::seconds(10)) {
            lastStats = now;
            auto s = relay.stats();
            if (s.forwarded != previous.forwarded || s.droppedUnknownSender != previous.droppedUnknownSender
                || s.droppedNoDestination != previous.droppedNoDestination || s.droppedRateLimit != previous.droppedRateLimit
                || s.refusedGroups != previous.refusedGroups) {
                std::printf("relay: forwarded %llu, dropped unknown %llu, no destination %llu, rate limit %llu, refused groups %llu\n",
                            (unsigned long long) s.forwarded, (unsigned long long) s.droppedUnknownSender,
                            (unsigned long long) s.droppedNoDestination, (unsigned long long) s.droppedRateLimit,
                            (unsigned long long) s.refusedGroups);
                std::fflush(stdout);
                previous = s;
            }
        }
    }

    server->quit();
    serverThread.join();
    aoo::net::set_relay_hook(nullptr);
    relay.stop();
    return 0;
}
