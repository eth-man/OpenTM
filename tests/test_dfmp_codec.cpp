#include <tm_core/dfmp_codec.h>
#include <tm_core/be_io.h>

#include <catch2/catch_test_macros.hpp>

#include <span>
#include <string>
#include <vector>

using namespace opentm::tm_core;

namespace {
template <typename... Ts>
std::vector<std::byte> bytes_of(Ts... vs) {
    return std::vector<std::byte>{static_cast<std::byte>(static_cast<std::uint8_t>(vs))...};
}
} // namespace

TEST_CASE("dfmp decodes the OpenDir request from trying_to_transfer_files", "[dfmp][codec]") {
    std::vector<std::byte> wire;
    auto hdr = bytes_of(
        0x00, 0x20, 0x00, 0x13,
        0x00, 0x00, 0x00, 0x18,
        0x00, 0x00, 0x04, 0x28,
        0x00, 0x00, 0x00, 0x00);
    wire.insert(wire.end(), hdr.begin(), hdr.end());
    wire.insert(wire.end(), 0x428, std::byte{0});

    auto r = decode_dfmp(wire);
    REQUIRE(r.frame.has_value());
    const auto& f = *r.frame;
    CHECK(f.cmd             == dfmp_op_cmd::open_directory);
    CHECK(f.seq             == 0x18u);
    CHECK(f.body.size()     == 0x428u);
    CHECK(f.status_or_flags == 0u);
    CHECK_FALSE(f.is_reply());
    CHECK(r.consumed        == wire.size());
}

TEST_CASE("dfmp reply detection and reply_of helper", "[dfmp][codec]") {
    dfmp_frame req;
    req.cmd = dfmp_op_cmd::open_directory;
    CHECK_FALSE(req.is_reply());

    dfmp_frame rep;
    rep.cmd = dfmp_op_cmd::reply_of(dfmp_op_cmd::open_directory);
    CHECK(rep.is_reply());
    CHECK(rep.cmd == 0x80200013u);

    CHECK(dfmp_op_cmd::reply_of(dfmp_op_cmd::file_operation) == 0x8020000Fu);
}

TEST_CASE("dfmp encode/decode round-trip preserves all fields", "[dfmp][codec]") {
    dfmp_frame f;
    f.cmd             = dfmp_op_cmd::open_directory;
    f.seq             = 42;
    f.status_or_flags = 0;
    f.body.assign(8, std::byte{0xab});

    std::vector<std::byte> wire;
    const auto wrote = encode_dfmp(f, wire);
    CHECK(wrote == dfmp_header_size + f.body.size());
    REQUIRE(wire.size() == dfmp_header_size + 8u);

    // be
    CHECK(std::to_integer<std::uint8_t>(wire[0])  == 0x00);
    CHECK(std::to_integer<std::uint8_t>(wire[1])  == 0x20);
    CHECK(std::to_integer<std::uint8_t>(wire[2])  == 0x00);
    CHECK(std::to_integer<std::uint8_t>(wire[3])  == 0x13);
    CHECK(std::to_integer<std::uint8_t>(wire[7])  == 0x2a); // seq=42
    CHECK(std::to_integer<std::uint8_t>(wire[11]) == 0x08); // body_len=8

    auto rt = decode_dfmp(wire);
    REQUIRE(rt.frame.has_value());
    CHECK(rt.frame->cmd             == f.cmd);
    CHECK(rt.frame->seq             == f.seq);
    CHECK(rt.frame->status_or_flags == f.status_or_flags);
    CHECK(rt.frame->body            == f.body);
}

TEST_CASE("build_path_op_body lays out the inner header + padded path slot", "[dfmp][codec]") {
    const auto body = build_path_op_body(0x00000036, "/dev_hdd0/");

    REQUIRE(body.size() == 8u + dfmp_path_slot);
    REQUIRE(body.size() == 0x428u);

    // param_c at offset 0 (BE u32)
    CHECK(std::to_integer<std::uint8_t>(body[0])  == 0x00);
    CHECK(std::to_integer<std::uint8_t>(body[1])  == 0x00);
    CHECK(std::to_integer<std::uint8_t>(body[2])  == 0x00);
    CHECK(std::to_integer<std::uint8_t>(body[3])  == 0x36);
    // reserved u32 zero at offset 4
    CHECK(std::to_integer<std::uint8_t>(body[4])  == 0x00);
    CHECK(std::to_integer<std::uint8_t>(body[5])  == 0x00);
    CHECK(std::to_integer<std::uint8_t>(body[6])  == 0x00);
    CHECK(std::to_integer<std::uint8_t>(body[7])  == 0x00);
    // path bytes appear at offset 8
    CHECK(std::to_integer<std::uint8_t>(body[8])  == static_cast<std::uint8_t>('/'));
    CHECK(std::to_integer<std::uint8_t>(body[9])  == static_cast<std::uint8_t>('d'));
    // NUL terminator after the 10-char path
    CHECK(std::to_integer<std::uint8_t>(body[8 + 10]) == 0x00);
    // and the tail of the slot is still zero
    CHECK(std::to_integer<std::uint8_t>(body[body.size() - 1]) == 0x00);
}

