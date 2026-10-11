// ps3mapi_client -- read/write lv1 and lv2 memory on a CFW console over HTTP.
//
// This is the "beyond a factory devkit" transport. SN Systems' ProDG / DECI3
// only ever talks to the lv2 debug agent; it cannot touch the hypervisor (lv1)
// or lv0. A CFW console exposes a webMAN/PS3MAPI web interface that peeks and
// pokes lv1 and lv2 directly -- below anything the DECI3 debugger can reach.
//
// This backend is independent of the DECI3 session: it is a plain synchronous
// HTTP client, so an lv1/lv2 read needs no target handshake, no debug agent and
// no registered process. The endpoints and their constraints were established
// against a live CFW console and implemented fresh here; no Sony code.
//
// lv1 read GET /getmem.ps3mapi?proc=1&addr=<HEX>&len=<n> (<=256 B/request)
// lv2 read GET /getmem.ps3mapi?proc=2&addr=<HEX>&len=<n>
// lv1 write GET /setmem.ps3mapi?proc=1&addr=<HEX>&oper=0&val=<HEXBYTES>
// lv2 write GET /setmem.ps3mapi?proc=2&addr=<HEX>&oper=0&val=<HEXBYTES>
//
// The response embeds the bytes in <textarea id="output">...</textarea>.
//
// HARD CONSTRAINTS measured against a live console (do not relax):
// * PS3MAPI addresses are parsed as HEX and are 32-bit. lv2 kernel VAs carry a
// 0x80000000_00000000 high half that is masked off here; any other high half
// is refused (a full 64-bit lv2 VA took a console down).
// * lv1 getmem reads lv1's MEMORY IMAGE. SPE MMIO apertures are NOT mapped and
// return zeros silently, even where they are "classified safe".
// * An lv1 read of an unmapped address machine-checks the console, so lv1 reads
// and writes are gated by lv1_guard unless the caller opts out explicitly.
#pragma once

#include <cstdint>

#include <QByteArray>
#include <QString>

namespace opentm::tm_core {

enum class mem_space { lv1, lv2 };

struct mapi_result {
    bool       ok = false;
    QByteArray data; // on a read
    QString    error; // on failure
};

class ps3mapi_client {
public:
    // host: console IP/hostname (webMAN's HTTP port, default 80).
    explicit ps3mapi_client(QString host, quint16 port = 80, int timeout_ms = 10000);

    // Confirm we are really talking to this console's lv1: lv1 0x385138 must
    // read 0x372d08 (a known-good probe value). Call once before trusting a
    // read; returns false (with reason) if the console answers wrong.
    mapi_result self_check();

    // Read `len` bytes. lv1 reads are refused for spans lv1_guard cannot vouch
    // for unless allow_unsafe is set (one address a human has looked at, never a
    // blanket loop override).
    mapi_result read(mem_space space, std::uint64_t addr, std::uint32_t len, bool allow_unsafe = false);

    // Write bytes, read them back, verify. Strictly more dangerous than a read:
    // lv1 writes are guarded identically; the caller must hold a restore value.
    mapi_result write(mem_space space, std::uint64_t addr, const QByteArray& bytes, bool allow_unsafe = false);

private:
    mapi_result http_get(const QString& path); // raw GET
    static bool parse_output(const QByteArray& body, QByteArray* out);
    // Normalise a kernel VA to the 32-bit value PS3MAPI wants; sets *err on a
    // bad high half. Returns the masked low 32 bits.
    static bool normalise_lv2(std::uint64_t addr, std::uint32_t* low, QString* err);

    QString host_;
    quint16 port_;
    int     timeout_ms_;
    bool    checked_ = false;
};

} // namespace opentm::tm_core
