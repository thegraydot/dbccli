#pragma once

#include "../dbd/DbdStructures.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace dbc {

// Locale labels for locstring expansion (builds ≤ 12340)
static const char* const LOCSTRING_LOCALES[17] = {
    "enUS", "enGB", "deDE", "frFR", "koKR", "zhCN", "zhTW", "esES",
    "esMX", "ruRU", "jaJP", "ptBR", "itIT",
    "unknown_13", "unknown_14", "unknown_15",
    "flags"
};

struct FieldValue {
    std::string              name;
    std::string              value;   // single-value fields
    std::vector<std::string> values;  // array / locstring fields
    bool                     isArray     = false;
    bool                     isLocString = false;
};

struct Record {
    uint32_t              id = 0;
    std::vector<FieldValue> fields;
};

class WdbcReader {
public:
    // Load from file path
    bool Open(const std::filesystem::path& path);

    // Load from buffer (stdin case)
    bool OpenBuffer(std::vector<uint8_t> buffer);

    // Set the version definition before reading
    void SetDefinition(const VersionDefinition& def);

    // Validate header against definition.
    // Returns true if valid, false with error message if not.
    bool Validate(std::string& errorMessage) const;

    uint32_t RecordCount() const { return _recordCount; }
    uint32_t FieldCount()  const { return _fieldCount;  }

    const VersionDefinition* Definition()    const { return _def; }
    uint32_t                 LocStringEntries() const { return _locStringEntries; }

    // Field names in layout order (for CSV header)
    std::vector<std::string> FieldNames() const;

    // Iterate all records
    std::optional<Record> NextRecord();
    void Reset();

private:
    bool LoadBuffer();

    std::vector<uint8_t> _buffer;

    // Parsed header
    uint32_t _recordCount  = 0;
    uint32_t _fieldCount   = 0;
    uint32_t _recordSize   = 0;
    uint32_t _stringBlockSize = 0;

    uint32_t _recordsOffset    = 0;  // offset into _buffer where records start
    uint32_t _stringBlockOffset = 0; // offset into _buffer where string block starts

    const VersionDefinition* _def = nullptr;

    uint32_t _cursor = 0;  // record index for NextRecord

    std::string ResolveString(uint32_t offset) const;

    uint32_t ExpectedRecordSize() const;

    // Number of 4-byte entries per locstring field.
    // Vanilla pre-2.0: 9 (8 locale strings + 1 flags word)
    // TBC+:           17 (16 locale strings + 1 flags word)
    // Auto-detected in Validate() from the file's fieldCount header.
    mutable uint32_t _locStringEntries = 17;
};

}  // namespace dbc
