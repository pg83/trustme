#pragma once

#include "tagged_union.h"
#include "hir_path.h"
#include "hir_expr_ptr.h"
#include "span.h"
#include "hir_type_ref.h"
#include "hir_literal.h"
#include "hir_generic_ref.h"
#include "hir_generic_params.h"
#include <memory>
#include <unordered_map>

constexpr const char* CLOSURE_PATH_PREFIX = "closure#";
constexpr const char* GENERATOR_PATH_PREFIX = "generator#";
constexpr const char* PATH_PREFIX_FUTURE = "future#";
constexpr const char* ATY_PREFIX_ERASED = "erased#";

namespace stl { class ObjPool; }

namespace HIR {

    struct TraitMarkings;
    class ExternType;
    class Struct;
    class Union;
    class Enum;
    class Function;
    class ItemPath;
    struct ExprNodeClosure;
    struct ExprNodeGenerator;

    enum class CoreType {
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
    extern ::std::ostream& operator<<(::std::ostream& os, const CoreType& ct);

    bool isInteger(const CoreType& v);

    bool isFloat(const CoreType& v);

    enum class BorrowType {
        Shared,
        Unique,
        Owned,
    };
    extern ::std::ostream& operator<<(::std::ostream& os, const BorrowType& bt);

    /// Array size used for types AND array literals
    TAGGED_UNION_EX(
        ArraySize,
        (),
        Unevaluated,
        (
            /// Un-evaluated size
            (Unevaluated, ConstGeneric),
            /// Fully known
            (Known, uint64_t)
        ),
        /*extra_move=*/(),
        /*extra_assign=*/(),
        /*extra=*/(ArraySize clone() const; Ordering ord(const ArraySize& x) const; bool operator==(const ArraySize& x) const { return ord(x) == OrdEqual; } bool operator!=(const ArraySize& x) const { return !operator==(x); })
    );
    extern ::std::ostream& operator<<(::std::ostream& os, const ArraySize& x);

    TAGGED_UNION_EX(
        TypePathBinding,
        (),
        Unbound,
        ((Unbound, struct {}), // Not yet bound, either during lowering OR during resolution (when associated and still being resolved)
         (Opaque, struct {}),  // Opaque, i.e. An associated type of a generic (or Self in a trait)
         (ExternType, const ::HIR::ExternType*),
         (Struct, const ::HIR::Struct*),
         (Union, const ::HIR::Union*),
         (Enum, const ::HIR::Enum*)),
        (),
        (),
        (TypePathBinding clone() const;

         const GenericParams* getGenerics() const;
         const TraitMarkings* getTraitMarkings() const;

         bool operator==(const TypePathBinding & x) const;
         bool operator!=(const TypePathBinding & x) const { return !(*this == x); })
    );

    struct TypeDataPath {
        ::HIR::Path path;
        TypePathBinding binding;
        ::std::unique_ptr<::HIR::GenericParams> hrtbs; // HRTBs for vtable paths ONLY

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

    struct TypeDataTraitObject {
        ::HIR::TraitPath mTrait;
        ::std::vector<::HIR::GenericPath> markers;
        ::HIR::LifetimeRef lifetime;
    };

    struct TypeDataErasedTypeAliasInner {
        HIR::GenericParams generics;
        HIR::SimplePath path;
        HIR::TypeRef type;

        TypeDataErasedTypeAliasInner(const HIR::ItemPath& p, const HIR::GenericParams& params);
        bool isPublicTo(const HIR::SimplePath& p) const;
    };

    TAGGED_UNION(
        TypeDataErasedTypeInner,
        Alias,
        (Fcn,
         struct {
             ::HIR::Path origin;
             unsigned int index;
         }),
        (Known, HIR::TypeRef),
        (Alias, struct {
            ::HIR::PathParams params;
            ::std::shared_ptr<TypeDataErasedTypeAliasInner> inner;
        })
    );

} // namespace HIR

extern Ordering ord(const HIR::TypeDataErasedTypeInner& a, const HIR::TypeDataErasedTypeInner& b);

static inline bool operator==(const HIR::TypeDataErasedTypeInner& a, const HIR::TypeDataErasedTypeInner& b) {
    return ord(a, b) == OrdEqual;
}

static inline bool operator!=(const HIR::TypeDataErasedTypeInner& a, const HIR::TypeDataErasedTypeInner& b) {
    return ord(a, b) != OrdEqual;
}

namespace HIR {

    struct TypeDataErasedType {
        bool isSized;
        ::std::vector<::HIR::TraitPath> traits;
        ::std::vector<::HIR::LifetimeRef> lifetimeBounds;
        TypeDataErasedTypeInner inner;
        /// Contents of the `use<...>` annotation/bound
        ::HIR::PathParams use;
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

    struct TypeDataFunctionPointer {
        GenericParams hrls; // Higher-ranked lifetimes
        bool isUnsafe;
        bool isVariadic;
        RcString mAbi; // RcString is usually used for identifiers, but ABI names also form a small interned set.
        TypeRef mRettype;
        ::std::vector<TypeRef> argTypes;
    };

    TAGGED_UNION_EX(
        TypeDataNamedFunctionTy,
        (),
        Function,
        ((Function, const ::HIR::Function*),
         (EnumConstructor,
          struct {
              const ::HIR::Enum* e;
              size_t v;
          }),
         (StructConstructor, const ::HIR::Struct*)),
        (),
        (),
        (TypeDataNamedFunctionTy clone() const;)
    );
    /// "magic structs": Any type generated from a node
    TAGGED_UNION_EX(
        TypeDataNodeType,
        (),
        Closure,
        ((Closure, const ::HIR::ExprNodeClosure*),
         (Generator, const ::HIR::ExprNodeGenerator*), // Aka a coroutine
         (Async, const ::HIR::ExprNodeAsyncBlock*)),
        (),
        (),
        (bool operator==(const TypeDataNodeType& x) const; bool operator!=(const TypeDataNodeType& x) const { return !(*this == x); } Ordering ord(const ::HIR::TypeDataNodeType& x) const; TypeDataNodeType clone() const; void fmt(::std::ostream& os) const;)
    );

