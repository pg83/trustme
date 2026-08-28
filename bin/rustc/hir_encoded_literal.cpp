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

EncodedLiteralSlice::EncodedLiteralSlice(const EncodedLiteral& base)
    : base(base)
    , ofs(0)
    , size(base.bytes.size())
{
}

EncodedLiteralSlice EncodedLiteralSlice::slice(size_t ofs) const {
    assert(ofs <= size);
    return slice(ofs, size - ofs);
}

EncodedLiteralSlice EncodedLiteralSlice::slice(size_t ofs, size_t len) const {
    assert(ofs <= size);
    assert(len <= size);
    assert(ofs + len <= size);
    auto rv = EncodedLiteralSlice(base);
    rv.ofs = this->ofs + ofs;
    rv.size = len;
    return rv;
}

std::ostream& operator<<(std::ostream& os, const Reloc& x) {
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

std::ostream& operator<<(std::ostream& os, const EncodedLiteral& x) {
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
