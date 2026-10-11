#include <tm_session/app_home_path.h>

#include <catch2/catch_test_macros.hpp>

using opentm::tm_session::host_to_app_home_path;

TEST_CASE("app_home: a bare filename is served as /app_home/<name>", "[app_home]") {
    REQUIRE(host_to_app_home_path(QStringLiteral("conwrite.gnpdrm.pkg"))
            == QStringLiteral("/app_home/conwrite.gnpdrm.pkg"));
    REQUIRE(host_to_app_home_path(QStringLiteral("conwrite.elf"))
            == QStringLiteral("/app_home/conwrite.elf"));
}

TEST_CASE("app_home: an absolute host path is reduced to its basename", "[app_home]") {
    // This is the bug class that broke load+install: the full host path must NOT be pasted
    // onto /app_home/. Only the basename goes on the wire.
    REQUIRE(host_to_app_home_path(QStringLiteral("/home/ran/hcall/conwrite.gnpdrm.pkg"))
            == QStringLiteral("/app_home/conwrite.gnpdrm.pkg"));
    REQUIRE(host_to_app_home_path(QStringLiteral("C:/APPHOME/conwrite.elf"))
            == QStringLiteral("/app_home/conwrite.elf"));
    REQUIRE(host_to_app_home_path(QStringLiteral("C:/Users/aiuser/Desktop/conwrite.elf"))
            == QStringLiteral("/app_home/conwrite.elf"));
}

TEST_CASE("app_home: an existing kit-side path is passed through verbatim", "[app_home]") {
    REQUIRE(host_to_app_home_path(QStringLiteral("/app_home/foo.self"))
            == QStringLiteral("/app_home/foo.self"));
    REQUIRE(host_to_app_home_path(QStringLiteral("/dev_hdd0/game/TEST00001/USRDIR/EBOOT.BIN"))
            == QStringLiteral("/dev_hdd0/game/TEST00001/USRDIR/EBOOT.BIN"));
    REQUIRE(host_to_app_home_path(QStringLiteral("/host_root/tmp/x.elf"))
            == QStringLiteral("/host_root/tmp/x.elf"));
}
