#pragma once

#include <QString>

#include <cstdint>

namespace opentm::tm_ui {

struct load_options {
    
    QString cmdline;
    QString home_dir;
    bool reset_target        = false;
    bool clear_streams       = true;
    bool enable_debug_module = false;
    bool disable_ppu_debug   = false;
    bool disable_spu_debug   = false;
    bool wait_for_bdvd       = false;
    bool          use_elf_priority = true;
    bool          use_elf_stack    = true;
    std::uint32_t priority         = 0x3e9;
    std::uint32_t stack_size       = 0x40;
    bool enable_extra_options = false;
    bool lv2_exception_handler = false;
    bool remote_play           = false;
    bool gcm_debug             = false;
    bool load_libprof          = false;
    bool core_dump             = false;
    bool remote_play_avc       = false;
    bool smart_image_capture   = false;
    bool memory_access_trap    = false;
    
    std::uint8_t game_attribute = 0;
    bool patch_boot            = false;
    std::uint64_t core_dump_location = 0x2;
    bool rsx_profiling_tool    = false;
    bool high_memory_footprint = false;
    bool gcm_capture_mode      = false;
    bool    paramsfo_mapping        = false;
    bool    paramsfo_use_elf_dir    = false;
    QString paramsfo_path;
    bool    rsx_hud                 = false;
    bool    rsx_hud_start_on        = false;
    bool    trig_disable_ppu_exc    = false;
    bool    trig_disable_spu_exc    = false;
    bool    trig_disable_rsx_exc    = false;
    bool    trig_disable_footswitch = false;
    bool    corefile_disable_memdump = false;
    bool    exec_restart_after_dump  = false;
    bool    exec_dump_fn_after_dump  = false;
    bool    bootable_msg_mapping     = false;
    bool    bootable_msg_use_elf_dir = false;
    QString bootable_msg_dir;
};

} // namespace opentm::tm_ui
