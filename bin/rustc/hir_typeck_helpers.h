#pragma once

#include "hir_hir.h"
#include "hir_expr.h" // t_trait_list
#include "hir_typeck_common.h"
#include "hir_typeck_resolve_common.h"
#include "thin_vector.h"
#include <std/lib/vector.h>
#include <std/mem/obj_pool.h>
#include <std/sym/i_map.h>

bool typeIsUnboundedInfer(const HIRTypeData* ty);

/// Owning description of the implementation selected by the solver.  The
/// only pointers are to crate-lifetime HIR declarations; all response-shaped
/// data (path, parameters and associated values) belongs to this object.
struct SolverImpl {
    HIRPathParams implParams;
    const HIRTrait* trait = nullptr;
    HIRSimplePath traitPath;
    const HIRTraitImpl* traitImpl = nullptr;

    HIRTypeRef type;
    HIRPathParams traitArgs;
    HIRTraitPath::assocListT associated;
    HIRBoundConstness constness = HIRBoundConstness::Never;
    bool ambiguousIdentity = false;

    static SolverImpl fromLegacy(ImplRef impl);
    ImplRef legacy() const;
};

struct SolverSlotValues {
    ThinVector<HIRTypeRef> typeInputs;
    ThinVector<HIRTypeRef> types;
    ThinVector<HIRConstGeneric> valueInputs;
    ThinVector<HIRConstGeneric> values;
};

struct SolverObligation {
    HIRTypeRef type;
    HIRTraitPath trait;
};

struct SolverTypeEquality {
    HIRTypeRef left;
    HIRTypeRef right;
};

struct SolverValueEquality {
    HIRConstGeneric left;
    HIRConstGeneric right;
};

/// Aggregate facts about the complete viable set for an operator-trait goal.
/// Type checking may use these facts to decide whether language primitive
/// equations are valid, but never observes or chooses individual impl heads.
struct SolverOperatorSummary {
    bool hasSemanticImpl = false;
    bool sawCurrentImpl = false;
    bool currentImplHasBuiltinSignature = false;
};

/// A self-contained solver answer.  Slot values are positional with respect
/// to the canonical input goal and can therefore be replayed into any caller
/// with the same key.  Inference effects remain exclusively in slots,
/// equalities and obligations.  A selected `impl` is already the materialised
/// answer; ambiguity never exposes individual candidate heads.
struct SolverResponse {
    SolverCertainty certainty = SolverCertainty::NoSolution;
    SolverSlotValues slots;
    ThinVector<SolverObligation> obligations;
    ThinVector<SolverTypeEquality> equalities;
    ThinVector<SolverValueEquality> valueEqualities;
    const SolverImpl* impl = nullptr;
    bool hasImpl = false;
    SolverOperatorSummary operatorSummary;
};

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
        SolverCertainty certainty = SolverCertainty::NoSolution;
        bool hasResponse = false;
        const SolverResponse* response = nullptr;
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

    Entry* insert(size_t hash, const HIRSimplePath& trait, HIRPathParams params, const HIRTypeData* type, SolverCertainty certainty) {
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

    // Transactional probes.  While at least one snapshot is active every
    // table mutation is journaled; rollbackTo restores the exact
    // pre-snapshot state.  mutationGeneration values are allocated by a
    // monotonic counter that survives rollback, so a generation observed
    // inside a rolled-back probe can never be mistaken for a live state.
    struct Snapshot {
        size_t journalLength;
        size_t ivarCount;
        size_t valueCount;
        u64 generation;
        bool hasChanged;
    };

    Snapshot snapshot();
    void commit(const Snapshot& snapshot);
    void rollbackTo(const Snapshot& snapshot);

    bool probing() const {
        return snapshotDepth != 0;
    }


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
    struct JournalEntry {
        enum class Kind : u8 {
            /// ivars[slot].type was overwritten; alias untouched.
            TypeSet,
            /// ivars[slot] became an alias; alias was ~0 before.
            TypeAlias,
            /// values[slot].val was overwritten; it was Infer{slot} before.
            ValSet,
            /// values[slot] became an alias; its val is kept alive while a
            /// snapshot is active, so undo only clears the alias.
            ValAlias,
            /// aliasTypeIvars gained the key `slot`.
            AliasTypeMap,
            /// aliasValueIvars gained the key `slot`.
            AliasValueMap,
        };

        Kind kind;
        unsigned slot;
        HIRTypeRef oldType;
    };

    stl::Vector<JournalEntry> journal;
    unsigned snapshotDepth = 0;
    u64 generationCounter = 0;

    void journalMutation(JournalEntry::Kind kind, unsigned slot, HIRTypeRef oldType);

    void addIvarsTraitPath(HIRTraitPath& path);
    void expandIvarsTraitPath(HIRTraitPath& path);

    unsigned int rootIvarIndex(unsigned int slot) const;
    IVar& getPointedIvar(unsigned int slot) const;

    /// Occurs check: whether `type`, fully resolved through the table,
    /// reaches the live variable rooted at `rootIndex`.
    bool containsLiveIvar(const HIRTypeData* type, unsigned int rootIndex) const;

    friend class Unifier;
};

