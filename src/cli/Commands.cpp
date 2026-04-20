#include "Commands.h"
#include "Dbc.h"

#include "../core/dbd/DbdParser.h"
#include "../core/version/VersionTable.h"
#include "export/CsvSchemaExporter.h"
#include "export/JsonSchemaExporter.h"

#include <filesystem>
#include <iomanip>
#include <iostream>
#include <optional>
#include <string>

namespace fs = std::filesystem;
using namespace dbc;

// ── Subcommand pointers (set by RegisterCommands, read by DispatchCommands) ──

static CLI::App* s_cmdVersion      = nullptr;
static CLI::App* s_cmdAbout        = nullptr;
static CLI::App* s_cmdExport       = nullptr;
static CLI::App* s_cmdSchema       = nullptr;
static CLI::App* s_cmdListVersions = nullptr;
static CLI::App* s_cmdListTables   = nullptr;
static CLI::App* s_cmdListFields   = nullptr;

// ── export options ────────────────────────────────────────────────────────────

static std::string s_expSource;
static std::string s_expVersion;
static std::string s_expTable;
static std::string s_expFormat  = "csv";
static std::string s_expOutput;
static bool        s_expAll     = false;
static bool        s_expClosest = false;

// ── schema options ────────────────────────────────────────────────────────────

static std::string s_schTable;
static std::string s_schVersion;
static std::string s_schFormat = "csv-schema";
static std::string s_schOutput;

// ── list tables options ───────────────────────────────────────────────────────

static std::string s_ltVersion;

// ── list fields options ───────────────────────────────────────────────────────

static std::string s_lfTable;
static std::string s_lfVersion;

// ─────────────────────────────────────────────────────────────────────────────

void RegisterCommands(CLI::App& app) {
    s_cmdVersion = app.add_subcommand("version", "Print tool version");
    s_cmdAbout   = app.add_subcommand("about",   "About dbccli");

    // ── export ────────────────────────────────────────────────────────────────
    s_cmdExport = app.add_subcommand("export", "Read DBC files and export data");
    s_cmdExport->add_option("source", s_expSource,
                            "Path to a .dbc file, a directory of .dbc files, or - for stdin")
               ->required();
    s_cmdExport->add_option("-v,--version", s_expVersion,
                            "Game version (e.g. 3.3.5, wrath, 12340)")
               ->required();
    s_cmdExport->add_option("-t,--table",   s_expTable,  "Table name (e.g. Map)");
    s_cmdExport->add_option("-f,--format",  s_expFormat, "Output format: csv (default), json");
    s_cmdExport->add_option("-o,--output",  s_expOutput,
                            "Output file path, or - for stdout (default: stdout).\n"
                            "With --all: output directory "
                            "(default: ./dbccli-export-<version>/ in the current directory)");
    s_cmdExport->add_flag("--all",        s_expAll,
                          "Export all tables; source must be a directory");
    s_cmdExport->add_flag("-c,--closest", s_expClosest,
                          "Allow fallback to closest matching layout on build mismatch");

    // ── schema ────────────────────────────────────────────────────────────────
    s_cmdSchema = app.add_subcommand("schema", "Export table field schema");
    s_cmdSchema->add_option("table", s_schTable, "Table name (e.g. Map)")->required();
    s_cmdSchema->add_option("-v,--version", s_schVersion,
                            "Game version (e.g. 3.3.5, wrath, 12340)")
               ->required();
    s_cmdSchema->add_option("-f,--format", s_schFormat,
                            "Output format: csv-schema (default), json-schema");
    s_cmdSchema->add_option("-o,--output", s_schOutput,
                            "Output file path, or - for stdout (default: stdout)");

    // ── list ──────────────────────────────────────────────────────────────────
    auto* cmdList = app.add_subcommand("list", "List versions, tables, or fields");
    cmdList->require_subcommand(1);

    s_cmdListVersions = cmdList->add_subcommand("versions", "List all known game versions");

    s_cmdListTables = cmdList->add_subcommand("tables",
                         "List tables in the blob. Supply -v to filter by game version");
    s_cmdListTables->add_option("-v,--version", s_ltVersion,
                                "Filter to tables with a definition for this version");

    s_cmdListFields = cmdList->add_subcommand("fields",
                         "List fields and types for a table at a given version");
    s_cmdListFields->add_option("table", s_lfTable, "Table name (e.g. Map)")->required();
    s_cmdListFields->add_option("-v,--version", s_lfVersion, "Game version")->required();
}

