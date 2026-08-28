#include "int128.h"

U128::U128()
    : lo(0)
    , hi(0)
{
}

U128::U128(u64 lo, u64 hi)
    : lo(lo)
    , hi(hi)
{
}

u64 U128::encodeFloat(int bits, int zeroExp) const {
    int n;
    {
        int nset = 0;
        U128 val = *this;
        while (val != U128(0)) {
            val >>= 1;
            nset += 1;
        }
        n = 128 - nset;
    }
    U128 y = *this << n;
    u64 a = (y.hi >> ((128 - (bits + 1)) - 64));
    int s = 64 - (bits + 1);
    u64 b = (y >> s).lo | (y.lo & ((1ull << s) - 1));
    u64 m = a + ((b - (b >> 63 & ~a)) >> 63);
    u64 e = (*this == U128(0)) ? 0 : (127 - n) + zeroExp - 1;
    return (e << bits) + m;
}

double U128::toDouble() const {
    u64 vi = encodeFloat(52, 1023);
    double rv;
    memcpy(&rv, &vi, sizeof(rv));
    return rv;
}

float U128::toFloat() const {
    u32 vi = static_cast<u32>(encodeFloat(23, 127));
    float rv;
    memcpy(&rv, &vi, sizeof(rv));
    return rv;
}

void U128::toBeBytes(u8* dst, size_t maxLen) {
    maxLen = maxLen > 16 ? 16 : maxLen;
    for (size_t i = 0; i < maxLen; i++) {
        dst[maxLen - 1 - i] = static_cast<u8>((*this >> static_cast<unsigned>(i * 8)).truncateU64());
    }
}

void U128::fromBeBytes(const u8* src, size_t maxLen) {
    maxLen = maxLen > 16 ? 16 : maxLen;
    *this = U128();
    for (size_t i = 0; i < maxLen; i++) {
        *this |= U128(src[maxLen - 1 - i]) << static_cast<unsigned>(i * 8);
    }
}

U128 U128::operator+(U128 x) const {
    U128 rv(0);
    add128O(*this, x, &rv);
    return rv;
}

U128 U128::operator-(U128 x) const {
    U128 rv(0);
    sub128O(*this, x, &rv);
    return rv;
}

U128 U128::operator*(U128 x) const {
    U128 rv(0);
    mul128O(*this, x, &rv);
    return rv;
}

U128 U128::operator/(U128 x) const {
    U128 rv(0);
    div128O(*this, x, &rv, nullptr);
    return rv;
}

U128 U128::operator%(U128 x) const {
    U128 rv(0);
    div128O(*this, x, nullptr, &rv);
    return rv;
}

U128& U128::operator+=(unsigned x) {
    *this = *this + x;
    return *this;
}

U128& U128::operator+=(U128 x) {
    *this = *this + x;
    return *this;
}

U128& U128::operator*=(unsigned x) {
    *this = *this * x;
    return *this;
}

U128& U128::operator*=(U128 x) {
    *this = *this * x;
    return *this;
}

U128& U128::operator|=(unsigned x) {
    *this = *this | x;
    return *this;
}

U128& U128::operator|=(U128 x) {
    *this = *this | x;
    return *this;
}

U128& U128::operator&=(unsigned x) {
    *this = *this & x;
    return *this;
}

U128& U128::operator&=(U128 x) {
    *this = *this & x;
    return *this;
}

U128& U128::operator<<=(unsigned bits) {
    *this = *this << bits;
    return *this;
}

U128& U128::operator>>=(unsigned bits) {
    *this = *this >> bits;
    return *this;
}

U128 U128::operator<<(U128 bits) const {
    if (bits >= 128) {
        return U128(0);
    }
    return *this << static_cast<unsigned>(bits.truncateU64());
}

U128 U128::operator>>(U128 bits) const {
    if (bits >= 128) {
        return U128(0);
    }
    return *this >> static_cast<unsigned>(bits.truncateU64());
}

U128 U128::operator<<(unsigned bits) const {
    if (bits == 0) {
        return *this;
    }
    if (bits >= 128) {
        return U128(0);
    }
    if (bits >= 64) {
        return U128(0, lo << (bits - 64));
    } else {
        return U128(lo << bits, (hi << bits) | (lo >> (64 - bits)));
    }
}

