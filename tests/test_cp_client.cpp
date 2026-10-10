#include <catch2/catch_test_macros.hpp>

#include <tm_core/cp_client.h>

using opentm::tm_core::cp_boot_params;
using opentm::tm_core::cp_client;

TEST_CASE("cp_client accepts only the CP's own tokens", "[cp_client]") {
    CHECK(cp_client::validate(cp_boot_params{})); // defaults are valid

    SECTION("every documented token is accepted") {
        for (const auto& boot : {"dbg", "sys", "rel"}) {
            cp_boot_params p;
            p.boot_mode = QString::fromLatin1(boot);
            INFO(boot);
            CHECK(cp_client::validate(p));
        }
        for (const auto& bd : {"emu_dev", "emu_usb", "drive"}) {
            cp_boot_params p;
            p.bd_access = QString::fromLatin1(bd);
            INFO(bd);
            CHECK(cp_client::validate(p));
        }
    }

    SECTION("shell metacharacters are refused") {
        const char* attacks[] = {
            "dbg; touch /tmp/pwned",
            "dbg`id`",
            "dbg$(id)",
            "dbg\nrm -rf /",
            "dbg && reboot",
            "dbg|sh",
        };
        for (const char* a : attacks) {
            cp_boot_params p;
            p.boot_mode = QString::fromLatin1(a);
            QString bad;
            INFO(a);
            CHECK_FALSE(cp_client::validate(p, &bad));
            CHECK(bad == QLatin1String("sysutil"));
        }
    }

    SECTION("a plausible-but-wrong token is refused") {
        cp_boot_params p;
        p.boot_mode = QStringLiteral("debug"); // page says "dbg"
        CHECK_FALSE(cp_client::validate(p));

        cp_boot_params q;
        q.model = QStringLiteral("ps3-hdd80"); // no such model
        QString bad;
        CHECK_FALSE(cp_client::validate(q, &bad));
        CHECK(bad == QLatin1String("model"));
    }

    SECTION("an empty field is refused rather than sent blank") {
        cp_boot_params p;
        p.hostfs.clear();
        QString bad;
        CHECK_FALSE(cp_client::validate(p, &bad));
        CHECK(bad == QLatin1String("hostfs"));
    }
}
