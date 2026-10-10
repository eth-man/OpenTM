#include "session_controller.h"

#include <tm_core/dcmp_codec.h>

#include "tsmp_helpers.h"

#include <QByteArray>
#include <QDateTime>
#include <QHostInfo>
#include <QStringLiteral>

namespace opentm::tm_ui {

session_controller::session_controller(opentm::tm_core::tcp_connection* conn, QObject* parent) : QObject(parent), connection_(conn) {}

session_controller::~session_controller() = default;

namespace {
struct netmp_profile {
    bool cfw = false;

    opentm::tm_core::deci3_direction direction() const noexcept {
        using namespace opentm::tm_core;
        return cfw ? deci3_direction::manager_to_target : deci3_direction::host_to_machine;
    }
    std::uint16_t category() const noexcept { return cfw ? 0x0001 : 0x0010; }

    QByteArray code(std::uint16_t c) const {
        QByteArray b;
        if (cfw) { b.append(char((c >> 8) & 0xff)); b.append(char(c & 0xff)); }
        else     { b.append(char(c & 0xff)); b.append(char((c >> 8) & 0xff)); }
        return b;
    }

    QByteArray register_body(std::uint16_t op, std::uint8_t prio, std::uint8_t port, std::uint32_t proto, const char* lpar) const {
        QByteArray b = code(op);
        if (cfw) {
            b.append(static_cast<char>(port));
            b.append('\0');
        } else {
            b.append(static_cast<char>(prio));
            b.append(static_cast<char>(port));
        }
        b.append(char((proto >> 24) & 0xff)); b.append(char((proto >> 16) & 0xff));
        b.append(char((proto >>  8) & 0xff)); b.append(char( proto & 0xff));
        if (!cfw) {
            QByteArray name(lpar);
            name.truncate(16);
            b.append(name);
            b.append(QByteArray(16 - name.size(), '\0'));
        }
        return b;
    }

    QByteArray connect_body(const QByteArray& ident) const {
        QByteArray b = code(0x0000);
        if (cfw) {
            
            b.append('\0'); b.append(char(0x02));
            QByteArray name("PS3_LPAR");
            b.append(name);
            b.append(QByteArray(32 - name.size(), '\0'));
        } else {
            b.append(ident);
            b.append('\0');
        }
        return b;
    }
};

} // namespace

void session_controller::send_version_probe() {
    using namespace opentm::tm_core;
    const netmp_profile prof{ is_cfw_dex() };
    const QByteArray ident = QStringLiteral("OpenTM@%1,OpenTM")
        .arg(QHostInfo::localHostName()).toUtf8();
    auto send_netmp_connect_on = [this, &ident, &prof] (tcp_connection::socket_role role, const char* role_tag) {
        deci3_frame nc;
        nc.direction = prof.direction();
        nc.category  = prof.category();
        nc.payload   = to_bytes(prof.connect_body(ident));
        if (connection_->send_frame_on(role, nc)) {
            emit log_message(QStringLiteral("    <- NETMP_CONNECT on %1 socket (%2)") .arg(role_tag) .arg(prof.cfw ? QStringLiteral("PS3_LPAR") : QStringLiteral("\"%1\"") .arg(QString::fromUtf8(ident))));
        }
    };
    
    send_netmp_connect_on(tcp_connection::socket_role::control, "CTRL");
    if (!prof.cfw) {
        
        
        send_netmp_connect_on(tcp_connection::socket_role::tty,     "TTY");
        send_netmp_connect_on(tcp_connection::socket_role::drfp,    "DRFP");
    }

    if (prof.cfw) {
        send_warmup();
        send_session_init();
        return;
    }
    deci3_frame f;
    f.direction = deci3_direction::host_to_machine;
    f.category  = 0x0010;
    f.payload = { std::byte{0x10}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00} };
    if (connection_->send_frame(f)) {
        emit log_message(QStringLiteral("    <- get-version (cat=0x0010 cmd=0x1000)"));
    }
}

