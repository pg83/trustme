#include "common.h"

FmtEscaped::FmtEscaped(const ::std::string& s)
    : s(s.c_str())
    , e(s.c_str() + s.size()) {
}

Ordering ord(bool l, bool r) {
    if (l == r) {
        return OrdEqual;
    } else if (l) {
        return OrdGreater;
    } else {
        return OrdLess;
    }
}
Ordering ord(const ::std::string& l, const ::std::string& r) {
    if (l == r) {
        return OrdEqual;
    } else if (l > r) {
        return OrdGreater;
    } else {
        return OrdLess;
    }
}
