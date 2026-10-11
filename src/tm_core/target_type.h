#pragma once

#include <cstdint>
#include <string_view>
#include <optional>

namespace opentm::tm_core {

enum class target_type : std::uint8_t {
    decr_tcp,    // PS3_DEH_TCP - DECR series. TCP/8530. NETMP+TSMP+DFMP.
    cfw_dex,     // PS3_DBG_DEX - debug station or retail running CFW/DEX. TCP/1000. NETMP_CFW+TSMP+DFMP
    core_dump,   // PS3_CORE_DUMP - some bs
    unknown,
};

constexpr std::string_view target_type_string(target_type t) noexcept {
    switch (t) {
    case target_type::decr_tcp:  return "PS3_DEH_TCP";
    case target_type::cfw_dex:   return "PS3_DBG_DEX";
    case target_type::core_dump: return "PS3_CORE_DUMP";
    case target_type::unknown:   return "";
    }
    return "";
}

constexpr std::optional<target_type> target_type_from_string(std::string_view s) noexcept {
    if (s == "PS3_DEH_TCP")   return target_type::decr_tcp;
    if (s == "PS3_DBG_DEX")   return target_type::cfw_dex;
    if (s == "PS3_CORE_DUMP") return target_type::core_dump;
    return std::nullopt;
}

struct target_timeouts {
    int default_ms   = 3000;
    int reset_ms     = 60000;
    int connect_ms   = 30000;
    int load_ms      = 60000;
    int status_ms    = 700;
    int reconnect_ms = 60000;
    int game_port_ms = 0;
    int game_exit_ms = 10000;
};

constexpr std::uint16_t default_deci3_port(target_type t) noexcept {
    switch (t) {
    case target_type::decr_tcp: return 8530;
    case target_type::cfw_dex:  return 1000;
    default:                    return 0;
    }
}

} // namespace opentm::tm_core
