#include "trans_monomorphise.h"
#include "hir_typeck_static.h"
#include "mir_mir.h"
#include "hir_hir.h"
#include "mir_operations.h" // Needed for post-monomorph checks and optimisations
#include "hir_conv_constant_evaluation.h"

namespace {
    class Cloner: public ::MIR::Cloner {
        const ::StaticTraitResolve& mResolve;
        const TransParams& params;

    public:
        Cloner(const Span& sp, const ::StaticTraitResolve& resolve, const TransParams& params)
            : ::MIR::Cloner(sp, resolve.crate.types)
            , mResolve(resolve)
            , params(params)
        {
        }

        const HIR::TypeData* value_generic_type(HIR::GenericRef g) const override {
            switch (g.group()) {
                case 0:
                    ASSERT_BUG(sp, g.idx() < mResolve.impl_generics().values.size(), "Value generic " << g << " out of bounds in impl: " << mResolve.impl_generics().values.size());
                    return mResolve.impl_generics().values.at(g.idx()).mType;
                case 1:
                    ASSERT_BUG(sp, g.idx() < mResolve.item_generics().values.size(), "Value generic " << g << " out of bounds in fcn: " << mResolve.item_generics().values.size());
                    return mResolve.item_generics().values.at(g.idx()).mType;
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

::MIR::FunctionPointer TransMonomorphise(const ::StaticTraitResolve& resolve, const TransParams& params, const ::MIR::FunctionPointer& tpl) {
    static Span sp;
    TRACE_FUNCTION;
    assert(tpl);

    ::MIR::Function output;

    // 1. Monomorphise locals and temporaries
    output.locals.reserve(tpl->locals.size());
    for (const auto& var : tpl->locals) {
        DEBUG("- _" << output.locals.size() << " (" << var << ")");
        output.locals.push_back(params.monomorph(resolve, var));
        DEBUG(" = " << output.locals.back());
    }
    output.drop_flags = tpl->drop_flags;

    Cloner c{sp, resolve, params};
    // 2. Monomorphise all paths
    output.blocks.reserve(tpl->blocks.size());
    for (const auto& block : tpl->blocks) {
        ::std::vector<::MIR::Statement> statements;

        TRACE_FUNCTION_F("bb" << output.blocks.size());
        statements.reserve(block.statements.size());
        for (const auto& stmt : block.statements) {
            switch (stmt.tag()) {
                // LAZY: These _should_ be in `clone_stmt`, but they're not needed in optimising and MIR cloning
                TU_ARM(stmt, SaveDropFlag, e) {
                    statements.push_back(::MIR::Statement::make_SaveDropFlag({e.slot.clone(), e.bit_index, e.idx}));
                }
                break;
                TU_ARM(stmt, LoadDropFlag, e) {
                    statements.push_back(::MIR::Statement::make_LoadDropFlag({e.idx, e.slot.clone(), e.bit_index}));
                }
                break;
                default:
                    statements.push_back(c.clone_stmt(stmt));
                    break;
            }
        }

        ::MIR::Terminator terminator = c.clone_term(block.terminator);
        output.blocks.push_back(::MIR::BasicBlock{mv$(statements), mv$(terminator)});
    }

    return ::MIR::FunctionPointer(box$(output).release());
}

/// Monomorphise all functions in a TransList
void TransMonomorphiseList(const ::HIR::Crate& crate, TransList& list, unsigned mir_opt_level) {
    ::StaticTraitResolve resolve{crate};

    struct Nvs: public ::HIR::Evaluator::Newval {
        TransList& out;
        const HIR::Crate& crate;
        unsigned count;
        ::std::vector<std::pair<HIR::SimplePath, HIR::Static*>> added;

        Nvs(TransList& out, const HIR::Crate& crate)
            : out(out)
            , crate(crate)
            , count(0)
        {
        }

        ::HIR::Path new_static(::HIR::TypeRef type, EncodedLiteral value) override {
            // Ensure that the type is in enumeration (it should have been, but maybe not?)
            out.add_type(type, false);
            auto name = RcString::new_interned(FMT("ConstEvalMonomorph#" << count));
            count++;
            auto p = ::HIR::SimplePath(crate.crateName, {name});
            auto ent = std::make_unique<HIR::VisEnt<HIR::ValueItem>>(HIR::VisEnt<HIR::ValueItem>{HIR::Publicity::new_global(), HIR::ValueItem(::HIR::Static(HIR::Linkage(), false, std::move(type), HIR::ExprPtr()))});

            {
                auto& s = ent->ent.as_Static();
                s.valueGenerated = true;
                s.valueRes = std::move(value);
                s.saveLiteral = false;
                added.push_back(std::make_pair(p, &s));
            }
            const_cast<HIR::Module&>(crate.rootModule).valueItems.insert(std::make_pair(name, std::move(ent)));
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
        auto eval = ::HIR::Evaluator{pp.sp, crate, nvs};
        eval.resolve.set_both_generics_raw(pp.gdef_impl, &c.mParams);
        MonomorphState ms(crate.types);
        ms.self_ty = pp.self_type;
        ms.pp_impl = &pp.pp_impl;
        ms.pp_method = &pp.pp_method;
        DEBUG("ms = " << ms);
        try {
            auto new_lit = eval.evaluate_constant(path, c.mValue, ::std::move(ty), ::std::move(ms));
            // 2. Store evaluated HIR::Literal in c.m_monomorph_cache
            c.monomorphCache.insert(::std::make_pair(path.clone(), ::std::move(new_lit)));
        } catch (...) {
            // Deferred - no update
            BUG(Span(), "Exception thrown during evaluation of: " << path);
        }
    }

    for (auto& ent : list.statics) {
        const auto& path = ent.first;
        const auto& pp = ent.second->pp;
        const auto& s = *ent.second->ptr;

        if (!s.mParams.is_generic()) {
            continue;
        }

        TRACE_FUNCTION_FR("STATIC " << path, "STATIC " << path);
        auto ty = pp.monomorph(resolve, s.mType);
        // 1. Evaluate the constant
        auto eval = ::HIR::Evaluator{pp.sp, crate, nvs};
        eval.resolve.set_both_generics_raw(pp.gdef_impl, &s.mParams);
        MonomorphState ms(crate.types);
        ms.self_ty = pp.self_type;
        ms.pp_impl = &pp.pp_impl;
        ms.pp_method = &pp.pp_method;
        DEBUG("ms = " << ms);
        try {
            auto new_lit = eval.evaluate_constant(path, s.mValue, ::std::move(ty), ::std::move(ms));
            // 2. Store evaluated HIR::Literal in s.m_monomorph_cache
            s.monomorphCache.insert(::std::make_pair(path.clone(), ::std::move(new_lit)));
        } catch (...) {
            // Deferred - no update
            BUG(Span(), "Exception thrown during evaluation of: " << path);
        }
    }

    for (auto& fcn_ent : list.functions) {
        const auto& fcn = *fcn_ent.second->ptr;
        // Trait methods (which are the only case where `Self` can exist in the argument list at this stage) always need to be monomorphised.
        bool is_method = (fcn.mArgs.size() > 0 && visit_ty_with(fcn.mArgs[0].second, [&](const auto& x) {
            return x == crate.types.self();
        }));
        bool monomorph_needed = fcn_ent.second->pp.has_types() || is_method;

        if (monomorph_needed) {
            const auto& path = fcn_ent.first;
            const auto& pp = fcn_ent.second->pp;
            TRACE_FUNCTION_FR("FUNCTION " << path, "FUNCTION " << path);
            ASSERT_BUG(Span(), fcn.mCode.mir, "No code for " << path);

            // TODO: Get the item params too
            if (fcn_ent.second->pp.pp_impl.has_params()) {
                assert(pp.gdef_impl);
            }
            resolve.set_both_generics_raw(pp.gdef_impl, &fcn.mParams);

            auto mir = TransMonomorphise(resolve, fcn_ent.second->pp, fcn.mCode.mir);

            // TODO: Should these be moved to their own pass? Potentially not, the extra pass should just be an inlining optimise pass
            auto ret_type = pp.monomorph(resolve, fcn.returnType);
            ::HIR::Function::args_t args;
            for (const auto& a : fcn.mArgs) {
                args.push_back(::std::make_pair(::HIR::Pattern{}, pp.monomorph(resolve, a.second)));
            }

            //::std::string s = FMT(path);
            ::HIR::ItemPath ip(path);
            MIRValidate(resolve, ip, *mir, args, ret_type);
            MIRCleanup(resolve, ip, *mir, args, ret_type);
            if (mir_opt_level == 0) {
                MIROptimiseMin(resolve, ip, *mir, args, ret_type);
            } else {
                MIROptimise(resolve, ip, *mir, args, ret_type, mir_opt_level, /*do_inline*/ false);
            }
            MIRValidate(resolve, ip, *mir, args, ret_type);

            fcn_ent.second->monomorphised.ret_ty = ::std::move(ret_type);
            fcn_ent.second->monomorphised.arg_tys = ::std::move(args);
            fcn_ent.second->monomorphised.code = ::std::move(mir);
            resolve.clear_both_generics();
        } else {
            DEBUG("Non-generic: FUNCTION " << fcn_ent.first);
        }
    }

    for (auto& v : nvs.added) {
        auto* o = list.add_static(crate.types, HIR::Path(v.first));
        ASSERT_BUG(Span(), o, "Generated static " << v.first << " already in TransList?");
        o->ptr = v.second;
    }
}
