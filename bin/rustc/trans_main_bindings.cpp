#include "trans_main_bindings.h"
#include "trans_main_bindings.h"

#include "hir_hir.h"
#include "mir_mir.h"
#include "wire_board.h"
#include "hir_visitor.h"
#include "mir_helpers.h"
#include "trans_target.h"
#include "hir_item_path.h"
#include "mir_operations.h"
#include "trans_mangling.h"
#include "trans_allocator.h"
#include "trans_trans_list.h"
#include "hir_typeck_common.h"
#include "hir_typeck_static.h"
#include "hir_typeck_monomorph.h"
#include "hir_conv_main_bindings.h"
#include "hir_conv_constant_evaluation.h"

#include <std/alg/defer.h>
#include <std/lib/vector.h>

#include <deque>
#include <algorithm>
#include <unordered_set>

using namespace stl;

namespace {
    struct State {
        HIRCrate& crate;
        StaticTraitResolve resolve;
        const TransList& transList;
        std::deque<const HIRTypeData*> todoList;
        HIRTypeRefSet doneList;

        HIRSimplePath langClone;

        State(const WireBoard& wb, HIRCrate& crate, const TransList& transList);

        void enqueueType(const HIRTypeData* ty);
    };

    struct CloneCleanupState {
        Vector<MIRBasicBlockId> calls;
        std::vector<std::pair<MIRLValue, unsigned>> values;
    };

    struct Builder {
        const State& state;
        MIRFunction& mir;
        const MIRLValue self;

        Builder(const State& state, MIRFunction& mir);

        MIRLValue addLocal(const HIRTypeData* ty);

        MIRLValue inTemporary(const HIRTypeData* ty, MIRRValue val);

        void ensureOpen();

        void pushStmt(MIRStatement s);

        void pushStmtAssign(MIRLValue lv, MIRRValue rv);

        MIRBasicBlockId pushStmtDrop(MIRLValue lv);

        void pushDropSequence(std::vector<MIRLValue> values, MIRBasicBlockId customDropCall = ~0u);

        void terminateBlock(MIRTerminator term);

        void terminateCall(MIRLValue rv, MIRCallTarget tgt, std::vector<MIRParam> args, MIRBasicBlockId bbRet, MIRBasicBlockId bbPanic, bool tracksCaller = false);

        MIRBasicBlockId pushCallDrop(const HIRTypeData* ty);
    };

    struct BindTranslationNominals final: public HIRVisitor {
        const HIRCrate& crate;

        explicit BindTranslationNominals(const HIRCrate& crate);

        [[nodiscard]] const HIRTypeData* visitType(const HIRTypeData* ty) override;
    };

    struct EnumState {
        const HIRCrate& crate;
        StaticTraitResolve resolve;
        TransList rv;
        const TransList* origList;

        std::deque<TransListFunction*> fcnQueue;
        Vector<TransListFunction*> fcnsToTypeVisit;

        std::set<std::string> emittedFunctions;
        std::set<HIRPath> activePaths;

        std::unordered_map<std::string, std::pair<HIRSimplePath, const HIRFunction*>> linkFunctions;

        EnumState(const WireBoard& wb);

        void enumFcn(HIRPath p, const HIRFunction& fcn, TransParams pp);

        void enumerateLinkFunctions();

        void enumerateLinkFunctionsIn(const HIRModule& mod, HIRItemPath modPath);
    };

    struct GlobalAsmOperandEvaluator: public HIRVisitor {
        const WireBoard& wb;
        const HIRCrate& crate;
        const Span* span = nullptr;

        explicit GlobalAsmOperandEvaluator(const WireBoard& wb);

        void evaluate(HIRGlobalAssembly& item);

        void visitConstgeneric(HIRConstGeneric& value) override;
    };

    struct TransPathCallback {
        virtual HIRSimplePath get() = 0;
    };

    template <typename F>
    struct TransPathCb final: TransPathCallback {
        F f;

        explicit TransPathCb(F f);

        HIRSimplePath get() override;
    };

    struct PtrComp {
        template <typename T>
        bool operator()(const T* lhs, const T* rhs) const;
    };

    struct TypeVisitor {
        const HIRCrate& crate;
        ::StaticTraitResolve resolve;
        TransList& out;
        const TransList* prevList;

        HIRTypeRefSet activeSet;

        TypeVisitor(const WireBoard& wb, TransList& out, const TransList* prevList);

        ~TypeVisitor();

        void visitStruct(const HIRTypeData* selfType, const HIRGenericPath& path, const HIRStruct& item);

        void visitUnion(const HIRTypeData* selfType, const HIRGenericPath& path, const HIRUnion& item);

        void visitEnum(const HIRTypeData* selfType, const HIRGenericPath& path, const HIREnum& item);

        enum class Mode {
            Shallow,
            Normal,
            Deep,
        };

        void visitType(const HIRTypeData* ty, Mode mode = Mode::Normal);

        void __attribute__((noinline)) visitFunction(const HIRPath& path, const HIRFunction& fcn, const TransParams& pp);
    };

    MIRFunctionPointer generatedBody(MIRFunction mir = MIRFunction()) {
        return MIRFunctionPointer(new MIRFunction(mv$(mir)));
    }

    using MIREnumCache = MIRFunction::MIREnumCache;
    using MIREnumCachePtr = MIRFunction::MIREnumCachePtr;

    MIRBasicBlock& cloneOpenBlock(MIRFunction& mirFcn) {
        if (mirFcn.blocks.empty() || !mirFcn.blocks.back().terminator.is_Incomplete()) {
            mirFcn.blocks.push_back(MIRBasicBlock());
        }
        return mirFcn.blocks.back();
    }

    MIRParam cloneField(const State& state, const Span& sp, MIRFunction& mirFcn, CloneCleanupState& cleanup, const HIRTypeData* subty, MIRLValue fldLvalue) {
        if (state.resolve.typeIsCopy(sp, subty)) {
            return std::move(fldLvalue);
        } else {
            const auto& langClone = state.resolve.hirCrate().getLangItemPath(sp, "clone");
            auto borrowLv = MIRLValue::newLocal(mirFcn.locals.length());
            mirFcn.locals.pushBack(state.crate.types.borrow(HIRBorrowType::Shared, subty));
            auto resLv = MIRLValue::newLocal(mirFcn.locals.length());
            mirFcn.locals.pushBack(subty);
            const auto dropFlag = static_cast<unsigned>(mirFcn.dropFlags.length());
            mirFcn.dropFlags.pushBack(false);

            auto& bb = cloneOpenBlock(mirFcn);
            bb.statements.push_back(MIRStatement::make_Assign({borrowLv.clone(), MIRRValue::make_Borrow({HIRBorrowType::Shared, false, mv$(fldLvalue)})}));
            HIRPathParams pp;
            const auto callBlock = static_cast<MIRBasicBlockId>(mirFcn.blocks.size() - 1);
            const auto retBlock = static_cast<MIRBasicBlockId>(mirFcn.blocks.size());
            bb.terminator = MIRTerminator::make_Call({retBlock, MIRUnwindAction::make_Continue({}), resLv.clone(), MIRCallTarget(HIRPath(subty, langClone, "clone", std::move(pp))), ::makeVec1<MIRParam>(std::move(borrowLv))});
            cleanup.calls.pushBack(callBlock);
            cleanup.values.push_back(std::make_pair(resLv.clone(), dropFlag));

            mirFcn.blocks.push_back(MIRBasicBlock());
            mirFcn.blocks.back().statements.push_back(MIRStatement::make_SetDropFlag({dropFlag, true, ~0u}));

            return std::move(resLv);
        }
    }

    void appendCloneCleanup(MIRFunction& mirFcn, const CloneCleanupState& cleanup) {
        if (cleanup.calls.empty()) {
            return;
        }

        const auto cleanupStart = static_cast<MIRBasicBlockId>(mirFcn.blocks.size());
        const auto resume = static_cast<MIRBasicBlockId>(cleanupStart + cleanup.values.size());
        for (auto it = cleanup.values.rbegin(); it != cleanup.values.rend(); ++it) {
            MIRBasicBlock block;
            block.isCleanup = true;
            block.terminator = MIRTerminator::make_Drop({
                MIRDropKind::DEEP,
                it->first.clone(),
                it->second,
                static_cast<MIRBasicBlockId>(mirFcn.blocks.size() + 1),
                MIRUnwindAction::make_Terminate({}),
            });
            mirFcn.blocks.push_back(mv$(block));
        }
        BUG_ASSERT(mirFcn.blocks.size() == resume);
        MIRBasicBlock resumeBlock;
        resumeBlock.isCleanup = true;
        resumeBlock.terminator = MIRTerminator::make_UnwindResume({});
        mirFcn.blocks.push_back(mv$(resumeBlock));

        for (const auto call : cleanup.calls) {
            BUG_ASSERT(mirFcn.blocks.at(call).terminator.is_Call());
            mirFcn.blocks[call].terminator.as_Call().unwind = MIRUnwindAction::make_Cleanup(cleanupStart);
        }
    }

    void TransAutoImplClone(State& state, const HIRTypeData* ty) {
        Span sp;

        TRACE_FUNCTION_F(ty);
        MIRFunction mirFcn;
        if (state.resolve.typeIsCopy(sp, ty)) {
            MIRBasicBlock bb;
            bb.statements.push_back(MIRStatement::make_Assign({MIRLValue::newReturn(), MIRRValue::make_Use(MIRLValue::newDeref(MIRLValue::newArgument(0)))}));
            bb.terminator = MIRTerminator::make_Return({});
            mirFcn.blocks.push_back(std::move(bb));
        } else {
            switch ((*ty).tag()) {
                default:
                    TODO(sp, StringView("auto Clone for ") << ty << StringView(" - Unknown and not Copy"));
                case HIRTypeData::TAG_Path: {
                    auto& te = (*ty).as_Path();
                    if (te.isClosure()) {
                        const auto& gp = te.path.data.as_Generic();
                        const auto& str = state.resolve.hirCrate().getStructByPath(sp, gp.path);
                        auto p = TransParams::newImpl(state.crate.types, sp, ty, gp.params.clone());
                        CloneCleanupState cleanup;
                        std::vector<MIRParam> values;
                        values.reserve(str.data.as_Tuple().size());
                        for (const auto& fld : str.data.as_Tuple()) {
                            const HIRTypeData* tmp;
                            const auto& tyM = monomorphiseTypeNeeded(fld.ent) ? (tmp = p.monomorph(state.resolve, fld.ent)) : fld.ent;
                            auto fldLvalue = MIRLValue::newField(MIRLValue::newDeref(MIRLValue::newArgument(0)), static_cast<unsigned>(values.size()));
                            values.push_back(cloneField(state, sp, mirFcn, cleanup, tyM, mv$(fldLvalue)));
                        }
                        auto& bb = cloneOpenBlock(mirFcn);
                        bb.statements.push_back(MIRStatement::make_Assign({MIRLValue::newReturn(), MIRRValue::make_Struct({gp.clone(), mv$(values)})}));
                        bb.terminator = MIRTerminator::make_Return({});
                        appendCloneCleanup(mirFcn, cleanup);
                    } else {
                        TODO(sp, StringView("auto Clone for ") << ty << StringView(" - Unknown and not Copy"));
                    }
                    break;
                }
                case HIRTypeData::TAG_Array: {
                    auto& te = (*ty).as_Array();
                    ASSERT_BUG(sp, te.size.as_Known() < 256, StringView("TODO: Is more than 256 elements sane for auto-generated non-Copy Clone impl? ") << ty);
                    CloneCleanupState cleanup;
                    std::vector<MIRParam> values;
                    values.reserve(te.size.as_Known());
                    for (size_t i = 0; i < te.size.as_Known(); i++) {
                        auto fldLvalue = MIRLValue::newField(MIRLValue::newDeref(MIRLValue::newArgument(0)), static_cast<unsigned>(values.size()));
                        values.push_back(cloneField(state, sp, mirFcn, cleanup, te.inner, mv$(fldLvalue)));
                    }
                    auto& bb = cloneOpenBlock(mirFcn);
                    bb.statements.push_back(MIRStatement::make_Assign({MIRLValue::newReturn(), MIRRValue::make_Array({mv$(values)})}));
                    bb.terminator = MIRTerminator::make_Return({});
                    appendCloneCleanup(mirFcn, cleanup);
                    break;
                }
                case HIRTypeData::TAG_Tuple: {
                    auto& te = (*ty).as_Tuple();
                    BUG_ASSERT(te.length() > 0);

                    CloneCleanupState cleanup;
                    std::vector<MIRParam> values;
                    values.reserve(te.length());
                    for (const auto& subty : te) {
                        auto fldLvalue = MIRLValue::newField(MIRLValue::newDeref(MIRLValue::newArgument(0)), static_cast<unsigned>(values.size()));
                        values.push_back(cloneField(state, sp, mirFcn, cleanup, subty, mv$(fldLvalue)));
                    }

                    auto& bb = cloneOpenBlock(mirFcn);
                    bb.statements.push_back(MIRStatement::make_Assign({MIRLValue::newReturn(), MIRRValue::make_Tuple({mv$(values)})}));
                    bb.terminator = MIRTerminator::make_Return({});
                    appendCloneCleanup(mirFcn, cleanup);
                    break;
                }
            }
        }

        HIRFunction fcn{
            HIRFunction::Receiver::BorrowShared,
            HIRGenericParams{},
            /*m_args=*/::makeVec1(std::make_pair(HIRPattern(HIRPatternBinding(false, HIRPatternBinding::Type::Move, "self", 0), HIRPattern::Data::make_Any({})), state.crate.types.borrow(HIRBorrowType::Shared, ty))),
            /*m_return=*/ty,
            HIRExprPtr{}
        };
        fcn.code.mir = generatedBody(mv$(mirFcn));

        HIRTraitImpl impl;
        impl.type = ty;
        impl.methods.insert(std::make_pair(RcString("clone"), HIRTraitImpl::ImplEnt<HIRFunction>{false, std::move(fcn)}));

        if (state.transList.autoCloneFromImpls.count(ty)) {
            MIRFunction fromMir;
            auto dst = MIRLValue::newDeref(MIRLValue::newArgument(0));
            if (state.resolve.typeIsCopy(sp, ty)) {
                MIRBasicBlock bb;
                bb.statements.push_back(MIRStatement::make_Assign({dst.clone(), MIRRValue::make_Use(MIRLValue::newDeref(MIRLValue::newArgument(1)))}));
                bb.terminator = MIRTerminator::make_Return({});
                fromMir.blocks.push_back(mv$(bb));
            } else {
                const auto& langClone = state.resolve.hirCrate().getLangItemPath(sp, "clone");
                auto cloned = MIRLValue::newLocal(fromMir.locals.length());
                fromMir.locals.pushBack(ty);

                MIRBasicBlock call;
                call.terminator = MIRTerminator::make_Call({
                    1,
                    MIRUnwindAction::make_Continue({}),
                    cloned.clone(),
                    MIRCallTarget(HIRPath(ty, langClone, "clone", HIRPathParams())),
                    ::makeVec1<MIRParam>(MIRLValue::newArgument(1)),
                });
                fromMir.blocks.push_back(mv$(call));

                MIRBasicBlock drop;
                drop.terminator = MIRTerminator::make_Drop({MIRDropKind::DEEP, dst.clone(), ~0u, 2, MIRUnwindAction::make_Continue({})});
                fromMir.blocks.push_back(mv$(drop));

                MIRBasicBlock store;
                store.statements.push_back(MIRStatement::make_Assign({dst.clone(), MIRRValue::make_Use(mv$(cloned))}));
                store.terminator = MIRTerminator::make_Return({});
                fromMir.blocks.push_back(mv$(store));
            }

            auto fromArgs = ::makeVec1(std::make_pair(HIRPattern(HIRPatternBinding(false, HIRPatternBinding::Type::Move, "self", 0), HIRPattern::Data::make_Any({})), state.crate.types.borrow(HIRBorrowType::Unique, ty)));
            fromArgs.push_back(std::make_pair(HIRPattern(HIRPatternBinding(false, HIRPatternBinding::Type::Move, "source", 1), HIRPattern::Data::make_Any({})), state.crate.types.borrow(HIRBorrowType::Shared, ty)));
            HIRFunction fromFcn{
                HIRFunction::Receiver::BorrowUnique,
                HIRGenericParams{},
                mv$(fromArgs),
                /*m_return=*/state.crate.types.unit(),
                HIRExprPtr{}
            };
            fromFcn.code.mir = generatedBody(mv$(fromMir));
            impl.methods.insert(std::make_pair(RcString("clone_from"), HIRTraitImpl::ImplEnt<HIRFunction>{false, std::move(fromFcn)}));
        }

        auto& list = state.crate.traitImpls[state.langClone].getListForTypeMut(impl.type);
        list.push_back(box$(impl));
        state.crate.allTraitImpls[state.langClone].getListForTypeMut(list.back()->type).push_back(list.back().get());
    }

    MIRLValue derefBox(MIRLValue box) {
        auto innerPtr = MIRLValue::newField(MIRLValue::newField(mv$(box), 0), 0);
        innerPtr = MIRLValue::newField(std::move(innerPtr), 0);
        return MIRLValue::newDeref(std::move(innerPtr));
    }

    MIRLValue getUnitPtr(const Span& sp, Builder& mutator, const HIRTypeData* ty, MIRLValue lv, MIRLValue& outInnerPtr) {
        if (ty->is_Path()) {
            const auto& te = ty->as_Path();
            ASSERT_BUG(sp, te.binding.is_Struct(), StringView(""));
            const auto& tyPath = te.path.data.as_Generic();
            const auto& str = *te.binding.as_Struct();
            const HIRTypeData* tmp;
            auto monomorph = [&](const auto& t) {
                return MonomorphStatePtr(mutator.state.crate.types, ty, &tyPath.params, nullptr).monomorphType(sp, t);
            };
            std::vector<MIRParam> vals;
            switch (str.data.tag()) {
                case HIRStructData::TAG_Unit: {
                    break;
                }
                case HIRStructData::TAG_Tuple: {
                    auto& se = str.data.as_Tuple();
                    for (unsigned int i = 0; i < se.size(); i++) {
                        auto val = MIRLValue::newField((i == se.size() - 1 ? mv$(lv) : lv.clone()), i);
                        if (i == str.structMarkings.coerceUnsizedIndex) {
                            vals.push_back(getUnitPtr(sp, mutator, monomorph(se[i].ent), mv$(val), outInnerPtr));
                        } else {
                            vals.push_back(mv$(val));
                        }
                    }
                    break;
                }
                case HIRStructData::TAG_Named: {
                    auto& se = str.data.as_Named();
                    for (unsigned int i = 0; i < se.size(); i++) {
                        auto val = MIRLValue::newField((i == se.size() - 1 ? mv$(lv) : lv.clone()), i);
                        if (i == str.structMarkings.coerceUnsizedIndex) {
                            vals.push_back(getUnitPtr(sp, mutator, monomorph(se[i].ty), mv$(val), outInnerPtr));
                        } else {
                            vals.push_back(mv$(val));
                        }
                    }
                    break;
                }
            }

            auto newPath = tyPath.clone();
            return mutator.inTemporary(mv$(ty), MIRRValue::make_Struct({mv$(newPath), mv$(vals)}));
        } else if (ty->is_Borrow() || ty->is_Pointer()) {
            outInnerPtr = lv.clone();
            return mutator.inTemporary(mv$(ty), MIRRValue::make_DstPtr({mv$(lv)}));
        } else {
            BUG(sp, StringView("Unexpected type coerce_unsize in receiver - ") << ty);
        }
    }

    void bindTranslationNominals(const HIRCrate& crate, HIRPath& path) {
        BindTranslationNominals visitor(crate);
        visitor.visitPath(path, HIRVisitor::PathContext::VALUE);
    }
}

struct MIRFunction::MIREnumCache {
    Vector<const HIRPath*> paths;
    Vector<const HIRTypeData*> typeids;
    Vector<const HIRTypeData*> destructorTypes;

    MIREnumCache();

    void insertPath(const HIRPath& newPath);

    void insertTypeid(const HIRTypeData* newTy);

    void insertDestructorType(const HIRTypeData* newTy);

    void apply(EnumState& state, const TransParams& pp) const;
};

void TransDeleteMIREnumCache(const MIRFunction::MIREnumCache* cache) {
    delete cache;
}

static TransList TransEnumerateCommonPost(EnumState& state);

static void TransEnumerateExplicitLinkage(EnumState& state, const HIRModule& mod, HIRSimplePath modPath);

static void TransEnumerateTypes(EnumState& state);

static void TransEnumerateFillFromPath(EnumState& state, const HIRPath& path, const TransParams& pp);

static void TransEnumerateFillFromPathMono(EnumState& state, HIRPath path);

static void TransEnumerateFillFromFunction(EnumState& state, const HIRPath& path, const HIRFunction& function, const TransParams& pp);

static void TransEnumerateFillFromStatic(EnumState& state, const HIRStatic& stat, TransListStatic& statOut, TransParams pp);