void session_controller::send_warmup() {
    using namespace opentm::tm_core;
    const netmp_profile prof{ is_cfw_dex() };
    auto send_cat10 = [this, &prof](std::initializer_list<std::uint8_t> bytes, const QString& tag) {
        deci3_frame f;
        f.direction = prof.direction();
        f.category  = prof.category();
        f.payload.reserve(bytes.size());
        for (auto b : bytes) f.payload.push_back(std::byte{b});
        if (connection_->send_frame(f)) {
            emit log_message(QStringLiteral("    <- %1").arg(tag));
        }
    };
    if (!prof.cfw) {
        send_cat10({0x0a, 0x00, 0x00, 0x00}, tr("warmup: cmd=0x0a00 probe"));
    }

    auto reg_on = [this, &prof](tcp_connection::socket_role role, std::uint8_t prio, std::uint8_t port, std::uint32_t proto, const char* lpar, const QString& tag) {
        deci3_frame f;
        f.direction = prof.direction();
        f.category  = prof.category();
        f.payload   = to_bytes(prof.register_body(0x0004, prio, port, proto, lpar));
        if (connection_->send_frame_on(role, f)) {
            emit log_message(QStringLiteral("    <- %1").arg(tag));
        }
    };
    auto reg = [&reg_on](std::uint8_t prio, std::uint8_t port, std::uint32_t proto, const char* lpar, const QString& tag) {
        reg_on(tcp_connection::socket_role::control, prio, port, proto, lpar, tag);
    };
    if (prof.cfw) {
        reg(0x00, 0xff, 0x00000020u, "CP", tr("warmup: register CP    proto=0x00000020 port=0xff (DEX)"));
        for (std::uint8_t s = 0x00; s <= 0x0f; ++s) {
            reg(0x00, s, 0x00800300u, "PS3_LPAR", tr("warmup: register tty slot 0x%1 (DEX)").arg(s, 2, 16, QChar('0')));
        }
        reg(0x00, 0x80, 0x00800300u, "CP", tr("warmup: register tty slot 0x80 (DEX)"));
        reg(0x00, 0x00, 0x00000310u, "PS3_LPAR", tr("warmup: register TTYP  proto=0x00000310 (DEX)"));
        reg(0x00, 0x00, 0x00000110u, "PS3_LPAR", tr("warmup: register DRFP  proto=0x00000110 (DEX)"));
        reg(0x00, 0x00, 0x00000100u, "PS3_LPAR", tr("warmup: register DFMP  proto=0x00000100 (DEX)"));
        reg(0x00, 0x10, 0x00000200u, "PS3_LPAR", tr("warmup: register DBGP  proto=0x00000200 port=0x10 (DEX)"));
        return;
    }

    if (control_only_) {
        reg(0x80, 0x00, 0x00000020u, "CP", tr("warmup: register CP proto=0x00000020 port=0x00 prio=0x80 (control only)"));
        send_cat10({0x08, 0x00, 0x00, 0x00}, tr("warmup: get-status"));
        return;
    }

    reg(0xff, 0x10, 0x00000200u, "PS3_LPAR", tr("warmup: register DBGP proto=0x00000200 port=0x10 prio=0xff PS3_LPAR"));
    reg(0x80, 0x00, 0x00000100u, "PS3_LPAR", tr("warmup: register DFMP  proto=0x00000100 port=0x00 prio=0x80 PS3_LPAR"));
    reg_on(tcp_connection::socket_role::drfp, 0x80, 0x00, 0x00000110u, "PS3_LPAR", tr("warmup: register DRFP  proto=0x00000110 port=0x00 prio=0x80 PS3_LPAR (on DRFP socket)"));
    reg(0x80, 0x00, 0x00000310u, "PS3_LPAR", tr("warmup: register TTYP  proto=0x00000310 port=0x00 prio=0x80 PS3_LPAR"));
    reg(0x80, 0x00, 0x00800011u, "CP", tr("warmup: register CTRLP proto=0x00800011 port=0x00 prio=0x80 CP"));

    send_cat10({0x08, 0x00, 0x00, 0x00}, tr("warmup: get-status"));

    reg(0x80, 0x00, 0x00000020u, "CP", tr("warmup: register CP proto=0x00000020 port=0x00 prio=0x80 (CP route)"));
    for (std::uint8_t s = 0x00; s <= 0x0f; ++s) {
        reg(0x80, s, 0x00800300u, "PS3_LPAR", tr("warmup: register tty slot 0x%1 (PS3_LPAR)").arg(s, 2, 16, QChar('0')));
    }
    reg(0x80, 0x80, 0x00800300u, "CP", tr("warmup: register tty slot 0x80 (CP) - boot-loader TTY"));
}

