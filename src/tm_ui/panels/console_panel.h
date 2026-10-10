#pragma once

#include "../tty_channel.h"

#include <QDateTime>
#include <QHash>
#include <QVector>
#include <QWidget>
#include <QString>

#include <cstdint>

class QLineEdit;
class QPlainTextEdit;
class QTabWidget;

namespace opentm::tm_ui {

class find_dialog;

class console_panel : public QWidget {
    Q_OBJECT
public:
    explicit console_panel(QWidget* parent = nullptr);
    ~console_panel() override;

    void set_channels(const QVector<tty_channel>& channels);

    const QVector<tty_channel>& channels() const { return channels_; }

signals:
    void save_requested();
    void settings_requested();

public slots:
    void append_stream(std::uint8_t stream, const QString& text);

public:
    void append_stream_at(std::uint8_t stream, const QString& text, const QDateTime& when);
    void clear();
    void clear_current();
    void apply_font_from_prefs();
    void focus_find();
    QString current_channel_name() const;
    QString current_text() const;

private:
    struct channel_view {
        QPlainTextEdit* view = nullptr;
        bool at_line_start = true;
    };

    void append_to(int channel_index, const QString& text, const QDateTime& when);
    void install_context_menu(QPlainTextEdit* view);
    QPlainTextEdit* current_view() const;

    QLineEdit*            filter_ = nullptr;
    find_dialog*          find_   = nullptr;
    QTabWidget*           tabs_   = nullptr;
    QVector<tty_channel>  channels_;
    QVector<channel_view> views_; // parallel to channels_
    // stream number -> indices into channels_/views_. 
    // Rebuilt once per set_channels() so the hot path is a hash lookup, not a scan
    QHash<std::uint8_t, QVector<int>> routes_;
};

} // namespace opentm::tm_ui
