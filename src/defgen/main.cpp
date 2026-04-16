#include "../core/dbd/DbdParser.h"
#include "../core/blob/BlobReader.h"   // for Fnv1a and BlobFormat constants
#include "../core/blob/BlobFormat.h"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace dbc;

static constexpr uint32_t MAX_BUILD = 12340u;

// Helpers

static std::string ToLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return s;
}

// Check if a VersionDefinition contains at least one build ≤ MAX_BUILD
static bool VersionRelevant(const VersionDefinition& v) {
    for (uint32_t b : v.builds) {
        if (b <= MAX_BUILD) return true;
    }
    for (auto& [lo, hi] : v.buildRanges) {
        if (lo <= MAX_BUILD) return true;
    }
    return false;
}

// Slice the raw DBD text to only include layout blocks that are relevant.
// We do this by re-reading the file text and keeping only the lines that
// belong to the COLUMNS section or to relevant VERSION blocks.
static std::string FilterDbdText(const std::string& rawText,
                                  const TableDefinition& parsed) {
    // Walk the raw text line by line, collecting sections
    std::istringstream ss(rawText);
    std::string line;

    std::string result;
    result.reserve(rawText.size());

    // State: are we in the COLUMNS block or in a version block?
    bool     inColumns      = false;
    bool     inVersionBlock = false;
    bool     keepCurrent    = false;
    int      versionIdx     = -1;

    while (std::getline(ss, line)) {
        std::string trimmed = line;
        // Remove CR
        if (!trimmed.empty() && trimmed.back() == '\r') trimmed.pop_back();
        while (!trimmed.empty() && (trimmed.front() == ' ' || trimmed.front() == '\t'))
            trimmed = trimmed.substr(1);

        // Blank lines serve as version block terminators
        if (trimmed.empty()) {
            if (inVersionBlock && keepCurrent) result += "\n";
            if (inVersionBlock) inVersionBlock = false;
            continue;
        }

        if (trimmed == "COLUMNS") {
            inColumns      = true;
            inVersionBlock = false;
            keepCurrent    = true;
            result += line + "\n";
            continue;
        }

        if (trimmed.rfind("LAYOUT", 0) == 0) {
            inColumns      = false;
            inVersionBlock = true;
            ++versionIdx;
            keepCurrent = (versionIdx < static_cast<int>(parsed.versions.size()) &&
                           VersionRelevant(parsed.versions[static_cast<size_t>(versionIdx)]));
            if (keepCurrent) result += line + "\n";
            continue;
        }

        if (trimmed.rfind("BUILD", 0) == 0) {
            if (!inVersionBlock) {
                // Start a build-only version block
                inColumns      = false;
                inVersionBlock = true;
                ++versionIdx;
                keepCurrent = (versionIdx < static_cast<int>(parsed.versions.size()) &&
                               VersionRelevant(parsed.versions[static_cast<size_t>(versionIdx)]));
            }
            if (keepCurrent) result += line + "\n";
            continue;
        }

        if (inColumns) {
            result += line + "\n";
            continue;
        }

        if (inVersionBlock && keepCurrent) {
            result += line + "\n";
        }
    }

    return result;
}

// Write little-endian uint32 into a vector
static void PushU32(std::vector<uint8_t>& v, uint32_t val) {
    v.push_back(static_cast<uint8_t>(val >>  0));
    v.push_back(static_cast<uint8_t>(val >>  8));
    v.push_back(static_cast<uint8_t>(val >> 16));
    v.push_back(static_cast<uint8_t>(val >> 24));
}

// Main

