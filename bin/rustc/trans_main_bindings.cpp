#include "trans_main_bindings.h"

#include "trans_main_bindings.h"
#include "trans_trans_list.h"
#include "hir_hir.h"
#include "mir_mir.h"
#include "hir_typeck_common.h" // monomorph
#include "hir_typeck_static.h" // StaticTraitResolve
#include <deque>
#include <algorithm> // find_if
#include "trans_target.h"
#include "mir_operations.h"
#include "mir_helpers.h"
#include "trans_allocator.h"
#include "hir_conv_main_bindings.h"
#include "hir_item_path.h"
#include "hir_visitor.h"
#include "trans_mangling.h"
#include <unordered_set>

namespace {
    struct State {
        ::HIR::Crate& crate;
        StaticTraitResolve resolve;
        const TransList& trans_list;
        ::std::deque<::HIR::TypeRef> todo_list;
        ::std::set<::HIR::TypeRef> done_list;

        ::HIR::SimplePath langClone;

        State(::HIR::Crate& crate, const TransList& trans_list)
            : crate(crate)
            , resolve(crate)
            , trans_list(trans_list)
        {
            langClone = crate.get_lang_item_path_opt("clone");
        }

        void enqueue_type(const ::HIR::TypeData* ty) {
            if (this->trans_list.auto_clone_impls.count(ty) == 0 && this->done_list.count(ty) == 0) {
                this->done_list.insert(ty);
                this->todo_list.push_back(ty);
            }
        }
    };

    const RcString rcstring_clone = RcString::new_interned("clone");
    const RcString rcstring_drop = RcString::new_interned("drop");
    const RcString rcstring_self = RcString::new_interned("self");
    const RcString rcstring_drop_glue = RcString::new_interned("#drop_glue");
}

namespace {
    struct CloneCleanupState {
        ::std::vector<::MIR::BasicBlockId> calls;
        ::std::vector<::std::pair<::MIR::LValue, unsigned>> values;
    };

    ::MIR::BasicBlock& clone_open_block(::MIR::Function& mir_fcn) {
        if (mir_fcn.blocks.empty() || !mir_fcn.blocks.back().terminator.is_Incomplete()) {
            mir_fcn.blocks.push_back(::MIR::BasicBlock());
        }
        return mir_fcn.blocks.back();
    }

    ::MIR::Param clone_field(
        const State& state,
        const Span& sp,
        ::MIR::Function& mir_fcn,
        CloneCleanupState& cleanup,
        const ::HIR::TypeData* subty,
        ::MIR::LValue fld_lvalue
    ) {
        if (state.resolve.type_is_copy(sp, subty)) {
            return ::std::move(fld_lvalue);
        } else {
            const auto& langClone = state.resolve.m_crate.get_lang_item_path(sp, "clone");
            // Allocate to locals (one for the `&T`, the other for the cloned `T`)
            auto borrow_lv = ::MIR::LValue::newLocal(mir_fcn.locals.size());
            mir_fcn.locals.push_back(state.crate.m_types.borrow(::HIR::BorrowType::Shared, subty));
            auto res_lv = ::MIR::LValue::newLocal(mir_fcn.locals.size());
            mir_fcn.locals.push_back(subty);
            const auto drop_flag = static_cast<unsigned>(mir_fcn.drop_flags.size());
            mir_fcn.drop_flags.push_back(false);

            // Call `<T as Clone>::clone`, passing a borrow of the field
            auto& bb = clone_open_block(mir_fcn);
            bb.statements.push_back(::MIR::Statement::make_Assign({borrow_lv.clone(), ::MIR::RValue::make_Borrow({::HIR::BorrowType::Shared, false, mv$(fld_lvalue)})}));
            ::HIR::PathParams pp;
            pp.m_lifetimes.push_back(HIR::LifetimeRef(1 * 256 + 0)); // 'M:0
            const auto call_block = static_cast<::MIR::BasicBlockId>(mir_fcn.blocks.size() - 1);
            const auto ret_block = static_cast<::MIR::BasicBlockId>(mir_fcn.blocks.size());
            bb.terminator = ::MIR::Terminator::make_Call(
                {ret_block,
                 ::MIR::UnwindAction::make_Continue({}),
                 res_lv.clone(),
                 ::MIR::CallTarget(::HIR::Path(subty, langClone, rcstring_clone, std::move(pp))),
                 ::make_vec1<::MIR::Param>(::std::move(borrow_lv))}
            );
            cleanup.calls.push_back(call_block);
            cleanup.values.push_back(::std::make_pair(res_lv.clone(), drop_flag));

            mir_fcn.blocks.push_back(::MIR::BasicBlock());
            mir_fcn.blocks.back().statements.push_back(::MIR::Statement::make_SetDropFlag({drop_flag, true, ~0u}));

            // Save the output of the `clone` call
            return ::std::move(res_lv);
        }
    }

    void append_clone_cleanup(::MIR::Function& mir_fcn, const CloneCleanupState& cleanup) {
        if (cleanup.calls.empty()) {
            return;
        }

        const auto cleanup_start = static_cast<::MIR::BasicBlockId>(mir_fcn.blocks.size());
        const auto resume = static_cast<::MIR::BasicBlockId>(cleanup_start + cleanup.values.size());
        for (auto it = cleanup.values.rbegin(); it != cleanup.values.rend(); ++it) {
            ::MIR::BasicBlock block;
            block.is_cleanup = true;
            block.terminator = ::MIR::Terminator::make_Drop({
                ::MIR::eDropKind::DEEP,
                it->first.clone(),
                it->second,
                static_cast<::MIR::BasicBlockId>(mir_fcn.blocks.size() + 1),
                ::MIR::UnwindAction::make_Terminate({}),
            });
            mir_fcn.blocks.push_back(mv$(block));
        }
        assert(mir_fcn.blocks.size() == resume);
        ::MIR::BasicBlock resume_block;
        resume_block.is_cleanup = true;
        resume_block.terminator = ::MIR::Terminator::make_UnwindResume({});
        mir_fcn.blocks.push_back(mv$(resume_block));

        for (const auto call : cleanup.calls) {
            assert(mir_fcn.blocks.at(call).terminator.is_Call());
            mir_fcn.blocks[call].terminator.as_Call().unwind = ::MIR::UnwindAction::make_Cleanup(cleanup_start);
        }
    }
}

void TransAutoImplClone(State& state, ::HIR::TypeRef ty) {
    Span sp;
    TRACE_FUNCTION_F(ty);

    // Create MIR
    ::MIR::Function mir_fcn;
    if (state.resolve.type_is_copy(sp, ty)) {
        ::MIR::BasicBlock bb;
        bb.statements.push_back(::MIR::Statement::make_Assign({::MIR::LValue::newReturn(), ::MIR::RValue::make_Use(::MIR::LValue::newDeref(::MIR::LValue::newArgument(0)))}));
        bb.terminator = ::MIR::Terminator::make_Return({});
        mir_fcn.blocks.push_back(::std::move(bb));
    } else {
        TU_MATCH_HDRA( ((*ty)), {)
        default:
            TODO(sp, "auto Clone for " << ty << " - Unknown and not Copy");
            TU_ARMA(Path, te) {
                if (te.is_closure()) {
                    const auto& gp = te.path.m_data.as_Generic();
                    const auto& str = state.resolve.m_crate.get_struct_by_path(sp, gp.m_path);
                    auto p = TransParams::new_impl(state.crate.m_types, sp, ty, gp.m_params.clone());
                    CloneCleanupState cleanup;
                    ::std::vector<::MIR::Param> values;
                    values.reserve(str.m_data.as_Tuple().size());
                    for (const auto& fld : str.m_data.as_Tuple()) {
                        ::HIR::TypeRef tmp;
                        const auto& ty_m = monomorphise_type_needed(fld.ent) ? (tmp = p.monomorph(state.resolve, fld.ent)) : fld.ent;
                        auto fld_lvalue = ::MIR::LValue::newField(::MIR::LValue::newDeref(::MIR::LValue::newArgument(0)), static_cast<unsigned>(values.size()));
                        values.push_back(clone_field(state, sp, mir_fcn, cleanup, ty_m, mv$(fld_lvalue)));
                    }
                    // Construct the result value
                    auto& bb = clone_open_block(mir_fcn);
                    bb.statements.push_back(::MIR::Statement::make_Assign({::MIR::LValue::newReturn(), ::MIR::RValue::make_Struct({gp.clone(), mv$(values)})}));
                    bb.terminator = ::MIR::Terminator::make_Return({});
                    append_clone_cleanup(mir_fcn, cleanup);
                } else {
                    TODO(sp, "auto Clone for " << ty << " - Unknown and not Copy");
                }
            }
            TU_ARMA(Array, te) {
                ASSERT_BUG(sp, te.size.as_Known() < 256, "TODO: Is more than 256 elements sane for auto-generated non-Copy Clone impl? " << ty);
                CloneCleanupState cleanup;
                ::std::vector<::MIR::Param> values;
                values.reserve(te.size.as_Known());
                for (size_t i = 0; i < te.size.as_Known(); i++) {
                    auto fld_lvalue = ::MIR::LValue::newField(::MIR::LValue::newDeref(::MIR::LValue::newArgument(0)), static_cast<unsigned>(values.size()));
                    values.push_back(clone_field(state, sp, mir_fcn, cleanup, te.inner, mv$(fld_lvalue)));
                }
                // Construct the result
                auto& bb = clone_open_block(mir_fcn);
                bb.statements.push_back(::MIR::Statement::make_Assign({::MIR::LValue::newReturn(), ::MIR::RValue::make_Array({mv$(values)})}));
                bb.terminator = ::MIR::Terminator::make_Return({});
                append_clone_cleanup(mir_fcn, cleanup);
            }
            TU_ARMA(Tuple, te) {
                assert(te.size() > 0);

                CloneCleanupState cleanup;
                ::std::vector<::MIR::Param> values;
                values.reserve(te.size());
                // For each field of the tuple, create a clone (either using Copy if posible, or calling Clone::clone)
                for (const auto& subty : te) {
                    auto fld_lvalue = ::MIR::LValue::newField(::MIR::LValue::newDeref(::MIR::LValue::newArgument(0)), static_cast<unsigned>(values.size()));
                    values.push_back(clone_field(state, sp, mir_fcn, cleanup, subty, mv$(fld_lvalue)));
                }

                // Construct the result tuple
                auto& bb = clone_open_block(mir_fcn);
                bb.statements.push_back(::MIR::Statement::make_Assign({::MIR::LValue::newReturn(), ::MIR::RValue::make_Tuple({mv$(values)})}));
                bb.terminator = ::MIR::Terminator::make_Return({});
                append_clone_cleanup(mir_fcn, cleanup);
            }
        }
    }

    // Function
    ::HIR::Function fcn{
        ::HIR::Function::Receiver::BorrowShared,
        ::HIR::GenericParams{},
        /*m_args=*/::make_vec1(::std::make_pair(::HIR::Pattern(::HIR::PatternBinding(false, ::HIR::PatternBinding::Type::Move, rcstring_self, 0), ::HIR::Pattern::Data::make_Any({})), state.crate.m_types.borrow(::HIR::BorrowType::Shared, ty))),
        /*m_return=*/ty,
        ::HIR::ExprPtr{}
    };
    fcn.m_params.m_lifetimes.push_back(HIR::LifetimeDef()); // 'M:0 - for the `&self` argument
    fcn.m_code.m_mir = ::MIR::FunctionPointer(new ::MIR::Function(mv$(mir_fcn)));

    // Impl
    ::HIR::TraitImpl impl;
    impl.m_type = mv$(ty);
    impl.m_methods.insert(::std::make_pair(rcstring_clone, ::HIR::TraitImpl::ImplEnt<::HIR::Function>{false, ::std::move(fcn)}));

    // Add impl to the crate
    auto& list = state.crate.m_trait_impls[state.langClone].get_list_for_type_mut(impl.m_type);
    list.push_back(box$(impl));
    state.crate.m_all_trait_impls[state.langClone].get_list_for_type_mut(list.back()->m_type).push_back(list.back().get());
}

namespace {

    struct Builder {
        const State& state;
        MIR::Function& mir;
        const MIR::LValue self;

        Builder(const State& state, MIR::Function& mir)
            : state(state)
            , mir(mir)
            , self(MIR::LValue::newArgument(0))
        {
            mir.blocks.push_back(MIR::BasicBlock());
        }

        MIR::LValue add_local(HIR::TypeRef ty) {
            auto rv = mir.locals.size();
            mir.locals.push_back(mv$(ty));
            return MIR::LValue::newLocal(rv);
        }

        MIR::LValue in_temporary(HIR::TypeRef ty, MIR::RValue val) {
            auto rv = add_local(mv$(ty));
            push_stmt_assign(rv.clone(), mv$(val));
            return rv;
        }

        void ensure_open() {
            if (!mir.blocks.back().terminator.is_Incomplete()) {
                mir.blocks.push_back(MIR::BasicBlock());
            }
        }

        void push_stmt(MIR::Statement s) {
            ensure_open();
            mir.blocks.back().statements.push_back(mv$(s));
        }

        void push_stmt_assign(MIR::LValue lv, MIR::RValue rv) {
            this->push_stmt(MIR::Statement::make_Assign({mv$(lv), mv$(rv)}));
        }

        MIR::BasicBlockId push_stmt_drop(MIR::LValue lv) {
            ensure_open();
            const auto drop_block = static_cast<MIR::BasicBlockId>(mir.blocks.size() - 1);
            const auto next = static_cast<MIR::BasicBlockId>(mir.blocks.size());
            terminate_block(MIR::Terminator::make_Drop({MIR::eDropKind::DEEP, mv$(lv), ~0u, next, MIR::UnwindAction::make_Continue({})}));
            mir.blocks.push_back(MIR::BasicBlock());
            return drop_block;
        }

        void push_drop_sequence(::std::vector<MIR::LValue> values, MIR::BasicBlockId custom_drop_call = ~0u) {
            if (values.empty()) {
                return;
            }

            // Lay the cleanup suffixes out before the normal chain.  A panic
            // from field N starts at field N+1; a panic from the user's Drop
            // implementation starts at field zero.  Cleanup drops terminate
            // on a second panic.
            ensure_open();
            const auto entry = static_cast<MIR::BasicBlockId>(mir.blocks.size() - 1);
            terminate_block(MIR::Terminator::make_Goto(~0u));

            const size_t cleanup_first = custom_drop_call == ~0u ? 1 : 0;
            const auto cleanup_start = static_cast<MIR::BasicBlockId>(mir.blocks.size());
            const auto cleanup_block = [&](size_t field) {
                assert(field >= cleanup_first && field < values.size());
                return static_cast<MIR::BasicBlockId>(cleanup_start + field - cleanup_first);
            };

            if (cleanup_first < values.size()) {
                const auto resume = static_cast<MIR::BasicBlockId>(cleanup_start + values.size() - cleanup_first);
                for (size_t i = cleanup_first; i < values.size(); i++) {
                    MIR::BasicBlock block;
                    block.is_cleanup = true;
                    const auto target = i + 1 < values.size() ? cleanup_block(i + 1) : resume;
                    block.terminator = MIR::Terminator::make_Drop({
                        MIR::eDropKind::DEEP,
                        values[i].clone(),
                        ~0u,
                        target,
                        MIR::UnwindAction::make_Terminate({}),
                    });
                    mir.blocks.push_back(mv$(block));
                }
                MIR::BasicBlock resume_block;
                resume_block.is_cleanup = true;
                resume_block.terminator = MIR::Terminator::make_UnwindResume({});
                mir.blocks.push_back(mv$(resume_block));
            }

            const auto normal_start = static_cast<MIR::BasicBlockId>(mir.blocks.size());
            mir.blocks[entry].terminator.as_Goto() = normal_start;
            for (size_t i = 0; i < values.size(); i++) {
                MIR::BasicBlock block;
                const auto target = static_cast<MIR::BasicBlockId>(normal_start + i + 1);
                auto unwind = i + 1 < values.size()
                    ? MIR::UnwindAction::make_Cleanup(cleanup_block(i + 1))
                    : MIR::UnwindAction::make_Continue({});
                block.terminator = MIR::Terminator::make_Drop({
                    MIR::eDropKind::DEEP,
                    values[i].clone(),
                    ~0u,
                    target,
                    mv$(unwind),
                });
                mir.blocks.push_back(mv$(block));
            }
            mir.blocks.push_back(MIR::BasicBlock());

            if (custom_drop_call != ~0u) {
                assert(mir.blocks.at(custom_drop_call).terminator.is_Call());
                mir.blocks[custom_drop_call].terminator.as_Call().unwind = MIR::UnwindAction::make_Cleanup(cleanup_block(0));
            }
        }

        void terminate_block(MIR::Terminator term) {
            assert(mir.blocks.back().terminator.is_Incomplete());
            mir.blocks.back().terminator = mv$(term);
        }

        void terminateCall(MIR::LValue rv, MIR::CallTarget tgt, std::vector<MIR::Param> args, MIR::BasicBlockId bb_ret, MIR::BasicBlockId bb_panic) {
            this->terminate_block(MIR::Terminator::make_Call({bb_ret, MIR::UnwindAction::make_Cleanup(bb_panic), mv$(rv), mv$(tgt), mv$(args)}));
        }

        MIR::BasicBlockId pushCallDrop(const HIR::TypeData* ty) {
            // Get a `&mut *self`
            auto borrow_lv = this->add_local(state.crate.m_types.borrow(HIR::BorrowType::Unique, ty));
            this->push_stmt_assign(borrow_lv.clone(), MIR::RValue::make_Borrow({HIR::BorrowType::Unique, false, ::MIR::LValue::newDeref(this->self.clone())}));

            ensure_open();
            const auto call_block = static_cast<MIR::BasicBlockId>(mir.blocks.size() - 1);
            const auto ret_block = static_cast<MIR::BasicBlockId>(mir.blocks.size());
            this->terminate_block(MIR::Terminator::make_Call({
                ret_block,
                MIR::UnwindAction::make_Continue({}),
                MIR::LValue::newReturn(),
                ::HIR::Path(ty, state.resolve.m_lang_Drop, rcstring_drop),
                make_vec1<MIR::Param>(mv$(borrow_lv)),
            }));
            mir.blocks.push_back(MIR::BasicBlock());
            return call_block;
        }
    };

