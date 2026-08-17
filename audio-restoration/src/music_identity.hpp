#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace aitd4::music_identity {

enum class Evidence {
    none,
    map_unique,
    sequence_unique,
    file_load,
    ambiguous,
};

struct Candidate {
    std::string_view container;
    bool sequence_match{};
    std::uint64_t load_serial{};
};

struct Resolution {
    int index{-1};
    Evidence evidence{Evidence::none};
};

inline Resolution resolve(std::span<const Candidate> candidates) {
    if (candidates.empty()) return {};

    int sequence_index = -1;
    int sequence_count = 0;
    for (std::size_t index = 0; index != candidates.size(); ++index) {
        if (!candidates[index].sequence_match) continue;
        ++sequence_count;
        sequence_index = static_cast<int>(index);
    }
    if (sequence_count == 1) return {sequence_index, Evidence::sequence_unique};

    // File provenance is the tie-breaker for retail containers whose maps and
    // DSEQ payloads are genuinely identical. If any sequence matched, do not
    // let a load from an unrelated map-identical candidate override that set.
    std::uint64_t latest = 0;
    for (const auto& candidate : candidates) {
        if (sequence_count != 0 && !candidate.sequence_match) continue;
        latest = std::max(latest, candidate.load_serial);
    }
    if (latest != 0) {
        int latest_index = -1;
        for (std::size_t index = 0; index != candidates.size(); ++index) {
            if ((sequence_count != 0 && !candidates[index].sequence_match) ||
                candidates[index].load_serial != latest) continue;
            if (latest_index >= 0) return {-1, Evidence::ambiguous};
            latest_index = static_cast<int>(index);
        }
        return {latest_index, Evidence::file_load};
    }

    if (candidates.size() == 1) return {0, Evidence::map_unique};
    return {-1, Evidence::ambiguous};
}

inline std::optional<std::string> container_from_path(std::string_view path) {
    std::string folded(path);
    std::replace(folded.begin(), folded.end(), '/', '\\');
    std::transform(folded.begin(), folded.end(), folded.begin(), [](unsigned char value) {
        return static_cast<char>(std::tolower(value));
    });

    constexpr std::string_view roots[] = {"midi\\aline\\", "midi\\carnby\\"};
    for (const auto root : roots) {
        const auto at = folded.rfind(root);
        if (at == std::string::npos) continue;
        const auto name_at = at + root.size();
        if (name_at >= folded.size()) return std::nullopt;
        const auto name = std::string_view(folded).substr(name_at);
        if (name.find('\\') != std::string_view::npos ||
            name.find('.') != std::string_view::npos) return std::nullopt;
        for (const unsigned char value : name) {
            if (!(std::isalnum(value) || value == '_')) return std::nullopt;
        }
        return std::string(name);
    }
    return std::nullopt;
}

inline const char* evidence_name(Evidence evidence) {
    switch (evidence) {
    case Evidence::map_unique: return "map-unique";
    case Evidence::sequence_unique: return "sequence-unique";
    case Evidence::file_load: return "file-load";
    case Evidence::ambiguous: return "ambiguous";
    default: return "none";
    }
}

} // namespace aitd4::music_identity
