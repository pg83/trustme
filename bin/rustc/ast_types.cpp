#include "ast_types.h"

#include "output.h"
#include "ast_ast.h"
#include "ast_expr.h"
#include "ast_crate.h"
#include "ast_pattern.h"

#include <std/lib/vector.h>
#include <std/mem/obj_pool.h>

using namespace stl;

ASTHigherRankedBounds::ASTHigherRankedBounds() = default;
ASTHigherRankedBounds::~ASTHigherRankedBounds() = default;
ASTHigherRankedBounds::ASTHigherRankedBounds(ASTHigherRankedBounds&&) = default;
ASTHigherRankedBounds& ASTHigherRankedBounds::operator=(ASTHigherRankedBounds&&) = default;
ASTHigherRankedBounds::ASTHigherRankedBounds(const ASTHigherRankedBounds&) = default;

bool ASTHigherRankedBounds::empty() const {
    return lifetimes.empty() && types.empty();
}

TypeFunction::TypeFunction() = default;

TypeFunction::TypeFunction(ASTHigherRankedBounds hrbs, bool isUnsafe, std::string abi, ASTType* ret, Vector<ASTType*> args, bool isVariadic)
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

TypeFunction::TypeFunction(const TypeFunction& other)
    : hrbs(other.hrbs)
    , isUnsafe(other.isUnsafe)
    , abi(other.abi)
    , rettype(other.rettype->clone())
    , isVariadic(other.isVariadic)
{
    for (const auto& at : other.argTypes) {
        argTypes.pushBack(at->clone());
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
        static Vector<::ASTType*> cloneTyVec(const Vector<ASTType*>& x) {
            Vector<ASTType*> rv;
            rv.grow(x.length());
            for (const auto& t : x) {
                rv.pushBack(t->clone());
            }
            return rv;
        }
    };

    auto& p = *this->pool;
    switch (data.tag()) {
#define _COPY(VAR)                                                      \
    case TypeData::TAG_##VAR:                                           \
        return mkType(p, span_, TypeData::make_##VAR(data.as_##VAR())); \
        break;
#define _CLONE(VAR, ...)                                            \
    case TypeData::TAG_##VAR: {                                     \
        auto& old = data.as_##VAR();                                \
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
        _CLONE(ErasedType, p.make<TypeErasedType>(TypeErasedType{old->traits, old->maybeTraits, old->lifetimes, old->use ? box$(*old->use) : std::unique_ptr<ASTPathParams>(), old->isEdition2024OrLater}))
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
            rv = ::ord(ent.isMut, xEnt.isMut);
            if (rv != OrdEqual) {
                return rv;
            }
            return ent.inner->ord(*xEnt.inner);
            break;
        }
        case TypeData::TAG_Pointer: {
            auto& ent = data.as_Pointer();
            auto& xEnt = x.data.as_Pointer();
            rv = ::ord(ent.isMut, xEnt.isMut);
            if (rv != OrdEqual) {
                return rv;
            }
            return ent.inner->ord(*xEnt.inner);
            break;
        }
        case TypeData::TAG_Array: {
            auto& ent = data.as_Array();
            auto& xEnt = x.data.as_Array();
            rv = ent.inner->ord(*xEnt.inner);
            if (rv != OrdEqual) {
                return rv;
            }
            if (ent.size) {
                TODO(Span(), StringView("Sized array comparisons"));
            }
            return OrdEqual;
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
            rv = ent.inner->ord(*xEnt.inner);
            if (rv != OrdEqual) {
                return rv;
            }
            return ::ord(*ent.pattern, *xEnt.pattern);
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
            ORD(ent->traits, xEnt->traits);
            ORD(ent->maybeTraits, xEnt->maybeTraits);
            ORD(ent->lifetimes, xEnt->lifetimes);
            ORD(ent->use != 0, xEnt->use != 0);
            if (ent->use) {
                ORD(*ent->use, *xEnt->use);
            }
            ORD(ent->isEdition2024OrLater, xEnt->isEdition2024OrLater);
            return OrdEqual;
            break;
        }
    }
    BUG(Span(), StringView("Unhandled ASTType* class '") << data.tag() << StringView("'"));
}

Ordering ord(ASTType* a, ASTType* b) {
    return a->ord(*b);
}

