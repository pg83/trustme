#include "trans_mangling.h"

#include "hir_hir.h" // ABI_RUST
#include "hir_type.h"
#include "wire_board.h"

#include <std/str/builder.h>
#include <std/str/fmt.h>
#include <std/sym/i_map.h>
#include <std/mem/obj_pool.h>
#include <std/lib/vector.h>

#include <cmath> // ceil/log10
#include <cctype>
#include <cstring>
#include <type_traits>
#include <algorithm> // std::find

#define XXH_INLINE_ALL
#include <xxhash.h>

class ManglingContext {
public:
    stl::StringBuilder buffer;
    stl::Vector<RcString> names;
    stl::IntMap<RcString> ordinaryTypeCache;
    stl::IntMap<RcString> typeIdCache;

    explicit ManglingContext(stl::ObjPool& pool)
        : ordinaryTypeCache(&pool)
        , typeIdCache(&pool)
    {
    }
};

ManglingContext* TransCreateManglingContext(stl::ObjPool& pool) {
    return pool.make<ManglingContext>(pool);
}

namespace {

    stl::StringBuilder& operator<<(stl::StringBuilder& sb, char c) {
        sb.append(&c, 1);
        return sb;
    }

    stl::StringBuilder& operator<<(stl::StringBuilder& sb, const char* s) {
        sb.append(s, strlen(s));
        return sb;
    }

    template <typename T>
        requires(std::is_integral_v<T> && sizeof(T) >= 2)
    stl::StringBuilder& operator<<(stl::StringBuilder& sb, T v) {
        char buf[20];
        sb.append(buf, static_cast<char*>(stl::formatU64Base10(static_cast<u64>(v), buf)) - buf);
        return sb;
    }
}

enum class LifetimeIdentityMode {
    Erased,
    Closed,
    All,
};

class Mangler {
    stl::StringBuilder& os;
    stl::Vector<RcString>& names;
    const size_t nameWindowStart = names.length();
    const LifetimeIdentityMode lifetimeIdentityMode;

    bool includeLifetimeIdentity(bool hasFree) const {
        return lifetimeIdentityMode == LifetimeIdentityMode::All || (lifetimeIdentityMode == LifetimeIdentityMode::Closed && !hasFree);
    }

public:
    Mangler(ManglingContext& context, LifetimeIdentityMode lifetimeIdentityMode = LifetimeIdentityMode::Erased)
        : os(context.buffer)
        , names(context.names)
        , lifetimeIdentityMode(lifetimeIdentityMode)
    {
    }

    /// Formats an integer in a decodable format (lower case until the final digit)
    // Used to encode values that might be have trailing digits
    void fmtBase26Int(unsigned val) {
        // Lower-case:
        while (val >= 26) {
            os << char('a' + (val % 26));
            val /= 26;
        }
        assert(val < 26);
        os << char('A' + val);
    }

    // Reference-counted item names
    // - These can be repeated quite often, so support back-references
    //   > Back-references are emitted as "`_` <base26>"
    // - Otherwise, emitted as a raw string (see below)
    void fmtName(const RcString& s) {
        // Support back-references to names (if shorter than the literal name)
        const auto* windowBegin = names.begin() + nameWindowStart;
        auto it = std::find(windowBegin, names.end(), s);
        if (it != names.end()) {
            auto idx = it - windowBegin;
            // Only emit this way if shorter than the formatted name would be.
            auto len = 1 + static_cast<unsigned>(std::ceil(std::log10(idx + 1) / std::log10(26)));
            if (len < s.size()) {
                os << "_";
                fmtBase26Int(idx);
                return;
            }
        } else {
            names.pushBack(s);
        }

        this->fmtName(s.c_str());
    }

