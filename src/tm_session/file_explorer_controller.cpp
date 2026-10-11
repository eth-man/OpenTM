#include "file_explorer_controller.h"

#include "session_controller.h"
#include "tsmp_helpers.h"

#include <tm_core/be_io.h>
#include <tm_core/deci3_codec.h>

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QStringLiteral>

#include <cstddef>
#include <span>

namespace opentm::tm_ui {

file_explorer_controller::file_explorer_controller(opentm::tm_core::tcp_connection* conn, session_controller* sess, QObject* parent) : QObject(parent), connection_(conn), session_(sess) {}
file_explorer_controller::~file_explorer_controller() = default;

void file_explorer_controller::list_directory(const QString& path) {
    using namespace opentm::tm_core;

    if (connection_->current_state() != tcp_connection::state::ready) {
        emit status_message(tr("File Explorer: connect first"));
        return;
    }
    if (!session_->is_ready()) {
        emit status_message(tr("File Explorer: session not ready yet"));
        return;
    }

    pending_path_ = path;


    const std::uint32_t dbgshl_sb = session_->dbgshl_sb();
    auto build_dfmp_frame = [dbgshl_sb](std::uint32_t cmd, std::uint32_t seq, const QByteArray& body) {
        return dbgshl_frame(dbgshl_sb, cmd, seq, body);
    };
    // devkit: 32 bytes header + two 1056 bytes path slots = 2144 (0x860)
    // dex: 8 byte header + one 1056 byte path slot = 1064 (0x428)
    //
    // 0x0020000f param_c=0x08 settings file -> reply body 0x10
    // 0x0020000f param_c=0x1a list directory -> reply body 0x398
    // 0x00200013 param_c=0x36 per-nav query -> reply body 0x18
    if (session_->is_cfw_dex()) {
        constexpr int kDexPathSlot = 1056;
        auto dex_body = [](std::uint32_t param_c, const QByteArray& p) {
            QByteArray b;
            b.reserve(8 + kDexPathSlot);
            for (int i = 3; i >= 0; --i) b.append(char((param_c >> (i * 8)) & 0xff));
            b.append(QByteArray(4, '\0'));
            QByteArray path = p.left(kDexPathSlot - 1);
            b.append(path);
            b.append(QByteArray(kDexPathSlot - path.size(), '\0'));
            return b;
        };

        if (!session_->is_dbgshl_stream_open()) {
            const auto probe_seq = session_->next_dbgshl_seq();
            if (connection_->send_frame(build_dfmp_frame(0x000000FFu, probe_seq, QByteArray()))) {
                emit log_message(tr("    <- DFMP stream-open probe (DEX, seq=0x%1)").arg(probe_seq, 8, 16, QChar('0')));
            }
            const auto set_seq = session_->next_dbgshl_seq();
            const auto set_body = dex_body(0x08, QByteArrayLiteral(
                "/dev_hdd0/game_debug/settings/PS3SETTINGS.SFT"));
            if (connection_->send_frame(build_dfmp_frame(dfmp_op_cmd::file_operation, set_seq, set_body))) {
                emit log_message(tr("    <- DFMP DA-init settings (DEX, param_c=0x08, " "seq=0x%1, %2B body)").arg(set_seq, 8, 16, QChar('0')).arg(set_body.size()));
            }
            session_->mark_dbgshl_stream_open();
        }

        open_seq_ = 0;
        list_seq_ = session_->next_dbgshl_seq();
        const auto body = dex_body(0x1a, path.toUtf8());
        if (connection_->send_frame(build_dfmp_frame(dfmp_op_cmd::file_operation, list_seq_, body))) {
            emit log_message(tr("    <- DFMP list_directory DEX(%1) param_c=0x1a " "seq=0x%2 (%3B body)") .arg(path).arg(list_seq_, 8, 16, QChar('0')) .arg(body.size()));
        }
        return;
    }

    if (!session_->is_dbgshl_stream_open()) {
        const auto probe_seq = session_->next_dbgshl_seq();
        const auto probe = build_dfmp_frame(0x000000FFu, probe_seq, QByteArray());
        if (connection_->send_frame(probe)) {
            emit log_message(tr("    <- DFMP stream-open probe (ucmd=0x000000ff, seq=0x%1)").arg(probe_seq, 8, 16, QChar('0')));
        }

        const auto init_seq = session_->next_dbgshl_seq();
        const auto init = build_dfmp_frame(0x00000810u, init_seq, QByteArray());
        if (connection_->send_frame(init)) {
            emit log_message(tr("    <- DFMP DA-init (ucmd=0x00000810 create_settings_file, seq=0x%1)").arg(init_seq, 8, 16, QChar('0')));
        }
        // The kit pulls PS3SETTINGS.SFT out of the served directory, so asking
        // for it when nothing is served (or the file is not there) just earns a
        // DRFP ENOENT and "transfer refused, result=0x4" every session.
        const bool have_settings =
            !serving_dir_.isEmpty()
            && QFileInfo::exists(QDir(serving_dir_).filePath(QStringLiteral("PS3SETTINGS.SFT")));
        if (!have_settings) {
            session_->mark_dbgshl_stream_open();
        } else {
        //
        // +0x00 u32 BE param_c = 0x12 (xfer)
        // +0x04 u32 BE mode = 0x02
        // +0x08 u32 BE reserved
        // +0x0C u32 BE reserved
        // +0x10 u32 BE timestamp in posix sec
        // +0x14 u32 BE reserved
        // +0x18 u32 BE file_size (src file size in bytes)
        // +0x1C u32 BE flags = 0x02
        // +0x20 [1056] source path, nul pad
        // +0x440 [1056] dest path, and nul pad
        //
        QByteArray xfer_body;
        xfer_body.reserve(2144);
        auto push_be_u32_qb = [&xfer_body](std::uint32_t v) { append_be_u32(xfer_body, v); };
        push_be_u32_qb(0x00000012u); // +0x00 param_c = transfer
        push_be_u32_qb(0x00000002u); // +0x04 mode = 2
        push_be_u32_qb(0); // +0x08 reserved
        push_be_u32_qb(0); // +0x0C reserved
        const auto now = static_cast<std::uint32_t>(QDateTime::currentSecsSinceEpoch() & 0xffffffffu);
        push_be_u32_qb(now); // +0x10 timestamp
        push_be_u32_qb(0); // +0x14 reserved
        push_be_u32_qb(0x000000A7u); // +0x18 file_size (PS3SETTINGS.SFT size)
        push_be_u32_qb(0x00000002u); // +0x1C flags

        constexpr int kPathSlot = 1056;
        const QByteArray src_path = QByteArrayLiteral("/app_home/PS3SETTINGS.SFT");
        const QByteArray dst_path = QByteArrayLiteral("/dev_hdd0/game_debug/settings/PS3SETTINGS.SFT");
        xfer_body.append(src_path);
        xfer_body.append(QByteArray(kPathSlot - src_path.size(), '\0'));
        xfer_body.append(dst_path);
        xfer_body.append(QByteArray(kPathSlot - dst_path.size(), '\0'));

        const auto xfer_seq = session_->next_dbgshl_seq();
        const auto xfer = build_dfmp_frame(dfmp_op_cmd::file_operation, xfer_seq, xfer_body);
        if (connection_->send_frame(xfer)) {
            emit log_message(tr("    <- DFMP DA-init transfer (cmd=0x0020000F param_c=0x12, seq=0x%1, %2B body)").arg(xfer_seq, 8, 16, QChar('0')).arg(xfer_body.size()));
        }
        session_->mark_dbgshl_stream_open();
        }
    }

    auto std_path = path.toStdString();
    const auto opendir_body = build_path_op_body(dfmp_file_op_kind::open_dir_marker, std_path);
    const auto getentries_body = build_path_op_body(dfmp_file_op_kind::get_entries, std_path);

    QByteArray opendir_qb(reinterpret_cast<const char*>(opendir_body.data()), static_cast<int>(opendir_body.size()));
    QByteArray getentries_qb(reinterpret_cast<const char*>(getentries_body.data()), static_cast<int>(getentries_body.size()));

    open_seq_ = session_->next_dbgshl_seq();
    const auto opendir_frame = build_dfmp_frame(dfmp_op_cmd::open_directory, open_seq_, opendir_qb);
    if (connection_->send_frame(opendir_frame)) {
        emit log_message(tr("    <- DFMP open_directory(%1) seq=0x%2").arg(path).arg(open_seq_, 8, 16, QChar('0')));
    }

    list_seq_ = session_->next_dbgshl_seq();
    const auto getentries_frame = build_dfmp_frame(dfmp_op_cmd::file_operation, list_seq_, getentries_qb);
    if (connection_->send_frame(getentries_frame)) {
        emit log_message(tr("    <- DFMP get_entries(%1) seq=0x%2").arg(path).arg(list_seq_, 8, 16, QChar('0')));
    }
}

namespace {

QByteArray to_qba(const std::vector<std::byte>& v) {
    return QByteArray(reinterpret_cast<const char*>(v.data()), static_cast<int>(v.size()));
}

} // namespace

std::uint32_t file_explorer_controller::send_dfmp(std::uint32_t cmd, const QByteArray& body, const QString& what)
{
    using namespace opentm::tm_core;
    if (connection_->current_state() != tcp_connection::state::ready || !session_->is_ready()) {
        emit status_message(tr("%1: connect first").arg(what));
        return 0;
    }
    const auto seq = session_->next_dbgshl_seq();
    const auto frame = dbgshl_frame(session_->dbgshl_sb(), cmd, seq, body);
    if (!connection_->send_frame(frame)) return 0;
    return seq;
}

void file_explorer_controller::download_file(const QString& kit_path, const QString& host_path, std::uint32_t size)
{
    using namespace opentm::tm_core;
    const auto body = build_transfer_body(
        dfmp_transfer_direction::to_host,
        kit_path.toStdString(),
        host_transfer_path(host_path.toStdString()),
        size);

    if (const auto seq = send_dfmp(dfmp_op_cmd::file_operation, to_qba(body), tr("Download"))) {
        emit log_message(tr("    <- DFMP transfer %1 -> %2 (%3B, seq=0x%4)").arg(kit_path, host_path).arg(size).arg(seq, 8, 16, QChar('0')));
        emit status_message(tr("Downloading %1").arg(kit_path));
    }
}

void file_explorer_controller::upload_file(const QString& host_path, const QString& kit_path)
{
    using namespace opentm::tm_core;
    const QFileInfo fi(host_path);
    if (!fi.exists() || !fi.isFile()) {
        emit status_message(tr("Upload: no such file %1").arg(host_path));
        emit log_message(tr("    !! upload aborted - %1 is not a file").arg(host_path));
        return;
    }

    // the target sets the destination's timestamp from this, so send the
    // source's rather than "now"
    const auto mtime = static_cast<std::uint32_t>(fi.lastModified().toSecsSinceEpoch() & 0xffffffffu);
    const auto body = build_transfer_body(
        dfmp_transfer_direction::to_target,
        host_transfer_path(host_path.toStdString()),
        kit_path.toStdString(),
        static_cast<std::uint32_t>(fi.size()),
        mtime);

    if (const auto seq = send_dfmp(dfmp_op_cmd::file_operation, to_qba(body), tr("Upload"))) {
        emit log_message(tr("    <- DFMP transfer %1 -> %2 (%3B, seq=0x%4)").arg(host_path, kit_path).arg(fi.size()).arg(seq, 8, 16, QChar('0')));
        emit status_message(tr("Uploading %1").arg(fi.fileName()));
    }
}

void file_explorer_controller::delete_file(const QString& kit_path) {
    using namespace opentm::tm_core;
    const auto body = build_path_op_body(dfmp_file_op_kind::remove, kit_path.toStdString());

    if (const auto seq = send_dfmp(dfmp_op_cmd::file_operation, to_qba(body), tr("Delete"))) {
        pending_ops_.insert(seq, QStringLiteral("delete"));
        emit log_message(tr("    <- DFMP delete %1 (seq=0x%2)").arg(kit_path).arg(seq, 8, 16, QChar('0')));
        emit status_message(tr("Deleting %1").arg(kit_path));
    }
}

void file_explorer_controller::rename_file(const QString& from, const QString& to) {
    using namespace opentm::tm_core;
    const auto body = build_rename_body(from.toStdString(), to.toStdString());

    if (const auto seq = send_dfmp(dfmp_op_cmd::file_operation, to_qba(body), tr("Rename"))) {
        pending_ops_.insert(seq, QStringLiteral("rename"));
        emit log_message(tr("    <- DFMP rename %1 -> %2 (seq=0x%3)").arg(from, to).arg(seq, 8, 16, QChar('0')));
        emit status_message(tr("Renaming %1").arg(from));
    }
}

void file_explorer_controller::make_directory(const QString& kit_path, std::uint32_t mode) {
    using namespace opentm::tm_core;
    const auto body = build_mkdir_body(mode, kit_path.toStdString());

    if (const auto seq = send_dfmp(dfmp_op_cmd::file_operation, to_qba(body), tr("New folder"))) {
        pending_ops_.insert(seq, QStringLiteral("mkdir"));
        emit log_message(tr("    <- DFMP mkdir %1 mode=0%2 (seq=0x%3)").arg(kit_path).arg(mode & 0777, 0, 8).arg(seq, 8, 16, QChar('0')));
        emit status_message(tr("Creating %1").arg(kit_path));
    }
}

void file_explorer_controller::set_permissions(const QString& kit_path, std::uint32_t mode) {
    using namespace opentm::tm_core;
    const auto body = build_chmod_body(mode, kit_path.toStdString());

    if (const auto seq = send_dfmp(dfmp_op_cmd::file_operation, to_qba(body), tr("Permissions"))) {
        pending_ops_.insert(seq, QStringLiteral("chmod"));
        emit log_message(tr("    <- DFMP chmod %1 mode=0%2 (seq=0x%3)").arg(kit_path).arg(mode & 0777, 0, 8).arg(seq, 8, 16, QChar('0')));
        emit status_message(tr("Setting permissions on %1").arg(kit_path));
    }
}

void file_explorer_controller::set_times(const QString& kit_path, std::uint64_t atime, std::uint64_t mtime) {
    using namespace opentm::tm_core;
    const auto body = build_utime_body(atime, mtime, kit_path.toStdString());

    if (const auto seq = send_dfmp(dfmp_op_cmd::file_operation, to_qba(body), tr("Time settings"))) {
        pending_ops_.insert(seq, QStringLiteral("utime"));
        emit log_message(tr("    <- DFMP utime %1 atime=%2 mtime=%3 (seq=0x%4)").arg(kit_path).arg(atime).arg(mtime).arg(seq, 8, 16, QChar('0')));
        emit status_message(tr("Setting timestamps on %1").arg(kit_path));
    }
}

void file_explorer_controller::on_dfmp_op_reply(std::uint32_t seq, std::uint32_t marker, std::uint32_t status) {
    const QString op = pending_ops_.take(seq);
    const QString named = op.isEmpty() ? QStringLiteral("file op 0x%1").arg(marker, 2, 16, QChar('0')) : op;
    if (status == 0) {
        emit log_message(tr("       << %1 ok").arg(named));
    } else {
        emit log_message(tr("       !! %1 failed, status=0x%2").arg(named).arg(status, 8, 16, QChar('0')));
    }
    emit file_op_finished(named, status);
}

void file_explorer_controller::on_dfmp_get_entries_reply(std::uint32_t seq, QByteArray body) {
    using namespace opentm::tm_core;
    if (list_seq_ == 0 || seq != list_seq_) return;

    std::span<const std::byte> body_span{reinterpret_cast<const std::byte*>(body.constData()), static_cast<std::size_t>(body.size()) };
    auto entries = session_->is_cfw_dex() ? parse_get_entries_reply_dex(body_span) : parse_get_entries_reply(body_span);

    emit log_message(tr("       >> DFMP listing %1 -> %2 entries").arg(pending_path_).arg(entries.size()));
    emit status_message(tr("Listed %1 (%2 entries)").arg(pending_path_).arg(entries.size()));

    emit directory_listed(pending_path_, std::move(entries));
    list_seq_ = 0;
}

void file_explorer_controller::on_session_invalidated() {
    open_seq_ = 0;
    list_seq_ = 0;
}

} // namespace opentm::tm_ui
