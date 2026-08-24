#include "mir_from_hir.h"

// Arrays at least this large use the sparse PartialArray move-tracking state.
static const size_t PARTIAL_ARRAY_MIN = 32;

#include "hir_hir.h"
#include "mir_mir.h"
#include "hir_expr.h"
#include "wire_board.h"
#include "hir_visitor.h"
#include "mir_helpers.h"
#include "mir_mir_ptr.h"
#include "trans_target.h" // Target_GetSizeAndAlignOf - for `box`
#include "hir_expr_state.h"
#include "mir_operations.h"
#include "hir_typeck_common.h" // monomorphise_type
#include "mir_main_bindings.h"
#include "mir_visit_crate_mir.h"
#include "hir_conv_main_bindings.h" // For consteval
#include "settings.h"

#include <cctype> // isdigit
#include <limits> // std::numeric_limits
#include <numeric>
#include <algorithm>
#include <type_traits> // for TU_MATCHA

#include <std/mem/obj_pool.h>
#include <std/alg/defer.h>
#include <std/sym/i_map.h>

namespace {
    class ExprVisitorConv: public MirConverter, public MIRDropEmitter {
        MirBuilder& builder;

        const ::std::vector<HIRTypeRef>& variableTypes;

        /// Generators do some different codegen quirks
        bool isGenerator;

        struct LoopDesc {
            ScopeHandle scope;
            RcString label;
            bool requireLabel;
            unsigned int cur;
            unsigned int next;
            MIRLValue resValue;
        };

        ::std::vector<LoopDesc> loopStack;

        const ScopeHandle* blockTmpScope = nullptr;
        const ScopeHandle* blockVarScope = nullptr;
        const ScopeHandle* borrowRaiseTarget = nullptr;
        const ScopeHandle* stmtScope = nullptr;
        const ScopeHandle* superLetScope = nullptr;
        bool inBorrow = false;

        struct GeneratorState {
            static constexpr unsigned UNRESUMED = 0;
            static constexpr unsigned RETURNED = 1;
            static constexpr unsigned POISONED = 2;
            static constexpr unsigned FIRST_SUSPENSION = 3;

            struct State {
                /// Entrypoint for the state
                MIRBasicBlockId entrypoint;
                /// Block that returns the suspended value for this state.
                MIRBasicBlockId suspensionBlock = ~0u;
                /// List of saved variables when this state yields
                std::map<unsigned, MirBuilder::SavedActiveLocal> saved;

                State(MIRBasicBlockId entry)
                    : entrypoint(entry)
                {
                }
            };

            // Basic block to be terminated with the state switch
            MIRBasicBlockId bbOpen;
            /// Yield points/states
            std::vector<State> states;

            // Set of drop flags that are stored in the output state
            // These are stored in a bit-set at the end of the state structure, and remapped after lower (with sets being writes,
            // and then re-read before use)
            std::set<unsigned> savedDropFlags;

            /// Path to the enum used for the state index field (used to generate enum variant construction)
            HIRSimplePath stateIdxEnmPath;

            /// Is this coroutine a future? (as opposed to a generator)
            bool isFuture = false;
            /// Is this coroutine an `async gen` body? It is a future that also
            /// yields: it returns `Poll<Option<Item>>`.
            bool isAsyncGen = false;
        } generatorState;

    public:
        ExprVisitorConv(MirBuilder& builder, const ::std::vector<HIRTypeRef>& varTypes, const HIRExprNodeGeneratorWrapper* isGenerator)
            : builder(builder)
            , variableTypes(varTypes)
            , isGenerator(isGenerator != nullptr)
        {
            if (isGenerator) {
                generatorState.isFuture = isGenerator->isFuture;
                generatorState.isAsyncGen = isGenerator->isAsyncGen;
                generatorState.stateIdxEnmPath = isGenerator->stateIdxEnum;
                generatorState.bbOpen = builder.pauseCurBlock();
                generatorState.states.push_back(GeneratorState::State(builder.newBbUnlinked()));
                builder.setCurBlock(generatorState.states.back().entrypoint);
                if (generatorState.isFuture) {
                    builder.setDropEmitter(this);
                }
            }
        }

        bool findAsyncDrop(const Span& sp, const HIRTypeData* ty, HIRPath& path, HIRTypeRef& futureTy) const {
            return builder.resolve().findAsyncDrop(sp, ty, path, futureTy);
        }

        bool hasDropImpl(const Span& sp, const HIRTypeData* ty) const {
            const auto& trait = builder.resolve().langDrop();
            if (trait.components().empty()) {
                return false;
            }
            return builder.resolve().findImpl(sp, trait, HIRPathParams{}, ty, [](ImplRef impl, bool fuzzed) {
                return !fuzzed && impl.data.is_TraitImpl();
            });
        }

        bool typeNeedsAsyncDrop(const Span& sp, const HIRTypeData* ty) const {
            return builder.resolve().typeNeedsAsyncDrop(sp, ty);
        }

        void emitSyncDestructor(const Span& sp, const HIRTypeData* ty, MIRLValue value) {
            auto& types = builder.crate().types;
            auto refTy = types.borrow(HIRBorrowType::Unique, ty);
            auto refValue = builder.lvalueOrTemp(sp, refTy, MIRRValue::make_Borrow({HIRBorrowType::Unique, false, std::move(value)}));
            auto result = builder.newTemporary(types.unit());
            auto bbRet = builder.newBbUnlinked();
            auto bbPanic = builder.newBbUnlinked();
            auto path = HIRPath(ty, HIRGenericPath(builder.resolve().langDrop()), RcString::newInterned("drop"), HIRPathParams{});
            builder.endBlock(MIRTerminator::make_Call({bbRet, MIRUnwindAction::make_Cleanup(bbPanic), result.clone(), std::move(path), makeVec1(MIRParam(refValue.clone()))}));
            builder.movedLvalue(sp, std::move(refValue));
            builder.setCurBlock(bbPanic);
            emitUnwind(sp);
            builder.setCurBlock(bbRet);
            builder.markValueAssigned(sp, result);
        }

        /// Poll a future or an async iterator, suspending the coroutine while it
        /// is not ready. `outputTy` is what the `Poll` carries: the future's
        /// output, or the iterator's `Option<Item>`.
        MIRLValue awaitFuture(const Span& sp, const HIRTypeData* futureTy, MIRLValue future, const HIRTypeData* outputTy, bool isNext = false) {
            const auto stateValue = static_cast<unsigned>(generatorState.states.size());
            generatorState.states.back().saved = builder.getActiveLocals(sp, generatorState.savedDropFlags);
            generatorState.states.push_back(builder.newBbUnlinked());
            builder.endBlock(generatorState.states.back().entrypoint);
            builder.setCurBlock(generatorState.states.back().entrypoint);

            const auto& langPin = builder.crate().getLangItemPath(sp, "pin");
            auto& types = builder.crate().types;
            auto typeMut = types.borrow(HIRBorrowType::Unique, futureTy);
            auto typePin = types.path(HIRGenericPath(langPin, HIRPathParams(typeMut)), &builder.crate().getStructByPath(sp, langPin));
            auto lvMut = builder.lvalueOrTemp(sp, typeMut, MIRRValue::make_Borrow({HIRBorrowType::Unique, false, std::move(future)}));
            auto lvPin = builder.newTemporary(typePin);
            {
                auto bbRet = builder.newBbUnlinked();
                auto bbPanic = builder.newBbUnlinked();
                builder.endBlock(MIRTerminator::make_Call({bbRet, MIRUnwindAction::make_Cleanup(bbPanic), lvPin.clone(), HIRPath(typePin, "new_unchecked"), makeVec1(MIRParam(lvMut.clone()))}));
                builder.movedLvalue(sp, std::move(lvMut));
                builder.setCurBlock(bbPanic);
                emitUnwind(sp);
                builder.setCurBlock(bbRet);
                builder.markValueAssigned(sp, lvPin);
            }

            const auto& langPoll = builder.crate().getLangItemPath(sp, "Poll");
            auto typePoll = types.path(HIRGenericPath(langPoll, HIRPathParams(outputTy)), &builder.crate().getEnumByPath(sp, langPoll));
            auto lvPoll = builder.newTemporary(typePoll);
            {
                auto bbRet = builder.newBbUnlinked();
                auto bbPanic = builder.newBbUnlinked();
                builder.endBlock(MIRTerminator::make_Call({
                    bbRet,
                    MIRUnwindAction::make_Cleanup(bbPanic),
                    lvPoll.clone(),
                    isNext ? HIRPath(futureTy, builder.resolve().langAsyncIterator(), "poll_next") : HIRPath(futureTy, builder.resolve().langFuture(), "poll"),
                    makeVec2(
                        MIRParam(lvPin.clone()),
                        MIRParam::make_Borrow({HIRBorrowType::Unique, MIRLValue::newDeref(MIRLValue::newArgument(1))})
                    )
                }));
                builder.movedLvalue(sp, std::move(lvPin));
                builder.setCurBlock(bbPanic);
                emitUnwind(sp);
                builder.setCurBlock(bbRet);
                builder.markValueAssigned(sp, lvPoll);
            }

            const auto bbPending = builder.newBbUnlinked();
            const auto bbReady = builder.newBbUnlinked();
            ASSERT_BUG(sp, typePoll->as_Path().binding.as_Enum()->findVariant("Ready") == 0, "");
            ASSERT_BUG(sp, typePoll->as_Path().binding.as_Enum()->findVariant("Pending") == 1, "");
            builder.endBlock(MIRTerminator::make_Switch({lvPoll.clone(), makeVec2(bbReady, bbPending)}));
            builder.setCurBlock(bbPending);

            HIRGenericPath returnPollPath = builder.valType(sp, MIRLValue::newReturn())->as_Path().path.data.as_Generic().clone();
            builder.pushStmtAssign(sp, MIRLValue::newReturn(), MIRRValue::make_EnumVariant({std::move(returnPollPath), 1, {}}));
            builder.pushStmtAssign(sp, generatorStateLv(), MIRRValue::make_EnumVariant({generatorState.stateIdxEnmPath.clone(), stateValue + GeneratorState::POISONED, {}}));
            generatorState.states.at(stateValue - 1).suspensionBlock = bbPending;
            builder.endBlock(MIRTerminator::make_Return({}));
            builder.setCurBlock(bbReady);

            return MIRLValue::newField(MIRLValue::newDowncast(std::move(lvPoll), 0), 0);
        }

        void emitDropFields(const Span& sp, const HIRTypeData* ty, const MIRLValue& value) {
            if (const auto* array = ty->opt_Array()) {
                if (!array->size.is_Known()) {
                    TODO(sp, "async drop of an array with unevaluated length");
                }
                for (size_t i = 0; i < array->size.as_Known(); i++) {
                    auto field = MIRLValue::newField(value.clone(), static_cast<unsigned int>(i));
                    if (!emitAsyncDrop(sp, field.clone(), ~0u) && builder.resolve().typeNeedsDropGlue(sp, array->inner)) {
                        builder.pushStmtDropRaw(sp, std::move(field));
                    }
                }
                return;
            }
            if (const auto* tuple = ty->opt_Tuple()) {
                for (size_t i = 0; i < tuple->size(); i++) {
                    auto field = MIRLValue::newField(value.clone(), static_cast<unsigned int>(i));
                    if (!emitAsyncDrop(sp, field.clone(), ~0u) && builder.resolve().typeNeedsDropGlue(sp, tuple->at(i))) {
                        builder.pushStmtDropRaw(sp, std::move(field));
                    }
                }
                return;
            }
            const auto* pathTy = ty->opt_Path();
            if (!pathTy || !pathTy->path.data.is_Generic()) {
                return;
            }
            const auto& generic = pathTy->path.data.as_Generic();
            if (generic.path == builder.crate().getLangItemPathOpt("manually_drop")) {
                return;
            }
            const auto* str = pathTy->binding.opt_Struct();
            if (!str) {
                return;
            }

            auto monomorph = MonomorphStatePtr(builder.crate().types, ty, &generic.params, nullptr);
            switch (((*str)->data).tag()) {
                case HIRStructData::TAG_Unit: {
                    break;
                }
                case HIRStructData::TAG_Tuple: {
                    auto& fields = ((*str)->data).as_Tuple();
                    for (size_t i = 0; i < fields.size(); i++) {
                        auto fieldTy = monomorph.monomorphType(sp, fields[i].ent);
                        builder.resolve().expandAssociatedTypes(sp, fieldTy);
                        auto field = MIRLValue::newField(value.clone(), static_cast<unsigned int>(i));
                        if (!emitAsyncDrop(sp, field.clone(), ~0u) && builder.resolve().typeNeedsDropGlue(sp, fieldTy)) {
                            builder.pushStmtDropRaw(sp, std::move(field));
                        }
                    }
                    break;
                }
                case HIRStructData::TAG_Named: {
                    auto& fields = ((*str)->data).as_Named();
                    for (size_t i = 0; i < fields.size(); i++) {
                        auto fieldTy = monomorph.monomorphType(sp, fields[i].ty);
                        builder.resolve().expandAssociatedTypes(sp, fieldTy);
                        auto field = MIRLValue::newField(value.clone(), static_cast<unsigned int>(i));
                        if (!emitAsyncDrop(sp, field.clone(), ~0u) && builder.resolve().typeNeedsDropGlue(sp, fieldTy)) {
                            builder.pushStmtDropRaw(sp, std::move(field));
                        }
                    }
                    break;
                }
            }
        }

        void emitCoroutineAsyncDrop(const Span& sp, const HIRTypeData* ty, MIRLValue value) {
            auto& types = builder.crate().types;
            const auto& path = builder.crate().getLangItemPath(sp, "async_drop_in_place");
            auto dropPath = HIRPath(HIRGenericPath(path, HIRPathParams(ty)));
            MonomorphState monomorph(types);
            auto item = builder.resolve().getValue(sp, dropPath, monomorph);
            const auto* function = item.opt_Function();
            ASSERT_BUG(sp, function, "async_drop_in_place did not resolve for " << ty);
            auto futureTy = monomorph.monomorphType(sp, (*function)->returnType);
            builder.resolve().expandAssociatedTypes(sp, futureTy);

            auto pointerTy = types.pointer(HIRBorrowType::Unique, ty);
            auto pointer = builder.lvalueOrTemp(sp, pointerTy, MIRRValue::make_Borrow({HIRBorrowType::Unique, true, std::move(value)}));
            auto future = builder.newTemporary(futureTy);
            auto bbRet = builder.newBbUnlinked();
            auto bbPanic = builder.newBbUnlinked();
            builder.endBlock(MIRTerminator::make_Call({
                bbRet,
                MIRUnwindAction::make_Cleanup(bbPanic),
                future.clone(),
                std::move(dropPath),
                makeVec1(MIRParam(pointer.clone())),
            }));
            builder.movedLvalue(sp, std::move(pointer));
            builder.setCurBlock(bbPanic);
            emitUnwind(sp);
            builder.setCurBlock(bbRet);
            builder.markValueAssigned(sp, future);

            (void)awaitFuture(sp, futureTy, future.clone(), types.unit());
            builder.movedLvalue(sp, future.clone());
            builder.pushStmtDropRaw(sp, std::move(future));
        }

        bool emitAsyncDrop(const Span& sp, MIRLValue value, unsigned int flag) {
            const HIRTypeData* ty = builder.valType(sp, value);
            HIRPath dropPath{HIRSimplePath()};
            HIRTypeRef futureTy;
            const bool hasAsyncDestructor = findAsyncDrop(sp, ty, dropPath, futureTy);
            if (!hasAsyncDestructor && !typeNeedsAsyncDrop(sp, ty)) {
                return false;
            }

            if (flag != ~0u) {
                auto& types = builder.crate().types;
                const auto& langPoll = builder.crate().getLangItemPath(sp, "Poll");
                auto conditionTy = types.path(HIRGenericPath(langPoll, HIRPathParams(types.unit())), &builder.crate().getEnumByPath(sp, langPoll));
                auto condition = builder.newTemporary(conditionTy);
                builder.pushStmtAssign(sp, condition.clone(), MIRRValue::make_EnumVariant({conditionTy->as_Path().path.data.as_Generic().clone(), 1, {}}));
                auto dropBb = builder.newBbUnlinked();
                auto nextBb = builder.newBbUnlinked();
                builder.endBlock(MIRTerminator::make_Switch({std::move(condition), ::std::vector<MIRBasicBlockId>{dropBb, dropBb}, flag, nextBb}));
                builder.setCurBlock(dropBb);
                const bool emitted = emitAsyncDrop(sp, value.clone(), ~0u);
                ASSERT_BUG(sp, emitted, "conditional async drop stopped being async for " << ty);
                builder.endBlock(MIRTerminator::make_Goto(nextBb));
                builder.setCurBlock(nextBb);
                return true;
            }

            if (const auto* pathTy = ty->opt_Path(); pathTy && (pathTy->isFuture() || pathTy->isGenerator())) {
                emitCoroutineAsyncDrop(sp, ty, std::move(value));
                return true;
            }

            const auto* boxedTy = builder.isTypeOwnedBox(ty);
            if (boxedTy) {
                auto pointee = MIRLValue::newDeref(value.clone());
                if (!emitAsyncDrop(sp, pointee.clone(), ~0u) && builder.resolve().typeNeedsDropGlue(sp, boxedTy)) {
                    builder.pushStmtDropRaw(sp, std::move(pointee));
                }
            }

            auto& types = builder.crate().types;
            if (hasAsyncDestructor) {
                const auto& langPin = builder.crate().getLangItemPath(sp, "pin");
                auto refTy = types.borrow(HIRBorrowType::Unique, ty);
                auto pinTy = types.path(HIRGenericPath(langPin, HIRPathParams(refTy)), &builder.crate().getStructByPath(sp, langPin));
                auto refValue = builder.lvalueOrTemp(sp, refTy, MIRRValue::make_Borrow({HIRBorrowType::Unique, false, value.clone()}));
                auto pinValue = builder.newTemporary(pinTy);
                {
                    auto bbRet = builder.newBbUnlinked();
                    auto bbPanic = builder.newBbUnlinked();
                    builder.endBlock(MIRTerminator::make_Call({bbRet, MIRUnwindAction::make_Cleanup(bbPanic), pinValue.clone(), HIRPath(pinTy, "new_unchecked"), makeVec1(MIRParam(refValue.clone()))}));
                    builder.movedLvalue(sp, std::move(refValue));
                    builder.setCurBlock(bbPanic);
                    emitUnwind(sp);
                    builder.setCurBlock(bbRet);
                    builder.markValueAssigned(sp, pinValue);
                }

                auto future = builder.newTemporary(futureTy);
                {
                    auto bbRet = builder.newBbUnlinked();
                    auto bbPanic = builder.newBbUnlinked();
                    builder.endBlock(MIRTerminator::make_Call({bbRet, MIRUnwindAction::make_Cleanup(bbPanic), future.clone(), std::move(dropPath), makeVec1(MIRParam(pinValue.clone()))}));
                    builder.movedLvalue(sp, std::move(pinValue));
                    builder.setCurBlock(bbPanic);
                    emitUnwind(sp);
                    builder.setCurBlock(bbRet);
                    builder.markValueAssigned(sp, future);
                }

                (void)awaitFuture(sp, futureTy, future.clone(), types.unit());
                builder.movedLvalue(sp, future.clone());
                builder.pushStmtDropRaw(sp, std::move(future));
            } else if (hasDropImpl(sp, ty)) {
                emitSyncDestructor(sp, ty, value.clone());
            }

            emitDropFields(sp, ty, value);
            return true;
        }

        bool emitAsyncBoxShallowDrop(const Span& sp, MIRLValue value, unsigned int flag) {
            const HIRTypeData* ty = builder.valType(sp, value);
            if (!builder.isTypeOwnedBox(ty) || !typeNeedsAsyncDrop(sp, ty)) {
                return false;
            }
            if (flag != ~0u) {
                auto& types = builder.crate().types;
                const auto& langPoll = builder.crate().getLangItemPath(sp, "Poll");
                auto conditionTy = types.path(HIRGenericPath(langPoll, HIRPathParams(types.unit())), &builder.crate().getEnumByPath(sp, langPoll));
                auto condition = builder.newTemporary(conditionTy);
                builder.pushStmtAssign(sp, condition.clone(), MIRRValue::make_EnumVariant({conditionTy->as_Path().path.data.as_Generic().clone(), 1, {}}));
                auto dropBb = builder.newBbUnlinked();
                auto nextBb = builder.newBbUnlinked();
                builder.endBlock(MIRTerminator::make_Switch({std::move(condition), ::std::vector<MIRBasicBlockId>{dropBb, dropBb}, flag, nextBb}));
                builder.setCurBlock(dropBb);
                const bool emitted = emitAsyncBoxShallowDrop(sp, value.clone(), ~0u);
                ASSERT_BUG(sp, emitted, "conditional shallow Box drop stopped being async for " << ty);
                builder.endBlock(MIRTerminator::make_Goto(nextBb));
                builder.setCurBlock(nextBb);
                return true;
            }
            if (hasDropImpl(sp, ty)) {
                emitSyncDestructor(sp, ty, value.clone());
            }
            emitDropFields(sp, ty, value);
            return true;
        }

        bool emitDeepDrop(const Span& sp, const MIRLValue& value, unsigned int flag) override {
            return emitAsyncDrop(sp, value.clone(), flag);
        }

        bool emitShallowDrop(const Span& sp, const MIRLValue& value, unsigned int flag) override {
            return emitAsyncBoxShallowDrop(sp, value.clone(), flag);
        }

        SaveAndEditVal<const ScopeHandle*> disableBorrowExtension() override {
            return saveAndEdit(borrowRaiseTarget, nullptr);
        }

        // Get a LValue pointing at the state index
        MIRLValue generatorStateLv() const {
            // (*self.ptr(?0)).state(0).value(?#1).idx(0)
            auto rv = MIRLValue::newArgument(0);
            rv = MIRLValue::newField(mv$(rv), 0);    // .ptr (From Pin)
            rv = MIRLValue::newDeref(mv$(rv));       // .*
            rv = MIRLValue::newField(mv$(rv), 0);    // .state
            rv = MIRLValue::newDowncast(mv$(rv), 1); // .value (From MaybeUninit)
            rv = MIRLValue::newField(mv$(rv), 0);    // .value (From ManuallyDrop)
            rv = MIRLValue::newField(mv$(rv), 0);    // .idx
            return rv;
        }

        const std::set<unsigned>& generatorDropFlags() const {
            return generatorState.savedDropFlags;
        }

        static u64 generatorStorageConflictKey(unsigned left, unsigned right) {
            const u64 first = left < right ? left : right;
            const u64 second = left < right ? right : left;
            return (first << 32) | second;
        }

        void generatorFindCompositeStorageConflicts(
            const MIRFunction& fcn,
            unsigned firstStoredLocal,
            stl::IntMap<bool>& conflicts
        ) const {
            for (const auto& block : fcn.blocks) {
                for (const auto& statement : block.statements) {
                    const auto* assign = statement.opt_Assign();
                    if (!assign || !assign->dst.root.is_Local()) {
                        continue;
                    }
                    switch (assign->src.tag()) {
                        case MIRRValue::TAG_SizedArray:
                        case MIRRValue::TAG_Tuple:
                        case MIRRValue::TAG_Array:
                        case MIRRValue::TAG_EnumVariant:
                        case MIRRValue::TAG_Struct:
                            break;
                        default:
                            continue;
                    }

                    const auto destination = assign->dst.root.as_Local();
                    if (destination < firstStoredLocal) {
                        continue;
                    }
                    auto addSource = [&](unsigned source) {
                        if (source < firstStoredLocal || source == destination) {
                            return;
                        }
                        const auto key = generatorStorageConflictKey(destination, source);
                        if (!conflicts.find(key)) {
                            conflicts.insert(key, true);
                        }
                    };
                    visitMirLvalues(assign->src, [&](const MIRLValue& lvalue, MIRValUsage) {
                        if (lvalue.root.is_Local()) {
                            addSource(lvalue.root.as_Local());
                        }
                        for (const auto& wrapper : lvalue.wrappers) {
                            if (wrapper.is_Index()) {
                                addSource(wrapper.as_Index());
                            }
                        }
                        return false;
                    });
                }
            }
        }

        bool generatorStorageSlotConflicts(
            unsigned local,
            unsigned slot,
            unsigned firstStoredLocal,
            const stl::IntMap<unsigned>& storageSlots,
            const stl::IntMap<bool>& compositeConflicts,
            const MIRValueLifetimes& segmentLifetimes
        ) const {
            for (unsigned other = firstStoredLocal; other < local; other++) {
                const auto* assignedSlot = storageSlots.find(other);
                if (!assignedSlot || *assignedSlot != slot) {
                    continue;
                }
                if (compositeConflicts.find(generatorStorageConflictKey(local, other))) {
                    return true;
                }
                if (segmentLifetimes.slots[local].overlaps(segmentLifetimes.slots[other])) {
                    return true;
                }
            }
            for (const auto& state : generatorState.states) {
                if (state.saved.count(local) == 0) {
                    continue;
                }
                for (const auto& saved : state.saved) {
                    if (saved.first < firstStoredLocal) {
                        continue;
                    }
                    const auto* assignedSlot = storageSlots.find(saved.first);
                    if (assignedSlot && *assignedSlot == slot) {
                        return true;
                    }
                }
            }
            return false;
        }

        MIRValueLifetimes generatorPruneInactiveLocals(
            const Span& sp,
            const StaticTraitResolve& resolve,
            const HIRItemPath& path,
            const HIRTypeData* retTy,
            const HIRFunction::argsT& args,
            MIRFunction& fcn
        ) {
            ASSERT_BUG(sp, !generatorState.states.empty(), "Coroutine has no initial state");
            auto traversalPool = stl::ObjPool::fromMemory();
            stl::IntMap<bool> bridgedReturns{traversalPool.mutPtr()};
            ThinVector<MIRBasicBlockId> bridgedReturnBlocks;
            auto pathCallback = makeCallable<MIRPathCb>([&](auto& os) { os << path; });
            MIRTypeResolve mirResolve{sp, resolve, pathCallback, retTy, args, fcn};
            // Keep the original, disconnected resume segments for storage
            // conflicts. The bridges below deliberately make separate states
            // adjacent for suspension pruning, but not simultaneously alive.
            auto segmentLifetimes = MIRHelperGetLifetimes(mirResolve, fcn, false);
            for (size_t i = 0; i + 1 < generatorState.states.size(); i++) {
                auto& state = generatorState.states[i];
                ASSERT_BUG(sp, state.suspensionBlock < fcn.blocks.size(), "Coroutine state " << i << " has no suspension block");

                // Async-drop expansion can put a normal cleanup chain between
                // the block that produces Pending and its eventual Return.
                // Bridge every Return reachable from that suspension path so
                // liveness sees execution continue at the resume entrypoint.
                stl::IntMap<bool> visited{traversalPool.mutPtr()};
                ThinVector<MIRBasicBlockId> pending;
                pending.push_back(state.suspensionBlock);
                bool foundReturn = false;
                while (pending.size() != 0) {
                    const auto block = pending.back();
                    pending.pop_back();
                    ASSERT_BUG(sp, block < fcn.blocks.size(), "Coroutine suspension target BB" << block << " is out of range");
                    if (visited.find(block)) {
                        continue;
                    }
                    visited.insert(block, true);

                    auto& terminator = fcn.blocks[block].terminator;
                    if (terminator.is_Return()) {
                        ASSERT_BUG(sp, !bridgedReturns.find(block), "Coroutine suspension paths share return BB" << block);
                        bridgedReturns.insert(block, true);
                        bridgedReturnBlocks.push_back(block);
                        terminator = MIRTerminator::make_Goto(generatorState.states[i + 1].entrypoint);
                        foundReturn = true;
                        continue;
                    }

                    struct TargetCollector final: MIRTargetVisitor {
                        ThinVector<MIRBasicBlockId>& targets;

                        explicit TargetCollector(ThinVector<MIRBasicBlockId>& targets)
                            : targets(targets)
                        {
                        }

                        void visitTarget(const MIRBasicBlockId& target) override {
                            targets.push_back(target);
                        }
                    } collector{pending};
                    visitTerminatorTarget(terminator, collector);
                }
                ASSERT_BUG(sp, foundReturn, "Coroutine suspension path from BB" << state.suspensionBlock << " does not return");
            }

            auto lifetimes = MIRHelperGetLifetimes(mirResolve, fcn, false);

            for (const auto block : bridgedReturnBlocks) {
                fcn.blocks[block].terminator = MIRTerminator::make_Return({});
            }

            for (size_t i = 0; i + 1 < generatorState.states.size(); i++) {
                auto& state = generatorState.states[i];
                auto& block = fcn.blocks[state.suspensionBlock];

                for (auto saved = state.saved.begin(); saved != state.saved.end();) {
                    const auto local = saved->first;
                    ASSERT_BUG(sp, local < fcn.locals.size(), "Saved coroutine local " << local << " is out of range");
                    const bool liveAcrossSuspension = lifetimes.slotValid(local, state.suspensionBlock, block.statements.size());
                    if (!liveAcrossSuspension && !resolve.typeNeedsDropGlue(sp, fcn.locals[local])) {
                        saved = state.saved.erase(saved);
                    } else {
                        ++saved;
                    }
                }
            }
            return segmentLifetimes;
        }

        std::set<unsigned> generatorFinalise(const Span& sp, HIREnum& stateEnm) {
            std::set<unsigned> usedVars;
            std::vector<MIRBasicBlockId> armTargets;
            armTargets.reserve(generatorState.states.size() + 2);
            ::std::vector<HIREnum::ValueVariant> enumVariants;
            enumVariants.reserve(generatorState.states.size() + 2);

            auto addStateArm = [&](const GeneratorState::State& state, RcString name) {
                armTargets.push_back(builder.newBbUnlinked());
                builder.setCurBlock(armTargets.back());
                builder.pushStmtAssign(sp, generatorStateLv(), MIRRValue::make_EnumVariant({generatorState.stateIdxEnmPath, GeneratorState::POISONED, {}}));
                builder.endBlock(MIRTerminator::make_Goto(state.entrypoint));
                enumVariants.push_back(HIREnum::ValueVariant{mv$(name), HIRExprPtr(), U128(armTargets.size() - 1)});
                for (const auto& e : state.saved) {
                    usedVars.insert(e.first);
                }
            };

            addStateArm(generatorState.states.front(), RcString::newInterned("UNRESUMED"));

            // A completed async iterator is fused. Other completed coroutines,
            // and all poisoned coroutines, cannot be resumed.
            armTargets.push_back(builder.newBbUnlinked());
            builder.setCurBlock(armTargets.back());
            if (generatorState.isAsyncGen) {
                asyncGenPollReady(sp, "None", {});
                builder.endBlock(MIRTerminator::make_Return({}));
            } else {
                builder.endBlock(MIRTerminator::make_Unreachable({}));
            }
            enumVariants.push_back(HIREnum::ValueVariant{RcString::newInterned("RETURNED"), HIRExprPtr(), U128(GeneratorState::RETURNED)});

            armTargets.push_back(builder.newBbUnlinked());
            builder.setCurBlock(armTargets.back());
            builder.endBlock(MIRTerminator::make_Unreachable({}));
            enumVariants.push_back(HIREnum::ValueVariant{RcString::newInterned("POISONED"), HIRExprPtr(), U128(GeneratorState::POISONED)});

            for (size_t i = 1; i < generatorState.states.size(); i++) {
                addStateArm(generatorState.states[i], RcString());
            }
            stateEnm.data = HIREnum::Class::make_Value({mv$(enumVariants)});

            builder.setCurBlock(generatorState.bbOpen);

            // switch _n { ... }
            builder.endBlock(MIRTerminator::make_Switch({generatorStateLv(), mv$(armTargets)}));

            return usedVars;
        }

        void generatorMakeDrop(const Span& sp, MirBuilder& outBuilder, size_t nCaptures, const ::std::map<unsigned, std::vector<MIRLValue::Wrapper>>& mappings, unsigned dropStateFieldIdx, const std::map<unsigned, unsigned>& dropFlagMapping) const {
            MIRLValue self = MIRLValue::newDeref(MIRLValue::newArgument(0));

            assert(generatorState.states.size() > 0);
            std::vector<MIRBasicBlockId> arms;
            arms.reserve(generatorState.states.size() + 2);

            // Set all drop flags from input
            if (!dropFlagMapping.empty()) {
                auto slot = MIRLValue::newArgument(0);
                slot.wrappers.push_back(MIRLValue::Wrapper::newDeref());                  // Deref `&mut Self`
                slot.wrappers.push_back(MIRLValue::Wrapper::newField(0));                 // Get state field
                slot.wrappers.push_back(MIRLValue::Wrapper::newDowncast(1));              // .value (From MaybeUninit)
                slot.wrappers.push_back(MIRLValue::Wrapper::newField(0));                 // .value (From ManuallyDrop)
                slot.wrappers.push_back(MIRLValue::Wrapper::newField(dropStateFieldIdx)); // drop flag bitset
                for (const auto& flagMapping : dropFlagMapping) {
                    auto i = outBuilder.newDropFlag(false);
                    assert(i == flagMapping.second); // Should hold, as the map was created in-order
                    outBuilder.pushStmt(
                        sp,
                        MIRStatement::make_LoadDropFlag({
                            flagMapping.first,
                            slot.clone(),
                            flagMapping.second,
                        })
                    );
                }
            }

            auto entryBlock = outBuilder.pauseCurBlock();
            // if state is 0, then drop captures (this is the pre-run state)
            arms.push_back(outBuilder.newBbUnlinked());
            outBuilder.setCurBlock(arms.back());
            size_t argCount = 2;
            for (size_t i = 0; i < nCaptures; i++) {
                // TODO: State tracking on captures, what if a by-value capture is moved?
                // Only by-value captures have a mapping to an owned future
                // field. Borrowed captures are aliases and must not be dropped.
                if (mappings.count(argCount + i) != 0) {
                    outBuilder.pushStmtDrop(sp, MIRLValue::newField(self.clone(), 1 + i));
                }
            }
            outBuilder.endBlock(MIRTerminator::make_Return({}));

            auto getLv = [&sp, &self, &mappings](unsigned idx) -> MIRLValue {
                MIRLValue rv = self.clone();
                ASSERT_BUG(sp, mappings.count(idx), "No LValue for index " << idx);
                rv.wrappers.insert(rv.wrappers.end(), mappings.at(idx).begin(), mappings.at(idx).end());
                DEBUG("get_lv: " << rv);
                return rv;
            };

            // Returned and poisoned states have no live coroutine locals.
            for (unsigned i = GeneratorState::RETURNED; i <= GeneratorState::POISONED; i++) {
                arms.push_back(outBuilder.newBbUnlinked());
                outBuilder.setCurBlock(arms.back());
                outBuilder.endBlock(MIRTerminator::make_Return({}));
            }

            // Each stored suspension discriminant corresponds to the locals
            // saved immediately before that yield/await. The final in-body
            // state is never stored: successful completion uses RETURNED.
            for (size_t i = 0; i + 1 < generatorState.states.size(); i++) {
                arms.push_back(outBuilder.newBbUnlinked());
                outBuilder.setCurBlock(arms.back());
                for (const auto& v : generatorState.states[i].saved) {
                    if (v.first == 0) {
                        continue;
                    }
                    // Note: Conditional drop handled by drop flags above
                    // HACK: The caller re-maps drop flags
                    outBuilder.dropActveLocal(sp, getLv(v.first), v.second);
                }
                outBuilder.endBlock(MIRTerminator::make_Return({}));
            }
            // Generate the dispatch switch
            outBuilder.setCurBlock(entryBlock);
            outBuilder.pushStmtAssign(sp, MIRLValue::newReturn(), MIRRValue::make_Tuple({}));
            auto stmtIdxLv = mv$(self);
            stmtIdxLv = MIRLValue::newField(mv$(stmtIdxLv), 0);    // .state
            stmtIdxLv = MIRLValue::newDowncast(mv$(stmtIdxLv), 1); // .value (From MaybeUninit)
            stmtIdxLv = MIRLValue::newField(mv$(stmtIdxLv), 0);    // .value (From ManuallyDrop)
            stmtIdxLv = MIRLValue::newField(mv$(stmtIdxLv), 0);    // .idx
            outBuilder.endBlock(MIRTerminator::make_Switch({mv$(stmtIdxLv), mv$(arms)}));
        }

        void schedulePatternDrops(const Span& sp, const HIRPattern& pat, PatternDropOrder order) override {
            for (const auto slot : patternBindingSlots(pat, order)) {
                builder.scheduleVariableDrop(slot);
            }
        }

        void registerPatternVariables(const Span& sp, const HIRPattern& pat, PatternDropOrder order) override {
            for (const auto slot : patternBindingSlots(pat, order)) {
                builder.registerVariableState(slot);
            }
        }

        void scheduleRegisteredPatternDrops(const Span& sp, const HIRPattern& pat, PatternDropOrder order) override {
            for (const auto slot : patternBindingSlots(pat, order)) {
                builder.scheduleRegisteredVariableDrop(slot);
            }
        }

        MIRLValue getValueForBindingPath(const Span& sp, const HIRTypeData* outerTy, const MIRLValue& outerLval, const PatternBinding& b) override {
            HIRTypeRef ty;
            MIRLValue lval;
            MIRLowerHIRGetTypeValueForPath(sp, builder, outerTy, outerLval, b.field, ty, lval);

            if (b.isSplitSlice()) {
                struct H {
                    static HIRBorrowType getBorrowType(const Span& sp, const HIRPatternBinding& pb) {
                        switch (pb.type) {
                            case HIRPatternBinding::Type::Move:
                                BUG(sp, "By-value pattern binding of a slice");
                            case HIRPatternBinding::Type::Ref:
                                return HIRBorrowType::Shared;
                            case HIRPatternBinding::Type::MutRef:
                                return HIRBorrowType::Unique;
                        }
                        throw "";
                    }
                };

                unsigned subValI = static_cast<unsigned>(b.splitSlice.first + b.splitSlice.second);
                auto& types = builder.resolve().crate.types;
                if (const auto* tep = ty->opt_Array()) {
                    auto innerType = tep->inner;
                    auto len = tep->size.as_Known() - subValI;
                    auto retTy = types.array(innerType, len);

                    if (b.binding->type == HIRPatternBinding::Type::Move) {
                        // Create a new array value
                        std::vector<MIRParam> arrayVals;
                        for (size_t i = b.splitSlice.first; i < tep->size.as_Known() - b.splitSlice.second; i++) {
                            arrayVals.push_back(MIRLValue::newField(lval.clone(), static_cast<unsigned>(i)));
                        }
                        lval = builder.lvalueOrTemp(sp, mv$(retTy), MIRRValue::make_Array({std::move(arrayVals)}));
                    } else {
                        // Create a pointer to this array, by casting a raw pointer to its first element
                        HIRBorrowType bt = H::getBorrowType(sp, *b.binding);
                        MIRLValue ptrVal = builder.lvalueOrTemp(sp, types.pointer(bt, innerType), MIRRValue::make_Borrow({bt, true, MIRLValue::newField(lval.clone(), static_cast<unsigned int>(b.splitSlice.first))}));

                        // 3. Create a slice pointer
                        auto ptrTy = types.pointer(bt, retTy);
                        lval = builder.lvalueOrTemp(sp, ptrTy, MIRRValue::make_Cast({mv$(ptrVal), ptrTy}));
                        // 4. And dereference it
                        lval = MIRLValue::newDeref(std::move(lval));
                    }
                } else if (const auto* tep = ty->opt_Slice()) {
                    auto innerType = tep->inner;

                    // 1. Obtain remaining length
                    auto usizeTy = types.primitive(HIRCoreType::Usize);
                    auto srcLenLval = builder.lvalueOrTemp(sp, usizeTy, MIRRValue::make_DstMeta({builder.getPtrToDst(sp, lval)}));
                    auto subVal = MIRParam(MIRConstant::make_Uint({U128(subValI), HIRCoreType::Usize}));
                    MIRLValue lenVal = builder.lvalueOrTemp(sp, usizeTy, MIRRValue::make_BinOp({mv$(srcLenLval), MIRBinOp::SUB, mv$(subVal)}));

                    // 2. Obtain pointer to the first element
                    // TODO: This currently emits a borrow to that element, but we need a raw pointer (to avoid being technically out-of-bounds)
                    // - Should add a MIR op for `BorrowRaw`
                    HIRBorrowType bt = H::getBorrowType(sp, *b.binding);
                    MIRLValue ptrVal = builder.lvalueOrTemp(sp, types.pointer(bt, innerType), MIRRValue::make_Borrow({bt, true, MIRLValue::newField(lval.clone(), static_cast<unsigned int>(b.splitSlice.first))}));

                    // 3. Create a slice pointer
                    lval = builder.lvalueOrTemp(sp, types.borrow(bt, ty), MIRRValue::make_MakeDst({mv$(ptrVal), mv$(lenVal)}));
                    // 4. And dereference it
                    lval = MIRLValue::newDeref(std::move(lval));
                } else {
                    TODO(sp, "SplitSlice binding: " << b.splitSlice << " - " << ty);
                }
            }

            return lval;
        }

        void destructureFromList(const Span& sp, const HIRTypeData* outerTy, MIRLValue outerLval, const ::std::vector<PatternBinding>& bindings, bool updateStates /*=true*/) override {
            TRACE_FUNCTION_F(outerLval << ": " << outerTy << " [" << bindings << "]");
            // Reverse order to avoid potential use-after-move for `foo @ Bar(baz, ..)`
            for (size_t i = bindings.size(); i--;) {
                const auto& b = bindings[i];
                auto lval = getValueForBindingPath(sp, outerTy, outerLval, b);

                MIRRValue rv;
                switch (b.binding->type) {
                    case HIRPatternBinding::Type::Move:
                        rv = mv$(lval);
                        break;
                    case HIRPatternBinding::Type::Ref:
                        if (borrowRaiseTarget) {
                            DEBUG("- Raising destructure borrow of " << lval << " to scope " << *borrowRaiseTarget);
                            builder.raiseTemporaries(sp, lval, *borrowRaiseTarget);
                        }

                        rv = MIRRValue::make_Borrow({HIRBorrowType::Shared, false, mv$(lval)});
                        break;
                    case HIRPatternBinding::Type::MutRef:
                        if (borrowRaiseTarget) {
                            DEBUG("- Raising destructure borrow of " << lval << " to scope " << *borrowRaiseTarget);
                            builder.raiseTemporaries(sp, lval, *borrowRaiseTarget);
                        }
                        rv = MIRRValue::make_Borrow({HIRBorrowType::Unique, false, mv$(lval)});
                        break;
                }
                // NOTE: Don't drop the destination, as `match` does some tricky things with calling destructure multiple times (to handle or-patterns)
                builder.pushStmtAssign(sp, builder.getVariable(sp, b.binding->slot), mv$(rv), updateStates);
            }
        }

        const HIRTypeData* getBindingType(const Span& sp, unsigned index) const override {
            return variableTypes.at(index);
        }

        void emitUnwind(const Span& sp) {
            builder.emitUnwindCleanup(sp);
            builder.endBlock(MIRTerminator::make_UnwindResume({}));
        }

        // -- ExprVisitor
        void visitNodePtr(HIRExprNodeP& nodeP) override {
            DEBUG(nodeP.get());
            HIRExprVisitor::visitNodePtr(nodeP);
        }

        void visit(HIRExprNodeBlock& node) override {
            TRACE_FUNCTION_F("_Block");
            // NOTE: This doesn't create a BB, as BBs are not needed for scoping
            bool diverged = false;

            auto resVal = (node.valueNode ? builder.newTemporary(node.resType) : MIRLValue());
            // Tail-expression temporaries outlive the block's locals. This is
            // a distinct scope from the one used for extended let initializers.
            auto tailTmpScope = builder.newScopeTemp(node.span());
            auto scope = builder.newScopeVar(node.span());
            auto _block_var_scope = saveAndEdit(blockVarScope, &scope);
            auto tmpScope = builder.newScopeTemp(node.span());
            auto _block_tmp_scope = saveAndEdit(blockTmpScope, &tmpScope);

            for (unsigned int i = 0; i < node.nodes.size(); i++) {
                auto _ = this->disableBorrowExtension();
                auto& subnode = node.nodes[i];
                const Span& sp = subnode->span();

                auto localStmtScope = builder.newScopeTemp(sp);
                const auto* letNode = cast<HIRExprNodeLet>(subnode.get());
                auto _super_let_scope = saveAndEdit(superLetScope, letNode && letNode->isSuper ? superLetScope : &localStmtScope);
                // NOTE: Only set the statement scope if processing a block
                auto _stmt_scope_push = saveAndEdit(stmtScope, cast<HIRExprNodeBlock>(subnode.get()) ? &localStmtScope : nullptr);
                this->visitNodePtr(subnode);

                if (builder.blockActive() || builder.hasResult()) {
                    auto result = builder.getResult(sp);
                    if (!builder.resolve().typeIsCopy(sp, subnode->resType)) {
                        auto discarded = builder.newTemporary(subnode->resType);
                        builder.pushStmtAssign(sp, std::move(discarded), std::move(result));
                    }
                    builder.terminateScope(sp, mv$(localStmtScope));
                    diverged |= subnode->resType->is_Diverge();
                } else {
                    builder.terminateScope(sp, mv$(localStmtScope), false);

                    builder.setCurBlock(builder.newBbUnlinked());
                    diverged = true;
                }
            }

            // For the last node, specially handle.
            // TODO: Any temporaries defined within this node must be elevated into the parent scope
            if (node.valueNode) {
                auto& subnode = node.valueNode;
                const Span& sp = subnode->span();

                auto localStmtScope = builder.newScopeTemp(sp);
                const auto* tailScope = stmtScope ? stmtScope : &tailTmpScope;
                auto _super_let_scope = saveAndEdit(superLetScope, superLetScope ? superLetScope : tailScope);
                this->visitNodePtr(subnode);
                if (builder.hasResult() || builder.blockActive()) {
                    ASSERT_BUG(sp, builder.blockActive(), "Result yielded, but no active block");
                    ASSERT_BUG(sp, builder.hasResult(), "Active block but no result yeilded");
                    // PROBLEM: This can drop the result before we want to use it.

                    builder.pushStmtAssign(sp, resVal.clone(), builder.getResult(sp));

                    // If this block is part of a statement, raise all temporaries from this final scope to the enclosing scope
                    if (stmtScope) {
                        builder.raiseAll(sp, mv$(localStmtScope), *stmtScope);
                    } else {
                        builder.raiseAll(sp, mv$(localStmtScope), tailTmpScope);
                    }
                    builder.setResult(node.span(), mv$(resVal));
                } else {
                    builder.terminateScope(sp, mv$(localStmtScope), false);
                    // Block diverged in final node.
                }
                builder.terminateScope(node.span(), mv$(tmpScope), builder.blockActive());
                builder.terminateScope(node.span(), mv$(scope), builder.blockActive());
                builder.terminateScope(node.span(), mv$(tailTmpScope), builder.blockActive());
            } else {
                if (diverged) {
                    builder.terminateScope(node.span(), mv$(tmpScope), false);
                    builder.terminateScope(node.span(), mv$(scope), false);
                    builder.terminateScope(node.span(), mv$(tailTmpScope), false);
                    builder.endBlock(MIRTerminator::make_Unreachable({}));
                    // Don't set a result if there's no block.
                } else {
                    builder.terminateScope(node.span(), mv$(tmpScope));
                    builder.terminateScope(node.span(), mv$(scope));
                    builder.terminateScope(node.span(), mv$(tailTmpScope));
                    builder.setResult(node.span(), MIRRValue::make_Tuple({}));
                }
            }
        }

        void visit(HIRExprNodeConstBlock& node) override {
            if (cast<HIRExprNodePathValue>(node.inner.get())) {
                this->visitNodePtr(node.inner);
            } else {
                BUG(node.span(), "Const block shouldn't have reached MIR generation");
            }
        }

        void visit(HIRExprNodeAsm& node) override {
            TRACE_FUNCTION_F("_Asm");

            ::std::vector<::std::pair<::std::string, MIRLValue>> inputs;
            // Inputs just need to be in lvalues
            for (auto& v : node.inputs) {
                this->visitNodePtr(v.value);
                auto lv = builder.getResultInLvalue(v.value->span(), v.value->resType);
                inputs.push_back(::std::make_pair(v.spec, mv$(lv)));
            }

            ::std::vector<::std::pair<::std::string, MIRLValue>> outputs;
            // Outputs can also (sometimes) be rvalues (only for `*m`?)
            for (auto& v : node.outputs) {
                this->visitNodePtr(v.value);
                if (v.spec[0] != '=' && v.spec[0] != '+') { // TODO: what does '+' mean?
                    ERROR(node.span(), E0000, "Assembly output specifiers must start with =");
                }
                MIRLValue lv;
                if (v.spec[1] == '*') {
                    lv = builder.getResultInLvalue(v.value->span(), v.value->resType);
                } else {
                    lv = builder.getResultUnwrapLvalue(v.value->span());
                }
                outputs.push_back(::std::make_pair(v.spec, mv$(lv)));
            }

            builder.pushStmtAsm(node.span(), {node.templateText, mv$(outputs), mv$(inputs), node.clobbers, node.flags});
            builder.setResult(node.span(), MIRRValue::make_Tuple({}));
        }

        void visit(HIRExprNodeAsm2& node) override {
            TRACE_FUNCTION_F("_Asm2");

            // TODO: How to represent inout in the MIR?
            // - Potentially a register specifier that links to one of the inputs
            // - OR: Just keep the parameter list as before - but now simplified to just one `Reg`
            MIRStatement::Data_Asm2 ent;
            ent.options = node.options;
            ent.lines = node.lines;
            std::vector<std::pair<MIRBasicBlockId, HIRExprNodeP*>> labels;

            auto movedParam = [&](const MIRParam& p) {
                if (const auto* e = p.opt_LValue()) {
                    builder.movedLvalue(node.span(), *e);
                }
            };

            for (auto& v : node.params) {
                switch (v.tag()) {
                    case HIRAsmParam::TAG_Const: {
                        auto& e = v.as_Const();
                        // A named constant is a constant: lowering an ordinary
                        // read of one puts it in a temporary first, which is
                        // not what an immediate operand can be given.
                        if (const auto* pv = cast<HIRExprNodePathValue>(&*e); pv && pv->target == HIRExprNodePathValue::CONSTANT) {
                            ent.params.push_back(MIRAsmParam::make_Const(MIRConstant::make_Const({box$(pv->path.clone())})));
                            break;
                        }
                        // This constant needs to have been evaluated fully (so a `MIR::Constant` can be created)
                        this->visitNodePtr(e);
                        auto param = builder.getResultInParam(e->span(), e->resType);
                        if (param.is_Constant()) {
                            ent.params.push_back(MIRAsmParam::make_Const(std::move(param.as_Constant())));
                        } else {
                            TODO(node.span(), "asm! const");
                        }
                        break;
                    }
                    case HIRAsmParam::TAG_Sym: {
                        auto& e = v.as_Sym();
                        ent.params.push_back(MIRAsmParam::make_Sym(e.clone()));
                        break;
                    }
                    case HIRAsmParam::TAG_Label: {
                        auto& e = v.as_Label();
                        auto bb = builder.newBbUnlinked();
                        ent.params.push_back(MIRAsmParam::make_Label(bb));
                        labels.push_back({bb, &e.code});
                        break;
                    }
                    case HIRAsmParam::TAG_RegSingle: {
                        auto& e = v.as_RegSingle();
                        std::unique_ptr<MIRParam> input;
                        std::unique_ptr<MIRLValue> output;
                        this->visitNodePtr(e.val);
                        switch (e.dir) {
                            case AsmDirection::In:
                                ASSERT_BUG(node.span(), e.val, "`in` register with no value");
                                input = box$(builder.getResultInParam(e.val->span(), e.val->resType));
                                break;
                            case AsmDirection::Out:
                            case AsmDirection::LateOut:
                                if (e.val) {
                                    output = box$(builder.getResultUnwrapLvalue(e.val->span()));
                                }
                                break;
                            case AsmDirection::InOut:
                            case AsmDirection::InLateOut:
                                ASSERT_BUG(node.span(), e.val, "`inout` register with no value");
                                output = box$(builder.getResultUnwrapLvalue(e.val->span()));
                                input = std::make_unique<MIRParam>(output->clone());
                                break;
                        }
                        if (input) {
                            movedParam(*input);
                        }
                        ent.params.push_back(MIRAsmParam::make_Reg({e.dir, std::move(e.spec), std::move(input), std::move(output)}));
                        break;
                    }
                    case HIRAsmParam::TAG_Reg: {
                        auto& e = v.as_Reg();
                        std::unique_ptr<MIRParam> input;
                        std::unique_ptr<MIRLValue> output;
                        switch (e.dir) {
                            case AsmDirection::In:
                                ASSERT_BUG(node.span(), e.valIn, "`in` register with no input");
                                this->visitNodePtr(e.valIn);
                                input = box$(builder.getResultInParam(e.valIn->span(), e.valIn->resType));
                                assert(!e.valOut);
                                break;
                            case AsmDirection::Out:
                            case AsmDirection::LateOut:
                                ASSERT_BUG(node.span(), !e.valIn, "`[late]out` register with input value");
                                if (e.valOut) {
                                    this->visitNodePtr(e.valOut);
                                    output = box$(builder.getResultUnwrapLvalue(e.valOut->span()));
                                }
                                break;
                            case AsmDirection::InOut:
                            case AsmDirection::InLateOut:
                                ASSERT_BUG(node.span(), e.valIn, "`in[late]out` register with no input");
                                this->visitNodePtr(e.valIn);
                                input = box$(builder.getResultInParam(e.valIn->span(), e.valIn->resType));
                                if (e.valOut) {
                                    this->visitNodePtr(e.valOut);
                                    output = box$(builder.getResultUnwrapLvalue(e.valOut->span()));
                                }
                                break;
                        }
                        if (input) {
                            movedParam(*input);
                        }
                        ent.params.push_back(MIRAsmParam::make_Reg({e.dir, std::move(e.spec), std::move(input), std::move(output)}));
                        break;
                    }
                }
            }

            if (labels.empty()) {
                builder.pushStmt(node.span(), mv$(ent));
                if (!node.options.noreturn) {
                    builder.setResult(node.span(), MIRRValue::make_Tuple({}));
                } else {
                    builder.endBlock(MIRTerminator::make_Unreachable({}));
                }
                return;
            }

            const auto nextBb = builder.newBbUnlinked();
            auto splitScope = builder.newScopeSplit(node.span());
            builder.endBlock(MIRTerminator::make_Asm2({std::move(ent.options), std::move(ent.lines), std::move(ent.params), node.options.noreturn ? ~0u : nextBb}));

            bool hasReachableArm = false;
            if (!node.options.noreturn) {
                builder.setCurBlock(nextBb);
                builder.endSplitArm(node.span(), splitScope, /*reachable=*/true);
                builder.pauseCurBlock();
                hasReachableArm = true;
            } else {
                builder.endSplitArm(node.span(), splitScope, /*reachable=*/false);
            }

            for (auto& label : labels) {
                builder.setCurBlock(label.first);
                this->visitNodePtr(*label.second);
                if (builder.blockActive()) {
                    if (builder.hasResult()) {
                        builder.getResult((*label.second)->span());
                    }
                    builder.endSplitArm((*label.second)->span(), splitScope, /*reachable=*/true);
                    builder.endBlock(MIRTerminator::make_Goto(nextBb));
                    hasReachableArm = true;
                } else {
                    builder.endSplitArm((*label.second)->span(), splitScope, /*reachable=*/false);
                }
            }

            builder.setCurBlock(nextBb);
            builder.terminateScope(node.span(), mv$(splitScope), hasReachableArm);
            if (hasReachableArm) {
                builder.setResult(node.span(), MIRRValue::make_Tuple({}));
            } else {
                builder.endBlock(MIRTerminator::make_Unreachable({}));
            }
        }

        /// The `Poll<Option<Item>>` return type of an `async gen` body, split
        /// into the two enum paths a return value needs.
        struct AsyncGenReturn {
            HIRGenericPath poll;
            const HIRTypeData* optionTy;
        };

        AsyncGenReturn asyncGenReturnType(const Span& sp) {
            AsyncGenReturn rv{HIRGenericPath(), nullptr};
            const auto* ty = builder.valType(sp, MIRLValue::newReturn());
            const auto& gp = ty->as_Path().path.data.as_Generic();
            ASSERT_BUG(sp, ty->as_Path().binding.as_Enum()->findVariant("Ready") == 0, "");
            ASSERT_BUG(sp, gp.params.types.size() == 1, "`async gen` return type " << ty);
            rv.poll = gp.clone();
            rv.optionTy = gp.params.types.at(0);
            return rv;
        }

        /// `RETURN = Poll::Ready(<Option variant>(values))`
        void asyncGenPollReady(const Span& sp, const char* variant, ::std::vector<MIRParam> values) {
            auto ret = asyncGenReturnType(sp);
            const auto& optionEnum = *ret.optionTy->as_Path().binding.as_Enum();
            const auto variantIdx = optionEnum.findVariant(variant);
            ASSERT_BUG(sp, variantIdx != SIZE_MAX, "Unable to find variant " << variant << " in " << ret.optionTy);
            auto item = builder.newTemporary(ret.optionTy);
            builder.pushStmtAssign(sp, item.clone(), MIRRValue::make_EnumVariant({ret.optionTy->as_Path().path.data.as_Generic().clone(), static_cast<unsigned>(variantIdx), std::move(values)}));
            builder.pushStmtAssign(sp, MIRLValue::newReturn(), MIRRValue::make_EnumVariant({std::move(ret.poll), 0, ::makeVec1(MIRParam(std::move(item)))}));
        }

        // Common code used by both `ExprNodeReturn` and the final return of a GeneratorWrapper
        void coroutineReturn(const Span& sp, const HIRTypeData* valueTy) {
            static RcString rcstringComplete = RcString::newInterned("Complete");
            static RcString rcstringReady = RcString::newInterned("Ready"); // TODO: This is a lang item
            if (generatorState.isAsyncGen) {
                // An `async gen` body ends the sequence: `Poll::Ready(None)`.
                // Its own value is unit, and is dropped here.
                builder.getResultInParam(sp, valueTy);
                asyncGenPollReady(sp, "None", {});
                builder.pushStmtAssign(sp, generatorStateLv(), MIRRValue::make_EnumVariant({generatorState.stateIdxEnmPath.clone(), GeneratorState::RETURNED, {}}));
                return;
            }
            const auto& variantName = generatorState.isFuture ? rcstringReady : rcstringComplete;
            // TODO: Handle difference between generators and futures (different return/yield types)
            const auto& te = builder.valType(sp, MIRLValue::newReturn())->as_Path();
            HIRGenericPath enmPath = te.path.data.as_Generic().clone();
            const size_t variantIndex = te.binding.as_Enum()->findVariant(variantName);
            ASSERT_BUG(sp, enmPath.path != HIRSimplePath(), "Failed to get path from return type?");
            ASSERT_BUG(sp, variantIndex != SIZE_MAX, "Unable to find variant " << variantName << " in " << enmPath << " for coroutine return");

            ::std::vector<MIRParam> values;
            values.push_back(builder.getResultInParam(sp, valueTy));
            auto res = MIRRValue::make_EnumVariant({std::move(enmPath), static_cast<unsigned>(variantIndex), std::move(values)});
            builder.pushStmtAssign(sp, MIRLValue::newReturn(), std::move(res));
            builder.pushStmtAssign(sp, generatorStateLv(), MIRRValue::make_EnumVariant({generatorState.stateIdxEnmPath.clone(), GeneratorState::RETURNED, {}}));
        }

        void visit(HIRExprNodeReturn& node) override {
            TRACE_FUNCTION_F("_Return");

            if (node.isTailCall) {
                MIRCallTarget target;
                ::std::vector<MIRParam> args;

                if (auto* call = cast<HIRExprNodeCallPath>(node.value.get())) {
                    args = getArgs(call->args);
                    if (!builder.blockActive()) {
                        return;
                    }
                    if (const auto* path = call->path.data.opt_Generic()) {
                        const auto& fcn = builder.crate().getFunctionByPath(node.span(), path->path);
                        if (path->path.crateName() == "#intrinsics"
                            || fcn.abi == "rust-intrinsic"
                            || fcn.abi == "platform-intrinsic") {
                            ERROR(node.span(), E0000, "intrinsics cannot be tail-called with `become`");
                        }
                    }
                    target = MIRCallTarget::make_Path(call->path.clone());
                } else if (auto* call = cast<HIRExprNodeCallValue>(node.value.get())) {
                    ASSERT_BUG(node.span(), call->value->resType->is_Function(), "Tail call through a non-function value");
                    this->visitNodePtr(call->value);
                    if (!builder.blockActive()) {
                        return;
                    }
                    auto fcnVal = builder.newTemporary(call->value->resType);
                    builder.pushStmtAssign(call->value->span(), fcnVal.clone(), builder.getResult(call->value->span()));
                    args = getArgs(call->args);
                    if (!builder.blockActive()) {
                        return;
                    }
                    target = MIRCallTarget::make_Value(mv$(fcnVal));
                } else {
                    BUG(node.span(), "Tail call expression was not a call");
                }

                builder.terminateScopeEarly(node.span(), builder.fcnScope());
                builder.endBlock(MIRTerminator::make_TailCall({mv$(target), mv$(args), SourceLocation(node.span())}));
                return;
            }

            this->visitNodePtr(node.value);

            if (!builder.blockActive()) {
                return;
            }

            if (isGenerator) {
                coroutineReturn(node.span(), node.value->resType);
            } else {
                builder.pushStmtAssign(node.span(), MIRLValue::newReturn(), builder.getResult(node.span()));
            }
            builder.terminateScopeEarly(node.span(), builder.fcnScope());
            builder.endBlock(MIRTerminator::make_Return({}));
        }

        void visit(HIRExprNodeYield& node) override {
            TRACE_FUNCTION_F("_Yield");
            if (isGenerator && generatorState.isAsyncGen) {
                // `yield v` in an `async gen` body returns `Poll::Ready(Some(v))`
                // and resumes at the next state, the same way `.await` returns
                // `Poll::Pending`.
                this->visitNodePtr(node.value);
                if (!builder.blockActive()) {
                    return;
                }
                asyncGenPollReady(node.span(), "Some", ::makeVec1(builder.getResultInParam(node.span(), node.value->resType)));
                builder.pushStmtAssign(node.span(), generatorStateLv(), MIRRValue::make_EnumVariant({generatorState.stateIdxEnmPath.clone(), static_cast<unsigned>(generatorState.states.size()) + GeneratorState::POISONED, {}}));
                // NOTE: No scope terminate
                const auto suspensionBlock = builder.activeBlock();
                builder.endBlock(MIRTerminator::make_Return({}));

                generatorState.states.back().saved = builder.getActiveLocals(node.span(), generatorState.savedDropFlags);
                generatorState.states.back().suspensionBlock = suspensionBlock;
                generatorState.states.push_back(builder.newBbUnlinked());
                builder.setCurBlock(generatorState.states.back().entrypoint);

                builder.setResult(node.span(), MIRRValue::make_Tuple({}));
                return;
            }
            if (isGenerator) {
                ASSERT_BUG(node.span(), !generatorState.isFuture, "");

                const auto& te = builder.valType(node.span(), MIRLValue::newReturn())->as_Path();
                HIRGenericPath enmPath = te.path.data.as_Generic().clone();
                ASSERT_BUG(node.span(), te.binding.as_Enum()->findVariant("Yielded") == 0, "");

                this->visitNodePtr(node.value);
                // Emit return, wrapped in GeneratorState::Yielded
                ::std::vector<MIRParam> values;
                values.push_back(builder.getResultInParam(node.span(), node.value->resType));
                auto res = MIRRValue::make_EnumVariant(
                    {mv$(enmPath),
                     0, // Yielded is the first variant
                     mv$(values)}
                );
                builder.pushStmtAssign(node.span(), MIRLValue::newReturn(), mv$(res));
                builder.pushStmtAssign(node.span(), generatorStateLv(), MIRRValue::make_EnumVariant({generatorState.stateIdxEnmPath.clone(), static_cast<unsigned>(generatorState.states.size()) + GeneratorState::POISONED, {}}));
                // NOTE: No scope terminate
                const auto suspensionBlock = builder.activeBlock();
                builder.endBlock(MIRTerminator::make_Return({}));

                generatorState.states.back().saved = builder.getActiveLocals(node.span(), generatorState.savedDropFlags);
                generatorState.states.back().suspensionBlock = suspensionBlock;
                generatorState.states.push_back(builder.newBbUnlinked());
                builder.setCurBlock(generatorState.states.back().entrypoint);

                // `yield` evaluates to the argument the coroutine was resumed
                // with, which the body takes after the self pointer. A `gen`
                // block resumes with `()`, and gets it.
                builder.setResult(node.span(), MIRRValue::make_Use(MIRLValue::newArgument(1)));
            } else {
                BUG(node.span(), "Unexpected ExprNode_Yield (should have been re-written)");
            }
        }

        void visit(HIRExprNodeAWait& node) override {
            const Span& sp = node.span();
            TRACE_FUNCTION_F("_AWait");
            ASSERT_BUG(node.span(), isGenerator && generatorState.isFuture, "`.await` not in an async block/function");
            const auto& tyInner = node.value->resType;

            this->visitNodePtr(node.value);
            if (!builder.blockActive()) {
                return;
            }
            auto lvRes = builder.getResultInLvalue(sp, tyInner);

            builder.setResult(node.span(), awaitFuture(sp, tyInner, std::move(lvRes), node.resType, node.isNext));
        }

        void visit(HIRExprNodeUse& node) override {
            BUG(node.span(), "`.use` expression was not expanded before MIR lowering");
        }

        void visit(HIRExprNodeLet& node) override {
            TRACE_FUNCTION_F("_Let " << node.pattern);
            if (node.value) {
                auto _ = saveAndEdit(borrowRaiseTarget, blockTmpScope);
                auto _super_let_scope = saveAndEdit(superLetScope, node.isSuper ? superLetScope : blockVarScope);
                this->visitNodePtr(node.value);

                if (!builder.blockActive()) {
                    return;
                }
                auto res = builder.getResult(node.span());

                // Shortcut for `let foo = bar;` (avoids the extra temporary that would need to be optimised out)
                // - Only for a single binding: `let a @ b = ...` needs the
                //   value once per binding, which the general path handles.
                if (node.pattern.data.is_Any() && node.pattern.bindings.size() == 1 && std::all_of(node.pattern.bindings.begin(), node.pattern.bindings.end(), [](const HIRPatternBinding& pb) {
                    return pb.type == HIRPatternBinding::Type::Move;
                })) {
                    this->schedulePatternDrops(node.span(), node.pattern, PatternDropOrder::FirstCandidate);
                    for (const auto& pb : node.pattern.bindings) {
                        builder.pushStmtAssign(node.span(), builder.getVariable(node.span(), pb.slot), mv$(res));
                    }
                } else {
                    auto patternValue = builder.lvalueOrTemp(node.value->span(), node.type, mv$(res));
                    auto dropValue = patternValue.clone();
                    this->registerPatternVariables(node.span(), node.pattern, PatternDropOrder::FirstCandidate);
                    MIRLowerHIRLet(builder, *this, node.span(), node.pattern, mv$(patternValue), nullptr);
                    if (blockTmpScope) {
                        builder.moveTemporaryDropToVariableScope(node.span(), dropValue, *blockTmpScope);
                    }
                    this->scheduleRegisteredPatternDrops(node.span(), node.pattern, PatternDropOrder::FirstCandidate);
                }
            } else {
                this->schedulePatternDrops(node.span(), node.pattern, PatternDropOrder::Declaration);
            }
            if (node.isSuper) {
                ASSERT_BUG(node.span(), superLetScope, "`super let` without an enclosing expression scope");
                for (const auto slot : patternBindingSlots(node.pattern, PatternDropOrder::FirstCandidate)) {
                    builder.moveVariableToScope(node.span(), slot, *superLetScope);
                }
            }
            builder.setResult(node.span(), MIRRValue::make_Tuple({}));
        }

        void visit(HIRExprNodeLoop& node) override {
            TRACE_FUNCTION_FR("_Loop", "_Loop");
            auto loopBlock = builder.newBbLinked();
            auto loopBodyScope = builder.newScopeLoop(node.span());
            auto loopNext = builder.newBbUnlinked();

            auto loopResultLvaue = builder.newTemporary(node.resType);

            auto loopTmpScope = builder.newScopeTemp(node.span());
            auto _ = saveAndEdit(stmtScope, &loopTmpScope);

            loopStack.push_back(LoopDesc{mv$(loopBodyScope), node.label, node.requireLabel, loopBlock, loopNext, loopResultLvaue.clone()});
            this->visitNodePtr(node.code);
            auto loopScope = mv$(loopStack.back().scope);
            loopStack.pop_back();

            // If there's a stray result, drop it
            if (builder.hasResult()) {
                assert(builder.blockActive());
                // TODO: Properly drop this? Or just discard it? It should be ()
                builder.getResult(node.span());
            }
            // Terminate block with a jump back to the start
            // - Also inserts the jump if this didn't uncondtionally diverge
            if (builder.blockActive()) {
                DEBUG("- Reached end, loop back");
                // Insert drop of all scopes within the current scope
                builder.terminateScope(node.span(), mv$(loopTmpScope));
                builder.terminateScope(node.span(), mv$(loopScope));
                builder.endBlock(MIRTerminator::make_Goto(loopBlock));
            } else {
                // Terminate scope without emitting cleanup (cleanup was handled by `break`)
                builder.terminateScope(node.span(), mv$(loopTmpScope), false);
                builder.terminateScope(node.span(), mv$(loopScope), false);
            }

            if (!node.diverges) {
                DEBUG("- Doesn't diverge");
                builder.setCurBlock(loopNext);
                builder.setResult(node.span(), mv$(loopResultLvaue));
            } else {
                DEBUG("- Diverges");
                assert(!builder.hasResult());

                builder.setCurBlock(loopNext);
                builder.endSplitArmEarly(node.span());
                assert(!builder.hasResult());
                builder.endBlock(MIRTerminator::make_Unreachable({}));
            }

            // TODO: Store the variable state on a break for restoration at the end of the loop.
        }

        /// Locate a loop given a name
        const LoopDesc& findLoop(const Span& sp, const RcString& targetLabel) const {
            if (targetLabel != "") {
                auto it = ::std::find_if(loopStack.rbegin(), loopStack.rend(), [&](const auto& x) {
                    return x.label == targetLabel;
                });
                if (it == loopStack.rend()) {
                    BUG(sp, "Named loop '" << targetLabel << " doesn't exist");
                }
                return *it;
            } else {
                auto it = ::std::find_if(loopStack.rbegin(), loopStack.rend(), [](const auto& x) {
                    return !x.requireLabel;
                });
                if (it == loopStack.rend()) {
                    BUG(sp, "Break outside of a breakable block");
                }
                if (it->label != "" && it->label.c_str()[0] == '#') {
                    TODO(sp, "Break within try block, want to break parent loop instead");
                }
                return *it;
            }
        }

        void visit(HIRExprNodeLoopControl& node) override {
            TRACE_FUNCTION_F("_LoopControl \"" << node.label << "\"");
            if (loopStack.size() == 0) {
                BUG(node.span(), "Loop control outside of a loop");
            }

            // Visit value before looking up the loop (loop stack may be manipulated during the inner visit)
            if (node.value) {
                ASSERT_BUG(node.span(), !node.isContinue, "Continue with a value isn't valid");
                DEBUG("break value;");
                this->visitNodePtr(node.value);
                if (node.value->resType->is_Diverge()) {
                }
            }
            if (!builder.blockActive()) {
                // No block is currently active, not worth running the rest
                return;
            }

            // TODO: Use node.m_target_node
            const LoopDesc& targetBlock = this->findLoop(node.span(), node.label);

            if (node.isContinue) {
                builder.terminateScopeEarly(node.span(), targetBlock.scope, /*loop_exit=*/false);
                builder.endBlock(MIRTerminator::make_Goto(targetBlock.cur));
            } else {
                if (node.value) {
                    builder.pushStmtAssign(node.span(), targetBlock.resValue.clone(), builder.getResult(node.span()));
                } else {
                    // Set result to ()
                    builder.pushStmtAssign(node.span(), targetBlock.resValue.clone(), MIRRValue::make_Tuple({{}}));
                }
                builder.terminateScopeEarly(node.span(), targetBlock.scope, /*loop_exit=*/true);
                builder.endBlock(MIRTerminator::make_Goto(targetBlock.next));
            }
        }

        void visit(HIRExprNodeMatch& node) override {
            TRACE_FUNCTION_FR("_Match", "_Match");
            std::vector<unsigned> letElseInitializerTemps;
            size_t letElseFirstTemporary = 0;
            if (node.isLetElse) {
                ASSERT_BUG(node.span(), borrowRaiseTarget, "let-else match has no remainder temporary scope");
                letElseFirstTemporary = builder.localCount();
                this->visitNodePtr(node.value);
            } else {
                auto _ = saveAndEdit(borrowRaiseTarget, nullptr);
                this->visitNodePtr(node.value);
            }
            if (!builder.blockActive()) {
                return;
            }
            auto matchVal = builder.getResultInLvalue(node.value->span(), node.value->resType);
            if (node.isLetElse) {
                const auto endTemporary = builder.localCount();
                letElseInitializerTemps.reserve(endTemporary - letElseFirstTemporary);
                for (auto temporary = letElseFirstTemporary; temporary < endTemporary; ++temporary) {
                    letElseInitializerTemps.push_back(temporary);
                }
            }

            if (node.arms.size() == 0) {
                // Nothing
                // TODO: Ensure that the type is a zero-variant enum or !
                builder.endSplitArmEarly(node.span());
                builder.endBlock(MIRTerminator::make_Unreachable({}));
                // Push an "diverge" result
            } else {
                MIRLowerHIRMatch(builder, *this, node, mv$(matchVal), letElseInitializerTemps);
            }

            if (builder.blockActive()) {
                const auto& sp = node.span();

                auto res = builder.getResult(sp);
                builder.setResult(sp, mv$(res));

            } else {
            }
        } // ExprNodeMatch

        void emitIf(/*const*/ HIRExprNodeP& cond, MIRBasicBlockId trueBranch, MIRBasicBlockId falseBranch) {
            TRACE_FUNCTION_F("true=bb" << trueBranch << ", false=bb" << falseBranch);
            auto* condP = &cond;

            // - Convert ! into a reverse of the branches
            {
                bool reverse = false;
                while (auto* condUni = cast<HIRExprNodeUniOp>(condP->get())) {
                    ASSERT_BUG(condUni->span(), condUni->op == HIRExprNodeUniOp::Op::Invert, "Unexpected UniOp on boolean in `if` condition");
                    condP = &condUni->value;
                    reverse = !reverse;
                }

                if (reverse) {
                    ::std::swap(trueBranch, falseBranch);
                }
            }

            // Short-circuit && and ||
            if (auto* condBin = cast<HIRExprNodeBinOp>(condP->get())) {
                switch (condBin->op) {
                    case HIRExprNodeBinOp::Op::BoolAnd:
                    case HIRExprNodeBinOp::Op::BoolOr: {
                        // TODO: Generate a SplitScope
                        if (condBin->op == HIRExprNodeBinOp::Op::BoolAnd) {
                            DEBUG("- Short-circuit BoolAnd");

                            // IF left false: go to false immediately
                            auto innerTrueBranch = builder.newBbUnlinked();
                            emitIf(condBin->left, innerTrueBranch, falseBranch);
                            // ELSE use right
                            builder.setCurBlock(innerTrueBranch);
                        } else {
                            DEBUG("- Short-circuit BoolOr");

                            // IF left true: got to true
                            auto innerFalseBranch = builder.newBbUnlinked();
                            emitIf(condBin->left, trueBranch, innerFalseBranch);
                            // ELSE use right
                            builder.setCurBlock(innerFalseBranch);
                        }

                        auto splitScope = builder.newScopeSplit(condBin->span());
                        builder.endSplitArm(condBin->span(), splitScope, /*reachable=*/true);
                        auto finalTrueBranch = builder.newBbUnlinked();
                        auto finalFalseBranch = builder.newBbUnlinked();
                        emitIf(condBin->right, finalTrueBranch, finalFalseBranch);

                        builder.setCurBlock(finalFalseBranch);
                        builder.endSplitArm(condBin->span(), splitScope, /*reachable=*/true, true);
                        builder.endBlock(MIRTerminator::make_Goto(falseBranch));

                        builder.setCurBlock(finalTrueBranch);
                        builder.endSplitArm(condBin->span(), splitScope, /*reachable=*/true);
                        builder.terminateScope(condBin->span(), std::move(splitScope));
                        builder.endBlock(MIRTerminator::make_Goto(trueBranch));
                    }
                        return;
                    default:
                        break;
                }
            }

            if (auto* condLit = cast<HIRExprNodeLiteral>(condP->get())) {
                DEBUG("- constant condition");
                if (condLit->data.as_Boolean()) {
                    builder.endBlock(MIRTerminator::make_Goto(trueBranch));
                } else {
                    builder.endBlock(MIRTerminator::make_Goto(falseBranch));
                }
                return;
            }

            // If short-circuiting didn't apply, emit condition
            MIRLValue decisionVal;
            {
                auto scope = builder.newScopeTemp(cond->span());
                this->visitNodePtr(*condP);
                ASSERT_BUG(cond->span(), cond->resType == HIRCoreType::Bool, "If condition wasn't a bool");
                decisionVal = builder.getResultInIfCond(cond->span());
                builder.terminateScope(cond->span(), mv$(scope));
            }

            builder.endBlock(MIRTerminator::make_If({mv$(decisionVal), trueBranch, falseBranch}));
        }

        static bool isSignedInteger(HIRCoreType type) {
            switch (type) {
                case HIRCoreType::Isize:
                case HIRCoreType::I8:
                case HIRCoreType::I16:
                case HIRCoreType::I32:
                case HIRCoreType::I64:
                case HIRCoreType::I128:
                    return true;
                default:
                    return false;
            }
        }

        static HIRCoreType unsignedIntegerType(HIRCoreType type) {
            switch (type) {
                case HIRCoreType::Isize: return HIRCoreType::Usize;
                case HIRCoreType::I8: return HIRCoreType::U8;
                case HIRCoreType::I16: return HIRCoreType::U16;
                case HIRCoreType::I32: return HIRCoreType::U32;
                case HIRCoreType::I64: return HIRCoreType::U64;
                case HIRCoreType::I128: return HIRCoreType::U128;
                default: return type;
            }
        }

        static unsigned integerBits(HIRCoreType type) {
            switch (type) {
                case HIRCoreType::U8: case HIRCoreType::I8: return 8;
                case HIRCoreType::U16: case HIRCoreType::I16: return 16;
                case HIRCoreType::U32: case HIRCoreType::I32: return 32;
                case HIRCoreType::U64: case HIRCoreType::I64: return 64;
                case HIRCoreType::U128: case HIRCoreType::I128: return 128;
                case HIRCoreType::Usize: case HIRCoreType::Isize: return TargetGetPointerBits();
                default: return 0;
            }
        }

        static MIRConstant integerConstant(HIRCoreType type, U128 value) {
            if (isSignedInteger(type)) {
                const auto bits = integerBits(type);
                if (bits < 128 && value.bit(bits - 1)) {
                    value |= U128::max() << bits;
                }
                return MIRConstant::make_Int({S128(value), type});
            }
            return MIRConstant::make_Uint({value, type});
        }

        static MIRRValue paramRvalue(MIRParam value) {
            if (auto* lvalue = value.opt_LValue()) {
                return MIRRValue(mv$(*lvalue));
            }
            if (auto* constant = value.opt_Constant()) {
                return MIRRValue(mv$(*constant));
            }
            auto& borrow = value.as_Borrow();
            return MIRRValue::make_Borrow({borrow.type, false, mv$(borrow.val)});
        }

        void panicIf(const Span& sp, MIRLValue condition, bool whenTrue, const char* langItem) {
            auto panicBlock = builder.newBbUnlinked();
            auto continueBlock = builder.newBbUnlinked();
            builder.endBlock(MIRTerminator::make_If({
                mv$(condition),
                whenTrue ? panicBlock : continueBlock,
                whenTrue ? continueBlock : panicBlock,
            }));

            builder.setCurBlock(panicBlock);
            const auto& panicPath = builder.crate().getLangItemPathOpt(langItem);
            if (panicPath.components().empty()) {
                // A no_core crate can use primitive arithmetic while providing
                // no panic runtime. Keep the failure edge explicit and fatal;
                // safe constant expressions can eliminate it without eagerly
                // requiring a lang item that does not exist.
                builder.endBlock(MIRTerminator::make_UnwindTerminate({}));
                builder.setCurBlock(continueBlock);
                return;
            }
            auto panicResult = builder.newTemporary(builder.resolve().crate.types.diverge());
            auto panicReturn = builder.newBbUnlinked();
            auto panicUnwind = builder.newBbUnlinked();
            builder.endBlock(MIRTerminator::make_Call({
                panicReturn,
                MIRUnwindAction::make_Cleanup(panicUnwind),
                mv$(panicResult),
                HIRPath(panicPath),
                {},
            }));

            builder.setCurBlock(panicReturn);
            builder.endBlock(MIRTerminator::make_Unreachable({}));

            builder.setCurBlock(panicUnwind);
            emitUnwind(sp);

            builder.setCurBlock(continueBlock);
        }

        void generateOverflowingArithmetic(
            const Span& sp,
            MIRLValue resSlot,
            const char* intrinsic,
            const char* panicLangItem,
            MIRParam valL,
            const HIRTypeData* tyL,
            MIRParam valR,
            bool updateDestState
        ) {
            const auto tupleType = builder.resolve().crate.types.tuple({
                tyL,
                builder.resolve().crate.types.primitive(HIRCoreType::Bool),
            });
            auto checked = builder.newTemporary(tupleType);
            auto callReturn = builder.newBbUnlinked();
            builder.endBlock(MIRTerminator::make_Call({
                callReturn,
                MIRUnwindAction::make_Unreachable({}),
                checked.clone(),
                MIRCallTarget::make_Intrinsic({intrinsic, HIRPathParams(tyL)}),
                makeVec2<MIRParam>(mv$(valL), mv$(valR)),
            }));

            builder.setCurBlock(callReturn);
            builder.pushStmtAssign(
                sp,
                mv$(resSlot),
                MIRRValue::make_Use({MIRLValue::newField(checked.clone(), 0)}),
                updateDestState
            );
            panicIf(sp, MIRLValue::newField(mv$(checked), 1), true, panicLangItem);
        }

        void generateDivisionChecks(
            const Span& sp,
            MIRBinOp op,
            const MIRParam& valL,
            HIRCoreType type,
            const MIRParam& valR
        ) {
            auto isZero = builder.newTemporary(builder.resolve().crate.types.primitive(HIRCoreType::Bool));
            builder.pushStmtAssign(sp, isZero.clone(), MIRRValue::make_BinOp({
                valR.clone(), MIRBinOp::EQ, MIRParam(integerConstant(type, U128(0))),
            }));
            panicIf(
                sp,
                mv$(isZero),
                true,
                op == MIRBinOp::DIV ? "panic_const_div_by_zero" : "panic_const_rem_by_zero"
            );

            if (!isSignedInteger(type)) {
                return;
            }
            auto isMinusOne = builder.newTemporary(builder.resolve().crate.types.primitive(HIRCoreType::Bool));
            builder.pushStmtAssign(sp, isMinusOne.clone(), MIRRValue::make_BinOp({
                valR.clone(), MIRBinOp::EQ, MIRParam(integerConstant(type, ~U128(0))),
            }));
            auto isMinimum = builder.newTemporary(builder.resolve().crate.types.primitive(HIRCoreType::Bool));
            builder.pushStmtAssign(sp, isMinimum.clone(), MIRRValue::make_BinOp({
                valL.clone(),
                MIRBinOp::EQ,
                MIRParam(integerConstant(type, U128(1) << (integerBits(type) - 1))),
            }));
            auto overflows = builder.newTemporary(builder.resolve().crate.types.primitive(HIRCoreType::Bool));
            builder.pushStmtAssign(sp, overflows.clone(), MIRRValue::make_BinOp({
                MIRParam(mv$(isMinusOne)), MIRBinOp::BIT_AND, MIRParam(mv$(isMinimum)),
            }));
            panicIf(
                sp,
                mv$(overflows),
                true,
                op == MIRBinOp::DIV ? "panic_const_div_overflow" : "panic_const_rem_overflow"
            );
        }

        void generateShiftCheck(const Span& sp, MIRBinOp op, const HIRTypeData* tyL, const MIRParam& valR, const HIRTypeData* tyR) {
            const auto rhsType = tyR->as_Primitive();
            const auto compareType = unsignedIntegerType(rhsType);
            MIRParam compareValue = valR.clone();
            if (compareType != rhsType) {
                auto rhsLvalue = builder.lvalueOrTemp(sp, tyR, paramRvalue(valR.clone()));
                auto castValue = builder.newTemporary(builder.resolve().crate.types.primitive(compareType));
                builder.pushStmtAssign(sp, castValue.clone(), MIRRValue::make_Cast({mv$(rhsLvalue), builder.resolve().crate.types.primitive(compareType)}));
                compareValue = MIRParam(mv$(castValue));
            }
            auto inRange = builder.newTemporary(builder.resolve().crate.types.primitive(HIRCoreType::Bool));
            builder.pushStmtAssign(sp, inRange.clone(), MIRRValue::make_BinOp({
                mv$(compareValue),
                MIRBinOp::LT,
                MIRParam(integerConstant(compareType, U128(integerBits(tyL->as_Primitive())))),
            }));
            panicIf(
                sp,
                mv$(inRange),
                false,
                op == MIRBinOp::BIT_SHL ? "panic_const_shl_overflow" : "panic_const_shr_overflow"
            );
        }

        MIRParam maskShiftAmount(const Span& sp, const HIRTypeData* tyL, MIRParam valR, const HIRTypeData* tyR) {
            const auto rhsType = tyR->as_Primitive();
            auto masked = builder.newTemporary(tyR);
            builder.pushStmtAssign(sp, masked.clone(), MIRRValue::make_BinOp({
                mv$(valR),
                MIRBinOp::BIT_AND,
                MIRParam(integerConstant(rhsType, U128(integerBits(tyL->as_Primitive()) - 1))),
            }));
            return MIRParam(mv$(masked));
        }

        void generateCheckedBinop(const Span& sp, MIRLValue resSlot, MIRBinOp op, MIRParam valL, const HIRTypeData* tyL, MIRParam valR, const HIRTypeData* tyR, bool updateDestState = true) {
            switch (op) {
                case MIRBinOp::EQ:
                case MIRBinOp::NE:
                case MIRBinOp::LT:
                case MIRBinOp::LE:
                case MIRBinOp::GT:
                case MIRBinOp::GE:
                    ASSERT_BUG(sp, tyL == tyR, "Types in comparison operators must be equal - " << tyL << " != " << tyR);
                    // Defensive assert that the type is a valid MIR comparison
                switch ((*tyL).tag()) {
default:
                    BUG(sp, "Invalid type in comparison - " << tyL);
                    case HIRTypeData::TAG_Pointer: {
                        // Valid
                        break;
                    }
                    case HIRTypeData::TAG_Primitive: {
                        auto& e = (*tyL).as_Primitive();
                        if (e == HIRCoreType::Str) {
                            BUG(sp, "Invalid type in comparison - " << tyL);
                        }
                        break;
                    }
                }
                builder.pushStmtAssign(sp, mv$(resSlot), MIRRValue::make_BinOp({ mv$(valL), op, mv$(valR) }), updateDestState);
                break;
            // Bitwise masking operations: Require equal integer types or bool
            case MIRBinOp::BIT_XOR:
            case MIRBinOp::BIT_OR :
            case MIRBinOp::BIT_AND:
                ASSERT_BUG(sp, tyL == tyR, "Types in bitwise operators must be equal - " << tyL << " != " << tyR);
                ASSERT_BUG(sp, tyL->is_Primitive(), "Only primitives allowed in bitwise operators");
                switch(tyL->as_Primitive())
                {
                        case HIRCoreType::Str:
                        case HIRCoreType::Char:
                        case HIRCoreType::F32:
                        case HIRCoreType::F64:
                            BUG(sp, "Invalid type for bitwise operator - " << tyL);
                        default:
                            break;
                }
                builder.pushStmtAssign(sp, mv$(resSlot), MIRRValue::make_BinOp({ mv$(valL), op, mv$(valR) }), updateDestState);
                break;
            case MIRBinOp::ADD:    case MIRBinOp::ADD_OV:
            case MIRBinOp::SUB:    case MIRBinOp::SUB_OV:
            case MIRBinOp::MUL:    case MIRBinOp::MUL_OV:
            case MIRBinOp::DIV:    case MIRBinOp::DIV_OV:
            case MIRBinOp::MOD:
                ASSERT_BUG(sp, tyL == tyR, "Types in arithmatic operators must be equal - " << tyL << " != " << tyR);
                ASSERT_BUG(sp, tyL->is_Primitive(), "Only primitives allowed in arithmatic operators");
                switch(tyL->as_Primitive())
                {
                        case HIRCoreType::Str:
                        case HIRCoreType::Char:
                        case HIRCoreType::Bool:
                            BUG(sp, "Invalid type for arithmatic operator - " << tyL);
                        default:
                            break;
                }
                if (tyL->is_Primitive() && isInteger(tyL->as_Primitive())) {
                    if (op == MIRBinOp::DIV || op == MIRBinOp::MOD) {
                        generateDivisionChecks(sp, op, valL, tyL->as_Primitive(), valR);
                    } else if (builder.resolve().wb.settings->overflowChecks) {
                        switch (op) {
                            case MIRBinOp::ADD:
                                generateOverflowingArithmetic(sp, mv$(resSlot), "add_with_overflow", "panic_const_add_overflow", mv$(valL), tyL, mv$(valR), updateDestState);
                                return;
                            case MIRBinOp::SUB:
                                generateOverflowingArithmetic(sp, mv$(resSlot), "sub_with_overflow", "panic_const_sub_overflow", mv$(valL), tyL, mv$(valR), updateDestState);
                                return;
                            case MIRBinOp::MUL:
                                generateOverflowingArithmetic(sp, mv$(resSlot), "mul_with_overflow", "panic_const_mul_overflow", mv$(valL), tyL, mv$(valR), updateDestState);
                                return;
                            default:
                                break;
                        }
                    }
                }
                builder.pushStmtAssign(sp, mv$(resSlot), MIRRValue::make_BinOp({ mv$(valL), op, mv$(valR) }), updateDestState);
                break;
            case MIRBinOp::BIT_SHL:
            case MIRBinOp::BIT_SHR:
                ;
                ASSERT_BUG(sp, tyL->is_Primitive(), "Only primitives allowed in arithmatic operators");
                ASSERT_BUG(sp, tyR->is_Primitive(), "Only primitives allowed in arithmatic operators");
                switch(tyL->as_Primitive())
                {
                        case HIRCoreType::Str:
                        case HIRCoreType::Char:
                        case HIRCoreType::F32:
                        case HIRCoreType::F64:
                            BUG(sp, "Invalid type for shift op-assignment - " << tyL);
                        default:
                            break;
                }
                switch(tyR->as_Primitive())
                {
                        case HIRCoreType::Str:
                        case HIRCoreType::Char:
                        case HIRCoreType::F32:
                        case HIRCoreType::F64:
                            BUG(sp, "Invalid type for shift op-assignment - " << tyR);
                        default:
                            break;
                }
                if (builder.resolve().wb.settings->overflowChecks) {
                    generateShiftCheck(sp, op, tyL, valR, tyR);
                } else {
                    valR = maskShiftAmount(sp, tyL, mv$(valR), tyR);
                }
                builder.pushStmtAssign(sp, mv$(resSlot), MIRRValue::make_BinOp({ mv$(valL), op, mv$(valR) }), updateDestState);
                break;
            }
        }

        void visit(HIRExprNodeAssign& node) override {
            TRACE_FUNCTION_F("_Assign");
            const auto& sp = node.span();
            auto _ = disableBorrowExtension(); // A bit of a hack
            const auto& tySlot = node.slot->resType;
            const auto& tyVal = node.value->resType;

            this->visitNodePtr(node.value);
            if (!builder.blockActive() || !builder.hasResult()) {
                return;
            }
            MIRRValue val = builder.getResult(sp);
            auto rhs = builder.newTemporary(tyVal);
            builder.pushStmtAssign(node.value->span(), rhs.clone(), mv$(val));

            this->visitNodePtr(node.slot);
            auto dstRes = builder.getResult(sp);
            if (!dstRes.is_Use()) {
                // `S = S;` names a unit struct or variant on the left. Such a
                // pattern matches and holds nothing, so there is nowhere for the
                // value to go -- keep it in a temporary for its effects.
                ASSERT_BUG(sp, node.op == HIRExprNodeAssign::Op::None, "Assignment operator on a value that is not a place");
                auto tmp = builder.newTemporary(tySlot);
                builder.pushStmtAssign(node.slot->span(), mv$(tmp), mv$(dstRes));
                builder.setResult(node.span(), MIRRValue::make_Tuple({}));
                return;
            }
            auto dst = mv$(dstRes.as_Use());

            if (node.op != HIRExprNodeAssign::Op::None) {
                auto dstClone = dst.clone();
                MIRParam valP = mv$(rhs);

                ASSERT_BUG(sp, tySlot->is_Primitive(), "Assignment operator overloads are only valid on primitives - ty_slot=" << tySlot);
                ASSERT_BUG(sp, tyVal->is_Primitive(), "Assignment operator overloads are only valid on primitives - ty_val=" << tyVal);

#define _(v) HIRExprNodeAssign::Op::v
                MIRBinOp op;
                switch (node.op) {
                    case _(None):
                        throw "";
                    case _(Add):
                        op = MIRBinOp::ADD;
                        if (0) {
                            case _(Sub):
                                op = MIRBinOp::SUB;
                        }
                        if (0) {
                            case _(Mul):
                                op = MIRBinOp::MUL;
                        }
                        if (0) {
                            case _(Div):
                                op = MIRBinOp::DIV;
                        }
                        if (0) {
                            case _(Mod):
                                op = MIRBinOp::MOD;
                        }
                        this->generateCheckedBinop(sp, mv$(dst), op, mv$(dstClone), tySlot, mv$(valP), tyVal, false);
                        break;
                    case _(Xor):
                        op = MIRBinOp::BIT_XOR;
                        if (0) {
                            case _(Or):
                                op = MIRBinOp::BIT_OR;
                        }
                        if (0) {
                            case _(And):
                                op = MIRBinOp::BIT_AND;
                        }
                        this->generateCheckedBinop(sp, mv$(dst), op, mv$(dstClone), tySlot, mv$(valP), tyVal, false);
                        break;
                    case _(Shl):
                        op = MIRBinOp::BIT_SHL;
                        if (0) {
                            case _(Shr):
                                op = MIRBinOp::BIT_SHR;
                        }
                        this->generateCheckedBinop(sp, mv$(dst), op, mv$(dstClone), tySlot, mv$(valP), tyVal, false);
                        break;
                }
#undef _
            } else {
                ASSERT_BUG(sp, tySlot == tyVal || tySlot->equalsIgnoringRegions(tyVal), "Types must match for assignment - " << tySlot << " != " << tyVal);
                builder.pushStmtAssign(node.span(), mv$(dst), MIRRValue::make_Use(mv$(rhs)));
            }
            builder.setResult(node.span(), MIRRValue::make_Tuple({}));
        }

        void visit(HIRExprNodeBinOp& node) override {
            const auto& sp = node.span();
            TRACE_FUNCTION_F("_BinOp");

            const auto& tyL = node.left->resType;
            const auto& tyR = node.right->resType;
            auto res = builder.newTemporary(node.resType);

            // Short-circuiting boolean operations
            if (node.op == HIRExprNodeBinOp::Op::BoolAnd || node.op == HIRExprNodeBinOp::Op::BoolOr) {
                DEBUG("- ShortCircuit Left");
                this->visitNodePtr(node.left);
                if (!builder.blockActive()) {
                    return;
                }
                auto left = builder.getResultInLvalue(node.left->span(), tyL);

                auto bbNext = builder.newBbUnlinked();
                auto bbTrue = builder.newBbUnlinked();
                auto bbFalse = builder.newBbUnlinked();
                builder.endBlock(MIRTerminator::make_If({mv$(left), bbTrue, bbFalse}));

                // Generate a SplitScope to handle the conditional nature of the next code
                auto splitScope = builder.newScopeSplit(node.span());

                if (node.op == HIRExprNodeBinOp::Op::BoolOr) {
                    DEBUG("- ShortCircuit ||");
                    // If left is true, assign result true and return
                    builder.setCurBlock(bbTrue);
                    builder.pushStmtAssign(node.span(), res.clone(), MIRRValue(MIRConstant::make_Bool({true})));
                    builder.endSplitArm(node.left->span(), splitScope, /*reachable=*/true);
                    builder.endBlock(MIRTerminator::make_Goto(bbNext));

                    // If left is false, assign result to right
                    builder.setCurBlock(bbFalse);
                } else {
                    DEBUG("- ShortCircuit &&");
                    // If left is false, assign result false and return
                    builder.setCurBlock(bbFalse);
                    builder.pushStmtAssign(node.span(), res.clone(), MIRRValue(MIRConstant::make_Bool({false})));
                    builder.endSplitArm(node.left->span(), splitScope, /*reachable=*/true);
                    builder.endBlock(MIRTerminator::make_Goto(bbNext));

                    // If left is true, assign result to right
                    builder.setCurBlock(bbTrue);
                }

                DEBUG("- ShortCircuit Right");
                auto tmpScope = builder.newScopeTemp(node.right->span());
                this->visitNodePtr(node.right);
                if (!builder.blockActive()) {
                    builder.terminateScope(node.right->span(), mv$(tmpScope), false);
                    builder.endSplitArm(node.right->span(), splitScope, /*reachable=*/false);
                    builder.setCurBlock(bbNext);
                    builder.terminateScope(node.span(), mv$(splitScope));
                    builder.setResult(node.span(), mv$(res));
                    return;
                }
                builder.pushStmtAssign(node.span(), res.clone(), builder.getResult(node.right->span()));
                builder.terminateScope(node.right->span(), mv$(tmpScope));

                builder.endSplitArm(node.right->span(), splitScope, /*reachable=*/true);
                builder.endBlock(MIRTerminator::make_Goto(bbNext));

                builder.setCurBlock(bbNext);
                builder.terminateScope(node.span(), mv$(splitScope));
                builder.setResult(node.span(), mv$(res));
                return;
            } else {
            }

            this->visitNodePtr(node.left);
            if (!builder.blockActive()) {
                return;
            }
            auto left = builder.getResultInParam(node.left->span(), tyL);
            this->visitNodePtr(node.right);
            if (!builder.blockActive()) {
                return;
            }
            auto right = builder.getResultInParam(node.right->span(), tyR);

            MIRBinOp op;
            switch (node.op) {
                case HIRExprNodeBinOp::Op::CmpEqu:
                    op = MIRBinOp::EQ;
                    if (0) {
                        case HIRExprNodeBinOp::Op::CmpNEqu:
                            op = MIRBinOp::NE;
                    }
                    if (0) {
                        case HIRExprNodeBinOp::Op::CmpLt:
                            op = MIRBinOp::LT;
                    }
                    if (0) {
                        case HIRExprNodeBinOp::Op::CmpLtE:
                            op = MIRBinOp::LE;
                    }
                    if (0) {
                        case HIRExprNodeBinOp::Op::CmpGt:
                            op = MIRBinOp::GT;
                    }
                    if (0) {
                        case HIRExprNodeBinOp::Op::CmpGtE:
                            op = MIRBinOp::GE;
                    }
                    this->generateCheckedBinop(sp, res.clone(), op, mv$(left), tyL, mv$(right), tyR);
                    break;

                case HIRExprNodeBinOp::Op::Xor:
                    op = MIRBinOp::BIT_XOR;
                    if (0) {
                        case HIRExprNodeBinOp::Op::Or:
                            op = MIRBinOp::BIT_OR;
                    }
                    if (0) {
                        case HIRExprNodeBinOp::Op::And:
                            op = MIRBinOp::BIT_AND;
                    }
                    this->generateCheckedBinop(sp, res.clone(), op, mv$(left), tyL, mv$(right), tyR);
                    break;

                case HIRExprNodeBinOp::Op::Shr:
                    op = MIRBinOp::BIT_SHR;
                    if (0) {
                        case HIRExprNodeBinOp::Op::Shl:
                            op = MIRBinOp::BIT_SHL;
                    }
                    this->generateCheckedBinop(sp, res.clone(), op, mv$(left), tyL, mv$(right), tyR);
                    break;

                case HIRExprNodeBinOp::Op::Add:
                    op = MIRBinOp::ADD;
                    if (0) {
                        case HIRExprNodeBinOp::Op::Sub:
                            op = MIRBinOp::SUB;
                    }
                    if (0) {
                        case HIRExprNodeBinOp::Op::Mul:
                            op = MIRBinOp::MUL;
                    }
                    if (0) {
                        case HIRExprNodeBinOp::Op::Div:
                            op = MIRBinOp::DIV;
                    }
                    if (0) {
                        case HIRExprNodeBinOp::Op::Mod:
                            op = MIRBinOp::MOD;
                    }
                    this->generateCheckedBinop(sp, res.clone(), op, mv$(left), tyL, mv$(right), tyR);
                    break;

                // Short-circuiting boolean operations
                case HIRExprNodeBinOp::Op::BoolAnd:
                case HIRExprNodeBinOp::Op::BoolOr:
                    BUG(node.span(), "");
                    break;
            }
            builder.setResult(node.span(), mv$(res));
        }

        void visit(HIRExprNodeUniOp& node) override {
            TRACE_FUNCTION_F("_UniOp");

            const auto& tyVal = node.value->resType;
            this->visitNodePtr(node.value);
            if (!builder.blockActive()) {
                return;
            }
            auto val = builder.getResultInLvalue(node.value->span(), tyVal);

            MIRRValue res;
            switch (node.op) {
                case HIRExprNodeUniOp::Op::Invert:
                    if (tyVal->is_Primitive()) {
                        switch (tyVal->as_Primitive()) {
                            case HIRCoreType::Str:
                            case HIRCoreType::Char:
                            case HIRCoreType::F32:
                            case HIRCoreType::F64:
                                BUG(node.span(), "`!` operator on invalid type - " << tyVal);
                                break;
                            default:
                                break;
                        }
                    } else {
                        BUG(node.span(), "`!` operator on invalid type - " << tyVal);
                    }
                    res = MIRRValue::make_UniOp({mv$(val), MIRUniOp::INV});
                    break;
                case HIRExprNodeUniOp::Op::Negate:
                    if (tyVal->is_Primitive()) {
                        switch (tyVal->as_Primitive()) {
                            case HIRCoreType::Str:
                            case HIRCoreType::Char:
                            case HIRCoreType::Bool:
                                BUG(node.span(), "`-` operator on invalid type - " << tyVal);
                                break;
                            case HIRCoreType::U8:
                            case HIRCoreType::U16:
                            case HIRCoreType::U32:
                            case HIRCoreType::U64:
                            case HIRCoreType::U128:
                            case HIRCoreType::Usize:
                                BUG(node.span(), "`-` operator on unsigned integer - " << tyVal);
                                break;
                            default:
                                break;
                        }
                    } else {
                        BUG(node.span(), "`!` operator on invalid type - " << tyVal);
                    }
                    if (builder.resolve().wb.settings->overflowChecks
                        && tyVal->is_Primitive()
                        && isSignedInteger(tyVal->as_Primitive())) {
                        const auto type = tyVal->as_Primitive();
                        auto overflows = builder.newTemporary(builder.resolve().crate.types.primitive(HIRCoreType::Bool));
                        builder.pushStmtAssign(node.span(), overflows.clone(), MIRRValue::make_BinOp({
                            MIRParam(val.clone()),
                            MIRBinOp::EQ,
                            MIRParam(integerConstant(type, U128(1) << (integerBits(type) - 1))),
                        }));
                        panicIf(node.span(), mv$(overflows), true, "panic_const_neg_overflow");
                    }
                    res = MIRRValue::make_UniOp({mv$(val), MIRUniOp::NEG});
                    break;
            }
            builder.setResult(node.span(), mv$(res));
        }

        void visit(HIRExprNodeBorrow& node) override {
            TRACE_FUNCTION_F("_Borrow");

            auto _ = saveAndEdit(inBorrow, true);

            const auto& tyVal = node.value->resType;
            this->visitNodePtr(node.value);
            if (!builder.blockActive()) {
                return;
            }
            auto val = builder.getResultInLvalue(node.value->span(), tyVal);

            if (borrowRaiseTarget) {
                DEBUG("- Raising borrow to scope " << *borrowRaiseTarget);
                builder.raiseTemporaries(node.span(), val, *borrowRaiseTarget);
            }

            builder.setResult(node.span(), MIRRValue::make_Borrow({node.type, false, mv$(val)}));
        }

        void visit(HIRExprNodeRawBorrow& node) override {
            TRACE_FUNCTION_F("_RawBorrow");

            auto _ = saveAndEdit(inBorrow, true);

            const auto& tyVal = node.value->resType;
            this->visitNodePtr(node.value);
            if (!builder.blockActive()) {
                return;
            }
            auto val = builder.getResultInLvalue(node.value->span(), tyVal);

            if (borrowRaiseTarget) {
                DEBUG("- Raising borrow to scope " << *borrowRaiseTarget);
                builder.raiseTemporaries(node.span(), val, *borrowRaiseTarget);
            }

            builder.setResult(node.span(), MIRRValue::make_Borrow({node.type, true, mv$(val)}));
        }

        void visit(HIRExprNodeCast& node) override {
            TRACE_FUNCTION_F("_Cast " << node.resType);
            this->visitNodePtr(node.value);
            if (!builder.blockActive()) {
                return;
            }

            const auto& tyOut = node.resType;
            const auto& tyIn = node.value->resType;

            // A cast produces a new value even when the runtime type does not
            // change: `&(a as i32)` borrows a copy, not `a` itself.
            if (tyOut == tyIn || tyOut->equalsIgnoringRegions(tyIn)) {
                auto sameVal = builder.getResultInLvalue(node.value->span(), node.value->resType);
                auto sameRes = builder.newTemporary(node.resType);
                builder.pushStmtAssign(node.span(), sameRes.clone(), MIRRValue::make_Use(mv$(sameVal)));
                builder.setResult(node.span(), mv$(sameRes));
                return;
            }

            auto val = builder.getResultInLvalue(node.value->span(), node.value->resType);

            // `!` coerces to every destination, including through explicit
            // cast syntax.  Other source types still need validation here.
            if (!tyIn->is_Diverge()) {
            switch ((*tyOut).tag()) {
default:
                BUG(node.span(), "Invalid cast to " << tyOut << " from " << tyIn);
                case HIRTypeData::TAG_Function: {
                    auto& de = (*tyOut).as_Function();
                    // Just trust the previous stages.
                    if (tyIn->is_Function()) {
                        ASSERT_BUG(node.span(), de.argTypes == tyIn->as_Function().argTypes, tyIn);
                    } else if (tyIn->is_NamedFunction()) {
                        // TODO: Extra checks?
                    } else {
                        BUG(node.span(), "_Cast from bad type: " << tyIn);
                    }
                    break;
                }
                case HIRTypeData::TAG_Pointer: {
                    auto& de = (*tyOut).as_Pointer();
                    if (tyIn->is_Primitive()) {
                        const auto& ie = tyIn->as_Primitive();
                        switch (ie) {
                            case HIRCoreType::Bool:
                            case HIRCoreType::Char:
                            case HIRCoreType::Str:
                            case HIRCoreType::F32:
                            case HIRCoreType::F64:
                                BUG(node.span(), "Cannot cast to pointer from " << tyIn);
                            default:
                                break;
                        }
                        // TODO: Only valid if T: Sized in *{const/mut/move} T
                    } else if (const auto* se = tyIn->opt_Borrow()) {
                        if (de.inner != se->inner && !de.inner->equalsIgnoringRegions(se->inner)) {
                            BUG(node.span(), "Cannot cast to " << tyOut << " from " << tyIn);
                        }
                        // Valid
                    } else if (tyIn->is_Function() || tyIn->is_NamedFunction()) {
                        if (!builder.resolve().typeIsSized(node.span(), de.inner)) {
                            BUG(node.span(), "Cannot cast to " << tyOut << " from " << tyIn);
                        }
                        // Valid
                    } else if (const auto* se = tyIn->opt_Pointer()) {
                        // Valid
                        if (se->inner == de.inner) {
                        }
                        // - If making a fat pointer from thin, convert to _Unsize
                        else if (builder.resolve().canUnsize(node.span(), de.inner, se->inner)) {
                            builder.setResult(node.span(), MIRRValue::make_MakeDst({mv$(val), MIRConstant::make_ItemAddr({})}));
                            auto tmpTy = builder.resolve().crate.types.pointer(se->type, de.inner);
                            val = builder.getResultInLvalue(node.value->span(), tmpTy);
                        }
                    } else {
                        BUG(node.span(), "Cannot cast to pointer from " << tyIn);
                    }
                    break;
                }
                case HIRTypeData::TAG_Primitive: {
                    auto& de = (*tyOut).as_Primitive();
                    switch (de) {
                        case HIRCoreType::Str:
                            BUG(node.span(), "Cannot cast to str");
                            break;
                        case HIRCoreType::Char:
                            if (tyIn->is_Primitive() && tyIn->as_Primitive() == HIRCoreType::U8) {
                                // Valid
                            } else {
                                BUG(node.span(), "Cannot cast to char from " << tyIn);
                            }
                            break;
                        case HIRCoreType::Bool:
                            BUG(node.span(), "Cannot cast to bool");
                            break;
                        case HIRCoreType::F32:
                        case HIRCoreType::F64:
                            if (tyIn->is_Primitive()) {
                                switch (de) {
                                    case HIRCoreType::Str:
                                    case HIRCoreType::Char:
                                    case HIRCoreType::Bool:
                                        BUG(node.span(), "Cannot cast to " << tyOut << " from " << tyIn);
                                        break;
                                    default:
                                        // Valid
                                        break;
                                }
                            } else {
                                BUG(node.span(), "Cannot cast to " << tyOut << " from " << tyIn);
                            }
                            break;
                        default:
                            if (tyIn->opt_Primitive()) {
                                switch (de) {
                                    case HIRCoreType::Str:
                                        BUG(node.span(), "Cannot cast to " << tyOut << " from " << tyIn);
                                    default:
                                        // Valid
                                        break;
                                }
                            } else if (const auto* se = tyIn->opt_Path()) {
                                if (se->binding.is_Enum()) {
                                    // TODO: Check if it's a repr(ty/C) enum - and if the type matches
                                } else {
                                    BUG(node.span(), "Cannot cast to " << tyOut << " from " << tyIn);
                                }
                            }
                            // NOTE: Valid for all integer types
                            else if (tyIn->is_Pointer()) {
                                // TODO: Only valid for T: Sized?
                            } else if (tyIn->is_Function() || tyIn->is_NamedFunction()) {
                                // Function pointers and function items can be
                                // cast to any integer type.
                            } else {
                                BUG(node.span(), "Cannot cast to " << tyOut << " from " << tyIn);
                            }
                            break;
                    }
                    break;
                }
            }
            }
            auto res = builder.newTemporary(node.resType);
            builder.pushStmtAssign(node.span(), res.clone(), MIRRValue::make_Cast({ mv$(val), node.resType }));
            builder.setResult( node.span(), mv$(res) );
        }

        void visit(HIRExprNodeUnsize& node) override {
            TRACE_FUNCTION_F("_Unsize");
            this->visitNodePtr(node.value);

            const auto& tyOut = node.resType;
            const auto& tyIn = node.value->resType;

            if (tyOut == tyIn) {
                return;
            }

            auto ptrLval = builder.getResultInLvalue(node.value->span(), node.value->resType);

            if (tyOut->is_Borrow() && tyIn->is_Borrow()) {
                const auto& oe = tyOut->as_Borrow();
                const auto& ie = tyIn->as_Borrow();
                const auto& tyOut = oe.inner;
                const auto& tyIn = ie.inner;
                switch ((*tyOut).tag()) {
default: {
                        const auto& langUnsize = builder.crate().getLangItemPath(node.span(), "unsize");
                        if (builder.resolve().findImpl(node.span(), langUnsize, HIRPathParams(tyOut), tyIn, [](auto, bool) {
                            return true;
                        })) {
                            // - HACK: Emit a cast operation on the pointers. Leave it up to monomorph to 'fix' it
                            builder.setResult(node.span(), MIRRValue::make_MakeDst({mv$(ptrLval), MIRConstant::make_ItemAddr({})}));
                        } else {
                            // Probably an error?
                            builder.setResult(node.span(), MIRRValue::make_MakeDst({mv$(ptrLval), MIRConstant::make_ItemAddr({})}));
                            //TODO(node.span(), "MIR _Unsize to " << ty_out);
                        }
                    }
                    break;
                    case HIRTypeData::TAG_Slice: {
                        if (tyIn->is_Array()) {
                            const auto& inArray = tyIn->as_Array();
                            MIRConstant sizeVal;
                        switch (inArray.size.tag()) {
                            case HIRArraySize::TAG_Unevaluated: {
                                auto& se = inArray.size.as_Unevaluated();
                                switch (se.tag()) {
default:
                                    // Preserve array-to-slice unsizing until the const
                                    // expression is evaluated during monomorphisation.
                                    // MIR cleanup recognises an empty ItemAddr as the
                                    // unsize pseudo-op and supplies the concrete length.
                                    sizeVal = MIRConstant::make_ItemAddr({});
                                    break;
                                    case HIRConstGeneric::TAG_Generic: {
                                        auto& cge = se.as_Generic();
                                        sizeVal = cge;
                                        break;
                                    }
                                }
                                break;
                            }
                            case HIRArraySize::TAG_Known: {
                                auto& se = inArray.size.as_Known();
                                sizeVal = MIRConstant::make_Uint({U128(se), HIRCoreType::Usize});
                                break;
                            }
                        }
                        builder.setResult( node.span(), MIRRValue::make_MakeDst({ mv$(ptrLval), mv$(sizeVal) }) );
                        } else if (tyIn->is_Generic() || (tyIn->is_Path() && tyIn->as_Path().binding.is_Opaque())) {
                            // The source is thin here: its concrete array length becomes
                            // available only after monomorphisation. Preserve the unsize
                            // sentinel for MIR cleanup instead of reading nonexistent metadata.
                            builder.setResult(node.span(), MIRRValue::make_MakeDst({mv$(ptrLval), MIRConstant::make_ItemAddr({})}));
                        } else {
                            ASSERT_BUG(node.span(), tyIn->is_Array(), "Unsize to slice from non-array - " << tyIn);
                        }
                        break;
                    }
                    case HIRTypeData::TAG_TraitObject: {
                        // NOTE: This pattern (an empty ItemAddr) is detected by cleanup, which populates the vtable properly
                        builder.setResult(node.span(), MIRRValue::make_MakeDst({mv$(ptrLval), MIRConstant::make_ItemAddr({})}));
                        break;
                    }
                }
            } else {
                // NOTES: (from IRC: eddyb)
                // < eddyb> they're required that T and U are the same struct definition (with different type parameters) and exactly one field differs in type between T and U (ignoring PhantomData)
                // < eddyb> Mutabah: I forgot to mention that the field that differs in type must also impl CoerceUnsized

                // TODO: Just emit a cast and leave magic handling to codegen
                // - This code _could_ do inspection of the types and insert a destructure+unsize+restructure, but that does't handle direct `T: CoerceUnsize<U>`
                builder.setResult(node.span(), MIRRValue::make_MakeDst({mv$(ptrLval), MIRConstant::make_ItemAddr({})}));
            }
        }

        void visitIndexOperator(HIRExprNodeIndex& node, const HIRTypeData* tyVal, MIRLValue value, const HIRTypeData* tyIdx, MIRLValue index) {
            DEBUG("");
            const Span& sp = node.span();

            // NOTE: Do operator replacement here after handling scope-raising for _Borrow
            if (borrowRaiseTarget && inBorrow) {
                DEBUG("- Raising deref in borrow to scope " << *borrowRaiseTarget);
                builder.raiseTemporaries(sp, value, *borrowRaiseTarget);
            }

            const char* langitem = nullptr;
            const char* method = nullptr;
            HIRBorrowType bt;
            switch (node.value->usage) {
                case HIRValueUsage::Unknown:
                    BUG(sp, "Usage of index reciever is still `Unknown`");
                    break;
                case HIRValueUsage::Borrow:
                    bt = HIRBorrowType::Shared;
                    langitem = method = "index";
                    break;
                case HIRValueUsage::Mutate:
                    bt = HIRBorrowType::Unique;
                    langitem = method = "index_mut";
                    break;
                case HIRValueUsage::Move:
                    TODO(sp, "Support moving out of indexed values");
                    break;
            }
            // Needs replacement, continue
            assert(langitem);
            assert(method);

            // - Construct trait path - Index*<IdxTy>
            HIRPathParams ppTrait;
            ppTrait.types.push_back(tyIdx);
            HIRGenericPath trait{builder.resolve().crate.getLangItemPath(node.span(), langitem), std::move(ppTrait)};

            HIRPathParams ppMethod;
            auto methodPath = HIRPath(tyVal, std::move(trait), RcString::newInterned(method), std::move(ppMethod));

            // Store a borrow of the input value
            ::std::vector<MIRParam> args;
            args.push_back(builder.lvalueOrTemp(sp, builder.resolve().crate.types.borrow(bt, node.value->resType), MIRRValue::make_Borrow({bt, false, std::move(value)})));
            args.push_back(std::move(index));
            builder.movedLvalue(node.span(), args[0].as_LValue());
            builder.movedLvalue(node.span(), args[1].as_LValue());
            auto resVal = builder.newTemporary(builder.resolve().crate.types.borrow(bt, node.resType));
            // Call the above trait method
            // Store result of that call in `val` (which will be derefed below)
            auto okBlock = builder.newBbUnlinked();
            auto panicBlock = builder.newBbUnlinked();
            builder.endBlock(MIRTerminator::make_Call({okBlock, MIRUnwindAction::make_Cleanup(panicBlock), resVal.clone(), std::move(methodPath), std::move(args), SourceLocation(node.span())}));
            builder.setCurBlock(panicBlock);
            emitUnwind(sp);

            builder.setCurBlock(okBlock);
            builder.setResult(node.span(), MIRLValue::newDeref(std::move(resVal)));
        }

        void visit(HIRExprNodeIndex& node) override {
            TRACE_FUNCTION_F("_Index");

            // NOTE: Calculate the index first (so if it borrows from the source, it's over by the time that's needed)
            const auto& tyIdx = node.index->resType;
            this->visitNodePtr(node.index);
            if (!builder.blockActive()) {
                return;
            }
            auto index = builder.getResultInLvalue(node.index->span(), tyIdx);

            const auto& tyVal = node.value->resType;
            this->visitNodePtr(node.value);
            if (!builder.blockActive()) {
                return;
            }
            auto value = builder.getResultInLvalue(node.value->span(), tyVal);

            if (tyIdx != HIRCoreType::Usize) {
                DEBUG("non-usize index");
                visitIndexOperator(node, tyVal, std::move(value), tyIdx, std::move(index));
                return;
            }

            MIRRValue limitVal;
            switch ((*tyVal).tag()) {
default:
                DEBUG("non-builtin type");
                visitIndexOperator(node, tyVal, std::move(value), tyIdx, std::move(index));
                return;
                case HIRTypeData::TAG_Array: {
                    auto& e = (*tyVal).as_Array();
                    switch (e.size.tag()) {
                        case HIRArraySize::TAG_Unevaluated: {
                            auto& se = e.size.as_Unevaluated();
                            if (se.is_Generic()) {
                                limitVal = MIRConstant::make_Generic(se.as_Generic());
                                break;
                            }
                            // The concrete length of a generic array is known after
                            // monomorphisation. DstMeta represents that length in MIR
                            // without forcing an unevaluated const expression here.
                            limitVal = MIRRValue::make_DstMeta({value.clone()});
                            break;
                        }
                        case HIRArraySize::TAG_Known: {
                            auto& se = e.size.as_Known();
                            limitVal = MIRConstant::make_Uint({U128(se), HIRCoreType::Usize});
                            break;
                        }
                    }
                    break;
                }
                case HIRTypeData::TAG_Slice: {
                    limitVal = MIRRValue::make_DstMeta({builder.getPtrToDst(node.value->span(), value)});
                    break;
                }
            }

            {
                auto limitLval = builder.lvalueOrTemp(node.span(), tyIdx, mv$(limitVal));

                auto cmpRes = builder.newTemporary(builder.resolve().crate.types.primitive(HIRCoreType::Bool));
                builder.pushStmtAssign(node.span(), cmpRes.clone(), MIRRValue::make_BinOp({index.clone(), MIRBinOp::GE, limitLval.clone()}));
                auto armPanic = builder.newBbUnlinked();
                auto armContinue = builder.newBbUnlinked();
                builder.endBlock(MIRTerminator::make_If({mv$(cmpRes), armPanic, armContinue}));

                builder.setCurBlock(armPanic);
                const auto& panicBoundsCheck = builder.crate().getLangItemPath(node.span(), "panic_bounds_check");
                auto panicResult = builder.newTemporary(builder.resolve().crate.types.diverge());
                auto panicReturn = builder.newBbUnlinked();
                auto panicUnwind = builder.newBbUnlinked();
                builder.endBlock(
                    MIRTerminator::make_Call({
                        panicReturn,
                        MIRUnwindAction::make_Cleanup(panicUnwind),
                        std::move(panicResult),
                        HIRPath(panicBoundsCheck),
                        makeVec2<MIRParam>(index.clone(), limitLval.clone()),
                    })
                );

                builder.setCurBlock(panicReturn);
                builder.endBlock(MIRTerminator::make_Unreachable({}));

                builder.setCurBlock(panicUnwind);
                emitUnwind(node.span());

                builder.setCurBlock(armContinue);
            }

            if( !index.is_Local())
            {
                auto localIdx = builder.newTemporary(builder.resolve().crate.types.primitive(HIRCoreType::Usize));
                builder.pushStmtAssign(node.span(), localIdx.clone(), mv$(index));
                index = mv$(localIdx);
            }
            builder.setResult( node.span(), MIRLValue::newIndex( mv$(value), index.root.as_Local() ) );
        }

        void visit(HIRExprNodeDeref& node) override {
            const Span& sp = node.span();
            TRACE_FUNCTION_F("_Deref");

            const auto& tyVal = node.value->resType;
            this->visitNodePtr(node.value);

            bool useTrait = node.traitUsed == HIRExprNodeDeref::TraitUsed::Trait;
            if (node.traitUsed == HIRExprNodeDeref::TraitUsed::Unknown) {
                useTrait = !tyVal->is_Pointer() && !tyVal->is_Borrow() && !builder.isTypeOwnedBox(tyVal);
            }

            auto value = builder.getResult(node.value->span());
            if (!useTrait) {
                if (auto* borrow = value.opt_Borrow()) {
                    // Preserve the original place for drop-state tracking instead of
                    // hiding *(&place) behind a materialised reference temporary.
                    builder.setResult(node.span(), MIRRValue::make_Use(mv$(borrow->val)));
                    return;
                }
            }
            auto val = builder.lvalueOrTemp(node.value->span(), tyVal, mv$(value));

            if (useTrait) {
                // Do operator replacement here after handling scope-raising
                // for _Borrow.  The type checker recorded this choice, so a
                // primitive reference can still dispatch to a user impl.
                if (borrowRaiseTarget && inBorrow) {
                    DEBUG("- Raising deref in borrow to scope " << *borrowRaiseTarget);
                    builder.raiseTemporaries(node.span(), val, *borrowRaiseTarget);
                }

                const char* langitem = nullptr;
                const char* method = nullptr;
                HIRBorrowType bt;
                // - Uses the value's usage beacuse for T: Copy node.m_value->m_usage is Borrow, but node.m_usage is Move
                switch (node.value->usage) {
                    case HIRValueUsage::Unknown:
                        BUG(sp, "Unknown usage type of deref value - " << tyVal);
                        break;
                    case HIRValueUsage::Borrow:
                        bt = HIRBorrowType::Shared;
                        langitem = method = "deref";
                        break;
                    case HIRValueUsage::Mutate:
                        bt = HIRBorrowType::Unique;
                        langitem = method = "deref_mut";
                        break;
                    case HIRValueUsage::Move:
                        TODO(sp, "ValueUsage::Move for desugared Deref of " << node.value->resType);
                        break;
                }
                assert(langitem);
                assert(method);

                auto methodPath = HIRPath(tyVal, HIRGenericPath(builder.resolve().crate.getLangItemPath(node.span(), langitem), {}), method, HIRPathParams());

                ::std::vector<MIRParam> args;
                args.push_back(builder.lvalueOrTemp(sp, builder.resolve().crate.types.borrow(bt, node.value->resType), MIRRValue::make_Borrow({bt, false, mv$(val)})));
                builder.movedLvalue(node.span(), args[0].as_LValue());
                val = builder.newTemporary(builder.resolve().crate.types.borrow(bt, node.resType));
                auto okBlock = builder.newBbUnlinked();
                auto panicBlock = builder.newBbUnlinked();
                builder.endBlock(MIRTerminator::make_Call({okBlock, MIRUnwindAction::make_Cleanup(panicBlock), val.clone(), mv$(methodPath), mv$(args)}));
                builder.setCurBlock(panicBlock);
                emitUnwind(sp);

                builder.setCurBlock(okBlock);
            }

            builder.setResult(node.span(), MIRLValue::newDeref(mv$(val)));
        }

        void visit(HIRExprNodeEmplace& node) override {
            assert(node.type == HIRExprNodeEmplace::Type::Boxer);
            const auto& dataTy = node.value->resType;

            node.value->visit(*this);
            auto val = builder.getResult(node.span());

            return boxNew(node, dataTy, std::move(val));
        }

        void boxNew(HIRExprNode& node, const HIRTypeData* dataTy, MIRRValue val) {
            const auto& langExchangeMalloc = builder.crate().getLangItemPath(node.span(), "exchange_malloc");

            HIRPathParams traitParamsData;
            traitParamsData.types.push_back(dataTy);
            auto& types = builder.resolve().crate.types;

            // 1. Determine the size/alignment of the type
            MIRParam sizeParam, alignParam;
            size_t itemSize, itemAlign;
            if (TargetGetSizeAndAlignOf(node.span(), builder.resolve(), dataTy, itemSize, itemAlign)) {
                sizeParam = MIRConstant::make_Uint({U128(itemSize), HIRCoreType::Usize});
                alignParam = MIRConstant::make_Uint({U128(itemAlign), HIRCoreType::Usize});
            } else {
                // Insert calls to "size_of" and "align_of" intrinsics
                auto sizeSlot = builder.newTemporary(types.primitive(HIRCoreType::Usize));
                auto sizePanic = builder.newBbUnlinked();
                auto sizeOk = builder.newBbUnlinked();
                builder.endBlock(MIRTerminator::make_Call({sizeOk, MIRUnwindAction::make_Cleanup(sizePanic), sizeSlot.clone(), MIRCallTarget::make_Intrinsic({"size_of", traitParamsData.clone()}), {}}));
                builder.setCurBlock(sizePanic);
                emitUnwind(node.span());
                builder.setCurBlock(sizeOk);
                auto alignSlot = builder.newTemporary(types.primitive(HIRCoreType::Usize));
                auto alignPanic = builder.newBbUnlinked();
                auto alignOk = builder.newBbUnlinked();
                builder.endBlock(MIRTerminator::make_Call({alignOk, MIRUnwindAction::make_Cleanup(alignPanic), alignSlot.clone(), MIRCallTarget::make_Intrinsic({"align_of", traitParamsData.clone()}), {}}));
                builder.setCurBlock(alignPanic);
                emitUnwind(node.span());
                builder.setCurBlock(alignOk);

                sizeParam = ::std::move(sizeSlot);
                alignParam = ::std::move(alignSlot);
            }

            // 2. Call the allocator function and get a pointer
            // - NOTE: "exchange_malloc" returns a `*mut u8`, need to cast that to the target type
            auto placeRawType = types.pointer(HIRBorrowType::Unique, types.primitive(HIRCoreType::U8));
            auto placeRaw = builder.newTemporary(placeRawType);

            auto placePanic = builder.newBbUnlinked();
            auto placeOk = builder.newBbUnlinked();
            builder.endBlock(MIRTerminator::make_Call({placeOk, MIRUnwindAction::make_Cleanup(placePanic), placeRaw.clone(), HIRPath(langExchangeMalloc), makeVec2<MIRParam>(::std::move(sizeParam), ::std::move(alignParam))}));
            builder.setCurBlock(placePanic);
            emitUnwind(node.span());
            builder.setCurBlock(placeOk);

            auto placeType = types.pointer(HIRBorrowType::Unique, dataTy);
            auto place = builder.newTemporary(placeType);
            builder.pushStmtAssign(node.span(), place.clone(), MIRRValue::make_Cast({mv$(placeRaw), placeType}));
            // 3. Do a non-dropping write into the target location (i.e. just a MIR assignment)
            builder.pushStmtAssign(node.span(), MIRLValue::newDeref(place.clone()), mv$(val), /*drop_destination=*/false);
            // 4. Convert the pointer into an `owned_box`
            const auto& resType = node.resType;
            auto res = builder.newTemporary(resType);
            auto castPanic = builder.newBbUnlinked();
            auto castOk = builder.newBbUnlinked();
            HIRPathParams transmuteParams;
            transmuteParams.types.push_back(resType);
            transmuteParams.types.push_back(placeType);
            builder.endBlock(MIRTerminator::make_Call({castOk, MIRUnwindAction::make_Cleanup(castPanic), res.clone(), MIRCallTarget::make_Intrinsic({"transmute", mv$(transmuteParams)}), makeVec1(MIRParam(mv$(place)))}));
            builder.setCurBlock(castPanic);
            emitUnwind(node.span());
            builder.setCurBlock(castOk);

            builder.setResult(node.span(), mv$(res));
        }

        void visit(HIRExprNodeTupleVariant& node) override {
            const Span& sp = node.span();
            TRACE_FUNCTION_F("_TupleVariant");
            ::std::vector<MIRParam> values;
            values.reserve(node.args.size());
            for (auto& arg : node.args) {
                this->visitNodePtr(arg);
                if (!builder.blockActive()) {
                    return;
                }
                values.push_back(builder.getResultInParam(arg->span(), arg->resType));
            }

            if (node.isStruct) {
                builder.setResult(node.span(), MIRRValue::make_Struct({node.path.clone(), mv$(values)}));
            } else {
                // Get the variant index from the enum.
                auto enumPath = node.path.clone();
                const auto varName = enumPath.path.popComponent();
                const auto& enm = builder.crate().getEnumByPath(sp, enumPath.path);

                size_t idx = enm.findVariant(varName);
                ASSERT_BUG(sp, idx != SIZE_MAX, "Variant " << node.path.path << " isn't present");

                // TODO: Validation?
                ASSERT_BUG(sp, enm.data.is_Data(), "TupleVariant on non-data enum - " << node.path.path);

                builder.setResult(node.span(), MIRRValue::make_EnumVariant({mv$(enumPath), static_cast<unsigned>(idx), mv$(values)}));
            }
        }

        ::std::vector<MIRParam> getArgs(/*const*/ ::std::vector<HIRExprNodeP>& args) {
            ::std::vector<MIRParam> values;
            values.reserve(args.size());
            for (auto& arg : args) {
                this->visitNodePtr(arg);
                if (!builder.blockActive()) {
                    return {};
                } else if (args.size() == 1) {
                    values.push_back(builder.getResultInParam(arg->span(), arg->resType, /*allow_missing_value=*/true));
                } else {
                    auto res = builder.getResult(arg->span());
                    if (auto* e = res.opt_Constant()) {
                        values.push_back(mv$(*e));
                    } else {
                        // NOTE: Have to allocate a new temporary because ordering matters
                        auto tmp = builder.newTemporary(arg->resType);
                        builder.pushStmtAssign(arg->span(), tmp.clone(), mv$(res));
                        values.push_back(mv$(tmp));
                    }
                }
            }
            // Keep already evaluated arguments live while evaluating the remaining arguments.
            // A later argument can yield, so consuming an earlier temporary here would prevent
            // the coroutine lowering from saving a value that the eventual call still needs.
            for (size_t i = 0; i < values.size(); i++) {
                if (const auto* e = values[i].opt_LValue()) {
                    builder.movedLvalue(args[i]->span(), *e);
                }
            }
            return values;
        }

        void visit(HIRExprNodeCallPath& node) override {
            TRACE_FUNCTION_F("_CallPath " << node.path);
            // TODO: if this is a `<foo as Index[Mut]>::index[_mut]` call then allow the borrow raise to go through to the receiver
            ::std::vector<MIRParam> values;
            bool isOperator = false;
            if (const auto* pe = node.path.data.opt_UfcsKnown()) {
                if (pe->trait.path == builder.resolve().crate.getLangItemPathOpt("index")) {
                    isOperator = true;
                } else if (pe->trait.path == builder.resolve().crate.getLangItemPathOpt("index_mut")) {
                    isOperator = true;
                } else if (pe->trait.path == builder.resolve().crate.getLangItemPathOpt("deref")) {
                    isOperator = true;
                } else if (pe->trait.path == builder.resolve().crate.getLangItemPathOpt("deref_mut")) {
                    isOperator = true;
                }
            }
            if (isOperator) {
                values = getArgs(node.args);
            } else {
                auto _ = saveAndEdit(borrowRaiseTarget, nullptr);
                values = getArgs(node.args);
            }
            if (!builder.blockActive()) {
                return;
            }

            auto panicBlock = builder.newBbUnlinked();
            auto nextBlock = builder.newBbUnlinked();
            auto res = builder.newTemporary(node.resType);

            bool unconditionalDiverge = false;

            // Emit intrinsics as a special call type
            if (node.path.data.is_Generic()) {
                const auto& gpath = node.path.data.as_Generic();
                const auto& fcn = builder.crate().getFunctionByPath(node.span(), gpath.path);
                const auto& name = gpath.path.components().back();
                if (gpath.path.crateName() == "#intrinsics") {
                    if (name == "offset_of") {
                        builder.endBlock(MIRTerminator::make_Call({nextBlock, MIRUnwindAction::make_Cleanup(panicBlock), res.clone(), MIRCallTarget::make_Intrinsic({name, gpath.params.clone()}), mv$(values)}));
                    } else {
                        ERROR(node.span(), E0000, "Unknown builtin - " << gpath.path);
                    }
                // A layout intrinsic may carry a fallback body, but a backend
                // that knows it still replaces the call with its own answer.
                } else if (fcn.abi == "rust-intrinsic"
                    || (fcn.markings.isRustcIntrinsic
                        && (name == "vtable_size" || name == "vtable_align"))) {
                    if (name == "ptr_metadata") {
                        auto& v = values.front();
                        builder.pushStmtAssign(node.span(), res.clone(), MIRRValue::make_DstMeta({std::move(v.as_LValue())}));
                        builder.setResult(node.span(), std::move(res));
                        return;
                    }
                    // aggregate_raw_ptr: Lowers to trustme's MakeDst (rustc's `Aggregate` with `AggregateKind::RawPtr`)
                    if (name == "aggregate_raw_ptr") {
                        auto& vPtr = values.at(0);
                        auto& vMeta = values.at(1);
                        builder.pushStmtAssign(node.span(), res.clone(), MIRRValue::make_MakeDst({std::move(vPtr), std::move(vMeta)}));
                        builder.setResult(node.span(), std::move(res));
                        return;
                    }
                    if (name == "ub_checks") {
                        builder.setResult(node.span(), MIRConstant::make_Bool({true}));
                        return;
                    }
                    // `slice_get_unchecked`: Acts like `&mut foo[idx]`, but handles all inner types
                    if (name == "slice_get_unchecked") {
                        MIRLValue slot;
                        {
                            auto& tuMatch = values[0];
                            switch (tuMatch.tag()) {
                                case MIRParam::TAG_LValue: {
                                    auto& lv = tuMatch.as_LValue();
                                    slot = MIRLValue::newDeref(std::move(lv));
                                    break;
                                }
                                case MIRParam::TAG_Constant: {
                                    TODO(node.span(), "");
                                    break;
                                }
                                case MIRParam::TAG_Borrow: {
                                    auto& v = tuMatch.as_Borrow();
                                    slot = std::move(v.val);
                                    break;
                                }
                            }
                        }
                        MIRLValue   indexLv = builder.newTemporary(builder.resolve().crate.types.primitive(HIRCoreType::Usize));
                        {
                            auto& tuMatch = values[1];
                            switch (tuMatch.tag()) {
                                case MIRParam::TAG_LValue: {
                                    auto& lv = tuMatch.as_LValue();
                                    builder.pushStmtAssign(node.span(), indexLv.clone(), std::move(lv));
                                    break;
                                }
                                case MIRParam::TAG_Constant: {
                                    auto& c = tuMatch.as_Constant();
                                    builder.pushStmtAssign(node.span(), indexLv.clone(), std::move(c));
                                    break;
                                }
                                case MIRParam::TAG_Borrow: {
                                    TODO(node.span(), "Borrow index?");
                                    break;
                                }
                            }
                        }
                        const auto& ptrTy = gpath.params.types.at(0);
                        ASSERT_BUG(node.span(), ptrTy->is_Borrow() || ptrTy->is_Pointer(), "" << ptrTy);
                        bool isRaw = ptrTy->is_Pointer();
                        auto borrowTy = isRaw ? ptrTy->as_Pointer().type : ptrTy->as_Borrow().type;
                        builder.pushStmtAssign(node.span(), res.clone(), MIRRValue::make_Borrow({
                            borrowTy,
                            isRaw,
                            MIRLValue::newIndex(std::move(slot), std::move(indexLv.as_Local()))
                            }));
                        builder.setResult(node.span(), std::move(res));
                        return ;
                    }

                    // Floating point operations that can be algebraically optimised
                    // Lazy: Just conver to base operations
                    if (name == "fadd_algebraic") {
                        builder.setResult(node.span(), MIRRValue::make_BinOp({std::move(values[0]), MIRBinOp::ADD, std::move(values[1])}));
                        return;
                    }
                    if (name == "fsub_algebraic") {
                        builder.setResult(node.span(), MIRRValue::make_BinOp({std::move(values[0]), MIRBinOp::SUB, std::move(values[1])}));
                        return;
                    }
                    if (name == "fmul_algebraic") {
                        builder.setResult(node.span(), MIRRValue::make_BinOp({std::move(values[0]), MIRBinOp::MUL, std::move(values[1])}));
                        return;
                    }
                    if (name == "fdiv_algebraic") {
                        builder.setResult(node.span(), MIRRValue::make_BinOp({std::move(values[0]), MIRBinOp::DIV, std::move(values[1])}));
                        return;
                    }
                    if (name == "frem_algebraic") {
                        builder.setResult(node.span(), MIRRValue::make_BinOp({std::move(values[0]), MIRBinOp::MOD, std::move(values[1])}));
                        return;
                    }
                    if (name == "box_new") {
                        // Call "exchange_malloc" and move the argument into that returned pointer (same as 1.29 emplace)
                        const auto& dataTy = gpath.params.types.at(0);
                        MIRRValue val;
                        {
                            auto& tuMatch = values[0];
                            switch (tuMatch.tag()) {
                                case MIRParam::TAG_LValue: {
                                    auto& lv = tuMatch.as_LValue();
                                    val = std::move(lv);
                                    break;
                                }
                                case MIRParam::TAG_Constant: {
                                    auto& c = tuMatch.as_Constant();
                                    val = std::move(c);
                                    break;
                                }
                                case MIRParam::TAG_Borrow: {
                                    TODO(node.span(), "box_new with a borrow input?");
                                    break;
                                }
                            }
                        }
                        boxNew(node, dataTy, std::move(val));
                        return ;
                    }
                    builder.endBlock(MIRTerminator::make_Call({nextBlock, MIRUnwindAction::make_Cleanup(panicBlock), res.clone(), MIRCallTarget::make_Intrinsic({name, gpath.params.clone()}), mv$(values)}));
                } else if (fcn.abi == "platform-intrinsic") {
                    builder.endBlock(MIRTerminator::make_Call({nextBlock, MIRUnwindAction::make_Cleanup(panicBlock), res.clone(), MIRCallTarget::make_Intrinsic({RcString(FMT("platform:" << gpath.path.components().back())), gpath.params.clone()}), mv$(values)}));
                }

                // rustc has drop_in_place as a lang item, trustme uses an intrinsic
                if (gpath.path == builder.crate().getLangItemPathOpt("drop_in_place")) {
                    builder.endBlock(MIRTerminator::make_Call({nextBlock, MIRUnwindAction::make_Cleanup(panicBlock), res.clone(), MIRCallTarget::make_Intrinsic({"drop_in_place", gpath.params.clone()}), mv$(values)}));
                }

                if (fcn.returnType->is_Diverge()) {
                    unconditionalDiverge = true;
                }
            } else {
                // TODO: Know if the call unconditionally diverges.
                if (node.cache.argTypes.back()->is_Diverge()) {
                    unconditionalDiverge = true;
                }
            }

            // If the call wasn't to an intrinsic, emit it as a path
            if (builder.blockActive()) {
                builder.endBlock(MIRTerminator::make_Call({nextBlock, MIRUnwindAction::make_Cleanup(panicBlock), res.clone(), node.path.clone(), mv$(values), SourceLocation(node.span())}));
            }

            builder.setCurBlock(panicBlock);
            emitUnwind(node.span());

            builder.setCurBlock(nextBlock);

            // If the function doesn't return, early-terminate the return block.
            if (unconditionalDiverge) {
                builder.endBlock(MIRTerminator::make_Unreachable({}));
                builder.setCurBlock(builder.newBbUnlinked());
            } else {
                // NOTE: This has to be done here because the builder can't easily do it.
                builder.markValueAssigned(node.span(), res);
            }
            builder.setResult(node.span(), mv$(res));
        }

        void visit(HIRExprNodeCallValue& node) override {
            TRACE_FUNCTION_F("_CallValue " << node.value->resType);
            auto _ = saveAndEdit(borrowRaiseTarget, nullptr);

            // _CallValue is ONLY valid on function pointers (all others must be desugared)
            ASSERT_BUG(node.span(), node.value->resType->is_Function(), "Leftover _CallValue on a non-fn()");
            this->visitNodePtr(node.value);
            if (!builder.blockActive()) {
                return;
            }

            // Get the function pointer in a temporary BEFORE getting arguments
            auto fcnVal = builder.newTemporary(node.value->resType);
            builder.pushStmtAssign(node.value->span(), fcnVal.clone(), builder.getResult(node.value->span()));

            auto values = getArgs(node.args);
            if (!builder.blockActive()) {
                return;
            }

            auto panicBlock = builder.newBbUnlinked();
            auto nextBlock = builder.newBbUnlinked();
            auto res = builder.newTemporary(node.resType);
            builder.endBlock(MIRTerminator::make_Call({nextBlock, MIRUnwindAction::make_Cleanup(panicBlock), res.clone(), mv$(fcnVal), mv$(values), SourceLocation(node.span())}));

            builder.setCurBlock(panicBlock);
            emitUnwind(node.span());

            builder.setCurBlock(nextBlock);
            // TODO: Support diverging value calls
            builder.markValueAssigned(node.span(), res);
            builder.setResult(node.span(), mv$(res));
        }

        void visit(HIRExprNodeCallMethod& node) override {
            // TODO: Allow use on trait objects? May not be needed, depends.
            BUG(node.span(), "Leftover _CallMethod");
        }

        void visit(HIRExprNodeField& node) override {
            TRACE_FUNCTION_F("_Field \"" << node.field << "\"");
            this->visitNodePtr(node.value);
            if (!builder.blockActive()) {
                return;
            }
            auto val = builder.getResultInLvalue(node.value->span(), node.value->resType);

            const auto& valTy = node.value->resType;

            unsigned int idx;
            if (::std::isdigit(node.field.c_str()[0])) {
                ::std::stringstream(node.field.c_str()) >> idx;
                builder.setResult(node.span(), MIRLValue::newField(mv$(val), idx));
            } else if (const auto* bep = valTy->as_Path().binding.opt_Struct()) {
                const auto& str = **bep;
                const auto& fields = str.data.as_Named();
                idx = ::std::find_if(fields.begin(), fields.end(), [&](const auto& x) {
                    return x.name == node.field;
                }) - fields.begin();
                builder.setResult(node.span(), MIRLValue::newField(mv$(val), idx));
            } else if (const auto* bep = valTy->as_Path().binding.opt_Union()) {
                const auto& unm = **bep;
                const auto& fields = unm.variants;
                idx = ::std::find_if(fields.begin(), fields.end(), [&](const auto& x) {
                    return x.name == node.field;
                }) - fields.begin();

                builder.setResult(node.span(), MIRLValue::newDowncast(mv$(val), idx));
            } else {
                BUG(node.span(), "Field access on non-union/struct - " << valTy);
            }
        }

        void visit(HIRExprNodeLiteral& node) override {
            TRACE_FUNCTION_F("_Literal");
            switch (node.data.tag()) {
                case HIRExprLiteral::TAG_Integer: {
                    auto& e = node.data.as_Integer();
                    const HIRTypeData* literalType = node.resType;
                    if (const auto* pattern = literalType->opt_Pattern()) literalType = pattern->inner;
                    ASSERT_BUG(node.span(), literalType->is_Primitive(), "Non-primitive-backed return type for Integer literal - " << node.resType);
                    auto ity = literalType->as_Primitive();
                    switch (ity) {
                        case HIRCoreType::U8:
                        case HIRCoreType::U16:
                        case HIRCoreType::U32:
                        case HIRCoreType::U64:
                        case HIRCoreType::U128:
                        case HIRCoreType::Usize:
                            builder.setResult(node.span(), MIRConstant::make_Uint({e.value, ity}));
                            break;
                        case HIRCoreType::Char:
                            builder.setResult(node.span(), MIRConstant::make_Uint({e.value, ity}));
                            break;
                        case HIRCoreType::I8:
                        case HIRCoreType::I16:
                        case HIRCoreType::I32:
                        case HIRCoreType::I64:
                        case HIRCoreType::I128:
                        case HIRCoreType::Isize:
                            builder.setResult(node.span(), MIRConstant::make_Int({S128(e.value), ity}));
                            break;
                        default:
                            BUG(node.span(), "Integer literal with unexpected type - " << node.resType);
                    }
                    break;
                }
                case HIRExprLiteral::TAG_Float: {
                    auto& e = node.data.as_Float();
                    const HIRTypeData* literalType = node.resType;
                    if (const auto* pattern = literalType->opt_Pattern()) literalType = pattern->inner;
                    ASSERT_BUG(node.span(), literalType->is_Primitive(), "Non-primitive-backed return type for Float literal - " << node.resType);
                    auto ity = literalType->as_Primitive();
                    builder.setResult(node.span(), MIRRValue::make_Constant(MIRConstant::make_Float({e.value, ity})));
                    break;
                }
                case HIRExprLiteral::TAG_Boolean: {
                    auto& e = node.data.as_Boolean();
                    builder.setResult(node.span(), MIRRValue::make_Constant(MIRConstant::make_Bool({e})));
                    break;
                }
                case HIRExprLiteral::TAG_String: {
                    auto& e = node.data.as_String();
                    builder.setResult(node.span(), MIRRValue::make_Constant(MIRConstant(e)));
                    break;
                }
                case HIRExprLiteral::TAG_CString: {
                    auto& e = node.data.as_CString();
                    auto s = e.v;
                    s.push_back('\0');

                    // Emit as `transmute<&Cstr,&str>`
                    auto res = builder.newTemporary(node.resType);

                    auto castPanic = builder.newBbUnlinked();
                    auto castOk = builder.newBbUnlinked();
                    HIRPathParams transmuteParams;
                    transmuteParams.types.push_back(node.resType);
                    transmuteParams.types.push_back(builder.resolve().crate.types.borrow(HIRBorrowType::Shared, builder.resolve().crate.types.primitive(HIRCoreType::Str)));
                    builder.endBlock(MIRTerminator::make_Call({castOk, MIRUnwindAction::make_Cleanup(castPanic), res.clone(), MIRCallTarget::make_Intrinsic({"transmute", mv$(transmuteParams)}), makeVec1(MIRParam(MIRConstant(std::move(s))))}));
                    builder.setCurBlock(castPanic);
                    emitUnwind(node.span());
                    builder.setCurBlock(castOk);

                    builder.setResult(node.span(), mv$(res));
                    break;
                }
                case HIRExprLiteral::TAG_ByteString: {
                    auto& e = node.data.as_ByteString();
                    ::std::vector<u8> v(e.begin(), e.end());
                    builder.setResult(node.span(), MIRRValue::make_Constant(MIRConstant(mv$(v))));
                    break;
                }
            }
        }

        void visit(HIRExprNodeUnitVariant& node) override {
            const Span& sp = node.span();
            TRACE_FUNCTION_F("_UnitVariant");
            if (!node.isStruct) {
                // Get the variant index from the enum.
                auto enumPath = node.path.clone();
                auto varName = enumPath.path.popComponent();

                const auto& enm = builder.crate().getEnumByPath(sp, enumPath.path);

                auto idx = enm.findVariant(varName);
                ASSERT_BUG(sp, idx != SIZE_MAX, "Variant " << node.path.path << " isn't present");

                // VALIDATION
                if (const auto* e = enm.data.opt_Data()) {
                    const auto& var = (*e)[idx];
                    ASSERT_BUG(sp, !var.isStruct, "Variant " << node.path.path << " isn't a unit variant");
                }

                builder.setResult(node.span(), MIRRValue::make_EnumVariant({mv$(enumPath), static_cast<unsigned>(idx), {}}));
            } else {
                builder.setResult(node.span(), MIRRValue::make_Struct({node.path.clone(), {}}));
            }
        }

        void visit(HIRExprNodePathValue& node) override {
            const auto& sp = node.span();
            TRACE_FUNCTION_F("_PathValue - " << node.path);
            if (node.resType->is_NamedFunction() && node.target != HIRExprNodePathValue::STATIC && node.target != HIRExprNodePathValue::CONSTANT) {
                auto tmp = builder.newTemporary(node.resType);
                builder.pushStmtAssign(sp, tmp.clone(), MIRConstant::make_Function({box$(node.path.clone())}));
                builder.setResult(sp, mv$(tmp));
                return;
            }
            switch (node.path.data.tag()) {
                case HIRPathData::TAG_Generic: {
                    auto& pe = node.path.data.as_Generic();
                    // Enum variant constructor.
                        if (node.target == HIRExprNodePathValue::ENUM_VAR_CONSTR) {
                            BUG(node.span(), "Should have produced a NamedFunction type and have been handled above");
                        }
                        const auto& vi = builder.crate().getValitemByPath(node.span(), pe.path);
                    switch (vi.tag()) {
                        case HIRValueItem::TAG_Import: {
                            BUG(sp, "All references via imports should be replaced");
                            break;
                        }
                        case HIRValueItem::TAG_Constant: {
                            const auto& e = *vi.as_Constant();
                            auto ty = MonomorphStatePtr(builder.resolve().crate.types, nullptr, nullptr, &pe.params).monomorphType(sp, e.type);
                            auto tmp = builder.newTemporary(ty);
                            builder.pushStmtAssign(sp, tmp.clone(), MIRConstant::make_Const({box$(node.path.clone())}));
                            builder.setResult(node.span(), mv$(tmp));
                            break;
                        }
                        case HIRValueItem::TAG_Static: {
                            builder.setResult(node.span(), MIRLValue::newStatic(node.path.clone()));
                            break;
                        }
                        case HIRValueItem::TAG_StructConstant: {
                            // TODO: Why is this still a PathValue?
                            builder.setResult(node.span(), MIRRValue::make_Struct({pe.clone(), {}}));
                            break;
                        }
                        case HIRValueItem::TAG_Function: {
                            BUG(node.span(), "Should have produced a NamedFunction type and have been handled above");
                            break;
                        }
                        case HIRValueItem::TAG_StructConstructor: {
                            BUG(node.span(), "Should have produced a NamedFunction type and have been handled above");
                            break;
                        }
                    }
                    break;
                }
                case HIRPathData::TAG_UfcsKnown: {
                    auto& pe = node.path.data.as_UfcsKnown();
                    // Check what item type this is (from the trait)
                    const auto& tr = builder.crate().getTraitByPath(sp, pe.trait.path);
                    auto it = tr.values.find(pe.item);
                    ASSERT_BUG(sp, it != tr.values.end(), "Cannot find trait item for " << node.path);
                    switch (it->second.tag()) {
                        case HIRTraitValueItem::TAG_Constant: {
                            builder.setResult(sp, MIRConstant::make_Const({box$(node.path.clone())}));
                            break;
                        }
                        case HIRTraitValueItem::TAG_Static: {
                            TODO(sp, "Associated statics (non-rustc) - " << node.path);
                            break;
                        }
                        case HIRTraitValueItem::TAG_Function: {
                            BUG(node.span(), "Should have produced a NamedFunction type and have been handled above");
                            break;
                        }
                    }
                    break;
                }
                case HIRPathData::TAG_UfcsUnknown: {
                    BUG(sp, "PathValue - Encountered UfcsUnknown - " << node.path);
                    break;
                }
                case HIRPathData::TAG_UfcsInherent: {
                    auto& pe = node.path.data.as_UfcsInherent();
                    // 1. Find item in an impl block
                    auto rv = builder.crate().findTypeImpls(pe.type, HIRResolvePlaceholdersNop(), [&](const auto& impl) {
                        DEBUG("- impl" << impl.params.fmtArgs() << " " << impl.type);
                        // Associated functions
                        {
                            auto it = impl.methods.find(pe.item);
                            if (it != impl.methods.end()) {
                                builder.setResult(sp, MIRConstant::make_ItemAddr({box$(node.path.clone())}));
                                return true;
                            }
                        }
                        // Associated consts
                        {
                            auto it = impl.constants.find(pe.item);
                            if (it != impl.constants.end()) {
                                builder.setResult(sp, MIRConstant::make_Const({box$(node.path.clone())}));
                                return true;
                            }
                        }
                        // Associated static (undef)
                        return false;
                    });
                    if (!rv) {
                        ERROR(sp, E0000, "Failed to locate item for " << node.path);
                    }
                    break;
                }
            }
        }

        void visit(HIRExprNodeVariable& node) override {
            TRACE_FUNCTION_F("_Variable - " << node.name << " #" << node.slot);
            // If there's an alias active, emit that
            if (const auto* a = builder.getVariableAlias(node.span(), node.slot)) {
                switch (a->first) {
                    case HIRPatternBinding::Type::Move:
                        builder.setResult(node.span(), a->second.clone());
                        break;
                    case HIRPatternBinding::Type::Ref:
                        builder.setResult(node.span(), MIRRValue::make_Borrow({HIRBorrowType::Shared, false, a->second.clone()}));
                        break;
                    case HIRPatternBinding::Type::MutRef:
                        builder.setResult(node.span(), MIRRValue::make_Borrow({HIRBorrowType::Unique, false, a->second.clone()}));
                        break;
                }
                return;
            }
            builder.setResult(node.span(), builder.getVariable(node.span(), node.slot));
        }

        void visit(HIRExprNodeConstParam& node) override {
            TRACE_FUNCTION_F("_ConstParam - " << node.name << " #" << node.binding);
            builder.setResult(node.span(), MIRConstant::make_Generic({node.name, node.binding}));
        }

        void visitSlInner(HIRExprNodeStructLiteral& node, const HIRStruct& str, const HIRGenericPath& path) {
            const Span& sp = node.span();

            ASSERT_BUG(sp, str.data.is_Named(), "");
            const tStructFields& fields = str.data.as_Named();

            ::std::vector<bool> valuesSet;
            ::std::vector<MIRParam> values;
            values.resize(fields.size());
            valuesSet.resize(fields.size());

            for (auto& ent : node.values) {
                auto& valnode = ent.second;
                auto idx = ::std::find_if(fields.begin(), fields.end(), [&](const auto& x) {
                    return x.name == ent.first;
                }) - fields.begin();
                assert(!valuesSet[idx]);
                valuesSet[idx] = true;
                DEBUG("_StructLiteral - fld '" << ent.first << "' (idx " << idx << ")");
                this->visitNodePtr(valnode);
                if (!builder.blockActive()) {
                    return;
                }

                auto res = builder.getResult(valnode->span());
                if (auto* e = res.opt_Constant()) {
                    values.at(idx) = mv$(*e);
                } else {
                    // NOTE: Have to allocate a new temporary because ordering matters
                    auto tmp = builder.newTemporary(valnode->resType);
                    builder.pushStmtAssign(valnode->span(), tmp.clone(), mv$(res));
                    values.at(idx) = mv$(tmp);
                }
            }

            auto baseVal = MIRLValue::newReturn();
            if (node.baseValue) {
                DEBUG("_StructLiteral - base");
                this->visitNodePtr(node.baseValue);
                if (!builder.blockActive()) {
                    return;
                }
                baseVal = builder.getResultInLvalue(node.baseValue->span(), node.baseValue->resType);
            }
            for (unsigned int i = 0; i < values.size(); i++) {
                if (!valuesSet[i]) {
                    if (node.baseValue) {
                        values[i] = MIRLValue::newField(baseVal.clone(), i);
                    } else if (fields[i].defaultValue) {
                        const auto& v = *fields[i].defaultValue;
                        auto ms = MonomorphStatePtr(builder.resolve().crate.types, nullptr, &path.params, nullptr);
                        values[i] = builder.lvalueOrTemp(sp, ms.monomorphType(sp, fields[i].ty), MIRConstant::make_Const({::std::make_unique<HIRPath>(ms.monomorphGenericpath(sp, v))}));
                    } else {
                        ERROR(node.span(), E0000, "Field '" << fields[i].name << "' not specified");
                    }
                } else {
                    // Partial move support will handle dropping the rest?
                }
            }

            builder.setResult(node.span(), MIRRValue::make_Struct({path.clone(), mv$(values)}));
        }

        void visit(HIRExprNodeStructLiteral& node) override {
            TRACE_FUNCTION_F("_StructLiteral");

            const auto& tyPath = node.realPath;

            {
                auto& tuMatch = node.resType->as_Path().binding;
                switch (tuMatch.tag()) {
                    case HIRTypePathBinding::TAG_Unbound: {
                        break;
                    }
                    case HIRTypePathBinding::TAG_Opaque: {
                        break;
                    }
                    case HIRTypePathBinding::TAG_Enum: {
                        auto& e = tuMatch.as_Enum();
                        auto enumPath = tyPath.clone();
                        auto varName = enumPath.path.popComponent();

                        const auto& enm = *e;
                        size_t idx = enm.findVariant(varName);
                        ASSERT_BUG(node.span(), idx != SIZE_MAX, "");
                        ASSERT_BUG(node.span(), enm.data.is_Data(), "");
                        const auto& varTy = enm.data.as_Data()[idx].type;
                        const auto& str = *varTy->as_Path().binding.as_Struct();

                        // Take advantage of the identical generics to cheaply clone/monomorph the path.
                        HIRGenericPath structPath = tyPath.clone();
                        structPath.path = varTy->as_Path().path.data.as_Generic().path;

                        this->visitSlInner(node, str, structPath);
                        if (!builder.blockActive()) {
                            return;
                        }
                        auto vals = std::move(builder.getResult(node.span()).as_Struct().vals);

                        // And create Variant
                        builder.setResult(node.span(), MIRRValue::make_EnumVariant({mv$(enumPath), static_cast<unsigned>(idx), mv$(vals)}));
                        break;
                    }
                    case HIRTypePathBinding::TAG_Union: {
                        auto& e = tuMatch.as_Union();
                        const auto& variantName = node.values.front().first;
                        auto& valueNode = node.values.front().second;
                        this->visitNodePtr(valueNode);
                        if (!builder.blockActive()) {
                            return;
                        }
                        auto val = builder.getResultInLvalue(valueNode->span(), valueNode->resType);

                        const auto& unm = *e;
                        auto it = ::std::find_if(unm.variants.begin(), unm.variants.end(), [&](const HIRStructField& v) -> auto {
                            return v.name == variantName;
                        });
                        assert(it != unm.variants.end());
                        unsigned int idx = it - unm.variants.begin();

                        builder.setResult(node.span(), MIRRValue::make_UnionVariant({node.realPath.clone(), idx, mv$(val)}));
                        break;
                    }
                    case HIRTypePathBinding::TAG_ExternType: {
                        BUG(node.span(), "_StructLiteral ExternType isn't valid?");
                        break;
                    }
                    case HIRTypePathBinding::TAG_Struct: {
                        auto& e = tuMatch.as_Struct();
                        if (e->data.is_Unit()) {
                            builder.setResult(node.span(), MIRRValue::make_Struct({tyPath.clone(), {}}));
                            return;
                        }
                        if (e->data.is_Tuple()) {
                            // `S { 0: a, ..base }` names tuple fields by index.
                            // The no-base form can survive AST lowering when
                            // the path is `Self`; in that case every field must
                            // be supplied by the expression.
                            const auto& fields = e->data.as_Tuple();
                            stl::Vector<u8> valuesSet;
                            valuesSet.zero(fields.size());
                            std::vector<MIRParam> values;
                            values.resize(fields.size());
                            for (auto& val : node.values) {
                                const auto idx = static_cast<unsigned>(::std::atoi(val.first.c_str()));
                                ASSERT_BUG(node.span(), idx < fields.size(), "Tuple field index " << idx << " out of range");
                                ASSERT_BUG(node.span(), !valuesSet[idx], "Tuple field " << idx << " specified twice");
                                this->visitNodePtr(val.second);
                                if (!builder.blockActive()) {
                                    return;
                                }
                                valuesSet.mut(idx) = true;
                                values.at(idx) = builder.getResultInParam(val.second->span(), val.second->resType);
                            }
                            auto baseValue = MIRLValue::newReturn();
                            if (node.baseValue) {
                                this->visitNodePtr(node.baseValue);
                                if (!builder.blockActive()) {
                                    return;
                                }
                                baseValue = builder.getResultInLvalue(node.baseValue->span(), node.baseValue->resType);
                            }
                            for (size_t i = 0; i < fields.size(); i++) {
                                if (!valuesSet[i]) {
                                    if (!node.baseValue) {
                                        ERROR(node.span(), E0000, "Field '" << i << "' not specified");
                                    }
                                    values.at(i) = MIRLValue::newField(baseValue.clone(), static_cast<unsigned>(i));
                                }
                            }
                            builder.setResult(node.span(), MIRRValue::make_Struct({tyPath.clone(), std::move(values)}));
                            return;
                        }

                        this->visitSlInner(node, *e, tyPath);
                        break;
                    }
                }
            }
        }

        void visit(HIRExprNodeTuple& node) override {
            TRACE_FUNCTION_F("_Tuple");
            auto values = getArgs(node.vals);
            if (!builder.blockActive()) {
                return;
            }

            builder.setResult(node.span(), MIRRValue::make_Tuple({mv$(values)}));
        }

        void visit(HIRExprNodeArrayList& node) override {
            TRACE_FUNCTION_F("_ArrayList");
            auto values = getArgs(node.vals);
            if (!builder.blockActive()) {
                return;
            }

            builder.setResult(node.span(), MIRRValue::make_Array({mv$(values)}));
        }

        void visit(HIRExprNodeArraySized& node) override {
            TRACE_FUNCTION_F("_ArraySized");
            this->visitNodePtr(node.val);
            if (!builder.blockActive()) {
                return;
            }
            auto value = builder.getResultInParam(node.span(), node.val->resType);

            builder.setResult(node.span(), MIRRValue::make_SizedArray({mv$(value), std::move(node.size)}));
            // Ensure that the size is valid (avoids crashes when debug is enabled)
            node.size = HIRArraySize();
        }

        void visit(HIRExprNodeClosure& node) override {
            TRACE_FUNCTION_F("_Closure - " << node.objPath);
            auto _ = saveAndEdit(borrowRaiseTarget, nullptr);

            ::std::vector<MIRParam> vals;
            vals.reserve(node.captures.size());
            for (auto& arg : node.captures) {
                this->visitNodePtr(arg);
                vals.push_back(builder.getResultInLvalue(arg->span(), arg->resType));
            }

            builder.setResult(node.span(), MIRRValue::make_Struct({node.objPath.clone(), mv$(vals)}));
        }

        void visitCommonCr(const Span& sp, const HIRGenericPath& objPath, const HIRTypeData* stateType, ::std::vector<HIRExprNodeP>& captures) {
            auto _ = saveAndEdit(borrowRaiseTarget, nullptr);

            ::std::vector<MIRParam> vals;
            vals.reserve(1 + captures.size());

            // Zero the state index
            {
                const auto& langMaybeUninit = builder.resolve().crate.getLangItemPath(sp, "maybe_uninit");
                const auto& unmMaybeUninit = builder.resolve().crate.getUnionByPath(sp, langMaybeUninit);
                auto slotType = builder.resolve().crate.types.path(HIRGenericPath(langMaybeUninit, HIRPathParams(stateType)), &unmMaybeUninit);

                auto resSlot = builder.newTemporary(slotType);
                auto sizePanic = builder.newBbUnlinked();
                auto sizeOk = builder.newBbUnlinked();
                builder.endBlock(
                    MIRTerminator::make_Call(
                        {sizeOk,
                         MIRUnwindAction::make_Cleanup(sizePanic),
                         resSlot.clone(),
                         MIRCallTarget::make_Intrinsic({"init", HIRPathParams(mv$(slotType))}), // I.e. `mem::zeroed`
                         {}}
                    )
                );
                builder.setCurBlock(sizePanic);
                emitUnwind(sp);
                builder.setCurBlock(sizeOk);
                vals.push_back(std::move(resSlot));
            }
            // Populate the rest
            for (auto& arg : captures) {
                this->visitNodePtr(arg);
                vals.push_back(builder.getResultInLvalue(arg->span(), arg->resType));
            }

            builder.setResult(sp, MIRRValue::make_Struct({objPath.clone(), mv$(vals)}));
        }

        void visit(HIRExprNodeGenerator& node) override {
            TRACE_FUNCTION_F("_Generator - " << node.objPath);
            ASSERT_BUG(node.span(), node.objPtr, "Generator not created");
            ASSERT_BUG(node.span(), !node.code, "Encountered outer generator wrapper");

            visitCommonCr(node.span(), node.objPath, node.stateDataType, node.captures);
        }

        void visit(HIRExprNodeGeneratorWrapper& node) override {
            BUG(node.span(), "Unexpected");
        }

        void visit(HIRExprNodeAsyncBlock& node) override {
            TRACE_FUNCTION_F("_AsyncBlock - " << node.objPath);
            ASSERT_BUG(node.span(), node.objPtr, "Future not created");
            ASSERT_BUG(node.span(), !node.code, "Encountered code inside post-expand async block");

            visitCommonCr(node.span(), node.objPath, node.stateDataType, node.captures);
        }
    };
}

namespace {
    MIRFunctionPointer lowerAsyncDropGluePoll(const StaticTraitResolve& resolve, const HIRItemPath& path, HIRExprNodeGeneratorWrapper& node, const HIRTypeData* retTy) {
        const Span& sp = node.span();
        ASSERT_BUG(sp, node.objPtr, "async-drop glue future without its generated type");
        ASSERT_BUG(sp, path.getTopIp().ty, "async-drop glue poll without its impl Self type");
        const auto& fields = node.objPtr->data.as_Tuple();
        ASSERT_BUG(sp, fields.size() == 2, "async-drop glue future must contain state and the dropee pointer, got " << fields.size());
        const auto* pointer = fields[1].ent->opt_Pointer();
        ASSERT_BUG(sp, pointer, "async-drop glue capture is not a raw pointer: " << fields[1].ent);

        MIRFunction fcn;
        HIRPathParams intrinsicParams;
        intrinsicParams.types.push_back(pointer->inner);
        intrinsicParams.types.push_back(path.getTopIp().ty);

        MIRBasicBlock poll;
        poll.terminator = MIRTerminator::make_Call({
            1,
            MIRUnwindAction::make_Continue({}),
            MIRLValue::newReturn(),
            MIRCallTarget::make_Intrinsic({"async_drop_glue_poll", std::move(intrinsicParams)}),
            ::makeVec2<MIRParam>(MIRLValue::newArgument(0), MIRLValue::newArgument(1)),
        });
        fcn.blocks.push_back(std::move(poll));
        MIRBasicBlock done;
        done.terminator = MIRTerminator::make_Return({});
        fcn.blocks.push_back(std::move(done));

        if (!node.dropFcnPtr->code.mir) {
            MIRFunction dropFcn;
            MIRBasicBlock block;
            block.statements.push_back(MIRStatement::make_Assign({MIRLValue::newReturn(), MIRRValue::make_Tuple({})}));
            block.terminator = MIRTerminator::make_Return({});
            dropFcn.blocks.push_back(std::move(block));
            node.dropFcnPtr->code.mir = MIRFunctionPointer(box$(std::move(dropFcn)).release());
        }
        return MIRFunctionPointer(box$(std::move(fcn)).release());
    }
}

MIRFunctionPointer LowerMIR(const StaticTraitResolve& resolve, const HIRItemPath& path, const HIRExprPtr& ptr, const HIRTypeData* retTy, const HIRFunction::argsT& args) {
    TRACE_FUNCTION_F(path);

    HIRExprNode& rootNode = const_cast<HIRExprNode&>(*ptr);
    if (auto* generator = cast<HIRExprNodeGeneratorWrapper>(&rootNode)) {
        if (generator->objPtr && generator->objPtr->structMarkings.isAsyncDropGlue) {
            return lowerAsyncDropGluePoll(resolve, path, *generator, retTy);
        }
    }

    MIRFunction fcn;
    fcn.locals.reserve(ptr.bindings.size());
    for (const auto& t : ptr.bindings) {
        fcn.locals.push_back(t);
    }

    // Scope ensures that builder cleanup happens before `fcn` is moved
    {
        const Span& sp = ptr->span();

        MirBuilder builder{ptr->span(), resolve, retTy, args, fcn};
        ExprVisitorConv ev{builder, ptr.bindings, cast<HIRExprNodeGeneratorWrapper>(&rootNode)};

        // 1. Apply destructuring to arguments
        unsigned int i = 0;
        for (const auto& arg : args) {
            const auto& pat = arg.first;
            builder.scheduleArgumentDrop(i);
            // If the binding is set (i.e. this isn't destructuring) then the table populated by `MirBuilder::MirBuilder(...)` will be used
            if (pat.bindings.size() == 1 && pat.bindings[0].type == HIRPatternBinding::Type::Move && pat.data.is_Any()) {
                // Simple `var: Type` arguments are handled by `MirBuilder.m_var_arg_mappings`
            } else {
                DEBUG("Argument a" << i << " - " << pat);
                ev.schedulePatternDrops(ptr->span(), arg.first, PatternDropOrder::FirstCandidate);
                MIRLowerHIRLet(builder, ev, ptr->span(), arg.first, MIRLValue::newArgument(i), /*else_node=*/nullptr);
            }
            i++;
        }

        // 2. Destructure code
        if (auto* genNode = cast<HIRExprNodeGeneratorWrapper>(&rootNode)) {
            // Mark all capture locals as valid (for later rewrite into variable acesses)
            ::std::map<unsigned, std::vector<MIRLValue::Wrapper>> mappings;
            for (size_t i = 0; i < genNode->captureUsages.size(); i++) {
                unsigned idx = args.size() + i;
                builder.scheduleVariableDrop(idx);
                switch (genNode->captureUsages[i]) {
                    case HIRValueUsage::Borrow:
                    case HIRValueUsage::Mutate: {
                        // TODO: Use `m_variable_aliases` for by-borrow captures, to avoid them being dropped
                        auto lv = MIRLValue::newArgument(0);
                        lv.wrappers.push_back(MIRLValue::Wrapper::newField(0)); // Pin.ptr
                        lv.wrappers.push_back(MIRLValue::Wrapper::newDeref());  // *
                        lv.wrappers.push_back(MIRLValue::Wrapper::newField(1 + i));
                        lv.wrappers.push_back(MIRLValue::Wrapper::newDeref());
                        builder.addVariableAlias(rootNode.span(), idx, HIRPatternBinding::Type::Move, std::move(lv));
                    } break;
                    case HIRValueUsage::Move:
                    case HIRValueUsage::Unknown:
                        builder.markValueAssigned(rootNode.span(), MIRLValue::newLocal(idx));
                        mappings.insert(std::make_pair(idx, ::makeVec1(MIRLValue::Wrapper::newField(1 + i))));
                        break;
                }
            }

            // ------------

            genNode->code->visit(ev);
            if (builder.blockActive() && builder.hasResult()) {
                ev.coroutineReturn(sp, genNode->code->resType);
            }
            builder.finalCleanup();

            // ------------

            // 1. Discard initialised-but-dead locals, then generate the state
            // machine switch and enumerate the values that really cross it.
            auto storageLifetimes = ev.generatorPruneInactiveLocals(sp, resolve, path, retTy, args, fcn);
            std::set<unsigned> saved = ev.generatorFinalise(genNode->span(), const_cast<HIREnum&>(resolve.hirCrate().getEnumByPath(sp, genNode->stateIdxEnum)));
            // 2. Populate state structure
            auto& stateTy = const_cast<HIRStruct&>(*genNode->stateDataType->as_Path().binding.as_Struct());
            unsigned valueVarIdx;
            {
                const auto& unmMaybeUninit = resolve.hirCrate().getUnionByPath(sp, resolve.hirCrate().getLangItemPath(genNode->span(), "maybe_uninit"));
                valueVarIdx = std::find_if(unmMaybeUninit.variants.begin(), unmMaybeUninit.variants.end(), [&](const auto& e) {
                    return e.name == "value";
                }) - unmMaybeUninit.variants.begin();
            }
            ASSERT_BUG(sp, valueVarIdx == 1, "Assumption on MaybeUninit.value's variant index failed");
            auto& fields = stateTy.data.as_Tuple();
            const auto firstStoredLocal = static_cast<unsigned>(1 + genNode->captureUsages.size());
            auto storagePool = stl::ObjPool::fromMemory();
            stl::IntMap<unsigned> storageSlots{storagePool.mutPtr()};
            stl::IntMap<bool> compositeConflicts{storagePool.mutPtr()};
            ev.generatorFindCompositeStorageConflicts(fcn, firstStoredLocal, compositeConflicts);
            ThinVector<HIRTypeRef> storageTypes;
            unsigned storageSlotCount = 0;
            auto makeStorageType = [&]() {
                auto& crate = const_cast<HIRCrate&>(resolve.hirCrate());
                auto& items = crate.rootModule.modItems;
                auto itemIndex = items.size();
                RcString name;
                do {
                    name = RcString::newInterned(FMT("coroutine_storage#M_" << itemIndex));
                    itemIndex += 1;
                } while (items.count(name) != 0);

                auto boxed = crate.pool->make<HIRVisEnt<HIRTypeItem>>(HIRVisEnt<HIRTypeItem>{
                    HIRPublicity::newNone(),
                    HIRUnion{stateTy.params.clone(), HIRUnion::Repr::Rust, {}}
                });
                auto* item = &boxed->ent;
                items.insert(std::make_pair(name, boxed));
                const auto& statePath = genNode->stateDataType->as_Path().path.data.as_Generic();
                return crate.types.path(
                    HIRGenericPath(HIRSimplePath(crate.crateName, {}) + name, statePath.params.clone()),
                    &item->as_Union()
                );
            };
            for (auto idx : saved) {
                if (idx < firstStoredLocal) {
                    continue;
                }
                ASSERT_BUG(sp, idx < fcn.locals.size(), idx << " >= " << fcn.locals.size());

                unsigned storageSlot = 0;
                while (ev.generatorStorageSlotConflicts(idx, storageSlot, firstStoredLocal, storageSlots, compositeConflicts, storageLifetimes)) {
                    storageSlot += 1;
                }
                if (storageSlot == storageSlotCount) {
                    storageTypes.push_back(makeStorageType());
                    fields.push_back(HIRVisEnt<HIRTypeRef>{HIRPublicity::newNone(), storageTypes[storageSlot]});
                    storageSlotCount += 1;
                }
                ASSERT_BUG(sp, storageSlot < storageSlotCount, "Non-contiguous coroutine storage slot " << storageSlot);
                storageSlots.insert(idx, storageSlot);

                auto& storageTy = const_cast<HIRUnion&>(*storageTypes[storageSlot]->as_Path().binding.as_Union());
                auto storageVariant = static_cast<unsigned>(storageTy.variants.size());
                storageTy.variants.push_back(HIRStructField{RcString(), HIRPublicity::newNone(), fcn.locals.at(idx), {}});
                mappings.insert(
                    std::make_pair(
                        idx,
                        std::vector<MIRLValue::Wrapper>{
                            MIRLValue::Wrapper::newField(0),
                            MIRLValue::Wrapper::newDowncast(valueVarIdx), // MaybeUninit.value
                            MIRLValue::Wrapper::newField(0),              // ManuallyDrop.value
                            MIRLValue::Wrapper::newField(1 + storageSlot),
                            MIRLValue::Wrapper::newDowncast(storageVariant)
                        }
                    )
                );
            }
            for (const auto& m : mappings) {
                DEBUG("Mapping _" << m.first << " = " << m.second);
            }
            ::std::map<unsigned, unsigned> dropFlagMapping;
            for (auto idx : ev.generatorDropFlags()) {
                dropFlagMapping[idx] = dropFlagMapping.size();
                DEBUG("df$" << idx << " = BIT" << dropFlagMapping[idx]);
            }
            // Add drop flags to the end
            auto dropFlagsFieldIdx = fields.size();
            fields.push_back(HIRVisEnt<HIRTypeRef>{HIRPublicity::newNone(), resolve.hirCrate().types.array(resolve.hirCrate().types.primitive(HIRCoreType::U8), (dropFlagMapping.size() + 7) / 8)});

            // 3. Rewrite usage of saved values
            // - Note: Need to allocate new temporaries if indexing by an updated lvalue
            class Rewriter: public MIRVisitorMut {
                /// Remapped locals (indexes into coroutine struct, not just into the state)
                ///
                /// From `Pin<&mut self>`, these are appended to `self.pin.*`
                const ::std::map<unsigned, std::vector<MIRLValue::Wrapper>>& mappings_;
                /// Mapping from drop flag indexes to bit sin the drop flag list
                const ::std::map<unsigned, unsigned>& dropFlagMapping;
                /// Index of the drop flags bitset (array of u8) in the state (field 0 of top structure)
                unsigned dropFlagsField;

                ::std::vector<MIRStatement> newStatements;
                unsigned bbIdx = 0;
                unsigned stmtIdx = 0;

            public:
                Rewriter(const ::std::map<unsigned, std::vector<MIRLValue::Wrapper>>& mappings, const ::std::map<unsigned, unsigned>& dropFlagMapping, unsigned dropFlagsField)
                    : mappings_(mappings)
                    , dropFlagMapping(dropFlagMapping)
                    , dropFlagsField(dropFlagsField)
                {
                }

                bool visitLvalue(MIRLValue& lv, MIRValUsage u) override {
                    if (lv.root.is_Local()) {
                        auto it = mappings_.find(lv.root.as_Local());
                        if (it != mappings_.end()) {
                            lv.root = MIRLValue::Storage::newArgument(0);
                            auto dit = lv.wrappers.begin();
                            dit = lv.wrappers.insert(dit, MIRLValue::Wrapper::newField(0)) + 1; // Pin.ptr
                            dit = lv.wrappers.insert(dit, MIRLValue::Wrapper::newDeref()) + 1;  // *
                            dit = lv.wrappers.insert(dit, it->second.begin(), it->second.end()) + 1;
                            DEBUG("BB" << bbIdx << "/" << FMT_CB(os, if (stmtIdx == ~0u) { os << "TERM"; } else { os << stmtIdx; }) << " > " << lv);
                        }
                    }
                    for (auto& w : lv.wrappers) {
                        if (w.is_Index()) {
                            auto it = mappings_.find(w.as_Index());
                            if (it != mappings_.end()) {
                                // Allocate a new temporary, assign it before this statement, use that
                                TODO(Span(), "");
                            }
                        }
                    }

                    return true;
                }

                bool visitStmt(MIRStatement& stmt) override {
                    auto getDropFlagsSlot = [this]() -> MIRLValue {
                        MIRLValue slot = MIRLValue::newArgument(0);
                        slot.wrappers.push_back(MIRLValue::Wrapper::newField(0));              // Pin.ptr
                        slot.wrappers.push_back(MIRLValue::Wrapper::newDeref());               // *
                        slot.wrappers.push_back(MIRLValue::Wrapper::newField(0));              // .0
                        slot.wrappers.push_back(MIRLValue::Wrapper::newDowncast(1));           // .value (From MaybeUninit)
                        slot.wrappers.push_back(MIRLValue::Wrapper::newField(0));              // .value (From ManuallyDrop)
                        slot.wrappers.push_back(MIRLValue::Wrapper::newField(dropFlagsField)); // .drop_flags
                        return slot;
                    };
                    if (auto* s = stmt.opt_SetDropFlag()) {
                        if (dropFlagMapping.count(s->other) != 0) {
                            auto slot = getDropFlagsSlot();
                            unsigned bitNum = dropFlagMapping.at(s->other);
                            // `LoadDropFlag(df$N, src_lv, bit_num)`, where `src_lv` is an array of `u8`
                            newStatements.push_back(
                                MIRStatement::make_LoadDropFlag({
                                    s->other,
                                    std::move(slot),
                                    bitNum,
                                })
                            );
                        }
                        if (dropFlagMapping.count(s->idx) != 0) {
                            // Copy this statement to the output queue, and then rewrite to be:
                            newStatements.push_back(*s);
                            // `SaveDropFlag(dst_lv, bit_num, df$N)`
                            auto slot = getDropFlagsSlot();
                            unsigned bitNum = dropFlagMapping.at(s->idx);
                            stmt = MIRStatement::make_SaveDropFlag({std::move(slot), bitNum, s->idx});
                            // TODO: Replace with no-op? (or let it be cleaned up later as dead code)
                        }
                    } else {
                        // Doesn't use drop flags, no changes/rewrites needed
                    }
                    return MIRVisitorMut::visitStmt(stmt);
                }

                void pushStatements(MIRBasicBlock& bb, size_t& ofs) {
                    for (auto& e : newStatements) {
                        bb.statements.insert(bb.statements.begin() + ofs, std::move(e));
                        ofs += 1;
                    }
                    newStatements.clear();
                }

                void rewriteFcn(MIRFunction& f) {
                    for (auto& bb : f.blocks) {
                        this->bbIdx = &bb - f.blocks.data();
                        for (size_t stmtIdx = 0; stmtIdx < bb.statements.size(); stmtIdx++) {
                            this->stmtIdx = stmtIdx;
                            this->visitStmt(bb.statements[stmtIdx]);
                            this->pushStatements(bb, stmtIdx);
                        }
                        this->stmtIdx = ~0u;
                        if (auto* s = bb.terminator.opt_Drop()) {
                            if (dropFlagMapping.count(s->flagIdx) != 0) {
                                auto slot = MIRLValue::newArgument(0);
                                slot.wrappers.push_back(MIRLValue::Wrapper::newField(0));
                                slot.wrappers.push_back(MIRLValue::Wrapper::newDeref());
                                slot.wrappers.push_back(MIRLValue::Wrapper::newField(0));
                                slot.wrappers.push_back(MIRLValue::Wrapper::newDowncast(1));
                                slot.wrappers.push_back(MIRLValue::Wrapper::newField(0));
                                slot.wrappers.push_back(MIRLValue::Wrapper::newField(dropFlagsField));
                                newStatements.push_back(
                                    MIRStatement::make_LoadDropFlag({
                                        s->flagIdx,
                                        std::move(slot),
                                        dropFlagMapping.at(s->flagIdx),
                                    })
                                );
                            }
                        }
                        this->visitTerminator(bb.terminator);
                        size_t stmtIdx = bb.statements.size();
                        this->pushStatements(bb, stmtIdx);
                    }
                }
            };

            Rewriter(mappings, dropFlagMapping, dropFlagsFieldIdx).rewriteFcn(fcn);

            // 4. Generate drop glue for the generator type and save for later
            // - Make a builder
            // - Insert the switch for each arm
            // - Trigger drops
            auto dropImplBody = MIRFunctionPointer(new MIRFunction());
            {
                TRACE_FUNCTION_F("Generating drop impl");
                MirBuilder dropBuilder(sp, resolve, resolve.hirCrate().types.unit(), genNode->dropFcnPtr->args, *dropImplBody);
                ev.generatorMakeDrop(sp, dropBuilder, genNode->captureUsages.size(), mappings, dropFlagsFieldIdx, dropFlagMapping);
                dropBuilder.finalCleanup();
            }
            for (auto& bb : dropImplBody->blocks) {
                for (auto& stmt : bb.statements) {
                    if (auto* d = stmt.opt_LoadDropFlag()) {
                        d->idx = dropFlagMapping.at(d->idx);
                    }
                }
                if (auto* d = bb.terminator.opt_Drop()) {
                    if (d->flagIdx != ~0u) {
                        d->flagIdx = dropFlagMapping.at(d->flagIdx);
                    }
                }
            }
            genNode->dropFcnPtr->code.mir = std::move(dropImplBody);
        } else {
            rootNode.visit(ev);
            builder.finalCleanup();
        }
    }

    // NOTE: Can't clean up yet, as consteval isn't done
    return MIRFunctionPointer(new MIRFunction(mv$(fcn)));
}

// --------------------------------------------------------------------

void HIRGenerateMIRExpr(const WireBoard& wb, const HIRCrate& crate, const HIRItemPath& path, HIRExprPtr& exprPtr, const HIRFunction::argsT& args, const HIRTypeData* resTy) {
    if (!exprPtr.mir) {
        TRACE_FUNCTION;
        StaticTraitResolve resolve{wb};
        resolve.setBothGenericsRaw(exprPtr.state->implGenerics, exprPtr.state->itemGenerics);
        exprPtr.setMir(LowerMIR(resolve, path, exprPtr, resTy, args));
        // Run cleanup to simplify consteval?
        // - This ends up running before things like vtable generation, so parts of cleanup won't work.
        // This path prepares an on-demand body for the constant evaluator, not
        // the runtime MIR selected by the driver. Keep normal inlining disabled,
        // but retain the local simplification that CTFE historically required.
        MIROptimise(resolve, path, *exprPtr.mir, args, resTy, /*opt_level=*/2, /*do_inline=*/false);
    }
}

void HIRGenerateMIR(const WireBoard& wb, HIRCrate& crate) {
    auto callback = makeCallable<MIRExprCb>([&](const auto& res, const auto& p, HIRExprPtr& exprPtr, const auto& args, const auto& ty) {
        if (!exprPtr.getMirOpt()) {
            exprPtr.setMir(LowerMIR(res, p, exprPtr, ty, args));
        }
    });
    MIROuterVisitor ov{wb, crate, callback};
    ov.visitCrate(crate);
}

void MIRLowerHIRMatch(MirBuilder& builder, MirConverter& conv, HIRExprNodeMatch& node, MIRLValue matchVal, const std::vector<unsigned>& letElseInitializerTemps);

namespace {
    void getTyAndVal(
        const Span& sp,
        MirBuilder& builder,
        const HIRTypeData* topTy,
        const MIRLValue& topVal,
        const fieldPathT& fieldPath,
        unsigned int fieldPathOfs,
        /*Out ->*/ HIRTypeRef& outTy,
        MIRLValue& outVal
    );
}

void MIRLowerHIRGetTypeValueForPath(
    const Span& sp,
    MirBuilder& builder,
    const HIRTypeData* topTy,
    const MIRLValue& topVal,
    const fieldPathT& fieldPath,
    /*Out ->*/ HIRTypeRef& outTy,
    MIRLValue& outVal
) {
    getTyAndVal(sp, builder, topTy, topVal, fieldPath, 0, outTy, outVal);
}

// Definitions generated from mir_from_hir_pattern.tu.
#include "mir_from_hir_pattern_tu.h"
::std::ostream& operator<<(::std::ostream& os, const PatternRule& x);

/// Constructed set of rules from a pattern
struct PatternRuleset {
    struct Deref {
        unsigned rootIndex;
        unsigned parentRoot;
        fieldPathT field;
        const HIRTypeData* sourceType;
        const HIRTypeData* targetType;
        HIRPattern::DerefKind kind;
        unsigned resultLocal = ~0u;
    };

    unsigned int armIdx;
    unsigned int armRuleIdx;

    ::std::vector<PatternRule> rules;
    ::std::vector<PatternBinding> bindings;
    ::std::vector<Deref> derefs;

    static ::Ordering ruleIsBefore(const PatternRule& l, const PatternRule& r);

    bool isBefore(const PatternRuleset& other) const;
};

struct PatternDump {
    const StaticTraitResolve& resolve;
    const HIRTypeData* ty;
    const ::std::vector<PatternRule>& rules;

    PatternDump(const StaticTraitResolve& resolve, const HIRTypeData* ty, const ::std::vector<PatternRule>& rules)
        : resolve(resolve)
        , ty(ty)
        , rules(rules)
    {
    }

    friend ::std::ostream& operator<<(::std::ostream& os, const PatternDump& x) {
        os << "[" << x.rules << "]";
        return os;
    }
};

/// Generated code for an arm
struct ArmCode {
    bool hasCondition = false;

    struct Pattern {
        /// Entrypoint for guard and destructuring
        MIRBasicBlockId entry = 0;
        /// Block jumped to by the guard code when the condition fails
        MIRBasicBlockId condFalse = ~0u;
    };

    std::vector<Pattern> rules;
};

typedef ::std::vector<PatternRuleset> tArmRules;

void allocatePatternDerefLocals(MirBuilder& builder, PatternRuleset& ruleset);
void materializePatternDerefs(MirBuilder& builder, const Span& sp, const PatternRuleset& ruleset, const HIRTypeData* topTy, const MIRLValue& topVal);
MIRLValue getPatternBindingValue(MirConverter& conv, const Span& sp, const PatternRuleset& ruleset, const HIRTypeData* topTy, const MIRLValue& topVal, const PatternBinding& binding);
void destructurePatternRuleset(MirConverter& conv, const Span& sp, const PatternRuleset& ruleset, const HIRTypeData* topTy, const MIRLValue& topVal, bool updateStates = true);

void MIRLowerHIRMatchSimple(MirBuilder& builder, MirConverter& conv, HIRExprNodeMatch& node, MIRLValue matchVal, tArmRules armRules, ::std::vector<ArmCode> armCode, MIRBasicBlockId firstCmpBlock);
int MIRLowerHIRMatchSimpleGeneratePattern(MirBuilder& builder, const Span& sp, const PatternRuleset* ruleset, const PatternRule* rules, unsigned int numRules, const HIRTypeData* topTy, const MIRLValue& topVal, unsigned int fieldPathOfs, MIRBasicBlockId failBb);
void MIRLowerHIRMatchGrouped(MirBuilder& builder, MirConverter& conv, const Span& sp, const HIRTypeData* matchTy, MIRLValue matchVal, tArmRules armRules, ::std::vector<ArmCode> armsCode, MIRBasicBlockId firstCmpBlock);
void MIRLowerHIRMatchDecisionTree(MirBuilder& builder, MirConverter& conv, HIRExprNodeMatch& node, MIRLValue matchVal, tArmRules armRules, ::std::vector<ArmCode> armCode, MIRBasicBlockId firstCmpBlock);

struct PatternSubsetCallback {
    virtual void visitSubset(size_t index) = 0;
};

template <typename F>
struct PatternSubsetCb final: PatternSubsetCallback {
    F f;

    explicit PatternSubsetCb(F f)
        : f(f)
    {
    }

    void visitSubset(size_t index) override {
        f(index);
    }
};

struct PatternTypeCallback {
    virtual const HIRTypeData* map(const HIRTypeData* type) = 0;
};

template <typename F>
struct PatternTypeCb final: PatternTypeCallback {
    F f;

    explicit PatternTypeCb(F f)
        : f(f)
    {
    }

    const HIRTypeData* map(const HIRTypeData* type) override {
        return f(type);
    }
};

/// Helper to construct rules from a passed pattern
struct PatternRulesetBuilder {
    const StaticTraitResolve& resolve;
    const HIRSimplePath* langBox = nullptr;

    struct WildcardType {
        const HIRTypeData* type;
        WildcardType* parent;
    };
    WildcardType* wildcardTypes;

    // NOTE: Multiple rulesets to handle or-patterns (which multiply the pattern set)
    struct Ruleset {
        bool isImpossible;
        ::std::vector<PatternRule> rules;
        ::std::vector<PatternBinding> bindings;
        ::std::vector<PatternRuleset::Deref> derefs;
        // Source-order path through nested or-pattern alternatives.  The
        // matching semantics are depth-first and left-to-right, so this path
        // orders the cartesian product after each expansion.
        ::std::vector<unsigned> orPath;

        Ruleset()
            : isImpossible(false)
        {
        }

        Ruleset clone() const {
            Ruleset rv;
            rv.isImpossible = isImpossible;
            for (const auto& e : rules) {
                rv.rules.push_back(e.clone());
            }
            rv.bindings = bindings;
            rv.derefs = derefs;
            rv.orPath = orPath;
            return rv;
        }
    };

    std::vector<Ruleset> rulesets;
    size_t subsetStart, subsetEnd;

    fieldPathT fieldPath;
    unsigned rootIndex = 0;
    unsigned nextRootStorage = 1;
    unsigned* nextRootIndex;

    PatternRulesetBuilder(const StaticTraitResolve& resolve, unsigned* sharedNextRootIndex = nullptr, WildcardType* parentWildcardTypes = nullptr)
        : resolve(resolve)
        , wildcardTypes(parentWildcardTypes)
        , rulesets(1)
        , subsetStart(0)
        , subsetEnd(1)
        , nextRootIndex(sharedNextRootIndex ? sharedNextRootIndex : &nextRootStorage)
    {
        if (resolve.hirCrate().langItems.count("owned_box") > 0) {
            langBox = &resolve.hirCrate().langItems.at("owned_box");
        }
    }

    void appendFromLit(const Span& sp, EncodedLiteralSlice lit, const HIRTypeData* ty);
    void appendFrom(const Span& sp, const HIRPattern& pat, const HIRTypeData* ty);

private:
    void pushRule(PatternRule r);
    void pushBinding(PatternBinding b);
    void pushBindings(std::vector<PatternBinding> b);
    void pushDerefs(std::vector<PatternRuleset::Deref> derefs);
    void setImpossible();

    void multiplyRulesetsWith(size_t n, PatternSubsetCallback& cb);

    template <typename F>
    void multiplyRulesets(size_t n, F f) {
        PatternSubsetCb<F> cb(f);
        multiplyRulesetsWith(n, cb);
    }
};

class RulesetRef {
    ::std::vector<PatternRuleset>* rulesVec = nullptr;
    RulesetRef* parent = nullptr;
    size_t parentOfs = 0; // If len == 0, this is the innner index, else it's the base
    size_t parentLen = 0;

public:
    RulesetRef(::std::vector<PatternRuleset>& rules)
        : rulesVec(&rules)
    {
    }

    RulesetRef(RulesetRef& parent, size_t start, size_t n)
        : parent(&parent)
        , parentOfs(start)
        , parentLen(n)
    {
    }

    RulesetRef(RulesetRef& parent, size_t idx)
        : parent(&parent)
        , parentOfs(idx)
    {
    }

    size_t size() const {
        if (rulesVec) {
            return rulesVec->size();
        } else if (parentLen) {
            return parentLen;
        } else {
            return parent->size();
        }
    }

    RulesetRef slice(size_t s, size_t n) {
        return RulesetRef(*this, s, n);
    }

    const ::std::vector<PatternRule>& operator[](size_t i) const {
        if (rulesVec) {
            return (*rulesVec)[i].rules;
        } else if (parentLen) {
            return (*parent)[parentOfs + i];
        } else {
            // Fun part - Indexes into inner patterns
            const auto& parentRule = (*parent)[i][parentOfs];
            if (const auto* re = parentRule.opt_Variant()) {
                return re->subRules;
            } else {
                throw "TODO";
            }
        }
    }

    void swap(size_t a, size_t b) {
        TRACE_FUNCTION_F(a << ", " << b);
        if (rulesVec) {
            ::std::swap((*rulesVec)[a], (*rulesVec)[b]);
        } else {
            assert(parent);
            if (parentLen) {
                parent->swap(parentOfs + a, parentOfs + b);
            } else {
                parent->swap(a, b);
            }
        }
    }
};

void sortRulesets(RulesetRef rulesets, size_t idx = 0);
void sortRulesetsInner(RulesetRef rulesets, size_t idx);

// --------------------------------------------------------------------
// CODE
// --------------------------------------------------------------------
/// `let` (also used for destructuring arguments) - Introduces arguments into the current scope
///
/// If `else_node` is non-null, a `_` "arm" is added to invoke that block (which must diverge)
void MIRLowerHIRLet(MirBuilder& builder, MirConverter& conv, const Span& sp, const HIRPattern& pat, MIRLValue val, const HIRExprNode* elseNode) {
    TRACE_FUNCTION;

    HIRTypeRef outerTy = builder.valType(sp, val);

    auto successNode = builder.newBbUnlinked();
    auto firstCmpBlock = builder.pauseCurBlock();

    // - Convert HIR pattern into ruleset
    std::vector<PatternRuleset> armRules;
    std::vector<ArmCode> armCode;

    auto patScope = builder.newScopeSplit(sp);

    auto patBuilder = PatternRulesetBuilder{builder.resolve()};
    patBuilder.appendFrom(sp, pat, outerTy);
    for (auto& sr : patBuilder.rulesets) {
        auto patIdx = static_cast<unsigned>(&sr - &patBuilder.rulesets.front());
        if (sr.isImpossible) {
            DEBUG("LET PAT #" << patIdx << " " << pat << " ==> IMPOSSIBLE [" << sr.rules << "]");
        } else {
            DEBUG("LET PAT #" << patIdx << " " << pat << " ==> [" << sr.rules << "]");
            armRules.push_back(PatternRuleset{patIdx, 0, mv$(sr.rules), mv$(sr.bindings), mv$(sr.derefs)});
            allocatePatternDerefLocals(builder, armRules.back());

            auto patNode = builder.newBbUnlinked();
            builder.setCurBlock(patNode);
            destructurePatternRuleset(conv, sp, armRules.back(), outerTy, val);
            builder.endSplitArm(sp, patScope, /*reachable=*/true);
            builder.endBlock(MIRTerminator::make_Goto(successNode));

            ArmCode::Pattern ap;
            ap.entry = patNode;
            ArmCode ac;
            ac.rules.push_back(ap);
            armCode.push_back(ac);
        }
    }
    builder.terminateScope(sp, mv$(patScope));
    if (elseNode) {
        // Emit a check (similar to match)
        // NOTE: This is handled by "HIR Lower" currently, seems to work well
        TODO(sp, "Handle let-else");
    }

    if (::std::any_of(armRules.begin(), armRules.end(), [](const auto& ruleset) { return !ruleset.derefs.empty(); })) {
        builder.setCurBlock(firstCmpBlock);
        auto failed = builder.newBbUnlinked();
        for (size_t i = 0; i < armRules.size(); i++) {
            auto& ruleset = armRules[i];
            materializePatternDerefs(builder, sp, ruleset, outerTy, val);
            MIRLowerHIRMatchSimpleGeneratePattern(builder, sp, &ruleset, ruleset.rules.data(), ruleset.rules.size(), outerTy, val, 0, failed);
            builder.endBlock(MIRTerminator::make_Goto(armCode[i].rules[0].entry));
            builder.setCurBlock(failed);
            failed = builder.newBbUnlinked();
        }
        builder.endBlock(MIRTerminator::make_Unreachable({}));
    } else {
        MIRLowerHIRMatchGrouped(builder, conv, sp, outerTy, mv$(val), mv$(armRules), mv$(armCode), firstCmpBlock);
    }

    builder.setCurBlock(successNode);
}

// Handles lowering non-trivial matches to MIR
// - Non-trivial means that there's more than one pattern
// - Trivial matches are handled using `MIR_LowerHIR_Let`
void MIRLowerHIRMatch(MirBuilder& builder, MirConverter& conv, HIRExprNodeMatch& node, MIRLValue matchVal, const std::vector<unsigned>& letElseInitializerTemps) {
    TRACE_FUNCTION;
    // NOTE: Lowers to the following pattern:
    // ```
    // loop {   // `match_scope`
    //         }
    //     }
    //         }
    //     }
    //         }
    //     }
    //     diverge()
    // }
    // ```

    // Indicates that an arm has a guard (which prevents most of the match optimisations from working)
    bool fallBackOnSimple = false;

    const auto& matchTy = node.value->resType;
    // Nothing inhabits `!`, so the value being matched cannot exist and no arm
    // can run: `match unimplemented!() { .. }` is dead code that still compiles.
    if (matchTy->is_Diverge()) {
        builder.endBlock(MIRTerminator::make_Unreachable({}));
        return;
    }
    auto resultVal = builder.newTemporary(node.resType);
    auto nextBlock = builder.newBbUnlinked();

    /// Top level scope for the match
    auto matchScope = builder.newScopeLoop(node.span());
    // Match bodies are emitted one after another, but each is reached from the
    // same pre-match state.  Keep their states in separate split arms; the
    // surrounding loop still owns drop-flag reinitialisation for a match that
    // is executed more than once.
    auto matchArmScope = builder.newScopeSplit(node.span());

    // 1. Stop the current block so we can generate code before generating the pattern matching code
    auto firstCmpBlock = builder.pauseCurBlock();

    /// Entries for each arm, containing the code to run for each
    ::std::vector<ArmCode> armCode;
    /// Final list of rules (flattened patterns), for all patterns
    tArmRules armRules;

    // For each arm, generate the contents of the logical `if pattern_matches { if guard { break body; } }`
    for (unsigned int armIdx = 0; armIdx < node.arms.size(); armIdx++) {
        TRACE_FUNCTION_FR("ARM " << armIdx, "ARM " << armIdx);
        /*const*/ auto& arm = node.arms[armIdx];
        const Span& sp = arm.code->span();

        // ---
        // Convert all patterns on this arm into flattened "rules"
        // ---
        auto firstArmRuleIdx = armRules.size();
        for (unsigned int patIdx = 0; patIdx < arm.patterns.size(); patIdx++) {
            const auto& pat = arm.patterns[patIdx];

            auto patBuilder = PatternRulesetBuilder{builder.resolve()};
            patBuilder.appendFrom(node.span(), pat, matchTy);
            size_t firstRule = armRules.size();
            for (auto& sr : patBuilder.rulesets) {
                size_t i = &sr - &patBuilder.rulesets.front();
                if (sr.isImpossible) {
                    DEBUG("ARM PAT (" << armIdx << "," << patIdx << " #" << i << ") " << pat << " ==> IMPOSSIBLE [" << sr.rules << "]");
                } else {
                    DEBUG("ARM PAT (" << armIdx << "," << patIdx << " #" << i << ") " << pat << " ==> [" << sr.rules << "]");
                    // Sort the binding lists, so we can check that the lists are compatible
                    ::std::sort(sr.bindings.begin(), sr.bindings.end(), [](const PatternBinding& a, const PatternBinding& b) {
                        return a.binding->slot < b.binding->slot;
                    });
                    // Ensure that all patterns binding to the same set of variables (only check the variables)
                    if (firstRule < armRules.size()) {
                        const auto& fr = armRules[firstRule];
                        ASSERT_BUG(sp, fr.bindings.size() == sr.bindings.size(), "Disagreement in bindings between pattern - {" << armRules[firstRule].bindings << "} vs {" << sr.bindings << "}");
                        for (size_t j = 0; j < fr.bindings.size(); j++) {
                            ASSERT_BUG(sp, fr.bindings[j].binding->slot == sr.bindings[j].binding->slot, "Disagreement in bindings between pattern - {" << armRules[firstRule].bindings << "} vs {" << sr.bindings << "}");
                        }
                    }
                    armRules.push_back(PatternRuleset{armIdx, static_cast<unsigned>(armRules.size() - firstArmRuleIdx), mv$(sr.rules), mv$(sr.bindings), mv$(sr.derefs)});
                    allocatePatternDerefLocals(builder, armRules.back());
                    if (!armRules.back().derefs.empty()) fallBackOnSimple = true;
                }
            }
        }

        ArmCode ac;

        /// Block allocated for the body code of this arm (jumped to after bindings are set)
        auto armBodyBlock = builder.newBbUnlinked();

        /// Block for when the first rule matches (contains the guard and binding setup for this rule)
        auto entryBlockPat0 = builder.newBbUnlinked();
        builder.setCurBlock(entryBlockPat0);

        // Split scope for the `if pattern_matches { }` outer arm,
        auto patScope = builder.newScopeSplit(node.span());
        builder.endSplitArm(sp, patScope, /*reachable=*/true); // Inject the `else` case first, this should not push any statements

        // Generate code for this arm (guard, destructuring, and body)
        {
            // Scopes present for the body (generated during guard processing)
            // - Temporary/variable scopes, and split scopes
            struct MatchScope {
                ScopeHandle handle;
                bool isSplit;
            };

            std::vector<MatchScope> scopes;

            const auto& bindings0 = armRules[firstArmRuleIdx].bindings;
            // Create aliases for every binding that only allows shared/immutable access (for use in the guard)
            auto aliases = builder.saveAliases();
            std::vector<unsigned> bindingTemps;
            for (const auto& b : bindings0) {
                HIRTypeRef finalTy = conv.getBindingType(sp, b.binding->slot);
                const Span& sp = arm.code->span();
                auto val = getPatternBindingValue(conv, sp, armRules[firstArmRuleIdx], matchTy, matchVal, b);
                DEBUG("Set alias for: " << *b.binding << " := " << val);
                if (b.binding->type != HIRPatternBinding::Type::Move) {
                    // A reference binding has an identity which is observable
                    // in both the guard and the arm (`&binding`). Initialise
                    // its real local before the guard, but don't activate its
                    // arm drop state until the pattern is selected. The guard
                    // below only receives a shared alias to this local.
                    const auto borrow = b.binding->type == HIRPatternBinding::Type::MutRef ? HIRBorrowType::Unique : HIRBorrowType::Shared;
                    auto bindingVal = builder.getVariable(sp, b.binding->slot);
                    builder.pushStmtAssign(sp, bindingVal.clone(), MIRRValue::make_Borrow({borrow, false, std::move(val)}), /*updateState=*/false);
                    val = std::move(bindingVal);
                }
                // Allocate a temporary to hold a borrow of that type
                auto tmp = builder.newTemporary(builder.resolve().crate.types.borrow(HIRBorrowType::Shared, finalTy));
                // - Store the temporary index so later copies can write to it
                bindingTemps.push_back(tmp.as_Local());
                // Assign the temporary with a borrow of the other slot
                builder.pushStmtAssign(sp, tmp.clone(), MIRRValue::make_Borrow({HIRBorrowType::Shared, false, std::move(val)}));
                // And set an alias to point to `*temp`
                builder.addVariableAlias(sp, b.binding->slot, HIRPatternBinding::Type::Move, MIRLValue::newDeref(std::move(tmp)));
            }

            // A guard is emitted once and cloned for the remaining rules.  Until
            // the saved code is complete, keep a diverging guard's state changes
            // off the fall-through path used to emit those clones.
            bool shouldFreeze = (!arm.guards.empty() && firstArmRuleIdx + 1 < armRules.size());
            scopes.push_back({builder.newScopeFreeze(sp), false});
            if (!shouldFreeze) {
                builder.unfreezeScope(sp, scopes.front().handle);
            }

            // Block at the start of the saved guard data
            auto block0 = builder.pauseCurBlock();
            builder.setCurBlock(block0);
            // Start saving code (the copyable part of the guard, after the assignment of the binding temporaries)
            auto csH = builder.codeSaveStart();
            MIRBasicBlockId condFalseBlockPat0 = ~0u;
            bool guardDiverged = false;
            // Emit the condtion using the first set of bindings
            if (!arm.guards.empty()) {
                auto _dbe = conv.disableBorrowExtension();
                // Emit the guard code
                TRACE_FUNCTION_FR("CONDITIONAL", "CONDITIONAL");

                // The guards are chanined, and all must match for the arm to be taken
                // I.e. These are ANDs
                for (auto& c : arm.guards) {
                    const Span& sp = c.val->span();
                    // Emit the logical `if !guard { } else { ... }`

                    /// Block for when this guard successfully matches
                    auto destructure = builder.newBbUnlinked();

                    // Make a temp scope and push
                    scopes.push_back({builder.newScopeTemp(c.val->span()), false});
                    conv.visitNodePtr(c.val);
                    if (!builder.blockActive()) {
                        guardDiverged = true;
                        break;
                    }
                    MIRLValue matchCondVal = builder.getResultInLvalue(c.val->span(), c.val->resType);
                    DEBUG("GUARD " << c.pat << " = " << matchCondVal);

                    // If this is not a pattern-match, terminate the temporary scope here
                    if (c.isIf) {
                        auto t = builder.newTemporary(c.val->resType);
                        builder.pushStmtAssign(c.val->span(), t.clone(), std::move(matchCondVal));
                        matchCondVal = std::move(t);
                        builder.terminateScope(sp, std::move(scopes.back().handle));
                        scopes.pop_back();
                    }

                    // Generate simplified rules from patterns
                    auto patBuilder = PatternRulesetBuilder{builder.resolve()};
                    patBuilder.appendFrom(node.span(), c.pat, c.val->resType);
                    ::std::vector<PatternRuleset> conditionRulesets;
                    conditionRulesets.reserve(patBuilder.rulesets.size());
                    for (auto& ruleset : patBuilder.rulesets) {
                        if (!ruleset.isImpossible) {
                            conditionRulesets.push_back(PatternRuleset{0, 0, mv$(ruleset.rules), mv$(ruleset.bindings), mv$(ruleset.derefs)});
                            allocatePatternDerefLocals(builder, conditionRulesets.back());
                        }
                    }

                    /// Block for when a pattern fails to match
                    auto localFalse = builder.newBbUnlinked();
                    bool localFalseUsed = false;
                    // OR'd patterns
                    ::std::vector<std::pair<MIRBasicBlockId, const PatternRuleset*>> ends;
                    for (auto& sr : conditionRulesets) {
                        if (localFalseUsed) {
                            localFalse = builder.newBbUnlinked();
                        }

                        ASSERT_BUG(c.val->span(), builder.blockActive(), "Block not active");
                        materializePatternDerefs(builder, c.val->span(), sr, c.val->resType, matchCondVal);
                        MIRLowerHIRMatchSimpleGeneratePattern(builder, c.val->span(), &sr, sr.rules.data(), sr.rules.size(), c.val->resType, matchCondVal, 0, localFalse);
                        ends.push_back(std::make_pair(builder.pauseCurBlock(), &sr));
                        builder.setCurBlock(localFalse);
                        localFalseUsed = true;
                    }
                    if (!localFalseUsed) {
                        // None of the patterns were possible?
                        TODO(sp, "No possible arms in a `if-let` guard?");
                    }
                    if (condFalseBlockPat0 == ~0u) {
                        condFalseBlockPat0 = builder.newBbUnlinked();
                    }
                    // Split scope for the body of this logical `if`
                    scopes.push_back({builder.newScopeSplit(sp), true});
                    builder.endSplitArm(sp, scopes.back().handle, true);
                    // Currently in `local_false`
                    DEBUG("GUARD: Clean up and jump to `cond_false`");
                    // End the top scope early, which also handles ending all intervening scopes
                    builder.terminateScopeEarly(sp, scopes.front().handle);
                    // Indicate an exit point to the split
                    builder.endSplitArm(arm.code->span(), patScope, /*reachable*/ true, /*early*/ true);
                    // A failed guard continues with the following match arm.
                    // Preserve moves performed while evaluating the guard as
                    // that arm's entry state; arm bodies remain isolated.
                    builder.endSplitCondition(sp, patScope, matchArmScope);
                    builder.endBlock(MIRTerminator::make_Goto(condFalseBlockPat0));

                    // Introduce a local variable scope for the new bindings
                    scopes.push_back({builder.newScopeVar(c.val->span()), false});
                    conv.schedulePatternDrops(c.val->span(), c.pat, PatternDropOrder::FirstCandidate);

                    // Only introduce the new bindings (with `destructure_from_list`) after handling the early-exit case
                    // - This stops the `terminate_scope_early` from dropping too eagerly
                    for (const auto& e : ends) {
                        builder.setCurBlock(e.first);
                        destructurePatternRuleset(conv, arm.code->span(), *e.second, c.val->resType, matchCondVal, /*updateStates=*/&e == ends.data());
                        builder.endBlock(MIRTerminator::make_Goto(destructure));
                    }

                    ASSERT_BUG(node.span(), !builder.blockActive(), "Block still active?");
                    builder.setCurBlock(destructure);
                }
            }
            if (guardDiverged) {
                if (shouldFreeze) {
                    builder.unfreezeScope(sp, scopes.front().handle);
                }
                builder.restoreAliases(std::move(aliases));
                auto guardCode = builder.codeSaveEnd(std::move(csH));

                while (!scopes.empty()) {
                    builder.terminateScope(arm.code->span(), std::move(scopes.back().handle), false);
                    scopes.pop_back();
                }
                builder.endSplitArm(arm.code->span(), patScope, /*reachable=*/false);
                builder.terminateScope(sp, std::move(patScope), false);
                builder.endSplitArm(sp, matchArmScope, /*reachable=*/false);

                ac.rules.push_back(ArmCode::Pattern{entryBlockPat0, ~0u});
                for (size_t i = firstArmRuleIdx + 1; i < armRules.size(); i++) {
                    struct DivergingGuardMapper: public MirBuilder::CloneMapper {
                        MIRBasicBlockId block0;

                        DivergingGuardMapper(MIRBasicBlockId block0)
                            : block0(block0)
                        {
                        }

                        MIRBasicBlockId updateBbRef(MIRBasicBlockId bbIdx) override {
                            if (bbIdx < block0) {
                                return bbIdx;
                            }
                            BUG(Span(), "Diverging guard referenced unsaved block bb" << bbIdx << " after bb" << block0);
                        }
                    } mapper(block0);

                    auto entryBlock = builder.newBbUnlinked();
                    builder.setCurBlock(entryBlock);
                    ASSERT_BUG(sp, bindingTemps.size() == armRules[i].bindings.size(), "Mismatched guard bindings");
                    for (size_t j = 0; j < bindingTemps.size(); j++) {
                        const auto& b = armRules[i].bindings[j];
                        auto val = getPatternBindingValue(conv, sp, armRules[i], matchTy, matchVal, b);
                        if (b.binding->type != HIRPatternBinding::Type::Move) {
                            const auto borrow = b.binding->type == HIRPatternBinding::Type::MutRef ? HIRBorrowType::Unique : HIRBorrowType::Shared;
                            auto bindingVal = builder.getVariable(sp, b.binding->slot);
                            builder.pushStmtAssign(sp, bindingVal.clone(), MIRRValue::make_Borrow({borrow, false, std::move(val)}), /*updateState=*/false);
                            val = std::move(bindingVal);
                        }
                        builder.pushStmtAssign(sp, MIRLValue::newLocal(bindingTemps[j]), MIRRValue::make_Borrow({HIRBorrowType::Shared, false, std::move(val)}));
                    }
                    builder.insertCloned(sp, guardCode, mapper);
                    ASSERT_BUG(sp, !builder.blockActive(), "Diverging guard clone remained reachable");
                    ac.rules.push_back(ArmCode::Pattern{entryBlock, ~0u});
                }

                ac.hasCondition = false;
                fallBackOnSimple = true;
                armCode.push_back(std::move(ac));
                continue;
            }
            // Release the freezing of outer states
            if (shouldFreeze) {
                // NOTE: The first scope should be the freeze
                builder.unfreezeScope(sp, scopes.front().handle);
            }
            // And undo aliases
            builder.restoreAliases(std::move(aliases));
            auto guardEndBlock = builder.newBbUnlinked();
            builder.endBlock(MIRTerminator::make_Goto(guardEndBlock));
            auto guardCode = builder.codeSaveEnd(std::move(csH));
            builder.setCurBlock(guardEndBlock);
            // Emit actual bindings
            DEBUG("Arm " << armIdx << " rule " << 0 << ":  Destructure");
            scopes.push_back({builder.newScopeVar(arm.code->span()), false});
            conv.schedulePatternDrops(node.span(), arm.patterns.back(), PatternDropOrder::LastCandidate);
            auto bindingSplit = builder.newScopeSplit(arm.code->span());
            destructurePatternRuleset(conv, arm.code->span(), armRules[firstArmRuleIdx], matchTy, matchVal);
            builder.endSplitArm(arm.code->span(), bindingSplit, /*reachable=*/true);
            builder.endBlock(MIRTerminator::make_Goto(armBodyBlock));

            // The first rule just uses the code generated above
            {
                ArmCode::Pattern acp;
                acp.entry = entryBlockPat0;
                acp.condFalse = condFalseBlockPat0;
                ac.rules.push_back(acp);
            }
            // Subsequent rules clone the guard with different values for the bindings, and (importantly) a different failure exit point
            for (size_t i = firstArmRuleIdx + 1; i < armRules.size(); i++) {
                TRACE_FUNCTION_FR("Bindings (AR" << i << ")", "Bindings (AR" << i << ")");

                // Clone guard code, with the two exit blocks updated, and references updated
                struct Mapper: public MirBuilder::CloneMapper {
                    MIRBasicBlockId block0;
                    MIRBasicBlockId condFalse;
                    MIRBasicBlockId condTrue;
                    MIRBasicBlockId newCondFalse;
                    MIRBasicBlockId newCondTrue;

                    Mapper(MirBuilder& builder, MIRBasicBlockId block0, MIRBasicBlockId condFalse, MIRBasicBlockId condTrue)
                        : block0(block0)
                        , condFalse(condFalse)
                        , condTrue(condTrue)
                        , newCondFalse(builder.newBbUnlinked())
                        , newCondTrue(builder.newBbUnlinked())
                    {
                        DEBUG("new_cond_false=" << newCondFalse << ", new_cond_true=" << newCondTrue);
                    }

                    MIRBasicBlockId updateBbRef(MIRBasicBlockId bbIdx) {
                        // Any block defined before the save just propagates through
                        // E.g. if the guard contains a `break`
                        if (bbIdx < block0) {
                            return bbIdx;
                        }
                        if (bbIdx == condFalse) {
                            return newCondFalse;
                        }
                        if (bbIdx == condTrue) {
                            return newCondTrue;
                        }
                        BUG(Span(),
                            "update_bb_ref: Unknown BB " << bbIdx << " "
                                                         << ": block0=" << block0 << ", cond_false=" << condFalse << ", cond_true=" << condTrue);
                    }
                } mapper(builder, block0, condFalseBlockPat0, guardEndBlock);

                auto entryBlock = builder.newBbUnlinked();
                builder.setCurBlock(entryBlock);
                // Set the binding temporaries with the correct borrows
                assert(bindingTemps.size() == armRules[i].bindings.size());
                for (size_t j = 0; j < bindingTemps.size(); j++) {
                    const auto& b = armRules[i].bindings[j];
                    auto val = getPatternBindingValue(conv, sp, armRules[i], matchTy, matchVal, b);
                    DEBUG("Set alias for: " << *b.binding << " := " << val);
                    if (b.binding->type != HIRPatternBinding::Type::Move) {
                        const auto borrow = b.binding->type == HIRPatternBinding::Type::MutRef ? HIRBorrowType::Unique : HIRBorrowType::Shared;
                        auto bindingVal = builder.getVariable(sp, b.binding->slot);
                        builder.pushStmtAssign(sp, bindingVal.clone(), MIRRValue::make_Borrow({borrow, false, std::move(val)}), /*updateState=*/false);
                        val = std::move(bindingVal);
                    }
                    builder.pushStmtAssign(sp, MIRLValue::newLocal(bindingTemps[j]), MIRRValue::make_Borrow({HIRBorrowType::Shared, false, std::move(val)}));
                }
                // Clone the guard contents with updated block references
                builder.insertCloned(sp, guardCode, mapper);

                // Add the final bindings and jump to the body
                builder.setCurBlock(mapper.newCondTrue);
                DEBUG("Arm " << armIdx << " rule " << i - firstArmRuleIdx << ":  Destructure");
                destructurePatternRuleset(conv, arm.code->span(), armRules[i], matchTy, matchVal);
                builder.endSplitArm(arm.code->span(), bindingSplit, /*reachable=*/true);
                builder.endBlock(MIRTerminator::make_Goto(armBodyBlock));

                ArmCode::Pattern acp;
                acp.entry = entryBlock;
                acp.condFalse = mapper.newCondFalse;
                ac.rules.push_back(acp);
            }

            // All successful pattern alternatives enter the same body. Merge
            // their move states first so the body and every unwind edge use
            // drop flags valid for every predecessor.
            builder.terminateScope(arm.code->span(), std::move(bindingSplit), /*emit_cleanup=*/false);

            // Emit body code
            DEBUG("-- Body Code");

            scopes.push_back({builder.newScopeTemp(arm.code->span()), false});
            builder.setCurBlock(armBodyBlock);

            if (node.isLetElse && armIdx + 1 == node.arms.size()) {
                for (const auto temporary : ::reverse(letElseInitializerTemps)) {
                    builder.dropLvalue(node.span(), MIRLValue::newLocal(temporary));
                }
            }

            conv.visitNodePtr(arm.code);

            if (builder.blockActive()) {
                // - Set result
                auto res = builder.getResult(arm.code->span());
                builder.pushStmtAssign(arm.code->span(), resultVal.clone(), mv$(res));
            } else {
                assert(!builder.hasResult());
            }
            // Pop/end scopes
            while (!scopes.empty()) {
                if (scopes.back().isSplit) {
                    builder.endSplitArm(arm.code->span(), scopes.back().handle, /*reachable*/ builder.blockActive());
                }
                builder.terminateScope(arm.code->span(), std::move(scopes.back().handle), builder.blockActive());
                scopes.pop_back();
            }
            builder.endSplitArm(arm.code->span(), patScope, /*reachable*/ builder.blockActive());
            builder.terminateScope(sp, std::move(patScope), builder.blockActive());
            builder.endSplitArm(sp, matchArmScope, /*reachable=*/builder.blockActive());

            // Go to the next block (out of the match) (if the body didn't diverge)
            if (builder.blockActive()) {
                builder.endBlock(MIRTerminator::make_Goto(nextBlock));
            }
        }

        // If there is a guard, then flag
        if (!arm.guards.empty()) {
            ac.hasCondition = true;

            // TODO: What to do with conditionals in the fast model?
            // > Could split the match on each conditional - separating such that if a conditional fails it can fall into the other compatible branches.
            // For now: Disable the complex logic, and fall back to a sequence of checks.
            fallBackOnSimple = true;
        } else {
            ac.hasCondition = false;
        }

        armCode.push_back(std::move(ac));
    }

    // Nothing inhabits the value being matched (`match unimplemented!()`), so
    // every arm is impossible and there is no rule to sort or test.
    if (armRules.empty()) {
        builder.setCurBlock(firstCmpBlock);
        builder.endBlock(MIRTerminator::make_Unreachable({}));
        return;
    }

    // Sort columns of `arm_rules` to maximise effectiveness
    if (!fallBackOnSimple && armRules[0].rules.size() > 1) {
        // TODO: Should columns be sorted within equal sub-arms too?
        ::std::vector<unsigned> columnWeights(armRules[0].rules.size());
        for (const auto& armRule : armRules) {
            ASSERT_BUG(node.span(), columnWeights.size() == armRule.rules.size(), "Arm " << (&armRule - &armRules.front()) << " size doesn't match first (" << armRule.rules.size() << " != " << columnWeights.size() << ")");
            for (unsigned int i = 0; i < armRule.rules.size(); i++) {
                if (!armRule.rules[i].is_Any()) {
                    columnWeights.at(i) += 1;
                }
            }
        }

        DEBUG("- Column weights = [" << columnWeights << "]");
        // - Sort columns such that the largest (most specific) comes first
        ::std::vector<unsigned> columnsSorted(columnWeights.size());
        ::std::iota(columnsSorted.begin(), columnsSorted.end(), 0);
        ::std::sort(columnsSorted.begin(), columnsSorted.end(), [&](auto a, auto b) {
            return columnWeights[a] > columnWeights[b];
        });
        DEBUG("- Sorted to = [" << columnsSorted << "]");
        for (auto& armRule : armRules) {
            assert(columnsSorted.size() == armRule.rules.size());
            ::std::vector<PatternRule> sorted;
            sorted.reserve(columnsSorted.size());
            for (auto idx : columnsSorted) {
                sorted.push_back(mv$(armRule.rules[idx]));
            }
            armRule.rules = mv$(sorted);
        }
    }

    for (const auto& armRule : armRules) {
        DEBUG("> (" << armRule.armIdx << ", " << armRule.armRuleIdx << ") - " << armRule.rules << (armCode[armRule.armIdx].hasCondition ? " (cond)" : ""));
    }

    // TODO: Remove columns that are all `_`?
    // - Ideally, only accessible structures would be fully destructured like this, making this check redundant

    // Sort rules using the following restrictions:
    // - A rule cannot be reordered across an item that has an overlapping match set
    //  > e.g. nothing can cross _
    //  > equal rules cannot be reordered
    //  > Values cannot cross ranges that contain the value
    //  > This will have to be a bubble sort to ensure that it's correctly stable.
    if (!fallBackOnSimple) {
        sortRulesets(armRules);
        DEBUG("Post-sort");
        for (const auto& armRule : armRules) {
            DEBUG("> (" << armRule.armIdx << ", " << armRule.armRuleIdx << ") - " << armRule.rules << (armCode[armRule.armIdx].hasCondition ? " (cond)" : ""));
        }
    }
    // De-duplicate arms (emitting a warning when it happens)
    // - This allows later code to assume that duplicate arms are a codegen bug.
    if (!armRules.empty()) {
        for (auto it = armRules.begin() + 1; it != armRules.end();) {
            // If duplicate rule, (and neither is conditional)
            if ((it - 1)->rules == it->rules && !armCode[it->armIdx].hasCondition && !armCode[(it - 1)->armIdx].hasCondition) {
                WARNING(node.arms[it->armIdx].code->span(), W0000, "Duplicate match pattern, unreachable code" << "\n - Pattern : " << PatternDump(builder.resolve(), matchTy, it->rules) << "\n - Previous at " << node.arms[(it - 1)->armIdx].code->span());
                // Remove
                it = armRules.erase(it);
            } else {
                ++it;
            }
        }
    }

    // TODO: Combine identical-pattern arms, allowing potential use of condtionals
    // -
    // If there's a conditional that isn't grouped with an unconditional pattern - then force fallback

    // TODO: SplitSlice is buggy, make it fall back to simple?

    // TODO: Don't generate inner code until decisions are generated (keeps MIR flow nice)
    // - Challenging, as the decision code needs somewhere to jump to.
    // - Allocating a BB and then rewriting references to it is a possibility.

    if (fallBackOnSimple) {
        MIRLowerHIRMatchSimple(builder, conv, node /*.span(), match_ty*/, mv$(matchVal), mv$(armRules), mv$(armCode), firstCmpBlock);
    } else {
        MIRLowerHIRMatchGrouped(builder, conv, node.span(), matchTy, mv$(matchVal), mv$(armRules), mv$(armCode), firstCmpBlock);
    }

    builder.setCurBlock(nextBlock);
    builder.terminateScope(node.span(), mv$(matchArmScope), /*emit_cleanup=*/false);
    builder.setResult(node.span(), mv$(resultVal));
    builder.terminateScope(node.span(), mv$(matchScope));
}

// --------------------------------------------------------------------
// Common Code - Pattern Rules
// --------------------------------------------------------------------
::std::ostream& operator<<(::std::ostream& os, const PatternRule& x) {
    os << "{root" << x.rootIndex << ":" << x.fieldPath << "}=";
    switch (x.tag()) {
        case PatternRule::TAG_Any: {
            os << "_";
            break;
        }
        case PatternRule::TAG_Variant: {
            auto& e = x.as_Variant();
            os << e.idx << " [" << e.subRules << "]";
            break;
        }
        case PatternRule::TAG_Slice: {
            auto& e = x.as_Slice();
            os << "len=" << e.len << " [" << e.subRules << "]";
            break;
        }
        case PatternRule::TAG_SplitSlice: {
            auto& e = x.as_SplitSlice();
            os << "len>=" << e.minLen << " [" << e.leading << ", ..., " << e.trailing << "]";
            break;
        }
        case PatternRule::TAG_Bool: {
            auto& e = x.as_Bool();
            os << (e ? "true" : "false");
            break;
        }
        case PatternRule::TAG_Value: {
            auto& e = x.as_Value();
            os << e;
            break;
        }
        case PatternRule::TAG_ValueRange: {
            auto& e = x.as_ValueRange();
            os << e.first << " .." << (e.isInclusive ? "=" : "") << " " << e.last;
            break;
        }
    }
    return os;
}

namespace {
    // Two rules for the same variant do not have to carry the same number of
    // sub-rules: a deref pattern matches against a root of its own, so
    // `Some("42")` (against `Option<String>`) has one sub-rule where `Some(_)`
    // has one per field of the string. Order on what the two share, then on
    // how much there is.
    ::Ordering ordSubRules(const ::std::vector<PatternRule>& a, const ::std::vector<PatternRule>& b) {
        const size_t n = ::std::min(a.size(), b.size());
        for (size_t i = 0; i < n; i++) {
            auto cmp = a[i].ord(b[i]);
            if (cmp != ::OrdEqual) {
                return cmp;
            }
        }
        return ::ord(static_cast<unsigned>(a.size()), static_cast<unsigned>(b.size()));
    }
}  // namespace

::Ordering PatternRule::ord(const PatternRule& x) const {
    ORD(static_cast<int>(tag()), static_cast<int>(x.tag()));
    ORD(this->rootIndex, x.rootIndex);
    ORD(this->fieldPath, x.fieldPath);

    switch ((*this).tag()) {
        case PatternRule::TAG_Any: {
            return OrdEqual;
        }
        case PatternRule::TAG_Variant: {
            auto& te = (*this).as_Variant();
            auto& xe = x.as_Variant();
            if (te.idx != xe.idx) {
                return ::ord(te.idx, xe.idx);
            }
            return ordSubRules(te.subRules, xe.subRules);
        }
        case PatternRule::TAG_Slice: {
            auto& te = (*this).as_Slice();
            auto& xe = x.as_Slice();
            if (te.len != xe.len) {
                return ::ord(te.len, xe.len);
            }
            return ordSubRules(te.subRules, xe.subRules);
        }
        case PatternRule::TAG_SplitSlice: {
            auto& te = (*this).as_SplitSlice();
            auto& xe = x.as_SplitSlice();
            ORD(te.leading, xe.leading);
            ORD(te.minLen, xe.minLen);
            return ::ord(te.trailing, xe.trailing);
        }
        case PatternRule::TAG_Bool: {
            auto& te = (*this).as_Bool();
            auto& xe = x.as_Bool();
            return ::ord(te, xe);
        }
        case PatternRule::TAG_Value: {
            auto& te = (*this).as_Value();
            auto& xe = x.as_Value();
            return ::ord(te, xe);
        }
        case PatternRule::TAG_ValueRange: {
            auto& te = (*this).as_ValueRange();
            auto& xe = x.as_ValueRange();
            ORD(te.first, xe.first);
            ORD(te.last, xe.last);
            return ::ord(te.isInclusive, xe.isInclusive);
        }
    }
    throw "";
}

PatternRule PatternRule::clone() const {
    struct H {
        static std::vector<PatternRule> cloneList(const std::vector<PatternRule>& l) {
            std::vector<PatternRule> rv;
            for (const auto& e : l) {
                rv.push_back(e.clone());
            }
            return rv;
        }

        static PatternRule cloneInner(const PatternRule& t) {
            switch (t.tag()) {
                case PatternRule::TAG_Any: {
                    auto& te = t.as_Any();
                    return te;
                }
                case PatternRule::TAG_Variant: {
                    auto& te = t.as_Variant();
                    return PatternRule::make_Variant({te.idx, H::cloneList(te.subRules)});
                }
                case PatternRule::TAG_Slice: {
                    auto& te = t.as_Slice();
                    return PatternRule::make_Slice({te.len, H::cloneList(te.subRules)});
                }
                case PatternRule::TAG_SplitSlice: {
                    auto& te = t.as_SplitSlice();
                    return PatternRule::make_SplitSlice({te.minLen, te.trailingLen, H::cloneList(te.leading), H::cloneList(te.trailing)});
                }
                case PatternRule::TAG_Bool: {
                    auto& te = t.as_Bool();
                    return te;
                }
                case PatternRule::TAG_Value: {
                    auto& te = t.as_Value();
                    return te.clone();
                }
                case PatternRule::TAG_ValueRange: {
                    auto& te = t.as_ValueRange();
                    return PatternRule::make_ValueRange({te.first.clone(), te.last.clone(), te.isInclusive});
                }
            }
            throw "";
        }
    };

    auto rv = H::cloneInner(*this);
    rv.fieldPath = this->fieldPath;
    rv.rootIndex = this->rootIndex;
    return rv;
}

::Ordering PatternRuleset::ruleIsBefore(const PatternRule& l, const PatternRule& r) {
    if (l.tag() != r.tag()) {
        // Any comes last, don't care about rest
        if (l.tag() < r.tag()) {
            return ::OrdGreater;
        } else {
            return ::OrdLess;
        }
    }

    switch (l.tag()) {
        case PatternRule::TAG_Any: {
            return ::OrdEqual;
        }
        case PatternRule::TAG_Variant: {
            auto& le = l.as_Variant();
            auto& re = r.as_Variant();
            if (le.idx != re.idx) {
                return ::ord(le.idx, re.idx);
            }
            assert(le.subRules.size() == re.subRules.size());
            for (unsigned int i = 0; i < le.subRules.size(); i++) {
                auto cmp = ruleIsBefore(le.subRules[i], re.subRules[i]);
                if (cmp != ::OrdEqual) {
                    return cmp;
                }
            }
            return ::OrdEqual;
        }
        case PatternRule::TAG_Slice: {
            auto& le = l.as_Slice();
            auto& re = r.as_Slice();
            if (le.len != re.len) {
                return ::ord(le.len, re.len);
            }
            // Wait? Why would the rule count be the same?
            assert(le.subRules.size() == re.subRules.size());
            for (unsigned int i = 0; i < le.subRules.size(); i++) {
                auto cmp = ruleIsBefore(le.subRules[i], re.subRules[i]);
                if (cmp != ::OrdEqual) {
                    return cmp;
                }
            }
            return ::OrdEqual;
        }
        case PatternRule::TAG_SplitSlice: {
            TODO(Span(), "Order PatternRule::SplitSlice");
            break;
        }
        case PatternRule::TAG_Bool: {
            auto& le = l.as_Bool();
            auto& re = r.as_Bool();
            return ::ord(le, re);
        }
        case PatternRule::TAG_Value: {
            TODO(Span(), "Order PatternRule::Value");
            break;
        }
        case PatternRule::TAG_ValueRange: {
            TODO(Span(), "Order PatternRule::ValueRange");
            break;
        }
    }
    throw "";
}

bool PatternRuleset::isBefore(const PatternRuleset& other) const {
    assert(rules.size() == other.rules.size());
    for (unsigned int i = 0; i < rules.size(); i++) {
        const auto& l = rules[i];
        const auto& r = other.rules[i];
        auto cmp = ruleIsBefore(l, r);
        if (cmp != ::OrdEqual) {
            return cmp == ::OrdLess;
        }
    }
    return false;
}

void PatternRulesetBuilder::pushRule(PatternRule r) {
    assert(this->subsetStart < this->subsetEnd);
    assert(this->subsetEnd <= rulesets.size());
    for (size_t i = subsetStart; i < subsetEnd; i++) {
        rulesets[i].rules.push_back(i == subsetEnd - 1 ? std::move(r) : r.clone());
        rulesets[i].rules.back().fieldPath = fieldPath;
        rulesets[i].rules.back().rootIndex = rootIndex;
    }
}

void PatternRulesetBuilder::pushBinding(PatternBinding b) {
    assert(this->subsetStart < this->subsetEnd);
    assert(this->subsetEnd <= rulesets.size());
    for (size_t i = subsetStart; i < subsetEnd; i++) {
        DEBUG(i << " " << b);
        b.rootIndex = rootIndex;
        rulesets[i].bindings.push_back(b);
    }
}

void PatternRulesetBuilder::pushBindings(std::vector<PatternBinding> bindings) {
    assert(this->subsetStart < this->subsetEnd);
    assert(this->subsetEnd <= rulesets.size());
    for (size_t i = subsetStart; i < subsetEnd; i++) {
        auto& l = rulesets[i].bindings;
        l.insert(l.end(), bindings.begin(), bindings.end());
        DEBUG(i << " [" << bindings << "] = [" << l << "]");
    }
}

void PatternRulesetBuilder::pushDerefs(std::vector<PatternRuleset::Deref> derefs) {
    assert(this->subsetStart < this->subsetEnd);
    assert(this->subsetEnd <= rulesets.size());
    for (size_t i = subsetStart; i < subsetEnd; i++) {
        auto& out = rulesets[i].derefs;
        out.insert(out.end(), derefs.begin(), derefs.end());
    }
}

void PatternRulesetBuilder::setImpossible() {
    assert(this->subsetStart < this->subsetEnd);
    assert(this->subsetEnd <= rulesets.size());
    for (size_t i = subsetStart; i < subsetEnd; i++) {
        rulesets[i].isImpossible = true;
    }
}

/// Multiply the current subset of the ruleset, then visit every new subset
void PatternRulesetBuilder::multiplyRulesetsWith(size_t n, PatternSubsetCallback& cb) {
    assert(n > 0);
    if (n == 1) {
        cb.visitSubset(0);
        return;
    }
    TRACE_FUNCTION_F(n);
    assert(this->subsetStart < this->subsetEnd);
    assert(this->subsetEnd <= rulesets.size());
    size_t subsetSize = this->subsetEnd - this->subsetStart;
    size_t ofs = (n - 1) * subsetSize;
    assert(ofs > 0);
    size_t newSubsetEnd = this->subsetStart + n * subsetSize;
    size_t nTail = rulesets.size() - this->subsetEnd;
    DEBUG("subset_size=" << subsetSize << ", ofs = " << ofs << ", n_tail=" << nTail);
    rulesets.resize(rulesets.size() + (n - 1) * subsetSize);
    assert(newSubsetEnd == rulesets.size() - nTail);
    // Copy the tail out of the way (reverse to avoid chasing itself)
    for (size_t i = rulesets.size(); i-- > newSubsetEnd;) {
        rulesets[i] = std::move(rulesets[i - ofs]);
    }
    // Copy `n-1` copies of the current subset after itself
    for (size_t j = 1; j < n; j++) {
        for (size_t i = 0; i < subsetSize; i++) {
            const auto& src = rulesets[this->subsetStart + i];
            rulesets[this->subsetStart + j * subsetSize + i] = src.clone();
        }
    }
    for (size_t j = this->subsetStart + subsetSize; j < newSubsetEnd; j += subsetSize) {
        for (size_t i = 0; i < subsetSize; i++) {
            const auto& exp = rulesets[this->subsetStart + i];
            const auto& a = rulesets[j + i];
            ASSERT_BUG(Span(), a.rules == exp.rules, "BUG: {" << a.rules << "} != {" << exp.rules << "}");
            ASSERT_BUG(Span(), a.bindings == exp.bindings, "BUG: {" << a.bindings << "} != {" << exp.bindings << "}");
        }
    }
    for (size_t i = this->subsetStart; i < newSubsetEnd; i += 1) {
        DEBUG("#" << i << " rules=[" << rulesets[i].rules << "], bindings=[" << rulesets[i].bindings << "]");
    }

    // Iterate the new subsets
    size_t savedStart = this->subsetStart;
    this->subsetEnd = this->subsetStart;
    for (size_t i = 0; i < n; i++) {
        auto origStart = this->subsetStart;
        this->subsetEnd += subsetSize;
        DEBUG("++ " << i << " " << this->subsetStart << " - " << this->subsetEnd);
        for (size_t j = this->subsetStart; j < this->subsetEnd; j++) {
            rulesets[j].orPath.push_back(static_cast<unsigned>(i));
        }
        cb.visitSubset(i);
        DEBUG("-- " << i);
        assert(this->subsetStart == origStart);                    // This should always be unchanged (even if the callback splits again). The end can change though.
        assert(this->subsetEnd >= this->subsetStart + subsetSize); // The end should always be at least equal to start + size (i.e. hasn't shrunk)
        this->subsetStart = this->subsetEnd;
    }
    // Update the subset again to cover everything
    this->subsetStart = savedStart;
    ::std::stable_sort(rulesets.begin() + this->subsetStart, rulesets.begin() + this->subsetEnd, [](const Ruleset& a, const Ruleset& b) {
        return a.orPath < b.orPath;
    });
    // NOTE: Can't asser that the end is as-expected, as there might be inner subsets created that makes this assumption no longer valid
    for (size_t i = this->subsetStart; i < this->subsetEnd; i += 1) {
        DEBUG("#" << i << " rules=[" << rulesets[i].rules << "], bindings=[" << rulesets[i].bindings << "]");
    }
}

void PatternRulesetBuilder::appendFromLit(const Span& sp, EncodedLiteralSlice lit, const HIRTypeData* ty) {
    TRACE_FUNCTION_F("lit=" << lit << ", ty=" << ty << ",   m_field_path=[" << fieldPath << "]");

    switch ((*ty).tag()) {
        case HIRTypeData::TAG_Infer: {
            BUG(sp, "Ivar for in match type");
            break;
        }
        case HIRTypeData::TAG_Diverge: {
            // As above: nothing to read, so nothing to test.
            this->pushRule(PatternRule::make_Any({}));
            break;
        }
        case HIRTypeData::TAG_Primitive: {
            auto& e = (*ty).as_Primitive();
            switch (e) {
                case HIRCoreType::F16:
                    this->pushRule(PatternRule::make_Value(MIRConstant::make_Float({lit.readFloat(2), e})));
                    break;
                case HIRCoreType::F32:
                    this->pushRule(PatternRule::make_Value(MIRConstant::make_Float({lit.readFloat(4), e})));
                    break;
                case HIRCoreType::F64:
                    this->pushRule(PatternRule::make_Value(MIRConstant::make_Float({lit.readFloat(8), e})));
                    break;
                case HIRCoreType::F128:
                    this->pushRule(PatternRule::make_Value(MIRConstant::make_Float({lit.readFloat(16), e})));
                    break;

                case HIRCoreType::U8:
                    this->pushRule(PatternRule::make_Value(MIRConstant::make_Uint({lit.readUint(1), e})));
                    break;
                case HIRCoreType::U16:
                    this->pushRule(PatternRule::make_Value(MIRConstant::make_Uint({lit.readUint(2), e})));
                    break;
                case HIRCoreType::U32:
                    this->pushRule(PatternRule::make_Value(MIRConstant::make_Uint({lit.readUint(4), e})));
                    break;
                case HIRCoreType::U64:
                    this->pushRule(PatternRule::make_Value(MIRConstant::make_Uint({lit.readUint(8), e})));
                    break;
                case HIRCoreType::U128:
                    this->pushRule(PatternRule::make_Value(MIRConstant::make_Uint({lit.readUint(16), e})));
                    break;
                case HIRCoreType::Usize:
                    this->pushRule(PatternRule::make_Value(MIRConstant::make_Uint({lit.readUint(TargetGetPointerBits() / 8), e})));
                    break;

                case HIRCoreType::I8:
                    this->pushRule(PatternRule::make_Value(MIRConstant::make_Int({lit.readSint(1), e})));
                    break;
                case HIRCoreType::I16:
                    this->pushRule(PatternRule::make_Value(MIRConstant::make_Int({lit.readSint(2), e})));
                    break;
                case HIRCoreType::I32:
                    this->pushRule(PatternRule::make_Value(MIRConstant::make_Int({lit.readSint(4), e})));
                    break;
                case HIRCoreType::I64:
                    this->pushRule(PatternRule::make_Value(MIRConstant::make_Int({lit.readSint(8), e})));
                    break;
                case HIRCoreType::I128:
                    this->pushRule(PatternRule::make_Value(MIRConstant::make_Int({lit.readSint(16), e})));
                    break;
                case HIRCoreType::Isize:
                    this->pushRule(PatternRule::make_Value(MIRConstant::make_Int({lit.readSint(TargetGetPointerBits() / 8), e})));
                    break;

                case HIRCoreType::Bool:
                    this->pushRule(PatternRule::make_Bool(lit.readUint(1) != 0));
                    break;
                // Char is just another name for 'u32'... but with a restricted range
                case HIRCoreType::Char:
                    this->pushRule(PatternRule::make_Value(MIRConstant::make_Uint({lit.readUint(4), e})));
                    break;
                case HIRCoreType::Str:
                    BUG(sp, "Hit match over `str` - must be `&str`");
                    break;
            }
            break;
        }
        case HIRTypeData::TAG_Pattern: {
            auto& e = (*ty).as_Pattern();
            this->appendFromLit(sp, lit, e.inner);
            break;
        }
        case HIRTypeData::TAG_Tuple: {
            auto& e = (*ty).as_Tuple();
            auto* repr = TargetGetTypeRepr(sp, resolve, ty);
            ASSERT_BUG(sp, repr, "Matching with generic constant type not valid - " << ty);
            ASSERT_BUG(sp, e.size() == repr->fields.size(), "Matching tuple with mismatched literal size - " << e.size() << " != " << repr->fields.size());

            fieldPath.push_back(0);
            for (unsigned int i = 0; i < e.size(); i++) {
                this->appendFromLit(sp, lit.slice(repr->fields[i].offset), repr->fields[i].ty);
                fieldPath.back()++;
            }
            fieldPath.pop_back();
            break;
        }
        case HIRTypeData::TAG_Path: {
            auto& e = (*ty).as_Path();
            // This is either a struct destructure or an enum
            switch (e.binding.tag()) {
                case HIRTypePathBinding::TAG_Unbound: {
                    BUG(sp, "Encounterd unbound path - " << e.path);
                    break;
                }
                case HIRTypePathBinding::TAG_Opaque: {
                    TODO(sp, "Can an opaque path type be matched with a literal?");
                    this->pushRule(PatternRule::make_Any({}));
                    break;
                }
                case HIRTypePathBinding::TAG_Struct: {
                    auto* repr = TargetGetTypeRepr(sp, resolve, ty);
                    ASSERT_BUG(sp, repr, "Matching with generic constant type not valid - " << ty);

                    fieldPath.push_back(0);
                    for (size_t i = 0; i < repr->fields.size(); i++) {
                        this->appendFromLit(sp, lit.slice(repr->fields[i].offset), repr->fields[i].ty);
                        fieldPath.back()++;
                    }
                    fieldPath.pop_back();
                    break;
                }
                case HIRTypePathBinding::TAG_ExternType: {
                    TODO(sp, "Match extern type");
                    break;
                }
                case HIRTypePathBinding::TAG_Union: {
                    TODO(sp, "Match union");
                    break;
                }
                case HIRTypePathBinding::TAG_Enum: {
                    auto* enmRepr = TargetGetTypeRepr(sp, resolve, ty);
                    ASSERT_BUG(sp, enmRepr, "Matching with generic constant type not valid - " << ty);

                    // TODO: Share code with `MIR_Cleanup_LiteralToRValue`
                    auto varInfo = enmRepr->getEnumVariant(sp, resolve, lit);
                    unsigned varIdx = varInfo.first;
                    bool subHasTag = varInfo.second;

                    PatternRulesetBuilder subBuilder{this->resolve};
                    if (enmRepr->fields.size() > 1 || enmRepr->variants.is_None()) {
                        subBuilder.fieldPath = fieldPath;
                        subBuilder.fieldPath.push_back(varIdx);

                        // If the tag is in the sub-type, then ignore.
                        const auto& varTy = enmRepr->fields[varIdx].ty;
                        auto varLit = lit.slice(enmRepr->fields[varIdx].offset);
                        // NOTE: The tag is only present if it's an auto-generated struct (i.e. not `()`)
                        if (subHasTag && varTy != resolve.hirCrate().types.unit()) {
                            // This inner type should be a struct
                            DEBUG("Enum variant type w/ tag field: " << varTy);
                            auto* innerRepr = TargetGetTypeRepr(sp, resolve, varTy);
                            assert(innerRepr->variants.is_None());
                            assert(innerRepr->fields.size() > 0);
                            subBuilder.fieldPath.push_back(0);
                            for (size_t i = 0; i < innerRepr->fields.size() - 1; i++) {
                                subBuilder.appendFromLit(sp, varLit.slice(innerRepr->fields[i].offset), innerRepr->fields[i].ty);
                                subBuilder.fieldPath.back()++;
                            }
                            subBuilder.fieldPath.pop_back();
                        } else {
                            subBuilder.appendFromLit(sp, varLit, varTy);
                        }
                    }

                    ASSERT_BUG(sp, subBuilder.rulesets.size() == 1, "Multiple rulesets generated from a literal");
                    this->pushRule(PatternRule::make_Variant({varIdx, mv$(subBuilder.rulesets[0].rules)}));
                    break;
                }
            }
            break;
        }
        case HIRTypeData::TAG_Generic: {
            // Generics don't destructure, so the only valid pattern is `_`
            TODO(sp, "Match generic with literal?");
            this->pushRule(PatternRule::make_Any({}));
            break;
        }
        case HIRTypeData::TAG_TraitObject: {
            TODO(sp, "Match trait object with literal?");
            break;
        }
        case HIRTypeData::TAG_ErasedType: {
            TODO(sp, "Match erased type with literal?");
            break;
        }
        case HIRTypeData::TAG_Array: {
            auto& e = (*ty).as_Array();
            size_t size = 0;
            ASSERT_BUG(sp, TargetGetSizeOf(sp, resolve, e.inner, size), "Matching with generic constant type not valid - " << ty);

            fieldPath.push_back(0);
            size_t ofs = 0;
            for (unsigned int i = 0; i < e.size.as_Known(); i++) {
                this->appendFromLit(sp, lit.slice(ofs, size), e.inner);
                ofs += size;
                fieldPath.back()++;
            }
            fieldPath.pop_back();
            break;
        }
        case HIRTypeData::TAG_Slice: {
            TODO(sp, "Match literal Slice");
            break;
        }
        case HIRTypeData::TAG_Borrow: {
            auto& e = (*ty).as_Borrow();
            fieldPath.push_back(FIELD_DEREF);
            const auto ptrSize = TargetGetPointerBits() / 8;
            auto ptr = lit.readUint(ptrSize).truncateU64();
            auto valueSize = u64 { 0 };
            auto sliceLen = u64 { 0 };
            const HIRTypeData* sliceInner = nullptr;

            if (e.inner == HIRCoreType::Str) {
                sliceLen = lit.slice(ptrSize, ptrSize).readUint(ptrSize).truncateU64();
                valueSize = sliceLen;
            } else if (const auto* slice = e.inner->opt_Slice()) {
                sliceInner = slice->inner;
                sliceLen = lit.slice(ptrSize, ptrSize).readUint(ptrSize).truncateU64();
                size_t elementSize = 0;
                ASSERT_BUG(sp, TargetGetSizeOf(sp, resolve, sliceInner, elementSize), "Matching a slice with generic element size - " << e.inner);
                ASSERT_BUG(sp, elementSize == 0 || sliceLen <= UINT64_MAX / elementSize, "Slice pattern size overflow");
                valueSize = sliceLen * elementSize;
            } else {
                size_t sizedValueSize = 0;
                ASSERT_BUG(sp, TargetGetSizeOf(sp, resolve, e.inner, sizedValueSize), "Matching a reference to an unsized literal - " << e.inner);
                valueSize = sizedValueSize;
            }

            EncodedLiteral inlineValue;
            const EncodedLiteral* value = nullptr;
            auto* relocation = lit.getReloc();
            auto valueOffset = u64 { 0 };
            if (valueSize == 0 && !relocation) {
                // A reference to a zero-sized value carries no allocation to
                // decode. Its address is deliberately irrelevant to a pattern.
                value = &inlineValue;
            } else {
                ASSERT_BUG(sp, relocation, "Reference pattern has no relocation - " << lit);
                ASSERT_BUG(sp, ptr >= EncodedLiteral::PTR_BASE, "Invalid encoded reference in pattern - " << lit);
                valueOffset = ptr - EncodedLiteral::PTR_BASE;

                if (relocation->p) {
                    MonomorphState valueParams(resolve.hirCrate().types);
                    auto resolved = resolve.getValue(sp, *relocation->p, valueParams);
                    ASSERT_BUG(sp, resolved.is_Static(), "Reference pattern points to non-static " << *relocation->p << " - " << resolved.tagStr());
                    const auto& s = *resolved.as_Static();
                    ASSERT_BUG(sp, s.valueGenerated, "Reference pattern points to unresolved static " << *relocation->p);
                    value = &s.valueRes;
                } else {
                    inlineValue.bytes.assign(relocation->bytes.begin(), relocation->bytes.end());
                    value = &inlineValue;
                }
            }

            ASSERT_BUG(sp, valueOffset <= value->bytes.size(), "Reference pattern offset is out of bounds");
            ASSERT_BUG(sp, valueSize <= value->bytes.size() - valueOffset, "Reference pattern value is out of bounds");
            auto valueLit = EncodedLiteralSlice(*value).slice(valueOffset, valueSize);

            if (e.inner == HIRCoreType::Str) {
                this->pushRule(PatternRule::make_Value(std::string(
                    value->bytes.begin() + valueOffset,
                    value->bytes.begin() + valueOffset + sliceLen
                )));
            } else if (sliceInner == HIRCoreType::U8) {
                this->pushRule(PatternRule::make_Value(std::vector<u8>(
                    value->bytes.begin() + valueOffset,
                    value->bytes.begin() + valueOffset + sliceLen
                )));
            } else if (sliceInner) {
                ASSERT_BUG(sp, sliceLen < FIELD_INDEX_MAX, "Too many elements in constant slice pattern");
                size_t elementSize = 0;
                ASSERT_BUG(sp, TargetGetSizeOf(sp, resolve, sliceInner, elementSize), "Matching a slice with generic element size - " << e.inner);

                PatternRulesetBuilder subBuilder{resolve};
                subBuilder.fieldPath = fieldPath;
                subBuilder.fieldPath.push_back(0);
                for (unsigned int i = 0; i < sliceLen; i++) {
                    subBuilder.appendFromLit(sp, valueLit.slice(i * elementSize, elementSize), sliceInner);
                    subBuilder.fieldPath.back()++;
                }
                ASSERT_BUG(sp, subBuilder.rulesets.size() == 1, "Multiple rulesets generated from a slice literal");
                this->pushRule(PatternRule::make_Slice({static_cast<unsigned int>(sliceLen), mv$(subBuilder.rulesets[0].rules)}));
            } else {
                this->appendFromLit(sp, valueLit, e.inner);
            }
            fieldPath.pop_back();
            break;
        }
        case HIRTypeData::TAG_Pointer: {
            // Need to be able to tell downstream to cast to integer before comparison?
            this->pushRule(PatternRule::make_Value(MIRConstant::make_Uint({lit.readUint(TargetGetPointerBits() / 8), HIRCoreType::Usize})));
            //TODO(sp, "Match literal with pointer? " << lit);
            break;
        }
        case HIRTypeData::TAG_NamedFunction: {
            ERROR(sp, E0000, "Attempting to match over a functon pointer");
            break;
        }
        case HIRTypeData::TAG_Function: {
            ERROR(sp, E0000, "Attempting to match over a functon pointer");
            break;
        }
        case HIRTypeData::TAG_NodeType: {
            ERROR(sp, E0000, "Attempting to match over a magic type");
            break;
        }
    }
}

namespace {
    /// The bytes of a constant named in a pattern.  The binding HIR conversion
    /// recorded is the trait's *declaration* when the constant is an associated
    /// one -- which impl provides the value is only settled once the pattern's
    /// type is -- so the impl is looked up here, and the value evaluated if it
    /// has not been already.
    static const EncodedLiteral* patternConstantLiteral(const Span& sp, const StaticTraitResolve& resolve, const HIRPattern::Value& val) {
        const auto* pve = val.opt_Named();
        if (!pve || !pve->binding) {
            return nullptr;
        }
        const HIRConstant* binding = pve->binding;
        MonomorphState valueMs(resolve.hirCrate().types);
        const HIRGenericParams* implDef = nullptr;
        auto value = resolve.getValue(sp, pve->path, valueMs, false, &implDef);
        if (const auto* constant = value.opt_Constant()) {
            binding = *constant;
        }
        if (binding->valueState == HIRConstant::ValueState::Unknown
            || (binding->valueState == HIRConstant::ValueState::Generic && !binding->monomorphCache.count(pve->path))) {
            ConvertHIRConstantEvaluateConstant(resolve, implDef, pve->path, const_cast<HIRConstant&>(*binding));
        }
        if (binding->valueState == HIRConstant::ValueState::Known) {
            return &binding->valueRes;
        }
        const auto cached = binding->monomorphCache.find(pve->path);
        return cached != binding->monomorphCache.end() ? &cached->second : nullptr;
    }
}

void PatternRulesetBuilder::appendFrom(const Span& sp, const HIRPattern& pat, const HIRTypeData* topTy) {
    static HIRPattern emptyPattern;
    TRACE_FUNCTION_F("pat=" << pat << ", ty=" << topTy << ",   m_field_path=[" << fieldPath << "]");

    HIRTypeRef revealedTopTy = topTy;
    resolve.revealOpaqueTypes(sp, revealedTopTy);
    topTy = revealedTopTy;

    // Nothing inhabits `!`, so there is no value for a rule to read. The arm
    // still has to keep its shape, so this stands in as a rule that tests
    // nothing.
    if (topTy->is_Diverge()) {
        this->pushRule(PatternRule::make_Any({}));
        return;
    }

    const bool trackWildcard = pat.data.is_Any();
    WildcardType wildcardType{topTy, wildcardTypes};
    if (trackWildcard) {
        for (auto* active = wildcardTypes; active; active = active->parent) {
            if (active->type == topTy) {
                this->pushRule(PatternRule::make_Any({}));
                return;
            }
        }
        wildcardTypes = &wildcardType;
    }
    STD_DEFER {
        if (trackWildcard) {
            wildcardTypes = wildcardType.parent;
        }
    };

    struct H {
        static U128 getPatternValueInt(const Span& sp, const StaticTraitResolve& resolve, const HIRPattern& pat, const HIRPattern::Value& val) {
            switch (val.tag()) {
                case HIRPattern::Value::TAG_Integer: {
                    auto& e = val.as_Integer();
                    return e.value;
                }
                case HIRPattern::Value::TAG_Named: {
                    const auto* lit = patternConstantLiteral(sp, resolve, val);
                    ASSERT_BUG(sp, lit, "Match with an unresolved constant in " << pat);
                    return EncodedLiteralSlice(*lit).readUint();
                }
                default: {
                    BUG(sp, "Invalid Value type in " << pat);
                    break;
                }
            }
            throw "";
        }

        static S128 getPatternValueSigned(const Span& sp, const StaticTraitResolve& resolve, const HIRPattern& pat, const HIRPattern::Value& val) {
            switch (val.tag()) {
                case HIRPattern::Value::TAG_Integer: {
                    auto& e = val.as_Integer();
                    return S128(e.value);
                }
                case HIRPattern::Value::TAG_Named: {
                    const auto* lit = patternConstantLiteral(sp, resolve, val);
                    ASSERT_BUG(sp, lit, "Match with an unresolved constant in " << pat);
                    return EncodedLiteralSlice(*lit).readSint();
                }
                default: {
                    BUG(sp, "Invalid signed Value type in " << pat);
                    break;
                }
            }
            throw "";
        }

        static FloatValue getPatternValueFloat(const Span& sp, const StaticTraitResolve& resolve, const HIRPattern& pat, const HIRPattern::Value& val) {
            switch (val.tag()) {
                case HIRPattern::Value::TAG_Float: {
                    auto& e = val.as_Float();
                    return e.value;
                }
                case HIRPattern::Value::TAG_Named: {
                    const auto* lit = patternConstantLiteral(sp, resolve, val);
                    ASSERT_BUG(sp, lit, "Match with an unresolved constant in " << pat);
                    return EncodedLiteralSlice(*lit).readFloat();
                }
                default: {
                    BUG(sp, "Invalid Value type in " << pat);
                    break;
                }
            }
            throw "";
        }

        static MIRConstant getPatternValue(const Span& sp, const StaticTraitResolve& resolve, const HIRPattern& pat, const HIRPattern::Value& val, const HIRCoreType& e) {
            switch (e) {
                case HIRCoreType::F16:
                case HIRCoreType::F32:
                case HIRCoreType::F64:
                case HIRCoreType::F128:
                    // Yes, this is valid.
                    return MIRConstant::make_Float({H::getPatternValueFloat(sp, resolve, pat, val), e});
                case HIRCoreType::U8:
                case HIRCoreType::U16:
                case HIRCoreType::U32:
                case HIRCoreType::U64:
                case HIRCoreType::U128:
                case HIRCoreType::Usize:
                    return MIRConstant::make_Uint({H::getPatternValueInt(sp, resolve, pat, val), e});
                case HIRCoreType::I8:
                case HIRCoreType::I16:
                case HIRCoreType::I32:
                case HIRCoreType::I64:
                case HIRCoreType::I128:
                case HIRCoreType::Isize:
                    return MIRConstant::make_Int({H::getPatternValueSigned(sp, resolve, pat, val), e});
                case HIRCoreType::Bool:
                    BUG(sp, "Can't range match on Bool");
                    break;
                case HIRCoreType::Char:
                    // Char is just another name for 'u32'... but with a restricted range
                    return MIRConstant::make_Uint({H::getPatternValueInt(sp, resolve, pat, val), e});
                case HIRCoreType::Str:
                    BUG(sp, "Hit match over `str` - must be `&str`");
                    break;
            }
            throw "";
        }

        static MIRConstant getPatternValueMin(const Span& sp, const HIRPattern& pat, const HIRCoreType& e) {
            switch (e) {
                case HIRCoreType::F16:
                case HIRCoreType::F32:
                case HIRCoreType::F64:
                case HIRCoreType::F128:
                    // Yes, this is valid.
                    return MIRConstant::make_Float({-std::numeric_limits<double>::infinity(), e});
                case HIRCoreType::U8:
                case HIRCoreType::U16:
                case HIRCoreType::U32:
                case HIRCoreType::U64:
                case HIRCoreType::U128:
                case HIRCoreType::Usize:
                    return MIRConstant::make_Uint({U128(0), e});
                case HIRCoreType::I8:
                case HIRCoreType::I16:
                case HIRCoreType::I32:
                case HIRCoreType::I64:
                case HIRCoreType::I128:
                case HIRCoreType::Isize:
                    return MIRConstant::make_Int({S128::min(), e});
                case HIRCoreType::Bool:
                    BUG(sp, "Can't range match on Bool");
                    break;
                case HIRCoreType::Char:
                    // Char is just another name for 'u32'... but with a restricted range
                    return MIRConstant::make_Uint({U128(0), e});
                case HIRCoreType::Str:
                    BUG(sp, "Hit match over `str` - must be `&str`");
                    break;
            }
            throw "";
        }

        static MIRConstant getPatternValueMax(const Span& sp, const HIRPattern& pat, const HIRCoreType& e) {
            switch (e) {
                case HIRCoreType::F16:
                case HIRCoreType::F32:
                case HIRCoreType::F64:
                case HIRCoreType::F128:
                    // Yes, this is valid.
                    return MIRConstant::make_Float({std::numeric_limits<double>::infinity(), e});
                case HIRCoreType::U8:
                case HIRCoreType::U16:
                case HIRCoreType::U32:
                case HIRCoreType::U64:
                case HIRCoreType::U128:
                case HIRCoreType::Usize:
                    return MIRConstant::make_Uint({U128::max(), e});
                case HIRCoreType::I8:
                case HIRCoreType::I16:
                case HIRCoreType::I32:
                case HIRCoreType::I64:
                case HIRCoreType::I128:
                case HIRCoreType::Isize:
                    return MIRConstant::make_Int({S128::max(), e});
                case HIRCoreType::Bool:
                    BUG(sp, "Can't range match on Bool");
                    break;
                case HIRCoreType::Char:
                    // Char is just another name for 'u32'... but with a restricted range
                    return MIRConstant::make_Uint({U128::max(), e});
                case HIRCoreType::Str:
                    BUG(sp, "Hit match over `str` - must be `&str`");
                    break;
            }
            throw "";
        }
    };

    for (const auto& pb : pat.bindings) {
        auto path = fieldPath;
        for (size_t i = 0; i < pb.implicitDerefCount; i++) {
            path.push_back(FIELD_DEREF);
        }

        this->pushBinding(PatternBinding(std::move(path), pb));
    }

    const auto* tyP = &topTy;
    for (size_t i = 0; i < pat.implicitDerefCount; i++) {
        if (!(*tyP)->is_Borrow()) {
            BUG(sp, "Deref step " << i << "/" << pat.implicitDerefCount << " hit a non-borrow " << *tyP << " from " << topTy);
        }
        tyP = &(*tyP)->as_Borrow().inner;
        fieldPath.push_back(FIELD_DEREF);
    }
    const HIRTypeData* ty = *tyP;
    if (const auto* pattern = ty->opt_Pattern()) ty = pattern->inner;

    // TODO: Outer handling for Value::Named patterns
    // - Convert them into either a pattern, or just a variant of this function that operates on ::HIR::Literal
    //  > It does need a way of handling unknown-value constants (e.g. <GenericT as Foo>::CONST)
    //  > Those should lead to a simple match? Or just a custom rule type that indicates that they're checked early
    if (const auto* pe = pat.data.opt_Value()) {
        if (const auto* pve = pe->val.opt_Named()) {
            if (pve->binding) {
                const EncodedLiteral* literal = patternConstantLiteral(sp, resolve, pe->val);
                ASSERT_BUG(sp, literal, "Match with an unresolved constant - " << pve->path);
                this->appendFromLit(sp, *literal, ty);
                for (size_t i = 0; i < pat.implicitDerefCount; i++) {
                    fieldPath.pop_back();
                }
                return;
            } else {
                TODO(sp, "Match with an unbound constant - " << pve->path);
            }
        }
    }

    if (const auto* pe = pat.data.opt_Deref()) {
        ASSERT_BUG(sp, pe->kind != HIRPattern::DerefKind::Unknown && pe->targetType, "Untyped deref pattern " << pat);
        if (pe->kind == HIRPattern::DerefKind::Box) {
            fieldPath.push_back(FIELD_DEREF);
            this->appendFrom(sp, *pe->sub, pe->targetType);
            fieldPath.pop_back();
        } else {
            const auto parentRoot = rootIndex;
            const auto inputField = fieldPath;
            const auto newRoot = (*nextRootIndex)++;
            for (size_t i = subsetStart; i < subsetEnd; i++) {
                rulesets[i].derefs.push_back(PatternRuleset::Deref{newRoot, parentRoot, inputField, ty, pe->targetType, pe->kind});
            }
            const auto savedField = fieldPath;
            const auto savedRoot = rootIndex;
            fieldPath = {};
            rootIndex = newRoot;
            this->appendFrom(sp, *pe->sub, pe->targetType);
            rootIndex = savedRoot;
            fieldPath = savedField;
        }
        for (size_t i = 0; i < pat.implicitDerefCount; i++) fieldPath.pop_back();
        return;
    }

    if (const auto* pe = pat.data.opt_Ref(); pe && pe->isSkipped) {
        this->appendFrom(sp, *pe->sub, ty);
        for (size_t i = 0; i < pat.implicitDerefCount; i++) fieldPath.pop_back();
        return;
    }

    if (pat.data.is_Or()) {
        // Multiply the current pattern (sub)set out, visit with sub-sets
        const auto& e = pat.data.as_Or();
        assert(pat.implicitDerefCount == 0); // Shouldn't have any, so this code doesn't need to pop them.
        assert(e.size() > 0);
        this->multiplyRulesets(e.size(), [&](size_t i) {
            this->appendFrom(sp, e[i], topTy);
        });
        return;
    }

    switch ((*ty).tag()) {
        case HIRTypeData::TAG_Infer: {
            BUG(sp, "Ivar for in match type");
            break;
        }
        case HIRTypeData::TAG_Diverge: {
            // Since ! can never exist, mark this arm as impossible.
            // TODO: Marking as impossible (and not emitting) leads to exhuaustiveness failure.
            break;
        }
        case HIRTypeData::TAG_Primitive: {
            auto& e = (*ty).as_Primitive();
            switch (pat.data.tag()) {
default:
                BUG(sp, "Matching primitive with invalid pattern - " << pat);
                case HIRPatternData::TAG_Any: {
                    this->pushRule(PatternRule::make_Any({}));
                    break;
                }
                case HIRPatternData::TAG_Range: {
                    auto& pe = pat.data.as_Range();
                    if (!pe.start || !pe.end) {
                        assert(pe.start || pe.end);
                        if (pe.start) {
                            this->pushRule(
                                PatternRule::make_ValueRange({
                                    H::getPatternValue(sp, resolve, pat, *pe.start, e),
                                    H::getPatternValueMax(sp, pat, e),
                                    true // Inclusive always
                                })
                            );
                        } else {
                            this->pushRule(PatternRule::make_ValueRange({H::getPatternValueMin(sp, pat, e), H::getPatternValue(sp, resolve, pat, *pe.end, e), pe.isInclusive}));
                        }
                    } else {
                        this->pushRule(PatternRule::make_ValueRange({H::getPatternValue(sp, resolve, pat, *pe.start, e), H::getPatternValue(sp, resolve, pat, *pe.end, e), pe.isInclusive}));
                    }
                    break;
                }
                case HIRPatternData::TAG_Value: {
                    auto& pe = pat.data.as_Value();
                    switch (e) {
                        case HIRCoreType::Bool:
                            // TODO: Support values from `const` too
                            this->pushRule(PatternRule::make_Bool(pe.val.as_Integer().value != 0));
                            break;
                        case HIRCoreType::Str:
                            ASSERT_BUG(sp, pe.val.is_String(), "Matching `str` with non-string value pattern - " << pat);
                            this->pushRule(PatternRule::make_Value(pe.val.as_String()));
                            break;
                        default:
                            this->pushRule(H::getPatternValue(sp, resolve, pat, pe.val, e));
                            break;
                    }
                    break;
                }
            }
            break;
        }
        case HIRTypeData::TAG_Pattern: {
            BUG(sp, "Pattern type was not reduced to its base type");
            break;
        }
        case HIRTypeData::TAG_Tuple: {
            auto& e = (*ty).as_Tuple();
            fieldPath.push_back(0);
            switch (pat.data.tag()) {
                case HIRPattern::Data::TAG_Any: {
                    // TODO: Avoid storing the empty patterns, to save on space/cost
                    for (const auto& sty : e) {
                        this->appendFrom(sp, emptyPattern, sty);
                        fieldPath.back()++;
                    }
                    break;
                }
                case HIRPattern::Data::TAG_Tuple: {
                    auto& pe = pat.data.as_Tuple();
                    assert(e.size() == pe.subPatterns.size()); for (unsigned int i = 0; i < e.size(); i++) {
                        this->appendFrom(sp, pe.subPatterns[i], e[i]);
                        fieldPath.back()++;
                    }
                    break;
                }
                case HIRPattern::Data::TAG_SplitTuple: {
                    auto& pe = pat.data.as_SplitTuple();
                    assert(e.size() >= pe.leading.size() + pe.trailing.size()); unsigned trailingStart = e.size() - pe.trailing.size(); for (unsigned int i = 0; i < e.size(); i++) {
                        if (i < pe.leading.size()) {
                            this->appendFrom(sp, pe.leading[i], e[i]);
                        } else if (i < trailingStart) {
                            this->appendFrom(sp, emptyPattern, e[i]);
                        } else {
                            this->appendFrom(sp, pe.trailing[i - trailingStart], e[i]);
                        }
                        fieldPath.back()++;
                    }
                    break;
                }
                default: {
                    BUG(sp, "Matching tuple with invalid pattern - " << pat);
                    break;
                }
            }
            fieldPath.pop_back();
            break;
        }
        case HIRTypeData::TAG_Path: {
            auto& e = (*ty).as_Path();
            struct PH {
                    static void pushPatternTuple(PatternRulesetBuilder& builder, const Span& sp, const HIRPattern::Data::Data_PathTuple& pe, PatternTypeCallback& maybeMonomorph) {
                        const auto& sd = patternGetTuple(sp, pe.path, pe.binding);
                        assert(sd.size() >= pe.leading.size() + pe.trailing.size());
                        size_t trailingStart = sd.size() - pe.trailing.size();
                        for (unsigned int i = 0; i < sd.size(); i++) {
                            const auto& fld = sd[i];

                            if (i < pe.leading.size()) {
                                builder.appendFrom(sp, pe.leading[i], maybeMonomorph.map(fld.ent));
                            } else if (i < trailingStart) {
                                builder.appendFrom(sp, emptyPattern, maybeMonomorph.map(fld.ent));
                            } else {
                                builder.appendFrom(sp, pe.trailing[i - trailingStart], maybeMonomorph.map(fld.ent));
                            }
                            builder.fieldPath.back()++;
                        }
                    }
                    static void pushPatternStruct(PatternRulesetBuilder& builder, const Span& sp, const HIRPattern::Data::Data_PathNamed& pe, PatternTypeCallback& maybeMonomorph) {
                        const auto& sd = patternGetNamed(sp, pe.path, pe.binding);
                        // NOTE: Iterates in field order (not pattern order) to ensure that patterns are in order between arms
                        for (const auto& fld : sd) {
                            const auto& styMono = maybeMonomorph.map(fld.ty);

                            auto it = ::std::find_if(pe.subPatterns.begin(), pe.subPatterns.end(), [&](const auto& x) {
                                return x.first == fld.name;
                            });
                            if (it == pe.subPatterns.end()) {
                                builder.appendFrom(sp, emptyPattern, styMono);
                            } else {
                                builder.appendFrom(sp, it->second, styMono);
                            }
                            builder.fieldPath.back()++;
                        }
                    }
                };
                HIRTypeRef tmp;
                auto maybeMonomorph = [&](const HIRTypeData* ty) -> const HIRTypeData* {
                    if (monomorphiseTypeNeeded(ty)) {
                        tmp = MonomorphStatePtr(resolve.hirCrate().types, nullptr, &e.path.data.as_Generic().params, nullptr).monomorphType(sp, ty);
                        this->resolve.expandAssociatedTypes(sp, tmp);
                        return tmp;
                    } else {
                        return ty;
                    }
                };
                PatternTypeCb<decltype(maybeMonomorph)> patternType(maybeMonomorph);
                // This is either a struct destructure or an enum
            switch (e.binding.tag()) {
                case HIRTypePathBinding::TAG_Unbound: {
                    BUG(sp, "Encounterd unbound path - " << e.path);
                    break;
                }
                case HIRTypePathBinding::TAG_Opaque: {
                    switch (pat.data.tag()) {
                        case HIRPattern::Data::TAG_Any: {
                            this->pushRule(PatternRule::make_Any({}));
                            break;
                        }
                        default: {
                            BUG(sp, "Matching opaque type with invalid pattern - " << pat);
                            break;
                        }
                    }
                    break;
                }
                case HIRTypePathBinding::TAG_Struct: {
                    auto& pbe = e.binding.as_Struct();
                    const auto& strData = pbe->data;

                            if (langBox && e.path.data.as_Generic().path == *langBox) {
                                const auto& innerTy = e.path.data.as_Generic().params.types.at(0);
                                switch (pat.data.tag()) {
                                    case HIRPattern::Data::TAG_Any: {
                                        // _ on a box, recurse into the box type.
                                        fieldPath.push_back(FIELD_DEREF);
                                        this->appendFrom(sp, emptyPattern, innerTy);
                                        fieldPath.pop_back();
                                        break;
                                    }
                                    case HIRPattern::Data::TAG_Box: {
                                        auto& pe = pat.data.as_Box();
                                        fieldPath.push_back(FIELD_DEREF); this->appendFrom(sp, *pe.sub, innerTy); fieldPath.pop_back();
                                        break;
                                    }
                                    default: {
                                        BUG(sp, "Match not allowed, " << ty << " with " << pat);
                                        break;
                                    }
                                }
                                break;
                            }
                    switch (strData.tag()) {
                        case HIRStructData::TAG_Unit: {
                            switch (pat.data.tag()) {
default:
                                BUG(sp, "Match not allowed, " << ty <<  " with " << pat);
                                case HIRPatternData::TAG_Any: {
                                    // _ on a unit-like type, unconditional
                                    break;
                                }
                                case HIRPatternData::TAG_PathValue: {
                                    // Unit-like struct value, nothing to match (it's unconditional)
                                    break;
                                }
                                case HIRPatternData::TAG_Value: {
                                    // Unit-like struct value, nothing to match (it's unconditional)
                                    break;
                                }
                                case HIRPatternData::TAG_PathNamed: {
                                    auto& pe = pat.data.as_PathNamed();
                                    ASSERT_BUG(sp, pe.subPatterns.size() == 0, "Matching unit-like struct with sub-patterns - " << pat);
                                    break;
                                }
                            }
                            break;
                        }
                        case HIRStructData::TAG_Tuple: {
                            auto& sd = strData.as_Tuple();
                            fieldPath.push_back(0);
                            switch (pat.data.tag()) {
default:
                                BUG(sp, "Match not allowed, " << ty <<  " with " << pat);
                                case HIRPatternData::TAG_Any: {
                                    // - Recurse into type using an empty pattern
                                    for (const auto& fld : sd) {
                                        ASSERT_BUG(sp, fieldPath.back() < FIELD_INDEX_MAX, "Too-large struct field index");
                                        this->appendFrom(sp, emptyPattern, maybeMonomorph(fld.ent));
                                        fieldPath.back()++;
                                    }
                                    break;
                                }
                                case HIRPatternData::TAG_PathNamed: {
                                    auto& pe = pat.data.as_PathNamed();
                                    // Only allow with an empty tuple (assuming that the pattern is also empty)... or if the pattern is a wildcard
                                    if (sd.size() != 0 && !pe.isWildcard()) {
                                        BUG(sp, "Match not allowed, " << ty << " with " << pat);
                                    }
                                    for (const auto& fld : sd) {
                                        ASSERT_BUG(sp, fieldPath.back() < FIELD_INDEX_MAX, "Too-large struct field index");
                                        this->appendFrom(sp, emptyPattern, maybeMonomorph(fld.ent));
                                        fieldPath.back()++;
                                    }
                                    break;
                                }
                                case HIRPatternData::TAG_PathTuple: {
                                    auto& pe = pat.data.as_PathTuple();
                                    assert(pe.binding.is_Struct());
                                    PH::pushPatternTuple(*this, sp, pe, patternType);
                                    break;
                                }
                            }
                            fieldPath.pop_back();
                            break;
                        }
                        case HIRStructData::TAG_Named: {
                            auto& sd = strData.as_Named();
                            switch (pat.data.tag()) {
default:
                                BUG(sp, "Match not allowed, " << ty <<  " with " << pat);
                                case HIRPatternData::TAG_Any: {
                                    fieldPath.push_back(0);
                                    for (const auto& fld : sd) {
                                        ASSERT_BUG(sp, fieldPath.back() < FIELD_INDEX_MAX, "Too-large struct field index");
                                        this->appendFrom(sp, emptyPattern, maybeMonomorph(fld.ty));
                                        fieldPath.back()++;
                                    }
                                    fieldPath.pop_back();
                                    break;
                                }
                                case HIRPatternData::TAG_PathNamed: {
                                    auto& pe = pat.data.as_PathNamed();
                                    assert(pe.binding.is_Struct());
                                    fieldPath.push_back(0);
                                    PH::pushPatternStruct(*this, sp, pe, patternType);
                                    fieldPath.pop_back();
                                    break;
                                }
                            }
                            break;
                        }
                    }
                    break;
                }
                case HIRTypePathBinding::TAG_Union: {
                    auto& pbe = e.binding.as_Union();
                    switch (pat.data.tag()) {
default:
                        TODO(sp, "Match over union - " << ty << " with " << pat);
                        case HIRPatternData::TAG_Any: {
                            this->pushRule(PatternRule::make_Any({}));
                            break;
                        }
                        case HIRPatternData::TAG_PathNamed: {
                            auto& pe = pat.data.as_PathNamed();
                            ASSERT_BUG(sp, pe.binding.is_Union() && pe.binding.as_Union() == pbe, "Union pattern binding mismatch");
                            ASSERT_BUG(sp, pe.subPatterns.size() == 1, "Union pattern must select exactly one field");

                            const auto& fieldPattern = pe.subPatterns.front();
                            auto fieldIt = ::std::find_if(pbe->variants.begin(), pbe->variants.end(), [&](const auto& field) {
                                return field.name == fieldPattern.first;
                            });
                            ASSERT_BUG(sp, fieldIt != pbe->variants.end(), "Unable to find union field " << fieldPattern.first);

                            const auto fieldIndex = static_cast<unsigned>(fieldIt - pbe->variants.begin());
                            ASSERT_BUG(sp, fieldIndex < FIELD_INDEX_MAX, "Too-large union field index");
                            fieldPath.push_back(fieldIndex);
                            this->appendFrom(sp, fieldPattern.second, maybeMonomorph(fieldIt->ty));
                            fieldPath.pop_back();
                            break;
                        }
                    }
                    break;
                }
                case HIRTypePathBinding::TAG_ExternType: {
                    switch (pat.data.tag()) {
default:
                        BUG(sp, "Match not allowed, " << ty <<  " with " << pat);
                        case HIRPatternData::TAG_Any: {
                            this->pushRule(PatternRule::make_Any({}));
                            break;
                        }
                    }
                    break;
                }
                case HIRTypePathBinding::TAG_Enum: {
                    switch (pat.data.tag()) {
default:
                        BUG(sp, "Match not allowed, " << ty <<  " with " << pat);
                        case HIRPatternData::TAG_Any: {
                            this->pushRule(PatternRule::make_Any({}));
                            break;
                        }
                        case HIRPatternData::TAG_Value: {
                            auto& pe = pat.data.as_Value();
                            if (!pe.val.is_Named()) {
                                BUG(sp, "Match not allowed, " << ty << " with " << pat);
                            }
                            // TODO: If the value of this constant isn't known at this point (i.e. it won't be until monomorphisation)
                            //       emit a special type of rule.
                            TODO(sp, "Match enum with const - " << pat);
                            break;
                        }
                        case HIRPatternData::TAG_PathValue: {
                            auto& pe = pat.data.as_PathValue();
                            assert(pe.binding.is_Enum());
                            this->pushRule(PatternRule::make_Variant({pe.binding.as_Enum().varIdx, {}}));
                            break;
                        }
                        case HIRPatternData::TAG_PathTuple: {
                            auto& pe = pat.data.as_PathTuple();
                            assert(pe.binding.is_Enum());
                            const auto& be = pe.binding.as_Enum();

                            PatternRulesetBuilder subBuilder{this->resolve, this->nextRootIndex, wildcardTypes};
                            subBuilder.fieldPath = fieldPath;
                            subBuilder.rootIndex = rootIndex;
                            ASSERT_BUG(sp, be.varIdx < FIELD_INDEX_MAX, "Too-large variant index in " << ty);
                            subBuilder.fieldPath.push_back(be.varIdx);
                            subBuilder.fieldPath.push_back(0);

                            PH::pushPatternTuple(subBuilder, sp, pe, patternType);

                            this->multiplyRulesets(subBuilder.rulesets.size(), [&](size_t i) {
                                auto& sr = subBuilder.rulesets[i];
                                if (sr.isImpossible) {
                                    this->setImpossible();
                                }
                                this->pushRule(PatternRule::make_Variant({be.varIdx, mv$(sr.rules)}));
                                this->pushBindings(mv$(sr.bindings));
                                this->pushDerefs(mv$(sr.derefs));
                            });
                            break;
                        }
                        case HIRPatternData::TAG_PathNamed: {
                            auto& pe = pat.data.as_PathNamed();
                            assert(pe.binding.is_Enum());
                            const auto& be = pe.binding.as_Enum();

                            PatternRulesetBuilder subBuilder{this->resolve, this->nextRootIndex, wildcardTypes};
                            subBuilder.fieldPath = fieldPath;
                            subBuilder.rootIndex = rootIndex;
                            ASSERT_BUG(sp, be.varIdx < FIELD_INDEX_MAX, "Too-large variant index");
                            subBuilder.fieldPath.push_back(be.varIdx);
                            subBuilder.fieldPath.push_back(0);

                            // Empty variants can be matched with `Var { [..] }` even if they're not struct-like
                            if (be.ptr->isValue()) {
                                assert(pe.subPatterns.empty());
                            } else if (be.ptr->data.as_Data().at(be.varIdx).type == resolve.hirCrate().types.unit()) {
                                assert(pe.subPatterns.empty());
                            } else if (!be.ptr->data.as_Data().at(be.varIdx).isStruct) {
                                assert(pe.subPatterns.empty());
                                const auto& sd = patternGetTuple(sp, pe.path, pe.binding);
                                for (unsigned int i = 0; i < sd.size(); i++) {
                                    const auto& fld = sd[i];
                                    subBuilder.appendFrom(sp, emptyPattern, maybeMonomorph(fld.ent));
                                    subBuilder.fieldPath.back()++;
                                }
                            } else {
                                PH::pushPatternStruct(subBuilder, sp, pe, patternType);
                            }

                            this->multiplyRulesets(subBuilder.rulesets.size(), [&](size_t i) {
                                auto& sr = subBuilder.rulesets[i];
                                if (sr.isImpossible) {
                                    this->setImpossible();
                                }
                                this->pushRule(PatternRule::make_Variant({be.varIdx, mv$(sr.rules)}));
                                this->pushBindings(mv$(sr.bindings));
                                this->pushDerefs(mv$(sr.derefs));
                            });
                            break;
                        }
                    }
                    break;
                }
            }
            break;
        }
        case HIRTypeData::TAG_Generic: {
            // Generics don't destructure, so the only valid pattern is `_`
            switch (pat.data.tag()) {
                case HIRPattern::Data::TAG_Any: {
                    this->pushRule(PatternRule::make_Any({}));
                    break;
                }
                default: {
                    BUG(sp, "Match not allowed, " << ty << " with " << pat);
                    break;
                }
            }
            break;
        }
        case HIRTypeData::TAG_TraitObject: {
            if (pat.data.is_Any()) {
            } else {
                ERROR(sp, E0000, "Attempting to match over a trait object");
            }
            break;
        }
        case HIRTypeData::TAG_ErasedType: {
            if (pat.data.is_Any()) {
            } else {
                ERROR(sp, E0000, "Attempting to match over an erased type");
            }
            break;
        }
        case HIRTypeData::TAG_Array: {
            auto& e = (*ty).as_Array();
            // If the size is unknown, just push a `_` pattern.
                // OR: don't push anything?
                if (!e.size.is_Known()) {
                    DEBUG("Matching over unknown-sized array - " << e.size);
                    ASSERT_BUG(sp, pat.data.is_Any(), "Matching generic-sized array with non `_` pattern - " << pat);
                    this->pushRule(PatternRule::make_Any({}));
                    break;
                }
                // Sequential match just like tuples.
                fieldPath.push_back(0);
            switch (pat.data.tag()) {
default:
                BUG(sp, "Matching array with invalid pattern - " << pat);
                case HIRPatternData::TAG_Any: {
                    if (e.size.as_Known() < PARTIAL_ARRAY_MIN) {
                        for (u64 i = 0; i < e.size.as_Known(); i++) {
                            this->appendFrom(sp, emptyPattern, e.inner);
                            fieldPath.back()++;
                        }
                    } else {
                        // A wildcard neither reads nor binds any element. Keep
                        // one placeholder rule instead of expanding the whole
                        // array; its field path is irrelevant to codegen.
                        this->pushRule(PatternRule::make_Any({}));
                    }
                    break;
                }
                case HIRPatternData::TAG_Value: {
                    auto& pe = pat.data.as_Value();
                    ASSERT_BUG(sp, pe.val.is_ByteString(), "Matching array with non-byte-string value pattern - " << pat);
                    const auto& bytes = pe.val.as_ByteString().v;
                    ASSERT_BUG(sp, e.inner == HIRCoreType::U8 && e.size.as_Known() == bytes.size(), "Byte string pattern type mismatch - " << pat << " vs " << ty);
                    for (auto byte : bytes) {
                        this->pushRule(PatternRule::make_Value(MIRConstant::make_Uint({U128(static_cast<u8>(byte)), HIRCoreType::U8})));
                        fieldPath.back()++;
                    }
                    break;
                }
                case HIRPatternData::TAG_Slice: {
                    auto& pe = pat.data.as_Slice();
                    ASSERT_BUG(sp, e.size.as_Known() == pe.subPatterns.size(), "Pattern size mismatch");
                    for (const auto& v : pe.subPatterns) {
                        this->appendFrom(sp, v, e.inner);
                        fieldPath.back()++;
                    }
                    break;
                }
                case HIRPatternData::TAG_SplitSlice: {
                    auto& pe = pat.data.as_SplitSlice();
                    ASSERT_BUG(sp, pe.leading.size() < FIELD_INDEX_MAX, "Too many leading slice rules to fit encodng");
                    const auto arraySize = e.size.as_Known();
                    ASSERT_BUG(sp, pe.leading.size() <= arraySize, "Too many leading slice rules for array type");
                    ASSERT_BUG(sp, pe.trailing.size() <= arraySize - pe.leading.size(), "Too many slice rules for array type");
                    for (const auto& subpat : pe.leading) {
                        this->appendFrom(sp, subpat, e.inner);
                        fieldPath.back()++;
                    }
                    if (arraySize < PARTIAL_ARRAY_MIN) {
                        // Small arrays enumerate the `..` middle so every arm of a
                        // multi-arm match produces the same rule count (the column
                        // sorter and the grouped matcher rely on that).
                        while (fieldPath.back() < arraySize - pe.trailing.size()) {
                            this->appendFrom(sp, emptyPattern, e.inner);
                            fieldPath.back()++;
                        }
                        for (const auto& subpat : pe.trailing) {
                            this->appendFrom(sp, subpat, e.inner);
                            fieldPath.back()++;
                        }
                    } else {
                        // The `..` middle of a huge array binds nothing and tests
                        // nothing, so no rules are emitted for it (rules are
                        // self-addressed via their field path). Enumerating it would
                        // allocate per-element rules - fatal for [T; 64_000_000].
                        if (!pe.trailing.empty()) {
                            ASSERT_BUG(sp, arraySize - pe.trailing.size() < FIELD_INDEX_MAX, "Trailing slice rules after a too-large array gap");
                            fieldPath.back() = static_cast<u16>(arraySize - pe.trailing.size());
                            for (const auto& subpat : pe.trailing) {
                                this->appendFrom(sp, subpat, e.inner);
                                fieldPath.back()++;
                            }
                        }
                    }

                    if (pe.extraBind.isValid()) {
                        ASSERT_BUG(sp, pe.extraBind.implicitDerefCount == 0, "");
                        PatternBinding pb(fieldPath, pe.extraBind);
                        pb.field.pop_back();
                        pb.splitSlice = std::make_pair(pe.leading.size(), pe.trailing.size());
                        this->pushBinding(mv$(pb));
                    }
                    break;
                }
            }
            fieldPath.pop_back();
            break;
        }
        case HIRTypeData::TAG_Slice: {
            auto& e = (*ty).as_Slice();
            switch (pat.data.tag()) {
default:
                BUG(sp, "Matching over [T] with invalid pattern - " << pat);
                case HIRPatternData::TAG_Any: {
                    this->pushRule(PatternRule::make_Any({}));
                    break;
                }
                case HIRPatternData::TAG_Value: {
                    auto& pe = pat.data.as_Value();
                    ASSERT_BUG(sp, pe.val.is_ByteString() && e.inner == HIRCoreType::U8, "Matching slice with non-byte-string value pattern - " << pat);
                    ::std::vector<u8> data;
                    data.reserve(pe.val.as_ByteString().v.size());
                    for (auto byte : pe.val.as_ByteString().v) {
                        data.push_back(static_cast<u8>(byte));
                    }
                    this->pushRule(PatternRule::make_Value(mv$(data)));
                    break;
                }
                case HIRPatternData::TAG_Slice: {
                    auto& pe = pat.data.as_Slice();
                    // Sub-patterns
                    PatternRulesetBuilder subBuilder{this->resolve, this->nextRootIndex, wildcardTypes};
                    subBuilder.fieldPath = fieldPath;
                    subBuilder.rootIndex = rootIndex;
                    subBuilder.fieldPath.push_back(0);
                    ASSERT_BUG(sp, pe.subPatterns.size() < FIELD_INDEX_MAX, "Too many slice rules to fit encodng");
                    for (const auto& subpat : pe.subPatterns) {
                        subBuilder.appendFrom(sp, subpat, e.inner);
                        subBuilder.fieldPath.back()++;
                    }

                    // Encodes length check and sub-pattern rules
                    this->multiplyRulesets(subBuilder.rulesets.size(), [&](size_t i) {
                        auto& sr = subBuilder.rulesets[i];
                        if (sr.isImpossible) {
                            this->setImpossible();
                        }
                        this->pushRule(PatternRule::make_Slice({static_cast<unsigned int>(pe.subPatterns.size()), mv$(sr.rules)}));
                        this->pushBindings(mv$(sr.bindings));
                        this->pushDerefs(mv$(sr.derefs));
                    });
                    break;
                }
                case HIRPatternData::TAG_SplitSlice: {
                    auto& pe = pat.data.as_SplitSlice();
                    PatternRulesetBuilder subBuilder{this->resolve, this->nextRootIndex, wildcardTypes};
                    subBuilder.fieldPath = fieldPath;
                    subBuilder.rootIndex = rootIndex;
                    ASSERT_BUG(sp, pe.leading.size() < FIELD_INDEX_MAX, "Too many leading slice rules to fit encodng");
                    subBuilder.fieldPath.push_back(0);
                    for (const auto& subpat : pe.leading) {
                        subBuilder.appendFrom(sp, subpat, e.inner);
                        subBuilder.fieldPath.back()++;
                    }
                    auto leadingRulesets = mv$(subBuilder.rulesets);
                    subBuilder.rulesets.clear();
                    subBuilder.rulesets.resize(1);
                    subBuilder.subsetStart = 0;
                    subBuilder.subsetEnd = 1;

                    if (pe.trailing.size()) {
                        // Needs a way of encoding the negative offset in the field path
                        // - For now, just use a very high number (and assert that it's not more than 128)
                        ASSERT_BUG(sp, pe.trailing.size() < FIELD_INDEX_MAX, "Too many trailing slice rules to fit encodng");
                        subBuilder.fieldPath.back() = FIELD_INDEX_MAX + (FIELD_INDEX_MAX - pe.trailing.size());
                        for (const auto& subpat : pe.trailing) {
                            subBuilder.appendFrom(sp, subpat, e.inner);
                            subBuilder.fieldPath.back()++;
                        }
                    }
                    auto trailingRulesets = mv$(subBuilder.rulesets);

                    if (pe.extraBind.isValid()) {
                        ASSERT_BUG(sp, pe.extraBind.implicitDerefCount == 0, "");
                        PatternBinding pb(fieldPath, pe.extraBind);
                        pb.splitSlice = std::make_pair(pe.leading.size(), pe.trailing.size());
                        this->pushBinding(mv$(pb));
                    }

                    this->multiplyRulesets(leadingRulesets.size() * trailingRulesets.size(), [&](size_t i) {
                        size_t iL = i % leadingRulesets.size();
                        size_t iT = i / leadingRulesets.size();
                        auto& srL = leadingRulesets[iL];
                        auto& srT = trailingRulesets[iT];
                        if (srL.isImpossible || srT.isImpossible) {
                            this->setImpossible();
                        }

                        auto rulesL = srL.clone();
                        auto rulesT = srT.clone();
                        this->pushRule(PatternRule::make_SplitSlice({static_cast<unsigned int>(pe.leading.size() + pe.trailing.size()), static_cast<unsigned int>(pe.trailing.size()), mv$(rulesL.rules), mv$(rulesT.rules)}));
                        this->pushBindings(mv$(rulesL.bindings));
                        this->pushBindings(mv$(rulesT.bindings));
                        this->pushDerefs(mv$(rulesL.derefs));
                        this->pushDerefs(mv$(rulesT.derefs));
                    });
                    break;
                }
            }
            break;
        }
        case HIRTypeData::TAG_Borrow: {
            auto& e = (*ty).as_Borrow();
            fieldPath.push_back(FIELD_DEREF);
            switch (pat.data.tag()) {
default:
                BUG(sp, "Matching borrow invalid pattern - " << ty << " with " << pat);
                case HIRPatternData::TAG_Any: {
                    this->appendFrom(sp, emptyPattern, e.inner);
                    break;
                }
                case HIRPatternData::TAG_Ref: {
                    auto& pe = pat.data.as_Ref();
                    this->appendFrom(sp, *pe.sub, e.inner);
                    break;
                }
                case HIRPatternData::TAG_Value: {
                    auto& pe = pat.data.as_Value();
                    // TODO: Check type?
                    if (pe.val.is_String()) {
                        const auto& s = pe.val.as_String();
                        this->pushRule(PatternRule::make_Value(s));
                    } else if (pe.val.is_ByteString()) {
                        const auto& s = pe.val.as_ByteString().v;
                        // When matching a fixed-size array, expand to per-element rules so the
                        // field paths line up with `[a, b, ...]` patterns in sibling arms.
                        if (e.inner->is_Array()) {
                            const auto& ae = e.inner->as_Array();
                            ASSERT_BUG(sp, ae.size.is_Known() && ae.size.as_Known() == s.size(), "Byte string pattern size mismatch - " << pat << " vs " << e.inner);
                            fieldPath.push_back(0);
                            for (auto c : s) {
                                this->pushRule(PatternRule::make_Value(MIRConstant::make_Uint({U128(static_cast<u8>(c)), HIRCoreType::U8})));
                                fieldPath.back()++;
                            }
                            fieldPath.pop_back();
                        } else {
                            ::std::vector<u8> data;
                            data.reserve(s.size());
                            for (auto c : s) {
                                data.push_back(c);
                            }

                            this->pushRule(PatternRule::make_Value(mv$(data)));
                        }
                    }
                    // TODO: Handle named values
                    else {
                        BUG(sp, "Matching borrow invalid pattern - " << pat);
                    }
                    break;
                }
            }
            fieldPath.pop_back();
            break;
        }
        case HIRTypeData::TAG_Pointer: {
            if (pat.data.is_Any()) {
                this->pushRule(PatternRule::make_Any({}));
            } else {
                ERROR(sp, E0000, "Attempting to match over a pointer");
            }
            break;
        }
        case HIRTypeData::TAG_NamedFunction: {
            if (pat.data.is_Any()) {
            } else {
                ERROR(sp, E0000, "Attempting to match over a functon pointer");
            }
            break;
        }
        case HIRTypeData::TAG_Function: {
            if (pat.data.is_Any()) {
            } else {
                ERROR(sp, E0000, "Attempting to match over a functon pointer");
            }
            break;
        }
        case HIRTypeData::TAG_NodeType: {
            if (pat.data.is_Any()) {
            } else {
                ERROR(sp, E0000, "Attempting to match over a closure/generator/async");
            }
            break;
        }
    }
    for(size_t i = 0; i < pat.implicitDerefCount; i ++)
    {
        fieldPath.pop_back();
    }
}

namespace {
    // Order rules ignoring inner rules
    Ordering ordRuleCompatible(const PatternRule& a, const PatternRule& b) {
        if (a.tag() != b.tag()) {
            return ::ord((unsigned)a.tag(), (unsigned)b.tag());
        }

        switch (a.tag()) {
            case PatternRule::TAG_Any: {
                return OrdEqual;
            }
            case PatternRule::TAG_Variant: {
                auto& ae = a.as_Variant();
                auto& be = b.as_Variant();
                return ::ord(ae.idx, be.idx);
            }
            case PatternRule::TAG_Slice: {
                auto& ae = a.as_Slice();
                auto& be = b.as_Slice();
                return ::ord(ae.len, be.len);
            }
            case PatternRule::TAG_SplitSlice: {
                auto& ae = a.as_SplitSlice();
                auto& be = b.as_SplitSlice();
                ORD(ae.leading, be.leading);
                // TODO: lengths?
                ORD(ae.trailing, be.trailing);
                return OrdEqual;
            }
            case PatternRule::TAG_Bool: {
                auto& ae = a.as_Bool();
                auto& be = b.as_Bool();
                return ::ord(ae, be);
            }
            case PatternRule::TAG_Value: {
                auto& ae = a.as_Value();
                auto& be = b.as_Value();
                return ::ord(ae, be);
            }
            case PatternRule::TAG_ValueRange: {
                auto& ae = a.as_ValueRange();
                auto& be = b.as_ValueRange();
                ORD(ae.first, be.first);
                ORD(ae.last, be.last);
                return ::ord(ae.isInclusive, be.isInclusive);
            }
        }
        throw "";
    }

    bool ruleCompatible(const PatternRule& a, const PatternRule& b) {
        return ordRuleCompatible(a, b) == OrdEqual;
    }

    bool rulesOverlap(const PatternRule& a, const PatternRule& b) {
        if (a.is_Any() || b.is_Any()) {
            return true;
        }

        // Defensive: If a constant is encountered, assume it overlaps with anything
        if (const auto* ae = a.opt_Value()) {
            if (ae->is_Const()) {
                return true;
            }
        }
        if (const auto* be = b.opt_Value()) {
            if (be->is_Const()) {
                return true;
            }
        }

        // A byte-string literal denotes a slice with one exact length.  For
        // reordering purposes, sequence patterns can only be proven disjoint
        // from it when their accepted length domains do not contain that
        // length; their element rules may otherwise accept the literal.
        if (const auto* ae = a.opt_Value(); ae && ae->is_Bytes()) {
            if (const auto* be = b.opt_Slice()) {
                return ae->as_Bytes().size() == be->len;
            }
            if (const auto* be = b.opt_SplitSlice()) {
                return ae->as_Bytes().size() >= be->minLen;
            }
        }
        if (const auto* be = b.opt_Value(); be && be->is_Bytes()) {
            if (const auto* ae = a.opt_Slice()) {
                return be->as_Bytes().size() == ae->len;
            }
            if (const auto* ae = a.opt_SplitSlice()) {
                return be->as_Bytes().size() >= ae->minLen;
            }
        }

        // Checks if the value is within the righthand edge of the range
        auto isWithinRight = [](const MIRConstant& c, const PatternRule::Data_ValueRange& e) -> bool {
            return (e.isInclusive ? c <= e.last : c < e.last);
        };

        // Value Range: Overlaps with contained values.
        if (const auto* ae = a.opt_ValueRange()) {
            if (const auto* be = b.opt_Value()) {
                return (ae->first <= *be && isWithinRight(*be, *ae));
            } else if (const auto* be = b.opt_ValueRange()) {
                // Range starts are inclusive, while their ends can be either
                // inclusive or exclusive.
                if (ae->last < be->first || (ae->last == be->first && !ae->isInclusive)) {
                    return false;
                }
                if (be->last < ae->first || (be->last == ae->first && !be->isInclusive)) {
                    return false;
                }
                return true;
            } else {
                TODO(Span(), "Check overlap of " << a << " and " << b);
            }
        }
        if (const auto* be = b.opt_ValueRange()) {
            if (const auto* ae = a.opt_Value()) {
                if (be->isInclusive) {
                    return (be->first <= *ae && *ae <= be->last);
                } else {
                    return (be->first <= *ae && *ae < be->last);
                }
            }
            // Note: A can't be ValueRange
            else {
                TODO(Span(), "Check overlap of " << a << " and " << b);
            }
        }

        // SplitSlice patterns overlap with other SplitSlice patterns and larger slices
        if (const auto* ae = a.opt_SplitSlice()) {
            if (b.is_SplitSlice()) {
                return true;
            } else if (const auto* be = b.opt_Slice()) {
                return be->len >= ae->minLen;
            } else {
                TODO(Span(), "Check overlap of " << a << " and " << b);
            }
        }
        if (const auto* be = b.opt_SplitSlice()) {
            if (const auto* ae = a.opt_Slice()) {
                return ae->len >= be->minLen;
            } else {
                TODO(Span(), "Check overlap of " << a << " and " << b);
            }
        }

        // Otherwise, If rules are approximately equal, they overlap
        return (ordRuleCompatible(a, b) == OrdEqual);
    }
}

void sortRulesets(RulesetRef rulesets, size_t idx) {
    if (rulesets.size() < 2) {
        return;
    }

    // NOTE: Assumption kinda breaks with byte string literals
    //for(size_t i = 0; i < rulesets.size(); i ++)

    // Multiple rules, but no checks within then (can happen with `match () { _ if foo => ..., _ => ... }`)
    if (rulesets[0].size() == 0) {
        return;
    }

    bool foundNonAny = false;
    for (size_t i = 0; i < rulesets.size(); i++) {
        assert(idx < rulesets[i].size());
        if (!rulesets[i][idx].is_Any()) {
            foundNonAny = true;
        }
    }
    if (foundNonAny) {
        TRACE_FUNCTION_F(idx);
        for (size_t i = 0; i < rulesets.size(); i++) {
            DEBUG("- " << i << ": " << rulesets[i]);
        }

        bool actionTaken;
        do {
            actionTaken = false;
            for (size_t i = 0; i < rulesets.size() - 1; i++) {
                if (rulesOverlap(rulesets[i][idx], rulesets[i + 1][idx])) {
                    // Don't move
                } else if (ordRuleCompatible(rulesets[i][idx], rulesets[i + 1][idx]) == OrdGreater) {
                    rulesets.swap(i, i + 1);
                    actionTaken = true;
                } else {
                }
            }
        } while (actionTaken);
        for (size_t i = 0; i < rulesets.size(); i++) {
            DEBUG("- " << i << ": " << rulesets[i]);
        }

        // TODO: Print sorted ruleset

        // Where compatible, sort insides
        size_t start = 0;
        for (size_t i = 1; i < rulesets.size(); i++) {
            if (ordRuleCompatible(rulesets[i][idx], rulesets[start][idx]) != OrdEqual) {
                sortRulesetsInner(rulesets.slice(start, i - start), idx);
                start = i;
            }
        }
        sortRulesetsInner(rulesets.slice(start, rulesets.size() - start), idx);

        // Iterate onwards where rules are equal
        if (idx + 1 < rulesets[0].size()) {
            size_t start = 0;
            for (size_t i = 1; i < rulesets.size(); i++) {
                if (rulesets[i][idx] != rulesets[start][idx]) {
                    sortRulesets(rulesets.slice(start, i - start), idx + 1);
                    start = i;
                }
            }
            sortRulesets(rulesets.slice(start, rulesets.size() - start), idx + 1);
        }
    } else {
        if (idx + 1 < rulesets[0].size()) {
            sortRulesets(rulesets, idx + 1);
        }
    }
}

void sortRulesetsInner(RulesetRef rulesets, size_t idx) {
    TRACE_FUNCTION_F(idx << " - " << rulesets[0][idx].tagStr());
    if (const auto* re = rulesets[0][idx].opt_Variant()) {
        // Sort rules based on contents of enum
        if (re->subRules.size() > 0) {
            sortRulesets(RulesetRef(rulesets, idx), 0);
        }
    }
}

namespace {
    void getTyAndVal(
        const Span& sp,
        MirBuilder& builder,
        const HIRTypeData* topTy,
        const MIRLValue& topVal,
        const fieldPathT& fieldPath,
        unsigned int fieldPathOfs,
        /*Out ->*/ HIRTypeRef& outTy,
        MIRLValue& outVal
    ) {
        const StaticTraitResolve& resolve = builder.resolve();
        MIRLValue lval = topVal.clone();
        HIRTypeRef tmpTy = topTy;
        const HIRTypeData* curTy = topTy;
        auto revealCurTy = [&]() {
            tmpTy = curTy;
            resolve.revealOpaqueTypes(sp, tmpTy);
            curTy = tmpTy;
        };

        // TODO: Cache the correspondance of path->type (lval can be inferred)
        ASSERT_BUG(sp, fieldPathOfs <= fieldPath.size(), "Field path offset " << fieldPathOfs << " is larger than the path [" << fieldPath << "]");
        for (unsigned int i = fieldPathOfs; i < fieldPath.size(); i++) {
            revealCurTy();
            unsigned idx = fieldPath.data[i];
            DEBUG("> " << curTy << " #" << idx);

            switch ((*curTy).tag()) {
                case HIRTypeData::TAG_Infer: {
                    BUG(sp, "Ivar for in match type");
                    break;
                }
                case HIRTypeData::TAG_Diverge: {
                    BUG(sp, "Diverge in match type");
                    break;
                }
                case HIRTypeData::TAG_Primitive: {
                    BUG(sp, "Destructuring a primitive");
                    break;
                }
                case HIRTypeData::TAG_Pattern: {
                    BUG(sp, "Destructuring a pattern type");
                    break;
                }
                case HIRTypeData::TAG_Tuple: {
                    auto& e = (*curTy).as_Tuple();
                    ASSERT_BUG(sp, idx < e.size(), "Tuple index out of range");
                    lval = MIRLValue::newField(mv$(lval), idx);
                    curTy = e[idx];
                    break;
                }
                case HIRTypeData::TAG_Path: {
                    auto& e = (*curTy).as_Path();
                    if (idx == FIELD_DEREF) {
                            auto newTy = resolve.isTypeOwnedBox(curTy);
                            ASSERT_BUG(sp, newTy, "Deref on non-Box - " << curTy);
                            lval = MIRLValue::newDeref(mv$(lval));
                            curTy = newTy;
                            break;
                        }
                        auto monomorphToPtr = [&](const HIRTypeData* ty) -> const HIRTypeData* {
                            if (monomorphiseTypeNeeded(ty)) {
                                auto rv = MonomorphStatePtr(resolve.hirCrate().types, nullptr, &e.path.data.as_Generic().params, nullptr).monomorphType(sp, ty);
                                resolve.expandAssociatedTypes(sp, rv);
                                tmpTy = mv$(rv);
                                return tmpTy;
                            } else {
                                return ty;
                            }
                        };
                    switch (e.binding.tag()) {
                        case HIRTypePathBinding::TAG_Unbound: {
                            BUG(sp, "Encounterd unbound path - " << e.path);
                            break;
                        }
                        case HIRTypePathBinding::TAG_Opaque: {
                            BUG(sp, "Destructuring an opaque type - " << curTy);
                            break;
                        }
                        case HIRTypePathBinding::TAG_ExternType: {
                            BUG(sp, "Destructuring an extern type - " << curTy);
                            break;
                        }
                        case HIRTypePathBinding::TAG_Struct: {
                            auto& pbe = e.binding.as_Struct();
                            switch (pbe->data.tag()) {
                                case HIRStructData::TAG_Unit: {
                                    BUG(sp, "Destructuring an unit-like tuple - " << curTy);
                                    break;
                                }
                                case HIRStructData::TAG_Tuple: {
                                    auto& fields = pbe->data.as_Tuple();
                                    ASSERT_BUG(sp, idx < fields.size(), "Tuple struct index (" << idx << ") out of range (" << fields.size() << ") in " << curTy);
                                    const auto& fld = fields[idx];
                                    curTy = monomorphToPtr(fld.ent);
                                    lval = MIRLValue::newField(mv$(lval), idx);
                                    break;
                                }
                                case HIRStructData::TAG_Named: {
                                    auto& fields = pbe->data.as_Named();
                                    ASSERT_BUG(sp, idx < fields.size(), "Tuple struct index (" << idx << ") out of range (" << fields.size() << ") in " << curTy);
                                    const auto& fld = fields[idx];
                                    curTy = monomorphToPtr(fld.ty);
                                    lval = MIRLValue::newField(mv$(lval), idx);
                                    break;
                                }
                            }
                            break;
                        }
                        case HIRTypePathBinding::TAG_Union: {
                            auto& pbe = e.binding.as_Union();
                            ASSERT_BUG(sp, idx < pbe->variants.size(), "Union variant index (" << idx << ") out of range (" << pbe->variants.size() << ") in " << curTy);
                            const auto& fld = pbe->variants[idx];
                            curTy = monomorphToPtr(fld.ty);
                            lval = MIRLValue::newDowncast(mv$(lval), idx);
                            break;
                        }
                        case HIRTypePathBinding::TAG_Enum: {
                            auto& pbe = e.binding.as_Enum();
                            ASSERT_BUG(sp, pbe->data.is_Data(), "Value enum being destructured - " << curTy);
                            const auto& variants = pbe->data.as_Data();
                            ASSERT_BUG(sp, idx < variants.size(), "Variant index (" << idx << ") out of range (" << variants.size() << ") for enum " << curTy);
                            const auto& var = variants[idx];

                            curTy = monomorphToPtr(var.type);
                            lval = MIRLValue::newDowncast(mv$(lval), idx);
                            break;
                        }
                    }
                    break;
                }
                case HIRTypeData::TAG_Generic: {
                    BUG(sp, "Destructuring a generic - " << curTy);
                    break;
                }
                case HIRTypeData::TAG_TraitObject: {
                    BUG(sp, "Destructuring a trait object - " << curTy);
                    break;
                }
                case HIRTypeData::TAG_ErasedType: {
                    BUG(sp, "Destructuring an erased type - " << curTy);
                    break;
                }
                case HIRTypeData::TAG_Array: {
                    auto& e = (*curTy).as_Array();
                    curTy = e.inner;
                    if (idx < FIELD_INDEX_MAX) {
                        ASSERT_BUG(sp, idx < e.size.as_Known(), "Index out of range");
                        lval = MIRLValue::newField(mv$(lval), idx);
                    } else {
                        idx -= FIELD_INDEX_MAX;
                        idx = FIELD_INDEX_MAX - idx;
                        ASSERT_BUG(sp, idx < e.size.as_Known(), "Index out of range");
                        TODO(sp, "Index " << idx << " from end of array " << lval);
                    }
                    break;
                }
                case HIRTypeData::TAG_Slice: {
                    auto& e = (*curTy).as_Slice();
                    curTy = e.inner;
                    if (idx < FIELD_INDEX_MAX) {
                        lval = MIRLValue::newField(mv$(lval), idx);
                    } else {
                        idx -= FIELD_INDEX_MAX;
                        idx = FIELD_INDEX_MAX - idx;
                        // 1. Create an LValue containing the size of this slice subtract `idx`
                        auto lenLval = builder.lvalueOrTemp(sp, builder.resolve().crate.types.primitive(HIRCoreType::Usize), MIRRValue::make_DstMeta({builder.getPtrToDst(sp, lval)}));
                        auto subVal = MIRParam(MIRConstant::make_Uint({U128(idx), HIRCoreType::Usize}));
                        auto ofsVal = builder.lvalueOrTemp(sp, builder.resolve().crate.types.primitive(HIRCoreType::Usize), MIRRValue::make_BinOp({mv$(lenLval), MIRBinOp::SUB, mv$(subVal)}));
                        // 2. Return _Index with that value
                        lval = MIRLValue::newIndex(mv$(lval), ofsVal.as_Local());
                    }
                    break;
                }
                case HIRTypeData::TAG_Borrow: {
                    auto& e = (*curTy).as_Borrow();
                    ASSERT_BUG(sp, idx == FIELD_DEREF, "Destructure of borrow doesn't correspond to a deref in the path");
                    curTy = e.inner;
                    lval = MIRLValue::newDeref(mv$(lval));
                    break;
                }
                case HIRTypeData::TAG_Pointer: {
                    ERROR(sp, E0000, "Attempting to match over a pointer");
                    break;
                }
                case HIRTypeData::TAG_NamedFunction: {
                    ERROR(sp, E0000, "Attempting to match over a functon pointer");
                    break;
                }
                case HIRTypeData::TAG_Function: {
                    ERROR(sp, E0000, "Attempting to match over a functon pointer");
                    break;
                }
                case HIRTypeData::TAG_NodeType: {
                    ERROR(sp, E0000, "Attempting to match over a magic type");
                    break;
                }
            }
        }

        revealCurTy();

        if (const auto* pattern = curTy->opt_Pattern()) {
            lval = builder.lvalueOrTemp(sp, pattern->inner, MIRRValue::make_Cast({mv$(lval), pattern->inner}));
            curTy = pattern->inner;
        }
        outTy = curTy;
        outVal = mv$(lval);
    }
}

// --------------------------------------------------------------------
// Dumb and Simple
// --------------------------------------------------------------------

namespace {
    void getPatternRoot(const Span& sp, const PatternRuleset& ruleset, unsigned rootIndex, const HIRTypeData* topTy, const MIRLValue& topVal, HIRTypeRef& rootTy, MIRLValue& rootVal) {
        if (rootIndex == 0) {
            rootTy = topTy;
            rootVal = topVal.clone();
            return;
        }
        const auto derefIt = ::std::find_if(ruleset.derefs.begin(), ruleset.derefs.end(), [&](const auto& deref) {
            return deref.rootIndex == rootIndex;
        });
        ASSERT_BUG(sp, derefIt != ruleset.derefs.end(), "Invalid pattern root " << rootIndex);
        const auto& deref = *derefIt;
        ASSERT_BUG(sp, deref.resultLocal != ~0u, "Pattern deref root has no MIR local");
        rootTy = deref.targetType;
        rootVal = MIRLValue::newDeref(MIRLValue::newLocal(deref.resultLocal));
    }
}

void allocatePatternDerefLocals(MirBuilder& builder, PatternRuleset& ruleset) {
    for (auto& deref : ruleset.derefs) {
        const auto borrow = deref.kind == HIRPattern::DerefKind::Unique ? HIRBorrowType::Unique : HIRBorrowType::Shared;
        deref.resultLocal = builder.newTemporary(builder.resolve().crate.types.borrow(borrow, deref.targetType)).as_Local();
    }
}

void materializePatternDerefs(MirBuilder& builder, const Span& sp, const PatternRuleset& ruleset, const HIRTypeData* topTy, const MIRLValue& topVal) {
    for (const auto& deref : ruleset.derefs) {
        HIRTypeRef parentTy;
        MIRLValue parentVal;
        getPatternRoot(sp, ruleset, deref.parentRoot, topTy, topVal, parentTy, parentVal);

        HIRTypeRef sourceTy;
        MIRLValue sourceVal;
        getTyAndVal(sp, builder, parentTy, parentVal, deref.field, 0, sourceTy, sourceVal);
        ASSERT_BUG(sp, sourceTy == deref.sourceType, "Deref pattern source changed from " << deref.sourceType << " to " << sourceTy);

        const auto borrow = deref.kind == HIRPattern::DerefKind::Unique ? HIRBorrowType::Unique : HIRBorrowType::Shared;
        const char* langItem = borrow == HIRBorrowType::Unique ? "deref_mut" : "deref";
        const char* method = borrow == HIRBorrowType::Unique ? "deref_mut" : "deref";
        auto argument = builder.lvalueOrTemp(sp, builder.resolve().crate.types.borrow(borrow, sourceTy), MIRRValue::make_Borrow({borrow, false, mv$(sourceVal)}));
        builder.movedLvalue(sp, argument);

        auto okBlock = builder.newBbUnlinked();
        auto unwindBlock = builder.newBbUnlinked();
        auto methodPath = HIRPath(sourceTy, HIRGenericPath(builder.resolve().crate.getLangItemPath(sp, langItem), {}), method, HIRPathParams());
        builder.endBlock(MIRTerminator::make_Call({
            okBlock,
            MIRUnwindAction::make_Cleanup(unwindBlock),
            MIRLValue::newLocal(deref.resultLocal),
            mv$(methodPath),
            makeVec1(MIRParam(mv$(argument))),
        }));
        builder.setCurBlock(unwindBlock);
        builder.emitUnwindCleanup(sp);
        if (builder.blockActive()) builder.endBlock(MIRTerminator::make_UnwindResume({}));
        builder.setCurBlock(okBlock);
    }
}

MIRLValue getPatternBindingValue(MirConverter& conv, const Span& sp, const PatternRuleset& ruleset, const HIRTypeData* topTy, const MIRLValue& topVal, const PatternBinding& binding) {
    HIRTypeRef rootTy;
    MIRLValue rootVal;
    getPatternRoot(sp, ruleset, binding.rootIndex, topTy, topVal, rootTy, rootVal);
    return conv.getValueForBindingPath(sp, rootTy, rootVal, binding);
}

void destructurePatternRuleset(MirConverter& conv, const Span& sp, const PatternRuleset& ruleset, const HIRTypeData* topTy, const MIRLValue& topVal, bool updateStates) {
    if (ruleset.derefs.empty()) {
        conv.destructureFromList(sp, topTy, topVal.clone(), ruleset.bindings, updateStates);
        return;
    }
    for (size_t i = ruleset.bindings.size(); i--;) {
        const auto& binding = ruleset.bindings[i];
        HIRTypeRef rootTy;
        MIRLValue rootVal;
        getPatternRoot(sp, ruleset, binding.rootIndex, topTy, topVal, rootTy, rootVal);
        conv.destructureFromList(sp, rootTy, mv$(rootVal), ::std::vector<PatternBinding>{binding}, updateStates);
    }
}

void MIRLowerHIRMatchSimple(MirBuilder& builder, MirConverter& conv, HIRExprNodeMatch& node, MIRLValue matchVal, tArmRules armRules, ::std::vector<ArmCode> armsCode, MIRBasicBlockId firstCmpBlock) {
    TRACE_FUNCTION;

    // 1. Generate pattern matches
    builder.setCurBlock(firstCmpBlock);
    auto nextArmBb = builder.newBbUnlinked();
    size_t prevArmIdx = !armRules.empty() ? armRules[0].armIdx : 0;
    for (const auto& patRule : armRules) {
        if (patRule.armIdx != prevArmIdx) {
            DEBUG("New arm (" << prevArmIdx << " -> " << patRule.armIdx << ")");
            prevArmIdx = patRule.armIdx;
            builder.endBlock(MIRTerminator::make_Goto(nextArmBb));
            builder.setCurBlock(nextArmBb);
            nextArmBb = builder.newBbUnlinked();
        }
        const auto& arm = node.arms[patRule.armIdx];
        const auto& rc = armsCode[patRule.armIdx].rules[patRule.armRuleIdx];
        auto nextPatternBb = builder.newBbUnlinked();

        // 1. Check
        // - If the ruleset is empty, this is a _ arm over a value
        materializePatternDerefs(builder, arm.code->span(), patRule, node.value->resType, matchVal);
        if (patRule.rules.size() > 0) {
            MIRLowerHIRMatchSimpleGeneratePattern(builder, arm.code->span(), &patRule, patRule.rules.data(), patRule.rules.size(), node.value->resType, matchVal, 0, nextPatternBb);
        }
        builder.endBlock(MIRTerminator::make_Goto(rc.entry));

        // - Update the condition's failure target
        if (armsCode[patRule.armIdx].hasCondition && (patRule.armRuleIdx == 0 || rc.condFalse != armsCode[patRule.armIdx].rules[0].condFalse)) {
            builder.setCurBlock(rc.condFalse);
            // A guard belongs to this expanded pattern candidate, not to the
            // arm as a whole.  If it fails, another or-pattern candidate from
            // the same arm must still be tested before advancing to the next
            // arm.
            builder.endBlock(MIRTerminator::make_Goto(nextPatternBb));
        }

        builder.setCurBlock(nextPatternBb);
    }
    // - Kill the final pattern block (which is dead code)
    builder.endBlock(MIRTerminator::make_Unreachable({}));
    builder.setCurBlock(nextArmBb);
    builder.endBlock(MIRTerminator::make_Unreachable({}));
}

int MIRLowerHIRMatchSimpleGeneratePattern(MirBuilder& builder, const Span& sp, const PatternRuleset* ruleset, const PatternRule* rules, unsigned int numRules, const HIRTypeData* topTy, const MIRLValue& topVal, unsigned int fieldPathOfs, MIRBasicBlockId failBb) {
    TRACE_FUNCTION_F("top_ty = " << topTy << ", rules = [" << FMT_CB(os, for (size_t i = 0; i < numRules; i++) os << rules[i] << ",";));
    for (unsigned int ruleIdx = 0; ruleIdx < numRules; ruleIdx++) {
        const auto& rule = rules[ruleIdx];
        DEBUG("rule = " << rule);

        // Don't emit anything for '_' matches
        if (rule.is_Any()) {
            continue;
        }

        MIRLValue val;
        HIRTypeRef ity;

        if (rule.rootIndex == 0) {
            getTyAndVal(sp, builder, topTy, topVal, rule.fieldPath, fieldPathOfs, ity, val);
        } else {
            ASSERT_BUG(sp, ruleset, "Adjusted pattern rule without a ruleset");
            HIRTypeRef rootTy;
            MIRLValue rootVal;
            getPatternRoot(sp, *ruleset, rule.rootIndex, topTy, topVal, rootTy, rootVal);
            getTyAndVal(sp, builder, rootTy, rootVal, rule.fieldPath, 0, ity, val);
        }
        DEBUG("ty = " << ity << ", val = " << val);

        const auto& ty = ity;
        switch ((*ty).tag()) {
            case HIRTypeData::TAG_Infer: {
                BUG(sp, "Hit _ in type - " << ty);
                break;
            }
            case HIRTypeData::TAG_Diverge: {
                BUG(sp, "Matching over !");
                break;
            }
            case HIRTypeData::TAG_Primitive: {
                auto& te = (*ty).as_Primitive();
                switch (te) {
                        case HIRCoreType::Bool: {
                            ASSERT_BUG(sp, rule.is_Bool(), "PatternRule for bool isn't _Bool");
                            bool testVal = rule.as_Bool();

                            auto succBb = builder.newBbUnlinked();

                            if (testVal) {
                                builder.endBlock(MIRTerminator::make_If({val.clone(), succBb, failBb}));
                            } else {
                                builder.endBlock(MIRTerminator::make_If({val.clone(), failBb, succBb}));
                            }
                            builder.setCurBlock(succBb);
                        } break;
                        case HIRCoreType::U8:
                        case HIRCoreType::U16:
                        case HIRCoreType::U32:
                        case HIRCoreType::U64:
                        case HIRCoreType::U128:
                        case HIRCoreType::Usize:
                    switch (rule.tag()) {
default:
                        BUG(sp, "PatternRule for integer is not Value or ValueRange");
                        case PatternRule::TAG_Value: {
                            auto& re = rule.as_Value();
                            auto succBb = builder.newBbUnlinked();

                            auto testVal = MIRParam(MIRConstant::make_Uint({re.as_Uint().v, te}));
                            builder.pushStmtAssign(sp, builder.getIfCond(), MIRRValue::make_BinOp({val.clone(), MIRBinOp::EQ, mv$(testVal)}));
                            builder.endBlock(MIRTerminator::make_If({builder.getIfCond(), succBb, failBb}));
                            builder.setCurBlock(succBb);
                            break;
                        }
                        case PatternRule::TAG_ValueRange: {
                            auto& re = rule.as_ValueRange();
                            auto succBb = builder.newBbUnlinked();

                            // IF `val` < `first` : fail_bb
                            if (re.first.as_Uint().v != 0) {
                                auto testBb2 = builder.newBbUnlinked();
                                auto testLtVal = MIRParam(MIRConstant::make_Uint({re.first.as_Uint().v, te}));
                                auto cmpLtLval = builder.lvalueOrTemp(sp, builder.resolve().crate.types.primitive(HIRCoreType::Bool), MIRRValue::make_BinOp({MIRParam(val.clone()), MIRBinOp::LT, mv$(testLtVal)}));
                                builder.endBlock(MIRTerminator::make_If({mv$(cmpLtLval), failBb, testBb2}));

                                builder.setCurBlock(testBb2);
                            }

                            // IF `val` > `last` : fail_bb
                            if (re.last.as_Uint().v == U128::max() && re.isInclusive) {
                                builder.endBlock(MIRTerminator::make_Goto({succBb}));
                            } else {
                                auto testGtVal = MIRParam(MIRConstant::make_Uint({re.last.as_Uint().v, te}));
                                auto op = re.isInclusive ? MIRBinOp::GT : MIRBinOp::GE;
                                auto cmpGtLval = builder.lvalueOrTemp(sp, builder.resolve().crate.types.primitive(HIRCoreType::Bool), MIRRValue::make_BinOp({MIRParam(val.clone()), op, mv$(testGtVal)}));
                                builder.endBlock(MIRTerminator::make_If({mv$(cmpGtLval), failBb, succBb}));
                            }

                            builder.setCurBlock(succBb);
                            break;
                        }
                    }
                    break;
                case HIRCoreType::I8:
                case HIRCoreType::I16:
                case HIRCoreType::I32:
                case HIRCoreType::I64:
                case HIRCoreType::I128:
                case HIRCoreType::Isize:
                    switch (rule.tag()) {
default:
                        BUG(sp, "PatternRule for integer is not Value or ValueRange");
                        case PatternRule::TAG_Value: {
                            auto& re = rule.as_Value();
                            auto succBb = builder.newBbUnlinked();

                            auto testVal = MIRParam(MIRConstant::make_Int({re.as_Int().v, te}));
                            auto cmpLval = builder.lvalueOrTemp(sp, builder.resolve().crate.types.primitive(HIRCoreType::Bool), MIRRValue::make_BinOp({val.clone(), MIRBinOp::EQ, mv$(testVal)}));
                            builder.endBlock(MIRTerminator::make_If({mv$(cmpLval), succBb, failBb}));
                            builder.setCurBlock(succBb);
                            break;
                        }
                        case PatternRule::TAG_ValueRange: {
                            auto& re = rule.as_ValueRange();
                            auto succBb = builder.newBbUnlinked();

                            // IF `val` < `first` : fail_bb
                            if (re.first.as_Int().v != S128::min()) {
                                auto testBb2 = builder.newBbUnlinked();
                                auto testLtVal = MIRParam(MIRConstant::make_Int({re.first.as_Int().v, te}));
                                auto cmpLtLval = builder.lvalueOrTemp(sp, builder.resolve().crate.types.primitive(HIRCoreType::Bool), MIRRValue::make_BinOp({MIRParam(val.clone()), MIRBinOp::LT, mv$(testLtVal)}));
                                builder.endBlock(MIRTerminator::make_If({mv$(cmpLtLval), failBb, testBb2}));
                                builder.setCurBlock(testBb2);
                            }

                            // IF `val` > `last` : fail_bb
                            if (re.last.as_Int().v == S128::max() && re.isInclusive) {
                                builder.endBlock(MIRTerminator::make_Goto({succBb}));
                            } else {
                                auto testGtVal = MIRParam(MIRConstant::make_Int({re.last.as_Int().v, te}));
                                auto op = re.isInclusive ? MIRBinOp::GT : MIRBinOp::GE;
                                auto cmpGtLval = builder.lvalueOrTemp(sp, builder.resolve().crate.types.primitive(HIRCoreType::Bool), MIRRValue::make_BinOp({MIRParam(val.clone()), op, mv$(testGtVal)}));
                                builder.endBlock(MIRTerminator::make_If({mv$(cmpGtLval), failBb, succBb}));
                            }

                            builder.setCurBlock(succBb);
                            break;
                        }
                    }
                    break;
                case HIRCoreType::Char:
                    switch (rule.tag()) {
                        case PatternRule::TAG_Value: {
                            auto& re = rule.as_Value();
                            auto succBb = builder.newBbUnlinked();

                            auto testVal = MIRParam(MIRConstant::make_Uint({ re.as_Uint().v, te }));
                            auto cmpLval = builder.lvalueOrTemp(sp, builder.resolve().crate.types.primitive(HIRCoreType::Bool), MIRRValue::make_BinOp({ MIRParam(val.clone()), MIRBinOp::EQ, mv$(testVal) }));
                            builder.endBlock( MIRTerminator::make_If({ mv$(cmpLval), succBb, failBb }) );
                            builder.setCurBlock(succBb);
                            break;
                        }
                        case PatternRule::TAG_ValueRange: {
                            auto& re = rule.as_ValueRange();
                            auto succBb = builder.newBbUnlinked();

                            // IF `val` < `first` : fail_bb
                            if( re.first.as_Uint().v != 0 ) {
                                    auto testBb2 = builder.newBbUnlinked();

                                    auto testLtVal = MIRParam(MIRConstant::make_Uint({re.first.as_Uint().v, te}));
                                    auto cmpLtLval = builder.lvalueOrTemp(sp, builder.resolve().crate.types.primitive(HIRCoreType::Bool), MIRRValue::make_BinOp({MIRParam(val.clone()), MIRBinOp::LT, mv$(testLtVal)}));
                                    builder.endBlock(MIRTerminator::make_If({mv$(cmpLtLval), failBb, testBb2}));

                                    builder.setCurBlock(testBb2);
                            }

                            // IF `val` > `last` : fail_bb
                            if(re.last.as_Uint().v >= 0x10FFFF ) {
                                    assert(re.isInclusive);
                                    builder.endBlock(MIRTerminator::make_Goto({succBb}));
                            }
                            else {
                                    auto testGtVal = MIRParam(MIRConstant::make_Uint({re.last.as_Uint().v, te}));
                                    auto op = re.isInclusive ? MIRBinOp::GT : MIRBinOp::GE;
                                    auto cmpGtLval = builder.lvalueOrTemp(sp, builder.resolve().crate.types.primitive(HIRCoreType::Bool), MIRRValue::make_BinOp({MIRParam(val.clone()), op, mv$(testGtVal)}));
                                    builder.endBlock(MIRTerminator::make_If({mv$(cmpGtLval), failBb, succBb}));
                            }

                            builder.setCurBlock(succBb);
                            break;
                        }
                        default: {

                            BUG(sp, "PatternRule for char is not Value or ValueRange");

                            break;
                        }
                    }
                    break;
                case HIRCoreType::F16:
                case HIRCoreType::F32:
                case HIRCoreType::F64:
                case HIRCoreType::F128:
                    switch (rule.tag()) {
                        case PatternRule::TAG_Value: {
                            auto& re = rule.as_Value();
                            auto succBb = builder.newBbUnlinked();

                            auto testVal = MIRParam(MIRConstant::make_Float({ re.as_Float().v, te }));
                            auto cmpLval = builder.lvalueOrTemp(sp, builder.resolve().crate.types.primitive(HIRCoreType::Bool), MIRRValue::make_BinOp({ val.clone(), MIRBinOp::EQ, mv$(testVal) }));
                            builder.endBlock( MIRTerminator::make_If({ mv$(cmpLval), succBb, failBb }) );
                            builder.setCurBlock(succBb);
                            break;
                        }
                        case PatternRule::TAG_ValueRange: {
                            auto& re = rule.as_ValueRange();
                            auto succBb = builder.newBbUnlinked();

                            // IF `val` < `first` : fail_bb
                            if( re.first.as_Float().v == -std::numeric_limits<double>::infinity()) {
                            }
                            else {
                                    auto testBb2 = builder.newBbUnlinked();
                                    auto testLtVal = MIRParam(MIRConstant::make_Float({re.first.as_Float().v, te}));
                                    auto cmpLtLval = builder.lvalueOrTemp(sp, builder.resolve().crate.types.primitive(HIRCoreType::Bool), MIRRValue::make_BinOp({MIRParam(val.clone()), MIRBinOp::LT, mv$(testLtVal)}));
                                    builder.endBlock(MIRTerminator::make_If({mv$(cmpLtLval), failBb, testBb2}));
                                    builder.setCurBlock(testBb2);
                            }

                            // IF `val` > `last` : fail_bb
                            if( re.first.as_Float().v == std::numeric_limits<double>::infinity() && re.isInclusive ) {
                                    builder.endBlock(MIRTerminator::make_Goto({succBb}));
                            }
                            else {
                                    auto testGtVal = MIRParam(MIRConstant::make_Float({re.last.as_Float().v, te}));
                                    auto op = re.isInclusive ? MIRBinOp::GT : MIRBinOp::GE;
                                    auto cmpGtLval = builder.lvalueOrTemp(sp, builder.resolve().crate.types.primitive(HIRCoreType::Bool), MIRRValue::make_BinOp({MIRParam(val.clone()), op, mv$(testGtVal)}));
                                    builder.endBlock(MIRTerminator::make_If({mv$(cmpGtLval), failBb, succBb}));
                            }

                            builder.setCurBlock(succBb);
                            break;
                        }
                        default: {

                            BUG(sp, "PatternRule for float is not Value or ValueRange");

                            break;
                        }
                    }
                    break;
                case HIRCoreType::Str: {
                                ASSERT_BUG(sp, rule.is_Value() && rule.as_Value().is_StaticString(), "Unexpected use of non-value pattern on `str`");
                                const auto& v = rule.as_Value();
                                ASSERT_BUG(sp, val.is_Deref(), "");
                                val.wrappers.pop_back();
                                auto strVal = mv$(val);

                                auto succBb = builder.newBbUnlinked();

                                auto testVal = MIRParam(MIRConstant(v.as_StaticString()));
                                auto cmpLval = builder.lvalueOrTemp(sp, builder.resolve().crate.types.primitive(HIRCoreType::Bool), MIRRValue::make_BinOp({mv$(strVal), MIRBinOp::EQ, mv$(testVal)}));
                                builder.endBlock(MIRTerminator::make_If({mv$(cmpLval), succBb, failBb}));
                                builder.setCurBlock(succBb);
                    } break;
                    }
                break;
            }
            case HIRTypeData::TAG_Pattern: {
                BUG(sp, "Pattern type was not reduced to its base type");
                break;
            }
            case HIRTypeData::TAG_Path: {
                auto& te = (*ty).as_Path();
                switch (te.binding.tag()) {
                    case HIRTypePathBinding::TAG_Unbound: {
                        BUG(sp, "Encounterd unbound path - " << te.path);
                        break;
                    }
                    case HIRTypePathBinding::TAG_Opaque: {
                        BUG(sp, "Attempting to match over opaque type - " << ty);
                        break;
                    }
                    case HIRTypePathBinding::TAG_Struct: {
                        auto& pbe = te.binding.as_Struct();
                        const auto& strData = pbe->data;
                        switch (strData.tag()) {
                            case HIRStructData::TAG_Unit: {
                                BUG(sp, "Attempting to match over unit type - " << ty);
                                break;
                            }
                            case HIRStructData::TAG_Tuple: {
                                TODO(sp, "Matching on tuple-like struct?");
                                break;
                            }
                            case HIRStructData::TAG_Named: {
                                TODO(sp, "Matching on struct?");
                                break;
                            }
                        }
                        break;
                    }
                    case HIRTypePathBinding::TAG_Union: {
                        TODO(sp, "Match over Union");
                        break;
                    }
                    case HIRTypePathBinding::TAG_ExternType: {
                        TODO(sp, "Match over ExternType");
                        break;
                    }
                    case HIRTypePathBinding::TAG_Enum: {
                        auto& pbe = te.binding.as_Enum();
                        auto monomorph = [&](const auto& ty) {
                            auto rv = MonomorphStatePtr(builder.resolve().crate.types, nullptr, &te.path.data.as_Generic().params, nullptr).monomorphType(sp, ty);
                            builder.resolve().expandAssociatedTypes(sp, rv);
                            return rv;
                        };
                        ASSERT_BUG(sp, rule.is_Variant(), "Rule for enum isn't Any or Variant");
                        const auto& re = rule.as_Variant();
                        unsigned int varIdx = re.idx;

                        auto nextBb = builder.newBbUnlinked();
                        auto varCount = pbe->numVariants();

                        // Generate a switch with only one option different.
                        ::std::vector<MIRBasicBlockId> arms(varCount, failBb);
                        arms[varIdx] = nextBb;
                        builder.endBlock(MIRTerminator::make_Switch({val.clone(), mv$(arms)}));

                        builder.setCurBlock(nextBb);

                        if (re.subRules.size() > 0) {
                            ASSERT_BUG(sp, pbe->data.is_Data(), "Sub-rules present for non-data enum");
                            const auto& variants = pbe->data.as_Data();
                            const auto& varTy = variants.at(re.idx).type;
                            HIRTypeRef tmp;
                            const auto& varTyM = (monomorphiseTypeNeeded(varTy) ? tmp = monomorph(varTy) : varTy);

                            // Recurse with the new ruleset
                            MIRLowerHIRMatchSimpleGeneratePattern(builder, sp, ruleset, re.subRules.data(), re.subRules.size(), varTyM, MIRLValue::newDowncast(val.clone(), varIdx), rule.fieldPath.size() + 1, failBb);
                        }
                        break;
                    }
                }
                break;
            }
            case HIRTypeData::TAG_Generic: {
                BUG(sp, "Attempting to match a generic");
                break;
            }
            case HIRTypeData::TAG_TraitObject: {
                BUG(sp, "Attempting to match a trait object");
                break;
            }
            case HIRTypeData::TAG_ErasedType: {
                BUG(sp, "Attempting to match an erased type");
                break;
            }
            case HIRTypeData::TAG_Array: {
                TODO(sp, "Match directly on array?");
                break;
            }
            case HIRTypeData::TAG_Slice: {
                auto& te = (*ty).as_Slice();
                ASSERT_BUG(sp, rule.is_Slice() || rule.is_SplitSlice() || (rule.is_Value() && rule.as_Value().is_Bytes()), "Can only match slice with Bytes or Slice rules - " << rule);
                if (rule.is_Value()) {
                    ASSERT_BUG(sp, te.inner == HIRCoreType::U8, "Bytes pattern on non-&[u8]");
                    auto clonedVal = MIRConstant(rule.as_Value().as_Bytes());
                    auto sizeVal = MIRConstant::make_Uint({U128(rule.as_Value().as_Bytes().size()), HIRCoreType::Usize});

                    auto succBb = builder.newBbUnlinked();

                    ASSERT_BUG(sp, val.is_Deref(), "Slice pattern on non-Deref - " << val);
                    auto innerVal = val.cloneUnwrapped();

                    auto sliceRval = MIRRValue::make_MakeDst({mv$(clonedVal), mv$(sizeVal)});
                    auto testLval = builder.lvalueOrTemp(sp, builder.resolve().crate.types.borrow(HIRBorrowType::Shared, ty), mv$(sliceRval));
                    auto cmpLval = builder.lvalueOrTemp(sp, builder.resolve().crate.types.primitive(HIRCoreType::Bool), MIRRValue::make_BinOp({mv$(innerVal), MIRBinOp::EQ, mv$(testLval)}));
                    builder.endBlock(MIRTerminator::make_If({mv$(cmpLval), succBb, failBb}));
                    builder.setCurBlock(succBb);
                } else if (rule.is_Slice()) {
                    const auto& re = rule.as_Slice();

                    // Compare length
                    auto testVal = MIRParam(MIRConstant::make_Uint({U128(re.len), HIRCoreType::Usize}));
                    auto lenVal = builder.lvalueOrTemp(sp, builder.resolve().crate.types.primitive(HIRCoreType::Usize), MIRRValue::make_DstMeta({builder.getPtrToDst(sp, val)}));
                    auto cmpLval = builder.lvalueOrTemp(sp, builder.resolve().crate.types.primitive(HIRCoreType::Bool), MIRRValue::make_BinOp({mv$(lenVal), MIRBinOp::EQ, mv$(testVal)}));

                    auto lenSuccBb = builder.newBbUnlinked();
                    builder.endBlock(MIRTerminator::make_If({mv$(cmpLval), lenSuccBb, failBb}));
                    builder.setCurBlock(lenSuccBb);

                    // Recurse checking values
                    MIRLowerHIRMatchSimpleGeneratePattern(builder, sp, ruleset, re.subRules.data(), re.subRules.size(), topTy, topVal, fieldPathOfs, failBb);
                } else if (rule.is_SplitSlice()) {
                    const auto& re = rule.as_SplitSlice();

                    // Compare length
                    auto testVal = MIRParam(MIRConstant::make_Uint({U128(re.minLen), HIRCoreType::Usize}));
                    auto lenVal = builder.lvalueOrTemp(sp, builder.resolve().crate.types.primitive(HIRCoreType::Usize), MIRRValue::make_DstMeta({builder.getPtrToDst(sp, val)}));
                    auto cmpLval = builder.lvalueOrTemp(sp, builder.resolve().crate.types.primitive(HIRCoreType::Bool), MIRRValue::make_BinOp({mv$(lenVal), MIRBinOp::LT, mv$(testVal)}));

                    auto lenSuccBb = builder.newBbUnlinked();
                    builder.endBlock(MIRTerminator::make_If({mv$(cmpLval), failBb, lenSuccBb})); // if len < test : FAIL
                    builder.setCurBlock(lenSuccBb);

                    MIRLowerHIRMatchSimpleGeneratePattern(builder, sp, ruleset, re.leading.data(), re.leading.size(), topTy, topVal, fieldPathOfs, failBb);

                    MIRLowerHIRMatchSimpleGeneratePattern(builder, sp, ruleset, re.trailing.data(), re.trailing.size(), topTy, topVal, fieldPathOfs, failBb);
                } else {
                    BUG(sp, "Invalid rule type for slice - " << rule);
                }
                break;
            }
            case HIRTypeData::TAG_Tuple: {
                TODO(sp, "Match directly on tuple?");
                break;
            }
            case HIRTypeData::TAG_Borrow: {
                TODO(sp, "Match directly on borrow?");
                break;
            }
            case HIRTypeData::TAG_Pointer: {
                BUG(sp, "Attempting to match a pointer - " << rule << " against " << ty);
                break;
            }
            case HIRTypeData::TAG_NamedFunction: {
                BUG(sp, "Attempting to match a function pointer - " << rule << " against " << ty);
                break;
            }
            case HIRTypeData::TAG_Function: {
                BUG(sp, "Attempting to match a function pointer - " << rule << " against " << ty);
                break;
            }
            case HIRTypeData::TAG_NodeType: {
                BUG(sp, "Attempting to match a magic type - " << rule << " against " << ty);
                break;
            }
        }
    }
    return 0;
}

// --
// Match v2 Algo - Grouped rules
// --

class tRulesSubset {
    ::std::vector<const ::std::vector<PatternRule>*> ruleSets;
    bool isArmIndexes;
    ::std::vector<size_t> armIdxes;

    static ::std::pair<size_t, size_t> decodeArmIdx(size_t v) {
        return ::std::make_pair(v & 0x3FFF, v >> 14);
    }

    static size_t encodeArmIdx(size_t armIdx, size_t patIdx) {
        assert(armIdx <= 0x3FFF);
        assert(patIdx <= 0x3FFF);
        return armIdx | (patIdx << 14);
    }

public:
    tRulesSubset(size_t exp, bool isArmIndexes)
        : isArmIndexes(isArmIndexes)
    {
        ruleSets.reserve(exp);
        armIdxes.reserve(exp);
    }

    size_t size() const {
        return ruleSets.size();
    }

    const ::std::vector<PatternRule>& operator[](size_t n) const {
        return *ruleSets[n];
    }

    bool isArm() const {
        return isArmIndexes;
    }

    struct ArmIdxes {
        size_t arm;
        size_t armRule;
    };

    ArmIdxes armIdx(size_t n) const {
        assert(isArmIndexes);
        auto v = decodeArmIdx(armIdxes.at(n));
        return ArmIdxes{v.first, v.second};
    }

    MIRBasicBlockId bbIdx(size_t n) const {
        assert(!isArmIndexes);
        return armIdxes.at(n);
    }

    void subSort(size_t ofs, size_t start, size_t n) {
        ::std::vector<size_t> v;
        for (size_t i = 0; i < n; i++) {
            v.push_back(start + i);
        }
        // Sort rules based on just the value (ignore inner rules)
        ::std::stable_sort(v.begin(), v.end(), [&](auto a, auto b) {
            return ordRuleCompatible((*ruleSets[a])[ofs], (*ruleSets[b])[ofs]) == OrdLess;
        });

        // Reorder contents to above sorting
        {
            decltype(this->ruleSets) tmp;
            for (auto i : v) {
                tmp.push_back(ruleSets[i]);
            }
            ::std::copy(tmp.begin(), tmp.end(), ruleSets.begin() + start);
        }
        {
            decltype(this->armIdxes) tmp;
            for (auto i : v) {
                tmp.push_back(armIdxes[i]);
            }
            ::std::copy(tmp.begin(), tmp.end(), armIdxes.begin() + start);
        }
    }

    tRulesSubset subSlice(size_t ofs, size_t n) {
        tRulesSubset rv{n, this->isArmIndexes};
        rv.ruleSets.reserve(n);
        for (size_t i = 0; i < n; i++) {
            rv.ruleSets.push_back(this->ruleSets[ofs + i]);
            rv.armIdxes.push_back(this->armIdxes[ofs + i]);
        }
        return rv;
    }

    void pushArm(const ::std::vector<PatternRule>& x, size_t armIdx, size_t patIdx) {
        assert(isArmIndexes);
        ruleSets.push_back(&x);
        armIdxes.push_back(encodeArmIdx(armIdx, patIdx));
    }

    void pushBb(const ::std::vector<PatternRule>& x, MIRBasicBlockId bb) {
        assert(!isArmIndexes);
        ruleSets.push_back(&x);
        armIdxes.push_back(bb);
    }

    friend ::std::ostream& operator<<(::std::ostream& os, const tRulesSubset& x) {
        os << "t_rules_subset{";
        for (size_t i = 0; i < x.ruleSets.size(); i++) {
            if (i != 0) {
                os << ", ";
            }
            os << "[";
            if (x.isArmIndexes) {
                auto v = decodeArmIdx(x.armIdxes[i]);
                os << v.first << "," << v.second;
            } else {
                os << "bb" << x.armIdxes[i];
            }
            os << "]";
            os << ": [" << *x.ruleSets[i] << "]";
        }
        os << "}";
        return os;
    }
};

class MatchGenGrouped {
    const Span& sp;
    MirBuilder& builder;
    const HIRTypeData* topTy;
    const MIRLValue& topVal;
    const ::std::vector<ArmCode>& armsCode;

    size_t fieldPathOfs;

public:
    MatchGenGrouped(MirBuilder& builder, const Span& sp, const HIRTypeData* topTy, const MIRLValue& topVal, const ::std::vector<ArmCode>& armsCode, size_t fieldPathOfs)
        : sp(sp)
        , builder(builder)
        , topTy(topTy)
        , topVal(topVal)
        , armsCode(armsCode)
        , fieldPathOfs(fieldPathOfs)
    {
    }

    void genForSlice(tRulesSubset rules, size_t ofs, MIRBasicBlockId defaultArm);
    void genDispatch(const ::std::vector<tRulesSubset>& rules, size_t ofs, const ::std::vector<MIRBasicBlockId>& armTargets, MIRBasicBlockId defBlk);
    void genDispatchPrimitive(HIRTypeRef ty, MIRLValue val, const ::std::vector<tRulesSubset>& rules, size_t ofs, const ::std::vector<MIRBasicBlockId>& armTargets, MIRBasicBlockId defBlk);
    void genDispatchEnum(HIRTypeRef ty, MIRLValue val, const ::std::vector<tRulesSubset>& rules, size_t ofs, const ::std::vector<MIRBasicBlockId>& armTargets, MIRBasicBlockId defBlk);
    void genDispatchSlice(HIRTypeRef ty, MIRLValue val, const ::std::vector<tRulesSubset>& rules, size_t ofs, const ::std::vector<MIRBasicBlockId>& armTargets, MIRBasicBlockId defBlk);

    void genDispatchRange(const fieldPathT& fieldPath, const MIRConstant& first, const MIRConstant& last, bool isInclusive, MIRBasicBlockId defBlk);
    void genDispatchSplitslice(const fieldPathT& fieldPath, const PatternRule::Data_SplitSlice& e, MIRBasicBlockId defBlk);

    MIRLValue pushCompare(MIRLValue left, MIRBinOp op, MIRParam right) {
        return builder.lvalueOrTemp(sp, builder.resolve().crate.types.primitive(HIRCoreType::Bool), MIRRValue::make_BinOp({mv$(left), op, mv$(right)}));
    }
};

namespace {
    void appendRuleColumns(::std::vector<PatternRule>& outRules, PatternRule rule) {
        switch (rule.tag()) {
            case PatternRule::TAG_Variant: {
                auto& e = rule.as_Variant();
                auto subRules = mv$(e.subRules);
                outRules.push_back(mv$(rule));
                for (auto& sr : subRules) {
                    appendRuleColumns(outRules, mv$(sr));
                }
                break;
            }
            case PatternRule::TAG_Slice: {
                auto& e = rule.as_Slice();
                auto subRules = mv$(e.subRules);
                outRules.push_back(mv$(rule));
                for (auto& sr : subRules) {
                    appendRuleColumns(outRules, mv$(sr));
                }
                break;
            }
            case PatternRule::TAG_SplitSlice: {
                auto& e = rule.as_SplitSlice();
                auto leading = mv$(e.leading);
                auto trailing = mv$(e.trailing);
                auto idx = outRules.size();
                outRules.push_back(mv$(rule));
                for (auto& sr : leading) {
                    appendRuleColumns(outRules, mv$(sr));
                }
                // Trailing rules are complex as they break the assumption that patterns across the same type share a prefix
                // - So, flatten them into the "flattened" rule
                for (auto& sr : trailing) {
                    appendRuleColumns(outRules[idx].as_SplitSlice().trailing, mv$(sr));
                }
                break;
            }
            case PatternRule::TAG_Bool: {
                outRules.push_back(mv$(rule));
                break;
            }
            case PatternRule::TAG_Value: {
                outRules.push_back(mv$(rule));
                break;
            }
            case PatternRule::TAG_ValueRange: {
                outRules.push_back(mv$(rule));
                break;
            }
            case PatternRule::TAG_Any: {
                outRules.push_back(mv$(rule));
                break;
            }
        }
    }

    tArmRules linearizeRuleColumns(tArmRules rules) {
        tArmRules rv;
        rv.reserve(rules.size());
        for (auto& ruleset : rules) {
            ::std::vector<PatternRule> patternRules;
            for (auto& r : ruleset.rules) {
                appendRuleColumns(patternRules, mv$(r));
            }
            rv.push_back(PatternRuleset{ruleset.armIdx, ruleset.armRuleIdx, mv$(patternRules)});
        }
        return rv;
    }
}

void MIRLowerHIRMatchGrouped(MirBuilder& builder, MirConverter& conv, const Span& sp, const HIRTypeData* matchTy, MIRLValue matchVal, tArmRules armRules, ::std::vector<ArmCode> armsCode, MIRBasicBlockId firstCmpBlock) {
    TRACE_FUNCTION_F("");

    // The grouped matcher consumes one constructor or field test per matrix
    // column. Keep each outer constructor before the payload columns and retain
    // the full field path on every test.
    armRules = linearizeRuleColumns(mv$(armRules));

    // - Create a "slice" of the passed rules, suitable for passing to the recursive part of the algo
    tRulesSubset rules{armRules.size(), /*is_arm_indexes=*/true};
    for (const auto& r : armRules) {
        rules.pushArm(r.rules, r.armIdx, r.armRuleIdx);
    }

    auto inst = MatchGenGrouped{builder, sp, matchTy, matchVal, armsCode, 0};

    // NOTE: This block should never be used
    auto defaultArm = builder.newBbUnlinked();

    builder.setCurBlock(firstCmpBlock);
    inst.genForSlice(mv$(rules), 0, defaultArm);

    // Make the default infinite loop.
    // - Preferably, it'd abort.
    builder.setCurBlock(defaultArm);
    builder.endBlock(MIRTerminator::make_Unreachable({}));
}

void MatchGenGrouped::genForSlice(tRulesSubset armRules, size_t ofs, MIRBasicBlockId defaultArm) {
    TRACE_FUNCTION_F("arm_rules=" << armRules << ", ofs=" << ofs << ", default_arm=" << defaultArm);
    ASSERT_BUG(sp, armRules.size() > 0, "");

    // Leading wildcard-only columns cannot discriminate between these rows.
    for (;;) {
        bool isAllAny = true;
        for (size_t i = 0; i < armRules.size() && isAllAny; i++) {
            if (armRules[i].size() <= ofs) {
                isAllAny = false;
            } else if (!armRules[i][ofs].is_Any()) {
                isAllAny = false;
            }
        }
        if (!isAllAny) {
            break;
        }
        ofs++;
        DEBUG("Skip to ofs=" << ofs);
    }

    // Split current set of rules into groups based on _ patterns
    for (size_t idx = 0; idx < armRules.size();) {
        // Completed arms
        while (idx < armRules.size() && armRules[idx].size() <= ofs) {
            ASSERT_BUG(sp, armRules[idx].size() == ofs, "Offset too large for rule - ofs=" << ofs << ", rules=" << armRules[idx]);
            DEBUG(idx << ": Complete");
            // Emit jump to either arm code, or arm condition
            if (armRules.isArm()) {
                auto ai = armRules.armIdx(idx);
                ASSERT_BUG(sp, armsCode.size() > 0, "Bottom-level ruleset with no arm code information");
                const auto& ac = armsCode[ai.arm];
                ASSERT_BUG(sp, ai.armRule < ac.rules.size(), "Arm rule index (" << ai.armRule << ") out of bounds (" << ac.rules.size() << ")");

                builder.endBlock(MIRTerminator::make_Goto(ac.rules.at(ai.armRule).entry));

                if (ac.hasCondition) {
                    TODO(sp, "Handle conditionals in Grouped");
                    // TODO: If the condition fails, this should re-try the match on other rules that could have worked.
                    // - For now, conditionals are disabled.

                    // TODO: What if there's multiple patterns on this condition?
                    // - For now, only the first pattern gets edited.
                    // - Maybe clone the blocks used for the condition?

                } else {
                    // A completed row is irrefutable.  As the matrix retains
                    // source priority, every following row is unreachable.
                    return;
                }
            } else {
                auto bb = armRules.bbIdx(idx);
                builder.endBlock(MIRTerminator::make_Goto(bb));
                return;
            }
            idx++;
        }

        // - Value arms
        auto start = idx;
        bool stoppedAtOverlap = false;
        for (; idx < armRules.size(); idx++) {
            if (armRules[idx].size() <= ofs) {
                break;
            }
            if (armRules[idx][ofs].is_Any()) {
                break;
            }
            if (armRules[idx][ofs].is_SplitSlice()) {
                break;
            }
            // TODO: It would be nice if ValueRange could be combined with Value (if there's no overlap)
            if (armRules[idx][ofs].is_ValueRange()) {
                break;
            }

            // The dispatch below sorts selector groups.  Keep an ordering
            // boundary before a selector that overlaps an incompatible
            // earlier selector, otherwise e.g. a byte literal can move past
            // an equal-length slice pattern and change the selected arm.
            for (size_t prev = start; prev < idx; prev++) {
                if (!ruleCompatible(armRules[prev][ofs], armRules[idx][ofs]) && rulesOverlap(armRules[prev][ofs], armRules[idx][ofs])) {
                    stoppedAtOverlap = true;
                    break;
                }
            }
            if (stoppedAtOverlap) {
                break;
            }
        }
        auto firstAny = idx;

        // Generate dispatch based on the above list
        // - If there's value ranges they need special handling
        // - Can sort arms within this group (ordering doesn't matter, as long as ranges are handled)
        // - Sort must be stable.

        if (start < firstAny) {
            DEBUG(start << "+" << (firstAny - start) << ": Values");
            bool hasDefault = (firstAny < armRules.size());
            auto next = (hasDefault ? builder.newBbUnlinked() : defaultArm);

            // Sort rules before getting compatible runs
            // TODO: Is this a valid operation?
            armRules.subSort(ofs, start, firstAny - start);

            // Create list of compatible arm slices (runs with the same selector value)
            ::std::vector<tRulesSubset> slices;
            auto curTest = start;
            for (auto i = start; i < firstAny; i++) {
                // Just check if the decision value differs (don't check nested rules)
                if (!ruleCompatible(armRules[i][ofs], armRules[curTest][ofs])) {
                    slices.push_back(armRules.subSlice(curTest, i - curTest));
                    curTest = i;
                }
            }
            slices.push_back(armRules.subSlice(curTest, firstAny - curTest));
            DEBUG("- " << slices.size() << " groupings");
            ::std::vector<MIRBasicBlockId> armBlocks;
            armBlocks.reserve(slices.size());

            auto curBlk = builder.pauseCurBlock();
            // > Stable sort list
            ::std::sort(slices.begin(), slices.end(), [&](const auto& a, const auto& b) {
                return a[0][ofs] < b[0][ofs];
            });
            // TODO: Should this do a stable sort of inner patterns too?
            // - A sort of inner patterns such that `_` (and range?) patterns don't change position.

            // > Get type of match, generate dispatch list.
            for (size_t i = 0; i < slices.size(); i++) {
                auto curBlock = builder.newBbUnlinked();
                builder.setCurBlock(curBlock);

                for (size_t j = 0; j < slices[i].size(); j++) {
                    if (j > 0) {
                        ASSERT_BUG(sp, slices[i][0][ofs] == slices[i][j][ofs], "Mismatched rules - " << slices[i][0][ofs] << " and " << slices[i][j][ofs]);
                    }
                    armBlocks.push_back(curBlock);
                }

                this->genForSlice(slices[i], ofs + 1, next);
            }

            builder.setCurBlock(curBlk);

            // Generate decision code
            this->genDispatch(slices, ofs, armBlocks, next);

            if (hasDefault) {
                builder.setCurBlock(next);
            }
        }

        if (stoppedAtOverlap) {
            continue;
        }

        // Collate matching blocks at `first_any`
        assert(firstAny == idx);
        if (firstAny < armRules.size() && armRules[idx].size() > ofs) {
            // Collate all equal rules
            while (idx < armRules.size() && armRules[idx][ofs] == armRules[firstAny][ofs]) {
                idx++;
            }
            DEBUG(firstAny << "-" << idx << ": Multi-match");

            bool hasNext = idx < armRules.size();
            auto next = (hasNext ? builder.newBbUnlinked() : defaultArm);

            const auto& rule = armRules[firstAny][ofs];
            if (const auto* e = rule.opt_ValueRange()) {
                // Generate branch based on range
                this->genDispatchRange(armRules[firstAny][ofs].fieldPath, e->first, e->last, e->isInclusive, next);
            } else if (const auto* e = rule.opt_SplitSlice()) {
                // Generate branch based on slice length being at least required.
                this->genDispatchSplitslice(rule.fieldPath, *e, next);
            } else {
                ASSERT_BUG(sp, rule.is_Any(), "Didn't expect non-Any rule here, got " << rule.tagStr() << " " << rule);
            }

            // Step deeper into these arms
            auto slice = armRules.subSlice(firstAny, idx - firstAny);
            this->genForSlice(mv$(slice), ofs + 1, next);

            if (hasNext) {
                builder.setCurBlock(next);
            }
        }
    }

    ASSERT_BUG(sp, !builder.blockActive(), "Block left active after match group");
}

/// <summary>
/// Generate dispatch code for the provided pattern list
/// </summary>
/// <param name="rules">A list of equivalent pattern rules (at the given offset)</param>
/// <param name="ofs">Offset into sub-patterns</param>
/// <param name="arm_targets">Target blocks for each arm in `rules`</param>
/// <param name="def_blk">Default block for if no arm matched</param>
void MatchGenGrouped::genDispatch(const ::std::vector<tRulesSubset>& rules, size_t ofs, const ::std::vector<MIRBasicBlockId>& armTargets, MIRBasicBlockId defBlk) {
    const auto& fieldPath = rules[0][0][ofs].fieldPath;
    TRACE_FUNCTION_F("rules=[" << rules << "], ofs=" << ofs << ", field_path=" << fieldPath);

    // Assert that all patterns combined here are over the same field
    {
        size_t n = 0;
        for (size_t i = 0; i < rules.size(); i++) {
            for (size_t j = 0; j < rules[i].size(); j++) {
                ASSERT_BUG(sp, rules[i][j][ofs].fieldPath == fieldPath, "Field path mismatch, " << rules[i][j][ofs].fieldPath << " != " << fieldPath);
                n++;
            }
        }
        ASSERT_BUG(sp, armTargets.size() == n, "Arm target count mismatch - " << n << " != " << armTargets.size());
    }

    MIRLValue val;
    HIRTypeRef ty;
    getTyAndVal(sp, builder, topTy, topVal, fieldPath, fieldPathOfs, ty, val);
    DEBUG("ty = " << ty << ", val = " << val);

    switch ((*ty).tag()) {
        case HIRTypeData::TAG_Infer: {
            BUG(sp, "Hit _ in type - " << ty);
            break;
        }
        case HIRTypeData::TAG_Diverge: {
            BUG(sp, "Matching over !");
            break;
        }
        case HIRTypeData::TAG_Primitive: {
            this->genDispatchPrimitive(mv$(ty), mv$(val), rules, ofs, armTargets, defBlk);
            break;
        }
        case HIRTypeData::TAG_Pattern: {
            BUG(sp, "Pattern type was not reduced to its base type");
            break;
        }
        case HIRTypeData::TAG_Path: {
            auto& te = (*ty).as_Path();
            // Matching over a path can only happen with an enum.
                // TODO: What about `box` destructures?
                // - They're handled via hidden derefs
            switch (te.binding.tag()) {
                case HIRTypePathBinding::TAG_Unbound: {
                    BUG(sp, "Encounterd unbound path - " << te.path);
                    break;
                }
                case HIRTypePathBinding::TAG_Opaque: {
                    BUG(sp, "Attempting to match over opaque type - " << ty);
                    break;
                }
                case HIRTypePathBinding::TAG_Struct: {
                    auto& pbe = te.binding.as_Struct();
                    const auto& strData = pbe->data;
                    switch (strData.tag()) {
                        case HIRStructData::TAG_Unit: {
                            BUG(sp, "Attempting to match over unit type - " << ty);
                            break;
                        }
                        case HIRStructData::TAG_Tuple: {
                            TODO(sp, "Matching on tuple-like struct?");
                            break;
                        }
                        case HIRStructData::TAG_Named: {
                            TODO(sp, "Matching on struct? - " << ty);
                            break;
                        }
                    }
                    break;
                }
                case HIRTypePathBinding::TAG_Union: {
                    TODO(sp, "Match over Union");
                    break;
                }
                case HIRTypePathBinding::TAG_ExternType: {
                    TODO(sp, "Match over ExternType - " << ty);
                    break;
                }
                case HIRTypePathBinding::TAG_Enum: {
                    this->genDispatchEnum(mv$(ty), mv$(val), rules, ofs, armTargets, defBlk);
                    break;
                }
            }
            break;
        }
        case HIRTypeData::TAG_Generic: {
            BUG(sp, "Attempting to match a generic");
            break;
        }
        case HIRTypeData::TAG_TraitObject: {
            BUG(sp, "Attempting to match a trait object");
            break;
        }
        case HIRTypeData::TAG_ErasedType: {
            BUG(sp, "Attempting to match an erased type");
            break;
        }
        case HIRTypeData::TAG_Array: {
            // Byte strings?
            // Remove the deref on the &str
            ASSERT_BUG(sp, !val.wrappers.empty() && val.wrappers.back().is_Deref(), "&[T; N] match on non-Deref lvalue - " << val);
            val.wrappers.pop_back();

            ::std::vector<MIRBasicBlockId> targets;
            ::std::vector<::std::vector<u8>> values;
            size_t tgtOfs = 0;
            for (size_t i = 0; i < rules.size(); i++) {
                for (size_t j = 1; j < rules[i].size(); j++) {
                    ASSERT_BUG(sp, armTargets[tgtOfs] == armTargets[tgtOfs + j], "Mismatched target blocks for Value match");
                }

                const auto& r = rules[i][0][ofs];
                ASSERT_BUG(sp, r.is_Value(), "Matching without _Value pattern - " << r.tagStr());
                const auto& re = r.as_Value();
                if (re.is_Const()) {
                    TODO(sp, "Handle Constant::Const in match");
                }

                targets.push_back(armTargets[tgtOfs]);
                values.push_back(re.as_Bytes());

                tgtOfs += rules[i].size();
            }
            builder.endBlock(MIRTerminator::make_SwitchValue({mv$(val), defBlk, mv$(targets), MIRSwitchValues(mv$(values))}));
            break;
        }
        case HIRTypeData::TAG_Slice: {
            this->genDispatchSlice(mv$(ty), mv$(val), rules, ofs, armTargets, defBlk);
            break;
        }
        case HIRTypeData::TAG_Tuple: {
            BUG(sp, "Match directly on tuple");
            break;
        }
        case HIRTypeData::TAG_Borrow: {
            BUG(sp, "Match directly on borrow");
            break;
        }
        case HIRTypeData::TAG_Pointer: {
            auto valUsize = builder.newTemporary(builder.resolve().crate.types.primitive(HIRCoreType::Usize));
            builder.pushStmtAssign(sp, valUsize.clone(), MIRRValue::make_Cast({mv$(val), builder.resolve().crate.types.primitive(HIRCoreType::Usize)}));
            this->genDispatchPrimitive(builder.resolve().crate.types.primitive(HIRCoreType::Usize), mv$(valUsize), rules, ofs, armTargets, defBlk);
            break;
        }
        case HIRTypeData::TAG_NamedFunction: {
            BUG(sp, "Attempting to match a function pointer - " << ty);
            break;
        }
        case HIRTypeData::TAG_Function: {
            // TODO: Could this actually be valid?
            BUG(sp, "Attempting to match a function pointer - " << ty);
            break;
        }
        case HIRTypeData::TAG_NodeType: {
            BUG(sp, "Attempting to match a magic type - " << ty);
            break;
        }
    }
}

namespace {
    void pushIfEqual(const Span& sp, MirBuilder& builder, MIRLValue val, MIRParam testVal, MIRBasicBlockId bbTrue, MIRBasicBlockId bbFalse) {
        auto cmpLval = builder.getRvalInIfCond(sp, MIRRValue::make_BinOp({mv$(val), MIRBinOp::EQ, mv$(testVal)}));
        builder.endBlock(MIRTerminator::make_If({mv$(cmpLval), bbTrue, bbFalse}));
    }
}

void MatchGenGrouped::genDispatchPrimitive(HIRTypeRef ty, MIRLValue val, const ::std::vector<tRulesSubset>& rules, size_t ofs, const ::std::vector<MIRBasicBlockId>& armTargets, MIRBasicBlockId defBlk) {
    auto te = ty->as_Primitive();
    switch (te) {
        case HIRCoreType::Bool: {
            ASSERT_BUG(sp, rules.size() <= 2, "More than 2 rules for boolean");
            for (size_t i = 0; i < rules.size(); i++) {
                ASSERT_BUG(sp, rules[i][0][ofs].is_Bool(), "PatternRule for bool isn't _Bool");
            }

            // False sorts before true.
            auto failBb = rules.size() == 2 ? armTargets[0] : (rules[0][0][ofs].as_Bool() ? defBlk : armTargets[0]);
            auto succBb = rules.size() == 2 ? armTargets[rules[0].size()] : (rules[0][0][ofs].as_Bool() ? armTargets[0] : defBlk);

            builder.endBlock(MIRTerminator::make_If({mv$(val), succBb, failBb}));
        } break;
        case HIRCoreType::U8:
        case HIRCoreType::U16:
        case HIRCoreType::U32:
        case HIRCoreType::U64:
        case HIRCoreType::U128:
        case HIRCoreType::Usize:

        case HIRCoreType::Char:
            if (rules.size() == 1) {
                // Special case, single option, equality only
                const auto& r = rules[0][0][ofs];
                ASSERT_BUG(sp, r.is_Value(), "Matching without _Value pattern - " << r.tagStr());
                const auto& re = r.as_Value();
                pushIfEqual(sp, builder, mv$(val), MIRParam(re.clone()), armTargets[0], defBlk);
            } else {
                // NOTE: Rules are currently sorted
                // TODO: If there are Constant::Const values in the list, they need to come first! (with equality checks)

                ::std::vector<::std::pair<MIRConstant, MIRBasicBlockId>> largeValues;
                ::std::vector<u64> values;
                ::std::vector<MIRBasicBlockId> targets;
                size_t tgtOfs = 0;
                for (size_t i = 0; i < rules.size(); i++) {
                    for (size_t j = 1; j < rules[i].size(); j++) {
                        ASSERT_BUG(sp, armTargets[tgtOfs] == armTargets[tgtOfs + j], "Mismatched target blocks for Value match");
                    }

                    const auto& r = rules[i][0][ofs];
                    ASSERT_BUG(sp, r.is_Value(), "Matching without _Value pattern - " << r.tagStr());
                    const auto& re = r.as_Value();
                    if (re.is_Const()) {
                        // Inject a `If` chained to a new block
                        auto nextBlock = builder.newBbUnlinked();
                        pushIfEqual(sp, builder, val.clone(), MIRParam(re.clone()), armTargets[tgtOfs], nextBlock);
                        builder.setCurBlock(nextBlock);
                    } else if (re.as_Uint().v > U128(UINT64_MAX)) {
                        largeValues.push_back(std::make_pair(re.clone(), armTargets[tgtOfs]));
                    } else {
                        values.push_back(re.as_Uint().v.truncateU64());
                        targets.push_back(armTargets[tgtOfs]);
                    }

                    tgtOfs += rules[i].size();
                }
                // If there were any values that don't fit in u64, then emit those as a chain of `if` terminators
                if (!largeValues.empty()) {
                    auto tailBlock = builder.newBbUnlinked();
                    builder.endBlock(MIRTerminator::make_SwitchValue({val.clone(), tailBlock, mv$(targets), MIRSwitchValues(mv$(values))}));
                    builder.setCurBlock(tailBlock);
                    for (auto& v : largeValues) {
                        auto nextBlock = builder.newBbUnlinked();
                        pushIfEqual(sp, builder, val.clone(), mv$(v.first), v.second, nextBlock);
                        builder.setCurBlock(nextBlock);
                    }
                    builder.endBlock(MIRTerminator::make_Goto(defBlk));
                } else {
                    builder.endBlock(MIRTerminator::make_SwitchValue({mv$(val), defBlk, mv$(targets), MIRSwitchValues(mv$(values))}));
                }
            }
            break;

        case HIRCoreType::I8:
        case HIRCoreType::I16:
        case HIRCoreType::I32:
        case HIRCoreType::I64:
        case HIRCoreType::I128:
        case HIRCoreType::Isize:
            if (rules.size() == 1) {
                // Special case, single option, equality only
                const auto& r = rules[0][0][ofs];
                ASSERT_BUG(sp, r.is_Value(), "Matching without _Value pattern - " << r.tagStr());
                const auto& re = r.as_Value();
                pushIfEqual(sp, builder, mv$(val), MIRParam(re.clone()), armTargets[0], defBlk);
            } else {
                // NOTE: Rules are currently sorted
                // TODO: If there are Constant::Const values in the list, they need to come first! (with equality checks)

                ::std::vector<i64> values;
                ::std::vector<MIRBasicBlockId> targets;
                size_t tgtOfs = 0;
                for (size_t i = 0; i < rules.size(); i++) {
                    for (size_t j = 1; j < rules[i].size(); j++) {
                        ASSERT_BUG(sp, armTargets[tgtOfs] == armTargets[tgtOfs + j], "Mismatched target blocks for Value match");
                    }

                    const auto& r = rules[i][0][ofs];
                    ASSERT_BUG(sp, r.is_Value(), "Matching without _Value pattern - " << r.tagStr());
                    const auto& re = r.as_Value();
                    if (re.is_Const()) {
                        TODO(sp, "Handle Constant::Const in match");
                    }

                    if (re.as_Int().v > S128(INT64_MAX) || re.as_Int().v < S128(INT64_MIN)) {
                        TODO(sp, "Handle 128-bit values in SwitchValue");
                    }
                    values.push_back(re.as_Int().v.truncateI64());
                    targets.push_back(armTargets[tgtOfs]);

                    tgtOfs += rules[i].size();
                }
                builder.endBlock(MIRTerminator::make_SwitchValue({mv$(val), defBlk, mv$(targets), MIRSwitchValues(mv$(values))}));
            }
            break;

        case HIRCoreType::F16:
        case HIRCoreType::F32:
        case HIRCoreType::F64:
        case HIRCoreType::F128: {
            // NOTE: Rules are currently sorted
            // TODO: If there are Constant::Const values in the list, they need to come first!
            size_t tgtOfs = 0;
            for (size_t i = 0; i < rules.size(); i++) {
                for (size_t j = 1; j < rules[i].size(); j++) {
                    ASSERT_BUG(sp, armTargets[tgtOfs] == armTargets[tgtOfs + j], "Mismatched target blocks for Value match");
                }

                const auto& r = rules[i][0][ofs];
                ASSERT_BUG(sp, r.is_Value(), "Matching without _Value pattern - " << r.tagStr());
                const auto& re = r.as_Value();
                if (re.is_Const()) {
                    TODO(sp, "Handle Constant::Const in match");
                }

                // IF v < tst : def_blk
                {
                    auto cmpEqBlk = builder.newBbUnlinked();
                    auto cmpLvalLt = builder.lvalueOrTemp(sp, builder.resolve().crate.types.primitive(HIRCoreType::Bool), MIRRValue::make_BinOp({val.clone(), MIRBinOp::LT, MIRParam(re.clone())}));
                    builder.endBlock(MIRTerminator::make_If({mv$(cmpLvalLt), defBlk, cmpEqBlk}));
                    builder.setCurBlock(cmpEqBlk);
                }

                // IF v == tst : target
                {
                    auto nextCmpBlk = builder.newBbUnlinked();
                    auto cmpLvalEq = builder.lvalueOrTemp(sp, builder.resolve().crate.types.primitive(HIRCoreType::Bool), MIRRValue::make_BinOp({val.clone(), MIRBinOp::EQ, MIRParam(re.clone())}));
                    builder.endBlock(MIRTerminator::make_If({mv$(cmpLvalEq), armTargets[tgtOfs], nextCmpBlk}));
                    builder.setCurBlock(nextCmpBlk);
                }

                tgtOfs += rules[i].size();
            }
            builder.endBlock(MIRTerminator::make_Goto(defBlk));
        } break;
        case HIRCoreType::Str: {
            // Remove the deref on the &str
            ASSERT_BUG(sp, !val.wrappers.empty() && val.wrappers.back().is_Deref(), "&str match on non-Deref lvalue - " << val);
            val.wrappers.pop_back();

            ::std::vector<MIRBasicBlockId> targets;
            ::std::vector<::std::string> values;
            size_t tgtOfs = 0;
            for (size_t i = 0; i < rules.size(); i++) {
                for (size_t j = 1; j < rules[i].size(); j++) {
                    ASSERT_BUG(sp, armTargets[tgtOfs] == armTargets[tgtOfs + j], "Mismatched target blocks for Value match");
                }

                const auto& r = rules[i][0][ofs];
                ASSERT_BUG(sp, r.is_Value(), "Matching without _Value pattern - " << r.tagStr());
                const auto& re = r.as_Value();
                if (re.is_Const()) {
                    TODO(sp, "Handle Constant::Const in match");
                }

                targets.push_back(armTargets[tgtOfs]);
                values.push_back(re.as_StaticString());

                tgtOfs += rules[i].size();
            }
            builder.endBlock(MIRTerminator::make_SwitchValue({mv$(val), defBlk, mv$(targets), MIRSwitchValues(mv$(values))}));
        } break;
    }
}

void MatchGenGrouped::genDispatchEnum(HIRTypeRef ty, MIRLValue val, const ::std::vector<tRulesSubset>& rules, size_t ofs, const ::std::vector<MIRBasicBlockId>& armTargets, MIRBasicBlockId defBlk) {
    TRACE_FUNCTION;
    auto& te = ty->as_Path();
    const auto& pbe = te.binding.as_Enum();

    auto decisonArm = builder.pauseCurBlock();

    auto varCount = pbe->numVariants();
    ::std::vector<MIRBasicBlockId> arms(varCount, defBlk);
    size_t armIdx = 0;
    for (size_t i = 0; i < rules.size(); i++) {
        ASSERT_BUG(sp, rules[i][0][ofs].is_Variant(), "Rule for enum isn't Any or Variant - " << rules[i][0][ofs].tagStr());
        const auto& re = rules[i][0][ofs].as_Variant();
        unsigned int varIdx = re.idx;
        DEBUG("Variant " << varIdx);

        ASSERT_BUG(sp, re.subRules.size() == 0, "Sub-rules in MatchGenGrouped");

        arms[varIdx] = armTargets[armIdx];
        for (size_t j = 0; j < rules[i].size(); j++) {
            assert(arms[varIdx] == armTargets[armIdx]);
            armIdx++;
        }
    }

    builder.setCurBlock(decisonArm);
    builder.endBlock(MIRTerminator::make_Switch({mv$(val), mv$(arms)}));
}

void MatchGenGrouped::genDispatchSlice(HIRTypeRef ty, MIRLValue val, const ::std::vector<tRulesSubset>& rules, size_t ofs, const ::std::vector<MIRBasicBlockId>& armTargets, MIRBasicBlockId defBlk) {
    auto valLen = builder.lvalueOrTemp(sp, builder.resolve().crate.types.primitive(HIRCoreType::Usize), MIRRValue::make_DstMeta({builder.getPtrToDst(sp, val)}));

    // TODO: Re-sort the rules list to interleve Constant::Bytes and Slice

    // Just needs to check the lengths, then dispatch.
    size_t tgtOfs = 0;
    for (size_t i = 0; i < rules.size(); i++) {
        const auto& r = rules[i][0][ofs];
        if (const auto* re = r.opt_Slice()) {
            ASSERT_BUG(sp, re->subRules.size() == 0, "Sub-rules in MatchGenGrouped");
            auto valTst = MIRConstant::make_Uint({U128(re->len), HIRCoreType::Usize});

            for (size_t j = 0; j < rules[i].size(); j++) {
                assert(armTargets[tgtOfs] == armTargets[tgtOfs + j]);
            }

            // IF v < tst : target
            if (re->len > 0) {
                auto cmpEqBlk = builder.newBbUnlinked();
                auto cmpLvalLt = this->pushCompare(valLen.clone(), MIRBinOp::LT, valTst.clone());
                builder.endBlock(MIRTerminator::make_If({mv$(cmpLvalLt), defBlk, cmpEqBlk}));
                builder.setCurBlock(cmpEqBlk);
            }

            // IF v == tst : target
            {
                auto nextCmpBlk = builder.newBbUnlinked();
                auto cmpLvalEq = this->pushCompare(valLen.clone(), MIRBinOp::EQ, mv$(valTst));
                builder.endBlock(MIRTerminator::make_If({mv$(cmpLvalEq), armTargets[tgtOfs], nextCmpBlk}));
                builder.setCurBlock(nextCmpBlk);
            }
        } else if (const auto* re = r.opt_Value()) {
            ASSERT_BUG(sp, re->is_Bytes(), "Slice with non-Bytes value - " << *re);
            const auto& b = re->as_Bytes();

            auto valTstLen = MIRConstant::make_Uint({U128(b.size()), HIRCoreType::Usize});

            // IF v == tst : target
            {
                auto nextCmpBlk = builder.newBbUnlinked();

                // TODO: What if `val` isn't a Deref?
                ASSERT_BUG(sp, !val.wrappers.empty() && val.wrappers.back().is_Deref(), "TODO: Handle non-Deref matches of byte strings - " << val);
                auto& types = builder.resolve().crate.types;
                auto cmpSliceVal = builder.lvalueOrTemp(sp, types.borrow(HIRBorrowType::Shared, types.slice(types.primitive(HIRCoreType::U8))), MIRRValue::make_MakeDst({MIRParam(re->clone()), valTstLen.clone()}));
                auto cmpLvalEq = this->pushCompare(val.cloneUnwrapped(), MIRBinOp::EQ, mv$(cmpSliceVal));
                builder.endBlock(MIRTerminator::make_If({mv$(cmpLvalEq), armTargets[tgtOfs], nextCmpBlk}));

                builder.setCurBlock(nextCmpBlk);
            }
        } else {
            BUG(sp, "Matching without _Slice pattern - " << r.tagStr() << " - " << r);
        }

        tgtOfs += rules[i].size();
    }
    builder.endBlock(MIRTerminator::make_Goto(defBlk));
}

void MatchGenGrouped::genDispatchRange(const fieldPathT& fieldPath, const MIRConstant& first, const MIRConstant& last, bool isInclusive, MIRBasicBlockId defBlk) {
    TRACE_FUNCTION_F("field_path=" << fieldPath << ", " << first << " .." << (isInclusive ? "=" : "") << " " << last);
    MIRLValue val;
    HIRTypeRef ty;
    getTyAndVal(sp, builder, topTy, topVal, fieldPath, fieldPathOfs, ty, val);
    DEBUG("ty = " << ty << ", val = " << val);

    if (const auto* tep = ty->opt_Primitive()) {
        auto te = *tep;

        bool lowerPossible = true;
        bool upperPossible = true;

        switch (te) {
            case HIRCoreType::Bool:
                BUG(sp, "Range match over Bool");
                break;
            case HIRCoreType::Str:
                BUG(sp, "Range match over Str - is this valid?");
                break;
            case HIRCoreType::U8:
            case HIRCoreType::U16:
            case HIRCoreType::U32:
            case HIRCoreType::U64:
            case HIRCoreType::U128:
            case HIRCoreType::Usize:
                lowerPossible = (first.as_Uint().v > 0);
                // TODO: Should this also check for the end being the max value of the type?
                // - Can just leave that to the optimiser
                upperPossible = isInclusive ? (last.as_Uint().v < U128::max()) : true;
                break;
            case HIRCoreType::I8:
            case HIRCoreType::I16:
            case HIRCoreType::I32:
            case HIRCoreType::I64:
            case HIRCoreType::I128:
            case HIRCoreType::Isize:
                lowerPossible = (first.as_Int().v > S128::min());
                upperPossible = isInclusive ? (last.as_Int().v < S128::max()) : true;
                break;
            case HIRCoreType::Char:
                lowerPossible = (first.as_Uint().v > 0);
                upperPossible = isInclusive ? (last.as_Uint().v <= 0x10FFFF) : (last.as_Uint().v < 0x10FFFF);
                break;
            case HIRCoreType::F16:
            case HIRCoreType::F32:
            case HIRCoreType::F64:
            case HIRCoreType::F128:
                // NOTE: No upper or lower limits
                lowerPossible = (first.as_Float().v > -std::numeric_limits<double>::infinity());
                upperPossible = (last.as_Float().v < std::numeric_limits<double>::infinity());
                break;
        }

        if (lowerPossible) {
            auto testBb2 = builder.newBbUnlinked();
            // IF `val` < `first` : fail_bb
            auto cmpLtLval = builder.getRvalInIfCond(sp, MIRRValue::make_BinOp({MIRParam(val.clone()), MIRBinOp::LT, MIRParam(first.clone())}));
            builder.endBlock(MIRTerminator::make_If({mv$(cmpLtLval), defBlk, testBb2}));

            builder.setCurBlock(testBb2);
        }

        if (upperPossible) {
            auto succBb = builder.newBbUnlinked();

            // IF `val` > `last` : fail_bb
            auto op = isInclusive ? MIRBinOp::GT : MIRBinOp::GE;
            auto cmpGtLval = builder.getRvalInIfCond(sp, MIRRValue::make_BinOp({MIRParam(val.clone()), op, MIRParam(last.clone())}));
            builder.endBlock(MIRTerminator::make_If({mv$(cmpGtLval), defBlk, succBb}));

            builder.setCurBlock(succBb);
        }
    } else {
        TODO(sp, "ValueRange on " << ty);
    }
}

void MatchGenGrouped::genDispatchSplitslice(const fieldPathT& fieldPath, const PatternRule::Data_SplitSlice& e, MIRBasicBlockId defBlk) {
    TRACE_FUNCTION_F("field_path=" << fieldPath << ", [" << e.leading << ", .., " << e.trailing << "]");
    MIRLValue val;
    HIRTypeRef ty;
    getTyAndVal(sp, builder, topTy, topVal, fieldPath, fieldPathOfs, ty, val);
    DEBUG("ty = " << ty << ", val = " << val);

    ASSERT_BUG(sp, e.leading.size() == 0, "Sub-rules in MatchGenGrouped");
    ASSERT_BUG(sp, ty->is_Slice(), "SplitSlice pattern on non-slice - " << ty);

    // Obtain slice length
    auto valLen = builder.lvalueOrTemp(sp, builder.resolve().crate.types.primitive(HIRCoreType::Usize), MIRRValue::make_DstMeta({builder.getPtrToDst(sp, val)}));

    // 1. Check that length is sufficient for the pattern to be used
    // `IF len < min_len : def_blk, next
    {
        auto next = builder.newBbUnlinked();
        auto cmpVal = this->pushCompare(valLen.clone(), MIRBinOp::LT, MIRConstant::make_Uint({U128(e.minLen), HIRCoreType::Usize}));
        builder.endBlock(MIRTerminator::make_If({mv$(cmpVal), defBlk, next}));
        builder.setCurBlock(next);
    }

    // 2. Recurse into leading patterns.
    // TODO: This is dead code (leading patterns should have been expanded, and there's an assert above for it)
    if (e.minLen > e.trailingLen) {
        auto next = builder.newBbUnlinked();
        auto innerSet = tRulesSubset{1, /*is_arm_indexes=*/false};
        innerSet.pushBb(e.leading, next);
        auto inst = MatchGenGrouped{builder, sp, ty, val, {}, fieldPath.size()};
        inst.genForSlice(innerSet, 0, defBlk);

        builder.setCurBlock(next);
    }

    // 3. Recurse into trailing patterns
    if (e.trailingLen != 0) {
        auto next = builder.newBbUnlinked();
        auto innerSet = tRulesSubset{1, /*is_arm_indexes=*/false};
        innerSet.pushBb(e.trailing, next);
        auto inst = MatchGenGrouped{builder, sp, ty, val, {}, fieldPath.size()};
        inst.genForSlice(innerSet, 0, defBlk);

        builder.setCurBlock(next);
    }
}

// --------------------------------------------------------------------
// MirBuilder
// --------------------------------------------------------------------
MirBuilder::MirBuilder(const Span& sp, const StaticTraitResolve& resolve, const HIRTypeData* retTy, const HIRFunction::argsT& args, MIRFunction& output)
    : rootSpan(sp)
    , resolve_(resolve)
    , retTy(retTy)
    , args_(args)
    , output(output)
    , langBox_(nullptr)
    , blockActive_(false)
    , resultValid(false)
    , fcnScope_(*this, 0)
{
    if (resolve.hirCrate().langItems.count("owned_box") > 0) {
        langBox_ = &resolve.hirCrate().langItems.at("owned_box");
    }

    setCurBlock(newBbUnlinked());
    scopes.push_back(ScopeDef{sp, ScopeType::make_Owning({false, {}, {}})});
    scopeStack.push_back(0);

    scopes.push_back(ScopeDef{sp, ScopeType::make_Owning({true, {}, {}})});
    scopeStack.push_back(1);

    argStates.reserve(args.size());
    for (size_t i = 0; i < args.size(); i++) {
        argStates.push_back(VarState::make_Valid({}));
    }
    slotStates.resize(output.locals.size());
    firstTempIdx = output.locals.size();
    DEBUG("First temporary will be " << firstTempIdx);

    ifCondLval = this->newTemporary(resolve_.hirCrate().types.primitive(HIRCoreType::Bool));

    // Determine which variables can be replaced by arguents
    for (size_t i = 0; i < args.size(); i++) {
        const auto& pat = args[i].first;
        if (pat.bindings.size() == 1 && pat.bindings[0].type == HIRPatternBinding::Type::Move) {
            DEBUG("Argument shortcut: " << pat.bindings[0] << " -> a" << i);
            varArgMappings[pat.bindings[0].slot] = i;
        }
    }

    variableAliases.resize(output.locals.size());
}

void MirBuilder::finalCleanup() {
    TRACE_FUNCTION_F("");
    const auto& sp = rootSpan;
    if (blockActive()) {
        if (retTy->is_Diverge()) {
            terminateScopeEarly(sp, fcnScope());
            // Validation fails if this is reachable.
            endBlock(MIRTerminator::make_Unreachable({}));
        } else {
            if (hasResult()) {
                pushStmtAssign(sp, MIRLValue::newReturn(), getResult(sp));
            }

            terminateScopeEarly(sp, fcnScope());

            endBlock(MIRTerminator::make_Return({}));
        }
    } else {
        terminateScope(sp, ScopeHandle(*this, 1), /*emit_cleanup=*/false);
        terminateScope(sp, mv$(fcnScope_), /*emit_cleanup=*/false);
    }

    // Rewrite drop flags
    // - Expand recursive lookups
    for (;;) {
        bool added = false;
        for (auto& a : dropFlagAliases) {
            auto& mappedFlags = a.second;
            // Iterate every "destination" flag
            for (size_t i = 0; i < mappedFlags.size(); i++) {
                auto it2 = dropFlagAliases.find(mappedFlags[i]);
                if (it2 != dropFlagAliases.end()) {
                    for (unsigned otherFlag : it2->second) {
                        // If this flag is not in the current list, add it and mark that something changed
                        if (std::find(mappedFlags.begin(), mappedFlags.end(), otherFlag) == mappedFlags.end()) {
                            mappedFlags.push_back(otherFlag);
                            added = true;
                        }
                    }
                }
            }
        }
        if (!added) {
            break;
        }
    }

    for (auto& b : output.blocks) {
        for (auto it = b.statements.begin(); it != b.statements.end(); ++it) {
            // NOTE: Only need to worry about SetDropFlag, as the other ways of setting a flag are not generated yet.
            if (auto* p = it->opt_SetDropFlag()) {
                // Take a copy, which will be mutated to create the copies
                auto v = *p;
                auto dfIt = dropFlagAliases.find(v.idx);
                if (dfIt != dropFlagAliases.end()) {
                    // For each entry in `df_it->second`, add a copy of this SetDropFlag _before_ `it` (so it doesn't get re-visited)
                    for (unsigned otherIdx : dfIt->second) {
                        v.idx = otherIdx;
                        // Ensure that `it` always points to the original
                        it = b.statements.insert(it, MIRStatement(v)) + 1;
                    }
                }
            }
        }
    }
}

const HIRTypeData* MirBuilder::isTypeOwnedBox(const HIRTypeData* ty) const {
    if (langBox_) {
        if (!ty->is_Path()) {
            return nullptr;
        }
        const auto& te = ty->as_Path();

        if (!te.path.data.is_Generic()) {
            return nullptr;
        }
        const auto& pe = te.path.data.as_Generic();

        if (pe.path != *langBox_) {
            return nullptr;
        }
        // TODO: Properly assert the size?
        return pe.params.types.at(0);
    } else {
        return nullptr;
    }
}

void MirBuilder::scheduleVariableDrop(unsigned int idx) {
    registerVariableState(idx);
    scheduleRegisteredVariableDrop(idx);
}

void MirBuilder::registerVariableState(unsigned int idx) {
    DEBUG("REGISTER STATE (var) _" << idx << ": " << output.locals.at(idx));
    for (auto scopeIdx : ::reverse(scopeStack)) {
        auto& scopeDef = scopes.at(scopeIdx);
        switch (scopeDef.data.tag()) {
            case ScopeType::TAG_Owning: {
                auto& e = scopeDef.data.as_Owning();
                if (!e.isTemporary) {
                    auto it = ::std::find(e.slots.begin(), e.slots.end(), idx);
                    assert(it == e.slots.end());
                    e.slots.push_back(idx);
                    return;
                }
                break;
            }
            case ScopeType::TAG_Split: {
                BUG(Span(), "Variable " << idx << " introduced within a Split");
                break;
            }
            default: {
                break;
            }
        }
    }
    BUG(Span(), "Variable " << idx << " introduced with no Variable scope");
}

void MirBuilder::scheduleRegisteredVariableDrop(unsigned int idx) {
    DEBUG("SCHEDULE DROP (var) _" << idx << ": " << output.locals.at(idx));
    for (auto scopeIdx : ::reverse(scopeStack)) {
        auto& scopeDef = scopes.at(scopeIdx);
        switch (scopeDef.data.tag()) {
            case ScopeType::TAG_Owning: {
                auto& e = scopeDef.data.as_Owning();
                if (!e.isTemporary) {
                    auto stateIt = ::std::find(e.slots.begin(), e.slots.end(), idx);
                    assert(stateIt != e.slots.end());
                    auto dropIt = ::std::find_if(e.dropSlots.begin(), e.dropSlots.end(), [&](const ScopeDropSlot& slot) {
                        return !slot.isArgument && slot.index == idx;
                    });
                    assert(dropIt == e.dropSlots.end());
                    e.dropSlots.push_back(ScopeDropSlot{false, idx});
                    return;
                }
                break;
            }
            case ScopeType::TAG_Split: {
                BUG(Span(), "Variable " << idx << " scheduled within a Split");
                break;
            }
            default: {
                break;
            }
        }
    }
    BUG(Span(), "Variable " << idx << " scheduled with no Variable scope");
}

void MirBuilder::scheduleArgumentDrop(unsigned int idx) {
    DEBUG("SCHEDULE DROP (arg) a" << idx << ": " << args_.at(idx).second);
    for (auto scopeIdx : ::reverse(scopeStack)) {
        auto& scopeDef = scopes.at(scopeIdx);
        switch (scopeDef.data.tag()) {
            case ScopeType::TAG_Owning: {
                auto& e = scopeDef.data.as_Owning();
                if (!e.isTemporary) {
                    auto it = ::std::find_if(e.dropSlots.begin(), e.dropSlots.end(), [&](const ScopeDropSlot& slot) {
                        return slot.isArgument && slot.index == idx;
                    });
                    assert(it == e.dropSlots.end());
                    e.dropSlots.push_back(ScopeDropSlot{true, idx});
                    return;
                }
                break;
            }
            case ScopeType::TAG_Split: {
                BUG(Span(), "Argument " << idx << " introduced within a Split");
                break;
            }
            default: {
                break;
            }
        }
    }
    BUG(Span(), "Argument " << idx << " introduced with no Variable scope");
}

void MirBuilder::moveTemporaryDropToVariableScope(const Span& sp, const MIRLValue& value, const ScopeHandle& source) {
    if (!value.root.is_Local() || !value.wrappers.empty()) {
        return;
    }
    const auto idx = value.root.as_Local();
    if (idx < firstTempIdx) {
        return;
    }

    ASSERT_BUG(sp, source.idx < scopes.size(), "Invalid temporary scope " << source);
    auto& sourceScope = scopes.at(source.idx);
    ASSERT_BUG(sp, sourceScope.data.is_Owning() && sourceScope.data.as_Owning().isTemporary, "Drop source is not a temporary scope: " << source);
    auto& sourceOwning = sourceScope.data.as_Owning();
    auto& sourceDrops = sourceOwning.dropSlots;
    auto sourceIt = ::std::find_if(sourceDrops.begin(), sourceDrops.end(), [&](const ScopeDropSlot& slot) {
        return !slot.isArgument && slot.index == idx;
    });
    if (sourceIt == sourceDrops.end()) {
        return;
    }
    auto sourceStateIt = ::std::find(sourceOwning.slots.begin(), sourceOwning.slots.end(), idx);
    ASSERT_BUG(sp, sourceStateIt != sourceOwning.slots.end(), "Missing state owner for " << value);

    bool sourceSeen = false;
    for (auto scopeIdx : ::reverse(scopeStack)) {
        if (scopeIdx == source.idx) {
            sourceSeen = true;
            continue;
        }
        if (!sourceSeen) {
            continue;
        }
        auto& scope = scopes.at(scopeIdx);
        if (auto* owning = scope.data.opt_Owning()) {
            if (!owning->isTemporary) {
                auto targetStateIt = ::std::find(owning->slots.begin(), owning->slots.end(), idx);
                ASSERT_BUG(sp, targetStateIt == owning->slots.end(), "Duplicate state owner for " << value);
                sourceOwning.slots.erase(sourceStateIt);
                sourceDrops.erase(sourceIt);
                owning->slots.push_back(idx);
                owning->dropSlots.push_back(ScopeDropSlot{false, idx});
                DEBUG("MOVE DROP " << value << " from scope " << source.idx << " to scope " << scopeIdx);
                return;
            }
        }
    }
    BUG(sp, "No variable scope outside temporary scope " << source);
}

void MirBuilder::moveVariableToScope(const Span& sp, unsigned int idx, const ScopeHandle& target) {
    ASSERT_BUG(sp, target.idx < scopes.size(), "Invalid `super let` target scope " << target);
    auto& targetScope = scopes.at(target.idx);
    ASSERT_BUG(sp, targetScope.data.is_Owning(), "`super let` target is not an owning scope: " << target);

    ScopeType::Data_Owning* source = nullptr;
    for (auto scopeIdx : ::reverse(scopeStack)) {
        auto* owning = scopes.at(scopeIdx).data.opt_Owning();
        if (!owning) {
            continue;
        }
        auto stateIt = ::std::find(owning->slots.begin(), owning->slots.end(), idx);
        if (stateIt != owning->slots.end()) {
            if (scopeIdx == target.idx) {
                return;
            }
            ASSERT_BUG(sp, !owning->isTemporary, "`super let` binding is already in a temporary scope");
            source = owning;
            owning->slots.erase(stateIt);
            break;
        }
    }
    ASSERT_BUG(sp, source, "`super let` binding _" << idx << " has no lexical scope");

    auto dropIt = ::std::find_if(source->dropSlots.begin(), source->dropSlots.end(), [&](const ScopeDropSlot& slot) {
        return !slot.isArgument && slot.index == idx;
    });
    ASSERT_BUG(sp, dropIt != source->dropSlots.end(), "`super let` binding _" << idx << " has no scheduled drop");
    source->dropSlots.erase(dropIt);

    auto& targetOwning = targetScope.data.as_Owning();
    ASSERT_BUG(sp, ::std::find(targetOwning.slots.begin(), targetOwning.slots.end(), idx) == targetOwning.slots.end(), "Duplicate `super let` state owner for _" << idx);
    targetOwning.slots.push_back(idx);
    targetOwning.dropSlots.push_back(ScopeDropSlot{false, idx});
}

void MirBuilder::dropLvalue(const Span& sp, const MIRLValue& value) {
    auto* state = getValStateMutP(sp, value);
    ASSERT_BUG(sp, state, "Dropping invalid value " << value);
    dropValueFromState(sp, *state, value.clone());
}

MIRLValue MirBuilder::newTemporary(const HIRTypeData* ty) {
    unsigned int rv = output.locals.size();
    DEBUG("DEFINE (temp) _" << rv << ": " << ty);

    assert(output.locals.size() == slotStates.size());
    output.locals.push_back(ty);
    slotStates.push_back(VarState::make_Invalid(InvalidType::Uninit));
    assert(output.locals.size() == slotStates.size());

    ScopeDef* topScope = nullptr;
    for (unsigned int i = scopeStack.size(); i--;) {
        auto idx = scopeStack[i];
        if (const auto* e = scopes.at(idx).data.opt_Owning()) {
            if (e->isTemporary) {
                topScope = &scopes.at(idx);
                break;
            }
        } else if (scopes.at(idx).data.is_Loop()) {
            // Newly created temporary within a loop, if there is a saved
            // state this temp needs a drop flag.
            // TODO: ^
        } else if (scopes.at(idx).data.is_Split()) {
            // Newly created temporary within a split, if there is a saved
            // state this temp needs a drop flag.
            // TODO: ^
        } else {
            // Nothign.
        }
    }
    assert(topScope);
    auto& tmpScope = topScope->data.as_Owning();
    assert(tmpScope.isTemporary);
    tmpScope.slots.push_back(rv);
    tmpScope.dropSlots.push_back(ScopeDropSlot{false, rv});
    return MIRLValue::newLocal(rv);
}

MIRLValue MirBuilder::lvalueOrTemp(const Span& sp, const HIRTypeData* ty, MIRRValue val) {
    if (val.is_Use()) {
        auto& e = val.as_Use();
        return mv$(e);
    }
    else {
        auto temp = newTemporary(ty);
        pushStmtAssign(sp, temp.clone(), mv$(val));
        return temp;
    }
}

MIRRValue MirBuilder::getResult(const Span& sp) {
    if (!resultValid) {
        BUG(sp, "No value avaliable");
    }
    auto rv = mv$(result);
    resultValid = false;
    DEBUG(rv);
    return rv;
}

MIRLValue MirBuilder::getResultUnwrapLvalue(const Span& sp) {
    auto rv = getResult(sp);
    if (rv.is_Use()) {
        auto& e = rv.as_Use();
        return mv$(e);
    }
    else {
        BUG(sp, "LValue expected, got RValue");
    }
}

MIRLValue MirBuilder::getResultInLvalue(const Span& sp, const HIRTypeData* ty, bool allowMissingValue /*=false*/) {
    if (allowMissingValue && !blockActive()) {
        return newTemporary(ty);
    }
    auto rv = getResult(sp);
    if (rv.is_Use()) {
        auto& e = rv.as_Use();
        return mv$(e);
    }
    else {
        auto temp = newTemporary(ty);
        pushStmtAssign(sp, MIRLValue(temp.clone()), mv$(rv));
        return temp;
    }
}

MIRParam MirBuilder::getResultInParam(const Span& sp, const HIRTypeData* ty, bool allowMissingValue) {
    if (allowMissingValue && !blockActive()) {
        return newTemporary(ty);
    }

    auto rv = getResult(sp);
    if (auto* e = rv.opt_Constant()) {
        return mv$(*e);
    }
    //else if( auto* e = rv.opt_Use() )
    //{
    //}
    else {
        auto temp = newTemporary(ty);
        pushStmtAssign(sp, MIRLValue(temp.clone()), mv$(rv));
        return MIRParam(mv$(temp));
    }
}

void MirBuilder::setResult(const Span& sp, MIRRValue val) {
    if (resultValid) {
        BUG(sp, "Pushing a result over an existing result");
    }
    result = mv$(val);
    resultValid = true;
    DEBUG(result);
}

void MirBuilder::pushStmtAssign(const Span& sp, MIRLValue dst, MIRRValue val, bool updateDestState /*=true*/) {
    DEBUG(dst << " = " << val);
    ASSERT_BUG(sp, blockActive_, "Pushing statement with no active block");

    auto movedParam = [&](const MIRParam& p) {
        if (const auto* e = p.opt_LValue()) {
            this->movedLvalue(sp, *e);
        }
    };
    switch (val.tag()) {
        case MIRRValue::TAG_Use: {
            auto& e = val.as_Use();
            this->movedLvalue(sp, e);
            break;
        }
        case MIRRValue::TAG_Constant: {
            break;
        }
        case MIRRValue::TAG_SizedArray: {
            auto& e = val.as_SizedArray();
            movedParam(e.val);
            break;
        }
        case MIRRValue::TAG_Borrow: {
            auto& e = val.as_Borrow();
            if (e.type == HIRBorrowType::Owned) {
                TODO(sp, "Move using &move");
                // Likely would require a marker that ensures that the memory isn't reused.
                this->movedLvalue(sp, e.val);
            } else {
                // Doesn't move
            }
            break;
        }
        case MIRRValue::TAG_Cast: {
            auto& e = val.as_Cast();
            this->movedLvalue(sp, e.val);
            break;
        }
        case MIRRValue::TAG_BinOp: {
            auto& e = val.as_BinOp();
            switch (e.op) {
                case MIRBinOp::EQ:
                case MIRBinOp::NE:
                case MIRBinOp::GT:
                case MIRBinOp::GE:
                case MIRBinOp::LT:
                case MIRBinOp::LE:
                    // Takes an implicit borrow... and only works on copy, so why is this block here?
                    break;
                default:
                    movedParam(e.valL);
                    movedParam(e.valR);
                    break;
            }
            break;
        }
        case MIRRValue::TAG_UniOp: {
            auto& e = val.as_UniOp();
            this->movedLvalue(sp, e.val);
            break;
        }
        case MIRRValue::TAG_DstMeta: {
            // Doesn't move
            break;
        }
        case MIRRValue::TAG_DstPtr: {
            // Doesn't move
            break;
        }
        case MIRRValue::TAG_MakeDst: {
            auto& e = val.as_MakeDst();
            movedParam(e.ptrVal); movedParam(e.metaVal);
            break;
        }
        case MIRRValue::TAG_Tuple: {
            auto& e = val.as_Tuple();
            for (const auto& val : e.vals) movedParam(val);
            break;
        }
        case MIRRValue::TAG_Array: {
            auto& e = val.as_Array();
            for (const auto& val : e.vals) movedParam(val);
            break;
        }
        case MIRRValue::TAG_UnionVariant: {
            auto& e = val.as_UnionVariant();
            movedParam(e.val);
            break;
        }
        case MIRRValue::TAG_EnumVariant: {
            auto& e = val.as_EnumVariant();
            for (const auto& val : e.vals) movedParam(val);
            break;
        }
        case MIRRValue::TAG_Struct: {
            auto& e = val.as_Struct();
            for (const auto& val : e.vals) movedParam(val);
            break;
        }
    }

    // Drop target if populated
    if (updateDestState) {
        if (const auto* enumVariant = val.opt_EnumVariant()) {
            markValueAssignedVariant(sp, dst, enumVariant->index);
        } else {
            markValueAssigned(sp, dst);
        }
    }
    this->pushStmt(sp, MIRStatement::make_Assign({mv$(dst), mv$(val)}));
}

void MirBuilder::pushStmtDrop(const Span& sp, MIRLValue val, unsigned int flag /*=~0u*/) {
    ASSERT_BUG(sp, blockActive_, "Pushing statement with no active block");

    if (lvalueIsCopy(sp, val)) {
        // Don't emit a drop for Copy values
        return;
    }

    if (!buildingCleanup && dropEmitter && dropEmitter->emitDeepDrop(sp, val, flag)) {
        return;
    }

    this->pushStmtDropRaw(sp, mv$(val), flag);
}

void MirBuilder::pushStmtDropRaw(const Span& sp, MIRLValue val, unsigned int flag /*=~0u*/) {
    ASSERT_BUG(sp, blockActive_, "Pushing statement with no active block");
    this->pushDropTerminator(sp, MIRDropKind::DEEP, mv$(val), flag);
}

void MirBuilder::pushStmtDropShallow(const Span& sp, MIRLValue val, unsigned int flag /*=~0u*/) {
    ASSERT_BUG(sp, blockActive_, "Pushing statement with no active block");

    // TODO: Ensure that the type is a Box?

    if (!buildingCleanup && dropEmitter && dropEmitter->emitShallowDrop(sp, val, flag)) {
        return;
    }

    this->pushDropTerminator(sp, MIRDropKind::SHALLOW, mv$(val), flag);
}

void MirBuilder::pushDropTerminator(const Span& sp, MIRDropKind kind, MIRLValue val, unsigned int flag) {
    ASSERT_BUG(sp, blockActive_, "Dropping a value with no active block");

    const auto nextBlock = newBbUnlinked();
    auto unwind = buildingCleanup ? MIRUnwindAction::make_Terminate({}) : makeUnwindAction(sp, &val);
    endBlock(MIRTerminator::make_Drop({kind, mv$(val), flag, nextBlock, mv$(unwind)}));
    setCurBlock(nextBlock);
}

void MirBuilder::pushStmtAsm(const Span& sp, MIRStatement::Data_Asm data) {
    ASSERT_BUG(sp, blockActive_, "Pushing statement with no active block");

    // 1. Mark outputs as valid
    for (const auto& v : data.outputs) {
        markValueAssigned(sp, v.second);
    }

    // 2. Push
    this->pushStmt(sp, MIRStatement::make_Asm(mv$(data)));
}

void MirBuilder::pushStmtSetDropflagVal(const Span& sp, unsigned int idx, bool value) {
    this->pushStmt(sp, MIRStatement::make_SetDropFlag({idx, value, ~0u}));
}

void MirBuilder::pushStmtSetDropflagOther(const Span& sp, unsigned int idx, unsigned int other) {
    this->pushStmt(sp, MIRStatement::make_SetDropFlag({idx, false, other}));
}

void MirBuilder::pushStmtSetDropflagDefault(const Span& sp, unsigned int idx) {
    this->pushStmt(sp, MIRStatement::make_SetDropFlag({idx, this->getDropFlagDefault(sp, idx), ~0u}));
}

void MirBuilder::pushStmt(const Span& sp, MIRStatement stmt) {
    ASSERT_BUG(sp, blockActive_, "Pushing statement with no active block");
    auto& blk = output.blocks.at(currentBlock);
    DEBUG("BB" << currentBlock << "/" << blk.statements.size() << " = " << stmt);
    blk.statements.push_back(mv$(stmt));
}

void MirBuilder::markValueAssigned(const Span& sp, const MIRLValue& dst) {
    markValueAssignedState(sp, dst, VarState::make_Valid({}));
}

void MirBuilder::markValueAssignedVariant(const Span& sp, const MIRLValue& dst, unsigned int variantIndex) {
    if (dst.root.is_Return()) {
        ASSERT_BUG(sp, dst.wrappers.empty(), "Assignment to a component of the return value should be impossible.");
        return;
    }

    const auto* ty = valType(sp, dst);
    ASSERT_BUG(sp, ty->is_Path() && ty->as_Path().binding.is_Enum(), "Enum variant assigned to non-enum " << ty);
    const auto& enm = *ty->as_Path().binding.as_Enum();
    if (!enm.data.is_Data()) {
        markValueAssigned(sp, dst);
        return;
    }
    const auto variantCount = enm.numVariants();
    ASSERT_BUG(sp, variantIndex < variantCount, "Enum variant index out of range");

    VarState::Data_Partial partial{{}, ~0u};
    partial.innerStates.reserve(variantCount);
    for (size_t i = 0; i < variantCount; i++) {
        partial.innerStates.push_back(VarState::make_Invalid(InvalidType::Uninit));
    }
    partial.innerStates[variantIndex] = VarState::make_Valid({});
    markValueAssignedState(sp, dst, VarState::make_Partial(mv$(partial)));
}

void MirBuilder::markValueAssignedState(const Span& sp, const MIRLValue& dst, VarState newState) {
    if (dst.root.is_Return()) {
        ASSERT_BUG(sp, dst.wrappers.empty(), "Assignment to a component of the return value should be impossible.");
        return;
    }
    VarState* stateP = getValStateMutP(sp, dst, /*expect_valid=*/newState.is_Valid());

    if (stateP) {
        if ((*stateP).is_Invalid()) {
            auto& se = (*stateP).as_Invalid();
            ASSERT_BUG(sp, se != InvalidType::Descoped, "Assining of descoped variable - " << dst);
        }
        dropValueFromState(sp, *stateP, dst.clone());
        DEBUG("State " << dst << " " << *stateP << " => " << newState);
        *stateP = std::move(newState);
    } else {
        // Assigning into non-tracked locations still causes a drop
        auto state = VarState::make_Valid({});
        dropValueFromState(sp, state, dst.clone());
    }
}

void MirBuilder::raiseTemporaries(const Span& sp, const MIRLValue& val, const ScopeHandle& scope, bool toAbove /*=false*/) {
    TRACE_FUNCTION_F(val);
    for (const auto& w : val.wrappers) {
        if (w.is_Index()) {
            // Raise index temporary
            raiseTemporaries(sp, MIRLValue::newLocal(w.as_Index()), scope, toAbove);
        }
    }
    if (!val.root.is_Local()) {
        // No raising of these source values?
        return;
    }
    const auto idx = val.root.as_Local();
    bool isTemp = (idx >= firstTempIdx);

    // Find controlling scope
    auto scopeIt = scopeStack.rbegin();
    while (scopeIt != scopeStack.rend()) {
        auto& scopeDef = scopes.at(*scopeIt);

        if (*scopeIt == scope.idx && !toAbove) {
            DEBUG(val << " defined in or above target (scope " << scope << ")");
        }

        if (scopeDef.data.is_Owning()) {
            auto& e = scopeDef.data.as_Owning();
            if (e.isTemporary == isTemp) {
                auto tmpIt = ::std::find(e.slots.begin(), e.slots.end(), idx);
                if (tmpIt != e.slots.end()) {
                    e.slots.erase(tmpIt);
                    auto dropIt = ::std::find_if(e.dropSlots.begin(), e.dropSlots.end(), [&](const ScopeDropSlot& slot) {
                        return !slot.isArgument && slot.index == idx;
                    });
                    ASSERT_BUG(sp, dropIt != e.dropSlots.end(), "Missing drop schedule for " << val);
                    e.dropSlots.erase(dropIt);
                    DEBUG("Raise slot " << idx << " from " << *scopeIt);
                    break;
                }
            } else {
                // TODO: Should this care about variables?
            }
        }
        else {
            // TODO: Does this need to handle this value being set in the
            // split scopes?
        }
        // If the variable was defined above the desired scope (i.e. this didn't find it), return
        if (*scopeIt == scope.idx) {
            DEBUG("Value " << val << " is defined above the target (scope " << scope << ")");
            return;
        }
        ++scopeIt;
    }
    if (scopeIt == scopeStack.rend()) {
        // Temporary wasn't defined in a visible scope?
        BUG(sp, val << " wasn't defined in a visible scope");
        return;
    }

    // If the definition scope was the target scope
    bool targetSeen = false;
    if (*scopeIt == scope.idx) {
        if (toAbove) {
            // Want to shift to any above (but not including) it
            ++scopeIt;
        } else {
            // Want to shift to it or above.
        }

        targetSeen = true;
    } else {
        // Don't bother searching the original definition scope
        ++scopeIt;
    }

    // Iterate stack until:
    // - The target scope is seen
    // - AND a scope was found for it
    for (; scopeIt != scopeStack.rend(); ++scopeIt) {
        auto& scopeDef = scopes.at(*scopeIt);
        DEBUG("> Cross " << *scopeIt << " - " << scopeDef.data.tagStr());

        if (*scopeIt == scope.idx) {
            targetSeen = true;
        }

        switch (scopeDef.data.tag()) {
            case ScopeType::TAG_Owning: {
                auto& e = scopeDef.data.as_Owning();
                if (targetSeen && e.isTemporary == isTemp) {
                    e.slots.push_back(idx);
                    e.dropSlots.push_back(ScopeDropSlot{false, idx});
                    DEBUG("- to " << *scopeIt);
                    return;
                }
                break;
            }
            case ScopeType::TAG_Loop: {
                auto& sdLoop = scopeDef.data.as_Loop();
                // If there is an exit state present, ensure that this variable is
                // present in that state (as invalid, as it can't have been valid
                // externally)
                if (sdLoop.exitStateValid) {
                    DEBUG("Adding " << val << " as unset to loop exit state");
                    auto v = sdLoop.exitState.states.insert(::std::make_pair(idx, VarState(InvalidType::Uninit)));
                    ASSERT_BUG(sp, v.second, "Raising " << val << " which already had a state entry");
                } else {
                    DEBUG("Crossing loop with no existing exit state");
                }
                break;
            }
            case ScopeType::TAG_Split: {
                auto& sdSplit = scopeDef.data.as_Split();
                // If the split has already registered an exit state, ensure that
                // this variable is present in it. (as invalid)
                if (sdSplit.endStateValid) {
                    DEBUG("Adding " << val << " as unset to loop exit state");
                    auto v = sdSplit.endState.states.insert(::std::make_pair(idx, VarState(InvalidType::Uninit)));
                    ASSERT_BUG(sp, v.second, "Raising " << val << " which already had a state entry");
                } else {
                    DEBUG("Crossing split with no existing end state");
                }

                // TODO: This should update the outer state to unset.
                auto& arm = sdSplit.arms.back();
                arm.states.insert(::std::make_pair(idx, getSlotState(sp, idx, SlotType::Local).clone()));
                slotStates.at(idx) = VarState(InvalidType::Uninit);
                break;
            }
            case ScopeType::TAG_Freeze: {
                auto& sde = scopeDef.data.as_Freeze();
                // Can we raise across a freeze state?
                if (!sde.unfrozen) {
                    TODO(sp, "Raising temporary across a freeze?");
                }
                break;
            }
        }
    }
    BUG(sp, "Couldn't find a scope to raise " << val << " into");
}

void MirBuilder::raiseTemporaries(const Span& sp, const MIRRValue& rval, const ScopeHandle& scope, bool toAbove /*=false*/) {
    auto raiseVars = [&](const MIRParam& p) {
        if (const auto* e = p.opt_LValue()) {
            this->raiseTemporaries(sp, *e, scope, toAbove);
        }
    };
    switch (rval.tag()) {
        case MIRRValue::TAG_Use: {
            auto& e = rval.as_Use();
            this->raiseTemporaries(sp, e, scope, toAbove);
            break;
        }
        case MIRRValue::TAG_Constant: {
            break;
        }
        case MIRRValue::TAG_SizedArray: {
            auto& e = rval.as_SizedArray();
            raiseVars(e.val);
            break;
        }
        case MIRRValue::TAG_Borrow: {
            auto& e = rval.as_Borrow();
            // TODO: Wait, is this valid?
            this->raiseTemporaries(sp, e.val, scope, toAbove);
            break;
        }
        case MIRRValue::TAG_Cast: {
            auto& e = rval.as_Cast();
            this->raiseTemporaries(sp, e.val, scope, toAbove);
            break;
        }
        case MIRRValue::TAG_BinOp: {
            auto& e = rval.as_BinOp();
            raiseVars(e.valL); raiseVars(e.valR);
            break;
        }
        case MIRRValue::TAG_UniOp: {
            auto& e = rval.as_UniOp();
            this->raiseTemporaries(sp, e.val, scope, toAbove);
            break;
        }
        case MIRRValue::TAG_DstMeta: {
            auto& e = rval.as_DstMeta();
            this->raiseTemporaries(sp, e.val, scope, toAbove);
            break;
        }
        case MIRRValue::TAG_DstPtr: {
            auto& e = rval.as_DstPtr();
            this->raiseTemporaries(sp, e.val, scope, toAbove);
            break;
        }
        case MIRRValue::TAG_MakeDst: {
            auto& e = rval.as_MakeDst();
            raiseVars(e.ptrVal); raiseVars(e.metaVal);
            break;
        }
        case MIRRValue::TAG_Tuple: {
            auto& e = rval.as_Tuple();
            for (const auto& val : e.vals) raiseVars(val);
            break;
        }
        case MIRRValue::TAG_Array: {
            auto& e = rval.as_Array();
            for (const auto& val : e.vals) raiseVars(val);
            break;
        }
        case MIRRValue::TAG_UnionVariant: {
            auto& e = rval.as_UnionVariant();
            raiseVars(e.val);
            break;
        }
        case MIRRValue::TAG_EnumVariant: {
            auto& e = rval.as_EnumVariant();
            for (const auto& val : e.vals) raiseVars(val);
            break;
        }
        case MIRRValue::TAG_Struct: {
            auto& e = rval.as_Struct();
            for (const auto& val : e.vals) raiseVars(val);
            break;
        }
    }
}

MirBuilder::SaveCodeProto MirBuilder::codeSaveStart() {
    TRACE_FUNCTION;
    // Push to the stack
    // Create a new block and link in
    static size_t sNextIndex;
    SaveCodeProto rv;
    rv.index = sNextIndex++;
    codeSaveStack.push_back(CodeSaveStackEnt{rv.index, {}});
    // If currently in a block, then go into a new one
    if (blockActive()) {
        newBbLinked();
    }
    return rv;
}

MirBuilder::SavedCode MirBuilder::codeSaveEnd(SaveCodeProto h) {
    // Check stack
    assert(!blockActive()); // Can't be a block active
    assert(!codeSaveStack.empty());
    assert(h.index == codeSaveStack.back().index);
    SavedCode rv;
    rv.blocks = std::move(codeSaveStack.back().blocks);
    codeSaveStack.pop_back();
    DEBUG("rv.blocks = { " << rv.blocks << " }");
    return rv;
}

void MirBuilder::insertCloned(const Span& sp, const SavedCode& c, CloneMapper& mapper) {
    TRACE_FUNCTION;
    assert(blockActive()); // Need an active block to start inserting
    if (!c.blocks.empty()) {
        struct Cloner: MIRCloner {
            CloneMapper& mapper;
            std::map<unsigned, unsigned> newBlockMap;

            Cloner(const Span& sp, CloneMapper& mapper, HIRTypeInterner& types)
                : MIRCloner(sp, types)
                , mapper(mapper)
            {
            }

            MIRBasicBlockId mapBbIdx(MIRBasicBlockId idx) const override {
                auto it = newBlockMap.find(idx);
                if (it != newBlockMap.end()) {
                    return it->second;
                }
                return mapper.updateBbRef(idx);
            }
        } cloner{sp, mapper, resolve_.hirCrate().types};

        // Allocate new block IDs for all referenced blocks
        for (auto bbIdx : c.blocks) {
            cloner.newBlockMap.insert(std::make_pair(bbIdx, newBbUnlinked()));
        }
        // End the current block with a goto to the first block
        endBlock(MIRTerminator::make_Goto({cloner.newBlockMap[c.blocks.front()]}));

        DEBUG("c.blocks = [" << c.blocks << "]");
        DEBUG("new_block_map = {" << cloner.newBlockMap << "}");
        // Start inserting (and remapping)
        for (auto srcIdx : c.blocks) {
            auto newIdx = cloner.newBlockMap.at(srcIdx);
            DEBUG("BB" << newIdx << " <= BB" << srcIdx);
            const auto& src = output.blocks[srcIdx];
            setCurBlock(newIdx);
            for (const auto& v : src.statements) {
                pushStmt(sp, cloner.cloneStmt(v));
            }
            endBlock(cloner.cloneTerm(src.terminator));
        }
        // Leave no active block
    }
}

void MirBuilder::setCurBlock(unsigned int newBlock) {
    ASSERT_BUG(Span(), !blockActive_, "Updating block when previous is active");
    ASSERT_BUG(Span(), newBlock < output.blocks.size(), "Invalid block ID being started - " << newBlock);
    ASSERT_BUG(Span(), output.blocks[newBlock].terminator.is_Incomplete(), "Attempting to resume a completed block - BB" << newBlock);
    // Record this new block in the save stack entries
    for (auto& v : codeSaveStack) {
        // Just in case a block is saved+resumed
        if (std::find(v.blocks.begin(), v.blocks.end(), newBlock) == v.blocks.end()) {
            v.blocks.push_back(newBlock);
        }
    }
    DEBUG("BB" << newBlock << " START");
    currentBlock = newBlock;
    blockActive_ = true;
}

void MirBuilder::endBlock(MIRTerminator term) {
    if (!blockActive_) {
        BUG(Span(), "Terminating block when none active");
    }
    if (auto* call = term.opt_Call(); call && !call->tracksCaller) {
        if (const auto* path = call->fcn.opt_Path()) {
            MonomorphState params(resolve_.hirCrate().types);
            auto value = resolve_.getValue(rootSpan, *path, params, /*signatureOnly=*/true);
            if (const auto* function = value.opt_Function()) {
                call->tracksCaller = resolve_.hirCrate().functionTracksCaller(rootSpan, *path, **function);
            }
        }
    }
    DEBUG("BB" << currentBlock << " END -> " << term);
    output.blocks.at(currentBlock).terminator = mv$(term);
    blockActive_ = false;
    currentBlock = 0;
}

MIRBasicBlockId MirBuilder::pauseCurBlock() {
    if (!blockActive_) {
        BUG(Span(), "Pausing block when none active");
    }
    DEBUG("BB" << currentBlock << " PAUSE");
    blockActive_ = false;
    auto rv = currentBlock;
    currentBlock = 0;
    return rv;
}

MIRBasicBlockId MirBuilder::newBbLinked() {
    auto rv = newBbUnlinked();
    DEBUG("BB" << rv);
    endBlock(MIRTerminator::make_Goto(rv));
    setCurBlock(rv);
    return rv;
}

MIRBasicBlockId MirBuilder::newBbUnlinked() {
    auto rv = output.blocks.size();
    DEBUG("BB" << rv);
    output.blocks.push_back({});
    output.blocks.back().isCleanup = buildingCleanup;
    return rv;
}

unsigned int MirBuilder::newDropFlag(bool defaultState) {
    auto rv = output.dropFlags.size();
    output.dropFlags.push_back(defaultState);
    for (size_t i = scopeStack.size(); i--;) {
        if (auto* e = scopes.at(scopeStack[i]).data.opt_Loop()) {
            e->dropFlags.push_back(rv);
            break;
        }
    }
    DEBUG("df$" << rv << " := " << defaultState);
    return rv;
}

unsigned int MirBuilder::newDropFlagAndSet(const Span& sp, bool setState) {
    auto rv = newDropFlag(!setState);
    pushStmtSetDropflagVal(sp, rv, setState);
    return rv;
}

bool MirBuilder::getDropFlagDefault(const Span& sp, unsigned int idx) {
    return output.dropFlags.at(idx);
}

void MirBuilder::dropFlagAlias(unsigned int oldIdx, unsigned int newIdx) {
    dropFlagAliases[oldIdx].push_back(newIdx);
}

ScopeHandle MirBuilder::newScopeVar(const Span& sp) {
    unsigned int idx = scopes.size();
    scopes.push_back(ScopeDef{sp, ScopeType::make_Owning({false, {}, {}})});
    scopeStack.push_back(idx);
    DEBUG("START (var) scope " << idx);
    return ScopeHandle{*this, idx};
}

ScopeHandle MirBuilder::newScopeTemp(const Span& sp) {
    unsigned int idx = scopes.size();

    scopes.push_back(ScopeDef{sp, ScopeType::make_Owning({true, {}, {}})});
    scopeStack.push_back(idx);
    DEBUG("START (temp) scope " << idx);
    return ScopeHandle{*this, idx};
}

ScopeHandle MirBuilder::newScopeSplit(const Span& sp) {
    unsigned int idx = scopes.size();
    scopes.push_back(ScopeDef{sp, ScopeType::make_Split({})});
    scopes.back().data.as_Split().arms.push_back({});
    scopeStack.push_back(idx);
    DEBUG("START (split) scope " << idx);
    return ScopeHandle{*this, idx};
}

ScopeHandle MirBuilder::newScopeLoop(const Span& sp) {
    unsigned int idx = scopes.size();
    scopes.push_back(ScopeDef{sp, ScopeType::make_Loop({})});
    scopes.back().data.as_Loop().entryBb = currentBlock;
    scopeStack.push_back(idx);
    DEBUG("START (loop) scope " << idx);
    return ScopeHandle{*this, idx};
}

ScopeHandle MirBuilder::newScopeFreeze(const Span& sp) {
    unsigned int idx = scopes.size();
    scopes.push_back(ScopeDef{sp, ScopeType::make_Freeze({})});
    scopeStack.push_back(idx);
    DEBUG("START (freeze) scope " << idx);
    return ScopeHandle{*this, idx};
}

void MirBuilder::terminateScope(const Span& sp, ScopeHandle scope, bool emitCleanup /*=true*/) {
    TRACE_FUNCTION_F("DONE scope " << scope.idx << " - " << (emitCleanup ? "CLEANUP" : "NO CLEANUP"));
    // 1. Check that this is the current scope (at the top of the stack)
    if (scopeStack.empty() || scopeStack.back() != scope.idx) {
        DEBUG("- m_scope_stack = [" << scopeStack << "]");
        auto it = ::std::find(scopeStack.begin(), scopeStack.end(), scope.idx);
        if (it == scopeStack.end()) {
            BUG(sp, "Terminating scope not on the stack - scope " << scope.idx);
        }
        BUG(sp, "Terminating scope " << scope.idx << " when not at top of stack, " << (scopeStack.end() - it - 1) << " scopes in the way");
    }

    auto& scopeDef = scopes.at(scope.idx);
    //}

    if (emitCleanup && scopeDef.complete == false) {
        // 2. Emit drops for all non-moved variables (share with below)
        dropScopeValues(scopeDef);

        // Emit ScopeEnd for all controlled values
    }

    // 3. Pop scope (last because `drop_scope_values` uses the stack)
    scopeStack.pop_back();

    completeScope(scopeDef);
}

void MirBuilder::raiseAll(const Span& sp, ScopeHandle source, const ScopeHandle& target) {
    TRACE_FUNCTION_F("scope " << source.idx << " => " << target.idx);

    // 1. Check that this is the current scope (at the top of the stack)
    if (scopeStack.empty() || scopeStack.back() != source.idx) {
        DEBUG("- m_scope_stack = [" << scopeStack << "]");
        auto it = ::std::find(scopeStack.begin(), scopeStack.end(), source.idx);
        if (it == scopeStack.end()) {
            BUG(sp, "Terminating scope not on the stack - scope " << source.idx);
        }
        BUG(sp, "Terminating scope " << source.idx << " when not at top of stack, " << (scopeStack.end() - it - 1) << " scopes in the way");
    }
    auto& srcScopeDef = scopes.at(source.idx);

    ASSERT_BUG(sp, srcScopeDef.data.is_Owning(), "Rasising scopes can only be done on temporaries (source)");
    ASSERT_BUG(sp, srcScopeDef.data.as_Owning().isTemporary, "Rasising scopes can only be done on temporaries (source)");
    auto& srcList = srcScopeDef.data.as_Owning().slots;
    for (auto idx : srcList) {
        DEBUG("> Raising " << MIRLValue::newLocal(idx));
        assert(idx >= firstTempIdx);
    }

    // Seek up stack until the target scope is seen
    auto it = scopeStack.rbegin() + 1;
    for (; it != scopeStack.rend() && *it != target.idx; ++it) {
        auto& scopeDef = scopes.at(*it);
        DEBUG("Through S" << *it << ": " << scopeDef.data.tagStr());

        if (auto* sdLoop = scopeDef.data.opt_Loop()) {
            if (sdLoop->exitStateValid) {
                DEBUG("Crossing loop with existing end state");
                // Insert these values as Invalid, both in the existing exit state, and in the changed list
                for (auto idx : srcList) {
                    auto v = sdLoop->exitState.states.insert(::std::make_pair(idx, VarState(InvalidType::Uninit)));
                    ASSERT_BUG(sp, v.second, "");
                }
            } else {
                DEBUG("Crossing loop with no end state");
            }

            for (auto idx : srcList) {
                auto v2 = sdLoop->changedSlots.insert(::std::make_pair(idx, VarState(InvalidType::Uninit)));
                ASSERT_BUG(sp, v2.second, "");
            }
        } else if (auto* sdSplit = scopeDef.data.opt_Split()) {
            if (sdSplit->endStateValid) {
                DEBUG("Crossing split with existing end state");
                // Insert these indexes as Invalid
                for (auto idx : srcList) {
                    auto v = sdSplit->endState.states.insert(::std::make_pair(idx, VarState(InvalidType::Uninit)));
                    ASSERT_BUG(sp, v.second, "");
                }
            } else {
                DEBUG("Crossing split with no end state");
            }

            // TODO: Insert current state in the current arm
            assert(!sdSplit->arms.empty());
            auto& arm = sdSplit->arms.back();
            for (auto idx : srcList) {
                arm.states.insert(::std::make_pair(idx, mv$(slotStates.at(idx))));
                slotStates.at(idx) = VarState(InvalidType::Uninit);
            }
        }
    }
    if (it == scopeStack.rend()) {
        BUG(sp, "Moving values to a scope not on the stack - scope " << target.idx);
    }
    auto& tgtScopeDef = scopes.at(target.idx);
    DEBUG("To S" << target.idx << ": " << tgtScopeDef.data.tagStr());
    ASSERT_BUG(sp, tgtScopeDef.data.is_Owning(), "Rasising scopes can only be done on temporaries (target)");
    ASSERT_BUG(sp, tgtScopeDef.data.as_Owning().isTemporary, "Rasising scopes can only be done on temporaries (target)");

    // Move all defined variables from one to the other
    auto& tgtList = tgtScopeDef.data.as_Owning().slots;
    tgtList.insert(tgtList.end(), srcList.begin(), srcList.end());
    auto& srcDropList = srcScopeDef.data.as_Owning().dropSlots;
    auto& tgtDropList = tgtScopeDef.data.as_Owning().dropSlots;
    tgtDropList.insert(tgtDropList.end(), srcDropList.begin(), srcDropList.end());

    // Scope completed
    scopeStack.pop_back();
    srcScopeDef.complete = true;
}

void MirBuilder::terminateScopeEarly(const Span& sp, const ScopeHandle& scope, bool loopExit /*=false*/) {
    TRACE_FUNCTION_F("EARLY scope " << scope.idx);

    // 1. Ensure that this block is in the stack
    auto it = ::std::find(scopeStack.begin(), scopeStack.end(), scope.idx);
    if (it == scopeStack.end()) {
        BUG(sp, "Early-terminating scope not on the stack");
    }
    unsigned int slot = it - scopeStack.begin();

    bool useFrozenExitState = false;
    for (unsigned int i = scopeStack.size(); i-- > slot;) {
        const auto& data = scopes.at(scopeStack[i]).data;
        const auto* freeze = data.opt_Freeze();
        useFrozenExitState |= freeze && !freeze->unfrozen;
        // An exit that crosses a conditional is a branch of its own: what it
        // drops is dropped only on that branch, so the states the fall-through
        // sees must not change. The drops within the exit still see each other's
        // effect, which is what keeps its own unwind edges right.
        useFrozenExitState |= data.is_Split() || data.is_Loop();
    }
    ASSERT_BUG(sp, !useFrozenExitState || !frozenExitStateActive, "Nested frozen early-exit state");
    if (useFrozenExitState) {
        frozenExitStateActive = true;
        frozenExitSlotStates.clear();
        frozenExitArgStates.clear();
    }

    bool isConditional = false;
    for (unsigned int i = scopeStack.size(); i-- > slot;) {
        auto idx = scopeStack[i];
        auto& scopeDef = scopes.at(idx);

        if (idx == scope.idx) {
            // If this is exiting a loop, save the state so the variable state after the loop is known.
            if (loopExit && scopeDef.data.is_Loop()) {
                terminateLoopEarly(sp, scopeDef.data.as_Loop());
            }
        }

        // If a conditional block is hit, prevent full termination of the rest
        if (scopeDef.data.is_Split() || scopeDef.data.is_Loop()) {
            isConditional = true;
        }

        if (!isConditional) {
            DEBUG("Complete scope " << idx);
            dropScopeValues(scopeDef);
            completeScope(scopeDef);
        } else {
            // Mark patial within this scope?
            DEBUG("Drop part of scope " << idx);

            // Emit drops for dropped values within this scope
            dropScopeValues(scopeDef);
            // Inform the scope that it's been early-exited
            if (scopeDef.data.is_Split()) {
                auto& e = scopeDef.data.as_Split();
                e.arms.back().hasEarlyTerminated = true;
            }
        }
    }

    if (useFrozenExitState) {
        frozenExitSlotStates.clear();
        frozenExitArgStates.clear();
        frozenExitStateActive = false;
    }
}

namespace {
    static void mergeOuterValidity(const Span& sp, MirBuilder& builder, unsigned int& oldFlag, bool newValid) {
        if (oldFlag == ~0u) {
            if (!newValid) {
                oldFlag = builder.newDropFlagAndSet(sp, false);
            }
        } else {
            builder.pushStmtSetDropflagVal(sp, oldFlag, newValid);
        }
    }

    static void mergeOuterValidity(const Span& sp, MirBuilder& builder, unsigned int& oldFlag, unsigned int newFlag) {
        if (oldFlag == newFlag) {
            return;
        }
        if (oldFlag == ~0u) {
            if (builder.getDropFlagDefault(sp, newFlag)) {
                oldFlag = newFlag;
            } else {
                oldFlag = builder.newDropFlag(true);
                builder.pushStmtSetDropflagOther(sp, oldFlag, newFlag);
            }
        } else {
            builder.pushStmtSetDropflagOther(sp, oldFlag, newFlag);
        }
    }

    static unsigned int mergeInvalidWithPartialOuter(const Span& sp, MirBuilder& builder, unsigned int newFlag) {
        const auto outerFlag = builder.newDropFlag(false);
        if (newFlag == ~0u) {
            builder.pushStmtSetDropflagVal(sp, outerFlag, true);
        } else {
            builder.pushStmtSetDropflagOther(sp, outerFlag, newFlag);
        }
        return outerFlag;
    }

    static void mergeState(const Span& sp, MirBuilder& builder, const MIRLValue& lv, VarState& oldState, const VarState& newState) {
        TRACE_FUNCTION_FR(lv << " : " << oldState << " <= " << newState, lv << " : " << oldState);
        switch (oldState.tag()) {
            case VarState::TAG_Invalid:
                switch (newState.tag()) {
                    case VarState::TAG_Invalid:
                        // Invalid->Invalid :: Choose the highest of the invalid types (TODO)
                        return;
                    case VarState::TAG_Valid:
                        // Allocate a drop flag
                        oldState = VarState::make_Optional(builder.newDropFlagAndSet(sp, true));
                        return;
                    case VarState::TAG_Optional: {
                        // Was invalid, now optional.
                        auto flagIdx = newState.as_Optional();
                        if (true || builder.getDropFlagDefault(sp, flagIdx) != false) {
                            auto newFlag = builder.newDropFlag(false);
                            builder.pushStmtSetDropflagOther(sp, newFlag, flagIdx);
                            oldState = VarState::make_Optional(newFlag);
                        } else {
                            oldState = VarState::make_Optional(flagIdx);
                        }
                        return;
                    }
                    case VarState::TAG_MovedOut: {
                        const auto& nse = newState.as_MovedOut();

                        // Create a new state that is internally valid and uses the same drop flag
                        oldState = VarState::make_MovedOut({box$(oldState.clone()), nse.outerFlag});
                        auto& ose = oldState.as_MovedOut();
                        if (ose.outerFlag != ~0u) {
                            // If the flag's default isn't false, then create a new flag that does have such a default
                            // - Other arm (old_state) uses default, this arm (new_state) can be manipulated
                            if (builder.getDropFlagDefault(sp, ose.outerFlag) != false) {
                                auto newFlag = builder.newDropFlag(false);
                                builder.pushStmtSetDropflagOther(sp, newFlag, nse.outerFlag);
                                ose.outerFlag = newFlag;
                            }
                        } else {
                            // In the old arm, the container isn't valid. Create a drop flag with a default of false and set it to true
                            ose.outerFlag = builder.newDropFlag(false);
                            builder.pushStmtSetDropflagVal(sp, ose.outerFlag, true);
                        }

                        const bool isBox = builder.isTypeOwnedBox(builder.valType(sp, lv));
                        if (isBox) {
                            mergeState(sp, builder, MIRLValue::newDeref(lv.clone()), *ose.innerState, *nse.innerState);
                        } else {
                            BUG(sp, "Handle MovedOut on non-Box");
                        }
                        return;
                    }
                    case VarState::TAG_Partial: {
                        const auto& nse = newState.as_Partial();
                        const auto* lvTy = builder.valType(sp, lv);
                        const bool is_enum = lvTy->is_Path() && lvTy->as_Path().binding.is_Enum();
                        const auto outerFlag = is_enum ? mergeInvalidWithPartialOuter(sp, builder, nse.outerFlag) : ~0u;

                        // Create a partial filled with Invalid
                        {
                            ::std::vector<VarState> inner;
                            inner.reserve(nse.innerStates.size());
                            for (size_t i = 0; i < nse.innerStates.size(); i++) {
                                inner.push_back(oldState.clone());
                            }
                            oldState = VarState::make_Partial({mv$(inner), outerFlag});
                        }
                        auto& ose = oldState.as_Partial();
                        if (is_enum) {
                            for (size_t i = 0; i < ose.innerStates.size(); i++) {
                                mergeState(sp, builder, MIRLValue::newDowncast(lv.clone(), static_cast<unsigned int>(i)), ose.innerStates[i], nse.innerStates[i]);
                            }
                        } else {
                            for (unsigned int i = 0; i < ose.innerStates.size(); i++) {
                                mergeState(sp, builder, MIRLValue::newField(lv.clone(), i), ose.innerStates[i], nse.innerStates[i]);
                            }
                        }
                    }
                        return;
                    // Invalid <= PartialArray
                    case VarState::TAG_PartialArray: {
                        const auto& nse = newState.as_PartialArray();
                        // Expand the scalar into a matching sparse state, then merge element-wise.
                        {
                            ::std::map<unsigned, VarState> other;
                            for (const auto& kv : nse.otherStates) {
                                other.insert(::std::make_pair(kv.first, oldState.clone()));
                            }
                            oldState = VarState::make_PartialArray({box$(oldState.clone()), mv$(other), nse.count});
                        }
                        auto& ose = oldState.as_PartialArray();
                        // NOTE: The fill covers a uniform run, so a single merged state (and any
                        // drop flag it allocates) stands for every untouched element.
                        mergeState(sp, builder, MIRLValue::newField(lv.clone(), 0), *ose.fillState, *nse.fillState);
                        for (auto& kv : ose.otherStates) {
                            mergeState(sp, builder, MIRLValue::newField(lv.clone(), kv.first), kv.second, nse.otherStates.at(kv.first));
                        }
                        return;
                    }
                }
                break;
            // Valid <= ...
            case VarState::TAG_Valid:
                switch (newState.tag()) {
                    // Valid <= Invalid
                    case VarState::TAG_Invalid:
                        oldState = VarState::make_Optional(builder.newDropFlagAndSet(sp, false));
                        return;
                    // Valid <= Valid
                    case VarState::TAG_Valid:
                        return;
                    // Valid <= Optional
                    case VarState::TAG_Optional: {
                        auto flagIdx = newState.as_Optional();
                        // Was valid, now optional.
                        if (builder.getDropFlagDefault(sp, flagIdx) != true) {
                            // Allocate a new drop flag with a default state of `true` and set it to this flag?
                            auto newFlag = builder.newDropFlag(true);
                            builder.pushStmtSetDropflagOther(sp, newFlag, flagIdx);
                            oldState = VarState::make_Optional(newFlag);
                        } else {
                            oldState = VarState::make_Optional(newState.as_Optional());
                        }
                        return;
                    }
                    // Valid <= MovedOut
                    case VarState::TAG_MovedOut: {
                        const auto& nse = newState.as_MovedOut();

                        // Create a new state that is internally valid and uses the same drop flag
                        oldState = VarState::make_MovedOut({box$(VarState::make_Valid({})), nse.outerFlag});
                        auto& ose = oldState.as_MovedOut();
                        if (ose.outerFlag != ~0u) {
                            // If the flag's default isn't true, then create a new flag that does have such a default
                            // - Other arm (old_state) uses default, this arm (new_state) can be manipulated
                            if (builder.getDropFlagDefault(sp, ose.outerFlag) != true) {
                                auto newFlag = builder.newDropFlag(true);
                                builder.pushStmtSetDropflagOther(sp, newFlag, nse.outerFlag);
                                ose.outerFlag = newFlag;
                            }
                        } else {
                            // In both arms, the container is valid. No need for a drop flag
                        }

                        const bool isBox = builder.isTypeOwnedBox(builder.valType(sp, lv));

                        if (isBox) {
                            mergeState(sp, builder, MIRLValue::newDeref(lv.clone()), *ose.innerState, *nse.innerState);
                        } else {
                            BUG(sp, "MovedOut on non-Box");
                        }
                        return;
                    }
                    // Valid <= Partial
                    case VarState::TAG_Partial: {
                        const auto& nse = newState.as_Partial();
                        const auto* lvTy = builder.valType(sp, lv);
                        const bool is_enum = lvTy->is_Path() && lvTy->as_Path().binding.is_Enum();
                        unsigned int outerFlag = ~0u;
                        if (is_enum && nse.outerFlag != ~0u) {
                            mergeOuterValidity(sp, builder, outerFlag, nse.outerFlag);
                        }

                        // Create a partial filled with Valid
                        {
                            ::std::vector<VarState> inner;
                            inner.reserve(nse.innerStates.size());
                            for (size_t i = 0; i < nse.innerStates.size(); i++) {
                                inner.push_back(VarState::make_Valid({}));
                            }
                            oldState = VarState::make_Partial({mv$(inner), outerFlag});
                        }
                        auto& ose = oldState.as_Partial();
                        if (is_enum) {
                            auto ilv = MIRLValue::newDowncast(lv.clone(), 0);
                            for (size_t i = 0; i < ose.innerStates.size(); i++) {
                                mergeState(sp, builder, ilv, ose.innerStates[i], nse.innerStates[i]);
                                ilv.incDowncast();
                            }
                        } else {
                            auto ilv = MIRLValue::newField(lv.clone(), 0);
                            for (unsigned int i = 0; i < ose.innerStates.size(); i++) {
                                mergeState(sp, builder, ilv, ose.innerStates[i], nse.innerStates[i]);
                                ilv.incField();
                            }
                        }
                    }
                        return;
                    // Valid <= PartialArray
                    case VarState::TAG_PartialArray: {
                        const auto& nse = newState.as_PartialArray();
                        {
                            ::std::map<unsigned, VarState> other;
                            for (const auto& kv : nse.otherStates) {
                                other.insert(::std::make_pair(kv.first, VarState::make_Valid({})));
                            }
                            oldState = VarState::make_PartialArray({box$(VarState::make_Valid({})), mv$(other), nse.count});
                        }
                        auto& ose = oldState.as_PartialArray();
                        mergeState(sp, builder, MIRLValue::newField(lv.clone(), 0), *ose.fillState, *nse.fillState);
                        for (auto& kv : ose.otherStates) {
                            mergeState(sp, builder, MIRLValue::newField(lv.clone(), kv.first), kv.second, nse.otherStates.at(kv.first));
                        }
                        return;
                    }
                }
                break;
            // Optional <= ...
            case VarState::TAG_Optional:
                switch (newState.tag()) {
                    case VarState::TAG_Invalid:
                        builder.pushStmtSetDropflagVal(sp, oldState.as_Optional(), false);
                        return;
                    case VarState::TAG_Valid:
                        builder.pushStmtSetDropflagVal(sp, oldState.as_Optional(), true);
                        return;
                    case VarState::TAG_Optional:
                        if (oldState.as_Optional() != newState.as_Optional()) {
                            builder.pushStmtSetDropflagOther(sp, oldState.as_Optional(), newState.as_Optional());
                        }
                        return;
                    case VarState::TAG_MovedOut: {
                        // Should become `MovedOut` with a flag
                        // - If this `MovedOut` has a flag, then propagate that into the `Optional`'s flag and reset
                        if (newState.as_MovedOut().outerFlag != ~0u) {
                            if (oldState.as_Optional() != newState.as_MovedOut().outerFlag) {
                                builder.pushStmtSetDropflagOther(sp, oldState.as_Optional(), newState.as_MovedOut().outerFlag);
                            }
                        }
                        // Create an old state that just wraps a copy of the `Optional`
                        oldState = VarState::make_MovedOut({std::make_unique<VarState>(oldState.clone()), oldState.as_Optional()});

                        const bool isBox = builder.isTypeOwnedBox(builder.valType(sp, lv));

                        if (isBox) {
                            mergeState(sp, builder, MIRLValue::newDeref(lv.clone()), *oldState.as_MovedOut().innerState, *newState.as_MovedOut().innerState);
                        } else {
                            BUG(sp, "MovedOut on non-Box");
                        }
                        return;
                    }
                    case VarState::TAG_Partial: {
                        const auto& nse = newState.as_Partial();
                        const auto* lvTy = builder.valType(sp, lv);
                        assert(!builder.isTypeOwnedBox(lvTy));
                        const bool is_enum = lvTy->is_Path() && lvTy->as_Path().binding.is_Enum();
                        const auto oldOptionalFlag = oldState.as_Optional();

                        // Create a Partial filled with copies of the Optional
                        // TODO: This can lead to contradictions when one field is moved and another not.
                        // - Need to allocate a new drop flag and handle the case where old_state is the state before the
                        //   split (and hence the default state of this new drop flag has to be the original state)
                        //  > Could store reference to start BB and assign into it?
                        //  > Can't it not be from before the split, because that would be a move when not known-valid?
                        //  > Re-assign and partial drop.
                        {
                            ::std::vector<VarState> inner;
                            inner.reserve(nse.innerStates.size());
                            for (size_t i = 0; i < nse.innerStates.size(); i++) {
                                auto newFlag = builder.newDropFlag(builder.getDropFlagDefault(sp, oldState.as_Optional()));
                                builder.dropFlagAlias(oldState.as_Optional(), newFlag);
                                inner.push_back(VarState::make_Optional(newFlag));
                            }
                            oldState = VarState::make_Partial({mv$(inner), is_enum ? oldOptionalFlag : ~0u});
                        }
                        auto& ose = oldState.as_Partial();
                        if (is_enum) {
                            if (nse.outerFlag == ~0u) {
                                mergeOuterValidity(sp, builder, ose.outerFlag, true);
                            } else {
                                mergeOuterValidity(sp, builder, ose.outerFlag, nse.outerFlag);
                            }
                        }
                        // Propagate to inners
                        if (is_enum) {
                            for (size_t i = 0; i < ose.innerStates.size(); i++) {
                                mergeState(sp, builder, MIRLValue::newDowncast(lv.clone(), static_cast<unsigned int>(i)), ose.innerStates[i], nse.innerStates[i]);
                            }
                        } else {
                            for (unsigned int i = 0; i < ose.innerStates.size(); i++) {
                                mergeState(sp, builder, MIRLValue::newField(lv.clone(), i), ose.innerStates[i], nse.innerStates[i]);
                            }
                        }
                        return;
                    }
                    // Optional <= PartialArray
                    case VarState::TAG_PartialArray: {
                        const auto& nse = newState.as_PartialArray();
                        const auto oldOptionalFlag = oldState.as_Optional();
                        // Expand into a sparse state whose entries each get their own flag
                        // aliased to the original one (same trick as Optional <= Partial).
                        const auto newAliasFlag = [&]() {
                            auto flag = builder.newDropFlag(builder.getDropFlagDefault(sp, oldOptionalFlag));
                            builder.dropFlagAlias(oldOptionalFlag, flag);
                            return flag;
                        };
                        {
                            ::std::map<unsigned, VarState> other;
                            for (const auto& kv : nse.otherStates) {
                                other.insert(::std::make_pair(kv.first, VarState::make_Optional(newAliasFlag())));
                            }
                            oldState = VarState::make_PartialArray({box$(VarState::make_Optional(newAliasFlag())), mv$(other), nse.count});
                        }
                        auto& ose = oldState.as_PartialArray();
                        mergeState(sp, builder, MIRLValue::newField(lv.clone(), 0), *ose.fillState, *nse.fillState);
                        for (auto& kv : ose.otherStates) {
                            mergeState(sp, builder, MIRLValue::newField(lv.clone(), kv.first), kv.second, nse.otherStates.at(kv.first));
                        }
                        return;
                    }
                }
                break;
            case VarState::TAG_MovedOut: {
                auto& ose = oldState.as_MovedOut();
                const bool isBox = builder.isTypeOwnedBox(builder.valType(sp, lv));
                if (!isBox) {
                    BUG(sp, "MovedOut on non-Box");
                }
                switch (newState.tag()) {
                    case VarState::TAG_Invalid:
                    case VarState::TAG_Valid: {
                        bool isValid = newState.is_Valid();
                        if (ose.outerFlag == ~0u) {
                            // If not valid in new arm, then the outer state is conditional
                            if (!isValid) {
                                ose.outerFlag = builder.newDropFlag(true);
                                builder.pushStmtSetDropflagVal(sp, ose.outerFlag, false);
                            }
                        } else {
                            builder.pushStmtSetDropflagVal(sp, ose.outerFlag, isValid);
                        }

                        mergeState(sp, builder, MIRLValue::newDeref(lv.clone()), *ose.innerState, newState);
                        return;
                    }
                    case VarState::TAG_Optional: {
                        const auto& nse = newState.as_Optional();
                        if (ose.outerFlag == ~0u) {
                            if (!builder.getDropFlagDefault(sp, nse)) {
                                // Default wasn't true, need to make a new flag that does have a default of true
                                auto newFlag = builder.newDropFlag(true);
                                builder.pushStmtSetDropflagOther(sp, newFlag, nse);
                                ose.outerFlag = newFlag;
                            } else {
                                ose.outerFlag = nse;
                            }
                        } else {
                            // In this arm, assign the outer state to this drop flag
                            builder.pushStmtSetDropflagOther(sp, ose.outerFlag, nse);
                        }
                        mergeState(sp, builder, MIRLValue::newDeref(lv.clone()), *ose.innerState, newState);
                        return;
                    }
                    case VarState::TAG_MovedOut: {
                        const auto& nse = newState.as_MovedOut();

                        if (ose.outerFlag == ~0u) {
                            ose.outerFlag = nse.outerFlag;
                        } else {
                            builder.pushStmtSetDropflagOther(sp, ose.outerFlag, nse.outerFlag);
                        }
                        mergeState(sp, builder, MIRLValue::newDeref(lv.clone()), *ose.innerState, *nse.innerState);
                        return;
                    }
                    case VarState::TAG_Partial:
                    case VarState::TAG_PartialArray:
                        BUG(sp, "MovedOut->Partial not valid");
                }
                break;
            }
            case VarState::TAG_Partial: {
                auto& ose = oldState.as_Partial();
                const auto* lvTy = builder.valType(sp, lv);
                assert(!builder.isTypeOwnedBox(lvTy));
                const bool is_enum = lvTy->is_Path() && lvTy->as_Path().binding.is_Enum();
                // Need to tag for conditional shallow drop? Or just do that at the end of the split?
                // - End of the split means that the only optional state is outer drop.
                switch (newState.tag()) {
                    case VarState::TAG_Invalid:
                    case VarState::TAG_Valid:
                    case VarState::TAG_Optional:
                        if (is_enum) {
                            if (newState.is_Invalid()) {
                                mergeOuterValidity(sp, builder, ose.outerFlag, false);
                            } else if (newState.is_Valid()) {
                                mergeOuterValidity(sp, builder, ose.outerFlag, true);
                            } else {
                                mergeOuterValidity(sp, builder, ose.outerFlag, newState.as_Optional());
                            }
                            for (size_t i = 0; i < ose.innerStates.size(); i++) {
                                mergeState(sp, builder, MIRLValue::newDowncast(lv.clone(), static_cast<unsigned int>(i)), ose.innerStates[i], newState);
                            }
                        } else {
                            for (unsigned int i = 0; i < ose.innerStates.size(); i++) {
                                mergeState(sp, builder, MIRLValue::newField(lv.clone(), i), ose.innerStates[i], newState);
                            }
                        }
                        return;
                    case VarState::TAG_MovedOut:
                        BUG(sp, "Partial->MovedOut not valid");
                    case VarState::TAG_PartialArray:
                        BUG(sp, "Partial->PartialArray not valid (threshold mismatch)");
                    case VarState::TAG_Partial: {
                        const auto& nse = newState.as_Partial();
                        ASSERT_BUG(sp, ose.innerStates.size() == nse.innerStates.size(), "Partial->Partial with mismatched sizes - " << oldState << " <= " << newState);
                        if (is_enum) {
                            if (nse.outerFlag == ~0u) {
                                mergeOuterValidity(sp, builder, ose.outerFlag, true);
                            } else {
                                mergeOuterValidity(sp, builder, ose.outerFlag, nse.outerFlag);
                            }
                            for (size_t i = 0; i < ose.innerStates.size(); i++) {
                                mergeState(sp, builder, MIRLValue::newDowncast(lv.clone(), static_cast<unsigned int>(i)), ose.innerStates[i], nse.innerStates[i]);
                            }
                        } else {
                            for (unsigned int i = 0; i < ose.innerStates.size(); i++) {
                                mergeState(sp, builder, MIRLValue::newField(lv.clone(), i), ose.innerStates[i], nse.innerStates[i]);
                            }
                        }
                    }
                        return;
                }
            } break;
            case VarState::TAG_PartialArray: {
                auto& ose = oldState.as_PartialArray();
                switch (newState.tag()) {
                    // PartialArray <= scalar: fold the scalar into the fill and every exception
                    case VarState::TAG_Invalid:
                    case VarState::TAG_Valid:
                    case VarState::TAG_Optional:
                        mergeState(sp, builder, MIRLValue::newField(lv.clone(), 0), *ose.fillState, newState);
                        for (auto& kv : ose.otherStates) {
                            mergeState(sp, builder, MIRLValue::newField(lv.clone(), kv.first), kv.second, newState);
                        }
                        return;
                    case VarState::TAG_MovedOut:
                        BUG(sp, "PartialArray->MovedOut not valid");
                    case VarState::TAG_Partial:
                        BUG(sp, "PartialArray->Partial not valid (threshold mismatch)");
                    case VarState::TAG_PartialArray: {
                        const auto& nse = newState.as_PartialArray();
                        ASSERT_BUG(sp, ose.count == nse.count, "PartialArray size mismatch - " << oldState << " <= " << newState);
                        // Materialise entries that only the new state singles out, using the
                        // pre-merge fill as their effective old state.
                        for (const auto& kv : nse.otherStates) {
                            if (ose.otherStates.find(kv.first) == ose.otherStates.end()) {
                                ose.otherStates.insert(::std::make_pair(kv.first, ose.fillState->clone()));
                            }
                        }
                        mergeState(sp, builder, MIRLValue::newField(lv.clone(), 0), *ose.fillState, *nse.fillState);
                        for (auto& kv : ose.otherStates) {
                            const auto it = nse.otherStates.find(kv.first);
                            const VarState& newEff = it != nse.otherStates.end() ? it->second : *nse.fillState;
                            mergeState(sp, builder, MIRLValue::newField(lv.clone(), kv.first), kv.second, newEff);
                        }
                        return;
                    }
                }
            } break;
        }
        BUG(sp, "Unhandled combination - " << oldState.tagStr() << " and " << newState.tagStr());
    }
}

void MirBuilder::terminateLoopEarly(const Span& sp, ScopeType::Data_Loop& sdLoop) {
    if (sdLoop.exitStateValid) {
        // Insert copies of parent state for newly changed values
        // and Merge all changed values
        auto mergeList = [sp, this](const auto& changed, auto& exitStates, auto valCb, auto type) {
            for (const auto& ent : changed) {
                auto idx = ent.first;
                auto it = exitStates.find(idx);
                if (it == exitStates.end()) {
                    it = exitStates.insert(::std::make_pair(idx, ent.second.clone())).first;
                }
                auto& oldState = it->second;
                mergeState(sp, *this, valCb(idx), oldState, getSlotState(sp, idx, type));
            }
        };
        mergeList(sdLoop.changedSlots, sdLoop.exitState.states, MIRLValue::newLocal, SlotType::Local);
        mergeList(sdLoop.changedArgs, sdLoop.exitState.argStates, [](auto v) {
            return MIRLValue::newArgument(v);
        }, SlotType::Argument);
    } else {
        auto initList = [sp, this](const auto& changed, auto& exitStates, auto type) {
            for (const auto& ent : changed) {
                DEBUG("Slot(" << ent.first << ") = " << ent.second);
                auto idx = ent.first;
                exitStates.insert(::std::make_pair(idx, getSlotState(sp, idx, type).clone()));
            }
        };
        // Obtain states of changed variables/temporaries
        initList(sdLoop.changedSlots, sdLoop.exitState.states, SlotType::Local);
        initList(sdLoop.changedArgs, sdLoop.exitState.argStates, SlotType::Argument);
        sdLoop.exitStateValid = true;
    }
}

void MirBuilder::mergeSplitLists(const Span& sp, const ScopeHandle& handle, const ::std::map<unsigned int, VarState>& states, ::std::map<unsigned int, VarState>& endStates, MirBuilder::SlotType type) {
    // Insert copies of the parent state
    for (const auto& ent : states) {
        if (endStates.count(ent.first) == 0) {
            auto s = this->getSlotState(sp, ent.first, type, &handle).clone();
            DEBUG("Add from parent: " << (type == SlotType::Local ? MIRLValue::newLocal(ent.first) : MIRLValue::newArgument(ent.first)) << " = " << s);
            endStates.insert(::std::make_pair(ent.first, std::move(s)));
        }
    }
    // Merge state
    for (auto& ent : endStates) {
        auto idx = ent.first;
        auto& outState = ent.second;

        // Merge the states
        auto it = states.find(idx);
        const auto& srcState = (it != states.end() ? it->second : this->getSlotState(sp, idx, type, &handle));

        auto lv = (type == SlotType::Local ? MIRLValue::newLocal(idx) : MIRLValue::newArgument(idx));
        mergeState(sp, *this, mv$(lv), outState, srcState);
    }
}

void MirBuilder::endSplitArm(const Span& sp, const ScopeHandle& handle, bool reachable, bool early /*=false*/) {
    ASSERT_BUG(sp, handle.idx < scopes.size(), "Handle passed to end_split_arm is invalid");
    auto& sd = scopes.at(handle.idx);
    ASSERT_BUG(sp, sd.data.is_Split(), "Ending split arm on non-Split arm - " << sd.data.tagStr());
    auto& sdSplit = sd.data.as_Split();
    ASSERT_BUG(sp, !sdSplit.arms.empty(), "Split arm list is empty (impossible)");

    // If this is not at the top of the stack (if there are other splits in the way), then get state from them
    for (auto v : ::reverse(scopeStack)) {
        if (v == handle.idx) {
            break;
        }

        // If this stack entry is a Split, get the current values and add them to `sd_split`
        if (const auto* otherSplit = scopes.at(v).data.opt_Split()) {
            for (auto& s : otherSplit->arms.back().states) {
                DEBUG("In scope " << handle.idx << " _" << s.first << " = " << s.second << " (from scope " << v << ")");
                sdSplit.arms.back().states[s.first] = s.second.clone();
            }
            for (auto& s : otherSplit->arms.back().argStates) {
                DEBUG("In scope " << handle.idx << " a" << s.first << " = " << s.second << " (from scope " << v << ")");
                sdSplit.arms.back().argStates[s.first] = s.second.clone();
            }
        }
    }

    TRACE_FUNCTION_F("end split scope " << handle.idx << " arm " << (sdSplit.arms.size() - 1) << (reachable ? " reachable" : "") << (early ? " early" : ""));
    if (reachable) {
        ASSERT_BUG(sp, blockActive_, "Block must be active when ending a reachable split arm");
    }

    auto& thisArmState = sdSplit.arms.back();
    thisArmState.alwaysEarlyTerminated = /*sd_split.arms.back().has_early_terminated &&*/ !reachable;

    if (sdSplit.endStateValid) {
        if (reachable) {
            DEBUG("Reachable w/ end state, merging");

            mergeSplitLists(sp, handle, thisArmState.states, sdSplit.endState.states, SlotType::Local);
            mergeSplitLists(sp, handle, thisArmState.argStates, sdSplit.endState.argStates, SlotType::Argument);
        } else {
            DEBUG("Unreachable, not merging");
        }
    } else {
        if (reachable) {
            DEBUG("Reachable w/ no end state, setting");
            // Clone this arm's state
            for (auto& ent : thisArmState.states) {
                DEBUG("State _" << ent.first << " = " << ent.second);
                sdSplit.endState.states.insert(::std::make_pair(ent.first, ent.second.clone()));
            }
            for (auto& ent : thisArmState.argStates) {
                DEBUG("State a" << ent.first << " = " << ent.second);
                sdSplit.endState.argStates.insert(::std::make_pair(ent.first, ent.second.clone()));
            }
            sdSplit.endStateValid = true;
        } else {
            DEBUG("Unreachable, not setting");
        }
    }

    if (reachable) {
        assert(blockActive_);
    }
    if (!early) {
        SplitArm arm;
        DEBUG("New Arm");
        for (auto& ent : sdSplit.condState.states) {
            DEBUG("Condition State _" << ent.first << " = " << ent.second);
            arm.states.insert(::std::make_pair(ent.first, ent.second.clone()));
        }
        for (auto& ent : sdSplit.condState.argStates) {
            DEBUG("Condition State a" << ent.first << " = " << ent.second);
            arm.argStates.insert(::std::make_pair(ent.first, ent.second.clone()));
        }
        sdSplit.arms.push_back(mv$(arm));
    }
}

void MirBuilder::endSplitArmEarly(const Span& sp) {
    TRACE_FUNCTION_F("");
    size_t i = scopeStack.size();
    // Terminate every sequence of owning scopes
    while (i-- && scopes.at(scopeStack[i]).data.is_Owning()) {
        auto& scopeDef = scopes[scopeStack[i]];
        // Fully drop the scope
        DEBUG("Complete scope " << scopeStack[i]);
        dropScopeValues(scopeDef);
        completeScope(scopeDef);
    }

    if (i < scopeStack.size()) {
        if (scopes.at(scopeStack[i]).data.is_Split()) {
            DEBUG("Early terminate split scope " << scopeStack.back());
            auto& sd = scopes[scopeStack[i]];
            auto& sdSplit = sd.data.as_Split();
            sdSplit.arms.back().hasEarlyTerminated = true;

            // TODO: Create drop flags if required?
        }
        // TODO: What if this is a loop?
    }
}

void MirBuilder::endSplitCondition(const Span& sp, const ScopeHandle& condition, const ScopeHandle& outer) {
    ASSERT_BUG(sp, condition.idx < scopes.size(), "Condition handle passed to end_split_condition is invalid");
    ASSERT_BUG(sp, outer.idx < scopes.size(), "Outer handle passed to end_split_condition is invalid");
    auto& conditionDef = scopes.at(condition.idx);
    auto& outerDef = scopes.at(outer.idx);
    ASSERT_BUG(sp, conditionDef.data.is_Split(), "Condition scope is not Split - " << conditionDef.data.tagStr());
    ASSERT_BUG(sp, outerDef.data.is_Split(), "Outer scope is not Split - " << outerDef.data.tagStr());
    const auto& conditionSplit = conditionDef.data.as_Split();
    auto& outerSplit = outerDef.data.as_Split();
    ASSERT_BUG(sp, conditionSplit.endStateValid, "Condition split has no reachable exit");

    DEBUG("Split condition clause end (scope " << condition.idx << " => " << outer.idx << ")");

    // The condition end state already merges pattern failure with every guard
    // failure seen so far.  It is the sequential state for the next arm, not
    // another alternative to merge with the previous condition state.
    for (const auto& ent : conditionSplit.endState.states) {
        outerSplit.condState.states[ent.first] = ent.second.clone();
    }
    for (const auto& ent : conditionSplit.endState.argStates) {
        outerSplit.condState.argStates[ent.first] = ent.second.clone();
    }
}

void MirBuilder::unfreezeScope(const Span& sp, const ScopeHandle& handle) {
    ASSERT_BUG(sp, handle.idx < scopes.size(), "Handle passed to `unfreeze_scope` is invalid");
    auto& sd = scopes.at(handle.idx);
    ASSERT_BUG(sp, sd.data.is_Freeze(), "Handle passed to `unfreeze_scope` was not a freeze,  - " << sd.data.tagStr());
    auto& sdE = sd.data.as_Freeze();

    DEBUG("Unfreeze scope " << handle.idx);
    sdE.unfrozen = true;
}

void MirBuilder::completeScope(ScopeDef& sd) {
    struct H {
        static void applyEndState(const Span& sp, MirBuilder& builder, SplitEnd& endState) {
            for (auto& ent : endState.states) {
                auto& vs = builder.getSlotStateMut(sp, ent.first, SlotType::Local);
                if (vs != ent.second) {
                    DEBUG(MIRLValue::newLocal(ent.first) << " " << vs << " => " << ent.second);
                    vs = ::std::move(ent.second);
                }
            }
            for (auto& ent : endState.argStates) {
                auto& vs = builder.getSlotStateMut(sp, ent.first, SlotType::Argument);
                if (vs != ent.second) {
                    DEBUG(MIRLValue::newArgument(ent.first) << " " << vs << " => " << ent.second);
                    vs = ::std::move(ent.second);
                }
            }
        }
    };

    sd.complete = true;

    switch (sd.data.tag()) {
        case ScopeType::TAG_Owning: {
            break;
        }
        case ScopeType::TAG_Freeze: {
            break;
        }
        case ScopeType::TAG_Loop: {
            auto& e = sd.data.as_Loop();
            TRACE_FUNCTION_F("Loop");
            if (e.exitStateValid) {
                H::applyEndState(sd.span, *this, e.exitState);
            }

            // Insert sets of drop flags to the first block (at the start of that block)
            auto& stmts = output.blocks.at(e.entryBb).statements;
            for (auto idx : e.dropFlags) {
                DEBUG("Reset df$" << idx);
                stmts.insert(stmts.begin(), MIRStatement::make_SetDropFlag({idx, output.dropFlags.at(idx), ~0u}));
            }
            break;
        }
        case ScopeType::TAG_Split: {
            auto& e = sd.data.as_Split();
            TRACE_FUNCTION_F("Split - " << (e.arms.size() - 1) << " arms");

            // TODO: if not set, then end the current state as unreachable?
            if (e.endStateValid) {
                H::applyEndState(sd.span, *this, e.endState);
            }
            break;
        }
    }
}

HIRTypeRef MirBuilder::valType(const Span& sp, const MIRLValue& val, const MIRLValue::Wrapper* stopWrapper /*=nullptr*/) const {
    HIRTypeRef tmp;
    const HIRTypeData* ty = nullptr;
    auto revealType = [&](const HIRTypeData* input) {
        HIRTypeRef revealed = input;
        resolve_.revealOpaqueTypes(sp, revealed);
        return revealed;
    };
    switch (val.root.tag()) {
        case MIRLValue::Storage::TAG_Return: {
            ty = retTy;
            break;
        }
        case MIRLValue::Storage::TAG_Argument: {
            decltype(val.root.as_Argument()) e = val.root.as_Argument();
            ty = args_.at(e).second;
            break;
        }
        case MIRLValue::Storage::TAG_Local: {
            decltype(val.root.as_Local()) e = val.root.as_Local();
            ty = output.locals.at(e);
            break;
        }
        case MIRLValue::Storage::TAG_Static: {
            decltype(val.root.as_Static()) e = val.root.as_Static();
            switch (e.data.tag()) {
                case HIRPathData::TAG_Generic: {
                    auto& pe = e.data.as_Generic();
                    ASSERT_BUG(sp, pe.params.types.empty(), "Path params on static"); const auto& s = resolve_.hirCrate().getStaticByPath(sp, pe.path); ty = s.type;
                    break;
                }
                case HIRPathData::TAG_UfcsKnown: {
                    TODO(sp, "Static - UfcsKnown - " << e);
                    break;
                }
                case HIRPathData::TAG_UfcsUnknown: {
                    BUG(sp, "Encountered UfcsUnknown in Static - " << e);
                    break;
                }
                case HIRPathData::TAG_UfcsInherent: {
                    TODO(sp, "Static - UfcsInherent - " << e);
                    break;
                }
            }
            break;
        }
    }
    assert(ty);
    for (const auto& w : val.wrappers) {
        if (&w == stopWrapper) {
            stopWrapper = nullptr; // Reset so the below bugcheck can work
            break;
        }
        const auto* currentTy = revealType(ty);
        ty = nullptr;
        auto maybeMonomorph = [&](const HIRGenericParams& paramsDef, const HIRPath& p, const HIRTypeData* t) -> const HIRTypeData* {
            if (monomorphiseTypeNeeded(t)) {
                tmp = MonomorphStatePtr(resolve_.hirCrate().types, nullptr, &p.data.as_Generic().params, nullptr).monomorphType(sp, t);
                resolve_.expandAssociatedTypes(sp, tmp);
                return tmp;
            } else {
                return t;
            }
        };
        switch (w.tag()) {
            case MIRLValue::Wrapper::TAG_Field: {
                decltype(w.as_Field()) fieldIndex = w.as_Field();
                switch ((*currentTy).tag()) {
default:
                    BUG(sp, "Field access on unexpected type - " << currentTy);
                    case HIRTypeData::TAG_Array: {
                        auto& te = (*currentTy).as_Array();
                        ty = te.inner;
                        break;
                    }
                    case HIRTypeData::TAG_Slice: {
                        auto& te = (*currentTy).as_Slice();
                        ty = te.inner;
                        break;
                    }
                    case HIRTypeData::TAG_Path: {
                        auto& te = (*currentTy).as_Path();
                        if (const auto* tep = te.binding.opt_Struct()) {
                            const auto& str = **tep;
                            switch (str.data.tag()) {
                                case HIRStructData::TAG_Unit: {
                                    BUG(sp, "Field on unit-like struct - " << currentTy);
                                    break;
                                }
                                case HIRStructData::TAG_Tuple: {
                                    auto& se = str.data.as_Tuple();
                                    ASSERT_BUG(sp, fieldIndex < se.size(), "Field index out of range in tuple-struct " << currentTy << " - " << fieldIndex << " > " << se.size()); const auto& fld = se[fieldIndex]; ty = maybeMonomorph(str.params, te.path, fld.ent);
                                    break;
                                }
                                case HIRStructData::TAG_Named: {
                                    auto& se = str.data.as_Named();
                                    ASSERT_BUG(sp, fieldIndex < se.size(), "Field index out of range in struct " << currentTy << " - " << fieldIndex << " > " << se.size()); const auto& fld = se[fieldIndex]; ty = maybeMonomorph(str.params, te.path, fld.ty);
                                    break;
                                }
                            }
                        } else if (/*const auto* tep =*/te.binding.opt_Union()) {
                            BUG(sp, "Field access on a union isn't valid, use Downcast instead - " << currentTy);
                        } else {
                            BUG(sp, "Field acess on unexpected type - " << currentTy);
                        }
                        break;
                    }
                    case HIRTypeData::TAG_Tuple: {
                        auto& te = (*currentTy).as_Tuple();
                        ASSERT_BUG(sp, fieldIndex < te.size(), "Field index out of range in tuple " << fieldIndex << " >= " << te.size());
                        ty = te[fieldIndex];
                        break;
                    }
                }
                break;
            }
            case MIRLValue::Wrapper::TAG_Deref: {
                switch ((*currentTy).tag()) {
default:
                    BUG(sp, "Deref on unexpected type - " << currentTy);
                    case HIRTypeData::TAG_Path: {
                        if (const auto* inner = this->isTypeOwnedBox(currentTy)) {
                            ty = inner;
                        } else {
                            BUG(sp, "Deref on unexpected type - " << currentTy);
                        }
                        break;
                    }
                    case HIRTypeData::TAG_Pointer: {
                        auto& te = (*currentTy).as_Pointer();
                        ty = te.inner;
                        break;
                    }
                    case HIRTypeData::TAG_Borrow: {
                        auto& te = (*currentTy).as_Borrow();
                        ty = te.inner;
                        break;
                    }
                }
                break;
            }
            case MIRLValue::Wrapper::TAG_Index: {
                switch ((*currentTy).tag()) {
                    case HIRTypeData::TAG_Slice: {
                        auto& te = (*currentTy).as_Slice();
                        ty = te.inner;
                        break;
                    }
                    case HIRTypeData::TAG_Array: {
                        auto& te = (*currentTy).as_Array();
                        ty = te.inner;
                        break;
                    }
                    default: {
                        BUG(sp, "Index on unexpected type - " << currentTy);
                        break;
                    }
                }
                break;
            }
            case MIRLValue::Wrapper::TAG_Downcast: {
                decltype(w.as_Downcast()) variantIndex = w.as_Downcast();
                switch ((*currentTy).tag()) {
default:
                    BUG(sp, "Downcast on unexpected type - " << currentTy);
                    case HIRTypeData::TAG_Path: {
                        auto& te = (*currentTy).as_Path();
                        if (const auto* pbe = te.binding.opt_Enum()) {
                            const auto& enm = **pbe;
                            ASSERT_BUG(sp, enm.data.is_Data(), "Downcast on non-data enum");
                            const auto& variants = enm.data.as_Data();
                            ASSERT_BUG(sp, variantIndex < variants.size(), "Variant index out of range");
                            const auto& variant = variants[variantIndex];

                            ty = maybeMonomorph(enm.params, te.path, variant.type);
                        } else if (const auto* pbe = te.binding.opt_Union()) {
                            const auto& unm = **pbe;
                            ASSERT_BUG(sp, variantIndex < unm.variants.size(), "Variant index out of range");
                            const auto& variant = unm.variants.at(variantIndex);

                            ty = maybeMonomorph(unm.params, te.path, variant.ty);
                        } else {
                            BUG(sp, "Downcast on non-Enum/Union - " << currentTy << " for " << val);
                        }
                        break;
                    }
                }
                break;
            }
        }
        assert(ty);
    }
    ASSERT_BUG(sp, !stopWrapper, "A stop wrapper was passed, but not found");
    return revealType(ty);
}

bool MirBuilder::lvalueIsCopy(const Span& sp, const MIRLValue& val) const {
    const auto* ty = valType(sp, val);
    DEBUG("[lvalue_is_copy] ty=" << ty);
    return resolve_.typeIsCopy(sp, ty);
}

const VarState& MirBuilder::getSlotState(const Span& sp, unsigned int idx, SlotType type, const ScopeHandle* aboveScope /*=nullptr*/) const {
    if (frozenExitStateActive && !aboveScope) {
        const auto& states = type == SlotType::Local ? frozenExitSlotStates : frozenExitArgStates;
        auto it = states.find(idx);
        if (it != states.end()) {
            return it->second;
        }
    }

    // 1. Find an applicable Split scope
    for (auto scopeIdx : ::reverse(scopeStack)) {
        // Is this supposed to only consider above a specified (likely split) scope?
        if (aboveScope) {
            // Once the scope is found, clear `above_scope` so subsequent iterations skip this check
            if (scopeIdx == aboveScope->idx) {
                aboveScope = nullptr;
            }
            continue;
        }
        const auto& scopeDef = scopes.at(scopeIdx);
        switch (scopeDef.data.tag()) {
default:
            break;
            case ScopeType::TAG_Owning: {
                auto& e = scopeDef.data.as_Owning();
                if (type == SlotType::Local) {
                    auto it = ::std::find(e.slots.begin(), e.slots.end(), idx);
                    if (it != e.slots.end()) {
                        // State from an outer split belongs to the outer
                        // incarnation of this local.  Once its owning scope
                        // is reached, fall back to the local's base state.
                        goto outOfLoop;
                    }
                }
                break;
            }
            case ScopeType::TAG_Split: {
                auto& e = scopeDef.data.as_Split();
                const auto& curArm = e.arms.back();
                const auto& list = (type == SlotType::Local ? curArm.states : curArm.argStates);
                auto it = list.find(idx);
                if (it != list.end()) {
                    DEBUG("From scope " << scopeIdx);
                    return it->second;
                }
                break;
            }
        }
    }

outOfLoop:
    if (aboveScope) {
        BUG(sp, "Scope " << *aboveScope << " not found on stack");
    }
    switch (type) {
        case SlotType::Local:
            if (idx == ~0u) {
                return returnState;
            } else {
                ASSERT_BUG(sp, idx < slotStates.size(), "Slot " << idx << " out of range for state table");
                return slotStates.at(idx);
            }
            break;
        case SlotType::Argument:
            ASSERT_BUG(sp, idx < argStates.size(), "Argument " << idx << " out of range for state table");
            return argStates.at(idx);
    }
    throw "";
}

VarState& MirBuilder::getSlotStateMut(const Span& sp, unsigned int idx, SlotType type) {
    if (frozenExitStateActive) {
        auto& states = type == SlotType::Local ? frozenExitSlotStates : frozenExitArgStates;
        auto it = states.find(idx);
        if (it == states.end()) {
            it = states.insert(::std::make_pair(idx, getSlotState(sp, idx, type).clone())).first;
        }
        return it->second;
    }

    VarState* ret = nullptr;
    for (auto scopeIdx : ::reverse(scopeStack)) {
        auto& scopeDef = scopes.at(scopeIdx);
        switch (scopeDef.data.tag()) {
            case ScopeType::TAG_Owning: {
                auto& e = scopeDef.data.as_Owning();
                if (type == SlotType::Local) // `Local` counts both variables and temporaries
                {
                    auto it = ::std::find(e.slots.begin(), e.slots.end(), idx);
                    if (it != e.slots.end()) {
                        goto outOfLoop; // `goto` to avoid issues with the loops in `TU_ARMA`
                    }
                }
                break;
            }
            case ScopeType::TAG_Split: {
                auto& e = scopeDef.data.as_Split();
                auto& curArm = e.arms.back();
                if (!ret) {
                    if (idx == ~0u) {
                    } else {
                        auto& states = (type == SlotType::Local ? curArm.states : curArm.argStates);
                        auto it = states.find(idx);
                        if (it == states.end()) {
                            DEBUG("Split new (scope " << scopeIdx << ")");
                            it = states.insert(::std::make_pair(idx, getSlotState(sp, idx, type).clone())).first;
                        } else {
                            DEBUG("Split existing (scope " << scopeIdx << ")");
                        }
                        ret = &it->second;
                    }
                }
                break;
            }
            case ScopeType::TAG_Loop: {
                auto& e = scopeDef.data.as_Loop();
                if (idx == ~0u) {
                } else {
                    auto& states = (type == SlotType::Local ? e.changedSlots : e.changedArgs);
                    if (states.count(idx) == 0) {
                        auto state = e.exitStateValid ? getSlotState(sp, idx, type).clone() : VarState::make_Valid({});
                        states.insert(::std::make_pair(idx, mv$(state)));
                    }
                }
                break;
            }
            case ScopeType::TAG_Freeze: {
                // Normal guard state is owned by the nested split/temporary
                // scopes.  Freeze only makes terminateScopeEarly use its
                // shadow state while saved guard code is still cloneable.
                break;
            }
        }
    }
    // Label used because we need to break out of the loop and the `TU_ARMA`/`TU_MATCH_HDRA`
outOfLoop:
    if (!ret) {
        // Not set by a split/loop scope
        switch (type) {
            case SlotType::Local:
                ret = (idx == ~0u) ? &returnState : &slotStates.at(idx);
                break;
            case SlotType::Argument:
                ret = &argStates.at(idx);
                break;
        }
    }
    assert(ret);
    return *ret;
}

VarState* MirBuilder::getValStateMutP(const Span& sp, const MIRLValue& lv, bool expectValid /*=false*/) {
    TRACE_FUNCTION_F(lv);
    VarState* vs = nullptr;
    switch (lv.root.tag()) {
        case MIRLValue::Storage::TAG_Return: {
            BUG(sp, "Move of return value"); vs = &getSlotStateMut(sp, ~0u, SlotType::Local);
            break;
        }
        case MIRLValue::Storage::TAG_Argument: {
            decltype(lv.root.as_Argument()) e = lv.root.as_Argument();
            vs = &getSlotStateMut(sp, e, SlotType::Argument);
            break;
        }
        case MIRLValue::Storage::TAG_Local: {
            decltype(lv.root.as_Local()) e = lv.root.as_Local();
            vs = &getSlotStateMut(sp, e, SlotType::Local);
            break;
        }
        case MIRLValue::Storage::TAG_Static: {
            return nullptr;
        }
    }
    assert(vs);

    if (expectValid && vs->is_Valid()) {
        return nullptr;
    }

    for (const auto& w : lv.wrappers) {
        auto& ivs = *vs;
        vs = nullptr;
        switch (w.tag()) {
            case MIRLValue::Wrapper::TAG_Field: {
                decltype(w.as_Field()) fieldIndex = w.as_Field();
                VarState tpl;
                switch (ivs.tag()) {
                    case VarState::TAG_Invalid: {
                        tpl = ivs.clone();
                        break;
                    }
                    case VarState::TAG_MovedOut: {
                        BUG(sp, "Field on value with MovedOut state - " << lv);
                        break;
                    }
                    case VarState::TAG_Partial: {
                        break;
                    }
                    case VarState::TAG_PartialArray: {
                        break;
                    }
                    case VarState::TAG_Optional: {
                        tpl = ivs.clone();
                        break;
                    }
                    case VarState::TAG_Valid: {
                        tpl = VarState::make_Valid({});
                        break;
                    }
                }
                if (!ivs.is_Partial() && !ivs.is_PartialArray()) {
                    size_t nFlds = 0;
                    bool isArray = false;
                    {
                        const auto* ty = valType(sp, lv, &w);
                        DEBUG("ty = " << ty);
                        if (const auto* e = ty->opt_Path()) {
                            ASSERT_BUG(sp, e->binding.is_Struct(), "");
                            const auto& str = *e->binding.as_Struct();
                            switch (str.data.tag()) {
                                case HIRStructData::TAG_Unit: {
                                    BUG(sp, "Field access of unit-like struct");
                                    break;
                                }
                                case HIRStructData::TAG_Tuple: {
                                    auto& se = str.data.as_Tuple();
                                    nFlds = se.size();
                                    break;
                                }
                                case HIRStructData::TAG_Named: {
                                    auto& se = str.data.as_Named();
                                    nFlds = se.size();
                                    break;
                                }
                            }
                        } else if (const auto* e = ty->opt_Tuple()) {
                            nFlds = e->size();
                        } else if (const auto* e = ty->opt_Array()) {
                            ASSERT_BUG(sp, e->size.is_Known(), "Array size not known");
                            nFlds = e->size.as_Known();
                            isArray = true;
                        } else {
                            TODO(sp, "Determine field count for " << ty);
                        }
                    }
                    if (isArray && nFlds >= PARTIAL_ARRAY_MIN) {
                        ivs = VarState::make_PartialArray({box$(tpl.clone()), {}, nFlds});
                    } else {
                        ::std::vector<VarState> innerVs;
                        innerVs.reserve(nFlds);
                        for (size_t i = 0; i < nFlds; i++) {
                            innerVs.push_back(tpl.clone());
                        }
                        ivs = VarState::make_Partial({mv$(innerVs), ~0u});
                    }
                }
                if (ivs.is_PartialArray()) {
                    auto& pa = ivs.as_PartialArray();
                    ASSERT_BUG(sp, fieldIndex < pa.count, "Array field index out of range - " << lv);
                    auto it = pa.otherStates.find(fieldIndex);
                    if (it == pa.otherStates.end()) {
                        it = pa.otherStates.insert(::std::make_pair(fieldIndex, pa.fillState->clone())).first;
                    }
                    vs = &it->second;
                } else {
                    vs = &ivs.as_Partial().innerStates.at(fieldIndex);
                }
                break;
            }
            case MIRLValue::Wrapper::TAG_Deref: {
                // A Box dereference is a move path: track its pointee separately so a
                // later shallow drop deallocates the Box without dropping moved data.
                bool isBox = false;
                if (this->langBox_) {
                    const auto* ty = valType(sp, lv, &w);
                    DEBUG("ty = " << ty);
                    isBox = this->isTypeOwnedBox(ty);
                }

                if (isBox) {
                    if (!ivs.is_MovedOut()) {
                        ::std::vector<VarState> inner;
                        inner.push_back(VarState::make_Valid({}));
                        unsigned int dropFlag = (ivs.is_Optional() ? ivs.as_Optional() : ~0u);
                        ivs = VarState::make_MovedOut({box$(VarState::make_Valid({})), dropFlag});
                    }
                    vs = &*ivs.as_MovedOut().innerState;
                } else {
                    return nullptr;
                }
                break;
            }
            case MIRLValue::Wrapper::TAG_Index: {
                return nullptr;
            }
            case MIRLValue::Wrapper::TAG_Downcast: {
                decltype(w.as_Downcast()) variantIndex = w.as_Downcast();
                if (!ivs.is_Partial()) {
                    ASSERT_BUG(sp, !ivs.is_MovedOut(), "Downcast of a MovedOut value");

                    size_t varCount = 0;
                    {
                        const auto* ty = valType(sp, lv, &w);
                        DEBUG("ty = " << ty);
                        ASSERT_BUG(sp, ty->is_Path(), "Downcast on non-Path type - " << ty);
                        const auto& pb = ty->as_Path().binding;
                        // TODO: What about unions?
                        // - Iirc, you can't move out of them so they will never have state mutated
                        if (pb.is_Enum()) {
                            const auto& enm = *pb.as_Enum();
                            varCount = enm.numVariants();
                        } else if (const auto* pbe = pb.opt_Union()) {
                            const auto& unm = **pbe;
                            varCount = unm.variants.size();
                        } else {
                            BUG(sp, "Downcast on non-Enum/Union - " << ty);
                        }
                    }

                    const auto outerFlag = ivs.is_Optional() ? ivs.as_Optional() : ~0u;
                    ::std::vector<VarState> inner;
                    for (size_t i = 0; i < varCount; i++) {
                        inner.push_back(VarState::make_Invalid(InvalidType::Uninit));
                    }
                    inner[variantIndex] = mv$(ivs);
                    ivs = VarState::make_Partial({mv$(inner), outerFlag});
                }

                vs = &ivs.as_Partial().innerStates.at(variantIndex);
                break;
            }
        }
        assert(vs);
    }
    return vs;
}

void MirBuilder::emitArrayElementDropLoop(const Span& sp, const MIRLValue& arrLv, size_t start, size_t end, unsigned int dropFlag) {
    if (start >= end) {
        return;
    }
    const auto* usizeTy = resolve_.hirCrate().types.primitive(HIRCoreType::Usize);
    const auto* boolTy = resolve_.hirCrate().types.primitive(HIRCoreType::Bool);
    // Unscoped locals: primitives that outlive scope teardown and need no drops.
    const auto newUnscopedLocal = [&](const HIRTypeData* ty) -> unsigned {
        const auto rv = static_cast<unsigned>(output.locals.size());
        output.locals.push_back(ty);
        slotStates.push_back(VarState::make_Valid({}));
        return rv;
    };
    const auto idxLocal = newUnscopedLocal(usizeTy);
    const auto cmpLocal = newUnscopedLocal(boolTy);
    const auto mkUsize = [](size_t v) {
        return MIRParam(MIRConstant::make_Uint({U128(static_cast<u64>(v)), HIRCoreType::Usize}));
    };
    pushStmtAssign(sp, MIRLValue::newLocal(idxLocal), MIRRValue::make_Constant(MIRConstant::make_Uint({U128(static_cast<u64>(start)), HIRCoreType::Usize})), /*update_dest_state=*/false);
    const auto bbCond = newBbUnlinked();
    const auto bbBody = newBbUnlinked();
    const auto bbNext = newBbUnlinked();
    endBlock(MIRTerminator::make_Goto(bbCond));
    setCurBlock(bbCond);
    pushStmtAssign(sp, MIRLValue::newLocal(cmpLocal), MIRRValue::make_BinOp({MIRParam(MIRLValue::newLocal(idxLocal)), MIRBinOp::LT, mkUsize(end)}), /*update_dest_state=*/false);
    endBlock(MIRTerminator::make_If({MIRLValue::newLocal(cmpLocal), bbBody, bbNext}));
    setCurBlock(bbBody);
    pushStmtDrop(sp, MIRLValue::newIndex(arrLv.clone(), idxLocal), dropFlag);
    pushStmtAssign(sp, MIRLValue::newLocal(idxLocal), MIRRValue::make_BinOp({MIRParam(MIRLValue::newLocal(idxLocal)), MIRBinOp::ADD, mkUsize(1)}), /*update_dest_state=*/false);
    endBlock(MIRTerminator::make_Goto(bbCond));
    setCurBlock(bbNext);
}

void MirBuilder::dropValueFromState(const Span& sp, VarState& vs, MIRLValue lv) {
    TRACE_FUNCTION_F(lv << " " << vs);
    switch (vs.tag()) {
        case VarState::TAG_Invalid: {
            break;
        }
        case VarState::TAG_Valid: {
            vs = VarState::make_Invalid(InvalidType::Moved); pushStmtDrop(sp, mv$(lv));
            break;
        }
        case VarState::TAG_MovedOut: {
            auto& vse = vs.as_MovedOut();
            auto movedState = vse.innerState->clone();
            const auto outerFlag = vse.outerFlag;
            vs = VarState::make_Invalid(InvalidType::Moved);
            const bool isBox = this->isTypeOwnedBox(valType(sp, lv));
                    if (isBox) {
                dropValueFromState(sp, movedState, MIRLValue::newDeref(lv.clone()));
                pushStmtDropShallow(sp, mv$(lv), outerFlag);
                    } else {
                TODO(sp, ""); }
            break;
        }
        case VarState::TAG_Partial: {
            auto partialState = vs.clone();
            vs = VarState::make_Invalid(InvalidType::Moved);
            auto& partial = partialState.as_Partial();
            const auto* lvTy = valType(sp, lv);
            const bool is_enum = lvTy->is_Path() && lvTy->as_Path().binding.is_Enum();
            const bool isUnion = lvTy->is_Path() && lvTy->as_Path().binding.is_Union();
                    if (is_enum) {
                bool hasValidVariant = false;
                for (const auto& state : partial.innerStates) {
                    hasValidVariant |= !state.is_Invalid();
                }
                if (!hasValidVariant) {
                    return;
                }

                const auto outerFlag = partial.outerFlag;
                const auto* markings = lvTy->as_Path().binding.getTraitMarkings();
                ASSERT_BUG(sp, markings, "Enum path binding has no trait markings - " << lvTy);
                if (markings->hasDropImpl) {
                    pushStmtDrop(sp, mv$(lv), outerFlag);
                    break;
                }

                const auto nextBb = newBbUnlinked();
                ::std::vector<MIRBasicBlockId> arms;
                ::std::vector<MIRBasicBlockId> cleanupBlocks;
                arms.reserve(partial.innerStates.size());
                cleanupBlocks.reserve(partial.innerStates.size());
                for (const auto& state : partial.innerStates) {
                    const auto cleanupBb = state.is_Invalid() ? nextBb : newBbUnlinked();
                    arms.push_back(cleanupBb);
                    cleanupBlocks.push_back(cleanupBb);
                }
                endBlock(MIRTerminator::make_Switch({lv.clone(), mv$(arms), outerFlag, outerFlag == ~0u ? ~0u : nextBb}));

                const auto variantCount = partial.innerStates.size();
                for (size_t i = 0; i < variantCount; i++) {
                    if (partial.innerStates[i].is_Invalid()) {
                        continue;
                    }
                    setCurBlock(cleanupBlocks[i]);
                    dropValueFromState(sp, partial.innerStates[i], MIRLValue::newDowncast(lv.clone(), static_cast<unsigned int>(i)));
                    endBlock(MIRTerminator::make_Goto(nextBb));
                }
                setCurBlock(nextBb);
                    } else if (isUnion) {
                // NOTE: Unions don't drop inner items.
                    } else {
                for (size_t i = 0; i < partial.innerStates.size(); i++) {
                    dropValueFromState(sp, partial.innerStates[i], MIRLValue::newField(lv.clone(), static_cast<unsigned int>(i)));
                }
                    }
            break;
        }
        case VarState::TAG_Optional: {
            auto& vse = vs.as_Optional();
            const auto flag = vse; vs = VarState::make_Invalid(InvalidType::Moved); pushStmtDrop(sp, mv$(lv), flag);
            break;
        }
        case VarState::TAG_PartialArray: {
            auto arrayState = vs.clone(); vs = VarState::make_Invalid(InvalidType::Moved); auto& array = arrayState.as_PartialArray();
                unsigned int fillFlag = ~0u;
                bool fillDrop = true;
                switch ((*array.fillState).tag()) {
default:
                    BUG(sp, "Composite fill state in PartialArray drop - " << *array.fillState);
                    case VarState::TAG_Valid: {
                        break;
                    }
                    case VarState::TAG_Invalid: {
                        fillDrop = false;
                        break;
                    }
                    case VarState::TAG_Optional: {
                        auto& fe = (*array.fillState).as_Optional();
                        fillFlag = fe;
                        break;
                    }
                }
                size_t prev = 0;
                for (auto& kv : array.otherStates) {
            if (fillDrop) {
                emitArrayElementDropLoop(sp, lv, prev, kv.first, fillFlag);
            }
            dropValueFromState(sp, kv.second, MIRLValue::newField(lv.clone(), kv.first));
            prev = kv.first + 1;
                }
                if (fillDrop) {
            emitArrayElementDropLoop(sp, lv, prev, array.count, fillFlag);
                }
            break;
        }
    }
}

void MirBuilder::dropScopeValues(ScopeDef& sd, bool preserveStates /*=false*/) {
    switch (sd.data.tag()) {
        case ScopeType::TAG_Owning: {
            auto& e = sd.data.as_Owning();
            const auto dropSlots = e.dropSlots;
            for (const auto& slot : ::reverse(dropSlots)) {
                const auto slotType = slot.isArgument ? SlotType::Argument : SlotType::Local;
                auto lvalue = slot.isArgument ? MIRLValue::newArgument(slot.index) : MIRLValue::newLocal(slot.index);
                if (buildingCleanup) {
                    if (unwindConsumedValue && lvalue == *unwindConsumedValue) {
                        continue;
                    }
                    auto state = getSlotState(sd.span, slot.index, slotType).clone();
                    DEBUG(lvalue << " - " << state);
                    dropValueFromState(sd.span, state, mv$(lvalue));
                } else if (preserveStates) {
                    auto state = getSlotState(sd.span, slot.index, slotType).clone();
                    DEBUG(lvalue << " - " << state << " (branch)");
                    dropValueFromState(sd.span, state, mv$(lvalue));
                } else {
                    auto& state = getSlotStateMut(sd.span, slot.index, slotType);
                    DEBUG(lvalue << " - " << state);
                    dropValueFromState(sd.span, state, mv$(lvalue));
                }
            }
            break;
        }
        case ScopeType::TAG_Split: {
            // No values, controls parent
            break;
        }
        case ScopeType::TAG_Loop: {
            // No values
            break;
        }
        case ScopeType::TAG_Freeze: {
            // No values
            break;
        }
    }
}

void MirBuilder::movedLvalue(const Span& sp, const MIRLValue& lv) {
    if (!lvalueIsCopy(sp, lv)) {
        auto* vsP = getValStateMutP(sp, lv);
        if (!vsP) {
            ERROR(sp, E0000, "Attempting to move out of invalid slot - " << lv);
        }
        auto& vs = *vsP;
        // TODO: If the current state is Optional, set the drop flag to 0
        auto newState = VarState::make_Invalid(InvalidType::Moved);
        DEBUG("State " << lv << " " << vs << " => " << newState);
        vs = std::move(newState);
    }
}

MIRLValue MirBuilder::getPtrToDst(const Span& sp, const MIRLValue& lv) const {
    // Undo field accesses
    size_t count = 0;
    while (count < lv.wrappers.size() && lv.wrappers[lv.wrappers.size() - 1 - count].is_Field()) {
        count++;
    }

    // TODO: Enum variants?

    if (count == lv.wrappers.size() && lv.root.is_Argument()) {
        const auto argument = lv.root.as_Argument();
        ASSERT_BUG(sp, argument < args_.size(), "Invalid unsized argument - " << lv);
        ASSERT_BUG(sp, !resolve_.typeIsSized(sp, args_[argument].second), "Access of a sized argument as an unsized value - " << lv);
        return count == 0 ? lv.clone() : lv.cloneUnwrapped(count);
    }

    ASSERT_BUG(sp, count < lv.wrappers.size() && lv.wrappers[lv.wrappers.size() - 1 - count].is_Deref(), "Access of an unsized field without a dereference - " << lv);

    return lv.cloneUnwrapped(count + 1);
}

std::map<unsigned, MirBuilder::SavedActiveLocal> MirBuilder::getActiveLocals(const Span& sp, std::set<unsigned>& savedDropFlags) const {
    TRACE_FUNCTION;
    std::map<unsigned, MirBuilder::SavedActiveLocal> rv;
    for (size_t i = 0; i < slotStates.size(); i++) {
        const auto& s = getSlotState(sp, i, SlotType::Local);
        switch (s.tag()) {
default:
            DEBUG("_" << i << " : " << s);
            s.getUsedDropFlags(&savedDropFlags);
            rv.insert(std::make_pair(static_cast<unsigned>(i), SavedActiveLocal(s.clone())));
            break;
            case VarState::TAG_Invalid: {
                break;
            }
            case VarState::TAG_MovedOut: {
                break;
            }
        }
    }
    return rv;
}

void MirBuilder::dropActveLocal(const Span& sp, MIRLValue lv, const SavedActiveLocal& loc) {
    auto state = loc.state.clone();
    this->dropValueFromState(sp, state, mv$(lv));
}

void MirBuilder::emitUnwindCleanup(const Span& sp) {
    const auto wasBuildingCleanup = buildingCleanup;
    buildingCleanup = true;
    output.blocks.at(currentBlock).isCleanup = true;
    for (auto it = scopeStack.rbegin(); it != scopeStack.rend(); ++it) {
        dropScopeValues(scopes.at(*it));
    }
    buildingCleanup = wasBuildingCleanup;
}

MIRUnwindAction MirBuilder::makeUnwindAction(const Span& sp, const MIRLValue* consumedValue) {
    if (buildingCleanup) {
        return MIRUnwindAction::make_Terminate({});
    }

    const auto sourceBlock = pauseCurBlock();
    const auto cleanupBlock = newBbUnlinked();
    setCurBlock(cleanupBlock);
    const auto* oldConsumedValue = unwindConsumedValue;
    unwindConsumedValue = consumedValue;
    emitUnwindCleanup(sp);
    unwindConsumedValue = oldConsumedValue;
    endBlock(MIRTerminator::make_UnwindResume({}));
    setCurBlock(sourceBlock);
    return MIRUnwindAction::make_Cleanup(cleanupBlock);
}

// --------------------------------------------------------------------

ScopeHandle::~ScopeHandle() {
    if (idx != ~0u) {
        try {
            ASSERT_BUG(Span(), builder.scopes.size() > idx, "Scope invalid");
            ASSERT_BUG(Span(), builder.scopes.at(idx).complete, "Scope " << idx << " not completed");
        } catch (...) {
            abort();
        }
    }
}

VarState VarState::clone() const {
    switch ((*this).tag()) {
        case VarState::TAG_Invalid: {
            auto& e = (*this).as_Invalid();
            return VarState(e);
        }
        case VarState::TAG_Valid: {
            auto& e = (*this).as_Valid();
            return VarState(e);
        }
        case VarState::TAG_Optional: {
            auto& e = (*this).as_Optional();
            return VarState(e);
        }
        case VarState::TAG_MovedOut: {
            auto& e = (*this).as_MovedOut();
            return VarState::make_MovedOut({box$(e.innerState->clone()), e.outerFlag});
        }
        case VarState::TAG_Partial: {
            auto& e = (*this).as_Partial();
            ::std::vector<VarState> n; n.reserve(e.innerStates.size()); for (const auto& a : e.innerStates) n.push_back(a.clone()); return VarState::make_Partial({mv$(n), e.outerFlag});
            break;
        }
        case VarState::TAG_PartialArray: {
            auto& e = (*this).as_PartialArray();
            ::std::map<unsigned, VarState> n; for (const auto& kv : e.otherStates) n.insert(::std::make_pair(kv.first, kv.second.clone())); return VarState::make_PartialArray({box$(e.fillState->clone()), mv$(n), e.count});
            break;
        }
    }
    throw "";
}

bool VarState::operator==(const VarState& x) const {
    if (this->tag() != x.tag()) {
        return false;
    }
    switch ((*this).tag()) {
        case VarState::TAG_Invalid: {
            auto& te = (*this).as_Invalid();
            auto& xe = x.as_Invalid();
            return te == xe;
        }
        case VarState::TAG_Valid: {
            return true;
        }
        case VarState::TAG_Optional: {
            auto& te = (*this).as_Optional();
            auto& xe = x.as_Optional();
            return te == xe;
        }
        case VarState::TAG_MovedOut: {
            auto& te = (*this).as_MovedOut();
            auto& xe = x.as_MovedOut();
            if (te.outerFlag != xe.outerFlag) return false; return *te.innerState == *xe.innerState;
            break;
        }
        case VarState::TAG_Partial: {
            auto& te = (*this).as_Partial();
            auto& xe = x.as_Partial();
            if (te.outerFlag != xe.outerFlag || te.innerStates.size() != xe.innerStates.size()) return false; for (unsigned int i = 0; i < te.innerStates.size(); i++) {
                if (te.innerStates[i] != xe.innerStates[i]) {
                    return false;
                }
            } return true;
            break;
        }
        case VarState::TAG_PartialArray: {
            auto& te = (*this).as_PartialArray();
            auto& xe = x.as_PartialArray();
            if (te.count != xe.count || !(*te.fillState == *xe.fillState) || te.otherStates.size() != xe.otherStates.size()) return false; for (auto itT = te.otherStates.begin(), itX = xe.otherStates.begin(); itT != te.otherStates.end(); ++itT, ++itX) {
                if (itT->first != itX->first || itT->second != itX->second) {
                    return false;
                }
            } return true;
            break;
        }
    }
    throw "";
}

::std::ostream& operator<<(::std::ostream& os, const VarState& x) {
    switch (x.tag()) {
        case VarState::TAG_Invalid: {
            auto& e = x.as_Invalid();
            switch (e) {
                case InvalidType::Uninit:
                    os << "Uninit";
                    break;
                case InvalidType::Moved:
                    os << "Moved";
                    break;
                case InvalidType::Descoped:
                    os << "Descoped";
                    break;
            }
            break;
        }
        case VarState::TAG_Valid: {
            os << "Valid";
            break;
        }
        case VarState::TAG_Optional: {
            auto& e = x.as_Optional();
            os << "Optional(df" << e << ")";
            break;
        }
        case VarState::TAG_MovedOut: {
            auto& e = x.as_MovedOut();
            os << "MovedOut("; if (e.outerFlag == ~0u) os << "-"; else os << "df" << e.outerFlag; os << " " << *e.innerState << ")";
            break;
        }
        case VarState::TAG_Partial: {
            auto& e = x.as_Partial();
            os << "Partial("; if (e.outerFlag == ~0u) os << "-"; else os << "df" << e.outerFlag; os << ", [" << e.innerStates << "])";
            break;
        }
        case VarState::TAG_PartialArray: {
            auto& e = x.as_PartialArray();
            os << "PartialArray(" << e.count << ", fill=" << *e.fillState << ", {"; for (const auto& kv : e.otherStates) os << kv.first << ": " << kv.second << ","; os << "})";
            break;
        }
    }
    return os;
}

bool VarState::getUsedDropFlags(std::set<unsigned>* out) const {
    bool rv = false;
    switch ((*this).tag()) {
        case VarState::TAG_Optional: {
            auto& ve = (*this).as_Optional();
            if (out) {
                out->insert(ve);
            }
            rv = true;
            break;
        }
        case VarState::TAG_Invalid: {
            break;
        }
        case VarState::TAG_Valid: {
            break;
        }
        case VarState::TAG_Partial: {
            auto& ve = (*this).as_Partial();
            if (ve.outerFlag != ~0u) {
                if (out) {
                    out->insert(ve.outerFlag);
                }
                rv = true;
            }
            for (const auto& vs : ve.innerStates) {
                rv |= vs.getUsedDropFlags(out);
            }
            break;
        }
        case VarState::TAG_PartialArray: {
            auto& ve = (*this).as_PartialArray();
            rv |= ve.fillState->getUsedDropFlags(out);
            for (const auto& kv : ve.otherStates) {
                rv |= kv.second.getUsedDropFlags(out);
            }
            break;
        }
        case VarState::TAG_MovedOut: {
            auto& ve = (*this).as_MovedOut();
            if (ve.outerFlag != ~0u) {
                if (out) {
                    out->insert(ve.outerFlag);
                }
                rv = true;
            }
            rv |= ve.innerState->getUsedDropFlags(out);
            break;
        }
    }
    return rv;
}

ScopeHandle::ScopeHandle(const MirBuilder& builder, unsigned int idx)
    : builder(builder)
    , idx(idx)
{
}

ScopeHandle::ScopeHandle(ScopeHandle&& x)
    : builder(x.builder)
    , idx(x.idx)
{
    x.idx = ~0;
}

PatternBinding::PatternBinding(fieldPathT field, const HIRPatternBinding& binding, unsigned rootIndex)
    : field(std::move(field))
    , rootIndex(rootIndex)
    , binding(&binding)
    , splitSlice(SIZE_MAX, SIZE_MAX)
{
}

MirBuilder::ScopeDef::ScopeDef(const Span& span)
    : span(span)
{
}

MirBuilder::ScopeDef::ScopeDef(const Span& span, ScopeType data)
    : span(span)
    , data(mv$(data))
{
}

/// Save the current state of aliases (see add_variable_alias)
MirBuilder::SavedAliases MirBuilder::saveAliases() const {
    SavedAliases rv;
    rv.setAliases.reserve(variableAliases.size());
    for (const auto& v : variableAliases) {
        rv.setAliases.push_back(v.second != MIRLValue());
    }
    return rv;
}

void MirBuilder::restoreAliases(SavedAliases a) {
    assert(a.setAliases.size() == variableAliases.size());
    for (size_t i = 0; i < a.setAliases.size(); i++) {
        if (!a.setAliases[i]) {
            variableAliases.at(i).second = MIRLValue();
        }
    }
}

// Variable aliases (used for match guards)
void MirBuilder::addVariableAlias(const Span& sp, unsigned idx, HIRPatternBinding::Type ty, MIRLValue lv) {
    DEBUG("#" << idx << " = " << int(ty) << " " << lv);
    ASSERT_BUG(sp, idx < variableAliases.size(), "Variable alias #" << idx << " out of bounds");
    ASSERT_BUG(sp, variableAliases[idx].second == MIRLValue(), "Variable alias #" << idx << " already exists: " << variableAliases[idx].second << " setting " << lv);
    variableAliases[idx] = std::make_pair(ty, mv$(lv));
}

const MirBuilder::varAliasT* MirBuilder::getVariableAlias(const Span& sp, unsigned idx) const {
    ASSERT_BUG(sp, idx < variableAliases.size(), "Variable alias #" << idx << " out of bounds");
    if (variableAliases[idx].second == MIRLValue()) {
        return nullptr;
    } else {
        return &variableAliases[idx];
    }
}

// - Values
MIRLValue MirBuilder::getVariable(const Span& sp, unsigned idx) const {
    auto it = varArgMappings.find(idx);
    if (it != varArgMappings.end()) {
        return MIRLValue::newArgument(it->second);
    }
    return MIRLValue::newLocal(idx);
}

MIRLValue MirBuilder::getRvalInIfCond(const Span& sp, MIRRValue val) {
    pushStmtAssign(sp, ifCondLval.clone(), mv$(val));
    return ifCondLval.clone();
}

MirBuilder::SavedActiveLocal::SavedActiveLocal(VarState vs)
    : state(mv$(vs))
{
}

::std::ostream& operator<<(::std::ostream& os, const ScopeHandle& x) {
    os << x.idx;
    return os;
}

::std::ostream& operator<<(::std::ostream& os, const fieldPathT& x) {
    for (auto idx : x.data) {
        os << ".";
        if (idx == FIELD_DEREF) {
            os << "*";
        } else if (idx > FIELD_INDEX_MAX) {
            idx -= FIELD_INDEX_MAX;
            idx = FIELD_INDEX_MAX - idx;
            os << "-" << static_cast<unsigned int>(idx);
        } else {
            os << static_cast<unsigned int>(idx);
        }
    }
    return os;
}

::std::ostream& operator<<(::std::ostream& os, const PatternBinding& x) {
    os << *x.binding << x.field;
    if (x.isSplitSlice()) {
        os << "[" << x.splitSlice.first << "..-" << x.splitSlice.second << "]";
    }
    return os;
}

// Bodies of the generated local unions (see mir_from_hir_pattern.tu).
#include "mir_from_hir_pattern_tu.cpp"
