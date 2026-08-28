#pragma once

#include "common.h"

#include <cstring>
#include <ostream>

class RcString {
    u32 id;

public:
    RcString()
        : id(0)
    {
    }

    RcString(const char* s, size_t len);

    RcString(const char* s);

    explicit RcString(const ::std::string& s);

    static RcString newInterned(const char* s, size_t len) {
        return RcString(s, len);
    }

    static RcString newInterned(const ::std::string& s) {
        return RcString(s);
    }

    static RcString newInterned(const char* s) {
        return RcString(s);
    }

    const char* begin() const {
        return c_str();
    }

    const char* end() const {
        return c_str() + size();
    }

    size_t size() const;

    const char* c_str() const;

    char back() const;

    u64 contentHash() const;

    u32 rawId() const {
        return id;
    }

    Ordering ord(const char* s, size_t l) const;

    Ordering ord(const RcString& s) const;

    bool operator==(const RcString& s) const {
        return id == s.id;
    }

    bool operator!=(const RcString& s) const {
        return id != s.id;
    }

    bool operator<(const RcString& s) const {
        return this->ord(s) == OrdLess;
    }

    bool operator>(const RcString& s) const {
        return this->ord(s) == OrdGreater;
    }

    Ordering ord(const std::string& s) const {
        return ord(s.data(), s.size());
    }

    bool operator==(const std::string& s) const {
        return this->ord(s) == OrdEqual;
    }

    bool operator!=(const std::string& s) const {
        return this->ord(s) != OrdEqual;
    }

    bool operator<(const std::string& s) const {
        return this->ord(s) == OrdLess;
    }

    bool operator>(const std::string& s) const {
        return this->ord(s) == OrdGreater;
    }

    Ordering ord(const char* s) const;

    bool operator==(const char* s) const {
        return this->ord(s) == OrdEqual;
    }

    bool operator!=(const char* s) const {
        return this->ord(s) != OrdEqual;
    }

    friend ::std::ostream& operator<<(::std::ostream& os, const RcString& x);

    friend bool operator==(const char* a, const RcString& b) {
        return b == a;
    }

    friend bool operator!=(const char* a, const RcString& b) {
        return b != a;
    }

    int compare(size_t o, size_t l, const char* s) const;
};

static_assert(sizeof(RcString) == sizeof(u32));

namespace std {
    static inline bool operator==(const string& a, const ::RcString& b) {
        return b == a;
    }

    static inline bool operator!=(const string& a, const ::RcString& b) {
        return b != a;
    }

    template <>
    struct hash<RcString> {
        size_t operator()(const RcString& s) const noexcept {
            return s.rawId() * 0x9E3779B97F4A7C15ull;
        }
    };
}
