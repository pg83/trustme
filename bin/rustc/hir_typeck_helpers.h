#pragma once

#include "hir_hir.h"
#include "hir_expr.h" // t_trait_list
#include "hir_typeck_common.h"
#include "hir_typeck_resolve_common.h"
#include <std/lib/vector.h>
#include <std/mem/obj_pool.h>
#include <std/sym/i_map.h>

bool typeIsUnboundedInfer(const HIRTypeData* ty);

// Crate-lifetime cache of solver answers for fully concrete goals (no
// inference variables, no generics, no placeholders): those answers cannot
// depend on any function's ParamEnv when the resolver carries no bounds, so
// they are shared across every per-function resolver and every phase.  The
// monomorphised phases (MIR inline, trans) query the same concrete goals
// thousands of times; without this each one rebuilt the candidate graph.
struct NextSolverCrateCache {
    struct Entry {
        Entry* next = nullptr;
        size_t hash = 0;
        HIRSimplePath trait;
        HIRPathParams params;
        const HIRTypeData* type = nullptr;
        unsigned certainty = 0;
        bool hasResponse = false;
        ImplRef response;
        HIRCompare responseCertainty = HIRCompare::Unequal;
    };

    stl::ObjPool::Ref pool;
    stl::IntMap<Entry*> index;

    NextSolverCrateCache()
        : pool(stl::ObjPool::fromMemory())
        , index(pool.mutPtr())
    {
    }

    Entry* find(size_t hash, const HIRSimplePath& trait, const HIRPathParams& params, const HIRTypeData* type) const {
        auto* head = index.find(hash);
        for (Entry* entry = head ? *head : nullptr; entry; entry = entry->next) {
            if (entry->type == type && entry->trait == trait && entry->params == params) {
                return entry;
            }
        }
        return nullptr;
    }

    Entry* insert(size_t hash, const HIRSimplePath& trait, HIRPathParams params, const HIRTypeData* type, unsigned certainty) {
        auto* entry = pool.mutPtr()->make<Entry>();
        entry->hash = hash;
        entry->trait = trait;
        entry->params = ::std::move(params);
        entry->type = type;
        entry->certainty = certainty;
        if (auto* head = index.find(hash)) {
            entry->next = *head;
            *head = entry;
        } else {
            index.insert(hash, entry);
        }
        return entry;
    }
};

class HMTypeInferrence {
public:
    struct FmtType {
        const HMTypeInferrence& ctxt;
        const HIRTypeData* ty;

        FmtType(const HMTypeInferrence& ctxt, const HIRTypeData* ty);

        friend ::std::ostream& operator<<(::std::ostream& os, const FmtType& x);
    };

    struct FmtPP {
        const HMTypeInferrence& ctxt;
        const HIRPathParams& pps;

        FmtPP(const HMTypeInferrence& ctxt, const HIRPathParams& pps);

        friend ::std::ostream& operator<<(::std::ostream& os, const FmtPP& x);
    };

public: // ?? - Needed once, anymore?
    struct IVar {
        unsigned int alias; // If not ~0, this points to another ivar
        HIRTypeRef type;    // Null only when alias != ~0

        explicit IVar(HIRTypeRef type);

        bool isAlias() const {
            return alias != ~0u;
        }
    };

    ::std::vector<IVar> ivars;

    struct IVarValue {
        unsigned int alias;
        ::std::unique_ptr<HIRConstGeneric> val;

        IVarValue();

        bool isAlias() const {
            return alias != ~0u;
        }
    };

    ::std::vector<IVarValue> values;

    HIRTypeInterner& types;
    bool hasChanged;
    // Bumped on every inference-table mutation; the goal cache keys on it
    // to stay warm across evaluations until the table actually changes.
    u64 mutationGeneration = 0;
    ::std::vector<HIRTypeRef> expandStack;
    stl::ObjPool::Ref aliasIvarPool;
    stl::IntMap<HIRTypeRef> aliasTypeIvars;
    stl::IntMap<HIRConstGeneric> aliasValueIvars;

public:
    explicit HMTypeInferrence(HIRTypeInterner& types);