    // Item names:
    // - These can have a single '#' in them (either leading or in the middle)
    // - '-' (crate names)
    // Encoding is either:
    // - "<len:int> <raw_data>" (fully valid identifier)
    // - "<ofs:base26> <len:int> <raw_data1> <raw_data2>" (`#` or `-` present)
    // - "<len:base26> `_` <raw_data2>" (`#` or `-` at the start)
    void fmtName(const char* const s) {
        bool needsByteEncoding = false;
        for (const auto* p = reinterpret_cast<const unsigned char*>(s); *p; ++p) {
            const bool canEmitRaw =
                ('0' <= *p && *p <= '9')
                || ('A' <= *p && *p <= 'Z')
                || ('a' <= *p && *p <= 'z')
                || *p == '_' || *p == '#' || *p == '-';
            needsByteEncoding |= !canEmitRaw;
        }

        // The C backend needs an ASCII identifier, not the externally-visible
        // Rust v0 symbol spelling. `U` marks a bytewise encoding for names that
        // cannot be emitted raw. Escape ordinary names beginning with `U` as
        // well, so the mapping stays injective.
        ::std::string encoded;
        if (needsByteEncoding) {
            static constexpr char HEX[] = "0123456789abcdef";
            encoded = "U";
            for (const auto* p = reinterpret_cast<const unsigned char*>(s); *p; ++p) {
                encoded += HEX[*p >> 4];
                encoded += HEX[*p & 0xf];
            }
        } else if (s[0] == 'U') {
            encoded = "U";
            encoded += s;
        }
        const auto* name = encoded.empty() ? s : encoded.c_str();

        size_t size = strlen(name);
        const char* hashPos = nullptr;
        // Search the string for a '#' or '-' character (only one allowed)
        for (const auto* p = name; *p; p++) {
            if (isalnum(static_cast<unsigned char>(*p))) {
            } else if (*p == '_') {
            } else if (*p == '#' || *p == '-') { // HACK: Treat '-' and '#' as the same in encoding
                // Multiple hash characters? abort/error
                // HACK: Only treat the last one as special, previous ones are replaced by underscores
                hashPos = p;
            } else {
                BUG(Span(), "Encounteded invalid character in symbol name while mangling: '" << *p << "' in '" << name << "'");
            }
        }

        // If there's a hash, then encode such that it's removed
        if (hashPos != nullptr) {
            auto preHashLen = static_cast<int>(hashPos - name);
            // If the hash is at the start, and is followed by either a digit (expected) or an underscore (unlikely) - then encode with a leading underscore
            if (hashPos == name && (isdigit(static_cast<unsigned char>(hashPos[1])) || hashPos[1] == '_')) {
                // <len:base26> '_' <body2>
                // An encoding that allows this pattern
                fmtBase26Int(size - 1);
                ASSERT_BUG(Span(), hashPos[1] != '_', "Leading underscore not valid in '" << name << "'");
                os << '_';
                os << hashPos + 1;
            } else {
                // <pos:base26> <len:int> <body1> <body2>
                fmtBase26Int(preHashLen);
                bool needsLeadingEscape = (isdigit(static_cast<unsigned char>(name[0])) || name[0] == '_');
                os << size - 1 + (needsLeadingEscape ? 1 : 0);
                // If the string starts with a digit or underscrore, then escape it with another underscore.
                if (needsLeadingEscape) {
                    os << '_';
                }
                for (const char* c = name; c != hashPos; ++c) {
                    if (*c == '-' || *c == '#') {
                        os << '_';
                    } else {
                        os << *c;
                    }
                }
                os << hashPos + 1;
            }
        } else {
            bool needsLeadingEscape = (isdigit(static_cast<unsigned char>(name[0])) || name[0] == '_');
            os << size + (needsLeadingEscape ? 1 : 0);
            if (needsLeadingEscape) {
                os << '_';
            }
            os << name;
        }
    }

    // SimplePath : <ncomp> 'c' [<RcString> ...]
    void fmtSimplePath(const HIRSimplePath& sp) {
        os << sp.components().size();
        os << "c"; // Needed to separate the component count from the crate name
        this->fmtName(sp.crateName());
        for (const auto& c : sp.components()) {
            this->fmtName(c);
        }
    }

    // PathParams : <ntys> 'g' [<ASTType*> ...]
    void fmtPathParams(const HIRPathParams& pp) {
        // Type Parameter count
        os << pp.types.size();
        if (pp.values.size() > 0) {
            os << "v";
            os << pp.values.size();
        }
        os << "g";
        for (const auto& ty : pp.types) {
            fmtType(ty);
        }
        for (const auto& v : pp.values) {
            fmtConstGeneric(v);
        }
    }

