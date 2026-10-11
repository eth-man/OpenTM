// offset size field
// 0 2 type 0x0021 in every frame observed so far
// 2 2 length inner length, INCLUDING these 4 header bytes
// 4 2 cmd e.g. 0x0206, 0x0202
// 6 N body op-specific (length 6 bytes)
//
// shared by netmp (HM/MH) and netmp_cfw (MT/TM) direction

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace opentm::tm_core {

inline constexpr std::size_t  tsmp_header_size = 6;
inline constexpr std::uint16_t tsmp_type_default = 0x0021;

// cmd word is (group << 8) | code 
// observed on decr 1000:
//
// 0x01 tsm version 0x21 LPAR status (108 byte reply)
// 0x02 session: connect/version 0x30 boot parameters (get 0x00, set 0x02)
// /login/logout 0x31 boot parameters currently in effect
// 0x04 logical console mode 0x32 system parameters
// 0x05 ip display mode 0x20 system control (below)
//
// groups 0x04 and 0x05 back settings the cp's own web UI never exposes, its lcnslsrv radio group is commented out in be_param.cgi
//
// shutdown powers the target down, reboot brings it back, and sresets is a forced reboot event, not a poweroff.
// sending a reboot where a power off was meant looks like the target spontaneously restarting
namespace tsmp_cmd {

inline constexpr std::uint16_t status    = 0x2000u; // reply 0x2001
inline constexpr std::uint16_t power_on  = 0x2002u; // reply 0x2003
inline constexpr std::uint16_t power_off = 0x2004u; // forced terminate, reply 0x2005
inline constexpr std::uint16_t reset     = 0x2006u; // forced reboot, reply 0x2007
inline constexpr std::uint16_t shutdown  = 0x2008u; // graceful, reply 0x2009
inline constexpr std::uint16_t reboot    = 0x200Au; // graceful, reply 0x200B

// accepted by the client library but rejected by the CP firmware result 0x0005 ("not supported")
inline constexpr std::uint16_t resume    = 0x200Cu;
inline constexpr std::uint16_t suspend   = 0x200Eu;

inline constexpr std::uint16_t get_boot_param = 0x3000u; // reply 0x3001
inline constexpr std::uint16_t set_boot_param = 0x3002u; // reply 0x3003
inline constexpr std::uint16_t get_cur_param  = 0x3100u; // in effect now, reply 0x3101
inline constexpr std::uint16_t get_sys_param  = 0x3200u; // reply 0x3201

inline constexpr std::uint16_t lpar_status    = 0x2100u; // reply 0x2101
inline constexpr std::uint16_t tsm_version    = 0x0100u; // reply 0x0101
inline constexpr std::uint16_t get_lcnsl_mode = 0x0400u; // logical console, reply 0x0401
inline constexpr std::uint16_t get_showip_mode = 0x0500u; // IP display, reply 0x0501

constexpr std::uint16_t reply_of(std::uint16_t request) noexcept {
    return static_cast<std::uint16_t>(request + 1);
}

} // namespace tsmp_cmd

struct tsmp_param_reply {
    std::uint16_t sub    = 0;
    std::uint32_t result = 0;
    std::uint64_t value  = 0;
};

std::optional<tsmp_param_reply> parse_param_reply(std::span<const std::byte> body) noexcept;

enum class tsmp_boot_mode { unknown, debug, system_software, release };

constexpr tsmp_boot_mode boot_mode_of(std::uint64_t boot_value) noexcept {
    switch (boot_value & 0x11ull) {
    case 0x10ull: return tsmp_boot_mode::debug;
    case 0x11ull: return tsmp_boot_mode::system_software;
    case 0x01ull: return tsmp_boot_mode::release;
    default:      return tsmp_boot_mode::unknown;
    }
}

const char* boot_mode_name(tsmp_boot_mode m) noexcept;

struct tsmp_frame {
    std::uint16_t          type = tsmp_type_default;
    std::uint16_t          cmd  = 0;
    std::vector<std::byte> body;
    std::uint16_t wire_length() const noexcept {
        return static_cast<std::uint16_t>(tsmp_header_size + body.size());
    }
};

// wire layout: 32 bytes per entry, entries starting at message offset 12:
// +0x00 char[16] name, nul padded
// +0x10 u64 BE status
// +0x18 u64 BE detail
struct tsmp_lpar_entry {
    std::string   name;
    std::uint64_t status = 0;
    std::uint64_t detail = 0;
};

// decr reports three: 
// PS3_LPAR which is what DA drives 
// PS2_SW_LPAR and PS2_NE_LPAR, the two ps2 bc partitions
// decr has no PS2 silicon so neither can run
namespace tsmp_lpar_state {
inline constexpr std::uint64_t down    = 0;
inline constexpr std::uint64_t suspend = 1; // reportable, but see tsmp_cmd
inline constexpr std::uint64_t up      = 2;
}

const char* tsmp_lpar_state_name(std::uint64_t status) noexcept;

inline constexpr std::size_t tsmp_lpar_entry_size   = 32;
inline constexpr std::size_t tsmp_lpar_entry_offset = 12;

std::vector<tsmp_lpar_entry> parse_lpar_status_reply(std::span<const std::byte> body);

enum class tsmp_decode_error {
    short_buffer,
    bad_length,
};

struct tsmp_decode_result {
    std::optional<tsmp_frame>          frame;
    std::size_t                        consumed = 0;
    std::optional<tsmp_decode_error>   error;
};

std::size_t encode_tsmp(const tsmp_frame& f, std::vector<std::byte>& out);
tsmp_decode_result decode_tsmp(std::span<const std::byte> buffer) noexcept;

} // namespace opentm::tm_core
