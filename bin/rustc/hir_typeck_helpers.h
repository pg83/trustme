#pragma once

#include "hir_hir.h"
#include "hir_expr.h" // t_trait_list

#include "hir_typeck_common.h"
#include "hir_typeck_resolve_common.h"

bool type_is_unbounded_infer(const ::HIR::TypeData* ty);

class HMTypeInferrence {
public:
    struct FmtType {
        const HMTypeInferrence& ctxt;
        const ::HIR::TypeData* ty;

        FmtType(const HMTypeInferrence& ctxt, const ::HIR::TypeData* ty);

        friend ::std::ostream& operator<<(::std::ostream& os, const FmtType& x);
    };

    struct FmtPP {
        const HMTypeInferrence& ctxt;
        const ::HIR::PathParams& pps;

        FmtPP(const HMTypeInferrence& ctxt, const ::HIR::PathParams& pps);

        friend ::std::ostream& operator<<(::std::ostream& os, const FmtPP& x);
    };

public: // ?? - Needed once, anymore?
    struct IVar {
        unsigned int alias; // If not ~0, this points to another ivar
        ::HIR::TypeRef type; // Null only when alias != ~0

        explicit IVar(::HIR::TypeRef type);

        bool is_alias() const {
            return alias != ~0u;
        }
    };

    ::std::vector<IVar> ivars;

    struct IVarValue {
        unsigned int alias;
        ::std::unique_ptr<::HIR::ConstGeneric> val;

        IVarValue();

        bool is_alias() const {
            return alias != ~0u;
        }
    };

    ::std::vector<IVarValue> values;

    HIR::TypeInterner& types;
    bool hasChanged;
    ::std::vector<::HIR::TypeRef> expandStack;

public:
    explicit HMTypeInferrence(HIR::TypeInterner& types);

    bool peek_changed() const {
        return hasChanged;
    }

    bool take_changed();

    void mark_change();

    void compactIvars();
    bool applyDefaults();

    void dump() const;

    void print_type(::std::ostream& os, const ::HIR::TypeData* tr, LList<const ::HIR::TypeData*> stack = {}) const;
    void print_genericpath(::std::ostream& os, const ::HIR::GenericPath& pps, LList<const ::HIR::TypeData*> stack) const;
    void print_pathparams(::std::ostream& os, const ::HIR::PathParams& pps, LList<const ::HIR::TypeData*> stack = {}) const;

    FmtType fmtType(const ::HIR::TypeData* tr) const {
        return FmtType(*this, tr);
    }

    FmtPP fmt(const ::HIR::PathParams& v) const {
        return FmtPP(*this, v);
    }

    /// Add (and bind) all '_' types in `type`
    void addIvars(::HIR::TypeRef& type);
    void addIvars(::HIR::ConstGeneric& val);
    // (helper) Add ivars to path parameters
    void addIvarsParams(::HIR::PathParams& params);

    struct ResolvePlaceholders: public HIR::ResolvePlaceholders {
        const HMTypeInferrence& parent;

        ResolvePlaceholders(const HMTypeInferrence& parent);

        const ::HIR::TypeData* getType(const Span& sp, const HIR::TypeData* ty) const override;

        const ::HIR::ConstGeneric& getVal(const Span& sp, const HIR::ConstGeneric& v) const override;
    };

    ResolvePlaceholders callbackResolveInfer() const {
        return ResolvePlaceholders(*this);
    }

    // Mutation
    unsigned int new_ivar(HIR::InferClass ic = HIR::InferClass::None);
    ::HIR::TypeRef new_ivar_tr(HIR::InferClass ic = HIR::InferClass::None);
    void set_ivar_to(unsigned int slot, ::HIR::TypeRef type);
    void ivar_unify(unsigned int left_slot, unsigned int right_slot);

    //
    unsigned int new_ivar_val();
    void set_ivar_val_to(unsigned int slot, ::HIR::ConstGeneric val);
    void ivar_val_unify(unsigned int left_slot, unsigned int right_slot);

