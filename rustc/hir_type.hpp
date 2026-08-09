/*
 * MRustC - Rust Compiler
 * - By John Hodge (Mutabah/thePowersGang)
 *
 * hir/type.hpp
 * - HIR Type representation
 */
#ifndef _HIR_TYPE_HPP_
#define _HIR_TYPE_HPP_
#pragma once

#include "tagged_union.hpp"
#include "hir_path.hpp"
#include "hir_expr_ptr.hpp"
#include "span.hpp"
#include "hir_type_ref.hpp"
#include "hir_literal.hpp"
#include "hir_generic_ref.hpp"
#include "hir_generic_params.hpp"
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
    struct ExprNode_Closure;
    struct ExprNode_Generator;

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

    static inline bool is_integer(const CoreType& v) {
        switch (v) {
            case CoreType::Usize:
            case CoreType::Isize:
            case CoreType::U8:
            case CoreType::I8:
            case CoreType::U16:
            case CoreType::I16:
            case CoreType::U32:
            case CoreType::I32:
            case CoreType::U64:
            case CoreType::I64:
            case CoreType::U128:
            case CoreType::I128:
                return true;
            default:
                return false;
        }
    }

    static inline bool is_float(const CoreType& v) {
        switch (v) {
            case CoreType::F16:
            case CoreType::F32:
            case CoreType::F64:
            case CoreType::F128:
                return true;
            default:
                return false;
        }
    }

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

         const GenericParams* get_generics() const;
         const TraitMarkings* get_trait_markings() const;

         bool operator==(const TypePathBinding & x) const;
         bool operator!=(const TypePathBinding & x) const { return !(*this == x); })
    );

    struct TypeData_Path {
        ::HIR::Path path;
        TypePathBinding binding;
        ::std::unique_ptr<::HIR::GenericParams> hrtbs; // HRTBs for vtable paths ONLY

        bool is_closure() const {
            return path.m_data.is_Generic() && path.m_data.as_Generic().m_path.components().back().size() > 8 && path.m_data.as_Generic().m_path.components().back().compare(0, strlen(CLOSURE_PATH_PREFIX), CLOSURE_PATH_PREFIX) == 0;
        }

        bool is_generator() const {
            return path.m_data.is_Generic() && path.m_data.as_Generic().m_path.components().back().size() > 8 && path.m_data.as_Generic().m_path.components().back().compare(0, strlen(GENERATOR_PATH_PREFIX), GENERATOR_PATH_PREFIX) == 0;
        }

        bool is_future() const {
            return path.m_data.is_Generic() && path.m_data.as_Generic().m_path.components().back().size() > 8 && path.m_data.as_Generic().m_path.components().back().compare(0, strlen(PATH_PREFIX_FUTURE), PATH_PREFIX_FUTURE) == 0;
        }
    };

    struct TypeData_TraitObject {
        ::HIR::TraitPath m_trait;
        ::std::vector<::HIR::GenericPath> m_markers;
        ::HIR::LifetimeRef m_lifetime;
    };

    struct TypeData_ErasedType_AliasInner {
        HIR::GenericParams generics;
        HIR::SimplePath path;
        HIR::TypeRef type;

        TypeData_ErasedType_AliasInner(const HIR::ItemPath& p, const HIR::GenericParams& params);
        bool is_public_to(const HIR::SimplePath& p) const;
    };

    TAGGED_UNION(
        TypeData_ErasedType_Inner,
        Alias,
        (Fcn,
         struct {
             ::HIR::Path m_origin;
             unsigned int m_index;
         }),
        (Known, HIR::TypeRef),
        (Alias, struct {
            ::HIR::PathParams params;
            ::std::shared_ptr<TypeData_ErasedType_AliasInner> inner;
        })
    );

} // namespace HIR

extern Ordering ord(const HIR::TypeData_ErasedType_Inner& a, const HIR::TypeData_ErasedType_Inner& b);

static inline bool operator==(const HIR::TypeData_ErasedType_Inner& a, const HIR::TypeData_ErasedType_Inner& b) {
    return ord(a, b) == OrdEqual;
}

static inline bool operator!=(const HIR::TypeData_ErasedType_Inner& a, const HIR::TypeData_ErasedType_Inner& b) {
    return ord(a, b) != OrdEqual;
}

namespace HIR {

    struct TypeData_ErasedType {
        bool m_is_sized;
        ::std::vector<::HIR::TraitPath> m_traits;
        ::std::vector<::HIR::LifetimeRef> m_lifetime_bounds;
        TypeData_ErasedType_Inner m_inner;
        /// Contents of the `use<...>` annotation/bound
        ::HIR::PathParams m_use;
        /// Indicates if `use<...>` was present (and what edition)
        enum class Use {
            /// @brief Omitted, but pre-2024 edition: Uses types/lifetimes present in bounds
            OmittedOld,
            /// @brief Omitted, 2024 edition and later: Uses all in-scope types/lifetimes
            Omitted2024,
            /// @brief `use<...>` was present
            Present,
        } m_use_present;
    };

