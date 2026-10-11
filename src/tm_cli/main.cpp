// opentm_cli - one-shot DECI3 driver.
//
// opentm_cli --host 192.0.2.11 --type dex --ls /dev_hdd0/
// opentm_cli --host 192.0.2.10 --serve C:/work/build --load build/foo.self
// opentm_cli --host 192.0.2.10 --power-on --wait-agent --load build/foo.self
// --pass-on "RESULT: OK" --fail-on "ASSERT"
//
// The exit code reports the operation, not the connection:
// 0 everything asked for succeeded
// 1 usage error
// 2 could not reach the console, or the session never came up
// 3 timed out waiting for the agent, a reply or a pattern
// 4 the target refused an operation (lv2 status, transfer result)
// 5 --fail-on matched

#include <tm_session/target_session.h>

#include <tm_core/target_type.h>
#include <tm_core/dbg_error.h>
#include <tm_core/ps3mapi_client.h>
#include <tm_core/nor_image.h>
#include <tm_core/regist_codec.h>

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

// format an lv2dbg result code as "0x???????? (meaning)" when we know it (dbg_error.h)
QString lv2_status(std::uint32_t code) {
    const char* name = opentm::tm_core::lv2dbg_error_name(code);
    QString hex = QStringLiteral("0x%1").arg(code, 8, 16, QChar('0'));
    return name ? hex + QStringLiteral(" (") + QString::fromLatin1(name) + QChar(')') : hex;
}

bool split_pair(const QString& text, QString* left, QString* right, bool host_is_left) {
    const int at = host_is_left ? text.lastIndexOf(QLatin1Char(':')) : text.indexOf(QLatin1Char(':'));
    if (at <= 0 || at == text.size() - 1) return false;
    *left  = text.left(at);
    *right = text.mid(at + 1);
    return !left->isEmpty();
}

// Print a classic hex + ASCII dump, base address `base`.
void hexdump(std::uint64_t base, const QByteArray& b) {
    for (int i = 0; i < b.size(); i += 16) {
        QString line = QStringLiteral("%1  ").arg(base + i, 8, 16, QLatin1Char('0'));
        QString ascii;
        for (int j = 0; j < 16; ++j) {
            if (i + j < b.size()) {
                const std::uint8_t c = std::uint8_t(b[i + j]);
                line += QStringLiteral("%1 ").arg(c, 2, 16, QLatin1Char('0'));
                ascii += (c >= 0x20 && c < 0x7f) ? QChar(c) : QChar('.');
            } else {
                line += QStringLiteral("   ");
            }
            if (j == 7) line += QLatin1Char(' ');
        }
        emit_line(QString(), line + QLatin1Char(' ') + ascii);
    }
}

// Parse ADDR[:LEN] (hex or 0x), LEN default 0x100.
bool parse_addr_len(const QString& v, std::uint64_t* addr, std::uint32_t* len) {
    const int c = v.indexOf(QLatin1Char(':'));
    bool a = false, l = true;
    *addr = (c < 0 ? v : v.left(c)).toULongLong(&a, 0);
    *len  = (c < 0) ? 0x100u : v.mid(c + 1).toUInt(&l, 0);
    return a && l && *len != 0;
}

