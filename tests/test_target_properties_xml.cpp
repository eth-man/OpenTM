#include <catch2/catch_test_macros.hpp>

#include <tm_ui/target_properties_xml.h>

using namespace opentm::tm_ui;

namespace {

target_record populated() {
    target_record r;
    r.name            = QStringLiteral("b00");
    r.type            = opentm::tm_core::target_type::cfw_dex;
    r.host            = QStringLiteral("192.0.2.75");
    r.port            = 1000;
    r.mac             = QStringLiteral("00:11:22:33:44:55");
    r.file_server_dir = QStringLiteral("D:/build");
    r.home_dir        = QStringLiteral("D:/home");
    r.force_case_sensitive = true;
    r.env_var_expansion    = true;
    r.events_to_log        = QStringLiteral("READ | WRITE");
    r.file_serving_log_size = 8192;
    r.file_trace_log_size   = 131072;
    r.console_cache_kb      = 2048;
    r.reset_mode         = target_record::reset_release_mode;
    r.reset_boot_value   = 0x01;
    r.reset_boot_mask    = 0x11;
    r.reset_system_value = 0xdeadbeefcafe1234ull;
    r.reset_system_mask  = 0xbfffffffffffffffull;
    r.display_reset_settings = false;

    r.timeouts.default_ms   = 1111;
    r.timeouts.reset_ms     = 2222;
    r.timeouts.connect_ms   = 3333;
    r.timeouts.load_ms      = 4444;
    r.timeouts.status_ms    = 555;
    r.timeouts.reconnect_ms = 6666;
    r.timeouts.game_port_ms = 777;
    r.timeouts.game_exit_ms = 8888;

    r.load.cmdline             = QStringLiteral("-a -b");
    r.load.priority            = 0x123;
    r.load.stack_size          = 0x200;
    r.load.use_elf_priority    = false;
    r.load.use_elf_stack       = false;
    r.load.enable_debug_module = true;
    r.load.disable_ppu_debug   = true;
    r.load.disable_spu_debug   = true;
    r.load.wait_for_bdvd       = true;
    r.load.reset_target        = true;
    r.load.clear_streams       = false;
    r.load.enable_extra_options  = true;
    r.load.lv2_exception_handler = true;
    r.load.remote_play           = true;
    r.load.gcm_debug             = true;
    r.load.load_libprof          = true;
    r.load.core_dump             = true;
    r.load.remote_play_avc       = true;
    r.load.smart_image_capture   = true;
    r.load.memory_access_trap    = true;
    r.load.game_attribute        = 2;
    r.load.patch_boot            = true;
    r.load.core_dump_location    = 0x8;
    r.load.rsx_profiling_tool    = true;
    r.load.high_memory_footprint = true;
    r.load.gcm_capture_mode      = true;
    return r;
}

} // namespace

TEST_CASE("target properties survive an XML round-trip", "[props_xml]") {
    const target_record in = populated();

    target_record out; // defaults - everything must come from the file
    QString error;
    REQUIRE(target_properties_from_xml(target_properties_to_xml(in), out, &error));
    REQUIRE(error.isEmpty());

    CHECK(out.name == in.name);
    CHECK(out.type == in.type);
    CHECK(out.host == in.host);
    CHECK(out.port == in.port);
    CHECK(out.mac == in.mac);
    CHECK(out.file_server_dir == in.file_server_dir);
    CHECK(out.home_dir == in.home_dir);
    CHECK(out.force_case_sensitive == in.force_case_sensitive);
    CHECK(out.env_var_expansion == in.env_var_expansion);
    CHECK(out.events_to_log == in.events_to_log);
    CHECK(out.file_serving_log_size == in.file_serving_log_size);
    CHECK(out.file_trace_log_size == in.file_trace_log_size);
    CHECK(out.console_cache_kb == in.console_cache_kb);
    CHECK(out.reset_mode == in.reset_mode);
    CHECK(out.reset_boot_value == in.reset_boot_value);
    CHECK(out.reset_boot_mask == in.reset_boot_mask);
    CHECK(out.reset_system_value == in.reset_system_value);
    CHECK(out.reset_system_mask == in.reset_system_mask);
    CHECK(out.display_reset_settings == in.display_reset_settings);

    CHECK(out.timeouts.default_ms == in.timeouts.default_ms);
    CHECK(out.timeouts.reset_ms == in.timeouts.reset_ms);
    CHECK(out.timeouts.connect_ms == in.timeouts.connect_ms);
    CHECK(out.timeouts.load_ms == in.timeouts.load_ms);
    CHECK(out.timeouts.status_ms == in.timeouts.status_ms);
    CHECK(out.timeouts.reconnect_ms == in.timeouts.reconnect_ms);
    CHECK(out.timeouts.game_port_ms == in.timeouts.game_port_ms);
    CHECK(out.timeouts.game_exit_ms == in.timeouts.game_exit_ms);

    CHECK(out.load.cmdline == in.load.cmdline);
    CHECK(out.load.priority == in.load.priority);
    CHECK(out.load.stack_size == in.load.stack_size);
    CHECK(out.load.use_elf_priority == in.load.use_elf_priority);
    CHECK(out.load.use_elf_stack == in.load.use_elf_stack);
    CHECK(out.load.enable_debug_module == in.load.enable_debug_module);
    CHECK(out.load.disable_ppu_debug == in.load.disable_ppu_debug);
    CHECK(out.load.disable_spu_debug == in.load.disable_spu_debug);
    CHECK(out.load.wait_for_bdvd == in.load.wait_for_bdvd);
    CHECK(out.load.reset_target == in.load.reset_target);
    CHECK(out.load.clear_streams == in.load.clear_streams);
    CHECK(out.load.enable_extra_options == in.load.enable_extra_options);
    CHECK(out.load.game_attribute == in.load.game_attribute);
    CHECK(out.load.core_dump_location == in.load.core_dump_location);
    CHECK(out.load.gcm_capture_mode == in.load.gcm_capture_mode);
}

