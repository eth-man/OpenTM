// inner framing for DECI3 cat=0x0200 DFMP payloads, the family the kits file management rides on
//
// outer header, inside a cat=0x0200 envelope:
// +0x00 u32 cmd high bit set on replies
// +0x04 u32 seq replies echo the requests seq
// +0x08 u32 body_length NOT counting the 1 byte frame trailer
// +0x0c u32 status_or_flags 0 on requests, lv2 status on replies
// +0x10... body
//
// per op body header, the next 8 bytes:
// +0x00 u16 param_a usually a flag word
// +0x02 u16 param_b usually a mode/handle slot
// +0x04 u32 param_c usually a size hint or per op id
//
// most path bearing ops then have 4 reserved bytes and a nul terminated path in a fixed 1kb byte slot, so a body is typically ~1064 bytes

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace opentm::tm_core {

inline constexpr std::size_t  dfmp_header_size   = 16;
inline constexpr std::uint32_t dfmp_reply_bit    = 0x80000000u;
// body_len = 0x428 = 1064 = 8 (inner header) + 1056 (path slot)
// da validates this size and rejects mismatches with "initialize_reply() called in wrong state" + 0xFFFFFFFF in the reply
inline constexpr std::size_t  dfmp_path_slot     = 1056;


namespace dfmp_op_cmd {
inline constexpr std::uint32_t open_directory   = 0x00200013u;
inline constexpr std::uint32_t file_operation   = 0x0020000Fu;

// seen from the file manager but idk what these do... listed so the decoder doesnt choke on them
inline constexpr std::uint32_t op_0014          = 0x00200014u;
inline constexpr std::uint32_t op_0015          = 0x00200015u;
inline constexpr std::uint32_t op_0028          = 0x00200028u;

constexpr std::uint32_t reply_of(std::uint32_t request) noexcept {
    return request | dfmp_reply_bit;
}

constexpr bool is_reply(std::uint32_t cmd) noexcept {
    return (cmd & dfmp_reply_bit) != 0;
}

} // namespace dfmp_op_cmd

namespace dfmp_file_op_kind {

// path = directory to enumerate
// res:
// +0x00 u32 BE reserved (zero)
// +0x04 u32 BE entry size (typically 0x1b = 27)
// +0x08 u32 BE reserved (zero)
// +0x0C u32 BE reserved (zero)
// +0x10 u32 BE reserved (zero)
// +0x14 u32 BE entry count
// +0x18... entry array, each entry per kit side struct (88 bytes)
inline constexpr std::uint32_t get_entries  = 0x0000001Au;

// path = file to delete
inline constexpr std::uint32_t remove       = 0x00000008u;

// path = file to stat. Goes with cmd 0x00200013, not the workhorse.
// reply body carries mode at +0x12 (u16, e.g. 0x81a4 = S_IFREG|0644), three timestamps at +0x20/+0x28/+0x30 and the size at +0x38 (u64).
inline constexpr std::uint32_t stat          = 0x0000001Cu;

// +0x00 u32 this kind (0x12)
// +0x04 u32 3 pulling to the host, 1 pushing to the target
// +0x08 u32 1 pulling to the host, 0 pushing to the target
// +0x10 u32 source mtime when pushing, 0 when pulling
// +0x18 u32 size of the file being moved
// +0x1C u32 repeats +0x04
// +0x20 path slot, source
// +0x440 path slot, destination

inline constexpr std::uint32_t transfer      = 0x00000012u;

// Opens the directory (paired with file_operation get_entries on the
// same path). For OpenDir specifically the cmd is 0x00200013, not the
// workhorse - this value is the param_c that goes with it.
inline constexpr std::uint32_t open_dir_marker = 0x00000036u;

// rename. Two path slots back to back, source then destination, and no
// other fields. Reply 0x0D. Recovered from ps3tmserver's NS_FTP_DQ_RENAMER
// builder, not from a capture - TM never put one on the wire in any of ours.
inline constexpr std::uint32_t rename        = 0x0000000Cu;

// mkdir. u32 mode then the path slot, same shape as chmod. Reply 0x0B.
// Decoded from NS_FTP_DQ_MKDIRR. The mode is passed straight through to the
// kit; unlike chmod it carries no file-type bits in the builder we read.
inline constexpr std::uint32_t make_dir      = 0x0000000Au;

// chmod. Body is the usual path op with the full st_mode (S_IFREG and the
// permission bits together) at +0x08, so the caller must keep the file-type
// bits the listing reported. Reply 0x17.
inline constexpr std::uint32_t chmod         = 0x00000016u;

// utime. atime then mtime, both u64 BE posix seconds, path slot at +0x18.
// TM converts from host local time, so 22:34:38 local goes on the wire as
// 21:34:38 UTC. Reply 0x19.
inline constexpr std::uint32_t utime         = 0x00000018u;

} // namespace dfmp_file_op_kind

// +0x00 u32 BE reserved (sometimes carries an index in the first entry)
// +0x04 u32 BE zero
// +0x08 u32 BE zero
// +0x0C u32 BE type/valid flag (= 1 for valid entries)
// +0x10 u32 BE mode, unix style
// +0x14 u32 BE zero (padding)
// +0x18 u32 BE ctime (posix seconds)
// +0x1C u32 BE zero
// +0x20 u32 BE atime
// +0x24 u32 BE zero
// +0x28 u32 BE mtime
// +0x2C u32 BE zero
// +0x30 u32 BE block_size / sub-flag

struct dfmp_frame {
    std::uint32_t       cmd            = 0; // full u32
    std::uint32_t       seq            = 0;
    std::uint32_t       status_or_flags = 0;
    std::vector<std::byte> body; // bytes after 16b headerd

    bool is_reply() const noexcept { return dfmp_op_cmd::is_reply(cmd); }
    std::size_t wire_size() const noexcept { return dfmp_header_size + body.size(); }
};

enum class dfmp_decode_error {
    short_buffer,
    body_truncated,
};

struct dfmp_decode_result {
    std::optional<dfmp_frame>          frame;
    std::size_t                        consumed = 0;
    std::optional<dfmp_decode_error>   error;
};

std::size_t encode_dfmp(const dfmp_frame& frame, std::vector<std::byte>& out);


dfmp_decode_result decode_dfmp(std::span<const std::byte> buffer) noexcept;

// xfer body layout: 32 bytes of metadata, then two path slots
inline constexpr std::size_t dfmp_transfer_meta_size = 32;
inline constexpr std::size_t dfmp_transfer_slot_2    = dfmp_transfer_meta_size + dfmp_path_slot;

std::vector<std::byte> build_path_op_body(
    std::uint32_t param_c,
    std::string_view path);

std::vector<std::byte> build_chmod_body(std::uint32_t mode, std::string_view path);

std::vector<std::byte> build_rename_body(std::string_view from, std::string_view to);

std::vector<std::byte> build_mkdir_body(std::uint32_t mode, std::string_view path);

std::vector<std::byte> build_utime_body(std::uint64_t atime, std::uint64_t mtime, std::string_view path);

enum class dfmp_transfer_direction {
    to_host,     // pull a target file down
    to_target,   // push a host file up
};


// mtime only carried when pushing, pulling ignores it.
std::vector<std::byte> build_transfer_body(
    dfmp_transfer_direction dir,
    std::string_view source,
    std::string_view destination,
    std::uint32_t    size,
    std::uint32_t    mtime = 0);

// prefix a host path the way the target expects to see it.
std::string host_transfer_path(std::string_view host_absolute_path);

struct dfmp_file_entry {
    std::string   name;
    std::uint64_t size  = 0;
    std::uint32_t mode  = 0;
    std::uint32_t ctime = 0;
    std::uint32_t atime = 0;
    std::uint32_t mtime = 0;
    bool is_directory()    const noexcept { return (mode & 0x4000u) != 0; }
    bool is_regular_file() const noexcept { return (mode & 0x8000u) != 0; }
};

std::vector<dfmp_file_entry> parse_get_entries_reply(
    std::span<const std::byte> reply_body);

std::vector<dfmp_file_entry> parse_get_entries_reply_dex(
    std::span<const std::byte> reply_body);

} // namespace opentm::tm_core
