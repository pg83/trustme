#include "rc_string.h"

#include "output.h"

#include <string>
#include <cstring>
#include <algorithm>

#define XXH_INLINE_ALL
#include <xxhash.h>

#include <std/lib/vector.h>
#include <std/mem/obj_pool.h>

using namespace stl;

namespace {
    struct InternedString {
        const char* begin;
        const char* end;
        u64 hash1;
        u64 hash2;
    };

    struct StrInterner {
        ObjPool::Ref poolRef = ObjPool::fromMemory();
        ObjPool* pool = poolRef.mutPtr();
        Vector<InternedString> strs;
        Vector<u32> slots;
        size_t mask;
        size_t used = 0;

        StrInterner();

        void grow();

        u32 intern(const char* s, size_t len);
    };

    StrInterner& interner() {
        static StrInterner in;
        return in;
    }

    const InternedString& ent(u32 id) {
        return interner().strs[id];
    }
}

RcString::RcString(const char* s, size_t len)
    : id(interner().intern(s, len))
{
}

RcString::RcString(const char* s)
    : RcString(s, std::strlen(s))
{
}

RcString::RcString(const std::string& s)
    : RcString(s.data(), s.size())
{
}

size_t RcString::size() const {
    const auto& e = ent(id);
    return e.end - e.begin;
}

const char* RcString::c_str() const {
    return ent(id).begin;
}

char RcString::back() const {
    BUG_ASSERT(size() > 0);
    return *(ent(id).end - 1);
}

u64 RcString::contentHash() const {
    return ent(id).hash1;
}

Ordering RcString::ord(const char* s, size_t len) const {
    auto cmpLen = std::min(len, this->size());
    if (cmpLen > 0) {
        int cmp = memcmp(this->c_str(), s, cmpLen);
        if (cmp != 0) {
            return ::ord(cmp, 0);
        }
    }
    return ::ord(this->size(), len);
}

Ordering RcString::ord(const RcString& s) const {
    if (id == s.id) {
        return OrdEqual;
    }
    const auto& b = ent(s.id);
    return ord(b.begin, b.end - b.begin);
}

Ordering RcString::ord(const char* s) const {
    if (id == 0) {
        return (*s == '\0' ? OrdEqual : OrdLess);
    }

    int cmp = strncmp(this->c_str(), s, this->size());
    if (cmp == 0) {
        if (s[this->size()] == '\0') {
            return OrdEqual;
        } else {
            return OrdLess;
        }
    }
    return ::ord(cmp, 0);
}

int RcString::compare(size_t o, size_t l, const char* s) const {
    BUG_ASSERT(o <= this->size());
    if (l <= this->size() - o) {
        return memcmp(this->c_str() + o, s, l);
    } else {
        if (int rv = memcmp(this->c_str() + o, s, this->size() - o)) {
            return rv;
        }
        return -1;
    }
}

StrInterner::StrInterner() {
    auto* empty = static_cast<char*>(pool->allocate(1));
    empty[0] = '\0';
    strs.pushBack(InternedString{empty, empty, 0, 0});

    const size_t initial = 1 << 19;
    slots.zero(initial);
    mask = initial - 1;
}

auto StrInterner::grow() -> void {
    Vector<u32> next;
    next.zero((mask + 1) * 2);
    const size_t nextMask = (mask + 1) * 2 - 1;
    for (u32 id = 1; id < strs.length(); id++) {
        size_t i = strs[id].hash1 & nextMask;
        while (next[i]) {
            i = (i + 1) & nextMask;
        }
        next.mut(i) = id;
    }
    slots.xchg(next);
    mask = nextMask;
}

auto StrInterner::intern(const char* s, size_t len) -> u32 {
    if (len == 0) {
        return 0;
    }
    const auto h = XXH3_128bits(s, len);
    size_t i = h.high64 & mask;
    while (u32 id = slots[i]) {
        const auto& e = strs[id];
        if (e.hash1 == h.high64 && e.hash2 == h.low64) {
            return id;
        }
        i = (i + 1) & mask;
    }

    auto* data = static_cast<char*>(pool->allocate(len + 1));
    std::memcpy(data, s, len);
    data[len] = '\0';

    const auto id = static_cast<u32>(strs.length());
    strs.pushBack(InternedString{data, data + len, h.high64, h.low64});
    slots.mut(i) = id;
    used += 1;
    if (used * 10 > (mask + 1) * 7) {
        grow();
    }
    return id;
}

template <>
void stl::output<ZeroCopyOutput, RcString>(ZeroCopyOutput& os, RcString x) {
    os.write(x.c_str(), x.size());
    return;
}

template <>
void stl::output<ZeroCopyOutput, std::vector<RcString>>(ZeroCopyOutput& out, const std::vector<RcString>& values) {
    outCont(out, values);
}

template <>
void stl::output<ZeroCopyOutput, std::pair<const RcString, RcString>>(ZeroCopyOutput& out, std::pair<const RcString, RcString> value) {
    out << value.first << StringView(": ") << value.second;
}

template <>
void stl::output<ZeroCopyOutput, std::map<RcString, RcString>>(ZeroCopyOutput& out, const std::map<RcString, RcString>& values) {
    outCont(out, values);
}
