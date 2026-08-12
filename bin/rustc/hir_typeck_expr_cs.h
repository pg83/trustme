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
        ::HIR::TypeRef leftTy;
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

            CoerceTy(::HIR::TypeRef ty, bool isCoerce);
        };

        // Strong disable (depends on a trait impl)
        bool forceDisable = false;
        //
        bool forceNoTo = false;
        bool forceNoFrom = false;

        // Possible types from trait impls (may introduce new types)
        // - This is union of all input bounds
        bool hasBounded = false;
        /// If the bounds include this ivar, mark differently (permits any incoming type, but types can be removed)
        /// - If an existing type isn't in the incoming set, it is removed
        /// - But any type in an incoming set is accepted (even if it doesn't already exist)
        bool boundsIncludeSelf = false;
        // Target types for coercion/unsizing (these types are known to exist in the function)
        ::std::vector<CoerceTy> types_coerce_to;
        // Source types for coercion/unsizing (these types are known to exist in the function)
        ::std::vector<CoerceTy> types_coerce_from;
        // Possible default types (from generic defaults)
        ::std::set<::HIR::TypeRef> types_default;

        ::std::vector<::HIR::TypeRef> bounded;

        void reset();

        bool hasRules() const;

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
        ::HIR::TypeRef leftTy;

        ::HIR::SimplePath trait;
        ::HIR::PathParams params;
        ::HIR::TypeRef implTy;
        RcString name; // if "", no type is used (and left is ignored) - Just does trait selection
        ::HIR::PathParams atyPp;

        // HACK: operators are special - the result when both types are primitives is ALWAYS the lefthand side
        bool isOperator;
        typeck::PrimitiveOperator operator_kind;
        bool isAmbiguous = false;

        ::std::vector<StallDependency> stalled_on;
        ::std::vector<CapturedIvarPossible> stalled_possibilities;

        friend ::std::ostream& operator<<(::std::ostream& os, const Associated& v);
    };

    const ::HIR::Crate& crate;
    const ::HIR::TraitImpl* currentTraitImpl;

    ::std::vector<Binding> mBindings;
    HMTypeInferrence ivars;
    TraitResolution mResolve;

    unsigned next_rule_idx;
    // NOTE: unique_ptr used to reduce copy costs of the list
    ::std::vector<::std::unique_ptr<Coercion>> linkCoerce;
    // Expected types are available while aggregate fields are enumerated,
    // before the corresponding coercion rules are solved.
    ::std::unordered_map<const ::HIR::ExprNode*, ::HIR::TypeRef> coercionHints;
    ::std::vector<Associated> linkAssoc;
    /// Nodes that need revisiting (e.g. method calls when the receiver isn't known)
    ::std::vector<::HIR::ExprNode*> to_visit;
    /// Callback-based revisits (e.g. for slice patterns handling slices/arrays)
    ::std::vector<::std::unique_ptr<Revisitor>> advRevisits;

    // Keep track of if an ivar is used in a context where it has to be Sized
    // - If it is, then we can discount any unsized possibilities
    ::std::vector<bool> ivarsSized;
    ::std::vector<IVarPossible> possible_ivar_vals;
    ::std::vector<Associated::CapturedIvarPossible>* possibleIvarSink = nullptr;

    IVarPossible* getPossibleIvarSink(unsigned index);

    struct TaitEntry {
        HIR::PathParams params;
        HIR::TypeRef our_type;

        TaitEntry(const HIR::PathParams& p, HIR::TypeRef t);
    };

    ::std::map<HIR::TypeDataErasedTypeAliasInner*, TaitEntry> erasedTypeAliases;

    const ::HIR::SimplePath mLangBox;

    Context(const ::HIR::Crate& crate, const ::HIR::GenericParams* impl_params, const ::HIR::GenericParams* item_params, const ::HIR::SimplePath& mod_path, const ::HIR::GenericPath* current_trait, const ::HIR::TraitImpl* current_trait_impl);

    void dump() const;

    bool take_changed() {
        return ivars.take_changed();
    }

    bool hasRules() const {
        return !(linkCoerce.empty() && linkAssoc.empty() && to_visit.empty() && advRevisits.empty());
    }

    inline void addIvars(::HIR::TypeRef& ty) {
        ivars.addIvars(ty);
    }

    // - Equate two types, with no possibility of coercion
    //  > Errors if the types are incompatible.
    //  > Forces types if one side is an infer
    void equateTypes(const Span& sp, const ::HIR::TypeData* l, const ::HIR::TypeData* r);
    void equateTypesInner(const Span& sp, const ::HIR::TypeData* l, const ::HIR::TypeData* r);
    // - Equate two types, allowing inferrence
    void equateTypesCoerce(const Span& sp, const ::HIR::TypeData* l, ::HIR::ExprNodeP& node_ptr);
    void record_coercion_hint(const ::HIR::TypeData* type, ::HIR::ExprNodeP& node_ptr);

    const ::HIR::TypeData* coercionHint(const ::HIR::ExprNode& node) const;
    // - Equate a type to an associated type (if name == "", no equation is done, but trait is searched)
    void equateTypesAssoc(const Span& sp, const ::HIR::TypeData* l, const ::HIR::SimplePath& trait, ::HIR::PathParams params, const ::HIR::TypeData* implTy, const char* name, const ::HIR::PathParams& atyPp, bool isOp = false, typeck::PrimitiveOperator operator_kind = typeck::PrimitiveOperator::None);

    bool isCurrentOperatorImpl(const ImplRef& impl) const;

    // A Deref implementation for a native pointer/reference receives `&Self`.
    // Dereferencing that receiver is the native step needed to recover `Self`,
    // not another dispatch through a potentially overlapping Deref impl.
    bool isCurrentNativeDerefReceiver(const ::HIR::SimplePath& derefTrait, const ::HIR::TypeData* operand) const;

    // Equate const generics (values)
    void equateValues(const Span& sp, const ::HIR::ConstGeneric& rl, const ::HIR::ConstGeneric& rr);

    /// Adds a `ty: Sized` bound to the contained ivars.
    void require_sized(const Span& sp, const ::HIR::TypeData* ty);

    // - Add a trait bound (gets encoded as an associated type bound)
    void addTraitBound(const Span& sp, const ::HIR::TypeData* implTy, const ::HIR::SimplePath& trait, ::HIR::PathParams params) {
        equateTypesAssoc(sp, crate.types.infer(), trait, mv$(params), implTy, "", {}, false);
    }

    /// Get the `possible_ivar_vals` entry for the given ivar index
    /// Returns `nullptr` if the ivar is already known
    IVarPossible* getIvarPossibilities(const Span& sp, unsigned int ivarIndex);

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
    void possible_equate_ivar(const Span& sp, unsigned int ivarIndex, const ::HIR::TypeData* t, PossibleTypeSource src_ty);
    /// Add a possible type for an ivar (which is used if only one possibility meets available bounds)
    void possible_equate_ivar_bounds(const Span& sp, unsigned int ivarIndex, ::std::vector<::HIR::TypeRef> t);
    /// Record that the IVar is equated to an unknown type
    void possible_equate_ivar_unknown(const Span& sp, unsigned int ivarIndex, IvarUnknownType src_ty);

    // ----
    // Patterns and bindings
    // ----

    // - Add a pattern binding (forcing the type to match)
    void handlePattern(const Span& sp, ::HIR::Pattern& pat, const ::HIR::TypeData* type, bool is_irrefutable = false);
    void handlePatternDirectInner(const Span& sp, ::HIR::Pattern& pat, const ::HIR::TypeData* type);
    void addBindingInner(const Span& sp, const ::HIR::PatternBinding& pb, ::HIR::TypeRef type);

    void addVar(const Span& sp, unsigned int index, const RcString& name, ::HIR::TypeRef type);
    const ::HIR::TypeData* getVar(const Span& sp, unsigned int idx) const;

    // - Add a revisit entry
    void addRevisit(::HIR::ExprNode& node);
    void addRevisitAdv(::std::unique_ptr<Revisitor> ent);

    const ::HIR::TypeData* getType(const ::HIR::TypeData* ty) const {
        return ivars.getType(ty);
    }

    /// Create an autoderef operation from val_node->m_res_type to ty_dst (handling implicit unsizing)
    ::HIR::ExprNodeP createAutoderef(::HIR::ExprNodeP val_node, ::HIR::TypeRef ty_dst) const;

private:
    void addIvarsParams(::HIR::PathParams& params) {
        ivars.addIvarsParams(params);
    }
};

namespace typecheck {
    extern bool visit_call_populate_cache(Context& context, const Span& sp, ::HIR::Path& path, ::HIR::ExprCallCache& cache) __attribute__((warn_unused_result));
}

extern void TypecheckCodeCSEnumerateRules(Context& context, const typeck::ModuleState& ms, t_args& args, const ::HIR::TypeData* result_type, ::HIR::ExprPtr& expr, ::HIR::ExprNodeP& root_ptr);
