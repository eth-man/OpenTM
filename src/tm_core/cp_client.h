#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>

#include <functional>

class QNetworkAccessManager;

namespace opentm::tm_core {

struct cp_boot_params {
    QString boot_mode      = QStringLiteral("dbg"); // dbg|sys|rel
    QString memory_size    = QStringLiteral("tool"); // tool|console
    QString bd_access      = QStringLiteral("drive"); // emu_dev|emu_usb|drive
    QString hdd_speed      = QStringLiteral("native"); // native|emulated
    QString release_check  = QStringLiteral("dev"); // dev|rel
    QString hostfs         = QStringLiteral("dev"); // dev|target
    QString model          = QStringLiteral("ps3-hdd60"); // ps3-hdd60|ps3-hdd20
    QString boot_beep      = QStringLiteral("silent"); // beep|silent

    bool operator==(const cp_boot_params&) const = default;
};

class cp_client : public QObject {
    Q_OBJECT
public:
    explicit cp_client(QObject* parent = nullptr);
    ~cp_client() override;

    void set_host(const QString& host) { host_ = host; }
    void set_credentials(const QString& user, const QString& password);

    using params_handler = std::function<void(bool ok, const cp_boot_params&, const QString& error)>;
    using ack_handler    = std::function<void(bool ok, const QString& error)>;

    void fetch_boot_params(params_handler on_done);
    void apply_boot_params(const cp_boot_params& p, ack_handler on_done);

    static bool validate(const cp_boot_params& p, QString* bad_field = nullptr);

signals:
    void log_message(QString line);

private:
    QByteArray auth_header() const;
    QString    page_url(const char* page) const;

    QNetworkAccessManager* net_ = nullptr;
    QString host_;

    QString user_     = QStringLiteral("Administrator");
    QString password_ = QStringLiteral("Administrator");
};

} // namespace opentm::tm_core