    bool peekChanged() const {
        return hasChanged;
    }

    bool takeChanged();

    void markChange();

    void compactIvars();
    bool applyDefault(unsigned int index);

    void dump() const;

    void printType(::std::ostream& os, const HIRTypeData* tr, LList<const HIRTypeData*> stack = {}) const;
    void printGenericpath(::std::ostream& os, const HIRGenericPath& pps, LList<const HIRTypeData*> stack) const;
    void printPathparams(::std::ostream& os, const HIRPathParams& pps, LList<const HIRTypeData*> stack = {}) const;

    FmtType fmtType(const HIRTypeData* tr) const {
        return FmtType(*this, tr);
    }

    FmtPP fmt(const HIRPathParams& v) const {
        return FmtPP(*this, v);
    }

    /// Add (and bind) all '_' types in `type`
    void addIvars(HIRTypeRef& type);
    void addIvars(HIRConstGeneric& val);
    // (helper) Add ivars to path parameters
    void addIvarsParams(HIRPathParams& params);

    struct ResolvePlaceholders: public HIRResolvePlaceholders {
        const HMTypeInferrence& parent;

        ResolvePlaceholders(const HMTypeInferrence& parent);

        const HIRTypeData* getType(const Span& sp, const HIRTypeData* ty) const override;

        const HIRConstGeneric& getVal(const Span& sp, const HIRConstGeneric& v) const override;
    };

    ResolvePlaceholders callbackResolveInfer() const {
        return ResolvePlaceholders(*this);
    }

    // Mutation
    unsigned int newIvar(HIRInferClass ic = HIRInferClass::None);
    HIRTypeRef newIvarTr(HIRInferClass ic = HIRInferClass::None);
    void setIvarTo(unsigned int slot, HIRTypeRef type);
    void ivarUnify(unsigned int leftSlot, unsigned int rightSlot);

    unsigned int newIvarVal();
    void setIvarValTo(unsigned int slot, HIRConstGeneric val);
    void ivarValUnify(unsigned int leftSlot, unsigned int rightSlot);

    HIRConstGeneric newValIvar();
    void setValIvarTo(unsigned int slot, HIRConstGeneric val);

    // Lookup
    //::HIR::ASTType*& get_type(::HIR::ASTType*& type);
    const HIRTypeData* getType(const HIRTypeData* type) const;
    HIRTypeRef& getType(unsigned idx);
    const HIRTypeData* getType(unsigned idx) const;

    const HIRConstGeneric& getValue(const HIRConstGeneric& val) const;
    const HIRConstGeneric& getValue(unsigned idx) const;

    void checkForLoops();
    void expandIvars(HIRTypeRef& type);
    void expandIvars(HIRConstGeneric& value);
    void expandIvarsParams(HIRPathParams& params);

    // Helpers
    bool pathparamsContainIvars(const HIRPathParams& pps, bool onlyUnbound) const;
    bool typeContainsIvars(const HIRTypeData* ty, bool onlyUnbound = false) const;
    bool pathparamsEqual(const HIRPathParams& ppsL, const HIRPathParams& ppsR) const;
    bool typesEqual(const HIRTypeData* l, const HIRTypeData* r) const;

private:
    void addIvarsTraitPath(HIRTraitPath& path);
    void expandIvarsTraitPath(HIRTraitPath& path);

    IVar& getPointedIvar(unsigned int slot) const;
};

class NextTraitGoalEvaluator;

struct TraitTypeConstraintCallback {
    virtual void constrain(const Span& sp, const HIRTypeData* receiver, const HIRTypeData* implType) = 0;
    virtual void registerSolverObligation(const Span& sp, HIRTypeRef type, HIRTraitPath trait) = 0;
};

struct TraitBoundCallback {
    virtual bool visit(HIRCompare cmp, const HIRTypeData* type, const HIRGenericPath& traitPath, const TraitResolveCommon::CachedBound& info) = 0;
};

template <typename F>
struct TraitBoundCb final: TraitBoundCallback {
    F f;

