// Bridge between Dreamcast PCM and the game's Miles Sound System. Miles owns
// the real device and mixer; this adapter keeps one double-buffered sample fed
// by AudioRenderer, preserving native effects and movie audio in the same mix.
#include "miles_stream.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace aitd4 {
namespace {

MilesStream* g_active_stream{};

template <typename T>
bool resolve(HMODULE module, const char* name, T& target) {
    target = reinterpret_cast<T>(GetProcAddress(module, name));
    return target != nullptr;
}

} // namespace

bool MilesApi::complete() const {
    return mem_alloc_lock && mem_free_lock && allocate_sample && release_sample && init_sample && set_sample_type &&
           set_sample_rate && set_sample_volume && sample_buffer_ready &&
           load_sample_buffer && register_eob && start_sample && end_sample;
}

MilesStream::MilesStream(MilesApi api, RenderPcmFn render, StreamLogFn log)
    : api_(api), render_(render), log_(log) {}

bool MilesStream::fill(unsigned index) {
    if (index >= buffer_count || !render_) return false;
    auto* buffer = buffers_[index];
    if (!buffer || !render_(buffer, frames_per_buffer)) return false;
    std::uint32_t local_peak = 0;
    for (unsigned offset = 0; offset != frames_per_buffer * channels; ++offset) {
        const auto value = buffer[offset];
        const auto magnitude = value == INT16_MIN ? 32768u :
            static_cast<std::uint32_t>(std::abs(static_cast<int>(value)));
        local_peak = (std::max)(local_peak, magnitude);
    }
    auto observed = peak_.load();
    while (observed < local_peak && !peak_.compare_exchange_weak(observed, local_peak)) {}
    api_.load_sample_buffer(sample_, index, buffer,
                            frames_per_buffer * channels * sizeof(std::int16_t));
    rendered_frames_ += frames_per_buffer;
    if (local_peak && !logged_nonzero_.exchange(true) && log_) {
        char message[160]{};
        std::snprintf(message, sizeof(message),
                      "AICA PCM nonzero: rendered_frames=%llu peak=%u",
                      static_cast<unsigned long long>(rendered_frames_.load()), local_peak);
        log_(message);
    }
    return true;
}

bool MilesStream::start(MilesDriver driver) {
    if (running_ || !driver || !api_.complete() || !render_ || g_active_stream) return false;
    sample_ = api_.allocate_sample(driver);
    if (!sample_) return false;
    for (auto& buffer : buffers_) {
        buffer = static_cast<std::int16_t*>(api_.mem_alloc_lock(
            frames_per_buffer * channels * sizeof(std::int16_t)));
        if (!buffer) {
            for (auto* allocated : buffers_) if (allocated) api_.mem_free_lock(allocated);
            buffers_.fill(nullptr);
            api_.release_sample(sample_);
            sample_ = nullptr;
            return false;
        }
    }
    api_.init_sample(sample_);
    api_.set_sample_type(sample_, stereo_16_format, 0);
    api_.set_sample_rate(sample_, 44100);
    api_.set_sample_volume(sample_, 127);
    g_active_stream = this;
    api_.register_eob(sample_, &MilesStream::eob_callback);
    if (!fill(0) || !fill(1)) {
        g_active_stream = nullptr;
        api_.end_sample(sample_);
        api_.release_sample(sample_);
        for (auto* buffer : buffers_) api_.mem_free_lock(buffer);
        buffers_.fill(nullptr);
        sample_ = nullptr;
        return false;
    }
    running_ = true;
    api_.start_sample(sample_);
    if (log_) log_("Miles AICA stream started: stereo16 44100Hz, two 2048-frame buffers");
    return true;
}

void MilesStream::service() {
    if (!running_ || servicing_.test_and_set()) return;
    for (;;) {
        const auto ready = api_.sample_buffer_ready(sample_);
        if (ready < 0) break;
        if (ready >= static_cast<std::int32_t>(buffer_count) || !fill(static_cast<unsigned>(ready))) {
            running_ = false;
            if (log_) log_("Miles AICA stream stopped: invalid buffer ownership or render failure");
            break;
        }
    }
    servicing_.clear();
}

void WINAPI MilesStream::eob_callback(MilesSample sample) {
    if (g_active_stream && g_active_stream->sample_ == sample) g_active_stream->service();
}

bool resolve_miles_api(HMODULE module, MilesApi& api) {
    if (!module) return false;
    return resolve(module, "_AIL_mem_alloc_lock@4", api.mem_alloc_lock) &&
           resolve(module, "_AIL_mem_free_lock@4", api.mem_free_lock) &&
           resolve(module, "_AIL_allocate_sample_handle@4", api.allocate_sample) &&
           resolve(module, "_AIL_release_sample_handle@4", api.release_sample) &&
           resolve(module, "_AIL_init_sample@4", api.init_sample) &&
           resolve(module, "_AIL_set_sample_type@12", api.set_sample_type) &&
           resolve(module, "_AIL_set_sample_playback_rate@8", api.set_sample_rate) &&
           resolve(module, "_AIL_set_sample_volume@8", api.set_sample_volume) &&
           resolve(module, "_AIL_sample_buffer_ready@4", api.sample_buffer_ready) &&
           resolve(module, "_AIL_load_sample_buffer@16", api.load_sample_buffer) &&
           resolve(module, "_AIL_register_EOB_callback@8", api.register_eob) &&
           resolve(module, "_AIL_start_sample@4", api.start_sample) &&
           resolve(module, "_AIL_end_sample@4", api.end_sample) && api.complete();
}

} // namespace aitd4