/// One structural-unification session over an inference table.  Bindings go
/// through the table's journal (occurs check and literal-class rules
/// included), so a caller controls wider transactionality with table
/// snapshots.  Every equality the walk can neither prove nor refute
/// structurally -- an unresolved projection, opaque, placeholder or
/// canonical variable on either side -- is collected on the session as
/// data, never dropped: the caller turns it into goals or reports
/// ambiguity carrying it.
class TraitResolution;

class Unifier {
public:
    struct Options {
        bool bindRigidValues = false;
        bool relateProjectionInputs = false;
    };

    enum class Outcome : u8 {
        /// Equal under the recorded bindings, with no deferred relation.
        Proven,
        /// Structurally compatible, but at least one type/value relation is
        /// deferred.  The pending lists are the proof obligations.
        Ambiguous,
        /// The types can never be equal; bindings made by the failed call
        /// were rolled back and the pending lists are unchanged.
        Mismatch,
    };

    struct PendingEquality {
        HIRTypeRef left;
        HIRTypeRef right;
    };

    struct PendingValueEquality {
        HIRConstGeneric left;
        HIRConstGeneric right;
    };

    Unifier(const Span& sp, HMTypeInferrence& table, const TraitResolution* resolve = nullptr);
    Unifier(const Span& sp, HMTypeInferrence& table, const TraitResolution* resolve, Options options);

    Outcome unify(const HIRTypeData* left, const HIRTypeData* right);
    Outcome unifyValues(const HIRConstGeneric& left, const HIRConstGeneric& right);

    const stl::Vector<PendingEquality>& pending() const {
        return pending_;
    }

    const ThinVector<PendingValueEquality>& pendingValues() const {
        return pendingValues_;
    }

private:
    Outcome unifyResolved(const HIRTypeData* left, const HIRTypeData* right);
    Outcome unifyParams(const HIRPathParams& left, const HIRPathParams& right);
    Outcome unifyValuesResolved(const HIRConstGeneric& left, const HIRConstGeneric& right);
    bool valueContainsLiveIvar(const HIRConstGeneric& value, unsigned rootIndex) const;
    bool paramsContainLiveValueIvar(const HIRPathParams& params, unsigned rootIndex) const;
    bool pathContainsLiveValueIvar(const HIRPath& path, unsigned rootIndex) const;
    bool traitPathContainsLiveValueIvar(const HIRTraitPath& path, unsigned rootIndex) const;
    bool typeContainsLiveValueIvar(const HIRTypeData* type, unsigned rootIndex) const;
    bool opaqueCanReveal(const HIRTypeData* type) const;
    Outcome defer(const HIRTypeData* left, const HIRTypeData* right);

    // Reserved for the diagnostics the goal-emission callers will need.
    [[maybe_unused]] const Span& sp_;
    HMTypeInferrence& table_;
    const TraitResolution* resolve_;
    // Candidate impl parameters are existential variables.  While such a
    // candidate is instantiated for a probe, a const ivar may legitimately
    // capture the goal's rigid placeholder/canonical value.  Ordinary
    // equality probes keep those relations pending instead.
    bool bindRigidValues_;
    // In an impl head an associated projection is a declared rigid
    // constructor. Matching two occurrences of that constructor relates its
    // Self and generic inputs instead of treating the whole alias as opaque.
    bool relateProjectionInputs_;
    stl::Vector<PendingEquality> pending_;
    ThinVector<PendingValueEquality> pendingValues_;
};

class NextTraitGoalEvaluator;

enum class SolverCoercionOp : u8 {
    Coercion,
    Unsizing,
};

