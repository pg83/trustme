#pragma once

#include "span.h"
#include "hir_path.h"
#include "hir_expr_ptr.h"
#include "hir_type_ref.h"
#include "tagged_union.h"
#include "hir_generic_ref.h"
#include "hir_generic_params.h"

#include <memory>
#include <unordered_map>

constexpr const char* CLOSURE_PATH_PREFIX = "closure#";
constexpr const char* GENERATOR_PATH_PREFIX = "generator#";
constexpr const char* PATH_PREFIX_FUTURE = "future#";
constexpr const char* ATY_PREFIX_ERASED = "erased#";

namespace stl {
    class ObjPool;
}

struct HIRTraitMarkings;
class HIRExternType;
class HIRStruct;
class HIRUnion;
class HIREnum;
class HIRFunction;
class HIRItemPath;
struct HIRExprNodeClosure;
struct HIRExprNodeGenerator;

enum class HIRCoreType {
    Usize,
    Isize,
    U8,
    I8,
    U16,
    I16,
    U32,
    I32,
    U64,
    I64,
    U128,
    I128,

    F16,
    F32,
    F64,
    F128,

    Bool,
    Char,
    Str,
};
extern ::std::ostream& operator<<(::std::ostream& os, const HIRCoreType& ct);

bool isInteger(const HIRCoreType& v);

bool isFloat(const HIRCoreType& v);

enum class HIRBorrowType {
    Shared,
    Unique,
    Owned,
};
extern ::std::ostream& operator<<(::std::ostream& os, const HIRBorrowType& bt);

/// Array size used for types AND array literals
TAGGED_UNION_EX(
    HIRArraySize,
    (),
    Unevaluated,
    (
        /// Un-evaluated size
        (Unevaluated, HIRConstGeneric),
        /// Fully known
        (Known, uint64_t)
    ),
    /*extra_move=*/(),
    /*extra_assign=*/(),
    /*extra=*/(HIRArraySize clone() const; Ordering ord(const HIRArraySize& x) const; bool operator==(const HIRArraySize& x) const { return ord(x) == OrdEqual; } bool operator!=(const HIRArraySize& x) const { return !operator==(x); })
);
extern ::std::ostream& operator<<(::std::ostream& os, const HIRArraySize& x);

TAGGED_UNION_EX(
    HIRTypePathBinding,
    (),
    Unbound,
    ((Unbound, struct {}), // Not yet bound, either during lowering OR during resolution (when associated and still being resolved)
     (Opaque, struct {}),  // Opaque, i.e. An associated type of a generic (or Self in a trait)
     (ExternType, const HIRExternType*),
     (Struct, const HIRStruct*),
     (Union, const HIRUnion*),
     (Enum, const HIREnum*)),
    (),
    (),
    (HIRTypePathBinding clone() const;

     const HIRGenericParams* getGenerics() const;
     const HIRTraitMarkings* getTraitMarkings() const;

     bool operator==(const HIRTypePathBinding & x) const;
     bool operator!=(const HIRTypePathBinding & x) const { return !(*this == x); })
);

struct HIRTypeDataPath {
    HIRPath path;
    HIRTypePathBinding binding;

    bool isClosure() const {
        return path.mData.is_Generic() && path.mData.as_Generic().mPath.components().back().size() > 8 && path.mData.as_Generic().mPath.components().back().compare(0, strlen(CLOSURE_PATH_PREFIX), CLOSURE_PATH_PREFIX) == 0;
    }

    bool isGenerator() const {
        return path.mData.is_Generic() && path.mData.as_Generic().mPath.components().back().size() > 8 && path.mData.as_Generic().mPath.components().back().compare(0, strlen(GENERATOR_PATH_PREFIX), GENERATOR_PATH_PREFIX) == 0;
    }

    bool isFuture() const {
        return path.mData.is_Generic() && path.mData.as_Generic().mPath.components().back().size() > 8 && path.mData.as_Generic().mPath.components().back().compare(0, strlen(PATH_PREFIX_FUTURE), PATH_PREFIX_FUTURE) == 0;
    }
};

struct HIRTypeDataTraitObject {
    HIRTraitPath mTrait;
    ::std::vector<HIRGenericPath> markers;
};

struct HIRTypeDataErasedTypeAliasInner {
    HIRGenericParams generics;
    HIRSimplePath path;
    HIRTypeRef type;

    HIRTypeDataErasedTypeAliasInner(const HIRItemPath& p, const HIRGenericParams& params);
    bool isLocalTo(const HIRSimplePath& p) const;
};