    MIR::LValue deref_box(MIR::LValue box) {
        auto inner_ptr = ::MIR::LValue::newField(::MIR::LValue::newField(mv$(box), 0), 0);
        inner_ptr = ::MIR::LValue::newField(std::move(inner_ptr), 0);
        return ::MIR::LValue::newDeref(std::move(inner_ptr));
    }

    ::MIR::LValue get_unit_ptr(const Span& sp, Builder& mutator, ::HIR::TypeRef ty, ::MIR::LValue lv, ::MIR::LValue& out_inner_ptr) {
        if (ty->is_Path()) {
            const auto& te = ty->as_Path();
            ASSERT_BUG(sp, te.binding.is_Struct(), "");
            const auto& ty_path = te.path.m_data.as_Generic();
            const auto& str = *te.binding.as_Struct();
            ::HIR::TypeRef tmp;
            auto monomorph = [&](const auto& t) {
                return MonomorphStatePtr(mutator.state.crate.m_types, nullptr, &ty_path.m_params, nullptr).monomorph_type(sp, t);
            };
            ::std::vector<::MIR::Param> vals;
            TU_MATCH_HDRA( (str.m_data), {)
            TU_ARMA(Unit, se) {
                }
                TU_ARMA(Tuple, se) {
                    for (unsigned int i = 0; i < se.size(); i++) {
                        auto val = ::MIR::LValue::newField((i == se.size() - 1 ? mv$(lv) : lv.clone()), i);
                        if (i == str.m_struct_markings.coerce_unsized_index) {
                            vals.push_back(get_unit_ptr(sp, mutator, monomorph(se[i].ent), mv$(val), out_inner_ptr));
                        } else {
                            vals.push_back(mv$(val));
                        }
                    }
                }
                TU_ARMA(Named, se) {
                    for (unsigned int i = 0; i < se.size(); i++) {
                        auto val = ::MIR::LValue::newField((i == se.size() - 1 ? mv$(lv) : lv.clone()), i);
                        if (i == str.m_struct_markings.coerce_unsized_index) {
                            vals.push_back(get_unit_ptr(sp, mutator, monomorph(se[i].ty), mv$(val), out_inner_ptr));
                        } else {
                            vals.push_back(mv$(val));
                        }
                    }
                }
            }

            auto new_path = ty_path.clone();
            return mutator.in_temporary( mv$(ty), ::MIR::RValue::make_Struct({ mv$(new_path), mv$(vals) }) );
        } else if (ty->is_Borrow() || ty->is_Pointer()) {
            out_inner_ptr = lv.clone();
            return mutator.in_temporary(mutator.state.crate.m_types.pointer(::HIR::BorrowType::Shared, mutator.state.crate.m_types.unit()), ::MIR::RValue::make_DstPtr({mv$(lv)}));
        } else {
            BUG(sp, "Unexpected type coerce_unsize in receiver - " << ty);
        }
    }
}