    explicit TraitBoundCb(F f)
        : f(f)
    {
    }

    bool visit(HIRCompare cmp, const HIRTypeData* type, const HIRGenericPath& traitPath, const TraitResolveCommon::CachedBound& info) override {
        return f(cmp, type, traitPath, info);
    }
};

struct TraitPathCallback {
    virtual bool visit(const HIRTraitPath& trait) = 0;
};

template <typename F>
struct TraitPathCb final: TraitPathCallback {
    F f;

    explicit TraitPathCb(F f)
        : f(f)
    {
    }

    bool visit(const HIRTraitPath& trait) override {
        return f(trait);
    }
};

struct TraitImplCallback {
    virtual bool visit(ImplRef impl, HIRCompare cmp) = 0;
};

template <typename F>
struct TraitImplCb final: TraitImplCallback {
    F f;

    explicit TraitImplCb(F f)
        : f(f)
    {
    }

    bool visit(ImplRef impl, HIRCompare cmp) override {
        // ImplRef is a move-only solver response; the callable itself is still
        // stored by an ordinary copy.
        return f(mv$(impl), cmp);
    }
};

struct UnsizeTypeCallback {
    virtual void visit(HIRTypeRef newDst) = 0;
};

template <typename F>
struct UnsizeTypeCb final: UnsizeTypeCallback {
    F f;

    explicit UnsizeTypeCb(F f)
        : f(f)
    {
    }

    void visit(HIRTypeRef newDst) override {
        f(newDst);
    }
};

struct UnsizeInferCallback {
    virtual void visit(const HIRTypeData* dst, const HIRTypeData* src) = 0;
};

class TraitResolution: public TraitResolveCommon {
    const HIRSimplePath& langDeref_;
    HMTypeInferrence& ivars;

public:
    const HIRSimplePath& visPath;

private:
    const HIRGenericPath* currentTraitPath_;
    const HIRTrait* currentTraitPtr;

    // A legacy solver invocation only needs this stack while it is actively
    // resolving nested trait bounds.  The goal is canonicalised before it is
    // stored, so fresh implementation placeholders do not hide a cycle.
    struct LegacyTraitGoal {
        HIRSimplePath trait;
        HIRPathParams params;
        HIRTypeRef type;
        bool hasParams;

        LegacyTraitGoal(const HIRSimplePath& trait, const HIRPathParams& params, bool hasParams, const HIRTypeData* type);

        bool matches(const HIRSimplePath& otherTrait, const HIRPathParams& otherParams, bool otherHasParams, const HIRTypeData* otherType) const;
    };

    struct EatCacheEntry {
        u64 generation;
        // An expansion touching inference variables is only valid until the
        // table mutates.
        u64 ivarGeneration;
        HIRTypeRef type;
    };

    mutable ::std::vector<LegacyTraitGoal> legacyTraitGoalStack;
    mutable stl::ObjPool::Ref eatCachePool;
    mutable stl::IntMap<EatCacheEntry> eatCache;
    mutable u64 eatCacheGeneration = 0;
    friend class NextTraitGoalEvaluator;
    mutable stl::Vector<HIRTypeRef> eatActiveStack;
    mutable bool normalizingBoundType = false;
    // Owned by the crate ObjPool.  TraitResolution only keeps a stable
    // pointer into the compiler-lifetime arena.
    mutable NextTraitGoalEvaluator* nextSolver = nullptr;
    // Coherence probes use an isolated inference table, so overlap checks
    // cannot bind or append variables in the caller's type-checking context.
    mutable HMTypeInferrence coherenceIvars;
    mutable TraitResolution* coherenceResolve = nullptr;
    TraitTypeConstraintCallback* typeConstraint = nullptr;
    // Bumped when the defining-opaque registrations change: they alter what
    // containsDefiningOpaque answers, so cached goals must not outlive them.
    mutable u64 solverEnvGeneration = 0;
    ::std::vector<HIRSimplePath> opaqueAliasScopes;
    ::std::vector<HIRSimplePath> definingOpaqueAliases;
    stl::Vector<const HIRPath*> definingFcnOrigins;

public:
    TraitResolution(HMTypeInferrence& ivars, const WireBoard& wb, const HIRGenericParams* implParams, const HIRGenericParams* itemParams, const HIRSimplePath& visPath, const HIRGenericPath* currentTrait);
    ~TraitResolution();

