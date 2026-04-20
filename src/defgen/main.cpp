#include "filter.h"
#include "diagnostics.h"

#include "../core/dbd/DbdParser.h"
#include "../core/blob/BlobReader.h"
#include "../core/blob/BlobFormat.h"

#include <CLI/CLI.hpp>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace dbc;

static std::string ToLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return s;
}

// Writes a uint32_t into a byte vector in little-endian order.
static void PushU32(std::vector<uint8_t>& v, uint32_t val) {
    v.push_back(static_cast<uint8_t>(val >>  0));
    v.push_back(static_cast<uint8_t>(val >>  8));
    v.push_back(static_cast<uint8_t>(val >> 16));
    v.push_back(static_cast<uint8_t>(val >> 24));
}

int main(int argc, char* argv[]) {
    bool        verbose      = false;
    bool        debug        = false;
    bool        validate     = false;
    bool        dumpFiltered = false;
    std::string singleFile;
    std::string defsArg;
    std::string outArg;

    CLI::App app{"defgen: generates a binary blob of filtered WoWDBDefs definitions"};
    app.add_flag("-v,--verbose", verbose, "Print per-table and per-block diagnostics");
    app.add_flag("-d,--debug",   debug,   "Print full field lists per block (implies --verbose)");
    app.add_flag("--validate",   validate, "Run all checks without writing output");
    app.add_flag("--dump-filtered", dumpFiltered,
        "Print the filtered .dbd text to stdout (single-file mode only)");
    app.add_option("-f,--file",  singleFile,
        "Inspect a single .dbd file and print diagnostics; no output is written");

    // Both positional arguments are optional at the CLI11 level so we can
    // accept them being absent in --file and --validate modes.
    app.add_option("definitions-dir", defsArg, "Directory containing .dbd definition files")
        ->check(CLI::ExistingDirectory);
    app.add_option("output-header", outArg, "Path to write the generated C++ header");

    CLI11_PARSE(app, argc, argv);

    // --debug implies --verbose
    if (debug) verbose = true;

    // Single-file mode: parse one .dbd, print diagnostics, then exit.
    // No blob is built and no output file is written.
    if (!singleFile.empty()) {
        fs::path p(singleFile);
        if (!fs::is_regular_file(p)) {
            std::cerr << "Error: not a file: " << p << "\n";
            return 1;
        }

        std::ifstream fs_in(p, std::ios::in);
        if (!fs_in.is_open()) {
            std::cerr << "Error: cannot open " << p << "\n";
            return 1;
        }

        // Read the entire file into a string. The iterator-pair constructor
        // streams all bytes from the file iterator into the string directly
        // without needing to know the file size in advance.
        std::string rawText((std::istreambuf_iterator<char>(fs_in)),
                             std::istreambuf_iterator<char>());

        DbdParser parser;
        TableDefinition tbl;
        try {
            tbl = parser.ParseFile(p);
        } catch (const std::exception& e) {
            std::cerr << "Error: parse failed: " << e.what() << "\n";
            return 1;
        }

        CheckDuplicateBuilds(tbl);

        int blocksKept = 0, blocksDropped = 0;
        for (const auto& v : tbl.versions) {
            if (VersionRelevant(v)) ++blocksKept;
            else                    ++blocksDropped;
        }

        std::cerr << "[single-file] " << tbl.name
                  << "  " << blocksKept << " block(s) kept";
        if (blocksDropped > 0) std::cerr << ", " << blocksDropped << " dropped";
        std::cerr << "\n";

        // Always print at least verbose output in single-file mode so the
        // mode is useful without requiring explicit flags.
        PrintBlockInfo(tbl, /*verbose=*/true, debug);

        // Emit the filtered .dbd text to stdout when requested, so the caller
        // can diff it directly against the original source file.
        if (dumpFiltered) {
            std::cout << FilterDbdText(rawText, tbl);
        }

        return 0;
    }

    // Directory-walk mode requires the definitions-dir positional.
    if (defsArg.empty()) {
        std::cerr << app.help();
        return 1;
    }
    // Output path is only required when actually writing output.
    if (!validate && outArg.empty()) {
        std::cerr << "Error: output-header is required unless --validate is set\n";
        return 1;
    }

    fs::path defsDir(defsArg);
    fs::path outPath(outArg);

    // Holds the name and filtered text for each table that survives the filter.
    struct Entry {
        std::string name;      // mixed-case table name (used as-is in the blob index)
        std::string filtered;  // the filtered .dbd text that gets embedded
    };

    std::vector<Entry> entries;
    DbdParser parser;
    int       skipped = 0;

    for (const auto& dirEntry : fs::directory_iterator(defsDir)) {
        if (!dirEntry.is_regular_file()) continue;
        if (dirEntry.path().extension() != ".dbd") continue;

        std::string tableName = dirEntry.path().stem().string();

        std::ifstream fs_in(dirEntry.path(), std::ios::in);
        if (!fs_in.is_open()) {
            std::cerr << "Warning: cannot open " << dirEntry.path() << "\n";
            continue;
        }
        // Read the entire file into a string (see note in single-file mode above).
        std::string rawText((std::istreambuf_iterator<char>(fs_in)),
                             std::istreambuf_iterator<char>());

        TableDefinition tbl;
        try {
            tbl = parser.ParseFile(dirEntry.path());
        } catch (const std::exception& e) {
            std::cerr << "Warning: parse error in " << tableName << ": " << e.what() << "\n";
            ++skipped;
            continue;
        }

        CheckDuplicateBuilds(tbl);

        int blocksKept    = 0;
        int blocksDropped = 0;
        for (const auto& v : tbl.versions) {
            if (VersionRelevant(v)) ++blocksKept;
            else                    ++blocksDropped;
        }

        if (blocksKept == 0) {
            ++skipped;
            if (verbose) std::cerr << "[skipped]  " << tableName << "\n";
            continue;
        }


        // FilterDbdText uses the original parsed table so that the versionIdx
        // counter inside the function lines up with LAYOUT/BUILD lines in the
        // raw text. Passing a pre-filtered table would shift the indices.
        std::string filtered = FilterDbdText(rawText, tbl);
        entries.push_back({ tableName, std::move(filtered) });

        if (verbose) {
            std::cerr << "[included] "
                      << std::left << std::setw(40) << tableName
                      << "  " << blocksKept << " block(s) kept";
            if (blocksDropped > 0)
                std::cerr << ", " << blocksDropped << " dropped";
            std::cerr << "\n";
        }

        if (verbose || debug) {
            PrintBlockInfo(tbl, verbose, debug);
        }
    }

    // In non-verbose mode, emit a one-line stderr summary so users know how
    // many tables were excluded without having to dig into the stdout summary.
    if (!verbose && skipped > 0) {
        std::cerr << "defgen: " << skipped
                  << " table(s) had no relevant blocks (use --verbose to list them)\n";
    }

    // Sort entries by name so the generated blob is deterministic regardless
    // of the filesystem iteration order.
    std::sort(entries.begin(), entries.end(),
              [](const Entry& a, const Entry& b) { return a.name < b.name; });

    // FNV-1a hash collision check
    //
    // The blob index uses 32-bit FNV-1a hashes of the lowercase table name for
    // O(log n) lookup. A collision between two table names would silently break
    // lookups, so we verify here at generation time.
    std::vector<std::pair<uint32_t, std::string>> sortedHashes;
    sortedHashes.reserve(entries.size());
    for (const auto& e : entries)
        sortedHashes.push_back({ Fnv1a(ToLower(e.name)), e.name });
    std::sort(sortedHashes.begin(), sortedHashes.end());
    for (size_t i = 1; i < sortedHashes.size(); ++i) {
        if (sortedHashes[i].first == sortedHashes[i - 1].first) {
            std::cerr << "FATAL: FNV1a hash collision between '"
                      << sortedHashes[i - 1].second << "' and '"
                      << sortedHashes[i].second << "'\n";
            return 2;
        }
    }

    // In --validate mode all parsing and checks have now run. Exit without
    // building the blob or writing any file.
    uint32_t tableCount = static_cast<uint32_t>(entries.size());
    if (validate) {
        std::cout << "defgen [validate]: " << tableCount << " tables OK, "
                  << skipped << " skipped. No output written.\n";
        return 0;
    }

    // Blob construction
    //
    // Binary layout:
    //   Header  : magic(4) + version(4) + count(4) = 12 bytes
    //   Index   : count * 44 bytes per entry
    //             entry = hash(4) + name[32] + data_offset(4) + data_len(4)
    //   Data    : all filtered .dbd texts concatenated, no separators
    //
    // At runtime BlobReader locates a table by binary-searching the index on
    // the hash field, then reads data_len bytes starting at data_offset.

    std::vector<uint32_t> offsets(tableCount);
    uint32_t dataOffset = 0;
    for (uint32_t i = 0; i < tableCount; ++i) {
        offsets[i]  = dataOffset;
        dataOffset += static_cast<uint32_t>(entries[i].filtered.size());
    }

    std::vector<uint8_t> blob;
    blob.reserve(12 + tableCount * 44 + dataOffset);

    PushU32(blob, BDBC_MAGIC);
    PushU32(blob, BDBC_VERSION);
    PushU32(blob, tableCount);

    for (uint32_t i = 0; i < tableCount; ++i) {
        PushU32(blob, Fnv1a(ToLower(entries[i].name)));

        // Name field: fixed 32-byte null-padded buffer, max 31 usable characters.
        char nameBuf[32]{};
        std::strncpy(nameBuf, entries[i].name.c_str(), 31);
        for (char c : nameBuf) blob.push_back(static_cast<uint8_t>(c));

        PushU32(blob, offsets[i]);
        PushU32(blob, static_cast<uint32_t>(entries[i].filtered.size()));
    }

    for (const auto& e : entries) {
        for (char c : e.filtered) blob.push_back(static_cast<uint8_t>(c));
    }

    // Write header file

    if (outPath.has_parent_path()) {
        fs::create_directories(outPath.parent_path());
    }

    std::ofstream out(outPath, std::ios::out | std::ios::trunc);
    if (!out.is_open()) {
        std::cerr << "Error: cannot write to " << outPath << "\n";
        return 1;
    }

    out << "// AUTO-GENERATED - DO NOT EDIT\n";
    out << "// Generated by filter_tool (src/defs) at build time\n";
    out << "// Tables: " << tableCount << "  Total size: " << blob.size() << " bytes\n\n";
    out << "#pragma once\n";
    out << "#include <cstdint>\n\n";
    out << "namespace dbc::embedded {\n\n";
    out << "    static constexpr uint32_t BDBC_MAGIC        = 0x42444243u;\n";
    out << "    static constexpr uint32_t BDBC_VERSION      = 1u;\n";
    out << "    static constexpr uint32_t BDBC_TABLE_COUNT  = " << tableCount << "u;\n";
    out << "    static constexpr uint32_t BDBC_DATA_SIZE    = " << blob.size() << "u;\n\n";
    out << "    static const uint8_t BDBC_DATA[] = {\n";

    // Write the blob as a hex byte array, 16 bytes per line.
    for (size_t i = 0; i < blob.size(); ++i) {
        if (i % 16 == 0) out << "        ";
        char hex[8];
        std::snprintf(hex, sizeof(hex), "0x%02X", blob[i]);
        out << hex;
        if (i + 1 < blob.size()) out << ',';
        if (i % 16 == 15 || i + 1 == blob.size()) out << "\n";
        else out << ' ';
    }

    out << "    };\n\n";
    out << "}  // namespace dbc::embedded\n";

    std::cout << "defgen: included " << tableCount << " tables, skipped "
              << skipped << ", blob size " << blob.size() << " bytes\n";
    std::cout << "Output: " << outPath << "\n";

    return 0;
}