static void TransEnumerateFillFromVTable(EnumState& state, HIRPath vtablePath, const TransParams& pp);

static void TransEnumerateFillFromLiteral(EnumState& state, const EncodedLiteral& lit, const TransParams& pp);

static void TransEnumerateFillFromMIR(MIREnumCache& state, const MIRFunction& code);

static void TransEnumerateGlobalAsm(EnumState& state, HIRModule& mod) {
    GlobalAsmOperandEvaluator evaluator{state.resolve.board()};
    for (auto& item : mod.globalAsm) {
        evaluator.evaluate(item);
        for (const auto& operand : item.operands) {
            if (const auto* path = operand.opt_Sym()) {
                state.rv.roots.push_back(path->clone());
                TransEnumerateFillFromPathMono(state, path->clone());
            }
        }
    }
    for (auto& named : mod.modItems) {
        if (auto* child = named.second->ent.opt_Module()) {
            TransEnumerateGlobalAsm(state, *child);
        }
    }
}

static void TransEnumerateGlobalAllocator(EnumState& state) {
    const auto allocatorIt = state.crate.langItems.find(GLOBAL_ALLOCATOR_LANG_ITEM);
    if (allocatorIt == state.crate.langItems.end()) {
        return;
    }

    const auto& allocatorPath = allocatorIt->second;
    const auto& allocator = state.crate.getStaticByPath(Span(), allocatorPath);

    HIRPath staticPath = HIRGenericPath(allocatorPath);
    state.rv.roots.push_back(staticPath.clone());
    TransEnumerateFillFromPathMono(state, std::move(staticPath));

    auto layoutCtor = TransAllocatorLayoutCtorPath(state.crate);
    state.rv.roots.push_back(layoutCtor.clone());
    TransEnumerateFillFromPathMono(state, std::move(layoutCtor));

    for (size_t i = 0; i < NUM_ALLOCATOR_METHODS; i++) {
        auto methodPath = TransAllocatorMethodPath(state.crate, allocator.type, ALLOCATOR_METHODS[i]);
        state.rv.roots.push_back(methodPath.clone());
        TransEnumerateFillFromPathMono(state, std::move(methodPath));
    }
}

static void enumerateDestructorType(EnumState& state, const HIRTypeData* type) {
    if (!state.resolve.typeNeedsDropGlue(Span(), type)) {
        return;
    }
    switch (type->tag()) {
        case HIRTypeData::TAG_Path:
            state.rv.dropGlue.insert(type);
            break;
        case HIRTypeData::TAG_Borrow: {
            const auto& borrow = type->as_Borrow();
            if (borrow.type == HIRBorrowType::Owned) {
                enumerateDestructorType(state, borrow.inner);
            }
            break;
        }
        case HIRTypeData::TAG_Array:
            enumerateDestructorType(state, type->as_Array().inner);
            break;
        case HIRTypeData::TAG_Slice:
            enumerateDestructorType(state, type->as_Slice().inner);
            break;
        case HIRTypeData::TAG_Tuple:
            for (const auto* field : type->as_Tuple()) {
                enumerateDestructorType(state, field);
            }
            break;
        case HIRTypeData::TAG_Pattern:
            enumerateDestructorType(state, type->as_Pattern().inner);
            break;
        default:
            break;
    }
}

static void TransEnumerateGenericFunctionItems(EnumState& state, const Span& sp, const HIRFunction& e, MonomorphStatePtr ms, bool hasConditionalBounds) {
    if (e.code.mir) {
        const auto& mirFcn = *e.code.mir;
        auto params = HIRPathParams();
        ms.ppMethod = &params;
        if (!mirFcn.transEnumState) {
            auto* esp = new MIREnumCache();
            TransEnumerateFillFromMIR(*esp, *e.code.mir);
            mirFcn.transEnumState = MIREnumCachePtr(esp);
        }

        for (const auto& path : mirFcn.transEnumState->paths) {
            if (!monomorphisePathNeeded(*path)) {
                DEBUG(StringView("Path ") << *path);
                MonomorphState unusedMs(state.crate.types);
                auto v = state.resolve.getValue(sp, *path, unusedMs, true);
                bool deferBoundPath = hasConditionalBounds && v.is_NotYetKnown();
                if (hasConditionalBounds && !deferBoundPath && v.is_Function() && path->data.is_UfcsKnown() && !path->data.as_UfcsKnown().type->is_TraitObject()) {
                    MonomorphState implMs(state.crate.types);
                    deferBoundPath = state.resolve.getValue(sp, *path, implMs, false).is_NotYetKnown();
                }
                if (v.is_StructConstructor() || v.is_EnumConstructor()) {
                } else if (deferBoundPath) {
                    DEBUG(StringView("Defer conditionally available path ") << *path);
                } else {
                    auto p = ms.monomorphPath(sp, *path);
                    state.rv.roots.push_back(p.clone());
                    TransEnumerateFillFromPathMono(state, std::move(p));
                }
            } else {
                DEBUG(StringView("Path ") << *path << StringView(" - Generic"));
            }
        }
    }
}

static void TransEnumerateValItem(EnumState& state, const HIRValueItem& vi, bool isVisible, TransPathCallback& getPath) {
    TRACE_FUNCTION_F(getPath.get() << StringView(" : ") << vi.tagStr() << StringView(" is_visible=") << isVisible);
    const Span sp;
    switch (vi.tag()) {
        break;
        case HIRValueItem::TAG_Import: {
            auto& e = vi.as_Import();
            // TODO: If visible, ensure that target is visited.
            if (isVisible) {
                if (!e.isVariant && e.path.crateName() == state.crate.crateName) {
                    const auto& vi2 = state.crate.getValitemByPath(sp, e.path, false);
                    auto callback = makeCallable<TransPathCb>([&]() {
                        return e.path;
                    });
                    TransEnumerateValItem(state, vi2, isVisible, callback);
                }
            }
        } break;
            break;
        case HIRValueItem::TAG_StructConstant: {
        } break;
            break;
        case HIRValueItem::TAG_StructConstructor: {
        } break;
            break;
        case HIRValueItem::TAG_Constant: {
            const auto& e = *vi.as_Constant();
            if (isVisible) {
                for (const auto& r : e.valueRes.relocations) {
                    if (r.p) {
                        state.rv.roots.push_back(r.p->clone());
                    }
                }
                TransEnumerateFillFromLiteral(state, e.valueRes, TransParams(state.crate.types));
            }
        } break;
            break;
        case HIRValueItem::TAG_Static: {
            const auto& e = *vi.as_Static();
            if (e.linkage.name != "" || e.linkage.section != "") {
                isVisible = true;
            }
            if (e.isPromoted && !e.params.isGeneric()) {
                isVisible = true;
            }
            if (isVisible && !e.params.isGeneric()) {
                // HACK: Refuse to emit unused generated statics

                if (e.type->is_Infer()) {
                    break;
                }
                auto* ptr = state.rv.addStatic(state.crate.types, getPath.get());
                if (ptr) {
                    TransEnumerateFillFromStatic(state, e, *ptr, TransParams(state.crate.types));
                }

                state.rv.roots.push_back(getPath.get());
            }
        } break;
            break;
        case HIRValueItem::TAG_Function: {
            const auto& e = *vi.as_Function();
            bool isInline = false;
            if (isVisible) {
                switch (e.markings.inlineType) {
                    case HIRFunction::Markings::Inline::Always:
                    case HIRFunction::Markings::Inline::Normal:
                        DEBUG(StringView("Don't emit inlined function"));
                        isInline = true;
                        break;
                    case HIRFunction::Markings::Inline::Auto:
                    case HIRFunction::Markings::Inline::Never:
                        break;
                }
            }
            if (e.linkage.name != "" || e.linkage.section != "") {
                isVisible = true;
            }

            if (e.params.isGeneric() || (isInline && isVisible)) {
                const_cast<HIRFunction&>(e).saveCode = true;
            } else {
                if (isVisible) {
                    TransParams pp(state.crate.types);
                    pp.ppMethod = HIRPathParams();
                    state.enumFcn(getPath.get(), e, mv$(pp));

                    state.rv.roots.push_back(getPath.get());
                }
            }
            if (e.saveCode) {
                TransEnumerateGenericFunctionItems(state, sp, e, MonomorphStatePtr(state.crate.types), !e.params.bounds.empty());
            }
        } break;
    }
}

static void TransEnumerateExplicitLinkage(EnumState& state, const HIRModule& mod, HIRSimplePath modPath) {
    for (const auto& vi : mod.valueItems) {
        bool hasExplicitLinkage = false;
        if (const auto* function = vi.second->ent.opt_Function()) {
            hasExplicitLinkage = (*function)->linkage.name != "" || (*function)->linkage.section != "";
        } else if (const auto* stat = vi.second->ent.opt_Static()) {
            hasExplicitLinkage = (*stat)->linkage.name != "" || (*stat)->linkage.section != "";
        }
        if (hasExplicitLinkage) {
            auto path = modPath + vi.first;
            auto callback = makeCallable<TransPathCb>([path]() {
                return path;
            });
            TransEnumerateValItem(state, vi.second->ent, false, callback);
        }
    }

    for (const auto& ti : mod.modItems) {
        if (const auto* child = ti.second->ent.opt_Module()) {
            TransEnumerateExplicitLinkage(state, *child, modPath + ti.first);
        }
    }
}

static void TransEnumeratePublicMod(EnumState& state, HIRModule& mod, HIRSimplePath modPath, bool isVisible) {
    TRACE_FUNCTION_F(modPath);
    for (auto& vi : mod.valueItems) {
        bool emit = isVisible && vi.second->publicity.isGlobal();
        auto p = modPath + vi.first;
        if (std::any_of(state.crate.langItems.begin(), state.crate.langItems.end(), [&](const auto& e) {
            return e.second == p;
        })) {
            emit = true;
        }
        auto callback = makeCallable<TransPathCb>([&]() {
            return p;
        });
        TransEnumerateValItem(state, vi.second->ent, emit, callback);
    }

    for (auto& ti : mod.modItems) {
        if (auto* e = ti.second->ent.opt_Module()) {
            TransEnumeratePublicMod(state, *e, modPath + ti.first, ti.second->publicity.isGlobal());
        } else if (const HIRTrait* e = ti.second->ent.opt_Trait()) {
            auto params = HIRPathParams();
            MonomorphStatePtr ms(state.crate.types);
            ms.ppImpl = &params;
            for (const auto& vi : e->values) {
                if (const auto* fcn = vi.second.opt_Function()) {
                    TransEnumerateGenericFunctionItems(state, Span(), *fcn, ms, !e->params.bounds.empty() || !fcn->params.bounds.empty());
                }
            }
        }
    }
}

static void TransEnumeratePublicTraitImpl(EnumState& state, StaticTraitResolve& resolve, const HIRSimplePath& traitPath, /*const*/ HIRTraitImpl& impl) {
    Span sp;
    const auto& implTy = impl.type;

    TRACE_FUNCTION_F(StringView("Impl") << impl.params.fmtArgs() << StringView(" ") << traitPath << impl.traitArgs << StringView(" for ") << implTy);
    auto paramsImpl = HIRPathParams();
    MonomorphStatePtr ms(state.crate.types);
    ms.ppImpl = &paramsImpl;
    if (!impl.params.isGeneric()) {
        bool implAvailable = true;
        if (!impl.params.bounds.empty()) {
            implAvailable = resolve.findImpl(sp, traitPath, impl.traitArgs, implTy, [&](SolverResponse response) {
                return response.certainty == SolverCertainty::Proven && response.impl && response.impl->traitImpl == &impl;
            });
        }
        if (!implAvailable) {
            DEBUG(StringView("Skip conditionally unavailable concrete impl"));
            return;
        }

        auto implParams = HIRPathParams();
        auto cbMonomorph = MonomorphStatePtr(state.crate.types, implTy, &impl.traitArgs, nullptr);
        auto cbMonomorph2 = MonomorphStatePtr(state.crate.types, nullptr, &implParams, nullptr);

        // TODO: Only emit impls if the type is going to be visible to downstream crates

        const auto& trait = resolve.hirCrate().getTraitByPath(sp, traitPath);
        for (const auto& vi : trait.values) {
            TRACE_FUNCTION_F(StringView("Item ") << vi.first << StringView(" : ") << vi.second.tagStr());
            if (vi.second.is_Constant())
                ;
            else if (vi.second.is_Function() && vi.second.as_Function().params.isGeneric())
                ;
            else if (vi.first == "vtable#")
                ;
            else {
                HIRPathParams pp;
                if (vi.second.is_Function()) {
                    const auto& fcn = vi.second.as_Function();
                    bool rv = true;
                    DEBUG(StringView("Bounds = ") << fcn.params.fmtBounds());
                    for (const auto& b : fcn.params.bounds) {
                        if (!b.is_TraitBound()) {
                            continue;
                        }
                        const auto& be = b.as_TraitBound();

                        auto bTyMono = resolve.monomorphExpand(sp, be.type, cbMonomorph);
                        auto bTpMono = cbMonomorph.monomorphTraitpath(sp, be.trait, false);
                        resolve.expandAssociatedTypesTp(sp, bTpMono);

                        DEBUG(StringView("Check ") << bTyMono << StringView(": ") << bTpMono);
                        rv = resolve.findImpl(sp, bTpMono.path.path, bTpMono.path.params, bTyMono, [&](SolverResponse response) {
                            if (!response.impl) {
                                return false;
                            }
                            for (const auto& tyB : bTpMono.typeBounds) {
                                const auto& ty = response.impl->getType(state.crate.types, tyB.first.c_str(), tyB.second.atyParams);
                                DEBUG(StringView("ATY ") << tyB.first << StringView(" ") << ty << StringView(" ?= exp ") << tyB.second.type);
                                if (ty != tyB.second.type) {
                                    return false;
                                }
                            }
                            return true;
                        });
                        if (!rv) {
                            break;
                        }
                    }
                    if (!rv) {
                        continue;
                    }
                    DEBUG(StringView("Params = ") << fcn.params.fmtArgs());
                }
                auto path = HIRPath(cbMonomorph2.monomorphType(sp, implTy), HIRGenericPath(traitPath, cbMonomorph2.monomorphPathParams(sp, impl.traitArgs, false)), vi.first, mv$(pp));
                state.rv.roots.push_back(path.clone());
                TransEnumerateFillFromPathMono(state, mv$(path));
            }
        }
        for (auto& m : impl.methods) {
            if (m.second.data.params.isGeneric()) {
                m.second.data.saveCode = true;
                TransEnumerateGenericFunctionItems(state, Span(), m.second.data, ms, !m.second.data.params.bounds.empty());
            }
        }
    } else {
        for (auto& m : impl.methods) {
            m.second.data.saveCode = true;
            TransEnumerateGenericFunctionItems(state, Span(), m.second.data, ms, !impl.params.bounds.empty() || !m.second.data.params.bounds.empty());
        }
    }
}

template <typename T>
static void removeMissing(const WireBoard& wb, std::map<HIRPath, T>& target, const std::map<HIRPath, T>& tpl) {
    std::unordered_map<std::string, const HIRPath*> requiredSymbols;
    for (const auto& entry : tpl) {
        auto symbol = FMT(TransMangleValue(wb, entry.first));
        auto inserted = requiredSymbols.emplace(mv$(symbol), &entry.first);
        ASSERT_BUG(Span(), inserted.second || inserted.first->second->equalsIgnoringRegions(entry.first), StringView("Distinct paths have the same mangled name: ") << *inserted.first->second << StringView(" and ") << entry.first);
    }

    for (auto itIn = target.begin(); itIn != target.end();) {
        const auto symbol = FMT(TransMangleValue(wb, itIn->first));
        const auto required = requiredSymbols.find(symbol);
        if (required == requiredSymbols.end()) {
            DEBUG(StringView("Remove ") << itIn->first);
            itIn = target.erase(itIn);
        } else {
            ASSERT_BUG(Span(), required->second->equalsIgnoringRegions(itIn->first), StringView("Distinct paths have the same mangled name: ") << *required->second << StringView(" and ") << itIn->first);
            DEBUG(StringView("Keep ") << itIn->first);
            ++itIn;
        }
    }
}

static const HIRTypeData* implicitDropType(const HIRPath& path, const HIRSimplePath& dropTrait) {
    if (const auto* inherent = path.data.opt_UfcsInherent()) {
        return inherent->item == "#drop_glue" ? inherent->type : nullptr;
    }
    if (const auto* known = path.data.opt_UfcsKnown()) {
        return known->item == "drop" && known->trait.path == dropTrait ? known->type : nullptr;
    }
    return nullptr;
}

static void TransEnumerateCommonPostRun(EnumState& state) {
    while (!state.fcnQueue.empty()) {
        auto& fcnOut = *state.fcnQueue.front();
        state.fcnQueue.pop_front();

        TRACE_FUNCTION_F(StringView("Function ") << find_if(state.rv.functions.begin(), state.rv.functions.end(), [&](const auto& x) {
            return x.second.get() == &fcnOut;
        })->first);
        TransEnumerateFillFromFunction(state, *fcnOut.path, *fcnOut.ptr, fcnOut.pp);
    }
}

static TransList TransEnumerateCommonPost(EnumState& state) {
    TransEnumerateCommonPostRun(state);
    TransEnumerateTypes(state);

    return mv$(state.rv);
}

static bool mergeEnumeratedItems(HIRTypeInterner& types, TransList& out, TransList additions) {
    ASSERT_BUG(Span(), additions.roots.empty(), StringView("Incremental translation enumeration unexpectedly added roots"));
    ASSERT_BUG(Span(), additions.autoStatics.empty() && additions.autoFunctions.empty(), StringView("Enumeration generated translation items before TransAutoImpls"));

    bool changed = false;
    for (auto& ent : additions.functions) {
        if (auto* dst = out.addFunction(types, ent.first.clone())) {
            changed = true;
            dst->ptr = ent.second->ptr;
            dst->pp = mv$(ent.second->pp);
            dst->monomorphised = mv$(ent.second->monomorphised);
            dst->forcePrototype = ent.second->forcePrototype;
        }
    }
    for (auto& ent : additions.statics) {
        if (auto* dst = out.addStatic(types, ent.first.clone())) {
            changed = true;
            dst->ptr = ent.second->ptr;
            dst->pp = mv$(ent.second->pp);
        }
    }
    for (auto& ent : additions.constants) {
        if (auto* dst = out.addConst(types, ent.first.clone())) {
            changed = true;
            dst->ptr = ent.second->ptr;
            dst->pp = mv$(ent.second->pp);
        }
    }
    for (auto& ent : additions.vtables) {
        changed |= out.addVtable(ent.first.clone(), mv$(ent.second));
    }
    for (const auto& ty : additions.typeids) {
        changed |= out.typeids.insert(ty).second;
    }
    for (const auto& ty : additions.dropGlue) {
        changed |= out.dropGlue.insert(ty).second;
    }
    for (const auto& path : additions.constructors) {
        changed |= out.constructors.insert(path.clone()).second;
    }
    for (const auto& ty : additions.autoCloneImpls) {
        changed |= out.autoCloneImpls.insert(ty).second;
    }
    for (const auto& ty : additions.autoCloneFromImpls) {
        changed |= out.autoCloneFromImpls.insert(ty).second;
    }
    for (const auto& ty : additions.autoFnptrImpls) {
        changed |= out.autoFnptrImpls.insert(ty).second;
    }
    for (const auto& path : additions.traitObjectMethods) {
        changed |= out.traitObjectMethods.insert(path.clone()).second;
    }
    for (const auto& ent : additions.types) {
        changed |= out.addType(ent.first, ent.second);
    }
    return changed;
}

static bool transListContainsPath(const TransList& list, const HIRPath& path) {
    return list.findFunction(path) || list.statics.count(path) || list.constants.count(path) || list.vtables.count(path);
}

