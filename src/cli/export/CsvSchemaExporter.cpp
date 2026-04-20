#include "CsvSchemaExporter.h"

#include <fstream>
#include <iostream>
#include <string>

namespace dbc {

static std::string TypeDescriptor(const FieldDefinition& f, uint32_t locEntries) {
    std::string type;
    if (f.isFloat) {
        type = "float32";
    } else if (f.isString) {
        type = "string";
    } else if (f.isLocString) {
        if (locEntries > 0) {
            type = "locstring:" + std::to_string(locEntries);
        } else {
            type = "locstring";
        }
    } else {
        type = (f.isSigned ? "int" : "uint") + std::to_string(f.sizeBits);
    }

    if (f.arrayCount > 1) {
        type += "[" + std::to_string(f.arrayCount) + "]";
    }
    if (f.isId) {
        type += ":id";
    }
    return type;
}

bool CsvSchemaExporter::Export(const VersionDefinition& ver,
                                const std::string& /*tableName*/,
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

    for (const auto& f : ver.fields) {
        if (f.isNonInlineId) continue;
        *out << f.name << "," << TypeDescriptor(f, locStringEntries) << "\r\n";
    }

    return true;
}

}  // namespace dbc
