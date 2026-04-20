#include "DbdParser.h"
#include "BuildVersion.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace dbc {

// Helpers

static std::string Trim(const std::string& s) {
    auto start = s.find_first_not_of(" \t\r\n");
    auto end   = s.find_last_not_of(" \t\r\n");
    if (start == std::string::npos) return {};
    return s.substr(start, end - start + 1);
}

static std::string ToLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return s;
}

// Public API

TableDefinition DbdParser::ParseFile(const std::filesystem::path& path) {
    std::ifstream fs(path, std::ios::in);
    if (!fs.is_open()) {
        throw std::runtime_error("DbdParser: cannot open file: " + path.string());
    }
    std::string tableName = path.stem().string();
    return Parse(fs, tableName);
}

TableDefinition DbdParser::ParseBuffer(std::string_view buffer,
                                        const std::string& tableName) {
    std::string str(buffer);
    std::istringstream ss(str);
    return Parse(ss, tableName);
}

// Core parser

enum class Section { None, Columns, Build };

TableDefinition DbdParser::Parse(std::istream& stream, const std::string& tableName) {
    TableDefinition table;
    table.name = tableName;

    Section           section = Section::None;
    VersionDefinition curVer;
    bool              inVersionBlock = false;

    auto FlushVersion = [&]() {
        if (inVersionBlock) {
            table.versions.push_back(std::move(curVer));
            curVer        = VersionDefinition{};
            inVersionBlock = false;
        }
    };

    std::string line;
    while (std::getline(stream, line)) {
        // Remove Windows CR
        if (!line.empty() && line.back() == '\r') line.pop_back();

        std::string trimmed = Trim(line);

        // Per the DBD spec: "Version structures are separated by an empty new line."
        // Flush the current version block on every blank line.
        if (trimmed.empty()) {
            if (inVersionBlock) FlushVersion();
            continue;
        }

        // Section headers
        if (trimmed == "COLUMNS") {
            FlushVersion();
            section       = Section::Columns;
            inVersionBlock = false;
            continue;
        }

        if (trimmed.rfind("LAYOUT", 0) == 0) {
            FlushVersion();
            section        = Section::Build;
            inVersionBlock = true;
            curVer         = VersionDefinition{};

            // Parse space-separated hashes after "LAYOUT"
            std::istringstream hss(trimmed.substr(6));
            std::string hash;
            while (hss >> hash) {
                if (hash.back() == ',') hash.pop_back();
                curVer.layoutHashes.push_back(hash);
            }
            continue;
        }

        if (trimmed.rfind("BUILD", 0) == 0) {
            // A BUILD line can appear in any version block (with or without LAYOUT).
            if (!inVersionBlock) {
                // Start a new build-only version block.
                section        = Section::Build;
                inVersionBlock = true;
                curVer         = VersionDefinition{};
            } else if (!curVer.fields.empty()) {
                // Fields already parsed into this block - missing blank-line separator.
                // Implicitly flush and start a new version block (4a: parser hardening).
                FlushVersion();
                section        = Section::Build;
                inVersionBlock = true;
                curVer         = VersionDefinition{};
            }
            ParseBuildLine(trimmed, curVer.builds, curVer.buildRanges);
            continue;
        }

        if (trimmed.rfind("COMMENT", 0) == 0) {
            // Table-level comment - skip
            continue;
        }

        // Section bodies
        if (section == Section::Columns) {
            if (trimmed.front() == '/') continue;  // comment line
            ColumnDefinition col = ParseColumnLine(trimmed);
            // The column name may include '?' - strip it for the key
            std::string key = col.comment.empty() ? "" : "";
            // We need the actual name from the line; ParseColumnLine returns it in its
            // comment temporarily. Re-derive the name.
            // Actually extract name from trimmed directly:
            {
                std::string tmp = trimmed;
                // Skip type token (e.g. "int", "string<32>", etc.)
                auto sp = tmp.find(' ');
                if (sp != std::string::npos) {
                    std::string namePart = Trim(tmp.substr(sp + 1));
                    // Strip foreign key annotation <table::col>
                    auto lt = namePart.find('<');
                    if (lt != std::string::npos) {
                        namePart = Trim(namePart.substr(0, lt));
                    }
                    // Strip comment //
                    auto cmt = namePart.find("//");
                    if (cmt != std::string::npos) namePart = Trim(namePart.substr(0, cmt));
                    // Strip trailing '?'
                    bool unverified = (!namePart.empty() && namePart.back() == '?');
                    if (unverified) {
                        namePart.pop_back();
                        col.verified = false;
                    }
                    key = namePart;
                }
            }
            if (!key.empty()) {
                table.columns[key] = col;
            }
            continue;
        }

        if (section == Section::Build && inVersionBlock) {
            if (trimmed.front() == '/') continue;  // comment line
            FieldDefinition field = ParseFieldLine(trimmed, table.columns);
            curVer.fields.push_back(std::move(field));
        }
    }

    FlushVersion();
    return table;
}

