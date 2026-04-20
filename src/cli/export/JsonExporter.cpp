#include "JsonExporter.h"

#include <cstdio>
#include <fstream>
#include <iostream>
#include <string>

namespace dbc {

static const char* const kJsonLocaleNames[17] = {
    "enUS", "enGB", "deDE", "frFR", "koKR", "zhCN", "zhTW", "esES",
    "esMX", "ruRU", "jaJP", "ptBR", "itIT",
    "unknown_13", "unknown_14", "unknown_15", "flags"
};

static std::string EscapeJsonString(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 2);
    for (unsigned char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (c < 0x20) {
                    char buf[7];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += static_cast<char>(c);
                }
                break;
        }
    }
    return out;
}

bool JsonExporter::Export(WdbcReader& reader,
                           const std::string& /*tableName*/,
                           const std::string& outputPath) {
    std::ostream* out = nullptr;
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

    const VersionDefinition* def = reader.Definition();
    if (!def) {
        std::cerr << "Error: no definition set on reader\n";
        return false;
    }

    *out << "[\n";
    reader.Reset();
    bool firstRecord = true;
    while (auto rec = reader.NextRecord()) {
        if (!firstRecord) *out << ",\n";
        firstRecord = false;

        *out << "  {\n";

        bool firstField = true;
        size_t fvIdx = 0;
        for (const auto& fd : def->fields) {
            if (fvIdx >= rec->fields.size()) break;
            const FieldValue& fv = rec->fields[fvIdx++];

            if (!firstField) *out << ",\n";
            firstField = false;

            *out << "    \"" << EscapeJsonString(fv.name) << "\": ";

            if (fv.isLocString) {
                // Object keyed by locale name; last entry is flags integer
                *out << "{";
                for (size_t loc = 0; loc < fv.values.size(); ++loc) {
                    if (loc > 0) *out << ", ";
                    const char* label = (loc < 17) ? kJsonLocaleNames[loc] : "locale_?";
                    *out << "\"" << label << "\": ";
                    if (loc == fv.values.size() - 1) {
                        *out << fv.values[loc];  // flags - emit as integer
                    } else {
                        *out << "\"" << EscapeJsonString(fv.values[loc]) << "\"";
                    }
                }
                *out << "}";
            } else if (fv.isArray) {
                // JSON array; base type determines quoting
                *out << "[";
                for (size_t i = 0; i < fv.values.size(); ++i) {
                    if (i > 0) *out << ", ";
                    if (fd.isString) {
                        *out << "\"" << EscapeJsonString(fv.values[i]) << "\"";
                    } else {
                        *out << fv.values[i];
                    }
                }
                *out << "]";
            } else {
                // Single value
                if (fd.isString) {
                    *out << "\"" << EscapeJsonString(fv.value) << "\"";
                } else {
                    *out << fv.value;
                }
            }
        }

        *out << "\n  }";
    }

    *out << "\n]\n";
    return true;
}

}  // namespace dbc
