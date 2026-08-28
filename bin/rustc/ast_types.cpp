#include "ast_types.h"

#include "ast_ast.h"
#include "ast_expr.h"
#include "ast_pattern.h"
#include "ast_crate.h"
#include <std/mem/obj_pool.h>

using namespace stl;

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
    return lifetimes.empty() && types.empty();
}

TypeFunction::TypeFunction() = default;

TypeFunction::TypeFunction(ASTHigherRankedBounds hrbs, bool isUnsafe, ::std::string abi, ASTType* ret, ::std::vector<ASTType*> args, bool isVariadic)
    : hrbs(mv$(hrbs))
    , isUnsafe(isUnsafe)
    , abi(mv$(abi))
    , rettype(mv$(ret))
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
    return "NFI";
}

TypeFunction::TypeFunction(const TypeFunction& other)
    : hrbs(other.hrbs)
    , isUnsafe(other.isUnsafe)
    , abi(other.abi)
    , rettype(other.rettype->clone())
    , isVariadic(other.isVariadic)
{
    for (const auto& at : other.argTypes) {
        argTypes.push_back(at->clone());
    }
}

Ordering TypeFunction::ord(const TypeFunction& x) const {
    Ordering rv;

    rv = ::ord(abi, x.abi);
    if (rv != OrdEqual) {
        return rv;
    }
    rv = ::ord(argTypes, x.argTypes);
    if (rv != OrdEqual) {
        return rv;
    }
    return rettype->ord(*x.rettype);
}

