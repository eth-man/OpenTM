#include "tcp_connection.h"

#include <QTcpSocket>

#include <span>
#include <vector>

namespace opentm::tm_core {

namespace {

constexpr std::uint16_t kCatDrfp = 0x0110;

constexpr const char* role_name(tcp_connection::socket_role w) noexcept {
    switch (w) {
    case tcp_connection::socket_role::control: return "control";
    case tcp_connection::socket_role::tty:     return "tty";
    case tcp_connection::socket_role::drfp:    return "drfp";
    }
    return "?";
}

} // namespace

tcp_connection::tcp_connection(QObject* parent) : QObject(parent) {
    for (int i = 0; i < 3; ++i) {
        const auto w = static_cast<which>(i);
        auto* s = new QTcpSocket(this);
        sockets_[i] = s;
        QObject::connect(s, &QTcpSocket::connected, this, [this, w] { on_connected(w); });
        QObject::connect(s, &QTcpSocket::disconnected, this, &tcp_connection::on_any_disconnected);
        QObject::connect(s, &QTcpSocket::readyRead, this, [this, w] { read_into(w); });
        QObject::connect(s, &QTcpSocket::errorOccurred, this, [this, s] { on_socket_error(s); });
    }
}

tcp_connection::~tcp_connection() = default;

QTcpSocket* tcp_connection::socket_for(which w) const {
    // dex multiplexes every protocol over one connection
    if (single_socket_) return sockets_[0];
    return sockets_[static_cast<int>(w)];
}

void tcp_connection::reset_buffers() {
    for (auto& b : rx_) b.clear();
    greeting_consumed_ = false;
}

void tcp_connection::connect_to_target(const QHostAddress& host, std::uint16_t port) {
    if (state_ != state::disconnected && state_ != state::error_state) {
        return;
    }
    reset_buffers();
    peer_host_ = host;
    peer_port_ = port;
    set_state(state::tcp_connecting);
    sockets_[0]->connectToHost(host, port);
}

void tcp_connection::disconnect_from_target() {
    bool all_down = true;
    for (auto it = sockets_.rbegin(); it!= sockets_.rend(); ++it) {
        if ((*it)->state() != QAbstractSocket::UnconnectedState) {
            (*it)->disconnectFromHost();
        }
        all_down = all_down && (*it)->state() == QAbstractSocket::UnconnectedState;
    }
    if (all_down) set_state(state::disconnected);
}

bool tcp_connection::disconnect_and_wait(int timeout_ms) {
    auto close_one = [timeout_ms](QTcpSocket* s) -> bool {
        if (s->state() == QAbstractSocket::UnconnectedState) return true;
        if (s->state() == QAbstractSocket::ConnectingState
            || s->state() == QAbstractSocket::HostLookupState) {
            s->abort();
            return true;
        }
        s->disconnectFromHost();
        if (s->state() != QAbstractSocket::UnconnectedState
            && !s->waitForDisconnected(timeout_ms)) {
            s->abort();
            return false;
        }
        return true;
    };
    bool ok = true;
    for (auto it = sockets_.rbegin(); it!= sockets_.rend(); ++it) {
        ok = close_one(*it) && ok;
    }
    set_state(state::disconnected);
    return ok;
}

bool tcp_connection::send_frame(const deci3_frame& f) {
    return send_frame_on(f.category == kCatDrfp ? socket_role::drfp : socket_role::control, f);
}

bool tcp_connection::send_frame_on(socket_role w, const deci3_frame& f) {
    if (state_ != state::ready && state_ != state::awaiting_greeting) return false;
    QTcpSocket* sock = socket_for(w);
    if (!sock || sock->state() != QAbstractSocket::ConnectedState) {
        emit error_occurred(QStringLiteral("write failed: socket %1 (cat=0x%2) is not connected").arg(static_cast<int>(w)).arg(f.category, 4, 16, QChar('0')));
        return false;
    }
    std::vector<std::byte> buf;
    encode_frame(f, buf);
    const auto written = sock->write(reinterpret_cast<const char*>(buf.data()), static_cast<qint64>(buf.size()));
    if (written < 0) {
        emit error_occurred(QStringLiteral("write failed: %1").arg(sock->errorString()));
        return false;
    }
    sock->flush();
    emit bytes_sent(written);
    return true;
}

QString tcp_connection::peer_summary() const {
    return QStringLiteral("%1:%2").arg(peer_host_.toString()).arg(peer_port_);
}

void tcp_connection::set_state(state s) {
    if (state_ == s) return;
    state_ = s;
    emit state_changed(s);
}

void tcp_connection::on_connected(which w) {
    auto* s = sockets_[static_cast<int>(w)];
    s->setSocketOption(QAbstractSocket::LowDelayOption, 1);
    emit log_message(QStringLiteral("    -- socket %1 (%2) connected, local port %3").arg(static_cast<int>(w)).arg(role_name(w)).arg(s->localPort()));
    // decr chains the three sockets up one after another
    switch (w) {
    case which::control:
        if (single_socket_) set_state(state::awaiting_greeting);
        else sockets_[1]->connectToHost(peer_host_, peer_port_);
        break;
    case which::tty:
        sockets_[2]->connectToHost(peer_host_, peer_port_);
        break;
    case which::drfp:
        set_state(state::awaiting_greeting);
        break;
    }
}

void tcp_connection::on_any_disconnected() {
    reset_buffers();
    for (auto it = sockets_.rbegin(); it!= sockets_.rend(); ++it) {
        if ((*it)->state() != QAbstractSocket::UnconnectedState) (*it)->abort();
    }
    set_state(state::disconnected);
}

void tcp_connection::on_socket_error(QTcpSocket* s) {
    emit error_occurred(s->errorString());
    set_state(state::error_state);
}

void tcp_connection::read_into(which w) {
    auto* sock = socket_for(w);
    if (!sock) return;
    rx_[static_cast<int>(w)].append(sock->readAll());
    try_parse_frames(w);
}

void tcp_connection::try_parse_frames(which w) {
    QByteArray& buf = rx_[static_cast<int>(w)];

    if (w == which::control && !greeting_consumed_) {
        if (buf.size() < 2) return;
        const auto b0 = static_cast<std::uint8_t>(buf.at(0));
        const auto b1 = static_cast<std::uint8_t>(buf.at(1));
        if (b0 == 0x30 && b1 == 0x10) {
            greeting_consumed_ = true;
            emit greeting_received(QByteArray());
            set_state(state::ready);
        } else {
            if (buf.size() < 6) return;
            bool all_zero = true;
            for (int i = 0; i < 6; ++i) {
                if (buf.at(i) != '\0') { all_zero = false; break; }
            }
            if (!all_zero) {
                emit error_occurred(QStringLiteral("framing error: unexpected leading bytes %1").arg(QString::fromLatin1(buf.left(6).toHex(' '))));
                sockets_[0]->abort();
                set_state(state::error_state);
                return;
            }
            const auto greeting = buf.left(6);
            buf.remove(0, 6);
            greeting_consumed_ = true;
            emit greeting_received(greeting);
            set_state(state::ready);
        }
    }

    while (true) {
        std::span<const std::byte> view{
            reinterpret_cast<const std::byte*>(buf.constData()),
            static_cast<std::size_t>(buf.size())
        };
        auto r = decode_frame(view);
        if (r.frame) {
            emit frame_received(std::move(*r.frame));
            buf.remove(0, static_cast<int>(r.consumed));
            continue;
        }
        if (!r.error) break;
        switch (*r.error) {
        case decode_error::short_buffer:
        case decode_error::bad_length:
            return;
        case decode_error::bad_magic:
            emit error_occurred(QStringLiteral("framing error: bad magic on %1 socket").arg(QLatin1String(role_name(w))));
            socket_for(w)->abort();
            set_state(state::error_state);
            return;
        }
    }
}

} // namespace opentm::tm_core
