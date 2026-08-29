#include "hir_encoded_literal.h"

#include "output.h"

using namespace stl;

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
    BUG_ASSERT(ofs <= size);
    return slice(ofs, size - ofs);
}

EncodedLiteralSlice EncodedLiteralSlice::slice(size_t ofs, size_t len) const {
    BUG_ASSERT(ofs <= size);
    BUG_ASSERT(len <= size);
    BUG_ASSERT(ofs + len <= size);
    auto rv = EncodedLiteralSlice(base);
    rv.ofs = this->ofs + ofs;
    rv.size = len;
    return rv;
}

namespace stl {
    template <>
    void output<ZeroCopyOutput, Reloc>(ZeroCopyOutput& os, const Reloc& x) {
        os << StringView("@0x") << formatHex(x.ofs) << StringView("+") << x.len << StringView(" = ");
        if (x.p) {
            os << StringView("&") << *x.p;
            if (x.preserveTrackCaller) {
                os << StringView(" [track_caller ABI]");
            }
        } else {
            os << StringView("\"") << FmtEscaped(x.bytes) << StringView("\"");
        }
        return;
    }

    template <>
    void output<ZeroCopyOutput, EncodedLiteral>(ZeroCopyOutput& os, const EncodedLiteral& x) {
        for (size_t i = 0; i < x.bytes.size(); i++) {
            const char* HEX = "0123456789ABCDEF";
            os << HEX[x.bytes[i] >> 4] << HEX[x.bytes[i] & 0xF];
            if ((i + 1) % 8 == 0 && i + 1 < x.bytes.size()) {
                os << StringView(" ");
            }
        }
        os << StringView("{") << x.relocations << StringView("}");
        return;
    }

    template <>
    void output<ZeroCopyOutput, EncodedLiteralSlice>(ZeroCopyOutput& out, EncodedLiteralSlice value) {
        auto relocation = std::find_if(value.base.relocations.begin(), value.base.relocations.end(), [&](const Reloc& item) {
            return item.ofs >= value.ofs;
        });
        for (size_t i = 0; i < value.size; i++) {
            const char* hex = "0123456789ABCDEF";
            auto offset = value.ofs + i;
            auto byte = value.base.bytes[offset];
            if (relocation != value.base.relocations.end() && relocation->ofs == offset) {
                if (relocation->p) {
                    out << StringView("{&") << *relocation->p << StringView("}");
                } else {
                    out << StringView("{\"") << FmtEscaped(relocation->bytes) << StringView("\"}");
                }
                ++relocation;
            }
            out << hex[byte >> 4] << hex[byte & 0xF];
            if ((i + 1) % 8 == 0 && i + 1 < value.size) {
                out << StringView(" ");
            }
        }
    }

    template <>
    void output<ZeroCopyOutput, std::vector<Reloc>>(ZeroCopyOutput& out, const std::vector<Reloc>& values) {
        outCont(out, values);
    }
}
