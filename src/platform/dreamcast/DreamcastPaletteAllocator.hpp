#pragma once
#include <cstdint>

namespace helengine::dreamcast {
/// Allocates the PVR's shared 1024 entries in sixteen-entry units without overwriting another live texture.
class DreamcastPaletteAllocator {
public:
    /// Returns an aligned first entry, or -1 if no complete PAL4/PAL8 bank is available.
    int allocate(unsigned entries) noexcept {
        if (entries != 16 && entries != 256) return -1;
        const unsigned units = entries / 16;
        for (unsigned start = 0; start < 64; start += units) {
            const std::uint64_t mask = ((std::uint64_t{1} << units) - 1) << start;
            if ((occupied_ & mask) == 0) { occupied_ |= mask; return static_cast<int>(start * 16); }
        }
        return -1;
    }
    /// Releases a bank previously acquired by its owner; texture disposal calls this once.
    void release(unsigned first, unsigned entries) noexcept {
        if ((entries != 16 && entries != 256) || first >= 1024 || first % entries != 0) return;
        const std::uint64_t mask = ((std::uint64_t{1} << (entries / 16)) - 1) << (first / 16);
        occupied_ &= ~mask;
    }
private:
    /// One occupancy bit for each sixteen-entry hardware bank.
    std::uint64_t occupied_ = 0;
};
}