void session_controller::send_warmup_deregister() {
    using namespace opentm::tm_core;
    if (!connection_) return;
    const netmp_profile prof{ is_cfw_dex() };
    auto dereg_on = [this, &prof](tcp_connection::socket_role role, std::uint8_t prio, std::uint8_t port, std::uint32_t proto, const char* lpar, const QString& tag) {
        deci3_frame f;
        f.direction = prof.direction();
        f.category  = prof.category();
        f.payload   = to_bytes(prof.register_body(0x0006, prio, port, proto, lpar));
        if (connection_->send_frame_on(role, f)) {
            emit log_message(QStringLiteral("    <- %1").arg(tag));
        }
    };
    auto dereg = [&dereg_on](std::uint8_t prio, std::uint8_t port, std::uint32_t proto, const char* lpar, const QString& tag) {
        dereg_on(tcp_connection::socket_role::control, prio, port, proto, lpar, tag);
    };

    // a control-only channel registered nothing but the CP route
    if (control_only_) {
        dereg(0x80, 0x00, 0x00000020u, "CP", tr("disconnect: deregister CP (control only)"));
        return;
    }

    if (prof.cfw) {
        dereg(0x00, 0x10, 0x00000200u, "PS3_LPAR", tr("disconnect: deregister DBGP"));
        dereg(0x00, 0x00, 0x00000100u, "PS3_LPAR", tr("disconnect: deregister DFMP"));
        dereg(0x00, 0x00, 0x00000110u, "PS3_LPAR", tr("disconnect: deregister DRFP"));
        dereg(0x00, 0x00, 0x00000310u, "PS3_LPAR", tr("disconnect: deregister TTYP"));
        dereg(0x00, 0xff, 0x00000020u, "CP",       tr("disconnect: deregister CP"));
    } else {
        dereg(0xff, 0x10, 0x00000200u, "PS3_LPAR", tr("disconnect: deregister DBGP"));
        dereg(0x80, 0x00, 0x00000100u, "PS3_LPAR", tr("disconnect: deregister DFMP (PS3_LPAR)"));
        dereg_on(tcp_connection::socket_role::drfp, 0x80, 0x00, 0x00000110u, "PS3_LPAR", tr("disconnect: deregister DRFP (on DRFP socket)"));
        dereg(0x80, 0x00, 0x00000310u, "PS3_LPAR", tr("disconnect: deregister TTYP"));
        dereg(0x80, 0x00, 0x00800011u, "CP", tr("disconnect: deregister CTRLP (CP)"));
    }

    auto disconnect_on = [this, &prof](tcp_connection::socket_role role, const QString& tag) {
        deci3_frame f;
        f.direction = prof.direction();
        f.category  = prof.category();
        f.payload   = to_bytes(prof.code(0x0002) + QByteArray(2, '\0'));
        if (connection_->send_frame_on(role, f)) {
            emit log_message(QStringLiteral("    <- NETMP_DISCONNECT on %1 socket").arg(tag));
        }
    };
    if (prof.cfw) {
        disconnect_on(tcp_connection::socket_role::control, tr("CTRL"));
        return;
    }
    disconnect_on(tcp_connection::socket_role::drfp,    tr("DRFP"));
    disconnect_on(tcp_connection::socket_role::tty,     tr("TTY"));
    disconnect_on(tcp_connection::socket_role::control, tr("CTRL"));
}