TAGGED_UNION(
    TypeDataErasedTypeInner,
    Alias,
    (Fcn,
     struct {
         HIRPath origin;
         unsigned int index;
     }),
    (Known, HIRTypeRef),
    (Alias, struct {
        HIRPathParams params;
        ::std::shared_ptr<HIRTypeDataErasedTypeAliasInner> inner;
    })
);

extern Ordering ord(const TypeDataErasedTypeInner& a, const TypeDataErasedTypeInner& b);

static inline bool operator==(const TypeDataErasedTypeInner& a, const TypeDataErasedTypeInner& b) {
    return ord(a, b) == OrdEqual;
}

static inline bool operator!=(const TypeDataErasedTypeInner& a, const TypeDataErasedTypeInner& b) {
    return ord(a, b) != OrdEqual;
}

struct HIRTypeDataErasedType {
    bool isSized;
    ::std::vector<HIRTraitPath> traits;
    TypeDataErasedTypeInner inner;
    /// Contents of the `use<...>` annotation/bound
    HIRPathParams use;
    /// Indicates if `use<...>` was present (and what edition)
    enum class Use {
        /// @brief Omitted, but pre-2024 edition: Uses types/lifetimes present in bounds
        OmittedOld,
        /// @brief Omitted, 2024 edition and later: Uses all in-scope types/lifetimes
        Omitted2024,
        /// @brief `use<...>` was present
        Present,
    } usePresent;
};

struct HIRTypeDataFunctionPointer {
    bool isUnsafe;
    bool isVariadic;
    RcString mAbi; // RcString is usually used for identifiers, but ABI names also form a small interned set.
    HIRTypeRef mRettype;
    ::std::vector<HIRTypeRef> argTypes;
    // Internal ABI bit used by trait-object vtables. Ordinary Rust function
    // pointers erase #[track_caller], but a tracked trait method keeps the
    // implicit caller-location argument across dynamic dispatch.
    bool trackCaller = false;
};

struct HIRTypePatternRange {
    bool hasStart;
    HIRConstGeneric start;
    bool hasEnd;
    HIRConstGeneric end;
    bool endInclusive;

    HIRTypePatternRange clone() const;
    Ordering ord(const HIRTypePatternRange& x) const;
    void fmt(::std::ostream& os) const;
};

struct HIRTypePattern {
    ::std::vector<HIRTypePatternRange> alternatives;

    HIRTypePattern clone() const;
    Ordering ord(const HIRTypePattern& x) const;
    void fmt(::std::ostream& os) const;
};

TAGGED_UNION_EX(
    HIRTypeDataNamedFunctionTy,
    (),
    Function,
    ((Function, const HIRFunction*),
     (EnumConstructor,
      struct {
          const HIREnum* e;
          size_t v;
      }),
     (StructConstructor, const HIRStruct*)),
    (),
    (),
    (HIRTypeDataNamedFunctionTy clone() const;)
);
/// "magic structs": Any type generated from a node
TAGGED_UNION_EX(
    HIRTypeDataNodeType,
    (),
    Closure,
    ((Closure, const HIRExprNodeClosure*),
     (Generator, const HIRExprNodeGenerator*), // Aka a coroutine
     (Async, const HIRExprNodeAsyncBlock*)),
    (),
    (),
    (bool operator==(const HIRTypeDataNodeType& x) const; bool operator!=(const HIRTypeDataNodeType& x) const { return !(*this == x); } Ordering ord(const HIRTypeDataNodeType& x) const; HIRTypeDataNodeType clone() const; void fmt(::std::ostream& os) const;)
);

