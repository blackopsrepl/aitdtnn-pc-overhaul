#pragma once

#include <cstddef>
#include <cstdint>

namespace aitdtnn::rumble {

constexpr std::uint32_t kEnableRva = 0x000a436cu;
constexpr unsigned char kExpectedEnable[13] = {
    0x55, 0x8b, 0xec, 0x51, 0x89, 0x4d, 0xfc,
    0x8b, 0xe5, 0x5d, 0xc2, 0x04, 0x00,
};
constexpr std::uint32_t kBackendRva = 0x000a4379u;
constexpr unsigned char kExpectedBackend[15] = {
    0x55, 0x8b, 0xec, 0x51, 0x89, 0x4d, 0xfc, 0x32,
    0xc0, 0x8b, 0xe5, 0x5d, 0xc2, 0x08, 0x00,
};
constexpr std::uint32_t kAvailableRva = 0x000a439fu;
constexpr unsigned char kExpectedAvailable[13] = {
    0x55, 0x8b, 0xec, 0x51, 0x89, 0x4d, 0xfc,
    0x32, 0xc0, 0x8b, 0xe5, 0x5d, 0xc3,
};
constexpr unsigned char kSupportedExeSha256[32] = {
    0x56, 0x68, 0x11, 0x8e, 0x0e, 0x19, 0xd5, 0x69,
    0x98, 0x65, 0x00, 0xa1, 0xc8, 0x05, 0xa8, 0x53,
    0x97, 0xc8, 0x68, 0x1e, 0x7b, 0x67, 0x2b, 0x49,
    0xa6, 0x86, 0x45, 0x46, 0x2e, 0xcc, 0xc6, 0x72,
};

struct CodeInterval {
    std::uint32_t first;
    std::uint32_t size;
};

constexpr bool intervals_overlap(CodeInterval left, CodeInterval right) noexcept {
    return left.first < right.first + right.size &&
           right.first < left.first + left.size;
}

constexpr CodeInterval kRumbleCodeIntervals[] = {
    {kEnableRva, 13},
    {kBackendRva, 15},
    {kAvailableRva, 13},
};
constexpr CodeInterval kClaimedCodeIntervals[] = {
    {0x00097c0du, 6},  // audio dispatcher
    {0x000a3e02u, 5}, // renderer input getter
    {0x0007fce5u, 5}, // renderer character confirmation
    {0x000bae94u, 5}, // renderer post-selection movie continuation
    {0x000812cfu, 9}, // renderer movie request
    {0x0009dedfu, 5}, // renderer movie skip getter
    {0x0009df37u, 5}, // renderer native Bink frame lifecycle
};

}  // namespace aitdtnn::rumble
