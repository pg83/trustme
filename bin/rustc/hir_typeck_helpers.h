#pragma once

#include "output.h"
#include "hir_hir.h"
#include "hir_expr.h"
#include "thin_vector.h"
#include "hir_typeck_common.h"
#include "hir_typeck_resolve_common.h"

#include <std/sym/i_map.h>
#include <std/lib/vector.h>
#include <std/mem/obj_pool.h>

bool typeIsUnboundedInfer(const HIRType* ty);

struct SolverImpl {
    HIRPathParams implParams;
    const HIRTrait* trait = nullptr;
    HIRSimplePath traitPath;
    const HIRTraitImpl* traitImpl = nullptr;

    const HIRType* type = nullptr;
    HIRPathParams traitArgs;
    HIRTraitPath::assocListT associated;
    HIRBoundConstness constness = HIRBoundConstness::Never;

    mutable const HIRType* cachedImplType = nullptr;
    mutable HIRPathParams cachedTraitParams;
    mutable bool hasCachedTraitParams = false;

    SolverImpl(HIRPathParams implParams, const HIRTrait& trait, const HIRSimplePath& traitPath, const HIRTraitImpl& traitImpl);
    SolverImpl(const HIRType* type, const HIRPathParams* traitArgs, const HIRTraitPath::assocListT* associated, HIRBoundConstness constness = HIRBoundConstness::Never);
    SolverImpl(const HIRType* type, HIRPathParams traitArgs, HIRTraitPath::assocListT associated, HIRBoundConstness constness = HIRBoundConstness::Never);

    bool isTraitImpl() const {
        return traitImpl != nullptr;
    }

    const HIRType* getImplType(HIRTypeInterner& types) const;
    const HIRPathParams& getTraitParamsRef(HIRTypeInterner& types) const;
    HIRPathParams getTraitParams(HIRTypeInterner& types) const;
    const HIRType* getTraitTyParam(HIRTypeInterner& types, unsigned index) const;
    const HIRType* getType(HIRTypeInterner& types, const char* name, const HIRPathParams& params) const;
    bool typeIsSpecialisable(const char* name) const;
    bool moreSpecificThan(HIRTypeInterner& types, const SolverImpl& other) const;
    const HIRType* monomorphImplType(HIRTypeInterner& types, const Span& sp, const HIRType* type, const HIRPathParams& methodParams = {}) const;
    HIRTraitPath monomorphImplTraitPath(HIRTypeInterner& types, const Span& sp, const HIRTraitPath& traitPath, const HIRPathParams& methodParams = {}) const;
};

struct SolverSlotValues {
    ThinVector<const HIRType*> typeInputs;
    ThinVector<const HIRType*> types;
    ThinVector<HIRConstGeneric> valueInputs;
    ThinVector<HIRConstGeneric> values;
};

struct SolverObligation {
    const HIRType* type;
    HIRTraitPath trait;
};

struct SolverTypeEquality {
    const HIRType* left;
    const HIRType* right;
};

struct SolverValueEquality {
    HIRConstGeneric left;
    HIRConstGeneric right;
};

struct AssembledImplEffects {
    ThinVector<SolverTypeEquality> equalities;
    ThinVector<SolverValueEquality> valueEqualities;
};

struct SolverOperatorSummary {
    bool hasSemanticImpl = false;
    bool sawCurrentImpl = false;
    bool currentImplHasBuiltinSignature = false;
};

struct SolverResponse {
    SolverCertainty certainty = SolverCertainty::NoSolution;
    SolverSlotValues slots;
    ThinVector<SolverObligation> obligations;
    ThinVector<SolverTypeEquality> equalities;
    ThinVector<SolverValueEquality> valueEqualities;
    const SolverImpl* impl = nullptr;
    SolverOperatorSummary operatorSummary;
};

enum class SolverCoercionRelation : u8 {
    None,
    Equality,
    Subtype,
    Coercion,
};

enum class SolverCoercionAdjustmentKind : u8 {
    None,
    Never,
    Retag,
    Unsize,
    SourceAutoderef,
    RawPointer,
    BorrowToPointer,
    Borrow,
    FunctionPointer,
};

