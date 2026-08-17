#include "../src/viewport.hpp"

#include <cstdio>

int main() {
    struct Test { int width, height, x, y, view_width, view_height; };
    const Test tests[] = {
        {1920, 1080, 240, 0, 1440, 1080},
        {2560, 1440, 320, 0, 1920, 1440},
        {3440, 1440, 760, 0, 1920, 1440},
        {1600, 1200, 0, 0, 1600, 1200},
        {1280, 1024, 0, 32, 1280, 960},
    };
    for (const auto& test : tests) {
        const auto actual = aitd4::proportional_4x3_viewport(test.width, test.height);
        if (actual.x != test.x || actual.y != test.y || actual.width != test.view_width ||
            actual.height != test.view_height) {
            std::fprintf(stderr, "%dx%d -> %d,%d %dx%d (expected %d,%d %dx%d)\n",
                         test.width, test.height, actual.x, actual.y, actual.width, actual.height,
                         test.x, test.y, test.view_width, test.view_height);
            return 1;
        }
    }
    std::puts("viewport tests passed");
    return 0;
}
