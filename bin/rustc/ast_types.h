#pragma once

#include "span.h"
#include "common.h"
#include "coretypes.h"

#include <memory>
#include <cstdint>

//#include "ast/macro.h"
#include "ast_lifetime_ref.h"
//#include "ast/path.h"
#include "tagged_union.h"

namespace stl {
    class ObjPool;
}

class ASTExprNode;
class ASTExpr;
class ASTLifetimeParam;

class ASTPath;
struct ASTPathParams;
class ASTMacroInvocation;
struct TypeStore;
typedef TypeStore* TypeRef;

enum class ASTBoundConstness : uint8_t {
    Never,
    Always,
    Maybe,
};

// Defined here for dependency reasons
class ASTHigherRankedBounds {
public:
    ::std::vector<ASTLifetimeParam> mLifetimes;
    //::std::vector<TypeParam>    m_types;
    //::std::vector<GenericBound>    m_bounds;

    ASTHigherRankedBounds();
    ~ASTHigherRankedBounds();
    ASTHigherRankedBounds(ASTHigherRankedBounds&&);
    ASTHigherRankedBounds& operator=(ASTHigherRankedBounds&&);
    ASTHigherRankedBounds(const ASTHigherRankedBounds&);

    bool empty() const;

    friend ::std::ostream& operator<<(::std::ostream& os, const ASTHigherRankedBounds& x);
};

class PrettyPrintType {
    const TypeStore* mType;

public:
    PrettyPrintType(const TypeStore* ty);

    void print(::std::ostream& os) const;

    friend ::std::ostream& operator<<(::std::ostream& os, const PrettyPrintType& v);
};

struct TypeFunction {
    ASTHigherRankedBounds hrbs;
    bool isUnsafe;
    ::std::string mAbi;
    TypeRef mRettype;
    ::std::vector<TypeRef> argTypes;
    bool isVariadic;

    TypeFunction();
    TypeFunction(ASTHigherRankedBounds hrbs, bool isUnsafe, ::std::string abi, TypeRef ret, ::std::vector<TypeRef> args, bool isVariadic);
    ~TypeFunction();
    TypeFunction(TypeFunction&& other);
    TypeFunction(const TypeFunction& other);

    Ordering ord(const TypeFunction& x) const;
};

struct TypeTraitPath {
    ASTHigherRankedBounds hrbs;
    ::std::unique_ptr<ASTPath> path;
    ASTBoundConstness constness = ASTBoundConstness::Never;

    TypeTraitPath();
    TypeTraitPath(ASTHigherRankedBounds hrbs, ASTPath path, ASTBoundConstness constness = ASTBoundConstness::Never);
    ~TypeTraitPath();
    TypeTraitPath(TypeTraitPath&&);
    TypeTraitPath(const TypeTraitPath&);

    Ordering ord(const TypeTraitPath& x) const;
};

struct TypeErasedType {
    ::std::vector<TypeTraitPath> traits;
    ::std::vector<TypeTraitPath> maybeTraits;
    ::std::vector<ASTLifetimeRef> lifetimes;
    ::std::unique_ptr<ASTPathParams> use;
    /// Was this `impl` from 2024 or later edition? This changes the behaviour if `use` is not present
    bool isEdition2024OrLater;
};

// The sum-type payload of a type. Now an ordinary inline tagged union: it holds
// only pointers to `TypeStore`, so it no longer needs the out-of-line macro
// stack. Node metadata (span, owning pool) lives on `TypeStore`.
TAGGED_UNION(
    TypeData,
    None,
    (None, struct {}),
    (Any, struct {}),
    (Bang, struct {}),
    (Unit, struct {}),
    (Macro, struct { ASTMacroInvocation* inv; }),
    (Primitive, struct { enum eCoreType coreType; }),
    (Function, struct { TypeFunction info; }),
    (Tuple, struct { ::std::vector<TypeRef> innerTypes; }),
    (Borrow,
     struct {
         ASTLifetimeRef lifetime;
         bool isMut;
         TypeRef inner;
     }),
    (Pointer,
     struct {
         bool isMut;
         TypeRef inner;
     }),
    (Array,
     struct {
         TypeRef inner;
         // If `nullptr` - this is an inferred size
         ::std::shared_ptr<ASTExprNode> size;
     }),
    (Slice, struct { TypeRef inner; }),
    (Generic,
     struct {
         RcString name;
         unsigned int index;
     }),
    (Path, ASTPath*),
    (TraitObject,
     struct {
         ::std::vector<TypeTraitPath> traits;
         ::std::vector<ASTLifetimeRef> lifetimes;
     }),
    (ErasedType, TypeErasedType*));

