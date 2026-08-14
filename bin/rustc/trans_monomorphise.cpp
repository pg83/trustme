#include "trans_monomorphise.h"

#include "hir_hir.h"
#include "mir_mir.h"
#include "wire_board.h"
#include "mir_operations.h" // Needed for post-monomorph checks and optimisations
#include "hir_typeck_static.h"
#include "hir_conv_constant_evaluation.h"

namespace {
    class Cloner: public MIRCloner {
        const ::StaticTraitResolve& mResolve;
        const TransParams& params;

    public:
        Cloner(const Span& sp, const ::StaticTraitResolve& resolve, const TransParams& params)
            : MIRCloner(sp, resolve.hirCrate().types)
            , mResolve(resolve)
            , params(params)
        {
        }

        const HIRTypeData* valueGenericType(HIRGenericRef g) const override {
            switch (g.group()) {
                case 0:
                    ASSERT_BUG(sp, g.idx() < mResolve.implGenerics().values.size(), "Value generic " << g << " out of bounds in impl: " << mResolve.implGenerics().values.size());
                    return mResolve.implGenerics().values.at(g.idx()).mType;
                case 1:
                    ASSERT_BUG(sp, g.idx() < mResolve.itemGenerics().values.size(), "Value generic " << g << " out of bounds in fcn: " << mResolve.itemGenerics().values.size());
                    return mResolve.itemGenerics().values.at(g.idx()).mType;
                default:
                    BUG(Span(), "");
            }
        }

        const Monomorphiser& monomorphiser() const override {
            return params;
        }

