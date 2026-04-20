#include "Dbc.h"

#include "../core/dbd/DbdParser.h"
#include "../core/reader/WdbcReader.h"
#include "export/CsvExporter.h"
#include "export/JsonExporter.h"

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <vector>

namespace fs = std::filesystem;
using namespace dbc;

// Case-insensitive DBC file search
static std::optional<fs::path> FindDbcFile(const fs::path& dir,
                                            const std::string& tableName) {
    std::string lowerTarget = tableName;
    std::transform(lowerTarget.begin(), lowerTarget.end(), lowerTarget.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        if (!entry.is_regular_file()) continue;
        std::string ext = entry.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        if (ext != ".dbc") continue;

        std::string stem = entry.path().stem().string();
        std::transform(stem.begin(), stem.end(), stem.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        if (stem == lowerTarget) return entry.path();
    }
    return std::nullopt;
}

// Count the 4-byte slots a VersionDefinition expects, excluding locstrings.
// Returns {scalarSlots, locStringFieldCount}.
static std::pair<uint32_t, uint32_t> DefSlotCounts(const VersionDefinition& v) {
    uint32_t scalarSlots  = 0;
    uint32_t locStrFields = 0;
    for (const auto& f : v.fields) {
        if (f.isNonInlineId) continue;
        if (f.isLocString) {
            ++locStrFields;
        } else {
            scalarSlots += static_cast<uint32_t>(f.arrayCount) *
                           static_cast<uint32_t>(f.sizeBits / 32);
        }
    }
    return {scalarSlots, locStrFields};
}

// Returns true when this VersionDefinition can produce a consistent locstring
// entry count given the file's 4-byte field count (all-32-bit-slot record).
static bool DefFitsFile(const VersionDefinition& v, uint32_t fileFieldCount) {
    auto [scalar, locFields] = DefSlotCounts(v);
    if (fileFieldCount < scalar) return false;
    if (locFields == 0) return scalar == fileFieldCount;
    uint32_t rem = fileFieldCount - scalar;
    if (rem % locFields != 0) return false;
    uint32_t locEntries = rem / locFields;
    return locEntries >= 8 && locEntries <= 17;
}

const VersionDefinition* FindVersionDef(const TableDefinition& tbl,
                                         uint32_t build,
                                         uint32_t fileFieldCount,
                                         bool closest) {
    const VersionDefinition* best = nullptr;
    for (const auto& v : tbl.versions) {
        for (uint32_t b : v.builds) {
            if (b == build) { best = &v; goto found; }
        }
        for (auto& [lo, hi] : v.buildRanges) {
            if (build >= lo && build <= hi) { best = &v; goto found; }
        }
    }
found:
    if (best == nullptr) {
        if (!closest || fileFieldCount == 0) return nullptr;
        for (const auto& v : tbl.versions) {
            if (DefFitsFile(v, fileFieldCount)) {
                best = &v;
                break;
            }
        }
        if (best != nullptr) {
            std::string rangeStr;
            if (!best->buildRanges.empty()) {
                rangeStr = std::to_string(best->buildRanges.front().first) + "-" +
                           std::to_string(best->buildRanges.back().second);
            } else if (!best->builds.empty()) {
                rangeStr = std::to_string(best->builds.front());
            } else {
                rangeStr = "unknown";
            }
            std::cerr << "Warning: " << tbl.name << " build " << build
                      << " unmatched, using [" << rangeStr << "]\n";
        }
        return best;
    }
    // Exact match found; if field count doesn't fit the file, try all blocks.
    if (fileFieldCount > 0 && !DefFitsFile(*best, fileFieldCount)) {
        for (const auto& v : tbl.versions) {
            if (&v == best) continue;
            if (DefFitsFile(v, fileFieldCount)) {
                best = &v;
                break;
            }
        }
    }
    return best;
}

int ExtractTable(BlobReader& blob,
                 uint32_t build,
                 const std::string& tableName,
                 const std::string& dbcSource,
                 const std::string& dbcDir,
                 const std::string& outPath,
                 const std::string& format,
                 bool closest) {
    // 1. Get DBD text from blob
    auto dbdText = blob.FindTable(tableName);
    if (!dbdText) {
        std::cerr << "Error: table \"" << tableName
                  << "\" is not supported for build " << build << "\n";
        return 1;
    }

    // 2. Parse DBD
    DbdParser parser;
    TableDefinition tbl;
    try {
        tbl = parser.ParseBuffer(*dbdText, tableName);
    } catch (const std::exception& e) {
        std::cerr << "Error: failed to parse DBD for " << tableName
                  << ": " << e.what() << "\n";
        return 1;
    }

    // 3. Open DBC first so we can pass fileFieldCount to FindVersionDef.
    //    This lets the fallback logic pick the right definition block when the
    //    build-matched block has the wrong scalar count (common with vanilla
    //    retail files vs WoW Classic-documented definitions).
    WdbcReader reader;
    bool       opened = false;

    if (dbcSource == "-") {
        // Read from stdin
        std::vector<uint8_t> buf(std::istreambuf_iterator<char>(std::cin), {});
        opened = reader.OpenBuffer(std::move(buf));
    } else if (!dbcSource.empty()) {
        opened = reader.Open(dbcSource);
    } else {
        // Search directory
        auto found = FindDbcFile(dbcDir, tableName);
        if (!found) {
            std::cerr << "Error: could not find " << tableName
                      << ".dbc in directory " << dbcDir << "\n";
            return kExtractNotFound;
        }
        opened = reader.Open(*found);
    }

    if (!opened) {
        std::cerr << "Error: failed to open DBC file for " << tableName << "\n";
        return 1;
    }

    // 4. Find VersionDefinition - now that we have the file's fieldCount we can
    //    fall back to the best-fit definition when the build-matched block has
    //    the wrong scalar count (e.g. vanilla retail vs WoW Classic definitions).
    const VersionDefinition* ver = FindVersionDef(tbl, build, reader.FieldCount(), closest);
    if (!ver) {
        std::cerr << "Error: no layout found in DBD for \"" << tableName
                  << "\" at build " << build
                  << ". Use -c/--closest to allow fallback to nearest layout.\n";
        return kExtractError;
    }

    // 5. Set definition + validate (auto-detects locstring entry count)
    reader.SetDefinition(*ver);
    std::string errMsg;
    if (!reader.Validate(errMsg)) {
        std::cerr << "Error: record size mismatch for " << tableName
                  << " (" << errMsg << "). Check -v/--version is correct.\n";
        return kExtractMismatch;
    }

    // 6. Export
    if (format == "json") {
        JsonExporter exporter;
        if (!exporter.Export(reader, tableName, outPath)) return 1;
    } else {
        CsvExporter exporter;
        if (!exporter.Export(reader, tableName, outPath)) return 1;
    }

    return 0;
}
