#pragma once

#include "core/reader/WdbcReader.h"

#include <string>

namespace dbc {

class CsvExporter {
public:
    // Export all records from reader to outputPath.
    // Pass "-" as outputPath to write to stdout.
    // All log/error messages go to stderr.
    bool Export(WdbcReader& reader,
                const std::string& tableName,
                const std::string& outputPath);

private:
    static std::string EscapeValue(const std::string& value);
    static void        WriteRow(std::ostream& out, const std::vector<std::string>& cols);
};

}  // namespace dbc
