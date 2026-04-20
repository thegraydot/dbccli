#include "VersionTable.h"

#include <algorithm>
#include <cctype>
#include <iostream>
#include <stdexcept>
#include <string>

namespace dbc {

// Known version table - alpha (0.5.3-0.12.0) through WotLK 3.3.5a (<= 12340)
// Build numbers sourced from WoWDBDefs build range data.
static const KnownVersion KNOWN_VERSIONS[] = {
    // Pre-release Alpha (0.x)
    { "0.5.3",    3368,  "Alpha",   "Pre-release Alpha"         },
    { "0.5.5",    3494,  "Alpha",   "Pre-release Alpha"         },
    { "0.6.0",    3592,  "Alpha",   "Pre-release Alpha"         },
    { "0.7.0",    3694,  "Alpha",   "Pre-release Alpha"         },
    { "0.7.1",    3702,  "Alpha",   "Pre-release Alpha"         },
    { "0.7.6",    3712,  "Alpha",   "Pre-release Alpha"         },
    { "0.8.0",    3734,  "Alpha",   "Pre-release Alpha"         },
    { "0.9.0",    3807,  "Alpha",   "Pre-release Alpha"         },
    { "0.9.1",    3810,  "Alpha",   "Pre-release Alpha"         },
    { "0.10.0",   3892,  "Alpha",   "Pre-release Alpha"         },
    { "0.11.0",   3925,  "Alpha",   "Pre-release Alpha"         },
    { "0.12.0",   3988,  "Alpha",   "Pre-release Alpha"         },
    // Vanilla
    { "1.0.0",    3980,  "Vanilla", "World of Warcraft"         },
    { "1.0.1",    3989,  "Vanilla", "World of Warcraft"         },
    { "1.1.0",   4044,  "Vanilla", "The Drums of War"          },
    { "1.1.1",   4062,  "Vanilla", "The Drums of War"          },
    { "1.1.2",   4115,  "Vanilla", "The Drums of War"          },
    { "1.1.2",   4125,  "Vanilla", "The Drums of War"          },
    { "1.2.0",   4124,  "Vanilla", "Mysteries of Maraudon"     },
    { "1.2.1",   4125,  "Vanilla", "Mysteries of Maraudon"     },
    { "1.2.2",   4151,  "Vanilla", "Mysteries of Maraudon"     },
    { "1.2.3",   4159,  "Vanilla", "Mysteries of Maraudon"     },
    { "1.2.4",   4209,  "Vanilla", "Mysteries of Maraudon"     },
    { "1.3.0",   4297,  "Vanilla", "Ruins of the Dire Maul"    },
    { "1.3.1",   4312,  "Vanilla", "Ruins of the Dire Maul"    },
    { "1.3.2",   4298,  "Vanilla", "Ruins of the Dire Maul"    },
    { "1.3.3",   4346,  "Vanilla", "Ruins of the Dire Maul"    },
    { "1.4.0",   4375,  "Vanilla", "Call of the Crusade"       },
    { "1.4.1",   4364,  "Vanilla", "Call of the Crusade"       },
    { "1.4.2",   4375,  "Vanilla", "Call of the Crusade"       },
    { "1.4.3",   4413,  "Vanilla", "Call of the Crusade"       },
    { "1.5.0",   4442,  "Vanilla", "Forces of Corruption"      },
    { "1.5.1",   4500,  "Vanilla", "Forces of Corruption"      },
    { "1.6.0",   4544,  "Vanilla", "Assault on Blackwing Lair" },
    { "1.6.1",   4565,  "Vanilla", "Assault on Blackwing Lair" },
    { "1.6.2",   4565,  "Vanilla", "Assault on Blackwing Lair" },
    { "1.7.0",   4671,  "Vanilla", "Rise of the Blood God"     },
    { "1.7.1",   4703,  "Vanilla", "Rise of the Blood God"     },
    { "1.8.0",   4714,  "Vanilla", "Dragons of Nightmare"      },
    { "1.8.1",   4878,  "Vanilla", "Dragons of Nightmare"      },
    { "1.8.2",   4784,  "Vanilla", "Dragons of Nightmare"      },
    { "1.8.3",   4807,  "Vanilla", "Dragons of Nightmare"      },
    { "1.8.4",   4878,  "Vanilla", "Dragons of Nightmare"      },
    { "1.9.0",   5086,  "Vanilla", "The Gates of Ahn'Qiraj"    },
    { "1.9.1",   5181,  "Vanilla", "The Gates of Ahn'Qiraj"    },
    { "1.9.2",   5234,  "Vanilla", "The Gates of Ahn'Qiraj"    },
    { "1.9.3",   5261,  "Vanilla", "The Gates of Ahn'Qiraj"    },
    { "1.9.4",   5302,  "Vanilla", "The Gates of Ahn'Qiraj"    },
    { "1.10.0",  5302,  "Vanilla", "The Burning Steppes"       },
    { "1.10.1",  5386,  "Vanilla", "The Burning Steppes"       },
    { "1.10.2",  5302,  "Vanilla", "The Burning Steppes"       },
    { "1.11.0",  5344,  "Vanilla", "Shadow of the Necropolis"  },
    { "1.11.1",  5462,  "Vanilla", "Shadow of the Necropolis"  },
    { "1.11.2",  5464,  "Vanilla", "Shadow of the Necropolis"  },
    { "1.12.0",  5595,  "Vanilla", "Drums of War"              },
    { "1.12.1",  5875,  "Vanilla", "Drums of War"              },
    { "1.12.2",  6005,  "Vanilla", "Drums of War"              },
    { "1.12.3",  6141,  "Vanilla", "Drums of War"              },
    // The Burning Crusade
    { "2.0.1",   6180,  "TBC",     "Before the Storm"          },
    { "2.0.3",   6299,  "TBC",     "Before the Storm"          },
    { "2.0.6",   6337,  "TBC",     "Before the Storm"          },
    { "2.0.7",   6383,  "TBC",     "Before the Storm"          },
    { "2.0.10",  6448,  "TBC",     "Before the Storm"          },
    { "2.0.12",  6546,  "TBC",     "Before the Storm"          },
    { "2.1.0",   6692,  "TBC",     "The Black Temple"          },
    { "2.1.1",   6739,  "TBC",     "The Black Temple"          },
    { "2.1.2",   6803,  "TBC",     "The Black Temple"          },
    { "2.1.3",   6898,  "TBC",     "The Black Temple"          },
    { "2.2.0",   7272,  "TBC",     "The Gods of Zul'Aman"      },
    { "2.2.2",   7318,  "TBC",     "The Gods of Zul'Aman"      },
    { "2.2.3",   7359,  "TBC",     "The Gods of Zul'Aman"      },
    { "2.3.0",   7561,  "TBC",     "The Gods of Zul'Aman"      },
    { "2.3.2",   7741,  "TBC",     "The Gods of Zul'Aman"      },
    { "2.3.3",   7799,  "TBC",     "The Gods of Zul'Aman"      },
    { "2.4.0",   8089,  "TBC",     "Fury of the Sunwell"       },
    { "2.4.1",   8125,  "TBC",     "Fury of the Sunwell"       },
    { "2.4.2",   8209,  "TBC",     "Fury of the Sunwell"       },
    { "2.4.3",   8606,  "TBC",     "Fury of the Sunwell"       },
    // Wrath of the Lich King
    { "3.0.1",   8303,  "WotLK",   "Echoes of Doom"            },
    { "3.0.2",   9056,  "WotLK",   "Echoes of Doom"            },
    { "3.0.3",   9183,  "WotLK",   "Echoes of Doom"            },
    { "3.0.8",   9464,  "WotLK",   "Echoes of Doom"            },
    { "3.0.9",   9551,  "WotLK",   "Echoes of Doom"            },
    { "3.1.0",   9767,  "WotLK",   "Secrets of Ulduar"         },
    { "3.1.1",   9806,  "WotLK",   "Secrets of Ulduar"         },
    { "3.1.2",   9901,  "WotLK",   "Secrets of Ulduar"         },
    { "3.1.3",   9947,  "WotLK",   "Secrets of Ulduar"         },
    { "3.2.0",   10192, "WotLK",   "Trial of the Crusader"     },
    { "3.2.2",   10505, "WotLK",   "Trial of the Crusader"     },
    { "3.3.0",   10958, "WotLK",   "Fall of the Lich King"     },
    { "3.3.2",   11403, "WotLK",   "Fall of the Lich King"     },
    { "3.3.3",   11723, "WotLK",   "Fall of the Lich King"     },
    { "3.3.5",   12340, "WotLK",   "Fall of the Lich King"     },
};

static const VersionAlias ALIASES[] = {
    { "alpha",   "0.12.0" },
    { "vanilla", "1.12.1" },
    { "tbc",     "2.4.3"  },
    { "wrath",   "3.3.5"  },
};

// Helpers

static std::string ToLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return s;
}

