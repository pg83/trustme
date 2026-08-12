#pragma once

#include <algorithm>
#include <unordered_map>
#include "hir_type_ref.h"
#include "hir_expr_ptr.h"
#include "hir_expr.h"
#include "hir_typeck_expr_visit.h"
#include "span.h"
#include "hir_typeck_helpers.h"

// PLAN: Build up a set of conditions that are easier to solve
struct Context {
    class Revisitor {
    public:
        virtual ~Revisitor() = default;
        virtual const Span& span() const = 0;
        virtual void fmt(::std::ostream& os) const = 0;
        virtual bool revisit(Context& context, bool is_fallback) = 0;
    };

    struct Binding {
        RcString name;
        ::HIR::TypeRef ty;
        //unsigned int ivar;
    };

    /// Inferrence variable equalities
    struct Coercion {
        unsigned rule_idx;
        ::HIR::TypeRef left_ty;
        ::HIR::ExprNodeP* right_node_ptr;

        friend ::std::ostream& operator<<(::std::ostream& os, const Coercion& v);
    };

    struct IVarPossible {
        struct CoerceTy {
            enum Op {
                Coercion,
                Unsizing,
            } op;

            ::HIR::TypeRef ty;

            CoerceTy(::HIR::TypeRef ty, bool is_coerce);
        };

        // Strong disable (depends on a trait impl)
        bool force_disable = false;
        //
        bool force_no_to = false;
        bool force_no_from = false;

        // Possible types from trait impls (may introduce new types)
        // - This is union of all input bounds
        bool has_bounded = false;
        /// If the bounds include this ivar, mark differently (permits any incoming type, but types can be removed)
        /// - If an existing type isn't in the incoming set, it is removed
        /// - But any type in an incoming set is accepted (even if it doesn't already exist)
        bool bounds_include_self = false;
        // Target types for coercion/unsizing (these types are known to exist in the function)
        ::std::vector<CoerceTy> types_coerce_to;
        // Source types for coercion/unsizing (these types are known to exist in the function)
        ::std::vector<CoerceTy> types_coerce_from;
        // Possible default types (from generic defaults)
        ::std::set<::HIR::TypeRef> types_default;

        ::std::vector<::HIR::TypeRef> bounded;

        void reset();

        bool has_rules() const;

        void merge_from(const IVarPossible& source);
    };

    struct Associated {
        struct StallDependency {
            unsigned index;
            ::HIR::TypeRef resolved;
        };

        struct CapturedIvarPossible {
            unsigned index;
            IVarPossible possibilities;
        };

        unsigned rule_idx;
        Span span;
        ::HIR::TypeRef left_ty;

        ::HIR::SimplePath trait;
        ::HIR::PathParams params;
        ::HIR::TypeRef impl_ty;
        RcString name; // if "", no type is used (and left is ignored) - Just does trait selection
        ::HIR::PathParams aty_pp;

        // HACK: operators are special - the result when both types are primitives is ALWAYS the lefthand side
        bool is_operator;
        typeck::PrimitiveOperator operator_kind;
        bool is_ambiguous = false;

        ::std::vector<StallDependency> stalled_on;
        ::std::vector<CapturedIvarPossible> stalled_possibilities;

        friend ::std::ostream& operator<<(::std::ostream& os, const Associated& v);
    };

    const ::HIR::Crate& m_crate;
    const ::HIR::TraitImpl* m_current_trait_impl;

    ::std::vector<Binding> m_bindings;
    HMTypeInferrence m_ivars;
    TraitResolution m_resolve;

