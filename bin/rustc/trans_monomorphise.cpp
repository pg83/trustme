#include "trans_monomorphise.h"

#include "hir_hir.h"
#include "mir_mir.h"
#include "wire_board.h"
#include "mir_helpers.h"
#include "mir_operations.h" // Needed for post-monomorph checks and optimisations
#include "hir_typeck_static.h"
#include "trans_main_bindings.h"
#include "hir_conv_constant_evaluation.h"

#include <std/lib/vector.h>

using namespace stl;

namespace {
    class AsyncDropPollBuilder {
        const Span& sp;
        const StaticTraitResolve& resolve;
        const HIRTypeData* dropeeTy;
        const HIRTypeData* outerTy;
        MIRFunction output;
        unsigned statePtrLocal;
        unsigned nextPhase = 3;
        Vector<::std::pair<unsigned, MIRBasicBlockId>> resumeTargets;

        class CoroutineDropCloner: public MIRCloner {
            const AsyncDropPollBuilder& owner;
            const MonomorphState& params;
            MIRLValue dropee;
            unsigned bbBase;
            unsigned localBase;
            unsigned dropFlagBase;
            unsigned returnLocal;

        public:
            CoroutineDropCloner(const AsyncDropPollBuilder& owner, const MonomorphState& monomorph, MIRLValue dropee, unsigned bbBase, unsigned localBase, unsigned dropFlagBase, unsigned returnLocal);

            MIRBasicBlockId mapBbIdx(MIRBasicBlockId idx) const override;

            unsigned mapLocal(unsigned idx) const override;

            unsigned mapDropFlag(unsigned idx) const override;

            const Monomorphiser& monomorphiser() const override;

            const StaticTraitResolve* resolve() const override;

            MIRStatement cloneStmt(const MIRStatement& src) const override;

            MIRLValue cloneLval(const MIRLValue& src) const override;
        };

        MIRBasicBlockId newBlock();

        unsigned newLocal(HIRTypeRef ty);

        MIRLValue outerValue() const;

        MIRLValue stateValue() const;

        MIRRValue pollResult(unsigned variant) const;

        MIRStatement setState(unsigned state) const;

        bool hasDropImpl(const HIRTypeData* ty) const;

        MIRBasicBlockId buildSyncDestructor(const HIRTypeData* ty, MIRLValue value, MIRBasicBlockId next);

        MIRBasicBlockId buildAsyncDestructor(const HIRTypeData* ty, MIRLValue value, HIRPath dropPath, HIRTypeRef futureTy, MIRBasicBlockId next);

        MIRBasicBlockId buildDeepDrop(MIRLValue value, MIRBasicBlockId next);

        MIRBasicBlockId buildField(const HIRTypeData* ty, MIRLValue value, MIRBasicBlockId next);

        MIRBasicBlockId buildStructFields(const HIRTypeData* ty, MIRLValue value, MIRBasicBlockId next);

        MIRBasicBlockId buildFields(const HIRTypeData* ty, MIRLValue value, MIRBasicBlockId next);

        MIRBasicBlockId buildCoroutineDrop(const HIRTypeData* ty, MIRLValue value, MIRBasicBlockId next);

        MIRBasicBlockId buildType(const HIRTypeData* ty, MIRLValue value, MIRBasicBlockId next);

    public:
        AsyncDropPollBuilder(const Span& sp, const StaticTraitResolve& resolve, const HIRTypeData* dropeeTy, const HIRTypeData* outerTy);

        MIRFunctionPointer build();
    };

    const MIRCallTarget::Data_Intrinsic* asyncDropPollMarker(const MIRFunctionPointer& tpl) {
        if (!tpl || tpl->blocks.empty()) {
            return nullptr;
        }
        const auto* call = tpl->blocks.front().terminator.opt_Call();
        if (!call) {
            return nullptr;
        }
        const auto* intrinsic = call->fcn.opt_Intrinsic();
        return intrinsic && intrinsic->name == "async_drop_glue_poll" ? intrinsic : nullptr;
    }

    class Cloner: public MIRCloner {
        const ::StaticTraitResolve& resolve_;
        const TransParams& params;

    public:
        Cloner(const Span& sp, const ::StaticTraitResolve& resolve, const TransParams& params);

        const HIRTypeData* valueGenericType(HIRGenericRef g) const override;

        const Monomorphiser& monomorphiser() const override;

        const StaticTraitResolve* resolve() const override;
    };
}

