#pragma once

#include "../core/dbd/DbdStructures.h"

#include <cstdint>
#include <string>

// The highest WoW build number this tool targets. Vanilla 1.12.1 = 5875,
// TBC 2.4.3 = 8606, WotLK 3.3.5 = 12340. Any version block whose builds
// all exceed this value is excluded from the generated output.
static constexpr uint32_t MAX_BUILD = 12340u;

// Returns true if a VersionDefinition is relevant for our build range.
// A block is relevant if it contains at least one exact build number or
// the lower end of a build range that is <= MAX_BUILD.
bool VersionRelevant(const dbc::VersionDefinition& v);

// Produces a filtered copy of a raw .dbd file, keeping only the COLUMNS
// section and the version blocks that are relevant for our build range.
//
// The function re-parses the raw text line by line rather than serialising
// the already-parsed TableDefinition back to text. This ensures the output
// is byte-for-byte identical to the original for the retained blocks, which
// matters for the downstream blob reader that re-parses this text at runtime.
//
// versionIdx is incremented each time a new version block starts (LAYOUT
// keyword, or a BUILD keyword that is not already inside a block). It must
// stay in sync with the version objects in `parsed.versions` so that the
// VersionRelevant() call refers to the correct object.
std::string FilterDbdText(const std::string& rawText, const dbc::TableDefinition& parsed);