// ParseColumnLine
//
// Format examples:
//   int ID
//   string Name<lang>
//   locstring Name
//   int ForeignField<ForeignTable::ForeignColumn>
//   float Value
//   string Note // some comment
//   int Field?
ColumnDefinition DbdParser::ParseColumnLine(const std::string& line) {
    ColumnDefinition col;

    std::string rest = line;

    // Extract type token (first word before space)
    auto sp = rest.find(' ');
    if (sp == std::string::npos) {
        col.type = Trim(rest);
        return col;
    }
    col.type = Trim(rest.substr(0, sp));
    rest     = Trim(rest.substr(sp + 1));

    // Strip trailing comment
    auto cmt = rest.find("//");
    if (cmt != std::string::npos) {
        col.comment = Trim(rest.substr(cmt + 2));
        rest        = Trim(rest.substr(0, cmt));
    }

    // Strip foreign key <Table::Column> or <lang>
    auto lt = rest.find('<');
    auto gt = rest.rfind('>');
    if (lt != std::string::npos && gt != std::string::npos && gt > lt) {
        std::string fk = rest.substr(lt + 1, gt - lt - 1);
        rest            = Trim(rest.substr(0, lt));
        auto sep = fk.find("::");
        if (sep != std::string::npos) {
            col.foreignTable  = fk.substr(0, sep);
            col.foreignColumn = fk.substr(sep + 2);
            col.hasForeignKey = true;
        }
        // else it's "<lang>" - ignore
    }

    // Handle trailing '?' (unverified)
    if (!rest.empty() && rest.back() == '?') {
        rest.pop_back();
        col.verified = false;
    }

    return col;
}

