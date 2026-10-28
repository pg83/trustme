/*
 * Generic representation of a filesystem path
 */
#include "path.h"
#include "compile_error.h"
#include <stdexcept>

using namespace stl;

FsPath::FsPath(const char* s)
    : str_(s)
{
    for (size_t i = 0; i < str_.size(); i++) {
        if (str_[i] == '/' || str_[i] == '\\') {
            str_[i] = SEP;
        }
    }

    if (!str_.empty()) {
        while (!str_.empty() && str_.back() == SEP) {
            str_.pop_back();
        }
        if (str_.empty()) {
            str_.push_back(SEP);
        }
    } else {
        throw std::runtime_error("Empty path being constructed");
    }
}

FsPath::FsPath() {
}

FsPath::FsPath(const std::string& s)
    : FsPath(s.c_str())
{
}

FsPath& FsPath::operator/=(const char* o) {
    if (!this->isValid()) {
        compileErrorBugCheck("Appending to an invalid path");
    }
    if (o[0] == '/') {
        compileErrorBugCheck("Appending an absolute path to another path");
    }
    this->str_.push_back(SEP);
    this->str_.append(o);
    return *this;
}

FsPath FsPath::operator/(const char* o) const {
    auto rv = *this;
    rv /= o;
    return rv;
}

FsPath FsPath::operator+(const char* o) const {
    if (!this->isValid()) {
        compileErrorBugCheck("Appending a string to an invalid path");
    }
    if (std::strchr(o, SEP) != nullptr) {
        compileErrorBugCheck("Appending a string containing the path separator (with operator+)");
    }
    auto rv = *this;
    rv.str_.append(o);
    return rv;
}

template <>
void stl::output<ZeroCopyOutput, FsPath>(ZeroCopyOutput& out, const FsPath& path) {
    out << path.str();
}
