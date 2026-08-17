#include "sha256.hpp"

#include <cstring>

namespace aitdtnn::loader {
namespace {

constexpr std::uint32_t kRoundConstants[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u,
    0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
    0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
    0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
    0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
    0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u,
    0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u,
    0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
    0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u,
};

constexpr std::uint32_t rotate_right(std::uint32_t value, unsigned count) noexcept {
    return (value >> count) | (value << (32u - count));
}

std::uint32_t load_be32(const std::uint8_t* input) noexcept {
    return (static_cast<std::uint32_t>(input[0]) << 24u) |
           (static_cast<std::uint32_t>(input[1]) << 16u) |
           (static_cast<std::uint32_t>(input[2]) << 8u) |
           static_cast<std::uint32_t>(input[3]);
}

void store_be32(std::uint8_t* output, std::uint32_t value) noexcept {
    output[0] = static_cast<std::uint8_t>(value >> 24u);
    output[1] = static_cast<std::uint8_t>(value >> 16u);
    output[2] = static_cast<std::uint8_t>(value >> 8u);
    output[3] = static_cast<std::uint8_t>(value);
}

}  // namespace

Sha256::Sha256() noexcept
    : state_{0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
             0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u},
      bit_count_(0),
      block_{},
      used_(0) {}

void Sha256::transform(const std::uint8_t block[64]) noexcept {
    std::uint32_t schedule[64]{};
    for (std::size_t index = 0; index < 16; ++index) {
        schedule[index] = load_be32(block + index * 4);
    }
    for (std::size_t index = 16; index < 64; ++index) {
        const std::uint32_t s0 = rotate_right(schedule[index - 15], 7) ^
                                 rotate_right(schedule[index - 15], 18) ^
                                 (schedule[index - 15] >> 3u);
        const std::uint32_t s1 = rotate_right(schedule[index - 2], 17) ^
                                 rotate_right(schedule[index - 2], 19) ^
                                 (schedule[index - 2] >> 10u);
        schedule[index] = schedule[index - 16] + s0 + schedule[index - 7] + s1;
    }

    std::uint32_t a = state_[0];
    std::uint32_t b = state_[1];
    std::uint32_t c = state_[2];
    std::uint32_t d = state_[3];
    std::uint32_t e = state_[4];
    std::uint32_t f = state_[5];
    std::uint32_t g = state_[6];
    std::uint32_t h = state_[7];

    for (std::size_t index = 0; index < 64; ++index) {
        const std::uint32_t sum1 = rotate_right(e, 6) ^ rotate_right(e, 11) ^
                                   rotate_right(e, 25);
        const std::uint32_t choose = (e & f) ^ (~e & g);
        const std::uint32_t temp1 = h + sum1 + choose + kRoundConstants[index] +
                                    schedule[index];
        const std::uint32_t sum0 = rotate_right(a, 2) ^ rotate_right(a, 13) ^
                                   rotate_right(a, 22);
        const std::uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
        const std::uint32_t temp2 = sum0 + majority;

        h = g;
        g = f;
        f = e;
        e = d + temp1;
        d = c;
        c = b;
        b = a;
        a = temp1 + temp2;
    }

    state_[0] += a;
    state_[1] += b;
    state_[2] += c;
    state_[3] += d;
    state_[4] += e;
    state_[5] += f;
    state_[6] += g;
    state_[7] += h;
}

void Sha256::update(const void* data, std::size_t length) noexcept {
    const auto* input = static_cast<const std::uint8_t*>(data);
    bit_count_ += static_cast<std::uint64_t>(length) * 8u;

    while (length != 0) {
        const std::size_t available = 64u - used_;
        const std::size_t take = length < available ? length : available;
        std::memcpy(block_ + used_, input, take);
        used_ += take;
        input += take;
        length -= take;
        if (used_ == 64u) {
            transform(block_);
            used_ = 0;
        }
    }
}

void Sha256::finish(std::uint8_t digest[32]) noexcept {
    const std::uint64_t original_bit_count = bit_count_;
    const std::uint8_t marker = 0x80u;
    update(&marker, 1);
    const std::uint8_t zero = 0;
    while (used_ != 56u) {
        update(&zero, 1);
    }

    std::uint8_t length_bytes[8]{};
    for (std::size_t index = 0; index < 8; ++index) {
        length_bytes[7u - index] =
            static_cast<std::uint8_t>(original_bit_count >> (index * 8u));
    }
    update(length_bytes, sizeof(length_bytes));

    for (std::size_t index = 0; index < 8; ++index) {
        store_be32(digest + index * 4u, state_[index]);
    }
}

}  // namespace aitdtnn::loader