TEST_CASE("get_entries: typeless root entries decode as directories", "[dfmp]") {
    using namespace opentm::tm_core;

    constexpr std::size_t kHeader = 24;
    constexpr std::size_t kStride = 296;

    std::vector<std::byte> body(kHeader + 2 * kStride + 8, std::byte{0});
    auto put_be32 = [&body](std::size_t off, std::uint32_t v) {
        body[off + 0] = std::byte{static_cast<std::uint8_t>(v >> 24)};
        body[off + 1] = std::byte{static_cast<std::uint8_t>(v >> 16)};
        body[off + 2] = std::byte{static_cast<std::uint8_t>(v >> 8)};
        body[off + 3] = std::byte{static_cast<std::uint8_t>(v)};
    };
    auto put_name = [&body](std::size_t off, const char* n) {
        for (std::size_t i = 0; n[i]; ++i) {
            body[off + i] = std::byte{static_cast<std::uint8_t>(n[i])};
        }
    };

    put_be32(20, 2); // entry count

    put_be32(kHeader + 0x0c, 1);
    put_be32(kHeader + 0x10, 0x00000000);
    put_name(kHeader + 0x34, "dev_hdd0");

    put_be32(kHeader + kStride + 0x0c, 1);
    put_be32(kHeader + kStride + 0x10, 0x000041ff);
    put_name(kHeader + kStride + 0x34, "game_debug");

    const auto entries = parse_get_entries_reply(body);
    REQUIRE(entries.size() == 2);
    CHECK(entries[0].name == "dev_hdd0");
    CHECK(entries[0].is_directory()); // would fail without the fix
    CHECK(entries[1].name == "game_debug");
    CHECK(entries[1].is_directory());
}

TEST_CASE("DFMP transfer body matches real TM, both directions", "[dfmp]") {
    using namespace opentm::tm_core;

    auto u32_at = [](const std::vector<std::byte>& b, std::size_t off) {
        return read_be_u32(std::span<const std::byte>(b), off);
    };
    auto text_at = [](const std::vector<std::byte>& b, std::size_t off) {
        return std::string(reinterpret_cast<const char*>(b.data()) + off);
    };

    SECTION("pull to host") {
        const auto body = build_transfer_body(
            dfmp_transfer_direction::to_host,
            "/dev_hdd0/boot_plugins.txt",
            host_transfer_path("C:/Users/dev/Desktop/boot_plugins.txt"),
            0x26);

        REQUIRE(body.size() == 2144); // 32 metadata + two 1056 slots
        CHECK(u32_at(body, 0x00) == dfmp_file_op_kind::transfer);
        CHECK(u32_at(body, 0x04) == 3);
        CHECK(u32_at(body, 0x08) == 1);
        CHECK(u32_at(body, 0x10) == 0); // no mtime when pulling
        CHECK(u32_at(body, 0x18) == 0x26);
        CHECK(u32_at(body, 0x1C) == 3);
        CHECK(text_at(body, 0x020) == "/dev_hdd0/boot_plugins.txt");
        CHECK(text_at(body, 0x440) ==
              "/app_home/C:/Users/dev/Desktop/boot_plugins.txt");
    }

    SECTION("push to target") {
        const auto body = build_transfer_body(
            dfmp_transfer_direction::to_target,
            host_transfer_path(R"(C:\Users\dev\Desktop\dummy)"),
            "/dev_hdd0/dummy",
            0x80, 0x6a07ff87);

        REQUIRE(body.size() == 2144);
        CHECK(u32_at(body, 0x00) == dfmp_file_op_kind::transfer);
        CHECK(u32_at(body, 0x04) == 1); // differs from a pull
        CHECK(u32_at(body, 0x08) == 0); // differs from a pull
        CHECK(u32_at(body, 0x10) == 0x6a07ff87); // source mtime
        CHECK(u32_at(body, 0x18) == 0x80);
        CHECK(u32_at(body, 0x1C) == 1);
        CHECK(text_at(body, 0x020) == R"(/app_home/C:\Users\dev\Desktop\dummy)");
        CHECK(text_at(body, 0x440) == "/dev_hdd0/dummy");
    }

    SECTION("the two directions are distinguishable on the wire") {
        const auto pull = build_transfer_body(
            dfmp_transfer_direction::to_host, "/a", "/app_home/b", 1);
        const auto push = build_transfer_body(
            dfmp_transfer_direction::to_target, "/app_home/b", "/a", 1);
        CHECK(u32_at(pull, 0x04) != u32_at(push, 0x04));
        CHECK(u32_at(pull, 0x08) != u32_at(push, 0x08));
    }

    SECTION("an over-long path is truncated inside its slot, not overflowing") {
        const std::string huge(4000, 'a');
        const auto b = build_transfer_body(
            dfmp_transfer_direction::to_host, huge, huge, 1);
        CHECK(b.size() == 2144);
        // the last byte of each slot stays nul so the target sees a terminator
        CHECK(b[0x020 + dfmp_path_slot - 1] == std::byte{0});
        CHECK(b[0x440 + dfmp_path_slot - 1] == std::byte{0});
    }
}