    void setGenericContext(const HIRGenericParams* implParams, const HIRGenericParams* itemParams);

    void addOpaqueAliasScope(const HIRSimplePath& path);

    void addDefiningOpaqueAlias(const HIRSimplePath& path);

    /// The current function's own return-position opaques: their goals are
    /// defining uses, everywhere else a Fcn-origin opaque is rigid.
    void addDefiningFcnOrigin(const HIRPath& origin);
    bool isDefiningFcnOrigin(const HIRPath& origin) const;

    /// While set, a method probe must answer from what is known: type checking
    /// has stabilised, so waiting for the receiver would wait forever.
    mutable bool methodProbeMustDecide = false;

    bool isOpaqueAliasDefiningScope(const HIRTypeDataErasedTypeAliasInner& alias) const;

    void setTypeConstraint(TraitTypeConstraintCallback* constraint) {
        typeConstraint = constraint;
    }

    const HIRGenericPath* currentTraitPath() const {
        return currentTraitPath_;
    }

    HIRCompare comparePp(const Span& sp, const HIRPathParams& left, const HIRPathParams& right) const;

    const HIRTypeData* resolveType(const HIRTypeData* type) const {
        return ivars.getType(type);
    }

    HIRCompare compareTy(const Span& sp, const HIRTypeData* left, const HIRTypeData* right) const {
        return left->compareWithPlaceholders(sp, right, ivars.callbackResolveInfer());
    }

    bool typeContainsIvars(const HIRTypeData* type) const {
        return ivars.typeContainsIvars(type, false);
    }

    bool paramsContainIvars(const HIRPathParams& params) const {
        return ivars.pathparamsContainIvars(params, false);
    }

    void compactIvars(HMTypeInferrence& ivars);

    bool hasAssociatedType(const HIRTypeData* ty) const;

    /// Expand any located associated types in the input, operating in-place and returning the result
    HIRTypeRef expandAssociatedTypes(const Span& sp, HIRTypeRef input) const;

    const HIRTypeData* expandAssociatedTypes(const Span& sp, const HIRTypeData* input, HIRTypeRef& tmp) const;

    void expandAssociatedTypesParams(const Span& sp, HIRPathParams& params) const;

    bool iterateBoundsTraitsCb(const Span& sp, const HIRTypeData* type, const HIRSimplePath& trait, TraitBoundCallback& cb) const;
    bool iterateBoundsTraitsCb(const Span& sp, const HIRTypeData* type, TraitBoundCallback& cb) const;
    bool iterateBoundsTraitsCb(const Span& sp, TraitBoundCallback& cb) const;
    bool iterateAtyBoundsCb(const Span& sp, const HIRPath::Data::Data_UfcsKnown& pe, TraitPathCallback& cb) const;

    template <typename F>
    bool iterateBoundsTraits(const Span& sp, const HIRTypeData* type, const HIRSimplePath& trait, F f) const {
        TraitBoundCb<F> cb(f);
        return iterateBoundsTraitsCb(sp, type, trait, cb);
    }

    template <typename F>
    bool iterateBoundsTraits(const Span& sp, const HIRTypeData* type, F f) const {
        TraitBoundCb<F> cb(f);
        return iterateBoundsTraitsCb(sp, type, cb);
    }

    template <typename F>
    bool iterateBoundsTraits(const Span& sp, F f) const {
        TraitBoundCb<F> cb(f);
        return iterateBoundsTraitsCb(sp, cb);
    }

    template <typename F>
    bool iterateAtyBounds(const Span& sp, const HIRPath::Data::Data_UfcsKnown& pe, F f) const {
        TraitPathCb<F> cb(f);
        return iterateAtyBoundsCb(sp, pe, cb);
    }

