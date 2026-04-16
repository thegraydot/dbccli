#include "CsvExporter.h"

#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace dbc {

// RFC 4180 value escaping

std::string CsvExporter::EscapeValue(const std::string& value) {
    bool needsQuote = value.find_first_of(",\"\r\n") != std::string::npos;
    if (!needsQuote) return value;

    std::string out;
    out.reserve(value.size() + 2);
    out += '"';
    for (char c : value) {
        if (c == '"') out += '"';  // "" escaping
        out += c;
    }
    out += '"';
    return out;
}


void CsvExporter::WriteRow(std::ostream& out,
                            const std::vector<std::string>& cols) {
    for (size_t i = 0; i < cols.size(); ++i) {
        if (i > 0) out << ',';
        out << EscapeValue(cols[i]);
    }
    out << "\r\n";
}


bool CsvExporter::Export(WdbcReader& reader,
                          const std::string& /*tableName*/,
                          const std::string& outputPath) {
    std::ostream* out    = nullptr;
    std::ofstream fileOut;

    if (outputPath == "-" || outputPath.empty()) {
        out = &std::cout;
    } else {
        fileOut.open(outputPath, std::ios::out | std::ios::trunc);
        if (!fileOut.is_open()) {
            std::cerr << "Error: cannot open output file: " << outputPath << "\n";
            return false;
        }
        out = &fileOut;
    }

    // Header row - expand locstring / array columns
    std::vector<std::string> headers = reader.FieldNames();
    WriteRow(*out, headers);

    // Data rows
    reader.Reset();
    while (auto rec = reader.NextRecord()) {
        std::vector<std::string> row;
        row.reserve(headers.size());

        for (const auto& fv : rec->fields) {
            if (fv.isLocString || fv.isArray) {
                for (const auto& v : fv.values) {
                    row.push_back(v);
                }
            } else {
                row.push_back(fv.value);
            }
        }

        WriteRow(*out, row);
    }

    return true;
}

}  // namespace dbc