MIRFunctionPointer TransMonomorphise(const ::StaticTraitResolve& resolve, const TransParams& params, const MIRFunctionPointer& tpl) {
    Span sp;
    assert(tpl);

    if (const auto* marker = asyncDropPollMarker(tpl)) {
        ASSERT_BUG(sp, marker->params.types.size() == 2, "async-drop poll marker has " << marker->params.types.size() << " type arguments");
        auto dropeeTy = params.monomorph(resolve, marker->params.types[0]);
        auto outerTy = params.monomorph(resolve, marker->params.types[1]);
        ASSERT_BUG(sp, !monomorphiseTypeNeeded(dropeeTy) && !monomorphiseTypeNeeded(outerTy), "async-drop poll remained generic after monomorphisation: " << dropeeTy << " in " << outerTy);
        return AsyncDropPollBuilder(sp, resolve, dropeeTy, outerTy).build();
    }

    MIRFunction output;

    // 1. Monomorphise locals and temporaries
    output.locals.reserve(tpl->locals.size());
    for (const auto& var : tpl->locals) {
        output.locals.push_back(params.monomorph(resolve, var));
    }
    output.dropFlags = tpl->dropFlags;

    Cloner c{sp, resolve, params};
    // 2. Monomorphise all paths
    output.blocks.reserve(tpl->blocks.size());
    for (const auto& block : tpl->blocks) {
        ::std::vector<MIRStatement> statements;

        statements.reserve(block.statements.size());
        for (const auto& stmt : block.statements) {
            switch (stmt.tag()) {
                // LAZY: These _should_ be in `clone_stmt`, but they're not needed in optimising and MIR cloning
                break;
                case MIRStatement::TAG_SaveDropFlag: {
                    auto& e = stmt.as_SaveDropFlag();
                    statements.push_back(MIRStatement::make_SaveDropFlag({e.slot.clone(), e.bitIndex, e.idx}));

                } break;
                    break;
                case MIRStatement::TAG_LoadDropFlag: {
                    auto& e = stmt.as_LoadDropFlag();
                    statements.push_back(MIRStatement::make_LoadDropFlag({e.idx, e.slot.clone(), e.bitIndex}));

                } break;
                default:
                    statements.push_back(c.cloneStmt(stmt));
                    break;
            }
        }

        MIRTerminator terminator = c.cloneTerm(block.terminator);
        output.blocks.push_back(MIRBasicBlock{mv$(statements), mv$(terminator)});
    }

    return MIRFunctionPointer(box$(output).release());
}

