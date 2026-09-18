#include "platform/dreamcast/DreamcastTextureFormat.hpp"

#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>

namespace {

using namespace helengine::dreamcast;

int failures = 0;

void expect(bool condition, const char* expression) {
    if (!condition) {
        std::cerr << "expected " << expression << '\n';
        ++failures;
    }
}

#define EXPECT(value) expect((value), #value)

std::vector<std::uint8_t> makeTexture(std::uint16_t side, std::uint16_t usedWidth, std::uint16_t usedHeight) {
    std::vector<std::uint8_t> bytes(kCookedTextureHeaderBytes + static_cast<std::size_t>(side) * side * 2, 0);
    const std::uint32_t magic = 0x58544344u;
    std::memcpy(bytes.data(), &magic, 4);
    const std::uint16_t version = 1;
    std::memcpy(bytes.data() + 4, &version, 2);
    std::memcpy(bytes.data() + 8, &side, 2);
    std::memcpy(bytes.data() + 10, &usedWidth, 2);
    std::memcpy(bytes.data() + 12, &usedHeight, 2);
    return bytes;
}

void ValidTextureParses() {
    std::vector<std::uint8_t> bytes = makeTexture(4, 3, 2);
    CookedTextureHeader header{};
    EXPECT(parseCookedTexture(bytes.data(), bytes.size(), header) == CookedTextureStatus::Ok);
    EXPECT(header.paddedSide == 4);
    EXPECT(header.usedWidth == 3);
    EXPECT(header.usedHeight == 2);
}

void CorruptTexturesAreRejectedSpecifically() {
    CookedTextureHeader header{};
    {
        std::vector<std::uint8_t> bytes = makeTexture(4, 3, 2);
        bytes[0] = 'X';
        EXPECT(parseCookedTexture(bytes.data(), bytes.size(), header) == CookedTextureStatus::BadMagic);
    }
    {
        std::vector<std::uint8_t> bytes = makeTexture(4, 3, 2);
        bytes[4] = 2;
        EXPECT(parseCookedTexture(bytes.data(), bytes.size(), header) == CookedTextureStatus::BadVersion);
    }
    {
        std::vector<std::uint8_t> bytes = makeTexture(4, 3, 2);
        bytes[6] = 1;
        EXPECT(parseCookedTexture(bytes.data(), bytes.size(), header) == CookedTextureStatus::BadFlags);
    }
    {
        // Side 6 is not a power of two.
        std::vector<std::uint8_t> bytes = makeTexture(4, 3, 2);
        const std::uint16_t side = 6;
        std::memcpy(bytes.data() + 8, &side, 2);
        EXPECT(parseCookedTexture(bytes.data(), bytes.size(), header) == CookedTextureStatus::BadSide);
    }
    {
        // Used extent cannot exceed the padded side.
        std::vector<std::uint8_t> bytes = makeTexture(4, 5, 2);
        EXPECT(parseCookedTexture(bytes.data(), bytes.size(), header) == CookedTextureStatus::BadExtent);
    }
    {
        std::vector<std::uint8_t> bytes = makeTexture(4, 3, 2);
        bytes.resize(bytes.size() - 2);
        EXPECT(parseCookedTexture(bytes.data(), bytes.size(), header) == CookedTextureStatus::SizeMismatch);
    }
    {
        std::vector<std::uint8_t> bytes = makeTexture(4, 3, 2);
        EXPECT(parseCookedTexture(bytes.data(), 4, header) == CookedTextureStatus::TooSmall);
    }
    EXPECT(parseCookedTexture(nullptr, 64, header) == CookedTextureStatus::TooSmall);
}

} // namespace

int main() {
    ValidTextureParses();
    CorruptTexturesAreRejectedSpecifically();
    if (failures != 0) {
        std::cerr << failures << " assertion(s) failed\n";
        return 1;
    }
    std::cout << "DreamcastTextureFormatTests: all tests passed\n";
    return 0;
}