static void TransEnumerateTypes(EnumState& state) {
    TRACE_FUNCTION;
    Span sp;
    TypeVisitor tv{state.resolve.board(), state.rv, state.origList};

    for (const auto& path : state.rv.traitObjectMethods) {
        const auto& pe = path.data.as_UfcsKnown();
        const auto& tyDyn = pe.type->as_TraitObject();
        tv.visitType(tyDyn.trait.traitPtr->getVtableType(sp, state.crate, tyDyn));
    }

    unsigned int typesCount = 0;
    size_t constructorsVisited = 0;
    bool constructorsAdded;
    do {
        for (unsigned int i = 0; i < state.fcnsToTypeVisit.length(); i++) {
            auto* p = state.fcnsToTypeVisit[i];
            BUG_ASSERT(p->path);
            BUG_ASSERT(p->ptr);
            auto& fcnPath = *p->path;
            const auto& fcn = *p->ptr;
            const auto& pp = p->pp;

            TRACE_FUNCTION_F(StringView("Function ") << fcnPath);
            tv.visitFunction(fcnPath, fcn, pp);
        }
        state.fcnsToTypeVisit.clear();
        // TODO: Similarly restrict revisiting of statics.

        for (const auto& ent : state.rv.statics) {
            TRACE_FUNCTION_F(StringView("Enumerate static ") << ent.first);
            BUG_ASSERT(ent.second->ptr);
            const auto& stat = *ent.second->ptr;
            const auto& pp = ent.second->pp;

            tv.visitType(pp.monomorph(tv.resolve, stat.type));
        }
        for (const auto& ent : state.rv.constants) {
            TRACE_FUNCTION_F(StringView("Enumerate constant ") << ent.first);
            BUG_ASSERT(ent.second->ptr);
            const auto& stat = *ent.second->ptr;
            const auto& pp = ent.second->pp;

            tv.visitType(pp.monomorph(tv.resolve, stat.type));
        }
        for (const auto& ent : state.rv.vtables) {
            TRACE_FUNCTION_F(StringView("vtable ") << ent.first);
            const auto& ty = ent.first.data.as_UfcsKnown().type;
            const auto& gpath = ent.first.data.as_UfcsKnown().trait;
            if (gpath.path == HIRSimplePath()) {
                Vector<const HIRTypeData*> tupleTys;
                tupleTys.pushBack(state.crate.types.primitive(HIRCoreType::Usize));
                tupleTys.pushBack(state.crate.types.primitive(HIRCoreType::Usize));
                tupleTys.pushBack(state.crate.types.primitive(HIRCoreType::Usize));
                auto vtableTy = state.crate.types.tuple(std::move(tupleTys));
                tv.visitType(ty);
                tv.visitType(vtableTy);
                continue;
            }
            const auto& trait = state.crate.getTraitByPath(sp, gpath.path);

            const auto& vtableTySpath = trait.vtablePath;
            const auto& vtableRef = state.crate.getStructByPath(sp, vtableTySpath);
            HIRPathParams vtableParams = gpath.params.clone();
            for (const auto& tyIdx : trait.typeIndexes) {
                auto idx = tyIdx.second;
                if (vtableParams.types.size() <= idx) {
                    vtableParams.types.resize(idx + 1);
                }
                auto p = ent.first.clone();
                p.data.as_UfcsKnown().item = tyIdx.first;
                vtableParams.types[idx] = state.crate.types.path(mv$(p), {});
                vtableParams.types[idx] = tv.resolve.expandAssociatedTypes(sp, vtableParams.types[idx]);
            }

            DEBUG(StringView("VTable: ") << vtableTySpath << vtableParams);
            tv.visitType(ty);
            tv.visitType(state.crate.types.path(HIRPath(HIRGenericPath(vtableTySpath, mv$(vtableParams))), &vtableRef));

            if (const auto* te = ty->opt_Function()) {
                for (const auto& t : te->argTypes) {
                    tv.visitType(t);
                }
                tv.visitType(te->rettype);

                if (gpath.params.types.size() >= 1) {
                    tv.visitType(gpath.params.types[0]);
                }
            }

            if (gpath.path == state.resolve.langFn() || gpath.path == state.resolve.langFnMut() || gpath.path == state.resolve.langFnOnce()) {
                tv.visitType(gpath.params.types[0]);
            }
        }
        for (const auto& ty : state.rv.autoCloneImpls) {
            tv.visitType(ty);
        }
        constructorsVisited = state.rv.constructors.size();
        for (const auto& path : state.rv.constructors) {
            TRACE_FUNCTION_F(StringView("constructor ") << path);
            if (path.path.components().size() > 1) {
                const auto& item = state.crate.getTypeitemByPath(sp, path.path, false, true);
                if (const auto* e = item.opt_Enum()) {
                    tv.visitType(state.crate.types.path(HIRPath(HIRGenericPath(path.path.parent(), path.params.clone())), e));
                    continue;
                }
            }
            const auto& str = state.crate.getStructByPath(sp, path.path);
            tv.visitType(state.crate.types.path(HIRPath(HIRGenericPath(path.path.clone(), path.params.clone())), &str));
        }

        constructorsAdded = false;
        for (unsigned int i = typesCount; i < state.rv.types.size(); i++) {
            const auto& ent = state.rv.types[i];
            if (ent.second) {
                continue;
            }
            const auto& ty = ent.first;
            TRACE_FUNCTION_F(ty);
            if (ty->is_Path()) {
                const auto& te = ty->as_Path();
                ASSERT_BUG(sp, te.path.data.is_Generic(), StringView("Non-Generic type path after enumeration - ") << ty);
                const auto& gp = te.path.data.as_Generic();
                const HIRTraitMarkings* markingsPtr = te.binding.getTraitMarkings();
                ASSERT_BUG(sp, markingsPtr, StringView("Path binding not set correctly - ") << ty);

                if (markingsPtr->hasDropImpl && (gp.path.crateName() == state.crate.crateName || gp.params.hasParams())) {
                    TransEnumerateFillFromPathMono(state, HIRPath(ty, state.crate.getLangItemPath(sp, "drop"), "drop", HIRPathParams()));
                    constructorsAdded = true;
                }
            }

            if (const auto* ity = tv.resolve.isTypeOwnedBox(ty)) {
                const auto& p = ty->as_Path().path.data.as_Generic().params;
                tv.visitType(ity);
            }
        }
        typesCount = state.rv.types.size();

        TransEnumerateCommonPostRun(state);
        if (state.rv.constructors.size() != constructorsVisited) {
            constructorsAdded = true;
        }
    } while (constructorsAdded);
}

#include "trans_ent_ptr_tu.h"

static bool pathAlreadyEnumerated(const EnumState& state, const HIRPath& path) {
    return state.rv.functions.count(path) || state.rv.statics.count(path) || state.rv.constants.count(path) || state.rv.vtables.count(path);
}

static void evaluateTranslationParams(const Span& sp, const WireBoard& wb, const HIRCrate& crate, const HIRGenericParams* defs, HIRPathParams& params) {
    if (params.values.empty()) {
        return;
    }

    ASSERT_BUG(sp, defs, StringView("Missing const parameter definitions for ") << params);
    ASSERT_BUG(sp, params.values.size() <= defs->values.size(), StringView("Too many const parameters in ") << params << StringView(" for ") << defs->fmtArgs());
    for (size_t i = 0; i < params.values.size(); i++) {
        auto& value = params.values[i];
        if (value.is_Unevaluated()) {
            const HIRTypeData* type = defs->values[i].type;
            const HIRTypeData* tmp;
            if (monomorphiseTypeNeeded(type)) {
                MonomorphStatePtr ms(crate.types, nullptr, &params, &params);
                type = tmp = ms.monomorphType(sp, type);
                ASSERT_BUG(sp, !monomorphiseTypeNeeded(type), StringView("Generic const parameter type ") << type << StringView(" in ") << defs->fmtArgs());
            }
            ConvertHIRConstantEvaluateConstGeneric(sp, wb, crate, type, value);
        }
        ASSERT_BUG(sp, value.is_Evaluated(), StringView("Const parameter was not concrete at translation: ") << value);
    }
}

static void evaluateTranslationImplAndTraitParams(const Span& sp, const WireBoard& wb, const HIRCrate& crate, HIRPath& path, TransParams& pp) {
    evaluateTranslationParams(sp, wb, crate, pp.gdefImpl, pp.ppImpl);

    switch (path.data.tag()) {
        case HIRPathData::TAG_Generic: {
            break;
        }
        case HIRPathData::TAG_UfcsKnown: {
            auto& pe = path.data.as_UfcsKnown();
            if (pe.trait.path != HIRSimplePath()) {
                const auto& trait = crate.getTraitByPath(sp, pe.trait.path);
                evaluateTranslationParams(sp, wb, crate, &trait.params, pe.trait.params);
            }
            break;
        }
        case HIRPathData::TAG_UfcsInherent: {
            auto& pe = path.data.as_UfcsInherent();
            evaluateTranslationParams(sp, wb, crate, pp.gdefImpl, pe.implParams);
            break;
        }
        case HIRPathData::TAG_UfcsUnknown: {
            BUG(sp, StringView("UfcsUnknown at translation: ") << path);
            break;
        }
    }
}

static void evaluateTranslationItemParams(const Span& sp, const WireBoard& wb, const HIRCrate& crate, const HIRGenericParams& defs, HIRPath& path, TransParams& pp) {
    evaluateTranslationParams(sp, wb, crate, &defs, pp.ppMethod);

    switch (path.data.tag()) {
        case HIRPathData::TAG_Generic: {
            auto& pe = path.data.as_Generic();
            evaluateTranslationParams(sp, wb, crate, &defs, pe.params);
            break;
        }
        case HIRPathData::TAG_UfcsKnown: {
            auto& pe = path.data.as_UfcsKnown();
            evaluateTranslationParams(sp, wb, crate, &defs, pe.params);
            break;
        }
        case HIRPathData::TAG_UfcsInherent: {
            auto& pe = path.data.as_UfcsInherent();
            evaluateTranslationParams(sp, wb, crate, &defs, pe.params);
            break;
        }
        case HIRPathData::TAG_UfcsUnknown: {
            BUG(sp, StringView("UfcsUnknown at translation: ") << path);
            break;
        }
    }
}

static void enumerateConstRelocations(EnumState& state, const HIRPathParams& params) {
    for (const auto& value : params.values) {
        if (const auto* evaluated = value.opt_Evaluated()) {
            TransEnumerateFillFromLiteral(state, **evaluated, TransParams(state.crate.types));
        }
    }
}

static void enumerateConstRelocations(EnumState& state, const HIRPath& path, const TransParams& params) {
    enumerateConstRelocations(state, params.ppImpl);
    enumerateConstRelocations(state, params.ppMethod);
    switch (path.data.tag()) {
        case HIRPathData::TAG_Generic: {
            auto& pe = path.data.as_Generic();
            enumerateConstRelocations(state, pe.params);
            break;
        }
        case HIRPathData::TAG_UfcsKnown: {
            auto& pe = path.data.as_UfcsKnown();
            enumerateConstRelocations(state, pe.trait.params);
            enumerateConstRelocations(state, pe.params);
            break;
        }
        case HIRPathData::TAG_UfcsInherent: {
            auto& pe = path.data.as_UfcsInherent();
            enumerateConstRelocations(state, pe.params);
            enumerateConstRelocations(state, pe.implParams);
            break;
        }
        case HIRPathData::TAG_UfcsUnknown: {
            break;
        }
    }
}

static EntPtr getEntFullpath(const Span& sp, const WireBoard& wb, const HIRCrate& crate, HIRPath& path, TransParams& params) {
    TRACE_FUNCTION_F(path);
    StaticTraitResolve resolve{wb, OpaqueReveal::All};

    if (path.data.is_UfcsInherent() && path.data.as_UfcsInherent().item == "#type_id") {
        return EntPtr::make_AutoGenerate({});
    }

    MonomorphState ms(crate.types);
    params.gdefImpl = nullptr;
    StaticTraitResolve::ResolvedTraitImplPath traitImplPath;
    auto ent = resolve.getValue(sp, path, ms, /*signature_only=*/false, &params.gdefImpl, &traitImplPath);
    if (traitImplPath.type) {
        auto& pe = path.data.as_UfcsKnown();
        pe.type = traitImplPath.type;
        pe.trait.params = mv$(traitImplPath.traitParams);
        params.selfType = pe.type;
    }
    if (ms.getImplParams()) {
        params.ppImpl = ms.getImplParams()->clone();
        if (params.ppImpl.hasParams()) {
            BUG_ASSERT(params.gdefImpl);
        }
    }
    DEBUG(path << StringView(" = ") << ent.tagStr() << StringView(" w/ impl") << params.ppImpl);
    switch (ent.tag()) {
        default:
            TODO(sp, path << StringView(" was ") << ent.tagStr());
        case TypeckValuePtr::TAG_NotYetKnown: {
            const auto* pe = &path.data.as_UfcsKnown();
            if (pe->item == "vtable#") {
                DEBUG(StringView("VTable, quick return"));
                return EntPtr::make_AutoGenerate({});
            }
            bool foundBound = false;
            bool foundImpl = false;
            resolve.findImpl(sp, pe->trait.path, pe->trait.params, pe->type, [&](SolverResponse response) -> bool {
                if (!response.impl) {
                    return false;
                }
                DEBUG(StringView("[get_ent_fullpath] Found ") << response.impl->traitPath << StringView(" for ") << response.impl->type);
                if (response.impl->traitImpl) {
                    foundImpl = true;
                } else {
                    foundBound = true;
                }
                return false;
            });
            if (foundBound) {
                return EntPtr::make_AutoGenerate({});
            }
            DEBUG(StringView("NotYetKnown -> NotFound"));
            return EntPtr();
        }
        case TypeckValuePtr::TAG_Function: {
            auto& f = ent.as_Function();

            // - They need a little hack to ensure that monomorph is run
            if (const auto* pe = path.data.opt_UfcsKnown()) {
                const auto& traitRef = crate.getTraitByPath(sp, pe->trait.path);
                const auto& traitVi = traitRef.values.at(pe->item);

                if (f == &traitVi.as_Function()) {
                    DEBUG(StringView("Default trait body"));
                    params.forceMonomorphisation = true;
                }
            }
            return EntPtr{f};
        }
        case TypeckValuePtr::TAG_Static: {
            auto& f = ent.as_Static();
            return EntPtr{f};
        }
        case TypeckValuePtr::TAG_Constant: {
            auto& f = ent.as_Constant();
            return EntPtr{f};
        }
        case TypeckValuePtr::TAG_StructConstructor: {
            auto& _ = ent.as_StructConstructor();
            return EntPtr::make_AutoGenerate({});
        }
        case TypeckValuePtr::TAG_EnumConstructor: {
            auto& _ = ent.as_EnumConstructor();
            return EntPtr::make_AutoGenerate({});
        }
    }
    UNREACHABLE();
}

static void TransEnumerateFillFromPath(EnumState& state, const HIRPath& path, const TransParams& pp) {
    auto pathMono = pp.monomorph(state.resolve, path);
    TransEnumerateFillFromPathMono(state, mv$(pathMono));
}

static void TransEnumerateFillFromPathMono(EnumState& state, HIRPath pathMono) {
    Span sp;
    bindTranslationNominals(state.crate, pathMono);
    TRACE_FUNCTION_F(pathMono);
    ASSERT_BUG(sp, !monomorphisePathNeeded(pathMono), StringView("Path ") << pathMono << StringView(" is generic"));
    // TODO: If already in the list, return early
    if (pathAlreadyEnumerated(state, pathMono)) {
        DEBUG(StringView("> Already enumerated"));
        return;
    }

    TransParams subPp(state.crate.types, sp);
    switch (pathMono.data.tag()) {
        case HIRPathData::TAG_Generic: {
            auto& pe = pathMono.data.as_Generic();
            subPp.ppMethod = pe.params.clone();
            break;
        }
        case HIRPathData::TAG_UfcsKnown: {
            auto& pe = pathMono.data.as_UfcsKnown();
            subPp.ppMethod = pe.params.clone();
            subPp.selfType = pe.type;
            break;
        }
        case HIRPathData::TAG_UfcsInherent: {
            auto& pe = pathMono.data.as_UfcsInherent();
            subPp.ppMethod = pe.params.clone();
            subPp.ppImpl = pe.implParams.clone();
            subPp.selfType = pe.type;
            break;
        }
        case HIRPathData::TAG_UfcsUnknown: {
            BUG(sp, StringView("UfcsUnknown - ") << pathMono);
            break;
        }
    }

    if (const auto* pe = pathMono.data.opt_UfcsKnown()) {
        if (const auto* tyDyn = pe->type->opt_TraitObject()) {
            if (pe->item != "vtable#" && tyDyn->trait.traitPtr->getVtableValueIndex(pe->trait, pe->item) > 0) {
                state.rv.traitObjectMethods.insert(mv$(pathMono));
                return;
            }
        }
    }

    auto itemRef = getEntFullpath(sp, state.resolve.board(), state.crate, pathMono, subPp);
    DEBUG(StringView("item_ref.tag_str() = ") << itemRef.tagStr());
    DEBUG(StringView("sub_pp.pp_method = ") << subPp.ppMethod);
    DEBUG(StringView("sub_pp.pp_impl = ") << subPp.ppImpl);
    evaluateTranslationImplAndTraitParams(sp, state.resolve.board(), state.crate, pathMono, subPp);
    if (pathAlreadyEnumerated(state, pathMono)) {
        DEBUG(StringView("> Already enumerated after const evaluation"));
        return;
    }

    auto activePath = state.activePaths.insert(pathMono.clone());
    if (!activePath.second) {
        DEBUG(StringView("> Already being enumerated"));
        return;
    }
    STD_DEFER {
        state.activePaths.erase(activePath.first);
    };

    enumerateConstRelocations(state, pathMono, subPp);
    switch (itemRef.tag()) {
        case EntPtr::TAG_NotFound: {
            BUG(sp, StringView("Item not found for ") << pathMono);
            break;
        }
        case EntPtr::TAG_AutoGenerate: {
            if (pathAlreadyEnumerated(state, pathMono)) {
                DEBUG(StringView("> Already enumerated after const evaluation"));
                return;
            }
            if (pathMono.data.is_Generic()) {
                // TODO: Add to a list of required constructors
                state.rv.constructors.insert(mv$(pathMono.data.as_Generic()));
            } else if (pathMono.data.is_UfcsInherent() && pathMono.data.as_UfcsInherent().item == "#type_id") {
                state.rv.typeids.insert(pathMono.data.as_UfcsInherent().type);
            } else if (pathMono.data.is_UfcsKnown() && pathMono.data.as_UfcsKnown().item == "vtable#") {
                if (state.rv.addVtable(pathMono.clone(), TransParams(state.crate.types))) {
                    TransEnumerateFillFromVTable(state, mv$(pathMono), subPp);
                }
            } else if (pathMono.data.is_UfcsKnown() && pathMono.data.as_UfcsKnown().type->is_TraitObject()) {
                state.rv.traitObjectMethods.insert(mv$(pathMono));
            } else if (pathMono.data.is_UfcsKnown() && pathMono.data.as_UfcsKnown().type->is_Function() && (pathMono.data.as_UfcsKnown().trait.path == state.crate.getLangItemPathOpt("fn") || pathMono.data.as_UfcsKnown().trait.path == state.crate.getLangItemPathOpt("fn_mut") || pathMono.data.as_UfcsKnown().trait.path == state.crate.getLangItemPathOpt("fn_once"))) {
            } else if (pathMono.data.is_UfcsKnown() && pathMono.data.as_UfcsKnown().type->is_NamedFunction() && (pathMono.data.as_UfcsKnown().trait.path == state.crate.getLangItemPathOpt("fn") || pathMono.data.as_UfcsKnown().trait.path == state.crate.getLangItemPathOpt("fn_mut") || pathMono.data.as_UfcsKnown().trait.path == state.crate.getLangItemPathOpt("fn_once"))) {
                TransEnumerateFillFromPath(state, pathMono.data.as_UfcsKnown().type->as_NamedFunction().path, subPp);
            } else if (pathMono.data.is_UfcsKnown() && pathMono.data.as_UfcsKnown().type->is_Function() && pathMono.data.as_UfcsKnown().trait.path == state.crate.getLangItemPathOpt("fn_ptr_trait")) {
                state.rv.autoFnptrImpls.insert(pathMono.data.as_UfcsKnown().type);
            } else if (pathMono.data.is_UfcsKnown() && pathMono.data.as_UfcsKnown().trait == state.crate.getLangItemPathOpt("clone")) {
                const auto& pe = pathMono.data.as_UfcsKnown();
                ASSERT_BUG(sp, pe.item == "clone" || pe.item == "clone_from", StringView("Unexpected Clone method called, ") << pathMono);
                const auto& innerTy = pe.type;
                ::StaticTraitResolve resolve{state.resolve.board(), OpaqueReveal::All};
                if (!resolve.typeIsCopy(sp, innerTy)) {
                    auto enumImpl = [&](const HIRTypeData* ity) {
                        if (!resolve.typeIsCopy(sp, ity)) {
                            auto innerPp = HIRPathParams();
                            TransEnumerateFillFromPathMono(state, HIRPath(ity, pe.trait.clone(), pe.item, mv$(innerPp)));
                        }
                    };
                    if (const auto* te = innerTy->opt_Tuple()) {
                        for (const auto& ity : *te) {
                            enumImpl(ity);
                        }
                    } else if (const auto* te = innerTy->opt_Array()) {
                        enumImpl(te->inner);
                    } else if (((*innerTy).is_Path() && ((*innerTy).as_Path().isClosure()))) {
                        const auto& gp = innerTy->as_Path().path.data.as_Generic();
                        const auto& str = state.crate.getStructByPath(sp, gp.path);
                        auto p = TransParams::newImpl(state.crate.types, sp, {}, gp.params.clone());
                        for (const auto& fld : str.data.as_Tuple()) {
                            const HIRTypeData* tmp;
                            const auto& tyM = monomorphiseTypeNeeded(fld.ent) ? (tmp = p.monomorph(resolve, fld.ent)) : fld.ent;
                            enumImpl(tyM);
                        }
                    } else {
                        BUG(sp, StringView("Unhandled magic clone in enumerate - ") << innerTy);
                    }
                }
                state.rv.autoCloneImpls.insert(innerTy);
                if (pe.item == "clone_from") {
                    state.rv.autoCloneFromImpls.insert(innerTy);
                }
            } else {
                BUG(sp, StringView("AutoGenerate returned for unknown path type - ") << pathMono);
            }
            break;
        }
        case EntPtr::TAG_Function: {
            auto& e = itemRef.as_Function();
            evaluateTranslationItemParams(sp, state.resolve.board(), state.crate, e->params, pathMono, subPp);
            if (pathAlreadyEnumerated(state, pathMono)) {
                DEBUG(StringView("> Already enumerated after const evaluation"));
                return;
            }
            state.enumFcn(mv$(pathMono), *e, mv$(subPp));
            break;
        }
        case EntPtr::TAG_Static: {
            auto& e = itemRef.as_Static();
            evaluateTranslationItemParams(sp, state.resolve.board(), state.crate, e->params, pathMono, subPp);
            if (pathAlreadyEnumerated(state, pathMono)) {
                DEBUG(StringView("> Already enumerated after const evaluation"));
                return;
            }
            if (auto* ptr = state.rv.addStatic(state.crate.types, mv$(pathMono))) {
                TransEnumerateFillFromStatic(state, *e, *ptr, mv$(subPp));
            }
            break;
        }
        case EntPtr::TAG_Constant: {
            auto& e = itemRef.as_Constant();
            evaluateTranslationItemParams(sp, state.resolve.board(), state.crate, e->params, pathMono, subPp);
            if (pathAlreadyEnumerated(state, pathMono)) {
                DEBUG(StringView("> Already enumerated after const evaluation"));
                return;
            }
            switch (e->valueState) {
                case HIRConstant::ValueState::InProgress:
                    BUG(sp, StringView("Constant still marked in-progress at translation: ") << pathMono);
                case HIRConstant::ValueState::Unknown:
                    BUG(sp, StringView("Unevaluated constant: ") << pathMono);
                case HIRConstant::ValueState::Generic:
                    if (auto* slot = state.rv.addConst(state.crate.types, mv$(pathMono))) {
                        slot->ptr = e;
                        slot->pp = std::move(subPp);
                    }
                    break;
                case HIRConstant::ValueState::Known:
                    TransEnumerateFillFromLiteral(state, e->valueRes, subPp);
                    break;
            }
            break;
        }
    }
}