/// Monomorphise all values and functions in a TransList.
void TransMonomorphiseList(const WireBoard& wb, HIRCrate& crate, TransList& list, unsigned mirOptLevel) {
    ::StaticTraitResolve resolve{wb, OpaqueReveal::All};

    struct Nvs: public HIREvaluator::Newval {
        TransList& out;
        const HIRCrate& crate;
        unsigned count;
        ::std::vector<std::pair<HIRSimplePath, HIRStatic*>> added;

        Nvs(TransList& out, const HIRCrate& crate)
            : out(out)
            , crate(crate)
            , count(0)
        {
        }

        HIRPath newStatic(HIRTypeRef type, EncodedLiteral value, size_t alignment) override {
            // Ensure that the type is in enumeration (it should have been, but maybe not?)
            out.addType(type, false);
            auto name = RcString::newInterned(FMT("ConstEvalMonomorph#" << count));
            count++;
            auto p = HIRSimplePath(crate.crateName, {name});
            auto* ent = crate.pool->make<HIRVisEnt<HIRValueItem>>(HIRVisEnt<HIRValueItem>{HIRPublicity::newGlobal(), HIRValueItem(crate.pool->make<HIRStatic>(HIRStatic(HIRLinkage(), false, std::move(type), HIRExprPtr())))});

            {
                auto& s = *ent->ent.as_Static();
                s.explicitAlignment = alignment;
                s.valueGenerated = true;
                s.valueRes = std::move(value);
                s.saveLiteral = false;
                added.push_back(std::make_pair(p, &s));
            }
            const_cast<HIRModule&>(crate.rootModule).valueItems.insert(std::make_pair(name, std::move(ent)));
            return p;
        }
    } nvs{list, crate};

    ::std::set<const TransListConst*> evaluatedConstants;
    ::std::set<const TransListStatic*> evaluatedStatics;
    size_t insertedStatics = 0;

    // CTFE can materialise a global allocation containing relocations to
    // translation items that were absent from the initial graph.  Enumerating
    // those items can in turn expose more monomorphised constants, so drive
    // value evaluation and late enumeration to a fixpoint before touching
    // function MIR.
    bool changed;
    do {
        changed = false;
        // A generated literal can relocate to a static that Newval has added
        // to HIR but that the insertion loop below has not put in TransList
        // yet. Defer relocation enumeration until those statics are present.
        Vector<const EncodedLiteral*> generatedLiterals;

        // Reverse order is intentional: const-eval commonly needs constants
        // referenced by a later entry to have been evaluated first.
        for (auto& ent : reverse(list.constants)) {
            if (!evaluatedConstants.insert(ent.second.get()).second) {
                continue;
            }
            changed = true;

            const auto& path = ent.first;
            const auto& pp = ent.second->pp;
            const auto& c = *ent.second->ptr;
            auto ty = pp.monomorph(resolve, c.type);
            auto eval = HIREvaluator{pp.sp, wb, nvs};
            eval.resolve.setBothGenericsRaw(pp.gdefImpl, &c.params);
            MonomorphState ms(crate.types);
            ms.selfTy = pp.selfType;
            ms.ppImpl = &pp.ppImpl;
            ms.ppMethod = &pp.ppMethod;
            {
                auto newLit = eval.evaluateConstant(path, c.value, ::std::move(ty), ::std::move(ms));
                auto inserted = c.monomorphCache.insert(::std::make_pair(path.clone(), ::std::move(newLit)));
                generatedLiterals.pushBack(&inserted.first->second);
            }
        }

        for (auto& ent : list.statics) {
            if (!ent.second->ptr->params.isGeneric() || !evaluatedStatics.insert(ent.second.get()).second) {
                continue;
            }
            changed = true;

            const auto& path = ent.first;
            const auto& pp = ent.second->pp;
            const auto& s = *ent.second->ptr;
            auto ty = pp.monomorph(resolve, s.type);
            auto eval = HIREvaluator{pp.sp, wb, nvs};
            eval.resolve.setBothGenericsRaw(pp.gdefImpl, &s.params);
            MonomorphState ms(crate.types);
            ms.selfTy = pp.selfType;
            ms.ppImpl = &pp.ppImpl;
            ms.ppMethod = &pp.ppMethod;
            {
                auto newLit = eval.evaluateConstant(path, s.value, ::std::move(ty), ::std::move(ms));
                s.monomorphCache.insert(::std::make_pair(path.clone(), ::std::move(newLit)));
            }
        }

        ::std::vector<HIRPath> generated;
        generated.reserve(nvs.added.size() - insertedStatics);
        while (insertedStatics < nvs.added.size()) {
            auto& value = nvs.added[insertedStatics++];
            auto* out = list.addStatic(crate.types, HIRPath(value.first));
            ASSERT_BUG(Span(), out, "Generated static " << value.first << " already in TransList?");
            out->ptr = value.second;
            generated.push_back(HIRPath(value.first));
        }

        bool enumeratedItems = false;
        if (!generated.empty()) {
            changed = true;
            TransEnumerateGeneratedStatics(wb, list, generated);
            enumeratedItems = true;
        }
        for (const auto* literal : generatedLiterals) {
            enumeratedItems |= TransEnumerateGeneratedLiteral(wb, list, *literal);
        }
        if (enumeratedItems) {
            changed = true;
            TransAutoImpls(wb, crate, list);
        }
    } while (changed);

    // MIR cleanup can make a previously generic coercion concrete and insert
    // translation paths such as `<T as Trait>::vtable#`.  Those paths do not
    // exist in the pre-monomorphisation MIR, so collect and prepare them until
    // the translation graph reaches a fixed point.
    ::std::set<const TransListFunction*> processedFunctions;
    while (processedFunctions.size() < list.functions.size()) {
        Vector<const TransListFunction*> generatedFunctions;
        for (auto& fcnEnt : list.functions) {
            auto* transFcn = fcnEnt.second.get();
            if (!processedFunctions.insert(transFcn).second) {
                continue;
            }

            const auto& fcn = *transFcn->ptr;
            // Trait methods (which are the only case where `Self` can exist in the argument list at this stage) always need to be monomorphised.
            bool isMethod = (fcn.args.size() > 0 && visitTyWith(fcn.args[0].second, [&](const auto& x) {
                return x == crate.types.self();
            }));
            bool monomorphNeeded = transFcn->pp.hasTypes() || isMethod;

            if (monomorphNeeded) {
                const auto& path = fcnEnt.first;
                const auto& pp = transFcn->pp;
                ASSERT_BUG(Span(), fcn.code.mir, "No code for " << path);

                // TODO: Get the item params too
                if (pp.ppImpl.hasParams()) {
                    assert(pp.gdefImpl);
                }
                resolve.setBothGenericsRaw(pp.gdefImpl, &fcn.params);

                auto mir = TransMonomorphise(resolve, pp, fcn.code.mir);

                // TODO: Should these be moved to their own pass? Potentially not, the extra pass should just be an inlining optimise pass
                auto retType = pp.monomorph(resolve, fcn.returnType);
                HIRFunction::argsT args;
                for (const auto& a : fcn.args) {
                    args.push_back(::std::make_pair(HIRPattern{}, pp.monomorph(resolve, a.second)));
                }

                HIRItemPath ip(path);
                MIRCleanup(resolve, ip, *mir, args, retType);
                if (mirOptLevel == 0) {
                    MIROptimiseMin(resolve, ip, *mir, args, retType);
                } else {
                    MIROptimise(resolve, ip, *mir, args, retType, mirOptLevel, /*do_inline*/ false);
                }

                transFcn->monomorphised.retTy = ::std::move(retType);
                transFcn->monomorphised.argTys = ::std::move(args);
                transFcn->monomorphised.code = ::std::move(mir);
                generatedFunctions.pushBack(transFcn);
                resolve.clearBothGenerics();
            } else {
                // TransAutoImpls may have generated concrete MIR before this
                // fixed point started.  Enumerate it again: the initial walk
                // only saw the function path, not the generated body.
                if (fcn.code.mir) {
                    generatedFunctions.pushBack(transFcn);
                }
            }
        }

        if (TransEnumerateGeneratedMIR(wb, list, generatedFunctions)) {
            TransAutoImpls(wb, crate, list);
        }
    }
}

auto AsyncDropPollBuilder::newBlock() -> MIRBasicBlockId {
    output.blocks.push_back(MIRBasicBlock{});
    return static_cast<MIRBasicBlockId>(output.blocks.size() - 1);
}

