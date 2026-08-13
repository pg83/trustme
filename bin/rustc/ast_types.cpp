#include "ast_types.h"

#include "ast_ast.h"
#include "ast_expr.h"
#include "ast_crate.h"
#include <std/mem/obj_pool.h>

// TypeData is an ordinary inline tagged union now (see ast_types.h); no
// out-of-line implementation is needed.

/// Mappings from internal type names to the core type enum
static const struct {
    const char* name;
    enum eCoreType type;
} CORETYPES[] = {
    // NOTE: Lexographically sorted (hence why 128 comes first)
    {"_", CORETYPE_ANY},
    {"bool", CORETYPE_BOOL},
    {"char", CORETYPE_CHAR},
    {"f128", CORETYPE_F128},
    {"f16", CORETYPE_F16},
    {"f32", CORETYPE_F32},
    {"f64", CORETYPE_F64},
    {"i128", CORETYPE_I128},
    {"i16", CORETYPE_I16},
    {"i32", CORETYPE_I32},
    {"i64", CORETYPE_I64},
    {"i8", CORETYPE_I8},
    //{"int", CORETYPE_INT},
    {"isize", CORETYPE_INT},
    {"str", CORETYPE_STR},
    {"u128", CORETYPE_U128},
    {"u16", CORETYPE_U16},
    {"u32", CORETYPE_U32},
    {"u64", CORETYPE_U64},
    {"u8", CORETYPE_U8},
    //{"uint", CORETYPE_UINT},
    {"usize", CORETYPE_UINT},
};

ASTHigherRankedBounds::ASTHigherRankedBounds() = default;
ASTHigherRankedBounds::~ASTHigherRankedBounds() = default;
ASTHigherRankedBounds::ASTHigherRankedBounds(ASTHigherRankedBounds&&) = default;
ASTHigherRankedBounds& ASTHigherRankedBounds::operator=(ASTHigherRankedBounds&&) = default;
ASTHigherRankedBounds::ASTHigherRankedBounds(const ASTHigherRankedBounds&) = default;

bool ASTHigherRankedBounds::empty() const {
    return mLifetimes.empty();
}

TypeFunction::TypeFunction() = default;

TypeFunction::TypeFunction(ASTHigherRankedBounds hrbs, bool isUnsafe, ::std::string abi, TypeRef ret, ::std::vector<TypeRef> args, bool isVariadic)
    : hrbs(mv$(hrbs))
    , isUnsafe(isUnsafe)
    , mAbi(mv$(abi))
    , mRettype(mv$(ret))
    , argTypes(mv$(args))
    , isVariadic(isVariadic)
{
}

TypeFunction::~TypeFunction() = default;
TypeFunction::TypeFunction(TypeFunction&&) = default;

enum eCoreType coretypeFromstring(const char* name) {
    for (unsigned int i = 0; i < sizeof(CORETYPES) / sizeof(CORETYPES[0]); i++) {
        int cmp = strcmp(name, CORETYPES[i].name);
        if (cmp < 0) {
            break;
        }
        if (cmp == 0) {
            return CORETYPES[i].type;
        }
    }
    return CORETYPE_INVAL;
}

const char* coretypeName(const eCoreType ct) {
    switch (ct) {
        case CORETYPE_INVAL:
            return "INVAL";
        case CORETYPE_ANY:
            return "_/*CORETYPE_ANY*/";
        case CORETYPE_CHAR:
            return "char";
        case CORETYPE_STR:
            return "str";
        case CORETYPE_BOOL:
            return "bool";
        case CORETYPE_UINT:
            return "usize";
        case CORETYPE_INT:
            return "isize";
        case CORETYPE_U8:
            return "u8";
        case CORETYPE_I8:
            return "i8";
        case CORETYPE_U16:
            return "u16";
        case CORETYPE_I16:
            return "i16";
        case CORETYPE_U32:
            return "u32";
        case CORETYPE_I32:
            return "i32";
        case CORETYPE_U64:
            return "u64";
        case CORETYPE_I64:
            return "i64";
        case CORETYPE_U128:
            return "u128";
        case CORETYPE_I128:
            return "i128";
        case CORETYPE_F16:
            return "f16";
        case CORETYPE_F32:
            return "f32";
        case CORETYPE_F64:
            return "f64";
        case CORETYPE_F128:
            return "f128";
    }
    DEBUG("Unknown core type?! " << ct);
    return "NFI";
}

