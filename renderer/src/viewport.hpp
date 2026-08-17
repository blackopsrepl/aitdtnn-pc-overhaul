#pragma once

#include <algorithm>

namespace aitd4 {

struct Viewport {
    int x{};
    int y{};
    int width{};
    int height{};
};

inline Viewport proportional_4x3_viewport(int physical_width, int physical_height) {
    if (physical_width <= 0 || physical_height <= 0) return {};
    // A common integer scale preserves exact 4:3. Making it even also keeps
    // both dimensions even for multisample resolves and video uploads.
    int scale = std::min(physical_width / 4, physical_height / 3) & ~1;
    if (scale <= 0) return {};
    const int width = scale * 4;
    const int height = scale * 3;
    return {(physical_width - width) / 2, (physical_height - height) / 2, width, height};
}

}  // namespace aitd4
