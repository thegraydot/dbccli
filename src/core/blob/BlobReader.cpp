#include "BlobReader.h"
#include "BlobFormat.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace dbc {

void BlobReader::Load(const uint8_t* data, uint32_t size) {
    if (size < 12) {
        throw std::runtime_error("BlobReader: blob too small to contain header");
    }

    uint32_t magic, version, tableCount;
    std::memcpy(&magic,      data + 0, 4);
    std::memcpy(&version,    data + 4, 4);
    std::memcpy(&tableCount, data + 8, 4);

    if (magic != BDBC_MAGIC) {
        throw std::runtime_error("BlobReader: invalid BDBC magic");
    }
    if (version != BDBC_VERSION) {
        throw std::runtime_error("BlobReader: unsupported BDBC version");
    }

    uint32_t headerSize = 12u + tableCount * BDBC_INDEX_ENTRY_SIZE;
    if (size < headerSize) {
        throw std::runtime_error("BlobReader: blob too small for declared index");
    }

    _index.reserve(tableCount);
    _sortedNames.reserve(tableCount);

    for (uint32_t i = 0; i < tableCount; ++i) {
        const uint8_t* entry = data + 12 + i * BDBC_INDEX_ENTRY_SIZE;
        BlobIndexEntry e;
        std::memcpy(&e.nameHash, entry + 0,  4);
        char nameBuf[32]{};
        std::memcpy(nameBuf,    entry + 4, 32);
        e.name = nameBuf;
        std::memcpy(&e.offset,  entry + 36, 4);
        std::memcpy(&e.length,  entry + 40, 4);

        _index[e.nameHash] = e;
        _sortedNames.push_back(e.name);
    }

    std::sort(_sortedNames.begin(), _sortedNames.end());

    _data       = data;
    _dataSize   = size;
    _dataOffset = headerSize;
}

std::optional<std::string_view> BlobReader::FindTable(const std::string& tableName) const {
    if (!IsLoaded()) return std::nullopt;

    // Compute hash of lowercase name
    std::string lower = tableName;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    uint32_t hash = Fnv1a(lower);

    auto it = _index.find(hash);
    if (it == _index.end()) return std::nullopt;

    const BlobIndexEntry& e = it->second;
    // offset is relative to the data section start
    uint32_t absOffset = _dataOffset + e.offset;
    if (absOffset + e.length > _dataSize) return std::nullopt;

    return std::string_view(reinterpret_cast<const char*>(_data + absOffset), e.length);
}

}  // namespace dbc
