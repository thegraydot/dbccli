#include "../core/blob/BlobReader.h"
#include "../core/dbd/DbdParser.h"
#include "../core/version/VersionTable.h"
#include "../core/reader/WdbcReader.h"
#include "export/CsvExporter.h"

// The generated blob - included only in the CLI binary
#include "../../generated/defs.h"

#include <CLI/CLI.hpp>

#include <algorithm>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <optional>
#include <string>
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

// Find the best VersionDefinition for a given build.
// fileFieldCount (from the DBC header) is used to fall back to a definition
// whose slot layout actually matches the file when the build-matched block has
// a mismatched scalar count (which can happen with old vanilla builds where
// community definition repos document the WoW Classic field layout instead of
// the original retail client layout).
static const VersionDefinition* FindVersionDef(const TableDefinition& tbl,
                                                uint32_t build,
                                                uint32_t fileFieldCount = 0) {
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
    // If we have file metadata and the matched definition's scalar count doesn't
    // fit the file, search all version blocks for one that does.
    if (fileFieldCount > 0 && best != nullptr &&
        !DefFitsFile(*best, fileFieldCount)) {
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

// Extract one table

static int ExtractTable(BlobReader& blob, uint32_t build,
                         const std::string& tableName,
                         const std::string& dbcSource,   // file path or "-"
                         const std::string& dbcDir,
                         const std::string& outPath) {
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
            return 1;
        }
        opened = reader.Open(*found);
    }

    if (!opened) {
        std::cerr << "Error: failed to open DBC file for " << tableName << "\n";
        return 1;
    }

    // 4. Find VersionDefinition — now that we have the file's fieldCount we can
    //    fall back to the best-fit definition when the build-matched block has
    //    the wrong scalar count (e.g. vanilla retail vs WoW Classic definitions).
    const VersionDefinition* ver = FindVersionDef(tbl, build, reader.FieldCount());
    if (!ver) {
        std::cerr << "Error: no layout found in DBD for \"" << tableName
                  << "\" at build " << build << "\n";
        return 1;
    }

    // 5. Set definition + validate (auto-detects locstring entry count)
    reader.SetDefinition(*ver);
    std::string errMsg;
    if (!reader.Validate(errMsg)) {
        std::cerr << "Error: record size mismatch for " << tableName
                  << " (" << errMsg << "). Check --version is correct.\n";
        return 1;
    }

    // 6. Export CSV
    CsvExporter exporter;
    if (!exporter.Export(reader, tableName, outPath)) {
        return 1;
    }

    return 0;
}

// main

