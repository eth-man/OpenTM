#pragma once

#include <tm_core/target_type.h>

#include <QHostAddress>
#include <QList>
#include <QObject>
#include <QString>

#include <cstdint>

class QTcpSocket;
class QTimer;

namespace opentm::tm_ui {

class target_discovery : public QObject {
    Q_OBJECT
public:
    explicit target_discovery(QObject* parent = nullptr);
    ~target_discovery() override;
    void set_connect_timeout_ms(int ms) noexcept { connect_timeout_ms_ = ms; }
    bool is_running() const noexcept { return !probes_.isEmpty(); }

public slots:
    void scan(QList<QHostAddress> hosts);
    void cancel();

signals:
    void scan_started(int probe_count);
    void probe_checked(QHostAddress host, std::uint16_t port, bool open);
    void target_found(QHostAddress host, std::uint16_t port, opentm::tm_core::target_type kind);
    void scan_finished();

private:
    struct probe {
        QHostAddress  host;
        std::uint16_t port = 0;
        opentm::tm_core::target_type kind = opentm::tm_core::target_type::unknown;
        QTcpSocket*   sock = nullptr;
        QTimer*       timer = nullptr;
        bool          greeted = false; // answered our version probe as DECI3
    };

    void on_connected(probe* p);
    void on_probe_readable(probe* p);
    void on_timeout(probe* p);
    void finish_probe(probe* p, bool open);
    void maybe_emit_finished();

    QList<probe*> probes_;
    int           connect_timeout_ms_ = 700;
};

QList<QHostAddress> expand_cidr(const QString& cidr, QString* error = nullptr);

} // namespace opentm::tm_ui