void session_controller::send_dex_tsmp(std::uint16_t cmd, const QByteArray& body, const QString& tag) {
    if (!connection_) return;
    auto inner = tsmp_inner(cmd, 0x0000, body);
    auto f = tsmp_frame_from_inner(inner);
    f.direction = opentm::tm_core::deci3_direction::manager_to_target;
    f.session_b = 0x00ff0000u;
    if (connection_->send_frame(f)) {
        emit log_message(QStringLiteral("    <- DEX tsmp cmd=0x%1 (%2)").arg(cmd, 4, 16, QChar('0')).arg(tag));
    }
}
void session_controller::on_dex_tsmp_reply(std::uint16_t reply_cmd) {
    if (!is_cfw_dex()) return;

    if (session_ready_) return;
    QByteArray lpar("PS3_LPAR");
    lpar.append(QByteArray(8, '\0'));
    switch (reply_cmd) {
    case 0x0103: send_dex_tsmp(0x3000, lpar, QStringLiteral("lpar info")); break;
    case 0x3001: send_dex_tsmp(0x3200, QByteArray(), QStringLiteral("status")); break;
    case 0x3201: send_dex_tsmp(0x4100, QByteArray(), QStringLiteral("mac addr")); break;
    case 0x4101: send_dex_tsmp(0x4102, QByteArray(), QStringLiteral("ip addr")); break;
    case 0x4103: send_dex_tsmp(0x3100, lpar, QStringLiteral("lpar state")); break;
    case 0x3101:
        session_ready_ = true;
        emit log_message(QStringLiteral(
            "       *** DEX session fully ready ***"));
        emit session_ready(session_token_, sub_token_);
        break;
    default: break;
    }
}

void session_controller::send_session_init() {
    auto fire = [this](std::uint16_t inner_cmd) {
        auto inner = tsmp_inner(inner_cmd, 0x0000);
        auto f = tsmp_frame_from_inner(inner);
        if (connection_->send_frame(f)) {
            emit log_message(QStringLiteral("    <- session-init (tsmp cmd=0x%1)").arg(inner_cmd, 4, 16, QChar('0')));
        }
    };
    if (is_cfw_dex()) {
        send_dex_tsmp(0x0102, QByteArray(), QStringLiteral("session-init"));
        return;
    }
    fire(0x0206);
    fire(0x0200);
}

void session_controller::on_connection_state(opentm::tm_core::tcp_connection::state s) {
    using state = opentm::tm_core::tcp_connection::state;
    if (s == state::awaiting_greeting) {
        send_version_probe();
    } else if (s == state::disconnected || s == state::error_state) {
        
        session_token_      = 0;
        sub_token_          = 0;
        session_ready_      = false;
        dbgshl_stream_open_ = false;
        sdk_emitted_        = false;
    }
}

void session_controller::on_tty_text(const QString& text) {
    if (sdk_emitted_) return;
    const int idx = text.indexOf(QStringLiteral("SDK Version:"));
    if (idx < 0) return;
    
    int i = idx + static_cast<int>(QStringLiteral("SDK Version:").size());
    while (i < text.size() && text[i].isSpace()) ++i;
    QString raw;
    while (i < text.size() && (text[i].isDigit() || text[i] == QChar('.')))
    {
        raw.append(text[i]);
        ++i;
    }
    if (raw.isEmpty()) return;
    
    const auto parts = raw.split('.');
    const auto major = parts.first();
    QString formatted;
    for (int j = 0; j < major.size(); ++j) {
        if (j) formatted.append('.');
        formatted.append(major[j]);
    }
    if (formatted.isEmpty()) return;
    sdk_emitted_ = true;
    emit sdk_version_received(formatted);
    emit log_message(tr("       SDK version = %1 (firmware %2)").arg(formatted, raw));
}

void session_controller::on_version_string(const QString& version) {
    emit status_message(tr("Ready (proto %1)").arg(version));
    emit log_message(tr("       version string = \"%1\"").arg(version));

    send_warmup();
    send_session_init();
}

