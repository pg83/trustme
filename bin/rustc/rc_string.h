#pragma once

#include <cstring>
#include <ostream>
#include "common.h"

class RcString {
    struct Inner {
        unsigned int refcount;
        unsigned int size;
        unsigned int ordering; // Populated only for interned strings, 0 otherwise
        unsigned int data[1];  // Actually arbitary
    }* ptr;

public:
    RcString();

    RcString(const char* s, size_t len);

    RcString(const char* s);

    explicit RcString(const ::std::string& s);

    static RcString newInterned(const char* s, size_t len);

    static RcString newInterned(const ::std::string& s) {
        return newInterned(s.data(), s.size());
    }

    static RcString newInterned(const char* s) {
        return newInterned(s, ::std::strlen(s));
    }

    RcString(const RcString& x);

    RcString(RcString&& x);

    ~RcString();

    RcString& operator=(const RcString& x);

    RcString& operator=(RcString&& x);

    const char* begin() const {
        return c_str();
    }

    const char* end() const {
        return c_str() + size();
    }

    bool isInterned() const {
        return ptr && ptr->ordering != 0;
    }

    size_t size() const {
        return ptr ? ptr->size : 0;
    }

    const char* c_str() const;

    char back() const;

    Ordering ord(const char* s, size_t l) const;
    Ordering ordInterned(const RcString& s) const;

    Ordering ord(const RcString& s) const;

    bool operator==(const RcString& s) const;

    bool operator!=(const RcString& s) const {
        return !(*this == s);
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

namespace std {
    static inline bool operator==(const string& a, const ::RcString& b) {
        return b == a;
    }

    static inline bool operator!=(const string& a, const ::RcString& b) {
        return b != a;
    }

    template <>
    struct hash<RcString> {
        size_t operator()(const RcString& s) const noexcept;
    };
}
