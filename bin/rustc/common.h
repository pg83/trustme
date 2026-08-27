#pragma once

#include <std/sys/types.h>

#include <iostream>
#include <vector>
#include <map>
#include <set>
#include <cassert>
#include <sstream>
#include <memory>
#include <utility>

#define FMT(ss) (static_cast<::std::ostringstream&&>(::std::ostringstream() << ss).str())
// Project-wide shorthand retained for the pervasive move idiom.
#define mv$(...) ::std::move(__VA_ARGS__)
#define box$(...) ::makeUniquePtr(::std::move(__VA_ARGS__))
#define rcNew$(...) ::makeSharedPtr(::std::move(__VA_ARGS__))

#include "compile_error.h"

struct RepeatLitStr {
    const char* s;
    int n;

    friend ::std::ostream& operator<<(::std::ostream& os, const RepeatLitStr& r) {
        for (int i = 0; i < r.n; i++) {
            os << r.s;
        }
        return os;
    }
};

template <typename F>
struct FmtLambda {
    F f;

    explicit FmtLambda(F f)
        : f(f)
    {
    }

    void operator()(::std::ostream& os) const {
        f(os);
    }

    friend ::std::ostream& operator<<(::std::ostream& os, const FmtLambda& x) {
        x(os);
        return os;
    }
};

#define FMT_CB(os, ...)         \
    ::FmtLambda([&](auto& os) { \
        __VA_ARGS__;            \
    })

template <typename Y, typename X>
Y* cast(X* x) noexcept {
    if (x && x->nodeKind() == Y::kind) {
        return static_cast<Y*>(x);
    }
    return nullptr;
}

template <typename T>
::std::unique_ptr<T> makeUniquePtr(T&& v) {
    return ::std::unique_ptr<T>(new T(mv$(v)));
}

template <typename T>
::std::shared_ptr<T> makeSharedPtr(T&& v) {
    return ::std::shared_ptr<T>(new T(mv$(v)));
}

template <typename T>
::std::vector<T> makeVec1(T&& v) {
    ::std::vector<T> rv;
    rv.push_back(mv$(v));
    return rv;
}

template <typename T>
::std::vector<T> makeVec2(T v1, T v2) {
    ::std::vector<T> rv;
    rv.reserve(2);
    rv.push_back(mv$(v1));
    rv.push_back(mv$(v2));
    return rv;
}

template <typename T>
::std::vector<T> makeVec3(T v1, T v2, T v3) {
    ::std::vector<T> rv;
    rv.reserve(3);
    rv.push_back(mv$(v1));
    rv.push_back(mv$(v2));
    rv.push_back(mv$(v3));
    return rv;
}

enum Ordering {
    OrdLess = -1,
    OrdEqual,
    OrdGreater,
};

Ordering ord(bool l, bool r);

static inline Ordering ord(char l, char r) {
    return (l == r ? OrdEqual : (l > r ? OrdGreater : OrdLess));
}

static inline Ordering ord(unsigned char l, unsigned char r) {
    return (l == r ? OrdEqual : (l > r ? OrdGreater : OrdLess));
}

static inline Ordering ord(char8_t l, char8_t r) {
    return (l == r ? OrdEqual : (l > r ? OrdGreater : OrdLess));
}


static inline Ordering ord(unsigned short l, unsigned short r) {
    return (l == r ? OrdEqual : (l > r ? OrdGreater : OrdLess));
}

static inline Ordering ord(unsigned l, unsigned r) {
    return (l == r ? OrdEqual : (l > r ? OrdGreater : OrdLess));
}

static inline Ordering ord(unsigned long l, unsigned long r) {
    return (l == r ? OrdEqual : (l > r ? OrdGreater : OrdLess));
}

static inline Ordering ord(unsigned long long l, unsigned long long r) {
    return (l == r ? OrdEqual : (l > r ? OrdGreater : OrdLess));
}

static inline Ordering ord(signed char l, signed char r) {
    return (l == r ? OrdEqual : (l > r ? OrdGreater : OrdLess));
}

static inline Ordering ord(int l, int r) {
    return (l == r ? OrdEqual : (l > r ? OrdGreater : OrdLess));
}

