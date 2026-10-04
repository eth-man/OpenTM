// opentm_cli - one-shot DECI3 driver.
//
//   opentm_cli --host 192.0.2.11 --type dex --ls /dev_hdd0/
//   opentm_cli --host 192.0.2.10 --serve C:/work/build --load build/foo.self
//   opentm_cli --host 192.0.2.10 --power-on --wait-agent --load build/foo.self
//              --pass-on "RESULT: OK" --fail-on "ASSERT"
//
// The exit code reports the operation, not the connection:
//   0 everything asked for succeeded
//   1 usage error
//   2 could not reach the console, or the session never came up
//   3 timed out waiting for the agent, a reply or a pattern
//   4 the target refused an operation (lv2 status, transfer result)
//   5 --fail-on matched

#include <tm_session/target_session.h>

#include <tm_core/target_type.h>

#include <csignal>

#include <atomic>

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QTextStream>
#include <QTimer>

namespace {

enum cli_exit {
    exit_ok = 0, exit_usage = 1, exit_unreachable = 2,
    exit_timeout = 3, exit_refused = 4, exit_pattern = 5
};

std::atomic<bool> g_interrupted{false};

extern "C" void on_signal(int) { g_interrupted.store(true); }

QTextStream& out() {
    static QTextStream s(stdout);
    return s;
}

void emit_line(const QString& tag, const QString& line) {
    out() << tag << line << Qt::endl;
    out().flush();
}

bool split_pair(const QString& text, QString* left, QString* right, bool host_is_left) {
    const int at = host_is_left ? text.lastIndexOf(QLatin1Char(':')) : text.indexOf(QLatin1Char(':'));
    if (at <= 0 || at == text.size() - 1) return false;
    *left  = text.left(at);
    *right = text.mid(at + 1);
    return !left->isEmpty();
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("opentm_cli"));

    QCommandLineParser p;
    p.setApplicationDescription(QStringLiteral(
        "Headless DECI3 driver for PS3 devkits and DEX consoles.\n\n"
        "Actions run in a fixed order whatever order they are given in:\n"
        "  power, reset -> mkdir -> upload -> download -> rename -> chmod ->\n"
        "  touch -> rm -> ls -> ps -> settings -> install -> load -> watch\n\n"
        "Exit codes: 0 ok, 1 usage, 2 unreachable, 3 timeout, 4 target refused,\n"
        "5 --fail-on matched."));
    p.addHelpOption();

    const QCommandLineOption o_host({"H", "host"}, "Target IP or hostname.", "host");
    const QCommandLineOption o_port({"P", "port"}, "TCP port (default: per type).", "port");
    const QCommandLineOption o_type({"t", "type"}, "decr | dex (default decr).", "type", "decr");
    const QCommandLineOption o_serve("serve", "Host dir exposed as /app_home/.", "dir");

    const QCommandLineOption o_power_on("power-on", "Power the target on.");
    const QCommandLineOption o_power_off("power-off", "Shut the target down.");
    const QCommandLineOption o_force("force", "Make --power-off a forced power off.");
    const QCommandLineOption o_reset("reset", "Reset the target using its configured mode.");
    const QCommandLineOption o_wol("wol", "Send a Wake on LAN packet (needs --mac).");
    const QCommandLineOption o_mac("mac", "Target MAC, for --wol.", "mac");

    const QCommandLineOption o_wait("wait-agent", "Run the actions once the debug agent is up, not at session ready.");
    const QCommandLineOption o_ls("ls", "List a kit directory.", "path");
    const QCommandLineOption o_load("load", "LOAD_EXT this SELF.", "path");
    const QCommandLineOption o_install("install", "Install a .pkg.", "path");
    const QCommandLineOption o_ps("ps", "Fetch the process list.");

