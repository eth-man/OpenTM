#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

// Decoding for the target's protocol register-ACKs. The target confirms each
// protocol registration with a reply that carries the protocol's port, number
// and lpar name; this turns that into a small struct, and names the protocol.

namespace opentm::tm_core::regist {

// Short name for a DECI3 protocol number, for display.
constexpr const char* proto_name(std::uint32_t proto) noexcept {
    switch (proto) {
    case 0x00000200u: return "DBGP";
    case 0x00000100u: return "DFMP";
    case 0x00000110u: return "DRFP";
    case 0x00000310u: return "TTYP";
    case 0x00800011u: return "CTRLP";
    case 0x00000020u: return "CP";
    case 0x00800300u: return "TTY";
    default:          break;
    }
    return "?";
}

// One protocol the target confirmed registered.
struct registered_protocol {
    std::uint8_t  port  = 0;
    std::uint32_t proto = 0;
    std::string   lpar;
};

// Parse a register-ACK body:
// 02 01 <port:u8> <pad:u8> <proto:u32 BE> <lpar: NUL-terminated/padded>
// Returns {port, proto, lpar} for such a body, or nothing otherwise.
inline std::optional<registered_protocol>
parse_register_ack(const std::uint8_t* p, std::size_t n) {
    if (p == nullptr || n < 8) return std::nullopt;
    if (p[0] != 0x02 || p[1] != 0x01) return std::nullopt;
    registered_protocol r;
    r.port  = p[2];
    r.proto = (static_cast<std::uint32_t>(p[4]) << 24)
            | (static_cast<std::uint32_t>(p[5]) << 16)
            | (static_cast<std::uint32_t>(p[6]) <<  8)
            |  static_cast<std::uint32_t>(p[7]);
    for (std::size_t i = 8; i < n && p[i]!= 0; ++i) r.lpar.push_back(static_cast<char>(p[i]));
    return r;
}

} // namespace opentm::tm_core::regist
