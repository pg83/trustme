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

bool typeIsUnboundedInfer(const HIRTypeData* ty);

struct SolverImpl {
    HIRPathParams implParams;
    const HIRTrait* trait = nullptr;
    HIRSimplePath traitPath;
    const HIRTraitImpl* traitImpl = nullptr;

    const HIRTypeData* type = nullptr;
    HIRPathParams traitArgs;
    HIRTraitPath::assocListT associated;
    HIRBoundConstness constness = HIRBoundConstness::Never;

    SolverImpl(HIRPathParams implParams, const HIRTrait& trait, const HIRSimplePath& traitPath, const HIRTraitImpl& traitImpl);
    SolverImpl(const HIRTypeData* type, const HIRPathParams* traitArgs, const HIRTraitPath::assocListT* associated, HIRBoundConstness constness = HIRBoundConstness::Never);
    SolverImpl(const HIRTypeData* type, HIRPathParams traitArgs, HIRTraitPath::assocListT associated, HIRBoundConstness constness = HIRBoundConstness::Never);

    bool isTraitImpl() const {
        return traitImpl != nullptr;
    }

    const HIRTypeData* getImplType(HIRTypeInterner& types) const;
    HIRPathParams getTraitParams(HIRTypeInterner& types) const;
    const HIRTypeData* getTraitTyParam(HIRTypeInterner& types, unsigned index) const;
    const HIRTypeData* getType(HIRTypeInterner& types, const char* name, const HIRPathParams& params) const;
    bool typeIsSpecialisable(const char* name) const;
    bool moreSpecificThan(HIRTypeInterner& types, const SolverImpl& other) const;
    const HIRTypeData* monomorphImplType(HIRTypeInterner& types, const Span& sp, const HIRTypeData* type, const HIRPathParams& methodParams = {}) const;
    HIRTraitPath monomorphImplTraitPath(HIRTypeInterner& types, const Span& sp, const HIRTraitPath& traitPath, const HIRPathParams& methodParams = {}) const;
};

struct SolverSlotValues {
    ThinVector<const HIRTypeData*> typeInputs;
    ThinVector<const HIRTypeData*> types;
    ThinVector<HIRConstGeneric> valueInputs;
    ThinVector<HIRConstGeneric> values;
};

struct SolverObligation {
    const HIRTypeData* type;
    HIRTraitPath trait;
};

struct SolverTypeEquality {
    const HIRTypeData* left;
    const HIRTypeData* right;
};

struct SolverValueEquality {
    HIRConstGeneric left;
    HIRConstGeneric right;
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
        const HIRTypeData* ty;

        FmtType(const HMTypeInferrence& ctxt, const HIRTypeData* ty);
    };

    struct FmtPP {
        const HMTypeInferrence& ctxt;
        const HIRPathParams& pps;

        FmtPP(const HMTypeInferrence& ctxt, const HIRPathParams& pps);
    };

