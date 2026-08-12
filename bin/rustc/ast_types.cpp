#include "ast_types.h"
#include "ast_ast.h"
#include "ast_crate.h"
#include "ast_expr.h"

TAGGED_UNION_OUT_OF_LINE_IMPL(
    TypeData,
    None,
    (None, struct {}),
    (Any, struct {}),
    (Bang, struct {}),
    (Unit, struct {}),
    (Macro, struct { ::std::unique_ptr<::AST::MacroInvocation> inv; }),
    (Primitive, struct { enum eCoreType coreType; }),
    (Function, struct { TypeFunction info; }),
    (Tuple, struct { ::std::vector<TypeRef> innerTypes; }),
    (Borrow,
     struct {
         AST::LifetimeRef lifetime;
         bool is_mut;
         ::std::unique_ptr<TypeRef> inner;
     }),
    (Pointer,
     struct {
         bool is_mut;
         ::std::unique_ptr<TypeRef> inner;
     }),
    (Array,
     struct {
         ::std::unique_ptr<TypeRef> inner;
         ::std::shared_ptr<AST::ExprNode> size;
     }),
    (Slice, struct { ::std::unique_ptr<TypeRef> inner; }),
    (Generic,
     struct {
         RcString name;
         unsigned int index;
     }),
    (Path, ::std::unique_ptr<AST::Path>),
    (TraitObject,
     struct {
         ::std::vector<TypeTraitPath> traits;
         ::std::vector<AST::LifetimeRef> lifetimes;
     }),
    (ErasedType, std::unique_ptr<TypeErasedType>)
);

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

AST::HigherRankedBounds::HigherRankedBounds() = default;
AST::HigherRankedBounds::~HigherRankedBounds() = default;
AST::HigherRankedBounds::HigherRankedBounds(HigherRankedBounds&&) = default;
AST::HigherRankedBounds& AST::HigherRankedBounds::operator=(HigherRankedBounds&&) = default;
AST::HigherRankedBounds::HigherRankedBounds(const HigherRankedBounds&) = default;

bool AST::HigherRankedBounds::empty() const {
    return mLifetimes.empty();
}

TypeFunction::TypeFunction() = default;