    unsigned next_rule_idx;
    // NOTE: unique_ptr used to reduce copy costs of the list
    ::std::vector<::std::unique_ptr<Coercion>> link_coerce;
    // Expected types are available while aggregate fields are enumerated,
    // before the corresponding coercion rules are solved.
    ::std::unordered_map<const ::HIR::ExprNode*, ::HIR::TypeRef> coercion_hints;
    ::std::vector<Associated> link_assoc;
    /// Nodes that need revisiting (e.g. method calls when the receiver isn't known)
    ::std::vector<::HIR::ExprNode*> to_visit;
    /// Callback-based revisits (e.g. for slice patterns handling slices/arrays)
    ::std::vector<::std::unique_ptr<Revisitor>> adv_revisits;

    // Keep track of if an ivar is used in a context where it has to be Sized
    // - If it is, then we can discount any unsized possibilities
    ::std::vector<bool> m_ivars_sized;
    ::std::vector<IVarPossible> possible_ivar_vals;
    ::std::vector<Associated::CapturedIvarPossible>* m_possible_ivar_sink = nullptr;

    IVarPossible* get_possible_ivar_sink(unsigned index);

    struct TaitEntry {
        HIR::PathParams params;
        HIR::TypeRef our_type;

        TaitEntry(const HIR::PathParams& p, HIR::TypeRef t);
    };

    ::std::map<HIR::TypeDataErasedTypeAliasInner*, TaitEntry> m_erased_type_aliases;

    const ::HIR::SimplePath m_lang_Box;

    Context(const ::HIR::Crate& crate, const ::HIR::GenericParams* impl_params, const ::HIR::GenericParams* item_params, const ::HIR::SimplePath& mod_path, const ::HIR::GenericPath* current_trait, const ::HIR::TraitImpl* current_trait_impl);

    void dump() const;

    bool take_changed() {
        return m_ivars.take_changed();
    }

    bool has_rules() const {
        return !(link_coerce.empty() && link_assoc.empty() && to_visit.empty() && adv_revisits.empty());
    }

    inline void add_ivars(::HIR::TypeRef& ty) {
        m_ivars.add_ivars(ty);
    }

    // - Equate two types, with no possibility of coercion
    //  > Errors if the types are incompatible.
    //  > Forces types if one side is an infer
    void equate_types(const Span& sp, const ::HIR::TypeData* l, const ::HIR::TypeData* r);
    void equate_types_inner(const Span& sp, const ::HIR::TypeData* l, const ::HIR::TypeData* r);
    // - Equate two types, allowing inferrence
    void equate_types_coerce(const Span& sp, const ::HIR::TypeData* l, ::HIR::ExprNodeP& node_ptr);
    void record_coercion_hint(const ::HIR::TypeData* type, ::HIR::ExprNodeP& node_ptr);

    const ::HIR::TypeData* coercion_hint(const ::HIR::ExprNode& node) const;
    // - Equate a type to an associated type (if name == "", no equation is done, but trait is searched)
    void equate_types_assoc(const Span& sp, const ::HIR::TypeData* l, const ::HIR::SimplePath& trait, ::HIR::PathParams params, const ::HIR::TypeData* impl_ty, const char* name, const ::HIR::PathParams& aty_pp, bool is_op = false, typeck::PrimitiveOperator operator_kind = typeck::PrimitiveOperator::None);

    bool is_current_operator_impl(const ImplRef& impl) const;

    // A Deref implementation for a native pointer/reference receives `&Self`.
    // Dereferencing that receiver is the native step needed to recover `Self`,
    // not another dispatch through a potentially overlapping Deref impl.
    bool is_current_native_deref_receiver(const ::HIR::SimplePath& deref_trait, const ::HIR::TypeData* operand) const;

    // Equate const generics (values)
    void equate_values(const Span& sp, const ::HIR::ConstGeneric& rl, const ::HIR::ConstGeneric& rr);

    /// Adds a `ty: Sized` bound to the contained ivars.
    void require_sized(const Span& sp, const ::HIR::TypeData* ty);