public:
    struct IVar {
        unsigned int alias;
        const HIRTypeData* type;

        explicit IVar(const HIRTypeData* type);

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
    stl::Vector<const HIRTypeData*> expandStack;
    stl::ObjPool::Ref aliasIvarPool;
    stl::IntMap<const HIRTypeData*> aliasTypeIvars;
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

    void printType(stl::ZeroCopyOutput& os, const HIRTypeData* tr, LList<const HIRTypeData*> stack = {}) const;
    void printGenericpath(stl::ZeroCopyOutput& os, const HIRGenericPath& pps, LList<const HIRTypeData*> stack) const;
    void printPathparams(stl::ZeroCopyOutput& os, const HIRPathParams& pps, LList<const HIRTypeData*> stack = {}) const;

    FmtType fmtType(const HIRTypeData* tr) const {
        return FmtType(*this, tr);
    }

    FmtPP fmt(const HIRPathParams& v) const {
        return FmtPP(*this, v);
    }

    const HIRTypeData* addIvars(const HIRTypeData* type);
    void addIvars(HIRConstGeneric& val);

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

    unsigned int newIvar(HIRInferClass ic = HIRInferClass::None);
    const HIRTypeData* newIvarTr(HIRInferClass ic = HIRInferClass::None);
    void setIvarTo(unsigned int slot, const HIRTypeData* type, bool solverProven = false);
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

    const HIRTypeData* getType(const HIRTypeData* type) const;
    const HIRTypeData* getType(unsigned idx) const;

    const HIRConstGeneric& getValue(const HIRConstGeneric& val) const;
    const HIRConstGeneric& getValue(unsigned idx) const;

    void checkForLoops();
    const HIRTypeData* expandIvars(const HIRTypeData* type);
    void expandIvars(HIRConstGeneric& value);
    void expandIvarsParams(HIRPathParams& params);

    bool pathparamsContainIvars(const HIRPathParams& pps, bool onlyUnbound) const;
    bool typeContainsIvars(const HIRTypeData* ty, bool onlyUnbound = false) const;
    bool pathparamsEqual(const HIRPathParams& ppsL, const HIRPathParams& ppsR) const;
    bool typesEqual(const HIRTypeData* l, const HIRTypeData* r) const;

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
        const HIRTypeData* oldType;
    };

    stl::Vector<JournalEntry> journal;
    unsigned snapshotDepth = 0;
    u64 generationCounter = 0;

    void journalMutation(JournalEntry::Kind kind, unsigned slot, const HIRTypeData* oldType);

    void addIvarsTraitPath(HIRTraitPath& path);
    void expandIvarsTraitPath(HIRTraitPath& path);

    unsigned int rootIvarIndex(unsigned int slot) const;
    IVar& getPointedIvar(unsigned int slot) const;

    bool containsLiveIvar(const HIRTypeData* type, unsigned int rootIndex) const;

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
        const HIRTypeData* left;
        const HIRTypeData* right;
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

    [[maybe_unused]] const Span& sp_;
    HMTypeInferrence& table_;
    const TraitResolution* resolve_;

    bool bindRigidValues_;
    bool relateProjectionInputs_;
    bool rigidGenericsAreDistinct_;
    bool rigidProjectionsAreDistinct_;
    stl::Vector<PendingEquality> pending_;
    ThinVector<PendingValueEquality> pendingValues_;
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
    const HIRTypeData* other;
    Direction direction;
    SolverCoercionOp op;

    bool isSelf = false;
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
    const HIRTypeData* assocType = nullptr;
    const HIRPathParams* assocParams = nullptr;

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
    const HIRTypeData* projection;
};

struct NormalizesToResponse {
    SolverResponse effects;
    const HIRTypeData* output;
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
    virtual bool visit(SolverImpl impl, SolverCertainty certainty = SolverCertainty::Proven) = 0;
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

    bool visit(SolverImpl impl, SolverCertainty certainty) override {
        return f(mv$(impl), certainty);
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
        const HIRTypeData* type;
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

    mutable NextTraitGoalEvaluator* nonBuiltinSolver = nullptr;

    mutable u64 solverEnvGeneration = 0;
    std::vector<HIRSimplePath> opaqueAliasScopes;
    std::vector<HIRSimplePath> definingOpaqueAliases;
    stl::Vector<const HIRPath*> definingFcnOrigins;

public:
    TraitResolution(HMTypeInferrence& ivars, const WireBoard& wb, const HIRGenericParams* implParams, const HIRGenericParams* itemParams, const HIRSimplePath& visPath, const HIRGenericPath* currentTrait);
    ~TraitResolution();

    void setGenericContext(const HIRGenericParams* implParams, const HIRGenericParams* itemParams);

    void addOpaqueAliasScope(const HIRSimplePath& path);

    void addDefiningOpaqueAlias(const HIRSimplePath& path);

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

    const HIRTypeData* expandAssociatedTypes(const Span& sp, const HIRTypeData* input, SolverResponseCallback* effects = nullptr) const;


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

    bool implsOverlap(const Span& sp, const SolverImpl& left, const SolverImpl& right) const;

    const HIRPathParams& solverExistentials(const Span& sp, const HIRGenericParams& definition) const;

    Unifier::Outcome relateInherentImplHeader(const Span& sp, const HIRTypeImpl& impl, const HIRTypeData* receiver, HIRPathParams& implParams) const;
    SolverCertainty evaluateInherentImpl(const Span& sp, const HIRTypeImpl& impl, const HIRTypeData* receiver, HIRPathParams& implParams) const;
    SolverCertainty probeInherentImplHeader(const Span& sp, const HIRTypeImpl& impl, const HIRTypeData* receiver, HIRPathParams& implParams) const;

    InherentImplSelection selectInherentImpl(const Span& sp, const HIRTypeData* receiver, const RcString& item, InherentItemKind kind, const HIRPathParams* initialParams = nullptr) const;

    bool findNamedTraitInTraitCb(const Span& sp, const HIRSimplePath& des, const HIRPathParams& params, const HIRTrait& traitPtr, const HIRSimplePath& traitPath, const HIRPathParams& pp, const HIRTypeData* selfType, TraitPathCallback& callback) const;

    template <typename F>
    bool findNamedTraitInTrait(const Span& sp, const HIRSimplePath& des, const HIRPathParams& params, const HIRTrait& traitPtr, const HIRSimplePath& traitPath, const HIRPathParams& pp, const HIRTypeData* selfType, F f) const {
        TraitPathCb<F> cb(f);
        return findNamedTraitInTraitCb(sp, des, params, traitPtr, traitPath, pp, selfType, cb);
    }

private:
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
    HIRPathParams materializeImplParams(const Span& sp, const HIRGenericParams& definition, const HIRPathParams& inferenceParams, size_t externalTypeIvars, size_t externalValueIvars) const;
    SolverCertainty solveNonBuiltinTraitGoal(const Span& sp, const HIRSimplePath& trait, const HIRTypeData* type) const;
    HIRCompare typeIsSizedBuiltin(const Span& sp, const HIRTypeData* type) const;
    HIRCompare typeIsCopyBuiltin(const Span& sp, const HIRTypeData* type) const;
    SolverCertainty evaluateCoercionGoal(const Span& sp, const SolverCoercionConstraint& constraint, const HIRTypeData* input, ThinVector<SolverTypeEquality>* equalities = nullptr) const;
    Ordering compareCoercionEndpoints(const Span& sp, const SolverCoercionConstraint& constraint, const HIRTypeData* left, const HIRTypeData* right) const;
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
        const HIRTypeData* topTy,
        const RcString& methodName,
        const ThinVector<const HIRTypeData*>& argumentTypes,
        const HIRTypeData* expectedResult,
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
        const HIRTypeData* target;
        std::optional<const HIRTypeData*> implType;
    };

