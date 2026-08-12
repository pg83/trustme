#pragma once

#include "common.h"
#include "tagged_union.h"
#include "span.h"
#include "stdspan.h"
#include "hir_type_ref.h"
#include "hir_generic_ref.h"
#include "hir_expr_ptr.h"

struct EncodedLiteral;
class Monomorphiser;
class HirSerialiser;
class HirDeserialiser;

namespace HIR {
    enum class BoundConstness : uint8_t {
        Never,
        Always,
        Maybe,
    };


    class EncodedLiteralPtr {
        EncodedLiteral* p;

    public:
        ~EncodedLiteralPtr();

        EncodedLiteralPtr();

        EncodedLiteralPtr(EncodedLiteral e);

        EncodedLiteralPtr(EncodedLiteralPtr&& x);

        EncodedLiteralPtr(const EncodedLiteralPtr& x) = delete;

        EncodedLiteralPtr& operator=(EncodedLiteralPtr&& x);

        EncodedLiteralPtr& operator=(const EncodedLiteralPtr& x) = delete;

        EncodedLiteral& operator*();

        const EncodedLiteral& operator*() const;

        EncodedLiteral* operator->();

        const EncodedLiteral* operator->() const;
    };
    struct ConstGeneric_Unevaluated;
    TAGGED_UNION_EX(
        ConstGeneric,
        (),
        Infer,
        (
            (Infer,
             struct InferData { // To be inferred
                 unsigned index;
                 // NOTE: Workaround for VS2014, which can't use initialiser lists when a default is specified
                 InferData(unsigned index = ~0u)
                     : index(index)
                 {
                 }
             }),
            // NOTE: This is a `unique_ptr` because it contains two PathParams and a shared (2*3 pointers + 2 pointers)
            // The rest of the variants here are two pointers
            (Unevaluated, std::unique_ptr<ConstGeneric_Unevaluated>), // Unevaluated (or evaluation deferred)
            //(Unevaluated, std::shared_ptr<HIR::ExprPtr>),   // Unevaluated (or evaluation deferred)
            (Generic, GenericRef),         // A single generic reference
            (Evaluated, EncodedLiteralPtr) // A fully known literal
        ),
        /*extra_move=*/(),
        /*extra_assign=*/(),
        /*extra=*/(ConstGeneric clone() const; bool operator==(const ConstGeneric& x) const; bool operator!=(const ConstGeneric& x) const { return !(*this == x); } Ordering ord(const ConstGeneric& x) const;)
    );
    ::std::ostream& operator<<(::std::ostream& os, const ConstGeneric& x);

    class Trait;
    class GenericParams;

    ::std::ostream& operator<<(::std::ostream& os, const Compare& x);

    Compare& operator&=(Compare& x, const Compare& y);

    /// Simple path - Absolute with no generic parameters
    // TODO: Maybe make this de-duplicated? Not sure about the overheads involved vs the gain - some paths are very common, others are only used once
    struct SimplePath {
        friend HirSerialiser;
        friend HirDeserialiser;

    private:
        ThinVector<RcString> m_members;

        SimplePath(ThinVector<RcString> members);

    public:
        SimplePath();

        SimplePath(RcString crate);

        SimplePath(RcString crate, ::std::vector<RcString> components);

        SimplePath(RcString crate, ::std::span<RcString> components);

        SimplePath(RcString crate, ::std::span<const RcString> components);

        SimplePath(RcString crate, ::std::initializer_list<RcString> components);

        SimplePath clone() const;
        SimplePath parent() const;

        const RcString& crate_name() const;

        ::std::span<const RcString> components() const {
            return m_members.empty() ? std::span<const RcString>() : std::span<const RcString>(m_members.begin() + 1, m_members.end());
        }

        ::std::vector<RcString> components_vec() const;

        SimplePath operator+(const RcString& s) const;

        void operator+=(const RcString& s);
        RcString pop_component();

        void update_crate_name(RcString v);
        void update_last_component(RcString v);

        bool operator==(const SimplePath& x) const {
            return ord(x) == OrdEqual;
        }

