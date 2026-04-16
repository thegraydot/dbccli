#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace dbc {

struct ColumnDefinition {
    std::string type;           // "int", "float", "string", "locstring"
    std::string foreignTable;
    std::string foreignColumn;
    bool        hasForeignKey = false;
    bool        verified      = true;  // false if column name ends with '?'
    std::string comment;
};

struct FieldDefinition {
    std::string name;
    int         sizeBits    = 32;
    bool        isSigned    = true;
    bool        isFloat     = false;
    bool        isString    = false;
    bool        isLocString = false;
    int         arrayCount  = 1;
    bool        isId           = false;
    bool        isNonInlineId  = false;
    bool        isRelation     = false;
    std::string comment;
};

struct VersionDefinition {
    std::vector<uint32_t>                      builds;
    std::vector<std::pair<uint32_t, uint32_t>> buildRanges;  // (min, max) inclusive
    std::vector<std::string>                   layoutHashes;
    std::vector<FieldDefinition>               fields;
};

struct TableDefinition {
    std::string                            name;
    std::map<std::string, ColumnDefinition> columns;
    std::vector<VersionDefinition>          versions;
};

}  // namespace dbc