    /// Searches for a trait impl using the solver selected for this session.
    bool findTraitImplsCb(const Span& sp, const HIRSimplePath& trait, const HIRPathParams& params, const HIRTypeData* type, TraitImplCallback& callback, bool magicTraitImpls) const;

    template <typename F>
    bool findTraitImpls(const Span& sp, const HIRSimplePath& trait, const HIRPathParams& params, const HIRTypeData* type, F f, bool magicTraitImpls = true) const {
        TraitImplCb<F> cb(f);
        return findTraitImplsCb(sp, trait, params, type, cb, magicTraitImpls);
    }

    /// Candidate lookup used by the legacy selector and by next-solver
    /// candidate assembly.  Callers performing trait selection must use
    /// `find_trait_impls`, not this assembly primitive.
    bool findTraitImplsLegacyCb(const Span& sp, const HIRSimplePath& trait, const HIRPathParams& params, const HIRTypeData* type, TraitImplCallback& callback, bool magicTraitImpls = true, bool searchCrate = true, bool searchBounds = true) const;

    template <typename F>
    bool findTraitImplsLegacy(const Span& sp, const HIRSimplePath& trait, const HIRPathParams& params, const HIRTypeData* type, F f, bool magicTraitImpls = true, bool searchCrate = true, bool searchBounds = true) const {
        TraitImplCb<F> cb(f);
        return findTraitImplsLegacyCb(sp, trait, params, type, cb, magicTraitImpls, searchCrate, searchBounds);
    }

    /// Evaluate a trait goal using the next-solver candidate model.  Candidate
    /// assembly is exhaustive, impl where-clauses are evaluated recursively,
    /// and only a merged response is exposed to the caller.  `assoc_name` and
    /// `assoc_type` add an associated-type equality to the goal.
    bool findTraitImplsNextCb(const Span& sp, const HIRSimplePath& trait, const HIRPathParams& params, const HIRTypeData* type, TraitImplCallback& callback, const char* assocName = nullptr, const HIRTypeData* assocType = nullptr, const HIRPathParams* assocParams = nullptr, bool allowInferInputs = false) const;

    template <typename F>
    bool findTraitImplsNext(const Span& sp, const HIRSimplePath& trait, const HIRPathParams& params, const HIRTypeData* type, F f, const char* assocName = nullptr, const HIRTypeData* assocType = nullptr, const HIRPathParams* assocParams = nullptr, bool allowInferInputs = false) const {
        TraitImplCb<F> cb(f);
        return findTraitImplsNextCb(sp, trait, params, type, cb, assocName, assocType, assocParams, allowInferInputs);
    }

    /// Whether two concrete impl candidates may apply to one canonical goal.
    /// With next-solver coherence enabled this unifies both headers and proves
    /// both sets of where-clauses in an isolated inference context.
    bool implsOverlap(const Span& sp, const ImplRef& left, const ImplRef& right) const;

    /// Locate a named trait in the provied trait (either itself or as a parent trait)
    bool findNamedTraitInTraitCb(const Span& sp, const HIRSimplePath& des, const HIRPathParams& params, const HIRTrait& traitPtr, const HIRSimplePath& traitPath, const HIRPathParams& pp, const HIRTypeData* selfType, TraitPathCallback& callback) const;

    template <typename F>
    bool findNamedTraitInTrait(const Span& sp, const HIRSimplePath& des, const HIRPathParams& params, const HIRTrait& traitPtr, const HIRSimplePath& traitPath, const HIRPathParams& pp, const HIRTypeData* selfType, F f) const {
        TraitPathCb<F> cb(f);
        return findNamedTraitInTraitCb(sp, des, params, traitPtr, traitPath, pp, selfType, cb);
    }
    /// Search for a trait implementation in current bounds
    bool findTraitImplsBoundCb(const Span& sp, const HIRSimplePath& trait, const HIRPathParams& params, const HIRTypeData* type, TraitImplCallback& callback) const;

    template <typename F>
    bool findTraitImplsBound(const Span& sp, const HIRSimplePath& trait, const HIRPathParams& params, const HIRTypeData* type, F f) const {
        TraitImplCb<F> cb(f);
        return findTraitImplsBoundCb(sp, trait, params, type, cb);
    }