auto AsyncDropPollBuilder::newLocal(HIRTypeRef ty) -> unsigned {
    const auto rv = static_cast<unsigned>(output.locals.size());
    output.locals.push_back(std::move(ty));
    return rv;
}

auto AsyncDropPollBuilder::outerValue() const -> MIRLValue {
    return MIRLValue::newDeref(MIRLValue::newField(MIRLValue::newArgument(0), 0));
}

auto AsyncDropPollBuilder::stateValue() const -> MIRLValue {
    return MIRLValue::newDeref(MIRLValue::newLocal(statePtrLocal));
}

auto AsyncDropPollBuilder::pollResult(unsigned variant) const -> MIRRValue {
    HIRPathParams params(resolve.hirCrate().types.unit());
    const auto& path = resolve.hirCrate().getLangItemPath(sp, "Poll");
    return MIRRValue::make_EnumVariant({HIRGenericPath(path, std::move(params)), variant, {}});
}

auto AsyncDropPollBuilder::setState(unsigned state) const -> MIRStatement {
    return MIRStatement::make_Assign({
        stateValue(),
        MIRRValue::make_Constant(MIRConstant::make_Uint({U128(state), HIRCoreType::U8})),
    });
}

auto AsyncDropPollBuilder::hasDropImpl(const HIRTypeData* ty) const -> bool {
    const auto& trait = resolve.langDrop();
    return !trait.components().empty() && resolve.findImpl(sp, trait, HIRPathParams{}, ty, [](ImplRef impl, SolverCertainty certainty) {
        return certainty == SolverCertainty::Proven && impl.data.is_TraitImpl();
    });
}

auto AsyncDropPollBuilder::buildSyncDestructor(const HIRTypeData* ty, MIRLValue value, MIRBasicBlockId next) -> MIRBasicBlockId {
    auto& types = resolve.hirCrate().types;
    const auto refLocal = newLocal(types.borrow(HIRBorrowType::Unique, ty));
    const auto resultLocal = newLocal(types.unit());
    const auto entry = newBlock();
    output.blocks[entry].statements.push_back(
        MIRStatement::make_Assign({
            MIRLValue::newLocal(refLocal),
            MIRRValue::make_Borrow({HIRBorrowType::Unique, false, std::move(value)}),
        })
    );
    output.blocks[entry].terminator = MIRTerminator::make_Call({
        next,
        MIRUnwindAction::make_Continue({}),
        MIRLValue::newLocal(resultLocal),
        HIRPath(ty, HIRGenericPath(resolve.langDrop()), RcString::newInterned("drop"), HIRPathParams{}),
        ::makeVec1<MIRParam>(MIRLValue::newLocal(refLocal)),
    });
    return entry;
}

