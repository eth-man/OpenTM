#include <tm_core/drfp_codec.h>

#include <catch2/catch_test_macros.hpp>

#include <QByteArray>

using namespace opentm::tm_core;

namespace {

QByteArray from_hex(const char* hex) {
    return QByteArray::fromHex(QByteArray(hex));
}

} // namespace

TEST_CASE("DRFP: parse STAT request from kit (ucmd=0x0e)", "[drfp]") {
    const auto bytes = from_hex(
        "0000000e"
        "00000091"
        "63656c6c6d61726b5f646563722e73656c6600");

    const auto parsed = parse_drfp(bytes);
    REQUIRE(parsed.has_value());
    REQUIRE(parsed->code == drfp_code::stat);
    REQUIRE(parsed->seq  == 0x91u);
    REQUIRE(parsed->payload == QByteArray("cellmark_decr.self\0", 19));
}

TEST_CASE("DRFP: build STAT reply matches TM wire bytes", "[drfp]") {
    const auto expected = from_hex(
        "0000000f"
        "00000091"
        "00000000"  // result = success
        "00008180"  // mode (regular file)
        "ffffffff"
        "ffffffff"
        "00000000" "6a08e4b8"   // mtime
        "00000000" "6a08e4b8"   // atime
        "00000000" "6a08e441"   // ctime
        "00000000"              // unknown_c
        "00150510"); // size (1377552 bytes)

    drfp_stat st;
    st.mode      = 0x00008180u;
    st.unknown_a = 0xffffffffu;
    st.unknown_b = 0xffffffffu;
    st.mtime     = 0x000000006a08e4b8ull;
    st.atime     = 0x000000006a08e4b8ull;
    st.ctime     = 0x000000006a08e441ull;
    st.unknown_c = 0;
    st.size      = 0x00150510u;

    const auto built = build_stat_reply(drfp_code::stat_reply, 0x91u, 0, st);
    REQUIRE(built == expected);
}

TEST_CASE("DRFP: build INIT reply has version + max code tail", "[drfp]") {
    const auto expected = from_hex(
        "00000001"   // ucmd = INITR
        "00000000"   // seq echoed
        "00000000"   // reserved/result = 0
        "0000000f"); // max DRFP code = 15

    const auto built = build_init_reply(/*seq=*/0);
    REQUIRE(built == expected);
}

TEST_CASE("DRFP: build READ reply layout", "[drfp]") {
    const auto data = from_hex(
        "53434500" "00000002" "80000001" "00000410"
        "00000000" "00000980" "00000000" "0014fb90");

    const auto expected_prefix = from_hex(
        "00000007" "00000095" "00000000" "00000020");

    const auto built = build_read_reply(0x95u, 0, data);
    REQUIRE(built.size() == 16 + data.size());
    REQUIRE(built.left(16) == expected_prefix);
    REQUIRE(built.mid(16) == data);
}

TEST_CASE("DRFP: build WRITE reply layout", "[drfp]") {
    const auto ok = build_write_reply(0x18cu, 0, 0xa7u);
    REQUIRE(ok == from_hex("00000009" "0000018c" "00000000" "000000a7"));

    const auto ebadf = build_write_reply(0x18cu, static_cast<std::int32_t>(0x8001002a), 0x40u);
    REQUIRE(ebadf == from_hex("00000009" "0000018c" "8001002a" "00000000"));
}

TEST_CASE("DRFP: build OPENDIR reply matches TM on the wire", "[drfp]") {
    const auto ok = build_dopen_reply(9, 0, 8);
    REQUIRE(ok == from_hex("00000015" "00000009" "00000000" "00000008"));

    const auto err = build_dopen_reply(9, static_cast<std::int32_t>(0x8001002e), 8);
    REQUIRE(err == from_hex("00000015" "00000009" "8001002e" "ffffffff"));
}

TEST_CASE("DRFP: dirent is a fixed 258-byte struct", "[drfp]") {
    // struct { u8 d_type; u8 d_namesize; signed char d_name[256]; }
    const auto e = build_dirent(drfp_dtype::directory, QByteArray("abc"));
    REQUIRE(e.size() == 258);
    REQUIRE(static_cast<unsigned char>(e[0]) == 1u); // DRFP_DT_DIRECTORY
    REQUIRE(static_cast<unsigned char>(e[1]) == 3u); // namesize
    REQUIRE(e.mid(2, 3) == QByteArray("abc"));
    REQUIRE(e.mid(5, 253) == QByteArray(253, '\0')); // NUL padding

    const auto big = build_dirent(drfp_dtype::file, QByteArray(400, 'x'));
    REQUIRE(big.size() == 258);
    REQUIRE(static_cast<unsigned char>(big[1]) == 255u);
}

TEST_CASE("DRFP: READDIR reply carries result + nbytes + entry", "[drfp]") {
    const auto entry = build_dirent(drfp_dtype::file, QByteArray("a"));
    const auto ok = build_dread_reply(0x20, 0, entry);
    REQUIRE(ok.size() == 8 + 8 + entry.size());
    REQUIRE(ok.left(16) == from_hex("00000019" "00000020" "00000000" "00000102"));
    REQUIRE(ok.mid(16) == entry);

    const auto eod = build_dread_reply(0x20, 0, {});
    REQUIRE(eod == from_hex("00000019" "00000020" "00000000" "00000000"));
}

TEST_CASE("DRFP: parse a built frame round-trips", "[drfp]") {
    QByteArray path("/app_home/foo.self");
    path.append('\0');

    const auto frame = build_drfp(drfp_code::stat, 0x1234u, path);
    const auto parsed = parse_drfp(frame);

    REQUIRE(parsed.has_value());
    REQUIRE(parsed->code == drfp_code::stat);
    REQUIRE(parsed->seq  == 0x1234u);
    REQUIRE(parsed->payload == path);
}

TEST_CASE("DRFP: parse rejects truncated and unknown frames", "[drfp]") {
    REQUIRE_FALSE(parse_drfp(from_hex("0000000e000000")).has_value()); // 7B
    REQUIRE_FALSE(parse_drfp(from_hex("00000042000000ff")).has_value()); // code > 33
}
