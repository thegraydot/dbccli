#include "BuildVersion.h"

#include <stdexcept>
#include <string>

namespace dbc {

BuildVersion BuildVersion::FromString(const std::string& s) {
    BuildVersion v;
    // Expected format: "X.Y.Z.B"
    int parsed = std::sscanf(s.c_str(), "%hu.%hu.%hu.%u",
                             &v.expansion, &v.major, &v.minor, &v.build);
    if (parsed != 4) {
        throw std::invalid_argument("BuildVersion::FromString: invalid format: " + s);
    }
    return v;
}

}  // namespace dbc
