// lv1_guard -- is an lv1 effective address inside something safe to read?
//
// Reading an unmapped lv1 address machine-checks the console (a hard RSOD /
// power-cycle). The host side therefore refuses any lv1 span it cannot vouch
// for, exactly like a write. The ranges below are the lv1 ELF LOAD segments
// plus a hand-curated set of addresses that were read on real hardware without
// incident -- a "not in a LOAD segment" verdict is necessary-but-not-sufficient
// evidence of danger, because the running lv1 maps more than its own image.
//
// Every range here was verified by reading it on real hardware without incident.
// The segment list comes from readelf on the lv1 image; no Sony code is used.
#pragma once

#include <cstdint>
#include <string>

namespace opentm::tm_core {

struct lv1_verdict {
    bool        ok = false;
    std::string why;
};

// Classify a single 8-byte-aligned lv1 address.
lv1_verdict lv1_classify(std::uint64_t addr);

// Classify every aligned address a read of `n` bytes from `addr` would touch.
// A short read that starts just inside a segment can still run off its end.
lv1_verdict lv1_classify_range(std::uint64_t addr, std::uint64_t n);

} // namespace opentm::tm_core