    struct TypeData_FunctionPointer {
        GenericParams hrls; // Higher-ranked lifetimes
        bool is_unsafe;
        bool is_variadic;
        RcString m_abi; // While RcString is usually used for identifiers only, there's not many ABIs, and this saves (on msvc x64 - 32-8 bytes)
        TypeRef m_rettype;
        ::std::vector<TypeRef> m_arg_types;
    };

    TAGGED_UNION_EX(
        TypeData_NamedFunction_Ty,
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
        (TypeData_NamedFunction_Ty clone() const;)
    );
    /// "magic structs": Any type generated from a node
    TAGGED_UNION_EX(
        TypeData_NodeType,
        (),
        Closure,
        ((Closure, const ::HIR::ExprNode_Closure*),
         (Generator, const ::HIR::ExprNode_Generator*), // Aka a coroutine
         (Async, const ::HIR::ExprNode_AsyncBlock*)),
        (),
        (),
        (bool operator==(const TypeData_NodeType& x) const; bool operator!=(const TypeData_NodeType& x) const { return !(*this == x); } Ordering ord(const ::HIR::TypeData_NodeType& x) const; TypeData_NodeType clone() const; void fmt(::std::ostream& os) const;)
    );

    TAGGED_UNION_EX(
        TypeData,
        (),
        Diverge,
        ((Infer,
         struct {
             unsigned int index;
             InferClass ty_class;

             /// Returns true if the ivar is a literal
             bool is_lit() const {
                 switch (this->ty_class) {
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
        (Path, TypeData_Path), // TODO: Pointer wrap
        (Generic, GenericRef),
        (TraitObject, TypeData_TraitObject),                      // TODO: Pointer wrap
        (ErasedType, /*::std::unique_ptr<*/ TypeData_ErasedType), // TODO: Pointer wrap
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
             TypeData_NamedFunction_Ty def;

             TypeData_FunctionPointer decay(TypeInterner& types, const Span& sp) const;
         }),
        (Function, TypeData_FunctionPointer), // TODO: Pointer wrap, this is quite large
        (NodeType, TypeData_NodeType)),
        (, m_flags(x.m_flags)),
        (m_flags = x.m_flags;),
        (
            enum TypeFlags : uint32_t {
                HAS_TYPE_INFER = 1u << 0,
                HAS_TYPE_PARAM = 1u << 1,
                HAS_LIFETIME_PARAM = 1u << 2,
                HAS_UNEVALUATED_CONST = 1u << 3,
                HAS_ASSOCIATED_TYPE = 1u << 4,
            };

            uint32_t m_flags = 0;

            bool has_type_infer() const { return m_flags & HAS_TYPE_INFER; }
            bool needs_monomorphisation(bool ignore_lifetimes = false) const {
                const auto mask = HAS_TYPE_PARAM | HAS_UNEVALUATED_CONST
                    | (ignore_lifetimes ? 0u : HAS_LIFETIME_PARAM);
                return m_flags & mask;
            }
            bool may_have_associated_type() const {
                return m_flags & (HAS_ASSOCIATED_TYPE | HAS_TYPE_INFER);
            }

            TypeData clone_data() const;
            void fmt(::std::ostream& os) const;

            // Deliberately semantic relations. Plain TypeRef equality is pointer identity.
            bool equals_ignoring_regions(::HIR::TypeRef x) const;
            Ordering ord_ignoring_regions(::HIR::TypeRef x) const;
            bool match_test_generics(const Span& sp, ::HIR::TypeRef x, t_cb_resolve_type resolve_placeholder, MatchGenerics& callback) const;
            ::HIR::Compare match_test_generics_fuzz(const Span& sp, ::HIR::TypeRef x, t_cb_resolve_type resolve_placeholder, MatchGenerics& callback) const;
            Compare compare_with_placeholders(const Span& sp, ::HIR::TypeRef x, t_cb_resolve_type resolve_placeholder) const;
            const ::HIR::SimplePath* get_sort_path() const;
        )
    );

    class TypeInterner {
        stl::ObjPool& m_pool;
        ::std::unordered_multimap<size_t, TypeRef> m_nodes;

    public:
        explicit TypeInterner(stl::ObjPool& pool): m_pool(pool) {}

        TypeRef intern(TypeData data);
        TypeRef infer(unsigned int idx = ~0u, InferClass ty_class = InferClass::None);
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
        TypeRef function(TypeData_FunctionPointer ft);
        TypeRef closure(ExprNode_Closure* node);
        TypeRef generator(ExprNode_Generator* node);
        TypeRef async_block(ExprNode_AsyncBlock* node);
    };

    inline bool operator==(TypeRef ty, CoreType ct) {
        return ty && ty->is_Primitive() && ty->as_Primitive() == ct;
    }
    inline bool operator!=(TypeRef ty, CoreType ct) { return !(ty == ct); }

    extern ::std::ostream& operator<<(::std::ostream& os, const ::HIR::TypeRef& ty);

} // namespace HIR

#endif
