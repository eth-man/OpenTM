#include "kernel_explorer_controller.h"

#include "session_controller.h"

#include <tm_core/tcp_connection.h>

#include <QStringLiteral>

namespace opentm::tm_ui {

namespace {

constexpr std::uint16_t kCatDbgp     = 0x0200;
constexpr std::uint32_t kEnvelopeSb  = 0x02100000;

} // namespace

kernel_explorer_controller::kernel_explorer_controller(
    opentm::tm_core::tcp_connection* conn,
    session_controller* session,
    QObject* parent)
    : QObject(parent), connection_(conn), session_(session)
{}

kernel_explorer_controller::~kernel_explorer_controller() = default;

std::uint32_t kernel_explorer_controller::send_dbgp(
    std::uint32_t cmd, std::uint32_t pid,
    const std::vector<std::byte>& body, std::uint32_t handle)
{
    using namespace opentm::tm_core;
    if (!connection_ || !session_) return 0;

    const auto seq = session_->next_dbgshl_seq();
    pending_[seq] = pending{cmd, pid, handle};

    
    dbgp::request req;
    req.cmd        = cmd & ~dbgshl::reply_bit; 
    req.req_id     = seq;
    req.process_id = pid;
    req.payload    = body;
    const auto bytes = dbgp::encode_request(req);

    deci3_frame f;
    f.direction = deci3_direction::host_to_target;
    f.category  = kCatDbgp;
    
    
    f.session_b = session_->dbgshl_sb();
    f.payload.assign(bytes.begin(), bytes.end());

    if (!connection_->send_frame(f)) {
        pending_.remove(seq);
        return 0;
    }
    return seq;
}

void kernel_explorer_controller::refresh_process_list() {
    
    process_state_.summaries.clear();
    process_state_.remaining_pids.clear();
    process_state_.got_list = false;

    const auto seq = send_dbgp(opentm::tm_core::dbgshl::cmd::get_process_list, 0, {});
    if (seq == 0) {
        emit log_message(QStringLiteral("    !! kernel_explorer: process_list send failed"));
        return;
    }
    emit log_message(QStringLiteral("    <- KE: GET_PROCESS_LIST seq=0x%1").arg(seq, 8, 16, QChar('0')));
}

void kernel_explorer_controller::attach_process(std::uint32_t pid) {
    const auto seq = send_dbgp(opentm::tm_core::dbgshl::cmd::attach_process, pid, {});
    if (seq == 0) {
        emit log_message(QStringLiteral("    !! KE: attach_process send failed"));
        return;
    }
    emit log_message(QStringLiteral("    <- KE: ATTACH_PROCESS pid=0x%1 seq=0x%2")
        .arg(pid, 8, 16, QChar('0')).arg(seq, 8, 16, QChar('0')));
}

void kernel_explorer_controller::read_memory(std::uint32_t pid, std::uint64_t addr, std::uint32_t size) {
    auto body = opentm::tm_core::dbgp::build_read_memory_request_body(addr, size);
    const auto seq = send_dbgp(opentm::tm_core::dbgshl::cmd::read_memory, pid, body);
    if (seq == 0) {
        emit log_message(QStringLiteral("    !! KE: read_memory send failed"));
        return;
    }
    read_addr_[seq] = addr;
    emit log_message(QStringLiteral("    <- KE: READ_MEMORY pid=0x%1 addr=0x%2 size=0x%3 seq=0x%4")
        .arg(pid, 8, 16, QChar('0')).arg(addr, 16, 16, QChar('0')).arg(size, 0, 16).arg(seq, 8, 16, QChar('0')));
}

void kernel_explorer_controller::read_ppu_registers(std::uint32_t pid, std::uint64_t tid) {
    auto body = opentm::tm_core::dbgp::build_read_ppu_registers_request_body(tid);
    const auto seq = send_dbgp(opentm::tm_core::dbgshl::cmd::read_ppu_registers, pid, body);
    if (seq == 0) {
        emit log_message(QStringLiteral("    !! KE: read_ppu_registers send failed"));
        return;
    }
    read_tid_[seq] = tid;
    emit log_message(QStringLiteral("    <- KE: READ_PPU_REGISTERS pid=0x%1 tid=0x%2 seq=0x%3")
        .arg(pid, 8, 16, QChar('0')).arg(tid, 16, 16, QChar('0')).arg(seq, 8, 16, QChar('0')));
}

void kernel_explorer_controller::read_spu_registers(std::uint32_t pid, std::uint64_t tid) {
    auto body = opentm::tm_core::dbgp::build_read_spu_registers_request_body(tid);
    const auto seq = send_dbgp(opentm::tm_core::dbgshl::cmd::read_spu_registers, pid, body);
    if (seq == 0) {
        emit log_message(QStringLiteral("    !! KE: read_spu_registers send failed"));
        return;
    }
    read_tid_[seq] = tid;
    emit log_message(QStringLiteral("    <- KE: READ_SPU_REGISTERS pid=0x%1 tid=0x%2 seq=0x%3")
        .arg(pid, 8, 16, QChar('0')).arg(tid, 16, 16, QChar('0')).arg(seq, 8, 16, QChar('0')));
}

void kernel_explorer_controller::read_spu_local_store(std::uint32_t pid, std::uint64_t tid, std::uint32_t offset, std::uint32_t size) {
    auto body = opentm::tm_core::dbgp::build_read_spu_local_store_request_body(tid, offset, size);
    const auto seq = send_dbgp(opentm::tm_core::dbgshl::cmd::read_spu_local_store, pid, body);
    if (seq == 0) {
        emit log_message(QStringLiteral("    !! KE: read_spu_local_store send failed"));
        return;
    }
    read_addr_[seq] = offset;
    emit log_message(QStringLiteral("    <- KE: READ_SPU_LS pid=0x%1 tid=0x%2 off=0x%3 size=0x%4 seq=0x%5")
        .arg(pid, 8, 16, QChar('0')).arg(tid, 16, 16, QChar('0')).arg(offset, 0, 16).arg(size, 0, 16).arg(seq, 8, 16, QChar('0')));
}

void kernel_explorer_controller::set_data_watchpoint(std::uint32_t pid, std::uint64_t addr, std::uint64_t ctrl) {
    auto body = opentm::tm_core::dbgp::build_set_data_watchpoint_request_body(addr, ctrl);
    const auto seq = send_dbgp(opentm::tm_core::dbgshl::cmd::set_data_watchpoint, pid, body);
    if (seq == 0) { emit log_message(QStringLiteral("    !! KE: set_data_watchpoint send failed")); return; }
    emit log_message(QStringLiteral("    <- KE: SET_DATA_WATCHPOINT pid=0x%1 addr=0x%2 ctrl=0x%3 seq=0x%4")
        .arg(pid, 8, 16, QChar('0')).arg(addr, 16, 16, QChar('0')).arg(ctrl, 0, 16).arg(seq, 8, 16, QChar('0')));
}

void kernel_explorer_controller::get_data_watchpoint(std::uint32_t pid) {
    const auto seq = send_dbgp(opentm::tm_core::dbgshl::cmd::get_data_watchpoint, pid, {});
    if (seq == 0) { emit log_message(QStringLiteral("    !! KE: get_data_watchpoint send failed")); return; }
    emit log_message(QStringLiteral("    <- KE: GET_DATA_WATCHPOINT pid=0x%1 seq=0x%2")
        .arg(pid, 8, 16, QChar('0')).arg(seq, 8, 16, QChar('0')));
}

void kernel_explorer_controller::set_breakpoint(std::uint32_t pid, std::uint64_t tid, std::uint64_t addr, bool spu) {
    auto body = opentm::tm_core::dbgp::build_breakpoint_request_body(tid, addr);
    const auto cmd = spu ? opentm::tm_core::dbgshl::cmd::set_spu_breakpoint
                         : opentm::tm_core::dbgshl::cmd::set_ppu_breakpoint;
    const auto seq = send_dbgp(cmd, pid, body);
    if (seq == 0) { emit log_message(QStringLiteral("    !! KE: set_breakpoint send failed")); return; }
    op_addr_[seq] = addr;
    emit log_message(QStringLiteral("    <- KE: SET_%1_BREAKPOINT pid=0x%2 tid=0x%3 addr=0x%4 seq=0x%5")
        .arg(spu ? "SPU" : "PPU").arg(pid, 8, 16, QChar('0')).arg(tid, 16, 16, QChar('0'))
        .arg(addr, 16, 16, QChar('0')).arg(seq, 8, 16, QChar('0')));
}

void kernel_explorer_controller::clear_breakpoint(std::uint32_t pid, std::uint64_t tid, std::uint64_t addr, bool spu) {
    auto body = opentm::tm_core::dbgp::build_breakpoint_request_body(tid, addr);
    const auto cmd = spu ? opentm::tm_core::dbgshl::cmd::clear_spu_breakpoint
                         : opentm::tm_core::dbgshl::cmd::clear_ppu_breakpoint;
    const auto seq = send_dbgp(cmd, pid, body);
    if (seq == 0) { emit log_message(QStringLiteral("    !! KE: clear_breakpoint send failed")); return; }
    op_addr_[seq] = addr;
    emit log_message(QStringLiteral("    <- KE: CLEAR_%1_BREAKPOINT pid=0x%2 tid=0x%3 addr=0x%4 seq=0x%5")
        .arg(spu ? "SPU" : "PPU").arg(pid, 8, 16, QChar('0')).arg(tid, 16, 16, QChar('0'))
        .arg(addr, 16, 16, QChar('0')).arg(seq, 8, 16, QChar('0')));
}

void kernel_explorer_controller::list_breakpoints(std::uint32_t pid, bool spu) {
    const auto cmd = spu ? opentm::tm_core::dbgshl::cmd::list_spu_breakpoints
                         : opentm::tm_core::dbgshl::cmd::list_ppu_breakpoints;
    const auto seq = send_dbgp(cmd, pid, {});
    if (seq == 0) { emit log_message(QStringLiteral("    !! KE: list_breakpoints send failed")); return; }
    emit log_message(QStringLiteral("    <- KE: LIST_%1_BREAKPOINTS pid=0x%2 seq=0x%3")
        .arg(spu ? "SPU" : "PPU").arg(pid, 8, 16, QChar('0')).arg(seq, 8, 16, QChar('0')));
}

void kernel_explorer_controller::refresh_threads(std::uint32_t pid) {
    threads_state_.pid = pid;
    threads_state_.expected = 0;
    threads_state_.info.clear();
    send_dbgp(opentm::tm_core::dbgshl::cmd::get_thread_list, pid, {});
}

void kernel_explorer_controller::refresh_modules(std::uint32_t pid) {
    modules_state_.pid = pid;
    modules_state_.expected = 0;
    modules_state_.received = 0;
    modules_state_.info.clear();
    send_dbgp(opentm::tm_core::dbgshl::cmd::get_module_list, pid, {});
}

template <class Info>
void kernel_explorer_controller::refresh_sync(sync_accum<Info>& st, std::uint32_t pid, std::uint32_t list_cmd) {
    st.pid = pid;
    st.handles_remaining.clear();
    st.info.clear();
    send_dbgp(list_cmd, pid, {});
}

template <class Info>
void kernel_explorer_controller::on_sync_info(sync_accum<Info>& st, std::optional<Info> parsed, std::uint32_t handle, void (kernel_explorer_controller::*ready)(std::uint32_t, QList<Info>)) {
    if (parsed) st.info.push_back(*parsed);
    st.handles_remaining.removeAll(handle);
    if (st.handles_remaining.isEmpty()) {
        emit (this->*ready)(st.pid, st.info);
    }
}

void kernel_explorer_controller::refresh_mutexes(std::uint32_t pid) {
    refresh_sync(mutexes_state_, pid, opentm::tm_core::dbgshl::cmd::get_mutex_list);
}

void kernel_explorer_controller::refresh_lwmutexes(std::uint32_t pid) {
    refresh_sync(lwmutexes_state_, pid, opentm::tm_core::dbgshl::cmd::get_lwmutex_list);
}

void kernel_explorer_controller::refresh_conds(std::uint32_t pid) {
    refresh_sync(conds_state_, pid, opentm::tm_core::dbgshl::cmd::get_cond_list);
}

void kernel_explorer_controller::refresh_event_queues(std::uint32_t pid) {
    refresh_sync(evq_state_, pid, opentm::tm_core::dbgshl::cmd::get_event_queue_list);
}

void kernel_explorer_controller::refresh_containers(std::uint32_t pid) {
    send_dbgp(opentm::tm_core::dbgshl::cmd::get_container_info, pid, {});
}

void kernel_explorer_controller::trigger_core_dump(std::uint32_t pid) {
    if (pid == 0) return;
    send_dbgp(opentm::tm_core::dbgshl::cmd::trigger_core_dump, pid, {});
    emit log_message(QStringLiteral("    <- KE: TRIGGER_CORE_DUMP pid=0x%1").arg(pid, 8, 16, QChar('0')));
}

void kernel_explorer_controller::resume_process(std::uint32_t pid) {
    if (pid == 0) return;
    send_dbgp(opentm::tm_core::dbgshl::cmd::continue_process, pid, {});
    emit log_message(QStringLiteral("    <- KE: CONTINUE_PROCESS pid=0x%1").arg(pid, 8, 16, QChar('0')));
}

void kernel_explorer_controller::pause_process(std::uint32_t pid) {
    if (pid == 0) return;
    send_dbgp(opentm::tm_core::dbgshl::cmd::stop_process, pid, {});
    emit log_message(QStringLiteral("    <- KE: STOP_PROCESS pid=0x%1").arg(pid, 8, 16, QChar('0')));
}

void kernel_explorer_controller::terminate_process(std::uint32_t pid) {
    if (pid == 0) return;
    send_dbgp(opentm::tm_core::dbgshl::cmd::terminate_game_process, pid, {});
    emit log_message(QStringLiteral("    <- KE: TERMINATE_GAME_PROCESS pid=0x%1").arg(pid, 8, 16, QChar('0')));
}

void kernel_explorer_controller::on_frame_received(opentm::tm_core::deci3_frame f) {
    using namespace opentm::tm_core;
    if (f.category != kCatDbgp) return;
    if (f.session_b != kEnvelopeSb) return;
    if (f.payload.size() < dbgp::response_header_size) return;
    
    if ((std::to_integer<std::uint8_t>(f.payload[0]) & 0x80) == 0) return;

    const std::span<const std::byte> view(f.payload.data(), f.payload.size());
    const auto r = dbgp::decode_response(view);
    if (!r) return;

    const auto it = pending_.find(r->req_id);
    if (it == pending_.end()) {
        return;
    }
    const auto p = it.value();
    pending_.erase(it);

    using namespace opentm::tm_core::dbgshl;
    switch (p.cmd) {
    case cmd::get_process_list:           on_process_list_reply(*r); break;
    case cmd::attach_process:
        if (r->result_code != 0) {
            emit log_message(QStringLiteral("    !! KE: attach_process pid=0x%1 refused, result_code=0x%2")
                .arg(r->process_id, 8, 16, QChar('0')).arg(r->result_code, 8, 16, QChar('0')));
        } else {
            emit log_message(QStringLiteral("    -- KE: attach_process pid=0x%1 OK - now registered for debugging")
                .arg(r->process_id, 8, 16, QChar('0')));
        }
        break;
    case cmd::read_memory: {
        const auto addr = read_addr_.take(r->req_id);
        if (r->result_code != 0) {
            emit memory_read_failed(addr, r->result_code);
        } else {
            QByteArray data(reinterpret_cast<const char*>(r->payload.data()), static_cast<int>(r->payload.size()));
            emit memory_read_ready(addr, data);
        }
        break;
    }
    case cmd::read_ppu_registers: {
        const auto tid = read_tid_.take(r->req_id);
        if (r->result_code != 0) {
            emit ppu_regs_failed(tid, r->result_code);
        } else {
            QByteArray data(reinterpret_cast<const char*>(r->payload.data()), static_cast<int>(r->payload.size()));
            emit ppu_regs_ready(tid, data);
        }
        break;
    }
    case cmd::read_spu_registers: {
        const auto tid = read_tid_.take(r->req_id);
        if (r->result_code != 0) {
            emit spu_regs_failed(tid, r->result_code);
        } else {
            QByteArray data(reinterpret_cast<const char*>(r->payload.data()), static_cast<int>(r->payload.size()));
            emit spu_regs_ready(tid, data);
        }
        break;
    }
    case cmd::read_spu_local_store: {
        const auto off = read_addr_.take(r->req_id);
        if (r->result_code != 0) {
            emit spu_ls_failed(off, r->result_code);
        } else {
            QByteArray data(reinterpret_cast<const char*>(r->payload.data()), static_cast<int>(r->payload.size()));
            emit spu_ls_ready(off, data);
        }
        break;
    }
    case cmd::set_data_watchpoint:
        emit watchpoint_set_done(r->result_code);
        break;
    case cmd::get_data_watchpoint: {
        if (r->result_code != 0) { emit watchpoint_info(0, 0, r->result_code); break; }
        const auto s = dbgp::parse_data_watchpoint(*r);
        if (s) emit watchpoint_info(s->addr, s->ctrl, 0);
        else   emit watchpoint_info(0, 0, 0xffffffffu);
        break;
    }
    case cmd::set_ppu_breakpoint:
    case cmd::clear_ppu_breakpoint:
    case cmd::set_spu_breakpoint:
    case cmd::clear_spu_breakpoint:
        emit breakpoint_done(op_addr_.take(r->req_id), r->result_code);
        break;
    case cmd::list_ppu_breakpoints:
    case cmd::list_spu_breakpoints: {
        QByteArray raw(reinterpret_cast<const char*>(r->payload.data()), static_cast<int>(r->payload.size()));
        emit breakpoints_listed(raw, r->result_code);
        break;
    }
    case cmd::get_process_info:  on_process_info_reply(*r, p.pid); break;
    case cmd::get_user_memory_stat: on_user_memory_stat_reply(*r, p.pid); break;
    case cmd::get_thread_list:            on_thread_list_reply(*r, p.pid); break;
    case cmd::get_ppu_thread_info:
        on_thread_info_reply(*r, p.pid, p.handle);
        break;
    case cmd::get_module_list: {
        const auto v = dbgp::parse_handle_list(*r);
        modules_state_.pid = p.pid;
        modules_state_.info.clear();
        modules_state_.expected = static_cast<int>(v.size());
        modules_state_.received = 0;
        if (modules_state_.expected == 0) {
            emit modules_ready(p.pid, {});
            break;
        }
        
        for (const auto h : v) {
            auto body = dbgp::build_prx_info_request_body(h);
            send_dbgp(cmd::get_module_info, p.pid, body, h);
        }
        break;
    }
    case cmd::get_module_info: {
        auto info = dbgp::parse_prx_info(*r);
        if (info) {
            info->handle = p.handle;
            modules_state_.info.push_back(*info);
        }
        ++modules_state_.received;
        if (modules_state_.received >= modules_state_.expected) {
            emit modules_ready(modules_state_.pid, modules_state_.info);
        }
        break;
    }
    case cmd::get_mutex_list:
        on_info_list_reply(*r, p.pid,
            cmd::get_mutex_info,
            mutexes_state_.handles_remaining, mutexes_state_.info);
        break;
    case cmd::get_mutex_info:
        on_sync_info(mutexes_state_, dbgp::parse_mutex_info(*r), p.handle, &kernel_explorer_controller::mutexes_ready);
        break;
    case cmd::get_lwmutex_list:
        on_info_list_reply(*r, p.pid,
            cmd::get_lwmutex_info,
            lwmutexes_state_.handles_remaining, lwmutexes_state_.info);
        break;
    case cmd::get_lwmutex_info:
        on_sync_info(lwmutexes_state_, dbgp::parse_lwmutex_info(*r), p.handle, &kernel_explorer_controller::lwmutexes_ready);
        break;
    case cmd::get_cond_list:
        on_info_list_reply(*r, p.pid,
            cmd::get_cond_info,
            conds_state_.handles_remaining, conds_state_.info);
        break;
    case cmd::get_cond_info:
        on_sync_info(conds_state_, dbgp::parse_cond_info(*r), p.handle, &kernel_explorer_controller::conds_ready);
        break;
    case cmd::get_event_queue_list:
        on_info_list_reply(*r, p.pid,
            cmd::get_event_queue_info,
            evq_state_.handles_remaining, evq_state_.info);
        break;
    case cmd::get_event_queue_info:
        on_sync_info(evq_state_, dbgp::parse_event_queue_info(*r), p.handle, &kernel_explorer_controller::event_queues_ready);
        break;
    case cmd::get_container_info:
        if (auto cs = dbgp::parse_container_info(*r)) {
            QList<dbgp::container_info> out;
            for (const auto& c : *cs) out.push_back(c);
            emit containers_ready(p.pid, out);
        }
        break;
    default:
        
        break;
    }
}

void kernel_explorer_controller::on_process_list_reply(
    const opentm::tm_core::dbgp::response& r)
{
    const auto pids = opentm::tm_core::dbgp::parse_process_list(r);
    process_state_.got_list = true;
    if (pids.empty()) {
        emit log_message(QStringLiteral("    -- KE: no running processes"));
        emit process_list_ready({});
        return;
    }
    emit log_message(QStringLiteral("    -- KE: %1 process(es) - fetching info").arg(pids.size()));
    for (const auto pid : pids) {
        process_summary entry;
        entry.pid = pid;
        process_state_.summaries.push_back(entry);
        process_state_.remaining_pids.push_back(pid);

        using namespace opentm::tm_core::dbgshl;
        send_dbgp(cmd::get_process_info,    pid, {});
        send_dbgp(cmd::get_user_memory_stat, pid, {});
    }
}

void kernel_explorer_controller::on_process_info_reply(
    const opentm::tm_core::dbgp::response& r, std::uint32_t pid)
{
    auto info = opentm::tm_core::dbgp::parse_process_info_ex2(r);
    if (!info) return;
    for (auto& s : process_state_.summaries) {
        if (s.pid == pid) { s.info = *info; break; }
    }
    maybe_emit_process_list();
}

void kernel_explorer_controller::on_user_memory_stat_reply(
    const opentm::tm_core::dbgp::response& r, std::uint32_t pid)
{
    if (auto stat = opentm::tm_core::dbgp::parse_user_memory_stat(r)) {
        for (auto& s : process_state_.summaries) {
            if (s.pid == pid) { s.memory = *stat; break; }
        }
    }
    
    process_state_.remaining_pids.removeAll(pid);
    maybe_emit_process_list();
}

void kernel_explorer_controller::maybe_emit_process_list() {
    if (!process_state_.got_list) return;
    if (!process_state_.remaining_pids.isEmpty()) return;
    emit process_list_ready(process_state_.summaries);
}

void kernel_explorer_controller::on_thread_list_reply(
    const opentm::tm_core::dbgp::response& r, std::uint32_t pid)
{
    const auto tl = opentm::tm_core::dbgp::parse_thread_list(r);
    if (!tl) {
        emit threads_ready(pid, {});
        return;
    }
    threads_state_.pid = pid;
    threads_state_.info.clear();
    threads_state_.expected = static_cast<int>(tl->ppu_thread_ids.size());
    threads_state_.received = 0;
    if (threads_state_.expected == 0) {
        emit threads_ready(pid, {});
        return;
    }
    for (const auto tid : tl->ppu_thread_ids) {
        auto body = opentm::tm_core::dbgp::build_ppu_thread_info_request_body(tid);
        send_dbgp(opentm::tm_core::dbgshl::cmd::get_ppu_thread_info, pid, body, static_cast<std::uint32_t>(tid & 0xffffffffu));
    }
}

void kernel_explorer_controller::on_thread_info_reply(
    const opentm::tm_core::dbgp::response& r,
    std::uint32_t pid, std::uint64_t )
{
    auto info = opentm::tm_core::dbgp::parse_ppu_thread_info(r);
    if (info) threads_state_.info.push_back(*info);
    ++threads_state_.received;
    
    if (threads_state_.received >= threads_state_.expected) {
        emit threads_ready(pid, threads_state_.info);
    }
}

template <class Info>
void kernel_explorer_controller::on_info_list_reply(
    const opentm::tm_core::dbgp::response& r,
    std::uint32_t pid, std::uint32_t info_cmd,
    QList<std::uint32_t>& expected_set, QList<Info>& accumulator)
{
    const auto handles = opentm::tm_core::dbgp::parse_handle_list(r);
    accumulator.clear();
    expected_set.clear();
    if (handles.empty()) {
        return;
    }
    for (const auto h : handles) expected_set.push_back(h);
    for (const auto h : handles) {
        auto body = opentm::tm_core::dbgp::build_handle_request_body(h);
        send_dbgp(info_cmd, pid, body, h);
    }
}

} // namespace opentm::tm_ui