    const QCommandLineOption o_upload("upload", "Push HOST:KIT.", "host:kit");
    const QCommandLineOption o_download("download", "Pull KIT:HOST.", "kit:host");
    const QCommandLineOption o_mkdir("mkdir", "Create a directory on the target.", "path");
    const QCommandLineOption o_rm("rm", "Delete a file or directory.", "path");
    const QCommandLineOption o_mv("mv", "Rename FROM:TO on the target.", "from:to");
    const QCommandLineOption o_chmod("chmod", "Set permissions, MODE:PATH with MODE octal.", "mode:path");
    const QCommandLineOption o_touch("touch", "Set both timestamps to now.", "path");

    const QCommandLineOption o_sref("settings-refresh", "Dump the target's XMB settings.");
    const QCommandLineOption o_scommit("settings-commit", "Send the XMB settings commit.");
    const QCommandLineOption o_sapply("settings-apply", "Send the XMB settings transfer for FILE.", "file");

    const QCommandLineOption o_pass("pass-on", "Exit 0 as soon as target output matches this regex.", "regex");
    const QCommandLineOption o_fail("fail-on", "Exit 5 as soon as target output matches this regex.", "regex");
    const QCommandLineOption o_tty_out("tty-out", "Also write target output to this file.", "file");

    const QCommandLineOption o_timeout("timeout", "Give up reaching the console after N seconds (default 15).", "sec", "15");
    const QCommandLineOption o_run_timeout("run-timeout", "Give up waiting for replies or patterns after N more seconds (default 60).", "sec", "60");
    const QCommandLineOption o_linger("linger", "Keep reading N seconds after the last reply (default 5).", "sec", "5");
    const QCommandLineOption o_quiet("quiet", "Suppress per-frame wire lines.");

    for (const auto& opt : {o_host, o_port, o_type, o_serve, o_power_on, o_power_off, o_force, o_reset, o_wol, o_mac, o_wait, o_ls, o_load, o_install, o_ps, o_upload, o_download, o_mkdir, o_rm, o_mv, o_chmod, o_touch, o_sref, o_scommit, o_sapply, o_pass, o_fail, o_tty_out, o_timeout, o_run_timeout, o_linger, o_quiet}) {
        p.addOption(opt);
    }
    p.process(app);

    if (!p.isSet(o_host)) {
        out() << "error: --host is required" << Qt::endl;
        return exit_usage;
    }
    const QString type_s = p.value(o_type).toLower();
    if (type_s != QLatin1String("decr") && type_s != QLatin1String("dex")) {
        out() << "error: --type must be decr or dex" << Qt::endl;
        return exit_usage;
    }
    const bool is_dex = (type_s == QLatin1String("dex"));
    const auto ttype  = is_dex ? opentm::tm_core::target_type::cfw_dex : opentm::tm_core::target_type::decr_tcp;
    const quint16 port = p.isSet(o_port) ? static_cast<quint16>(p.value(o_port).toUShort()) : (is_dex ? 1000 : 8530);

    QRegularExpression pass_re(p.value(o_pass));
    QRegularExpression fail_re(p.value(o_fail));
    if (p.isSet(o_pass) && !pass_re.isValid()) {
        out() << "error: bad --pass-on regex" << Qt::endl; return exit_usage;
    }
    if (p.isSet(o_fail) && !fail_re.isValid()) {
        out() << "error: bad --fail-on regex" << Qt::endl; return exit_usage;
    }

    QString up_from, up_to, dl_from, dl_to, mv_from, mv_to, chmod_mode, chmod_path;
    if (p.isSet(o_upload) && !split_pair(p.value(o_upload), &up_from, &up_to, true)) {
        out() << "error: --upload wants HOST:KIT" << Qt::endl; return exit_usage;
    }
    if (p.isSet(o_download) && !split_pair(p.value(o_download), &dl_from, &dl_to, false)) {
        out() << "error: --download wants KIT:HOST" << Qt::endl; return exit_usage;
    }
    if (p.isSet(o_mv) && !split_pair(p.value(o_mv), &mv_from, &mv_to, false)) {
        out() << "error: --mv wants FROM:TO" << Qt::endl; return exit_usage;
    }
    if (p.isSet(o_chmod) && !split_pair(p.value(o_chmod), &chmod_mode, &chmod_path, false)) {
        out() << "error: --chmod wants MODE:PATH" << Qt::endl; return exit_usage;
    }
    if (p.isSet(o_upload) && !QFileInfo::exists(up_from)) {
        out() << "error: no such file: " << up_from << Qt::endl; return exit_usage;
    }

