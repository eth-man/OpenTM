//
// req header (16 bytes):
// +0x00 4B cmd (request command id)
// +0x04 4B req_id (echoed back in reply)
// +0x08 4B data_len (size of payload in bytes)
// +0x0C 4B process_id (target PID is zero for whole system queries)
// res header (20 bytes):
// +0x00 4B cmd (0x80000000 | request cmd)
// +0x04 4B req_id (echoed)
// +0x08 4B data_len (size of payload that follows)
// +0x0C 4B process_id
// +0x10 4B result_code (0 = OK)
//

#pragma once

#include "dbgshl_cmd.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace opentm::tm_core::dbgp {

inline constexpr std::size_t request_header_size  = 16;
inline constexpr std::size_t response_header_size = 20;

//framing

struct request {
    std::uint32_t cmd        = 0;
    std::uint32_t req_id     = 0;
    std::uint32_t process_id = 0;
    std::vector<std::byte> payload;
};

struct response {
    std::uint32_t cmd         = 0;
    std::uint32_t req_id      = 0;
    std::uint32_t data_len    = 0;
    std::uint32_t process_id  = 0;
    std::uint32_t result_code = 0;
    std::vector<std::byte> payload; // = exactly data_len bytes
};

std::vector<std::byte> encode_request(const request& r);
std::optional<response> decode_response(std::span<const std::byte> bytes);

std::vector<std::uint32_t> parse_process_list(const response& r);

// res payload:
// +0x00 u32 PPU thread count (n)
// +0x04 u32 SPU thread group count (m)
// +0x08... N x u64 ppu thread ids
//... M x u32 spu thread group ids
struct thread_list {
    std::vector<std::uint64_t> ppu_thread_ids;
    std::vector<std::uint32_t> spu_thread_group_ids;
};
std::optional<thread_list> parse_thread_list(const response& r);

// res payload:
// +0x00 u64 ppu thread id
// +0x08 u32 priority
// +0x0C u32 state (0=IDLE 1=RUNNABLE 2=ONPROC 3=SLEEP 6=STOP 7=ZOMBIE 8=DEAD)
// +0x10 u64 stack addr
// +0x18 u64 stack size
// +0x20 u32 base priority
// +0x24... nul terminated thread name (0 - 128 bytes)
struct ppu_thread_info {
    std::uint64_t thread_id     = 0;
    std::uint32_t priority      = 0;
    std::uint32_t state         = 0;
    std::uint64_t stack_addr    = 0;
    std::uint64_t stack_size    = 0;
    std::uint32_t base_priority = 0;
    std::string   name;
};
std::vector<std::byte> build_ppu_thread_info_request_body(std::uint64_t tid);
std::optional<ppu_thread_info> parse_ppu_thread_info(const response& r);

const char* ppu_thread_state_name(std::uint32_t state) noexcept;

// req payload empty, header PID=<target>.
// res payload
// +0x00 u32 status
// +0x04 u32 ppu thread count
// +0x08 u32 spu thread count
// +0x0C u32 raw spu count
// +0x10 u32 parent pid
// +0x14 u64 max physical memory size
// +0x1C... nul terminated self path (0-512 bytes)
// variable 32B ppu guid
// variable 32B reserved
// variable 4B flags about following info (0x00000001 = valid)
// variable 4B debug flags
// variable 136B debug param

struct process_info {
    std::uint32_t status                 = 0;
    std::uint32_t ppu_thread_count       = 0;
    std::uint32_t spu_thread_count       = 0;
    std::uint32_t raw_spu_count          = 0;
    std::uint32_t parent_pid             = 0;
    std::uint64_t max_physical_mem_size  = 0;
    std::string   self_path;
    std::array<std::uint8_t, 32> guid    = {};
    std::uint32_t flags                  = 0;
    std::uint32_t debug_flags            = 0;
};
std::optional<process_info> parse_process_info_ex2(const response& r);