    /// Search for a trait implementation in the crate
    bool findTraitImplsCrateCb(const Span& sp, const HIRSimplePath& trait, const HIRPathParams& params, const HIRTypeData* type, TraitImplCallback& callback) const {
        return findTraitImplsCrateCb(sp, trait, &params, type, callback);
    }

    /// Search for a trait implementation in the crate (allows nullptr to ignore params)
    bool findTraitImplsCrateCb(const Span& sp, const HIRSimplePath& trait, const HIRPathParams* params, const HIRTypeData* type, TraitImplCallback& callback) const;

    template <typename F>
    bool findTraitImplsCrate(const Span& sp, const HIRSimplePath& trait, const HIRPathParams& params, const HIRTypeData* type, F f) const {
        TraitImplCb<F> cb(f);
        return findTraitImplsCrateCb(sp, trait, params, type, cb);
    }

    template <typename F>
    bool findTraitImplsCrate(const Span& sp, const HIRSimplePath& trait, const HIRPathParams* params, const HIRTypeData* type, F f) const {
        TraitImplCb<F> cb(f);
        return findTraitImplsCrateCb(sp, trait, params, type, cb);
    }
    /// Check for magic (automatically determined) trait implementations
    bool findTraitImplsMagicCb(const Span& sp, const HIRSimplePath& trait, const HIRPathParams& params, const HIRTypeData* type, TraitImplCallback& callback) const;

    template <typename F>
    bool findTraitImplsMagic(const Span& sp, const HIRSimplePath& trait, const HIRPathParams& params, const HIRTypeData* type, F f) const {
        TraitImplCb<F> cb(f);
        return findTraitImplsMagicCb(sp, trait, params, type, cb);
    }
    /// Check for trait implementations on magical types (closures, generators, fn pointers, ...)
    bool findTraitImplsTypesCb(const Span& sp, const HIRSimplePath& trait, const HIRPathParams& params, const HIRTypeData* type, TraitImplCallback& callback) const;

    template <typename F>
    bool findTraitImplsTypes(const Span& sp, const HIRSimplePath& trait, const HIRPathParams& params, const HIRTypeData* type, F f) const {
        TraitImplCb<F> cb(f);
        return findTraitImplsTypesCb(sp, trait, params, type, cb);
    }


private:
    friend class NextTraitGoalEvaluator;

    HIRPathParams makeFreshImplParams(const HIRGenericParams& params) const;

    HIRCompare checkAutoTraitImplDestructure(const Span& sp, const HIRSimplePath& trait, const HIRPathParams* paramsPtr, const HIRTypeData* type) const;
    HIRCompare fticCheckParams(
        const Span& sp,
        const HIRSimplePath& trait,
        const HIRPathParams* params,
        const HIRTypeData* type,
        const HIRGenericParams& implParamsDef,
        const HIRPathParams& implTraitArgs,
        const HIRTypeData* implTy,
        /*Out->*/ HIRPathParams& outImplParams,
        bool evaluateBounds = true,
        bool commitDefiningOpaque = false
    ) const;

public:
    enum class AutoderefBorrow {
        None,
        Shared,
        Unique,
        Owned,
        /// Not a borrow: a `*mut T` receiver taken as the `*const T` a method
        /// was written for.
        RawShared,
        /// The pin-ergonomics reborrow from `Pin<&mut T>` to `Pin<&T>`.
        PinShared,
    };
    friend ::std::ostream& operator<<(::std::ostream& os, const AutoderefBorrow& x);
    /// Locate the named method by applying auto-dereferencing.
    /// \return Number of times deref was applied (or ~0 if _ was hit)
    unsigned int autoderefFindMethod(
        const Span& sp,
        const tTraitList& traits,
        const ::std::vector<unsigned>& ivars,
        unsigned int typeIvarCount,
        const HIRTypeData* topTy,
        const RcString& methodName,
        /* Out -> */ ::std::vector<::std::pair<AutoderefBorrow, HIRPath>>& possibilities
    ) const;
    /// Locate the named field by applying auto-dereferencing.
    /// \return Number of times deref was applied (or ~0 if _ was hit)
    unsigned int autoderefFindField(const Span& sp, const HIRTypeData* topTy, const RcString& name, /* Out -> */ HIRTypeRef& fieldType) const;

