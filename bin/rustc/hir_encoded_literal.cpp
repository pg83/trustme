#include "hir_encoded_literal.h"

#include "output.h"

#include <cstring>

using namespace stl;

EncodedLiteral EncodedLiteral::makeUsize(u64 v) {
    EncodedLiteral rv;
    rv.bytes.zero(8);
    rv.writeUsize(0, v);
    return rv;
}

EncodedLiteral EncodedLiteral::clone() const {
    EncodedLiteral rv;
    rv.bytes = bytes;
    rv.relocations.reserve(relocations.size());
    for (const auto& r : relocations) {
        if (r.p) {
            rv.relocations.push_back(Reloc::newNamed(r.ofs, r.len, r.p->clone(), r.preserveTrackCaller));
        } else {
            rv.relocations.push_back(Reloc::newBytes(r.ofs, r.len, r.bytes));
        }
    }
    return rv;
}

void EncodedLiteral::writeUint(size_t ofs, size_t size, u64 v) {
    BUG_ASSERT(ofs + size <= bytes.length());
    for (size_t i = 0; i < size; i++) {
        size_t bit = i * 8;
        if (bit < 64) {
            auto b = static_cast<u8>(v >> bit);
            bytes.mut(ofs + i) = b;
        }
    }
}

void EncodedLiteral::writeUsize(size_t ofs, u64 v) {
    this->writeUint(ofs, 8, v);
}

u64 EncodedLiteral::readUsize(size_t ofs) const {
    return EncodedLiteralSlice(*this).slice(ofs).readUint(8).truncateU64();
}

U128 EncodedLiteralSlice::readUint(size_t size /*=0*/) const {
    if (size == 0) {
        size = this->size;
    }
    ASSERT_BUG(Span(), size <= this->size, StringView("Over-large read (") << size << StringView(" > ") << this->size << StringView(")"));
    U128 v(0);
    for (size_t i = 0; i < size; i++) {
        size_t bit = i * 8;
        if (bit < 128) {
            v |= U128(base.bytes[ofs + i]) << bit;
        }
    }
    DEBUG(StringView("(") << size << StringView(") = ") << v);
    return v;
}

S128 EncodedLiteralSlice::readSint(size_t size /*=0*/) const {
    if (size == 0) {
        size = this->size;
    }
    auto v = readUint(size);
    if (size < 128 / 8 && ((v >> (8 * size - 1)) != 0)) {
        v |= U128(UINT64_MAX, UINT64_MAX) << (8 * size);
    }
    DEBUG(StringView("(") << size << StringView(") = ") << v);
    return S128(v);
}

FloatValue EncodedLiteralSlice::readFloat(size_t size /*=0*/) const {
    if (size == 0) {
        size = this->size;
    }
    BUG_ASSERT(size <= this->size);
    switch (size) {
        case 2: {
            F16 v;
            memcpy(&v, &base.bytes[ofs], 2);
            return FloatValue(static_cast<float>(v));
        }
        case 4: {
            float v;
            memcpy(&v, &base.bytes[ofs], 4);
            return v;
        }
        case 8: {
            double v;
            memcpy(&v, &base.bytes[ofs], 8);
            return v;
        }
        case 16: {
            F128 v;
            memcpy(&v, &base.bytes[ofs], 16);
            return v;
        }
        default:
            BUG(Span(), StringView("Unexpected float size"));
    }
}

const Reloc* EncodedLiteralSlice::getReloc() const {
    for (const auto& r : base.relocations) {
        if (r.ofs == ofs) {
            return &r;
        }
    }
    return nullptr;
}

bool EncodedLiteralSlice::operator==(const EncodedLiteralSlice& x) const {
    if (size != x.size) {
        return false;
    }
    for (size_t i = 0; i < size; i++) {
        if (base.bytes[ofs + i] != x.base.bytes[x.ofs + i]) {
            return false;
        }
    }
    auto it1 = std::find_if(base.relocations.begin(), base.relocations.end(), [&](const Reloc& r) {
        return r.ofs >= ofs;
    });
    auto it2 = std::find_if(x.base.relocations.begin(), x.base.relocations.end(), [&](const Reloc& r) {
        return r.ofs >= x.ofs;
    });
    for (; it1 != base.relocations.end() && it2 != x.base.relocations.end(); ++it1, ++it2) {
        if (it1->ofs - ofs != it2->ofs - x.ofs) {
            return false;
        }
        if (it1->len != it2->len) {
            return false;
        }
        if (bool(it1->p) != bool(it2->p)) {
            return false;
        }
        if (it1->p) {
            if (*it1->p != *it2->p) {
                return false;
            }
        } else if (it1->bytes != it2->bytes) {
            return false;
        }
    }
    return true;
}

Ordering EncodedLiteralSlice::ord(const EncodedLiteralSlice& x) const {
    auto minSize = std::min(size, x.size);
    for (size_t i = 0; i < minSize; i++) {
        if (auto cmp = ::ord(base.bytes[ofs + i], x.base.bytes[x.ofs + i])) {
            return cmp;
        }
    }
    if (auto cmp = ::ord(size, x.size)) {
        return cmp;
    }

    auto it1 = std::find_if(base.relocations.begin(), base.relocations.end(), [&](const Reloc& r) {
        return r.ofs >= ofs;
    });
    auto it2 = std::find_if(x.base.relocations.begin(), x.base.relocations.end(), [&](const Reloc& r) {
        return r.ofs >= x.ofs;
    });

    for (; it1 != base.relocations.end() && it2 != x.base.relocations.end(); ++it1, ++it2) {
        if (auto cmp = ::ord(it1->ofs - ofs, it2->ofs - x.ofs)) {
            return cmp;
        }
        if (auto cmp = ::ord(it1->len, it2->len)) {
            return cmp;
        }
        if (auto cmp = ::ord(bool(it1->p), bool(it2->p))) {
            return cmp;
        }
        if (it1->p) {
            if (auto cmp = ::ord(*it1->p, *it2->p)) {
                return cmp;
            }
        } else if (auto cmp = ::ord(it1->bytes, it2->bytes)) {
            return cmp;
        }
    }
    return OrdEqual;
}

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
    , size(base.bytes.length())
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

template <>
void stl::output<ZeroCopyOutput, Reloc>(ZeroCopyOutput& os, const Reloc& x) {
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
void stl::output<ZeroCopyOutput, EncodedLiteral>(ZeroCopyOutput& os, const EncodedLiteral& x) {
    for (size_t i = 0; i < x.bytes.length(); i++) {
        const char* HEX = "0123456789ABCDEF";
        os << HEX[x.bytes[i] >> 4] << HEX[x.bytes[i] & 0xF];
        if ((i + 1) % 8 == 0 && i + 1 < x.bytes.length()) {
            os << StringView(" ");
        }
    }
    os << StringView("{") << x.relocations << StringView("}");
    return;
}

template <>
void stl::output<ZeroCopyOutput, EncodedLiteralSlice>(ZeroCopyOutput& out, EncodedLiteralSlice value) {
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
void stl::output<ZeroCopyOutput, std::vector<Reloc>>(ZeroCopyOutput& out, const std::vector<Reloc>& values) {
    outCont(out, values);
}
