#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace dbc {

struct KnownVersion {
    const char* versionString;  // e.g. "1.12.1"
    uint32_t    buildNumber;    // e.g. 5875
    const char* expansion;      // e.g. "Vanilla"
    const char* patchName;      // e.g. "Drums of War"
};

struct VersionAlias {
    const char* alias;          // e.g. "vanilla"
    const char* versionString;  // e.g. "1.12.1"
};

class VersionTable {
public:
    // Resolve any user input to a build number.
    // Accepts: "0.12.0.3988", "0.12.0", "3988", "alpha", "3.3.5.12340", "3.3.5", "12340", "wrath"
    // Returns nullopt if unrecognised or out of target range (> 12340).
    static std::optional<uint32_t> Resolve(const std::string& input);

    // List all known version strings (for --list-versions)
    static const std::vector<KnownVersion>& AllVersions();
};

}  // namespace dbc
