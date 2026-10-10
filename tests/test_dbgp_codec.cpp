#include <tm_core/dbgp_codec.h>

#include <catch2/catch_test_macros.hpp>

#include <vector>

using namespace opentm::tm_core;

namespace {

std::vector<std::byte> from_hex(const char* hex) {
    std::vector<std::byte> out;
    auto val = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    for (; *hex; ) {
        while (*hex && *hex == ' ') ++hex;
        if (!*hex) break;
        const int hi = val(*hex++);
        if (!*hex) break;
        const int lo = val(*hex++);
        if (hi < 0 || lo < 0) break;
        out.push_back(std::byte{static_cast<std::uint8_t>((hi << 4) | lo)});
    }
    return out;
}

} // namespace

TEST_CASE("DBGP: encode_request round-trips a process_list request", "[dbgp]") {
    dbgp::request req;
    req.cmd        = 0x00000102;
    req.req_id     = 0x00000018;
    req.process_id = 0;

    const auto bytes = dbgp::encode_request(req);
    REQUIRE(bytes.size() == 17); // 16B header + 0B payload + 1B trailer
    // First 16 bytes should match the captured request header.
    const auto expected_header = from_hex("00000102 00000018 00000000 00000000");
    for (std::size_t i = 0; i < 16; ++i) {
        REQUIRE(static_cast<std::uint8_t>(bytes[i]) == static_cast<std::uint8_t>(expected_header[i]));
    }
}

TEST_CASE("DBGP: decode process_list reply with one process", "[dbgp]") {
    const auto bytes = from_hex(
        "80000102 00000018 00000004 00000000 00000000"  // header
        "01000500"                                       // payload (1 PID)
        "a5"); // trailer
    const auto r = dbgp::decode_response(bytes);
    REQUIRE(r.has_value());
    REQUIRE(r->cmd         == 0x80000102u);
    REQUIRE(r->req_id      == 0x00000018u);
    REQUIRE(r->data_len    == 4u);
    REQUIRE(r->process_id  == 0u);
    REQUIRE(r->result_code == 0u);

    const auto pids = dbgp::parse_process_list(*r);
    REQUIRE(pids.size() == 1);
    REQUIRE(pids[0] == 0x01000500u);
}

TEST_CASE("DBGP: decode empty process_list", "[dbgp]") {
    const auto bytes = from_hex(
        "80000102 00000005 00000000 00000000 00000000 88");
    const auto r = dbgp::decode_response(bytes);
    REQUIRE(r.has_value());
    REQUIRE(r->data_len == 0u);
    REQUIRE(dbgp::parse_process_list(*r).empty());
}

TEST_CASE("DBGP: decode user_memory_stat reply", "[dbgp]") {
    const auto bytes = from_hex(
        "80200008 00000019 0000001c 01000500 00000000"  // header
        "00000000"          // shared_created
        "00010000"          // shared_attached
        "186b0000"          // local_memory (390.7 MB)
        "00060000"          // local_text (384 KB)
        "000c0000"          // prx_text (768 KB)
        "00050000"          // prx_data (320 KB)
        "003a6000"          // remain (3.6 MB)
        "18");
    const auto r = dbgp::decode_response(bytes);
    REQUIRE(r.has_value());
    REQUIRE(r->cmd == 0x80200008u);
    const auto s = dbgp::parse_user_memory_stat(*r);
    REQUIRE(s.has_value());
    REQUIRE(s->shared_created  == 0x00000000u);
    REQUIRE(s->shared_attached == 0x00010000u);
    REQUIRE(s->local_memory    == 0x186b0000u);
    REQUIRE(s->local_text      == 0x00060000u);
    REQUIRE(s->prx_text        == 0x000c0000u);
    REQUIRE(s->prx_data        == 0x00050000u);
    REQUIRE(s->remain_memory   == 0x003a6000u);
}