TAGGED_UNION_EX(
        HIRTypeData,
        (),
        Diverge,
        ((Infer,
         struct {
             unsigned int index;
             HIRInferClass tyClass;

             /// Returns true if the ivar is a literal
             bool isLit() const {
                 switch (this->tyClass) {
                     case HIRInferClass::None:
                         return false;
                     case HIRInferClass::Integer:
                     case HIRInferClass::Float:
                         return true;
                 }
                 throw "";
             }
         }),
        (Diverge, struct {}),
        (Primitive, HIRCoreType),
        (Path, HIRTypeDataPath), // TODO: Pointer wrap
        (Generic, HIRGenericRef),
        (TraitObject, HIRTypeDataTraitObject),                      // TODO: Pointer wrap
        (ErasedType, /*::std::unique_ptr<*/ HIRTypeDataErasedType), // TODO: Pointer wrap
        (Array,
         struct {
             HIRTypeRef inner;
             HIRArraySize size;
         }),
        (Slice, struct { HIRTypeRef inner; }),
        (Tuple, ::std::vector<HIRTypeRef>),
        (Borrow,
         struct {
             HIRBorrowType type;
             HIRTypeRef inner;
         }),
        (Pointer,
         struct {
             HIRBorrowType type;
             HIRTypeRef inner;
         }),
        (NamedFunction,
         struct {
             HIRPath path;
             HIRTypeDataNamedFunctionTy def;

             HIRTypeDataFunctionPointer decay(HIRTypeInterner& types, const Span& sp) const;
         }),
        (Function, HIRTypeDataFunctionPointer), // TODO: Pointer wrap, this is quite large
        (NodeType, HIRTypeDataNodeType),
        (Pattern,
         struct {
             HIRTypeRef inner;
             HIRTypePattern pattern;
         })),
        (, flags(x.flags)),
        (flags = x.flags;),
        (
            enum HIRTypeFlags : uint32_t {
                HAS_TYPE_INFER = 1u << 0,
                HAS_TYPE_PARAM = 1u << 1,
                HAS_UNEVALUATED_CONST = 1u << 3,
                HAS_ASSOCIATED_TYPE = 1u << 4,
                HAS_DEFERRED_CONST = 1u << 5,
            };

            uint32_t flags = 0;

            bool hasTypeInfer() const { return flags & HAS_TYPE_INFER; }
            bool needsMonomorphisation() const {
                return flags & (HAS_TYPE_PARAM | HAS_UNEVALUATED_CONST | HAS_DEFERRED_CONST);
            }
            bool mayHaveAssociatedType() const {
                return flags & (HAS_ASSOCIATED_TYPE | HAS_TYPE_INFER);
            }

            HIRTypeData cloneData() const;
            void fmt(::std::ostream& os) const;

            // Deliberately semantic relations. Plain ASTType* equality is pointer identity.
            bool equalsIgnoringRegions(HIRTypeRef x) const;
            Ordering ordIgnoringRegions(HIRTypeRef x) const;
            bool matchTestGenerics(const Span& sp, HIRTypeRef x, tCbResolveType resolvePlaceholder, HIRMatchGenerics& callback) const;
            HIRCompare matchTestGenericsFuzz(const Span& sp, HIRTypeRef x, tCbResolveType resolvePlaceholder, HIRMatchGenerics& callback) const;
            HIRCompare compareWithPlaceholders(const Span& sp, HIRTypeRef x, tCbResolveType resolvePlaceholder) const;
            const HIRSimplePath* getSortPath() const;
        )
    );

class HIRTypeInterner {
    stl::ObjPool& pool;
    ::std::unordered_multimap<size_t, HIRTypeRef> nodes;

public:
    explicit HIRTypeInterner(stl::ObjPool& pool);

    HIRTypeRef intern(HIRTypeData data);
    HIRTypeRef infer(unsigned int idx = ~0u, HIRInferClass tyClass = HIRInferClass::None);
    HIRTypeRef primitive(HIRCoreType ct);
    HIRTypeRef generic(RcString name, unsigned int slot);
    HIRTypeRef self();
    HIRTypeRef unit();
    HIRTypeRef diverge();
    HIRTypeRef borrow(HIRBorrowType bt, HIRTypeRef inner);
    HIRTypeRef pointer(HIRBorrowType bt, HIRTypeRef inner);
    HIRTypeRef tuple(::std::vector<HIRTypeRef> types);
    HIRTypeRef slice(HIRTypeRef inner);
    HIRTypeRef array(HIRTypeRef inner, HIRArraySize size);
    HIRTypeRef array(HIRTypeRef inner, uint64_t size);
    HIRTypeRef array(HIRTypeRef inner, HIRConstGeneric size);
    HIRTypeRef path(HIRPath path, HIRTypePathBinding binding);
    HIRTypeRef function(HIRTypeDataFunctionPointer ft);
    HIRTypeRef closure(HIRExprNodeClosure* node);
    HIRTypeRef generator(HIRExprNodeGenerator* node);
    HIRTypeRef asyncBlock(HIRExprNodeAsyncBlock* node);
};

inline bool operator==(HIRTypeRef ty, HIRCoreType ct) {
    return ty && ty->is_Primitive() && ty->as_Primitive() == ct;
}

inline bool operator!=(HIRTypeRef ty, HIRCoreType ct) {
    return !(ty == ct);
}

extern ::std::ostream& operator<<(::std::ostream& os, const HIRTypeData* ty);
