#pragma once

//
//
//
#include "common.h"
#include "hir_type.h"
#include "int128.h"
#include "floats.h"

struct Reloc {
    size_t ofs;
    size_t len;
    ::std::unique_ptr<::HIR::Path> p;
    ::std::string bytes;

    static Reloc new_named(size_t ofs, size_t len, ::HIR::Path p) {
        return Reloc{ofs, len, box$(p), ""};
    }

    static Reloc new_bytes(size_t ofs, size_t len, ::std::string bytes) {
        return Reloc{ofs, len, nullptr, ::std::move(bytes)};
    }

    friend ::std::ostream& operator<<(::std::ostream& os, const Reloc& x);

    Ordering ord(const Reloc& x) const;

    bool operator==(const Reloc& x) const {
        return ord(x) == OrdEqual;
    }
};

struct EncodedLiteral {
    inline static const unsigned PTR_BASE = 0x1000;

    std::vector<uint8_t> bytes;
    std::vector<Reloc> relocations;

    static EncodedLiteral make_usize(uint64_t v);
    EncodedLiteral clone() const;

    void write_uint(size_t ofs, size_t size, uint64_t v);

    void write_usize(size_t ofs, uint64_t v);
    uint64_t read_usize(size_t ofs) const;

    friend ::std::ostream& operator<<(std::ostream& os, const EncodedLiteral& x);

    Ordering ord(const EncodedLiteral& x) const;

    bool operator==(const EncodedLiteral& x) const {
        return ord(x) == OrdEqual;
    }
};

struct EncodedLiteralSlice {
    const EncodedLiteral& base;
    size_t ofs;
    size_t mSize;

    //size_t  m_reloc_ofs;
    //size_t  m_reloc_size;

    EncodedLiteralSlice(const EncodedLiteral& base);

    EncodedLiteralSlice slice(size_t ofs) const;

    EncodedLiteralSlice slice(size_t ofs, size_t len) const;

    U128 read_uint(size_t size = 0) const;
    S128 read_sint(size_t size = 0) const;
    FloatValue read_float(size_t size = 0) const;
    const Reloc* get_reloc() const;

    bool operator==(const EncodedLiteralSlice& x) const;

    bool operator!=(const EncodedLiteralSlice& x) const {
        return !(*this == x);
    }

    Ordering ord(const EncodedLiteralSlice& x) const;

    friend ::std::ostream& operator<<(std::ostream& os, const EncodedLiteralSlice& x);
};
