#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace rvn {

/// Classic 8-byte CAN 2.0 frame.
struct CanFrame {
    std::uint32_t id{};
    bool extended{false};
    std::uint8_t dlc{8};
    std::array<std::uint8_t, 8> data{};
};

enum class ByteOrder : std::uint8_t {
    BigEndian = 0,    ///< Motorola (@0 in DBC): startBit is the MSB
    LittleEndian = 1, ///< Intel (@1 in DBC): startBit is the LSB
};

class DbcParseError : public std::runtime_error {
public:
    DbcParseError(std::size_t line, const std::string& what)
        : std::runtime_error("line " + std::to_string(line) + ": " + what), line_(line) {}
    [[nodiscard]] std::size_t line() const noexcept { return line_; }

private:
    std::size_t line_;
};

struct DbcSignal {
    std::string name;
    std::uint8_t startBit{};
    std::uint8_t length{1};
    ByteOrder byteOrder{ByteOrder::LittleEndian};
    bool isSigned{false};
    double scale{1.0};
    double offset{0.0};
    double minimum{0.0};
    double maximum{0.0};
    std::string unit;

    [[nodiscard]] double decode(std::span<const std::uint8_t, 8> data) const noexcept;
    void encode(double physical, std::array<std::uint8_t, 8>& data) const noexcept;
};

struct DbcMessage {
    std::uint32_t id{};
    bool extended{false};
    std::uint8_t dlc{8};
    std::string name;
    std::string transmitter;
    std::vector<DbcSignal> signalDefs;

    [[nodiscard]] const DbcSignal* findSignal(std::string_view signalName) const noexcept;
};

/// Parsed Vector CANdb++ (.dbc) database supporting BO_ messages and SG_ signals
/// (Intel @1 and Motorola @0 bit layouts, signed/unsigned, scale/offset).
class DbcDatabase {
public:
    /// Parses a .dbc stream strictly on BO_ and SG_ definitions; standard metadata sections
    /// (VERSION, NS_, BS_, BU_, CM_, BA_, VAL_) are skipped. Throws DbcParseError on syntax
    /// or bit-layout errors.
    [[nodiscard]] static DbcDatabase parse(std::istream& in);

    [[nodiscard]] const DbcMessage* findMessage(std::uint32_t id, bool extended = false) const noexcept;
    [[nodiscard]] const DbcMessage* findMessageByName(std::string_view name) const noexcept;
    [[nodiscard]] const std::vector<DbcMessage>& messages() const noexcept { return messages_; }

private:
    std::vector<DbcMessage> messages_;
};

} // namespace rvn
