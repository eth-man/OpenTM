#include "target_actions.h"

#include "session_controller.h"
#include "tsmp_helpers.h"

#include <tm_core/be_io.h>
#include <tm_core/dbgshl_cmd.h>
#include <tm_core/deci3_codec.h>
#include <tm_core/tsmp_codec.h>
#include <tm_core/wol.h>

#include <QByteArray>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QHostAddress>
#include <QList>
#include <QRegularExpression>
#include <QStringLiteral>

namespace opentm::tm_ui {

namespace {

QByteArray reset_lpar_payload() {
    QByteArray b;
    b.append("PS3_LPAR", 8);
    b.append(QByteArray(8, '\0'));
    return b;
}

QByteArray reset_payload(std::uint64_t boot_value, std::uint64_t boot_mask) {
    QByteArray b = reset_lpar_payload();
    opentm::tm_core::append_be_u64(b, boot_value);
    opentm::tm_core::append_be_u64(b, boot_mask);
    return b;
}

} // namespace

target_actions::target_actions(opentm::tm_core::tcp_connection* conn, session_controller* sess, QObject* parent) : QObject(parent), connection_(conn), session_(sess) {
    connect(sess, &session_controller::debug_agent_ready, this, &target_actions::on_debug_agent_ready);
    pending_load_timeout_.setSingleShot(true);
    pending_load_timeout_.setInterval(120000);
    connect(&pending_load_timeout_, &QTimer::timeout, this, [this] {
        drop_pending_load(tr("the target did not come back within 120s"));
    });
}

target_actions::~target_actions() = default;

void target_actions::set_target(const target_record& r) {
    target_      = r;
    have_target_ = true;
}

void target_actions::clear_target() {
    target_      = target_record{};
    have_target_ = false;
    drop_pending_load(tr("the target was cleared"));
}

void target_actions::drop_pending_load(const QString& why) {
    if (!pending_load_) return;
    const auto path = pending_load_->path;
    pending_load_.reset();
    pending_load_timeout_.stop();
    emit log_message(tr("    !! Load Executable: gave up on %1 - %2").arg(path, why));
    emit status_message(tr("Load cancelled: %1").arg(why));
}

void target_actions::on_debug_agent_ready() {
    if (!pending_load_) return;
    const auto p = *pending_load_;
    pending_load_.reset();
    pending_load_timeout_.stop();
    emit log_message(tr("    -- target is back: resuming load of %1").arg(p.path));
    send_load_executable(p.path, p.opts);
}

bool target_actions::require_ready(const QString& action_label) {
    if (!have_target_) {
        emit status_message(tr("%1: no target selected").arg(action_label));
        return false;
    }
    if (connection_->current_state() != opentm::tm_core::tcp_connection::state::ready) {
        emit status_message(tr("%1: connect first").arg(action_label));
        return false;
    }
    if (!session_->is_ready()) {
        emit status_message(tr("%1: session not ready yet").arg(action_label));
        return false;
    }
    return true;
}

void target_actions::send_session_tsmp(std::uint16_t cmd, const QByteArray& extra, const QString& tag) {
    auto f = tsmp_frame_from_inner(tsmp_inner(cmd, session_->sub_token(), extra));
    f.session_b = static_cast<std::uint32_t>(session_->session_token()) << 16;
    if (session_->is_cfw_dex()) tsmp_retarget_for_dex(f);
    if (connection_->send_frame(f)) {
        emit log_message(tr("    <- %1 (tsmp cmd=0x%2, sub=0x%3, body=%4B)").arg(tag).arg(cmd, 4, 16, QChar('0')).arg(session_->sub_token(), 4, 16, QChar('0')).arg(extra.size()));
    }
}

void target_actions::power_on() {
    if (!require_ready(tr("Power On"))) return;
    send_session_tsmp(opentm::tm_core::tsmp_cmd::power_on, {}, tr("power on"));
}

void target_actions::power_off() {
    if (!require_ready(tr("Power Off"))) return;
    send_session_tsmp(opentm::tm_core::tsmp_cmd::shutdown, {}, tr("shutdown"));
}

void target_actions::power_off_force() {
    if (!require_ready(tr("Power Off (Force)"))) return;
    send_session_tsmp(opentm::tm_core::tsmp_cmd::power_off, {}, tr("power off (forced terminate)"));
}

void target_actions::reset_debug() {
    if (!require_ready(tr("Reset"))) return;
    send_reset_sequence(0x10, 0x11, tr("Debug Mode"));
}

void target_actions::reset_ssm() {
    if (!require_ready(tr("Reset"))) return;
    send_reset_sequence(0x11, 0x11, tr("SSM"));
}

void target_actions::reset_current() {
    if (!require_ready(tr("Reset"))) return;
    switch (target_.reset_mode) {
    case target_record::reset_ssm_mode:
        send_reset_sequence(0x11, 0x11, tr("System Software Mode"));
        break;
    case target_record::reset_release_mode:
        send_reset_sequence(0x01, 0x11, tr("Release Mode"));
        break;
    case target_record::reset_advanced:
        send_reset_sequence(target_.reset_boot_value, target_.reset_boot_mask, tr("Advanced (boot=0x%1/0x%2)").arg(target_.reset_boot_value, 0, 16).arg(target_.reset_boot_mask, 0, 16));
        break;
    case target_record::reset_debug_mode:
    default:
        send_reset_sequence(0x10, 0x11, tr("Debug Mode"));
        break;
    }
}

void target_actions::send_reset_sequence(std::uint64_t boot_value, std::uint64_t boot_mask, const QString& label) {
    using namespace opentm::tm_core;
    send_session_tsmp(tsmp_cmd::set_boot_param, reset_payload(boot_value, boot_mask), tr("reset %1 - set boot param (boot=0x%2)").arg(label).arg(boot_value, 0, 16));
    send_session_tsmp(tsmp_cmd::get_boot_param, reset_lpar_payload(), tr("reset %1 - read back boot param").arg(label));
    send_session_tsmp(tsmp_cmd::get_sys_param, QByteArray(), tr("reset %1 - read back system param").arg(label));
    send_session_tsmp(tsmp_cmd::reboot, QByteArray(), tr("reset %1 - reboot").arg(label));
}

void target_actions::request_lpar_status() {
    if (!require_ready(tr("LPAR Status"))) return;
    send_session_tsmp(opentm::tm_core::tsmp_cmd::lpar_status, QByteArray(), tr("LPAR status"));
}

void target_actions::request_current_boot_param() {
    if (!require_ready(tr("Boot Parameters"))) return;
    send_session_tsmp(opentm::tm_core::tsmp_cmd::get_cur_param, reset_lpar_payload(), tr("read boot param in effect"));
}

void target_actions::wake_on_lan() {
    if (!have_target_) {
        emit status_message(tr("Wake on LAN: no target selected"));
        return;
    }
    if (target_.mac.isEmpty()) {
        emit status_message(tr("Wake on LAN: target has no MAC set (Edit Target -> MAC)"));
        emit log_message(tr("    !! WoL aborted - no MAC set for target %1").arg(target_.name));
        return;
    }
    const auto mac = opentm::tm_core::parse_mac(target_.mac.toStdString());
    if (!mac) {
        emit status_message(tr("Wake on LAN: malformed MAC \"%1\"").arg(target_.mac));
        emit log_message(tr("    !! WoL aborted - malformed MAC \"%1\"").arg(target_.mac));
        return;
    }

    emit log_message(tr("    <- Wake-on-LAN magic packet for %1 (UDP :1000)").arg(target_.mac));
    int sent = 0;

    if (opentm::tm_core::send_wol(*mac)) {
        emit log_message(QStringLiteral("    >> WoL: 102 bytes -> 255.255.255.255:1000"));
        ++sent;
    } else {
        emit log_message(QStringLiteral("    !! WoL: limited-broadcast send failed"));
    }

    QHostAddress host_addr;
    if (host_addr.setAddress(target_.host)
        && host_addr.protocol() == QAbstractSocket::IPv4Protocol)
    {
        const auto ip = host_addr.toIPv4Address();
        const auto subnet_bcast = QHostAddress((ip & 0xffffff00u) | 0xffu);
        if (opentm::tm_core::send_wol(*mac, subnet_bcast)) {
            emit log_message(tr("    >> WoL: 102 bytes -> %1:1000 (subnet broadcast)").arg(subnet_bcast.toString()));
            ++sent;
        } else {
            emit log_message(tr("    !! WoL: subnet-broadcast send to %1 failed").arg(subnet_bcast.toString()));
        }
        if (opentm::tm_core::send_wol(*mac, host_addr)) {
            emit log_message(tr("    >> WoL: 102 bytes -> %1:1000 (unicast)").arg(target_.host));
            ++sent;
        } else {
            emit log_message(tr("    !! WoL: unicast send to %1 failed").arg(target_.host));
        }
    }

    if (sent > 0) {
        emit status_message(tr("Wake on LAN: sent %1 magic packet(s) to %2").arg(sent).arg(target_.mac));
    } else {
        emit status_message(tr("Wake on LAN: all sends failed (check firewall + multi-NIC routes)"));
    }
}

void target_actions::send_preload_handshake() {
    using namespace opentm::tm_core;

    if (!session_->is_cfw_dex()) {
        deci3_frame f;
        f.direction = deci3_direction::host_to_machine;
        f.category  = 0x0010;
        for (auto b : { 0x08, 0x00, 0x00, 0x00 }) {
            f.payload.push_back(std::byte{static_cast<std::uint8_t>(b)});
        }
        if (connection_->send_frame(f)) {
            emit log_message(QStringLiteral("    <- preload: cat=0x0010 cmd=0x0800 get-status"));
        }
    }

    if (session_->is_cfw_dex()) return;

    send_session_tsmp(0x0102, {}, tr("preload"));
    send_session_tsmp(0x0104, {}, tr("preload"));
}

void target_actions::send_load_executable(const QString& file, const load_options& opts) {
    if (!require_ready(tr("Load Executable"))) return;
    if (opts.clear_streams) {
        emit clear_console();
        emit log_message(tr("    -- clear console output streams"));
    }
    const bool already_debug = session_->current_boot_mode() == opentm::tm_core::tsmp_boot_mode::debug;
    if (opts.reset_target && already_debug) {
        emit log_message(tr("    -- already in Debug Mode: skipping the reset"));
    } else if (opts.reset_target) {
        load_options resumed = opts;
        resumed.reset_target  = false;
        resumed.clear_streams = false;
        pending_load_ = pending_load{file, resumed};
        pending_load_timeout_.start();
        emit log_message(tr("    -- reset target (debug mode) requested by dialog"));
        emit log_message(tr("    -- load of %1 held until the debug agent is back").arg(file));
        emit status_message(tr("Resetting %1 - the load starts when it is back up").arg(target_.name.isEmpty() ? target_.host : target_.name));
        reset_debug();
        return;
    }

    const QFileInfo fi(file);
    emit log_message(tr(">>> Load Executable: %1 (%2 bytes)").arg(file).arg(fi.size()));

    QString wire_path;
    const bool kit_side = file.startsWith(QStringLiteral("/app_home/")) || file.startsWith(QStringLiteral("/dev_")) || file.startsWith(QStringLiteral("/host_root/"));
    if (kit_side) {
        wire_path = file;
    } else {
        wire_path = QStringLiteral("/app_home/") + QFileInfo(file).fileName();
        emit log_message(tr("    .. host path rewritten to %1").arg(wire_path));
    }

    using namespace opentm::tm_core;

    send_preload_handshake();

    const auto probe_seq = session_->next_dbgshl_seq();
    const auto probe = build_dbgshl(0x000000FFu, probe_seq, QByteArray());
    if (connection_->send_frame(probe)) {
        emit log_message(tr("    <- DBGSHL_CMD_GET_VERSION (ucmd=0x000000ff, seq=0x%1)").arg(probe_seq, 8, 16, QChar('0')));
    }
    std::uint32_t debug_flags = 0u;
    if (opts.use_elf_priority)    debug_flags |= 0x00000100u;
    if (opts.use_elf_stack)       debug_flags |= 0x00000200u;
    if (opts.enable_debug_module) debug_flags |= 0x00000001u;
    if (opts.disable_ppu_debug)   debug_flags |= 0x00010000u;
    if (opts.disable_spu_debug)   debug_flags |= 0x00020000u;
    if (opts.wait_for_bdvd)       debug_flags |= 0x00002000u;

    QList<QByteArray> argv;
    if (!opts.cmdline.isEmpty()) {
        // Plain whitespace split.
        const auto pieces = opts.cmdline.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
        argv.reserve(pieces.size());
        for (const auto& p : pieces) argv.push_back(p.toUtf8());
    }

    QList<QByteArray> envv;
    if (!opts.home_dir.isEmpty()) {
        envv.push_back(QByteArrayLiteral("HOME=") + opts.home_dir.toUtf8());
    }

    using opentm::tm_core::append_be_u32;
    using opentm::tm_core::append_be_u64;

    const QByteArray path_bytes = wire_path.toUtf8();
    QByteArray body;
    body.reserve(4 + path_bytes.size() + 1 + 8 + 8 + 156);

    append_be_u32(body, opts.priority);
    body.append(path_bytes);
    body.append(static_cast<char>(0x00));
    append_be_u32(body, static_cast<std::uint32_t>(argv.size()));
    for (const auto& a : argv) {
        body.append(a);
        body.append(static_cast<char>(0x00));
    }
    append_be_u32(body, static_cast<std::uint32_t>(envv.size()));
    for (const auto& e : envv) {
        body.append(e);
        body.append(static_cast<char>(0x00));
    }
    append_be_u32(body, debug_flags);
    append_be_u32(body, opts.stack_size);
    append_be_u32(body, 0x00000000u);
    std::uint64_t d0 = 0;
    if (opts.lv2_exception_handler) d0 |= 0x0001ull;
    if (opts.remote_play)           d0 |= 0x0002ull;
    if (opts.gcm_debug)             d0 |= 0x0004ull;
    if (opts.load_libprof)          d0 |= 0x0008ull;
    if (opts.core_dump)             d0 |= 0x0010ull;
    if (opts.remote_play_avc)       d0 |= 0x0020ull;
    if (opts.smart_image_capture)   d0 |= 0x0040ull;
    if (opts.memory_access_trap)    d0 |= 0x0080ull;
    d0 |= (static_cast<std::uint64_t>(opts.game_attribute) & 0xfull) << 8;
    if (opts.patch_boot)            d0 |= 0x1000ull;

    std::uint64_t d3 = 0;
    if (opts.rsx_profiling_tool)    d3 |= 0x1ull;
    if (opts.high_memory_footprint) d3 |= 0x2ull;

    append_be_u64(body, 0); // version
    append_be_u64(body, opts.enable_extra_options ? d0 : 0); // data[0]
    append_be_u64(body, opts.enable_extra_options ? 1 : 0); // data[1]
    append_be_u64(body, opts.core_dump ? opts.core_dump_location : 0); // data[2]
    append_be_u64(body, d3); // data[3]
    append_be_u64(body, opts.gcm_capture_mode ? 1 : 0); // data[4]
    for (int i = 5; i <= 15; ++i) append_be_u64(body, 0); // data[5..15]

    emit log_message(tr("    -- LOAD_EXT debug_flags=0x%1 (debug=%2 ppu_dis=%3 spu_dis=%4)"" argc=%5 envc=%6").arg(debug_flags, 8, 16, QChar('0')).arg(opts.enable_debug_module ? 1 : 0).arg(opts.disable_ppu_debug ? 1 : 0).arg(opts.disable_spu_debug ? 1 : 0).arg(argv.size()).arg(envv.size()));

    const auto load_seq = session_->next_dbgshl_seq();
    const auto load_frame = build_dbgshl(0x00000004u, load_seq, body);
    if (connection_->send_frame(load_frame)) {
        emit log_message(tr("    <- DBGSHL_CMD_LOAD_EXT (ucmd=0x00000004, seq=0x%1, body=%2B)").arg(load_seq, 8, 16, QChar('0')).arg(load_frame.payload.size()));
        emit status_message(tr("Sent LOAD_EXT - watch Wire Log for reply"));
    }
}

opentm::tm_core::deci3_frame target_actions::build_dbgshl(
    std::uint32_t ucmd, std::uint32_t seq, const QByteArray& body) const
{
    return dbgshl_frame(session_->dbgshl_sb(), ucmd, seq, body);
}

void target_actions::install_package(const QString& host_path) {
    if (!require_ready(tr("Install package"))) return;
    const QString wire_path = QStringLiteral("/app_home/") + QDir::toNativeSeparators(host_path);

    QByteArray body = wire_path.toLatin1();
    body.append('\0');

    const auto seq = session_->next_dbgshl_seq();
    if (connection_->send_frame(build_dbgshl(opentm::tm_core::dbgshl::cmd::install_package & ~opentm::tm_core::dbgshl::reply_bit, seq, body)))
    {
        emit log_message(tr("    <- INSTALL_PACKAGE %1 (ucmd=0x00000802, seq=0x%2)").arg(wire_path).arg(seq, 8, 16, QChar('0')));
        emit status_message(tr("Installing %1...").arg(QFileInfo(host_path).fileName()));
    }
}

void target_actions::send_settings_refresh() {
    if (!require_ready(tr("Refresh XMB settings"))) return;
    const auto seq = session_->next_dbgshl_seq();
    if (connection_->send_frame(build_dbgshl(0x00000810u, seq, QByteArray()))) {
        emit log_message(tr("    <- CREATE_SETTINGS_FILE refresh (ucmd=0x00000810, seq=0x%1)").arg(seq, 8, 16, QChar('0')));
        emit status_message(tr("Requested settings dump from target"));
    }
}

void target_actions::send_settings_apply(const QString& host_path, std::uint32_t file_size)
{
    if (!require_ready(tr("Apply XMB settings"))) return;
    constexpr int kPathSlot = 1056;
    QByteArray body;
    body.reserve(0x860);
    auto be32 = [&body](std::uint32_t v) { opentm::tm_core::append_be_u32(body, v); };
    const std::uint32_t id = transfer_id_++;
    be32(0x00000012u); // +0x00 param_c = transfer
    be32(id); // +0x04 transfer id (TM: 2,3,4...)
    be32(0); // +0x08
    be32(0); // +0x0C
    be32(static_cast<std::uint32_t>(
             QDateTime::currentSecsSinceEpoch() & 0xffffffffu)); // +0x10
    be32(0); // +0x14
    be32(file_size); // +0x18 source size in bytes
    be32(id); // +0x1C mirrors the id
    QString rel = host_path;
    if (!target_.file_server_dir.isEmpty()) {
        const QDir root(target_.file_server_dir);
        const QString candidate = root.relativeFilePath(host_path);
        if (!candidate.startsWith(QStringLiteral(".."))) rel = candidate;
    }
    QByteArray src = QByteArrayLiteral("/app_home/") + QDir::toNativeSeparators(rel).toUtf8();
    QByteArray dst = QByteArrayLiteral("/dev_hdd0/game_debug/settings/PS3SETTINGS.SFT");
    src.truncate(kPathSlot - 1);
    dst.truncate(kPathSlot - 1);
    body.append(src);
    body.append(QByteArray(kPathSlot - src.size(), '\0'));
    body.append(dst);
    body.append(QByteArray(kPathSlot - dst.size(), '\0'));

    const auto seq = session_->next_dbgshl_seq();
    if (connection_->send_frame(build_dbgshl(0x0020000Fu, seq, body))) {
        emit log_message(tr("    <- XMB settings transfer (param_c=0x12, id=%1, ""%2B file, seq=0x%3)").arg(id).arg(file_size).arg(seq, 8, 16, QChar('0')));
        emit log_message(tr("       src=%1").arg(QString::fromUtf8(src)));
    }
}

void target_actions::send_settings_commit() {
    if (!require_ready(tr("Apply XMB settings"))) return;

    QByteArray body;
    body.reserve(12);
    for (auto v : {0x00000014u, 0x00000000u, 0x00000000u}) {
        opentm::tm_core::append_be_u32(body, v);
    }
    const auto seq = session_->next_dbgshl_seq();
    if (connection_->send_frame(build_dbgshl(0x0020000Fu, seq, body))) {
        emit log_message(tr("    <- XMB settings commit (param_c=0x14, seq=0x%1)").arg(seq, 8, 16, QChar('0')));
    }
}

} // namespace opentm::tm_ui