    ::HIR::ConstGeneric new_val_ivar();
    void set_val_ivar_to(unsigned int slot, ::HIR::ConstGeneric val);

    // Lookup
    //::HIR::TypeRef& get_type(::HIR::TypeRef& type);
    const ::HIR::TypeData* getType(const ::HIR::TypeData* type) const;
    ::HIR::TypeRef& getType(unsigned idx);
    const ::HIR::TypeData* getType(unsigned idx) const;

    const ::HIR::ConstGeneric& getValue(const ::HIR::ConstGeneric& val) const;
    const ::HIR::ConstGeneric& getValue(unsigned idx) const;

    void checkForLoops();
    void expandIvars(::HIR::TypeRef& type);
    void expandIvarsParams(::HIR::PathParams& params);

    // Helpers
    bool pathparams_contain_ivars(const ::HIR::PathParams& pps, bool only_unbound) const;
    bool type_contains_ivars(const ::HIR::TypeData* ty, bool only_unbound = false) const;
    bool pathparams_equal(const ::HIR::PathParams& pps_l, const ::HIR::PathParams& pps_r) const;
    bool types_equal(const ::HIR::TypeData* l, const ::HIR::TypeData* r) const;

private:
    IVar& getPointedIvar(unsigned int slot) const;
};

class NextTraitGoalEvaluator;

class TraitResolution: public TraitResolveCommon {
    const HIR::SimplePath& mLangDeref;
    const HMTypeInferrence& ivars;

public:
    const ::HIR::SimplePath& visPath;

private:
    const ::HIR::GenericPath* currentTraitPath;
    const ::HIR::Trait* currentTraitPtr;

    // A legacy solver invocation only needs this stack while it is actively
    // resolving nested trait bounds.  The goal is canonicalised before it is
    // stored, so fresh implementation placeholders do not hide a cycle.
    struct LegacyTraitGoal {
        ::HIR::SimplePath trait;
        ::HIR::PathParams params;
        ::HIR::TypeRef type;
        bool hasParams;

        LegacyTraitGoal(
            const ::HIR::SimplePath& trait,
            const ::HIR::PathParams& params,
            bool hasParams,
            const ::HIR::TypeData* type
        );

        bool matches(
            const ::HIR::SimplePath& other_trait,
            const ::HIR::PathParams& other_params,
            bool other_has_params,
            const ::HIR::TypeData* other_type
        ) const;
    };

    mutable ::std::vector<LegacyTraitGoal> legacyTraitGoalStack;
    mutable uint64_t freshImplPlaceholderCounter = 0;
    mutable ::std::map<std::string, HIR::TypeRef> eatCache;
    mutable ::std::vector<::HIR::TypeRef> eatActiveStack;
    // Owned by the crate ObjPool.  TraitResolution only keeps a stable
    // pointer into the compiler-lifetime arena.
    mutable NextTraitGoalEvaluator* nextSolver = nullptr;
    // Coherence probes use an isolated inference table, so overlap checks
    // cannot bind or append variables in the caller's type-checking context.
    mutable HMTypeInferrence coherenceIvars;
    mutable TraitResolution* coherenceResolve = nullptr;
    ::std::function<void(const Span&, const ::HIR::TypeData*, const ::HIR::TypeData*)> inherentTypeConstraint;

public:
    TraitResolution(const HMTypeInferrence& ivars, const ::HIR::Crate& crate, const ::HIR::GenericParams* impl_params, const ::HIR::GenericParams* item_params, const ::HIR::SimplePath& vis_path, const ::HIR::GenericPath* current_trait);
    ~TraitResolution();

    void set_generic_context(const ::HIR::GenericParams* impl_params, const ::HIR::GenericParams* item_params);

    void set_inherent_type_constraint(::std::function<void(const Span&, const ::HIR::TypeData*, const ::HIR::TypeData*)> constraint) {
        inherentTypeConstraint = mv$(constraint);
    }

    const ::HIR::GenericPath* current_trait_path() const {
        return currentTraitPath;
    }