void session_controller::on_session_token(std::uint16_t token) {
    using namespace opentm::tm_core;
    if (session_token_ != 0) return;
    session_token_ = token;
    emit log_message(tr("       *** session token = 0x%1 ***").arg(token, 4, 16, QChar('0')));

    
    {
        deci3_frame f;
        f.direction = deci3_direction::host_to_machine;
        f.category  = 0x0010;
        for (auto b : {0x06,0x00,0x00,0x00,0x00,0x00,0x00,0x20}) {
            f.payload.push_back(std::byte{static_cast<std::uint8_t>(b)});
        }
        if (connection_->send_frame(f)) {
            emit log_message(QStringLiteral("    <- cmd=0x0600 session ack"));
        }
    }

    {
        std::uint8_t flag_lo = static_cast<std::uint8_t>(token & 0xff);
        std::uint8_t flag_hi = static_cast<std::uint8_t>(0x80 | ((token >> 8) & 0x7f));
        deci3_frame f;
        f.direction = deci3_direction::host_to_machine;
        f.category  = 0x0010;
        std::initializer_list<std::uint8_t> bytes = {
            0x04, 0x00, flag_hi, flag_lo, 0x00, 0x00, 0x00, 0x20,
            'C','P', 0,0,0,0,0,0,0,0,0,0,0,0,0,0
        };
        for (auto b : bytes) f.payload.push_back(std::byte{b});
        if (connection_->send_frame(f)) {
            emit log_message(tr("    <- re-register CP with session 0x%1").arg(token, 4, 16, QChar('0')));
        }
    }

    QByteArray host_blob;
    host_blob.append(static_cast<char>(0xf8));
    host_blob.append(static_cast<char>(0xff));
    host_blob.append(static_cast<char>(0x00));
    host_blob.append(static_cast<char>(0x00));
    host_blob.append(QByteArray::fromHex("d421ca2a6051"));
    host_blob.append(static_cast<char>(0x00));
    host_blob.append(static_cast<char>(0x10));
    host_blob.append(QByteArray::fromHex("40c3ca2a"));
    auto inner = tsmp_inner(0x0202, 0x0000, host_blob);
    auto sf = tsmp_frame_from_inner(inner);
    sf.session_b = static_cast<std::uint32_t>(token) << 16;
    if (connection_->send_frame(sf)) {
        emit log_message(QStringLiteral("    <- post-session: cmd=0x0202 host blob"));
    }
    emit status_message(tr("Session 0x%1, allocating sub-token...") .arg(token, 4, 16, QChar('0')));
}

void session_controller::on_sub_token(std::uint16_t sub) {
    using namespace opentm::tm_core;
    if (sub_token_ != 0) return;
    sub_token_ = sub;
    emit log_message(tr("       *** sub-token = 0x%1 ***") .arg(sub, 4, 16, QChar('0')));

    auto send_with_sub = [this](std::uint16_t inner_cmd) {
        auto inner = tsmp_inner(inner_cmd, sub_token_);
        auto f = tsmp_frame_from_inner(inner);
        f.session_b = static_cast<std::uint32_t>(session_token_) << 16;
        if (connection_->send_frame(f)) {
            emit log_message(tr("    <- post-session: cmd=0x%1 with sub 0x%2").arg(inner_cmd, 4, 16, QChar('0')).arg(sub_token_, 4, 16, QChar('0')));
        }
    };
    send_with_sub(0x0102);
    send_with_sub(0x0104);
}

void session_controller::on_handshake_acked() {
    if (sub_token_ == 0 || session_ready_) return;

    session_ready_ = true;
    emit log_message(tr("       *** session fully ready (id=0x%1 sub=0x%2) ***").arg(session_token_, 4, 16, QChar('0')).arg(sub_token_, 4, 16, QChar('0')));
    emit status_message(tr("Ready (session 0x%1 / sub 0x%2)").arg(session_token_, 4, 16, QChar('0')).arg(sub_token_, 4, 16, QChar('0')));
    emit state_indicator_changed(opentm::tm_core::tcp_connection::state::ready);
    dbgshl_seq_ = 0x1000u + static_cast<std::uint32_t>(QDateTime::currentMSecsSinceEpoch() & 0x3fffu);
    dbgshl_stream_open_ = false;

    emit session_ready(session_token_, sub_token_);
}

