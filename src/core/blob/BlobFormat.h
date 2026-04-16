#pragma once

#include <cstdint>

namespace dbc {

static constexpr uint32_t BDBC_MAGIC          = 0x42444243u;  // "BDBC"
static constexpr uint32_t BDBC_VERSION        = 1u;
static constexpr uint32_t BDBC_INDEX_ENTRY_SIZE = 44u;

// On-disk index entry layout (44 bytes)
//   0   4   name_hash  FNV1a of lowercase table name
//   4  32   name       mixed-case, null-terminated (max 31 chars + NUL)
//  36   4   offset     byte offset into data section
//  40   4   length     byte length of this table's DBD text
struct BdbcIndexEntry {
    uint32_t nameHash;
    char     name[32];
    uint32_t offset;
    uint32_t length;
};
static_assert(sizeof(BdbcIndexEntry) == BDBC_INDEX_ENTRY_SIZE,
              "BdbcIndexEntry must be 44 bytes");

}  // namespace dbc
