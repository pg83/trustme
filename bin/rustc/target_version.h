#pragma once

#include <std/str/view.h>
#include <std/sys/types.h>

struct RustcVersion {
    u16 major = 0;
    u16 minor = 0;
    u16 patch = 0;

    static bool parse(stl::StringView text, RustcVersion& out);

    bool operator<(const RustcVersion& other) const {
        if (major != other.major) {
            return major < other.major;
        }
        if (minor != other.minor) {
            return minor < other.minor;
        }
        return patch < other.patch;
    }
};
