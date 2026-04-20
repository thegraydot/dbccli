#pragma once

#include "../core/blob/BlobReader.h"
#include "../core/dbd/DbdStructures.h"

#include <cstdint>
#include <string>

// Return codes for ExtractTable
inline constexpr int kExtractOk       = 0;
inline constexpr int kExtractError    = 1;
inline constexpr int kExtractNotFound = 2;  // DBC file not in directory - skip, not a failure
inline constexpr int kExtractMismatch = 3;  // definition/record-size mismatch

// Find the best VersionDefinition for a given build.
// fileFieldCount: from the DBC header; used for closest fallback and for
//                 correcting the build-matched block when its scalar count
//                 mismatches the file.
// closest:        if true, fall back to a field-count-compatible definition
//                 when no exact build/range match exists.
const dbc::VersionDefinition* FindVersionDef(const dbc::TableDefinition& tbl,
                                              uint32_t build,
                                              uint32_t fileFieldCount = 0,
                                              bool closest = false);

// Extract one table from a DBC source and write to outPath.
// dbcSource: file path or "-" for stdin (empty = search dbcDir)
// dbcDir:    directory to search for <tableName>.dbc
// format:    "csv" or "json"
// closest:   when true, fall back to field-count-compatible layout on build mismatch
int ExtractTable(dbc::BlobReader& blob,
                 uint32_t build,
                 const std::string& tableName,
                 const std::string& dbcSource,
                 const std::string& dbcDir,
                 const std::string& outPath,
                 const std::string& format,
                 bool closest = false);
