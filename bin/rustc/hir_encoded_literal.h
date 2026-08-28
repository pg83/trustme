#pragma once

#include "common.h"
#include "floats.h"
#include "int128.h"
#include "hir_type.h"

struct Reloc {
    size_t ofs;
    size_t len;
    ::std::unique_ptr<HIRPath> p;
    ::std::string bytes;
    bool preserveTrackCaller = false;

    static Reloc newNamed(size_t ofs, size_t len, HIRPath p, bool preserveTrackCaller = false) {
        return Reloc{ofs, len, box$(p), "", preserveTrackCaller};
    }

    static Reloc newBytes(size_t ofs, size_t len, ::std::string bytes) {
        return Reloc{ofs, len, nullptr, ::std::move(bytes), false};
    }

    friend ::std::ostream& operator<<(::std::ostream& os, const Reloc& x);

    Ordering ord(const Reloc& x) const;

    bool operator==(const Reloc& x) const {
        return ord(x) == OrdEqual;
    }
};

struct EncodedLiteral {
    inline static const unsigned PTR_BASE = 0x1000;

    std::vector<u8> bytes;
    std::vector<Reloc> relocations;

    static EncodedLiteral makeUsize(u64 v);
    EncodedLiteral clone() const;

    void writeUint(size_t ofs, size_t size, u64 v);

    void writeUsize(size_t ofs, u64 v);
    u64 readUsize(size_t ofs) const;

    friend ::std::ostream& operator<<(std::ostream& os, const EncodedLiteral& x);

    Ordering ord(const EncodedLiteral& x) const;

    bool operator==(const EncodedLiteral& x) const {
        return ord(x) == OrdEqual;
    }
};

struct EncodedLiteralSlice {
    const EncodedLiteral& base;
    size_t ofs;
    size_t size;

    EncodedLiteralSlice(const EncodedLiteral& base);

    EncodedLiteralSlice slice(size_t ofs) const;

    EncodedLiteralSlice slice(size_t ofs, size_t len) const;

    U128 readUint(size_t size = 0) const;
    S128 readSint(size_t size = 0) const;
    FloatValue readFloat(size_t size = 0) const;
    const Reloc* getReloc() const;

    bool operator==(const EncodedLiteralSlice& x) const;

    bool operator!=(const EncodedLiteralSlice& x) const {
        return !(*this == x);
    }

    Ordering ord(const EncodedLiteralSlice& x) const;

    friend ::std::ostream& operator<<(std::ostream& os, const EncodedLiteralSlice& x);
};