struct SolverCoercionAdjustment {
    SolverCoercionAdjustmentKind kind = SolverCoercionAdjustmentKind::None;
    const HIRType* intermediateType = nullptr;
    SolverCoercionRelation innerRelation = SolverCoercionRelation::None;
    ThinVector<const HIRType*> sourceAutoderef;
};

enum class SolverCoercionOp : u8;

struct SolverDeferredCoercion {
    const HIRType* destination;
    const HIRType* source;
    SolverCoercionOp op;
    unsigned alternativeGroup = 0;
};

struct SolverCoercionResponse {
    SolverResponse effects;
    ThinVector<SolverDeferredCoercion> deferred;
    SolverCoercionRelation relation = SolverCoercionRelation::None;
    SolverCoercionAdjustment adjustment;
    bool reachedAutoderefLimit = false;
};

struct NextSolverCrateCache {
    struct Entry {
        Entry* next = nullptr;
        size_t hash = 0;
        HIRSimplePath trait;
        HIRPathParams params;
        const HIRType* type = nullptr;
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

    Entry* find(size_t hash, const HIRSimplePath& trait, const HIRPathParams& params, const HIRType* type) const {
        auto* head = index.find(hash);
        for (Entry* entry = head ? *head : nullptr; entry; entry = entry->next) {
            if (entry->type == type && entry->trait == trait && entry->params == params) {
                return entry;
            }
        }
        return nullptr;
    }

    Entry* insert(size_t hash, const HIRSimplePath& trait, HIRPathParams params, const HIRType* type, SolverCertainty certainty) {
        auto* entry = pool.mutPtr()->make<Entry>();
        entry->hash = hash;
        entry->trait = trait;
        entry->params = std::move(params);
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
        const HIRType* ty;

        FmtType(const HMTypeInferrence& ctxt, const HIRType* ty);
    };

    struct FmtPP {
        const HMTypeInferrence& ctxt;
        const HIRPathParams& pps;

        FmtPP(const HMTypeInferrence& ctxt, const HIRPathParams& pps);
    };

public:
    struct IVar {
        unsigned int alias;
        const HIRType* type;

        explicit IVar(const HIRType* type);

        bool isAlias() const {
            return alias != ~0u;
        }
    };

    std::vector<IVar> ivars;

    struct IVarValue {
        unsigned int alias;
        std::unique_ptr<HIRConstGeneric> val;

        IVarValue();

        bool isAlias() const {
            return alias != ~0u;
        }
    };

    std::vector<IVarValue> values;

    HIRTypeInterner& types;
    bool hasChanged;

    u64 mutationGeneration = 0;
    stl::Vector<const HIRType*> expandStack;
    stl::ObjPool::Ref aliasIvarPool;
    stl::IntMap<const HIRType*> aliasTypeIvars;
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

    void printType(stl::ZeroCopyOutput& os, const HIRType* tr, LList<const HIRType*> stack = {}) const;
    void printGenericpath(stl::ZeroCopyOutput& os, const HIRGenericPath& pps, LList<const HIRType*> stack) const;
    void printPathparams(stl::ZeroCopyOutput& os, const HIRPathParams& pps, LList<const HIRType*> stack = {}) const;

    FmtType fmtType(const HIRType* tr) const {
        return FmtType(*this, tr);
    }

    FmtPP fmt(const HIRPathParams& v) const {
        return FmtPP(*this, v);
    }

    const HIRType* addIvars(const HIRType* type);
    void addIvars(HIRConstGeneric& val);

    void addIvarsParams(HIRPathParams& params);

    struct ResolvePlaceholders: public HIRResolvePlaceholders {
        const HMTypeInferrence& parent;

        ResolvePlaceholders(const HMTypeInferrence& parent);

        const HIRType* getType(const Span& sp, const HIRType* ty) const override;

        const HIRConstGeneric& getVal(const Span& sp, const HIRConstGeneric& v) const override;
    };

    ResolvePlaceholders callbackResolveInfer() const {
        return ResolvePlaceholders(*this);
    }

    unsigned int newIvar(HIRInferClass ic = HIRInferClass::None);
    const HIRType* newIvarTr(HIRInferClass ic = HIRInferClass::None);
    void setIvarTo(unsigned int slot, const HIRType* type, bool solverProven = false);
    void ivarUnify(unsigned int leftSlot, unsigned int rightSlot);