static inline Ordering ord(short l, short r) {
    return (l == r ? OrdEqual : (l > r ? OrdGreater : OrdLess));
}

static inline Ordering ord(long l, long r) {
    return (l == r ? OrdEqual : (l > r ? OrdGreater : OrdLess));
}

static inline Ordering ord(long long l, long long r) {
    return (l == r ? OrdEqual : (l > r ? OrdGreater : OrdLess));
}

static inline Ordering ord(float l, float r) {
    return (l == r ? OrdEqual : (l > r ? OrdGreater : OrdLess));
}

static inline Ordering ord(double l, double r) {
    return (l == r ? OrdEqual : (l > r ? OrdGreater : OrdLess));
}

Ordering ord(const ::std::string& l, const ::std::string& r);

// Bridge a lambda into a per-interface callback adapter: T is the adapter
// template for one concrete callback interface (e.g. ExpandAttrCb), F the
// lambda. Keeps call sites on lambdas while the API takes a plain virtual
// interface reference, with no allocation.
template <template <typename> class T, typename F>
auto makeCallable(F f) {
    return T<F>(f);
}

class HIRTypeData;
// Interned types order by the interner-assigned uid (creation order), never
// by address: pointer order leaks the allocation layout into anything walked
// in container order, including the emitted output. Declared before the
// generic templates below so their qualified ::ord calls resolve to it.
Ordering ord(const HIRTypeData* l, const HIRTypeData* r);

template <typename T>
Ordering ord(T* const& l, T* const& r) {
    return l == r ? OrdEqual : (l > r ? OrdGreater : OrdLess);
}

template <typename T>
Ordering ord(const T& l, const T& r) {
    return l.ord(r);
}

template <typename T, typename U>
Ordering ord(const ::std::pair<T, U>& l, const ::std::pair<T, U>& r) {
    Ordering rv;
    rv = ::ord(l.first, r.first);
    if (rv != OrdEqual) {
        return rv;
    }
    rv = ::ord(l.second, r.second);
    return rv;
}

