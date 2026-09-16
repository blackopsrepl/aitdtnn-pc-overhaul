#include "../src/frame_limiter.hpp"

#include <cstdio>
#include <cstdint>

namespace {

int failures = 0;

void expect_interval(const char* label, std::int64_t actual, std::int64_t expected) {
    if (actual != expected) {
        std::fprintf(stderr, "%s: got %lld expected %lld\n", label,
                     static_cast<long long>(actual), static_cast<long long>(expected));
        ++failures;
    }
}

void expect_wait(const char* label, std::int64_t actual, std::int64_t expected) {
    expect_interval(label, actual, expected);
}

}  // namespace

int main() {
    using aitd4::FrameLimiter;

    expect_interval("60 fps", FrameLimiter::fps_interval_us(60), 16666);
    expect_interval("15 fps", FrameLimiter::fps_interval_us(15), 66666);
    expect_interval("uncapped", FrameLimiter::fps_interval_us(0), 0);
    expect_interval("negative", FrameLimiter::fps_interval_us(-5), 0);

    FrameLimiter limiter;
    limiter.configure(false, 60, FrameLimiter::automatic);
    expect_interval("disabled game", limiter.interval_us(false, 0), 0);
    expect_interval("disabled movie", limiter.interval_us(true, 0), 0);

    limiter.configure(true, 0, FrameLimiter::automatic);
    expect_interval("uncapped game", limiter.interval_us(false, 0), 0);
    expect_interval("auto movie fallback", limiter.interval_us(true, 0), 66666);
    expect_interval("auto movie authored", limiter.interval_us(true, 33333), 33333);

    limiter.configure(true, 60, 30);
    expect_interval("fixed game", limiter.interval_us(false, 0), 16666);
    expect_interval("fixed movie ignores authored", limiter.interval_us(true, 33333), 33333);
    expect_interval("fixed movie ignores fallback", limiter.interval_us(true, 0), 33333);

    // On-time cadence: each call reports the wait that places the next present
    // exactly one interval after the previous present.
    limiter.configure(true, 60, 0);
    limiter.reset();
    const std::int64_t interval = limiter.interval_us(false, 0);
    expect_interval("game interval", interval, 16666);
    std::int64_t now = 0;
    now += limiter.wait_us(now, interval);
    expect_interval("first present immediate", now, 0);
    now += limiter.wait_us(now, interval);
    expect_interval("second present paced", now, 16666);
    now += limiter.wait_us(now, interval);
    expect_interval("third present paced", now, 33332);

    // A long stall resynchronizes instead of bursting to catch up.
    limiter.reset();
    expect_wait("stall first immediate", limiter.wait_us(0, 10000), 0);
    expect_wait("stall present now", limiter.wait_us(50000, 10000), 0);
    expect_wait("stall resynchronized", limiter.wait_us(55000, 10000), 5000);

    // Unchanged configuration keeps the deadline; a changed one restarts it.
    limiter.configure(true, 60, 0);
    limiter.reset();
    expect_wait("configured first immediate", limiter.wait_us(0, 16666), 0);
    limiter.configure(true, 60, 0);
    expect_wait("unchanged config keeps deadline", limiter.wait_us(0, 16666), 16666);
    limiter.configure(true, 30, 0);
    expect_wait("changed config resets deadline", limiter.wait_us(0, 33333), 0);
    expect_wait("changed config paces again", limiter.wait_us(0, 33333), 33333);

    limiter.configure(true, 60, 0);
    expect_wait("uncapped interval resets", limiter.wait_us(7000, 0), 0);
    expect_wait("uncapped stays immediate", limiter.wait_us(9000, 0), 0);

    if (failures) {
        std::fprintf(stderr, "frame limiter tests failed: %d\n", failures);
        return 1;
    }
    std::puts("frame limiter tests passed");
    return 0;
}
