#pragma once

#include <tm_core/deci3_codec.h>
#include <tm_core/dcmp_codec.h>
#include <tm_core/tsmp_codec.h>

#include <QByteArray>
#include <QObject>
#include <QString>

#include <cstdint>

namespace opentm::tm_ui {

class frame_dispatcher : public QObject {
    Q_OBJECT
public:
    explicit frame_dispatcher(QObject* parent = nullptr);
    ~frame_dispatcher() override;

public slots:
    void on_frame_received(opentm::tm_core::deci3_frame f);

signals:
    void frame_logged(QString line);
    void raw_frame(opentm::tm_core::deci3_frame f);
    void version_string_received(QString version);
    void session_token_received(std::uint16_t token);
    void sub_token_received(std::uint16_t sub);
    void session_handshake_acked();
    void cp_version_received(QString cp_version);
    void session_reaped(std::uint16_t reaped_token);
    void dex_tsmp_reply(std::uint16_t inner_cmd);
    void dcmp_status(std::uint8_t code);
    void transfer_acked(std::uint32_t seq, std::uint32_t result);
    void transfer_finished();
    void lpar_status_received(std::vector<opentm::tm_core::tsmp_lpar_entry> entries);
    void server_rejection(std::uint8_t kind);
    void netmp_register_nack(std::uint8_t status, std::uint32_t proto);
    void protocol_registered(std::uint8_t port, std::uint32_t proto, QString lpar);
    void debug_agent_up();
    void boot_param_received(quint64 value, bool in_effect);
    void power_reply(quint16 cmd, quint32 result);
    void install_progress(int percent);
    void install_finished(QString installed_path);
    void install_reply(std::uint32_t lv2_status);
    void load_ext_reply(std::uint32_t lv2_status);
    void load_ext_pid(std::uint32_t pid);
    void dfmp_get_entries_reply(std::uint32_t seq, QByteArray body);
    void dfmp_op_reply(std::uint32_t seq, std::uint32_t marker, std::uint32_t status);
    void tty_text(QString text);
    void tty_stream_text(std::uint8_t stream, QString text);
};

} // namespace opentm::tm_ui
