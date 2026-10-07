#include "target_version.h"

using namespace stl;

bool RustcVersion::parse(StringView text, RustcVersion& out) {
    size_t end = 0;
    while (end < text.length() && text[end] != '-') {
        end++;
    }
    u16 parts[3] = {0, 0, 0};
    size_t count = 0;
    size_t pos = 0;
    while (count < 3) {
        if (pos >= end || text[pos] < '0' || text[pos] > '9') {
            return false;
        }
        u32 value = 0;
        while (pos < end && text[pos] >= '0' && text[pos] <= '9') {
            value = value * 10 + static_cast<u32>(text[pos] - '0');
            if (value > 0xFFFF) {
                return false;
            }
            pos++;
        }
        parts[count++] = static_cast<u16>(value);
        if (pos == end) {
            break;
        }
        if (text[pos] != '.') {
            return false;
        }
        pos++;
    }
    if (count < 2 || pos != end) {
        return false;
    }
    out = RustcVersion{parts[0], parts[1], parts[2]};
    return true;
}
