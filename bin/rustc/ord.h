#pragma once

#include <std/lib/vector.h>

#include <map>
#include <string>
#include <utility>
#include <vector>

enum Ordering {
    OrdLess = -1,
    OrdEqual,
    OrdGreater,
};

Ordering ord(bool l, bool r);

inline Ordering ord(char l, char r) {
    return l == r ? OrdEqual : (l > r ? OrdGreater : OrdLess);
}

inline Ordering ord(unsigned char l, unsigned char r) {
    return l == r ? OrdEqual : (l > r ? OrdGreater : OrdLess);
}

inline Ordering ord(char8_t l, char8_t r) {
    return l == r ? OrdEqual : (l > r ? OrdGreater : OrdLess);
}

inline Ordering ord(unsigned short l, unsigned short r) {
    return l == r ? OrdEqual : (l > r ? OrdGreater : OrdLess);
}

inline Ordering ord(unsigned l, unsigned r) {
    return l == r ? OrdEqual : (l > r ? OrdGreater : OrdLess);
}

inline Ordering ord(unsigned long l, unsigned long r) {
    return l == r ? OrdEqual : (l > r ? OrdGreater : OrdLess);
}

inline Ordering ord(unsigned long long l, unsigned long long r) {
    return l == r ? OrdEqual : (l > r ? OrdGreater : OrdLess);
}

inline Ordering ord(signed char l, signed char r) {
    return l == r ? OrdEqual : (l > r ? OrdGreater : OrdLess);
}

inline Ordering ord(int l, int r) {
    return l == r ? OrdEqual : (l > r ? OrdGreater : OrdLess);
}

inline Ordering ord(short l, short r) {
    return l == r ? OrdEqual : (l > r ? OrdGreater : OrdLess);
}

inline Ordering ord(long l, long r) {
    return l == r ? OrdEqual : (l > r ? OrdGreater : OrdLess);
}

inline Ordering ord(long long l, long long r) {
    return l == r ? OrdEqual : (l > r ? OrdGreater : OrdLess);
}

inline Ordering ord(float l, float r) {
    return l == r ? OrdEqual : (l > r ? OrdGreater : OrdLess);
}

inline Ordering ord(double l, double r) {
    return l == r ? OrdEqual : (l > r ? OrdGreater : OrdLess);
}

Ordering ord(const std::string& l, const std::string& r);

class HIRType;

Ordering ord(const HIRType* l, const HIRType* r);

template <typename T>
Ordering ord(T* const& l, T* const& r) {
    return l == r ? OrdEqual : (l > r ? OrdGreater : OrdLess);
}

template <typename T>
Ordering ord(const T& l, const T& r) {
    return l.ord(r);
}

template <typename T, typename U>
Ordering ord(const std::pair<T, U>& l, const std::pair<T, U>& r) {
    Ordering rv;
    rv = ::ord(l.first, r.first);
    if (rv != OrdEqual) {
        return rv;
    }
    rv = ::ord(l.second, r.second);
    return rv;
}

template <typename T>
Ordering ord(const std::vector<T>& l, const std::vector<T>& r) {
    unsigned int i = 0;
    for (const auto& it : l) {
        if (i >= r.size()) {
            return OrdGreater;
        }

        auto rv = ::ord(it, r[i]);
        if (rv != OrdEqual) {
            return rv;
        }

        i++;
    }

    if (i < r.size()) {
        return OrdLess;
    }
    return OrdEqual;
}

template <typename T>
Ordering ord(const stl::Vector<T>& l, const stl::Vector<T>& r) {
    size_t i = 0;
    for (const auto& item : l) {
        if (i >= r.length()) {
            return OrdGreater;
        }

        auto result = ::ord(item, r[i]);
        if (result != OrdEqual) {
            return result;
        }

        ++i;
    }

    return i < r.length() ? OrdLess : OrdEqual;
}

template <typename T, typename U>
Ordering ord(const std::map<T, U>& l, const std::map<T, U>& r) {
    auto rIt = r.begin();
    for (const auto& le : l) {
        if (rIt == r.end()) {
            return OrdGreater;
        }
        auto rv = ::ord(le, *rIt);
        if (rv != OrdEqual) {
            return rv;
        }
        ++rIt;
    }
    return OrdEqual;
}

#define ORD(a, b)                     \
    do {                              \
        Ordering ORDRv = ::ord(a, b); \
        if (ORDRv != ::OrdEqual)      \
            return ORDRv;             \
    } while (0)
