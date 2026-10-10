#include "rpc_client.h"

#include <QJsonDocument>
#include <QLocalSocket>
#include <QTcpSocket>

namespace opentm::tm_ui {

rpc_client::rpc_client(QObject* parent) : QObject(parent) {}
rpc_client::~rpc_client() = default;

bool rpc_client::connect_tcp(const QString& host, quint16 port, int timeout_ms) {
    disconnect_from_server();
    auto* s = new QTcpSocket(this);
    s->connectToHost(host, port);
    if (!s->waitForConnected(timeout_ms)) {
        emit protocol_error(QStringLiteral("connect to %1:%2 failed: %3") .arg(host).arg(port).arg(s->errorString()));
        s->deleteLater();
        return false;
    }
    connect(s, &QTcpSocket::disconnected, this, &rpc_client::disconnected);
    attach(s);
    return true;
}

bool rpc_client::connect_local(const QString& name, int timeout_ms) {
    disconnect_from_server();
    auto* s = new QLocalSocket(this);
    s->connectToServer(name);
    if (!s->waitForConnected(timeout_ms)) {
        emit protocol_error(QStringLiteral("connect to '%1' failed: %2").arg(name, s->errorString()));
        s->deleteLater();
        return false;
    }
    connect(s, &QLocalSocket::disconnected, this, &rpc_client::disconnected);
    attach(s);
    return true;
}

void rpc_client::attach(QIODevice* dev) {
    dev_ = dev;
    buf_.clear();
    connect(dev_, &QIODevice::readyRead, this, &rpc_client::on_ready_read);
    emit connected();
}

void rpc_client::disconnect_from_server() {
    if (!dev_) return;
    const auto pending = pending_;
    pending_.clear();
    for (const auto& h : pending) {
        if (h) h(false, {}, QStringLiteral("disconnected"));
    }
    dev_->deleteLater();
    dev_ = nullptr;
}

bool rpc_client::is_connected() const {
    return dev_ && dev_->isOpen();
}

void rpc_client::call(const QString& method, const QJsonObject& params, reply_handler on_reply)
{
    if (!is_connected()) {
        if (on_reply) on_reply(false, {}, QStringLiteral("not connected"));
        return;
    }
    const int id = ++next_id_;
    QJsonObject req{{"id", id}, {"method", method}};
    if (!params.isEmpty()) req["params"] = params;
    if (on_reply) pending_.insert(id, std::move(on_reply));

    dev_->write(QJsonDocument(req).toJson(QJsonDocument::Compact));
    dev_->write("\n");
}

void rpc_client::on_ready_read() {
    buf_ += dev_->readAll();
    int nl;
    while ((nl = buf_.indexOf('\n')) >= 0) {
        const QByteArray line = buf_.left(nl).trimmed();
        buf_.remove(0, nl + 1);
        if (line.isEmpty()) continue;

        QJsonParseError perr{};
        const auto doc = QJsonDocument::fromJson(line, &perr);
        if (perr.error != QJsonParseError::NoError || !doc.isObject()) {
            emit protocol_error(QStringLiteral("bad reply: %1").arg(perr.errorString()));
            continue;
        }
        handle_message(doc.object());
    }
}

void rpc_client::handle_message(const QJsonObject& msg) {
    if (msg.contains("event")) {
        emit event_received(msg.value("target").toString(), msg.value("event").toString(), msg.value("params").toObject());
        return;
    }
    const int id = msg.value("id").toInt(-1);
    const auto it = pending_.find(id);
    if (it == pending_.end()) return; // no handler registered

    const auto handler = *it;
    pending_.erase(it);
    if (handler) {
        handler(msg.value("ok").toBool(), msg.value("result").toObject(), msg.value("error").toString());
    }
}

} // namespace opentm::tm_ui