    ::HIR::Compare comparePp(const Span& sp, const ::HIR::PathParams& left, const ::HIR::PathParams& right) const;

    const ::HIR::TypeData* resolve_type(const ::HIR::TypeData* type) const {
        return ivars.getType(type);
    }

    ::HIR::Compare compareTy(const Span& sp, const ::HIR::TypeData* left, const ::HIR::TypeData* right) const {
        return left->compareWithPlaceholders(sp, right, ivars.callbackResolveInfer());
    }

    bool type_contains_ivars(const ::HIR::TypeData* type) const {
        return ivars.type_contains_ivars(type, false);
    }

    bool params_contain_ivars(const ::HIR::PathParams& params) const {
        return ivars.pathparams_contain_ivars(params, false);
    }

    void compactIvars(HMTypeInferrence& ivars);

    bool hasAssociatedType(const ::HIR::TypeData* ty) const;

    /// Expand any located associated types in the input, operating in-place and returning the result
    ::HIR::TypeRef expandAssociatedTypes(const Span& sp, ::HIR::TypeRef input) const;

    const ::HIR::TypeData* expandAssociatedTypes(const Span& sp, const ::HIR::TypeData* input, ::HIR::TypeRef& tmp) const;

    void expandAssociatedTypesParams(const Span& sp, ::HIR::PathParams& params) const;

    typedef ::std::function<bool(HIR::Compare cmp, const ::HIR::TypeData*, const ::HIR::GenericPath& trait_path, const CachedBound& info)> t_cb_bound;
    bool iterate_bounds_traits(const Span& sp, const HIR::TypeData* type, const HIR::SimplePath& trait, t_cb_bound cb) const;
    bool iterate_bounds_traits(const Span& sp, const HIR::TypeData* type, t_cb_bound cb) const;
    bool iterate_bounds_traits(const Span& sp, t_cb_bound cb) const;
    bool iterate_aty_bounds(const Span& sp, const ::HIR::Path::Data::Data_UfcsKnown& pe, ::std::function<bool(const ::HIR::TraitPath&)> cb) const;

    typedef ::std::function<bool(const ::HIR::TypeData*, const ::HIR::PathParams&, const ::HIR::TraitPath::assocListT&)> t_cb_trait_impl;
    typedef ::std::function<bool(ImplRef, ::HIR::Compare)> t_cb_trait_impl_r;

    /// Searches for a trait impl using the solver selected for this session.
    bool findTraitImpls(const Span& sp, const ::HIR::SimplePath& trait, const ::HIR::PathParams& params, const ::HIR::TypeData* type, t_cb_trait_impl_r callback, bool magic_trait_impls = true) const;

    /// Candidate lookup used by the legacy selector and by next-solver
    /// candidate assembly.  Callers performing trait selection must use
    /// `find_trait_impls`, not this assembly primitive.
    bool findTraitImplsLegacy(const Span& sp, const ::HIR::SimplePath& trait, const ::HIR::PathParams& params, const ::HIR::TypeData* type, t_cb_trait_impl_r callback, bool magic_trait_impls = true, bool search_crate = true, bool search_bounds = true) const;

    /// Evaluate a trait goal using the next-solver candidate model.  Candidate
    /// assembly is exhaustive, impl where-clauses are evaluated recursively,
    /// and only a merged response is exposed to the caller.  `assoc_name` and
    /// `assoc_type` add an associated-type equality to the goal.
    bool findTraitImplsNext(
        const Span& sp,
        const ::HIR::SimplePath& trait,
        const ::HIR::PathParams& params,
        const ::HIR::TypeData* type,
        t_cb_trait_impl_r callback,
        const char* assocName = nullptr,
        const ::HIR::TypeData* assocType = nullptr,
        const ::HIR::PathParams* assocParams = nullptr
    ) const;

    /// Whether two concrete impl candidates may apply to one canonical goal.
    /// With next-solver coherence enabled this unifies both headers and proves
    /// both sets of where-clauses in an isolated inference context.
    bool impls_overlap(const Span& sp, const ImplRef& left, const ImplRef& right) const;