TEST_CASE("DFMP delete is a single path op", "[dfmp]") {
    using namespace opentm::tm_core;
    const auto body = build_path_op_body(dfmp_file_op_kind::remove, "/dev_hdd0/game/UPDWEBMOD/ICON0.PNG");
    REQUIRE(body.size() == 8 + dfmp_path_slot);
    CHECK(read_be_u32(std::span<const std::byte>(body), 0) == 0x08u);
    CHECK(read_be_u32(std::span<const std::byte>(body), 4) == 0u);
    CHECK(std::string(reinterpret_cast<const char*>(body.data()) + 8)
          == "/dev_hdd0/game/UPDWEBMOD/ICON0.PNG");
}

TEST_CASE("chmod and utime bodies match the captured TM frames", "[dfmp]") {
    using namespace opentm::tm_core;
    const std::string path = "/dev_hdd0/cellmark/cellmark_decr.self";

    SECTION("chmod carries the full st_mode at +0x08") {
        // tm_update_permissions.pcapng: 00 00 00 16 | 00 00 00 00 | 00 00 81 c0 | path
        const auto body = build_chmod_body(0x81c0, path);
        REQUIRE(body.size() == 12 + dfmp_path_slot);
        CHECK(read_be_u32(body.data() + 0) == dfmp_file_op_kind::chmod);
        CHECK(read_be_u32(body.data() + 4) == 0);
        CHECK(read_be_u32(body.data() + 8) == 0x81c0);
        CHECK(std::string(reinterpret_cast<const char*>(body.data() + 12)) == path);
    }

    SECTION("utime carries atime then mtime as u64") {
        // change_time_settings_epoch_2006.pcapng
        const auto body = build_utime_body(1154554478ull, 1154554088ull, path);
        REQUIRE(body.size() == 24 + dfmp_path_slot);
        CHECK(read_be_u32(body.data() + 0) == dfmp_file_op_kind::utime);
        CHECK(read_be_u32(body.data() + 8) == 0); // high word of atime
        CHECK(read_be_u32(body.data() + 12) == 0x44d11a6e); // atime
        CHECK(read_be_u32(body.data() + 20) == 0x44d118e8); // mtime
        CHECK(std::string(reinterpret_cast<const char*>(body.data() + 24)) == path);
    }

    SECTION("an over-long path is truncated inside the slot, still nul terminated") {
        const std::string huge(dfmp_path_slot + 64, 'a');
        const auto body = build_chmod_body(0x81a4, huge);
        REQUIRE(body.size() == 12 + dfmp_path_slot);
        CHECK(body.back() == std::byte{0});
    }
}

TEST_CASE("rename and mkdir bodies follow the NS_FTP_DQ builders", "[dfmp]") {
    using namespace opentm::tm_core;

    SECTION("rename is two path slots, source then destination") {
        const auto body = build_rename_body("/dev_hdd0/cellmark/old.self", "/dev_hdd0/cellmark/new.self");
        REQUIRE(body.size() == 8 + dfmp_path_slot * 2);
        CHECK(read_be_u32(body.data() + 0) == dfmp_file_op_kind::rename);
        CHECK(read_be_u32(body.data() + 4) == 0);
        CHECK(std::string(reinterpret_cast<const char*>(body.data() + 8)) == "/dev_hdd0/cellmark/old.self");
        CHECK(std::string(reinterpret_cast<const char*>(body.data() + 8 + dfmp_path_slot)) == "/dev_hdd0/cellmark/new.self");
    }

    SECTION("mkdir carries the mode where chmod does") {
        const auto body = build_mkdir_body(0777, "/dev_hdd0/cellmark/sub");
        REQUIRE(body.size() == 12 + dfmp_path_slot);
        CHECK(read_be_u32(body.data() + 0) == dfmp_file_op_kind::make_dir);
        CHECK(read_be_u32(body.data() + 8) == 0777);
        CHECK(std::string(reinterpret_cast<const char*>(body.data() + 12)) == "/dev_hdd0/cellmark/sub");
    }

    SECTION("both slots stay nul terminated with over-long paths") {
        const std::string huge(dfmp_path_slot + 32, 'z');
        const auto body = build_rename_body(huge, huge);
        REQUIRE(body.size() == 8 + dfmp_path_slot * 2);
        CHECK(body[8 + dfmp_path_slot - 1] == std::byte{0});
        CHECK(body.back() == std::byte{0});
    }
}
