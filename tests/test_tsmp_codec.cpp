#include <tm_core/tsmp_codec.h>

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

using namespace opentm::tm_core;

namespace {
template <typename... Ts>
std::vector<std::byte> bytes_of(Ts... vs) {
    return std::vector<std::byte>{static_cast<std::byte>(static_cast<std::uint8_t>(vs))...};
}
} // namespace

TEST_CASE("tsmp decodes a minimal cat=0x0020 inner frame", "[tsmp][codec]") {
    const auto wire = bytes_of(0x00, 0x21, 0x00, 0x08, 0x02, 0x06, 0x00, 0x00);
    auto r = decode_tsmp(wire);
    REQUIRE(r.frame.has_value());
    CHECK(r.frame->type == tsmp_type_default);
    CHECK(r.frame->cmd  == 0x0206);
    REQUIRE(r.frame->body.size() == 2);
    CHECK(r.consumed == 8);
}

TEST_CASE("tsmp round-trips a frame with a multi-byte body", "[tsmp][codec]") {
    tsmp_frame f;
    f.type = tsmp_type_default;
    f.cmd  = 0x0202;
    f.body = bytes_of(0x00,0x00, 0xf8,0xff, 0x00,0x00, 0xd4,0x21, 0xca,0x2a, 0x60,0x51, 0x00,0x10, 0x40,0xc3, 0xca,0x2a);
    REQUIRE(f.wire_length() == 24);

    std::vector<std::byte> wire;
    const auto wrote = encode_tsmp(f, wire);
    CHECK(wrote == 24);
    CHECK(wire.size() == 24);
    CHECK(std::to_integer<std::uint8_t>(wire[3]) == 0x18); // length field

    auto rt = decode_tsmp(wire);
    REQUIRE(rt.frame.has_value());
    CHECK(rt.frame->type == f.type);
    CHECK(rt.frame->cmd  == f.cmd);
    CHECK(rt.frame->body == f.body);
}

TEST_CASE("TSMP system control command map", "[tsmp]") {
    using namespace opentm::tm_core::tsmp_cmd;

    CHECK(status    == 0x2000);
    CHECK(power_on  == 0x2002);
    CHECK(power_off == 0x2004); // forced terminate
    CHECK(reset     == 0x2006); // forced reboot
    CHECK(shutdown  == 0x2008); // graceful
    CHECK(reboot    == 0x200A); // graceful

    SECTION("requests are even and replies are request+1") {
        for (auto cmd : {status, power_on, power_off, reset, shutdown, reboot}) {
            INFO("cmd 0x" << std::hex << cmd);
            CHECK((cmd & 1u) == 0u);
            CHECK(reply_of(cmd) == cmd + 1);
        }
    }

    SECTION("power-down and restart are distinct commands") {
        CHECK(power_off != shutdown);
        CHECK(power_off != reboot);
        CHECK(power_off != reset);
        CHECK(power_off != 0x200A);
    }

    SECTION("suspend and resume exist but the firmware rejects them") {
        CHECK(resume  == 0x200C);
        CHECK(suspend == 0x200E);
    }
}

