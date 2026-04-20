#include "diagnostics.h"
#include "filter.h"

#include <iomanip>
#include <iostream>
#include <set>
#include <string>

using namespace dbc;

BlockSizeInfo ComputeBlockSize(const VersionDefinition& v) {
    BlockSizeInfo info{0, 0, 0};
    info.fieldCount = static_cast<int>(v.fields.size());
    for (const auto& f : v.fields) {
        if (f.isLocString) {
            ++info.locstringCount;
        } else {
            info.fixedBytes += (f.sizeBits / 8) * f.arrayCount;
        }
    }
    return info;
}

void CheckDuplicateBuilds(const TableDefinition& tbl) {
    // Only warn when an exact build from one block falls within a range in a
    // DIFFERENT block. Same-block redundancy (e.g. BUILD 3368, 3494 alongside
    // BUILD 3368-3592 in the same block) is harmless and produces no warning.
    for (size_t i = 0; i < tbl.versions.size(); ++i) {
        for (uint32_t b : tbl.versions[i].builds) {
            for (size_t j = 0; j < tbl.versions.size(); ++j) {
                if (i == j) continue;
                for (const auto& [lo, hi] : tbl.versions[j].buildRanges) {
                    if (b >= lo && b <= hi) {
                        std::cerr << "Warning: " << tbl.name << " build " << b
                                  << " (block " << i << ") also covered by range "
                                  << lo << "-" << hi << " in block " << j << "\n";
                    }
                }
            }
        }
    }
}

void PrintBlockInfo(const TableDefinition& tbl, bool verbose, bool debug) {
    for (size_t i = 0; i < tbl.versions.size(); ++i) {
        const auto& v    = tbl.versions[i];
        bool        kept = VersionRelevant(v);

        // Full block header: LAYOUT hashes, exact builds, build ranges (debug only)
        if (debug) {
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

        if (!kept) continue;

        // Field count and expected record size (verbose and debug)
        if (verbose || debug) {
            BlockSizeInfo bsi = ComputeBlockSize(v);
            if (debug) {
                std::cerr << "      ";  // indent under the debug block header above
            } else {
                // Print block index plus a brief build/range summary so the
                // output can be cross-referenced against the raw .dbd file.
                std::cerr << "    block " << i;
                if (!v.builds.empty() || !v.buildRanges.empty()) {
                    std::cerr << " [";
                    bool first = true;
                    for (uint32_t b : v.builds) {
                        if (!first) std::cerr << ",";
                        std::cerr << b;
                        first = false;
                    }
                    for (const auto& [lo, hi] : v.buildRanges) {
                        if (!first) std::cerr << ",";
                        std::cerr << lo << "-" << hi;
                        first = false;
                    }
                    std::cerr << "]";
                }
                std::cerr << ": ";
            }
            std::cerr << bsi.fieldCount << " fields, ";
            if (bsi.locstringCount > 0)
                std::cerr << bsi.fixedBytes << "+" << bsi.locstringCount
                          << "x locstring bytes/record";
            else
                std::cerr << bsi.fixedBytes << " bytes/record";
            std::cerr << "\n";
        }

        // Full field list with type descriptor and unknown-column warnings (debug only)
        if (debug) {
            for (const auto& f : v.fields) {
                std::string typeDesc;
                if      (f.isFloat)     typeDesc = "float";
                else if (f.isLocString) typeDesc = "locstring";
                else if (f.isString)    typeDesc = "string";
                else                    typeDesc = (f.isSigned ? "int" : "uint")
                                                   + std::to_string(f.sizeBits);
                std::cerr << "      " << std::left << std::setw(32) << f.name
                          << "  type=" << std::left << std::setw(12) << typeDesc
                          << "  size=" << f.sizeBits << "b";
                if (f.arrayCount > 1)
                    std::cerr << "[" << f.arrayCount << "]";
                std::cerr << "\n";
                if (tbl.columns.find(f.name) == tbl.columns.end())
                    std::cerr << "      Warning: field '" << f.name
                              << "' not found in COLUMNS\n";
            }
        }
    }
}
