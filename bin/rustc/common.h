#pragma once

#include "output.h"

#include <std/sys/types.h>
#include <std/lib/vector.h>
#include <std/str/builder.h>

#include <map>
#include <set>
#include <memory>
#include <vector>
#include <utility>

#define FMT(ss)                                                                       \
    ([&] {                                                                            \
        ::stl::StringBuilder fmtOut;                                                  \
        fmtOut << ss;                                                                 \
        return std::string(static_cast<const char*>(fmtOut.data()), fmtOut.length()); \
    }())

#define mv$(...) std::move(__VA_ARGS__)
#define box$(...) ::makeUniquePtr(std::move(__VA_ARGS__))
#define rcNew$(...) ::makeSharedPtr(std::move(__VA_ARGS__))

#include "trace.h"
#include "compile_error.h"

struct RepeatLitStr {
    const char* s;
    int n;
};

#define FMT_CB(os, ...)                                                       \
    ([&] {                                                                    \
        ::stl::StringBuilder os;                                              \
        __VA_ARGS__;                                                          \
        return std::string(static_cast<const char*>(os.data()), os.length()); \
    }())

template <typename Y, typename X>
Y* cast(X* x) noexcept {
    if (x && x->nodeKind() == Y::kind) {
        return static_cast<Y*>(x);
    }
    return nullptr;
}

template <typename T>
std::unique_ptr<T> makeUniquePtr(T&& v) {
    return std::unique_ptr<T>(new T(mv$(v)));
}

template <typename T>
std::shared_ptr<T> makeSharedPtr(T&& v) {
    return std::shared_ptr<T>(new T(mv$(v)));
}

template <typename T>
std::vector<T> makeVec1(T&& v) {
    std::vector<T> rv;
    rv.push_back(mv$(v));
    return rv;
}

template <typename T>
std::vector<T> makeVec2(T v1, T v2) {
    std::vector<T> rv;
    rv.reserve(2);
    rv.push_back(mv$(v1));
    rv.push_back(mv$(v2));
    return rv;
}

template <typename T>
std::vector<T> makeVec3(T v1, T v2, T v3) {
    std::vector<T> rv;
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

Ordering ord(const std::string& l, const std::string& r);

template <template <typename> class T, typename F>
auto makeCallable(F f) {
    return T<F>(f);
}

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
        , item(std::move(item))
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
        BUG_ASSERT(prev);
        *this = *prev;
    }

    const T& operator*() const {
        return item;
    }
};

class FmtEscaped {
    const char* s;
    const char* e;

public:
    FmtEscaped(const std::string& s);

    const char* begin() const {
        return s;
    }

    const char* end() const {
        return e;
    }
};

template <typename T>
struct reversionWrapper {
    T& iterable;
};

template <typename T>

auto begin(reversionWrapper<T> w) {
    return w.iterable.rbegin();
}

template <typename T>

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
    std::pair<size_t, size_t> cur;

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
            cur = std::make_pair(start, ofs - 1);
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

    const std::pair<size_t, size_t>& operator*() const {
        return this->cur;
    }

    const std::pair<size_t, size_t>* operator->() const {
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
        DEBUG(stl::StringView("NULL ") << static_cast<const void*>(&ptr));
        ptr = nullptr;
    }
};