auto AsyncDropPollBuilder::buildAsyncDestructor(const HIRTypeData* ty, MIRLValue value, HIRPath dropPath, HIRTypeRef futureTy, MIRBasicBlockId next) -> MIRBasicBlockId {
    auto& types = resolve.hirCrate().types;
    const auto& pinPath = resolve.hirCrate().getLangItemPathOpt("pin");
    const auto& pollPath = resolve.hirCrate().getLangItemPathOpt("Poll");
    ASSERT_BUG(sp, !pinPath.components().empty(), "AsyncDrop poll for " << ty << " without the Pin lang item");
    ASSERT_BUG(sp, !pollPath.components().empty(), "AsyncDrop poll for " << ty << " without the Poll lang item");
    ASSERT_BUG(sp, !resolve.langFuture().components().empty(), "AsyncDrop poll for " << ty << " without the Future lang item");
    const auto storagePtrLocal = newLocal(types.pointer(HIRBorrowType::Unique, futureTy));
    const auto valueRefTy = types.borrow(HIRBorrowType::Unique, ty);
    const auto valueRefLocal = newLocal(valueRefTy);
    const auto valuePinTy = types.path(HIRGenericPath(pinPath, HIRPathParams(valueRefTy)), &resolve.hirCrate().getStructByPath(sp, pinPath));
    const auto valuePinLocal = newLocal(valuePinTy);
    const auto futureRefTy = types.borrow(HIRBorrowType::Unique, futureTy);
    const auto futureRefLocal = newLocal(futureRefTy);
    const auto futurePinTy = types.path(HIRGenericPath(pinPath, HIRPathParams(futureRefTy)), &resolve.hirCrate().getStructByPath(sp, pinPath));
    const auto futurePinLocal = newLocal(futurePinTy);
    const auto pollTy = types.path(HIRGenericPath(pollPath, HIRPathParams(types.unit())), &resolve.hirCrate().getEnumByPath(sp, pollPath));
    const auto pollLocal = newLocal(pollTy);

    const auto phase = nextPhase++;
    const auto getStorage = newBlock();
    const auto makeValuePin = newBlock();
    const auto construct = newBlock();
    const auto markPolling = newBlock();
    const auto resumeGetStorage = newBlock();
    const auto makeFuturePin = newBlock();
    const auto poll = newBlock();
    const auto inspect = newBlock();
    const auto ready = newBlock();
    const auto pending = newBlock();

    HIRPathParams storageParams;
    storageParams.types.push_back(outerTy);
    storageParams.types.push_back(futureTy);
    output.blocks[getStorage].terminator = MIRTerminator::make_Call({
        makeValuePin,
        MIRUnwindAction::make_Continue({}),
        MIRLValue::newLocal(storagePtrLocal),
        MIRCallTarget::make_Intrinsic({"async_drop_storage", storageParams.clone()}),
        ::makeVec1<MIRParam>(MIRParam::make_Borrow({HIRBorrowType::Unique, outerValue()})),
    });
    output.blocks[makeValuePin].statements.push_back(
        MIRStatement::make_Assign({
            MIRLValue::newLocal(valueRefLocal),
            MIRRValue::make_Borrow({HIRBorrowType::Unique, false, value.clone()}),
        })
    );
    output.blocks[makeValuePin].terminator = MIRTerminator::make_Call({
        construct,
        MIRUnwindAction::make_Continue({}),
        MIRLValue::newLocal(valuePinLocal),
        HIRPath(valuePinTy, "new_unchecked"),
        ::makeVec1<MIRParam>(MIRLValue::newLocal(valueRefLocal)),
    });
    output.blocks[construct].terminator = MIRTerminator::make_Call({
        markPolling,
        MIRUnwindAction::make_Continue({}),
        MIRLValue::newDeref(MIRLValue::newLocal(storagePtrLocal)),
        std::move(dropPath),
        ::makeVec1<MIRParam>(MIRLValue::newLocal(valuePinLocal)),
    });
    output.blocks[markPolling].statements.push_back(setState(phase));
    output.blocks[markPolling].terminator = MIRTerminator::make_Goto(makeFuturePin);

    output.blocks[resumeGetStorage].terminator = MIRTerminator::make_Call({
        makeFuturePin,
        MIRUnwindAction::make_Continue({}),
        MIRLValue::newLocal(storagePtrLocal),
        MIRCallTarget::make_Intrinsic({"async_drop_storage", std::move(storageParams)}),
        ::makeVec1<MIRParam>(MIRParam::make_Borrow({HIRBorrowType::Unique, outerValue()})),
    });
    resumeTargets.pushBack(std::make_pair(phase, resumeGetStorage));

    output.blocks[makeFuturePin].statements.push_back(
        MIRStatement::make_Assign({
            MIRLValue::newLocal(futureRefLocal),
            MIRRValue::make_Borrow({HIRBorrowType::Unique, false, MIRLValue::newDeref(MIRLValue::newLocal(storagePtrLocal))}),
        })
    );
    output.blocks[makeFuturePin].terminator = MIRTerminator::make_Call({
        poll,
        MIRUnwindAction::make_Continue({}),
        MIRLValue::newLocal(futurePinLocal),
        HIRPath(futurePinTy, "new_unchecked"),
        ::makeVec1<MIRParam>(MIRLValue::newLocal(futureRefLocal)),
    });
    output.blocks[poll].terminator = MIRTerminator::make_Call({
        inspect,
        MIRUnwindAction::make_Continue({}),
        MIRLValue::newLocal(pollLocal),
        HIRPath(futureTy, resolve.langFuture(), "poll"),
        ::makeVec2<MIRParam>(MIRLValue::newLocal(futurePinLocal), MIRParam::make_Borrow({HIRBorrowType::Unique, MIRLValue::newDeref(MIRLValue::newArgument(1))})),
    });
    output.blocks[inspect].terminator = MIRTerminator::make_Switch({MIRLValue::newLocal(pollLocal), ::makeVec2(ready, pending)});
    output.blocks[pending].statements.push_back(MIRStatement::make_Assign({MIRLValue::newReturn(), pollResult(1)}));
    output.blocks[pending].terminator = MIRTerminator::make_Return({});
    output.blocks[ready].terminator = MIRTerminator::make_Drop({
        MIRDropKind::DEEP,
        MIRLValue::newDeref(MIRLValue::newLocal(storagePtrLocal)),
        ~0u,
        next,
        MIRUnwindAction::make_Continue({}),
    });
    return getStorage;
}

auto AsyncDropPollBuilder::buildDeepDrop(MIRLValue value, MIRBasicBlockId next) -> MIRBasicBlockId {
    const auto entry = newBlock();
    output.blocks[entry].terminator = MIRTerminator::make_Drop({
        MIRDropKind::DEEP,
        std::move(value),
        ~0u,
        next,
        MIRUnwindAction::make_Continue({}),
    });
    return entry;
}

auto AsyncDropPollBuilder::buildField(const HIRTypeData* ty, MIRLValue value, MIRBasicBlockId next) -> MIRBasicBlockId {
    if (resolve.typeNeedsAsyncDrop(sp, ty)) {
        return buildType(ty, std::move(value), next);
    }
    return resolve.typeNeedsDropGlue(sp, ty) ? buildDeepDrop(std::move(value), next) : next;
}