    void fmtConstGeneric(const HIRConstGeneric& value) {
        const auto* evaluated = value.opt_Evaluated();
        if (!evaluated) {
            BUG(Span(), "Non-encodable const generic " << value);
        }
        const auto& literal = **evaluated;
        os << "V" << literal.bytes.size() << "_";
        for (const auto byte : literal.bytes) {
            os << "0123456789abcdef"[byte >> 4];
            os << "0123456789abcdef"[byte & 0xF];
        }
        if (!literal.relocations.empty()) {
            os << "_" << literal.relocations.size() << "R";
            for (const auto& relocation : literal.relocations) {
                os << relocation.ofs << "o" << relocation.len;
                if (relocation.p) {
                    os << "p";
                    fmtPath(*relocation.p);
                } else {
                    os << "b" << relocation.bytes.size() << "_";
                    for (const auto byte : relocation.bytes) {
                        os << "0123456789abcdef"[static_cast<unsigned char>(byte) >> 4];
                        os << "0123456789abcdef"[static_cast<unsigned char>(byte) & 0xF];
                    }
                }
            }
        }
    }

    // GenericPath : <SimplePath> <PathParams>
    void fmtGenericPath(const HIRGenericPath& gp) {
        this->fmtSimplePath(gp.path);
        this->fmtPathParams(gp.params);
    }

    void fmtPath(const HIRPath& p) {
        // Path type
        // - Generic: starts with `G`
        // - Inherent: Starts with `I`
        // - Trait: Starts with `Q` (qualified)
        // - bare type: Starts with `T` (see Trans_MangleType)
        switch (p.data.tag()) {
            case HIRPathData::TAG_Generic: {
                auto& e = p.data.as_Generic();
                os << "G";
                this->fmtGenericPath(e);
                break;
            }
            case HIRPathData::TAG_UfcsInherent: {
                auto& e = p.data.as_UfcsInherent();
                os << "I";
                this->fmtType(e.type);
                this->fmtName(e.item);
                this->fmtPathParams(e.params);
                break;
            }
            case HIRPathData::TAG_UfcsKnown: {
                auto& e = p.data.as_UfcsKnown();
                os << "Q";
                this->fmtType(e.type);
                this->fmtGenericPath(e.trait);
                this->fmtName(e.item);
                this->fmtPathParams(e.params);
                break;
            }
            case HIRPathData::TAG_UfcsUnknown: {
                BUG(Span(), "Non-encodable path " << p);
                break;
            }
        }
    }