static void TransEnumerateFillFromMIRLValue(MIREnumCache& state, const MIRLValue& lv) {
    if (lv.root.is_Static()) {
        state.insertPath(lv.root.as_Static());
    }
}

static void TransEnumerateFillFromMIRConstant(MIREnumCache& state, const MIRConstant& c) {
    switch (c.tag()) {
        case MIRConstant::TAG_Int: {
            break;
        }
        case MIRConstant::TAG_Uint: {
            break;
        }
        case MIRConstant::TAG_Float: {
            break;
        }
        case MIRConstant::TAG_Bool: {
            break;
        }
        case MIRConstant::TAG_Bytes: {
            break;
        }
        case MIRConstant::TAG_StaticString: {
            break;
        }
        case MIRConstant::TAG_Encoded: {
            auto& ce = c.as_Encoded();
            for (const auto& reloc : ce.value.relocations) {
                if (reloc.p) {
                    state.insertPath(*reloc.p);
                }
            }
            break;
        }
        case MIRConstant::TAG_Const: {
            auto& ce = c.as_Const();
            state.insertPath(*ce.p);
            break;
        }
        case MIRConstant::TAG_Generic: {
            break;
        }
        case MIRConstant::TAG_Function: {
            auto& ce = c.as_Function();
            state.insertPath(*ce.p);
            break;
        }
        case MIRConstant::TAG_ItemAddr: {
            auto& ce = c.as_ItemAddr();
            if (ce) {
                state.insertPath(*ce);
            }
            break;
        }
    }
}

static void TransEnumerateFillFromMIRParam(MIREnumCache& state, const MIRParam& p) {
    switch (p.tag()) {
        case MIRParam::TAG_LValue: {
            auto& e = p.as_LValue();
            TransEnumerateFillFromMIRLValue(state, e);
            break;
        }
        case MIRParam::TAG_Borrow: {
            auto& e = p.as_Borrow();
            TransEnumerateFillFromMIRLValue(state, e.val);
            break;
        }
        case MIRParam::TAG_Constant: {
            auto& e = p.as_Constant();
            TransEnumerateFillFromMIRConstant(state, e);
            break;
        }
    }
}

static void TransEnumerateFillFromMIR(MIREnumCache& state, const MIRFunction& code) {
    TRACE_FUNCTION_F(StringView(""));
    for (const auto& ty : code.locals) {
        visitTyWith(ty, [&state](const HIRTypeData* t) -> bool {
            if (const auto* te = t->opt_NamedFunction()) {
                state.insertPath(te->path);
            }
            return false;
        });
    }
    for (const auto& bb : code.blocks) {
        for (const auto& stmt : bb.statements) {
            switch (stmt.tag()) {
                case MIRStatement::TAG_Assign: {
                    auto& se = stmt.as_Assign();
                    DEBUG(StringView("- ") << se.dst << StringView(" = ") << se.src);
                    TransEnumerateFillFromMIRLValue(state, se.dst);
                    switch (se.src.tag()) {
                        case MIRRValue::TAG_Use: {
                            auto& e = se.src.as_Use();
                            TransEnumerateFillFromMIRLValue(state, e);
                            break;
                        }
                        case MIRRValue::TAG_Constant: {
                            auto& e = se.src.as_Constant();
                            TransEnumerateFillFromMIRConstant(state, e);
                            break;
                        }
                        case MIRRValue::TAG_SizedArray: {
                            auto& e = se.src.as_SizedArray();
                            TransEnumerateFillFromMIRParam(state, e.val);
                            break;
                        }
                        case MIRRValue::TAG_Borrow: {
                            auto& e = se.src.as_Borrow();
                            TransEnumerateFillFromMIRLValue(state, e.val);
                            break;
                        }
                        case MIRRValue::TAG_Cast: {
                            auto& e = se.src.as_Cast();
                            TransEnumerateFillFromMIRLValue(state, e.val);
                            break;
                        }
                        case MIRRValue::TAG_BinOp: {
                            auto& e = se.src.as_BinOp();
                            TransEnumerateFillFromMIRParam(state, e.valL);
                            TransEnumerateFillFromMIRParam(state, e.valR);
                            break;
                        }
                        case MIRRValue::TAG_UniOp: {
                            auto& e = se.src.as_UniOp();
                            TransEnumerateFillFromMIRLValue(state, e.val);
                            break;
                        }
                        case MIRRValue::TAG_DstMeta: {
                            auto& e = se.src.as_DstMeta();
                            TransEnumerateFillFromMIRLValue(state, e.val);
                            break;
                        }
                        case MIRRValue::TAG_DstPtr: {
                            auto& e = se.src.as_DstPtr();
                            TransEnumerateFillFromMIRLValue(state, e.val);
                            break;
                        }
                        case MIRRValue::TAG_MakeDst: {
                            auto& e = se.src.as_MakeDst();
                            TransEnumerateFillFromMIRParam(state, e.ptrVal);
                            TransEnumerateFillFromMIRParam(state, e.metaVal);
                            break;
                        }
                        case MIRRValue::TAG_Tuple: {
                            auto& e = se.src.as_Tuple();
                            for (const auto& val : e.vals) {
                                TransEnumerateFillFromMIRParam(state, val);
                            }
                            break;
                        }
                        case MIRRValue::TAG_Array: {
                            auto& e = se.src.as_Array();
                            for (const auto& val : e.vals) {
                                TransEnumerateFillFromMIRParam(state, val);
                            }
                            break;
                        }
                        case MIRRValue::TAG_UnionVariant: {
                            auto& e = se.src.as_UnionVariant();
                            TransEnumerateFillFromMIRParam(state, e.val);
                            break;
                        }
                        case MIRRValue::TAG_EnumVariant: {
                            auto& e = se.src.as_EnumVariant();
                            for (const auto& val : e.vals) {
                                TransEnumerateFillFromMIRParam(state, val);
                            }
                            break;
                        }
                        case MIRRValue::TAG_Struct: {
                            auto& e = se.src.as_Struct();
                            for (const auto& val : e.vals) {
                                TransEnumerateFillFromMIRParam(state, val);
                            }
                            break;
                        }
                    }
                    break;
                }
                case MIRStatement::TAG_Asm2: {
                    auto& e = stmt.as_Asm2();
                    for (auto& p : e.params) {
                        switch (p.tag()) {
                            case MIRAsmParam::TAG_Const: {
                                auto& v = p.as_Const();
                                TransEnumerateFillFromMIRConstant(state, v);
                                break;
                            }
                            case MIRAsmParam::TAG_Sym: {
                                auto& v = p.as_Sym();
                                state.insertPath(v);
                                break;
                            }
                            case MIRAsmParam::TAG_Reg: {
                                auto& v = p.as_Reg();
                                if (v.input) {
                                    TransEnumerateFillFromMIRParam(state, *v.input);
                                }
                                if (v.output) {
                                    TransEnumerateFillFromMIRLValue(state, *v.output);
                                }
                                break;
                            }
                            case MIRAsmParam::TAG_Label: {
                                break;
                            }
                        }
                    }
                    break;
                }
                case MIRStatement::TAG_Asm: {
                    auto& se = stmt.as_Asm();
                    DEBUG(StringView("- llvm_asm! ..."));
                    for (const auto& v : se.inputs) {
                        TransEnumerateFillFromMIRLValue(state, v.second);
                    }
                    for (const auto& v : se.outputs) {
                        TransEnumerateFillFromMIRLValue(state, v.second);
                    }
                    break;
                }
                case MIRStatement::TAG_SetDropFlag: {
                    break;
                }
                case MIRStatement::TAG_SaveDropFlag: {
                    auto& se = stmt.as_SaveDropFlag();
                    TransEnumerateFillFromMIRLValue(state, se.slot);
                    break;
                }
                case MIRStatement::TAG_LoadDropFlag: {
                    auto& se = stmt.as_LoadDropFlag();
                    TransEnumerateFillFromMIRLValue(state, se.slot);
                    break;
                }
                case MIRStatement::TAG_ScopeEnd: {
                    break;
                }
            }
        }
        DEBUG(StringView("> ") << bb.terminator);
        switch (bb.terminator.tag()) {
            case MIRTerminator::TAG_Incomplete: {
                break;
            }
            case MIRTerminator::TAG_Return: {
                break;
            }
            case MIRTerminator::TAG_UnwindResume: {
                break;
            }
            case MIRTerminator::TAG_UnwindTerminate: {
                break;
            }
            case MIRTerminator::TAG_Unreachable: {
                break;
            }
            case MIRTerminator::TAG_Goto: {
                break;
            }
            case MIRTerminator::TAG_If: {
                auto& e = bb.terminator.as_If();
                TransEnumerateFillFromMIRLValue(state, e.cond);
                break;
            }
            case MIRTerminator::TAG_Switch: {
                auto& e = bb.terminator.as_Switch();
                TransEnumerateFillFromMIRLValue(state, e.val);
                break;
            }
            case MIRTerminator::TAG_SwitchValue: {
                auto& e = bb.terminator.as_SwitchValue();
                TransEnumerateFillFromMIRLValue(state, e.val);
                break;
            }
            case MIRTerminator::TAG_Drop: {
                auto& e = bb.terminator.as_Drop();
                TransEnumerateFillFromMIRLValue(state, e.slot);
                break;
            }
            case MIRTerminator::TAG_Call: {
                auto& e = bb.terminator.as_Call();
                TransEnumerateFillFromMIRLValue(state, e.retVal);
                switch (e.fcn.tag()) {
                    case MIRCallTarget::TAG_Value: {
                        auto& e2 = e.fcn.as_Value();
                        TransEnumerateFillFromMIRLValue(state, e2);
                        break;
                    }
                    case MIRCallTarget::TAG_Path: {
                        auto& e2 = e.fcn.as_Path();
                        state.insertPath(e2);
                        break;
                    }
                    case MIRCallTarget::TAG_Intrinsic: {
                        auto& e2 = e.fcn.as_Intrinsic();
                        if (e2.name == "type_id") {
                            state.insertTypeid(e2.params.types.at(0));
                        } else if (e2.name == "drop_in_place") {
                            state.insertDestructorType(e2.params.types.at(0));
                        }
                        break;
                    }
                }
                for (const auto& arg : e.args) {
                    TransEnumerateFillFromMIRParam(state, arg);
                }
                break;
            }
            case MIRTerminator::TAG_TailCall: {
                auto& e = bb.terminator.as_TailCall();
                switch (e.fcn.tag()) {
                    case MIRCallTarget::TAG_Value: {
                        auto& e2 = e.fcn.as_Value();
                        TransEnumerateFillFromMIRLValue(state, e2);
                        break;
                    }
                    case MIRCallTarget::TAG_Path: {
                        auto& e2 = e.fcn.as_Path();
                        state.insertPath(e2);
                        break;
                    }
                    case MIRCallTarget::TAG_Intrinsic: {
                        auto& e2 = e.fcn.as_Intrinsic();
                        if (e2.name == "type_id") {
                            state.insertTypeid(e2.params.types.at(0));
                        } else if (e2.name == "drop_in_place") {
                            state.insertDestructorType(e2.params.types.at(0));
                        }
                        break;
                    }
                }
                for (const auto& arg : e.args) {
                    TransEnumerateFillFromMIRParam(state, arg);
                }
                break;
            }
            case MIRTerminator::TAG_Asm2: {
                auto& e = bb.terminator.as_Asm2();
                for (const auto& p : e.params) {
                    if (const auto* c = p.opt_Const()) {
                        TransEnumerateFillFromMIRConstant(state, *c);
                    } else if (const auto* s = p.opt_Sym()) {
                        state.insertPath(*s);
                    } else if (const auto* r = p.opt_Reg()) {
                        if (r->input) {
                            TransEnumerateFillFromMIRParam(state, *r->input);
                        }
                        if (r->output) {
                            TransEnumerateFillFromMIRLValue(state, *r->output);
                        }
                    }
                }
                break;
            }
        }
    }
}

static void TransEnumerateFillFromVTable(EnumState& state, HIRPath vtablePath, const TransParams& pp) {
    Span sp;
    const auto& type = vtablePath.data.as_UfcsKnown().type;
    const auto& traitPath = vtablePath.data.as_UfcsKnown().trait;
    if (traitPath == HIRSimplePath()) {
        // TODO: Ensure that the drop glue is available
        return;
    }
    const auto& tr = state.crate.getTraitByPath(Span(), traitPath.path);

    ASSERT_BUG(sp, !type->is_Slice(), StringView("Getting vtable for unsized type - ") << vtablePath);
    ASSERT_BUG(sp, !type->is_TraitObject(), StringView("Getting vtable for unsized type - ") << vtablePath);

    auto monomorphCbTrait = MonomorphStatePtr(state.crate.types, type, &traitPath.params, nullptr);
    for (const auto& m : tr.valueIndexes) {
        DEBUG(StringView("- ") << m.second.first << StringView(" = ") << m.second.second << StringView(" :: ") << m.first);
        auto gpath = monomorphCbTrait.monomorphGenericpath(sp, m.second.second, false);
        const auto& fcn = state.crate.getTraitByPath(sp, gpath.path).values.at(m.first).as_Function();
        auto methodPath = HIRPath(type, gpath.clone(), m.first, HIRPathParams());
        state.resolve.expandAssociatedTypesPath(sp, methodPath);
        TransEnumerateFillFromPathMono(state, methodPath.clone());
    }
    for (const auto& ptPath : tr.allParentTraits) {
        ASSERT_BUG(sp, ptPath.traitPtr, StringView("Unset trait pointer - ") << ptPath);
        const auto& pt = *ptPath.traitPtr;
        if (pt.vtablePath != HIRSimplePath()) {
            auto ptMono = MonomorphStatePtr(state.crate.types, type, &traitPath.params, nullptr).monomorphGenericpath(sp, ptPath.path);
            auto ptVtablePath = HIRPath(type, mv$(ptMono), vtablePath.data.as_UfcsKnown().item);
            state.rv.addVtable(mv$(ptVtablePath), TransParams(state.crate.types));
        }
    }
}

static void TransEnumerateFillFromLiteral(EnumState& state, const EncodedLiteral& lit, const TransParams& pp) {
    for (const auto& r : lit.relocations) {
        if (r.p) {
            // TODO: Replace lifetimes
            TransEnumerateFillFromPath(state, *r.p, pp);
        }
    }
}

static void TransEnumerateFillFromFunction(EnumState& state, const HIRPath& p, const HIRFunction& function, const TransParams& pp) {
    TRACE_FUNCTION_F(StringView("Function ") << p << StringView(" pp=") << pp.ppImpl << StringView(" + ") << pp.ppMethod);
    if (!function.code.mir) {
        if (function.linkage.name != "") {
            auto it = state.linkFunctions.find(function.linkage.name);
            if (it != state.linkFunctions.end()) {
                state.enumFcn(HIRPath(it->second.first), *it->second.second, TransParams(state.crate.types, pp.sp));
            }
        }
    } else if (state.origList) {
        const auto* transFcn = state.origList->findFunction(p);
        if (transFcn) {
            if (transFcn->monomorphised.code) {
                DEBUG(StringView("Monomorphised"));
                MIREnumCache ec;
                TransEnumerateFillFromMIR(ec, *transFcn->monomorphised.code);
                ec.apply(state, pp);
            } else if (transFcn->ptr->code.mir) {
                DEBUG(StringView("Concrete"));
                MIREnumCache ec;
                TransEnumerateFillFromMIR(ec, *transFcn->ptr->code.mir);
                ec.apply(state, pp);
            } else {
                DEBUG(StringView("No code"));
            }
        } else {
            ASSERT_BUG(Span(), transFcn, StringView("Missing ") << p << StringView(" in input TransList?"));
        }
    } else {
        const auto& mirFcn = *function.code.mir;
        if (!mirFcn.transEnumState) {
            auto* esp = new MIREnumCache();
            TransEnumerateFillFromMIR(*esp, *function.code.mir);
            mirFcn.transEnumState = MIREnumCachePtr(esp);
        }
        // TODO: Ensure that all types have drop glue generated too? (Iirc this is unconditional currently)
        mirFcn.transEnumState->apply(state, pp);
    }
}

static void TransEnumerateFillFromStatic(EnumState& state, const HIRStatic& item, TransListStatic& outStat, TransParams pp) {
    if (item.params.isGeneric()) {
        MIREnumCache es;
        TransEnumerateFillFromMIR(es, *item.value.mir);
        es.apply(state, pp);
    } else if (item.type->is_Infer()) {
        BUG(Span(), StringView("Enumerating static with no assigned type (unused elevated literal)"));
    } else if (item.valueGenerated) {
        TransEnumerateFillFromLiteral(state, item.valueRes, pp);
    }
    outStat.ptr = &item;
    outStat.pp = mv$(pp);
}

