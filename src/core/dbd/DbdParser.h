#pragma once

#include "DbdStructures.h"

#include <filesystem>
#include <map>
#include <string>
#include <string_view>

namespace dbc {

class DbdParser {
public:
    // Filesystem mode: read from a .dbd file path
    TableDefinition ParseFile(const std::filesystem::path& path);

    // Buffer mode: parse from a string_view (into blob data section)
    TableDefinition ParseBuffer(std::string_view buffer, const std::string& tableName);

private:
    TableDefinition Parse(std::istream& stream, const std::string& tableName);

    ColumnDefinition ParseColumnLine(const std::string& line);

    FieldDefinition ParseFieldLine(const std::string& line,
                                   const std::map<std::string, ColumnDefinition>& columns);

    void ParseBuildLine(const std::string& line,
                        std::vector<uint32_t>& builds,
                        std::vector<std::pair<uint32_t, uint32_t>>& ranges);
};

}  // namespace dbc
