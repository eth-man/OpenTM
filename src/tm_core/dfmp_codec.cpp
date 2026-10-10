#include "dfmp_codec.h"

#include "be_io.h"

#include <cstring>

namespace opentm::tm_core {

std::size_t encode_dfmp(const dfmp_frame& f, std::vector<std::byte>& out) {
    const std::size_t total = f.wire_size();
    const std::size_t start = out.size();
    out.resize(start + total);
    std::byte* p = out.data() + start;

    write_be_u32(p + 0,  f.cmd);
    write_be_u32(p + 4,  f.seq);
    write_be_u32(p + 8,  static_cast<std::uint32_t>(f.body.size()));
    write_be_u32(p + 12, f.status_or_flags);
    if (!f.body.empty()) {
        std::memcpy(p + dfmp_header_size, f.body.data(), f.body.size());
    }
    return total;
}

dfmp_decode_result decode_dfmp(std::span<const std::byte> buffer) noexcept {
    dfmp_decode_result r{};
    if (buffer.size() < dfmp_header_size) {
        r.error = dfmp_decode_error::short_buffer;
        return r;
    }
    const std::byte* p = buffer.data();
    dfmp_frame f;
    f.cmd = read_be_u32(p + 0);
    f.seq = read_be_u32(p + 4);
    const std::uint32_t body_len = read_be_u32(p + 8);
    f.status_or_flags = read_be_u32(p + 12);
    if (dfmp_header_size + body_len > buffer.size()) {
        r.error = dfmp_decode_error::body_truncated;
        return r;
    }
    f.body.assign(p + dfmp_header_size, p + dfmp_header_size + body_len);
    r.frame = std::move(f);
    r.consumed = dfmp_header_size + body_len;
    return r;
}

std::vector<dfmp_file_entry> parse_get_entries_reply(
    std::span<const std::byte> reply_body)
{
    // all multi byte fields are BE
    //
    // +0x00 u32 reserved (sometimes carries the entry index for the (first record, unused now)
    // +0x04 u32 zero
    // +0x08 u32 zero
    // +0x0C u32 type/valid flag (= 1 for valid entries)
    // +0x10 u32 mode unix style 0x41ff = S_IFDIR|0777, 0x81xx = file
    // +0x14 u32 zero padding
    // +0x18 u32 ctime in posix seconds
    // +0x1C u32 zero
    // +0x20 u32 atime
    // +0x24 u32 zero
    // +0x28 u32 mtime
    // +0x2C u32 zero
    // +0x30 u32 block_size / flag (0x200 or 0x400)
    // +0x34 char[] name, nul terminated, fills the rest of the entry slot
    //
    //
    // res:
    // 24 bytes before the entry array is:
    // +0x00..+0x10 reserved (mostly zero, +0x07 = 0x1b protocol marker?)
    // +0x14 u32 BE entry count
    std::vector<dfmp_file_entry> entries;
    constexpr std::size_t reply_header = 24;
    if (reply_body.size() < reply_header) return entries;

    const std::byte* p = reply_body.data();
    const std::uint32_t count = read_be_u32(p + 20);
    if (count == 0) return entries;

    const std::size_t blob_len = reply_body.size() - reply_header;

    constexpr std::size_t entry_size = 296; // fixed! do not change
    if (count * entry_size > blob_len) {
        return entries;
    }

    entries.reserve(count);
    const std::byte* base = p + reply_header;
    for (std::uint32_t i = 0; i < count; ++i) {
        const std::byte* e = base + i * entry_size;
        dfmp_file_entry fe;
        fe.mode  = read_be_u32(e + 0x10);
        if (fe.mode == 0) fe.mode = 0x4000u;
        fe.ctime = read_be_u32(e + 0x18);
        fe.atime = read_be_u32(e + 0x20);
        fe.mtime = read_be_u32(e + 0x28);
        fe.size  = read_be_u32(e + 0x30);

        // name is nul terminated, starts at +0x34, fills remainder of entry
        const std::size_t name_max = entry_size - 0x34;
        const char* name_start = reinterpret_cast<const char*>(e + 0x34);
        std::size_t name_len = 0;
        while (name_len < name_max && name_start[name_len] != '\0') ++name_len;
        fe.name.assign(name_start, name_len);

        entries.push_back(std::move(fe));
    }
    return entries;
}

std::vector<std::byte> build_path_op_body(
    std::uint32_t param_c,
    std::string_view path)
{
    // body layout after the 16 byte outer header:
    // +0x00 u32 BE param_c (sub op discriminator, like 0x36 OpenDir)
    // +0x04 u32 BE reserved (zero)
    // +0x08... path bytes + nul padded to fill the 1056 byte path slot
    constexpr std::size_t inner_header = 8;
    std::vector<std::byte> body(inner_header + dfmp_path_slot, std::byte{0});
    std::byte* p = body.data();
    write_be_u32(p + 0,  param_c);
    write_be_u32(p + 4,  0);
    const std::size_t copy_len = std::min(path.size(), dfmp_path_slot - 1); // leave room for nul
    std::memcpy(p + inner_header, path.data(), copy_len);
    return body;
}

std::vector<std::byte> build_rename_body(std::string_view from, std::string_view to) {
    constexpr std::size_t inner_header = 8;
    std::vector<std::byte> body(inner_header + dfmp_path_slot * 2, std::byte{0});
    std::byte* p = body.data();
    write_be_u32(p + 0, dfmp_file_op_kind::rename);
    write_be_u32(p + 4, 0);
    const std::size_t from_len = std::min(from.size(), dfmp_path_slot - 1);
    const std::size_t to_len   = std::min(to.size(),   dfmp_path_slot - 1);
    std::memcpy(p + inner_header, from.data(), from_len);
    std::memcpy(p + inner_header + dfmp_path_slot, to.data(), to_len);
    return body;
}

std::vector<std::byte> build_mkdir_body(std::uint32_t mode, std::string_view path) {
    constexpr std::size_t inner_header = 12;
    std::vector<std::byte> body(inner_header + dfmp_path_slot, std::byte{0});
    std::byte* p = body.data();
    write_be_u32(p + 0, dfmp_file_op_kind::make_dir);
    write_be_u32(p + 4, 0);
    write_be_u32(p + 8, mode);
    const std::size_t copy_len = std::min(path.size(), dfmp_path_slot - 1);
    std::memcpy(p + inner_header, path.data(), copy_len);
    return body;
}

std::vector<std::byte> build_chmod_body(std::uint32_t mode, std::string_view path) {
    constexpr std::size_t inner_header = 12;
    std::vector<std::byte> body(inner_header + dfmp_path_slot, std::byte{0});
    std::byte* p = body.data();
    write_be_u32(p + 0, dfmp_file_op_kind::chmod);
    write_be_u32(p + 4, 0);
    write_be_u32(p + 8, mode);
    const std::size_t copy_len = std::min(path.size(), dfmp_path_slot - 1);
    std::memcpy(p + inner_header, path.data(), copy_len);
    return body;
}

std::vector<std::byte> build_utime_body(
    std::uint64_t atime, std::uint64_t mtime, std::string_view path)
{
    constexpr std::size_t inner_header = 24;
    std::vector<std::byte> body(inner_header + dfmp_path_slot, std::byte{0});
    std::byte* p = body.data();
    write_be_u32(p + 0,  dfmp_file_op_kind::utime);
    write_be_u32(p + 4,  0);
    write_be_u64(p + 8,  atime);
    write_be_u64(p + 16, mtime);
    const std::size_t copy_len = std::min(path.size(), dfmp_path_slot - 1);
    std::memcpy(p + inner_header, path.data(), copy_len);
    return body;
}

std::string host_transfer_path(std::string_view host_absolute_path) {
    std::string out = "/app_home/";
    out.append(host_absolute_path);
    return out;
}

std::vector<std::byte> build_transfer_body(
    dfmp_transfer_direction dir,
    std::string_view source,
    std::string_view destination,
    std::uint32_t    size,
    std::uint32_t    mtime)
{
    std::vector<std::byte> body(dfmp_transfer_slot_2 + dfmp_path_slot, std::byte{0});
    std::byte* p = body.data();

    const bool pulling = dir == dfmp_transfer_direction::to_host;

    const std::uint32_t mode  = pulling ? 3u : 1u;
    const std::uint32_t flag  = pulling ? 1u : 0u;

    write_be_u32(p + 0x00, dfmp_file_op_kind::transfer);
    write_be_u32(p + 0x04, mode);
    write_be_u32(p + 0x08, flag);
    write_be_u32(p + 0x10, pulling ? 0u : mtime);
    write_be_u32(p + 0x18, size);
    write_be_u32(p + 0x1C, mode);

    auto put_path = [&](std::size_t at, std::string_view s) {
        const std::size_t n = std::min(s.size(), dfmp_path_slot - 1);
        std::memcpy(p + at, s.data(), n);
    };
    put_path(dfmp_transfer_meta_size, source);
    put_path(dfmp_transfer_slot_2,    destination);
    return body;
}

std::vector<dfmp_file_entry> parse_get_entries_reply_dex(
    std::span<const std::byte> reply_body)
{
    // see the header for the layout and how it differs from the decr vs dex
    constexpr std::size_t kHeader = 0x20;
    constexpr std::size_t kStride = 296;
    constexpr std::size_t kType   = 0x04;
    constexpr std::size_t kName   = 0x2c;

    std::vector<dfmp_file_entry> out;
    if (reply_body.size() < kHeader + kStride) return out;

    const std::uint32_t declared = read_be_u32(reply_body, 0x14);
    const std::size_t   fits     = (reply_body.size() - kHeader) / kStride;
    // jsut trsust# whichever is smaller
    const std::size_t   count    = std::min<std::size_t>(declared, fits);
    out.reserve(count);

    for (std::size_t i = 0; i < count; ++i) {
        const std::size_t base = kHeader + i * kStride;
        dfmp_file_entry fe;
        // rexpress the drfp d_type as the unix mode bits the rest of the codebase (plus dfmp_file_entry::is_directory) expects
        switch (read_be_u32(reply_body, base + kType)) {
        case 1:  fe.mode = 0x4000u; break; // dir
        case 2:  fe.mode = 0x8000u; break; // normal file
        default: fe.mode = 0u; break;
        }
        std::string name;
        for (std::size_t k = base + kName; k < base + kStride; ++k) {
            const auto c = std::to_integer<char>(reply_body[k]);
            if (c == '\0') break;
            name.push_back(c);
        }
        if (name.empty()) continue;
        fe.name = std::move(name);
        out.push_back(std::move(fe));
    }
    return out;
}

} // namespace opentm::tm_core