void TransAutoImpls(::HIR::Crate& crate, TransList& trans_list) {
    State state{crate, trans_list};

    {
        // Generate for all
        for (const auto& ty : trans_list.auto_clone_impls) {
            state.done_list.insert(ty);
            TransAutoImplClone(state, ty);
        }

        while (!state.todo_list.empty()) {
            auto ty = ::std::move(state.todo_list.front());
            state.todo_list.pop_back();

            TransAutoImplClone(state, mv$(ty));
        }

        auto impl_list_it = crate.m_trait_impls.find(state.langClone);
        for (const auto& ty : state.done_list) {
            assert(impl_list_it != crate.m_trait_impls.end());
            // TODO: Find a way of turning a set into a vector so items can be erased.

            auto p = ::HIR::Path(ty, ::HIR::GenericPath(state.langClone), "clone");
            //DEBUG("add_function(" << p << ")");
            auto e = trans_list.add_function(crate.m_types, ::std::move(p));

            const auto* impl_list = impl_list_it->second.get_list_for_type(ty);
            ASSERT_BUG(Span(), impl_list, "No impl list of Clone for " << ty);
            auto& impl = **::std::find_if(impl_list->begin(), impl_list->end(), [&](const auto& i) {
                return i->m_type == ty;
            });
            assert(impl.m_methods.size() == 1);
            e->ptr = &impl.m_methods.begin()->second.data;
        }
    }

    if (!trans_list.auto_fnptr_impls.empty()) {
        const auto& langFnPtr = crate.get_lang_item_path(Span(), "fn_ptr_trait");
        for (const auto& ty : trans_list.auto_fnptr_impls) {
            auto out_ty = state.crate.m_types.pointer(HIR::BorrowType::Shared, state.crate.m_types.unit());
            ::MIR::Function mir_fcn;

            ::MIR::BasicBlock bb;
            bb.statements.push_back(::MIR::Statement::make_Assign({::MIR::LValue::newReturn(), ::MIR::RValue::make_Cast({::MIR::LValue::newArgument(0), out_ty})}));
            bb.terminator = ::MIR::Terminator::make_Return({});
            mir_fcn.blocks.push_back(::std::move(bb));

            // Function
            // `fn addr(self) -> usize;`
            ::HIR::Function fcn{
                ::HIR::Function::Receiver::Value,
                ::HIR::GenericParams{},
                /*m_args=*/::make_vec1(::std::make_pair(::HIR::Pattern(::HIR::PatternBinding(false, ::HIR::PatternBinding::Type::Move, rcstring_self, 0), ::HIR::Pattern::Data::make_Any({})), ty)),
                /*m_return=*/std::move(out_ty),
                ::HIR::ExprPtr{}
            };
            fcn.m_code.m_mir = ::MIR::FunctionPointer(new ::MIR::Function(mv$(mir_fcn)));

            // Impl
            ::HIR::TraitImpl impl;
            impl.m_type = ty;
            impl.m_methods.insert(::std::make_pair(RcString::new_interned("addr"), ::HIR::TraitImpl::ImplEnt<::HIR::Function>{false, ::std::move(fcn)}));

            // Add impl to the crate
            auto& list = state.crate.m_trait_impls[langFnPtr].get_list_for_type_mut(impl.m_type);
            list.push_back(box$(impl));
            state.crate.m_all_trait_impls[langFnPtr].get_list_for_type_mut(list.back()->m_type).push_back(list.back().get());

            // - Add this function to the TransList

            {
                auto p = ::HIR::Path(ty, ::HIR::GenericPath(langFnPtr), "addr");
                auto e = trans_list.add_function(crate.m_types, ::std::move(p));

                auto& impl = *list.back();
                assert(impl.m_methods.size() == 1);
                e->ptr = &impl.m_methods.begin()->second.data;
            }
        }
    }

    // Trait object methods
    {
        TRACE_FUNCTION_F("Trait object methods");
        trans_list.m_auto_functions.reserve(trans_list.m_auto_functions.size() + trans_list.trait_object_methods.size());
        for (const auto& path : trans_list.trait_object_methods) {
            DEBUG(path);
            static Span sp;
            const auto& pe = path.m_data.as_UfcsKnown();
            const auto& trait_path = pe.trait;
            const auto& name = pe.item;
            const auto& ty_dyn = pe.type->as_TraitObject();

            const auto& trait = crate.get_trait_by_path(sp, trait_path.m_path);
            const auto& fcn_def = trait.m_values.at(name).as_Function();

            // Get the vtable index for this function
            unsigned vtable_idx = ty_dyn.m_trait.m_trait_ptr->get_vtable_value_index(trait_path, name);
            ASSERT_BUG(sp, vtable_idx > 0, "Calling method '" << name << "' from " << trait_path << " through " << pe.type << " which isn't in the vtable");

            auto pp = fcn_def.m_params.make_nop_params(crate.m_types, 1, true);
            MonomorphStatePtr ms(crate.m_types, pe.type, &trait_path.m_params, &pp);

            HIR::Function new_fcn;
            new_fcn.m_return = ms.monomorph_type(sp, fcn_def.m_return);
            state.resolve.expand_associated_types(sp, new_fcn.m_return);
            for (const auto& arg : fcn_def.m_args) {
                new_fcn.m_args.push_back(std::make_pair(HIR::Pattern(), ms.monomorph_type(sp, arg.second)));
                state.resolve.expand_associated_types(sp, new_fcn.m_args.back().second);
            }
            ASSERT_BUG(sp, !new_fcn.m_args.empty(), "Trait object method with no arguments?!");

            new_fcn.m_code.m_mir = MIR::FunctionPointer(new MIR::Function());
            Builder builder(state, *new_fcn.m_code.m_mir);

            MIR::LValue lv_self = MIR::LValue::newArgument(0);
            MIR::LValue lv_ptr;
            // ---
            // bb0:
            //   _1 = DstPtr a1
            switch (fcn_def.m_receiver) {
                case HIR::Function::Receiver::Value: {
                    // By-value trait object dispatch
                    // - Receiver should be a `&move` (BUT, does the caller know this?)
                    // - MIR Cleanup should fix that (after monomoprh)
                    auto& self_ty = new_fcn.m_args.front().second;
                    self_ty = crate.m_types.borrow(HIR::BorrowType::Owned, self_ty);
                    lv_ptr = builder.add_local(crate.m_types.borrow(HIR::BorrowType::Owned, crate.m_types.unit()));
                    builder.push_stmt_assign(lv_ptr.clone(), MIR::RValue::make_DstPtr({lv_self.clone()}));
                    DEBUG("<dyn " << trait_path << ">::" << name << " - By-Value");
                } break;
                case HIR::Function::Receiver::BorrowOwned:
                case HIR::Function::Receiver::BorrowUnique:
                case HIR::Function::Receiver::BorrowShared: {
                    ASSERT_BUG(sp, new_fcn.m_args.front().second->is_Borrow(), new_fcn.m_args.front().second);
                    auto bt = new_fcn.m_args.front().second->as_Borrow().type;
                    DEBUG("<dyn " << trait_path << ">::" << name << " - By-borrow");
                    lv_ptr = builder.add_local(crate.m_types.borrow(bt, crate.m_types.unit()));
                    builder.push_stmt_assign(lv_ptr.clone(), MIR::RValue::make_DstPtr({lv_self.clone()}));
                } break;
                case HIR::Function::Receiver::Box: {
                    // TODO: What is the real reciver here? (for the MIR)
                    // - the `self` type is `Box<dyn ThisTrait>`, so need to deref through that to the right type
                    DEBUG("<dyn " << trait_path << ">::" << name << " - Boxed");
                    // - Need to make a new receiver (convert `Box<dyn ThisTrait>` into `Box<()>`)
                    auto gpath = new_fcn.m_args.front().second->as_Path().path.m_data.as_Generic().clone();
                    gpath.m_params.m_types.at(0) = crate.m_types.unit();
                    auto ty = crate.m_types.path(mv$(gpath), new_fcn.m_args.front().second->as_Path().binding.clone());
                    lv_ptr = get_unit_ptr(sp, builder, mv$(ty), MIR::LValue::newArgument(0), lv_self);
                } break;
                default:
                    TODO(sp, "Handle different receiver types: <dyn " << trait_path << ">::" << name << " - self: " << new_fcn.m_args.front().second);
            }

            //   _2 = DstMeta a1
            auto lv_vtable = builder.add_local(crate.m_types.borrow(HIR::BorrowType::Shared, ty_dyn.m_trait.m_trait_ptr->get_vtable_type(sp, crate, ty_dyn)));
            builder.push_stmt_assign(lv_vtable.clone(), MIR::RValue::make_DstMeta({mv$(lv_self)}));
            //   rv = _2*.{idx}(a2, ...) goto bb2 else bb3
            std::vector<MIR::Param> call_args;
            call_args.push_back(mv$(lv_ptr));
            for (size_t i = 1; i < fcn_def.m_args.size(); i++) {
                call_args.push_back(MIR::LValue::newArgument(i));
            }
            builder.terminateCall(MIR::LValue::newReturn(), MIR::LValue::newField(MIR::LValue::newDeref(mv$(lv_vtable)), vtable_idx), mv$(call_args), 1, 2);
            // bb1:
            //   RETURN
            builder.ensure_open();
            builder.terminate_block(MIR::Terminator::make_Return({}));
            // bb2:
            //   UNWIND
            builder.ensure_open();
            builder.mir.blocks.back().is_cleanup = true;
            builder.terminate_block(MIR::Terminator::make_UnwindResume({}));
            // ---

            MIRValidate(state.resolve, HIR::ItemPath(path), *new_fcn.m_code.m_mir, new_fcn.m_args, new_fcn.m_return);
            trans_list.m_auto_functions.push_back(box$(new_fcn));
            auto* e = trans_list.add_function(crate.m_types, path.clone());
            if (e) {
                e->ptr = trans_list.m_auto_functions.back().get();
            } else {
                trans_list.m_auto_functions.pop_back();
            }
        }
    }

    // Create VTable instances
    {
        TRACE_FUNCTION_F("VTables");
        trans_list.m_auto_statics.reserve(trans_list.m_vtables.size());
        for (const auto& ent : trans_list.m_vtables) {
            Span sp;
            const auto& path = ent.first;
            const auto& trait_path = path.m_data.as_UfcsKnown().trait;
            const auto& type = path.m_data.as_UfcsKnown().type;

            struct {
                const char* fcn_name;
                const HIR::SimplePath* trait_path;
                HIR::BorrowType bt;
            } const entries[3] = {{"call", &state.resolve.m_lang_Fn, HIR::BorrowType::Shared}, {"call_mut", &state.resolve.m_lang_FnMut, HIR::BorrowType::Unique}, {"call_once", &state.resolve.m_lang_FnOnce, HIR::BorrowType::Owned}};

            size_t offset;
            if (trait_path.m_path == state.resolve.m_lang_Fn) {
                offset = 0;
            } else if (trait_path.m_path == state.resolve.m_lang_FnMut) {
                offset = 1;
            } else if (trait_path.m_path == state.resolve.m_lang_FnOnce) {
                offset = 2;
            } else {
                offset = 3; // Wait, is this reachable?
            }

            if (const auto* te = type->opt_NamedFunction()) {
                for (; offset < sizeof(entries) / sizeof(entries[0]); offset++) {
                    bool is_by_value = (offset == 2);
                    const auto& ent = entries[offset];

                    auto fcn_p = path.clone();
                    fcn_p.m_data.as_UfcsKnown().item = ent.fcn_name;
                    fcn_p.m_data.as_UfcsKnown().trait.m_path = ent.trait_path->clone();

                    auto* e = trans_list.add_function(crate.m_types, mv$(fcn_p));
                    if (e) {
                        auto ft = te->decay(crate.m_types, sp);

                        ::std::vector<HIR::TypeRef> arg_tys;
                        for (auto& ty : ft.m_arg_types) {
                            arg_tys.push_back(ty);
                        }
                        auto arg_ty = crate.m_types.tuple(mv$(arg_tys));
                        state.resolve.expand_associated_types(sp, arg_ty);

                        HIR::Function fcn;
                        fcn.m_return = ft.m_rettype;
                        state.resolve.expand_associated_types(sp, arg_ty);
                        fcn.m_args.push_back(std::make_pair(HIR::Pattern(), !is_by_value ? crate.m_types.borrow(ent.bt, type) : type));
                        fcn.m_args.push_back(std::make_pair(HIR::Pattern(), mv$(arg_ty)));

                        fcn.m_code.m_mir = MIR::FunctionPointer(new MIR::Function());
                        Builder builder(state, *fcn.m_code.m_mir);

                        std::vector<MIR::Param> arg_params;
                        for (size_t i = 0; i < ft.m_arg_types.size(); i++) {
                            arg_params.push_back(MIR::LValue::newField(MIR::LValue::newArgument(1), i));
                        }
                        builder.terminateCall(MIR::LValue::newReturn(), te->path.clone(), mv$(arg_params), 1, 2);
                        // BB1: Return
                        builder.ensure_open();
                        builder.terminate_block(MIR::Terminator::make_Return({}));
                        // BB1: Diverge
                        builder.ensure_open();
                        builder.mir.blocks.back().is_cleanup = true;
                        builder.terminate_block(MIR::Terminator::make_UnwindResume({}));

                        MIRValidate(state.resolve, HIR::ItemPath(path), *fcn.m_code.m_mir, fcn.m_args, fcn.m_return);
                        trans_list.m_auto_functions.push_back(box$(fcn));
                        e->ptr = trans_list.m_auto_functions.back().get();
                    }
                }
            } else if (const auto* te = type->opt_Function()) {
                for (; offset < sizeof(entries) / sizeof(entries[0]); offset++) {
                    bool is_by_value = (offset == 2);
                    const auto& ent = entries[offset];

                    auto fcn_p = path.clone();
                    fcn_p.m_data.as_UfcsKnown().item = ent.fcn_name;
                    fcn_p.m_data.as_UfcsKnown().trait.m_path = ent.trait_path->clone();

                    auto* e = trans_list.add_function(crate.m_types, mv$(fcn_p));
                    if (e) {
                        ::std::vector<HIR::TypeRef> arg_tys;
                        for (const auto& ty : te->m_arg_types) {
                            arg_tys.push_back(ty);
                        }
                        auto arg_ty = crate.m_types.tuple(mv$(arg_tys));

                        HIR::Function fcn;
                        fcn.m_return = te->m_rettype;
                        fcn.m_args.push_back(std::make_pair(HIR::Pattern(), !is_by_value ? crate.m_types.borrow(ent.bt, type) : type));
                        fcn.m_args.push_back(std::make_pair(HIR::Pattern(), mv$(arg_ty)));

                        fcn.m_code.m_mir = MIR::FunctionPointer(new MIR::Function());
                        Builder builder(state, *fcn.m_code.m_mir);

                        std::vector<MIR::Param> arg_params;
                        for (size_t i = 0; i < te->m_arg_types.size(); i++) {
                            arg_params.push_back(MIR::LValue::newField(MIR::LValue::newArgument(1), i));
                        }
                        builder.terminateCall(MIR::LValue::newReturn(), !is_by_value ? MIR::LValue::newDeref(MIR::LValue::newArgument(0)) : MIR::LValue::newArgument(0), mv$(arg_params), 1, 2);
                        // BB1: Return
                        builder.ensure_open();
                        builder.terminate_block(MIR::Terminator::make_Return({}));
                        // BB1: Diverge
                        builder.ensure_open();
                        builder.mir.blocks.back().is_cleanup = true;
                        builder.terminate_block(MIR::Terminator::make_UnwindResume({}));

                        MIRValidate(state.resolve, HIR::ItemPath(path), *fcn.m_code.m_mir, fcn.m_args, fcn.m_return);
                        trans_list.m_auto_functions.push_back(box$(fcn));
                        e->ptr = trans_list.m_auto_functions.back().get();
                    }
                }
            }
        }
        for (const auto& ent : trans_list.m_vtables) {
            Span sp;
            const auto& trait_path = ent.first.m_data.as_UfcsKnown().trait;
            const auto& type = ent.first.m_data.as_UfcsKnown().type;
            if (trait_path.m_path != HIR::SimplePath()) {
                continue;
            }
            DEBUG("VTABLE <empty> for " << type);

            ::std::vector<HIR::TypeRef> tuple_tys;
            tuple_tys.push_back(crate.m_types.primitive(::HIR::CoreType::Usize));
            tuple_tys.push_back(crate.m_types.primitive(::HIR::CoreType::Usize));
            tuple_tys.push_back(crate.m_types.primitive(::HIR::CoreType::Usize)); // fn
            auto vtable_ty = crate.m_types.tuple(std::move(tuple_tys));

            // Look up the size of the VTable, so we can allocate the right buffer size
            const auto* repr = TargetGetTypeRepr(sp, state.resolve, vtable_ty);
            assert(repr);

            HIR::Linkage linkage;
            linkage.type = HIR::Linkage::Type::Weak;
            HIR::Static vtable_static(::std::move(linkage), /*is_mut*/ false, mv$(vtable_ty), {});
            auto& vtable_data = vtable_static.m_value_res;
            const auto ptr_bytes = TargetGetPointerBits() / 8;
            vtable_data.bytes.resize(repr->size);
            size_t ofs = 0;
            auto push_ptr = [&vtable_data, &ofs, ptr_bytes](HIR::Path p) {
                DEBUG("@" << ofs << " = " << p);
                assert(ofs + ptr_bytes <= vtable_data.bytes.size());
                vtable_data.relocations.push_back(Reloc::new_named(ofs, ptr_bytes, mv$(p)));
                vtable_data.write_uint(ofs, ptr_bytes, EncodedLiteral::PTR_BASE);
                ofs += ptr_bytes;
                assert(ofs <= vtable_data.bytes.size());
            };
            // Drop glue
            trans_list.m_drop_glue.insert(type);
            push_ptr(::HIR::Path(type, rcstring_drop_glue));
            // Size & align
            {
                size_t size, align;
                // NOTE: Uses the Size+Align version because that doesn't panic on unsized
                ASSERT_BUG(sp, TargetGetSizeAndAlignOf(sp, state.resolve, type, size, align), "Unexpected generic? " << type);
                vtable_data.write_uint(ofs, ptr_bytes, size);
                ofs += ptr_bytes;
                vtable_data.write_uint(ofs, ptr_bytes, align);
                ofs += ptr_bytes;
            }
            assert(ofs == vtable_data.bytes.size());
            vtable_static.m_value_generated = true;

            // Add to list
            trans_list.m_auto_statics.push_back(box$(vtable_static));
            auto* e = trans_list.add_static(crate.m_types, ent.first.clone());
            if (e) {
                e->ptr = trans_list.m_auto_statics.back().get();
            } else {
                trans_list.m_auto_statics.pop_back();
            }
        }
        for (const auto& ent : trans_list.m_vtables) {
            Span sp;
            const auto& trait_path = ent.first.m_data.as_UfcsKnown().trait;
            const auto& type = ent.first.m_data.as_UfcsKnown().type;
            if (trait_path.m_path == HIR::SimplePath()) {
                continue;
            }
            DEBUG("VTABLE " << trait_path << " for " << type);
            // TODO: What's the use of `ent.second` here? (it's a `Trans_Params`)

            // Get the vtable type
            const auto& trait = crate.get_trait_by_path(sp, trait_path.m_path);
            const auto& vtable_sp = trait.m_vtable_path;
            ASSERT_BUG(sp, vtable_sp != HIR::SimplePath(), "Trait " << trait_path.m_path << " doesn't have a vtable");
            auto vtable_params = trait_path.m_params.clone();
            for (const auto& ty : trait.m_type_indexes) {
                auto aty = crate.m_types.path(::HIR::Path(type, trait_path.clone(), ty.first), {});
                state.resolve.expand_associated_types(sp, aty);
                vtable_params.m_types.push_back(mv$(aty));
            }
            const auto& vtable_ref = crate.get_struct_by_path(sp, vtable_sp);
            auto vtable_ty = crate.m_types.path(::HIR::GenericPath(mv$(vtable_sp), mv$(vtable_params)), &vtable_ref);

            // Ensure that the type is defined/populated
            trans_list.add_type(vtable_ty, false);

            // Look up the size of the VTable, so we can allocate the right buffer size
            const auto* repr = TargetGetTypeRepr(sp, state.resolve, vtable_ty);
            assert(repr);

            // Create vtable contents
            auto monomorph_cb_trait = MonomorphStatePtr(crate.m_types, type, &trait_path.m_params, nullptr);

            HIR::Linkage linkage;
            linkage.type = HIR::Linkage::Type::Weak;
            HIR::Static vtable_static(::std::move(linkage), /*is_mut*/ false, mv$(vtable_ty), {});
            auto& vtable_data = vtable_static.m_value_res;
            const auto ptr_bytes = TargetGetPointerBits() / 8;
            vtable_data.bytes.resize(repr->size);
            size_t ofs = 0;
            auto push_ptr = [&vtable_data, &ofs, ptr_bytes](HIR::Path p) {
                DEBUG("@" << ofs << " = " << p);
                assert(ofs + ptr_bytes <= vtable_data.bytes.size());
                vtable_data.relocations.push_back(Reloc::new_named(ofs, ptr_bytes, mv$(p)));
                vtable_data.write_uint(ofs, ptr_bytes, EncodedLiteral::PTR_BASE);
                ofs += ptr_bytes;
                assert(ofs <= vtable_data.bytes.size());
            };
            // Drop glue
            trans_list.m_drop_glue.insert(type);
            push_ptr(::HIR::Path(type, rcstring_drop_glue));
            // Size & align
            {
                size_t size, align;
                // NOTE: Uses the Size+Align version because that doesn't panic on unsized
                ASSERT_BUG(sp, TargetGetSizeAndAlignOf(sp, state.resolve, type, size, align), "Unexpected generic? " << type);
                vtable_data.write_uint(ofs, ptr_bytes, size);
                ofs += ptr_bytes;
                vtable_data.write_uint(ofs, ptr_bytes, align);
                ofs += ptr_bytes;
            }

            // Methods
            // - The `m_value_indexes` list isn't sorted (well, it's sorted differently) so we need an `O(n^2)` search

            for (unsigned int i = 0; i < trait.m_value_indexes.size(); i++) {
                // Find the corresponding vtable entry
                for (const auto& m : trait.m_value_indexes) {
                    // NOTE: The "3" is the number of non-method vtable entries
                    if (m.second.first != 3 + i) {
                        continue;
                    }

                    DEBUG("- " << m.second.first << " = " << m.second.second << " :: " << m.first);

                    auto trait_gpath = monomorph_cb_trait.monomorph_genericpath(sp, m.second.second, false);
                    auto item_path = ::HIR::Path(type, mv$(trait_gpath), m.first);

                    auto src_trait_ms = MonomorphStatePtr(crate.m_types, type, &item_path.m_data.as_UfcsKnown().trait.m_params, nullptr);
                    const auto& src_trait = state.resolve.m_crate.get_trait_by_path(sp, m.second.second.m_path);
                    const auto& item = src_trait.m_values.at(m.first);
                    // If the entry is a by-value function, then emit a reference to a shim
                    if (item.is_Function()) {
                        const auto& tpl_fcn = item.as_Function();
                        if (tpl_fcn.m_receiver == HIR::Function::Receiver::Value) {
                            auto call_path = item_path.clone();
                            item_path.m_data.as_UfcsKnown().item = RcString::new_interned(FMT(m.first << "#ptr"));
                            auto* e = trans_list.add_function(crate.m_types, item_path.clone());
                            if (e) {
                                // Create the shim (forward to the true call, dereferencing the first argument)
                                HIR::Function new_fcn;
                                new_fcn.m_return = src_trait_ms.monomorph_type(sp, tpl_fcn.m_return);
                                state.resolve.expand_associated_types(sp, new_fcn.m_return);
                                new_fcn.m_args.push_back(std::make_pair(HIR::Pattern(), crate.m_types.borrow(HIR::BorrowType::Owned, type)));
                                for (size_t i = 1; i < tpl_fcn.m_args.size(); i++) {
                                    new_fcn.m_args.push_back(std::make_pair(HIR::Pattern(), src_trait_ms.monomorph_type(sp, tpl_fcn.m_args[i].second)));
                                }
                                for (size_t i = 0; i < new_fcn.m_args.size(); i++) {
                                    state.resolve.expand_associated_types(sp, new_fcn.m_args[i].second);
                                }

                                DEBUG("> Generate shim: " << item_path);

                                new_fcn.m_code.m_mir = MIR::FunctionPointer(new MIR::Function());
                                ::MIR::TypeResolve mir_res{sp, state.resolve, FMT_CB(ss, ss << item_path), new_fcn.m_return, new_fcn.m_args, *new_fcn.m_code.m_mir};
                                Builder builder(state, *new_fcn.m_code.m_mir);
                                // bb0:
                                //   rv = CALL ...
                                ::std::vector<::MIR::Param> call_args;
                                call_args.push_back(::MIR::LValue::newDeref(::MIR::LValue::newArgument(0)));
                                for (size_t i = 1; i < tpl_fcn.m_args.size(); i++) {
                                    call_args.push_back(::MIR::LValue::newArgument(i));
                                }
                                builder.terminateCall(::MIR::LValue::newReturn(), mv$(call_path), std::move(call_args), 1, 2);
                                // bb1:
                                //   RETURN
                                builder.ensure_open();
                                builder.terminate_block(MIR::Terminator::make_Return({}));
                                // bb2:
                                //   UNWIND
                                builder.ensure_open();
                                builder.mir.blocks.back().is_cleanup = true;
                                builder.terminate_block(MIR::Terminator::make_UnwindResume({}));
                                // ---

                                MIRValidate(state.resolve, HIR::ItemPath(item_path), *new_fcn.m_code.m_mir, new_fcn.m_args, new_fcn.m_return);
                                trans_list.m_auto_functions.push_back(box$(new_fcn));
                                e->ptr = trans_list.m_auto_functions.back().get();
                            }
                        }
                    }
                    //MIR_ASSERT(*m_mir_res, tr.m_values.at(m.first).is_Function(), "TODO: Handle generating vtables with non-function items");
                    push_ptr(mv$(item_path));
                }
            }
            // Parent trait vtables
            for (size_t i = 0; i < trait.m_all_parent_traits.size(); i++) {
                const auto& pt = trait.m_all_parent_traits[i];
                const auto& fld = repr->fields.at(trait.m_vtable_parent_traits_start + i);
                ASSERT_BUG(sp, fld.offset == ofs, "");
                if (!fld.ty->is_Tuple()) {
                    auto pt_mono = MonomorphStatePtr(crate.m_types, nullptr, &trait_path.m_params, nullptr).monomorph_genericpath(sp, pt.m_path);
                    auto pt_vtable_path = ::HIR::Path(type, mv$(pt_mono), ent.first.m_data.as_UfcsKnown().item);
                    push_ptr(mv$(pt_vtable_path));
                }
            }
            assert(ofs == vtable_data.bytes.size());
            vtable_static.m_value_generated = true;

            // Add to list
            trans_list.m_auto_statics.push_back(box$(vtable_static));
            auto* e = trans_list.add_static(crate.m_types, ent.first.clone());
            if (e) {
                e->ptr = trans_list.m_auto_statics.back().get();
            } else {
                trans_list.m_auto_statics.pop_back();
            }
        }
        trans_list.m_vtables.clear();
    }

    // Create drop glue implementations
    {
        TRACE_FUNCTION_F("Drop Glue");
        for (const auto& ty : trans_list.m_types) {
            Span sp;
            if (ty.second) {
                continue;
            }
            if (!state.resolve.type_needs_drop_glue(sp, ty.first)) {
                continue;
            }

            if (ty.first->is_TraitObject()) {
                continue;
            }
            if (ty.first->is_Slice()) {
                continue;
            }
            trans_list.m_drop_glue.insert(ty.first);
        }

        for (const auto& ty : trans_list.m_drop_glue) {
            Span sp;
            auto path = ::HIR::Path(ty, rcstring_drop_glue);

            HIR::Function fcn;
            fcn.m_return = crate.m_types.unit();
            fcn.m_args.push_back(std::make_pair(HIR::Pattern(), crate.m_types.borrow(HIR::BorrowType::Owned, ty)));

            fcn.m_code.m_mir = MIR::FunctionPointer(new MIR::Function());
            ::MIR::TypeResolve mir_res{sp, state.resolve, FMT_CB(ss, ss << path), fcn.m_return, fcn.m_args, *fcn.m_code.m_mir};
            Builder builder(state, *fcn.m_code.m_mir);
            builder.push_stmt_assign(MIR::LValue::newReturn(), MIR::RValue::make_Tuple({}));
            auto owned_box_pointee_drop = static_cast<MIR::BasicBlockId>(~0u);
            auto owned_box_drop_call = static_cast<MIR::BasicBlockId>(~0u);
            if (const auto* ity = state.resolve.is_type_owned_box(ty)) {
                // Call inner destructors
                auto inner_val = deref_box(::MIR::LValue::newDeref(builder.self.clone()));
                HIR::TypeRef tmp;
                ASSERT_BUG(sp, mir_res.get_lvalue_type(tmp, inner_val) == ity, "Hard-coded box pointer path didn't result in the inner type");
                owned_box_pointee_drop = builder.push_stmt_drop(std::move(inner_val));
            }

            if (state.resolve.type_needs_drop_glue(sp, ty)) {
                TU_MATCH_HDRA( ((*ty)), {)
                TU_ARMA(Infer, _te)
                    throw "";
                    TU_ARMA(Generic, _te)
                    throw "";
                    TU_ARMA(ErasedType, _te)
                    throw "";
                    TU_ARMA(TraitObject, _te)
                    TODO(sp, "Drop glue for TraitObject? " << ty);
                    TU_ARMA(Slice, _te)
                    TODO(sp, "Drop glue for Slice? " << ty);
                    TU_ARMA(NodeType, _te)
                    TODO(sp, "Drop glue for NodeType? " << ty); // Should have been converted to a named structure by this point
                    TU_ARMA(Diverge, te) {
                        // Exists for reasons...
                        builder.terminate_block(MIR::Terminator::make_Unreachable({}));
                    }
                    TU_ARMA(Primitive, te) {
                        // Nothing to do
                    }
                    TU_ARMA(NamedFunction, te) {
                        // Nothing to do
                    }
                    TU_ARMA(Function, te) {
                        // Nothing to do
                    }
                    TU_ARMA(Pointer, te) {
                        // Nothing to do
                    }
                    TU_ARMA(Borrow, te) {
                        if (te.type == HIR::BorrowType::Owned) {
                            // `drop a0**`
                            builder.push_stmt_drop(::MIR::LValue::newDeref(::MIR::LValue::newDeref(builder.self.clone())));
                        }
                    }
                    TU_ARMA(Tuple, te) {
                        auto self = ::MIR::LValue::newDeref(builder.self.clone());
                        auto fld_lv = ::MIR::LValue::newField(mv$(self), 0);
                        ::std::vector<MIR::LValue> fields;
                        for (size_t i = 0; i < te.size(); i++) {
                            if (state.resolve.type_needs_drop_glue(sp, te[i])) {
                                fields.push_back(fld_lv.clone());
                            }
                            fld_lv.incField();
                        }
                        builder.push_drop_sequence(mv$(fields));
                    }
                    TU_ARMA(Array, te) {
                        auto size = te.size.as_Known();
                        auto self = ::MIR::LValue::newDeref(builder.self.clone());
                        if (size > 0 && state.resolve.type_needs_drop_glue(sp, te.inner)) {
                            // The C++ backend expands structural array drops
                            // itself, including the unwind cleanup of the tail.
                            // Keeping a second hand-built MIR loop here would
                            // duplicate that logic and lose the tail on panic.
                            builder.push_stmt_drop(mv$(self));
                        }
                    }
                    TU_ARMA(Path, te) {
                        bool has_drop = false;
                    TU_MATCH_HDRA( (te.binding), {)
                    TU_ARMA(Unbound, pbe) throw "";
                            TU_ARMA(Opaque, pbe) throw "";
                            TU_ARMA(ExternType, pbe) {
                                // Why is this trying to be dropped?
                            }

                            TU_ARMA(Struct, pbe) {
                                auto custom_drop_call = static_cast<MIR::BasicBlockId>(~0u);
                                if (pbe->m_markings.has_drop_impl) {
                                    custom_drop_call = builder.pushCallDrop(ty);
                                    if (owned_box_pointee_drop != ~0u) {
                                        owned_box_drop_call = custom_drop_call;
                                    }
                                    has_drop = true;
                                }

                                if (ty->is_Path() && ty->as_Path().is_generator()) {
                                    ASSERT_BUG(sp, has_drop, "");
                                    // Generators use a custom Drop impl that handles dropping values
                                } else {
                                    // NOTE: Lazy option of monomorphising and handling the two classes
                                    const auto* repr = TargetGetTypeRepr(sp, state.resolve, ty);
                                    ASSERT_BUG(sp, repr, "No repr for struct " << ty);

                                    auto self = ::MIR::LValue::newDeref(builder.self.clone());
                                    auto fld_lv = ::MIR::LValue::newField(mv$(self), 0);
                                    ::std::vector<MIR::LValue> fields;
                                    for (size_t i = 0; i < repr->fields.size(); i++) {
                                        if (state.resolve.type_needs_drop_glue(sp, repr->fields[i].ty)) {
                                            fields.push_back(fld_lv.clone());
                                        }
                                        fld_lv.incField();
                                    }
                                    builder.push_drop_sequence(mv$(fields), custom_drop_call);
                                }
                            }
                            TU_ARMA(Union, pbe) {
                                if (pbe->m_markings.has_drop_impl) {
                                    builder.pushCallDrop(ty);
                                    has_drop = true;
                                }
                                // Union requires no internal drop glue
                            }
                            TU_ARMA(Enum, pbe) {
                                auto custom_drop_call = static_cast<MIR::BasicBlockId>(~0u);
                                if (pbe->m_markings.has_drop_impl) {
                                    custom_drop_call = builder.pushCallDrop(ty);
                                    has_drop = true;
                                }
                                const HIR::Enum& enm = *pbe;
                        TU_MATCH_HDRA( (enm.m_data), {)
                        TU_ARMA(Value, ee) {
                                        builder.terminate_block(MIR::Terminator::make_Return({}));
                                    }
                                    TU_ARMA(Data, variants) {
                                        auto self = ::MIR::LValue::newDeref(builder.self.clone());
                                        MIR::Terminator::Data_Switch sw;
                                        sw.val = self.clone();
                                        const auto switch_block = builder.mir.blocks.size() - 1;
                                        builder.terminate_block(MIR::Terminator::make_Switch(mv$(sw)));

                                        ::std::vector<MIR::BasicBlockId> targets;
                                        targets.reserve(variants.size());
                                        auto fld_lv = ::MIR::LValue::newDowncast(mv$(self), 0);
                                        for (size_t idx = 0; idx < variants.size(); idx++) {
                                            builder.ensure_open();
                                            targets.push_back(builder.mir.blocks.size() - 1);
                                            // TODO: Monomorphise and check
                                            //if( state.resolve.type_needs_drop_glue(sp, repr->fields[i].ty) )
                                            {
                                                builder.push_stmt_drop(fld_lv.clone());
                                            }
                                            fld_lv.incDowncast();
                                            builder.ensure_open();
                                            builder.terminate_block(MIR::Terminator::make_Return({}));
                                        }
                                        builder.mir.blocks[switch_block].terminator.as_Switch().targets = mv$(targets);

                                        if (custom_drop_call != ~0u) {
                                            // If the user's Drop implementation
                                            // panics, select the active variant
                                            // again and destroy its payload on a
                                            // cleanup edge.
                                            const auto cleanup_switch = static_cast<MIR::BasicBlockId>(builder.mir.blocks.size());
                                            MIR::BasicBlock switch_cleanup_block;
                                            switch_cleanup_block.is_cleanup = true;
                                            MIR::Terminator::Data_Switch cleanup_switch_data;
                                            cleanup_switch_data.val = ::MIR::LValue::newDeref(builder.self.clone());
                                            switch_cleanup_block.terminator = MIR::Terminator::make_Switch(mv$(cleanup_switch_data));
                                            builder.mir.blocks.push_back(mv$(switch_cleanup_block));

                                            const auto resume = static_cast<MIR::BasicBlockId>(builder.mir.blocks.size() + variants.size());
                                            ::std::vector<MIR::BasicBlockId> cleanup_targets;
                                            cleanup_targets.reserve(variants.size());
                                            auto cleanup_field = ::MIR::LValue::newDowncast(
                                                ::MIR::LValue::newDeref(builder.self.clone()),
                                                0
                                            );
                                            for (size_t idx = 0; idx < variants.size(); idx++) {
                                                cleanup_targets.push_back(builder.mir.blocks.size());
                                                MIR::BasicBlock block;
                                                block.is_cleanup = true;
                                                block.terminator = MIR::Terminator::make_Drop({
                                                    MIR::eDropKind::DEEP,
                                                    cleanup_field.clone(),
                                                    ~0u,
                                                    resume,
                                                    MIR::UnwindAction::make_Terminate({}),
                                                });
                                                builder.mir.blocks.push_back(mv$(block));
                                                cleanup_field.incDowncast();
                                            }
                                            MIR::BasicBlock resume_block;
                                            resume_block.is_cleanup = true;
                                            resume_block.terminator = MIR::Terminator::make_UnwindResume({});
                                            builder.mir.blocks.push_back(mv$(resume_block));

                                            builder.mir.blocks[cleanup_switch].terminator.as_Switch().targets = mv$(cleanup_targets);
                                            builder.mir.blocks[custom_drop_call].terminator.as_Call().unwind = MIR::UnwindAction::make_Cleanup(cleanup_switch);
                                        }
                                    }
                        }
                            }
                    }
                    if( has_drop ) {
                            if (auto* e = trans_list.add_function(crate.m_types, ::HIR::Path(ty, state.resolve.m_lang_Drop, rcstring_drop))) {
                                MonomorphState params(crate.m_types);
                                auto p = ::HIR::Path(ty, state.resolve.m_lang_Drop, rcstring_drop);
                                auto fcn_e = state.resolve.get_value(sp, p, /*out*/ params, /*signature_only=*/false);
                                ASSERT_BUG(sp, fcn_e.is_Function(), "Drop didn't point to a function! " << fcn_e.tag_str() << " " << p);
                                ASSERT_BUG(sp, !params.has_types(), "Generic drop impl encountered during auto_impls (should have been populated during enum)");
                                e->force_prototype = true;
                                e->ptr = fcn_e.as_Function();
                                //e->pp = mv$(params);
                            }
                    }
                    }
                }
            }

            if (owned_box_pointee_drop != ~0u) {
                ASSERT_BUG(sp, owned_box_drop_call != ~0u, "Owned Box did not have a Drop implementation: " << ty);

                // The pointee is destroyed before Box::drop deallocates its
                // storage.  If the pointee panics, run Box::drop and the
                // remaining fields on the cleanup path before resuming the
                // original exception.
                if (builder.mir.blocks.back().terminator.is_Incomplete()) {
                    builder.terminate_block(MIR::Terminator::make_Return({}));
                }

                MIR::BasicBlockId after_cleanup_call;
                if (const auto* field_cleanup = builder.mir.blocks[owned_box_drop_call].terminator.as_Call().unwind.opt_Cleanup()) {
                    after_cleanup_call = *field_cleanup;
                } else {
                    after_cleanup_call = static_cast<MIR::BasicBlockId>(builder.mir.blocks.size() + 1);
                }

                auto cleanup_borrow = builder.add_local(state.crate.m_types.borrow(HIR::BorrowType::Unique, ty));
                const auto cleanup_call = static_cast<MIR::BasicBlockId>(builder.mir.blocks.size());
                MIR::BasicBlock cleanup_call_block;
                cleanup_call_block.is_cleanup = true;
                cleanup_call_block.statements.push_back(MIR::Statement::make_Assign({
                    cleanup_borrow.clone(),
                    MIR::RValue::make_Borrow({HIR::BorrowType::Unique, false, ::MIR::LValue::newDeref(builder.self.clone())}),
                }));
                cleanup_call_block.terminator = MIR::Terminator::make_Call({
                    after_cleanup_call,
                    MIR::UnwindAction::make_Terminate({}),
                    MIR::LValue::newReturn(),
                    ::HIR::Path(ty, state.resolve.m_lang_Drop, rcstring_drop),
                    make_vec1<MIR::Param>(mv$(cleanup_borrow)),
                });
                builder.mir.blocks.push_back(mv$(cleanup_call_block));

                if (after_cleanup_call == builder.mir.blocks.size()) {
                    MIR::BasicBlock resume_block;
                    resume_block.is_cleanup = true;
                    resume_block.terminator = MIR::Terminator::make_UnwindResume({});
                    builder.mir.blocks.push_back(mv$(resume_block));
                }
                builder.mir.blocks[owned_box_pointee_drop].terminator.as_Drop().unwind = MIR::UnwindAction::make_Cleanup(cleanup_call);
            }
            if (builder.mir.blocks.back().terminator.is_Incomplete()) {
                builder.terminate_block(MIR::Terminator::make_Return({}));
            }

            MIRValidate(state.resolve, HIR::ItemPath(path), *fcn.m_code.m_mir, fcn.m_args, fcn.m_return);
            trans_list.m_auto_functions.push_back(box$(fcn));
            auto* e = trans_list.add_function(crate.m_types, mv$(path));
            if (e) {
                e->ptr = trans_list.m_auto_functions.back().get();
            } else {
                trans_list.m_auto_functions.pop_back();
            }
        }
    }
}


