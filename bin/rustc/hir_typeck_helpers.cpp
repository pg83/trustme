#include "hir_typeck_helpers.h"

#include "settings.h"
#include "wire_board.h"
#include "hir_inherent_cache.h"
#include "hir_conv_main_bindings.h"
#include "thin_vector.h"
#include "trans_target.h"

#include <std/mem/obj_list.h>
#include <std/mem/obj_pool.h>
#include <std/alg/defer.h>
#include <std/sym/i_map.h>
#include <std/lib/vector.h>
#include <std/rng/split_mix_64.h>

#include <optional>
#include <algorithm>
#include <unordered_map>

SolverImpl SolverImpl::fromLegacy(ImplRef impl) {
    SolverImpl result;
    result.ambiguousIdentity = impl.isAmbiguousIdentity();
    if (auto* traitImpl = impl.data.opt_TraitImpl()) {
        ASSERT_BUG(Span(), traitImpl->traitPtr && traitImpl->impl, "invalid trait impl solver response");
        result.implParams = ::std::move(traitImpl->implParams);
        result.trait = traitImpl->traitPtr;
        ASSERT_BUG(Span(), traitImpl->traitPath, "trait impl solver response has no trait path");
        result.traitPath = *traitImpl->traitPath;
        result.traitImpl = traitImpl->impl;
    } else if (const auto* bounded = impl.data.opt_BoundedPtr()) {
        result.type = bounded->type;
        if (bounded->traitArgs) {
            result.traitArgs = bounded->traitArgs->clone();
        }
        if (bounded->assoc) {
            for (const auto& entry : *bounded->assoc) {
                result.associated.insert({entry.first, entry.second.clone()});
            }
        }
        result.constness = bounded->constness;
    } else {
        auto& owned = impl.data.as_Bounded();
        result.type = ::std::move(owned.type);
        result.traitArgs = ::std::move(owned.traitArgs);
        result.associated = ::std::move(owned.assoc);
        result.constness = owned.constness;
    }
    return result;
}

ImplRef SolverImpl::legacy() const {
    ImplRef result;
    if (traitImpl) {
        ASSERT_BUG(Span(), trait, "trait impl solver response has no trait declaration");
        result = ImplRef(implParams.clone(), *trait, traitPath, *traitImpl);
    } else {
        HIRTraitPath::assocListT assoc;
        for (const auto& entry : associated) {
            assoc.insert({entry.first, entry.second.clone()});
        }
        result = ImplRef(type, traitArgs.clone(), ::std::move(assoc), constness);
    }
    if (ambiguousIdentity) {
        result.markAmbiguousIdentity();
    }
    return result;
}

namespace {
    // Give every fresh placeholder in one active trait goal the same stable
    // spelling.  This makes a recurrence through independently-instantiated
    // blanket impls visible to the solver without changing the goal's actual
    // type data or inference state.
    class CanonicalizeTraitGoal final: public Monomorphiser {
        mutable ::std::vector<::std::pair<RcString, RcString>> placeholderNames_;
        // Unresolved inference variables in the goal, canonicalised
        // positionally so structurally identical goals share one cache key
        // regardless of which caller variables they hold.  The canonical
        // name is derived from the position, so only the node is stored.
        mutable stl::Vector<const HIRTypeData*> ivarNodes_;
        // Unresolved const inference variables, canonicalised positionally
        // into the same reserved index range (value ivars live in their own
        // index space).  Stores the original table index per slot.
        mutable stl::Vector<unsigned> valueIvarIndexes_;
        // Only mappings present when the goal is sealed belong to the input.
        // Placeholders first seen while canonicalising a response are
        // existential and must be freshly instantiated at every boundary.
        mutable size_t inputPlaceholderCount_ = 0;
        // Present only for canonicalizers that fold unresolved inference
        // variables into the reserved solver range; null keeps the legacy
        // placeholder-only behaviour (ivars pass through untouched).
        const HMTypeInferrence* ivarTable_ = nullptr;
        // Once frozen, unknown variables pass through raw instead of
        // claiming new slots; sawForeignIvar_ records that it happened.
        mutable bool frozen_ = false;
        mutable bool sawForeignIvar_ = false;

        RcString canonicalPlaceholderName(const RcString& name) const {
            for (const auto& entry : placeholderNames_) {
                if (entry.first == name) {
                    return entry.second;
                }
                if (entry.second == name) {
                    return name;
                }
            }
            auto canonical = RcString::newInterned(FMT("#solver-placeholder-" << placeholderNames_.size()));
            placeholderNames_.push_back({name, canonical});
            return canonical;
        }

    public:
        explicit CanonicalizeTraitGoal(HIRTypeInterner& types, const HMTypeInferrence* ivarTable = nullptr)
            : Monomorphiser(types)
            , ivarTable_(ivarTable)
        {
        }

        // Canonical variables are interned Infer nodes in a reserved index
        // range: they keep the literal class of the variable they stand for,
        // so impl matching treats them exactly like the caller's variable.
        HIRTypeRef canonicalIvar(const HIRTypeData* infer) const {
            for (size_t i = 0; i < ivarNodes_.length(); i++) {
                if (ivarNodes_[i] == infer) {
                    return types.infer(HIR_INFER_SOLVER_CANONICAL_MIN + static_cast<unsigned>(i), infer->as_Infer().tyClass);
                }
            }
            if (frozen_) {
                if (ivarTable_ && infer->as_Infer().index >= ivarTable_->ivars.size()) {
                    const auto original = RcString::newInterned(FMT("#solver-unowned-type-" << infer->as_Infer().index));
                    return types.generic(canonicalPlaceholderName(original), GENERICPlaceholder * 256);
                }
                // The goal's slots are sealed: a variable beyond them (an
                // environment bound pulled a live caller variable into the
                // response) passes through raw and is reported, so the
                // caller can restrict how the response is cached.
                sawForeignIvar_ = true;
                return infer;
            }
            ivarNodes_.pushBack(infer);
            return types.infer(HIR_INFER_SOLVER_CANONICAL_MIN + static_cast<unsigned>(ivarNodes_.length() - 1), infer->as_Infer().tyClass);
        }

        /// Seal the slot set once the goal key is complete; only responses
        /// are canonicalised afterwards.
        void freeze() const {
            if (!frozen_) {
                inputPlaceholderCount_ = placeholderNames_.size();
            }
            frozen_ = true;
        }

        bool sawForeignIvar() const {
            return sawForeignIvar_;
        }

        const HIRTypeData* originalIvar(unsigned index) const {
            if (!isSolverCanonicalInfer(index)) {
                return nullptr;
            }
            const size_t slot = index - HIR_INFER_SOLVER_CANONICAL_MIN;
            return slot < ivarNodes_.length() ? ivarNodes_[slot] : nullptr;
        }

        HIRConstGeneric canonicalValueIvar(unsigned original) const {
            for (size_t i = 0; i < valueIvarIndexes_.length(); i++) {
                if (valueIvarIndexes_[i] == original) {
                    return HIRConstGeneric::make_Infer({HIR_INFER_SOLVER_CANONICAL_MIN + static_cast<unsigned>(i)});
                }
            }
            if (frozen_) {
                if (ivarTable_ && original >= ivarTable_->values.size()) {
                    const auto name = RcString::newInterned(FMT("#solver-unowned-value-" << original));
                    return HIRConstGeneric(HIRGenericRef(canonicalPlaceholderName(name), GENERICPlaceholder * 256));
                }
                sawForeignIvar_ = true;
                return HIRConstGeneric::make_Infer({original});
            }
            valueIvarIndexes_.pushBack(original);
            return HIRConstGeneric::make_Infer({HIR_INFER_SOLVER_CANONICAL_MIN + static_cast<unsigned>(valueIvarIndexes_.length() - 1)});
        }

        const unsigned* originalValueIvar(unsigned index) const {
            if (!isSolverCanonicalInfer(index)) {
                return nullptr;
            }
            const size_t slot = index - HIR_INFER_SOLVER_CANONICAL_MIN;
            return slot < valueIvarIndexes_.length() ? &valueIvarIndexes_[slot] : nullptr;
        }

        HIRTypeRef monomorphType(const Span& sp, const HIRTypeData* ty, bool allowInfer = true) const override {
            if (ivarTable_ && ty->is_Infer()) {
                const auto& infer = ty->as_Infer();
                // Assembly already runs in this canonicalizer's space.  When
                // the completed response is sealed and canonicalised for
                // storage, its input slots must therefore pass through
                // unchanged.  Treating one as an unowned response variable
                // turns it into an existential placeholder and loses the
                // identity of GAT/item parameters that do not occur in the
                // impl head.
                if (frozen_ && isSolverCanonicalInfer(infer.index) && originalIvar(infer.index)) {
                    return ty;
                }
                const auto* resolved = ivarTable_->getType(ty);
                if (const auto* infer = resolved->opt_Infer()) {
                    // Alias-input placeholders predate the inference table
                    // and stay rigid.  A parent level's canonical variable is
                    // renumbered into THIS level's slots (its node becomes the
                    // "original"): every level then owns all canonical indices
                    // in its goal, so boundary decanonicalisation is exact --
                    // levels sharing raw indices would otherwise alias each
                    // other's slots (all levels intern the same Infer nodes).
                    if (isAliasInputInfer(infer->index) && !isSolverCanonicalInfer(infer->index)) {
                        return resolved;
                    }
                    return canonicalIvar(resolved);
                }
                return monomorphType(sp, resolved, allowInfer);
            }
            if (const auto* path = ty->opt_Path(); path && path->binding.is_Opaque()) {
                // Canonicalisation only renames variables; the path still
                // resolves to the same item, so the Opaque binding stays
                // valid and must survive -- assembly recognises rigid
                // projections (alias-bound self) by it, where the generic
                // monomorphiser would have to drop it for re-resolution.
                auto base = Monomorphiser::monomorphType(sp, ty, allowInfer);
                if (const auto* basePath = base->opt_Path(); basePath && !basePath->binding.is_Opaque()) {
                    return types.intern(HIRTypeData::make_Path({basePath->path.clone(), path->binding.clone()}));
                }
                return base;
            }
            return Monomorphiser::monomorphType(sp, ty, allowInfer);
        }

        HIRTypeRef getType(const Span&, const HIRGenericRef& generic) const override {
            return generic.isPlaceholder() ? types.generic(canonicalPlaceholderName(generic.name), generic.binding) : types.generic(generic.name, generic.binding);
        }

        HIRConstGeneric getValue(const Span&, const HIRGenericRef& generic) const override {
            return HIRConstGeneric(generic.isPlaceholder() ? HIRGenericRef(canonicalPlaceholderName(generic.name), generic.binding) : generic);
        }

        HIRConstGeneric monomorphConstgeneric(const Span& sp, const HIRConstGeneric& val, bool allowInfer) const override {
            if (ivarTable_) {
                if (const auto* infer = val.opt_Infer(); infer && infer->index != ~0u) {
                    // As for type slots above, a canonical value slot already
                    // belonging to the sealed input is not a fresh response
                    // existential.
                    if (frozen_ && isSolverCanonicalInfer(infer->index) && originalValueIvar(infer->index)) {
                        return val.clone();
                    }
                    const auto& resolved = ivarTable_->getValue(val);
                    if (const auto* resolvedInfer = resolved.opt_Infer()) {
                        // Alias-input value placeholders stay rigid; a parent
                        // level's canonical value is renumbered into THIS
                        // level's slots, mirroring the type-ivar rule above.
                        if (isAliasInputInfer(resolvedInfer->index) && !isSolverCanonicalInfer(resolvedInfer->index)) {
                            return resolved.clone();
                        }
                        return canonicalValueIvar(resolvedInfer->index);
                    }
                    // A bound value can still hold generics or nested params;
                    // canonicalise its content through the base walk.
                    return Monomorphiser::monomorphConstgeneric(sp, resolved, allowInfer);
                }
            }
            return Monomorphiser::monomorphConstgeneric(sp, val, allowInfer);
        }

        const ::std::vector<::std::pair<RcString, RcString>>& placeholderNames() const {
            return placeholderNames_;
        }

        const RcString* originalPlaceholderName(const RcString& canonical) const {
            const auto count = frozen_ ? inputPlaceholderCount_ : placeholderNames_.size();
            for (size_t i = 0; i < count; i++) {
                const auto& entry = placeholderNames_[i];
                if (entry.second == canonical) {
                    return &entry.first;
                }
            }
            return nullptr;
        }

        const RcString* originalResponsePlaceholderName(const RcString& canonical) const {
            for (const auto& entry : placeholderNames_) {
                if (entry.second == canonical) {
                    return &entry.first;
                }
            }
            return nullptr;
        }

        const stl::Vector<const HIRTypeData*>& ivarNodes() const {
            return ivarNodes_;
        }

        size_t typeSlotCount() const {
            return ivarNodes_.length();
        }

        size_t valueSlotCount() const {
            return valueIvarIndexes_.length();
        }

        HIRTypeRef canonicalTypeSlot(size_t slot) const {
            ASSERT_BUG(Span(), slot < ivarNodes_.length(), "canonical type slot out of range");
            return types.infer(HIR_INFER_SOLVER_CANONICAL_MIN + static_cast<unsigned>(slot), ivarNodes_[slot]->as_Infer().tyClass);
        }

        HIRConstGeneric canonicalValueSlot(size_t slot) const {
            ASSERT_BUG(Span(), slot < valueIvarIndexes_.length(), "canonical value slot out of range");
            return HIRConstGeneric::make_Infer({HIR_INFER_SOLVER_CANONICAL_MIN + static_cast<unsigned>(slot)});
        }
    };

    class InstantiateCanonicalTraitResponse final: public Monomorphiser {
        const ::std::vector<::std::pair<RcString, RcString>>& goalNames;
        const class CanonicalizeTraitGoal* goalCanonicalizer = nullptr;
        const u64 instance;
        mutable ::std::vector<::std::pair<RcString, RcString>> freshNames;

        RcString instantiatePlaceholderName(const RcString& canonical) const {
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
            auto fresh = RcString(FMT("solver_response_" << instance << "_" << freshNames.size()));
            freshNames.push_back({canonical, fresh});
            return fresh;
        }

    public:
        InstantiateCanonicalTraitResponse(HIRTypeInterner& types, const ::std::vector<::std::pair<RcString, RcString>>& goalNames, u64 instance, const CanonicalizeTraitGoal* goalCanonicalizer = nullptr)
            : Monomorphiser(types)
            , goalNames(goalNames)
            , goalCanonicalizer(goalCanonicalizer)
            , instance(instance)
        {
        }

        HIRTypeRef getType(const Span&, const HIRGenericRef& generic) const override;

        HIRTypeRef monomorphType(const Span& sp, const HIRTypeData* ty, bool allowInfer = true) const override {
            if (goalCanonicalizer) {
                if (const auto* infer = ty->opt_Infer()) {
                    if (const auto* original = goalCanonicalizer->originalIvar(infer->index)) {
                        return original;
                    }
                }
            }
            return Monomorphiser::monomorphType(sp, ty, allowInfer);
        }

        HIRConstGeneric monomorphConstgeneric(const Span& sp, const HIRConstGeneric& val, bool allowInfer) const override {
            if (goalCanonicalizer) {
                if (const auto* infer = val.opt_Infer()) {
                    if (const auto* original = goalCanonicalizer->originalValueIvar(infer->index)) {
                        return HIRConstGeneric::make_Infer({*original});
                    }
                }
            }
            return Monomorphiser::monomorphConstgeneric(sp, val, allowInfer);
        }

        HIRConstGeneric getValue(const Span&, const HIRGenericRef& generic) const override {
            return HIRConstGeneric(generic.isPlaceholder() ? HIRGenericRef(instantiatePlaceholderName(generic.name), generic.binding) : generic);
        }
    };

    HIRTypeRef InstantiateCanonicalTraitResponse::getType(const Span&, const HIRGenericRef& generic) const {
        return types.generic(generic.isPlaceholder() ? instantiatePlaceholderName(generic.name) : generic.name, generic.binding);
    }

    // Canonical query variables created while evaluating a goal are
    // existential.  They must be instantiated as fresh variables in the
    // caller's inference table before a root response leaves the solver.
    // Placeholders already present in the input goal are universal and stay
    // unchanged.
    class InstantiateTraitResponseForCaller final: public Monomorphiser {
        HMTypeInferrence& ivars;
        const ::std::vector<::std::pair<RcString, RcString>>& goalNames;
        const CanonicalizeTraitGoal* goalCanonicalizer = nullptr;
        mutable ::std::vector<::std::pair<HIRGenericRef, HIRTypeRef>> typeValues;
        mutable ::std::vector<::std::pair<HIRGenericRef, HIRConstGeneric>> values;

        bool isGoalPlaceholder(const HIRGenericRef& generic) const {
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

    public:
        InstantiateTraitResponseForCaller(HIRTypeInterner& types, HMTypeInferrence& ivars, const ::std::vector<::std::pair<RcString, RcString>>& goalNames, const CanonicalizeTraitGoal* goalCanonicalizer = nullptr)
            : Monomorphiser(types)
            , ivars(ivars)
            , goalNames(goalNames)
            , goalCanonicalizer(goalCanonicalizer)
        {
        }

        HIRTypeRef monomorphType(const Span& sp, const HIRTypeData* ty, bool allowInfer = true) const override {
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

        HIRConstGeneric monomorphConstgeneric(const Span& sp, const HIRConstGeneric& val, bool allowInfer) const override {
            if (goalCanonicalizer) {
                if (const auto* infer = val.opt_Infer()) {
                    if (const auto* original = goalCanonicalizer->originalValueIvar(infer->index)) {
                        return HIRConstGeneric::make_Infer({*original});
                    }
                }
            }
            return Monomorphiser::monomorphConstgeneric(sp, val, allowInfer);
        }

        // Assembly runs in the canonical space, so a response names goal
        // placeholders by their canonical spelling: translate those back to
        // the caller's names before deciding what is existential.
        HIRGenericRef callerGeneric(const HIRGenericRef& generic) const {
            if (generic.isPlaceholder() && goalCanonicalizer) {
                if (const auto* original = goalCanonicalizer->originalResponsePlaceholderName(generic.name)) {
                    return HIRGenericRef(*original, generic.binding);
                }
            }
            return generic;
        }

        HIRTypeRef getType(const Span&, const HIRGenericRef& raw) const override {
            const auto generic = callerGeneric(raw);
            if (!generic.isPlaceholder() || isGoalPlaceholder(generic)) {
                return Monomorphiser::types.generic(generic.name, generic.binding);
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

        HIRConstGeneric getValue(const Span&, const HIRGenericRef& raw) const override {
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
    };

    // Legacy/read-only consumers can observe an implementation head but have
    // nowhere to apply the response's explicit slot assignments.  Present
    // response existentials as the input variables they refine, preserving
    // the correlation without mutating the inference table.
    class CorrelateSolverResponseSlots final: public MonomorphiserNop {
        const SolverSlotValues& slots_;
        stl::Vector<::std::pair<HIRTypeRef, HIRTypeRef>> structuralTypes_;

        void correlateParams(const HIRPathParams& input, const HIRPathParams& response) {
            if (input.types.size() != response.types.size()) {
                return;
            }
            for (size_t i = 0; i < input.types.size(); i++) {
                correlateType(input.types[i], response.types[i]);
            }
        }

        void correlateGenericPath(const HIRGenericPath& input, const HIRGenericPath& response) {
            if (input.path == response.path) {
                correlateParams(input.params, response.params);
            }
        }

        void correlatePath(const HIRPath& input, const HIRPath& response) {
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

    public:
        CorrelateSolverResponseSlots(HIRTypeInterner& interner, const SolverSlotValues& slots)
            : MonomorphiserNop(interner)
            , slots_(slots)
        {
        }

        void correlateType(const HIRTypeData* input, const HIRTypeData* response) {
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
                if (!right || left->size() != right->size()) {
                    return;
                }
                for (size_t i = 0; i < left->size(); i++) {
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
                if (!right || left->argTypes.size() != right->argTypes.size()) {
                    return;
                }
                for (size_t i = 0; i < left->argTypes.size(); i++) {
                    correlateType(left->argTypes[i], right->argTypes[i]);
                }
                correlateType(left->rettype, right->rettype);
            }
        }

        void correlateImpl(const ImplRef& input, const SolverImpl& response) {
            auto responseImpl = response.legacy();
            correlateType(input.getImplType(types), responseImpl.getImplType(types));
            correlateParams(input.getTraitParams(types), responseImpl.getTraitParams(types));
        }

        HIRTypeRef monomorphType(const Span& sp, const HIRTypeData* type, bool allowInfer = true) const override {
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

        HIRConstGeneric monomorphConstgeneric(const Span& sp, const HIRConstGeneric& value, bool allowInfer) const override {
            for (size_t i = 0; i < slots_.values.size(); i++) {
                if (slots_.values[i] == value && slots_.valueInputs[i] != value) {
                    return slots_.valueInputs[i].clone();
                }
            }
            return MonomorphiserNop::monomorphConstgeneric(sp, value, allowInfer);
        }
    };

    // Maps one level's canonical solver variables and placeholder spellings
    // back to what they stood for, so a response crossing a nested goal
    // boundary never leaks that level's canonical names into the parent.
    class DecanonicalizeSolverInfers final: public MonomorphiserNop {
        const CanonicalizeTraitGoal& canonicalizer_;

    public:
        DecanonicalizeSolverInfers(HIRTypeInterner& types, const CanonicalizeTraitGoal& canonicalizer)
            : MonomorphiserNop(types)
            , canonicalizer_(canonicalizer)
        {
        }

        HIRTypeRef monomorphType(const Span& sp, const HIRTypeData* ty, bool allowInfer = true) const override {
            if (const auto* infer = ty->opt_Infer()) {
                if (const auto* original = canonicalizer_.originalIvar(infer->index)) {
                    return original;
                }
                return ty;
            }
            if (const auto* path = ty->opt_Path(); path && path->binding.is_Opaque()) {
                // Only names change here; the resolution is intact, so the
                // Opaque binding survives for the parent's assembly.
                auto base = MonomorphiserNop::monomorphType(sp, ty, allowInfer);
                if (const auto* basePath = base->opt_Path(); basePath && !basePath->binding.is_Opaque()) {
                    return types.intern(HIRTypeData::make_Path({basePath->path.clone(), path->binding.clone()}));
                }
                return base;
            }
            return MonomorphiserNop::monomorphType(sp, ty, allowInfer);
        }

        HIRTypeRef getType(const Span&, const HIRGenericRef& generic) const override {
            if (generic.isPlaceholder()) {
                if (const auto* original = canonicalizer_.originalPlaceholderName(generic.name)) {
                    return types.generic(*original, generic.binding);
                }
            }
            return types.generic(generic.name, generic.binding);
        }

        HIRConstGeneric monomorphConstgeneric(const Span& sp, const HIRConstGeneric& val, bool allowInfer) const override {
            if (const auto* infer = val.opt_Infer()) {
                if (const auto* original = canonicalizer_.originalValueIvar(infer->index)) {
                    return HIRConstGeneric::make_Infer({*original});
                }
            }
            return MonomorphiserNop::monomorphConstgeneric(sp, val, allowInfer);
        }

        HIRConstGeneric getValue(const Span&, const HIRGenericRef& generic) const override {
            if (generic.isPlaceholder()) {
                if (const auto* original = canonicalizer_.originalPlaceholderName(generic.name)) {
                    return HIRConstGeneric(HIRGenericRef(*original, generic.binding));
                }
            }
            return HIRConstGeneric(generic);
        }
    };

}

// --------------------------------------------------------------------
// HMTypeInferrence
// --------------------------------------------------------------------
void HMTypeInferrence::checkForLoops() {
    struct LoopChecker {
        stl::Vector<unsigned int>& indexes;

        void checkTy(const HMTypeInferrence& ivars, const HIRTypeData* ty) {
            visitTyWith(ty, [&](const HIRTypeData* t) {
                if (const auto* ep = t->opt_Infer()) {
                    const auto& e = *ep;
                    for (auto idx : indexes) {
                        ASSERT_BUG(Span(), e.index != idx, "Recursion in ivar #" << indexes[0] << " " << ivars.ivars[indexes[0]].type << " - loop with " << idx << " " << ivars.ivars[idx].type);
                    }
                    const auto& ivd = ivars.getPointedIvar(e.index);
                    assert(!ivd.isAlias());
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

    stl::Vector<unsigned int> indexes;
    unsigned int i = 0;
    for (const auto& v : ivars) {
        if (!v.isAlias() && !v.type->is_Infer()) {
            indexes.clear();
            indexes.pushBack(i);
            (LoopChecker{indexes}).checkTy(*this, v.type);
        }
        i++;
    }
}

void HMTypeInferrence::compactIvars() {
    // Compaction rewrites stored types in place, which the journal does not
    // model; it must never run while a probe could still roll back.
    ASSERT_BUG(Span(), snapshotDepth == 0, "ivar compaction during an active inference snapshot");
    this->checkForLoops();

    unsigned int i = 0;
    for (auto& v : ivars) {
        if (!v.isAlias()) {
            auto old = v.type;
            this->expandIvars(v.type);
        } else {
            auto index = v.alias;
            unsigned int count = 0;
            assert(index < ivars.size());
            while (ivars.at(index).isAlias()) {
                index = ivars.at(index).alias;

                if (count >= ivars.size()) {
                    BUG(Span(), "Loop detected in ivar list when starting at " << v.alias << ", current is " << index);
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
            this->journalMutation(JournalEntry::Kind::TypeSet, index, v.type);
            v.type = types.primitive(HIRCoreType::I32);
            return true;
        case HIRInferClass::Float:
            this->journalMutation(JournalEntry::Kind::TypeSet, index, v.type);
            v.type = types.primitive(HIRCoreType::F64);
            return true;
    }
    return false;
}

void HMTypeInferrence::printType(::std::ostream& os, const HIRTypeData* tr, LList<const HIRTypeData*> outerStack) const {
    const auto& ty = this->getType(tr);
    for (const auto* pty : outerStack) {
        if (pty) {
            if (pty == ty) {
                os << "/*RECURSE*/";
                return;
            }
        }
    }
    auto stack = LList<const HIRTypeData*>(&outerStack, ty);

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
                os << "<";
                this->printType(os, pe.type, stack);
                os << " as ";
                this->printGenericpath(os, pe.trait, stack);
                os << ">::" << pe.item;
                this->printPathparams(os, pe.params, stack);
                break;
            }
            case HIRPathData::TAG_UfcsInherent: {
                auto& pe = path.data.as_UfcsInherent();
                os << "<";
                this->printType(os, pe.type, stack);
                os << ">::" << pe.item;
                this->printPathparams(os, pe.params, stack);
                break;
            }
            case HIRPathData::TAG_UfcsUnknown: {
                BUG(Span(), "UfcsUnknown");
                break;
            }
        }
    };

    switch ((*ty).tag()) {
        case HIRTypeData::TAG_Infer: {
            os << ty;
            break;
        }
        case HIRTypeData::TAG_Primitive: {
            os << ty;
            break;
        }
        case HIRTypeData::TAG_Diverge: {
            os << ty;
            break;
        }
        case HIRTypeData::TAG_Generic: {
            os << ty;
            break;
        }
        case HIRTypeData::TAG_Path: {
            auto& e = (*ty).as_Path();
            printPath(e.path);
            break;
        }
        case HIRTypeData::TAG_Borrow: {
            auto& e = (*ty).as_Borrow();
            os << "&";
            switch (e.type) {
                case HIRBorrowType::Shared:
                    os << "";
                    break;
                case HIRBorrowType::Unique:
                    os << "mut ";
                    break;
                case HIRBorrowType::Owned:
                    os << "move ";
                    break;
            }
            this->printType(os, e.inner, stack);
            break;
        }
        case HIRTypeData::TAG_Pointer: {
            auto& e = (*ty).as_Pointer();
            switch (e.type) {
                case HIRBorrowType::Shared:
                    os << "*const ";
                    break;
                case HIRBorrowType::Unique:
                    os << "*mut ";
                    break;
                case HIRBorrowType::Owned:
                    os << "*move ";
                    break;
            }
            this->printType(os, e.inner, stack);
            break;
        }
        case HIRTypeData::TAG_Slice: {
            auto& e = (*ty).as_Slice();
            os << "[";
            this->printType(os, e.inner, stack);
            os << "]";
            break;
        }
        case HIRTypeData::TAG_Array: {
            auto& e = (*ty).as_Array();
            os << "[";
            this->printType(os, e.inner, stack);
            os << "; " << e.size << "]";
            break;
        }
        case HIRTypeData::TAG_Pattern: {
            auto& e = (*ty).as_Pattern();
            this->printType(os, e.inner, stack);
            os << " is ";
            e.pattern.fmt(os);
            break;
        }
        case HIRTypeData::TAG_NodeType: {
            auto& e = (*ty).as_NodeType();
            e.fmt(os);
            switch (e.tag()) {
                case HIRTypeDataNodeType::TAG_Closure: {
                    auto& nodeP = e.as_Closure();
                    os << "(";
                    for (const auto& arg : nodeP->args) {
                        this->printType(os, arg.second, stack);
                        os << ",";
                    }
                    os << ")->";
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
        case HIRTypeData::TAG_NamedFunction: {
            auto& e = (*ty).as_NamedFunction();
            os << "fn{";
            printPath(e.path);
            os << "}";
            break;
        }
        case HIRTypeData::TAG_Function: {
            auto& e = (*ty).as_Function();
            if (e.isUnsafe) {
                os << "unsafe ";
            }
            if (e.abi != "") {
                os << "extern \"" << e.abi << "\" ";
            }
            os << "fn(";
            for (const auto& arg : e.argTypes) {
                this->printType(os, arg, stack);
                os << ",";
            }
            os << ")->";
            this->printType(os, e.rettype, stack);
            break;
        }
        case HIRTypeData::TAG_TraitObject: {
            auto& e = (*ty).as_TraitObject();
            os << "dyn (";
            printTraitpath(e.trait);
            for (const auto& marker : e.markers) {
                os << "+";
                this->printGenericpath(os, marker, stack);
            }
            os << ")";
            break;
        }
        case HIRTypeData::TAG_ErasedType: {
            auto& e = (*ty).as_ErasedType();
            os << "impl ";
                for (const auto& tr : e.traits) {
                    if (&tr != &e.traits[0]) {
                        os << "+";
                    }
                    printTraitpath(tr);
                }
                os << "+use";
                this->printPathparams(os, e.use, outerStack);
                os << "/*";
            switch (e.inner.tag()) {
                case TypeDataErasedTypeInner::TAG_Fcn: {
                    auto& ee = e.inner.as_Fcn();
                    os << "fn ";
                    printPath(ee.origin);
                    os << "#" << ee.index;
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
            os << "*/";
            break;
        }
        case HIRTypeData::TAG_Tuple: {
            auto& e = (*ty).as_Tuple();
            os << "(";
            for (const auto& st : e) {
                this->printType(os, st, stack);
                os << ",";
            }
            os << ")";
            break;
        }
    }
}

void HMTypeInferrence::printGenericpath(::std::ostream& os, const HIRGenericPath& gp, LList<const HIRTypeData*> stack) const {
    os << gp.path;
    this->printPathparams(os, gp.params, stack);
}

void HMTypeInferrence::printPathparams(::std::ostream& os, const HIRPathParams& pps, LList<const HIRTypeData*> stack) const {
    if (pps.hasParams()) {
        os << "<";
        for (const auto& ppT : pps.types) {
            this->printType(os, ppT, stack);
            os << ",";
        }
        for (const auto& ppV : pps.values) {
            os << ppV;
            os << ",";
        }
        os << ">";
    }
}

void HMTypeInferrence::expandIvars(HIRTypeRef& type) {
    if (!type->hasTypeInfer()) {
        return;
    }
    if (::std::find(expandStack.begin(), expandStack.end(), type) != expandStack.end()) {
        return;
    }
    expandStack.push_back(type);

    STD_DEFER {
        expandStack.pop_back();
    };

    if (type->is_Infer()) {
        const auto& resolved = this->getType(type);
        if (resolved != type) {
            type = resolved;
        }
        return;
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
                    self.expandIvars(e2.type); self.expandIvarsParams(e2.trait.params); self.expandIvarsParams(e2.params);
                    break;
                }
                case HIRPath::Data::TAG_UfcsUnknown: {
                    auto& e2 = path.data.as_UfcsUnknown();
                    self.expandIvars(e2.type); self.expandIvarsParams(e2.params);
                    break;
                }
                case HIRPath::Data::TAG_UfcsInherent: {
                    auto& e2 = path.data.as_UfcsInherent();
                    self.expandIvars(e2.type); self.expandIvarsParams(e2.params);
                    break;
                }
            }
        }
    };

    switch (data.tag()) {
        case HIRTypeData::TAG_Infer: {
            break;
        }
        case HIRTypeData::TAG_Diverge: {
            break;
        }
        case HIRTypeData::TAG_Primitive: {
            break;
        }
        case HIRTypeData::TAG_Path: {
            auto& e = data.as_Path();
            // Iterate all arguments
            H::expandIvarsPath(*this, e.path);
            break;
        }
        case HIRTypeData::TAG_Generic: {
            break;
        }
        case HIRTypeData::TAG_TraitObject: {
            auto& e = data.as_TraitObject();
            this->expandIvarsTraitPath(e.trait);
            for (auto& marker : e.markers) {
                this->expandIvarsParams(marker.params);
            }
            break;
        }
        case HIRTypeData::TAG_ErasedType: {
            auto& e = data.as_ErasedType();
            switch (e.inner.tag()) {
                case TypeDataErasedTypeInner::TAG_Fcn: {
                    auto& ee = e.inner.as_Fcn();
                    H::expandIvarsPath(*this, ee.origin);
                    break;
                }
                case TypeDataErasedTypeInner::TAG_Known: {
                    auto& ee = e.inner.as_Known();
                    this->expandIvars(ee);
                    break;
                }
                case TypeDataErasedTypeInner::TAG_Alias: {
                    break;
                }
            }
            for(auto& trait : e.traits)
            {
                    this->expandIvarsParams(trait.path.params);
                    // TODO: Associated types
            }
            break;
        }
        case HIRTypeData::TAG_Array: {
            auto& e = data.as_Array();
            this->expandIvars(e.inner);
            break;
        }
        case HIRTypeData::TAG_Slice: {
            auto& e = data.as_Slice();
            this->expandIvars(e.inner);
            break;
        }
        case HIRTypeData::TAG_Pattern: {
            auto& e = data.as_Pattern();
            this->expandIvars(e.inner);
            for (auto& range : e.pattern.alternatives) {
                HIRConstGeneric* values[] = {range.hasStart ? &range.start : nullptr, range.hasEnd ? &range.end : nullptr};
                for (auto* value : values) {
                    if (value && value->is_Infer()) {
                        const auto& resolved = this->getValue(*value);
                        if (!resolved.is_Infer()) *value = resolved.clone();
                    }
                }
            }
            break;
        }
        case HIRTypeData::TAG_Tuple: {
            auto& e = data.as_Tuple();
            for (auto& ty : e) {
                this->expandIvars(ty);
            }
            break;
        }
        case HIRTypeData::TAG_Borrow: {
            auto& e = data.as_Borrow();
            this->expandIvars(e.inner);
            break;
        }
        case HIRTypeData::TAG_Pointer: {
            auto& e = data.as_Pointer();
            this->expandIvars(e.inner);
            break;
        }
        case HIRTypeData::TAG_NamedFunction: {
            auto& e = data.as_NamedFunction();
            H::expandIvarsPath(*this, e.path);
            break;
        }
        case HIRTypeData::TAG_Function: {
            auto& e = data.as_Function();
            this->expandIvars(e.rettype);
            for (auto& ty : e.argTypes) {
                this->expandIvars(ty);
            }
            break;
        }
        case HIRTypeData::TAG_NodeType: {
            break;
        }
    }
    type = types.intern(std::move(data));
}

void HMTypeInferrence::expandIvarsParams(HIRPathParams& params) {
    for (auto& arg : params.types) {
        expandIvars(arg);
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
            expandIvars((*unevaluated)->selfType);
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
        expandIvars(bound.second.type);
    }
    for (auto& bound : path.traitBounds) {
        expandIvarsParams(bound.second.sourceTrait.params);
        expandIvarsParams(bound.second.atyParams);
        for (auto& trait : bound.second.traits) {
            expandIvarsTraitPath(trait);
        }
    }
}

void HMTypeInferrence::addIvars(HIRTypeRef& type) {
    if (const auto* infer = type->opt_Infer()) {
        if (infer->index == ~0u) {
            type = newIvarTr(infer->tyClass);
            this->markChange();
            return;
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
            return;
        }
    }

    auto data = type->cloneData();
    switch (data.tag()) {
        case HIRTypeData::TAG_Infer: {
            break;
        }
        case HIRTypeData::TAG_Diverge: {
            break;
        }
        case HIRTypeData::TAG_Primitive: {
            break;
        }
        case HIRTypeData::TAG_Path: {
            auto& e = data.as_Path();
            // Iterate all arguments
            switch (e.path.data.tag()) {
                case HIRPath::Data::TAG_Generic: {
                    auto& e2 = e.path.data.as_Generic();
                    this->addIvarsParams(e2.params);
                    break;
                }
                case HIRPath::Data::TAG_UfcsKnown: {
                    auto& e2 = e.path.data.as_UfcsKnown();
                    this->addIvars(e2.type); this->addIvarsParams(e2.trait.params); this->addIvarsParams(e2.params);
                    break;
                }
                case HIRPath::Data::TAG_UfcsUnknown: {
                    auto& e2 = e.path.data.as_UfcsUnknown();
                    this->addIvars(e2.type); this->addIvarsParams(e2.params);
                    break;
                }
                case HIRPath::Data::TAG_UfcsInherent: {
                    auto& e2 = e.path.data.as_UfcsInherent();
                    this->addIvars(e2.type); this->addIvarsParams(e2.params);
                    break;
                }
            }
            break;
        }
        case HIRTypeData::TAG_Generic: {
            break;
        }
        case HIRTypeData::TAG_TraitObject: {
            auto& e = data.as_TraitObject();
            // Iterate all paths
            this->addIvarsTraitPath(e.trait);
            for (auto& marker : e.markers) {
                this->addIvarsParams(marker.params);
            }
            break;
        }
        case HIRTypeData::TAG_ErasedType: {
            if (typeContainsIvars(type, /*only_unbound=*/true)) {
                BUG(Span(), "ErasedType getting ivars added - " << type);
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
        case HIRTypeData::TAG_Array: {
            auto& e = data.as_Array();
            addIvars(e.inner);
            if (e.size.is_Unevaluated()) {
                addIvars(e.size.as_Unevaluated());
            }
            break;
        }
        case HIRTypeData::TAG_Slice: {
            auto& e = data.as_Slice();
            addIvars(e.inner);
            break;
        }
        case HIRTypeData::TAG_Pattern: {
            auto& e = data.as_Pattern();
            addIvars(e.inner);
            for (auto& range : e.pattern.alternatives) {
                if (range.hasStart) addIvars(range.start);
                if (range.hasEnd) addIvars(range.end);
            }
            break;
        }
        case HIRTypeData::TAG_Tuple: {
            auto& e = data.as_Tuple();
            for (auto& ty : e) {
                addIvars(ty);
            }
            break;
        }
        case HIRTypeData::TAG_Borrow: {
            auto& e = data.as_Borrow();
            addIvars(e.inner);
            break;
        }
        case HIRTypeData::TAG_Pointer: {
            auto& e = data.as_Pointer();
            addIvars(e.inner);
            break;
        }
        case HIRTypeData::TAG_NamedFunction: {
            // Shouldn't be possible?
            // Even if it is seen, it shouldn't have any empty ivars
            break;
        }
        case HIRTypeData::TAG_Function: {
            auto& e = data.as_Function();
            addIvars(e.rettype);
            for (auto& ty : e.argTypes) {
                addIvars(ty);
            }
            break;
        }
        case HIRTypeData::TAG_NodeType: {
            // Shouldn't be possible
            break;
        }
    }
    type = types.intern(std::move(data));
}

void HMTypeInferrence::addIvars(HIRConstGeneric& val) {
    if (val.is_Infer()) {
        if (val.as_Infer().index == ~0u) {
            val.as_Infer().index = newIvarVal();
            this->markChange();
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
        addIvars(arg);
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
    // A cache built while this slot exists must not survive a rollback that
    // removes it.  The monotonic counter ensures that reusing the same table
    // index later cannot make that cache look current again.
    mutationGeneration = ++generationCounter;
    return rv;
}

HIRTypeRef HMTypeInferrence::newIvarTr(HIRInferClass ic /* = HIR::InferClass::None*/) {
    return ivars.at(this->newIvar(ic)).type;
}

unsigned int HMTypeInferrence::newIvarVal() {
    values.push_back(IVarValue());
    values.back().val->as_Infer().index = values.size() - 1;
    mutationGeneration = ++generationCounter;
    return values.size() - 1;
}

void HMTypeInferrence::setIvarValTo(unsigned int slot, HIRConstGeneric val) {
    ASSERT_BUG(Span(), slot < values.size(), "slot " << slot << " >= " << values.size());
    ASSERT_BUG(Span(), !values[slot].isAlias(), "slot " << slot);
    if (*values[slot].val == val) {
    } else {
        ASSERT_BUG(Span(), values[slot].val->is_Infer(), "slot " << slot << " - " << *values[slot].val);
        ASSERT_BUG(Span(), values[slot].val->as_Infer().index == slot, "slot " << slot << " - " << *values[slot].val);
        this->journalMutation(JournalEntry::Kind::ValSet, slot, nullptr);
        *values[slot].val = std::move(val);
        // The warm goal cache keys on this generation; a const-value
        // binding invalidates it just like a type binding does.
        this->markChange();
    }
}

void HMTypeInferrence::ivarValUnify(unsigned int leftSlot, unsigned int rightSlot) {
    Span sp;
    ASSERT_BUG(sp, leftSlot < values.size(), "slot " << leftSlot << " >= " << values.size());
    ASSERT_BUG(sp, rightSlot < values.size(), "slot " << leftSlot << " >= " << values.size());
    ASSERT_BUG(sp, !values[leftSlot].isAlias(), "slot " << leftSlot);
    ASSERT_BUG(sp, !values[rightSlot].isAlias(), "slot " << rightSlot);

    if (leftSlot == rightSlot) {
        return;
    }

    if (/*const auto* re =*/values[rightSlot].val->opt_Infer()) {
        values[rightSlot].alias = leftSlot;
        if (snapshotDepth != 0) {
            // Keep the Infer value alive so rollback only clears the alias.
            this->journalMutation(JournalEntry::Kind::ValAlias, rightSlot, nullptr);
        } else {
            values[rightSlot].val.reset();
        }

        this->markChange();
    } else {
        BUG(sp, "Unifiying over a set value");
    }
}

//::HIR::ASTType*& HMTypeInferrence::get_type(::HIR::ASTType*& type)
//{
//    }
//    }
//}

const HIRTypeData* HMTypeInferrence::getType(const HIRTypeData* type) const {
    const auto* current = &type;
    for (size_t count = 0; count <= ivars.size(); count++) {
        const auto* e = (*current)->opt_Infer();
        if (!e) {
            return *current;
        }
        ASSERT_BUG(Span(), e->index != ~0u, "Encountered non-populated IVar");
        if (isAliasInputInfer(e->index)) {
            return *current;
        }

        const auto* next = &getPointedIvar(e->index).type;
        if (*next == *current) {
            return *current;
        }
        current = next;
    }
    BUG(Span(), "Loop detected while resolving type " << type);
}

HIRTypeRef& HMTypeInferrence::getType(unsigned idx) {
    assert(idx != ~0u);
    auto* current = &getPointedIvar(idx).type;
    for (size_t count = 0; count <= ivars.size(); count++) {
        const auto* e = (*current)->opt_Infer();
        if (!e) {
            return *current;
        }
        auto* next = &getPointedIvar(e->index).type;
        if (*next == *current) {
            return *current;
        }
        current = next;
    }
    BUG(Span(), "Loop detected while resolving type ivar " << idx);
}

const HIRTypeData* HMTypeInferrence::getType(unsigned idx) const {
    assert(idx != ~0u);
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
    BUG(Span(), "Loop detected while resolving type ivar " << idx);
}

void HMTypeInferrence::setIvarTo(unsigned int slot, HIRTypeRef type) {
    auto sp = Span();
    const auto rootIndex = this->rootIvarIndex(slot);
    auto& rootIvar = ivars.at(rootIndex);

    // If the left type was '_', alias the right to it
    if (const auto* lE = type->opt_Infer(); lE && !isAliasInputInfer(lE->index)) {
        assert(lE->index != slot);
        if (lE->tyClass != HIRInferClass::None) {
            switch ((*rootIvar.type).tag()) {
                case HIRTypeData::TAG_Primitive: {
                    auto& e = (*rootIvar.type).as_Primitive();
                    checkTypeClassPrimitive(sp, type, lE->tyClass, e);
                    break;
                }
                case HIRTypeData::TAG_Pattern: {
                    auto& e = (*rootIvar.type).as_Pattern();
                    const auto* primitive = e.inner->opt_Primitive(); if (!primitive) { ERROR(sp, E0000, "Type unificiation of literal with invalid pattern type - " << rootIvar.type); } checkTypeClassPrimitive(sp, type, lE->tyClass, *primitive);
                    break;
                }
                case HIRTypeData::TAG_Infer: {
                    auto& e = (*rootIvar.type).as_Infer();
                    // Check for right having a ty_class
                    if (e.tyClass != HIRInferClass::None && e.tyClass != lE->tyClass) { ERROR(sp, E0000, "Unifying types with mismatching literal classes - " << type << " := " << rootIvar.type); }
                    break;
                }
                default: {
                    ERROR(sp, E0000, "Type unificiation of literal with invalid type - " << rootIvar.type);
                    break;
                }
            }
        }

        // Alias `l_e.index` to this slot
        const auto rightIndex = this->rootIvarIndex(lE->index);
        auto& rIvar = ivars.at(rightIndex);
        this->journalMutation(JournalEntry::Kind::TypeAlias, rightIndex, rIvar.type);
        rIvar.alias = slot;
        rIvar.type = nullptr;
    } else if (rootIvar.type == type) {
        return;
    } else {
        // Erase (replace with blank) lifetimes
        // TODO: Avoid needing to clone in all cases?
        struct MonomorphAddLifetimes: public Monomorphiser {
            explicit MonomorphAddLifetimes(HIRTypeInterner& types)
                : Monomorphiser(types)
            {
            }

            HIRTypeRef getType(const Span& sp, const HIRGenericRef& g) const override {
                return types.generic(g.name, g.binding);
            }

            HIRConstGeneric getValue(const Span& sp, const HIRGenericRef& g) const override {
                return g;
            }
        };

        type = MonomorphAddLifetimes(types).monomorphType(sp, type, true);

        // Otherwise, store left in right's slot
        if (const auto* e = rootIvar.type->opt_Infer()) {
            switch (e->tyClass) {
                case HIRInferClass::None:
                    break;
                case HIRInferClass::Integer:
                case HIRInferClass::Float:
                    // `type` can't be an ivar, so it has to be a primitive (or an associated?)
                    if (const auto* lE = type->opt_Primitive()) {
                        checkTypeClassPrimitive(sp, type, e->tyClass, *lE);
                    } else if (const auto* pattern = type->opt_Pattern()) {
                        const auto* primitive = pattern->inner->opt_Primitive();
                        if (!primitive) {
                            BUG(sp, "Setting primitive to " << type);
                        }
                        checkTypeClassPrimitive(sp, type, e->tyClass, *primitive);
                    } else if (type->is_Diverge()) {
                        // ... acceptable
                    } else {
                        BUG(sp, "Setting primitive to " << type);
                    }
                    break;
            }
        } else {
            BUG(sp, "Overwriting ivar " << slot << " (" << rootIvar.type << ") with " << type);
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

            if (re->tyClass != HIRInferClass::None) {
                if (const auto* le = leftIvar.type->opt_Infer()) {
                    if (le->tyClass != HIRInferClass::None && le->tyClass != re->tyClass) {
                        ERROR(sp, E0000, "Unifying types with mismatching literal classes - " << leftIvar.type << " := " << rootIvar.type);
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
                        ERROR(sp, E0000, "Type unificiation of literal with invalid pattern type - " << leftIvar.type);
                    }
                    checkTypeClassPrimitive(sp, leftIvar.type, re->tyClass, *primitive);
                } else {
                    ERROR(sp, E0000, "Type unificiation of literal with invalid type - " << leftIvar.type);
                }
            } else {
            }
        } else {
            BUG(sp, "Unifying over a concrete type - " << rootIvar.type);
        }

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
    ASSERT_BUG(Span(), slot != ~0u, "HMTypeInferrence::get_value: Value generic ivar index not assigned");
    auto index = slot;
    // Limit the iteration count to the number of ivars
    for (unsigned int count = 0; count < values.size(); count++) {
        ASSERT_BUG(Span(), index < values.size(), "");
        auto& ent = values[index];
        if (!ent.isAlias()) {
            return *ent.val;
        }
        index = ent.alias;
    }
    BUG(Span(), "Loop detected in ivar list when starting at " << slot << ", current is " << index);
}

unsigned int HMTypeInferrence::rootIvarIndex(unsigned int slot) const {
    auto index = slot;
    unsigned int count = 0;
    assert(index < ivars.size());
    while (ivars.at(index).isAlias()) {
        index = ivars.at(index).alias;

        if (count >= ivars.size()) {
            BUG(Span(), "Loop detected in ivar list when starting at " << slot << ", current is " << index);
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

bool HMTypeInferrence::typeContainsIvars(const HIRTypeData* ty, bool onlyUnbound) const {
    if (!ty->hasTypeInfer()) {
        return false;
    }
    auto pathContainsIvars = [this](const HIRPath& path, bool onlyUnbound) {
        switch (path.data.tag()) {
            case HIRPath::Data::TAG_Generic: {
                auto& pe = path.data.as_Generic();
                return this->pathparamsContainIvars(pe.params, onlyUnbound);
            }
            case HIRPath::Data::TAG_UfcsKnown: {
                auto& pe = path.data.as_UfcsKnown();
                if (this->typeContainsIvars(pe.type, onlyUnbound)) return true; if (this->pathparamsContainIvars(pe.trait.params, onlyUnbound)) return true; return this->pathparamsContainIvars(pe.params, onlyUnbound);
                break;
            }
            case HIRPath::Data::TAG_UfcsInherent: {
                auto& pe = path.data.as_UfcsInherent();
                if (this->typeContainsIvars(pe.type, onlyUnbound)) return true; return this->pathparamsContainIvars(pe.params, onlyUnbound);
                break;
            }
            case HIRPath::Data::TAG_UfcsUnknown: {
                BUG(Span(), "UfcsUnknown");
                break;
            }
        }
        UNREACHABLE();
    };
    //TU_MATCH(::HIR::TypeData, (this->get_type(ty).m_data), (e),
    switch ((*ty).tag()) {
        case HIRTypeData::TAG_Infer: {
            auto& e = (*ty).as_Infer();
            if( onlyUnbound ) {
            return e.index == ~0u;
            }
            return true;
        }
        case HIRTypeData::TAG_Primitive: {
            return false;
        }
        case HIRTypeData::TAG_Diverge: {
            return false;
        }
        case HIRTypeData::TAG_Generic: {
            return false;
        }
        case HIRTypeData::TAG_Path: {
            auto& e = (*ty).as_Path();
            return pathContainsIvars(e.path, onlyUnbound);
        }
        case HIRTypeData::TAG_Borrow: {
            auto& e = (*ty).as_Borrow();
            return typeContainsIvars(e.inner, onlyUnbound);
        }
        case HIRTypeData::TAG_Pointer: {
            auto& e = (*ty).as_Pointer();
            return typeContainsIvars(e.inner, onlyUnbound);
        }
        case HIRTypeData::TAG_Slice: {
            auto& e = (*ty).as_Slice();
            return typeContainsIvars(e.inner, onlyUnbound);
        }
        case HIRTypeData::TAG_Pattern: {
            auto& e = (*ty).as_Pattern();
            return typeContainsIvars(e.inner, onlyUnbound);
        }
        case HIRTypeData::TAG_Array: {
            auto& e = (*ty).as_Array();
            return typeContainsIvars(e.inner, onlyUnbound);
        }
        case HIRTypeData::TAG_NodeType: {
            return false;
        }
        case HIRTypeData::TAG_NamedFunction: {
            auto& e = (*ty).as_NamedFunction();
            return pathContainsIvars(e.path, onlyUnbound);
        }
        case HIRTypeData::TAG_Function: {
            auto& e = (*ty).as_Function();
            for(const auto& arg : e.argTypes)
                if( typeContainsIvars(arg, onlyUnbound) )
                    return true;
            return typeContainsIvars(e.rettype, onlyUnbound);
        }
        case HIRTypeData::TAG_TraitObject: {
            auto& e = (*ty).as_TraitObject();
            for(const auto& marker : e.markers)
                if( pathparamsContainIvars(marker.params, onlyUnbound) )
                    return true;
            return pathparamsContainIvars(e.trait.path.params, onlyUnbound);
        }
        case HIRTypeData::TAG_ErasedType: {
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
        case HIRTypeData::TAG_Tuple: {
            auto& e = (*ty).as_Tuple();
            for(const auto& st : e)
                if( typeContainsIvars(st, onlyUnbound) )
                    return true;
            return false;
        }
    }
    UNREACHABLE();
        }

        namespace {
            bool typeListEqual(const HMTypeInferrence& context, const ::std::vector<HIRTypeRef>& l, const ::std::vector<HIRTypeRef>& r) {
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

            bool typeListEqual(const HMTypeInferrence& context, const ThinVector<HIRTypeRef>& l, const ThinVector<HIRTypeRef>& r) {
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
        }

        bool HMTypeInferrence::pathparamsEqual(const HIRPathParams& ppsL, const HIRPathParams& ppsR) const {
            return typeListEqual(*this, ppsL.types, ppsR.types);
        }

        bool HMTypeInferrence::typesEqual(const HIRTypeData* rl, const HIRTypeData* rr) const {
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
                            if (lpe.path != rpe.path) return false; return self.pathparamsEqual(lpe.params, rpe.params);
                            break;
                        }
                        case HIRPath::Data::TAG_UfcsKnown: {
                            auto& lpe = l.data.as_UfcsKnown();
                            auto& rpe = r.data.as_UfcsKnown();
                            if (lpe.item != rpe.item) return false; if (!self.typesEqual(lpe.type, rpe.type)) return false; if (!self.pathparamsEqual(lpe.trait.params, rpe.trait.params)) return false; return self.pathparamsEqual(lpe.params, rpe.params);
                            break;
                        }
                        case HIRPath::Data::TAG_UfcsInherent: {
                            auto& lpe = l.data.as_UfcsInherent();
                            auto& rpe = r.data.as_UfcsInherent();
                            if (lpe.item != rpe.item) return false; if (!self.typesEqual(lpe.type, rpe.type)) return false; return self.pathparamsEqual(lpe.params, rpe.params);
                            break;
                        }
                        case HIRPath::Data::TAG_UfcsUnknown: {
                            BUG(Span(), "UfcsUnknown");
                            break;
                        }
                    }
                    UNREACHABLE();
                }
            };

    switch ((*l).tag()) {
        case HIRTypeData::TAG_Infer: {
            auto& le = (*l).as_Infer();
            auto& re = (*r).as_Infer();
            return le.index == re.index;
        }
        case HIRTypeData::TAG_Primitive: {
            auto& le = (*l).as_Primitive();
            auto& re = (*r).as_Primitive();
            return le == re;
        }
        case HIRTypeData::TAG_Diverge: {
            return true;
        }
        case HIRTypeData::TAG_Generic: {
            auto& le = (*l).as_Generic();
            auto& re = (*r).as_Generic();
            return le.binding == re.binding;
        }
        case HIRTypeData::TAG_Path: {
            auto& le = (*l).as_Path();
            auto& re = (*r).as_Path();
            return H::comparePath(*this, le.path, re.path);
        }
        case HIRTypeData::TAG_Borrow: {
            auto& le = (*l).as_Borrow();
            auto& re = (*r).as_Borrow();
            if( le.type != re.type )
                return false;
            return typesEqual(le.inner, re.inner);
        }
        case HIRTypeData::TAG_Pointer: {
            auto& le = (*l).as_Pointer();
            auto& re = (*r).as_Pointer();
            if( le.type != re.type )
                return false;
            return typesEqual(le.inner, re.inner);
        }
        case HIRTypeData::TAG_Slice: {
            auto& le = (*l).as_Slice();
            auto& re = (*r).as_Slice();
            return typesEqual(le.inner, re.inner);
        }
        case HIRTypeData::TAG_Pattern: {
            auto& le = (*l).as_Pattern();
            auto& re = (*r).as_Pattern();
            return le.pattern.ord(re.pattern) == OrdEqual && typesEqual(le.inner, re.inner);
        }
        case HIRTypeData::TAG_Array: {
            auto& le = (*l).as_Array();
            auto& re = (*r).as_Array();
            if( le.size != re.size )
                return false;
            return typesEqual(le.inner, re.inner);
        }
        case HIRTypeData::TAG_NodeType: {
            auto& le = (*l).as_NodeType();
            auto& re = (*r).as_NodeType();
            return le == re;
        }
        case HIRTypeData::TAG_NamedFunction: {
            auto& le = (*l).as_NamedFunction();
            auto& re = (*r).as_NamedFunction();
            return H::comparePath(*this, le.path, re.path);
        }
        case HIRTypeData::TAG_Function: {
            auto& le = (*l).as_Function();
            auto& re = (*r).as_Function();
            if( le.isUnsafe != re.isUnsafe || le.abi != re.abi || le.isVariadic != re.isVariadic || le.trackCaller != re.trackCaller )
                return false;
            if( !typeListEqual(*this, le.argTypes, re.argTypes) )
                return false;
            return typesEqual(le.rettype, re.rettype);
        }
        case HIRTypeData::TAG_TraitObject: {
            auto& le = (*l).as_TraitObject();
            auto& re = (*r).as_TraitObject();
            if( le.markers.size() != re.markers.size() )
                return false;
            for(unsigned int i = 0; i < le.markers.size(); i ++) {
            const auto& lm = le.markers[i];
            const auto& rm = re.markers[i];
            if (lm.path != rm.path) {
                return false;
            }
            if (!pathparamsEqual(lm.params, rm.params)) {
                return false;
            }
            }
            if( le.trait.path.path != re.trait.path.path )
                return false;
            return pathparamsEqual(le.trait.path.params, re.trait.path.params);
        }
        case HIRTypeData::TAG_ErasedType: {
            auto& le = (*l).as_ErasedType();
            auto& re = (*r).as_ErasedType();
            if( le.inner.tag() != re.inner.tag() )
                return false;
            switch (le.inner.tag()) {
                case TypeDataErasedTypeInner::TAG_Fcn: {
                    auto& l = le.inner.as_Fcn();
                    auto& r = re.inner.as_Fcn();
                    ASSERT_BUG(Span(), l.origin != HIRSimplePath(), "Erased type with unset origin");
                    ASSERT_BUG(Span(), r.origin != HIRSimplePath(), "Erased type with unset origin");
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
        case HIRTypeData::TAG_Tuple: {
            auto& le = (*l).as_Tuple();
            auto& re = (*r).as_Tuple();
            return typeListEqual(*this, le, re);
        }
    }
    UNREACHABLE();
        }

        // --------------------------------------------------------------------
        // Unifier
        // --------------------------------------------------------------------

        bool HMTypeInferrence::containsLiveIvar(const HIRTypeData* type, unsigned int rootIndex) const {
            const auto* resolved = this->getType(type);
            return visitTyWith(resolved, [&](const HIRTypeData* inner) {
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

        namespace {
            bool inferIsLive(const HIRTypeData* type) {
                const auto* infer = type->opt_Infer();
                return infer && infer->index != ~0u && !isAliasInputInfer(infer->index);
            }

            /// An unresolved projection may still normalise to anything: it
            /// can neither prove nor refute an equality here.  Erased opaque
            /// types are nominal and rigid outside their defining scope; the
            /// defining-scope caller handles reveal before invoking Unifier.
            bool typeIsRigidUnknown(const HIRTypeData* type) {
                if (const auto* path = type->opt_Path()) {
                    if (!path->path.data.is_Generic()) {
                        return true;
                    }
                    return path->binding.is_Unbound() || path->binding.is_Opaque();
                }
                return false;
            }

            bool literalClassAccepts(const HMTypeInferrence& table, HIRInferClass tyClass, const HIRTypeData* type) {
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
        }

        Unifier::Unifier(const Span& sp, HMTypeInferrence& table, const TraitResolution* resolve, bool bindRigidValues)
            : sp_(sp)
            , table_(table)
            , resolve_(resolve)
            , bindRigidValues_(bindRigidValues)
        {
        }

        bool Unifier::opaqueCanReveal(const HIRTypeData* type) const {
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

        Unifier::Outcome Unifier::defer(const HIRTypeData* left, const HIRTypeData* right) {
            pending_.pushBack(PendingEquality{left, right});
            return Outcome::Ambiguous;
        }

        Unifier::Outcome Unifier::unify(const HIRTypeData* left, const HIRTypeData* right) {
            const auto snapshot = table_.snapshot();
            const auto pendingBefore = pending_.length();
            const auto pendingValuesBefore = pendingValues_.size();
            const auto outcome = this->unifyResolved(left, right);
            if (outcome == Outcome::Mismatch) {
                table_.rollbackTo(snapshot);
                while (pending_.length() > pendingBefore) {
                    pending_.popBack();
                }
                while (pendingValues_.size() > pendingValuesBefore) {
                    pendingValues_.pop_back();
                }
            } else {
                table_.commit(snapshot);
            }
            return outcome == Outcome::Mismatch
                ? Outcome::Mismatch
                : pending_.empty() && pendingValues_.empty() ? Outcome::Proven : Outcome::Ambiguous;
        }

        Unifier::Outcome Unifier::unifyValues(const HIRConstGeneric& left, const HIRConstGeneric& right) {
            const auto snapshot = table_.snapshot();
            const auto pendingValuesBefore = pendingValues_.size();
            const auto outcome = this->unifyValuesResolved(left, right);
            if (outcome == Outcome::Mismatch) {
                table_.rollbackTo(snapshot);
                while (pendingValues_.size() > pendingValuesBefore) {
                    pendingValues_.pop_back();
                }
            } else {
                table_.commit(snapshot);
            }
            return outcome == Outcome::Mismatch
                ? Outcome::Mismatch
                : pending_.empty() && pendingValues_.empty() ? Outcome::Proven : Outcome::Ambiguous;
        }

        Unifier::Outcome Unifier::unifyResolved(const HIRTypeData* leftRaw, const HIRTypeData* rightRaw) {
            const auto* left = table_.getType(leftRaw);
            const auto* right = table_.getType(rightRaw);
            if (left == right) {
                return Outcome::Proven;
            }
            // Canonicalization can rebuild the same rigid projection with a
            // different interned node. It is still an exact equality; only a
            // genuinely different rigid alias must remain deferred.
            if (left->equalsIgnoringRegions(right)) {
                return Outcome::Proven;
            }

            const bool leftLive = inferIsLive(left);
            const bool rightLive = inferIsLive(right);
            if (leftLive && rightLive) {
                const auto leftClass = left->as_Infer().tyClass;
                const auto rightClass = right->as_Infer().tyClass;
                if (leftClass != HIRInferClass::None && rightClass != HIRInferClass::None && leftClass != rightClass) {
                    return Outcome::Mismatch;
                }
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
                    // A projection may normalise to a primitive in this
                    // literal class.  Defer the alias relation first; the
                    // normalised output is checked against the class later.
                    return this->defer(left, right);
                }
                if (!literalClassAccepts(table_, infer->as_Infer().tyClass, other)) {
                    return Outcome::Mismatch;
                }
                const auto rootIndex = table_.rootIvarIndex(infer->as_Infer().index);
                if (table_.containsLiveIvar(other, rootIndex)) {
                    return Outcome::Mismatch;
                }
                table_.setIvarTo(rootIndex, other);
                return Outcome::Proven;
            }
            if (left->is_Infer() || right->is_Infer()) {
                if ((left->is_Infer() && typeIsRigidUnknown(right))
                    || (right->is_Infer() && typeIsRigidUnknown(left))) {
                    return this->defer(left, right);
                }
                // Rigid unknowns: canonical variables, alias inputs, or a
                // still-unassigned wildcard.  A canonical literal slot is
                // unknown only within its literal class: `_/*i*/` can match
                // `usize`, but cannot make `&usize` a viable impl head.
                auto rigidInferAccepts = [&](const HIRTypeData* inferType, const HIRTypeData* other) {
                    const auto tyClass = inferType->as_Infer().tyClass;
                    if (tyClass == HIRInferClass::None) {
                        return true;
                    }
                    if (const auto* otherInfer = other->opt_Infer()) {
                        return otherInfer->tyClass == HIRInferClass::None || otherInfer->tyClass == tyClass;
                    }
                    return literalClassAccepts(table_, tyClass, other);
                };
                if ((left->is_Infer() && !rigidInferAccepts(left, right))
                    || (right->is_Infer() && !rigidInferAccepts(right, left))) {
                    return Outcome::Mismatch;
                }
                return this->defer(left, right);
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
                case HIRTypeData::TAG_Infer: {
                    UNREACHABLE();
                }
                case HIRTypeData::TAG_Primitive:
                case HIRTypeData::TAG_Diverge:
                case HIRTypeData::TAG_NodeType: {
                    // Interned: equality is pointer identity, checked above.
                    return Outcome::Mismatch;
                }
                case HIRTypeData::TAG_Generic: {
                    if (left->as_Generic().isPlaceholder() || right->as_Generic().isPlaceholder()) {
                        return this->defer(left, right);
                    }
                    return Outcome::Mismatch;
                }
                case HIRTypeData::TAG_Path: {
                    // Rigid-unknown paths (any UFCS form, unbound or opaque
                    // bindings) were deferred above; both sides are nominal.
                    const auto& le = left->as_Path().path.data.as_Generic();
                    const auto& re = right->as_Path().path.data.as_Generic();
                    if (le.path != re.path) {
                        return Outcome::Mismatch;
                    }
                    return this->unifyParams(le.params, re.params);
                }
                case HIRTypeData::TAG_Borrow: {
                    const auto& le = left->as_Borrow();
                    const auto& re = right->as_Borrow();
                    if (le.type != re.type) {
                        return Outcome::Mismatch;
                    }
                    return this->unifyResolved(le.inner, re.inner);
                }
                case HIRTypeData::TAG_Pointer: {
                    const auto& le = left->as_Pointer();
                    const auto& re = right->as_Pointer();
                    if (le.type != re.type) {
                        return Outcome::Mismatch;
                    }
                    return this->unifyResolved(le.inner, re.inner);
                }
                case HIRTypeData::TAG_Slice: {
                    return this->unifyResolved(left->as_Slice().inner, right->as_Slice().inner);
                }
                case HIRTypeData::TAG_Array: {
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
                case HIRTypeData::TAG_Pattern: {
                    const auto& le = left->as_Pattern();
                    const auto& re = right->as_Pattern();
                    if (le.pattern.ord(re.pattern) != OrdEqual) {
                        return Outcome::Mismatch;
                    }
                    return this->unifyResolved(le.inner, re.inner);
                }
                case HIRTypeData::TAG_Tuple: {
                    const auto& le = left->as_Tuple();
                    const auto& re = right->as_Tuple();
                    if (le.size() != re.size()) {
                        return Outcome::Mismatch;
                    }
                    for (size_t i = 0; i < le.size(); i++) {
                        if (this->unifyResolved(le[i], re[i]) == Outcome::Mismatch) {
                            return Outcome::Mismatch;
                        }
                    }
                    return Outcome::Proven;
                }
                case HIRTypeData::TAG_Function: {
                    const auto& le = left->as_Function();
                    const auto& re = right->as_Function();
                    if (le.isUnsafe != re.isUnsafe || le.abi != re.abi || le.isVariadic != re.isVariadic || le.trackCaller != re.trackCaller || le.argTypes.size() != re.argTypes.size()) {
                        return Outcome::Mismatch;
                    }
                    for (size_t i = 0; i < le.argTypes.size(); i++) {
                        if (this->unifyResolved(le.argTypes[i], re.argTypes[i]) == Outcome::Mismatch) {
                            return Outcome::Mismatch;
                        }
                    }
                    return this->unifyResolved(le.rettype, re.rettype);
                }
                case HIRTypeData::TAG_NamedFunction: {
                    // Distinct fn items never unify, but comparing their
                    // paths structurally is not implemented here yet.
                    return this->defer(left, right);
                }
                case HIRTypeData::TAG_TraitObject: {
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
                        // Associated-type bounds carry their own structure;
                        // defer the whole object equality for now.
                        return this->defer(left, right);
                    }
                    return this->unifyParams(le.trait.path.params, re.trait.path.params);
                }
                case HIRTypeData::TAG_ErasedType: {
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
                    return this->typeContainsLiveValueIvar(data.type, rootIndex)
                        || this->paramsContainLiveValueIvar(data.trait.params, rootIndex)
                        || this->paramsContainLiveValueIvar(data.params, rootIndex);
                }
                case HIRPathData::TAG_UfcsInherent: {
                    const auto& data = path.data.as_UfcsInherent();
                    return this->typeContainsLiveValueIvar(data.type, rootIndex)
                        || this->paramsContainLiveValueIvar(data.params, rootIndex)
                        || this->paramsContainLiveValueIvar(data.implParams, rootIndex);
                }
                case HIRPathData::TAG_UfcsUnknown: {
                    const auto& data = path.data.as_UfcsUnknown();
                    return this->typeContainsLiveValueIvar(data.type, rootIndex)
                        || this->paramsContainLiveValueIvar(data.params, rootIndex);
                }
            }
            UNREACHABLE();
        }

        bool Unifier::traitPathContainsLiveValueIvar(const HIRTraitPath& path, unsigned rootIndex) const {
            if (this->paramsContainLiveValueIvar(path.path.params, rootIndex)) {
                return true;
            }
            for (const auto& bound : path.typeBounds) {
                if (this->paramsContainLiveValueIvar(bound.second.sourceTrait.params, rootIndex)
                    || this->paramsContainLiveValueIvar(bound.second.atyParams, rootIndex)
                    || this->typeContainsLiveValueIvar(bound.second.type, rootIndex)) {
                    return true;
                }
            }
            for (const auto& bound : path.traitBounds) {
                if (this->paramsContainLiveValueIvar(bound.second.sourceTrait.params, rootIndex)
                    || this->paramsContainLiveValueIvar(bound.second.atyParams, rootIndex)) {
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

        bool Unifier::typeContainsLiveValueIvar(const HIRTypeData* type, unsigned rootIndex) const {
            return visitTyWith(table_.getType(type), [&](const HIRTypeData* inner) {
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
                    return array->size.is_Unevaluated()
                        && this->valueContainsLiveIvar(array->size.as_Unevaluated(), rootIndex);
                }
                if (const auto* pattern = inner->opt_Pattern()) {
                    for (const auto& range : pattern->pattern.alternatives) {
                        if ((range.hasStart && this->valueContainsLiveIvar(range.start, rootIndex))
                            || (range.hasEnd && this->valueContainsLiveIvar(range.end, rootIndex))) {
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
            return (data.selfType && this->typeContainsLiveValueIvar(data.selfType, rootIndex))
                || this->paramsContainLiveValueIvar(data.paramsImpl, rootIndex)
                || this->paramsContainLiveValueIvar(data.paramsItem, rootIndex);
        }

        Unifier::Outcome Unifier::unifyValuesResolved(const HIRConstGeneric& leftRaw, const HIRConstGeneric& rightRaw) {
            const auto& left = table_.getValue(leftRaw);
            const auto& right = table_.getValue(rightRaw);
            if (left == right) {
                return Outcome::Proven;
            }

            const auto liveIndex = [&](const HIRConstGeneric& value) -> ::std::optional<unsigned> {
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
                table_.ivarValUnify(*leftLive, *rightLive);
                return Outcome::Proven;
            }
            if (leftLive || rightLive) {
                const auto& other = leftLive ? right : left;
                const auto* rigidInfer = other.opt_Infer();
                if ((bindRigidValues_ && (other.is_Generic() || rigidInfer))
                    || other.is_Evaluated()
                    || (other.is_Generic() && !other.as_Generic().isPlaceholder())
                    || (rigidInfer && rigidInfer->index != ~0u && isAliasInputInfer(rigidInfer->index))
                    || (other.is_Unevaluated() && !this->valueContainsLiveIvar(other, leftLive ? *leftLive : *rightLive))) {
                    table_.setIvarValTo(leftLive ? *leftLive : *rightLive, other.clone());
                    return Outcome::Proven;
                }
                // Placeholders, unevaluated expressions and rigid unknowns.
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
            if ((left.is_Generic() && !left.as_Generic().isPlaceholder() && right.is_Evaluated())
                || (right.is_Generic() && !right.as_Generic().isPlaceholder() && left.is_Evaluated())) {
                return Outcome::Mismatch;
            }
            // Unevaluated expressions or placeholder/value mixtures.
            return deferValue();
        }

        // --------------------------------------------------------------------
        // TraitResolution
        // --------------------------------------------------------------------

        namespace {
            HIRCompare compareValue(const Span& sp, const HIRConstGeneric& leftRaw, const HIRConstGeneric& rightRaw, const HMTypeInferrence& infer) {
                // The guarded overload keeps reserved-range indexes (alias
                // inputs and solver-canonical value slots) rigid instead of
                // asserting on the table bounds.
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
                //TODO(sp, "compare_value: " << left << " == " << right);
                return HIRCompare::Unequal;
            }
        }

        HIRCompare TraitResolution::comparePp(const Span& sp, const HIRPathParams& left, const HIRPathParams& right) const {
            ASSERT_BUG(sp, left.types.size() == right.types.size(), "Parameter count mismatch - `" << left << "` vs `" << right << "`");
            ASSERT_BUG(sp, left.values.size() == right.values.size(), "Parameter count mismatch - `" << left << "` vs `" << right << "`");
            HIRCompare ord = HIRCompare::Equal;
            for (unsigned int i = 0; i < left.types.size(); i++) {
                // TODO: Should allow fuzzy matches using placeholders (match_test_generics_fuzz works for that)
                // - Better solution is to remove the placeholders in method searching.
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

        // -------------------------------------------------------------------------------------------------------------------
        // -------------------------------------------------------------------------------------------------------------------
        bool TraitResolution::iterateBoundsTraitsCb(const Span& sp, const HIRTypeData* type, const HIRSimplePath& trait, TraitBoundCallback& cb) const {
            for (const auto& b : traitBounds) {
                if (b.first.second.path != trait) {
                    continue;
                }
                const HIRTypeData* boundType = b.first.first;
                auto cmp = boundType->compareWithPlaceholders(sp, type, this->ivars.callbackResolveInfer());
                HIRTypeRef normalizedBound;
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

        bool TraitResolution::iterateBoundsTraitsCb(const Span& sp, const HIRTypeData* type, TraitBoundCallback& cb) const {
            for (const auto& b : traitBounds) {
                const HIRTypeData* boundType = b.first.first;
                auto cmp = boundType->compareWithPlaceholders(sp, type, this->ivars.callbackResolveInfer());
                HIRTypeRef normalizedBound;
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
            if (!this->traitContainsType(sp, pe.trait, this->crate.getTraitByPath(sp, pe.trait.path), pe.item.c_str(), traitPath)) {
                BUG(sp, "Cannot find associated type " << pe.item << " anywhere in trait " << pe.trait);
            }
            const auto& traitRef = crate.getTraitByPath(sp, traitPath.path);
            const auto& atyDef = traitRef.types.find(pe.item)->second;

            for (const auto& bound : atyDef.traitBounds) {
                if (cb.visit(bound)) {
                    return true;
                }
            }
            // Search `<Self as Trait>::Name` bounds on the trait itself
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

        bool TraitResolution::assembleMagicCandidatesCb(const Span& sp, const HIRSimplePath& trait, const HIRPathParams& params, const HIRTypeData* ty, TraitImplCallback& callback) const {
            const auto langCoerceUnsized = this->crate.getLangItemPathOpt("coerce_unsized");
            const auto langFnPtr = this->crate.getLangItemPathOpt("fn_ptr_trait");
            const auto langTuple = this->crate.getLangItemPathOpt("tuple_trait");
            const auto langTransmute = this->crate.getLangItemPathOpt("transmute_trait");

            const auto& type = this->ivars.getType(ty);

            if (trait == langSized()) {
                // As with Copy below, candidate assembly must not solve the
                // same goal recursively. Declared Sized bounds have already
                // been assembled; this branch contributes only the builtin
                // structural candidate.
                auto cmp = typeIsSizedBuiltin(sp, type);
                if (cmp != HIRCompare::Unequal) {
                    return callback.visit(ImplRef(type, nullptr, nullptr), cmp);
                } else {
                    return false;
                }
            }

            if (trait == langCopy()) {
                // Candidate assembly is already the declared-impl search.
                // Calling the public typeIsCopy here would solve the same
                // Copy goal recursively and turn the active-cycle ambiguity
                // into a spurious builtin candidate for nominal types.
                auto cmp = this->typeIsCopyBuiltin(sp, type);
                if (cmp != HIRCompare::Unequal) {
                    auto impl = ImplRef(type, nullptr, nullptr);
                    if (cmp == HIRCompare::Fuzzy) {
                        impl.markAmbiguousIdentity();
                    }
                    return callback.visit(std::move(impl), cmp);
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
                if (!assume.relocations.empty() || assume.bytes.size() != 4) {
                    return false;
                }
                StaticTraitResolve targetResolve(this->board());
                if (TargetTypesAreTransmutable(
                        sp,
                        targetResolve,
                        sourceType,
                        type,
                        assume.bytes[0] != 0,
                        assume.bytes[1] != 0,
                        assume.bytes[2] != 0,
                        assume.bytes[3] != 0)) {
                    return callback.visit(ImplRef(type, params.clone(), {}), HIRCompare::Equal);
                }
                return false;
            }

            if (!langFnPtr.components().empty() && trait == langFnPtr) {
                if (type->is_Function()) {
                    return callback.visit(ImplRef(type, nullptr, nullptr), HIRCompare::Equal);
                }
            }

            if (trait == langClone() && (type->is_Tuple() || type->is_Array() || type->is_Function() || type->is_NodeType() || type->is_NamedFunction() || ((*type).is_Path() && ((*type).as_Path().isClosure())))) {
                auto cmp = this->typeIsClone(sp, type);
                if (cmp != HIRCompare::Unequal) {
                    return callback.visit(ImplRef(type, nullptr, nullptr), cmp);
                } else {
                    return false;
                }
            }

            // - `DiscriminantKind`
            if (!langDiscriminantKind().components().empty() && trait == langDiscriminantKind()) {
                const auto nameDiscriminant = RcString::newInterned("Discriminant");
                // TODO: This logic is near identical to the logic in `static.cpp` - can it be de-duplicated?

                if (type->is_Infer() || (type->is_Path() && type->as_Path().binding.is_Unbound())) {
                    // TODO: How to prevent EAT from expanding (or setting opaque) too early?
                    return callback.visit(ImplRef(type, HIRPathParams(), HIRTraitPath::assocListT()), HIRCompare::Fuzzy);
                } else if (type->is_Generic() || (type->is_Path() && type->as_Path().binding.is_Opaque())) {
                    HIRTraitPath::assocListT assocList;
                    assocList.insert(std::make_pair(nameDiscriminant, HIRTraitPath::AtyEqual{trait, {}, crate.types.path(HIRPath(type, trait.clone(), nameDiscriminant), HIRTypePathBinding::make_Opaque({}))}));
                    return callback.visit(ImplRef(type, HIRPathParams(), HIRTraitPath::assocListT()), HIRCompare::Equal);
                } else if (type->is_Path() && type->as_Path().binding.is_Enum()) {
                    const auto& enm = *type->as_Path().binding.as_Enum();
                    HIRTypeRef tagTy = crate.types.primitive(enm.getReprType(enm.tagRepr));
                    HIRTraitPath::assocListT assocList;
                    assocList.insert(std::make_pair(nameDiscriminant, HIRTraitPath::AtyEqual{trait, {}, std::move(tagTy)}));
                    return callback.visit(ImplRef(type, {}, std::move(assocList)), HIRCompare::Equal);
                } else if ((type->is_NodeType() && (type->as_NodeType().is_Generator() || type->as_NodeType().is_Async()))
                    || (type->is_Path() && (type->as_Path().isGenerator() || type->as_Path().isFuture()))) {
                    HIRTraitPath::assocListT assocList;
                    assocList.insert(std::make_pair(nameDiscriminant, HIRTraitPath::AtyEqual{trait, {}, crate.types.primitive(HIRCoreType::U32)}));
                    return callback.visit(ImplRef(type, {}, std::move(assocList)), HIRCompare::Equal);
                } else {
                    HIRTraitPath::assocListT assocList;
                    assocList.insert(std::make_pair(nameDiscriminant, HIRTraitPath::AtyEqual{trait, {}, crate.types.primitive(HIRCoreType::U8)}));
                    return callback.visit(ImplRef(type, {}, std::move(assocList)), HIRCompare::Equal);
                }
            }
            if (!langPointee().components().empty() && trait == langPointee()) {
                const auto nameMetadata = RcString::newInterned("Metadata");
                const auto delegateMetadata = [&](const HIRTypeData* tailTy) {
                    return this->solveTraitGoal(sp, trait, params, tailTy, [&](SolverResponse response) {
                        if (!response.hasImpl || !response.impl) {
                            return false;
                        }
                        auto impl = response.impl->legacy();
                        HIRTraitPath::assocListT assoc;
                        auto metadataTy = impl.getType(crate.types, "Metadata", {});
                        if (metadataTy) {
                            assoc.insert(std::make_pair(nameMetadata, HIRTraitPath::AtyEqual{trait, {}, std::move(metadataTy)}));
                        }
                        const auto cmp = response.certainty == SolverCertainty::Proven ? HIRCompare::Equal : HIRCompare::Fuzzy;
                        return callback.visit(ImplRef(type, params.clone(), std::move(assoc)), cmp);
                    }, {.assocName = ""});
                };
                // TODO: This logic is near identical to the logic in `static.cpp` - can it be de-duplicated?

                HIRTypeRef metaTy = crate.types.infer();
                bool hasMetaTy = false;
                if (type->is_Infer() || (type->is_Path() && type->as_Path().binding.is_Unbound())) {
                    return callback.visit(ImplRef(type, HIRPathParams(), HIRTraitPath::assocListT()), HIRCompare::Fuzzy);
                }
                // Generics (or opaque ATYs)
                else if (type->is_Generic() || (type->is_Path() && type->as_Path().binding.is_Opaque())) {
                    // If the type is `Sized` return `()` as the type
                    if (typeIsSized(sp, type) != HIRCompare::Unequal) {
                        metaTy = crate.types.unit();
                        hasMetaTy = true;
                    } else {
                        // Return unbounded
                        // - leave as `_`
                    }
                }
                // Trait object: `Metadata=DynMetadata<T>`
                else if (type->is_TraitObject()) {
                    metaTy = crate.types.path(HIRPath(HIRGenericPath(this->crate.getLangItemPath(sp, "dyn_metadata"), HIRPathParams(type))), HIRTypePathBinding::make_Struct(&crate.getStructByPath(sp, this->crate.getLangItemPath(sp, "dyn_metadata"))));
                    hasMetaTy = true;
                }
                // Slice and str
                else if (type->is_Slice() || ((*type).is_Primitive() && ((*type).as_Primitive() == HIRCoreType::Str))) {
                    metaTy = crate.types.primitive(HIRCoreType::Usize);
                    hasMetaTy = true;
                }
                // Structs: Can delegate their metadata
                else if (type->is_Path() && type->as_Path().binding.is_Struct()) {
                    const auto& str = *type->as_Path().binding.as_Struct();
                    switch (str.structMarkings.dstType) {
                        case HIRStructMarkings::DstType::None:
                            metaTy = crate.types.unit();
                            hasMetaTy = true;
                            break;
                        case HIRStructMarkings::DstType::Possible:
                        case HIRStructMarkings::DstType::Projection:
                        case HIRStructMarkings::DstType::TraitObject: {
                            const HIRTypeData* tailTpl = nullptr;
                            switch (str.data.tag()) {
                                case HIRStructData::TAG_Unit: {
                                    BUG(sp, "Unsized unit struct in Pointee lookup - " << type);
                                    break;
                                }
                                case HIRStructData::TAG_Tuple: {
                                    auto& se = str.data.as_Tuple();
                                    ASSERT_BUG(sp, !se.empty(), "Unsized tuple struct without fields - " << type); tailTpl = se.back().ent;
                                    break;
                                }
                                case HIRStructData::TAG_Named: {
                                    auto& se = str.data.as_Named();
                                    ASSERT_BUG(sp, !se.empty(), "Unsized struct without fields - " << type); tailTpl = se.back().ty;
                                    break;
                                }
                            }
                            ASSERT_BUG(sp, tailTpl, "Missing unsized tail field for " << type);

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
                }
                // A tuple is unsized when its last element is, and it takes
                // that element's metadata.
                else if (type->is_Tuple() && !type->as_Tuple().empty()) {
                    auto tailTy = this->expandAssociatedTypes(sp, HIRTypeRef(type->as_Tuple().back()));
                    return delegateMetadata(tailTy);
                } else {
                    metaTy = crate.types.unit();
                    hasMetaTy = true;
                }
                HIRTraitPath::assocListT assocList;
                if (hasMetaTy) {
                    assocList.insert(std::make_pair(RcString::newInterned("Metadata"), HIRTraitPath::AtyEqual{trait, {}, mv$(metaTy)}));
                }

                return callback.visit(ImplRef(type, {}, std::move(assocList)), HIRCompare::Equal);
            }
            // - `Tuple`
            if (!langTuple.components().empty() && trait == langTuple) {
                // Fuzzy impl for `_` and unbound ATYs
                if (type->is_Infer() || (type->is_Path() && type->as_Path().binding.is_Unbound())) {
                    return callback.visit(ImplRef(type, HIRPathParams(), HIRTraitPath::assocListT()), HIRCompare::Fuzzy);
                }
                // Impl for tuples
                if (type->is_Tuple()) {
                    return callback.visit(ImplRef(type, {}, HIRTraitPath::assocListT()), HIRCompare::Equal);
                }
                // No impls for anything else
                return false;
            }

            // Magic Unsize impls to trait objects
            if (trait == langUnsize()) {
                ASSERT_BUG(sp, params.types.size() == 1, "Unsize trait requires a single type param");
                const auto& dstTy = this->ivars.getType(params.types[0]);

                if (assembleParamEnvCandidatesCb(sp, trait, params, type, callback)) {
                    return true;
                }

                bool rv = false;
                auto cb = [&](auto newDst) {
                    HIRPathParams realParams{mv$(newDst)};
                    rv = callback.visit(ImplRef(type, mv$(realParams), {}), HIRCompare::Fuzzy);
                };
                //if( dst_ty->is_Infer() || type->is_Infer() )
                //{
                //}
                auto cmp = this->canUnsize(sp, dstTy, type, cb);
                if (cmp == HIRCompare::Equal) {
                    assert(!rv);
                    rv = callback.visit(ImplRef(type, params.clone(), {}), HIRCompare::Equal);
                }
                return rv;
            }

            // Magical CoerceUnsized impls for various types
            if (!langCoerceUnsized.components().empty() && trait == langCoerceUnsized) {
                if (assembleParamEnvCandidatesCb(sp, trait, params, type, callback)) {
                    return true;
                }

                const auto& dstTy = params.types.at(0);
                // - `*mut T => *const T`
                if (const auto* e = type->opt_Pointer()) {
                    if (const auto* de = dstTy->opt_Pointer()) {
                        if (de->type < e->type) {
                            auto cmp = e->inner->compareWithPlaceholders(sp, de->inner, this->ivars.callbackResolveInfer());
                            if (cmp != HIRCompare::Unequal) {
                                HIRPathParams pp;
                                pp.types.push_back(dstTy);
                                if (callback.visit(ImplRef(type, mv$(pp), {}), cmp)) {
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
                // Lowest level of sizedness: This _might_ be sized (i.e. it's not an extern type?)
                return callback.visit(ImplRef(type, {}, HIRTraitPath::assocListT()), HIRCompare::Equal);
            } else if (trait == langMetaSized()) {
                TODO(sp, "MetaSized");
                // Next level of sizedness: There's metadata that allows getting the size
                // - No difference to the above?
                //switch( this->metadata_type(sp, type) )
                //{
                //case MetadataType::Unknown:
                //case MetadataType::None:
                //case MetadataType::Slice:
                //case MetadataType::TraitObject:
                //case MetadataType::Zero:    // TODO: Does zero apply here?
                //}
            }

            if (trait == langDestruct()) {
                // Inidicates that something is droppable
                // - Applies to everything?
                if (assembleParamEnvCandidatesCb(sp, trait, params, type, callback)) {
                    return true;
                }
                // Lowest level of sizedness: This _might_ be sized (i.e. it's not an extern type?)
                return callback.visit(ImplRef(type, {}, HIRTraitPath::assocListT()), HIRCompare::Equal);
            }

            return false;
        }

        bool TraitResolution::assembleTypeCandidatesCb(const Span& sp, const HIRSimplePath& trait, const HIRPathParams& params, const HIRTypeData* type, TraitImplCallback& callback) const {
    type = this->ivars.getType(type);
    const bool isAsyncCallableTrait = trait == langAsyncFn() || trait == langAsyncFnMut() || trait == langAsyncFnOnce();
    auto findAsyncCallable = [&](const ::std::vector<HIRTypeRef>& inputTypes, const HIRTypeData* futureType, bool supportsShared, bool supportsMutable) {
        if (!isAsyncCallableTrait || (trait == langAsyncFn() && !supportsShared) || (trait == langAsyncFnMut() && !supportsMutable)) {
            return false;
        }
        if (params.types.size() != 1 || !params.types.front()->is_Tuple()) {
            BUG(sp, "AsyncFn* traits require a single tuple argument");
        }

        const auto& desiredInputs = params.types.front()->as_Tuple();
        if (desiredInputs.size() != inputTypes.size()) {
            return false;
        }

        HIRCompare cmp = HIRCompare::Equal;
        for (size_t i = 0; i < inputTypes.size(); i++) {
            cmp &= inputTypes[i]->compareWithPlaceholders(sp, desiredInputs[i], this->ivars.callbackResolveInfer());
        }
        if (cmp == HIRCompare::Unequal) {
            return false;
        }

        HIRTypeRef outputType = HIRTypeRef();
        HIRCompare futureCmp = HIRCompare::Unequal;
        this->solveTraitGoal(sp, langFuture(), {}, futureType, [&](SolverResponse response) {
            if (!response.hasImpl || !response.impl) {
                return false;
            }
            auto impl = response.impl->legacy();
            auto candidateOutput = impl.getType(crate.types, "Output", {});
            if (candidateOutput == HIRTypeRef()) {
                return false;
            }
            outputType = mv$(candidateOutput);
            futureCmp = response.certainty == SolverCertainty::Proven ? HIRCompare::Equal : HIRCompare::Fuzzy;
            return response.certainty == SolverCertainty::Proven;
        }, {.assocName = ""});
        if (outputType == HIRTypeRef()) {
            return false;
        }
        cmp &= futureCmp;

        HIRPathParams actualParams;
        actualParams.types.push_back(crate.types.tuple(inputTypes));
        HIRGenericPath oncePath(langAsyncFnOnce(), actualParams.clone());
        HIRTraitPath::assocListT assoc;
        assoc.insert(::std::make_pair("Output", HIRTraitPath::AtyEqual{oncePath.clone(), {}, outputType}));
        assoc.insert(::std::make_pair("CallOnceFuture", HIRTraitPath::AtyEqual{mv$(oncePath), {}, futureType}));
        // A by-reference call hands back the same future; its lifetime parameter
        // is not carried in HIR.
        assoc.insert(::std::make_pair("CallRefFuture", HIRTraitPath::AtyEqual{HIRGenericPath(langAsyncFnMut(), actualParams.clone()), {}, futureType}));
        return callback.visit(ImplRef(type, mv$(actualParams), mv$(assoc)), cmp);
    };

    switch ((*type).tag()) {
default:
        break;
        case HIRTypeData::TAG_NodeType: {
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
                        ::std::vector<HIRTypeRef> inputs;
                        inputs.reserve(nodeP->args.size());
                        for (const auto& arg : nodeP->args) {
                            inputs.push_back(arg.second);
                        }
                        return findAsyncCallable(inputs, nodeP->returnType, supportsShared, supportsMutable);
                    }
                    if (trait == langFn() || trait == langFnMut() || trait == langFnOnce()) {
                        // `FnMut::call_mut(&mut f, ())` names the trait without
                        // an argument tuple to compare against. What the closure
                        // takes is the only thing that tuple could be, so it is
                        // the answer, and the caller equates.
                        const HIRTypeData* wanted = params.types.size() == 1 && params.types[0]->is_Tuple() ? params.types[0] : nullptr;

                        auto cmp = wanted ? HIRCompare::Equal : HIRCompare::Fuzzy;
                        ::std::vector<HIRTypeRef> args;
                        if (wanted && wanted->as_Tuple().size() != nodeP->args.size()) {
                            return false;
                        }
                        for (unsigned int i = 0; i < nodeP->args.size(); i++) {
                            const auto& at = nodeP->args[i].second;
                            args.push_back(at);
                            if (!wanted) {
                                continue;
                            }
                            const auto& argsDes = wanted->as_Tuple();
                            auto argCmp = at->compareWithPlaceholders(sp, argsDes[i], this->ivars.callbackResolveInfer());
                            if (argCmp == HIRCompare::Unequal) {
                                // The wanted argument may be an opaque type this
                                // body gets to define, in which case the
                                // closure's own argument is what it turns out to
                                // be -- the caller equates the two.
                                if (const auto* er = argsDes[i]->opt_ErasedType()) {
                                    if (const auto* alias = er->inner.opt_Alias(); alias && isOpaqueAliasDefiningScope(*alias->inner)) {
                                        argCmp = HIRCompare::Fuzzy;
                                    }
                                }
                            }
                            cmp &= argCmp;
                        }
                        if (cmp != HIRCompare::Unequal) {
                            // NOTE: This is a conditional "true", we know nothing about the move/mut-ness of this closure yet
                            // - Could we?
                            // - Not until after the first stage of typeck


                            HIRPathParams pp;
                            pp.types.push_back(crate.types.tuple(mv$(args)));
                            HIRTraitPath::assocListT types;
                            types.insert(::std::make_pair("Output", HIRTraitPath::AtyEqual{HIRGenericPath(langFnOnce(), pp.clone()), {}, nodeP->returnType}));
                            return callback.visit(ImplRef(type, mv$(pp), mv$(types)), cmp);
                        } else {
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
                        assoc.insert(::std::make_pair(rcstringYield, HIRTraitPath::AtyEqual{trait.clone(), {}, nodeP->yieldTy}));
                        assoc.insert(::std::make_pair(rcstringReturn, HIRTraitPath::AtyEqual{trait.clone(), {}, nodeP->returnType}));
                        HIRPathParams params;
                        params.types.push_back(nodeP->resumeTy);
                        return callback.visit(ImplRef(type, mv$(params), mv$(assoc)), HIRCompare::Equal);
                    }
                    break;
                }
                case HIRTypeDataNodeType::TAG_Async: {
                    auto& nodeP = e.as_Async();
                    if (nodeP->isAsyncGen) {
                        // An `async gen` block is an AsyncIterator, not a Future.
                        if (trait == langAsyncIterator()) {
                            const RcString rcstringItem = RcString::newInterned("Item");
                            HIRTraitPath::assocListT assoc;
                            assoc.insert(::std::make_pair(rcstringItem, HIRTraitPath::AtyEqual{trait.clone(), {}, nodeP->yieldTy}));
                            return callback.visit(ImplRef(type, {}, mv$(assoc)), HIRCompare::Equal);
                        }
                    } else if (trait == langFuture()) {
                        const RcString rcstringOutput = RcString::newInterned("Output");
                        HIRTraitPath::assocListT assoc;
                        assoc.insert(::std::make_pair(rcstringOutput, HIRTraitPath::AtyEqual{trait.clone(), {}, nodeP->returnType}));
                        return callback.visit(ImplRef(type, {}, mv$(assoc)), HIRCompare::Equal);
                    }
                    break;
                }
            }
            break;
        }
        case HIRTypeData::TAG_Path: {
            // A closure lowered to its struct (post-typecheck phases): its
            // `Fn*` impls are generated, so async-callability forwards
            // through them.  StaticTraitResolve has the same forwarding, but
            // the goal bridge answers before reaching it.
            if (isAsyncCallableTrait && type->as_Path().isClosure() && params.types.size() == 1 && params.types.front()->is_Tuple()) {
                const auto& fnTrait = trait == langAsyncFn() ? langFn() : (trait == langAsyncFnMut() ? langFnMut() : langFnOnce());
                HIRTypeRef futureType;
                this->solveTraitGoal(sp, langFnOnce(), params, type, [&](SolverResponse response) {
                    if (response.certainty != SolverCertainty::Proven || !response.hasImpl || !response.impl) {
                        return false;
                    }
                    auto impl = response.impl->legacy();
                    futureType = impl.getType(crate.types, "Output", {});
                    return futureType != HIRTypeRef();
                }, {.assocName = ""});
                bool callable = fnTrait == langFnOnce();
                if (!callable) {
                    callable = this->solveTraitGoal(sp, fnTrait, params, type, [](SolverResponse response) {
                        return response.hasImpl && response.certainty == SolverCertainty::Proven;
                    }, {.assocName = ""});
                }
                if (callable && futureType != HIRTypeRef() && findAsyncCallable(params.types.front()->as_Tuple(), futureType, true, true)) {
                    return true;
                }
            }
            break;
        }
        case HIRTypeData::TAG_Function: {
            auto& e = (*type).as_Function();
            if (isAsyncCallableTrait) {
                if (e.abi != ABI_RUST || e.isUnsafe) {
                    return false;
                }
                return findAsyncCallable(e.argTypes, e.rettype, true, true);
            }
            if (trait == langFn() || trait == langFnMut() || trait == langFnOnce()) {
                if (params.types.size() != 1) {
                    BUG(sp, "Fn* traits require a single tuple argument");
                }
                if (!params.types[0]->is_Tuple()) {
                    BUG(sp, "Fn* traits require a single tuple argument");
                }
                const auto& argsDes = params.types[0]->as_Tuple();
                if (argsDes.size() != e.argTypes.size()) {
                    return false;
                }

                // NOTE: unsafe or non-rust ABI functions aren't valid
                if (e.abi != ABI_RUST || e.isUnsafe) {
                    return false;
                }

                auto cmp = HIRCompare::Equal;
                ::std::vector<HIRTypeRef> args;
                for (unsigned int i = 0; i < e.argTypes.size(); i++) {
                    const auto& at = e.argTypes[i];
                    args.push_back(at);
                    cmp &= at->compareWithPlaceholders(sp, argsDes[i], this->ivars.callbackResolveInfer());
                }

                HIRPathParams pp;
                pp.types.push_back(crate.types.tuple(mv$(args)));
                HIRTraitPath::assocListT types;
                types.insert(::std::make_pair("Output", HIRTraitPath::AtyEqual{HIRGenericPath(langFnOnce(), pp.clone()), {}, e.rettype}));
                return callback.visit(ImplRef(type, mv$(pp), mv$(types)), cmp);
            }
            break;
        }
        case HIRTypeData::TAG_NamedFunction: {
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
                    BUG(sp, "Fn* traits require a single tuple argument");
                }
                if (!params.types[0]->is_Tuple()) {
                    BUG(sp, "Fn* traits require a single tuple argument");
                }

                auto e = realE.decay(crate.types, sp);
                const auto& argsDes = params.types[0]->as_Tuple();
                if (argsDes.size() != e.argTypes.size()) {
                    return false;
                }

                // NOTE: unsafe or non-rust ABI functions aren't valid
                if (e.abi != ABI_RUST) {
                    return false;
                }
                if (e.isUnsafe) {
                    return false;
                }

                auto cmp = HIRCompare::Equal;
                ::std::vector<HIRTypeRef> args;
                for (unsigned int i = 0; i < e.argTypes.size(); i++) {
                    const auto& at = e.argTypes[i];
                    args.push_back(at);
                    cmp &= at->compareWithPlaceholders(sp, argsDes[i], this->ivars.callbackResolveInfer());
                }

                HIRPathParams pp;
                pp.types.push_back(crate.types.tuple(mv$(args)));
                HIRTraitPath::assocListT types;
                types.insert(::std::make_pair("Output", HIRTraitPath::AtyEqual{HIRGenericPath(langFnOnce(), pp.clone()), {}, e.rettype}));
                return callback.visit(ImplRef(type, mv$(pp), mv$(types)), cmp);
            }
            break;
        }
        case HIRTypeData::TAG_Array: {
            break;
        }
    }
    return false;
        }

        bool TraitResolution::assembleOtherCandidatesCb(
            const Span& sp,
            const HIRSimplePath& trait,
            const HIRPathParams& params,
            const HIRTypeData* ty,
            TraitImplCallback& callback
        ) const {
            const auto& type = this->ivars.getType(ty);

            if (assembleTypeCandidatesCb(sp, trait, params, ty, callback)) {
                return true;
            }

            // Trait impls from complex bounds
    switch ((*type).tag()) {
default:
        break;
        // Trait objects automatically implement their own traits
        // - IF object safe (TODO)
        case HIRTypeData::TAG_TraitObject: {
            auto& e = (*type).as_TraitObject();
            if (trait == e.trait.path.path) {
                auto cmp = comparePp(sp, e.trait.path.params, params);
                if (cmp != HIRCompare::Unequal) {
                    return callback.visit(ImplRef(type, &e.trait.path.params, &e.trait.typeBounds, e.trait.constness), cmp);
                }
            }
            // Markers too
            for (const auto& mt : e.markers) {
                if (trait == mt.path) {
                    auto cmp = comparePp(sp, mt.params, params);
                    if (cmp != HIRCompare::Unequal) {
                        return callback.visit(ImplRef(type, &mt.params, nullptr), cmp);
                    }
                }
            }

            if (e.trait.path.path != HIRSimplePath()) {
                // - Check if the desired trait is a supertrait of this.
                // NOTE: `params` (aka des_params) is not used (TODO)
                bool rv = false;
                bool isSupertrait = false;
                this->findNamedTraitInTrait(sp, trait, params, *e.trait.traitPtr, e.trait.path.path, e.trait.path.params, type, [&](const HIRTraitPath& iTp) {
                    // The above is just the monomorphised params and associated set. Comparison is still needed.
                    auto cmp = this->comparePp(sp, iTp.path.params, params);
                    if (cmp != HIRCompare::Unequal) {
                        // Invoke callback with a proper ImplRef
                        HIRTraitPath::assocListT assocClone;
                        for (const auto& e : iTp.typeBounds) {
                            assocClone.insert(::std::make_pair(e.first, e.second.clone()));
                        }
                        for (const auto& bound : e.trait.typeBounds) {
                            if (bound.second.sourceTrait.path == trait && comparePp(sp, bound.second.sourceTrait.params, iTp.path.params) != HIRCompare::Unequal) {
                                assocClone.erase(bound.first);
                                assocClone.insert(::std::make_pair(bound.first, bound.second.clone()));
                            }
                        }
                        auto ir = ImplRef(type, iTp.path.params.clone(), mv$(assocClone));
                        isSupertrait = true;
                        rv = callback.visit(mv$(ir), cmp);
                        return cmp == HIRCompare::Equal; // Shortcut if perfect match
                    }
                    return false;
                });
                if (isSupertrait) {
                    return rv;
                }
            }
            break;
        }
        case HIRTypeData::TAG_ErasedType: {
            auto& e = (*type).as_ErasedType();
            for (const auto& traitPath : e.traits) {
                if (trait == traitPath.path.path) {
                    auto cmp = comparePp(sp, traitPath.path.params, params);
                    if (cmp != HIRCompare::Unequal) {
                        return callback.visit(ImplRef(type, &traitPath.path.params, &traitPath.typeBounds, traitPath.constness), cmp);
                    }
                }

                // - Check if the desired trait is a supertrait of this.
                // NOTE: `params` (aka des_params) is not used (TODO)
                bool rv = false;
                bool isSupertrait = false;
                this->findNamedTraitInTrait(sp, trait, params, *traitPath.traitPtr, traitPath.path.path, traitPath.path.params, type, [&](const HIRTraitPath& iTp) {
                    // The above is just the monomorphised params and associated set. Comparison is still needed.
                    auto cmp = this->comparePp(sp, iTp.path.params, params);
                    if (cmp != HIRCompare::Unequal) {
                        // Invoke callback with a proper ImplRef
                        HIRTraitPath::assocListT assocClone;
                        for (const auto& e : iTp.typeBounds) {
                            assocClone.insert(::std::make_pair(e.first, e.second.clone()));
                        }
                        // Existential equalities are stored on the principal
                        // bound even when the associated item is declared by
                        // a supertrait (e.g. `FnMut` carries `FnOnce::Output`).
                        // Project those equalities together with the
                        // supertrait candidate.
                        for (const auto& e : traitPath.typeBounds) {
                            if (e.second.sourceTrait.path == trait && comparePp(sp, e.second.sourceTrait.params, iTp.path.params) != HIRCompare::Unequal) {
                                assocClone.erase(e.first);
                                assocClone.insert(::std::make_pair(e.first, e.second.clone()));
                            }
                        }
                        auto ir = ImplRef(type, iTp.path.params.clone(), mv$(assocClone));
                        isSupertrait = true;
                        rv = callback.visit(mv$(ir), cmp);
                        return cmp == HIRCompare::Equal;
                    }
                    return false;
                });
                if (isSupertrait) {
                    return rv;
                }
            }
            break;
        }
        case HIRTypeData::TAG_Generic: {
            auto& e = (*type).as_Generic();
            if ((e.binding >> 8) == 2) {
                // TODO: This is probably going to break something in the future.
                return callback.visit(ImplRef(type, nullptr, nullptr), HIRCompare::Fuzzy);
            }
            break;
        }
        case HIRTypeData::TAG_Path: {
            auto& e = (*type).as_Path();
            if (e.binding.is_Opaque()) {
                ASSERT_BUG(sp, e.path.data.is_UfcsKnown(), "Opaque bound type wasn't UfcsKnown - " << type);
                const auto& pe = e.path.data.as_UfcsKnown();

                // TODO: Should Self here be `type` or `pe.type`
                // - Depends... if implicit it should be `type` (as it relates to the associated type), but if explicit it's referring to the trait
                auto monomorphCb = MonomorphStatePtr(crate.types, pe.type, &pe.trait.params, &pe.params);
                auto rv = this->iterateAtyBounds(sp, pe, [&](const HIRTraitPath& bound) {
                    auto ppHrb = HIRPathParams();
                    monomorphCb.ppHrb = &ppHrb;
                    const auto& bParams = bound.path.params;
                    HIRPathParams paramsMonoO;
                    const HIRPathParams* bParamsMono = &bParams;
                    if (monomorphisePathparamsNeeded(bParams)) {
                        paramsMonoO = monomorphCb.monomorphPathParams(sp, bParams, false);
                        bParamsMono = &paramsMonoO;
                    }
                    const bool paramsNeedNormalisation = ::std::any_of(bParamsMono->types.begin(), bParamsMono->types.end(), [&](const auto& ty) {
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
                        bAtys.insert(::std::make_pair(aty.first, HIRTraitPath::AtyEqual{
                            monomorphCb.monomorphGenericpath(sp, aty.second.sourceTrait, false),
                            monomorphCb.monomorphPathParams(sp, aty.second.atyParams, false),
                            monomorphCb.monomorphType(sp, aty.second.type)}));
                    }

                    if (bound.path.path == trait) {
                        auto cmp = this->comparePp(sp, *bParamsMono, params);
                        if (cmp != HIRCompare::Unequal) {
                            if (bParamsMono == &paramsMonoO) {
                                // TODO: assoc bounds
                                if (callback.visit(ImplRef(type, mv$(paramsMonoO), mv$(bAtys), bound.constness), cmp)) {
                                    return true;
                                }
                                paramsMonoO = monomorphCb.monomorphPathParams(sp, bParams, false);
                                if (paramsNeedNormalisation) {
                                    this->expandAssociatedTypesParams(sp, paramsMonoO);
                                }
                            } else if (!bAtys.empty()) {
                                if (callback.visit(ImplRef(type, bParamsMono->clone(), mv$(bAtys), bound.constness), cmp)) {
                                    return true;
                                }
                            } else {
                                if (callback.visit(ImplRef(type, &bound.path.params, nullptr, bound.constness), cmp)) {
                                    return true;
                                }
                            }
                        }
                    }
                    monomorphCb.ppHrb = nullptr;

                    bool rv = false;
                    bool ret = false;
                    this->findNamedTraitInTrait(sp, trait, params, *bound.traitPtr, bound.path.path, *bParamsMono, type, [&](const HIRTraitPath& iTp) {
                        auto cmp = this->comparePp(sp, iTp.path.params, params);
                        // The supertrait's associated equalities travel with
                        // the candidate (`Int: BitXor<Output = Self>` must
                        // answer `Output`), exactly as the TraitObject and
                        // ErasedType supertrait paths do.
                        HIRTraitPath::assocListT assocClone;
                        for (const auto& aty : iTp.typeBounds) {
                            assocClone.insert(::std::make_pair(aty.first, aty.second.clone()));
                        }
                        auto ir = ImplRef(type, iTp.path.params.clone(), mv$(assocClone), iTp.constness);
                        rv |= (cmp != HIRCompare::Unequal && callback.visit(std::move(ir), cmp));
                        ret = true;
                        return false; // Continue
                    });
                    if (ret) {
                        // NOTE: Callback called in closure's return statement
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
    } // TU_MATCH_HDRA

    return false;
        }

        class NextTraitGoalEvaluator {
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
                ImplRef impl;
                // Exactness is only a ranking fact: an environment head that
                // names the goal verbatim outranks one which constrains an
                // input.  Whether the relation is actually proved is carried
                // separately by headRelation and never inferred from this.
                HIRCompare headMatch;
                Certainty headRelation;
                Certainty certainty;
                const HIRMarkerImpl* markerImpl;
                HIRPathParams markerImplParams;
                bool autoBuiltin;
                CandidateSource source;
                // The head stayed fuzzy because a rigid projection/deferred
                // const must normalize, rather than because caller inference
                // can be guided. Such a candidate cannot shadow a proven
                // specialization fallback yet.
                bool headNormalizationAmbiguity;
                bool ambiguityBeyondHead = false;
                // Ambiguity from the candidate's own predicates (nested
                // bounds, implicit Sized, type equalities, the structural
                // auto walk) -- NOT from comparing the requested associated
                // output, which is an output rather than a filter.  Decides
                // whether an exported possibility keeps its Equal head.
                bool nestedAmbiguity = false;
                // Every call-site coercion relation over this head was
                // proven.  A merely possible relation keeps the whole trait
                // response ambiguous and must not publish candidate effects.
                bool coercionsProven = true;
                // Relations deferred while matching the impl head are part of
                // the candidate answer.  In particular, an unresolved
                // projection is not a wildcard: selecting a concrete head
                // requires that projection to equal the concrete type.
                ThinVector<SolverTypeEquality> headEqualities;
                ThinVector<SolverValueEquality> headValueEqualities;
                // Equalities established by a selected call-site relation.
                // They are answer effects just like impl-head equalities;
                // keeping them on the candidate prevents an unselected
                // coercion endpoint from constraining the caller.
                ThinVector<SolverTypeEquality> coercionEqualities;
                // Declared alias bounds needed by a deferred head relation.
                // Ambiguous ones are exported as ordinary solver obligations.
                ThinVector<SolverObligation> headObligations;
                // Effects returned by nested relation goals while evaluating
                // this candidate.  They are candidate-local: publishing them
                // before selection would let an unselected projection guide
                // caller inference.
                ThinVector<SolverTypeEquality> relationEqualities;
                ThinVector<SolverValueEquality> relationValueEqualities;
                ThinVector<SolverObligation> relationObligations;
                stl::Vector<const HIRGenericBound*> normalizationNestedGoals;
                bool discarded = false;
                // The certainty of the trait goal alone, before the root
                // associated-item requirement could downgrade it.
                Certainty traitCertainty = Certainty::Ambiguous;
                // The most specific impl this candidate shadowed during merge:
                // a specialising impl that omits an associated item inherits
                // the value from this chain (rustc's specialization graph).
                const Candidate* specializationItemSource = nullptr;

                Candidate(ImplRef impl, HIRCompare headMatch, Certainty headRelation, const HIRMarkerImpl* markerImpl, HIRPathParams markerImplParams, bool autoBuiltin, CandidateSource source, bool headNormalizationAmbiguity = false, ThinVector<SolverTypeEquality> headEqualities = {}, ThinVector<SolverValueEquality> headValueEqualities = {})
                    : impl(::std::move(impl))
                    , headMatch(headMatch)
                    , headRelation(headRelation)
                    , certainty(Certainty::Ambiguous)
                    , markerImpl(markerImpl)
                    , markerImplParams(::std::move(markerImplParams))
                    , autoBuiltin(autoBuiltin)
                    , source(source)
                    , headNormalizationAmbiguity(headNormalizationAmbiguity)
                    , headEqualities(::std::move(headEqualities))
                    , headValueEqualities(::std::move(headValueEqualities))
                {
                }

                bool isNegative() const {
                    return markerImpl && !markerImpl->isPositive;
                }

                bool isPositiveMarkerImpl() const {
                    return markerImpl && markerImpl->isPositive;
                }
            };

            struct CandidateFrame {
                ::std::vector<Candidate*> candidates;
                ::std::vector<Candidate*> viable;
                size_t availableDepth = 0;
                bool encounteredOverflow = false;

                CandidateFrame() {
                    candidates.reserve(32);
                    viable.reserve(32);
                }

                void clear(stl::ObjList<Candidate>& nodes) {
                    for (auto* candidate : candidates) {
                        nodes.release(candidate);
                    }
                    candidates.clear();
                    viable.clear();
                    availableDepth = 0;
                    encounteredOverflow = false;
                }
            };

            static constexpr size_t ROOT_DEPTH = 128;
            static constexpr size_t OVERFLOW_DEPTH_DIVISOR = 4;

            struct GoalKey {
                size_t hash;
                HIRSimplePath trait;
                HIRPathParams params;
                HIRTypeRef type;
                HIRTraitPath::assocListT associated;

                GoalKey(size_t hash, const HIRSimplePath& trait, const HIRPathParams& params, const HIRTypeData* type, const HIRTraitPath::assocListT* associated)
                    : hash(hash)
                    , trait(trait)
                    , params(params.clone())
                    , type(type)
                    , associated(cloneAssociated(associated))
                {
                }
            };

            struct CachedGoal {
                GoalKey goal;
                Certainty certainty;
                const SolverResponse* response = nullptr;
                bool hasResponse = false;
                // A fully rigid canonical goal (no inference variables in the
                // key) evaluated without touching a goal cycle depends only on
                // the resolver's ParamEnv: it stays valid for the resolver's
                // whole lifetime, not just one outermost evaluation.
                bool persistent = false;

                CachedGoal(size_t hash, const HIRSimplePath& trait, const HIRPathParams& params, const HIRTypeData* type, const HIRTraitPath::assocListT* associated, Certainty certainty)
                    : goal(hash, trait, params, type, associated)
                    , certainty(certainty)
                {
                }
            };

            const TraitResolution& resolve_;
            const HIRCrate& crate;
            const Span* span_ = nullptr;
            bool coherenceMode = false;
            // Counts cycle-head hits; a result computed while this moved is
            // provisional and must not persist across evaluations.
            mutable u64 cycleHits_ = 0;
            // Tracks the resolver's generic-context generation for the
            // persistent slice of the goal cache.
            mutable u64 envGeneration_ = ~0ull;
            // Inference-table + defining-opaque registration state the
            // non-persistent cache slice was built against; the slice stays
            // warm across evaluations until either changes.
            mutable u64 ivarGenerationSeen_ = ~0ull;
            mutable u64 solverEnvGenerationSeen_ = ~0ull;

            // Frames and candidates have stable pool-backed addresses.  Vectors
            // are pointer indexes only, so recursive growth never moves an ImplRef
            // or invalidates a parent candidate.
            stl::ObjList<Candidate> candidateNodes;
            ::std::vector<CandidateFrame*> frames;
            size_t frameDepth = 0;
            stl::ObjList<GoalKey> activeGoalNodes;
            stl::ObjList<CachedGoal> cachedGoalNodes;
            ::std::vector<GoalKey*> goalStack;
            ::std::vector<CachedGoal*> goalCache;
            ::std::unordered_multimap<size_t, GoalKey*> activeGoalIndex;
            ::std::unordered_multimap<size_t, CachedGoal*> goalCacheIndex;

            struct CanonicalGoal {
                HIRPathParams params;
                HIRTypeRef type;
                HIRTraitPath::assocListT associated;

                CanonicalGoal(HIRPathParams params, HIRTypeRef type)
                    : params(::std::move(params))
                    , type(type)
                {
                }
            };

            const Span& span() const {
                ASSERT_BUG(Span(), span_, "next-solver session used outside an evaluation");
                return *span_;
            }

            // ---- Crate-lifetime concrete-goal cache ----
            // Fully concrete canonical goals (no inference variables, no
            // generics, no placeholders, no opaques, no unbound paths) have
            // context-free answers whenever the resolver carries no bounds.
            bool goalIsConcrete(const HIRSimplePath& trait, const CanonicalGoal& canonical) const {
                bool sawGeneric = false;
                auto concrete = [&sawGeneric](const HIRTypeData* ty) {
                    // The type visitor does not walk const-generic VALUES, so
                    // a value-Infer hiding in a path's parameters (Simd<f32,
                    // {Infer}>) would read as concrete and poison the
                    // crate-lifetime cache across functions; the aggregate
                    // flags do see them.
                    if (ty->flags & (HIRTypeData::HAS_TYPE_INFER | HIRTypeData::HAS_DEFERRED_CONST | HIRTypeData::HAS_UNEVALUATED_CONST)) {
                        return false;
                    }
                    return !visitTyWith(ty, [&sawGeneric](const HIRTypeData* inner) {
                        if (const auto* generic = inner->opt_Generic()) {
                            // A rigid user generic in a bound-free env is
                            // answered structurally (blanket impls only), so
                            // the answer transfers between functions.  A
                            // candidate-match placeholder does not: its
                            // answers run under forced-ambiguity rules.
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
                if (sawGeneric
                        && (trait == resolve_.langSized() || trait == resolve_.langMetaSized() || trait == resolve_.langPointeeSized())) {
                    // Sized-family answers on a rigid generic read the
                    // parameter's ?Sized declaration -- context the
                    // empty-bounds gate does not cover.
                    return false;
                }
                return canonical.associated.empty();
            }

            bool crateCacheUsable() const {
                return resolve_.traitBounds.size() == 0 && !coherenceMode;
            }

            NextSolverCrateCache& crateCache() const {
                auto& wb = const_cast<WireBoard&>(resolve_.board());
                if (!wb.solverCache) {
                    wb.solverCache = wb.pool->make<NextSolverCrateCache>();
                }
                return *wb.solverCache;
            }

            // Unresolved inference variables canonicalise positionally into
            // the reserved solver range (CanonicalizeTraitGoal::monomorphType):
            // structurally identical goals share one cache key regardless of
            // which caller variables they hold, and assembly/evaluation run in
            // the same canonical space the key describes.
            CanonicalGoal canonicalizeGoal(const HIRPathParams& params, const HIRTypeData* type, const HIRTraitPath::assocListT* associated, CanonicalizeTraitGoal& canonicalizer) const {
                // Const inference variables resolve through the table inside
                // the canonicalizer: bound values enter the key by value and
                // unresolved ones become canonical value slots, exactly like
                // type variables.
                auto canonicalParams = canonicalizer.monomorphPathParams(span(), params, true);
                const auto canonicalType = canonicalizer.monomorphType(span(), type, true);
                CanonicalGoal result(::std::move(canonicalParams), canonicalType);
                if (associated) {
                    for (const auto& entry : *associated) {
                        result.associated.insert({entry.first, HIRTraitPath::AtyEqual{canonicalizer.monomorphGenericpath(span(), entry.second.sourceTrait, true), canonicalizer.monomorphPathParams(span(), entry.second.atyParams, true), canonicalizer.monomorphType(span(), entry.second.type, true)}});
                    }
                }
                return result;
            }

            ::std::optional<size_t> availableDepthForNested() {
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

            static bool isEnvironmentOrBuiltin(const ImplRef& impl) {
                return !impl.data.is_TraitImpl();
            }

            bool paramsHaveUnknownTypes(const HIRPathParams& params) const {
                for (const auto& type : params.types) {
                    if (typeHasUnknown(type)) {
                        return true;
                    }
                }
                return false;
            }

            bool pathHasUnknownTypes(const HIRPath& path) const {
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

            bool traitPathHasUnknownTypes(const HIRTraitPath& trait) const {
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

            bool valueHasUnassignedInfer(const HIRConstGeneric& value) const {
                if (const auto* infer = value.opt_Infer()) {
                    return infer->index == ~0u;
                }
                if (const auto* unevaluated = value.opt_Unevaluated()) {
                    return ((*unevaluated)->selfType && typeHasUnassignedInfer((*unevaluated)->selfType))
                        || paramsHaveUnassignedInfer((*unevaluated)->paramsImpl)
                        || paramsHaveUnassignedInfer((*unevaluated)->paramsItem);
                }
                return false;
            }

            bool paramsHaveUnassignedInfer(const HIRPathParams& params) const {
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

            bool pathHasUnassignedInfer(const HIRPath& path) const {
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

            bool traitPathHasUnassignedInfer(const HIRTraitPath& trait) const {
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

            bool typeHasUnassignedInfer(const HIRTypeData* input) const {
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

            bool goalHasUnassignedInfer(const HIRPathParams& params, const HIRTypeData* type, const HIRTraitPath::assocListT* associated) const {
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

            bool selfIsUnresolvedProjectionOverIvar(const HIRTypeData* type) const {
                const auto* path = type->opt_Path();
                return path && path->binding.is_Unbound() && path->path.data.is_UfcsKnown() && resolve_.typeContainsIvars(type);
            }

            HIRTypeRef normalizeGoalInput(HIRTypeRef input) const {
                const auto* path = input->opt_Path();
                const auto* projection = path ? path->path.data.opt_UfcsKnown() : nullptr;
                auto output = resolve_.expandAssociatedTypes(span(), input);
                // A ParamEnv predicate can prove the trait without defining
                // the associated value.  During a re-entrant canonical query
                // that non-answer may be represented by a fresh infer.  A
                // projection over a rigid generic must stay a projection so
                // its declaration bounds and the caller ParamEnv remain
                // available when candidate obligations are revalidated.
                if (projection && projection->type->is_Generic() && output->is_Infer()) {
                    return input;
                }
                return output;
            }

            bool typeHasUnknown(const HIRTypeData* input) const {
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

            static bool typeHasCandidatePlaceholder(const HIRTypeData* type) {
                bool found = false;
                visitTyWith(type, [&](const HIRTypeData* inner) {
                    if (const auto* generic = inner->opt_Generic()) {
                        found |= generic->group() == GENERICPlaceholder;
                    }
                    return found;
                });
                return found;
            }

            static bool typeHasUfcsUnknown(const HIRTypeData* type) {
                if (!type) {
                    return false;
                }
                return visitTyWith(type, [](const HIRTypeData* inner) {
                    const auto* path = inner->opt_Path();
                    return path && path->path.data.is_UfcsUnknown();
                });
            }

            static bool paramsNeedResponseConstraints(const HIRPathParams& params) {
                for (const auto& type : params.types) {
                    bool found = false;
                    visitTyWith(type, [&](const HIRTypeData* inner) {
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

            bool candidateNeedsResponseConstraints(const Candidate& candidate) const {
                if (const auto* traitImpl = candidate.impl.data.opt_TraitImpl()) {
                    return paramsNeedResponseConstraints(traitImpl->implParams);
                }
                return candidate.markerImpl && paramsNeedResponseConstraints(candidate.markerImplParams);
            }

            OrphanVisit orphanVisitResolvedType(const HIRTypeData* type, OrphanPerspective perspective) const {
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
                    // References are fundamental even though raw pointers are not.
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

                // Primitive, tuple, array, slice, raw-pointer, function, opaque,
                // and foreign rigid types are non-local and cover their contents.
                return OrphanVisit::NonLocal;
            }

            OrphanVisit orphanVisitType(const HIRTypeData* input, OrphanPerspective perspective) const {
                const auto& resolved = resolve_.resolveType(input);
                const auto* path = resolved->opt_Path();
                const bool isAlias = path && (!path->path.data.is_Generic() || path->binding.is_Unbound() || path->binding.is_Opaque());
                if (isAlias) {
                    // rustc's orphan checker normalizes aliases lazily.  Keep a
                    // rigid alias if normalization only produces a fresh type
                    // variable; such an alias still carries coverage information.
                    auto normalized = resolve_.expandAssociatedTypes(span(), resolved);
                    if (!(normalized->is_Infer() && !resolved->is_Infer())) {
                        return orphanVisitResolvedType(normalized, perspective);
                    }
                }
                return orphanVisitResolvedType(resolved, perspective);
            }

            bool orphanCheckTraitRef(const HIRPathParams& params, const HIRTypeData* type, OrphanPerspective perspective) const {
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

            bool traitRefIsKnowable(const HIRSimplePath& trait, const HIRPathParams& params, const HIRTypeData* type) const {
                if (orphanCheckTraitRef(params, type, OrphanPerspective::Remote)) {
                    return false;
                }

                const auto& traitDef = crate.getTraitByPath(span(), trait);
                if (trait.crateName() == crate.crateName || traitDef.isFundamental) {
                    return true;
                }

                return orphanCheckTraitRef(params, type, OrphanPerspective::Local);
            }

            static size_t hashMix(size_t state, size_t value) {
                // boost::hash_combine's avalanche step.  Equality never relies on
                // this fingerprint: hash collisions are resolved by goal_matches.
                return state ^ (value + 0x9e3779b97f4a7c15ULL + (state << 6) + (state >> 2));
            }

            static size_t hashSimplePath(const HIRSimplePath& path) {
                size_t result = ::std::hash<RcString>()(path.crateName());
                for (const auto& component : path.components()) {
                    result = hashMix(result, ::std::hash<RcString>()(component));
                }
                return result;
            }

            static size_t hashType(const HIRTypeData* type) {
                if (const auto* path = type->getSortPath()) {
                    return hashMix(0x10, hashSimplePath(*path));
                }
                if (const auto* primitive = type->opt_Primitive()) {
                    return hashMix(0x20, static_cast<size_t>(*primitive));
                }
                if (const auto* generic = type->opt_Generic()) {
                    return hashMix(0x30, generic->binding);
                }
                if (const auto* infer = type->opt_Infer()) {
                    return hashMix(0x40, infer->index);
                }
                if (const auto* tuple = type->opt_Tuple()) {
                    size_t result = hashMix(0x50, tuple->size());
                    for (const auto& field : *tuple) {
                        result = hashMix(result, hashType(field));
                    }
                    return result;
                }
                if (const auto* array = type->opt_Array()) {
                    return hashMix(0x60, hashType(array->inner));
                }
                if (const auto* slice = type->opt_Slice()) {
                    return hashMix(0x70, hashType(slice->inner));
                }
                if (const auto* borrow = type->opt_Borrow()) {
                    return hashMix(hashMix(0x80, static_cast<size_t>(borrow->type)), hashType(borrow->inner));
                }
                if (const auto* pointer = type->opt_Pointer()) {
                    return hashMix(hashMix(0x90, static_cast<size_t>(pointer->type)), hashType(pointer->inner));
                }
                if (const auto* traitObject = type->opt_TraitObject()) {
                    return hashMix(0xa0, hashSimplePath(traitObject->trait.path.path));
                }
                if (type->is_Diverge()) {
                    return 0xb0;
                }
                // Function, erased, and compiler-generated node types are rare in
                // recursive solver tables.  A stable tag is sufficient; full
                // equality below still resolves every collision correctly.
                return 0xc0;
            }

            static size_t goalHash(const HIRSimplePath& trait, const HIRPathParams& params, const HIRTypeData* type, const HIRTraitPath::assocListT* associated) {
                size_t result = hashSimplePath(trait);
                result = hashMix(result, params.types.size());
                for (const auto& param : params.types) {
                    result = hashMix(result, hashType(param));
                }
                result = hashMix(result, params.values.size());
                result = hashMix(result, hashType(type));
                if (associated && !associated->empty()) {
                    result = hashMix(result, associated->size());
                    for (const auto& entry : *associated) {
                        result = hashMix(result, ::std::hash<RcString>()(entry.first));
                        result = hashMix(result, hashSimplePath(entry.second.sourceTrait.path));
                        result = hashMix(result, hashType(entry.second.type));
                    }
                }
                return result;
            }

            static HIRTraitPath::assocListT cloneAssociated(const HIRTraitPath::assocListT* associated) {
                HIRTraitPath::assocListT result;
                if (associated) {
                    for (const auto& entry : *associated) {
                        result.insert({entry.first, entry.second.clone()});
                    }
                }
                return result;
            }

            ImplRef monomorphImplRef(const ImplRef& source, const Monomorphiser& monomorph) const {
                auto monomorphAssociated = [&](const HIRTraitPath::assocListT* associated) {
                    HIRTraitPath::assocListT result;
                    if (associated) {
                        for (const auto& entry : *associated) {
                            result.insert({entry.first, monomorph.monomorphTpAtyEqual(span(), entry.second, true)});
                        }
                    }
                    return result;
                };

                ImplRef result;
                if (const auto* impl = source.data.opt_TraitImpl()) {
                    ASSERT_BUG(span(), impl->traitPtr && impl->traitPath && impl->impl, "Cannot monomorphise an invalid trait impl response");
                    result = ImplRef(monomorph.monomorphPathParams(span(), impl->implParams, true), *impl->traitPtr, *impl->traitPath, *impl->impl);
                } else if (const auto* bounded = source.data.opt_BoundedPtr()) {
                    HIRPathParams params;
                    if (bounded->traitArgs) {
                        params = monomorph.monomorphPathParams(span(), *bounded->traitArgs, true);
                    }
                    result = ImplRef(monomorph.monomorphType(span(), bounded->type, true), ::std::move(params), monomorphAssociated(bounded->assoc));
                } else {
                    const auto& owned = source.data.as_Bounded();
                    result = ImplRef(monomorph.monomorphType(span(), owned.type, true), monomorph.monomorphPathParams(span(), owned.traitArgs, true), monomorphAssociated(&owned.assoc));
                }
                if (source.isAmbiguousIdentity()) {
                    result.markAmbiguousIdentity();
                }
                return result;
            }

            const SolverImpl* ownSolverImpl(ImplRef source) const {
                ASSERT_BUG(span(), crate.pool, "solver response requires the crate object pool");
                return crate.pool->make<SolverImpl>(SolverImpl::fromLegacy(::std::move(source)));
            }

            const SolverImpl* monomorphSolverImpl(const SolverImpl& source, const Monomorphiser& monomorph) const {
                return ownSolverImpl(monomorphImplRef(source.legacy(), monomorph));
            }

            const SolverImpl* correlateSolverImplForRead(
                const SolverImpl& source,
                const SolverSlotValues& slots,
                const HIRTypeData* type,
                const HIRPathParams& params
            ) const {
                auto raw = source.legacy();
                CorrelateSolverResponseSlots correlate(crate.types, slots);
                auto resolveInput = [&](const HIRTypeData* input) {
                    const auto* infer = input->opt_Infer();
                    return infer && infer->index == ~0u ? input : resolve_.ivars.getType(input);
                };
                correlate.correlateType(resolveInput(type), raw.getImplType(crate.types));
                auto responseParams = raw.getTraitParams(crate.types);
                if (params.types.size() == responseParams.types.size()) {
                    for (size_t i = 0; i < params.types.size(); i++) {
                        correlate.correlateType(resolveInput(params.types[i]), responseParams.types[i]);
                    }
                }
                return monomorphSolverImpl(source, correlate);
            }

            ImplRef monomorphSolverImplForLegacy(const SolverImpl& source, const Monomorphiser& monomorph) const {
                if (source.traitImpl) {
                    ASSERT_BUG(span(), source.trait, "trait impl solver response has no trait declaration");
                    auto result = ImplRef(monomorph.monomorphPathParams(span(), source.implParams, true), *source.trait, source.traitPath, *source.traitImpl);
                    if (source.ambiguousIdentity) {
                        result.markAmbiguousIdentity();
                    }
                    return result;
                }
                HIRTraitPath::assocListT associated;
                for (const auto& entry : source.associated) {
                    associated.insert({entry.first, monomorph.monomorphTpAtyEqual(span(), entry.second, true)});
                }
                auto result = ImplRef(
                    monomorph.monomorphType(span(), source.type, true),
                    monomorph.monomorphPathParams(span(), source.traitArgs, true),
                    ::std::move(associated),
                    source.constness
                );
                if (source.ambiguousIdentity) {
                    result.markAmbiguousIdentity();
                }
                return result;
            }

            SolverResponse monomorphSolverResponse(const SolverResponse& source, const Monomorphiser& monomorph, bool includeObligations = true) const {
                SolverResponse result;
                result.certainty = source.certainty;
                result.operatorSummary = source.operatorSummary;
                result.hasImpl = source.hasImpl;
                if (source.hasImpl) {
                    ASSERT_BUG(span(), source.impl, "solver response has no implementation");
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
                        result.obligations.push_back(SolverObligation{
                            monomorph.monomorphType(span(), obligation.type, true),
                            monomorph.monomorphTraitpath(span(), obligation.trait, true),
                        });
                    }
                }
                for (const auto& equality : source.equalities) {
                    result.equalities.push_back(SolverTypeEquality{
                        monomorph.monomorphType(span(), equality.left, true),
                        monomorph.monomorphType(span(), equality.right, true),
                    });
                }
                for (const auto& equality : source.valueEqualities) {
                    result.valueEqualities.push_back(SolverValueEquality{
                        monomorph.monomorphConstgeneric(span(), equality.left, true),
                        monomorph.monomorphConstgeneric(span(), equality.right, true),
                    });
                }
                return result;
            }

            SolverSlotValues extractSlotValues(const CanonicalGoal& goal, const ImplRef& response, const CanonicalizeTraitGoal& canonicalizer, Certainty certainty) const {
                SolverSlotValues result;
                if (canonicalizer.typeSlotCount() == 0 && canonicalizer.valueSlotCount() == 0) {
                    return result;
                }

                // Slot extraction is a read-only derivation of the canonical
                // answer.  Use a private table so fresh probe variables do not
                // advance the caller table's mutation generation and evict the
                // response we are about to cache.
                HMTypeInferrence table(crate.types);

                for (size_t i = 0; i < canonicalizer.typeSlotCount(); i++) {
                    result.typeInputs.push_back(canonicalizer.canonicalTypeSlot(i));
                }
                for (size_t i = 0; i < canonicalizer.valueSlotCount(); i++) {
                    result.valueInputs.push_back(canonicalizer.canonicalValueSlot(i));
                }

                class InstantiateSlots final: public MonomorphiserNop {
                    const CanonicalizeTraitGoal& canonicalizer_;
                    RcString foreignTypeName_;
                    RcString foreignValueName_;

                public:
                    ThinVector<HIRTypeRef> types;
                    ThinVector<HIRConstGeneric> values;
                    mutable ThinVector<HIRTypeRef> foreignTypes;
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

                    HIRTypeRef monomorphType(const Span& sp, const HIRTypeData* type, bool allowInfer = true) const override {
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

                ThinVector<HIRTypeRef> directTypeValues(canonicalizer.typeSlotCount());
                for (auto& value : directTypeValues) {
                    value = HIRTypeRef();
                }

                const auto responseType = response.getImplType(crate.types);
                const auto responseParams = response.getTraitParams(crate.types);
                if (goal.params.types.size() != responseParams.types.size() || goal.params.values.size() != responseParams.values.size()) {
                    ASSERT_BUG(span(), certainty == Certainty::Ambiguous, "proven solver response has a different trait arity than its goal: goal=" << goal.params << " response=" << responseParams << " impl=" << response);
                    for (size_t i = 0; i < canonicalizer.typeSlotCount(); i++) {
                        result.types.push_back(canonicalizer.canonicalTypeSlot(i));
                    }
                    for (size_t i = 0; i < canonicalizer.valueSlotCount(); i++) {
                        result.values.push_back(canonicalizer.canonicalValueSlot(i));
                    }
                    return result;
                }

                Unifier unifier(span(), table, nullptr, true);
                bool responseMismatch = false;
                // Ordinary unification treats unresolved projections as
                // opaque. Response extraction has a narrower job: matching
                // projection constructors expose which input slot an
                // existential response variable came from. Walk those equal
                // nominal/projection shells so the relation survives in the
                // explicit slot assignment.
                auto unifyInstantiatedType = [&](this auto&& self, const HIRTypeData* left, const HIRTypeData* right) -> void {
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
                    if (leftNominal && rightNominal
                        && leftNominal->path == rightNominal->path
                        && leftNominal->params.types.size() == rightNominal->params.types.size()
                        && leftNominal->params.values.size() == rightNominal->params.values.size()) {
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
                    if (leftProjection && rightProjection
                        && leftProjection->trait.path == rightProjection->trait.path
                        && leftProjection->item == rightProjection->item
                        && leftProjection->trait.params.types.size() == rightProjection->trait.params.types.size()
                        && leftProjection->trait.params.values.size() == rightProjection->trait.params.values.size()
                        && leftProjection->params.types.size() == rightProjection->params.types.size()
                        && leftProjection->params.values.size() == rightProjection->params.values.size()) {
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
                auto unifyType = [&](const HIRTypeData* left, const HIRTypeData* right) {
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
                    // Some proven environment/builtin responses intentionally
                    // keep an existential placeholder where the canonical
                    // input has a literal-class variable.  They prove the
                    // predicate but contribute no slot constraint.
                    for (size_t i = 0; i < canonicalizer.typeSlotCount(); i++) {
                        result.types.push_back(canonicalizer.canonicalTypeSlot(i));
                    }
                    for (size_t i = 0; i < canonicalizer.valueSlotCount(); i++) {
                        result.values.push_back(canonicalizer.canonicalValueSlot(i));
                    }
                    return result;
                }

                class MaterializeSlots final: public MonomorphiserNop {
                    const HMTypeInferrence& table_;
                    const CanonicalizeTraitGoal& canonicalizer_;
                    const ThinVector<HIRTypeRef>& types_;
                    const ThinVector<HIRConstGeneric>& values_;
                    const RcString foreignTypeName_;
                    const RcString foreignValueName_;
                    const ThinVector<HIRTypeRef>& foreignTypes_;
                    const ThinVector<HIRConstGeneric>& foreignValues_;

                public:
                    MaterializeSlots(HIRTypeInterner& interner, const HMTypeInferrence& table, const CanonicalizeTraitGoal& canonicalizer, const ThinVector<HIRTypeRef>& types, const ThinVector<HIRConstGeneric>& values, const ThinVector<HIRTypeRef>& foreignTypes, const ThinVector<HIRConstGeneric>& foreignValues)
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

                    HIRTypeRef getType(const Span& sp, const HIRGenericRef& generic) const override {
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

                    HIRTypeRef monomorphType(const Span& sp, const HIRTypeData* type, bool allowInfer = true) const override {
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

            static bool goalMatches(const GoalKey& goal, const HIRSimplePath& trait, const HIRPathParams& params, const HIRTypeData* type, const HIRTraitPath::assocListT* associated) {
                if (goal.trait != trait || goal.params != params || goal.type != type) {
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

            CachedGoal* findCachedGoal(size_t hash, const HIRSimplePath& trait, const HIRPathParams& params, const HIRTypeData* type, const HIRTraitPath::assocListT* associated) const {
                const auto range = goalCacheIndex.equal_range(hash);
                for (auto it = range.first; it != range.second; ++it) {
                    if (goalMatches(it->second->goal, trait, params, type, associated)) {
                        return it->second;
                    }
                }
                return nullptr;
            }

            GoalKey* findActiveGoal(size_t hash, const HIRSimplePath& trait, const HIRPathParams& params, const HIRTypeData* type, const HIRTraitPath::assocListT* associated) const {
                const auto range = activeGoalIndex.equal_range(hash);
                for (auto it = range.first; it != range.second; ++it) {
                    if (goalMatches(*it->second, trait, params, type, associated)) {
                        return it->second;
                    }
                }
                return nullptr;
            }

            GoalKey* pushActiveGoal(size_t hash, const HIRSimplePath& trait, const HIRPathParams& params, const HIRTypeData* type, const HIRTraitPath::assocListT* associated) {
                auto* goal = activeGoalNodes.make(hash, trait, params, type, associated);
                goalStack.push_back(goal);
                activeGoalIndex.emplace(hash, goal);
                return goal;
            }

            void popActiveGoal(GoalKey* goal) {
                assert(!goalStack.empty() && goalStack.back() == goal);
                const auto range = activeGoalIndex.equal_range(goal->hash);
                for (auto it = range.first; it != range.second; ++it) {
                    if (it->second == goal) {
                        activeGoalIndex.erase(it);
                        goalStack.pop_back();
                        activeGoalNodes.release(goal);
                        return;
                    }
                }
                assert(!"next-solver active goal missing from hash index");
                ::std::abort();
            }

            Certainty cacheGoal(size_t hash, const HIRSimplePath& trait, const HIRPathParams& params, const HIRTypeData* type, const HIRTraitPath::assocListT* associated, Certainty certainty, bool persistent = false) {
                auto* goal = cachedGoalNodes.make(hash, trait, params, type, associated, certainty);
                goal->persistent = persistent;
                goalCache.push_back(goal);
                goalCacheIndex.emplace(hash, goal);
                return certainty;
            }

            CachedGoal* cacheResponse(size_t hash, const HIRSimplePath& trait, const HIRPathParams& params, const HIRTypeData* type, const HIRTraitPath::assocListT* associated, const SolverResponse* response) {
                ASSERT_BUG(span(), response, "cannot cache an empty solver response");
                auto* cached = findCachedGoal(hash, trait, params, type, associated);
                const auto certainty = response->certainty;
                if (!cached) {
                    cached = cachedGoalNodes.make(hash, trait, params, type, associated, certainty);
                    goalCache.push_back(cached);
                    goalCacheIndex.emplace(hash, cached);
                }
                cached->certainty = certainty;
                cached->response = response;
                cached->hasResponse = true;
                return cached;
            }

            void clearGoalCache() {
                goalCacheIndex.clear();
                size_t kept = 0;
                for (auto* goal : goalCache) {
                    if (goal->persistent) {
                        goalCache[kept++] = goal;
                        goalCacheIndex.emplace(goal->goal.hash, goal);
                    } else {
                        cachedGoalNodes.release(goal);
                    }
                }
                goalCache.resize(kept);
            }

            static bool canonicalGoalIsRigid(const CanonicalGoal& canonical) {
                auto typeIsRigid = [](const HIRTypeData* ty) {
                    return !visitTyWith(ty, [](const HIRTypeData* inner) {
                        if (inner->is_Infer()) {
                            return true;
                        }
                        // Canonical placeholders are position-normalised
                        // per evaluation: identical keys can name different
                        // tentative variables across evaluations.
                        if (const auto* generic = inner->opt_Generic(); generic && generic->group() == GENERICPlaceholder) {
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
                    if (value.is_Generic() && value.as_Generic().group() == GENERICPlaceholder) {
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

            static const HIRTraitPath::assocListT* boundedAssociated(const ImplRef& impl) {
                if (const auto* bounded = impl.data.opt_BoundedPtr()) {
                    return bounded->assoc;
                }
                return &impl.data.as_Bounded().assoc;
            }

            static bool associatedResponsesEqual(const HIRTraitPath::assocListT* left, const HIRTraitPath::assocListT* right) {
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

            bool isSameImpl(const ImplRef& left, const ImplRef& right) const {
                const auto* li = left.data.opt_TraitImpl();
                const auto* ri = right.data.opt_TraitImpl();
                if (li || ri) {
                    return li && ri && li->impl == ri->impl && li->implParams == ri->implParams;
                }
                return left.getImplType(crate.types) == right.getImplType(crate.types) && left.getTraitParams(crate.types) == right.getTraitParams(crate.types) && associatedResponsesEqual(boundedAssociated(left), boundedAssociated(right));
            }

            bool paramEnvCandidateIsNonGlobal(const Candidate& candidate) const {
                if (candidate.source != CandidateSource::ParamEnv) {
                    return false;
                }
                auto typeIsNonGlobal = [&](const HIRTypeData* type) {
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
                // An alias bound -- a predicate whose self is a rigid
                // projection -- is not a where-clause at all: rustc keeps
                // alias-bound candidates preferred over impls regardless of
                // globalness (the ATB `Output: Into<u8>` on an opaque's
                // associated chain must win over the blanket Into impl).
                {
                    const auto* implSelf = resolve_.resolveType(candidate.impl.getImplType(crate.types));
                    if (const auto* selfPath = implSelf->opt_Path(); selfPath && selfPath->binding.is_Opaque()) {
                        return true;
                    }
                }
                if (typeIsNonGlobal(candidate.impl.getImplType(crate.types)) || paramsAreNonGlobal(candidate.impl.getTraitParams(crate.types))) {
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

            void pushCandidate(size_t frameIndex, ImplRef impl, HIRCompare match, Certainty headRelation, const HIRMarkerImpl* markerImpl = nullptr, HIRPathParams markerImplParams = {}, bool autoBuiltin = false, CandidateSource source = CandidateSource::Other, bool headNormalizationAmbiguity = false, ThinVector<SolverTypeEquality> headEqualities = {}, ThinVector<SolverValueEquality> headValueEqualities = {}) {
                if (match == HIRCompare::Unequal) {
                    return;
                }
                auto& candidates = frames[frameIndex]->candidates;
                for (size_t i = 0; i < candidates.size(); i++) {
                    const bool sameSource = candidates[i]->markerImpl == markerImpl && candidates[i]->autoBuiltin == autoBuiltin && candidates[i]->source == source;
                    const bool same = markerImpl ? sameSource && candidates[i]->markerImplParams == markerImplParams : sameSource && isSameImpl(candidates[i]->impl, impl);
                    if (same) {
                        candidates[i]->headMatch &= match;
                        if (headRelation != Certainty::Proven) {
                            candidates[i]->headRelation = Certainty::Ambiguous;
                        }
                        candidates[i]->headNormalizationAmbiguity |= headNormalizationAmbiguity;
                        for (auto& equality : headEqualities) {
                            candidates[i]->headEqualities.push_back(::std::move(equality));
                        }
                        for (auto& equality : headValueEqualities) {
                            candidates[i]->headValueEqualities.push_back(::std::move(equality));
                        }
                        return;
                    }
                }
                candidates.push_back(candidateNodes.make(::std::move(impl), match, headRelation, markerImpl, ::std::move(markerImplParams), autoBuiltin, source, headNormalizationAmbiguity, ::std::move(headEqualities), ::std::move(headValueEqualities)));
            }

            Certainty relateAssembledHead(
                const HIRPathParams& goalParams,
                const HIRTypeData* goalType,
                const ImplRef& impl,
                bool& headNormalizationAmbiguity,
                ThinVector<SolverTypeEquality>& headEqualities,
                ThinVector<SolverValueEquality>& headValueEqualities
            ) {
                const auto candidateType = impl.getImplType(crate.types);
                const auto candidateParams = impl.getTraitParams(crate.types);
                if (candidateParams.types.size() != goalParams.types.size() || candidateParams.values.size() != goalParams.values.size()) {
                    return Certainty::NoSolution;
                }

                const auto snapshot = resolve_.ivars.snapshot();
                STD_DEFER {
                    resolve_.ivars.rollbackTo(snapshot);
                };

                Unifier unifier(span(), resolve_.ivars, &resolve_);
                auto relation = unifier.unify(goalType, candidateType);
                if (relation == Unifier::Outcome::Mismatch) {
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

                // These are the selected head's inference effects.  Record
                // the relation inputs themselves rather than reconstructing
                // them later from a fuzzy callback result.
                if (goalType != candidateType) {
                    headEqualities.push_back(SolverTypeEquality{goalType, candidateType});
                }
                for (size_t i = 0; i < candidateParams.types.size(); i++) {
                    if (goalParams.types[i] != candidateParams.types[i]) {
                        headEqualities.push_back(SolverTypeEquality{goalParams.types[i], candidateParams.types[i]});
                    }
                }
                for (size_t i = 0; i < candidateParams.values.size(); i++) {
                    if (goalParams.values[i] != candidateParams.values[i]) {
                        headValueEqualities.push_back(SolverValueEquality{goalParams.values[i].clone(), candidateParams.values[i].clone()});
                    }
                }
                headNormalizationAmbiguity = !unifier.pendingValues().empty();
                for (const auto& equality : unifier.pending()) {
                    headNormalizationAmbiguity |= resolve_.hasAssociatedType(equality.left) || resolve_.hasAssociatedType(equality.right);
                }
                return relation == Unifier::Outcome::Proven ? Certainty::Proven : Certainty::Ambiguous;
            }

            Certainty unifyImplHead(
                const HIRGenericParams& implParamsDef,
                const HIRPathParams& implTraitArgs,
                const HIRTypeData* implType,
                const HIRPathParams& goalParams,
                const HIRTypeData* goalType,
                HIRPathParams& outputParams,
                bool& headNormalizationAmbiguity,
                ThinVector<SolverTypeEquality>& headEqualities,
                ThinVector<SolverValueEquality>& headValueEqualities
            ) {
                const auto snapshot = resolve_.ivars.snapshot();
                STD_DEFER {
                    resolve_.ivars.rollbackTo(snapshot);
                };

                auto inferenceParams = resolve_.makeFreshImplParams(implParamsDef);
                auto monomorph = MonomorphStatePtr(crate.types, nullptr, &inferenceParams, nullptr);
                const auto candidateType = monomorph.monomorphType(span(), implType, true);

                Unifier unifier(span(), resolve_.ivars, &resolve_);
                // Put the longer-lived goal variables on the left.  Ivar
                // union aliases the right root to the left root, so a
                // coherence probe can materialize the temporary candidate
                // parameters in terms of the first impl's variables before
                // rolling its own snapshot back.
                auto relation = unifier.unify(goalType, candidateType);
                if (relation == Unifier::Outcome::Mismatch) {
                    return Certainty::NoSolution;
                }
                // Self commonly fixes every impl const parameter.  Rebuild
                // the trait arguments only after those bindings exist so a
                // concrete expression such as min(2, 3) is evaluated before
                // it is related to the goal.
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

                // Candidate storage outlives this inference snapshot.  Resolve
                // every bound existential now and represent the still-free
                // ones with the solver's stable candidate placeholders; no
                // inference-table index is allowed to escape the rollback.
                class MaterializeCandidate final: public MonomorphiserNop {
                    const HMTypeInferrence& table;
                    const HIRPathParams& inferenceParams;
                    const RcString placeholderName;

                public:
                    MaterializeCandidate(HIRTypeInterner& types, const HMTypeInferrence& table, const HIRPathParams& inferenceParams, const HIRGenericParams& implParamsDef)
                        : MonomorphiserNop(types)
                        , table(table)
                        , inferenceParams(inferenceParams)
                        , placeholderName(RcString::newInterned(FMT("impl_?_" << &implParamsDef)))
                    {
                    }

                    HIRTypeRef monomorphType(const Span& sp, const HIRTypeData* type, bool allowInfer = true) const override {
                        if (const auto* infer = type->opt_Infer()) {
                            for (size_t i = 0; i < inferenceParams.types.size(); i++) {
                                const auto* parameter = inferenceParams.types[i]->opt_Infer();
                                if (!parameter || parameter->index != infer->index) {
                                    continue;
                                }
                                const auto* resolved = table.getType(type);
                                if (resolved == type) {
                                    ASSERT_BUG(sp, i < 256, "Too many candidate type parameters");
                                    return types.generic(placeholderName, GENERICPlaceholder * 256 + static_cast<unsigned>(i));
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
                                    ASSERT_BUG(sp, i < 256, "Too many candidate value parameters");
                                    return HIRGenericRef(placeholderName, GENERICPlaceholder * 256 + static_cast<unsigned>(i));
                                }
                                return this->monomorphConstgeneric(sp, resolved, allowInfer);
                            }
                        }
                        return Monomorphiser::monomorphConstgeneric(sp, value, allowInfer);
                    }
                };

                MaterializeCandidate materialize(crate.types, resolve_.ivars, inferenceParams, implParamsDef);
                outputParams = materialize.monomorphPathParams(span(), inferenceParams, true);
                headNormalizationAmbiguity = !unifier.pendingValues().empty();
                for (const auto& equality : unifier.pending()) {
                    headNormalizationAmbiguity |= resolve_.hasAssociatedType(equality.left) || resolve_.hasAssociatedType(equality.right);
                    headEqualities.push_back(SolverTypeEquality{
                        materialize.monomorphType(span(), equality.left, true),
                        materialize.monomorphType(span(), equality.right, true),
                    });
                }
                for (const auto& equality : unifier.pendingValues()) {
                    headValueEqualities.push_back(SolverValueEquality{
                        materialize.monomorphConstgeneric(span(), equality.left, true),
                        materialize.monomorphConstgeneric(span(), equality.right, true),
                    });
                }
                return relation == Unifier::Outcome::Proven ? Certainty::Proven : Certainty::Ambiguous;
            }

            void assembleAliasBoundCandidates(size_t frameIndex, const HIRSimplePath& trait, const HIRPathParams& params, const HIRTypeData* type) {
                const auto* path = type->opt_Path();
                const auto* projection = path ? path->path.data.opt_UfcsKnown() : nullptr;
                if (!projection) {
                    return;
                }

                HIRGenericPath declaringTrait;
                if (!resolve_.traitContainsType(span(), projection->trait, crate.getTraitByPath(span(), projection->trait.path), projection->item.c_str(), declaringTrait)) {
                    BUG(span(), "Cannot find associated type " << projection->item << " anywhere in trait " << projection->trait);
                }
                const auto& declaration = crate.getTraitByPath(span(), declaringTrait.path).types.at(projection->item);
                auto monomorph = MonomorphStatePtr(crate.types, projection->type, &declaringTrait.params, &projection->params);

                auto emit = [&](HIRTraitPath response) {
                    auto match = response.path.params.compareWithPlaceholders(span(), params, resolve_.ivars.callbackResolveInfer());
                    if (match != HIRCompare::Unequal) {
                        auto impl = ImplRef(type, ::std::move(response.path.params), ::std::move(response.typeBounds), response.constness);
                        bool headNormalizationAmbiguity = false;
                        ThinVector<SolverTypeEquality> headEqualities;
                        ThinVector<SolverValueEquality> headValueEqualities;
                        const auto relation = relateAssembledHead(params, type, impl, headNormalizationAmbiguity, headEqualities, headValueEqualities);
                        if (relation == Certainty::NoSolution) {
                            return;
                        }
                        pushCandidate(
                            frameIndex,
                            ::std::move(impl),
                            match,
                            relation,
                            nullptr,
                            {},
                            false,
                            CandidateSource::AliasBound,
                            headNormalizationAmbiguity,
                            ::std::move(headEqualities),
                            ::std::move(headValueEqualities)
                        );
                    }
                };

                for (const auto& declaredBound : declaration.traitBounds) {
                    auto bound = monomorph.monomorphTraitpath(span(), declaredBound, false);
                    if (bound.path.path == trait) {
                        emit(::std::move(bound));
                        continue;
                    }

                    const auto& boundDefinition = crate.getTraitByPath(span(), bound.path.path);
                    resolve_.findNamedTraitInTrait(span(), trait, params, boundDefinition, bound.path.path, bound.path.params, type, [&](const HIRTraitPath& parent) {
                        auto response = parent.clone();
                        // An equality can be written through a subtrait even
                        // though the item is declared by this parent trait:
                        // `Int<Unsigned = Self>` carries a `MinInt::Unsigned`
                        // equality.  The enumerated parent path contains the
                        // inherited declaration but not those use-site
                        // equalities, so project the matching entries onto the
                        // candidate that will answer NormalizesTo.
                        for (const auto& associated : bound.typeBounds) {
                            if (associated.second.sourceTrait.path != trait
                                || resolve_.comparePp(span(), associated.second.sourceTrait.params, response.path.params) == HIRCompare::Unequal) {
                                continue;
                            }
                            response.typeBounds.erase(associated.first);
                            response.typeBounds.insert({associated.first, associated.second.clone()});
                        }
                        emit(::std::move(response));
                        return false;
                    });
                }
            }

            void assembleCandidates(size_t frameIndex, const HIRSimplePath& trait, const HIRPathParams& params, const HIRTypeData* type, bool includeMagicCandidates = true) {
                // A rigid projection self only matches environment-class
                // candidates: bounds on the associated type's declaration
                // (alias bounds) surface through the legacy collector, but
                // they are the same preference class as ParamEnv predicates.
                // Mislabelling them lets the non-global-ParamEnv shadow drop
                // `CastFrom<u16>`-style alias bounds and commit the one
                // declared directly, guiding inference to the wrong type.
                const auto* selfPath = resolve_.resolveType(type)->opt_Path();
                const bool selfIsRigidProjection = selfPath && selfPath->binding.is_Opaque();
                auto collect = [&](CandidateSource source) {
                    return [&, source, selfIsRigidProjection](ImplRef impl, HIRCompare match) {
                        auto effectiveSource = source;
                        if (source == CandidateSource::Other && selfIsRigidProjection && !impl.data.is_TraitImpl()) {
                            effectiveSource = CandidateSource::ParamEnv;
                        }
                        // Legacy assembly only discovers the shape and source.
                        // The same Unifier relation used for impl heads decides
                        // compatibility and produces the typed response effects.
                        bool headNormalizationAmbiguity = false;
                        ThinVector<SolverTypeEquality> headEqualities;
                        ThinVector<SolverValueEquality> headValueEqualities;
                        const auto relation = relateAssembledHead(params, type, impl, headNormalizationAmbiguity, headEqualities, headValueEqualities);
                        if (relation == Certainty::NoSolution) {
                            return false;
                        }
                        pushCandidate(frameIndex, ::std::move(impl), match, relation, nullptr, {}, false, effectiveSource, headNormalizationAmbiguity, ::std::move(headEqualities), ::std::move(headValueEqualities));
                        return false;
                    };
                };

                // Candidate source is semantically significant: a non-global
                // ParamEnv predicate shadows builtin and impl candidates in the
                // next solver.  The legacy lookup flattened these sources into
                // the same bounded ImplRef, so collect each source independently.
                if (includeMagicCandidates) {
                    resolve_.assembleMagicCandidates(span(), trait, params, type, collect(CandidateSource::Builtin));
                }
                resolve_.assembleOtherCandidates(span(), trait, params, type, collect(CandidateSource::Other));
                resolve_.assembleParamEnvCandidates(span(), trait, params, type, collect(CandidateSource::ParamEnv));
                assembleAliasBoundCandidates(frameIndex, trait, params, type);

                const auto& resolvedType = resolve_.resolveType(type);
                const auto& traitDef = crate.getTraitByPath(span(), trait);
                if (!traitDef.isMarker) {
                    // Assemble impl heads without evaluating their where-clauses.
                    // Those nested goals belong exclusively to evaluate_candidate.
                    crate.findTraitImpls(trait, resolvedType, resolve_.ivars.callbackResolveInfer(), [&](const HIRTraitImpl& impl) {
                        // A reservation impl only reserves coherence space for a
                        // possible future implementation. Outside coherence it
                        // is not a candidate and must never provide an item.
                        if (impl.isReservation) {
                            return false;
                        }
                        HIRPathParams implParams;
                        bool headNormalizationAmbiguity = false;
                        ThinVector<SolverTypeEquality> headEqualities;
                        ThinVector<SolverValueEquality> headValueEqualities;
                        const auto relation = this->unifyImplHead(impl.params, impl.traitArgs, impl.type, params, resolvedType, implParams, headNormalizationAmbiguity, headEqualities, headValueEqualities);
                        if (relation != Certainty::NoSolution) {
                            pushCandidate(frameIndex, ImplRef(::std::move(implParams), traitDef, trait, impl), relation == Certainty::Proven ? HIRCompare::Equal : HIRCompare::Fuzzy, relation, nullptr, {}, false, CandidateSource::TraitImpl, headNormalizationAmbiguity, ::std::move(headEqualities), ::std::move(headValueEqualities));
                        }
                        return false;
                    });
                } else {
                    // Explicit positive and negative auto-trait impls are
                    // candidates with polarity.  Only their heads are matched
                    // here; their bounds are nested goals evaluated below.
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
                            pushCandidate(frameIndex, ImplRef(::std::move(responseType), ::std::move(responseParams), HIRTraitPath::assocListT()), relation == Certainty::Proven ? HIRCompare::Equal : HIRCompare::Fuzzy, relation, &impl, ::std::move(implParams), false, CandidateSource::TraitImpl, headNormalizationAmbiguity, ::std::move(headEqualities), ::std::move(headValueEqualities));
                        }
                        return false;
                    });

                    // The structural auto candidate is evaluated recursively in
                    // evaluate_candidate, after explicit polarity is known.  A
                    // non-builtin query deliberately keeps only declared impls
                    // and ParamEnv candidates; re-adding this candidate there
                    // makes typeIsCopy/typeIsClone ask themselves recursively.
                    if (includeMagicCandidates) {
                        const auto structuralRelation = resolve_.typeContainsIvars(resolvedType) || resolve_.paramsContainIvars(params) ? Certainty::Ambiguous : Certainty::Proven;
                        pushCandidate(frameIndex, ImplRef(resolvedType, params.clone(), HIRTraitPath::assocListT()), structuralRelation == Certainty::Proven ? HIRCompare::Equal : HIRCompare::Fuzzy, structuralRelation, nullptr, {}, true, CandidateSource::Builtin);
                    }
                }
            }

            HIRTypeRef makeAssociatedProjection(const HIRTypeData* type, const HIRGenericPath& sourceTrait, const RcString& name, const HIRPathParams& associatedParams) const {
                return crate.types.path(HIRPath(type, sourceTrait.clone(), name, associatedParams.clone()), HIRTypePathBinding::make_Opaque({}));
            }

            HIRTypeRef makeAssociatedProjection(const ImplRef& impl, const HIRGenericPath& sourceTrait, const RcString& name, const HIRPathParams& associatedParams) const {
                return makeAssociatedProjection(impl.getImplType(crate.types), sourceTrait, name, associatedParams);
            }

            struct CandidateTypeBinding {
                HIRTypeRef stable;
                HIRTypeRef probe;
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

            // Relate a candidate pattern to one or more response values as a
            // single inference-table transaction.  Stable candidate
            // placeholders are first instantiated as fresh ivars.  A
            // successful relation is materialised back into stable params
            // before every probe slot is rolled back; a mismatch leaves the
            // candidate byte-for-byte unchanged.
            template <typename Relate>
            CandidateBindingResult unifyCandidateParams(HIRPathParams& params, Relate relate) {
                const auto original = params.clone();
                const auto snapshot = resolve_.ivars.snapshot();
                STD_DEFER {
                    resolve_.ivars.rollbackTo(snapshot);
                };

                stl::Vector<CandidateTypeBinding> typeBindings;
                stl::Vector<CandidateValueBinding> valueBindings;

                const auto addTypeBinding = [&](const HIRTypeData* type) {
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
                    visitTyWith(type, [&](const HIRTypeData* inner) {
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

                class InstantiateCandidate final: public MonomorphiserNop {
                    const stl::Vector<CandidateTypeBinding>& typeBindings_;
                    const stl::Vector<CandidateValueBinding>& valueBindings_;

                public:
                    InstantiateCandidate(HIRTypeInterner& types, const stl::Vector<CandidateTypeBinding>& typeBindings, const stl::Vector<CandidateValueBinding>& valueBindings)
                        : MonomorphiserNop(types)
                        , typeBindings_(typeBindings)
                        , valueBindings_(valueBindings)
                    {
                    }

                    HIRTypeRef monomorphType(const Span& sp, const HIRTypeData* type, bool allowInfer = true) const override {
                        for (const auto& binding : typeBindings_) {
                            if (binding.stable == type) {
                                return binding.probe;
                            }
                        }
                        return MonomorphiserNop::monomorphType(sp, type, allowInfer);
                    }

                    HIRTypeRef getType(const Span& sp, const HIRGenericRef& generic) const override {
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
                            const bool matches = binding.isGeneric
                                ? generic && generic->name == binding.name && generic->binding == binding.stableIndex
                                : infer && infer->index == binding.stableIndex;
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
                Unifier unifier(span(), resolve_.ivars, &resolve_, true);

                class Relations {
                    const Span& span_;
                    InstantiateCandidate& instantiate_;
                    Unifier& unifier_;

                public:
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

                    void type(const HIRTypeData* candidate, const HIRTypeData* value) {
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

                class MaterializeCandidate final: public MonomorphiserNop {
                    const HMTypeInferrence& table_;
                    const stl::Vector<CandidateTypeBinding>& typeBindings_;
                    const stl::Vector<CandidateValueBinding>& valueBindings_;

                public:
                    MaterializeCandidate(HIRTypeInterner& types, const HMTypeInferrence& table, const stl::Vector<CandidateTypeBinding>& typeBindings, const stl::Vector<CandidateValueBinding>& valueBindings)
                        : MonomorphiserNop(types)
                        , table_(table)
                        , typeBindings_(typeBindings)
                        , valueBindings_(valueBindings)
                    {
                    }

                    HIRTypeRef monomorphType(const Span& sp, const HIRTypeData* type, bool allowInfer = true) const override {
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
                                    return binding.isGeneric
                                        ? HIRConstGeneric(HIRGenericRef(binding.name, binding.stableIndex))
                                        : HIRConstGeneric::make_Infer({binding.stableIndex});
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
                    params = ::std::move(output);
                }
                return changed ? CandidateBindingResult::Changed : CandidateBindingResult::Unchanged;
            }

            CandidateBindingResult bindCandidatePlaceholders(Candidate& candidate, const HIRTypeData* nestedType, const HIRTraitPath::assocListT& associated, bool useCandidateResponse = false) {
                HIRPathParams* candidateParams = nullptr;
                if (auto* traitImpl = candidate.impl.data.opt_TraitImpl()) {
                    candidateParams = &traitImpl->implParams;
                } else if (candidate.markerImpl) {
                    candidateParams = &candidate.markerImplParams;
                }
                if (!candidateParams || associated.empty()) {
                    return CandidateBindingResult::Unchanged;
                }

                bool changed = false;
                for (const auto& requirement : associated) {
                    auto candidateOutput = useCandidateResponse ? candidate.impl.getType(crate.types, requirement.first.c_str(), requirement.second.atyParams) : HIRTypeRef();
                    if (!useCandidateResponse) {
                        // An impl parameter can occur only in a nested projection
                        // equality (for example `I: Iterator<Item = &'a T>`).
                        // Ask the solver for that projection's actual response so
                        // `T` is bound to the response, not to the alias syntax.
                        auto nestedCallback = makeCallable<SolverResponseCb>([&](SolverResponse response) {
                            if (response.certainty != Certainty::Proven || !response.hasImpl || !response.impl || response.impl->ambiguousIdentity) {
                                return false;
                            }
                            auto impl = response.impl->legacy();
                            auto output = impl.getType(crate.types, requirement.first.c_str(), requirement.second.atyParams);
                            if (output == HIRTypeRef()) {
                                return false;
                            }
                            candidateOutput = ::std::move(output);
                            return true;
                        });
                        evaluateTyped(span(), requirement.second.sourceTrait.path, requirement.second.sourceTrait.params, nestedType, nestedCallback, {.assocName = requirement.first.c_str(), .assocParams = &requirement.second.atyParams});
                    }
                    if (candidateOutput == HIRTypeRef()) {
                        candidateOutput = makeAssociatedProjection(nestedType, requirement.second.sourceTrait, requirement.first, requirement.second.atyParams);
                    }
                    if (!useCandidateResponse && !typeHasUfcsUnknown(candidateOutput)) {
                        // rustc normalises a nested projection response before it
                        // is unified with the outer candidate.  In particular,
                        // `<&mut I as Iterator>::Item` first becomes
                        // `<I as Iterator>::Item` and then the ParamEnv equality
                        // `&T`; matching the unnormalised alias against
                        // `&placeholder` only reports a fuzzy relation and loses
                        // the constraint.  A candidate's own response is not a
                        // nested solver response: during Resolve UFCS Outer either
                        // form can still legally contain a local UfcsUnknown, and
                        // such a response must remain deferred until that pass
                        // resolves its trait path.
                        candidateOutput = resolve_.expandAssociatedTypes(span(), ::std::move(candidateOutput));
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
                    auto& response = candidate.impl.data.as_Bounded();
                    response.type = monomorph.monomorphType(span(), candidate.markerImpl->type, false);
                    response.traitArgs = monomorph.monomorphPathParams(span(), candidate.markerImpl->traitArgs, false);
                }
                return changed ? CandidateBindingResult::Changed : CandidateBindingResult::Unchanged;
            }

            CandidateBindingResult bindCandidateResponse(Candidate& candidate, const HIRTypeData* nestedType, const HIRPathParams& nestedParams, const HIRTraitPath::assocListT& nestedAssociated, const ImplRef& response) {
                HIRPathParams* candidateParams = nullptr;
                if (auto* traitImpl = candidate.impl.data.opt_TraitImpl()) {
                    candidateParams = &traitImpl->implParams;
                } else if (candidate.markerImpl) {
                    candidateParams = &candidate.markerImplParams;
                }
                if (!candidateParams || response.isAmbiguousIdentity()) {
                    return CandidateBindingResult::Unchanged;
                }

                const auto binding = this->unifyCandidateParams(*candidateParams, [&](auto& relations) {
                    relations.type(nestedType, response.getImplType(crate.types));
                    relations.pathParams(nestedParams, response.getTraitParams(crate.types));
                    for (const auto& requirement : nestedAssociated) {
                        auto output = response.getType(crate.types, requirement.first.c_str(), requirement.second.atyParams);
                        if (output == HIRTypeRef()) {
                            relations.mismatch();
                            break;
                        }
                        relations.type(requirement.second.type, output);
                    }
                });

                if (binding == CandidateBindingResult::Changed && candidate.markerImpl) {
                    auto monomorph = MonomorphStatePtr(crate.types, nullptr, &candidate.markerImplParams, nullptr);
                    auto& bounded = candidate.impl.data.as_Bounded();
                    bounded.type = monomorph.monomorphType(span(), candidate.markerImpl->type, false);
                    bounded.traitArgs = monomorph.monomorphPathParams(span(), candidate.markerImpl->traitArgs, false);
                }
                return binding;
            }

            Certainty unifyProbe(const HIRTypeData* left, const HIRTypeData* right) {
                if (left == right) {
                    return Certainty::Proven;
                }

                // Candidate evaluation must be a pure probe until a typed
                // response can carry its bindings back to the caller.  Run
                // the real unifier on the inference table, observe whether
                // proving the equality needed a binding, then restore the
                // exact input state in every case.
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

            Certainty unifyValueProbe(const HIRConstGeneric& left, const HIRConstGeneric& right) {
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

            void appendRelationEffects(Candidate& candidate, SolverResponse response) {
                for (size_t i = 0; i < response.slots.typeInputs.size(); i++) {
                    if (response.slots.typeInputs[i] != response.slots.types[i]) {
                        candidate.relationEqualities.push_back(SolverTypeEquality{
                            response.slots.typeInputs[i],
                            response.slots.types[i],
                        });
                    }
                }
                for (size_t i = 0; i < response.slots.valueInputs.size(); i++) {
                    if (response.slots.valueInputs[i] != response.slots.values[i]) {
                        candidate.relationValueEqualities.push_back(SolverValueEquality{
                            response.slots.valueInputs[i].clone(),
                            response.slots.values[i].clone(),
                        });
                    }
                }
                for (auto& equality : response.equalities) {
                    candidate.relationEqualities.push_back(::std::move(equality));
                }
                for (auto& equality : response.valueEqualities) {
                    candidate.relationValueEqualities.push_back(::std::move(equality));
                }
                for (auto& obligation : response.obligations) {
                    candidate.relationObligations.push_back(::std::move(obligation));
                }
            }

            Certainty relateTypes(Candidate& candidate, const HIRTypeData* left, const HIRTypeData* right) {
                if (left == right) {
                    return Certainty::Proven;
                }

                const auto snapshot = resolve_.ivars.snapshot();
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
                resolve_.ivars.rollbackTo(snapshot);

                if (outcome == Unifier::Outcome::Mismatch) {
                    return Certainty::NoSolution;
                }

                // The transaction above proved this relation, including any
                // caller-ivar bindings.  Preserve it as response data before
                // rollback; only the selected candidate may apply it.
                candidate.relationEqualities.push_back(SolverTypeEquality{left, right});
                if (outcome == Unifier::Outcome::Proven) {
                    return Certainty::Proven;
                }

                Certainty result = pendingValues.empty() ? Certainty::Proven : Certainty::Ambiguous;
                for (const auto& equality : pending) {
                    const auto isSolverExistential = [](const HIRTypeData* type) {
                        const auto* infer = type->opt_Infer();
                        return infer && isSolverCanonicalInfer(infer->index);
                    };
                    if (isSolverExistential(equality.left) || isSolverExistential(equality.right)) {
                        // Canonical slots are the existential inputs/outputs
                        // of this solver query.  Equating one with a selected
                        // candidate value is a proven response effect, not
                        // structural uncertainty.  Ordinary placeholders and
                        // alias-input infer nodes remain rigid and therefore
                        // ambiguous here.
                        continue;
                    }
                    struct ProjectionRelation {
                        bool isProjection;
                        Certainty certainty;
                    };
                    auto relateProjection = [&](const HIRTypeData* alias, const HIRTypeData* other) -> ProjectionRelation {
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
                            appendRelationEffects(candidate, ::std::move(response.effects));
                            if (response.output != HIRTypeRef()) {
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

                    auto nested = relateProjection(equality.left, equality.right);
                    if (!nested.isProjection) {
                        nested = relateProjection(equality.right, equality.left);
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

            Certainty evaluateHeadEquality(Candidate& candidate, const SolverTypeEquality& equality) {
                const auto normalizedLeft = normalizeGoalInput(equality.left);
                const auto normalizedRight = normalizeGoalInput(equality.right);
                const auto relation = this->relateTypes(candidate, normalizedLeft, normalizedRight);
                if (relation == Certainty::Proven) {
                    return relation;
                }

                bool sawAlias = false;
                bool failed = false;
                auto checkAliasBounds = [&](const HIRTypeData* alias, const HIRTypeData* replacement) {
                    const auto* path = alias->opt_Path();
                    const auto* projection = path ? path->path.data.opt_UfcsKnown() : nullptr;
                    if (!projection) {
                        return;
                    }
                    sawAlias = true;

                    HIRGenericPath declaringTrait;
                    if (!resolve_.traitContainsType(span(), projection->trait, crate.getTraitByPath(span(), projection->trait.path), projection->item.c_str(), declaringTrait)) {
                        BUG(span(), "Cannot find associated type " << projection->item << " anywhere in trait " << projection->trait);
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
                            candidate.headObligations.push_back(SolverObligation{replacement, ::std::move(bound)});
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

            Certainty matchAssociatedTypes(const HIRSimplePath& trait, Candidate& candidate, const HIRTraitPath::assocListT* associated) {
                if (!associated || associated->empty()) {
                    return Certainty::Proven;
                }

                const auto& impl = candidate.impl;
                Certainty result = Certainty::Proven;
                for (const auto& requirement : *associated) {
                    const auto& aty = requirement.second;
                    if (!impl.data.is_TraitImpl() && aty.atyParams.hasParams()) {
                        // Bounded candidates currently store non-GAT projections.
                        // They remain a valid but non-guiding response instead of
                        // being rejected or calling ImplRef's non-GAT assertion.
                        result = Certainty::Ambiguous;
                        continue;
                    }
                    auto output = impl.getType(crate.types, requirement.first.c_str(), aty.atyParams);
                    if (output == HIRTypeRef()) {
                        if (aty.sourceTrait.path != trait) {
                            HIRTraitPath::assocListT sourceAssociated;
                            sourceAssociated.insert({requirement.first, requirement.second.clone()});
                            const auto sourceResult = solveGoal(aty.sourceTrait.path, aty.sourceTrait.params, impl.getImplType(crate.types), &sourceAssociated);
                            if (sourceResult == Certainty::NoSolution) {
                                return Certainty::NoSolution;
                            }
                            if (sourceResult == Certainty::Ambiguous) {
                                result = Certainty::Ambiguous;
                            }
                            continue;
                        }
                        if (impl.data.is_TraitImpl()) {
                            result = Certainty::Ambiguous;
                            continue;
                        }
                        // A ParamEnv predicate without an explicit equality still
                        // has a canonical projection response.  This is what lets
                        // `T: Trait` prove a nested `T: Trait<Assoc = U>` while
                        // constraining U to `<T as Trait>::Assoc`.
                        output = makeAssociatedProjection(impl, aty.sourceTrait, requirement.first, aty.atyParams);
                    }
                    // The projection response may contain the very caller-owned
                    // inference variable from the requested equality. That is an
                    // exact response, not an ambiguous comparison of two ivars.
                    if (containsDefiningOpaque(output) || containsDefiningOpaque(aty.type)) {
                        continue;
                    }
                    const auto relation = this->relateTypes(candidate, output, aty.type);
                    if (relation == Certainty::NoSolution) {
                        // `!` coerces into any requirement: a diverging
                        // closure's Output does not reject the candidate, the
                        // caller's coercion machinery settles it (rustc seeds
                        // the closure signature from the expectation instead).
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

            Certainty evaluateAutoBuiltin(const HIRSimplePath& trait, const HIRPathParams& params, const HIRTypeData* type) {
                auto combine = [](Certainty& result, Certainty nested) {
                    if (nested == Certainty::NoSolution) {
                        result = Certainty::NoSolution;
                    } else if (nested == Certainty::Ambiguous && result == Certainty::Proven) {
                        result = Certainty::Ambiguous;
                    }
                };
                auto evaluateInner = [&](const HIRTypeData* inner) {
                    return solveGoal(trait, params, inner, nullptr);
                };

            switch ((*type).tag()) {
default:
                return Certainty::Proven;
                case HIRTypeData::TAG_Path: {
                    auto& e = (*type).as_Path();
                    if (const auto* pe = e.path.data.opt_Generic()) {
                        HIRTypeRef tmp;
                        auto monomorph = MonomorphStatePtr(crate.types, nullptr, &pe->params, nullptr);
                        auto evaluateField = [&](const HIRTypeData* field) {
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
                case HIRTypeData::TAG_Generic: {
                    return evaluateInner(type);
                }
                case HIRTypeData::TAG_Tuple: {
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
                case HIRTypeData::TAG_Array: {
                    auto& e = (*type).as_Array();
                    return evaluateInner(e.inner);
                }
            }
            UNREACHABLE();
            }

            Certainty evaluateCandidate(size_t frameIndex, size_t candidateIndex, const HIRSimplePath& trait, const HIRTraitPath::assocListT* associated) {
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
                // Assembly exactness is a ranking fact, not proof.  Only the
                // unifier relation decides whether the head is proven.  An
                // ambiguous-identity builtin is explicitly non-committal even
                // when its response happens to repeat the goal verbatim.
                auto result = candidate->impl.isAmbiguousIdentity()
                    ? Certainty::Ambiguous
                    : candidate->headRelation;

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
                    const auto& response = candidate->impl.data.as_Bounded();
                    const auto structural = evaluateAutoBuiltin(trait, response.traitArgs, response.type);
                    if (structural == Certainty::NoSolution) {
                        return Certainty::NoSolution;
                    }
                    if (structural == Certainty::Ambiguous) {
                        candidate->ambiguityBeyondHead = true;
                        candidate->nestedAmbiguity = true;
                        result = Certainty::Ambiguous;
                    }
                }

                const auto assocResult = matchAssociatedTypes(trait, *candidate, associated);
                if (assocResult == Certainty::NoSolution) {
                    return Certainty::NoSolution;
                }
                if (assocResult == Certainty::Ambiguous) {
                    candidate->ambiguityBeyondHead = true;
                    result = Certainty::Ambiguous;
                }

                const auto* traitImpl = candidate->impl.data.opt_TraitImpl();
                const HIRGenericParams* implParamsDef = markerImpl ? &markerImpl->params : (traitImpl && traitImpl->impl ? &traitImpl->impl->params : nullptr);
                if (!implParamsDef) {
                    return result;
                }

                // An impl parameter without a `?Sized` relaxation carries an
                // implicit `Sized` requirement.  For a specialising impl this
                // is often the only distinguishing predicate (`Box<I>` vs
                // `Box<I: ?Sized>`), so skipping it keeps a dead candidate
                // alive and blocks the unique general impl.
                {
                    const HIRPathParams* boundParams = markerImpl ? &candidate->markerImplParams : &traitImpl->implParams;
                    for (size_t i = 0; i < implParamsDef->types.size() && i < boundParams->types.size(); i++) {
                        if (!implParamsDef->types[i].isSized) {
                            continue;
                        }
                        const auto& bound = boundParams->types[i];
                        if (bound == HIRTypeRef()) {
                            continue;
                        }
                        // A tentative impl placeholder is a parameter the
                        // nested goals have not inferred yet; its sizedness
                        // is checked through whatever it resolves to, not
                        // here (a placeholder always reads as fuzzy and
                        // would wrongly downgrade the candidate).
                        if (typeHasCandidatePlaceholder(bound)) {
                            continue;
                        }
                        const auto sized = resolve_.typeIsSized(span(), bound);
                        if (sized == HIRCompare::Unequal) {
                            return Certainty::NoSolution;
                        }
                        if (sized == HIRCompare::Fuzzy) {
                            candidate->ambiguityBeyondHead = true;
                            candidate->nestedAmbiguity = true;
                            result = Certainty::Ambiguous;
                        }
                    }
                }

                auto monomorphTraitBound = [&](const auto& traitBound, HIRTypeRef& nestedType, HIRSimplePath& nestedTrait, HIRPathParams& nestedParams, HIRTraitPath::assocListT& nestedAssociated) {
                    auto monomorphBound = [&](auto& ms) {
                        auto boundType = ms.monomorphType(span(), traitBound.type);
                        auto boundTrait = ms.monomorphTraitpath(span(), traitBound.trait, true);

                        nestedType = ::std::move(boundType);
                        nestedTrait = boundTrait.path.path;
                        nestedParams = boundTrait.path.params.clone();
                        for (const auto& aty : boundTrait.typeBounds) {
                            nestedAssociated.insert({aty.first, aty.second.clone()});
                        }
                    };
                    if (markerImpl) {
                        auto ms = MonomorphStatePtr(crate.types, nullptr, &candidate->markerImplParams, nullptr);
                        monomorphBound(ms);
                    } else {
                        auto ms = candidate->impl.getCbMonomorphTraitimpl(crate.types, span(), {});
                        monomorphBound(ms);
                    }
                };

                // Associated equalities are inference constraints for the
                // candidate's own parameters, not ordinary ordered
                // obligations.  Collect them from every nested predicate
                // first, so `Extend<B>` does not stay ambiguous merely
                // because a later `Iterator<Item = (B, A)>` is what fixes B.
                for (const auto& bound : implParamsDef->bounds) {
                    const auto* traitBound = bound.opt_TraitBound();
                    if (!traitBound || traitBound->trait.typeBounds.empty()) {
                        continue;
                    }
                    HIRTypeRef nestedType;
                    HIRSimplePath nestedTrait;
                    HIRPathParams nestedParams;
                    HIRTraitPath::assocListT nestedAssociated;
                    monomorphTraitBound(*traitBound, nestedType, nestedTrait, nestedParams, nestedAssociated);
                    if (bindCandidatePlaceholders(*candidate, nestedType, nestedAssociated) == CandidateBindingResult::Mismatch) {
                        return Certainty::NoSolution;
                    }
                }

                for (const auto& bound : implParamsDef->bounds) {
                    if (const auto* be = bound.opt_TraitBound()) {
                        HIRTypeRef nestedType;
                        HIRSimplePath nestedTrait;
                        HIRPathParams nestedParams;
                        HIRTraitPath::assocListT nestedAssociated;

                        // Candidate and response storage is pool-backed, so nested
                        // goals cannot relocate this parent slot.
                        monomorphTraitBound(*be, nestedType, nestedTrait, nestedParams, nestedAssociated);

                        // An impl parameter may occur only in an associated-type
                        // equality of a nested goal.  Canonical solvers infer that
                        // parameter from the projection response of the nested
                        // goal; preserve the same response in our impl parameters
                        // before evaluating the goal itself.
                        const auto binding = bindCandidatePlaceholders(*candidate, nestedType, nestedAssociated);
                        if (binding == CandidateBindingResult::Mismatch) {
                            return Certainty::NoSolution;
                        }
                        if (binding == CandidateBindingResult::Changed) {
                            nestedAssociated.clear();
                            monomorphTraitBound(*be, nestedType, nestedTrait, nestedParams, nestedAssociated);
                        }

                        // The certainty-only table is the fast path for the vast
                        // majority of nested obligations and also validates all
                        // associated-type constraints. Only an ambiguous goal
                        // whose response can still bind this candidate needs the
                        // more expensive canonical response assembly.
                        auto nested = solveGoal(nestedTrait, nestedParams, nestedType, &nestedAssociated);
                        if (nested == Certainty::NoSolution) {
                            return Certainty::NoSolution;
                        }
                        if (nested == Certainty::Ambiguous && candidateNeedsResponseConstraints(*candidate)) {
                            Certainty responseCertainty = Certainty::NoSolution;
                            auto nestedCallback = makeCallable<SolverResponseCb>([&](SolverResponse response) {
                                if (!response.hasImpl || !response.impl
                                    || bindCandidateResponse(*candidate, nestedType, nestedParams, nestedAssociated, response.impl->legacy()) == CandidateBindingResult::Mismatch) {
                                    return false;
                                }
                                responseCertainty = response.certainty;
                                return true;
                            });
                            const bool hasResponse = evaluateTyped(span(), nestedTrait, nestedParams, nestedType, nestedCallback, {.assocName = ""});
                            if (!hasResponse) {
                                return Certainty::NoSolution;
                            }
                            nested = responseCertainty;
                        }
                        if (nested == Certainty::Ambiguous) {
                            candidate->ambiguityBeyondHead = true;
                            candidate->nestedAmbiguity = true;
                            candidate->normalizationNestedGoals.pushBack(&bound);
                            result = Certainty::Ambiguous;
                        }
                    } else if (const auto* equality = bound.opt_TypeEquality()) {
                        HIRTypeRef left;
                        HIRTypeRef right;
                        if (markerImpl) {
                            auto ms = MonomorphStatePtr(crate.types, nullptr, &candidate->markerImplParams, nullptr);
                            left = ms.monomorphType(span(), equality->type);
                            right = ms.monomorphType(span(), equality->otherType);
                        } else {
                            auto ms = candidate->impl.getCbMonomorphTraitimpl(crate.types, span(), {});
                            left = ms.monomorphType(span(), equality->type);
                            right = ms.monomorphType(span(), equality->otherType);
                        }
                        const auto relation = this->unifyProbe(left, right);
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

            Certainty solveGoal(const HIRSimplePath& trait, const HIRPathParams& params, const HIRTypeData* type, const HIRTraitPath::assocListT* associated) {
                const auto availableDepth = availableDepthForNested();
                if (!availableDepth) {
                    return Certainty::Ambiguous;
                }
                auto goalType = type;
                auto goalParams = params.clone();
                // Resolve const inference variables up front: assembly and
                // parameter binding must see the bound value, or a nested
                // enumeration invents one (core's swap_bytes bound N=1 for a
                // goal whose lane count had already resolved to 2).
                for (auto& value : goalParams.values) {
                    if (const auto* infer = value.opt_Infer(); infer && infer->index != ~0u) {
                        // The guarded overload keeps reserved-range indexes
                        // (a nested goal in canonical space carries canonical
                        // value slots) rigid instead of asserting.
                        const auto& resolved = resolve_.ivars.getValue(value);
                        if (!resolved.is_Infer()) {
                            value = resolved.clone();
                        }
                    }
                }
                if (goalHasUnassignedInfer(goalParams, goalType, associated)) {
                    return Certainty::Ambiguous;
                }
                // Nested obligations are formed directly from monomorphised impl
                // bounds.  Their Self type can therefore still be a projection,
                // e.g. `<Option::IntoIter<T> as Iterator>::Item: IntoIterator`.
                // Candidate assembly operates on the normalized goal input, just
                // as it already does for trait arguments.
                goalType = normalizeGoalInput(goalType);
                for (auto& param : goalParams.types) {
                    param = normalizeGoalInput(::std::move(param));
                }
                if (goalHasUnassignedInfer(goalParams, goalType, associated)) {
                    return Certainty::Ambiguous;
                }
                // rustc structurally normalises Self before candidate assembly.
                // An unresolved projection over a type variable normalises to an
                // inference variable and therefore forces ambiguity; treating the
                // projection syntax as rigid lets an unrelated fuzzy ParamEnv
                // predicate constrain its output.
                if (selfIsUnresolvedProjectionOverIvar(goalType)) {
                    return Certainty::Ambiguous;
                }
                const auto& resolvedType = resolve_.resolveType(goalType);
                // Candidate assembly must not use an unconstrained `Self` type to
                // guide inference.  A concrete associated-type equality does
                // constrain the goal, however, and may uniquely determine Self.
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
                CanonicalizeTraitGoal canonicalizer(crate.types, &resolve_.ivars);
                const auto canonical = canonicalizeGoal(goalParams, resolvedType, associated, canonicalizer);
                const auto* canonicalAssociated = canonical.associated.empty() ? nullptr : &canonical.associated;
                const auto hash = goalHash(trait, canonical.params, canonical.type, canonicalAssociated);
                if (const auto* cached = findCachedGoal(hash, trait, canonical.params, canonical.type, canonicalAssociated)) {
                    return cached->certainty;
                }
                const bool crateCacheable = !canonicalAssociated && crateCacheUsable() && goalIsConcrete(trait, canonical);
                if (crateCacheable) {
                    if (const auto* global = crateCache().find(hash, trait, canonical.params, canonical.type)) {
                        return global->certainty;
                    }
                }
                if (findActiveGoal(hash, trait, canonical.params, canonical.type, canonicalAssociated)) {
                    // Productive recursive traits prove their provisional goal;
                    // ordinary trait cycles remain ambiguous.
                    cycleHits_++;
                    return crate.getTraitByPath(span(), trait).isCoinductive ? Certainty::Proven : Certainty::Ambiguous;
                }

                auto* activeGoal = pushActiveGoal(hash, trait, canonical.params, canonical.type, canonicalAssociated);

                STD_DEFER {
                    popActiveGoal(activeGoal);
                };

                const auto cycleHitsBefore = cycleHits_;
                const bool rigidKey = canonicalGoalIsRigid(canonical);
                auto cacheResult = [&](Certainty certainty) {
                    if (crateCacheable && rigidKey && cycleHits_ == cycleHitsBefore) {
                        crateCache().insert(hash, trait, canonical.params.clone(), canonical.type, certainty);
                    }
                    // Provisional results computed under a goal cycle depend on
                    // the cycle head and must not outlive this evaluation.
                    return cacheGoal(hash, trait, canonical.params, canonical.type, canonicalAssociated, certainty, rigidKey && cycleHits_ == cycleHitsBefore);
                };

                const size_t frameIndex = frameDepth++;
                if (frameIndex == frames.size()) {
                    frames.push_back(crate.pool->make<CandidateFrame>());
                }
                frames[frameIndex]->clear(candidateNodes);
                frames[frameIndex]->availableDepth = *availableDepth;

                STD_DEFER {
                    const bool encounteredOverflow = frames[frameIndex]->encounteredOverflow;
                    frames[frameIndex]->clear(candidateNodes);
                    assert(frameDepth == frameIndex + 1);
                    frameDepth--;
                    if (encounteredOverflow && frameIndex > 0) {
                        frames[frameIndex - 1]->encounteredOverflow = true;
                    }
                };

                // Assembly and evaluation run against the canonical goal, so
                // candidate sets and certainties are functions of the cache
                // key rather than of caller variable identity.
                assembleCandidates(frameIndex, trait, canonical.params, canonical.type);

                bool sawAmbiguous = false;
                bool suppressAutoBuiltin = false;
                bool negativeProven = false;
                bool negativeAmbiguous = false;
                Certainty autoBuiltinResult = Certainty::NoSolution;
                const size_t candidateCount = frames[frameIndex]->candidates.size();
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
                        return cacheResult(Certainty::Proven);
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
                // Zero viable candidates: heads are selected by the self
                // CONSTRUCTOR, so a concrete constructor stays NoSolution no
                // matter what its arguments or the trait parameters resolve
                // to ([?i; 3]: ExactSizeIterator).  A bare-ivar self can
                // still match anything, and a rigid projection self is not a
                // constructor -- inference elsewhere in the goal can still
                // unlock an environment candidate for it.
                const auto* selfPath = resolvedType->opt_Path();
                const bool selfIsAlias = selfPath && (selfPath->binding.is_Opaque() || selfPath->binding.is_Unbound());
                // A return-position opaque SELF with zero candidates lacks
                // the goal trait in its declared bounds: its trait repertoire
                // is exactly those bounds, so the answer is a rigid
                // NoSolution -- forced ambiguity here re-evaluates the same
                // dead blanket exponentially (issue-64848's
                // `F: FnOnce() -> T` against `impl AssociatedConstant`).
                const auto* selfErased = resolvedType->opt_ErasedType();
                const bool selfIsRigidOpaque = selfErased && selfErased->inner.is_Fcn();
                bool paramsHoldOpaque = !selfIsRigidOpaque && containsDefiningOpaque(resolvedType);
                for (const auto& ty : goalParams.types) {
                    paramsHoldOpaque |= containsDefiningOpaque(ty);
                }
                const bool inferMayUnlock = resolvedType->is_Infer()
                    || paramsHoldOpaque
                    || (selfIsAlias && (resolve_.typeContainsIvars(resolvedType) || resolve_.paramsContainIvars(goalParams)));
                if (sawAmbiguous || inferMayUnlock || (coherenceMode && !traitRefIsKnowable(trait, goalParams, resolvedType))) {
                    return cacheResult(Certainty::Ambiguous);
                }
                return cacheResult(Certainty::NoSolution);
            }

            // A literal (integer/float class) inference variable can only
            // become one of its class's primitives.  If no impl or bound
            // head matches ANY of them, the goal can never be proven --
            // rustc probes literal receivers per primitive type, which is
            // what keeps `(&mut ?int).partial_cmp(..)` from selecting
            // Iterator::partial_cmp through the &mut I blanket.
            bool literalClassCanMatch(const HIRSimplePath& trait, const HIRPathParams& params, HIRInferClass tyClass) const {
                // Auto/marker traits have no impl heads for primitives: they
                // are proven structurally, and every primitive satisfies the
                // structural probe.
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
                    auto probe = [&](ImplRef, HIRCompare) {
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

            bool containsDefiningOpaque(const HIRTypeData* ty) const {
                return visitTyWith(ty, [&](const HIRTypeData* inner) {
                    const auto* erased = inner->opt_ErasedType();
                    if (!erased) {
                        return false;
                    }
                    // A return-position opaque outside its defining function
                    // is rigid like any nominal type (its repertoire is its
                    // declared bounds); treating every Fcn-origin opaque as
                    // "may still be defined" held goals ambiguous forever
                    // (example-calendar) or re-evaluated dead blankets
                    // exponentially (issue-64848).  Inside the defining
                    // function (its own return opaques, registered at
                    // typecheck start) the conservative treatment stays.
                    if (const auto* fcn = erased->inner.opt_Fcn()) {
                        return resolve_.isDefiningFcnOrigin(fcn->origin);
                    }
                    const auto* alias = erased->inner.opt_Alias();
                    return alias && resolve_.isOpaqueAliasDefiningScope(*alias->inner);
                });
            }

            Certainty matchRootAssociated(const HIRSimplePath& trait, Candidate& candidate, const char* assocName, const HIRTypeData* assocType, const HIRPathParams* assocParams) {
                if (!assocName || !assocName[0]) {
                    return Certainty::Proven;
                }
                const auto& impl = candidate.impl;
                const HIRPathParams noParams;
                const auto& params = assocParams ? *assocParams : noParams;
                if (!impl.data.is_TraitImpl() && params.hasParams()) {
                    return Certainty::Ambiguous;
                }
                auto output = impl.getType(crate.types, assocName, params);
                if (output == HIRTypeRef()) {
                    if (impl.data.is_TraitImpl()) {
                        return Certainty::Ambiguous;
                    }
                    // A bare ParamEnv trait predicate proves only that the
                    // projection exists.  It cannot provide a NormalizesTo
                    // value, even when the destination is an inference slot:
                    // rebuilding the same projection with the bound's refined
                    // inputs would leak those inputs as a fake normalization.
                    return Certainty::Ambiguous;
                }
                if (!assocType) {
                    return Certainty::Proven;
                }
                // A defining-scope opaque in the response is an OUTPUT of
                // alias-relate (the requirement defines its hidden type), not
                // an input that can reject an otherwise valid candidate.
                if (containsDefiningOpaque(output) || containsDefiningOpaque(assocType)) {
                    return Certainty::Proven;
                }
                const auto relation = this->relateTypes(candidate, assocType, output);
                if (relation == Certainty::NoSolution) {
                    // `!` coerces into any requirement: a diverging closure's
                    // Output does not reject the candidate, the caller's
                    // coercion machinery settles it (rustc seeds the closure
                    // signature from the expectation instead).
                    if (resolve_.ivars.getType(output)->is_Diverge()) {
                        return Certainty::Ambiguous;
                    }
                    return Certainty::NoSolution;
                }
                return relation;
            }

            ImplRef materializeRootAssociated(ImplRef impl, const HIRSimplePath& trait, const char* assocName, const HIRPathParams* assocParams) const {
                if (!assocName || !assocName[0] || impl.data.is_TraitImpl()) {
                    return impl;
                }
                const HIRPathParams noParams;
                const auto& itemParams = assocParams ? *assocParams : noParams;
                if (impl.getType(crate.types, assocName, itemParams) != HIRTypeRef()) {
                    return impl;
                }

                auto type = impl.getImplType(crate.types);
                auto params = impl.getTraitParams(crate.types);
                HIRTraitPath::assocListT associated;
                if (const auto* bounded = impl.data.opt_BoundedPtr()) {
                    if (bounded->assoc) {
                        for (const auto& entry : *bounded->assoc) {
                            associated.insert({entry.first, entry.second.clone()});
                        }
                    }
                } else if (const auto* bounded = impl.data.opt_Bounded()) {
                    for (const auto& entry : bounded->assoc) {
                        associated.insert({entry.first, entry.second.clone()});
                    }
                }

                const auto name = RcString::newInterned(assocName);
                auto sourceTrait = HIRGenericPath(trait, params.clone());
                auto projection = makeAssociatedProjection(type, sourceTrait, name, itemParams);
                associated.erase(name);
                associated.insert({name, HIRTraitPath::AtyEqual{::std::move(sourceTrait), itemParams.clone(), ::std::move(projection)}});
                const bool ambiguousIdentity = impl.isAmbiguousIdentity();
                auto result = ImplRef(::std::move(type), ::std::move(params), ::std::move(associated));
                if (ambiguousIdentity) {
                    result.markAmbiguousIdentity();
                }
                return result;
            }

            void appendResponseObligations(ThinVector<SolverObligation>& obligations, const Candidate* candidate, const Monomorphiser& canonicalizer) const {
                if (!candidate) {
                    return;
                }

                auto append = [&](HIRTypeRef type, HIRTraitPath trait) {
                    type = canonicalizer.monomorphType(span(), type, true);
                    trait = canonicalizer.monomorphTraitpath(span(), trait, true);
                    obligations.push_back(SolverObligation{::std::move(type), ::std::move(trait)});
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
                } else if (const auto* traitImpl = candidate->impl.data.opt_TraitImpl(); traitImpl && traitImpl->impl) {
                    params = &traitImpl->impl->params;
                }
                if (!params) {
                    return;
                }

                auto needsResponse = [&](const HIRTypeData* type) {
                    return visitTyWith(type, [&](const HIRTypeData* inner) {
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
                    HIRTypeRef type;
                    HIRTraitPath trait;
                    if (candidate->markerImpl) {
                        auto monomorph = MonomorphStatePtr(crate.types, nullptr, &candidate->markerImplParams, nullptr);
                        type = monomorph.monomorphType(span(), traitBound->type);
                        trait = monomorph.monomorphTraitpath(span(), traitBound->trait, true);
                    } else {
                        auto monomorph = candidate->impl.getCbMonomorphTraitimpl(crate.types, span(), {});
                        type = monomorph.monomorphType(span(), traitBound->type);
                        trait = monomorph.monomorphTraitpath(span(), traitBound->trait, true);
                    }

                    bool needed = isNormalizationGoal(bound) || needsResponse(type);
                    for (const auto& argument : trait.path.params.types) {
                        needed |= needsResponse(argument);
                    }
                    for (const auto& associated : trait.typeBounds) {
                        needed |= needsResponse(associated.second.type);
                    }
                    if (needed) {
                        append(::std::move(type), ::std::move(trait));
                    }
                }
            }

            static bool implDefinesValue(const ImplRef& impl, const char* valueName) {
                const auto* traitImpl = impl.data.opt_TraitImpl();
                if (!traitImpl || !traitImpl->impl) {
                    return false;
                }
                const auto name = RcString::newInterned(valueName);
                return traitImpl->impl->constants.count(name)
                    || traitImpl->impl->statics.count(name)
                    || traitImpl->impl->methods.count(name);
            }

            static const Candidate* specializationValueSource(const Candidate* selected, const char* valueName) {
                for (const Candidate* source = selected; source; source = source->specializationItemSource) {
                    if (implDefinesValue(source->impl, valueName)) {
                        return source;
                    }
                }
                return nullptr;
            }

            bool responsesEqual(const ImplRef& left, const ImplRef& right, const char* assocName, const HIRPathParams* assocParams, const char* valueName) const {
                auto typesEqualAfterNormalization = [&](const HIRTypeData* lhs, const HIRTypeData* rhs) {
                    if (lhs == HIRTypeRef() || rhs == HIRTypeRef()) {
                        return lhs == rhs;
                    }
                    // ASTType* identity is structural equality after interning.
                    // Avoid recursively normalising and re-interning the common
                    // case where both canonical responses already share a type.
                    if (lhs == rhs) {
                        return true;
                    }
                    auto normalizedLhs = resolve_.expandAssociatedTypes(span(), lhs);
                    auto normalizedRhs = resolve_.expandAssociatedTypes(span(), rhs);
                    if (normalizedLhs == HIRTypeRef() || normalizedRhs == HIRTypeRef()) {
                        return normalizedLhs == normalizedRhs;
                    }
                    const auto* resolvedLhs = resolve_.resolveType(normalizedLhs);
                    const auto* resolvedRhs = resolve_.resolveType(normalizedRhs);
                    return resolvedLhs == resolvedRhs || resolvedLhs->equalsIgnoringRegions(resolvedRhs);
                };
                auto paramsEqualAfterNormalization = [&](const HIRPathParams& lhs, const HIRPathParams& rhs) {
                    if (lhs.types.size() != rhs.types.size() || lhs.values.size() != rhs.values.size()) {
                        return false;
                    }
                    // Ordinary regions are deliberately erased when a type is
                    // stored in an HM inference variable, and trait selection
                    // defers their constraints to lifetime inference.  Thus a
                    // ParamEnv proof for `Projection<'a>` and the same declared
                    // GAT bound seen through `Projection<'#omitted>` are one
                    // canonical solver response.  Higher-ranked leak checking is
                    // performed while evaluating the candidate bounds above; it
                    // must not be reintroduced here as response identity.
                    for (size_t i = 0; i < lhs.types.size(); i++) {
                        if (!typesEqualAfterNormalization(lhs.types[i], rhs.types[i])) {
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

                if (!typesEqualAfterNormalization(left.getImplType(crate.types), right.getImplType(crate.types)) || !paramsEqualAfterNormalization(left.getTraitParams(crate.types), right.getTraitParams(crate.types))) {
                    return false;
                }
                if (valueName) {
                    const auto* leftImpl = left.data.opt_TraitImpl();
                    const auto* rightImpl = right.data.opt_TraitImpl();
                    if (leftImpl || rightImpl) {
                        return leftImpl && rightImpl && leftImpl->impl == rightImpl->impl;
                    }
                    return true;
                }
                if (!assocName || !assocName[0]) {
                    return true;
                }
                const HIRPathParams noParams;
                const auto& params = assocParams ? *assocParams : noParams;
                if ((!left.data.is_TraitImpl() || !right.data.is_TraitImpl()) && params.hasParams()) {
                    return false;
                }
                const auto leftValue = left.getType(crate.types, assocName, params);
                const auto rightValue = right.getType(crate.types, assocName, params);
                // A bare environment predicate has no opinion on the value: the
                // same where-clause reaches assembly twice (cached ParamEnv and
                // the implied-declaration walk), once with its equality and once
                // without.  That is a refinement, not a conflicting response.
                if (!left.data.is_TraitImpl() && !right.data.is_TraitImpl() && (leftValue == HIRTypeRef()) != (rightValue == HIRTypeRef())) {
                    return true;
                }
                return typesEqualAfterNormalization(leftValue, rightValue);
            }

        public:
            NextTraitGoalEvaluator(const TraitResolution& resolve, const HIRCrate& crate)
                : resolve_(resolve)
                , crate(crate)
                , overlapCache(crate.pool)
                , candidateNodes(crate.pool)
                , activeGoalNodes(crate.pool)
                , cachedGoalNodes(crate.pool)
            {
                frames.reserve(16);
                goalStack.reserve(16);
                goalCache.reserve(64);
                activeGoalIndex.reserve(32);
                goalCacheIndex.reserve(128);
            }

            HIRCompare evaluateCertainty(const Span& callSpan, const HIRSimplePath& trait, const HIRPathParams& params, const HIRTypeData* type) {
                const bool outermost = span_ == nullptr;
                if (outermost) {
                    ASSERT_BUG(callSpan, goalStack.empty(), "next-solver goal stack leaked between evaluations");
                    ASSERT_BUG(callSpan, activeGoalIndex.empty(), "next-solver active goal index leaked between evaluations");
                    ASSERT_BUG(callSpan, frameDepth == 0, "next-solver candidate frames leaked between evaluations");
                    // Persistent entries key on generic-parameter names; a
                    // switched generic context (Typecheck Outer walks impls on
                    // one resolver) changes what those names mean.
                    if (envGeneration_ != resolve_.eatCacheGeneration) {
                        envGeneration_ = resolve_.eatCacheGeneration;
                        // A switched generic context renames what generic
                        // bindings mean (M:0 of one function equals M:0 of
                        // another); nothing keyed on them may survive.
                        for (auto* goal : goalCache) {
                            goal->persistent = false;
                        }
                        ivarGenerationSeen_ = resolve_.ivars.mutationGeneration;
                        solverEnvGenerationSeen_ = resolve_.solverEnvGeneration;
                        clearGoalCache();
                    }
                    // The non-persistent slice keys on canonical inference
                    // variables: it stays valid until the table (or the
                    // defining-opaque registrations) actually mutates.
                    else if (ivarGenerationSeen_ != resolve_.ivars.mutationGeneration || solverEnvGenerationSeen_ != resolve_.solverEnvGeneration) {
                        ivarGenerationSeen_ = resolve_.ivars.mutationGeneration;
                        solverEnvGenerationSeen_ = resolve_.solverEnvGeneration;
                        clearGoalCache();
                    }
                    // Responses stay warm alongside certainties.  The goal
                    // key is the complete canonical input (type AND const
                    // slots), so an equal key implies the current query's
                    // canonicalizer maps every slot of the cached response to
                    // its own variables, and existential placeholders are
                    // instantiated fresh per replay.
                    span_ = &callSpan;
                }

                STD_DEFER {
                    if (outermost) {
                        assert(goalStack.empty());
                        assert(activeGoalIndex.empty());
                        if (ivarGenerationSeen_ != resolve_.ivars.mutationGeneration || solverEnvGenerationSeen_ != resolve_.solverEnvGeneration) {
                            ivarGenerationSeen_ = resolve_.ivars.mutationGeneration;
                            solverEnvGenerationSeen_ = resolve_.solverEnvGeneration;
                            clearGoalCache();
                        }
                        frameDepth = 0;
                        span_ = nullptr;
                    }
                };

                switch (solveGoal(trait, params, type, nullptr)) {
                    case Certainty::NoSolution:
                        return HIRCompare::Unequal;
                    case Certainty::Ambiguous:
                        return HIRCompare::Fuzzy;
                    case Certainty::Proven:
                        return HIRCompare::Equal;
                }
                UNREACHABLE();
            }

            bool evaluateOverlap(const Span& callSpan, const HIRSimplePath& trait, const HIRTraitImpl& left, const HIRTraitImpl& right) {
                // The probe is a pure function of the impl pair (impls are
                // immutable after conversion), so cache it by identity.
                const auto key = stl::splitMix64(reinterpret_cast<uintptr_t>(&left)) ^ stl::splitMix64(~reinterpret_cast<uintptr_t>(&right));
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

            struct OverlapEntry {
                const HIRTraitImpl* left;
                const HIRTraitImpl* right;
                bool overlaps;
            };

            stl::IntMap<ThinVector<OverlapEntry>> overlapCache;

            bool evaluateOverlapUncached(const Span& callSpan, const HIRSimplePath& trait, const HIRTraitImpl& left, const HIRTraitImpl& right) {
                ASSERT_BUG(callSpan, !span_, "nested coherence overlap session");
                ASSERT_BUG(callSpan, !coherenceMode, "coherence mode leaked before overlap probe");
                ASSERT_BUG(callSpan, goalStack.empty(), "next-solver goal stack leaked before coherence probe");
                ASSERT_BUG(callSpan, activeGoalIndex.empty(), "next-solver active goal index leaked before coherence probe");
                ASSERT_BUG(callSpan, frameDepth == 0, "next-solver candidate frames leaked before coherence probe");
                clearGoalCache();
                span_ = &callSpan;
                coherenceMode = true;

                STD_DEFER {
                    assert(goalStack.empty());
                    assert(activeGoalIndex.empty());
                    clearGoalCache();
                    frameDepth = 0;
                    coherenceMode = false;
                    span_ = nullptr;
                };

                // Instantiate the first header with fresh inference variables, then
                // match the second header against it.  This is a unification of two
                // independently generic impls, not a one-way syntactic ordering.
                auto leftParams = resolve_.makeFreshImplParams(left.params);
                auto leftMonomorph = MonomorphStatePtr(crate.types, nullptr, &leftParams, nullptr);
                auto goalType = leftMonomorph.monomorphType(callSpan, left.type, true);
                auto goalParams = leftMonomorph.monomorphPathParams(callSpan, left.traitArgs, true);

                HIRPathParams rightParams;
                bool rightHeadNormalizationAmbiguity = false;
                ThinVector<SolverTypeEquality> rightHeadEqualities;
                ThinVector<SolverValueEquality> rightHeadValueEqualities;
                const auto rightRelation = this->unifyImplHead(
                    right.params,
                    right.traitArgs,
                    right.type,
                    goalParams,
                    goalType,
                    rightParams,
                    rightHeadNormalizationAmbiguity,
                    rightHeadEqualities,
                    rightHeadValueEqualities
                );
                if (rightRelation == Certainty::NoSolution) {
                    return false;
                }

                const size_t frameIndex = frameDepth++;
                if (frameIndex == frames.size()) {
                    frames.push_back(crate.pool->make<CandidateFrame>());
                }
                frames[frameIndex]->clear(candidateNodes);
                frames[frameIndex]->availableDepth = ROOT_DEPTH;

                STD_DEFER {
                    const bool encounteredOverflow = frames[frameIndex]->encounteredOverflow;
                    frames[frameIndex]->clear(candidateNodes);
                    assert(frameDepth == frameIndex + 1);
                    frameDepth--;
                    if (encounteredOverflow && frameIndex > 0) {
                        frames[frameIndex - 1]->encounteredOverflow = true;
                    }
                };

                const auto& traitDef = crate.getTraitByPath(callSpan, trait);
                pushCandidate(frameIndex, ImplRef(::std::move(leftParams), traitDef, trait, left), HIRCompare::Equal, Certainty::Proven, nullptr, {}, false, CandidateSource::TraitImpl);
                pushCandidate(frameIndex, ImplRef(::std::move(rightParams), traitDef, trait, right), rightRelation == Certainty::Proven ? HIRCompare::Equal : HIRCompare::Fuzzy, rightRelation, nullptr, {}, false, CandidateSource::TraitImpl, rightHeadNormalizationAmbiguity, ::std::move(rightHeadEqualities), ::std::move(rightHeadValueEqualities));

                const auto& candidates = frames[frameIndex]->candidates;
                ASSERT_BUG(callSpan, candidates.size() == 2, "coherence probe lost an impl candidate");
                const auto leftResult = evaluateCandidate(frameIndex, 0, trait, nullptr);
                if (leftResult == Certainty::NoSolution) {
                    return false;
                }
                const auto rightResult = evaluateCandidate(frameIndex, 1, trait, nullptr);
                return rightResult != Certainty::NoSolution;
            }

            bool evaluateTyped(const Span& callSpan, const HIRSimplePath& trait, const HIRPathParams& params, const HIRTypeData* type, SolverResponseCallback& callback, const TraitGoalQuery& query, bool callerBoundary = false, bool includeRootMagicCandidates = true) {
                const char* assocName = query.assocName;
                const HIRTypeData* assocType = query.assocType;
                const HIRPathParams* assocParams = query.assocParams;
                const char* valueName = query.valueName;
                const bool allowInferInputs = query.allowInferInputs;
                const auto* excludedImpl = query.excludedImpl;
                const bool hasCoercionGoals = query.coercions && !query.coercions->empty();
                const bool hasSelfCoercionGoal = hasCoercionGoals && ::std::any_of(query.coercions->begin(), query.coercions->end(), [](const SolverCoercionConstraint& constraint) {
                    return constraint.isSelf;
                });
                const bool outermost = span_ == nullptr;
                if (outermost) {
                    ASSERT_BUG(callSpan, goalStack.empty(), "next-solver goal stack leaked between evaluations");
                    ASSERT_BUG(callSpan, activeGoalIndex.empty(), "next-solver active goal index leaked between evaluations");
                    ASSERT_BUG(callSpan, frameDepth == 0, "next-solver candidate frames leaked between evaluations");
                    // Persistent entries key on generic-parameter names; a
                    // switched generic context (Typecheck Outer walks impls on
                    // one resolver) changes what those names mean.
                    if (envGeneration_ != resolve_.eatCacheGeneration) {
                        envGeneration_ = resolve_.eatCacheGeneration;
                        // A switched generic context renames what generic
                        // bindings mean (M:0 of one function equals M:0 of
                        // another); nothing keyed on them may survive.
                        for (auto* goal : goalCache) {
                            goal->persistent = false;
                        }
                        ivarGenerationSeen_ = resolve_.ivars.mutationGeneration;
                        solverEnvGenerationSeen_ = resolve_.solverEnvGeneration;
                        clearGoalCache();
                    }
                    // The non-persistent slice keys on canonical inference
                    // variables: it stays valid until the table (or the
                    // defining-opaque registrations) actually mutates.
                    else if (ivarGenerationSeen_ != resolve_.ivars.mutationGeneration || solverEnvGenerationSeen_ != resolve_.solverEnvGeneration) {
                        ivarGenerationSeen_ = resolve_.ivars.mutationGeneration;
                        solverEnvGenerationSeen_ = resolve_.solverEnvGeneration;
                        clearGoalCache();
                    }
                    // Responses stay warm alongside certainties.  The goal
                    // key is the complete canonical input (type AND const
                    // slots), so an equal key implies the current query's
                    // canonicalizer maps every slot of the cached response to
                    // its own variables, and existential placeholders are
                    // instantiated fresh per replay.
                    span_ = &callSpan;
                }

                STD_DEFER {
                    if (outermost) {
                        assert(goalStack.empty());
                        assert(activeGoalIndex.empty());
                        if (ivarGenerationSeen_ != resolve_.ivars.mutationGeneration || solverEnvGenerationSeen_ != resolve_.solverEnvGeneration) {
                            ivarGenerationSeen_ = resolve_.ivars.mutationGeneration;
                            solverEnvGenerationSeen_ = resolve_.solverEnvGeneration;
                            clearGoalCache();
                        }
                        frameDepth = 0;
                        span_ = nullptr;
                    }
                };

                auto goalType = type;
                auto goalParams = params.clone();
                // Resolve const inference variables up front: assembly and
                // parameter binding must see the bound value, or a nested
                // enumeration invents one (core's swap_bytes bound N=1 for a
                // goal whose lane count had already resolved to 2).
                for (auto& value : goalParams.values) {
                    if (const auto* infer = value.opt_Infer(); infer && infer->index != ~0u) {
                        // The guarded overload keeps reserved-range indexes
                        // (a nested goal in canonical space carries canonical
                        // value slots) rigid instead of asserting.
                        const auto& resolved = resolve_.ivars.getValue(value);
                        if (!resolved.is_Infer()) {
                            value = resolved.clone();
                        }
                    }
                }
                auto emitForcedAmbiguity = [&]() {
                    // Ordinary lookup cannot consume an identity response, while
                    // extended solver callers use it to retain the original goal
                    // without committing any candidate substitutions.
                    if (!assocName) {
                        return false;
                    }
                    auto ambiguous = ImplRef(goalType, goalParams.clone(), HIRTraitPath::assocListT());
                    ambiguous.markAmbiguousIdentity();
                    SolverResponse response;
                    response.certainty = Certainty::Ambiguous;
                    response.impl = ownSolverImpl(materializeRootAssociated(::std::move(ambiguous), trait, assocName, assocParams));
                    response.hasImpl = true;
                    if (query.operatorGoal) {
                        response.operatorSummary.hasSemanticImpl = true;
                    }
                    return callback.visit(::std::move(response));
                };
                if (!allowInferInputs && goalHasUnassignedInfer(goalParams, goalType, nullptr)) {
                    return emitForcedAmbiguity();
                }
                goalType = normalizeGoalInput(goalType);
                for (auto& param : goalParams.types) {
                    param = normalizeGoalInput(::std::move(param));
                }
                if (selfIsUnresolvedProjectionOverIvar(goalType)) {
                    return emitForcedAmbiguity();
                }
                const auto& resolvedType = resolve_.resolveType(goalType);
                // Match rustc's forced-ambiguity response for a genuinely
                // unconstrained `Self` type.  A known associated output is an
                // input constraint and can legitimately select a unique
                // response.  This holds for exporting callers too: assembling
                // every impl of the trait against a bare variable was a
                // full-crate enumeration on each probe (coretests/num timed
                // out on it).
                const bool associatedConstrainsSelf = assocName && assocName[0] && assocType && !typeHasUnknown(assocType);
                if (const auto* infer = resolvedType->opt_Infer()) {
                    if (!infer->isLit() && !associatedConstrainsSelf && !hasSelfCoercionGoal) {
                        return emitForcedAmbiguity();
                    }
                    if (infer->isLit() && goalParams.types.empty() && goalParams.values.empty() && !literalClassCanMatch(trait, goalParams, infer->tyClass)) {
                        return false;
                    }
                }
                CanonicalizeTraitGoal canonicalizer(crate.types, &resolve_.ivars);
                const auto canonical = canonicalizeGoal(goalParams, resolvedType, nullptr, canonicalizer);
                // The associated output is not part of the response cache key,
                // but its placeholders (and canonical variables) are still
                // inputs of this query.  Canonicalise it too so evaluation and
                // response instantiation see one consistent space.
                HIRTypeRef canonicalAssocTypeStorage;
                const HIRTypeData* canonicalAssocType = nullptr;
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
                ThinVector<SolverCoercionConstraint> canonicalCoercions;
                if (hasCoercionGoals) {
                    for (const auto& constraint : *query.coercions) {
                        canonicalCoercions.push_back(SolverCoercionConstraint{
                            constraint.typeIndex,
                            canonicalizer.monomorphType(span(), constraint.other, true),
                            constraint.direction,
                            constraint.op,
                            constraint.isSelf,
                        });
                    }
                }
                const auto rootHash = goalHash(trait, canonical.params, canonical.type, nullptr);
                stl::Vector<::std::pair<const Candidate*, HIRCompare>> distinctViable;
                auto deliverResponse = [&](const SolverResponse& response, const ImplRef* directImpl) {
                    if (!outermost && !callerBoundary) {
                        DecanonicalizeSolverInfers mapper(crate.types, canonicalizer);
                        auto nestedResponse = monomorphSolverResponse(response, mapper);
                        if (directImpl) {
                            auto direct = monomorphImplRef(*directImpl, mapper);
                            nestedResponse.impl = ownSolverImpl(::std::move(direct));
                        }
                        return callback.visit(::std::move(nestedResponse));
                    }
                    InstantiateTraitResponseForCaller instantiator(crate.types, resolve_.ivars, canonicalizer.placeholderNames(), &canonicalizer);
                    auto callerResponse = monomorphSolverResponse(response, instantiator);
                    if (directImpl) {
                        auto direct = monomorphImplRef(*directImpl, instantiator);
                        callerResponse.impl = ownSolverImpl(::std::move(direct));
                    }
                    return callback.visit(::std::move(callerResponse));
                };
                // Extended callers use an explicit empty associated-item name
                // when they need the canonical trait response itself. Cache that
                // completed response, not just its certainty: otherwise every
                // repeated nested obligation rebuilds the entire candidate graph.
                // Since step B the goal's inference variables are canonical
                // slots shared by the key, the assembly, and the cached
                // response; replay maps each slot back to the CURRENT query's
                // variable through the query's own canonicalizer.  Const
                // inference variables canonicalise into value slots the same
                // way.  A response that pulls in variables beyond the goal's
                // slots is rejected by the slot-count check in emitResponse.
                // Coercion goals contain caller-side endpoints that are not
                // part of the canonical trait key. Cache the underlying trait
                // certainties as usual, but rebuild and filter the typed
                // response for each such query.
                const bool cacheableResponse = assocName && !assocName[0] && !excludedImpl && !hasCoercionGoals && !query.operatorGoal;
                const bool crateCacheableResponse = cacheableResponse && crateCacheUsable() && goalIsConcrete(trait, canonical);
                if (cacheableResponse) {
                    if (const auto* cached = findCachedGoal(rootHash, trait, canonical.params, canonical.type, nullptr); cached && cached->hasResponse) {
                        return deliverResponse(*cached->response, nullptr);
                    }
                    if (crateCacheableResponse) {
                        if (const auto* global = crateCache().find(rootHash, trait, canonical.params, canonical.type); global && global->hasResponse) {
                            return deliverResponse(*global->response, nullptr);
                        }
                    }
                }
                const auto cycleHitsBefore = cycleHits_;
                const bool rigidKey = canonicalGoalIsRigid(canonical);
                const auto appendAssociatedEquality = [&](auto& response, const HIRTypeData* required, HIRTypeRef output) {
                    response.equalities.push_back(SolverTypeEquality{required, output});

                    // Preserve the unifier's leaf relations as response data.
                    // `Context::equateTypes` treats projections as
                    // non-injective and may therefore leave a nested const
                    // slot untouched even though this selected response has
                    // already established the exact projection application.
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

                    const auto appendStructuralValues = [&](auto&& self, const HIRTypeData* lhs, const HIRTypeData* rhs) -> void {
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
                            case HIRTypeData::TAG_Path: {
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
                            case HIRTypeData::TAG_Array: {
                                const auto& left = lhs->as_Array();
                                const auto& right = rhs->as_Array();
                                self(self, left.inner, right.inner);
                                if (left.size.is_Unevaluated() && right.size.is_Unevaluated()
                                    && left.size.as_Unevaluated() != right.size.as_Unevaluated()) {
                                    response.valueEqualities.push_back(SolverValueEquality{
                                        left.size.as_Unevaluated().clone(),
                                        right.size.as_Unevaluated().clone(),
                                    });
                                }
                                return;
                            }
                            case HIRTypeData::TAG_Tuple: {
                                const auto& left = lhs->as_Tuple();
                                const auto& right = rhs->as_Tuple();
                                if (left.size() == right.size()) {
                                    for (size_t i = 0; i < left.size(); i++) {
                                        self(self, left[i], right[i]);
                                    }
                                }
                                return;
                            }
                            case HIRTypeData::TAG_Slice:
                                self(self, lhs->as_Slice().inner, rhs->as_Slice().inner);
                                return;
                            case HIRTypeData::TAG_Borrow:
                                self(self, lhs->as_Borrow().inner, rhs->as_Borrow().inner);
                                return;
                            case HIRTypeData::TAG_Pointer:
                                self(self, lhs->as_Pointer().inner, rhs->as_Pointer().inner);
                                return;
                            default:
                                return;
                        }
                    };
                    appendStructuralValues(appendStructuralValues, required, output);

                    // Associated types are not injective in general, so the
                    // caller's ordinary type equality deliberately does not
                    // infer projection inputs.  Once the solver has selected
                    // a response, however, two occurrences of the exact same
                    // symbolic projection denote the same application and
                    // their Self/type/const inputs are direct response effects.
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
                                    if (leftArray && rightArray && leftArray->size.is_Unevaluated() && rightArray->size.is_Unevaluated()
                                        && leftArray->size.as_Unevaluated() != rightArray->size.as_Unevaluated()) {
                                        response.valueEqualities.push_back(SolverValueEquality{
                                            leftArray->size.as_Unevaluated().clone(),
                                            rightArray->size.as_Unevaluated().clone(),
                                        });
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
                const auto operatorImplHasBuiltinSignature = [&](const ImplRef& impl) {
                    ASSERT_BUG(span(), query.operatorGoal, "operator candidate classification without an operator goal");
                    const auto& operatorGoal = *query.operatorGoal;
                    auto implType = impl.getImplType(crate.types);
                    auto implParams = impl.getTraitParams(crate.types);
                    if (resolve_.ivars.typeContainsIvars(implType, /*onlyUnbound=*/true)
                        || resolve_.ivars.pathparamsContainIvars(implParams, /*onlyUnbound=*/true)) {
                        return false;
                    }
                    resolve_.expandAssociatedTypesInplace(span(), implType);
                    for (auto& type : implParams.types) {
                        resolve_.expandAssociatedTypesInplace(span(), type);
                    }

                    const bool hasBuiltinInputs = implParams.types.empty()
                        ? primitiveOperatorHasBuiltin(operatorGoal.operation, implType)
                        : implParams.types.size() == 1
                            && primitiveOperatorHasBuiltin(operatorGoal.operation, implType, implParams.types.front());
                    if (!hasBuiltinInputs) {
                        return false;
                    }
                    if (!operatorGoal.outputName || !operatorGoal.outputName[0]) {
                        return true;
                    }

                    const HIRPathParams noParams;
                    const auto& outputParams = operatorGoal.outputParams ? *operatorGoal.outputParams : noParams;
                    auto output = impl.getType(crate.types, operatorGoal.outputName, outputParams);
                    if (output == HIRTypeRef() || resolve_.ivars.typeContainsIvars(output, /*onlyUnbound=*/true)) {
                        return false;
                    }
                    resolve_.expandAssociatedTypesInplace(span(), output);

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

                const auto classifyOperatorImpl = [&](SolverOperatorSummary& summary, const ImplRef& impl) {
                    if (!query.operatorGoal || impl.isAmbiguousIdentity()) {
                        return;
                    }
                    const bool builtinSignature = operatorImplHasBuiltinSignature(impl);
                    const auto* traitImpl = impl.data.opt_TraitImpl();
                    if (query.operatorGoal->currentImpl && traitImpl && traitImpl->impl == query.operatorGoal->currentImpl) {
                        summary.sawCurrentImpl = true;
                        summary.currentImplHasBuiltinSignature = builtinSignature;
                    } else if (!builtinSignature) {
                        summary.hasSemanticImpl = true;
                    }
                };

                // Filled by the distinct-responses branch.  Candidate heads
                // stay local to this evaluation; only their common slot
                // assignments and aggregate query facts may escape.
                auto emitResponse = [&](ImplRef response, HIRCompare certainty, const Candidate* responseCandidate = nullptr) {
                    // A response can hold live inference variables beyond the
                    // goal's slots (an environment bound pulled a caller
                    // variable into an impl parameter).  Those pass through
                    // the frozen canonicalizer raw and make the response
                    // uncacheable: the goal key does not capture which caller
                    // variable the environment linked in, and the table can
                    // mutate between write and replay even inside one
                    // outermost evaluation (replaying then imports a binding
                    // the current goal never established).
                    canonicalizer.freeze();
                    auto canonicalResponse = monomorphImplRef(response, canonicalizer);
                    SolverResponse solverResponse;
                    solverResponse.certainty = certainty == HIRCompare::Equal ? Certainty::Proven : Certainty::Ambiguous;
                    solverResponse.slots = extractSlotValues(canonical, canonicalResponse, canonicalizer, solverResponse.certainty);
                    solverResponse.impl = ownSolverImpl(monomorphImplRef(canonicalResponse, MonomorphiserNop(crate.types)));
                    solverResponse.hasImpl = true;
                    if (distinctViable.empty()) {
                        classifyOperatorImpl(solverResponse.operatorSummary, canonicalResponse);
                    }
                    if (solverResponse.impl->ambiguousIdentity) {
                        // The identity is the unresolved input goal, not one
                        // candidate response. Normalising that goal can expose
                        // a ParamEnv projection and make slot extraction look
                        // like a real assignment; replaying it would let an
                        // unselected environment candidate guide inference.
                        // Start from literal identity. The distinct-candidate
                        // intersection below replaces only assignments shared
                        // by every viable response.
                        for (size_t i = 0; i < solverResponse.slots.types.size(); i++) {
                            solverResponse.slots.types[i] = solverResponse.slots.typeInputs[i];
                        }
                        for (size_t i = 0; i < solverResponse.slots.values.size(); i++) {
                            solverResponse.slots.values[i] = solverResponse.slots.valueInputs[i].clone();
                        }
                    }
                    appendResponseObligations(solverResponse.obligations, responseCandidate, canonicalizer);
                    if (responseCandidate) {
                        for (const auto& equality : responseCandidate->headEqualities) {
                            solverResponse.equalities.push_back(SolverTypeEquality{
                                canonicalizer.monomorphType(span(), equality.left, true),
                                canonicalizer.monomorphType(span(), equality.right, true),
                            });
                        }
                        for (const auto& equality : responseCandidate->headValueEqualities) {
                            solverResponse.valueEqualities.push_back(SolverValueEquality{
                                canonicalizer.monomorphConstgeneric(span(), equality.left, true),
                                canonicalizer.monomorphConstgeneric(span(), equality.right, true),
                            });
                        }
                        for (const auto& equality : responseCandidate->relationEqualities) {
                            solverResponse.equalities.push_back(SolverTypeEquality{
                                canonicalizer.monomorphType(span(), equality.left, true),
                                canonicalizer.monomorphType(span(), equality.right, true),
                            });
                        }
                        for (const auto& equality : responseCandidate->relationValueEqualities) {
                            solverResponse.valueEqualities.push_back(SolverValueEquality{
                                canonicalizer.monomorphConstgeneric(span(), equality.left, true),
                                canonicalizer.monomorphConstgeneric(span(), equality.right, true),
                            });
                        }
                        for (const auto& equality : responseCandidate->coercionEqualities) {
                            solverResponse.equalities.push_back(SolverTypeEquality{
                                canonicalizer.monomorphType(span(), equality.left, true),
                                canonicalizer.monomorphType(span(), equality.right, true),
                            });
                        }
                    }
                    if (canonicalAssocType && assocName && assocName[0] && !solverResponse.impl->ambiguousIdentity) {
                        const HIRPathParams noParams;
                        const auto& itemParams = canonicalAssocParams ? *canonicalAssocParams : noParams;
                        auto output = canonicalResponse.getType(crate.types, assocName, itemParams);
                        if (output != HIRTypeRef()) {
                            output = normalizeGoalInput(::std::move(output));
                            appendAssociatedEquality(solverResponse, canonicalAssocType, canonicalizer.monomorphType(span(), output, true));
                        }
                    }
                    if (distinctViable.length() != 0) {
                        ThinVector<SolverSlotValues> candidateSlots;
                        for (size_t i = 0; i < distinctViable.length(); i++) {
                            const auto* candidate = distinctViable[i].first;
                            const auto candidateCertainty = distinctViable[i].second == HIRCompare::Equal ? Certainty::Proven : Certainty::Ambiguous;
                            auto candidateImpl = monomorphImplRef(candidate->impl, canonicalizer);
                            classifyOperatorImpl(solverResponse.operatorSummary, candidateImpl);
                            candidateSlots.push_back(extractSlotValues(canonical, candidateImpl, canonicalizer, candidateCertainty));
                        }

                        // Distinct impl heads are still allowed to constrain
                        // an input when every viable response assigns that
                        // slot identically. Publish that intersection on the
                        // ambiguous identity response; consumers must not
                        // recover it by choosing among candidate heads.
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
                    }
                    if (!cacheableResponse || canonicalizer.sawForeignIvar()) {
                        return deliverResponse(solverResponse, &response);
                    }
                    if (crateCacheableResponse && rigidKey && cycleHits_ == cycleHitsBefore && !canonicalResponse.data.is_BoundedPtr()) {
                        auto* global = crateCache().insert(rootHash, trait, canonical.params.clone(), canonical.type, solverResponse.certainty);
                        auto globalResponse = monomorphSolverResponse(solverResponse, MonomorphiserNop(crate.types));
                        global->response = crate.pool->make<SolverResponse>(::std::move(globalResponse));
                        global->hasResponse = true;
                    }
                    auto* storedResponse = crate.pool->make<SolverResponse>(::std::move(solverResponse));
                    auto* cached = cacheResponse(rootHash, trait, canonical.params, canonical.type, nullptr, storedResponse);
                    cached->persistent = rigidKey && cycleHits_ == cycleHitsBefore;
                    return deliverResponse(*storedResponse, nullptr);
                };
                if (findActiveGoal(rootHash, trait, canonical.params, canonical.type, nullptr)) {
                    const bool coinductive = crate.getTraitByPath(span(), trait).isCoinductive;
                    cycleHits_++;
                    return emitResponse(ImplRef(resolvedType, &goalParams, nullptr), coinductive ? HIRCompare::Equal : HIRCompare::Fuzzy);
                }
                auto* rootGoal = pushActiveGoal(rootHash, trait, canonical.params, canonical.type, nullptr);

                STD_DEFER {
                    popActiveGoal(rootGoal);
                };

                const size_t frameIndex = frameDepth++;
                if (frameIndex == frames.size()) {
                    frames.push_back(crate.pool->make<CandidateFrame>());
                }
                frames[frameIndex]->clear(candidateNodes);
                frames[frameIndex]->availableDepth = ROOT_DEPTH;

                STD_DEFER {
                    const bool encounteredOverflow = frames[frameIndex]->encounteredOverflow;
                    frames[frameIndex]->clear(candidateNodes);
                    assert(frameDepth == frameIndex + 1);
                    frameDepth--;
                    if (encounteredOverflow && frameIndex > 0) {
                        frames[frameIndex - 1]->encounteredOverflow = true;
                    }
                };

                // Assembly and evaluation run against the canonical goal, so
                // candidate sets and certainties are functions of the cache
                // key rather than of caller variable identity.
                assembleCandidates(frameIndex, trait, canonical.params, canonical.type, includeRootMagicCandidates);
                auto& frame = *frames[frameIndex];
                const size_t candidateCount = frame.candidates.size();

                bool suppressAutoBuiltin = false;
                bool negativeProven = false;
                bool negativeAmbiguous = false;
                const HIRTypeData* candidateAssocType = canonicalAssocType;
                if (candidateAssocType) {
                    if (const auto* erased = candidateAssocType->opt_ErasedType()) {
                        if (const auto* alias = erased->inner.opt_Alias(); alias && resolve_.isOpaqueAliasDefiningScope(*alias->inner)) {
                            // A defining opaque is an output of alias-relate, not
                            // an input that can reject an otherwise valid impl.
                            // Return the projection response to the caller, which
                            // then equates it with this opaque and records its
                            // hidden type.
                            candidateAssocType = nullptr;
                        }
                    }
                }
                HIRTraitPath::assocListT rootAssociated;
                if (assocName && assocName[0] && candidateAssocType && !typeHasUnknown(candidateAssocType)) {
                    const HIRPathParams noAssocParams;
                    // The requirement projects through the trait that DECLARES
                    // the item: `Output` on an `FnMut` goal is declared on
                    // `FnOnce`, and only the declaring trait's projection folds
                    // through the elaborated ParamEnv equality.
                    auto sourceTrait = HIRGenericPath(trait, canonical.params.clone());
                    HIRGenericPath declaringTrait;
                    if (resolve_.traitContainsType(span(), sourceTrait, crate.getTraitByPath(span(), trait), assocName, declaringTrait)) {
                        sourceTrait = ::std::move(declaringTrait);
                    }
                    rootAssociated.insert({RcString::newInterned(assocName), HIRTraitPath::AtyEqual{::std::move(sourceTrait), canonicalAssocParams ? canonicalAssocParams->clone() : noAssocParams.clone(), candidateAssocType}});
                }
                for (size_t i = 0; i < candidateCount; i++) {
                    if (excludedImpl) {
                        const auto* traitImpl = frame.candidates[i]->impl.data.opt_TraitImpl();
                        if (traitImpl && traitImpl->impl == excludedImpl) {
                            frame.candidates[i]->traitCertainty = Certainty::NoSolution;
                            frame.candidates[i]->certainty = Certainty::NoSolution;
                            continue;
                        }
                    }
                    auto certainty = evaluateCandidate(frameIndex, i, trait, rootAssociated.empty() ? nullptr : &rootAssociated);
                    auto* candidate = frame.candidates[i];
                    candidate->traitCertainty = certainty;
                    if (!candidate->isNegative()) {
                        const auto assocCertainty = matchRootAssociated(trait, *candidate, assocName, candidateAssocType, canonicalAssocParams);
                        if (assocCertainty == Certainty::NoSolution) {
                            certainty = Certainty::NoSolution;
                        } else if (assocCertainty == Certainty::Ambiguous && certainty == Certainty::Proven) {
                            certainty = Certainty::Ambiguous;
                        }
                    }
                    candidate->certainty = certainty;
                    if (candidate->isNegative()) {
                        negativeProven |= certainty == Certainty::Proven;
                        negativeAmbiguous |= certainty == Certainty::Ambiguous;
                        continue;
                    }
                    suppressAutoBuiltin |= candidate->isPositiveMarkerImpl() && certainty != Certainty::NoSolution;
                    if (certainty != Certainty::NoSolution) {
                        frame.viable.push_back(candidate);
                    }
                }

                if (suppressAutoBuiltin || negativeProven) {
                    auto& viable = frame.viable;
                    viable.erase(
                        ::std::remove_if(
                            viable.begin(),
                            viable.end(),
                            [&](Candidate* candidate) {
                        return candidate->autoBuiltin;
                    }
                        ),
                        viable.end()
                    );
                } else if (negativeAmbiguous) {
                    for (auto* candidate : frame.viable) {
                        if (candidate->autoBuiltin && candidate->certainty == Certainty::Proven) {
                            candidate->certainty = Certainty::Ambiguous;
                        }
                    }
                }

                if (hasCoercionGoals) {
                    struct RelatedCandidate {
                        Candidate* candidate;
                        HIRTypeRef self;
                        HIRPathParams inputs;
                    };
                    ThinVector<RelatedCandidate> related;
                    for (auto* candidate : frame.viable) {
                        candidate->discarded = false;
                        candidate->coercionsProven = true;
                        auto self = candidate->impl.getImplType(crate.types);
                        auto inputs = candidate->impl.getTraitParams(crate.types);
                        for (const auto& constraint : canonicalCoercions) {
                            ASSERT_BUG(span(), constraint.isSelf || constraint.typeIndex < inputs.types.size(), "coercion-constrained trait input is out of range");
                            const auto* input = constraint.isSelf ? self : inputs.types[constraint.typeIndex];
                            const auto result = resolve_.evaluateCoercionGoal(span(), constraint, input, &candidate->coercionEqualities);
                            if (result == Certainty::NoSolution) {
                                candidate->discarded = true;
                                break;
                            }
                            if (result == Certainty::Ambiguous) {
                                candidate->coercionsProven = false;
                                candidate->certainty = Certainty::Ambiguous;
                                candidate->ambiguityBeyondHead = true;
                            }
                        }
                        if (!candidate->discarded) {
                            related.push_back(RelatedCandidate{candidate, ::std::move(self), ::std::move(inputs)});
                        }
                    }

                    // Keep the Pareto frontier across all input relations.  A
                    // head is dominated only when another endpoint is better
                    // on at least one edge and no worse on every other edge.
                    for (size_t i = 0; i < related.size(); i++) {
                        for (size_t j = 0; j < related.size(); j++) {
                            if (i == j) {
                                continue;
                            }
                            bool jBetter = false;
                            bool iBetter = false;
                            for (const auto& constraint : canonicalCoercions) {
                                const auto ordering = resolve_.compareCoercionEndpoints(
                                    span(),
                                    constraint,
                                    constraint.isSelf ? related[j].self : related[j].inputs.types[constraint.typeIndex],
                                    constraint.isSelf ? related[i].self : related[i].inputs.types[constraint.typeIndex]
                                );
                                jBetter |= ordering == OrdGreater;
                                iBetter |= ordering == OrdLess;
                            }
                            if (jBetter && !iBetter) {
                                related[i].candidate->discarded = true;
                                break;
                            }
                        }
                    }
                    frame.viable.erase(
                        ::std::remove_if(
                            frame.viable.begin(),
                            frame.viable.end(),
                            [](const Candidate* candidate) {
                                return candidate->discarded;
                            }
                        ),
                        frame.viable.end()
                    );
                }

                if (frame.viable.empty()) {
                    // solve_goal keeps an obligation ambiguous while inference
                    // still occurs in its inputs.  The response-producing path
                    // must preserve the same result: nested candidate evaluation
                    // calls it specifically to recover constraints from an
                    // ambiguous goal.  Returning false here would turn e.g.
                    // `<_ as IntoIterator>::IntoIter: Iterator` into NoSolution
                    // and incorrectly discard an enclosing `Zip` candidate.
                    //
                    // Only inference at the *root* of the self type warrants
                    // that: heads are selected by the self CONSTRUCTOR, so a
                    // concrete constructor with zero head matches stays
                    // NoSolution no matter what its arguments resolve to
                    // (`[?i; 3]: ExactSizeIterator` -- no impl has an array
                    // self; keeping it ambiguous lets the `&mut I` blanket
                    // shadow the inherent slice `len`).  A bare-ivar self can
                    // still match anything, and a rigid projection self is
                    // not a constructor -- inference elsewhere in the goal
                    // can still unlock an environment candidate for it.
                    const auto* noViableSelfPath = resolvedType->opt_Path();
                    const bool noViableSelfIsAlias = noViableSelfPath && (noViableSelfPath->binding.is_Opaque() || noViableSelfPath->binding.is_Unbound());
                    // Same repertoire rule as solve_goal's zero-candidate
                    // branch: a return-position opaque self with no matching
                    // head is rigid NoSolution, not ambiguity.
                    const auto* noViableSelfErased = resolvedType->opt_ErasedType();
                    const bool noViableSelfRigidOpaque = noViableSelfErased && noViableSelfErased->inner.is_Fcn();
                    bool noViableOpaque = !noViableSelfRigidOpaque && containsDefiningOpaque(resolvedType);
                    for (const auto& ty : goalParams.types) {
                        noViableOpaque |= containsDefiningOpaque(ty);
                    }
                    if (resolvedType->is_Infer()
                        || noViableOpaque
                        || (noViableSelfIsAlias && (resolve_.typeContainsIvars(resolvedType) || resolve_.paramsContainIvars(goalParams)))) {
                        return emitForcedAmbiguity();
                    }
                    return false;
                }

                // rustc prefers all ParamEnv responses when any applicable
                // non-global where-bound can answer this goal (dev-guide,
                // candidate preference): normalisation does not consider
                // impls when the trait goal is proven via ParamEnv, so a bare
                // `U: Trait` bound keeps `Trait::Assoc` rigid -- verified
                // against rustc -Znext-solver on the TryFrom/TryInto blanket
                // pair.
                const bool hasNonGlobalParamEnv = ::std::any_of(frame.viable.begin(), frame.viable.end(), [&](const Candidate* candidate) {
                    // ParamEnv preference applies only after the complete
                    // input relation proves that environment head. A fuzzy
                    // where-bound whose Self would have to be chosen through
                    // an unresolved call-site coercion is still merely one
                    // candidate and must not discard concrete impl heads.
                    return paramEnvCandidateIsNonGlobal(*candidate) && candidate->coercionsProven;
                });
                if (hasNonGlobalParamEnv) {
                    auto& viable = frame.viable;
                    viable.erase(
                        ::std::remove_if(
                            viable.begin(),
                            viable.end(),
                            [](const Candidate* candidate) {
                                return candidate->source != CandidateSource::ParamEnv && candidate->source != CandidateSource::AliasBound;
                    }
                        ),
                        viable.end()
                    );
                }

                // A proven builtin or alias-bound candidate shadows impl
                // candidates. ParamEnv candidates use the globalness rule below.
                bool hasPreferredNonImpl = false;
                for (const auto* candidate : frame.viable) {
                    hasPreferredNonImpl |= candidate->source != CandidateSource::ParamEnv && isEnvironmentOrBuiltin(candidate->impl) && candidate->certainty == Certainty::Proven;
                }
                if (hasPreferredNonImpl) {
                    auto& viable = frame.viable;
                    viable.erase(
                        ::std::remove_if(
                            viable.begin(),
                            viable.end(),
                            [&](Candidate* candidate) {
                        return !isEnvironmentOrBuiltin(candidate->impl);
                    }
                        ),
                        viable.end()
                    );
                }

                // Global where-bounds are a fallback. If any other candidate
                // applies, drop the global ParamEnv response before merging;
                // unlike a non-global where-bound, it must not guide inference.
                const bool hasNonParamEnv = ::std::any_of(frame.viable.begin(), frame.viable.end(), [](const Candidate* candidate) {
                    return candidate->source != CandidateSource::ParamEnv;
                });
                if (hasNonParamEnv) {
                    auto& viable = frame.viable;
                    viable.erase(
                        ::std::remove_if(
                            viable.begin(),
                            viable.end(),
                            [&](const Candidate* candidate) {
                        return candidate->source == CandidateSource::ParamEnv && !paramEnvCandidateIsNonGlobal(*candidate);
                    }
                        ),
                        viable.end()
                    );
                }

                // A definite environment head wins over other environment
                // predicates that only match by binding caller inference.
                // Associated declarations commonly carry several instances
                // of the same trait (`ISet2: From<ISet1> + From<u8>`): the
                // exact `From<u8>` predicate must prove that goal, while a
                // call with an unknown argument must keep both heads until
                // the argument type is known.
                const bool hasExactEnvironment = ::std::any_of(frame.viable.begin(), frame.viable.end(), [&](const Candidate* candidate) {
                    return isEnvironmentOrBuiltin(candidate->impl)
                        && candidate->headMatch == HIRCompare::Equal
                        && candidate->certainty == Certainty::Proven;
                });
                if (hasExactEnvironment) {
                    auto& viable = frame.viable;
                    viable.erase(
                        ::std::remove_if(
                            viable.begin(),
                            viable.end(),
                            [&](const Candidate* candidate) {
                        return isEnvironmentOrBuiltin(candidate->impl) && candidate->headMatch != HIRCompare::Equal;
                    }
                        ),
                        viable.end()
                    );
                }

                // Apply specialization only after nested goals have been probed.
                for (auto* candidate : frame.viable) {
                    candidate->discarded = false;
                    candidate->specializationItemSource = nullptr;
                }
                auto recordItemSource = [&](Candidate* winner, const Candidate* shadowed) {
                    if (!shadowed->impl.data.is_TraitImpl()) {
                        return;
                    }
                    if (!winner->specializationItemSource || shadowed->impl.moreSpecificThan(crate.types, winner->specializationItemSource->impl)) {
                        winner->specializationItemSource = shadowed;
                    }
                };
                for (size_t i = 0; i < frame.viable.size(); i++) {
                    if (frame.viable[i]->discarded) {
                        continue;
                    }
                    for (size_t j = i + 1; j < frame.viable.size(); j++) {
                        if (frame.viable[j]->discarded) {
                            continue;
                        }
                        auto& left = frame.viable[i]->impl;
                        auto& right = frame.viable[j]->impl;
                        // Specialization only distinguishes different canonical
                        // responses.  If both candidates constrain the caller in
                        // exactly the same way, merge their certainties instead;
                        // a proven route must not be discarded behind an
                        // ambiguous, cyclic route to the same response.
                        if (responsesEqual(left, right, assocName, canonicalAssocParams, valueName)) {
                            continue;
                        }
                        if (!left.data.is_TraitImpl() || !right.data.is_TraitImpl()) {
                            continue;
                        }
                        // evaluate_overlap is itself the recursive overlap query.
                        // Re-entering either overlap implementation here makes a
                        // coinductive pair recurse without a solver cycle head.
                        // Keeping both responses is conservative: ambiguity is
                        // already sufficient to report that the impls may overlap.
                        if (coherenceMode || !resolve_.implsOverlap(span(), left, right)) {
                            continue;
                        }
                        // A more-specific impl with an ambiguous where-clause
                        // cannot shadow the fallback: that nested goal may still
                        // fail.  Head ambiguity alone is inference guidance and
                        // remains eligible for specialization.
                        if (left.moreSpecificThan(crate.types, right)) {
                            if (!frame.viable[i]->ambiguityBeyondHead) {
                                frame.viable[j]->discarded = true;
                                recordItemSource(frame.viable[i], frame.viable[j]);
                            } else if (frame.viable[j]->certainty == Certainty::Proven) {
                                // The specialising predicate is not known in
                                // this generic environment. It cannot block a
                                // proven default impl; monomorphisation will
                                // select the specialisation if the predicate
                                // becomes proven for the concrete instance.
                                frame.viable[i]->discarded = true;
                            }
                        } else if (right.moreSpecificThan(crate.types, left)) {
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
                frame.viable.erase(
                    ::std::remove_if(
                        frame.viable.begin(),
                        frame.viable.end(),
                        [](const Candidate* candidate) {
                    return candidate->discarded;
                }
                    ),
                    frame.viable.end()
                );

                bool oneResponse = true;
                for (size_t i = 1; i < frame.viable.size(); i++) {
                    if (!responsesEqual(frame.viable.front()->impl, frame.viable[i]->impl, assocName, canonicalAssocParams, valueName)) {
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
                        // The trait head is known, but its call-site relation
                        // is not.  Do not leak that head's slots while the
                        // relation can still fail; ordinary solver ambiguity
                        // will be revisited after inference changes.
                        return emitForcedAmbiguity();
                    }
                    // Among equal responses, prefer the one that actually
                    // carries the requested associated value: the bare
                    // predicate variant of the same where-clause cannot
                    // answer the normalizes-to part.
                    if (assocName && assocName[0]) {
                        const HIRPathParams noItemParams;
                        const auto& itemParams = canonicalAssocParams ? *canonicalAssocParams : noItemParams;
                        for (auto* candidate : frame.viable) {
                            if ((!hasCoercionGoals || candidate->coercionsProven)
                                && candidate->certainty == selected->certainty
                                && candidate->impl.getType(crate.types, assocName, itemParams) != HIRTypeRef()) {
                                selected = candidate;
                                break;
                            }
                        }
                    }
                    const auto certainty = selected->certainty;
                    // rustc specialization graph: a specialising impl that
                    // omits an associated item inherits the nearest shadowed
                    // ancestor's value.  Projecting it is legal only when the
                    // ancestor declared the item final (no `default`); a
                    // `default` value stays rigid here.  The trait goal itself
                    // must be proven -- only the missing item downgraded it.
                    if (assocName && assocName[0] && selected->impl.data.is_TraitImpl() && selected->traitCertainty == Certainty::Proven) {
                        const HIRPathParams noParams;
                        const auto& itemParams = canonicalAssocParams ? *canonicalAssocParams : noParams;
                        if (selected->impl.getType(crate.types, assocName, itemParams) == HIRTypeRef()) {
                            for (const Candidate* source = selected->specializationItemSource; source; source = source->specializationItemSource) {
                                const auto* sourceImpl = source->impl.data.opt_TraitImpl();
                                if (!sourceImpl || !sourceImpl->impl) {
                                    break;
                                }
                                const auto it = sourceImpl->impl->types.find(assocName);
                                if (it == sourceImpl->impl->types.end()) {
                                    continue;
                                }
                                if (it->second.isSpecialisable) {
                                    break;
                                }
                                auto inherited = source->impl.getType(crate.types, assocName, itemParams);
                                if (inherited == HIRTypeRef()) {
                                    break;
                                }
                                auto implType = selected->impl.getImplType(crate.types);
                                auto traitParams = selected->impl.getTraitParams(crate.types);
                                auto sourceTrait = HIRGenericPath(trait, traitParams.clone());
                                HIRTraitPath::assocListT associated;
                                associated.insert({RcString::newInterned(assocName), HIRTraitPath::AtyEqual{::std::move(sourceTrait), itemParams.clone(), ::std::move(inherited)}});
                                return emitResponse(ImplRef(::std::move(implType), ::std::move(traitParams), ::std::move(associated)), HIRCompare::Equal);
                            }
                        }
                    }
                    const Candidate* responseSource = selected;
                    if (valueName && selected->impl.data.is_TraitImpl()) {
                        responseSource = specializationValueSource(selected, valueName);
                        if (!responseSource) {
                            // The selected specialization chain has no impl
                            // body for this item.  The caller may still use a
                            // trait-provided default, but there is no impl
                            // provider to publish.
                            return false;
                        }
                    }
                    auto selectedResponse = monomorphImplRef(responseSource->impl, MonomorphiserNop(crate.types));
                    if (certainty != Certainty::Proven) {
                        // A unique but conditional ParamEnv/builtin response
                        // still has a canonical projection value.  Materialise
                        // it so the associated equality is returned as a typed
                        // effect; the consumer must not reconstruct it from
                        // ImplRef just because certainty is ambiguous.
                        return emitResponse(materializeRootAssociated(::std::move(selectedResponse), trait, assocName, canonicalAssocParams), HIRCompare::Fuzzy, selected);
                    }
                    return emitResponse(materializeRootAssociated(::std::move(selectedResponse), trait, assocName, canonicalAssocParams), HIRCompare::Equal, selected);
                }

                // Distinct canonical responses cannot guide inference through
                // one committed response.  Keep them locally so emitResponse
                // can publish only slot assignments shared by every viable
                // head and any aggregate query facts.
                for (auto* candidate : frame.viable) {
                    const auto exportedCmp = candidate->headMatch == HIRCompare::Equal && !candidate->nestedAmbiguity ? HIRCompare::Equal : HIRCompare::Fuzzy;
                    distinctViable.pushBack({candidate, exportedCmp});
                }
                auto ambiguous = ImplRef(resolvedType, goalParams.clone(), HIRTraitPath::assocListT());
                ambiguous.markAmbiguousIdentity();
                return emitResponse(materializeRootAssociated(::std::move(ambiguous), trait, assocName, canonicalAssocParams), HIRCompare::Fuzzy);
            }

            bool evaluateNormalizesTo(const Span& callSpan, const NormalizesTo& goal, NormalizesToCallback& callback, bool callerBoundary = false) {
                const auto* path = goal.projection->opt_Path();
                const auto* projection = path ? path->path.data.opt_UfcsKnown() : nullptr;
                ASSERT_BUG(callSpan, projection, "NormalizesTo goal is not an associated-type projection: " << goal.projection);

                HIRGenericPath declaringTrait;
                if (!resolve_.traitContainsType(callSpan, projection->trait, crate.getTraitByPath(callSpan, projection->trait.path), projection->item.c_str(), declaringTrait)) {
                    BUG(callSpan, "Cannot find associated type " << projection->item << " anywhere in trait " << projection->trait);
                }

                // The destination is a solver-protocol variable, not caller
                // inference state. Canonicalisation still records it as an
                // ordinary response slot, while an unresolved projection can
                // now be retried without growing the HM table on every pass.
                const auto outputSlot = crate.types.infer(HIR_INFER_SOLVER_NORMALIZES_TO_OUTPUT, HIRInferClass::None);
                auto adapter = makeCallable<SolverResponseCb>([&](SolverResponse response) {
                    HIRTypeRef output = nullptr;
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
                        retainedEqualities.push_back(::std::move(equality));
                    }
                    // Unbound/Opaque is resolution state, not part of an
                    // alias's semantic identity.  A solver response that only
                    // changes those bindings is the original projection, not
                    // a normalised output; publishing it makes the static
                    // walker recurse forever while toggling child bindings.
                    if (output && output->equalsIgnoringRegions(goal.projection)) {
                        output = nullptr;
                    }
                    // A bare ParamEnv predicate can prove the trait without
                    // defining the requested associated value.  In that case
                    // the response carries only the protocol destination back
                    // as its own value.  This is not a normalization result:
                    // replacing the projection with that auxiliary ivar loses
                    // the projection's declared bounds (for example
                    // `I::IntoIter: Iterator<Item = I::Item>`).
                    if (output == outputSlot) {
                        output = nullptr;
                    }
                    response.equalities = ::std::move(retainedEqualities);

                    // The fresh destination belongs to the NormalizesTo
                    // protocol, not to the consumer's inference state.  Keep
                    // the selected value separately and strip the auxiliary
                    // identity slot before applying the remaining effects;
                    // otherwise Context::equateTypes would recursively try to
                    // normalise the value while this goal is still active.
                    SolverSlotValues retainedSlots;
                    for (size_t i = 0; i < response.slots.typeInputs.size(); i++) {
                        if (response.slots.typeInputs[i] == outputSlot) {
                            continue;
                        }
                        retainedSlots.typeInputs.push_back(response.slots.typeInputs[i]);
                        retainedSlots.types.push_back(response.slots.types[i]);
                    }
                    retainedSlots.valueInputs = ::std::move(response.slots.valueInputs);
                    retainedSlots.values = ::std::move(response.slots.values);
                    response.slots = ::std::move(retainedSlots);
                    return callback.visit(NormalizesToResponse{::std::move(response), ::std::move(output)});
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
        };

        TraitResolution::TraitResolution(HMTypeInferrence& ivars, const WireBoard& wb, const HIRGenericParams* implParams, const HIRGenericParams* itemParams, const HIRSimplePath& visPath, const HIRGenericPath* currentTrait)
            : TraitResolveCommon(wb)
            , langDeref_(crate.getLangItemPathOpt("deref"))
            , ivars(ivars)
            , visPath(visPath)
            , currentTraitPath_(currentTrait)
            , currentTraitPtr(currentTrait ? &crate.getTraitByPath(Span(), currentTrait->path) : nullptr)
            , eatCachePool(stl::ObjPool::fromMemory())
            , eatCache(eatCachePool.mutPtr())
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
            if (::std::find(opaqueAliasScopes.begin(), opaqueAliasScopes.end(), path) == opaqueAliasScopes.end()) {
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

        void TraitResolution::addDefiningOpaqueAlias(const HIRSimplePath& path) {
            if (::std::find(definingOpaqueAliases.begin(), definingOpaqueAliases.end(), path) == definingOpaqueAliases.end()) {
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
            // `ivars` is a reference member: constness of the resolver does
            // not propagate through it, so no cast is needed to mutate.
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

        bool TraitResolution::implsOverlap(const Span& sp, const ImplRef& left, const ImplRef& right) const {
            const auto* leftImpl = left.data.opt_TraitImpl();
            const auto* rightImpl = right.data.opt_TraitImpl();
            if (!leftImpl || !rightImpl || !leftImpl->impl || !rightImpl->impl) {
                return left.overlapsWith(crate, right);
            }
            if (!leftImpl->traitPath || !rightImpl->traitPath || *leftImpl->traitPath != *rightImpl->traitPath) {
                return false;
            }
            if (leftImpl->impl == rightImpl->impl) {
                return true;
            }

            // Structural head unification with binding consistency is a
            // necessary condition: `Extend<T>` and `Extend<&T>` for the same
            // self can never overlap (`T = &T` is an infinite type).  The
            // goal probe below over-approximates here because its one-way
            // matcher drops goal-side inference constraints (no occurs
            // check), which would let specialization discard a live impl.
            // Heads only: the legacy bound walk recurses without a cycle
            // guard on coinductive marker traits; bounds are the probe's job.
            if (!leftImpl->impl->overlapsWith(crate, *rightImpl->impl)) {
                return false;
            }

            // The probe runs on the caller's own inference table under a
            // snapshot: every binding and fresh variable it creates is rolled
            // back, so nothing escapes into the type-checking context.  A
            // dedicated evaluator keeps the probe's goal bookkeeping out of
            // any evaluation session currently on the stack.
            if (!coherenceEvaluator) {
                ASSERT_BUG(sp, crate.pool, "next-solver coherence requires the crate object pool");
                coherenceEvaluator = crate.pool->make<NextTraitGoalEvaluator>(*this, crate);
            }
            auto snapshot = ivars.snapshot();
            STD_DEFER {
                ivars.rollbackTo(snapshot);
            };
            return coherenceEvaluator->evaluateOverlap(sp, *leftImpl->traitPath, *leftImpl->impl, *rightImpl->impl);
        }

        bool TraitResolution::solveTraitGoalCb(const Span& sp, const HIRSimplePath& trait, const HIRPathParams& params, const HIRTypeData* type, SolverResponseCallback& callback, const TraitGoalQuery& query) const {
            if (!nextSolver) {
                ASSERT_BUG(sp, crate.pool, "next-solver requires the crate object pool");
                nextSolver = crate.pool->make<NextTraitGoalEvaluator>(*this, crate);
            }
            return nextSolver->evaluateTyped(sp, trait, params, type, callback, query, true);
        }

        SolverCertainty TraitResolution::solveNonBuiltinTraitGoal(const Span& sp, const HIRSimplePath& trait, const HIRTypeData* type) const {
            if (!nonBuiltinSolver) {
                ASSERT_BUG(sp, crate.pool, "next-solver requires the crate object pool");
                nonBuiltinSolver = crate.pool->make<NextTraitGoalEvaluator>(*this, crate);
            }
            SolverCertainty certainty = SolverCertainty::NoSolution;
            auto callback = makeCallable<SolverResponseCb>([&](SolverResponse response) {
                if (!response.hasImpl) {
                    return false;
                }
                certainty = response.certainty;
                return true;
            });
            nonBuiltinSolver->evaluateTyped(
                sp,
                trait,
                HIRPathParams{},
                type,
                callback,
                {.assocName = "", .allowInferInputs = true},
                true,
                false
            );
            return certainty;
        }

        bool TraitResolution::solveNormalizesToCb(const Span& sp, const NormalizesTo& goal, NormalizesToCallback& callback) const {
            if (!nextSolver) {
                ASSERT_BUG(sp, crate.pool, "next-solver requires the crate object pool");
                nextSolver = crate.pool->make<NextTraitGoalEvaluator>(*this, crate);
            }
            return nextSolver->evaluateNormalizesTo(sp, goal, callback, true);
        }

        // -------------------------------------------------------------------------------------------------------------------
        // -------------------------------------------------------------------------------------------------------------------

        void TraitResolution::compactIvars(HMTypeInferrence& ivars, SolverResponseCallback* effects) {
            ASSERT_BUG(Span(), !ivars.probing(), "ivar compaction during an active inference snapshot");
            ivars.checkForLoops();

            // Solver responses may append auxiliary output slots while a
            // projection is normalised.  They did not exist at compaction
            // entry and no pre-existing type can refer to them, so processing
            // the growing tail would manufacture one new slot per visit and
            // make this pass non-terminating.
            const auto initialIvarCount = ivars.ivars.size();
            for (unsigned int i = 0; i < initialIvarCount; i++) {
                if (!ivars.ivars[i].isAlias()) {
                    auto type = ivars.ivars[i].type;
                    ivars.expandIvars(type);
                    // Don't expand unless it is needed
                    if (this->hasAssociatedType(type)) {
                        auto normalized = this->expandAssociatedTypes(Span(), type, effects);
                        // Applying a typed response can allocate inference
                        // variables and reallocate the table.  Never retain a
                        // reference into `ivars` across that callback.
                        if (!ivars.ivars[i].isAlias()) {
                            ivars.ivars[i].type = normalized;
                        }
                    } else {
                        ivars.ivars[i].type = type;
                    }
                } else {
                    auto index = ivars.ivars[i].alias;
                    unsigned int count = 0;
                    assert(index < ivars.ivars.size());
                    while (ivars.ivars.at(index).isAlias()) {
                        index = ivars.ivars.at(index).alias;

                        if (count >= ivars.ivars.size()) {
                            BUG(Span(), "Loop detected in ivar list when starting at " << ivars.ivars[i].alias << ", current is " << index);
                        }
                        count++;
                    }
                    ivars.ivars[i].alias = index;
                }
            }
        }

        bool TraitResolution::hasAssociatedType(const HIRTypeData* input) const {
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
                            if (r.hasAssociatedType(e2.type)) return true; if (H::checkPathparams(r, e2.params)) return true; return false;
                            break;
                        }
                        case HIRPath::Data::TAG_UfcsKnown: {
                            auto& e2 = p.data.as_UfcsKnown();
                            if (r.hasAssociatedType(e2.type)) return true; if (H::checkPathparams(r, e2.trait.params)) return true; if (H::checkPathparams(r, e2.params)) return true; return false;
                            break;
                        }
                        case HIRPath::Data::TAG_UfcsUnknown: {
                            BUG(Span(), "Encountered UfcsUnknown - " << p);
                            break;
                        }
                    }
                    UNREACHABLE();
                }
            };

    switch ((*input).tag()) {
        case HIRTypeData::TAG_Infer: {
            const auto& ty = this->ivars.getType(input);
            if (ty != input) {
                return this->hasAssociatedType(ty);
            }
            return false;
        }
        case HIRTypeData::TAG_Diverge: {
            return false;
        }
        case HIRTypeData::TAG_Primitive: {
            return false;
        }
        case HIRTypeData::TAG_Path: {
            auto& e = (*input).as_Path();
            // Both states still need projection normalisation. `Opaque` means
            // that no rule was available at the previous attempt, not that
            // the projection can never become known.
            if (e.path.data.is_UfcsKnown() && (e.binding.is_Unbound() || e.binding.is_Opaque())) {
                return true;
            }
            return H::checkPath(*this, e.path);
        }
        case HIRTypeData::TAG_Generic: {
            return false;
        }
        case HIRTypeData::TAG_TraitObject: {
            auto& e = (*input).as_TraitObject();
            // Recurse?
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
        case HIRTypeData::TAG_ErasedType: {
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
            for(const auto& m : e.traits) {
                    if (H::checkPathparams(*this, m.path.params)) {
                        return true;
                    }
            }
            return false;
        }
        case HIRTypeData::TAG_Array: {
            auto& e = (*input).as_Array();
            return hasAssociatedType(e.inner);
        }
        case HIRTypeData::TAG_Slice: {
            auto& e = (*input).as_Slice();
            return hasAssociatedType(e.inner);
        }
        case HIRTypeData::TAG_Pattern: {
            auto& e = (*input).as_Pattern();
            return hasAssociatedType(e.inner);
        }
        case HIRTypeData::TAG_Tuple: {
            auto& e = (*input).as_Tuple();
            bool rv = false;
            for (const auto& sub : e) {
                rv |= hasAssociatedType(sub);
            }
            return rv;
        }
        case HIRTypeData::TAG_Borrow: {
            auto& e = (*input).as_Borrow();
            return hasAssociatedType(e.inner);
        }
        case HIRTypeData::TAG_Pointer: {
            auto& e = (*input).as_Pointer();
            return hasAssociatedType(e.inner);
        }
        case HIRTypeData::TAG_NamedFunction: {
            auto& e = (*input).as_NamedFunction();
            return H::checkPath(*this, e.path);
        }
        case HIRTypeData::TAG_Function: {
            // Recurse?
            return false;
        }
        case HIRTypeData::TAG_NodeType: {
            // Recurse?
            return false;
        }
    }
    BUG(Span(), "Fell off the end of has_associated_type - input=" << input);
        }

        void TraitResolution::expandAssociatedTypesInplace(const Span& sp, HIRTypeRef& input, SolverResponseCallback* effects) const {
            struct H {
                static void expandAssociatedTypesParams(const Span& sp, const TraitResolution& res, HIRPathParams& params, SolverResponseCallback* effects) {
                    for (auto& arg : params.types) {
                        res.expandAssociatedTypesInplace(sp, arg, effects);
                    }
                }

                static void expandAssociatedTypesTp(const Span& sp, const TraitResolution& res, HIRTraitPath& input, SolverResponseCallback* effects) {
                    expandAssociatedTypesParams(sp, res, input.path.params, effects);
                    for (auto& arg : input.typeBounds) {
                        expandAssociatedTypesParams(sp, res, arg.second.sourceTrait.params, effects);
                        res.expandAssociatedTypesInplace(sp, arg.second.type, effects);
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
        case HIRTypeData::TAG_Infer: {
            const auto* ty = this->ivars.getType(input);
            if (ty != input) {
                input = ty;
                expandAssociatedTypesInplace(sp, input, effects);
                return;
            }
            break;
        }
        case HIRTypeData::TAG_Diverge: {
            break;
        }
        case HIRTypeData::TAG_Primitive: {
            break;
        }
        case HIRTypeData::TAG_Path: {
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
                    expandAssociatedTypesInplace(sp, pe.type, effects);
                    H::expandAssociatedTypesParams(sp, *this, pe.params, effects);
                    H::expandAssociatedTypesParams(sp, *this, pe.implParams, effects);
                    input = crate.types.intern(mv$(data));
                    if (this->expandAssociatedTypesInplaceUfcsInherent(sp, input, effects)) {
                        this->expandAssociatedTypesInplace(sp, input, effects);
                    }
                    return;
                }
                case HIRPathData::TAG_UfcsKnown: {
                    auto& pe = e.path.data.as_UfcsKnown();
                    expandAssociatedTypesInplace(sp, pe.type, effects);
                    H::expandAssociatedTypesParams(sp, *this, pe.params, effects);
                    const auto& traitDef = crate.getTraitByPath(sp, pe.trait.path);
                    ConvertHIRConstantEvaluateMethodParams(sp, this->wb, crate, &traitDef.params, pe.trait.params);
                    H::expandAssociatedTypesParams(sp, *this, pe.trait.params, effects);
                    input = crate.types.intern(mv$(data));
                    // Retry opaque projections too: equality bounds can be
                    // learned after an earlier normalisation attempt.
                    const bool wasUnbound = input->as_Path().binding.is_Unbound();
                    const bool wasOpaque = input->as_Path().binding.is_Opaque();
                    if (wasUnbound || wasOpaque) {
                        if (wasOpaque) {
                            this->expandAssociatedTypesInplaceUfcsKnown(sp, input, effects);
                            return;
                        }

                        // Cache the result of this to avoid needing to do the full resolution too often.
                        // - This avoids VERY slow typechecking in 1.90's librustc_target
                        const auto cacheKey = input->uid;
                        // A probe is rolled back as a unit, but normalization
                        // can manufacture an output containing one of its
                        // temporary ivars even when the input projection is
                        // concrete.  Such an output must never cross the
                        // transaction through this resolver-lifetime cache.
                        auto* cached = ivars.probing() ? nullptr : eatCache.find(cacheKey);
                        if (cached && cached->generation == eatCacheGeneration
                            && (!((input->flags | cached->type->flags) & (HIRTypeData::HAS_TYPE_INFER | HIRTypeData::HAS_DEFERRED_CONST)) || cached->ivarGeneration == ivars.mutationGeneration)) {
                            if (input != cached->type) {
                                this->expandAssociatedTypesInplace(sp, cached->type, effects);
                            }
                            input = cached->type;
                        } else {
                            this->expandAssociatedTypesInplaceUfcsKnown(sp, input, effects);
                            if (input->is_Path() && (input->as_Path().binding.is_Unbound() || input->as_Path().binding.is_Opaque())) {
                            } else if (!ivars.probing()) {
                                eatCache.insert(cacheKey, EatCacheEntry{eatCacheGeneration, ivars.mutationGeneration, input});
                            }
                        }
                    }
                    return;
                }
                case HIRPathData::TAG_UfcsUnknown: {
                    auto& pe = e.path.data.as_UfcsUnknown();
                    // ResolveUFCS has not selected the declaring trait yet.
                    // Its receiver and explicit item arguments are still
                    // ordinary children of the type walker; the path itself
                    // remains retryable until that later phase.
                    expandAssociatedTypesInplace(sp, pe.type, effects);
                    H::expandAssociatedTypesParams(sp, *this, pe.params, effects);
                    break;
                }
            }
            break;
        }
        case HIRTypeData::TAG_Generic: {
            break;
        }
        case HIRTypeData::TAG_TraitObject: {
            auto& e = data.as_TraitObject();
            // Recurse?
            H::expandAssociatedTypesTp(sp, *this, e.trait, effects);
            for (auto& m : e.markers) {
                H::expandAssociatedTypesParams(sp, *this, m.params, effects);
            }
            break;
        }
        case HIRTypeData::TAG_ErasedType: {
            // Recurse?
            break;
        }
        case HIRTypeData::TAG_Array: {
            auto& e = data.as_Array();
            ConvertHIRConstantEvaluateArraySize(sp, this->wb, crate, visPath, e.size);
            expandAssociatedTypesInplace(sp, e.inner, effects);
            break;
        }
        case HIRTypeData::TAG_Slice: {
            auto& e = data.as_Slice();
            expandAssociatedTypesInplace(sp, e.inner, effects);
            break;
        }
        case HIRTypeData::TAG_Pattern: {
            auto& e = data.as_Pattern();
            expandAssociatedTypesInplace(sp, e.inner, effects);
            for (auto& range : e.pattern.alternatives) {
                HIRConstGeneric* values[] = {range.hasStart ? &range.start : nullptr, range.hasEnd ? &range.end : nullptr};
                for (auto* value : values) {
                    if (value) ConvertHIRConstantEvaluateConstGeneric(sp, this->wb, crate, e.inner, *value);
                }
            }
            break;
        }
        case HIRTypeData::TAG_Tuple: {
            auto& e = data.as_Tuple();
            for (auto& sub : e) {
                expandAssociatedTypesInplace(sp, sub, effects);
            }
            break;
        }
        case HIRTypeData::TAG_Borrow: {
            auto& e = data.as_Borrow();
            expandAssociatedTypesInplace(sp, e.inner, effects);
            break;
        }
        case HIRTypeData::TAG_Pointer: {
            auto& e = data.as_Pointer();
            expandAssociatedTypesInplace(sp, e.inner, effects);
            break;
        }
        case HIRTypeData::TAG_NamedFunction: {
            auto& e = data.as_NamedFunction();
            switch (e.path.data.tag()) {
                case HIRPathData::TAG_Generic: {
                    auto& pe = e.path.data.as_Generic();
                    H::expandAssociatedTypesParams(sp, *this, pe.params, effects);
                    break;
                }
                case HIRPathData::TAG_UfcsInherent: {
                    auto& pe = e.path.data.as_UfcsInherent();
                    expandAssociatedTypesInplace(sp, pe.type, effects);
                    H::expandAssociatedTypesParams(sp, *this, pe.params, effects);
                    break;
                }
                case HIRPathData::TAG_UfcsKnown: {
                    auto& pe = e.path.data.as_UfcsKnown();
                    expandAssociatedTypesInplace(sp, pe.type, effects);
                    H::expandAssociatedTypesParams(sp, *this, pe.params, effects);
                    H::expandAssociatedTypesParams(sp, *this, pe.trait.params, effects);
                    break;
                }
                case HIRPathData::TAG_UfcsUnknown: {
                    auto& pe = e.path.data.as_UfcsUnknown();
                    expandAssociatedTypesInplace(sp, pe.type, effects);
                    H::expandAssociatedTypesParams(sp, *this, pe.params, effects);
                    break;
                }
            }
            // TODO: Should this re-populate `def`? Not right now, assuming it's set once only
            break;
        }
        case HIRTypeData::TAG_Function: {
            auto& e = data.as_Function();
            for (auto& ty : e.argTypes) {
                expandAssociatedTypesInplace(sp, ty, effects);
            }
            expandAssociatedTypesInplace(sp, e.rettype, effects);
            break;
        }
        case HIRTypeData::TAG_NodeType: {
            // Recurse? Nah.
            break;
        }
    }
            input = crate.types.intern(mv$(data));
        }

        bool TraitResolution::expandAssociatedTypesInplaceUfcsInherent(const Span& sp, HIRTypeRef& input, SolverResponseCallback* effects) const {
            ASSERT_BUG(sp, input->is_Path() && input->as_Path().path.data.is_UfcsInherent(), input);

            const auto& pe = input->as_Path().path.data.as_UfcsInherent();
            const HIRTypeAlias* alias = nullptr;
            const HIRGenericParams* implParamsDef = nullptr;
            const HIRTypeImpl* selectedImpl = nullptr;
            HIRPathParams implParams;
            HIRCompare bestMatch = HIRCompare::Unequal;
            const HIRPathParams noTraitParams;

            crate.findTypeImpls(pe.type, ivars.callbackResolveInfer(), [&](const auto& impl) {
                const auto itemIt = impl.types.find(pe.item);
                if (itemIt == impl.types.end()) {
                    return false;
                }

                HIRPathParams candidateParams;
                const auto match = this->fticCheckParams(sp, HIRSimplePath(), nullptr, pe.type, impl.params, noTraitParams, impl.type, candidateParams);
                if (match != HIRCompare::Unequal && (bestMatch == HIRCompare::Unequal || match == HIRCompare::Equal)) {
                    alias = &itemIt->second.data;
                    implParamsDef = &impl.params;
                    selectedImpl = &impl;
                    implParams = mv$(candidateParams);
                    bestMatch = match;
                }
                return bestMatch == HIRCompare::Equal;
            });

            if (!alias) {
                return false;
            }

            ConvertHIRConstantEvaluateMethodParams(sp, this->wb, crate, implParamsDef, implParams);
            if (effects) {
                auto selectedType = MonomorphStatePtr(crate.types, nullptr, &implParams, nullptr).monomorphType(sp, selectedImpl->type);
                SolverResponse response;
                response.certainty = SolverCertainty::Proven;
                response.equalities.push_back(SolverTypeEquality{pe.type, selectedType});
                effects->visit(::std::move(response));
            }

            auto itemParams = pe.params.clone();
            if (itemParams.types.size() != alias->params.types.size() || itemParams.values.size() != alias->params.values.size()) {
                ERROR(sp, E0000, "Incorrect generic arguments for inherent associated type " << input);
            }
            ConvertHIRConstantEvaluateMethodParams(sp, this->wb, crate, &alias->params, itemParams);

            input = MonomorphStatePtr(crate.types, pe.type, &implParams, &itemParams).monomorphType(sp, alias->type);
            return true;
        }

        void TraitResolution::expandAssociatedTypesInplaceUfcsKnown(const Span& sp, HIRTypeRef& input, SolverResponseCallback* effects) const {
            ASSERT_BUG(sp, input->is_Path() && input->as_Path().path.data.is_UfcsKnown(), input);

            bool normalized = false;
            this->solveNormalizesTo(sp, NormalizesTo{input}, [&](NormalizesToResponse response) {
                if (response.output == HIRTypeRef() || response.output == input) {
                    return true;
                }
                if (effects) {
                    effects->visit(::std::move(response.effects));
                }
                input = ::std::move(response.output);
                normalized = true;
                return true;
            });
            if (normalized) {
                this->expandAssociatedTypesInplace(sp, input, effects);
                return;
            }

            // A projection over caller inference remains retryable.  A rigid
            // unresolved projection is an opaque alias which can participate
            // in later bounds and method lookup.
            if (!this->ivars.typeContainsIvars(input, false)) {
                auto data = input->cloneData();
                data.as_Path().binding = HIRTypePathBinding::make_Opaque({});
                input = crate.types.intern(::std::move(data));
            }
        }

        // -------------------------------------------------------------------------------------------------------------------
        // -------------------------------------------------------------------------------------------------------------------
        bool TraitResolution::findNamedTraitInTraitCb(const Span& sp, const HIRSimplePath& des, const HIRPathParams& desParams, const HIRTrait& traitPtr, const HIRSimplePath& traitPath, const HIRPathParams& pp, const HIRTypeData* targetType, TraitPathCallback& callback) const {
            if (pp.types.size() != traitPtr.params.types.size()) {
                BUG(sp, "Incorrect number of parameters for trait " << traitPath);
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
                    // NOTE: Doesn't quite work...
                    //if( cmp != ::HIR::Compare::Unequal )
                    //{
                    if (callback.visit(ptMono)) {
                        return true;
                    }
                    //}
                }
            }

            return false;
        }

        bool TraitResolution::assembleParamEnvCandidatesCb(const Span& sp, const HIRSimplePath& trait, const HIRPathParams& params, const HIRTypeData* type, TraitImplCallback& callback) const {
            const HIRPath::Data::Data_UfcsKnown* assocInfo = nullptr;
            if (const auto* e = type->opt_Path()) {
                assocInfo = e->path.data.opt_UfcsKnown();
            }

            // If the type is a fully unknown type, then don't bother looking?
            // - Ah, but what if the prams provide sufficient information?
            // - TODO: Determine if the params could provide enough info to be worth checking for bounds.
            if (type->is_Infer() && !type->as_Infer().isLit() && !isSolverCanonicalInfer(type->as_Infer().index)) {
                return false;
            }

            // NOTE: Even if the type is completely unknown (unbound UFCS), search the bound list.

            // TODO: A bound can imply something via its associated types. How deep can this go?
            // E.g. `T: IntoIterator<Item=&u8>` implies `<T as IntoIterator>::IntoIter : Iterator<Item=&u8>`
            // > Would maybe want a list of all explicit and implied bounds instead.
            {
                struct HrtbBoundMatcher: public HIRMatchGenerics, public Monomorphiser {
                    Span sp;
                    stl::ObjPool::Ref scratchPool;
                    stl::IntMap<HIRTypeRef> hrtbTypes;
                    stl::IntMap<HIRConstGeneric> hrtbValues;

                    HrtbBoundMatcher(Span sp, HIRTypeInterner& types)
                        : HIRMatchGenerics(types.objectPool())
                        , Monomorphiser(types)
                        , sp(sp)
                        , scratchPool(stl::ObjPool::fromMemory())
                        , hrtbTypes(scratchPool.mutPtr())
                        , hrtbValues(scratchPool.mutPtr())
                    {
                    }

                    HIRCompare matchTy(const HIRGenericRef& generic, const HIRTypeData* type, tCbResolveType resolve) override {
                        if (generic.group() != GENERICHrtb) {
                            return types.generic(generic.name, generic.binding)->compareWithPlaceholders(sp, type, resolve);
                        }
                        auto* existing = hrtbTypes.find(generic.binding);
                        if (!existing) {
                            hrtbTypes.insert(generic.binding, type);
                            return HIRCompare::Equal;
                        }
                        return (*existing)->compareWithPlaceholders(sp, type, resolve);
                    }

                    HIRCompare matchVal(const HIRGenericRef& generic, const HIRConstGeneric& value) override {
                        if (generic.group() != GENERICHrtb) {
                            if (value.is_Infer()) {
                                return HIRCompare::Fuzzy;
                            }
                            return value.is_Generic() && value.as_Generic() == generic ? HIRCompare::Equal : HIRCompare::Unequal;
                        }
                        auto* existing = hrtbValues.find(generic.binding);
                        if (!existing) {
                            hrtbValues.insert(generic.binding, value.clone());
                            return HIRCompare::Equal;
                        }
                        if (*existing == value) {
                            return HIRCompare::Equal;
                        }
                        return existing->is_Infer() || value.is_Infer() ? HIRCompare::Fuzzy : HIRCompare::Unequal;
                    }

                    HIRTypeRef getType(const Span&, const HIRGenericRef& generic) const override {
                        if (generic.group() == GENERICHrtb) {
                            if (auto* type = hrtbTypes.find(generic.binding)) {
                                return *type;
                            }
                        }
                        return types.generic(generic.name, generic.binding);
                    }

                    HIRConstGeneric getValue(const Span&, const HIRGenericRef& generic) const override {
                        if (generic.group() == GENERICHrtb) {
                            if (auto* value = hrtbValues.find(generic.binding)) {
                                return value->clone();
                            }
                        }
                        return generic;
                    }
                };

                bool rv = this->iterateBoundsTraits(sp, type, trait, [&](HIRCompare cmp, const HIRTypeData* boundTy, const HIRGenericPath& boundTrait, const CachedBound& boundInfo) -> bool {
                    const auto& storedParams = boundTrait.params;
                    HIRPathParams normalisedParams;
                    const HIRPathParams* bParams = &storedParams;
                    if (::std::any_of(storedParams.types.begin(), storedParams.types.end(), [&](const auto& ty) {
                        return this->hasAssociatedType(ty);
                    })) {
                        normalisedParams = storedParams.clone();
                        this->expandAssociatedTypesParams(sp, normalisedParams);
                        bParams = &normalisedParams;
                    }


                    // Check against `params`
                    auto ord = cmp;
                    const bool hasHrtb = ::std::any_of(bParams->types.begin(), bParams->types.end(), [](const auto& ty) {
                        return visitTyWith(ty, [](const auto* type) {
                            return type->is_Generic() && type->as_Generic().group() == GENERICHrtb;
                        });
                    }) || ::std::any_of(bParams->values.begin(), bParams->values.end(), [](const auto& value) {
                        return value.is_Generic() && value.as_Generic().group() == GENERICHrtb;
                    });
                    if (!hasHrtb) {
                        ord &= this->comparePp(sp, *bParams, params);
                        if (ord == HIRCompare::Unequal) {
                            return false;
                        }
                        if (ord == HIRCompare::Fuzzy) {
                        }
                        return callback.visit(ImplRef(boundTy, &boundTrait.params, &boundInfo.assoc, boundInfo.constness), ord);
                    }

                    HrtbBoundMatcher matcher(sp, crate.types);
                    ord &= bParams->matchTestGenericsFuzz(sp, params, this->ivars.callbackResolveInfer(), matcher);
                    if (ord == HIRCompare::Unequal) {
                        return false;
                    }
                    if (ord == HIRCompare::Fuzzy) {
                    }
                    // Hand off to the closure, and return true if it does
                    // TODO: The type bounds are only the types that are specified.
                    HIRTraitPath::assocListT assoc;
                    for (const auto& entry : boundInfo.assoc) {
                        assoc.insert({entry.first, matcher.monomorphTpAtyEqual(sp, entry.second, true)});
                    }
                    if (callback.visit(ImplRef(boundTy, params.clone(), mv$(assoc), boundInfo.constness), ord)) {
                        return true;
                    }

                    return false;
                });
                if (rv) {
                    return rv;
                }
            }

            // Match nested declaration predicates on demand. Putting every
            // declared predicate in the cached ParamEnv changes ordinary
            // generic inference and specialization, while the declaration
            // tree already gives each nested predicate its exact subject.
            auto visitDeclaredTrait = [&](auto&& visit, const HIRTypeData* subject, const HIRTraitPath& declaredTrait, bool matchCurrent) -> bool {
                if (matchCurrent && declaredTrait.path.path == trait) {
                    auto ord = subject->compareWithPlaceholders(sp, type, this->ivars.callbackResolveInfer());
                    if (ord != HIRCompare::Unequal) {
                        ord &= this->comparePp(sp, declaredTrait.path.params, params);
                        if (ord != HIRCompare::Unequal) {
                            auto response = declaredTrait.clone();
                            if (callback.visit(ImplRef(subject, mv$(response.path.params), mv$(response.typeBounds), response.constness), ord)) {
                                return true;
                            }
                        }
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
                ASSERT_BUG(sp, environmentInfo.traitPtr, "Cached trait bound has no trait definition");

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
                    if (!impliedProjection || !visitTyWith(impliedProjection->type, [](const HIRTypeData* inner) {
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
                    if (definition.generics.isGeneric() || !definition.generics.isEmpty()) {
                        continue;
                    }
                    if (::std::none_of(definition.traitBounds.begin(), definition.traitBounds.end(), [&](const auto& declaredTrait) {
                        return declaredTraitMayMatch(declaredTraitMayMatch, declaredTrait, false);
                    })) {
                        continue;
                    }
                    auto associatedType = crate.types.path(HIRPath(environmentType, environmentTrait.clone(), associated.first, {}), HIRTypePathBinding::make_Opaque({}));
                    monomorph.ppMethod = &associatedType->as_Path().path.data.as_UfcsKnown().params;
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
                bool rv = this->iterateBoundsTraits(sp, assocInfo->type, assocInfo->trait.path, [&](HIRCompare cmp, const HIRTypeData* boundTy, const HIRGenericPath& boundTrait, const CachedBound& boundInfo) -> bool {
                    // Check the trait params
                    cmp &= this->comparePp(sp, boundTrait.params, assocInfo->trait.params);
                    if (cmp == HIRCompare::Fuzzy) {
                        //TODO(sp, "Handle fuzzy matches searching for associated type bounds");
                    } else if (cmp == HIRCompare::Unequal) {
                        return false;
                    }
                    auto outerOrd = cmp;

                    const auto& traitRef = *boundInfo.traitPtr;
                    const auto& at = traitRef.types.at(assocInfo->item);
                    for (const auto& bound : at.traitBounds) {
                        if (bound.path.path == trait) {
                            auto monomorphCb = MonomorphStatePtr(crate.types, assocInfo->type, &assocInfo->trait.params, &assocInfo->params);

                            HIRCompare ord = outerOrd;
                            if (monomorphisePathparamsNeeded(bound.path.params)) {
                                // TODO: Use a compare+callback method instead
                                auto bParamsMono = monomorphCb.monomorphPathParams(sp, bound.path.params, false);
                                this->expandAssociatedTypesParams(sp, bParamsMono);
                                ord &= this->comparePp(sp, bParamsMono, params);
                            } else {
                                ord &= this->comparePp(sp, bound.path.params, params);
                            }
                            if (ord == HIRCompare::Unequal) {
                                return false;
                            }
                            if (ord == HIRCompare::Fuzzy) {
                            }

                            auto tpMono = monomorphCb.monomorphTraitpath(sp, bound, false);
                            // - Expand associated types
                            this->expandAssociatedTypesParams(sp, tpMono.path.params);
                            for (auto& ty : tpMono.typeBounds) {
                                ty.second.type = this->expandAssociatedTypes(sp, mv$(ty.second.type));
                            }
                            // TODO: Instead of using `type` here, build the real type
                            if (callback.visit(ImplRef(type, mv$(tpMono.path.params), mv$(tpMono.typeBounds), tpMono.constness), ord)) {
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

        HIRCompare TraitResolution::fticCheckParams(
            const Span& sp,
            const HIRSimplePath& trait,
            const HIRPathParams* paramsPtr,
            const HIRTypeData* type,
            const HIRGenericParams& implParamsDef,
            const HIRPathParams& implTraitArgs,
            const HIRTypeData* implTy,
            /*Out->*/ HIRPathParams& outImplParams,
            bool evaluateBounds /*=true*/,
            bool commitDefiningOpaque /*=false*/,
            SolverResponse* effects /*=nullptr*/
        ) const {

            class GetParams: public HIRMatchGenerics {
                Span sp;
                HIRPathParams& outImplParams;

            public:
                GetParams(Span sp, stl::ObjPool& valuePool, HIRPathParams& outImplParams)
                    : HIRMatchGenerics(valuePool)
                    , sp(sp)
                    , outImplParams(outImplParams)
                {
                }

                HIRCompare matchTy(const HIRGenericRef& g, const HIRTypeData* ty, tCbResolveType resolveCb) override {
                    assert(g.binding < outImplParams.types.size());
                    if (outImplParams.types[g.binding] == HIRTypeRef()) {
                        outImplParams.types[g.binding] = ty;
                        return HIRCompare::Equal;
                    } else {
                        auto rv = outImplParams.types[g.binding]->compareWithPlaceholders(sp, ty, resolveCb);
                        // If the existing is an ivar, replace with this.
                        // - TODO: Store the least fuzzy option, or store all fuzzy options?
                        if (rv == HIRCompare::Fuzzy && outImplParams.types[g.binding]->is_Infer()) {
                            // The same impl parameter can be learned through more than one
                            // component of an impl header.  `Y = X` followed by `Y = &X`
                            // is not a fuzzy refinement: it would require the infinite type
                            // `X = &X`.  Treat that header as disjoint instead of replacing
                            // the first constraint and letting specialization pick it.
                            const auto& existingResolved = resolveCb.getType(sp, outImplParams.types[g.binding]);
                            const auto* existingInfer = existingResolved->opt_Infer();
                            if (existingInfer && existingInfer->index != ~0u) {
                                const bool recursive = visitTyWith(ty, [&](const HIRTypeData* inner) {
                                    if (const auto* infer = inner->opt_Infer()) {
                                        if (infer->index == ~0u) {
                                            return false;
                                        }
                                        const auto& resolved = resolveCb.getType(sp, inner);
                                        const auto* resolvedInfer = resolved->opt_Infer();
                                        return resolvedInfer && resolvedInfer->index == existingInfer->index;
                                    }
                                    return false;
                                });
                                if (recursive) {
                                    return HIRCompare::Unequal;
                                }
                            }
                            outImplParams.types[g.binding] = ty;
                        }
                        return rv;
                    }
                }

                HIRCompare matchVal(const HIRGenericRef& g, const HIRConstGeneric& sz) override {
                    ASSERT_BUG(sp, g.binding < outImplParams.values.size(), "Value generic " << g << " out of range (" << outImplParams.values.size() << ")");
                    if (sz.is_Infer()) {
                        ASSERT_BUG(sp, sz.as_Infer().index != ~0u, "");
                    }
                    if (outImplParams.values[g.binding] == HIRConstGeneric()) {
                        outImplParams.values[g.binding] = sz.clone();
                        return HIRCompare::Equal;
                    } else {
                        if (outImplParams.values[g.binding] == sz) {
                            return HIRCompare::Equal;
                        }
                        if (outImplParams.values[g.binding].is_Infer()) {
                            if (!sz.is_Infer()) {
                                outImplParams.values[g.binding] = sz.clone();
                            }
                            return HIRCompare::Fuzzy;
                        }
                        if (sz.is_Infer()) {
                            return HIRCompare::Fuzzy;
                        }
                        // An unevaluated expression can still contain inference
                        // variables supplied by the caller.  A concrete occurrence
                        // of the same impl parameter is the stronger constraint;
                        // retain it while marking the candidate as fuzzy until the
                        // expression itself is normalized.
                        if (outImplParams.values[g.binding].is_Unevaluated()) {
                            if (sz.is_Evaluated()) {
                                outImplParams.values[g.binding] = sz.clone();
                            }
                            return HIRCompare::Fuzzy;
                        }
                        if (sz.is_Unevaluated()) {
                            return HIRCompare::Fuzzy;
                        }
                        return HIRCompare::Unequal;
                    }
                }
            };

            GetParams getParams{sp, *crate.pool, outImplParams};

            outImplParams.types.resize(implParamsDef.types.size());
            outImplParams.values.resize(implParamsDef.values.size());

            // NOTE: If this type references an associated type, the match will incorrectly fail.
            // - HACK: match_test_generics_fuzz has been changed to return Fuzzy if there's a tag mismatch and the LHS is an Opaque path
            auto match = HIRCompare::Equal;
            match &= implTy->matchTestGenericsFuzz(sp, type, this->ivars.callbackResolveInfer(), getParams);
            if (paramsPtr) {
                const auto& params = *paramsPtr;
                const HIRPathParams* traitArgs = &implTraitArgs;
                HIRPathParams monomorphTraitArgs;
                bool implParamsKnown = match != HIRCompare::Unequal;
                for (const auto& ty : outImplParams.types) {
                    implParamsKnown &= ty != HIRTypeRef();
                }
                for (const auto& value : outImplParams.values) {
                    implParamsKnown &= value != HIRConstGeneric();
                }
                if (implParamsKnown && monomorphisePathparamsNeeded(implTraitArgs)) {
                    MonomorphStatePtr monomorph(crate.types, nullptr, &outImplParams, nullptr);
                    monomorph.setConstevalState(this->board(), HIRItemPath(""));
                    monomorphTraitArgs = monomorph.monomorphPathParams(sp, implTraitArgs, true);
                    traitArgs = &monomorphTraitArgs;
                }
                if (traitArgs == &implTraitArgs) {
                    match &= traitArgs->matchTestGenericsFuzz(sp, params, this->ivars.callbackResolveInfer(), getParams);
                } else {
                    match &= traitArgs->compareWithPlaceholders(sp, params, this->ivars.callbackResolveInfer());
                }
                if (match == HIRCompare::Unequal) {
                    return HIRCompare::Unequal;
                }
            } else {
                if (match == HIRCompare::Unequal) {
                    return HIRCompare::Unequal;
                }
            }


            // Some impl blocks have type params used as part of type bounds.
            // - A rough idea is to have monomorph return a third class of generic for params that are not yet bound.
            //  - compare_with_placeholders gets called on both ivars and generics, so that can be used to replace it once known.
            HIRPathParams placeholders;
            RcString placeholderName;
            bool placeholdersNeeded = false;
            {
                for (const auto& ty : outImplParams.types) {
                    if (ty == HIRTypeRef()) {
                        placeholdersNeeded = true;
                    }
                }
                for (const auto& val : outImplParams.values) {
                    if (val == HIRConstGeneric()) {
                        placeholdersNeeded = true;
                    }
                }
            }
            if (placeholdersNeeded) {
                // Candidate substitutions for the same immutable impl header
                // are alpha-equivalent.  A stable identity lets repeated
                // constraint passes converge instead of manufacturing a new
                // type and associated rule on every lookup.
                placeholderName = RcString::newInterned(FMT("impl_?_" << &implParamsDef));
                for (unsigned int i = 0; i < outImplParams.types.size(); i++) {
                    if (outImplParams.types[i] == HIRTypeRef()) {
                        if (placeholders.types.size() == 0) {
                            placeholders.types.resize(outImplParams.types.size());
                        }
                        placeholders.types[i] = crate.types.generic(placeholderName, 2 * 256 + i);
                    }
                }
                for (unsigned int i = 0; i < outImplParams.values.size(); i++) {
                    if (outImplParams.values[i] == HIRConstGeneric()) {
                        if (placeholders.values.size() == 0) {
                            placeholders.values.resize(outImplParams.values.size());
                        }
                        placeholders.values[i] = HIRGenericRef(placeholderName, 2 * 256 + i);
                    }
                }
            } else {
            }

            if (!evaluateBounds) {
                for (size_t i = 0; i < outImplParams.types.size(); i++) {
                    if (outImplParams.types[i] == HIRTypeRef()) {
                        outImplParams.types[i] = ::std::move(placeholders.types[i]);
                    }
                }
                for (size_t i = 0; i < outImplParams.values.size(); i++) {
                    if (outImplParams.values[i] == HIRConstGeneric()) {
                        outImplParams.values[i] = ::std::move(placeholders.values[i]);
                    }
                }
                return match;
            }
            auto cbInfer = this->ivars.callbackResolveInfer();

            struct Matcher: public HIRMatchGenerics, public Monomorphiser {
                Span sp;
                const HIRPathParams& implParams;
                RcString placeholderName;
                HIRPathParams& placeholders;

                Matcher(HIRTypeInterner& types, Span sp, const HIRPathParams& implParams, RcString placeholderName, HIRPathParams& placeholders)
                    : HIRMatchGenerics(types.objectPool())
                    , Monomorphiser(types)
                    , sp(sp)
                    , implParams(implParams)
                    , placeholderName(placeholderName)
                    , placeholders(placeholders)
                {
                }

                HIRCompare matchTy(const HIRGenericRef& g, const HIRTypeData* ty, tCbResolveType resolveCb) override {
                    if (const auto* e = ty->opt_Generic()) {
                        if (e->binding == g.binding && e->name == g.name) {
                            return HIRCompare::Equal;
                        }
                    }
                    if (g.isPlaceholder() && g.name == placeholderName) {
                        auto i = g.idx();
                        ASSERT_BUG(sp, implParams.types[i] == HIRTypeRef(), "Placeholder to populated type returned - " << implParams.types[i] << " vs " << ty);
                        auto& ph = placeholders.types[i];
                        // TODO: Only want to do this if ... what?
                        // - Problem: This can poison the output if the result was fuzzy
                        // - E.g. `Q: Borrow<V>` can equate Q and V
                        if (ph->is_Generic() && ph->as_Generic().binding == g.binding) {
                            ph = ty;
                            return HIRCompare::Equal;
                        } else {
                            return ph->compareWithPlaceholders(sp, ty, resolveCb);
                        }
                    } else {
                        if (g.isPlaceholder()) {
                            return HIRCompare::Fuzzy;
                        }
                        // If the RHS is a non-literal ivar, return fuzzy
                        if (ty->is_Infer() && !ty->as_Infer().isLit()) {
                            return HIRCompare::Fuzzy;
                        }
                        // If the RHS is an unbound UfcsKnown, also fuzzy
                        if (ty->is_Path() && ty->as_Path().binding.is_Unbound()) {
                            return HIRCompare::Fuzzy;
                        }
                        if (ty->is_Generic() && ty->as_Generic().isPlaceholder()) {
                            return HIRCompare::Fuzzy;
                        }
                        return HIRCompare::Unequal;
                    }
                }

                HIRCompare matchVal(const HIRGenericRef& g, const HIRConstGeneric& v) override {
                    if (const auto* e = v.opt_Generic()) {
                        if (e->binding == g.binding && e->name == g.name) {
                            return HIRCompare::Equal;
                        }
                    }
                    if (g.isPlaceholder() && g.name == placeholderName) {
                        auto i = g.idx();
                        ASSERT_BUG(sp, implParams.values[i] == HIRConstGeneric(), "Placeholder to populated value returned - " << implParams.values[i] << " vs " << v);
                        auto& ph = placeholders.values[i];
                        if (ph.is_Generic() && ph.as_Generic().binding == g.binding) {
                            ph = v.clone();
                            return HIRCompare::Equal;
                        } else {
                            TODO(Span(), "[ftic_check_params:cb_match] Compare placeholder " << i << " " << ph << " == " << v);
                        }
                    } else {
                        if (g.isPlaceholder()) {
                            return HIRCompare::Fuzzy;
                        }
                        // If the RHS is a non-literal ivar, return fuzzy
                        if (v.is_Infer()) {
                            return HIRCompare::Fuzzy;
                        }
                        return HIRCompare::Unequal;
                    }
                }

                HIRTypeRef getType(const Span& sp, const HIRGenericRef& ge) const override {
                    //    // TODO: `impl_type` or `des_type`
                    //    //TODO(sp, "[find_impl__check_crate_raw] Self - " << impl_type << " or " << des_type);
                    //}
                    ASSERT_BUG(sp, !ge.isPlaceholder(), "[find_impl__check_crate_raw] Placeholder param seen - " << ge);
                    if (implParams.types.at(ge.binding) != HIRTypeRef()) {
                        return implParams.types.at(ge.binding);
                    }
                    ASSERT_BUG(sp, placeholders.types.size() == implParams.types.size(), "Placeholder size mismatch: " << placeholders.types.size() << " != " << implParams.types.size());
                    return placeholders.types.at(ge.binding);
                }

                HIRConstGeneric getValue(const Span& sp, const HIRGenericRef& val) const override {
                    ASSERT_BUG(sp, val.binding < 256, "Generic value binding in " << val << " out of range (>=256)");
                    ASSERT_BUG(sp, val.binding < implParams.values.size(), "Generic value binding in " << val << " out of range (>= " << implParams.values.size() << ")");
                    if (implParams.values.at(val.binding) != HIRConstGeneric()) {
                        return implParams.values.at(val.binding).clone();
                    }
                    ASSERT_BUG(sp, placeholders.values.size() == implParams.values.size(), "Placeholder size mismatch: " << placeholders.values.size() << " != " << implParams.values.size());
                    return placeholders.values.at(val.binding).clone();
                }
            };

            Matcher matcher{crate.types, sp, outImplParams, placeholderName, placeholders};

            //::std::vector<::HIR::ASTType*> saved_ph;

            // Keep looping while placeholders are updated
            using DeferredTypeConstraint = ::std::pair<const HIRTypeData*, const HIRTypeData*>;

            int loops = 0;
            HIRPathParams lastPlaceholders;
            ThinVector<DeferredTypeConstraint> deferredTypeConstraints;
            do {
                ASSERT_BUG(sp, loops < 4, "Excessive iterations while resolving bound placeholders");
                loops += 1;
                lastPlaceholders = placeholders.clone();
                deferredTypeConstraints.clear();
                // Check bounds for this impl
                // - If a bound fails, then this can't be a valid impl
                for (const auto& bound : implParamsDef.bounds) {
            switch (bound.tag()) {
                case HIRGenericBound::TAG_TraitBound: {
                    auto& be = bound.as_TraitBound();
                    auto realType = matcher.monomorphType(sp, be.type, false);
                    auto realTrait = matcher.monomorphTraitpath(sp, be.trait, false);
                    realType = this->expandAssociatedTypes(sp, mv$(realType));
                    for (auto& p : realTrait.path.params.types) {
                        p = this->expandAssociatedTypes(sp, mv$(p));
                    }
                    for (auto& ab : realTrait.typeBounds) {
                        ab.second.type = this->expandAssociatedTypes(sp, mv$(ab.second.type));
                    }
                    const auto& realTraitPath = realTrait.path;
                    bool foundFuzzyMatch = false;
                    // If the type is an unbound UFCS path, assume fuzzy
                    if (((*realType).is_Path() && ((*realType).as_Path().binding.is_Unbound()))) {
                        foundFuzzyMatch = true;
                    }
                    // If the type is an ivar, but not a literal, assume fuzzy
                    if (((*realType).is_Infer() && ((*realType).as_Infer().isLit() == false))) {
                        foundFuzzyMatch = true;
                    }
                    // NOTE: Save the placeholder state and restore if the result was Fuzzy
                    HIRPathParams savedPh = placeholders.clone();
                    HIRPathParams fuzzyPh;
                    ThinVector<DeferredTypeConstraint> fuzzyTypeConstraints;
                    unsigned numFuzzy = 0;       //!< Number of detected fuzzy impls
                    bool fuzzyCompatible = true; //!< Indicates that the `fuzzy_ph` applies to all detected fuzzy impls
                    bool rv = false;
                    this->solveTraitGoal(sp, realTraitPath.path, realTraitPath.params, realType, [&](SolverResponse response) {
                        if (!response.hasImpl || !response.impl) {
                            return false;
                        }
                        auto impl = response.impl->legacy();
                        ThinVector<DeferredTypeConstraint> candidateTypeConstraints;
                        // TODO: Save and restore placeholders if this isn't a full match
                        auto cmp = response.certainty == SolverCertainty::Proven ? HIRCompare::Equal : HIRCompare::Fuzzy;
                        if (cmp == HIRCompare::Fuzzy) {
                            // If the match was fuzzy, try again filling in with `cb_match`
                            auto iTy = impl.getImplType(crate.types);
                            this->expandAssociatedTypesInplace(sp, iTy);
                            auto iTp = impl.getTraitParams(crate.types);
                            for (auto& t : iTp.types) {
                                this->expandAssociatedTypesInplace(sp, t);
                            }
                            cmp &= realType->matchTestGenericsFuzz(sp, iTy, cbInfer, matcher);
                            cmp &= realTraitPath.params.matchTestGenericsFuzz(sp, iTp, cbInfer, matcher);
                        }
                        for (const auto& assocBound : realTrait.typeBounds) {
                            HIRTypeRef tmp;
                            const HIRTypeData* ty;

                            tmp = impl.getType(crate.types, assocBound.first.c_str(), assocBound.second.atyParams);
                            if (tmp == HIRTypeRef()) {
                                // This bound isn't from this particular trait, go the slow way of using expand_associated_types
                                tmp = this->expandAssociatedTypes(sp, crate.types.path(HIRPath(HIRPath::Data::Data_UfcsKnown{realType, realTraitPath.clone(), assocBound.first, {}}), {}));
                                ty = tmp;
                            } else {
                                // Expand after extraction, just to make sure.
                                this->expandAssociatedTypesInplace(sp, tmp);
                                ty = this->ivars.getType(tmp);
                            }
                            // `ty` = Monomorphised actual type (< `be.type` as `be.trait` >::`assoc_bound.first`)
                            // `assoc_bound.second` = Desired type (monomorphised too)
                            auto cmpI = assocBound.second.type->matchTestGenericsFuzz(sp, ty, cbInfer, matcher);
                            const auto containsOpaqueInScope = [&](const HIRTypeData* type, bool defining) {
                                return visitTyWith(type, [&](const HIRTypeData* inner) {
                                    const auto* erased = inner->opt_ErasedType();
                                    const auto* alias = erased ? erased->inner.opt_Alias() : nullptr;
                                    return alias && (this->isOpaqueAliasDefiningScope(*alias->inner) == defining);
                                });
                            };
                            const bool containsDefiningOpaque = containsOpaqueInScope(ty, true)
                                || containsOpaqueInScope(assocBound.second.type, true);
                            // Generic matching keeps an unresolved opaque fuzzy so a
                            // defining scope can select an impl that reveals its hidden
                            // type. Outside that scope the opaque is rigid: a structural
                            // mismatch is enough to discard this conditional impl.
                            if (cmpI == HIRCompare::Fuzzy
                                && !containsDefiningOpaque
                                && (containsOpaqueInScope(ty, false) || containsOpaqueInScope(assocBound.second.type, false))
                                && assocBound.second.type->compareWithPlaceholders(sp, ty, cbInfer) == HIRCompare::Unequal) {
                                cmpI = HIRCompare::Unequal;
                            }
                            switch (cmpI) {
                                case HIRCompare::Equal:
                                    break;
                                case HIRCompare::Unequal:
                                    cmp = HIRCompare::Unequal;
                                    break;
                                case HIRCompare::Fuzzy:
                                    // TODO: When a fuzzy match is encountered on a conditional bound, returning `false` can lead to an false negative (and a compile error)
                                    // BUT, returning `true` could lead to it being selected. (Is this a problem, should a later validation pass check?)
                                    if (commitDefiningOpaque && effects && containsDefiningOpaque) {
                                        candidateTypeConstraints.emplace_back(ty, assocBound.second.type);
                                    }
                                    cmp = HIRCompare::Fuzzy;
                                    break;
                            }
                            if (cmp == HIRCompare::Unequal) {
                                break;
                            }
                        }

                        if (cmp == HIRCompare::Fuzzy) {
                            foundFuzzyMatch |= true;
                            // `fuzzy_ph` is set (num_fuzzy > 0) then check if the PH set is equal, if not then flag not equal
                            if (numFuzzy > 0 && fuzzyPh != placeholders) {
                                fuzzyCompatible = false;
                            }
                            numFuzzy += 1;

                            if (numFuzzy == 1) {
                                fuzzyTypeConstraints = ::std::move(candidateTypeConstraints);
                            } else {
                                fuzzyTypeConstraints.clear();
                            }

                            fuzzyPh = ::std::move(placeholders);
                            // TODO: Should this do some form of reset?
                            placeholders.types.resize(fuzzyPh.types.size());
                            placeholders.values.resize(fuzzyPh.values.size());
                        }
                        if (cmp != HIRCompare::Equal) {
                            // Restore placeholders
                            // - Maybe save the results for later?
                            placeholders = savedPh.clone();
                        }
                        // If the match isn't a concrete equal, return false (to keep searching)
                        rv = cmp == HIRCompare::Equal;
                        return rv;
                    }, {.assocName = ""});
                    if (rv) {
                    } else if (foundFuzzyMatch) {
                        if (numFuzzy == 0) {
                             // `real_type` was infer
                        } else if (numFuzzy == 1) {
                            placeholders = ::std::move(fuzzyPh);
                            for (const auto& constraint : fuzzyTypeConstraints) {
                                deferredTypeConstraints.push_back(constraint);
                            }
                        } else if (fuzzyCompatible) {
                            placeholders = ::std::move(fuzzyPh);
                        } else {
                        }
                        match = HIRCompare::Fuzzy;
                    } else if (((*realType).is_Infer() && ((*realType).as_Infer().tyClass == HIRInferClass::None))) {
                        match = HIRCompare::Fuzzy;
                    } else if (((*realType).is_Generic() && ((*realType).as_Generic().isPlaceholder()))) {
                        match = HIRCompare::Fuzzy;
                    } else {
                        return HIRCompare::Unequal;
                    }

                    //}
                    break;
                }
                case HIRGenericBound::TAG_TypeEquality: {
                    auto& be = bound.as_TypeEquality();
                    TODO(sp, "Check bound " << be.type << " = " << be.otherType);
                    break;
                }
            }
                }
            } while (placeholders != lastPlaceholders);

            for (size_t i = 0; i < outImplParams.types.size(); i++) {
                if (outImplParams.types[i] == HIRTypeRef()) {
                    outImplParams.types[i] = std::move(placeholders.types[i]);
                }
                ASSERT_BUG(sp, outImplParams.types[i] != HIRTypeRef(), "");
            }
            for (size_t i = 0; i < outImplParams.values.size(); i++) {
                if (outImplParams.values[i] == HIRConstGeneric()) {
                    outImplParams.values[i] = std::move(placeholders.values[i]);
                }
                ASSERT_BUG(sp, outImplParams.values[i] != HIRConstGeneric(), "");
            }

            for (size_t i = 0; i < implParamsDef.types.size(); i++) {
                if (implParamsDef.types.at(i).isSized) {
                    if (outImplParams.types[i] != HIRTypeRef()) {
                        auto cmp = typeIsSized(sp, outImplParams.types[i]);
                        if (cmp == HIRCompare::Unequal) {
                            return HIRCompare::Unequal;
                        }
                    } else {
                        // TODO: Set match to fuzzy?
                    }
                }
            }

            if (commitDefiningOpaque && effects) {
                for (const auto& constraint : deferredTypeConstraints) {
                    effects->equalities.push_back(SolverTypeEquality{constraint.first, constraint.second});
                }
            }

            return match;
        }

        namespace {
            bool traitContainsMethodInner(const HIRTrait& traitPtr, const RcString& name, const HIRFunction*& outFcnPtr) {
                auto it = traitPtr.values.find(name);
                if (it != traitPtr.values.end()) {
                    if (it->second.is_Function()) {
                        const auto& v = it->second.as_Function();
                        outFcnPtr = &v;
                        return true;
                    }
                }
                return false;
            }
        }

        const HIRFunction* TraitResolution::traitContainsMethod(const Span& sp, const HIRGenericPath& traitPath, const HIRTrait& traitPtr, const HIRTypeData* self, const RcString& name, HIRGenericPath& outPath) const {
            const HIRFunction* rv = nullptr;

            if (traitContainsMethodInner(traitPtr, name, rv)) {
                assert(rv);
                outPath = traitPath.clone();
                return rv;
            }

            auto monomorphCb = MonomorphStatePtr(crate.types, self, &traitPath.params, nullptr);
            for (const auto& st : traitPtr.allParentTraits) {
                if (traitContainsMethodInner(*st.traitPtr, name, rv)) {
                    assert(rv);
                    outPath.path = st.path.path;
                    outPath.params = monomorphCb.monomorphPathParams(sp, st.path.params, false);
                    return rv;
                }
            }
            return nullptr;
        }

        bool TraitResolution::traitContainsType(const Span& sp, const HIRGenericPath& traitPath, const HIRTrait& traitPtr, const char* name, HIRGenericPath& outPath) const {

            auto it = traitPtr.types.find(name);
            if (it != traitPtr.types.end()) {
                outPath = traitPath.clone();
                return true;
            }

            auto monomorphCb = MonomorphStatePtr(crate.types, nullptr, &traitPath.params, nullptr);
            for (const auto& st : traitPtr.allParentTraits) {
                if (st.traitPtr->types.count(name)) {
                    outPath.path = st.path.path;
                    outPath.params = monomorphCb.monomorphPathParams(sp, st.path.params, false);
                    return true;
                }
            }
            return false;
        }

        HIRCompare TraitResolution::typeIsSized(const Span& sp, const HIRTypeData* ty) const {
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

        HIRCompare TraitResolution::typeIsSizedBuiltin(const Span& sp, const HIRTypeData* ty) const {
            const auto& type = this->ivars.getType(ty);
    switch ((*type).tag()) {
default:
        // Any unknown - it's sized
        break;
        case HIRTypeData::TAG_Infer: {
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
        case HIRTypeData::TAG_Primitive: {
            auto& e = (*type).as_Primitive();
            if (e == HIRCoreType::Str) {
                return HIRCompare::Unequal;
            }
            break;
        }
        case HIRTypeData::TAG_Slice: {
            return HIRCompare::Unequal;
        }
        case HIRTypeData::TAG_Path: {
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
                    // Is it sized? No.
                    return HIRCompare::Unequal;
                }
                case HIRTypePathBinding::TAG_Enum: {
                    // HAS to be Sized
                    break;
                }
                case HIRTypePathBinding::TAG_Union: {
                    // Pretty sure unions are Sized
                    break;
                }
                case HIRTypePathBinding::TAG_Struct: {
                    auto& pb = e.binding.as_Struct();
                    // Possibly not sized
                    switch (pb->structMarkings.dstType) {
                        case HIRStructMarkings::DstType::None:
                            break;
                        case HIRStructMarkings::DstType::Possible:
                            return typeIsSized(sp, e.path.data.as_Generic().params.types.at(pb->structMarkings.unsizedParam));
                        case HIRStructMarkings::DstType::Projection: {
                            const HIRTypeData* tailTpl = nullptr;
                            switch (pb->data.tag()) {
                                case HIRStructData::TAG_Unit:
                                    BUG(sp, "Potentially-unsized unit struct " << type);
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
        case HIRTypeData::TAG_Generic: {
            auto& e = (*type).as_Generic();
            switch (e.group()) {
                case 0:
                    return this->implGenerics_->types.at(e.idx()).isSized ? HIRCompare::Equal : HIRCompare::Unequal;
                case 1:
                    return this->itemGenerics_->types.at(e.idx()).isSized ? HIRCompare::Equal : HIRCompare::Unequal;
                default:
                    // Assume sized for anything else?
                    return HIRCompare::Equal;
            }
            break;
        }
        case HIRTypeData::TAG_ErasedType: {
            auto& e = (*type).as_ErasedType();
            return e.isSized ? HIRCompare::Equal : HIRCompare::Unequal;
        }
        case HIRTypeData::TAG_TraitObject: {
            return HIRCompare::Unequal;
        }
    }
    return HIRCompare::Equal;
        }

        HIRCompare TraitResolution::typeIsCopy(const Span& sp, const HIRTypeData* ty) const {
            const auto& type = this->ivars.getType(ty);
            // `#![no_core]` crates need MIR's structural copy classification
            // even when they do not define the Copy lang item.  There is no
            // trait goal to solve in that language mode.
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

        HIRCompare TraitResolution::typeIsCopyBuiltin(const Span& sp, const HIRTypeData* ty) const {
            const auto& type = this->ivars.getType(ty);
    switch ((*type).tag()) {
default: {
            return HIRCompare::Unequal;
        }
        break;
        case HIRTypeData::TAG_Infer: {
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
        case HIRTypeData::TAG_Generic: {
            // TODO: Store this result - or even pre-calculate it.
            return this->iterateBoundsTraits(
                       sp,
                       ty,
                       langCopy(),
                       [&](HIRCompare _cmp, const HIRTypeData* beType, const HIRGenericPath& beTrait, const CachedBound& info) -> bool {
                return true;
            }
                   )
                       ? HIRCompare::Equal
                       : HIRCompare::Unequal;
            break;
        }
        case HIRTypeData::TAG_Primitive: {
            auto& e = (*type).as_Primitive();
            if (e == HIRCoreType::Str) {
                return HIRCompare::Unequal;
            }
            return HIRCompare::Equal;
        }
        case HIRTypeData::TAG_Borrow: {
            auto& e = (*type).as_Borrow();
            return e.type == HIRBorrowType::Shared ? HIRCompare::Equal : HIRCompare::Unequal;
        }
        case HIRTypeData::TAG_Pointer: {
            return HIRCompare::Equal;
        }
        case HIRTypeData::TAG_Tuple: {
            auto& e = (*type).as_Tuple();
            auto rv = HIRCompare::Equal;
            for (const auto& sty : e) {
                rv &= typeIsCopy(sp, sty);
            }
            return rv;
        }
        case HIRTypeData::TAG_Slice: {
            return HIRCompare::Unequal;
        }
        case HIRTypeData::TAG_NamedFunction: {
            return HIRCompare::Equal;
        }
        case HIRTypeData::TAG_Function: {
            return HIRCompare::Equal;
        }
        case HIRTypeData::TAG_NodeType: {
            // NOTE: This isn't strictly true, we're leaving the actual checking up to the validate pass
            return HIRCompare::Equal;
        }
        case HIRTypeData::TAG_Array: {
            auto& e = (*type).as_Array();
            return typeIsCopy(sp, e.inner);
        }
    }
    UNREACHABLE();
        }

        HIRCompare TraitResolution::typeIsClone(const Span& sp, const HIRTypeData* ty) const {
            const auto& type = this->ivars.getType(ty);
    switch ((*type).tag()) {
default: {
            if (type->is_Path() && type->as_Path().isClosure()) {
                // If it was a closure, assume true (later code can check)
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
        }
        break;
        case HIRTypeData::TAG_Infer: {
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
        case HIRTypeData::TAG_Generic: {
            // TODO: Store this result - or even pre-calculate it.
            return this->iterateBoundsTraits(
                       sp,
                       ty,
                       langClone(),
                       [&](HIRCompare _cmp, const HIRTypeData* beType, const HIRGenericPath& beTrait, const CachedBound& info) -> bool {
                return true;
            }
                   )
                       ? HIRCompare::Equal
                       : HIRCompare::Unequal;
            break;
        }
        case HIRTypeData::TAG_Primitive: {
            auto& e = (*type).as_Primitive();
            if (e == HIRCoreType::Str) {
                return HIRCompare::Unequal;
            }
            return HIRCompare::Equal;
        }
        case HIRTypeData::TAG_Borrow: {
            auto& e = (*type).as_Borrow();
            return e.type == HIRBorrowType::Shared ? HIRCompare::Equal : HIRCompare::Unequal;
        }
        case HIRTypeData::TAG_Pointer: {
            return HIRCompare::Equal;
        }
        case HIRTypeData::TAG_Tuple: {
            auto& e = (*type).as_Tuple();
            auto rv = HIRCompare::Equal;
            for (const auto& sty : e) {
                rv &= typeIsClone(sp, sty);
            }
            return rv;
        }
        case HIRTypeData::TAG_Slice: {
            return HIRCompare::Unequal;
        }
        case HIRTypeData::TAG_NamedFunction: {
            return HIRCompare::Equal;
        }
        case HIRTypeData::TAG_Function: {
            return HIRCompare::Equal;
        }
        case HIRTypeData::TAG_NodeType: {
            // NOTE: This isn't strictly true, we're leaving the actual checking up to the validate pass
            // TODO: Determine captures earlier and check captures here
            return HIRCompare::Equal;
        }
        case HIRTypeData::TAG_Array: {
            auto& e = (*type).as_Array();
            return typeIsClone(sp, e.inner);
        }
    }
    UNREACHABLE();
        }

        // Checks if a type can unsize to another
        // - Returns Compare::Equal if the unsize is possible and fully known
        // - Returns Compare::Fuzzy if the unsize is possible, but still unknown.
        // - Returns Compare::Unequal if the unsize is impossibe (for any reason)
        // Closure is called `get_new_type` is true, and the unsize is possible
        // usecases:
        // - Checking for an impl as part of impl selection (return True/False/Maybe with required match for Maybe)
        // - Checking for an impl as part of typeck (return True/False/Maybe with unsize possibility OR required equality)
        HIRCompare TraitResolution::canUnsizeCb(const Span& sp, const HIRTypeData* dstTy, const HIRTypeData* srcTy, UnsizeTypeCallback* newTypeCallback, UnsizeInferCallback* inferCallback) const {

            // 1. Test for type equality
            {
                auto cmp = dstTy->compareWithPlaceholders(sp, srcTy, ivars.callbackResolveInfer());
                if (cmp == HIRCompare::Equal) {
                    return HIRCompare::Unequal;
                }
            }

            // 2. If either side is an ivar, fuzzy.
            if (dstTy->is_Infer() || srcTy->is_Infer()) {
                // Inform the caller that these two types could unsize to each other
                // - This allows the coercions code to move the coercion rule up
                if (inferCallback) {
                    inferCallback->visit(dstTy, srcTy);
                }
                return HIRCompare::Fuzzy;
            }

            {
                bool foundBound = this->iterateBoundsTraits(sp, srcTy, langUnsize(), [&](HIRCompare cmp, const HIRTypeData* beType, const HIRGenericPath& beTrait, const CachedBound& info) -> bool {
                    const auto& beDst = beTrait.params.types.at(0);

                    cmp &= dstTy->compareWithPlaceholders(sp, beDst, ivars.callbackResolveInfer());
                    if (cmp == HIRCompare::Unequal) {
                        return false;
                    }

                    if (cmp != HIRCompare::Equal) {
                        TODO(sp, "Found bound " << dstTy << "=" << beDst << " <- " << srcTy << "=" << beType);
                    }
                    return true;
                });
                if (foundBound) {
                    return HIRCompare::Equal;
                }
            }

            // Associated types, check the bounds in the trait.
            if (srcTy->is_Path() && srcTy->as_Path().path.data.is_UfcsKnown()) {
                HIRCompare rv = HIRCompare::Equal;
                const auto& pe = srcTy->as_Path().path.data.as_UfcsKnown();
                auto monomorphCb = MonomorphStatePtr(crate.types, pe.type, &pe.trait.params, nullptr);
                auto foundBound = this->iterateAtyBounds(sp, pe, [&](const HIRTraitPath& bound) {
                    if (bound.path.path != langUnsize()) {
                        return false;
                    }
                    const auto& beDstTpl = bound.path.params.types.at(0);
                    HIRTypeRef tmpTy;
                    const auto& beDst = monomorphCb.maybeMonomorphType(sp, tmpTy, beDstTpl);

                    auto cmp = dstTy->compareWithPlaceholders(sp, beDst, ivars.callbackResolveInfer());
                    if (cmp == HIRCompare::Unequal) {
                        return false;
                    }

                    if (cmp != HIRCompare::Equal) {
                        rv = HIRCompare::Fuzzy;
                    }
                    return true;
                });
                if (foundBound) {
                    return rv;
                }
            }

            // Struct<..., T, ...>: Unsize<Struct<..., U, ...>>
            if (dstTy->is_Path() && srcTy->is_Path()) {
                bool dstIsUnsizable = dstTy->as_Path().binding.is_Struct() && dstTy->as_Path().binding.as_Struct()->structMarkings.canUnsize;
                bool srcIsUnsizable = srcTy->as_Path().binding.is_Struct() && srcTy->as_Path().binding.as_Struct()->structMarkings.canUnsize;
                if (dstIsUnsizable && srcIsUnsizable) {
                    const auto& str = *dstTy->as_Path().binding.as_Struct();
                    const auto& dstGp = dstTy->as_Path().path.data.as_Generic();
                    const auto& srcGp = srcTy->as_Path().path.data.as_Generic();

                    if (dstGp == srcGp) {
                        return HIRCompare::Unequal;
                    } else if (dstGp.path == srcGp.path) {
                        if (str.structMarkings.dstType == HIRStructMarkings::DstType::Possible) {
                            const auto& dstInner = ivars.getType(dstGp.params.types.at(str.structMarkings.unsizedParam));
                            const auto& srcInner = ivars.getType(srcGp.params.types.at(str.structMarkings.unsizedParam));

                            auto cb = [&](auto d) {
                                assert(newTypeCallback);

                                // Re-create structure with s/d
                                auto dstGpNew = dstGp.clone();
                                dstGpNew.params.types.at(str.structMarkings.unsizedParam) = mv$(d);
                                newTypeCallback->visit(crate.types.path(HIRPath(mv$(dstGpNew)), HIRTypePathBinding::make_Struct(&str)));
                            };
                            if (newTypeCallback) {
                                UnsizeTypeCb cbP(cb);
                                return this->canUnsizeCb(sp, dstInner, srcInner, &cbP, inferCallback);
                            } else {
                                return this->canUnsizeCb(sp, dstInner, srcInner, nullptr, inferCallback);
                            }
                        }

                        auto monomorphField = [&](const HIRTypeData* self, const HIRPathParams& params, const HIRTypeData* tpl) {
                            auto fieldTy = MonomorphStatePtr(crate.types, self, &params, nullptr).monomorphType(sp, tpl);
                            return this->expandAssociatedTypes(sp, mv$(fieldTy));
                        };
                        HIRCompare fieldsCmp = HIRCompare::Equal;
                        auto compareField = [&](const HIRTypeData* tpl) {
                            auto dstField = monomorphField(dstTy, dstGp.params, tpl);
                            auto srcField = monomorphField(srcTy, srcGp.params, tpl);
                            fieldsCmp &= dstField->compareWithPlaceholders(sp, srcField, ivars.callbackResolveInfer());
                            return fieldsCmp != HIRCompare::Unequal;
                        };
                        const HIRTypeData* tailTpl = nullptr;
                        switch (str.data.tag()) {
                            case HIRStructData::TAG_Unit:
                                BUG(sp, "Potentially-unsized unit struct " << dstTy);
                            case HIRStructData::TAG_Tuple: {
                                const auto& fields = str.data.as_Tuple();
                                tailTpl = fields.at(str.structMarkings.unsizedField).ent;
                                for (size_t i = 0; i < fields.size(); i++) {
                                    if (i != str.structMarkings.unsizedField && !compareField(fields[i].ent)) {
                                        return HIRCompare::Unequal;
                                    }
                                }
                                break;
                            }
                            case HIRStructData::TAG_Named: {
                                const auto& fields = str.data.as_Named();
                                tailTpl = fields.at(str.structMarkings.unsizedField).ty;
                                for (size_t i = 0; i < fields.size(); i++) {
                                    if (i != str.structMarkings.unsizedField && !compareField(fields[i].ty)) {
                                        return HIRCompare::Unequal;
                                    }
                                }
                                break;
                            }
                        }
                        auto dstTail = monomorphField(dstTy, dstGp.params, tailTpl);
                        auto srcTail = monomorphField(srcTy, srcGp.params, tailTpl);
                        auto rv = this->canUnsizeCb(sp, dstTail, srcTail, nullptr, inferCallback);
                        rv &= fieldsCmp;
                        if (rv == HIRCompare::Fuzzy && newTypeCallback) {
                            newTypeCallback->visit(HIRTypeRef(dstTy));
                        }
                        return rv;
                    } else {
                        return HIRCompare::Unequal;
                    }
                }
            }

            // (Trait) <- Foo
            if (const auto* de = dstTy->opt_TraitObject()) {
                // TODO: Check if src_ty is !Sized
                // - Only allowed if the source is a trait object with the same data trait and lesser bounds


                // (Trait) <- (Trait+Foo)
                if (const auto* se = srcTy->opt_TraitObject()) {
                    auto rv = HIRCompare::Equal;

                    // Project the source principal to the requested
                    // supertrait.  A trait may contain the same supertrait
                    // with different substitutions, so compare the fully
                    // monomorphised parameters instead of only its path.
                    const HIRTraitPath* projected = nullptr;
                    HIRTraitPath projectedStorage;
                    if (de->trait.path.path == se->trait.path.path) {
                        rv &= comparePp(sp, se->trait.path.params, de->trait.path.params);
                        projected = &se->trait;
                    } else if (se->trait.path.path != HIRSimplePath()) {
                        findNamedTraitInTrait(sp, de->trait.path.path, de->trait.path.params, *se->trait.traitPtr, se->trait.path.path, se->trait.path.params, srcTy, [&](const HIRTraitPath& parent) {
                            const auto cmp = comparePp(sp, parent.path.params, de->trait.path.params);
                            if (cmp == HIRCompare::Unequal) {
                                return false;
                            }
                            rv &= cmp;
                            projectedStorage = parent.clone();
                            projected = &projectedStorage;
                            return cmp == HIRCompare::Equal;
                        });
                    }
                    if (!projected || rv == HIRCompare::Unequal) {
                        return HIRCompare::Unequal;
                    }

                    // Every associated-type equality required by the
                    // destination object must also hold on the projected
                    // source supertrait.
                    for (const auto& required : de->trait.typeBounds) {
                        const auto source = projected->typeBounds.find(required.first);
                        if (source == projected->typeBounds.end()) {
                            return HIRCompare::Unequal;
                        }
                        rv &= source->second.type->compareWithPlaceholders(sp, required.second.type, ivars.callbackResolveInfer());
                        if (rv == HIRCompare::Unequal) {
                            return rv;
                        }
                    }

                    // 2. Destination markers must be a strict subset
                    for (const auto& mt : de->markers) {
                        // TODO: Fuzzy match
                        bool found = false;
                        for (const auto& omt : se->markers) {
                            if (omt == mt) {
                                found = true;
                                break;
                            }
                        }
                        if (!found) {
                            // Return early.
                            return HIRCompare::Unequal;
                        }
                    }

                    if (rv == HIRCompare::Fuzzy && newTypeCallback) {
                        // TODO: Inner type
                    }
                    return rv;
                }

                bool good;
                HIRCompare totalCmp = HIRCompare::Equal;

                HIRTypeData::Data_TraitObject tmpE;
                tmpE.trait.path = de->trait.path.path;

                // Check data trait first.
                if (de->trait.path.path == HIRSimplePath()) {
                    ASSERT_BUG(sp, de->markers.size() > 0, "TraitObject with no traits - " << dstTy);
                    good = true;
                } else {
                    good = solveTraitGoal(sp, de->trait.path.path, de->trait.path.params, srcTy, [&](SolverResponse response) {
                        if (!response.hasImpl || !response.impl) {
                            return false;
                        }

                        const auto impl = response.impl->legacy();
                        auto candidateCmp = response.certainty == SolverCertainty::Proven ? HIRCompare::Equal : HIRCompare::Fuzzy;
                        HIRTypeData::Data_TraitObject candidateE;
                        candidateE.trait.path = de->trait.path.path;
                        candidateE.trait.path.params = impl.getTraitParams(crate.types);

                        // Associated types declared by a supertrait carry the
                        // declaring trait path.  Rebuild that path with the
                        // selected principal-trait response instead of mixing
                        // response parameters with the original goal.
                        auto remapSourceTrait = [&](const HIRGenericPath& sourceTrait) {
                            if (sourceTrait.path == de->trait.path.path) {
                                return HIRGenericPath(sourceTrait.path, candidateE.trait.path.params.clone());
                            }

                            HIRGenericPath result = sourceTrait.clone();
                            if (!de->trait.traitPtr) {
                                candidateCmp = HIRCompare::Fuzzy;
                                return result;
                            }

                            auto goalMonomorph = MonomorphStatePtr(crate.types, srcTy, &de->trait.path.params, nullptr);
                            auto responseMonomorph = MonomorphStatePtr(crate.types, srcTy, &candidateE.trait.path.params, nullptr);
                            bool found = false;
                            bool foundEqual = false;
                            for (const auto& parent : de->trait.traitPtr->allParentTraits) {
                                if (parent.path.path != sourceTrait.path) {
                                    continue;
                                }
                                auto goalParent = goalMonomorph.monomorphGenericpath(sp, parent.path, false);
                                const auto parentCmp = comparePp(sp, goalParent.params, sourceTrait.params);
                                if (parentCmp == HIRCompare::Unequal || (foundEqual && parentCmp != HIRCompare::Equal)) {
                                    continue;
                                }

                                auto responseParent = responseMonomorph.monomorphGenericpath(sp, parent.path, false);
                                if (!found || parentCmp == HIRCompare::Equal) {
                                    result = ::std::move(responseParent);
                                    found = true;
                                    foundEqual = parentCmp == HIRCompare::Equal;
                                } else if (result != responseParent) {
                                    // Multiple fuzzy supertrait projections
                                    // are a legitimate ambiguous response.
                                    candidateCmp = HIRCompare::Fuzzy;
                                }
                            }
                            if (!found) {
                                candidateCmp = HIRCompare::Fuzzy;
                            } else if (!foundEqual) {
                                candidateCmp = HIRCompare::Fuzzy;
                            }
                            return result;
                        };

                        for (const auto& aty : de->trait.typeBounds) {
                            auto atyv = impl.getType(crate.types, aty.first.c_str(), aty.second.atyParams);
                            if (atyv == HIRTypeRef()) {
                                // Get the trait from which this associated type comes.
                                // Insert a UfcsKnown path for that
                                auto p = HIRPath(srcTy, aty.second.sourceTrait.clone(), aty.first, aty.second.atyParams.clone());
                                // Run EAT
                                atyv = this->expandAssociatedTypes(sp, crate.types.path(mv$(p), {}));
                            }

                            auto desired = this->expandAssociatedTypes(sp, aty.second.type);
                            const auto atyCmp = compareTy(sp, atyv, desired);
                            if (atyCmp == HIRCompare::Unequal) {
                                return false;
                            }
                            candidateCmp &= atyCmp;
                            candidateE.trait.typeBounds[aty.first] = HIRTraitPath::AtyEqual{remapSourceTrait(aty.second.sourceTrait), aty.second.atyParams.clone(), mv$(atyv)};
                        }

                        totalCmp &= candidateCmp;
                        tmpE = ::std::move(candidateE);
                        return true;
                    }, {.assocName = ""});
                }

                // Then markers
                auto cb = [&](SolverResponse response) {
                    if (!response.hasImpl || !response.impl) {
                        return false;
                    }
                    const auto impl = response.impl->legacy();
                    const auto cmp = response.certainty == SolverCertainty::Proven ? HIRCompare::Equal : HIRCompare::Fuzzy;
                    totalCmp &= cmp;
                    tmpE.markers.back().params = impl.getTraitParams(crate.types);
                    return true;
                };
                for (const auto& marker : de->markers) {
                    if (!good) {
                        break;
                    }
                    tmpE.markers.push_back(marker.path);
                    good &= solveTraitGoal(sp, marker.path, marker.params, srcTy, cb, {.assocName = ""});
                }

                if (good && totalCmp == HIRCompare::Fuzzy && newTypeCallback) {
                    newTypeCallback->visit(crate.types.intern(HIRTypeData::make_TraitObject(mv$(tmpE))));
                }
                return totalCmp;
            }

            // [T] <- [T; n]
            if (const auto* de = dstTy->opt_Slice()) {
                if (const auto* se = srcTy->opt_Array()) {
                    auto cmp = de->inner->compareWithPlaceholders(sp, se->inner, ivars.callbackResolveInfer());
                    // TODO: Indicate to caller that for this to be true, these two must be the same.
                    // - I.E. if true, equate these types
                    if (cmp == HIRCompare::Fuzzy && newTypeCallback) {
                        newTypeCallback->visit(crate.types.slice(se->inner));
                    }
                    return cmp;
                }
            }

            return HIRCompare::Unequal;
        }

        SolverCertainty TraitResolution::evaluateCoercionGoal(const Span& sp, const SolverCoercionConstraint& constraint, const HIRTypeData* input, ThinVector<SolverTypeEquality>* equalities) const {
            const auto certainty = [](HIRCompare comparison) {
                switch (comparison) {
                    case HIRCompare::Equal:
                        return SolverCertainty::Proven;
                    case HIRCompare::Fuzzy:
                        return SolverCertainty::Ambiguous;
                    case HIRCompare::Unequal:
                        return SolverCertainty::NoSolution;
                }
                UNREACHABLE();
            };
            const auto compare = [&](const HIRTypeData* left, const HIRTypeData* right) {
                return certainty(left->compareWithPlaceholders(sp, right, ivars.callbackResolveInfer()));
            };
            const auto resolveKnown = [&](const HIRTypeData* type) {
                return ivars.getType(type);
            };
            const auto relateEquality = [&](const HIRTypeData* left, const HIRTypeData* right) {
                const auto snapshot = ivars.snapshot();
                Unifier unifier(sp, ivars, this);
                const auto outcome = unifier.unify(left, right);
                const bool boundInference = ivars.mutationGeneration != snapshot.generation;
                ivars.rollbackTo(snapshot);
                if (outcome == Unifier::Outcome::Mismatch) {
                    return SolverCertainty::NoSolution;
                }

                // A structural relation may solve caller inference, but an
                // unresolved alias/opaque relation may not select an impl.
                // The Unifier gives us leaf equalities, so only direct
                // canonical input bindings are answer effects; every other
                // deferred leaf remains genuine ambiguity.
                for (const auto& pending : unifier.pending()) {
                    const auto* leftInfer = pending.left->opt_Infer();
                    const auto* rightInfer = pending.right->opt_Infer();
                    if (!((leftInfer && isSolverCanonicalInfer(leftInfer->index))
                        || (rightInfer && isSolverCanonicalInfer(rightInfer->index)))) {
                        return SolverCertainty::Ambiguous;
                    }
                }

                if (equalities) {
                    if (boundInference && unifier.pending().length() == 0) {
                        equalities->push_back(SolverTypeEquality{left, right});
                    } else {
                        for (const auto& pending : unifier.pending()) {
                            equalities->push_back(SolverTypeEquality{pending.left, pending.right});
                        }
                    }
                }
                return SolverCertainty::Proven;
            };

            const auto* destination = constraint.direction == SolverCoercionConstraint::Direction::InputIsDestination ? input : constraint.other;
            const auto* source = constraint.direction == SolverCoercionConstraint::Direction::InputIsDestination ? constraint.other : input;
            destination = resolveKnown(destination);
            source = resolveKnown(source);

            const auto unsize = [&](const HIRTypeData* rawDestination, const HIRTypeData* rawSource) {
                const auto* destination = resolveKnown(rawDestination);
                const auto* source = resolveKnown(rawSource);
                if (ivars.typesEqual(destination, source)) {
                    return SolverCertainty::Proven;
                }
                if (destination->is_Infer() || source->is_Infer()
                    || (destination->is_Path() && destination->as_Path().binding.is_Unbound())
                    || (source->is_Path() && source->as_Path().binding.is_Unbound())) {
                    return SolverCertainty::Ambiguous;
                }
                if (const auto* destinationSlice = destination->opt_Slice()) {
                    if (const auto* sourceArray = source->opt_Array()) {
                        return relateEquality(destinationSlice->inner, sourceArray->inner);
                    }
                }
                const auto result = canUnsizeCb(sp, destination, source, nullptr);
                if (result != HIRCompare::Unequal) {
                    return certainty(result);
                }
                return compare(destination, source);
            };

            if (constraint.op == SolverCoercionOp::Unsizing) {
                if (constraint.direction == SolverCoercionConstraint::Direction::InputIsDestination) {
                    HIRTypeRef storage;
                    const auto* dereferenced = source;
                    while ((dereferenced = autoderef(sp, dereferenced, storage))) {
                        const auto result = unsize(destination, dereferenced);
                        if (result == SolverCertainty::Proven) {
                            return result;
                        }
                    }
                }
                return unsize(destination, source);
            }

            if (ivars.typesEqual(destination, source)) {
                return SolverCertainty::Proven;
            }
            if ((destination->is_Infer() && destination->as_Infer().isLit())
                || destination->is_Diverge()
                || (source->is_Infer() && source->as_Infer().isLit())) {
                return compare(destination, source);
            }
            if (destination->is_Infer() || source->is_Infer()
                || (destination->is_Path() && destination->as_Path().binding.is_Unbound())
                || (source->is_Path() && source->as_Path().binding.is_Unbound())) {
                return SolverCertainty::Ambiguous;
            }
            if (source->is_Diverge()) {
                return SolverCertainty::Proven;
            }

            const auto typeIsBounded = [](const HIRTypeData* type) {
                return type->is_Generic()
                    || (type->is_Path() && (monomorphiseTypeNeeded(type) || type->as_Path().binding.is_Opaque()));
            };
            const auto langCoerceUnsized = crate.getLangItemPathOpt("coerce_unsized");
            if (!langCoerceUnsized.components().empty() && (typeIsBounded(source) || typeIsBounded(destination))) {
                SolverCertainty result = SolverCertainty::NoSolution;
                solveTraitGoal(sp, langCoerceUnsized, HIRPathParams(destination), source, [&](SolverResponse response) {
                    if (response.hasImpl) {
                        result = response.certainty;
                    }
                    return response.hasImpl;
                }, {.allowInferInputs = true});
                if (result != SolverCertainty::NoSolution) {
                    return result;
                }
            }

            const auto relateValues = [](const ThinVector<HIRConstGeneric>& left, const ThinVector<HIRConstGeneric>& right) {
                if (left.size() != right.size()) {
                    return SolverCertainty::NoSolution;
                }
                auto result = SolverCertainty::Proven;
                for (size_t i = 0; i < left.size(); i++) {
                    if (left[i] == right[i]) {
                        continue;
                    }
                    if (left[i].is_Infer() || right[i].is_Infer()
                        || (left[i].is_Generic() && left[i].as_Generic().isPlaceholder())
                        || (right[i].is_Generic() && right[i].as_Generic().isPlaceholder())) {
                        result = SolverCertainty::Ambiguous;
                    } else {
                        return SolverCertainty::NoSolution;
                    }
                }
                return result;
            };

            if (const auto* sourcePath = source->opt_Path()) {
                const auto* destinationPath = destination->opt_Path();
                if (destinationPath && sourcePath->binding.is_Struct() && destinationPath->binding.is_Struct()) {
                    const auto* sourceStruct = sourcePath->binding.as_Struct();
                    if (sourceStruct != destinationPath->binding.as_Struct()) {
                        return compare(destination, source);
                    }
                    const auto& sourceParams = sourcePath->path.data.as_Generic().params;
                    const auto& destinationParams = destinationPath->path.data.as_Generic().params;
                    const auto& markings = sourceStruct->structMarkings;
                    if (markings.coerceUnsized != HIRStructMarkings::Coerce::None) {
                        ASSERT_BUG(sp, markings.coerceParam < sourceParams.types.size() && sourceParams.types.size() == destinationParams.types.size(), "Malformed CoerceUnsized struct markings");
                        auto result = markings.coerceUnsized == HIRStructMarkings::Coerce::Passthrough
                            ? evaluateCoercionGoal(sp, SolverCoercionConstraint{markings.coerceParam, sourceParams.types[markings.coerceParam], SolverCoercionConstraint::Direction::InputIsDestination, SolverCoercionOp::Coercion}, destinationParams.types[markings.coerceParam])
                            : unsize(destinationParams.types[markings.coerceParam], sourceParams.types[markings.coerceParam]);
                        for (size_t i = 0; result != SolverCertainty::NoSolution && i < sourceParams.types.size(); i++) {
                            if (i == markings.coerceParam) {
                                continue;
                            }
                            const auto fieldResult = compare(destinationParams.types[i], sourceParams.types[i]);
                            if (fieldResult == SolverCertainty::NoSolution) {
                                result = fieldResult;
                            } else if (fieldResult == SolverCertainty::Ambiguous) {
                                result = fieldResult;
                            }
                        }
                        const auto valueResult = relateValues(destinationParams.values, sourceParams.values);
                        if (valueResult == SolverCertainty::NoSolution
                            || (valueResult == SolverCertainty::Ambiguous && result == SolverCertainty::Proven)) {
                            result = valueResult;
                        }
                        return result;
                    }

                    // A type parameter unused by fields is bivariant.  Relate
                    // the instantiated fields rather than the nominal path
                    // arguments so such a parameter does not reject a valid
                    // coercion endpoint.
                    auto result = SolverCertainty::Proven;
                    const auto relateField = [&](const HIRTypeData* field) {
                        auto sourceField = expandAssociatedTypes(sp, MonomorphStatePtr(crate.types, source, &sourceParams, nullptr).monomorphType(sp, field));
                        auto destinationField = expandAssociatedTypes(sp, MonomorphStatePtr(crate.types, destination, &destinationParams, nullptr).monomorphType(sp, field));
                        const auto fieldResult = compare(destinationField, sourceField);
                        if (fieldResult == SolverCertainty::NoSolution) {
                            result = fieldResult;
                        } else if (fieldResult == SolverCertainty::Ambiguous && result == SolverCertainty::Proven) {
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
                    const auto valueResult = relateValues(destinationParams.values, sourceParams.values);
                    if (valueResult == SolverCertainty::NoSolution
                        || (valueResult == SolverCertainty::Ambiguous && result == SolverCertainty::Proven)) {
                        result = valueResult;
                    }
                    return result;
                }
            }

            if (const auto* sourcePointer = source->opt_Pointer()) {
                const auto* destinationPointer = destination->opt_Pointer();
                if (!destinationPointer || destinationPointer->type > sourcePointer->type) {
                    return compare(destination, source);
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
                    return unsize(destinationBorrow->inner, sourceBorrow->inner);
                }
                return compare(destination, source);
            }
            if (source->is_NodeType() && source->as_NodeType().is_Closure() && destination->is_Function()) {
                return SolverCertainty::Proven;
            }

            return compare(destination, source);
        }

        Ordering TraitResolution::compareCoercionEndpoints(const Span& sp, const SolverCoercionConstraint& constraint, const HIRTypeData* left, const HIRTypeData* right) const {
            if (constraint.direction != SolverCoercionConstraint::Direction::InputIsDestination) {
                return OrdEqual;
            }
            left = ivars.getType(left);
            right = ivars.getType(right);
            const auto compatibleTarget = [&](const HIRTypeData* leftInner, const HIRTypeData* rightInner) {
                return ivars.typesEqual(leftInner, rightInner)
                    || leftInner->compareWithPlaceholders(sp, rightInner, ivars.callbackResolveInfer()) != HIRCompare::Unequal;
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

        const HIRTypeData* TraitResolution::typeIsOwnedBox(const Span& sp, const HIRTypeData* ty) const {
            if (const auto* e = ty->opt_Path()) {
                if (const auto* pe = e->path.data.opt_Generic()) {
                    if (pe->path == langBox()) {
                        return this->ivars.getType(pe->params.types.at(0));
                    }
                }
            }
            return nullptr;
        }

        // -------------------------------------------------------------------------------------------------------------------
        // -------------------------------------------------------------------------------------------------------------------
        TraitResolution::AutoderefResult TraitResolution::autoderefStep(const Span& sp, const HIRTypeData* tyIn, HIRTypeRef& target, ::std::optional<HIRTypeRef>* implType) const {
            if (implType) {
                implType->reset();
            }

            const auto& ty = this->ivars.getType(tyIn);
            if (ty->is_Infer()) {
                return AutoderefResult::NoMatch;
            }
            // A type mentioning an opaque alias this body defines cannot be
            // dereferenced until its hidden type is recorded; the ordinary
            // lookup path cannot consume the solver's identity response, so
            // report the ambiguity directly and let the caller retry.
            if (visitTyWith(ty, [&](const HIRTypeData* inner) {
                const auto* erased = inner->opt_ErasedType();
                const auto* alias = erased ? erased->inner.opt_Alias() : nullptr;
                return alias && this->isOpaqueAliasDefiningScope(*alias->inner);
            })) {
                return AutoderefResult::Ambiguous;
            }
            if (const auto* e = ty->opt_Borrow()) {
                target = this->ivars.getType(e->inner);
                return AutoderefResult::Match;
            }
            // Array-to-slice is the final unsize step in an autoderef search.
            // create_autoderef materialises it as borrow -> pointer unsize -> deref.
            else if (const auto* e = ty->opt_Array()) {
                target = crate.types.slice(e->inner);
                return AutoderefResult::Match;
            }
            // Shortcut, don't look up a Deref impl for primitives or slices
            else if (ty->is_Slice() || ty->is_Primitive() || ty->is_Tuple() || ty->is_Array()) {
                return AutoderefResult::NoMatch;
            } else {
                ::std::optional<HIRTypeRef> candidateTarget;
                ::std::optional<HIRTypeRef> candidateImplType;
                SolverCertainty certainty = SolverCertainty::NoSolution;
                bool ambiguous = false;

                this->solveTraitGoal(sp, langDeref_, HIRPathParams{}, ty, [&](SolverResponse response) {
                    const auto inspect = [&](const SolverImpl& solverImpl, SolverCertainty candidateCertainty) {
                        auto impl = solverImpl.legacy();
                        auto foundTarget = impl.getType(crate.types, "Target", {});
                        if (foundTarget == HIRTypeRef()) {
                            foundTarget = crate.types.path(HIRPath(ty, langDeref_, RcString::newInterned("Target")), HIRTypePathBinding::make_Opaque({}));
                        } else {
                            this->expandAssociatedTypesInplace(sp, foundTarget);
                        }
                        candidateTarget = foundTarget;
                        candidateImplType = impl.getImplType(crate.types);
                        certainty = candidateCertainty;
                    };

                    if (response.hasImpl && response.impl && !response.impl->ambiguousIdentity) {
                        inspect(*response.impl, response.certainty);
                        return true;
                    }

                    ambiguous = response.hasImpl;
                    return false;
                }, {.assocName = ""});

                if (ambiguous) {
                    return AutoderefResult::Ambiguous;
                }

                if (!candidateTarget) {
                    return AutoderefResult::NoMatch;
                }
                if (certainty == SolverCertainty::NoSolution) {
                    return AutoderefResult::NoMatch;
                }

                target = *candidateTarget;
                if (implType) {
                    *implType = *candidateImplType;
                }
                return AutoderefResult::Match;
            }
        }

        const HIRTypeData* TraitResolution::autoderef(const Span& sp, const HIRTypeData* ty, HIRTypeRef& tmpType) const {
            return autoderefStep(sp, ty, tmpType) == AutoderefResult::Match ? tmpType : nullptr;
        }

        unsigned int TraitResolution::autoderefFindMethod(
            const Span& sp,
            const tTraitList& traits,
            const ::std::vector<unsigned>& ivars,
            unsigned int typeIvarCount,
            const HIRTypeData* topTy,
            const RcString& methodName,
            const HIRTypeData* expectedResult,
            bool mustDecide,
            /* Out -> */ ::std::vector<::std::pair<AutoderefBorrow, HIRPath>>& possibilities
        ) const {
            {
                unsigned int derefCount = 0;
                HIRTypeRef tmpType; // Temporary type used for handling Deref
                const auto& topTyR = this->ivars.getType(topTy);
                const auto* currentTy = topTyR;

                // Correct algorithm:
                // - Find any available method with a receiver type of `T`
                // - If no, try &T
                // - If no, try &mut T
                // - If no, try &move T
                // - If no, dereference T and try again
                auto curAccess = MethodAccess::Move; // Assume that the input value is movable
                auto collapseToMostSpecificSubtrait = [&]() {
                    if (!crate.featureEnabled("supertrait_item_shadowing") || possibilities.size() < 2) {
                        return;
                    }

                    ::std::vector<HIRSimplePath> candidateTraits;
                    candidateTraits.reserve(possibilities.size());
                    for (const auto& possibility : possibilities) {
                        const auto* path = possibility.second.data.opt_UfcsKnown();
                        if (!path) {
                            // RFC 3624 only collapses extension-trait picks.
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
                do {
                    const auto* ty = this->ivars.getType(currentTy);
                    auto shouldPause = [](const auto& ty) -> bool {
                        if (typeIsUnboundedInfer(ty)) {
                            return true;
                        }
                        if (ty->is_Path() && ty->as_Path().binding.is_Unbound()) {
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

                    // A candidate that is only a candidate because the receiver
                    // is not known yet cannot be picked: wait for the type.
                    bool undecided = false;

                    // Non-referenced
                    if (this->findMethod(sp, traits, ivars, typeIvarCount, ty, methodName, expectedResult, curAccess, AutoderefBorrow::None, possibilities, &undecided)) {
                    }

                    // `*mut T` coerces to `*const T`, so a method written for
                    // the shared pointer applies to the mutable one. Only look
                    // when the mutable pointer has none of its own, so that a
                    // method on it still wins.
                    if (possibilities.empty()) {
                        if (const auto* ptr = ty->opt_Pointer()) {
                            if (ptr->type != HIRBorrowType::Shared) {
                                auto constTy = crate.types.pointer(HIRBorrowType::Shared, ptr->inner);
                                if (this->findMethod(sp, traits, ivars, typeIvarCount, constTy, methodName, expectedResult, curAccess, AutoderefBorrow::RawShared, possibilities)) {
                                }
                            }
                        }
                    }

                    // Pin ergonomics permits a method receiver written as
                    // `Pin<&T>` to reborrow a `Pin<&mut T>`. Probe that
                    // receiver before ordinary autoref candidates.
                    if (possibilities.empty() && crate.featureEnabled("pin_ergonomics")) {
                        const auto* pathTy = ty->opt_Path();
                        const auto& langPin = crate.getLangItemPathOpt("pin");
                        if (pathTy && pathTy->path.data.is_Generic()
                            && !langPin.components().empty()) {
                            const auto& pinPath = pathTy->path.data.as_Generic();
                            if (pinPath.path == langPin && pinPath.params.types.size() == 1) {
                                const auto* pinInner = this->ivars.getType(pinPath.params.types.front());
                                if (const auto* borrow = pinInner->opt_Borrow();
                                    borrow && borrow->type == HIRBorrowType::Unique) {
                                    auto shared = crate.types.borrow(HIRBorrowType::Shared, borrow->inner);
                                    auto sharedPin = crate.types.path(
                                        HIRGenericPath(langPin, HIRPathParams(shared)),
                                        pathTy->binding.clone()
                                    );
                                    if (this->findMethod(
                                            sp, traits, ivars, typeIvarCount, sharedPin, methodName,
                                            expectedResult,
                                            MethodAccess::Move, AutoderefBorrow::PinShared,
                                            possibilities, &undecided
                                        )) {
                                    }
                                }
                            }
                        }
                    }

                    // Auto-ref
                    auto borrowTy = crate.types.borrow(HIRBorrowType::Shared, ty);
                    if (this->findMethod(sp, traits, ivars, typeIvarCount, borrowTy, methodName, expectedResult, MethodAccess::Move, AutoderefBorrow::Shared, possibilities, &undecided)) {
                    }
                    borrowTy = crate.types.borrow(HIRBorrowType::Unique, ty);
                    if (curAccess >= MethodAccess::Unique && this->findMethod(sp, traits, ivars, typeIvarCount, borrowTy, methodName, expectedResult, MethodAccess::Move, AutoderefBorrow::Unique, possibilities, &undecided)) {
                    }
                    borrowTy = crate.types.borrow(HIRBorrowType::Owned, ty);
                    if (curAccess >= MethodAccess::Move && this->findMethod(sp, traits, ivars, typeIvarCount, borrowTy, methodName, expectedResult, MethodAccess::Move, AutoderefBorrow::Owned, possibilities, &undecided)) {
                    }
                    if (!possibilities.empty()) {
                        collapseToMostSpecificSubtrait();
                        // A candidate that only matches while the receiver is
                        // unknown is not yet an answer, even when it is alone:
                        // the eventual type may expose an inherent method at a
                        // later autoderef level. Wait until inference advances;
                        // the fallback pass must answer from what remains.
                        if (undecided && !mustDecide) {
                            possibilities.clear();
                            return ~0u;
                        }
                        return derefCount;
                    }

                    // Auto-dereference
                    derefCount += 1;
                    if (const auto* typ = this->typeIsOwnedBox(sp, ty)) {
                        // `cur_access` can stay as-is (Box can be moved out of)
                        currentTy = typ;
                    } else {
                        // TODO: Update `cur_access` based on the avaliable Deref impls
                        switch (this->autoderefStep(sp, ty, tmpType)) {
                            case AutoderefResult::NoMatch:
                                currentTy = nullptr;
                                break;
                            case AutoderefResult::Match:
                                currentTy = tmpType;
                                break;
                            case AutoderefResult::Ambiguous:
                                return ~0u;
                        }
                    }
                } while (currentTy);

                if (this->typeContainsIvars(topTy)) {
                    return ~0u;
                }

                // No method found, return an empty list and return 0
                assert(possibilities.empty());
                return 0;
            }
        }

        ::std::ostream& operator<<(::std::ostream& os, const TraitResolution::AutoderefBorrow& x) {
            switch (x) {
                case TraitResolution::AutoderefBorrow::None:
                    os << "None";
                    break;
                case TraitResolution::AutoderefBorrow::Shared:
                    os << "Shared";
                    break;
                case TraitResolution::AutoderefBorrow::Unique:
                    os << "Unique";
                    break;
                case TraitResolution::AutoderefBorrow::RawShared:
                    os << "RawShared";
                    break;
                case TraitResolution::AutoderefBorrow::PinShared:
                    os << "PinShared";
                    break;
                case TraitResolution::AutoderefBorrow::Owned:
                    os << "Owned";
                    break;
            }
            return os;
        }

        ::std::ostream& operator<<(::std::ostream& os, const TraitResolution::AllowedReceivers& x) {
            switch (x) {
                case TraitResolution::AllowedReceivers::All:
                    os << "All";
                    break;
                case TraitResolution::AllowedReceivers::AnyBorrow:
                    os << "AnyBorrow";
                    break;
                case TraitResolution::AllowedReceivers::SharedBorrow:
                    os << "SharedBorrow";
                    break;
                case TraitResolution::AllowedReceivers::Value:
                    os << "Value";
                    break;
                case TraitResolution::AllowedReceivers::Box:
                    os << "Box";
                    break;
            }
            return os;
        }

        ::std::ostream& operator<<(::std::ostream& os, const TraitResolution::MethodAccess& x) {
            switch (x) {
                case TraitResolution::MethodAccess::Shared:
                    os << "Shared";
                    break;
                case TraitResolution::MethodAccess::Unique:
                    os << "Unique";
                    break;
                case TraitResolution::MethodAccess::Move:
                    os << "Move";
                    break;
            }
            return os;
        }

        // Checks that a given real receiver type matches a desired receiver type (with the correct access)
        // Returns the matched `Self` type, or nothing if there's a mismatch.
        ::std::optional<HIRTypeRef> TraitResolution::checkMethodReceiver(const Span& sp, const HIRFunction& fcn, const HIRTypeData* ty, TraitResolution::MethodAccess access) const {
            switch (fcn.receiver) {
                case HIRFunction::Receiver::Free:
                    // Free functions are never usable
                    return ::std::nullopt;
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
                            [](const HIRTypeData* v) {
                        return v->is_Generic() && v->as_Generic().isSelf();
                    }
                        ),
                        receiverType
                    );
                    // TODO: Handle custom-receiver functions
                    // - match_test_generics, if it succeeds return the matched Self
                    {
                        struct GetSelf: public HIRMatchGenerics {
                            ::std::optional<HIRTypeRef> detectedSelfTy;

                            GetSelf()
                                : HIRMatchGenerics(BorrowMatchedValues{})
                            {
                            }

                            HIRCompare matchTy(const HIRGenericRef& g, const HIRTypeData* ty, tCbResolveType _resolve_cb) override {
                                if (g.isSelf()) {
                                    detectedSelfTy = ty;
                                }
                                return HIRCompare::Equal;
                            }

                            HIRCompare matchVal(const HIRGenericRef& g, const HIRConstGeneric& sz) override {
                                TODO(Span(), "GetSelf::match_val " << g << " with " << sz);
                            }
                        } getself;

                        if (receiverType->matchTestGenerics(sp, ty, this->ivars.callbackResolveInfer(), getself)) {
                            ASSERT_BUG(sp, getself.detectedSelfTy, "Unable to determine receiver type when matching " << receiverType << " and " << ty);
                            return this->ivars.getType(*getself.detectedSelfTy);
                        }
                    }
                    return ::std::nullopt;
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
            return ::std::nullopt;
        }

        bool TraitResolution::findMethod(const Span& sp, const tTraitList& traits, const ::std::vector<unsigned>& ivars, unsigned int typeIvarCount, const HIRTypeData* ty, const RcString& methodName, const HIRTypeData* expectedResult, MethodAccess access, AutoderefBorrow borrowType, /* Out -> */ ::std::vector<::std::pair<AutoderefBorrow, HIRPath>>& possibilities, /* Out -> */ bool* outUndecided) const {
            bool rv = false;
            auto cbInfer = this->ivars.callbackResolveInfer();

            auto getIvaredParams = [&](const HIRGenericParams& tpl) -> HIRPathParams {
                unsigned int nParams = tpl.types.size();
                ASSERT_BUG(sp, typeIvarCount <= ivars.size(), "Invalid method ivar split: " << typeIvarCount << " type ivars in a pool of " << ivars.size());
                ASSERT_BUG(sp, nParams <= typeIvarCount, "Not enough type ivars allocated for method: " << nParams << " needed but " << typeIvarCount << " allocated by caller\ntpl = " << tpl.fmtArgs());
                HIRPathParams traitParams;
                traitParams.types.reserve(nParams);
                for (unsigned int i = 0; i < nParams; i++) {
                    traitParams.types.push_back(crate.types.infer(ivars[i], HIRInferClass::None));
                    ASSERT_BUG(sp, this->ivars.getType(traitParams.types.back())->as_Infer().index == ivars[i], "A method selection ivar was bound");
                }
                const unsigned int nValues = tpl.values.size();
                ASSERT_BUG(sp, nValues <= ivars.size() - typeIvarCount, "Not enough value ivars allocated for method: " << nValues << " needed but " << ivars.size() - typeIvarCount << " allocated by caller\ntpl = " << tpl.fmtArgs());
                traitParams.values.reserve(nValues);
                for (unsigned int i = 0; i < nValues; i++) {
                    traitParams.values.push_back(HIRConstGeneric::make_Infer({ivars[typeIvarCount + i]}));
                }
                return traitParams;
            };

            // 1. Search for inherent methods
            // - Inherent methods are searched first.
            // TODO: Have a cache of name+receiver_type to a list of types and impls
            // e.g. `len` `&Self` = `[T]`
            const auto* inherentReceiver = ty;
            while (const auto* borrow = inherentReceiver->opt_Borrow()) {
                inherentReceiver = this->ivars.getType(borrow->inner);
            }
            const auto* erased = inherentReceiver->opt_ErasedType();
            const auto* alias = erased ? erased->inner.opt_Alias() : nullptr;
            const bool opaqueCanReveal = !erased
                || (alias && this->isOpaqueAliasDefiningScope(*alias->inner))
                || erased->inner.is_Known();
            if (opaqueCanReveal) {
                this->wb.inherentMethods->find(sp, methodName, ty, this->ivars.callbackResolveInfer(), [&](const HIRTypeData* selfTy, const HIRTypeImpl& impl) {
                    const auto& method = impl.methods.at(methodName);
                    if (!method.publicity.isVisible(this->visPath)) {
                        // Ignore method: Not visibile
                        return;
                    }
                    HIRPathParams implParams;
                    auto cmp = fticCheckParams(sp, HIRSimplePath(), nullptr, selfTy, impl.params, {}, impl.type, implParams);
                    if (cmp != HIRCompare::Unequal) {
                        // A wrapper receiver such as `Box<_>` can reach a
                        // concrete inherent-method bucket through its open
                        // inner type.  That bucket is only a possibility until
                        // inference identifies the inner type; selecting it now
                        // would leave an unresolvable `<_>::method` path.
                        if (outUndecided && typeIsUnboundedInfer(this->ivars.getType(selfTy))) {
                            *outUndecided = true;
                        }
                        {
                            HIRPathParams methodParams;
                            RcString placeholderName;
                            if (method.data.params.isGeneric()) {
                                placeholderName = RcString::newInterned(FMT("method_wf_" << &method.data));
                            }
                            methodParams.types.reserve(method.data.params.types.size());
                            for (size_t i = 0; i < method.data.params.types.size(); i++) {
                                methodParams.types.push_back(crate.types.generic(placeholderName, GENERICPlaceholder * 256 + i));
                            }
                            methodParams.values.reserve(method.data.params.values.size());
                            for (size_t i = 0; i < method.data.params.values.size(); i++) {
                                methodParams.values.push_back(HIRGenericRef(placeholderName, GENERICPlaceholder, i));
                            }

                            const auto monomorph = MonomorphStatePtr(crate.types, selfTy, &implParams, &methodParams);
                            const auto returnType = monomorph.monomorphType(sp, method.data.returnType, true);
                            auto wf = HIRCompare::Equal;
                            visitTyWith(returnType, [&](const HIRTypeData* inner) {
                                const auto* path = inner->opt_Path();
                                const auto* projection = path ? path->path.data.opt_UfcsKnown() : nullptr;
                                if (!projection) {
                                    return false;
                                }
                                if (!nextSolver) {
                                    ASSERT_BUG(sp, crate.pool, "next-solver requires the crate object pool");
                                    nextSolver = crate.pool->make<NextTraitGoalEvaluator>(*this, crate);
                                }
                                auto projectionWf = nextSolver->evaluateCertainty(sp, projection->trait.path, projection->trait.params, projection->type);
                                if (projectionWf == HIRCompare::Unequal && placeholderName != RcString()) {
                                    const bool dependsOnMethodParam = visitTyWith(inner, [&](const HIRTypeData* part) {
                                        const auto* generic = part->opt_Generic();
                                        return generic && generic->isPlaceholder() && generic->name == placeholderName;
                                    });
                                    if (dependsOnMethodParam) {
                                        projectionWf = HIRCompare::Fuzzy;
                                    }
                                }
                                wf &= projectionWf;
                                return wf == HIRCompare::Unequal;
                            });
                            if (wf == HIRCompare::Unequal) {
                                return;
                            }
                        }
                        possibilities.push_back(::std::make_pair(borrowType, HIRPath(selfTy, methodName, {})));
                        rv = true;
                    }
                });
            }

            // TODO: Handle custom recievers by finding the bottom of a deref chain (or take the top-level reciever as an argument here?)

            // 3. Search generic bounds for a match
            // - If there is a bound on the receiver, then that bound is usable no-matter what
            bool foundBound = false;
            bool foundNonGlobalBound = false;
            auto typeIsNonGlobalAfterNormalization = [&](const HIRTypeData* type) {
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
            auto recordBoundGlobalness = [&](const HIRTypeData* type, const HIRGenericPath& trait, const CachedBound& info) {
                foundNonGlobalBound |= typeIsNonGlobalAfterNormalization(type) || paramsAreNonGlobalAfterNormalization(trait.params);
                for (const auto& associated : info.assoc) {
                    foundNonGlobalBound |= paramsAreNonGlobalAfterNormalization(associated.second.sourceTrait.params)
                        || paramsAreNonGlobalAfterNormalization(associated.second.atyParams)
                        || typeIsNonGlobalAfterNormalization(associated.second.type);
                }
            };
            for (const auto& tb : traitBounds) {
                const auto& eType = tb.first.first;
                const auto& eTraitGp = tb.first.second;
                const auto& eTraitInfo = tb.second;

                assert(eTraitInfo.traitPtr);
                // 1. Find the named method in the trait.
                HIRGenericPath finalTraitPath;
                const HIRFunction* fcnPtr;
                if (!(fcnPtr = this->traitContainsMethod(sp, eTraitGp, *eTraitInfo.traitPtr, eType, methodName, finalTraitPath))) {
                    continue;
                }

                // 2. Compare the receiver of the above to this type and the bound.
                if (auto selfTy = checkMethodReceiver(sp, *fcnPtr, ty, access)) {
                    // HRLs - could be some in the path from `trait_contains_method`
                    // - Lazy option, just erase whatever we find
                    struct MonomorphEraseHrls: public Monomorphiser {
                        using Monomorphiser::Monomorphiser;

                        HIRTypeRef getType(const Span& sp, const HIRGenericRef& ty) const override {
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

                    // If the type is an unbounded ivar, don't check.
                    if (((**selfTy).is_Infer() && ((**selfTy).as_Infer().isLit() == false))) {
                        return false;
                    }
                    // TODO: Do a fuzzy match here?
                    auto cmp = (*selfTy)->compareWithPlaceholders(sp, eType, cbInfer);
                    if (cmp == HIRCompare::Equal) {
                        // TODO: Re-monomorphise final trait using `ty`?
                        // - Could collide with legitimate uses of `Self`

                        // Found the method, return the UFCS path for it
                        possibilities.push_back(::std::make_pair(borrowType, HIRPath(HIRPath::Data::make_UfcsKnown({*selfTy, mv$(finalTraitPath), methodName, {}}))));
                        rv = true;
                        foundBound = true;
                        recordBoundGlobalness(eType, eTraitGp, eTraitInfo);
                    } else if (cmp == HIRCompare::Fuzzy) {

                        // Found the method, return the UFCS path for it
                        possibilities.push_back(::std::make_pair(borrowType, HIRPath(HIRPath::Data::make_UfcsKnown({*selfTy, mv$(finalTraitPath), methodName, {}}))));
                        rv = true;
                        foundBound = true;
                        recordBoundGlobalness(eType, eTraitGp, eTraitInfo);
                    } else {
                    }
                } else {
                }
            }
            // The next solver only lets a non-global where-bound shadow crate
            // impls. A global bound remains a method candidate, but probing must
            // continue so the call signature can disambiguate it from an impl.
            if (foundBound && foundNonGlobalBound) {
                return rv;
            }

            // 2. Search the current trait (if in an impl block)
            if (currentTraitPath_) {
                HIRGenericPath finalTraitPath;
                const HIRFunction* fcnPtr;
                if ((fcnPtr = this->traitContainsMethod(sp, *currentTraitPath_, *currentTraitPtr, ty, methodName, finalTraitPath))) {
                    if (auto selfTy = checkMethodReceiver(sp, *fcnPtr, ty, access)) {
                        // If the type is an unbounded ivar, don't check.
                        if (((**selfTy).is_Infer() && ((**selfTy).as_Infer().isLit() == false))) {
                            return false;
                        }

                        // Use the set of ivars we were given to populate the trait parameters
                        const auto& trait = crate.getTraitByPath(sp, finalTraitPath.path);
                        auto traitParams = getIvaredParams(trait.params);

                        {
                            // The current trait wins only when it is known to
                            // apply to this receiver.  An ambiguous candidate
                            // must not shadow a declared associated-type bound
                            // that is examined below (for example
                            // `X::Sampler: UniformSampler` while implementing
                            // another trait with a method of the same name).
                            // Trait arguments remain inference variables shared
                            // with the eventual call signature.
                            const bool crateImplFound = solveTraitGoal(sp, finalTraitPath.path, traitParams, *selfTy, [](SolverResponse response) {
                                return response.hasImpl && response.certainty == SolverCertainty::Proven;
                            }, {.assocName = ""});
                            if (crateImplFound) {
                                possibilities.push_back(::std::make_pair(borrowType, HIRPath(*selfTy, HIRGenericPath(finalTraitPath.path, mv$(traitParams)), methodName, {})));
                                return true;
                            } else {
                            }
                        }
                    }
                }
            }

            auto getInnerType = [this, sp](const HIRTypeData* ty, auto cb) -> const HIRTypeData* {
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

            // 4. If the type is a trait object, search for methods on that trait object
            // - NOTE: This isnt mutually exclusive with the below set (an inherent impl of `(Trait)` is valid)
            if (const auto* ityp = getInnerType(ty, [](const auto& t) {
                return t->is_TraitObject();
            })) {
                const auto& e = ityp->as_TraitObject();
                const auto& trait = this->crate.getTraitByPath(sp, e.trait.path.path);

                bool foundTraitObject = false;
                auto addTraitObjectMethod = [&](const HIRFunction& fcn, HIRGenericPath finalTraitPath) {
                    // - If the receiver is valid, then it's correct (no need to check the type again)
                    if (auto selfTyP = checkMethodReceiver(sp, fcn, ty, access)) {
                        possibilities.push_back(::std::make_pair(borrowType, HIRPath(*selfTyP, mv$(finalTraitPath), methodName, {})));
                        rv = true;
                        foundTraitObject = true;
                    }
                };

                const HIRFunction* fcnPtr = nullptr;
                if (traitContainsMethodInner(trait, methodName, fcnPtr)) {
                    assert(fcnPtr);
                    addTraitObjectMethod(*fcnPtr, e.trait.path.clone());
                } else {
                    const auto selfTy = crate.types.self();
                    auto monomorphCb = MonomorphStatePtr(crate.types, selfTy, &e.trait.path.params, nullptr);
                    for (const auto& st : trait.allParentTraits) {
                        fcnPtr = nullptr;
                        if (!traitContainsMethodInner(*st.traitPtr, methodName, fcnPtr)) {
                            continue;
                        }
                        assert(fcnPtr);
                        auto finalTraitPath = HIRGenericPath(st.path.path, monomorphCb.monomorphPathParams(sp, st.path.params, false));
                        addTraitObjectMethod(*fcnPtr, std::move(finalTraitPath));
                    }
                }

                // If the method was found on the trait object, prefer that over all others.
                if (foundTraitObject) {
                    return rv;
                }
            }

            // 5. Mutually exclusive searches
            // - Erased type - `impl Trait`
            if (const auto* ityp = getInnerType(ty, [](const auto& t) {
                return t->is_ErasedType();
            })) {
                const auto& e = ityp->as_ErasedType();
                for (const auto& traitPath : e.traits) {
                    const auto& trait = this->crate.getTraitByPath(sp, traitPath.path.path);

                    HIRGenericPath finalTraitPath;
                    if (const auto* fcnPtr = this->traitContainsMethod(sp, traitPath.path, trait, crate.types.self(), methodName, finalTraitPath)) {

                        if (auto selfTyP = checkMethodReceiver(sp, *fcnPtr, ty, access)) {
                            possibilities.push_back(::std::make_pair(borrowType, HIRPath(*selfTyP, mv$(finalTraitPath), methodName, {})));
                            rv = true;
                        }
                    }
                }
            }
            // Generics: Nothing except the bounds (Which have already been checked)
            else if (getInnerType(ty, [](const auto& t) {
                return t->is_Generic();
            })) {
            }
            // UfcsKnown paths: Can have trait bounds added by the definer
            else if (const auto* ityp = getInnerType(ty, [](const auto& t) {
                return t->is_Path() && t->as_Path().path.data.is_UfcsKnown();
            })) {
                const auto& e = ityp->as_Path().path.data.as_UfcsKnown();

                // UFCS known - Assuming that it's reached the maximum resolvable level (i.e. a type within is generic), search for trait bounds on the type

                // `Self` = `*.type`
                // `/*I:#*/` := `e.trait.m_params`
                auto monomorphCb = MonomorphStatePtr(crate.types, e.type, &e.trait.params, &e.params);

                const auto& trait = this->crate.getTraitByPath(sp, e.trait.path);
                const auto& assocTy = trait.types.at(e.item);
                // NOTE: The bounds here have 'Self' = the type
                for (const auto& bound : assocTy.traitBounds) {
                    ASSERT_BUG(sp, bound.traitPtr, "Pointer to trait " << bound.path << " not set in " << e.trait.path);
                    HIRGenericPath finalTraitPath;

                    auto tySelf = crate.types.path(HIRPath(crate.types.self(), bound.path.clone(), e.item), HIRTypePathBinding::make_Opaque({}));
                    if (const auto* fcnPtr = this->traitContainsMethod(sp, bound.path, *bound.traitPtr, tySelf, methodName, finalTraitPath)) {

                        if (auto selfTyP = checkMethodReceiver(sp, *fcnPtr, ty, access)) {
                            if (*selfTyP == ityp) {
                                auto ppHrb = HIRPathParams();
                                monomorphCb.ppHrb = &ppHrb;
                                finalTraitPath = monomorphCb.monomorphGenericpath(sp, finalTraitPath, false);

                                // Found the method, return the UFCS path for it
                                possibilities.push_back(::std::make_pair(borrowType, HIRPath(*selfTyP, mv$(finalTraitPath), methodName, {})));
                                rv = true;
                            }
                        }
                    }
                }

                // Search `<Self as Trait>::Name` bounds on the trait itself
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

                    // Found such a bound, now to test if it is useful

                    HIRGenericPath finalTraitPath;
                    if (const auto* fcnPtr = this->traitContainsMethod(sp, be.trait.path, *be.trait.traitPtr, crate.types.self(), methodName, finalTraitPath)) {

                        if (auto selfTyP = checkMethodReceiver(sp, *fcnPtr, ty, access)) {
                            if (*selfTyP == ityp) {
                                if (monomorphisePathparamsNeeded(finalTraitPath.params)) {
                                    finalTraitPath.params = monomorphCb.monomorphPathParams(sp, finalTraitPath.params, false);
                                }

                                // Found the method, return the UFCS path for it
                                possibilities.push_back(::std::make_pair(borrowType, HIRPath(*selfTyP, mv$(finalTraitPath), methodName, {})));
                                rv = true;
                            }
                        }
                    }
                }
            } else {
            }

            // 6. Search for trait methods (using currently in-scope traits)
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

                if (auto selfTyP = checkMethodReceiver(sp, *fcnPtr, ty, access)) {
                    const auto& selfTy = *selfTyP;

                    // Use the set of ivars we were given to populate the trait parameters
                    HIRPathParams traitParams = getIvaredParams(traitRef.second->params);

                    // TODO: Re-monomorphise the trait path!

                    bool undecided = false;
                    bool implFound = false;

                    // A literal's type is decided by fallback whatever the
                    // method turns out to be, but a type with no class at all
                    // is only decided by what happens around it: until then a
                    // fuzzy match says nothing.
                    const bool receiverIsOpen = visitTyWith(this->ivars.getType(selfTy), [&](const HIRTypeData* inner) {
                        const auto* r = this->ivars.getType(inner);
                        const auto* e = r->opt_Infer();
                        return e && e->tyClass == HIRInferClass::None;
                    });

                    // Probe is existential over the trait arguments. An exact
                    // projected return can carry the surrounding expectation
                    // into selection; method-generic returns stay with the
                    // eventual call signature because its method arguments
                    // are not part of this probe.
                    HIRTypeRef methodReturn;
                    const HIRPath::Data::Data_UfcsKnown* returnProjection = nullptr;
                    if (expectedResult && !fcnPtr->params.isGeneric()) {
                        methodReturn = MonomorphStatePtr(crate.types, selfTy, &traitParams, nullptr).monomorphType(sp, fcnPtr->returnType, true);
                        const auto* returnPath = methodReturn->opt_Path();
                        returnProjection = returnPath ? returnPath->path.data.opt_UfcsKnown() : nullptr;
                        if (returnProjection) {
                            // Only an associated output of this trait goal can
                            // guide selection.  A method returning `Self` also
                            // becomes a projection when the receiver itself is
                            // one (for example `F::SignedInt::wrapping_neg`),
                            // but asking the receiver's `Int` goal for
                            // `SignedInt` invents the invalid projection
                            // `<F as Int>::SignedInt`.
                            HIRGenericPath sourceTrait;
                            auto rootTrait = HIRGenericPath(*traitRef.first, traitParams.clone());
                            if (returnProjection->type != selfTy
                                || !traitContainsType(sp, rootTrait, *traitRef.second, returnProjection->item.c_str(), sourceTrait)
                                || sourceTrait.path != returnProjection->trait.path) {
                                returnProjection = nullptr;
                            }
                        }
                    }
                    const TraitGoalQuery methodQuery{
                        .assocName = returnProjection ? returnProjection->item.c_str() : (receiverIsOpen ? "" : nullptr),
                        .assocType = returnProjection ? expectedResult : nullptr,
                        .assocParams = returnProjection ? &returnProjection->params : nullptr,
                        .allowInferInputs = receiverIsOpen,
                    };
                    solveTraitGoal(sp, *traitRef.first, traitParams, selfTy, [&](SolverResponse response) {
                        if (!response.hasImpl) {
                            return false;
                        }
                        implFound = true;
                        if (receiverIsOpen && response.certainty != SolverCertainty::Proven) {
                            undecided = true;
                        }
                        return true;
                    }, methodQuery);
                    if (implFound) {
                        possibilities.push_back(::std::make_pair(borrowType, HIRPath(selfTy, HIRGenericPath(*traitRef.first, mv$(traitParams)), methodName, {})));
                        rv = true;
                    }
                    if (undecided && outUndecided) {
                        *outUndecided = true;
                    }
                } else {
                }
            }

            return rv;
        }

        unsigned int TraitResolution::autoderefFindField(const Span& sp, const HIRTypeData* topTy, const RcString& fieldName, /* Out -> */ HIRTypeRef& fieldType) const {
            unsigned int derefCount = 0;
            HIRTypeRef tmpType; // Temporary type used for handling Deref
            const auto* currentTy = topTy;
            if (const auto* e = this->ivars.getType(topTy)->opt_Borrow()) {
                currentTy = e->inner;
                derefCount += 1;
            }

            do {
                const auto& ty = this->ivars.getType(currentTy);
                if (ty->is_Infer()) {
                    return ~0u;
                }
                if (ty->is_Path() && ty->as_Path().binding.is_Unbound()) {
                    return ~0u;
                }

                if (this->findField(sp, ty, fieldName, fieldType)) {
                    return derefCount;
                }

                // 3. Dereference and try again
                derefCount += 1;
                currentTy = this->autoderef(sp, ty, tmpType);
            } while (currentTy);

            if (/*const auto* e =*/this->ivars.getType(topTy)->opt_Borrow()) {
                const auto& ty = this->ivars.getType(topTy);

                if (findField(sp, ty, fieldName, fieldType)) {
                    return 0;
                }
            }

            // Dereference failed! This is a hard error (hitting _ is checked above and returns ~0)
            TODO(sp, "Error when no field could be found, but type is known - (: " << topTy << ")." << fieldName);
        }

        bool TraitResolution::findField(const Span& sp, const HIRTypeData* ty, const RcString& name, /* Out -> */ HIRTypeRef& fieldTy) const {
            if (const auto* e = ty->opt_Path()) {
        switch (e->binding.tag()) {
            case HIRTypePathBinding::TAG_Unbound: {
                // Wut?
                TODO(sp, "Handle TypePathBinding::Unbound - " << ty);
                break;
            }
            case HIRTypePathBinding::TAG_Opaque: {
                // Ignore, no fields on an opaque
                break;
            }
            case HIRTypePathBinding::TAG_Struct: {
                auto& be = e->binding.as_Struct();
                // Has fields!
                    const auto& str = *be;
                    const auto& params = e->path.data.as_Generic().params;
                    auto monomorph = MonomorphStatePtr(crate.types, ty, &params, nullptr);
                switch (str.data.tag()) {
                    case HIRStructData::TAG_Unit: {
                        // No fields on a unit struct
                        break;
                    }
                    case HIRStructData::TAG_Tuple: {
                        auto& se = str.data.as_Tuple();
                        for (unsigned int i = 0; i < se.size(); i++) {
                            if (se[i].publicity.isVisible(this->visPath) && FMT(i) == name) {
                                fieldTy = monomorph.monomorphType(sp, se[i].ent);
                                return true;
                            }
                        }
                        break;
                    }
                    case HIRStructData::TAG_Named: {
                        auto& se = str.data.as_Named();
                        for (const auto& fld : se) {
                            if (fld.vis.isVisible(this->visPath) && fld.name == name) {
                                fieldTy = monomorph.monomorphType(sp, fld.ty);
                                return true;
                            }
                        }
                        break;
                    }
                }
                break;
            }
            case HIRTypePathBinding::TAG_Enum: {
                // No fields on enums either
                break;
            }
            case HIRTypePathBinding::TAG_ExternType: {
                // No fields on extern types
                break;
            }
            case HIRTypePathBinding::TAG_Union: {
                auto& be = e->binding.as_Union();
                const auto& unm = *be;
                const auto& params = e->path.data.as_Generic().params;
                auto monomorph = MonomorphStatePtr(crate.types, ty, &params, nullptr);

                for (const auto& fld : unm.variants) {
                    if (fld.vis.isVisible(this->visPath) && fld.name == name) {
                        fieldTy = monomorph.monomorphType(sp, fld.ty);
                        return true;
                    }
                }
                break;
            }
        }
            } else if (const auto* e = ty->opt_Tuple()) {
                for (unsigned int i = 0; i < e->size(); i++) {
                    if (FMT(i) == name) {
                        fieldTy = (*e)[i];
                        return true;
                    }
                }
            } else {
            }
            return false;
        }

        HMTypeInferrence::FmtType::FmtType(const HMTypeInferrence& ctxt, const HIRTypeData* ty)
            : ctxt(ctxt)
            , ty(ty)
        {
        }

        HMTypeInferrence::FmtPP::FmtPP(const HMTypeInferrence& ctxt, const HIRPathParams& pps)
            : ctxt(ctxt)
            , pps(pps)
        {
        }

        // Null only when alias != ~0

        HMTypeInferrence::IVar::IVar(HIRTypeRef type)
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
            , aliasIvarPool(stl::ObjPool::fromMemory())
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
            // Generations are allocated by a counter that survives rollback:
            // a generation observed inside a rolled-back probe never recurs,
            // so nothing cached against it can be mistaken for a live state.
            mutationGeneration = ++generationCounter;
            if (!hasChanged) {
                hasChanged = true;
            }
        }

        void HMTypeInferrence::journalMutation(JournalEntry::Kind kind, unsigned slot, HIRTypeRef oldType) {
            if (snapshotDepth != 0) {
                journal.pushBack(JournalEntry{kind, slot, oldType});
            }
        }

        HMTypeInferrence::Snapshot HMTypeInferrence::snapshot() {
            snapshotDepth++;
            return Snapshot{journal.length(), ivars.size(), values.size(), mutationGeneration, hasChanged};
        }

        void HMTypeInferrence::commit(const Snapshot& snapshot) {
            ASSERT_BUG(Span(), snapshotDepth != 0, "commit without an active inference snapshot");
            ASSERT_BUG(Span(), journal.length() >= snapshot.journalLength, "inference snapshots committed out of order");
            snapshotDepth--;
            if (snapshotDepth == 0) {
                // Value aliases keep their Infer payload alive for rollback;
                // once nothing can roll back any more, release it as the
                // non-probing path does.
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
            ASSERT_BUG(Span(), snapshotDepth != 0, "rollback without an active inference snapshot");
            ASSERT_BUG(Span(), journal.length() >= snapshot.journalLength, "inference snapshots rolled back out of order");
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
                        // The Infer payload was kept alive at mutation time.
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
            ASSERT_BUG(Span(), ivars.size() >= snapshot.ivarCount, "inference snapshot saw the ivar table shrink");
            ASSERT_BUG(Span(), values.size() >= snapshot.valueCount, "inference snapshot saw the value table shrink");
            ivars.erase(ivars.begin() + snapshot.ivarCount, ivars.end());
            values.erase(values.begin() + snapshot.valueCount, values.end());
            mutationGeneration = snapshot.generation;
            hasChanged = snapshot.hasChanged;
        }

        HMTypeInferrence::ResolvePlaceholders::ResolvePlaceholders(const HMTypeInferrence& parent)
            : parent(parent)
        {
        }

        /// Expand any located associated types in the input, operating in-place and returning the result
        HIRTypeRef TraitResolution::expandAssociatedTypes(const Span& sp, HIRTypeRef input, SolverResponseCallback* effects) const {
            expandAssociatedTypesInplace(sp, input, effects);
            return input;
        }

        const HIRTypeData* TraitResolution::expandAssociatedTypes(const Span& sp, const HIRTypeData* input, HIRTypeRef& tmp, SolverResponseCallback* effects) const {
            if (this->hasAssociatedType(input)) {
                return (tmp = this->expandAssociatedTypes(sp, input, effects));
            } else {
                return input;
            }
        }

        void TraitResolution::expandAssociatedTypesParams(const Span& sp, HIRPathParams& params, SolverResponseCallback* effects) const {
            for (auto& type : params.types) {
                if (this->hasAssociatedType(type)) {
                    type = this->expandAssociatedTypes(sp, type, effects);
                }
            }
        }

        bool typeIsUnboundedInfer(const HIRTypeData* ty) {
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

        ::std::ostream& operator<<(::std::ostream& os, const HMTypeInferrence::FmtType& x) {
            x.ctxt.printType(os, x.ty);
            return os;
        }

        ::std::ostream& operator<<(::std::ostream& os, const HMTypeInferrence::FmtPP& x) {
            x.ctxt.printPathparams(os, x.pps);
            return os;
        }

        const HIRTypeData* HMTypeInferrence::ResolvePlaceholders::getType(const Span& sp, const HIRTypeData* ty) const {
            if (ty->is_Infer()) {
                return parent.getType(ty);
            } else {
                return ty;
            }
        }

        const HIRConstGeneric& HMTypeInferrence::ResolvePlaceholders::getVal(const Span& sp, const HIRConstGeneric& v) const {
            if (v.is_Infer()) {
                return parent.getValue(v);
            } else {
                return v;
            }
        }