    typedef ::std::function<bool(const ::HIR::TraitPath&)> t_cb_find_trait;
    /// Locate a named trait in the provied trait (either itself or as a parent trait)
    bool findNamedTraitInTrait(const Span& sp, const ::HIR::SimplePath& des, const ::HIR::PathParams& params, const ::HIR::Trait& trait_ptr, const ::HIR::SimplePath& trait_path, const ::HIR::PathParams& pp, const ::HIR::TypeData* self_type, t_cb_find_trait callback) const;
    /// Search for a trait implementation in current bounds
    bool findTraitImplsBound(const Span& sp, const ::HIR::SimplePath& trait, const ::HIR::PathParams& params, const ::HIR::TypeData* type, t_cb_trait_impl_r callback) const;

    /// Search for a trait implementation in the crate
    bool findTraitImplsCrate(const Span& sp, const ::HIR::SimplePath& trait, const ::HIR::PathParams& params, const ::HIR::TypeData* type, t_cb_trait_impl_r callback) const {
        return findTraitImplsCrate(sp, trait, &params, type, callback);
    }

    /// Search for a trait implementation in the crate (allows nullptr to ignore params)
    bool findTraitImplsCrate(const Span& sp, const ::HIR::SimplePath& trait, const ::HIR::PathParams* params, const ::HIR::TypeData* type, t_cb_trait_impl_r callback) const;
    /// Check for magic (automatically determined) trait implementations
    bool findTraitImplsMagic(const Span& sp, const ::HIR::SimplePath& trait, const ::HIR::PathParams& params, const ::HIR::TypeData* type, t_cb_trait_impl_r callback) const;
    /// Check for trait implementations on magical types (closures, generators, fn pointers, ...)
    bool findTraitImplsTypes(const Span& sp, const ::HIR::SimplePath& trait, const ::HIR::PathParams& params, const ::HIR::TypeData* type, t_cb_trait_impl_r callback) const;

    struct RecursionDetected {};

private:
    friend class NextTraitGoalEvaluator;

    ::HIR::PathParams make_fresh_impl_params(const ::HIR::GenericParams& params) const;

    ::HIR::Compare checkAutoTraitImplDestructure(const Span& sp, const ::HIR::SimplePath& trait, const ::HIR::PathParams* params_ptr, const ::HIR::TypeData* type) const;
    ::HIR::Compare fticCheckParams(
        const Span& sp,
        const ::HIR::SimplePath& trait,
        const ::HIR::PathParams* params,
        const ::HIR::TypeData* type,
        const ::HIR::GenericParams& impl_params_def,
        const ::HIR::PathParams& impl_trait_args,
        const ::HIR::TypeData* impl_ty,
        /*Out->*/ HIR::PathParams& out_impl_params,
        bool evaluateBounds = true
    ) const;

public:
    enum class AutoderefBorrow {
        None,
        Shared,
        Unique,
        Owned,
    };
    friend ::std::ostream& operator<<(::std::ostream& os, const AutoderefBorrow& x);
    /// Locate the named method by applying auto-dereferencing.
    /// \return Number of times deref was applied (or ~0 if _ was hit)
    unsigned int autoderefFindMethod(
        const Span& sp,
        const HIR::t_trait_list& traits,
        const ::std::vector<unsigned>& ivars,
        unsigned int type_ivar_count,
        const ::HIR::TypeData* top_ty,
        const RcString& method_name,
        /* Out -> */ ::std::vector<::std::pair<AutoderefBorrow, ::HIR::Path>>& possibilities
    ) const;
    /// Locate the named field by applying auto-dereferencing.
    /// \return Number of times deref was applied (or ~0 if _ was hit)
    unsigned int autoderefFindField(const Span& sp, const ::HIR::TypeData* top_ty, const RcString& name, /* Out -> */ ::HIR::TypeRef& fieldType) const;

    enum class AutoderefResult {
        NoMatch,
        Match,
        Ambiguous,
    };

