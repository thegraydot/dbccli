#include "JsonSchemaExporter.h"

#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace dbc {

static const char* const kSchemaLocaleNames[17] = {
    "enUS", "enGB", "deDE", "frFR", "koKR", "zhCN", "zhTW", "esES",
    "esMX", "ruRU", "jaJP", "ptBR", "itIT",
    "unknown_13", "unknown_14", "unknown_15", "flags"
};

bool JsonSchemaExporter::Export(const VersionDefinition& ver,
                                 const std::string& tableName,
                                 const std::string& outputPath,
                                 uint32_t locStringEntries) {
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

    // Collect required field names (all inline fields are always present)
    std::vector<std::string> required;
    for (const auto& f : ver.fields) {
        if (!f.isNonInlineId) required.push_back(f.name);
    }

    *out << "{\n";
    *out << "  \"$schema\": \"https://json-schema.org/draft/2020-12/schema\",\n";
    *out << "  \"title\": \"" << tableName << "\",\n";
    *out << "  \"type\": \"array\",\n";
    *out << "  \"items\": {\n";
    *out << "    \"type\": \"object\",\n";
    *out << "    \"properties\": {\n";

    bool firstField = true;
    for (const auto& f : ver.fields) {
        if (f.isNonInlineId) continue;
        if (!firstField) *out << ",\n";
        firstField = false;

        *out << "      \"" << f.name << "\": ";

        if (f.isLocString) {
            if (locStringEntries == 0) {
                *out << "{ \"type\": \"object\" }";
            } else {
                *out << "{\n";
                *out << "        \"type\": \"object\",\n";
                *out << "        \"properties\": {\n";
                for (uint32_t loc = 0; loc < locStringEntries; ++loc) {
                    if (loc > 0) *out << ",\n";
                    const char* label = (loc < 17) ? kSchemaLocaleNames[loc] : "locale_?";
                    // Last entry is always the flags bitmask integer
                    if (loc == locStringEntries - 1) {
                        *out << "          \"" << label << "\": { \"type\": \"integer\" }";
                    } else {
                        *out << "          \"" << label << "\": { \"type\": \"string\" }";
                    }
                }
                *out << "\n        }\n      }";
            }
        } else if (f.arrayCount > 1) {
            const char* itemType = f.isFloat ? "number" : (f.isString ? "string" : "integer");
            *out << "{\n";
            *out << "        \"type\": \"array\",\n";
            *out << "        \"items\": { \"type\": \"" << itemType << "\" },\n";
            *out << "        \"minItems\": " << f.arrayCount << ",\n";
            *out << "        \"maxItems\": " << f.arrayCount << "\n";
            *out << "      }";
        } else {
            const char* t = f.isFloat ? "number" : (f.isString ? "string" : "integer");
            *out << "{ \"type\": \"" << t << "\" }";
        }
    }

    *out << "\n    },\n";

    *out << "    \"required\": [";
    for (size_t i = 0; i < required.size(); ++i) {
        if (i > 0) *out << ", ";
        *out << "\"" << required[i] << "\"";
    }
    *out << "]\n";

    *out << "  }\n";
    *out << "}\n";

    return true;
}

}  // namespace dbc