// ParseFieldLine
//
// Format examples:
//   $id$ID<32>
//   $noninline,id$ID<32>
//   $relation$ForeignKey<u32>
//   Name<locstring>[3]
//   Field<16>
//   Field<u8>
//   Field[4]          (default 32-bit)
//   Field<32>[4]
FieldDefinition DbdParser::ParseFieldLine(
    const std::string& line,
    const std::map<std::string, ColumnDefinition>& columns) {

    FieldDefinition field;

    std::string rest = Trim(line);
    if (rest.empty()) return field;

    // Strip trailing comment
    auto cmt = rest.find("//");
    if (cmt != std::string::npos) {
        field.comment = Trim(rest.substr(cmt + 2));
        rest          = Trim(rest.substr(0, cmt));
    }

    // Parse leading annotations $...$
    if (!rest.empty() && rest.front() == '$') {
        auto closeAnn = rest.find('$', 1);
        if (closeAnn != std::string::npos) {
            std::string ann = rest.substr(1, closeAnn - 1);
            rest             = rest.substr(closeAnn + 1);

            // Annotations are comma-separated
            std::istringstream annss(ann);
            std::string tok;
            while (std::getline(annss, tok, ',')) {
                std::string a = Trim(tok);
                if (a == "id")           field.isId          = true;
                else if (a == "noninline") field.isNonInlineId = true;
                else if (a == "relation")  field.isRelation    = true;
            }
        }
    }

    // Parse array suffix [N] at the end
    {
        auto lb = rest.rfind('[');
        auto rb = rest.rfind(']');
        if (lb != std::string::npos && rb != std::string::npos && rb > lb) {
            std::string arrStr = rest.substr(lb + 1, rb - lb - 1);
            try { field.arrayCount = std::stoi(arrStr); } catch (...) {}
            rest = Trim(rest.substr(0, lb));
        }
    }

    // Parse size annotation <...> at the end of what remains
    {
        auto lt = rest.rfind('<');
        auto gt = rest.rfind('>');
        if (lt != std::string::npos && gt != std::string::npos && gt > lt) {
            std::string sizeStr = rest.substr(lt + 1, gt - lt - 1);
            rest                 = Trim(rest.substr(0, lt));

            // sizeStr is one of: "8", "u8", "16", "u16", "32", "u32", "64", "u64"
            if (!sizeStr.empty() && sizeStr.front() == 'u') {
                field.isSigned = false;
                sizeStr        = sizeStr.substr(1);
            } else {
                field.isSigned = true;
            }
            try { field.sizeBits = std::stoi(sizeStr); } catch (...) {}
        }
    }

    // What remains is the field name (possibly with trailing '?')
    if (!rest.empty() && rest.back() == '?') rest.pop_back();
    field.name = rest;

    // Resolve logical type from COLUMNS
    auto it = columns.find(field.name);
    if (it != columns.end()) {
        const auto& col = it->second;
        if (col.type == "float") {
            field.isFloat   = true;
            field.isSigned  = true;
            field.sizeBits  = 32;
        } else if (col.type == "string") {
            field.isString  = true;
            field.isSigned  = false;
            field.sizeBits  = 32;
        } else if (col.type == "locstring") {
            field.isLocString = true;
            field.isSigned    = false;
            field.sizeBits    = 32;
        }
        // "int" - keep whatever <size> annotation was parsed
    }

    return field;
}

// ParseBuildLine
//
// Examples:
//   BUILD 1.1.0.4044
//   BUILD 1.1.0.4044, 1.1.1.4222
//   BUILD 1.1.0.4044-1.12.1.5875
void DbdParser::ParseBuildLine(const std::string& line,
                                std::vector<uint32_t>& builds,
                                std::vector<std::pair<uint32_t, uint32_t>>& ranges) {
    // Strip "BUILD " prefix
    std::string rest = Trim(line.substr(5));

    // Split by ','
    std::istringstream ss(rest);
    std::string token;
    while (std::getline(ss, token, ',')) {
        token = Trim(token);
        if (token.empty()) continue;

        // Check for range: "A-B" where A and B are full version strings
        // A version string looks like "1.2.3.4567" so find '-' after a '.'
        // Strategy: find '-' that is not the first char and is preceded by a digit
        auto dashPos = std::string::npos;
        for (size_t i = 1; i < token.size(); ++i) {
            if (token[i] == '-' && std::isdigit((unsigned char)token[i - 1])) {
                dashPos = i;
                break;
            }
        }

        if (dashPos != std::string::npos) {
            std::string lo = Trim(token.substr(0, dashPos));
            std::string hi = Trim(token.substr(dashPos + 1));
            try {
                uint32_t loB = BuildVersion::FromString(lo).build;
                uint32_t hiB = BuildVersion::FromString(hi).build;
                ranges.push_back({loB, hiB});
            } catch (...) {}
        } else {
            try {
                uint32_t b = BuildVersion::FromString(token).build;
                builds.push_back(b);
            } catch (...) {}
        }
    }
}

}  // namespace dbc