void session_controller::on_session_reaped(std::uint16_t reaped_token) {
    if (session_token_ == 0 || reaped_token != session_token_) return;

    emit log_message(tr("       !! kit reaped session 0x%1 (cat=0x0001 cmd=0x0202). " "Re-running session-init.").arg(session_token_, 4, 16, QChar('0')));
    session_token_      = 0;
    sub_token_          = 0;
    session_ready_      = false;
    dbgshl_stream_open_ = false;
    emit status_message(tr("Kit reaped our session - re-initialising..."));
    emit session_invalidated();

    send_warmup();
    send_session_init();
}

void session_controller::on_server_rejection(std::uint8_t kind) {
    namespace dcmp = opentm::tm_core::dcmp;
    const char* hint = "";
    switch (kind) {
    case dcmp::error_code::invalhead:
        hint = " - invalid DECI3 header"; break;
    case dcmp::error_code::system_off:
        hint = " - kit is powered off"; break;
    case dcmp::error_code::system_suspended:
        hint = " - kit is suspended"; break;
    case dcmp::error_code::lpar_none:
        hint = " - target LPAR doesn't exist"; break;
    case dcmp::error_code::lpar_suspended:
        hint = " - target LPAR is suspended"; break;
    case dcmp::error_code::noconnect:
        hint = " - kit side is not up (the CP answers even when the " "system is off, so the link stays open)"; break;
    case dcmp::error_code::noproto:
        hint = " - protocol not registered on this connection"; break;
    case dcmp::error_code::priority:
        hint = " - priority already used by another host"; break;
    case dcmp::error_code::nospace:
        hint = " - kit-side dtnetm buffer full"; break;
    default:   break;
    }
    const auto name = QStringLiteral("DCMP_CODE_%1").arg(QString::fromLatin1(dcmp::error_name(kind)));
    emit log_message(tr("       !! DCMP_TYPE_ERROR %1 (cat=0x0001 cmd=0x03%2)%3").arg(name).arg(kind, 2, 16, QChar('0')).arg(QString::fromLatin1(hint)));
    dbgshl_stream_open_ = false;
    emit status_message(tr("DCMP error: %1%2").arg(name).arg(QString::fromLatin1(hint)));

    const bool kit_is_down = kind == dcmp::error_code::system_off || kind == dcmp::error_code::system_suspended || kind == dcmp::error_code::lpar_none || kind == dcmp::error_code::lpar_suspended || kind == dcmp::error_code::noconnect;
    if (kit_is_down && session_ready_) {
        session_ready_ = false;
        emit session_invalidated();
    }
}

void session_controller::on_target_came_up() {
    if (session_token_ == 0) return;
    emit log_message(tr("       -- target rebooted: re-registering protocols"));
    dbgshl_stream_open_ = false;
    send_warmup();
}

void session_controller::on_boot_param(quint64 value, bool in_effect) {
    const auto mode = opentm::tm_core::boot_mode_of(value);
    const auto name = QString::fromLatin1(opentm::tm_core::boot_mode_name(mode));
    emit log_message(tr("    << boot param %1: 0x%2 (%3)").arg(in_effect ? tr("in effect") : tr("for next boot"), QString::number(value, 16), name));
    if (!in_effect) return;
    if (boot_mode_ == mode) return;
    boot_mode_ = mode;
    emit boot_mode_changed(name);
}

void session_controller::on_debug_agent_up() {
    emit log_message(QStringLiteral("       *** debug-agent up - kicking file-explorer auto-list"));
    dbgshl_stream_open_ = false;
    emit debug_agent_ready();
}

} // namespace opentm::tm_ui
