#pragma once

#include <stdint.h>
#include <iostream>
#include "common.h"
#include <cstring> // memcpy

class U128 {
    friend class S128;
    uint64_t lo;
    uint64_t hi;

public:
    U128();

    explicit U128(uint64_t lo, uint64_t hi = 0);

    static U128 max() {
        return U128(UINT64_MAX, UINT64_MAX);
    }

    uint64_t getLo() const {
        return lo;
    }

    uint64_t getHi() const {
        return hi;
    }

    bool is_u64() const {
        return hi == 0;
    }

    uint64_t truncate_u64() const {
        return lo;
    }

    uint64_t encodeFloat(int bits, int zero_exp) const;

    double to_double() const;

    float to_float() const;

    void to_le_bytes(uint8_t* dst, size_t max_len) {
        max_len = max_len > 16 ? 16 : max_len;
#if __LITTLE_ENDIAN__
        memcpy(dst, this, max_len);
#else
        for (size_t i = 0; i < max_len; i++) {
            dst[i] = static_cast<uint8_t>((*this >> static_cast<unsigned>(i * 8)).truncate_u64());
        }
#endif
    }

    void to_be_bytes(uint8_t* dst, size_t max_len);

    void fromLeBytes(const uint8_t* src, size_t max_len) {
        max_len = max_len > 16 ? 16 : max_len;
        *this = U128();
#if __LITTLE_ENDIAN__
        memcpy(this, src, max_len);
#else
        for (size_t i = 0; i < max_len; i++) {
            *this |= U128(src[i]) << static_cast<unsigned>(i * 8);
        }
#endif
    }

    void fromBeBytes(const uint8_t* src, size_t max_len);

    U128 operator~() const {
        return U128(~lo, ~hi);
    }

    U128 operator+(U128 x) const;

    U128 operator-(U128 x) const;

    U128 operator|(U128 x) const {
        return U128(lo | x.lo, hi | x.hi);
    }

    U128 operator&(U128 x) const {
        return U128(lo & x.lo, hi & x.hi);
    }

    U128 operator^(U128 x) const {
        return U128(lo ^ x.lo, hi ^ x.hi);
    }

    U128 operator*(U128 x) const;

    U128 operator/(U128 x) const;

    U128 operator%(U128 x) const;

    U128 operator+(unsigned x) const {
        return *this + U128(x);
    }

    U128 operator-(unsigned x) const {
        return *this - U128(x);
    }

    U128 operator*(unsigned x) const {
        return *this * U128(x);
    }

    U128 operator|(unsigned x) const {
        return *this | U128(x);
    }

    U128 operator&(unsigned x) const {
        return *this & U128(x);
    }

    U128 operator^(unsigned x) const {
        return *this ^ U128(x);
    }

    U128 operator/(unsigned x) const {
        return *this / U128(x);
    }

    U128 operator%(unsigned x) const {
        return *this % U128(x);
    }

    U128& operator+=(unsigned x);

    U128& operator+=(U128 x);

    U128& operator*=(unsigned x);

    U128& operator*=(U128 x);

    U128& operator|=(unsigned x);

    U128& operator|=(U128 x);

    U128& operator&=(unsigned x);

    U128& operator&=(U128 x);

    U128& operator<<=(unsigned bits);

    U128& operator>>=(unsigned bits);

    U128 operator<<(U128 bits) const;

    U128 operator>>(U128 bits) const;

    U128 operator<<(unsigned bits) const;

    U128 operator>>(unsigned bits) const;

    Ordering ord(const U128& x) const;

    bool operator<(const U128& x) const {
        return cmp128(*this, x) < 0;
    }

    bool operator<=(const U128& x) const {
        return cmp128(*this, x) <= 0;
    }

    bool operator>(const U128& x) const {
        return cmp128(*this, x) > 0;
    }

    bool operator>=(const U128& x) const {
        return cmp128(*this, x) >= 0;
    }

    bool operator==(const U128& x) const {
        return cmp128(*this, x) == 0;
    }

    bool operator!=(const U128& x) const {
        return cmp128(*this, x) != 0;
    }

    bool operator<(unsigned x) const {
        return *this < U128(x);
    }

    bool operator<=(unsigned x) const {
        return *this <= U128(x);
    }

