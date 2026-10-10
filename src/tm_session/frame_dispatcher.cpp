#include "frame_dispatcher.h"

#include <span>

#include <tm_core/be_io.h>
#include <tm_core/dcmp_codec.h>
#include <tm_core/dbgshl_cmd.h>
#include <tm_core/tty_stream.h>
#include <tm_core/dfmp_codec.h>
#include <tm_core/tsmp_codec.h>

namespace opentm::tm_ui {

namespace {

std::uint32_t be_u32_at(const std::vector<std::byte>& p, std::size_t off) {
    return opentm::tm_core::read_be_u32(std::span<const std::byte>(p), off);
    }
}

frame_dispatcher::frame_dispatcher(QObject* parent) : QObject(parent) {}
frame_dispatcher::~frame_dispatcher() = default;

void frame_dispatcher::on_frame_received(opentm::tm_core::deci3_frame f) {
    using namespace opentm::tm_core;

    {
        QByteArray hex;
        if (!f.payload.empty()) {
            QByteArray payload(reinterpret_cast<const char*>(f.payload.data()), static_cast<int>(f.payload.size()));
            hex = payload.left(32).toHex(' ');
        }
        emit frame_logged(QStringLiteral("    >> %1 cat=0x%2 sa=0x%3 sb=0x%4 payload(%5B)=%6").arg(QString::fromUtf8(direction_name(f.direction).data())).arg(f.category, 4, 16, QChar('0')).arg(f.session_a, 8, 16, QChar('0')).arg(f.session_b, 8, 16, QChar('0')).arg(f.payload.size()).arg(QString::fromLatin1(hex)));
    }
    // [0] 0x05 NETMP_CODE_REGISTER_REPLY
    // [1] status 0=OK,!0=NACK
    // [2-3] echo arg (priority/port on OK; zeros on NACK)
    // [4-7] proto BE u32 (which protocol the register was for)
    if (f.direction == deci3_direction::machine_to_host
        && f.category == 0x0010
        && f.payload.size() >= 8
        && std::to_integer<std::uint8_t>(f.payload[0]) == 0x05)
    {
        const auto status = std::to_integer<std::uint8_t>(f.payload[1]);
        if (status != 0) {
            emit netmp_register_nack(status, be_u32_at(f.payload, 4));
        }
    }

    if (f.direction == deci3_direction::machine_to_host && f.category == 0x0010 && f.payload.size() >= 2 && std::to_integer<std::uint8_t>(f.payload[0]) == 0x11 && std::to_integer<std::uint8_t>(f.payload[1]) == 0x00)
    {
        QByteArray ver;
        for (std::size_t i = 2; i < f.payload.size(); ++i) {
            const auto b = std::to_integer<std::uint8_t>(f.payload[i]);
            if (b == 0) break;
            ver.append(static_cast<char>(b));
        }
        emit version_string_received(QString::fromLatin1(ver));
    }

    if (f.direction == deci3_direction::target_to_manager
        && f.category == 0x0020
        && f.payload.size() >= 6
        && std::to_integer<std::uint8_t>(f.payload[0]) == 0x00
        && std::to_integer<std::uint8_t>(f.payload[1]) == 0x21)
    {
        emit dex_tsmp_reply(opentm::tm_core::read_be_u16(f.payload.data() + 4));
    }

    if (f.direction == deci3_direction::machine_to_host && f.category == 0x0020 && f.payload.size() >= 6 && std::to_integer<std::uint8_t>(f.payload[0]) == 0x00 && std::to_integer<std::uint8_t>(f.payload[1]) == 0x21)
    {
        const auto inner_cmd_hi = std::to_integer<std::uint8_t>(f.payload[4]);
        const auto inner_cmd_lo = std::to_integer<std::uint8_t>(f.payload[5]);

        if (inner_cmd_hi == 0x02 && inner_cmd_lo == 0x01 && f.payload.size() >= 10)
        {
            const auto token = opentm::tm_core::read_be_u16(f.payload.data() + 8);
            if (token != 0) emit session_token_received(token);
        }

        if (inner_cmd_hi == 0x02 && inner_cmd_lo == 0x03 && f.payload.size() >= 12)
        {
            const auto sub = opentm::tm_core::read_be_u16(f.payload.data() + 10);
            if (sub != 0) emit sub_token_received(sub);
        }

        if (inner_cmd_hi == 0x21 && inner_cmd_lo == 0x01) {
            auto entries = opentm::tm_core::parse_lpar_status_reply(
                std::span<const std::byte>(f.payload).subspan(
                    opentm::tm_core::tsmp_header_size));
            if (!entries.empty()) emit lpar_status_received(std::move(entries));
        }

        if (inner_cmd_hi == 0x20 && (inner_cmd_lo & 1) == 1) {
            const auto r = opentm::tm_core::parse_param_reply(std::span<const std::byte>(f.payload).subspan(opentm::tm_core::tsmp_header_size));
            emit power_reply(static_cast<std::uint16_t>((inner_cmd_hi << 8) | inner_cmd_lo), r ? r->result : 0xffffffffu);
        }

        // 0x3001 is what the reset sequence reads back; 0x3101 is what the target is running right now
        if ((inner_cmd_hi == 0x30 || inner_cmd_hi == 0x31) && inner_cmd_lo == 0x01) {
            const auto r = opentm::tm_core::parse_param_reply(
                std::span<const std::byte>(f.payload).subspan(opentm::tm_core::tsmp_header_size));
            if (r && r->result == 0) emit boot_param_received(r->value, inner_cmd_hi == 0x31);
        }

        if (inner_cmd_hi == 0x01 && inner_cmd_lo == 0x05) {
            emit session_handshake_acked();
            if (f.payload.size() >= 20) {
                const auto vmajor = std::to_integer<std::uint8_t>(f.payload[17]);
                const auto vminor = std::to_integer<std::uint8_t>(f.payload[18]);
                const auto vpatch = std::to_integer<std::uint8_t>(f.payload[19]);
                if (vmajor || vminor || vpatch) {
                    emit cp_version_received(QStringLiteral("%1.%2.%3").arg(vmajor).arg(vminor).arg(vpatch));
                }
            }
        }
    }

    if (f.category == 0x0001 && f.payload.size() >= 2 && std::to_integer<std::uint8_t>(f.payload[0]) == dcmp::type::status)
    {
        const auto code = std::to_integer<std::uint8_t>(f.payload[1]);
        if (code >= dcmp::status_code::system_boot) emit dcmp_status(code);
    }

    // DELETE_PROTO (type=0x02 code=0x02): a protocol registration went away
    // payload[2..3] session id LE u16
    // payload[6..7] category BE u16 0x0020 = tsmp router
    if (f.category == 0x0001 && f.payload.size() >= 4 && std::to_integer<std::uint8_t>(f.payload[0]) == dcmp::type::status && std::to_integer<std::uint8_t>(f.payload[1]) == dcmp::status_code::delete_proto)
    {
        const std::uint16_t notified = static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(f.payload[2])) | (static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(f.payload[3])) << 8);
        emit session_reaped(notified);
    }

    if (f.category == 0x0001 && f.payload.size() >= 2 && std::to_integer<std::uint8_t>(f.payload[0]) == dcmp::type::error && std::to_integer<std::uint8_t>(f.payload[1]) <= dcmp::error_code::nospace)
    {
        emit server_rejection(std::to_integer<std::uint8_t>(f.payload[1]));
    }

    if (f.direction == deci3_direction::target_to_host && f.category == 0x0200 && f.payload.size() >= dbgshl::notify::event_offset + 4 && be_u32_at(f.payload, 0) == dbgshl::notify::ucmd)
    {
        const std::uint32_t source = be_u32_at(f.payload, dbgshl::notify::source_offset);
        const std::uint32_t event  = be_u32_at(f.payload, dbgshl::notify::event_offset);

        if (source == dbgshl::notify::source_agent && event == dbgshl::notify::event_agent_up && f.session_b == 0x02100000u)
        {
            emit debug_agent_up();
        } else if (source == dbgshl::notify::source_install) {
            if (event == dbgshl::notify::event_install_percent && f.payload.size() >= dbgshl::notify::data_offset + 4)
            {
                emit install_progress(static_cast<int>(be_u32_at(f.payload, dbgshl::notify::data_offset)));
            } else if (event == dbgshl::notify::event_install_done) {
                QByteArray raw(reinterpret_cast<const char*>(f.payload.data() + dbgshl::notify::data_offset), static_cast<int>(f.payload.size() - dbgshl::notify::data_offset));
                const int nul = raw.indexOf('\0');
                if (nul >= 0) raw.truncate(nul);
                emit install_finished(QString::fromLatin1(raw));
            }
        }
    }

    if (f.category == 0x0200 && f.direction == deci3_direction::target_to_host && f.payload.size() >= 20 && be_u32_at(f.payload, 0) == dbgshl::cmd::install_package)
    {
        emit install_reply(be_u32_at(f.payload, 16));
    }

    if (f.category == 0x0200 && f.payload.size() >= 20 && be_u32_at(f.payload, 0) == 0x80000004u)
    {
        emit load_ext_reply(be_u32_at(f.payload, 16));
    }

    // DFMP get_entries reply (TH cat=0x0200 cmd=0x8020000F).
    if (f.category == 0x0200 && f.direction == deci3_direction::target_to_host && f.payload.size() >= dfmp_header_size + 4 && be_u32_at(f.payload, 0) == 0x8020000fu)
    {
        const std::uint32_t seq      = be_u32_at(f.payload, 4);
        const std::uint32_t body_len = be_u32_at(f.payload, 8);
        const std::size_t avail = f.payload.size() - dfmp_header_size;
        const std::size_t take = std::min<std::size_t>(body_len, avail);
        QByteArray body(reinterpret_cast<const char*>(f.payload.data() + dfmp_header_size), static_cast<int>(take));

        const std::uint32_t marker = take >= 8 ? be_u32_at(f.payload, dfmp_header_size + 4) : 0;

        // 0x11 acks the transfer request (result!= 0 means it was refused)
        // 0x13 arrives afterwards, unsolicited, when the copy has finished
        if (marker == 0x11 && take >= 16) {
            emit transfer_acked(seq, be_u32_at(f.payload, dfmp_header_size + 12));
        } else if (marker == 0x13) {
            emit transfer_finished();
        } else if (marker == 0x09 || marker == 0x0b || marker == 0x0d
                   || marker == 0x17 || marker == 0x19) {
            // delete / mkdir / rename / chmod / utime all answer with sub-op + 1
            // and carry their lv2 status in the outer header
            emit dfmp_op_reply(seq, marker, be_u32_at(f.payload, 12));
        } else {
            emit dfmp_get_entries_reply(seq, body);
        }
    }

    if (f.category == 0x0300 && (f.direction == deci3_direction::machine_to_host || f.direction == deci3_direction::target_to_host) && f.payload.size() >= 8 && be_u32_at(f.payload, 0) == 0)
    {
        const QByteArray text(reinterpret_cast<const char*>(f.payload.data() + 8), static_cast<int>(f.payload.size() - 8));
        const QString decoded = QString::fromUtf8(text);
        emit tty_text(decoded);
        emit tty_stream_text(tty_stream_of(be_u32_at(f.payload, 4)), decoded);
    }
    emit raw_frame(f);
}

} // namespace opentm::tm_ui