    // - Add a trait bound (gets encoded as an associated type bound)
    void add_trait_bound(const Span& sp, const ::HIR::TypeData* impl_ty, const ::HIR::SimplePath& trait, ::HIR::PathParams params) {
        equate_types_assoc(sp, m_crate.m_types.infer(), trait, mv$(params), impl_ty, "", {}, false);
    }

    /// Get the `possible_ivar_vals` entry for the given ivar index
    /// Returns `nullptr` if the ivar is already known
    IVarPossible* get_ivar_possibilities(const Span& sp, unsigned int ivar_index);

    enum class IvarUnknownType {
        /// Coercion to an unknown type (disables
        To,
        /// Coercion from an unknown type
        From,
        /// Bounded to be an unknown type (a strong disable)
        Bound,
    };
    /// Type is unknown (e.g. no used/results from a trait impl that can't be looked up)
    void possible_equate_type_unknown(const Span& sp, const ::HIR::TypeData* ty, IvarUnknownType src_ty);
    /// Type must be one of the provided set
    void possible_equate_type_bounds(const Span& sp, const ::HIR::TypeData* ty, ::std::vector<::HIR::TypeRef> t);

    // ----
    // IVar possibilties
    // ----

    enum class PossibleTypeSource {
        CoerceTo,   //!< IVar must coerce to this type
        UnsizeTo,   //!< IVar must unsize to this type
        CoerceFrom, //!< IVar must coerce from this type
        UnsizeFrom, //!< IVar must unsize from this type
    };

    /// Default type
    //void possible_equate_ivar_def(unsigned int ivar_index, const ::HIR::TypeData* t);

    /// Record that the IVar may be this type (and what the source is)
    void possible_equate_ivar(const Span& sp, unsigned int ivar_index, const ::HIR::TypeData* t, PossibleTypeSource src_ty);
    /// Add a possible type for an ivar (which is used if only one possibility meets available bounds)
    void possible_equate_ivar_bounds(const Span& sp, unsigned int ivar_index, ::std::vector<::HIR::TypeRef> t);
    /// Record that the IVar is equated to an unknown type
    void possible_equate_ivar_unknown(const Span& sp, unsigned int ivar_index, IvarUnknownType src_ty);

    // ----
    // Patterns and bindings
    // ----

    // - Add a pattern binding (forcing the type to match)
    void handle_pattern(const Span& sp, ::HIR::Pattern& pat, const ::HIR::TypeData* type, bool is_irrefutable = false);
    void handle_pattern_direct_inner(const Span& sp, ::HIR::Pattern& pat, const ::HIR::TypeData* type);
    void add_binding_inner(const Span& sp, const ::HIR::PatternBinding& pb, ::HIR::TypeRef type);

    void add_var(const Span& sp, unsigned int index, const RcString& name, ::HIR::TypeRef type);
    const ::HIR::TypeData* get_var(const Span& sp, unsigned int idx) const;

    // - Add a revisit entry
    void add_revisit(::HIR::ExprNode& node);
    void add_revisit_adv(::std::unique_ptr<Revisitor> ent);

    const ::HIR::TypeData* get_type(const ::HIR::TypeData* ty) const {
        return m_ivars.get_type(ty);
    }

    /// Create an autoderef operation from val_node->m_res_type to ty_dst (handling implicit unsizing)
    ::HIR::ExprNodeP create_autoderef(::HIR::ExprNodeP val_node, ::HIR::TypeRef ty_dst) const;

private:
    void add_ivars_params(::HIR::PathParams& params) {
        m_ivars.add_ivars_params(params);
    }
};

namespace typecheck {
    extern bool visit_call_populate_cache(Context& context, const Span& sp, ::HIR::Path& path, ::HIR::ExprCallCache& cache) __attribute__((warn_unused_result));
}

extern void Typecheck_Code_CS__EnumerateRules(Context& context, const typeck::ModuleState& ms, t_args& args, const ::HIR::TypeData* result_type, ::HIR::ExprPtr& expr, ::HIR::ExprNodeP& root_ptr);