// Tag markers for the type factories (mirror the old TypeRef constructors).
namespace TypeRefTags {
    struct Invalid {};
    struct Macro {};
    struct Unit {};
    struct Primitive {};
    struct Tuple {};
    struct Function {};
    struct Reference {};
    struct Pointer {};
    struct SizedArray {};
    struct UnsizedArray {};
    struct Arg {};
    struct Path {};
}  // namespace TypeRefTags

/// A pool-allocated type node. `TypeRef` is a pointer to one of these; the node
/// records the pool it lives in so `clone()` is cheap and self-sufficient.
struct TypeStore {
    Span mSpan;
    TypeData mData;
    stl::ObjPool* pool = nullptr;

    TypeStore(Span sp, TypeData data, stl::ObjPool* pool)
        : mSpan(::std::move(sp))
        , mData(::std::move(data))
        , pool(pool) {
    }

    const Span& span() const {
        return mSpan;
    }

    bool isValid() const {
        return !mData.is_None();
    }
    bool isUnbounded() const {
        return mData.is_Any();
    }
    bool isWildcard() const {
        return mData.is_Any();
    }
    bool isUnit() const {
        return mData.is_Unit();
    }
    bool isPrimitive() const {
        return mData.is_Primitive();
    }
    bool isPath() const {
        return mData.is_Path();
    }
    const ASTPath& path() const {
        return *mData.as_Path();
    }
    ASTPath& path() {
        return *mData.as_Path();
    }
    bool isTypeParam() const {
        return mData.is_Generic();
    }
    const RcString& typeParam() const {
        return mData.as_Generic().name;
    }
    bool isReference() const {
        return mData.is_Borrow();
    }
    bool isPointer() const {
        return mData.is_Pointer();
    }
    bool isTuple() const {
        return mData.is_Tuple();
    }

    TypeRef clone() const;

    TypeRef innerType() const {
        TU_MATCH_DEF(TypeData, (mData), (e), (throw ::std::runtime_error("Called inner_type on non-wrapper");), (Borrow, return e.inner;), (Pointer, return e.inner;), (Array, return e.inner;))
    }

    Ordering ord(const TypeStore& x) const;
    bool operator==(const TypeStore& x) const {
        return ord(x) == OrdEqual;
    }
    bool operator!=(const TypeStore& x) const {
        return ord(x) != OrdEqual;
    }

    void print(::std::ostream& os, bool isDebug = false) const;
    PrettyPrintType printPretty() const {
        return PrettyPrintType(this);
    }
};

// Type factories - allocate a fresh node from `pool` (or the ambient pool).
extern stl::ObjPool& astTypePool();
extern void setAstTypePool(stl::ObjPool& pool);

extern TypeRef mktype(stl::ObjPool& pool, Span sp, TypeData data);
inline TypeRef mktype(Span sp, TypeData data) {
    return mktype(astTypePool(), sp, ::std::move(data));
}
extern TypeRef mktype(Span sp);  // wildcard / Any
extern TypeRef mktype(TypeRefTags::Invalid, Span sp);
extern TypeRef mktype(TypeRefTags::Macro, ASTMacroInvocation inv);
extern TypeRef mktype(TypeRefTags::Unit, Span sp);
extern TypeRef mktype(TypeRefTags::Primitive, Span sp, enum eCoreType type);
extern TypeRef mktype(Span sp, enum eCoreType type);
extern TypeRef mktype(TypeRefTags::Tuple, Span sp, ::std::vector<TypeRef> innerTypes);
extern TypeRef mktype(TypeRefTags::Function, Span sp, ASTHigherRankedBounds hrbs, bool isUnsafe, ::std::string abi, ::std::vector<TypeRef> args, bool isVariadic, TypeRef ret);
extern TypeRef mktype(TypeRefTags::Reference, Span sp, ASTLifetimeRef lft, bool isMut, TypeRef innerType);
extern TypeRef mktype(TypeRefTags::Pointer, Span sp, bool isMut, TypeRef innerType);
extern TypeRef mktype(TypeRefTags::SizedArray, Span sp, TypeRef innerType, ::std::shared_ptr<ASTExprNode> size);
extern TypeRef mktype(TypeRefTags::UnsizedArray, Span sp, TypeRef innerType);
extern TypeRef mktype(TypeRefTags::Arg, Span sp, RcString name, unsigned int binding = ~0u);
extern TypeRef mktype(Span sp, RcString name, unsigned int binding = ~0u);
extern TypeRef mktype(TypeRefTags::Path, Span sp, ASTPath path);
extern TypeRef mktype(Span sp, ASTPath path);
extern TypeRef mktype(Span sp, ::std::vector<TypeTraitPath> traits, ::std::vector<ASTLifetimeRef> lifetimes);

extern ::std::ostream& operator<<(::std::ostream& os, const TypeStore& tr);
inline ::std::ostream& operator<<(::std::ostream& os, const TypeStore* tr) {
    return tr ? (os << *tr) : (os << "(null-type)");
}
extern Ordering ord(const TypeRef& a, const TypeRef& b);