// ─────────────────────────────────────────────────────────────────────────────

int DispatchCommands(BlobReader& blob) {
    // ── version ───────────────────────────────────────────────────────────────
    if (s_cmdVersion->parsed()) {
        std::cout << "dbccli 0.1.0\n";
        return 0;
    }

    // ── about ─────────────────────────────────────────────────────────────────
    if (s_cmdAbout->parsed()) {
        std::cout <<
            "dbccli 0.1.0\n"
            "World of Warcraft DBC file extractor.\n"
            "Supports Vanilla, The Burning Crusade, and Wrath of the Lich King.\n"
            "\n"
            "Definitions sourced from WoWDBDefs:\n"
            "  https://github.com/wowdev/WoWDBDefs\n";
        return 0;
    }

    // ── list versions ─────────────────────────────────────────────────────────
    if (s_cmdListVersions->parsed()) {
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
                      << kv.patchName  << "\n";
        }
        return 0;
    }

    // ── list tables ───────────────────────────────────────────────────────────
    if (s_cmdListTables->parsed()) {
        if (s_ltVersion.empty()) {
            for (const auto& name : blob.TableNames())
                std::cout << name << "\n";
        } else {
            auto build = VersionTable::Resolve(s_ltVersion);
            if (!build) {
                std::cerr << "Error: unknown version \"" << s_ltVersion
                          << "\". Use 'dbccli list versions' to see supported versions.\n";
                return 1;
            }
            DbdParser parser;
            for (const auto& name : blob.TableNames()) {
                auto dbdText = blob.FindTable(name);
                if (!dbdText) continue;
                try {
                    auto tbl = parser.ParseBuffer(*dbdText, name);
                    if (FindVersionDef(tbl, *build) != nullptr)
                        std::cout << name << "\n";
                } catch (...) {}
            }
        }
        return 0;
    }

    // ── list fields ───────────────────────────────────────────────────────────
    if (s_cmdListFields->parsed()) {
        auto build = VersionTable::Resolve(s_lfVersion);
        if (!build) {
            std::cerr << "Error: unknown version \"" << s_lfVersion
                      << "\". Use 'dbccli list versions' to see supported versions.\n";
            return 1;
        }
        auto dbdText = blob.FindTable(s_lfTable);
        if (!dbdText) {
            std::cerr << "Error: table \"" << s_lfTable << "\" not found in blob\n";
            return 1;
        }
        DbdParser parser;
        auto tbl = parser.ParseBuffer(*dbdText, s_lfTable);
        const VersionDefinition* ver = FindVersionDef(tbl, *build);
        if (!ver) {
            std::cerr << "Error: no layout for " << s_lfTable
                      << " at build " << *build << "\n";
            return 1;
        }

        std::cout << std::left
                  << std::setw(26) << "Field"
                  << std::setw(14) << "Type"
                  << std::setw(10) << "Size"
                  << "Notes\n";
        std::cout << std::string(58, '-') << "\n";

        for (const auto& f : ver->fields) {
            std::string typeStr;
            if      (f.isLocString) typeStr = "locstring";
            else if (f.isString)    typeStr = "string";
            else if (f.isFloat)     typeStr = "float";
            else                    typeStr = f.isSigned ? "int" : "uint";

            std::string sizeStr;
            if (f.isLocString) {
                sizeStr = "?x32";  // locale count unknown without a DBC file
            } else {
                sizeStr = std::to_string(f.sizeBits);
                if (f.arrayCount > 1)
                    sizeStr += "[" + std::to_string(f.arrayCount) + "]";
            }

            std::string notes;
            if (f.isId)                                  notes = "$id$";
            else if (f.arrayCount > 1 && !f.isLocString) notes = "array";

            std::cout << std::left
                      << std::setw(26) << f.name
                      << std::setw(14) << typeStr
                      << std::setw(10) << sizeStr
                      << notes << "\n";
        }
        return 0;
    }

    // ── schema ────────────────────────────────────────────────────────────────
    if (s_cmdSchema->parsed()) {
        if (s_schFormat != "csv-schema" && s_schFormat != "json-schema") {
            std::cerr << "Error: unknown format \"" << s_schFormat
                      << "\". Valid values: csv-schema, json-schema\n";
            return 1;
        }
        auto build = VersionTable::Resolve(s_schVersion);
        if (!build) {
            std::cerr << "Error: unknown version \"" << s_schVersion
                      << "\". Use 'dbccli list versions' to see supported versions.\n";
            return 1;
        }
        auto dbdText = blob.FindTable(s_schTable);
        if (!dbdText) {
            std::cerr << "Error: table \"" << s_schTable << "\" not found in blob\n";
            return 1;
        }
        DbdParser parser;
        auto tbl = parser.ParseBuffer(*dbdText, s_schTable);
        const VersionDefinition* ver = FindVersionDef(tbl, *build);
        if (!ver) {
            std::cerr << "Error: no layout for " << s_schTable
                      << " at build " << *build << "\n";
            return 1;
        }
        std::string dest = s_schOutput.empty() ? "-" : s_schOutput;
        if (s_schFormat == "json-schema") {
            JsonSchemaExporter exp;
            return exp.Export(*ver, s_schTable, dest) ? 0 : 1;
        } else {
            CsvSchemaExporter exp;
            return exp.Export(*ver, s_schTable, dest) ? 0 : 1;
        }
    }

    // ── export ────────────────────────────────────────────────────────────────
    if (s_cmdExport->parsed()) {
        if (s_expFormat != "csv" && s_expFormat != "json") {
            std::cerr << "Error: unknown format \"" << s_expFormat
                      << "\". Valid values: csv, json\n";
            return 1;
        }
        if (s_expAll && !s_expTable.empty()) {
            std::cerr << "Error: -t/--table and --all are mutually exclusive\n";
            return 1;
        }
        if (s_expAll && s_expSource == "-") {
            std::cerr << "Error: --all cannot be used with stdin input\n";
            return 1;
        }
        if (!s_expAll && s_expTable.empty()) {
            std::cerr << "Error: -t/--table is required (or use --all)\n";
            return 1;
        }

        auto build = VersionTable::Resolve(s_expVersion);
        if (!build) {
            std::cerr << "Error: unknown version \"" << s_expVersion
                      << "\". Use 'dbccli list versions' to see supported versions.\n";
            return 1;
        }

        // Resolve source positional into dbcSource (file/stdin) or dbcDir
        std::string dbcSource;
        std::string dbcDir;
        if (s_expSource == "-") {
            dbcSource = "-";
        } else {
            std::error_code ec;
            if (fs::is_directory(fs::path(s_expSource), ec)) {
                dbcDir = s_expSource;
            } else {
                dbcSource = s_expSource;
            }
        }

        if (s_expAll) {
            if (dbcDir.empty()) {
                std::cerr << "Error: --all requires source to be a directory\n";
                return 1;
            }
            std::string outDir = s_expOutput.empty()
                ? (fs::current_path() / ("dbccli-export-" + s_expVersion)).string()
                : s_expOutput;
            fs::create_directories(outDir);

            int extracted = 0, skipped = 0, failures = 0;
            for (const auto& name : blob.TableNames()) {
                std::string dest =
                    (fs::path(outDir) / (name + "." + s_expFormat)).string();
                std::cerr << "Extracting " << name << "...\n";
                int rc = ExtractTable(blob, *build, name, "", dbcDir, dest,
                                      s_expFormat, s_expClosest);
                if      (rc == kExtractOk)       ++extracted;
                else if (rc == kExtractNotFound)  ++skipped;
                else                              ++failures;
            }
            std::cerr << "Extracted " << extracted << " table(s)."
                      << " Skipped "  << skipped   << " (file not found)."
                      << " Failed "   << failures  << " (definition mismatch or error).\n";
            return failures > 0 ? 1 : 0;
        }

        // Single table
        std::string dest = s_expOutput.empty() ? "-" : s_expOutput;
        return ExtractTable(blob, *build, s_expTable, dbcSource, dbcDir, dest,
                            s_expFormat, s_expClosest);
    }

    return 0;
}