ASTType* ASTType::clone() const {
    struct H {
        static ::std::vector<::ASTType*> cloneTyVec(const ::std::vector<ASTType*>& x) {
            ::std::vector<ASTType*> rv;
            rv.reserve(x.size());
            for (const auto& t : x) {
                rv.push_back(t->clone());
            }
            return rv;
        }
    };

    auto& p = *this->pool;
    switch (data.tag()) {
#define _COPY(VAR)                                                        \
    case TypeData::TAG_##VAR:                                             \
        return mkType(p, span_, TypeData::make_##VAR(data.as_##VAR())); \
        break;
#define _CLONE(VAR, ...)                                       \
    case TypeData::TAG_##VAR: {                                \
        auto& old = data.as_##VAR();                          \
        return mkType(p, span_, TypeData::make_##VAR(__VA_ARGS__)); \
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
            _CLONE(Pattern, {old.inner->clone(), p.make<ASTPattern>(old.pattern->clone())})
            _COPY(Generic)
            _CLONE(Path, p.make<ASTPath>(*old))
            _COPY(TraitObject)
            _CLONE(ErasedType, p.make<TypeErasedType>(TypeErasedType{old->traits, old->maybeTraits, old->lifetimes, old->use ? box$(*old->use) : ::std::unique_ptr<ASTPathParams>(), old->isEdition2024OrLater}))
#undef _COPY
#undef _CLONE
    }
    UNREACHABLE();
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
    , isAsync(x.isAsync)
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

Ordering ASTType::ord(const ASTType& x) const {
    Ordering rv;

    rv = ::ord((unsigned)data.tag(), (unsigned)x.data.tag());
    if (rv != OrdEqual) {
        return rv;
    }

    switch (data.tag()) {
        case TypeData::TAG_None: {
            return OrdEqual;
        }
        case TypeData::TAG_Macro: {
            compileErrorBugCheck("ASTType*::ord - unexpanded macro");
        }
        case TypeData::TAG_Any: {
            return OrdEqual;
        }
        case TypeData::TAG_Unit: {
            return OrdEqual;
        }
        case TypeData::TAG_Bang: {
            return OrdEqual;
        }
        case TypeData::TAG_Primitive: {
            auto& ent = data.as_Primitive();
            auto& xEnt = x.data.as_Primitive();
            return ::ord((unsigned)ent.coreType, (unsigned)xEnt.coreType);
        }
        case TypeData::TAG_Function: {
            auto& ent = data.as_Function();
            auto& xEnt = x.data.as_Function();
            return ent.info.ord(xEnt.info);
        }
        case TypeData::TAG_Tuple: {
            auto& ent = data.as_Tuple();
            auto& xEnt = x.data.as_Tuple();
            return ::ord(ent.innerTypes, xEnt.innerTypes);
        }
        case TypeData::TAG_Borrow: {
            auto& ent = data.as_Borrow();
            auto& xEnt = x.data.as_Borrow();
            rv = ::ord(ent.isMut, xEnt.isMut); if (rv != OrdEqual) return rv; return ent.inner->ord(*xEnt.inner);
            break;
        }
        case TypeData::TAG_Pointer: {
            auto& ent = data.as_Pointer();
            auto& xEnt = x.data.as_Pointer();
            rv = ::ord(ent.isMut, xEnt.isMut); if (rv != OrdEqual) return rv; return ent.inner->ord(*xEnt.inner);
            break;
        }
        case TypeData::TAG_Array: {
            auto& ent = data.as_Array();
            auto& xEnt = x.data.as_Array();
            rv = ent.inner->ord(*xEnt.inner); if (rv != OrdEqual) return rv; if (ent.size.get()) { throw ::std::runtime_error("TODO: Sized array comparisons"); } return OrdEqual;
            break;
        }
        case TypeData::TAG_Slice: {
            auto& ent = data.as_Slice();
            auto& xEnt = x.data.as_Slice();
            return ent.inner->ord(*xEnt.inner);
        }
        case TypeData::TAG_Pattern: {
            auto& ent = data.as_Pattern();
            auto& xEnt = x.data.as_Pattern();
            rv = ent.inner->ord(*xEnt.inner); if (rv != OrdEqual) return rv; return ::ord(*ent.pattern, *xEnt.pattern);
            break;
        }
        case TypeData::TAG_Generic: {
            auto& ent = data.as_Generic();
            auto& xEnt = x.data.as_Generic();
            return ::ord(ent.name, xEnt.name);
        }
        case TypeData::TAG_Path: {
            auto& ent = data.as_Path();
            auto& xEnt = x.data.as_Path();
            return ent->ord(*xEnt);
        }
        case TypeData::TAG_TraitObject: {
            auto& ent = data.as_TraitObject();
            auto& xEnt = x.data.as_TraitObject();
            return ::ord(ent.traits, xEnt.traits);
        }
        case TypeData::TAG_ErasedType: {
            auto& ent = data.as_ErasedType();
            auto& xEnt = x.data.as_ErasedType();
            ORD(ent->traits, xEnt->traits); ORD(ent->maybeTraits, xEnt->maybeTraits); ORD(ent->lifetimes, xEnt->lifetimes); ORD(ent->use != 0, xEnt->use != 0); if (ent->use) { ORD(*ent->use, *xEnt->use); } ORD(ent->isEdition2024OrLater, xEnt->isEdition2024OrLater); return OrdEqual;
            break;
        }
    }
    throw ::std::runtime_error(FMT("BUGCHECK - Unhandled ASTType* class '" << data.tag() << "'"));
}

::std::ostream& operator<<(::std::ostream& os, const eCoreType ct) {
    return os << coretypeName(ct);
}

Ordering ord(ASTType* a, ASTType* b) {
    return a->ord(*b);
}

void ASTType::print(::std::ostream& os, bool isDebug /*=false*/) const {
#define _(VAR, ...)                               \
    case TypeData::TAG_##VAR: {                   \
        const auto& ent = this->data.as_##VAR(); \
        (void)&ent;                               \
        __VA_ARGS__                               \
    } break;
#define _2(VAR, brace)                            \
    case TypeData::TAG_##VAR: {                   \
        const auto& ent = this->data.as_##VAR(); \
        (void)&ent;
    switch (this->data.tag()) {
            _(None, os << "!/*none*/!";)
            _(Any, os << "_";)
            _(Bang, os << "!";)
            _(Macro, os << *ent.inv;)
            _(Unit, os << "()";)
            _(Primitive, os << ent.coreType;)
            break;
            case TypeData::TAG_Function: {
                auto& ent = data.as_Function();
                os << ent.info.hrbs;
                if (ent.info.abi != "") {
                    os << "extern \"" << ent.info.abi << "\" ";
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
                if (!ent.info.rettype->isUnit()) {
                    os << " -> " << *ent.info.rettype;
                }

            }
            break;
            _(Tuple, os << "( "; for (const auto& it : ent.innerTypes) {
                it->print(os, isDebug);
                os << ", ";
            } os << ")";)
            break;
            case TypeData::TAG_Borrow: {
                auto& ent = data.as_Borrow();
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
            _(Pattern, ent.inner->print(os, isDebug); os << " is " << *ent.pattern;)
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

::std::ostream& operator<<(::std::ostream& os, const ASTType& tr) {
    tr.print(os, true);
    return os;
}

::std::ostream& operator<<(::std::ostream& os, const PrettyPrintType& x) {
    x.type_->print(os, false);
    return os;
}

::std::ostream& operator<<(::std::ostream& os, const ASTLifetimeRef& x) {
    if (x.binding_ == ASTLifetimeRef::BINDING_STATIC) {
        os << "'static";
    } else if (x.binding_ == ASTLifetimeRef::BINDING_INFER) {
        os << "'_";
    } else if (x.binding_ == ASTLifetimeRef::BINDING_UNSPECIFIED) {
        os << "/*'UNSPEC*/";
    } else {
        os << "'" << x.name_.name;
        if (x.binding_ != ASTLifetimeRef::BINDING_UNBOUND) {
            os << "/*" << x.binding_ << "*/";
        }
    }
    return os;
}

PrettyPrintType::PrettyPrintType(const ASTType* ty)
    : type_(ty)
{
}

// ------------------------------------------------------------------------
// Type node pool + factories
// ------------------------------------------------------------------------
ASTType* mkType(ObjPool& pool, Span sp, TypeData data) {
    return pool.make<ASTType>(mv$(sp), mv$(data), &pool);
}

ASTType* mkType(ObjPool& pool, Span sp) {
    return mkType(pool, sp, TypeData::make_Any({}));
}
ASTType* mkType(ObjPool& pool, ASTTypeTags::Invalid, Span sp) {
    return mkType(pool, sp, TypeData::make_None({}));
}
ASTType* mkType(ObjPool& pool, ASTTypeTags::Macro, ASTMacroInvocation inv) {
    auto sp = inv.span();
    return mkType(pool, sp, TypeData::make_Macro({pool.make<ASTMacroInvocation>(mv$(inv))}));
}
ASTType* mkType(ObjPool& pool, ASTTypeTags::Unit, Span sp) {
    return mkType(pool, sp, TypeData::make_Unit({}));
}
ASTType* mkType(ObjPool& pool, ASTTypeTags::Primitive, Span sp, enum eCoreType type) {
    return mkType(pool, sp, TypeData::make_Primitive({type}));
}
ASTType* mkType(ObjPool& pool, Span sp, enum eCoreType type) {
    return mkType(pool, sp, TypeData::make_Primitive({type}));
}
ASTType* mkType(ObjPool& pool, ASTTypeTags::Tuple, Span sp, ::std::vector<ASTType*> innerTypes) {
    return mkType(pool, sp, TypeData::make_Tuple({mv$(innerTypes)}));
}
ASTType* mkType(ObjPool& pool, ASTTypeTags::Function, Span sp, ASTHigherRankedBounds hrbs, bool isUnsafe, ::std::string abi, ::std::vector<ASTType*> args, bool isVariadic, ASTType* ret) {
    return mkType(pool, sp, TypeData::make_Function({TypeFunction(mv$(hrbs), isUnsafe, abi, ret, mv$(args), isVariadic)}));
}
ASTType* mkType(ObjPool& pool, ASTTypeTags::Reference, Span sp, ASTLifetimeRef lft, bool isMut, ASTType* innerType, bool isPin) {
    return mkType(pool, sp, TypeData::make_Borrow({mv$(lft), isMut, innerType, isPin}));
}
ASTType* mkType(ObjPool& pool, ASTTypeTags::Pointer, Span sp, bool isMut, ASTType* innerType) {
    return mkType(pool, sp, TypeData::make_Pointer({isMut, innerType}));
}
ASTType* mkType(ObjPool& pool, ASTTypeTags::SizedArray, Span sp, ASTType* innerType, ::std::shared_ptr<ASTExprNode> size) {
    return mkType(pool, sp, TypeData::make_Array({innerType, mv$(size)}));
}
ASTType* mkType(ObjPool& pool, ASTTypeTags::UnsizedArray, Span sp, ASTType* innerType) {
    return mkType(pool, sp, TypeData::make_Slice({innerType}));
}
ASTType* mkType(ObjPool& pool, ASTTypeTags::Arg, Span sp, RcString name, unsigned int binding) {
    return mkType(pool, sp, TypeData::make_Generic({mv$(name), binding}));
}
ASTType* mkType(ObjPool& pool, Span sp, RcString name, unsigned int binding) {
    return mkType(pool, ASTTypeTags::Arg(), sp, mv$(name), binding);
}
ASTType* mkType(ObjPool& pool, ASTTypeTags::Path, Span sp, ASTPath path) {
    return mkType(pool, sp, TypeData::make_Path(pool.make<ASTPath>(mv$(path))));
}
ASTType* mkType(ObjPool& pool, Span sp, ASTPath path) {
    return mkType(pool, ASTTypeTags::Path(), sp, mv$(path));
}
ASTType* mkType(ObjPool& pool, Span sp, ::std::vector<TypeTraitPath> traits, ::std::vector<ASTLifetimeRef> lifetimes) {
    return mkType(pool, sp, TypeData::make_TraitObject({mv$(traits), mv$(lifetimes)}));
}