struct SolverCoercionConstraint {
    enum class Direction : u8 {
        /// `other` is coerced to the selected trait input.
        InputIsDestination,
        /// The selected trait input is coerced to `other`.
        InputIsSource,
    };

    unsigned typeIndex;
    HIRTypeRef other;
    Direction direction;
    SolverCoercionOp op;
    /// Select `Self` instead of `traitParams.types[typeIndex]`.
    bool isSelf = false;
};

/// Extra relation requested by the operator type-checking rule.  Candidate
/// classification is performed inside the solver over its final viable set;
/// only the aggregate summary is returned.
struct SolverOperatorGoal {
    TypeckPrimitiveOperator operation = TypeckPrimitiveOperator::None;
    const char* outputName = nullptr;
    const HIRPathParams* outputParams = nullptr;
    const HIRTraitImpl* currentImpl = nullptr;
};

/// Options for a next-solver goal query.
struct TraitGoalQuery {
    /// With `assocName`/`assocType`/`assocParams`, an associated-type
    /// equality is added to the goal; an empty (non-null) name requests the
    /// canonical trait response itself.
    const char* assocName = nullptr;
    const HIRTypeData* assocType = nullptr;
    const HIRPathParams* assocParams = nullptr;
    /// Ask specialization for the impl that provides this value item
    /// (method, associated constant, or associated static).  This is not an
    /// associated-type constraint: it changes response identity to the
    /// nearest provider in the selected specialization chain.
    const char* valueName = nullptr;
    /// Evaluate even when the inputs still hold unassigned inference
    /// variables.
    bool allowInferInputs = false;
    /// Omit this concrete impl from root candidate selection.  This is used
    /// while checking the language-defined primitive semantics inside that
    /// same operator impl; it is part of the goal, not a caller-side filter.
    const HIRTraitImpl* excludedImpl = nullptr;
    /// Additional call-site coercion goals over trait type inputs.  These are
    /// first-class solver relations: candidate filtering, ambiguity and
    /// preference are all decided inside the evaluator.
    const ThinVector<SolverCoercionConstraint>* coercions = nullptr;
    const SolverOperatorGoal* operatorGoal = nullptr;
};

/// A projection-equality goal.  `output` is represented by a fresh canonical
/// solver slot; the selected value and every inference effect travel back in
/// the typed response instead of being recovered by a second impl lookup.
struct NormalizesTo {
    HIRTypeRef projection;
};

struct NormalizesToResponse {
    SolverResponse effects;
    HIRTypeRef output;
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

struct AssembledImplCallback {
    virtual bool visit(ImplRef impl, SolverCertainty certainty = SolverCertainty::Proven) = 0;
};

struct SolverResponseCallback {
    virtual bool visit(SolverResponse response) = 0;
};

struct NormalizesToCallback {
    virtual bool visit(NormalizesToResponse response) = 0;
};

template <typename F>
struct SolverResponseCb final: SolverResponseCallback {
    F f;

    explicit SolverResponseCb(F f)
        : f(f)
    {
    }

    bool visit(SolverResponse response) override {
        return f(::std::move(response));
    }
};

template <typename F>
struct NormalizesToCb final: NormalizesToCallback {
    F f;

    explicit NormalizesToCb(F f)
        : f(f)
    {
    }

    bool visit(NormalizesToResponse response) override {
        return f(::std::move(response));
    }
};

template <typename F>
struct AssembledImplCb final: AssembledImplCallback {
    F f;

    explicit AssembledImplCb(F f)
        : f(f)
    {
    }