template <typename T>
Ordering ord(const ::std::vector<T>& l, const ::std::vector<T>& r) {
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

template <typename T, typename U>
Ordering ord(const ::std::map<T, U>& l, const ::std::map<T, U>& r) {
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

#define ORD(a, b)                      \
    do {                               \
        Ordering ORDRv = ::ord(a, b); \
        if (ORDRv != ::OrdEqual)      \
            return ORDRv;             \
    } while (0)

template <typename T>
struct LList {
    const LList* prev;
    T item{};

    LList()
        : prev(nullptr)
    {
    }

    LList(const LList* prev, T item)
        : prev(prev)
        , item(::std::move(item))
    {
    }

    LList end() const {
        return LList();
    }

    LList begin() const {
        return *this;
    }

    bool operator==(const LList& x) {
        return prev == x.prev;
    }

    bool operator!=(const LList& x) {
        return prev != x.prev;
    }

    void operator++() {
        assert(prev);
        *this = *prev;
    }

    const T& operator*() const {
        return item;
    }
};

template <typename T>
struct Join {
    const char* sep;
    const ::std::vector<T>& v;

    friend ::std::ostream& operator<<(::std::ostream& os, const Join& j) {
        if (j.v.size() > 0) {
            os << j.v[0];
        }
        for (unsigned int i = 1; i < j.v.size(); i++) {
            os << j.sep << j.v[i];
        }
        return os;
    }
};

template <typename T>
inline Join<T> join(const char* sep, const ::std::vector<T> v) {
    return Join<T>({sep, v});
}

namespace std {

    // The standard deletes streaming of char8_t; restore the pre-u8
    // byte-as-char behaviour (u8 used to be unsigned char, which streams as
    // a character). Lives in std so it is found ahead of the deleted
    // overload everywhere, including the container printers below.
    inline ::std::ostream& operator<<(::std::ostream& os, char8_t v) {
        return os << static_cast<char>(v);
    }

    template <typename T>
    inline auto operator<<(::std::ostream& os, const T& v) -> decltype(v.fmt(os)) {
        return v.fmt(os);
    }

    template <typename T>
    inline ::std::ostream& operator<<(::std::ostream& os, const ::std::vector<T*>& v) {
        if (v.size() > 0) {
            bool isFirst = true;
            for (const auto& i : v) {
                if (!isFirst) {
                    os << ", ";
                }
                isFirst = false;
                os << *i;
            }
        }
        return os;
    }

    template <typename T>
    inline ::std::ostream& operator<<(::std::ostream& os, const ::std::vector<T>& v) {
        if (v.size() > 0) {
            bool isFirst = true;
            for (const auto& i : v) {
                if (!isFirst) {
                    os << ", ";
                }
                isFirst = false;
                os << i;
            }
        }
        return os;
    }

    template <typename T>
    inline ::std::ostream& operator<<(::std::ostream& os, const ::std::set<T>& v) {
        if (v.size() > 0) {
            bool isFirst = true;
            for (const auto& i : v) {
                if (!isFirst) {
                    os << ", ";
                }
                isFirst = false;
                os << i;
            }
        }
        return os;
    }

    template <typename T, typename U>
    inline ::std::ostream& operator<<(::std::ostream& os, const ::std::pair<T, U>& v) {
        os << "(" << v.first << ", " << v.second << ")";
        return os;
    }

    template <typename T, typename U, class Cmp>
    inline ::std::ostream& operator<<(::std::ostream& os, const ::std::map<T, U, Cmp>& v) {
        if (v.size() > 0) {
            bool isFirst = true;
            for (const auto& i : v) {
                if (!isFirst) {
                    os << ", ";
                }
                isFirst = false;
                os << i.first << ": " << i.second;
            }
        }
        return os;
    }

    template <typename T, typename U, class Cmp>
    inline ::std::ostream& operator<<(::std::ostream& os, const ::std::multimap<T, U, Cmp>& v) {
        if (v.size() > 0) {
            bool isFirst = true;
            for (const auto& i : v) {
                if (!isFirst) {
                    os << ", ";
                }
                isFirst = false;
                os << i.first << ": " << i.second;
            }
        }
        return os;
    }

} // namespace std

class FmtEscaped {
    const char* s;
    const char* e;

public:
    FmtEscaped(const ::std::string& s);

    // See main.cpp
    friend ::std::ostream& operator<<(::std::ostream& os, const FmtEscaped& x);
};

// -------------------------------------------------------------------
// --- Reversed iterable
template <typename T>
struct reversionWrapper {
    T& iterable;
};

template <typename T>
//auto begin (reversion_wrapper<T> w) { return ::std::rbegin(w.iterable); }
auto begin(reversionWrapper<T> w) {
    return w.iterable.rbegin();
}

template <typename T>
//auto end (reversion_wrapper<T> w) { return ::std::rend(w.iterable); }
auto end(reversionWrapper<T> w) {
    return w.iterable.rend();
}

template <typename T>
reversionWrapper<T> reverse(T&& iterable) {
    return {iterable};
}

template <typename T>
struct RunIterable {
    const T& list;
    unsigned int ofs;
    ::std::pair<size_t, size_t> cur;

    RunIterable(const T& list)
        : list(list)
        , ofs(0)
    {
        advance();
    }

    void advance() {
        if (ofs < list.size()) {
            auto start = ofs;
            while (ofs < list.size() && list[ofs] == list[start]) {
                ofs++;
            }
            cur = ::std::make_pair(start, ofs - 1);
        } else {
            ofs = list.size() + 1;
        }
    }

    RunIterable<T> begin() {
        return *this;
    }

    RunIterable<T> end() {
        auto rv = *this;
        rv.ofs = list.size() + 1;
        return rv;
    }

    bool operator==(const RunIterable<T>& x) {
        return x.ofs == ofs;
    }

    bool operator!=(const RunIterable<T>& x) {
        return !(*this == x);
    }

    void operator++() {
        advance();
    }

    const ::std::pair<size_t, size_t>& operator*() const {
        return this->cur;
    }

    const ::std::pair<size_t, size_t>* operator->() const {
        return &this->cur;
    }
};

template <typename T>
RunIterable<T> runs(const T& x) {
    return RunIterable<T>(x);
}

template <typename T>
class NullOnDrop {
    T*& ptr;

public:
    NullOnDrop(T*& ptr)
        : ptr(ptr)
    {
    }

    ~NullOnDrop() {
        ptr = nullptr;
    }
};