    // Type
    // - Tuple: 'T' <nelem> [<ASTType*> ...]
    // - Slice: 'S' <ASTType*>
    // - Array: 'A' <size> <ASTType*>
    // - Path: 'N' <Path>
    // - TraitObject: 'D' <data:GenericPath> <nmarker> [markers: <GenericPath> ...] <naty> [<ASTType*> ...]    TODO: Does this need to include the ATY name?
    // - Borrow: 'B' ('s'|'u'|'o') <ASTType*>
    // - RawPointer: 'P' ('s'|'u'|'o') <ASTType*>
    // - Named Function: 'f' <Path>
    // - Function: 'F' (|'u') (| 'e' <abi:RcString>) <nargs> [args: <ASTType*> ...] <ret:ASTType*>
    // - Primitives::
    //   - u8  : 'C' 'a'
    //   - i8  : 'C' 'b'
    //   - u16 : 'C' 'c'
    //   - i16 : 'C' 'd'
    //   - u32 : 'C' 'e'
    //   - i32 : 'C' 'f'
    //   - u64 : 'C' 'g'
    //   - i64 : 'C' 'h'
    //   - u128: 'C' 'i'
    //   - i128: 'C' 'j'
    //   --
    //   - f32  : 'C' 'n'
    //   - f64  : 'C' 'o'
    //   --
    //   - usize: 'C' 'u'
    //   - isize: 'C' 'v'
    //   - bool : 'C' 'x'
    //   - char : 'C' 'x'
    //   - str  : 'C' 'y'
    // - Diverge: 'C' 'z'
    void fmtType(const HIRTypeData* ty) {
        switch ((*ty).tag()) {
case HIRTypeData::TAG_Infer:
        case HIRTypeData::TAG_Generic:
        case HIRTypeData::TAG_ErasedType:
        case HIRTypeData::TAG_NodeType:
            BUG(Span(), "Non-encodable type " << ty);
            case HIRTypeData::TAG_Tuple: {
                auto& e = (*ty).as_Tuple();
                os << "T" << e.size();
                for (const auto& sty : e) {
                    this->fmtType(sty);
                }
                break;
            }
            case HIRTypeData::TAG_Slice: {
                auto& e = (*ty).as_Slice();
                os << "S";
                this->fmtType(e.inner);
                break;
            }
            case HIRTypeData::TAG_Array: {
                auto& e = (*ty).as_Array();
                os << "A" << e.size.as_Known();
                this->fmtType(e.inner);
                break;
            }
            case HIRTypeData::TAG_Path: {
                auto& e = (*ty).as_Path();
                this->fmtPath(e.path);
                break;
            }
            case HIRTypeData::TAG_TraitObject: {
                auto& e = (*ty).as_TraitObject();
                // - TraitObject: 'D' <data:GenericPath> <naty> [<ASTType*> ...] <nmarker> [markers: <GenericPath> ...]
                os << "D";
                this->fmtGenericPath(e.trait.path);
                os << e.trait.typeBounds.size();
                // HACK: Assume all TraitObject types have the same aty set (std::map is deterministic)
                for (const auto& aty : e.trait.typeBounds) {
                    this->fmtType(aty.second.type);
                }
                os << e.markers.size();
                for (const auto& p : e.markers) {
                    this->fmtGenericPath(p);
                }
                if (includeLifetimeIdentity(e.lifetimeIdentityHasFree) && e.lifetimeIdentity != "") {
                    os << "l";
                    this->fmtName(e.lifetimeIdentity);
                }
                break;
            }
            case HIRTypeData::TAG_NamedFunction: {
                auto& e = (*ty).as_NamedFunction();
                // - Named function: 'f' <path>
                os << "f";
                this->fmtPath(e.path);
                break;
            }
            case HIRTypeData::TAG_Function: {
                auto& e = (*ty).as_Function();
                // - Function: 'F' <abi:RcString> <nargs> [args: <ASTType*> ...] <ret:ASTType*>
                os << "F";
                os << (e.isUnsafe ? "u" : ""); // Optional allowed, next is a number
                os << (e.trackCaller ? "c" : "");
                if (e.abi != ABI_RUST) {
                    os << "e";
                    this->fmtName(e.abi.c_str());
                }
                os << e.argTypes.size();
                for (const auto& t : e.argTypes) {
                    this->fmtType(t);
                }
                this->fmtType(e.rettype);
                if (includeLifetimeIdentity(e.lifetimeIdentityHasFree) && e.lifetimeIdentity != "") {
                    os << "l";
                    this->fmtName(e.lifetimeIdentity);
                }
                break;
            }
            case HIRTypeData::TAG_Borrow: {
                auto& e = (*ty).as_Borrow();
                os << "B";
                switch (e.type) {
                    case HIRBorrowType::Shared:
                        os << "s";
                        break;
                    case HIRBorrowType::Unique:
                        os << "u";
                        break;
                    case HIRBorrowType::Owned:
                        os << "o";
                        break;
                }
                this->fmtType(e.inner);
                break;
            }
            case HIRTypeData::TAG_Pointer: {
                auto& e = (*ty).as_Pointer();
                os << "P";
                switch (e.type) {
                    case HIRBorrowType::Shared:
                        os << "s";
                        break;
                    case HIRBorrowType::Unique:
                        os << "u";
                        break;
                    case HIRBorrowType::Owned:
                        os << "o";
                        break;
                }
                this->fmtType(e.inner);
                break;
            }
            case HIRTypeData::TAG_Primitive: {
                auto& e = (*ty).as_Primitive();
                switch (e) {
                    case HIRCoreType::U8:
                        os << 'C' << 'a';
                        break;
                    case HIRCoreType::I8:
                        os << 'C' << 'b';
                        break;
                    case HIRCoreType::U16:
                        os << 'C' << 'c';
                        break;
                    case HIRCoreType::I16:
                        os << 'C' << 'd';
                        break;
                    case HIRCoreType::U32:
                        os << 'C' << 'e';
                        break;
                    case HIRCoreType::I32:
                        os << 'C' << 'f';
                        break;
                    case HIRCoreType::U64:
                        os << 'C' << 'g';
                        break;
                    case HIRCoreType::I64:
                        os << 'C' << 'h';
                        break;
                    case HIRCoreType::U128:
                        os << 'C' << 'i';
                        break;
                    case HIRCoreType::I128:
                        os << 'C' << 'j';
                        break;
                    case HIRCoreType::F16:
                        os << 'C' << 'm';
                        break;
                    case HIRCoreType::F32:
                        os << 'C' << 'n';
                        break;
                    case HIRCoreType::F64:
                        os << 'C' << 'o';
                        break;
                    case HIRCoreType::F128:
                        os << 'C' << 'p';
                        break;
                    case HIRCoreType::Usize:
                        os << 'C' << 'u';
                        break;
                    case HIRCoreType::Isize:
                        os << 'C' << 'v';
                        break;
                    case HIRCoreType::Bool:
                        os << 'C' << 'w';
                        break;
                    case HIRCoreType::Char:
                        os << 'C' << 'x';
                        break;
                    case HIRCoreType::Str:
                        os << 'C' << 'y';
                        break;
                }
                break;
            }
            case HIRTypeData::TAG_Diverge: {
                os << 'C' << 'z';
                break;
            }
            case HIRTypeData::TAG_Pattern: {
                auto& e = (*ty).as_Pattern();
                os << "Q";
                this->fmtType(e.inner);
                os << e.pattern.alternatives.size() << "r";
                for (const auto& range : e.pattern.alternatives) {
                    os << (range.hasStart ? 's' : 'n');
                    if (range.hasStart) this->fmtConstGeneric(range.start);
                    os << (range.hasEnd ? (range.endInclusive ? 'i' : 'e') : 'n');
                    if (range.hasEnd) this->fmtConstGeneric(range.end);
                }
                break;
            }
        }
    }
};

