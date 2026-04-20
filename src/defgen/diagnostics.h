#pragma once

#include "../core/dbd/DbdStructures.h"

// Holds the computed size metrics for a single version block.
struct BlockSizeInfo {
    int fieldCount;     // total number of field entries in the block
    int fixedBytes;     // bytes from non-locstring fields
    int locstringCount; // number of locstring fields
};

// Computes the on-disk record size contribution for a version block.
// Fixed-size fields (int, float, string offset pointer) are counted directly.
// Locstring fields are counted separately because their total byte size depends
// on the number of locales stored in the actual .dbc file (L locales + 1 flags
// word per field), which is not known at definition time.
BlockSizeInfo ComputeBlockSize(const dbc::VersionDefinition& v);

// Warns when a build number that appears as a standalone BUILD entry in one
// version block also falls within a build range in a DIFFERENT block. This
// indicates overlapping coverage between blocks and is a data quality issue
// in the upstream .dbd definitions. Same-block redundancy (an explicit build
// listed alongside a range that already covers it) is harmless and ignored.
void CheckDuplicateBuilds(const dbc::TableDefinition& tbl);

// Prints verbose/debug information for all version blocks of a single parsed
// table. Shared by both the directory-walk path and the single-file path.
void PrintBlockInfo(const dbc::TableDefinition& tbl, bool verbose, bool debug);
