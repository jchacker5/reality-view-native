#include "dbc.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <istream>
#include <limits>
#include <string_view>

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

std::string_view nextToken(std::string_view& sv) noexcept {
    sv = trim(sv);
    const std::size_t pos = sv.find_first_of(" \t\r\n");
    if (pos == std::string_view::npos) {
        const std::string_view tok = sv;
        sv = {};
        return tok;
    }
    const std::string_view tok = sv.substr(0, pos);
    sv.remove_prefix(pos);
    return tok;
}

template <typename T> bool parseInt(std::string_view sv, T& out) noexcept {
    if (sv.empty()) {
        return false;
    }
    const char* begin = sv.data();
    const char* end = sv.data() + sv.size();
    const auto [ptr, ec] = std::from_chars(begin, end, out);
    return ec == std::errc{} && ptr == end;
}

bool parseDouble(std::string_view sv, double& out) noexcept {
    if (sv.empty()) {
        return false;
    }
    std::string buf(sv);
    char* end = nullptr;
    const double v = std::strtod(buf.c_str(), &end);
    if (end != buf.c_str() + buf.size() || !std::isfinite(v)) {
        return false;
    }
    out = v;
    return true;
}

std::size_t nextMotorolaBit(std::size_t bitPos) noexcept {
    return (bitPos % 8U == 0U) ? (bitPos + 15U) : (bitPos - 1U);
}

bool signalFitsDlc(std::uint8_t startBit, std::uint8_t length, ByteOrder order, std::uint8_t dlc) noexcept {
    const std::size_t totalBits = static_cast<std::size_t>(dlc) * 8U;
    if (length == 0 || length > 64 || startBit >= totalBits) {
        return false;
    }
    if (order == ByteOrder::LittleEndian) {
        return static_cast<std::size_t>(startBit) + static_cast<std::size_t>(length) <= totalBits;
    }
    std::size_t bitPos = startBit;
    for (std::size_t i = 0; i < length; ++i) {
        if (bitPos >= totalBits) {
            return false;
        }
        bitPos = nextMotorolaBit(bitPos);
    }
    return true;
}

DbcMessage parseMessageLine(std::string_view rest, std::size_t lineNo) {
    const std::string_view idTok = nextToken(rest);
    std::uint32_t rawId = 0;
    if (!parseInt(idTok, rawId)) {
        throw DbcParseError(lineNo, "invalid CAN ID in BO_");
    }

    const bool extended = (rawId & 0x80000000U) != 0U;
    const std::uint32_t canId = rawId & (extended ? 0x1FFFFFFFU : 0x7FFU);
    if (!extended && rawId > 0x7FFU) {
        throw DbcParseError(lineNo, "standard CAN ID exceeds 11 bits");
    }
    if (extended && (rawId & 0x60000000U) != 0U) {
        throw DbcParseError(lineNo, "extended CAN ID exceeds 29 bits");
    }

    std::string_view nameTok = nextToken(rest);
    if (nameTok.empty() || nameTok.back() != ':') {
        throw DbcParseError(lineNo, "expected '<name>:' in BO_");
    }
    nameTok.remove_suffix(1);
    if (nameTok.empty()) {
        throw DbcParseError(lineNo, "empty message name in BO_");
    }

    const std::string_view dlcTok = nextToken(rest);
    unsigned int dlcVal = 0;
    if (!parseInt(dlcTok, dlcVal) || dlcVal > 8U) {
        throw DbcParseError(lineNo, "invalid DLC in BO_ (expected 0..8)");
    }

    const std::string_view txTok = nextToken(rest);
    if (txTok.empty()) {
        throw DbcParseError(lineNo, "missing transmitter in BO_");
    }

    DbcMessage msg;
    msg.id = canId;
    msg.extended = extended;
    msg.dlc = static_cast<std::uint8_t>(dlcVal);
    msg.name = std::string(nameTok);
    msg.transmitter = std::string(txTok);
    return msg;
}