        const StaticTraitResolve* resolve() const override {
            return &mResolve;
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

/// Monomorphise all functions in a TransList
void TransMonomorphiseList(const WireBoard& wb, const HIRCrate& crate, TransList& list, unsigned mirOptLevel) {
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

        HIRPath newStatic(HIRTypeRef type, EncodedLiteral value) override {
            // Ensure that the type is in enumeration (it should have been, but maybe not?)
            out.addType(type, false);
            auto name = RcString::newInterned(FMT("ConstEvalMonomorph#" << count));
            count++;
            auto p = HIRSimplePath(crate.crateName, {name});
            auto* ent = crate.pool->make<HIRVisEnt<HIRValueItem>>(HIRVisEnt<HIRValueItem>{HIRPublicity::newGlobal(), HIRValueItem(HIRStatic(HIRLinkage(), false, std::move(type), HIRExprPtr()))});

            {
                auto& s = ent->ent.as_Static();
                s.valueGenerated = true;
                s.valueRes = std::move(value);
                s.saveLiteral = false;
                added.push_back(std::make_pair(p, &s));
            }
            const_cast<HIRModule&>(crate.mRootModule).valueItems.insert(std::make_pair(name, std::move(ent)));
            return p;
        }
    } nvs{list, crate};

    // Also do constants and statics (stored in where?)
    // - NOTE: Done in reverse order, because consteval needs used constants to be evaluated
    for (auto& ent : reverse(list.constants)) {
        const auto& path = ent.first;
        const auto& pp = ent.second->pp;
        const auto& c = *ent.second->ptr;
        TRACE_FUNCTION_FR("CONSTANT " << path, "CONSTANT " << path);
        auto ty = pp.monomorph(resolve, c.mType);
        // 1. Evaluate the constant
        auto eval = HIREvaluator{pp.sp, wb, nvs};
        eval.resolve.setBothGenericsRaw(pp.gdefImpl, &c.mParams);
        MonomorphState ms(crate.types);
        ms.selfTy = pp.selfType;
        ms.ppImpl = &pp.ppImpl;
        ms.ppMethod = &pp.ppMethod;
        DEBUG("ms = " << ms);
        try {
            auto newLit = eval.evaluateConstant(path, c.mValue, ::std::move(ty), ::std::move(ms));
            // 2. Store evaluated HIR::Literal in c.m_monomorph_cache
            c.monomorphCache.insert(::std::make_pair(path.clone(), ::std::move(newLit)));
        } catch (...) {
            // Deferred - no update
            BUG(Span(), "Exception thrown during evaluation of: " << path);
        }
    }

    for (auto& ent : list.statics) {
        const auto& path = ent.first;
        const auto& pp = ent.second->pp;
        const auto& s = *ent.second->ptr;

        if (!s.mParams.isGeneric()) {
            continue;
        }

        TRACE_FUNCTION_FR("STATIC " << path, "STATIC " << path);
        auto ty = pp.monomorph(resolve, s.mType);
        // 1. Evaluate the constant
        auto eval = HIREvaluator{pp.sp, wb, nvs};
        eval.resolve.setBothGenericsRaw(pp.gdefImpl, &s.mParams);
        MonomorphState ms(crate.types);
        ms.selfTy = pp.selfType;
        ms.ppImpl = &pp.ppImpl;
        ms.ppMethod = &pp.ppMethod;
        DEBUG("ms = " << ms);
        try {
            auto newLit = eval.evaluateConstant(path, s.mValue, ::std::move(ty), ::std::move(ms));
            // 2. Store evaluated HIR::Literal in s.m_monomorph_cache
            s.monomorphCache.insert(::std::make_pair(path.clone(), ::std::move(newLit)));
        } catch (...) {
            // Deferred - no update
            BUG(Span(), "Exception thrown during evaluation of: " << path);
        }
    }

    for (auto& fcnEnt : list.functions) {
        const auto& fcn = *fcnEnt.second->ptr;
        // Trait methods (which are the only case where `Self` can exist in the argument list at this stage) always need to be monomorphised.
        bool isMethod = (fcn.mArgs.size() > 0 && visitTyWith(fcn.mArgs[0].second, [&](const auto& x) {
            return x == crate.types.self();
        }));
        bool monomorphNeeded = fcnEnt.second->pp.hasTypes() || isMethod;

        if (monomorphNeeded) {
            const auto& path = fcnEnt.first;
            const auto& pp = fcnEnt.second->pp;
            TRACE_FUNCTION_FR("FUNCTION " << path, "FUNCTION " << path);
            ASSERT_BUG(Span(), fcn.mCode.mir, "No code for " << path);

            // TODO: Get the item params too
            if (fcnEnt.second->pp.ppImpl.hasParams()) {
                assert(pp.gdefImpl);
            }
            resolve.setBothGenericsRaw(pp.gdefImpl, &fcn.mParams);

            auto mir = TransMonomorphise(resolve, fcnEnt.second->pp, fcn.mCode.mir);

            // TODO: Should these be moved to their own pass? Potentially not, the extra pass should just be an inlining optimise pass
            auto retType = pp.monomorph(resolve, fcn.returnType);
            HIRFunction::argsT args;
            for (const auto& a : fcn.mArgs) {
                args.push_back(::std::make_pair(HIRPattern{}, pp.monomorph(resolve, a.second)));
            }

            //::std::string s = FMT(path);
            HIRItemPath ip(path);
            MIRCleanup(resolve, ip, *mir, args, retType);
            if (mirOptLevel == 0) {
                MIROptimiseMin(resolve, ip, *mir, args, retType);
            } else {
                MIROptimise(resolve, ip, *mir, args, retType, mirOptLevel, /*do_inline*/ false);
            }

            fcnEnt.second->monomorphised.retTy = ::std::move(retType);
            fcnEnt.second->monomorphised.argTys = ::std::move(args);
            fcnEnt.second->monomorphised.code = ::std::move(mir);
            resolve.clearBothGenerics();
        } else {
            DEBUG("Non-generic: FUNCTION " << fcnEnt.first);
        }
    }

    for (auto& v : nvs.added) {
        auto* o = list.addStatic(crate.types, HIRPath(v.first));
        ASSERT_BUG(Span(), o, "Generated static " << v.first << " already in TransList?");
        o->ptr = v.second;
    }
}
