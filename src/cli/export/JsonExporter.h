#pragma once

#include "core/reader/WdbcReader.h"

#include <string>

namespace dbc {

class JsonExporter {
public:
    // Export all records from reader to outputPath as a JSON array of objects.
    // Pass "-" as outputPath to write to stdout.
    // All log/error messages go to stderr.
    bool Export(WdbcReader& reader,
                const std::string& tableName,
                const std::string& outputPath);
};

}  // namespace dbc
