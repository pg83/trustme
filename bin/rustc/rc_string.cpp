#include "rc_string.h"

#include <cstring>
#include <string>
#include <algorithm> // std::min

#define XXH_INLINE_ALL
#include <xxhash.h>

#include <std/lib/vector.h>
#include <std/mem/obj_pool.h>

// The string interner. Every constructed string lives here forever:
// zero-terminated bytes in the interner's ObjPool, one InternedString
// entry per unique content, and an open-addressing slot table keyed by
// the first xxh128 half with the second half as the whole equality check
// (a full 128-bit collision is the accepted, negligible failure mode).
namespace {
    struct InternedString {
        const char* begin;
        const char* end;
        uint64_t hash1;
        uint64_t hash2;
    };

    struct StrInterner {
        stl::ObjPool::Ref poolRef = stl::ObjPool::fromMemory();
        stl::ObjPool* pool = poolRef.mutPtr();
        stl::Vector<InternedString> strs;
        stl::Vector<uint32_t> slots; // values are ids; 0 = empty slot
        size_t mask;
        size_t used = 0;

        StrInterner() {
            auto* empty = static_cast<char*>(pool->allocate(1));
            empty[0] = '\0';
            strs.pushBack(InternedString{empty, empty, 0, 0});

            const size_t initial = 1 << 19; // libcargo peaks at ~129k uniques
            slots.zero(initial);
            mask = initial - 1;
        }

        void grow() {
            stl::Vector<uint32_t> next;
            next.zero((mask + 1) * 2);
            const size_t nextMask = (mask + 1) * 2 - 1;
            for (uint32_t id = 1; id < strs.length(); id++) {
                size_t i = strs[id].hash1 & nextMask;
                while (next[i]) {
                    i = (i + 1) & nextMask;
                }
                next.mut(i) = id;
            }
            slots.xchg(next);
            mask = nextMask;
        }

        uint32_t intern(const char* s, size_t len) {
            if (len == 0) {
                return 0;
            }
            const auto h = XXH3_128bits(s, len);
            size_t i = h.high64 & mask;
            while (uint32_t id = slots[i]) {
                const auto& e = strs[id];
                if (e.hash1 == h.high64 && e.hash2 == h.low64) {
                    return id;
                }
                i = (i + 1) & mask;
            }

            auto* data = static_cast<char*>(pool->allocate(len + 1));
            ::std::memcpy(data, s, len);
            data[len] = '\0';

            const auto id = static_cast<uint32_t>(strs.length());
            strs.pushBack(InternedString{data, data + len, h.high64, h.low64});
            slots.mut(i) = id;
            used += 1;
            if (used * 10 > (mask + 1) * 7) {
                grow();
            }
            return id;
        }
    };

    StrInterner& interner() {
        static StrInterner in;
        return in;
    }

    const InternedString& ent(uint32_t id) {
        return interner().strs[id];
    }
}

RcString::RcString(const char* s, size_t len)
    : id(interner().intern(s, len))
{
}

RcString::RcString(const char* s)
    : RcString(s, ::std::strlen(s))
{
}

RcString::RcString(const ::std::string& s)
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
    assert(size() > 0);
    return *(ent(id).end - 1);
}

uint64_t RcString::contentHash() const {
    return ent(id).hash1;
}

Ordering RcString::ord(const char* s, size_t len) const {
    auto cmpLen = ::std::min(len, this->size());
    if (cmpLen > 0) {
        int cmp = memcmp(this->c_str(), s, cmpLen);
        if (cmp != 0) {
            return ::ord(cmp, 0);
        }
    }
    // Since the prefix is equal, then sort `this` before `s` if it's shorter
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

::std::ostream& operator<<(::std::ostream& os, const RcString& x) {
    os.write(x.c_str(), x.size());
    return os;
}

int RcString::compare(size_t o, size_t l, const char* s) const {
    assert(o <= this->size());
    if (l <= this->size() - o) {
        return memcmp(this->c_str() + o, s, l);
    } else {
        if (int rv = memcmp(this->c_str() + o, s, this->size() - o)) {
            return rv;
        }
        return -1;
    }
}
