#pragma once

#include "core/dbd/DbdStructures.h"

#include <cstdint>
#include <string>

namespace dbc {

class CsvSchemaExporter {
public:
    // Emit a two-column CSV schema (field_name,type_descriptor) for ver.
    // locStringEntries: number of locale entries per locstring field (0 = unknown).
    // Pass "-" as outputPath to write to stdout.
    bool Export(const VersionDefinition& ver,
                const std::string& tableName,
                const std::string& outputPath,
                uint32_t locStringEntries = 0);
};

}  // namespace dbc
