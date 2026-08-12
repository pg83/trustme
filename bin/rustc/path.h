#pragma once

/*
 * mrustc common code
 * - by John Hodge (Mutabah)
 *
 * tools/common/path.h
 * - Generic representation of a filesystem path (HEADER)
 */

#include <cstring>
#include <string>
#include <string_view>
#include <stdexcept>

/// Path helper class (because I don't want to include boost)
class FsPath {
    static const char SEP = '/';

    ::std::string mStr;

public:
    FsPath();

    FsPath(const ::std::string& s);

    FsPath(const char* s);

    bool isValid() const {
        return mStr != "";
    }

    bool isAbsolute() const {
        return mStr != "" && mStr[0] == '/';
    }

    bool operator==(const FsPath& p) const {
        return mStr == p.mStr;
    }

    bool operator!=(const FsPath& p) const {
        return mStr != p.mStr;
    }

    FsPath& operator/=(const FsPath& p);

    FsPath& operator/=(const char* o);

    FsPath& operator/=(::std::string_view o);

    FsPath operator/(const FsPath& p) const;

    /// Append a relative path
    FsPath operator/(const char* o) const;

    /// Add an arbitary string to the final component
    FsPath operator+(const char* o) const;

    bool popComponent();

    FsPath parent() const;

    FsPath toAbsolute() const;

    ::std::string basename() const;

    const ::std::string& str() const {
        return mStr;
    }

    operator ::std::string() const {
        return mStr;
    }

    class ComponentsIter {
        const FsPath& p;
        size_t pos;
        size_t end;

        friend class FsPath;

        ComponentsIter(const FsPath& p, size_t i);

    public:
        ::std::string_view operator*() const {
            return ::std::string_view(p.mStr.c_str() + pos, end - pos);
        }

        void operator++();

        bool operator!=(const ComponentsIter& x) const {
            return pos != x.pos;
        }
    };

    ComponentsIter begin() const {
        return ComponentsIter(*this, 0);
    }

    ComponentsIter end() const {
        return ComponentsIter(*this, mStr.size());
    }

    FsPath normalise() const;


    friend ::std::ostream& operator<<(::std::ostream& os, const FsPath& p) {
        return os << p.mStr;
    }
};