void TransAutoImpls(const WireBoard& wb, HIRCrate& crate, TransList& transList) {
    State state{wb, crate, transList};

    {
        for (const auto& ty : transList.autoCloneImpls) {
            state.doneList.insert(ty);
            TransAutoImplClone(state, ty);
        }

        while (!state.todoList.empty()) {
            auto ty = std::move(state.todoList.front());
            state.todoList.pop_back();

            TransAutoImplClone(state, mv$(ty));
        }

        auto implListIt = crate.traitImpls.find(state.langClone);
        for (const auto& ty : state.doneList) {
            BUG_ASSERT(implListIt != crate.traitImpls.end());
            // TODO: Find a way of turning a set into a vector so items can be erased.

            const auto* implList = implListIt->second.getListForType(ty);
            ASSERT_BUG(Span(), implList, StringView("No impl list of Clone for ") << ty);
            auto& impl = **std::find_if(implList->begin(), implList->end(), [&](const auto& i) {
                return i->type == ty;
            });

            auto bind = [&](const RcString& method) {
                auto p = HIRPath(ty, HIRGenericPath(state.langClone), method);
                auto* e = transList.addFunction(crate.types, p.clone());
                if (!e) {
                    DEBUG(p << StringView(" was already enumerated"));
                    return;
                }
                auto m = impl.methods.find(method);
                ASSERT_BUG(Span(), m != impl.methods.end(), StringView("Generated Clone for ") << ty << StringView(" has no `") << method << StringView("`"));
                e->ptr = &m->second.data;
            };
            bind("clone");
            if (transList.autoCloneFromImpls.count(ty)) {
                bind("clone_from");
            }
        }
        transList.autoCloneImpls.clear();
        transList.autoCloneFromImpls.clear();
    }

    if (!transList.autoFnptrImpls.empty()) {
        const auto& langFnPtr = crate.getLangItemPath(Span(), "fn_ptr_trait");
        for (const auto& ty : transList.autoFnptrImpls) {
            auto outTy = state.crate.types.pointer(HIRBorrowType::Shared, state.crate.types.unit());
            MIRFunction mirFcn;

            MIRBasicBlock bb;
            bb.statements.push_back(MIRStatement::make_Assign({MIRLValue::newReturn(), MIRRValue::make_Cast({MIRLValue::newArgument(0), outTy})}));
            bb.terminator = MIRTerminator::make_Return({});
            mirFcn.blocks.push_back(std::move(bb));

            HIRFunction fcn{
                HIRFunction::Receiver::Value,
                HIRGenericParams{},
                /*m_args=*/::makeVec1(std::make_pair(HIRPattern(HIRPatternBinding(false, HIRPatternBinding::Type::Move, "self", 0), HIRPattern::Data::make_Any({})), ty)),
                /*m_return=*/std::move(outTy),
                HIRExprPtr{}
            };
            fcn.code.mir = generatedBody(mv$(mirFcn));

            HIRTraitImpl impl;
            impl.type = ty;
            impl.methods.insert(std::make_pair(RcString::newInterned("addr"), HIRTraitImpl::ImplEnt<HIRFunction>{false, std::move(fcn)}));

            auto& list = state.crate.traitImpls[langFnPtr].getListForTypeMut(impl.type);
            list.push_back(box$(impl));
            state.crate.allTraitImpls[langFnPtr].getListForTypeMut(list.back()->type).push_back(list.back().get());

            {
                auto p = HIRPath(ty, HIRGenericPath(langFnPtr), "addr");
                auto e = transList.addFunction(crate.types, std::move(p));

                auto& impl = *list.back();
                BUG_ASSERT(impl.methods.size() == 1);
                e->ptr = &impl.methods.begin()->second.data;
            }
        }
        transList.autoFnptrImpls.clear();
    }

    {
        TRACE_FUNCTION_F(StringView("Trait object methods"));
        transList.autoFunctions.reserve(transList.autoFunctions.size() + transList.traitObjectMethods.size());
        for (const auto& path : transList.traitObjectMethods) {
            DEBUG(path);
            Span sp;
            const auto& pe = path.data.as_UfcsKnown();
            const auto& traitPath = pe.trait;
            const auto& name = pe.item;
            const auto& tyDyn = pe.type->as_TraitObject();

            const auto& trait = crate.getTraitByPath(sp, traitPath.path);
            const auto& fcnDef = trait.values.at(name).as_Function();

            unsigned vtableIdx = tyDyn.trait.traitPtr->getVtableValueIndex(traitPath, name);
            ASSERT_BUG(sp, vtableIdx > 0, StringView("Calling method '") << name << StringView("' from ") << traitPath << StringView(" through ") << pe.type << StringView(" which isn't in the vtable"));

            auto pp = fcnDef.params.makeNopParams(crate.types, 1);
            MonomorphStatePtr ms(crate.types, pe.type, &traitPath.params, &pp);

            HIRFunction newFcn;
            newFcn.markings.trackCaller = fcnDef.markings.trackCaller;
            newFcn.markings.alignment = fcnDef.markings.alignment;
            newFcn.returnType = ms.monomorphType(sp, fcnDef.returnType);
            newFcn.returnType = state.resolve.expandAssociatedTypes(sp, newFcn.returnType);
            for (const auto& arg : fcnDef.args) {
                newFcn.args.push_back(std::make_pair(HIRPattern(), ms.monomorphType(sp, arg.second)));
                newFcn.args.back().second = state.resolve.expandAssociatedTypes(sp, newFcn.args.back().second);
            }
            ASSERT_BUG(sp, !newFcn.args.empty(), StringView("Trait object method with no arguments?!"));

            newFcn.code.mir = generatedBody();
            Builder builder(state, *newFcn.code.mir);

            MIRLValue lvSelf = MIRLValue::newArgument(0);
            MIRLValue lvPtr;
            switch (fcnDef.receiver) {
                case HIRFunction::Receiver::Value: {
                    auto& selfTy = newFcn.args.front().second;
                    selfTy = crate.types.borrow(HIRBorrowType::Owned, selfTy);
                    lvPtr = builder.addLocal(crate.types.borrow(HIRBorrowType::Owned, crate.types.unit()));
                    builder.pushStmtAssign(lvPtr.clone(), MIRRValue::make_DstPtr({lvSelf.clone()}));
                    DEBUG(StringView("<dyn ") << traitPath << StringView(">::") << name << StringView(" - By-Value"));
                } break;
                case HIRFunction::Receiver::BorrowOwned:
                case HIRFunction::Receiver::BorrowUnique:
                case HIRFunction::Receiver::BorrowShared: {
                    ASSERT_BUG(sp, newFcn.args.front().second->is_Borrow(), newFcn.args.front().second);
                    auto bt = newFcn.args.front().second->as_Borrow().type;
                    DEBUG(StringView("<dyn ") << traitPath << StringView(">::") << name << StringView(" - By-borrow"));
                    lvPtr = builder.addLocal(crate.types.borrow(bt, crate.types.unit()));
                    builder.pushStmtAssign(lvPtr.clone(), MIRRValue::make_DstPtr({lvSelf.clone()}));
                } break;
                case HIRFunction::Receiver::Box: {
                    // TODO: What is the real reciver here? (for the MIR)

                    auto gpath = newFcn.args.front().second->as_Path().path.data.as_Generic().clone();
                    gpath.params.types.at(0) = crate.types.unit();
                    auto ty = crate.types.path(mv$(gpath), newFcn.args.front().second->as_Path().binding.clone());
                    lvPtr = getUnitPtr(sp, builder, mv$(ty), MIRLValue::newArgument(0), lvSelf);
                } break;
                case HIRFunction::Receiver::Custom: {
                    ASSERT_BUG(sp, fcnDef.receiverType, StringView("Custom receiver without a receiver type"));
                    auto thinReceiver = cloneTyWith(crate.types, sp, newFcn.args.front().second, [&](const HIRTypeData* ty) -> const HIRTypeData* {
                        if (ty == pe.type) {
                            return crate.types.unit();
                        }
                        return nullptr;
                    });
                    lvPtr = getUnitPtr(sp, builder, mv$(thinReceiver), MIRLValue::newArgument(0), lvSelf);
                } break;
                default:
                    TODO(sp, StringView("Handle different receiver types: <dyn ") << traitPath << StringView(">::") << name << StringView(" - self: ") << newFcn.args.front().second);
            }

            auto lvVtable = builder.addLocal(crate.types.borrow(HIRBorrowType::Shared, tyDyn.trait.traitPtr->getVtableType(sp, crate, tyDyn)));
            builder.pushStmtAssign(lvVtable.clone(), MIRRValue::make_DstMeta({mv$(lvSelf)}));
            std::vector<MIRParam> callArgs;
            callArgs.push_back(mv$(lvPtr));
            for (size_t i = 1; i < fcnDef.args.size(); i++) {
                callArgs.push_back(MIRLValue::newArgument(i));
            }
            builder.terminateCall(MIRLValue::newReturn(), MIRLValue::newField(MIRLValue::newDeref(mv$(lvVtable)), vtableIdx), mv$(callArgs), 1, 2, fcnDef.markings.trackCaller);
            builder.ensureOpen();
            builder.terminateBlock(MIRTerminator::make_Return({}));
            builder.ensureOpen();
            builder.mir.blocks.back().isCleanup = true;
            builder.terminateBlock(MIRTerminator::make_UnwindResume({}));

            transList.autoFunctions.push_back(box$(newFcn));
            auto* e = transList.addFunction(crate.types, path.clone());
            if (e) {
                e->ptr = transList.autoFunctions.back().get();
            } else {
                transList.autoFunctions.pop_back();
            }
        }
        transList.traitObjectMethods.clear();
    }

    {
        TRACE_FUNCTION_F(StringView("VTables"));
        transList.autoStatics.reserve(transList.vtables.size());
        for (const auto& ent : transList.vtables) {
            Span sp;
            const auto& path = ent.first;
            const auto& traitPath = path.data.as_UfcsKnown().trait;
            const auto& type = path.data.as_UfcsKnown().type;

            struct {
                const char* fcnName;
                const HIRSimplePath* traitPath;
                HIRBorrowType bt;
            } const entries[3] = {{"call", &state.resolve.langFn(), HIRBorrowType::Shared}, {"call_mut", &state.resolve.langFnMut(), HIRBorrowType::Unique}, {"call_once", &state.resolve.langFnOnce(), HIRBorrowType::Owned}};

            size_t offset;
            if (traitPath.path == state.resolve.langFn()) {
                offset = 0;
            } else if (traitPath.path == state.resolve.langFnMut()) {
                offset = 1;
            } else if (traitPath.path == state.resolve.langFnOnce()) {
                offset = 2;
            } else {
                offset = 3;
            }

            if (const auto* te = type->opt_NamedFunction()) {
                for (; offset < sizeof(entries) / sizeof(entries[0]); offset++) {
                    bool isByValue = (offset == 2);
                    const auto& ent = entries[offset];

                    auto fcnP = path.clone();
                    fcnP.data.as_UfcsKnown().item = ent.fcnName;
                    fcnP.data.as_UfcsKnown().trait.path = ent.traitPath->clone();

                    auto* e = transList.addFunction(crate.types, mv$(fcnP));
                    if (e) {
                        auto ft = te->decay(crate.types, sp);

                        Vector<const HIRTypeData*> argTys;
                        for (auto& ty : ft.argTypes) {
                            argTys.pushBack(ty);
                        }
                        auto argTy = crate.types.tuple(mv$(argTys));
                        argTy = state.resolve.expandAssociatedTypes(sp, argTy);

                        HIRFunction fcn;
                        fcn.returnType = ft.rettype;
                        argTy = state.resolve.expandAssociatedTypes(sp, argTy);
                        fcn.args.push_back(std::make_pair(HIRPattern(), !isByValue ? crate.types.borrow(ent.bt, type) : type));
                        fcn.args.push_back(std::make_pair(HIRPattern(), mv$(argTy)));

                        fcn.code.mir = generatedBody();
                        Builder builder(state, *fcn.code.mir);

                        std::vector<MIRParam> argParams;
                        for (size_t i = 0; i < ft.argTypes.length(); i++) {
                            argParams.push_back(MIRLValue::newField(MIRLValue::newArgument(1), i));
                        }
                        builder.terminateCall(MIRLValue::newReturn(), te->path.clone(), mv$(argParams), 1, 2);
                        builder.ensureOpen();
                        builder.terminateBlock(MIRTerminator::make_Return({}));
                        builder.ensureOpen();
                        builder.mir.blocks.back().isCleanup = true;
                        builder.terminateBlock(MIRTerminator::make_UnwindResume({}));

                        transList.autoFunctions.push_back(box$(fcn));
                        e->ptr = transList.autoFunctions.back().get();
                    }
                }
            } else if (const auto* te = type->opt_Function()) {
                for (; offset < sizeof(entries) / sizeof(entries[0]); offset++) {
                    bool isByValue = (offset == 2);
                    const auto& ent = entries[offset];

                    auto fcnP = path.clone();
                    fcnP.data.as_UfcsKnown().item = ent.fcnName;
                    fcnP.data.as_UfcsKnown().trait.path = ent.traitPath->clone();

                    auto* e = transList.addFunction(crate.types, mv$(fcnP));
                    if (e) {
                        Vector<const HIRTypeData*> argTys;
                        for (const auto& ty : te->argTypes) {
                            argTys.pushBack(ty);
                        }
                        auto argTy = crate.types.tuple(mv$(argTys));

                        HIRFunction fcn;
                        fcn.returnType = te->rettype;
                        fcn.args.push_back(std::make_pair(HIRPattern(), !isByValue ? crate.types.borrow(ent.bt, type) : type));
                        fcn.args.push_back(std::make_pair(HIRPattern(), mv$(argTy)));

                        fcn.code.mir = generatedBody();
                        Builder builder(state, *fcn.code.mir);

                        std::vector<MIRParam> argParams;
                        for (size_t i = 0; i < te->argTypes.length(); i++) {
                            argParams.push_back(MIRLValue::newField(MIRLValue::newArgument(1), i));
                        }
                        builder.terminateCall(MIRLValue::newReturn(), !isByValue ? MIRLValue::newDeref(MIRLValue::newArgument(0)) : MIRLValue::newArgument(0), mv$(argParams), 1, 2);
                        builder.ensureOpen();
                        builder.terminateBlock(MIRTerminator::make_Return({}));
                        builder.ensureOpen();
                        builder.mir.blocks.back().isCleanup = true;
                        builder.terminateBlock(MIRTerminator::make_UnwindResume({}));

                        transList.autoFunctions.push_back(box$(fcn));
                        e->ptr = transList.autoFunctions.back().get();
                    }
                }
            }
        }
        for (const auto& ent : transList.vtables) {
            Span sp;
            const auto& traitPath = ent.first.data.as_UfcsKnown().trait;
            const auto& type = ent.first.data.as_UfcsKnown().type;
            if (traitPath.path != HIRSimplePath()) {
                continue;
            }

            DEBUG(StringView("VTABLE <empty> for ") << type);
            Vector<const HIRTypeData*> tupleTys;
            tupleTys.pushBack(crate.types.primitive(HIRCoreType::Usize));
            tupleTys.pushBack(crate.types.primitive(HIRCoreType::Usize));
            tupleTys.pushBack(crate.types.primitive(HIRCoreType::Usize));
            auto vtableTy = crate.types.tuple(std::move(tupleTys));

            const auto* repr = TargetGetTypeRepr(sp, state.resolve, vtableTy);
            BUG_ASSERT(repr);

            HIRLinkage linkage;
            linkage.type = HIRLinkage::Type::Weak;
            HIRStatic vtableStatic(std::move(linkage), /*is_mut*/ false, mv$(vtableTy), {});
            auto& vtableData = vtableStatic.valueRes;
            const auto ptrBytes = TargetGetPointerBits() / 8;
            vtableData.bytes.zero(repr->size);
            size_t ofs = 0;
            auto pushPtr = [&vtableData, &ofs, ptrBytes](HIRPath p, bool preserveTrackCaller = false) {
                BUG_ASSERT(ofs + ptrBytes <= vtableData.bytes.length());
                vtableData.relocations.push_back(Reloc::newNamed(ofs, ptrBytes, mv$(p), preserveTrackCaller));
                vtableData.writeUint(ofs, ptrBytes, EncodedLiteral::PTR_BASE);
                ofs += ptrBytes;
                BUG_ASSERT(ofs <= vtableData.bytes.length());
            };
            transList.dropGlue.insert(type);
            pushPtr(HIRPath(type, "#drop_glue"));
            {
                size_t size, align;
                ASSERT_BUG(sp, TargetGetSizeAndAlignOf(sp, state.resolve, type, size, align), StringView("Unexpected generic? ") << type);
                vtableData.writeUint(ofs, ptrBytes, size);
                ofs += ptrBytes;
                vtableData.writeUint(ofs, ptrBytes, align);
                ofs += ptrBytes;
            }
            BUG_ASSERT(ofs == vtableData.bytes.length());
            vtableStatic.valueGenerated = true;

            transList.autoStatics.push_back(box$(vtableStatic));
            auto* e = transList.addStatic(crate.types, ent.first.clone());
            if (e) {
                e->ptr = transList.autoStatics.back().get();
            } else {
                transList.autoStatics.pop_back();
            }
        }
        for (const auto& ent : transList.vtables) {
            Span sp;
            const auto& traitPath = ent.first.data.as_UfcsKnown().trait;
            const auto& type = ent.first.data.as_UfcsKnown().type;
            if (traitPath.path == HIRSimplePath()) {
                continue;
            }
            DEBUG(StringView("VTABLE ") << traitPath << StringView(" for ") << type);
            // TODO: What's the use of `ent.second` here? (it's a `Trans_Params`)

            const auto& trait = crate.getTraitByPath(sp, traitPath.path);
            const auto& vtableSp = trait.vtablePath;
            ASSERT_BUG(sp, vtableSp != HIRSimplePath(), StringView("Trait ") << traitPath.path << StringView(" doesn't have a vtable"));
            auto vtableParams = traitPath.params.clone();
            for (const auto& ty : trait.typeIndexes) {
                auto aty = crate.types.path(HIRPath(type, traitPath.clone(), ty.first), {});
                aty = state.resolve.expandAssociatedTypes(sp, aty);
                vtableParams.types.push_back(mv$(aty));
            }
            const auto& vtableRef = crate.getStructByPath(sp, vtableSp);
            auto vtableTy = crate.types.path(HIRGenericPath(mv$(vtableSp), mv$(vtableParams)), &vtableRef);

            transList.addType(vtableTy, false);

            const auto* repr = TargetGetTypeRepr(sp, state.resolve, vtableTy);
            BUG_ASSERT(repr);

            auto monomorphCbTrait = MonomorphStatePtr(crate.types, type, &traitPath.params, nullptr);

            HIRLinkage linkage;
            linkage.type = HIRLinkage::Type::Weak;
            HIRStatic vtableStatic(std::move(linkage), /*is_mut*/ false, mv$(vtableTy), {});
            auto& vtableData = vtableStatic.valueRes;
            const auto ptrBytes = TargetGetPointerBits() / 8;
            vtableData.bytes.zero(repr->size);
            size_t ofs = 0;
            auto pushPtr = [&vtableData, &ofs, ptrBytes](HIRPath p, bool preserveTrackCaller = false) {
                BUG_ASSERT(ofs + ptrBytes <= vtableData.bytes.length());
                vtableData.relocations.push_back(Reloc::newNamed(ofs, ptrBytes, mv$(p), preserveTrackCaller));
                vtableData.writeUint(ofs, ptrBytes, EncodedLiteral::PTR_BASE);
                ofs += ptrBytes;
                BUG_ASSERT(ofs <= vtableData.bytes.length());
            };
            transList.dropGlue.insert(type);
            pushPtr(HIRPath(type, "#drop_glue"));
            {
                size_t size, align;
                ASSERT_BUG(sp, TargetGetSizeAndAlignOf(sp, state.resolve, type, size, align), StringView("Unexpected generic? ") << type);
                vtableData.writeUint(ofs, ptrBytes, size);
                ofs += ptrBytes;
                vtableData.writeUint(ofs, ptrBytes, align);
                ofs += ptrBytes;
            }

            for (unsigned int i = 0; i < trait.valueIndexes.size(); i++) {
                for (const auto& m : trait.valueIndexes) {
                    if (m.second.first != 3 + i) {
                        continue;
                    }

                    DEBUG(StringView("- ") << m.second.first << StringView(" = ") << m.second.second << StringView(" :: ") << m.first);
                    auto traitGpath = monomorphCbTrait.monomorphGenericpath(sp, m.second.second, false);
                    auto itemPath = HIRPath(type, mv$(traitGpath), m.first);
                    state.resolve.expandAssociatedTypesPath(sp, itemPath);

                    auto srcTraitMs = MonomorphStatePtr(crate.types, type, &itemPath.data.as_UfcsKnown().trait.params, nullptr);
                    const auto& srcTrait = state.resolve.hirCrate().getTraitByPath(sp, m.second.second.path);
                    const auto& item = srcTrait.values.at(m.first);
                    bool preserveTrackCaller = false;
                    if (item.is_Function()) {
                        const auto& tplFcn = item.as_Function();
                        preserveTrackCaller = tplFcn.markings.trackCaller;
                        if (tplFcn.receiver == HIRFunction::Receiver::Value) {
                            auto callPath = itemPath.clone();
                            itemPath.data.as_UfcsKnown().item = RcString::newInterned(FMT(m.first << StringView("#ptr")));
                            auto* e = transList.addFunction(crate.types, itemPath.clone());
                            if (e) {
                                HIRFunction newFcn;
                                newFcn.markings.trackCaller = preserveTrackCaller;
                                newFcn.returnType = srcTraitMs.monomorphType(sp, tplFcn.returnType);
                                newFcn.returnType = state.resolve.expandAssociatedTypes(sp, newFcn.returnType);
                                newFcn.args.push_back(std::make_pair(HIRPattern(), crate.types.borrow(HIRBorrowType::Owned, type)));
                                for (size_t i = 1; i < tplFcn.args.size(); i++) {
                                    newFcn.args.push_back(std::make_pair(HIRPattern(), srcTraitMs.monomorphType(sp, tplFcn.args[i].second)));
                                }
                                for (size_t i = 0; i < newFcn.args.size(); i++) {
                                    newFcn.args[i].second = state.resolve.expandAssociatedTypes(sp, newFcn.args[i].second);
                                }

                                DEBUG(StringView("> Generate shim: ") << itemPath);
                                newFcn.code.mir = generatedBody();
                                auto pathCallback = makeCallable<MIRPathCb>([&](auto& os) {
                                    os << itemPath;
                                });
                                MIRTypeResolve localMirRes{sp, state.resolve, pathCallback, newFcn.returnType, newFcn.args, *newFcn.code.mir};
                                Builder builder(state, *newFcn.code.mir);
                                std::vector<MIRParam> callArgs;
                                callArgs.push_back(MIRLValue::newDeref(MIRLValue::newArgument(0)));
                                for (size_t i = 1; i < tplFcn.args.size(); i++) {
                                    callArgs.push_back(MIRLValue::newArgument(i));
                                }
                                builder.terminateCall(MIRLValue::newReturn(), mv$(callPath), std::move(callArgs), 1, 2, preserveTrackCaller);
                                builder.ensureOpen();
                                builder.terminateBlock(MIRTerminator::make_Return({}));
                                builder.ensureOpen();
                                builder.mir.blocks.back().isCleanup = true;
                                builder.terminateBlock(MIRTerminator::make_UnwindResume({}));

                                transList.autoFunctions.push_back(box$(newFcn));
                                e->ptr = transList.autoFunctions.back().get();
                            }
                        }
                    }
                    //MIR_ASSERT(*m_mir_res, tr.m_values.at(m.first).is_Function(), StringView("TODO: Handle generating vtables with non-function items"));
                    pushPtr(mv$(itemPath), preserveTrackCaller);
                }
            }
            for (size_t i = 0; i < trait.allParentTraits.size(); i++) {
                const auto& pt = trait.allParentTraits[i];
                const auto& fld = repr->fields.at(trait.vtableParentTraitsStart + i);
                ASSERT_BUG(sp, fld.offset == ofs, StringView(""));
                if (!fld.ty->is_Tuple()) {
                    auto ptMono = MonomorphStatePtr(crate.types, type, &traitPath.params, nullptr).monomorphGenericpath(sp, pt.path);
                    auto ptVtablePath = HIRPath(type, mv$(ptMono), ent.first.data.as_UfcsKnown().item);
                    state.resolve.expandAssociatedTypesPath(sp, ptVtablePath);
                    pushPtr(mv$(ptVtablePath));
                }
            }
            BUG_ASSERT(ofs == vtableData.bytes.length());
            vtableStatic.valueGenerated = true;

            transList.autoStatics.push_back(box$(vtableStatic));
            auto* e = transList.addStatic(crate.types, ent.first.clone());
            if (e) {
                e->ptr = transList.autoStatics.back().get();
            } else {
                transList.autoStatics.pop_back();
            }
        }
        transList.vtables.clear();
    }

    {
        TRACE_FUNCTION_F(StringView("Drop Glue"));
        for (const auto& ty : transList.types) {
            Span sp;
            if (ty.second) {
                continue;
            }
            if (!state.resolve.typeNeedsDropGlue(sp, ty.first)) {
                continue;
            }

            if (ty.first->is_TraitObject()) {
                continue;
            }
            if (ty.first->is_Slice()) {
                continue;
            }
            transList.dropGlue.insert(ty.first);
        }

        for (const auto& ty : transList.dropGlue) {
            Span sp;
            auto path = HIRPath(ty, "#drop_glue");

            HIRFunction fcn;
            fcn.returnType = crate.types.unit();
            fcn.args.push_back(std::make_pair(HIRPattern(), crate.types.borrow(HIRBorrowType::Owned, ty)));

            fcn.code.mir = generatedBody();
            auto pathCallback = makeCallable<MIRPathCb>([&](auto& os) {
                os << path;
            });
            MIRTypeResolve localMirRes{sp, state.resolve, pathCallback, fcn.returnType, fcn.args, *fcn.code.mir};
            Builder builder(state, *fcn.code.mir);
            builder.pushStmtAssign(MIRLValue::newReturn(), MIRRValue::make_Tuple({}));
            auto ownedBoxPointeeDrop = static_cast<MIRBasicBlockId>(~0u);
            auto ownedBoxDropCall = static_cast<MIRBasicBlockId>(~0u);
            if (const auto* ity = state.resolve.isTypeOwnedBox(ty)) {
                auto innerVal = derefBox(MIRLValue::newDeref(builder.self.clone()));
                const HIRTypeData* tmp;
                ASSERT_BUG(sp, localMirRes.getLvalueType(innerVal) == ity, StringView("Hard-coded box pointer path didn't result in the inner type"));
                ownedBoxPointeeDrop = builder.pushStmtDrop(std::move(innerVal));
            }

            if (state.resolve.typeNeedsDropGlue(sp, ty)) {
                switch ((*ty).tag()) {
                    case HIRTypeData::TAG_Infer: {
                        UNREACHABLE();
                    }
                    case HIRTypeData::TAG_Generic: {
                        UNREACHABLE();
                    }
                    case HIRTypeData::TAG_ErasedType: {
                        UNREACHABLE();
                    }
                    case HIRTypeData::TAG_TraitObject: {
                        TODO(sp, StringView("Drop glue for TraitObject? ") << ty);
                        break;
                    }
                    case HIRTypeData::TAG_Slice: {
                        TODO(sp, StringView("Drop glue for Slice? ") << ty);
                        break;
                    }
                    case HIRTypeData::TAG_NodeType: {
                        TODO(sp, StringView("Drop glue for NodeType? ") << ty);
                        break;
                    }
                    case HIRTypeData::TAG_Diverge: {
                        builder.terminateBlock(MIRTerminator::make_Unreachable({}));
                        break;
                    }
                    case HIRTypeData::TAG_Primitive: {
                        break;
                    }
                    case HIRTypeData::TAG_Pattern: {
                        break;
                    }
                    case HIRTypeData::TAG_NamedFunction: {
                        break;
                    }
                    case HIRTypeData::TAG_Function: {
                        break;
                    }
                    case HIRTypeData::TAG_Pointer: {
                        break;
                    }
                    case HIRTypeData::TAG_Borrow: {
                        auto& te = (*ty).as_Borrow();
                        if (te.type == HIRBorrowType::Owned) {
                            builder.pushStmtDrop(MIRLValue::newDeref(MIRLValue::newDeref(builder.self.clone())));
                        }
                        break;
                    }
                    case HIRTypeData::TAG_Tuple: {
                        auto& te = (*ty).as_Tuple();
                        auto self = MIRLValue::newDeref(builder.self.clone());
                        auto fldLv = MIRLValue::newField(mv$(self), 0);
                        std::vector<MIRLValue> fields;
                        for (size_t i = 0; i < te.length(); i++) {
                            if (state.resolve.typeNeedsDropGlue(sp, te[i])) {
                                fields.push_back(fldLv.clone());
                            }
                            fldLv.incField();
                        }
                        builder.pushDropSequence(mv$(fields));
                        break;
                    }
                    case HIRTypeData::TAG_Array: {
                        auto& te = (*ty).as_Array();
                        auto size = te.size.as_Known();
                        auto self = MIRLValue::newDeref(builder.self.clone());
                        if (size > 0 && state.resolve.typeNeedsDropGlue(sp, te.inner)) {
                            builder.pushStmtDrop(mv$(self));
                        }
                        break;
                    }
                    case HIRTypeData::TAG_Path: {
                        auto& te = (*ty).as_Path();
                        bool hasDrop = false;
                        switch (te.binding.tag()) {
                            case HIRTypePathBinding::TAG_Unbound: {
                                UNREACHABLE();
                            }
                            case HIRTypePathBinding::TAG_Opaque: {
                                UNREACHABLE();
                            }
                            case HIRTypePathBinding::TAG_ExternType: {
                                break;
                            }
                            case HIRTypePathBinding::TAG_Struct: {
                                auto& pbe = te.binding.as_Struct();
                                auto customDropCall = static_cast<MIRBasicBlockId>(~0u);
                                if (pbe->markings.hasDropImpl) {
                                    customDropCall = builder.pushCallDrop(ty);
                                    if (ownedBoxPointeeDrop != ~0u) {
                                        ownedBoxDropCall = customDropCall;
                                    }
                                    hasDrop = true;
                                }

                                if (ty->is_Path() && (ty->as_Path().isGenerator() || ty->as_Path().isFuture())) {
                                    ASSERT_BUG(sp, hasDrop, StringView(""));
                                } else {
                                    const auto* repr = TargetGetTypeRepr(sp, state.resolve, ty);
                                    ASSERT_BUG(sp, repr, StringView("No repr for struct ") << ty);

                                    auto self = MIRLValue::newDeref(builder.self.clone());
                                    auto fldLv = MIRLValue::newField(mv$(self), 0);
                                    std::vector<MIRLValue> fields;
                                    for (size_t i = 0; i < repr->fields.size(); i++) {
                                        if (state.resolve.typeNeedsDropGlue(sp, repr->fields[i].ty)) {
                                            fields.push_back(fldLv.clone());
                                        }
                                        fldLv.incField();
                                    }
                                    builder.pushDropSequence(mv$(fields), customDropCall);
                                }
                                break;
                            }
                            case HIRTypePathBinding::TAG_Union: {
                                auto& pbe = te.binding.as_Union();
                                if (pbe->markings.hasDropImpl) {
                                    builder.pushCallDrop(ty);
                                    hasDrop = true;
                                }
                                break;
                            }
                            case HIRTypePathBinding::TAG_Enum: {
                                auto& pbe = te.binding.as_Enum();
                                auto customDropCall = static_cast<MIRBasicBlockId>(~0u);
                                if (pbe->markings.hasDropImpl) {
                                    customDropCall = builder.pushCallDrop(ty);
                                    hasDrop = true;
                                }
                                const HIREnum& enm = *pbe;
                                switch (enm.data.tag()) {
                                    case HIREnumClass::TAG_Value: {
                                        builder.terminateBlock(MIRTerminator::make_Return({}));
                                        break;
                                    }
                                    case HIREnumClass::TAG_Data: {
                                        auto& variants = enm.data.as_Data();
                                        auto self = MIRLValue::newDeref(builder.self.clone());
                                        MIRTerminator::Data_Switch sw;
                                        sw.val = self.clone();
                                        const auto switchBlock = builder.mir.blocks.size() - 1;
                                        builder.terminateBlock(MIRTerminator::make_Switch(mv$(sw)));

                                        Vector<MIRBasicBlockId> targets;
                                        targets.grow(variants.size());
                                        auto fldLv = MIRLValue::newDowncast(mv$(self), 0);
                                        for (size_t idx = 0; idx < variants.size(); idx++) {
                                            builder.ensureOpen();
                                            targets.pushBack(builder.mir.blocks.size() - 1);
                                            // TODO: Monomorphise and check

                                            {
                                                builder.pushStmtDrop(fldLv.clone());
                                            }
                                            fldLv.incDowncast();
                                            builder.ensureOpen();
                                            builder.terminateBlock(MIRTerminator::make_Return({}));
                                        }
                                        builder.mir.blocks[switchBlock].terminator.as_Switch().targets = mv$(targets);

                                        if (customDropCall != ~0u) {
                                            const auto cleanupSwitch = static_cast<MIRBasicBlockId>(builder.mir.blocks.size());
                                            MIRBasicBlock switchCleanupBlock;
                                            switchCleanupBlock.isCleanup = true;
                                            MIRTerminator::Data_Switch cleanupSwitchData;
                                            cleanupSwitchData.val = MIRLValue::newDeref(builder.self.clone());
                                            switchCleanupBlock.terminator = MIRTerminator::make_Switch(mv$(cleanupSwitchData));
                                            builder.mir.blocks.push_back(mv$(switchCleanupBlock));

                                            const auto resume = static_cast<MIRBasicBlockId>(builder.mir.blocks.size() + variants.size());
                                            Vector<MIRBasicBlockId> cleanupTargets;
                                            cleanupTargets.grow(variants.size());
                                            auto cleanupField = MIRLValue::newDowncast(MIRLValue::newDeref(builder.self.clone()), 0);
                                            for (size_t idx = 0; idx < variants.size(); idx++) {
                                                cleanupTargets.pushBack(builder.mir.blocks.size());
                                                MIRBasicBlock block;
                                                block.isCleanup = true;
                                                block.terminator = MIRTerminator::make_Drop({
                                                    MIRDropKind::DEEP,
                                                    cleanupField.clone(),
                                                    ~0u,
                                                    resume,
                                                    MIRUnwindAction::make_Terminate({}),
                                                });
                                                builder.mir.blocks.push_back(mv$(block));
                                                cleanupField.incDowncast();
                                            }
                                            MIRBasicBlock resumeBlock;
                                            resumeBlock.isCleanup = true;
                                            resumeBlock.terminator = MIRTerminator::make_UnwindResume({});
                                            builder.mir.blocks.push_back(mv$(resumeBlock));

                                            builder.mir.blocks[cleanupSwitch].terminator.as_Switch().targets = mv$(cleanupTargets);
                                            builder.mir.blocks[customDropCall].terminator.as_Call().unwind = MIRUnwindAction::make_Cleanup(cleanupSwitch);
                                        }
                                        break;
                                    }
                                }
                                break;
                            }
                        }
                        if (hasDrop) {
                            if (auto* e = transList.addFunction(crate.types, HIRPath(ty, state.resolve.langDrop(), "drop"))) {
                                MonomorphState params(crate.types);
                                auto p = HIRPath(ty, state.resolve.langDrop(), "drop");
                                const HIRGenericParams* implParamsDef = nullptr;
                                auto fcnE = state.resolve.getValue(sp, p, /*out*/ params, /*signature_only=*/false, &implParamsDef);
                                ASSERT_BUG(sp, fcnE.is_Function(), StringView("Drop didn't point to a function! ") << fcnE.tagStr() << StringView(" ") << p);
                                e->ptr = fcnE.as_Function();
                                e->pp.selfType = params.getSelfType();
                                e->pp.gdefImpl = implParamsDef;
                                if (const auto* implParams = params.getImplParams()) {
                                    e->pp.ppImpl = implParams->clone();
                                }
                                if (const auto* methodParams = params.getMethodParams()) {
                                    e->pp.ppMethod = methodParams->clone();
                                }
                            }
                        }
                        break;
                    }
                }
            }

            if (ownedBoxPointeeDrop != ~0u) {
                ASSERT_BUG(sp, ownedBoxDropCall != ~0u, StringView("Owned Box did not have a Drop implementation: ") << ty);

                if (builder.mir.blocks.back().terminator.is_Incomplete()) {
                    builder.terminateBlock(MIRTerminator::make_Return({}));
                }

                MIRBasicBlockId afterCleanupCall;
                if (const auto* fieldCleanup = builder.mir.blocks[ownedBoxDropCall].terminator.as_Call().unwind.opt_Cleanup()) {
                    afterCleanupCall = *fieldCleanup;
                } else {
                    afterCleanupCall = static_cast<MIRBasicBlockId>(builder.mir.blocks.size() + 1);
                }

                auto cleanupBorrow = builder.addLocal(state.crate.types.borrow(HIRBorrowType::Unique, ty));
                const auto cleanupCall = static_cast<MIRBasicBlockId>(builder.mir.blocks.size());
                MIRBasicBlock cleanupCallBlock;
                cleanupCallBlock.isCleanup = true;
                cleanupCallBlock.statements.push_back(
                    MIRStatement::make_Assign({
                        cleanupBorrow.clone(),
                        MIRRValue::make_Borrow({HIRBorrowType::Unique, false, MIRLValue::newDeref(builder.self.clone())}),
                    })
                );
                cleanupCallBlock.terminator = MIRTerminator::make_Call({
                    afterCleanupCall,
                    MIRUnwindAction::make_Terminate({}),
                    MIRLValue::newReturn(),
                    HIRPath(ty, state.resolve.langDrop(), "drop"),
                    makeVec1<MIRParam>(mv$(cleanupBorrow)),
                });
                builder.mir.blocks.push_back(mv$(cleanupCallBlock));

                if (afterCleanupCall == builder.mir.blocks.size()) {
                    MIRBasicBlock resumeBlock;
                    resumeBlock.isCleanup = true;
                    resumeBlock.terminator = MIRTerminator::make_UnwindResume({});
                    builder.mir.blocks.push_back(mv$(resumeBlock));
                }
                builder.mir.blocks[ownedBoxPointeeDrop].terminator.as_Drop().unwind = MIRUnwindAction::make_Cleanup(cleanupCall);
            }
            if (builder.mir.blocks.back().terminator.is_Incomplete()) {
                builder.terminateBlock(MIRTerminator::make_Return({}));
            }

            transList.autoFunctions.push_back(box$(fcn));
            auto* e = transList.addFunction(crate.types, mv$(path));
            if (e) {
                e->ptr = transList.autoFunctions.back().get();
            } else {
                transList.autoFunctions.pop_back();
            }
        }
        transList.dropGlue.clear();
    }
}