TypeFunction::TypeFunction(const TypeFunction& other)
    : hrbs(other.hrbs)
    , isUnsafe(other.isUnsafe)
    , mAbi(other.mAbi)
    , mRettype(other.mRettype->clone())
    , isVariadic(other.isVariadic)
{
    for (const auto& at : other.argTypes) {
        argTypes.push_back(at->clone());
    }
}

Ordering TypeFunction::ord(const TypeFunction& x) const {
    Ordering rv;

    rv = ::ord(mAbi, x.mAbi);
    if (rv != OrdEqual) {
        return rv;
    }
    rv = ::ord(argTypes, x.argTypes);
    if (rv != OrdEqual) {
        return rv;
    }
    return mRettype->ord(*x.mRettype);
}

TypeRef TypeStore::clone() const {
    struct H {
        static ::std::vector<::TypeRef> cloneTyVec(const ::std::vector<TypeRef>& x) {
            ::std::vector<TypeRef> rv;
            rv.reserve(x.size());
            for (const auto& t : x) {
                rv.push_back(t->clone());
            }
            return rv;
        }
    };

    auto& p = *this->pool;
    switch (mData.tag()) {
        case TypeData::TAGDEAD:
            assert(!"Copying a destructed type");
#define _COPY(VAR)                                                        \
    case TypeData::TAG_##VAR:                                             \
        return mktype(p, mSpan, TypeData::make_##VAR(mData.as_##VAR())); \
        break;
#define _CLONE(VAR, ...)                                       \
    case TypeData::TAG_##VAR: {                                \
        auto& old = mData.as_##VAR();                          \
        return mktype(p, mSpan, TypeData::make_##VAR(__VA_ARGS__)); \
    } break;
            _COPY(None)
            _COPY(Any)
            _COPY(Bang)
            _CLONE(Macro, {p.make<ASTMacroInvocation>(old.inv->clone())})
            _COPY(Unit)
            _COPY(Primitive)
            _COPY(Function)
            _CLONE(Tuple, {H::cloneTyVec(old.innerTypes)})
            _CLONE(Borrow, {ASTLifetimeRef(old.lifetime), old.isMut, old.inner->clone()})
            _CLONE(Pointer, {old.isMut, old.inner->clone()})
            _CLONE(Array, {old.inner->clone(), old.size})
            _CLONE(Slice, {old.inner->clone()})
            _COPY(Generic)
            _CLONE(Path, p.make<ASTPath>(*old))
            _COPY(TraitObject)
            _CLONE(ErasedType, p.make<TypeErasedType>(TypeErasedType{old->traits, old->maybeTraits, old->lifetimes, old->use ? box$(*old->use) : ::std::unique_ptr<ASTPathParams>(), old->isEdition2024OrLater}))
#undef _COPY
#undef _CLONE
    }
    throw "";
}

TypeTraitPath::TypeTraitPath(ASTHigherRankedBounds hrbs, ASTPath path, ASTBoundConstness constness)
    : hrbs(mv$(hrbs))
    , path(box$(path))
    , constness(constness)
{
}

TypeTraitPath::TypeTraitPath() = default;
TypeTraitPath::~TypeTraitPath() = default;
TypeTraitPath::TypeTraitPath(TypeTraitPath&&) = default;

TypeTraitPath::TypeTraitPath(const TypeTraitPath& x)
    : hrbs(x.hrbs)
    , path(std::make_unique<ASTPath>(*x.path))
    , constness(x.constness)
{
}

Ordering TypeTraitPath::ord(const TypeTraitPath& x) const {
    Ordering rv;

    rv = ::ord(static_cast<unsigned>(this->constness), static_cast<unsigned>(x.constness));
    if (rv != OrdEqual) {
        return rv;
    }

    rv = ::ord(*this->path, *x.path);
    if (rv != OrdEqual) {
        return rv;
    }

    return rv;
}

Ordering TypeStore::ord(const TypeStore& x) const {
    Ordering rv;

    rv = ::ord((unsigned)mData.tag(), (unsigned)x.mData.tag());
    if (rv != OrdEqual) {
        return rv;
    }

    TU_MATCH(TypeData, (mData, x.mData), (ent, xEnt), (None, return OrdEqual;), (Macro, throw CompileErrorBugCheck("TypeRef::ord - unexpanded macro");), (Any, return OrdEqual;), (Unit, return OrdEqual;), (Bang, return OrdEqual;), (Primitive, return ::ord((unsigned)ent.coreType, (unsigned)xEnt.coreType);), (Function, return ent.info.ord(xEnt.info);), (Tuple, return ::ord(ent.innerTypes, xEnt.innerTypes);), (Borrow, rv = ::ord(ent.isMut, xEnt.isMut); if (rv != OrdEqual) return rv; return ent.inner->ord(*xEnt.inner);), (Pointer, rv = ::ord(ent.isMut, xEnt.isMut); if (rv != OrdEqual) return rv; return ent.inner->ord(*xEnt.inner);), (Array, rv = ent.inner->ord(*xEnt.inner); if (rv != OrdEqual) return rv; if (ent.size.get()) { throw ::std::runtime_error("TODO: Sized array comparisons"); } return OrdEqual;), (Slice, return ent.inner->ord(*xEnt.inner);), (Generic, return ::ord(ent.name, xEnt.name);), (Path, return ent->ord(*xEnt);), (TraitObject, return ::ord(ent.traits, xEnt.traits);), (ErasedType, ORD(ent->traits, xEnt->traits); ORD(ent->maybeTraits, xEnt->maybeTraits); ORD(ent->lifetimes, xEnt->lifetimes); ORD(ent->use != 0, xEnt->use != 0); if (ent->use) { ORD(*ent->use, *xEnt->use); } ORD(ent->isEdition2024OrLater, xEnt->isEdition2024OrLater); return OrdEqual;))
    throw ::std::runtime_error(FMT("BUGCHECK - Unhandled TypeRef class '" << mData.tag() << "'"));
}

::std::ostream& operator<<(::std::ostream& os, const eCoreType ct) {
    return os << coretypeName(ct);
}

Ordering ord(const TypeRef& a, const TypeRef& b) {
    return a->ord(*b);
}

void TypeStore::print(::std::ostream& os, bool isDebug /*=false*/) const {
#define _(VAR, ...)                               \
    case TypeData::TAG_##VAR: {                   \
        const auto& ent = this->mData.as_##VAR(); \
        (void)&ent;                               \
        __VA_ARGS__                               \
    } break;
#define _2(VAR, brace)                            \
    case TypeData::TAG_##VAR: {                   \
        const auto& ent = this->mData.as_##VAR(); \
        (void)&ent;
    switch (this->mData.tag()) {
        case TypeData::TAGDEAD:
            throw "";
            _(None, os << "!/*none*/!";)
            _(Any, os << "_";)
            _(Bang, os << "!";)
            _(Macro, os << *ent.inv;)
            _(Unit, os << "()";)
            _(Primitive, os << ent.coreType;)
            TU_ARM(mData, Function, ent) {
                os << ent.info.hrbs;
                if (ent.info.mAbi != "") {
                    os << "extern \"" << ent.info.mAbi << "\" ";
                }
                if (ent.info.isUnsafe) {
                    os << "unsafe ";
                }
                os << "fn(";
                for (const auto& arg : ent.info.argTypes) {
                    arg->print(os, isDebug);
                    os << ", ";
                }
                os << ")";
                if (!ent.info.mRettype->isUnit()) {
                    os << " -> " << *ent.info.mRettype;
                }
            }
            break;
            _(Tuple, os << "( "; for (const auto& it : ent.innerTypes) {
                it->print(os, isDebug);
                os << ", ";
            } os << ")";)
            TU_ARM(mData, Borrow, ent) {
                os << "&";
                if (ent.lifetime != ASTLifetimeRef()) {
                    os << ent.lifetime << " ";
                }
                os << (ent.isMut ? "mut " : "");
                ent.inner->print(os, isDebug);
            }
            break;
            _(Pointer, os << "*" << (ent.isMut ? "mut " : "const "); ent.inner->print(os, isDebug);)
            _(Array, os << "["; ent.inner->print(os, isDebug); os << "; "; if (ent.size.get()) { os << *ent.size; } else { os << "_"; } os << "]";)
            _(Slice, os << "["; ent.inner->print(os, isDebug); os << "]";)
            _(Generic, if (isDebug) os << "/* arg */ "; os << ent.name; if (isDebug) os << "/*" << ent.index << "*/";)
            _(Path, ent->printPretty(os, true, isDebug);)
            _(TraitObject, os << "("; bool needsPlus = false; for (const auto& it : ent.traits) {
                if (needsPlus) {
                    os << "+";
                }
                needsPlus = true;
                os << it.hrbs;
                if (it.constness == ASTBoundConstness::Always) {
                    os << "const ";
                } else if (it.constness == ASTBoundConstness::Maybe) {
                    os << "[const] ";
                }
                it.path->printPretty(os, true, isDebug);
            } for (const auto& it : ent.lifetimes) {
                if (it.binding() != ASTLifetimeRef::BINDING_UNSPECIFIED) {
                    if (needsPlus) {
                        os << "+";
                    }
                    needsPlus = true;
                    os << it;
                }
            } os << ")";)
            _(ErasedType, os << "impl "; bool needsPlus = false; for (const auto& it : ent->traits) {
                if (needsPlus) {
                    os << "+";
                }
                needsPlus = true;
                os << it.hrbs;
                if (it.constness == ASTBoundConstness::Always) {
                    os << "const ";
                } else if (it.constness == ASTBoundConstness::Maybe) {
                    os << "[const] ";
                }
                it.path->printPretty(os, true, isDebug);
            } for (const auto& it : ent->maybeTraits) {
                if (needsPlus) {
                    os << "+";
                }
                needsPlus = true;
                os << it.hrbs;
                it.path->printPretty(os, true, isDebug);
            } for (const auto& it : ent->lifetimes) {
                if (needsPlus) {
                    os << "+";
                }
                needsPlus = true;
                os << it;
            } if (ent->use) { os << "use" << *ent->use; } os << "";)
    }
#undef _
#undef _2
}

