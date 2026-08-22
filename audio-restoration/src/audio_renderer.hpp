#pragma once

// Lifetime-safe C++ boundary around the Dreamcast emulator: initialize once,
// replace banks as scenes change, dispatch game events, then render PCM blocks.

#include <cstdint>

namespace aitd4 {

bool renderer_start(const char* asset_root, const char* log_path);
bool renderer_render_pcm(std::int16_t* output, unsigned frames);
bool renderer_set_scene(int player, const char* container, int bank_slot);
void renderer_stop_player(int player);
void renderer_event(int player, const char* container, int local_program, const std::uint8_t* message);
bool renderer_ready();

}
