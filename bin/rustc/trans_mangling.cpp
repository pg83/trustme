#include "trans_mangling.h"

#include "debug.h"
#include "hir_hir.h" // ABI_RUST
#include "hir_type.h"

#include <cmath> // ceil/log10
#include <cctype>
#include <algorithm> // std::find

class Mangler {
    ::std::ostream& os;
    std::vector<RcString> nameCache;

public:
    Mangler(::std::ostream& os)
        : os(os)
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
        auto it = std::find(nameCache.begin(), nameCache.end(), s);
        if (it != nameCache.end()) {
            auto idx = it - nameCache.begin();
            // Only emit this way if shorter than the formatted name would be.
            auto len = 1 + static_cast<unsigned>(std::ceil(std::log10(idx + 1) / std::log10(26)));
            if (len < s.size()) {
                os << "_";
                fmtBase26Int(idx);
                return;
            }
        } else {
            nameCache.push_back(s);
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

    // PathParams : <ntys> 'g' [<TypeRef> ...]
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
            const auto& ev = *v.as_Evaluated();
            os << "V";
            os << ev.bytes.size();
            os << "_";
            // TODO: Base64 data? (`_` and `$` as the other two?)
            for (size_t i = 0; i < ev.bytes.size(); i++) {
                os << "0123456789abcdef"[ev.bytes[i] >> 4];
                os << "0123456789abcdef"[ev.bytes[i] & 0xF];
            }
            if (ev.relocations.size() > 0) {
                os << "_" << ev.relocations.size() << "R";
                TODO(Span(), "Mangle relocated values");
            }
        }
    }

    // GenericPath : <SimplePath> <PathParams>
    void fmtGenericPath(const HIRGenericPath& gp) {
        this->fmtSimplePath(gp.mPath);
        this->fmtPathParams(gp.mParams);
    }

    void fmtPath(const HIRPath& p) {
        // Path type
        // - Generic: starts with `G`
        // - Inherent: Starts with `I`
        // - Trait: Starts with `Q` (qualified)
        // - bare type: Starts with `T` (see Trans_MangleType)
        TU_MATCH_HDRA( (p.mData), {)
        TU_ARMA(Generic, e) {
                os << "G";
                this->fmtGenericPath(e);
            }
            TU_ARMA(UfcsInherent, e) {
                os << "I";
                this->fmtType(e.type);
                this->fmtName(e.item);
                this->fmtPathParams(e.params);
            }
            TU_ARMA(UfcsKnown, e) {
                os << "Q";
                this->fmtType(e.type);
                this->fmtGenericPath(e.trait);
                this->fmtName(e.item);
                this->fmtPathParams(e.params);
            }
            TU_ARMA(UfcsUnknown, e)
            BUG(Span(), "Non-encodable path " << p);
        }
    }

    // Type
    // - Tuple: 'T' <nelem> [<TypeRef> ...]
    // - Slice: 'S' <TypeRef>
    // - Array: 'A' <size> <TypeRef>
    // - Path: 'N' <Path>
    // - TraitObject: 'D' <data:GenericPath> <nmarker> [markers: <GenericPath> ...] <naty> [<TypeRef> ...]    TODO: Does this need to include the ATY name?
    // - Borrow: 'B' ('s'|'u'|'o') <TypeRef>
    // - RawPointer: 'P' ('s'|'u'|'o') <TypeRef>
    // - Named Function: 'f' <Path>
    // - Function: 'F' (|'u') (| 'e' <abi:RcString>) <nargs> [args: <TypeRef> ...] <ret:TypeRef>
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
        TU_MATCH_HDRA( ((*ty)), { )
        case HIRTypeData::TAG_Infer:
        case HIRTypeData::TAG_Generic:
        case HIRTypeData::TAG_ErasedType:
        case HIRTypeData::TAG_NodeType:
            BUG(Span(), "Non-encodable type " << ty);
            TU_ARMA(Tuple, e) {
                os << "T" << e.size();
                for (const auto& sty : e) {
                    this->fmtType(sty);
                }
            }
            TU_ARMA(Slice, e) {
                os << "S";
                this->fmtType(e.inner);
            }
            TU_ARMA(Array, e) {
                os << "A" << e.size.as_Known();
                this->fmtType(e.inner);
            }
            TU_ARMA(Path, e) {
                this->fmtPath(e.path);
            }
            TU_ARMA(TraitObject, e) {
                // - TraitObject: 'D' <data:GenericPath> <naty> [<TypeRef> ...] <nmarker> [markers: <GenericPath> ...]
                os << "D";
                this->fmtGenericPath(e.mTrait.mPath);
                os << e.mTrait.typeBounds.size();
                // HACK: Assume all TraitObject types have the same aty set (std::map is deterministic)
                for (const auto& aty : e.mTrait.typeBounds) {
                    this->fmtType(aty.second.type);
                }
                os << e.markers.size();
                for (const auto& p : e.markers) {
                    this->fmtGenericPath(p);
                }
            }
            TU_ARMA(NamedFunction, e) {
                // - Named function: 'f' <path>
                os << "f";
                this->fmtPath(e.path);
            }
            TU_ARMA(Function, e) {
                // - Function: 'F' <abi:RcString> <nargs> [args: <TypeRef> ...] <ret:TypeRef>
                os << "F";
                os << (e.isUnsafe ? "u" : ""); // Optional allowed, next is a number
                if (e.mAbi != ABI_RUST) {
                    os << "e";
                    this->fmtName(e.mAbi.c_str());
                }
                os << e.argTypes.size();
                for (const auto& t : e.argTypes) {
                    this->fmtType(t);
                }
                this->fmtType(e.mRettype);
            }
            TU_ARMA(Borrow, e) {
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
            }
            TU_ARMA(Pointer, e) {
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
            }
            TU_ARMA(Primitive, e) {
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
            }
            TU_ARMA(Diverge, _e) {
                os << 'C' << 'z';
            }
        }
    }
};

