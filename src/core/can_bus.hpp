#pragma once

#include "dbc.hpp"

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <iosfwd>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace rvn {

struct CandumpEntry {
    double timestampS{}; ///< seconds relative to the first entry in the log
    std::string iface;
    CanFrame frame;
};

class CandumpParseError : public std::runtime_error {
public:
    CandumpParseError(std::size_t line, const std::string& what)
        : std::runtime_error("line " + std::to_string(line) + ": " + what), line_(line) {}
    [[nodiscard]] std::size_t line() const noexcept { return line_; }

private:
    std::size_t line_;
};

/// Parses a single SocketCAN candump line: `(<timestamp>) <iface> <hex_id>#<hex_payload>`.
[[nodiscard]] CandumpEntry parseCandumpLine(std::string_view line, std::size_t lineNo);

/// Reads a candump log stream, normalizing timestamps so the first entry sits at t = 0 s.
/// Throws CandumpParseError on malformed lines or non-monotonic timestamps.
[[nodiscard]] std::vector<CandumpEntry> readCandumpLog(std::istream& in);

/// Formats a frame in standard candump format: `(%.6f) <iface> <id>#<hex>`.
[[nodiscard]] std::string formatCandumpLine(double timestampS, std::string_view iface, const CanFrame& frame);

/// Abstract CAN bus transport for live interfaces (SocketCAN) or in-memory test buses.
class CanBus {
public:
    CanBus() = default;
    virtual ~CanBus() = default;
    CanBus(const CanBus&) = delete;
    CanBus& operator=(const CanBus&) = delete;
    CanBus(CanBus&&) = delete;
    CanBus& operator=(CanBus&&) = delete;

    /// Wait up to `timeoutMs` (0 = non-blocking) for a frame. Returns true if a frame was read.
    virtual bool receive(CanFrame& out, int timeoutMs) = 0;
    virtual bool send(const CanFrame& frame) = 0;
};

/// Thread-safe in-memory CAN loopback bus for tests and headless verification.
class MemoryCanBus final : public CanBus {
public:
    explicit MemoryCanBus(std::size_t maxFrames = 4096) : maxFrames_(maxFrames) {}

    bool receive(CanFrame& out, int timeoutMs) override;
    bool send(const CanFrame& frame) override;

private:
    std::size_t maxFrames_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<CanFrame> queue_;
};

/// Opens a Linux SocketCAN raw socket bound to `interfaceName` (e.g. `vcan0` or `can0`).
/// Throws std::runtime_error on failure or on platforms without SocketCAN.
[[nodiscard]] std::unique_ptr<CanBus> openSocketCan(std::string_view interfaceName);

} // namespace rvn