    QFile tty_file;
    if (p.isSet(o_tty_out)) {
        tty_file.setFileName(p.value(o_tty_out));
        if (!tty_file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
            out() << "error: cannot write " << tty_file.fileName() << Qt::endl;
            return exit_usage;
        }
    }

    using namespace opentm::tm_ui;

    target_session ts;

    int  code     = exit_unreachable;   // until a session proves otherwise
    bool acted    = false;
    struct { int transfer = 0, listing = 0, file_op = 0, load = 0, install = 0, ps = 0; } owed;
    auto pending_total = [&owed]() {
        return owed.transfer + owed.listing + owed.file_op + owed.load + owed.install + owed.ps;
    };
    bool settled  = false;
    bool agent_up   = false;   // the debug agent announced itself, or is assumed up
    bool sess_ready = false;   // the DECI3 session handshake has completed
    const bool watching = p.isSet(o_pass) || p.isSet(o_fail);

    auto quit_now = [&app, &settled]() {
        if (settled) return;
        settled = true;
        QTimer::singleShot(0, &app, &QCoreApplication::quit);
    };
    auto linger_then_quit = [&]() {
        if (settled) return;
        QTimer::singleShot(p.value(o_linger).toInt() * 1000, &app, &QCoreApplication::quit);
    };

    // one reply accounted for; the worst result wins. Events we did not ask for
    // (the agent pushes its own settings transfer) are ignored, not counted.
    auto done_one = [&](int& counter, int result) {
        if (counter <= 0) return;
        --counter;
        if (result != exit_ok && code == exit_ok) code = result;
        if (pending_total() <= 0 && !watching) linger_then_quit();
    };

    if (!p.isSet(o_quiet)) {
        QObject::connect(&ts, &target_session::wire_line, [](const QString& l) { emit_line(QStringLiteral("    "), l); });
    }
    QObject::connect(&ts, &target_session::log_message, [](const QString& l) { emit_line(QString(), l); });
    QObject::connect(&ts, &target_session::error, [](const QString& l) { emit_line(QStringLiteral("!!  "), l); });
    QObject::connect(&ts, &target_session::status_message, [](const QString& l) { emit_line(QStringLiteral("--  "), l); });

    const bool needs_files = p.isSet(o_ls) || p.isSet(o_upload) || p.isSet(o_download)
                             || p.isSet(o_mkdir) || p.isSet(o_rm) || p.isSet(o_mv)
                             || p.isSet(o_chmod) || p.isSet(o_touch)
                             || p.isSet(o_sref) || p.isSet(o_sapply) || p.isSet(o_scommit);
    const bool needs_dbgp = p.isSet(o_load) || p.isSet(o_install) || p.isSet(o_ps);

    QObject::connect(&ts, &target_session::protocol_rejected, [&](quint32 proto, QString name) {
        const bool fatal = (proto == 0x00000100u && needs_files) || (proto == 0x00000200u && (needs_dbgp || needs_files));
        if (fatal) {
            emit_line(QStringLiteral("!!  "),QStringLiteral("%1 is owned by another connection - close the other target ""manager, or wait for the kit to drop the stale session").arg(name));
            code = exit_unreachable;
            quit_now();
        }
    });