    unsigned int newIvarVal();
    void setIvarValTo(unsigned int slot, HIRConstGeneric val);
    void ivarValUnify(unsigned int leftSlot, unsigned int rightSlot);

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

    const HIRType* getType(const HIRType* type) const;
    const HIRType* getType(unsigned idx) const;

    const HIRConstGeneric& getValue(const HIRConstGeneric& val) const;
    const HIRConstGeneric& getValue(unsigned idx) const;

    void checkForLoops();
    const HIRType* expandIvars(const HIRType* type);
    void expandIvars(HIRConstGeneric& value);
    void expandIvarsParams(HIRPathParams& params);

    bool pathparamsContainIvars(const HIRPathParams& pps, bool onlyUnbound) const;
    bool typeContainsIvars(const HIRType* ty, bool onlyUnbound = false) const;
    bool pathparamsEqual(const HIRPathParams& ppsL, const HIRPathParams& ppsR) const;
    bool typesEqual(const HIRType* l, const HIRType* r) const;

private:
    struct JournalEntry {
        enum class Kind : u8 {
            TypeSet,

            TypeAlias,

            ValSet,

            ValAlias,

            AliasTypeMap,

            AliasValueMap,
        };

        Kind kind;
        unsigned slot;
        const HIRType* oldType;
    };

    stl::Vector<JournalEntry> journal;
    unsigned snapshotDepth = 0;
    u64 generationCounter = 0;

    void journalMutation(JournalEntry::Kind kind, unsigned slot, const HIRType* oldType);

    void addIvarsTraitPath(HIRTraitPath& path);
    void expandIvarsTraitPath(HIRTraitPath& path);

    unsigned int rootIvarIndex(unsigned int slot) const;
    IVar& getPointedIvar(unsigned int slot) const;

    bool containsLiveIvar(const HIRType* type, unsigned int rootIndex) const;

    friend class Unifier;
};

class TraitResolution;

class Unifier {
public:
    struct Options {
        bool bindRigidValues = false;
        bool relateProjectionInputs = false;
        bool rigidGenericsAreDistinct = false;
        bool rigidProjectionsAreDistinct = false;
    };

    enum class Outcome : u8 {
        Proven,

        Ambiguous,

        Mismatch,
    };

    struct PendingEquality {
        const HIRType* left;
        const HIRType* right;
    };

    struct PendingValueEquality {
        HIRConstGeneric left;
        HIRConstGeneric right;
    };

    Unifier(const Span& sp, HMTypeInferrence& table, const TraitResolution* resolve = nullptr);
    Unifier(const Span& sp, HMTypeInferrence& table, const TraitResolution* resolve, Options options);

    Outcome unify(const HIRType* left, const HIRType* right);
    Outcome unifyValues(const HIRConstGeneric& left, const HIRConstGeneric& right);

    const stl::Vector<PendingEquality>& pending() const {
        return pending_;
    }

    const ThinVector<PendingValueEquality>& pendingValues() const {
        return pendingValues_;
    }

    const stl::Vector<PendingEquality>& bindings() const {
        return bindings_;
    }

    const ThinVector<PendingValueEquality>& valueBindings() const {
        return valueBindings_;
    }

private:
    Outcome unifyResolved(const HIRType* left, const HIRType* right);
    Outcome unifyParams(const HIRPathParams& left, const HIRPathParams& right);
    Outcome unifyValuesResolved(const HIRConstGeneric& left, const HIRConstGeneric& right);
    bool valueContainsLiveIvar(const HIRConstGeneric& value, unsigned rootIndex) const;
    bool paramsContainLiveValueIvar(const HIRPathParams& params, unsigned rootIndex) const;
    bool pathContainsLiveValueIvar(const HIRPath& path, unsigned rootIndex) const;
    bool traitPathContainsLiveValueIvar(const HIRTraitPath& path, unsigned rootIndex) const;
    bool typeContainsLiveValueIvar(const HIRType* type, unsigned rootIndex) const;
    bool opaqueCanReveal(const HIRType* type) const;
    Outcome defer(const HIRType* left, const HIRType* right);

    [[maybe_unused]] const Span& sp_;
    HMTypeInferrence& table_;
    const TraitResolution* resolve_;