        bool operator!=(const SimplePath& x) const {
            return !(*this == x);
        }

        bool operator<(const SimplePath& x) const {
            return ord(x) == OrdLess;
        }

        Ordering ord(const SimplePath& x) const {
            return ::ord(m_members, x.m_members);
        }

        bool starts_with(const SimplePath& x, bool skip_last = false) const;
        friend ::std::ostream& operator<<(::std::ostream& os, const SimplePath& x);
    };

    struct PathParams {
        ThinVector<LifetimeRef> m_lifetimes;
        ThinVector<TypeRef> m_types;
        ThinVector<HIR::ConstGeneric> m_values;

        PathParams();
        PathParams(::HIR::TypeRef);
        PathParams(::HIR::LifetimeRef);
        PathParams clone() const;
        PathParams(const PathParams&) = delete;
        PathParams& operator=(const PathParams&) = delete;
        PathParams(PathParams&&) = default;
        PathParams& operator=(PathParams&&) = default;

        Compare compare_with_placeholders(const Span& sp, const PathParams& x, t_cb_resolve_type resolve_placeholder) const;
        Compare match_test_generics_fuzz(const Span& sp, const PathParams& x, t_cb_resolve_type resolve_placeholder, ::HIR::MatchGenerics& match) const;
        bool equals_ignoring_regions(const PathParams& x) const;

        /// Indicates that params exist (and thus the target requires monomorphisation)
        /// - Ignores lifetime params
        bool has_params() const {
            return !m_types.empty() || !m_values.empty();
        }

        bool operator==(const PathParams& x) const {
            return ord(x) == OrdEqual;
        }

        bool operator!=(const PathParams& x) const {
            return ord(x) != OrdEqual;
        }

        bool operator<(const PathParams& x) const {
            return ord(x) == OrdLess;
        }

        Ordering ord(const PathParams& x) const;

        friend ::std::ostream& operator<<(::std::ostream& os, const PathParams& x);
    };

    /// Generic path - Simple path with one lot of generic params
    class GenericPath {
    public:
        SimplePath m_path;
        PathParams m_params;

        GenericPath();
        GenericPath(::HIR::SimplePath sp);
        GenericPath(::HIR::SimplePath sp, ::HIR::PathParams params);
        GenericPath(::HIR::GenericParams hrls, ::HIR::SimplePath sp, ::HIR::PathParams params);

        GenericPath clone() const;
        Compare compare_with_placeholders(const Span& sp, const GenericPath& x, t_cb_resolve_type resolve_placeholder) const;
        bool equals_ignoring_regions(const GenericPath& x) const;

        bool operator==(const GenericPath& x) const {
            return ord(x) == OrdEqual;
        }

        bool operator!=(const GenericPath& x) const {
            return ord(x) != OrdEqual;
        }

        bool operator<(const GenericPath& x) const {
            return ord(x) == OrdLess;
        }

        Ordering ord(const GenericPath& x) const;

        friend ::std::ostream& operator<<(::std::ostream& os, const GenericPath& x);
    };

    class TraitPath {
    public:
        // TODO: Each bound should list its origin trait
        struct AtyEqual {
            ::HIR::GenericPath source_trait;
            ::HIR::PathParams aty_params;
            ::HIR::TypeRef type;

            Ordering ord(const AtyEqual& x) const;

            AtyEqual clone() const {
                return AtyEqual{source_trait.clone(), aty_params.clone(), type};
            }

            friend ::std::ostream& operator<<(::std::ostream& os, const AtyEqual& x);
        };

        /// Associated type trait bounds (`Type: Trait`)
        struct AtyBound {
            ::HIR::GenericPath source_trait;
            ::HIR::PathParams aty_params;
            std::vector<::HIR::TraitPath> traits;

            Ordering ord(const AtyBound& x) const;

            AtyBound clone() const;
        };

        typedef ::std::map<RcString, AtyEqual> assoc_list_t;

