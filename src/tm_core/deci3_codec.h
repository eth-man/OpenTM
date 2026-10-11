//
// layout layout, all multi byte integers are BE:
//
// offset size field
// 0 2 magic always 0x3010
// 2 4 session_a zero early in a stream and carries a session id later
// 6 2 length total frame size in bytes, including this header
// 8 2 dir_marker ascii pair (in direction enum)
// 10 4 session_b zero or scope/handle id
// 14 2 category sub-protocol code
// 16 N payload opaque bytes (handed to inner codecs)
//
// sub protocol families are picked by the direction marker bytes:
// "HM"/"MH" netmp DECR control family
// "HT"/"TH" dfmp shared file/management family
// "MT"/"TM" netmp_cfw CFW target control family

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace opentm::tm_core {

inline constexpr std::uint16_t deci3_magic = 0x3010;
inline constexpr std::size_t   deci3_header_size = 16;

enum class deci3_direction : std::uint16_t {
    host_to_machine    = 0x484d, // "HM"
    machine_to_host    = 0x4d48, // "MH"
    host_to_target     = 0x4854, // "HT"
    target_to_host     = 0x5448, // "TH"
    manager_to_target  = 0x4d54, // "MT"
    target_to_manager  = 0x544d, // "TM"
};

enum class deci3_family : std::uint8_t {
    netmp,
    dfmp,
    netmp_cfw,
    unknown,
};

deci3_family family_of(deci3_direction d) noexcept;
std::string_view direction_name(deci3_direction d) noexcept;
bool is_host_originated(deci3_direction d) noexcept;

struct deci3_frame {
    std::uint32_t      session_a = 0;
    deci3_direction    direction = deci3_direction::host_to_machine;
    std::uint32_t      session_b = 0;
    std::uint16_t      category  = 0;
    std::vector<std::byte> payload;

    std::size_t wire_size() const noexcept { return deci3_header_size + payload.size(); }
};

enum class decode_error {
    short_buffer,
    bad_magic,
    bad_length,
};

struct decode_result {
    std::optional<deci3_frame> frame;
    std::size_t                consumed = 0;
    std::optional<decode_error> error;
};

std::size_t encode_frame(const deci3_frame& frame, std::vector<std::byte>& out);

decode_result decode_frame(std::span<const std::byte> buffer) noexcept;

std::vector<deci3_frame> decode_all(
    std::span<const std::byte> buffer,
    std::optional<decode_error>* error_out = nullptr,
    std::size_t* error_offset_out = nullptr) noexcept;

} // namespace opentm::tm_core
