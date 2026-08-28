#pragma once

/*
 * Generic representation of a filesystem path (HEADER)
 */

#include <cstring>
#include <string>
#include <string_view>

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

    bool isAbsolute() const {
        return str_ != "" && str_[0] == '/';
    }

    bool operator==(const FsPath& p) const {
        return str_ == p.str_;
    }

    bool operator!=(const FsPath& p) const {
        return str_ != p.str_;
    }

    FsPath& operator/=(const FsPath& p);

    FsPath& operator/=(const char* o);

    FsPath& operator/=(std::string_view o);

    FsPath operator/(const FsPath& p) const;

    FsPath operator/(const char* o) const;

    FsPath operator+(const char* o) const;

    bool popComponent();

    FsPath parent() const;

    FsPath toAbsolute() const;

    std::string basename() const;

    const std::string& str() const {
        return str_;
    }

    operator std::string() const {
        return str_;
    }

    class ComponentsIter {
        const FsPath& p;
        size_t pos;
        size_t end;

        friend class FsPath;

        ComponentsIter(const FsPath& p, size_t i);

    public:
        std::string_view operator*() const {
            return std::string_view(p.str_.c_str() + pos, end - pos);
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
        return ComponentsIter(*this, str_.size());
    }

    FsPath normalise() const;

    friend std::ostream& operator<<(std::ostream& os, const FsPath& p) {
        return os << p.str_;
    }
};
