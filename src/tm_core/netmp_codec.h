#pragma once

#include <cstdint>

// NETMP is the DECI3 network/registration manager. When a protocol registration
// is refused, the NACK carries a one-byte status whose meaning is fixed by the
// host protocol. These are those status values and their names, so a refusal can
// be logged as a reason ("ALREADY_REGISTER") instead of a bare number.

namespace opentm::tm_core::netmp {

namespace status_code {
inline constexpr std::uint8_t inval            = 1; // malformed / invalid request
inline constexpr std::uint8_t busy             = 2;
inline constexpr std::uint8_t notconnect       = 3; // not connected
inline constexpr std::uint8_t alreadyconn      = 4; // already connected
inline constexpr std::uint8_t nomem            = 5;
inline constexpr std::uint8_t invallpar        = 6; // no such LPAR
inline constexpr std::uint8_t invalpriority    = 7;
inline constexpr std::uint8_t invalproto       = 8; // no such protocol
inline constexpr std::uint8_t power_off         = 9;
inline constexpr std::uint8_t nodeci_connect   = 10;
inline constexpr std::uint8_t noregister       = 11; // protocol not registered
inline constexpr std::uint8_t already_register = 12; // this connection already holds it
}

constexpr const char* status_name(std::uint8_t code) noexcept {
    switch (code) {
    case status_code::inval:            return "INVAL";
    case status_code::busy:             return "BUSY";
    case status_code::notconnect:       return "NOTCONNECT";
    case status_code::alreadyconn:      return "ALREADYCONN";
    case status_code::nomem:            return "NOMEM";
    case status_code::invallpar:        return "INVALLPAR";
    case status_code::invalpriority:    return "INVALPRIORITY";
    case status_code::invalproto:       return "INVALPROTO";
    case status_code::power_off:        return "POWER_OFF";
    case status_code::nodeci_connect:   return "NODECI_CONNECT";
    case status_code::noregister:       return "NOREGISTER";
    case status_code::already_register: return "ALREADY_REGISTER";
    default:                            break;
    }
    return "UNKNOWN";
}

// A refusal with this status means THIS connection already holds the protocol,
// which is benign (we asked twice); any other status means the kit will not
// hand the protocol to us.
constexpr bool status_is_already_registered(std::uint8_t code) noexcept {
    return code == status_code::already_register;
}

} // namespace opentm::tm_core::netmp
