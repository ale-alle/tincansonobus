#include "relay.hpp"
#include "group_auth.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>

#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
  using PollFd = WSAPOLLFD;
  static int pollSockets(PollFd * fds, size_t n, int ms) { return WSAPoll(fds, (ULONG) n, ms); }
  static void closeSocket(int s) { closesocket(s); }
  static void setNonBlocking(int s) { u_long on = 1; ioctlsocket(s, FIONBIO, &on); }
#else
  #include <arpa/inet.h>
  #include <fcntl.h>
  #include <netinet/in.h>
  #include <poll.h>
  #include <sys/socket.h>
  #include <unistd.h>
  using PollFd = struct pollfd;
  static int pollSockets(PollFd * fds, size_t n, int ms) { return poll(fds, (nfds_t) n, ms); }
  static void closeSocket(int s) { close(s); }
  static void setNonBlocking(int s) { fcntl(s, F_SETFL, fcntl(s, F_GETFL, 0) | O_NONBLOCK); }
#endif

namespace tincan {

namespace {

double seconds()
{
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

void readAddress(const aoo::net::ip_address & in, uint32_t & ip, uint16_t & port)
{
    auto * sa = reinterpret_cast<const sockaddr_in *>(&in.address);
    ip = sa->sin_addr.s_addr;
    port = sa->sin_port;
}

} // namespace

Relay::Relay(std::string publicIp, RelayLimits limits, std::string groupSecret)
    : publicIp_(std::move(publicIp)), limits_(limits), groupSecret_(std::move(groupSecret)),
      nextPort_(limits.firstPort)
{
}

bool Relay::allow_group(const std::string & group)
{
    if (groupSecret_.empty()) return true;
    const auto now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    if (checkSignedGroup(group, groupSecret_, (int64_t) now) == GroupCheck::ok) return true;
    // counted, not logged per attempt: a script can't fill the disk; never the group name
    std::lock_guard<std::mutex> guard(lock_);
    ++stats_.refusedGroups;
    return false;
}

Relay::~Relay()
{
    stop();
    for (auto & p : participants_) {
        closeSocket(p->socket);
    }
}

bool Relay::start()
{
    running_ = true;
    thread_ = std::thread([this] { run(); });
    return true;
}

void Relay::stop()
{
    running_ = false;
    if (thread_.joinable()) {
        thread_.join();
    }
}

RelayStats Relay::stats() const
{
    std::lock_guard<std::mutex> sl(lock_);
    return stats_;
}

int Relay::openPort(int & portOut)
{
    const int count = limits_.lastPort - limits_.firstPort + 1;
    for (int i = 0; i < count; ++i) {
        int port = nextPort_;
        nextPort_ = nextPort_ >= limits_.lastPort ? limits_.firstPort : nextPort_ + 1;

        int s = (int) socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (s < 0) return -1;

        sockaddr_in sa{};
        sa.sin_family = AF_INET;
        sa.sin_addr.s_addr = htonl(INADDR_ANY);
        sa.sin_port = htons((uint16_t) port);
        if (bind(s, reinterpret_cast<sockaddr *>(&sa), sizeof(sa)) == 0) {
            setNonBlocking(s);
            portOut = port;
            return s;
        }
        closeSocket(s);
    }
    return -1;
}

bool Relay::add_participant(const std::string & group, const std::string & user,
                            const aoo::net::ip_address & connectionAddress,
                            const aoo::net::ip_address & claimedPublicAddress)
{
    std::lock_guard<std::mutex> sl(lock_);

    auto & room = rooms_[group];
    for (auto & p : room) {
        if (p->user == user) return true;
    }

    auto p = std::make_shared<Participant>();
    p->group = group;
    p->user = user;
    p->socket = openPort(p->port);
    if (p->socket < 0) {
        std::fprintf(stderr, "relay: no free port\n");
        if (room.empty()) rooms_.erase(group);
        return false;
    }

    uint16_t ignoredPort = 0;
    readAddress(connectionAddress, p->connectionIp, ignoredPort);
    readAddress(claimedPublicAddress, p->claimedIp, p->claimedPort);
    p->tokens = (double) limits_.burstBytes;
    p->tokensAt = seconds();

    room.push_back(p);
    participants_.push_back(p);
    return true;
}

void Relay::remove_participant(const std::string & group, const std::string & user)
{
    std::lock_guard<std::mutex> sl(lock_);

    auto found = rooms_.find(group);
    if (found == rooms_.end()) return;

    auto & room = found->second;
    auto it = std::find_if(room.begin(), room.end(), [&](const ParticipantPtr & p) { return p->user == user; });
    if (it != room.end()) {
        auto p = *it;
        room.erase(it);
        participants_.erase(std::remove(participants_.begin(), participants_.end(), p), participants_.end());
        closeSocket(p->socket);
        p->socket = -1;
    }
    if (room.empty()) rooms_.erase(found);
}

bool Relay::relay_address(const std::string & group, const std::string & user, std::string & host, int & port)
{
    std::lock_guard<std::mutex> sl(lock_);

    auto found = rooms_.find(group);
    if (found == rooms_.end()) return false;

    for (auto & p : found->second) {
        if (p->user == user) {
            host = publicIp_;
            port = p->port;
            return true;
        }
    }
    return false;
}

Relay::ParticipantPtr Relay::findSender(const ParticipantPtr & destination, uint32_t sourceIp, uint16_t sourcePort, double now)
{
    auto & room = rooms_[destination->group];

    for (auto & p : room) {
        if (p != destination && p->learned && p->learnedIp == sourceIp && p->learnedPort == sourcePort) {
            return p;
        }
    }

    // A new address must come from the IP the server saw that person connect from. Prefer the exact
    // address they reported; otherwise allow a different port only if there's one possible person.
    ParticipantPtr exact, onlyCandidate;
    int candidates = 0;
    for (auto & p : room) {
        if (p == destination || p->connectionIp != sourceIp) continue;
        if (p->claimedIp == sourceIp && p->claimedPort == sourcePort) exact = p;
        onlyCandidate = p;
        ++candidates;
    }

    ParticipantPtr sender = exact ? exact : (candidates == 1 ? onlyCandidate : nullptr);
    if (!sender) return nullptr;

    // someone already heard recently keeps their address, so a second sender can't take it over
    if (sender->learned && now - sender->lastHeard < limits_.rememberAddressSeconds) return nullptr;

    sender->learned = true;
    sender->learnedIp = sourceIp;
    sender->learnedPort = sourcePort;
    return sender;
}

bool Relay::takeTokens(Participant & sender, int size, double now)
{
    sender.tokens = (std::min)((double) limits_.burstBytes,
                               sender.tokens + (now - sender.tokensAt) * (double) limits_.bytesPerSecond);
    sender.tokensAt = now;
    if (sender.tokens < size) return false;
    sender.tokens -= size;
    return true;
}

void Relay::handlePacket(const ParticipantPtr & destination, const char * data, int size,
                         uint32_t sourceIp, uint16_t sourcePort, double now)
{
    auto sender = findSender(destination, sourceIp, sourcePort, now);
    if (!sender) {
        ++stats_.droppedUnknownSender;
        return;
    }
    sender->lastHeard = now;

    // nothing can reach someone until their own first packet shows where they are
    if (!destination->learned) {
        ++stats_.droppedNoDestination;
        return;
    }
    if (!takeTokens(*sender, size, now)) {
        ++stats_.droppedRateLimit;
        return;
    }

    sockaddr_in to{};
    to.sin_family = AF_INET;
    to.sin_addr.s_addr = destination->learnedIp;
    to.sin_port = destination->learnedPort;
    sendto(sender->socket, data, size, 0, reinterpret_cast<sockaddr *>(&to), sizeof(to));
    ++stats_.forwarded;
}

void Relay::run()
{
    std::vector<PollFd> fds;
    std::vector<ParticipantPtr> owners;
    char buffer[65536];

    while (running_) {
        {
            std::lock_guard<std::mutex> sl(lock_);
            owners = participants_;
        }
        fds.resize(owners.size());
        for (size_t i = 0; i < owners.size(); ++i) {
            fds[i].fd = owners[i]->socket;
            fds[i].events = POLLIN;
            fds[i].revents = 0;
        }

        if (fds.empty()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            continue;
        }
        // short timeout: a newly added port is picked up within 20 ms
        if (pollSockets(fds.data(), fds.size(), 20) <= 0) continue;

        std::lock_guard<std::mutex> sl(lock_);
        const double now = seconds();
        for (size_t i = 0; i < owners.size(); ++i) {
            if (!(fds[i].revents & POLLIN) || owners[i]->socket < 0) continue;

            for (int reads = 0; reads < 64; ++reads) {
                sockaddr_in from{};
                socklen_t fromLen = sizeof(from);
                int n = (int) recvfrom(owners[i]->socket, buffer, sizeof(buffer), 0,
                                       reinterpret_cast<sockaddr *>(&from), &fromLen);
                if (n <= 0) break;
                handlePacket(owners[i], buffer, n, from.sin_addr.s_addr, from.sin_port, now);
            }
        }
    }
}

} // namespace tincan
