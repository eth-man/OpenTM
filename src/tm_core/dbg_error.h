#pragma once

#include <cstdint>

// lv2 debugger (lv2dbg) result codes, 0x80010401..0x8001042f.
//
// These are protocol FACTS of the console-side debug ABI: a fixed code -> meaning
// map. The descriptions below are written fresh (our own wording), not copied from
// any vendor header, so the mapping can be used without carrying vendor source. A
// debug reply's result_code field uses these; decoding them turns a bare hex status
// into something a reader can act on.

namespace opentm::tm_core {

// Returns a short human description for an lv2dbg result code, or nullptr if the
// code is outside the known range (callers then just print the raw hex).
inline const char* lv2dbg_error_name(std::uint32_t code) noexcept {
    switch (code) {
    case 0x80010401u: return "invalid process id";
    case 0x80010402u: return "invalid thread id";
    case 0x80010403u: return "illegal register type";
    case 0x80010404u: return "illegal register number";
    case 0x80010405u: return "illegal thread state";
    case 0x80010406u: return "invalid effective address";
    case 0x80010407u: return "process id not found";
    case 0x80010408u: return "out of memory";
    case 0x80010409u: return "invalid arguments";
    case 0x8001040au: return "file not found";
    case 0x8001040bu: return "invalid file type";
    case 0x8001040cu: return "thread id not found";
    case 0x8001040du: return "invalid thread status";
    case 0x8001040eu: return "no process id available";
    case 0x8001040fu: return "event handler not found";
    case 0x80010410u: return "spu: no room";
    case 0x80010411u: return "spu: not found";
    case 0x80010412u: return "spu: already in a process";
    case 0x80010413u: return "invalid primary spu thread id";
    case 0x80010414u: return "thread is not stopped";
    case 0x80010415u: return "invalid thread type";
    case 0x80010416u: return "continue failed";
    case 0x80010417u: return "stop failed";
    case 0x80010418u: return "no exception pending";
    case 0x80010419u: return "no more event queues";
    case 0x8001041au: return "event queue not created";
    case 0x8001041bu: return "event queue overflowed";
    case 0x8001041cu: return "not implemented";
    case 0x8001041du: return "queue not registered";
    case 0x8001041eu: return "no more event processes";
    case 0x8001041fu: return "process not registered for debug";
    case 0x80010420u: return "event discarded";
    case 0x80010421u: return "no more sync ids";
    case 0x80010422u: return "sync id already added";
    case 0x80010423u: return "sync id not found";
    case 0x80010424u: return "sync id not acquired";
    case 0x80010425u: return "process already registered";
    case 0x80010426u: return "invalid local-store address";
    case 0x80010427u: return "invalid operation";
    case 0x80010428u: return "invalid module id";
    case 0x80010429u: return "handler already registered";
    case 0x8001042au: return "invalid handler";
    case 0x8001042bu: return "handle not registered";
    case 0x8001042cu: return "operation denied";
    case 0x8001042du: return "handler not initialized";
    case 0x8001042eu: return "handler already initialized";
    case 0x8001042fu: return "illegal core-dump parameter";
    default:          return nullptr;
    }
}

} // namespace opentm::tm_core
