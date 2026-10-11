#include "tm_core/nor_image.h"

#include <QFile>
#include <QFileInfo>

namespace opentm::tm_core {

namespace {
// Big-endian field readers over a QByteArray (CELL ELF is always big-endian).
std::uint16_t be16(const QByteArray& b, int off) {
    return (std::uint16_t(std::uint8_t(b[off])) << 8) | std::uint8_t(b[off + 1]);
}
std::uint32_t be32(const QByteArray& b, int off) {
    std::uint32_t v = 0;
    for (int i = 0; i < 4; ++i) v = (v << 8) | std::uint8_t(b[off + i]);
    return v;
}
std::uint64_t be64(const QByteArray& b, int off) {
    std::uint64_t v = 0;
    for (int i = 0; i < 8; ++i) v = (v << 8) | std::uint8_t(b[off + i]);
    return v;
}
constexpr std::uint32_t PT_LOAD = 1;
} // namespace

bool nor_image::open(const QString& path, QString* err) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (err) *err = QStringLiteral("cannot open %1: %2").arg(path, f.errorString());
        return false;
    }
    bytes_ = f.readAll();
    path_  = path;
    segs_.clear();
    is_elf_ = false;

    // ELF64 big-endian? e_ident: 7f 45 4c 46 | EI_CLASS=2 | EI_DATA=2
    if (bytes_.size() >= 64 && std::uint8_t(bytes_[0]) == 0x7f && bytes_[1] == 'E'
        && bytes_[2] == 'L' && bytes_[3] == 'F'
        && std::uint8_t(bytes_[4]) == 2 && std::uint8_t(bytes_[5]) == 2) {
        is_elf_ = true;
        const std::uint64_t e_phoff    = be64(bytes_, 0x20);
        const std::uint16_t e_phentsz  = be16(bytes_, 0x36);
        const std::uint16_t e_phnum    = be16(bytes_, 0x38);
        if (e_phentsz < 56) {
            if (err) *err = QStringLiteral("ELF program-header entry too small (%1)").arg(e_phentsz);
            return false;
        }
        for (std::uint16_t i = 0; i < e_phnum; ++i) {
            const std::uint64_t ph = e_phoff + std::uint64_t(i) * e_phentsz;
            if (ph + 56 > std::uint64_t(bytes_.size())) break;
            const std::uint32_t p_type = be32(bytes_, int(ph));
            if (p_type != PT_LOAD) continue;
            // Elf64_Phdr: type(4) flags(4) offset(8) vaddr(8) paddr(8) filesz(8) memsz(8)...
            load_seg s;
            s.offset = be64(bytes_, int(ph + 0x08));
            s.vaddr  = be64(bytes_, int(ph + 0x10));
            s.filesz = be64(bytes_, int(ph + 0x20));
            s.memsz  = be64(bytes_, int(ph + 0x28));
            segs_.push_back(s);
        }
        if (segs_.empty()) {
            if (err) *err = QStringLiteral("ELF has no PT_LOAD segments");
            return false;
        }
    }
    return true;
}

QString nor_image::summary() const {
    const QString name = QFileInfo(path_).fileName();
    if (!is_elf_)
        return QStringLiteral("%1: raw blob, %2 bytes (vaddr == file offset)")
            .arg(name).arg(bytes_.size());
    QString s = QStringLiteral("%1: CELL ELF64-BE, %2 LOAD segment(s):")
                    .arg(name).arg(segs_.size());
    for (const auto& seg : segs_)
        s += QStringLiteral("\n    vaddr %1..%2  filesz %3  (file off %4)")
                 .arg(seg.vaddr, 0, 16).arg(seg.vaddr + seg.memsz, 0, 16)
                 .arg(seg.filesz, 0, 16).arg(seg.offset, 0, 16);
    return s;
}

bool nor_image::read_va(std::uint64_t vaddr, std::uint32_t len, QByteArray* out, QString* err) const {
    if (len == 0) { if (err) *err = QStringLiteral("zero-length read"); return false; }

    if (!is_elf_) {
        if (vaddr + len > std::uint64_t(bytes_.size())) {
            if (err) *err = QStringLiteral("offset %1..%2 past end of blob (%3 bytes)")
                                .arg(vaddr, 0, 16).arg(vaddr + len, 0, 16).arg(bytes_.size());
            return false;
        }
        *out = bytes_.mid(int(vaddr), int(len));
        return true;
    }

    for (const auto& s : segs_) {
        if (vaddr < s.vaddr || vaddr >= s.vaddr + s.memsz) continue;
        const std::uint64_t within = vaddr - s.vaddr;
        if (within + len > s.filesz) {
            if (err) *err = QStringLiteral(
                "read %1..%2 runs past the file-backed part of segment %3..%4 "
                "(filesz %5 -- the tail is .bss, not in the file)")
                .arg(vaddr, 0, 16).arg(vaddr + len, 0, 16)
                .arg(s.vaddr, 0, 16).arg(s.vaddr + s.memsz, 0, 16).arg(s.filesz, 0, 16);
            return false;
        }
        const std::uint64_t file_off = s.offset + within;
        *out = bytes_.mid(int(file_off), int(len));
        return true;
    }
    if (err) *err = QStringLiteral("vaddr %1 is in no LOAD segment").arg(vaddr, 0, 16);
    return false;
}

} // namespace opentm::tm_core