namespace {
    // Translation paths are assembled after inference and monomorphisation.
    // A nominal type can therefore arrive through an older, structurally
    // equivalent TypeRef whose path binding is still Unbound.  rustc's Ty
    // carries the ADT DefId as part of the nominal type and cannot represent
    // that state.  Restore the equivalent invariant at the translation
    // boundary instead of teaching codegen to accept missing metadata.
    class BindTranslationNominals final: public ::HIR::Visitor {
        const ::HIR::Crate& m_crate;

    public:
        explicit BindTranslationNominals(const ::HIR::Crate& crate)
            : ::HIR::Visitor(nullptr, crate.m_types)
            , m_crate(crate)
        {
        }

        void visit_type(::HIR::TypeRef& ty) override {
            auto data = ty->clone_data();
            visit_type_data(data);

            if (auto* path_ty = data.opt_Path()) {
                if (path_ty->binding.is_Unbound() && path_ty->path.m_data.is_Generic()) {
                    const auto& path = path_ty->path.m_data.as_Generic().m_path;
                    const auto& item = m_crate.get_typeitem_by_path(Span(), path);
                    TU_MATCH_HDRA((item), {)
                    default:
                        BUG(Span(), "Nominal translation type points to " << item.tag_str() << " - " << ty);
                    TU_ARMA(ExternType, e) {
                        path_ty->binding = ::HIR::TypePathBinding::make_ExternType(&e);
                    }
                    TU_ARMA(Struct, e) {
                        path_ty->binding = ::HIR::TypePathBinding::make_Struct(&e);
                    }
                    TU_ARMA(Union, e) {
                        path_ty->binding = ::HIR::TypePathBinding::make_Union(&e);
                    }
                    TU_ARMA(Enum, e) {
                        path_ty->binding = ::HIR::TypePathBinding::make_Enum(&e);
                    }
                    }
                }
            }

            ty = type_interner().intern(mv$(data));
        }
    };

    void bind_translation_nominals(const ::HIR::Crate& crate, ::HIR::Path& path) {
        BindTranslationNominals visitor(crate);
        visitor.visit_path(path, ::HIR::Visitor::PathContext::VALUE);
    }

    struct EnumState {
        const ::HIR::Crate& crate;
        StaticTraitResolve resolve;
        TransList rv;
        const TransList* orig_list;

        // Queue of items to enumerate
        ::std::deque<TransListFunction*> fcn_queue;
        ::std::vector<TransListFunction*> fcns_to_type_visit;

        ::std::set<std::string> emitted_functions;

        // Map of locally-defined exported `link_name` functions
        ::std::unordered_map<std::string, std::pair<HIR::SimplePath, const HIR::Function*>> m_link_functions;

        EnumState(const ::HIR::Crate& crate)
            : crate(crate)
            , resolve(crate)
            , rv()
            , orig_list(nullptr)
        {
            enumerate_link_functions();
        }

        void enum_fcn(::HIR::Path p, const ::HIR::Function& fcn, TransParams pp) {
            if (auto* e = rv.add_function(crate.m_types, mv$(p))) {
#if 1
                auto name = FMT(TransMangle(*e->path));
                auto inserted = emitted_functions.insert(name).second;
                ASSERT_BUG(Span(), inserted, "Duplicated mangled name - " << *e->path);
#endif
                fcns_to_type_visit.push_back(e);
                e->ptr = &fcn;
                e->pp = mv$(pp);
                DEBUG(*e->path << " w/ " << e->pp.pp_impl << " and " << e->pp.pp_method);
                fcn_queue.push_back(e);
            }
        }

    private:
        void enumerate_link_functions() {
            enumerate_link_functions_in(crate.m_root_module, HIR::ItemPath(crate.m_crate_name));
            for (const auto& e_crate : crate.m_ext_crates) {
                enumerate_link_functions_in(e_crate.second.m_data->m_root_module, HIR::ItemPath(e_crate.first));
            }
        }

        void enumerate_link_functions_in(const HIR::Module& mod, HIR::ItemPath mod_path) {
            for (const auto& vi : mod.m_value_items) {
                if (const auto* ip = vi.second->ent.opt_Function()) {
                    const auto& i = *ip;
                    if (i.m_code.m_mir && i.m_linkage.name != "") {
                        m_link_functions[i.m_linkage.name] = std::make_pair((mod_path + vi.first).get_simple_path(), &i);
                    }
                }
            }

            for (const auto& ti : mod.m_mod_items) {
                if (const auto* ip = ti.second->ent.opt_Module()) {
                    enumerate_link_functions_in(*ip, mod_path + ti.first);
                }
            }
        }
    };

    const RcString enumerate_rcstring_drop = RcString::new_interned("drop");
}

TransList TransEnumerateCommonPost(EnumState& state);
namespace {
    void TransEnumerateExplicitLinkage(EnumState& state, const ::HIR::Module& mod, ::HIR::SimplePath mod_path);
}
void TransEnumerateTypes(EnumState& state);
void TransEnumerateFillFromPath(EnumState& state, const ::HIR::Path& path, const TransParams& pp);
void TransEnumerateFillFromPathMono(EnumState& state, ::HIR::Path path);
void TransEnumerateFillFromFunction(EnumState& state, const ::HIR::Path& path, const ::HIR::Function& function, const TransParams& pp);
void TransEnumerateFillFromStatic(EnumState& state, const ::HIR::Static& stat, TransListStatic& stat_out, TransParams pp);
void TransEnumerateFillFromVTable(EnumState& state, ::HIR::Path vtable_path, const TransParams& pp);
void TransEnumerateFillFromLiteral(EnumState& state, const EncodedLiteral& lit, const TransParams& pp);
void TransEnumerateFillFromMIR(MIR::EnumCache& state, const ::MIR::Function& code);

void TransEnumerateGlobalAllocator(EnumState& state) {
    const auto allocator_it = state.crate.m_lang_items.find(GLOBAL_ALLOCATOR_LANG_ITEM);
    if (allocator_it == state.crate.m_lang_items.end()) {
        return;
    }

    const auto& allocator_path = allocator_it->second;
    const auto& allocator = state.crate.get_static_by_path(Span(), allocator_path);

    HIR::Path static_path = HIR::GenericPath(allocator_path);
    state.rv.m_roots.push_back(static_path.clone());
    TransEnumerateFillFromPathMono(state, std::move(static_path));

    auto layout_ctor = TransAllocatorLayoutCtorPath(state.crate);
    state.rv.m_roots.push_back(layout_ctor.clone());
    TransEnumerateFillFromPathMono(state, std::move(layout_ctor));

    for (size_t i = 0; i < NUM_ALLOCATOR_METHODS; i++) {
        auto method_path = TransAllocatorMethodPath(state.crate, allocator.m_type, ALLOCATOR_METHODS[i]);
        state.rv.m_roots.push_back(method_path.clone());
        TransEnumerateFillFromPathMono(state, std::move(method_path));
    }
}

namespace MIR {
    struct EnumCache {
        ::std::vector<const ::HIR::Path*> paths;
        ::std::vector<const ::HIR::TypeData*> typeids;

        EnumCache() {
        }

        void insert_path(const ::HIR::Path& new_path) {
            for (const auto* p : this->paths) {
                if (*p == new_path) {
                    return;
                }
            }
            this->paths.push_back(&new_path);
        }

        void insert_typeid(const ::HIR::TypeData* new_ty) {
            for (const auto* p : this->typeids) {
                if (p == new_ty) {
                    return;
                }
            }
            this->typeids.push_back(new_ty);
        }

        void apply(EnumState& state, const TransParams& pp) const {
            TRACE_FUNCTION_F(" w/ impl=" << pp.pp_impl << " method=" << pp.pp_method);
            for (const auto* ty_p : this->typeids) {
                DEBUG("TypeID " << ty_p);
                state.rv.m_typeids.insert(pp.monomorph(state.resolve, ty_p));
            }
            for (const auto& path : this->paths) {
                DEBUG("Path " << *path);
                TransEnumerateFillFromPath(state, *path, pp);
            }
        }
    };

    EnumCachePtr::~EnumCachePtr() {
        delete this->p;
        this->p = nullptr;
    }
}

/// Enumerate trans items starting from `::main` (binary crate)
TransList TransEnumerateMain(const ::HIR::Crate& crate) {
    static Span sp;

    EnumState state{crate};

    if (!crate.m_no_main) {
        auto c_start_path = crate.get_lang_item_path_opt("mrustc-start");
        if (c_start_path == ::HIR::SimplePath()) {
            // user entrypoint
            auto main_path = crate.get_lang_item_path(Span(), "mrustc-main");
            const auto& main_fcn = crate.get_function_by_path(sp, main_path);

            state.rv.m_roots.push_back(main_path);
            state.enum_fcn(main_path, main_fcn, TransParams(crate.m_types));

            // "start" language item
            // - Takes main, and argc/argv as arguments
            const auto& start_path = crate.get_lang_item_path_opt("start");
            if (start_path != ::HIR::SimplePath()) {
                const auto& fcn = crate.get_function_by_path(sp, start_path);

                TransParams lang_start_pp(crate.m_types);
                lang_start_pp.pp_method.m_types.push_back(main_fcn.m_return);
                HIR::Path p = HIR::GenericPath(start_path, lang_start_pp.pp_method.clone());
                state.rv.m_roots.push_back(p.clone());
                //state.enum_fcn( start_path, fcn, mv$(lang_start_pp) );
                state.enum_fcn(std::move(p), fcn, mv$(lang_start_pp));
            } else if (!crate.m_is_no_core) {
                // Preserve the usual diagnostic for crates that rely on the
                // standard entrypoint protocol.
                crate.get_lang_item_path(sp, "start");
            }
        } else {
            const auto& fcn = crate.get_function_by_path(sp, c_start_path);

            state.rv.m_roots.push_back(c_start_path);
            state.enum_fcn(c_start_path, fcn, TransParams(crate.m_types));
        }
    }

    TransEnumerateExplicitLinkage(state, crate.m_root_module, ::HIR::SimplePath(crate.m_crate_name, {}));
    TransEnumerateGlobalAllocator(state);

    return TransEnumerateCommonPost(state);
}

namespace {
    void TransEnumerateGenericFunctionItems(EnumState& state, const Span& sp, const ::HIR::Function& e, MonomorphStatePtr ms) {
        if (e.m_code.m_mir) {
            const auto& mir_fcn = *e.m_code.m_mir;
            auto params = e.m_params.make_empty_params(true);
            ms.pp_method = &params;
            if (!mir_fcn.trans_enum_state) {
                auto* esp = new MIR::EnumCache();
                TransEnumerateFillFromMIR(*esp, *e.m_code.m_mir);
                mir_fcn.trans_enum_state = ::MIR::EnumCachePtr(esp);
            }

            for (const auto& path : mir_fcn.trans_enum_state->paths) {
                if (!monomorphise_path_needed(*path, true)) {
                    DEBUG("Path " << *path);
                    MonomorphState unused_ms(state.crate.m_types);
                    auto v = state.resolve.get_value(sp, *path, unused_ms, true);
                    if (v.is_StructConstructor() || v.is_EnumConstructor()) {
                    } else {
                        auto p = ms.monomorph_path(sp, *path);
                        state.rv.m_roots.push_back(p.clone());
                        TransEnumerateFillFromPathMono(state, std::move(p));
                    }
                } else {
                    DEBUG("Path " << *path << " - Generic");
                }
            }
        }
    }

