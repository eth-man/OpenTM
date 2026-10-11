#pragma once

#include "target_record.h"
#include "load_options.h"

#include <tm_core/tcp_connection.h>

#include <QByteArray>
#include <QObject>
#include <QString>

#include <QTimer>

#include <cstdint>
#include <optional>

namespace opentm::tm_ui {

class session_controller;

class target_actions : public QObject {
    Q_OBJECT
public:
    target_actions(opentm::tm_core::tcp_connection* conn, session_controller* sess, QObject* parent = nullptr);
    ~target_actions() override;
    using load_options = opentm::tm_ui::load_options;
public slots:
    void set_target(const target_record& r);
    void clear_target();
    void power_on();
    void power_off();
    void power_off_force();
    void reset_debug();
    void reset_ssm();
    void reset_release();
    void reset_current();
    void wake_on_lan();
    void send_load_executable(const QString& path, const load_options& opts);
    void install_package(const QString& host_path);
    void send_settings_refresh(); 
    void send_settings_apply(const QString& host_path, std::uint32_t file_size);
    void send_settings_commit();

signals:
    void log_message(QString line);
    void status_message(QString text);
    void clear_console();

private:
    
    bool require_ready(const QString& action_label);
    void send_session_tsmp(std::uint16_t cmd, const QByteArray& extra, const QString& tag);
public:
    void request_lpar_status();
    void request_current_boot_param();
private:
    void send_reset_sequence(std::uint64_t boot_value,std::uint64_t boot_mask,const QString& label);
    void send_preload_handshake();
    void on_debug_agent_ready();
    void drop_pending_load(const QString& why);
    
    struct pending_load { QString path; load_options opts; };
    std::optional<pending_load> pending_load_;
    QTimer                      pending_load_timeout_;

    opentm::tm_core::deci3_frame build_dbgshl(std::uint32_t ucmd, std::uint32_t seq, const QByteArray& body) const;
    std::uint32_t transfer_id_ = 2;

    opentm::tm_core::tcp_connection* connection_  = nullptr;
    session_controller*               session_     = nullptr;
    target_record                     target_;
    bool                              have_target_ = false;
};

} // namespace opentm::tm_ui
