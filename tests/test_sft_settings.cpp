#include <catch2/catch_test_macros.hpp>

#include <tm_core/sft_settings.h>

using opentm::tm_core::sft_settings;

namespace {

const QByteArray kConsoleDump =
    "[Version]\r\n"
    "version=1\r\n"
    "\r\n"
    "[System]\r\n"
    "fakeFreeSpace=0\r\n"
    "nickname=PS3-300\r\n"
    "buttonAssign=1\r\n"
    "\r\n"
    "[Network]\r\n"
    "ipAddress=192.0.2.75\r\n"
    "dhcpHostName=B00\r\n";

} // namespace

TEST_CASE("SFT: parses sections and keys in file order", "[sft]") {
    const auto s = sft_settings::parse(kConsoleDump);
    REQUIRE(s.sections().size() == 3);
    REQUIRE(s.sections()[0].name == "Version");
    REQUIRE(s.sections()[1].name == "System");
    REQUIRE(s.sections()[2].name == "Network");

    const auto& sys = s.sections()[1].entries;
    REQUIRE(sys.size() == 3);
    REQUIRE(sys[0].key == "fakeFreeSpace");
    REQUIRE(sys[1].key == "nickname");
    REQUIRE(sys[2].key == "buttonAssign");

    REQUIRE(s.value("System", "nickname") == "PS3-300");
    REQUIRE(s.value("Network", "ipAddress") == "192.0.2.75");
}

TEST_CASE("SFT: contains distinguishes absent from empty", "[sft]") {
    const auto s = sft_settings::parse("[System]\r\nupdateServerUrl=\r\n");
    REQUIRE(s.contains("System", "updateServerUrl"));
    REQUIRE(s.value("System", "updateServerUrl").isEmpty());
    REQUIRE_FALSE(s.contains("System", "nope"));
    REQUIRE_FALSE(s.contains("Nope", "updateServerUrl"));
}

TEST_CASE("SFT: round-trips through serialise", "[sft]") {
    const auto a = sft_settings::parse(kConsoleDump);
    const auto b = sft_settings::parse(a.serialise());
    REQUIRE(b.sections().size() == a.sections().size());
    REQUIRE(b.value("System", "nickname") == "PS3-300");
    REQUIRE(b.value("Network", "dhcpHostName") == "B00");

    const auto text = a.serialise();
    REQUIRE(text.startsWith("\r\n[Version]\r\n"));
    REQUIRE_FALSE(text.contains("\n\n")); // no bare LF anywhere
}

TEST_CASE("SFT: set updates in place and appends otherwise", "[sft]") {
    auto s = sft_settings::parse(kConsoleDump);
    s.set("System", "nickname", "B00DEX");
    REQUIRE(s.value("System", "nickname") == "B00DEX");
    REQUIRE(s.sections()[1].entries.size() == 3); // updated, not appended

    s.set("System", "newKey", "7");
    REQUIRE(s.sections()[1].entries.size() == 4);
    REQUIRE(s.sections()[1].entries.back().key == "newKey");

    s.set("BrandNew", "k", "v");
    REQUIRE(s.sections().size() == 4);
    REQUIRE(s.value("BrandNew", "k") == "v");
}

TEST_CASE("SFT: diff produces only changed keys", "[sft]") {
    const auto baseline = sft_settings::parse(kConsoleDump);
    auto edited = baseline;
    edited.set("System", "nickname", "B00DEX");
    edited.set("Network", "dhcpHostName", "b00kit");

    const auto d = edited.diff_against(baseline);
    REQUIRE(d.sections().size() == 2);
    REQUIRE(d.value("System", "nickname") == "B00DEX");
    REQUIRE(d.value("Network", "dhcpHostName") == "b00kit");
    REQUIRE_FALSE(d.contains("System", "fakeFreeSpace"));
    REQUIRE_FALSE(d.contains("Version", "version"));

    REQUIRE(baseline.diff_against(baseline).empty());
}

TEST_CASE("SFT: overrides always lead with [Version]", "[sft]") {
    const auto baseline = sft_settings::parse(kConsoleDump);
    auto edited = baseline;
    edited.set("System", "nickname", "B00DEX");

    const auto o = edited.overrides_against(baseline);
    REQUIRE(o.sections().front().name == "Version");
    REQUIRE(o.value("Version", "version") == "1");
    REQUIRE(o.value("System", "nickname") == "B00DEX");
    REQUIRE_FALSE(o.contains("System", "fakeFreeSpace"));
    REQUIRE(o.serialise().startsWith("\r\n[Version]\r\nversion=1\r\n"));

    REQUIRE(baseline.overrides_against(baseline).empty());
}

TEST_CASE("SFT: overrides echo the console's own version value", "[sft]") {
    const auto baseline = sft_settings::parse("[Version]\r\nversion=7\r\n\r\n[System]\r\nnickname=a\r\n");
    auto edited = baseline;
    edited.set("System", "nickname", "b");
    REQUIRE(edited.overrides_against(baseline).value("Version", "version") == "7");
}

TEST_CASE("SFT: a key absent from the baseline counts as changed", "[sft]") {
    const auto baseline = sft_settings::parse("[System]\r\na=1\r\n");
    auto edited = baseline;
    edited.set("System", "b", "2");
    const auto d = edited.diff_against(baseline);
    REQUIRE(d.value("System", "b") == "2");
    REQUIRE_FALSE(d.contains("System", "a"));
}

TEST_CASE("SFT: tolerates LF-only input and stray lines", "[sft]") {
    const auto s = sft_settings::parse("[System]\nnickname=x\njunk-with-no-equals\n; comment\nk=v");
    REQUIRE(s.value("System", "nickname") == "x");
    REQUIRE(s.value("System", "k") == "v");
    REQUIRE(s.sections()[0].entries.size() == 2); // junk + comment dropped
}