DbcSignal parseSignalLine(std::string_view rest, std::uint8_t dlc, std::size_t lineNo) {
    const std::string_view nameTok = nextToken(rest);
    if (nameTok.empty()) {
        throw DbcParseError(lineNo, "missing signal name in SG_");
    }

    const std::string_view colon = nextToken(rest);
    if (colon != ":") {
        throw DbcParseError(lineNo, "expected ':' after signal name in SG_");
    }

    const std::string_view bitSpec = nextToken(rest);
    const std::size_t barPos = bitSpec.find('|');
    const std::size_t atPos = bitSpec.find('@');
    if (barPos == std::string_view::npos || atPos == std::string_view::npos || atPos <= barPos + 1 ||
        atPos + 3 != bitSpec.size()) {
        throw DbcParseError(lineNo, "invalid bit spec in SG_ (expected start|len@endianSign)");
    }

    unsigned int startBit = 0;
    unsigned int length = 0;
    if (!parseInt(bitSpec.substr(0, barPos), startBit) ||
        !parseInt(bitSpec.substr(barPos + 1, atPos - barPos - 1), length)) {
        throw DbcParseError(lineNo, "invalid start bit or length in SG_");
    }
    const char endianCh = bitSpec[atPos + 1];
    const char signCh = bitSpec[atPos + 2];
    if ((endianCh != '0' && endianCh != '1') || (signCh != '+' && signCh != '-')) {
        throw DbcParseError(lineNo, "invalid byte order or sign in SG_");
    }

    const ByteOrder order = endianCh == '0' ? ByteOrder::BigEndian : ByteOrder::LittleEndian;
    if (startBit >= 64U || length == 0U || length > 64U ||
        !signalFitsDlc(static_cast<std::uint8_t>(startBit), static_cast<std::uint8_t>(length), order, dlc)) {
        throw DbcParseError(lineNo, "signal bit range exceeds message DLC");
    }

    // Parse (scale,offset)
    rest = trim(rest);
    if (rest.empty() || rest.front() != '(') {
        throw DbcParseError(lineNo, "expected '(scale,offset)' in SG_");
    }
    const std::size_t closeParen = rest.find(')');
    if (closeParen == std::string_view::npos) {
        throw DbcParseError(lineNo, "unterminated '(scale,offset)' in SG_");
    }
    const std::string_view scaleOffset = rest.substr(1, closeParen - 1);
    const std::size_t comma = scaleOffset.find(',');
    if (comma == std::string_view::npos) {
        throw DbcParseError(lineNo, "expected comma in '(scale,offset)'");
    }
    double scale = 1.0;
    double offset = 0.0;
    if (!parseDouble(trim(scaleOffset.substr(0, comma)), scale) ||
        !parseDouble(trim(scaleOffset.substr(comma + 1)), offset)) {
        throw DbcParseError(lineNo, "invalid scale or offset in SG_");
    }
    rest.remove_prefix(closeParen + 1);

    // Parse [min|max]
    rest = trim(rest);
    if (rest.empty() || rest.front() != '[') {
        throw DbcParseError(lineNo, "expected '[min|max]' in SG_");
    }
    const std::size_t closeBracket = rest.find(']');
    if (closeBracket == std::string_view::npos) {
        throw DbcParseError(lineNo, "unterminated '[min|max]' in SG_");
    }
    const std::string_view minMax = rest.substr(1, closeBracket - 1);
    const std::size_t pipe = minMax.find('|');
    if (pipe == std::string_view::npos) {
        throw DbcParseError(lineNo, "expected '|' in '[min|max]'");
    }
    double minVal = 0.0;
    double maxVal = 0.0;
    if (!parseDouble(trim(minMax.substr(0, pipe)), minVal) || !parseDouble(trim(minMax.substr(pipe + 1)), maxVal)) {
        throw DbcParseError(lineNo, "invalid min or max in SG_");
    }
    rest.remove_prefix(closeBracket + 1);

    // Parse "unit"
    rest = trim(rest);
    if (rest.empty() || rest.front() != '"') {
        throw DbcParseError(lineNo, "expected quoted unit in SG_");
    }
    const std::size_t closeQuote = rest.find('"', 1);
    if (closeQuote == std::string_view::npos) {
        throw DbcParseError(lineNo, "unterminated unit string in SG_");
    }
    const std::string_view unit = rest.substr(1, closeQuote - 1);

    DbcSignal sig;
    sig.name = std::string(nameTok);
    sig.startBit = static_cast<std::uint8_t>(startBit);
    sig.length = static_cast<std::uint8_t>(length);
    sig.byteOrder = order;
    sig.isSigned = signCh == '-';
    sig.scale = scale;
    sig.offset = offset;
    sig.minimum = minVal;
    sig.maximum = maxVal;
    sig.unit = std::string(unit);
    return sig;
}

} // namespace

double DbcSignal::decode(std::span<const std::uint8_t, 8> data) const noexcept {
    if (length == 0 || length > 64) {
        return 0.0;
    }
    std::uint64_t raw = 0;
    if (byteOrder == ByteOrder::LittleEndian) {
        for (std::size_t i = 0; i < length; ++i) {
            const std::size_t bitPos = static_cast<std::size_t>(startBit) + i;
            if (bitPos >= 64U) {
                break;
            }
            const std::uint64_t bit = (static_cast<std::uint64_t>(data[bitPos / 8U]) >> (bitPos % 8U)) & 1ULL;
            raw |= (bit << i);
        }
    } else {
        std::size_t bitPos = startBit;
        for (std::size_t i = 0; i < length; ++i) {
            if (bitPos >= 64U) {
                break;
            }
            const std::uint64_t bit = (static_cast<std::uint64_t>(data[bitPos / 8U]) >> (bitPos % 8U)) & 1ULL;
            const std::size_t shift = static_cast<std::size_t>(length) - 1U - i;
            raw |= (bit << shift);
            bitPos = nextMotorolaBit(bitPos);
        }
    }

    if (isSigned && length < 64) {
        const std::uint64_t signBit = 1ULL << (length - 1U);
        if ((raw & signBit) != 0ULL) {
            raw |= (~0ULL << length);
        }
        return static_cast<double>(static_cast<std::int64_t>(raw)) * scale + offset;
    }
    if (isSigned) {
        return static_cast<double>(static_cast<std::int64_t>(raw)) * scale + offset;
    }
    return static_cast<double>(raw) * scale + offset;
}