TransList TransEnumerateMain(const WireBoard& wb, HIRCrate& crate) {
    Span sp;

    EnumState state{wb};

    if (!crate.noMain) {
        auto cStartPath = crate.getLangItemPathOpt("trustme-start");
        if (cStartPath == HIRSimplePath()) {
            auto mainPath = crate.getLangItemPath(Span(), "trustme-main");
            const auto& mainFcn = crate.getFunctionByPath(sp, mainPath);

            state.rv.roots.push_back(mainPath);
            state.enumFcn(mainPath, mainFcn, TransParams(crate.types));

            const auto& startPath = crate.getLangItemPathOpt("start");
            if (startPath != HIRSimplePath()) {
                const auto& fcn = crate.getFunctionByPath(sp, startPath);

                TransParams langStartPp(crate.types);
                langStartPp.ppMethod.types.push_back(mainFcn.returnType);
                HIRPath p = HIRGenericPath(startPath, langStartPp.ppMethod.clone());
                state.rv.roots.push_back(p.clone());
                state.enumFcn(std::move(p), fcn, mv$(langStartPp));
            } else if (!crate.isNoCore) {
                crate.getLangItemPath(sp, "start");
            }
        } else {
            const auto& fcn = crate.getFunctionByPath(sp, cStartPath);

            state.rv.roots.push_back(cStartPath);
            state.enumFcn(cStartPath, fcn, TransParams(crate.types));
        }
    }

    TransEnumerateExplicitLinkage(state, crate.rootModule, HIRSimplePath(crate.crateName, {}));
    TransEnumerateGlobalAllocator(state);
    TransEnumerateGlobalAsm(state, crate.rootModule);

    return TransEnumerateCommonPost(state);
}

TransList TransEnumeratePublic(const WireBoard& wb, HIRCrate& crate) {
    Span sp;
    EnumState state{wb};

    TransEnumeratePublicMod(state, crate.rootModule, HIRSimplePath(crate.crateName, {}), true);

    StaticTraitResolve resolve{wb, OpaqueReveal::All};
    for (auto& implGroup : crate.traitImpls) {
        const auto& traitPath = implGroup.first;
        for (auto& implList : implGroup.second.named) {
            for (auto& impl : implList.second) {
                TransEnumeratePublicTraitImpl(state, resolve, traitPath, *impl);
            }
        }
        for (auto& impl : implGroup.second.nonNamed) {
            TransEnumeratePublicTraitImpl(state, resolve, traitPath, *impl);
        }
        for (auto& impl : implGroup.second.generic) {
            TransEnumeratePublicTraitImpl(state, resolve, traitPath, *impl);
        }
    }

    struct H1 {
        static void enumerateTypeImpl(EnumState& state, HIRTypeImpl& impl) {
            TRACE_FUNCTION_F(StringView("impl") << impl.params.fmtArgs() << StringView(" ") << impl.type);
            HIRPathParams implParams = HIRPathParams();
            MonomorphStatePtr ms(state.crate.types);
            ms.ppImpl = &implParams;
            if (!impl.params.isGeneric()) {
                for (auto& fcn : impl.methods) {
                    DEBUG(StringView("fn ") << fcn.first << fcn.second.data.params.fmtArgs());
                    if (!fcn.second.data.params.isGeneric()) {
                        TransParams pp(state.crate.types);
                        pp.ppImpl = implParams.clone();
                        pp.ppMethod = HIRPathParams();
                        auto path = HIRPath(MonomorphStatePtr(state.crate.types, nullptr, &implParams, nullptr).monomorphType(Span(), impl.type), fcn.first);
                        path.data.as_UfcsInherent().implParams = pp.ppImpl.clone();
                        path.data.as_UfcsInherent().params = pp.ppMethod.clone();
                        if (fcn.second.publicity.isGlobal()) {
                            state.rv.roots.push_back(path.clone());
                        }
                        state.enumFcn(mv$(path), fcn.second.data, mv$(pp));
                    } else {
                        fcn.second.data.saveCode = true;
                    }
                    if (fcn.second.data.saveCode) {
                        TransEnumerateGenericFunctionItems(state, Span(), fcn.second.data, ms, !impl.params.bounds.empty() || !fcn.second.data.params.bounds.empty());
                    }
                }
            } else {
                for (auto& m : impl.methods) {
                    m.second.data.saveCode = true;
                    TransEnumerateGenericFunctionItems(state, Span(), m.second.data, ms, !impl.params.bounds.empty() || !m.second.data.params.bounds.empty());
                }
            }
            for (auto& e : impl.constants) {
                TransParams tp(state.crate.types);
                tp.ppImpl = HIRPathParams();
                TransEnumerateFillFromLiteral(state, e.second.data.valueRes, std::move(tp));

                if (e.second.publicity.isGlobal() && !impl.params.isGeneric() && !e.second.data.params.isGeneric()) {
                    auto ppMethod = HIRPathParams();
                    for (const auto& r : e.second.data.valueRes.relocations) {
                        if (r.p) {
                            state.rv.roots.push_back(MonomorphStatePtr(state.crate.types, nullptr, &implParams, &ppMethod).monomorphPath(Span(), *r.p));
                        }
                    }
                }
            }
        }
    };

    for (auto& implGrp : crate.typeImpls.named) {
        for (auto& impl : implGrp.second) {
            H1::enumerateTypeImpl(state, *impl);
        }
    }
    for (auto& impl : crate.typeImpls.nonNamed) {
        H1::enumerateTypeImpl(state, *impl);
    }
    for (auto& impl : crate.typeImpls.generic) {
        H1::enumerateTypeImpl(state, *impl);
    }

    {
        auto it = crate.langItems.find("trustme-panic_implementation");
        if (it != crate.langItems.end()) {
            HIRGenericPath p = it->second;
            const auto& f = crate.getFunctionByPath(Span(), p.path);
            p.params = HIRPathParams();
            TransEnumerateFillFromPathMono(state, std::move(p));
        }
    }

    TransEnumerateGlobalAsm(state, crate.rootModule);

    auto rv = TransEnumerateCommonPost(state);

    for (auto it = rv.functions.begin(); it != rv.functions.end();) {
        if (monomorphisePathNeeded(it->first)) {
            rv.functions.erase(it++);
        } else {
            ++it;
        }
    }
    for (auto it = rv.statics.begin(); it != rv.statics.end();) {
        if (monomorphisePathNeeded(it->first)) {
            rv.statics.erase(it++);
        } else {
            ++it;
        }
    }

    return rv;
}

