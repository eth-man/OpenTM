// nor_image -- read lv0 (and any NOR-resident blob) by virtual address, from a
// file on disk.
//
// lv0 is the lowest authorable boot level, and a CFW console already runs a
// non-Sony one. It is NOT reachable live through PS3MAPI: lv0 hands control to
// lv1 at boot and is not resident in the lv1/lv2 memory image getmem exposes.
// So the lv0 "backend" is file-backed -- it reads the decrypted lv0 image the
// target actually executes (e.g. extracted/lv0-493.elf for a 4.93 console).
//
// Two addressing modes:
// * ELF image (CELL ELF64, big-endian PPC64): map a virtual address through the
// PT_LOAD program headers to a file offset, so the reader answers at the same
// addresses our lv0 disassembly uses.
// * raw blob: address == file offset.
//
// All offline, no console -- reading a file can never hurt the hardware.
#pragma once

#include <cstdint>
#include <vector>

#include <QByteArray>
#include <QString>

namespace opentm::tm_core {

class nor_image {
public:
    // Load a file. Detects CELL ELF64-BE and parses its PT_LOAD segments; any
    // other file is treated as a raw blob (vaddr == offset). Returns false with
    // *err set on failure.
    bool open(const QString& path, QString* err);

    bool   is_elf() const { return is_elf_; }
    QString summary() const; // human description of what was loaded

    // Read `len` bytes at virtual address `vaddr`. For an ELF, vaddr is resolved
    // through the loaded segments; a read that falls in no segment (or runs off a
    // segment's file-backed end) fails with *err. For a raw blob, vaddr is a byte
    // offset. Never touches hardware.
    bool read_va(std::uint64_t vaddr, std::uint32_t len, QByteArray* out, QString* err) const;

private:
    struct load_seg { std::uint64_t vaddr, offset, filesz, memsz; };

    QByteArray             bytes_;
    std::vector<load_seg>  segs_;
    bool                   is_elf_ = false;
    QString                path_;
};

} // namespace opentm::tm_core
