#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdint>

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
    std::uint32_t next_frame_calls{};
    std::uint32_t close_calls{};
};

namespace {
BinkStubState state;
}

extern "C" __declspec(dllexport) void* WINAPI BinkOpen(
    const char*, std::uint32_t flags) {
    ++state.movie_open_calls;
    state.movie_open_flags = flags;
    if (state.movie_open_calls == 1 &&
        GetEnvironmentVariableA("AITD4_TEST_BINK_FIRST_OPEN_REJECT", nullptr, 0))
        return nullptr;
    return &state;
}

extern "C" __declspec(dllexport) const char* WINAPI BinkGetError() {
    return "Bink stub error";
}

extern "C" __declspec(dllexport) std::uint32_t WINAPI BinkDoFrame(void*) {
    return ++state.do_frame_calls;
}

extern "C" __declspec(dllexport) std::uint32_t WINAPI BinkCopyToBuffer(
    void*, void*, std::int32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t) {
    return ++state.copy_calls;
}

extern "C" __declspec(dllexport) void WINAPI BinkNextFrame(void*) {
    ++state.next_frame_calls;
}

extern "C" __declspec(dllexport) void WINAPI BinkClose(void*) {
    ++state.close_calls;
}

extern "C" __declspec(dllexport) void* WINAPI BinkBufferOpen(
    HWND, std::uint32_t width, std::uint32_t height, std::uint32_t) {
    state.open_width = width;
    state.open_height = height;
    return &state;
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

extern "C" __declspec(dllexport) void WINAPI BinkStubGetState(BinkStubState* output) {
    if (output) *output = state;
}
