#pragma once

#include <cstdint>

namespace aitd4 {

bool renderer_start(const char* asset_root, const char* log_path);
bool renderer_set_scene(int player, const char* container, int bank_slot);
void renderer_stop_player(int player);
void renderer_event(int player, const char* container, int local_program, const std::uint8_t* message);
bool renderer_ready();

}