    bool bindRigidValues_;
    bool relateProjectionInputs_;
    bool rigidGenericsAreDistinct_;
    bool rigidProjectionsAreDistinct_;
    stl::Vector<PendingEquality> pending_;
    ThinVector<PendingValueEquality> pendingValues_;
    stl::Vector<PendingEquality> bindings_;
    ThinVector<PendingValueEquality> valueBindings_;
};

enum class SolverCoercionOp : u8 {
    Coercion,
    Unsizing,
};

struct SolverCoercionConstraint {
    enum class Direction : u8 {
        InputIsDestination,

        InputIsSource,
    };

    unsigned typeIndex;
    const HIRType* other;
    Direction direction;
    SolverCoercionOp op;

    bool isSelf = false;
    bool inputRequiresSized = false;
    bool allowSourceAutoderef = false;
    bool bindInputToCandidate = true;
    unsigned alternativeGroup = 0;
};

struct SolverOperatorGoal {
    TypeckPrimitiveOperator operation = TypeckPrimitiveOperator::None;
    const char* outputName = nullptr;
    const HIRPathParams* outputParams = nullptr;
    const HIRTraitImpl* currentImpl = nullptr;
};

enum class SolverAmbiguityPolicy : u8 {
    Suppress,
    Report,
};

struct TraitGoalQuery {
    const char* assocName = nullptr;
    const HIRType* assocType = nullptr;
    const HIRPathParams* assocParams = nullptr;
    const HIRTraitPath::assocListT* associated = nullptr;

    const char* valueName = nullptr;

    bool allowInferInputs = false;

    const HIRTraitImpl* excludedImpl = nullptr;

    const ThinVector<SolverCoercionConstraint>* coercions = nullptr;
    const SolverOperatorGoal* operatorGoal = nullptr;
    SolverAmbiguityPolicy ambiguity = SolverAmbiguityPolicy::Suppress;
};

enum class InherentItemKind : u8 {
    Type,
    Method,
    Value,
};

struct InherentImplSelection {
    SolverCertainty certainty = SolverCertainty::NoSolution;
    const HIRTypeImpl* impl = nullptr;
    HIRPathParams implParams;
};

struct NormalizesTo {
    const HIRType* projection;
};

struct NormalizesToResponse {
    SolverResponse effects;
    const HIRType* output;
};

struct TraitBoundCallback {
    virtual bool visit(const HIRType* type, const HIRGenericPath& traitPath, const TraitResolveCommon::CachedBound& info) = 0;
};

template <typename F>
struct TraitBoundCb final: TraitBoundCallback {
    F f;

    explicit TraitBoundCb(F f)
        : f(f)
    {
    }

    bool visit(const HIRType* type, const HIRGenericPath& traitPath, const TraitResolveCommon::CachedBound& info) override {
        return f(type, traitPath, info);
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
    virtual bool visit(SolverImpl impl, SolverCertainty certainty = SolverCertainty::Proven, AssembledImplEffects* effects = nullptr) = 0;
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
        return f(std::move(response));
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
        return f(std::move(response));
    }
};

template <typename F>
struct AssembledImplCb final: AssembledImplCallback {
    F f;

    explicit AssembledImplCb(F f)
        : f(f)
    {
    }

    bool visit(SolverImpl impl, SolverCertainty certainty, AssembledImplEffects* effects) override {
        return f(mv$(impl), certainty, effects);
    }
};

class TraitResolution: public TraitResolveCommon {
public:
    struct NextTraitGoalEvaluator;

private:
    const HIRSimplePath& langDeref_;
    HMTypeInferrence& ivars;

public:
    const HIRSimplePath& visPath;

private:
    const HIRGenericPath* currentTraitPath_;
    const HIRTrait* currentTraitPtr;

    struct EatCacheEntry {
        u64 generation;

        u64 ivarGeneration;
        const HIRType* type;
    };

    mutable stl::ObjPool::Ref eatCachePool;
    mutable stl::IntMap<EatCacheEntry> eatCache;

    struct SolverExistentials {
        const HIRGenericParams* definition;
        HIRPathParams params;
    };

    mutable stl::IntMap<ThinVector<SolverExistentials>> solverExistentials_;
    mutable u64 eatCacheGeneration = 0;
    mutable bool normalizingBoundType = false;