void TransEnumerateCleanup(const WireBoard& wb, const HIRCrate& crate, TransList& list) {
    for (const auto& fcnE : list.functions) {
        auto& function = *fcnE.second->ptr;
        if (function.code.mir) {
            function.code.mir->transEnumState = MIREnumCachePtr();
        }
    }
    for (const auto& fcnE : list.functions) {
        auto& function = *fcnE.second->ptr;
        if (function.code.mir && !function.code.mir->transEnumState) {
            DEBUG(fcnE.first);
            auto* esp = new MIREnumCache();
            TransEnumerateFillFromMIR(*esp, *function.code.mir);
            function.code.mir->transEnumState = MIREnumCachePtr(esp);
        }
    }

    EnumState state{wb};
    state.origList = &list;
    for (const auto& p : list.roots) {
        HIRPath path = p.clone();
        MonomorphState unusedParams(state.crate.types);
        const auto& vi = state.resolve.getValue(Span(), path, unusedParams, /*signature_only=*/true);
        if (const auto* f = vi.opt_Function()) {
            switch (path.data.tag()) {
                default:
                    break;
                case HIRPathData::TAG_Generic: {
                    break;
                }
            }
        } else {
        }
        TransEnumerateFillFromPathMono(state, std::move(path));
    }
    auto newList = TransEnumerateCommonPost(state);

    Vector<const TransListFunction*> enumeratedImplicitDrops;
    for (;;) {
        Vector<const TransListFunction*> generatedFunctions;
        for (const auto& entry : list.functions) {
            const auto* type = implicitDropType(entry.first, state.resolve.langDrop());
            if (!type || (!newList.hasType(type, false) && newList.dropGlue.count(type) == 0) || newList.findFunction(entry.first) || entry.second->forcePrototype) {
                continue;
            }
            if (!entry.second->monomorphised.code && !entry.second->ptr->code.mir) {
                continue;
            }

            bool alreadyEnumerated = false;
            for (const auto* function : enumeratedImplicitDrops) {
                alreadyEnumerated |= function == entry.second.get();
            }
            if (alreadyEnumerated) {
                continue;
            }
            enumeratedImplicitDrops.pushBack(entry.second.get());
            generatedFunctions.pushBack(entry.second.get());
        }
        if (generatedFunctions.empty()) {
            break;
        }
        TransEnumerateGeneratedMIR(wb, newList, generatedFunctions);
    }

    for (const auto* type : newList.dropGlue) {
        newList.functions.insert(std::make_pair(HIRPath(type, "#drop_glue"), nullptr));
    }
    for (const auto& vtp : newList.vtables) {
        Span sp;
        const auto& traitPath = vtp.first.data.as_UfcsKnown().trait;
        const auto& type = vtp.first.data.as_UfcsKnown().type;

        HIRPath dropGlueFn(type, "#drop_glue");
        DEBUG(StringView("++ ") << dropGlueFn);
        newList.functions.insert(std::make_pair(std::move(dropGlueFn), nullptr));

        DEBUG(StringView("++ ") << vtp.first);
        newList.statics.insert(std::make_pair(vtp.first.clone(), nullptr));

        if (traitPath.path == HIRSimplePath()) {
            continue;
        }

        const auto& trait = crate.getTraitByPath(sp, traitPath.path);

        auto monomorphCbTrait = MonomorphStatePtr(state.crate.types, type, &traitPath.params, nullptr);
        for (unsigned int i = 0; i < trait.valueIndexes.size(); i++) {
            for (const auto& m : trait.valueIndexes) {
                if (m.second.first != 3 + i) {
                    continue;
                }

                auto traitGpath = monomorphCbTrait.monomorphGenericpath(sp, m.second.second, false);
                auto itemPath = HIRPath(type, mv$(traitGpath), m.first);
                state.resolve.expandAssociatedTypesPath(sp, itemPath);

                DEBUG(StringView("++ ") << itemPath);
                newList.functions.insert(std::make_pair(std::move(itemPath), nullptr));

                const auto& srcTrait = state.resolve.hirCrate().getTraitByPath(sp, m.second.second.path);
                const auto& item = srcTrait.values.at(m.first);
                if (item.is_Function() && item.as_Function().receiver == HIRFunction::Receiver::Value) {
                    traitGpath = monomorphCbTrait.monomorphGenericpath(sp, m.second.second, false);
                    auto itemPath = HIRPath(type, mv$(traitGpath), RcString::newInterned(FMT(m.first << StringView("#ptr"))));
                    state.resolve.expandAssociatedTypesPath(sp, itemPath);
                    DEBUG(StringView("++ ") << itemPath);
                    newList.functions.insert(std::make_pair(std::move(itemPath), nullptr));
                }
            }
        }
    }
    for (const auto& ty : newList.types) {
        Span sp;
        if (ty.second) {
            continue;
        }
        if (ty.first->is_TraitObject() || ty.first->is_Slice()) {
            continue;
        }
        if (!state.resolve.typeNeedsDropGlue(sp, ty.first)) {
            continue;
        }

        HIRPath dropGlueFn(ty.first, "#drop_glue");
        DEBUG(StringView("++ ") << dropGlueFn);
        newList.functions.insert(std::make_pair(std::move(dropGlueFn), nullptr));

        if (ty.first->is_Path() && ty.first->as_Path().binding.getTraitMarkings()->hasDropImpl) {
            auto fcnPath = HIRPath(ty.first, state.resolve.langDrop(), "drop");
            DEBUG(StringView("++ ") << fcnPath);
            newList.functions.insert(std::make_pair(std::move(fcnPath), nullptr));
        }
    }
    for (const auto& ty : newList.autoCloneImpls) {
        HIRPath fnPath(ty, crate.getLangItemPath(Span(), "clone"), "clone");
        DEBUG(StringView("++ ") << fnPath);
        newList.functions.insert(std::make_pair(std::move(fnPath), nullptr));
    }
    for (const auto& ty : newList.autoCloneFromImpls) {
        HIRPath fnPath(ty, crate.getLangItemPath(Span(), "clone"), "clone_from");
        DEBUG(StringView("++ ") << fnPath);
        newList.functions.insert(std::make_pair(std::move(fnPath), nullptr));
    }
    for (const auto& fnPath : newList.traitObjectMethods) {
        DEBUG(StringView("++ ") << fnPath);
        newList.functions.insert(std::make_pair(fnPath.clone(), nullptr));
    }
    for (const auto& ty : newList.autoFnptrImpls) {
        HIRPath fnPath(ty, crate.getLangItemPath(Span(), "fn_ptr_trait"), "addr");
        DEBUG(StringView("++ ") << fnPath);
        newList.functions.insert(std::make_pair(std::move(fnPath), nullptr));
    }

    for (const auto& entry : list.functions) {
        const auto* type = implicitDropType(entry.first, state.resolve.langDrop());
        if (type && (newList.hasType(type, false) || newList.dropGlue.count(type) != 0)) {
            newList.functions.insert(std::make_pair(entry.first.clone(), nullptr));
        }
    }

    list.clearTypes();
    for (const auto& ty : newList.types) {
        ASSERT_BUG(Span(), list.addType(ty.first, ty.second), StringView("Duplicate type in cleaned translation list: ") << ty.first);
    }
    list.constructors = mv$(newList.constructors);
    removeMissing(wb, list.functions, newList.functions);
    removeMissing(wb, list.statics, newList.statics);
}

void TransEnumerateGeneratedStatics(const WireBoard& wb, TransList& list, const std::vector<HIRPath>& paths) {
    if (paths.empty()) {
        return;
    }

    EnumState state{wb};
    for (const auto& path : paths) {
        TransEnumerateFillFromPathMono(state, path.clone());
    }
    mergeEnumeratedItems(state.crate.types, list, TransEnumerateCommonPost(state));
}

bool TransEnumerateGeneratedLiteral(const WireBoard& wb, TransList& list, const EncodedLiteral& literal) {
    EnumState state{wb};
    for (const auto& relocation : literal.relocations) {
        if (relocation.p && !transListContainsPath(list, *relocation.p)) {
            ASSERT_BUG(Span(), !monomorphisePathNeeded(*relocation.p), StringView("Generated literal contains a generic translation path: ") << *relocation.p);
            TransEnumerateFillFromPathMono(state, relocation.p->clone());
        }
    }
    return mergeEnumeratedItems(state.crate.types, list, TransEnumerateCommonPost(state));
}

bool TransEnumerateGeneratedMIR(const WireBoard& wb, TransList& list, const Vector<const TransListFunction*>& functions) {
    EnumState state{wb};
    for (const auto* function : functions) {
        const MIRFunction* mir;
        const HIRTypeData* returnType;
        const HIRFunction::argsT* args;
        if (function->monomorphised.code) {
            mir = &*function->monomorphised.code;
            returnType = function->monomorphised.retTy;
            args = &function->monomorphised.argTys;
        } else {
            ASSERT_BUG(Span(), function->ptr->code.mir, StringView("Generated function has no MIR: ") << *function->path);
            mir = &*function->ptr->code.mir;
            returnType = function->ptr->returnType;
            args = &function->ptr->args;
        }

        MIREnumCache cache;
        TransEnumerateFillFromMIR(cache, *mir);
        for (const auto* ty : cache.typeids) {
            if (list.typeids.count(ty) == 0) {
                state.rv.typeids.insert(ty);
            }
        }
        for (const auto* type : cache.destructorTypes) {
            enumerateDestructorType(state, type);
        }
        for (const auto* path : cache.paths) {
            ASSERT_BUG(Span(), !monomorphisePathNeeded(*path), StringView("Generated MIR contains a generic translation path: ") << *path);
            if (!transListContainsPath(list, *path)) {
                TransEnumerateFillFromPathMono(state, path->clone());
            }
        }

        Span sp;
        auto pathCallback = makeCallable<MIRPathCb>([&](auto& os) {
            os << *function->path;
        });
        MIRTypeResolve mirResolve{sp, state.resolve, pathCallback, returnType, *args, *mir};
        for (const auto& block : mir->blocks) {
            if (const auto* drop = block.terminator.opt_Drop()) {
                const HIRTypeData* tmp;
                enumerateDestructorType(state, mirResolve.getLvalueType(drop->slot));
            }
        }
    }
    return mergeEnumeratedItems(state.crate.types, list, TransEnumerateCommonPost(state));
}

#include "trans_ent_ptr_tu.cpp"

State::State(const WireBoard& wb, HIRCrate& crate, const TransList& transList)
    : crate(crate)
    , resolve(wb, OpaqueReveal::All)
    , transList(transList)
{
    langClone = crate.getLangItemPathOpt("clone");
}

auto State::enqueueType(const HIRTypeData* ty) -> void {
    if (this->transList.autoCloneImpls.count(ty) == 0 && this->doneList.count(ty) == 0) {
        this->doneList.insert(ty);
        this->todoList.push_back(ty);
    }
}

Builder::Builder(const State& state, MIRFunction& mir)
    : state(state)
    , mir(mir)
    , self(MIRLValue::newArgument(0))
{
    mir.blocks.push_back(MIRBasicBlock());
}

auto Builder::addLocal(const HIRTypeData* ty) -> MIRLValue {
    auto rv = mir.locals.length();
    mir.locals.pushBack(mv$(ty));
    return MIRLValue::newLocal(rv);
}

auto Builder::inTemporary(const HIRTypeData* ty, MIRRValue val) -> MIRLValue {
    auto rv = addLocal(mv$(ty));
    pushStmtAssign(rv.clone(), mv$(val));
    return rv;
}

auto Builder::ensureOpen() -> void {
    if (!mir.blocks.back().terminator.is_Incomplete()) {
        mir.blocks.push_back(MIRBasicBlock());
    }
}

auto Builder::pushStmt(MIRStatement s) -> void {
    ensureOpen();
    mir.blocks.back().statements.push_back(mv$(s));
}

auto Builder::pushStmtAssign(MIRLValue lv, MIRRValue rv) -> void {
    this->pushStmt(MIRStatement::make_Assign({mv$(lv), mv$(rv)}));
}

auto Builder::pushStmtDrop(MIRLValue lv) -> MIRBasicBlockId {
    ensureOpen();
    const auto dropBlock = static_cast<MIRBasicBlockId>(mir.blocks.size() - 1);
    const auto next = static_cast<MIRBasicBlockId>(mir.blocks.size());
    terminateBlock(MIRTerminator::make_Drop({MIRDropKind::DEEP, mv$(lv), ~0u, next, MIRUnwindAction::make_Continue({})}));
    mir.blocks.push_back(MIRBasicBlock());
    return dropBlock;
}

auto Builder::pushDropSequence(std::vector<MIRLValue> values, MIRBasicBlockId customDropCall) -> void {
    if (values.empty()) {
        return;
    }

    ensureOpen();
    const auto entry = static_cast<MIRBasicBlockId>(mir.blocks.size() - 1);
    terminateBlock(MIRTerminator::make_Goto(~0u));

    const size_t cleanupFirst = customDropCall == ~0u ? 1 : 0;
    const auto cleanupStart = static_cast<MIRBasicBlockId>(mir.blocks.size());
    const auto cleanupBlock = [&](size_t field) {
        BUG_ASSERT(field >= cleanupFirst && field < values.size());
        return static_cast<MIRBasicBlockId>(cleanupStart + field - cleanupFirst);
    };

    if (cleanupFirst < values.size()) {
        const auto resume = static_cast<MIRBasicBlockId>(cleanupStart + values.size() - cleanupFirst);
        for (size_t i = cleanupFirst; i < values.size(); i++) {
            MIRBasicBlock block;
            block.isCleanup = true;
            const auto target = i + 1 < values.size() ? cleanupBlock(i + 1) : resume;
            block.terminator = MIRTerminator::make_Drop({
                MIRDropKind::DEEP,
                values[i].clone(),
                ~0u,
                target,
                MIRUnwindAction::make_Terminate({}),
            });
            mir.blocks.push_back(mv$(block));
        }
        MIRBasicBlock resumeBlock;
        resumeBlock.isCleanup = true;
        resumeBlock.terminator = MIRTerminator::make_UnwindResume({});
        mir.blocks.push_back(mv$(resumeBlock));
    }

    const auto normalStart = static_cast<MIRBasicBlockId>(mir.blocks.size());
    mir.blocks[entry].terminator.as_Goto() = normalStart;
    for (size_t i = 0; i < values.size(); i++) {
        MIRBasicBlock block;
        const auto target = static_cast<MIRBasicBlockId>(normalStart + i + 1);
        auto unwind = i + 1 < values.size() ? MIRUnwindAction::make_Cleanup(cleanupBlock(i + 1)) : MIRUnwindAction::make_Continue({});
        block.terminator = MIRTerminator::make_Drop({
            MIRDropKind::DEEP,
            values[i].clone(),
            ~0u,
            target,
            mv$(unwind),
        });
        mir.blocks.push_back(mv$(block));
    }
    mir.blocks.push_back(MIRBasicBlock());

    if (customDropCall != ~0u) {
        BUG_ASSERT(mir.blocks.at(customDropCall).terminator.is_Call());
        mir.blocks[customDropCall].terminator.as_Call().unwind = MIRUnwindAction::make_Cleanup(cleanupBlock(0));
    }
}

auto Builder::terminateBlock(MIRTerminator term) -> void {
    BUG_ASSERT(mir.blocks.back().terminator.is_Incomplete());
    mir.blocks.back().terminator = mv$(term);
}

auto Builder::terminateCall(MIRLValue rv, MIRCallTarget tgt, std::vector<MIRParam> args, MIRBasicBlockId bbRet, MIRBasicBlockId bbPanic, bool tracksCaller) -> void {
    this->terminateBlock(MIRTerminator::make_Call({bbRet, MIRUnwindAction::make_Cleanup(bbPanic), mv$(rv), mv$(tgt), mv$(args), {}, tracksCaller}));
}

auto Builder::pushCallDrop(const HIRTypeData* ty) -> MIRBasicBlockId {
    auto borrowLv = this->addLocal(state.crate.types.borrow(HIRBorrowType::Unique, ty));
    this->pushStmtAssign(borrowLv.clone(), MIRRValue::make_Borrow({HIRBorrowType::Unique, false, MIRLValue::newDeref(this->self.clone())}));

    ensureOpen();
    const auto callBlock = static_cast<MIRBasicBlockId>(mir.blocks.size() - 1);
    const auto retBlock = static_cast<MIRBasicBlockId>(mir.blocks.size());
    this->terminateBlock(
        MIRTerminator::make_Call({
            retBlock,
            MIRUnwindAction::make_Continue({}),
            MIRLValue::newReturn(),
            HIRPath(ty, state.resolve.langDrop(), "drop"),
            makeVec1<MIRParam>(mv$(borrowLv)),
        })
    );
    mir.blocks.push_back(MIRBasicBlock());
    return callBlock;
}

BindTranslationNominals::BindTranslationNominals(const HIRCrate& crate)
    : HIRVisitor(nullptr, crate.types)
    , crate(crate)
{
}

[[nodiscard]] auto BindTranslationNominals::visitType(const HIRTypeData* ty) -> const HIRTypeData* {
    auto data = ty->cloneData();
    visitTypeDataChildren(data);

    if (auto* pathTy = data.opt_Path()) {
        if (pathTy->binding.is_Unbound() && pathTy->path.data.is_Generic()) {
            const auto& path = pathTy->path.data.as_Generic().path;
            const auto& item = crate.getTypeitemByPath(Span(), path);
            switch (item.tag()) {
                default:
                    BUG(Span(), StringView("Nominal translation type points to ") << item.tagStr() << StringView(" - ") << ty);
                case HIRTypeItem::TAG_ExternType: {
                    auto& e = item.as_ExternType();
                    pathTy->binding = HIRTypePathBinding::make_ExternType(&e);
                    break;
                }
                case HIRTypeItem::TAG_Struct: {
                    auto& e = item.as_Struct();
                    pathTy->binding = HIRTypePathBinding::make_Struct(&e);
                    break;
                }
                case HIRTypeItem::TAG_Union: {
                    auto& e = item.as_Union();
                    pathTy->binding = HIRTypePathBinding::make_Union(&e);
                    break;
                }
                case HIRTypeItem::TAG_Enum: {
                    auto& e = item.as_Enum();
                    pathTy->binding = HIRTypePathBinding::make_Enum(&e);
                    break;
                }
            }
        }
    }

    return typeInterner().intern(mv$(data));
}

EnumState::EnumState(const WireBoard& wb)
    : crate(*wb.crate)
    , resolve(wb, OpaqueReveal::All)
    , rv(wb)
    , origList(nullptr)
{
    enumerateLinkFunctions();
}

auto EnumState::enumFcn(HIRPath p, const HIRFunction& fcn, TransParams pp) -> void {
    if (auto* e = rv.addFunction(crate.types, mv$(p))) {
        auto name = FMT(TransMangleValue(resolve.board(), *e->path));
        auto inserted = emittedFunctions.insert(name).second;
        ASSERT_BUG(Span(), inserted, StringView("Duplicated mangled name - ") << *e->path);
        fcnsToTypeVisit.pushBack(e);
        e->ptr = &fcn;
        e->pp = mv$(pp);
        DEBUG(*e->path << StringView(" w/ ") << e->pp.ppImpl << StringView(" and ") << e->pp.ppMethod);
        fcnQueue.push_back(e);
    }
}

auto EnumState::enumerateLinkFunctions() -> void {
    enumerateLinkFunctionsIn(crate.rootModule, HIRItemPath(crate.crateName));
    for (const auto& eCrate : crate.extCrates) {
        enumerateLinkFunctionsIn(eCrate.second.data->rootModule, HIRItemPath(eCrate.first));
    }
}

