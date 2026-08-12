/*
 * mrustc common code
 * - by John Hodge (Mutabah)
 *
 * tools/common/path.cpp
 * - Generic representation of a filesystem path
 */
#include "path.h"
#include <unistd.h> // getcwd/chdir
#include <limits.h> // PATH_MAX

helpers::path::path(const char* s)
    : mStr(s)
{
    // 1. Normalise path separators to the system specified separator
    for (size_t i = 0; i < mStr.size(); i++) {
        if (mStr[i] == '/' || mStr[i] == '\\') {
            mStr[i] = SEP;
        }
    }

    // 2. Remove any trailing separators
    if (!mStr.empty()) {
        while (!mStr.empty() && mStr.back() == SEP) {
            mStr.pop_back();
        }
        if (mStr.empty()) {
            mStr.push_back(SEP);
        }
    } else {
        throw ::std::runtime_error("Empty path being constructed");
    }
}

helpers::path helpers::path::to_absolute() const {
    if (!this->isValid()) {
        throw ::std::runtime_error("Calling to_absolute() on an invalid path");
    }

    if (this->mStr[0] == SEP) {
        return *this;
    }

    char cwd[PATH_MAX];
    if (!getcwd(cwd, sizeof(cwd))) {
        throw ::std::runtime_error("Calling getcwd() failed in path::to_absolute()");
    }
    auto rv = path(cwd);
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

helpers::path helpers::path::normalise() const {
    path rv;
    rv.mStr.reserve(mStr.size() + 1);

    for (auto comp : *this) {
        if (comp == ".") {
            // Ignore.
        } else if (comp == "..") {
            // If the path is empty, OR the last element is a "..", push the element
            if (rv.mStr.empty() || (rv.mStr.size() == 3 && rv.mStr[0] == '.' && rv.mStr[1] == '.' && rv.mStr[2] == SEP) || (rv.mStr.size() > 4 && *(rv.mStr.end() - 4) == SEP && *(rv.mStr.end() - 3) == '.' && *(rv.mStr.end() - 2) == '.' && *(rv.mStr.end() - 1) == SEP)) {
                // Push
                rv.mStr += comp;
                rv.mStr += SEP;
            } else {
                rv.mStr.pop_back();
                auto pos = rv.mStr.find_last_of(SEP);
                if (pos == ::std::string::npos) {
                    rv.mStr.resize(0);
                } else if (pos == 0) {
                    // Keep.
                } else {
                    rv.mStr.resize(pos + 1);
                }
            }
        } else {
            rv.mStr += comp;
            rv.mStr += SEP;
        }
    }
    rv.mStr.pop_back();
    return rv;
}


void helpers::path::ComponentsIter::operator++() {
    if (end == p.mStr.size()) {
        pos = end;
    } else {
        pos = end + 1;
        end = p.mStr.find(SEP, pos);
        if (end == ::std::string::npos) {
            end = p.mStr.size();
        }
    }
}

namespace helpers {

path::path() {
}
path::path(const ::std::string& s)
    : path(s.c_str()) {
}
path& path::operator/=(const path& p) {
    if (!p.isValid()) {
        throw ::std::runtime_error("Appending from an invalid path");
    }

    return *this /= p.mStr.c_str();
}
path& path::operator/=(const char* o) {
    if (!this->isValid()) {
        throw ::std::runtime_error("Appending to an invalid path");
    }
    if (o[0] == '/') {
        throw ::std::runtime_error("Appending an absolute path to another path");
    }
    this->mStr.push_back(SEP);
    this->mStr.append(o);
    return *this;
}
path& path::operator/=(::std::string_view o) {
    if (!this->isValid()) {
        throw ::std::runtime_error("Appending to an invalid path");
    }
    if (o[0] == '/') {
        throw ::std::runtime_error("Appending an absolute path to another path");
    }
    this->mStr.push_back(SEP);
    this->mStr.append(o);
    return *this;
}
path path::operator/(const path& p) const {
    auto rv = *this;
    rv /= p;
    return rv;
}
/// Append a relative path
path path::operator/(const char* o) const {
    auto rv = *this;
    rv /= o;
    return rv;
}
/// Add an arbitary string to the  component
path path::operator+(const char* o) const {
    if (!this->isValid()) {
        throw ::std::runtime_error("Appending a string to an invalid path");
    }
    if (::std::strchr(o, SEP) != nullptr) {
        throw ::std::runtime_error("Appending a string containing the path separator (with operator+)");
    }
    auto rv = *this;
    rv.mStr.append(o);
    return rv;
}
bool path::popComponent() {
    if (!this->isValid()) {
        throw ::std::runtime_error("Calling pop_component() on an invalid path");
    }
    auto pos = mStr.find_last_of(SEP);
    if (pos == ::std::string::npos || pos == 0) {
        return false;
    } else {
        this->mStr.resize(pos);
        return true;
    }
}
path path::parent() const {
    if (!this->isValid()) {
        throw ::std::runtime_error("Calling parent() on an invalid path");
    }
    auto pos = mStr.find_last_of(SEP);
    if (pos == ::std::string::npos) {
        return path();
    } else {
        path rv;
        rv.mStr = mStr.substr(0, pos);
        return rv;
    }
}
::std::string path::basename() const {
    if (!this->isValid()) {
        throw ::std::runtime_error("Calling basename() on an invalid path");
    }

    auto pos = mStr.find_last_of(SEP);
    if (pos == ::std::string::npos) {
        return mStr;
    } else {
        return mStr.substr(pos + 1);
    }
}
path::ComponentsIter::ComponentsIter(const path& p, size_t i)
    : p(p)
    , pos(i) {
    end = p.mStr.find(SEP, pos);
    if (end == ::std::string::npos) {
        end = p.mStr.size();
    }
}
}
