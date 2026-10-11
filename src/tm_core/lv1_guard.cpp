#include "tm_core/lv1_guard.h"

#include <array>
#include <cstdio>
#include <vector>

namespace opentm::tm_core {
namespace {

// lv1 ELF LOAD segments (vaddr, memsz) from readelf -l on the 4.93 lv1 image.
struct seg { std::uint64_t va, sz; const char* fl; };
constexpr std::array<seg, 4> kSegs{{
    {0x000000, 0x004400, "RWE"},
    {0x200000, 0x1e6c00, "RWE"},
    {0x400000, 0x200000, "RW"},
    {0x600000, 0x001050, "RW"},
}};

// Ranges that must never be read (possible key material, or fused-off apertures
// that machine-check the console).
struct span { std::uint64_t lo, hi; const char* why; };
constexpr std::array<span, 5> kNeverDump{{
    {0x20000406000ULL, 0x20000408000ULL, "SPE 3 priv1 -- FUSED OFF, RSODs"},
    {0x2000040e000ULL, 0x20000410000ULL, "SPE 7 priv1 -- DISABLED"},
    {0x370c00ULL, 0x370d40ULL, "high-entropy, possible CSPRNG/key pool"},
    {0x074000ULL, 0x074100ULL, "descriptor sel 15/16/17 blobs"},
    {0x010190ULL, 0x0101a0ULL, "descriptor +0x90/+0x98"},
}};

// Addresses MEASURED to kill the console. Refused by overlap, naming the cost.
constexpr std::array<span, 1> kKnownFatal{{
    {0x2000050A050ULL, 0x2000050A058ULL,
     "MIC_Aux_Trc_Base: an lv1peek READ of this killed the console. "
     "The neighbour MIC_Ctl_Cnfg2 at 0x2000050A040 reads fine; this is the single address."},
}};

// Outside the LOAD segments but read on silicon without incident, repeatedly.
// Each entry is backed by a logged hardware read -- adding one needs the same.
constexpr std::array<span, 15> kProvenSafe{{
    {0x010100ULL, 0x010190ULL, "metldr descriptor object"},
    {0x0101a0ULL, 0x010200ULL, "metldr descriptor object, past the never-dump gap"},
    {0x064000ULL, 0x066000ULL, "descriptor sel 0/12: metldr container + LS 0x3E400 authority"},
    {0x004400ULL, 0x200000ULL, "lv1 low heap/runtime image: densely scanned on silicon"},
    {0x761000ULL, 0x761400ULL, "radix bucket bitmap object: read clean"},
    {0x600000ULL, 0x6c0000ULL, "lv1 heap: scanned end-to-end on silicon"},
    {0x20000509800ULL, 0x20000509808ULL, "PRV +0x800 TS_CTSR1: read on silicon"},
    {0x20000509c00ULL, 0x20000509c08ULL, "PRV +0xC00 checkstop_fir: read on silicon"},
    {0x20000509c08ULL, 0x20000509c10ULL, "PRV +0xC08 recoverable_fir: read on silicon"},
    {0x20000509c10ULL, 0x20000509c18ULL, "PRV +0xC10 spec_att_mchk_fir: read on silicon"},
    {0x20000509c18ULL, 0x20000509c20ULL, "PRV +0xC18 fir_mode_reg: read on silicon"},
    {0x20000509c20ULL, 0x20000509c28ULL, "PRV +0xC20 fir_enable_mask: read on silicon"},
    {0x20000509c80ULL, 0x20000509c88ULL, "PRV +0xC80 serial_number: read on silicon"},
    // SPE priv1 split to EXCLUDE the fused-off SPE 3 and disabled SPE 7.
    {0x20000400000ULL, 0x20000406000ULL, "SPE priv1, SPE 0-2: read on silicon"},
    {0x20000408000ULL, 0x2000040e000ULL, "SPE priv1, SPE 4-6: read on silicon"},
    // NOTE: the SPE4 MMIO dwords (SPU_Mbox_Stat/SPU_Status/puint_mb at 0x20000244010/020,
    // 0x20000264000) are reachable only through lv1peek (hc183), NOT PS3MAPI's getmem, which
    // returns zeros for SPE MMIO silently. They are deliberately not vouched here
    // because this guard fronts the getmem transport.
}};

bool in_span(std::uint64_t a, const span& s) { return s.lo <= a && a < s.hi; }

} // namespace

lv1_verdict lv1_classify(std::uint64_t a) {
    for (const auto& s : kNeverDump)
        if (in_span(a, s)) return {false, std::string("NEVER-DUMP range -- ") + s.why};
    if (a & 7u)
        return {false, "UNALIGNED (lv1 read issues a 64-bit ld; must be 8-byte aligned)"};
    for (const auto& s : kSegs)
        if (s.va <= a && a < s.va + s.sz) {
            char buf[96];
            std::snprintf(buf, sizeof buf, "inside LOAD %#llx..%#llx (%s)",
                          (unsigned long long)s.va, (unsigned long long)(s.va + s.sz), s.fl);
            return {true, buf};
        }
    for (const auto& s : kProvenSafe)
        if (in_span(a, s)) return {true, std::string("outside LOAD but PROVEN on silicon: ") + s.why};
    return {false, "OUTSIDE every LOAD segment -- DO NOT PEEK"};
}

lv1_verdict lv1_classify_range(std::uint64_t a, std::uint64_t n) {
    for (const auto& s : kKnownFatal)
        if (a < s.hi && (a + (n ? n : 1)) > s.lo)
            return {false, std::string("KNOWN FATAL -- ") + s.why};
    if (n == 0) return {false, "zero-length read"};
    for (std::uint64_t x = (a & ~7ULL); x < a + n; x += 8) {
        auto v = lv1_classify(x);
        if (!v.ok) {
            char buf[32];
            std::snprintf(buf, sizeof buf, "%#llx: ", (unsigned long long)x);
            return {false, std::string(buf) + v.why};
        }
    }
    return {true, "whole span mapped"};
}

} // namespace opentm::tm_core