auto AsyncDropPollBuilder::buildStructFields(const HIRTypeData* ty, MIRLValue value, MIRBasicBlockId next) -> MIRBasicBlockId {
    if (const auto* tuple = ty->opt_Tuple()) {
        for (size_t i = tuple->size(); i > 0; i--) {
            next = buildField(tuple->at(i - 1), MIRLValue::newField(value.clone(), static_cast<unsigned>(i - 1)), next);
        }
        return next;
    }

    const auto* pathTy = ty->opt_Path();
    if (!pathTy || !pathTy->binding.is_Struct() || !pathTy->path.data.is_Generic()) {
        return next;
    }
    const auto& generic = pathTy->path.data.as_Generic();
    if (generic.path == resolve.hirCrate().getLangItemPathOpt("manually_drop")) {
        return next;
    }

    const auto& str = *pathTy->binding.as_Struct();
    auto monomorph = MonomorphStatePtr(resolve.hirCrate().types, ty, &generic.params, nullptr);
    switch (str.data.tag()) {
        case HIRStructData::TAG_Unit:
            break;
        case HIRStructData::TAG_Tuple: {
            const auto& fields = str.data.as_Tuple();
            for (size_t i = fields.size(); i > 0; i--) {
                auto fieldTy = resolve.monomorphExpand(sp, fields[i - 1].ent, monomorph);
                next = buildField(fieldTy, MIRLValue::newField(value.clone(), static_cast<unsigned>(i - 1)), next);
            }
            break;
        }
        case HIRStructData::TAG_Named: {
            const auto& fields = str.data.as_Named();
            for (size_t i = fields.size(); i > 0; i--) {
                auto fieldTy = resolve.monomorphExpand(sp, fields[i - 1].ty, monomorph);
                next = buildField(fieldTy, MIRLValue::newField(value.clone(), static_cast<unsigned>(i - 1)), next);
            }
            break;
        }
    }
    return next;
}

auto AsyncDropPollBuilder::buildFields(const HIRTypeData* ty, MIRLValue value, MIRBasicBlockId next) -> MIRBasicBlockId {
    if (const auto* array = ty->opt_Array()) {
        ASSERT_BUG(sp, array->size.is_Known(), "async drop of an array with unevaluated length: " << ty);
        for (size_t i = array->size.as_Known(); i > 0; i--) {
            next = buildField(array->inner, MIRLValue::newField(value.clone(), static_cast<unsigned>(i - 1)), next);
        }
        return next;
    }

    const auto* pathTy = ty->opt_Path();
    if (pathTy && pathTy->binding.is_Enum() && pathTy->path.data.is_Generic()) {
        const auto* variants = pathTy->binding.as_Enum()->data.opt_Data();
        if (!variants) {
            return next;
        }
        auto monomorph = MonomorphStatePtr(resolve.hirCrate().types, ty, &pathTy->path.data.as_Generic().params, nullptr);
        auto targets = ::std::vector<MIRBasicBlockId>(); // escape: MIR enum-switch storage is a generated std::vector interface
        targets.reserve(variants->size());
        for (size_t i = 0; i < variants->size(); i++) {
            auto variantTy = resolve.monomorphExpand(sp, variants->at(i).type, monomorph);
            targets.push_back(buildStructFields(variantTy, MIRLValue::newDowncast(value.clone(), static_cast<unsigned>(i)), next));
        }
        const auto entry = newBlock();
        output.blocks[entry].terminator = MIRTerminator::make_Switch({std::move(value), std::move(targets)});
        return entry;
    }
    return buildStructFields(ty, std::move(value), next);
}

