#include "ord.h"

#include "output.h"

using namespace stl;

template <>
void stl::output<ZeroCopyOutput, Ordering>(ZeroCopyOutput& out, Ordering value) {
    out << static_cast<int>(value);
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

Ordering ord(const std::string& l, const std::string& r) {
    if (l == r) {
        return OrdEqual;
    } else if (l > r) {
        return OrdGreater;
    } else {
        return OrdLess;
    }
}
