#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace dbc {

struct BlobIndexEntry {
    uint32_t    nameHash;
    std::string name;    // mixed case
    uint32_t    offset;
    uint32_t    length;
};

class BlobReader {
public:
    // Initialise from the embedded blob (call once at startup)
    void Load(const uint8_t* data, uint32_t size);

    // Find a table by name (case-insensitive)
    // Returns string_view into blob data section, or empty if not found
    std::optional<std::string_view> FindTable(const std::string& tableName) const;

    // List all table names (for --list-tables)
    const std::vector<std::string>& TableNames() const { return _sortedNames; }

    bool IsLoaded() const { return _data != nullptr; }

private:
    const uint8_t*                               _data       = nullptr;
    uint32_t                                     _dataSize   = 0;
    uint32_t                                     _dataOffset = 0;
    std::unordered_map<uint32_t, BlobIndexEntry> _index;
    std::vector<std::string>                     _sortedNames;
};

// FNV1a hash - always called on lowercase table name
constexpr uint32_t Fnv1a(const char* str) noexcept {
    uint32_t hash = 2166136261u;
    while (*str) {
        hash ^= static_cast<uint8_t>(*str++);
        hash *= 16777619u;
    }
    return hash;
}

inline uint32_t Fnv1a(const std::string& s) noexcept {
    return Fnv1a(s.c_str());
}

}  // namespace dbc