void DbcSignal::encode(double physical, std::array<std::uint8_t, 8>& data) const noexcept {
    if (length == 0 || length > 64) {
        return;
    }
    const double scaled = scale != 0.0 ? (physical - offset) / scale : 0.0;
    std::uint64_t raw = 0;
    if (isSigned) {
        const std::uint64_t half = 1ULL << (length - 1U);
        const std::int64_t minRaw =
            length == 64 ? std::numeric_limits<std::int64_t>::min() : -static_cast<std::int64_t>(half);
        const std::int64_t maxRaw = static_cast<std::int64_t>(half - 1ULL);
        const double minD = static_cast<double>(minRaw);
        const double maxD = static_cast<double>(maxRaw);
        if (scaled <= minD) {
            raw = static_cast<std::uint64_t>(minRaw);
        } else if (scaled >= maxD) {
            raw = static_cast<std::uint64_t>(maxRaw);
        } else {
            raw = static_cast<std::uint64_t>(static_cast<std::int64_t>(std::llround(scaled)));
        }
    } else {
        const std::uint64_t maxRaw = length == 64 ? ~0ULL : ((1ULL << length) - 1ULL);
        const double maxD = static_cast<double>(maxRaw);
        if (scaled <= 0.0) {
            raw = 0ULL;
        } else if (scaled >= maxD) {
            raw = maxRaw;
        } else {
            raw = static_cast<std::uint64_t>(std::llround(scaled));
        }
    }

    if (byteOrder == ByteOrder::LittleEndian) {
        for (std::size_t i = 0; i < length; ++i) {
            const std::size_t bitPos = static_cast<std::size_t>(startBit) + i;
            if (bitPos >= 64U) {
                break;
            }
            const auto bit = static_cast<std::uint8_t>((raw >> i) & 1ULL);
            const auto mask = static_cast<std::uint8_t>(1U << (bitPos % 8U));
            data[bitPos / 8U] = static_cast<std::uint8_t>((data[bitPos / 8U] & ~mask) | (bit << (bitPos % 8U)));
        }
    } else {
        std::size_t bitPos = startBit;
        for (std::size_t i = 0; i < length; ++i) {
            if (bitPos >= 64U) {
                break;
            }
            const std::size_t shift = static_cast<std::size_t>(length) - 1U - i;
            const auto bit = static_cast<std::uint8_t>((raw >> shift) & 1ULL);
            const auto mask = static_cast<std::uint8_t>(1U << (bitPos % 8U));
            data[bitPos / 8U] = static_cast<std::uint8_t>((data[bitPos / 8U] & ~mask) | (bit << (bitPos % 8U)));
            bitPos = nextMotorolaBit(bitPos);
        }
    }
}

const DbcSignal* DbcMessage::findSignal(std::string_view signalName) const noexcept {
    for (const DbcSignal& s : signalDefs) {
        if (s.name == signalName) {
            return &s;
        }
    }
    return nullptr;
}

DbcDatabase DbcDatabase::parse(std::istream& in) {
    DbcDatabase db;
    std::string line;
    std::size_t lineNo = 0;
    while (std::getline(in, line)) {
        ++lineNo;
        std::string_view sv = trim(line);
        if (sv.empty() || sv.starts_with("//")) {
            continue;
        }
        const std::string_view keyword = nextToken(sv);
        if (keyword == "BO_") {
            DbcMessage msg = parseMessageLine(sv, lineNo);
            if (db.findMessage(msg.id, msg.extended) != nullptr) {
                throw DbcParseError(lineNo, "duplicate CAN message ID");
            }
            db.messages_.push_back(std::move(msg));
        } else if (keyword == "SG_") {
            if (db.messages_.empty()) {
                throw DbcParseError(lineNo, "SG_ encountered before any BO_ message");
            }
            DbcMessage& current = db.messages_.back();
            current.signalDefs.push_back(parseSignalLine(sv, current.dlc, lineNo));
        }
    }
    return db;
}

const DbcMessage* DbcDatabase::findMessage(std::uint32_t id, bool extended) const noexcept {
    for (const DbcMessage& m : messages_) {
        if (m.id == id && m.extended == extended) {
            return &m;
        }
    }
    return nullptr;
}

const DbcMessage* DbcDatabase::findMessageByName(std::string_view name) const noexcept {
    for (const DbcMessage& m : messages_) {
        if (m.name == name) {
            return &m;
        }
    }
    return nullptr;
}

} // namespace rvn