int main(int argc, char* argv[]) {
    CLI::App app{"dbccli - World of Warcraft DBC extractor"};
    app.set_version_flag("--version-info", "dbccli 0.1.0");

    std::string versionStr;
    std::string dbcDir;
    std::string dbcFile;
    std::string dbcStdin;  // "--dbc -" flag
    std::string outPath;
    std::string outDir;
    std::string tableName;
    std::string format = "csv";
    bool        doAll         = false;
    bool        listTables    = false;
    bool        listFields    = false;
    bool        listVersions  = false;
    bool        readStdin     = false;

    app.add_option("--version,-v", versionStr, "Game version (e.g. 3.3.5, wrath, 12340)");
    app.add_option("--dbc-dir",    dbcDir,    "Directory containing extracted .dbc files");
    app.add_option("--dbc-file",   dbcFile,   "Path to a single .dbc file");
    app.add_flag  ("--dbc",        readStdin, "Read .dbc from stdin");
    app.add_option("--table",      tableName, "Table name (e.g. Map)");
    app.add_option("--out",        outPath,   "Output file path, or - for stdout");
    app.add_option("--out-dir",    outDir,    "Output directory (for --all)");
    app.add_option("--format",     format,    "Output format: csv (default)");
    app.add_flag  ("--all",        doAll,     "Extract all tables");
    app.add_flag  ("--list-tables",  listTables,  "List tables supported for version");
    app.add_flag  ("--list-fields",  listFields,  "List fields for --table");
    app.add_flag  ("--list-versions",listVersions,"List all known versions");

    CLI11_PARSE(app, argc, argv);

    // -----------------------------------------------------------------------
    // --list-versions  (no version required)
    // -----------------------------------------------------------------------
    if (listVersions) {
        std::cout << std::left
                  << std::setw(12) << "Version"
                  << std::setw(8)  << "Build"
                  << std::setw(10) << "Expansion"
                  << "Patch\n";
        std::cout << std::string(60, '-') << "\n";
        for (const auto& kv : VersionTable::AllVersions()) {
            std::cout << std::setw(12) << kv.versionString
                      << std::setw(8)  << kv.buildNumber
                      << std::setw(10) << kv.expansion
                      << kv.patchName << "\n";
        }
        return 0;
    }

    // -----------------------------------------------------------------------
    // Resolve version
    // -----------------------------------------------------------------------
    std::optional<uint32_t> build;
    if (!versionStr.empty()) {
        build = VersionTable::Resolve(versionStr);
        if (!build) {
            std::cerr << "Error: unknown version \"" << versionStr
                      << "\". Use --list-versions to see supported versions.\n";
            return 1;
        }
    }

    // -----------------------------------------------------------------------
    // Load embedded blob
    // -----------------------------------------------------------------------
    BlobReader blob;
    blob.Load(dbc::embedded::BDBC_DATA, dbc::embedded::BDBC_DATA_SIZE);

    // -----------------------------------------------------------------------
    // --list-tables
    // -----------------------------------------------------------------------
    if (listTables) {
        if (!build) {
            std::cerr << "Error: --list-tables requires --version\n";
            return 1;
        }
        // List all table names in the blob (they were already filtered to ≤ 12340)
        for (const auto& name : blob.TableNames()) {
            std::cout << name << "\n";
        }
        return 0;
    }

    // -----------------------------------------------------------------------
    // --list-fields
    // -----------------------------------------------------------------------
    if (listFields) {
        if (!build) { std::cerr << "Error: --list-fields requires --version\n"; return 1; }
        if (tableName.empty()) { std::cerr << "Error: --list-fields requires --table\n"; return 1; }

        auto dbdText = blob.FindTable(tableName);
        if (!dbdText) {
            std::cerr << "Error: table \"" << tableName << "\" not found in blob\n";
            return 1;
        }

        DbdParser parser;
        auto tbl = parser.ParseBuffer(*dbdText, tableName);
        const VersionDefinition* ver = FindVersionDef(tbl, *build);
        if (!ver) {
            std::cerr << "Error: no layout for " << tableName << " at build " << *build << "\n";
            return 1;
        }

        for (const auto& f : ver->fields) {
            std::cout << f.name;
            if (f.isLocString)  std::cout << "  [locstring]";
            else if (f.isString) std::cout << "  [string]";
            else if (f.isFloat)  std::cout << "  [float]";
            else                 std::cout << "  [" << (f.isSigned ? "" : "u") << "int" << f.sizeBits << "]";
            if (f.arrayCount > 1) std::cout << "[" << f.arrayCount << "]";
            if (f.isId)           std::cout << " $id$";
            std::cout << "\n";
        }
        return 0;
    }

    // -----------------------------------------------------------------------
    // Validate mutual exclusions
    // -----------------------------------------------------------------------
    if (doAll && !tableName.empty()) {
        std::cerr << "Error: --table and --all are mutually exclusive\n";
        return 1;
    }
    if (doAll && readStdin) {
        std::cerr << "Error: --all cannot be used with stdin input\n";
        return 1;
    }
    if (doAll && outDir.empty()) {
        std::cerr << "Error: --all requires --out-dir\n";
        return 1;
    }

    if (!build) {
        std::cerr << "Error: --version is required for extraction commands\n";
        return 1;
    }

    // -----------------------------------------------------------------------
    // --all
    // -----------------------------------------------------------------------
    if (doAll) {
        if (dbcDir.empty()) {
            std::cerr << "Error: --all requires --dbc-dir\n";
            return 1;
        }
        fs::create_directories(outDir);
        int errors = 0;
        for (const auto& name : blob.TableNames()) {
            std::string dest = (fs::path(outDir) / (name + ".csv")).string();
            std::cerr << "Extracting " << name << "...\n";
            if (ExtractTable(blob, *build, name, "", dbcDir, dest) != 0) {
                ++errors;
            }
        }
        if (errors > 0) {
            std::cerr << errors << " table(s) failed to extract\n";
        }
        return errors > 0 ? 1 : 0;
    }

    // -----------------------------------------------------------------------
    // Single table
    // -----------------------------------------------------------------------
    if (tableName.empty()) {
        std::cerr << "Error: --table is required (or use --all)\n";
        return 1;
    }

    std::string dbcSource;
    if (readStdin) {
        dbcSource = "-";
    } else if (!dbcFile.empty()) {
        dbcSource = dbcFile;
    }

    std::string dest = outPath.empty() ? "-" : outPath;

    return ExtractTable(blob, *build, tableName, dbcSource, dbcDir, dest);
}
