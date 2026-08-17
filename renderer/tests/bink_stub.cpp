#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdint>
#include <cstddef>

struct BinkStubState {
    std::uint32_t movie_open_calls{};
    std::uint32_t movie_open_flags{};
    std::uint32_t open_width{};
    std::uint32_t open_height{};
    std::uint32_t scale_width{};
    std::uint32_t scale_height{};
    int offset_x{};
    int offset_y{};
    std::uint32_t do_frame_calls{};
    std::uint32_t copy_calls{};
    std::uint32_t blit_calls{};
    std::uint32_t lock_calls{};
    std::uint32_t unlock_calls{};
    std::uint32_t get_rects_calls{};
    std::uint32_t next_frame_calls{};
    std::uint32_t close_calls{};
};

namespace {
BinkStubState state;
struct StubMovie {
    std::uint32_t width{};
    std::uint32_t height{};
    std::uint32_t frames{};
    std::uint32_t frame{};
    std::uint8_t reserved[0x24]{};
};
struct StubBuffer {
    std::uint32_t width{};
    std::uint32_t height{};
    std::uint32_t reserved_08{};
    std::uint32_t reserved_0c{};
    std::uint32_t surface{};
    void* pixels{};
    std::int32_t pitch{};
};
static_assert(offsetof(StubMovie, frames) == 0x08);
static_assert(offsetof(StubMovie, frame) == 0x0C);
static_assert(offsetof(StubBuffer, surface) == 0x10);
static_assert(offsetof(StubBuffer, pixels) == 0x14);
static_assert(offsetof(StubBuffer, pitch) == 0x18);
StubMovie movie;
StubBuffer buffer;
std::uint8_t pixels[640 * 480 * 3]{};
}

extern "C" __declspec(dllexport) void* WINAPI BinkOpen(
    const char*, std::uint32_t flags) {
    ++state.movie_open_calls;
    state.movie_open_flags = flags;
    if (state.movie_open_calls == 1 &&
        GetEnvironmentVariableA("AITD4_TEST_BINK_FIRST_OPEN_REJECT", nullptr, 0))
        return nullptr;
    movie.width = 640;
    movie.height = 320;
    movie.frames = 2;
    movie.frame = 1;
    state.open_width = movie.width;
    state.open_height = movie.height;
    return &movie;
}

extern "C" __declspec(dllexport) const char* WINAPI BinkGetError() {
    return "Bink stub error";
}

extern "C" __declspec(dllexport) std::uint32_t WINAPI BinkDoFrame(void*) {
    return ++state.do_frame_calls;
}

extern "C" __declspec(dllexport) std::uint32_t WINAPI BinkCopyToBuffer(
    void*, void* destination, std::int32_t pitch, std::uint32_t height,
    std::uint32_t, std::uint32_t, std::uint32_t) {
    if (destination && pitch != 0) {
        auto* top = static_cast<std::uint8_t*>(destination);
        for (std::uint32_t y = 0; y < height; ++y) {
            auto* row = top + static_cast<std::ptrdiff_t>(y) * pitch;
            for (std::uint32_t x = 0; x < state.open_width; ++x) {
                row[x * 3 + 0] = static_cast<std::uint8_t>((x + y) & 0xff);
                row[x * 3 + 1] = 96;
                row[x * 3 + 2] = 160;
            }
        }
    }
    return ++state.copy_calls;
}

extern "C" __declspec(dllexport) void WINAPI BinkBufferBlit(
    void*, void*, std::uint32_t) {
    ++state.blit_calls;
}

extern "C" __declspec(dllexport) int WINAPI BinkBufferLock(void*) {
    ++state.lock_calls;
    return 1;
}

extern "C" __declspec(dllexport) void WINAPI BinkBufferUnlock(void*) {
    ++state.unlock_calls;
}

extern "C" __declspec(dllexport) void WINAPI BinkNextFrame(void*) {
    ++state.next_frame_calls;
    ++movie.frame;
}

extern "C" __declspec(dllexport) void WINAPI BinkClose(void*) {
    ++state.close_calls;
}

extern "C" __declspec(dllexport) void* WINAPI BinkBufferOpen(
    HWND, std::uint32_t width, std::uint32_t height, std::uint32_t) {
    state.open_width = width;
    state.open_height = height;
    buffer.width = width;
    buffer.height = height;
    buffer.surface = 2;
    buffer.pixels = pixels;
    buffer.pitch = static_cast<std::int32_t>(width * 3);
    return &buffer;
}

extern "C" __declspec(dllexport) int WINAPI BinkBufferSetOffset(void*, int x, int y) {
    state.offset_x = x;
    state.offset_y = y;
    return 1;
}

extern "C" __declspec(dllexport) int WINAPI BinkBufferSetScale(
    void*, std::uint32_t width, std::uint32_t height) {
    if (GetEnvironmentVariableA("AITD4_TEST_BINK_SCALE_REJECT", nullptr, 0)) return 0;
    state.scale_width = width;
    state.scale_height = height;
    return 1;
}

extern "C" __declspec(dllexport) std::uint32_t WINAPI BinkGetRects(
    void*, std::uint32_t) {
    ++state.get_rects_calls;
    return 1;
}

extern "C" __declspec(dllexport) void WINAPI BinkStubGetState(BinkStubState* output) {
    if (output) *output = state;
}