auto EnumState::enumerateLinkFunctionsIn(const HIRModule& mod, HIRItemPath modPath) -> void {
    for (const auto& vi : mod.valueItems) {
        if (const auto* ip = vi.second->ent.opt_Function()) {
            const auto& i = **ip;
            if (i.code.mir && i.linkage.name != "") {
                linkFunctions[i.linkage.name] = std::make_pair((modPath + vi.first).getSimplePath(), &i);
            }
        }
    }

    for (const auto& ti : mod.modItems) {
        if (const auto* ip = ti.second->ent.opt_Module()) {
            enumerateLinkFunctionsIn(*ip, modPath + ti.first);
        }
    }
}

GlobalAsmOperandEvaluator::GlobalAsmOperandEvaluator(const WireBoard& wb)
    : HIRVisitor(nullptr, wb.crate->types)
    , wb(wb)
    , crate(*wb.crate)
{
}

auto GlobalAsmOperandEvaluator::evaluate(HIRGlobalAssembly& item) -> void {
    span = &item.span;
    visitGlobalAssembly(item);
    span = nullptr;
}

auto GlobalAsmOperandEvaluator::visitConstgeneric(HIRConstGeneric& value) -> void {
    ConvertHIRConstantEvaluateConstGeneric(*span, wb, crate, value);
    ASSERT_BUG(*span, value.is_Evaluated(), StringView("global_asm operand remained unevaluated at translation"));
}

MIREnumCache::MIREnumCache() {
}

auto MIREnumCache::insertPath(const HIRPath& newPath) -> void {
    for (const auto* p : this->paths) {
        if (*p == newPath) {
            return;
        }
    }
    this->paths.pushBack(&newPath);
}

auto MIREnumCache::insertTypeid(const HIRTypeData* newTy) -> void {
    for (const auto* p : this->typeids) {
        if (p == newTy) {
            return;
        }
    }
    this->typeids.pushBack(newTy);
}

auto MIREnumCache::insertDestructorType(const HIRTypeData* newTy) -> void {
    for (const auto* p : this->destructorTypes) {
        if (p == newTy) {
            return;
        }
    }
    this->destructorTypes.pushBack(newTy);
}

auto MIREnumCache::apply(EnumState& state, const TransParams& pp) const -> void {
    TRACE_FUNCTION_F(StringView(" w/ impl=") << pp.ppImpl << StringView(" method=") << pp.ppMethod);
    for (const auto* tyP : this->typeids) {
        DEBUG(StringView("TypeID ") << tyP);
        state.rv.typeids.insert(pp.monomorph(state.resolve, tyP));
    }
    for (const auto* tyP : this->destructorTypes) {
        enumerateDestructorType(state, pp.monomorph(state.resolve, tyP));
    }
    for (const auto& path : this->paths) {
        DEBUG(StringView("Path ") << *path);
        TransEnumerateFillFromPath(state, *path, pp);
    }
}

template <typename F>
TransPathCb<F>::TransPathCb(F f)
    : f(f)
{
}

template <typename F>
auto TransPathCb<F>::get() -> HIRSimplePath {
    return f();
}

template <typename T>
auto PtrComp::operator()(const T* lhs, const T* rhs) const -> bool {
    return *lhs < *rhs;
}

TypeVisitor::TypeVisitor(const WireBoard& wb, TransList& out, const TransList* prevList)
    : crate(*wb.crate)
    , resolve(wb, OpaqueReveal::All)
    , out(out)
    , prevList(prevList)
{
}

TypeVisitor::~TypeVisitor() {
}

auto TypeVisitor::visitStruct(const HIRTypeData* selfType, const HIRGenericPath& path, const HIRStruct& item) -> void {
    Span sp;
    const HIRTypeData* tmp;
    size_t fieldCount = 0;
    MonomorphStatePtr ms(crate.types, selfType, &path.params, nullptr);
    auto monomorph = [&](const auto& x) {
        DEBUG(x);
        return resolve.monomorphExpandOpt(sp, x, ms);
    };
    switch (item.data.tag()) {
        case HIRStructData::TAG_Unit: {
            break;
        }
        case HIRStructData::TAG_Tuple: {
            auto& e = item.data.as_Tuple();
            fieldCount = e.size();
            for (const auto& fld : e) {
                visitType(monomorph(fld.ent));
            }
            break;
        }
        case HIRStructData::TAG_Named: {
            auto& e = item.data.as_Named();
            fieldCount = e.size();
            for (const auto& fld : e) {
                visitType(monomorph(fld.ty));
            }
            break;
        }
    }
    if (item.structMarkings.isAsyncDropGlue) {
        const auto* repr = TargetGetTypeRepr(sp, resolve, selfType);
        ASSERT_BUG(sp, repr && repr->fields.size() >= fieldCount, StringView("invalid async-drop glue representation for ") << selfType);
        for (size_t i = fieldCount; i < repr->fields.size(); i++) {
            visitType(repr->fields[i].ty);
        }
    }
}

auto TypeVisitor::visitUnion(const HIRTypeData* selfType, const HIRGenericPath& path, const HIRUnion& item) -> void {
    Span sp;
    const HIRTypeData* tmp;
    MonomorphStatePtr ms(crate.types, selfType, &path.params, nullptr);
    auto monomorph = [&](const auto& x) {
        return resolve.monomorphExpandOpt(sp, x, ms);
    };
    for (const auto& variant : item.variants) {
        visitType(monomorph(variant.ty));
    }
}

auto TypeVisitor::visitEnum(const HIRTypeData* selfType, const HIRGenericPath& path, const HIREnum& item) -> void {
    Span sp;
    const HIRTypeData* tmp;
    MonomorphStatePtr ms(crate.types, selfType, &path.params, nullptr);
    auto monomorph = [&](const auto& x) {
        return resolve.monomorphExpandOpt(sp, x, ms);
    };
    if (const auto* e = item.data.opt_Data()) {
        for (const auto& variant : *e) {
            visitType(monomorph(variant.type));
        }
    }
}

auto TypeVisitor::visitType(const HIRTypeData* ty, Mode mode) -> void {
    Span sp;
    if (visitTyWith(ty, [](const HIRTypeData* inner) {
        return inner->is_ErasedType();
    })) {
        const HIRTypeData* revealed = ty;
        revealed = resolve.revealOpaqueTypes(sp, revealed);
        revealed = resolve.expandAssociatedTypes(sp, revealed);
        ty = revealed;
    }
    if (out.hasType(ty, mode == Mode::Shallow)) {
        return;
    }

    TRACE_FUNCTION_F(ty << StringView(" - ") << (mode == Mode::Shallow ? "Shallow" : (mode == Mode::Normal ? "Normal" : "Deep")));
    if (mode == Mode::Shallow) {
        switch ((*ty).tag()) {
            default:
                break;
            case HIRTypeData::TAG_Infer: {
                BUG(sp, StringView("`_` type hit in enumeration"));
                break;
            }
            case HIRTypeData::TAG_Path: {
                auto& te = (*ty).as_Path();
                switch (te.binding.tag()) {
                    case HIRTypePathBinding::TAG_Unbound: {
                        BUG(sp, StringView("Unbound type hit in enumeration - ") << ty);
                        break;
                    }
                    case HIRTypePathBinding::TAG_Opaque: {
                        BUG(sp, StringView("Opaque type hit in enumeration - ") << ty);
                        break;
                    }
                    case HIRTypePathBinding::TAG_ExternType: {
                        break;
                    }
                    case HIRTypePathBinding::TAG_Struct: {
                        break;
                    }
                    case HIRTypePathBinding::TAG_Union: {
                        break;
                    }
                    case HIRTypePathBinding::TAG_Enum: {
                        break;
                    }
                }
                break;
            }
            case HIRTypeData::TAG_Array: {
                auto& te = (*ty).as_Array();
                ASSERT_BUG(sp, te.size.is_Known(), StringView("Encountered unknown array size - ") << ty);
                break;
            }
            case HIRTypeData::TAG_Function: {
                auto& te = (*ty).as_Function();
                visitType(te.rettype, Mode::Shallow);
                for (const auto& sty : te.argTypes) {
                    visitType(sty, Mode::Shallow);
                }
                break;
            }
            case HIRTypeData::TAG_Pointer: {
                auto& te = (*ty).as_Pointer();
                visitType(te.inner, Mode::Shallow);
                break;
            }
            case HIRTypeData::TAG_Borrow: {
                auto& te = (*ty).as_Borrow();
                visitType(te.inner, Mode::Shallow);
                break;
            }
            case HIRTypeData::TAG_Pattern: {
                auto& te = (*ty).as_Pattern();
                visitType(te.inner, Mode::Shallow);
                break;
            }
        }
    } else {
        if (activeSet.find(ty) != activeSet.end()) {
            // TODO: Handle recursion
            BUG(sp, StringView("- Type recursion on ") << ty);
        }
        activeSet.insert(ty);

        switch ((*ty).tag()) {
            case HIRTypeData::TAG_Infer: {
                BUG(sp, StringView("`_` type hit in enumeration"));
                break;
            }
            case HIRTypeData::TAG_Generic: {
                BUG(sp, StringView("Generic type hit in enumeration - ") << ty);
                break;
            }
            case HIRTypeData::TAG_ErasedType: {
                break;
            }
            case HIRTypeData::TAG_NodeType: {
                BUG(sp, StringView("NodeType type hit in enumeration - ") << ty);
                break;
            }
            case HIRTypeData::TAG_Diverge: {
                break;
            }
            case HIRTypeData::TAG_Primitive: {
                break;
            }
            case HIRTypeData::TAG_Path: {
                auto& te = (*ty).as_Path();
                switch (te.binding.tag()) {
                    case HIRTypePathBinding::TAG_Unbound: {
                        BUG(sp, StringView("Unbound type hit in enumeration - ") << ty);
                        break;
                    }
                    case HIRTypePathBinding::TAG_Opaque: {
                        BUG(sp, StringView("Opaque type hit in enumeration - ") << ty);
                        break;
                    }
                    case HIRTypePathBinding::TAG_ExternType: {
                        break;
                    }
                    case HIRTypePathBinding::TAG_Struct: {
                        auto& tpb = te.binding.as_Struct();
                        visitStruct(ty, te.path.data.as_Generic(), *tpb);
                        break;
                    }
                    case HIRTypePathBinding::TAG_Union: {
                        auto& tpb = te.binding.as_Union();
                        visitUnion(ty, te.path.data.as_Generic(), *tpb);
                        break;
                    }
                    case HIRTypePathBinding::TAG_Enum: {
                        auto& tpb = te.binding.as_Enum();
                        TargetGetTypeRepr(sp, resolve, ty);
                        visitEnum(ty, te.path.data.as_Generic(), *tpb);
                        break;
                    }
                }
                break;
            }
            case HIRTypeData::TAG_TraitObject: {
                auto& te = (*ty).as_TraitObject();
                Span sp;

                if (!te.trait.path.path.components().empty()) {
                    const auto& trait = *te.trait.traitPtr;
                    auto vtableTy = trait.getVtableType(sp, crate, te);

                    visitType(vtableTy);
                } else {
                }
                break;
            }
            case HIRTypeData::TAG_Array: {
                auto& te = (*ty).as_Array();
                ASSERT_BUG(sp, te.size.is_Known(), StringView("Encountered unknown array size - ") << ty);
                visitType(te.inner, mode);
                break;
            }
            case HIRTypeData::TAG_Slice: {
                auto& te = (*ty).as_Slice();
                visitType(te.inner, mode);
                break;
            }
            case HIRTypeData::TAG_Pattern: {
                auto& te = (*ty).as_Pattern();
                visitType(te.inner, mode);
                break;
            }
            case HIRTypeData::TAG_Borrow: {
                auto& te = (*ty).as_Borrow();
                visitType(te.inner, mode != Mode::Deep ? Mode::Shallow : Mode::Deep);
                break;
            }
            case HIRTypeData::TAG_Pointer: {
                auto& te = (*ty).as_Pointer();
                visitType(te.inner, mode != Mode::Deep ? Mode::Shallow : Mode::Deep);
                break;
            }
            case HIRTypeData::TAG_Tuple: {
                auto& te = (*ty).as_Tuple();
                for (const auto& sty : te) {
                    visitType(sty, mode);
                }
                break;
            }
            case HIRTypeData::TAG_NamedFunction: {
                break;
            }
            case HIRTypeData::TAG_Function: {
                auto& te = (*ty).as_Function();
                visitType(te.rettype, mode != Mode::Deep ? Mode::Shallow : Mode::Deep);
                for (const auto& sty : te.argTypes) {
                    visitType(sty, mode != Mode::Deep ? Mode::Shallow : Mode::Deep);
                }
                break;
            }
        }
        activeSet.erase(ty);
    }

    bool shallow = (mode == Mode::Shallow);
    auto i = out.types.size();
    ASSERT_BUG(sp, out.addType(ty, shallow), StringView("Type was emitted while it was being visited: ") << ty);
    DEBUG(StringView("Add type ") << ty << (shallow ? " (Shallow)" : "") << StringView(" ") << i);
}

void __attribute__((noinline)) TypeVisitor::visitFunction(const HIRPath& path, const HIRFunction& fcn, const TransParams& pp) {
    Span sp;
    auto& tv = *this;

    const HIRTypeData* tmp;
    bool useMonomorph = true;
    auto monomorph = [&](const HIRTypeData* ty) -> const HIRTypeData* {
        return useMonomorph ? pp.maybeMonomorph(resolve, ty) : ty;
    };
    DEBUG(fcn.returnType);
    bool hasErased = visitTyWith(fcn.returnType, [&](const auto& x) {
        return x->is_ErasedType();
    });
    if (hasErased || monomorphiseTypeNeeded(fcn.returnType)) {
        const HIRTypeData* retTy;
        if (hasErased) {
            retTy = cloneTyWith(crate.types, sp, fcn.returnType, [&](const auto* x) -> const HIRTypeData* {
                if (const auto* te = x->opt_ErasedType()) {
                    if (const auto* e = te->inner.opt_Fcn()) {
                        BUG_ASSERT(e->index < fcn.code.erasedTypes.length());
                        return fcn.code.erasedTypes[e->index];
                    }
                }
                return nullptr;
            });
            DEBUG(retTy);
            retTy = pp.monomorph(tv.resolve, retTy);
        } else {
            retTy = pp.monomorph(tv.resolve, fcn.returnType);
        }
        tv.visitType(retTy);
    } else {
        tv.visitType(fcn.returnType);
    }
    for (const auto& arg : fcn.args) {
        DEBUG(arg.second);
        tv.visitType(monomorph(arg.second));
    }

    const MIRFunction* mirP = nullptr;
    if (fcn.code.mir) {
        mirP = &*fcn.code.mir;
    }
    if (prevList) {
        const auto* transFcn = prevList->findFunction(path);
        ASSERT_BUG(sp, transFcn, StringView("Unable to find ") << path << StringView(" in first-pass enumerate result"));
        if (transFcn && transFcn->monomorphised.code) {
            mirP = &*transFcn->monomorphised.code;
            useMonomorph = false;
        }
    }
    if (mirP) {
        const MIRFunction& mir = *mirP;
        for (const auto& ty : mir.locals) {
            tv.visitType(monomorph(ty));
        }

        MIRTypeResolve::argsT emptyArgs;
        const HIRTypeData* emptyTy;
        auto pathCallback = makeCallable<MIRPathCb>([](auto&) {});
        MIRTypeResolve localMirRes(sp, tv.resolve, pathCallback, /*ret_ty=*/emptyTy, emptyArgs, mir);
        for (const auto& block : mir.blocks) {
            struct MirVisitor: public MIRVisitor {
                const Span& sp;
                TypeVisitor& tv;
                const TransParams& pp;
                const HIRFunction& fcn;
                const MIRTypeResolve& localMirRes;

                MirVisitor(const Span& sp, TypeVisitor& tv, const TransParams& pp, const HIRFunction& fcn, const MIRTypeResolve& localMirRes)
                    : sp(sp)
                    , tv(tv)
                    , pp(pp)
                    , fcn(fcn)
                    , localMirRes(localMirRes)
                {
                }

                bool visitLvalue(const MIRLValue& lv, MIRValUsage /*vu*/) override {
                    TRACE_FUNCTION_F(lv);
                    if (std::none_of(lv.wrappers.begin(), lv.wrappers.end(), [](const auto& w) {
                        return w.is_Deref();
                    })) {
                        return false;
                    }
                    const HIRTypeData* tmp;
                    auto monomorphOuter = [&](const auto& tpl) {
                        return pp.maybeMonomorph(tv.resolve, tpl);
                    };
                    const HIRTypeData* ty = nullptr;
                    ;
                    switch (lv.root.tag()) {
                        case MIRLValue::Storage::TAG_Return: {
                            MIR_TODO(localMirRes, StringView("Get return type for MIR type enumeration"));
                            break;
                        }
                        case MIRLValue::Storage::TAG_Argument: {
                            decltype(lv.root.as_Argument()) e = lv.root.as_Argument();
                            ty = monomorphOuter(fcn.args[e].second);
                            break;
                        }
                        case MIRLValue::Storage::TAG_Local: {
                            decltype(lv.root.as_Local()) e = lv.root.as_Local();
                            if (&localMirRes.fcn == &*fcn.code.mir) {
                                ty = monomorphOuter(fcn.code.mir->locals[e]);
                            } else {
                                ty = localMirRes.fcn.locals[e];
                            }
                            break;
                        }
                        case MIRLValue::Storage::TAG_Static: {
                            decltype(lv.root.as_Static()) e = lv.root.as_Static();
                            // TODO: Monomorphise the path then hand to MIR::TypeResolve?
                            const auto& path = e;
                            switch (path.data.tag()) {
                                case HIRPathData::TAG_Generic: {
                                    auto& pe = path.data.as_Generic();
                                    MIR_ASSERT(localMirRes, pe.params.types.empty(), StringView("Path params on static - ") << path);
                                    const auto& s = tv.resolve.hirCrate().getStaticByPath(localMirRes.sp, pe.path);
                                    ty = s.type;
                                    break;
                                }
                                case HIRPathData::TAG_UfcsKnown: {
                                    MIR_TODO(localMirRes, StringView("LValue::Static - UfcsKnown - ") << path);
                                }
                                case HIRPathData::TAG_UfcsUnknown: {
                                    MIR_BUG(localMirRes, StringView("Encountered UfcsUnknown in LValue::Static - ") << path);
                                }
                                case HIRPathData::TAG_UfcsInherent: {
                                    MIR_TODO(localMirRes, StringView("LValue::Static - UfcsInherent - ") << path);
                                }
                            }
                            break;
                        }
                    }
                    BUG_ASSERT(ty);

                    for (const auto& w : lv.wrappers) {
                        ty = localMirRes.getUnwrappedType(w, ty);
                        if (w.is_Deref()) {
                            tv.visitType(ty);
                        }
                    }
                    return false;
                }

                bool visitConst(const MIRConstant& c) override {
                    if (c.is_Bytes()) {
                        const HIRTypeData* tmp;
                        auto ty = localMirRes.getConstType(c);
                        tv.visitType(pp.maybeMonomorph(tv.resolve, ty));
                    }
                    return MIRVisitor::visitConst(c);
                }

                void visitPath(const HIRPath& /*p*/) override {
                }

                const HIRTypeData* visitType(const HIRTypeData* ty) override {
                    const HIRTypeData* tmp;
                    tv.visitType(pp.maybeMonomorph(tv.resolve, ty));
                    return ty;
                }
            };

            MirVisitor mirVisit(sp, tv, pp, fcn, localMirRes);
            for (const auto& stmt : block.statements) {
                DEBUG(stmt);
                mirVisit.visitStmt(stmt);
            }
            DEBUG(block.terminator);
            mirVisit.visitTerminator(block.terminator);

            // HACK: Currently calling `caller_location` creates an empty location (so needs the type)
            if (block.terminator.is_Call() && block.terminator.as_Call().fcn.is_Intrinsic()) {
                const auto& e2 = block.terminator.as_Call().fcn.as_Intrinsic();
                if (e2.name == "caller_location") {
                    const auto& p = localMirRes.resolve.hirCrate().getLangItemPath(sp, "panic_location");
                    const auto& s = localMirRes.resolve.hirCrate().getStructByPath(sp, p);
                    tv.visitType(tv.crate.types.path(HIRPath(p), &s));
                } else if (e2.name == "offset") {
                    const HIRTypeData* tmp;
                    const auto& ty = pp.maybeMonomorph(tv.resolve, e2.params.types.at(0));
                    tv.visitType(ty->as_Pointer().inner);
                }
            }
            if (block.terminator.is_Call() && block.terminator.as_Call().fcn.is_Path()) {
                const auto& p = block.terminator.as_Call().fcn.as_Path();
                if (p.data.is_UfcsKnown()) {
                    const HIRTypeData* tmp;
                    const auto& ty = pp.maybeMonomorph(tv.resolve, p.data.as_UfcsKnown().type);
                    if (ty->is_TraitObject()) {
                        tv.visitType(ty);
                    }
                }
            }
        }
    }
}