    QObject::connect(&ts, &target_session::tty_text, [&](const QString& t) {
        for (const auto& l : t.split('\n', Qt::SkipEmptyParts)) {
            const QString line = l.trimmed();
            emit_line(QStringLiteral("TTY | "), line);
            if (tty_file.isOpen()) {
                tty_file.write(line.toUtf8());
                tty_file.write("\n");
                tty_file.flush();
            }
            if (settled) continue;
            if (p.isSet(o_fail) && fail_re.match(line).hasMatch()) {
                emit_line(QStringLiteral(">>> "), QStringLiteral("--fail-on matched"));
                code = exit_pattern;
                quit_now();
            } else if (p.isSet(o_pass) && pass_re.match(line).hasMatch()) {
                emit_line(QStringLiteral(">>> "), QStringLiteral("--pass-on matched"));
                if (code == exit_ok || code == exit_unreachable) code = exit_ok;
                quit_now();
            }
        }
    });

    QObject::connect(&ts, &target_session::load_ext_reply, [&](std::uint32_t status) {
        emit_line(QStringLiteral(">>> "), QStringLiteral("load reply: lv2 status 0x%1").arg(status, 8, 16, QChar('0')));
        done_one(owed.load, status == 0 ? exit_ok : exit_refused);
    });
    QObject::connect(&ts, &target_session::install_reply, [&](std::uint32_t status) {
        emit_line(QStringLiteral(">>> "), QStringLiteral("install reply: lv2 status 0x%1").arg(status, 8, 16, QChar('0')));
        done_one(owed.install, status == 0 ? exit_ok : exit_refused);
    });
    QObject::connect(&ts, &target_session::file_op_finished, [&](QString op, quint32 status) {
        emit_line(QStringLiteral(">>> "), status == 0 ? QStringLiteral("%1 ok").arg(op) : QStringLiteral("%1 failed, status 0x%2").arg(op).arg(status, 8, 16, QChar('0')));
        done_one(owed.file_op, status == 0 ? exit_ok : exit_refused);
    });
    QObject::connect(&ts, &target_session::transfer_finished, [&]() {
        if (owed.transfer <= 0) return;   // the agent's own settings push
        emit_line(QStringLiteral(">>> "), QStringLiteral("transfer complete"));
        done_one(owed.transfer, exit_ok);
    });
    QObject::connect(&ts, &target_session::transfer_failed, [&](std::uint32_t result) {
        emit_line(QStringLiteral("!!  "), QStringLiteral("transfer refused, result=0x%1").arg(result, 0, 16));
        done_one(owed.transfer, exit_refused);
    });
    QObject::connect(&ts, &target_session::directory_listed, [&](const QString& path, std::vector<opentm::tm_core::dfmp_file_entry> entries) {
        emit_line(QStringLiteral(">>> "), QStringLiteral("%1 -> %2 entries").arg(path).arg(static_cast<int>(entries.size())));
        for (const auto& e : entries) {
            emit_line(QStringLiteral("      "), QStringLiteral("%1%2  %3B").arg(QString::fromStdString(e.name), e.is_directory() ? QStringLiteral("/") : QString()).arg(e.size));
        }
        done_one(owed.listing, exit_ok);
    });
    QObject::connect(&ts, &target_session::process_list_ready, [&](QList<session_api::process_summary> ps) {
        emit_line(QStringLiteral(">>> "), QStringLiteral("%1 process(es)").arg(ps.size()));
        for (const auto& s : ps) {
            emit_line(QStringLiteral("      "), QStringLiteral("pid 0x%1  %2").arg(s.pid, 8, 16, QChar('0')).arg(QString::fromStdString(s.info.self_path)));
        }
        done_one(owed.ps, exit_ok);
    });

    target_record rec;
    rec.name            = QStringLiteral("cli");
    rec.type            = ttype;
    rec.host            = p.value(o_host);
    rec.port            = port;
    rec.file_server_dir = p.value(o_serve);
    rec.mac             = p.value(o_mac);
    ts.set_target(rec);

    if (p.isSet(o_wol)) {
        if (rec.mac.isEmpty()) {
            out() << "error: --wol needs --mac" << Qt::endl;
            return exit_usage;
        }
        emit_line(QStringLiteral(">>> "), QStringLiteral("wake on lan -> %1").arg(rec.mac));
        ts.wake_on_lan();
    }

