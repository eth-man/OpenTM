#include "tsmp_codec.h"

#include "be_io.h"

#include <cstring>

namespace opentm::tm_core {

std::size_t encode_tsmp(const tsmp_frame& f, std::vector<std::byte>& out) {
    const std::size_t total = tsmp_header_size + f.body.size();
    const std::size_t start = out.size();
    out.resize(start + total);
    std::byte* p = out.data() + start;
    write_be_u16(p + 0, f.type);
    write_be_u16(p + 2, static_cast<std::uint16_t>(total));
    write_be_u16(p + 4, f.cmd);
    if (!f.body.empty()) {
        std::memcpy(p + tsmp_header_size, f.body.data(), f.body.size());
    }
    return total;
}

const char* tsmp_lpar_state_name(std::uint64_t status) noexcept {
    switch (status) {
    case tsmp_lpar_state::down:    return "down";
    case tsmp_lpar_state::suspend: return "suspend";
    case tsmp_lpar_state::up:      return "up";
    default:                       break;
    }
    return "unknown";
}

std::vector<tsmp_lpar_entry> parse_lpar_status_reply(std::span<const std::byte> body) {
    std::vector<tsmp_lpar_entry> out;
    // entries begin at message offset 12, the body starts at offset 6, so the first 6 bytes of it are the key and result word
    constexpr std::size_t skip = tsmp_lpar_entry_offset - tsmp_header_size;
    if (body.size() < skip) return out;

    const std::size_t payload = body.size() - skip;
    if (payload % tsmp_lpar_entry_size != 0) return out; // partial entry

    out.reserve(payload / tsmp_lpar_entry_size);
    for (std::size_t off = skip; off + tsmp_lpar_entry_size <= body.size();
         off += tsmp_lpar_entry_size)
    {
        const std::byte* p = body.data() + off;
        tsmp_lpar_entry e;
        // the name is a fixed 16 byte field, not necessarily nul terminated
        const char* chars = reinterpret_cast<const char*>(p);
        std::size_t len = 0;
        while (len < 16 && chars[len] != '\0') ++len;
        e.name.assign(chars, len);
        e.status = read_be_u64(p + 16);
        e.detail = read_be_u64(p + 24);
        out.push_back(std::move(e));
    }
    return out;
}

std::optional<tsmp_param_reply> parse_param_reply(std::span<const std::byte> body) noexcept {
    if (body.size() < 6) return std::nullopt;
    tsmp_param_reply r;
    r.sub    = read_be_u16(body.data() + 0);
    r.result = read_be_u32(body.data() + 2);
    if (body.size() >= 14) r.value = read_be_u64(body.data() + 6);
    return r;
}

const char* boot_mode_name(tsmp_boot_mode m) noexcept {
    switch (m) {
    case tsmp_boot_mode::debug:           return "Debug Mode";
    case tsmp_boot_mode::system_software: return "System Software Mode";
    case tsmp_boot_mode::release:         return "Release Mode";
    case tsmp_boot_mode::unknown:         break;
    }
    return "unknown";
}

tsmp_decode_result decode_tsmp(std::span<const std::byte> buffer) noexcept {
    tsmp_decode_result r{};
    if (buffer.size() < tsmp_header_size) {
        r.error = tsmp_decode_error::short_buffer;
        return r;
    }
    const std::byte* p = buffer.data();
    const std::uint16_t length = read_be_u16(p + 2);
    if (length < tsmp_header_size || length > buffer.size()) {
        r.error = tsmp_decode_error::bad_length;
        return r;
    }
    tsmp_frame f;
    f.type = read_be_u16(p + 0);
    f.cmd  = read_be_u16(p + 4);
    const std::size_t body_size = length - tsmp_header_size;
    f.body.assign(p + tsmp_header_size, p + tsmp_header_size + body_size);
    r.frame = std::move(f);
    r.consumed = length;
    return r;
}

} // namespace opentm::tm_core