// req payload empty, header PID=<target>.
// res payload (28 bytes):
// +0x00 u32 created shared memory size
// +0x04 u32 attached shared memory size
// +0x08 u32 process local memory size
// +0x0C u32 process local text size
// +0x10 u32 text size of prx
// +0x14 u32 data size of prx
// +0x18 u32 remaining memory size
struct user_memory_stat {
    std::uint32_t shared_created   = 0;
    std::uint32_t shared_attached  = 0;
    std::uint32_t local_memory     = 0;
    std::uint32_t local_text       = 0;
    std::uint32_t prx_text         = 0;
    std::uint32_t prx_data         = 0;
    std::uint32_t remain_memory    = 0;
};
std::optional<user_memory_stat> parse_user_memory_stat(const response& r);

// req payload: u32 module handle at +0x00.
// res layout
// +0x000 8B sizeof module
// +0x008 30B name (nul pad ascii)
// +0x026 2B ver (high byte = major, low = minor)
// +0x028 4B attr
// +0x02C 4B start entry
// +0x030 4B stop entry
// +0x034 512B program#m filename
// +0x234 4B no of segments
// +0x238... segmenets, each 56 bytes = 7 x u64: base, file_size, mem_size, seg_num, seg_type, attr_flags, align
struct prx_segment {
    std::uint64_t base        = 0;
    std::uint64_t file_size   = 0;
    std::uint64_t mem_size    = 0;
    std::uint64_t segment_num = 0; // 0=text, 1=data/bss, 2&3=rodata
    std::uint64_t segment_type = 0;
    std::uint64_t attr_flags  = 0;
    std::uint64_t align       = 0;
};
struct prx_info {
    std::uint32_t handle      = 0;
    std::uint64_t mem_size    = 0;
    std::uint8_t  version_major = 0;
    std::uint8_t  version_minor = 0;
    std::uint32_t attribute   = 0;
    std::uint32_t start_entry = 0;
    std::uint32_t stop_entry  = 0;
    std::string   name;
    std::string   path;
    std::vector<prx_segment> segments;
};
std::vector<std::byte> build_prx_info_request_body(std::uint32_t handle);
std::optional<prx_info> parse_prx_info(const response& r);

// res: u32 count, then count * u32 handles.
std::vector<std::uint32_t> parse_prx_list(const response& r);

std::vector<std::uint32_t> parse_handle_list(const response& r);
std::vector<std::byte> build_handle_request_body(std::uint32_t handle);

// read_memory (dbgshl 0x80000300): header carries the target process_id (0 for a
// whole-system/lv2 read); body is the address and length. Layout inferred, to be
// confirmed against the first live reply. The reply's payload is the raw bytes read.
std::vector<std::byte> build_read_memory_request_body(std::uint64_t address, std::uint32_t size);
// read_ppu_registers (0x80000400): req body = [ppu_thread_id:u64]. Reply payload = PPU register
// context, big-endian: gpr[32]/cr/xer/lr/ctr/pc/fpr[32]/fpscr/vr[32]/vscr.
std::vector<std::byte> build_read_ppu_registers_request_body(std::uint64_t tid);
// read_spu_registers (0x80000402): req = [spu_thread_id:u64]. Reply = sys_dbg_spu_thread_context2_t,
// big-endian: gpr[128] (16B each), npc, fpscr(16B), srr0, spu_status, spu_cfg, mb_stat, ppu_mb,
// spu_mb[4], decrementer, mfc_cq_sr[96].
std::vector<std::byte> build_read_spu_registers_request_body(std::uint64_t tid);
// read_spu_local_store (0x80000302): req = [spu_thread_id:u64][offset:u32][size:u32] (INFERRED,
// verify). Reply payload = raw LS bytes.
std::vector<std::byte> build_read_spu_local_store_request_body(std::uint64_t tid, std::uint32_t offset, std::uint32_t size);