namespace {
    stl::StringBuilder& mangleBegin(ManglingContext& context) {
        context.buffer.reset();
        context.names.clear();
        return context.buffer;
    }

    RcString mangleFinish(stl::StringBuilder& sb) {
        const auto* data = static_cast<const char*>(sb.data());
        const auto size = static_cast<const char*>(sb.current()) - data;
        auto hash = XXH3_64bits(data, size);
        char symbol[18] = {'Z', 'R'};
        static constexpr char HEX[] = "0123456789abcdef";
        for (size_t i = sizeof(symbol); i > 2; i--) {
            symbol[i - 1] = HEX[hash & 0xf];
            hash >>= 4;
        }
        return RcString::newInterned(symbol, sizeof(symbol));
    }
}

RcString TransMangle(const WireBoard& wb, const HIRSimplePath& p) {
    auto& context = *wb.mangling;
    auto& sb = mangleBegin(context);
    sb << "ZRG";
    Mangler(context).fmtSimplePath(p);
    Mangler(context).fmtPathParams({});
    return mangleFinish(sb);
}

RcString TransMangle(const WireBoard& wb, const HIRGenericPath& p) {
    auto& context = *wb.mangling;
    auto& sb = mangleBegin(context);
    sb << "ZRG";
    Mangler(context).fmtGenericPath(p);
    return mangleFinish(sb);
}

RcString TransMangle(const WireBoard& wb, const HIRPath& p) {
    auto& context = *wb.mangling;
    auto& sb = mangleBegin(context);
    sb << "ZR";
    Mangler(context).fmtPath(p);
    return mangleFinish(sb);
}

RcString TransMangleValue(const WireBoard& wb, const HIRGenericPath& p) {
    auto& context = *wb.mangling;
    auto& sb = mangleBegin(context);
    sb << "ZRG";
    Mangler(context, LifetimeIdentityMode::Closed).fmtGenericPath(p);
    return mangleFinish(sb);
}

RcString TransMangleValue(const WireBoard& wb, const HIRSimplePath& p) {
    return TransMangleValue(wb, HIRGenericPath(p));
}

RcString TransMangleValue(const WireBoard& wb, const HIRPath& p) {
    auto& context = *wb.mangling;
    auto& sb = mangleBegin(context);
    sb << "ZR";
    Mangler(context, LifetimeIdentityMode::Closed).fmtPath(p);
    return mangleFinish(sb);
}

namespace {
    RcString transMangleType(ManglingContext& context, const HIRTypeData* v, bool includeLifetimeIdentity) {
        auto& cache = includeLifetimeIdentity ? context.typeIdCache : context.ordinaryTypeCache;
        if (const auto* hit = cache.find(reinterpret_cast<uintptr_t>(v))) {
            return *hit;
        }
        auto& sb = mangleBegin(context);
        sb << "ZRT";
        Mangler(context, includeLifetimeIdentity ? LifetimeIdentityMode::All : LifetimeIdentityMode::Erased).fmtType(v);
        return *cache.insert(reinterpret_cast<uintptr_t>(v), mangleFinish(sb));
    }
}

RcString TransMangle(const WireBoard& wb, const HIRTypeData* v) {
    return transMangleType(*wb.mangling, v, false);
}

RcString TransMangleTypeId(const WireBoard& wb, const HIRTypeData* v) {
    return transMangleType(*wb.mangling, v, true);
}
