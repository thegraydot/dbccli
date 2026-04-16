#pragma once

#include <cstdint>
#include <string>

namespace dbc {

struct BuildVersion {
    uint16_t expansion = 0;
    uint16_t major     = 0;
    uint16_t minor     = 0;
    uint32_t build     = 0;

    // Parse from "3.3.5.12340"
    static BuildVersion FromString(const std::string& s);

    // Extract just the build number
    uint32_t BuildNumber() const { return build; }

    bool operator==(const BuildVersion& o) const { return build == o.build; }
    bool operator<(const BuildVersion& o)  const { return build <  o.build; }
    bool operator>(const BuildVersion& o)  const { return build >  o.build; }
};

}  // namespace dbc
