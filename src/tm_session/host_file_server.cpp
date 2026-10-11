#include "host_file_server.h"

#include "tsmp_helpers.h"

#include <tm_core/tcp_connection.h>

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace opentm::tm_ui {

namespace {

constexpr std::uint16_t kCatDrfp     = 0x0110;
constexpr std::uint32_t kEnvelopeSb  = 0x02000000;

constexpr std::int32_t kLv2Enoent  = static_cast<std::int32_t>(0x80010006);
constexpr std::int32_t kLv2Einval  = static_cast<std::int32_t>(0x8001001d);
constexpr std::int32_t kLv2Ebadf   = static_cast<std::int32_t>(0x8001002a);
constexpr std::int32_t kLv2Eio       = static_cast<std::int32_t>(0x8001002b);
constexpr std::int32_t kLv2Eexist    = static_cast<std::int32_t>(0x80010014);
constexpr std::int32_t kLv2Eisdir    = static_cast<std::int32_t>(0x80010012);
constexpr std::int32_t kLv2Enotdir   = static_cast<std::int32_t>(0x8001002e);
constexpr std::int32_t kLv2Enotempty = static_cast<std::int32_t>(0x80010036);

QString strip_app_home(QString path) {
    if (path.startsWith(QStringLiteral("/app_home/")))
        path = path.mid(10);
    else if (path == QStringLiteral("/app_home"))
        path.clear();
    path.replace('\\', '/');
    while (path.startsWith('/')) path = path.mid(1);
    return path;
}

QString read_cstr(const QByteArray& payload, int offset) {
    int end = payload.indexOf('\0', offset);
    if (end < 0) end = payload.size();
    return QString::fromUtf8(payload.constData() + offset, end - offset);
}

opentm::tm_core::drfp_stat stat_from_qfileinfo(const QFileInfo& fi) {
    opentm::tm_core::drfp_stat st;
    st.mode  = fi.isDir() ? opentm::tm_core::drfp_mode::dir_default : opentm::tm_core::drfp_mode::file_default;
    st.mtime = static_cast<std::uint64_t>(fi.lastModified().toSecsSinceEpoch());
    st.atime = static_cast<std::uint64_t>(fi.lastRead().toSecsSinceEpoch());
    st.ctime = static_cast<std::uint64_t>(fi.birthTime().isValid() ? fi.birthTime().toSecsSinceEpoch() : st.mtime);
    st.size  = static_cast<std::uint32_t>(fi.size());
    return st;
}

opentm::tm_core::drfp_stat synthetic_stat(int size) {
    opentm::tm_core::drfp_stat st;
    st.mode = 0x00008180u;
    st.size = static_cast<std::uint32_t>(size);
    st.mtime = st.atime = st.ctime = static_cast<std::uint64_t>(QDateTime::currentSecsSinceEpoch());
    return st;
}

QByteArray seek_reply_body(qint64 target) {
    QByteArray body;
    opentm::tm_core::append_be_u32(body, 0);
    opentm::tm_core::append_be_u32(body, static_cast<std::uint32_t>(target & 0xffffffffu));
    opentm::tm_core::append_be_u32(body, static_cast<std::uint32_t>((static_cast<std::uint64_t>(target) >> 32) & 0xffffffffu));
    return body;
}

QByteArray build_synthetic_param_sfo() {
    auto le_u16 = [](QByteArray& b, std::uint16_t v) {
        b.append(static_cast<char>(v & 0xff));
        b.append(static_cast<char>((v >> 8) & 0xff));
    };
    auto le_u32 = [](QByteArray& b, std::uint32_t v) {
        b.append(static_cast<char>(v & 0xff));
        b.append(static_cast<char>((v >> 8) & 0xff));
        b.append(static_cast<char>((v >> 16) & 0xff));
        b.append(static_cast<char>((v >> 24) & 0xff));
    };
    struct entry {
        const char*   key;
        std::uint16_t key_off;
        std::uint16_t data_fmt;
        std::uint32_t data_len;
        std::uint32_t data_max_len;
        std::uint32_t data_off;
        const char*   value;
    };
    constexpr std::uint16_t fmt_utf8 = 0x0204; 
    entry entries[] = {
        { "CATEGORY",  0,       fmt_utf8, 3,        4,            0,        "HG"        },
        { "TITLE",     9,       fmt_utf8, 7,        128,          4,        "OpenTM"    },
        { "TITLE_ID",  15,      fmt_utf8, 10,       16,           132,      "BLES00001" },
        { "VERSION",   24,      fmt_utf8, 6,        8,            148,      "01.00"     },
    };
    constexpr int n = sizeof(entries) / sizeof(entries[0]);

    const std::uint32_t key_table_off = 20 + 16 * n; 
    std::uint32_t data_table_off = key_table_off;
    for (const auto& e : entries) data_table_off += static_cast<std::uint32_t>(qstrlen(e.key) + 1);
    while ((data_table_off & 3u) != 0) ++data_table_off;
    std::uint32_t data_table_size = 0;
    for (const auto& e : entries) data_table_size += e.data_max_len;

    QByteArray sfo;
    sfo.reserve(static_cast<int>(data_table_off + data_table_size));
    sfo.append('\0'); sfo.append('P'); sfo.append('S'); sfo.append('F');
    le_u32(sfo, 0x00000101u); 
    le_u32(sfo, key_table_off);
    le_u32(sfo, data_table_off);
    le_u32(sfo, static_cast<std::uint32_t>(n));
    for (const auto& e : entries) {
        le_u16(sfo, e.key_off);
        le_u16(sfo, e.data_fmt);
        le_u32(sfo, e.data_len);
        le_u32(sfo, e.data_max_len);
        le_u32(sfo, e.data_off);
    }
    const int key_table_start = sfo.size();
    for (const auto& e : entries) {
        sfo.append(e.key);
        sfo.append('\0');
    }
    while (((sfo.size() - key_table_start + key_table_off) & 3) != 0) sfo.append('\0');
    for (const auto& e : entries) {
        const QByteArray v(e.value);
        sfo.append(v);
        sfo.append('\0');
        for (std::uint32_t i = static_cast<std::uint32_t>(v.size() + 1); i < e.data_max_len; ++i) {
            sfo.append('\0');
        }
    }
    return sfo;
}

} // namespace

host_file_server::host_file_server(opentm::tm_core::tcp_connection* conn, QObject* parent) : QObject(parent), connection_(conn) {}
host_file_server::~host_file_server() { reset(); }

void host_file_server::set_file_server_dir(const QString& dir) {
    const QString next = dir.isEmpty() ? QString() : QDir::cleanPath(dir);
    // the UI re-pushes the record on every selection change, so only say
    // something when the root actually moves
    if (next == file_server_dir_) return;
    file_server_dir_ = next;
    warned_no_dir_   = false;
    emit log_message(QStringLiteral("    -- host_file_server root -> %1") .arg(file_server_dir_.isEmpty() ? QStringLiteral("(none)") : file_server_dir_));
}

void host_file_server::warn_no_serving_dir() {
    if (warned_no_dir_) return;
    warned_no_dir_ = true;
    emit log_message(QStringLiteral("    !! this target has no File Server Dir set, so /app_home/ cannot ""be served."));
    emit log_message(QStringLiteral("       The console needs it to finish booting - it writes its ""settings there - and will retry forever until it exists."));
    emit log_message(QStringLiteral("       Set one in Target Properties, or the target's app_home/ row. ""Further probes are not logged."));
}

void host_file_server::reset() {
    for (auto& kv : open_files_) {
        if (kv.second) {
            kv.second->close();
            delete kv.second;
        }
    }
    open_files_.clear();
    synth_files_.clear();
    open_dirs_.clear();
}

void host_file_server::on_frame_received(opentm::tm_core::deci3_frame f) {
    using namespace opentm::tm_core;
    if (f.category != kCatDrfp) return;

    QByteArray inner( reinterpret_cast<const char*>(f.payload.data()), static_cast<int>(f.payload.size()));
    const auto parsed = parse_drfp(inner);
    if (!parsed) {
        emit log_message(QStringLiteral("    !! host_file_server: unparseable DRFP frame, %1B").arg(inner.size()));
        return;
    }

    QByteArray reply;
    switch (parsed->code) {
    case drfp_code::init:  reply = handle_init(*parsed); break;
    case drfp_code::stat:  reply = handle_stat(*parsed); break;
    case drfp_code::open:  reply = handle_open(*parsed); break;
    case drfp_code::close: reply = handle_close(*parsed); break;
    case drfp_code::read:  reply = handle_read(*parsed); break;
    case drfp_code::write: reply = handle_write(*parsed); break;
    case drfp_code::mkdir:     reply = handle_mkdir(*parsed); break;
    case drfp_code::rmdir:     reply = handle_rmdir(*parsed); break;
    case drfp_code::dopen:     reply = handle_dopen(*parsed); break;
    case drfp_code::dclose:    reply = handle_dclose(*parsed); break;
    case drfp_code::dread:     reply = handle_dread(*parsed); break;
    case drfp_code::ftruncate: reply = handle_ftruncate(*parsed); break;
    case drfp_code::truncate:  reply = handle_truncate(*parsed); break;
    case drfp_code::rename:    reply = handle_rename(*parsed); break;
    case drfp_code::unlink:    reply = handle_unlink(*parsed); break;
    case drfp_code::fstat: reply = handle_fstat(*parsed); break;
    case drfp_code::seek:  reply = handle_seek(*parsed); break;
    default:
        emit log_message(QStringLiteral("    ?? host_file_server: unhandled DRFP code=%1 seq=0x%2 - replying ENOENT").arg(static_cast<int>(parsed->code)).arg(parsed->seq, 8, 16, QChar('0')));
        reply = build_result_reply( static_cast<drfp_code>(static_cast<std::uint32_t>(parsed->code) + 1), parsed->seq, kLv2Enoent);
        break;
    }
    if (!reply.isEmpty()) send_drfp_reply(reply);
}

void host_file_server::send_drfp_reply(const QByteArray& drfp_body) {
    if (!connection_) return;

    using namespace opentm::tm_core;
    deci3_frame env;
    env.session_a = 0;
    env.direction = deci3_direction::host_to_target;
    env.session_b = cfw_dex_ ? 0x00000000u : kEnvelopeSb;
    env.category  = kCatDrfp;
    env.payload   = to_bytes(drfp_body);
    connection_->send_frame(env);
}

QString host_file_server::normalise_host_path(QString p) {
    p.replace('\\', '/');
    p = QDir::cleanPath(p);
    //see below#
    if (p.size() == 2 && p[1] == QLatin1Char(':') && p[0].isLetter()) {
        p.append(QLatin1Char('/'));
    }
    return p;
}

bool host_file_server::looks_absolute(const QString& p) {
    return p.startsWith(QLatin1Char('/'))
        || (p.size() >= 2 && p[1] == QLatin1Char(':') && p[0].isLetter());
}

QStringList host_file_server::transfer_path_chain(const QString& host_path) {
    QStringList out;
    QString p = normalise_host_path(host_path);
    if (p.isEmpty()) return out;

    // the target stats each directory on the way down before opening the
    // file, so the ancestors have to be reachable too
    out.append(p);
    for (int slash = p.lastIndexOf('/'); slash > 0; slash = p.lastIndexOf('/')) {
        p.truncate(slash);
        if (p.isEmpty()) break;
        const auto step = normalise_host_path(p);
        if (!out.contains(step)) out.append(step);
    }
    return out;
}

void host_file_server::authorise_transfer_path(const QString& host_path) {
    for (const auto& p : transfer_path_chain(host_path)) {
        transfer_paths_.insert(p);
    }
}

void host_file_server::forget_transfer_path(const QString& host_path) {
    transfer_paths_.remove(normalise_host_path(host_path));
}

bool host_file_server::is_authorised(const QString& cleaned) const {
    if (transfer_paths_.contains(cleaned)) return true;
#ifdef Q_OS_WIN
    // drive letters and the rest of the path are case-insensitive here
    for (const auto& p : transfer_paths_) {
        if (p.compare(cleaned, Qt::CaseInsensitive) == 0) return true;
    }
#endif
    return false;
}

QString host_file_server::resolve(const QString& kit_path) const {
    QString rel = strip_app_home(kit_path);
    rel.replace('\\', '/');

    // "C:" is as absolute as "C:/"
    // the target walks a destination's ancestors and asks about the bare drive on the way past
    // needing the slash made that look relative and joined it onto the serving root
    const bool is_absolute = looks_absolute(rel);

    if (is_absolute) {
        const QString cleaned = normalise_host_path(rel);
        if (is_authorised(cleaned)) return cleaned;
    }

    if (file_server_dir_.isEmpty()) return {};
    const QString combined = is_absolute ? QDir::cleanPath(rel) : QDir::cleanPath(file_server_dir_ + QStringLiteral("/") + rel);
    QString root = file_server_dir_;
    if (!root.endsWith('/')) root.append('/');
    if (combined + QStringLiteral("/") == root || combined == file_server_dir_)
        return file_server_dir_;
    if (!combined.startsWith(root)) return {};
    return combined;
}

bool host_file_server::is_synthetic_param_sfo(const QString& kit_path) {
    return kit_path.endsWith(QStringLiteral("PS3_GAME/PARAM.SFO"), Qt::CaseInsensitive)
        || kit_path.endsWith(QStringLiteral("PS3_GAME\\PARAM.SFO"), Qt::CaseInsensitive)
        || kit_path.endsWith(QStringLiteral("/PARAM.SFO"), Qt::CaseInsensitive)
        || kit_path.endsWith(QStringLiteral("\\PARAM.SFO"), Qt::CaseInsensitive)
        || kit_path.compare(QStringLiteral("PARAM.SFO"), Qt::CaseInsensitive) == 0;
}

QByteArray host_file_server::handle_init(
    const opentm::tm_core::drfp_frame& f) {
    using namespace opentm::tm_core;
    emit log_message(QStringLiteral("    -> DRFP INIT (seq=0x%1)").arg(f.seq, 8, 16, QChar('0')));
    return build_init_reply(f.seq);
}

QByteArray host_file_server::handle_stat(
    const opentm::tm_core::drfp_frame& f) {
    using namespace opentm::tm_core;
    const QString kit_path = read_cstr(f.payload, 0);
    emit log_message(QStringLiteral("    -> DRFP STAT %1 (seq=0x%2)").arg(kit_path).arg(f.seq, 8, 16, QChar('0')));

    if (is_synthetic_param_sfo(kit_path)) {
        const auto st = synthetic_stat(build_synthetic_param_sfo().size());
        emit log_message(QStringLiteral("       << synthetic PARAM.SFO stat ok: %1B").arg(st.size));
        return build_stat_reply(drfp_code::stat_reply, f.seq, 0, st);
    }

    const QString local = resolve(kit_path);
    if (local.isEmpty()) {
        if (file_server_dir_.isEmpty()) {
            warn_no_serving_dir();
        } else {
            emit log_message(QStringLiteral("       !! '%1' escapes the File Server Dir - ENOENT").arg(kit_path));
        }
        return build_result_reply(drfp_code::stat_reply, f.seq, kLv2Enoent);
    }
    const QFileInfo fi(local);
    if (!fi.exists()) {
        emit log_message(QStringLiteral(
            "       !! %1 does not exist - ENOENT").arg(local));
        return build_result_reply(drfp_code::stat_reply, f.seq, kLv2Enoent);
    }
    const auto st = stat_from_qfileinfo(fi);
    emit log_message(QStringLiteral("       << stat ok: %1B mode=0x%2")
                          .arg(st.size).arg(st.mode, 4, 16, QChar('0')));
    return build_stat_reply(drfp_code::stat_reply, f.seq, 0, st);
}

QByteArray host_file_server::handle_open(
    const opentm::tm_core::drfp_frame& f) {
    using namespace opentm::tm_core;
    if (f.payload.size() < 8) {
        return build_open_reply(f.seq, kLv2Einval, 0);
    }
    const std::uint32_t flag = read_be_u32(f.payload, 0);
    const std::uint32_t mode = read_be_u32(f.payload, 4);
    const QString kit_path   = read_cstr(f.payload, 8);
    emit log_message(QStringLiteral("    -> DRFP OPEN %1 flag=0x%2 mode=0x%3 (seq=0x%4)").arg(kit_path).arg(flag, 0, 16).arg(mode, 0, 16).arg(f.seq, 8, 16, QChar('0')));

    constexpr std::uint32_t kDrfpWronly = 0x00000001u;
    constexpr std::uint32_t kDrfpRdwr   = 0x00000002u;
    constexpr std::uint32_t kDrfpCreat  = 0x00000040u;
    constexpr std::uint32_t kDrfpExcl   = 0x00000080u;
    constexpr std::uint32_t kDrfpTrunc  = 0x00000200u;
    constexpr std::uint32_t kDrfpAppend = 0x00000400u;

    const bool want_write = (flag & (kDrfpWronly | kDrfpRdwr)) != 0;

    if (!want_write && is_synthetic_param_sfo(kit_path)) {
        const std::uint32_t fd = next_fd_++;
        synth_files_[fd] = synth_handle{ build_synthetic_param_sfo(), 0 };
        emit log_message(QStringLiteral("       << synthetic PARAM.SFO opened fd=0x%1 (%2B)").arg(fd, 0, 16).arg(synth_files_[fd].content.size()));
        return build_open_reply(f.seq, 0, fd);
    }

    const QString local = resolve(kit_path);
    if (local.isEmpty()) {
        return build_open_reply(f.seq, kLv2Enoent, 0);
    }

    const bool exists = QFileInfo::exists(local);
    if (!exists && !(flag & kDrfpCreat)) {
        emit log_message(QStringLiteral("       !! %1 does not exist - ENOENT").arg(local));
        return build_open_reply(f.seq, kLv2Enoent, 0);
    }
    if (exists && (flag & kDrfpCreat) && (flag & kDrfpExcl)) {
        emit log_message(QStringLiteral("       !! %1 exists and O_EXCL set - EEXIST").arg(local));
        return build_open_reply(f.seq, kLv2Eexist, 0);
    }

    QIODevice::OpenMode mode_flags;
    if (flag & kDrfpRdwr) mode_flags = QIODevice::ReadWrite;
    else if (flag & kDrfpWronly) mode_flags = QIODevice::WriteOnly;
    else mode_flags = QIODevice::ReadOnly;
    if (want_write && (flag & kDrfpTrunc))  mode_flags |= QIODevice::Truncate;
    if (want_write && (flag & kDrfpAppend)) mode_flags |= QIODevice::Append;

    auto* file = new QFile(local);
    if (!file->open(mode_flags)) {
        emit log_message(QStringLiteral("       !! open failed: %1").arg(file->errorString()));
        delete file;
        return build_open_reply(f.seq, want_write ? kLv2Eio : kLv2Enoent, 0);
    }
    const std::uint32_t fd = next_fd_++;
    open_files_[fd] = file;
    emit log_message(QStringLiteral("       << opened fd=0x%1 (%2B, %3)").arg(fd, 0, 16).arg(file->size()).arg(want_write ? QStringLiteral("write") : QStringLiteral("read")));
    return build_open_reply(f.seq, 0, fd);
}

QByteArray host_file_server::handle_write(
    const opentm::tm_core::drfp_frame& f) {
    using namespace opentm::tm_core;
    if (f.payload.size() < 8) {
        return build_write_reply(f.seq, kLv2Einval, 0);
    }
    const std::uint32_t fd     = read_be_u32(f.payload, 0);
    const std::uint32_t nbytes = read_be_u32(f.payload, 4);
    const QByteArray data = f.payload.mid(8, static_cast<int>(nbytes));
    emit log_message(QStringLiteral("    -> DRFP WRITE fd=0x%1 nbytes=%2 (seq=0x%3)").arg(fd, 0, 16).arg(nbytes).arg(f.seq, 8, 16, QChar('0')));

    if (synth_files_.count(fd)) {
        return build_write_reply(f.seq, kLv2Ebadf, 0);
    }
    auto it = open_files_.find(fd);
    if (it == open_files_.end() || !it->second) {
        emit log_message(QStringLiteral("       !! unknown fd - EBADF"));
        return build_write_reply(f.seq, kLv2Ebadf, 0);
    }
    if (!it->second->isWritable()) {
        emit log_message(QStringLiteral("       !! fd not opened for writing - EBADF"));
        return build_write_reply(f.seq, kLv2Ebadf, 0);
    }
    const qint64 written = it->second->write(data);
    if (written < 0) {
        emit log_message(QStringLiteral("       !! write failed: %1").arg(it->second->errorString()));
        return build_write_reply(f.seq, kLv2Eio, 0);
    }
    it->second->flush();
    emit log_message(QStringLiteral("       << wrote %1B").arg(written));
    return build_write_reply(f.seq, 0, static_cast<std::uint32_t>(written));
}


QByteArray host_file_server::handle_mkdir(
    const opentm::tm_core::drfp_frame& f) {
    using namespace opentm::tm_core;
    if (f.payload.size() < 8) {
        return build_result_reply(drfp_code::mkdir_reply, f.seq, kLv2Einval);
    }
    const std::uint32_t mode = read_be_u32(f.payload, 4);
    const QString kit_path   = read_cstr(f.payload, 8);
    emit log_message(QStringLiteral("    -> DRFP MKDIR %1 mode=0x%2 (seq=0x%3)").arg(kit_path).arg(mode, 0, 16).arg(f.seq, 8, 16, QChar('0')));

    const QString local = resolve(kit_path);
    if (local.isEmpty()) {
        return build_result_reply(drfp_code::mkdir_reply, f.seq, kLv2Enoent);
    }
    if (QFileInfo::exists(local)) {
        emit log_message(QStringLiteral("       !! already exists - EEXIST"));
        return build_result_reply(drfp_code::mkdir_reply, f.seq, kLv2Eexist);
    }
    if (!QDir().mkpath(local)) {
        emit log_message(QStringLiteral("       !! mkpath failed - EIO"));
        return build_result_reply(drfp_code::mkdir_reply, f.seq, kLv2Eio);
    }
    emit log_message(QStringLiteral("       << created %1").arg(local));
    return build_result_reply(drfp_code::mkdir_reply, f.seq, 0);
}

QByteArray host_file_server::handle_rmdir(
    const opentm::tm_core::drfp_frame& f) {
    using namespace opentm::tm_core;
    const QString kit_path = read_cstr(f.payload, 0);
    emit log_message(QStringLiteral("    -> DRFP RMDIR %1 (seq=0x%2)").arg(kit_path).arg(f.seq, 8, 16, QChar('0')));

    const QString local = resolve(kit_path);
    if (local.isEmpty() || !QFileInfo::exists(local)) {
        return build_result_reply(drfp_code::rmdir_reply, f.seq, kLv2Enoent);
    }
    const QFileInfo fi(local);
    if (!fi.isDir()) {
        return build_result_reply(drfp_code::rmdir_reply, f.seq, kLv2Enotdir);
    }
    QDir d(local);
    if (!d.isEmpty()) {
        emit log_message(QStringLiteral("       !! not empty - ENOTEMPTY"));
        return build_result_reply(drfp_code::rmdir_reply, f.seq, kLv2Enotempty);
    }
    if (!d.removeRecursively()) {
        return build_result_reply(drfp_code::rmdir_reply, f.seq, kLv2Eio);
    }
    emit log_message(QStringLiteral("       << removed %1").arg(local));
    return build_result_reply(drfp_code::rmdir_reply, f.seq, 0);
}

QByteArray host_file_server::handle_dopen(
    const opentm::tm_core::drfp_frame& f) {
    using namespace opentm::tm_core;
    const QString kit_path = read_cstr(f.payload, 0);
    emit log_message(QStringLiteral("    -> DRFP OPENDIR %1 (seq=0x%2)").arg(kit_path).arg(f.seq, 8, 16, QChar('0')));

    const QString local = resolve(kit_path);
    if (local.isEmpty() || !QFileInfo::exists(local)) {
        return build_dopen_reply(f.seq, kLv2Enoent, 0);
    }
    if (!QFileInfo(local).isDir()) {
        return build_dopen_reply(f.seq, kLv2Enotdir, 0);
    }
    dir_handle h;
    h.entries = QDir(local).entryList(QDir::AllEntries | QDir::Hidden | QDir::System);
    h.local   = local;
    h.pos     = 0;
    const std::uint32_t fd = next_fd_++;
    open_dirs_[fd] = h;
    emit log_message(QStringLiteral("       << opened dir fd=0x%1 (%2 entries)").arg(fd, 0, 16).arg(h.entries.size()));
    return build_dopen_reply(f.seq, 0, fd);
}

QByteArray host_file_server::handle_dclose(
    const opentm::tm_core::drfp_frame& f) {
    using namespace opentm::tm_core;
    if (f.payload.size() < 4) {
        return build_result_reply(drfp_code::dclose_reply, f.seq, kLv2Einval);
    }
    const std::uint32_t fd = read_be_u32(f.payload, 0);
    auto it = open_dirs_.find(fd);
    if (it == open_dirs_.end()) {
        emit log_message(QStringLiteral("    -> DRFP CLOSEDIR fd=0x%1 - unknown fd").arg(fd, 0, 16));
        return build_result_reply(drfp_code::dclose_reply, f.seq, kLv2Ebadf);
    }
    open_dirs_.erase(it);
    emit log_message(QStringLiteral("    -> DRFP CLOSEDIR fd=0x%1 ok").arg(fd, 0, 16));
    return build_result_reply(drfp_code::dclose_reply, f.seq, 0);
}

QByteArray host_file_server::handle_dread(
    const opentm::tm_core::drfp_frame& f) {
    using namespace opentm::tm_core;
    if (f.payload.size() < 8) {
        return build_dread_reply(f.seq, kLv2Einval, {});
    }
    const std::uint32_t fd     = read_be_u32(f.payload, 0);
    const std::uint32_t nbytes = read_be_u32(f.payload, 4);
    auto it = open_dirs_.find(fd);
    if (it == open_dirs_.end()) {
        emit log_message(QStringLiteral("    -> DRFP READDIR fd=0x%1 - unknown fd").arg(fd, 0, 16));
        return build_dread_reply(f.seq, kLv2Ebadf, {});
    }
    auto& h = it->second;
    constexpr int kDirentSize = 258; 
    if (h.pos >= h.entries.size() || nbytes < kDirentSize) {
        emit log_message(QStringLiteral("    -> DRFP READDIR fd=0x%1 - end of directory").arg(fd, 0, 16));
        return build_dread_reply(f.seq, 0, {});
    }
    const QString name = h.entries.at(h.pos++);
    const QFileInfo fi(QDir(h.local), name);
    const std::uint8_t dtype = fi.isDir() ? drfp_dtype::directory : drfp_dtype::file;
    emit log_message(QStringLiteral("    -> DRFP READDIR fd=0x%1 -> %2 (%3)").arg(fd, 0, 16).arg(name).arg(fi.isDir() ? QStringLiteral("dir") : QStringLiteral("file")));
    return build_dread_reply(f.seq, 0, build_dirent(dtype, name.toUtf8()));
}

QByteArray host_file_server::handle_ftruncate(
    const opentm::tm_core::drfp_frame& f) {
    using namespace opentm::tm_core;
    if (f.payload.size() < 12) {
        return build_result_reply(drfp_code::ftruncate_reply, f.seq, kLv2Einval);
    }
    const std::uint32_t fd = read_be_u32(f.payload, 0);
    const std::uint64_t hi = read_be_u32(f.payload, 4);
    const std::uint64_t lo = read_be_u32(f.payload, 8);
    const qint64 size = static_cast<qint64>((hi << 32) | lo);
    emit log_message(QStringLiteral("    -> DRFP FTRUNCATE fd=0x%1 size=%2 (seq=0x%3)").arg(fd, 0, 16).arg(size).arg(f.seq, 8, 16, QChar('0')));

    if (synth_files_.count(fd)) {
        return build_result_reply(drfp_code::ftruncate_reply, f.seq, kLv2Ebadf);
    }
    auto it = open_files_.find(fd);
    if (it == open_files_.end() || !it->second) {
        return build_result_reply(drfp_code::ftruncate_reply, f.seq, kLv2Ebadf);
    }
    if (!it->second->resize(size)) {
        emit log_message(QStringLiteral("       !! resize failed: %1").arg(it->second->errorString()));
        return build_result_reply(drfp_code::ftruncate_reply, f.seq, kLv2Eio);
    }
    return build_result_reply(drfp_code::ftruncate_reply, f.seq, 0);
}

QByteArray host_file_server::handle_truncate(
    const opentm::tm_core::drfp_frame& f) {
    using namespace opentm::tm_core;
    if (f.payload.size() < 8) {
        return build_result_reply(drfp_code::truncate_reply, f.seq, kLv2Einval);
    }
    const std::uint64_t hi = read_be_u32(f.payload, 0);
    const std::uint64_t lo = read_be_u32(f.payload, 4);
    const qint64 size      = static_cast<qint64>((hi << 32) | lo);
    const QString kit_path = read_cstr(f.payload, 8);
    emit log_message(QStringLiteral("    -> DRFP TRUNCATE %1 size=%2 (seq=0x%3)").arg(kit_path).arg(size).arg(f.seq, 8, 16, QChar('0')));

    const QString local = resolve(kit_path);
    if (local.isEmpty() || !QFileInfo::exists(local)) {
        return build_result_reply(drfp_code::truncate_reply, f.seq, kLv2Enoent);
    }
    if (QFileInfo(local).isDir()) {
        return build_result_reply(drfp_code::truncate_reply, f.seq, kLv2Eisdir);
    }
    QFile file(local);
    if (!file.open(QIODevice::ReadWrite) || !file.resize(size)) {
        emit log_message(QStringLiteral("       !! truncate failed: %1").arg(file.errorString()));
        return build_result_reply(drfp_code::truncate_reply, f.seq, kLv2Eio);
    }
    return build_result_reply(drfp_code::truncate_reply, f.seq, 0);
}

QByteArray host_file_server::handle_rename(
    const opentm::tm_core::drfp_frame& f) {
    using namespace opentm::tm_core;
    const QString old_path = read_cstr(f.payload, 0);
    const int new_off = old_path.toUtf8().size() + 1;
    const QString new_path = read_cstr(f.payload, new_off);
    emit log_message(QStringLiteral("    -> DRFP RENAME %1 -> %2 (seq=0x%3)").arg(old_path).arg(new_path).arg(f.seq, 8, 16, QChar('0')));

    const QString from = resolve(old_path);
    const QString to   = resolve(new_path);
    if (from.isEmpty() || to.isEmpty()) {
        return build_result_reply(drfp_code::rename_reply, f.seq, kLv2Enoent);
    }
    if (!QFileInfo::exists(from)) {
        return build_result_reply(drfp_code::rename_reply, f.seq, kLv2Enoent);
    }
    if (QFileInfo::exists(to)) {
        return build_result_reply(drfp_code::rename_reply, f.seq, kLv2Eexist);
    }
    if (!QFile::rename(from, to)) {
        emit log_message(QStringLiteral("       !! rename failed - EIO"));
        return build_result_reply(drfp_code::rename_reply, f.seq, kLv2Eio);
    }
    emit log_message(QStringLiteral("       << renamed"));
    return build_result_reply(drfp_code::rename_reply, f.seq, 0);
}

QByteArray host_file_server::handle_unlink(
    const opentm::tm_core::drfp_frame& f) {
    using namespace opentm::tm_core;
    const QString kit_path = read_cstr(f.payload, 0);
    emit log_message(QStringLiteral("    -> DRFP UNLINK %1 (seq=0x%2)").arg(kit_path).arg(f.seq, 8, 16, QChar('0')));

    const QString local = resolve(kit_path);
    if (local.isEmpty() || !QFileInfo::exists(local)) {
        return build_result_reply(drfp_code::unlink_reply, f.seq, kLv2Enoent);
    }
    if (QFileInfo(local).isDir()) {
        emit log_message(QStringLiteral("       !! is a directory - EISDIR"));
        return build_result_reply(drfp_code::unlink_reply, f.seq, kLv2Eisdir);
    }
    if (!QFile::remove(local)) {
        emit log_message(QStringLiteral("       !! remove failed - EIO"));
        return build_result_reply(drfp_code::unlink_reply, f.seq, kLv2Eio);
    }
    emit log_message(QStringLiteral("       << deleted %1").arg(local));
    return build_result_reply(drfp_code::unlink_reply, f.seq, 0);
}

QByteArray host_file_server::handle_close(
    const opentm::tm_core::drfp_frame& f) {
    using namespace opentm::tm_core;
    if (f.payload.size() < 4) {
        return build_result_reply(drfp_code::close_reply, f.seq, kLv2Einval);
    }
    const std::uint32_t fd = read_be_u32(f.payload, 0);
    if (auto sit = synth_files_.find(fd); sit!= synth_files_.end()) {
        synth_files_.erase(sit);
        emit log_message(QStringLiteral("    -> DRFP CLOSE fd=0x%1 ok (synthetic)").arg(fd, 0, 16));
        return build_result_reply(drfp_code::close_reply, f.seq, 0);
    }
    auto it = open_files_.find(fd);
    if (it == open_files_.end()) {
        emit log_message(QStringLiteral("    -> DRFP CLOSE fd=0x%1 (seq=0x%2) - unknown fd").arg(fd, 0, 16).arg(f.seq, 8, 16, QChar('0')));
        return build_result_reply(drfp_code::close_reply, f.seq, kLv2Ebadf);
    }
    it->second->close();
    delete it->second;
    open_files_.erase(it);
    emit log_message(QStringLiteral("    -> DRFP CLOSE fd=0x%1 ok").arg(fd, 0, 16));
    return build_result_reply(drfp_code::close_reply, f.seq, 0);
}

QByteArray host_file_server::handle_read(
    const opentm::tm_core::drfp_frame& f) {
    using namespace opentm::tm_core;
    if (f.payload.size() < 8) {
        return build_read_reply(f.seq, kLv2Einval, {});
    }
    const std::uint32_t fd     = read_be_u32(f.payload, 0);
    const std::uint32_t nbytes = read_be_u32(f.payload, 4);
    const std::uint32_t cap    = (nbytes > 0x4000u) ? 0x4000u : nbytes;
    if (auto sit = synth_files_.find(fd); sit!= synth_files_.end()) {
        auto& h = sit->second;
        const qint64 remaining = h.content.size() - h.pos;
        const qint64 take = qMin<qint64>(static_cast<qint64>(cap), qMax<qint64>(0, remaining));
        QByteArray data = h.content.mid(static_cast<int>(h.pos), static_cast<int>(take));
        h.pos += take;
        emit log_message(QStringLiteral("    -> DRFP READ fd=0x%1 nbytes=%2 (asked=%3, synthetic) seq=0x%4").arg(fd, 0, 16).arg(data.size()).arg(nbytes).arg(f.seq, 8, 16, QChar('0')));
        return build_read_reply(f.seq, 0, data);
    }
    auto it = open_files_.find(fd);
    if (it == open_files_.end()) {
        return build_read_reply(f.seq, kLv2Ebadf, {});
    }
    QByteArray data = it->second->read(static_cast<qint64>(cap));
    emit log_message(QStringLiteral("    -> DRFP READ fd=0x%1 nbytes=%2 (asked=%3) seq=0x%4").arg(fd, 0, 16).arg(data.size()).arg(nbytes).arg(f.seq, 8, 16, QChar('0')));
    return build_read_reply(f.seq, 0, data);
}

QByteArray host_file_server::handle_fstat(
    const opentm::tm_core::drfp_frame& f) {
    using namespace opentm::tm_core;
    if (f.payload.size() < 4) {
        return build_result_reply(drfp_code::fstat_reply, f.seq, kLv2Einval);
    }
    const std::uint32_t fd = read_be_u32(f.payload, 0);
    if (auto sit = synth_files_.find(fd); sit!= synth_files_.end()) {
        const auto st = synthetic_stat(sit->second.content.size());
        emit log_message(QStringLiteral("    -> DRFP FSTAT fd=0x%1 size=%2 (synthetic)").arg(fd, 0, 16).arg(st.size));
        return build_stat_reply(drfp_code::fstat_reply, f.seq, 0, st);
    }
    auto it = open_files_.find(fd);
    if (it == open_files_.end()) {
        return build_result_reply(drfp_code::fstat_reply, f.seq, kLv2Ebadf);
    }
    const QFileInfo fi(*it->second);
    const auto st = stat_from_qfileinfo(fi);
    emit log_message(QStringLiteral("    -> DRFP FSTAT fd=0x%1 size=%2").arg(fd, 0, 16).arg(st.size));
    return build_stat_reply(drfp_code::fstat_reply, f.seq, 0, st);
}

QByteArray host_file_server::handle_seek(
    const opentm::tm_core::drfp_frame& f) {
    using namespace opentm::tm_core;
    if (f.payload.size() < 16) {
        return build_result_reply(drfp_code::seek_reply, f.seq, kLv2Einval);
    }
    const std::uint32_t fd      = read_be_u32(f.payload, 0);
    const std::uint32_t off_hi  = read_be_u32(f.payload, 4);
    const std::uint32_t off_lo  = read_be_u32(f.payload, 8);
    const std::uint32_t base    = read_be_u32(f.payload, 12);
    const std::uint64_t off64   = (static_cast<std::uint64_t>(off_hi) << 32) | off_lo;

    qint64 target = 0;
    if (auto sit = synth_files_.find(fd); sit!= synth_files_.end()) {
        auto& h = sit->second;
        switch (base) {
        case 0: target = static_cast<qint64>(off64); break;
        case 1: target = h.pos + static_cast<qint64>(off64); break;
        case 2: target = h.content.size() + static_cast<qint64>(off64); break;
        default:
            return build_result_reply(drfp_code::seek_reply, f.seq, kLv2Einval);
        }
        if (target < 0 || target > h.content.size()) {
            return build_result_reply(drfp_code::seek_reply, f.seq, kLv2Einval);
        }
        h.pos = target;
        return build_drfp(drfp_code::seek_reply, f.seq, seek_reply_body(target));
    }
    auto it = open_files_.find(fd);
    if (it == open_files_.end()) {
        return build_result_reply(drfp_code::seek_reply, f.seq, kLv2Ebadf);
    }
    switch (base) {
    case 0: target = static_cast<qint64>(off64); break;
    case 1: target = it->second->pos() + static_cast<qint64>(off64); break;
    case 2: target = it->second->size() + static_cast<qint64>(off64); break; 
    default:
        return build_result_reply(drfp_code::seek_reply, f.seq, kLv2Einval);
    }
    if (!it->second->seek(target)) {
        return build_result_reply(drfp_code::seek_reply, f.seq, kLv2Einval);
    }
    emit log_message(QStringLiteral("    -> DRFP SEEK fd=0x%1 base=%2 -> %3").arg(fd, 0, 16).arg(base).arg(target));
    return build_drfp(drfp_code::seek_reply, f.seq, seek_reply_body(target));
}

} // namespace opentm::tm_ui
