// Lesson audio relay (doc/TINCAN-RELAY.md): each participant has a UDP port that stands in for them.
// A packet arriving on a participant's port goes to that participant, sent from the sender's own
// port, so each app sees only the relay. Packets stay sealed: the relay holds no lesson key.
#pragma once

#include "server.hpp"

#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace tincan {

struct RelayLimits {
    int firstPort = 40000;
    int lastPort = 40999;
    int64_t bytesPerSecond = 625000; // 5 Mbit/s per sender, above uncompressed stereo audio
    int64_t burstBytes = 125000;
    double rememberAddressSeconds = 3.0; // a router's port change is accepted after this much silence
};

struct RelayStats {
    uint64_t forwarded = 0;
    uint64_t droppedUnknownSender = 0;
    uint64_t droppedNoDestination = 0;
    uint64_t droppedRateLimit = 0;
};

class Relay final : public aoo::net::relay_hook {
public:
    Relay(std::string publicIp, RelayLimits limits);
    ~Relay() override;

    bool start();
    void stop();
    RelayStats stats() const;

    // aoo::net::relay_hook, called by the connection server
    bool add_participant(const std::string & group, const std::string & user,
                         const aoo::net::ip_address & connectionAddress,
                         const aoo::net::ip_address & claimedPublicAddress) override;
    void remove_participant(const std::string & group, const std::string & user) override;
    bool relay_address(const std::string & group, const std::string & user,
                       std::string & host, int & port) override;

private:
    struct Participant {
        std::string group;
        std::string user;
        int port = 0;
        int socket = -1;
        uint32_t connectionIp = 0;   // network order, seen by the server on the TCP connection
        uint32_t claimedIp = 0;
        uint16_t claimedPort = 0;
        bool learned = false;
        uint32_t learnedIp = 0;
        uint16_t learnedPort = 0;
        double lastHeard = 0;
        double tokens = 0;
        double tokensAt = 0;
    };
    using ParticipantPtr = std::shared_ptr<Participant>;

    void run();
    void handlePacket(const ParticipantPtr & destination, const char * data, int size,
                      uint32_t sourceIp, uint16_t sourcePort, double now);
    ParticipantPtr findSender(const ParticipantPtr & destination, uint32_t sourceIp, uint16_t sourcePort, double now);
    bool takeTokens(Participant & sender, int size, double now);
    int openPort(int & portOut);

    const std::string publicIp_;
    const RelayLimits limits_;

    mutable std::mutex lock_;
    std::map<std::string, std::vector<ParticipantPtr>> rooms_;
    std::vector<ParticipantPtr> participants_;
    RelayStats stats_;
    int nextPort_;

    std::atomic<bool> running_{false};
    std::thread thread_;
};

} // namespace tincan