    mutable NextTraitGoalEvaluator* nextSolver = nullptr;

    mutable NextTraitGoalEvaluator* coherenceEvaluator = nullptr;

    mutable NextTraitGoalEvaluator* traitQuerySolver = nullptr;

    SolverCertainty probeParamRelation(const Span& sp, const HIRPathParams& left, const HIRPathParams& right) const;

    mutable u64 solverEnvGeneration = 0;
    std::vector<HIRSimplePath> opaqueAliasScopes;
    std::vector<HIRSimplePath> definingOpaqueAliases;
    stl::Vector<const HIRPath*> definingFcnOrigins;
    struct ClosureReturnExpectation {
        const HIRExprNodeClosure* closure;
        const HIRType* type;
    };
    stl::Vector<ClosureReturnExpectation> closureReturnExpectations;

public:
    SolverCertainty probeTypeRelation(const Span& sp, const HIRType* left, const HIRType* right) const;

    TraitResolution(HMTypeInferrence& ivars, const WireBoard& wb, const HIRGenericParams* implParams, const HIRGenericParams* itemParams, const HIRSimplePath& visPath, const HIRGenericPath* currentTrait);
    ~TraitResolution();

    void setGenericContext(const HIRGenericParams* implParams, const HIRGenericParams* itemParams);

    void addOpaqueAliasScope(const HIRSimplePath& path);

    void addDefiningOpaqueAlias(const HIRSimplePath& path);

    void addDefiningFcnOrigin(const HIRPath& origin);
    bool isDefiningFcnOrigin(const HIRPath& origin) const;

    void setClosureReturnExpectation(const HIRExprNodeClosure* closure, const HIRType* type);
    const HIRType* closureReturnExpectation(const HIRExprNodeClosure* closure) const;

    bool isOpaqueAliasDefiningScope(const HIRTypeDataErasedTypeAliasInner& alias) const;

    const HIRGenericPath* currentTraitPath() const {
        return currentTraitPath_;
    }

    const HIRType* resolveType(const HIRType* type) const {
        return ivars.getType(type);
    }

    bool typeContainsIvars(const HIRType* type) const {
        return ivars.typeContainsIvars(type, false);
    }

    bool paramsContainIvars(const HIRPathParams& params) const {
        return ivars.pathparamsContainIvars(params, false);
    }

    void compactIvars(HMTypeInferrence& ivars, SolverResponseCallback* effects = nullptr);

    bool hasAssociatedType(const HIRType* ty) const;

    const HIRType* expandAssociatedTypes(const Span& sp, const HIRType* input, SolverResponseCallback* effects = nullptr) const;


    void expandAssociatedTypesParams(const Span& sp, HIRPathParams& params, SolverResponseCallback* effects = nullptr) const;

    bool iterateBoundsTraitsCb(const Span& sp, const HIRType* type, const HIRSimplePath& trait, TraitBoundCallback& cb) const;
    bool iterateBoundsTraitsCb(const Span& sp, const HIRType* type, TraitBoundCallback& cb) const;
    bool iterateBoundsTraitsCb(const Span& sp, TraitBoundCallback& cb) const;
    bool iterateAtyBoundsCb(const Span& sp, const HIRPath::Data::Data_UfcsKnown& pe, TraitPathCallback& cb) const;

    template <typename F>
    bool iterateBoundsTraits(const Span& sp, const HIRType* type, const HIRSimplePath& trait, F f) const {
        TraitBoundCb<F> cb(f);
        return iterateBoundsTraitsCb(sp, type, trait, cb);
    }

