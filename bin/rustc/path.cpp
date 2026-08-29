/*
 * Generic representation of a filesystem path
 */
#include "path.h"
#include "compile_error.h"
#include <unistd.h>
#include <limits.h>
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

FsPath FsPath::toAbsolute() const {
    if (!this->isValid()) {
        compileErrorBugCheck("Calling to_absolute() on an invalid path");
    }

    if (this->str_[0] == SEP) {
        return *this;
    }

    char cwd[PATH_MAX];
    if (!getcwd(cwd, sizeof(cwd))) {
        throw std::runtime_error("Calling getcwd() failed in path::to_absolute()");
    }
    auto rv = FsPath(cwd);
    for (auto comp : *this) {
        if (comp == ".")
            ;
        else if (comp == "..") {
            rv.popComponent();
        } else {
            rv /= comp;
        }
    }
    return rv;
}

FsPath FsPath::normalise() const {
    FsPath rv;
    rv.str_.reserve(str_.size() + 1);

    for (auto comp : *this) {
        if (comp == ".") {
        } else if (comp == "..") {
            if (rv.str_.empty() || (rv.str_.size() == 3 && rv.str_[0] == '.' && rv.str_[1] == '.' && rv.str_[2] == SEP) || (rv.str_.size() > 4 && *(rv.str_.end() - 4) == SEP && *(rv.str_.end() - 3) == '.' && *(rv.str_.end() - 2) == '.' && *(rv.str_.end() - 1) == SEP)) {
                rv.str_ += comp;
                rv.str_ += SEP;
            } else {
                rv.str_.pop_back();
                auto pos = rv.str_.find_last_of(SEP);
                if (pos == std::string::npos) {
                    rv.str_.resize(0);
                } else if (pos == 0) {
                } else {
                    rv.str_.resize(pos + 1);
                }
            }
        } else {
            rv.str_ += comp;
            rv.str_ += SEP;
        }
    }
    rv.str_.pop_back();
    return rv;
}

void FsPath::ComponentsIter::operator++() {
    if (end == p.str_.size()) {
        pos = end;
    } else {
        pos = end + 1;
        end = p.str_.find(SEP, pos);
        if (end == std::string::npos) {
            end = p.str_.size();
        }
    }
}

FsPath::FsPath() {
}

FsPath::FsPath(const std::string& s)
    : FsPath(s.c_str())
{
}

FsPath& FsPath::operator/=(const FsPath& p) {
    if (!p.isValid()) {
        compileErrorBugCheck("Appending from an invalid path");
    }

    return *this /= p.str_.c_str();
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

FsPath& FsPath::operator/=(std::string_view o) {
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

FsPath FsPath::operator/(const FsPath& p) const {
    auto rv = *this;
    rv /= p;
    return rv;
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

bool FsPath::popComponent() {
    if (!this->isValid()) {
        compileErrorBugCheck("Calling pop_component() on an invalid path");
    }
    auto pos = str_.find_last_of(SEP);
    if (pos == std::string::npos || pos == 0) {
        return false;
    } else {
        this->str_.resize(pos);
        return true;
    }
}

FsPath FsPath::parent() const {
    if (!this->isValid()) {
        compileErrorBugCheck("Calling parent() on an invalid path");
    }
    auto pos = str_.find_last_of(SEP);
    if (pos == std::string::npos) {
        return FsPath();
    } else {
        FsPath rv;
        rv.str_ = str_.substr(0, pos);
        return rv;
    }
}

std::string FsPath::basename() const {
    if (!this->isValid()) {
        compileErrorBugCheck("Calling basename() on an invalid path");
    }

    auto pos = str_.find_last_of(SEP);
    if (pos == std::string::npos) {
        return str_;
    } else {
        return str_.substr(pos + 1);
    }
}

FsPath::ComponentsIter::ComponentsIter(const FsPath& p, size_t i)
    : p(p)
    , pos(i)
{
    end = p.str_.find(SEP, pos);
    if (end == std::string::npos) {
        end = p.str_.size();
    }
}

namespace stl {
template <>
void output<ZeroCopyOutput, FsPath>(ZeroCopyOutput& out, const FsPath& path) {
    out << path.str();
}
}
