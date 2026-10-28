#pragma once

#include "output.h"

/*
 * Generic representation of a filesystem path (HEADER)
 */

#include <cstring>
#include <string>

class FsPath {
    static const char SEP = '/';

    std::string str_;

public:
    FsPath();

    FsPath(const std::string& s);

    FsPath(const char* s);

    bool isValid() const {
        return str_ != "";
    }

    FsPath& operator/=(const char* o);

    FsPath operator/(const char* o) const;

    FsPath operator+(const char* o) const;

    const std::string& str() const {
        return str_;
    }

    operator std::string() const {
        return str_;
    }

};
