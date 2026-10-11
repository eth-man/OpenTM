// verified against captures:
// 0x01000300 setmonitor "Warning: unavailable resolution" stream 3
// 0x01010200 setmonitor "monitorType = 9... OK" stream 2
// 0x01020200 game stdout (b00_load_from_disk) stream 2
// 0x01000500 "[DA]: [LAUNCH_GAME_PARAM] reply failed" stream 5
// TODO: add more streams since TM support it
#pragma once

#include <cstdint>
#include <string_view>

namespace opentm::tm_core {

inline constexpr std::uint8_t tty_stream_count = 0x12;

constexpr std::uint8_t tty_stream_of(std::uint32_t marker) noexcept {
    return static_cast<std::uint8_t>((marker >> 8) & 0xffu);
}

constexpr std::uint8_t tty_unit_of(std::uint32_t marker) noexcept {
    return static_cast<std::uint8_t>((marker >> 16) & 0xffu);
}

constexpr std::string_view tty_stream_name(std::uint8_t stream) noexcept {
    switch (stream) {
    case 0x00: return "TM";
    case 0x01: return "KERNEL";
    case 0x02: return "PPU";
    case 0x03: return "PPU (STDERR)";
    case 0x04: return "SPU";
    case 0x05: return "USER 1";
    case 0x06: return "USER 2";
    case 0x07: return "USER 3";
    case 0x08: return "USER 4";
    case 0x09: return "USER 5";
    case 0x0a: return "USER 6";
    case 0x0b: return "USER 7";
    case 0x0c: return "USER 8";
    case 0x0d: return "USER 9";
    case 0x0e: return "USER 10";
    case 0x0f: return "USER 11";
    case 0x10: return "USER 12";
    case 0x11: return "USER 13";
    default:   return "";
    }
}

} // namespace opentm::tm_core
