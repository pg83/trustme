#pragma once

#include <cstdint>
#include <memory>

#include "common.h"
#include "coretypes.h"
#include "span.h"
//#include "ast/macro.h"
#include "ast_lifetime_ref.h"
//#include "ast/path.h"
#include "tagged_union.h"

namespace AST {
    class ExprNode;
    class Expr;
    class LifetimeParam;

    class Path;
    struct PathParams;
    class MacroInvocation;
}
class TypeRef;

namespace AST {

    enum class BoundConstness : uint8_t {
        Never,
        Always,
        Maybe,
    };

    // Defined here for dependency reasons
    class HigherRankedBounds {
    public:
        ::std::vector<LifetimeParam> mLifetimes;
        //::std::vector<TypeParam>    m_types;
        //::std::vector<GenericBound>    m_bounds;

        HigherRankedBounds();
        ~HigherRankedBounds();
        HigherRankedBounds(HigherRankedBounds&&);
        HigherRankedBounds& operator=(HigherRankedBounds&&);
        HigherRankedBounds(const HigherRankedBounds&);

        bool empty() const;

        friend ::std::ostream& operator<<(::std::ostream& os, const HigherRankedBounds& x);
    };

}

class PrettyPrintType {
    const TypeRef& mType;

public:
    PrettyPrintType(const TypeRef& ty);

    void print(::std::ostream& os) const;

    friend ::std::ostream& operator<<(::std::ostream& os, const PrettyPrintType& v);
};

struct TypeFunction {
    AST::HigherRankedBounds hrbs;
    bool is_unsafe;
    ::std::string mAbi;
    ::std::unique_ptr<TypeRef> mRettype;
    ::std::vector<TypeRef> argTypes;
    bool is_variadic;

    TypeFunction();
    TypeFunction(AST::HigherRankedBounds hrbs, bool is_unsafe, ::std::string abi, ::std::unique_ptr<TypeRef> ret, ::std::vector<TypeRef> args, bool is_variadic);
    ~TypeFunction();
    TypeFunction(TypeFunction&& other);
    TypeFunction(const TypeFunction& other);

    Ordering ord(const TypeFunction& x) const;
};

struct TypeTraitPath {
    AST::HigherRankedBounds hrbs;
    ::std::unique_ptr<AST::Path> path;
    AST::BoundConstness constness = AST::BoundConstness::Never;

    TypeTraitPath();
    TypeTraitPath(AST::HigherRankedBounds hrbs, AST::Path path, AST::BoundConstness constness = AST::BoundConstness::Never);
    ~TypeTraitPath();
    TypeTraitPath(TypeTraitPath&&);
    TypeTraitPath(const TypeTraitPath&);

    Ordering ord(const TypeTraitPath& x) const;
};

struct TypeErasedType {
    ::std::vector<TypeTraitPath> traits;
    ::std::vector<TypeTraitPath> maybe_traits;
    ::std::vector<AST::LifetimeRef> lifetimes;
    ::std::unique_ptr<AST::PathParams> use;
    /// Was this `impl` from 2024 or later edition? This changes the behaviour if `use` is not present
    bool isEdition2024OrLater;
};

TAGGED_UNION_OUT_OF_LINE(
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
         // If `nullptr` - this is an inferred size
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

/// A type
class TypeRef {
    Span mSpan;

public:
    TypeData mData;

    ~TypeRef();

    TypeRef(TypeRef&& other) = default;
    TypeRef& operator=(TypeRef&& other) = default;

#if 1
    TypeRef(const TypeRef& other) = delete;
    TypeRef& operator=(const TypeRef& other) = delete;
#else
    TypeRef(const TypeRef& other)
        : mSpan(other.mSpan)
    {
        *this = other.clone();
    }

    TypeRef& operator=(const TypeRef& other) {
        mData = mv$(other.clone().mData);
        return *this;
    }
#endif

    TypeRef(Span sp);

    TypeRef(Span sp, TypeData data);

    struct TagInvalid {};

    TypeRef(TagInvalid, Span sp);

    struct TagMacro {};

    TypeRef(TagMacro, ::AST::MacroInvocation inv);

    struct TagUnit {}; // unit maps to a zero-length tuple, just easier to type

    TypeRef(TagUnit, Span sp);

    struct TagPrimitive {};

    TypeRef(TagPrimitive, Span sp, enum eCoreType type);

    TypeRef(Span sp, enum eCoreType type);

    struct TagTuple {};

    TypeRef(TagTuple, Span sp, ::std::vector<TypeRef> innerTypes);

    struct TagFunction {};

    TypeRef(TagFunction, Span sp, AST::HigherRankedBounds hrbs, bool is_unsafe, ::std::string abi, ::std::vector<TypeRef> args, bool is_variadic, TypeRef ret);

    struct TagReference {};

    TypeRef(TagReference, Span sp, AST::LifetimeRef lft, bool is_mut, TypeRef innerType);

    struct TagPointer {};

    TypeRef(TagPointer, Span sp, bool is_mut, TypeRef innerType);

    struct TagSizedArray {};

    TypeRef(TagSizedArray, Span sp, TypeRef innerType, ::std::shared_ptr<AST::ExprNode> size);

    struct TagUnsizedArray {};

    TypeRef(TagUnsizedArray, Span sp, TypeRef innerType);

    struct TagArg {};

    TypeRef(TagArg, Span sp, RcString name, unsigned int binding = ~0u);

    TypeRef(Span sp, RcString name, unsigned int binding = ~0u);

    struct TagPath {};

    TypeRef(TagPath, Span sp, AST::Path path);
    TypeRef(Span sp, AST::Path path);

    TypeRef(Span sp, ::std::vector<TypeTraitPath> traits, ::std::vector<AST::LifetimeRef> lifetimes);

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

    const AST::Path& path() const {
        return *mData.as_Path();
    }

    AST::Path& path() {
        return *mData.as_Path();
    }

    bool isTypeParam() const {
        return mData.is_Generic();
    }

    const RcString& type_param() const {
        return mData.as_Generic().name;
    }

    bool is_reference() const {
        return mData.is_Borrow();
    }

    bool is_pointer() const {
        return mData.is_Pointer();
    }

    bool isTuple() const {
        return mData.is_Tuple();
    }

    TypeRef clone() const;

    const TypeRef& innerType() const {
        TU_MATCH_DEF(TypeData, (mData), (e), (throw ::std::runtime_error("Called inner_type on non-wrapper");), (Borrow, return *e.inner;), (Pointer, return *e.inner;), (Array, return *e.inner;))
    }

    TypeRef& innerType() {
        TU_MATCH_DEF(TypeData, (mData), (e), (throw ::std::runtime_error("Called inner_type on non-wrapper");), (Borrow, return *e.inner;), (Pointer, return *e.inner;), (Array, return *e.inner;))
    }

    Ordering ord(const TypeRef& x) const;

    bool operator==(const TypeRef& x) const {
        return ord(x) == OrdEqual;
    }

    bool operator!=(const TypeRef& x) const {
        return ord(x) != OrdEqual;
    }

    bool operator<(const TypeRef& x) const {
        return ord(x) == OrdLess;
    };

    void print(::std::ostream& os, bool isDebug = false) const;

    PrettyPrintType print_pretty() const {
        return PrettyPrintType(*this);
    }

    friend class PrettyPrintType;

    friend ::std::ostream& operator<<(::std::ostream& os, const TypeRef& tr);
};