TEST_CASE("LPAR status reply parses into 32-byte entries", "[tsmp]") {
    using namespace opentm::tm_core;

    auto make_entry = [](const char* name, std::uint64_t status, std::uint64_t detail) {
        std::vector<std::byte> e(32, std::byte{0});
        for (std::size_t i = 0; name[i] && i < 16; ++i) {
            e[i] = static_cast<std::byte>(name[i]);
        }
        for (int i = 0; i < 8; ++i) {
            e[16 + i] = static_cast<std::byte>((status >> (56 - 8 * i)) & 0xFF);
            e[24 + i] = static_cast<std::byte>((detail >> (56 - 8 * i)) & 0xFF);
        }
        return e;
    };

    // body = everything after the 6-byte header
    std::vector<std::byte> body(6, std::byte{0}); // key + result
    // the three a DECR-1000A actually reports
    for (const auto& e : {make_entry("PS3_LPAR", 2, 0x1f600000), make_entry("PS2_SW_LPAR", 0, 0x1f600000), make_entry("PS2_NE_LPAR", 0, 0)}) {
        body.insert(body.end(), e.begin(), e.end());
    }
    REQUIRE(body.size() + tsmp_header_size == 108); // matches the wire

    const auto entries = parse_lpar_status_reply(body);
    REQUIRE(entries.size() == 3);
    CHECK(entries[0].name   == "PS3_LPAR");
    CHECK(entries[0].status == tsmp_lpar_state::up);
    CHECK(entries[0].detail == 0x1f600000);
    CHECK(entries[1].name   == "PS2_SW_LPAR"); // PS2 software emulation
    CHECK(entries[2].name   == "PS2_NE_LPAR"); // PS2 native EE/GS
    CHECK(entries[2].status == tsmp_lpar_state::down);

    CHECK(std::string(tsmp_lpar_state_name(tsmp_lpar_state::down))    == "down");
    CHECK(std::string(tsmp_lpar_state_name(tsmp_lpar_state::suspend)) == "suspend");
    CHECK(std::string(tsmp_lpar_state_name(tsmp_lpar_state::up))      == "up");
    CHECK(std::string(tsmp_lpar_state_name(99))                       == "unknown");

    SECTION("a name filling all 16 bytes is not over-read") {
        std::vector<std::byte> b(6, std::byte{0});
        auto e = make_entry("ABCDEFGHIJKLMNOP", 7, 8); // exactly 16, no nul
        b.insert(b.end(), e.begin(), e.end());
        const auto got = parse_lpar_status_reply(b);
        REQUIRE(got.size() == 1);
        CHECK(got[0].name == "ABCDEFGHIJKLMNOP");
        CHECK(got[0].status == 7);
    }

    SECTION("a partial entry is refused rather than half-parsed") {
        std::vector<std::byte> b(6 + 20, std::byte{0});
        CHECK(parse_lpar_status_reply(b).empty());
    }

    SECTION("a body too short to hold the prefix is refused") {
        std::vector<std::byte> b(3, std::byte{0});
        CHECK(parse_lpar_status_reply(b).empty());
    }
}

TEST_CASE("parameter replies decode to result plus value", "[tsmp]") {
    using namespace opentm::tm_core;

    auto body_of = [](std::initializer_list<int> bytes) {
        std::vector<std::byte> v;
        for (int b : bytes) v.push_back(std::byte{static_cast<std::uint8_t>(b)});
        return v;
    };

    SECTION("0x3001 boot param readback from a DECR-1000A") {
        // frame payload was 00 21 00 14 30 01 a9 5c 00 00 00 00 00 00 00 00 00 00 01 14
        const auto body = body_of({0xa9,0x5c, 0,0,0,0, 0,0,0,0,0,0,0x01,0x14});
        const auto r = parse_param_reply(body);
        REQUIRE(r.has_value());
        CHECK(r->sub    == 0xa95c);
        CHECK(r->result == 0);
        CHECK(r->value  == 0x114);
        CHECK(boot_mode_of(r->value) == tsmp_boot_mode::debug);
    }

    SECTION("an ack carries a result but no value") {
        const auto body = body_of({0xa9,0x5c, 0,0,0,0});
        const auto r = parse_param_reply(body);
        REQUIRE(r.has_value());
        CHECK(r->result == 0);
        CHECK(r->value  == 0);
    }

    SECTION("a body too short for the result word is refused") {
        CHECK_FALSE(parse_param_reply(body_of({0xa9,0x5c,0,0})).has_value());
    }

    SECTION("boot values classify under mask 0x11") {
        CHECK(boot_mode_of(0x10) == tsmp_boot_mode::debug);
        CHECK(boot_mode_of(0x11) == tsmp_boot_mode::system_software);
        CHECK(boot_mode_of(0x01) == tsmp_boot_mode::release);
        CHECK(boot_mode_of(0x00) == tsmp_boot_mode::unknown);
        // bits outside the mask must not change the verdict
        CHECK(boot_mode_of(0x114) == tsmp_boot_mode::debug);
        CHECK(boot_mode_of(0x115) == tsmp_boot_mode::system_software);
        CHECK(std::string(boot_mode_name(tsmp_boot_mode::debug)) == "Debug Mode");
    }
}
