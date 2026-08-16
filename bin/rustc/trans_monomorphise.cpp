#include "trans_monomorphise.h"

#include "hir_hir.h"
#include "mir_mir.h"
#include "wire_board.h"
#include "mir_operations.h" // Needed for post-monomorph checks and optimisations
#include "hir_typeck_static.h"
#include "hir_conv_constant_evaluation.h"
#include "trans_main_bindings.h"

namespace {
    class Cloner: public MIRCloner {
        const ::StaticTraitResolve& resolve_;
        const TransParams& params;

    public:
        Cloner(const Span& sp, const ::StaticTraitResolve& resolve, const TransParams& params)
            : MIRCloner(sp, resolve.hirCrate().types)
            , resolve_(resolve)
            , params(params)
        {
        }

        const HIRTypeData* valueGenericType(HIRGenericRef g) const override {
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

        const Monomorphiser& monomorphiser() const override {
            return params;
        }

        const StaticTraitResolve* resolve() const override {
            return &resolve_;
        }
    };
}

MIRFunctionPointer TransMonomorphise(const ::StaticTraitResolve& resolve, const TransParams& params, const MIRFunctionPointer& tpl) {
    static Span sp;
    TRACE_FUNCTION;
    assert(tpl);

    MIRFunction output;

    // 1. Monomorphise locals and temporaries
    output.locals.reserve(tpl->locals.size());
    for (const auto& var : tpl->locals) {
        DEBUG("- _" << output.locals.size() << " (" << var << ")");
        output.locals.push_back(params.monomorph(resolve, var));
        DEBUG(" = " << output.locals.back());
    }
    output.dropFlags = tpl->dropFlags;

    Cloner c{sp, resolve, params};
    // 2. Monomorphise all paths
    output.blocks.reserve(tpl->blocks.size());
    for (const auto& block : tpl->blocks) {
        ::std::vector<MIRStatement> statements;

        TRACE_FUNCTION_F("bb" << output.blocks.size());
        statements.reserve(block.statements.size());
        for (const auto& stmt : block.statements) {
            switch (stmt.tag()) {
                // LAZY: These _should_ be in `clone_stmt`, but they're not needed in optimising and MIR cloning
                TU_ARM(stmt, SaveDropFlag, e) {
                    statements.push_back(MIRStatement::make_SaveDropFlag({e.slot.clone(), e.bitIndex, e.idx}));
                }
                break;
                TU_ARM(stmt, LoadDropFlag, e) {
                    statements.push_back(MIRStatement::make_LoadDropFlag({e.idx, e.slot.clone(), e.bitIndex}));
                }
                break;
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
    ::StaticTraitResolve resolve{wb};

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
            auto* ent = crate.pool->make<HIRVisEnt<HIRValueItem>>(HIRVisEnt<HIRValueItem>{HIRPublicity::newGlobal(), HIRValueItem(HIRStatic(HIRLinkage(), false, std::move(type), HIRExprPtr()))});

            {
                auto& s = ent->ent.as_Static();
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
            TRACE_FUNCTION_FR("CONSTANT " << path, "CONSTANT " << path);
            auto ty = pp.monomorph(resolve, c.type);
            auto eval = HIREvaluator{pp.sp, wb, nvs};
            eval.resolve.setBothGenericsRaw(pp.gdefImpl, &c.params);
            MonomorphState ms(crate.types);
            ms.selfTy = pp.selfType;
            ms.ppImpl = &pp.ppImpl;
            ms.ppMethod = &pp.ppMethod;
            try {
                auto newLit = eval.evaluateConstant(path, c.value, ::std::move(ty), ::std::move(ms));
                c.monomorphCache.insert(::std::make_pair(path.clone(), ::std::move(newLit)));
            } catch (...) {
                BUG(Span(), "Exception thrown during evaluation of: " << path);
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
            TRACE_FUNCTION_FR("STATIC " << path, "STATIC " << path);
            auto ty = pp.monomorph(resolve, s.type);
            auto eval = HIREvaluator{pp.sp, wb, nvs};
            eval.resolve.setBothGenericsRaw(pp.gdefImpl, &s.params);
            MonomorphState ms(crate.types);
            ms.selfTy = pp.selfType;
            ms.ppImpl = &pp.ppImpl;
            ms.ppMethod = &pp.ppMethod;
            try {
                auto newLit = eval.evaluateConstant(path, s.value, ::std::move(ty), ::std::move(ms));
                s.monomorphCache.insert(::std::make_pair(path.clone(), ::std::move(newLit)));
            } catch (...) {
                BUG(Span(), "Exception thrown during evaluation of: " << path);
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

        if (!generated.empty()) {
            changed = true;
            TransEnumerateGeneratedStatics(wb, list, generated);
            TransAutoImpls(wb, crate, list);
        }
    } while (changed);

    // MIR cleanup can make a previously generic coercion concrete and insert
    // translation paths such as `<T as Trait>::vtable#`.  Those paths do not
    // exist in the pre-monomorphisation MIR, so collect and prepare them until
    // the translation graph reaches a fixed point.
    ::std::set<const TransListFunction*> initialFunctions;
    for (const auto& ent : list.functions) {
        initialFunctions.insert(ent.second.get());
    }
    ::std::set<const TransListFunction*> processedFunctions;
    while (processedFunctions.size() < list.functions.size()) {
        ::std::vector<const MIRFunction*> generatedMir;
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
                TRACE_FUNCTION_FR("FUNCTION " << path, "FUNCTION " << path);
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
                generatedMir.push_back(&*transFcn->monomorphised.code);
                resolve.clearBothGenerics();
            } else {
                DEBUG("Non-generic: FUNCTION " << fcnEnt.first);
                // Concrete MIR was already collected by the initial
                // enumeration.  Only automatic functions created by a late
                // TransAutoImpls pass need their raw MIR collected here.
                if (initialFunctions.count(transFcn) == 0 && fcn.code.mir) {
                    generatedMir.push_back(&*fcn.code.mir);
                }
            }
        }

        if (TransEnumerateGeneratedMIR(wb, list, generatedMir)) {
            TransAutoImpls(wb, crate, list);
        }
    }

}
