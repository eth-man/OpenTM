#include "tm_core/ps3mapi_client.h"
#include "tm_core/lv1_guard.h"

#include <QEventLoop>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QTimer>
#include <QUrl>

namespace opentm::tm_core {

namespace {
constexpr std::uint64_t kProbeAddr = 0x385138;
constexpr std::uint64_t kProbeWant = 0x372d08;
constexpr int           kChunk     = 256; // getmem returns at most 256 B/request
constexpr std::uint64_t kLv2Base   = 0x8000000000000000ULL;
} // namespace

ps3mapi_client::ps3mapi_client(QString host, quint16 port, int timeout_ms)
    : host_(std::move(host)), port_(port), timeout_ms_(timeout_ms) {}

bool ps3mapi_client::normalise_lv2(std::uint64_t addr, std::uint32_t* low, QString* err) {
    const std::uint64_t hi = addr >> 32;
    if (hi != 0 && hi != (kLv2Base >> 32)) {
        if (err) *err = QStringLiteral(
            "lv2 address %1 has a high half that is not 0x80000000 -- refusing")
            .arg(addr, 0, 16);
        return false;
    }
    *low = static_cast<std::uint32_t>(addr & 0xFFFFFFFFULL);
    return true;
}

mapi_result ps3mapi_client::http_get(const QString& path) {
    QNetworkAccessManager nam;
    QUrl url;
    url.setScheme(QStringLiteral("http"));
    url.setHost(host_);
    url.setPort(port_);
    // path already includes the query string; set it raw so '?' and '&' survive.
    const int q = path.indexOf(QLatin1Char('?'));
    if (q < 0) {
        url.setPath(path);
    } else {
        url.setPath(path.left(q));
        url.setQuery(path.mid(q + 1));
    }

    QNetworkRequest req(url);
    QNetworkReply* reply = nam.get(req);

    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    bool timed_out = false;
    QObject::connect(&timer, &QTimer::timeout, &loop, [&]() { timed_out = true; reply->abort(); loop.quit(); });
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    timer.start(timeout_ms_);
    loop.exec();

    mapi_result r;
    if (timed_out) {
        r.error = QStringLiteral("timeout after %1 ms reaching http://%2:%3")
                      .arg(timeout_ms_).arg(host_).arg(port_);
    } else if (reply->error() != QNetworkReply::NoError) {
        r.error = QStringLiteral("HTTP error: %1").arg(reply->errorString());
    } else {
        r.ok = true;
        r.data = reply->readAll();
    }
    reply->deleteLater();
    return r;
}

bool ps3mapi_client::parse_output(const QByteArray& body, QByteArray* out) {
    // <textarea... id="output"...>HEXBYTES</textarea>, tolerant of attribute order
    // and of whitespace inside the field.
    static const QRegularExpression re(
        QStringLiteral("<textarea[^>]*id=\"output\"[^>]*>(.*?)</textarea>"),
        QRegularExpression::DotMatchesEverythingOption | QRegularExpression::CaseInsensitiveOption);
    const auto m = re.match(QString::fromUtf8(body));
    if (!m.hasMatch()) return false;
    QString hex = m.captured(1);
    hex.remove(QRegularExpression(QStringLiteral("\\s")));
    if (hex.isEmpty() || (hex.size() % 2) != 0) return false;
    *out = QByteArray::fromHex(hex.toLatin1());
    return !out->isEmpty();
}

mapi_result ps3mapi_client::self_check() {
    mapi_result r = read(mem_space::lv1, kProbeAddr, 8, /*allow_unsafe=*/true);
    if (!r.ok) return r;
    std::uint64_t got = 0;
    for (unsigned char b : r.data) got = (got << 8) | b;
    if (got != kProbeWant) {
        r.ok = false;
        r.error = QStringLiteral("self-check FAILED: lv1 %1 read %2, expected %3")
                      .arg(kProbeAddr, 0, 16).arg(got, 0, 16).arg(kProbeWant, 0, 16);
        return r;
    }
    checked_ = true;
    r.error.clear();
    return r;
}

mapi_result ps3mapi_client::read(mem_space space, std::uint64_t addr, std::uint32_t len, bool allow_unsafe) {
    mapi_result r;
    if (len == 0) { r.error = QStringLiteral("zero-length read"); return r; }

    if (space == mem_space::lv1 && !allow_unsafe) {
        auto v = lv1_classify_range(addr, len);
        if (!v.ok) {
            r.error = QStringLiteral("lv1_guard REFUSED %1..%2 -- %3\n"
                                     "    Reading an unmapped lv1 address hangs the console. "
                                     "Pass --unsafe only for ONE address you have vetted.")
                          .arg(addr, 0, 16).arg(addr + len, 0, 16).arg(QString::fromStdString(v.why));
            return r;
        }
    }

    const int proc = (space == mem_space::lv1) ? 1 : 2;
    QByteArray out;
    for (std::uint32_t done = 0; done < len; ) {
        const std::uint32_t want = std::min<std::uint32_t>(kChunk, len - done);
        std::uint64_t at = addr + done;
        if (space == mem_space::lv2) {
            std::uint32_t low = 0; QString err;
            if (!normalise_lv2(at, &low, &err)) { r.error = err; return r; }
            at = low;
        }
        const QString path = QStringLiteral("/getmem.ps3mapi?proc=%1&addr=%2&len=%3")
                                 .arg(proc).arg(at, 0, 16).arg(want);
        mapi_result g = http_get(path);
        if (!g.ok) return g;
        QByteArray chunk;
        if (!parse_output(g.data, &chunk)) {
            r.error = QStringLiteral("no output field in response at %1").arg(at, 0, 16);
            return r;
        }
        if (chunk.size() > int(want)) chunk.truncate(int(want));
        if (chunk.isEmpty()) { r.error = QStringLiteral("empty read at %1").arg(at, 0, 16); return r; }
        out.append(chunk);
        done += std::uint32_t(chunk.size());
    }
    r.ok = true;
    r.data = out;
    return r;
}

mapi_result ps3mapi_client::write(mem_space space, std::uint64_t addr, const QByteArray& bytes, bool allow_unsafe) {
    mapi_result r;
    if (bytes.isEmpty()) { r.error = QStringLiteral("empty write"); return r; }

    if (space == mem_space::lv1 && !allow_unsafe) {
        auto v = lv1_classify_range(addr, std::uint64_t(bytes.size()));
        if (!v.ok) {
            r.error = QStringLiteral("lv1_guard REFUSED write of %1..%2 -- %3")
                          .arg(addr, 0, 16).arg(addr + bytes.size(), 0, 16).arg(QString::fromStdString(v.why));
            return r;
        }
    }

    const int proc = (space == mem_space::lv1) ? 1 : 2;
    std::uint64_t at = addr;
    if (space == mem_space::lv2) {
        std::uint32_t low = 0; QString err;
        if (!normalise_lv2(addr, &low, &err)) { r.error = err; return r; }
        at = low;
    }
    const QString path = QStringLiteral("/setmem.ps3mapi?proc=%1&addr=%2&oper=0&val=%3")
                             .arg(proc).arg(at, 0, 16).arg(QString::fromLatin1(bytes.toHex()));
    mapi_result s = http_get(path);
    if (!s.ok) return s;

    // Read back and verify -- a write that did not take is a failure, not success.
    mapi_result back = read(space, addr, std::uint32_t(bytes.size()), allow_unsafe);
    if (!back.ok) return back;
    if (back.data != bytes) {
        r.error = QStringLiteral("write did NOT take at %1: wrote %2 read %3")
                      .arg(addr, 0, 16).arg(QString::fromLatin1(bytes.toHex()))
                      .arg(QString::fromLatin1(back.data.toHex()));
        return r;
    }
    r.ok = true;
    r.data = back.data;
    return r;
}

} // namespace opentm::tm_core