::FmtLambda TransManglePath(const HIRPath& p) {
    return FMT_CB(os, os << "ZR"; Mangler(os).fmtPath(p));
}

::FmtLambda TransMangleSimplePath(const HIRSimplePath& p) {
    return FMT_CB(os, os << "ZRG"; Mangler(os).fmtSimplePath(p); Mangler(os).fmtPathParams({}););
}

::FmtLambda TransMangleGenericPath(const HIRGenericPath& p) {
    return FMT_CB(os, os << "ZRG"; Mangler(os).fmtGenericPath(p));
}

::FmtLambda TransMangleTypeRef(const HIRTypeData* p) {
    return ::FmtLambda([p](::std::ostream& os) {
        os << "ZRT";
        Mangler(os).fmtType(p);
    });
}

namespace {
    ::FmtLambda maxLen(::FmtLambda v) {
        std::stringstream ss;
        ss << v;
        auto s = ss.str();
        static const size_t MAX_LEN = 128;
        if (s.size() > 128) {
            size_t hash = ::std::hash<std::string>()(s);
            ss.str("");
            ss << s.substr(0, MAX_LEN - 9) << "$" << ::std::hex << hash;
            DEBUG("Over-long symbol '" << s << "' -> '" << ss.str() << "'");
            s = ss.str();
        } else {
        }
        return ::FmtLambda([=](::std::ostream& os) {
            os << s;
        });
    }
}

// TODO: If the mangled name exceeds a limit, stop emitting the real name and start hashing the rest.
#define DO_MANGLE(ty, suffix)                  \
    ::FmtLambda TransMangle(const ty& v) {     \
        return maxLen(TransMangle##suffix(v)); \
    }
DO_MANGLE(HIRSimplePath, SimplePath)
DO_MANGLE(HIRGenericPath, GenericPath)
DO_MANGLE(HIRPath, Path)

::FmtLambda TransMangle(const HIRTypeData* v) {
    return maxLen(TransMangleTypeRef(v));
}