TEST_CASE("DBGP: decode thread_list reply", "[dbgp]") {
    const auto bytes = from_hex(
        "80000103 0000001b 00000024 01000500 00000000"
        "00000000"                              // ppu count = 0
        "00000003"                              // spu count = 3??
        "00000001 00000000"                     // u64
        "01000086 00000000"                     // u64
        "01000085 00000000"                     // u64
        "01000083 04000100"                     // u64
        "63");
    const auto r = dbgp::decode_response(bytes);
    REQUIRE(r.has_value());
    REQUIRE(r->data_len == 36u);
    REQUIRE(r->process_id  == 0x01000500u);
    REQUIRE(r->result_code == 0u);
}

// --- request-body byte layouts: lock the wire format so a refactor cannot silently
// shift a field. These are the bytes the agent actually receives. ---

TEST_CASE("DBGP: read_memory / register / LS request bodies", "[dbgp]") {
    REQUIRE(dbgp::build_read_memory_request_body(0x8000000012345678ull, 0x100)
            == from_hex("8000000012345678 00000100"));
    REQUIRE(dbgp::build_read_ppu_registers_request_body(0x0100050000000000ull)
            == from_hex("0100050000000000"));
    REQUIRE(dbgp::build_read_spu_registers_request_body(0x0100008600000000ull)
            == from_hex("0100008600000000"));
    REQUIRE(dbgp::build_read_spu_local_store_request_body(0x0100008600000000ull, 0x3e800, 0x800)
            == from_hex("0100008600000000 0003e800 00000800"));
}

TEST_CASE("DBGP: DABR watchpoint control flags match sys/dbg.h", "[dbgp]") {
    // SYS_DBG_DABR_CTRL_READ/WRITE/CLEAR, verbatim from the SDK header.
    REQUIRE(dbgp::dabr_ctrl::read  == 0x5ull);
    REQUIRE(dbgp::dabr_ctrl::write == 0x6ull);
    REQUIRE(dbgp::dabr_ctrl::clear == 0x0ull);
}

TEST_CASE("DBGP: set_data_watchpoint request body = [addr][ctrl], both u64 BE", "[dbgp]") {
    REQUIRE(dbgp::build_set_data_watchpoint_request_body(0x0000000012345678ull, dbgp::dabr_ctrl::write)
            == from_hex("0000000012345678 0000000000000006"));
    REQUIRE(dbgp::build_set_data_watchpoint_request_body(0x00000000deadbe00ull, dbgp::dabr_ctrl::read)
            == from_hex("00000000deadbe00 0000000000000005"));
    // clear: ctrl = 0
    REQUIRE(dbgp::build_set_data_watchpoint_request_body(0, dbgp::dabr_ctrl::clear)
            == from_hex("0000000000000000 0000000000000000"));
}

TEST_CASE("DBGP: get_data_watchpoint reply parses [addr][ctrl]", "[dbgp]") {
    const auto bytes = from_hex(
        "80200009 00000030 00000010 00000000 00000000"   // header, result_code=0, data_len=16
        "0000000012345678"                                // addr
        "0000000000000006"                                // ctrl = WRITE
        "a5");
    const auto r = dbgp::decode_response(bytes);
    REQUIRE(r.has_value());
    const auto s = dbgp::parse_data_watchpoint(*r);
    REQUIRE(s.has_value());
    REQUIRE(s->addr == 0x12345678ull);
    REQUIRE(s->ctrl == dbgp::dabr_ctrl::write);
}

TEST_CASE("DBGP: breakpoint request body = [tid][addr] (INFERRED, locked here)", "[dbgp]") {
    // Body is inferred from Sony's PS3AddBreakPoint(proc, thread, addr, 1); the process id
    // rides the dbgp header, so the body carries the thread id + address. If a live reply
    // later shows a different layout, change build_breakpoint_request_body AND this test
    // together -- the test is the record of what we currently send.
    REQUIRE(dbgp::build_breakpoint_request_body(0x0100050000000000ull, 0x0000000000010000ull)
            == from_hex("0100050000000000 0000000000010000"));
}