    bool operator>(unsigned x) const {
        return *this > U128(x);
    }

    bool operator>=(unsigned x) const {
        return *this >= U128(x);
    }

    bool operator==(unsigned x) const {
        return *this == U128(x);
    }

    bool operator!=(unsigned x) const {
        return *this != U128(x);
    }

    bool bit(unsigned idx) const;

    friend std::ostream& operator<<(::std::ostream& os, const U128& x);

private:
    // TODO: All of these are functionally identical to code in `codegen_c.cpp` - could it be shared?
    static int cmp128(U128 a, U128 b);

    static bool add128O(U128 a, U128 b, U128* o);

    static bool sub128_o(U128 a, U128 b, U128* o);

    static bool mul128_o(U128 a, U128 b, U128* o);

    // Long division
    static bool div128O(U128 a, U128 b, U128* q, U128* r);
};

class S128 {
    U128 inner;

public:
    S128();

    explicit S128(int64_t v);

    S128(U128 v);

    static S128 max() {
        return S128(U128(UINT64_MAX, INT64_MAX));
    }

    static S128 min() {
        return S128(U128(0, INT64_MIN));
    }

    bool is_i64() const {
        return inner.hi == ((inner.lo >> 63) ? UINT64_MAX : 0);
    }

    int64_t truncate_i64() const;

    double to_double() const {
        return (*this < 0 ? -1.0 : 1.0) * this->u_abs().to_double();
    }

    float to_float() const {
        return (*this < 0 ? -1.0f : 1.0f) * this->u_abs().to_float();
    }

    U128 getInner() const {
        return inner;
    }

private:
    void sign_extend(size_t n_bytes);

public:
    void fromLeBytes(const uint8_t* src, size_t max_len);

    void fromBeBytes(const uint8_t* src, size_t max_len);

    S128 operator~() const {
        return S128(~inner);
    }

    S128 operator-() const {
        return S128(~inner) + S128(1);
    }

    S128 operator+(S128 x) const {
        return S128(inner + x.inner);
    }

    S128 operator-(S128 x) const {
        return S128(inner - x.inner);
    }

    S128 operator|(S128 x) const {
        return S128(inner | x.inner);
    }

    S128 operator&(S128 x) const {
        return S128(inner & x.inner);
    }

    S128 operator^(S128 x) const {
        return S128(inner ^ x.inner);
    }

    S128 operator*(S128 x) const;

    S128 operator/(S128 x) const;

    S128 operator%(S128 x) const;

    bool is_neg() const {
        return (inner >> 127).truncate_u64() != 0;
    }

    /// Unsigned absolute value (handles MIN correctly)
    U128 u_abs() const;

    Ordering ord(const S128& x) const;

    bool operator<(const S128& x) const {
        return cmp128s(this->inner, x.inner) < 0;
    }

    bool operator<=(const S128& x) const {
        return cmp128s(this->inner, x.inner) <= 0;
    }

    bool operator>(const S128& x) const {
        return cmp128s(this->inner, x.inner) > 0;
    }

    bool operator>=(const S128& x) const {
        return cmp128s(this->inner, x.inner) >= 0;
    }

    bool operator==(const S128& x) const {
        return cmp128s(this->inner, x.inner) == 0;
    }

    bool operator!=(const S128& x) const {
        return cmp128s(this->inner, x.inner) != 0;
    }

    bool operator<(int x) const {
        return *this < S128(x);
    }

    bool operator<=(int x) const {
        return *this <= S128(x);
    }

    bool operator>(int x) const {
        return *this > S128(x);
    }

    bool operator>=(int x) const {
        return *this >= S128(x);
    }

    bool operator==(int x) const {
        return *this == S128(x);
    }

    bool operator!=(int x) const {
        return *this != S128(x);
    }

    S128& operator<<=(unsigned bits);

    S128& operator>>=(unsigned bits);

    S128 operator<<(unsigned bits) const {
        return S128(inner << bits);
    }

    S128 operator>>(unsigned bits) const;

    void fmt(::std::ostream& os) const;

    friend std::ostream& operator<<(::std::ostream& os, const S128& x);

private:
    static int cmp128s(U128 a, U128 b);
};