void ASTType::print(ZeroCopyOutput& os, bool isDebug /*=false*/) const {
#define _(VAR, ...)                              \
    case TypeData::TAG_##VAR: {                  \
        const auto& ent = this->data.as_##VAR(); \
        (void)&ent;                              \
        __VA_ARGS__                              \
    } break;
#define _2(VAR, brace)                           \
    case TypeData::TAG_##VAR: {                  \
        const auto& ent = this->data.as_##VAR(); \
        (void)&ent;
    switch (this->data.tag()) {
        _(None, os << StringView("!/*none*/!");)
        _(Any, os << StringView("_");)
        _(Bang, os << StringView("!");)
        _(Macro, os << *ent.inv;)
        _(Unit, os << StringView("()");)
        _(Primitive, os << ent.coreType;)
        break;
        case TypeData::TAG_Function: {
            auto& ent = data.as_Function();
            os << ent.info.hrbs;
            if (ent.info.abi != "") {
                os << StringView("extern \"") << ent.info.abi << StringView("\" ");
            }
            if (ent.info.isUnsafe) {
                os << StringView("unsafe ");
            }
            os << StringView("fn(");
            for (const auto& arg : ent.info.argTypes) {
                arg->print(os, isDebug);
                os << StringView(", ");
            }
            os << StringView(")");
            if (!ent.info.rettype->isUnit()) {
                os << StringView(" -> ") << *ent.info.rettype;
            }
        } break;
            _(Tuple, os << StringView("( "); for (const auto& it : ent.innerTypes) {
                it->print(os, isDebug);
                os << StringView(", ");
            } os << StringView(")");)
            break;
        case TypeData::TAG_Borrow: {
            auto& ent = data.as_Borrow();
            os << StringView("&");
            if (ent.lifetime != ASTLifetimeRef()) {
                os << ent.lifetime << StringView(" ");
            }
            os << (ent.isMut ? "mut " : "");
            ent.inner->print(os, isDebug);
        } break;
            _(Pointer, os << StringView("*") << (ent.isMut ? "mut " : "const "); ent.inner->print(os, isDebug);)
            _(Array, os << StringView("["); ent.inner->print(os, isDebug); os << StringView("; "); if (ent.size) { os << *ent.size; } else { os << StringView("_"); } os << StringView("]");)
            _(Slice, os << StringView("["); ent.inner->print(os, isDebug); os << StringView("]");)
            _(Pattern, ent.inner->print(os, isDebug); os << StringView(" is ") << *ent.pattern;)
            _(Generic, if (isDebug) os << StringView("/* arg */ "); os << ent.name; if (isDebug) os << StringView("/*") << ent.index << StringView("*/");)
            _(Path, ent->printPretty(os, true, isDebug);)
            _(TraitObject, os << StringView("("); bool needsPlus = false; for (const auto& it : ent.traits) {
                if (needsPlus) {
                    os << StringView("+");
                }
                needsPlus = true;
                os << it.hrbs;
                if (it.constness == ASTBoundConstness::Always) {
                    os << StringView("const ");
                } else if (it.constness == ASTBoundConstness::Maybe) {
                    os << StringView("[const] ");
                }
                it.path->printPretty(os, true, isDebug);
            } for (const auto& it : ent.lifetimes) {
                if (it.binding() != ASTLifetimeRef::BINDING_UNSPECIFIED) {
                    if (needsPlus) {
                        os << StringView("+");
                    }
                    needsPlus = true;
                    os << it;
                }
            } os << StringView(")");)
            _(ErasedType, os << StringView("impl "); bool needsPlus = false; for (const auto& it : ent->traits) {
                if (needsPlus) {
                    os << StringView("+");
                }
                needsPlus = true;
                os << it.hrbs;
                if (it.constness == ASTBoundConstness::Always) {
                    os << StringView("const ");
                } else if (it.constness == ASTBoundConstness::Maybe) {
                    os << StringView("[const] ");
                }
                it.path->printPretty(os, true, isDebug);
            } for (const auto& it : ent->maybeTraits) {
                if (needsPlus) {
                    os << StringView("+");
                }
                needsPlus = true;
                os << it.hrbs;
                it.path->printPretty(os, true, isDebug);
            } for (const auto& it : ent->lifetimes) {
                if (needsPlus) {
                    os << StringView("+");
                }
                needsPlus = true;
                os << it;
            } if (ent->use) { os << StringView("use") << *ent->use; } os << StringView("");)
    }
