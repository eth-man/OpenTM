#include <catch2/catch_test_macros.hpp>

#include <tm_core/wol.h>

using opentm::tm_core::parse_mac;
using opentm::tm_core::format_mac;
using opentm::tm_core::mac_address;
using opentm::tm_core::build_magic_packet;

namespace {
constexpr mac_address kExpected{0x00, 0x11, 0x22, 0x33, 0x44, 0x55};
}

TEST_CASE("parse_mac accepts the spellings that actually occur", "[wol]") {
    CHECK(parse_mac("00:11:22:33:44:55") == kExpected);
    CHECK(parse_mac("00:11:22:33:44:55") == kExpected);
    CHECK(parse_mac("00-11-22-33-44-55") == kExpected);
    CHECK(parse_mac("001122334455")      == kExpected);
    CHECK(parse_mac("0011.2233.4455")    == kExpected);
    CHECK(parse_mac("  00:11:22:33:44:55  ") == kExpected);
}

TEST_CASE("parse_mac rejects the wrong number of digits", "[wol]") {
    CHECK_FALSE(parse_mac("00:11:22:33:44").has_value()); // five bytes
    CHECK_FALSE(parse_mac("00:11:22:33:44:55:AB").has_value()); // seven
    CHECK_FALSE(parse_mac("00:11:22:33:44:5").has_value()); // odd nibble
    CHECK_FALSE(parse_mac("").has_value());
    CHECK_FALSE(parse_mac("00:11:22:33:44:__").has_value());
    CHECK_FALSE(parse_mac("zz:11:22:33:44:55").has_value());
}

TEST_CASE("format_mac round trips through parse_mac", "[wol]") {
    const auto text = format_mac(kExpected);
    CHECK(text == "00:11:22:33:44:55");
    CHECK(parse_mac(text) == kExpected);
}

TEST_CASE("magic packet is 6 sync bytes then 16 copies of the MAC", "[wol]") {
    const auto packet = build_magic_packet(kExpected);
    REQUIRE(packet.size() == 102);
    for (int i = 0; i < 6; ++i) CHECK(packet[i] == 0xFF);
    for (int copy = 0; copy < 16; ++copy) {
        for (int b = 0; b < 6; ++b) {
            INFO("copy " << copy << " byte " << b);
            CHECK(packet[6 + copy * 6 + b] == kExpected[b]);
        }
    }
}