// The lv1/lv2/lv0 backend -- below what DECI3 can reach. Independent of the
// DECI3 session: lv1/lv2 go over PS3MAPI HTTP, lv0 is read from a file. Returns
// an exit code; sets *handled if any of these flags was present.
int run_hv_backend(const QCommandLineParser& p,
                   const QCommandLineOption& o_lv1_peek, const QCommandLineOption& o_lv2_peek,
                   const QCommandLineOption& o_lv1_poke, const QCommandLineOption& o_lv2_poke,
                   const QCommandLineOption& o_lv0_read, const QCommandLineOption& o_unsafe,
                   const QCommandLineOption& o_out, const QCommandLineOption& o_host,
                   const QCommandLineOption& o_mapi_port, const QCommandLineOption& o_timeout,
                   bool* handled) {
    using namespace opentm::tm_core;
    const bool want_lv0  = p.isSet(o_lv0_read);
    const bool want_live = p.isSet(o_lv1_peek) || p.isSet(o_lv2_peek)
                           || p.isSet(o_lv1_poke) || p.isSet(o_lv2_poke);
    *handled = want_lv0 || want_live;
    if (!*handled) return exit_ok;

    const bool unsafe = p.isSet(o_unsafe);

    // ---- lv0 (file-backed, no console) ----
    if (want_lv0) {
        // --lv0-read FILE[@ADDR[:LEN]] or --lv0-read FILE with --peek-style via o_lv0_read value
        // Form: FILE:ADDR[:LEN] (FILE may contain no ':' on these paths; use '@' to be safe)
        const QString v = p.value(o_lv0_read);
        int at = v.indexOf(QLatin1Char('@'));
        if (at < 0) {
            emit_line(QStringLiteral("!!  "), QStringLiteral(
                "--lv0-read wants FILE@ADDR[:LEN], e.g. extracted/lv0-493.elf@0x8007a4:0x40"));
            return exit_usage;
        }
        const QString file = v.left(at);
        std::uint64_t addr = 0; std::uint32_t len = 0;
        if (!parse_addr_len(v.mid(at + 1), &addr, &len)) {
            emit_line(QStringLiteral("!!  "), QStringLiteral("--lv0-read: bad ADDR[:LEN]"));
            return exit_usage;
        }
        nor_image img;
        QString err;
        if (!img.open(file, &err)) { emit_line(QStringLiteral("!!  "), err); return exit_usage; }
        emit_line(QStringLiteral("--  "), img.summary());
        QByteArray data;
        if (!img.read_va(addr, len, &data, &err)) { emit_line(QStringLiteral("!!  "), err); return exit_refused; }
        if (p.isSet(o_out)) {
            QFile f(p.value(o_out));
            if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                emit_line(QStringLiteral("!!  "), QStringLiteral("cannot write %1").arg(p.value(o_out)));
                return exit_usage;
            }
            f.write(data);
            emit_line(QStringLiteral("--  "), QStringLiteral("wrote %1 bytes -> %2").arg(data.size()).arg(p.value(o_out)));
        } else {
            hexdump(addr, data);
        }
        return exit_ok;
    }

    // ---- live lv1 / lv2 (PS3MAPI HTTP) ----
    if (!p.isSet(o_host)) {
        emit_line(QStringLiteral("!!  "), QStringLiteral("error: --host is required for --lv1-peek/--lv2-peek/--lv1-poke/--lv2-poke"));
        return exit_usage;
    }
    const quint16 mport = p.isSet(o_mapi_port) ? quint16(p.value(o_mapi_port).toUShort()) : quint16(80);
    const int tmo = p.value(o_timeout).toInt() * 1000;
    ps3mapi_client cli(p.value(o_host), mport, tmo > 0 ? tmo : 10000);

    // A read is only trustworthy once the lv1 probe matches; run it first.
    mapi_result chk = cli.self_check();
    if (!chk.ok) {
        emit_line(QStringLiteral("!!  "), QStringLiteral("PS3MAPI self-check failed: %1").arg(chk.error));
        emit_line(QStringLiteral("--  "), QStringLiteral("is webMAN/PS3MAPI running on the console? (http://%1/)").arg(p.value(o_host)));
        return exit_unreachable;
    }
    emit_line(QStringLiteral("--  "), QStringLiteral("PS3MAPI reachable; lv1 probe OK"));

    auto do_peek = [&](mem_space sp, const QString& v, const char* tag) -> int {
        std::uint64_t addr = 0; std::uint32_t len = 0;
        if (!parse_addr_len(v, &addr, &len)) {
            emit_line(QStringLiteral("!!  "), QStringLiteral("%1 wants ADDR[:LEN]").arg(tag));
            return exit_usage;
        }
        mapi_result r = cli.read(sp, addr, len, unsafe);
        if (!r.ok) { emit_line(QStringLiteral("!!  "), r.error); return exit_refused; }
        hexdump(addr, r.data);
        return exit_ok;
    };
    auto do_poke = [&](mem_space sp, const QString& v, const char* tag) -> int {
        if (!unsafe) {
            emit_line(QStringLiteral("!!  "), QStringLiteral("%1 writes live memory; add --unsafe to confirm").arg(tag));
            return exit_usage;
        }
        const int eq = v.indexOf(QLatin1Char('='));
        if (eq <= 0) { emit_line(QStringLiteral("!!  "), QStringLiteral("%1 wants ADDR=HEXBYTES").arg(tag)); return exit_usage; }
        bool ok = false;
        const std::uint64_t addr = v.left(eq).toULongLong(&ok, 0);
        const QByteArray bytes = QByteArray::fromHex(v.mid(eq + 1).toLatin1());
        if (!ok || bytes.isEmpty()) { emit_line(QStringLiteral("!!  "), QStringLiteral("%1: bad ADDR=HEXBYTES").arg(tag)); return exit_usage; }
        mapi_result r = cli.write(sp, addr, bytes, unsafe);
        if (!r.ok) { emit_line(QStringLiteral("!!  "), r.error); return exit_refused; }
        emit_line(QStringLiteral("--  "), QStringLiteral("wrote %1 at %2 (verified)").arg(QString::fromLatin1(bytes.toHex())).arg(addr, 0, 16));
        return exit_ok;
    };

    int code = exit_ok, rc;
    if (p.isSet(o_lv1_peek) && (rc = do_peek(mem_space::lv1, p.value(o_lv1_peek), "--lv1-peek")) != exit_ok) code = rc;
    if (p.isSet(o_lv2_peek) && (rc = do_peek(mem_space::lv2, p.value(o_lv2_peek), "--lv2-peek")) != exit_ok) code = rc;
    if (p.isSet(o_lv1_poke) && (rc = do_poke(mem_space::lv1, p.value(o_lv1_poke), "--lv1-poke")) != exit_ok) code = rc;
    if (p.isSet(o_lv2_poke) && (rc = do_poke(mem_space::lv2, p.value(o_lv2_poke), "--lv2-poke")) != exit_ok) code = rc;
    return code;
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
    const QCommandLineOption o_reset_release("reset-release", "Reset the target into Release (normal retail) boot, not Debug (boot=0x01).");
    const QCommandLineOption o_wol("wol", "Send a Wake on LAN packet (needs --mac).");
    const QCommandLineOption o_mac("mac", "Target MAC, for --wol.", "mac");

    const QCommandLineOption o_wait("wait-agent", "Run the actions once the debug agent is up, not at session ready.");
    const QCommandLineOption o_ls("ls", "List a kit directory.", "path");
    const QCommandLineOption o_load("load", "LOAD_EXT this SELF.", "path");
    const QCommandLineOption o_debug_module("debug-module", "Load with the debug module enabled (LOAD_EXT debug_flags bit 0), so the process is REGISTERED with the debugger and shows in --ps / is readable. Without it a loaded process is not debuggable and per-process reads return 0xffffffff.");
    const QCommandLineOption o_attach("attach", "Attach the debugger to an already-running process (dbgshl attach_process 0x80000002); PID hex or dec.", "pid");
    const QCommandLineOption o_install("install", "Install a .pkg.", "path");
    const QCommandLineOption o_ps("ps", "Fetch the process list.");
    const QCommandLineOption o_probe("probe", "Report which target backends answer (session/TTY, debugger, settings).");
    const QCommandLineOption o_registered("registered", "List the DECI3 protocols registered on this connection (from the target's register ACKs).");
    const QCommandLineOption o_peek("peek", "Read target memory: ADDR[:LEN] in hex (LEN default 0x100).", "addr:len");
    const QCommandLineOption o_pid("pid", "Target process id for --peek/--dump (default 0 = whole system).", "pid", "0");
    const QCommandLineOption o_dump("dump", "Dump target memory to a file: ADDR:LEN in hex (chunked read_memory).", "addr:len");
    const QCommandLineOption o_dump_out("out", "Output file for --dump (default dump_<addr>_<len>.bin).", "file");
    const QCommandLineOption o_debug_read("debug-read", "Persistent debug session: --load <SELF> --debug-read ADDR[:LEN] reads the loaded process's memory in ONE session (load+debug -> process list -> read). This is the correct debugger order; plain --peek cannot read a process because registration is torn down at disconnect.", "addr:len");
    const QCommandLineOption o_ppuregs("ppu-regs", "Read a PPU thread's registers (GPRs/pc/lr/cr/ctr/xer); needs --tid.");
    const QCommandLineOption o_tid("tid", "PPU/SPU thread id for --ppu-regs/--spu-regs/--spu-ls (hex or dec).", "tid");
    const QCommandLineOption o_spuregs("spu-regs", "Read an SPU thread's registers (128 GPRs + npc/spu_status); needs --tid.");
    const QCommandLineOption o_spuls("spu-ls", "Dump an SPU thread's full 256 KB local store to a file; needs --tid (--out for the path).");
    const QCommandLineOption o_threads("threads", "List a process's PPU threads (id/state/name) for --pid; pick a tid for --ppu-regs.");

    // --- execution control: DABR hardware watchpoint + code breakpoints (needs --pid, usually --attach first) ---
    const QCommandLineOption o_watch("watch", "Set the DABR hardware data watchpoint: ADDR[:r|w] (default w=store). Breaks when a LOAD/STORE touches ADDR. One DABR per process; use --pid.", "addr:rw");
    const QCommandLineOption o_watch_clear("watch-clear", "Clear the DABR data watchpoint (sets ctrl=CLEAR); use --pid.");
    const QCommandLineOption o_watch_get("watch-get", "Read back the current DABR address + control flags; use --pid.");
    const QCommandLineOption o_bp("bp", "Set a PPU code breakpoint at ADDR (hex); needs --tid and --pid.", "addr");
    const QCommandLineOption o_bp_clear("bp-clear", "Clear the PPU code breakpoint at ADDR (hex); needs --tid and --pid.", "addr");
    const QCommandLineOption o_bp_list("bp-list", "List the PPU breakpoints for --pid (reply format unverified; raw payload shown).");
    const QCommandLineOption o_spu_bp("spu-bp", "Set an SPU code breakpoint at ADDR (hex); needs --tid and --pid.", "addr");
    const QCommandLineOption o_spu_bp_clear("spu-bp-clear", "Clear the SPU code breakpoint at ADDR (hex); needs --tid and --pid.", "addr");

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

    // --- hypervisor / lv0 backend (below DECI3; live reads need a CFW console) ---
    const QCommandLineOption o_lv1_peek("lv1-peek", "Read lv1 (hypervisor) memory over PS3MAPI: ADDR[:LEN] in hex. Guarded against unmapped addresses; add --unsafe to override one vetted address.", "addr:len");
    const QCommandLineOption o_lv2_peek("lv2-peek", "Read lv2 (kernel) memory over PS3MAPI: ADDR[:LEN] in hex (kernel VA high half 0x80000000 is masked).", "addr:len");
    const QCommandLineOption o_lv1_poke("lv1-poke", "Write lv1 memory over PS3MAPI: ADDR=HEXBYTES. Needs --unsafe. Read back and verified.", "addr=bytes");
    const QCommandLineOption o_lv2_poke("lv2-poke", "Write lv2 memory over PS3MAPI: ADDR=HEXBYTES. Needs --unsafe. Read back and verified.", "addr=bytes");
    const QCommandLineOption o_lv0_read("lv0-read", "Read lv0 from a decrypted image FILE by virtual address: FILE@ADDR[:LEN]. Offline, no console (lv0 is not live-reachable). --out to dump to a file.", "file@addr:len");
    const QCommandLineOption o_unsafe("unsafe", "Allow an lv1 read/write the guard would refuse, and enable --lv1-poke/--lv2-poke. For ONE vetted address, never a loop.");
    const QCommandLineOption o_mapi_port("mapi-port", "HTTP port for PS3MAPI (default 80).", "port");
    // --lv0-read / --dump share the one "out" option (o_dump_out) -- a second QCommandLineOption
    // with the long name "out" would be a duplicate and be rejected.

    for (const auto& opt : {o_host, o_port, o_type, o_serve, o_power_on, o_power_off, o_force, o_reset, o_reset_release, o_wol, o_mac, o_wait, o_ls, o_load, o_debug_module, o_attach, o_install, o_ps, o_upload, o_download, o_mkdir, o_rm, o_mv, o_chmod, o_touch, o_sref, o_scommit, o_sapply, o_pass, o_fail, o_tty_out, o_timeout, o_run_timeout, o_linger, o_quiet, o_probe, o_registered, o_peek, o_pid, o_dump, o_dump_out, o_debug_read, o_ppuregs, o_tid, o_spuregs, o_spuls, o_threads, o_lv1_peek, o_lv2_peek, o_lv1_poke, o_lv2_poke, o_lv0_read, o_unsafe, o_mapi_port, o_watch, o_watch_clear, o_watch_get, o_bp, o_bp_clear, o_bp_list, o_spu_bp, o_spu_bp_clear}) {
        p.addOption(opt);
    }
    p.process(app);

    // The lv1/lv2/lv0 backend is independent of the DECI3 session: handle it and
    // exit before any target handshake. lv0 reads need no console at all.
    {
        bool handled = false;
        const int rc = run_hv_backend(p, o_lv1_peek, o_lv2_peek, o_lv1_poke, o_lv2_poke,
                                      o_lv0_read, o_unsafe, o_dump_out, o_host, o_mapi_port, o_timeout, &handled);
        if (handled) return rc;
    }

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

    int  code     = exit_unreachable; // until a session proves otherwise
    bool acted    = false;
    struct { int transfer = 0, listing = 0, file_op = 0, load = 0, install = 0, ps = 0, peek = 0; } owed;
    auto pending_total = [&owed]() {
        return owed.transfer + owed.listing + owed.file_op + owed.load + owed.install + owed.ps + owed.peek;
    };
    bool settled  = false;
    bool agent_up   = false; // the debug agent announced itself, or is assumed up
    bool sess_ready = false; // the DECI3 session handshake has completed
    const bool registered_mode = p.isSet(o_registered);
    const bool probe_mode = p.isSet(o_probe) || registered_mode;
    // --probe drives its own timed window and prints a summary, so it must not
    // settle early the way a plain one-shot action does.
    const bool watching = p.isSet(o_pass) || p.isSet(o_fail) || probe_mode;
    bool probe_dbg_ok = false; // debugger answered a process-list request
    bool probe_settings_ok = false; // the agent pushed its settings file back
    bool probe_printed = false;

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
    const bool needs_dbgp = p.isSet(o_load) || p.isSet(o_attach) || p.isSet(o_install) || p.isSet(o_ps) || p.isSet(o_peek) || p.isSet(o_dump) || p.isSet(o_debug_read) || p.isSet(o_ppuregs) || p.isSet(o_spuregs) || p.isSet(o_spuls) || p.isSet(o_threads)
                            || p.isSet(o_watch) || p.isSet(o_watch_clear) || p.isSet(o_watch_get) || p.isSet(o_bp) || p.isSet(o_bp_clear) || p.isSet(o_bp_list) || p.isSet(o_spu_bp) || p.isSet(o_spu_bp_clear);

    quint64 peek_addr = 0; quint32 peek_len = 0x100; quint32 peek_pid = 0;
    if (p.isSet(o_peek)) {
        const QString v = p.value(o_peek);
        const int c = v.indexOf(QLatin1Char(':'));
        bool ok1 = false, ok2 = true;
        peek_addr = (c < 0 ? v : v.left(c)).toULongLong(&ok1, 0);
        if (c >= 0) peek_len = v.mid(c + 1).toUInt(&ok2, 0);
        peek_pid = p.value(o_pid).toUInt(nullptr, 0);
        if (!ok1 || !ok2 || peek_len == 0) {
            out() << "error: --peek wants ADDR[:LEN], e.g. 0x10000:0x100" << Qt::endl;
            return exit_usage;
        }
    }

    // --dump: chunked read_memory to a file. Reuses the proven read_memory channel.
    // --debug-read: the correct persistent-session read. load(debug) -> process list -> read, all in
    // one connection. Needs --load to spawn+register the target in THIS session.
    bool   dbg_mode = p.isSet(o_debug_read);
    quint64 dbg_addr = 0; quint32 dbg_len = 0x100;
    if (dbg_mode) {
        if (!p.isSet(o_load)) { out() << "error: --debug-read needs --load <SELF> (the process to spawn and read)" << Qt::endl; return exit_usage; }
        if (p.isSet(o_peek) || p.isSet(o_dump)) { out() << "error: --debug-read is its own session; do not combine with --peek/--dump" << Qt::endl; return exit_usage; }
        const QString v = p.value(o_debug_read);
        const int c = v.indexOf(QChar(':'));
        bool ok1 = false, ok2 = true;
        dbg_addr = (c < 0 ? v : v.left(c)).toULongLong(&ok1, 0);
        if (c >= 0) dbg_len = v.mid(c + 1).toUInt(&ok2, 0);
        if (!ok1 || !ok2 || dbg_len == 0) { out() << "error: --debug-read wants ADDR[:LEN], e.g. 0x10000:0x100" << Qt::endl; return exit_usage; }
    }

    quint64 dump_addr = 0, dump_len = 0, dump_got = 0; quint32 dump_pid = 0;
    const quint32 dump_chunk = 0x800; // per-request read; short replies are handled by advancing by actual size
    QString dump_out;
    QFile dump_file;
    bool dump_active = false;
    if (p.isSet(o_dump)) {
        if (p.isSet(o_peek)) { out() << "error: use --peek OR --dump, not both" << Qt::endl; return exit_usage; }
        const QString v = p.value(o_dump);
        const int c = v.indexOf(QLatin1Char(':'));
        bool ok1 = false, ok2 = false;
        dump_addr = (c < 0 ? v : v.left(c)).toULongLong(&ok1, 0);
        if (c >= 0) dump_len = v.mid(c + 1).toULongLong(&ok2, 0);
        dump_pid = p.value(o_pid).toUInt(nullptr, 0);
        if (!ok1 || !ok2 || dump_len == 0) {
            out() << "error: --dump wants ADDR:LEN, e.g. 0x10000:0x4000" << Qt::endl;
            return exit_usage;
        }
        dump_out = p.isSet(o_dump_out) ? p.value(o_dump_out)
                 : QStringLiteral("dump_%1_%2.bin").arg(dump_addr, 0, 16).arg(dump_len, 0, 16);
    }
    auto dump_request_next = [&]() {
        const quint64 rem = dump_len - dump_got;
        const quint32 n = static_cast<quint32>(std::min<quint64>(dump_chunk, rem));
        ts.read_memory(dump_pid, dump_addr + dump_got, n);
    };

    // --ppu-regs: read a PPU thread's register context.
    quint64 regs_tid = 0; quint32 regs_pid = 0;
    if (p.isSet(o_ppuregs)) {
        if (!p.isSet(o_tid)) { out() << "error: --ppu-regs needs --tid <thread id>" << Qt::endl; return exit_usage; }
        bool okt = false;
        regs_tid = p.value(o_tid).toULongLong(&okt, 0);
        regs_pid = p.value(o_pid).toUInt(nullptr, 0);
        if (!okt) { out() << "error: --tid wants a thread id, e.g. 0x1 or 1" << Qt::endl; return exit_usage; }
    }

    // --spu-regs: read an SPU thread's register context.
    quint64 sregs_tid = 0; quint32 sregs_pid = 0;
    if (p.isSet(o_spuregs)) {
        if (!p.isSet(o_tid)) { out() << "error: --spu-regs needs --tid <thread id>" << Qt::endl; return exit_usage; }
        bool okt = false;
        sregs_tid = p.value(o_tid).toULongLong(&okt, 0);
        sregs_pid = p.value(o_pid).toUInt(nullptr, 0);
        if (!okt) { out() << "error: --tid wants a thread id" << Qt::endl; return exit_usage; }
    }

    // --spu-ls: chunked dump of an SPU thread's local store to a file.
    quint64 sl_tid = 0, sl_len = 0x40000, sl_got = 0; const quint64 sl_off = 0; quint32 sl_pid = 0;
    QString sl_out; QFile sl_file;
    if (p.isSet(o_spuls)) {
        if (!p.isSet(o_tid)) { out() << "error: --spu-ls needs --tid <thread id>" << Qt::endl; return exit_usage; }
        bool okt = false;
        sl_tid = p.value(o_tid).toULongLong(&okt, 0);
        sl_pid = p.value(o_pid).toUInt(nullptr, 0);
        if (!okt) { out() << "error: --tid wants a thread id" << Qt::endl; return exit_usage; }
        sl_out = p.isSet(o_dump_out) ? p.value(o_dump_out) : QStringLiteral("spuls_tid%1.bin").arg(sl_tid, 0, 16);
    }
    auto spuls_request_next = [&]() {
        const quint64 rem = sl_len - sl_got;
        const quint32 n = static_cast<quint32>(std::min<quint64>(0x800, rem));
        ts.read_spu_local_store(sl_pid, sl_tid, static_cast<quint32>(sl_off + sl_got), n);
    };

    // --- DABR watchpoint + code breakpoints ---
    quint32 ctl_pid = p.value(o_pid).toUInt(nullptr, 0);
    quint64 ctl_tid = 0; bool ctl_has_tid = p.isSet(o_tid);
    if (ctl_has_tid) { bool okt = false; ctl_tid = p.value(o_tid).toULongLong(&okt, 0);
        if (!okt) { out() << "error: --tid wants a thread id" << Qt::endl; return exit_usage; } }

    quint64 wp_addr = 0, wp_ctrl = opentm::tm_core::dbgp::dabr_ctrl::write;
    if (p.isSet(o_watch)) {
        const QString v = p.value(o_watch);
        const int c = v.indexOf(QLatin1Char(':'));
        bool oka = false;
        wp_addr = (c < 0 ? v : v.left(c)).toULongLong(&oka, 0);
        if (c >= 0) {
            const QString rw = v.mid(c + 1).toLower();
            if (rw == QLatin1String("r")) wp_ctrl = opentm::tm_core::dbgp::dabr_ctrl::read;
            else if (rw == QLatin1String("w")) wp_ctrl = opentm::tm_core::dbgp::dabr_ctrl::write;
            else { out() << "error: --watch access must be r or w" << Qt::endl; return exit_usage; }
        }
        if (!oka) { out() << "error: --watch wants ADDR[:r|w]" << Qt::endl; return exit_usage; }
    }
    // breakpoint address parser (hex), shared by ppu/spu set/clear
    auto parse_bp = [&](const QCommandLineOption& o, quint64* out_addr) -> bool {
        bool ok = false; *out_addr = p.value(o).toULongLong(&ok, 0); return ok;
    };
    quint64 bp_addr = 0, bpc_addr = 0, sbp_addr = 0, sbpc_addr = 0;
    const bool any_bp = p.isSet(o_bp) || p.isSet(o_bp_clear) || p.isSet(o_spu_bp) || p.isSet(o_spu_bp_clear);
    if (any_bp && !ctl_has_tid) { out() << "error: breakpoints need --tid <thread id>" << Qt::endl; return exit_usage; }
    if (p.isSet(o_bp)        && !parse_bp(o_bp, &bp_addr))         { out() << "error: --bp wants ADDR (hex)" << Qt::endl; return exit_usage; }
    if (p.isSet(o_bp_clear)  && !parse_bp(o_bp_clear, &bpc_addr))  { out() << "error: --bp-clear wants ADDR (hex)" << Qt::endl; return exit_usage; }
    if (p.isSet(o_spu_bp)    && !parse_bp(o_spu_bp, &sbp_addr))    { out() << "error: --spu-bp wants ADDR (hex)" << Qt::endl; return exit_usage; }
    if (p.isSet(o_spu_bp_clear) && !parse_bp(o_spu_bp_clear, &sbpc_addr)) { out() << "error: --spu-bp-clear wants ADDR (hex)" << Qt::endl; return exit_usage; }

    const bool any_ctl = p.isSet(o_watch) || p.isSet(o_watch_clear) || p.isSet(o_watch_get) || any_bp || p.isSet(o_bp_list);
    // Watchpoints/breakpoints target a REGISTERED process. A freshly --load --debug-module'd
    // process's pid is only known from the load reply, and registration is torn
    // down at disconnect, so when a control op is combined with a debug-load and no
    // explicit --pid, fire it in the SAME session against the load-captured pid -- like --debug-read.
    const bool ctl_load_mode = any_ctl && p.isSet(o_load) && p.isSet(o_debug_module) && !p.isSet(o_pid);
    quint32 loaded_pid = 0;
    auto count_control_ops = [&]() {
        return int(p.isSet(o_watch)) + int(p.isSet(o_watch_clear)) + int(p.isSet(o_watch_get))
             + int(p.isSet(o_bp)) + int(p.isSet(o_bp_clear)) + int(p.isSet(o_bp_list))
             + int(p.isSet(o_spu_bp)) + int(p.isSet(o_spu_bp_clear));
    };
    // Issue the requested control ops against `pid` (no owed accounting here; the caller reserves).
    auto fire_control_ops = [&](quint32 pid) {
        if (p.isSet(o_watch))       { emit_line(QStringLiteral(">>> "), QStringLiteral("watch pid=0x%1 addr=0x%2 ctrl=0x%3").arg(pid, 0, 16).arg(wp_addr, 0, 16).arg(wp_ctrl, 0, 16)); ts.set_data_watchpoint(pid, wp_addr, wp_ctrl); }
        if (p.isSet(o_watch_clear)) { emit_line(QStringLiteral(">>> "), QStringLiteral("watch-clear pid=0x%1").arg(pid, 0, 16)); ts.set_data_watchpoint(pid, 0, opentm::tm_core::dbgp::dabr_ctrl::clear); }
        if (p.isSet(o_watch_get))   { emit_line(QStringLiteral(">>> "), QStringLiteral("watch-get pid=0x%1").arg(pid, 0, 16)); ts.get_data_watchpoint(pid); }
        if (p.isSet(o_bp))          { emit_line(QStringLiteral(">>> "), QStringLiteral("ppu-bp set pid=0x%1 tid=0x%2 addr=0x%3").arg(pid, 0, 16).arg(ctl_tid, 0, 16).arg(bp_addr, 0, 16)); ts.set_breakpoint(pid, ctl_tid, bp_addr, false); }
        if (p.isSet(o_bp_clear))    { emit_line(QStringLiteral(">>> "), QStringLiteral("ppu-bp clear pid=0x%1 tid=0x%2 addr=0x%3").arg(pid, 0, 16).arg(ctl_tid, 0, 16).arg(bpc_addr, 0, 16)); ts.clear_breakpoint(pid, ctl_tid, bpc_addr, false); }
        if (p.isSet(o_spu_bp))      { emit_line(QStringLiteral(">>> "), QStringLiteral("spu-bp set pid=0x%1 tid=0x%2 addr=0x%3").arg(pid, 0, 16).arg(ctl_tid, 0, 16).arg(sbp_addr, 0, 16)); ts.set_breakpoint(pid, ctl_tid, sbp_addr, true); }
        if (p.isSet(o_spu_bp_clear)){ emit_line(QStringLiteral(">>> "), QStringLiteral("spu-bp clear pid=0x%1 tid=0x%2 addr=0x%3").arg(pid, 0, 16).arg(ctl_tid, 0, 16).arg(sbpc_addr, 0, 16)); ts.clear_breakpoint(pid, ctl_tid, sbpc_addr, true); }
        if (p.isSet(o_bp_list))     { emit_line(QStringLiteral(">>> "), QStringLiteral("bp-list pid=0x%1").arg(pid, 0, 16)); ts.list_breakpoints(pid, false); }
    };

    // In ctl_load_mode the control ops wait for the debug-load to register the process, then fire
    // against the pid the load reply carried (reserved as owed.peek in run_actions).
    if (ctl_load_mode) {
        QObject::connect(&ts, &target_session::load_ext_pid, [&](std::uint32_t pid) { loaded_pid = pid; });
        QObject::connect(&ts, &target_session::load_ext_reply, [&](std::uint32_t status) {
            if (status != 0) {
                // load failed (generic handler reports it); release the reserved control slots so
                // the session settles now instead of hanging to the run-timeout.
                for (int i = 0, n = count_control_ops(); i < n; ++i) done_one(owed.peek, exit_refused);
                return;
            }
            const quint32 pid = loaded_pid ? loaded_pid : ctl_pid;
            emit_line(QStringLiteral(">>> "), QStringLiteral("debug-load OK, firing control ops against pid=0x%1").arg(pid, 0, 16));
            fire_control_ops(pid);
        });
    }

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
        emit_line(QStringLiteral(">>> "), QStringLiteral("load reply: lv2 status %1").arg(lv2_status(status)));
        done_one(owed.load, status == 0 ? exit_ok : exit_refused);
    });
    QObject::connect(&ts, &target_session::install_reply, [&](std::uint32_t status) {
        emit_line(QStringLiteral(">>> "), QStringLiteral("install reply: lv2 status %1").arg(lv2_status(status)));
        done_one(owed.install, status == 0 ? exit_ok : exit_refused);
    });
    QObject::connect(&ts, &target_session::file_op_finished, [&](QString op, quint32 status) {
        emit_line(QStringLiteral(">>> "), status == 0 ? QStringLiteral("%1 ok").arg(op) : QStringLiteral("%1 failed, status 0x%2").arg(op).arg(status, 8, 16, QChar('0')));
        done_one(owed.file_op, status == 0 ? exit_ok : exit_refused);
    });
    QObject::connect(&ts, &target_session::transfer_finished, [&]() {
        if (owed.transfer <= 0) return; // the agent's own settings push
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
    QObject::connect(&ts, &target_session::memory_read_ready, [&](std::uint64_t addr, QByteArray data) {
        if (dump_active) {
            dump_file.write(data);
            dump_got += static_cast<quint64>(data.size());
            if (data.isEmpty() || dump_got >= dump_len) {
                dump_file.close();
                emit_line(QStringLiteral(">>> "), QStringLiteral("dumped 0x%1 / 0x%2 bytes -> %3")
                          .arg(dump_got, 0, 16).arg(dump_len, 0, 16).arg(dump_out));
                done_one(owed.peek, (dump_got >= dump_len) ? exit_ok : exit_refused);
            } else {
                dump_request_next();
            }
            return;
        }
        emit_line(QStringLiteral(">>> "), QStringLiteral("read 0x%1 bytes @ 0x%2").arg(data.size(), 0, 16).arg(addr, 16, 16, QChar('0')));
        for (int i = 0; i < data.size(); i += 16) {
            const int n = static_cast<int>(std::min<qsizetype>(16, data.size() - i));
            QString hex, asc;
            for (int j = 0; j < n; ++j) {
                const auto b = static_cast<std::uint8_t>(data[i + j]);
                hex += QStringLiteral("%1 ").arg(b, 2, 16, QChar('0'));
                asc += (b >= 0x20 && b < 0x7f) ? QChar(b) : QChar('.');
            }
            emit_line(QStringLiteral("      "), QStringLiteral("%1  %2  %3").arg(addr + i, 16, 16, QChar('0')).arg(hex, -48).arg(asc));
        }
        done_one(owed.peek, exit_ok);
    });
    QObject::connect(&ts, &target_session::memory_read_failed, [&](std::uint64_t addr, std::uint32_t result) {
        if (dump_active) {
            dump_file.close();
            emit_line(QStringLiteral("!!  "), QStringLiteral("dump stopped at 0x%1 (0x%2 B written) -> %3, lv2 status 0x%4")
                      .arg(addr, 0, 16).arg(dump_got, 0, 16).arg(dump_out).arg(result, 8, 16, QChar('0')));
            done_one(owed.peek, exit_refused);
            return;
        }
        emit_line(QStringLiteral("!!  "), QStringLiteral("read @ 0x%1 refused, lv2 status %2").arg(addr, 16, 16, QChar('0')).arg(lv2_status(result)));
        done_one(owed.peek, exit_refused);
    });
    QObject::connect(&ts, &target_session::ppu_regs_ready, [&](std::uint64_t tid, QByteArray d) {
        // register-context offsets from(sys_dbg_ppu_thread_context_t, big-endian)
        auto be64 = [&](int off) -> quint64 { quint64 v = 0; for (int i = 0; i < 8 && off + i < d.size(); ++i) v = (v << 8) | static_cast<quint8>(d[off + i]); return v; };
        auto be32 = [&](int off) -> quint32 { quint32 v = 0; for (int i = 0; i < 4 && off + i < d.size(); ++i) v = (v << 8) | static_cast<quint8>(d[off + i]); return v; };
        emit_line(QStringLiteral(">>> "), QStringLiteral("ppu regs tid=0x%1 (%2 bytes)").arg(tid, 0, 16).arg(d.size()));
        for (int i = 0; i < 32; i += 4) {
            emit_line(QStringLiteral("      "), QStringLiteral("r%1=%2  r%3=%4  r%5=%6  r%7=%8")
                .arg(i).arg(be64(i * 8), 16, 16, QChar('0'))
                .arg(i + 1).arg(be64((i + 1) * 8), 16, 16, QChar('0'))
                .arg(i + 2).arg(be64((i + 2) * 8), 16, 16, QChar('0'))
                .arg(i + 3).arg(be64((i + 3) * 8), 16, 16, QChar('0')));
        }
        emit_line(QStringLiteral("      "), QStringLiteral("pc=%1 lr=%2 ctr=%3 xer=%4 cr=%5")
            .arg(be64(0x120), 16, 16, QChar('0')).arg(be64(0x110), 16, 16, QChar('0'))
            .arg(be64(0x118), 16, 16, QChar('0')).arg(be64(0x108), 16, 16, QChar('0'))
            .arg(be32(0x100), 8, 16, QChar('0')));
        // raw head for silicon verification of the wire layout
        emit_line(QStringLiteral("      "), QStringLiteral("raw[0:32]=%1").arg(QString::fromLatin1(d.left(32).toHex(' '))));
        done_one(owed.peek, exit_ok);
    });
    QObject::connect(&ts, &target_session::ppu_regs_failed, [&](std::uint64_t tid, std::uint32_t result) {
        emit_line(QStringLiteral("!!  "), QStringLiteral("ppu regs tid=0x%1 refused, lv2 status %2").arg(tid, 0, 16).arg(lv2_status(result)));
        done_one(owed.peek, exit_refused);
    });
    QObject::connect(&ts, &target_session::spu_regs_ready, [&](std::uint64_t tid, QByteArray d) {
        // SPU context: gpr[128] x 16B at 0x000; scalar fields after 0x800 (offsets alignment-assumed, verify).
        auto be32 = [&](int off) -> quint32 { quint32 v = 0; for (int i = 0; i < 4 && off + i < d.size(); ++i) v = (v << 8) | static_cast<quint8>(d[off + i]); return v; };
        emit_line(QStringLiteral(">>> "), QStringLiteral("spu regs tid=0x%1 (%2 bytes)").arg(tid, 0, 16).arg(d.size()));
        emit_line(QStringLiteral("      "), QStringLiteral("npc=%1 spu_status=%2 srr0=%3")
            .arg(be32(0x800), 8, 16, QChar('0')).arg(be32(0x824), 8, 16, QChar('0')).arg(be32(0x820), 8, 16, QChar('0')));
        for (int i = 0; i < 128 && (i * 16 + 16) <= d.size(); ++i)
            emit_line(QStringLiteral("      "), QStringLiteral("r%1 = %2").arg(i, 3).arg(QString::fromLatin1(d.mid(i * 16, 16).toHex(' '))));
        emit_line(QStringLiteral("      "), QStringLiteral("raw[0x800:32]=%1").arg(QString::fromLatin1(d.mid(0x800, 32).toHex(' '))));
        done_one(owed.peek, exit_ok);
    });
    QObject::connect(&ts, &target_session::spu_regs_failed, [&](std::uint64_t tid, std::uint32_t result) {
        emit_line(QStringLiteral("!!  "), QStringLiteral("spu regs tid=0x%1 refused, lv2 status %2").arg(tid, 0, 16).arg(lv2_status(result)));
        done_one(owed.peek, exit_refused);
    });
    QObject::connect(&ts, &target_session::spu_ls_ready, [&](std::uint64_t off, QByteArray data) {
        (void)off;
        sl_file.write(data);
        sl_got += static_cast<quint64>(data.size());
        if (data.isEmpty() || sl_got >= sl_len) {
            sl_file.close();
            emit_line(QStringLiteral(">>> "), QStringLiteral("spu-ls dumped 0x%1 / 0x%2 bytes -> %3").arg(sl_got, 0, 16).arg(sl_len, 0, 16).arg(sl_out));
            done_one(owed.peek, (sl_got >= sl_len) ? exit_ok : exit_refused);
        } else {
            spuls_request_next();
        }
    });
    QObject::connect(&ts, &target_session::spu_ls_failed, [&](std::uint64_t off, std::uint32_t result) {
        sl_file.close();
        emit_line(QStringLiteral("!!  "), QStringLiteral("spu-ls stopped at 0x%1 (0x%2 B written) -> %3, lv2 status 0x%4").arg(off, 0, 16).arg(sl_got, 0, 16).arg(sl_out).arg(result, 8, 16, QChar('0')));
        done_one(owed.peek, exit_refused);
    });
    QObject::connect(&ts, &target_session::watchpoint_set_done, [&](std::uint32_t result) {
        if (result == 0) emit_line(QStringLiteral(">>> "), QStringLiteral("watchpoint set OK"));
        else             emit_line(QStringLiteral("!!  "), QStringLiteral("watchpoint set failed, %1").arg(lv2_status(result)));
        done_one(owed.peek, result == 0 ? exit_ok : exit_refused);
    });
    QObject::connect(&ts, &target_session::watchpoint_info, [&](std::uint64_t addr, std::uint64_t ctrl, std::uint32_t result) {
        if (result != 0) { emit_line(QStringLiteral("!!  "), QStringLiteral("watch-get failed, %1").arg(lv2_status(result))); done_one(owed.peek, exit_refused); return; }
        const char* k = (ctrl == opentm::tm_core::dbgp::dabr_ctrl::read) ? "read" : (ctrl == opentm::tm_core::dbgp::dabr_ctrl::write) ? "write" : (ctrl == 0 ? "clear/none" : "?");
        emit_line(QStringLiteral(">>> "), QStringLiteral("DABR addr=0x%1 ctrl=0x%2 (%3)").arg(addr, 16, 16, QChar('0')).arg(ctrl, 0, 16).arg(k));
        done_one(owed.peek, exit_ok);
    });
    QObject::connect(&ts, &target_session::breakpoint_done, [&](std::uint64_t addr, std::uint32_t result) {
        if (result == 0) emit_line(QStringLiteral(">>> "), QStringLiteral("breakpoint @0x%1 OK").arg(addr, 0, 16));
        else             emit_line(QStringLiteral("!!  "), QStringLiteral("breakpoint @0x%1 failed, %2").arg(addr, 0, 16).arg(lv2_status(result)));
        done_one(owed.peek, result == 0 ? exit_ok : exit_refused);
    });
    QObject::connect(&ts, &target_session::breakpoints_listed, [&](QByteArray raw, std::uint32_t result) {
        if (result != 0) { emit_line(QStringLiteral("!!  "), QStringLiteral("bp-list failed, %1").arg(lv2_status(result))); done_one(owed.peek, exit_refused); return; }
        emit_line(QStringLiteral(">>> "), QStringLiteral("bp-list reply %1 bytes (format unverified): %2")
            .arg(raw.size()).arg(QString::fromLatin1(raw.toHex())));
        done_one(owed.peek, exit_ok);
    });
    QObject::connect(&ts, &target_session::threads_ready, [&](std::uint32_t pid, QList<opentm::tm_core::dbgp::ppu_thread_info> items) {
        emit_line(QStringLiteral(">>> "), QStringLiteral("threads pid=0x%1: %2 PPU thread(s)").arg(pid, 0, 16).arg(items.size()));
        for (const auto& t : items)
            emit_line(QStringLiteral("      "), QStringLiteral("tid=0x%1  state=%2  %3")
                .arg(t.thread_id, 0, 16)
                .arg(QString::fromLatin1(opentm::tm_core::dbgp::ppu_thread_state_name(t.state)))
                .arg(QString::fromStdString(t.name)));
        done_one(owed.ps, exit_ok);
    });

    // --probe taps: a process-list reply means the debugger backend answers; the
    // agent pushing its settings file back means the settings backend answers.
    struct reg_row { quint8 port; quint32 proto; QString lpar; };
    QList<reg_row> reg_rows;
    if (probe_mode) {
        QObject::connect(&ts, &target_session::process_list_ready, [&](QList<session_api::process_summary>) { probe_dbg_ok = true; });
        QObject::connect(&ts, &target_session::transfer_finished, [&]() { probe_settings_ok = true; });
        QObject::connect(&ts, &target_session::protocol_registered, [&](quint8 port, quint32 proto, QString lpar) {
            for (const auto& r : reg_rows) if (r.proto == proto && r.port == port) return; // dedup
            reg_rows.push_back({port, proto, std::move(lpar)});
        });
    }

    auto print_probe = [&]() {
        if (probe_printed) return;
        probe_printed = true;
        // Reaching the probe means the session handshake completed, so the
        // session and TTY transport are up; the rest is what actually answered.
        emit_line(QStringLiteral(">>> "), QStringLiteral("backend probe:"));
        emit_line(QStringLiteral("      "), QStringLiteral("session + TTY   : up"));
        emit_line(QStringLiteral("      "), QStringLiteral("debugger (DBGP) : %1").arg(probe_dbg_ok ? QStringLiteral("works") : QStringLiteral("no reply")));
        emit_line(QStringLiteral("      "), QStringLiteral("settings (DCMP) : %1").arg(probe_settings_ok ? QStringLiteral("works") : QStringLiteral("unavailable")));
        if (registered_mode) {
            emit_line(QStringLiteral(">>> "), QStringLiteral("registered protocols (%1):").arg(reg_rows.size()));
            for (const auto& r : reg_rows) {
                emit_line(QStringLiteral("      "),
                          QStringLiteral("%1 proto=0x%2 port=0x%3 lpar=%4")
                              .arg(QString::fromLatin1(opentm::tm_core::regist::proto_name(r.proto)), -6)
                              .arg(r.proto, 8, 16, QChar('0'))
                              .arg(r.port, 2, 16, QChar('0'))
                              .arg(r.lpar));
            }
        }
        if (code == exit_unreachable) code = exit_ok; // session is up: healthcheck passes
        quit_now();
    };

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

        if (dbg_mode) {
            // persistent debug session: kick the debug load; the chain
            // (load reply -> process list -> read) runs on the signal handlers below.
            // owed.peek keeps the session alive until the read reply settles it.
            owed.peek = 1;
            load_options opts; opts.enable_debug_module = true; opts.reset_target = false;
            emit_line(QStringLiteral(">>> "), QStringLiteral("debug-read session: load %1 (debug module ON)").arg(p.value(o_load)));
            ts.load_executable(p.value(o_load), opts);
            return;
        }

        if (p.isSet(o_power_off)) {
            emit_line(QStringLiteral(">>> "), p.isSet(o_force) ? QStringLiteral("power off (forced)") : QStringLiteral("power off"));
            if (p.isSet(o_force)) ts.power_off_force(); else ts.power_off();
        }

        const bool reset_with_load = p.isSet(o_reset) && p.isSet(o_load);
        if (p.isSet(o_reset) && !reset_with_load) {
            emit_line(QStringLiteral(">>> "), QStringLiteral("reset"));
            ts.reset_current();
        }
        if (p.isSet(o_reset_release)) {
            emit_line(QStringLiteral(">>> "), QStringLiteral("reset-release (normal retail boot)"));
            ts.reset_release();
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
        if (p.isSet(o_peek)) {
            emit_line(QStringLiteral(">>> "), QStringLiteral("peek pid=0x%1 addr=0x%2 len=0x%3").arg(peek_pid, 0, 16).arg(peek_addr, 0, 16).arg(peek_len, 0, 16));
            ++owed.peek; ts.read_memory(peek_pid, peek_addr, peek_len);
        }
        if (p.isSet(o_dump)) {
            dump_file.setFileName(dump_out);
            if (!dump_file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                emit_line(QStringLiteral("!!  "), QStringLiteral("cannot open %1 for writing").arg(dump_out));
            } else {
                dump_active = true;
                emit_line(QStringLiteral(">>> "), QStringLiteral("dump pid=0x%1 addr=0x%2 len=0x%3 -> %4")
                          .arg(dump_pid, 0, 16).arg(dump_addr, 0, 16).arg(dump_len, 0, 16).arg(dump_out));
                ++owed.peek; dump_request_next();
            }
        }
        if (p.isSet(o_ppuregs)) {
            emit_line(QStringLiteral(">>> "), QStringLiteral("ppu-regs pid=0x%1 tid=0x%2").arg(regs_pid, 0, 16).arg(regs_tid, 0, 16));
            ++owed.peek; ts.read_ppu_registers(regs_pid, regs_tid);
        }
        if (p.isSet(o_spuregs)) {
            emit_line(QStringLiteral(">>> "), QStringLiteral("spu-regs pid=0x%1 tid=0x%2").arg(sregs_pid, 0, 16).arg(sregs_tid, 0, 16));
            ++owed.peek; ts.read_spu_registers(sregs_pid, sregs_tid);
        }
        if (p.isSet(o_spuls)) {
            sl_file.setFileName(sl_out);
            if (!sl_file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                emit_line(QStringLiteral("!!  "), QStringLiteral("cannot open %1 for writing").arg(sl_out));
            } else {
                emit_line(QStringLiteral(">>> "), QStringLiteral("spu-ls pid=0x%1 tid=0x%2 len=0x%3 -> %4").arg(sl_pid, 0, 16).arg(sl_tid, 0, 16).arg(sl_len, 0, 16).arg(sl_out));
                ++owed.peek; spuls_request_next();
            }
        }
        // Watchpoint/breakpoint control ops. Reserve one owed.peek per op either way; in
        // ctl_load_mode the load-reply hook fires them against the load-captured pid, otherwise
        // fire now against the explicit --pid.
        if (any_ctl) {
            owed.peek += count_control_ops();
            if (ctl_load_mode) {
                emit_line(QStringLiteral("--  "), QStringLiteral("control ops deferred until the debug-load registers the process"));
            } else {
                fire_control_ops(ctl_pid);
            }
        }
        if (p.isSet(o_threads)) {
            const quint32 tpid = p.value(o_pid).toUInt(nullptr, 0);
            emit_line(QStringLiteral(">>> "), QStringLiteral("threads pid=0x%1").arg(tpid, 0, 16));
            ++owed.ps; ts.refresh_threads(tpid);
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
            opts.enable_debug_module = p.isSet(o_debug_module);
            const QString dbg = opts.enable_debug_module ? QStringLiteral(" (debug module ON - registers the process for debugging)") : QString();
            emit_line(QStringLiteral(">>> "), (reset_with_load ? QStringLiteral("reset, then load %1 when the agent is back").arg(p.value(o_load)) : QStringLiteral("load %1").arg(p.value(o_load))) + dbg);
            ++owed.load; ts.load_executable(p.value(o_load), opts);
        }
        if (p.isSet(o_attach)) {
            bool ok = false;
            const QString a = p.value(o_attach).trimmed();
            const std::uint32_t pid = a.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive) ? a.mid(2).toUInt(&ok, 16) : a.toUInt(&ok, 10);
            if (!ok) {
                emit_line(QStringLiteral("!! "), QStringLiteral("attach: bad pid '%1'").arg(a));
            } else {
                emit_line(QStringLiteral(">>> "), QStringLiteral("attach debugger to pid 0x%1").arg(pid, 0, 16));
                ts.attach_process(pid);
            }
        }

        if (probe_mode) {
            emit_line(QStringLiteral(">>> "), QStringLiteral("probe: asking the debugger and settings backends"));
            ts.refresh_process_list();
            ts.settings_refresh();
            // give the target a bounded window to answer, then report whatever did
            const int probe_ms = qBound(3, qMin(p.value(o_run_timeout).toInt(), 10), 10) * 1000;
            QTimer::singleShot(probe_ms, &app, print_probe);
        }

        if (pending_total() == 0 && !watching) linger_then_quit();
    };

    // Actions need a live session. The agent announcement can land before the
    // handshake finishes, so --wait-agent alone isn't enough: fire once both are
    // up, or the verbs bail "session not ready yet" and the run hangs.
    // --debug-read chain: these fire AFTER the generic print handlers above
    // (Qt invokes slots in connection order), so the load reply and process list are already
    // printed; here we just advance load -> list -> read within the one session.
    int dbg_ps_tries = 0;
    quint32 dbg_loadpid = 0; // pid carried by this boot's load reply (auto-captured)
    if (dbg_mode) {
        QObject::connect(&ts, &target_session::load_ext_pid, [&](std::uint32_t pid) {
            dbg_loadpid = pid;
            emit_line(QStringLiteral(">>> "), QStringLiteral("debug-read: load reply carried pid 0x%1").arg(pid, 8, 16, QChar('0')));
        });
        QObject::connect(&ts, &target_session::load_ext_reply, [&](std::uint32_t status) {
            if (settled) return;
            if (status != 0) {
                if (code == exit_ok || code == exit_unreachable) code = exit_refused;
                emit_line(QStringLiteral("!!  "), QStringLiteral("debug-read: load failed, lv2 status %1").arg(lv2_status(status)));
                quit_now();
                return;
            }
            // read IN THIS SESSION with the pid: explicit --pid wins, else the id the load reply
            // carried, else fall back to the process list (which this agent tends to leave empty).
            quint32 pid = 0;
            if (p.isSet(o_pid)) pid = p.value(o_pid).toUInt(nullptr, 0);
            else if (dbg_loadpid != 0) pid = dbg_loadpid;
            if (pid != 0) {
                emit_line(QStringLiteral(">>> "), QStringLiteral("debug-read: in-session read, pid 0x%1 @ 0x%2 len 0x%3")
                          .arg(pid, 8, 16, QChar('0')).arg(dbg_addr, 0, 16).arg(dbg_len, 0, 16));
                ts.read_memory(pid, dbg_addr, dbg_len);
                return;
            }
            emit_line(QStringLiteral(">>> "), QStringLiteral("debug-read: no pid from load reply; falling back to process list"));
            ts.refresh_process_list();
        });
        QObject::connect(&ts, &target_session::process_list_ready, [&](QList<session_api::process_summary> ps) {
            if (settled) return;
            if (ps.isEmpty()) {
                if (++dbg_ps_tries <= 5) {
                    emit_line(QStringLiteral("--  "), QStringLiteral("debug-read: process not registered yet, retry %1/5").arg(dbg_ps_tries));
                    QTimer::singleShot(400, &app, [&]() { if (!settled) ts.refresh_process_list(); });
                } else {
                    if (code == exit_ok || code == exit_unreachable) code = exit_refused;
                    emit_line(QStringLiteral("!! "), QStringLiteral("debug-read: no debuggable process after load - the agent did not register it"));
                    quit_now();
                }
                return;
            }
            // pick the loaded process: match the loaded file name in self_path, else the last entry
            const QString lp = p.value(o_load);
            const QString want = lp.mid(lp.lastIndexOf(QChar('/')) + 1);
            quint32 pick = ps.last().pid;
            QString pick_path = QString::fromStdString(ps.last().info.self_path);
            for (const auto& s : ps) {
                const QString sp = QString::fromStdString(s.info.self_path);
                if (!want.isEmpty() && sp.contains(want)) { pick = s.pid; pick_path = sp; break; }
            }
            emit_line(QStringLiteral(">>> "), QStringLiteral("debug-read: read pid 0x%1 (%2) @ 0x%3 len 0x%4")
                      .arg(pick, 8, 16, QChar('0')).arg(pick_path).arg(dbg_addr, 0, 16).arg(dbg_len, 0, 16));
            ts.read_memory(pick, dbg_addr, dbg_len); // owed.peek (set in run_actions) is settled by the read reply
        });
    }

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
