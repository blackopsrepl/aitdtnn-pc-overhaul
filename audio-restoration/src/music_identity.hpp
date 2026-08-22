#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace aitd4::music_identity {

enum class Evidence {
    none,
    loader_identity,
    identity_missing,
    validation_failed,
};

struct LoadedIdentity {
    const void* sequence_base{};
    const void* sequence_table{};
    std::string container;
    std::uint64_t serial{};
};

struct LoadedBankIdentity {
    int bank_id{-1};
    std::string container;
    std::uint64_t serial{};
};

class LoadedIdentityRegistry {
public:
    std::uint64_t record(const void* sequence_base, const void* sequence_table,
                         std::string_view container) {
        const auto serial = ++serial_;
        entries_.push_back({sequence_base, sequence_table, std::string(container), serial});
        if (entries_.size() > 256) entries_.erase(entries_.begin());
        return serial;
    }

    std::string find(const void* sequence_base, const void* sequence_table) const {
        std::string result;
        std::uint64_t latest = 0;
        for (const auto& entry : entries_) {
            if (entry.sequence_base != sequence_base ||
                entry.sequence_table != sequence_table || entry.serial < latest) continue;
            latest = entry.serial;
            result = entry.container;
        }
        return result;
    }

private:
    std::vector<LoadedIdentity> entries_;
    std::uint64_t serial_{};
};

class LoadedBankRegistry {
public:
    std::uint64_t record(int bank_id, std::string_view container) {
        const auto serial = ++serial_;
        entries_.push_back({bank_id, std::string(container), serial});
        if (entries_.size() > 256) entries_.erase(entries_.begin());
        return serial;
    }

    std::string find(int bank_id) const {
        std::string result;
        std::uint64_t latest = 0;
        for (const auto& entry : entries_) {
            if (entry.bank_id != bank_id || entry.serial < latest) continue;
            latest = entry.serial;
            result = entry.container;
        }
        return result;
    }

    std::uint64_t serial(int bank_id) const {
        std::uint64_t latest = 0;
        for (const auto& entry : entries_)
            if (entry.bank_id == bank_id) latest = std::max(latest, entry.serial);
        return latest;
    }

private:
    std::vector<LoadedBankIdentity> entries_;
    std::uint64_t serial_{};
};

inline std::uint16_t read_be16(const std::uint8_t* bytes) {
    return static_cast<std::uint16_t>((static_cast<std::uint16_t>(bytes[0]) << 8) |
                                      static_cast<std::uint16_t>(bytes[1]));
}

inline std::uint32_t read_be32(const std::uint8_t* bytes) {
    return (static_cast<std::uint32_t>(bytes[0]) << 24) |
           (static_cast<std::uint32_t>(bytes[1]) << 16) |
           (static_cast<std::uint32_t>(bytes[2]) << 8) |
           static_cast<std::uint32_t>(bytes[3]);
}

inline bool loaded_dseq_matches(std::span<const std::uint8_t> asset,
                                std::span<const std::uint8_t> loaded) {
    constexpr std::size_t header_size = 0x10;
    if (asset.size() < header_size || loaded.size() < asset.size() ||
        !std::equal(asset.begin(), asset.begin() + header_size, loaded.begin()))
        return false;

    const auto sequence_count = read_be16(asset.data() + 0x0E);
    const auto table_end = header_size + static_cast<std::size_t>(sequence_count) * 4;
    if (sequence_count == 0 || table_end > asset.size()) return false;

    // The PC parser converts the DSEQ offset table from big endian to native
    // little endian in place. All other bytes remain identical to the source.
    for (std::size_t at = header_size; at != table_end; at += 4) {
        std::uint32_t loaded_offset{};
        std::memcpy(&loaded_offset, loaded.data() + at, sizeof(loaded_offset));
        if (loaded_offset != read_be32(asset.data() + at)) return false;
    }
    return std::equal(asset.begin() + table_end, asset.end(), loaded.begin() + table_end);
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
    case Evidence::loader_identity: return "loader-identity";
    case Evidence::identity_missing: return "identity-missing";
    case Evidence::validation_failed: return "validation-failed";
    default: return "none";
    }
}

} // namespace aitd4::music_identity