    void TransEnumerateValItem(EnumState& state, const ::HIR::ValueItem& vi, bool is_visible, ::std::function<::HIR::SimplePath()> get_path) {
        TRACE_FUNCTION_F(get_path() << " : " << vi.tag_str() << " is_visible=" << is_visible);
        const Span sp;
        switch (vi.tag()) {
            case ::HIR::ValueItem::TAGDEAD:
                throw "";
                TU_ARM(vi, Import, e) {
                    // TODO: If visible, ensure that target is visited.
                    if (is_visible) {
                        if (!e.is_variant && e.path.crate_name() == state.crate.m_crate_name) {
                            const auto& vi2 = state.crate.get_valitem_by_path(sp, e.path, false);
                            TransEnumerateValItem(state, vi2, is_visible, [&]() {
                                return e.path;
                            });
                        }
                    }
                }
                break;
                TU_ARM(vi, StructConstant, e) {
                }
                break;
                TU_ARM(vi, StructConstructor, e) {
                }
                break;
                TU_ARM(vi, Constant, e) {
                    if (is_visible) {
                        // Visible constants need their relocations added as roots
                        // - Can't add this logic to `Trans_Enumerate_FillFrom_Literal` as it's used by non-public enumeration
                        for (const auto& r : e.m_value_res.relocations) {
                            if (r.p) {
                                state.rv.m_roots.push_back(r.p->clone());
                            }
                        }
                        TransEnumerateFillFromLiteral(state, e.m_value_res, TransParams(state.crate.m_types));
                    }
                }
                break;
                TU_ARM(vi, Static, e) {
                    if (e.m_linkage.name != "" || e.m_linkage.section != "") {
                        // If a link name is set, force emit
                        is_visible = true;
                    }
                    if (is_visible && !e.m_params.is_generic()) {
                        // HACK: Refuse to emit unused generated statics
                        // - Needed because all items are visited (regardless of
                        // visibility)
                        if (e.m_type->is_Infer()) {
                            continue;
                        }
                        //state.enum_static(mod_path + vi.first, *e);
                        auto* ptr = state.rv.add_static(state.crate.m_types, get_path());
                        if (ptr) {
                            TransEnumerateFillFromStatic(state, e, *ptr, TransParams(state.crate.m_types));
                        }

                        state.rv.m_roots.push_back(get_path());
                    }
                }
                break;
                TU_ARM(vi, Function, e) {
                    bool is_inline = false;
                    if (is_visible) {
                        switch (e.m_markings.inline_type) {
                            case ::HIR::Function::Markings::Inline::Always:
                            case ::HIR::Function::Markings::Inline::Normal:
                                // Don't emit, it's going to be emitted by callers
                                DEBUG("Don't emit inlined function");
                                is_inline = true;
                                break;
                            case ::HIR::Function::Markings::Inline::Auto:
                            case ::HIR::Function::Markings::Inline::Never:
                                // Should still be emitted, as it won't be emitted downstream
                                break;
                        }
                    }
                    if (e.m_linkage.name != "" || e.m_linkage.section != "") {
                        // If a link name is set, force emit
                        is_visible = true;
                    }

                    if (e.m_params.is_generic() || (is_inline && is_visible)) {
                        const_cast<::HIR::Function&>(e).m_save_code = true;
                    } else {
                        if (is_visible) {
                            TransParams pp(state.crate.m_types);
                            pp.pp_method = e.m_params.make_empty_params(/*lifetimes_only=*/true);
                            state.enum_fcn(get_path(), e, mv$(pp));

                            state.rv.m_roots.push_back(get_path());
                        }
                    }
                    // Enumerate concrete items used
                    // - These are functions that have to be emitted, even if they're not public themselves
                    if (e.m_save_code) {
                        TransEnumerateGenericFunctionItems(state, sp, e, MonomorphStatePtr(state.crate.m_types));
                    }
                }
                break;
        }
    }

    void TransEnumerateExplicitLinkage(EnumState& state, const ::HIR::Module& mod, ::HIR::SimplePath mod_path) {
        for (const auto& vi : mod.m_value_items) {
            bool has_explicit_linkage = false;
            if (const auto* function = vi.second->ent.opt_Function()) {
                has_explicit_linkage = function->m_linkage.name != "" || function->m_linkage.section != "";
            } else if (const auto* stat = vi.second->ent.opt_Static()) {
                has_explicit_linkage = stat->m_linkage.name != "" || stat->m_linkage.section != "";
            }
            if (has_explicit_linkage) {
                auto path = mod_path + vi.first;
                TransEnumerateValItem(state, vi.second->ent, false, [path]() {
                    return path;
                });
            }
        }

        for (const auto& ti : mod.m_mod_items) {
            if (const auto* child = ti.second->ent.opt_Module()) {
                TransEnumerateExplicitLinkage(state, *child, mod_path + ti.first);
            }
        }
    }

    void TransEnumeratePublicMod(EnumState& state, ::HIR::Module& mod, ::HIR::SimplePath mod_path, bool is_visible) {
        TRACE_FUNCTION_F(mod_path);
        for (auto& vi : mod.m_value_items) {
            bool emit = is_visible && vi.second->publicity.is_global();
            auto p = mod_path + vi.first;
            if (::std::any_of(state.crate.m_lang_items.begin(), state.crate.m_lang_items.end(), [&](const auto& e) {
                return e.second == p;
            })) {
                emit = true;
            }
            TransEnumerateValItem(state, vi.second->ent, emit, [&]() {
                return p;
            });
        }

        for (auto& ti : mod.m_mod_items) {
            if (auto* e = ti.second->ent.opt_Module()) {
                TransEnumeratePublicMod(state, *e, mod_path + ti.first, ti.second->publicity.is_global());
            } else if (const HIR::Trait* e = ti.second->ent.opt_Trait()) {
                auto params = e->m_params.make_empty_params(true);
                MonomorphStatePtr ms(state.crate.m_types);
                ms.pp_impl = &params;
                for (const auto& vi : e->m_values) {
                    if (const auto* fcn = vi.second.opt_Function()) {
                        TransEnumerateGenericFunctionItems(state, Span(), *fcn, ms);
                    }
                }
            }
        }
    }