int main(int argc, char* argv[]) {
    bool verbose = false;
    bool debug   = false;
    int  argStart = 1;

    while (argStart < argc && argv[argStart][0] == '-') {
        std::string flag(argv[argStart]);
        if (flag == "--verbose" || flag == "-v") verbose = true;
        else if (flag == "--debug"   || flag == "-d") { verbose = true; debug = true; }
        ++argStart;
    }

    if (argc - argStart != 2) {
        std::cerr << "Usage: defgen [--verbose] [--debug] <definitions-dir> <output-header>\n";
        return 1;
    }

    fs::path defsDir(argv[argStart]);
    fs::path outPath(argv[argStart + 1]);

    if (!fs::is_directory(defsDir)) {
        std::cerr << "Error: not a directory: " << defsDir << "\n";
        return 1;
    }

    // -----------------------------------------------------------------------
    // Collect and filter all .dbd files
    // -----------------------------------------------------------------------
    struct Entry {
        std::string name;       // mixed-case table name
        std::string filtered;   // filtered DBD text
    };

    std::vector<Entry> entries;
    DbdParser parser;
    int       skipped = 0;

    for (const auto& dirEntry : fs::directory_iterator(defsDir)) {
        if (!dirEntry.is_regular_file()) continue;
        if (dirEntry.path().extension() != ".dbd") continue;

        std::string tableName = dirEntry.path().stem().string();

        // Read raw text
        std::ifstream fs_in(dirEntry.path(), std::ios::in);
        if (!fs_in.is_open()) {
            std::cerr << "Warning: cannot open " << dirEntry.path() << "\n";
            continue;
        }
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

        // Count relevant version blocks
        int blocksKept    = 0;
        int blocksDropped = 0;
        for (const auto& v : tbl.versions) {
            if (VersionRelevant(v)) ++blocksKept;
            else                    ++blocksDropped;
        }

        if (blocksKept == 0) {
            ++skipped;
            if (verbose) {
                std::cerr << "[skipped]  " << tableName << "\n";
            }
            continue;
        }

        // FilterDbdText uses the original (unfiltered) tbl so that versionIdx
        // aligns with LAYOUT lines in the raw text.
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

        if (debug) {
            for (size_t i = 0; i < tbl.versions.size(); ++i) {
                const auto& v    = tbl.versions[i];
                bool        kept = VersionRelevant(v);
                std::cerr << "    block " << i << ": ";
                if (!v.layoutHashes.empty()) {
                    std::cerr << "LAYOUT";
                    for (const auto& h : v.layoutHashes) std::cerr << " " << h;
                    std::cerr << "  ";
                }
                if (!v.builds.empty()) {
                    std::cerr << "builds=[";
                    for (size_t j = 0; j < v.builds.size(); ++j) {
                        if (j) std::cerr << ",";
                        std::cerr << v.builds[j];
                    }
                    std::cerr << "]  ";
                }
                if (!v.buildRanges.empty()) {
                    std::cerr << "ranges=[";
                    for (size_t j = 0; j < v.buildRanges.size(); ++j) {
                        if (j) std::cerr << ",";
                        std::cerr << v.buildRanges[j].first << "-" << v.buildRanges[j].second;
                    }
                    std::cerr << "]  ";
                }
                std::cerr << (kept ? "-> kept" : "-> dropped") << "\n";
            }
        }
    }

    // Sort entries by name for determinism
    std::sort(entries.begin(), entries.end(),
              [](const Entry& a, const Entry& b) { return a.name < b.name; });

    // -----------------------------------------------------------------------
    // FNV1a hash + collision detection
    // -----------------------------------------------------------------------
    std::vector<std::pair<uint32_t, std::string>> hashes;
    hashes.reserve(entries.size());
    for (const auto& e : entries) {
        uint32_t h = Fnv1a(ToLower(e.name));
        hashes.push_back({ h, e.name });
    }

    // Sort by hash and check adjacent pairs
    std::vector<std::pair<uint32_t, std::string>> sortedHashes = hashes;
    std::sort(sortedHashes.begin(), sortedHashes.end());
    for (size_t i = 1; i < sortedHashes.size(); ++i) {
        if (sortedHashes[i].first == sortedHashes[i - 1].first) {
            std::cerr << "FATAL: FNV1a hash collision between '"
                      << sortedHashes[i - 1].second << "' and '"
                      << sortedHashes[i].second << "'\n";
            return 2;
        }
    }

    // -----------------------------------------------------------------------
    // Build binary blob
    // -----------------------------------------------------------------------
    uint32_t tableCount = static_cast<uint32_t>(entries.size());

    // Pre-compute data offsets
    std::vector<uint32_t> offsets(tableCount);
    uint32_t dataOffset = 0;
    for (uint32_t i = 0; i < tableCount; ++i) {
        offsets[i] = dataOffset;
        dataOffset += static_cast<uint32_t>(entries[i].filtered.size());
    }

    // Header: magic(4) + version(4) + count(4) = 12 bytes
    // Index : count * 44 bytes
    // Data  : all filtered text concatenated
    std::vector<uint8_t> blob;
    blob.reserve(12 + tableCount * 44 + dataOffset);

    // Header
    PushU32(blob, BDBC_MAGIC);
    PushU32(blob, BDBC_VERSION);
    PushU32(blob, tableCount);

    // Index
    for (uint32_t i = 0; i < tableCount; ++i) {
        uint32_t hash = Fnv1a(ToLower(entries[i].name));
        PushU32(blob, hash);

        // 32-byte name field (null-terminated, max 31 characters)
        char nameBuf[32]{};
        std::strncpy(nameBuf, entries[i].name.c_str(), 31);
        for (char c : nameBuf) blob.push_back(static_cast<uint8_t>(c));

        PushU32(blob, offsets[i]);
        PushU32(blob, static_cast<uint32_t>(entries[i].filtered.size()));
    }

    // Data section
    for (const auto& e : entries) {
        for (char c : e.filtered) blob.push_back(static_cast<uint8_t>(c));
    }

    // -----------------------------------------------------------------------
    // Write header file
    // -----------------------------------------------------------------------

    // Create parent directories if needed
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

    // Write bytes, 16 per line
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