U128 U128::operator>>(unsigned bits) const {
    if (bits == 0) {
        return *this;
    }
    if (bits >= 128) {
        return U128(0);
    }
    if (bits >= 64) {
        return U128(hi >> (bits - 64), 0);
    } else {
        return U128(lo >> bits | (hi << (64 - bits)), hi >> bits);
    }
}

Ordering U128::ord(const U128& x) const {
    int c = cmp128(*this, x);
    if (c == 0) {
        return OrdEqual;
    }
    return c < 0 ? OrdLess : OrdGreater;
}

bool U128::bit(unsigned idx) const {
    if (idx < 64) {
        return ((lo >> idx) & 1) != 0;
    }
    if (idx < 128) {
        return ((hi >> (idx - 64)) & 1) != 0;
    }
    return false;
}

// TODO: All of these are functionally identical to code in `codegen_c.cpp` - could it be shared?
int U128::cmp128(U128 a, U128 b) {
    if (a.hi != b.hi) {
        return a.hi < b.hi ? -1 : 1;
    }
    if (a.lo != b.lo) {
        return a.lo < b.lo ? -1 : 1;
    }
    return 0;
}

bool U128::add128O(U128 a, U128 b, U128* o) {
    o->lo = a.lo + b.lo;
    o->hi = a.hi + b.hi + (o->lo < a.lo ? 1 : 0);
    return (o->hi < a.hi);
}

bool U128::sub128O(U128 a, U128 b, U128* o) {
    o->lo = a.lo - b.lo;
    o->hi = a.hi - b.hi - (o->lo > a.lo ? 1 : 0);
    return (o->hi > a.hi);
}

bool U128::mul128O(U128 a, U128 b, U128* o) {
    bool of = false;
    o->hi = 0;
    o->lo = 0;
    for (int i = 0; i < 128; i++) {
        u64 m = (1ull << (i % 64));
        if (a.hi == 0 && a.lo < m) {
            break;
        }
        if (i >= 64 && a.hi < m) {
            break;
        }
        if (m & (i >= 64 ? a.hi : a.lo)) {
            of |= add128O(*o, b, o);
        }
        b.hi = (b.hi << 1) | (b.lo >> 63);
        b.lo = (b.lo << 1);
    }
    return of;
}

bool U128::div128O(U128 a, U128 b, U128* q, U128* r) {
    if (a.hi == 0 && b.hi == 0) {
        if (q) {
            q->hi = 0;
            q->lo = a.lo / b.lo;
        }
        if (r) {
            r->hi = 0;
            r->lo = a.lo % b.lo;
        }
        return false;
    }
    if (cmp128(a, b) < 0) {
        if (q) {
            q->hi = 0;
            q->lo = 0;
        }
        if (r) {
            *r = a;
        }
        return false;
    }
    U128 aDiv2((a.lo >> 1) | (a.hi << 63), a.hi >> 1);
    int shift = 0;
    while (cmp128(aDiv2, b) >= 0 && shift < 128) {
        shift += 1;
        b.hi = (b.hi << 1) | (b.lo >> 63);
        b.lo <<= 1;
    }
    if (shift == 128) {
        return true;
    }
    U128 mask(/*lo=*/(shift >= 64 ? 0 : (1ull << shift)), /*hi=*/(shift < 64 ? 0 : 1ull << (shift - 64)));
    shift++;
    if (q) {
        q->hi = 0;
        q->lo = 0;
    }
    while (shift--) {
        if (cmp128(a, b) >= 0) {
            if (q) {
                add128O(*q, mask, q);
            }
            sub128O(a, b, &a);
        }
        mask.lo = (mask.lo >> 1) | (mask.hi << 63);
        mask.hi >>= 1;
        b.lo = (b.lo >> 1) | (b.hi << 63);
        b.hi >>= 1;
    }
    if (r) {
        *r = a;
    }
    return false;
}

S128::S128() {
}

S128::S128(i64 v)
    : inner(v, v < 0 ? UINT64_MAX : 0)
{
}

S128::S128(U128 v)
    : inner(v)
{
}

i64 S128::truncateI64() const { /*assert(inner.hi == 0 || inner.hi == UINT64_MAX);*/
    return inner.lo;
}

