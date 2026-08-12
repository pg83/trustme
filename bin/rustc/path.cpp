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

FsPath::FsPath(const char* s)
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

FsPath FsPath::toAbsolute() const {
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


void FsPath::ComponentsIter::operator++() {
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


FsPath::FsPath() {
}
FsPath::FsPath(const ::std::string& s)
    : FsPath(s.c_str()) {
}
FsPath& FsPath::operator/=(const FsPath& p) {
    if (!p.isValid()) {
        throw ::std::runtime_error("Appending from an invalid path");
    }

    return *this /= p.mStr.c_str();
}
FsPath& FsPath::operator/=(const char* o) {
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
FsPath& FsPath::operator/=(::std::string_view o) {
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
FsPath FsPath::operator/(const FsPath& p) const {
    auto rv = *this;
    rv /= p;
    return rv;
}
/// Append a relative path
FsPath FsPath::operator/(const char* o) const {
    auto rv = *this;
    rv /= o;
    return rv;
}
/// Add an arbitary string to the  component
FsPath FsPath::operator+(const char* o) const {
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
bool FsPath::popComponent() {
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
FsPath FsPath::parent() const {
    if (!this->isValid()) {
        throw ::std::runtime_error("Calling parent() on an invalid path");
    }
    auto pos = mStr.find_last_of(SEP);
    if (pos == ::std::string::npos) {
        return FsPath();
    } else {
        FsPath rv;
        rv.mStr = mStr.substr(0, pos);
        return rv;
    }
}
::std::string FsPath::basename() const {
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
FsPath::ComponentsIter::ComponentsIter(const FsPath& p, size_t i)
    : p(p)
    , pos(i) {
    end = p.mStr.find(SEP, pos);
    if (end == ::std::string::npos) {
        end = p.mStr.size();
    }
}