    bool visit(ImplRef impl, SolverCertainty certainty) override {
        // ImplRef is a move-only solver response; the callable itself is still
        // stored by an ordinary copy.
        return f(mv$(impl), certainty);
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

    struct EatCacheEntry {
        u64 generation;
        // An expansion touching inference variables is only valid until the
        // table mutates.
        u64 ivarGeneration;
        HIRTypeRef type;
    };

    mutable stl::ObjPool::Ref eatCachePool;
    mutable stl::IntMap<EatCacheEntry> eatCache;
    struct SolverExistentials {
        const HIRGenericParams* definition;
        HIRPathParams params;
    };
    mutable stl::IntMap<ThinVector<SolverExistentials>> solverExistentials_;
    mutable u64 eatCacheGeneration = 0;
    friend class NextTraitGoalEvaluator;
    mutable bool normalizingBoundType = false;
    // Owned by the crate ObjPool.  TraitResolution only keeps a stable
    // pointer into the compiler-lifetime arena.
    mutable NextTraitGoalEvaluator* nextSolver = nullptr;
    // Coherence probes run on the caller's own inference table under a
    // snapshot that is rolled back afterwards; a dedicated evaluator keeps
    // the probe's goal bookkeeping out of any active evaluation session.
    mutable NextTraitGoalEvaluator* coherenceEvaluator = nullptr;
    // Builtin predicates such as structural Sized/Copy/Clone must ask whether
    // a declared impl or ParamEnv predicate exists without recursively adding
    // the builtin candidate currently being assembled.  A separate evaluator
    // keeps that root-candidate scope out of the ordinary response cache.
    mutable NextTraitGoalEvaluator* nonBuiltinSolver = nullptr;
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

    bool isOpaqueAliasDefiningScope(const HIRTypeDataErasedTypeAliasInner& alias) const;

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

    void compactIvars(HMTypeInferrence& ivars, SolverResponseCallback* effects = nullptr);

    bool hasAssociatedType(const HIRTypeData* ty) const;

    /// Expand any located associated types in the input, operating in-place and returning the result
    HIRTypeRef expandAssociatedTypes(const Span& sp, HIRTypeRef input, SolverResponseCallback* effects = nullptr) const;

    const HIRTypeData* expandAssociatedTypes(const Span& sp, const HIRTypeData* input, HIRTypeRef& tmp, SolverResponseCallback* effects = nullptr) const;

    void expandAssociatedTypesParams(const Span& sp, HIRPathParams& params, SolverResponseCallback* effects = nullptr) const;

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

    /// Return the complete next-solver answer.  Slot constraints and nested
    /// obligations are data in the response; this is the API new consumers
    /// use instead of observing inference effects through callbacks.
    bool solveTraitGoalCb(const Span& sp, const HIRSimplePath& trait, const HIRPathParams& params, const HIRTypeData* type, SolverResponseCallback& callback, const TraitGoalQuery& query = {}) const;

    template <typename F>
    bool solveTraitGoal(const Span& sp, const HIRSimplePath& trait, const HIRPathParams& params, const HIRTypeData* type, F f, const TraitGoalQuery& query = {}) const {
        SolverResponseCb<F> cb(f);
        return solveTraitGoalCb(sp, trait, params, type, cb, query);
    }

    bool solveNormalizesToCb(const Span& sp, const NormalizesTo& goal, NormalizesToCallback& callback) const;

    template <typename F>
    bool solveNormalizesTo(const Span& sp, const NormalizesTo& goal, F f) const {
        NormalizesToCb<F> cb(f);
        return solveNormalizesToCb(sp, goal, cb);
    }

    /// Whether two concrete impl candidates may apply to one canonical goal.
    /// With next-solver coherence enabled this unifies both headers and proves
    /// both sets of where-clauses in an isolated inference context.
    bool implsOverlap(const Span& sp, const ImplRef& left, const ImplRef& right) const;

    /// One stable typed existential binder for an immutable generic
    /// definition.  The returned parameters contain no inference-table state.
    const HIRPathParams& solverExistentials(const Span& sp, const HIRGenericParams& definition) const;

    /// Instantiate an inherent impl's existential parameters as real inference
    /// variables and relate its declared Self type to the receiver.  Callers
    /// probing more than one impl must wrap this in an inference snapshot.
    Unifier::Outcome relateInherentImplHeader(
        const Span& sp,
        const HIRTypeImpl& impl,
        const HIRTypeData* receiver,
        HIRPathParams& implParams
    ) const;
    SolverCertainty evaluateInherentImpl(
        const Span& sp,
        const HIRTypeImpl& impl,
        const HIRTypeData* receiver,
        HIRPathParams& implParams
    ) const;
    /// Probe an inherent impl without leaking this resolver's inference
    /// variables.  Any still-unconstrained impl parameters are returned as
    /// typed solver existentials with stable binder identity.
    SolverCertainty probeInherentImpl(
        const Span& sp,
        const HIRTypeImpl& impl,
        const HIRTypeData* receiver,
        HIRPathParams& implParams
    ) const;

    /// Locate a named trait in the provied trait (either itself or as a parent trait)
    bool findNamedTraitInTraitCb(const Span& sp, const HIRSimplePath& des, const HIRPathParams& params, const HIRTrait& traitPtr, const HIRSimplePath& traitPath, const HIRPathParams& pp, const HIRTypeData* selfType, TraitPathCallback& callback) const;

    template <typename F>
    bool findNamedTraitInTrait(const Span& sp, const HIRSimplePath& des, const HIRPathParams& params, const HIRTrait& traitPtr, const HIRSimplePath& traitPath, const HIRPathParams& pp, const HIRTypeData* selfType, F f) const {
        TraitPathCb<F> cb(f);
        return findNamedTraitInTraitCb(sp, des, params, traitPtr, traitPath, pp, selfType, cb);
    }
private:
    friend class NextTraitGoalEvaluator;

    bool assembleMagicCandidatesCb(const Span& sp, const HIRSimplePath& trait, const HIRPathParams& params, const HIRTypeData* type, AssembledImplCallback& callback) const;
    bool assembleTypeCandidatesCb(const Span& sp, const HIRSimplePath& trait, const HIRPathParams& params, const HIRTypeData* type, AssembledImplCallback& callback) const;
    bool assembleOtherCandidatesCb(const Span& sp, const HIRSimplePath& trait, const HIRPathParams& params, const HIRTypeData* type, AssembledImplCallback& callback) const;
    bool assembleParamEnvCandidatesCb(const Span& sp, const HIRSimplePath& trait, const HIRPathParams& params, const HIRTypeData* type, AssembledImplCallback& callback) const;

    template <typename F>
    bool assembleMagicCandidates(const Span& sp, const HIRSimplePath& trait, const HIRPathParams& params, const HIRTypeData* type, F f) const {
        AssembledImplCb<F> cb(f);
        return assembleMagicCandidatesCb(sp, trait, params, type, cb);
    }

    template <typename F>
    bool assembleOtherCandidates(const Span& sp, const HIRSimplePath& trait, const HIRPathParams& params, const HIRTypeData* type, F f) const {
        AssembledImplCb<F> cb(f);
        return assembleOtherCandidatesCb(sp, trait, params, type, cb);
    }

    template <typename F>
    bool assembleParamEnvCandidates(const Span& sp, const HIRSimplePath& trait, const HIRPathParams& params, const HIRTypeData* type, F f) const {
        AssembledImplCb<F> cb(f);
        return assembleParamEnvCandidatesCb(sp, trait, params, type, cb);
    }

    HIRPathParams makeFreshImplParams(const HIRGenericParams& params) const;
    HIRPathParams materializeImplParams(const Span& sp, const HIRGenericParams& definition, const HIRPathParams& inferenceParams) const;
    SolverCertainty solveNonBuiltinTraitGoal(const Span& sp, const HIRSimplePath& trait, const HIRTypeData* type) const;
    HIRCompare typeIsSizedBuiltin(const Span& sp, const HIRTypeData* type) const;
    HIRCompare typeIsCopyBuiltin(const Span& sp, const HIRTypeData* type) const;
    SolverCertainty evaluateCoercionGoal(const Span& sp, const SolverCoercionConstraint& constraint, const HIRTypeData* input, ThinVector<SolverTypeEquality>* equalities = nullptr) const;
    Ordering compareCoercionEndpoints(const Span& sp, const SolverCoercionConstraint& constraint, const HIRTypeData* left, const HIRTypeData* right) const;
    SolverCertainty evaluateInherentImplBounds(const Span& sp, const HIRTypeImpl& impl, const HIRPathParams& implParams) const;

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
        const HIRTypeData* expectedResult,
        bool mustDecide,
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
    bool findMethod(const Span& sp, const tTraitList& traits, const ::std::vector<unsigned>& ivars, unsigned int typeIvarCount, const HIRTypeData* ty, const RcString& methodName, const HIRTypeData* expectedResult, MethodAccess access, AutoderefBorrow borrowType, /* Out -> */ ::std::vector<::std::pair<AutoderefBorrow, HIRPath>>& possibilities, /* Out -> */ bool* outUndecided = nullptr) const;

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
    void expandAssociatedTypesInplace(const Span& sp, HIRTypeRef& input, SolverResponseCallback* effects = nullptr) const;
    bool expandAssociatedTypesInplaceUfcsInherent(const Span& sp, HIRTypeRef& input, SolverResponseCallback* effects = nullptr) const;
    void expandAssociatedTypesInplaceUfcsKnown(const Span& sp, HIRTypeRef& input, SolverResponseCallback* effects = nullptr) const;
};
