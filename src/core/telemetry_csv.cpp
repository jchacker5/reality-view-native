#include "telemetry_csv.hpp"

#include <array>
#include <charconv>
#include <istream>
#include <ostream>
#include <string_view>

namespace rvn {
namespace {

constexpr std::string_view kMagic = "# reality-view-native telemetry v1";
constexpr std::string_view kColumns =
    "t,x,y,heading,speed,accel,yaw_rate,steering_deg,throttle,brake,set_speed,lane_offset,road_s,lane,"
    "gear,turn_signal,autopilot,lat,lon";
constexpr std::size_t kColumnCount = 19;

template <typename T> void put(std::string& line, T value) {
    std::array<char, 64> buf{};
    const auto res = std::to_chars(buf.data(), buf.data() + buf.size(), value);
    line.append(buf.data(), res.ptr);
}

template <typename T> T parseNumber(std::string_view field, std::size_t lineNo) {
    T value{};
    const char* end = field.data() + field.size();
    const auto res = std::from_chars(field.data(), end, value);
    if (res.ec != std::errc{} || res.ptr != end) {
        throw CsvParseError(lineNo, "not a number: '" + std::string(field) + "'");
    }
    return value;
}

Gear parseGear(std::string_view f, std::size_t lineNo) {
    if (f == "P")
        return Gear::Park;
    if (f == "R")
        return Gear::Reverse;
    if (f == "N")
        return Gear::Neutral;
    if (f == "D")
        return Gear::Drive;
    throw CsvParseError(lineNo, "unknown gear '" + std::string(f) + "'");
}

template <typename Enum> Enum parseEnum(std::string_view f, std::size_t lineNo, int maxValue) {
    const int v = parseNumber<int>(f, lineNo);
    if (v < 0 || v > maxValue) {
        throw CsvParseError(lineNo, "enum value out of range: " + std::string(f));
    }
    return static_cast<Enum>(v);
}

} // namespace

void writeTelemetryCsv(std::ostream& out, const TelemetryLog& log) {
    out << kMagic << " road_seed=" << log.roadSeed << '\n' << kColumns << '\n';
    std::string line;
    line.reserve(256);
    for (const TelemetrySample& s : log.samples) {
        line.clear();
        const auto field = [&line](auto v) {
            put(line, v);
            line.push_back(',');
        };
        field(s.simTimeS);
        field(s.x);
        field(s.y);
        field(s.heading);
        field(s.speedMps);
        field(s.accelMps2);
        field(s.yawRateRadS);
        field(s.steeringWheelDeg);
        field(s.throttle);
        field(s.brake);
        field(s.setSpeedMps);
        field(s.laneOffsetM);
        field(s.roadS);
        field(static_cast<int>(s.lane));
        line.append(label(s.gear));
        line.push_back(',');
        field(static_cast<int>(s.turnSignal));
        field(static_cast<int>(s.autopilot));
        field(s.latitude);
        put(line, s.longitude);
        out << line << '\n';
    }
}

TelemetryLog readTelemetryCsv(std::istream& in) {
    TelemetryLog log;
    std::string line;
    std::size_t lineNo = 0;

    if (!std::getline(in, line) || !std::string_view(line).starts_with(kMagic)) {
        throw CsvParseError(1, "missing '# reality-view-native telemetry v1' header");
    }
    ++lineNo;
    if (const auto pos = line.find("road_seed="); pos != std::string::npos) {
        log.roadSeed = parseNumber<std::uint64_t>(std::string_view(line).substr(pos + 10), lineNo);
    }
    if (!std::getline(in, line) || line != kColumns) {
        throw CsvParseError(2, "unexpected column header");
    }
    ++lineNo;

    std::array<std::string_view, kColumnCount> f{};
    while (std::getline(in, line)) {
        ++lineNo;
        if (line.empty()) {
            continue;
        }
        // Split without allocating: string_views into `line`.
        std::size_t count = 0;
        std::string_view rest(line);
        while (true) {
            const auto comma = rest.find(',');
            if (count == kColumnCount) {
                throw CsvParseError(lineNo, "too many columns");
            }
            f[count++] = rest.substr(0, comma);
            if (comma == std::string_view::npos) {
                break;
            }
            rest.remove_prefix(comma + 1);
        }
        if (count != kColumnCount) {
            throw CsvParseError(lineNo, "expected 19 columns, got " + std::to_string(count));
        }

        TelemetrySample s;
        s.seq = log.samples.size();
        s.simTimeS = parseNumber<double>(f[0], lineNo);
        s.x = parseNumber<double>(f[1], lineNo);
        s.y = parseNumber<double>(f[2], lineNo);
        s.heading = parseNumber<float>(f[3], lineNo);
        s.speedMps = parseNumber<float>(f[4], lineNo);
        s.accelMps2 = parseNumber<float>(f[5], lineNo);
        s.yawRateRadS = parseNumber<float>(f[6], lineNo);
        s.steeringWheelDeg = parseNumber<float>(f[7], lineNo);
        s.throttle = parseNumber<float>(f[8], lineNo);
        s.brake = parseNumber<float>(f[9], lineNo);
        s.setSpeedMps = parseNumber<float>(f[10], lineNo);
        s.laneOffsetM = parseNumber<float>(f[11], lineNo);
        s.roadS = parseNumber<float>(f[12], lineNo);
        s.lane = static_cast<std::int8_t>(parseNumber<int>(f[13], lineNo));
        s.gear = parseGear(f[14], lineNo);
        s.turnSignal = parseEnum<TurnSignal>(f[15], lineNo, 2);
        s.autopilot = parseEnum<AutopilotState>(f[16], lineNo, 2);
        s.latitude = parseNumber<double>(f[17], lineNo);
        s.longitude = parseNumber<double>(f[18], lineNo);
        if (!log.samples.empty() && s.simTimeS < log.samples.back().simTimeS) {
            throw CsvParseError(lineNo, "timestamps go backwards");
        }
        log.samples.push_back(s);
    }
    return log;
}

} // namespace rvn
