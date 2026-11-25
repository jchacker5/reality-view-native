#include "can_bus.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <istream>

#ifdef __linux__
#include <cerrno>
#include <fcntl.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <net/if.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace rvn {
namespace {

std::string_view trim(std::string_view sv) noexcept {
    while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.front())) != 0) {
        sv.remove_prefix(1);
    }
    while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.back())) != 0) {
        sv.remove_suffix(1);
    }
    return sv;
}

int hexNibble(char c) noexcept {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return 10 + (c - 'a');
    }
    if (c >= 'A' && c <= 'F') {
        return 10 + (c - 'A');
    }
    return -1;
}

bool parseHexUint32(std::string_view sv, std::uint32_t& out) noexcept {
    if (sv.empty() || sv.size() > 8) {
        return false;
    }
    std::uint32_t val = 0;
    for (char c : sv) {
        const int n = hexNibble(c);
        if (n < 0) {
            return false;
        }
        val = (val << 4U) | static_cast<std::uint32_t>(n);
    }
    out = val;
    return true;
}

#ifdef __linux__
class SocketCanBus final : public CanBus {
public:
    explicit SocketCanBus(int fd) : fd_(fd) {}
    ~SocketCanBus() override {
        if (fd_ >= 0) {
            ::close(fd_);
        }
    }

    bool receive(CanFrame& out, int timeoutMs) override {
        if (timeoutMs > 0) {
            pollfd pfd{};
            pfd.fd = fd_;
            pfd.events = POLLIN;
            const int ready = ::poll(&pfd, 1, timeoutMs);
            if (ready <= 0 || (pfd.revents & POLLIN) == 0) {
                return false;
            }
        }
        struct can_frame raw{};
        const ssize_t n = ::read(fd_, &raw, sizeof(raw));
        if (n != static_cast<ssize_t>(sizeof(raw))) {
            return false;
        }
        out.extended = (raw.can_id & CAN_EFF_FLAG) != 0U;
        out.id = raw.can_id & (out.extended ? CAN_EFF_MASK : CAN_SFF_MASK);
        out.dlc = std::min<std::uint8_t>(raw.can_dlc, 8);
        out.data.fill(0);
        std::memcpy(out.data.data(), raw.data, out.dlc);
        return true;
    }

    bool send(const CanFrame& frame) override {
        struct can_frame raw{};
        raw.can_id = frame.extended ? ((frame.id & CAN_EFF_MASK) | CAN_EFF_FLAG) : (frame.id & CAN_SFF_MASK);
        raw.can_dlc = std::min<std::uint8_t>(frame.dlc, 8);
        std::memcpy(raw.data, frame.data.data(), raw.can_dlc);
        return ::write(fd_, &raw, sizeof(raw)) == static_cast<ssize_t>(sizeof(raw));
    }

private:
    int fd_{-1};
};
#endif

} // namespace

CandumpEntry parseCandumpLine(std::string_view line, std::size_t lineNo) {
    line = trim(line);
    if (line.empty() || line.front() != '(') {
        throw CandumpParseError(lineNo, "expected '(timestamp) iface id#payload'");
    }
    const std::size_t closeParen = line.find(')');
    if (closeParen == std::string_view::npos) {
        throw CandumpParseError(lineNo, "unterminated timestamp '(...)'");
    }
    const std::string tsStr(trim(line.substr(1, closeParen - 1)));
    char* tsEnd = nullptr;
    const double ts = std::strtod(tsStr.c_str(), &tsEnd);
    if (tsStr.empty() || tsEnd != tsStr.c_str() + tsStr.size() || !std::isfinite(ts)) {
        throw CandumpParseError(lineNo, "invalid timestamp");
    }

    std::string_view rest = trim(line.substr(closeParen + 1));
    const std::size_t sp = rest.find_first_of(" \t");
    if (sp == std::string_view::npos) {
        throw CandumpParseError(lineNo, "expected interface name and frame");
    }
    const std::string_view iface = rest.substr(0, sp);
    const std::string_view frameTok = trim(rest.substr(sp + 1));
    const std::size_t hash = frameTok.find('#');
    if (hash == std::string_view::npos || hash == 0) {
        throw CandumpParseError(lineNo, "expected '<hex_id>#<hex_payload>'");
    }

    const std::string_view idHex = frameTok.substr(0, hash);
    const std::string_view dataHex = frameTok.substr(hash + 1);
    std::uint32_t canId = 0;
    if (!parseHexUint32(idHex, canId)) {
        throw CandumpParseError(lineNo, "invalid hexadecimal CAN ID");
    }
    const bool extended = idHex.size() > 3 || canId > 0x7FFU;
    if (extended && canId > 0x1FFFFFFFU) {
        throw CandumpParseError(lineNo, "CAN ID exceeds 29 bits");
    }

    if (dataHex.size() % 2 != 0 || dataHex.size() > 16) {
        throw CandumpParseError(lineNo, "CAN payload hex length must be even and <= 16 chars");
    }

    CandumpEntry entry;
    entry.timestampS = ts;
    entry.iface = std::string(iface);
    entry.frame.id = canId;
    entry.frame.extended = extended;
    entry.frame.dlc = static_cast<std::uint8_t>(dataHex.size() / 2);
    for (std::size_t i = 0; i < entry.frame.dlc; ++i) {
        const int hi = hexNibble(dataHex[2 * i]);
        const int lo = hexNibble(dataHex[2 * i + 1]);
        if (hi < 0 || lo < 0) {
            throw CandumpParseError(lineNo, "invalid hexadecimal payload byte");
        }
        entry.frame.data[i] = static_cast<std::uint8_t>((hi << 4) | lo);
    }
    return entry;
}

