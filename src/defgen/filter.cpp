#include "filter.h"

#include <sstream>
#include <string>

using namespace dbc;

bool VersionRelevant(const VersionDefinition& v) {
    for (uint32_t b : v.builds) {
        if (b <= MAX_BUILD) return true;
    }
    for (auto& [lo, hi] : v.buildRanges) {
        if (lo <= MAX_BUILD) return true;
    }
    return false;
}

std::string FilterDbdText(const std::string& rawText, const TableDefinition& parsed) {
    std::istringstream ss(rawText);
    std::string line;

    std::string result;
    result.reserve(rawText.size());

    bool inColumns      = false;
    bool inVersionBlock = false;
    bool keepCurrent    = false;
    int  versionIdx     = -1;

    while (std::getline(ss, line)) {
        std::string trimmed = line;
        if (!trimmed.empty() && trimmed.back() == '\r') trimmed.pop_back();
        while (!trimmed.empty() && (trimmed.front() == ' ' || trimmed.front() == '\t'))
            trimmed = trimmed.substr(1);

        // Blank lines terminate the current version block. Emit one blank line
        // in the output so the downstream parser also sees the separator.
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
                // No LAYOUT preceded this BUILD, so it starts a build-only block.
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