static bool IsAllDigits(const std::string& s) {
    return !s.empty() &&
           std::all_of(s.begin(), s.end(),
                       [](unsigned char c) { return std::isdigit(c); });
}

// VersionTable implementation

static const std::vector<KnownVersion>& BuildVersionVector() {
    static std::vector<KnownVersion> v(
        std::begin(KNOWN_VERSIONS), std::end(KNOWN_VERSIONS));
    return v;
}

const std::vector<KnownVersion>& VersionTable::AllVersions() {
    return BuildVersionVector();
}

std::optional<uint32_t> VersionTable::Resolve(const std::string& input) {
    if (input.empty()) return std::nullopt;

    std::string work = input;

    // Step 1 - check aliases (case-insensitive)
    std::string workLower = ToLower(work);
    for (const auto& alias : ALIASES) {
        if (workLower == alias.alias) {
            work = alias.versionString;
            break;
        }
    }

    // Step 2 - bare integer build number
    if (IsAllDigits(work)) {
        uint32_t b = static_cast<uint32_t>(std::stoul(work));
        if (b <= 12340u) return b;
        return std::nullopt;
    }

    // Step 3 - count dots to distinguish formats
    size_t dots = std::count(work.begin(), work.end(), '.');

    if (dots == 3) {
        // "X.Y.Z.B" - extract B
        auto lastDot = work.rfind('.');
        std::string bStr = work.substr(lastDot + 1);
        if (IsAllDigits(bStr)) {
            uint32_t b = static_cast<uint32_t>(std::stoul(bStr));
            if (b <= 12340u) return b;
        }
        return std::nullopt;
    }

    if (dots == 2) {
        // "X.Y.Z" - strip any trailing non-digit suffix from last component
        // (e.g. "3.3.5a" -> "3.3.5") then look up in KNOWN_VERSIONS.
        auto lastDot = work.rfind('.');
        std::string lastComponent = work.substr(lastDot + 1);
        std::string stripped = lastComponent;
        while (!stripped.empty() && !std::isdigit(static_cast<unsigned char>(stripped.back()))) {
            stripped.pop_back();
        }
        std::string normalised = work;
        if (stripped != lastComponent) {
            normalised = work.substr(0, lastDot + 1) + stripped;
            std::cerr << "Warning: normalised version \"" << work
                      << "\" to \"" << normalised << "\"\n";
        }

        for (const auto& kv : KNOWN_VERSIONS) {
            if (normalised == kv.versionString) {
                return kv.buildNumber;
            }
        }
        return std::nullopt;
    }

    return std::nullopt;
}

}  // namespace dbc