auto AsyncDropPollBuilder::buildCoroutineDrop(const HIRTypeData* ty, MIRLValue value, MIRBasicBlockId next) -> MIRBasicBlockId {
    auto& types = resolve.hirCrate().types;
    auto dropPath = HIRPath(ty, HIRGenericPath(resolve.langDrop()), RcString::newInterned("drop"), HIRPathParams{});
    MonomorphState monomorph(types);
    auto item = resolve.getValue(sp, dropPath, monomorph);
    const auto* functionPtr = item.opt_Function();
    ASSERT_BUG(sp, functionPtr && (*functionPtr)->code.mir, "coroutine Drop MIR is unavailable for " << ty);
    const auto* function = *functionPtr;
    const auto& source = *function->code.mir;

    const auto localBase = static_cast<unsigned>(output.locals.size());
    for (const auto* localTy : source.locals) {
        output.locals.push_back(resolve.monomorphExpand(sp, localTy, monomorph));
    }
    const auto dropFlagBase = static_cast<unsigned>(output.dropFlags.size());
    output.dropFlags.insert(output.dropFlags.end(), source.dropFlags.begin(), source.dropFlags.end());
    const auto returnLocal = newLocal(types.unit());
    const auto bbBase = static_cast<unsigned>(output.blocks.size());
    for (size_t i = 0; i < source.blocks.size(); i++) {
        newBlock();
    }

    CoroutineDropCloner cloner(*this, monomorph, std::move(value), bbBase, localBase, dropFlagBase, returnLocal);
    auto pathCallback = makeCallable<MIRPathCb>([&](auto& os) {
        os << dropPath;
    });
    MIRTypeResolve sourceTypes(sp, resolve, pathCallback, function->returnType, function->args, source);
    for (size_t i = 0; i < source.blocks.size(); i++) {
        const auto& sourceBlock = source.blocks[i];
        const auto targetIdx = static_cast<MIRBasicBlockId>(bbBase + i);
        output.blocks[targetIdx].isCleanup = sourceBlock.isCleanup;
        output.blocks[targetIdx].statements.reserve(sourceBlock.statements.size());
        for (const auto& statement : sourceBlock.statements) {
            output.blocks[targetIdx].statements.push_back(cloner.cloneStmt(statement));
        }

        if (sourceBlock.terminator.is_Return()) {
            output.blocks[targetIdx].terminator = MIRTerminator::make_Goto(next);
            continue;
        }
        const auto* drop = sourceBlock.terminator.opt_Drop();
        if (!drop) {
            output.blocks[targetIdx].terminator = cloner.cloneTerm(sourceBlock.terminator);
            continue;
        }

        sourceTypes.setCurStmtTerm(static_cast<unsigned>(i));
        HIRTypeRef slotTyTmp;
        auto slotTy = cloner.monomorph(sourceTypes.getLvalueType(slotTyTmp, drop->slot));
        if (!resolve.typeNeedsAsyncDrop(sp, slotTy)) {
            output.blocks[targetIdx].terminator = cloner.cloneTerm(sourceBlock.terminator);
            continue;
        }

        const auto normalTarget = cloner.mapBbIdx(drop->target);
        const auto asyncTarget = buildType(slotTy, cloner.cloneLval(drop->slot), normalTarget);
        if (drop->flagIdx == ~0u) {
            output.blocks[targetIdx].terminator = MIRTerminator::make_Goto(asyncTarget);
            continue;
        }

        HIRPathParams params(types.unit());
        const auto& pollPath = resolve.hirCrate().getLangItemPath(sp, "Poll");
        auto conditionTy = types.path(HIRGenericPath(pollPath, std::move(params)), &resolve.hirCrate().getEnumByPath(sp, pollPath));
        const auto conditionLocal = newLocal(conditionTy);
        output.blocks[targetIdx].statements.push_back(
            MIRStatement::make_Assign({
                MIRLValue::newLocal(conditionLocal),
                MIRRValue::make_EnumVariant({conditionTy->as_Path().path.data.as_Generic().clone(), 1, {}}),
            })
        );
        output.blocks[targetIdx].terminator = MIRTerminator::make_Switch({
            MIRLValue::newLocal(conditionLocal),
            ::makeVec2(asyncTarget, asyncTarget),
            cloner.mapDropFlag(drop->flagIdx),
            normalTarget,
        });
    }
    return bbBase;
}

auto AsyncDropPollBuilder::buildType(const HIRTypeData* ty, MIRLValue value, MIRBasicBlockId next) -> MIRBasicBlockId {
    if (const auto* pathTy = ty->opt_Path(); pathTy && (pathTy->isFuture() || pathTy->isGenerator())) {
        return buildCoroutineDrop(ty, std::move(value), next);
    }
    const auto fields = buildFields(ty, value.clone(), next);
    HIRPath dropPath{HIRSimplePath()};
    HIRTypeRef futureTy;
    if (resolve.findAsyncDrop(sp, ty, dropPath, futureTy)) {
        return buildAsyncDestructor(ty, std::move(value), std::move(dropPath), std::move(futureTy), fields);
    }
    if (hasDropImpl(ty)) {
        return buildSyncDestructor(ty, std::move(value), fields);
    }
    return fields;
}

AsyncDropPollBuilder::AsyncDropPollBuilder(const Span& sp, const StaticTraitResolve& resolve, const HIRTypeData* dropeeTy, const HIRTypeData* outerTy)
    : sp(sp)
    , resolve(resolve)
    , dropeeTy(dropeeTy)
    , outerTy(outerTy)
    , statePtrLocal(newLocal(resolve.hirCrate().types.pointer(HIRBorrowType::Unique, resolve.hirCrate().types.primitive(HIRCoreType::U8))))
{
}

auto AsyncDropPollBuilder::build() -> MIRFunctionPointer {
    const auto acquireState = newBlock();
    const auto dispatch = newBlock();
    const auto finish = newBlock();
    const auto returned = newBlock();
    const auto poisoned = newBlock();
    const auto invalid = newBlock();

    output.blocks[finish].statements.push_back(setState(1));
    output.blocks[finish].statements.push_back(MIRStatement::make_Assign({MIRLValue::newReturn(), pollResult(0)}));
    output.blocks[finish].terminator = MIRTerminator::make_Return({});
    output.blocks[returned].statements.push_back(MIRStatement::make_Assign({MIRLValue::newReturn(), pollResult(0)}));
    output.blocks[returned].terminator = MIRTerminator::make_Return({});
    output.blocks[poisoned].terminator = MIRTerminator::make_Unreachable({});
    output.blocks[invalid].terminator = MIRTerminator::make_Unreachable({});

    auto dropee = MIRLValue::newDeref(MIRLValue::newField(outerValue(), 1));
    const auto start = buildType(dropeeTy, std::move(dropee), finish);

    HIRPathParams stateParams(outerTy);
    output.blocks[acquireState].terminator = MIRTerminator::make_Call({
        dispatch,
        MIRUnwindAction::make_Continue({}),
        MIRLValue::newLocal(statePtrLocal),
        MIRCallTarget::make_Intrinsic({"async_drop_state", std::move(stateParams)}),
        ::makeVec1<MIRParam>(MIRParam::make_Borrow({HIRBorrowType::Unique, outerValue()})),
    });

    auto values = ::makeVec3<u64>(0, 1, 2);
    auto targets = ::makeVec3<MIRBasicBlockId>(start, returned, poisoned);
    for (const auto& target : resumeTargets) {
        values.push_back(target.first);
        targets.push_back(target.second);
    }
    output.blocks[dispatch].terminator = MIRTerminator::make_SwitchValue({
        stateValue(),
        invalid,
        std::move(targets),
        MIRSwitchValues(std::move(values)),
    });
    return MIRFunctionPointer(box$(std::move(output)).release());
}