std::vector<CandumpEntry> readCandumpLog(std::istream& in) {
    std::vector<CandumpEntry> entries;
    std::string line;
    std::size_t lineNo = 0;
    double firstTs = 0.0;
    bool hasFirst = false;
    double prevTs = 0.0;
    while (std::getline(in, line)) {
        ++lineNo;
        const std::string_view trimmed = trim(line);
        if (trimmed.empty() || trimmed.front() == '#') {
            continue;
        }
        CandumpEntry entry = parseCandumpLine(trimmed, lineNo);
        if (!hasFirst) {
            firstTs = entry.timestampS;
            hasFirst = true;
        }
        entry.timestampS -= firstTs;
        if (entry.timestampS < prevTs) {
            throw CandumpParseError(lineNo, "candump timestamps must be non-decreasing");
        }
        prevTs = entry.timestampS;
        entries.push_back(std::move(entry));
    }
    return entries;
}

std::string formatCandumpLine(double timestampS, std::string_view iface, const CanFrame& frame) {
    char buf[96];
    const int n = frame.extended ? std::snprintf(buf, sizeof(buf), "(%.6f) %.*s %08X#", timestampS,
                                                 static_cast<int>(iface.size()), iface.data(), frame.id & 0x1FFFFFFFU)
                                 : std::snprintf(buf, sizeof(buf), "(%.6f) %.*s %03X#", timestampS,
                                                 static_cast<int>(iface.size()), iface.data(), frame.id & 0x7FFU);
    std::string out(buf, static_cast<std::size_t>(std::max(0, n)));
    const std::uint8_t dlc = std::min<std::uint8_t>(frame.dlc, 8);
    static constexpr char kHex[] = "0123456789ABCDEF";
    for (std::uint8_t i = 0; i < dlc; ++i) {
        out.push_back(kHex[(frame.data[i] >> 4U) & 0xFU]);
        out.push_back(kHex[frame.data[i] & 0xFU]);
    }
    return out;
}

bool MemoryCanBus::receive(CanFrame& out, int timeoutMs) {
    std::unique_lock lock(mutex_);
    if (queue_.empty() && timeoutMs > 0) {
        cv_.wait_for(lock, std::chrono::milliseconds(timeoutMs), [this] { return !queue_.empty(); });
    }
    if (queue_.empty()) {
        return false;
    }
    out = queue_.front();
    queue_.pop_front();
    return true;
}

bool MemoryCanBus::send(const CanFrame& frame) {
    {
        const std::lock_guard lock(mutex_);
        if (queue_.size() >= maxFrames_) {
            return false;
        }
        queue_.push_back(frame);
    }
    cv_.notify_one();
    return true;
}

std::unique_ptr<CanBus> openSocketCan(std::string_view interfaceName) {
#ifdef __linux__
    if (interfaceName.empty() || interfaceName.size() >= IFNAMSIZ) {
        throw std::runtime_error("invalid SocketCAN interface name: " + std::string(interfaceName));
    }
    const int fd = ::socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (fd < 0) {
        throw std::runtime_error("socket(PF_CAN) failed: " + std::string(std::strerror(errno)));
    }

    ifreq ifr{};
    std::memcpy(ifr.ifr_name, interfaceName.data(), interfaceName.size());
    if (::ioctl(fd, SIOCGIFINDEX, &ifr) < 0) {
        const int err = errno;
        ::close(fd);
        throw std::runtime_error("cannot find CAN interface '" + std::string(interfaceName) +
                                 "': " + std::strerror(err));
    }

    const int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags < 0 || ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        const int err = errno;
        ::close(fd);
        throw std::runtime_error("fcntl(O_NONBLOCK) failed: " + std::string(std::strerror(err)));
    }

    sockaddr_can addr{};
    addr.can_family = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;
    if (::bind(fd, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) < 0) {
        const int err = errno;
        ::close(fd);
        throw std::runtime_error("bind(" + std::string(interfaceName) + ") failed: " + std::strerror(err));
    }
    return std::make_unique<SocketCanBus>(fd);
#else
    throw std::runtime_error("SocketCAN interface '" + std::string(interfaceName) +
                             "' is only supported on Linux; use --can-log <file.candump> on this platform");
#endif
}

} // namespace rvn
