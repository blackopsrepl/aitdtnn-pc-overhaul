#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <array>
#include <atomic>
#include <cstdint>

namespace aitd4 {

using MilesDriver = void*;
using MilesSample = void*;
using MilesSampleCallback = void (WINAPI*)(MilesSample);

struct MilesApi {
    void* (WINAPI* mem_alloc_lock)(std::uint32_t){};
    void (WINAPI* mem_free_lock)(void*){};
    MilesSample (WINAPI* allocate_sample)(MilesDriver){};
    void (WINAPI* release_sample)(MilesSample){};
    void (WINAPI* init_sample)(MilesSample){};
    void (WINAPI* set_sample_type)(MilesSample, std::int32_t, std::uint32_t){};
    void (WINAPI* set_sample_rate)(MilesSample, std::int32_t){};
    void (WINAPI* set_sample_volume)(MilesSample, std::int32_t){};
    std::int32_t (WINAPI* sample_buffer_ready)(MilesSample){};
    void (WINAPI* load_sample_buffer)(MilesSample, std::uint32_t, const void*, std::uint32_t){};
    MilesSampleCallback (WINAPI* register_eob)(MilesSample, MilesSampleCallback){};
    void (WINAPI* start_sample)(MilesSample){};
    void (WINAPI* end_sample)(MilesSample){};

    bool complete() const;
};

using RenderPcmFn = bool (*)(std::int16_t*, unsigned);
using StreamLogFn = void (*)(const char*);

class MilesStream {
public:
    static constexpr unsigned frames_per_buffer = 2048;
    static constexpr unsigned channels = 2;
    static constexpr unsigned buffer_count = 2;
    static constexpr std::int32_t stereo_16_format = 3;

    MilesStream(MilesApi api, RenderPcmFn render, StreamLogFn log);
    bool start(MilesDriver driver);
    void service();
    bool running() const { return running_.load(); }
    std::uint64_t rendered_frames() const { return rendered_frames_.load(); }
    std::uint32_t peak() const { return peak_.load(); }

private:
    static void WINAPI eob_callback(MilesSample sample);
    bool fill(unsigned index);

    MilesApi api_{};
    RenderPcmFn render_{};
    StreamLogFn log_{};
    MilesSample sample_{};
    std::array<std::int16_t*, buffer_count> buffers_{};
    std::atomic_bool running_{};
    std::atomic_flag servicing_ = ATOMIC_FLAG_INIT;
    std::atomic<std::uint64_t> rendered_frames_{};
    std::atomic<std::uint32_t> peak_{};
    std::atomic_bool logged_nonzero_{};
};

bool resolve_miles_api(HMODULE module, MilesApi& api);

} // namespace aitd4