    /// Probe one automatic-dereference step without changing inference state.
    /// `impl_type` is populated for a trait-based step so that a caller which
    /// actually selects this step can commit the impl response afterwards.
    AutoderefResult autoderefStep(
        const Span& sp,
        const ::HIR::TypeData* ty,
        ::HIR::TypeRef& target,
        ::std::optional<::HIR::TypeRef>* impl_type = nullptr
    ) const;

    /// Apply an automatic dereference
    const ::HIR::TypeData* autoderef(const Span& sp, const ::HIR::TypeData* ty, ::HIR::TypeRef& tmp_type) const;

    bool findField(const Span& sp, const ::HIR::TypeData* ty, const RcString& name, /* Out -> */ ::HIR::TypeRef& fieldType) const;

    enum class MethodAccess {
        Shared,
        Unique,
        Move,
    };

private:
    ::std::optional<::HIR::TypeRef> checkMethodReceiver(const Span& sp, const ::HIR::Function& fcn, const ::HIR::TypeData* ty, TraitResolution::MethodAccess access) const;

public:
    enum class AllowedReceivers {
        All,
        AnyBorrow,
        SharedBorrow,
        Value,
        Box,
    };
    friend ::std::ostream& operator<<(::std::ostream& os, const AllowedReceivers& x);
    bool findMethod(const Span& sp, const HIR::t_trait_list& traits, const ::std::vector<unsigned>& ivars, unsigned int type_ivar_count, const ::HIR::TypeData* ty, const RcString& method_name, MethodAccess access, AutoderefBorrow borrowType, /* Out -> */ ::std::vector<::std::pair<AutoderefBorrow, ::HIR::Path>>& possibilities) const;

    /// Locates a named method in a trait, and returns the path of the trait that contains it (with fixed parameters)
    const ::HIR::Function* trait_contains_method(const Span& sp, const ::HIR::GenericPath& trait_path, const ::HIR::Trait& trait_ptr, const ::HIR::TypeData* self, const RcString& name, ::HIR::GenericPath& out_path) const;
    bool trait_contains_type(const Span& sp, const ::HIR::GenericPath& trait_path, const ::HIR::Trait& trait_ptr, const char* name, ::HIR::GenericPath& out_path) const;

    ::HIR::Compare type_is_sized(const Span& sp, const ::HIR::TypeData* ty) const;
    ::HIR::Compare type_is_copy(const Span& sp, const ::HIR::TypeData* ty) const;
    ::HIR::Compare type_is_clone(const Span& sp, const ::HIR::TypeData* ty) const;

    // If `new_type_callback` is populated, it will be called with the actual/possible dst_type
    // If `infer_callback` is populated, it will be called when either side is an ivar
    ::HIR::Compare canUnsize(const Span& sp, const ::HIR::TypeData* dstTy, const ::HIR::TypeData* src_ty, ::std::function<void(::HIR::TypeRef new_dst)> new_type_callback) const {
        return canUnsize(sp, dstTy, src_ty, &new_type_callback);
    }

    ::HIR::Compare canUnsize(const Span& sp, const ::HIR::TypeData* dstTy, const ::HIR::TypeData* src_ty, ::std::function<void(::HIR::TypeRef new_dst)>* new_type_callback, ::std::function<void(const ::HIR::TypeData* dst, const ::HIR::TypeData* src)>* infer_callback = nullptr) const;

    const ::HIR::TypeData* type_is_owned_box(const Span& sp, const ::HIR::TypeData* ty) const;

private:
    void expandAssociatedTypesInplace(const Span& sp, ::HIR::TypeRef& input, LList<const ::HIR::TypeData*> stack) const;
    bool expandAssociatedTypesInplaceUfcsInherent(const Span& sp, ::HIR::TypeRef& input, LList<const ::HIR::TypeData*> stack) const;
    void expandAssociatedTypesInplaceUfcsKnown(const Span& sp, ::HIR::TypeRef& input, LList<const ::HIR::TypeData*> stack) const;
};
