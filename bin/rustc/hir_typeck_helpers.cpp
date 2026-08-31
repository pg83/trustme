#include "hir_typeck_helpers.h"

#include "output.h"
#include "settings.h"
#include "wire_board.h"
#include "thin_vector.h"
#include "trans_target.h"
#include "hir_inherent_cache.h"
#include "hir_typeck_monomorph.h"
#include "hir_conv_main_bindings.h"
#include "hir_conv_constant_evaluation.h"

#include <std/alg/defer.h>
#include <std/alg/range.h>
#include <std/sym/i_map.h>
#include <std/lib/vector.h>
#include <std/mem/obj_list.h>
#include <std/mem/obj_pool.h>
#include <std/rng/split_mix_64.h>

#include <optional>
#include <algorithm>
#include <unordered_map>

using namespace stl;

namespace {
    constexpr u32 SOLVER_ALPHA_SCOPE_BASE = ~u32(255);

    struct CanonicalizeTraitGoal;

    using NextTraitGoalEvaluator = TraitResolution::NextTraitGoalEvaluator;

    struct CanonicalizeTraitGoal final: public Monomorphiser {
        mutable std::vector<std::pair<RcString, RcString>> placeholderNames_;
        mutable Vector<const HIRType*> ivarNodes_;
        mutable Vector<unsigned> valueIvarIndexes_;
        mutable size_t inputPlaceholderCount_ = 0;
        const HMTypeInferrence* ivarTable_ = nullptr;
        mutable bool frozen_ = false;
        mutable bool sawForeignIvar_ = false;
        mutable bool sawForeignSolverExistential_ = false;
        const IntMap<u32>* alphaSolverScopes_ = nullptr;
        u32 alphaScopeBase_ = 0;
        mutable Vector<u32> solverScopes_;
        mutable Vector<u32> solverScopeClasses_;

        RcString canonicalPlaceholderName(const RcString& name) const;

        explicit CanonicalizeTraitGoal(HIRTypeInterner& types, const HMTypeInferrence* ivarTable = nullptr, const IntMap<u32>* alphaSolverScopes = nullptr, u32 alphaScopeBase = 0);

        const HIRType* canonicalIvar(const HIRType* infer) const;

        void freeze() const;

        bool sawForeignIvar() const;

        bool sawForeignSolverExistential() const;

        std::optional<HIRGenericRef> originalSolverGeneric(const HIRGenericRef& generic) const;

        const Vector<u32>& alphaSolverEnvironment() const;

        const HIRType* originalIvar(unsigned index) const;

        HIRConstGeneric canonicalValueIvar(unsigned original) const;

        const unsigned* originalValueIvar(unsigned index) const;

        const HIRType* monomorphType(const Span& sp, const HIRType* ty, bool allowInfer = true) const override;

        const HIRType* getType(const Span&, const HIRGenericRef& generic) const override;

        HIRConstGeneric getValue(const Span&, const HIRGenericRef& generic) const override;

        HIRConstGeneric monomorphConstgeneric(const Span& sp, const HIRConstGeneric& val, bool allowInfer) const override;

        const std::vector<std::pair<RcString, RcString>>& placeholderNames() const;

        const RcString* originalPlaceholderName(const RcString& canonical) const;

        const RcString* originalResponsePlaceholderName(const RcString& canonical) const;

        const Vector<const HIRType*>& ivarNodes() const;

        size_t typeSlotCount() const;

        size_t valueSlotCount() const;

        const HIRType* canonicalTypeSlot(size_t slot) const;

        HIRConstGeneric canonicalValueSlot(size_t slot) const;
    };

    struct InstantiateCanonicalTraitResponse final: public Monomorphiser {
        const std::vector<std::pair<RcString, RcString>>& goalNames;
        const struct CanonicalizeTraitGoal* goalCanonicalizer = nullptr;
        const u64 instance;
        mutable std::vector<std::pair<RcString, RcString>> freshNames;

        RcString instantiatePlaceholderName(const RcString& canonical) const;

        InstantiateCanonicalTraitResponse(HIRTypeInterner& types, const std::vector<std::pair<RcString, RcString>>& goalNames, u64 instance, const CanonicalizeTraitGoal* goalCanonicalizer = nullptr);

        const HIRType* getType(const Span&, const HIRGenericRef& generic) const override;

        const HIRType* monomorphType(const Span& sp, const HIRType* ty, bool allowInfer = true) const override;

        HIRConstGeneric monomorphConstgeneric(const Span& sp, const HIRConstGeneric& val, bool allowInfer) const override;

        HIRConstGeneric getValue(const Span&, const HIRGenericRef& generic) const override;
    };

    struct InstantiateTraitResponseForCaller final: public Monomorphiser {
        HMTypeInferrence& ivars;
        const std::vector<std::pair<RcString, RcString>>& goalNames;
        const CanonicalizeTraitGoal* goalCanonicalizer = nullptr;
        mutable std::vector<std::pair<HIRGenericRef, const HIRType*>> typeValues;
        mutable std::vector<std::pair<HIRGenericRef, HIRConstGeneric>> values;

        bool isGoalPlaceholder(const HIRGenericRef& generic) const;

        InstantiateTraitResponseForCaller(HIRTypeInterner& types, HMTypeInferrence& ivars, const std::vector<std::pair<RcString, RcString>>& goalNames, const CanonicalizeTraitGoal* goalCanonicalizer = nullptr);

        const HIRType* monomorphType(const Span& sp, const HIRType* ty, bool allowInfer = true) const override;

        HIRConstGeneric monomorphConstgeneric(const Span& sp, const HIRConstGeneric& val, bool allowInfer) const override;

        HIRGenericRef callerGeneric(const HIRGenericRef& generic) const;

        const HIRType* getType(const Span&, const HIRGenericRef& raw) const override;

        HIRConstGeneric getValue(const Span&, const HIRGenericRef& raw) const override;
    };

    struct CorrelateSolverResponseSlots final: public MonomorphiserNop {
        const SolverSlotValues& slots_;
        Vector<std::pair<const HIRType*, const HIRType*>> structuralTypes_;

        void correlateParams(const HIRPathParams& input, const HIRPathParams& response);

        void correlateGenericPath(const HIRGenericPath& input, const HIRGenericPath& response);

        void correlatePath(const HIRPath& input, const HIRPath& response);

        CorrelateSolverResponseSlots(HIRTypeInterner& interner, const SolverSlotValues& slots);

        void correlateType(const HIRType* input, const HIRType* response);

        const HIRType* monomorphType(const Span& sp, const HIRType* type, bool allowInfer = true) const override;

        HIRConstGeneric monomorphConstgeneric(const Span& sp, const HIRConstGeneric& value, bool allowInfer) const override;
    };

    struct DecanonicalizeSolverInfers final: public MonomorphiserNop {
        const CanonicalizeTraitGoal& canonicalizer_;

        DecanonicalizeSolverInfers(HIRTypeInterner& types, const CanonicalizeTraitGoal& canonicalizer);

        const HIRType* monomorphType(const Span& sp, const HIRType* ty, bool allowInfer = true) const override;

        const HIRType* getType(const Span&, const HIRGenericRef& generic) const override;

        HIRConstGeneric monomorphConstgeneric(const Span& sp, const HIRConstGeneric& val, bool allowInfer) const override;

        HIRConstGeneric getValue(const Span&, const HIRGenericRef& generic) const override;
    };

    const HIRType* InstantiateCanonicalTraitResponse::getType(const Span&, const HIRGenericRef& generic) const {
        if (goalCanonicalizer) {
            if (const auto original = goalCanonicalizer->originalSolverGeneric(generic)) {
                return types.generic(*original);
            }
        }
        return generic.isPlaceholder() && !generic.isSolverExistential() ? types.generic(instantiatePlaceholderName(generic.name), generic.binding) : types.generic(generic);
    }

    bool typeListEqual(const HMTypeInferrence& context, const Vector<const HIRType*>& l, const Vector<const HIRType*>& r) {
        if (l.length() != r.length()) {
            return false;
        }

        for (unsigned int i = 0; i < l.length(); i++) {
            if (!context.typesEqual(l[i], r[i])) {
                return false;
            }
        }
        return true;
    }

    bool typeListEqual(const HMTypeInferrence& context, const ThinVector<const HIRType*>& l, const ThinVector<const HIRType*>& r) {
        if (l.size() != r.size()) {
            return false;
        }

        for (unsigned int i = 0; i < l.size(); i++) {
            if (!context.typesEqual(l[i], r[i])) {
                return false;
            }
        }
        return true;
    }

    bool inferIsLive(const HIRType* type) {
        const auto* infer = type->opt_Infer();
        return infer && infer->index != ~0u && !isAliasInputInfer(infer->index);
    }

    bool typeIsRigidUnknown(const HIRType* type) {
        if (const auto* path = type->opt_Path()) {
            if (!path->path.data.is_Generic()) {
                return true;
            }
            return path->binding.is_Unbound() || path->binding.is_Opaque();
        }
        return false;
    }

    bool literalClassAccepts(const HMTypeInferrence& table, HIRInferClass tyClass, const HIRType* type) {
        if (tyClass == HIRInferClass::None) {
            return true;
        }
        if (const auto* primitive = type->opt_Primitive()) {
            return typeClassPrimitiveCompatible(tyClass, *primitive);
        }
        if (const auto* pattern = type->opt_Pattern()) {
            const auto* primitive = table.getType(pattern->inner)->opt_Primitive();
            return primitive && typeClassPrimitiveCompatible(tyClass, *primitive);
        }
        return false;
    }

    HIRCompare compareValue(const Span& sp, const HIRConstGeneric& leftRaw, const HIRConstGeneric& rightRaw, const HMTypeInferrence& infer) {
        const auto& left = infer.getValue(leftRaw);
        const auto& right = infer.getValue(rightRaw);
        if (left == right) {
            return HIRCompare::Equal;
        }
        if (left.is_Infer() || right.is_Infer()) {
            return HIRCompare::Fuzzy;
        }
        if (left.is_Generic() && left.as_Generic().isPlaceholder()) {
            return HIRCompare::Fuzzy;
        }
        if (right.is_Generic() && right.as_Generic().isPlaceholder()) {
            return HIRCompare::Fuzzy;
        }
        //TODO(sp, StringView("compare_value: ") << left << " == " << right);
        return HIRCompare::Unequal;
    }

    bool traitContainsMethodInner(const HIRTrait& traitPtr, const RcString& name, const HIRFunction*& outFcnPtr) {
        auto it = traitPtr.values.find(name);
        if (it != traitPtr.values.end() && it->second.is_Function()) {
            outFcnPtr = &it->second.as_Function();
            return true;
        }
        return false;
    }
}

struct TraitResolution::NextTraitGoalEvaluator {
    using Certainty = SolverCertainty;

    enum class CandidateSource {
        Builtin,
        ParamEnv,
        AliasBound,
        Other,
        TraitImpl,
    };

    enum class OrphanPerspective {
        Local,
        Remote,
    };

    enum class OrphanVisit {
        NonLocal,
        LocalKey,
        Uncovered,
    };

    struct Candidate {
        SolverImpl impl;
        bool headExact;
        Certainty headRelation;
        Certainty certainty;
        const HIRMarkerImpl* markerImpl;
        HIRPathParams markerImplParams;
        bool autoBuiltin;
        CandidateSource source;
        bool headNormalizationAmbiguity;
        bool ambiguityBeyondHead = false;
        bool nestedAmbiguity = false;
        bool coercionsProven = true;
        bool coercionsEvaluated = false;
        ThinVector<SolverTypeEquality> headEqualities;
        ThinVector<SolverValueEquality> headValueEqualities;
        ThinVector<SolverTypeEquality> coercionEqualities;
        ThinVector<SolverObligation> headObligations;
        ThinVector<SolverTypeEquality> relationEqualities;
        ThinVector<SolverValueEquality> relationValueEqualities;
        ThinVector<SolverObligation> relationObligations;
        Vector<const HIRGenericBound*> normalizationNestedGoals;
        bool discarded = false;
        Certainty traitCertainty = Certainty::Ambiguous;
        const Candidate* specializationItemSource = nullptr;
        mutable bool responseHeadMaterialized = false;
        mutable const HIRType* responseImplType = nullptr;
        mutable HIRPathParams responseTraitParams;
        mutable bool responseAssociatedMaterialized = false;
        mutable const HIRType* responseAssociatedType = nullptr;

        Candidate(SolverImpl impl, bool headExact, Certainty headRelation, const HIRMarkerImpl* markerImpl, HIRPathParams markerImplParams, bool autoBuiltin, CandidateSource source, bool headNormalizationAmbiguity = false, ThinVector<SolverTypeEquality> headEqualities = {}, ThinVector<SolverValueEquality> headValueEqualities = {});

        bool isNegative() const;

        bool isPositiveMarkerImpl() const;
    };

    struct CandidateFrame {
        Vector<Candidate*> candidates;
        Vector<Candidate*> viable;
        size_t availableDepth = 0;
        bool encounteredOverflow = false;

        CandidateFrame();

        void clear(ObjList<Candidate>& nodes);
    };

    static constexpr size_t ROOT_DEPTH = 128;
    static constexpr size_t OVERFLOW_DEPTH_DIVISOR = 4;

    struct GoalKey {
        size_t hash;
        HIRSimplePath trait;
        HIRPathParams params;
        const HIRType* type;
        HIRTraitPath::assocListT associated;
        ThinVector<u32> existentialEnvironment;
        bool suppressAmbiguity;

        GoalKey(size_t hash, const HIRSimplePath& trait, const HIRPathParams& params, const HIRType* type, const HIRTraitPath::assocListT* associated, const Vector<u32>* existentialEnvironment = nullptr, bool suppressAmbiguity = false);
    };

    struct CachedGoal {
        GoalKey goal;
        Certainty certainty;
        const SolverResponse* response = nullptr;
        bool hasResponse = false;
        bool persistent = false;
        bool responseIsIdentity = false;

        CachedGoal(size_t hash, const HIRSimplePath& trait, const HIRPathParams& params, const HIRType* type, const HIRTraitPath::assocListT* associated, Certainty certainty, const Vector<u32>* existentialEnvironment = nullptr, bool suppressAmbiguity = false);
    };

    const TraitResolution& resolve_;
    const HIRCrate& crate;
    const Span* span_ = nullptr;
    bool coherenceMode = false;
    mutable u64 cycleHits_ = 0;
    mutable u64 envGeneration_ = ~0ull;
    mutable u64 ivarGenerationSeen_ = ~0ull;
    mutable u64 solverEnvGenerationSeen_ = ~0ull;

    struct ImplExistentials {
        const HIRGenericParams* definition;
        HIRPathParams params;
    };

    IntMap<ThinVector<ImplExistentials>> implExistentials_;
    IntMap<u32> implExistentialScopes_;
    u32 alphaExistentialScopeBase_ = 0;

    ObjList<Candidate> candidateNodes;
    Vector<CandidateFrame*> frames;
    size_t frameDepth = 0;
    ObjList<GoalKey> activeGoalNodes;
    ObjList<CachedGoal> cachedGoalNodes;
    Vector<GoalKey*> goalStack;
    Vector<CachedGoal*> goalCache;
    std::unordered_multimap<size_t, GoalKey*> activeGoalIndex;
    std::unordered_multimap<size_t, CachedGoal*> goalCacheIndex;

    struct EmptyRootGoal {
        GoalKey goal;
        bool includeMagicCandidates;

        EmptyRootGoal(size_t hash, const HIRSimplePath& trait, const HIRPathParams& params, const HIRType* type, const Vector<u32>& existentialEnvironment, bool includeMagicCandidates);
    };

    ThinVector<EmptyRootGoal> emptyRootGoals;
    IntMap<ThinVector<size_t>> emptyRootGoalIndex;

    struct RawNestedNoEffectResponse {
        GoalKey goal;
        const HIRTraitImpl* traitImpl;
        const HIRMarkerImpl* markerImpl;
        HIRPathParams candidateParams;
        Certainty certainty;

        RawNestedNoEffectResponse(size_t hash, const HIRSimplePath& trait, const HIRPathParams& params, const HIRType* type, const HIRTraitPath::assocListT* associated, const Candidate& candidate, Certainty certainty);
    };

    ThinVector<RawNestedNoEffectResponse> rawNestedNoEffectResponses;
    IntMap<ThinVector<size_t>> rawNestedNoEffectResponseIndex;

    struct CanonicalNestedNoEffectResponse {
        GoalKey goal;
        const HIRTraitImpl* traitImpl;
        const HIRMarkerImpl* markerImpl;
        HIRPathParams candidateParams;
        Certainty certainty;

        CanonicalNestedNoEffectResponse(size_t hash, const HIRSimplePath& trait, const HIRPathParams& params, const HIRType* type, const HIRTraitPath::assocListT* associated, const Vector<u32>& existentialEnvironment, const Candidate& candidate, const HIRPathParams& candidateParams, Certainty certainty);
    };

    ThinVector<CanonicalNestedNoEffectResponse> canonicalNestedNoEffectResponses;
    IntMap<ThinVector<size_t>> canonicalNestedNoEffectResponseIndex;

    struct CanonicalGoal {
        HIRPathParams params;
        const HIRType* type;
        HIRTraitPath::assocListT associated;

        CanonicalGoal(HIRPathParams params, const HIRType* type);
    };

    const Span& span() const;

    const HIRPathParams& implExistentials(const HIRGenericParams& definition);

    bool goalIsConcrete(const HIRSimplePath& trait, const CanonicalGoal& canonical) const;

    bool crateCacheUsable() const;

    NextSolverCrateCache& crateCache() const;

    CanonicalGoal canonicalizeGoal(const HIRPathParams& params, const HIRType* type, const HIRTraitPath::assocListT* associated, CanonicalizeTraitGoal& canonicalizer) const;

    std::optional<size_t> availableDepthForNested();

    static bool isEnvironmentOrBuiltin(const SolverImpl& impl);

    bool paramsHaveUnknownTypes(const HIRPathParams& params) const;

    bool pathHasUnknownTypes(const HIRPath& path) const;

    bool traitPathHasUnknownTypes(const HIRTraitPath& trait) const;

    bool valueHasUnassignedInfer(const HIRConstGeneric& value) const;

    bool paramsHaveUnassignedInfer(const HIRPathParams& params) const;

    bool pathHasUnassignedInfer(const HIRPath& path) const;

    bool traitPathHasUnassignedInfer(const HIRTraitPath& trait) const;

    bool typeHasUnassignedInfer(const HIRType* input) const;

    bool goalHasUnassignedInfer(const HIRPathParams& params, const HIRType* type, const HIRTraitPath::assocListT* associated) const;

    bool selfIsUnresolvedProjectionOverIvar(const HIRType* type) const;

    const HIRType* normalizeGoalInput(const HIRType* input) const;

    bool typeHasUnknown(const HIRType* input) const;

    static bool typeHasCandidatePlaceholder(const HIRType* type);

    static bool typeHasUfcsUnknown(const HIRType* type);

    static bool paramsNeedResponseConstraints(const HIRPathParams& params);

    bool candidateNeedsResponseConstraints(const Candidate& candidate) const;

    OrphanVisit orphanVisitResolvedType(const HIRType* type, OrphanPerspective perspective) const;

    OrphanVisit orphanVisitType(const HIRType* input, OrphanPerspective perspective) const;

    bool orphanCheckTraitRef(const HIRPathParams& params, const HIRType* type, OrphanPerspective perspective) const;

    bool traitRefIsKnowable(const HIRSimplePath& trait, const HIRPathParams& params, const HIRType* type) const;

    static size_t hashMix(size_t state, size_t value);

    static size_t hashSimplePath(const HIRSimplePath& path);

    static size_t hashType(const HIRType* type);

    static size_t hashValue(const HIRConstGeneric& value);

    static size_t goalHash(const HIRSimplePath& trait, const HIRPathParams& params, const HIRType* type, const HIRTraitPath::assocListT* associated);

    static size_t goalHashWithEnvironment(size_t hash, const Vector<u32>& existentialEnvironment);

    static HIRTraitPath::assocListT cloneAssociated(const HIRTraitPath::assocListT* associated);

    SolverImpl monomorphCandidateImpl(const SolverImpl& source, const Monomorphiser& monomorph) const;

    const SolverImpl* ownSolverImpl(SolverImpl source) const;

    const SolverImpl* monomorphSolverImpl(const SolverImpl& source, const Monomorphiser& monomorph) const;

    const SolverImpl* correlateSolverImplForRead(const SolverImpl& source, const SolverSlotValues& slots, const HIRType* type, const HIRPathParams& params) const;

    SolverResponse monomorphSolverResponse(const SolverResponse& source, const Monomorphiser& monomorph, bool includeObligations = true) const;

    SolverSlotValues extractSlotValues(const CanonicalGoal& goal, const SolverImpl& response, const CanonicalizeTraitGoal& canonicalizer, Certainty certainty) const;

    static bool goalMatches(const GoalKey& goal, const HIRSimplePath& trait, const HIRPathParams& params, const HIRType* type, const HIRTraitPath::assocListT* associated, const Vector<u32>* existentialEnvironment = nullptr, bool suppressAmbiguity = false);

    CachedGoal* findCachedGoal(size_t hash, const HIRSimplePath& trait, const HIRPathParams& params, const HIRType* type, const HIRTraitPath::assocListT* associated, const Vector<u32>* existentialEnvironment = nullptr, bool suppressAmbiguity = false) const;

    GoalKey* findActiveGoal(size_t hash, const HIRSimplePath& trait, const HIRPathParams& params, const HIRType* type, const HIRTraitPath::assocListT* associated) const;

    GoalKey* pushActiveGoal(size_t hash, const HIRSimplePath& trait, const HIRPathParams& params, const HIRType* type, const HIRTraitPath::assocListT* associated);

    void popActiveGoal(GoalKey* goal);

    CachedGoal* cacheGoal(size_t hash, const HIRSimplePath& trait, const HIRPathParams& params, const HIRType* type, const HIRTraitPath::assocListT* associated, Certainty certainty, bool persistent = false, bool responseIsIdentity = false, const Vector<u32>* existentialEnvironment = nullptr);

    CachedGoal* cacheResponse(size_t hash, const HIRSimplePath& trait, const HIRPathParams& params, const HIRType* type, const HIRTraitPath::assocListT* associated, const SolverResponse* response, const Vector<u32>* existentialEnvironment = nullptr, bool suppressAmbiguity = false);

    void clearGoalCache(bool clearCanonicalNoEffectResponses = false);

    bool rootAssemblyKnownEmpty(size_t hash, const HIRSimplePath& trait, const HIRPathParams& params, const HIRType* type, const Vector<u32>& existentialEnvironment, bool includeMagicCandidates) const;

    void rememberEmptyRootAssembly(size_t hash, const HIRSimplePath& trait, const HIRPathParams& params, const HIRType* type, const Vector<u32>& existentialEnvironment, bool includeMagicCandidates);

    size_t rawNestedNoEffectHash(const HIRSimplePath& trait, const HIRPathParams& params, const HIRType* type, const HIRTraitPath::assocListT* associated, const Candidate& candidate) const;

    bool findRawNestedNoEffectResponse(const HIRSimplePath& trait, const HIRPathParams& params, const HIRType* type, const HIRTraitPath::assocListT* associated, const Candidate& candidate, Certainty& certainty) const;

    void rememberRawNestedNoEffectResponse(const HIRSimplePath& trait, const HIRPathParams& params, const HIRType* type, const HIRTraitPath::assocListT* associated, const Candidate& candidate, Certainty certainty);

    size_t canonicalNestedNoEffectHash(size_t goalHash, const Candidate& candidate, const HIRPathParams& candidateParams) const;

    bool findCanonicalNestedNoEffectResponse(size_t goalHash, const HIRSimplePath& trait, const HIRPathParams& params, const HIRType* type, const HIRTraitPath::assocListT* associated, const Vector<u32>& existentialEnvironment, const Candidate& candidate, const HIRPathParams& candidateParams, Certainty& certainty) const;

    void rememberCanonicalNestedNoEffectResponse(const GoalKey& goal, const Candidate& candidate, const HIRPathParams& candidateParams, Certainty certainty);

    static bool canonicalGoalIsRigid(const CanonicalGoal& canonical);

    static const HIRTraitPath::assocListT* boundedAssociated(const SolverImpl& impl);

    static bool associatedResponsesEqual(const HIRTraitPath::assocListT* left, const HIRTraitPath::assocListT* right);

    bool isSameImpl(const SolverImpl& left, const SolverImpl& right) const;

    bool paramEnvCandidateIsNonGlobal(const Candidate& candidate) const;

    void pushCandidate(size_t frameIndex, SolverImpl impl, bool headExact, Certainty headRelation, const HIRMarkerImpl* markerImpl = nullptr, HIRPathParams markerImplParams = {}, bool autoBuiltin = false, CandidateSource source = CandidateSource::Other, bool headNormalizationAmbiguity = false, ThinVector<SolverTypeEquality> headEqualities = {}, ThinVector<SolverValueEquality> headValueEqualities = {});

    Certainty relateAssembledHead(CandidateSource source, const HIRPathParams& goalParams, const HIRType* goalType, SolverImpl& impl, bool& headNormalizationAmbiguity, ThinVector<SolverTypeEquality>& headEqualities, ThinVector<SolverValueEquality>& headValueEqualities) const;

    bool assembledHeadIsExact(const HIRPathParams& goalParams, const HIRType* goalType, const SolverImpl& impl) const;

    Certainty unifyImplHead(const HIRGenericParams& implParamsDef, const HIRPathParams& implTraitArgs, const HIRType* implType, const HIRPathParams& goalParams, const HIRType* goalType, HIRPathParams& outputParams, bool& headNormalizationAmbiguity, ThinVector<SolverTypeEquality>& headEqualities, ThinVector<SolverValueEquality>& headValueEqualities);

    void assembleAliasBoundCandidates(size_t frameIndex, const HIRSimplePath& trait, const HIRPathParams& params, const HIRType* type);

    void assembleCandidates(size_t frameIndex, const HIRSimplePath& trait, const HIRPathParams& params, const HIRType* type, bool includeMagicCandidates = true, bool includeTraitImplCandidates = true);

    void assembleTraitImplCandidates(size_t frameIndex, const HIRSimplePath& trait, const HIRPathParams& params, const HIRType* type, bool includeMagicCandidates = true);

    const HIRType* makeAssociatedProjection(const HIRType* type, const HIRGenericPath& sourceTrait, const RcString& name, const HIRPathParams& associatedParams) const;

    const HIRType* makeAssociatedProjection(const SolverImpl& impl, const HIRGenericPath& sourceTrait, const RcString& name, const HIRPathParams& associatedParams) const;

    struct CandidateTypeBinding {
        const HIRType* stable;
        const HIRType* probe;
    };

    struct CandidateValueBinding {
        RcString name;
        unsigned stableIndex;
        unsigned probeIndex;
        bool isGeneric;
    };

    enum class CandidateBindingResult {
        Mismatch,
        Unchanged,
        Changed,
    };

    template <typename Relate>
    CandidateBindingResult unifyCandidateParams(HIRPathParams& params, Relate relate);

    CandidateBindingResult bindCandidatePlaceholders(Candidate& candidate, const HIRType* nestedType, const HIRTraitPath::assocListT& associated, bool useCandidateResponse = false, bool applyResponseBindings = false);

    CandidateBindingResult bindCandidateResponse(Candidate& candidate, const HIRType* nestedType, const HIRPathParams& nestedParams, const HIRTraitPath::assocListT& nestedAssociated, const SolverImpl& response);

    Certainty unifyProbe(const HIRType* left, const HIRType* right);

    Certainty unifyValueProbe(const HIRConstGeneric& left, const HIRConstGeneric& right);

    void appendRelationEffects(Candidate& candidate, SolverResponse response);

    Certainty solveRelationGoal(Candidate& candidate, const HIRSimplePath& trait, const HIRPathParams& params, const HIRType* type, const HIRTraitPath::assocListT* associated);

    Certainty relateTypes(Candidate& candidate, const HIRType* left, const HIRType* right);

    Certainty relateValues(Candidate& candidate, const HIRConstGeneric& left, const HIRConstGeneric& right);

    Certainty evaluateBuiltinUnsize(Candidate& candidate, const HIRType* destination, const HIRType* source);

    Certainty evaluateUnsizeRelation(Candidate& candidate, const HIRType* destination, const HIRType* source);

    bool canAssembleBuiltinUnsize(const HIRType* destination, const HIRType* source) const;

    Certainty evaluateHeadEquality(Candidate& candidate, const SolverTypeEquality& equality);

    Certainty matchAssociatedTypes(const HIRSimplePath& trait, Candidate& candidate, const HIRTraitPath::assocListT* associated, const Monomorphiser* headBindings = nullptr);

    Certainty evaluateAutoBuiltin(const HIRSimplePath& trait, const HIRPathParams& params, const HIRType* type);

    Certainty evaluateCandidate(size_t frameIndex, size_t candidateIndex, const HIRSimplePath& trait, const HIRTraitPath::assocListT* associated);

    struct NestedResponseMemo {
        const Candidate& candidate;
        CachedGoal* goal = nullptr;
        HIRPathParams canonicalCandidateParams;
        bool prepared = false;
        bool knownNoEffect = false;
        Certainty noEffectCertainty = Certainty::NoSolution;

        explicit NestedResponseMemo(const Candidate& candidate)
            : candidate(candidate)
        {
        }
    };

    Certainty solveGoal(const HIRSimplePath& trait, const HIRPathParams& params, const HIRType* type, const HIRTraitPath::assocListT* associated, bool* responseIsIdentity = nullptr, NestedResponseMemo* responseMemo = nullptr);

    bool literalClassCanMatch(const HIRSimplePath& trait, const HIRPathParams& params, HIRInferClass tyClass) const;

    bool containsDefiningOpaque(const HIRType* ty) const;

    Certainty matchRootAssociated(const HIRSimplePath& trait, Candidate& candidate, const char* assocName, const HIRType* assocType, const HIRPathParams* assocParams);

    SolverImpl materializeRootAssociated(SolverImpl impl, const HIRSimplePath& trait, const char* assocName, const HIRPathParams* assocParams) const;

    void appendResponseObligations(ThinVector<SolverObligation>& obligations, const Candidate* candidate, const Monomorphiser& canonicalizer) const;

    static bool implDefinesValue(const SolverImpl& impl, const char* valueName);

    static const Candidate* specializationValueSource(const Candidate* selected, const char* valueName);

    bool responsesEqual(const Candidate& left, const Candidate& right, const char* assocName, const HIRPathParams* assocParams, const char* valueName) const;

    NextTraitGoalEvaluator(const TraitResolution& resolve, const HIRCrate& crate);

    bool evaluateOverlap(const Span& callSpan, const HIRSimplePath& trait, const HIRTraitImpl& left, const HIRTraitImpl& right);

    struct OverlapEntry {
        const HIRTraitImpl* left;
        const HIRTraitImpl* right;
        bool overlaps;
    };

    IntMap<ThinVector<OverlapEntry>> overlapCache;

    bool evaluateOverlapUncached(const Span& callSpan, const HIRSimplePath& trait, const HIRTraitImpl& left, const HIRTraitImpl& right);

    bool evaluateTyped(const Span& callSpan, const HIRSimplePath& trait, const HIRPathParams& params, const HIRType* type, SolverResponseCallback& callback, const TraitGoalQuery& query, bool callerBoundary = false, bool includeRootMagicCandidates = true);

    bool evaluateNormalizesTo(const Span& callSpan, const NormalizesTo& goal, NormalizesToCallback& callback, bool callerBoundary = false);
};

SolverImpl::SolverImpl(HIRPathParams implParams, const HIRTrait& trait, const HIRSimplePath& traitPath, const HIRTraitImpl& traitImpl)
    : implParams(std::move(implParams))
    , trait(&trait)
    , traitPath(traitPath)
    , traitImpl(&traitImpl)
{
}

SolverImpl::SolverImpl(const HIRType* type, const HIRPathParams* traitArgs, const HIRTraitPath::assocListT* associated, HIRBoundConstness constness)
    : type(type)
    , traitArgs(traitArgs ? traitArgs->clone() : HIRPathParams())
    , constness(constness)
{
    BUG_ASSERT(this->type);
    if (associated) {
        for (const auto& entry : *associated) {
            this->associated.insert({entry.first, entry.second.clone()});
        }
    }
}

SolverImpl::SolverImpl(const HIRType* type, HIRPathParams traitArgs, HIRTraitPath::assocListT associated, HIRBoundConstness constness)
    : type(std::move(type))
    , traitArgs(std::move(traitArgs))
    , associated(std::move(associated))
    , constness(constness)
{
    BUG_ASSERT(this->type);
}

namespace {
    struct SolverImplTraitMonomorph final: Monomorphiser {
        const HIRTraitImpl& traitImpl_;
        const HIRPathParams& implParams_;
        const HIRPathParams& methodParams_;
        mutable const HIRType* selfType_ = nullptr;
        mutable bool resolvingSelf_ = false;

        SolverImplTraitMonomorph(HIRTypeInterner& types, const HIRTraitImpl& traitImpl, const HIRPathParams& implParams, const HIRPathParams& methodParams)
            : Monomorphiser(types)
            , traitImpl_(traitImpl)
            , implParams_(implParams)
            , methodParams_(methodParams)
        {
        }

        const HIRType* getType(const Span& sp, const HIRGenericRef& generic) const override {
            if (generic.isSelf()) {
                ASSERT_BUG(sp, !resolvingSelf_, StringView("Use of Self in expansion of Self"));
                if (!selfType_) {
                    resolvingSelf_ = true;
                    selfType_ = monomorphType(sp, traitImpl_.type);
                    resolvingSelf_ = false;
                }
                return selfType_;
            }
            return MonomorphStatePtr(types, nullptr, &implParams_, &methodParams_).getType(sp, generic);
        }

        HIRConstGeneric getValue(const Span& sp, const HIRGenericRef& generic) const override {
            return MonomorphStatePtr(types, nullptr, &implParams_, &methodParams_).getValue(sp, generic);
        }
    };
}

const HIRType* SolverImpl::getImplType(HIRTypeInterner& types) const {
    if (!traitImpl) {
        return type;
    }
    if (cachedImplType) {
        return cachedImplType;
    }
    Span sp;
    cachedImplType = SolverImplTraitMonomorph(types, *traitImpl, implParams, {}).monomorphType(sp, traitImpl->type);
    return cachedImplType;
}

const HIRPathParams& SolverImpl::getTraitParamsRef(HIRTypeInterner& types) const {
    if (!traitImpl) {
        return traitArgs;
    }
    if (!hasCachedTraitParams) {
        Span sp;
        cachedTraitParams = SolverImplTraitMonomorph(types, *traitImpl, implParams, {}).monomorphPathParams(sp, traitImpl->traitArgs, true);
        hasCachedTraitParams = true;
    }
    return cachedTraitParams;
}

HIRPathParams SolverImpl::getTraitParams(HIRTypeInterner& types) const {
    return getTraitParamsRef(types).clone();
}

const HIRType* SolverImpl::getTraitTyParam(HIRTypeInterner& types, unsigned index) const {
    const auto& params = getTraitParamsRef(types);
    return index < params.types.size() ? params.types[index] : nullptr;
}

const HIRType* SolverImpl::getType(HIRTypeInterner& types, const char* name, const HIRPathParams& params) const {
    if (!name[0]) {
        return nullptr;
    }
    if (!traitImpl) {
        const auto it = associated.find(name);
        return it != associated.end() && it->second.atyParams.equalsIgnoringRegions(params) ? it->second.type : nullptr;
    }

    Span sp;
    const auto it = traitImpl->types.find(name);
    if (it != traitImpl->types.end()) {
        return SolverImplTraitMonomorph(types, *traitImpl, implParams, params).monomorphType(sp, it->second.data);
    }
    ASSERT_BUG(sp, trait, StringView("trait impl solver response has no trait declaration"));
    const auto defaultIt = trait->types.find(name);
    if (defaultIt == trait->types.end() || !defaultIt->second.hasDefault) {
        return nullptr;
    }
    auto defaultType = MonomorphStatePtr(types, types.self(), &traitImpl->traitArgs, nullptr).monomorphType(sp, defaultIt->second.defaultValue);
    return SolverImplTraitMonomorph(types, *traitImpl, implParams, params).monomorphType(sp, defaultType);
}

bool SolverImpl::typeIsSpecialisable(const char* name) const {
    if (!traitImpl) {
        return false;
    }
    const auto it = traitImpl->types.find(name);
    return it != traitImpl->types.end() && it->second.isSpecialisable;
}

bool SolverImpl::moreSpecificThan(HIRTypeInterner& types, const SolverImpl& other) const {
    if (traitImpl) {
        return other.traitImpl && traitImpl->moreSpecificThan(types, *other.traitImpl);
    }
    if (other.traitImpl) {
        return false;
    }
    BUG_ASSERT(type == other.type);
    BUG_ASSERT(traitArgs == other.traitArgs);
    return associated.size() > other.associated.size();
}

const HIRType* SolverImpl::monomorphImplType(HIRTypeInterner& types, const Span& sp, const HIRType* type, const HIRPathParams& methodParams) const {
    ASSERT_BUG(sp, traitImpl, StringView("cannot monomorphise a trait-impl type through an environment response"));
    return SolverImplTraitMonomorph(types, *traitImpl, implParams, methodParams).monomorphType(sp, type);
}

HIRTraitPath SolverImpl::monomorphImplTraitPath(HIRTypeInterner& types, const Span& sp, const HIRTraitPath& traitPath, const HIRPathParams& methodParams) const {
    ASSERT_BUG(sp, traitImpl, StringView("cannot monomorphise a trait-impl bound through an environment response"));
    return SolverImplTraitMonomorph(types, *traitImpl, implParams, methodParams).monomorphTraitpath(sp, traitPath, true);
}

void HMTypeInferrence::checkForLoops() {
    struct LoopChecker {
        Vector<unsigned int>& indexes;

        void checkTy(const HMTypeInferrence& ivars, const HIRType* ty) {
            visitTyWith(ty, [&](const HIRType* t) {
                if (const auto* ep = t->opt_Infer()) {
                    const auto& e = *ep;
                    for (auto idx : indexes) {
                        ASSERT_BUG(Span(), e.index != idx, StringView("Recursion in ivar #") << indexes[0] << StringView(" ") << ivars.ivars[indexes[0]].type << StringView(" - loop with ") << idx << StringView(" ") << ivars.ivars[idx].type);
                    }
                    const auto& ivd = ivars.getPointedIvar(e.index);
                    BUG_ASSERT(!ivd.isAlias());
                    if (!ivd.type->is_Infer()) {
                        indexes.pushBack(e.index);
                        this->checkTy(ivars, ivd.type);
                        indexes.popBack();
                    }
                }
                return false;
            });
        }
    };

    Vector<unsigned int> indexes;
    unsigned int i = 0;
    for (const auto& v : ivars) {
        if (!v.isAlias() && !v.type->is_Infer()) {
            DEBUG(StringView("- ") << i << StringView(" ") << v.type);
            indexes.clear();
            indexes.pushBack(i);
            (LoopChecker{indexes}).checkTy(*this, v.type);
        }
        i++;
    }
}

void HMTypeInferrence::compactIvars() {
    ASSERT_BUG(Span(), snapshotDepth == 0, StringView("ivar compaction during an active inference snapshot"));
    this->checkForLoops();

    unsigned int i = 0;
    for (auto& v : ivars) {
        if (!v.isAlias()) {
            auto old = v.type;
            v.type = this->expandIvars(v.type);
            DEBUG(StringView("- ") << i << StringView(" ") << old << StringView(" -> ") << v.type);
        } else {
            auto index = v.alias;
            unsigned int count = 0;
            BUG_ASSERT(index < ivars.size());
            while (ivars.at(index).isAlias()) {
                index = ivars.at(index).alias;

                if (count >= ivars.size()) {
                    BUG(Span(), StringView("Loop detected in ivar list when starting at ") << v.alias << StringView(", current is ") << index);
                }
                count++;
            }
            v.alias = index;
        }
        i++;
    }
}

bool HMTypeInferrence::applyDefault(unsigned int index) {
    auto& v = ivars.at(index);
    if (v.isAlias()) {
        return false;
    }
    const auto* e = v.type->opt_Infer();
    if (!e) {
        return false;
    }
    switch (e->tyClass) {
        case HIRInferClass::None:
            return false;
        case HIRInferClass::Integer:
            DEBUG(StringView("- IVar ") << e->index << StringView(" = i32"));
            this->journalMutation(JournalEntry::Kind::TypeSet, index, v.type);
            v.type = types.primitive(HIRCoreType::I32);
            return true;
        case HIRInferClass::Float:
            DEBUG(StringView("- IVar ") << e->index << StringView(" = f64"));
            this->journalMutation(JournalEntry::Kind::TypeSet, index, v.type);
            v.type = types.primitive(HIRCoreType::F64);
            return true;
    }
    return false;
}

void HMTypeInferrence::printType(ZeroCopyOutput& os, const HIRType* tr, LList<const HIRType*> outerStack) const {
    const auto& ty = this->getType(tr);
    for (const auto* pty : outerStack) {
        if (pty) {
            if (pty == ty) {
                os << StringView("/*RECURSE*/");
                return;
            }
        }
    }
    auto stack = LList<const HIRType*>(&outerStack, ty);

    auto printTraitpath = [&](const HIRTraitPath& tp) {
        this->printGenericpath(os, tp.path, stack);
        // TODO: ATYs?
    };
    auto printPath = [&](const HIRPath& path) {
        switch (path.data.tag()) {
            case HIRPathData::TAG_Generic: {
                auto& pe = path.data.as_Generic();
                this->printGenericpath(os, pe, stack);
                break;
            }
            case HIRPathData::TAG_UfcsKnown: {
                auto& pe = path.data.as_UfcsKnown();
                os << StringView("<");
                this->printType(os, pe.type, stack);
                os << StringView(" as ");
                this->printGenericpath(os, pe.trait, stack);
                os << StringView(">::") << pe.item;
                this->printPathparams(os, pe.params, stack);
                break;
            }
            case HIRPathData::TAG_UfcsInherent: {
                auto& pe = path.data.as_UfcsInherent();
                os << StringView("<");
                this->printType(os, pe.type, stack);
                os << StringView(">::") << pe.item;
                this->printPathparams(os, pe.params, stack);
                break;
            }
            case HIRPathData::TAG_UfcsUnknown: {
                BUG(Span(), StringView("UfcsUnknown"));
                break;
            }
        }
    };

    switch ((*ty).tag()) {
        case HIRType::TAG_Infer: {
            os << ty;
            break;
        }
        case HIRType::TAG_Primitive: {
            os << ty;
            break;
        }
        case HIRType::TAG_Diverge: {
            os << ty;
            break;
        }
        case HIRType::TAG_Generic: {
            os << ty;
            break;
        }
        case HIRType::TAG_Path: {
            auto& e = (*ty).as_Path();
            printPath(e.path);
            break;
        }
        case HIRType::TAG_Borrow: {
            auto& e = (*ty).as_Borrow();
            os << StringView("&");
            switch (e.type) {
                case HIRBorrowType::Shared:
                    os << StringView("");
                    break;
                case HIRBorrowType::Unique:
                    os << StringView("mut ");
                    break;
                case HIRBorrowType::Owned:
                    os << StringView("move ");
                    break;
            }
            this->printType(os, e.inner, stack);
            break;
        }
        case HIRType::TAG_Pointer: {
            auto& e = (*ty).as_Pointer();
            switch (e.type) {
                case HIRBorrowType::Shared:
                    os << StringView("*const ");
                    break;
                case HIRBorrowType::Unique:
                    os << StringView("*mut ");
                    break;
                case HIRBorrowType::Owned:
                    os << StringView("*move ");
                    break;
            }
            this->printType(os, e.inner, stack);
            break;
        }
        case HIRType::TAG_Slice: {
            auto& e = (*ty).as_Slice();
            os << StringView("[");
            this->printType(os, e.inner, stack);
            os << StringView("]");
            break;
        }
        case HIRType::TAG_Array: {
            auto& e = (*ty).as_Array();
            os << StringView("[");
            this->printType(os, e.inner, stack);
            os << StringView("; ") << e.size << StringView("]");
            break;
        }
        case HIRType::TAG_Pattern: {
            auto& e = (*ty).as_Pattern();
            this->printType(os, e.inner, stack);
            os << StringView(" is ");
            e.pattern.fmt(os);
            break;
        }
        case HIRType::TAG_NodeType: {
            auto& e = (*ty).as_NodeType();
            e.fmt(os);
            switch (e.tag()) {
                case HIRTypeDataNodeType::TAG_Closure: {
                    auto& nodeP = e.as_Closure();
                    os << StringView("(");
                    for (const auto& arg : nodeP->args) {
                        this->printType(os, arg.second, stack);
                        os << StringView(",");
                    }
                    os << StringView(")->");
                    this->printType(os, nodeP->returnType, stack);
                    break;
                }
                case HIRTypeDataNodeType::TAG_Generator: {
                    break;
                }
                case HIRTypeDataNodeType::TAG_Async: {
                    break;
                }
            }
            break;
        }
        case HIRType::TAG_NamedFunction: {
            auto& e = (*ty).as_NamedFunction();
            os << StringView("fn{");
            printPath(e.path);
            os << StringView("}");
            break;
        }
        case HIRType::TAG_Function: {
            auto& e = (*ty).as_Function();
            if (e.isUnsafe) {
                os << StringView("unsafe ");
            }
            if (e.abi != "") {
                os << StringView("extern \"") << e.abi << StringView("\" ");
            }
            os << StringView("fn(");
            for (const auto& arg : e.argTypes) {
                this->printType(os, arg, stack);
                os << StringView(",");
            }
            os << StringView(")->");
            this->printType(os, e.rettype, stack);
            break;
        }
        case HIRType::TAG_TraitObject: {
            auto& e = (*ty).as_TraitObject();
            os << StringView("dyn (");
            printTraitpath(e.trait);
            for (const auto& marker : e.markers) {
                os << StringView("+");
                this->printGenericpath(os, marker, stack);
            }
            os << StringView(")");
            break;
        }
        case HIRType::TAG_ErasedType: {
            auto& e = (*ty).as_ErasedType();
            os << StringView("impl ");
            for (const auto& tr : e.traits) {
                if (&tr != &e.traits[0]) {
                    os << StringView("+");
                }
                printTraitpath(tr);
            }
            os << StringView("+use");
            this->printPathparams(os, e.use, outerStack);
            os << StringView("/*");
            switch (e.inner.tag()) {
                case TypeDataErasedTypeInner::TAG_Fcn: {
                    auto& ee = e.inner.as_Fcn();
                    os << StringView("fn ");
                    printPath(ee.origin);
                    os << StringView("#") << ee.index;
                    break;
                }
                case TypeDataErasedTypeInner::TAG_Known: {
                    auto& ee = e.inner.as_Known();
                    printType(os, ee, stack);
                    break;
                }
                case TypeDataErasedTypeInner::TAG_Alias: {
                    break;
                }
            }
            os << StringView("*/");
            break;
        }
        case HIRType::TAG_Tuple: {
            auto& e = (*ty).as_Tuple();
            os << StringView("(");
            for (const auto& st : e) {
                this->printType(os, st, stack);
                os << StringView(",");
            }
            os << StringView(")");
            break;
        }
    }
}

void HMTypeInferrence::printGenericpath(ZeroCopyOutput& os, const HIRGenericPath& gp, LList<const HIRType*> stack) const {
    os << gp.path;
    this->printPathparams(os, gp.params, stack);
}

void HMTypeInferrence::printPathparams(ZeroCopyOutput& os, const HIRPathParams& pps, LList<const HIRType*> stack) const {
    if (pps.hasParams()) {
        os << StringView("<");
        for (const auto& ppT : pps.types) {
            this->printType(os, ppT, stack);
            os << StringView(",");
        }
        for (const auto& ppV : pps.values) {
            os << ppV;
            os << StringView(",");
        }
        os << StringView(">");
    }
}

const HIRType* HMTypeInferrence::expandIvars(const HIRType* type) {
    if (!type->hasTypeInfer()) {
        return type;
    }
    if (std::find(expandStack.begin(), expandStack.end(), type) != expandStack.end()) {
        return type;
    }
    expandStack.pushBack(type);

    STD_DEFER {
        expandStack.popBack();
    };

    if (type->is_Infer()) {
        const auto& resolved = this->getType(type);
        if (resolved != type) {
            type = resolved;
        }
        return type;
    }

    auto data = type->cloneData();

    struct H {
        static void expandIvarsPath(/*const*/ HMTypeInferrence& self, HIRPath& path) {
            switch (path.data.tag()) {
                case HIRPath::Data::TAG_Generic: {
                    auto& e2 = path.data.as_Generic();
                    self.expandIvarsParams(e2.params);
                    break;
                }
                case HIRPath::Data::TAG_UfcsKnown: {
                    auto& e2 = path.data.as_UfcsKnown();
                    e2.type = self.expandIvars(e2.type);
                    self.expandIvarsParams(e2.trait.params);
                    self.expandIvarsParams(e2.params);
                    break;
                }
                case HIRPath::Data::TAG_UfcsUnknown: {
                    auto& e2 = path.data.as_UfcsUnknown();
                    e2.type = self.expandIvars(e2.type);
                    self.expandIvarsParams(e2.params);
                    break;
                }
                case HIRPath::Data::TAG_UfcsInherent: {
                    auto& e2 = path.data.as_UfcsInherent();
                    e2.type = self.expandIvars(e2.type);
                    self.expandIvarsParams(e2.params);
                    break;
                }
            }
        }
    };

    switch (data.tag()) {
        case HIRType::TAG_Infer: {
            break;
        }
        case HIRType::TAG_Diverge: {
            break;
        }
        case HIRType::TAG_Primitive: {
            break;
        }
        case HIRType::TAG_Path: {
            auto& e = data.as_Path();
            H::expandIvarsPath(*this, e.path);
            break;
        }
        case HIRType::TAG_Generic: {
            break;
        }
        case HIRType::TAG_TraitObject: {
            auto& e = data.as_TraitObject();
            this->expandIvarsTraitPath(e.trait);
            for (auto& marker : e.markers) {
                this->expandIvarsParams(marker.params);
            }
            break;
        }
        case HIRType::TAG_ErasedType: {
            auto& e = data.as_ErasedType();
            switch (e.inner.tag()) {
                case TypeDataErasedTypeInner::TAG_Fcn: {
                    auto& ee = e.inner.as_Fcn();
                    H::expandIvarsPath(*this, ee.origin);
                    break;
                }
                case TypeDataErasedTypeInner::TAG_Known: {
                    auto& ee = e.inner.as_Known();
                    ee = this->expandIvars(ee);
                    break;
                }
                case TypeDataErasedTypeInner::TAG_Alias: {
                    break;
                }
            }
            for (auto& trait : e.traits) {
                this->expandIvarsParams(trait.path.params);
                // TODO: Associated types
            }
            break;
        }
        case HIRType::TAG_Array: {
            auto& e = data.as_Array();
            e.inner = this->expandIvars(e.inner);
            break;
        }
        case HIRType::TAG_Slice: {
            auto& e = data.as_Slice();
            e.inner = this->expandIvars(e.inner);
            break;
        }
        case HIRType::TAG_Pattern: {
            auto& e = data.as_Pattern();
            e.inner = this->expandIvars(e.inner);
            for (auto& range : e.pattern.alternatives) {
                HIRConstGeneric* values[] = {range.hasStart ? &range.start : nullptr, range.hasEnd ? &range.end : nullptr};
                for (auto* value : values) {
                    if (value && value->is_Infer()) {
                        const auto& resolved = this->getValue(*value);
                        if (!resolved.is_Infer()) {
                            *value = resolved.clone();
                        }
                    }
                }
            }
            break;
        }
        case HIRType::TAG_Tuple: {
            auto& e = data.as_Tuple();
            for (auto& type : mutRange(e)) {
                type = this->expandIvars(type);
            }
            break;
        }
        case HIRType::TAG_Borrow: {
            auto& e = data.as_Borrow();
            e.inner = this->expandIvars(e.inner);
            break;
        }
        case HIRType::TAG_Pointer: {
            auto& e = data.as_Pointer();
            e.inner = this->expandIvars(e.inner);
            break;
        }
        case HIRType::TAG_NamedFunction: {
            auto& e = data.as_NamedFunction();
            H::expandIvarsPath(*this, e.path);
            break;
        }
        case HIRType::TAG_Function: {
            auto& e = data.as_Function();
            e.rettype = this->expandIvars(e.rettype);
            for (auto& type : mutRange(e.argTypes)) {
                type = this->expandIvars(type);
            }
            break;
        }
        case HIRType::TAG_NodeType: {
            break;
        }
    }
    return types.intern(std::move(data));
}

void HMTypeInferrence::expandIvarsParams(HIRPathParams& params) {
    for (auto& arg : params.types) {
        arg = expandIvars(arg);
    }
    for (auto& value : params.values) {
        expandIvars(value);
    }
}

void HMTypeInferrence::expandIvars(HIRConstGeneric& value) {
    if (value.is_Infer()) {
        const auto& resolved = getValue(value);
        if (resolved != value) {
            value = resolved.clone();
            expandIvars(value);
        }
        return;
    }

    if (auto* unevaluated = value.opt_Unevaluated()) {
        if ((*unevaluated)->selfType) {
            (*unevaluated)->selfType = expandIvars((*unevaluated)->selfType);
        }
        expandIvarsParams((*unevaluated)->paramsImpl);
        expandIvarsParams((*unevaluated)->paramsItem);
    }
}

void HMTypeInferrence::expandIvarsTraitPath(HIRTraitPath& path) {
    expandIvarsParams(path.path.params);
    for (auto& bound : path.typeBounds) {
        expandIvarsParams(bound.second.sourceTrait.params);
        expandIvarsParams(bound.second.atyParams);
        bound.second.type = expandIvars(bound.second.type);
    }
    for (auto& bound : path.traitBounds) {
        expandIvarsParams(bound.second.sourceTrait.params);
        expandIvarsParams(bound.second.atyParams);
        for (auto& trait : bound.second.traits) {
            expandIvarsTraitPath(trait);
        }
    }
}

const HIRType* HMTypeInferrence::addIvars(const HIRType* type) {
    if (const auto* infer = type->opt_Infer()) {
        if (infer->index == ~0u) {
            type = newIvarTr(infer->tyClass);
            this->markChange();
            DEBUG(StringView("New ivar ") << type);
            return type;
        }
        if (isAliasInputInfer(infer->index)) {
            auto* mapped = aliasTypeIvars.find(infer->index);
            if (!mapped) {
                aliasTypeIvars.insert(infer->index, newIvarTr(infer->tyClass));
                this->journalMutation(JournalEntry::Kind::AliasTypeMap, infer->index, nullptr);
                mapped = aliasTypeIvars.find(infer->index);
            }
            type = *mapped;
            this->markChange();
            return type;
        }
    }

    auto data = type->cloneData();
    switch (data.tag()) {
        case HIRType::TAG_Infer: {
            break;
        }
        case HIRType::TAG_Diverge: {
            break;
        }
        case HIRType::TAG_Primitive: {
            break;
        }
        case HIRType::TAG_Path: {
            auto& e = data.as_Path();
            switch (e.path.data.tag()) {
                case HIRPath::Data::TAG_Generic: {
                    auto& e2 = e.path.data.as_Generic();
                    this->addIvarsParams(e2.params);
                    break;
                }
                case HIRPath::Data::TAG_UfcsKnown: {
                    auto& e2 = e.path.data.as_UfcsKnown();
                    e2.type = this->addIvars(e2.type);
                    this->addIvarsParams(e2.trait.params);
                    this->addIvarsParams(e2.params);
                    break;
                }
                case HIRPath::Data::TAG_UfcsUnknown: {
                    auto& e2 = e.path.data.as_UfcsUnknown();
                    e2.type = this->addIvars(e2.type);
                    this->addIvarsParams(e2.params);
                    break;
                }
                case HIRPath::Data::TAG_UfcsInherent: {
                    auto& e2 = e.path.data.as_UfcsInherent();
                    e2.type = this->addIvars(e2.type);
                    this->addIvarsParams(e2.params);
                    break;
                }
            }
            break;
        }
        case HIRType::TAG_Generic: {
            break;
        }
        case HIRType::TAG_TraitObject: {
            auto& e = data.as_TraitObject();
            this->addIvarsTraitPath(e.trait);
            for (auto& marker : e.markers) {
                this->addIvarsParams(marker.params);
            }
            break;
        }
        case HIRType::TAG_ErasedType: {
            if (typeContainsIvars(type, /*only_unbound=*/true)) {
                BUG(Span(), StringView("ErasedType getting ivars added - ") << type);
            }
            auto& e = data.as_ErasedType();
            if (auto* alias = e.inner.opt_Alias()) {
                addIvarsParams(alias->params);
                for (auto& trait : e.traits) {
                    addIvarsTraitPath(trait);
                }
                addIvarsParams(e.use);
            }
            break;
        }
        case HIRType::TAG_Array: {
            auto& e = data.as_Array();
            e.inner = addIvars(e.inner);
            if (e.size.is_Unevaluated()) {
                addIvars(e.size.as_Unevaluated());
            }
            break;
        }
        case HIRType::TAG_Slice: {
            auto& e = data.as_Slice();
            e.inner = addIvars(e.inner);
            break;
        }
        case HIRType::TAG_Pattern: {
            auto& e = data.as_Pattern();
            e.inner = addIvars(e.inner);
            for (auto& range : e.pattern.alternatives) {
                if (range.hasStart) {
                    addIvars(range.start);
                }
                if (range.hasEnd) {
                    addIvars(range.end);
                }
            }
            break;
        }
        case HIRType::TAG_Tuple: {
            auto& e = data.as_Tuple();
            for (auto& type : mutRange(e)) {
                type = addIvars(type);
            }
            break;
        }
        case HIRType::TAG_Borrow: {
            auto& e = data.as_Borrow();
            e.inner = addIvars(e.inner);
            break;
        }
        case HIRType::TAG_Pointer: {
            auto& e = data.as_Pointer();
            e.inner = addIvars(e.inner);
            break;
        }
        case HIRType::TAG_NamedFunction: {
            break;
        }
        case HIRType::TAG_Function: {
            auto& e = data.as_Function();
            e.rettype = addIvars(e.rettype);
            for (auto& type : mutRange(e.argTypes)) {
                type = addIvars(type);
            }
            break;
        }
        case HIRType::TAG_NodeType: {
            break;
        }
    }
    return types.intern(std::move(data));
}

void HMTypeInferrence::addIvars(HIRConstGeneric& val) {
    if (val.is_Infer()) {
        if (val.as_Infer().index == ~0u) {
            val.as_Infer().index = newIvarVal();
            this->markChange();
            DEBUG(StringView("New ivar ") << val);
        } else if (isAliasInputInfer(val.as_Infer().index)) {
            auto* mapped = aliasValueIvars.find(val.as_Infer().index);
            if (!mapped) {
                aliasValueIvars.insert(val.as_Infer().index, HIRConstGeneric::make_Infer({newIvarVal()}));
                this->journalMutation(JournalEntry::Kind::AliasValueMap, val.as_Infer().index, nullptr);
                mapped = aliasValueIvars.find(val.as_Infer().index);
            }
            val = mapped->clone();
            this->markChange();
        }
    }
}

void HMTypeInferrence::addIvarsParams(HIRPathParams& params) {
    for (auto& arg : params.types) {
        arg = addIvars(arg);
    }
    for (auto& arg : params.values) {
        addIvars(arg);
    }
}

void HMTypeInferrence::addIvarsTraitPath(HIRTraitPath& path) {
    Span sp;
    auto originalParams = path.path.params.clone();
    addIvarsParams(path.path.params);

    auto populateSourceTrait = [&](HIRGenericPath& sourceTrait) {
        if (sourceTrait.path == path.path.path && sourceTrait.params == originalParams) {
            sourceTrait.params = path.path.params.clone();
            return;
        }
        if (path.traitPtr) {
            auto self = types.self();
            for (const auto& parent : path.traitPtr->allParentTraits) {
                auto original = MonomorphStatePtr(types, self, &originalParams, nullptr).monomorphGenericpath(sp, parent.path);
                if (original == sourceTrait) {
                    sourceTrait = MonomorphStatePtr(types, self, &path.path.params, nullptr).monomorphGenericpath(sp, parent.path);
                    return;
                }
            }
        }
        addIvarsParams(sourceTrait.params);
    };

    for (auto& bound : path.typeBounds) {
        populateSourceTrait(bound.second.sourceTrait);
        addIvarsParams(bound.second.atyParams);
        addIvars(bound.second.type);
    }
    for (auto& bound : path.traitBounds) {
        populateSourceTrait(bound.second.sourceTrait);
        addIvarsParams(bound.second.atyParams);
        for (auto& trait : bound.second.traits) {
            addIvarsTraitPath(trait);
        }
    }
}

unsigned int HMTypeInferrence::newIvar(HIRInferClass ic /* = HIR::InferClass::None*/) {
    auto rv = ivars.size();
    ivars.emplace_back(types.infer(rv, ic));
    mutationGeneration = ++generationCounter;
    DEBUG(StringView("New type IVar ") << rv);
    return rv;
}

const HIRType* HMTypeInferrence::newIvarTr(HIRInferClass ic /* = HIR::InferClass::None*/) {
    return ivars.at(this->newIvar(ic)).type;
}

unsigned int HMTypeInferrence::newIvarVal() {
    values.push_back(IVarValue());
    values.back().val->as_Infer().index = values.size() - 1;
    mutationGeneration = ++generationCounter;
    return values.size() - 1;
}

void HMTypeInferrence::setIvarValTo(unsigned int slot, HIRConstGeneric val) {
    ASSERT_BUG(Span(), slot < values.size(), StringView("slot ") << slot << StringView(" >= ") << values.size());
    ASSERT_BUG(Span(), !values[slot].isAlias(), StringView("slot ") << slot);
    if (*values[slot].val == val) {
    } else {
        DEBUG(StringView("Set ValIVar ") << slot << StringView(" = ") << val);
        ASSERT_BUG(Span(), values[slot].val->is_Infer(), StringView("slot ") << slot << StringView(" - ") << *values[slot].val);
        ASSERT_BUG(Span(), values[slot].val->as_Infer().index == slot, StringView("slot ") << slot << StringView(" - ") << *values[slot].val);
        this->journalMutation(JournalEntry::Kind::ValSet, slot, nullptr);
        *values[slot].val = std::move(val);
        this->markChange();
    }
}

void HMTypeInferrence::ivarValUnify(unsigned int leftSlot, unsigned int rightSlot) {
    Span sp;
    ASSERT_BUG(sp, leftSlot < values.size(), StringView("slot ") << leftSlot << StringView(" >= ") << values.size());
    ASSERT_BUG(sp, rightSlot < values.size(), StringView("slot ") << leftSlot << StringView(" >= ") << values.size());
    ASSERT_BUG(sp, !values[leftSlot].isAlias(), StringView("slot ") << leftSlot);
    ASSERT_BUG(sp, !values[rightSlot].isAlias(), StringView("slot ") << rightSlot);

    if (leftSlot == rightSlot) {
        return;
    }

    if (/*const auto* re =*/values[rightSlot].val->opt_Infer()) {
        DEBUG(StringView("Set ValIVar ") << rightSlot << StringView(" = @") << leftSlot);
        values[rightSlot].alias = leftSlot;
        if (snapshotDepth != 0) {
            this->journalMutation(JournalEntry::Kind::ValAlias, rightSlot, nullptr);
        } else {
            values[rightSlot].val.reset();
        }

        this->markChange();
    } else {
        BUG(sp, StringView("Unifiying over a set value"));
    }
}

const HIRType* HMTypeInferrence::getType(const HIRType* type) const {
    const auto* current = &type;
    for (size_t count = 0; count <= ivars.size(); count++) {
        const auto* e = (*current)->opt_Infer();
        if (!e) {
            return *current;
        }
        ASSERT_BUG(Span(), e->index != ~0u, StringView("Encountered non-populated IVar"));
        if (isAliasInputInfer(e->index)) {
            return *current;
        }

        const auto* next = &getPointedIvar(e->index).type;
        if (*next == *current) {
            return *current;
        }
        current = next;
    }
    BUG(Span(), StringView("Loop detected while resolving type ") << type);
}

const HIRType* HMTypeInferrence::getType(unsigned idx) const {
    BUG_ASSERT(idx != ~0u);
    const auto* current = &getPointedIvar(idx).type;
    for (size_t count = 0; count <= ivars.size(); count++) {
        const auto* e = (*current)->opt_Infer();
        if (!e) {
            return *current;
        }
        const auto* next = &getPointedIvar(e->index).type;
        if (*next == *current) {
            return *current;
        }
        current = next;
    }
    BUG(Span(), StringView("Loop detected while resolving type ivar ") << idx);
}

void HMTypeInferrence::setIvarTo(unsigned int slot, const HIRType* type, bool solverProven) {
    auto sp = Span();
    const auto rootIndex = this->rootIvarIndex(slot);
    auto& rootIvar = ivars.at(rootIndex);

    DEBUG(StringView("set_ivar_to(") << slot << StringView(" { ") << rootIvar.type << StringView(" }, ") << type << StringView(")"));
    if (const auto* lE = type->opt_Infer(); lE && !isAliasInputInfer(lE->index)) {
        BUG_ASSERT(lE->index != slot);
        if (lE->tyClass != HIRInferClass::None) {
            switch ((*rootIvar.type).tag()) {
                case HIRType::TAG_Primitive: {
                    auto& e = (*rootIvar.type).as_Primitive();
                    checkTypeClassPrimitive(sp, type, lE->tyClass, e);
                    break;
                }
                case HIRType::TAG_Pattern: {
                    auto& e = (*rootIvar.type).as_Pattern();
                    const auto* primitive = e.inner->opt_Primitive();
                    if (!primitive) {
                        ERROR(sp, E0000, StringView("Type unificiation of literal with invalid pattern type - ") << rootIvar.type);
                    }
                    checkTypeClassPrimitive(sp, type, lE->tyClass, *primitive);
                    break;
                }
                case HIRType::TAG_Infer: {
                    auto& e = (*rootIvar.type).as_Infer();
                    if (e.tyClass != HIRInferClass::None && e.tyClass != lE->tyClass) {
                        ERROR(sp, E0000, StringView("Unifying types with mismatching literal classes - ") << type << StringView(" := ") << rootIvar.type);
                    }
                    break;
                }
                default: {
                    ERROR(sp, E0000, StringView("Type unificiation of literal with invalid type - ") << rootIvar.type);
                    break;
                }
            }
        }

        DEBUG(StringView("Set IVar ") << lE->index << StringView(" = @") << slot);
        const auto rightIndex = this->rootIvarIndex(lE->index);
        auto& rIvar = ivars.at(rightIndex);
        this->journalMutation(JournalEntry::Kind::TypeAlias, rightIndex, rIvar.type);
        rIvar.alias = slot;
        rIvar.type = nullptr;
    } else if (rootIvar.type == type) {
        return;
    } else {
        // TODO: Avoid needing to clone in all cases?
        struct MonomorphAddLifetimes: public Monomorphiser {
            explicit MonomorphAddLifetimes(HIRTypeInterner& types)
                : Monomorphiser(types)
            {
            }

            const HIRType* getType(const Span& sp, const HIRGenericRef& g) const override {
                return types.generic(g.name, g.binding);
            }

            HIRConstGeneric getValue(const Span& sp, const HIRGenericRef& g) const override {
                return g;
            }
        };

        type = MonomorphAddLifetimes(types).monomorphType(sp, type, true);

        DEBUG(StringView("Set IVar ") << slot << StringView(" = ") << type);
        if (const auto* e = rootIvar.type->opt_Infer()) {
            switch (e->tyClass) {
                case HIRInferClass::None:
                    break;
                case HIRInferClass::Integer:
                case HIRInferClass::Float:
                    if (const auto* lE = type->opt_Primitive()) {
                        checkTypeClassPrimitive(sp, type, e->tyClass, *lE);
                    } else if (const auto* pattern = type->opt_Pattern()) {
                        const auto* primitive = pattern->inner->opt_Primitive();
                        if (!primitive) {
                            BUG(sp, StringView("Setting primitive to ") << type);
                        }
                        checkTypeClassPrimitive(sp, type, e->tyClass, *primitive);
                    } else if (type->is_Diverge()) {
                    } else if (solverProven) {
                        const auto* path = type->opt_Path();
                        ASSERT_BUG(sp, path && path->path.data.is_UfcsKnown(), StringView("Solver-proven literal fallback target is not a projection: ") << type);
                    } else {
                        BUG(sp, StringView("Setting primitive to ") << type);
                    }
                    break;
            }
        } else {
            BUG(sp, StringView("Overwriting ivar ") << slot << StringView(" (") << rootIvar.type << StringView(") with ") << type);
        }

        this->journalMutation(JournalEntry::Kind::TypeSet, rootIndex, rootIvar.type);
        rootIvar.type = type;
    }

    this->markChange();
}

void HMTypeInferrence::ivarUnify(unsigned int leftSlot, unsigned int rightSlot) {
    auto sp = Span();
    if (leftSlot != rightSlot) {
        const auto leftIndex = this->rootIvarIndex(leftSlot);
        auto& leftIvar = ivars.at(leftIndex);

        // TODO: Assert that setting this won't cause a loop.
        const auto rightIndex = this->rootIvarIndex(rightSlot);
        auto& rootIvar = ivars.at(rightIndex);

        if (const auto* re = rootIvar.type->opt_Infer()) {
            DEBUG(StringView("Class unify ") << leftIvar.type << StringView(" <- ") << rootIvar.type);
            if (re->tyClass != HIRInferClass::None) {
                if (const auto* le = leftIvar.type->opt_Infer()) {
                    if (le->tyClass != HIRInferClass::None && le->tyClass != re->tyClass) {
                        ERROR(sp, E0000, StringView("Unifying types with mismatching literal classes - ") << leftIvar.type << StringView(" := ") << rootIvar.type);
                    }
                    if (le->tyClass == HIRInferClass::None) {
                        this->journalMutation(JournalEntry::Kind::TypeSet, leftIndex, leftIvar.type);
                        leftIvar.type = types.infer(le->index, re->tyClass);
                    }
                } else if (const auto* le = leftIvar.type->opt_Primitive()) {
                    checkTypeClassPrimitive(sp, leftIvar.type, re->tyClass, *le);
                } else if (const auto* pattern = leftIvar.type->opt_Pattern()) {
                    const auto* primitive = pattern->inner->opt_Primitive();
                    if (!primitive) {
                        ERROR(sp, E0000, StringView("Type unificiation of literal with invalid pattern type - ") << leftIvar.type);
                    }
                    checkTypeClassPrimitive(sp, leftIvar.type, re->tyClass, *primitive);
                } else {
                    ERROR(sp, E0000, StringView("Type unificiation of literal with invalid type - ") << leftIvar.type);
                }
            } else {
            }
        } else {
            BUG(sp, StringView("Unifying over a concrete type - ") << rootIvar.type);
        }

        DEBUG(StringView("IVar ") << rootIvar.type->as_Infer().index << StringView(" = @") << leftSlot);
        this->journalMutation(JournalEntry::Kind::TypeAlias, rightIndex, rootIvar.type);
        rootIvar.alias = leftSlot;
        rootIvar.type = nullptr;

        this->markChange();
    }
}

const HIRConstGeneric& HMTypeInferrence::getValue(const HIRConstGeneric& val) const {
    if (val.is_Infer()) {
        if (isAliasInputInfer(val.as_Infer().index)) {
            return val;
        }
        return getValue(val.as_Infer().index);
    } else {
        return val;
    }
}

const HIRConstGeneric& HMTypeInferrence::getValue(unsigned slot) const {
    ASSERT_BUG(Span(), slot != ~0u, StringView("HMTypeInferrence::get_value: Value generic ivar index not assigned"));
    auto index = slot;
    for (unsigned int count = 0; count < values.size(); count++) {
        ASSERT_BUG(Span(), index < values.size(), StringView(""));
        auto& ent = values[index];
        if (!ent.isAlias()) {
            return *ent.val;
        }
        index = ent.alias;
    }
    BUG(Span(), StringView("Loop detected in ivar list when starting at ") << slot << StringView(", current is ") << index);
}

unsigned int HMTypeInferrence::rootIvarIndex(unsigned int slot) const {
    auto index = slot;
    unsigned int count = 0;
    BUG_ASSERT(index < ivars.size());
    while (ivars.at(index).isAlias()) {
        index = ivars.at(index).alias;

        if (count >= ivars.size()) {
            BUG(Span(), StringView("Loop detected in ivar list when starting at ") << slot << StringView(", current is ") << index);
        }
        count++;
    }
    return index;
}

HMTypeInferrence::IVar& HMTypeInferrence::getPointedIvar(unsigned int slot) const {
    return const_cast<IVar&>(ivars.at(this->rootIvarIndex(slot)));
}

bool HMTypeInferrence::pathparamsContainIvars(const HIRPathParams& pps, bool onlyUnbound) const {
    for (const auto& ty : pps.types) {
        if (this->typeContainsIvars(ty, onlyUnbound)) {
            return true;
        }
    }
    return false;
}

bool HMTypeInferrence::typeContainsIvars(const HIRType* ty, bool onlyUnbound) const {
    if (!ty->hasTypeInfer()) {
        return false;
    }
    TRACE_FUNCTION_F(StringView("ty = ") << ty);
    auto pathContainsIvars = [this](const HIRPath& path, bool onlyUnbound) {
        switch (path.data.tag()) {
            case HIRPath::Data::TAG_Generic: {
                auto& pe = path.data.as_Generic();
                return this->pathparamsContainIvars(pe.params, onlyUnbound);
            }
            case HIRPath::Data::TAG_UfcsKnown: {
                auto& pe = path.data.as_UfcsKnown();
                if (this->typeContainsIvars(pe.type, onlyUnbound)) {
                    return true;
                }
                if (this->pathparamsContainIvars(pe.trait.params, onlyUnbound)) {
                    return true;
                }
                return this->pathparamsContainIvars(pe.params, onlyUnbound);
                break;
            }
            case HIRPath::Data::TAG_UfcsInherent: {
                auto& pe = path.data.as_UfcsInherent();
                if (this->typeContainsIvars(pe.type, onlyUnbound)) {
                    return true;
                }
                return this->pathparamsContainIvars(pe.params, onlyUnbound);
                break;
            }
            case HIRPath::Data::TAG_UfcsUnknown: {
                BUG(Span(), StringView("UfcsUnknown"));
                break;
            }
        }
        UNREACHABLE();
    };
    switch ((*ty).tag()) {
        case HIRType::TAG_Infer: {
            auto& e = (*ty).as_Infer();
            if (onlyUnbound) {
                return e.index == ~0u;
            }
            return true;
        }
        case HIRType::TAG_Primitive: {
            return false;
        }
        case HIRType::TAG_Diverge: {
            return false;
        }
        case HIRType::TAG_Generic: {
            return false;
        }
        case HIRType::TAG_Path: {
            auto& e = (*ty).as_Path();
            return pathContainsIvars(e.path, onlyUnbound);
        }
        case HIRType::TAG_Borrow: {
            auto& e = (*ty).as_Borrow();
            return typeContainsIvars(e.inner, onlyUnbound);
        }
        case HIRType::TAG_Pointer: {
            auto& e = (*ty).as_Pointer();
            return typeContainsIvars(e.inner, onlyUnbound);
        }
        case HIRType::TAG_Slice: {
            auto& e = (*ty).as_Slice();
            return typeContainsIvars(e.inner, onlyUnbound);
        }
        case HIRType::TAG_Pattern: {
            auto& e = (*ty).as_Pattern();
            return typeContainsIvars(e.inner, onlyUnbound);
        }
        case HIRType::TAG_Array: {
            auto& e = (*ty).as_Array();
            return typeContainsIvars(e.inner, onlyUnbound);
        }
        case HIRType::TAG_NodeType: {
            return false;
        }
        case HIRType::TAG_NamedFunction: {
            auto& e = (*ty).as_NamedFunction();
            return pathContainsIvars(e.path, onlyUnbound);
        }
        case HIRType::TAG_Function: {
            auto& e = (*ty).as_Function();
            for (const auto& arg : e.argTypes) {
                if (typeContainsIvars(arg, onlyUnbound)) {
                    return true;
                }
            }
            return typeContainsIvars(e.rettype, onlyUnbound);
        }
        case HIRType::TAG_TraitObject: {
            auto& e = (*ty).as_TraitObject();
            for (const auto& marker : e.markers) {
                if (pathparamsContainIvars(marker.params, onlyUnbound)) {
                    return true;
                }
            }
            return pathparamsContainIvars(e.trait.path.params, onlyUnbound);
        }
        case HIRType::TAG_ErasedType: {
            auto& e = (*ty).as_ErasedType();
            switch (e.inner.tag()) {
                case TypeDataErasedTypeInner::TAG_Fcn: {
                    auto& ee = e.inner.as_Fcn();
                    return pathContainsIvars(ee.origin, onlyUnbound);
                }
                case TypeDataErasedTypeInner::TAG_Known: {
                    auto& ee = e.inner.as_Known();
                    return typeContainsIvars(ee, onlyUnbound);
                }
                case TypeDataErasedTypeInner::TAG_Alias: {
                    return false;
                }
            }
            break;
        }
        case HIRType::TAG_Tuple: {
            auto& e = (*ty).as_Tuple();
            for (const auto& st : e) {
                if (typeContainsIvars(st, onlyUnbound)) {
                    return true;
                }
            }
            return false;
        }
    }
    UNREACHABLE();
}

bool HMTypeInferrence::pathparamsEqual(const HIRPathParams& ppsL, const HIRPathParams& ppsR) const {
    return typeListEqual(*this, ppsL.types, ppsR.types);
}

bool HMTypeInferrence::typesEqual(const HIRType* rl, const HIRType* rr) const {
    const auto& l = this->getType(rl);
    const auto& r = this->getType(rr);
    if (l->tag() != r->tag()) {
        return false;
    }

    struct H {
        static bool comparePath(const HMTypeInferrence& self, const HIRPath& l, const HIRPath& r) {
            if (l.data.tag() != r.data.tag()) {
                return false;
            }
            switch (l.data.tag()) {
                case HIRPath::Data::TAG_Generic: {
                    auto& lpe = l.data.as_Generic();
                    auto& rpe = r.data.as_Generic();
                    if (lpe.path != rpe.path) {
                        return false;
                    }
                    return self.pathparamsEqual(lpe.params, rpe.params);
                    break;
                }
                case HIRPath::Data::TAG_UfcsKnown: {
                    auto& lpe = l.data.as_UfcsKnown();
                    auto& rpe = r.data.as_UfcsKnown();
                    if (lpe.item != rpe.item) {
                        return false;
                    }
                    if (!self.typesEqual(lpe.type, rpe.type)) {
                        return false;
                    }
                    if (!self.pathparamsEqual(lpe.trait.params, rpe.trait.params)) {
                        return false;
                    }
                    return self.pathparamsEqual(lpe.params, rpe.params);
                    break;
                }
                case HIRPath::Data::TAG_UfcsInherent: {
                    auto& lpe = l.data.as_UfcsInherent();
                    auto& rpe = r.data.as_UfcsInherent();
                    if (lpe.item != rpe.item) {
                        return false;
                    }
                    if (!self.typesEqual(lpe.type, rpe.type)) {
                        return false;
                    }
                    return self.pathparamsEqual(lpe.params, rpe.params);
                    break;
                }
                case HIRPath::Data::TAG_UfcsUnknown: {
                    BUG(Span(), StringView("UfcsUnknown"));
                    break;
                }
            }
            UNREACHABLE();
        }
    };

    switch ((*l).tag()) {
        case HIRType::TAG_Infer: {
            auto& le = (*l).as_Infer();
            auto& re = (*r).as_Infer();
            return le.index == re.index;
        }
        case HIRType::TAG_Primitive: {
            auto& le = (*l).as_Primitive();
            auto& re = (*r).as_Primitive();
            return le == re;
        }
        case HIRType::TAG_Diverge: {
            return true;
        }
        case HIRType::TAG_Generic: {
            auto& le = (*l).as_Generic();
            auto& re = (*r).as_Generic();
            return le.binding == re.binding;
        }
        case HIRType::TAG_Path: {
            auto& le = (*l).as_Path();
            auto& re = (*r).as_Path();
            return H::comparePath(*this, le.path, re.path);
        }
        case HIRType::TAG_Borrow: {
            auto& le = (*l).as_Borrow();
            auto& re = (*r).as_Borrow();
            if (le.type != re.type) {
                return false;
            }
            return typesEqual(le.inner, re.inner);
        }
        case HIRType::TAG_Pointer: {
            auto& le = (*l).as_Pointer();
            auto& re = (*r).as_Pointer();
            if (le.type != re.type) {
                return false;
            }
            return typesEqual(le.inner, re.inner);
        }
        case HIRType::TAG_Slice: {
            auto& le = (*l).as_Slice();
            auto& re = (*r).as_Slice();
            return typesEqual(le.inner, re.inner);
        }
        case HIRType::TAG_Pattern: {
            auto& le = (*l).as_Pattern();
            auto& re = (*r).as_Pattern();
            return le.pattern.ord(re.pattern) == OrdEqual && typesEqual(le.inner, re.inner);
        }
        case HIRType::TAG_Array: {
            auto& le = (*l).as_Array();
            auto& re = (*r).as_Array();
            if (le.size != re.size) {
                return false;
            }
            return typesEqual(le.inner, re.inner);
        }
        case HIRType::TAG_NodeType: {
            auto& le = (*l).as_NodeType();
            auto& re = (*r).as_NodeType();
            return le == re;
        }
        case HIRType::TAG_NamedFunction: {
            auto& le = (*l).as_NamedFunction();
            auto& re = (*r).as_NamedFunction();
            return H::comparePath(*this, le.path, re.path);
        }
        case HIRType::TAG_Function: {
            auto& le = (*l).as_Function();
            auto& re = (*r).as_Function();
            if (le.isUnsafe != re.isUnsafe || le.abi != re.abi || le.isVariadic != re.isVariadic || le.trackCaller != re.trackCaller) {
                return false;
            }
            if (!typeListEqual(*this, le.argTypes, re.argTypes)) {
                return false;
            }
            return typesEqual(le.rettype, re.rettype);
        }
        case HIRType::TAG_TraitObject: {
            auto& le = (*l).as_TraitObject();
            auto& re = (*r).as_TraitObject();
            if (le.markers.size() != re.markers.size()) {
                return false;
            }
            for (unsigned int i = 0; i < le.markers.size(); i++) {
                const auto& lm = le.markers[i];
                const auto& rm = re.markers[i];
                if (lm.path != rm.path) {
                    return false;
                }
                if (!pathparamsEqual(lm.params, rm.params)) {
                    return false;
                }
            }
            if (le.trait.path.path != re.trait.path.path) {
                return false;
            }
            return pathparamsEqual(le.trait.path.params, re.trait.path.params);
        }
        case HIRType::TAG_ErasedType: {
            auto& le = (*l).as_ErasedType();
            auto& re = (*r).as_ErasedType();
            if (le.inner.tag() != re.inner.tag()) {
                return false;
            }
            switch (le.inner.tag()) {
                case TypeDataErasedTypeInner::TAG_Fcn: {
                    auto& l = le.inner.as_Fcn();
                    auto& r = re.inner.as_Fcn();
                    ASSERT_BUG(Span(), l.origin != HIRSimplePath(), StringView("Erased type with unset origin"));
                    ASSERT_BUG(Span(), r.origin != HIRSimplePath(), StringView("Erased type with unset origin"));
                    return H::comparePath(*this, l.origin, r.origin);
                }
                case TypeDataErasedTypeInner::TAG_Known: {
                    auto& l = le.inner.as_Known();
                    auto& r = re.inner.as_Known();
                    return typesEqual(l, r);
                }
                case TypeDataErasedTypeInner::TAG_Alias: {
                    auto& l = le.inner.as_Alias();
                    auto& r = re.inner.as_Alias();
                    if (l.inner->path != r.inner->path) {
                        return false;
                    }
                    return pathparamsEqual(l.params, r.params);
                }
            }
            break;
        }
        case HIRType::TAG_Tuple: {
            auto& le = (*l).as_Tuple();
            auto& re = (*r).as_Tuple();
            return typeListEqual(*this, le, re);
        }
    }
    UNREACHABLE();
}

bool HMTypeInferrence::containsLiveIvar(const HIRType* type, unsigned int rootIndex) const {
    const auto* resolved = this->getType(type);
    return visitTyWith(resolved, [&](const HIRType* inner) {
        const auto* infer = inner->opt_Infer();
        if (!infer || infer->index == ~0u || isAliasInputInfer(infer->index)) {
            return false;
        }
        if (this->rootIvarIndex(infer->index) == rootIndex) {
            return true;
        }
        const auto* bound = this->getType(inner);
        return bound != inner && this->containsLiveIvar(bound, rootIndex);
    });
}

Unifier::Unifier(const Span& sp, HMTypeInferrence& table, const TraitResolution* resolve)
    : Unifier(sp, table, resolve, {})
{
}

Unifier::Unifier(const Span& sp, HMTypeInferrence& table, const TraitResolution* resolve, Options options)
    : sp_(sp)
    , table_(table)
    , resolve_(resolve)
    , bindRigidValues_(options.bindRigidValues)
    , relateProjectionInputs_(options.relateProjectionInputs)
    , rigidGenericsAreDistinct_(options.rigidGenericsAreDistinct)
    , rigidProjectionsAreDistinct_(options.rigidProjectionsAreDistinct)
{
}

bool Unifier::opaqueCanReveal(const HIRType* type) const {
    const auto* erased = type->opt_ErasedType();
    if (!erased) {
        return false;
    }
    if (erased->inner.is_Known()) {
        return true;
    }
    if (!resolve_) {
        return false;
    }
    if (const auto* alias = erased->inner.opt_Alias()) {
        return resolve_->isOpaqueAliasDefiningScope(*alias->inner);
    }
    if (const auto* function = erased->inner.opt_Fcn()) {
        return resolve_->isDefiningFcnOrigin(function->origin);
    }
    return false;
}

Unifier::Outcome Unifier::defer(const HIRType* left, const HIRType* right) {
    pending_.pushBack(PendingEquality{left, right});
    return Outcome::Ambiguous;
}

Unifier::Outcome Unifier::unify(const HIRType* left, const HIRType* right) {
    const auto snapshot = table_.snapshot();
    const auto pendingBefore = pending_.length();
    const auto pendingValuesBefore = pendingValues_.size();
    const auto bindingsBefore = bindings_.length();
    const auto valueBindingsBefore = valueBindings_.size();
    const auto outcome = this->unifyResolved(left, right);
    if (outcome == Outcome::Mismatch) {
        table_.rollbackTo(snapshot);
        while (pending_.length() > pendingBefore) {
            pending_.popBack();
        }
        while (pendingValues_.size() > pendingValuesBefore) {
            pendingValues_.pop_back();
        }
        while (bindings_.length() > bindingsBefore) {
            bindings_.popBack();
        }
        while (valueBindings_.size() > valueBindingsBefore) {
            valueBindings_.pop_back();
        }
    } else {
        table_.commit(snapshot);
    }
    return outcome == Outcome::Mismatch ? Outcome::Mismatch : pending_.empty() && pendingValues_.empty() ? Outcome::Proven : Outcome::Ambiguous;
}

Unifier::Outcome Unifier::unifyValues(const HIRConstGeneric& left, const HIRConstGeneric& right) {
    const auto snapshot = table_.snapshot();
    const auto pendingValuesBefore = pendingValues_.size();
    const auto bindingsBefore = bindings_.length();
    const auto valueBindingsBefore = valueBindings_.size();
    const auto outcome = this->unifyValuesResolved(left, right);
    if (outcome == Outcome::Mismatch) {
        table_.rollbackTo(snapshot);
        while (pendingValues_.size() > pendingValuesBefore) {
            pendingValues_.pop_back();
        }
        while (bindings_.length() > bindingsBefore) {
            bindings_.popBack();
        }
        while (valueBindings_.size() > valueBindingsBefore) {
            valueBindings_.pop_back();
        }
    } else {
        table_.commit(snapshot);
    }
    return outcome == Outcome::Mismatch ? Outcome::Mismatch : pending_.empty() && pendingValues_.empty() ? Outcome::Proven : Outcome::Ambiguous;
}

Unifier::Outcome Unifier::unifyResolved(const HIRType* leftRaw, const HIRType* rightRaw) {
    const auto* left = table_.getType(leftRaw);
    const auto* right = table_.getType(rightRaw);
    if (left == right) {
        return Outcome::Proven;
    }
    if (left->equalsIgnoringRegions(right)) {
        return Outcome::Proven;
    }

    const auto isDeferredRigidAtom = [](const HIRType* type) {
        if (const auto* infer = type->opt_Infer()) {
            return !inferIsLive(type);
        }
        const auto* generic = type->opt_Generic();
        return generic && generic->isPlaceholder();
    };
    const auto isRigidStructuralType = [](const HIRType* type) {
        switch (type->tag()) {
            case HIRType::TAG_Array:
            case HIRType::TAG_Slice:
            case HIRType::TAG_Tuple:
            case HIRType::TAG_Borrow:
            case HIRType::TAG_Pointer:
            case HIRType::TAG_NamedFunction:
            case HIRType::TAG_Function:
            case HIRType::TAG_TraitObject:
            case HIRType::TAG_Pattern:
                return true;
            default:
                return false;
        }
    };
    const auto rigidAtomOccursIn = [](const HIRType* atom, const HIRType* type) {
        return visitTyWith(type, [&](const HIRType* inner) {
            return inner == atom;
        });
    };
    if ((isDeferredRigidAtom(left) && isRigidStructuralType(right) && rigidAtomOccursIn(left, right)) || (isDeferredRigidAtom(right) && isRigidStructuralType(left) && rigidAtomOccursIn(right, left))) {
        return Outcome::Mismatch;
    }

    const bool leftLive = inferIsLive(left);
    const bool rightLive = inferIsLive(right);
    if (leftLive && rightLive) {
        const auto leftClass = left->as_Infer().tyClass;
        const auto rightClass = right->as_Infer().tyClass;
        if (leftClass != HIRInferClass::None && rightClass != HIRInferClass::None && leftClass != rightClass) {
            return Outcome::Mismatch;
        }
        bindings_.pushBack(PendingEquality{left, right});
        table_.ivarUnify(table_.rootIvarIndex(left->as_Infer().index), table_.rootIvarIndex(right->as_Infer().index));
        return Outcome::Proven;
    }
    if (leftLive || rightLive) {
        const auto* infer = leftLive ? left : right;
        const auto* other = leftLive ? right : left;
        if (const auto* rigidInfer = other->opt_Infer()) {
            if (rigidInfer->index == ~0u || infer->as_Infer().tyClass != HIRInferClass::None) {
                return this->defer(left, right);
            }
        }
        if (infer->as_Infer().tyClass != HIRInferClass::None && typeIsRigidUnknown(other)) {
            return this->defer(left, right);
        }
        if (!literalClassAccepts(table_, infer->as_Infer().tyClass, other)) {
            return Outcome::Mismatch;
        }
        const auto rootIndex = table_.rootIvarIndex(infer->as_Infer().index);
        if (table_.containsLiveIvar(other, rootIndex)) {
            return Outcome::Mismatch;
        }
        bindings_.pushBack(PendingEquality{infer, other});
        table_.setIvarTo(rootIndex, other);
        return Outcome::Proven;
    }
    if (left->is_Infer() || right->is_Infer()) {
        if ((left->is_Infer() && typeIsRigidUnknown(right)) || (right->is_Infer() && typeIsRigidUnknown(left))) {
            return this->defer(left, right);
        }
        auto rigidInferAccepts = [&](const HIRType* inferType, const HIRType* other) {
            const auto tyClass = inferType->as_Infer().tyClass;
            if (tyClass == HIRInferClass::None) {
                return true;
            }
            if (const auto* otherInfer = other->opt_Infer()) {
                return otherInfer->tyClass == HIRInferClass::None || otherInfer->tyClass == tyClass;
            }
            return literalClassAccepts(table_, tyClass, other);
        };
        if ((left->is_Infer() && !rigidInferAccepts(left, right)) || (right->is_Infer() && !rigidInferAccepts(right, left))) {
            return Outcome::Mismatch;
        }
        return this->defer(left, right);
    }
    if (rigidGenericsAreDistinct_) {
        const auto isOrdinaryGeneric = [](const HIRType* type) {
            const auto* generic = type->opt_Generic();
            return generic && !generic->isPlaceholder();
        };
        if (isOrdinaryGeneric(left) || isOrdinaryGeneric(right)) {
            return Outcome::Mismatch;
        }
    }

    if (relateProjectionInputs_) {
        const auto* leftPath = left->opt_Path();
        const auto* rightPath = right->opt_Path();
        const auto* leftProjection = leftPath ? leftPath->path.data.opt_UfcsKnown() : nullptr;
        const auto* rightProjection = rightPath ? rightPath->path.data.opt_UfcsKnown() : nullptr;
        if (leftProjection || rightProjection) {
            if (rigidProjectionsAreDistinct_ && resolve_) {
                auto normalizedLeft = leftProjection ? resolve_->expandAssociatedTypes(sp_, left) : nullptr;
                auto normalizedRight = rightProjection ? resolve_->expandAssociatedTypes(sp_, right) : nullptr;
                if (normalizedLeft && normalizedLeft->is_Infer()) {
                    normalizedLeft = left;
                }
                if (normalizedRight && normalizedRight->is_Infer()) {
                    normalizedRight = right;
                }
                if ((normalizedLeft && normalizedLeft != left) || (normalizedRight && normalizedRight != right)) {
                    return this->unifyResolved(normalizedLeft ? normalizedLeft : left, normalizedRight ? normalizedRight : right);
                }
            }
            if (!leftProjection || !rightProjection) {
                if (!rigidProjectionsAreDistinct_) {
                    return this->defer(left, right);
                }
                return Outcome::Mismatch;
            }
            if (leftProjection->trait.path != rightProjection->trait.path || leftProjection->item != rightProjection->item) {
                return Outcome::Mismatch;
            }
            if (this->unifyResolved(leftProjection->type, rightProjection->type) == Outcome::Mismatch || this->unifyParams(leftProjection->trait.params, rightProjection->trait.params) == Outcome::Mismatch || this->unifyParams(leftProjection->params, rightProjection->params) == Outcome::Mismatch) {
                return Outcome::Mismatch;
            }
            return Outcome::Proven;
        }
    }

    if (typeIsRigidUnknown(left) || typeIsRigidUnknown(right)) {
        return this->defer(left, right);
    }

    if (left->tag() != right->tag()) {
        if ((left->is_Generic() && left->as_Generic().isPlaceholder()) || (right->is_Generic() && right->as_Generic().isPlaceholder())) {
            return this->defer(left, right);
        }
        if (const auto* erased = left->opt_ErasedType(); erased && erased->inner.is_Known()) {
            return this->unifyResolved(erased->inner.as_Known(), right);
        }
        if (const auto* erased = right->opt_ErasedType(); erased && erased->inner.is_Known()) {
            return this->unifyResolved(left, erased->inner.as_Known());
        }
        if (this->opaqueCanReveal(left) || this->opaqueCanReveal(right)) {
            return this->defer(left, right);
        }
        return Outcome::Mismatch;
    }

    switch ((*left).tag()) {
        case HIRType::TAG_Infer: {
            UNREACHABLE();
        }
        case HIRType::TAG_Primitive:
        case HIRType::TAG_Diverge:
        case HIRType::TAG_NodeType: {
            return Outcome::Mismatch;
        }
        case HIRType::TAG_Generic: {
            if (left->as_Generic().isPlaceholder() || right->as_Generic().isPlaceholder()) {
                return this->defer(left, right);
            }
            return Outcome::Mismatch;
        }
        case HIRType::TAG_Path: {
            const auto& le = left->as_Path().path.data.as_Generic();
            const auto& re = right->as_Path().path.data.as_Generic();
            if (le.path != re.path) {
                return Outcome::Mismatch;
            }
            return this->unifyParams(le.params, re.params);
        }
        case HIRType::TAG_Borrow: {
            const auto& le = left->as_Borrow();
            const auto& re = right->as_Borrow();
            if (le.type != re.type) {
                return Outcome::Mismatch;
            }
            return this->unifyResolved(le.inner, re.inner);
        }
        case HIRType::TAG_Pointer: {
            const auto& le = left->as_Pointer();
            const auto& re = right->as_Pointer();
            if (le.type != re.type) {
                return Outcome::Mismatch;
            }
            return this->unifyResolved(le.inner, re.inner);
        }
        case HIRType::TAG_Slice: {
            return this->unifyResolved(left->as_Slice().inner, right->as_Slice().inner);
        }
        case HIRType::TAG_Array: {
            const auto& le = left->as_Array();
            const auto& re = right->as_Array();
            const auto inner = this->unifyResolved(le.inner, re.inner);
            if (inner == Outcome::Mismatch) {
                return Outcome::Mismatch;
            }
            if (!(le.size != re.size)) {
                return inner;
            }
            if (le.size.is_Known() && re.size.is_Known()) {
                return Outcome::Mismatch;
            }
            auto knownValue = [&](u64 value) {
                return HIRConstGeneric::make_Evaluated(freezeEncodedLiteral(table_.types.objectPool(), EncodedLiteral::makeUsize(value)));
            };
            if (le.size.is_Known()) {
                auto value = knownValue(le.size.as_Known());
                return this->unifyValuesResolved(value, re.size.as_Unevaluated()) == Outcome::Mismatch ? Outcome::Mismatch : inner;
            }
            if (re.size.is_Known()) {
                auto value = knownValue(re.size.as_Known());
                return this->unifyValuesResolved(le.size.as_Unevaluated(), value) == Outcome::Mismatch ? Outcome::Mismatch : inner;
            }
            return this->unifyValuesResolved(le.size.as_Unevaluated(), re.size.as_Unevaluated()) == Outcome::Mismatch ? Outcome::Mismatch : inner;
        }
        case HIRType::TAG_Pattern: {
            const auto& le = left->as_Pattern();
            const auto& re = right->as_Pattern();
            if (le.pattern.ord(re.pattern) != OrdEqual) {
                return Outcome::Mismatch;
            }
            return this->unifyResolved(le.inner, re.inner);
        }
        case HIRType::TAG_Tuple: {
            const auto& le = left->as_Tuple();
            const auto& re = right->as_Tuple();
            if (le.length() != re.length()) {
                return Outcome::Mismatch;
            }
            for (size_t i = 0; i < le.length(); i++) {
                if (this->unifyResolved(le[i], re[i]) == Outcome::Mismatch) {
                    return Outcome::Mismatch;
                }
            }
            return Outcome::Proven;
        }
        case HIRType::TAG_Function: {
            const auto& le = left->as_Function();
            const auto& re = right->as_Function();
            if (le.isUnsafe != re.isUnsafe || le.abi != re.abi || le.isVariadic != re.isVariadic || le.trackCaller != re.trackCaller || le.argTypes.length() != re.argTypes.length()) {
                return Outcome::Mismatch;
            }
            for (size_t i = 0; i < le.argTypes.length(); i++) {
                if (this->unifyResolved(le.argTypes[i], re.argTypes[i]) == Outcome::Mismatch) {
                    return Outcome::Mismatch;
                }
            }
            return this->unifyResolved(le.rettype, re.rettype);
        }
        case HIRType::TAG_NamedFunction: {
            return this->defer(left, right);
        }
        case HIRType::TAG_TraitObject: {
            const auto& le = left->as_TraitObject();
            const auto& re = right->as_TraitObject();
            if (le.trait.path.path != re.trait.path.path || le.markers.size() != re.markers.size()) {
                return Outcome::Mismatch;
            }
            for (size_t i = 0; i < le.markers.size(); i++) {
                if (le.markers[i].path != re.markers[i].path) {
                    return Outcome::Mismatch;
                }
                if (this->unifyParams(le.markers[i].params, re.markers[i].params) == Outcome::Mismatch) {
                    return Outcome::Mismatch;
                }
            }
            if (!le.trait.typeBounds.empty() || !re.trait.typeBounds.empty()) {
                return this->defer(left, right);
            }
            return this->unifyParams(le.trait.path.params, re.trait.path.params);
        }
        case HIRType::TAG_ErasedType: {
            const auto& le = left->as_ErasedType();
            const auto& re = right->as_ErasedType();
            if (le.inner.tag() != re.inner.tag()) {
                return this->defer(left, right);
            }
            switch (le.inner.tag()) {
                case TypeDataErasedTypeInner::TAG_Alias: {
                    const auto& li = le.inner.as_Alias();
                    const auto& ri = re.inner.as_Alias();
                    if (li.inner->path != ri.inner->path) {
                        return Outcome::Mismatch;
                    }
                    return this->unifyParams(li.params, ri.params);
                }
                case TypeDataErasedTypeInner::TAG_Known:
                    return this->unifyResolved(le.inner.as_Known(), re.inner.as_Known());
                case TypeDataErasedTypeInner::TAG_Fcn: {
                    const auto& li = le.inner.as_Fcn();
                    const auto& ri = re.inner.as_Fcn();
                    if (li.index != ri.index) {
                        return Outcome::Mismatch;
                    }
                    return li.origin.equalsIgnoringRegions(ri.origin) ? Outcome::Proven : this->defer(left, right);
                }
            }
            UNREACHABLE();
        }
    }
    UNREACHABLE();
}

Unifier::Outcome Unifier::unifyParams(const HIRPathParams& left, const HIRPathParams& right) {
    if (left.types.size() != right.types.size() || left.values.size() != right.values.size()) {
        return Outcome::Mismatch;
    }
    for (size_t i = 0; i < left.types.size(); i++) {
        if (this->unifyResolved(left.types[i], right.types[i]) == Outcome::Mismatch) {
            return Outcome::Mismatch;
        }
    }
    for (size_t i = 0; i < left.values.size(); i++) {
        if (this->unifyValuesResolved(left.values[i], right.values[i]) == Outcome::Mismatch) {
            return Outcome::Mismatch;
        }
    }
    return Outcome::Proven;
}

bool Unifier::paramsContainLiveValueIvar(const HIRPathParams& params, unsigned rootIndex) const {
    for (const auto& type : params.types) {
        if (this->typeContainsLiveValueIvar(type, rootIndex)) {
            return true;
        }
    }
    for (const auto& value : params.values) {
        if (this->valueContainsLiveIvar(value, rootIndex)) {
            return true;
        }
    }
    return false;
}

bool Unifier::pathContainsLiveValueIvar(const HIRPath& path, unsigned rootIndex) const {
    switch (path.data.tag()) {
        case HIRPathData::TAG_Generic:
            return this->paramsContainLiveValueIvar(path.data.as_Generic().params, rootIndex);
        case HIRPathData::TAG_UfcsKnown: {
            const auto& data = path.data.as_UfcsKnown();
            return this->typeContainsLiveValueIvar(data.type, rootIndex) || this->paramsContainLiveValueIvar(data.trait.params, rootIndex) || this->paramsContainLiveValueIvar(data.params, rootIndex);
        }
        case HIRPathData::TAG_UfcsInherent: {
            const auto& data = path.data.as_UfcsInherent();
            return this->typeContainsLiveValueIvar(data.type, rootIndex) || this->paramsContainLiveValueIvar(data.params, rootIndex) || this->paramsContainLiveValueIvar(data.implParams, rootIndex);
        }
        case HIRPathData::TAG_UfcsUnknown: {
            const auto& data = path.data.as_UfcsUnknown();
            return this->typeContainsLiveValueIvar(data.type, rootIndex) || this->paramsContainLiveValueIvar(data.params, rootIndex);
        }
    }
    UNREACHABLE();
}

bool Unifier::traitPathContainsLiveValueIvar(const HIRTraitPath& path, unsigned rootIndex) const {
    if (this->paramsContainLiveValueIvar(path.path.params, rootIndex)) {
        return true;
    }
    for (const auto& bound : path.typeBounds) {
        if (this->paramsContainLiveValueIvar(bound.second.sourceTrait.params, rootIndex) || this->paramsContainLiveValueIvar(bound.second.atyParams, rootIndex) || this->typeContainsLiveValueIvar(bound.second.type, rootIndex)) {
            return true;
        }
    }
    for (const auto& bound : path.traitBounds) {
        if (this->paramsContainLiveValueIvar(bound.second.sourceTrait.params, rootIndex) || this->paramsContainLiveValueIvar(bound.second.atyParams, rootIndex)) {
            return true;
        }
        for (const auto& trait : bound.second.traits) {
            if (this->traitPathContainsLiveValueIvar(trait, rootIndex)) {
                return true;
            }
        }
    }
    return false;
}

bool Unifier::typeContainsLiveValueIvar(const HIRType* type, unsigned rootIndex) const {
    return visitTyWith(table_.getType(type), [&](const HIRType* inner) {
        if (const auto* path = inner->opt_Path()) {
            return this->pathContainsLiveValueIvar(path->path, rootIndex);
        }
        if (const auto* object = inner->opt_TraitObject()) {
            if (this->traitPathContainsLiveValueIvar(object->trait, rootIndex)) {
                return true;
            }
            for (const auto& marker : object->markers) {
                if (this->paramsContainLiveValueIvar(marker.params, rootIndex)) {
                    return true;
                }
            }
            return false;
        }
        if (const auto* erased = inner->opt_ErasedType()) {
            for (const auto& trait : erased->traits) {
                if (this->traitPathContainsLiveValueIvar(trait, rootIndex)) {
                    return true;
                }
            }
            if (this->paramsContainLiveValueIvar(erased->use, rootIndex)) {
                return true;
            }
            switch (erased->inner.tag()) {
                case TypeDataErasedTypeInner::TAG_Fcn:
                    return this->pathContainsLiveValueIvar(erased->inner.as_Fcn().origin, rootIndex);
                case TypeDataErasedTypeInner::TAG_Known:
                    return this->typeContainsLiveValueIvar(erased->inner.as_Known(), rootIndex);
                case TypeDataErasedTypeInner::TAG_Alias:
                    return this->paramsContainLiveValueIvar(erased->inner.as_Alias().params, rootIndex);
            }
            UNREACHABLE();
        }
        if (const auto* array = inner->opt_Array()) {
            return array->size.is_Unevaluated() && this->valueContainsLiveIvar(array->size.as_Unevaluated(), rootIndex);
        }
        if (const auto* pattern = inner->opt_Pattern()) {
            for (const auto& range : pattern->pattern.alternatives) {
                if ((range.hasStart && this->valueContainsLiveIvar(range.start, rootIndex)) || (range.hasEnd && this->valueContainsLiveIvar(range.end, rootIndex))) {
                    return true;
                }
            }
            return false;
        }
        if (const auto* function = inner->opt_NamedFunction()) {
            return this->pathContainsLiveValueIvar(function->path, rootIndex);
        }
        return false;
    });
}

bool Unifier::valueContainsLiveIvar(const HIRConstGeneric& value, unsigned rootIndex) const {
    const auto& resolved = table_.getValue(value);
    if (const auto* infer = resolved.opt_Infer()) {
        return infer->index != ~0u && !isAliasInputInfer(infer->index) && infer->index == rootIndex;
    }
    const auto* unevaluated = resolved.opt_Unevaluated();
    if (!unevaluated) {
        return false;
    }
    const auto& data = **unevaluated;
    return (data.selfType && this->typeContainsLiveValueIvar(data.selfType, rootIndex)) || this->paramsContainLiveValueIvar(data.paramsImpl, rootIndex) || this->paramsContainLiveValueIvar(data.paramsItem, rootIndex);
}

Unifier::Outcome Unifier::unifyValuesResolved(const HIRConstGeneric& leftRaw, const HIRConstGeneric& rightRaw) {
    const auto& left = table_.getValue(leftRaw);
    const auto& right = table_.getValue(rightRaw);
    if (left == right) {
        return Outcome::Proven;
    }

    const auto liveIndex = [&](const HIRConstGeneric& value) -> std::optional<unsigned> {
        const auto* infer = value.opt_Infer();
        if (!infer || infer->index == ~0u || isAliasInputInfer(infer->index)) {
            return {};
        }
        return infer->index;
    };
    const auto deferValue = [&]() {
        pendingValues_.push_back(PendingValueEquality{left.clone(), right.clone()});
        return Outcome::Ambiguous;
    };

    const auto leftLive = liveIndex(left);
    const auto rightLive = liveIndex(right);
    if (leftLive && rightLive) {
        valueBindings_.push_back(PendingValueEquality{left.clone(), right.clone()});
        table_.ivarValUnify(*leftLive, *rightLive);
        return Outcome::Proven;
    }
    if (leftLive || rightLive) {
        const auto& other = leftLive ? right : left;
        const auto* rigidInfer = other.opt_Infer();
        if ((bindRigidValues_ && (other.is_Generic() || rigidInfer)) || other.is_Evaluated() || (other.is_Generic() && !other.as_Generic().isPlaceholder()) || (rigidInfer && rigidInfer->index != ~0u && isAliasInputInfer(rigidInfer->index)) || (other.is_Unevaluated() && !this->valueContainsLiveIvar(other, leftLive ? *leftLive : *rightLive))) {
            valueBindings_.push_back(PendingValueEquality{left.clone(), right.clone()});
            table_.setIvarValTo(leftLive ? *leftLive : *rightLive, other.clone());
            return Outcome::Proven;
        }
        return deferValue();
    }
    if (left.is_Infer() || right.is_Infer()) {
        return deferValue();
    }
    if (left.is_Evaluated() && right.is_Evaluated()) {
        return Outcome::Mismatch;
    }
    if (left.is_Generic() && right.is_Generic()) {
        if (left.as_Generic().isPlaceholder() || right.as_Generic().isPlaceholder()) {
            return deferValue();
        }
        return Outcome::Mismatch;
    }
    if ((left.is_Generic() && !left.as_Generic().isPlaceholder() && right.is_Evaluated()) || (right.is_Generic() && !right.as_Generic().isPlaceholder() && left.is_Evaluated())) {
        return Outcome::Mismatch;
    }
    return deferValue();
}

HIRCompare TraitResolution::comparePp(const Span& sp, const HIRPathParams& left, const HIRPathParams& right) const {
    ASSERT_BUG(sp, left.types.size() == right.types.size(), StringView("Parameter count mismatch - `") << left << StringView("` vs `") << right << StringView("`"));
    ASSERT_BUG(sp, left.values.size() == right.values.size(), StringView("Parameter count mismatch - `") << left << StringView("` vs `") << right << StringView("`"));
    HIRCompare ord = HIRCompare::Equal;
    for (unsigned int i = 0; i < left.types.size(); i++) {
        // TODO: Should allow fuzzy matches using placeholders (match_test_generics_fuzz works for that)

        ord &= left.types[i]->compareWithPlaceholders(sp, right.types[i], this->ivars.callbackResolveInfer());
        if (ord == HIRCompare::Unequal) {
            return ord;
        }
    }
    for (unsigned int i = 0; i < left.values.size(); i++) {
        ord &= compareValue(sp, left.values[i], right.values[i], this->ivars);
        if (ord == HIRCompare::Unequal) {
            return ord;
        }
    }
    return ord;
}

bool TraitResolution::iterateBoundsTraitsCb(const Span& sp, const HIRType* type, const HIRSimplePath& trait, TraitBoundCallback& cb) const {
    for (const auto& b : traitBounds) {
        if (b.first.second.path != trait) {
            continue;
        }
        const HIRType* boundType = b.first.first;
        auto cmp = boundType->compareWithPlaceholders(sp, type, this->ivars.callbackResolveInfer());
        const HIRType* normalizedBound;
        if (cmp == HIRCompare::Unequal && this->hasAssociatedType(boundType) && !normalizingBoundType) {
            normalizingBoundType = true;
            STD_DEFER {
                normalizingBoundType = false;
            };
            normalizedBound = this->expandAssociatedTypes(sp, boundType);
            if (normalizedBound != boundType) {
                boundType = normalizedBound;
                cmp = boundType->compareWithPlaceholders(sp, type, this->ivars.callbackResolveInfer());
            }
        }
        if (cmp != HIRCompare::Unequal && cb.visit(cmp, boundType, b.first.second, b.second)) {
            return true;
        }
    }
    return false;
}

bool TraitResolution::iterateBoundsTraitsCb(const Span& sp, const HIRType* type, TraitBoundCallback& cb) const {
    for (const auto& b : traitBounds) {
        const HIRType* boundType = b.first.first;
        auto cmp = boundType->compareWithPlaceholders(sp, type, this->ivars.callbackResolveInfer());
        const HIRType* normalizedBound;
        if (cmp == HIRCompare::Unequal && this->hasAssociatedType(boundType) && !normalizingBoundType) {
            normalizingBoundType = true;
            STD_DEFER {
                normalizingBoundType = false;
            };
            normalizedBound = this->expandAssociatedTypes(sp, boundType);
            if (normalizedBound != boundType) {
                boundType = normalizedBound;
                cmp = boundType->compareWithPlaceholders(sp, type, this->ivars.callbackResolveInfer());
            }
        }
        if (cmp == HIRCompare::Unequal) {
            continue;
        }
        if (cb.visit(cmp, boundType, b.first.second, b.second)) {
            return true;
        }
    }
    return false;
}

bool TraitResolution::iterateBoundsTraitsCb(const Span& sp, TraitBoundCallback& cb) const {
    for (const auto& b : traitBounds) {
        if (cb.visit(HIRCompare::Equal, b.first.first, b.first.second, b.second)) {
            return true;
        }
    }
    return false;
}

bool TraitResolution::iterateAtyBoundsCb(const Span& sp, const HIRPath::Data::Data_UfcsKnown& pe, TraitPathCallback& cb) const {
    HIRGenericPath traitPath;
    DEBUG(StringView("Checking ATY bounds on ") << pe.trait << StringView(" :: ") << pe.item);
    if (!this->traitContainsType(sp, pe.trait, this->crate.getTraitByPath(sp, pe.trait.path), pe.item.c_str(), traitPath)) {
        BUG(sp, StringView("Cannot find associated type ") << pe.item << StringView(" anywhere in trait ") << pe.trait);
    }
    DEBUG(StringView("trait_path=") << traitPath);
    const auto& traitRef = crate.getTraitByPath(sp, traitPath.path);
    const auto& atyDef = traitRef.types.find(pe.item)->second;

    for (const auto& bound : atyDef.traitBounds) {
        if (cb.visit(bound)) {
            return true;
        }
    }
    for (const auto& bound : traitRef.params.bounds) {
        if (!bound.is_TraitBound()) {
            continue;
        }
        const auto& be = bound.as_TraitBound();

        if (!be.type->is_Path()) {
            continue;
        }
        if (!be.type->as_Path().binding.is_Opaque()) {
            continue;
        }

        const auto& beTypePe = be.type->as_Path().path.data.as_UfcsKnown();
        if (beTypePe.type != crate.types.self()) {
            continue;
        }
        if (beTypePe.trait.path != pe.trait.path) {
            continue;
        }
        if (beTypePe.item != pe.item) {
            continue;
        }

        if (cb.visit(be.trait)) {
            return true;
        }
    }

    return false;
}

bool TraitResolution::assembleMagicCandidatesCb(const Span& sp, const HIRSimplePath& trait, const HIRPathParams& params, const HIRType* ty, AssembledImplCallback& callback) const {
    const auto langCoerceUnsized = this->crate.getLangItemPathOpt("coerce_unsized");
    const auto langFnPtr = this->crate.getLangItemPathOpt("fn_ptr_trait");
    const auto langTuple = this->crate.getLangItemPathOpt("tuple_trait");
    const auto langTransmute = this->crate.getLangItemPathOpt("transmute_trait");
    const auto certaintyFromCompare = [](HIRCompare cmp) {
        return cmp == HIRCompare::Equal ? SolverCertainty::Proven : SolverCertainty::Ambiguous;
    };

    const auto& type = this->ivars.getType(ty);

    TRACE_FUNCTION_F(StringView("trait = ") << trait << params << StringView(", type = ") << type);
    if (trait == langSized()) {
        auto cmp = typeIsSizedBuiltin(sp, type);
        if (cmp != HIRCompare::Unequal) {
            return callback.visit(SolverImpl(type, nullptr, nullptr), certaintyFromCompare(cmp));
        } else {
            return false;
        }
    }

    if (trait == langCopy()) {
        auto cmp = this->typeIsCopyBuiltin(sp, type);
        if (cmp != HIRCompare::Unequal) {
            return callback.visit(SolverImpl(type, nullptr, nullptr), certaintyFromCompare(cmp));
        } else {
            return false;
        }
    }

    if (!langTransmute.components().empty() && trait == langTransmute) {
        if (params.types.size() != 1 || params.values.size() != 1) {
            return false;
        }
        const auto* sourceType = this->ivars.getType(params.types[0]);
        const auto& assumeValue = this->ivars.getValue(params.values[0]);
        if (type->needsMonomorphisation() || sourceType->needsMonomorphisation() || !assumeValue.is_Evaluated()) {
            return false;
        }

        const auto& assume = *assumeValue.as_Evaluated();
        if (!assume.relocations.empty() || assume.bytes.length() != 4) {
            return false;
        }
        StaticTraitResolve targetResolve(this->board());
        if (TargetTypesAreTransmutable(sp, targetResolve, sourceType, type, assume.bytes[0] != 0, assume.bytes[1] != 0, assume.bytes[2] != 0, assume.bytes[3] != 0)) {
            return callback.visit(SolverImpl(type, params.clone(), {}));
        }
        return false;
    }

    if (!langFnPtr.components().empty() && trait == langFnPtr) {
        if (type->is_Function()) {
            return callback.visit(SolverImpl(type, nullptr, nullptr));
        }
    }

    if (trait == langClone() && (type->is_Tuple() || type->is_Array() || type->is_Function() || type->is_NodeType() || type->is_NamedFunction() || ((*type).is_Path() && ((*type).as_Path().isClosure())))) {
        auto cmp = this->typeIsClone(sp, type);
        if (cmp != HIRCompare::Unequal) {
            return callback.visit(SolverImpl(type, nullptr, nullptr), certaintyFromCompare(cmp));
        } else {
            return false;
        }
    }

    if (!langDiscriminantKind().components().empty() && trait == langDiscriminantKind()) {
        const auto nameDiscriminant = RcString::newInterned("Discriminant");
        // TODO: This logic is near identical to the logic in `static.cpp` - can it be de-duplicated?

        if (type->is_Infer() || (type->is_Path() && type->as_Path().binding.is_Unbound())) {
            // TODO: How to prevent EAT from expanding (or setting opaque) too early?
            return callback.visit(SolverImpl(type, HIRPathParams(), HIRTraitPath::assocListT()), SolverCertainty::Ambiguous);
        } else if (type->is_Generic() || (type->is_Path() && type->as_Path().binding.is_Opaque())) {
            HIRTraitPath::assocListT assocList;
            assocList.insert(std::make_pair(nameDiscriminant, HIRTraitPath::AtyEqual{trait, {}, crate.types.path(HIRPath(type, trait.clone(), nameDiscriminant), HIRTypePathBinding::make_Opaque({}))}));
            return callback.visit(SolverImpl(type, HIRPathParams(), HIRTraitPath::assocListT()));
        } else if (type->is_Path() && type->as_Path().binding.is_Enum()) {
            const auto& enm = *type->as_Path().binding.as_Enum();
            const HIRType* tagTy = crate.types.primitive(enm.getReprType(enm.tagRepr));
            HIRTraitPath::assocListT assocList;
            assocList.insert(std::make_pair(nameDiscriminant, HIRTraitPath::AtyEqual{trait, {}, std::move(tagTy)}));
            return callback.visit(SolverImpl(type, {}, std::move(assocList)));
        } else if ((type->is_NodeType() && (type->as_NodeType().is_Generator() || type->as_NodeType().is_Async())) || (type->is_Path() && (type->as_Path().isGenerator() || type->as_Path().isFuture()))) {
            HIRTraitPath::assocListT assocList;
            assocList.insert(std::make_pair(nameDiscriminant, HIRTraitPath::AtyEqual{trait, {}, crate.types.primitive(HIRCoreType::U32)}));
            return callback.visit(SolverImpl(type, {}, std::move(assocList)));
        } else {
            HIRTraitPath::assocListT assocList;
            assocList.insert(std::make_pair(nameDiscriminant, HIRTraitPath::AtyEqual{trait, {}, crate.types.primitive(HIRCoreType::U8)}));
            return callback.visit(SolverImpl(type, {}, std::move(assocList)));
        }
    }
    if (!langPointee().components().empty() && trait == langPointee()) {
        const auto nameMetadata = RcString::newInterned("Metadata");
        const auto delegateMetadata = [&](const HIRType* tailTy) {
            return this->solveTraitGoal(sp, trait, params, tailTy, [&](SolverResponse response) {
                if (!response.impl) {
                    return false;
                }
                HIRTraitPath::assocListT assoc;
                auto metadataTy = response.impl->getType(crate.types, "Metadata", {});
                if (metadataTy) {
                    assoc.insert(std::make_pair(nameMetadata, HIRTraitPath::AtyEqual{trait, {}, std::move(metadataTy)}));
                }
                return callback.visit(SolverImpl(type, params.clone(), std::move(assoc)), response.certainty);
            }, {.ambiguity = SolverAmbiguityPolicy::Report});
        };
        // TODO: This logic is near identical to the logic in `static.cpp` - can it be de-duplicated?

        const HIRType* metaTy = crate.types.infer();
        bool hasMetaTy = false;
        auto certainty = SolverCertainty::Proven;
        if (type->is_Infer() || (type->is_Path() && type->as_Path().binding.is_Unbound())) {
            return callback.visit(SolverImpl(type, HIRPathParams(), HIRTraitPath::assocListT()), SolverCertainty::Ambiguous);
        } else if (type->is_Generic() || (type->is_Path() && type->as_Path().binding.is_Opaque())) {
            const auto sized = typeIsSized(sp, type);
            if (sized != HIRCompare::Unequal) {
                metaTy = crate.types.unit();
                hasMetaTy = true;
                if (sized == HIRCompare::Fuzzy) {
                    certainty = SolverCertainty::Ambiguous;
                }
            } else {
            }
        } else if (type->is_TraitObject()) {
            metaTy = crate.types.path(HIRPath(HIRGenericPath(this->crate.getLangItemPath(sp, "dyn_metadata"), HIRPathParams(type))), HIRTypePathBinding::make_Struct(&crate.getStructByPath(sp, this->crate.getLangItemPath(sp, "dyn_metadata"))));
            hasMetaTy = true;
        } else if (type->is_Slice() || ((*type).is_Primitive() && ((*type).as_Primitive() == HIRCoreType::Str))) {
            metaTy = crate.types.primitive(HIRCoreType::Usize);
            hasMetaTy = true;
        } else if (type->is_Path() && type->as_Path().binding.is_Struct()) {
            const auto& str = *type->as_Path().binding.as_Struct();
            switch (str.structMarkings.dstType) {
                case HIRStructMarkings::DstType::None:
                    metaTy = crate.types.unit();
                    hasMetaTy = true;
                    break;
                case HIRStructMarkings::DstType::Possible:
                case HIRStructMarkings::DstType::Projection:
                case HIRStructMarkings::DstType::TraitObject: {
                    const HIRType* tailTpl = nullptr;
                    switch (str.data.tag()) {
                        case HIRStructData::TAG_Unit: {
                            BUG(sp, StringView("Unsized unit struct in Pointee lookup - ") << type);
                            break;
                        }
                        case HIRStructData::TAG_Tuple: {
                            auto& se = str.data.as_Tuple();
                            ASSERT_BUG(sp, !se.empty(), StringView("Unsized tuple struct without fields - ") << type);
                            tailTpl = se.back().ent;
                            break;
                        }
                        case HIRStructData::TAG_Named: {
                            auto& se = str.data.as_Named();
                            ASSERT_BUG(sp, !se.empty(), StringView("Unsized struct without fields - ") << type);
                            tailTpl = se.back().ty;
                            break;
                        }
                    }
                    ASSERT_BUG(sp, tailTpl, StringView("Missing unsized tail field for ") << type);

                    const auto& path = type->as_Path().path.data.as_Generic();
                    auto tailTy = MonomorphStatePtr(crate.types, type, &path.params, nullptr).monomorphType(sp, tailTpl);
                    tailTy = this->expandAssociatedTypes(sp, std::move(tailTy));

                    return delegateMetadata(tailTy);
                }
                case HIRStructMarkings::DstType::Slice:
                    metaTy = crate.types.primitive(HIRCoreType::Usize);
                    hasMetaTy = true;
                    break;
            }
        } else if (type->is_Tuple() && !type->as_Tuple().empty()) {
            auto tailTy = this->expandAssociatedTypes(sp, type->as_Tuple().back());
            return delegateMetadata(tailTy);
        } else {
            metaTy = crate.types.unit();
            hasMetaTy = true;
        }
        DEBUG(StringView("<") << type << StringView(" as Pointee>::Metadata = ") << metaTy);
        HIRTraitPath::assocListT assocList;
        if (hasMetaTy) {
            assocList.insert(std::make_pair(RcString::newInterned("Metadata"), HIRTraitPath::AtyEqual{trait, {}, mv$(metaTy)}));
        }

        return callback.visit(SolverImpl(type, {}, std::move(assocList)), certainty);
    }
    if (!langTuple.components().empty() && trait == langTuple) {
        if (type->is_Infer() || (type->is_Path() && type->as_Path().binding.is_Unbound())) {
            return callback.visit(SolverImpl(type, HIRPathParams(), HIRTraitPath::assocListT()), SolverCertainty::Ambiguous);
        }
        if (type->is_Tuple()) {
            return callback.visit(SolverImpl(type, {}, HIRTraitPath::assocListT()));
        }
        return false;
    }

    if (!langCoerceUnsized.components().empty() && trait == langCoerceUnsized) {
        if (assembleParamEnvCandidatesCb(sp, trait, params, type, callback)) {
            return true;
        }

        const auto& dstTy = params.types.at(0);
        if (const auto* e = type->opt_Pointer()) {
            if (const auto* de = dstTy->opt_Pointer()) {
                if (de->type < e->type) {
                    auto cmp = e->inner->compareWithPlaceholders(sp, de->inner, this->ivars.callbackResolveInfer());
                    if (cmp != HIRCompare::Unequal) {
                        HIRPathParams pp;
                        pp.types.push_back(dstTy);
                        if (callback.visit(SolverImpl(type, mv$(pp), {}), certaintyFromCompare(cmp))) {
                            return true;
                        }
                    }
                }
            }
        }
    } else if (trait == langPointeeSized()) {
        if (assembleParamEnvCandidatesCb(sp, trait, params, type, callback)) {
            return true;
        }
        return callback.visit(SolverImpl(type, {}, HIRTraitPath::assocListT()));
    } else if (trait == langMetaSized()) {
        TODO(sp, StringView("MetaSized"));

        //case MetadataType::Zero:    // TODO: Does zero apply here?
    }

    if (trait == langDestruct()) {
        if (assembleParamEnvCandidatesCb(sp, trait, params, type, callback)) {
            return true;
        }
        return callback.visit(SolverImpl(type, {}, HIRTraitPath::assocListT()));
    }

    return false;
}

bool TraitResolution::assembleTypeCandidatesCb(const Span& sp, const HIRSimplePath& trait, const HIRPathParams& params, const HIRType* type, AssembledImplCallback& callback) const {
    type = this->ivars.getType(type);
    const bool isAsyncCallableTrait = trait == langAsyncFn() || trait == langAsyncFnMut() || trait == langAsyncFnOnce();
    auto findAsyncCallable = [&](const Vector<const HIRType*>& inputTypes, const HIRType* futureType, bool supportsShared, bool supportsMutable) {
        if (!isAsyncCallableTrait || (trait == langAsyncFn() && !supportsShared) || (trait == langAsyncFnMut() && !supportsMutable)) {
            return false;
        }
        if (params.types.size() != 1 || !params.types.front()->is_Tuple()) {
            BUG(sp, StringView("AsyncFn* traits require a single tuple argument"));
        }

        const auto& desiredInputs = params.types.front()->as_Tuple();
        if (desiredInputs.length() != inputTypes.length()) {
            return false;
        }

        HIRCompare cmp = HIRCompare::Equal;
        for (size_t i = 0; i < inputTypes.length(); i++) {
            cmp &= inputTypes[i]->compareWithPlaceholders(sp, desiredInputs[i], this->ivars.callbackResolveInfer());
        }
        if (cmp == HIRCompare::Unequal) {
            return false;
        }

        const HIRType* outputType = nullptr;
        auto futureCertainty = SolverCertainty::NoSolution;
        this->solveTraitGoal(sp, langFuture(), {}, futureType, [&](SolverResponse response) {
            if (!response.impl) {
                return false;
            }
            auto candidateOutput = response.impl->getType(crate.types, "Output", {});
            if (candidateOutput == nullptr) {
                return false;
            }
            outputType = mv$(candidateOutput);
            futureCertainty = response.certainty;
            return response.certainty == SolverCertainty::Proven;
        }, {.ambiguity = SolverAmbiguityPolicy::Report});
        if (outputType == nullptr) {
            return false;
        }
        HIRPathParams actualParams;
        actualParams.types.push_back(crate.types.tuple(inputTypes));
        HIRGenericPath oncePath(langAsyncFnOnce(), actualParams.clone());
        HIRTraitPath::assocListT assoc;
        assoc.insert(std::make_pair("Output", HIRTraitPath::AtyEqual{oncePath.clone(), {}, outputType}));
        assoc.insert(std::make_pair("CallOnceFuture", HIRTraitPath::AtyEqual{mv$(oncePath), {}, futureType}));
        assoc.insert(std::make_pair("CallRefFuture", HIRTraitPath::AtyEqual{HIRGenericPath(langAsyncFnMut(), actualParams.clone()), {}, futureType}));
        return callback.visit(SolverImpl(type, mv$(actualParams), mv$(assoc)), futureCertainty);
    };

    switch ((*type).tag()) {
        default:
            break;
        case HIRType::TAG_NodeType: {
            auto& e = (*type).as_NodeType();
            switch (e.tag()) {
                case HIRTypeDataNodeType::TAG_Closure: {
                    auto& nodeP = e.as_Closure();
                    if (isAsyncCallableTrait) {
                        bool supportsShared = true;
                        bool supportsMutable = true;
                        if (nodeP->cls == HIRExprNodeClosure::Class::Once) {
                            supportsShared = false;
                            supportsMutable = false;
                        } else if (nodeP->cls == HIRExprNodeClosure::Class::Mut) {
                            supportsShared = false;
                        }
                        Vector<const HIRType*> inputs;
                        inputs.grow(nodeP->args.size());
                        for (const auto& arg : nodeP->args) {
                            inputs.pushBack(arg.second);
                        }
                        return findAsyncCallable(inputs, nodeP->returnType, supportsShared, supportsMutable);
                    }
                    DEBUG(StringView("Closure, ") << trait << StringView(" ?= Fn*"));
                    if (trait == langFn() || trait == langFnMut() || trait == langFnOnce()) {
                        const HIRType* wanted = params.types.size() == 1 && params.types[0]->is_Tuple() ? params.types[0] : nullptr;

                        auto cmp = wanted ? HIRCompare::Equal : HIRCompare::Fuzzy;
                        Vector<const HIRType*> args;
                        if (wanted && wanted->as_Tuple().length() != nodeP->args.size()) {
                            return false;
                        }
                        for (unsigned int i = 0; i < nodeP->args.size(); i++) {
                            const auto& at = nodeP->args[i].second;
                            args.pushBack(at);
                            if (!wanted) {
                                continue;
                            }
                            const auto& argsDes = wanted->as_Tuple();
                            DEBUG(at << StringView(" ?= ") << argsDes[i]);
                            auto argCmp = at->compareWithPlaceholders(sp, argsDes[i], this->ivars.callbackResolveInfer());
                            if (argCmp == HIRCompare::Unequal) {
                                if (const auto* er = argsDes[i]->opt_ErasedType()) {
                                    if (const auto* alias = er->inner.opt_Alias(); alias && isOpaqueAliasDefiningScope(*alias->inner)) {
                                        DEBUG(StringView("- ") << argsDes[i] << StringView(" is an opaque this body defines"));
                                        argCmp = HIRCompare::Fuzzy;
                                    }
                                }
                            }
                            cmp &= argCmp;
                        }
                        if (cmp != HIRCompare::Unequal) {
                            DEBUG(StringView("Closure Fn* impl - cmp = ") << cmp);
                            HIRPathParams pp;
                            pp.types.push_back(crate.types.tuple(mv$(args)));
                            HIRTraitPath::assocListT types;
                            const auto* returnType = closureReturnExpectation(nodeP);
                            types.insert(std::make_pair("Output", HIRTraitPath::AtyEqual{HIRGenericPath(langFnOnce(), pp.clone()), {}, returnType ? returnType : nodeP->returnType}));
                            return callback.visit(SolverImpl(type, mv$(pp), mv$(types)));
                        } else {
                            DEBUG(StringView("Closure Fn* impl - cmp = Compare::Unequal"));
                            return false;
                        }
                    }
                    break;
                }
                case HIRTypeDataNodeType::TAG_Generator: {
                    auto& nodeP = e.as_Generator();
                    if (trait == langGenerator()) {
                        const RcString rcstringYield = RcString::newInterned("Yield");
                        const RcString rcstringReturn = RcString::newInterned("Return");
                        HIRTraitPath::assocListT assoc;
                        assoc.insert(std::make_pair(rcstringYield, HIRTraitPath::AtyEqual{trait.clone(), {}, nodeP->yieldTy}));
                        assoc.insert(std::make_pair(rcstringReturn, HIRTraitPath::AtyEqual{trait.clone(), {}, nodeP->returnType}));
                        HIRPathParams params;
                        params.types.push_back(nodeP->resumeTy);
                        return callback.visit(SolverImpl(type, mv$(params), mv$(assoc)));
                    }
                    break;
                }
                case HIRTypeDataNodeType::TAG_Async: {
                    auto& nodeP = e.as_Async();
                    if (nodeP->isAsyncGen) {
                        if (trait == langAsyncIterator()) {
                            const RcString rcstringItem = RcString::newInterned("Item");
                            HIRTraitPath::assocListT assoc;
                            assoc.insert(std::make_pair(rcstringItem, HIRTraitPath::AtyEqual{trait.clone(), {}, nodeP->yieldTy}));
                            return callback.visit(SolverImpl(type, {}, mv$(assoc)));
                        }
                    } else if (trait == langFuture()) {
                        const RcString rcstringOutput = RcString::newInterned("Output");
                        HIRTraitPath::assocListT assoc;
                        assoc.insert(std::make_pair(rcstringOutput, HIRTraitPath::AtyEqual{trait.clone(), {}, nodeP->returnType}));
                        return callback.visit(SolverImpl(type, {}, mv$(assoc)));
                    }
                    break;
                }
            }
            break;
        }
        case HIRType::TAG_Path: {
            if (isAsyncCallableTrait && type->as_Path().isClosure() && params.types.size() == 1 && params.types.front()->is_Tuple()) {
                const auto& fnTrait = trait == langAsyncFn() ? langFn() : (trait == langAsyncFnMut() ? langFnMut() : langFnOnce());
                const HIRType* futureType;
                this->solveTraitGoal(sp, langFnOnce(), params, type, [&](SolverResponse response) {
                    if (response.certainty != SolverCertainty::Proven || !response.impl) {
                        return false;
                    }
                    futureType = response.impl->getType(crate.types, "Output", {});
                    return futureType != nullptr;
                }, {.ambiguity = SolverAmbiguityPolicy::Report});
                bool callable = fnTrait == langFnOnce();
                if (!callable) {
                    callable = this->solveTraitGoal(sp, fnTrait, params, type, [](SolverResponse response) {
                        return response.impl && response.certainty == SolverCertainty::Proven;
                    }, {.ambiguity = SolverAmbiguityPolicy::Report});
                }
                if (callable && futureType != nullptr && findAsyncCallable(params.types.front()->as_Tuple(), futureType, true, true)) {
                    return true;
                }
            }
            break;
        }
        case HIRType::TAG_Function: {
            auto& e = (*type).as_Function();
            if (isAsyncCallableTrait) {
                if (e.abi != ABI_RUST || e.isUnsafe) {
                    return false;
                }
                return findAsyncCallable(e.argTypes, e.rettype, true, true);
            }
            if (trait == langFn() || trait == langFnMut() || trait == langFnOnce()) {
                if (params.types.size() != 1) {
                    BUG(sp, StringView("Fn* traits require a single tuple argument"));
                }
                if (!params.types[0]->is_Tuple()) {
                    BUG(sp, StringView("Fn* traits require a single tuple argument"));
                }
                const auto& argsDes = params.types[0]->as_Tuple();
                if (argsDes.length() != e.argTypes.length()) {
                    return false;
                }

                if (e.abi != ABI_RUST || e.isUnsafe) {
                    DEBUG(StringView("- No magic impl, wrong ABI or unsafe in ") << type);
                    return false;
                }

                DEBUG(StringView("- Magic impl of Fn* for ") << type);
                Vector<const HIRType*> args;
                for (unsigned int i = 0; i < e.argTypes.length(); i++) {
                    const auto& at = e.argTypes[i];
                    args.pushBack(at);
                }

                HIRPathParams pp;
                pp.types.push_back(crate.types.tuple(mv$(args)));
                HIRTraitPath::assocListT types;
                types.insert(std::make_pair("Output", HIRTraitPath::AtyEqual{HIRGenericPath(langFnOnce(), pp.clone()), {}, e.rettype}));
                return callback.visit(SolverImpl(type, mv$(pp), mv$(types)));
            }
            break;
        }
        case HIRType::TAG_NamedFunction: {
            auto& realE = (*type).as_NamedFunction();
            if (isAsyncCallableTrait) {
                auto e = realE.decay(crate.types, sp);
                if (e.abi != ABI_RUST || e.isUnsafe) {
                    return false;
                }
                return findAsyncCallable(e.argTypes, e.rettype, true, true);
            }
            if (trait == langFn() || trait == langFnMut() || trait == langFnOnce()) {
                if (params.types.size() != 1) {
                    BUG(sp, StringView("Fn* traits require a single tuple argument"));
                }
                if (!params.types[0]->is_Tuple()) {
                    BUG(sp, StringView("Fn* traits require a single tuple argument"));
                }

                DEBUG(StringView("- Magic impl of Fn* for ") << type);
                auto e = realE.decay(crate.types, sp);
                DEBUG(StringView("> ") << e.rettype << StringView(" - ") << e.argTypes);
                const auto& argsDes = params.types[0]->as_Tuple();
                if (argsDes.length() != e.argTypes.length()) {
                    return false;
                }

                if (e.abi != ABI_RUST) {
                    DEBUG(StringView("- No magic impl, wrong ABI (`") << e.abi << StringView("`): ") << type);
                    return false;
                }
                if (e.isUnsafe) {
                    DEBUG(StringView("- No magic impl, unsafe function: ") << type);
                    return false;
                }

                DEBUG(StringView("- Magic impl of Fn* for ") << type);
                Vector<const HIRType*> args;
                for (unsigned int i = 0; i < e.argTypes.length(); i++) {
                    const auto& at = e.argTypes[i];
                    args.pushBack(at);
                }

                HIRPathParams pp;
                pp.types.push_back(crate.types.tuple(mv$(args)));
                HIRTraitPath::assocListT types;
                types.insert(std::make_pair("Output", HIRTraitPath::AtyEqual{HIRGenericPath(langFnOnce(), pp.clone()), {}, e.rettype}));
                return callback.visit(SolverImpl(type, mv$(pp), mv$(types)));
            }
            break;
        }
        case HIRType::TAG_Array: {
            break;
        }
    }
    return false;
}

bool TraitResolution::assembleOtherCandidatesCb(const Span& sp, const HIRSimplePath& trait, const HIRPathParams& params, const HIRType* ty, AssembledImplCallback& callback) const {
    const auto& type = this->ivars.getType(ty);

    TRACE_FUNCTION_F(StringView("trait = ") << trait << params << StringView(", type = ") << type);
    if (assembleTypeCandidatesCb(sp, trait, params, ty, callback)) {
        return true;
    }

    switch ((*type).tag()) {
        default:
            break;

        // - IF object safe (TODO)
        case HIRType::TAG_TraitObject: {
            auto& e = (*type).as_TraitObject();
            if (trait == e.trait.path.path) {
                return callback.visit(SolverImpl(type, &e.trait.path.params, &e.trait.typeBounds, e.trait.constness));
            }
            for (const auto& mt : e.markers) {
                if (trait == mt.path) {
                    return callback.visit(SolverImpl(type, &mt.params, nullptr));
                }
            }

            if (e.trait.path.path != HIRSimplePath()) {
                bool rv = false;
                bool isSupertrait = false;
                this->findNamedTraitInTrait(sp, trait, params, *e.trait.traitPtr, e.trait.path.path, e.trait.path.params, type, [&](const HIRTraitPath& iTp) {
                    HIRTraitPath::assocListT assocClone;
                    for (const auto& entry : iTp.typeBounds) {
                        assocClone.insert(std::make_pair(entry.first, entry.second.clone()));
                    }
                    for (const auto& bound : e.trait.typeBounds) {
                        if (bound.second.sourceTrait.path == trait && comparePp(sp, bound.second.sourceTrait.params, iTp.path.params) != HIRCompare::Unequal) {
                            assocClone.erase(bound.first);
                            assocClone.insert(std::make_pair(bound.first, bound.second.clone()));
                        }
                    }
                    auto impl = SolverImpl(type, iTp.path.params.clone(), mv$(assocClone));
                    isSupertrait = true;
                    rv = callback.visit(mv$(impl));
                    return rv;
                });
                if (isSupertrait) {
                    return rv;
                }
            }
            break;
        }
        case HIRType::TAG_ErasedType: {
            auto& e = (*type).as_ErasedType();
            for (const auto& traitPath : e.traits) {
                if (trait == traitPath.path.path) {
                    return callback.visit(SolverImpl(type, &traitPath.path.params, &traitPath.typeBounds, traitPath.constness));
                }

                bool rv = false;
                bool isSupertrait = false;
                this->findNamedTraitInTrait(sp, trait, params, *traitPath.traitPtr, traitPath.path.path, traitPath.path.params, type, [&](const HIRTraitPath& iTp) {
                    HIRTraitPath::assocListT assocClone;
                    for (const auto& entry : iTp.typeBounds) {
                        assocClone.insert(std::make_pair(entry.first, entry.second.clone()));
                    }
                    for (const auto& bound : traitPath.typeBounds) {
                        if (bound.second.sourceTrait.path == trait && comparePp(sp, bound.second.sourceTrait.params, iTp.path.params) != HIRCompare::Unequal) {
                            assocClone.erase(bound.first);
                            assocClone.insert(std::make_pair(bound.first, bound.second.clone()));
                        }
                    }
                    auto impl = SolverImpl(type, iTp.path.params.clone(), mv$(assocClone));
                    isSupertrait = true;
                    rv = callback.visit(mv$(impl));
                    return rv;
                });
                if (isSupertrait) {
                    return rv;
                }
            }
            break;
        }
        case HIRType::TAG_Generic: {
            break;
        }
        case HIRType::TAG_Path: {
            auto& e = (*type).as_Path();
            if (e.binding.is_Opaque()) {
                ASSERT_BUG(sp, e.path.data.is_UfcsKnown(), StringView("Opaque bound type wasn't UfcsKnown - ") << type);
                const auto& pe = e.path.data.as_UfcsKnown();

                DEBUG(StringView("Checking bounds on definition of ") << pe.item << StringView(" in ") << pe.trait);
                // TODO: Should Self here be `type` or `pe.type`

                auto monomorphCb = MonomorphStatePtr(crate.types, pe.type, &pe.trait.params, &pe.params);
                auto rv = this->iterateAtyBounds(sp, pe, [&](const HIRTraitPath& bound) {
                    DEBUG(StringView("Bound on ATY: ") << bound);
                    auto ppHrb = HIRPathParams();
                    monomorphCb.ppHrb = &ppHrb;
                    const auto& bParams = bound.path.params;
                    HIRPathParams paramsMonoO;
                    const HIRPathParams* bParamsMono = &bParams;
                    if (monomorphisePathparamsNeeded(bParams)) {
                        paramsMonoO = monomorphCb.monomorphPathParams(sp, bParams, false);
                        bParamsMono = &paramsMonoO;
                    }
                    const bool paramsNeedNormalisation = std::any_of(bParamsMono->types.begin(), bParamsMono->types.end(), [&](const auto& ty) {
                        return this->hasAssociatedType(ty);
                    });
                    if (paramsNeedNormalisation) {
                        if (bParamsMono != &paramsMonoO) {
                            paramsMonoO = bParams.clone();
                            bParamsMono = &paramsMonoO;
                        }
                        this->expandAssociatedTypesParams(sp, paramsMonoO);
                    }

                    HIRTraitPath::assocListT bAtys;
                    for (const auto& aty : bound.typeBounds) {
                        bAtys.insert(std::make_pair(aty.first, HIRTraitPath::AtyEqual{monomorphCb.monomorphGenericpath(sp, aty.second.sourceTrait, false), monomorphCb.monomorphPathParams(sp, aty.second.atyParams, false), monomorphCb.monomorphType(sp, aty.second.type)}));
                    }

                    if (bound.path.path == trait) {
                        if (bParamsMono == &paramsMonoO) {
                            if (callback.visit(SolverImpl(type, mv$(paramsMonoO), mv$(bAtys), bound.constness))) {
                                return true;
                            }
                            paramsMonoO = monomorphCb.monomorphPathParams(sp, bParams, false);
                            if (paramsNeedNormalisation) {
                                this->expandAssociatedTypesParams(sp, paramsMonoO);
                            }
                        } else if (!bAtys.empty()) {
                            if (callback.visit(SolverImpl(type, bParamsMono->clone(), mv$(bAtys), bound.constness))) {
                                return true;
                            }
                        } else {
                            if (callback.visit(SolverImpl(type, &bound.path.params, nullptr, bound.constness))) {
                                return true;
                            }
                        }
                    }
                    monomorphCb.ppHrb = nullptr;

                    bool rv = false;
                    bool ret = false;
                    this->findNamedTraitInTrait(sp, trait, params, *bound.traitPtr, bound.path.path, *bParamsMono, type, [&](const HIRTraitPath& iTp) {
                        HIRTraitPath::assocListT assocClone;
                        for (const auto& aty : iTp.typeBounds) {
                            assocClone.insert(std::make_pair(aty.first, aty.second.clone()));
                        }
                        auto ir = SolverImpl(type, iTp.path.params.clone(), mv$(assocClone), iTp.constness);
                        rv |= callback.visit(std::move(ir));
                        ret = true;
                        return rv;
                    });
                    if (ret) {
                        return rv;
                    }
                    return false;
                });
                if (rv) {
                    return true;
                }
            }
            break;
        }
    }

    return false;
}

TraitResolution::TraitResolution(HMTypeInferrence& ivars, const WireBoard& wb, const HIRGenericParams* implParams, const HIRGenericParams* itemParams, const HIRSimplePath& visPath, const HIRGenericPath* currentTrait)
    : TraitResolveCommon(wb)
    , langDeref_(crate.getLangItemPathOpt("deref"))
    , ivars(ivars)
    , visPath(visPath)
    , currentTraitPath_(currentTrait)
    , currentTraitPtr(currentTrait ? &crate.getTraitByPath(Span(), currentTrait->path) : nullptr)
    , eatCachePool(ObjPool::fromMemory())
    , eatCache(eatCachePool.mutPtr())
    , solverExistentials_(eatCachePool.mutPtr())
{
    implGenerics_ = implParams;
    itemGenerics_ = itemParams;
    prepIndexes(Span());
}

TraitResolution::~TraitResolution() = default;

void TraitResolution::setGenericContext(const HIRGenericParams* implParams, const HIRGenericParams* itemParams) {
    if (implGenerics_ == implParams && itemGenerics_ == itemParams) {
        return;
    }
    implGenerics_ = implParams;
    itemGenerics_ = itemParams;
    eatCacheGeneration++;
    prepIndexes(Span());
}

void TraitResolution::addOpaqueAliasScope(const HIRSimplePath& path) {
    if (path.components().empty()) {
        return;
    }
    if (std::find(opaqueAliasScopes.begin(), opaqueAliasScopes.end(), path) == opaqueAliasScopes.end()) {
        opaqueAliasScopes.push_back(path);
        solverEnvGeneration++;
    }
}

void TraitResolution::addDefiningFcnOrigin(const HIRPath& origin) {
    for (const auto* existing : definingFcnOrigins) {
        if (*existing == origin) {
            return;
        }
    }
    definingFcnOrigins.pushBack(eatCachePool.mutPtr()->make<HIRPath>(origin.clone()));
    solverEnvGeneration++;
}

bool TraitResolution::isDefiningFcnOrigin(const HIRPath& origin) const {
    for (const auto* existing : definingFcnOrigins) {
        if (*existing == origin) {
            return true;
        }
    }
    return false;
}

void TraitResolution::setClosureReturnExpectation(const HIRExprNodeClosure* closure, const HIRType* type) {
    for (size_t i = 0; i < closureReturnExpectations.length(); i++) {
        auto& expectation = closureReturnExpectations.mut(i);
        if (expectation.closure != closure) {
            continue;
        }
        if (expectation.type != type) {
            expectation.type = type;
            solverEnvGeneration++;
        }
        return;
    }
    closureReturnExpectations.pushBack(ClosureReturnExpectation{closure, type});
    solverEnvGeneration++;
}

const HIRType* TraitResolution::closureReturnExpectation(const HIRExprNodeClosure* closure) const {
    for (const auto& expectation : closureReturnExpectations) {
        if (expectation.closure == closure) {
            return expectation.type;
        }
    }
    return nullptr;
}

void TraitResolution::addDefiningOpaqueAlias(const HIRSimplePath& path) {
    if (std::find(definingOpaqueAliases.begin(), definingOpaqueAliases.end(), path) == definingOpaqueAliases.end()) {
        definingOpaqueAliases.push_back(path);
        solverEnvGeneration++;
    }
}

bool TraitResolution::isOpaqueAliasDefiningScope(const HIRTypeDataErasedTypeAliasInner& alias) const {
    if (this->wb.crate && this->wb.crate->isOpaqueAliasNamedBy(alias, definingOpaqueAliases.data(), definingOpaqueAliases.size())) {
        return true;
    }
    for (const auto& path : opaqueAliasScopes) {
        if (alias.isLocalTo(path)) {
            return true;
        }
    }
    return false;
}

HIRPathParams TraitResolution::makeFreshImplParams(const HIRGenericParams& params) const {
    HIRPathParams result;
    result.types.reserve(params.types.size());
    for (size_t i = 0; i < params.types.size(); i++) {
        result.types.push_back(this->ivars.newIvarTr());
    }
    result.values.reserve(params.values.size());
    for (size_t i = 0; i < params.values.size(); i++) {
        result.values.push_back(HIRConstGeneric::make_Infer({this->ivars.newIvarVal()}));
    }
    return result;
}

const HIRPathParams& TraitResolution::solverExistentials(const Span& sp, const HIRGenericParams& definition) const {
    const auto key = splitMix64(reinterpret_cast<uintptr_t>(&definition));
    auto* bucket = solverExistentials_.find(key);
    if (bucket) {
        for (const auto& entry : *bucket) {
            if (entry.definition == &definition) {
                return entry.params;
            }
        }
    } else {
        bucket = solverExistentials_.insert(key);
    }

    const auto scope = ++this->board().id;
    ASSERT_BUG(sp, scope != 0, StringView("solver existential scope exhausted"));

    HIRPathParams params;
    params.types.reserve(definition.types.size());
    for (size_t i = 0; i < definition.types.size(); i++) {
        ASSERT_BUG(sp, i < 256, StringView("Too many candidate type parameters"));
        params.types.push_back(crate.types.generic(HIRGenericRef::newSolverExistential(scope, static_cast<u16>(i))));
    }
    params.values.reserve(definition.values.size());
    for (size_t i = 0; i < definition.values.size(); i++) {
        ASSERT_BUG(sp, i < 256, StringView("Too many candidate value parameters"));
        params.values.push_back(HIRGenericRef::newSolverExistential(scope, static_cast<u16>(i)));
    }
    bucket->push_back(SolverExistentials{&definition, std::move(params)});
    return bucket->back().params;
}

HIRPathParams TraitResolution::materializeImplParams(const Span& sp, const HIRGenericParams& definition, const HIRPathParams& inferenceParams, size_t externalTypeIvars, size_t externalValueIvars) const {
    const auto& stable = this->solverExistentials(sp, definition);

    struct Materialize final: public MonomorphiserNop {
        const HMTypeInferrence& table;
        const HIRPathParams& inference;
        const HIRPathParams& stable;
        size_t externalTypeIvars;
        size_t externalValueIvars;

    public:
        Materialize(HIRTypeInterner& types, const HMTypeInferrence& table, const HIRPathParams& inference, const HIRPathParams& stable, size_t externalTypeIvars, size_t externalValueIvars)
            : MonomorphiserNop(types)
            , table(table)
            , inference(inference)
            , stable(stable)
            , externalTypeIvars(externalTypeIvars)
            , externalValueIvars(externalValueIvars)
        {
        }

        const HIRType* monomorphType(const Span& sp, const HIRType* type, bool allowInfer = true) const override {
            if (const auto* infer = type->opt_Infer()) {
                for (size_t i = 0; i < inference.types.size(); i++) {
                    const auto* parameter = inference.types[i]->opt_Infer();
                    if (!parameter || parameter->index != infer->index) {
                        continue;
                    }
                    const auto* resolved = table.getType(type);
                    if (const auto* resolvedInfer = resolved->opt_Infer()) {
                        for (size_t j = 0; j < inference.types.size(); j++) {
                            const auto* candidate = inference.types[j]->opt_Infer();
                            if (candidate && candidate->index == resolvedInfer->index) {
                                return stable.types[j];
                            }
                        }
                        if (resolvedInfer->index >= externalTypeIvars) {
                            return stable.types[i];
                        }
                        return resolved;
                    }
                    return this->monomorphType(sp, resolved, allowInfer);
                }
            }
            return MonomorphiserNop::monomorphType(sp, type, allowInfer);
        }

        HIRConstGeneric monomorphConstgeneric(const Span& sp, const HIRConstGeneric& value, bool allowInfer) const override {
            if (const auto* infer = value.opt_Infer()) {
                for (size_t i = 0; i < inference.values.size(); i++) {
                    const auto* parameter = inference.values[i].opt_Infer();
                    if (!parameter || parameter->index != infer->index) {
                        continue;
                    }
                    const auto& resolved = table.getValue(value);
                    if (const auto* resolvedInfer = resolved.opt_Infer()) {
                        for (size_t j = 0; j < inference.values.size(); j++) {
                            const auto* candidate = inference.values[j].opt_Infer();
                            if (candidate && candidate->index == resolvedInfer->index) {
                                return stable.values[j].clone();
                            }
                        }
                        if (resolvedInfer->index >= externalValueIvars) {
                            return stable.values[i].clone();
                        }
                        return resolved.clone();
                    }
                    return this->monomorphConstgeneric(sp, resolved, allowInfer);
                }
            }
            return MonomorphiserNop::monomorphConstgeneric(sp, value, allowInfer);
        }
    } materialize(crate.types, ivars, inferenceParams, stable, externalTypeIvars, externalValueIvars);

    return materialize.monomorphPathParams(sp, inferenceParams, true);
}

bool TraitResolution::implsOverlap(const Span& sp, const SolverImpl& left, const SolverImpl& right) const {
    const auto* leftImpl = left.traitImpl;
    const auto* rightImpl = right.traitImpl;
    if (!leftImpl || !rightImpl) {
        return !leftImpl && !rightImpl && left.getImplType(crate.types) == right.getImplType(crate.types) && left.getTraitParamsRef(crate.types) == right.getTraitParamsRef(crate.types);
    }
    if (left.traitPath != right.traitPath) {
        return false;
    }
    if (leftImpl == rightImpl) {
        return true;
    }

    if (!coherenceEvaluator) {
        coherenceEvaluator = eatCachePool->make<NextTraitGoalEvaluator>(*this, crate);
    }
    auto snapshot = ivars.snapshot();
    STD_DEFER {
        ivars.rollbackTo(snapshot);
    };
    return coherenceEvaluator->evaluateOverlap(sp, left.traitPath, *leftImpl, *rightImpl);
}

bool TraitResolution::solveTraitGoalCb(const Span& sp, const HIRSimplePath& trait, const HIRPathParams& params, const HIRType* type, SolverResponseCallback& callback, const TraitGoalQuery& query) const {
    if (!nextSolver) {
        nextSolver = eatCachePool->make<NextTraitGoalEvaluator>(*this, crate);
    }
    return nextSolver->evaluateTyped(sp, trait, params, type, callback, query, true);
}

SolverCertainty TraitResolution::solveNonBuiltinTraitGoal(const Span& sp, const HIRSimplePath& trait, const HIRType* type) const {
    if (!nonBuiltinSolver) {
        nonBuiltinSolver = eatCachePool->make<NextTraitGoalEvaluator>(*this, crate);
    }
    SolverCertainty certainty = SolverCertainty::NoSolution;
    auto callback = makeCallable<SolverResponseCb>([&](SolverResponse response) {
        if (!response.impl) {
            return false;
        }
        certainty = response.certainty;
        return true;
    });
    nonBuiltinSolver->evaluateTyped(sp, trait, HIRPathParams{}, type, callback, {.allowInferInputs = true, .ambiguity = SolverAmbiguityPolicy::Report}, true, false);
    return certainty;
}

bool TraitResolution::solveNormalizesToCb(const Span& sp, const NormalizesTo& goal, NormalizesToCallback& callback) const {
    if (!nextSolver) {
        nextSolver = eatCachePool->make<NextTraitGoalEvaluator>(*this, crate);
    }
    return nextSolver->evaluateNormalizesTo(sp, goal, callback, true);
}

void TraitResolution::compactIvars(HMTypeInferrence& ivars, SolverResponseCallback* effects) {
    ASSERT_BUG(Span(), !ivars.probing(), StringView("ivar compaction during an active inference snapshot"));
    ivars.checkForLoops();

    const auto initialIvarCount = ivars.ivars.size();
    for (unsigned int i = 0; i < initialIvarCount; i++) {
        if (!ivars.ivars[i].isAlias()) {
            auto type = ivars.ivars[i].type;
            type = ivars.expandIvars(type);
            if (this->hasAssociatedType(type)) {
                auto normalized = this->expandAssociatedTypes(Span(), type, effects);
                if (!ivars.ivars[i].isAlias()) {
                    ivars.ivars[i].type = normalized;
                }
            } else {
                ivars.ivars[i].type = type;
            }
        } else {
            auto index = ivars.ivars[i].alias;
            unsigned int count = 0;
            BUG_ASSERT(index < ivars.ivars.size());
            while (ivars.ivars.at(index).isAlias()) {
                index = ivars.ivars.at(index).alias;

                if (count >= ivars.ivars.size()) {
                    BUG(Span(), StringView("Loop detected in ivar list when starting at ") << ivars.ivars[i].alias << StringView(", current is ") << index);
                }
                count++;
            }
            ivars.ivars[i].alias = index;
        }
    }
}

bool TraitResolution::hasAssociatedType(const HIRType* input) const {
    if (!input->mayHaveAssociatedType()) {
        return false;
    }

    struct H {
        static bool checkPathparams(const TraitResolution& r, const HIRPathParams& pp) {
            for (const auto& arg : pp.types) {
                if (r.hasAssociatedType(arg)) {
                    return true;
                }
            }
            return false;
        }

        static bool checkPath(const TraitResolution& r, const HIRPath& p) {
            switch (p.data.tag()) {
                case HIRPath::Data::TAG_Generic: {
                    auto& e2 = p.data.as_Generic();
                    return H::checkPathparams(r, e2.params);
                }
                case HIRPath::Data::TAG_UfcsInherent: {
                    auto& e2 = p.data.as_UfcsInherent();
                    if (r.hasAssociatedType(e2.type)) {
                        return true;
                    }
                    if (H::checkPathparams(r, e2.params)) {
                        return true;
                    }
                    return false;
                    break;
                }
                case HIRPath::Data::TAG_UfcsKnown: {
                    auto& e2 = p.data.as_UfcsKnown();
                    if (r.hasAssociatedType(e2.type)) {
                        return true;
                    }
                    if (H::checkPathparams(r, e2.trait.params)) {
                        return true;
                    }
                    if (H::checkPathparams(r, e2.params)) {
                        return true;
                    }
                    return false;
                    break;
                }
                case HIRPath::Data::TAG_UfcsUnknown: {
                    BUG(Span(), StringView("Encountered UfcsUnknown - ") << p);
                    break;
                }
            }
            UNREACHABLE();
        }
    };

    switch ((*input).tag()) {
        case HIRType::TAG_Infer: {
            const auto& ty = this->ivars.getType(input);
            if (ty != input) {
                return this->hasAssociatedType(ty);
            }
            return false;
        }
        case HIRType::TAG_Diverge: {
            return false;
        }
        case HIRType::TAG_Primitive: {
            return false;
        }
        case HIRType::TAG_Path: {
            auto& e = (*input).as_Path();
            if (e.path.data.is_UfcsKnown() && (e.binding.is_Unbound() || e.binding.is_Opaque())) {
                return true;
            }
            return H::checkPath(*this, e.path);
        }
        case HIRType::TAG_Generic: {
            return false;
        }
        case HIRType::TAG_TraitObject: {
            auto& e = (*input).as_TraitObject();
            if (H::checkPathparams(*this, e.trait.path.params)) {
                return true;
            }
            for (const auto& m : e.markers) {
                if (H::checkPathparams(*this, m.params)) {
                    return true;
                }
            }
            return false;
        }
        case HIRType::TAG_ErasedType: {
            auto& e = (*input).as_ErasedType();
            switch (e.inner.tag()) {
                case TypeDataErasedTypeInner::TAG_Fcn: {
                    auto& ee = e.inner.as_Fcn();
                    if (H::checkPath(*this, ee.origin)) {
                        return true;
                    }
                    break;
                }
                case TypeDataErasedTypeInner::TAG_Known: {
                    auto& ee = e.inner.as_Known();
                    if (hasAssociatedType(ee)) {
                        return true;
                    }
                    break;
                }
                case TypeDataErasedTypeInner::TAG_Alias: {
                    break;
                }
            }
            for (const auto& m : e.traits) {
                if (H::checkPathparams(*this, m.path.params)) {
                    return true;
                }
            }
            return false;
        }
        case HIRType::TAG_Array: {
            auto& e = (*input).as_Array();
            return hasAssociatedType(e.inner);
        }
        case HIRType::TAG_Slice: {
            auto& e = (*input).as_Slice();
            return hasAssociatedType(e.inner);
        }
        case HIRType::TAG_Pattern: {
            auto& e = (*input).as_Pattern();
            return hasAssociatedType(e.inner);
        }
        case HIRType::TAG_Tuple: {
            auto& e = (*input).as_Tuple();
            bool rv = false;
            for (const auto& sub : e) {
                rv |= hasAssociatedType(sub);
            }
            return rv;
        }
        case HIRType::TAG_Borrow: {
            auto& e = (*input).as_Borrow();
            return hasAssociatedType(e.inner);
        }
        case HIRType::TAG_Pointer: {
            auto& e = (*input).as_Pointer();
            return hasAssociatedType(e.inner);
        }
        case HIRType::TAG_NamedFunction: {
            auto& e = (*input).as_NamedFunction();
            return H::checkPath(*this, e.path);
        }
        case HIRType::TAG_Function: {
            return false;
        }
        case HIRType::TAG_NodeType: {
            return false;
        }
    }
    BUG(Span(), StringView("Fell off the end of has_associated_type - input=") << input);
}

const HIRType* TraitResolution::expandAssociatedTypesInplace(const Span& sp, const HIRType* input, SolverResponseCallback* effects) const {
    struct H {
        static void expandAssociatedTypesParams(const Span& sp, const TraitResolution& res, HIRPathParams& params, SolverResponseCallback* effects) {
            for (auto& arg : params.types) {
                arg = res.expandAssociatedTypesInplace(sp, arg, effects);
            }
        }

        static void expandAssociatedTypesTp(const Span& sp, const TraitResolution& res, HIRTraitPath& input, SolverResponseCallback* effects) {
            expandAssociatedTypesParams(sp, res, input.path.params, effects);
            for (auto& arg : input.typeBounds) {
                expandAssociatedTypesParams(sp, res, arg.second.sourceTrait.params, effects);
                arg.second.type = res.expandAssociatedTypesInplace(sp, arg.second.type, effects);
            }
            for (auto& arg : input.traitBounds) {
                expandAssociatedTypesParams(sp, res, arg.second.sourceTrait.params, effects);
                for (auto& t : arg.second.traits) {
                    expandAssociatedTypesTp(sp, res, t, effects);
                }
            }
        }
    };

    auto data = input->cloneData();
    switch (data.tag()) {
        case HIRType::TAG_Infer: {
            const auto* ty = this->ivars.getType(input);
            if (ty != input) {
                return expandAssociatedTypesInplace(sp, ty, effects);
            }
            break;
        }
        case HIRType::TAG_Diverge: {
            break;
        }
        case HIRType::TAG_Primitive: {
            break;
        }
        case HIRType::TAG_Path: {
            auto& e = data.as_Path();
            switch (e.path.data.tag()) {
                case HIRPathData::TAG_Generic: {
                    auto& pe = e.path.data.as_Generic();
                    ConvertHIRConstantEvaluateMethodParams(sp, this->wb, crate, e.binding.getGenerics(), pe.params);
                    H::expandAssociatedTypesParams(sp, *this, pe.params, effects);
                    break;
                }
                case HIRPathData::TAG_UfcsInherent: {
                    auto& pe = e.path.data.as_UfcsInherent();
                    pe.type = expandAssociatedTypesInplace(sp, pe.type, effects);
                    H::expandAssociatedTypesParams(sp, *this, pe.params, effects);
                    H::expandAssociatedTypesParams(sp, *this, pe.implParams, effects);
                    input = crate.types.intern(mv$(data));
                    if (const auto* expanded = this->expandAssociatedTypesInplaceUfcsInherent(sp, input, effects)) {
                        return this->expandAssociatedTypesInplace(sp, expanded, effects);
                    }
                    return input;
                }
                case HIRPathData::TAG_UfcsKnown: {
                    auto& pe = e.path.data.as_UfcsKnown();
                    pe.type = expandAssociatedTypesInplace(sp, pe.type, effects);
                    H::expandAssociatedTypesParams(sp, *this, pe.params, effects);
                    const auto& traitDef = crate.getTraitByPath(sp, pe.trait.path);
                    ConvertHIRConstantEvaluateMethodParams(sp, this->wb, crate, &traitDef.params, pe.trait.params);
                    H::expandAssociatedTypesParams(sp, *this, pe.trait.params, effects);
                    input = crate.types.intern(mv$(data));
                    const bool wasUnbound = input->as_Path().binding.is_Unbound();
                    const bool wasOpaque = input->as_Path().binding.is_Opaque();
                    if (wasUnbound || wasOpaque) {
                        if (wasOpaque) {
                            return this->expandAssociatedTypesInplaceUfcsKnown(sp, input, effects);
                        }

                        const auto cacheKey = input->uid;

                        auto* cached = ivars.probing() ? nullptr : eatCache.find(cacheKey);
                        if (cached && cached->generation == eatCacheGeneration && (!((input->flags | cached->type->flags) & (HIRType::HAS_TYPE_INFER | HIRType::HAS_DEFERRED_CONST)) || cached->ivarGeneration == ivars.mutationGeneration)) {
                            if (input != cached->type) {
                                cached->type = this->expandAssociatedTypesInplace(sp, cached->type, effects);
                            }
                            DEBUG(StringView("CACHED: ") << input << StringView(" -> ") << cached->type);
                            input = cached->type;
                        } else {
                            input = this->expandAssociatedTypesInplaceUfcsKnown(sp, input, effects);
                            if (input->is_Path() && (input->as_Path().binding.is_Unbound() || input->as_Path().binding.is_Opaque())) {
                            } else if (!ivars.probing()) {
                                DEBUG(StringView("CACHE+: ") << cacheKey << StringView(" = ") << input);
                                eatCache.insert(cacheKey, EatCacheEntry{eatCacheGeneration, ivars.mutationGeneration, input});
                            }
                        }
                    }
                    return input;
                }
                case HIRPathData::TAG_UfcsUnknown: {
                    auto& pe = e.path.data.as_UfcsUnknown();
                    pe.type = expandAssociatedTypesInplace(sp, pe.type, effects);
                    H::expandAssociatedTypesParams(sp, *this, pe.params, effects);
                    break;
                }
            }
            break;
        }
        case HIRType::TAG_Generic: {
            break;
        }
        case HIRType::TAG_TraitObject: {
            auto& e = data.as_TraitObject();
            H::expandAssociatedTypesTp(sp, *this, e.trait, effects);
            for (auto& m : e.markers) {
                H::expandAssociatedTypesParams(sp, *this, m.params, effects);
            }
            break;
        }
        case HIRType::TAG_ErasedType: {
            break;
        }
        case HIRType::TAG_Array: {
            auto& e = data.as_Array();
            ConvertHIRConstantEvaluateArraySize(sp, this->wb, crate, visPath, e.size);
            e.inner = expandAssociatedTypesInplace(sp, e.inner, effects);
            break;
        }
        case HIRType::TAG_Slice: {
            auto& e = data.as_Slice();
            e.inner = expandAssociatedTypesInplace(sp, e.inner, effects);
            break;
        }
        case HIRType::TAG_Pattern: {
            auto& e = data.as_Pattern();
            e.inner = expandAssociatedTypesInplace(sp, e.inner, effects);
            for (auto& range : e.pattern.alternatives) {
                HIRConstGeneric* values[] = {range.hasStart ? &range.start : nullptr, range.hasEnd ? &range.end : nullptr};
                for (auto* value : values) {
                    if (value) {
                        ConvertHIRConstantEvaluateConstGeneric(sp, this->wb, crate, e.inner, *value);
                    }
                }
            }
            break;
        }
        case HIRType::TAG_Tuple: {
            auto& e = data.as_Tuple();
            for (auto& type : mutRange(e)) {
                type = expandAssociatedTypesInplace(sp, type, effects);
            }
            break;
        }
        case HIRType::TAG_Borrow: {
            auto& e = data.as_Borrow();
            e.inner = expandAssociatedTypesInplace(sp, e.inner, effects);
            break;
        }
        case HIRType::TAG_Pointer: {
            auto& e = data.as_Pointer();
            e.inner = expandAssociatedTypesInplace(sp, e.inner, effects);
            break;
        }
        case HIRType::TAG_NamedFunction: {
            auto& e = data.as_NamedFunction();
            switch (e.path.data.tag()) {
                case HIRPathData::TAG_Generic: {
                    auto& pe = e.path.data.as_Generic();
                    H::expandAssociatedTypesParams(sp, *this, pe.params, effects);
                    break;
                }
                case HIRPathData::TAG_UfcsInherent: {
                    auto& pe = e.path.data.as_UfcsInherent();
                    pe.type = expandAssociatedTypesInplace(sp, pe.type, effects);
                    H::expandAssociatedTypesParams(sp, *this, pe.params, effects);
                    break;
                }
                case HIRPathData::TAG_UfcsKnown: {
                    auto& pe = e.path.data.as_UfcsKnown();
                    pe.type = expandAssociatedTypesInplace(sp, pe.type, effects);
                    H::expandAssociatedTypesParams(sp, *this, pe.params, effects);
                    H::expandAssociatedTypesParams(sp, *this, pe.trait.params, effects);
                    break;
                }
                case HIRPathData::TAG_UfcsUnknown: {
                    auto& pe = e.path.data.as_UfcsUnknown();
                    pe.type = expandAssociatedTypesInplace(sp, pe.type, effects);
                    H::expandAssociatedTypesParams(sp, *this, pe.params, effects);
                    break;
                }
            }
            // TODO: Should this re-populate `def`? Not right now, assuming it's set once only
            break;
        }
        case HIRType::TAG_Function: {
            auto& e = data.as_Function();
            for (auto& type : mutRange(e.argTypes)) {
                type = expandAssociatedTypesInplace(sp, type, effects);
            }
            e.rettype = expandAssociatedTypesInplace(sp, e.rettype, effects);
            break;
        }
        case HIRType::TAG_NodeType: {
            break;
        }
    }
    return crate.types.intern(mv$(data));
}

Unifier::Outcome TraitResolution::relateInherentImplHeader(const Span& sp, const HIRTypeImpl& impl, const HIRType* receiver, HIRPathParams& implParams) const {
    ASSERT_BUG(sp, implParams.types.size() <= impl.params.types.size(), StringView("Too many inherent impl type parameters"));
    implParams.types.reserve(impl.params.types.size());
    while (implParams.types.size() < impl.params.types.size()) {
        implParams.types.push_back(ivars.newIvarTr());
    }
    for (auto& type : implParams.types) {
        if (const auto* infer = type->opt_Infer(); infer && infer->index == ~0u) {
            type = ivars.newIvarTr(infer->tyClass);
        }
    }

    ASSERT_BUG(sp, implParams.values.size() <= impl.params.values.size(), StringView("Too many inherent impl const parameters"));
    implParams.values.reserve(impl.params.values.size());
    while (implParams.values.size() < impl.params.values.size()) {
        implParams.values.push_back(HIRConstGeneric::make_Infer({ivars.newIvarVal()}));
    }
    for (auto& value : implParams.values) {
        if (const auto* infer = value.opt_Infer(); infer && infer->index == ~0u) {
            value = HIRConstGeneric::make_Infer({ivars.newIvarVal()});
        }
    }

    auto monomorph = MonomorphStatePtr(crate.types, receiver, &implParams, nullptr);
    monomorph.setConstevalState(this->board(), HIRItemPath(""));
    const auto candidate = monomorph.monomorphType(sp, impl.type, true);
    Unifier relation(
        sp,
        ivars,
        this,
        {
            .bindRigidValues = true,
            .relateProjectionInputs = true,
        }
    );
    return relation.unify(receiver, candidate);
}

SolverCertainty TraitResolution::evaluateGenericBounds(const Span& sp, const HIRGenericParams& definition, const HIRPathParams& parameters, const Monomorphiser& monomorph, u32 conditionalScope, bool onlyBoundsConstrainingTraitParams) const {
    auto result = SolverCertainty::Proven;
    const auto merge = [&](SolverCertainty certainty) {
        if (certainty == SolverCertainty::NoSolution) {
            result = SolverCertainty::NoSolution;
        } else if (certainty == SolverCertainty::Ambiguous && result == SolverCertainty::Proven) {
            result = SolverCertainty::Ambiguous;
        }
    };
    const auto mergeRelation = [&](Unifier::Outcome outcome) {
        switch (outcome) {
            case Unifier::Outcome::Proven:
                break;
            case Unifier::Outcome::Ambiguous:
                merge(SolverCertainty::Ambiguous);
                break;
            case Unifier::Outcome::Mismatch:
                merge(SolverCertainty::NoSolution);
                break;
        }
    };

    struct ContainsConditionalParameter final: MonomorphiserNop {
        u32 scope;
        mutable bool found = false;

        ContainsConditionalParameter(HIRTypeInterner& types, u32 scope)
            : MonomorphiserNop(types)
            , scope(scope)
        {
        }

        const HIRType* getType(const Span& sp, const HIRGenericRef& generic) const override {
            found |= generic.solverScope == scope;
            return MonomorphiserNop::getType(sp, generic);
        }

        HIRConstGeneric getValue(const Span& sp, const HIRGenericRef& generic) const override {
            found |= generic.solverScope == scope;
            return MonomorphiserNop::getValue(sp, generic);
        }

        bool contains(const Span& sp, const HIRType* type) const {
            found = false;
            (void)this->monomorphType(sp, type, true);
            return found;
        }

        bool contains(const Span& sp, const HIRTraitPath& trait) const {
            found = false;
            (void)this->monomorphTraitpath(sp, trait, true);
            return found;
        }
    } conditional(crate.types, conditionalScope);

    struct ContainsTraitParameter final: MonomorphiserNop {
        mutable bool found = false;

        explicit ContainsTraitParameter(HIRTypeInterner& types)
            : MonomorphiserNop(types)
        {
        }

        const HIRType* getType(const Span& sp, const HIRGenericRef& generic) const override {
            found |= generic.group() == GENERICImpl;
            return MonomorphiserNop::getType(sp, generic);
        }

        HIRConstGeneric getValue(const Span& sp, const HIRGenericRef& generic) const override {
            found |= generic.group() == GENERICImpl;
            return MonomorphiserNop::getValue(sp, generic);
        }

        bool contains(const Span& sp, const HIRType* type) const {
            found = false;
            (void)this->monomorphType(sp, type, true);
            return found;
        }

        bool contains(const Span& sp, const HIRTraitPath& trait) const {
            found = false;
            (void)this->monomorphTraitpath(sp, trait, true);
            return found;
        }
    } containsTraitParameter(crate.types);

    for (const auto& bound : definition.bounds) {
        if (const auto* traitBound = bound.opt_TraitBound()) {
            if (onlyBoundsConstrainingTraitParams) {
                bool constrainsTraitParameter = false;
                for (const auto& associated : traitBound->trait.typeBounds) {
                    constrainsTraitParameter |= containsTraitParameter.contains(sp, associated.second.type);
                }
                if (!constrainsTraitParameter) {
                    continue;
                }
            }
            auto realType = monomorph.monomorphType(sp, traitBound->type, true);
            auto realTrait = monomorph.monomorphTraitpath(sp, traitBound->trait, true);
            realType = this->expandAssociatedTypes(sp, std::move(realType));
            for (auto& argument : realTrait.path.params.types) {
                argument = this->expandAssociatedTypes(sp, std::move(argument));
            }
            for (auto& associated : realTrait.typeBounds) {
                associated.second.type = this->expandAssociatedTypes(sp, std::move(associated.second.type));
            }
            if (conditionalScope && (conditional.contains(sp, realType) || conditional.contains(sp, realTrait))) {
                continue;
            }

            bool sawResponse = false;
            this->solveTraitGoal(sp, realTrait.path.path, realTrait.path.params, realType, [&](SolverResponse response) {
                if (!response.impl) {
                    return false;
                }
                sawResponse = true;
                merge(response.certainty);

                Unifier relation(
                    sp,
                    ivars,
                    this,
                    {
                        .bindRigidValues = true,
                        .relateProjectionInputs = true,
                    }
                );
                const auto relateType = [&](const HIRType* left, const HIRType* right) {
                    if (result == SolverCertainty::NoSolution) {
                        return;
                    }
                    auto normalizedLeft = this->expandAssociatedTypes(sp, left);
                    auto normalizedRight = this->expandAssociatedTypes(sp, right);
                    mergeRelation(relation.unify(normalizedLeft, normalizedRight));
                };
                const auto relateValue = [&](const HIRConstGeneric& left, const HIRConstGeneric& right) {
                    if (result != SolverCertainty::NoSolution) {
                        mergeRelation(relation.unifyValues(left, right));
                    }
                };

                ASSERT_BUG(sp, response.slots.typeInputs.size() == response.slots.types.size(), StringView("Malformed solver type slots in inherent impl bound"));
                for (size_t i = 0; i < response.slots.types.size(); i++) {
                    relateType(response.slots.typeInputs[i], response.slots.types[i]);
                }
                ASSERT_BUG(sp, response.slots.valueInputs.size() == response.slots.values.size(), StringView("Malformed solver value slots in inherent impl bound"));
                for (size_t i = 0; i < response.slots.values.size(); i++) {
                    relateValue(response.slots.valueInputs[i], response.slots.values[i]);
                }
                for (const auto& equality : response.equalities) {
                    relateType(equality.left, equality.right);
                }
                for (const auto& equality : response.valueEqualities) {
                    relateValue(equality.left, equality.right);
                }
                if (!response.obligations.empty()) {
                    merge(SolverCertainty::Ambiguous);
                }

                for (const auto& associated : realTrait.typeBounds) {
                    auto actual = response.impl->getType(crate.types, associated.first.c_str(), associated.second.atyParams);
                    if (actual == nullptr) {
                        actual = crate.types.path(HIRPath(realType, associated.second.sourceTrait.clone(), associated.first, associated.second.atyParams.clone()), HIRTypePathBinding::make_Opaque({}));
                    }
                    actual = this->expandAssociatedTypes(sp, std::move(actual));
                    relateType(associated.second.type, actual);
                }
                return true;
            }, {.allowInferInputs = true, .ambiguity = SolverAmbiguityPolicy::Report});

            if (!sawResponse) {
                bool unresolved = ivars.typeContainsIvars(realType, false) || ivars.pathparamsContainIvars(realTrait.path.params, false);
                for (const auto& associated : realTrait.typeBounds) {
                    unresolved |= ivars.pathparamsContainIvars(associated.second.sourceTrait.params, false) || ivars.pathparamsContainIvars(associated.second.atyParams, false) || ivars.typeContainsIvars(associated.second.type, false);
                }
                if (unresolved) {
                    merge(SolverCertainty::Ambiguous);
                    continue;
                }
                return SolverCertainty::NoSolution;
            }
            if (result == SolverCertainty::NoSolution) {
                return result;
            }
        } else if (const auto* equality = bound.opt_TypeEquality()) {
            if (onlyBoundsConstrainingTraitParams && !containsTraitParameter.contains(sp, equality->type) && !containsTraitParameter.contains(sp, equality->otherType)) {
                continue;
            }
            const auto left = monomorph.monomorphType(sp, equality->type, true);
            const auto right = monomorph.monomorphType(sp, equality->otherType, true);
            if (conditionalScope && (conditional.contains(sp, left) || conditional.contains(sp, right))) {
                continue;
            }
            Unifier relation(
                sp,
                ivars,
                this,
                {
                    .bindRigidValues = true,
                    .relateProjectionInputs = true,
                }
            );
            mergeRelation(relation.unify(left, right));
            if (result == SolverCertainty::NoSolution) {
                return result;
            }
        }
    }

    ASSERT_BUG(sp, parameters.types.size() == definition.types.size(), StringView("Generic bound parameter count mismatch"));
    if (onlyBoundsConstrainingTraitParams) {
        return result;
    }
    for (size_t i = 0; i < definition.types.size(); i++) {
        if (!definition.types[i].isSized) {
            continue;
        }
        const auto* parameter = ivars.getType(parameters.types[i]);
        if (conditionalScope && conditional.contains(sp, parameter)) {
            continue;
        }
        switch (this->typeIsSized(sp, parameter)) {
            case HIRCompare::Equal:
                break;
            case HIRCompare::Fuzzy:
                merge(SolverCertainty::Ambiguous);
                break;
            case HIRCompare::Unequal:
                return SolverCertainty::NoSolution;
        }
    }
    return result;
}

SolverCertainty TraitResolution::evaluateInherentImplBounds(const Span& sp, const HIRTypeImpl& impl, const HIRPathParams& implParams) const {
    auto monomorph = MonomorphStatePtr(crate.types, nullptr, &implParams, nullptr);
    monomorph.setConstevalState(this->board(), HIRItemPath(""));
    return this->evaluateGenericBounds(sp, impl.params, implParams, monomorph);
}

SolverCertainty TraitResolution::evaluateInherentImpl(const Span& sp, const HIRTypeImpl& impl, const HIRType* receiver, HIRPathParams& implParams) const {
    auto result = SolverCertainty::Proven;
    switch (this->relateInherentImplHeader(sp, impl, receiver, implParams)) {
        case Unifier::Outcome::Proven:
            break;
        case Unifier::Outcome::Ambiguous:
            result = SolverCertainty::Ambiguous;
            break;
        case Unifier::Outcome::Mismatch:
            return SolverCertainty::NoSolution;
    }
    const auto bounds = this->evaluateInherentImplBounds(sp, impl, implParams);
    if (bounds == SolverCertainty::NoSolution) {
        return bounds;
    }
    if (bounds == SolverCertainty::Ambiguous) {
        result = bounds;
    }
    return result;
}

SolverCertainty TraitResolution::probeInherentImplHeader(const Span& sp, const HIRTypeImpl& impl, const HIRType* receiver, HIRPathParams& implParams) const {
    const auto snapshot = ivars.snapshot();
    STD_DEFER {
        ivars.rollbackTo(snapshot);
    };

    const HIRType* inferenceReceiver = receiver;
    inferenceReceiver = ivars.addIvars(inferenceReceiver);
    HIRPathParams inferenceParams = implParams.clone();
    SolverCertainty certainty;
    switch (this->relateInherentImplHeader(sp, impl, inferenceReceiver, inferenceParams)) {
        case Unifier::Outcome::Proven:
            certainty = SolverCertainty::Proven;
            break;
        case Unifier::Outcome::Ambiguous:
            certainty = SolverCertainty::Ambiguous;
            break;
        case Unifier::Outcome::Mismatch:
            return SolverCertainty::NoSolution;
    }
    if (certainty != SolverCertainty::NoSolution) {
        implParams = this->materializeImplParams(sp, impl.params, inferenceParams, snapshot.ivarCount, snapshot.valueCount);
    }
    return certainty;
}

InherentImplSelection TraitResolution::selectInherentImpl(const Span& sp, const HIRType* receiver, const RcString& item, InherentItemKind kind, const HIRPathParams* initialParams) const {
    InherentImplSelection selected;
    crate.findTypeImpls(receiver, ivars.callbackResolveInfer(), [&](const HIRTypeImpl& impl) {
        bool hasItem = false;
        switch (kind) {
            case InherentItemKind::Type:
                hasItem = impl.types.count(item) != 0;
                break;
            case InherentItemKind::Method:
                hasItem = impl.methods.count(item) != 0;
                break;
            case InherentItemKind::Value:
                hasItem = impl.methods.count(item) != 0 || impl.constants.count(item) != 0;
                break;
        }
        if (!hasItem) {
            return false;
        }

        HIRPathParams implParams = initialParams ? initialParams->clone() : HIRPathParams();
        if (implParams.types.size() > impl.params.types.size() || implParams.values.size() > impl.params.values.size()) {
            return false;
        }
        const auto certainty = this->probeInherentImplHeader(sp, impl, receiver, implParams);
        if (certainty == SolverCertainty::NoSolution) {
            return false;
        }
        if (selected.certainty != SolverCertainty::NoSolution) {
            selected.certainty = SolverCertainty::Ambiguous;
            selected.impl = nullptr;
            selected.implParams = {};
            return false;
        }
        selected.certainty = SolverCertainty::Proven;
        selected.impl = &impl;
        selected.implParams = std::move(implParams);
        return false;
    });
    return selected;
}

const HIRType* TraitResolution::expandAssociatedTypesInplaceUfcsInherent(const Span& sp, const HIRType* input, SolverResponseCallback* effects) const {
    TRACE_FUNCTION_FR(input, input);
    ASSERT_BUG(sp, input->is_Path() && input->as_Path().path.data.is_UfcsInherent(), input);

    const auto& pe = input->as_Path().path.data.as_UfcsInherent();
    auto selection = this->selectInherentImpl(sp, pe.type, pe.item, InherentItemKind::Type);
    if (selection.certainty != SolverCertainty::Proven || !selection.impl) {
        DEBUG(StringView("No proven inherent associated type candidate for ") << input);
        return nullptr;
    }
    const auto& selectedImpl = *selection.impl;
    const auto& alias = selectedImpl.types.at(pe.item).data;
    auto implParams = pe.implParams.clone();
    ASSERT_BUG(sp, this->relateInherentImplHeader(sp, selectedImpl, pe.type, implParams) != Unifier::Outcome::Mismatch, StringView("Selected inherent associated type impl no longer matches ") << pe.type);
    ConvertHIRConstantEvaluateMethodParams(sp, this->wb, crate, &selectedImpl.params, implParams);
    if (effects) {
        auto selectedType = MonomorphStatePtr(crate.types, nullptr, &implParams, nullptr).monomorphType(sp, selectedImpl.type);
        SolverResponse response;
        response.certainty = SolverCertainty::Proven;
        response.equalities.push_back(SolverTypeEquality{pe.type, selectedType});
        effects->visit(std::move(response));
    }

    auto itemParams = pe.params.clone();
    if (itemParams.types.size() != alias.params.types.size() || itemParams.values.size() != alias.params.values.size()) {
        ERROR(sp, E0000, StringView("Incorrect generic arguments for inherent associated type ") << input);
    }
    ConvertHIRConstantEvaluateMethodParams(sp, this->wb, crate, &alias.params, itemParams);

    return MonomorphStatePtr(crate.types, pe.type, &implParams, &itemParams).monomorphType(sp, alias.type);
}

const HIRType* TraitResolution::expandAssociatedTypesInplaceUfcsKnown(const Span& sp, const HIRType* input, SolverResponseCallback* effects) const {
    ASSERT_BUG(sp, input->is_Path() && input->as_Path().path.data.is_UfcsKnown(), input);

    bool normalized = false;
    this->solveNormalizesTo(sp, NormalizesTo{input}, [&](NormalizesToResponse response) {
        if (response.output == nullptr || response.output == input) {
            return true;
        }
        if (effects) {
            effects->visit(std::move(response.effects));
        }
        input = std::move(response.output);
        normalized = true;
        return true;
    });
    if (normalized) {
        return this->expandAssociatedTypesInplace(sp, input, effects);
    }

    if (!this->ivars.typeContainsIvars(input, false)) {
        auto data = input->cloneData();
        data.as_Path().binding = HIRTypePathBinding::make_Opaque({});
        input = crate.types.intern(std::move(data));
    }
    return input;
}

bool TraitResolution::findNamedTraitInTraitCb(const Span& sp, const HIRSimplePath& des, const HIRPathParams& desParams, const HIRTrait& traitPtr, const HIRSimplePath& traitPath, const HIRPathParams& pp, const HIRType* targetType, TraitPathCallback& callback) const {
    TRACE_FUNCTION_F(des << desParams << StringView(" in ") << traitPath << pp);
    if (pp.types.size() != traitPtr.params.types.size()) {
        BUG(sp, StringView("Incorrect number of parameters for trait ") << traitPath);
    }

    auto monomorphCb = MonomorphStatePtr(crate.types, targetType, &pp, nullptr);
    for (const auto& pt : traitPtr.allParentTraits) {
        auto ptMono = monomorphCb.monomorphTraitpath(sp, pt, false);
        for (auto& ty : ptMono.path.params.types) {
            ty = this->expandAssociatedTypes(sp, mv$(ty));
        }
        for (auto& ty : ptMono.typeBounds) {
            ty.second.type = this->expandAssociatedTypes(sp, mv$(ty.second.type));
        }

        if (pt.path.path == des) {
            DEBUG(StringView("Found potential ") << ptMono);
            if (callback.visit(ptMono)) {
                return true;
            }
        }
    }

    return false;
}

bool TraitResolution::assembleParamEnvCandidatesCb(const Span& sp, const HIRSimplePath& trait, const HIRPathParams& params, const HIRType* type, AssembledImplCallback& callback) const {
    TRACE_FUNCTION_F(StringView("trait = ") << trait << params << StringView(", type = ") << type);
    const HIRPath::Data::Data_UfcsKnown* assocInfo = nullptr;
    if (const auto* e = type->opt_Path()) {
        assocInfo = e->path.data.opt_UfcsKnown();
    }

    // - TODO: Determine if the params could provide enough info to be worth checking for bounds.
    if (type->is_Infer() && !type->as_Infer().isLit() && !isSolverCanonicalInfer(type->as_Infer().index)) {
        return false;
    }

    // TODO: A bound can imply something via its associated types. How deep can this go?

    for (const auto& bound : traitBounds) {
        if (bound.first.second.path != trait) {
            continue;
        }
        if (callback.visit(SolverImpl(bound.first.first, &bound.first.second.params, &bound.second.assoc, bound.second.constness))) {
            return true;
        }
    }

    auto visitDeclaredTrait = [&](auto&& visit, const HIRType* subject, const HIRTraitPath& declaredTrait, bool matchCurrent) -> bool {
        if (matchCurrent && declaredTrait.path.path == trait) {
            auto response = declaredTrait.clone();
            if (callback.visit(SolverImpl(subject, mv$(response.path.params), mv$(response.typeBounds), response.constness))) {
                return true;
            }
        }

        for (const auto& associated : declaredTrait.traitBounds) {
            auto nestedSubject = crate.types.path(HIRPath(subject, associated.second.sourceTrait.clone(), associated.first, associated.second.atyParams.clone()), HIRTypePathBinding::make_Opaque({}));
            for (const auto& nestedTrait : associated.second.traits) {
                if (visit(visit, nestedSubject, nestedTrait, true)) {
                    return true;
                }
            }
        }
        return false;
    };
    auto declaredTraitMayMatch = [&](auto&& visit, const HIRTraitPath& declaredTrait, bool matchCurrent) -> bool {
        if (matchCurrent && declaredTrait.path.path == trait) {
            return true;
        }
        for (const auto& associated : declaredTrait.traitBounds) {
            for (const auto& nestedTrait : associated.second.traits) {
                if (visit(visit, nestedTrait, true)) {
                    return true;
                }
            }
        }
        return false;
    };

    for (const auto& environment : traitBounds) {
        const auto& environmentType = environment.first.first;
        const auto& environmentTrait = environment.first.second;
        const auto& environmentInfo = environment.second;
        ASSERT_BUG(sp, environmentInfo.traitPtr, StringView("Cached trait bound has no trait definition"));

        auto monomorph = MonomorphStatePtr(crate.types, environmentType, &environmentTrait.params, nullptr);
        for (const auto& declaredBound : environmentInfo.traitPtr->params.bounds) {
            const auto* declaredTrait = declaredBound.opt_TraitBound();
            if (!declaredTrait) {
                continue;
            }
            if (!declaredTraitMayMatch(declaredTraitMayMatch, declaredTrait->trait, true)) {
                continue;
            }

            auto impliedType = monomorph.monomorphType(sp, declaredTrait->type);
            auto impliedTrait = monomorph.monomorphTraitpath(sp, declaredTrait->trait, false);
            const auto* impliedPath = impliedType->opt_Path();
            const auto* impliedProjection = impliedPath ? impliedPath->path.data.opt_UfcsKnown() : nullptr;
            if (!impliedProjection || !visitTyWith(impliedProjection->type, [](const HIRType* inner) {
                const auto* path = inner->opt_Path();
                return path && path->path.data.is_UfcsKnown();
            })) {
                continue;
            }
            if (visitDeclaredTrait(visitDeclaredTrait, impliedType, impliedTrait, true)) {
                return true;
            }
        }

        for (const auto& associated : environmentInfo.traitPtr->types) {
            const auto& definition = associated.second;
            HIRPathParams noAssociatedParams;
            const HIRPathParams* associatedParams = &noAssociatedParams;
            if (definition.generics.isGeneric()) {
                if (!assocInfo || assocInfo->item != associated.first || assocInfo->params.types.size() != definition.generics.types.size() || assocInfo->params.values.size() != definition.generics.values.size()) {
                    continue;
                }
                associatedParams = &assocInfo->params;
            }
            if (std::none_of(definition.traitBounds.begin(), definition.traitBounds.end(), [&](const auto& declaredTrait) {
                return declaredTraitMayMatch(declaredTraitMayMatch, declaredTrait, false);
            })) {
                continue;
            }
            auto associatedType = crate.types.path(HIRPath(environmentType, environmentTrait.clone(), associated.first, associatedParams->clone()), HIRTypePathBinding::make_Opaque({}));
            monomorph.ppMethod = associatedParams;
            bool found = false;
            for (const auto& declaredTrait : definition.traitBounds) {
                if (!declaredTraitMayMatch(declaredTraitMayMatch, declaredTrait, false)) {
                    continue;
                }
                auto impliedTrait = monomorph.monomorphTraitpath(sp, declaredTrait, false);
                if (visitDeclaredTrait(visitDeclaredTrait, associatedType, impliedTrait, false)) {
                    found = true;
                    break;
                }
            }
            monomorph.ppMethod = nullptr;
            if (found) {
                return true;
            }
        }
    }

    if (assocInfo) {
        bool rv = this->iterateBoundsTraits(sp, assocInfo->type, assocInfo->trait.path, [&](HIRCompare cmp, const HIRType* boundTy, const HIRGenericPath& boundTrait, const CachedBound& boundInfo) -> bool {
            if (cmp == HIRCompare::Unequal) {
                return false;
            }

            const auto& traitRef = *boundInfo.traitPtr;
            const auto& at = traitRef.types.at(assocInfo->item);
            for (const auto& bound : at.traitBounds) {
                if (bound.path.path == trait) {
                    auto monomorphCb = MonomorphStatePtr(crate.types, boundTy, &boundTrait.params, &assocInfo->params);

                    auto tpMono = monomorphCb.monomorphTraitpath(sp, bound, false);
                    this->expandAssociatedTypesParams(sp, tpMono.path.params);
                    for (auto& ty : tpMono.typeBounds) {
                        ty.second.type = this->expandAssociatedTypes(sp, mv$(ty.second.type));
                    }
                    auto projectedSubject = crate.types.path(HIRPath(boundTy, boundTrait.clone(), assocInfo->item, assocInfo->params.clone()), HIRTypePathBinding::make_Opaque({}));
                    if (callback.visit(SolverImpl(std::move(projectedSubject), mv$(tpMono.path.params), mv$(tpMono.typeBounds), tpMono.constness))) {
                        return true;
                    }
                }
            }
            return false;
        });
        if (rv) {
            return true;
        }
    }
    return false;
}

const HIRFunction* TraitResolution::traitContainsMethod(const Span& sp, const HIRGenericPath& traitPath, const HIRTrait& traitPtr, const HIRType* self, const RcString& name, HIRGenericPath& outPath) const {
    TRACE_FUNCTION_FR(StringView("trait_path=") << traitPath << StringView(",name=") << name, outPath);
    const HIRFunction* rv = nullptr;

    if (traitContainsMethodInner(traitPtr, name, rv)) {
        BUG_ASSERT(rv);
        outPath = traitPath.clone();
        return rv;
    }

    auto monomorphCb = MonomorphStatePtr(crate.types, self, &traitPath.params, nullptr);
    for (const auto& st : traitPtr.allParentTraits) {
        if (traitContainsMethodInner(*st.traitPtr, name, rv)) {
            BUG_ASSERT(rv);
            outPath.path = st.path.path;
            outPath.params = monomorphCb.monomorphPathParams(sp, st.path.params, false);
            return rv;
        }
    }
    return nullptr;
}

bool TraitResolution::traitContainsType(const Span& sp, const HIRGenericPath& traitPath, const HIRTrait& traitPtr, const char* name, HIRGenericPath& outPath) const {
    TRACE_FUNCTION_FR(traitPath << StringView(" has ") << name, outPath);
    auto it = traitPtr.types.find(name);
    if (it != traitPtr.types.end()) {
        DEBUG(StringView("- Found in cur"));
        outPath = traitPath.clone();
        return true;
    }

    auto monomorphCb = MonomorphStatePtr(crate.types, nullptr, &traitPath.params, nullptr);
    for (const auto& st : traitPtr.allParentTraits) {
        if (st.traitPtr->types.count(name)) {
            DEBUG(StringView("- Found in ") << st);
            outPath.path = st.path.path;
            outPath.params = monomorphCb.monomorphPathParams(sp, st.path.params, false);
            return true;
        }
    }
    return false;
}

HIRCompare TraitResolution::typeIsSized(const Span& sp, const HIRType* ty) const {
    const auto& type = this->ivars.getType(ty);
    if (!langSized().components().empty()) {
        switch (solveNonBuiltinTraitGoal(sp, langSized(), type)) {
            case SolverCertainty::Proven:
                return HIRCompare::Equal;
            case SolverCertainty::Ambiguous:
                return HIRCompare::Fuzzy;
            case SolverCertainty::NoSolution:
                break;
        }
    }
    return typeIsSizedBuiltin(sp, type);
}

HIRCompare TraitResolution::typeIsSizedBuiltin(const Span& sp, const HIRType* ty) const {
    const auto& type = this->ivars.getType(ty);
    switch ((*type).tag()) {
        default:
            break;
        case HIRType::TAG_Infer: {
            auto& e = (*type).as_Infer();
            switch (e.tyClass) {
                case HIRInferClass::Integer:
                case HIRInferClass::Float:
                    return HIRCompare::Equal;
                default:
                    return HIRCompare::Fuzzy;
            }
            break;
        }
        case HIRType::TAG_Primitive: {
            auto& e = (*type).as_Primitive();
            if (e == HIRCoreType::Str) {
                return HIRCompare::Unequal;
            }
            break;
        }
        case HIRType::TAG_Slice: {
            return HIRCompare::Unequal;
        }
        case HIRType::TAG_Path: {
            auto& e = (*type).as_Path();
            // TODO: Check that only ?Sized parameters are !Sized
            switch (e.binding.tag()) {
                case HIRTypePathBinding::TAG_Unbound: {
                    break;
                }
                case HIRTypePathBinding::TAG_Opaque: {
                    if (const auto* pe = e.path.data.opt_UfcsKnown()) {
                        const auto& trait = crate.getTraitByPath(sp, pe->trait.path);
                        const auto* aty = trait.getAtyDef(pe->item).first;
                        if (aty && !aty->isSized) {
                            return HIRCompare::Unequal;
                        }
                    }
                    break;
                }
                case HIRTypePathBinding::TAG_ExternType: {
                    return HIRCompare::Unequal;
                }
                case HIRTypePathBinding::TAG_Enum: {
                    break;
                }
                case HIRTypePathBinding::TAG_Union: {
                    break;
                }
                case HIRTypePathBinding::TAG_Struct: {
                    auto& pb = e.binding.as_Struct();
                    switch (pb->structMarkings.dstType) {
                        case HIRStructMarkings::DstType::None:
                            break;
                        case HIRStructMarkings::DstType::Possible:
                            return typeIsSized(sp, e.path.data.as_Generic().params.types.at(pb->structMarkings.unsizedParam));
                        case HIRStructMarkings::DstType::Projection: {
                            const HIRType* tailTpl = nullptr;
                            switch (pb->data.tag()) {
                                case HIRStructData::TAG_Unit:
                                    BUG(sp, StringView("Potentially-unsized unit struct ") << type);
                                case HIRStructData::TAG_Tuple:
                                    tailTpl = pb->data.as_Tuple().at(pb->structMarkings.unsizedField).ent;
                                    break;
                                case HIRStructData::TAG_Named:
                                    tailTpl = pb->data.as_Named().at(pb->structMarkings.unsizedField).ty;
                                    break;
                            }
                            const auto& params = e.path.data.as_Generic().params;
                            auto tailTy = MonomorphStatePtr(crate.types, type, &params, nullptr).monomorphType(sp, tailTpl);
                            tailTy = this->expandAssociatedTypes(sp, mv$(tailTy));
                            return typeIsSized(sp, tailTy);
                        }
                        case HIRStructMarkings::DstType::Slice:
                        case HIRStructMarkings::DstType::TraitObject:
                            return HIRCompare::Unequal;
                    }
                    break;
                }
            }
            break;
        }
        case HIRType::TAG_Generic: {
            auto& e = (*type).as_Generic();
            switch (e.group()) {
                case 0:
                    if (!this->implGenerics_) {
                        return HIRCompare::Fuzzy;
                    }
                    return this->implGenerics_->types.at(e.idx()).isSized ? HIRCompare::Equal : HIRCompare::Unequal;
                case 1:
                    if (!this->itemGenerics_) {
                        return HIRCompare::Fuzzy;
                    }
                    return this->itemGenerics_->types.at(e.idx()).isSized ? HIRCompare::Equal : HIRCompare::Unequal;
                default:
                    return HIRCompare::Equal;
            }
            break;
        }
        case HIRType::TAG_ErasedType: {
            auto& e = (*type).as_ErasedType();
            return e.isSized ? HIRCompare::Equal : HIRCompare::Unequal;
        }
        case HIRType::TAG_TraitObject: {
            return HIRCompare::Unequal;
        }
    }
    return HIRCompare::Equal;
}

HIRCompare TraitResolution::typeIsCopy(const Span& sp, const HIRType* ty) const {
    const auto& type = this->ivars.getType(ty);
    if (langCopy().components().empty()) {
        return typeIsCopyBuiltin(sp, type);
    }
    switch (solveNonBuiltinTraitGoal(sp, langCopy(), type)) {
        case SolverCertainty::Proven:
            return HIRCompare::Equal;
        case SolverCertainty::Ambiguous:
            return HIRCompare::Fuzzy;
        case SolverCertainty::NoSolution:
            break;
    }
    if (type->is_Path() && type->as_Path().binding.is_Unbound()) {
        return HIRCompare::Fuzzy;
    }
    return typeIsCopyBuiltin(sp, type);
}

HIRCompare TraitResolution::typeIsCopyBuiltin(const Span& sp, const HIRType* ty) const {
    const auto& type = this->ivars.getType(ty);
    switch ((*type).tag()) {
        default: {
            return HIRCompare::Unequal;
        } break;
        case HIRType::TAG_Infer: {
            auto& e = (*type).as_Infer();
            switch (e.tyClass) {
                case HIRInferClass::Integer:
                case HIRInferClass::Float:
                    return HIRCompare::Equal;
                default:
                    return HIRCompare::Fuzzy;
            }
            break;
        }
        case HIRType::TAG_Generic: {
            // TODO: Store this result - or even pre-calculate it.
            return this->iterateBoundsTraits(
                       sp,
                       ty,
                       langCopy(),
                       [&](HIRCompare _cmp, const HIRType* beType, const HIRGenericPath& beTrait, const CachedBound& info) -> bool {
                return true;
            }
                   )
                       ? HIRCompare::Equal
                       : HIRCompare::Unequal;
            break;
        }
        case HIRType::TAG_Primitive: {
            auto& e = (*type).as_Primitive();
            if (e == HIRCoreType::Str) {
                return HIRCompare::Unequal;
            }
            return HIRCompare::Equal;
        }
        case HIRType::TAG_Borrow: {
            auto& e = (*type).as_Borrow();
            return e.type == HIRBorrowType::Shared ? HIRCompare::Equal : HIRCompare::Unequal;
        }
        case HIRType::TAG_Pointer: {
            return HIRCompare::Equal;
        }
        case HIRType::TAG_Tuple: {
            auto& e = (*type).as_Tuple();
            auto rv = HIRCompare::Equal;
            for (const auto& sty : e) {
                rv &= typeIsCopy(sp, sty);
            }
            return rv;
        }
        case HIRType::TAG_Slice: {
            return HIRCompare::Unequal;
        }
        case HIRType::TAG_NamedFunction: {
            return HIRCompare::Equal;
        }
        case HIRType::TAG_Function: {
            return HIRCompare::Equal;
        }
        case HIRType::TAG_NodeType: {
            return HIRCompare::Equal;
        }
        case HIRType::TAG_Array: {
            auto& e = (*type).as_Array();
            return typeIsCopy(sp, e.inner);
        }
    }
    UNREACHABLE();
}

HIRCompare TraitResolution::typeIsClone(const Span& sp, const HIRType* ty) const {
    TRACE_FUNCTION_F(ty);
    const auto& type = this->ivars.getType(ty);
    switch ((*type).tag()) {
        default: {
            if (type->is_Path() && type->as_Path().isClosure()) {
                return HIRCompare::Equal;
            }
            switch (solveNonBuiltinTraitGoal(sp, langClone(), ty)) {
                case SolverCertainty::Proven:
                    return HIRCompare::Equal;
                case SolverCertainty::Ambiguous:
                    return HIRCompare::Fuzzy;
                case SolverCertainty::NoSolution:
                    return HIRCompare::Unequal;
            }
            UNREACHABLE();
        } break;
        case HIRType::TAG_Infer: {
            auto& e = (*type).as_Infer();
            switch (e.tyClass) {
                case HIRInferClass::Integer:
                case HIRInferClass::Float:
                    return HIRCompare::Equal;
                default:
                    return HIRCompare::Fuzzy;
            }
            break;
        }
        case HIRType::TAG_Generic: {
            // TODO: Store this result - or even pre-calculate it.
            return this->iterateBoundsTraits(
                       sp,
                       ty,
                       langClone(),
                       [&](HIRCompare _cmp, const HIRType* beType, const HIRGenericPath& beTrait, const CachedBound& info) -> bool {
                return true;
            }
                   )
                       ? HIRCompare::Equal
                       : HIRCompare::Unequal;
            break;
        }
        case HIRType::TAG_Primitive: {
            auto& e = (*type).as_Primitive();
            if (e == HIRCoreType::Str) {
                return HIRCompare::Unequal;
            }
            return HIRCompare::Equal;
        }
        case HIRType::TAG_Borrow: {
            auto& e = (*type).as_Borrow();
            return e.type == HIRBorrowType::Shared ? HIRCompare::Equal : HIRCompare::Unequal;
        }
        case HIRType::TAG_Pointer: {
            return HIRCompare::Equal;
        }
        case HIRType::TAG_Tuple: {
            auto& e = (*type).as_Tuple();
            auto rv = HIRCompare::Equal;
            for (const auto& sty : e) {
                rv &= typeIsClone(sp, sty);
            }
            return rv;
        }
        case HIRType::TAG_Slice: {
            return HIRCompare::Unequal;
        }
        case HIRType::TAG_NamedFunction: {
            return HIRCompare::Equal;
        }
        case HIRType::TAG_Function: {
            return HIRCompare::Equal;
        }
        case HIRType::TAG_NodeType: {
            // TODO: Determine captures earlier and check captures here
            return HIRCompare::Equal;
        }
        case HIRType::TAG_Array: {
            auto& e = (*type).as_Array();
            return typeIsClone(sp, e.inner);
        }
    }
    UNREACHABLE();
}

SolverCoercionResponse TraitResolution::evaluateCoercionGoal(const Span& sp, const HIRType* destination, const HIRType* source, SolverCoercionOp op, bool allowSourceAutoderef) const {
    SolverCoercionResponse result;
    TRACE_FUNCTION_FR(
        StringView("dst=") << destination << StringView(", src=") << source << StringView(", op=") << static_cast<unsigned>(op),
        StringView("certainty=") << static_cast<unsigned>(result.effects.certainty) << StringView(", relation=") << static_cast<unsigned>(result.relation) << StringView(", equalities=") << result.effects.equalities.size() << StringView(", obligations=") << result.effects.obligations.size() << StringView(", deferred=") << result.deferred.size()
    );
    auto normalizationCertainty = SolverCertainty::Proven;
    auto normalizationEffects = makeCallable<SolverResponseCb>([&](SolverResponse response) {
        if (response.certainty == SolverCertainty::NoSolution) {
            normalizationCertainty = SolverCertainty::NoSolution;
        } else if (response.certainty == SolverCertainty::Ambiguous && normalizationCertainty == SolverCertainty::Proven) {
            normalizationCertainty = SolverCertainty::Ambiguous;
        }
        for (size_t i = 0; i < response.slots.types.size(); i++) {
            result.effects.slots.typeInputs.push_back(std::move(response.slots.typeInputs[i]));
            result.effects.slots.types.push_back(std::move(response.slots.types[i]));
        }
        for (size_t i = 0; i < response.slots.values.size(); i++) {
            result.effects.slots.valueInputs.push_back(std::move(response.slots.valueInputs[i]));
            result.effects.slots.values.push_back(std::move(response.slots.values[i]));
        }
        for (auto& obligation : response.obligations) {
            result.effects.obligations.push_back(std::move(obligation));
        }
        for (auto& equality : response.equalities) {
            result.effects.equalities.push_back(std::move(equality));
        }
        for (auto& equality : response.valueEqualities) {
            result.effects.valueEqualities.push_back(std::move(equality));
        }
        return false;
    });
    const auto normalize = [&](const HIRType* type) {
        type = ivars.getType(type);
        return type->mayHaveAssociatedType() ? expandAssociatedTypes(sp, type, &normalizationEffects) : type;
    };
    const auto* normalizedDestination = normalize(destination);
    const auto* normalizedSource = normalize(source);
    const auto coercionCertainty = evaluateCoercionConstraint(
        sp,
        SolverCoercionConstraint{
            0,
            normalizedDestination,
            SolverCoercionConstraint::Direction::InputIsSource,
            op,
            false,
            false,
            allowSourceAutoderef,
        },
        normalizedSource,
        nullptr,
        &result.effects,
        &result.deferred,
        &result.reachedAutoderefLimit
    );
    result.effects.certainty = coercionCertainty == SolverCertainty::NoSolution || normalizationCertainty == SolverCertainty::NoSolution
        ? SolverCertainty::NoSolution
        : coercionCertainty == SolverCertainty::Ambiguous || normalizationCertainty == SolverCertainty::Ambiguous
        ? SolverCertainty::Ambiguous
        : SolverCertainty::Proven;
    const auto* resolvedDestination = normalizedDestination;
    const auto* resolvedSource = normalizedSource;
    const auto* object = resolvedDestination->opt_TraitObject();
    const auto* closure = resolvedSource->is_NodeType() ? resolvedSource->as_NodeType().opt_Closure() : nullptr;
    if (object && closure && (object->trait.path.path == langFn() || object->trait.path.path == langFnMut() || object->trait.path.path == langFnOnce())) {
        const HIRType* expectedOutput = nullptr;
        for (const auto& associated : object->trait.typeBounds) {
            if (associated.first == "Output") {
                expectedOutput = associated.second.type;
                break;
            }
        }
        if (expectedOutput) {
            const auto* closureOutput = (*closure)->returnType;
            size_t kept = 0;
            for (size_t i = 0; i < result.effects.equalities.size(); i++) {
                auto& equality = result.effects.equalities[i];
                const bool outputEquality = (equality.left == closureOutput && equality.right == expectedOutput) || (equality.left == expectedOutput && equality.right == closureOutput);
                if (outputEquality) {
                    result.deferred.push_back(SolverDeferredCoercion{expectedOutput, closureOutput, SolverCoercionOp::Coercion});
                    continue;
                }
                if (kept != i) {
                    result.effects.equalities[kept] = std::move(equality);
                }
                kept++;
            }
            result.effects.equalities.resize(kept);
        }
    }
    if (result.effects.certainty == SolverCertainty::Proven) {
        if (op == SolverCoercionOp::Coercion && resolvedSource->is_Diverge()) {
            result.relation = SolverCoercionRelation::Coercion;
            return result;
        }
        const auto snapshot = ivars.snapshot();
        Unifier unifier(sp, ivars, this);
        const auto equality = unifier.unify(normalizedDestination, normalizedSource);
        ivars.rollbackTo(snapshot);
        if (equality == Unifier::Outcome::Proven) {
            result.relation = SolverCoercionRelation::Equality;
        } else {
            const auto* destinationPath = resolvedDestination->opt_Path();
            const auto* sourcePath = resolvedSource->opt_Path();
            const bool structuralSubtype = op == SolverCoercionOp::Coercion
                && destinationPath
                && sourcePath
                && destinationPath->binding.is_Struct()
                && sourcePath->binding.is_Struct()
                && destinationPath->binding.as_Struct() == sourcePath->binding.as_Struct()
                && sourcePath->binding.as_Struct()->structMarkings.coerceUnsized == HIRStructMarkings::Coerce::None;
            result.relation = structuralSubtype ? SolverCoercionRelation::Subtype : SolverCoercionRelation::Coercion;
        }
    }
    return result;
}

SolverCertainty TraitResolution::evaluateCoercionConstraint(const Span& sp, const SolverCoercionConstraint& constraint, const HIRType* input, ThinVector<SolverTypeEquality>* equalities, SolverResponse* effects, ThinVector<SolverDeferredCoercion>* deferred, bool* reachedAutoderefLimit) const {
    const auto appendEffects = [](SolverResponse& destination, SolverResponse source) {
        for (size_t i = 0; i < source.slots.types.size(); i++) {
            destination.slots.typeInputs.push_back(std::move(source.slots.typeInputs[i]));
            destination.slots.types.push_back(std::move(source.slots.types[i]));
        }
        for (size_t i = 0; i < source.slots.values.size(); i++) {
            destination.slots.valueInputs.push_back(std::move(source.slots.valueInputs[i]));
            destination.slots.values.push_back(std::move(source.slots.values[i]));
        }
        for (auto& obligation : source.obligations) {
            destination.obligations.push_back(std::move(obligation));
        }
        for (auto& equality : source.equalities) {
            destination.equalities.push_back(std::move(equality));
        }
        for (auto& equality : source.valueEqualities) {
            destination.valueEqualities.push_back(std::move(equality));
        }
    };
    const auto resolveKnown = [&](const HIRType* type) {
        return ivars.getType(type);
    };
    if (constraint.inputRequiresSized && typeIsSized(sp, resolveKnown(input)) == HIRCompare::Unequal) {
        return SolverCertainty::NoSolution;
    }
    const auto isRigidUnsized = [&](const auto& self, const HIRType* rawType) -> bool {
        const auto* type = resolveKnown(rawType);
        if (type->is_Slice() || type->is_TraitObject() || (type->is_Primitive() && type->as_Primitive() == HIRCoreType::Str)) {
            return true;
        }
        const auto* path = type->opt_Path();
        if (!path) {
            return false;
        }
        if (path->binding.is_ExternType()) {
            return true;
        }
        if (!path->binding.is_Struct() || !path->path.data.is_Generic()) {
            return false;
        }
        const auto* definition = path->binding.as_Struct();
        const auto& markings = definition->structMarkings;
        if (markings.dstType == HIRStructMarkings::DstType::Slice || markings.dstType == HIRStructMarkings::DstType::TraitObject) {
            return true;
        }
        const auto& params = path->path.data.as_Generic().params;
        if (markings.dstType == HIRStructMarkings::DstType::Possible) {
            ASSERT_BUG(sp, markings.unsizedParam < params.types.size(), StringView("Malformed unsized struct markings"));
            return self(self, params.types[markings.unsizedParam]);
        }
        if (markings.dstType != HIRStructMarkings::DstType::Projection) {
            return false;
        }
        const HIRType* tail = nullptr;
        switch (definition->data.tag()) {
            case HIRStructData::TAG_Unit:
                return false;
            case HIRStructData::TAG_Tuple:
                ASSERT_BUG(sp, markings.unsizedField < definition->data.as_Tuple().size(), StringView("Malformed unsized struct field marking"));
                tail = definition->data.as_Tuple()[markings.unsizedField].ent;
                break;
            case HIRStructData::TAG_Named:
                ASSERT_BUG(sp, markings.unsizedField < definition->data.as_Named().size(), StringView("Malformed unsized struct field marking"));
                tail = definition->data.as_Named()[markings.unsizedField].ty;
                break;
        }
        auto concreteTail = expandAssociatedTypes(sp, MonomorphStatePtr(crate.types, type, &params, nullptr).monomorphType(sp, tail));
        return self(self, concreteTail);
    };
    const auto relateUsing = [&](auto&& run) {
        const auto snapshot = ivars.snapshot();
        Unifier unifier(sp, ivars, this);
        const auto outcome = run(unifier);
        ivars.rollbackTo(snapshot);
        if (outcome == Unifier::Outcome::Mismatch) {
            return SolverCertainty::NoSolution;
        }

        const auto isDefiningOpaque = [&](const HIRType* type) {
            const auto* erased = type->opt_ErasedType();
            if (!erased) {
                return false;
            }
            if (const auto* alias = erased->inner.opt_Alias()) {
                return this->isOpaqueAliasDefiningScope(*alias->inner);
            }
            if (const auto* function = erased->inner.opt_Fcn()) {
                return this->isDefiningFcnOrigin(function->origin);
            }
            return false;
        };
        const auto pendingIsResponseEquality = [&](const Unifier::PendingEquality& pending) {
            const auto* leftInfer = pending.left->opt_Infer();
            const auto* rightInfer = pending.right->opt_Infer();
            if ((leftInfer && isSolverCanonicalInfer(leftInfer->index)) || (rightInfer && isSolverCanonicalInfer(rightInfer->index))) {
                return true;
            }
            return isDefiningOpaque(pending.left) != isDefiningOpaque(pending.right);
        };
        const auto pendingIsDeferredProjectionEquality = [&](const Unifier::PendingEquality& pending) {
            const auto* leftPath = pending.left->opt_Path();
            const auto* rightPath = pending.right->opt_Path();
            const auto* leftProjection = leftPath ? leftPath->path.data.opt_UfcsKnown() : nullptr;
            const auto* rightProjection = rightPath ? rightPath->path.data.opt_UfcsKnown() : nullptr;
            const bool leftRigid = leftPath && (leftPath->binding.is_Unbound() || leftPath->binding.is_Opaque());
            const bool rightRigid = rightPath && (rightPath->binding.is_Unbound() || rightPath->binding.is_Opaque());
            if (!leftRigid || !rightRigid || !leftProjection || !rightProjection || leftProjection->trait.path != rightProjection->trait.path || leftProjection->item != rightProjection->item) {
                return false;
            }
            const auto typesMayRelate = [&](const HIRType* left, const HIRType* right) {
                return left == right || left->equalsIgnoringRegions(right) || ivars.getType(left)->is_Infer() || ivars.getType(right)->is_Infer();
            };
            const auto paramsMayRelate = [&](const HIRPathParams& left, const HIRPathParams& right) {
                if (left.types.size() != right.types.size() || left.values.size() != right.values.size()) {
                    return false;
                }
                for (size_t i = 0; i < left.types.size(); i++) {
                    if (left.types[i] != right.types[i] && !left.types[i]->equalsIgnoringRegions(right.types[i])) {
                        return false;
                    }
                }
                for (size_t i = 0; i < left.values.size(); i++) {
                    if (left.values[i] != right.values[i]) {
                        return false;
                    }
                }
                return true;
            };
            return (ivars.getType(leftProjection->type)->is_Infer() || ivars.getType(rightProjection->type)->is_Infer())
                && typesMayRelate(leftProjection->type, rightProjection->type)
                && paramsMayRelate(leftProjection->trait.params, rightProjection->trait.params)
                && paramsMayRelate(leftProjection->params, rightProjection->params);
        };

        bool ambiguous = outcome == Unifier::Outcome::Ambiguous && unifier.pending().length() == 0 && unifier.pendingValues().empty();
        for (const auto& pending : unifier.pending()) {
            if (!pendingIsResponseEquality(pending) && !pendingIsDeferredProjectionEquality(pending)) {
                ambiguous = true;
            }
        }
        for (const auto& pending : unifier.pendingValues()) {
            const auto* leftInfer = pending.left.opt_Infer();
            const auto* rightInfer = pending.right.opt_Infer();
            if (!((leftInfer && isSolverCanonicalInfer(leftInfer->index)) || (rightInfer && isSolverCanonicalInfer(rightInfer->index)))) {
                ambiguous = true;
            }
        }

        const auto appendEquality = [&](const HIRType* equalityLeft, const HIRType* equalityRight) {
            if (equalities) {
                equalities->push_back(SolverTypeEquality{equalityLeft, equalityRight});
            }
            if (effects) {
                effects->equalities.push_back(SolverTypeEquality{equalityLeft, equalityRight});
            }
        };
        for (const auto& binding : unifier.bindings()) {
            appendEquality(binding.left, binding.right);
        }
        for (const auto& pending : unifier.pending()) {
            if (pendingIsResponseEquality(pending) || pendingIsDeferredProjectionEquality(pending)) {
                appendEquality(pending.left, pending.right);
            }
        }
        if (effects) {
            for (const auto& binding : unifier.valueBindings()) {
                effects->valueEqualities.push_back(SolverValueEquality{binding.left.clone(), binding.right.clone()});
            }
            for (const auto& pending : unifier.pendingValues()) {
                const auto* leftInfer = pending.left.opt_Infer();
                const auto* rightInfer = pending.right.opt_Infer();
                if ((leftInfer && isSolverCanonicalInfer(leftInfer->index)) || (rightInfer && isSolverCanonicalInfer(rightInfer->index))) {
                    effects->valueEqualities.push_back(SolverValueEquality{pending.left.clone(), pending.right.clone()});
                }
            }
        }
        return ambiguous ? SolverCertainty::Ambiguous : SolverCertainty::Proven;
    };
    const auto relateEquality = [&](const HIRType* left, const HIRType* right) {
        return relateUsing([&](Unifier& unifier) {
            return unifier.unify(left, right);
        });
    };
    const auto relateValueEquality = [&](const HIRConstGeneric& left, const HIRConstGeneric& right) {
        if (left == right) {
            return SolverCertainty::Proven;
        }
        const auto snapshot = ivars.snapshot();
        Unifier unifier(sp, ivars, this);
        const auto outcome = unifier.unifyValues(left, right);
        ivars.rollbackTo(snapshot);
        if (outcome == Unifier::Outcome::Mismatch) {
            return SolverCertainty::NoSolution;
        }
        if (effects) {
            for (const auto& binding : unifier.valueBindings()) {
                effects->valueEqualities.push_back(SolverValueEquality{binding.left.clone(), binding.right.clone()});
            }
            for (const auto& pending : unifier.pendingValues()) {
                const auto* leftInfer = pending.left.opt_Infer();
                const auto* rightInfer = pending.right.opt_Infer();
                if ((leftInfer && isSolverCanonicalInfer(leftInfer->index)) || (rightInfer && isSolverCanonicalInfer(rightInfer->index))) {
                    effects->valueEqualities.push_back(SolverValueEquality{pending.left.clone(), pending.right.clone()});
                }
            }
        }
        bool ambiguous = outcome == Unifier::Outcome::Ambiguous && unifier.pendingValues().empty();
        for (const auto& pending : unifier.pendingValues()) {
            const auto* leftInfer = pending.left.opt_Infer();
            const auto* rightInfer = pending.right.opt_Infer();
            if (!((leftInfer && isSolverCanonicalInfer(leftInfer->index)) || (rightInfer && isSolverCanonicalInfer(rightInfer->index)))) {
                ambiguous = true;
            }
        }
        return ambiguous ? SolverCertainty::Ambiguous : SolverCertainty::Proven;
    };

    const auto* destination = constraint.direction == SolverCoercionConstraint::Direction::InputIsDestination ? input : constraint.other;
    const auto* source = constraint.direction == SolverCoercionConstraint::Direction::InputIsDestination ? constraint.other : input;
    destination = resolveKnown(destination);
    source = resolveKnown(source);

    const auto isOpenInference = [](const HIRType* type) {
        const auto* infer = type->opt_Infer();
        return infer && !infer->isLit();
    };
    const auto typeIsBounded = [](const HIRType* type) {
        return type->is_Generic() || (type->is_Path() && (monomorphiseTypeNeeded(type) || type->as_Path().binding.is_Opaque()));
    };
    const auto isOpenStructuralUnsize = [&](const HIRType* rawDestination, const HIRType* rawSource) {
        const auto* destination = resolveKnown(rawDestination);
        const auto* source = resolveKnown(rawSource);
        const auto* destinationPath = destination->opt_Path();
        const auto* sourcePath = source->opt_Path();
        const auto* destinationStruct = destinationPath && destinationPath->binding.is_Struct() ? destinationPath->binding.as_Struct() : nullptr;
        const auto* sourceStruct = sourcePath && sourcePath->binding.is_Struct() ? sourcePath->binding.as_Struct() : nullptr;
        return destinationStruct
            && destinationStruct == sourceStruct
            && destinationStruct->structMarkings.canUnsize
            && ivars.typeContainsIvars(source, false);
    };
    const auto sourceCanCoerce = [&]() {
        if (source->is_Diverge() || source->is_Pointer() || source->is_Borrow() || source->is_NamedFunction() || source->is_Function() || source->is_TraitObject() || typeIsBounded(source)) {
            return true;
        }
        if (source->is_NodeType() && source->as_NodeType().is_Closure()) {
            return true;
        }
        const auto* path = source->opt_Path();
        return path && path->binding.is_Struct() && path->binding.as_Struct()->structMarkings.coerceUnsized != HIRStructMarkings::Coerce::None;
    };
    const bool destinationOpen = isOpenInference(destination);
    const bool sourceOpen = isOpenInference(source);
    const bool openEndpoint = destinationOpen && sourceOpen
        || constraint.op == SolverCoercionOp::Unsizing && (destinationOpen || sourceOpen)
        || constraint.op == SolverCoercionOp::Coercion && destinationOpen && sourceCanCoerce()
        || constraint.op == SolverCoercionOp::Coercion && sourceOpen && !destination->is_Primitive();
    bool openMarkedParameter = false;
    bool openPointerTarget = false;
    if (constraint.op == SolverCoercionOp::Coercion) {
        const auto* destinationPath = destination->opt_Path();
        const auto* sourcePath = source->opt_Path();
        if (destinationPath && sourcePath && destinationPath->binding.is_Struct() && sourcePath->binding.is_Struct() && destinationPath->binding.as_Struct() == sourcePath->binding.as_Struct()) {
            const auto& markings = sourcePath->binding.as_Struct()->structMarkings;
            if (markings.coerceUnsized != HIRStructMarkings::Coerce::None) {
                const auto& destinationParams = destinationPath->path.data.as_Generic().params;
                const auto& sourceParams = sourcePath->path.data.as_Generic().params;
                ASSERT_BUG(sp, markings.coerceParam < destinationParams.types.size() && markings.coerceParam < sourceParams.types.size(), StringView("Malformed CoerceUnsized struct markings"));
                openMarkedParameter = isRigidUnsized(isRigidUnsized, destinationParams.types[markings.coerceParam]) && (resolveKnown(destinationParams.types[markings.coerceParam])->is_Infer() || resolveKnown(sourceParams.types[markings.coerceParam])->is_Infer());
            }
        }
        const HIRType* destinationInner = nullptr;
        const HIRType* sourceInner = nullptr;
        if (const auto* destinationBorrow = destination->opt_Borrow()) {
            if (const auto* sourceBorrow = source->opt_Borrow(); sourceBorrow && destinationBorrow->type <= sourceBorrow->type) {
                destinationInner = destinationBorrow->inner;
                sourceInner = sourceBorrow->inner;
            }
        } else if (const auto* destinationPointer = destination->opt_Pointer()) {
            if (const auto* sourcePointer = source->opt_Pointer(); sourcePointer && destinationPointer->type <= sourcePointer->type) {
                destinationInner = destinationPointer->inner;
                sourceInner = sourcePointer->inner;
            } else if (const auto* sourceBorrow = source->opt_Borrow(); sourceBorrow && destinationPointer->type <= sourceBorrow->type) {
                destinationInner = destinationPointer->inner;
                sourceInner = sourceBorrow->inner;
            }
        }
        openPointerTarget = destinationInner
            && !ivars.typesEqual(destinationInner, sourceInner)
            && (isOpenInference(resolveKnown(destinationInner)) || isOpenInference(resolveKnown(sourceInner))
                || isOpenStructuralUnsize(destinationInner, sourceInner));
    }
    if (!openEndpoint && !openMarkedParameter && !openPointerTarget && relateEquality(destination, source) == SolverCertainty::Proven) {
        return SolverCertainty::Proven;
    }

    const auto unsize = [&](const HIRType* rawDestination, const HIRType* rawSource, bool hasAutoderefAlternative = false, unsigned alternativeGroup = 0) {
        const auto* destination = resolveKnown(rawDestination);
        const auto* source = resolveKnown(rawSource);
        if (ivars.typesEqual(destination, source)) {
            return SolverCertainty::Proven;
        }
        const bool destinationLiteral = destination->is_Infer() && destination->as_Infer().isLit();
        const bool sourceLiteral = source->is_Infer() && source->as_Infer().isLit();
        if ((destinationLiteral && (sourceLiteral || source->is_Primitive())) || (sourceLiteral && destination->is_Primitive())) {
            return relateEquality(destination, source);
        }
        if (destination->is_Infer() || (source->is_Infer() && !sourceLiteral)) {
            if (!hasAutoderefAlternative && !destination->is_Infer() && typeIsSized(sp, destination) == HIRCompare::Equal) {
                return relateEquality(destination, source);
            }
            if (deferred) {
                deferred->push_back(SolverDeferredCoercion{destination, source, SolverCoercionOp::Unsizing, alternativeGroup});
            }
            return SolverCertainty::Ambiguous;
        }
        if (typeIsSized(sp, destination) == HIRCompare::Equal) {
            return relateEquality(destination, source);
        }
        const bool structuralUnsizeWithOpenSource = isOpenStructuralUnsize(destination, source);
        const auto equality = structuralUnsizeWithOpenSource ? SolverCertainty::Ambiguous : relateEquality(destination, source);
        if (equality == SolverCertainty::Proven) {
            return SolverCertainty::Proven;
        }
        if ((destination->is_Path() && destination->as_Path().binding.is_Unbound()) || (source->is_Path() && source->as_Path().binding.is_Unbound())) {
            return SolverCertainty::Ambiguous;
        }

        const auto unsizeTrait = langUnsize();
        if (unsizeTrait.components().empty()) {
            return equality;
        }
        SolverCertainty result = SolverCertainty::NoSolution;
        solveTraitGoal(sp, unsizeTrait, HIRPathParams(destination), source, [&](SolverResponse response) {
            if (!response.impl && response.certainty != SolverCertainty::Ambiguous) {
                return false;
            }
            result = response.certainty;
            if (equalities) {
                for (size_t i = 0; i < response.slots.typeInputs.size(); i++) {
                    if (response.slots.typeInputs[i] != response.slots.types[i]) {
                        equalities->push_back(SolverTypeEquality{response.slots.typeInputs[i], response.slots.types[i]});
                    }
                }
                for (auto& equality : response.equalities) {
                    equalities->push_back(SolverTypeEquality{equality.left, equality.right});
                }
            }
            if (effects) {
                appendEffects(*effects, std::move(response));
            }
            return true;
        }, {.allowInferInputs = true, .ambiguity = SolverAmbiguityPolicy::Report});
        if (result == SolverCertainty::NoSolution && equality == SolverCertainty::Ambiguous) {
            return equality;
        }
        return result;
    };

    if (constraint.op == SolverCoercionOp::Unsizing) {
        const bool hasAutoderefAlternative = constraint.direction == SolverCoercionConstraint::Direction::InputIsDestination || constraint.allowSourceAutoderef;
        const auto deferredStart = deferred ? deferred->size() : 0;
        const auto alternativeGroup = hasAutoderefAlternative && deferred ? static_cast<unsigned>(deferredStart + 1) : 0;
        auto result = unsize(destination, source, hasAutoderefAlternative, alternativeGroup);
        if (result == SolverCertainty::Proven) {
            return result;
        }
        if (hasAutoderefAlternative) {
            const auto* dereferenced = source;
            while ((dereferenced = autoderef(sp, dereferenced))) {
                const auto dereferencedResult = unsize(destination, dereferenced, true, alternativeGroup);
                if (dereferencedResult == SolverCertainty::Proven) {
                    if (deferred) {
                        deferred->resize(deferredStart);
                    }
                    return dereferencedResult;
                }
                if (dereferencedResult == SolverCertainty::Ambiguous) {
                    result = dereferencedResult;
                }
            }
        }
        return result;
    }

    if (ivars.typesEqual(destination, source)) {
        return SolverCertainty::Proven;
    }
    if (source->is_Diverge()) {
        if (destination->is_Infer() && deferred) {
            deferred->push_back(SolverDeferredCoercion{destination, source, SolverCoercionOp::Coercion});
            return SolverCertainty::Ambiguous;
        }
        return SolverCertainty::Proven;
    }
    if ((destination->is_Infer() && destination->as_Infer().isLit()) || destination->is_Diverge() || (source->is_Infer() && source->as_Infer().isLit())) {
        return relateEquality(destination, source);
    }
    if (destination->is_Infer() || source->is_Infer() || (destination->is_Path() && destination->as_Path().binding.is_Unbound()) || (source->is_Path() && source->as_Path().binding.is_Unbound())) {
        if (deferred) {
            deferred->push_back(SolverDeferredCoercion{destination, source, constraint.op});
        }
        return SolverCertainty::Ambiguous;
    }
    const auto langCoerceUnsized = crate.getLangItemPathOpt("coerce_unsized");
    if (!langCoerceUnsized.components().empty() && (typeIsBounded(source) || typeIsBounded(destination))) {
        SolverCertainty result = SolverCertainty::NoSolution;
        solveTraitGoal(sp, langCoerceUnsized, HIRPathParams(destination), source, [&](SolverResponse response) {
            const bool selected = response.impl != nullptr;
            if (selected) {
                result = response.certainty;
                if (effects) {
                    appendEffects(*effects, std::move(response));
                }
            }
            return selected;
        }, {.allowInferInputs = true});
        if (result != SolverCertainty::NoSolution) {
            return result;
        }
    }

    const auto relateValues = [&](const ThinVector<HIRConstGeneric>& left, const ThinVector<HIRConstGeneric>& right) {
        if (left.size() != right.size()) {
            return SolverCertainty::NoSolution;
        }
        auto result = SolverCertainty::Proven;
        for (size_t i = 0; i < left.size(); i++) {
            const auto nested = relateValueEquality(left[i], right[i]);
            if (nested == SolverCertainty::NoSolution) {
                return nested;
            }
            if (nested == SolverCertainty::Ambiguous) {
                result = nested;
            }
        }
        return result;
    };

    if (const auto* sourcePath = source->opt_Path()) {
        const auto* destinationPath = destination->opt_Path();
        if (destinationPath && sourcePath->binding.is_Struct() && destinationPath->binding.is_Struct()) {
            const auto* sourceStruct = sourcePath->binding.as_Struct();
            if (sourceStruct != destinationPath->binding.as_Struct()) {
                return relateEquality(destination, source);
            }
            const auto& sourceParams = sourcePath->path.data.as_Generic().params;
            const auto& destinationParams = destinationPath->path.data.as_Generic().params;
            const auto& markings = sourceStruct->structMarkings;
            if (markings.coerceUnsized != HIRStructMarkings::Coerce::None) {
                ASSERT_BUG(sp, markings.coerceParam < sourceParams.types.size() && sourceParams.types.size() == destinationParams.types.size(), StringView("Malformed CoerceUnsized struct markings"));
                auto result = markings.coerceUnsized == HIRStructMarkings::Coerce::Passthrough ? evaluateCoercionConstraint(sp, SolverCoercionConstraint{markings.coerceParam, sourceParams.types[markings.coerceParam], SolverCoercionConstraint::Direction::InputIsDestination, SolverCoercionOp::Coercion}, destinationParams.types[markings.coerceParam], equalities, effects, deferred, reachedAutoderefLimit) : unsize(destinationParams.types[markings.coerceParam], sourceParams.types[markings.coerceParam]);
                for (size_t i = 0; result != SolverCertainty::NoSolution && i < sourceParams.types.size(); i++) {
                    if (i == markings.coerceParam) {
                        continue;
                    }
                    const auto fieldResult = relateEquality(destinationParams.types[i], sourceParams.types[i]);
                    if (fieldResult == SolverCertainty::NoSolution) {
                        result = fieldResult;
                    } else if (fieldResult == SolverCertainty::Ambiguous) {
                        result = fieldResult;
                    }
                }
                const auto valueResult = relateValues(destinationParams.values, sourceParams.values);
                if (valueResult == SolverCertainty::NoSolution || (valueResult == SolverCertainty::Ambiguous && result == SolverCertainty::Proven)) {
                    result = valueResult;
                }
                return result;
            }
            if (ivars.pathparamsContainIvars(destinationParams, false) || ivars.pathparamsContainIvars(sourceParams, false)) {
                return relateEquality(destination, source);
            }
            auto result = SolverCertainty::Proven;
            const auto relateField = [&](const HIRType* field) {
                auto destinationField = expandAssociatedTypes(sp, MonomorphStatePtr(crate.types, destination, &destinationParams, nullptr).monomorphType(sp, field));
                auto sourceField = expandAssociatedTypes(sp, MonomorphStatePtr(crate.types, source, &sourceParams, nullptr).monomorphType(sp, field));
                const auto fieldResult = relateEquality(destinationField, sourceField);
                if (fieldResult == SolverCertainty::NoSolution || fieldResult == SolverCertainty::Ambiguous && result == SolverCertainty::Proven) {
                    result = fieldResult;
                }
            };
            switch (sourceStruct->data.tag()) {
                case HIRStructData::TAG_Unit:
                    break;
                case HIRStructData::TAG_Tuple:
                    for (const auto& field : sourceStruct->data.as_Tuple()) {
                        relateField(field.ent);
                    }
                    break;
                case HIRStructData::TAG_Named:
                    for (const auto& field : sourceStruct->data.as_Named()) {
                        relateField(field.ty);
                    }
                    break;
            }
            if (result == SolverCertainty::NoSolution) {
                return result;
            }
            const auto valueResult = relateValues(destinationParams.values, sourceParams.values);
            if (valueResult == SolverCertainty::NoSolution || valueResult == SolverCertainty::Ambiguous && result == SolverCertainty::Proven) {
                result = valueResult;
            }
            return result;
        }
    }

    const auto relateFunctionSignature = [&](const HIRTypeDataFunctionPointer& destinationFunction, const HIRTypeDataFunctionPointer& sourceFunction) {
        if (destinationFunction.abi != sourceFunction.abi || destinationFunction.isVariadic != sourceFunction.isVariadic || (sourceFunction.isUnsafe && !destinationFunction.isUnsafe) || destinationFunction.argTypes.length() != sourceFunction.argTypes.length()) {
            return SolverCertainty::NoSolution;
        }
        return relateUsing([&](Unifier& unifier) {
            auto result = Unifier::Outcome::Proven;
            for (size_t i = 0; i < destinationFunction.argTypes.length(); i++) {
                const auto argument = unifier.unify(destinationFunction.argTypes[i], sourceFunction.argTypes[i]);
                if (argument == Unifier::Outcome::Mismatch) {
                    return argument;
                }
                if (argument == Unifier::Outcome::Ambiguous) {
                    result = argument;
                }
            }
            const auto* destinationReturn = ivars.expandIvars(destinationFunction.rettype);
            const auto* sourceReturn = ivars.expandIvars(sourceFunction.rettype);
            const auto output = unifier.unify(destinationReturn, sourceReturn);
            if (output == Unifier::Outcome::Mismatch || (output == Unifier::Outcome::Ambiguous && result == Unifier::Outcome::Proven)) {
                result = output;
            }
            return result;
        });
    };
    if (const auto* destinationFunction = destination->opt_Function()) {
        if (const auto* sourceFunction = source->opt_Function()) {
            return relateFunctionSignature(*destinationFunction, *sourceFunction);
        }
        if (const auto* named = source->opt_NamedFunction()) {
            const auto sourceFunction = named->decay(crate.types, sp);
            return relateFunctionSignature(*destinationFunction, sourceFunction);
        }
        if (source->is_NodeType() && source->as_NodeType().is_Closure()) {
            const auto* closure = source->as_NodeType().as_Closure();
            if (destinationFunction->abi != ABI_RUST || destinationFunction->isVariadic || destinationFunction->argTypes.length() != closure->args.size()) {
                return SolverCertainty::NoSolution;
            }
            return relateUsing([&](Unifier& unifier) {
                auto result = Unifier::Outcome::Proven;
                for (size_t i = 0; i < destinationFunction->argTypes.length(); i++) {
                    const auto argument = unifier.unify(destinationFunction->argTypes[i], closure->args[i].second);
                    if (argument == Unifier::Outcome::Mismatch) {
                        return argument;
                    }
                    if (argument == Unifier::Outcome::Ambiguous) {
                        result = argument;
                    }
                }
                const auto* destinationReturn = ivars.expandIvars(destinationFunction->rettype);
                const auto* sourceReturn = ivars.expandIvars(closure->returnType);
                const auto output = unifier.unify(destinationReturn, sourceReturn);
                if (output == Unifier::Outcome::Mismatch || (output == Unifier::Outcome::Ambiguous && result == Unifier::Outcome::Proven)) {
                    result = output;
                }
                return result;
            });
        }
    }

    if (const auto* sourcePointer = source->opt_Pointer()) {
        const auto* destinationPointer = destination->opt_Pointer();
        if (!destinationPointer || destinationPointer->type > sourcePointer->type) {
            return relateEquality(destination, source);
        }
        return unsize(destinationPointer->inner, sourcePointer->inner);
    }
    if (const auto* sourceBorrow = source->opt_Borrow()) {
        if (const auto* destinationPointer = destination->opt_Pointer()) {
            if (destinationPointer->type > sourceBorrow->type) {
                return SolverCertainty::NoSolution;
            }
            return unsize(destinationPointer->inner, sourceBorrow->inner);
        }
        if (const auto* destinationBorrow = destination->opt_Borrow()) {
            if (destinationBorrow->type > sourceBorrow->type) {
                return SolverCertainty::NoSolution;
            }
            const auto deferredStart = deferred ? deferred->size() : 0;
            const auto alternativeGroup = deferred ? static_cast<unsigned>(deferredStart + 1) : 0;
            auto result = unsize(destinationBorrow->inner, sourceBorrow->inner, true, alternativeGroup);
            if (result == SolverCertainty::Proven) {
                return result;
            }

            const HIRType* current = sourceBorrow->inner;
            for (unsigned depth = 0; depth < board().settings->recursionLimit; depth++) {
                auto step = autoderefStep(sp, current);
                switch (step.result) {
                    case AutoderefResult::NoMatch:
                        return result;
                    case AutoderefResult::Ambiguous:
                        return SolverCertainty::Ambiguous;
                    case AutoderefResult::Match:
                        current = step.target;
                        break;
                }
                const auto dereferenced = unsize(destinationBorrow->inner, current, true, alternativeGroup);
                if (dereferenced == SolverCertainty::Proven) {
                    if (deferred) {
                        deferred->resize(deferredStart);
                    }
                    return dereferenced;
                }
                if (dereferenced == SolverCertainty::Ambiguous) {
                    result = dereferenced;
                }
            }
            if (reachedAutoderefLimit) {
                *reachedAutoderefLimit = true;
            }
            return SolverCertainty::Ambiguous;
        }
        return relateEquality(destination, source);
    }
    return relateEquality(destination, source);
}

Ordering TraitResolution::compareCoercionEndpoints(const Span& sp, const SolverCoercionConstraint& constraint, const HIRType* left, const HIRType* right) const {
    left = ivars.getType(left);
    right = ivars.getType(right);
    if (constraint.direction == SolverCoercionConstraint::Direction::InputIsSource) {
        const auto* destination = ivars.getType(constraint.other);
        const bool leftExact = ivars.typesEqual(left, destination);
        const bool rightExact = ivars.typesEqual(right, destination);
        if (leftExact != rightExact) {
            return leftExact ? OrdGreater : OrdLess;
        }
        if (left->is_Diverge() != right->is_Diverge()) {
            return left->is_Diverge() ? OrdLess : OrdGreater;
        }
        return OrdEqual;
    }
    const auto* source = ivars.getType(constraint.other);
    const bool leftExact = ivars.typesEqual(left, source);
    const bool rightExact = ivars.typesEqual(right, source);
    if (leftExact != rightExact) {
        return leftExact ? OrdGreater : OrdLess;
    }
    const auto compatibleTarget = [&](const HIRType* leftInner, const HIRType* rightInner) {
        return ivars.typesEqual(leftInner, rightInner) || leftInner->compareWithPlaceholders(sp, rightInner, ivars.callbackResolveInfer()) != HIRCompare::Unequal;
    };
    if (const auto* leftBorrow = left->opt_Borrow()) {
        const auto* rightBorrow = right->opt_Borrow();
        if (!rightBorrow || !compatibleTarget(leftBorrow->inner, rightBorrow->inner)) {
            return OrdEqual;
        }
        return ord(static_cast<int>(leftBorrow->type), static_cast<int>(rightBorrow->type));
    }
    if (const auto* leftPointer = left->opt_Pointer()) {
        const auto* rightPointer = right->opt_Pointer();
        if (!rightPointer || !compatibleTarget(leftPointer->inner, rightPointer->inner)) {
            return OrdEqual;
        }
        return ord(static_cast<int>(leftPointer->type), static_cast<int>(rightPointer->type));
    }
    return OrdEqual;
}

const HIRType* TraitResolution::typeIsOwnedBox(const Span& sp, const HIRType* ty) const {
    if (const auto* e = ty->opt_Path()) {
        if (const auto* pe = e->path.data.opt_Generic()) {
            if (pe->path == langBox()) {
                return this->ivars.getType(pe->params.types.at(0));
            }
        }
    }
    return nullptr;
}

TraitResolution::Autoderef TraitResolution::autoderefStep(const Span& sp, const HIRType* tyIn) const {
    const auto& ty = this->ivars.getType(tyIn);
    if (ty->is_Infer()) {
        return {AutoderefResult::NoMatch, nullptr, {}};
    }
    if (visitTyWith(ty, [&](const HIRType* inner) {
        const auto* erased = inner->opt_ErasedType();
        const auto* alias = erased ? erased->inner.opt_Alias() : nullptr;
        return alias && this->isOpaqueAliasDefiningScope(*alias->inner);
    })) {
        return {AutoderefResult::Ambiguous, nullptr, {}};
    }
    if (const auto* e = ty->opt_Borrow()) {
        DEBUG(StringView("Deref ") << ty << StringView(" into ") << e->inner);
        return {AutoderefResult::Match, this->ivars.getType(e->inner), {}};
    } else if (const auto* e = ty->opt_Array()) {
        DEBUG(StringView("Deref ") << ty << StringView(" into [") << e->inner << StringView("]"));
        return {AutoderefResult::Match, crate.types.slice(e->inner), {}};
    } else if (ty->is_Slice() || ty->is_Primitive() || ty->is_Tuple() || ty->is_Array()) {
        return {AutoderefResult::NoMatch, nullptr, {}};
    } else {
        std::optional<const HIRType*> candidateTarget;
        std::optional<const HIRType*> candidateImplType;
        SolverCertainty certainty = SolverCertainty::NoSolution;
        bool ambiguous = false;

        this->solveTraitGoal(sp, langDeref_, HIRPathParams{}, ty, [&](SolverResponse response) {
            const auto inspect = [&](const SolverImpl& solverImpl, SolverCertainty candidateCertainty) {
                auto foundTarget = solverImpl.getType(crate.types, "Target", {});
                if (foundTarget == nullptr) {
                    foundTarget = crate.types.path(HIRPath(ty, langDeref_, RcString::newInterned("Target")), HIRTypePathBinding::make_Opaque({}));
                } else {
                    foundTarget = this->expandAssociatedTypesInplace(sp, foundTarget);
                }
                candidateTarget = foundTarget;
                candidateImplType = solverImpl.getImplType(crate.types);
                certainty = candidateCertainty;
            };

            if (response.impl) {
                inspect(*response.impl, response.certainty);
                return true;
            }

            ambiguous |= response.certainty == SolverCertainty::Ambiguous;
            return false;
        }, {.ambiguity = SolverAmbiguityPolicy::Report});

        if (ambiguous) {
            DEBUG(StringView("Ambiguous Deref impl for ") << ty);
            return {AutoderefResult::Ambiguous, nullptr, {}};
        }

        if (!candidateTarget) {
            return {AutoderefResult::NoMatch, nullptr, {}};
        }
        if (certainty == SolverCertainty::NoSolution) {
            return {AutoderefResult::NoMatch, nullptr, {}};
        }

        DEBUG(StringView("Deref ") << ty << StringView(" into ") << *candidateTarget);
        return {AutoderefResult::Match, *candidateTarget, candidateImplType};
    }
}

const HIRType* TraitResolution::autoderef(const Span& sp, const HIRType* ty) const {
    auto result = autoderefStep(sp, ty);
    return result.result == AutoderefResult::Match ? result.target : nullptr;
}

unsigned int TraitResolution::autoderefFindMethod(
    const Span& sp,
    const tTraitList& traits,
    const Vector<unsigned>& ivars,
    unsigned int typeIvarCount,
    const HIRType* topTy,
    const RcString& methodName,
    const ThinVector<const HIRType*>& argumentTypes,
    const HIRType* expectedResult,
    bool mustDecide,
    /* Out -> */ ThinVector<MethodCandidate>& possibilities
) const {
    {
        TRACE_FUNCTION_F(StringView("{") << topTy << StringView("}.") << methodName);
        unsigned int derefCount = 0;
        const auto& topTyR = this->ivars.getType(topTy);
        const auto* currentTy = topTyR;

        auto curAccess = MethodAccess::Move;
        auto collapseToMostSpecificSubtrait = [&]() {
            if (!crate.featureEnabled("supertrait_item_shadowing") || possibilities.size() < 2) {
                return;
            }

            std::vector<HIRSimplePath> candidateTraits;
            candidateTraits.reserve(possibilities.size());
            for (const auto& possibility : possibilities) {
                const auto* path = possibility.path.data.opt_UfcsKnown();
                if (!path) {
                    return;
                }
                candidateTraits.push_back(path->trait.path);
            }

            const auto selected = crate.findMostSpecificTrait(sp, candidateTraits);
            if (selected) {
                auto selectedPossibility = mv$(possibilities[*selected]);
                possibilities.clear();
                possibilities.push_back(mv$(selectedPossibility));
            }
        };
        auto canonicalizeTraitCandidates = [&]() {
            auto sharedParams = [&](const HIRPathParams& shape) {
                ASSERT_BUG(sp, typeIvarCount <= ivars.length(), StringView("Invalid method ivar split"));
                ASSERT_BUG(sp, shape.types.size() <= typeIvarCount, StringView("Not enough type ivars for method candidate"));
                ASSERT_BUG(sp, shape.values.size() <= ivars.length() - typeIvarCount, StringView("Not enough value ivars for method candidate"));

                HIRPathParams params;
                params.types.reserve(shape.types.size());
                for (size_t i = 0; i < shape.types.size(); i++) {
                    params.types.push_back(crate.types.infer(ivars[i], HIRInferClass::None));
                }
                params.values.reserve(shape.values.size());
                for (size_t i = 0; i < shape.values.size(); i++) {
                    params.values.push_back(HIRConstGeneric::make_Infer({ivars[typeIvarCount + i]}));
                }
                return params;
            };

            for (auto first = possibilities.begin(); first != possibilities.end(); ++first) {
                auto* firstPath = first->path.data.opt_UfcsKnown();
                if (!firstPath) {
                    continue;
                }
                for (auto second = first + 1; second != possibilities.end();) {
                    auto* secondPath = second->path.data.opt_UfcsKnown();
                    if (!secondPath) {
                        ++second;
                        continue;
                    }
                    if (first->path == second->path) {
                        for (auto move = second; move + 1 != possibilities.end(); ++move) {
                            *move = std::move(*(move + 1));
                        }
                        possibilities.pop_back();
                        continue;
                    }

                    const bool sameSelf = firstPath->type == secondPath->type || firstPath->type->equalsIgnoringRegions(secondPath->type);
                    if (!sameSelf || firstPath->trait.path != secondPath->trait.path) {
                        ++second;
                        continue;
                    }

                    firstPath->trait.params = sharedParams(firstPath->trait.params);
                    for (auto move = second; move + 1 != possibilities.end(); ++move) {
                        *move = std::move(*(move + 1));
                    }
                    possibilities.pop_back();
                }
            }
        };
        auto preferCurrentTraitCandidate = [&]() {
            if (possibilities.size() > 1 && currentTraitPath_) {
                for (size_t i = 0; i < possibilities.size(); i++) {
                    const auto* path = possibilities[i].path.data.opt_UfcsKnown();
                    if (path && path->trait.path == currentTraitPath_->path) {
                        auto selected = mv$(possibilities[i]);
                        possibilities.clear();
                        possibilities.push_back(mv$(selected));
                        break;
                    }
                }
            }
        };
        do {
            const auto* ty = this->ivars.getType(currentTy);
            auto shouldPause = [](const auto& ty) -> bool {
                if (typeIsUnboundedInfer(ty)) {
                    DEBUG(StringView("- Ivar") << ty << StringView(", pausing"));
                    return true;
                }
                if (ty->is_Path() && ty->as_Path().binding.is_Unbound()) {
                    DEBUG(StringView("- Unbound type path ") << ty << StringView(", pausing"));
                    return true;
                }
                return false;
            };
            if (shouldPause(ty)) {
                return ~0u;
            }
            if (ty->is_Borrow() && shouldPause(this->ivars.getType(ty->as_Borrow().inner))) {
                return ~0u;
            }
            // TODO: Pause on Box<_>?

            DEBUG(derefCount << StringView(": ") << ty);
            bool undecided = false;

            if (this->findMethod(sp, traits, ivars, typeIvarCount, ty, methodName, argumentTypes, expectedResult, curAccess, AutoderefBorrow::None, possibilities, &undecided)) {
            }

            if (possibilities.empty()) {
                if (const auto* ptr = ty->opt_Pointer()) {
                    if (ptr->type != HIRBorrowType::Shared) {
                        auto constTy = crate.types.pointer(HIRBorrowType::Shared, ptr->inner);
                        if (this->findMethod(sp, traits, ivars, typeIvarCount, constTy, methodName, argumentTypes, expectedResult, curAccess, AutoderefBorrow::RawShared, possibilities, &undecided)) {
                        }
                    }
                }
            }

            if (possibilities.empty() && crate.featureEnabled("pin_ergonomics")) {
                const auto* pathTy = ty->opt_Path();
                const auto& langPin = crate.getLangItemPathOpt("pin");
                if (pathTy && pathTy->path.data.is_Generic() && !langPin.components().empty()) {
                    const auto& pinPath = pathTy->path.data.as_Generic();
                    if (pinPath.path == langPin && pinPath.params.types.size() == 1) {
                        const auto* pinInner = this->ivars.getType(pinPath.params.types.front());
                        if (const auto* borrow = pinInner->opt_Borrow(); borrow && borrow->type == HIRBorrowType::Unique) {
                            auto shared = crate.types.borrow(HIRBorrowType::Shared, borrow->inner);
                            auto sharedPin = crate.types.path(HIRGenericPath(langPin, HIRPathParams(shared)), pathTy->binding.clone());
                            if (this->findMethod(sp, traits, ivars, typeIvarCount, sharedPin, methodName, argumentTypes, expectedResult, MethodAccess::Move, AutoderefBorrow::PinShared, possibilities, &undecided)) {
                            }
                        }
                    }
                }
            }

            auto borrowTy = crate.types.borrow(HIRBorrowType::Shared, ty);
            if (possibilities.empty() && this->findMethod(sp, traits, ivars, typeIvarCount, borrowTy, methodName, argumentTypes, expectedResult, MethodAccess::Move, AutoderefBorrow::Shared, possibilities, &undecided)) {
            }
            borrowTy = crate.types.borrow(HIRBorrowType::Unique, ty);
            if (possibilities.empty() && curAccess >= MethodAccess::Unique && this->findMethod(sp, traits, ivars, typeIvarCount, borrowTy, methodName, argumentTypes, expectedResult, MethodAccess::Move, AutoderefBorrow::Unique, possibilities, &undecided)) {
            }
            borrowTy = crate.types.borrow(HIRBorrowType::Owned, ty);
            if (possibilities.empty() && curAccess >= MethodAccess::Move && this->findMethod(sp, traits, ivars, typeIvarCount, borrowTy, methodName, argumentTypes, expectedResult, MethodAccess::Move, AutoderefBorrow::Owned, possibilities, &undecided)) {
            }
            if (!possibilities.empty()) {
                canonicalizeTraitCandidates();
                collapseToMostSpecificSubtrait();
                preferCurrentTraitCandidate();
                if (undecided && !mustDecide) {
                    DEBUG(StringView("- ") << possibilities.size() << StringView(" options and the receiver is not known, pausing"));
                    possibilities.clear();
                    return ~0u;
                }
                DEBUG(StringView("FOUND ") << possibilities.size() << StringView(" options: ") << possibilities);
                return derefCount;
            }
            if (undecided && !mustDecide) {
                return ~0u;
            }

            derefCount += 1;
            if (const auto* typ = this->typeIsOwnedBox(sp, ty)) {
                currentTy = typ;
            } else {
                // TODO: Update `cur_access` based on the avaliable Deref impls
                auto step = this->autoderefStep(sp, ty);
                switch (step.result) {
                    case AutoderefResult::NoMatch:
                        currentTy = nullptr;
                        break;
                    case AutoderefResult::Match:
                        currentTy = step.target;
                        break;
                    case AutoderefResult::Ambiguous:
                        return ~0u;
                }
            }
        } while (currentTy);

        if (this->typeContainsIvars(topTy)) {
            DEBUG(StringView("No method on a partially inferred receiver, pausing"));
            return ~0u;
        }

        BUG_ASSERT(possibilities.empty());
        return 0;
    }
}

std::optional<const HIRType*> TraitResolution::checkMethodReceiver(const Span& sp, const HIRFunction& fcn, const HIRType* ty, TraitResolution::MethodAccess access) const {
    switch (fcn.receiver) {
        case HIRFunction::Receiver::Free:
            return std::nullopt;
        case HIRFunction::Receiver::Value:
            if (access >= TraitResolution::MethodAccess::Move) {
                return this->ivars.getType(ty);
            }
            break;
        case HIRFunction::Receiver::BorrowOwned:
            if (!ty->is_Borrow())
                ;
            else if (ty->as_Borrow().type != HIRBorrowType::Owned)
                ;
            else if (access < TraitResolution::MethodAccess::Move)
                ;
            else {
                return this->ivars.getType(ty->as_Borrow().inner);
            }
            break;
        case HIRFunction::Receiver::BorrowUnique:
            if (!ty->is_Borrow())
                ;
            else if (ty->as_Borrow().type != HIRBorrowType::Unique)
                ;
            else if (access < TraitResolution::MethodAccess::Unique)
                ;
            else {
                return this->ivars.getType(ty->as_Borrow().inner);
            }
            break;
        case HIRFunction::Receiver::BorrowShared:
            if (!ty->is_Borrow())
                ;
            else if (ty->as_Borrow().type != HIRBorrowType::Shared)
                ;
            else if (access < TraitResolution::MethodAccess::Shared)
                ;
            else {
                return this->ivars.getType(ty->as_Borrow().inner);
            }
            break;
        case HIRFunction::Receiver::Custom: {
            const auto& receiverType = fcn.args.front().second;
            ASSERT_BUG(
                sp,
                visitTyWith(
                    receiverType,
                    [](const HIRType* v) {
                return v->is_Generic() && v->as_Generic().isSelf();
            }
                ),
                receiverType
            );
            // TODO: Handle custom-receiver functions
            {
                struct GetSelf: public HIRMatchGenerics {
                    std::optional<const HIRType*> detectedSelfTy;

                    GetSelf()
                        : HIRMatchGenerics(BorrowMatchedValues{})
                    {
                    }

                    HIRCompare matchTy(const HIRGenericRef& g, const HIRType* ty, tCbResolveType _resolve_cb) override {
                        if (g.isSelf()) {
                            detectedSelfTy = ty;
                        }
                        return HIRCompare::Equal;
                    }

                    HIRCompare matchVal(const HIRGenericRef& g, const HIRConstGeneric& sz) override {
                        TODO(Span(), StringView("GetSelf::match_val ") << g << StringView(" with ") << sz);
                    }
                } getself;

                if (receiverType->matchTestGenerics(sp, ty, this->ivars.callbackResolveInfer(), getself)) {
                    ASSERT_BUG(sp, getself.detectedSelfTy, StringView("Unable to determine receiver type when matching ") << receiverType << StringView(" and ") << ty);
                    return this->ivars.getType(*getself.detectedSelfTy);
                }
            }
            return std::nullopt;
        }
        case HIRFunction::Receiver::Box:
            if (const auto* ity = this->typeIsOwnedBox(sp, ty)) {
                if (access < TraitResolution::MethodAccess::Move) {
                } else {
                    return this->ivars.getType(ity);
                }
            }
            break;
    }
    return std::nullopt;
}

bool TraitResolution::findMethod(const Span& sp, const tTraitList& traits, const Vector<unsigned>& ivars, unsigned int typeIvarCount, const HIRType* ty, const RcString& methodName, const ThinVector<const HIRType*>& argumentTypes, const HIRType* expectedResult, MethodAccess access, AutoderefBorrow borrowType, /* Out -> */ ThinVector<MethodCandidate>& possibilities, /* Out -> */ bool* outUndecided) const {
    bool rv = false;

    TRACE_FUNCTION_FR(StringView("ty=") << ty << StringView(", name=") << methodName << StringView(", access=") << access, rv << StringView(" ") << possibilities);
    auto getIvaredParams = [&](const HIRGenericParams& tpl) -> HIRPathParams {
        unsigned int nParams = tpl.types.size();
        ASSERT_BUG(sp, typeIvarCount <= ivars.length(), StringView("Invalid method ivar split: ") << typeIvarCount << StringView(" type ivars in a pool of ") << ivars.length());
        ASSERT_BUG(sp, nParams <= typeIvarCount, StringView("Not enough type ivars allocated for method: ") << nParams << StringView(" needed but ") << typeIvarCount << StringView(" allocated by caller\ntpl = ") << tpl.fmtArgs());
        HIRPathParams traitParams;
        traitParams.types.reserve(nParams);
        for (unsigned int i = 0; i < nParams; i++) {
            traitParams.types.push_back(crate.types.infer(ivars[i], HIRInferClass::None));
            ASSERT_BUG(sp, this->ivars.getType(traitParams.types.back())->as_Infer().index == ivars[i], StringView("A method selection ivar was bound"));
        }
        const unsigned int nValues = tpl.values.size();
        ASSERT_BUG(sp, nValues <= ivars.length() - typeIvarCount, StringView("Not enough value ivars allocated for method: ") << nValues << StringView(" needed but ") << ivars.length() - typeIvarCount << StringView(" allocated by caller\ntpl = ") << tpl.fmtArgs());
        traitParams.values.reserve(nValues);
        for (unsigned int i = 0; i < nValues; i++) {
            traitParams.values.push_back(HIRConstGeneric::make_Infer({ivars[typeIvarCount + i]}));
        }
        return traitParams;
    };

    // TODO: Have a cache of name+receiver_type to a list of types and impls
    const auto* inherentReceiver = ty;
    while (const auto* borrow = inherentReceiver->opt_Borrow()) {
        inherentReceiver = this->ivars.getType(borrow->inner);
    }
    const auto* erased = inherentReceiver->opt_ErasedType();
    const auto* alias = erased ? erased->inner.opt_Alias() : nullptr;
    const bool inherentReceiverUnknown = inherentReceiver->is_Infer();
    const bool opaqueCanReveal = !erased || (alias && this->isOpaqueAliasDefiningScope(*alias->inner)) || erased->inner.is_Known();
    if (inherentReceiverUnknown) {
        if (outUndecided) {
            *outUndecided = true;
        }
    } else if (opaqueCanReveal) {
        this->wb.inherentMethods->find(sp, methodName, ty, this->ivars.callbackResolveInfer(), [&](const HIRType* selfTy, const HIRTypeImpl& impl) {
            const auto& method = impl.methods.at(methodName);
            if (!method.publicity.isVisible(this->visPath)) {
                return;
            }
            const auto snapshot = this->ivars.snapshot();
            STD_DEFER {
                this->ivars.rollbackTo(snapshot);
            };
            HIRPathParams implParams;
            auto inherentResult = SolverCertainty::Proven;
            switch (this->relateInherentImplHeader(sp, impl, selfTy, implParams)) {
                case Unifier::Outcome::Proven:
                    break;
                case Unifier::Outcome::Ambiguous:
                    inherentResult = SolverCertainty::Ambiguous;
                    break;
                case Unifier::Outcome::Mismatch:
                    inherentResult = SolverCertainty::NoSolution;
                    break;
            }
            if (inherentResult != SolverCertainty::NoSolution && expectedResult && !method.data.returnType->is_ErasedType()) {
                auto methodParams = this->makeFreshImplParams(method.data.params);
                auto monomorph = MonomorphStatePtr(crate.types, selfTy, &implParams, &methodParams);
                const auto* methodReturn = monomorph.monomorphType(sp, method.data.returnType, true);
                Unifier relation(sp, this->ivars, this, {.relateProjectionInputs = true});
                switch (relation.unify(methodReturn, expectedResult)) {
                    case Unifier::Outcome::Proven:
                        break;
                    case Unifier::Outcome::Ambiguous:
                        inherentResult = SolverCertainty::Ambiguous;
                        break;
                    case Unifier::Outcome::Mismatch:
                        inherentResult = SolverCertainty::NoSolution;
                        break;
                }
            }
            if (inherentResult != SolverCertainty::NoSolution) {
                const auto bounds = this->evaluateInherentImplBounds(sp, impl, implParams);
                if (bounds == SolverCertainty::NoSolution) {
                    inherentResult = bounds;
                } else if (bounds == SolverCertainty::Ambiguous) {
                    inherentResult = bounds;
                }
            }
            if (inherentResult != SolverCertainty::NoSolution) {
                if (outUndecided && (inherentResult == SolverCertainty::Ambiguous || typeIsUnboundedInfer(this->ivars.getType(selfTy)))) {
                    *outUndecided = true;
                }
                {
                    const auto& methodParams = this->solverExistentials(sp, method.data.params);
                    u32 methodScope = 0;
                    if (!methodParams.types.empty()) {
                        const auto* generic = methodParams.types.front()->opt_Generic();
                        ASSERT_BUG(sp, generic && generic->isSolverExistential(), StringView("Method type parameter is not a solver existential"));
                        methodScope = generic->solverScope;
                    } else if (!methodParams.values.empty()) {
                        const auto* generic = methodParams.values.front().opt_Generic();
                        ASSERT_BUG(sp, generic && generic->isSolverExistential(), StringView("Method value parameter is not a solver existential"));
                        methodScope = generic->solverScope;
                    }

                    auto monomorph = MonomorphStatePtr(crate.types, selfTy, &implParams, &methodParams);
                    monomorph.setConstevalState(this->board(), HIRItemPath(""));
                    const auto methodBounds = this->evaluateGenericBounds(sp, method.data.params, methodParams, monomorph, methodScope);
                    if (methodBounds == SolverCertainty::NoSolution) {
                        return;
                    }
                    if (outUndecided && methodBounds == SolverCertainty::Ambiguous) {
                        *outUndecided = true;
                    }
                }
                possibilities.push_back({borrowType, HIRPath(selfTy, methodName, {}), &impl});
                rv = true;
            }
        });
    }

    if (rv) {
        return true;
    }

    // TODO: Handle custom recievers by finding the bottom of a deref chain (or take the top-level reciever as an argument here?)

    DEBUG(StringView("> Bounds"));
    bool foundBound = false;
    bool foundNonGlobalBound = false;
    auto typeIsNonGlobalAfterNormalization = [&](const HIRType* type) {
        auto normalized = this->expandAssociatedTypes(sp, type);
        return monomorphiseTypeNeeded(normalized) || this->typeContainsIvars(normalized);
    };
    auto paramsAreNonGlobalAfterNormalization = [&](const HIRPathParams& params) {
        for (const auto& type : params.types) {
            if (typeIsNonGlobalAfterNormalization(type)) {
                return true;
            }
        }
        return false;
    };
    auto recordBoundGlobalness = [&](const HIRType* type, const HIRGenericPath& trait, const CachedBound& info) {
        foundNonGlobalBound |= typeIsNonGlobalAfterNormalization(type) || paramsAreNonGlobalAfterNormalization(trait.params);
        for (const auto& associated : info.assoc) {
            foundNonGlobalBound |= paramsAreNonGlobalAfterNormalization(associated.second.sourceTrait.params) || paramsAreNonGlobalAfterNormalization(associated.second.atyParams) || typeIsNonGlobalAfterNormalization(associated.second.type);
        }
    };
    for (const auto& tb : traitBounds) {
        const auto& eType = tb.first.first;
        const auto& eTraitGp = tb.first.second;
        const auto& eTraitInfo = tb.second;

        BUG_ASSERT(eTraitInfo.traitPtr);
        HIRGenericPath finalTraitPath;
        const HIRFunction* fcnPtr;
        if (!(fcnPtr = this->traitContainsMethod(sp, eTraitGp, *eTraitInfo.traitPtr, eType, methodName, finalTraitPath))) {
            DEBUG(StringView("- Method '") << methodName << StringView("' missing"));
            continue;
        }

        DEBUG(StringView("- Found trait ") << finalTraitPath << StringView(" (bound)"));
        if (auto selfTy = checkMethodReceiver(sp, *fcnPtr, ty, access)) {
            struct MonomorphEraseHrls: public Monomorphiser {
                using Monomorphiser::Monomorphiser;

                const HIRType* getType(const Span& sp, const HIRGenericRef& ty) const override {
                    if (ty.group() == 3) {
                        return types.infer();
                    }
                    return types.generic(ty.name, ty.binding);
                }

                HIRConstGeneric getValue(const Span& sp, const HIRGenericRef& val) const override {
                    if (val.group() == 3) {
                        return HIRConstGeneric();
                    }
                    return HIRConstGeneric(val);
                }
            };

            finalTraitPath = MonomorphEraseHrls(crate.types).monomorphGenericpath(sp, finalTraitPath, true);

            if (((**selfTy).is_Infer() && ((**selfTy).as_Infer().isLit() == false))) {
                return false;
            }
            const auto snapshot = this->ivars.snapshot();
            Unifier relation(
                sp,
                this->ivars,
                this,
                {
                    .relateProjectionInputs = true,
                    .rigidGenericsAreDistinct = true,
                }
            );
            const auto outcome = relation.unify(*selfTy, eType);
            const bool boundInference = this->ivars.mutationGeneration != snapshot.generation;
            this->ivars.rollbackTo(snapshot);
            if (outcome == Unifier::Outcome::Mismatch) {
                continue;
            }
            if (outUndecided && (outcome == Unifier::Outcome::Ambiguous || boundInference)) {
                *outUndecided = true;
            }

            // TODO: Re-monomorphise final trait using `ty`?
            possibilities.push_back({borrowType, HIRPath(HIRPath::Data::make_UfcsKnown({*selfTy, mv$(finalTraitPath), methodName, {}})), nullptr});
            rv = true;
            foundBound = true;
            if (outcome == Unifier::Outcome::Proven && !boundInference) {
                recordBoundGlobalness(eType, eTraitGp, eTraitInfo);
            }
        } else {
        }
    }
    if (foundBound && foundNonGlobalBound) {
        return rv;
    }

    if (currentTraitPath_) {
        HIRGenericPath finalTraitPath;
        const HIRFunction* fcnPtr;
        if ((fcnPtr = this->traitContainsMethod(sp, *currentTraitPath_, *currentTraitPtr, ty, methodName, finalTraitPath))) {
            DEBUG(StringView("- Found trait ") << finalTraitPath << StringView(" (current)"));
            if (auto selfTy = checkMethodReceiver(sp, *fcnPtr, ty, access)) {
                if (((**selfTy).is_Infer() && ((**selfTy).as_Infer().isLit() == false))) {
                    return false;
                }

                const auto& trait = crate.getTraitByPath(sp, finalTraitPath.path);
                auto traitParams = getIvaredParams(trait.params);

                {
                    const bool crateImplFound = solveTraitGoal(sp, finalTraitPath.path, traitParams, *selfTy, [](SolverResponse response) {
                        return response.impl && response.certainty == SolverCertainty::Proven;
                    }, {.ambiguity = SolverAmbiguityPolicy::Report});
                    if (crateImplFound) {
                        possibilities.push_back({borrowType, HIRPath(*selfTy, HIRGenericPath(finalTraitPath.path, mv$(traitParams)), methodName, {}), nullptr});
                        return true;
                    } else {
                    }
                }
            }
        }
    }

    auto getInnerType = [this, sp](const HIRType* ty, auto cb) -> const HIRType* {
        if (cb(ty)) {
            return ty;
        } else if (ty->is_Borrow()) {
            const auto* ity = this->ivars.getType(ty->as_Borrow().inner);
            if (cb(ity)) {
                return ity;
            } else {
                return nullptr;
            }
        } else {
            auto tp = this->typeIsOwnedBox(sp, ty);
            if (tp && cb(tp)) {
                return tp;
            } else {
                return nullptr;
            }
        }
    };

    DEBUG(StringView("> Special cases"));
    if (const auto* ityp = getInnerType(ty, [](const auto& t) {
        return t->is_TraitObject();
    })) {
        const auto& e = ityp->as_TraitObject();
        const auto& trait = this->crate.getTraitByPath(sp, e.trait.path.path);

        bool foundTraitObject = false;
        auto addTraitObjectMethod = [&](const HIRFunction& fcn, HIRGenericPath finalTraitPath) {
            DEBUG(StringView("- Found trait ") << finalTraitPath << StringView(" (trait object)"));
            if (auto selfTyP = checkMethodReceiver(sp, fcn, ty, access)) {
                possibilities.push_back({borrowType, HIRPath(*selfTyP, mv$(finalTraitPath), methodName, {}), nullptr});
                rv = true;
                foundTraitObject = true;
            }
        };

        const HIRFunction* fcnPtr = nullptr;
        if (traitContainsMethodInner(trait, methodName, fcnPtr)) {
            BUG_ASSERT(fcnPtr);
            addTraitObjectMethod(*fcnPtr, e.trait.path.clone());
        } else {
            const auto selfTy = crate.types.self();
            auto monomorphCb = MonomorphStatePtr(crate.types, selfTy, &e.trait.path.params, nullptr);
            for (const auto& st : trait.allParentTraits) {
                fcnPtr = nullptr;
                if (!traitContainsMethodInner(*st.traitPtr, methodName, fcnPtr)) {
                    continue;
                }
                BUG_ASSERT(fcnPtr);
                auto finalTraitPath = HIRGenericPath(st.path.path, monomorphCb.monomorphPathParams(sp, st.path.params, false));
                addTraitObjectMethod(*fcnPtr, std::move(finalTraitPath));
            }
        }

        if (foundTraitObject) {
            return rv;
        }
    }

    if (const auto* ityp = getInnerType(ty, [](const auto& t) {
        return t->is_ErasedType();
    })) {
        const auto& e = ityp->as_ErasedType();
        for (const auto& traitPath : e.traits) {
            const auto& trait = this->crate.getTraitByPath(sp, traitPath.path.path);

            HIRGenericPath finalTraitPath;
            if (const auto* fcnPtr = this->traitContainsMethod(sp, traitPath.path, trait, crate.types.self(), methodName, finalTraitPath)) {
                DEBUG(StringView("- Found trait ") << finalTraitPath << StringView(" (erased type)"));
                if (auto selfTyP = checkMethodReceiver(sp, *fcnPtr, ty, access)) {
                    possibilities.push_back({borrowType, HIRPath(*selfTyP, mv$(finalTraitPath), methodName, {}), nullptr});
                    rv = true;
                }
            }
        }
    } else if (getInnerType(ty, [](const auto& t) {
        return t->is_Generic();
    })) {
    } else if (const auto* ityp = getInnerType(ty, [](const auto& t) {
        return t->is_Path() && t->as_Path().path.data.is_UfcsKnown();
    })) {
        const auto& e = ityp->as_Path().path.data.as_UfcsKnown();

        DEBUG(StringView("UfcsKnown - Search associated type bounds in trait - ") << e.trait);
        auto monomorphCb = MonomorphStatePtr(crate.types, e.type, &e.trait.params, &e.params);

        const auto& trait = this->crate.getTraitByPath(sp, e.trait.path);
        const auto& assocTy = trait.types.at(e.item);
        for (const auto& bound : assocTy.traitBounds) {
            ASSERT_BUG(sp, bound.traitPtr, StringView("Pointer to trait ") << bound.path << StringView(" not set in ") << e.trait.path);
            HIRGenericPath finalTraitPath;

            auto tySelf = crate.types.path(HIRPath(crate.types.self(), bound.path.clone(), e.item), HIRTypePathBinding::make_Opaque({}));
            if (const auto* fcnPtr = this->traitContainsMethod(sp, bound.path, *bound.traitPtr, tySelf, methodName, finalTraitPath)) {
                DEBUG(StringView("- Found trait ") << finalTraitPath << StringView(" (UFCS Known, aty bounds)"));
                if (auto selfTyP = checkMethodReceiver(sp, *fcnPtr, ty, access)) {
                    if (*selfTyP == ityp) {
                        auto ppHrb = HIRPathParams();
                        monomorphCb.ppHrb = &ppHrb;
                        finalTraitPath = monomorphCb.monomorphGenericpath(sp, finalTraitPath, false);

                        possibilities.push_back({borrowType, HIRPath(*selfTyP, mv$(finalTraitPath), methodName, {}), nullptr});
                        rv = true;
                    }
                }
            }
        }

        for (const auto& bound : trait.params.bounds) {
            if (!bound.is_TraitBound()) {
                continue;
            }
            const auto& be = bound.as_TraitBound();

            if (!be.type->is_Path()) {
                continue;
            }
            if (!be.type->as_Path().binding.is_Opaque()) {
                continue;
            }

            const auto& beTypePe = be.type->as_Path().path.data.as_UfcsKnown();
            if (beTypePe.type != crate.types.self()) {
                continue;
            }
            if (beTypePe.trait.path != e.trait.path) {
                continue;
            }
            if (beTypePe.item != e.item) {
                continue;
            }

            HIRGenericPath finalTraitPath;
            if (const auto* fcnPtr = this->traitContainsMethod(sp, be.trait.path, *be.trait.traitPtr, crate.types.self(), methodName, finalTraitPath)) {
                DEBUG(StringView("- Found trait ") << finalTraitPath << StringView(" (UFCS Known, trait bounds)"));
                if (auto selfTyP = checkMethodReceiver(sp, *fcnPtr, ty, access)) {
                    if (*selfTyP == ityp) {
                        if (monomorphisePathparamsNeeded(finalTraitPath.params)) {
                            finalTraitPath.params = monomorphCb.monomorphPathParams(sp, finalTraitPath.params, false);
                            DEBUG(StringView("- Monomorph to ") << finalTraitPath);
                        }

                        possibilities.push_back({borrowType, HIRPath(*selfTyP, mv$(finalTraitPath), methodName, {}), nullptr});
                        rv = true;
                    }
                }
            }
        }
    } else {
    }

    DEBUG(StringView("> Trait methods"));
    for (const auto& traitRef : ::reverse(traits)) {
        if (traitRef.first == nullptr) {
            break;
        }

        if (crate.edition < ASTEdition::Rust2021 && traitRef.second->skipArrayDuringMethodDispatch && ty->is_Array()) {
            continue;
        }
        if (crate.edition < ASTEdition::Rust2024 && traitRef.second->skipBoxedSliceDuringMethodDispatch) {
            const auto* boxedInner = this->typeIsOwnedBox(sp, ty);
            if (boxedInner && boxedInner->is_Slice()) {
                continue;
            }
        }

        HIRGenericPath finalTraitPath;
        const HIRFunction* fcnPtr;
        if (!(fcnPtr = this->traitContainsMethod(sp, *traitRef.first, *traitRef.second, crate.types.self(), methodName, finalTraitPath))) {
            continue;
        }

        DEBUG(StringView("- Found trait ") << finalTraitPath << StringView(" (in scope)"));
        if (auto selfTyP = checkMethodReceiver(sp, *fcnPtr, ty, access)) {
            const auto& selfTy = *selfTyP;
            HIRPathParams traitParams = getIvaredParams(traitRef.second->params);

            // TODO: Re-monomorphise the trait path!

            bool undecided = false;
            bool implFound = false;

            const auto inputSnapshot = this->ivars.snapshot();
            STD_DEFER {
                this->ivars.rollbackTo(inputSnapshot);
            };
            auto methodParams = this->makeFreshImplParams(fcnPtr->params);
            auto methodMonomorph = MonomorphStatePtr(crate.types, selfTy, &traitParams, &methodParams);
            bool inputsApplicable = true;
            if (fcnPtr->fixedArgCount() == argumentTypes.size() + 1) {
                for (size_t i = 0; i < argumentTypes.size(); i++) {
                    const auto expectedArgument = methodMonomorph.monomorphType(sp, fcnPtr->args[i + 1].second, true);
                    const auto equalitySnapshot = this->ivars.snapshot();
                    Unifier equality(sp, this->ivars, this, {.relateProjectionInputs = true});
                    const auto equalityOutcome = equality.unify(expectedArgument, argumentTypes[i]);
                    if (equalityOutcome != Unifier::Outcome::Mismatch) {
                        this->ivars.commit(equalitySnapshot);
                        continue;
                    }
                    this->ivars.rollbackTo(equalitySnapshot);

                    ThinVector<SolverTypeEquality> equalities;
                    const auto coercion = this->evaluateCoercionConstraint(
                        sp,
                        SolverCoercionConstraint{
                            0,
                            argumentTypes[i],
                            SolverCoercionConstraint::Direction::InputIsDestination,
                            SolverCoercionOp::Coercion,
                        },
                        expectedArgument,
                        &equalities
                    );
                    if (coercion == SolverCertainty::NoSolution) {
                        inputsApplicable = false;
                        break;
                    }
                    const auto* destination = this->ivars.getType(expectedArgument);
                    const auto* source = this->ivars.getType(argumentTypes[i]);
                    if (equalities.empty() && (destination->is_Infer() || source->is_Infer())) {
                        equalities.push_back(SolverTypeEquality{destination, source});
                    }
                    if (equalities.empty() && coercion == SolverCertainty::Ambiguous) {
                        const HIRType* destinationInner = nullptr;
                        const HIRType* sourceInner = nullptr;
                        if (const auto* destinationPointer = destination->opt_Pointer()) {
                            if (const auto* sourceBorrow = source->opt_Borrow(); sourceBorrow && destinationPointer->type <= sourceBorrow->type) {
                                destinationInner = destinationPointer->inner;
                                sourceInner = sourceBorrow->inner;
                            } else if (const auto* sourcePointer = source->opt_Pointer(); sourcePointer && destinationPointer->type <= sourcePointer->type) {
                                destinationInner = destinationPointer->inner;
                                sourceInner = sourcePointer->inner;
                            }
                        } else if (const auto* destinationBorrow = destination->opt_Borrow()) {
                            if (const auto* sourceBorrow = source->opt_Borrow(); sourceBorrow && destinationBorrow->type <= sourceBorrow->type) {
                                destinationInner = destinationBorrow->inner;
                                sourceInner = sourceBorrow->inner;
                            }
                        }
                        if (destinationInner && sourceInner) {
                            equalities.push_back(SolverTypeEquality{destinationInner, sourceInner});
                        }
                    }
                    for (const auto& equality : equalities) {
                        Unifier relation(
                            sp,
                            this->ivars,
                            this,
                            {
                                .relateProjectionInputs = true,
                            }
                        );
                        if (relation.unify(equality.left, equality.right) == Unifier::Outcome::Mismatch) {
                            inputsApplicable = false;
                            break;
                        }
                    }
                    if (!inputsApplicable) {
                        break;
                    }
                }
            }
            if (!inputsApplicable) {
                continue;
            }
            constexpr bool onlyBoundsConstrainingTraitParams = true;
            bool methodBoundsDeferred = false;
            const auto methodBoundsSnapshot = this->ivars.snapshot();
            const auto methodBounds = this->evaluateGenericBounds(sp, fcnPtr->params, methodParams, methodMonomorph, 0, onlyBoundsConstrainingTraitParams);
            if (methodBounds == SolverCertainty::NoSolution) {
                this->ivars.rollbackTo(methodBoundsSnapshot);
                if (!this->paramsContainIvars(traitParams)) {
                    continue;
                }
                methodBoundsDeferred = true;
            } else {
                this->ivars.commit(methodBoundsSnapshot);
            }
            if (methodBounds == SolverCertainty::Ambiguous) {
                undecided = true;
            }

            const bool receiverIsOpen = visitTyWith(this->ivars.getType(selfTy), [&](const HIRType* inner) {
                const auto* r = this->ivars.getType(inner);
                const auto* e = r->opt_Infer();
                return e && e->tyClass == HIRInferClass::None;
            });

            const HIRType* methodReturn;
            const HIRPath::Data::Data_UfcsKnown* returnProjection = nullptr;
            if (expectedResult) {
                methodReturn = methodMonomorph.monomorphType(sp, fcnPtr->returnType, true);
                const auto* returnPath = methodReturn->opt_Path();
                returnProjection = returnPath ? returnPath->path.data.opt_UfcsKnown() : nullptr;
                if (returnProjection) {
                    HIRGenericPath sourceTrait;
                    auto rootTrait = HIRGenericPath(*traitRef.first, traitParams.clone());
                    if (returnProjection->type != selfTy || !traitContainsType(sp, rootTrait, *traitRef.second, returnProjection->item.c_str(), sourceTrait) || sourceTrait.path != returnProjection->trait.path) {
                        returnProjection = nullptr;
                    }
                }
                if (!returnProjection && !methodReturn->is_ErasedType()) {
                    const auto snapshot = this->ivars.snapshot();
                    Unifier relation(sp, this->ivars, this);
                    const auto outcome = relation.unify(methodReturn, expectedResult);
                    if (outcome == Unifier::Outcome::Mismatch) {
                        this->ivars.rollbackTo(snapshot);
                        continue;
                    }
                    auto constrainedParams = traitParams.clone();
                    for (auto& type : constrainedParams.types) {
                        type = this->ivars.getType(type);
                    }
                    for (auto& value : constrainedParams.values) {
                        value = this->ivars.getValue(value).clone();
                    }
                    this->ivars.rollbackTo(snapshot);
                    traitParams = std::move(constrainedParams);
                }
            }
            const TraitGoalQuery methodQuery{
                .assocName = returnProjection ? returnProjection->item.c_str() : (receiverIsOpen ? "" : nullptr),
                .assocType = returnProjection ? expectedResult : nullptr,
                .assocParams = returnProjection ? &returnProjection->params : nullptr,
                .allowInferInputs = receiverIsOpen,
            };
            solveTraitGoal(sp, *traitRef.first, traitParams, selfTy, [&](SolverResponse response) {
                if (!response.impl) {
                    if (response.certainty == SolverCertainty::Ambiguous) {
                        undecided = true;
                    }
                    if (response.certainty == SolverCertainty::Proven) {
                        implFound = true;
                        return true;
                    }
                    return false;
                }

                const auto responseSnapshot = this->ivars.snapshot();
                Unifier responseRelation(
                    sp,
                    this->ivars,
                    this,
                    {
                        .bindRigidValues = true,
                        .relateProjectionInputs = true,
                    }
                );
                const auto relateType = [&](const HIRType* left, const HIRType* right) {
                    auto normalizedLeft = this->expandAssociatedTypes(sp, left);
                    auto normalizedRight = this->expandAssociatedTypes(sp, right);
                    return responseRelation.unify(normalizedLeft, normalizedRight) != Unifier::Outcome::Mismatch;
                };
                const auto relateValue = [&](const HIRConstGeneric& left, const HIRConstGeneric& right) {
                    return responseRelation.unifyValues(left, right) != Unifier::Outcome::Mismatch;
                };
                bool responseApplies = true;
                ASSERT_BUG(sp, response.slots.typeInputs.size() == response.slots.types.size(), StringView("Malformed method solver type slots"));
                for (size_t i = 0; responseApplies && i < response.slots.types.size(); i++) {
                    responseApplies = relateType(response.slots.typeInputs[i], response.slots.types[i]);
                }
                ASSERT_BUG(sp, response.slots.valueInputs.size() == response.slots.values.size(), StringView("Malformed method solver value slots"));
                for (size_t i = 0; responseApplies && i < response.slots.values.size(); i++) {
                    responseApplies = relateValue(response.slots.valueInputs[i], response.slots.values[i]);
                }
                for (const auto& equality : response.equalities) {
                    responseApplies &= relateType(equality.left, equality.right);
                }
                for (const auto& equality : response.valueEqualities) {
                    responseApplies &= relateValue(equality.left, equality.right);
                }
                if (!responseApplies) {
                    this->ivars.rollbackTo(responseSnapshot);
                    return false;
                }

                if (methodBoundsDeferred) {
                    const auto resolvedMethodBounds = this->evaluateGenericBounds(sp, fcnPtr->params, methodParams, methodMonomorph, 0, onlyBoundsConstrainingTraitParams);
                    if (resolvedMethodBounds == SolverCertainty::NoSolution) {
                        this->ivars.rollbackTo(responseSnapshot);
                        return false;
                    }
                    if (resolvedMethodBounds == SolverCertainty::Ambiguous) {
                        undecided = true;
                    }
                }
                this->ivars.commit(responseSnapshot);
                implFound = true;
                if (receiverIsOpen && response.certainty != SolverCertainty::Proven) {
                    DEBUG(StringView("[find_method] impl only matches while the receiver is unknown"));
                    undecided = true;
                }
                return true;
            }, methodQuery);
            if (implFound) {
                auto constrainedSelf = selfTy;
                constrainedSelf = this->ivars.expandIvars(constrainedSelf);
                this->ivars.expandIvarsParams(traitParams);
                possibilities.push_back({borrowType, HIRPath(constrainedSelf, HIRGenericPath(*traitRef.first, mv$(traitParams)), methodName, {}), nullptr});
                rv = true;
            }
            if (undecided && outUndecided) {
                *outUndecided = true;
            }
        } else {
            DEBUG(StringView("> Incorrect receiver"));
        }
    }

    return rv;
}

const HIRType* TraitResolution::findField(const Span& sp, const HIRType* ty, const RcString& name) const {
    if (const auto* e = ty->opt_Path()) {
        switch (e->binding.tag()) {
            case HIRTypePathBinding::TAG_Unbound: {
                TODO(sp, StringView("Handle TypePathBinding::Unbound - ") << ty);
                break;
            }
            case HIRTypePathBinding::TAG_Opaque: {
                break;
            }
            case HIRTypePathBinding::TAG_Struct: {
                auto& be = e->binding.as_Struct();
                const auto& str = *be;
                const auto& params = e->path.data.as_Generic().params;
                auto monomorph = MonomorphStatePtr(crate.types, ty, &params, nullptr);
                switch (str.data.tag()) {
                    case HIRStructData::TAG_Unit: {
                        break;
                    }
                    case HIRStructData::TAG_Tuple: {
                        auto& se = str.data.as_Tuple();
                        for (unsigned int i = 0; i < se.size(); i++) {
                            DEBUG(i << StringView(": ") << se[i].publicity << StringView(", ") << this->visPath << StringView(" : ") << se[i].ent);
                            if (se[i].publicity.isVisible(this->visPath) && FMT(i) == name) {
                                return monomorph.monomorphType(sp, se[i].ent);
                            }
                        }
                        break;
                    }
                    case HIRStructData::TAG_Named: {
                        auto& se = str.data.as_Named();
                        for (const auto& fld : se) {
                            DEBUG(fld.name << StringView(": ") << fld.vis << StringView(", ") << this->visPath << StringView(" : ") << fld.ty);
                            if (fld.vis.isVisible(this->visPath) && fld.name == name) {
                                return monomorph.monomorphType(sp, fld.ty);
                            }
                        }
                        break;
                    }
                }
                break;
            }
            case HIRTypePathBinding::TAG_Enum: {
                break;
            }
            case HIRTypePathBinding::TAG_ExternType: {
                break;
            }
            case HIRTypePathBinding::TAG_Union: {
                auto& be = e->binding.as_Union();
                const auto& unm = *be;
                const auto& params = e->path.data.as_Generic().params;
                auto monomorph = MonomorphStatePtr(crate.types, ty, &params, nullptr);

                for (const auto& fld : unm.variants) {
                    if (fld.vis.isVisible(this->visPath) && fld.name == name) {
                        return monomorph.monomorphType(sp, fld.ty);
                    }
                }
                break;
            }
        }
    } else if (const auto* e = ty->opt_Tuple()) {
        for (unsigned int i = 0; i < e->length(); i++) {
            if (FMT(i) == name) {
                return (*e)[i];
            }
        }
    } else {
    }
    return nullptr;
}

HMTypeInferrence::FmtType::FmtType(const HMTypeInferrence& ctxt, const HIRType* ty)
    : ctxt(ctxt)
    , ty(ty)
{
}

HMTypeInferrence::FmtPP::FmtPP(const HMTypeInferrence& ctxt, const HIRPathParams& pps)
    : ctxt(ctxt)
    , pps(pps)
{
}

HMTypeInferrence::IVar::IVar(const HIRType* type)
    : alias(~0u)
    , type(type)
{
}

HMTypeInferrence::IVarValue::IVarValue()
    : alias(~0u)
    , val(new HIRConstGeneric())
{
}

HMTypeInferrence::HMTypeInferrence(HIRTypeInterner& types)
    : types(types)
    , hasChanged(false)
    , aliasIvarPool(ObjPool::fromMemory())
    , aliasTypeIvars(aliasIvarPool.mutPtr())
    , aliasValueIvars(aliasIvarPool.mutPtr())
{
}

bool HMTypeInferrence::takeChanged() {
    bool rv = hasChanged;
    hasChanged = false;
    return rv;
}

void HMTypeInferrence::markChange() {
    mutationGeneration = ++generationCounter;
    if (!hasChanged) {
        DEBUG(StringView("- CHANGE"));
        hasChanged = true;
    }
}

void HMTypeInferrence::journalMutation(JournalEntry::Kind kind, unsigned slot, const HIRType* oldType) {
    if (snapshotDepth != 0) {
        journal.pushBack(JournalEntry{kind, slot, oldType});
    }
}

HMTypeInferrence::Snapshot HMTypeInferrence::snapshot() {
    snapshotDepth++;
    return Snapshot{journal.length(), ivars.size(), values.size(), mutationGeneration, hasChanged};
}

void HMTypeInferrence::commit(const Snapshot& snapshot) {
    ASSERT_BUG(Span(), snapshotDepth != 0, StringView("commit without an active inference snapshot"));
    ASSERT_BUG(Span(), journal.length() >= snapshot.journalLength, StringView("inference snapshots committed out of order"));
    snapshotDepth--;
    if (snapshotDepth == 0) {
        for (size_t i = 0; i < journal.length(); i++) {
            const auto& entry = journal[i];
            if (entry.kind == JournalEntry::Kind::ValAlias) {
                values.at(entry.slot).val.reset();
            }
        }
        journal.clear();
    }
}

void HMTypeInferrence::rollbackTo(const Snapshot& snapshot) {
    ASSERT_BUG(Span(), snapshotDepth != 0, StringView("rollback without an active inference snapshot"));
    ASSERT_BUG(Span(), journal.length() >= snapshot.journalLength, StringView("inference snapshots rolled back out of order"));
    snapshotDepth--;
    while (journal.length() > snapshot.journalLength) {
        const auto& entry = journal[journal.length() - 1];
        switch (entry.kind) {
            case JournalEntry::Kind::TypeSet: {
                ivars.at(entry.slot).type = entry.oldType;
                break;
            }
            case JournalEntry::Kind::TypeAlias: {
                auto& ivar = ivars.at(entry.slot);
                ivar.alias = ~0u;
                ivar.type = entry.oldType;
                break;
            }
            case JournalEntry::Kind::ValSet: {
                *values.at(entry.slot).val = HIRConstGeneric::make_Infer({entry.slot});
                break;
            }
            case JournalEntry::Kind::ValAlias: {
                values.at(entry.slot).alias = ~0u;
                break;
            }
            case JournalEntry::Kind::AliasTypeMap: {
                aliasTypeIvars.erase(entry.slot);
                break;
            }
            case JournalEntry::Kind::AliasValueMap: {
                aliasValueIvars.erase(entry.slot);
                break;
            }
        }
        journal.popBack();
    }
    ASSERT_BUG(Span(), ivars.size() >= snapshot.ivarCount, StringView("inference snapshot saw the ivar table shrink"));
    ASSERT_BUG(Span(), values.size() >= snapshot.valueCount, StringView("inference snapshot saw the value table shrink"));
    ivars.erase(ivars.begin() + snapshot.ivarCount, ivars.end());
    values.erase(values.begin() + snapshot.valueCount, values.end());
    mutationGeneration = snapshot.generation;
    hasChanged = snapshot.hasChanged;
}

HMTypeInferrence::ResolvePlaceholders::ResolvePlaceholders(const HMTypeInferrence& parent)
    : parent(parent)
{
}

const HIRType* TraitResolution::expandAssociatedTypes(const Span& sp, const HIRType* input, SolverResponseCallback* effects) const {
    return expandAssociatedTypesInplace(sp, input, effects);
}

void TraitResolution::expandAssociatedTypesParams(const Span& sp, HIRPathParams& params, SolverResponseCallback* effects) const {
    for (auto& type : params.types) {
        if (this->hasAssociatedType(type)) {
            type = this->expandAssociatedTypes(sp, type, effects);
        }
    }
}

bool typeIsUnboundedInfer(const HIRType* ty) {
    if (const auto* te = ty->opt_Infer()) {
        switch (te->tyClass) {
            case HIRInferClass::Integer:
                return false;
            case HIRInferClass::Float:
                return false;
            case HIRInferClass::None:
                return true;
        }
    }
    return false;
}

const HIRType* HMTypeInferrence::ResolvePlaceholders::getType(const Span& sp, const HIRType* ty) const {
    if (const auto* infer = ty->opt_Infer(); infer && infer->index != ~0u) {
        return parent.getType(ty);
    } else {
        return ty;
    }
}

const HIRConstGeneric& HMTypeInferrence::ResolvePlaceholders::getVal(const Span& sp, const HIRConstGeneric& v) const {
    if (const auto* infer = v.opt_Infer(); infer && infer->index != ~0u) {
        return parent.getValue(v);
    } else {
        return v;
    }
}

auto CanonicalizeTraitGoal::canonicalPlaceholderName(const RcString& name) const -> RcString {
    for (const auto& entry : placeholderNames_) {
        if (entry.first == name) {
            return entry.second;
        }
        if (entry.second == name) {
            return name;
        }
    }
    auto canonical = RcString::newInterned(FMT(StringView("#solver-placeholder-") << placeholderNames_.size()));
    placeholderNames_.push_back({name, canonical});
    return canonical;
}

CanonicalizeTraitGoal::CanonicalizeTraitGoal(HIRTypeInterner& types, const HMTypeInferrence* ivarTable, const IntMap<u32>* alphaSolverScopes, u32 alphaScopeBase)
    : Monomorphiser(types)
    , ivarTable_(ivarTable)
    , alphaSolverScopes_(alphaSolverScopes)
    , alphaScopeBase_(alphaScopeBase)
{
}

auto CanonicalizeTraitGoal::canonicalIvar(const HIRType* infer) const -> const HIRType* {
    for (size_t i = 0; i < ivarNodes_.length(); i++) {
        if (ivarNodes_[i] == infer) {
            return types.infer(HIR_INFER_SOLVER_CANONICAL_MIN + static_cast<unsigned>(i), infer->as_Infer().tyClass);
        }
    }
    if (frozen_) {
        if (ivarTable_ && infer->as_Infer().index >= ivarTable_->ivars.size()) {
            const auto original = RcString::newInterned(FMT(StringView("#solver-unowned-type-") << infer->as_Infer().index));
            return types.generic(canonicalPlaceholderName(original), GENERICPlaceholder * 256);
        }
        sawForeignIvar_ = true;
        return infer;
    }
    ivarNodes_.pushBack(infer);
    return types.infer(HIR_INFER_SOLVER_CANONICAL_MIN + static_cast<unsigned>(ivarNodes_.length() - 1), infer->as_Infer().tyClass);
}

auto CanonicalizeTraitGoal::freeze() const -> void {
    if (!frozen_) {
        inputPlaceholderCount_ = placeholderNames_.size();
    }
    frozen_ = true;
}

auto CanonicalizeTraitGoal::sawForeignIvar() const -> bool {
    return sawForeignIvar_;
}

auto CanonicalizeTraitGoal::sawForeignSolverExistential() const -> bool {
    return sawForeignSolverExistential_;
}

auto CanonicalizeTraitGoal::originalSolverGeneric(const HIRGenericRef& generic) const -> std::optional<HIRGenericRef> {
    if (!generic.isSolverExistential() || generic.solverScope < alphaScopeBase_) {
        return {};
    }
    const auto slot = static_cast<size_t>(generic.solverScope - alphaScopeBase_);
    if (slot >= solverScopes_.length()) {
        return {};
    }
    return HIRGenericRef::newSolverExistential(solverScopes_[slot], static_cast<u16>(generic.idx()));
}

auto CanonicalizeTraitGoal::alphaSolverEnvironment() const -> const Vector<u32>& {
    return solverScopeClasses_;
}

auto CanonicalizeTraitGoal::originalIvar(unsigned index) const -> const HIRType* {
    if (!isSolverCanonicalInfer(index)) {
        return nullptr;
    }
    const size_t slot = index - HIR_INFER_SOLVER_CANONICAL_MIN;
    return slot < ivarNodes_.length() ? ivarNodes_[slot] : nullptr;
}

auto CanonicalizeTraitGoal::canonicalValueIvar(unsigned original) const -> HIRConstGeneric {
    for (size_t i = 0; i < valueIvarIndexes_.length(); i++) {
        if (valueIvarIndexes_[i] == original) {
            return HIRConstGeneric::make_Infer({HIR_INFER_SOLVER_CANONICAL_MIN + static_cast<unsigned>(i)});
        }
    }
    if (frozen_) {
        if (ivarTable_ && original >= ivarTable_->values.size()) {
            const auto name = RcString::newInterned(FMT(StringView("#solver-unowned-value-") << original));
            return HIRConstGeneric(HIRGenericRef(canonicalPlaceholderName(name), GENERICPlaceholder * 256));
        }
        sawForeignIvar_ = true;
        return HIRConstGeneric::make_Infer({original});
    }
    valueIvarIndexes_.pushBack(original);
    return HIRConstGeneric::make_Infer({HIR_INFER_SOLVER_CANONICAL_MIN + static_cast<unsigned>(valueIvarIndexes_.length() - 1)});
}

auto CanonicalizeTraitGoal::originalValueIvar(unsigned index) const -> const unsigned* {
    if (!isSolverCanonicalInfer(index)) {
        return nullptr;
    }
    const size_t slot = index - HIR_INFER_SOLVER_CANONICAL_MIN;
    return slot < valueIvarIndexes_.length() ? &valueIvarIndexes_[slot] : nullptr;
}

auto CanonicalizeTraitGoal::monomorphType(const Span& sp, const HIRType* ty, bool allowInfer) const -> const HIRType* {
    const auto* inputPath = ty->opt_Path();
    if (!ty->hasTypeInfer() && !ty->needsMonomorphisation() && !(inputPath && inputPath->binding.is_Opaque())) {
        return ty;
    }
    if (ivarTable_ && ty->is_Infer()) {
        const auto& infer = ty->as_Infer();
        if (frozen_ && isSolverCanonicalInfer(infer.index) && originalIvar(infer.index)) {
            return ty;
        }
        const auto* resolved = ivarTable_->getType(ty);
        if (const auto* infer = resolved->opt_Infer()) {
            if (isAliasInputInfer(infer->index) && !isSolverCanonicalInfer(infer->index)) {
                return resolved;
            }
            return canonicalIvar(resolved);
        }
        return monomorphType(sp, resolved, allowInfer);
    }
    if (const auto* path = ty->opt_Path(); path && path->binding.is_Opaque()) {
        auto base = Monomorphiser::monomorphType(sp, ty, allowInfer);
        if (const auto* basePath = base->opt_Path(); basePath && !basePath->binding.is_Opaque()) {
            return types.intern(HIRType::make_Path({basePath->path.clone(), path->binding.clone()}));
        }
        return base;
    }
    return Monomorphiser::monomorphType(sp, ty, allowInfer);
}

auto CanonicalizeTraitGoal::getType(const Span&, const HIRGenericRef& generic) const -> const HIRType* {
    if (generic.isSolverExistential() && alphaSolverScopes_) {
        const auto* alphaScope = alphaSolverScopes_->find(generic.solverScope);
        if (!alphaScope) {
            return types.generic(generic);
        }
        for (size_t i = 0; i < solverScopes_.length(); i++) {
            if (solverScopes_[i] == generic.solverScope) {
                return types.generic(HIRGenericRef::newSolverExistential(alphaScopeBase_ + static_cast<u32>(i), static_cast<u16>(generic.idx())));
            }
        }
        if (frozen_ || solverScopes_.length() >= 256) {
            sawForeignSolverExistential_ = true;
            return types.generic(generic);
        }
        const auto slot = solverScopes_.length();
        solverScopes_.pushBack(generic.solverScope);
        solverScopeClasses_.pushBack(*alphaScope);
        return types.generic(HIRGenericRef::newSolverExistential(alphaScopeBase_ + static_cast<u32>(slot), static_cast<u16>(generic.idx())));
    }
    return generic.isPlaceholder() && !generic.isSolverExistential() ? types.generic(canonicalPlaceholderName(generic.name), generic.binding) : types.generic(generic);
}

auto CanonicalizeTraitGoal::getValue(const Span&, const HIRGenericRef& generic) const -> HIRConstGeneric {
    if (generic.isSolverExistential() && alphaSolverScopes_) {
        const auto* alphaScope = alphaSolverScopes_->find(generic.solverScope);
        if (!alphaScope) {
            return HIRConstGeneric(generic);
        }
        for (size_t i = 0; i < solverScopes_.length(); i++) {
            if (solverScopes_[i] == generic.solverScope) {
                return HIRConstGeneric(HIRGenericRef::newSolverExistential(alphaScopeBase_ + static_cast<u32>(i), static_cast<u16>(generic.idx())));
            }
        }
        if (frozen_ || solverScopes_.length() >= 256) {
            sawForeignSolverExistential_ = true;
            return HIRConstGeneric(generic);
        }
        const auto slot = solverScopes_.length();
        solverScopes_.pushBack(generic.solverScope);
        solverScopeClasses_.pushBack(*alphaScope);
        return HIRConstGeneric(HIRGenericRef::newSolverExistential(alphaScopeBase_ + static_cast<u32>(slot), static_cast<u16>(generic.idx())));
    }
    return HIRConstGeneric(generic.isPlaceholder() && !generic.isSolverExistential() ? HIRGenericRef(canonicalPlaceholderName(generic.name), generic.binding) : generic);
}

auto CanonicalizeTraitGoal::monomorphConstgeneric(const Span& sp, const HIRConstGeneric& val, bool allowInfer) const -> HIRConstGeneric {
    if (ivarTable_) {
        if (const auto* infer = val.opt_Infer(); infer && infer->index != ~0u) {
            if (frozen_ && isSolverCanonicalInfer(infer->index) && originalValueIvar(infer->index)) {
                return val.clone();
            }
            const auto& resolved = ivarTable_->getValue(val);
            if (const auto* resolvedInfer = resolved.opt_Infer()) {
                if (isAliasInputInfer(resolvedInfer->index) && !isSolverCanonicalInfer(resolvedInfer->index)) {
                    return resolved.clone();
                }
                return canonicalValueIvar(resolvedInfer->index);
            }
            return Monomorphiser::monomorphConstgeneric(sp, resolved, allowInfer);
        }
    }
    return Monomorphiser::monomorphConstgeneric(sp, val, allowInfer);
}

auto CanonicalizeTraitGoal::placeholderNames() const -> const std::vector<std::pair<RcString, RcString>>& {
    return placeholderNames_;
}

auto CanonicalizeTraitGoal::originalPlaceholderName(const RcString& canonical) const -> const RcString* {
    const auto count = frozen_ ? inputPlaceholderCount_ : placeholderNames_.size();
    for (size_t i = 0; i < count; i++) {
        const auto& entry = placeholderNames_[i];
        if (entry.second == canonical) {
            return &entry.first;
        }
    }
    return nullptr;
}

auto CanonicalizeTraitGoal::originalResponsePlaceholderName(const RcString& canonical) const -> const RcString* {
    for (const auto& entry : placeholderNames_) {
        if (entry.second == canonical) {
            return &entry.first;
        }
    }
    return nullptr;
}

auto CanonicalizeTraitGoal::ivarNodes() const -> const Vector<const HIRType*>& {
    return ivarNodes_;
}

auto CanonicalizeTraitGoal::typeSlotCount() const -> size_t {
    return ivarNodes_.length();
}

auto CanonicalizeTraitGoal::valueSlotCount() const -> size_t {
    return valueIvarIndexes_.length();
}

auto CanonicalizeTraitGoal::canonicalTypeSlot(size_t slot) const -> const HIRType* {
    ASSERT_BUG(Span(), slot < ivarNodes_.length(), StringView("canonical type slot out of range"));
    return types.infer(HIR_INFER_SOLVER_CANONICAL_MIN + static_cast<unsigned>(slot), ivarNodes_[slot]->as_Infer().tyClass);
}

auto CanonicalizeTraitGoal::canonicalValueSlot(size_t slot) const -> HIRConstGeneric {
    ASSERT_BUG(Span(), slot < valueIvarIndexes_.length(), StringView("canonical value slot out of range"));
    return HIRConstGeneric::make_Infer({HIR_INFER_SOLVER_CANONICAL_MIN + static_cast<unsigned>(slot)});
}

auto InstantiateCanonicalTraitResponse::instantiatePlaceholderName(const RcString& canonical) const -> RcString {
    if (goalCanonicalizer) {
        if (const auto* original = goalCanonicalizer->originalPlaceholderName(canonical)) {
            return *original;
        }
    } else {
        for (const auto& entry : goalNames) {
            if (entry.second == canonical) {
                return entry.first;
            }
        }
    }
    for (const auto& entry : freshNames) {
        if (entry.first == canonical) {
            return entry.second;
        }
    }
    auto fresh = RcString(FMT(StringView("solver_response_") << instance << StringView("_") << freshNames.size()));
    freshNames.push_back({canonical, fresh});
    return fresh;
}

InstantiateCanonicalTraitResponse::InstantiateCanonicalTraitResponse(HIRTypeInterner& types, const std::vector<std::pair<RcString, RcString>>& goalNames, u64 instance, const CanonicalizeTraitGoal* goalCanonicalizer)
    : Monomorphiser(types)
    , goalNames(goalNames)
    , goalCanonicalizer(goalCanonicalizer)
    , instance(instance)
{
}

auto InstantiateCanonicalTraitResponse::monomorphType(const Span& sp, const HIRType* ty, bool allowInfer) const -> const HIRType* {
    if (goalCanonicalizer) {
        if (const auto* infer = ty->opt_Infer()) {
            if (const auto* original = goalCanonicalizer->originalIvar(infer->index)) {
                return original;
            }
        }
    }
    return Monomorphiser::monomorphType(sp, ty, allowInfer);
}

auto InstantiateCanonicalTraitResponse::monomorphConstgeneric(const Span& sp, const HIRConstGeneric& val, bool allowInfer) const -> HIRConstGeneric {
    if (goalCanonicalizer) {
        if (const auto* infer = val.opt_Infer()) {
            if (const auto* original = goalCanonicalizer->originalValueIvar(infer->index)) {
                return HIRConstGeneric::make_Infer({*original});
            }
        }
    }
    return Monomorphiser::monomorphConstgeneric(sp, val, allowInfer);
}

auto InstantiateCanonicalTraitResponse::getValue(const Span&, const HIRGenericRef& generic) const -> HIRConstGeneric {
    if (goalCanonicalizer) {
        if (const auto original = goalCanonicalizer->originalSolverGeneric(generic)) {
            return HIRConstGeneric(*original);
        }
    }
    return HIRConstGeneric(generic.isPlaceholder() && !generic.isSolverExistential() ? HIRGenericRef(instantiatePlaceholderName(generic.name), generic.binding) : generic);
}

auto InstantiateTraitResponseForCaller::isGoalPlaceholder(const HIRGenericRef& generic) const -> bool {
    if (generic.isSolverExistential()) {
        return false;
    }
    if (goalCanonicalizer) {
        for (const auto& entry : goalCanonicalizer->placeholderNames()) {
            if (entry.first == generic.name && goalCanonicalizer->originalPlaceholderName(entry.second)) {
                return true;
            }
        }
        return false;
    }
    for (const auto& entry : goalNames) {
        if (entry.first == generic.name) {
            return true;
        }
    }
    return false;
}

InstantiateTraitResponseForCaller::InstantiateTraitResponseForCaller(HIRTypeInterner& types, HMTypeInferrence& ivars, const std::vector<std::pair<RcString, RcString>>& goalNames, const CanonicalizeTraitGoal* goalCanonicalizer)
    : Monomorphiser(types)
    , ivars(ivars)
    , goalNames(goalNames)
    , goalCanonicalizer(goalCanonicalizer)
{
}

auto InstantiateTraitResponseForCaller::monomorphType(const Span& sp, const HIRType* ty, bool allowInfer) const -> const HIRType* {
    if (goalCanonicalizer) {
        if (const auto* infer = ty->opt_Infer()) {
            if (const auto* original = goalCanonicalizer->originalIvar(infer->index)) {
                return original;
            }
            if (!isAliasInputInfer(infer->index)) {
                const auto* resolved = ivars.getType(ty);
                if (resolved != ty) {
                    return this->monomorphType(sp, resolved, allowInfer);
                }
            }
        }
    }
    return Monomorphiser::monomorphType(sp, ty, allowInfer);
}

auto InstantiateTraitResponseForCaller::monomorphConstgeneric(const Span& sp, const HIRConstGeneric& val, bool allowInfer) const -> HIRConstGeneric {
    if (goalCanonicalizer) {
        if (const auto* infer = val.opt_Infer()) {
            if (const auto* original = goalCanonicalizer->originalValueIvar(infer->index)) {
                return HIRConstGeneric::make_Infer({*original});
            }
        }
    }
    return Monomorphiser::monomorphConstgeneric(sp, val, allowInfer);
}

auto InstantiateTraitResponseForCaller::callerGeneric(const HIRGenericRef& generic) const -> HIRGenericRef {
    if (generic.isPlaceholder() && !generic.isSolverExistential() && goalCanonicalizer) {
        if (const auto* original = goalCanonicalizer->originalResponsePlaceholderName(generic.name)) {
            return HIRGenericRef(*original, generic.binding);
        }
    }
    return generic;
}

auto InstantiateTraitResponseForCaller::getType(const Span&, const HIRGenericRef& raw) const -> const HIRType* {
    if (goalCanonicalizer) {
        if (const auto original = goalCanonicalizer->originalSolverGeneric(raw)) {
            return Monomorphiser::types.generic(*original);
        }
    }
    const auto generic = callerGeneric(raw);
    if (!generic.isPlaceholder() || isGoalPlaceholder(generic)) {
        return Monomorphiser::types.generic(generic);
    }
    for (const auto& entry : typeValues) {
        if (entry.first == generic) {
            return entry.second;
        }
    }
    auto fresh = ivars.newIvarTr();
    typeValues.push_back({generic, fresh});
    return fresh;
}

auto InstantiateTraitResponseForCaller::getValue(const Span&, const HIRGenericRef& raw) const -> HIRConstGeneric {
    if (goalCanonicalizer) {
        if (const auto original = goalCanonicalizer->originalSolverGeneric(raw)) {
            return HIRConstGeneric(*original);
        }
    }
    const auto generic = callerGeneric(raw);
    if (!generic.isPlaceholder() || isGoalPlaceholder(generic)) {
        return HIRConstGeneric(generic);
    }
    for (const auto& entry : values) {
        if (entry.first == generic) {
            return entry.second.clone();
        }
    }
    auto fresh = HIRConstGeneric::make_Infer({ivars.newIvarVal()});
    values.push_back({generic, fresh.clone()});
    return fresh;
}

auto CorrelateSolverResponseSlots::correlateParams(const HIRPathParams& input, const HIRPathParams& response) -> void {
    if (input.types.size() != response.types.size()) {
        return;
    }
    for (size_t i = 0; i < input.types.size(); i++) {
        correlateType(input.types[i], response.types[i]);
    }
}

auto CorrelateSolverResponseSlots::correlateGenericPath(const HIRGenericPath& input, const HIRGenericPath& response) -> void {
    if (input.path == response.path) {
        correlateParams(input.params, response.params);
    }
}

auto CorrelateSolverResponseSlots::correlatePath(const HIRPath& input, const HIRPath& response) -> void {
    if (input.data.tag() != response.data.tag()) {
        return;
    }
    switch (input.data.tag()) {
        case HIRPathData::TAG_Generic:
            correlateGenericPath(input.data.as_Generic(), response.data.as_Generic());
            break;
        case HIRPathData::TAG_UfcsInherent: {
            const auto& left = input.data.as_UfcsInherent();
            const auto& right = response.data.as_UfcsInherent();
            if (left.item == right.item) {
                correlateType(left.type, right.type);
                correlateParams(left.params, right.params);
                correlateParams(left.implParams, right.implParams);
            }
            break;
        }
        case HIRPathData::TAG_UfcsKnown: {
            const auto& left = input.data.as_UfcsKnown();
            const auto& right = response.data.as_UfcsKnown();
            if (left.item == right.item && left.trait.path == right.trait.path) {
                correlateType(left.type, right.type);
                correlateParams(left.trait.params, right.trait.params);
                correlateParams(left.params, right.params);
            }
            break;
        }
        case HIRPathData::TAG_UfcsUnknown: {
            const auto& left = input.data.as_UfcsUnknown();
            const auto& right = response.data.as_UfcsUnknown();
            if (left.item == right.item) {
                correlateType(left.type, right.type);
                correlateParams(left.params, right.params);
            }
            break;
        }
    }
}

CorrelateSolverResponseSlots::CorrelateSolverResponseSlots(HIRTypeInterner& interner, const SolverSlotValues& slots)
    : MonomorphiserNop(interner)
    , slots_(slots)
{
}

auto CorrelateSolverResponseSlots::correlateType(const HIRType* input, const HIRType* response) -> void {
    if (input == response) {
        return;
    }
    if (const auto* infer = response->opt_Infer(); infer && infer->index != ~0u) {
        for (const auto& entry : structuralTypes_) {
            if (entry.first == response) {
                return;
            }
        }
        structuralTypes_.pushBack({response, input});
        return;
    }
    if (const auto* left = input->opt_Path()) {
        if (const auto* right = response->opt_Path()) {
            correlatePath(left->path, right->path);
        }
        return;
    }
    if (const auto* left = input->opt_Tuple()) {
        const auto* right = response->opt_Tuple();
        if (!right || left->length() != right->length()) {
            return;
        }
        for (size_t i = 0; i < left->length(); i++) {
            correlateType((*left)[i], (*right)[i]);
        }
        return;
    }
    if (const auto* left = input->opt_Borrow()) {
        const auto* right = response->opt_Borrow();
        if (right && left->type == right->type) {
            correlateType(left->inner, right->inner);
        }
        return;
    }
    if (const auto* left = input->opt_Pointer()) {
        const auto* right = response->opt_Pointer();
        if (right && left->type == right->type) {
            correlateType(left->inner, right->inner);
        }
        return;
    }
    if (const auto* left = input->opt_Slice()) {
        if (const auto* right = response->opt_Slice()) {
            correlateType(left->inner, right->inner);
        }
        return;
    }
    if (const auto* left = input->opt_Array()) {
        if (const auto* right = response->opt_Array(); right && left->size == right->size) {
            correlateType(left->inner, right->inner);
        }
        return;
    }
    if (const auto* left = input->opt_ErasedType()) {
        const auto* right = response->opt_ErasedType();
        if (!right || left->inner.tag() != right->inner.tag()) {
            return;
        }
        correlateParams(left->use, right->use);
        switch (left->inner.tag()) {
            case TypeDataErasedTypeInner::TAG_Fcn: {
                const auto& leftOrigin = left->inner.as_Fcn();
                const auto& rightOrigin = right->inner.as_Fcn();
                if (leftOrigin.index == rightOrigin.index) {
                    correlatePath(leftOrigin.origin, rightOrigin.origin);
                }
                break;
            }
            case TypeDataErasedTypeInner::TAG_Known:
                correlateType(left->inner.as_Known(), right->inner.as_Known());
                break;
            case TypeDataErasedTypeInner::TAG_Alias: {
                const auto& leftAlias = left->inner.as_Alias();
                const auto& rightAlias = right->inner.as_Alias();
                if (leftAlias.inner == rightAlias.inner) {
                    correlateParams(leftAlias.params, rightAlias.params);
                }
                break;
            }
        }
        return;
    }
    if (const auto* left = input->opt_NamedFunction()) {
        if (const auto* right = response->opt_NamedFunction()) {
            correlatePath(left->path, right->path);
        }
        return;
    }
    if (const auto* left = input->opt_Function()) {
        const auto* right = response->opt_Function();
        if (!right || left->argTypes.length() != right->argTypes.length()) {
            return;
        }
        for (size_t i = 0; i < left->argTypes.length(); i++) {
            correlateType(left->argTypes[i], right->argTypes[i]);
        }
        correlateType(left->rettype, right->rettype);
    }
}

auto CorrelateSolverResponseSlots::monomorphType(const Span& sp, const HIRType* type, bool allowInfer) const -> const HIRType* {
    for (const auto& entry : structuralTypes_) {
        if (entry.first == type) {
            return entry.second;
        }
    }
    for (size_t i = 0; i < slots_.types.size(); i++) {
        if (slots_.types[i]->is_Infer() && slots_.types[i] == type && slots_.typeInputs[i] != type) {
            return slots_.typeInputs[i];
        }
    }
    return MonomorphiserNop::monomorphType(sp, type, allowInfer);
}

auto CorrelateSolverResponseSlots::monomorphConstgeneric(const Span& sp, const HIRConstGeneric& value, bool allowInfer) const -> HIRConstGeneric {
    for (size_t i = 0; i < slots_.values.size(); i++) {
        if (slots_.values[i] == value && slots_.valueInputs[i] != value) {
            return slots_.valueInputs[i].clone();
        }
    }
    return MonomorphiserNop::monomorphConstgeneric(sp, value, allowInfer);
}

DecanonicalizeSolverInfers::DecanonicalizeSolverInfers(HIRTypeInterner& types, const CanonicalizeTraitGoal& canonicalizer)
    : MonomorphiserNop(types)
    , canonicalizer_(canonicalizer)
{
}

auto DecanonicalizeSolverInfers::monomorphType(const Span& sp, const HIRType* ty, bool allowInfer) const -> const HIRType* {
    if (const auto* infer = ty->opt_Infer()) {
        if (const auto* original = canonicalizer_.originalIvar(infer->index)) {
            return original;
        }
        return ty;
    }
    if (const auto* path = ty->opt_Path(); path && path->binding.is_Opaque()) {
        auto base = MonomorphiserNop::monomorphType(sp, ty, allowInfer);
        if (const auto* basePath = base->opt_Path(); basePath && !basePath->binding.is_Opaque()) {
            return types.intern(HIRType::make_Path({basePath->path.clone(), path->binding.clone()}));
        }
        return base;
    }
    return MonomorphiserNop::monomorphType(sp, ty, allowInfer);
}

auto DecanonicalizeSolverInfers::getType(const Span&, const HIRGenericRef& generic) const -> const HIRType* {
    if (const auto original = canonicalizer_.originalSolverGeneric(generic)) {
        return types.generic(*original);
    }
    if (generic.isPlaceholder() && !generic.isSolverExistential()) {
        if (const auto* original = canonicalizer_.originalPlaceholderName(generic.name)) {
            return types.generic(*original, generic.binding);
        }
    }
    return types.generic(generic);
}

auto DecanonicalizeSolverInfers::monomorphConstgeneric(const Span& sp, const HIRConstGeneric& val, bool allowInfer) const -> HIRConstGeneric {
    if (const auto* infer = val.opt_Infer()) {
        if (const auto* original = canonicalizer_.originalValueIvar(infer->index)) {
            return HIRConstGeneric::make_Infer({*original});
        }
    }
    return MonomorphiserNop::monomorphConstgeneric(sp, val, allowInfer);
}

auto DecanonicalizeSolverInfers::getValue(const Span&, const HIRGenericRef& generic) const -> HIRConstGeneric {
    if (const auto original = canonicalizer_.originalSolverGeneric(generic)) {
        return HIRConstGeneric(*original);
    }
    if (generic.isPlaceholder() && !generic.isSolverExistential()) {
        if (const auto* original = canonicalizer_.originalPlaceholderName(generic.name)) {
            return HIRConstGeneric(HIRGenericRef(*original, generic.binding));
        }
    }
    return HIRConstGeneric(generic);
}

auto NextTraitGoalEvaluator::span() const -> const Span& {
    ASSERT_BUG(Span(), span_, StringView("next-solver session used outside an evaluation"));
    return *span_;
}

auto NextTraitGoalEvaluator::implExistentials(const HIRGenericParams& definition) -> const HIRPathParams& {
    const auto key = splitMix64(reinterpret_cast<uintptr_t>(&definition));
    auto* bucket = implExistentials_.find(key);
    if (bucket) {
        for (const auto& entry : *bucket) {
            if (entry.definition == &definition) {
                return entry.params;
            }
        }
    } else {
        bucket = implExistentials_.insert(key);
    }

    const auto scope = ++resolve_.board().id;
    ASSERT_BUG(span(), scope != 0, StringView("solver existential scope exhausted"));
    implExistentialScopes_.insert(scope, 0);

    HIRPathParams params;
    params.types.reserve(definition.types.size());
    for (size_t i = 0; i < definition.types.size(); i++) {
        ASSERT_BUG(span(), i < 256, StringView("Too many candidate type parameters"));
        params.types.push_back(crate.types.generic(HIRGenericRef::newSolverExistential(scope, static_cast<u16>(i))));
    }
    params.values.reserve(definition.values.size());
    for (size_t i = 0; i < definition.values.size(); i++) {
        ASSERT_BUG(span(), i < 256, StringView("Too many candidate value parameters"));
        params.values.push_back(HIRGenericRef::newSolverExistential(scope, static_cast<u16>(i)));
    }
    bucket->push_back(ImplExistentials{&definition, mv$(params)});
    return bucket->back().params;
}

auto NextTraitGoalEvaluator::goalIsConcrete(const HIRSimplePath& trait, const CanonicalGoal& canonical) const -> bool {
    bool sawGeneric = false;
    auto concrete = [&sawGeneric](const HIRType* ty) {
        if (ty->flags & (HIRType::HAS_TYPE_INFER | HIRType::HAS_DEFERRED_CONST | HIRType::HAS_UNEVALUATED_CONST)) {
            return false;
        }
        return !visitTyWith(ty, [&sawGeneric](const HIRType* inner) {
            if (const auto* generic = inner->opt_Generic()) {
                if (generic->group() == GENERICPlaceholder) {
                    return true;
                }
                sawGeneric = true;
                return false;
            }
            if (inner->is_Infer() || inner->is_NodeType() || inner->is_ErasedType()) {
                return true;
            }
            if (const auto* path = inner->opt_Path()) {
                return path->binding.is_Unbound();
            }
            return false;
        });
    };
    if (!concrete(canonical.type)) {
        return false;
    }
    for (const auto& ty : canonical.params.types) {
        if (!concrete(ty)) {
            return false;
        }
    }
    for (const auto& value : canonical.params.values) {
        if (!value.is_Evaluated()) {
            return false;
        }
    }
    if (sawGeneric && (trait == resolve_.langSized() || trait == resolve_.langMetaSized() || trait == resolve_.langPointeeSized())) {
        return false;
    }
    return canonical.associated.empty();
}

auto NextTraitGoalEvaluator::crateCacheUsable() const -> bool {
    return resolve_.traitBounds.size() == 0 && !coherenceMode;
}

auto NextTraitGoalEvaluator::crateCache() const -> NextSolverCrateCache& {
    auto& wb = const_cast<WireBoard&>(resolve_.board());
    if (!wb.solverCache) {
        wb.solverCache = wb.pool->make<NextSolverCrateCache>();
    }
    return *wb.solverCache;
}

auto NextTraitGoalEvaluator::canonicalizeGoal(const HIRPathParams& params, const HIRType* type, const HIRTraitPath::assocListT* associated, CanonicalizeTraitGoal& canonicalizer) const -> CanonicalGoal {
    auto canonicalParams = canonicalizer.monomorphPathParams(span(), params, true);
    const auto canonicalType = canonicalizer.monomorphType(span(), type, true);
    CanonicalGoal result(std::move(canonicalParams), canonicalType);
    if (associated) {
        for (const auto& entry : *associated) {
            result.associated.insert({entry.first, HIRTraitPath::AtyEqual{canonicalizer.monomorphGenericpath(span(), entry.second.sourceTrait, true), canonicalizer.monomorphPathParams(span(), entry.second.atyParams, true), canonicalizer.monomorphType(span(), entry.second.type, true)}});
        }
    }
    return result;
}

auto NextTraitGoalEvaluator::availableDepthForNested() -> std::optional<size_t> {
    if (frameDepth == 0) {
        return ROOT_DEPTH;
    }
    auto& parent = *frames[frameDepth - 1];
    if (parent.availableDepth == 0) {
        parent.encounteredOverflow = true;
        return {};
    }
    return parent.encounteredOverflow ? parent.availableDepth / OVERFLOW_DEPTH_DIVISOR : parent.availableDepth - 1;
}

auto NextTraitGoalEvaluator::isEnvironmentOrBuiltin(const SolverImpl& impl) -> bool {
    return !impl.isTraitImpl();
}

auto NextTraitGoalEvaluator::paramsHaveUnknownTypes(const HIRPathParams& params) const -> bool {
    for (const auto& type : params.types) {
        if (typeHasUnknown(type)) {
            return true;
        }
    }
    return false;
}

auto NextTraitGoalEvaluator::pathHasUnknownTypes(const HIRPath& path) const -> bool {
    if (const auto* pe = path.data.opt_Generic()) {
        return paramsHaveUnknownTypes(pe->params);
    }
    if (const auto* pe = path.data.opt_UfcsInherent()) {
        return typeHasUnknown(pe->type) || paramsHaveUnknownTypes(pe->params) || paramsHaveUnknownTypes(pe->implParams);
    }
    if (const auto* pe = path.data.opt_UfcsKnown()) {
        return typeHasUnknown(pe->type) || paramsHaveUnknownTypes(pe->trait.params) || paramsHaveUnknownTypes(pe->params);
    }
    const auto& pe = path.data.as_UfcsUnknown();
    return typeHasUnknown(pe.type) || paramsHaveUnknownTypes(pe.params);
}

auto NextTraitGoalEvaluator::traitPathHasUnknownTypes(const HIRTraitPath& trait) const -> bool {
    if (paramsHaveUnknownTypes(trait.path.params)) {
        return true;
    }
    for (const auto& assoc : trait.typeBounds) {
        if (paramsHaveUnknownTypes(assoc.second.sourceTrait.params) || paramsHaveUnknownTypes(assoc.second.atyParams) || typeHasUnknown(assoc.second.type)) {
            return true;
        }
    }
    for (const auto& assoc : trait.traitBounds) {
        if (paramsHaveUnknownTypes(assoc.second.sourceTrait.params) || paramsHaveUnknownTypes(assoc.second.atyParams)) {
            return true;
        }
        for (const auto& bound : assoc.second.traits) {
            if (traitPathHasUnknownTypes(bound)) {
                return true;
            }
        }
    }
    return false;
}

auto NextTraitGoalEvaluator::valueHasUnassignedInfer(const HIRConstGeneric& value) const -> bool {
    if (const auto* infer = value.opt_Infer()) {
        return infer->index == ~0u;
    }
    if (const auto* unevaluated = value.opt_Unevaluated()) {
        return ((*unevaluated)->selfType && typeHasUnassignedInfer((*unevaluated)->selfType)) || paramsHaveUnassignedInfer((*unevaluated)->paramsImpl) || paramsHaveUnassignedInfer((*unevaluated)->paramsItem);
    }
    return false;
}

auto NextTraitGoalEvaluator::paramsHaveUnassignedInfer(const HIRPathParams& params) const -> bool {
    for (const auto& type : params.types) {
        if (typeHasUnassignedInfer(type)) {
            return true;
        }
    }
    for (const auto& value : params.values) {
        if (valueHasUnassignedInfer(value)) {
            return true;
        }
    }
    return false;
}

auto NextTraitGoalEvaluator::pathHasUnassignedInfer(const HIRPath& path) const -> bool {
    if (const auto* pe = path.data.opt_Generic()) {
        return paramsHaveUnassignedInfer(pe->params);
    }
    if (const auto* pe = path.data.opt_UfcsInherent()) {
        return typeHasUnassignedInfer(pe->type) || paramsHaveUnassignedInfer(pe->params) || paramsHaveUnassignedInfer(pe->implParams);
    }
    if (const auto* pe = path.data.opt_UfcsKnown()) {
        return typeHasUnassignedInfer(pe->type) || paramsHaveUnassignedInfer(pe->trait.params) || paramsHaveUnassignedInfer(pe->params);
    }
    const auto& pe = path.data.as_UfcsUnknown();
    return typeHasUnassignedInfer(pe.type) || paramsHaveUnassignedInfer(pe.params);
}

auto NextTraitGoalEvaluator::traitPathHasUnassignedInfer(const HIRTraitPath& trait) const -> bool {
    if (paramsHaveUnassignedInfer(trait.path.params)) {
        return true;
    }
    for (const auto& assoc : trait.typeBounds) {
        if (paramsHaveUnassignedInfer(assoc.second.sourceTrait.params) || paramsHaveUnassignedInfer(assoc.second.atyParams) || typeHasUnassignedInfer(assoc.second.type)) {
            return true;
        }
    }
    for (const auto& assoc : trait.traitBounds) {
        if (paramsHaveUnassignedInfer(assoc.second.sourceTrait.params) || paramsHaveUnassignedInfer(assoc.second.atyParams)) {
            return true;
        }
        for (const auto& bound : assoc.second.traits) {
            if (traitPathHasUnassignedInfer(bound)) {
                return true;
            }
        }
    }
    return false;
}

auto NextTraitGoalEvaluator::typeHasUnassignedInfer(const HIRType* input) const -> bool {
    if (const auto* infer = input->opt_Infer()) {
        if (infer->index == ~0u) {
            return true;
        }
        const auto* resolved = resolve_.resolveType(input);
        return resolved != input && typeHasUnassignedInfer(resolved);
    }
    if (const auto* path = input->opt_Path()) {
        return pathHasUnassignedInfer(path->path);
    }
    if (const auto* object = input->opt_TraitObject()) {
        if (traitPathHasUnassignedInfer(object->trait)) {
            return true;
        }
        for (const auto& marker : object->markers) {
            if (paramsHaveUnassignedInfer(marker.params)) {
                return true;
            }
        }
        return false;
    }
    if (const auto* erased = input->opt_ErasedType()) {
        for (const auto& trait : erased->traits) {
            if (traitPathHasUnassignedInfer(trait)) {
                return true;
            }
        }
        if (const auto* known = erased->inner.opt_Known()) {
            return typeHasUnassignedInfer(*known);
        }
        if (const auto* alias = erased->inner.opt_Alias()) {
            return paramsHaveUnassignedInfer(alias->params);
        }
        if (const auto* fcn = erased->inner.opt_Fcn()) {
            return pathHasUnassignedInfer(fcn->origin);
        }
        return false;
    }
    if (const auto* array = input->opt_Array()) {
        const auto* size = array->size.opt_Unevaluated();
        return typeHasUnassignedInfer(array->inner) || (size && valueHasUnassignedInfer(*size));
    }
    if (const auto* slice = input->opt_Slice()) {
        return typeHasUnassignedInfer(slice->inner);
    }
    if (const auto* tuple = input->opt_Tuple()) {
        for (const auto& field : *tuple) {
            if (typeHasUnassignedInfer(field)) {
                return true;
            }
        }
        return false;
    }
    if (const auto* borrow = input->opt_Borrow()) {
        return typeHasUnassignedInfer(borrow->inner);
    }
    if (const auto* pointer = input->opt_Pointer()) {
        return typeHasUnassignedInfer(pointer->inner);
    }
    if (const auto* named = input->opt_NamedFunction()) {
        return pathHasUnassignedInfer(named->path);
    }
    if (const auto* fcn = input->opt_Function()) {
        for (const auto& arg : fcn->argTypes) {
            if (typeHasUnassignedInfer(arg)) {
                return true;
            }
        }
        return typeHasUnassignedInfer(fcn->rettype);
    }
    return false;
}

auto NextTraitGoalEvaluator::goalHasUnassignedInfer(const HIRPathParams& params, const HIRType* type, const HIRTraitPath::assocListT* associated) const -> bool {
    if (paramsHaveUnassignedInfer(params) || typeHasUnassignedInfer(type)) {
        return true;
    }
    if (associated) {
        for (const auto& entry : *associated) {
            if (paramsHaveUnassignedInfer(entry.second.sourceTrait.params) || paramsHaveUnassignedInfer(entry.second.atyParams) || typeHasUnassignedInfer(entry.second.type)) {
                return true;
            }
        }
    }
    return false;
}

auto NextTraitGoalEvaluator::selfIsUnresolvedProjectionOverIvar(const HIRType* type) const -> bool {
    const auto* path = type->opt_Path();
    return path && path->binding.is_Unbound() && path->path.data.is_UfcsKnown() && resolve_.typeContainsIvars(type);
}

auto NextTraitGoalEvaluator::normalizeGoalInput(const HIRType* input) const -> const HIRType* {
    const auto* path = input->opt_Path();
    const auto* projection = path ? path->path.data.opt_UfcsKnown() : nullptr;
    auto output = resolve_.expandAssociatedTypes(span(), input);
    if (projection && projection->type->is_Generic() && output->is_Infer()) {
        return input;
    }
    return output;
}

auto NextTraitGoalEvaluator::typeHasUnknown(const HIRType* input) const -> bool {
    const auto& type = resolve_.resolveType(input);
    if (type->is_Infer() || type->is_Generic()) {
        return true;
    }
    if (const auto* path = type->opt_Path()) {
        return pathHasUnknownTypes(path->path);
    }
    if (const auto* object = type->opt_TraitObject()) {
        if (traitPathHasUnknownTypes(object->trait)) {
            return true;
        }
        for (const auto& marker : object->markers) {
            if (paramsHaveUnknownTypes(marker.params)) {
                return true;
            }
        }
        return false;
    }
    if (const auto* erased = type->opt_ErasedType()) {
        for (const auto& trait : erased->traits) {
            if (traitPathHasUnknownTypes(trait)) {
                return true;
            }
        }
        if (const auto* known = erased->inner.opt_Known()) {
            return typeHasUnknown(*known);
        }
        if (const auto* alias = erased->inner.opt_Alias()) {
            return paramsHaveUnknownTypes(alias->params);
        }
        if (const auto* fcn = erased->inner.opt_Fcn()) {
            return pathHasUnknownTypes(fcn->origin);
        }
        return false;
    }
    if (const auto* array = type->opt_Array()) {
        return typeHasUnknown(array->inner);
    }
    if (const auto* slice = type->opt_Slice()) {
        return typeHasUnknown(slice->inner);
    }
    if (const auto* tuple = type->opt_Tuple()) {
        for (const auto& field : *tuple) {
            if (typeHasUnknown(field)) {
                return true;
            }
        }
        return false;
    }
    if (const auto* borrow = type->opt_Borrow()) {
        return typeHasUnknown(borrow->inner);
    }
    if (const auto* pointer = type->opt_Pointer()) {
        return typeHasUnknown(pointer->inner);
    }
    if (const auto* named = type->opt_NamedFunction()) {
        return pathHasUnknownTypes(named->path);
    }
    if (const auto* fcn = type->opt_Function()) {
        for (const auto& arg : fcn->argTypes) {
            if (typeHasUnknown(arg)) {
                return true;
            }
        }
        return typeHasUnknown(fcn->rettype);
    }
    return false;
}

auto NextTraitGoalEvaluator::typeHasCandidatePlaceholder(const HIRType* type) -> bool {
    bool found = false;
    visitTyWith(type, [&](const HIRType* inner) {
        if (const auto* generic = inner->opt_Generic()) {
            found |= generic->group() == GENERICPlaceholder;
        }
        return found;
    });
    return found;
}

auto NextTraitGoalEvaluator::typeHasUfcsUnknown(const HIRType* type) -> bool {
    if (!type) {
        return false;
    }
    return visitTyWith(type, [](const HIRType* inner) {
        const auto* path = inner->opt_Path();
        return path && path->path.data.is_UfcsUnknown();
    });
}

auto NextTraitGoalEvaluator::paramsNeedResponseConstraints(const HIRPathParams& params) -> bool {
    for (const auto& type : params.types) {
        bool found = false;
        visitTyWith(type, [&](const HIRType* inner) {
            if (const auto* generic = inner->opt_Generic()) {
                found |= generic->group() == GENERICPlaceholder;
            } else if (const auto* infer = inner->opt_Infer()) {
                found |= !infer->isLit();
            }
            return found;
        });
        if (found) {
            return true;
        }
    }
    for (const auto& value : params.values) {
        if (value.is_Infer() || (value.is_Generic() && value.as_Generic().group() == GENERICPlaceholder)) {
            return true;
        }
    }
    return false;
}

auto NextTraitGoalEvaluator::candidateNeedsResponseConstraints(const Candidate& candidate) const -> bool {
    if (candidate.impl.traitImpl) {
        return paramsNeedResponseConstraints(candidate.impl.implParams);
    }
    return candidate.markerImpl && paramsNeedResponseConstraints(candidate.markerImplParams);
}

auto NextTraitGoalEvaluator::orphanVisitResolvedType(const HIRType* type, OrphanPerspective perspective) const -> OrphanVisit {
    if (type->is_Infer() || type->is_Generic()) {
        return perspective == OrphanPerspective::Remote ? OrphanVisit::LocalKey : OrphanVisit::Uncovered;
    }

    if (const auto* path = type->opt_Path()) {
        const auto* generic = path->path.data.opt_Generic();
        const bool concreteAdt = generic && (path->binding.is_Struct() || path->binding.is_Enum() || path->binding.is_Union() || path->binding.is_ExternType());
        if (!concreteAdt) {
            if (typeHasUnknown(type)) {
                return perspective == OrphanPerspective::Remote ? OrphanVisit::LocalKey : OrphanVisit::Uncovered;
            }
            return OrphanVisit::NonLocal;
        }

        const bool local = perspective == OrphanPerspective::Local && generic->path.crateName() == crate.crateName;
        if (local) {
            return OrphanVisit::LocalKey;
        }

        const auto* strPtr = path->binding.opt_Struct();
        if (strPtr && (*strPtr)->structMarkings.isFundamental) {
            for (const auto& param : generic->params.types) {
                const auto result = orphanVisitType(param, perspective);
                if (result != OrphanVisit::NonLocal) {
                    return result;
                }
            }
        }
        return OrphanVisit::NonLocal;
    }

    if (const auto* borrow = type->opt_Borrow()) {
        return orphanVisitType(borrow->inner, perspective);
    }

    if (const auto* object = type->opt_TraitObject()) {
        const auto& principal = object->trait.path.path;
        if (perspective == OrphanPerspective::Local && principal != HIRSimplePath() && principal.crateName() == crate.crateName) {
            return OrphanVisit::LocalKey;
        }
        return OrphanVisit::NonLocal;
    }

    if (type->is_NodeType()) {
        return perspective == OrphanPerspective::Local ? OrphanVisit::LocalKey : OrphanVisit::NonLocal;
    }

    if (type->is_ErasedType() && typeHasUnknown(type)) {
        return perspective == OrphanPerspective::Remote ? OrphanVisit::LocalKey : OrphanVisit::Uncovered;
    }

    return OrphanVisit::NonLocal;
}

auto NextTraitGoalEvaluator::orphanVisitType(const HIRType* input, OrphanPerspective perspective) const -> OrphanVisit {
    const auto& resolved = resolve_.resolveType(input);
    const auto* path = resolved->opt_Path();
    const bool isAlias = path && (!path->path.data.is_Generic() || path->binding.is_Unbound() || path->binding.is_Opaque());
    if (isAlias) {
        auto normalized = resolve_.expandAssociatedTypes(span(), resolved);
        if (!(normalized->is_Infer() && !resolved->is_Infer())) {
            return orphanVisitResolvedType(normalized, perspective);
        }
    }
    return orphanVisitResolvedType(resolved, perspective);
}

auto NextTraitGoalEvaluator::orphanCheckTraitRef(const HIRPathParams& params, const HIRType* type, OrphanPerspective perspective) const -> bool {
    const auto selfResult = orphanVisitType(type, perspective);
    if (selfResult != OrphanVisit::NonLocal) {
        return selfResult == OrphanVisit::LocalKey;
    }
    for (const auto& param : params.types) {
        const auto result = orphanVisitType(param, perspective);
        if (result != OrphanVisit::NonLocal) {
            return result == OrphanVisit::LocalKey;
        }
    }
    return false;
}

auto NextTraitGoalEvaluator::traitRefIsKnowable(const HIRSimplePath& trait, const HIRPathParams& params, const HIRType* type) const -> bool {
    if (orphanCheckTraitRef(params, type, OrphanPerspective::Remote)) {
        return false;
    }

    const auto& traitDef = crate.getTraitByPath(span(), trait);
    if (trait.crateName() == crate.crateName || traitDef.isFundamental) {
        return true;
    }

    return orphanCheckTraitRef(params, type, OrphanPerspective::Local);
}

auto NextTraitGoalEvaluator::hashMix(size_t state, size_t value) -> size_t {
    return state ^ (value + 0x9e3779b97f4a7c15ULL + (state << 6) + (state >> 2));
}

auto NextTraitGoalEvaluator::hashSimplePath(const HIRSimplePath& path) -> size_t {
    size_t result = std::hash<RcString>()(path.crateName());
    for (const auto& component : path.components()) {
        result = hashMix(result, std::hash<RcString>()(component));
    }
    return result;
}

auto NextTraitGoalEvaluator::hashType(const HIRType* type) -> size_t {
    return std::hash<const void*>()(type);
}

auto NextTraitGoalEvaluator::hashValue(const HIRConstGeneric& value) -> size_t {
    auto result = static_cast<size_t>(value.tag());
    if (const auto* infer = value.opt_Infer()) {
        return hashMix(result, infer->index);
    }
    if (const auto* generic = value.opt_Generic()) {
        result = hashMix(result, generic->binding);
        if (generic->group() == GENERICPlaceholder) {
            result = hashMix(result, generic->isSolverExistential() ? generic->solverScope : std::hash<RcString>()(generic->name));
        }
    }
    return result;
}

auto NextTraitGoalEvaluator::goalHash(const HIRSimplePath& trait, const HIRPathParams& params, const HIRType* type, const HIRTraitPath::assocListT* associated) -> size_t {
    size_t result = hashSimplePath(trait);
    result = hashMix(result, params.types.size());
    for (const auto& param : params.types) {
        result = hashMix(result, hashType(param));
    }
    result = hashMix(result, params.values.size());
    for (const auto& value : params.values) {
        result = hashMix(result, hashValue(value));
    }
    result = hashMix(result, hashType(type));
    if (associated && !associated->empty()) {
        result = hashMix(result, associated->size());
        for (const auto& entry : *associated) {
            result = hashMix(result, std::hash<RcString>()(entry.first));
            result = hashMix(result, hashSimplePath(entry.second.sourceTrait.path));
            result = hashMix(result, entry.second.sourceTrait.params.types.size());
            for (const auto& param : entry.second.sourceTrait.params.types) {
                result = hashMix(result, hashType(param));
            }
            result = hashMix(result, entry.second.sourceTrait.params.values.size());
            for (const auto& value : entry.second.sourceTrait.params.values) {
                result = hashMix(result, hashValue(value));
            }
            result = hashMix(result, entry.second.atyParams.types.size());
            for (const auto& param : entry.second.atyParams.types) {
                result = hashMix(result, hashType(param));
            }
            result = hashMix(result, entry.second.atyParams.values.size());
            for (const auto& value : entry.second.atyParams.values) {
                result = hashMix(result, hashValue(value));
            }
            result = hashMix(result, hashType(entry.second.type));
        }
    }
    return result;
}

auto NextTraitGoalEvaluator::goalHashWithEnvironment(size_t hash, const Vector<u32>& existentialEnvironment) -> size_t {
    hash = hashMix(hash, existentialEnvironment.length());
    for (const auto environmentClass : existentialEnvironment) {
        hash = hashMix(hash, environmentClass);
    }
    return hash;
}

auto NextTraitGoalEvaluator::cloneAssociated(const HIRTraitPath::assocListT* associated) -> HIRTraitPath::assocListT {
    HIRTraitPath::assocListT result;
    if (associated) {
        for (const auto& entry : *associated) {
            result.insert({entry.first, entry.second.clone()});
        }
    }
    return result;
}

auto NextTraitGoalEvaluator::monomorphCandidateImpl(const SolverImpl& source, const Monomorphiser& monomorph) const -> SolverImpl {
    ASSERT_BUG(span(), source.traitImpl || source.type, StringView("empty solver candidate response"));
    ASSERT_BUG(span(), !source.traitImpl || !source.type, StringView("solver candidate response mixes trait impl and bounded data"));
    ASSERT_BUG(span(), !source.type || !source.type->isDead(), StringView("dead type in solver candidate response: type=") << static_cast<const void*>(source.type) << StringView(" trait_impl=") << static_cast<const void*>(source.traitImpl));
    if (source.traitImpl) {
        ASSERT_BUG(span(), source.trait, StringView("trait impl solver candidate has no trait definition"));
        return SolverImpl(monomorph.monomorphPathParams(span(), source.implParams, true), *source.trait, source.traitPath, *source.traitImpl);
    }

    auto type = monomorph.monomorphType(span(), source.type, true);
    auto traitArgs = monomorph.monomorphPathParams(span(), source.traitArgs, true);
    HIRTraitPath::assocListT associated;
    for (const auto& entry : source.associated) {
        associated.insert({entry.first, monomorph.monomorphTpAtyEqual(span(), entry.second, true)});
    }
    return SolverImpl(std::move(type), std::move(traitArgs), std::move(associated), source.constness);
}

auto NextTraitGoalEvaluator::ownSolverImpl(SolverImpl source) const -> const SolverImpl* {
    return resolve_.eatCachePool->make<SolverImpl>(std::move(source));
}

auto NextTraitGoalEvaluator::monomorphSolverImpl(const SolverImpl& source, const Monomorphiser& monomorph) const -> const SolverImpl* {
    return ownSolverImpl(monomorphCandidateImpl(source, monomorph));
}

auto NextTraitGoalEvaluator::correlateSolverImplForRead(const SolverImpl& source, const SolverSlotValues& slots, const HIRType* type, const HIRPathParams& params) const -> const SolverImpl* {
    CorrelateSolverResponseSlots correlate(crate.types, slots);
    auto resolveInput = [&](const HIRType* input) {
        const auto* infer = input->opt_Infer();
        return infer && infer->index == ~0u ? input : resolve_.ivars.getType(input);
    };
    correlate.correlateType(resolveInput(type), source.getImplType(crate.types));
    auto responseParams = source.getTraitParams(crate.types);
    if (params.types.size() == responseParams.types.size()) {
        for (size_t i = 0; i < params.types.size(); i++) {
            correlate.correlateType(resolveInput(params.types[i]), responseParams.types[i]);
        }
    }
    return monomorphSolverImpl(source, correlate);
}

auto NextTraitGoalEvaluator::monomorphSolverResponse(const SolverResponse& source, const Monomorphiser& monomorph, bool includeObligations) const -> SolverResponse {
    SolverResponse result;
    result.certainty = source.certainty;
    result.operatorSummary = source.operatorSummary;
    if (source.impl) {
        ASSERT_BUG(span(), source.impl, StringView("solver response has no implementation"));
        result.impl = monomorphSolverImpl(*source.impl, monomorph);
    }
    for (const auto& type : source.slots.typeInputs) {
        result.slots.typeInputs.push_back(monomorph.monomorphType(span(), type, true));
    }
    for (const auto& type : source.slots.types) {
        result.slots.types.push_back(monomorph.monomorphType(span(), type, true));
    }
    for (const auto& value : source.slots.valueInputs) {
        result.slots.valueInputs.push_back(monomorph.monomorphConstgeneric(span(), value, true));
    }
    for (const auto& value : source.slots.values) {
        result.slots.values.push_back(monomorph.monomorphConstgeneric(span(), value, true));
    }
    if (includeObligations) {
        for (const auto& obligation : source.obligations) {
            result.obligations.push_back(
                SolverObligation{
                    monomorph.monomorphType(span(), obligation.type, true),
                    monomorph.monomorphTraitpath(span(), obligation.trait, true),
                }
            );
        }
    }
    for (const auto& equality : source.equalities) {
        result.equalities.push_back(
            SolverTypeEquality{
                monomorph.monomorphType(span(), equality.left, true),
                monomorph.monomorphType(span(), equality.right, true),
            }
        );
    }
    for (const auto& equality : source.valueEqualities) {
        result.valueEqualities.push_back(
            SolverValueEquality{
                monomorph.monomorphConstgeneric(span(), equality.left, true),
                monomorph.monomorphConstgeneric(span(), equality.right, true),
            }
        );
    }
    return result;
}

auto NextTraitGoalEvaluator::extractSlotValues(const CanonicalGoal& goal, const SolverImpl& response, const CanonicalizeTraitGoal& canonicalizer, Certainty certainty) const -> SolverSlotValues {
    SolverSlotValues result;
    if (canonicalizer.typeSlotCount() == 0 && canonicalizer.valueSlotCount() == 0) {
        return result;
    }

    HMTypeInferrence table(crate.types);

    for (size_t i = 0; i < canonicalizer.typeSlotCount(); i++) {
        result.typeInputs.push_back(canonicalizer.canonicalTypeSlot(i));
    }
    for (size_t i = 0; i < canonicalizer.valueSlotCount(); i++) {
        result.valueInputs.push_back(canonicalizer.canonicalValueSlot(i));
    }

    struct InstantiateSlots final: public MonomorphiserNop {
        const CanonicalizeTraitGoal& canonicalizer_;
        RcString foreignTypeName_;
        RcString foreignValueName_;

        ThinVector<const HIRType*> types;
        ThinVector<HIRConstGeneric> values;
        mutable ThinVector<const HIRType*> foreignTypes;
        mutable ThinVector<HIRConstGeneric> foreignValues;

        InstantiateSlots(HIRTypeInterner& interner, HMTypeInferrence& table, const CanonicalizeTraitGoal& canonicalizer)
            : MonomorphiserNop(interner)
            , canonicalizer_(canonicalizer)
            , foreignTypeName_(RcString::newInterned("#solver-foreign-type"))
            , foreignValueName_(RcString::newInterned("#solver-foreign-value"))
        {
            for (size_t i = 0; i < canonicalizer_.typeSlotCount(); i++) {
                types.push_back(table.newIvarTr(canonicalizer_.canonicalTypeSlot(i)->as_Infer().tyClass));
            }
            for (size_t i = 0; i < canonicalizer_.valueSlotCount(); i++) {
                values.push_back(HIRConstGeneric::make_Infer({table.newIvarVal()}));
            }
        }

        const HIRType* monomorphType(const Span& sp, const HIRType* type, bool allowInfer = true) const override {
            if (const auto* infer = type->opt_Infer()) {
                if (isSolverCanonicalInfer(infer->index)) {
                    const size_t slot = infer->index - HIR_INFER_SOLVER_CANONICAL_MIN;
                    if (slot < types.size()) {
                        return types[slot];
                    }
                } else if (infer->index != ~0u && !isAliasInputInfer(infer->index)) {
                    for (size_t i = 0; i < foreignTypes.size(); i++) {
                        if (foreignTypes[i] == type) {
                            return MonomorphiserNop::types.generic(foreignTypeName_, GENERICPlaceholder * 256 + static_cast<unsigned>(i));
                        }
                    }
                    const auto slot = foreignTypes.size();
                    foreignTypes.push_back(type);
                    return MonomorphiserNop::types.generic(foreignTypeName_, GENERICPlaceholder * 256 + static_cast<unsigned>(slot));
                }
            }
            return MonomorphiserNop::monomorphType(sp, type, allowInfer);
        }

        HIRConstGeneric monomorphConstgeneric(const Span& sp, const HIRConstGeneric& value, bool allowInfer) const override {
            if (const auto* infer = value.opt_Infer()) {
                if (isSolverCanonicalInfer(infer->index)) {
                    const size_t slot = infer->index - HIR_INFER_SOLVER_CANONICAL_MIN;
                    if (slot < values.size()) {
                        return values[slot].clone();
                    }
                } else if (infer->index != ~0u && !isAliasInputInfer(infer->index)) {
                    for (size_t i = 0; i < foreignValues.size(); i++) {
                        if (foreignValues[i] == value) {
                            return HIRConstGeneric(HIRGenericRef(foreignValueName_, GENERICPlaceholder * 256 + static_cast<unsigned>(i)));
                        }
                    }
                    const auto slot = foreignValues.size();
                    foreignValues.push_back(value.clone());
                    return HIRConstGeneric(HIRGenericRef(foreignValueName_, GENERICPlaceholder * 256 + static_cast<unsigned>(slot)));
                }
            }
            return MonomorphiserNop::monomorphConstgeneric(sp, value, allowInfer);
        }
    } slots(crate.types, table, canonicalizer);

    ThinVector<const HIRType*> directTypeValues(canonicalizer.typeSlotCount());
    for (auto& value : directTypeValues) {
        value = nullptr;
    }

    const auto responseType = response.getImplType(crate.types);
    const auto responseParams = response.getTraitParams(crate.types);
    if (goal.params.types.size() != responseParams.types.size() || goal.params.values.size() != responseParams.values.size()) {
        ASSERT_BUG(span(), certainty == Certainty::Ambiguous, StringView("proven solver response has a different trait arity than its goal: goal=") << goal.params << StringView(" response=") << responseParams << StringView(" impl=") << response);
        for (size_t i = 0; i < canonicalizer.typeSlotCount(); i++) {
            result.types.push_back(canonicalizer.canonicalTypeSlot(i));
        }
        for (size_t i = 0; i < canonicalizer.valueSlotCount(); i++) {
            result.values.push_back(canonicalizer.canonicalValueSlot(i));
        }
        return result;
    }

    Unifier unifier(span(), table, nullptr, {.bindRigidValues = true});
    bool responseMismatch = false;
    auto unifyInstantiatedType = [&](this auto&& self, const HIRType* left, const HIRType* right) -> void {
        if (responseMismatch) {
            return;
        }
        const auto* leftInfer = left->opt_Infer();
        const auto* rightGeneric = right->opt_Generic();
        if (leftInfer && rightGeneric && rightGeneric->isPlaceholder() && !canonicalizer.originalPlaceholderName(rightGeneric->name)) {
            for (size_t i = 0; i < slots.types.size(); i++) {
                if (slots.types[i]->as_Infer().index == leftInfer->index) {
                    directTypeValues[i] = right;
                    return;
                }
            }
        }
        const auto* leftPath = left->opt_Path();
        const auto* rightPath = right->opt_Path();
        const auto* leftNominal = leftPath ? leftPath->path.data.opt_Generic() : nullptr;
        const auto* rightNominal = rightPath ? rightPath->path.data.opt_Generic() : nullptr;
        if (leftNominal && rightNominal && leftNominal->path == rightNominal->path && leftNominal->params.types.size() == rightNominal->params.types.size() && leftNominal->params.values.size() == rightNominal->params.values.size()) {
            for (size_t i = 0; i < leftNominal->params.types.size(); i++) {
                self(leftNominal->params.types[i], rightNominal->params.types[i]);
            }
            for (size_t i = 0; i < leftNominal->params.values.size(); i++) {
                responseMismatch |= unifier.unifyValues(leftNominal->params.values[i], rightNominal->params.values[i]) == Unifier::Outcome::Mismatch;
            }
            return;
        }
        const auto* leftProjection = leftPath ? leftPath->path.data.opt_UfcsKnown() : nullptr;
        const auto* rightProjection = rightPath ? rightPath->path.data.opt_UfcsKnown() : nullptr;
        if (leftProjection && rightProjection && leftProjection->trait.path == rightProjection->trait.path && leftProjection->item == rightProjection->item && leftProjection->trait.params.types.size() == rightProjection->trait.params.types.size() && leftProjection->trait.params.values.size() == rightProjection->trait.params.values.size() && leftProjection->params.types.size() == rightProjection->params.types.size() && leftProjection->params.values.size() == rightProjection->params.values.size()) {
            self(leftProjection->type, rightProjection->type);
            for (size_t i = 0; i < leftProjection->trait.params.types.size(); i++) {
                self(leftProjection->trait.params.types[i], rightProjection->trait.params.types[i]);
            }
            for (size_t i = 0; i < leftProjection->trait.params.values.size(); i++) {
                responseMismatch |= unifier.unifyValues(leftProjection->trait.params.values[i], rightProjection->trait.params.values[i]) == Unifier::Outcome::Mismatch;
            }
            for (size_t i = 0; i < leftProjection->params.types.size(); i++) {
                self(leftProjection->params.types[i], rightProjection->params.types[i]);
            }
            for (size_t i = 0; i < leftProjection->params.values.size(); i++) {
                responseMismatch |= unifier.unifyValues(leftProjection->params.values[i], rightProjection->params.values[i]) == Unifier::Outcome::Mismatch;
            }
            return;
        }
        const auto* leftErased = left->opt_ErasedType();
        const auto* rightErased = right->opt_ErasedType();
        if (leftErased && rightErased && leftErased->inner.tag() == rightErased->inner.tag()) {
            auto unifyParams = [&](const HIRPathParams& leftParams, const HIRPathParams& rightParams) {
                if (leftParams.types.size() != rightParams.types.size() || leftParams.values.size() != rightParams.values.size()) {
                    responseMismatch = true;
                    return;
                }
                for (size_t i = 0; i < leftParams.types.size(); i++) {
                    self(leftParams.types[i], rightParams.types[i]);
                }
                for (size_t i = 0; i < leftParams.values.size(); i++) {
                    responseMismatch |= unifier.unifyValues(leftParams.values[i], rightParams.values[i]) == Unifier::Outcome::Mismatch;
                }
            };
            auto unifyPath = [&](const HIRPath& leftPath, const HIRPath& rightPath) {
                if (leftPath.data.tag() != rightPath.data.tag()) {
                    responseMismatch = true;
                    return;
                }
                switch (leftPath.data.tag()) {
                    case HIRPathData::TAG_Generic: {
                        const auto& leftData = leftPath.data.as_Generic();
                        const auto& rightData = rightPath.data.as_Generic();
                        if (leftData.path != rightData.path) {
                            responseMismatch = true;
                            return;
                        }
                        unifyParams(leftData.params, rightData.params);
                        break;
                    }
                    case HIRPathData::TAG_UfcsInherent: {
                        const auto& leftData = leftPath.data.as_UfcsInherent();
                        const auto& rightData = rightPath.data.as_UfcsInherent();
                        if (leftData.item != rightData.item) {
                            responseMismatch = true;
                            return;
                        }
                        self(leftData.type, rightData.type);
                        unifyParams(leftData.params, rightData.params);
                        unifyParams(leftData.implParams, rightData.implParams);
                        break;
                    }
                    case HIRPathData::TAG_UfcsKnown: {
                        const auto& leftData = leftPath.data.as_UfcsKnown();
                        const auto& rightData = rightPath.data.as_UfcsKnown();
                        if (leftData.item != rightData.item || leftData.trait.path != rightData.trait.path) {
                            responseMismatch = true;
                            return;
                        }
                        self(leftData.type, rightData.type);
                        unifyParams(leftData.trait.params, rightData.trait.params);
                        unifyParams(leftData.params, rightData.params);
                        break;
                    }
                    case HIRPathData::TAG_UfcsUnknown: {
                        const auto& leftData = leftPath.data.as_UfcsUnknown();
                        const auto& rightData = rightPath.data.as_UfcsUnknown();
                        if (leftData.item != rightData.item) {
                            responseMismatch = true;
                            return;
                        }
                        self(leftData.type, rightData.type);
                        unifyParams(leftData.params, rightData.params);
                        break;
                    }
                }
            };

            unifyParams(leftErased->use, rightErased->use);
            switch (leftErased->inner.tag()) {
                case TypeDataErasedTypeInner::TAG_Fcn: {
                    const auto& leftOrigin = leftErased->inner.as_Fcn();
                    const auto& rightOrigin = rightErased->inner.as_Fcn();
                    if (leftOrigin.index != rightOrigin.index) {
                        responseMismatch = true;
                        return;
                    }
                    unifyPath(leftOrigin.origin, rightOrigin.origin);
                    break;
                }
                case TypeDataErasedTypeInner::TAG_Known:
                    self(leftErased->inner.as_Known(), rightErased->inner.as_Known());
                    break;
                case TypeDataErasedTypeInner::TAG_Alias: {
                    const auto& leftAlias = leftErased->inner.as_Alias();
                    const auto& rightAlias = rightErased->inner.as_Alias();
                    if (leftAlias.inner != rightAlias.inner) {
                        responseMismatch = true;
                        return;
                    }
                    unifyParams(leftAlias.params, rightAlias.params);
                    break;
                }
            }
            return;
        }
        responseMismatch = unifier.unify(left, right) == Unifier::Outcome::Mismatch;
    };
    auto unifyType = [&](const HIRType* left, const HIRType* right) {
        const auto instantiatedLeft = slots.monomorphType(span(), left, true);
        const auto instantiatedRight = slots.monomorphType(span(), right, true);
        unifyInstantiatedType(instantiatedLeft, instantiatedRight);
    };
    auto unifyValue = [&](const HIRConstGeneric& left, const HIRConstGeneric& right) {
        if (responseMismatch) {
            return;
        }
        const auto instantiatedLeft = slots.monomorphConstgeneric(span(), left, true);
        const auto instantiatedRight = slots.monomorphConstgeneric(span(), right, true);
        responseMismatch = unifier.unifyValues(instantiatedLeft, instantiatedRight) == Unifier::Outcome::Mismatch;
    };

    unifyType(goal.type, responseType);
    for (size_t i = 0; i < goal.params.types.size(); i++) {
        unifyType(goal.params.types[i], responseParams.types[i]);
    }
    for (size_t i = 0; i < goal.params.values.size(); i++) {
        unifyValue(goal.params.values[i], responseParams.values[i]);
    }
    if (responseMismatch) {
        for (size_t i = 0; i < canonicalizer.typeSlotCount(); i++) {
            result.types.push_back(canonicalizer.canonicalTypeSlot(i));
        }
        for (size_t i = 0; i < canonicalizer.valueSlotCount(); i++) {
            result.values.push_back(canonicalizer.canonicalValueSlot(i));
        }
        return result;
    }

    struct MaterializeSlots final: public MonomorphiserNop {
        const HMTypeInferrence& table_;
        const CanonicalizeTraitGoal& canonicalizer_;
        const ThinVector<const HIRType*>& types_;
        const ThinVector<HIRConstGeneric>& values_;
        const RcString foreignTypeName_;
        const RcString foreignValueName_;
        const ThinVector<const HIRType*>& foreignTypes_;
        const ThinVector<HIRConstGeneric>& foreignValues_;

        MaterializeSlots(HIRTypeInterner& interner, const HMTypeInferrence& table, const CanonicalizeTraitGoal& canonicalizer, const ThinVector<const HIRType*>& types, const ThinVector<HIRConstGeneric>& values, const ThinVector<const HIRType*>& foreignTypes, const ThinVector<HIRConstGeneric>& foreignValues)
            : MonomorphiserNop(interner)
            , table_(table)
            , canonicalizer_(canonicalizer)
            , types_(types)
            , values_(values)
            , foreignTypeName_(RcString::newInterned("#solver-foreign-type"))
            , foreignValueName_(RcString::newInterned("#solver-foreign-value"))
            , foreignTypes_(foreignTypes)
            , foreignValues_(foreignValues)
        {
        }

        const HIRType* getType(const Span& sp, const HIRGenericRef& generic) const override {
            if (generic.name == foreignTypeName_ && generic.binding >= GENERICPlaceholder * 256) {
                const auto slot = generic.binding - GENERICPlaceholder * 256;
                if (slot < foreignTypes_.size()) {
                    return foreignTypes_[slot];
                }
            }
            return MonomorphiserNop::getType(sp, generic);
        }

        HIRConstGeneric getValue(const Span& sp, const HIRGenericRef& generic) const override {
            if (generic.name == foreignValueName_ && generic.binding >= GENERICPlaceholder * 256) {
                const auto slot = generic.binding - GENERICPlaceholder * 256;
                if (slot < foreignValues_.size()) {
                    return foreignValues_[slot].clone();
                }
            }
            return MonomorphiserNop::getValue(sp, generic);
        }

        const HIRType* monomorphType(const Span& sp, const HIRType* type, bool allowInfer = true) const override {
            if (const auto* infer = type->opt_Infer(); infer && infer->index < table_.ivars.size()) {
                const auto* resolved = table_.getType(type);
                if (resolved != type) {
                    return this->monomorphType(sp, resolved, allowInfer);
                }
                for (size_t i = 0; i < types_.size(); i++) {
                    if (types_[i]->as_Infer().index == infer->index) {
                        return canonicalizer_.canonicalTypeSlot(i);
                    }
                }
            }
            return MonomorphiserNop::monomorphType(sp, type, allowInfer);
        }

        HIRConstGeneric monomorphConstgeneric(const Span& sp, const HIRConstGeneric& value, bool allowInfer) const override {
            if (const auto* infer = value.opt_Infer(); infer && infer->index < table_.values.size()) {
                const auto& resolved = table_.getValue(value);
                if (resolved != value) {
                    return this->monomorphConstgeneric(sp, resolved, allowInfer);
                }
                for (size_t i = 0; i < values_.size(); i++) {
                    if (values_[i].as_Infer().index == infer->index) {
                        return canonicalizer_.canonicalValueSlot(i);
                    }
                }
            }
            return MonomorphiserNop::monomorphConstgeneric(sp, value, allowInfer);
        }
    } materialize(crate.types, table, canonicalizer, slots.types, slots.values, slots.foreignTypes, slots.foreignValues);

    for (size_t i = 0; i < slots.types.size(); i++) {
        result.types.push_back(directTypeValues[i] ? directTypeValues[i] : materialize.monomorphType(span(), table.getType(slots.types[i]), true));
    }
    for (const auto& value : slots.values) {
        result.values.push_back(materialize.monomorphConstgeneric(span(), table.getValue(value), true));
    }
    return result;
}

auto NextTraitGoalEvaluator::goalMatches(const GoalKey& goal, const HIRSimplePath& trait, const HIRPathParams& params, const HIRType* type, const HIRTraitPath::assocListT* associated, const Vector<u32>* existentialEnvironment, bool suppressAmbiguity) -> bool {
    if (goal.trait != trait || goal.params != params || goal.type != type || goal.suppressAmbiguity != suppressAmbiguity) {
        return false;
    }
    const auto environmentSize = existentialEnvironment ? existentialEnvironment->length() : 0;
    if (goal.existentialEnvironment.size() != environmentSize ||
        (existentialEnvironment && !std::equal(goal.existentialEnvironment.begin(), goal.existentialEnvironment.end(), existentialEnvironment->begin()))) {
        return false;
    }
    if (!associated || associated->empty()) {
        return goal.associated.empty();
    }
    if (goal.associated.size() != associated->size()) {
        return false;
    }
    auto left = goal.associated.begin();
    auto right = associated->begin();
    for (; left != goal.associated.end(); ++left, ++right) {
        if (left->first != right->first || left->second.sourceTrait != right->second.sourceTrait || left->second.atyParams != right->second.atyParams || left->second.type != right->second.type) {
            return false;
        }
    }
    return true;
}

auto NextTraitGoalEvaluator::findCachedGoal(size_t hash, const HIRSimplePath& trait, const HIRPathParams& params, const HIRType* type, const HIRTraitPath::assocListT* associated, const Vector<u32>* existentialEnvironment, bool suppressAmbiguity) const -> CachedGoal* {
    const auto range = goalCacheIndex.equal_range(hash);
    for (auto it = range.first; it != range.second; ++it) {
        if (goalMatches(it->second->goal, trait, params, type, associated, existentialEnvironment, suppressAmbiguity)) {
            return it->second;
        }
    }
    return nullptr;
}

auto NextTraitGoalEvaluator::findActiveGoal(size_t hash, const HIRSimplePath& trait, const HIRPathParams& params, const HIRType* type, const HIRTraitPath::assocListT* associated) const -> GoalKey* {
    const auto range = activeGoalIndex.equal_range(hash);
    for (auto it = range.first; it != range.second; ++it) {
        if (goalMatches(*it->second, trait, params, type, associated)) {
            return it->second;
        }
    }
    return nullptr;
}

auto NextTraitGoalEvaluator::pushActiveGoal(size_t hash, const HIRSimplePath& trait, const HIRPathParams& params, const HIRType* type, const HIRTraitPath::assocListT* associated) -> GoalKey* {
    auto* goal = activeGoalNodes.make(hash, trait, params, type, associated);
    goalStack.pushBack(goal);
    activeGoalIndex.emplace(hash, goal);
    return goal;
}

auto NextTraitGoalEvaluator::popActiveGoal(GoalKey* goal) -> void {
    BUG_ASSERT(!goalStack.empty() && goalStack.back() == goal);
    const auto range = activeGoalIndex.equal_range(goal->hash);
    for (auto it = range.first; it != range.second; ++it) {
        if (it->second == goal) {
            activeGoalIndex.erase(it);
            goalStack.popBack();
            activeGoalNodes.release(goal);
            return;
        }
    }
    BUG_ASSERT(!"next-solver active goal missing from hash index");
    std::abort();
}

auto NextTraitGoalEvaluator::cacheGoal(size_t hash, const HIRSimplePath& trait, const HIRPathParams& params, const HIRType* type, const HIRTraitPath::assocListT* associated, Certainty certainty, bool persistent, bool responseIsIdentity, const Vector<u32>* existentialEnvironment) -> CachedGoal* {
    auto* goal = cachedGoalNodes.make(hash, trait, params, type, associated, certainty, existentialEnvironment);
    goal->persistent = persistent;
    goal->responseIsIdentity = responseIsIdentity;
    goalCache.pushBack(goal);
    goalCacheIndex.emplace(hash, goal);
    return goal;
}

auto NextTraitGoalEvaluator::cacheResponse(size_t hash, const HIRSimplePath& trait, const HIRPathParams& params, const HIRType* type, const HIRTraitPath::assocListT* associated, const SolverResponse* response, const Vector<u32>* existentialEnvironment, bool suppressAmbiguity) -> CachedGoal* {
    ASSERT_BUG(span(), response, StringView("cannot cache an empty solver response"));
    auto* cached = findCachedGoal(hash, trait, params, type, associated, existentialEnvironment, suppressAmbiguity);
    const auto certainty = response->certainty;
    if (!cached) {
        cached = cachedGoalNodes.make(hash, trait, params, type, associated, certainty, existentialEnvironment, suppressAmbiguity);
        goalCache.pushBack(cached);
        goalCacheIndex.emplace(hash, cached);
    }
    cached->certainty = certainty;
    cached->response = response;
    cached->hasResponse = true;
    cached->responseIsIdentity = response->certainty == Certainty::Proven && response->impl &&
        response->impl->getImplType(crate.types) == type && response->impl->getTraitParamsRef(crate.types) == params &&
        response->equalities.empty() && response->valueEqualities.empty() && response->obligations.empty() &&
        std::equal(response->slots.typeInputs.begin(), response->slots.typeInputs.end(), response->slots.types.begin(), response->slots.types.end()) &&
        std::equal(response->slots.valueInputs.begin(), response->slots.valueInputs.end(), response->slots.values.begin(), response->slots.values.end());
    return cached;
}

auto NextTraitGoalEvaluator::clearGoalCache(bool clearCanonicalNoEffectResponses) -> void {
    goalCacheIndex.clear();
    rawNestedNoEffectResponses.clear();
    rawNestedNoEffectResponseIndex.visit([](auto& indexes) {
        indexes.clear();
    });
    if (clearCanonicalNoEffectResponses) {
        canonicalNestedNoEffectResponses.clear();
        canonicalNestedNoEffectResponseIndex.visit([](auto& indexes) {
            indexes.clear();
        });
        emptyRootGoals.clear();
        emptyRootGoalIndex.visit([](auto& indexes) {
            indexes.clear();
        });
    }
    size_t kept = 0;
    for (auto* goal : goalCache) {
        if (goal->persistent) {
            goalCache.mut(kept++) = goal;
            goalCacheIndex.emplace(goal->goal.hash, goal);
        } else {
            cachedGoalNodes.release(goal);
        }
    }
    while (goalCache.length() > kept) {
        goalCache.popBack();
    }
}

auto NextTraitGoalEvaluator::rootAssemblyKnownEmpty(size_t hash, const HIRSimplePath& trait, const HIRPathParams& params, const HIRType* type, const Vector<u32>& existentialEnvironment, bool includeMagicCandidates) const -> bool {
    const auto* indexes = emptyRootGoalIndex.find(hash);
    if (!indexes) {
        return false;
    }
    for (const auto index : *indexes) {
        const auto& cached = emptyRootGoals[index];
        if (cached.includeMagicCandidates == includeMagicCandidates && goalMatches(cached.goal, trait, params, type, nullptr, &existentialEnvironment)) {
            return true;
        }
    }
    return false;
}

auto NextTraitGoalEvaluator::rememberEmptyRootAssembly(size_t hash, const HIRSimplePath& trait, const HIRPathParams& params, const HIRType* type, const Vector<u32>& existentialEnvironment, bool includeMagicCandidates) -> void {
    const auto index = emptyRootGoals.size();
    emptyRootGoals.emplace_back(hash, trait, params, type, existentialEnvironment, includeMagicCandidates);
    auto* indexes = emptyRootGoalIndex.find(hash);
    if (!indexes) {
        indexes = emptyRootGoalIndex.insert(hash);
    }
    indexes->push_back(index);
}

auto NextTraitGoalEvaluator::rawNestedNoEffectHash(const HIRSimplePath& trait, const HIRPathParams& params, const HIRType* type, const HIRTraitPath::assocListT* associated, const Candidate& candidate) const -> size_t {
    size_t hash = goalHash(trait, params, type, associated);
    hash = hashMix(hash, reinterpret_cast<uintptr_t>(candidate.impl.traitImpl));
    hash = hashMix(hash, reinterpret_cast<uintptr_t>(candidate.markerImpl));
    const auto& candidateParams = candidate.impl.traitImpl ? candidate.impl.implParams : candidate.markerImplParams;
    hash = hashMix(hash, candidateParams.types.size());
    for (const auto& candidateType : candidateParams.types) {
        hash = hashMix(hash, hashType(candidateType));
    }
    return hashMix(hash, candidateParams.values.size());
}

auto NextTraitGoalEvaluator::findRawNestedNoEffectResponse(const HIRSimplePath& trait, const HIRPathParams& params, const HIRType* type, const HIRTraitPath::assocListT* associated, const Candidate& candidate, Certainty& certainty) const -> bool {
    const auto hash = rawNestedNoEffectHash(trait, params, type, associated, candidate);
    const auto* indexes = rawNestedNoEffectResponseIndex.find(hash);
    if (!indexes) {
        return false;
    }
    const auto& candidateParams = candidate.impl.traitImpl ? candidate.impl.implParams : candidate.markerImplParams;
    for (const auto index : *indexes) {
        const auto& cached = rawNestedNoEffectResponses[index];
        if (cached.traitImpl == candidate.impl.traitImpl && cached.markerImpl == candidate.markerImpl &&
            cached.candidateParams == candidateParams && goalMatches(cached.goal, trait, params, type, associated)) {
            certainty = cached.certainty;
            return true;
        }
    }
    return false;
}

auto NextTraitGoalEvaluator::rememberRawNestedNoEffectResponse(const HIRSimplePath& trait, const HIRPathParams& params, const HIRType* type, const HIRTraitPath::assocListT* associated, const Candidate& candidate, Certainty certainty) -> void {
    Certainty cachedCertainty;
    if (findRawNestedNoEffectResponse(trait, params, type, associated, candidate, cachedCertainty)) {
        return;
    }
    const auto hash = rawNestedNoEffectHash(trait, params, type, associated, candidate);
    rawNestedNoEffectResponses.emplace_back(hash, trait, params, type, associated, candidate, certainty);
    auto* indexes = rawNestedNoEffectResponseIndex.find(hash);
    if (!indexes) {
        indexes = rawNestedNoEffectResponseIndex.insert(hash);
    }
    indexes->push_back(rawNestedNoEffectResponses.size() - 1);
}

auto NextTraitGoalEvaluator::canonicalNestedNoEffectHash(size_t goalHash, const Candidate& candidate, const HIRPathParams& candidateParams) const -> size_t {
    auto hash = hashMix(goalHash, reinterpret_cast<uintptr_t>(candidate.impl.traitImpl));
    hash = hashMix(hash, reinterpret_cast<uintptr_t>(candidate.markerImpl));
    hash = hashMix(hash, candidateParams.types.size());
    for (const auto& candidateType : candidateParams.types) {
        hash = hashMix(hash, hashType(candidateType));
    }
    return hashMix(hash, candidateParams.values.size());
}

auto NextTraitGoalEvaluator::findCanonicalNestedNoEffectResponse(size_t goalHash, const HIRSimplePath& trait, const HIRPathParams& params, const HIRType* type, const HIRTraitPath::assocListT* associated, const Vector<u32>& existentialEnvironment, const Candidate& candidate, const HIRPathParams& candidateParams, Certainty& certainty) const -> bool {
    const auto hash = canonicalNestedNoEffectHash(goalHash, candidate, candidateParams);
    const auto* indexes = canonicalNestedNoEffectResponseIndex.find(hash);
    if (!indexes) {
        return false;
    }
    for (const auto index : *indexes) {
        const auto& cached = canonicalNestedNoEffectResponses[index];
        if (cached.traitImpl == candidate.impl.traitImpl && cached.markerImpl == candidate.markerImpl &&
            cached.candidateParams == candidateParams && goalMatches(cached.goal, trait, params, type, associated, &existentialEnvironment)) {
            certainty = cached.certainty;
            return true;
        }
    }
    return false;
}

auto NextTraitGoalEvaluator::rememberCanonicalNestedNoEffectResponse(const GoalKey& goal, const Candidate& candidate, const HIRPathParams& candidateParams, Certainty certainty) -> void {
    Certainty cachedCertainty;
    Vector<u32> existentialEnvironment;
    for (const auto environmentClass : goal.existentialEnvironment) {
        existentialEnvironment.pushBack(environmentClass);
    }
    if (findCanonicalNestedNoEffectResponse(goal.hash, goal.trait, goal.params, goal.type, goal.associated.empty() ? nullptr : &goal.associated, existentialEnvironment, candidate, candidateParams, cachedCertainty)) {
        return;
    }
    const auto hash = canonicalNestedNoEffectHash(goal.hash, candidate, candidateParams);
    canonicalNestedNoEffectResponses.emplace_back(hash, goal.trait, goal.params, goal.type, goal.associated.empty() ? nullptr : &goal.associated, existentialEnvironment, candidate, candidateParams, certainty);
    auto* indexes = canonicalNestedNoEffectResponseIndex.find(hash);
    if (!indexes) {
        indexes = canonicalNestedNoEffectResponseIndex.insert(hash);
    }
    indexes->push_back(canonicalNestedNoEffectResponses.size() - 1);
}

auto NextTraitGoalEvaluator::canonicalGoalIsRigid(const CanonicalGoal& canonical) -> bool {
    auto typeIsRigid = [](const HIRType* ty) {
        return !visitTyWith(ty, [](const HIRType* inner) {
            if (inner->is_Infer()) {
                return true;
            }
            if (const auto* generic = inner->opt_Generic(); generic && generic->group() == GENERICPlaceholder && !generic->isSolverExistential()) {
                return true;
            }
            if (const auto* path = inner->opt_Path(); path && path->binding.is_Unbound()) {
                return true;
            }
            return false;
        });
    };
    if (!typeIsRigid(canonical.type)) {
        return false;
    }
    for (const auto& ty : canonical.params.types) {
        if (!typeIsRigid(ty)) {
            return false;
        }
    }
    for (const auto& value : canonical.params.values) {
        if (value.is_Infer()) {
            return false;
        }
        if (value.is_Generic() && value.as_Generic().group() == GENERICPlaceholder && !value.as_Generic().isSolverExistential()) {
            return false;
        }
    }
    for (const auto& entry : canonical.associated) {
        if (!typeIsRigid(entry.second.type)) {
            return false;
        }
    }
    return true;
}

auto NextTraitGoalEvaluator::boundedAssociated(const SolverImpl& impl) -> const HIRTraitPath::assocListT* {
    return impl.isTraitImpl() ? nullptr : &impl.associated;
}

auto NextTraitGoalEvaluator::associatedResponsesEqual(const HIRTraitPath::assocListT* left, const HIRTraitPath::assocListT* right) -> bool {
    const auto leftSize = left ? left->size() : 0;
    const auto rightSize = right ? right->size() : 0;
    if (leftSize != rightSize) {
        return false;
    }
    if (leftSize == 0) {
        return true;
    }
    auto li = left->begin();
    auto ri = right->begin();
    for (; li != left->end(); ++li, ++ri) {
        if (li->first != ri->first || li->second.ord(ri->second) != OrdEqual) {
            return false;
        }
    }
    return true;
}

auto NextTraitGoalEvaluator::isSameImpl(const SolverImpl& left, const SolverImpl& right) const -> bool {
    if (left.traitImpl || right.traitImpl) {
        return left.traitImpl && right.traitImpl && left.traitImpl == right.traitImpl && left.implParams == right.implParams;
    }
    return left.getImplType(crate.types) == right.getImplType(crate.types) && left.getTraitParamsRef(crate.types) == right.getTraitParamsRef(crate.types) && associatedResponsesEqual(boundedAssociated(left), boundedAssociated(right));
}

auto NextTraitGoalEvaluator::paramEnvCandidateIsNonGlobal(const Candidate& candidate) const -> bool {
    if (candidate.source != CandidateSource::ParamEnv) {
        return false;
    }
    auto typeIsNonGlobal = [&](const HIRType* type) {
        return typeHasUnknown(resolve_.expandAssociatedTypes(span(), type));
    };
    auto paramsAreNonGlobal = [&](const HIRPathParams& params) {
        for (const auto& type : params.types) {
            if (typeIsNonGlobal(type)) {
                return true;
            }
        }
        return false;
    };
    {
        const auto* implSelf = resolve_.resolveType(candidate.impl.getImplType(crate.types));
        if (const auto* selfPath = implSelf->opt_Path(); selfPath && selfPath->binding.is_Opaque()) {
            return true;
        }
    }
    if (typeIsNonGlobal(candidate.impl.getImplType(crate.types)) || paramsAreNonGlobal(candidate.impl.getTraitParamsRef(crate.types))) {
        return true;
    }
    if (const auto* associatedTypes = boundedAssociated(candidate.impl)) {
        for (const auto& associated : *associatedTypes) {
            if (paramsAreNonGlobal(associated.second.sourceTrait.params) || paramsAreNonGlobal(associated.second.atyParams) || typeIsNonGlobal(associated.second.type)) {
                return true;
            }
        }
    }
    return false;
}

auto NextTraitGoalEvaluator::pushCandidate(size_t frameIndex, SolverImpl impl, bool headExact, Certainty headRelation, const HIRMarkerImpl* markerImpl, HIRPathParams markerImplParams, bool autoBuiltin, CandidateSource source, bool headNormalizationAmbiguity, ThinVector<SolverTypeEquality> headEqualities, ThinVector<SolverValueEquality> headValueEqualities) -> void {
    auto& candidates = frames[frameIndex]->candidates;
    for (size_t i = 0; i < candidates.length(); i++) {
        const bool sameSource = candidates[i]->markerImpl == markerImpl && candidates[i]->autoBuiltin == autoBuiltin && candidates[i]->source == source;
        const bool same = markerImpl ? sameSource && candidates[i]->markerImplParams == markerImplParams : sameSource && isSameImpl(candidates[i]->impl, impl);
        if (same) {
            candidates[i]->headExact &= headExact;
            if (headRelation != Certainty::Proven) {
                candidates[i]->headRelation = Certainty::Ambiguous;
            }
            candidates[i]->headNormalizationAmbiguity |= headNormalizationAmbiguity;
            for (auto& equality : headEqualities) {
                candidates[i]->headEqualities.push_back(std::move(equality));
            }
            for (auto& equality : headValueEqualities) {
                candidates[i]->headValueEqualities.push_back(std::move(equality));
            }
            return;
        }
    }
    candidates.pushBack(candidateNodes.make(std::move(impl), headExact, headRelation, markerImpl, std::move(markerImplParams), autoBuiltin, source, headNormalizationAmbiguity, std::move(headEqualities), std::move(headValueEqualities)));
}

auto NextTraitGoalEvaluator::relateAssembledHead(CandidateSource source, const HIRPathParams& goalParams, const HIRType* goalType, SolverImpl& impl, bool& headNormalizationAmbiguity, ThinVector<SolverTypeEquality>& headEqualities, ThinVector<SolverValueEquality>& headValueEqualities) const -> Certainty {
    struct HrtbTypeBinding {
        HIRGenericRef generic;
        const HIRType* probe;
    };

    struct HrtbValueBinding {
        HIRGenericRef generic;
        unsigned probeIndex;
    };

    struct InstantiateHrtb final: public MonomorphiserNop {
        HMTypeInferrence& table_;

        mutable Vector<HrtbTypeBinding> typeBindings;
        mutable Vector<HrtbValueBinding> valueBindings;

        InstantiateHrtb(HIRTypeInterner& types, HMTypeInferrence& table)
            : MonomorphiserNop(types)
            , table_(table)
        {
        }

        const HIRType* getType(const Span& sp, const HIRGenericRef& generic) const override {
            if (generic.group() != GENERICHrtb) {
                return MonomorphiserNop::getType(sp, generic);
            }
            for (const auto& binding : typeBindings) {
                if (binding.generic.binding == generic.binding) {
                    return binding.probe;
                }
            }
            auto probe = table_.newIvarTr();
            typeBindings.pushBack(HrtbTypeBinding{generic, probe});
            return probe;
        }

        HIRConstGeneric getValue(const Span& sp, const HIRGenericRef& generic) const override {
            if (generic.group() != GENERICHrtb) {
                return MonomorphiserNop::getValue(sp, generic);
            }
            for (const auto& binding : valueBindings) {
                if (binding.generic.binding == generic.binding) {
                    return HIRConstGeneric::make_Infer({binding.probeIndex});
                }
            }
            const auto probeIndex = table_.newIvarVal();
            valueBindings.pushBack(HrtbValueBinding{generic, probeIndex});
            return HIRConstGeneric::make_Infer({probeIndex});
        }
    };

    const auto originalCandidateType = impl.getImplType(crate.types);
    const auto& originalCandidateParams = impl.getTraitParamsRef(crate.types);
    if (originalCandidateParams.types.size() != goalParams.types.size() || originalCandidateParams.values.size() != goalParams.values.size()) {
        return Certainty::NoSolution;
    }

    const bool typeHasHrtb = typeContainsGenericGroup(originalCandidateType, GENERICHrtb);
    const bool paramsHaveHrtb = pathParamsContainGenericGroup(originalCandidateParams, GENERICHrtb);

    const HIRTraitPath::assocListT* originalAssoc = impl.isTraitImpl() ? nullptr : &impl.associated;
    Vector<RcString> hrtbAssocNames;
    if (originalAssoc) {
        for (const auto& entry : *originalAssoc) {
            if (pathParamsContainGenericGroup(entry.second.sourceTrait.params, GENERICHrtb) || pathParamsContainGenericGroup(entry.second.atyParams, GENERICHrtb) || typeContainsGenericGroup(entry.second.type, GENERICHrtb)) {
                hrtbAssocNames.pushBack(entry.first);
            }
        }
    }
    const bool headHasHrtb = typeHasHrtb || paramsHaveHrtb;
    const bool hasHrtb = headHasHrtb || !hrtbAssocNames.empty();

    const auto snapshot = resolve_.ivars.snapshot();
    STD_DEFER {
        resolve_.ivars.rollbackTo(snapshot);
    };

    InstantiateHrtb instantiate(crate.types, resolve_.ivars);
    auto instantiatedType = originalCandidateType;
    if (typeHasHrtb) {
        instantiatedType = instantiate.monomorphType(span(), originalCandidateType, true);
    }
    HIRPathParams instantiatedParams;
    if (paramsHaveHrtb) {
        instantiatedParams = instantiate.monomorphPathParams(span(), originalCandidateParams, true);
    }
    HIRTraitPath::assocListT instantiatedAssoc;
    if (hasHrtb && originalAssoc) {
        for (const auto& entry : *originalAssoc) {
            bool entryHasHrtb = false;
            for (const auto& name : hrtbAssocNames) {
                if (name == entry.first) {
                    entryHasHrtb = true;
                    break;
                }
            }
            instantiatedAssoc.insert({
                entry.first,
                entryHasHrtb ? instantiate.monomorphTpAtyEqual(span(), entry.second, true) : entry.second.clone(),
            });
        }
    }

    const auto candidateType = typeHasHrtb ? instantiatedType : originalCandidateType;
    const HIRPathParams& candidateParams = paramsHaveHrtb ? instantiatedParams : originalCandidateParams;

    const bool paramEnvHead = source == CandidateSource::ParamEnv || source == CandidateSource::AliasBound;
    Unifier unifier(
        span(),
        resolve_.ivars,
        &resolve_,
        {
            .bindRigidValues = headHasHrtb,
            .relateProjectionInputs = paramEnvHead,
            .rigidProjectionsAreDistinct = paramEnvHead,
        }
    );
    auto relation = unifier.unify(goalType, candidateType);
    if (relation == Unifier::Outcome::Mismatch) {
        return Certainty::NoSolution;
    }
    const auto selfTypePending = unifier.pending().length();
    const auto selfValuePending = unifier.pendingValues().size();
    for (size_t i = 0; i < candidateParams.types.size(); i++) {
        relation = unifier.unify(goalParams.types[i], candidateParams.types[i]);
        if (relation == Unifier::Outcome::Mismatch) {
            return Certainty::NoSolution;
        }
    }
    for (size_t i = 0; i < candidateParams.values.size(); i++) {
        relation = unifier.unifyValues(goalParams.values[i], candidateParams.values[i]);
        if (relation == Unifier::Outcome::Mismatch) {
            return Certainty::NoSolution;
        }
    }

    if (hasHrtb) {
        struct MaterializeHrtb final: public MonomorphiserNop {
            const HMTypeInferrence& table_;
            const Vector<HrtbTypeBinding>& typeBindings_;
            const Vector<HrtbValueBinding>& valueBindings_;

            MaterializeHrtb(HIRTypeInterner& types, const HMTypeInferrence& table, const Vector<HrtbTypeBinding>& typeBindings, const Vector<HrtbValueBinding>& valueBindings)
                : MonomorphiserNop(types)
                , table_(table)
                , typeBindings_(typeBindings)
                , valueBindings_(valueBindings)
            {
            }

            const HIRType* monomorphType(const Span& sp, const HIRType* type, bool allowInfer = true) const override {
                for (const auto& binding : typeBindings_) {
                    if (binding.probe != type) {
                        continue;
                    }
                    const auto* resolved = table_.getType(type);
                    if (resolved == type) {
                        return types.generic(binding.generic.name, binding.generic.binding);
                    }
                    return this->monomorphType(sp, resolved, allowInfer);
                }
                return MonomorphiserNop::monomorphType(sp, type, allowInfer);
            }

            HIRConstGeneric monomorphConstgeneric(const Span& sp, const HIRConstGeneric& value, bool allowInfer) const override {
                if (const auto* infer = value.opt_Infer()) {
                    for (const auto& binding : valueBindings_) {
                        if (binding.probeIndex != infer->index) {
                            continue;
                        }
                        const auto& resolved = table_.getValue(value);
                        if (resolved == value) {
                            return binding.generic;
                        }
                        return this->monomorphConstgeneric(sp, resolved, allowInfer);
                    }
                }
                return MonomorphiserNop::monomorphConstgeneric(sp, value, allowInfer);
            }
        };

        MaterializeHrtb materialize(crate.types, resolve_.ivars, instantiate.typeBindings, instantiate.valueBindings);
        auto stableType = typeHasHrtb ? materialize.monomorphType(span(), candidateType, true) : originalCandidateType;
        auto stableParams = paramsHaveHrtb ? materialize.monomorphPathParams(span(), candidateParams, true) : originalCandidateParams.clone();
        HIRTraitPath::assocListT stableAssoc;
        if (originalAssoc) {
            for (const auto& entry : instantiatedAssoc) {
                bool entryHasHrtb = false;
                for (const auto& name : hrtbAssocNames) {
                    if (name == entry.first) {
                        entryHasHrtb = true;
                        break;
                    }
                }
                stableAssoc.insert({
                    entry.first,
                    entryHasHrtb ? materialize.monomorphTpAtyEqual(span(), entry.second, true) : entry.second.clone(),
                });
            }
        }
        impl = SolverImpl(mv$(stableType), mv$(stableParams), mv$(stableAssoc), impl.constness);
    }

    const auto stableCandidateType = impl.getImplType(crate.types);
    const auto& stableCandidateParams = impl.getTraitParamsRef(crate.types);

    if (goalType != stableCandidateType) {
        headEqualities.push_back(SolverTypeEquality{goalType, stableCandidateType});
    }
    for (size_t i = 0; i < stableCandidateParams.types.size(); i++) {
        if (goalParams.types[i] != stableCandidateParams.types[i]) {
            headEqualities.push_back(SolverTypeEquality{goalParams.types[i], stableCandidateParams.types[i]});
        }
    }
    for (size_t i = 0; i < stableCandidateParams.values.size(); i++) {
        if (goalParams.values[i] != stableCandidateParams.values[i]) {
            headValueEqualities.push_back(SolverValueEquality{goalParams.values[i].clone(), stableCandidateParams.values[i].clone()});
        }
    }
    const auto isCanonicalTypeInput = [](const HIRType* type) {
        const auto* infer = type->opt_Infer();
        return infer && isSolverCanonicalInfer(infer->index);
    };
    const auto isCanonicalValueInput = [](const HIRConstGeneric& value) {
        const auto* infer = value.opt_Infer();
        return infer && isSolverCanonicalInfer(infer->index);
    };
    bool unresolved = false;
    headNormalizationAmbiguity = false;
    for (size_t i = 0; i < unifier.pending().length(); i++) {
        const auto& equality = unifier.pending()[i];
        const bool exportedDefiningOpaque = containsDefiningOpaque(equality.left) != containsDefiningOpaque(equality.right);
        if (exportedDefiningOpaque || (i >= selfTypePending && (isCanonicalTypeInput(equality.left) || isCanonicalTypeInput(equality.right)))) {
            continue;
        }
        unresolved = true;
        headNormalizationAmbiguity |= resolve_.hasAssociatedType(equality.left) || resolve_.hasAssociatedType(equality.right);
    }
    for (size_t i = 0; i < unifier.pendingValues().size(); i++) {
        const auto& equality = unifier.pendingValues()[i];
        if (i >= selfValuePending && (isCanonicalValueInput(equality.left) || isCanonicalValueInput(equality.right))) {
            continue;
        }
        unresolved = true;
        headNormalizationAmbiguity = true;
    }
    return unresolved ? Certainty::Ambiguous : Certainty::Proven;
}

auto NextTraitGoalEvaluator::assembledHeadIsExact(const HIRPathParams& goalParams, const HIRType* goalType, const SolverImpl& impl) const -> bool {
    return goalType == impl.getImplType(crate.types) && goalParams == impl.getTraitParamsRef(crate.types);
}

auto NextTraitGoalEvaluator::unifyImplHead(const HIRGenericParams& implParamsDef, const HIRPathParams& implTraitArgs, const HIRType* implType, const HIRPathParams& goalParams, const HIRType* goalType, HIRPathParams& outputParams, bool& headNormalizationAmbiguity, ThinVector<SolverTypeEquality>& headEqualities, ThinVector<SolverValueEquality>& headValueEqualities) -> Certainty {
    const auto snapshot = resolve_.ivars.snapshot();
    STD_DEFER {
        resolve_.ivars.rollbackTo(snapshot);
    };

    auto inferenceParams = resolve_.makeFreshImplParams(implParamsDef);
    auto monomorph = MonomorphStatePtr(crate.types, nullptr, &inferenceParams, nullptr);
    const auto candidateType = monomorph.monomorphType(span(), implType, true);

    Unifier unifier(span(), resolve_.ivars, &resolve_);

    auto relation = unifier.unify(goalType, candidateType);
    if (relation == Unifier::Outcome::Mismatch) {
        return Certainty::NoSolution;
    }
    auto resolvedInferenceParams = inferenceParams.clone();
    for (auto& type : resolvedInferenceParams.types) {
        type = resolve_.ivars.getType(type);
    }
    for (auto& value : resolvedInferenceParams.values) {
        const auto& resolved = resolve_.ivars.getValue(value);
        if (resolved != value) {
            value = resolved.clone();
        }
    }
    auto resolvedMonomorph = MonomorphStatePtr(crate.types, nullptr, &resolvedInferenceParams, nullptr);
    resolvedMonomorph.setConstevalState(resolve_.board(), HIRItemPath(""));
    const auto candidateParams = resolvedMonomorph.monomorphPathParams(span(), implTraitArgs, true);
    if (candidateParams.types.size() != goalParams.types.size() || candidateParams.values.size() != goalParams.values.size()) {
        return Certainty::NoSolution;
    }
    for (size_t i = 0; i < candidateParams.types.size(); i++) {
        relation = unifier.unify(goalParams.types[i], candidateParams.types[i]);
        if (relation == Unifier::Outcome::Mismatch) {
            return Certainty::NoSolution;
        }
    }
    for (size_t i = 0; i < candidateParams.values.size(); i++) {
        relation = unifier.unifyValues(goalParams.values[i], candidateParams.values[i]);
        if (relation == Unifier::Outcome::Mismatch) {
            return Certainty::NoSolution;
        }
    }

    const auto& stableExistentials = implExistentials(implParamsDef);

    struct MaterializeCandidate final: public MonomorphiserNop {
        const HMTypeInferrence& table;
        const HIRPathParams& inferenceParams;
        const HIRPathParams& stableExistentials;

        MaterializeCandidate(HIRTypeInterner& types, const HMTypeInferrence& table, const HIRPathParams& inferenceParams, const HIRPathParams& stableExistentials)
            : MonomorphiserNop(types)
            , table(table)
            , inferenceParams(inferenceParams)
            , stableExistentials(stableExistentials)
        {
        }

        const HIRType* monomorphType(const Span& sp, const HIRType* type, bool allowInfer = true) const override {
            if (const auto* infer = type->opt_Infer()) {
                for (size_t i = 0; i < inferenceParams.types.size(); i++) {
                    const auto* parameter = inferenceParams.types[i]->opt_Infer();
                    if (!parameter || parameter->index != infer->index) {
                        continue;
                    }
                    const auto* resolved = table.getType(type);
                    if (resolved == type) {
                        return stableExistentials.types[i];
                    }
                    return this->monomorphType(sp, resolved, allowInfer);
                }
                return type;
            }
            return Monomorphiser::monomorphType(sp, type, allowInfer);
        }

        HIRConstGeneric monomorphConstgeneric(const Span& sp, const HIRConstGeneric& value, bool allowInfer) const override {
            if (const auto* infer = value.opt_Infer()) {
                for (size_t i = 0; i < inferenceParams.values.size(); i++) {
                    const auto* parameter = inferenceParams.values[i].opt_Infer();
                    if (!parameter || parameter->index != infer->index) {
                        continue;
                    }
                    const auto& resolved = table.getValue(value);
                    if (resolved == value) {
                        return stableExistentials.values[i].clone();
                    }
                    return this->monomorphConstgeneric(sp, resolved, allowInfer);
                }
            }
            return Monomorphiser::monomorphConstgeneric(sp, value, allowInfer);
        }
    };

    MaterializeCandidate materialize(crate.types, resolve_.ivars, inferenceParams, stableExistentials);
    outputParams = materialize.monomorphPathParams(span(), inferenceParams, true);
    headNormalizationAmbiguity = false;
    const auto isCanonicalTypeInput = [](const HIRType* type) {
        const auto* infer = type->opt_Infer();
        return infer && isSolverCanonicalInfer(infer->index);
    };
    const auto isCanonicalValueInput = [](const HIRConstGeneric& value) {
        const auto* infer = value.opt_Infer();
        return infer && isSolverCanonicalInfer(infer->index);
    };
    bool unresolved = false;
    for (const auto& equality : unifier.pending()) {
        const bool exportedDefiningOpaque = containsDefiningOpaque(equality.left) != containsDefiningOpaque(equality.right);
        const bool pendingUnresolved = !exportedDefiningOpaque && !isCanonicalTypeInput(equality.left) && !isCanonicalTypeInput(equality.right);
        unresolved |= pendingUnresolved;
        headNormalizationAmbiguity |= pendingUnresolved && (resolve_.hasAssociatedType(equality.left) || resolve_.hasAssociatedType(equality.right));
        headEqualities.push_back(
            SolverTypeEquality{
                materialize.monomorphType(span(), equality.left, true),
                materialize.monomorphType(span(), equality.right, true),
            }
        );
    }
    for (const auto& equality : unifier.pendingValues()) {
        const bool pendingUnresolved = !isCanonicalValueInput(equality.left) && !isCanonicalValueInput(equality.right);
        unresolved |= pendingUnresolved;
        headNormalizationAmbiguity |= pendingUnresolved;
        headValueEqualities.push_back(
            SolverValueEquality{
                materialize.monomorphConstgeneric(span(), equality.left, true),
                materialize.monomorphConstgeneric(span(), equality.right, true),
            }
        );
    }
    return unresolved ? Certainty::Ambiguous : Certainty::Proven;
}

auto NextTraitGoalEvaluator::assembleAliasBoundCandidates(size_t frameIndex, const HIRSimplePath& trait, const HIRPathParams& params, const HIRType* type) -> void {
    const auto* path = type->opt_Path();
    const auto* projection = path ? path->path.data.opt_UfcsKnown() : nullptr;
    if (!projection) {
        return;
    }

    HIRGenericPath declaringTrait;
    if (!resolve_.traitContainsType(span(), projection->trait, crate.getTraitByPath(span(), projection->trait.path), projection->item.c_str(), declaringTrait)) {
        BUG(span(), StringView("Cannot find associated type ") << projection->item << StringView(" anywhere in trait ") << projection->trait);
    }
    const auto& declaration = crate.getTraitByPath(span(), declaringTrait.path).types.at(projection->item);
    auto monomorph = MonomorphStatePtr(crate.types, projection->type, &declaringTrait.params, &projection->params);

    auto emit = [&](HIRTraitPath response) {
        auto match = response.path.params.compareWithPlaceholders(span(), params, resolve_.ivars.callbackResolveInfer());
        if (match != HIRCompare::Unequal) {
            auto impl = SolverImpl(type, std::move(response.path.params), std::move(response.typeBounds), response.constness);
            bool headNormalizationAmbiguity = false;
            ThinVector<SolverTypeEquality> headEqualities;
            ThinVector<SolverValueEquality> headValueEqualities;
            const auto relation = relateAssembledHead(CandidateSource::AliasBound, params, type, impl, headNormalizationAmbiguity, headEqualities, headValueEqualities);
            if (relation == Certainty::NoSolution) {
                return;
            }
            const bool headExact = assembledHeadIsExact(params, type, impl);
            pushCandidate(frameIndex, std::move(impl), headExact, relation, nullptr, {}, false, CandidateSource::AliasBound, headNormalizationAmbiguity, std::move(headEqualities), std::move(headValueEqualities));
        }
    };

    for (const auto& declaredBound : declaration.traitBounds) {
        auto bound = monomorph.monomorphTraitpath(span(), declaredBound, false);
        if (bound.path.path == trait) {
            emit(std::move(bound));
            continue;
        }

        const auto& boundDefinition = crate.getTraitByPath(span(), bound.path.path);
        resolve_.findNamedTraitInTrait(span(), trait, params, boundDefinition, bound.path.path, bound.path.params, type, [&](const HIRTraitPath& parent) {
            auto response = parent.clone();
            for (const auto& associated : bound.typeBounds) {
                if (associated.second.sourceTrait.path != trait || resolve_.comparePp(span(), associated.second.sourceTrait.params, response.path.params) == HIRCompare::Unequal) {
                    continue;
                }
                response.typeBounds.erase(associated.first);
                response.typeBounds.insert({associated.first, associated.second.clone()});
            }
            emit(std::move(response));
            return false;
        });
    }
}

auto NextTraitGoalEvaluator::assembleCandidates(size_t frameIndex, const HIRSimplePath& trait, const HIRPathParams& params, const HIRType* type, bool includeMagicCandidates, bool includeTraitImplCandidates) -> void {
    const auto* selfPath = resolve_.resolveType(type)->opt_Path();
    const bool selfIsRigidProjection = selfPath && selfPath->binding.is_Opaque();
    auto collect = [&](CandidateSource source) {
        return [&, source, selfIsRigidProjection](SolverImpl impl, Certainty assemblyCertainty) {
            auto effectiveSource = source;
            if (source == CandidateSource::Other && selfIsRigidProjection && !impl.isTraitImpl()) {
                effectiveSource = CandidateSource::ParamEnv;
            }
            bool headNormalizationAmbiguity = false;
            ThinVector<SolverTypeEquality> headEqualities;
            ThinVector<SolverValueEquality> headValueEqualities;
            auto relation = relateAssembledHead(source, params, type, impl, headNormalizationAmbiguity, headEqualities, headValueEqualities);
            if (relation == Certainty::NoSolution) {
                return false;
            }
            if (assemblyCertainty == Certainty::NoSolution) {
                return false;
            }
            if (assemblyCertainty == Certainty::Ambiguous) {
                relation = Certainty::Ambiguous;
            }
            const bool headExact = assemblyCertainty == Certainty::Proven && assembledHeadIsExact(params, type, impl);
            pushCandidate(frameIndex, std::move(impl), headExact, relation, nullptr, {}, false, effectiveSource, headNormalizationAmbiguity, std::move(headEqualities), std::move(headValueEqualities));
            return false;
        };
    };

    if (includeMagicCandidates && trait == resolve_.langUnsize()) {
        ASSERT_BUG(span(), params.types.size() == 1, StringView("Unsize trait requires a single type param"));
        if (canAssembleBuiltinUnsize(params.types[0], type)) {
            pushCandidate(frameIndex, SolverImpl(type, params.clone(), HIRTraitPath::assocListT()), true, Certainty::Proven, nullptr, {}, false, CandidateSource::Builtin);
        }
    } else if (includeMagicCandidates) {
        resolve_.assembleMagicCandidates(span(), trait, params, type, collect(CandidateSource::Builtin));
    }
    resolve_.assembleOtherCandidates(span(), trait, params, type, collect(CandidateSource::Other));
    resolve_.assembleParamEnvCandidates(span(), trait, params, type, collect(CandidateSource::ParamEnv));
    assembleAliasBoundCandidates(frameIndex, trait, params, type);

    if (includeTraitImplCandidates) {
        assembleTraitImplCandidates(frameIndex, trait, params, type, includeMagicCandidates);
    }
}

auto NextTraitGoalEvaluator::assembleTraitImplCandidates(size_t frameIndex, const HIRSimplePath& trait, const HIRPathParams& params, const HIRType* type, bool includeMagicCandidates) -> void {
    const auto& resolvedType = resolve_.resolveType(type);
    const auto& traitDef = crate.getTraitByPath(span(), trait);
    if (!traitDef.isMarker) {
        crate.findTraitImpls(trait, resolvedType, resolve_.ivars.callbackResolveInfer(), [&](const HIRTraitImpl& impl) {
            DEBUG(StringView("[find_trait_impls_crate] Found impl") << impl.params.fmtArgs() << StringView(" ") << trait << impl.traitArgs << StringView(" for ") << impl.type << StringView(" ") << impl.params.fmtBounds());
            if (impl.isReservation) {
                return false;
            }
            HIRPathParams implParams;
            bool headNormalizationAmbiguity = false;
            ThinVector<SolverTypeEquality> headEqualities;
            ThinVector<SolverValueEquality> headValueEqualities;
            const auto relation = this->unifyImplHead(impl.params, impl.traitArgs, impl.type, params, resolvedType, implParams, headNormalizationAmbiguity, headEqualities, headValueEqualities);
            if (relation != Certainty::NoSolution) {
                pushCandidate(frameIndex, SolverImpl(std::move(implParams), traitDef, trait, impl), relation == Certainty::Proven, relation, nullptr, {}, false, CandidateSource::TraitImpl, headNormalizationAmbiguity, std::move(headEqualities), std::move(headValueEqualities));
            }
            return false;
        });
    } else {
        crate.findAutoTraitImpls(trait, resolvedType, resolve_.ivars.callbackResolveInfer(), [&](const HIRMarkerImpl& impl) {
            HIRPathParams implParams;
            bool headNormalizationAmbiguity = false;
            ThinVector<SolverTypeEquality> headEqualities;
            ThinVector<SolverValueEquality> headValueEqualities;
            const auto relation = this->unifyImplHead(impl.params, impl.traitArgs, impl.type, params, resolvedType, implParams, headNormalizationAmbiguity, headEqualities, headValueEqualities);
            if (relation != Certainty::NoSolution) {
                auto monomorph = MonomorphStatePtr(crate.types, nullptr, &implParams, nullptr);
                auto responseType = monomorph.monomorphType(span(), impl.type, false);
                auto responseParams = monomorph.monomorphPathParams(span(), impl.traitArgs, false);
                pushCandidate(frameIndex, SolverImpl(std::move(responseType), std::move(responseParams), HIRTraitPath::assocListT()), relation == Certainty::Proven, relation, &impl, std::move(implParams), false, CandidateSource::TraitImpl, headNormalizationAmbiguity, std::move(headEqualities), std::move(headValueEqualities));
            }
            return false;
        });

        if (includeMagicCandidates) {
            const auto structuralRelation = resolve_.typeContainsIvars(resolvedType) || resolve_.paramsContainIvars(params) ? Certainty::Ambiguous : Certainty::Proven;
            pushCandidate(frameIndex, SolverImpl(resolvedType, params.clone(), HIRTraitPath::assocListT()), structuralRelation == Certainty::Proven, structuralRelation, nullptr, {}, true, CandidateSource::Builtin);
        }
    }
}

auto NextTraitGoalEvaluator::makeAssociatedProjection(const HIRType* type, const HIRGenericPath& sourceTrait, const RcString& name, const HIRPathParams& associatedParams) const -> const HIRType* {
    return crate.types.path(HIRPath(type, sourceTrait.clone(), name, associatedParams.clone()), HIRTypePathBinding::make_Opaque({}));
}

auto NextTraitGoalEvaluator::makeAssociatedProjection(const SolverImpl& impl, const HIRGenericPath& sourceTrait, const RcString& name, const HIRPathParams& associatedParams) const -> const HIRType* {
    return makeAssociatedProjection(impl.getImplType(crate.types), sourceTrait, name, associatedParams);
}

template <typename Relate>
auto NextTraitGoalEvaluator::unifyCandidateParams(HIRPathParams& params, Relate relate) -> CandidateBindingResult {
    const auto original = params.clone();
    const auto snapshot = resolve_.ivars.snapshot();
    STD_DEFER {
        resolve_.ivars.rollbackTo(snapshot);
    };

    Vector<CandidateTypeBinding> typeBindings;
    Vector<CandidateValueBinding> valueBindings;

    const auto addTypeBinding = [&](const HIRType* type) {
        const auto* generic = type->opt_Generic();
        const auto* infer = type->opt_Infer();
        if ((!generic || !generic->isPlaceholder()) && (!infer || infer->isLit())) {
            return;
        }
        for (const auto& binding : typeBindings) {
            if (binding.stable == type) {
                return;
            }
        }
        typeBindings.pushBack(CandidateTypeBinding{type, resolve_.ivars.newIvarTr(infer ? infer->tyClass : HIRInferClass::None)});
    };
    for (const auto& type : params.types) {
        visitTyWith(type, [&](const HIRType* inner) {
            addTypeBinding(inner);
            return false;
        });
    }

    const auto addValueBinding = [&](const HIRConstGeneric& value) {
        const auto* generic = value.opt_Generic();
        const auto* infer = value.opt_Infer();
        if ((!generic || !generic->isPlaceholder()) && !infer) {
            return;
        }
        const bool isGeneric = generic != nullptr;
        const auto name = isGeneric ? generic->name : RcString();
        const auto stableIndex = isGeneric ? generic->binding : infer->index;
        for (const auto& binding : valueBindings) {
            if (binding.isGeneric == isGeneric && binding.name == name && binding.stableIndex == stableIndex) {
                return;
            }
        }
        valueBindings.pushBack(CandidateValueBinding{name, stableIndex, resolve_.ivars.newIvarVal(), isGeneric});
    };
    for (const auto& value : params.values) {
        addValueBinding(value);
    }

    struct InstantiateCandidate final: public MonomorphiserNop {
        const Vector<CandidateTypeBinding>& typeBindings_;
        const Vector<CandidateValueBinding>& valueBindings_;

        InstantiateCandidate(HIRTypeInterner& types, const Vector<CandidateTypeBinding>& typeBindings, const Vector<CandidateValueBinding>& valueBindings)
            : MonomorphiserNop(types)
            , typeBindings_(typeBindings)
            , valueBindings_(valueBindings)
        {
        }

        const HIRType* monomorphType(const Span& sp, const HIRType* type, bool allowInfer = true) const override {
            for (const auto& binding : typeBindings_) {
                if (binding.stable == type) {
                    return binding.probe;
                }
            }
            return MonomorphiserNop::monomorphType(sp, type, allowInfer);
        }

        const HIRType* getType(const Span& sp, const HIRGenericRef& generic) const override {
            for (const auto& binding : typeBindings_) {
                const auto* stable = binding.stable->opt_Generic();
                if (stable && *stable == generic) {
                    return binding.probe;
                }
            }
            return MonomorphiserNop::getType(sp, generic);
        }

        HIRConstGeneric monomorphConstgeneric(const Span& sp, const HIRConstGeneric& value, bool allowInfer) const override {
            const auto* generic = value.opt_Generic();
            const auto* infer = value.opt_Infer();
            for (const auto& binding : valueBindings_) {
                const bool matches = binding.isGeneric ? generic && generic->name == binding.name && generic->binding == binding.stableIndex : infer && infer->index == binding.stableIndex;
                if (matches) {
                    return HIRConstGeneric::make_Infer({binding.probeIndex});
                }
            }
            return MonomorphiserNop::monomorphConstgeneric(sp, value, allowInfer);
        }

        HIRConstGeneric getValue(const Span& sp, const HIRGenericRef& generic) const override {
            for (const auto& binding : valueBindings_) {
                if (binding.isGeneric && binding.name == generic.name && binding.stableIndex == generic.binding) {
                    return HIRConstGeneric::make_Infer({binding.probeIndex});
                }
            }
            return MonomorphiserNop::getValue(sp, generic);
        }
    };

    InstantiateCandidate instantiate(crate.types, typeBindings, valueBindings);
    const auto probeParams = instantiate.monomorphPathParams(span(), params, true);
    Unifier unifier(span(), resolve_.ivars, &resolve_, {.bindRigidValues = true});

    struct Relations {
        const Span& span_;
        InstantiateCandidate& instantiate_;
        Unifier& unifier_;

        bool failed = false;

        Relations(const Span& span, InstantiateCandidate& instantiate, Unifier& unifier)
            : span_(span)
            , instantiate_(instantiate)
            , unifier_(unifier)
        {
        }

        void mismatch() {
            failed = true;
        }

        void type(const HIRType* candidate, const HIRType* value) {
            if (failed) {
                return;
            }
            const auto pattern = instantiate_.monomorphType(span_, candidate, true);
            failed = unifier_.unify(value, pattern) == Unifier::Outcome::Mismatch;
        }

        void value(const HIRConstGeneric& candidate, const HIRConstGeneric& value) {
            if (failed) {
                return;
            }
            const auto pattern = instantiate_.monomorphConstgeneric(span_, candidate, true);
            failed = unifier_.unifyValues(value, pattern) == Unifier::Outcome::Mismatch;
        }

        void pathParams(const HIRPathParams& candidate, const HIRPathParams& value) {
            if (candidate.types.size() != value.types.size() || candidate.values.size() != value.values.size()) {
                failed = true;
                return;
            }
            for (size_t i = 0; i < candidate.types.size(); i++) {
                this->type(candidate.types[i], value.types[i]);
            }
            for (size_t i = 0; i < candidate.values.size(); i++) {
                this->value(candidate.values[i], value.values[i]);
            }
        }
    };

    Relations relations(span(), instantiate, unifier);
    relate(relations);
    if (relations.failed) {
        return CandidateBindingResult::Mismatch;
    }

    struct MaterializeCandidate final: public MonomorphiserNop {
        const HMTypeInferrence& table_;
        const Vector<CandidateTypeBinding>& typeBindings_;
        const Vector<CandidateValueBinding>& valueBindings_;

        MaterializeCandidate(HIRTypeInterner& types, const HMTypeInferrence& table, const Vector<CandidateTypeBinding>& typeBindings, const Vector<CandidateValueBinding>& valueBindings)
            : MonomorphiserNop(types)
            , table_(table)
            , typeBindings_(typeBindings)
            , valueBindings_(valueBindings)
        {
        }

        const HIRType* monomorphType(const Span& sp, const HIRType* type, bool allowInfer = true) const override {
            for (const auto& binding : typeBindings_) {
                if (binding.probe != type) {
                    continue;
                }
                const auto* resolved = table_.getType(type);
                return resolved == type ? binding.stable : this->monomorphType(sp, resolved, allowInfer);
            }
            return MonomorphiserNop::monomorphType(sp, type, allowInfer);
        }

        HIRConstGeneric monomorphConstgeneric(const Span& sp, const HIRConstGeneric& value, bool allowInfer) const override {
            if (const auto* infer = value.opt_Infer()) {
                for (const auto& binding : valueBindings_) {
                    if (binding.probeIndex != infer->index) {
                        continue;
                    }
                    const auto& resolved = table_.getValue(value);
                    if (resolved == value) {
                        return binding.isGeneric ? HIRConstGeneric(HIRGenericRef(binding.name, binding.stableIndex)) : HIRConstGeneric::make_Infer({binding.stableIndex});
                    }
                    return this->monomorphConstgeneric(sp, resolved, allowInfer);
                }
            }
            return MonomorphiserNop::monomorphConstgeneric(sp, value, allowInfer);
        }
    };

    MaterializeCandidate materialize(crate.types, resolve_.ivars, typeBindings, valueBindings);
    auto output = materialize.monomorphPathParams(span(), probeParams, true);
    const bool changed = output != original;
    if (changed) {
        params = std::move(output);
    }
    return changed ? CandidateBindingResult::Changed : CandidateBindingResult::Unchanged;
}

auto NextTraitGoalEvaluator::bindCandidatePlaceholders(Candidate& candidate, const HIRType* nestedType, const HIRTraitPath::assocListT& associated, bool useCandidateResponse, bool applyResponseBindings) -> CandidateBindingResult {
    HIRPathParams* candidateParams = nullptr;
    if (candidate.impl.traitImpl) {
        candidateParams = &candidate.impl.implParams;
    } else if (candidate.markerImpl) {
        candidateParams = &candidate.markerImplParams;
    }
    if (!candidateParams || associated.empty()) {
        return CandidateBindingResult::Unchanged;
    }

    bool changed = false;
    for (const auto& requirement : associated) {
        auto candidateOutput = useCandidateResponse ? candidate.impl.getType(crate.types, requirement.first.c_str(), requirement.second.atyParams) : nullptr;
        auto responseBinding = CandidateBindingResult::Unchanged;
        if (!useCandidateResponse) {
            const bool constrainOutput = applyResponseBindings && !typeHasUnknown(requirement.second.type);
            auto nestedCallback = makeCallable<SolverResponseCb>([&](SolverResponse response) {
                if (response.certainty != Certainty::Proven || !response.impl) {
                    return false;
                }
                auto output = response.impl->getType(crate.types, requirement.first.c_str(), requirement.second.atyParams);
                if (output == nullptr) {
                    return false;
                }
                if (constrainOutput) {
                    HIRTraitPath::assocListT responseAssociated;
                    responseAssociated.insert({requirement.first, requirement.second.clone()});
                    responseBinding = bindCandidateResponse(
                        candidate,
                        nestedType,
                        requirement.second.sourceTrait.params,
                        responseAssociated,
                        *response.impl
                    );
                    if (responseBinding == CandidateBindingResult::Mismatch) {
                        return false;
                    }
                    appendRelationEffects(candidate, std::move(response));
                }
                candidateOutput = std::move(output);
                return true;
            });
            evaluateTyped(
                span(),
                requirement.second.sourceTrait.path,
                requirement.second.sourceTrait.params,
                nestedType,
                nestedCallback,
                {
                    .assocName = requirement.first.c_str(),
                    .assocType = constrainOutput ? requirement.second.type : nullptr,
                    .assocParams = &requirement.second.atyParams,
                }
            );
            if (responseBinding == CandidateBindingResult::Mismatch) {
                return responseBinding;
            }
            changed |= responseBinding == CandidateBindingResult::Changed;
        }
        if (candidateOutput == nullptr) {
            candidateOutput = makeAssociatedProjection(nestedType, requirement.second.sourceTrait, requirement.first, requirement.second.atyParams);
        }
        if (!useCandidateResponse && !typeHasUfcsUnknown(candidateOutput)) {
            candidateOutput = resolve_.expandAssociatedTypes(span(), std::move(candidateOutput));
        }
        const bool callableOutput = requirement.first == "Output" && (requirement.second.sourceTrait.path == resolve_.langFn() || requirement.second.sourceTrait.path == resolve_.langFnMut() || requirement.second.sourceTrait.path == resolve_.langFnOnce());
        const auto* candidateOutputInfer = candidateOutput->opt_Infer();
        const auto* resolvedCandidateOutput = candidateOutputInfer && candidateOutputInfer->index == ~0u ? candidateOutput : resolve_.ivars.getType(candidateOutput);
        if (callableOutput && resolvedCandidateOutput->is_Diverge()) {
            continue;
        }
        const auto* candidatePattern = useCandidateResponse ? candidateOutput : requirement.second.type;
        const auto* responseValue = useCandidateResponse ? requirement.second.type : candidateOutput;
        const auto binding = this->unifyCandidateParams(*candidateParams, [&](auto& relations) {
            relations.type(candidatePattern, responseValue);
        });
        if (binding == CandidateBindingResult::Mismatch) {
            return binding;
        }
        changed |= binding == CandidateBindingResult::Changed;
    }

    if (changed && candidate.markerImpl) {
        auto monomorph = MonomorphStatePtr(crate.types, nullptr, &candidate.markerImplParams, nullptr);
        candidate.impl.type = monomorph.monomorphType(span(), candidate.markerImpl->type, false);
        candidate.impl.traitArgs = monomorph.monomorphPathParams(span(), candidate.markerImpl->traitArgs, false);
    }
    return changed ? CandidateBindingResult::Changed : CandidateBindingResult::Unchanged;
}

auto NextTraitGoalEvaluator::bindCandidateResponse(Candidate& candidate, const HIRType* nestedType, const HIRPathParams& nestedParams, const HIRTraitPath::assocListT& nestedAssociated, const SolverImpl& response) -> CandidateBindingResult {
    HIRPathParams* candidateParams = nullptr;
    if (candidate.impl.traitImpl) {
        candidateParams = &candidate.impl.implParams;
    } else if (candidate.markerImpl) {
        candidateParams = &candidate.markerImplParams;
    }
    if (!candidateParams) {
        return CandidateBindingResult::Unchanged;
    }

    const auto binding = this->unifyCandidateParams(*candidateParams, [&](auto& relations) {
        relations.type(nestedType, response.getImplType(crate.types));
        relations.pathParams(nestedParams, response.getTraitParamsRef(crate.types));
        for (const auto& requirement : nestedAssociated) {
            auto output = response.getType(crate.types, requirement.first.c_str(), requirement.second.atyParams);
            if (output == nullptr) {
                relations.mismatch();
                break;
            }
            const bool callableOutput = requirement.first == "Output" && (requirement.second.sourceTrait.path == resolve_.langFn() || requirement.second.sourceTrait.path == resolve_.langFnMut() || requirement.second.sourceTrait.path == resolve_.langFnOnce());
            const auto* outputInfer = output->opt_Infer();
            const auto* resolvedOutput = outputInfer && outputInfer->index == ~0u ? output : resolve_.ivars.getType(output);
            if (callableOutput && resolvedOutput->is_Diverge()) {
                continue;
            }
            relations.type(requirement.second.type, output);
        }
    });

    if (binding == CandidateBindingResult::Changed && candidate.markerImpl) {
        auto monomorph = MonomorphStatePtr(crate.types, nullptr, &candidate.markerImplParams, nullptr);
        candidate.impl.type = monomorph.monomorphType(span(), candidate.markerImpl->type, false);
        candidate.impl.traitArgs = monomorph.monomorphPathParams(span(), candidate.markerImpl->traitArgs, false);
    }
    return binding;
}

auto NextTraitGoalEvaluator::unifyProbe(const HIRType* left, const HIRType* right) -> Certainty {
    if (left == right) {
        return Certainty::Proven;
    }

    const auto snapshot = resolve_.ivars.snapshot();
    Unifier unifier(span(), resolve_.ivars, &resolve_);
    const auto outcome = unifier.unify(left, right);
    const bool boundInference = resolve_.ivars.mutationGeneration != snapshot.generation;
    resolve_.ivars.rollbackTo(snapshot);
    if (outcome == Unifier::Outcome::Mismatch) {
        return Certainty::NoSolution;
    }
    if (boundInference || outcome == Unifier::Outcome::Ambiguous) {
        return Certainty::Ambiguous;
    }
    return Certainty::Proven;
}

auto NextTraitGoalEvaluator::unifyValueProbe(const HIRConstGeneric& left, const HIRConstGeneric& right) -> Certainty {
    if (left == right) {
        return Certainty::Proven;
    }
    const auto snapshot = resolve_.ivars.snapshot();
    Unifier unifier(span(), resolve_.ivars, &resolve_);
    const auto outcome = unifier.unifyValues(left, right);
    const bool boundInference = resolve_.ivars.mutationGeneration != snapshot.generation;
    resolve_.ivars.rollbackTo(snapshot);
    if (outcome == Unifier::Outcome::Mismatch) {
        return Certainty::NoSolution;
    }
    if (boundInference || outcome == Unifier::Outcome::Ambiguous) {
        return Certainty::Ambiguous;
    }
    return Certainty::Proven;
}

auto NextTraitGoalEvaluator::appendRelationEffects(Candidate& candidate, SolverResponse response) -> void {
    for (size_t i = 0; i < response.slots.typeInputs.size(); i++) {
        if (response.slots.typeInputs[i] != response.slots.types[i]) {
            candidate.relationEqualities.push_back(
                SolverTypeEquality{
                    response.slots.typeInputs[i],
                    response.slots.types[i],
                }
            );
        }
    }
    for (size_t i = 0; i < response.slots.valueInputs.size(); i++) {
        if (response.slots.valueInputs[i] != response.slots.values[i]) {
            candidate.relationValueEqualities.push_back(
                SolverValueEquality{
                    response.slots.valueInputs[i].clone(),
                    response.slots.values[i].clone(),
                }
            );
        }
    }
    for (auto& equality : response.equalities) {
        candidate.relationEqualities.push_back(std::move(equality));
    }
    for (auto& equality : response.valueEqualities) {
        candidate.relationValueEqualities.push_back(std::move(equality));
    }
    for (auto& obligation : response.obligations) {
        candidate.relationObligations.push_back(std::move(obligation));
    }
}

auto NextTraitGoalEvaluator::solveRelationGoal(Candidate& candidate, const HIRSimplePath& trait, const HIRPathParams& params, const HIRType* type, const HIRTraitPath::assocListT* associated) -> Certainty {
    Certainty certainty = Certainty::NoSolution;
    auto callback = makeCallable<SolverResponseCb>([&](SolverResponse response) {
        if (!response.impl && response.certainty != Certainty::Ambiguous) {
            return false;
        }
        certainty = response.certainty;
        appendRelationEffects(candidate, std::move(response));
        return true;
    });
    return evaluateTyped(span(), trait, params, type, callback, {.associated = associated, .ambiguity = SolverAmbiguityPolicy::Report}) ? certainty : Certainty::NoSolution;
}

auto NextTraitGoalEvaluator::relateTypes(Candidate& candidate, const HIRType* left, const HIRType* right) -> Certainty {
    if (left == right) {
        return Certainty::Proven;
    }

    const auto snapshot = resolve_.ivars.snapshot();
    STD_DEFER {
        resolve_.ivars.rollbackTo(snapshot);
    };
    Unifier unifier(span(), resolve_.ivars, &resolve_);
    const auto outcome = unifier.unify(left, right);
    ThinVector<SolverTypeEquality> pending;
    ThinVector<SolverValueEquality> pendingValues;
    if (outcome != Unifier::Outcome::Mismatch) {
        for (const auto& equality : unifier.pending()) {
            pending.push_back(SolverTypeEquality{equality.left, equality.right});
        }
        for (const auto& equality : unifier.pendingValues()) {
            pendingValues.push_back(SolverValueEquality{equality.left.clone(), equality.right.clone()});
        }
    }
    if (outcome == Unifier::Outcome::Mismatch) {
        return Certainty::NoSolution;
    }

    candidate.relationEqualities.push_back(SolverTypeEquality{left, right});
    if (outcome == Unifier::Outcome::Proven) {
        return Certainty::Proven;
    }

    ThinVector<SolverTypeEquality> localBindings;
    struct MaterializeRelationBindings final: public MonomorphiserNop {
        const HMTypeInferrence& table;
        const ThinVector<SolverTypeEquality>& bindings;

        MaterializeRelationBindings(HIRTypeInterner& types, const HMTypeInferrence& table, const ThinVector<SolverTypeEquality>& bindings)
            : MonomorphiserNop(types)
            , table(table)
            , bindings(bindings)
        {
        }

        const HIRType* monomorphType(const Span& sp, const HIRType* type, bool allowInfer = true) const override {
            const auto* resolved = table.getType(type);
            if (resolved != type) {
                return this->monomorphType(sp, resolved, allowInfer);
            }
            for (const auto& binding : bindings) {
                if (binding.left == type) {
                    return this->monomorphType(sp, binding.right, allowInfer);
                }
            }
            return MonomorphiserNop::monomorphType(sp, type, allowInfer);
        }
    } materializeBindings(crate.types, resolve_.ivars, localBindings);

    Certainty result = pendingValues.empty() ? Certainty::Proven : Certainty::Ambiguous;
    for (const auto& equality : pending) {
        const auto isSolverExistential = [](const HIRType* type) {
            const auto* infer = type->opt_Infer();
            return infer && isSolverCanonicalInfer(infer->index);
        };
        auto leftStorage = materializeBindings.monomorphType(span(), equality.left, true);
        auto rightStorage = materializeBindings.monomorphType(span(), equality.right, true);
        const auto* left = &*leftStorage;
        const auto* right = &*rightStorage;
        const auto bindExistential = [&](const HIRType* existential, const HIRType* value) {
            if (!isSolverExistential(existential) || visitTyWith(value, [&](const HIRType* inner) { return inner == existential; })) {
                return false;
            }
            localBindings.push_back(SolverTypeEquality{existential, value});
            return true;
        };
        if (bindExistential(left, right) || bindExistential(right, left)) {
            continue;
        }
        if (isSolverExistential(left) || isSolverExistential(right) || containsDefiningOpaque(left) || containsDefiningOpaque(right)) {
            continue;
        }

        struct ProjectionRelation {
            bool isProjection;
            Certainty certainty;
        };

        auto relateProjection = [&](const HIRType* alias, const HIRType* other) -> ProjectionRelation {
            const auto* path = alias->opt_Path();
            const auto* projection = path ? path->path.data.opt_UfcsKnown() : nullptr;
            if (!projection) {
                return {false, Certainty::Ambiguous};
            }

            bool sawResponse = false;
            bool sawOutput = false;
            Certainty nestedResult = Certainty::Ambiguous;
            auto callback = makeCallable<NormalizesToCb>([&](NormalizesToResponse response) {
                sawResponse = true;
                const auto nestedCertainty = response.effects.certainty;
                appendRelationEffects(candidate, std::move(response.effects));
                if (response.output != nullptr) {
                    sawOutput = true;
                    nestedResult = this->relateTypes(candidate, response.output, other);
                    if (nestedResult == Certainty::Proven && nestedCertainty == Certainty::Ambiguous) {
                        nestedResult = Certainty::Ambiguous;
                    }
                }
                return true;
            });
            evaluateNormalizesTo(span(), NormalizesTo{alias}, callback, false);
            if (!sawResponse) {
                nestedResult = Certainty::NoSolution;
            } else if (!sawOutput) {
                nestedResult = Certainty::Ambiguous;
            }
            return {true, nestedResult};
        };

        auto nested = relateProjection(left, right);
        if (!nested.isProjection) {
            nested = relateProjection(right, left);
        }
        if (!nested.isProjection) {
            result = Certainty::Ambiguous;
            continue;
        }
        if (nested.certainty == Certainty::NoSolution) {
            return Certainty::NoSolution;
        }
        if (nested.certainty == Certainty::Ambiguous) {
            result = Certainty::Ambiguous;
        }
    }
    return result;
}

auto NextTraitGoalEvaluator::relateValues(Candidate& candidate, const HIRConstGeneric& left, const HIRConstGeneric& right) -> Certainty {
    if (left == right) {
        return Certainty::Proven;
    }

    const auto snapshot = resolve_.ivars.snapshot();
    Unifier unifier(span(), resolve_.ivars, &resolve_);
    const auto outcome = unifier.unifyValues(left, right);
    resolve_.ivars.rollbackTo(snapshot);
    if (outcome == Unifier::Outcome::Mismatch) {
        return Certainty::NoSolution;
    }

    candidate.relationValueEqualities.push_back(SolverValueEquality{left.clone(), right.clone()});
    if (outcome == Unifier::Outcome::Proven) {
        return Certainty::Proven;
    }

    const auto* leftInfer = left.opt_Infer();
    const auto* rightInfer = right.opt_Infer();
    if ((leftInfer && isSolverCanonicalInfer(leftInfer->index)) || (rightInfer && isSolverCanonicalInfer(rightInfer->index))) {
        return Certainty::Proven;
    }
    return Certainty::Ambiguous;
}

auto NextTraitGoalEvaluator::evaluateUnsizeRelation(Candidate& candidate, const HIRType* rawDestination, const HIRType* rawSource) -> Certainty {
    const auto structural = evaluateBuiltinUnsize(candidate, rawDestination, rawSource);
    if (structural == Certainty::Proven) {
        return structural;
    }

    HIRPathParams params(rawDestination);
    const auto declared = solveRelationGoal(candidate, resolve_.langUnsize(), params, rawSource, nullptr);
    if (declared == Certainty::Proven) {
        return declared;
    }
    if (structural == Certainty::Ambiguous || declared == Certainty::Ambiguous) {
        candidate.relationObligations.push_back(SolverObligation{rawSource, HIRTraitPath(HIRGenericPath(resolve_.langUnsize(), std::move(params)))});
        return Certainty::Ambiguous;
    }
    return Certainty::NoSolution;
}

bool NextTraitGoalEvaluator::canAssembleBuiltinUnsize(const HIRType* rawDestination, const HIRType* rawSource) const {
    const auto* destination = resolve_.resolveType(rawDestination);
    const auto* source = resolve_.resolveType(rawSource);

    if (destination->is_TraitObject()) {
        return true;
    }
    if (destination->is_Slice() && source->is_Array()) {
        return true;
    }

    const auto* destinationPath = destination->opt_Path();
    const auto* sourcePath = source->opt_Path();
    const auto* destinationStruct = destinationPath && destinationPath->binding.is_Struct() ? destinationPath->binding.as_Struct() : nullptr;
    const auto* sourceStruct = sourcePath && sourcePath->binding.is_Struct() ? sourcePath->binding.as_Struct() : nullptr;
    if (destinationStruct && destinationStruct == sourceStruct && destinationStruct->structMarkings.canUnsize) {
        return true;
    }

    return false;
}

auto NextTraitGoalEvaluator::evaluateBuiltinUnsize(Candidate& candidate, const HIRType* rawDestination, const HIRType* rawSource) -> Certainty {
    auto destinationStorage = normalizeGoalInput(rawDestination);
    auto sourceStorage = normalizeGoalInput(rawSource);
    const auto* destination = resolve_.resolveType(destinationStorage);
    const auto* source = resolve_.resolveType(sourceStorage);

    if (resolve_.ivars.typesEqual(destination, source)) {
        return Certainty::NoSolution;
    }

    if (const auto* destinationSlice = destination->opt_Slice()) {
        if (const auto* sourceArray = source->opt_Array()) {
            return relateTypes(candidate, destinationSlice->inner, sourceArray->inner);
        }
    }

    if (const auto* object = destination->opt_TraitObject()) {
        Certainty result = Certainty::Proven;
        const auto require = [&](const HIRTraitPath& requirement) {
            const auto nested = solveRelationGoal(candidate, requirement.path.path, requirement.path.params, source, &requirement.typeBounds);
            if (nested == Certainty::Ambiguous) {
                candidate.relationObligations.push_back(SolverObligation{source, requirement.clone()});
            }
            return nested;
        };
        const auto combine = [&](Certainty nested) {
            if (nested == Certainty::NoSolution) {
                result = Certainty::NoSolution;
            } else if (nested == Certainty::Ambiguous && result == Certainty::Proven) {
                result = Certainty::Ambiguous;
            }
        };

        if (object->trait.path.path != HIRSimplePath()) {
            combine(require(object->trait));
        }
        for (const auto& marker : object->markers) {
            if (result == Certainty::NoSolution) {
                break;
            }
            combine(require(HIRTraitPath(marker.clone())));
        }
        return result;
    }

    const auto* destinationPath = destination->opt_Path();
    const auto* sourcePath = source->opt_Path();
    const auto* destinationGeneric = destinationPath ? destinationPath->path.data.opt_Generic() : nullptr;
    const auto* sourceGeneric = sourcePath ? sourcePath->path.data.opt_Generic() : nullptr;
    const auto* destinationStruct = destinationPath && destinationPath->binding.is_Struct() ? destinationPath->binding.as_Struct() : nullptr;
    const auto* sourceStruct = sourcePath && sourcePath->binding.is_Struct() ? sourcePath->binding.as_Struct() : nullptr;
    if (destinationStruct && destinationStruct == sourceStruct && destinationGeneric && sourceGeneric && destinationStruct->structMarkings.canUnsize) {
        if (destinationGeneric->path != sourceGeneric->path || destinationGeneric->params.types.size() != sourceGeneric->params.types.size() || destinationGeneric->params.values.size() != sourceGeneric->params.values.size()) {
            return Certainty::NoSolution;
        }

        const auto combine = [](Certainty& result, Certainty nested) {
            if (nested == Certainty::NoSolution) {
                result = Certainty::NoSolution;
            } else if (nested == Certainty::Ambiguous && result == Certainty::Proven) {
                result = Certainty::Ambiguous;
            }
        };
        Certainty result = Certainty::Proven;
        const auto& markings = destinationStruct->structMarkings;
        if (markings.dstType == HIRStructMarkings::DstType::Possible) {
            ASSERT_BUG(span(), markings.unsizedParam < destinationGeneric->params.types.size(), StringView("Malformed unsized struct markings"));
            for (size_t i = 0; i < destinationGeneric->params.types.size(); i++) {
                if (i != markings.unsizedParam) {
                    combine(result, relateTypes(candidate, destinationGeneric->params.types[i], sourceGeneric->params.types[i]));
                }
            }
            for (size_t i = 0; i < destinationGeneric->params.values.size(); i++) {
                combine(result, relateValues(candidate, destinationGeneric->params.values[i], sourceGeneric->params.values[i]));
            }
            if (result == Certainty::NoSolution) {
                return result;
            }
            combine(result, evaluateUnsizeRelation(candidate, destinationGeneric->params.types[markings.unsizedParam], sourceGeneric->params.types[markings.unsizedParam]));
            return result;
        }

        ASSERT_BUG(span(), markings.dstType == HIRStructMarkings::DstType::Projection, StringView("Unexpected unsized struct marking"));
        const auto monomorphField = [&](const HIRType* self, const HIRPathParams& params, const HIRType* field) {
            return normalizeGoalInput(MonomorphStatePtr(crate.types, self, &params, nullptr).monomorphType(span(), field));
        };
        const HIRType* tail = nullptr;
        switch (destinationStruct->data.tag()) {
            case HIRStructData::TAG_Unit:
                BUG(span(), StringView("Potentially-unsized unit struct ") << destination);
            case HIRStructData::TAG_Tuple: {
                const auto& fields = destinationStruct->data.as_Tuple();
                ASSERT_BUG(span(), markings.unsizedField < fields.size(), StringView("Malformed unsized struct field marking"));
                tail = fields[markings.unsizedField].ent;
                for (size_t i = 0; i < fields.size(); i++) {
                    if (i != markings.unsizedField) {
                        combine(result, relateTypes(candidate, monomorphField(destination, destinationGeneric->params, fields[i].ent), monomorphField(source, sourceGeneric->params, fields[i].ent)));
                    }
                }
                break;
            }
            case HIRStructData::TAG_Named: {
                const auto& fields = destinationStruct->data.as_Named();
                ASSERT_BUG(span(), markings.unsizedField < fields.size(), StringView("Malformed unsized struct field marking"));
                tail = fields[markings.unsizedField].ty;
                for (size_t i = 0; i < fields.size(); i++) {
                    if (i != markings.unsizedField) {
                        combine(result, relateTypes(candidate, monomorphField(destination, destinationGeneric->params, fields[i].ty), monomorphField(source, sourceGeneric->params, fields[i].ty)));
                    }
                }
                break;
            }
        }
        if (result == Certainty::NoSolution) {
            return result;
        }
        combine(result, evaluateUnsizeRelation(candidate, monomorphField(destination, destinationGeneric->params, tail), monomorphField(source, sourceGeneric->params, tail)));
        return result;
    }

    const bool destinationIsOpen = destination->is_Infer() || destination->is_Generic() || (destinationPath && (destinationPath->binding.is_Unbound() || destinationPath->binding.is_Opaque()));
    const bool sourceIsOpen = source->is_Infer() || source->is_Generic() || (sourcePath && (sourcePath->binding.is_Unbound() || sourcePath->binding.is_Opaque()));
    if (destinationIsOpen) {
        if (sourceIsOpen || source->is_Array() || source->is_TraitObject() || (sourceStruct && sourceStruct->structMarkings.canUnsize)) {
            return Certainty::Ambiguous;
        }
        return Certainty::NoSolution;
    }
    if (sourceIsOpen) {
        return Certainty::Ambiguous;
    }
    return Certainty::NoSolution;
}

auto NextTraitGoalEvaluator::evaluateHeadEquality(Candidate& candidate, const SolverTypeEquality& equality) -> Certainty {
    const auto normalizedLeft = normalizeGoalInput(equality.left);
    const auto normalizedRight = normalizeGoalInput(equality.right);
    const auto relation = this->relateTypes(candidate, normalizedLeft, normalizedRight);
    if (relation == Certainty::Proven) {
        return relation;
    }
    if (containsDefiningOpaque(equality.left) || containsDefiningOpaque(equality.right)) {
        return Certainty::Ambiguous;
    }

    bool sawAlias = false;
    bool failed = false;
    auto checkAliasBounds = [&](const HIRType* alias, const HIRType* replacement) {
        const auto* path = alias->opt_Path();
        const auto* projection = path ? path->path.data.opt_UfcsKnown() : nullptr;
        if (!projection) {
            return;
        }
        sawAlias = true;

        HIRGenericPath declaringTrait;
        if (!resolve_.traitContainsType(span(), projection->trait, crate.getTraitByPath(span(), projection->trait.path), projection->item.c_str(), declaringTrait)) {
            BUG(span(), StringView("Cannot find associated type ") << projection->item << StringView(" anywhere in trait ") << projection->trait);
        }
        auto monomorph = MonomorphStatePtr(crate.types, projection->type, &declaringTrait.params, &projection->params);
        resolve_.iterateAtyBounds(span(), *projection, [&](const HIRTraitPath& declaredBound) {
            auto bound = monomorph.monomorphTraitpath(span(), declaredBound, true);
            const auto* associated = bound.typeBounds.empty() ? nullptr : &bound.typeBounds;
            const auto result = solveGoal(bound.path.path, bound.path.params, replacement, associated);
            if (result == Certainty::NoSolution) {
                failed = true;
                return true;
            }
            if (result == Certainty::Ambiguous) {
                candidate.headObligations.push_back(SolverObligation{replacement, std::move(bound)});
            }
            return false;
        });
    };

    checkAliasBounds(equality.left, normalizedRight);
    if (!failed) {
        checkAliasBounds(equality.right, normalizedLeft);
    }
    if (failed || (relation == Certainty::NoSolution && !sawAlias)) {
        return Certainty::NoSolution;
    }
    return Certainty::Ambiguous;
}

auto NextTraitGoalEvaluator::matchAssociatedTypes(const HIRSimplePath& trait, Candidate& candidate, const HIRTraitPath::assocListT* associated, const Monomorphiser* headBindings) -> Certainty {
    if (!associated || associated->empty()) {
        return Certainty::Proven;
    }

    const auto& impl = candidate.impl;
    Certainty result = Certainty::Proven;
    for (const auto& requirement : *associated) {
        const auto& aty = requirement.second;
        if (!impl.isTraitImpl() && aty.atyParams.hasParams()) {
            result = Certainty::Ambiguous;
            continue;
        }
        auto output = impl.getType(crate.types, requirement.first.c_str(), aty.atyParams);
        if (output == nullptr) {
            if (aty.sourceTrait.path != trait) {
                HIRTraitPath::assocListT sourceAssociated;
                sourceAssociated.insert({requirement.first, requirement.second.clone()});
                const auto sourceResult = solveGoal(aty.sourceTrait.path, aty.sourceTrait.params, impl.getImplType(crate.types), &sourceAssociated);
                DEBUG(StringView("declaring-trait redirect ") << aty.sourceTrait << StringView(" => ") << static_cast<unsigned>(sourceResult));
                if (sourceResult == Certainty::NoSolution) {
                    return Certainty::NoSolution;
                }
                if (sourceResult == Certainty::Ambiguous) {
                    result = Certainty::Ambiguous;
                }
                continue;
            }
            if (impl.isTraitImpl()) {
                result = Certainty::Ambiguous;
                continue;
            }
            output = makeAssociatedProjection(impl, aty.sourceTrait, requirement.first, aty.atyParams);
        }
        auto required = aty.type;
        if (headBindings) {
            output = headBindings->monomorphType(span(), output, true);
            required = headBindings->monomorphType(span(), required, true);
        }
        const auto relation = this->relateTypes(candidate, output, required);
        if (relation == Certainty::NoSolution) {
            if (resolve_.ivars.getType(output)->is_Diverge()) {
                result = Certainty::Ambiguous;
                continue;
            }
            return Certainty::NoSolution;
        }
        if (relation == Certainty::Ambiguous) {
            result = Certainty::Ambiguous;
        }
    }
    return result;
}

auto NextTraitGoalEvaluator::evaluateAutoBuiltin(const HIRSimplePath& trait, const HIRPathParams& params, const HIRType* type) -> Certainty {
    auto combine = [](Certainty& result, Certainty nested) {
        if (nested == Certainty::NoSolution) {
            result = Certainty::NoSolution;
        } else if (nested == Certainty::Ambiguous && result == Certainty::Proven) {
            result = Certainty::Ambiguous;
        }
    };
    auto evaluateInner = [&](const HIRType* inner) {
        return solveGoal(trait, params, inner, nullptr);
    };

    switch ((*type).tag()) {
        default:
            return Certainty::Proven;
        case HIRType::TAG_Path: {
            auto& e = (*type).as_Path();
            if (const auto* pe = e.path.data.opt_Generic()) {
                const HIRType* tmp;
                auto monomorph = MonomorphStatePtr(crate.types, nullptr, &pe->params, nullptr);
                auto evaluateField = [&](const HIRType* field) {
                    const auto& fieldType = monomorphiseTypeNeeded(field) ? (tmp = resolve_.expandAssociatedTypes(span(), monomorph.monomorphType(span(), field))) : field;
                    return evaluateInner(fieldType);
                };

                if (e.binding.is_Unbound() || e.binding.is_Opaque()) {
                    return Certainty::Ambiguous;
                }
                Certainty result = Certainty::Proven;
                if (const auto* strPtr = e.binding.opt_Struct()) {
                    const auto& str = **strPtr;
                    switch (str.data.tag()) {
                        case HIRStruct::Data::TAG_Unit: {
                            break;
                        }
                        case HIRStruct::Data::TAG_Tuple: {
                            auto& se = str.data.as_Tuple();
                            for (const auto& field : se) {
                                combine(result, evaluateField(field.ent));
                                if (result == Certainty::NoSolution) {
                                    return result;
                                }
                            }
                            break;
                        }
                        case HIRStruct::Data::TAG_Named: {
                            auto& se = str.data.as_Named();
                            for (const auto& field : se) {
                                combine(result, evaluateField(field.ty));
                                if (result == Certainty::NoSolution) {
                                    return result;
                                }
                            }
                            break;
                        }
                    }
                } else if (const auto* enmPtr = e.binding.opt_Enum()) {
                    const auto& enm = **enmPtr;
                    if (const auto* variants = enm.data.opt_Data()) {
                        for (const auto& variant : *variants) {
                            combine(result, evaluateField(variant.type));
                            if (result == Certainty::NoSolution) {
                                return result;
                            }
                        }
                    }
                } else if (const auto* unnPtr = e.binding.opt_Union()) {
                    const auto& unn = **unnPtr;
                    for (const auto& field : unn.variants) {
                        combine(result, evaluateField(field.ty));
                        if (result == Certainty::NoSolution) {
                            return result;
                        }
                    }
                } else if (e.binding.is_ExternType()) {
                    return Certainty::NoSolution;
                }
                return result;
            }
            if (e.path.data.is_UfcsKnown() && (e.binding.is_Unbound() || e.binding.is_Opaque())) {
                return Certainty::Ambiguous;
            }
            return Certainty::Ambiguous;
        }
        case HIRType::TAG_Generic: {
            return evaluateInner(type);
        }
        case HIRType::TAG_Tuple: {
            auto& e = (*type).as_Tuple();
            Certainty result = Certainty::Proven;
            for (const auto& field : e) {
                combine(result, evaluateInner(field));
                if (result == Certainty::NoSolution) {
                    return result;
                }
            }
            return result;
        }
        case HIRType::TAG_Array: {
            auto& e = (*type).as_Array();
            return evaluateInner(e.inner);
        }
    }
    UNREACHABLE();
}

auto NextTraitGoalEvaluator::evaluateCandidate(size_t frameIndex, size_t candidateIndex, const HIRSimplePath& trait, const HIRTraitPath::assocListT* associated) -> Certainty {
    auto* candidate = frames[frameIndex]->candidates[candidateIndex];
    candidate->ambiguityBeyondHead = candidate->headNormalizationAmbiguity;
    candidate->nestedAmbiguity = false;
    candidate->headObligations.clear();
    candidate->relationEqualities.clear();
    candidate->relationValueEqualities.clear();
    candidate->relationObligations.clear();
    if (associated) {
        if (bindCandidatePlaceholders(*candidate, candidate->impl.getImplType(crate.types), *associated, true) == CandidateBindingResult::Mismatch) {
            return Certainty::NoSolution;
        }
    }

    struct CandidateHeadTypeBinding {
        const HIRType* original;
        const HIRType* probe;
    };

    struct CandidateHeadValueBinding {
        HIRConstGeneric original;
        unsigned probeIndex;
    };

    struct InstantiateCandidateHead final: MonomorphiserNop {
        HMTypeInferrence& table;
        mutable ThinVector<CandidateHeadTypeBinding> types;
        mutable ThinVector<CandidateHeadValueBinding> values;

        InstantiateCandidateHead(HIRTypeInterner& interner, HMTypeInferrence& table)
            : MonomorphiserNop(interner)
            , table(table)
        {
        }

        const HIRType* monomorphType(const Span& sp, const HIRType* type, bool allowInfer = true) const override {
            const auto* infer = type->opt_Infer();
            if (!infer || infer->index == ~0u || (isAliasInputInfer(infer->index) && !isSolverCanonicalInfer(infer->index))) {
                return MonomorphiserNop::monomorphType(sp, type, allowInfer);
            }
            for (const auto& binding : types) {
                if (binding.original == type) {
                    return binding.probe;
                }
            }
            auto probe = table.newIvarTr(infer->tyClass);
            types.push_back(CandidateHeadTypeBinding{type, probe});
            return probe;
        }

        HIRConstGeneric monomorphConstgeneric(const Span& sp, const HIRConstGeneric& value, bool allowInfer) const override {
            const auto* infer = value.opt_Infer();
            if (!infer || infer->index == ~0u || (isAliasInputInfer(infer->index) && !isSolverCanonicalInfer(infer->index))) {
                return MonomorphiserNop::monomorphConstgeneric(sp, value, allowInfer);
            }
            for (const auto& binding : values) {
                if (binding.original == value) {
                    return HIRConstGeneric::make_Infer({binding.probeIndex});
                }
            }
            const auto probeIndex = table.newIvarVal();
            values.push_back(CandidateHeadValueBinding{value.clone(), probeIndex});
            return HIRConstGeneric::make_Infer({probeIndex});
        }
    };

    struct MaterializeCandidateHead final: MonomorphiserNop {
        const HMTypeInferrence& table;
        const ThinVector<CandidateHeadTypeBinding>& types;
        const ThinVector<CandidateHeadValueBinding>& values;
        mutable unsigned resolvingDepth = 0;

        MaterializeCandidateHead(HIRTypeInterner& interner, const HMTypeInferrence& table, const ThinVector<CandidateHeadTypeBinding>& types, const ThinVector<CandidateHeadValueBinding>& values)
            : MonomorphiserNop(interner)
            , table(table)
            , types(types)
            , values(values)
        {
        }

        const HIRType* monomorphType(const Span& sp, const HIRType* type, bool allowInfer = true) const override {
            if (const auto* infer = type->opt_Infer()) {
                for (const auto& binding : types) {
                    const bool matches = resolvingDepth == 0 ? binding.original == type : binding.probe == type;
                    if (!matches) {
                        continue;
                    }
                    const auto* resolved = table.getType(binding.probe);
                    if (resolved == binding.probe) {
                        return binding.original;
                    }
                    resolvingDepth++;
                    auto materialized = this->monomorphType(sp, resolved, allowInfer);
                    resolvingDepth--;
                    return materialized;
                }
            }
            return MonomorphiserNop::monomorphType(sp, type, allowInfer);
        }

        HIRConstGeneric monomorphConstgeneric(const Span& sp, const HIRConstGeneric& value, bool allowInfer) const override {
            if (const auto* infer = value.opt_Infer()) {
                for (const auto& binding : values) {
                    const bool matches = resolvingDepth == 0 ? binding.original == value : binding.probeIndex == infer->index;
                    if (!matches) {
                        continue;
                    }
                    const auto probe = HIRConstGeneric::make_Infer({binding.probeIndex});
                    const auto& resolved = table.getValue(probe);
                    if (resolved == probe) {
                        return binding.original.clone();
                    }
                    resolvingDepth++;
                    auto materialized = this->monomorphConstgeneric(sp, resolved, allowInfer);
                    resolvingDepth--;
                    return materialized;
                }
            }
            return MonomorphiserNop::monomorphConstgeneric(sp, value, allowInfer);
        }
    };

    HMTypeInferrence headTable(crate.types);
    InstantiateCandidateHead instantiateHead(crate.types, headTable);
    Unifier headUnifier(span(), headTable, nullptr, {.bindRigidValues = true});
    for (const auto& equality : candidate->headEqualities) {
        const auto left = instantiateHead.monomorphType(span(), equality.left, true);
        const auto right = instantiateHead.monomorphType(span(), equality.right, true);
        if (headUnifier.unify(left, right) == Unifier::Outcome::Mismatch && !containsDefiningOpaque(equality.left) && !containsDefiningOpaque(equality.right)) {
            return Certainty::NoSolution;
        }
    }
    for (const auto& equality : candidate->headValueEqualities) {
        const auto left = instantiateHead.monomorphConstgeneric(span(), equality.left, true);
        const auto right = instantiateHead.monomorphConstgeneric(span(), equality.right, true);
        if (headUnifier.unifyValues(left, right) == Unifier::Outcome::Mismatch) {
            return Certainty::NoSolution;
        }
    }
    MaterializeCandidateHead materializeHead(crate.types, headTable, instantiateHead.types, instantiateHead.values);

    auto result = candidate->headRelation;

    for (const auto& equality : candidate->headEqualities) {
        const auto equalityResult = evaluateHeadEquality(*candidate, equality);
        if (equalityResult == Certainty::NoSolution) {
            return Certainty::NoSolution;
        }
        if (equalityResult == Certainty::Ambiguous) {
            result = Certainty::Ambiguous;
        }
    }
    for (const auto& equality : candidate->headValueEqualities) {
        const auto equalityResult = unifyValueProbe(equality.left, equality.right);
        if (equalityResult == Certainty::NoSolution) {
            return Certainty::NoSolution;
        }
        if (equalityResult == Certainty::Ambiguous) {
            result = Certainty::Ambiguous;
        }
    }
    if (!candidate->headObligations.empty()) {
        candidate->ambiguityBeyondHead = true;
        candidate->nestedAmbiguity = true;
    }

    const bool autoBuiltin = candidate->autoBuiltin;
    const auto* markerImpl = candidate->markerImpl;
    if (autoBuiltin) {
        const auto structural = evaluateAutoBuiltin(trait, candidate->impl.traitArgs, candidate->impl.type);
        if (structural == Certainty::NoSolution) {
            return Certainty::NoSolution;
        }
        if (structural == Certainty::Ambiguous) {
            candidate->ambiguityBeyondHead = true;
            candidate->nestedAmbiguity = true;
            result = Certainty::Ambiguous;
        }
    }

    struct ForwardedProjectionRequirement {
        const HIRType* type;
        HIRGenericPath trait;
        RcString item;
        HIRPathParams itemParams;
        const HIRType* required;
    };
    ThinVector<ForwardedProjectionRequirement> forwardedProjectionRequirements;
    if (associated) {
        for (const auto& requirement : *associated) {
            auto output = candidate->impl.getType(crate.types, requirement.first.c_str(), requirement.second.atyParams);
            if (output == nullptr) {
                continue;
            }
            output = materializeHead.monomorphType(span(), output, true);
            const auto required = materializeHead.monomorphType(span(), requirement.second.type, true);
            if (typeHasUnknown(required)) {
                continue;
            }

            const auto snapshot = resolve_.ivars.snapshot();
            Unifier relation(span(), resolve_.ivars, &resolve_);
            if (relation.unify(output, required) != Unifier::Outcome::Mismatch) {
                for (const auto& pending : relation.pending()) {
                    const auto append = [&](const HIRType* alias, const HIRType* other) {
                        const auto* path = alias->opt_Path();
                        const auto* projection = path ? path->path.data.opt_UfcsKnown() : nullptr;
                        if (!projection || !visitTyWith(output, [&](const HIRType* inner) { return inner == alias; })) {
                            return;
                        }
                        for (const auto& existing : forwardedProjectionRequirements) {
                            if (existing.type == projection->type && existing.trait.equalsIgnoringRegions(projection->trait) &&
                                existing.item == projection->item && existing.itemParams.equalsIgnoringRegions(projection->params) &&
                                existing.required == other) {
                                return;
                            }
                        }
                        forwardedProjectionRequirements.push_back(
                            ForwardedProjectionRequirement{
                                projection->type,
                                projection->trait.clone(),
                                projection->item,
                                projection->params.clone(),
                                other,
                            }
                        );
                    };
                    append(pending.left, pending.right);
                    append(pending.right, pending.left);
                }
            }
            resolve_.ivars.rollbackTo(snapshot);
        }
    }

    if (candidate->source == CandidateSource::Builtin && trait == resolve_.langUnsize()) {
        ASSERT_BUG(span(), candidate->impl.traitArgs.types.size() == 1, StringView("Unsize trait requires a single type param"));
        const auto structural = evaluateBuiltinUnsize(*candidate, candidate->impl.traitArgs.types[0], candidate->impl.getImplType(crate.types));
        if (structural == Certainty::NoSolution) {
            return Certainty::NoSolution;
        }
        if (structural == Certainty::Ambiguous) {
            candidate->ambiguityBeyondHead = true;
            candidate->nestedAmbiguity = true;
            result = Certainty::Ambiguous;
        }
    }

    const auto* traitImpl = candidate->impl.traitImpl;
    const auto candidateType = candidate->impl.getImplType(crate.types);
    const auto* candidatePath = candidateType->opt_Path();
    const auto* associatedProjection = candidatePath ? candidatePath->path.data.opt_UfcsKnown() : nullptr;
    const HIRAssociatedType* associatedDefinition = nullptr;
    if (!markerImpl && !traitImpl && associatedProjection) {
        const auto& definitionTrait = crate.getTraitByPath(span(), associatedProjection->trait.path);
        if (const auto it = definitionTrait.types.find(associatedProjection->item); it != definitionTrait.types.end()) {
            associatedDefinition = &it->second;
        }
    }
    const HIRGenericParams* implParamsDef = markerImpl ? &markerImpl->params : (traitImpl ? &traitImpl->params : associatedDefinition ? &associatedDefinition->generics : nullptr);
    if (!implParamsDef) {
        return result;
    }
    const HIRPathParams* boundParams = markerImpl ? &candidate->markerImplParams : traitImpl ? &candidate->impl.implParams : &associatedProjection->params;

    auto monomorphTraitBound = [&](const auto& traitBound, HIRSimplePath& nestedTrait, HIRPathParams& nestedParams, HIRTraitPath::assocListT& nestedAssociated) -> const HIRType* {
        auto monomorphBound = [&](auto& ms) {
            auto boundType = ms.monomorphType(span(), traitBound.type);
            auto boundTrait = ms.monomorphTraitpath(span(), traitBound.trait, true);

            nestedTrait = boundTrait.path.path;
            nestedParams = boundTrait.path.params.clone();
            for (const auto& aty : boundTrait.typeBounds) {
                nestedAssociated.insert({aty.first, aty.second.clone()});
            }
            return boundType;
        };
        if (markerImpl) {
            auto ms = MonomorphStatePtr(crate.types, nullptr, &candidate->markerImplParams, nullptr);
            return monomorphBound(ms);
        } else if (traitImpl) {
            auto nestedType = candidate->impl.monomorphImplType(crate.types, span(), traitBound.type);
            auto boundTrait = candidate->impl.monomorphImplTraitPath(crate.types, span(), traitBound.trait);
            nestedTrait = boundTrait.path.path;
            nestedParams = boundTrait.path.params.clone();
            for (const auto& aty : boundTrait.typeBounds) {
                nestedAssociated.insert({aty.first, aty.second.clone()});
            }
            return nestedType;
        } else {
            auto ms = MonomorphStatePtr(crate.types, associatedProjection->type, &associatedProjection->trait.params, &associatedProjection->params);
            return monomorphBound(ms);
        }
    };

    const auto forwardProjectionRequirements = [&](const HIRType* nestedType, const HIRSimplePath& nestedTrait, const HIRPathParams& nestedParams, HIRTraitPath::assocListT& nestedAssociated) {
        bool added = false;
        for (const auto& requirement : forwardedProjectionRequirements) {
            if (requirement.trait.path != nestedTrait || !requirement.trait.params.equalsIgnoringRegions(nestedParams) ||
                (requirement.type != nestedType && !requirement.type->equalsIgnoringRegions(nestedType))) {
                continue;
            }
            if (nestedAssociated.count(requirement.item)) {
                continue;
            }
            nestedAssociated.insert(
                {
                    requirement.item,
                    HIRTraitPath::AtyEqual{
                        requirement.trait.clone(),
                        requirement.itemParams.clone(),
                        requirement.required,
                    },
                }
            );
            added = true;
        }
        return added;
    };

    struct PreparedTraitBound {
        bool ready = false;
        const HIRType* type = nullptr;
        HIRSimplePath trait;
        HIRPathParams params;
        HIRTraitPath::assocListT associated;
    };
    ThinVector<PreparedTraitBound> preparedBounds(implParamsDef->bounds.size());
    bool prebindingChanged = false;
    for (size_t boundIndex = 0; boundIndex < implParamsDef->bounds.size(); boundIndex++) {
        const auto& bound = implParamsDef->bounds[boundIndex];
        const auto* traitBound = bound.opt_TraitBound();
        if (!traitBound) {
            continue;
        }
        auto& prepared = preparedBounds[boundIndex];
        prepared.type = monomorphTraitBound(*traitBound, prepared.trait, prepared.params, prepared.associated);
        const bool forwarded = forwardProjectionRequirements(prepared.type, prepared.trait, prepared.params, prepared.associated);
        prepared.ready = true;
        const auto binding = bindCandidatePlaceholders(*candidate, prepared.type, prepared.associated, false, forwarded);
        if (binding == CandidateBindingResult::Mismatch) {
            return Certainty::NoSolution;
        }
        prebindingChanged |= binding == CandidateBindingResult::Changed;
    }

    const auto assocResult = matchAssociatedTypes(trait, *candidate, associated, &materializeHead);
    if (assocResult == Certainty::NoSolution) {
        return Certainty::NoSolution;
    }
    if (assocResult == Certainty::Ambiguous) {
        DEBUG(StringView("candidate downgrade: assoc"));
        candidate->ambiguityBeyondHead = true;
        result = Certainty::Ambiguous;
    }

    {
        for (size_t i = 0; i < implParamsDef->types.size() && i < boundParams->types.size(); i++) {
            if (!implParamsDef->types[i].isSized) {
                continue;
            }
            const auto& bound = boundParams->types[i];
            if (bound == nullptr) {
                continue;
            }
            if (typeHasCandidatePlaceholder(bound)) {
                continue;
            }
            const auto sized = resolve_.typeIsSized(span(), bound);
            if (sized == HIRCompare::Unequal) {
                return Certainty::NoSolution;
            }
            if (sized == HIRCompare::Fuzzy) {
                DEBUG(StringView("candidate downgrade: implicit sized"));
                candidate->headObligations.push_back(
                    SolverObligation{
                        bound,
                        HIRTraitPath(HIRGenericPath(resolve_.langSized(), {})),
                    }
                );
                candidate->ambiguityBeyondHead = true;
                candidate->nestedAmbiguity = true;
                result = Certainty::Ambiguous;
            }
        }
    }

    for (size_t boundIndex = 0; boundIndex < implParamsDef->bounds.size(); boundIndex++) {
        const auto& bound = implParamsDef->bounds[boundIndex];
        if (const auto* be = bound.opt_TraitBound()) {
            const HIRType* nestedType;
            HIRSimplePath nestedTrait;
            HIRPathParams nestedParams;
            HIRTraitPath::assocListT nestedAssociated;

            auto& prepared = preparedBounds[boundIndex];
            if (prepared.ready && !prebindingChanged) {
                nestedType = prepared.type;
                nestedTrait = std::move(prepared.trait);
                nestedParams = std::move(prepared.params);
                nestedAssociated = std::move(prepared.associated);
            } else {
                nestedType = monomorphTraitBound(*be, nestedTrait, nestedParams, nestedAssociated);
                const bool forwarded = forwardProjectionRequirements(nestedType, nestedTrait, nestedParams, nestedAssociated);
                const auto binding = bindCandidatePlaceholders(*candidate, nestedType, nestedAssociated, false, forwarded);
                if (binding == CandidateBindingResult::Mismatch) {
                    return Certainty::NoSolution;
                }
                if (binding == CandidateBindingResult::Changed) {
                    nestedAssociated.clear();
                    nestedType = monomorphTraitBound(*be, nestedTrait, nestedParams, nestedAssociated);
                    forwardProjectionRequirements(nestedType, nestedTrait, nestedParams, nestedAssociated);
                }
            }

            const bool needsResponseConstraints = candidateNeedsResponseConstraints(*candidate);
            Certainty nested = Certainty::NoSolution;
            const bool rawResponseKnownNoEffect = needsResponseConstraints && findRawNestedNoEffectResponse(nestedTrait, nestedParams, nestedType, &nestedAssociated, *candidate, nested);
            bool nestedResponseIsIdentity = false;
            NestedResponseMemo responseMemo(*candidate);
            if (!rawResponseKnownNoEffect) {
                nested = solveGoal(nestedTrait, nestedParams, nestedType, &nestedAssociated, &nestedResponseIsIdentity, &responseMemo);
            }
            if (nestedResponseIsIdentity) {
                if (needsResponseConstraints) {
                    if (responseMemo.goal) {
                        rememberCanonicalNestedNoEffectResponse(responseMemo.goal->goal, *candidate, responseMemo.canonicalCandidateParams, nested);
                    }
                    rememberRawNestedNoEffectResponse(nestedTrait, nestedParams, nestedType, &nestedAssociated, *candidate, nested);
                }
            }
            const bool canonicalResponseKnownNoEffect = needsResponseConstraints && responseMemo.knownNoEffect;
            if (canonicalResponseKnownNoEffect) {
                nested = responseMemo.noEffectCertainty;
                rememberRawNestedNoEffectResponse(nestedTrait, nestedParams, nestedType, &nestedAssociated, *candidate, nested);
            }
            if (nested != Certainty::NoSolution && !nestedResponseIsIdentity && !rawResponseKnownNoEffect && !canonicalResponseKnownNoEffect && needsResponseConstraints) {
                Certainty responseCertainty = Certainty::NoSolution;
                auto responseBinding = CandidateBindingResult::Unchanged;
                bool responseHadEffects = false;
                auto nestedCallback = makeCallable<SolverResponseCb>([&](SolverResponse response) {
                    if (response.impl) {
                        responseBinding = bindCandidateResponse(*candidate, nestedType, nestedParams, nestedAssociated, *response.impl);
                        if (responseBinding == CandidateBindingResult::Mismatch) {
                            return false;
                        }
                    }
                    if (!response.impl && response.certainty != Certainty::Ambiguous) {
                        return false;
                    }
                    bool hasEffects = !response.equalities.empty() || !response.valueEqualities.empty() || !response.obligations.empty();
                    for (size_t i = 0; i < response.slots.typeInputs.size(); i++) {
                        hasEffects |= response.slots.typeInputs[i] != response.slots.types[i];
                    }
                    for (size_t i = 0; i < response.slots.valueInputs.size(); i++) {
                        hasEffects |= response.slots.valueInputs[i] != response.slots.values[i];
                    }
                    responseHadEffects = hasEffects;
                    responseCertainty = response.certainty;
                    appendRelationEffects(*candidate, std::move(response));
                    return true;
                });
                const bool hasResponse = evaluateTyped(span(), nestedTrait, nestedParams, nestedType, nestedCallback, {.ambiguity = SolverAmbiguityPolicy::Report});
                if (!hasResponse) {
                    return Certainty::NoSolution;
                }
                if (responseBinding == CandidateBindingResult::Unchanged && !responseHadEffects) {
                    if (responseMemo.goal) {
                        rememberCanonicalNestedNoEffectResponse(responseMemo.goal->goal, *candidate, responseMemo.canonicalCandidateParams, responseCertainty);
                    }
                    rememberRawNestedNoEffectResponse(nestedTrait, nestedParams, nestedType, &nestedAssociated, *candidate, responseCertainty);
                }
                nested = responseCertainty;
            }
            if (nested == Certainty::NoSolution) {
                return Certainty::NoSolution;
            }
            if (nested == Certainty::Ambiguous) {
                DEBUG(StringView("candidate downgrade: nested bound"));
                candidate->ambiguityBeyondHead = true;
                candidate->nestedAmbiguity = true;
                candidate->normalizationNestedGoals.pushBack(&bound);
                result = Certainty::Ambiguous;
            }
        } else if (const auto* equality = bound.opt_TypeEquality()) {
            const HIRType* left;
            const HIRType* right;
            if (markerImpl) {
                auto ms = MonomorphStatePtr(crate.types, nullptr, &candidate->markerImplParams, nullptr);
                left = ms.monomorphType(span(), equality->type);
                right = ms.monomorphType(span(), equality->otherType);
            } else if (traitImpl) {
                left = candidate->impl.monomorphImplType(crate.types, span(), equality->type);
                right = candidate->impl.monomorphImplType(crate.types, span(), equality->otherType);
            } else {
                auto ms = MonomorphStatePtr(crate.types, associatedProjection->type, &associatedProjection->trait.params, &associatedProjection->params);
                left = ms.monomorphType(span(), equality->type);
                right = ms.monomorphType(span(), equality->otherType);
            }
            const auto relation = this->relateTypes(*candidate, left, right);
            if (relation == Certainty::NoSolution) {
                return Certainty::NoSolution;
            }
            if (relation == Certainty::Ambiguous) {
                candidate->ambiguityBeyondHead = true;
                candidate->nestedAmbiguity = true;
                result = Certainty::Ambiguous;
            }
        }
    }
    return result;
}

auto NextTraitGoalEvaluator::solveGoal(const HIRSimplePath& trait, const HIRPathParams& params, const HIRType* type, const HIRTraitPath::assocListT* associated, bool* responseIsIdentity, NestedResponseMemo* responseMemo) -> Certainty {
    if (responseIsIdentity) {
        *responseIsIdentity = false;
    }
    if (responseMemo) {
        responseMemo->goal = nullptr;
        responseMemo->canonicalCandidateParams = HIRPathParams();
        responseMemo->prepared = false;
        responseMemo->knownNoEffect = false;
        responseMemo->noEffectCertainty = Certainty::NoSolution;
    }
    const auto availableDepth = availableDepthForNested();
    if (!availableDepth) {
        return Certainty::Ambiguous;
    }
    auto goalType = type;
    auto goalParams = params.clone();
    for (auto& value : goalParams.values) {
        if (const auto* infer = value.opt_Infer(); infer && infer->index != ~0u) {
            const auto& resolved = resolve_.ivars.getValue(value);
            if (!resolved.is_Infer()) {
                value = resolved.clone();
            }
        }
    }
    if (goalHasUnassignedInfer(goalParams, goalType, associated)) {
        return Certainty::Ambiguous;
    }
    goalType = normalizeGoalInput(goalType);
    for (auto& param : goalParams.types) {
        param = normalizeGoalInput(std::move(param));
    }
    if (goalHasUnassignedInfer(goalParams, goalType, associated)) {
        return Certainty::Ambiguous;
    }
    if (selfIsUnresolvedProjectionOverIvar(goalType)) {
        return Certainty::Ambiguous;
    }
    const auto& resolvedType = resolve_.resolveType(goalType);
    bool associatedConstrainsSelf = false;
    if (associated) {
        for (const auto& entry : *associated) {
            associatedConstrainsSelf |= !typeHasUnknown(entry.second.type);
        }
    }
    if (const auto* infer = resolvedType->opt_Infer()) {
        if (!infer->isLit() && !associatedConstrainsSelf) {
            return Certainty::Ambiguous;
        }
        if (infer->isLit() && goalParams.types.empty() && goalParams.values.empty() && !literalClassCanMatch(trait, goalParams, infer->tyClass)) {
            return Certainty::NoSolution;
        }
    }
    CanonicalizeTraitGoal canonicalizer(crate.types, &resolve_.ivars, &implExistentialScopes_, alphaExistentialScopeBase_);
    const auto canonical = canonicalizeGoal(goalParams, resolvedType, associated, canonicalizer);
    const auto* canonicalAssociated = canonical.associated.empty() ? nullptr : &canonical.associated;
    const auto hash = goalHashWithEnvironment(goalHash(trait, canonical.params, canonical.type, canonicalAssociated), canonicalizer.alphaSolverEnvironment());
    auto prepareResponseMemo = [&](CachedGoal* cached) {
        if (!responseMemo) {
            return;
        }
        if (cached) {
            responseMemo->goal = cached;
        }
        if (responseMemo->prepared) {
            return;
        }
        responseMemo->prepared = true;
        const auto& candidate = responseMemo->candidate;
        const auto& candidateParams = candidate.impl.traitImpl ? candidate.impl.implParams : candidate.markerImplParams;
        responseMemo->canonicalCandidateParams = canonicalizer.monomorphPathParams(span(), candidateParams, true);
        responseMemo->knownNoEffect = findCanonicalNestedNoEffectResponse(
            hash,
            trait,
            canonical.params,
            canonical.type,
            canonicalAssociated,
            canonicalizer.alphaSolverEnvironment(),
            candidate,
            responseMemo->canonicalCandidateParams,
            responseMemo->noEffectCertainty
        );
    };
    prepareResponseMemo(nullptr);
    if (responseMemo && responseMemo->knownNoEffect) {
        return responseMemo->noEffectCertainty;
    }
    if (auto* cached = findCachedGoal(hash, trait, canonical.params, canonical.type, canonicalAssociated, &canonicalizer.alphaSolverEnvironment())) {
        if (responseIsIdentity) {
            *responseIsIdentity = cached->responseIsIdentity;
        }
        prepareResponseMemo(cached);
        return cached->certainty;
    }
    const bool crateCacheable = !canonicalAssociated && crateCacheUsable() && goalIsConcrete(trait, canonical);
    if (crateCacheable) {
        if (const auto* global = crateCache().find(hash, trait, canonical.params, canonical.type)) {
            return global->certainty;
        }
    }
    CanonicalizeTraitGoal activeCanonicalizer(crate.types, &resolve_.ivars);
    const auto activeCanonical = canonicalizeGoal(goalParams, resolvedType, associated, activeCanonicalizer);
    const auto* activeAssociated = activeCanonical.associated.empty() ? nullptr : &activeCanonical.associated;
    const auto activeHash = goalHash(trait, activeCanonical.params, activeCanonical.type, activeAssociated);
    if (findActiveGoal(activeHash, trait, activeCanonical.params, activeCanonical.type, activeAssociated)) {
        cycleHits_++;
        return crate.getTraitByPath(span(), trait).isCoinductive ? Certainty::Proven : Certainty::Ambiguous;
    }

    auto* activeGoal = pushActiveGoal(activeHash, trait, activeCanonical.params, activeCanonical.type, activeAssociated);

    STD_DEFER {
        popActiveGoal(activeGoal);
    };

    const auto cycleHitsBefore = cycleHits_;
    const bool rigidKey = canonicalGoalIsRigid(canonical);
    auto cacheResult = [&](Certainty certainty, bool identityResponse = false) {
        DEBUG(StringView("solveGoal ") << trait << StringView(" for ") << type << StringView(" => ") << static_cast<unsigned>(certainty));
        if (responseIsIdentity) {
            *responseIsIdentity = identityResponse;
        }
        if (crateCacheable && rigidKey && cycleHits_ == cycleHitsBefore) {
            crateCache().insert(hash, trait, canonical.params.clone(), canonical.type, certainty);
        }
        auto* cached = cacheGoal(hash, trait, canonical.params, canonical.type, canonicalAssociated, certainty, rigidKey && cycleHits_ == cycleHitsBefore, identityResponse, &canonicalizer.alphaSolverEnvironment());
        prepareResponseMemo(cached);
        return certainty;
    };

    const size_t frameIndex = frameDepth++;
    if (frameIndex == frames.length()) {
        frames.pushBack(resolve_.eatCachePool->make<CandidateFrame>());
    }
    frames[frameIndex]->clear(candidateNodes);
    frames[frameIndex]->availableDepth = *availableDepth;

    STD_DEFER {
        const bool encounteredOverflow = frames[frameIndex]->encounteredOverflow;
        frames[frameIndex]->clear(candidateNodes);
        BUG_ASSERT(frameDepth == frameIndex + 1);
        frameDepth--;
        if (encounteredOverflow && frameIndex > 0) {
            frames[frameIndex - 1]->encounteredOverflow = true;
        }
    };

    assembleCandidates(frameIndex, trait, canonical.params, canonical.type);

    bool sawAmbiguous = false;
    bool suppressAutoBuiltin = false;
    bool negativeProven = false;
    bool negativeAmbiguous = false;
    Certainty autoBuiltinResult = Certainty::NoSolution;
    const size_t candidateCount = frames[frameIndex]->candidates.length();
    const size_t paramEnvCandidateCount = static_cast<size_t>(std::count_if(
        frames[frameIndex]->candidates.begin(),
        frames[frameIndex]->candidates.end(),
        [](const Candidate* candidate) { return candidate->source == CandidateSource::ParamEnv; }
    ));
    for (size_t i = 0; i < candidateCount; i++) {
        const auto result = evaluateCandidate(frameIndex, i, trait, canonicalAssociated);
        auto* candidate = frames[frameIndex]->candidates[i];
        candidate->certainty = result;
        if (candidate->isNegative()) {
            negativeProven |= result == Certainty::Proven;
            negativeAmbiguous |= result == Certainty::Ambiguous;
            continue;
        }
        if (candidate->autoBuiltin) {
            autoBuiltinResult = result;
            continue;
        }
        suppressAutoBuiltin |= candidate->isPositiveMarkerImpl() && result != Certainty::NoSolution;
        if (result == Certainty::Proven) {
            const bool identityResponse = !canonicalAssociated && candidate->source == CandidateSource::ParamEnv &&
                paramEnvCandidateCount == 1 && paramEnvCandidateIsNonGlobal(*candidate) &&
                assembledHeadIsExact(canonical.params, canonical.type, candidate->impl) &&
                candidate->headEqualities.empty() && candidate->headValueEqualities.empty() &&
                candidate->coercionEqualities.empty() && candidate->headObligations.empty() &&
                candidate->relationEqualities.empty() && candidate->relationValueEqualities.empty() && candidate->relationObligations.empty();
            return cacheResult(Certainty::Proven, identityResponse);
        }
        sawAmbiguous |= result == Certainty::Ambiguous;
    }
    if (!suppressAutoBuiltin && !negativeProven) {
        if (negativeAmbiguous && autoBuiltinResult == Certainty::Proven) {
            autoBuiltinResult = Certainty::Ambiguous;
        }
        if (autoBuiltinResult == Certainty::Proven) {
            return cacheResult(Certainty::Proven);
        }
        sawAmbiguous |= autoBuiltinResult == Certainty::Ambiguous;
    }
    const auto* selfPath = resolvedType->opt_Path();
    const bool selfIsAlias = selfPath && (selfPath->binding.is_Opaque() || selfPath->binding.is_Unbound());
    const auto* selfErased = resolvedType->opt_ErasedType();
    const bool selfIsRigidOpaque = selfErased && selfErased->inner.is_Fcn();
    bool paramsHoldOpaque = !selfIsRigidOpaque && containsDefiningOpaque(resolvedType);
    for (const auto& ty : goalParams.types) {
        paramsHoldOpaque |= containsDefiningOpaque(ty);
    }
    const bool inferMayUnlock = resolvedType->is_Infer() || paramsHoldOpaque || (selfIsAlias && (resolve_.typeContainsIvars(resolvedType) || resolve_.paramsContainIvars(goalParams)));
    if (sawAmbiguous || inferMayUnlock || (coherenceMode && !traitRefIsKnowable(trait, goalParams, resolvedType))) {
        return cacheResult(Certainty::Ambiguous);
    }
    return cacheResult(Certainty::NoSolution);
}

auto NextTraitGoalEvaluator::literalClassCanMatch(const HIRSimplePath& trait, const HIRPathParams& params, HIRInferClass tyClass) const -> bool {
    if (crate.getTraitByPath(span(), trait).isMarker) {
        return true;
    }
    static const HIRCoreType intPrims[] = {HIRCoreType::I8, HIRCoreType::U8, HIRCoreType::I16, HIRCoreType::U16, HIRCoreType::I32, HIRCoreType::U32, HIRCoreType::I64, HIRCoreType::U64, HIRCoreType::I128, HIRCoreType::U128, HIRCoreType::Isize, HIRCoreType::Usize};
    static const HIRCoreType floatPrims[] = {HIRCoreType::F16, HIRCoreType::F32, HIRCoreType::F64, HIRCoreType::F128};
    const HIRCoreType* prims = tyClass == HIRInferClass::Integer ? intPrims : floatPrims;
    const size_t count = tyClass == HIRInferClass::Integer ? 12 : 4;
    for (size_t i = 0; i < count; i++) {
        const auto prim = crate.types.primitive(prims[i]);
        bool matches = false;
        auto probe = [&](SolverImpl, Certainty) {
            matches = true;
            return true;
        };
        resolve_.assembleMagicCandidates(span(), trait, params, prim, probe);
        if (!matches) {
            resolve_.assembleParamEnvCandidates(span(), trait, params, prim, probe);
        }
        if (!matches) {
            crate.findTraitImpls(trait, prim, HIRResolvePlaceholdersNop(), [&](const HIRTraitImpl&) {
                matches = true;
                return true;
            });
        }
        if (matches) {
            return true;
        }
    }
    return false;
}

auto NextTraitGoalEvaluator::containsDefiningOpaque(const HIRType* ty) const -> bool {
    return visitTyWith(ty, [&](const HIRType* inner) {
        const auto* erased = inner->opt_ErasedType();
        if (!erased) {
            return false;
        }
        if (const auto* fcn = erased->inner.opt_Fcn()) {
            return resolve_.isDefiningFcnOrigin(fcn->origin);
        }
        const auto* alias = erased->inner.opt_Alias();
        return alias && resolve_.isOpaqueAliasDefiningScope(*alias->inner);
    });
}

auto NextTraitGoalEvaluator::matchRootAssociated(const HIRSimplePath& trait, Candidate& candidate, const char* assocName, const HIRType* assocType, const HIRPathParams* assocParams) -> Certainty {
    if (!assocName || !assocName[0]) {
        return Certainty::Proven;
    }
    const auto& impl = candidate.impl;
    const HIRPathParams noParams;
    const auto& params = assocParams ? *assocParams : noParams;
    if (!impl.isTraitImpl() && params.hasParams()) {
        return Certainty::Ambiguous;
    }
    auto output = impl.getType(crate.types, assocName, params);
    if (output == nullptr) {
        if (impl.isTraitImpl()) {
            return Certainty::Ambiguous;
        }
        return !assocType || typeHasUnknown(assocType) ? Certainty::Proven : Certainty::Ambiguous;
    }
    if (!assocType) {
        return Certainty::Proven;
    }
    const auto relation = this->relateTypes(candidate, assocType, output);
    if (relation == Certainty::NoSolution) {
        if (resolve_.ivars.getType(output)->is_Diverge()) {
            return Certainty::Ambiguous;
        }
        return Certainty::NoSolution;
    }
    return relation;
}

auto NextTraitGoalEvaluator::materializeRootAssociated(SolverImpl impl, const HIRSimplePath& trait, const char* assocName, const HIRPathParams* assocParams) const -> SolverImpl {
    if (!assocName || !assocName[0] || impl.isTraitImpl()) {
        return impl;
    }
    const HIRPathParams noParams;
    const auto& itemParams = assocParams ? *assocParams : noParams;
    if (impl.getType(crate.types, assocName, itemParams) != nullptr) {
        return impl;
    }

    auto type = impl.getImplType(crate.types);
    auto params = impl.getTraitParams(crate.types);
    auto associated = std::move(impl.associated);

    const auto name = RcString::newInterned(assocName);
    auto sourceTrait = HIRGenericPath(trait, params.clone());
    auto projection = makeAssociatedProjection(type, sourceTrait, name, itemParams);
    associated.erase(name);
    associated.insert({name, HIRTraitPath::AtyEqual{std::move(sourceTrait), itemParams.clone(), std::move(projection)}});
    return SolverImpl(std::move(type), std::move(params), std::move(associated));
}

auto NextTraitGoalEvaluator::appendResponseObligations(ThinVector<SolverObligation>& obligations, const Candidate* candidate, const Monomorphiser& canonicalizer) const -> void {
    if (!candidate) {
        return;
    }

    auto append = [&](const HIRType* type, HIRTraitPath trait) {
        type = canonicalizer.monomorphType(span(), type, true);
        trait = canonicalizer.monomorphTraitpath(span(), trait, true);
        obligations.push_back(SolverObligation{std::move(type), std::move(trait)});
    };
    for (const auto& obligation : candidate->headObligations) {
        append(obligation.type, obligation.trait.clone());
    }
    for (const auto& obligation : candidate->relationObligations) {
        append(obligation.type, obligation.trait.clone());
    }

    const HIRGenericParams* params = nullptr;
    if (candidate->markerImpl) {
        params = &candidate->markerImpl->params;
    } else if (candidate->impl.traitImpl) {
        params = &candidate->impl.traitImpl->params;
    }
    if (!params) {
        return;
    }

    auto needsResponse = [&](const HIRType* type) {
        return visitTyWith(type, [&](const HIRType* inner) {
            return inner->is_Infer() || inner->is_NodeType() || containsDefiningOpaque(inner);
        });
    };
    auto isNormalizationGoal = [&](const HIRGenericBound& bound) {
        for (const auto* nested : candidate->normalizationNestedGoals) {
            if (nested == &bound) {
                return true;
            }
        }
        return false;
    };
    for (const auto& bound : params->bounds) {
        const auto* traitBound = bound.opt_TraitBound();
        if (!traitBound) {
            continue;
        }
        const HIRType* type;
        HIRTraitPath trait;
        if (candidate->markerImpl) {
            auto monomorph = MonomorphStatePtr(crate.types, nullptr, &candidate->markerImplParams, nullptr);
            type = monomorph.monomorphType(span(), traitBound->type);
            trait = monomorph.monomorphTraitpath(span(), traitBound->trait, true);
        } else {
            type = candidate->impl.monomorphImplType(crate.types, span(), traitBound->type);
            trait = candidate->impl.monomorphImplTraitPath(crate.types, span(), traitBound->trait);
        }

        bool needed = isNormalizationGoal(bound) || needsResponse(type);
        for (const auto& argument : trait.path.params.types) {
            needed |= needsResponse(argument);
        }
        for (const auto& associated : trait.typeBounds) {
            needed |= needsResponse(associated.second.type);
        }
        if (needed) {
            append(std::move(type), std::move(trait));
        }
    }
}

auto NextTraitGoalEvaluator::implDefinesValue(const SolverImpl& impl, const char* valueName) -> bool {
    if (!impl.traitImpl) {
        return false;
    }
    const auto name = RcString::newInterned(valueName);
    return impl.traitImpl->constants.count(name) || impl.traitImpl->statics.count(name) || impl.traitImpl->methods.count(name);
}

auto NextTraitGoalEvaluator::specializationValueSource(const Candidate* selected, const char* valueName) -> const Candidate* {
    for (const Candidate* source = selected; source; source = source->specializationItemSource) {
        if (implDefinesValue(source->impl, valueName)) {
            return source;
        }
    }
    return nullptr;
}

auto NextTraitGoalEvaluator::responsesEqual(const Candidate& leftCandidate, const Candidate& rightCandidate, const char* assocName, const HIRPathParams* assocParams, const char* valueName) const -> bool {
    const auto& left = leftCandidate.impl;
    const auto& right = rightCandidate.impl;
    auto typesEqual = [&](const HIRType* lhs, const HIRType* rhs) {
        if (lhs == nullptr || rhs == nullptr) {
            return lhs == rhs;
        }
        if (lhs == rhs) {
            return true;
        }
        const auto* resolvedLhs = resolve_.resolveType(lhs);
        const auto* resolvedRhs = resolve_.resolveType(rhs);
        return resolvedLhs == resolvedRhs || resolvedLhs->equalsIgnoringRegions(resolvedRhs);
    };
    auto paramsEqual = [&](const HIRPathParams& lhs, const HIRPathParams& rhs) {
        if (lhs.types.size() != rhs.types.size() || lhs.values.size() != rhs.values.size()) {
            return false;
        }
        for (size_t i = 0; i < lhs.types.size(); i++) {
            if (!typesEqual(lhs.types[i], rhs.types[i])) {
                return false;
            }
        }
        for (size_t i = 0; i < lhs.values.size(); i++) {
            if (lhs.values[i] != rhs.values[i]) {
                return false;
            }
        }
        return true;
    };

    const auto materializeHead = [&](const Candidate& candidate) {
        if (candidate.responseHeadMaterialized) {
            return;
        }
        candidate.responseImplType = resolve_.expandAssociatedTypes(span(), candidate.impl.getImplType(crate.types));
        candidate.responseTraitParams = candidate.impl.getTraitParams(crate.types);
        for (auto& type : candidate.responseTraitParams.types) {
            type = resolve_.expandAssociatedTypes(span(), std::move(type));
        }
        candidate.responseHeadMaterialized = true;
    };
    materializeHead(leftCandidate);
    materializeHead(rightCandidate);
    if (!typesEqual(leftCandidate.responseImplType, rightCandidate.responseImplType) || !paramsEqual(leftCandidate.responseTraitParams, rightCandidate.responseTraitParams)) {
        return false;
    }
    if (valueName) {
        const auto* leftImpl = left.traitImpl;
        const auto* rightImpl = right.traitImpl;
        if (leftImpl || rightImpl) {
            if (!leftImpl || !rightImpl) {
                return false;
            }
            const bool leftDefines = implDefinesValue(left, valueName);
            const bool rightDefines = implDefinesValue(right, valueName);
            if (!leftDefines && !rightDefines) {
                return true;
            }
            return leftDefines && rightDefines && leftImpl == rightImpl;
        }
        return true;
    }
    if (!assocName || !assocName[0]) {
        return true;
    }
    const HIRPathParams noParams;
    const auto& params = assocParams ? *assocParams : noParams;
    if ((!left.isTraitImpl() || !right.isTraitImpl()) && params.hasParams()) {
        return false;
    }
    const auto materializeAssociated = [&](const Candidate& candidate) {
        if (candidate.responseAssociatedMaterialized) {
            return;
        }
        auto value = candidate.impl.getType(crate.types, assocName, params);
        if (value != nullptr) {
            value = resolve_.expandAssociatedTypes(span(), value);
        }
        candidate.responseAssociatedType = std::move(value);
        candidate.responseAssociatedMaterialized = true;
    };
    materializeAssociated(leftCandidate);
    materializeAssociated(rightCandidate);
    const auto& leftValue = leftCandidate.responseAssociatedType;
    const auto& rightValue = rightCandidate.responseAssociatedType;
    if (!left.isTraitImpl() && !right.isTraitImpl() && (leftValue == nullptr) != (rightValue == nullptr)) {
        return true;
    }
    return typesEqual(leftValue, rightValue);
}

NextTraitGoalEvaluator::NextTraitGoalEvaluator(const TraitResolution& resolve, const HIRCrate& crate)
    : resolve_(resolve)
    , crate(crate)
    , implExistentials_(resolve.eatCachePool.mutPtr())
    , implExistentialScopes_(resolve.eatCachePool.mutPtr())
    , candidateNodes(resolve.eatCachePool.mutPtr())
    , activeGoalNodes(resolve.eatCachePool.mutPtr())
    , cachedGoalNodes(resolve.eatCachePool.mutPtr())
    , emptyRootGoalIndex(resolve.eatCachePool.mutPtr())
    , rawNestedNoEffectResponseIndex(resolve.eatCachePool.mutPtr())
    , canonicalNestedNoEffectResponseIndex(resolve.eatCachePool.mutPtr())
    , overlapCache(resolve.eatCachePool.mutPtr())
{
    BUG_ASSERT(resolve.board().id < SOLVER_ALPHA_SCOPE_BASE);
    alphaExistentialScopeBase_ = SOLVER_ALPHA_SCOPE_BASE;
    frames.grow(16);
    goalStack.grow(16);
    goalCache.grow(64);
    activeGoalIndex.reserve(32);
    goalCacheIndex.reserve(128);
    emptyRootGoals.reserve(32);
    rawNestedNoEffectResponses.reserve(32);
    canonicalNestedNoEffectResponses.reserve(64);
}

auto NextTraitGoalEvaluator::evaluateOverlap(const Span& callSpan, const HIRSimplePath& trait, const HIRTraitImpl& left, const HIRTraitImpl& right) -> bool {
    const auto key = splitMix64(reinterpret_cast<uintptr_t>(&left)) ^ splitMix64(~reinterpret_cast<uintptr_t>(&right));
    auto* bucket = overlapCache.find(key);
    if (bucket) {
        for (const auto& ent : *bucket) {
            if (ent.left == &left && ent.right == &right) {
                return ent.overlaps;
            }
        }
    }
    const bool rv = evaluateOverlapUncached(callSpan, trait, left, right);
    if (!bucket) {
        bucket = overlapCache.insert(key);
    }
    bucket->push_back(OverlapEntry{&left, &right, rv});
    return rv;
}

auto NextTraitGoalEvaluator::evaluateOverlapUncached(const Span& callSpan, const HIRSimplePath& trait, const HIRTraitImpl& left, const HIRTraitImpl& right) -> bool {
    ASSERT_BUG(callSpan, !span_, StringView("nested coherence overlap session"));
    ASSERT_BUG(callSpan, !coherenceMode, StringView("coherence mode leaked before overlap probe"));
    ASSERT_BUG(callSpan, goalStack.empty(), StringView("next-solver goal stack leaked before coherence probe"));
    ASSERT_BUG(callSpan, activeGoalIndex.empty(), StringView("next-solver active goal index leaked before coherence probe"));
    ASSERT_BUG(callSpan, frameDepth == 0, StringView("next-solver candidate frames leaked before coherence probe"));
    clearGoalCache(true);
    span_ = &callSpan;
    coherenceMode = true;

    STD_DEFER {
        BUG_ASSERT(goalStack.empty());
        BUG_ASSERT(activeGoalIndex.empty());
        clearGoalCache(true);
        frameDepth = 0;
        coherenceMode = false;
        span_ = nullptr;
    };

    auto leftParams = resolve_.makeFreshImplParams(left.params);
    auto leftMonomorph = MonomorphStatePtr(crate.types, nullptr, &leftParams, nullptr);
    auto goalType = leftMonomorph.monomorphType(callSpan, left.type, true);
    auto goalParams = leftMonomorph.monomorphPathParams(callSpan, left.traitArgs, true);

    HIRPathParams rightParams;
    bool rightHeadNormalizationAmbiguity = false;
    ThinVector<SolverTypeEquality> rightHeadEqualities;
    ThinVector<SolverValueEquality> rightHeadValueEqualities;
    const auto rightRelation = this->unifyImplHead(right.params, right.traitArgs, right.type, goalParams, goalType, rightParams, rightHeadNormalizationAmbiguity, rightHeadEqualities, rightHeadValueEqualities);
    if (rightRelation == Certainty::NoSolution) {
        return false;
    }

    const size_t frameIndex = frameDepth++;
    if (frameIndex == frames.length()) {
        frames.pushBack(resolve_.eatCachePool->make<CandidateFrame>());
    }
    frames[frameIndex]->clear(candidateNodes);
    frames[frameIndex]->availableDepth = ROOT_DEPTH;

    STD_DEFER {
        const bool encounteredOverflow = frames[frameIndex]->encounteredOverflow;
        frames[frameIndex]->clear(candidateNodes);
        BUG_ASSERT(frameDepth == frameIndex + 1);
        frameDepth--;
        if (encounteredOverflow && frameIndex > 0) {
            frames[frameIndex - 1]->encounteredOverflow = true;
        }
    };

    const auto& traitDef = crate.getTraitByPath(callSpan, trait);
    pushCandidate(frameIndex, SolverImpl(std::move(leftParams), traitDef, trait, left), true, Certainty::Proven, nullptr, {}, false, CandidateSource::TraitImpl);
    pushCandidate(frameIndex, SolverImpl(std::move(rightParams), traitDef, trait, right), rightRelation == Certainty::Proven, rightRelation, nullptr, {}, false, CandidateSource::TraitImpl, rightHeadNormalizationAmbiguity, std::move(rightHeadEqualities), std::move(rightHeadValueEqualities));

    const auto& candidates = frames[frameIndex]->candidates;
    ASSERT_BUG(callSpan, candidates.length() == 2, StringView("coherence probe lost an impl candidate"));
    const auto leftResult = evaluateCandidate(frameIndex, 0, trait, nullptr);
    if (leftResult == Certainty::NoSolution) {
        return false;
    }
    const auto rightResult = evaluateCandidate(frameIndex, 1, trait, nullptr);
    return rightResult != Certainty::NoSolution;
}

auto NextTraitGoalEvaluator::evaluateTyped(const Span& callSpan, const HIRSimplePath& trait, const HIRPathParams& params, const HIRType* type, SolverResponseCallback& callback, const TraitGoalQuery& query, bool callerBoundary, bool includeRootMagicCandidates) -> bool {
    const char* assocName = query.assocName;
    const HIRType* assocType = query.assocType;
    const HIRPathParams* assocParams = query.assocParams;
    const auto* associated = query.associated;
    ASSERT_BUG(callSpan, !associated || !assocName, StringView("trait query cannot mix a full associated constraint set with a single associated item"));
    const char* valueName = query.valueName;
    const bool allowInferInputs = query.allowInferInputs;
    const auto* excludedImpl = query.excludedImpl;
    const bool hasCoercionGoals = query.coercions && !query.coercions->empty();
    const bool coercionSelectsCandidate = hasCoercionGoals && trait != resolve_.langPointeeSized();
    const bool hasSelfCoercionGoal = hasCoercionGoals && std::any_of(query.coercions->begin(), query.coercions->end(), [](const SolverCoercionConstraint& constraint) {
        return constraint.isSelf;
    });
    const bool outermost = span_ == nullptr;
    if (outermost) {
        ASSERT_BUG(callSpan, goalStack.empty(), StringView("next-solver goal stack leaked between evaluations"));
        ASSERT_BUG(callSpan, activeGoalIndex.empty(), StringView("next-solver active goal index leaked between evaluations"));
        ASSERT_BUG(callSpan, frameDepth == 0, StringView("next-solver candidate frames leaked between evaluations"));
        if (envGeneration_ != resolve_.eatCacheGeneration) {
            envGeneration_ = resolve_.eatCacheGeneration;
            for (auto* goal : goalCache) {
                goal->persistent = false;
            }
            ivarGenerationSeen_ = resolve_.ivars.mutationGeneration;
            solverEnvGenerationSeen_ = resolve_.solverEnvGeneration;
            clearGoalCache(true);
        } else if (ivarGenerationSeen_ != resolve_.ivars.mutationGeneration || solverEnvGenerationSeen_ != resolve_.solverEnvGeneration) {
            const bool environmentChanged = solverEnvGenerationSeen_ != resolve_.solverEnvGeneration;
            ivarGenerationSeen_ = resolve_.ivars.mutationGeneration;
            solverEnvGenerationSeen_ = resolve_.solverEnvGeneration;
            clearGoalCache(environmentChanged);
        }
        span_ = &callSpan;
    }

    STD_DEFER {
        if (outermost) {
            BUG_ASSERT(goalStack.empty());
            BUG_ASSERT(activeGoalIndex.empty());
            if (ivarGenerationSeen_ != resolve_.ivars.mutationGeneration || solverEnvGenerationSeen_ != resolve_.solverEnvGeneration) {
                const bool environmentChanged = solverEnvGenerationSeen_ != resolve_.solverEnvGeneration;
                ivarGenerationSeen_ = resolve_.ivars.mutationGeneration;
                solverEnvGenerationSeen_ = resolve_.solverEnvGeneration;
                clearGoalCache(environmentChanged);
            }
            frameDepth = 0;
            span_ = nullptr;
        }
    };

    auto goalType = type;
    auto goalParams = params.clone();
    for (auto& value : goalParams.values) {
        if (const auto* infer = value.opt_Infer(); infer && infer->index != ~0u) {
            const auto& resolved = resolve_.ivars.getValue(value);
            if (!resolved.is_Infer()) {
                value = resolved.clone();
            }
        }
    }
    auto emitForcedAmbiguity = [&]() {
        if (query.ambiguity != SolverAmbiguityPolicy::Report) {
            return false;
        }
        SolverResponse response;
        response.certainty = Certainty::Ambiguous;
        if (query.operatorGoal) {
            response.operatorSummary.hasSemanticImpl = true;
        }
        return callback.visit(std::move(response));
    };
    if (!allowInferInputs && goalHasUnassignedInfer(goalParams, goalType, nullptr)) {
        return emitForcedAmbiguity();
    }
    goalType = normalizeGoalInput(goalType);
    for (auto& param : goalParams.types) {
        param = normalizeGoalInput(std::move(param));
    }
    if (selfIsUnresolvedProjectionOverIvar(goalType)) {
        return emitForcedAmbiguity();
    }
    const auto& resolvedType = resolve_.resolveType(goalType);
    bool associatedConstrainsSelf = assocName && assocName[0] && assocType && !typeHasUnknown(assocType);
    if (associated) {
        for (const auto& entry : *associated) {
            associatedConstrainsSelf |= !typeHasUnknown(entry.second.type);
        }
    }
    if (const auto* infer = resolvedType->opt_Infer()) {
        if (!infer->isLit() && !associatedConstrainsSelf && !hasSelfCoercionGoal) {
            return emitForcedAmbiguity();
        }
        if (infer->isLit() && goalParams.types.empty() && goalParams.values.empty() && !literalClassCanMatch(trait, goalParams, infer->tyClass)) {
            return false;
        }
    }
    const auto emitNoViable = [&]() {
        const auto* selfPath = resolvedType->opt_Path();
        const bool selfIsAlias = selfPath && (selfPath->binding.is_Opaque() || selfPath->binding.is_Unbound());
        const auto* selfErased = resolvedType->opt_ErasedType();
        const bool selfIsRigidOpaque = selfErased && selfErased->inner.is_Fcn();
        bool holdsOpaque = !selfIsRigidOpaque && containsDefiningOpaque(resolvedType);
        for (const auto& ty : goalParams.types) {
            holdsOpaque |= containsDefiningOpaque(ty);
        }
        if (resolvedType->is_Infer() || holdsOpaque || (selfIsAlias && (resolve_.typeContainsIvars(resolvedType) || resolve_.paramsContainIvars(goalParams)))) {
            return emitForcedAmbiguity();
        }
        return false;
    };
    CanonicalizeTraitGoal canonicalizer(crate.types, &resolve_.ivars, &implExistentialScopes_, alphaExistentialScopeBase_);
    const auto canonical = canonicalizeGoal(goalParams, resolvedType, associated, canonicalizer);
    const auto* canonicalAssociated = canonical.associated.empty() ? nullptr : &canonical.associated;
    const HIRType* canonicalAssocTypeStorage;
    const HIRType* canonicalAssocType = nullptr;
    if (assocType) {
        canonicalAssocTypeStorage = canonicalizer.monomorphType(span(), assocType, true);
        canonicalAssocType = canonicalAssocTypeStorage;
    }
    HIRPathParams canonicalAssocParamsStorage;
    const HIRPathParams* canonicalAssocParams = nullptr;
    if (assocParams) {
        canonicalAssocParamsStorage = canonicalizer.monomorphPathParams(span(), *assocParams, true);
        canonicalAssocParams = &canonicalAssocParamsStorage;
    }
    const bool hasAssociatedItemQuery = assocName && assocName[0];
    ThinVector<SolverCoercionConstraint> canonicalCoercions;
    if (hasCoercionGoals) {
        for (const auto& constraint : *query.coercions) {
            canonicalCoercions.push_back(
                SolverCoercionConstraint{
                    constraint.typeIndex,
                    canonicalizer.monomorphType(span(), constraint.other, true),
                    constraint.direction,
                    constraint.op,
                    constraint.isSelf,
                    constraint.inputRequiresSized,
                    constraint.allowSourceAutoderef,
                    constraint.bindInputToCandidate,
                    constraint.alternativeGroup,
                }
            );
        }
    }
    HIRTraitPath::assocListT responseCacheAssociatedStorage;
    const auto* responseCacheAssociated = canonicalAssociated;
    if (hasAssociatedItemQuery && canonicalAssocType) {
        const HIRPathParams noParams;
        const auto& itemParams = canonicalAssocParams ? *canonicalAssocParams : noParams;
        auto sourceTrait = HIRGenericPath(trait, canonical.params.clone());
        responseCacheAssociatedStorage.insert(
            {
                RcString::newInterned(assocName),
                HIRTraitPath::AtyEqual{std::move(sourceTrait), itemParams.clone(), canonicalAssocType},
            }
        );
        responseCacheAssociated = &responseCacheAssociatedStorage;
    }
    const bool suppressAmbiguity = query.ambiguity != SolverAmbiguityPolicy::Report;
    const auto rootHash = hashMix(
        goalHashWithEnvironment(goalHash(trait, canonical.params, canonical.type, responseCacheAssociated), canonicalizer.alphaSolverEnvironment()),
        suppressAmbiguity
    );
    const bool cacheEmptyAssembly = outermost && !hasCoercionGoals;
    const auto emptyAssemblyHash = cacheEmptyAssembly ? hashMix(goalHashWithEnvironment(goalHash(trait, canonical.params, canonical.type, nullptr), canonicalizer.alphaSolverEnvironment()), includeRootMagicCandidates) : 0;
    Vector<std::pair<const Candidate*, Certainty>> distinctViable;
    auto deliverResponse = [&](const SolverResponse& response, const SolverImpl* directImpl) {
        if (!outermost && !callerBoundary) {
            DecanonicalizeSolverInfers mapper(crate.types, canonicalizer);
            auto nestedResponse = monomorphSolverResponse(response, mapper);
            if (directImpl) {
                auto direct = monomorphCandidateImpl(*directImpl, mapper);
                nestedResponse.impl = ownSolverImpl(std::move(direct));
            }
            return callback.visit(std::move(nestedResponse));
        }
        InstantiateTraitResponseForCaller instantiator(crate.types, resolve_.ivars, canonicalizer.placeholderNames(), &canonicalizer);
        auto callerResponse = monomorphSolverResponse(response, instantiator);
        if (directImpl) {
            auto direct = monomorphCandidateImpl(*directImpl, instantiator);
            callerResponse.impl = ownSolverImpl(std::move(direct));
        }
        return callback.visit(std::move(callerResponse));
    };
    const bool cacheableAssociatedItem = !hasAssociatedItemQuery || canonicalAssocType;
    const bool cacheableResponse = cacheableAssociatedItem && !valueName && !canonicalAssociated && !excludedImpl && !hasCoercionGoals && !query.operatorGoal;
    const bool crateCacheableResponse = cacheableResponse && !suppressAmbiguity && !hasAssociatedItemQuery && crateCacheUsable() && goalIsConcrete(trait, canonical);
    if (cacheableResponse) {
        if (const auto* cached = findCachedGoal(rootHash, trait, canonical.params, canonical.type, responseCacheAssociated, &canonicalizer.alphaSolverEnvironment(), suppressAmbiguity);
            cached && cached->hasResponse) {
            return deliverResponse(*cached->response, nullptr);
        }
        if (crateCacheableResponse) {
            if (const auto* global = crateCache().find(rootHash, trait, canonical.params, canonical.type); global && global->hasResponse) {
                return deliverResponse(*global->response, nullptr);
            }
        }
    }
    if (cacheEmptyAssembly && rootAssemblyKnownEmpty(emptyAssemblyHash, trait, canonical.params, canonical.type, canonicalizer.alphaSolverEnvironment(), includeRootMagicCandidates)) {
        return emitNoViable();
    }
    const auto cycleHitsBefore = cycleHits_;
    const bool rigidKey = canonicalGoalIsRigid(canonical);
    const auto appendAssociatedEquality = [&](auto& response, const HIRType* required, const HIRType* output) {
        response.equalities.push_back(SolverTypeEquality{required, output});

        const auto snapshot = resolve_.ivars.snapshot();
        Unifier unifier(span(), resolve_.ivars, &resolve_);
        const auto outcome = unifier.unify(required, output);
        if (outcome != Unifier::Outcome::Mismatch) {
            for (const auto& equality : unifier.pending()) {
                response.equalities.push_back(SolverTypeEquality{equality.left, equality.right});
            }
            for (const auto& equality : unifier.pendingValues()) {
                response.valueEqualities.push_back(SolverValueEquality{equality.left.clone(), equality.right.clone()});
            }
        }
        resolve_.ivars.rollbackTo(snapshot);

        const auto appendStructuralValues = [&](auto&& self, const HIRType* lhs, const HIRType* rhs) -> void {
            if (lhs == rhs || lhs->tag() != rhs->tag()) {
                return;
            }
            const auto appendParams = [&](const HIRPathParams& leftParams, const HIRPathParams& rightParams) {
                if (leftParams.types.size() == rightParams.types.size()) {
                    for (size_t i = 0; i < leftParams.types.size(); i++) {
                        self(self, leftParams.types[i], rightParams.types[i]);
                    }
                }
                if (leftParams.values.size() == rightParams.values.size()) {
                    for (size_t i = 0; i < leftParams.values.size(); i++) {
                        if (leftParams.values[i] != rightParams.values[i]) {
                            response.valueEqualities.push_back(SolverValueEquality{leftParams.values[i].clone(), rightParams.values[i].clone()});
                        }
                    }
                }
            };
            switch (lhs->tag()) {
                case HIRType::TAG_Path: {
                    const auto& leftPath = lhs->as_Path().path.data;
                    const auto& rightPath = rhs->as_Path().path.data;
                    if (leftPath.tag() != rightPath.tag()) {
                        return;
                    }
                    if (const auto* left = leftPath.opt_Generic()) {
                        const auto& right = rightPath.as_Generic();
                        if (left->path == right.path) {
                            appendParams(left->params, right.params);
                        }
                    } else if (const auto* left = leftPath.opt_UfcsKnown()) {
                        const auto& right = rightPath.as_UfcsKnown();
                        if (left->trait.path == right.trait.path && left->item == right.item) {
                            self(self, left->type, right.type);
                            appendParams(left->trait.params, right.trait.params);
                            appendParams(left->params, right.params);
                        }
                    }
                    return;
                }
                case HIRType::TAG_Array: {
                    const auto& left = lhs->as_Array();
                    const auto& right = rhs->as_Array();
                    self(self, left.inner, right.inner);
                    if (left.size.is_Unevaluated() && right.size.is_Unevaluated() && left.size.as_Unevaluated() != right.size.as_Unevaluated()) {
                        response.valueEqualities.push_back(
                            SolverValueEquality{
                                left.size.as_Unevaluated().clone(),
                                right.size.as_Unevaluated().clone(),
                            }
                        );
                    }
                    return;
                }
                case HIRType::TAG_Tuple: {
                    const auto& left = lhs->as_Tuple();
                    const auto& right = rhs->as_Tuple();
                    if (left.length() == right.length()) {
                        for (size_t i = 0; i < left.length(); i++) {
                            self(self, left[i], right[i]);
                        }
                    }
                    return;
                }
                case HIRType::TAG_Slice:
                    self(self, lhs->as_Slice().inner, rhs->as_Slice().inner);
                    return;
                case HIRType::TAG_Borrow:
                    self(self, lhs->as_Borrow().inner, rhs->as_Borrow().inner);
                    return;
                case HIRType::TAG_Pointer:
                    self(self, lhs->as_Pointer().inner, rhs->as_Pointer().inner);
                    return;
                default:
                    return;
            }
        };
        appendStructuralValues(appendStructuralValues, required, output);

        const auto* requiredPath = required->opt_Path();
        const auto* outputPath = output->opt_Path();
        const auto* left = requiredPath ? requiredPath->path.data.opt_UfcsKnown() : nullptr;
        const auto* right = outputPath ? outputPath->path.data.opt_UfcsKnown() : nullptr;
        if (!left || !right || left->trait.path != right->trait.path || left->item != right->item) {
            return;
        }
        const auto appendParams = [&](const HIRPathParams& lhs, const HIRPathParams& rhs) {
            if (lhs.types.size() == rhs.types.size()) {
                for (size_t i = 0; i < lhs.types.size(); i++) {
                    if (lhs.types[i] != rhs.types[i]) {
                        response.equalities.push_back(SolverTypeEquality{lhs.types[i], rhs.types[i]});
                        const auto* leftArray = lhs.types[i]->opt_Array();
                        const auto* rightArray = rhs.types[i]->opt_Array();
                        if (leftArray && rightArray && leftArray->size.is_Unevaluated() && rightArray->size.is_Unevaluated() && leftArray->size.as_Unevaluated() != rightArray->size.as_Unevaluated()) {
                            response.valueEqualities.push_back(
                                SolverValueEquality{
                                    leftArray->size.as_Unevaluated().clone(),
                                    rightArray->size.as_Unevaluated().clone(),
                                }
                            );
                        }
                    }
                }
            }
            if (lhs.values.size() == rhs.values.size()) {
                for (size_t i = 0; i < lhs.values.size(); i++) {
                    if (lhs.values[i] != rhs.values[i]) {
                        response.valueEqualities.push_back(SolverValueEquality{lhs.values[i].clone(), rhs.values[i].clone()});
                    }
                }
            }
        };
        if (left->type != right->type) {
            response.equalities.push_back(SolverTypeEquality{left->type, right->type});
        }
        appendParams(left->trait.params, right->trait.params);
        appendParams(left->params, right->params);
    };
    const auto operatorImplHasBuiltinSignature = [&](const SolverImpl& impl) {
        ASSERT_BUG(span(), query.operatorGoal, StringView("operator candidate classification without an operator goal"));
        const auto& operatorGoal = *query.operatorGoal;
        auto implType = impl.getImplType(crate.types);
        auto implParams = impl.getTraitParams(crate.types);
        if (resolve_.ivars.typeContainsIvars(implType, /*onlyUnbound=*/true) || resolve_.ivars.pathparamsContainIvars(implParams, /*onlyUnbound=*/true)) {
            return false;
        }
        implType = resolve_.expandAssociatedTypesInplace(span(), implType);
        for (auto& type : implParams.types) {
            type = resolve_.expandAssociatedTypesInplace(span(), type);
        }

        const bool hasBuiltinInputs = implParams.types.empty() ? primitiveOperatorHasBuiltin(operatorGoal.operation, implType) : implParams.types.size() == 1 && primitiveOperatorHasBuiltin(operatorGoal.operation, implType, implParams.types.front());
        if (!hasBuiltinInputs) {
            return false;
        }
        if (!operatorGoal.outputName || !operatorGoal.outputName[0]) {
            return true;
        }

        const HIRPathParams noParams;
        const auto& outputParams = operatorGoal.outputParams ? *operatorGoal.outputParams : noParams;
        auto output = impl.getType(crate.types, operatorGoal.outputName, outputParams);
        if (output == nullptr || resolve_.ivars.typeContainsIvars(output, /*onlyUnbound=*/true)) {
            return false;
        }
        output = resolve_.expandAssociatedTypesInplace(span(), output);

        auto builtinOutput = implType;
        if (operatorGoal.operation == TypeckPrimitiveOperator::Deref) {
            if (const auto* pointer = implType->opt_Pointer()) {
                builtinOutput = pointer->inner;
            } else if (const auto* borrow = implType->opt_Borrow()) {
                builtinOutput = borrow->inner;
            } else {
                return false;
            }
        }
        return output->compareWithPlaceholders(span(), builtinOutput, resolve_.ivars.callbackResolveInfer()) == HIRCompare::Equal;
    };

    const auto classifyOperatorImpl = [&](SolverOperatorSummary& summary, const SolverImpl& impl) {
        if (!query.operatorGoal) {
            return;
        }
        const bool builtinSignature = operatorImplHasBuiltinSignature(impl);
        const auto* traitImpl = impl.traitImpl;
        if (query.operatorGoal->currentImpl && traitImpl == query.operatorGoal->currentImpl) {
            summary.sawCurrentImpl = true;
            summary.currentImplHasBuiltinSignature = builtinSignature;
        } else if (!builtinSignature) {
            summary.hasSemanticImpl = true;
        }
    };

    const auto appendCandidateEffects = [&](SolverResponse& response, const Candidate* candidate) {
        appendResponseObligations(response.obligations, candidate, canonicalizer);
        if (!candidate) {
            return;
        }
        const auto appendTypes = [&](const auto& equalities) {
            for (const auto& equality : equalities) {
                response.equalities.push_back(
                    SolverTypeEquality{
                        canonicalizer.monomorphType(span(), equality.left, true),
                        canonicalizer.monomorphType(span(), equality.right, true),
                    }
                );
            }
        };
        const auto appendValues = [&](const auto& equalities) {
            for (const auto& equality : equalities) {
                response.valueEqualities.push_back(
                    SolverValueEquality{
                        canonicalizer.monomorphConstgeneric(span(), equality.left, true),
                        canonicalizer.monomorphConstgeneric(span(), equality.right, true),
                    }
                );
            }
        };
        appendTypes(candidate->headEqualities);
        appendValues(candidate->headValueEqualities);
        appendTypes(candidate->relationEqualities);
        appendValues(candidate->relationValueEqualities);
        appendTypes(candidate->coercionEqualities);
    };

    auto emitResponse = [&](SolverImpl response, Certainty certainty, const Candidate* responseCandidate = nullptr, bool exposeImpl = true) {
        canonicalizer.freeze();
        auto canonicalResponse = monomorphCandidateImpl(response, canonicalizer);
        SolverResponse solverResponse;
        solverResponse.certainty = certainty;
        solverResponse.slots = extractSlotValues(canonical, canonicalResponse, canonicalizer, solverResponse.certainty);
        if (exposeImpl) {
            solverResponse.impl = ownSolverImpl(monomorphCandidateImpl(canonicalResponse, MonomorphiserNop(crate.types)));
        }
        if (distinctViable.empty()) {
            classifyOperatorImpl(solverResponse.operatorSummary, canonicalResponse);
        }
        if (!exposeImpl) {
            for (size_t i = 0; i < solverResponse.slots.types.size(); i++) {
                solverResponse.slots.types[i] = solverResponse.slots.typeInputs[i];
            }
            for (size_t i = 0; i < solverResponse.slots.values.size(); i++) {
                solverResponse.slots.values[i] = solverResponse.slots.valueInputs[i].clone();
            }
        }
        appendCandidateEffects(solverResponse, responseCandidate);
        if (exposeImpl && canonicalAssocType && assocName && assocName[0]) {
            const HIRPathParams noParams;
            const auto& itemParams = canonicalAssocParams ? *canonicalAssocParams : noParams;
            auto output = canonicalResponse.getType(crate.types, assocName, itemParams);
            if (output != nullptr) {
                output = normalizeGoalInput(std::move(output));
                appendAssociatedEquality(solverResponse, canonicalAssocType, canonicalizer.monomorphType(span(), output, true));
            }
        }
        if (exposeImpl && canonicalAssociated) {
            for (const auto& requirement : *canonicalAssociated) {
                auto output = canonicalResponse.getType(crate.types, requirement.first.c_str(), requirement.second.atyParams);
                if (output != nullptr) {
                    output = normalizeGoalInput(std::move(output));
                    appendAssociatedEquality(solverResponse, requirement.second.type, canonicalizer.monomorphType(span(), output, true));
                }
            }
        }
        if (distinctViable.length() != 0) {
            ThinVector<SolverSlotValues> candidateSlots;
            ThinVector<SolverResponse> candidateEffects;
            for (size_t i = 0; i < distinctViable.length(); i++) {
                const auto* candidate = distinctViable[i].first;
                const auto candidateCertainty = distinctViable[i].second;
                auto candidateImpl = monomorphCandidateImpl(candidate->impl, canonicalizer);
                classifyOperatorImpl(solverResponse.operatorSummary, candidateImpl);
                candidateSlots.push_back(extractSlotValues(canonical, candidateImpl, canonicalizer, candidateCertainty));
                SolverResponse effects;
                appendCandidateEffects(effects, candidate);
                if (canonicalAssocType && assocName && assocName[0]) {
                    const HIRPathParams noParams;
                    const auto& itemParams = canonicalAssocParams ? *canonicalAssocParams : noParams;
                    auto output = candidateImpl.getType(crate.types, assocName, itemParams);
                    const Candidate* inheritedFrom = nullptr;
                    if (output == nullptr && candidate->impl.isTraitImpl()) {
                        const auto considerInherited = [&](const Candidate* source) {
                            if (!source || !source->impl.isTraitImpl() || !candidate->impl.moreSpecificThan(crate.types, source->impl)) {
                                return;
                            }
                            auto sourceImpl = monomorphCandidateImpl(source->impl, canonicalizer);
                            if (sourceImpl.getType(crate.types, assocName, itemParams) == nullptr) {
                                return;
                            }
                            if (!inheritedFrom || source->impl.moreSpecificThan(crate.types, inheritedFrom->impl)) {
                                inheritedFrom = source;
                            }
                        };
                        for (const Candidate* source = candidate->specializationItemSource; source; source = source->specializationItemSource) {
                            considerInherited(source);
                        }
                        for (const auto& viable : distinctViable) {
                            for (const Candidate* source = viable.first; source; source = source->specializationItemSource) {
                                if (source != candidate) {
                                    considerInherited(source);
                                }
                            }
                        }
                        if (inheritedFrom) {
                            auto sourceImpl = monomorphCandidateImpl(inheritedFrom->impl, canonicalizer);
                            output = sourceImpl.getType(crate.types, assocName, itemParams);
                        }
                    }
                    if (output != nullptr) {
                        output = normalizeGoalInput(std::move(output));
                        appendAssociatedEquality(effects, canonicalAssocType, std::move(output));
                    }
                }
                candidateEffects.push_back(std::move(effects));
            }

            const auto& first = candidateSlots.front();
            for (size_t slot = 0; slot < solverResponse.slots.types.size(); slot++) {
                if (slot >= first.types.size()) {
                    break;
                }
                const auto common = first.types[slot];
                bool shared = true;
                for (size_t candidate = 1; candidate < candidateSlots.size(); candidate++) {
                    const auto& slots = candidateSlots[candidate].types;
                    if (slot >= slots.size() || slots[slot] != common) {
                        shared = false;
                        break;
                    }
                }
                if (shared) {
                    solverResponse.slots.types[slot] = common;
                }
            }
            for (size_t slot = 0; slot < solverResponse.slots.values.size(); slot++) {
                if (slot >= first.values.size()) {
                    break;
                }
                const auto common = first.values[slot].clone();
                bool shared = true;
                for (size_t candidate = 1; candidate < candidateSlots.size(); candidate++) {
                    const auto& slots = candidateSlots[candidate].values;
                    if (slot >= slots.size() || slots[slot] != common) {
                        shared = false;
                        break;
                    }
                }
                if (shared) {
                    solverResponse.slots.values[slot] = common.clone();
                }
            }

            const auto& firstEffects = candidateEffects.front();
            const auto sharedTypeEquality = [&](const SolverTypeEquality& equality) {
                for (size_t candidate = 1; candidate < candidateEffects.size(); candidate++) {
                    const auto& equalities = candidateEffects[candidate].equalities;
                    if (std::none_of(equalities.begin(), equalities.end(), [&](const SolverTypeEquality& other) {
                        return (other.left == equality.left && other.right == equality.right) || (other.left == equality.right && other.right == equality.left);
                    })) {
                        return false;
                    }
                }
                return true;
            };
            const auto sharedValueEquality = [&](const SolverValueEquality& equality) {
                for (size_t candidate = 1; candidate < candidateEffects.size(); candidate++) {
                    const auto& equalities = candidateEffects[candidate].valueEqualities;
                    if (std::none_of(equalities.begin(), equalities.end(), [&](const SolverValueEquality& other) {
                        return (other.left == equality.left && other.right == equality.right) || (other.left == equality.right && other.right == equality.left);
                    })) {
                        return false;
                    }
                }
                return true;
            };
            const auto sharedObligation = [&](const SolverObligation& obligation) {
                for (size_t candidate = 1; candidate < candidateEffects.size(); candidate++) {
                    const auto& obligations = candidateEffects[candidate].obligations;
                    if (std::none_of(obligations.begin(), obligations.end(), [&](const SolverObligation& other) {
                        return other.type == obligation.type && other.trait == obligation.trait;
                    })) {
                        return false;
                    }
                }
                return true;
            };
            for (const auto& equality : firstEffects.equalities) {
                if (sharedTypeEquality(equality)) {
                    solverResponse.equalities.push_back(equality);
                }
            }
            for (const auto& equality : firstEffects.valueEqualities) {
                if (sharedValueEquality(equality)) {
                    solverResponse.valueEqualities.push_back(SolverValueEquality{equality.left.clone(), equality.right.clone()});
                }
            }
            for (const auto& obligation : firstEffects.obligations) {
                if (sharedObligation(obligation)) {
                    solverResponse.obligations.push_back(SolverObligation{obligation.type, obligation.trait.clone()});
                }
            }
        }
        size_t keptTypeSlots = 0;
        for (size_t i = 0; i < solverResponse.slots.typeInputs.size(); i++) {
            const bool protectedCoercionInput = std::any_of(canonicalCoercions.begin(), canonicalCoercions.end(), [&](const SolverCoercionConstraint& constraint) {
                if (constraint.bindInputToCandidate) {
                    return false;
                }
                const auto* input = constraint.isSelf ? canonical.type : canonical.params.types.at(constraint.typeIndex);
                return solverResponse.slots.typeInputs[i] == input;
            });
            if (protectedCoercionInput) {
                continue;
            }
            if (keptTypeSlots != i) {
                solverResponse.slots.typeInputs[keptTypeSlots] = std::move(solverResponse.slots.typeInputs[i]);
                solverResponse.slots.types[keptTypeSlots] = std::move(solverResponse.slots.types[i]);
            }
            keptTypeSlots++;
        }
        solverResponse.slots.typeInputs.resize(keptTypeSlots);
        solverResponse.slots.types.resize(keptTypeSlots);
        if (!cacheableResponse || canonicalizer.sawForeignIvar() || canonicalizer.sawForeignSolverExistential()) {
            return deliverResponse(solverResponse, exposeImpl ? &response : nullptr);
        }
        if (crateCacheableResponse && rigidKey && cycleHits_ == cycleHitsBefore) {
            auto* global = crateCache().insert(rootHash, trait, canonical.params.clone(), canonical.type, solverResponse.certainty);
            auto globalResponse = monomorphSolverResponse(solverResponse, MonomorphiserNop(crate.types));
            if (globalResponse.impl) {
                auto globalImpl = monomorphCandidateImpl(*globalResponse.impl, MonomorphiserNop(crate.types));
                globalResponse.impl = crateCache().pool->make<SolverImpl>(std::move(globalImpl));
            }
            global->response = crateCache().pool->make<SolverResponse>(std::move(globalResponse));
            global->hasResponse = true;
        }
        auto* storedResponse = resolve_.eatCachePool->make<SolverResponse>(std::move(solverResponse));
        auto* cached = cacheResponse(rootHash, trait, canonical.params, canonical.type, responseCacheAssociated, storedResponse, &canonicalizer.alphaSolverEnvironment(), suppressAmbiguity);
        cached->persistent = rigidKey && cycleHits_ == cycleHitsBefore;
        return deliverResponse(*storedResponse, nullptr);
    };
    CanonicalizeTraitGoal activeCanonicalizer(crate.types, &resolve_.ivars);
    const auto activeCanonical = canonicalizeGoal(goalParams, resolvedType, associated, activeCanonicalizer);
    const auto* activeAssociated = activeCanonical.associated.empty() ? nullptr : &activeCanonical.associated;
    const auto activeHash = goalHash(trait, activeCanonical.params, activeCanonical.type, activeAssociated);
    if (findActiveGoal(activeHash, trait, activeCanonical.params, activeCanonical.type, activeAssociated)) {
        const bool coinductive = crate.getTraitByPath(span(), trait).isCoinductive;
        cycleHits_++;
        DEBUG(StringView("evaluate exit: active-goal cycle"));
        return emitResponse(SolverImpl(resolvedType, &goalParams, nullptr), coinductive ? Certainty::Proven : Certainty::Ambiguous, nullptr, coinductive);
    }
    auto* rootGoal = pushActiveGoal(activeHash, trait, activeCanonical.params, activeCanonical.type, activeAssociated);

    STD_DEFER {
        popActiveGoal(rootGoal);
    };

    const size_t frameIndex = frameDepth++;
    if (frameIndex == frames.length()) {
        frames.pushBack(resolve_.eatCachePool->make<CandidateFrame>());
    }
    frames[frameIndex]->clear(candidateNodes);
    frames[frameIndex]->availableDepth = ROOT_DEPTH;

    STD_DEFER {
        const bool encounteredOverflow = frames[frameIndex]->encounteredOverflow;
        frames[frameIndex]->clear(candidateNodes);
        BUG_ASSERT(frameDepth == frameIndex + 1);
        frameDepth--;
        if (encounteredOverflow && frameIndex > 0) {
            frames[frameIndex - 1]->encounteredOverflow = true;
        }
    };

    const auto selfGuidanceIsExact = [&](const SolverCoercionConstraint& constraint) {
        if (!constraint.isSelf || constraint.direction != SolverCoercionConstraint::Direction::InputIsDestination || constraint.op != SolverCoercionOp::Unsizing) {
            return false;
        }
        const auto* guidedPath = constraint.other->opt_Path();
        if (constraint.other->is_Infer() || (guidedPath && guidedPath->binding.is_Unbound())) {
            return false;
        }
        if (trait == resolve_.langSized()) {
            return true;
        }
        const auto& definition = crate.getTraitByPath(span(), trait);
        return std::any_of(definition.allParentTraits.begin(), definition.allParentTraits.end(), [&](const HIRTraitPath& parent) {
            return parent.path.path == resolve_.langSized();
        }) || std::any_of(definition.parentTraits.begin(), definition.parentTraits.end(), [&](const HIRTraitPath& parent) {
            return parent.path.path == resolve_.langSized();
        });
    };
    const bool hasExactSelfGuidance = coercionSelectsCandidate && canonical.type->is_Infer() && std::any_of(canonicalCoercions.begin(), canonicalCoercions.end(), selfGuidanceIsExact);
    if (!hasExactSelfGuidance) {
        assembleCandidates(frameIndex, trait, canonical.params, canonical.type, includeRootMagicCandidates);
    }
    if (coercionSelectsCandidate && canonical.type->is_Infer()) {
        for (const auto& constraint : canonicalCoercions) {
            if (!constraint.isSelf) {
                continue;
            }
            const auto* guidedSelf = constraint.other;
            const auto* guidedPath = guidedSelf->opt_Path();
            if (guidedSelf->is_Infer() || (guidedPath && guidedPath->binding.is_Unbound())) {
                continue;
            }
            assembleCandidates(frameIndex, trait, canonical.params, guidedSelf, includeRootMagicCandidates);
            if (selfGuidanceIsExact(constraint)) {
                const HIRType* dereferencedStorage;
                while ((guidedSelf = resolve_.autoderef(span(), guidedSelf))) {
                    assembleCandidates(frameIndex, trait, canonical.params, guidedSelf, includeRootMagicCandidates);
                }
            }
        }
    }
    auto& frame = *frames[frameIndex];
    const size_t candidateCount = frame.candidates.length();
    if (candidateCount == 0) {
        if (cacheEmptyAssembly) {
            rememberEmptyRootAssembly(emptyAssemblyHash, trait, canonical.params, canonical.type, canonicalizer.alphaSolverEnvironment(), includeRootMagicCandidates);
        }
        return emitNoViable();
    }
    DEBUG(StringView("next-solver assembled ") << candidateCount << StringView(" candidate(s) for ") << type << StringView(": ") << trait << params);
    bool suppressAutoBuiltin = false;
    bool negativeProven = false;
    bool negativeAmbiguous = false;
    const HIRType* candidateAssocType = canonicalAssocType;
    if (candidateAssocType) {
        if (const auto* erased = candidateAssocType->opt_ErasedType()) {
            if (const auto* alias = erased->inner.opt_Alias(); alias && resolve_.isOpaqueAliasDefiningScope(*alias->inner)) {
                candidateAssocType = nullptr;
            }
        }
    }
    HIRTraitPath::assocListT rootAssociated;
    for (const auto& entry : canonical.associated) {
        rootAssociated.insert({entry.first, entry.second.clone()});
    }
    if (assocName && assocName[0] && candidateAssocType && !typeHasUnknown(candidateAssocType)) {
        const HIRPathParams noAssocParams;
        auto sourceTrait = HIRGenericPath(trait, canonical.params.clone());
        HIRGenericPath declaringTrait;
        if (resolve_.traitContainsType(span(), sourceTrait, crate.getTraitByPath(span(), trait), assocName, declaringTrait)) {
            sourceTrait = std::move(declaringTrait);
        }
        rootAssociated.insert({RcString::newInterned(assocName), HIRTraitPath::AtyEqual{std::move(sourceTrait), canonicalAssocParams ? canonicalAssocParams->clone() : noAssocParams.clone(), candidateAssocType}});
    }
    const auto evaluateCandidateCoercions = [&](Candidate& candidate) {
        if (candidate.coercionsEvaluated) {
            return;
        }
        candidate.coercionsEvaluated = true;
        candidate.discarded = false;
        candidate.coercionsProven = true;
        auto self = candidate.impl.getImplType(crate.types);
        const auto& inputs = candidate.impl.getTraitParamsRef(crate.types);
        bool selfBoundByCoercion = false;
        Vector<bool> inputBoundByCoercion;
        inputBoundByCoercion.zero(inputs.types.size());
        ThinVector<unsigned> evaluatedAlternativeGroups;
        for (const auto& constraint : canonicalCoercions) {
            if (constraint.alternativeGroup != 0 && std::find(evaluatedAlternativeGroups.begin(), evaluatedAlternativeGroups.end(), constraint.alternativeGroup) != evaluatedAlternativeGroups.end()) {
                continue;
            }
            ASSERT_BUG(span(), constraint.isSelf || constraint.typeIndex < inputs.types.size(), StringView("coercion-constrained trait input is out of range"));
            auto result = Certainty::NoSolution;
            const SolverCoercionConstraint* selectedConstraint = nullptr;
            ThinVector<SolverTypeEquality> selectedEqualities;
            const auto evaluate = [&](const SolverCoercionConstraint& alternative) {
                ASSERT_BUG(span(), alternative.isSelf || alternative.typeIndex < inputs.types.size(), StringView("coercion-constrained trait input is out of range"));
                const auto* input = alternative.isSelf ? self : inputs.types[alternative.typeIndex];
                ThinVector<SolverTypeEquality> alternativeEqualities;
                const auto alternativeResult = resolve_.evaluateCoercionConstraint(span(), alternative, input, &alternativeEqualities);
                if (alternativeResult == Certainty::Proven || (alternativeResult == Certainty::Ambiguous && result == Certainty::NoSolution)) {
                    result = alternativeResult;
                    selectedConstraint = &alternative;
                    selectedEqualities = std::move(alternativeEqualities);
                }
                return alternativeResult;
            };
            if (constraint.alternativeGroup == 0) {
                result = evaluate(constraint);
            } else {
                evaluatedAlternativeGroups.push_back(constraint.alternativeGroup);
                for (const auto& alternative : canonicalCoercions) {
                    if (alternative.alternativeGroup != constraint.alternativeGroup) {
                        continue;
                    }
                    if (evaluate(alternative) == Certainty::Proven) {
                        break;
                    }
                }
            }
            if (result == Certainty::NoSolution) {
                candidate.discarded = true;
                break;
            }
            if (result == Certainty::Proven && selectedConstraint && selectedConstraint->bindInputToCandidate) {
                if (selectedConstraint->isSelf) {
                    selfBoundByCoercion = true;
                } else {
                    inputBoundByCoercion.mut(selectedConstraint->typeIndex) = true;
                }
            }
            for (auto& equality : selectedEqualities) {
                candidate.coercionEqualities.push_back(std::move(equality));
            }
            if (result == Certainty::Ambiguous) {
                candidate.coercionsProven = false;
                candidate.certainty = Certainty::Ambiguous;
                candidate.ambiguityBeyondHead = true;
            }
        }
        const auto hasOpenInput = [](const HIRType* type) {
            return visitTyWith(type, [](const HIRType* inner) {
                const auto* infer = inner->opt_Infer();
                return infer && isSolverCanonicalInfer(infer->index);
            });
        };
        bool openHeadIsBound = !hasOpenInput(canonical.type) || selfBoundByCoercion;
        for (size_t i = 0; i < canonical.params.types.size(); i++) {
            openHeadIsBound &= !hasOpenInput(canonical.params.types[i]) || (i < inputBoundByCoercion.length() && inputBoundByCoercion[i]);
        }
        if (!candidate.discarded && candidate.coercionsProven && openHeadIsBound && candidate.certainty == Certainty::Ambiguous && !candidate.ambiguityBeyondHead) {
            candidate.certainty = Certainty::Proven;
        }
    };
    auto evaluateCandidateAt = [&](size_t i) {
        auto* candidate = frame.candidates[i];
        if (excludedImpl) {
            const auto* traitImpl = candidate->impl.traitImpl;
            if (traitImpl == excludedImpl) {
                candidate->traitCertainty = Certainty::NoSolution;
                candidate->certainty = Certainty::NoSolution;
                return false;
            }
        }
        auto certainty = evaluateCandidate(frameIndex, i, trait, rootAssociated.empty() ? nullptr : &rootAssociated);
        candidate->traitCertainty = certainty;
        if (!candidate->isNegative() && rootAssociated.empty()) {
            const auto assocCertainty = matchRootAssociated(trait, *candidate, assocName, candidateAssocType, canonicalAssocParams);
            if (assocCertainty == Certainty::NoSolution) {
                certainty = Certainty::NoSolution;
            } else if (assocCertainty == Certainty::Ambiguous && certainty == Certainty::Proven) {
                certainty = Certainty::Ambiguous;
            }
        }
        candidate->certainty = certainty;
        DEBUG(StringView("next-solver candidate ") << candidate->impl << StringView(" => ") << static_cast<unsigned>(certainty));
        if (candidate->isNegative()) {
            negativeProven |= certainty == Certainty::Proven;
            negativeAmbiguous |= certainty == Certainty::Ambiguous;
            return false;
        }
        suppressAutoBuiltin |= candidate->isPositiveMarkerImpl() && certainty != Certainty::NoSolution;
        if (certainty != Certainty::NoSolution) {
            frame.viable.pushBack(candidate);
        }
        if (!paramEnvCandidateIsNonGlobal(*candidate)) {
            return false;
        }
        if (hasCoercionGoals) {
            if (!coercionSelectsCandidate) {
                return false;
            }
            evaluateCandidateCoercions(*candidate);
            return !candidate->discarded && candidate->coercionsProven && candidate->certainty == Certainty::Proven;
        }
        return candidate->traitCertainty == Certainty::Proven;
    };

    bool provenNonGlobalParamEnv = false;
    for (size_t i = 0; i < candidateCount; i++) {
        if (frame.candidates[i]->source == CandidateSource::ParamEnv) {
            provenNonGlobalParamEnv |= evaluateCandidateAt(i);
        }
    }
    for (size_t i = 0; i < candidateCount; i++) {
        const auto source = frame.candidates[i]->source;
        if (source == CandidateSource::ParamEnv) {
            continue;
        }
        if (!provenNonGlobalParamEnv || source == CandidateSource::AliasBound) {
            evaluateCandidateAt(i);
        }
    }
    if (suppressAutoBuiltin || negativeProven) {
        auto& viable = frame.viable;
        size_t kept = 0;
        for (auto* candidate : viable) {
            if (!candidate->autoBuiltin) {
                viable.mut(kept++) = candidate;
            }
        }
        while (viable.length() > kept) {
            viable.popBack();
        }
    } else if (negativeAmbiguous) {
        for (auto* candidate : frame.viable) {
            if (candidate->autoBuiltin && candidate->certainty == Certainty::Proven) {
                candidate->certainty = Certainty::Ambiguous;
            }
        }
    }

    if (coercionSelectsCandidate) {
        struct RelatedCandidate {
            Candidate* candidate;
            const HIRType* self;
            HIRPathParams inputs;
        };

        ThinVector<RelatedCandidate> related;
        for (auto* candidate : frame.viable) {
            evaluateCandidateCoercions(*candidate);
            auto self = candidate->impl.getImplType(crate.types);
            auto inputs = candidate->impl.getTraitParams(crate.types);
            if (!candidate->discarded) {
                related.push_back(RelatedCandidate{candidate, std::move(self), std::move(inputs)});
            }
        }

        for (size_t i = 0; i < related.size(); i++) {
            for (size_t j = 0; j < related.size(); j++) {
                if (i == j) {
                    continue;
                }
                bool jBetter = false;
                bool iBetter = false;
                for (const auto& constraint : canonicalCoercions) {
                    const auto ordering = resolve_.compareCoercionEndpoints(span(), constraint, constraint.isSelf ? related[j].self : related[j].inputs.types[constraint.typeIndex], constraint.isSelf ? related[i].self : related[i].inputs.types[constraint.typeIndex]);
                    jBetter |= ordering == OrdGreater;
                    iBetter |= ordering == OrdLess;
                }
                if (jBetter && !iBetter) {
                    related[i].candidate->discarded = true;
                    break;
                }
            }
        }
        size_t kept = 0;
        for (auto* candidate : frame.viable) {
            if (!candidate->discarded) {
                frame.viable.mut(kept++) = candidate;
            }
        }
        while (frame.viable.length() > kept) {
            frame.viable.popBack();
        }
    }

    if (frame.viable.empty()) {
        DEBUG(StringView("next-solver: no viable response"));
        return emitNoViable();
    }

    const bool hasNonGlobalParamEnv = std::any_of(frame.viable.begin(), frame.viable.end(), [&](const Candidate* candidate) {
        return paramEnvCandidateIsNonGlobal(*candidate) && candidate->certainty == Certainty::Proven && candidate->coercionsProven;
    });
    if (hasNonGlobalParamEnv) {
        auto& viable = frame.viable;
        size_t kept = 0;
        for (auto* candidate : viable) {
            if (candidate->source == CandidateSource::ParamEnv || candidate->source == CandidateSource::AliasBound) {
                viable.mut(kept++) = candidate;
            }
        }
        while (viable.length() > kept) {
            viable.popBack();
        }
    }

    bool hasPreferredNonImpl = false;
    for (const auto* candidate : frame.viable) {
        hasPreferredNonImpl |= candidate->source != CandidateSource::ParamEnv && isEnvironmentOrBuiltin(candidate->impl) && candidate->certainty == Certainty::Proven;
    }
    if (hasPreferredNonImpl) {
        auto& viable = frame.viable;
        size_t kept = 0;
        for (auto* candidate : viable) {
            if (isEnvironmentOrBuiltin(candidate->impl)) {
                viable.mut(kept++) = candidate;
            }
        }
        while (viable.length() > kept) {
            viable.popBack();
        }
    }

    const bool hasNonParamEnv = std::any_of(frame.viable.begin(), frame.viable.end(), [](const Candidate* candidate) {
        return candidate->source != CandidateSource::ParamEnv;
    });
    if (hasNonParamEnv) {
        auto& viable = frame.viable;
        size_t kept = 0;
        for (auto* candidate : viable) {
            if (candidate->source != CandidateSource::ParamEnv || paramEnvCandidateIsNonGlobal(*candidate)) {
                viable.mut(kept++) = candidate;
            }
        }
        while (viable.length() > kept) {
            viable.popBack();
        }
    }

    const bool hasExactEnvironment = std::any_of(frame.viable.begin(), frame.viable.end(), [&](const Candidate* candidate) {
        return isEnvironmentOrBuiltin(candidate->impl) && candidate->headExact && candidate->certainty == Certainty::Proven;
    });
    if (hasExactEnvironment) {
        auto& viable = frame.viable;
        size_t kept = 0;
        for (auto* candidate : viable) {
            if (!isEnvironmentOrBuiltin(candidate->impl) || candidate->headExact) {
                viable.mut(kept++) = candidate;
            }
        }
        while (viable.length() > kept) {
            viable.popBack();
        }
    }

    for (auto* candidate : frame.viable) {
        candidate->discarded = false;
        candidate->specializationItemSource = nullptr;
    }
    auto recordItemSource = [&](Candidate* winner, const Candidate* shadowed) {
        if (!shadowed->impl.isTraitImpl()) {
            return;
        }
        if (!winner->specializationItemSource || shadowed->impl.moreSpecificThan(crate.types, winner->specializationItemSource->impl)) {
            winner->specializationItemSource = shadowed;
        }
    };
    for (size_t i = 0; i < frame.viable.length(); i++) {
        if (frame.viable[i]->discarded) {
            continue;
        }
        for (size_t j = i + 1; j < frame.viable.length(); j++) {
            if (frame.viable[j]->discarded) {
                continue;
            }
            auto& left = frame.viable[i]->impl;
            auto& right = frame.viable[j]->impl;
            if (responsesEqual(*frame.viable[i], *frame.viable[j], assocName, canonicalAssocParams, valueName)) {
                continue;
            }
            if (!left.isTraitImpl() || !right.isTraitImpl()) {
                continue;
            }
            if (coherenceMode) {
                continue;
            }
            const auto leftType = left.getImplType(crate.types);
            const auto rightType = right.getImplType(crate.types);
            const auto& leftParams = left.getTraitParamsRef(crate.types);
            const auto& rightParams = right.getTraitParamsRef(crate.types);
            const bool sameInstantiatedHead = (leftType == rightType || leftType->equalsIgnoringRegions(rightType)) && leftParams.equalsIgnoringRegions(rightParams);
            if (!sameInstantiatedHead && !resolve_.implsOverlap(span(), left, right)) {
                continue;
            }
            const bool leftMoreSpecific = left.moreSpecificThan(crate.types, right);
            const bool rightMoreSpecific = right.moreSpecificThan(crate.types, left);
            if (leftMoreSpecific) {
                if (!frame.viable[i]->ambiguityBeyondHead) {
                    frame.viable[j]->discarded = true;
                    recordItemSource(frame.viable[i], frame.viable[j]);
                } else if (frame.viable[j]->certainty == Certainty::Proven) {
                    frame.viable[i]->discarded = true;
                }
            } else if (rightMoreSpecific) {
                if (!frame.viable[j]->ambiguityBeyondHead) {
                    frame.viable[i]->discarded = true;
                    recordItemSource(frame.viable[j], frame.viable[i]);
                    break;
                } else if (frame.viable[i]->certainty == Certainty::Proven) {
                    frame.viable[j]->discarded = true;
                }
            }
        }
    }
    {
        size_t kept = 0;
        for (auto* candidate : frame.viable) {
            if (!candidate->discarded) {
                frame.viable.mut(kept++) = candidate;
            }
        }
        while (frame.viable.length() > kept) {
            frame.viable.popBack();
        }
    }

    bool sameResponse = true;
    bool oneResponse = true;
    for (size_t i = 1; i < frame.viable.length(); i++) {
        if (!responsesEqual(*frame.viable[0], *frame.viable[i], assocName, canonicalAssocParams, valueName)) {
            sameResponse = false;
            oneResponse = false;
            break;
        }
    }

    if (oneResponse) {
        Candidate* selected = nullptr;
        for (auto* candidate : frame.viable) {
            if (hasCoercionGoals && !candidate->coercionsProven) {
                continue;
            }
            if (!selected) {
                selected = candidate;
            }
            if (candidate->certainty == Certainty::Proven) {
                selected = candidate;
                break;
            }
        }
        if (!selected) {
            return emitForcedAmbiguity();
        }
        if (assocName && assocName[0]) {
            const HIRPathParams noItemParams;
            const auto& itemParams = canonicalAssocParams ? *canonicalAssocParams : noItemParams;
            for (auto* candidate : frame.viable) {
                if ((!hasCoercionGoals || candidate->coercionsProven) && candidate->certainty == selected->certainty && candidate->impl.getType(crate.types, assocName, itemParams) != nullptr) {
                    selected = candidate;
                    break;
                }
            }
        }
        const auto certainty = selected->certainty;
        DEBUG(StringView("next-solver: applying merged response ") << selected->impl << StringView(" certainty=") << static_cast<unsigned>(certainty));
        if (assocName && assocName[0] && selected->impl.isTraitImpl() && selected->traitCertainty == Certainty::Proven) {
            const HIRPathParams noParams;
            const auto& itemParams = canonicalAssocParams ? *canonicalAssocParams : noParams;
            if (selected->impl.getType(crate.types, assocName, itemParams) == nullptr) {
                for (const Candidate* source = selected->specializationItemSource; source; source = source->specializationItemSource) {
                    const auto* sourceImpl = source->impl.traitImpl;
                    if (!sourceImpl) {
                        break;
                    }
                    const auto it = sourceImpl->types.find(assocName);
                    if (it == sourceImpl->types.end()) {
                        continue;
                    }
                    if (it->second.isSpecialisable) {
                        break;
                    }
                    auto inherited = source->impl.getType(crate.types, assocName, itemParams);
                    if (inherited == nullptr) {
                        break;
                    }
                    auto implType = selected->impl.getImplType(crate.types);
                    auto traitParams = selected->impl.getTraitParams(crate.types);
                    auto sourceTrait = HIRGenericPath(trait, traitParams.clone());
                    HIRTraitPath::assocListT associated;
                    associated.insert({RcString::newInterned(assocName), HIRTraitPath::AtyEqual{std::move(sourceTrait), itemParams.clone(), std::move(inherited)}});
                    return emitResponse(SolverImpl(std::move(implType), std::move(traitParams), std::move(associated)), Certainty::Proven);
                }
            }
        }
        const Candidate* responseSource = selected;
        if (valueName && selected->impl.isTraitImpl()) {
            responseSource = specializationValueSource(selected, valueName);
            if (!responseSource) {
                const auto& traitDef = crate.getTraitByPath(span(), trait);
                const auto valueIt = traitDef.values.find(RcString::newInterned(valueName));
                bool hasDefault = false;
                if (valueIt != traitDef.values.end()) {
                    const auto& value = valueIt->second;
                    switch (value.tag()) {
                        case HIRTraitValueItem::TAG_Constant: {
                            const auto& constant = value.as_Constant();
                            hasDefault = constant.value || constant.valueState != HIRConstant::ValueState::Unknown;
                            break;
                        }
                        case HIRTraitValueItem::TAG_Static: {
                            const auto& staticValue = value.as_Static();
                            hasDefault = staticValue.value || staticValue.value.mir;
                            break;
                        }
                        case HIRTraitValueItem::TAG_Function: {
                            const auto& function = value.as_Function();
                            hasDefault = function.code || function.code.mir;
                            break;
                        }
                    }
                }
                if (!hasDefault) {
                    return false;
                }
                responseSource = selected;
            }
        }
        auto selectedResponse = monomorphCandidateImpl(responseSource->impl, MonomorphiserNop(crate.types));
        if (certainty != Certainty::Proven) {
            const auto* declaredSelf = selected->impl.traitImpl ? selected->impl.traitImpl->type : nullptr;
            const auto selectedSelfHasOpenInference = visitTyWith(selectedResponse.getImplType(crate.types), [](const HIRType* inner) {
                const auto* infer = inner->opt_Infer();
                return infer && !infer->isLit();
            });
            if (selected->nestedAmbiguity && declaredSelf && declaredSelf->is_Generic() && selectedSelfHasOpenInference) {
                return emitForcedAmbiguity();
            }
            return emitResponse(materializeRootAssociated(std::move(selectedResponse), trait, assocName, canonicalAssocParams), Certainty::Ambiguous, selected);
        }
        return emitResponse(materializeRootAssociated(std::move(selectedResponse), trait, assocName, canonicalAssocParams), Certainty::Proven, selected);
    }

    for (auto* candidate : frame.viable) {
        const auto candidateCertainty = candidate->headExact && !candidate->nestedAmbiguity ? Certainty::Proven : Certainty::Ambiguous;
        distinctViable.pushBack({candidate, candidateCertainty});
    }
    auto ambiguous = SolverImpl(resolvedType, goalParams.clone(), HIRTraitPath::assocListT());
    const bool responseProven = sameResponse && std::any_of(frame.viable.begin(), frame.viable.end(), [](const Candidate* candidate) {
        return candidate->certainty == Certainty::Proven;
    });
    return emitResponse(std::move(ambiguous), responseProven ? Certainty::Proven : Certainty::Ambiguous, nullptr, false);
}

auto NextTraitGoalEvaluator::evaluateNormalizesTo(const Span& callSpan, const NormalizesTo& goal, NormalizesToCallback& callback, bool callerBoundary) -> bool {
    const auto* path = goal.projection->opt_Path();
    const auto* projection = path ? path->path.data.opt_UfcsKnown() : nullptr;
    ASSERT_BUG(callSpan, projection, StringView("NormalizesTo goal is not an associated-type projection: ") << goal.projection);

    HIRGenericPath declaringTrait;
    if (!resolve_.traitContainsType(callSpan, projection->trait, crate.getTraitByPath(callSpan, projection->trait.path), projection->item.c_str(), declaringTrait)) {
        BUG(callSpan, StringView("Cannot find associated type ") << projection->item << StringView(" anywhere in trait ") << projection->trait);
    }

    const auto outputSlot = crate.types.infer(HIR_INFER_SOLVER_NORMALIZES_TO_OUTPUT, HIRInferClass::None);
    auto adapter = makeCallable<SolverResponseCb>([&](SolverResponse response) {
        const HIRType* output = nullptr;
        ThinVector<SolverTypeEquality> retainedEqualities;
        for (auto& equality : response.equalities) {
            if (equality.left == outputSlot) {
                output = equality.right;
                continue;
            }
            if (equality.right == outputSlot) {
                output = equality.left;
                continue;
            }
            retainedEqualities.push_back(std::move(equality));
        }
        if (output && output->equalsIgnoringRegions(goal.projection)) {
            output = nullptr;
        }
        if (output == outputSlot) {
            output = nullptr;
        }
        response.equalities = std::move(retainedEqualities);

        SolverSlotValues retainedSlots;
        for (size_t i = 0; i < response.slots.typeInputs.size(); i++) {
            if (response.slots.typeInputs[i] == outputSlot) {
                continue;
            }
            retainedSlots.typeInputs.push_back(response.slots.typeInputs[i]);
            retainedSlots.types.push_back(response.slots.types[i]);
        }
        retainedSlots.valueInputs = std::move(response.slots.valueInputs);
        retainedSlots.values = std::move(response.slots.values);
        response.slots = std::move(retainedSlots);
        return callback.visit(NormalizesToResponse{std::move(response), std::move(output)});
    });
    return evaluateTyped(
        callSpan,
        declaringTrait.path,
        declaringTrait.params,
        projection->type,
        adapter,
        TraitGoalQuery{
            .assocName = projection->item.c_str(),
            .assocType = outputSlot,
            .assocParams = &projection->params,
        },
        callerBoundary
    );
}

NextTraitGoalEvaluator::Candidate::Candidate(SolverImpl impl, bool headExact, Certainty headRelation, const HIRMarkerImpl* markerImpl, HIRPathParams markerImplParams, bool autoBuiltin, CandidateSource source, bool headNormalizationAmbiguity, ThinVector<SolverTypeEquality> headEqualities, ThinVector<SolverValueEquality> headValueEqualities)
    : impl(std::move(impl))
    , headExact(headExact)
    , headRelation(headRelation)
    , certainty(Certainty::Ambiguous)
    , markerImpl(markerImpl)
    , markerImplParams(std::move(markerImplParams))
    , autoBuiltin(autoBuiltin)
    , source(source)
    , headNormalizationAmbiguity(headNormalizationAmbiguity)
    , headEqualities(std::move(headEqualities))
    , headValueEqualities(std::move(headValueEqualities))
{
}

auto NextTraitGoalEvaluator::Candidate::isNegative() const -> bool {
    return markerImpl && !markerImpl->isPositive;
}

auto NextTraitGoalEvaluator::Candidate::isPositiveMarkerImpl() const -> bool {
    return markerImpl && markerImpl->isPositive;
}

NextTraitGoalEvaluator::CandidateFrame::CandidateFrame() {
    candidates.grow(32);
    viable.grow(32);
}

auto NextTraitGoalEvaluator::CandidateFrame::clear(ObjList<Candidate>& nodes) -> void {
    for (auto* candidate : candidates) {
        nodes.release(candidate);
    }
    candidates.clear();
    viable.clear();
    availableDepth = 0;
    encounteredOverflow = false;
}

NextTraitGoalEvaluator::GoalKey::GoalKey(size_t hash, const HIRSimplePath& trait, const HIRPathParams& params, const HIRType* type, const HIRTraitPath::assocListT* associated, const Vector<u32>* existentialEnvironment, bool suppressAmbiguity)
    : hash(hash)
    , trait(trait)
    , params(params.clone())
    , type(type)
    , associated(cloneAssociated(associated))
    , suppressAmbiguity(suppressAmbiguity)
{
    if (existentialEnvironment) {
        for (const auto environmentClass : *existentialEnvironment) {
            this->existentialEnvironment.push_back(environmentClass);
        }
    }
}

NextTraitGoalEvaluator::CachedGoal::CachedGoal(size_t hash, const HIRSimplePath& trait, const HIRPathParams& params, const HIRType* type, const HIRTraitPath::assocListT* associated, Certainty certainty, const Vector<u32>* existentialEnvironment, bool suppressAmbiguity)
    : goal(hash, trait, params, type, associated, existentialEnvironment, suppressAmbiguity)
    , certainty(certainty)
{
}

NextTraitGoalEvaluator::EmptyRootGoal::EmptyRootGoal(size_t hash, const HIRSimplePath& trait, const HIRPathParams& params, const HIRType* type, const Vector<u32>& existentialEnvironment, bool includeMagicCandidates)
    : goal(hash, trait, params, type, nullptr, &existentialEnvironment)
    , includeMagicCandidates(includeMagicCandidates)
{
}

NextTraitGoalEvaluator::RawNestedNoEffectResponse::RawNestedNoEffectResponse(size_t hash, const HIRSimplePath& trait, const HIRPathParams& params, const HIRType* type, const HIRTraitPath::assocListT* associated, const Candidate& candidate, Certainty certainty)
    : goal(hash, trait, params, type, associated)
    , traitImpl(candidate.impl.traitImpl)
    , markerImpl(candidate.markerImpl)
    , candidateParams((candidate.impl.traitImpl ? candidate.impl.implParams : candidate.markerImplParams).clone())
    , certainty(certainty)
{
}

NextTraitGoalEvaluator::CanonicalNestedNoEffectResponse::CanonicalNestedNoEffectResponse(size_t hash, const HIRSimplePath& trait, const HIRPathParams& params, const HIRType* type, const HIRTraitPath::assocListT* associated, const Vector<u32>& existentialEnvironment, const Candidate& candidate, const HIRPathParams& candidateParams, Certainty certainty)
    : goal(hash, trait, params, type, associated, &existentialEnvironment)
    , traitImpl(candidate.impl.traitImpl)
    , markerImpl(candidate.markerImpl)
    , candidateParams(candidateParams.clone())
    , certainty(certainty)
{
}

NextTraitGoalEvaluator::CanonicalGoal::CanonicalGoal(HIRPathParams params, const HIRType* type)
    : params(std::move(params))
    , type(type)
{
}

template <>
void stl::output<ZeroCopyOutput, SolverImpl>(ZeroCopyOutput& os, const SolverImpl& impl) {
    if (impl.traitImpl) {
        os << StringView("impl(") << static_cast<const void*>(impl.traitImpl) << StringView(") ") << impl.traitPath << impl.traitImpl->traitArgs << StringView(" for ") << impl.traitImpl->type << StringView(" ") << impl.implParams;
        return;
    }
    os << StringView("bound ") << impl.type << StringView(" : ?") << impl.traitArgs << StringView(" + {") << impl.associated << StringView("}");
}

template <>
void stl::output<ZeroCopyOutput, TraitResolution::MethodCandidate>(ZeroCopyOutput& os, const TraitResolution::MethodCandidate& candidate) {
    os << StringView("{") << candidate.borrow << StringView(", ") << candidate.path;
    if (candidate.inherentImpl) {
        os << StringView(", inherent");
    }
    os << StringView("}");
}

template <>
void stl::output<ZeroCopyOutput, ThinVector<TraitResolution::MethodCandidate>>(ZeroCopyOutput& out, const ThinVector<TraitResolution::MethodCandidate>& values) {
    outCont(out, values);
}

template <>
void stl::output<ZeroCopyOutput, TraitResolution::AutoderefBorrow>(ZeroCopyOutput& os, TraitResolution::AutoderefBorrow x) {
    switch (x) {
        case TraitResolution::AutoderefBorrow::None:
            os << StringView("None");
            break;
        case TraitResolution::AutoderefBorrow::Shared:
            os << StringView("Shared");
            break;
        case TraitResolution::AutoderefBorrow::Unique:
            os << StringView("Unique");
            break;
        case TraitResolution::AutoderefBorrow::RawShared:
            os << StringView("RawShared");
            break;
        case TraitResolution::AutoderefBorrow::PinShared:
            os << StringView("PinShared");
            break;
        case TraitResolution::AutoderefBorrow::Owned:
            os << StringView("Owned");
            break;
    }
    return;
}

template <>
void stl::output<ZeroCopyOutput, TraitResolution::AllowedReceivers>(ZeroCopyOutput& os, TraitResolution::AllowedReceivers x) {
    switch (x) {
        case TraitResolution::AllowedReceivers::All:
            os << StringView("All");
            break;
        case TraitResolution::AllowedReceivers::AnyBorrow:
            os << StringView("AnyBorrow");
            break;
        case TraitResolution::AllowedReceivers::SharedBorrow:
            os << StringView("SharedBorrow");
            break;
        case TraitResolution::AllowedReceivers::Value:
            os << StringView("Value");
            break;
        case TraitResolution::AllowedReceivers::Box:
            os << StringView("Box");
            break;
    }
    return;
}

template <>
void stl::output<ZeroCopyOutput, TraitResolution::MethodAccess>(ZeroCopyOutput& os, TraitResolution::MethodAccess x) {
    switch (x) {
        case TraitResolution::MethodAccess::Shared:
            os << StringView("Shared");
            break;
        case TraitResolution::MethodAccess::Unique:
            os << StringView("Unique");
            break;
        case TraitResolution::MethodAccess::Move:
            os << StringView("Move");
            break;
    }
    return;
}

template <>
void stl::output<ZeroCopyOutput, HMTypeInferrence::FmtType>(ZeroCopyOutput& os, HMTypeInferrence::FmtType x) {
    x.ctxt.printType(os, x.ty);
    return;
}

template <>
void stl::output<ZeroCopyOutput, HMTypeInferrence::FmtPP>(ZeroCopyOutput& os, HMTypeInferrence::FmtPP x) {
    x.ctxt.printPathparams(os, x.pps);
    return;
}

template <>
void stl::output<ZeroCopyOutput, std::pair<TraitResolution::AutoderefBorrow, HIRPath>>(ZeroCopyOutput& out, const std::pair<TraitResolution::AutoderefBorrow, HIRPath>& value) {
    out << StringView("(") << value.first << StringView(", ") << value.second << StringView(")");
}

template <>
void stl::output<ZeroCopyOutput, std::vector<std::pair<TraitResolution::AutoderefBorrow, HIRPath>>>(ZeroCopyOutput& out, const std::vector<std::pair<TraitResolution::AutoderefBorrow, HIRPath>>& values) {
    outCont(out, values);
}