    Autoderef autoderefStep(const Span& sp, const HIRTypeData* ty) const;

    const HIRTypeData* autoderef(const Span& sp, const HIRTypeData* ty) const;

    const HIRTypeData* findField(const Span& sp, const HIRTypeData* ty, const RcString& name) const;

    enum class MethodAccess {
        Shared,
        Unique,
        Move,
    };

private:
    std::optional<const HIRTypeData*> checkMethodReceiver(const Span& sp, const HIRFunction& fcn, const HIRTypeData* ty, TraitResolution::MethodAccess access) const;

public:
    enum class AllowedReceivers {
        All,
        AnyBorrow,
        SharedBorrow,
        Value,
        Box,
    };
    bool findMethod(const Span& sp, const tTraitList& traits, const stl::Vector<unsigned>& ivars, unsigned int typeIvarCount, const HIRTypeData* ty, const RcString& methodName, const ThinVector<const HIRTypeData*>& argumentTypes, const HIRTypeData* expectedResult, MethodAccess access, AutoderefBorrow borrowType, /* Out -> */ ThinVector<MethodCandidate>& possibilities, /* Out -> */ bool* outUndecided = nullptr) const;

    const HIRFunction* traitContainsMethod(const Span& sp, const HIRGenericPath& traitPath, const HIRTrait& traitPtr, const HIRTypeData* self, const RcString& name, HIRGenericPath& outPath) const;
    bool traitContainsType(const Span& sp, const HIRGenericPath& traitPath, const HIRTrait& traitPtr, const char* name, HIRGenericPath& outPath) const;

    HIRCompare typeIsSized(const Span& sp, const HIRTypeData* ty) const;
    HIRCompare typeIsCopy(const Span& sp, const HIRTypeData* ty) const;
    HIRCompare typeIsClone(const Span& sp, const HIRTypeData* ty) const;

    const HIRTypeData* typeIsOwnedBox(const Span& sp, const HIRTypeData* ty) const;

private:
    const HIRTypeData* expandAssociatedTypesInplace(const Span& sp, const HIRTypeData* input, SolverResponseCallback* effects = nullptr) const;
    const HIRTypeData* expandAssociatedTypesInplaceUfcsInherent(const Span& sp, const HIRTypeData* input, SolverResponseCallback* effects = nullptr) const;
    const HIRTypeData* expandAssociatedTypesInplaceUfcsKnown(const Span& sp, const HIRTypeData* input, SolverResponseCallback* effects = nullptr) const;
};