TypeFunction::TypeFunction(AST::HigherRankedBounds hrbs, bool is_unsafe, ::std::string abi, ::std::unique_ptr<TypeRef> ret, ::std::vector<TypeRef> args, bool is_variadic)
    : hrbs(mv$(hrbs))
    , is_unsafe(is_unsafe)
    , mAbi(mv$(abi))
    , mRettype(mv$(ret))
    , argTypes(mv$(args))
    , is_variadic(is_variadic)
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
    , is_unsafe(other.is_unsafe)
    , mAbi(other.mAbi)
    , mRettype(box$(other.mRettype->clone()))
    , is_variadic(other.is_variadic)
{
    for (const auto& at : other.argTypes) {
        argTypes.push_back(at.clone());
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
    return (*mRettype).ord(*x.mRettype);
}

TypeRef::~TypeRef() {
}

TypeRef::TypeRef(TagMacro, ::AST::MacroInvocation inv)
    : mSpan(inv.span())
    , mData(TypeData::make_Macro({box$(inv)}))
{
}

TypeRef::TypeRef(TagPath, Span sp, AST::Path path)
    : mSpan(mv$(sp))
    , mData(TypeData::make_Path(box$(path)))
{
}

TypeRef::TypeRef(Span sp, AST::Path path)
    : TypeRef(TagPath(), mv$(sp), mv$(path))
{
}

TypeRef TypeRef::clone() const {
    struct H {
        static ::std::vector<::TypeRef> cloneTyVec(const ::std::vector<TypeRef>& x) {
            ::std::vector<TypeRef> rv;
            rv.reserve(x.size());
            for (const auto& t : x) {
                rv.push_back(t.clone());
            }
            return rv;
        }
    };

    switch (mData.tag()) {
        case TypeData::TAGDEAD:
            assert(!"Copying a destructed type");
#define _COPY(VAR)                                                       \
    case TypeData::TAG_##VAR:                                            \
        return TypeRef(mSpan, TypeData::make_##VAR(mData.as_##VAR())); \
        break;
#define _CLONE(VAR, ...)                                           \
    case TypeData::TAG_##VAR: {                                    \
        auto& old = mData.as_##VAR();                             \
        return TypeRef(mSpan, TypeData::make_##VAR(__VA_ARGS__)); \
    } break;
            _COPY(None)
            _COPY(Any)
            _COPY(Bang)
            _CLONE(Macro, {box$(old.inv->clone())})
            //case TypeData::TAG_Macro:   assert( !"Copying an unexpanded type macro" );
            _COPY(Unit)
            _COPY(Primitive)
            _COPY(Function)
            _CLONE(Tuple, {H::cloneTyVec(old.innerTypes)})
            _CLONE(Borrow, {AST::LifetimeRef(old.lifetime), old.is_mut, box$(old.inner->clone())})
            _CLONE(Pointer, {old.is_mut, box$(old.inner->clone())})
            _CLONE(Array, {box$(old.inner->clone()), old.size})
            _CLONE(Slice, {box$(old.inner->clone())})
            _COPY(Generic)
            _CLONE(Path, std::make_unique<AST::Path>(*old))
            _COPY(TraitObject)
            _CLONE(ErasedType, std::make_unique<TypeErasedType>(TypeErasedType{old->traits, old->maybeTraits, old->lifetimes, old->use ? box$(*old->use) : ::std::unique_ptr<AST::PathParams>(), old->isEdition2024OrLater}))
#undef _COPY
#undef _CLONE
    }
    throw "";
}

TypeTraitPath::TypeTraitPath(AST::HigherRankedBounds hrbs, AST::Path path, AST::BoundConstness constness)
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
    , path(std::make_unique<AST::Path>(*x.path))
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

Ordering TypeRef::ord(const TypeRef& x) const {
    Ordering rv;

    rv = ::ord((unsigned)mData.tag(), (unsigned)x.mData.tag());
    if (rv != OrdEqual) {
        return rv;
    }

    TU_MATCH(TypeData, (mData, x.mData), (ent, x_ent), (None, return OrdEqual;), (Macro, throw CompileError::BugCheck("TypeRef::ord - unexpanded macro");), (Any, return OrdEqual;), (Unit, return OrdEqual;), (Bang, return OrdEqual;), (Primitive, return ::ord((unsigned)ent.coreType, (unsigned)x_ent.coreType);), (Function, return ent.info.ord(x_ent.info);), (Tuple, return ::ord(ent.innerTypes, x_ent.innerTypes);), (Borrow, rv = ::ord(ent.is_mut, x_ent.is_mut); if (rv != OrdEqual) return rv; return (*ent.inner).ord(*x_ent.inner);), (Pointer, rv = ::ord(ent.is_mut, x_ent.is_mut); if (rv != OrdEqual) return rv; return (*ent.inner).ord(*x_ent.inner);), (Array, rv = (*ent.inner).ord(*x_ent.inner); if (rv != OrdEqual) return rv; if (ent.size.get()) { throw ::std::runtime_error("TODO: Sized array comparisons"); } return OrdEqual;), (Slice, return (*ent.inner).ord(*x_ent.inner);), (Generic, return ::ord(ent.name, x_ent.name);), (Path, return ent->ord(*x_ent);), (TraitObject, return ::ord(ent.traits, x_ent.traits);), (ErasedType, ORD(ent->traits, x_ent->traits); ORD(ent->maybeTraits, x_ent->maybeTraits); ORD(ent->lifetimes, x_ent->lifetimes); ORD(ent->use != 0, x_ent->use != 0); if (ent->use) { ORD(*ent->use, *x_ent->use); } ORD(ent->isEdition2024OrLater, x_ent->isEdition2024OrLater); return OrdEqual;))
    throw ::std::runtime_error(FMT("BUGCHECK - Unhandled TypeRef class '" << mData.tag() << "'"));
}

::std::ostream& operator<<(::std::ostream& os, const eCoreType ct) {
    return os << coretypeName(ct);
}

void TypeRef::print(::std::ostream& os, bool isDebug /*=false*/) const {
//os << "TypeRef(";
#define _(VAR, ...)                                \
    case TypeData::TAG_##VAR: {                    \
        const auto& ent = this->mData.as_##VAR(); \
        (void)&ent;                                \
        __VA_ARGS__                                \
    } break;
#define _2(VAR, brace)                             \
    case TypeData::TAG_##VAR: {                    \
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
                if (ent.info.is_unsafe) {
                    os << "unsafe ";
                }
                os << "fn(";
                for (const auto& arg : ent.info.argTypes) {
                    arg.print(os, isDebug);
                    os << ", ";
                }
                os << ")";
                if (!ent.info.mRettype->isUnit()) {
                    os << " -> " << *ent.info.mRettype;
                }
            }
            break;
            _(Tuple, os << "( "; for (const auto& it : ent.innerTypes) {
                it.print(os, isDebug);
                os << ", ";
            } os << ")";)
            TU_ARM(mData, Borrow, ent) {
                os << "&";
                if (ent.lifetime != AST::LifetimeRef()) {
                    os << ent.lifetime << " ";
                }
                os << (ent.is_mut ? "mut " : "");
                ent.inner->print(os, isDebug);
            }
            break;
            _(Pointer, os << "*" << (ent.is_mut ? "mut " : "const "); ent.inner->print(os, isDebug);)
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
                if (it.constness == AST::BoundConstness::Always) os << "const "; else if (it.constness == AST::BoundConstness::Maybe) os << "[const] ";
                it.path->printPretty(os, true, isDebug);
            } for (const auto& it : ent.lifetimes) {
                if (it.binding() != AST::LifetimeRef::BINDING_UNSPECIFIED) {
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
                if (it.constness == AST::BoundConstness::Always) os << "const "; else if (it.constness == AST::BoundConstness::Maybe) os << "[const] ";
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

::std::ostream& operator<<(::std::ostream& os, const TypeRef& tr) {
    tr.print(os, true);
    return os;
}

::std::ostream& operator<<(::std::ostream& os, const PrettyPrintType& x) {
    x.mType.print(os, false);
    return os;
}

namespace AST {
    ::std::ostream& operator<<(::std::ostream& os, const LifetimeRef& x) {
        if (x.mBinding == LifetimeRef::BINDING_STATIC) {
            os << "'static";
        } else if (x.mBinding == LifetimeRef::BINDING_INFER) {
            os << "'_";
        } else if (x.mBinding == LifetimeRef::BINDING_UNSPECIFIED) {
            os << "/*'UNSPEC*/";
        } else {
            os << "'" << x.mName.name;
            if (x.mBinding != LifetimeRef::BINDING_UNBOUND) {
                os << "/*" << x.mBinding << "*/";
            }
        }
        return os;
    }
}

PrettyPrintType::PrettyPrintType(const TypeRef& ty)
    : mType(ty) {
}
TypeRef::TypeRef(Span sp)
    : mSpan(mv$(sp))
    , mData(TypeData::make_Any({})) {
}
TypeRef::TypeRef(Span sp, TypeData data)
    : mSpan(mv$(sp))
    , mData(mv$(data)) {
}
TypeRef::TypeRef(TagInvalid, Span sp)
    : mSpan(mv$(sp))
    , mData(TypeData::make_None({})) {
}
// unit maps to a zero-length tuple, just easier to type

TypeRef::TypeRef(TagUnit, Span sp)
    : mSpan(mv$(sp))
    , mData(TypeData::make_Unit({})) {
}
TypeRef::TypeRef(TagPrimitive, Span sp, enum eCoreType type)
    : mSpan(mv$(sp))
    , mData(TypeData::make_Primitive({type})) {
}
TypeRef::TypeRef(Span sp, enum eCoreType type)
    : mSpan(mv$(sp))
    , mData(TypeData::make_Primitive({type})) {
}
TypeRef::TypeRef(TagTuple, Span sp, ::std::vector<TypeRef> innerTypes)
    : mSpan(mv$(sp))
    , mData(TypeData::make_Tuple({::std::move(innerTypes)})) {
}
TypeRef::TypeRef(TagFunction, Span sp, AST::HigherRankedBounds hrbs, bool is_unsafe, ::std::string abi, ::std::vector<TypeRef> args, bool is_variadic, TypeRef ret)
    : mSpan(mv$(sp))
    , mData(TypeData::make_Function({TypeFunction(mv$(hrbs), is_unsafe, abi, box$(ret), mv$(args), is_variadic)})) {
}
TypeRef::TypeRef(TagReference, Span sp, AST::LifetimeRef lft, bool is_mut, TypeRef innerType)
    : mSpan(mv$(sp))
    , mData(TypeData::make_Borrow({::std::move(lft), is_mut, ::makeUniquePtr(mv$(innerType))})) {
}
TypeRef::TypeRef(TagPointer, Span sp, bool is_mut, TypeRef innerType)
    : mSpan(mv$(sp))
    , mData(TypeData::make_Pointer({is_mut, ::makeUniquePtr(mv$(innerType))})) {
}
TypeRef::TypeRef(TagSizedArray, Span sp, TypeRef innerType, ::std::shared_ptr<AST::ExprNode> size)
    : mSpan(mv$(sp))
    , mData(TypeData::make_Array({::makeUniquePtr(mv$(innerType)), mv$(size)})) {
}
TypeRef::TypeRef(TagUnsizedArray, Span sp, TypeRef innerType)
    : mSpan(mv$(sp))
    , mData(TypeData::make_Slice({::makeUniquePtr(mv$(innerType))})) {
}
TypeRef::TypeRef(TagArg, Span sp, RcString name, unsigned int binding)
    : mSpan(mv$(sp))
    , mData(TypeData::make_Generic({name, binding})) {
}
TypeRef::TypeRef(Span sp, RcString name, unsigned int binding)
    : TypeRef(TagArg(), mv$(sp), mv$(name), binding) {
}
TypeRef::TypeRef(Span sp, ::std::vector<TypeTraitPath> traits, ::std::vector<AST::LifetimeRef> lifetimes)
    : mSpan(mv$(sp))
    , mData(TypeData::make_TraitObject({::std::move(traits), mv$(lifetimes)})) {
}