    enum class AutoderefResult {
        NoMatch,
        Match,
        Ambiguous,
    };

    /// Probe one automatic-dereference step without changing inference state.
    /// `impl_type` is populated for a trait-based step so that a caller which
    /// actually selects this step can commit the impl response afterwards.
    AutoderefResult autoderefStep(const Span& sp, const HIRTypeData* ty, HIRTypeRef& target, ::std::optional<HIRTypeRef>* implType = nullptr) const;

    /// Apply an automatic dereference
    const HIRTypeData* autoderef(const Span& sp, const HIRTypeData* ty, HIRTypeRef& tmpType) const;

    bool findField(const Span& sp, const HIRTypeData* ty, const RcString& name, /* Out -> */ HIRTypeRef& fieldType) const;

    enum class MethodAccess {
        Shared,
        Unique,
        Move,
    };

private:
    ::std::optional<HIRTypeRef> checkMethodReceiver(const Span& sp, const HIRFunction& fcn, const HIRTypeData* ty, TraitResolution::MethodAccess access) const;

public:
    enum class AllowedReceivers {
        All,
        AnyBorrow,
        SharedBorrow,
        Value,
        Box,
    };
    friend ::std::ostream& operator<<(::std::ostream& os, const AllowedReceivers& x);
    bool findMethod(const Span& sp, const tTraitList& traits, const ::std::vector<unsigned>& ivars, unsigned int typeIvarCount, const HIRTypeData* ty, const RcString& methodName, MethodAccess access, AutoderefBorrow borrowType, /* Out -> */ ::std::vector<::std::pair<AutoderefBorrow, HIRPath>>& possibilities, /* Out -> */ bool* outUndecided = nullptr) const;

    /// Locates a named method in a trait, and returns the path of the trait that contains it (with fixed parameters)
    const HIRFunction* traitContainsMethod(const Span& sp, const HIRGenericPath& traitPath, const HIRTrait& traitPtr, const HIRTypeData* self, const RcString& name, HIRGenericPath& outPath) const;
    bool traitContainsType(const Span& sp, const HIRGenericPath& traitPath, const HIRTrait& traitPtr, const char* name, HIRGenericPath& outPath) const;

    HIRCompare typeIsSized(const Span& sp, const HIRTypeData* ty) const;
    HIRCompare typeIsCopy(const Span& sp, const HIRTypeData* ty) const;
    HIRCompare typeIsClone(const Span& sp, const HIRTypeData* ty) const;

    // If `new_type_callback` is populated, it will be called with the actual/possible dst_type
    // If `infer_callback` is populated, it will be called when either side is an ivar
    template <typename F>
    HIRCompare canUnsize(const Span& sp, const HIRTypeData* dstTy, const HIRTypeData* srcTy, F f) const {
        UnsizeTypeCb<F> cb(f);
        return canUnsizeCb(sp, dstTy, srcTy, &cb);
    }

    HIRCompare canUnsizeCb(const Span& sp, const HIRTypeData* dstTy, const HIRTypeData* srcTy, UnsizeTypeCallback* newTypeCallback, UnsizeInferCallback* inferCallback = nullptr) const;

    const HIRTypeData* typeIsOwnedBox(const Span& sp, const HIRTypeData* ty) const;

private:
    void expandAssociatedTypesInplace(const Span& sp, HIRTypeRef& input, LList<const HIRTypeData*> stack) const;
    bool expandAssociatedTypesInplaceUfcsInherent(const Span& sp, HIRTypeRef& input, LList<const HIRTypeData*> stack) const;
    void expandAssociatedTypesInplaceUfcsKnown(const Span& sp, HIRTypeRef& input, LList<const HIRTypeData*> stack) const;
};
