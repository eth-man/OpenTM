#include <catch2/catch_test_macros.hpp>

#include <tm_core/netmp_codec.h>

#include <string>

using namespace opentm::tm_core::netmp;

TEST_CASE("NETMP status codes decode to their names", "[netmp]") {
    CHECK(std::string(status_name(status_code::inval))            == "INVAL");
    CHECK(std::string(status_name(status_code::busy))            == "BUSY");
    CHECK(std::string(status_name(status_code::notconnect))      == "NOTCONNECT");
    CHECK(std::string(status_name(status_code::alreadyconn))     == "ALREADYCONN");
    CHECK(std::string(status_name(status_code::nomem))           == "NOMEM");
    CHECK(std::string(status_name(status_code::invallpar))       == "INVALLPAR");
    CHECK(std::string(status_name(status_code::invalpriority))   == "INVALPRIORITY");
    CHECK(std::string(status_name(status_code::invalproto))      == "INVALPROTO");
    CHECK(std::string(status_name(status_code::power_off))       == "POWER_OFF");
    CHECK(std::string(status_name(status_code::nodeci_connect))  == "NODECI_CONNECT");
    CHECK(std::string(status_name(status_code::noregister))      == "NOREGISTER");
    CHECK(std::string(status_name(status_code::already_register))== "ALREADY_REGISTER");
}

TEST_CASE("NETMP status codes are the wire values 1..12", "[netmp]") {
    CHECK(static_cast<int>(status_code::inval)            == 1);
    CHECK(static_cast<int>(status_code::already_register) == 12);
}

TEST_CASE("NETMP unknown status decodes to UNKNOWN", "[netmp]") {
    CHECK(std::string(status_name(0))    == "UNKNOWN");
    CHECK(std::string(status_name(13))   == "UNKNOWN");
    CHECK(std::string(status_name(0xFF)) == "UNKNOWN");
}

TEST_CASE("NETMP already-registered is benign, others are not", "[netmp]") {
    CHECK(status_is_already_registered(status_code::already_register));
    CHECK(status_is_already_registered(0x0c)); // the wire value seen when asking twice
    for (std::uint8_t c : {status_code::inval, status_code::busy, status_code::invalproto,
                           status_code::noregister}) {
        INFO(status_name(c));
        CHECK_FALSE(status_is_already_registered(c));
    }
}
