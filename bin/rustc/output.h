#pragma once

#include <std/ios/manip.h>
#include <std/ios/sys.h>
#include <std/ios/out_zc.h>
#include <std/str/view.h>

#include <string_view>

template <typename T>
inline void outCont(stl::ZeroCopyOutput& out, const T& values) {
    bool first = true;
    for (const auto& value : values) {
        if (!first) {
            out << stl::StringView(", ");
        }
        first = false;
        out << value;
    }
}

struct FormattedHex {
    u64 value;
    unsigned width;
    bool uppercase;
};

FormattedHex formatHex(u64 value, unsigned width = 0, bool uppercase = false);
