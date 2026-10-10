#pragma once

#include <tm_core/tty_stream.h>

#include <QString>
#include <QVector>

#include <cstdint>

namespace opentm::tm_ui {

struct tty_channel {
    QString name;
    bool    enabled = true;

    QVector<std::uint8_t> streams;

    bool echo_input        = false;
    bool timestamps        = false;
    bool date_in_timestamp = false;
    int  buffer_kb         = 1024;
    bool word_wrap         = true;

    bool prevent_interleaved = false;

    bool    logging = false;
    QString log_path;
    int     log_size = -1;
    bool    log_append = false;
    bool    log_clear_with_output = false;

    struct filter {
        QString match;
        QString replace;
    };
    QVector<filter> filters;
};

inline QVector<tty_channel> default_tty_channels() {
    QVector<tty_channel> out;

    tty_channel all;
    all.name = QStringLiteral("All");
    for (std::uint8_t s = 0; s < opentm::tm_core::tty_stream_count; ++s) {
        all.streams.push_back(s);
    }
    out.push_back(all);

    for (std::uint8_t s = 0; s < opentm::tm_core::tty_stream_count; ++s) {
        const auto sv = opentm::tm_core::tty_stream_name(s);
        tty_channel c;
        c.name    = QString::fromLatin1(sv.data(), static_cast<int>(sv.size()));
        c.streams = {s};
        c.enabled = (s == 0x00 || s == 0x02); // TM, PPU
        out.push_back(c);
    }
    return out;
}

} // namespace opentm::tm_ui