    void TransEnumeratePublicTraitImpl(EnumState& state, StaticTraitResolve& resolve, const ::HIR::SimplePath& trait_path, /*const*/ ::HIR::TraitImpl& impl) {
        static Span sp;
        const auto& impl_ty = impl.m_type;
        TRACE_FUNCTION_F("Impl" << impl.m_params.fmt_args() << " " << trait_path << impl.m_trait_args << " for " << impl_ty);

        auto params_impl = impl.m_params.make_empty_params(true);
        MonomorphStatePtr ms(state.crate.m_types);
        ms.pp_impl = &params_impl;
        if (!impl.m_params.is_generic()) {
            auto impl_params = impl.m_params.make_empty_params(true);
            auto cb_monomorph = MonomorphStatePtr(state.crate.m_types, impl_ty, &impl.m_trait_args, nullptr);
            auto cb_monomorph2 = MonomorphStatePtr(state.crate.m_types, nullptr, &impl_params, nullptr);

            // TODO: Only emit impls if the type is going to be visible to downstream crates
            // - But how to tell that? What if the type is exposed via `-> impl Foo`?
            // - Lazy (wrong) version would be to not emit if the type is private - but private types can be leaked
            //   - Could flag leaked private types in a previous pass?

            // Emit each method/static (in the trait itself)
            const auto& trait = resolve.m_crate.get_trait_by_path(sp, trait_path);
            for (const auto& vi : trait.m_values) {
                TRACE_FUNCTION_F("Item " << vi.first << " : " << vi.second.tag_str());
                // Constant, no codegen
                if (vi.second.is_Constant())
                    ;
                // Generic method, no codegen
                else if (vi.second.is_Function() && vi.second.as_Function().m_params.is_generic())
                    ;
                // VTable, magic
                else if (vi.first == "vtable#")
                    ;
                else {
                    // Check bounds before queueing for codegen
                    HIR::PathParams pp;
                    if (vi.second.is_Function()) {
                        const auto& fcn = vi.second.as_Function();
                        bool rv = true;
                        DEBUG("Bounds = " << fcn.m_params.fmt_bounds());
                        for (const auto& b : fcn.m_params.m_bounds) {
                            if (!b.is_TraitBound()) {
                                continue;
                            }
                            const auto& be = b.as_TraitBound();

                            auto b_ty_mono = resolve.monomorph_expand(sp, be.type, cb_monomorph);
                            auto b_tp_mono = cb_monomorph.monomorph_traitpath(sp, be.trait, false);
                            resolve.expand_associated_types_tp(sp, b_tp_mono);

                            DEBUG("Check " << b_ty_mono << ": " << b_tp_mono);
                            rv = resolve.find_impl(sp, b_tp_mono.m_path.m_path, b_tp_mono.m_path.m_params, b_ty_mono, [&](const ImplRef& impl, bool) {
                                for (const auto& ty_b : b_tp_mono.m_type_bounds) {
                                    const auto& ty = impl.get_type(state.crate.m_types, ty_b.first.c_str(), ty_b.second.aty_params);
                                    DEBUG("ATY " << ty_b.first << " " << ty << " ?= exp " << ty_b.second.type);
                                    if (ty != ty_b.second.type) {
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

                        DEBUG("Params = " << fcn.m_params.fmt_args());
                        for (const auto& lft : fcn.m_params.m_lifetimes) {
                            (void)lft;
                            pp.m_lifetimes.push_back(HIR::LifetimeRef());
                        }
                    }
                    auto path = ::HIR::Path(cb_monomorph2.monomorph_type(sp, impl_ty), ::HIR::GenericPath(trait_path, cb_monomorph2.monomorph_path_params(sp, impl.m_trait_args, false)), vi.first, mv$(pp));
                    state.rv.m_roots.push_back(path.clone());
                    TransEnumerateFillFromPathMono(state, mv$(path));
                    //state.enum_fcn(mv$(path), fcn.second.data, {});
                }
            }
            for (auto& m : impl.m_methods) {
                if (m.second.data.m_params.is_generic()) {
                    m.second.data.m_save_code = true;
                    TransEnumerateGenericFunctionItems(state, Span(), m.second.data, ms);
                }
            }
        } else {
            for (auto& m : impl.m_methods) {
                m.second.data.m_save_code = true;
                TransEnumerateGenericFunctionItems(state, Span(), m.second.data, ms);
            }
        }
    }
}

/// Enumerate trans items for all public non-generic items (library crate)
TransList TransEnumeratePublic(::HIR::Crate& crate) {
    static Span sp;
    EnumState state{crate};

    TransEnumeratePublicMod(state, crate.m_root_module, ::HIR::SimplePath(crate.m_crate_name, {}), true);

    // Impl blocks
    StaticTraitResolve resolve{crate};
    for (auto& impl_group : crate.m_trait_impls) {
        const auto& trait_path = impl_group.first;
        for (auto& impl_list : impl_group.second.named) {
            for (auto& impl : impl_list.second) {
                TransEnumeratePublicTraitImpl(state, resolve, trait_path, *impl);
            }
        }
        for (auto& impl : impl_group.second.non_named) {
            TransEnumeratePublicTraitImpl(state, resolve, trait_path, *impl);
        }
        for (auto& impl : impl_group.second.generic) {
            TransEnumeratePublicTraitImpl(state, resolve, trait_path, *impl);
        }
    }

    struct H1 {
        static void enumerate_type_impl(EnumState& state, ::HIR::TypeImpl& impl) {
            TRACE_FUNCTION_F("impl" << impl.m_params.fmt_args() << " " << impl.m_type);
            HIR::PathParams impl_params = impl.m_params.make_empty_params(/*allow_lifetimes_only=*/true);
            MonomorphStatePtr ms(state.crate.m_types);
            ms.pp_impl = &impl_params;
            if (!impl.m_params.is_generic()) {
                for (auto& fcn : impl.m_methods) {
                    DEBUG("fn " << fcn.first << fcn.second.data.m_params.fmt_args());
                    if (!fcn.second.data.m_params.is_generic()) {
                        TransParams pp(state.crate.m_types);
                        pp.pp_impl = impl_params.clone();
                        pp.pp_method = fcn.second.data.m_params.make_empty_params(/*allow_lifetimes_only=*/true);
                        auto path = ::HIR::Path(MonomorphStatePtr(state.crate.m_types, nullptr, &impl_params, nullptr).monomorph_type(Span(), impl.m_type), fcn.first);
                        path.m_data.as_UfcsInherent().impl_params = pp.pp_impl.clone();
                        path.m_data.as_UfcsInherent().params = pp.pp_method.clone();
                        if (fcn.second.publicity.is_global()) {
                            state.rv.m_roots.push_back(path.clone());
                        }
                        state.enum_fcn(mv$(path), fcn.second.data, mv$(pp));
                    } else {
                        fcn.second.data.m_save_code = true;
                    }
                    if (fcn.second.data.m_save_code) {
                        TransEnumerateGenericFunctionItems(state, Span(), fcn.second.data, ms);
                    }
                }
            } else {
                for (auto& m : impl.m_methods) {
                    m.second.data.m_save_code = true;
                    TransEnumerateGenericFunctionItems(state, Span(), m.second.data, ms);
                }
            }
            for (auto& e : impl.m_constants) {
                TransParams tp(state.crate.m_types);
                tp.pp_impl = impl.m_params.make_empty_params(/*allow_lifetimes_only=*/true);
                TransEnumerateFillFromLiteral(state, e.second.data.m_value_res, std::move(tp));

                if (e.second.publicity.is_global() && !impl.m_params.is_generic() && !e.second.data.m_params.is_generic()) {
                    auto pp_method = e.second.data.m_params.make_empty_params(/*allow_lifetimes_only=*/true);
                    for (const auto& r : e.second.data.m_value_res.relocations) {
                        if (r.p) {
                            // Still need to monomorph, as lifetimes aren't counted in `is_generic`
                            state.rv.m_roots.push_back(MonomorphStatePtr(state.crate.m_types, nullptr, &impl_params, &pp_method).monomorph_path(Span(), *r.p));
                        }
                    }
                }
            }
        }
    };

    for (auto& impl_grp : crate.m_type_impls.named) {
        for (auto& impl : impl_grp.second) {
            H1::enumerate_type_impl(state, *impl);
        }
    }
    for (auto& impl : crate.m_type_impls.non_named) {
        H1::enumerate_type_impl(state, *impl);
    }
    for (auto& impl : crate.m_type_impls.generic) {
        H1::enumerate_type_impl(state, *impl);
    }

    // Ensure that the panic handler is emitted
    {
        auto it = crate.m_lang_items.find("mrustc-panic_implementation");
        if (it != crate.m_lang_items.end()) {
            HIR::GenericPath p = it->second;
            const auto& f = crate.get_function_by_path(Span(), p.m_path);
            p.m_params = f.m_params.make_empty_params(true);
            TransEnumerateFillFromPathMono(state, std::move(p));
        }
    }

    auto rv = TransEnumerateCommonPost(state);

    // Strip out any functions/types/statics that are still generic?
    for (auto it = rv.m_functions.begin(); it != rv.m_functions.end();) {
        if (monomorphise_path_needed(it->first, /*ignore_lifetimes*/ true)) {
            rv.m_functions.erase(it++);
        } else {
            ++it;
        }
    }
    for (auto it = rv.m_statics.begin(); it != rv.m_statics.end();) {
        if (monomorphise_path_needed(it->first, /*ignore_lifetimes*/ true)) {
            rv.m_statics.erase(it++);
        } else {
            ++it;
        }
    }

    return rv;
}

namespace {
    template <typename T>
    void remove_missing(std::map<HIR::Path, T>& target, const std::map<HIR::Path, T>& tpl) {
        ::std::unordered_map<::std::string, const HIR::Path*> required_symbols;
        for (const auto& entry : tpl) {
            auto symbol = FMT(TransMangle(entry.first));
            auto inserted = required_symbols.emplace(mv$(symbol), &entry.first);
            ASSERT_BUG(Span(), inserted.second || inserted.first->second->equals_ignoring_regions(entry.first),
                "Distinct paths have the same mangled name: " << *inserted.first->second << " and " << entry.first);
        }

        for (auto it_in = target.begin(); it_in != target.end();) {
            const auto symbol = FMT(TransMangle(it_in->first));
            const auto required = required_symbols.find(symbol);
            if (required == required_symbols.end()) {
                DEBUG("Remove " << it_in->first);
                it_in = target.erase(it_in);
            } else {
                ASSERT_BUG(Span(), required->second->equals_ignoring_regions(it_in->first),
                    "Distinct paths have the same mangled name: " << *required->second << " and " << it_in->first);
                DEBUG("Keep " << it_in->first);
                ++it_in;
            }
        }
    }
}

void TransEnumerateCleanup(const ::HIR::Crate& crate, TransList& list) {
#if 1
    // Clear the function enum cache and re-generate
    // - This is called after optimisation, so the cache may point to functions that have been optimised out
    for (const auto& fcn_e : list.m_functions) {
        auto& function = *fcn_e.second->ptr;
        if (function.m_code.m_mir) {
            function.m_code.m_mir->trans_enum_state = MIR::EnumCachePtr();
        }
    }
    for (const auto& fcn_e : list.m_functions) {
        auto& function = *fcn_e.second->ptr;
        if (function.m_code.m_mir && !function.m_code.m_mir->trans_enum_state) {
            DEBUG(fcn_e.first);
            auto* esp = new MIR::EnumCache();
            TransEnumerateFillFromMIR(*esp, *function.m_code.m_mir);
            function.m_code.m_mir->trans_enum_state = ::MIR::EnumCachePtr(esp);
        }
    }

    // Completely re-run enumeration, but this time include the TransList so MIR recursion uses the optimised versions
    EnumState state{crate};
    state.orig_list = &list;
    for (const auto& p : list.m_roots) {
        HIR::Path path = p.clone();
        MonomorphState unused_params(state.crate.m_types);
        const auto& vi = state.resolve.get_value(Span(), path, unused_params, /*signature_only=*/true);
        if (const auto* f = vi.opt_Function()) {
            TU_MATCH_HDRA( (path.m_data), {)
            default:
                break;
                TU_ARMA(Generic, e) {
                    e.m_params.m_lifetimes.resize((*f)->m_params.m_lifetimes.size());
                }
                //TU_ARMA(Generic, e) {
                //    e.m_params.m_lifetimes.resize( (*f)->m_params.m_lifetimes.size() );
                //    }
            }
        } else {
            // Statics don't have lifetime params
        }
        TransEnumerateFillFromPathMono(state, std::move(path));
    }
    auto new_list = TransEnumerateCommonPost(state);

    // Add stub entries to `new_list` for vtables and destructors, items that would be created by stages after enumerate
    // - VTables
    static RcString rcstring_drop_glue = RcString::new_interned("#drop_glue");
    for (const auto& vtp : new_list.m_vtables) {
        static Span sp;
        const auto& trait_path = vtp.first.m_data.as_UfcsKnown().trait;
        const auto& type = vtp.first.m_data.as_UfcsKnown().type;

        HIR::Path drop_glue_fn(type, rcstring_drop_glue);
        DEBUG("++ " << drop_glue_fn);
        new_list.m_functions.insert(std::make_pair(std::move(drop_glue_fn), nullptr));

        DEBUG("++ " << vtp.first);
        new_list.m_statics.insert(std::make_pair(vtp.first.clone(), nullptr));

        if (trait_path.m_path == HIR::SimplePath()) {
            // Non-data traits
            continue;
        }

        const auto& trait = crate.get_trait_by_path(sp, trait_path.m_path);

        auto monomorph_cb_trait = MonomorphStatePtr(state.crate.m_types, type, &trait_path.m_params, nullptr);
        for (unsigned int i = 0; i < trait.m_value_indexes.size(); i++) {
            // Find the corresponding vtable entry
            for (const auto& m : trait.m_value_indexes) {
                // NOTE: The "3" is the number of non-method vtable entries
                if (m.second.first != 3 + i) {
                    continue;
                }

                auto trait_gpath = monomorph_cb_trait.monomorph_genericpath(sp, m.second.second, false);
                auto item_path = ::HIR::Path(type, mv$(trait_gpath), m.first);

                DEBUG("++ " << item_path);
                new_list.m_functions.insert(std::make_pair(std::move(item_path), nullptr));

                // If the entry is a by-value function, then emit a reference to a shim
                const auto& src_trait = state.resolve.m_crate.get_trait_by_path(sp, m.second.second.m_path);
                const auto& item = src_trait.m_values.at(m.first);
                if (item.is_Function() && item.as_Function().m_receiver == HIR::Function::Receiver::Value) {
                    trait_gpath = monomorph_cb_trait.monomorph_genericpath(sp, m.second.second, false);
                    auto item_path = ::HIR::Path(type, mv$(trait_gpath), RcString::new_interned(FMT(m.first << "#ptr")));
                    DEBUG("++ " << item_path);
                    new_list.m_functions.insert(std::make_pair(std::move(item_path), nullptr));
                }
            }
        }
    }
    // - Drop Glue
    for (const auto& ty : new_list.m_types) {
        Span sp;
        // Ignore shallow types
        if (ty.second) {
            continue;
        }
        // TraitObject and Slice flag as needing drop glue... but don't actually get it generated
        if (ty.first->is_TraitObject() || ty.first->is_Slice()) {
            continue;
        }
        if (!state.resolve.type_needs_drop_glue(sp, ty.first)) {
            continue;
        }

        HIR::Path drop_glue_fn(ty.first, rcstring_drop_glue);
        DEBUG("++ " << drop_glue_fn);
        new_list.m_functions.insert(std::make_pair(std::move(drop_glue_fn), nullptr));

        if (ty.first->is_Path() && ty.first->as_Path().binding.get_trait_markings()->has_drop_impl) {
            auto fcn_path = ::HIR::Path(ty.first, state.resolve.m_lang_Drop, enumerate_rcstring_drop);
            DEBUG("++ " << fcn_path);
            new_list.m_functions.insert(std::make_pair(std::move(fcn_path), nullptr));
        }
    }
    for (const auto& ty : new_list.auto_clone_impls) {
        static RcString rcstring_clone = RcString::new_interned("clone");
        HIR::Path fn_path(ty, crate.get_lang_item_path(Span(), "clone"), rcstring_clone);
        DEBUG("++ " << fn_path);
        new_list.m_functions.insert(std::make_pair(std::move(fn_path), nullptr));
    }
    for (const auto& fn_path : new_list.trait_object_methods) {
        DEBUG("++ " << fn_path);
        new_list.m_functions.insert(std::make_pair(fn_path.clone(), nullptr));
    }
    for (const auto& ty : new_list.auto_fnptr_impls) {
        // - <fn(...) as FnPtr>::addr
        static RcString rcstring_item = RcString::new_interned("addr");
        HIR::Path fn_path(ty, crate.get_lang_item_path(Span(), "fn_ptr_trait"), rcstring_item);
        DEBUG("++ " << fn_path);
        new_list.m_functions.insert(std::make_pair(std::move(fn_path), nullptr));
    }

    remove_missing(list.m_functions, new_list.m_functions);
    remove_missing(list.m_statics, new_list.m_statics);
#endif
}

/// Common post-processing
void TransEnumerateCommonPostRun(EnumState& state) {
    // Run the enumerate queue (keeps the recursion depth down)
    while (!state.fcn_queue.empty()) {
        auto& fcn_out = *state.fcn_queue.front();
        state.fcn_queue.pop_front();

        TRACE_FUNCTION_F("Function " << ::std::find_if(state.rv.m_functions.begin(), state.rv.m_functions.end(), [&](const auto& x) {
            return x.second.get() == &fcn_out;
        })->first);

        TransEnumerateFillFromFunction(state, *fcn_out.path, *fcn_out.ptr, fcn_out.pp);
    }
}

TransList TransEnumerateCommonPost(EnumState& state) {
    TransEnumerateCommonPostRun(state);
    TransEnumerateTypes(state);

    return mv$(state.rv);
}

namespace {
    struct PtrComp {
        template <typename T>
        bool operator()(const T* lhs, const T* rhs) const {
            return *lhs < *rhs;
        }
    };

    struct TypeVisitor {
        const ::HIR::Crate& m_crate;
        ::StaticTraitResolve m_resolve;
        TransList& out;
        const TransList* prev_list;

        ::std::set<::HIR::TypeRef> active_set;

        TypeVisitor(const ::HIR::Crate& crate, TransList& out, const TransList* prev_list)
            : m_crate(crate)
            , m_resolve(crate)
            , out(out)
            , prev_list(prev_list)
        {
        }

        ~TypeVisitor() {
            DEBUG("Emitted a total of " << out.m_types.size() << " type entries");
        }

        void visit_struct(const ::HIR::GenericPath& path, const ::HIR::Struct& item) {
            static Span sp;
            ::HIR::TypeRef tmp;
            MonomorphStatePtr ms(m_crate.m_types, nullptr, &path.m_params, nullptr);
            auto monomorph = [&](const auto& x) {
                DEBUG(x);
                return m_resolve.monomorph_expand_opt(sp, tmp, x, ms);
            };
            TU_MATCHA((item.m_data), (e), (Unit, ), (Tuple, for (const auto& fld : e) { visit_type(monomorph(fld.ent)); }), (Named, for (const auto& fld : e) visit_type(monomorph(fld.ty));))
        }

        void visit_union(const ::HIR::GenericPath& path, const ::HIR::Union& item) {
            static Span sp;
            ::HIR::TypeRef tmp;
            MonomorphStatePtr ms(m_crate.m_types, nullptr, &path.m_params, nullptr);
            auto monomorph = [&](const auto& x) {
                return m_resolve.monomorph_expand_opt(sp, tmp, x, ms);
            };
            for (const auto& variant : item.m_variants) {
                visit_type(monomorph(variant.ty));
            }
        }

        void visit_enum(const ::HIR::GenericPath& path, const ::HIR::Enum& item) {
            static Span sp;
            ::HIR::TypeRef tmp;
            MonomorphStatePtr ms(m_crate.m_types, nullptr, &path.m_params, nullptr);
            auto monomorph = [&](const auto& x) {
                return m_resolve.monomorph_expand_opt(sp, tmp, x, ms);
            };
            if (const auto* e = item.m_data.opt_Data()) {
                for (const auto& variant : *e) {
                    visit_type(monomorph(variant.type));
                }
            }
        }

        enum class Mode {
            Shallow,
            Normal,
            Deep,
        };

        void visit_type(const ::HIR::TypeData* ty, Mode mode = Mode::Normal) {
            Span sp;
            // If the type has already been visited, AND either this is a shallow visit, or the previous wasn't
            if (out.has_type(ty, mode == Mode::Shallow)) {
                return;
            }
            TRACE_FUNCTION_F(ty << " - " << (mode == Mode::Shallow ? "Shallow" : (mode == Mode::Normal ? "Normal" : "Deep")));

            if (mode == Mode::Shallow) {
                TU_MATCH_HDRA( (*ty), {)
                default:
                    break;
                    TU_ARMA(Infer, te) {
                        BUG(sp, "`_` type hit in enumeration");
                    }
                    TU_ARMA(Path, te) {
                        TU_MATCHA((te.binding), (tpb), (Unbound, BUG(sp, "Unbound type hit in enumeration - " << ty);), (Opaque, BUG(sp, "Opaque type hit in enumeration - " << ty);), (ExternType, ), (Struct, ), (Union, ), (Enum, ))
                    }
                    TU_ARMA(Array, te) {
                        ASSERT_BUG(sp, te.size.is_Known(), "Encountered unknown array size - " << ty);
                    }
                    TU_ARMA(Function, te) {
                        visit_type(te.m_rettype, Mode::Shallow);
                        for (const auto& sty : te.m_arg_types) {
                            visit_type(sty, Mode::Shallow);
                        }
                    }
                    TU_ARMA(Pointer, te) {
                        visit_type(te.inner, Mode::Shallow);
                    }
                    TU_ARMA(Borrow, te) {
                        visit_type(te.inner, Mode::Shallow);
                    }
                }
            } else {
                if (active_set.find(ty) != active_set.end()) {
                    // TODO: Handle recursion
                    BUG(sp, "- Type recursion on " << ty);
                }
                active_set.insert(ty);

                TU_MATCH_HDRA( (*ty), {)
                // Impossible
                TU_ARMA(Infer, te) {
                        BUG(sp, "`_` type hit in enumeration");
                    }
                    TU_ARMA(Generic, te) {
                        BUG(sp, "Generic type hit in enumeration - " << ty);
                    }
                    TU_ARMA(ErasedType, te) {
                        //BUG(sp, "ErasedType hit in enumeration - " << ty);
                    }
                    TU_ARMA(NodeType, te) {
                        BUG(sp, "NodeType type hit in enumeration - " << ty);
                    }
                    // Nothing to do
                    TU_ARMA(Diverge, te) {
                    }
                    TU_ARMA(Primitive, te) {
                    }
                    // Recursion!
                    TU_ARMA(Path, te) {
                        TU_MATCHA(
                            (te.binding),
                            (tpb),
                            (Unbound, BUG(sp, "Unbound type hit in enumeration - " << ty);),
                            (Opaque, BUG(sp, "Opaque type hit in enumeration - " << ty);),
                            (
                                ExternType,
                                // No innards to visit
                            ),
                            (Struct, visit_struct(te.path.m_data.as_Generic(), *tpb);),
                            (Union, visit_union(te.path.m_data.as_Generic(), *tpb);),
                            (Enum,
                             // NOTE: Force repr generation before recursing into enums (allows layout optimisation to be calculated)
                             TargetGetTypeRepr(sp, m_resolve, ty);
                             visit_enum(te.path.m_data.as_Generic(), *tpb);)
                        )
                    }
                    TU_ARMA(TraitObject, te) {
                        static Span sp;

                        // If the data trait is empty, then no vtable to visit
                        if (!te.m_trait.m_path.m_path.components().empty()) {
                            // Ensure that the data trait's vtable is present
                            const auto& trait = *te.m_trait.m_trait_ptr;
                            auto vtable_ty = trait.get_vtable_type(sp, m_crate, te);

                            visit_type(vtable_ty);
                        } else {
                            // Wait, what vtable should be used then?
                        }
                    }
                    TU_ARMA(Array, te) {
                        ASSERT_BUG(sp, te.size.is_Known(), "Encountered unknown array size - " << ty);
                        visit_type(te.inner, mode);
                    }
                    TU_ARMA(Slice, te) {
                        visit_type(te.inner, mode);
                    }
                    TU_ARMA(Borrow, te) {
                        visit_type(te.inner, mode != Mode::Deep ? Mode::Shallow : Mode::Deep);
                    }
                    TU_ARMA(Pointer, te) {
                        visit_type(te.inner, mode != Mode::Deep ? Mode::Shallow : Mode::Deep);
                    }
                    TU_ARMA(Tuple, te) {
                        for (const auto& sty : te) {
                            visit_type(sty, mode);
                        }
                    }
                    TU_ARMA(NamedFunction, te) {
                    }
                    TU_ARMA(Function, te) {
                        visit_type(te.m_rettype, mode != Mode::Deep ? Mode::Shallow : Mode::Deep);
                        for (const auto& sty : te.m_arg_types) {
                            visit_type(sty, mode != Mode::Deep ? Mode::Shallow : Mode::Deep);
                        }
                    }
                }
                active_set.erase(ty);
            }

            bool shallow = (mode == Mode::Shallow);
            auto i = out.m_types.size();
            ASSERT_BUG(sp, out.add_type(ty, shallow), "Type was emitted while it was being visited: " << ty);
            DEBUG("Add type " << ty << (shallow ? " (Shallow)" : "") << " " << i);
        }

        void __attribute__((noinline)) visit_function(const ::HIR::Path& path, const ::HIR::Function& fcn, const TransParams& pp) {
            Span sp;
            auto& tv = *this;

            ::HIR::TypeRef tmp;
            std::function<const HIR::TypeData*(const HIR::TypeData*)> monomorph = [&](const HIR::TypeData* ty) -> const HIR::TypeData* {
                return pp.maybe_monomorph(m_resolve, tmp, ty);
            };
            DEBUG(fcn.m_return);
            bool has_erased = visit_ty_with(fcn.m_return, [&](const auto& x) {
                return x->is_ErasedType();
            });
            // Handle erased types in the return type.
            if (has_erased || monomorphise_type_needed(fcn.m_return)) {
                // If there's an erased type, make a copy with the erased type expanded
                ::HIR::TypeRef ret_ty;
                if (has_erased) {
                    ret_ty = clone_ty_with(m_crate.m_types, sp, fcn.m_return, [&](const auto& x, auto& out) {
                        if (const auto* te = x->opt_ErasedType()) {
                            if (const auto* e = te->m_inner.opt_Fcn()) {
                                out = fcn.m_code.m_erased_types.at(e->m_index);
                                return true;
                            }
                        }
                        return false;
                    });
                    DEBUG(ret_ty);
                    ret_ty = pp.monomorph(tv.m_resolve, ret_ty);
                } else {
                    ret_ty = pp.monomorph(tv.m_resolve, fcn.m_return);
                }
                tv.visit_type(ret_ty);
            } else {
                tv.visit_type(fcn.m_return);
            }
            for (const auto& arg : fcn.m_args) {
                DEBUG(arg.second);
                tv.visit_type(monomorph(arg.second));
            }

            const MIR::Function* mir_p = nullptr;
            if (fcn.m_code.m_mir) {
                mir_p = &*fcn.m_code.m_mir;
            }
            // If the previous list is populated, then this should be in it.
            if (prev_list) {
                const auto* trans_fcn = prev_list->find_function(path);
                ASSERT_BUG(sp, trans_fcn, "Unable to find " << path << " in first-pass enumerate result");
                if (trans_fcn && trans_fcn->monomorphised.code) {
                    mir_p = &*trans_fcn->monomorphised.code;
                    monomorph = [](const HIR::TypeData* ty) {
                        return ty;
                    };
                }
            }
            if (mir_p) {
                const MIR::Function& mir = *mir_p;
                for (const auto& ty : mir.locals) {
                    tv.visit_type(monomorph(ty));
                }

                // Find all LValue::Deref instances and get the result type
                ::MIR::TypeResolve::args_t empty_args;
                ::HIR::TypeRef empty_ty;
                ::MIR::TypeResolve mir_res(sp, tv.m_resolve, FMT_CB(fcn_path), /*ret_ty=*/empty_ty, empty_args, mir);
                for (const auto& block : mir.blocks) {
                    struct MirVisitor: public ::MIR::visit::Visitor {
                        const Span& sp;
                        TypeVisitor& tv;
                        const TransParams& pp;
                        const ::HIR::Function& fcn;
                        const ::MIR::TypeResolve& mir_res;

                        MirVisitor(const Span& sp, TypeVisitor& tv, const TransParams& pp, const ::HIR::Function& fcn, const ::MIR::TypeResolve& mir_res)
                            : sp(sp)
                            , tv(tv)
                            , pp(pp)
                            , fcn(fcn)
                            , mir_res(mir_res)
                        {
                        }

                        bool visit_lvalue(const ::MIR::LValue& lv, MIR::visit::ValUsage /*vu*/) override {
                            TRACE_FUNCTION_F(lv);
                            if (::std::none_of(lv.m_wrappers.begin(), lv.m_wrappers.end(), [](const auto& w) {
                                return w.is_Deref();
                            })) {
                                return false;
                            }
                            ::HIR::TypeRef tmp;
                            auto monomorph_outer = [&](const auto& tpl) {
                                return pp.maybe_monomorph(tv.m_resolve, tmp, tpl);
                            };
                            const ::HIR::TypeData* ty = nullptr;
                            ;
                            // Recurse, if Deref get the type and add it to the visitor
                            TU_MATCH_HDRA( (lv.m_root), {)
                            TU_ARMA(Return, e) {
                                MIR_TODO(mir_res, "Get return type for MIR type enumeration");
                        }

                        TU_ARMA(Argument, e) {
                            ty = monomorph_outer(fcn.m_args[e].second);
                        }

                        TU_ARMA(Local, e) {
                            if (&mir_res.m_fcn == &*fcn.m_code.m_mir) {
                                ty = monomorph_outer(fcn.m_code.m_mir->locals[e]);
                            } else {
                                ty = mir_res.m_fcn.locals[e];
                            }
                        }

                        TU_ARMA(Static, e) {
                            // TODO: Monomorphise the path then hand to MIR::TypeResolve?
                            const auto& path = e;
                                TU_MATCHA( (path.m_data), (pe),
                                (Generic,
                                    MIR_ASSERT(mir_res, pe.m_params.m_types.empty(), "Path params on static - " << path);
                                    const auto& s = tv.m_resolve.m_crate.get_static_by_path(mir_res.sp, pe.m_path);
                                    ty = s.m_type;
                                    ),
                                (UfcsKnown,
                                    MIR_TODO(mir_res, "LValue::Static - UfcsKnown - " << path);
                                    ),
                                (UfcsUnknown,
                                    MIR_BUG(mir_res, "Encountered UfcsUnknown in LValue::Static - " << path);
                                    ),
                                (UfcsInherent,
                                    MIR_TODO(mir_res, "LValue::Static - UfcsInherent - " << path);
                                    )
                        }
                                }
                            )
                            assert(ty);

                                for (const auto& w : lv.m_wrappers) {
                                    ty = mir_res.get_unwrapped_type(tmp, w, ty);
                                    if (w.is_Deref()) {
                                        tv.visit_type(ty);
                                    }
                                }
                                return false;
                }

                void visit_path(const HIR::Path& /*p*/) override {
                    // Paths don't need visiting?
                }
                void visit_type(const HIR::TypeData* ty) override {
                    HIR::TypeRef tmp;
                    tv.visit_type(pp.maybe_monomorph(tv.m_resolve, tmp, ty));
                }
            };
            MirVisitor mir_visit(sp, tv, pp, fcn, mir_res);
            for (const auto& stmt : block.statements) {
                DEBUG(stmt);
                mir_visit.visit_stmt(stmt);
            }
            DEBUG(block.terminator);
            mir_visit.visit_terminator(block.terminator);

            // HACK: Currently calling `caller_location` creates an empty location (so needs the type)
            if (block.terminator.is_Call() && block.terminator.as_Call().fcn.is_Intrinsic()) {
                const auto& e2 = block.terminator.as_Call().fcn.as_Intrinsic();
                if (e2.name == "caller_location") {
                    const auto& p = mir_res.m_resolve.m_crate.get_lang_item_path(sp, "panic_location");
                    const auto& s = mir_res.m_resolve.m_crate.get_struct_by_path(sp, p);
                    tv.visit_type(tv.m_crate.m_types.path(HIR::Path(p), &s));
                }
                // In 1.74+ the `offset` intrinsic takes a pointer as its generic
                else if (e2.name == "offset") {
                    HIR::TypeRef tmp;
                    const auto& ty = pp.maybe_monomorph(tv.m_resolve, tmp, e2.params.m_types.at(0));
                    tv.visit_type(ty->as_Pointer().inner);
                }
            }
            if (block.terminator.is_Call() && block.terminator.as_Call().fcn.is_Path()) {
                const auto& p = block.terminator.as_Call().fcn.as_Path();
                if (p.m_data.is_UfcsKnown()) {
                    HIR::TypeRef tmp;
                    const auto& ty = pp.maybe_monomorph(tv.m_resolve, tmp, p.m_data.as_UfcsKnown().type);
                    if (ty->is_TraitObject()) {
                        // Must have the vtable for the trait object available!
                        tv.visit_type(ty);
                    }
                }
            }
        }
    }
}
}
; // struct TypeVisitor
} // namespace <empty>

// Enumerate types required for the enumerated items
void TransEnumerateTypes(EnumState& state) {
    TRACE_FUNCTION;
    static Span sp;
    TypeVisitor tv{state.crate, state.rv, state.orig_list};

    unsigned int types_count = 0;
    bool constructors_added;
    do {
        // Visit all functions that haven't been type-visited yet
        for (unsigned int i = 0; i < state.fcns_to_type_visit.size(); i++) {
            auto* p = state.fcns_to_type_visit[i];
            assert(p->path);
            assert(p->ptr);
            auto& fcn_path = *p->path;
            const auto& fcn = *p->ptr;
            const auto& pp = p->pp;

            TRACE_FUNCTION_F("Function " << fcn_path);
            tv.visit_function(fcn_path, fcn, pp);
        }
        state.fcns_to_type_visit.clear();
        // TODO: Similarly restrict revisiting of statics.
        // - Challenging, as they're stored as a std::map
        for (const auto& ent : state.rv.m_statics) {
            TRACE_FUNCTION_F("Enumerate static " << ent.first);
            assert(ent.second->ptr);
            const auto& stat = *ent.second->ptr;
            const auto& pp = ent.second->pp;

            tv.visit_type(pp.monomorph(tv.m_resolve, stat.m_type));
        }
        // - Constants need visiting, as they will be expanded
        for (const auto& ent : state.rv.m_constants) {
            TRACE_FUNCTION_F("Enumerate constant " << ent.first);
            assert(ent.second->ptr);
            const auto& stat = *ent.second->ptr;
            const auto& pp = ent.second->pp;

            tv.visit_type(pp.monomorph(tv.m_resolve, stat.m_type));
        }
        for (const auto& ent : state.rv.m_vtables) {
            TRACE_FUNCTION_F("vtable " << ent.first);
            const auto& ty = ent.first.m_data.as_UfcsKnown().type;
            const auto& gpath = ent.first.m_data.as_UfcsKnown().trait;
            if (gpath.m_path == HIR::SimplePath()) {
                ::std::vector<HIR::TypeRef> tuple_tys;
                tuple_tys.push_back(state.crate.m_types.primitive(::HIR::CoreType::Usize));
                tuple_tys.push_back(state.crate.m_types.primitive(::HIR::CoreType::Usize));
                tuple_tys.push_back(state.crate.m_types.primitive(::HIR::CoreType::Usize)); // fn
                auto vtable_ty = state.crate.m_types.tuple(std::move(tuple_tys));
                tv.visit_type(ty);
                tv.visit_type(vtable_ty);
                continue;
            }
            const auto& trait = state.crate.get_trait_by_path(sp, gpath.m_path);

            const auto& vtable_ty_spath = trait.m_vtable_path;
            const auto& vtable_ref = state.crate.get_struct_by_path(sp, vtable_ty_spath);
            // Copy the param set from the trait in the trait object
            ::HIR::PathParams vtable_params = gpath.m_params.clone();
            // - Include associated types on bound
            for (const auto& ty_idx : trait.m_type_indexes) {
                auto idx = ty_idx.second;
                if (vtable_params.m_types.size() <= idx) {
                    vtable_params.m_types.resize(idx + 1);
                }
                auto p = ent.first.clone();
                p.m_data.as_UfcsKnown().item = ty_idx.first;
                vtable_params.m_types[idx] = state.crate.m_types.path(mv$(p), {});
                tv.m_resolve.expand_associated_types(sp, vtable_params.m_types[idx]);
            }
            DEBUG("VTable: " << vtable_ty_spath << vtable_params);

            tv.visit_type(ty);
            tv.visit_type(state.crate.m_types.path(::HIR::Path(::HIR::GenericPath(vtable_ty_spath, mv$(vtable_params))), &vtable_ref));

            // If this is for a function pointer, visit all arguments
            // - `auto_impls.cpp` will generate a vtable shim for it (which requires argument types to be fully known)
            // NOTE: Assumes that the trait is one of the Fn* traits (doesn't matter if it isn't here)
            if (const auto* te = ty->opt_Function()) {
                for (const auto& t : te->m_arg_types) {
                    tv.visit_type(t);
                }
                tv.visit_type(te->m_rettype);

                if (gpath.m_params.m_types.size() >= 1) {
                    tv.visit_type(gpath.m_params.m_types[0]);
                }
            }

            if (gpath.m_path == state.resolve.m_lang_Fn || gpath.m_path == state.resolve.m_lang_FnMut || gpath.m_path == state.resolve.m_lang_FnOnce) {
                tv.visit_type(gpath.m_params.m_types[0]);
            }
        }
        for (const auto& ty : state.rv.auto_clone_impls) {
            tv.visit_type(ty);
        }

        constructors_added = false;
        for (unsigned int i = types_count; i < state.rv.m_types.size(); i++) {
            const auto& ent = state.rv.m_types[i];
            // Shallow? Skip.
            if (ent.second) {
                continue;
            }
            const auto& ty = ent.first;
            TRACE_FUNCTION_F(ty);
            if (ty->is_Path()) {
                const auto& te = ty->as_Path();
                ASSERT_BUG(sp, te.path.m_data.is_Generic(), "Non-Generic type path after enumeration - " << ty);
                const auto& gp = te.path.m_data.as_Generic();
                const ::HIR::TraitMarkings* markings_ptr = te.binding.get_trait_markings();
                ASSERT_BUG(sp, markings_ptr, "Path binding not set correctly - " << ty);

                // If the type has a drop impl, and it's either defined in this crate or has params (and thus was monomorphised)
                if (markings_ptr->has_drop_impl && (gp.m_path.crate_name() == state.crate.m_crate_name || gp.m_params.has_params())) {
                    // Add the Drop impl to the codegen list
                    TransEnumerateFillFromPathMono(state, ::HIR::Path(ty, state.crate.get_lang_item_path(sp, "drop"), enumerate_rcstring_drop, HIR::PathParams(HIR::LifetimeRef())));
                    constructors_added = true;
                }
            }

            if (const auto* ity = tv.m_resolve.is_type_owned_box(ty)) {
                // NOTE: Save the params before visiting, as the TypeRef might move as types are added, but the inner data won't move
                const auto& p = ty->as_Path().path.m_data.as_Generic().m_params;
                tv.visit_type(ity);

            }
        }
        types_count = state.rv.m_types.size();

        // Run queue
        TransEnumerateCommonPostRun(state);
    } while (constructors_added);
}

namespace {
    TAGGED_UNION(EntPtr, NotFound, (NotFound, struct {}), (AutoGenerate, struct {}), (Function, const ::HIR::Function*), (Static, const ::HIR::Static*), (Constant, const ::HIR::Constant*));

    bool path_already_enumerated(const EnumState& state, const ::HIR::Path& path) {
        return state.rv.m_functions.count(path) || state.rv.m_statics.count(path) || state.rv.m_constants.count(path) || state.rv.m_vtables.count(path);
    }

    void evaluate_translation_params(const Span& sp, const ::HIR::Crate& crate, const ::HIR::GenericParams* defs, ::HIR::PathParams& params) {
        if (params.m_values.empty()) {
            return;
        }

        ASSERT_BUG(sp, defs, "Missing const parameter definitions for " << params);
        ASSERT_BUG(sp, params.m_values.size() <= defs->m_values.size(), "Too many const parameters in " << params << " for " << defs->fmt_args());
        for (size_t i = 0; i < params.m_values.size(); i++) {
            auto& value = params.m_values[i];
            if (value.is_Unevaluated()) {
                const auto& type = defs->m_values[i].m_type;
                ASSERT_BUG(sp, !monomorphise_type_needed(type), "Generic const parameter type " << type << " in " << defs->fmt_args());
                ConvertHIRConstantEvaluateConstGeneric(sp, crate, type, value);
            }
            ASSERT_BUG(sp, value.is_Evaluated(), "Const parameter was not concrete at translation: " << value);
        }
    }

    void evaluate_translation_impl_and_trait_params(const Span& sp, const ::HIR::Crate& crate, ::HIR::Path& path, TransParams& pp) {
        evaluate_translation_params(sp, crate, pp.gdef_impl, pp.pp_impl);

        TU_MATCH_HDRA((path.m_data), {)
        TU_ARMA(Generic, _pe) {
            }
            TU_ARMA(UfcsKnown, pe) {
                // An empty trait path is the marker-only vtable sentinel. It
                // has no trait parameters to evaluate; the vtable enumerator
                // handles this representation directly.
                if (pe.trait.m_path != HIR::SimplePath()) {
                    const auto& trait = crate.get_trait_by_path(sp, pe.trait.m_path);
                    evaluate_translation_params(sp, crate, &trait.m_params, pe.trait.m_params);
                }
            }
            TU_ARMA(UfcsInherent, pe) {
                evaluate_translation_params(sp, crate, pp.gdef_impl, pe.impl_params);
            }
            TU_ARMA(UfcsUnknown, _pe) {
                BUG(sp, "UfcsUnknown at translation: " << path);
            }
        }
    }

    void evaluate_translation_item_params(const Span& sp, const ::HIR::Crate& crate, const ::HIR::GenericParams& defs, ::HIR::Path& path, TransParams& pp) {
        evaluate_translation_params(sp, crate, &defs, pp.pp_method);

        TU_MATCH_HDRA((path.m_data), {)
        TU_ARMA(Generic, pe) {
                evaluate_translation_params(sp, crate, &defs, pe.m_params);
            }
            TU_ARMA(UfcsKnown, pe) {
                evaluate_translation_params(sp, crate, &defs, pe.params);
            }
            TU_ARMA(UfcsInherent, pe) {
                evaluate_translation_params(sp, crate, &defs, pe.params);
            }
            TU_ARMA(UfcsUnknown, _pe) {
                BUG(sp, "UfcsUnknown at translation: " << path);
            }
        }
    }

    EntPtr get_ent_fullpath(const Span& sp, const ::HIR::Crate& crate, const ::HIR::Path& path, TransParams& params) {
        TRACE_FUNCTION_F(path);
        StaticTraitResolve resolve{crate};

        if (path.m_data.is_UfcsInherent() && path.m_data.as_UfcsInherent().item == "#type_id") {
            return EntPtr::make_AutoGenerate({});
        }

        MonomorphState ms(crate.m_types);
        params.gdef_impl = nullptr;
        auto ent = resolve.get_value(sp, path, ms, /*signature_only=*/false, &params.gdef_impl);
        if (ms.get_impl_params()) {
            params.pp_impl = ms.get_impl_params()->clone();
            if (params.pp_impl.has_params()) {
                assert(params.gdef_impl);
            }
        }
        DEBUG(path << " = " << ent.tag_str() << " w/ impl" << params.pp_impl);
        TU_MATCH_HDRA( (ent), {)
        default:
            TODO(sp, path << " was " << ent.tag_str());
            TU_ARMA(NotYetKnown, _e) {
                const auto* pe = &path.m_data.as_UfcsKnown();
                // Options:
                // - VTable
                if (pe->item == "vtable#") {
                    DEBUG("VTable, quick return");
                    return EntPtr::make_AutoGenerate({});
                }
                // - Auto-generated impl (the only trait impl was a bound)
                //  > Need to check if the trait is impled bounded
                bool found_bound = false;
                bool found_impl = false;
                resolve.find_impl(sp, pe->trait.m_path, pe->trait.m_params, pe->type, [&](auto impl_ref, auto is_fuzz) -> bool {
                    DEBUG("[get_ent_fullpath] Found " << impl_ref);
                    if (impl_ref.m_data.is_TraitImpl()) {
                        found_impl = true;
                    } else {
                        found_bound = true;
                    }
                    return false;
                });
                if (found_bound) {
                    return EntPtr::make_AutoGenerate({});
                }
                DEBUG("NotYetKnown -> NotFound");
                return EntPtr();
            }
            TU_ARMA(Function, f) {
                // Check for trait provided bodies
                // - They need a little hack to ensure that monomorph is run
                if (const auto* pe = path.m_data.opt_UfcsKnown()) {
                    const auto& trait_ref = crate.get_trait_by_path(sp, pe->trait.m_path);
                    const auto& trait_vi = trait_ref.m_values.at(pe->item);

                    if (f == &trait_vi.as_Function()) {
                        DEBUG("Default trait body");
                        params.force_monomorphisation = true;
                    }
                }
                return EntPtr{f};
            }
            TU_ARMA(Static, f) {
                return EntPtr{f};
            }
            TU_ARMA(Constant, f) {
                return EntPtr{f};
            }
            TU_ARMA(StructConstructor, _) {
                return EntPtr::make_AutoGenerate({});
            }
            TU_ARMA(EnumConstructor, _) {
                return EntPtr::make_AutoGenerate({});
            }
        }
        throw "";
    }
}

void TransEnumerateFillFromPath(EnumState& state, const ::HIR::Path& path, const TransParams& pp) {
    auto path_mono = pp.monomorph(state.resolve, path);
    TransEnumerateFillFromPathMono(state, mv$(path_mono));
}

void TransEnumerateFillFromPathMono(EnumState& state, ::HIR::Path path_mono) {
    Span sp;
    bind_translation_nominals(state.crate, path_mono);
    TRACE_FUNCTION_F(path_mono);
    // Don't want duplicates of lifetime-generic items
    ASSERT_BUG(sp, !monomorphise_path_needed(path_mono, /*ignore_lifetimes=*/false), "Path " << path_mono << " is generic");
    // TODO: If already in the list, return early
    if (path_already_enumerated(state, path_mono)) {
        DEBUG("> Already enumerated");
        return;
    }

    TransParams sub_pp(state.crate.m_types, sp);
    TU_MATCH_HDRA( (path_mono.m_data), { )
    TU_ARMA(Generic, pe) {
            sub_pp.pp_method = pe.m_params.clone();
        }
        TU_ARMA(UfcsKnown, pe) {
            sub_pp.pp_method = pe.params.clone();
            sub_pp.self_type = pe.type;
        }
        TU_ARMA(UfcsInherent, pe) {
            sub_pp.pp_method = pe.params.clone();
            sub_pp.pp_impl = pe.impl_params.clone();
            sub_pp.self_type = pe.type;
        }
        TU_ARMA(UfcsUnknown, pe) {
            BUG(sp, "UfcsUnknown - " << path_mono);
        }
    }
    // Get the item type
    // - Valid types are Function and Static
    auto item_ref = get_ent_fullpath(sp, state.crate, path_mono, sub_pp);
    DEBUG("item_ref.tag_str() = " << item_ref.tag_str());
    DEBUG("sub_pp.pp_method = " << sub_pp.pp_method);
    DEBUG("sub_pp.pp_impl = " << sub_pp.pp_impl);
    evaluate_translation_impl_and_trait_params(sp, state.crate, path_mono, sub_pp);
    TU_MATCH_HDRA( (item_ref), {)
    TU_ARMA(NotFound, e) {
            BUG(sp, "Item not found for " << path_mono);
        }
        TU_ARMA(AutoGenerate, e) {
            if (path_already_enumerated(state, path_mono)) {
                DEBUG("> Already enumerated after const evaluation");
                return;
            }
            if (path_mono.m_data.is_Generic()) {
                // Leave generation of struct/enum constructors to codgen
                // TODO: Add to a list of required constructors
                state.rv.m_constructors.insert(mv$(path_mono.m_data.as_Generic()));
            }
            // - <T>::#type_id
            else if (path_mono.m_data.is_UfcsInherent() && path_mono.m_data.as_UfcsInherent().item == "#type_id") {
                state.rv.m_typeids.insert(path_mono.m_data.as_UfcsInherent().type);
            }
            // - <T as U>::#vtable
            else if (path_mono.m_data.is_UfcsKnown() && path_mono.m_data.as_UfcsKnown().item == "vtable#") {
                if (state.rv.add_vtable(path_mono.clone(), TransParams(state.crate.m_types))) {
                    // Fill from the vtable
                    TransEnumerateFillFromVTable(state, mv$(path_mono), sub_pp);
                }
            }
            // - <(Trait) as Trait>::method
            else if (path_mono.m_data.is_UfcsKnown() && path_mono.m_data.as_UfcsKnown().type->is_TraitObject()) {
                state.rv.trait_object_methods.insert(mv$(path_mono));
            }
            // - <fn(...) as Fn*>::call*
            else if (path_mono.m_data.is_UfcsKnown() && path_mono.m_data.as_UfcsKnown().type->is_Function() && (path_mono.m_data.as_UfcsKnown().trait.m_path == state.crate.get_lang_item_path_opt("fn") || path_mono.m_data.as_UfcsKnown().trait.m_path == state.crate.get_lang_item_path_opt("fn_mut") || path_mono.m_data.as_UfcsKnown().trait.m_path == state.crate.get_lang_item_path_opt("fn_once"))) {
                // Must have been a dynamic dispatch request, just leave as-is
                // - However, ensure that all arguments are visited?
                //const auto& fcn_ty = path_mono.m_data.as_UfcsKnown().type->as_Function();
                //for(const auto& ty : fcn_ty.m_arg_types)
                //    state.rv.vi
            }
            // - <fn{...} as Fn*>::call*
            else if (path_mono.m_data.is_UfcsKnown() && path_mono.m_data.as_UfcsKnown().type->is_NamedFunction() && (path_mono.m_data.as_UfcsKnown().trait.m_path == state.crate.get_lang_item_path_opt("fn") || path_mono.m_data.as_UfcsKnown().trait.m_path == state.crate.get_lang_item_path_opt("fn_mut") || path_mono.m_data.as_UfcsKnown().trait.m_path == state.crate.get_lang_item_path_opt("fn_once"))) {
                // Calling a non-dynamic function, need to visit that function
                TransEnumerateFillFromPath(state, path_mono.m_data.as_UfcsKnown().type->as_NamedFunction().path, sub_pp);
            }
            // - <fn(...) as FnPtr>::addr
            else if (path_mono.m_data.is_UfcsKnown() && path_mono.m_data.as_UfcsKnown().type->is_Function() && path_mono.m_data.as_UfcsKnown().trait.m_path == state.crate.get_lang_item_path_opt("fn_ptr_trait")) {
                state.rv.auto_fnptr_impls.insert(path_mono.m_data.as_UfcsKnown().type);
            }
            // <* as Clone>::clone
            else if (path_mono.m_data.is_UfcsKnown() && path_mono.m_data.as_UfcsKnown().trait == state.crate.get_lang_item_path_opt("clone")) {
                const auto& pe = path_mono.m_data.as_UfcsKnown();
                ASSERT_BUG(sp, pe.item == "clone" || pe.item == "clone_from", "Unexpected Clone method called, " << path_mono);
                const auto& inner_ty = pe.type;
                // If this is !Copy, then we need to ensure that the inner type's clone impls are also available
                ::StaticTraitResolve resolve{state.crate};
                if (!resolve.type_is_copy(sp, inner_ty)) {
                    auto enum_impl = [&](const ::HIR::TypeData* ity) {
                        if (!resolve.type_is_copy(sp, ity)) {
                            auto inner_pp = HIR::PathParams(HIR::LifetimeRef());
                            if (pe.item == "clone_from") {
                                inner_pp.m_lifetimes.push_back(HIR::LifetimeRef());
                            }
                            TransEnumerateFillFromPathMono(state, ::HIR::Path(ity, pe.trait.clone(), pe.item, mv$(inner_pp)));
                        }
                    };
                    if (const auto* te = inner_ty->opt_Tuple()) {
                        for (const auto& ity : *te) {
                            enum_impl(ity);
                        }
                    } else if (const auto* te = inner_ty->opt_Array()) {
                        enum_impl(te->inner);
                    } else if (TU_TEST1(*inner_ty, Path, .is_closure())) {
                        const auto& gp = inner_ty->as_Path().path.m_data.as_Generic();
                        const auto& str = state.crate.get_struct_by_path(sp, gp.m_path);
                        auto p = TransParams::new_impl(state.crate.m_types, sp, {}, gp.m_params.clone());
                        for (const auto& fld : str.m_data.as_Tuple()) {
                            ::HIR::TypeRef tmp;
                            const auto& ty_m = monomorphise_type_needed(fld.ent) ? (tmp = p.monomorph(resolve, fld.ent)) : fld.ent;
                            enum_impl(ty_m);
                        }
                    } else {
                        BUG(sp, "Unhandled magic clone in enumerate - " << inner_ty);
                    }
                }
                // Add this type to a list of types that will have the impl auto-generated
                state.rv.auto_clone_impls.insert(inner_ty);
            } else {
                BUG(sp, "AutoGenerate returned for unknown path type - " << path_mono);
            }
        }
        TU_ARMA(Function, e) {
            evaluate_translation_item_params(sp, state.crate, e->m_params, path_mono, sub_pp);
            if (path_already_enumerated(state, path_mono)) {
                DEBUG("> Already enumerated after const evaluation");
                return;
            }
            // Add this path (monomorphised) to the queue
            state.enum_fcn(mv$(path_mono), *e, mv$(sub_pp));
        }
        TU_ARMA(Static, e) {
            evaluate_translation_item_params(sp, state.crate, e->m_params, path_mono, sub_pp);
            if (path_already_enumerated(state, path_mono)) {
                DEBUG("> Already enumerated after const evaluation");
                return;
            }
            if (auto* ptr = state.rv.add_static(state.crate.m_types, mv$(path_mono))) {
                TransEnumerateFillFromStatic(state, *e, *ptr, mv$(sub_pp));
            }
        }
        TU_ARMA(Constant, e) {
            evaluate_translation_item_params(sp, state.crate, e->m_params, path_mono, sub_pp);
            if (path_already_enumerated(state, path_mono)) {
                DEBUG("> Already enumerated after const evaluation");
                return;
            }
            switch (e->m_value_state) {
                case HIR::Constant::ValueState::Unknown:
                    BUG(sp, "Unevaluated constant: " << path_mono);
                case HIR::Constant::ValueState::Generic:
                    if (auto* slot = state.rv.add_const(state.crate.m_types, mv$(path_mono))) {
                        MIR::EnumCache es;
                        TransEnumerateFillFromMIR(es, *e->m_value.m_mir);
                        es.apply(state, sub_pp);
                        slot->ptr = e;
                        slot->pp = ::std::move(sub_pp);
                    }
                    break;
                case HIR::Constant::ValueState::Known:
                    TransEnumerateFillFromLiteral(state, e->m_value_res, sub_pp);
                    break;
            }
        }
    }
}

void TransEnumerateFillFromMIRLValue(MIR::EnumCache& state, const ::MIR::LValue& lv) {
    if (lv.m_root.is_Static()) {
        state.insert_path(lv.m_root.as_Static());
    }
}

void TransEnumerateFillFromMIRConstant(MIR::EnumCache& state, const ::MIR::Constant& c) {
    TU_MATCHA(
        (c),
        (ce),
        (Int, ),
        (Uint, ),
        (Float, ),
        (Bool, ),
        (Bytes, ),
        (StaticString, ), // String
        (Const,
         // - Check if this constant has a value of Defer
         state.insert_path(*ce.p);),
        (Generic, ),
        (Function, state.insert_path(*ce.p);),
        (ItemAddr, if (ce) state.insert_path(*ce);)
    )
}

void TransEnumerateFillFromMIRParam(MIR::EnumCache& state, const ::MIR::Param& p) {
    TU_MATCHA((p), (e), (LValue, TransEnumerateFillFromMIRLValue(state, e);), (Borrow, TransEnumerateFillFromMIRLValue(state, e.val);), (Constant, TransEnumerateFillFromMIRConstant(state, e);))
}

void TransEnumerateFillFromMIR(MIR::EnumCache& state, const ::MIR::Function& code) {
    TRACE_FUNCTION_F("");
    for (const auto& ty : code.locals) {
        visit_ty_with(ty, [&state](const HIR::TypeData* t) -> bool {
            if (const auto* te = t->opt_NamedFunction()) {
                state.insert_path(te->path);
            }
            return false;
        });
    }
    for (const auto& bb : code.blocks) {
        for (const auto& stmt : bb.statements) {
            TU_MATCH_HDRA((stmt), {)
            TU_ARMA(Assign, se) {
                    DEBUG("- " << se.dst << " = " << se.src);
                    TransEnumerateFillFromMIRLValue(state, se.dst);
                    TU_MATCHA((se.src), (e), (Use, TransEnumerateFillFromMIRLValue(state, e);), (Constant, TransEnumerateFillFromMIRConstant(state, e);), (SizedArray, TransEnumerateFillFromMIRParam(state, e.val);), (Borrow, TransEnumerateFillFromMIRLValue(state, e.val);), (Cast, TransEnumerateFillFromMIRLValue(state, e.val);), (BinOp, TransEnumerateFillFromMIRParam(state, e.val_l); TransEnumerateFillFromMIRParam(state, e.val_r);), (UniOp, TransEnumerateFillFromMIRLValue(state, e.val);), (DstMeta, TransEnumerateFillFromMIRLValue(state, e.val);), (DstPtr, TransEnumerateFillFromMIRLValue(state, e.val);), (MakeDst, TransEnumerateFillFromMIRParam(state, e.ptr_val); TransEnumerateFillFromMIRParam(state, e.meta_val);), (Tuple, for (const auto& val : e.vals) TransEnumerateFillFromMIRParam(state, val);), (Array, for (const auto& val : e.vals) TransEnumerateFillFromMIRParam(state, val);), (UnionVariant, TransEnumerateFillFromMIRParam(state, e.val);), (EnumVariant, for (const auto& val : e.vals) TransEnumerateFillFromMIRParam(state, val);), (Struct, for (const auto& val : e.vals) TransEnumerateFillFromMIRParam(state, val);))
                }
                TU_ARMA(Asm2, e) {
                    for (auto& p : e.params) {
                    TU_MATCH_HDRA( (p), { )
                    TU_ARMA(Const, v) TransEnumerateFillFromMIRConstant(state, v);
                            TU_ARMA(Sym, v) state.insert_path(v);
                            TU_ARMA(Reg, v) {
                                if (v.input) {
                                    TransEnumerateFillFromMIRParam(state, *v.input);
                                }
                                if (v.output) {
                                    TransEnumerateFillFromMIRLValue(state, *v.output);
                                }
                            }
                    }
                    }
                }
                TU_ARMA(Asm, se) {
                    DEBUG("- llvm_asm! ...");
                    for (const auto& v : se.inputs) {
                        TransEnumerateFillFromMIRLValue(state, v.second);
                    }
                    for (const auto& v : se.outputs) {
                        TransEnumerateFillFromMIRLValue(state, v.second);
                    }
                }
                TU_ARMA(SetDropFlag, se) {
                }
                TU_ARMA(SaveDropFlag, se) {
                    TransEnumerateFillFromMIRLValue(state, se.slot);
                }
                TU_ARMA(LoadDropFlag, se) {
                    TransEnumerateFillFromMIRLValue(state, se.slot);
                }
                TU_ARMA(ScopeEnd, se) {
                }
            }
        }
        DEBUG("> " << bb.terminator);
        TU_MATCHA((bb.terminator), (e), (Incomplete, ), (Return, ), (UnwindResume, ), (UnwindTerminate, ), (Unreachable, ), (Goto, ), (If, TransEnumerateFillFromMIRLValue(state, e.cond);), (Switch, TransEnumerateFillFromMIRLValue(state, e.val);), (SwitchValue, TransEnumerateFillFromMIRLValue(state, e.val);), (Drop, TransEnumerateFillFromMIRLValue(state, e.slot);), (Call, TransEnumerateFillFromMIRLValue(state, e.ret_val); TU_MATCHA((e.fcn), (e2), (Value, TransEnumerateFillFromMIRLValue(state, e2);), (Path, state.insert_path(e2);), (Intrinsic, if (e2.name == "type_id") {
                                                                                                                                                                                                                                                                                                                                                                              // Add <T>::#type_id to the enumerate list
                                                                                                                                                                                                                                                                                                                                                                              state.insert_typeid(e2.params.m_types.at(0));
                                                                                                                                                                                                                                                                                                                                                                          })) for (const auto& arg : e.args) TransEnumerateFillFromMIRParam(state, arg);))
    }
}

void TransEnumerateFillFromVTable(EnumState& state, ::HIR::Path vtable_path, const TransParams& pp) {
    static Span sp;
    const auto& type = vtable_path.m_data.as_UfcsKnown().type;
    const auto& trait_path = vtable_path.m_data.as_UfcsKnown().trait;
    if (trait_path == HIR::SimplePath()) {
        // TODO: Ensure that the drop glue is available
        return;
    }
    const auto& tr = state.crate.get_trait_by_path(Span(), trait_path.m_path);

    ASSERT_BUG(sp, !type->is_Slice(), "Getting vtable for unsized type - " << vtable_path);
    ASSERT_BUG(sp, !type->is_TraitObject(), "Getting vtable for unsized type - " << vtable_path);

    auto monomorph_cb_trait = MonomorphStatePtr(state.crate.m_types, type, &trait_path.m_params, nullptr);
    for (const auto& m : tr.m_value_indexes) {
        DEBUG("- " << m.second.first << " = " << m.second.second << " :: " << m.first);
        auto gpath = monomorph_cb_trait.monomorph_genericpath(sp, m.second.second, false);
        const auto& fcn = state.crate.get_trait_by_path(sp, gpath.m_path).m_values.at(m.first).as_Function();
        TransEnumerateFillFromPathMono(state, ::HIR::Path(type, mv$(gpath), m.first, fcn.m_params.make_empty_params(true)));
    }
    for (const auto& pt_path : tr.m_all_parent_traits) {
        ASSERT_BUG(sp, pt_path.m_trait_ptr, "Unset trait pointer - " << pt_path);
        const auto& pt = *pt_path.m_trait_ptr;
        if (pt.m_vtable_path != HIR::SimplePath()) {
            auto pt_mono = MonomorphStatePtr(state.crate.m_types, nullptr, &trait_path.m_params, nullptr).monomorph_genericpath(sp, pt_path.m_path);
            auto pt_vtable_path = ::HIR::Path(type, mv$(pt_mono), vtable_path.m_data.as_UfcsKnown().item);
            state.rv.add_vtable(mv$(pt_vtable_path), TransParams(state.crate.m_types));
            // No need to recurse.
        }
    }
}

void TransEnumerateFillFromLiteral(EnumState& state, const EncodedLiteral& lit, const TransParams& pp) {
    for (const auto& r : lit.relocations) {
        if (r.p) {
            // TODO: Replace lifetimes
            TransEnumerateFillFromPath(state, *r.p, pp);
        }
    }
}

void TransEnumerateFillFromFunction(EnumState& state, const HIR::Path& p, const ::HIR::Function& function, const TransParams& pp) {
    TRACE_FUNCTION_F("Function " << p << " pp=" << pp.pp_impl << " + " << pp.pp_method);
    if (!function.m_code.m_mir) {
        // External.
        if (function.m_linkage.name != "") {
            // Search for a function with the same linkage name anywhere in the loaded crates
            auto it = state.m_link_functions.find(function.m_linkage.name);
            if (it != state.m_link_functions.end()) {
                state.enum_fcn(::HIR::Path(it->second.first), *it->second.second, TransParams(state.crate.m_types, pp.sp));
            }
        }
    } else if (state.orig_list) {
        const auto* trans_fcn = state.orig_list->find_function(p);
        if (trans_fcn) {
            if (trans_fcn->monomorphised.code) {
                DEBUG("Monomorphised");
                MIR::EnumCache ec;
                TransEnumerateFillFromMIR(ec, *trans_fcn->monomorphised.code);
                ec.apply(state, pp);
            } else if (trans_fcn->ptr->m_code.m_mir) {
                DEBUG("Concrete");
                MIR::EnumCache ec;
                TransEnumerateFillFromMIR(ec, *trans_fcn->ptr->m_code.m_mir);
                ec.apply(state, pp);
            } else {
                DEBUG("No code");
            }
        } else {
            ASSERT_BUG(Span(), trans_fcn, "Missing " << p << " in input TransList?");
        }
    } else {
        const auto& mir_fcn = *function.m_code.m_mir;
        if (!mir_fcn.trans_enum_state) {
            auto* esp = new MIR::EnumCache();
            TransEnumerateFillFromMIR(*esp, *function.m_code.m_mir);
            mir_fcn.trans_enum_state = ::MIR::EnumCachePtr(esp);
        }
        // TODO: Ensure that all types have drop glue generated too? (Iirc this is unconditional currently)
        mir_fcn.trans_enum_state->apply(state, pp);
    }
}

void TransEnumerateFillFromStatic(EnumState& state, const ::HIR::Static& item, TransListStatic& out_stat, TransParams pp) {
    // HACK: Ensure that lifetimes are populated.
    pp.pp_method.m_lifetimes.resize(item.m_params.m_lifetimes.size());

    if (item.m_params.is_generic()) {
        MIR::EnumCache es;
        TransEnumerateFillFromMIR(es, *item.m_value.m_mir);
        es.apply(state, pp);
    } else if (item.m_type->is_Infer()) {
        BUG(Span(), "Enumerating static with no assigned type (unused elevated literal)");
    } else if (item.m_value_generated) {
        TransEnumerateFillFromLiteral(state, item.m_value_res, pp);
    }
    out_stat.ptr = &item;
    out_stat.pp = mv$(pp);
}
