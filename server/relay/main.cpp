// CanDuet connection server with the lesson audio relay (doc/TINCAN-RELAY.md).
//   canduet-server --relay-ip <public IPv4> [--port 10998] [--relay-ports 40000-40999]
// Without a relay IP it refuses to start, so it can never share participants' addresses by mistake.
#include "relay.hpp"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>

static std::atomic<bool> stopRequested{false};
static void onSignal(int) { stopRequested = true; }

static int32_t ignoreEvents(void *, const aoo_event **, int32_t) { return 1; }

int main(int argc, char ** argv)
{
    int port = 10998;
    std::string relayIp;
    tincan::RelayLimits limits;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        const bool hasValue = i + 1 < argc;
        if (arg == "--port" && hasValue) {
            port = std::atoi(argv[++i]);
        } else if (arg == "--relay-ip" && hasValue) {
            relayIp = argv[++i];
        } else if (arg == "--relay-ports" && hasValue) {
            if (std::sscanf(argv[++i], "%d-%d", &limits.firstPort, &limits.lastPort) != 2
                || limits.firstPort > limits.lastPort) {
                std::fprintf(stderr, "bad --relay-ports\n");
                return 2;
            }
        } else {
            std::fprintf(stderr, "usage: canduet-server --relay-ip <public IPv4> [--port 10998] [--relay-ports 40000-40999]\n");
            return 2;
        }
    }
    if (relayIp.empty()) {
        std::fprintf(stderr, "--relay-ip is required\n");
        return 2;
    }

    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);

#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif

    tincan::Relay relay(relayIp, limits);
    aoo::net::set_relay_hook(&relay);

    int32_t err = 0;
    aoo::net::iserver::pointer server(aoo::net::iserver::create(port, &err));
    if (!server || err != 0) {
        std::fprintf(stderr, "couldn't start the connection server on port %d (error %d)\n", port, err);
        return 1;
    }

    relay.start();
    std::thread serverThread([&server] { server->run(); });

    std::printf("canduet-server: connection server on %d, relay ports %d-%d\n", port, limits.firstPort, limits.lastPort);
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
                || s.droppedNoDestination != previous.droppedNoDestination || s.droppedRateLimit != previous.droppedRateLimit) {
                std::printf("relay: forwarded %llu, dropped unknown %llu, no destination %llu, rate limit %llu\n",
                            (unsigned long long) s.forwarded, (unsigned long long) s.droppedUnknownSender,
                            (unsigned long long) s.droppedNoDestination, (unsigned long long) s.droppedRateLimit);
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
