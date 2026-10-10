// odd code
// 0 / 1 INIT
// 2 / 3 OPEN
// 4 / 5 CLOSE
// 6 / 7 READ
// 8 / 9 WRITE
// 10 / 11 SEEK
// 12 / 13 FSTAT
// 14 / 15 STAT
// 16 / 17 MKDIR
// 18 / 19 RMDIR
// 20 / 21 DOPEN
// 22 / 23 DCLOSE
// 24 / 25 DREAD
// 26 / 27 FTRUNCATE
// 28 / 29 TRUNCATE
// 30 / 31 RENAME
// 32 / 33 UNLINK

#pragma once

#include "be_io.h"

#include <QByteArray>

#include <cstdint>
#include <optional>

namespace opentm::tm_core {

enum class drfp_code : std::uint32_t {
    init             = 0,
    init_reply       = 1,
    open             = 2,
    open_reply       = 3,
    close            = 4,
    close_reply      = 5,
    read             = 6,
    read_reply       = 7,
    write            = 8,
    write_reply      = 9,
    seek             = 10,
    seek_reply       = 11,
    fstat            = 12,
    fstat_reply      = 13,
    stat             = 14,
    stat_reply       = 15,
    mkdir            = 16,
    mkdir_reply      = 17,
    rmdir            = 18,
    rmdir_reply      = 19,
    dopen            = 20,
    dopen_reply      = 21,
    dclose           = 22,
    dclose_reply     = 23,
    dread            = 24,
    dread_reply      = 25,
    ftruncate        = 26,
    ftruncate_reply  = 27,
    truncate         = 28,
    truncate_reply   = 29,
    rename           = 30,
    rename_reply     = 31,
    unlink           = 32,
    unlink_reply     = 33,
};

const char* drfp_code_name(drfp_code c) noexcept;

struct drfp_frame {
    drfp_code     code;
    std::uint32_t seq;
    QByteArray    payload; 
};

namespace drfp_mode {
inline constexpr std::uint32_t iexec  = 0x00000040u;
inline constexpr std::uint32_t iwrite = 0x00000080u;
inline constexpr std::uint32_t iread  = 0x00000100u;
inline constexpr std::uint32_t ifdir  = 0x00004000u;
inline constexpr std::uint32_t ifreg  = 0x00008000u;
inline constexpr std::uint32_t dir_default  = ifdir | iread | iwrite | iexec;
inline constexpr std::uint32_t file_default = ifreg | iread | iwrite;
} // namespace drfp_mode

namespace drfp_dtype {
inline constexpr std::uint8_t unknown   = 0;
inline constexpr std::uint8_t directory = 1;
inline constexpr std::uint8_t file      = 2;
} // namespace drfp_dtype

// u32 mode // see drfp_mode above
// u32 unknown_a // wire bytes 0xFFFFFFFF (dev?)
// u32 unknown_b // wire bytes 0xFFFFFFFF (rdev?)
// u64 mtime // unix seconds (big-endian)
// u64 atime
// u64 ctime
// u32 unknown_c // 0
// u32 size // file size in bytes
struct drfp_stat {
    std::uint32_t mode      = 0x8180;
    std::uint32_t unknown_a = 0xffffffffu;
    std::uint32_t unknown_b = 0xffffffffu;
    std::uint64_t mtime     = 0;
    std::uint64_t atime     = 0;
    std::uint64_t ctime     = 0;
    std::uint32_t unknown_c = 0;
    std::uint32_t size      = 0;
};

std::optional<drfp_frame> parse_drfp(const QByteArray& payload);
QByteArray build_drfp(drfp_code code, std::uint32_t seq, const QByteArray& payload = QByteArray());
QByteArray build_open_reply(std::uint32_t seq, std::int32_t result, std::uint32_t fd);
QByteArray build_result_reply(drfp_code reply_code, std::uint32_t seq, std::int32_t result);
QByteArray build_read_reply(std::uint32_t seq, std::int32_t result, const QByteArray& data);
QByteArray build_write_reply(std::uint32_t seq, std::int32_t result, std::uint32_t nbytes);
QByteArray build_dopen_reply(std::uint32_t seq, std::int32_t result, std::uint32_t fd);
QByteArray build_dread_reply(std::uint32_t seq, std::int32_t result, const QByteArray& entry);
QByteArray build_dirent(std::uint8_t d_type, const QByteArray& name);
QByteArray build_stat_reply(drfp_code reply_code, std::uint32_t seq, std::int32_t result, const drfp_stat& st);
QByteArray build_init_reply(std::uint32_t seq);

} // namespace opentm::tm_core
