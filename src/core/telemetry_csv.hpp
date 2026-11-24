#pragma once

#include "telemetry.hpp"

#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <stdexcept>
#include <string>
#include <vector>

namespace rvn {

/// A recorded drive. Tracked objects are not recorded; replay shows the ego vehicle only.
struct TelemetryLog {
    std::uint64_t roadSeed{7};
    std::vector<TelemetrySample> samples;
};

class CsvParseError : public std::runtime_error {
public:
    CsvParseError(std::size_t line, const std::string& what)
        : std::runtime_error("line " + std::to_string(line) + ": " + what), line_(line) {}
    [[nodiscard]] std::size_t line() const noexcept { return line_; }

private:
    std::size_t line_;
};

/// Writes a header comment (`# reality-view-native telemetry v1 road_seed=N`), a column header,
/// then one row per sample. Values round-trip exactly (std::to_chars shortest representation).
void writeTelemetryCsv(std::ostream& out, const TelemetryLog& log);

/// Strict parser: unknown gear letters, wrong column counts or non-numeric fields throw
/// CsvParseError with the 1-based line number. Samples must be in non-decreasing time order.
[[nodiscard]] TelemetryLog readTelemetryCsv(std::istream& in);

} // namespace rvn