AsyncDropPollBuilder::CoroutineDropCloner::CoroutineDropCloner(const AsyncDropPollBuilder& owner, const MonomorphState& monomorph, MIRLValue dropee, unsigned bbBase, unsigned localBase, unsigned dropFlagBase, unsigned returnLocal)
    : MIRCloner(owner.sp, owner.resolve.hirCrate().types)
    , owner(owner)
    , params(monomorph)
    , dropee(std::move(dropee))
    , bbBase(bbBase)
    , localBase(localBase)
    , dropFlagBase(dropFlagBase)
    , returnLocal(returnLocal)
{
}

auto AsyncDropPollBuilder::CoroutineDropCloner::mapBbIdx(MIRBasicBlockId idx) const -> MIRBasicBlockId {
    return bbBase + idx;
}

auto AsyncDropPollBuilder::CoroutineDropCloner::mapLocal(unsigned idx) const -> unsigned {
    return localBase + idx;
}

auto AsyncDropPollBuilder::CoroutineDropCloner::mapDropFlag(unsigned idx) const -> unsigned {
    return dropFlagBase + idx;
}

auto AsyncDropPollBuilder::CoroutineDropCloner::monomorphiser() const -> const Monomorphiser& {
    return params;
}

auto AsyncDropPollBuilder::CoroutineDropCloner::resolve() const -> const StaticTraitResolve* {
    return &owner.resolve;
}

auto AsyncDropPollBuilder::CoroutineDropCloner::cloneStmt(const MIRStatement& src) const -> MIRStatement {
    if (const auto* save = src.opt_SaveDropFlag()) {
        return MIRStatement::make_SaveDropFlag({cloneLval(save->slot), save->bitIndex, mapDropFlag(save->idx)});
    }
    if (const auto* load = src.opt_LoadDropFlag()) {
        return MIRStatement::make_LoadDropFlag({mapDropFlag(load->idx), cloneLval(load->slot), load->bitIndex});
    }
    return MIRCloner::cloneStmt(src);
}

auto AsyncDropPollBuilder::CoroutineDropCloner::cloneLval(const MIRLValue& src) const -> MIRLValue {
    if (src.root.is_Argument()) {
        ASSERT_BUG(sp, src.root.as_Argument() == 0 && !src.wrappers.empty() && src.wrappers.front().is_Deref(), "unexpected coroutine Drop argument lvalue " << src);
        auto rv = dropee.clone();
        for (size_t i = 1; i < src.wrappers.size(); i++) {
            auto wrapper = src.wrappers[i];
            if (wrapper.is_Index()) {
                wrapper = MIRLValue::Wrapper::newIndex(mapLocal(wrapper.as_Index()));
            }
            rv.wrappers.push_back(std::move(wrapper));
        }
        return rv;
    }
    if (src.root.is_Return()) {
        auto rv = src.clone();
        rv.root = MIRLValue::Storage::newLocal(returnLocal);
        for (auto& wrapper : rv.wrappers) {
            if (wrapper.is_Index()) {
                wrapper = MIRLValue::Wrapper::newIndex(mapLocal(wrapper.as_Index()));
            }
        }
        return rv;
    }
    return MIRCloner::cloneLval(src);
}

Cloner::Cloner(const Span& sp, const ::StaticTraitResolve& resolve, const TransParams& params)
    : MIRCloner(sp, resolve.hirCrate().types)
    , resolve_(resolve)
    , params(params)
{
}

auto Cloner::valueGenericType(HIRGenericRef g) const -> const HIRTypeData* {
    switch (g.group()) {
        case 0:
            ASSERT_BUG(sp, g.idx() < resolve_.implGenerics().values.size(), "Value generic " << g << " out of bounds in impl: " << resolve_.implGenerics().values.size());
            return resolve_.implGenerics().values.at(g.idx()).type;
        case 1:
            ASSERT_BUG(sp, g.idx() < resolve_.itemGenerics().values.size(), "Value generic " << g << " out of bounds in fcn: " << resolve_.itemGenerics().values.size());
            return resolve_.itemGenerics().values.at(g.idx()).type;
        default:
            BUG(Span(), "");
    }
}

auto Cloner::monomorphiser() const -> const Monomorphiser& {
    return params;
}

auto Cloner::resolve() const -> const StaticTraitResolve* {
    return &resolve_;
}
