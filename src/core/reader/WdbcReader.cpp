#include "WdbcReader.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <stdexcept>

namespace dbc {

// File / buffer loading

bool WdbcReader::Open(const std::filesystem::path& path) {
    std::ifstream fs(path, std::ios::in | std::ios::binary);
    if (!fs.is_open()) return false;

    _buffer = std::vector<uint8_t>(
        std::istreambuf_iterator<char>(fs),
        std::istreambuf_iterator<char>());

    return LoadBuffer();
}

bool WdbcReader::OpenBuffer(std::vector<uint8_t> buffer) {
    _buffer = std::move(buffer);
    return LoadBuffer();
}

bool WdbcReader::LoadBuffer() {
    // WDBC header: 20 bytes
    // Magic(4) + RecordCount(4) + FieldCount(4) + RecordSize(4) + StringBlockSize(4)
    if (_buffer.size() < 20) return false;

    uint32_t magic;
    std::memcpy(&magic, _buffer.data(), 4);
    if (magic != 0x43424457u) return false;  // "WDBC"

    std::memcpy(&_recordCount,     _buffer.data() + 4,  4);
    std::memcpy(&_fieldCount,      _buffer.data() + 8,  4);
    std::memcpy(&_recordSize,      _buffer.data() + 12, 4);
    std::memcpy(&_stringBlockSize, _buffer.data() + 16, 4);

    _recordsOffset     = 20u;
    _stringBlockOffset = 20u + _recordCount * _recordSize;

    // Basic size sanity
    uint64_t required = static_cast<uint64_t>(_recordsOffset) +
                        static_cast<uint64_t>(_recordCount) * _recordSize +
                        _stringBlockSize;
    if (_buffer.size() < required) return false;

    _cursor = 0;
    return true;
}


void WdbcReader::SetDefinition(const VersionDefinition& def) {
    _def = &def;
}

// Validation

uint32_t WdbcReader::ExpectedRecordSize() const {
    if (!_def) return 0;
    uint32_t size = 0;
    for (const auto& f : _def->fields) {
        if (f.isNonInlineId) continue;  // not stored in record
        uint32_t fieldBytes;
        if (f.isLocString) {
            fieldBytes = _locStringEntries * 4;
        } else {
            fieldBytes = static_cast<uint32_t>(f.sizeBits / 8);
        }
        size += fieldBytes * static_cast<uint32_t>(f.arrayCount);
    }
    return size;
}

bool WdbcReader::Validate(std::string& errorMessage) const {
    if (!_def) {
        errorMessage = "No VersionDefinition set";
        return false;
    }

    // Auto-detect locstring entry count when all record slots are 4-byte wide.
    // In WDBC format: fieldCount * 4 == recordSize means every logical field is a
    // single 32-bit slot (the common case for vanilla through WotLK DBC files).
    // We solve:  scalarSlots + locStringFields * L = fieldCount  for L.
    if (_fieldCount > 0 && _fieldCount * 4 == _recordSize) {
        uint32_t scalarSlots  = 0;
        uint32_t locStrFields = 0;
        for (const auto& f : _def->fields) {
            if (f.isNonInlineId) continue;
            if (f.isLocString) {
                ++locStrFields;
            } else {
                // Each non-locstring field occupies (sizeBits/32) 4-byte slots
                // (32-bit fields → 1 slot, 64-bit → 2 slots, etc.)
                scalarSlots += static_cast<uint32_t>(f.arrayCount) *
                               static_cast<uint32_t>(f.sizeBits / 32);
            }
        }
        if (locStrFields > 0 && _fieldCount >= scalarSlots) {
            uint32_t remaining = _fieldCount - scalarSlots;
            if (remaining % locStrFields == 0) {
                uint32_t candidate = remaining / locStrFields;
                // Valid range: 8 (rare no-flags old format) through 17 (WotLK+)
                if (candidate >= 8 && candidate <= 17) {
                    _locStringEntries = candidate;
                }
            }
        }
    }

    uint32_t expected = ExpectedRecordSize();
    if (_recordSize != expected) {
        errorMessage = "Record size mismatch: file has " + std::to_string(_recordSize) +
                       " bytes/record, definition expects " + std::to_string(expected);
        return false;
    }
    return true;
}

// Field names

std::vector<std::string> WdbcReader::FieldNames() const {
    std::vector<std::string> names;
    if (!_def) return names;
    for (const auto& f : _def->fields) {
        if (f.isLocString) {
            const uint32_t n = _locStringEntries;
            for (uint32_t loc = 0; loc < n; ++loc) {
                // LOCSTRING_LOCALES has 17 entries; cap index to its length
                const char* label = (loc < 17) ? LOCSTRING_LOCALES[loc] :
                                    "locale_?";
                names.push_back(f.name + "_" + label);
            }
        } else if (f.arrayCount > 1) {
            for (int i = 0; i < f.arrayCount; ++i) {
                names.push_back(f.name + "_" + std::to_string(i));
            }
        } else {
            names.push_back(f.name);
        }
    }
    return names;
}

// Record iteration

void WdbcReader::Reset() { _cursor = 0; }

std::optional<Record> WdbcReader::NextRecord() {
    if (!_def || _cursor >= _recordCount) return std::nullopt;

    const uint8_t* recPtr = _buffer.data() + _recordsOffset + _cursor * _recordSize;
    uint32_t       pos    = 0;

    Record rec;
    rec.id = _cursor;  // may be overridden below when $id$ field is encountered

    for (const auto& f : _def->fields) {
        FieldValue fv;
        fv.name = f.name;

        if (f.isNonInlineId) {
            // Not present in record data; ID comes from cursor position
            rec.id = _cursor;
            fv.value = std::to_string(_cursor);
            rec.fields.push_back(std::move(fv));
            continue;
        }

        if (f.isLocString) {
            fv.isLocString = true;
            const uint32_t n = _locStringEntries;
            for (uint32_t loc = 0; loc < n; ++loc) {
                uint32_t raw;
                std::memcpy(&raw, recPtr + pos, 4);
                pos += 4;
                // Last entry is always the flags bitmask, not a string offset
                if (loc == n - 1) {
                    fv.values.push_back(std::to_string(raw));
                } else {
                    fv.values.push_back(ResolveString(raw));
                }
            }
            // Use enUS as the primary value
            if (!fv.values.empty()) fv.value = fv.values[0];
            rec.fields.push_back(std::move(fv));
            continue;
        }

        auto ReadField = [&]() -> std::string {
            if (f.isFloat) {
                float v;
                std::memcpy(&v, recPtr + pos, 4);
                pos += 4;
                // Use enough precision to round-trip
                char buf[64];
                std::snprintf(buf, sizeof(buf), "%.9g", static_cast<double>(v));
                return buf;
            }
            if (f.isString) {
                uint32_t offset;
                std::memcpy(&offset, recPtr + pos, 4);
                pos += 4;
                return ResolveString(offset);
            }
            // Integer - read at declared width
            switch (f.sizeBits) {
                case 8: {
                    if (f.isSigned) {
                        int8_t v; std::memcpy(&v, recPtr + pos, 1); pos += 1;
                        return std::to_string(v);
                    } else {
                        uint8_t v; std::memcpy(&v, recPtr + pos, 1); pos += 1;
                        return std::to_string(v);
                    }
                }
                case 16: {
                    if (f.isSigned) {
                        int16_t v; std::memcpy(&v, recPtr + pos, 2); pos += 2;
                        return std::to_string(v);
                    } else {
                        uint16_t v; std::memcpy(&v, recPtr + pos, 2); pos += 2;
                        return std::to_string(v);
                    }
                }
                default:
                case 32: {
                    if (f.isSigned) {
                        int32_t v; std::memcpy(&v, recPtr + pos, 4); pos += 4;
                        // Check if this is the $id$ field
                        if (f.isId) rec.id = static_cast<uint32_t>(v);
                        return std::to_string(v);
                    } else {
                        uint32_t v; std::memcpy(&v, recPtr + pos, 4); pos += 4;
                        if (f.isId) rec.id = v;
                        return std::to_string(v);
                    }
                }
                case 64: {
                    if (f.isSigned) {
                        int64_t v; std::memcpy(&v, recPtr + pos, 8); pos += 8;
                        return std::to_string(v);
                    } else {
                        uint64_t v; std::memcpy(&v, recPtr + pos, 8); pos += 8;
                        return std::to_string(v);
                    }
                }
            }
        };

        if (f.arrayCount > 1) {
            fv.isArray = true;
            for (int i = 0; i < f.arrayCount; ++i) {
                fv.values.push_back(ReadField());
            }
            if (!fv.values.empty()) fv.value = fv.values[0];
        } else {
            fv.value = ReadField();
        }

        rec.fields.push_back(std::move(fv));
    }

    ++_cursor;
    return rec;
}

// String block

std::string WdbcReader::ResolveString(uint32_t offset) const {
    if (_stringBlockSize == 0 || offset >= _stringBlockSize) return {};
    const char* base = reinterpret_cast<const char*>(
        _buffer.data() + _stringBlockOffset + offset);
    // Ensure NUL-terminated within bounds
    uint32_t maxLen = _stringBlockSize - offset;
    size_t   len    = strnlen(base, maxLen);
    return std::string(base, len);
}

}  // namespace dbc