        ::std::unique_ptr<GenericParams> m_hrtbs;
        GenericPath m_path;
        assoc_list_t m_type_bounds;
        ::std::map<RcString, AtyBound> m_trait_bounds;
        BoundConstness m_constness = BoundConstness::Never;
        // Parenthesised Fn-trait syntax uses function lifetime-elision rules.
        // This is consumed and cleared by ConvertHIR_LifetimeElision.
        bool m_lifetime_elision = false;

        const ::HIR::Trait* m_trait_ptr;

        TraitPath();
        TraitPath(::std::unique_ptr<GenericParams> hrtbs, GenericPath path);
        TraitPath(::std::unique_ptr<GenericParams> hrtbs, GenericPath path, assoc_list_t type_bounds, ::std::map<RcString, AtyBound> trait_bounds, const ::HIR::Trait* trait_ptr = nullptr, BoundConstness constness = BoundConstness::Never);
        ~TraitPath();
        TraitPath(TraitPath&&);
        TraitPath& operator=(TraitPath&&);

        TraitPath clone() const;
        Compare compare_with_placeholders(const Span& sp, const TraitPath& x, t_cb_resolve_type resolve_placeholder) const;
        bool equals_ignoring_regions(const TraitPath& x) const;

        bool operator==(const TraitPath& x) const {
            return ord(x) == OrdEqual;
        }

        bool operator!=(const TraitPath& x) const {
            return ord(x) != OrdEqual;
        }

        bool operator<(const TraitPath& x) const {
            return ord(x) == OrdLess;
        }

        Ordering ord(const TraitPath& x) const;

        friend ::std::ostream& operator<<(::std::ostream& os, const TraitPath& x);
    };

    class Path {
    public:
        // Two possibilities
        // - UFCS
        // - Generic path
        TAGGED_UNION(
            Data,
            Generic,
            (Generic, GenericPath),
            (UfcsInherent,
             struct {
                 TypeRef type;
                 RcString item;
                 PathParams params;
                 PathParams impl_params;
             }),
            (UfcsKnown,
             struct {
                 TypeRef type;
                 GenericPath trait;
                 RcString item;
                 PathParams params;
                 std::unique_ptr<GenericParams> hrtbs;
             }),
            (UfcsUnknown, struct {
                TypeRef type;
                //GenericPath ??;
                RcString item;
                PathParams params;
            })
        );

        Data m_data;

        Path(Data data);

        Path(GenericPath _);
        Path(SimplePath _);

        Path(TypeRef ty, RcString item, PathParams item_params = PathParams());
        Path(TypeRef ty, GenericPath trait, RcString item, PathParams item_params = PathParams());
        Path(TypeRef ty, GenericParams hrtbs, GenericPath trait, RcString item, PathParams item_params = PathParams());

        Path clone() const;
        Compare compare_with_placeholders(const Span& sp, const Path& x, t_cb_resolve_type resolve_placeholder) const;
        bool equals_ignoring_regions(const Path& x) const;

        Ordering ord(const Path& x) const;

        bool operator==(const Path& x) const;

        bool operator!=(const Path& x) const {
            return !(*this == x);
        }

        bool operator<(const Path& x) const {
            return ord(x) == OrdLess;
        }

        friend ::std::ostream& operator<<(::std::ostream& os, const Path& x);
    };

    struct ConstGeneric_Unevaluated {
        /// Impl-level parameters to the expression
        HIR::PathParams params_impl;
        HIR::PathParams params_item;
        /// HIR/MIR for this unevaluated parameter
        std::shared_ptr<HIR::ExprPtr> expr;

        ConstGeneric_Unevaluated(HIR::ExprPtr ep);
        ConstGeneric_Unevaluated clone() const;
        ConstGeneric_Unevaluated monomorph(const Span& sp, const Monomorphiser& ms, bool allow_infer = true) const;
        bool equivalent(const ConstGeneric_Unevaluated& x) const;
        Ordering ord(const ConstGeneric_Unevaluated& x) const;
        void fmt(::std::ostream& os) const;

    private:
        ConstGeneric_Unevaluated();
    };

} // namespace HIR
