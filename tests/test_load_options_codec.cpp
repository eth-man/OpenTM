#include <catch2/catch_test_macros.hpp>

#include <tm_session/load_options_codec.h>

using opentm::tm_ui::load_options;
using opentm::tm_ui::load_options_from_json;
using opentm::tm_ui::load_options_to_json;

namespace {

load_options fully_populated() {
    load_options o;
    o.cmdline                  = QStringLiteral("--verbose --level 3");
    o.home_dir                 = QStringLiteral("/dev_hdd0/game/TEST00000");
    o.reset_target             = true;
    o.clear_streams            = false;
    o.enable_debug_module      = true;
    o.disable_ppu_debug        = true;
    o.disable_spu_debug        = true;
    o.wait_for_bdvd            = true;
    o.use_elf_priority         = false;
    o.use_elf_stack            = false;
    o.priority                 = 0x123;
    o.stack_size               = 0x400;
    o.enable_extra_options     = true;
    o.lv2_exception_handler    = true;
    o.remote_play              = true;
    o.gcm_debug                = true;
    o.load_libprof             = true;
    o.core_dump                = true;
    o.remote_play_avc          = true;
    o.smart_image_capture      = true;
    o.memory_access_trap       = true;
    o.game_attribute           = 2;
    o.patch_boot               = true;
    o.core_dump_location       = 0x8;
    o.rsx_profiling_tool       = true;
    o.high_memory_footprint    = true;
    o.gcm_capture_mode         = true;
    o.paramsfo_mapping         = true;
    o.paramsfo_use_elf_dir     = true;
    o.paramsfo_path            = QStringLiteral(R"(C:\work\PARAM.SFO)");
    o.rsx_hud                  = true;
    o.rsx_hud_start_on         = true;
    o.trig_disable_ppu_exc     = true;
    o.trig_disable_spu_exc     = true;
    o.trig_disable_rsx_exc     = true;
    o.trig_disable_footswitch  = true;
    o.corefile_disable_memdump = true;
    o.exec_restart_after_dump  = true;
    o.exec_dump_fn_after_dump  = true;
    o.bootable_msg_mapping     = true;
    o.bootable_msg_use_elf_dir = true;
    o.bootable_msg_dir         = QStringLiteral(R"(D:\msgs)");
    return o;
}

} // namespace

TEST_CASE("load_options survive a JSON round trip", "[load_options]") {
    const auto original = fully_populated();
    const auto restored = load_options_from_json(load_options_to_json(original));
    REQUIRE(restored == original);
}

TEST_CASE("defaults round trip unchanged", "[load_options]") {
    const load_options original;
    const auto restored = load_options_from_json(load_options_to_json(original));
    REQUIRE(restored == original);
}

TEST_CASE("an empty object yields defaults", "[load_options]") {
    const auto restored = load_options_from_json({});
    REQUIRE(restored == load_options{});
    REQUIRE(restored.stack_size == 0x40u);
    REQUIRE(restored.use_elf_stack);
}

TEST_CASE("a partial object changes only what it names", "[load_options]") {
    QJsonObject j;
    j["wait_for_bdvd"] = true;
    j["stack_size"]    = QStringLiteral("0x200");
    const auto o = load_options_from_json(j);
    REQUIRE(o.wait_for_bdvd);
    REQUIRE(o.stack_size == 0x200u);
    REQUIRE(o.clear_streams); // untouched default
    REQUIRE_FALSE(o.enable_debug_module);
}

TEST_CASE("numeric fields accept both spellings", "[load_options]") {
    QJsonObject as_number;
    as_number["priority"] = 1001;
    REQUIRE(load_options_from_json(as_number).priority == 1001u);

    QJsonObject as_hex_string;
    as_hex_string["priority"] = QStringLiteral("0x3e9");
    REQUIRE(load_options_from_json(as_hex_string).priority == 0x3e9u);
}

TEST_CASE("every marshalled field appears in the JSON", "[load_options]") {
    const auto j = load_options_to_json(fully_populated());
    REQUIRE(j.size() == 42);
    REQUIRE(j.contains(QStringLiteral("stack_size")));
    REQUIRE(j.contains(QStringLiteral("core_dump_location")));
    REQUIRE(j.contains(QStringLiteral("bootable_msg_dir")));
}