    TAGGED_UNION_EX(
        TypeData,
        (),
        Diverge,
        ((Infer,
         struct {
             unsigned int index;
             InferClass tyClass;

             /// Returns true if the ivar is a literal
             bool isLit() const {
                 switch (this->tyClass) {
                     case InferClass::None:
                         return false;
                     case InferClass::Integer:
                     case InferClass::Float:
                         return true;
                 }
                 throw "";
             }
         }),
        (Diverge, struct {}),
        (Primitive, ::HIR::CoreType),
        (Path, TypeDataPath), // TODO: Pointer wrap
        (Generic, GenericRef),
        (TraitObject, TypeDataTraitObject),                      // TODO: Pointer wrap
        (ErasedType, /*::std::unique_ptr<*/ TypeDataErasedType), // TODO: Pointer wrap
        (Array,
         struct {
             TypeRef inner;
             ArraySize size;
         }),
        (Slice, struct { TypeRef inner; }),
        (Tuple, ::std::vector<TypeRef>),
        (Borrow,
         struct {
             ::HIR::LifetimeRef lifetime;
             ::HIR::BorrowType type;
             TypeRef inner;
         }),
        (Pointer,
         struct {
             ::HIR::BorrowType type;
             TypeRef inner;
         }),
        (NamedFunction,
         struct {
             ::HIR::Path path;
             TypeDataNamedFunctionTy def;

             TypeDataFunctionPointer decay(TypeInterner& types, const Span& sp) const;
         }),
        (Function, TypeDataFunctionPointer), // TODO: Pointer wrap, this is quite large
        (NodeType, TypeDataNodeType)),
        (, flags(x.flags)),
        (flags = x.flags;),
        (
            enum TypeFlags : uint32_t {
                HAS_TYPE_INFER = 1u << 0,
                HAS_TYPE_PARAM = 1u << 1,
                HAS_LIFETIME_PARAM = 1u << 2,
                HAS_UNEVALUATED_CONST = 1u << 3,
                HAS_ASSOCIATED_TYPE = 1u << 4,
                HAS_DEFERRED_CONST = 1u << 5,
            };

            uint32_t flags = 0;

            bool hasTypeInfer() const { return flags & HAS_TYPE_INFER; }
            bool needsMonomorphisation(bool ignoreLifetimes = false) const {
                const auto mask = HAS_TYPE_PARAM | HAS_UNEVALUATED_CONST
                    | (ignoreLifetimes ? 0u : HAS_LIFETIME_PARAM);
                return flags & mask;
            }
            bool mayHaveAssociatedType() const {
                return flags & (HAS_ASSOCIATED_TYPE | HAS_TYPE_INFER);
            }

            TypeData cloneData() const;
            void fmt(::std::ostream& os) const;

            // Deliberately semantic relations. Plain TypeRef equality is pointer identity.
            bool equalsIgnoringRegions(::HIR::TypeRef x) const;
            Ordering ordIgnoringRegions(::HIR::TypeRef x) const;
            bool matchTestGenerics(const Span& sp, ::HIR::TypeRef x, tCbResolveType resolvePlaceholder, MatchGenerics& callback) const;
            ::HIR::Compare matchTestGenericsFuzz(const Span& sp, ::HIR::TypeRef x, tCbResolveType resolvePlaceholder, MatchGenerics& callback) const;
            Compare compareWithPlaceholders(const Span& sp, ::HIR::TypeRef x, tCbResolveType resolvePlaceholder) const;
            const ::HIR::SimplePath* getSortPath() const;
        )
    );

    class TypeInterner {
        stl::ObjPool& pool;
        ::std::unordered_multimap<size_t, TypeRef> nodes;

    public:
        explicit TypeInterner(stl::ObjPool& pool);

        TypeRef intern(TypeData data);
        TypeRef infer(unsigned int idx = ~0u, InferClass tyClass = InferClass::None);
        TypeRef primitive(CoreType ct);
        TypeRef generic(RcString name, unsigned int slot);
        TypeRef self();
        TypeRef unit();
        TypeRef diverge();
        TypeRef borrow(BorrowType bt, TypeRef inner, LifetimeRef lft = LifetimeRef());
        TypeRef pointer(BorrowType bt, TypeRef inner);
        TypeRef tuple(::std::vector<TypeRef> types);
        TypeRef slice(TypeRef inner);
        TypeRef array(TypeRef inner, ArraySize size);
        TypeRef array(TypeRef inner, uint64_t size);
        TypeRef array(TypeRef inner, ConstGeneric size);
        TypeRef path(Path path, TypePathBinding binding, ::std::unique_ptr<GenericParams> hrtbs = {});
        TypeRef function(TypeDataFunctionPointer ft);
        TypeRef closure(ExprNodeClosure* node);
        TypeRef generator(ExprNodeGenerator* node);
        TypeRef asyncBlock(ExprNodeAsyncBlock* node);
    };

    inline bool operator==(TypeRef ty, CoreType ct) {
        return ty && ty->is_Primitive() && ty->as_Primitive() == ct;
    }
    inline bool operator!=(TypeRef ty, CoreType ct) { return !(ty == ct); }

    extern ::std::ostream& operator<<(::std::ostream& os, const ::HIR::TypeData* ty);

} // namespace HIR