    bool powered = false;
    auto run_power = [&]() {
        if (powered) return;
        powered = true;
        if (code == exit_unreachable) code = exit_ok;
        if (p.isSet(o_power_on)) {
            emit_line(QStringLiteral(">>> "), QStringLiteral("power on"));
            ts.power_on();
        }
    };

    auto run_actions = [&]() {
        if (acted) return;
        acted = true;
        if (code == exit_unreachable) code = exit_ok;


        if (p.isSet(o_power_off)) {
            emit_line(QStringLiteral(">>> "), p.isSet(o_force) ? QStringLiteral("power off (forced)") : QStringLiteral("power off"));
            if (p.isSet(o_force)) ts.power_off_force(); else ts.power_off();
        }

        const bool reset_with_load = p.isSet(o_reset) && p.isSet(o_load);
        if (p.isSet(o_reset) && !reset_with_load) {
            emit_line(QStringLiteral(">>> "), QStringLiteral("reset"));
            ts.reset_current();
        }
        if (p.isSet(o_mkdir)) {
            emit_line(QStringLiteral(">>> "), QStringLiteral("mkdir %1").arg(p.value(o_mkdir)));
            ++owed.file_op; ts.make_directory(p.value(o_mkdir), 0777u);
        }
        if (p.isSet(o_upload)) {
            emit_line(QStringLiteral(">>> "), QStringLiteral("upload %1 -> %2").arg(up_from, up_to));
            ++owed.transfer; ts.upload_file(up_from, up_to);
        }
        if (p.isSet(o_download)) {
            emit_line(QStringLiteral(">>> "), QStringLiteral("download %1 -> %2").arg(dl_from, dl_to));
            ++owed.transfer; ts.download_file(dl_from, dl_to, 0);
        }
        if (p.isSet(o_mv)) {
            emit_line(QStringLiteral(">>> "), QStringLiteral("rename %1 -> %2").arg(mv_from, mv_to));
            ++owed.file_op; ts.rename_file(mv_from, mv_to);
        }
        if (p.isSet(o_chmod)) {
            bool mode_ok = false;
            const auto mode = chmod_mode.toUInt(&mode_ok, 8);
            if (!mode_ok) {
                emit_line(QStringLiteral("!!  "), QStringLiteral("--chmod mode must be octal"));
                code = exit_usage;
            } else {
                emit_line(QStringLiteral(">>> "), QStringLiteral("chmod 0%1 %2").arg(mode, 0, 8).arg(chmod_path));
                ++owed.file_op; ts.set_permissions(chmod_path, mode);
            }
        }
        if (p.isSet(o_touch)) {
            const auto now = static_cast<std::uint64_t>(QDateTime::currentSecsSinceEpoch());
            emit_line(QStringLiteral(">>> "), QStringLiteral("touch %1").arg(p.value(o_touch)));
            ++owed.file_op; ts.set_times(p.value(o_touch), now, now);
        }
        if (p.isSet(o_rm)) {
            emit_line(QStringLiteral(">>> "), QStringLiteral("rm %1").arg(p.value(o_rm)));
            ++owed.file_op; ts.delete_file(p.value(o_rm));
        }
        if (p.isSet(o_ls)) {
            emit_line(QStringLiteral(">>> "), QStringLiteral("ls %1").arg(p.value(o_ls)));
            ++owed.listing; ts.list_directory(p.value(o_ls));
        }
        if (p.isSet(o_ps)) {
            emit_line(QStringLiteral(">>> "), QStringLiteral("process list"));
            ++owed.ps; ts.refresh_process_list();
        }
        if (p.isSet(o_sref)) {
            emit_line(QStringLiteral(">>> "), QStringLiteral("settings refresh"));
            ts.settings_refresh();
        }
        if (p.isSet(o_sapply)) {
            const QString f = p.value(o_sapply);
            const auto sz = static_cast<std::uint32_t>(QFileInfo(f).size());
            emit_line(QStringLiteral(">>> "), QStringLiteral("settings apply %1 (%2B) - NO reset").arg(f).arg(sz));
            ts.settings_apply(f, sz);
            QTimer::singleShot(900, &app, [&ts] { ts.settings_commit(); });
        } else if (p.isSet(o_scommit)) {
            emit_line(QStringLiteral(">>> "), QStringLiteral("settings commit"));
            ts.settings_commit();
        }
        if (p.isSet(o_install)) {
            emit_line(QStringLiteral(">>> "), QStringLiteral("install %1").arg(p.value(o_install)));
            ++owed.install; ts.install_package(p.value(o_install));
        }
        if (p.isSet(o_load)) {
            load_options opts;
            opts.reset_target = reset_with_load;
            emit_line(QStringLiteral(">>> "), reset_with_load ? QStringLiteral("reset, then load %1 when the agent is back").arg(p.value(o_load)) : QStringLiteral("load %1").arg(p.value(o_load)));
            ++owed.load; ts.load_executable(p.value(o_load), opts);
        }

        if (pending_total() == 0 && !watching) linger_then_quit();
    };

