// socket 0 control cat=0x0001 / 0x0010 / 0x0020 / 0x0200
// socket 1 TTY cat=0x0300 (kit -> host only)
// socket 2 DRFP cat=0x0110 (host file serving for /app_home/)

#pragma once

#include "deci3_codec.h"

#include <QByteArray>
#include <QHostAddress>
#include <QObject>

#include <array>
#include <cstdint>

class QTcpSocket;

namespace opentm::tm_core {

class tcp_connection : public QObject {
    Q_OBJECT
public:
    enum class state {
        disconnected,
        tcp_connecting,
        awaiting_greeting,
        ready,
        error_state,
    };
    Q_ENUM(state)

    explicit tcp_connection(QObject* parent = nullptr);
    ~tcp_connection() override;

    void connect_to_target(const QHostAddress& host, std::uint16_t port);
    void set_single_socket(bool on) noexcept { single_socket_ = on; }
    bool single_socket() const noexcept { return single_socket_; }
    void disconnect_from_target();
    bool disconnect_and_wait(int timeout_ms = 1500);
    bool send_frame(const deci3_frame& f);
    enum class socket_role { control = 0, tty = 1, drfp = 2 };
    bool send_frame_on(socket_role w, const deci3_frame& f);

    state current_state() const noexcept { return state_; }
    QString peer_summary() const;

signals:
    void state_changed(opentm::tm_core::tcp_connection::state s);
    void frame_received(opentm::tm_core::deci3_frame f);
    void greeting_received(QByteArray bytes);
    void bytes_sent(qint64 n);
    void error_occurred(QString message);
    void log_message(QString line);

private:
    using which = socket_role;

    void on_connected(which w);
    void on_any_disconnected();
    void on_socket_error(QTcpSocket* s);
    void set_state(state s);
    QTcpSocket* socket_for(which w) const;
    void reset_buffers();
    void read_into(which w);
    void try_parse_frames(which w);

    std::array<QTcpSocket*, 3> sockets_{};
    std::array<QByteArray, 3>  rx_;
    bool          greeting_consumed_ = false;
    bool          single_socket_     = false;
    state         state_            = state::disconnected;
    QHostAddress  peer_host_;
    std::uint16_t peer_port_        = 0;
};

} // namespace opentm::tm_core
