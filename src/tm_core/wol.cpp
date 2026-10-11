#include "wol.h"

#include <QHostAddress>
#include <QUdpSocket>
#include <QtGlobal>

#ifdef Q_OS_WIN
# include <winsock2.h>
#else
# include <sys/socket.h>
#endif

namespace opentm::tm_core {

namespace {

std::optional<std::uint8_t> hex_nibble(char c) noexcept {
    if (c >= '0' && c <= '9') return static_cast<std::uint8_t>(c - '0');
    if (c >= 'a' && c <= 'f') return static_cast<std::uint8_t>(c - 'a' + 10);
    if (c >= 'A' && c <= 'F') return static_cast<std::uint8_t>(c - 'A' + 10);
    return std::nullopt;
}

} // namespace

std::optional<mac_address> parse_mac(std::string_view s) {
    mac_address mac{};
    int nibbles = 0;
    std::uint8_t pending = 0;

    for (const char c : s) {
        if (c == ':' || c == '-' || c == '.' || c == ' ' || c == '\t') continue;
        const auto n = hex_nibble(c);
        if (!n) return std::nullopt;
        if (nibbles >= 12) return std::nullopt;
        if ((nibbles % 2) == 0) {
            pending = *n;
        } else {
            mac[nibbles / 2] = static_cast<std::uint8_t>((pending << 4) | *n);
        }
        ++nibbles;
    }
    if (nibbles != 12) return std::nullopt;
    return mac;
}

std::string format_mac(const mac_address& mac) {
    static constexpr char kHex[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(17);
    for (std::size_t i = 0; i < mac.size(); ++i) {
        if (i) out.push_back(':');
        out.push_back(kHex[(mac[i] >> 4) & 0xF]);
        out.push_back(kHex[mac[i] & 0xF]);
    }
    return out;
}

std::array<std::uint8_t, 102> build_magic_packet(const mac_address& mac) {
    std::array<std::uint8_t, 102> packet{};
    for (int i = 0; i < 6; ++i) packet[i] = 0xff;
    for (int copy = 0; copy < 16; ++copy) {
        for (int b = 0; b < 6; ++b) {
            packet[6 + copy * 6 + b] = mac[b];
        }
    }
    return packet;
}

bool send_wol(const mac_address& mac, const QHostAddress& broadcast, std::uint16_t port) {
    QUdpSocket sock;
    if (!sock.bind(QHostAddress::AnyIPv4, 0)) return false;
    const auto fd = sock.socketDescriptor();
    if (fd >= 0) {
#ifdef Q_OS_WIN
        BOOL on = TRUE;
        ::setsockopt(static_cast<SOCKET>(fd), SOL_SOCKET, SO_BROADCAST, reinterpret_cast<const char*>(&on), sizeof(on));
#else
        int on = 1;
        ::setsockopt(static_cast<int>(fd), SOL_SOCKET, SO_BROADCAST, &on, sizeof(on));
#endif
    }

    const auto packet = build_magic_packet(mac);
    const qint64 wrote = sock.writeDatagram(reinterpret_cast<const char*>(packet.data()), static_cast<qint64>(packet.size()), broadcast, port);
    return wrote == static_cast<qint64>(packet.size());
}

bool send_wol(const mac_address& mac, std::uint16_t port) {
    return send_wol(mac, QHostAddress::Broadcast, port);
}

} // namespace opentm::tm_core