void S128::signExtend(size_t nBytes) {
    if (nBytes < 16 && inner.bit(static_cast<unsigned>(nBytes * 8 - 1))) {
        inner |= U128::max() << static_cast<unsigned>(nBytes * 8);
    }
}

void S128::fromLeBytes(const u8* src, size_t maxLen) {
    inner.fromLeBytes(src, maxLen);
    signExtend(maxLen);
}

void S128::fromBeBytes(const u8* src, size_t maxLen) {
    inner.fromBeBytes(src, maxLen);
    signExtend(maxLen);
}

S128 S128::operator*(S128 x) const {
    auto retNeg = isNeg() != x.isNeg();
    auto rvU = uAbs() * x.uAbs();
    return retNeg ? -S128(rvU) : S128(rvU);
}

S128 S128::operator/(S128 x) const {
    auto retNeg = isNeg() != x.isNeg();
    auto rvU = uAbs() / x.uAbs();
    return retNeg ? -S128(rvU) : S128(rvU);
}

S128 S128::operator%(S128 x) const {
    auto retNeg = isNeg();
    auto rvU = uAbs() % x.uAbs();
    return retNeg ? -S128(rvU) : S128(rvU);
}

U128 S128::uAbs() const {
    if (inner.hi == UINT64_MAX && inner.lo == 0) {
        return inner;
    }
    if (isNeg()) {
        return (-*this).inner;
    } else {
        return (*this).inner;
    }
}

Ordering S128::ord(const S128& x) const {
    int c = cmp128s(this->inner, x.inner);
    if (c == 0) {
        return OrdEqual;
    }
    return c < 0 ? OrdLess : OrdGreater;
}

S128& S128::operator<<=(unsigned bits) {
    *this = *this << bits;
    return *this;
}

S128& S128::operator>>=(unsigned bits) {
    *this = *this >> bits;
    return *this;
}

S128 S128::operator>>(unsigned bits) const {
    if (bits == 0) {
        return *this;
    }
    if (bits >= 128) {
        return *this < 0 ? S128(-1) : S128(0);
    }
    if (bits >= 64) {
        auto lo = static_cast<u64>(static_cast<i64>(inner.hi) >> (bits - 64));
        return S128(U128(lo, *this < 0 ? UINT64_MAX : 0));
    }
    return S128(U128(inner.lo >> bits | (inner.hi << (64 - bits)), static_cast<u64>(static_cast<i64>(inner.hi) >> bits)));
}

void S128::fmt(std::ostream& os) const {
    if (isI64()) {
        os << static_cast<i64>(inner.lo);
    } else {
        if (*this < 0) {
            os << '-';
            os << (-*this).inner;
        } else {
            os << inner;
        }
    }
}

int S128::cmp128s(U128 a, U128 b) {
    if (a.hi != b.hi) {
        return (i64)a.hi < (i64)b.hi ? -1 : 1;
    }
    if (a.lo != b.lo) {
        return a.lo < b.lo ? -1 : 1;
    }
    return 0;
}

std::ostream& operator<<(std::ostream& os, const U128& x) {
    if (x.hi == 0) {
        os << x.lo;
    } else {
        char output[40 + 1];
        auto v = x;
        unsigned i = 0;
        const char* chars = (os.flags() & std::ios_base::uppercase) ? "0123456789ABCDEF" : "0123456789abcdef";
        switch (os.flags() & std::ios_base::basefield) {
            case std::ios_base::hex:
                while (v.hi > 0 || v.lo > 0) {
                    output[i++] = chars[(v.lo & 0xF)];
                    v >>= 4u;
                }
                break;
            case std::ios_base::oct:
                while (v.hi > 0 || v.lo > 0) {
                    output[i++] = chars[(v.lo & 7)];
                    v >>= 3u;
                }
                break;
            case std::ios_base::dec:
            default:
                while (v.hi > 0 || v.lo > 0) {
                    U128 v2(0), rem(0);
                    U128::div128O(v, U128(10), &v2, &rem);
                    output[i++] = chars[(rem.lo % 10)];
                    v = v2;
                }
                break;
        }
        for (auto v = os.width(); v > i; v--) {
            os << ' ';
        }
        while (i--) {
            os << output[i];
        }
    }
    return os;
}

std::ostream& operator<<(std::ostream& os, const S128& x) {
    x.fmt(os);
    return os;
}
