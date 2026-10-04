#pragma once

#include <cstdint>

namespace opentm::tm_core::dcmp {

namespace type {
inline constexpr std::uint8_t connect = 0;
inline constexpr std::uint8_t echo    = 1;
inline constexpr std::uint8_t status  = 2;
inline constexpr std::uint8_t error   = 3;
}

namespace connect_code {
inline constexpr std::uint8_t connect      = 0;
inline constexpr std::uint8_t connect_r    = 1;
inline constexpr std::uint8_t disconnect   = 2;
inline constexpr std::uint8_t disconnect_r = 3;
inline constexpr std::uint8_t start        = 4;
inline constexpr std::uint8_t stop         = 5;
}

namespace status_code {
inline constexpr std::uint8_t connected       = 0;
inline constexpr std::uint8_t proto           = 1;
inline constexpr std::uint8_t delete_proto    = 2;
inline constexpr std::uint8_t space           = 3;
inline constexpr std::uint8_t system_boot     = 4;
inline constexpr std::uint8_t system_shutdown = 5;
inline constexpr std::uint8_t system_suspend  = 6;
inline constexpr std::uint8_t system_resume   = 7;
inline constexpr std::uint8_t lpar_boot       = 8;
inline constexpr std::uint8_t lpar_shutdown   = 9;
inline constexpr std::uint8_t lpar_suspend    = 10;
inline constexpr std::uint8_t lpar_resume     = 11;
}

namespace error_code {
inline constexpr std::uint8_t invalhead         = 0;
inline constexpr std::uint8_t system_off        = 1;
inline constexpr std::uint8_t system_suspended  = 2;
inline constexpr std::uint8_t lpar_none         = 3;
inline constexpr std::uint8_t lpar_suspended    = 4;
inline constexpr std::uint8_t noconnect         = 5;
inline constexpr std::uint8_t noproto           = 6;
inline constexpr std::uint8_t priority          = 7;
inline constexpr std::uint8_t nospace           = 8;
inline constexpr std::uint8_t version           = 10;  // host/target protocol version mismatch
}

constexpr const char* status_name(std::uint8_t code) noexcept {
    switch (code) {
    case status_code::connected:       return "CONNECTED";
    case status_code::proto:           return "PROTO";
    case status_code::delete_proto:    return "DELETE_PROTO";
    case status_code::space:           return "SPACE";
    case status_code::system_boot:     return "SYSTEM_BOOT";
    case status_code::system_shutdown: return "SYSTEM_SHUTDOWN";
    case status_code::system_suspend:  return "SYSTEM_SUSPEND";
    case status_code::system_resume:   return "SYSTEM_RESUME";
    case status_code::lpar_boot:       return "LPAR_BOOT";
    case status_code::lpar_shutdown:   return "LPAR_SHUTDOWN";
    case status_code::lpar_suspend:    return "LPAR_SUSPEND";
    case status_code::lpar_resume:     return "LPAR_RESUME";
    default:                           break;
    }
    return "UNKNOWN";
}

constexpr const char* error_name(std::uint8_t code) noexcept {
    switch (code) {
    case error_code::invalhead:        return "INVALHEAD";
    case error_code::system_off:       return "SYSTEM_OFF";
    case error_code::system_suspended: return "SYSTEM_SUSPENDED";
    case error_code::lpar_none:        return "LPAR_NONE";
    case error_code::lpar_suspended:   return "LPAR_SUSPENDED";
    case error_code::noconnect:        return "NOCONNECT";
    case error_code::noproto:          return "NOPROTO";
    case error_code::priority:         return "PRIORITY";
    case error_code::nospace:          return "NOSPACE";
    case error_code::version:          return "VERSION";
    default:                           break;
    }
    return "UNKNOWN";
}

constexpr bool status_means_going_down(std::uint8_t code) noexcept {
    return code == status_code::system_shutdown || code == status_code::system_suspend || code == status_code::lpar_shutdown || code == status_code::lpar_suspend;
}

constexpr bool status_means_coming_up(std::uint8_t code) noexcept {
    return code == status_code::system_boot || code == status_code::system_resume || code == status_code::lpar_boot || code == status_code::lpar_resume;
}

} // namespace opentm::tm_core::dcmp