#undef _
#undef _2
}

PrettyPrintType::PrettyPrintType(const ASTType* ty)
    : type_(ty)
{
}

void PrettyPrintType::print(ZeroCopyOutput& os) const {
    type_->print(os, false);
}

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

ASTType* mkType(ObjPool& pool, ASTTypeTags::Tuple, Span sp, Vector<ASTType*> innerTypes) {
    return mkType(pool, sp, TypeData::make_Tuple({mv$(innerTypes)}));
}

ASTType* mkType(ObjPool& pool, ASTTypeTags::Function, Span sp, ASTHigherRankedBounds hrbs, bool isUnsafe, std::string abi, Vector<ASTType*> args, bool isVariadic, ASTType* ret) {
    return mkType(pool, sp, TypeData::make_Function({TypeFunction(mv$(hrbs), isUnsafe, abi, ret, mv$(args), isVariadic)}));
}

ASTType* mkType(ObjPool& pool, ASTTypeTags::Reference, Span sp, ASTLifetimeRef lft, bool isMut, ASTType* innerType, bool isPin) {
    return mkType(pool, sp, TypeData::make_Borrow({mv$(lft), isMut, innerType, isPin}));
}

ASTType* mkType(ObjPool& pool, ASTTypeTags::Pointer, Span sp, bool isMut, ASTType* innerType) {
    return mkType(pool, sp, TypeData::make_Pointer({isMut, innerType}));
}

ASTType* mkType(ObjPool& pool, ASTTypeTags::SizedArray, Span sp, ASTType* innerType, ASTExprNode* size) {
    return mkType(pool, sp, TypeData::make_Array({innerType, size}));
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

ASTType* mkType(ObjPool& pool, Span sp, std::vector<TypeTraitPath> traits, Vector<ASTLifetimeRef> lifetimes) {
    return mkType(pool, sp, TypeData::make_TraitObject({mv$(traits), mv$(lifetimes)}));
}

template <>
void stl::output<ZeroCopyOutput, ASTBoundConstness>(ZeroCopyOutput& out, ASTBoundConstness value) {
    switch (value) {
        case ASTBoundConstness::Never:
            out << StringView("Never");
            return;
        case ASTBoundConstness::Always:
            out << StringView("Always");
            return;
        case ASTBoundConstness::Maybe:
            out << StringView("Maybe");
            return;
    }
}

template <>
void stl::output<ZeroCopyOutput, ASTType*>(ZeroCopyOutput& out, ASTType* type) {
    if (type) {
        out << *type;
    } else {
        out << StringView("(null-type)");
    }
}

template <>
void stl::output<ZeroCopyOutput, const ASTType*>(ZeroCopyOutput& out, const ASTType* type) {
    if (type) {
        out << *type;
    } else {
        out << StringView("(null-type)");
    }
}

template <>
void stl::output<ZeroCopyOutput, TypeData::Tag>(ZeroCopyOutput& out, TypeData::Tag value) {
    out << static_cast<unsigned>(value);
}

template <>
void stl::output<ZeroCopyOutput, ASTType>(ZeroCopyOutput& os, const ASTType& tr) {
    tr.print(os, true);
    return;
}

template <>
void stl::output<ZeroCopyOutput, PrettyPrintType>(ZeroCopyOutput& os, PrettyPrintType x) {
    x.print(os);
    return;
}

template <>
void stl::output<ZeroCopyOutput, ASTHigherRankedBounds>(ZeroCopyOutput& out, const ASTHigherRankedBounds& value) {
    if (value.empty()) {
        return;
    }
    out << StringView("for<");
    for (const auto& lifetime : value.lifetimes) {
        out << lifetime << StringView(",");
    }
    for (const auto& type : value.types) {
        out << type << StringView(",");
    }
    out << StringView("> ");
}

template <>
void stl::output<ZeroCopyOutput, Vector<ASTType*>>(ZeroCopyOutput& out, const Vector<ASTType*>& values) {
    outCont(out, values);
}