// DABR hardware data watchpoint. Backs sys_dbg_set_address_to_dabr(addr, ctrl) /
// sys_dbg_get_address_from_dabr (sys/dbg.h). One DABR, one address. ctrl values
// below (SYS_DBG_DABR_CTRL_*). This ABI is DOCUMENTED, not inferred.
namespace dabr_ctrl {
inline constexpr std::uint64_t clear = 0x0; // remove the watchpoint
inline constexpr std::uint64_t read  = 0x5; // break on a LOAD from addr
inline constexpr std::uint64_t write = 0x6; // break on a STORE to addr
}
// set_data_watchpoint (dbgshl 0x8020000a): req body = [addr:u64][ctrl:u64].
std::vector<std::byte> build_set_data_watchpoint_request_body(std::uint64_t addr, std::uint64_t ctrl);
// get_data_watchpoint (dbgshl 0x80200009): no request body. Reply payload = [addr:u64][ctrl:u64].
struct dabr_state { std::uint64_t addr = 0; std::uint64_t ctrl = 0; };
std::optional<dabr_state> parse_data_watchpoint(const response& r);

// PPU/SPU code breakpoints. NOTE: there is NO lv2dbg syscall for these (sys/dbg.h has
// none) -- they are agent-layer (dbgshl) commands. Sony's own ProDG sample calls
// PS3AddBreakPoint(processID, threadID, address, 1) / PS3RemoveBreakPoint(processID,
// threadID, address), so the process_id rides the dbgp header and the body carries the
// thread id + address. The exact wire body is therefore INFERRED from that API shape
// (and the read_ppu_registers precedent), to be confirmed against the first live reply.
// set_ppu_breakpoint 0x80000502 / clear 0x80000503 / set_spu 0x80000504 / clear_spu 0x80000505.
std::vector<std::byte> build_breakpoint_request_body(std::uint64_t tid, std::uint64_t address);

struct mutex_info {
    std::uint32_t handle             = 0;
    std::uint32_t attr_protocol      = 0; // 1=FIFO, 2=PRIORITY, etc
    std::uint32_t attr_recursive     = 0; // 0x10=RECURSIVE 0x20=NOT_RECURSIVE
    std::uint32_t attr_shared        = 0;
    std::uint32_t attr_adaptive      = 0;
    std::uint64_t key                = 0;
    std::uint32_t flags              = 0;
    std::string   name; // 8 bytes per spec but ascii / nul pad
    std::uint64_t owner_thread_id    = 0;
    std::uint32_t lock_counter       = 0;
    std::uint32_t cond_ref_counter   = 0;
    std::uint32_t cond_var_id        = 0;
    std::uint32_t wait_thread_count  = 0;
    std::uint32_t all_wait_count     = 0;
    std::vector<std::uint64_t> wait_thread_ids;
};
std::optional<mutex_info> parse_mutex_info(const response& r);

struct lwmutex_info {
    std::uint32_t handle          = 0;
    std::uint32_t attr_protocol   = 0;
    std::uint32_t attr_recursive  = 0;
    std::string   name;
    std::uint32_t owner_thread_id = 0;
    std::uint32_t lock_counter    = 0;
    std::vector<std::uint32_t> wait_thread_ids;
};
std::optional<lwmutex_info> parse_lwmutex_info(const response& r);

struct cond_info {
    std::uint32_t handle       = 0;
    std::uint32_t attr_shared  = 0;
    std::uint64_t key          = 0;
    std::uint32_t flags        = 0;
    std::string   name;
    std::uint32_t mutex_id     = 0;
    std::vector<std::uint64_t> wait_thread_ids;
};
std::optional<cond_info> parse_cond_info(const response& r);

struct event_queue_info {
    std::uint32_t handle        = 0;
    std::uint32_t attr_protocol = 0;
    std::uint32_t type          = 0; // SYS_PPU_QUEUE / SYS_SPU_QUEUE
    std::uint64_t key           = 0;
    std::uint32_t queue_size    = 0;
    std::string   name;
    std::uint32_t queued_count  = 0;
    std::vector<std::uint64_t> wait_thread_ids;
};
std::optional<event_queue_info> parse_event_queue_info(const response& r);

struct container_info {
    std::uint32_t id        = 0;
    std::uint32_t parent_id = 0; // 0xFFFFFFFF / sentinel = none
    std::uint32_t total     = 0; // bytes
    std::uint32_t available = 0;
};
std::optional<std::vector<container_info>> parse_container_info(const response& r);

} // namespace opentm::tm_core::dbgp
