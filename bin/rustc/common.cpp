#include "common.h"

FmtEscaped::FmtEscaped(const ::std::string& s)
    : s(s.c_str())
    , e(s.c_str() + s.size()) {
}
