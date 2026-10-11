#include <catch2/catch_test_macros.hpp>

#include <tm_core/regist_codec.h>

#include <string>

using namespace opentm::tm_core::regist;

TEST_CASE("proto_name maps the known DECI3 protocols", "[regist]") {
    CHECK(std::string(proto_name(0x00000200u)) == "DBGP");
    CHECK(std::string(proto_name(0x00000100u)) == "DFMP");
    CHECK(std::string(proto_name(0x00000110u)) == "DRFP");
    CHECK(std::string(proto_name(0x00000310u)) == "TTYP");
    CHECK(std::string(proto_name(0x00800011u)) == "CTRLP");
    CHECK(std::string(proto_name(0x00000020u)) == "CP");
    CHECK(std::string(proto_name(0x00800300u)) == "TTY");
    CHECK(std::string(proto_name(0x12345678u)) == "?");
}

TEST_CASE("parse_register_ack decodes a register-ACK body", "[regist]") {
    const std::uint8_t cp[] = {0x02,0x01,0xff,0x00, 0x00,0x00,0x00,0x20,
                               'P','S','3','_','L','P','A','R',0,0,0,0,0,0,0,0};
    auto r = parse_register_ack(cp, sizeof cp);
    REQUIRE(r.has_value());
    CHECK(r->port  == 0xff);
    CHECK(r->proto == 0x00000020u); // CP
    CHECK(r->lpar  == "PS3_LPAR");

    const std::uint8_t dbgp[] = {0x02,0x01,0x10,0x00, 0x00,0x00,0x02,0x00,
                                 'P','S','3','_','L','P','A','R',0,0,0,0,0,0,0,0};
    auto d = parse_register_ack(dbgp, sizeof dbgp);
    REQUIRE(d.has_value());
    CHECK(d->port  == 0x10);
    CHECK(d->proto == 0x00000200u); // DBGP
    CHECK(std::string(proto_name(d->proto)) == "DBGP");
}

TEST_CASE("parse_register_ack rejects non-ACK or short bodies", "[regist]") {
    const std::uint8_t nack[] = {0x05,0x06,0x00,0x00,0x00,0x00,0x02,0x00};
    CHECK_FALSE(parse_register_ack(nack, sizeof nack).has_value());
    const std::uint8_t tooShort[] = {0x02,0x01,0x10};
    CHECK_FALSE(parse_register_ack(tooShort, sizeof tooShort).has_value());
    CHECK_FALSE(parse_register_ack(nullptr, 0).has_value());
}