    // Actions need a live session. The agent announcement can land before the
    // handshake finishes, so --wait-agent alone isn't enough: fire once both are
    // up, or the verbs bail "session not ready yet" and the run hangs.
    auto maybe_run_actions = [&]() {
        if (acted || settled) return;
        if (!sess_ready) return;
        if (p.isSet(o_wait) && !agent_up) return;
        run_actions();
    };

    QObject::connect(&ts, &target_session::session_ready, [&](std::uint16_t tok, std::uint16_t sub) {
        emit_line(QStringLiteral(">>> "), QStringLiteral("session ready (token=0x%1 sub=0x%2)").arg(tok, 4, 16, QChar('0')).arg(sub, 4, 16, QChar('0')));
        if (code == exit_unreachable) code = exit_ok;
        sess_ready = true;
        run_power();
        maybe_run_actions();
    });
    QObject::connect(&ts, &target_session::debug_agent_ready, [&]() {
        emit_line(QStringLiteral(">>> "), QStringLiteral("debug agent ready"));
        agent_up = true;
        maybe_run_actions();
    });

    const int timeout_s = p.value(o_timeout).toInt();
    QTimer::singleShot(timeout_s * 1000, &app, [&]() {
        if (acted || settled) return;
        if (p.isSet(o_wait) && code == exit_ok) {
            emit_line(QStringLiteral(">>> "), QStringLiteral("no agent announcement in %1s - it was probably already up, continuing").arg(timeout_s));
            agent_up = true;
            maybe_run_actions();
            return;
        }
        emit_line(QStringLiteral("!!  "), QStringLiteral("timeout after %1s waiting for the console").arg(timeout_s));
        if (code == exit_ok) code = exit_timeout;
        quit_now();
    });

    const int run_timeout_s = p.value(o_run_timeout).toInt();
    QTimer::singleShot((timeout_s + run_timeout_s) * 1000, &app, [&]() {
        if (settled) return;
        if (pending_total() > 0 || watching) {
            emit_line(QStringLiteral("!!  "), QStringLiteral("timeout after %1s waiting for a reply").arg(run_timeout_s));
            code = exit_timeout;
        }
        quit_now();
    });

    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);
    QTimer interrupt_poll;
    QObject::connect(&interrupt_poll, &QTimer::timeout, &app, [&]() {
        if (!g_interrupted.load()) return;
        emit_line(QStringLiteral("!!  "), QStringLiteral("interrupted - deregistering before exit"));
        if (code == exit_ok) code = exit_timeout;
        quit_now();
    });
    interrupt_poll.start(100);

    emit_line(QStringLiteral(">>> "), QStringLiteral("connect %1:%2 (%3)").arg(rec.host).arg(port).arg(type_s));
    ts.connect_to_target();

    app.exec();
    ts.disconnect_from_target();
    if (tty_file.isOpen()) tty_file.close();
    out() << (code == exit_ok ? QStringLiteral(">>> done") : QStringLiteral(">>> FAILED (exit %1)").arg(code)) << Qt::endl;
    out().flush();
    return code;
}
