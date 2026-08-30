#pragma once

#include "span.h"
#include "common.h"
#include "output.h"
#include "coretypes.h"
#include "ast_lifetime_ref.h"

#include <std/lib/vector.h>

#include <memory>
#include <cstdint>

namespace stl {
    class ObjPool;
}

class ASTExprNode;
class ASTLifetimeParam;

class ASTPath;
struct ASTPathParams;
class ASTMacroInvocation;
class ASTPattern;
struct ASTType;

enum class ASTBoundConstness : u8 {
    Never,
    Always,
    Maybe,
};

class ASTHigherRankedBounds {
public:
    std::vector<ASTLifetimeParam> lifetimes;

    stl::Vector<RcString> types;

    ASTHigherRankedBounds();
    ~ASTHigherRankedBounds();
    ASTHigherRankedBounds(ASTHigherRankedBounds&&);
    ASTHigherRankedBounds& operator=(ASTHigherRankedBounds&&);
    ASTHigherRankedBounds(const ASTHigherRankedBounds&);

    bool empty() const;
};

class PrettyPrintType {
    const ASTType* type_;

public:
    PrettyPrintType(const ASTType* ty);

    void print(stl::ZeroCopyOutput& os) const;
};

struct TypeFunction {
    ASTHigherRankedBounds hrbs;
    bool isUnsafe;
    std::string abi;
    ASTType* rettype;
    stl::Vector<ASTType*> argTypes;
    bool isVariadic;

    TypeFunction();
    TypeFunction(ASTHigherRankedBounds hrbs, bool isUnsafe, std::string abi, ASTType* ret, stl::Vector<ASTType*> args, bool isVariadic);
    ~TypeFunction();
    TypeFunction(TypeFunction&& other);
    TypeFunction(const TypeFunction& other);

    Ordering ord(const TypeFunction& x) const;
};

struct TypeTraitPath {
    ASTHigherRankedBounds hrbs;
    std::unique_ptr<ASTPath> path;
    ASTBoundConstness constness = ASTBoundConstness::Never;

    bool isAsync = false;

    TypeTraitPath();
    TypeTraitPath(ASTHigherRankedBounds hrbs, ASTPath path, ASTBoundConstness constness = ASTBoundConstness::Never);
    ~TypeTraitPath();
    TypeTraitPath(TypeTraitPath&&);
    TypeTraitPath(const TypeTraitPath&);

    Ordering ord(const TypeTraitPath& x) const;
};

struct TypeErasedType {
    std::vector<TypeTraitPath> traits;
    std::vector<TypeTraitPath> maybeTraits;
    stl::Vector<ASTLifetimeRef> lifetimes;
    std::unique_ptr<ASTPathParams> use;

    bool isEdition2024OrLater;
};

#include "ast_types_tu.h"

namespace ASTTypeTags {
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
}

struct ASTType {
    Span span_;
    TypeData data;
    stl::ObjPool* pool = nullptr;

    ASTType(Span sp, TypeData data, stl::ObjPool* pool)
        : span_(std::move(sp))
        , data(std::move(data))
        , pool(pool)
    {
    }

    const Span& span() const {
        return span_;
    }

    bool isValid() const {
        return !data.is_None();
    }

    bool isUnbounded() const {
        return data.is_Any();
    }

    bool isWildcard() const {
        return data.is_Any();
    }

    bool isUnit() const {
        return data.is_Unit();
    }

    bool isPrimitive() const {
        return data.is_Primitive();
    }

    bool isPath() const {
        return data.is_Path();
    }

    const ASTPath& path() const {
        return *data.as_Path();
    }

    ASTPath& path() {
        return *data.as_Path();
    }

    bool isTypeParam() const {
        return data.is_Generic();
    }

    const RcString& typeParam() const {
        return data.as_Generic().name;
    }

    bool isReference() const {
        return data.is_Borrow();
    }

    bool isPointer() const {
        return data.is_Pointer();
    }

    bool isTuple() const {
        return data.is_Tuple();
    }

    ASTType* clone() const;

    ASTType* innerType() const {
        switch (data.tag()) {
            case TypeData::TAG_Borrow: {
                auto& e = data.as_Borrow();
                return e.inner;
            }
            case TypeData::TAG_Pointer: {
                auto& e = data.as_Pointer();
                return e.inner;
            }
            case TypeData::TAG_Array: {
                auto& e = data.as_Array();
                return e.inner;
            }
            default: {
                BUG(Span(), stl::StringView("Called inner_type on non-wrapper"));
            }
        }
    }

    Ordering ord(const ASTType& x) const;

    bool operator==(const ASTType& x) const {
        return ord(x) == OrdEqual;
    }

    bool operator!=(const ASTType& x) const {
        return ord(x) != OrdEqual;
    }

    void print(stl::ZeroCopyOutput& os, bool isDebug = false) const;

    PrettyPrintType printPretty() const {
        return PrettyPrintType(this);
    }
};

ASTType* mkType(stl::ObjPool& pool, Span sp, TypeData data);
ASTType* mkType(stl::ObjPool& pool, Span sp);
ASTType* mkType(stl::ObjPool& pool, ASTTypeTags::Invalid, Span sp);
ASTType* mkType(stl::ObjPool& pool, ASTTypeTags::Macro, ASTMacroInvocation inv);
ASTType* mkType(stl::ObjPool& pool, ASTTypeTags::Unit, Span sp);
ASTType* mkType(stl::ObjPool& pool, ASTTypeTags::Primitive, Span sp, enum eCoreType type);
ASTType* mkType(stl::ObjPool& pool, Span sp, enum eCoreType type);
ASTType* mkType(stl::ObjPool& pool, ASTTypeTags::Tuple, Span sp, stl::Vector<ASTType*> innerTypes);
ASTType* mkType(stl::ObjPool& pool, ASTTypeTags::Function, Span sp, ASTHigherRankedBounds hrbs, bool isUnsafe, std::string abi, stl::Vector<ASTType*> args, bool isVariadic, ASTType* ret);
ASTType* mkType(stl::ObjPool& pool, ASTTypeTags::Reference, Span sp, ASTLifetimeRef lft, bool isMut, ASTType* innerType, bool isPin = false);
ASTType* mkType(stl::ObjPool& pool, ASTTypeTags::Pointer, Span sp, bool isMut, ASTType* innerType);
ASTType* mkType(stl::ObjPool& pool, ASTTypeTags::SizedArray, Span sp, ASTType* innerType, ASTExprNode* size);
ASTType* mkType(stl::ObjPool& pool, ASTTypeTags::UnsizedArray, Span sp, ASTType* innerType);
ASTType* mkType(stl::ObjPool& pool, ASTTypeTags::Arg, Span sp, RcString name, unsigned int binding = ~0u);
ASTType* mkType(stl::ObjPool& pool, Span sp, RcString name, unsigned int binding = ~0u);
ASTType* mkType(stl::ObjPool& pool, ASTTypeTags::Path, Span sp, ASTPath path);
ASTType* mkType(stl::ObjPool& pool, Span sp, ASTPath path);
ASTType* mkType(stl::ObjPool& pool, Span sp, std::vector<TypeTraitPath> traits, stl::Vector<ASTLifetimeRef> lifetimes);

Ordering ord(ASTType* a, ASTType* b);