::std::ostream& operator<<(::std::ostream& os, const TypeStore& tr) {
    tr.print(os, true);
    return os;
}

::std::ostream& operator<<(::std::ostream& os, const PrettyPrintType& x) {
    x.mType->print(os, false);
    return os;
}

::std::ostream& operator<<(::std::ostream& os, const ASTLifetimeRef& x) {
    if (x.mBinding == ASTLifetimeRef::BINDING_STATIC) {
        os << "'static";
    } else if (x.mBinding == ASTLifetimeRef::BINDING_INFER) {
        os << "'_";
    } else if (x.mBinding == ASTLifetimeRef::BINDING_UNSPECIFIED) {
        os << "/*'UNSPEC*/";
    } else {
        os << "'" << x.mName.name;
        if (x.mBinding != ASTLifetimeRef::BINDING_UNBOUND) {
            os << "/*" << x.mBinding << "*/";
        }
    }
    return os;
}

PrettyPrintType::PrettyPrintType(const TypeStore* ty)
    : mType(ty)
{
}

// ------------------------------------------------------------------------
// Type node pool + factories
// ------------------------------------------------------------------------
static stl::ObjPool* g_ast_type_pool = nullptr;

void setAstTypePool(stl::ObjPool& pool) {
    g_ast_type_pool = &pool;
}
stl::ObjPool& astTypePool() {
    assert(g_ast_type_pool && "AST type pool not set - call setAstTypePool()");
    return *g_ast_type_pool;
}

