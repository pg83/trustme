#include "hir_encoded_literal.h"

Ordering Reloc::ord(const Reloc& x) const {
    ORD(ofs, x.ofs);
    ORD(len, x.len);
    ORD(preserveTrackCaller, x.preserveTrackCaller);
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
    : base(base)
    , ofs(0)
    , mSize(base.bytes.size())
//, m_reloc_ofs(0)
//, m_reloc_size(base.relocations.size())
{
}
EncodedLiteralSlice EncodedLiteralSlice::slice(size_t ofs) const {
    assert(ofs <= mSize);
    return slice(ofs, mSize - ofs);
}
EncodedLiteralSlice EncodedLiteralSlice::slice(size_t ofs, size_t len) const {
    assert(ofs <= mSize);
    assert(len <= mSize);
    assert(ofs + len <= mSize);
    auto rv = EncodedLiteralSlice(base);
    rv.ofs = this->ofs + ofs;
    rv.mSize = len;
    return rv;
}

::std::ostream& operator<<(::std::ostream& os, const Reloc& x) {
    os << "@" << std::hex << "0x" << x.ofs << std::dec << "+" << x.len << " = ";
    if (x.p) {
        os << "&" << *x.p;
        if (x.preserveTrackCaller) {
            os << " [track_caller ABI]";
        }
    } else {
        os << "\"" << FmtEscaped(x.bytes) << "\"";
    }
    return os;
}
::std::ostream& operator<<(std::ostream& os, const EncodedLiteral& x) {
    for (size_t i = 0; i < x.bytes.size(); i++) {
        const char* HEX = "0123456789ABCDEF";
        os << HEX[x.bytes[i] >> 4] << HEX[x.bytes[i] & 0xF];
        if ((i + 1) % 8 == 0 && i + 1 < x.bytes.size()) {
            os << " ";
        }
    }
    os << "{" << x.relocations << "}";
    return os;
}