TEST_CASE("TM's own export parses", "[props_xml]") {
    const QByteArray tm = R"(<?xml version="1.0" encoding="utf-8" standalone="yes"?>
<Target>
	<ServerSettings
		HomeDir="D:\C++\cellmark\build"
		CaseSensitiveFileServing0="n"
		EnableEnvVarExpansion="n"
		TTYCacheSize="00100000"
		DefaultLoadFlags="00000300"
		MaxLoadRetryTime="00003A98"
		DefaultELFLoadPriority="000003E9"
		DefaultELFStackSize="00000040"
		CoreDumpFlags="0000000000000002"
		DisplayResetSettings="y"/>
	<UiSettings FileServingHistoryLength="00001000" FileTraceMaxEventHistory="00010000">
		<Options
			LastElfArgs=""
			FSPath="D:\C++\cellmark\build\"
			HomePath="D:\C++\cellmark\build"
			EnableDebugging="n"
			DisablePPUDebugging="n"
			DisableSPUDebugging="n"/>
		<ResetParameters
			ResetType="00000002"
			BootValue="0000000000000010"
			BootMask="0000000000000011"
			SystemValue="0000000000000000"
			SystemMask="0000000000000000"/>
	</UiSettings>
</Target>)";

    target_record r;
    QString error;
    REQUIRE(target_properties_from_xml(tm, r, &error));

    CHECK(r.home_dir == QStringLiteral("D:\\C++\\cellmark\\build"));
    CHECK(r.force_case_sensitive == false);
    CHECK(r.timeouts.load_ms == 15000); // 0x3A98
    CHECK(r.load.priority == 0x3e9u);
    CHECK(r.load.stack_size == 0x40u);
    CHECK(r.display_reset_settings == true);
    CHECK(r.file_serving_log_size == 0x1000);
    CHECK(r.reset_mode == target_record::reset_release_mode); // 2
    CHECK(r.reset_boot_value == 0x10u);
    CHECK(r.reset_boot_mask == 0x11u);
}

TEST_CASE("a non-properties file is rejected", "[props_xml]") {
    target_record r;
    QString error;
    CHECK_FALSE(target_properties_from_xml("<html><body/></html>", r, &error));
    CHECK_FALSE(error.isEmpty());
}

TEST_CASE("import only overrides what the file mentions", "[props_xml]") {
    target_record r;
    r.name = QStringLiteral("keep-me");
    r.host = QStringLiteral("192.0.2.75");
    r.port = 1000;

    const QByteArray partial = R"(<Target><ServerSettings HomeDir="D:\x"/></Target>)";

    QString error;
    REQUIRE(target_properties_from_xml(partial, r, &error));

    CHECK(r.name == QStringLiteral("keep-me"));
    CHECK(r.host == QStringLiteral("192.0.2.75"));
    CHECK(r.port == 1000);
    CHECK(r.home_dir == QStringLiteral("D:\\x"));
}