TypeRef mktype(stl::ObjPool& pool, Span sp, TypeData data) {
    return pool.make<TypeStore>(mv$(sp), mv$(data), &pool);
}

TypeRef mktype(Span sp) {
    return mktype(sp, TypeData::make_Any({}));
}
TypeRef mktype(TypeRefTags::Invalid, Span sp) {
    return mktype(sp, TypeData::make_None({}));
}
TypeRef mktype(TypeRefTags::Macro, ASTMacroInvocation inv) {
    auto sp = inv.span();
    auto& p = astTypePool();
    return mktype(p, sp, TypeData::make_Macro({p.make<ASTMacroInvocation>(mv$(inv))}));
}
TypeRef mktype(TypeRefTags::Unit, Span sp) {
    return mktype(sp, TypeData::make_Unit({}));
}
TypeRef mktype(TypeRefTags::Primitive, Span sp, enum eCoreType type) {
    return mktype(sp, TypeData::make_Primitive({type}));
}
TypeRef mktype(Span sp, enum eCoreType type) {
    return mktype(sp, TypeData::make_Primitive({type}));
}
TypeRef mktype(TypeRefTags::Tuple, Span sp, ::std::vector<TypeRef> innerTypes) {
    return mktype(sp, TypeData::make_Tuple({mv$(innerTypes)}));
}
TypeRef mktype(TypeRefTags::Function, Span sp, ASTHigherRankedBounds hrbs, bool isUnsafe, ::std::string abi, ::std::vector<TypeRef> args, bool isVariadic, TypeRef ret) {
    return mktype(sp, TypeData::make_Function({TypeFunction(mv$(hrbs), isUnsafe, abi, ret, mv$(args), isVariadic)}));
}
TypeRef mktype(TypeRefTags::Reference, Span sp, ASTLifetimeRef lft, bool isMut, TypeRef innerType) {
    return mktype(sp, TypeData::make_Borrow({mv$(lft), isMut, innerType}));
}
TypeRef mktype(TypeRefTags::Pointer, Span sp, bool isMut, TypeRef innerType) {
    return mktype(sp, TypeData::make_Pointer({isMut, innerType}));
}
TypeRef mktype(TypeRefTags::SizedArray, Span sp, TypeRef innerType, ::std::shared_ptr<ASTExprNode> size) {
    return mktype(sp, TypeData::make_Array({innerType, mv$(size)}));
}
TypeRef mktype(TypeRefTags::UnsizedArray, Span sp, TypeRef innerType) {
    return mktype(sp, TypeData::make_Slice({innerType}));
}
TypeRef mktype(TypeRefTags::Arg, Span sp, RcString name, unsigned int binding) {
    return mktype(sp, TypeData::make_Generic({mv$(name), binding}));
}
TypeRef mktype(Span sp, RcString name, unsigned int binding) {
    return mktype(TypeRefTags::Arg(), sp, mv$(name), binding);
}
TypeRef mktype(TypeRefTags::Path, Span sp, ASTPath path) {
    auto& p = astTypePool();
    return mktype(p, sp, TypeData::make_Path(p.make<ASTPath>(mv$(path))));
}
TypeRef mktype(Span sp, ASTPath path) {
    return mktype(TypeRefTags::Path(), sp, mv$(path));
}
TypeRef mktype(Span sp, ::std::vector<TypeTraitPath> traits, ::std::vector<ASTLifetimeRef> lifetimes) {
    return mktype(sp, TypeData::make_TraitObject({mv$(traits), mv$(lifetimes)}));
}
