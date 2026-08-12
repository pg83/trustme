#include "hir_encoded_literal.h"

Ordering Reloc::ord(const Reloc& x) const {
    ORD(ofs, x.ofs);
    ORD(len, x.len);
    if (p) {
        if (!x.p) {
            return OrdLess;
        }
        return p->ord(*x.p);
    } else {
        if (x.p) {
            return OrdGreater;
        }
        return ::ord(bytes, x.bytes);
    }
}
Ordering EncodedLiteral::ord(const EncodedLiteral& x) const {
    ORD(bytes, x.bytes);
    ORD(relocations, x.relocations);
    return OrdEqual;
}
//size_t  m_reloc_ofs;
//size_t  m_reloc_size;

EncodedLiteralSlice::EncodedLiteralSlice(const EncodedLiteral& base)
    : m_base(base)
    , m_ofs(0)
    , m_size(base.bytes.size())
//, m_reloc_ofs(0)
//, m_reloc_size(base.relocations.size())
{
}
EncodedLiteralSlice EncodedLiteralSlice::slice(size_t ofs) const {
    assert(ofs <= m_size);
    return slice(ofs, m_size - ofs);
}
EncodedLiteralSlice EncodedLiteralSlice::slice(size_t ofs, size_t len) const {
    assert(ofs <= m_size);
    assert(len <= m_size);
    assert(ofs + len <= m_size);
    auto rv = EncodedLiteralSlice(m_base);
    rv.m_ofs = m_ofs + ofs;
    rv.m_size = len;
    return rv;
}