    template <typename F>
    bool iterateBoundsTraits(const Span& sp, const HIRType* type, F f) const {
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

    bool solveTraitGoalCb(const Span& sp, const HIRSimplePath& trait, const HIRPathParams& params, const HIRType* type, SolverResponseCallback& callback, const TraitGoalQuery& query = {}) const;

    template <typename F>
    bool solveTraitGoal(const Span& sp, const HIRSimplePath& trait, const HIRPathParams& params, const HIRType* type, F f, const TraitGoalQuery& query = {}) const {
        SolverResponseCb<F> cb(f);
        return solveTraitGoalCb(sp, trait, params, type, cb, query);
    }

    bool solveNormalizesToCb(const Span& sp, const NormalizesTo& goal, NormalizesToCallback& callback) const;

    template <typename F>
    bool solveNormalizesTo(const Span& sp, const NormalizesTo& goal, F f) const {
        NormalizesToCb<F> cb(f);
        return solveNormalizesToCb(sp, goal, cb);
    }

    bool implsOverlap(const Span& sp, const SolverImpl& left, const SolverImpl& right) const;

    const HIRPathParams& solverExistentials(const Span& sp, const HIRGenericParams& definition) const;

    Unifier::Outcome relateInherentImplHeader(const Span& sp, const HIRTypeImpl& impl, const HIRType* receiver, HIRPathParams& implParams) const;
    SolverCertainty evaluateInherentImpl(const Span& sp, const HIRTypeImpl& impl, const HIRType* receiver, HIRPathParams& implParams) const;
    SolverCertainty probeInherentImplHeader(const Span& sp, const HIRTypeImpl& impl, const HIRType* receiver, HIRPathParams& implParams) const;

    SolverCoercionResponse evaluateCoercionGoal(const Span& sp, const HIRType* destination, const HIRType* source, SolverCoercionOp op, bool allowSourceAutoderef = false) const;

    InherentImplSelection selectInherentImpl(const Span& sp, const HIRType* receiver, const RcString& item, InherentItemKind kind, const HIRPathParams* initialParams = nullptr) const;

    bool findNamedTraitInTraitCb(const Span& sp, const HIRSimplePath& des, const HIRPathParams& params, const HIRTrait& traitPtr, const HIRSimplePath& traitPath, const HIRPathParams& pp, const HIRType* selfType, TraitPathCallback& callback) const;

    template <typename F>
    bool findNamedTraitInTrait(const Span& sp, const HIRSimplePath& des, const HIRPathParams& params, const HIRTrait& traitPtr, const HIRSimplePath& traitPath, const HIRPathParams& pp, const HIRType* selfType, F f) const {
        TraitPathCb<F> cb(f);
        return findNamedTraitInTraitCb(sp, des, params, traitPtr, traitPath, pp, selfType, cb);
    }

private:
    enum class StructuralTrait {
        Sized,
        Copy,
        Clone,
    };

    bool assembleMagicCandidatesCb(const Span& sp, const HIRSimplePath& trait, const HIRPathParams& params, const HIRType* type, AssembledImplCallback& callback) const;
    bool assembleTypeCandidatesCb(const Span& sp, const HIRSimplePath& trait, const HIRPathParams& params, const HIRType* type, AssembledImplCallback& callback) const;
    bool assembleOtherCandidatesCb(const Span& sp, const HIRSimplePath& trait, const HIRPathParams& params, const HIRType* type, AssembledImplCallback& callback) const;
    bool assembleParamEnvCandidatesCb(const Span& sp, const HIRSimplePath& trait, const HIRPathParams& params, const HIRType* type, AssembledImplCallback& callback) const;

    template <typename F>
    bool assembleMagicCandidates(const Span& sp, const HIRSimplePath& trait, const HIRPathParams& params, const HIRType* type, F f) const {
        AssembledImplCb<F> cb(f);
        return assembleMagicCandidatesCb(sp, trait, params, type, cb);
    }

    template <typename F>
    bool assembleOtherCandidates(const Span& sp, const HIRSimplePath& trait, const HIRPathParams& params, const HIRType* type, F f) const {
        AssembledImplCb<F> cb(f);
        return assembleOtherCandidatesCb(sp, trait, params, type, cb);
    }

    template <typename F>
    bool assembleParamEnvCandidates(const Span& sp, const HIRSimplePath& trait, const HIRPathParams& params, const HIRType* type, F f) const {
        AssembledImplCb<F> cb(f);
        return assembleParamEnvCandidatesCb(sp, trait, params, type, cb);
    }

    HIRPathParams makeFreshImplParams(const HIRGenericParams& params) const;
    HIRPathParams materializeImplParams(const Span& sp, const HIRGenericParams& definition, const HIRPathParams& inferenceParams, size_t externalTypeIvars, size_t externalValueIvars) const;
    SolverCertainty solveTraitGoalCertainty(const Span& sp, const HIRSimplePath& trait, const HIRType* type) const;
    SolverCertainty solveStructuralTraitGoalCertainty(const Span& sp, StructuralTrait trait, const HIRType* type) const;
    SolverCertainty evaluateCoercionConstraint(const Span& sp, const SolverCoercionConstraint& constraint, const HIRType* input, ThinVector<SolverTypeEquality>* equalities = nullptr, SolverResponse* effects = nullptr, ThinVector<SolverDeferredCoercion>* deferred = nullptr, bool* reachedAutoderefLimit = nullptr, SolverCoercionAdjustment* adjustment = nullptr, bool exportPlaceholderEqualities = false) const;
    SolverCertainty evaluateGenericBounds(const Span& sp, const HIRGenericParams& definition, const HIRPathParams& parameters, const Monomorphiser& monomorph, u32 conditionalScope = 0, bool onlyBoundsConstrainingTraitParams = false) const;
    SolverCertainty evaluateInherentImplBounds(const Span& sp, const HIRTypeImpl& impl, const HIRPathParams& implParams) const;

public:
    enum class AutoderefBorrow {
        None,
        Shared,
        Unique,
        Owned,

        RawShared,

        PinShared,
    };

    struct MethodCandidate {
        AutoderefBorrow borrow;
        HIRPath path;
        const HIRTypeImpl* inherentImpl;
    };

    unsigned int autoderefFindMethod(
        const Span& sp,
        const tTraitList& traits,
        const stl::Vector<unsigned>& ivars,
        unsigned int typeIvarCount,
        const HIRType* topTy,
        const RcString& methodName,
        const ThinVector<const HIRType*>& argumentTypes,
        const HIRType* expectedResult,
        bool mustDecide,
        /* Out -> */ ThinVector<MethodCandidate>& possibilities
    ) const;

    enum class AutoderefResult {
        NoMatch,
        Match,
        Ambiguous,
    };

    struct Autoderef {
        AutoderefResult result;
        const HIRType* target;
        std::optional<const HIRType*> implType;
    };

    Autoderef autoderefStep(const Span& sp, const HIRType* ty) const;

    const HIRType* autoderef(const Span& sp, const HIRType* ty) const;

    const HIRType* findField(const Span& sp, const HIRType* ty, const RcString& name) const;

    enum class MethodAccess {
        Shared,
        Unique,
        Move,
    };

private:
    std::optional<const HIRType*> checkMethodReceiver(const Span& sp, const HIRFunction& fcn, const HIRType* ty, TraitResolution::MethodAccess access) const;

public:
    enum class AllowedReceivers {
        All,
        AnyBorrow,
        SharedBorrow,
        Value,
        Box,
    };
    bool findMethod(const Span& sp, const tTraitList& traits, const stl::Vector<unsigned>& ivars, unsigned int typeIvarCount, const HIRType* ty, const RcString& methodName, const ThinVector<const HIRType*>& argumentTypes, const HIRType* expectedResult, MethodAccess access, AutoderefBorrow borrowType, /* Out -> */ ThinVector<MethodCandidate>& possibilities, /* Out -> */ bool* outUndecided = nullptr) const;

    const HIRFunction* traitContainsMethod(const Span& sp, const HIRGenericPath& traitPath, const HIRTrait& traitPtr, const HIRType* self, const RcString& name, HIRGenericPath& outPath) const;
    bool traitContainsType(const Span& sp, const HIRGenericPath& traitPath, const HIRTrait& traitPtr, const char* name, HIRGenericPath& outPath) const;

    HIRCompare typeIsSized(const Span& sp, const HIRType* ty) const;
    HIRCompare typeIsCopy(const Span& sp, const HIRType* ty) const;
    HIRCompare typeIsClone(const Span& sp, const HIRType* ty) const;

    const HIRType* typeIsOwnedBox(const Span& sp, const HIRType* ty) const;

private:
    const HIRType* expandAssociatedTypesInplace(const Span& sp, const HIRType* input, SolverResponseCallback* effects = nullptr) const;
    const HIRType* expandAssociatedTypesInplaceUfcsInherent(const Span& sp, const HIRType* input, SolverResponseCallback* effects = nullptr) const;
    const HIRType* expandAssociatedTypesInplaceUfcsKnown(const Span& sp, const HIRType* input, SolverResponseCallback* effects = nullptr) const;
};
