#include <cassert>
#include <cmath>
#include <cstdint>

#include "../src/rumble_protocol.hpp"
#include "../src/executable_profile.hpp"

int main() {
    using namespace aitdtnn::rumble;

    assert(request_to_vibset(0, 0) == kStopVibset);
    assert(request_to_vibset(1, 0) == kStopVibset);
    assert(request_to_vibset(0, 1) == kStrongVibset);
    assert(request_to_vibset(0, 0xffffffffu) == kStrongVibset);
    assert(request_to_vibset(1, 1) == kWeakVibset);
    assert(request_to_vibset(9, 0x80) == kWeakVibset);

    const auto stopped = decode_vibset(kStopVibset);
    assert(stopped.raw_vibset == kStopVibset);
    assert(stopped.power == 0.0f);
    assert(stopped.inclination == 0.0f);
    assert(stopped.duration_milliseconds == kDreamcastAutoStopMilliseconds);

    const auto weak = decode_vibset(kWeakVibset);
    assert(std::fabs(weak.power - (2.0f / 7.0f)) < 0.00001f);
    assert(weak.inclination == 0.0f);
    assert(weak.duration_milliseconds == kDreamcastAutoStopMilliseconds);

    const auto strong = decode_vibset(kStrongVibset);
    assert(strong.power == 1.0f);
    assert(strong.inclination == 0.0f);
    assert(strong.duration_milliseconds == kDreamcastAutoStopMilliseconds);
    assert(to_xinput_motor(stopped.power, 1.0f) == 0u);
    assert(to_xinput_motor(weak.power, 1.0f) == 18724u);
    assert(to_xinput_motor(strong.power, 1.0f) == 65535u);
    assert(to_xinput_motor(strong.power, 0.5f) == 32767u);

    // Dreamcast SetCondition commands replace the previous command.
    std::uint32_t current = request_to_vibset(0, 1);
    current = request_to_vibset(1, 0x80);
    assert(current == kWeakVibset);
    current = request_to_vibset(1, 0);
    assert(current == kStopVibset);

    static_assert(kEnableRva == 0x000a436cu);
    static_assert(kBackendRva == 0x000a4379u);
    static_assert(kAvailableRva == 0x000a439fu);
    static_assert(std::size(kExecutableProfiles) == 3);
    static_assert(kExecutableProfiles[0].enable_rva == kEnableRva);
    static_assert(kExecutableProfiles[1].enable_rva == 0x000a420cu);
    static_assert(kExecutableProfiles[1].backend_rva == 0x000a4219u);
    static_assert(kExecutableProfiles[1].available_rva == 0x000a423fu);
    // Steam is the retail CD image with the CD check patched: same addresses.
    static_assert(kExecutableProfiles[2].enable_rva == kExecutableProfiles[1].enable_rva);
    static_assert(kExecutableProfiles[2].backend_rva == kExecutableProfiles[1].backend_rva);
    static_assert(kExecutableProfiles[2].available_rva == kExecutableProfiles[1].available_rva);
    static_assert(kExecutableProfiles[2].sha256 == kSupportedSteamExeSha256);
    // The Steam CD-check patch sites must stay clear of the retail rumble code.
    constexpr CodeInterval steam_patches[] = {{0x0008d722u, 2}, {0x0008db28u, 1}};
    constexpr CodeInterval retail_rumble[] = {
        {0x000a420cu, 13}, {0x000a4219u, 15}, {0x000a423fu, 13}};
    for (const auto patch : steam_patches) {
        for (const auto rumble : retail_rumble) {
            assert(!intervals_overlap(patch, rumble));
        }
    }
    static_assert(sizeof(kExpectedEnable) == 13);
    static_assert(sizeof(kExpectedBackend) == 15);
    static_assert(sizeof(kExpectedAvailable) == 13);
    static_assert(kExpectedBackend[0] == 0x55 && kExpectedBackend[12] == 0xc2 &&
                  kExpectedBackend[13] == 0x08 && kExpectedBackend[14] == 0x00);
    for (const auto rumble : kRumbleCodeIntervals) {
        for (const auto claimed : kClaimedCodeIntervals) {
            assert(!intervals_overlap(rumble, claimed));
        }
    }
    return 0;
}
