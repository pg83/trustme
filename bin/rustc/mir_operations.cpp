#include "mir_operations.h"

#include "mir_main_bindings.h"
#include "mir_mir.h"
#include "hir_visitor.h"
#include "hir_typeck_static.h"
#include "mir_helpers.h"
#include "mir_operations.h"
#include "mir_visit_crate_mir.h"
#include <algorithm>
#include <iomanip>

namespace {
    /// Value states
    enum class ValState {
        Uninit,   // No value written yet
        FullInit, // Value written, can be read
        Shared,   // immutably borrowed
        Frozen,   // mutably borrowed
    };

    /// Borrow status for a single variable
    struct VarState {
        /// State bits, see `ValState`
        unsigned state : 2;
        /// Index into `FcnState.inner` for the _first_ partially-borrowed for this variable
        unsigned partial_idx : 14; // If non-zero, it's a partial init/borrow

        VarState()
            : state(static_cast<unsigned>(ValState::Uninit))
            , partial_idx(0)
        {
        }

        VarState(ValState vs)
            : state(static_cast<unsigned>(vs))
            , partial_idx(0)
        {
        }
    };

    struct FcnState {
        VarState retval;
        std::vector<VarState> args;
        std::vector<VarState> locals;
        /// Mutable state flags for statics
        std::map<HIR::Path, VarState> static_mut;

        std::vector<VarState> inner;

        FcnState(size_t n_args, size_t n_locals)
            : retval()
            , args(n_args)
            , locals(n_locals)
        {
        }

        void check_inner_state(const ::MIR::TypeResolve& state, const MIR::LValue& lv, std::function<bool(ValState vs)> cb) const {
            const auto& val_state = get_state(state, lv);
            if (val_state.partial_idx != 0) {
                // Recurse into all inner entries
            } else {
                if (!cb(static_cast<ValState>(val_state.state))) {
                    // Error!
                    //MIR_BUG(state, "Borrow check failure: ");
                }
            }
        }

        const VarState& get_state_root(const MIR::LValue::Storage& lv_root) const {
            TU_MATCH_HDRA( (lv_root), {)
            TU_ARMA(Return, e)
                return retval;
                TU_ARMA(Local, e)
                return locals.at(e);
                TU_ARMA(Argument, e)
                return args.at(e);
                TU_ARMA(Static, e) {
                    // TODO: If it's a static mut, return ValState::FullInit?
                    static const VarState vs_static = VarState(ValState::Shared);
                    return vs_static;
                }
            }
            throw "";
        }

        VarState& get_state_root_mut(const MIR::LValue::Storage& lv_root) {
            TU_MATCH_HDRA( (lv_root), {)
            TU_ARMA(Return, e)
                return retval;
                TU_ARMA(Local, e)
                return locals.at(e);
                TU_ARMA(Argument, e)
                return args.at(e);
                TU_ARMA(Static, e) {
                    // TODO: If it's a static mut, return ValState::FullInit?
                    auto it = static_mut.find(e);
                    if (it == static_mut.end()) {
                        it = static_mut.insert(::std::make_pair(e.clone(), VarState(ValState::FullInit))).first;
                    }
                    return it->second;
                }
            }
            throw "";
        }

        const VarState& get_state(const ::MIR::TypeResolve& state, const MIR::LValue& lv) const {
            const VarState* rv = &this->get_state_root(lv.m_root);
            for (const auto& w : lv.m_wrappers) {
                if (rv->partial_idx == 0) {
                    break;
                }
                TU_MATCH_HDRA( (w), {)
                TU_ARMA(Deref, e) {
                        MIR_TODO(state, "get_state - Deref");
                    }
                    TU_ARMA(Field, e) {
                        MIR_TODO(state, "get_state - Field");
                    }
                    TU_ARMA(Downcast, e) {
                        MIR_TODO(state, "get_state - Variant");
                    }
                    TU_ARMA(Index, e) {
                        return *rv;
                    }
                }
            }
            return *rv;
        }

        VarState* get_state_mut(const ::MIR::TypeResolve& state, const MIR::LValue& lv, bool allow_parent) {
            VarState* rv = &this->get_state_root_mut(lv.m_root);
            for (const auto& w : lv.m_wrappers) {
                TU_MATCH_HDRA( (w), {)
                TU_ARMA(Deref, e) {
                        MIR_TODO(state, "get_state_mut - Deref");
                    }
                    TU_ARMA(Field, e) {
                        MIR_TODO(state, "get_state_mut - Field");
                    }
                    TU_ARMA(Downcast, e) {
                        MIR_TODO(state, "get_state_mut - Variant");
                    }
                    TU_ARMA(Index, e) {
                        if (allow_parent) {
                            return rv;
                        }
                        return nullptr;
                    }
                }
            }
            return rv;
        }

        void set_state(const ::MIR::TypeResolve& state, const MIR::LValue& lv, ValState target) {
            VarState* rv = &this->get_state_root_mut(lv.m_root);
            for (const auto& w : lv.m_wrappers) {
                TU_MATCH_HDRA( (w), {)
                TU_ARMA(Deref, e) {
                        // Can't set to `Uninit` through a deref
                        MIR_ASSERT(state, target != ValState::Uninit, "Attempting to move out of borrow");
                    }
                    TU_ARMA(Field, e) {
                    }
                    TU_ARMA(Downcast, e) {
                    }
                    TU_ARMA(Index, e) {
                        // Can't set to `Uninit` through an index
                        MIR_ASSERT(state, target != ValState::Uninit, "Attempting to move through indexing");
                    }
                }
            }
            size_t i;
            for (i = 0; i < lv.m_wrappers.size(); i++) {
                const auto& w = lv.m_wrappers[i];
                TU_MATCH_HDRA( (w), {)
                TU_ARMA(Deref, e) {
                        // Doesn't consume an inner, but stops the lookup?
                        // - Could track borrows through a deref, but can't track/allow moves
                        break;
                    }
                    TU_ARMA(Field, e) {
                        // Since partial is set, look it up
                        //if( !rv->has_partial() ) {
                        //    rv->partial_idx = this->allocate_partial(state, *ty_p);
                        //}
                        rv = &this->inner[static_cast<size_t>(rv->partial_idx) + e];
                    }
                    TU_ARMA(Downcast, e) {
                        // Doesn't consume an inner
                    }
                    TU_ARMA(Index, e) {
                        // Doesn't consume an inner, but stops the lookup
                        break;
                    }
                }
            }
            while (i--) {
                switch (static_cast<ValState>(rv->state)) {
                    case ValState::Uninit:
                    case ValState::FullInit:
                        rv->state = static_cast<unsigned>(target);
                        break;
                    case ValState::Shared:
                        rv->state = static_cast<unsigned>(target);
                        break;
                    case ValState::Frozen:
                        rv->state = static_cast<unsigned>(target);
                        break;
                }
            }
        }

        void move_lvalue(const ::MIR::TypeResolve& state, const MIR::LValue& lv) {
            // Must be `init` (or if Copy, `Shared`)
            if (state.lvalue_is_copy(lv)) {
                check_inner_state(state, lv, [&](const ValState vs) {
                    return vs == ValState::FullInit || vs == ValState::Shared;
                });
            } else {
                check_inner_state(state, lv, [&](const ValState vs) {
                    return vs == ValState::FullInit;
                });
                set_state(state, lv, ValState::Uninit);
            }
        }

        void write_lvalue(const ::MIR::TypeResolve& state, const MIR::LValue& lv) {
            check_inner_state(state, lv, [&](const ValState vs) {
                return vs != ValState::Shared || vs != ValState::Frozen;
            });
            set_state(state, lv, ValState::FullInit);
        }

        void borrow_lvalue(const ::MIR::TypeResolve& state, ::HIR::BorrowType bt, const MIR::LValue& lv) {
            switch (bt) {
                case ::HIR::BorrowType::Owned:
                case ::HIR::BorrowType::Unique:
                    check_inner_state(state, lv, [&](const ValState vs) {
                        return vs == ValState::FullInit;
                    });
                    set_state(state, lv, ValState::Frozen);
                    break;
                case ::HIR::BorrowType::Shared:
                    check_inner_state(state, lv, [&](const ValState vs) {
                        return vs == ValState::FullInit || vs == ValState::Shared;
                    });
                    set_state(state, lv, ValState::Shared);
                    break;
            }
        }
    };

    struct StmtRef {
        MIR::BasicBlockId block;
        size_t stmt_idx;

        StmtRef(MIR::BasicBlockId bb, size_t stmt)
            : block(bb)
            , stmt_idx(stmt)
        {
        }
    };

    struct SubStmtRef {
        StmtRef stmt;
        size_t sub_idx;

        SubStmtRef(StmtRef stmt, size_t sub_idx)
            : stmt(stmt)
            , sub_idx(sub_idx)
        {
        }

        SubStmtRef(MIR::BasicBlockId bb, size_t stmt, size_t sub_idx)
            : stmt(bb, stmt)
            , sub_idx(sub_idx)
        {
        }
    };

    struct ValidRegion {
        StmtRef start;
        std::vector<MIR::BasicBlockId> path;
        SubStmtRef end;
    };

    class BorrowState {
        const ::MIR::TypeResolve& state;

        struct LifetimeInfo {
            SubStmtRef origin;
            MIR::LValue value;
        };

        std::vector<LifetimeInfo> local_lifetimes;

        struct LifetimeIvar {
            //unsigned    target_binding;
            std::vector<HIR::LifetimeRef> srcs;
            std::vector<HIR::LifetimeRef> dsts;
        };

        std::vector<LifetimeIvar> ivar_lifetimes;

    public:
        BorrowState(const ::MIR::TypeResolve& state)
            : state(state)
        {
        }

        /// <summary>
        /// Assign two lifetimes (e.g. via an assignment, or a function call)
        /// </summary>
        /// <param name="target">Target lifetime (LHS or receiver)</param>
        /// <param name="src">Source lifetime</param>
        void lifetime_assign(const HIR::LifetimeRef& target, const HIR::LifetimeRef& src) {
            DEBUG(state << target << " = " << src);
            // One of these must be an ivar?
            // - Record the to/from for each.
            if (auto* iv = opt_ivar(target)) {
                iv->srcs.push_back(src);
            }
            if (auto* iv = opt_ivar(src)) {
                iv->dsts.push_back(target);
            }
        }

        void type_assign_pp(const HIR::PathParams& dst, const HIR::PathParams& src) {
            MIR_ASSERT(state, dst.m_lifetimes.size() == src.m_lifetimes.size(), "Param count error - " << dst << " == " << src);
            MIR_ASSERT(state, dst.m_types.size() == src.m_types.size(), "Param count error - " << dst << " == " << src);
            for (size_t i = 0; i < dst.m_lifetimes.size(); i++) {
                lifetime_assign(dst.m_lifetimes[i], src.m_lifetimes[i]);
            }
            for (size_t i = 0; i < dst.m_types.size(); i++) {
                type_assign(dst.m_types[i], src.m_types[i]);
            }
        }

        void type_assign(const HIR::TypeRef& dst_ty, const HIR::TypeRef& src_ty) {
            MIR_ASSERT(state, dst_ty->tag() == src_ty->tag(), dst_ty << " != " << src_ty);
            TU_MATCH_HDRA( ((*dst_ty), (*src_ty)),  { )
            TU_ARMA(Infer, de, se) MIR_BUG(state, "Unexpected infer - " << dst_ty << ", " << src_ty);
                TU_ARMA(Generic, de, se) {
                }
                TU_ARMA(Diverge, de, se) {
                }
                TU_ARMA(Primitive, de, se) {
                }
                TU_ARMA(Borrow, de, se) {
                    lifetime_assign(de.lifetime, se.lifetime);
                    type_assign(de.inner, se.inner);
                }
                TU_ARMA(Pointer, de, se) {
                    type_assign(de.inner, se.inner);
                }
                TU_ARMA(TraitObject, de, se) {
                    lifetime_assign(de.m_lifetime, se.m_lifetime);
                    type_assign_pp(de.m_trait.m_path.m_params, se.m_trait.m_path.m_params);
                    // TODO: Markers
                }
                TU_ARMA(NodeType, de, se) MIR_BUG(state, "Unexpected NodeType");
                TU_ARMA(ErasedType, de, se) MIR_BUG(state, "Unexpected ErasedType");
                TU_ARMA(Path, de, se) {
                    MIR_ASSERT(state, de.binding == se.binding, dst_ty << " != " << src_ty);
                    MIR_ASSERT(state, de.path.m_data.tag() == se.path.m_data.tag(), dst_ty << " != " << src_ty);
                TU_MATCH_HDRA( (de.path.m_data, se.path.m_data), { )
                TU_ARMA(Generic, dpe, spe) {
                            type_assign_pp(dpe.m_params, spe.m_params);
                        }
                        TU_ARMA(UfcsInherent, dpe, spe) {
                            type_assign_pp(dpe.impl_params, spe.impl_params);
                            type_assign(dpe.type, spe.type);
                            type_assign_pp(dpe.params, spe.params);
                        }
                        TU_ARMA(UfcsKnown, dpe, spe) {
                            type_assign_pp(dpe.trait.m_params, spe.trait.m_params);
                            type_assign(dpe.type, spe.type);
                            type_assign_pp(dpe.params, spe.params);
                        }
                        TU_ARMA(UfcsUnknown, dpe, spe) MIR_BUG(state, "Unexpected UfcsUnknown - " << dst_ty << ", " << src_ty);
                }
                }
                TU_ARMA(Array, de, se) {
                    type_assign(de.inner, se.inner);
                }
                TU_ARMA(Slice, de, se) {
                    type_assign(de.inner, se.inner);
                }
                TU_ARMA(Tuple, de, se) {
                    assert(de.size() == se.size());
                    for (size_t i = 0; i < de.size(); i++) {
                        type_assign(de[i], se[i]);
                    }
                }
                TU_ARMA(NamedFunction, de, se) {
                    MIR_TODO(state, "NamedFunction MIR borrowcheck");
                }
                TU_ARMA(Function, de, se) {
                    MIR_ASSERT(state, de.m_arg_types.size() == se.m_arg_types.size(), "Arg count error");
                    for (size_t i = 0; i < de.m_arg_types.size(); i++) {
                        type_assign(de.m_arg_types[i], se.m_arg_types[i]);
                    }
                    type_assign(de.m_rettype, se.m_rettype);
                }
            }
        }

        void handle_param(const HIR::TypeRef& target, const MIR::Param& param, size_t ofs) {
            if (const auto* b = param.opt_Borrow()) {
                HIR::TypeRef tmp;
                auto src_ty = state.get_lvalue_type(tmp, b->val);
                auto lft = borrow_lvalue(ofs, b->type, b->val);
                type_assign(target, state.m_crate.m_types.borrow(b->type, src_ty, lft));
            } else {
                HIR::TypeRef tmp;
                type_assign(target, state.get_param_type(tmp, param));
            }
        }

        void do_assign(const MIR::LValue& lv, const HIR::TypeRef& src_ty) {
            HIR::TypeRef tmp;
            const auto& dst_ty = state.get_lvalue_type(tmp, lv);
            type_assign(dst_ty, src_ty);
        }

        /// <summary>
        /// Borrow a lvalue, returning a lifetime reference created to point at the current position
        /// </summary>
        /// <param name="stmt_inner_ofs">Offset within the statement (e.g. argument index)</param>
        /// <param name="lv">LValue</param>
        HIR::LifetimeRef borrow_lvalue(size_t stmt_inner_ofs, HIR::BorrowType bt, const MIR::LValue& lv) {
            MIR::LValue::CRef lvr(lv);
            // Unwrap until a deref or the bottom value
            while (lvr.wrapper_count() > 0 && !lvr.is_Deref()) {
                lvr = lvr.inner_ref();
            }

            TU_MATCH_HDRA( (lvr), { )
            TU_ARMA(Downcast, _)    throw "";
                TU_ARMA(Index, _) throw "";
                TU_ARMA(Field, _) throw "";

                TU_ARMA(Deref, _) {
                    HIR::TypeRef tmp;
                    const auto& inner_ty = state.get_lvalue_type(tmp, lvr.inner_ref());
                    if (const auto* tep = inner_ty->opt_Borrow()) {
                        return tep->lifetime;
                    } else if (inner_ty->is_Pointer()) {
                        // TODO: Return an unbound lifetime
                        return HIR::LifetimeRef::new_static();
                    } else {
                        MIR_BUG(state, "Unexpected type: " << inner_ty);
                    }
                }

                TU_ARMA(Static, _)
                return HIR::LifetimeRef::new_static();
                TU_ARMA(Return, _)
                MIR_BUG(state, "Borrowing return slot");
                TU_ARMA(Local, _) {
                    // Allocate/find a local borrow reference for this slot
                    // - Record the entire lvalue for this borrow
                    return this->allocate_local(SubStmtRef(state.get_cur_block(), state.get_cur_stmt_ofs(), stmt_inner_ofs), lv.clone());
                }
                TU_ARMA(Argument, _) {
                    // Allocate/find a local borrow reference for this slot
                    // - Record the entire lvalue for this borrow
                    return this->allocate_local(SubStmtRef(state.get_cur_block(), state.get_cur_stmt_ofs(), stmt_inner_ofs), lv.clone());
                }
            }
            throw "";
        }

        HIR::LifetimeRef allocate_ivar() {
            auto idx = ivar_lifetimes.size();
            ivar_lifetimes.push_back(LifetimeIvar());
            return HIR::LifetimeRef(static_cast<uint32_t>(idx + 0x14000));
        }

    private:
        HIR::LifetimeRef allocate_local(SubStmtRef origin, MIR::LValue value) {
            DEBUG(state << "New local: " << value);
            auto idx = local_lifetimes.size();
            local_lifetimes.push_back(LifetimeInfo{origin, std::move(value)});
            assert(idx < (0x4000 - 0));
            return HIR::LifetimeRef(static_cast<uint32_t>(idx + 0x10000));
        }

        LifetimeIvar* opt_ivar(const HIR::LifetimeRef& lr) {
            if (0x14000 <= lr.binding) {
                return &ivar_lifetimes.at(lr.binding - 0x14000);
            }
            return nullptr;
        }
    };
}

void MIR_BorrowCheck(const StaticTraitResolve& resolve, const ::HIR::ItemPath& path, ::MIR::Function& fcn, const ::HIR::Function::args_t& args, const ::HIR::TypeRef& ret_type) {
    static Span sp;
    TRACE_FUNCTION_F(path);
    ::MIR::TypeResolve state {
        sp, resolve, FMT_CB(ss, ss << path;), ret_type, args, fcn
    };

    DEBUG(FMT_CB(ss, MIR_Dump_Fcn(ss, fcn)));

    BorrowState borrow_state{state};

    // 0. Create liftime names/references for all borrows
    // - Each type instance gets its own new lifetime reference
    {
        TRACE_FUNCTION_FR("Fill", "Fill");

        struct LifetimeVisitor: public HIR::Visitor {
            const ::MIR::TypeResolve& state;
            BorrowState& borrow_state;

            LifetimeVisitor(const ::MIR::TypeResolve& state, BorrowState& borrow_state)
                : HIR::Visitor(nullptr, state.m_crate.m_types)
                , state(state)
                , borrow_state(borrow_state)
            {
            }

            void visit_lifetime_ref(::HIR::LifetimeRef& lr) {
                if (lr.binding == ::HIR::LifetimeRef::UNKNOWN) {
                    lr = borrow_state.allocate_ivar();
                }
            }

            void visit_path_params(::HIR::PathParams& pp) override {
                for (auto& lr : pp.m_lifetimes) {
                    visit_lifetime_ref(lr);
                }
                HIR::Visitor::visit_path_params(pp);
            }

            void visit_type(::HIR::TypeRef& t) override {
                auto data = t->clone_data();
                if (auto* te = data.opt_Borrow()) {
                    visit_lifetime_ref(te->lifetime);
                } else if (auto* te = data.opt_TraitObject()) {
                    visit_lifetime_ref(te->m_lifetime);
                } else if (data.is_ErasedType()) {
                    MIR_BUG(state, "Unexpected " << t);
                }
                HIR::Visitor::visit_type_data(data);
                t = state.m_crate.m_types.intern(mv$(data));
            };
        };

        struct V: public MIR::visit::VisitorMut {
            LifetimeVisitor lifetimes;

            V(const ::MIR::TypeResolve& state, BorrowState& borrow_state)
                : lifetimes(state, borrow_state)
            {
            }

            void visit_type(::HIR::TypeRef& t) override {
                lifetimes.visit_type(t);
            }
        } v{state, borrow_state};

        v.visit_function(state, fcn);
    }
    // - Run inference/assignment of lifetime references (between named lifetimes and borrows)
    {
        TRACE_FUNCTION_FR("Assign", "Assign");
        for (auto& blk : fcn.blocks) {
            for (auto& stmt : blk.statements) {
                state.set_cur_stmt(&blk - fcn.blocks.data(), &stmt - blk.statements.data());
                DEBUG(state << stmt);
                TU_MATCH_HDRA( (stmt), {)
                TU_ARMA(Assign, se) {
                    TU_MATCH_HDRA((se.src), {)
                    TU_ARMA(Use, rse) {
                                HIR::TypeRef tmp;
                                borrow_state.do_assign(se.dst, state.get_lvalue_type(tmp, rse));
                            }
                            TU_ARMA(Borrow, rse) {
                                HIR::TypeRef tmp;
                                auto src_ty = state.get_lvalue_type(tmp, rse.val);
                                auto lft = borrow_state.borrow_lvalue(0, rse.type, rse.val);
                                borrow_state.do_assign(se.dst, state.m_crate.m_types.borrow(rse.type, src_ty, lft));
                            }
                            TU_ARMA(Array, rse) {
                                HIR::TypeRef tmp;
                                const auto& dst_ty = state.get_lvalue_type(tmp, se.dst)->as_Array().inner;
                                for (size_t i = 0; i < rse.vals.size(); i++) {
                                    borrow_state.handle_param(dst_ty, rse.vals[i], i);
                                }
                            }
                            TU_ARMA(SizedArray, rse) {
                                HIR::TypeRef tmp;
                                const auto& dst_ty = state.get_lvalue_type(tmp, se.dst)->as_Array().inner;
                                borrow_state.handle_param(dst_ty, rse.val, 0);
                            }
                            TU_ARMA(Struct, rse) {
                                const auto& str = resolve.m_crate.get_struct_by_path(state.sp, rse.path.m_path);
                                MonomorphStatePtr ms(state.m_crate.m_types, nullptr, &rse.path.m_params, nullptr);
                                HIR::TypeRef tmp;
                                auto maybe_monomorph = [&](const auto& ty) -> const HIR::TypeRef& {
                                    return resolve.monomorph_expand_opt(sp, tmp, ty, ms);
                                };
                                auto get_field_ty = [&](size_t field_index) -> const HIR::TypeRef& {
                            TU_MATCH_HDRA( (str.m_data), {)
                            TU_ARMA(Unit, se) {
                                            MIR_BUG(state, "Field on unit-like struct - " << rse.path);
                                        }
                                        TU_ARMA(Tuple, se) {
                                            MIR_ASSERT(state, field_index < se.size(), "Field index out of range in tuple-struct " << rse.path);
                                            return maybe_monomorph(se[field_index].ent);
                                        }
                                        TU_ARMA(Named, se) {
                                            MIR_ASSERT(state, field_index < se.size(), "Field index out of range in struct " << rse.path);
                                            return maybe_monomorph(se[field_index].ty);
                                        }
                            }
                            throw "";
                                };
                                for (size_t i = 0; i < rse.vals.size(); i++) {
                                    borrow_state.handle_param(get_field_ty(i), rse.vals[i], i);
                                }
                            }
                            TU_ARMA(EnumVariant, rse) {
                                const auto& enm = resolve.m_crate.get_enum_by_path(state.sp, rse.path.m_path);
                                MonomorphStatePtr ms(state.m_crate.m_types, nullptr, &rse.path.m_params, nullptr);
                                HIR::TypeRef tmp;
                                //auto maybe_monomorph = [&](const auto& ty)->const HIR::TypeRef& {
                                //    return resolve.monomorph_expand_opt(sp, tmp, ty, ms);
                                //};
                                if (rse.vals.size() > 0) {
                                    MIR_ASSERT(state, enm.m_data.is_Data(), "");
                                    const auto& variants = enm.m_data.as_Data();
                                    MIR_ASSERT(state, rse.index < variants.size(), "Variant index out of range for " << rse.path);
                                    const auto& variant = variants[rse.index];

                                    const auto& var_ty = resolve.monomorph_expand_opt(sp, tmp, variant.type, MonomorphStatePtr(state.m_crate.m_types, nullptr, &rse.path.m_params, nullptr));
                                    const auto& str = *var_ty->as_Path().binding.as_Struct();
                                    const auto& s_path = var_ty->as_Path().path.m_data.as_Generic();
                                    auto maybe_monomorph = [&](const HIR::TypeRef& ty) -> const HIR::TypeRef& {
                                        return resolve.monomorph_expand_opt(sp, tmp, ty, MonomorphStatePtr(state.m_crate.m_types, nullptr, &s_path.m_params, nullptr));
                                    };
                            TU_MATCH_HDRA( (str.m_data), {)
                            TU_ARMA(Unit, se) {
                                        }
                                        TU_ARMA(Tuple, se) {
                                            MIR_ASSERT(state, se.size() == rse.vals.size(), "Field index out of range in tuple enum variant " << rse.path);
                                            for (size_t i = 0; i < rse.vals.size(); i++) {
                                                borrow_state.handle_param(maybe_monomorph(se[i].ent), rse.vals[i], i);
                                            }
                                        }
                                        TU_ARMA(Named, se) {
                                            MIR_ASSERT(state, se.size() == rse.vals.size(), "Field index out of range in named enum variant " << rse.path);
                                            for (size_t i = 0; i < rse.vals.size(); i++) {
                                                borrow_state.handle_param(maybe_monomorph(se[i].ty), rse.vals[i], i);
                                            }
                                        }
                            }
                                }
                            }
                            TU_ARMA(UnionVariant, rse) {
                                MIR_TODO(state, "");
                            }
                            TU_ARMA(Tuple, rse) {
                                HIR::TypeRef tmp;
                                const auto& dst_ty = state.get_lvalue_type(tmp, se.dst);
                                const auto& de = dst_ty->as_Tuple();
                                MIR_ASSERT(state, de.size() == rse.vals.size(), "Tuple size and rvalue mismatch");
                                for (size_t i = 0; i < rse.vals.size(); i++) {
                                    borrow_state.handle_param(de[i], rse.vals[i], i);
                                }
                            }
                            TU_ARMA(DstPtr, rse) {
                            }
                            TU_ARMA(DstMeta, rse) {
                                // TODO &'static for vtables
                            }
                            TU_ARMA(MakeDst, rse) {
                                HIR::TypeRef tmp;
                                const auto& dst_ty = state.get_lvalue_type(tmp, se.dst);
                                if (dst_ty->is_Borrow()) {
                                    if (rse.ptr_val.is_Borrow()) {
                                        // TODO: Make the borrow?
                                    } else {
                                        HIR::TypeRef tmp2;
                                        const auto& src_ty = state.get_param_type(tmp2, rse.ptr_val);
                                        borrow_state.lifetime_assign(dst_ty->as_Borrow().lifetime, src_ty->as_Borrow().lifetime);
                                    }
                                }
                            }
                            TU_ARMA(UniOp, rse) {
                            }
                            TU_ARMA(BinOp, rse) {
                            }
                            TU_ARMA(Constant, rse) {
                                borrow_state.do_assign(se.dst, state.get_const_type(rse));
                            }
                            TU_ARMA(Cast, rse) {
                                HIR::TypeRef tmp;
                                const auto& dst_ty = state.get_lvalue_type(tmp, se.dst);
                                HIR::TypeRef tmp2;
                                const auto& src_ty = state.get_lvalue_type(tmp2, rse.val);
                                // Handle both being borrows
                                if (dst_ty->is_Borrow() && src_ty->is_Borrow()) {
                                    borrow_state.lifetime_assign(dst_ty->as_Borrow().lifetime, src_ty->as_Borrow().lifetime);
                                }
                            }
                    }
                    }
                    TU_ARMA(SetDropFlag, se) {
                    }
                    TU_ARMA(LoadDropFlag, se) {
                    }
                    TU_ARMA(SaveDropFlag, se) {
                    }
                    TU_ARMA(ScopeEnd, se) { /* todo */
                    }
                    TU_ARMA(Drop, se) { /* todo */
                    }
                    TU_ARMA(Asm, se) {
                    }
                    TU_ARMA(Asm2, se) {
                    }
                }
                // Note: Also need to pass through function calls, assignments, and structs
                // - Drop needs to be handled for anything with drop glue (as it counts as a use of contained borrows)
            }

            state.set_cur_stmt_term(&blk - fcn.blocks.data());
            DEBUG(state << blk.terminator);
            TU_MATCH_HDRA( (blk.terminator), { )
            default:
                break;
                TU_ARMA(Call, e) {
                TU_MATCH_HDRA( (e.fcn), {)
                TU_ARMA(Intrinsic, fe) {
                        }
                        TU_ARMA(Value, fe) {
                            HIR::TypeRef tmp;
                            const auto& ty = state.get_lvalue_type(tmp, fe);
                            const auto& fcn = ty->as_Function();
                            // TODO: HKTs
                            MIR_ASSERT(state, fcn.m_arg_types.size() == e.args.size(), "");
                            for (size_t i = 0; i < fcn.m_arg_types.size(); i++) {
                                borrow_state.handle_param(fcn.m_arg_types[i], e.args[i], i);
                            }
                            borrow_state.do_assign(e.ret_val, fcn.m_rettype);
                        }
                        TU_ARMA(Path, fe) {
                            HIR::TypeRef tmp;

                            MonomorphState ms(state.m_crate.m_types);
                            auto v = resolve.get_value(state.sp, fe, ms, true);
                            auto maybe_monomorph = [&](const ::HIR::TypeRef& ty) -> const HIR::TypeRef& {
                                return resolve.monomorph_expand_opt(state.sp, tmp, ty, ms);
                            };

                            const auto& fcn = *v.as_Function();
                            MIR_ASSERT(state, fcn.m_args.size() <= e.args.size(), "");
                            for (size_t i = 0; i < fcn.m_args.size(); i++) {
                                // Handle the param, unify types.
                                const auto& exp_ty = maybe_monomorph(fcn.m_args[i].second);
                                DEBUG("ARG" << i << " " << exp_ty << " = " << e.args[i]);

                                borrow_state.handle_param(exp_ty, e.args[i], i);
                            }
                            const auto& rv_ty = maybe_monomorph(fcn.m_return);
                            DEBUG("RV" << " " << e.ret_val << " = " << rv_ty);
                            borrow_state.do_assign(e.ret_val, rv_ty);
                        }
                }
                }
            }
        }
    }

    auto val_states = FcnState(args.size(), fcn.locals.size());

    // 1. Determine the lifetime (scope) of each variable (from assignment to last use)
    // - Needs to represent disjoint lifetimes (i.e. same variable assigned multiple times)
    // - Walk the graph, finding assignments of locals and tracking last use until next assignment
    //   > At next assignment (or drop/move), record that lifetime span (as start,path,end)

    // 2. Run full state tracking, including tracking of borrow sources.
    // TODO: Figure out the rest
}

void MIR_BorrowCheck_Crate(::HIR::Crate& crate) {
    ::MIR::OuterVisitor ov{crate, [&](const auto& res, const auto& p, ::HIR::ExprPtr& expr_ptr, const auto& args, const auto& ty) {
        MIR_BorrowCheck(res, p, expr_ptr.get_mir_or_error_mut(Span()), args, ty);
    }};
    ov.visit_crate(crate);
}

#include <algorithm>
#include "mir_main_bindings.h"
#include "mir_mir.h"
#include "hir_visitor.h"
#include "hir_typeck_static.h"
#include "mir_helpers.h"
#include "mir_visit_crate_mir.h"
#include "mir_operations.h"

namespace {
    ::HIR::TypeRef get_metadata_type(const ::MIR::TypeResolve& state, const ::HIR::TypeRef& unsized_ty) {
        static Span sp;
        auto& types = state.m_crate.m_types;
        if (const auto* tep = unsized_ty->opt_TraitObject()) {
            const auto& trait_path = tep->m_trait;

            if (trait_path.m_path.m_path == ::HIR::SimplePath()) {
                return types.unit();
            } else {
                const auto& trait = *tep->m_trait.m_trait_ptr;

                auto vtable_ty = trait.get_vtable_type(state.sp, state.m_resolve.m_crate, *tep);

                return types.borrow(HIR::BorrowType::Shared, vtable_ty);
            }
        } else if (unsized_ty->is_Slice() || (unsized_ty->is_Primitive() && unsized_ty->as_Primitive() == HIR::CoreType::Str)) {
            return types.primitive(::HIR::CoreType::Usize);
        } else if (const auto* tep = unsized_ty->opt_Path()) {
            if (tep->binding.is_Struct()) {
                switch (tep->binding.as_Struct()->m_struct_markings.dst_type) {
                    case ::HIR::StructMarkings::DstType::None:
                        return ::HIR::TypeRef();
                    case ::HIR::StructMarkings::DstType::Possible: {
                        const auto& path = tep->path.m_data.as_Generic();
                        const auto& str = *tep->binding.as_Struct();
                        auto monomorph = [&](const auto& tpl) {
                            auto rv = MonomorphStatePtr(types, nullptr, &path.m_params, nullptr).monomorph_type(sp, tpl);
                            state.m_resolve.expand_associated_types(sp, rv);
                            return rv;
                        };
                        TU_MATCHA((str.m_data), (se), (Unit, MIR_BUG(state, "Unit-like struct with DstType::Possible - " << unsized_ty);), (Tuple, return get_metadata_type(state, monomorph(se.back().ent));), (Named, return get_metadata_type(state, monomorph(se.back().ty));))
                        throw "";
                    }
                    case ::HIR::StructMarkings::DstType::Slice:
                        return types.primitive(::HIR::CoreType::Usize);
                    case ::HIR::StructMarkings::DstType::TraitObject:
                        return types.unit(); // TODO: Get the actual inner metadata type?
                }
            }
            return ::HIR::TypeRef();
        } else if (unsized_ty->is_Generic()) {
            ::HIR::Path p{unsized_ty, state.m_resolve.m_lang_Pointee, "Metadata"};
            auto rv = types.path(std::move(p), {});
            state.m_resolve.expand_associated_types(sp, rv);
            return rv;
        } else {
            return ::HIR::TypeRef();
        }
    }
}

//template<typename T>
//::std::ostream& operator<<(::std::ostream& os, const T& v) {
//    v.fmt(os);
//    return os;
//}

// [ValState] = Value state tracking (use after move, uninit, ...)
// - [ValState] No drops or usage of uninitalised values (Uninit, Moved, or Dropped)
// - [ValState] Temporaries are write-once.
//  - Requires maintaining state information for all variables/temporaries with support for loops
void MIR_Validate_ValState(::MIR::TypeResolve& state, const ::MIR::Function& fcn) {
    TRACE_FUNCTION;

    // > Iterate through code, creating state maps. Save map at the start of each bb.
    struct ValStates {
        // Wrapper for an enum that fits in a `uint8_t`
        // TODO: A u2 would be even better (packed into a custom vector)
        // - But, there's the `runs` iterator wrapper below
        struct State {
            enum Values {
                Invalid,
                Either,
                Valid,
            };

            uint8_t v;

            State()
                : v(0)
            {
            }

            State(uint8_t v)
                : v(v)
            {
            }

            bool operator==(const State& x) const {
                return v == x.v;
            }

            bool operator!=(const State& x) const {
                return v != x.v;
            }

            bool operator==(uint8_t x) const {
                return v == x;
            }

            bool operator!=(uint8_t x) const {
                return v != x;
            }
        };

        /// Collection of `State`s
        struct StateVec {
            size_t m_size;
            std::vector<uint64_t> v;

            StateVec(size_t n = 0, State init = {})
                : m_size(n)
                , v((n + 31) / 32, uint64_t(init.v) * 0x5555555555555555ULL)
            {
                const auto used = n % 32;
                if (used != 0) {
                    v.back() |= ~((uint64_t(1) << (used * 2)) - 1);
                }
            }

            bool operator==(const StateVec& x) const {
                return m_size == x.m_size && v == x.v;
            }

            bool operator!=(const StateVec& x) const {
                return !(*this == x);
            }

            bool empty() const {
                return v.empty();
            }

            size_t size() const {
                return m_size;
            }

            class reference {
                uint64_t& slot;
                uint8_t bit_ofs;
                State v;

                friend StateVec;

                reference(uint64_t& slot, uint8_t bit_ofs)
                    : slot(slot)
                    , bit_ofs(bit_ofs)
                    , v((slot >> bit_ofs) & 3)
                {
                }

            public:
                ~reference() {
                    slot = (slot & ~(uint64_t(3) << bit_ofs)) | (uint64_t(v.v) << bit_ofs);
                }

                State& get() {
                    return v;
                }

                operator State() const {
                    return v;
                }

                reference& operator=(State v) {
                    this->v = v;
                    return *this;
                }

                bool operator==(State v) const {
                    return (State) * this == v;
                }

                bool operator!=(State v) const {
                    return (State) * this != v;
                }
            };

            State operator[](size_t idx) const {
                return (v[idx / 32] >> (idx % 32 * 2)) & 3;
            }

            reference operator[](size_t idx) {
                return reference(v[idx / 32], idx % 32 * 2);
            }
        };

        State ret_state = State::Invalid;
        StateVec args;
        StateVec locals;

        ValStates() {
        }

        ValStates(size_t n_args, size_t n_locals)
            : args(n_args, State::Valid)
            , locals(n_locals)
        {
        }

        explicit ValStates(const ValStates& v) = default;
        ValStates(ValStates&& v) = default;
        ValStates& operator=(const ValStates& v) = delete;
        ValStates& operator=(ValStates&& v) = default;

        void fmt(::std::ostream& os) const {
            os << "ValStates { ";
            switch (ret_state.v) {
                case State::Invalid:
                    break;
                case State::Either:
                    os << "?";
                case State::Valid:
                    os << "rv, ";
                    break;
            }
            auto fmt_val_range = [&](const char* prefix, const StateVec& list) {
                for (auto range : runs(list)) {
                    switch (list[range.first].v) {
                        case State::Invalid:
                            continue;
                        case State::Either:
                            os << "?";
                            break;
                        case State::Valid:
                            break;
                    }
                    if (range.first == range.second) {
                        os << prefix << range.first << ", ";
                    } else {
                        os << prefix << range.first << "-" << prefix << range.second << ", ";
                    }
                }
            };
            fmt_val_range("a", this->args);
            fmt_val_range("_", this->locals);
            os << "}";
        }

        bool operator==(const ValStates& x) const {
            if (ret_state != x.ret_state) {
                return false;
            }
            if (args != x.args) {
                return false;
            }
            if (locals != x.locals) {
                return false;
            }
            return true;
        }

        bool merge(unsigned bb_idx, const ValStates& other) {
            DEBUG("bb" << bb_idx << " this=" << FMT_CB(ss, this->fmt(ss);) << ", other=" << FMT_CB(ss, other.fmt(ss);));
            if (*this == other) {
                return false;
            } else {
                bool rv = false;
                rv |= ValStates::merge_state(this->ret_state, other.ret_state);
                rv |= ValStates::merge_lists(this->args, other.args);
                rv |= ValStates::merge_lists(this->locals, other.locals);
                return rv;
            }
        }

        void mark_validity(const ::MIR::TypeResolve& state, const ::MIR::LValue& lv, bool is_valid) {
            if (!lv.m_wrappers.empty()) {
                return;
            }
            TU_MATCH_HDRA( (lv.m_root), {)
            TU_ARMA(Return, e) {
                    ret_state = is_valid ? State::Valid : State::Invalid;
                }
                TU_ARMA(Argument, e) {
                    MIR_ASSERT(state, e < this->args.size(), "Argument index out of range " << lv);
                    DEBUG("arg$" << e << " = " << (is_valid ? "Valid" : "Invalid"));
                    this->args[e] = is_valid ? State::Valid : State::Invalid;
                }
                TU_ARMA(Local, e) {
                    MIR_ASSERT(state, e < this->locals.size(), "Local index out of range - " << lv);
                    DEBUG("_" << e << " = " << (is_valid ? "Valid" : "Invalid"));
                    this->locals[e] = is_valid ? State::Valid : State::Invalid;
                }
                TU_ARMA(Static, e) {
                }
            }
        }

        void ensure_valid(const ::MIR::TypeResolve& state, const ::MIR::LValue& lv) {
            TU_MATCH_HDRA( (lv.m_root), {)
            TU_ARMA(Return, e) {
                    if (this->ret_state != State::Valid) {
                        MIR_BUG(state, "Use of non-valid lvalue - " << lv);
                    }
                }
                TU_ARMA(Argument, e) {
                    MIR_ASSERT(state, e < this->args.size(), "Arg index out of range");
                    if (this->args[e] != State::Valid) {
                        MIR_BUG(state, "Use of non-valid lvalue - " << lv);
                    }
                }
                TU_ARMA(Local, e) {
                    MIR_ASSERT(state, e < this->locals.size(), "Local index out of range");
                    if (this->locals[e] != State::Valid) {
                        MIR_BUG(state, "Use of non-valid lvalue - " << lv);
                    }
                }
                TU_ARMA(Static, e) {
                }
            }

            for(const auto& w : lv.m_wrappers)
            {
                if (w.is_Index()) {
                    if (this->locals[w.as_Index()] != State::Valid) {
                        MIR_BUG(state, "Use of non-valid lvalue - " << ::MIR::LValue::new_Local(w.as_Index()));
                    }
                }
            }
        }

        void move_val(const ::MIR::TypeResolve& state, const ::MIR::LValue& lv) {
            ensure_valid(state, lv);
            if (!state.lvalue_is_copy(lv)) {
                mark_validity(state, lv, false);
            }
        }

        void move_val(const ::MIR::TypeResolve& state, const ::MIR::Param& p) {
            if (const auto* e = p.opt_LValue()) {
                move_val(state, *e);
            }
        }

    private:
        static bool merge_state(State& a, const State& b) {
            bool rv = false;
            if (a != b) {
                // NOTE: This is an attempted optimisation to avoid re-running a block when it's not a new state.
                if (a == State::Either /*|| b == State::Either*/) {
                } else {
                    rv = true;
                }
                a = State::Either;
            }
            return rv;
        }

        static bool merge_lists(StateVec& a, const StateVec& b) {
            bool rv = false;
            assert(a.size() == b.size());
            assert(a.v.size() == b.v.size());
            for (size_t i = 0; i < a.v.size(); i++) {
                const uint64_t av = a.v[i];
                const uint64_t bv = b.v[i];
                const uint64_t differingLowBits = ((av ^ bv) | ((av ^ bv) >> 1)) & 0x5555555555555555ULL;
                const uint64_t differingMask = differingLowBits | (differingLowBits << 1);
                const uint64_t merged = (av & ~differingMask) | (0x5555555555555555ULL & differingMask);
                rv |= merged != av;
                a.v[i] = merged;
            }
            return rv;
        }
    };

    ::std::vector<ValStates> block_start_states(fcn.blocks.size());
    ::std::vector<bool> block_has_start_state(fcn.blocks.size());
    ::std::vector<bool> block_is_queued(fcn.blocks.size());
    ::std::vector<unsigned int> to_visit_blocks;
    size_t next_block_to_visit = 0;

    // TODO: Check that all used locals are also set (anywhere at all)

    auto add_to_visit = [&](unsigned int idx, const ValStates& incoming) {
        auto& start_state = block_start_states.at(idx);
        bool changed;
        if (!block_has_start_state[idx]) {
            start_state = ValStates(incoming);
            block_has_start_state[idx] = true;
            changed = true;
        } else {
            changed = start_state.merge(idx, incoming);
        }
        if (changed && !block_is_queued[idx]) {
            block_is_queued[idx] = true;
            to_visit_blocks.push_back(idx);
        }
    };
    add_to_visit(0, ValStates{state.m_args.size(), fcn.locals.size()});
    while (next_block_to_visit < to_visit_blocks.size()) {
        auto block = to_visit_blocks[next_block_to_visit++];
        block_is_queued[block] = false;
        assert(block < fcn.blocks.size());

        // 1. Copy the stable entry state. Incoming states are merged before a block is queued,
        // so each block is visited only when its entry state changes.
        auto val_state = ValStates(block_start_states.at(block));
        ASSERT_BUG(Span(), val_state.locals.size() == fcn.locals.size(), "");
        DEBUG("BB" << block << " " << FMT_CB(ss, val_state.fmt(ss);));

        // 2. Using the newly merged state, iterate statements checking the usage and updating state.
        const auto& bb = fcn.blocks[block];
        for (unsigned int stmt_idx = 0; stmt_idx < bb.statements.size(); stmt_idx++) {
            const auto& stmt = bb.statements[stmt_idx];
            state.set_cur_stmt(block, stmt_idx);

            DEBUG(state << stmt);
            switch (stmt.tag()) {
                case ::MIR::Statement::TAGDEAD:
                    throw "";
                case ::MIR::Statement::TAG_SetDropFlag:
                    MIR_ASSERT(state, stmt.as_SetDropFlag().idx < fcn.drop_flags.size(), "");
                    if (stmt.as_SetDropFlag().other != ~0u) {
                        MIR_ASSERT(state, stmt.as_SetDropFlag().other < fcn.drop_flags.size(), "");
                    }
                    break;
                case ::MIR::Statement::TAG_LoadDropFlag:
                    MIR_ASSERT(state, stmt.as_LoadDropFlag().idx < fcn.drop_flags.size(), "");
                    val_state.ensure_valid(state, stmt.as_LoadDropFlag().slot);
                    break;
                case ::MIR::Statement::TAG_SaveDropFlag:
                    MIR_ASSERT(state, stmt.as_SaveDropFlag().idx < fcn.drop_flags.size(), "");
                    val_state.ensure_valid(state, stmt.as_SaveDropFlag().slot);
                    break;
                case ::MIR::Statement::TAG_Drop:
                    // Invalidate the slot
                    if (stmt.as_Drop().flag_idx == ~0u) {
                        val_state.ensure_valid(state, stmt.as_Drop().slot);
                    } else {
                        MIR_ASSERT(state, stmt.as_Drop().flag_idx < fcn.drop_flags.size(), "");
                    }
                    val_state.mark_validity(state, stmt.as_Drop().slot, false);
                    break;
                case ::MIR::Statement::TAG_Asm:
                    for (const auto& v : stmt.as_Asm().inputs) {
                        val_state.ensure_valid(state, v.second);
                    }
                    for (const auto& v : stmt.as_Asm().outputs) {
                        val_state.mark_validity(state, v.second, true);
                    }
                    break;
                    TU_ARM(stmt, Asm2, e) {
                        for (const auto& p : e.params) {
                    TU_MATCH_HDRA( (p), { )
                    TU_ARMA(Const, v) {
                                }
                                TU_ARMA(Sym, v) {
                                }
                                TU_ARMA(Reg, v) {
                                    if (v.input) {
                                        val_state.move_val(state, *v.input);
                                    }
                                    if (v.output) {
                                        val_state.mark_validity(state, *v.output, true);
                                    }
                                }
                    }
                        }
                    }
                    break;
                case ::MIR::Statement::TAG_Assign:
                    // Destination must be valid
                    for (const auto& w : stmt.as_Assign().dst.m_wrappers) {
                        if (w.is_Deref()) {
                            // TODO: Check validity of the rest of the wrappers.
                        }
                        if (w.is_Index()) {
                            if (val_state.locals[w.as_Index()] != ValStates::State::Valid) {
                                MIR_BUG(state, "Use of non-valid lvalue - " << ::MIR::LValue::new_Local(w.as_Index()));
                            }
                        }
                    }
                    // Check source (and invalidate sources)
                    TU_MATCH(
                        ::MIR::RValue,
                        (stmt.as_Assign().src),
                        (se),
                        (Use, val_state.move_val(state, se);),
                        (
                            Constant,
                            //(void)state.get_const_type(se);
                        ),
                        (SizedArray, val_state.move_val(state, se.val);),
                        (Borrow, val_state.ensure_valid(state, se.val);),
                        (
                            Cast,
                            // Well.. it's not exactly moved...
                            val_state.ensure_valid(state, se.val);
                            //val_state.move_val(state, se.val);
                        ),
                        (BinOp, val_state.move_val(state, se.val_l); val_state.move_val(state, se.val_r);),
                        (UniOp, val_state.move_val(state, se.val);),
                        (DstMeta, val_state.ensure_valid(state, se.val);),
                        (DstPtr, val_state.ensure_valid(state, se.val);),
                        (MakeDst,
                         //val_state.move_val(state, se.ptr_val);
                         if (const auto* e = se.ptr_val.opt_LValue()) val_state.ensure_valid(state, *e);
                         val_state.move_val(state, se.meta_val);),
                        (Tuple, for (const auto& v : se.vals) val_state.move_val(state, v);),
                        (Array, for (const auto& v : se.vals) val_state.move_val(state, v);),
                        (UnionVariant, val_state.move_val(state, se.val);),
                        (EnumVariant, for (const auto& v : se.vals) val_state.move_val(state, v);),
                        (Struct, for (const auto& v : se.vals) val_state.move_val(state, v);)
                    )
                    // Mark destination as valid
                    val_state.mark_validity(state, stmt.as_Assign().dst, true);
                    break;
                case ::MIR::Statement::TAG_ScopeEnd:
                    //for(auto idx : stmt.as_ScopeEnd().vars)
                    //    val_state.mark_validity(state, ::MIR::LValue::make_Variable(idx), false);
                    //for(auto idx : stmt.as_ScopeEnd().tmps)
                    //    val_state.mark_validity(state, ::MIR::LValue::make_Temporary({idx}), false);
                    break;
            }
        }

        // 3. Pass new state on to destination blocks
        state.set_cur_stmt_term(block);
        DEBUG(state << bb.terminator);
        TU_MATCH_HDRA( (bb.terminator), { )
        TU_ARMA(Incomplete, e) {
                // Should be impossible here.
            }
            TU_ARMA(Return, e) {
                // Check if the return value has been set
                val_state.ensure_valid(state, ::MIR::LValue::new_Return());
                // Ensure that no other non-Copy values are valid
                for (unsigned int i = 0; i < val_state.locals.size(); i++) {
                    if (val_state.locals[i] == ValStates::State::Invalid) {
                    } else if (state.m_resolve.type_is_copy(state.sp, fcn.locals[i])) {
                    } else {
                        // TODO: Error, becuase this has just been leaked
                        // Can't error, as this doesn't know if the value has been partially moved out of (as this code doesn't track that detailed)
                        //MIR_BUG(state, "Value _" << i << ": " << fcn.locals[i] << " still valid?");
                    }
                }
            }
            TU_ARMA(Diverge, e) {
                // TODO: Ensure that cleanup has been performed.
            }
            TU_ARMA(Goto, e) {
                // Push block with the new state
                add_to_visit(e, val_state);
            }
            TU_ARMA(Panic, e) {
                // What should be done here?
            }
            TU_ARMA(If, e) {
                // Push blocks
                val_state.ensure_valid(state, e.cond);
                add_to_visit(e.bb_true, val_state);
                add_to_visit(e.bb_false, val_state);
            }
            TU_ARMA(Switch, e) {
                if (e.valid_flag == ~0u) {
                    val_state.ensure_valid(state, e.val);
                } else {
                    MIR_ASSERT(state, e.valid_flag < fcn.drop_flags.size(), "df" << e.valid_flag << " out of range");
                    MIR_ASSERT(state, e.invalid_target < fcn.blocks.size(), "Invalid conditional switch target");
                    add_to_visit(e.invalid_target, val_state);
                }
                for (const auto& tgt : e.targets) {
                    add_to_visit(tgt, val_state);
                }
            }
            TU_ARMA(SwitchValue, e) {
                val_state.ensure_valid(state, e.val);
                for (const auto& tgt : e.targets) {
                    add_to_visit(tgt, val_state);
                }
                add_to_visit(e.def_target, val_state);
            }
            TU_ARMA(Call, e) {
                if (e.fcn.is_Value()) {
                    val_state.ensure_valid(state, e.fcn.as_Value());
                }
                for (const auto& arg : e.args) {
                    val_state.move_val(state, arg);
                }
                // Push blocks (with return valid only in one)
                add_to_visit(e.panic_block, val_state);

                // TODO: If the function returns !, don't follow the ret_block
                val_state.mark_validity(state, e.ret_val, true);
                add_to_visit(e.ret_block, val_state);
            }
        }
    }
}

void MIR_Validate(const StaticTraitResolve& resolve, const ::HIR::ItemPath& path, const ::MIR::Function& fcn, const ::HIR::Function::args_t& args, const ::HIR::TypeRef& ret_type) {
    TRACE_FUNCTION_F(path);
    Span sp;
    ::MIR::TypeResolve state {
        sp, resolve, FMT_CB(ss, ss << path;), ret_type, args, fcn
    };
    auto& types = resolve.m_crate.m_types;
    // Validation rules:

    if (debug_enabled()) {
        MIR_Dump_Fcn(::std::cout, fcn, 0);
    }

    {
        HIR::TypeRef ty_Self = types.self();
        HIR::PathParams empty_params_i = resolve.m_impl_generics ? resolve.m_impl_generics->make_nop_params(types, 0) : HIR::PathParams();
        HIR::PathParams empty_params_m = resolve.m_item_generics ? resolve.m_item_generics->make_nop_params(types, 1) : HIR::PathParams();
        MonomorphStatePtr m(types, &ty_Self, resolve.m_impl_generics ? &empty_params_i : nullptr, resolve.m_item_generics ? &empty_params_m : nullptr);
        for (const auto& ty : fcn.locals) {
            DEBUG("_" << (&ty - fcn.locals.data()) << ": " << ty);
            if (!monomorphise_type_needed(ty)) {
                MIR_ASSERT(state, resolve.type_is_sized(sp, ty), "Local variable _" << (&ty - fcn.locals.data()) << ": " << ty << " isn't Sized");
            }
            m.monomorph_type(sp, ty, /*allow_infer=*/false);
        }
    }

    {
        for (const auto& bb : fcn.blocks) {
            state.set_cur_stmt_term(&bb - &fcn.blocks.front());
            MIR_ASSERT(state, bb.terminator.tag() != ::MIR::Terminator::TAGDEAD, "Moved terminator");
        }
    }
    // [CFA] = Control Flow Analysis
    // - [CFA] All code paths from bb0 must end with either a return or a diverge (or loop)
    //  - Requires checking the links between basic blocks, with a bitmap to catch loops/multipath
    {
        bool returns = false;
        ::std::vector<bool> visited_bbs(fcn.blocks.size());
        ::std::vector<unsigned int> to_visit_blocks;
        to_visit_blocks.push_back(0);
        while (to_visit_blocks.size() > 0) {
            auto block = to_visit_blocks.back();
            to_visit_blocks.pop_back();
            MIR_ASSERT(state, block < fcn.blocks.size(), "Ended up with BB out of range (" << block << " >= " << fcn.blocks.size() << ")");
            if (visited_bbs[block]) {
                continue;
            }
            visited_bbs[block] = true;

            state.set_cur_stmt_term(block);

#define PUSH_BB(idx, desc)                                                     \
    do {                                                                       \
        if (!(idx < fcn.blocks.size()))                                        \
            MIR_BUG(state, "Invalid target block - " << desc << " bb" << idx); \
        if (visited_bbs[idx] == false) {                                       \
            to_visit_blocks.push_back(idx);                                    \
        }                                                                      \
    } while (0)
            TU_MATCH(
                ::MIR::Terminator,
                (fcn.blocks[block].terminator),
                (e),
                (Incomplete, MIR_BUG(state, "Encounterd `Incomplete` block in control flow - BB" << block);),
                (Return, returns = true;),
                (
                    Diverge,
                    //can_panic = true;
                ),
                (Goto, PUSH_BB(e, "Goto");),
                (Panic, PUSH_BB(e.dst, "Panic");),
                (If, PUSH_BB(e.bb_true, "If true"); PUSH_BB(e.bb_false, "If false");),
                (Switch, for (unsigned int i = 0; i < e.targets.size(); i++) { PUSH_BB(e.targets[i], "Switch V" << i); }),
                (SwitchValue, for (unsigned int i = 0; i < e.targets.size(); i++) { PUSH_BB(e.targets[i], "SwitchValue " << i); } PUSH_BB(e.def_target, "SwitchValue def");),
                (Call, PUSH_BB(e.ret_block, "Call ret"); PUSH_BB(e.panic_block, "Call panic");)
            )
#undef PUSH_BB
        }
        if (!returns) {
            DEBUG("- Function doesn't return.");
        }
    }

    // [Flat] = Basic checks (just iterates BBs)
    // - [Flat] Types must be valid (correct type for slot etc.)
    //  - Simple check of all assignments/calls/...
    DEBUG("=== FLAT CHECKS");
    {
        for (unsigned int bb_idx = 0; bb_idx < fcn.blocks.size(); bb_idx++) {
            const auto& bb = fcn.blocks[bb_idx];
            for (unsigned int stmt_idx = 0; stmt_idx < bb.statements.size(); stmt_idx++) {
                const auto& stmt = bb.statements[stmt_idx];
                state.set_cur_stmt(bb_idx, stmt_idx);
                DEBUG(state << stmt);

                switch (stmt.tag()) {
                    case ::MIR::Statement::TAGDEAD:
                        throw "";
                    case ::MIR::Statement::TAG_SetDropFlag:
                        break;
                    case ::MIR::Statement::TAG_SaveDropFlag:
                    case ::MIR::Statement::TAG_LoadDropFlag: {
                        const auto idx = stmt.is_SaveDropFlag() ? stmt.as_SaveDropFlag().idx : stmt.as_LoadDropFlag().idx;
                        MIR_ASSERT(state, idx < fcn.drop_flags.size(), "df" << idx << " out of range (nflags " << fcn.drop_flags.size() << ")");
                        const auto& slot = stmt.is_SaveDropFlag() ? stmt.as_SaveDropFlag().slot : stmt.as_LoadDropFlag().slot;
                        const auto bit = stmt.is_SaveDropFlag() ? stmt.as_SaveDropFlag().bit_index : stmt.as_LoadDropFlag().bit_index;
                        ::HIR::TypeRef slot_tmp;
                        const auto& slot_ty = state.get_lvalue_type(slot_tmp, slot);
                        MIR_ASSERT(state, slot_ty->is_Array(), "Save/Load Drop flag, slot not array: " << slot_ty);
                        const auto& slot_ty_i = slot_ty->as_Array();
                        MIR_ASSERT(state, slot_ty_i.inner == HIR::CoreType::U8, "Save/Load Drop flag, slot not u8 array: " << slot_ty);
                        auto bytes = slot_ty_i.size.as_Known();
                        MIR_ASSERT(state, bit < bytes * 8, "Save/Load drop flag, bit index out of range " << bit << " >= " << bytes * 8);
                    } break;
                    case ::MIR::Statement::TAG_Assign: {
                        const auto& a = stmt.as_Assign();
                        ::HIR::TypeRef dst_tmp;
                        const auto& dst_ty = state.get_lvalue_type(dst_tmp, a.dst);

                        auto check_types = [&](const auto& dst_ty, const auto& src_ty) {
                            DEBUG("check_types: " << dst_ty << " := " << src_ty);
                            if (src_ty == types.diverge()) {
                                // It's valid to assign to anything from a !
                            } else if (src_ty == dst_ty || src_ty->equals_ignoring_regions(dst_ty)) {
                                // Types are equal, good.
                            } else {
                                MIR_BUG(
                                    state,
                                    "Type mismatch:\n"
                                        << " dst : " << dst_ty << "\n"
                                        << " src : " << src_ty
                                );
                            }
                        };
                    TU_MATCH_HDRA( (a.src), {)
                    TU_ARMA(Use, e) {
                                ::HIR::TypeRef tmp;
                                check_types(dst_ty, state.get_lvalue_type(tmp, e));
                            }
                            TU_ARMA(Constant, e) {
                                // TODO: Check constant types.
                        TU_MATCH_HDRA( (e), {)
                        TU_ARMA(Int, c) {
                                        bool good = false;
                                        if (dst_ty->is_Primitive()) {
                                            switch (dst_ty->as_Primitive()) {
                                                case ::HIR::CoreType::I8:
                                                case ::HIR::CoreType::I16:
                                                case ::HIR::CoreType::I32:
                                                case ::HIR::CoreType::I64:
                                                case ::HIR::CoreType::I128:
                                                case ::HIR::CoreType::Isize:
                                                    good = true;
                                                    break;
                                                default:
                                                    break;
                                            }
                                        }
                                        if (!good) {
                                            MIR_BUG(state, "Type mismatch, destination is " << dst_ty << ", source is a signed integer");
                                        }
                                    }
                                    TU_ARMA(Uint, c) {
                                        bool good = false;
                                        if (dst_ty->is_Primitive()) {
                                            switch (dst_ty->as_Primitive()) {
                                                case ::HIR::CoreType::U8:
                                                case ::HIR::CoreType::U16:
                                                case ::HIR::CoreType::U32:
                                                case ::HIR::CoreType::U64:
                                                case ::HIR::CoreType::U128:
                                                case ::HIR::CoreType::Usize:
                                                case ::HIR::CoreType::Char:
                                                    good = true;
                                                    break;
                                                default:
                                                    break;
                                            }
                                        }
                                        if (!good) {
                                            MIR_BUG(state, "Type mismatch, destination is " << dst_ty << ", source is an unsigned integer");
                                        }
                                    }
                                    TU_ARMA(Float, c) {
                                        bool good = false;
                                        if (dst_ty->is_Primitive()) {
                                            switch (dst_ty->as_Primitive()) {
                                                case ::HIR::CoreType::F16:
                                                case ::HIR::CoreType::F32:
                                                case ::HIR::CoreType::F64:
                                                case ::HIR::CoreType::F128:
                                                    good = true;
                                                    break;
                                                default:
                                                    break;
                                            }
                                        }
                                        if (!good) {
                                            MIR_BUG(state, "Type mismatch, destination is " << dst_ty << ", source is a floating point value");
                                        }
                                    }
                                    TU_ARMA(Bool, c) {
                                        check_types(dst_ty, types.primitive(::HIR::CoreType::Bool));
                                    }
                                    TU_ARMA(Bytes, c) {
                                        check_types(dst_ty, types.borrow(::HIR::BorrowType::Shared, types.array(types.primitive(::HIR::CoreType::U8), c.size())));
                                    }
                                    TU_ARMA(StaticString, c) {
                                        check_types(dst_ty, types.borrow(::HIR::BorrowType::Shared, types.primitive(::HIR::CoreType::Str)));
                                    }
                                    TU_ARMA(Const, c) {
                                        // TODO: Check result type against type of const
                                    }
                                    TU_ARMA(Generic, c) {
                                        // TODO: Check result type against type of const
                                    }
                                    TU_ARMA(Function, c) {
                                        MIR_ASSERT(state, dst_ty->is_NamedFunction(), dst_ty);
                                    }
                                    TU_ARMA(ItemAddr, c) {
                                        MonomorphState ms(types);
                                        auto v = state.m_resolve.get_value(state.sp, *c, ms, /*sig_only=*/true);
                                        ::HIR::TypeRef tmp;
                            TU_MATCH_HDRA( (v), {)
                            TU_ARMA(NotFound, ve)
                                if( c->m_data.is_UfcsInherent() && c->m_data.as_UfcsInherent().item == "#type_id") {
                                            }
                                            else {
                                                MIR_BUG(state, "Unable to find item: " << *c);
                                            }
                                            TU_ARMA(NotYetKnown, ve)
                                            MIR_BUG(state, "NotYetKnown returned with sig_only=true? for " << *c);
                                            TU_ARMA(Constant, ve)
                                            MIR_BUG(state, "Constant in ItemAddr: " << *c);
                                            TU_ARMA(StructConstant, ve)
                                            MIR_BUG(state, "StructConstant in ItemAddr: " << *c);
                                            TU_ARMA(EnumValue, ve)
                                            MIR_BUG(state, "EnumValue in ItemAddr: " << *c);
                                            TU_ARMA(Static, ve) {
                                                tmp = ms.monomorph_type(state.sp, ve->m_type);
                                                resolve.expand_associated_types(state.sp, tmp);
                                                // TODO: Have a raw pointer flag
                                                if (const auto* te = dst_ty->opt_Pointer()) {
                                                    check_types(te->inner, tmp);
                                                } else {
                                                    check_types(dst_ty, types.borrow(::HIR::BorrowType::Shared, tmp));
                                                }
                                            }
                                            TU_ARMA(Function, ve) {
                                                MIR_ASSERT(state, dst_ty->is_Function(), dst_ty);
                                                // TODO: Check
                                            }
                                            TU_ARMA(EnumConstructor, ve) {
                                                MIR_ASSERT(state, dst_ty->is_Function(), dst_ty);
                                                // TODO: Check
                                            }
                                            TU_ARMA(StructConstructor, ve) {
                                                MIR_ASSERT(state, dst_ty->is_Function(), dst_ty);
                                                // TODO: Check
                                            }
                            }
                                    }
                        }
                            }
                            TU_ARMA(SizedArray, e) {
                            // NOTE: Something in liballoc does this with `MaybeUninit`, which is kinda a special case?
                                // TODO: Check that return type is an array
                            }
                            TU_ARMA(Borrow, e) {
                                ::HIR::TypeRef tmp;
                                if (e.is_raw) {
                                    check_types(dst_ty, types.pointer(e.type, state.get_lvalue_type(tmp, e.val)));
                                } else {
                                    check_types(dst_ty, types.borrow(e.type, state.get_lvalue_type(tmp, e.val)));
                                }
                            }
                            TU_ARMA(Cast, e) {
                                // Check return type
                                check_types(dst_ty, e.type);

                                // TODO: Move this to a function shared by the HIR (typecheck validate) and here

                                ::HIR::TypeRef tmp;
                                const auto& src_ty = state.get_lvalue_type(tmp, e.val);
                                // Check suitability of source type (COMPLEX)
                        TU_MATCH_HDRA((*src_ty), {)
                        default:
                            MIR_BUG(state, "Invalid cast: " << dst_ty << " from " << src_ty);
                                    // Path: Only value enums
                                    TU_ARMA(Path, s_e) {
                                        MIR_ASSERT(state, s_e.binding.is_Enum(), "Invalid cast: " << dst_ty << " from " << src_ty);
                                        MIR_ASSERT(state, s_e.binding.as_Enum()->is_value(), "Invalid cast: " << dst_ty << " from " << src_ty);
                                        MIR_ASSERT(state, dst_ty->is_Primitive(), "Invalid cast: " << dst_ty << " from " << src_ty);
                                    }
                                    // Function pointers: can be casted to integers and to sized pointers
                                    TU_ARMA(Function, s_e) {
                                        //TU_MATCH_HDRA((dst_ty.data()), {)
                                        //default:
                                        //    MIR_BUG(state, "Invalid cast: " << dst_ty << " from " << src_ty);
                                        //TU_ARMA(Primitive, d_e) {
                                        //    MIR_ASSERT(state, d_e == HIR::CoreType::Usize, "Invalid cast: " << dst_ty << " from " << src_ty);
                                        //    }
                                        //}
                                    }
                                    TU_ARMA(NamedFunction, s_e) {
                                    }
                                    // Primitives: Can cast to thin pointers or to other primitives
                                    TU_ARMA(Primitive, s_e) {
                                        MIR_ASSERT(state, s_e != HIR::CoreType::Str, "Casting from `str` is invalid");
                            TU_MATCH_HDRA((*dst_ty), {)
                            default:
                                MIR_BUG(state, "Invalid cast: " << dst_ty << " from " << src_ty);
                                            TU_ARMA(Function, d_e) {
                                                // Valid in MMIR generated from C
                                            }
                                            TU_ARMA(Pointer, d_e) {
                                                switch (s_e) {
                                                    case ::HIR::CoreType::Str:
                                                    case ::HIR::CoreType::Char:
                                                    case ::HIR::CoreType::F32:
                                                    case ::HIR::CoreType::F64:
                                                        MIR_BUG(state, "Invalid cast: " << dst_ty << " from " << src_ty);
                                                        break;
                                                    default:
                                                        break;
                                                }
                                                auto d_meta = state.m_resolve.metadata_type(state.sp, d_e.inner);
                                                MIR_ASSERT(state, d_meta == MetadataType::None || d_meta == MetadataType::Zero, "Casting primitive to invalid pointer type: " << dst_ty << " from " << src_ty);
                                            }
                                            TU_ARMA(Primitive, d_e) {
                                                MIR_ASSERT(state, d_e != HIR::CoreType::Str, "Casting to `str` is invalid");
                                                if (d_e == HIR::CoreType::Char)
                                                    MIR_ASSERT(state, s_e == HIR::CoreType::U8, "Invalid cast: " << dst_ty << " from " << src_ty);
                                            }
                            }
                                    }
                                    // Can cast to a matching raw pointer
                                    TU_ARMA(Borrow, s_e) {
                                        MIR_ASSERT(state, dst_ty->is_Pointer(), "Casting borrow to invalid type: " << dst_ty << " from " << src_ty);
                                        MIR_ASSERT(state, dst_ty->as_Pointer().type <= s_e.type, "Casting borrow to invalid type: " << dst_ty << " from " << src_ty);
                                        MIR_ASSERT(state,
                                            dst_ty->as_Pointer().inner == s_e.inner
                                                || dst_ty->as_Pointer().inner->equals_ignoring_regions(s_e.inner),
                                            "Casting borrow to invalid type: " << dst_ty << " from " << src_ty);
                                    }
                                    // Pointers: Can either be casted to another pointer, or to integers
                                    TU_ARMA(Pointer, s_e) {
                                        auto s_meta = state.m_resolve.metadata_type(state.sp, s_e.inner);
                            TU_MATCH_HDRA((*dst_ty), {)
                            default:
                                MIR_BUG(state, "Invalid cast: " << dst_ty << " from " << src_ty);
                                            TU_ARMA(Pointer, d_e) {
                                                // Only valid if metadata matches, or destination is thin
                                                if (s_e.inner != d_e.inner) {
                                                    auto d_meta = state.m_resolve.metadata_type(state.sp, d_e.inner);
                                                    if (d_meta != MetadataType::None && d_meta != MetadataType::Zero) {
                                                        if (d_meta != MetadataType::Unknown && s_meta != MetadataType::Unknown) {
                                                            MIR_ASSERT(state, d_meta == s_meta, "Casting has mismatched metadata: " << dst_ty << " from " << src_ty << " (" << d_meta << " from " << s_meta << ")");
                                                        }
                                                    }
                                                }
                                            }
                                            TU_ARMA(Primitive, d_e) {
                                                switch (d_e) {
                                                    case ::HIR::CoreType::Str:
                                                    case ::HIR::CoreType::Char:
                                                    case ::HIR::CoreType::F32:
                                                    case ::HIR::CoreType::F64:
                                                        MIR_BUG(state, "Casting pointer to invalid type: " << dst_ty << " from " << src_ty);
                                                        break;
                                                    default:
                                                        MIR_ASSERT(state, s_meta == MetadataType::None || s_meta == MetadataType::Zero, "Casting fat pointer to integer: " << dst_ty << " from " << src_ty);
                                                        break;
                                                }
                                            }
                            }
                                    }
                        }
                            }
                            TU_ARMA(BinOp, e) {
                                /*
                        ::HIR::TypeRef  tmp_l, tmp_r;
                        const auto& ty_l = state.get_lvalue_type(tmp_l, e.val_l);
                        const auto& ty_r = state.get_lvalue_type(tmp_r, e.val_r);
                        // TODO: Check that operation is valid on these types
                        switch( e.op )
                        {
                        case ::MIR::eBinOp::BIT_SHR:
                        case ::MIR::eBinOp::BIT_SHL:
                            break;
                        default:
                            // Check argument types are equal
                            if( ty_l != ty_r )
                                MIR_BUG(state, "Type mismatch in binop, " << ty_l << " != " << ty_r);
                        }
                        */
                                // TODO: Check return type
                            }
                            TU_ARMA(UniOp, e) {
                                // TODO: Check that operation is valid on this type
                                // TODO: Check return type
                            }
                            TU_ARMA(DstMeta, e) {
                                ::HIR::TypeRef tmp;
                                const auto& ty = state.get_lvalue_type(tmp, e.val);
                                const ::HIR::TypeRef* ity_p = nullptr;
                                if ((ity_p = state.is_type_owned_box(ty)))
                                    ;
                                else if (ty->is_Borrow())
                                    ity_p = &ty->as_Borrow().inner;
                                else if (ty->is_Pointer())
                                    ity_p = &ty->as_Pointer().inner;
                                else {
                                    MIR_BUG(state, "DstMeta requires a &-ptr as input, got " << ty);
                                }
                                const auto& ity = *ity_p;
                                HIR::TypeRef res_ty;
                                if (ity->is_Generic() || (ity->is_Path() && ity->as_Path().binding.is_Opaque()))
                                    ;
                                else if (ity->is_Array()) {
                                    res_ty = state.m_crate.m_types.primitive(HIR::CoreType::Usize);
                                } else if (ity->is_Slice()) {
                                    res_ty = state.m_crate.m_types.primitive(HIR::CoreType::Usize);
                                } else if (ity->is_TraitObject())
                                    ;
                                else if (ity->is_Path()) {
                                    // TODO: Check DST type of this path
                                } else {
                                }
                                // TODO: Check return type
                            }
                            TU_ARMA(DstPtr, e) {
                                ::HIR::TypeRef tmp;
                                const auto& ty = state.get_lvalue_type(tmp, e.val);
                                const ::HIR::TypeRef* ity_p = nullptr;
                                if ((ity_p = state.is_type_owned_box(ty)))
                                    ;
                                else if (ty->is_Borrow())
                                    ity_p = &ty->as_Borrow().inner;
                                else if (ty->is_Pointer())
                                    ity_p = &ty->as_Pointer().inner;
                                else {
                                    MIR_BUG(state, "DstPtr requires a &-ptr as input, got " << ty);
                                }
                                const auto& ity = *ity_p;
                                if (ity->is_Slice() || (ity->is_Primitive() && ity->as_Primitive() == HIR::CoreType::Str))
                                    ;
                                else if (ity->is_TraitObject())
                                    ;
                                else if (ity->is_Generic() || (ity->is_Path() && ity->as_Path().binding.is_Opaque()))
                                    ;
                                else if (ity->is_Path()) {
                                    // TODO: Check DST type of this path
                                } else {
                                    MIR_BUG(state, "DstPtr on invalid type - " << ity);
                                }
                                // TODO: Check return type
                            }
                            TU_ARMA(MakeDst, e) {
                                if (TU_TEST2(e.meta_val, Constant, , ItemAddr, .get() == nullptr)) {
                                    // TODO: Check the validity?
                                    // - Ensure that something is generic in either the destination or source
                                    //::HIR::TypeRef  tmp;
                                    //const auto& src_ty = state.get_param_type(tmp, e.ptr_val);
                                    //MIR_ASSERT(state, monomorphise_type_needed(src_ty), "MakeDst Unsize with known source - " << src_ty);
                                    break;
                                }
                                const ::HIR::TypeRef* ity_p = nullptr;
                                if (const auto* te = dst_ty->opt_Borrow())
                                    ity_p = &te->inner;
                                else if (const auto* te = dst_ty->opt_Pointer())
                                    ity_p = &te->inner;
                                else {
                                    MIR_BUG(state, "MakeDst requires a pointer as output, got " << dst_ty);
                                }
                                assert(ity_p);
                                auto meta = get_metadata_type(state, *ity_p);
                                if (meta == ::HIR::TypeRef()) {
                                    // In 1.90, this gets used for thin pointers too
                                    meta = types.unit();
                                }
// TODO: Check metadata type?
// > Borrows vs pointers are fun

                                // NOTE: Output type checked above.
                            }
                            TU_ARMA(Tuple, e) {
                                if (!dst_ty->is_Tuple())
                                    MIR_BUG(state, "Tuple assigned slot of invalid type, " << dst_ty);
                                const auto& dst_itys = dst_ty->as_Tuple();
                                if (dst_itys.size() != e.vals.size())
                                    MIR_BUG(state, "Tuple assigned slot of invalid type, " << dst_ty << " - expected " << e.vals.size() << " elements");
                                for (size_t i = 0; i < e.vals.size(); i++) {
                                    ::HIR::TypeRef tmp2;
                                    check_types(dst_itys[i], state.get_param_type(tmp2, e.vals[i]));
                                }
                            }
                            TU_ARMA(Array, e) {
                                // TODO: Check return type
                            }
                            TU_ARMA(UnionVariant, e) {
                                // TODO: Check return type
                            }
                            TU_ARMA(EnumVariant, e) {
                                // TODO: Check return type
                            }
                            TU_ARMA(Struct, e) {
                                // TODO: Check return type
                            }
                    }
                    } break;
                    case ::MIR::Statement::TAG_Asm:
                    case ::MIR::Statement::TAG_Asm2:
                        // TODO: Ensure that values are all thin pointers or integers?
                        break;
                    case ::MIR::Statement::TAG_Drop: {
                        const auto& se = stmt.as_Drop();
                        // TODO: Anything need checking here?
                        // - Check the path to see if this has mutable/owned access
                        if (se.slot.is_Deref()) {
                            HIR::TypeRef tmp;
                            const auto& ty = state.get_lvalue_type(tmp, se.slot, 1);
                            if (ty->is_Borrow()) {
                                // Note: Dropping through `&mut` happens when assigning
                                MIR_ASSERT(state, ty->as_Borrow().type != HIR::BorrowType::Shared, "Droping though non-owned pointer: " << ty);
                            }
                        }
                    } break;
                    case ::MIR::Statement::TAG_ScopeEnd:
                        // TODO: Mark listed values as descoped
                        break;
                }
            }

            state.set_cur_stmt_term(bb_idx);
            DEBUG(state << bb.terminator);
            TU_MATCH_HDRA( (bb.terminator), {)
            TU_ARMA(Incomplete, e) {
                }
                TU_ARMA(Return, e) {
                    // TODO: Check if the function can return (i.e. if its return type isn't an empty type)
                }
                TU_ARMA(Diverge, e) {
                }
                TU_ARMA(Goto, e) {
                }
                TU_ARMA(Panic, e) {
                }
                TU_ARMA(If, e) {
                    // Check that condition lvalue is a bool
                    ::HIR::TypeRef tmp;
                    const auto& ty = state.get_lvalue_type(tmp, e.cond);
                    if (ty != ::HIR::CoreType::Bool) {
                        MIR_BUG(state, "Type mismatch in `If` - expected bool, got " << ty);
                    }
                }
                TU_ARMA(Switch, e) {
                    // Check that the condition is an enum
                    MIR_ASSERT(state, (e.valid_flag == ~0u) == (e.invalid_target == ~0u), "Conditional switch flag/target mismatch");
                    if (e.valid_flag != ~0u) {
                        MIR_ASSERT(state, e.valid_flag < fcn.drop_flags.size(), "Conditional switch flag out of range");
                        MIR_ASSERT(state, e.invalid_target < fcn.blocks.size(), "Conditional switch target out of range");
                    }
                }
                TU_ARMA(SwitchValue, e) {
                    // Check that the condition's type matches the values
                }
                TU_ARMA(Call, e) {
                    if (e.fcn.is_Value()) {
                        ::HIR::TypeRef tmp;
                        const auto& ty = state.get_lvalue_type(tmp, e.fcn.as_Value());
                        if (!ty->is_Function()) {
                            MIR_BUG(state, "Call Fcn::Value with non-function type - " << ty);
                        }
                        // NOTE: VTable functions use this, and have a little bit of type shenanigans going on
                    } else if (e.fcn.is_Path()) {
                        const auto& p = e.fcn.as_Path();

                        MonomorphState out_params(types);
                        out_params.set_consteval_state(state.m_crate, HIR::ItemPath(p));
                        const auto& sig = state.m_resolve.get_value(sp, p, out_params, /*sig_only=*/true);
                        MIR_ASSERT(state, sig.is_Function(), "Call Fcn::Path with non-function value - " << p << " is " << sig.tag_str());
                        const auto& fcn = *sig.as_Function();

                        ::HIR::TypeRef tmp1;
                        ::HIR::TypeRef tmp2;
                        auto maybe_monomorph = [&](const ::HIR::TypeRef& ty) -> const ::HIR::TypeRef& {
                            if (true || monomorphise_type_needed(ty)) {
                                tmp2 = out_params.monomorph_type(sp, ty);
                                state.m_resolve.expand_associated_types(sp, tmp2);
                                return tmp2;
                            } else {
                                return ty;
                            }
                        };
                        // Check arguments
                        if (fcn.m_variadic) {
                            MIR_ASSERT(state, e.args.size() >= fcn.m_args.size(), "");
                        } else {
                            MIR_ASSERT(state, e.args.size() == fcn.m_args.size(), "");
                        }
                        for (size_t i = 0; i < fcn.m_args.size(); i++) {
                            const auto& in_ty = state.get_param_type(tmp1, e.args[i]);
                            const auto& exp_ty = maybe_monomorph(fcn.m_args[i].second);
                            DEBUG("Arg " << i << " " << in_ty << " ?= " << exp_ty);
                            if (in_ty == types.diverge()) {
                                // It's valid to assign to anything from a !
                            } else if (in_ty == exp_ty || in_ty->equals_ignoring_regions(exp_ty)) {
                                // Types are equal, good.
                            } else {
                                MIR_BUG(state, "Argument (" << i << ") type mismatch: input is " << in_ty << ", but expected is " << exp_ty);
                            }
                        }
                        // Check return
                        const auto& slot_ty = state.get_lvalue_type(tmp1, e.ret_val);
                        const auto& exp_ty = maybe_monomorph(fcn.m_return);
                        DEBUG("Ret " << slot_ty << " ?= " << exp_ty);
                        if (!exp_ty->is_Diverge()) {
                            MIR_ASSERT(state, slot_ty == exp_ty || slot_ty->equals_ignoring_regions(exp_ty), "Return type mismatch: slot is " << slot_ty << ", but return is " << exp_ty);
                        }
                    }
                    // Typecheck arguments and return value
                }
            }
        }
    }

    // [ValState] = Value state tracking (use after move, uninit, ...)
    MIR_Validate_ValState(state, fcn);
}

// --------------------------------------------------------------------

void MIR_CheckCrate(/*const*/ ::HIR::Crate& crate) {
    ::MIR::OuterVisitor ov(crate, [](const auto& res, const auto& p, auto& expr, const auto& args, const auto& ty) {
        MIR_Validate(res, p, *expr.m_mir, args, ty);
    });
    ov.visit_crate(crate);
}

#include "mir_main_bindings.h"
#include "mir_mir.h"
#include "hir_visitor.h"
#include "hir_typeck_static.h"
#include "mir_helpers.h"
#include "mir_visit_crate_mir.h"

namespace {
    struct State {
        // 0 = invalid
        // -1 = valid
        // other = 1-based index into `inner_states`
        unsigned int index;

        explicit State(const State&) = default;
        State(State&& x) = default;
        State& operator=(const State&) = delete;
        State& operator=(State&&) = default;

        State()
            : index(0)
        {
        }

        State(bool valid)
            : index(valid ? ~0u : 0)
        {
        }

        State(size_t idx)
            : index(idx + 1)
        {
        }

        bool is_composite() const {
            return index != 0 && index != ~0u;
        }

        bool is_valid() const {
            return index != 0;
        }

        bool operator==(const State& x) const {
            return index == x.index;
        }

        bool operator!=(const State& x) const {
            return !(*this == x);
        }
    };

    struct ValueStates;
}

struct StateFmt {
    const ValueStates& vss;
    const State& s;

    StateFmt(const ValueStates& vss, const State& s)
        : vss(vss)
        , s(s)
    {
    }
};

::std::ostream& operator<<(::std::ostream& os, const StateFmt& x);

namespace {
    struct ValueStates {
        State return_value;
        ::std::vector<State> args;
        ::std::vector<State> locals;
        ::std::vector<bool> drop_flags;

        ::std::vector<::std::vector<State>> inner_states;

        ::std::vector<unsigned int> bb_path;

        ValueStates clone() const {
            struct H {
                static ::std::vector<State> clone_state_list(const ::std::vector<State>& l) {
                    ::std::vector<State> rv;
                    rv.reserve(l.size());
                    for (const auto& s : l) {
                        rv.push_back(State(s));
                    }
                    return rv;
                }
            };

            ValueStates rv;
            rv.return_value = State(this->return_value);
            rv.args = H::clone_state_list(this->args);
            rv.locals = H::clone_state_list(this->locals);
            rv.drop_flags = this->drop_flags;
            rv.inner_states.reserve(this->inner_states.size());
            for (const auto& isl : this->inner_states) {
                rv.inner_states.push_back(H::clone_state_list(isl));
            }
            rv.bb_path = this->bb_path;
            return *this;
        }

        bool is_equivalent_to(const ValueStates& x) const {
            struct H {
                static bool equal(const ValueStates& vss_a, const State& a, const ValueStates& vss_b, const State& b) {
                    if (a.index == 0) {
                        return b.index == 0;
                    }
                    if (a.index == ~0u) {
                        return b.index == ~0u;
                    }
                    if (b.index == 0 || b.index == ~0u) {
                        return false;
                    }

                    const auto& states_a = vss_a.inner_states.at(a.index - 1);
                    const auto& states_b = vss_b.inner_states.at(b.index - 1);
                    // NOTE: If there's two differen variants, this can happen.
                    if (states_a.size() != states_b.size()) {
                        return false;
                    }

                    for (size_t i = 0; i < states_a.size(); i++) {
                        if (!H::equal(vss_a, states_a[i], vss_b, states_b[i])) {
                            return false;
                        }
                    }
                    // If the above loop didn't early exit, the two states are equal
                    return true;
                }
            };

            if (this->drop_flags != x.drop_flags) {
                return false;
            }
            if (!H::equal(*this, return_value, x, x.return_value)) {
                return false;
            }

            assert(args.size() == x.args.size());
            for (size_t i = 0; i < args.size(); i++) {
                if (!H::equal(*this, args[i], x, x.args[i])) {
                    return false;
                }
            }

            assert(locals.size() == x.locals.size());
            for (size_t i = 0; i < locals.size(); i++) {
                if (!H::equal(*this, locals[i], x, x.locals[i])) {
                    return false;
                }
            }
            return true;
        }

        StateFmt fmt_state(const ::MIR::TypeResolve& mir_res, const ::MIR::LValue& lv) const {
            return StateFmt(*this, get_lvalue_state(mir_res, lv));
        }

        void ensure_param_valid(const ::MIR::TypeResolve& mir_res, const ::MIR::Param& lv) const {
            if (const auto* e = lv.opt_LValue()) {
                this->ensure_lvalue_valid(mir_res, *e);
            }
        }

        void ensure_lvalue_valid(const ::MIR::TypeResolve& mir_res, const ::MIR::LValue& lv) const {
            const auto& vs = get_lvalue_state(mir_res, lv);
            ::std::vector<unsigned int> path;
            ensure_valid(mir_res, lv, vs, path);
        }

    private:
        struct InvalidReason {
            enum {
                Unwritten,
                Moved,
                Invalidated,
            } ty;

            size_t bb;
            size_t stmt;

            void fmt(::std::ostream& os) const {
                switch (this->ty) {
                    case Unwritten:
                        os << "Not Written";
                        break;
                    case Moved:
                        os << "Moved at BB" << bb << "/" << stmt;
                        break;
                    case Invalidated:
                        os << "Invalidated at BB" << bb << "/" << stmt;
                        break;
                }
            }
        };

        InvalidReason find_invalid_reason(const ::MIR::TypeResolve& mir_res, const ::MIR::LValue& root_lv) const {
            using ::MIR::visit::ValUsage;
            using ::MIR::visit::visit_mir_lvalues;

            ::HIR::TypeRef tmp;
            bool is_copy = mir_res.lvalue_is_copy(root_lv);
            size_t cur_stmt = mir_res.get_cur_stmt_ofs();

            // Dump all statements
            if (true) {
                for (size_t i = 0; i < this->bb_path.size() - 1; i++) {
                    size_t bb_idx = this->bb_path[i];
                    const auto& bb = mir_res.m_fcn.blocks.at(bb_idx);

                    for (size_t stmt_idx = 0; stmt_idx < bb.statements.size(); stmt_idx++) {
                        DEBUG("BB" << bb_idx << "/" << stmt_idx << " - " << bb.statements[stmt_idx]);
                    }
                    DEBUG("BB" << bb_idx << "/TERM - " << bb.terminator);
                }

                {
                    size_t bb_idx = this->bb_path.back();
                    const auto& bb = mir_res.m_fcn.blocks.at(bb_idx);
                    for (size_t stmt_idx = 0; stmt_idx < cur_stmt; stmt_idx++) {
                        DEBUG("BB" << bb_idx << "/" << stmt_idx << " - " << bb.statements[stmt_idx]);
                    }
                }
            }

            if (!is_copy) {
                // Walk backwards through the BBs and find where it's used by value
                assert(this->bb_path.size() > 0);
                size_t bb_idx;
                size_t stmt_idx;

                bool was_moved = false;
                size_t moved_bb, moved_stmt;
                auto visit_cb = [&](const auto& lv, auto vu) {
                    // If this is a move that touches the slot of interest (in part or full)
                    // e.g. if `root_lv` is `_1.0` then `_1` and `_1.0*` should be handled, but `_1.1` should not
                    if (lv.is_either_subset(root_lv) && vu == ValUsage::Move) {
                        was_moved = true;
                        moved_bb = bb_idx;
                        moved_stmt = stmt_idx;
                        return false;
                    }
                    return false;
                };
                // Most recent block (incomplete)
                {
                    bb_idx = this->bb_path.back();
                    const auto& bb = mir_res.m_fcn.blocks.at(bb_idx);
                    for (stmt_idx = cur_stmt; stmt_idx-- && !was_moved;) {
                        visit_mir_lvalues(bb.statements[stmt_idx], visit_cb);
                    }
                }
                for (size_t i = this->bb_path.size() - 1; i-- && !was_moved;) {
                    bb_idx = this->bb_path[i];
                    const auto& bb = mir_res.m_fcn.blocks.at(bb_idx);
                    stmt_idx = bb.statements.size();

                    visit_mir_lvalues(bb.terminator, visit_cb);

                    for (stmt_idx = bb.statements.size(); stmt_idx-- && !was_moved;) {
                        visit_mir_lvalues(bb.statements[stmt_idx], visit_cb);
                    }
                }

                if (was_moved) {
                    // Reason found, the value was moved
                    DEBUG("- Moved in BB" << moved_bb << "/" << moved_stmt);
                    return InvalidReason{InvalidReason::Moved, moved_bb, moved_stmt};
                }
            } else {
                // Walk backwards to find assignment (if none, it's never initialized)
                assert(this->bb_path.size() > 0);
                size_t bb_idx;
                size_t stmt_idx;

                bool assigned = false;
                auto visit_cb = [&](const auto& lv, auto vu) {
                    if (lv.is_either_subset(root_lv) && vu == ValUsage::Write) {
                        assigned = true;
                        //assigned_bb = this->bb_path[i];
                        //assigned_stmt = j;
                        return true;
                    }
                    return false;
                };

                // Most recent block (incomplete)
                {
                    bb_idx = this->bb_path.back();
                    const auto& bb = mir_res.m_fcn.blocks.at(bb_idx);
                    for (stmt_idx = cur_stmt; stmt_idx-- && !assigned;) {
                        visit_mir_lvalues(bb.statements[stmt_idx], visit_cb);
                    }
                }
                for (size_t i = this->bb_path.size() - 1; i-- && !assigned;) {
                    bb_idx = this->bb_path[i];
                    const auto& bb = mir_res.m_fcn.blocks.at(bb_idx);
                    stmt_idx = bb.statements.size();

                    visit_mir_lvalues(bb.terminator, visit_cb);

                    for (stmt_idx = bb.statements.size(); stmt_idx-- && !assigned;) {
                        visit_mir_lvalues(bb.statements[stmt_idx], visit_cb);
                    }
                }

                if (!assigned) {
                    // Value wasn't ever assigned, that's why it's not valid.
                    DEBUG("- Not assigned");
                    return InvalidReason{InvalidReason::Unwritten, 0, 0};
                }
            }
            // If neither of the above return a reason, check for blocks that don't have the value valid.
            // TODO: This requires access to the lifetime bitmaps to know where it was invalidated
            DEBUG("- (assume) lifetime invalidated [is_copy=" << is_copy << "]");
            return InvalidReason{InvalidReason::Invalidated, 0, 0};
        }

        void ensure_valid(const ::MIR::TypeResolve& mir_res, const ::MIR::LValue& root_lv, const State& vs, ::std::vector<unsigned int>& path) const {
            if (vs.is_composite()) {
                MIR_ASSERT(mir_res, vs.index - 1 < this->inner_states.size(), "");
                const auto& states = this->inner_states.at(vs.index - 1);

                path.push_back(0);
                for (const auto& inner_vs : states) {
                    ensure_valid(mir_res, root_lv, inner_vs, path);
                    path.back()++;
                }
                path.pop_back();
            } else if (!vs.is_valid()) {
                // Locate where it was invalidated.
                auto reason = find_invalid_reason(mir_res, root_lv);
                MIR_BUG(mir_res, "Accessing invalidated lvalue - " << root_lv << " - " << FMT_CB(s, reason.fmt(s);) << " - field path=[" << path << "], BBs=[" << this->bb_path << "]");
            } else {
            }
        }

    public:
        void move_lvalue(const ::MIR::TypeResolve& mir_res, const ::MIR::LValue& lv) {
            this->ensure_lvalue_valid(mir_res, lv);

            if (mir_res.lvalue_is_copy(lv)) {
                // NOTE: Copy types aren't moved.
            } else {
                this->set_lvalue_state(mir_res, lv, State(false));
            }
        }

        void mark_lvalue_valid(const ::MIR::TypeResolve& mir_res, const ::MIR::LValue& lv) {
            this->set_lvalue_state(mir_res, lv, State(true));
        }

        // Scan states and clear unused composite slots
        void garbage_collect() {
            struct Marker {
                ::std::vector<bool> used;

                void mark_from_state(const ValueStates& vss, const State& s) {
                    if (s.is_composite()) {
                        used.at(s.index - 1) = true;
                        for (const auto& is : vss.inner_states.at(s.index - 1)) {
                            mark_from_state(vss, is);
                        }

                        // TODO: Should this compact composites with all-equal inner states?
                    }
                }
            };

            Marker m;
            m.used.resize(this->inner_states.size(), false);

            m.mark_from_state(*this, this->return_value);
            for (const auto& s : this->args) {
                m.mark_from_state(*this, s);
            }
            for (const auto& s : this->locals) {
                m.mark_from_state(*this, s);
            }
        }

    private:
        ::std::vector<State>& allocate_composite_int(State& out_state) {
            // 1. Search for an unused (empty) slot
            for (size_t i = 0; i < this->inner_states.size(); i++) {
                if (this->inner_states[i].size() == 0) {
                    out_state = State(i);
                    return inner_states[i];
                }
            }
            // 2. If none avaliable, allocate a new slot
            auto idx = inner_states.size();
            inner_states.push_back({});
            out_state = State(idx);
            return inner_states.back();
        }

        State allocate_composite(unsigned int n_fields, const State& basis) {
            assert(n_fields > 0);
            assert(!basis.is_composite());

            State rv;
            auto& sub_states = allocate_composite_int(rv);
            assert(sub_states.size() == 0);

            sub_states.reserve(n_fields);
            while (n_fields--) {
                sub_states.push_back(State(basis));
            }

            return rv;
        }

    public:
        ::std::vector<State>& get_composite(const ::MIR::TypeResolve& mir_res, const State& vs) {
            MIR_ASSERT(mir_res, vs.index != 0, "No inner state");
            MIR_ASSERT(mir_res, vs.index - 1 < this->inner_states.size(), "Inner state index out of range - " << vs.index - 1 << " >= " << this->inner_states.size());
            return this->inner_states.at(vs.index - 1);
        }

        const ::std::vector<State>& get_composite(const ::MIR::TypeResolve& mir_res, const State& vs) const {
            MIR_ASSERT(mir_res, vs.index != 0, "No inner state");
            MIR_ASSERT(mir_res, vs.index - 1 < this->inner_states.size(), "Inner state index out of range - " << vs.index - 1 << " >= " << this->inner_states.size());
            return this->inner_states.at(vs.index - 1);
        }

        const State& get_lvalue_state(const ::MIR::TypeResolve& mir_res, const ::MIR::LValue& lv) const {
            const State* state_p = nullptr;
            TU_MATCHA((lv.m_root), (e), (Return, state_p = &return_value;), (Argument, state_p = &args.at(e);), (Local, state_p = &locals.at(e);), (Static, static State state_of_static(true); return state_of_static;))

            for (const auto& w : lv.m_wrappers) {
                if (w.is_Index()) {
                    const auto& vs_i = get_lvalue_state(mir_res, ::MIR::LValue::new_Local(w.as_Index()));
                    MIR_ASSERT(mir_res, vs_i.is_valid(), "Indexing with an invalidated value");
                }
            }
            for (const auto& w : lv.m_wrappers) {
                if (!state_p->is_composite()) {
                    // Not a composite, stop immediately
                    break;
                }
                const auto& vs = *state_p;
                state_p = nullptr;

                TU_MATCHA(
                    (w),
                    (e),
                    (Field, const auto& states = this->get_composite(mir_res, vs); MIR_ASSERT(mir_res, e < states.size(), "Field index out of range"); state_p = &states[e];),
                    (Deref,
                     //MIR_TODO(mir_res, "Deref with composite state - " << lv);
                     const auto& states = this->get_composite(mir_res, vs);
                     MIR_ASSERT(mir_res, states.size() == 2, "Deref on composite of invalid size - " << StateFmt(*this, vs));
                     state_p = &states[1];),
                    (Index, MIR_BUG(mir_res, "Indexing a composite state");),
                    (Downcast, const auto& states = this->get_composite(mir_res, vs); MIR_ASSERT(mir_res, states.size() == 1, "Downcast on composite of invalid size - " << StateFmt(*this, vs)); state_p = &states[0];)
                )
                assert(state_p);
            }
            return *state_p;
        }

        void clear_state(const ::MIR::TypeResolve& mir_res, State& s) {
            if (s.is_composite()) {
                auto& sub_states = this->get_composite(mir_res, s);
                for (auto& ss : sub_states) {
                    this->clear_state(mir_res, ss);
                }
                sub_states.clear();
            }
        }

        void set_lvalue_state(const ::MIR::TypeResolve& mir_res, const ::MIR::LValue& lv, State new_vs) {
            TRACE_FUNCTION_F(lv << " = " << StateFmt(*this, new_vs) << " (from " << StateFmt(*this, get_lvalue_state(mir_res, lv)) << ")");
            State* state_p = nullptr;
            TU_MATCHA((lv.m_root), (e), (Return, state_p = &return_value;), (Argument, state_p = &args.at(e);), (Local, state_p = &locals.at(e);), (Static, return;))

            for (const auto& w : lv.m_wrappers) {
                auto& cur_vs = *state_p;

                // If this is not a composite, and it matches the new state
                if (!cur_vs.is_composite() && cur_vs == new_vs) {
                    // Early return
                    return;
                }

                state_p = nullptr;
                TU_MATCH_HDRA( (w), {)
                TU_ARMA(Field, e) {
                        // Current isn't a composite, we need to change that
                        if (!cur_vs.is_composite()) {
                            ::HIR::TypeRef tmp;
                            const auto& ty = mir_res.get_lvalue_type(tmp, lv, /*wrapper_skip_count=*/(1 + &lv.m_wrappers.back() - &w));
                            unsigned int n_fields = 0;
                            if (const auto* e = ty->opt_Tuple()) {
                                n_fields = e->size();
                            }
                            // TODO: Fixed-size arrays
                            else if (ty->is_Path() && ty->as_Path().binding.is_Struct()) {
                                const auto& e = ty->as_Path().binding.as_Struct();
                                TU_MATCHA((e->m_data), (se), (Unit, n_fields = 0;), (Tuple, n_fields = se.size();), (Named, n_fields = se.size();))
                            } else {
                                MIR_BUG(mir_res, "Unknown type being accessed with Field " << lv << ": " << ty);
                            }

                            cur_vs = State(this->allocate_composite(n_fields, cur_vs));
                        }
                        // Get composite state and assign into it
                        auto& states = this->get_composite(mir_res, cur_vs);
                        MIR_ASSERT(mir_res, e < states.size(), "Field index out of range");
                        state_p = &states[e];
                    }
                    TU_ARMA(Deref, e) {
                        if (!cur_vs.is_composite()) {
                            cur_vs = State(this->allocate_composite(2, cur_vs));
                        }
                        // Get composite state and assign into it
                        auto& states = this->get_composite(mir_res, cur_vs);
                        MIR_ASSERT(mir_res, states.size() == 2, "Deref with invalid state list size");
                        state_p = &states[1];
                    }
                    TU_ARMA(Index, e) {
                        const auto& vs_i = get_lvalue_state(mir_res, ::MIR::LValue::new_Local(e));
                        MIR_ASSERT(mir_res, !cur_vs.is_composite(), "");
                        MIR_ASSERT(mir_res, !vs_i.is_composite(), "");

                        MIR_ASSERT(mir_res, cur_vs.is_valid(), "Indexing an invalid value");
                        MIR_ASSERT(mir_res, vs_i.is_valid(), "Indexing with an invalid index");

                        // NOTE: Ignore
                        return;
                    }
                    TU_ARMA(Downcast, e) {
                        if (!cur_vs.is_composite()) {
                            cur_vs = State(this->allocate_composite(1, cur_vs));
                        }
                        // Get composite state and assign into it
                        auto& states = this->get_composite(mir_res, cur_vs);
                        MIR_ASSERT(mir_res, states.size() == 1, "Downcast on composite of invalid size - " << lv << " - " << StateFmt(*this, cur_vs));
                        state_p = &states[0];
                    }
                }
                MIR_ASSERT(mir_res, state_p, "No state result?");
            }
            this->clear_state(mir_res, *state_p);
            *state_p = mv$(new_vs);
        }
    };

    struct StateSet {
        ::std::vector<ValueStates> known_state_sets;

        bool add_state(const ValueStates& state_set) {
            for (const auto& s : this->known_state_sets) {
                if (s.is_equivalent_to(state_set)) {
                    return false;
                }
            }
            this->known_state_sets.push_back(state_set.clone());
            this->known_state_sets.back().bb_path = ::std::vector<unsigned int>();
            return true;
        }
    };
}

::std::ostream& operator<<(::std::ostream& os, const StateFmt& x) {
    if (x.s.index == 0) {
        os << "_";
    } else if (x.s.index == ~0u) {
        os << "X";
    } else {
        assert(x.s.index - 1 < x.vss.inner_states.size());
        const auto& is = x.vss.inner_states[x.s.index - 1];
        os << "[";
        for (const auto& s : is) {
            os << StateFmt(x.vss, s);
        }
        os << "]";
    }
    return os;
}

namespace std {
    ostream& operator<<(ostream& os, const ValueStates& x) {
        auto print_val = [&](auto tag, const State& s) {
            if (s.is_composite()) {
                os << tag << "=" << StateFmt(x, s);
            } else if (s.is_valid()) {
                os << tag;
            } else {
            }
        };

        os << "ValueStates(path=[" << x.bb_path << "]";
        print_val(",rv", x.return_value);
        for (unsigned int i = 0; i < x.args.size(); i++) {
            print_val(FMT_CB(ss, ss << ",a" << i;), x.args[i]);
        }
        for (unsigned int i = 0; i < x.locals.size(); i++) {
            print_val(FMT_CB(ss, ss << ",_" << i;), x.locals[i]);
        }
        for (unsigned int i = 0; i < x.drop_flags.size(); i++) {
            if (x.drop_flags[i]) {
                os << ",df" << i;
            }
        }
        os << ")";
        return os;
    }
}

// "Executes" the function, keeping track of drop flags and variable validities
void MIR_Validate_FullValState(::MIR::TypeResolve& mir_res, const ::MIR::Function& fcn) {
    // TODO: Use a timer to check elapsed CPU time in this function, and check on each iteration
    // - If more than `n` (10?) seconds passes on one function, warn and abort
    //ElapsedTimeCounter    timer;
    ::std::vector<unsigned> block_ref_counts(fcn.blocks.size());
    ::std::vector<StateSet> block_entry_states(fcn.blocks.size());

    // Determine value lifetimes (BBs in which Copy values are valid)
    // - Used to mask out Copy value (prevents combinatorial explosion)
    auto lifetimes = MIR_Helper_GetLifetimes(mir_res, fcn, /*dump_debug=*/true);
    DEBUG(lifetimes.m_block_offsets);

    ValueStates state;

    struct H {
        static ::std::vector<State> make_list(size_t n, bool pop) {
            ::std::vector<State> rv;
            rv.reserve(n);
            while (n--) {
                rv.push_back(State(pop));
            }
            return rv;
        }
    };

    state.args = H::make_list(mir_res.m_args.size(), true);
    state.locals = H::make_list(fcn.locals.size(), false);
    state.drop_flags = fcn.drop_flags;

    block_ref_counts[0] = 1;
    for (const auto& blk : fcn.blocks) {
        MIR::visit::visit_terminator_target(blk.terminator, [&](const ::MIR::BasicBlockId& e) {
            block_ref_counts.at(e) += 1;
        });
    }

    ::std::vector<::std::pair<unsigned int, ValueStates>> todo_queue;
    todo_queue.push_back(::std::make_pair(0, mv$(state)));
    while (!todo_queue.empty()) {
        auto cur_block = todo_queue.back().first;
        auto state = mv$(todo_queue.back().second);
        todo_queue.pop_back();

        // Mask off any values which aren't valid in the first statement of this block
        {
            for (unsigned i = 0; i < state.locals.size(); i++) {
                /*if( !variables_copy[i] )
                {
                    // Not Copy, don't apply masking
                }
                else*/
                if (!state.locals[i].is_valid()) {
                    // Already invalid
                } else if (lifetimes.slot_valid(i, cur_block, 0)) {
                    // Expected to be valid in this block, leave as-is
                } else {
                    // Copy value not used at/after this block, mask to false
                    DEBUG("BB" << cur_block << " - _" << i << " - Outside lifetime, discard");
                    state.locals[i] = State(false);
                }
            }
        }

        // If this state already exists in the map, skip
        // - Note: The `block_ref_counts` check saves a tiny bit of time, but not a huge amount
        if (block_ref_counts[cur_block] > 1 && !block_entry_states[cur_block].add_state(state)) {
            DEBUG("BB" << cur_block << " - Nothing new");
            continue;
        }
        DEBUG("BB" << cur_block << " - " << state);
        state.bb_path.push_back(cur_block);

        const auto& blk = fcn.blocks.at(cur_block);
        for (size_t i = 0; i < blk.statements.size(); i++) {
            mir_res.set_cur_stmt(cur_block, i);

            DEBUG(mir_res << blk.statements[i] << " " << state);

            TU_MATCH_HDRA( (blk.statements[i]), {)
            TU_ARMA(Assign, se) {
                    TU_MATCHA(
                        (se.src),
                        (ve),
                        (Use, state.move_lvalue(mir_res, ve);),
                        (Constant, ),
                        (SizedArray, state.ensure_param_valid(mir_res, ve.val);),
                        (Borrow, state.ensure_lvalue_valid(mir_res, ve.val);),
                        // Cast on primitives
                        (Cast, state.ensure_lvalue_valid(mir_res, ve.val);),
                        // Binary operation on primitives
                        (BinOp, state.ensure_param_valid(mir_res, ve.val_l); state.ensure_param_valid(mir_res, ve.val_r);),
                        // Unary operation on primitives
                        (UniOp, state.ensure_lvalue_valid(mir_res, ve.val);),
                        // Extract the metadata from a DST pointer
                        // NOTE: If used on an array, this yields the array size (for generics)
                        (DstMeta, state.ensure_lvalue_valid(mir_res, ve.val);),
                        // Extract the pointer from a DST pointer (as *const ())
                        (DstPtr, state.ensure_lvalue_valid(mir_res, ve.val);),
                        // Construct a DST pointer from a thin pointer and metadata
                        (MakeDst, state.ensure_param_valid(mir_res, ve.ptr_val); state.ensure_param_valid(mir_res, ve.meta_val);),
                        (Tuple, for (const auto& v : ve.vals) if (const auto* e = v.opt_LValue()) state.move_lvalue(mir_res, *e);),
                        // Array literal
                        (Array, for (const auto& v : ve.vals) if (const auto* e = v.opt_LValue()) state.move_lvalue(mir_res, *e);),
                        // Create a new instance of a union
                        (UnionVariant, if (const auto* e = ve.val.opt_LValue()) state.move_lvalue(mir_res, *e);),
                        (EnumVariant, for (const auto& v : ve.vals) if (const auto* e = v.opt_LValue()) state.move_lvalue(mir_res, *e);),
                        // Create a new instance of a struct (or enum)
                        (Struct, for (const auto& v : ve.vals) if (const auto* e = v.opt_LValue()) state.move_lvalue(mir_res, *e);)
                    )
                    state.mark_lvalue_valid(mir_res, se.dst);
                }
                TU_ARMA(Asm, se) {
                    for (const auto& v : se.inputs) {
                        state.ensure_lvalue_valid(mir_res, v.second);
                    }
                    for (const auto& v : se.outputs) {
                        state.mark_lvalue_valid(mir_res, v.second);
                    }
                }
                TU_ARMA(Asm2, se) {
                    for (const auto& p : se.params) {
                    TU_MATCH_HDRA( (p), { )
                    TU_ARMA(Const, v) {
                            }
                            TU_ARMA(Sym, v) {
                            }
                            TU_ARMA(Reg, v) {
                                if (v.input) {
                                    state.ensure_param_valid(mir_res, *v.input);
                                }
                                if (v.output) {
                                    state.mark_lvalue_valid(mir_res, *v.output);
                                }
                            }
                    }
                    }
                }
                TU_ARMA(SetDropFlag, se) {
                    if (se.other == ~0u) {
                        state.drop_flags[se.idx] = se.new_val;
                    } else {
                        state.drop_flags[se.idx] = (se.new_val != state.drop_flags[se.other]);
                    }
                }
                TU_ARMA(LoadDropFlag, se) {
                    MIR_TODO(mir_res, "");
                }
                TU_ARMA(SaveDropFlag, se) {
                    MIR_TODO(mir_res, "");
                }
                TU_ARMA(Drop, se) {
                    if (se.flag_idx == ~0u || state.drop_flags.at(se.flag_idx)) {
                        if (se.kind == ::MIR::eDropKind::SHALLOW) {
                            // HACK: A move out of a Box generates the following pattern: `[[[[X_]]X]]`
                            // - Ensure that that is the pattern we're seeing here.
                            const auto& vs = state.get_lvalue_state(mir_res, se.slot);

                            MIR_ASSERT(mir_res, vs.index != ~0u, "Shallow drop on fully-valid value - " << se.slot);

                            // Box<T> - Wrapper around Unique<T>
                            MIR_ASSERT(mir_res, vs.is_composite(), "Shallow drop on non-composite state - " << se.slot << " (state=" << StateFmt(state, vs) << ")");
                            const auto& sub_states = state.get_composite(mir_res, vs);
                            MIR_ASSERT(mir_res, sub_states.size() == 2, "Shallow drop of slot with incorrect state shape (state=" << StateFmt(state, vs) << ")");
                            MIR_ASSERT(mir_res, sub_states[0].is_valid(), "Shallow drop on deallocated Box - " << se.slot << " (state=" << StateFmt(state, vs) << ")");
                            state.set_lvalue_state(mir_res, se.slot, State(false));
                        } else {
                            state.move_lvalue(mir_res, se.slot);
                        }
                    }
                }
                TU_ARMA(ScopeEnd, se) {
                    // TODO: Mark all mentioned variables as invalid
                }
            }
        }

        state.garbage_collect();

        mir_res.set_cur_stmt_term(cur_block);
        DEBUG(mir_res << " " << blk.terminator);
        // TODO: Don't clone/push if the state already exists in the target
        // 1. Check all targets, calling `add_state` and checking result.
        //  - Count number of true results (and which bbs they were)
        TU_MATCHA(
            (blk.terminator),
            (te),
            (Incomplete, ),
            (
                Return, state.ensure_lvalue_valid(mir_res, ::MIR::LValue::new_Return());
            ),
            (Diverge, ),
            (Goto, // Jump to another block
             todo_queue.push_back(::std::make_pair(te, mv$(state)));),
            (Panic, todo_queue.push_back(::std::make_pair(te.dst, mv$(state)));),
            (If, state.ensure_lvalue_valid(mir_res, te.cond); todo_queue.push_back(::std::make_pair(te.bb_true, state.clone())); todo_queue.push_back(::std::make_pair(te.bb_false, mv$(state)));),
            (Switch, if (te.valid_flag != ~0u && !state.drop_flags.at(te.valid_flag)) { todo_queue.push_back(::std::make_pair(te.invalid_target, mv$(state))); } else {
                state.ensure_lvalue_valid(mir_res, te.val);
                for (size_t i = 0; i < te.targets.size(); i++) {
                    todo_queue.push_back(::std::make_pair(te.targets[i], i == te.targets.size() - 1 ? mv$(state) : state.clone()));
                }
            }),
            (SwitchValue, state.ensure_lvalue_valid(mir_res, te.val); for (size_t i = 0; i < te.targets.size(); i++) { todo_queue.push_back(::std::make_pair(te.targets[i], state.clone())); } todo_queue.push_back(::std::make_pair(te.def_target, mv$(state)));),
            (Call, if (const auto* e = te.fcn.opt_Value()) { state.ensure_lvalue_valid(mir_res, *e); } for (auto& arg : te.args) {
                if (const auto* e = arg.opt_LValue()) {
                    state.move_lvalue(mir_res, *e);
                }
            } if (fcn.blocks[te.panic_block].statements.empty() && fcn.blocks[te.panic_block].terminator.is_Diverge()) {
                // Don't bother, it's just an empty block
            } else { todo_queue.push_back(::std::make_pair(te.panic_block, state.clone())); } state.mark_lvalue_valid(mir_res, te.ret_val);
             todo_queue.push_back(::std::make_pair(te.ret_block, mv$(state)));)
        )
    }
}

void MIR_Validate_Full(const StaticTraitResolve& resolve, const ::HIR::ItemPath& path, const ::MIR::Function& fcn, const ::HIR::Function::args_t& args, const ::HIR::TypeRef& ret_type) {
    TRACE_FUNCTION_F(path);
    Span sp;
    ::MIR::TypeResolve state {
        sp, resolve, FMT_CB(ss, ss << path;), ret_type, args, fcn
    };
    // Validation rules:

    MIR_Validate_FullValState(state, fcn);
}

// --------------------------------------------------------------------

void MIR_CheckCrate_Full(/*const*/ ::HIR::Crate& crate) {
    ::MIR::OuterVisitor ov(crate, [](const auto& res, const auto& p, auto& expr, const auto& args, const auto& ty) {
        MIR_Validate_Full(res, p, *expr.m_mir, args, ty);
    });
    ov.visit_crate(crate);
}

#include "mir_main_bindings.h"
#include "mir_mir.h"
#include "hir_visitor.h"
#include "hir_typeck_static.h"
#include "mir_helpers.h"
#include "mir_operations.h"
#include "mir_visit_crate_mir.h"
#include "trans_target.h"
#include <algorithm>

namespace {
    /// @brief Used to tell the constant replacement code that replacements should be available
    bool g_is_post_monomorph = false;
}

class MirMutator {
    ::MIR::Function& m_fcn;
    unsigned int cur_block;
    unsigned int cur_stmt;
    mutable ::std::vector<::MIR::Statement> new_statements;

public:
    MirMutator(::MIR::Function& fcn, unsigned int bb, unsigned int stmt)
        : m_fcn(fcn)
        , cur_block(bb)
        , cur_stmt(stmt)
    {
    }

    void update_state(::MIR::TypeResolve& state) {
        if (this->cur_stmt == m_fcn.blocks[this->cur_block].statements.size()) {
            state.set_cur_stmt_term(this->cur_block);
        } else {
            state.set_cur_stmt(this->cur_block, this->cur_stmt);
        }
    }

    ::MIR::LValue new_temporary(::HIR::TypeRef ty) {
        auto rv = ::MIR::LValue::new_Local(static_cast<unsigned int>(m_fcn.locals.size()));
        m_fcn.locals.push_back(mv$(ty));
        return rv;
    }

    void push_statement(::MIR::Statement stmt) {
        new_statements.push_back(mv$(stmt));
    }

    ::MIR::LValue in_temporary(::HIR::TypeRef ty, ::MIR::RValue val) {
        auto rv = this->new_temporary(mv$(ty));
        push_statement(::MIR::Statement::make_Assign({rv.clone(), mv$(val)}));
        return rv;
    }

    decltype(new_statements.begin()) flush_stmt() {
        auto rv = flush();
        this->cur_stmt += 1;
        return rv;
    }

    void flush_block() {
        flush();
        m_fcn.blocks.at(cur_block).statements.shrink_to_fit();
        this->cur_stmt = 0;
        this->cur_block += 1;
    }

private:
    decltype(new_statements.begin()) flush() {
        auto& block = m_fcn.blocks.at(cur_block);
        assert(cur_stmt <= block.statements.size());
        auto it = block.statements.begin() + cur_stmt;
        if (new_statements.size() > 0) {
            DEBUG("flush - BB" << cur_block << "/" << cur_stmt);
            for (auto& stmt : new_statements) {
                DEBUG("- Push stmt @" << cur_stmt << ": " << stmt);
                it = block.statements.insert(it, mv$(stmt));
                ++it;
                cur_stmt += 1;
            }
            new_statements.clear();
        }
        return it;
    }
};

void MIR_Cleanup_LValue(const ::MIR::TypeResolve& state, MirMutator& mutator, ::MIR::LValue& lval);

namespace {
    ::HIR::TypeRef get_vtable_type(const Span& sp, const ::StaticTraitResolve& resolve, const ::HIR::TypeData::Data_TraitObject& te) {
        return te.m_trait.m_trait_ptr->get_vtable_type(sp, resolve.m_crate, te);
    }
}

const EncodedLiteral* MIR_Cleanup_GetConstant(const MIR::TypeResolve& state, const ::HIR::Path& path, ::HIR::TypeRef& out_ty, MonomorphState& params) {
    TRACE_FUNCTION_F(path);

    auto v = state.m_resolve.get_value(state.sp, path, params);
    if (const auto* e = v.opt_Constant()) {
        const auto& hir_const = **e;
        out_ty = params.monomorph_type(state.sp, hir_const.m_type);
        state.m_resolve.expand_associated_types(state.sp, out_ty);
        switch (hir_const.m_value_state) {
            case HIR::Constant::ValueState::Known:
                return &hir_const.m_value_res;
            case HIR::Constant::ValueState::Generic: {
                // Do some form of lookup of a pre-cached evaluated monomorphised constant
                // - Maybe on the `Constant` entry there can be a list of pre-monomorphised values
                auto it = hir_const.m_monomorph_cache.find(path);
                if (it == hir_const.m_monomorph_cache.end()) {
                    // Emit a bug if the cache is empty? (or if this is in the post-monomorph pass)
                    if (g_is_post_monomorph && !monomorphise_path_needed(path)) {
                        //MIR_BUG(state, "Constant with Defer literal and no cached monomorphisation - " << path);
                        // NOTE: Dead code can trigger this :(
                        // - There's a check in hir/serialise.cpp that makes sure that this doesn't reach the saved MIR
                    }
                    DEBUG("Generic, but no cached monomorphisation: " << hir_const.m_monomorph_cache.size() << " entries");
                    return nullptr;
                }
                return &it->second;
            }
            case HIR::Constant::ValueState::Unknown:
                MIR_ASSERT(state, monomorphise_path_needed(path, true), "Unevaluated constant - " << path);
                return nullptr;
        }
        throw "";
    } else if (v.is_NotYetKnown()) {
        auto v = state.m_resolve.get_value(state.sp, path, params, /*signature_only=*/true);
        if (const auto* e = v.opt_Constant()) {
            const auto& hir_const = **e;
            out_ty = params.monomorph_type(state.sp, hir_const.m_type);
            DEBUG("NotYetKnown");
        } else {
            MIR_BUG(state, "get_literal_for_const - Not a constant - " << path);
        }
        return nullptr;
    } else {
        MIR_BUG(state, "get_literal_for_const - Not a constant - " << path);
        return nullptr;
    }
}

namespace {
    const RcString rcstring_vtable = RcString::new_interned("vtable#");

    bool type_accepts_all_bit_patterns(const Span& sp, const StaticTraitResolve& resolve, const HIR::TypeRef& ty) {
        if (const auto* primitive = ty->opt_Primitive()) {
            return *primitive != HIR::CoreType::Bool && *primitive != HIR::CoreType::Char && *primitive != HIR::CoreType::Str;
        }
        if (const auto* array = ty->opt_Array()) {
            return array->size.as_Known() == 0 || type_accepts_all_bit_patterns(sp, resolve, array->inner);
        }
        if (ty->is_Tuple() || (ty->is_Path() && ty->as_Path().binding.is_Struct())) {
            const auto* repr = Target_GetTypeRepr(sp, resolve, ty);
            if (!repr) {
                return false;
            }
            for (const auto& field : repr->fields) {
                if (!type_accepts_all_bit_patterns(sp, resolve, field.ty)) {
                    return false;
                }
            }
            return true;
        }
        return false;
    }

    ::MIR::Constant create_vtable(HIR::TypeRef ty, const HIR::TraitPath& trait) {
        auto vtable_path = trait.m_hrtbs ? ::HIR::Path(mv$(ty), trait.m_hrtbs->clone(), trait.m_path.clone(), rcstring_vtable) : ::HIR::Path(mv$(ty), trait.m_path.clone(), rcstring_vtable);
        return ::MIR::Constant::make_ItemAddr(box$(vtable_path));
    }
}

::MIR::RValue MIR_Cleanup_LiteralToRValue(const ::MIR::TypeResolve& state, MirMutator& mutator, EncodedLiteralSlice lit, ::HIR::TypeRef ty, const MonomorphState& params, ::HIR::Path path) {
    struct M: Monomorphiser {
        explicit M(HIR::TypeInterner& types): Monomorphiser(types) {}

        ::HIR::TypeRef get_type(const Span& sp, const ::HIR::GenericRef& ty) const override {
            return m_types.generic(ty.name, ty.binding);
        }

        ::HIR::ConstGeneric get_value(const Span& sp, const ::HIR::GenericRef& val) const override {
            return HIR::ConstGeneric(val);
        }

        ::HIR::LifetimeRef get_lifetime(const Span& sp, const ::HIR::GenericRef& lft_ref) const override {
            return ::HIR::LifetimeRef();
        }
    } monomorph_erase_lifetimes(state.m_crate.m_types);

    TRACE_FUNCTION_F(ty << " <= " << lit);
    TU_MATCH_HDRA( (*ty), {)
    default:
        if( path == ::HIR::GenericPath() )
            MIR_TODO(state, "Literal of type " << ty << " - " << lit);
        DEBUG("Unknown type " << ty << ", but a path was provided - Return ItemAddr " << path);
        return ::MIR::Constant::make_ItemAddr(box$(path));
        TU_ARMA(Tuple, te) {
            auto* repr = Target_GetTypeRepr(state.sp, state.m_resolve, ty);
            MIR_ASSERT(state, repr, "No type repr, but encoded value available? " << ty);

            ::std::vector<::MIR::Param> lvals;
            lvals.reserve(repr->fields.size());

            for (const auto& fld : repr->fields) {
                auto rval = MIR_Cleanup_LiteralToRValue(state, mutator, lit.slice(fld.offset), monomorph_erase_lifetimes.monomorph_type(state.sp, fld.ty), params, ::HIR::GenericPath());
                lvals.push_back(mutator.in_temporary(monomorph_erase_lifetimes.monomorph_type(state.sp, fld.ty), mv$(rval)));
            }

            return ::MIR::RValue::make_Tuple({mv$(lvals)});
        }
        TU_ARMA(Array, te) {
            size_t size = 0;
            MIR_ASSERT(state, Target_GetSizeOf(state.sp, state.m_resolve, te.inner, size), "No size, but encoded value available? " << ty);
            auto count = te.size.as_Known();

            bool is_all_same;
            if (count > 1) {
                is_all_same = true;
                size_t ofs = size;
                auto element0 = lit.slice(0, size);
                for (unsigned int i = 1; i < count; i++) {
                    auto cur = lit.slice(ofs, size);
                    //DEBUG(element0 << " ?= " << cur);
                    if (element0 != cur) {
                        is_all_same = false;
                        break;
                    }
                    ofs += size;
                }
            } else {
                is_all_same = false;
            }

            // If all of the literals are the same value, then optimise into a count-based initialisation
            if (is_all_same) {
                auto rval = MIR_Cleanup_LiteralToRValue(state, mutator, lit.slice(0, size), te.inner, params, ::HIR::GenericPath());
                auto data_lval = mutator.in_temporary(te.inner, mv$(rval));
                return ::MIR::RValue::make_SizedArray({mv$(data_lval), static_cast<unsigned int>(count)});
            } else {
                ::std::vector<::MIR::Param> lvals;
                lvals.reserve(te.size.as_Known());

                size_t ofs = 0;
                for (unsigned int i = 0; i < count; i++) {
                    auto rval = MIR_Cleanup_LiteralToRValue(state, mutator, lit.slice(ofs, size), te.inner, params, ::HIR::GenericPath());
                    lvals.push_back(mutator.in_temporary(te.inner, mv$(rval)));
                    ofs += size;
                }

                return ::MIR::RValue::make_Array({mv$(lvals)});
            }
        }
        TU_ARMA(Path, te) {
            auto* repr = Target_GetTypeRepr(state.sp, state.m_resolve, ty);
            MIR_ASSERT(state, repr, "No type repr, but encoded value available? " << ty);

            if (te.binding.is_Struct()) {
                ::std::vector<::MIR::Param> lvals;
                lvals.reserve(repr->fields.size());

                for (const auto& fld : repr->fields) {
                    auto rval = MIR_Cleanup_LiteralToRValue(state, mutator, lit.slice(fld.offset), monomorph_erase_lifetimes.monomorph_type(state.sp, fld.ty), params, ::HIR::GenericPath());
                    lvals.push_back(mutator.in_temporary(monomorph_erase_lifetimes.monomorph_type(state.sp, fld.ty), mv$(rval)));
                }

                return ::MIR::RValue::make_Struct({te.path.m_data.as_Generic().clone(), mv$(lvals)});
            } else if (te.binding.is_Enum()) {
                auto var_info = repr->get_enum_variant(state.sp, state.m_resolve, lit);
                unsigned var_idx = var_info.first;
                bool has_tag_field = var_info.second;

                const auto& enm = *te.binding.as_Enum();

                std::vector<::MIR::Param> vals;
                if (enm.m_data.is_Data()) {
                    const auto& fld = repr->fields.at(var_idx);

                    size_t base_ofs = fld.offset;
                    const auto* repr = Target_GetTypeRepr(state.sp, state.m_resolve, fld.ty);
                    vals.reserve(repr->fields.size());

                    for (const auto& fld : repr->fields) {
                        if (has_tag_field && &fld == &repr->fields.back()) {
                            continue;
                        }
                        auto rval = MIR_Cleanup_LiteralToRValue(state, mutator, lit.slice(base_ofs + fld.offset), monomorph_erase_lifetimes.monomorph_type(state.sp, fld.ty), params, ::HIR::GenericPath());
                        vals.push_back(mutator.in_temporary(monomorph_erase_lifetimes.monomorph_type(state.sp, fld.ty), mv$(rval)));
                    }
                } else {
                    // Leave empty
                }
                return ::MIR::RValue::make_EnumVariant({te.path.m_data.as_Generic().clone(), var_idx, mv$(vals)});
            } else if (te.binding.is_Union()) {
                unsigned var_idx = ~0u;
                const auto* repr = Target_GetTypeRepr(state.sp, state.m_resolve, ty);
                MIR_ASSERT(state, repr, "");
                // TODO: Find a way of storing backing information that specifies the variant (maybe as a relocation?)

                if (var_idx == ~0u) {
                    for (const auto& e : repr->fields) {
                        // A byte array covering the entire structure - can just emit
                        if (e.ty->is_Array() && e.ty->as_Array().inner == ::HIR::CoreType::U8 && e.ty->as_Array().size.as_Known() == repr->size) {
                            DEBUG("Found an array covering the whole union");
                            var_idx = &e - &repr->fields.front();
                            break;
                        }
                    }
                }
                // MaybeUninit (1.39) - `union MaybeUninit<T> { uninit: (), data: T }`
                // - If the body is all zeroes, then emit `uninit` (as that's the default)
                // - Otherwise, emit the actual value
                if (var_idx == ~0u) {
                    if (repr->fields.size() == 2 && repr->fields[0].ty == state.m_crate.m_types.unit()) {
                        // If all zeroes, then emit the tuple field, otherwise the other field
                        bool is_nonzero = false;
                        for (size_t i = 0; i < repr->size; i++) {
                            if (lit.slice(i, 1).read_uint(1) != 0) {
                                is_nonzero = true;
                                break;
                            }
                        }

                        var_idx = (is_nonzero ? 1 : 0);
                    }
                }

                // If there's a POD field (pointer or integer) of size equal to the whole struct, use that
                if (var_idx == ~0u) {
                    for (const auto& e : repr->fields) {
                        if (e.ty->is_Pointer() || e.ty->is_Primitive()) {
                            // If there's a relocation, then we have to use a pointer field
                            if (lit.get_reloc() && !e.ty->is_Pointer()) {
                                continue;
                            }

                            size_t fld_size = 0;
                            Target_GetSizeOf(state.sp, state.m_resolve, e.ty, fld_size);
                            if (fld_size == repr->size) {
                                // Found a suitable field!
                                DEBUG("Found a covering field");
                                var_idx = &e - &repr->fields.front();
                                break;
                            }
                        }
                    }
                }

                // A full-size aggregate made entirely from unrestricted scalar types can
                // represent the storage without knowing which union field initialized it.
                if (var_idx == ~0u) {
                    const auto literal_end = lit.m_ofs + lit.m_size;
                    const bool has_relocation = std::any_of(lit.m_base.relocations.begin(), lit.m_base.relocations.end(), [&](const auto& relocation) {
                        return relocation.ofs < literal_end && lit.m_ofs < relocation.ofs + relocation.len;
                    });
                    if (!has_relocation) {
                        for (const auto& e : repr->fields) {
                            size_t field_size = 0;
                            if (Target_GetSizeOf(state.sp, state.m_resolve, e.ty, field_size) && field_size == repr->size && type_accepts_all_bit_patterns(state.sp, state.m_resolve, e.ty)) {
                                DEBUG("Found an unrestricted covering field");
                                var_idx = &e - &repr->fields.front();
                                break;
                            }
                        }
                    }
                }

                if (var_idx == ~0u) {
                    MIR_TODO(state, "MIR_Cleanup_LiteralToRValue - " << path << ": " << ty << " = " << lit << " - Decode union into MIR");
                }
                auto inner_rval = MIR_Cleanup_LiteralToRValue(state, mutator, lit, repr->fields[var_idx].ty, params, mv$(path));
                auto inner_lval = mutator.in_temporary(monomorph_erase_lifetimes.monomorph_type(state.sp, repr->fields[var_idx].ty), mv$(inner_rval));
                return ::MIR::RValue::make_UnionVariant({te.path.m_data.as_Generic().clone(), var_idx, mv$(inner_lval)});
            } else {
                MIR_BUG(state, "Unexpected type for literal from " << path << " - " << ty << " (lit = " << lit << ")");
            }
        }
        TU_ARMA(Primitive, te) {
            switch (te) {
                case ::HIR::CoreType::Char:
                    return ::MIR::Constant::make_Uint({lit.read_uint(4), te});
                case ::HIR::CoreType::Usize:
                    return ::MIR::Constant::make_Uint({lit.read_uint(Target_GetPointerBits() / 8), te});
                case ::HIR::CoreType::U128:
                    return ::MIR::Constant::make_Uint({lit.read_uint(16), te});
                case ::HIR::CoreType::U64:
                    return ::MIR::Constant::make_Uint({lit.read_uint(8), te});
                case ::HIR::CoreType::U32:
                    return ::MIR::Constant::make_Uint({lit.read_uint(4), te});
                case ::HIR::CoreType::U16:
                    return ::MIR::Constant::make_Uint({lit.read_uint(2), te});
                case ::HIR::CoreType::U8:
                    return ::MIR::Constant::make_Uint({lit.read_uint(1), te});

                case ::HIR::CoreType::Isize:
                    return ::MIR::Constant::make_Int({lit.read_sint(Target_GetPointerBits() / 8), te});
                case ::HIR::CoreType::I128:
                    return ::MIR::Constant::make_Int({lit.read_sint(16), te});
                case ::HIR::CoreType::I64:
                    return ::MIR::Constant::make_Int({lit.read_sint(8), te});
                case ::HIR::CoreType::I32:
                    return ::MIR::Constant::make_Int({lit.read_sint(4), te});
                case ::HIR::CoreType::I16:
                    return ::MIR::Constant::make_Int({lit.read_sint(2), te});
                case ::HIR::CoreType::I8:
                    return ::MIR::Constant::make_Int({lit.read_sint(1), te});

                case ::HIR::CoreType::F128:
                    return ::MIR::Constant::make_Float({lit.read_float(16), te});
                case ::HIR::CoreType::F64:
                    return ::MIR::Constant::make_Float({lit.read_float(8), te});
                case ::HIR::CoreType::F32:
                    return ::MIR::Constant::make_Float({lit.read_float(4), te});
                case ::HIR::CoreType::F16:
                    return ::MIR::Constant::make_Float({lit.read_float(2), te});
                case ::HIR::CoreType::Bool:
                    return ::MIR::Constant::make_Bool({lit.read_uint(1) != 0});

                case ::HIR::CoreType::Str:
                    MIR_BUG(state, "Const of type `str` - " << path);
            }
            throw "";
        }
        TU_ARMA(Pointer, te) {
            if (lit.get_reloc()) {
                // Share logic with `Borrow` below, but wrap returned value in a cast op
                auto ty_borrow = state.m_crate.m_types.borrow(te.type, te.inner);
                auto rval = MIR_Cleanup_LiteralToRValue(state, mutator, lit, ty_borrow, params, mv$(path));
                auto lval = mutator.in_temporary(mv$(ty_borrow), mv$(rval));
                return ::MIR::RValue::make_Cast({mv$(lval), mv$(ty)});
            } else {
                auto v = lit.read_uint(Target_GetPointerBits() / 8);
                auto lval = mutator.in_temporary(state.m_crate.m_types.primitive(::HIR::CoreType::Usize), ::MIR::RValue(::MIR::Constant::make_Uint({v, ::HIR::CoreType::Usize})));
                return ::MIR::RValue::make_Cast({mv$(lval), mv$(ty)});
            }
        }
        TU_ARMA(Borrow, te) {
            const auto* data_reloc = lit.get_reloc();
            const auto data_ptr = lit.read_uint(Target_GetPointerBits() / 8);
            MIR_ASSERT(state, data_ptr >= EncodedLiteral::PTR_BASE, "Bad pointer value - 0x" << std::hex << data_ptr);

            if (!data_reloc) {
                ::HIR::TypeRef ptr_inner;
                const auto metadata_type = state.m_resolve.metadata_type(state.sp, te.inner);
                if (metadata_type == MetadataType::Slice) {
                    if (const auto* slice = te.inner->opt_Slice()) {
                        ptr_inner = slice->inner;
                    } else {
                        MIR_ASSERT(state, te.inner == ::HIR::CoreType::Str, "Slice metadata on non-slice type " << te.inner);
                        ptr_inner = state.m_crate.m_types.primitive(::HIR::CoreType::U8);
                    }
                } else {
                    ptr_inner = te.inner;
                }

                auto addr = mutator.in_temporary(state.m_crate.m_types.primitive(::HIR::CoreType::Usize), ::MIR::Constant::make_Uint({data_ptr, ::HIR::CoreType::Usize}));
                auto ptr_ty = state.m_crate.m_types.pointer(te.type, ptr_inner);
                auto ptr = mutator.in_temporary(ptr_ty, ::MIR::RValue::make_Cast({mv$(addr), ptr_ty}));

                switch (metadata_type) {
                    case MetadataType::None:
                        return ::MIR::RValue::make_Borrow({te.type, false, ::MIR::LValue::new_Deref(mv$(ptr))});
                    case MetadataType::Slice: {
                        const auto ptr_size = Target_GetPointerBits() / 8;
                        auto size = ::MIR::Constant::make_Uint({lit.slice(ptr_size).read_uint(ptr_size), ::HIR::CoreType::Usize});
                        return ::MIR::RValue::make_MakeDst({mv$(ptr), mv$(size)});
                    }
                    case MetadataType::TraitObject:
                    case MetadataType::Unknown:
                    case MetadataType::Zero:
                        MIR_TODO(state, "Integer-address borrow with metadata " << metadata_type);
                }
            }

            const auto ofs = data_ptr - EncodedLiteral::PTR_BASE;
            if (data_reloc->p) {
                const auto& path = *data_reloc->p;
                auto ptr_val = ::MIR::Constant::make_ItemAddr({box$(params.monomorph_path(state.sp, path)), ofs});
                DEBUG("ptr_val = " << ptr_val);
                ::HIR::TypeRef tmp;
                const auto& src_ty = state.get_static_type(tmp, path);

                // Get the metadata type (for !Sized wrapper types)
                auto meta_ty = state.m_resolve.metadata_type(state.sp, te.inner);
                switch (meta_ty) {
                    case MetadataType::None:
                        // TODO: What if the type doesn't match? Emit a `_Cast foo as &Bar`?
                        if (src_ty != te.inner) {
                            auto src_ref_ty = state.m_crate.m_types.borrow(te.type, src_ty);
                            auto src_ptr_ty = state.m_crate.m_types.pointer(te.type, src_ty);
                            auto inner_ptr_ty = state.m_crate.m_types.pointer(te.type, te.inner);
                            auto src_ty_ref = mutator.in_temporary(src_ref_ty, mv$(ptr_val));
                            auto src_ty_ptr = mutator.in_temporary(src_ptr_ty, ::MIR::RValue::make_Cast({mv$(src_ty_ref), src_ptr_ty}));
                            auto inner_lval = mutator.in_temporary(inner_ptr_ty, ::MIR::RValue::make_Cast({mv$(src_ty_ptr), inner_ptr_ty}));
                            return ::MIR::RValue::make_Borrow({te.type, false, MIR::LValue::new_Deref(mv$(inner_lval))});
                        }
                        return mv$(ptr_val);
                    case MetadataType::Slice: {
                        const auto ptr_size = Target_GetPointerBits() / 8;
                        auto size = lit.slice(ptr_size).read_uint(ptr_size);
                        auto size_val = ::MIR::Param(::MIR::Constant::make_Uint({size, ::HIR::CoreType::Usize}));
                        return ::MIR::RValue::make_MakeDst({::MIR::Param(mv$(ptr_val)), mv$(size_val)});
                        break;
                    }
                    case MetadataType::TraitObject: {
                        const auto* tep = te.inner->opt_TraitObject();
                        if (!tep) {
                            MIR_TODO(state, "Hidden vtable");
                        }

                        auto vtable_val = ::MIR::Param(create_vtable(src_ty, tep->m_trait));

                        return ::MIR::RValue::make_MakeDst({::MIR::Param(mv$(ptr_val)), mv$(vtable_val)});
                        break;
                    }
                    case MetadataType::Unknown:
                        MIR_BUG(state, te.inner << " unknown metadata type");
                    case MetadataType::Zero:
                        MIR_TODO(state, "Zero metadata");
                }
            } else {
                // This is a borrow of a "string"
                MIR_ASSERT(state, ofs <= data_reloc->bytes.size(), "Offset out of range");
                auto s = data_reloc->bytes.begin() + ofs.truncate_u64();
                auto e = data_reloc->bytes.end();

                if (te.inner->is_Slice() && te.inner->as_Slice().inner == ::HIR::CoreType::U8) {
                    ::std::vector<uint8_t> bytestr;
                    for (auto it = s; it != e; ++it) {
                        bytestr.push_back(static_cast<uint8_t>(*it));
                    }
                    auto size = ::MIR::Constant::make_Uint({U128(bytestr.size()), ::HIR::CoreType::Usize});
                    return ::MIR::RValue::make_MakeDst({::MIR::Constant(mv$(bytestr)), std::move(size)});
                } else if (te.inner->is_Array() && te.inner->as_Array().inner == ::HIR::CoreType::U8) {
                    // TODO: How does this differ at codegen to the above?
                    ::std::vector<uint8_t> bytestr;
                    for (auto it = s; it != e; ++it) {
                        bytestr.push_back(static_cast<uint8_t>(*it));
                    }
                    return ::MIR::Constant(mv$(bytestr));
                } else if (te.inner == ::HIR::CoreType::Str) {
                    return ::MIR::Constant::make_StaticString(std::string(s, e));
                } else {
                    // Get repr, assert that there's only one field and it's a `[u8]` or `str`
                    // Pointer cast
                    ::std::vector<uint8_t> bytestr;
                    for (auto it = s; it != e; ++it) {
                        bytestr.push_back(static_cast<uint8_t>(*it));
                    }
                    auto size = ::MIR::Constant::make_Uint({U128(bytestr.size()), ::HIR::CoreType::Usize});
                    // Make a `*const [u8]`
                    auto ptr1 = ::MIR::RValue::make_MakeDst({::MIR::Constant(mv$(bytestr)), ::std::move(size)});
                    auto lval = mutator.in_temporary(state.m_crate.m_types.pointer(HIR::BorrowType::Shared, state.m_crate.m_types.slice(state.m_crate.m_types.primitive(::HIR::CoreType::U8))), mv$(ptr1));
                    // Cast to `*const T`
                    auto raw_ptr_ty = state.m_crate.m_types.pointer(HIR::BorrowType::Shared, te.inner);
                    auto lval2 = mutator.in_temporary(raw_ptr_ty, ::MIR::RValue::make_Cast({mv$(lval), raw_ptr_ty}));
                    // Reborrow as `&T`
                    return ::MIR::RValue::make_Borrow({::HIR::BorrowType::Shared, false, ::MIR::LValue::new_Deref(mv$(lval2))});
                }
            }
        }
        TU_ARMA(NamedFunction, te) {
            // Function items are zero-sized: their identity is carried by
            // the NamedFunction type, not by bytes or a relocation in the
            // evaluated literal.  Reconstruct the ZST function value instead
            // of treating the lifted inline constant itself as addressable.
            return ::MIR::Constant::make_Function({box$(te.path.clone())});
        }
        TU_ARMA(Function, te) {
            const auto* data_reloc = lit.get_reloc();
            MIR_ASSERT(state, data_reloc, "Function with no relocation?!");
            MIR_ASSERT(state, data_reloc->p, "");
            return ::MIR::Constant::make_ItemAddr(box$(data_reloc->p->clone()));
        }
    }
    throw "";
}

::MIR::LValue MIR_Cleanup_Virtualize(const Span& sp, const ::MIR::TypeResolve& state, MirMutator& mutator, ::MIR::LValue& receiver_lvp, const ::HIR::Path::Data::Data_UfcsKnown& pe) {
    TRACE_FUNCTION_F("<" << pe.type << " as " << pe.trait << ">::" << pe.item << pe.params);

    assert(pe.type->is_TraitObject());
    const ::HIR::TypeData::Data_TraitObject& te = pe.type->as_TraitObject();
    assert(te.m_trait.m_trait_ptr);
    const auto& trait = *te.m_trait.m_trait_ptr;

    // 1. Get the vtable index for this function
    unsigned int vtable_idx = trait.get_vtable_value_index(pe.trait.m_path, pe.item);
    if (vtable_idx == 0) {
        BUG(sp, "Calling method '" << pe.item << "' from " << pe.trait << " through " << te.m_trait.m_path << " which isn't in the vtable");
    }

    // 2. Load from the vtable
    auto vtable_ty = state.m_crate.m_types.pointer(::HIR::BorrowType::Shared, get_vtable_type(sp, state.m_resolve, te));
    DEBUG("vtable_ty = " << vtable_ty);

    // If the method is a by-value method, add a `&move`
    const auto& fn_def = state.m_crate.get_trait_by_path(sp, pe.trait.m_path).m_values.at(pe.item).as_Function();
    if (fn_def.m_receiver == HIR::Function::Receiver::Value) {
        receiver_lvp = mutator.in_temporary(state.m_crate.m_types.borrow(HIR::BorrowType::Owned, pe.type), MIR::RValue::make_Borrow({HIR::BorrowType::Owned, false, mv$(receiver_lvp)}));
    }

    // Allocate a temporary for the vtable pointer itself
    auto vtable_lv = mutator.new_temporary(mv$(vtable_ty));
    auto fcn_lval = ::MIR::LValue::new_Field(::MIR::LValue::new_Deref(vtable_lv.clone()), vtable_idx);
    ::HIR::TypeRef tmp;
    const auto& ty = state.get_lvalue_type(tmp, fcn_lval);
    DEBUG("callable type " << ty);
    auto receiver = MonomorphHrlsOnly(state.m_crate.m_types, ty->as_Function().hrls.make_empty_params(true)).monomorph_type(state.sp, ty->as_Function().m_arg_types.at(0));

    struct H {
        static ::MIR::LValue get_unit_ptr(const ::MIR::TypeResolve& state, MirMutator& mutator, ::HIR::TypeRef ty, ::MIR::LValue lv, ::MIR::LValue& out_inner_ptr) {
            if (ty->is_Path()) {
                const auto& te = ty->as_Path();
                MIR_ASSERT(state, te.binding.is_Struct(), "");
                const auto& ty_path = te.path.m_data.as_Generic();
                const auto& str = *te.binding.as_Struct();
                ::HIR::TypeRef tmp;
                auto monomorph = [&](const auto& t) {
                    return MonomorphStatePtr(state.m_crate.m_types, nullptr, &ty_path.m_params, nullptr).monomorph_type(state.sp, t);
                };
                ::std::vector<::MIR::Param> vals;
                TU_MATCH_HDRA( (str.m_data), {)
                TU_ARMA(Unit, se) {
                    }
                    TU_ARMA(Tuple, se) {
                        for (unsigned int i = 0; i < se.size(); i++) {
                            auto val = ::MIR::LValue::new_Field((i == se.size() - 1 ? mv$(lv) : lv.clone()), i);
                            if (i == str.m_struct_markings.coerce_unsized_index) {
                                vals.push_back(H::get_unit_ptr(state, mutator, monomorph(se[i].ent), mv$(val), out_inner_ptr));
                            } else {
                                vals.push_back(mv$(val));
                            }
                        }
                    }
                    TU_ARMA(Named, se) {
                        for (unsigned int i = 0; i < se.size(); i++) {
                            auto val = ::MIR::LValue::new_Field((i == se.size() - 1 ? mv$(lv) : lv.clone()), i);
                            if (i == str.m_struct_markings.coerce_unsized_index) {
                                vals.push_back(H::get_unit_ptr(state, mutator, monomorph(se[i].ty), mv$(val), out_inner_ptr));
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
                return mutator.in_temporary(state.m_crate.m_types.pointer(::HIR::BorrowType::Shared, state.m_crate.m_types.unit()), ::MIR::RValue::make_DstPtr({mv$(lv)}));
            } else {
                MIR_BUG(state, "Unexpected type coerce_unsize in receiver - " << ty);
            }
        }
    };

    ::MIR::LValue receiver_ptr;
    ::MIR::LValue inner_dyn_ptr;

    if (receiver->is_Path() && receiver->as_Path().binding.is_Struct() && receiver->as_Path().binding.as_Struct()->m_struct_markings.coerce_unsized != ::HIR::StructMarkings::Coerce::None) {
        // If the receiver is Box (or anything that implements CoerceUnsized), create a Foo<()> as the value.
        // - Requires de/restructuring the Box same as CoerceUnsized
        // - Can use the `coerce_unsized_index` field too
        receiver_lvp = H::get_unit_ptr(state, mutator, ::std::move(receiver), receiver_lvp.clone(), inner_dyn_ptr);
    } else if (receiver->is_Borrow() || receiver->is_Pointer()) {
        inner_dyn_ptr = receiver_lvp.clone();
        auto ptr_rval = ::MIR::RValue::make_DstPtr({receiver_lvp.clone()});

        auto ptr_lv = mutator.new_temporary(state.m_crate.m_types.pointer(::HIR::BorrowType::Shared, state.m_crate.m_types.unit()));
        mutator.push_statement(::MIR::Statement::make_Assign({ptr_lv.clone(), mv$(ptr_rval)}));
        receiver_lvp = mv$(ptr_lv);
    } else {
        // TODO: How to handle `Pin`?
        // - Locate the pointer (similar to unsized path?)
        MIR_TODO(state, "Handle virtual call through " << receiver);
    }

    // - Load the vtable and store it
    auto vtable_rval = ::MIR::RValue::make_DstMeta({mv$(inner_dyn_ptr)});
    mutator.push_statement(::MIR::Statement::make_Assign({vtable_lv.clone(), mv$(vtable_rval)}));

    // Update the terminator with the new information.
    return fcn_lval;
}

bool MIR_Cleanup_Unsize_GetMetadata(const ::MIR::TypeResolve& state, MirMutator& mutator, const ::HIR::TypeRef& dst_ty, const ::HIR::TypeRef& src_ty, const ::MIR::LValue& ptr_value, ::MIR::Param& out_meta_val, ::HIR::TypeRef& out_meta_ty, bool& out_src_is_dst) {
    TU_MATCH_HDRA( (*dst_ty), { )
    default:
        MIR_TODO(state, "Obtain metadata converting to " << dst_ty);
        TU_ARMA(Generic, de) {
            // TODO: What should be returned to indicate "no conversion"
            return false;
        }
        TU_ARMA(Path, de) {
            // Source must be Path and Unsize
            if (de.binding.is_Opaque()) {
                return false;
            }

            MIR_ASSERT(state, src_ty->is_Path(), "Unsize to path from non-path - " << src_ty);
            const auto& se = src_ty->as_Path();
            MIR_ASSERT(state, de.binding.tag() == se.binding.tag(), "Unsize between mismatched types - " << dst_ty << " and " << src_ty);
            MIR_ASSERT(state, de.binding.is_Struct(), "Unsize to non-struct - " << dst_ty);
            MIR_ASSERT(state, de.binding.as_Struct() == se.binding.as_Struct(), "Unsize between mismatched types - " << dst_ty << " and " << src_ty);
            const auto& str = *de.binding.as_Struct();
            MIR_ASSERT(state, str.m_struct_markings.unsized_field != ~0u, "Unsize on type that doesn't implement have a ?Sized field - " << dst_ty);

            auto monomorph_cb_d = MonomorphStatePtr(state.m_crate.m_types, nullptr, &de.path.m_data.as_Generic().m_params, nullptr);
            auto monomorph_cb_s = MonomorphStatePtr(state.m_crate.m_types, nullptr, &se.path.m_data.as_Generic().m_params, nullptr);

            // Return GetMetadata on the inner type
        TU_MATCH_HDRA( (str.m_data), {)
        TU_ARMA(Unit, se) {
                    MIR_BUG(state, "Unit-like struct Unsize is impossible - " << src_ty);
                }
                TU_ARMA(Tuple, se) {
                    const auto& ty_tpl = se.at(str.m_struct_markings.unsized_field).ent;
                    auto ty_d = monomorph_cb_d.monomorph_type(state.sp, ty_tpl, false);
                    auto ty_s = monomorph_cb_s.monomorph_type(state.sp, ty_tpl, false);

                    return MIR_Cleanup_Unsize_GetMetadata(state, mutator, ty_d, ty_s, ptr_value, out_meta_val, out_meta_ty, out_src_is_dst);
                }
                TU_ARMA(Named, se) {
                    const auto& ty_tpl = se.at(str.m_struct_markings.unsized_field).ty;
                    auto ty_d = monomorph_cb_d.monomorph_type(state.sp, ty_tpl, false);
                    auto ty_s = monomorph_cb_s.monomorph_type(state.sp, ty_tpl, false);

                    return MIR_Cleanup_Unsize_GetMetadata(state, mutator, ty_d, ty_s, ptr_value, out_meta_val, out_meta_ty, out_src_is_dst);
                }
        }
        throw "";
        }
        TU_ARMA(Slice, de) {
            // Source must be an array (or generic)
            if (src_ty->is_Array()) {
                const auto& in_array = src_ty->as_Array();
                if (!in_array.size.is_Known()) {
                    DEBUG("Array size not yet known - " << in_array.size);
                    return false;
                }
                out_meta_ty = state.m_crate.m_types.primitive(::HIR::CoreType::Usize);
                out_meta_val = ::MIR::Constant::make_Uint({U128(in_array.size.as_Known()), ::HIR::CoreType::Usize});
                return true;
            } else if (src_ty->is_Generic() || (src_ty->is_Path() && src_ty->as_Path().binding.is_Opaque())) {
                // Defer until monomorphisation supplies the concrete source array.
                return false;
            } else {
                MIR_BUG(state, "Unsize to slice from non-array - " << src_ty);
            }
        }
        TU_ARMA(TraitObject, de) {
            // Obtain vtable type `::"path"::to::Trait#vtable`
            auto vtable_ty = de.m_trait.m_path != HIR::SimplePath() ? de.m_trait.m_trait_ptr->get_vtable_type(state.sp, state.m_crate, de) : state.m_crate.m_types.unit();
            out_meta_ty = state.m_crate.m_types.pointer(::HIR::BorrowType::Shared, vtable_ty);

            // If the data trait hasn't changed, return the vtable pointer
            if (const auto* se = src_ty->opt_TraitObject()) {
                out_src_is_dst = true;
                if (se->m_trait.m_trait_ptr != de.m_trait.m_trait_ptr) {
                    assert(se->m_trait.m_trait_ptr);
                    const auto& trait = *se->m_trait.m_trait_ptr;
                    auto vtable_ty = trait.get_vtable_type(state.sp, state.m_crate, *se);
                    auto in_meta_ty = state.m_crate.m_types.pointer(::HIR::BorrowType::Shared, vtable_ty);

                    auto parent_trait_field = trait.get_vtable_parent_index(state.m_crate.m_types, state.sp, se->m_trait.m_path.m_params, de.m_trait.m_path);
                    MIR_ASSERT(state, parent_trait_field != 0, "Unable to find parent trait for trait object upcast - " << se->m_trait.m_path << " in " << de.m_trait.m_path);
                    auto in_meta_val = mutator.in_temporary(mv$(in_meta_ty), ::MIR::RValue::make_DstMeta({ptr_value.clone()}));
                    out_meta_val = MIR::LValue::new_Field(MIR::LValue::new_Deref(mv$(in_meta_val)), parent_trait_field);
                } else {
                    out_meta_val = mutator.in_temporary(out_meta_ty, ::MIR::RValue::make_DstMeta({ptr_value.clone()}));
                }
            } else {
                MIR_ASSERT(state, state.m_resolve.type_is_sized(state.sp, src_ty), "Attempting to get vtable for unsized type - " << src_ty);
                out_meta_val = create_vtable(src_ty, de.m_trait);
            }
            return true;
        }
    }
    throw "";
}

::MIR::RValue MIR_Cleanup_Unsize(const ::MIR::TypeResolve& state, MirMutator& mutator, const ::HIR::TypeRef& dst_ty, const ::HIR::TypeRef& src_ty_inner, ::MIR::LValue ptr_value) {
    const auto& dst_ty_inner = (dst_ty->is_Borrow() ? dst_ty->as_Borrow().inner : dst_ty->as_Pointer().inner);

    ::HIR::TypeRef meta_type;
    ::MIR::Param meta_value;
    bool source_is_dst = false;
    if (MIR_Cleanup_Unsize_GetMetadata(state, mutator, dst_ty_inner, src_ty_inner, ptr_value, meta_value, meta_type, source_is_dst)) {
        // There is a case where the source is already a fat pointer. In that case the pointer of the new DST must be the source DST pointer
        if (source_is_dst) {
            auto ty_unit_ptr = state.m_crate.m_types.pointer(::HIR::BorrowType::Shared, state.m_crate.m_types.unit());
            auto thin_ptr_lval = mutator.in_temporary(mv$(ty_unit_ptr), ::MIR::RValue::make_DstPtr({mv$(ptr_value)}));

            return ::MIR::RValue::make_MakeDst({mv$(thin_ptr_lval), mv$(meta_value)});
        } else {
            return ::MIR::RValue::make_MakeDst({mv$(ptr_value), mv$(meta_value)});
        }
    } else {
        // Re-emit the "unsize" pseudo-op
        return ::MIR::RValue::make_MakeDst({mv$(ptr_value), MIR::Constant::make_ItemAddr({})});
    }
}

::MIR::RValue MIR_Cleanup_CoerceUnsized(const ::MIR::TypeResolve& state, MirMutator& mutator, const ::HIR::TypeRef& dst_ty, const ::HIR::TypeRef& src_ty, ::MIR::LValue value) {
    TRACE_FUNCTION_F(dst_ty << " <- " << src_ty << " ( " << value << " )");
    //  > Path -> Path = Unsize
    // (path being destination is otherwise invalid)
    if (dst_ty->is_Path()) {
        MIR_ASSERT(state, src_ty->is_Path(), "CoerceUnsized to Path must have a Path source - " << src_ty << " to " << dst_ty);
        const auto& dte = dst_ty->as_Path();
        const auto& ste = src_ty->as_Path();

        // - Types must differ only by a single field, and be from the same definition
        MIR_ASSERT(state, dte.binding.is_Struct(), "Note, can't CoerceUnsized non-structs");
        MIR_ASSERT(state, dte.binding.tag() == ste.binding.tag(), "Note, can't CoerceUnsized mismatched structs - " << src_ty << " to " << dst_ty);
        MIR_ASSERT(state, dte.binding.as_Struct() == ste.binding.as_Struct(), "Note, can't CoerceUnsized mismatched structs - " << src_ty << " to " << dst_ty);
        const auto& str = *dte.binding.as_Struct();
        MIR_ASSERT(state, str.m_struct_markings.coerce_unsized_index != ~0u, "Struct " << src_ty << " doesn't impl CoerceUnsized");

        auto monomorph_cb_d = MonomorphStatePtr(state.m_crate.m_types, nullptr, &dte.path.m_data.as_Generic().m_params, nullptr);
        auto monomorph_cb_s = MonomorphStatePtr(state.m_crate.m_types, nullptr, &ste.path.m_data.as_Generic().m_params, nullptr);

        // - Destructure and restrucure with the unsized fields
        ::std::vector<::MIR::Param> ents;
        TU_MATCH_HDRA( (str.m_data), {)
        TU_ARMA(Unit, se) {
                MIR_BUG(state, "Unit-like struct CoerceUnsized is impossible - " << src_ty);
            }
            TU_ARMA(Tuple, se) {
                ents.reserve(se.size());
                for (unsigned int i = 0; i < se.size(); i++) {
                    if (i == str.m_struct_markings.coerce_unsized_index) {
                        auto ty_d = monomorph_cb_d.monomorph_type(state.sp, se[i].ent, false);
                        auto ty_s = monomorph_cb_s.monomorph_type(state.sp, se[i].ent, false);

                        auto new_rval = MIR_Cleanup_CoerceUnsized(state, mutator, ty_d, ty_s, ::MIR::LValue::new_Field(value.clone(), i));
                        auto new_lval = mutator.in_temporary(mv$(ty_d), mv$(new_rval));

                        ents.push_back(mv$(new_lval));
                    } else if (state.m_resolve.is_type_phantom_data(se[i].ent)) {
                        auto ty_d = monomorph_cb_d.monomorph_type(state.sp, se[i].ent, false);

                        auto new_rval = ::MIR::RValue::make_Struct({ty_d->as_Path().path.m_data.as_Generic().clone(), {}});
                        auto new_lval = mutator.in_temporary(mv$(ty_d), mv$(new_rval));

                        ents.push_back(mv$(new_lval));
                    } else {
                        ents.push_back(::MIR::LValue::new_Field(value.clone(), i));
                    }
                }
            }
            TU_ARMA(Named, se) {
                ents.reserve(se.size());
                for (unsigned int i = 0; i < se.size(); i++) {
                    if (i == str.m_struct_markings.coerce_unsized_index) {
                        auto ty_d = monomorph_cb_d.monomorph_type(state.sp, se[i].ty, false);
                        auto ty_s = monomorph_cb_s.monomorph_type(state.sp, se[i].ty, false);

                        auto new_rval = MIR_Cleanup_CoerceUnsized(state, mutator, ty_d, ty_s, ::MIR::LValue::new_Field(value.clone(), i));
                        auto new_lval = mutator.new_temporary(mv$(ty_d));
                        mutator.push_statement(::MIR::Statement::make_Assign({new_lval.clone(), mv$(new_rval)}));

                        ents.push_back(mv$(new_lval));
                    } else if (state.m_resolve.is_type_phantom_data(se[i].ty)) {
                        auto ty_d = monomorph_cb_d.monomorph_type(state.sp, se[i].ty, false);

                        auto new_rval = ::MIR::RValue::make_Struct({ty_d->as_Path().path.m_data.as_Generic().clone(), {}});
                        auto new_lval = mutator.in_temporary(mv$(ty_d), mv$(new_rval));

                        ents.push_back(mv$(new_lval));
                    } else {
                        ents.push_back(::MIR::LValue::new_Field(value.clone(), i));
                    }
                }
            }
        }
        return ::MIR::RValue::make_Struct({ dte.path.m_data.as_Generic().clone(), mv$(ents) });
    }

    if (dst_ty->is_Borrow()) {
        MIR_ASSERT(state, src_ty->is_Borrow(), "CoerceUnsized to Borrow must have a Borrow source - " << src_ty << " to " << dst_ty);
        const auto& ste = src_ty->as_Borrow();

        return MIR_Cleanup_Unsize(state, mutator, dst_ty, ste.inner, mv$(value));
    }

    // Pointer Coercion - Downcast and unsize
    if (dst_ty->is_Pointer()) {
        MIR_ASSERT(state, src_ty->is_Pointer(), "CoerceUnsized to Pointer must have a Pointer source - " << src_ty << " to " << dst_ty);
        const auto& dte = dst_ty->as_Pointer();
        const auto& ste = src_ty->as_Pointer();

        if (dte.type == ste.type) {
            return MIR_Cleanup_Unsize(state, mutator, dst_ty, ste.inner, mv$(value));
        } else {
            MIR_ASSERT(state, dte.inner == ste.inner, "TODO: Can pointer CoerceUnsized unsize? " << src_ty << " to " << dst_ty);
            MIR_ASSERT(state, dte.type < ste.type, "CoerceUnsize attempting to raise pointer type");

            return ::MIR::RValue::make_Cast({mv$(value), dst_ty});
        }
    }

    MIR_BUG(state, "Unknown CoerceUnsized target " << dst_ty << " from " << src_ty);
    throw "";
}

void MIR_Cleanup_LValue(const ::MIR::TypeResolve& state, MirMutator& mutator, ::MIR::LValue& lval) {
    TU_MATCH_HDRA( (lval.m_root), {)
    TU_ARMA(Return, le) {
        }
        TU_ARMA(Argument, le) {
        }
        TU_ARMA(Local, le) {
        }
        TU_ARMA(Static, le) {
        }
    }

    for(size_t i = 0; i < lval.m_wrappers.size(); i ++)
    {
        if (!lval.m_wrappers[i].is_Deref()) {
            continue;
        }

        // If this is a deref of Box, unpack and deref the inner pointer
        ::HIR::TypeRef tmp;
        const auto& ty = state.get_lvalue_type(tmp, lval, lval.m_wrappers.size() - i);
        if (state.m_resolve.is_type_owned_box(ty)) {
            unsigned num_injected_fld_zeros = 0;

            // Handle Box by extracting it to its pointer.
            // - Locate (or remember) which field in Box is the pointer, and replace the inner by that field
            // > Dumb idea, assume it's always the first field. Keep accessing until located.

            auto typ = ty;
            while (typ->is_Path()) {
                const auto& te = typ->as_Path();
                MIR_ASSERT(state, te.binding.is_Struct(), "Box contained a non-struct");
                const auto& str = *te.binding.as_Struct();
                const ::HIR::TypeRef* ty_tpl = nullptr;
                TU_MATCH_HDRA( (str.m_data), {)
                TU_ARMA(Unit, se) {
                        MIR_BUG(state, "Box contained a unit-like struct");
                    }
                    TU_ARMA(Tuple, se) {
                        MIR_ASSERT(state, se.size() > 0, "Box contained an empty tuple struct");
                        ty_tpl = &se[0].ent;
                    }
                    TU_ARMA(Named, se) {
                        MIR_ASSERT(state, se.size() > 0, "Box contained an empty named struct");
                        ty_tpl = &se[0].ty;
                    }
                }
                tmp = MonomorphStatePtr(state.m_crate.m_types, nullptr, &te.path.m_data.as_Generic().m_params, nullptr).monomorph_type(state.sp, *ty_tpl);
                typ = tmp;

                num_injected_fld_zeros ++;
            }
            MIR_ASSERT(state, typ->is_Pointer(), "First non-path field in Box wasn't a pointer - " << typ);
            // We have reached the pointer. Good.

            // Inject all of the field zero accesses (before the deref)
            while (num_injected_fld_zeros--) {
                lval.m_wrappers.insert(lval.m_wrappers.begin() + i, ::MIR::LValue::Wrapper::new_Field(0));
            }
        } else {
            // What about other types?
        }
    }
}

void MIR_Cleanup_Constant(const ::MIR::TypeResolve& state, MirMutator& mutator, ::MIR::Constant& p) {
    if (auto* e = p.opt_Uint()) {
        switch (e->t) {
            // Constants use U128 storage; truncate usize values to the target pointer width.
            case ::HIR::CoreType::Usize:
                if (Target_GetCurSpec().m_arch.m_pointer_bits == 32) {
                    e->v &= U128(0xFFFFFFFF);
                }
                break;
            default:
                break;
        }
    }
}

void MIR_Cleanup_Param(const ::MIR::TypeResolve& state, MirMutator& mutator, ::MIR::Param& p) {
    TU_MATCH_HDRA( (p), { )
    TU_ARMA(LValue, e) {
            MIR_Cleanup_LValue(state, mutator, e);
        }
        TU_ARMA(Borrow, e) {
            MIR_Cleanup_LValue(state, mutator, e.val);
        }
        TU_ARMA(Constant, e) {
            MIR_Cleanup_Constant(state, mutator, e);
        }
    }

    // Effectively a copy of the code that handles RValue::Constant below
    if( p.is_Constant() && p.as_Constant().is_Const() )
    {
        const auto& ce = p.as_Constant().as_Const();
        ::HIR::TypeRef c_ty;
        MonomorphState params(state.m_crate.m_types);
        const auto* lit_ptr = MIR_Cleanup_GetConstant(state, *ce.p, c_ty, params);
        if (lit_ptr) {
            DEBUG("Replace constant " << *ce.p << " with " << *lit_ptr);
            auto new_rval = MIR_Cleanup_LiteralToRValue(state, mutator, *lit_ptr, c_ty, params, mv$(*ce.p));
            if (auto* lv = new_rval.opt_Use()) {
                p = ::MIR::Param::make_LValue(::std::move(*lv));
            } else if (auto* c = new_rval.opt_Constant()) {
                MIR_Cleanup_Constant(state, mutator, *c);
                p = ::MIR::Param::make_Constant(::std::move(*c));
            } else {
                auto tmp_lv = mutator.in_temporary(mv$(c_ty), mv$(new_rval));
                p = ::MIR::Param::make_LValue(::std::move(tmp_lv));
            }
        } else {
            DEBUG("No replacement for constant " << *ce.p);
        }
    }
}

void MIR_Cleanup(const StaticTraitResolve& resolve, const ::HIR::ItemPath& path, ::MIR::Function& fcn, const ::HIR::Function::args_t& args, const ::HIR::TypeRef& ret_type) {
    Span sp;
    TRACE_FUNCTION_F(path);
    ::MIR::TypeResolve state {
        sp, resolve, FMT_CB(ss, ss << path;), ret_type, args, fcn
    };

    MirMutator mutator{fcn, 0, 0};
    for (auto& block : fcn.blocks) {
        for (auto it = block.statements.begin(); it != block.statements.end(); ++it) {
            mutator.update_state(state);
            auto& stmt = *it;

            // >> Detect use of `!` as a value
            ::HIR::TypeRef tmp;
            if (TU_TEST1(stmt, Assign, .src.is_Borrow()) && state.get_lvalue_type(tmp, stmt.as_Assign().src.as_Borrow().val)->is_Diverge()) {
                DEBUG(state << "Not killing block due to use of `!`, it's being borrowed");
            } else {
                if (::MIR::visit::visit_mir_lvalues(stmt, [&](const auto& lv, auto /*vu*/) {
                    return state.get_lvalue_type(tmp, lv)->is_Diverge();
                })) {
                    DEBUG(state << "Truncate entire block due to use of `!` as a value - " << stmt);
                    block.statements.erase(it, block.statements.end());
                    block.terminator = ::MIR::Terminator::make_Diverge({});
                    break;
                }
            }
            // >> Visit all LValues for box deref hackery
            DEBUG(state << stmt);
            TU_MATCH_HDRA( (stmt), { )
            TU_ARMA(Drop, se) {
                    MIR_Cleanup_LValue(state, mutator, se.slot);
                }
                TU_ARMA(SetDropFlag, se) {
                }
                TU_ARMA(SaveDropFlag, se) {
                    MIR_Cleanup_LValue(state, mutator, se.slot);
                }
                TU_ARMA(LoadDropFlag, se) {
                    MIR_Cleanup_LValue(state, mutator, se.slot);
                }
                TU_ARMA(ScopeEnd, se) {
                }
                TU_ARMA(Asm, se) {
                    for (auto& v : se.inputs) {
                        MIR_Cleanup_LValue(state, mutator, v.second);
                    }
                    for (auto& v : se.outputs) {
                        MIR_Cleanup_LValue(state, mutator, v.second);
                    }
                }
                TU_ARMA(Asm2, e) {
                    for (auto& p : e.params) {
                    TU_MATCH_HDRA( (p), { )
                    TU_ARMA(Const, v) {
                            }
                            TU_ARMA(Sym, v) {
                            }
                            TU_ARMA(Reg, v) {
                                if (v.input) {
                                    MIR_Cleanup_Param(state, mutator, *v.input);
                                }
                                if (v.output) {
                                    MIR_Cleanup_LValue(state, mutator, *v.output);
                                }
                            }
                    }
                    }
                }
                TU_ARMA(Assign, se) {
                    MIR_Cleanup_LValue(state, mutator, se.dst);
                TU_MATCH_HDRA( (se.src), {)
                TU_ARMA(Use, re) {
                            MIR_Cleanup_LValue(state, mutator, re);
                        }
                        TU_ARMA(Constant, re) {
                            MIR_Cleanup_Constant(state, mutator, re);
                        }
                        TU_ARMA(SizedArray, re) {
                            MIR_Cleanup_Param(state, mutator, re.val);
                        }
                        TU_ARMA(Borrow, re) {
                            MIR_Cleanup_LValue(state, mutator, re.val);
                        }
                        TU_ARMA(Cast, re) {
                            MIR_Cleanup_LValue(state, mutator, re.val);
                        }
                        TU_ARMA(BinOp, re) {
                            MIR_Cleanup_Param(state, mutator, re.val_l);
                            MIR_Cleanup_Param(state, mutator, re.val_r);
                        }
                        TU_ARMA(UniOp, re) {
                            MIR_Cleanup_LValue(state, mutator, re.val);
                        }
                        TU_ARMA(DstMeta, re) {
                            // HACK: Ensure that the box Deref conversion fires here.
                            re.val.m_wrappers.push_back(::MIR::LValue::Wrapper::new_Deref());
                            MIR_Cleanup_LValue(state, mutator, re.val);
                            re.val.m_wrappers.pop_back();

                            // If the type is an array (due to a monomorpised generic?) then replace.
                            ::HIR::TypeRef tmp;
                            const auto& ty = state.get_lvalue_type(tmp, re.val);
                            const ::HIR::TypeRef* ity_p;
                            if (const auto* te = ty->opt_Borrow()) {
                                ity_p = &te->inner;
                            } else if (const auto* te = ty->opt_Pointer()) {
                                ity_p = &te->inner;
                            }
                            // NOTE: This can happen with calling a by-value method on a trait object, e.g. `<dyn Foo as FnOnce>::call_once`
                            // - That is handled with magic in trans, so needs magic here (for inlining)
                            else if (ty->is_TraitObject()) {
                                ity_p = &ty;
                                // Remove the deref so downstream doesn't need to care
                                MIR_ASSERT(state, !re.val.m_wrappers.empty() && re.val.m_wrappers.back().is_Deref(), "DstMeta on bare trait object with no deref: " << re.val);
                                re.val.m_wrappers.pop_back();
                            } else {
                                BUG(Span(), "Unexpected input type for DstMeta - " << ty);
                            }
                        }
                        TU_ARMA(DstPtr, re) {
                            // HACK: Ensure that the box Deref conversion fires here.
                            re.val.m_wrappers.push_back(::MIR::LValue::Wrapper::new_Deref());
                            MIR_Cleanup_LValue(state, mutator, re.val);
                            re.val.m_wrappers.pop_back();

                            ::HIR::TypeRef tmp;
                            const auto& ty = state.get_lvalue_type(tmp, re.val);
                            const ::HIR::TypeRef* ity_p;
                            if (const auto* te = ty->opt_Borrow()) {
                                ity_p = &te->inner;
                            } else if (const auto* te = ty->opt_Pointer()) {
                                ity_p = &te->inner;
                            }
                            // NOTE: This can happen with calling a by-value method on a trait object, e.g. `<dyn Foo as FnOnce>::call_once`
                            // - That is handled with magic in trans, so needs magic here (for inlining)
                            else if (ty->is_TraitObject()) {
                                ity_p = &ty;
                                // Remove the deref so downstream doesn't need to care
                                MIR_ASSERT(state, !re.val.m_wrappers.empty() && re.val.m_wrappers.back().is_Deref(), "DstPtr on bare trait object with no deref: " << re.val);
                                re.val.m_wrappers.pop_back();
                            } else {
                                BUG(Span(), "Unexpected input type for DstMeta - " << ty);
                            }
                            (void)ity_p; // TODO: What is this needed for?
                        }
                        TU_ARMA(MakeDst, re) {
                            MIR_Cleanup_Param(state, mutator, re.ptr_val);
                            MIR_Cleanup_Param(state, mutator, re.meta_val);
                        }
                        TU_ARMA(Tuple, re) {
                            for (auto& lv : re.vals) {
                                MIR_Cleanup_Param(state, mutator, lv);
                            }
                        }
                        TU_ARMA(Array, re) {
                            for (auto& lv : re.vals) {
                                MIR_Cleanup_Param(state, mutator, lv);
                            }
                        }
                        TU_ARMA(UnionVariant, re) {
                            MIR_Cleanup_Param(state, mutator, re.val);
                        }
                        TU_ARMA(EnumVariant, re) {
                            for (auto& lv : re.vals) {
                                MIR_Cleanup_Param(state, mutator, lv);
                            }
                        }
                        TU_ARMA(Struct, re) {
                            for (auto& lv : re.vals) {
                                MIR_Cleanup_Param(state, mutator, lv);
                            }
                        }
                }
                }
            }

            // 2. RValue conversions
            if( stmt.is_Assign() )
            {
                auto& se = stmt.as_Assign();

                if (auto* e = se.src.opt_Constant()) {
                    // Replace `Const` with actual values
                    if (auto* ce = e->opt_Const()) {
                        // 1. Find the constant
                        MonomorphState params(state.m_crate.m_types);
                        ::HIR::TypeRef ty;
                        const auto* lit_ptr = MIR_Cleanup_GetConstant(state, *ce->p, ty, params);
                        if (lit_ptr) {
                            DEBUG("Replace constant " << *ce->p << " with " << *lit_ptr);
                            se.src = MIR_Cleanup_LiteralToRValue(state, mutator, *lit_ptr, mv$(ty), params, mv$(*ce->p));
                            if (auto* p = se.src.opt_Constant()) {
                                MIR_Cleanup_Constant(state, mutator, *p);
                            }
                        } else {
                            DEBUG("No replacement for constant " << *ce->p);
                        }
                    }
                }

                // Fix up coercions
                if (auto* e = se.src.opt_MakeDst()) {
                    if (TU_TEST2(e->meta_val, Constant, , ItemAddr, .get() == nullptr)) {
                        ::HIR::TypeRef tmp, tmp2;
                        const auto& src_ty = state.get_param_type(tmp, e->ptr_val);
                        const auto& dst_ty = state.get_lvalue_type(tmp2, se.dst);
                        MIR_ASSERT(state, e->ptr_val.is_LValue(), "BUG: MakeDst with no metadata should be LValue");
                        se.src = MIR_Cleanup_CoerceUnsized(state, mutator, dst_ty, src_ty, mv$(e->ptr_val.as_LValue()));
                    }
                }

                if (auto* e = se.src.opt_MakeDst()) {
                    if (TU_TEST2(e->meta_val, Constant, , ItemAddr, .get() == nullptr)) {
                        // TODO: Check the validity?
                        // - Ensure that something is generic in either the destination or source
                        ::HIR::TypeRef tmp;
                        const auto& src_ty = state.get_param_type(tmp, e->ptr_val);
                        MIR_ASSERT(state, monomorphise_type_needed(src_ty), "MakeDst Unsize with known source - " << src_ty);
                    }
                }
            }

            //DEBUG(it - block.statements.begin());
            it = mutator.flush_stmt();
            //DEBUG(it - block.statements.begin());
        }

        mutator.update_state(state);
        //state.set_cur_stmt_term( mutator.cur_block );

        TU_MATCH_HDRA( (block.terminator), {)
        TU_ARMA(Incomplete, e) {
            }
            TU_ARMA(Return, e) {
            }
            TU_ARMA(Diverge, e) {
            }
            TU_ARMA(Goto, e) {
            }
            TU_ARMA(Panic, e) {
            }
            TU_ARMA(If, e) {
                MIR_Cleanup_LValue(state, mutator, e.cond);
            }
            TU_ARMA(Switch, e) {
                MIR_Cleanup_LValue(state, mutator, e.val);
            }
            TU_ARMA(SwitchValue, e) {
                MIR_Cleanup_LValue(state, mutator, e.val);
            }
            TU_ARMA(Call, e) {
                MIR_Cleanup_LValue(state, mutator, e.ret_val);
                if (e.fcn.is_Value()) {
                    MIR_Cleanup_LValue(state, mutator, e.fcn.as_Value());
                }
                for (auto& lv : e.args) {
                    MIR_Cleanup_Param(state, mutator, lv);
                }
            }
        }

        // VTable calls
        if(auto* ep = block.terminator.opt_Call())
        {
            auto& e = *ep;
            if (auto* path_p = e.fcn.opt_Path()) {
                auto& path = *path_p;
                // Detect calling `<Trait as Trait>::method()` and replace with vtable call
                if (path.m_data.is_UfcsKnown() && path.m_data.as_UfcsKnown().type->is_TraitObject()) {
                    const auto& pe = path.m_data.as_UfcsKnown();
                    const auto& te = pe.type->as_TraitObject();
                    // TODO: What if the method is from a supertrait?

                    if (te.m_trait.m_path == pe.trait || resolve.find_named_trait_in_trait(sp, pe.trait.m_path, pe.trait.m_params, *te.m_trait.m_trait_ptr, te.m_trait.m_path.m_path, te.m_trait.m_path.m_params, pe.type, [](const auto&, auto) {
                        return true;
                    })) {
                        auto tgt_lvalue = MIR_Cleanup_Virtualize(sp, state, mutator, e.args.front().as_LValue(), pe);
                        e.fcn = mv$(tgt_lvalue);
                    }
                }

                if (path.m_data.is_UfcsKnown() && path.m_data.as_UfcsKnown().type->is_Function()) {
                    const auto& pe = path.m_data.as_UfcsKnown();
                    const auto& fcn_ty = pe.type->as_Function();
                    if (pe.trait.m_path == resolve.m_lang_Fn || pe.trait.m_path == resolve.m_lang_FnMut || pe.trait.m_path == resolve.m_lang_FnOnce) {
                        MIR_ASSERT(state, e.args.size() == 2, "Fn* call requires two arguments");
                        auto fcn_lvalue = mv$(e.args[0].as_LValue());
                        auto args_lvalue = mv$(e.args[1].as_LValue());

                        DEBUG("Convert function pointer call");

                        e.args.clear();
                        e.args.reserve(fcn_ty.m_arg_types.size());
                        for (unsigned int i = 0; i < fcn_ty.m_arg_types.size(); i++) {
                            e.args.push_back(::MIR::LValue::new_Field(args_lvalue.clone(), i));
                        }
                        // If the trait is Fn/FnMut, dereference the input value.
                        if (pe.trait.m_path == resolve.m_lang_FnOnce) {
                            e.fcn = mv$(fcn_lvalue);
                        } else {
                            e.fcn = ::MIR::LValue::new_Deref(mv$(fcn_lvalue));
                        }
                    }
                }
                if (path.m_data.is_UfcsKnown() && path.m_data.as_UfcsKnown().type->is_NamedFunction()) {
                    const auto& pe = path.m_data.as_UfcsKnown();
                    const auto& fcn_ty = pe.type->as_NamedFunction();
                    if (pe.trait.m_path == resolve.m_lang_Fn || pe.trait.m_path == resolve.m_lang_FnMut || pe.trait.m_path == resolve.m_lang_FnOnce) {
                        auto n_args = fcn_ty.decay(state.m_crate.m_types, state.sp).m_arg_types.size();
                        MIR_ASSERT(state, e.args.size() == 2, "Fn* call requires two arguments");
                        auto fcn_lvalue = mv$(e.args[0].as_LValue());
                        auto args_lvalue = mv$(e.args[1].as_LValue());

                        DEBUG("Convert named function pointer call");

                        e.args.clear();
                        e.args.reserve(n_args);
                        for (unsigned int i = 0; i < n_args; i++) {
                            e.args.push_back(::MIR::LValue::new_Field(args_lvalue.clone(), i));
                        }
                        TU_MATCH_HDRA( (fcn_ty.def), {)
                        TU_ARMA(Function, ve) {
                                e.fcn = fcn_ty.path.clone();
                            }
                            TU_ARMA(StructConstructor, ve) {
                                block.statements.push_back(::MIR::Statement::make_Assign({std::move(e.ret_val), MIR::RValue::make_Struct({fcn_ty.path.m_data.as_Generic().clone(), std::move(e.args)})}));
                                block.terminator = MIR::Terminator::make_Goto(e.ret_block);
                            }
                            TU_ARMA(EnumConstructor, ve) {
                                auto enm_path = fcn_ty.path.m_data.as_Generic().clone();
                                enm_path.m_path.pop_component();
                                block.statements.push_back(::MIR::Statement::make_Assign({std::move(e.ret_val), MIR::RValue::make_EnumVariant({std::move(enm_path), static_cast<unsigned>(ve.v), std::move(e.args)})}));
                                block.terminator = MIR::Terminator::make_Goto(e.ret_block);
                            }
                        }
                    }
                }
            }

            // NOTE: Would be nice to do this in `Lower_MIR` - but that confuses the validity checks
            if (e.fcn.is_Intrinsic() && e.fcn.as_Intrinsic().name == "read_via_copy") {
                // TODO: Replace with `res = *ptr;`
                block.statements.push_back(MIR::Statement::make_Assign({std::move(e.ret_val), MIR::LValue::new_Deref(std::move(e.args.at(0).as_LValue()))}));
                block.terminator = MIR::Terminator::make_Goto(e.ret_block);
            }
            if (e.fcn.is_Intrinsic() && e.fcn.as_Intrinsic().name == "write_via_move") {
                // TODO: Replace with `*ptr = arg;`
                block.statements.push_back(MIR::Statement::make_Assign({MIR::LValue::new_Deref(std::move(e.args.at(0).as_LValue())), std::move(e.args.at(1).as_LValue())}));
                block.statements.push_back(MIR::Statement::make_Assign({std::move(e.ret_val), MIR::RValue::make_Tuple({})}));
                block.terminator = MIR::Terminator::make_Goto(e.ret_block);
            }
        }

        mutator.flush_block();
    }

}

void MIR_CleanupCrate(::HIR::Crate& crate) {
    ::MIR::OuterVisitor ov{crate, [&](const auto& res, const auto& p, ::HIR::ExprPtr& expr_ptr, const auto& args, const auto& ty) {
        if (expr_ptr) {
            MIR_Cleanup(res, p, expr_ptr.get_mir_or_error_mut(Span()), args, ty);
            MIR_Validate(res, p, expr_ptr.get_mir_or_error_mut(Span()), args, ty);
        }
    }};
    ov.visit_crate(crate);
}

void MIR_Cleanup_SetPostMonomorph() {
    g_is_post_monomorph = true;
}

#include "mir_main_bindings.h"
#include "mir_mir.h"
#include "hir_visitor.h"
#include "hir_typeck_static.h"
#include "mir_helpers.h"
#include "mir_operations.h"
#include "mir_visit_crate_mir.h"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <unordered_map>
#include <unordered_set>
#include "trans_target.h"
#include "trans_trans_list.h" // Note: This is included for inlining after enumeration and monomorph

#include "hir_expr.h" // The optimiser section accesses complete HIR expression nodes.

#define DUMP_BEFORE_ALL 1
#define DUMP_BEFORE_CONSTPROPAGATE 0
#define DUMP_AFTER_PASS 1
#define DUMP_AFTER_ALL 0

#define DUMP_AFTER_DONE 1
#define CHECK_AFTER_DONE 2 // 1 = Check before GC, 2 = check before and after GC

// ----
// List of optimisations avaliable
// ----
bool MIR_Optimise_BlockSimplify(::MIR::TypeResolve& state, ::MIR::Function& fcn);
bool MIR_Optimise_Inlining(::MIR::TypeResolve& state, ::MIR::Function& fcn, bool minimal, const TransList* list = nullptr);
bool MIR_Optimise_SplitAggregates(::MIR::TypeResolve& state, ::MIR::Function& fcn);
bool MIR_Optimise_PropagateSingleAssignments(::MIR::TypeResolve& state, ::MIR::Function& fcn);
bool MIR_Optimise_PropagateKnownValues(::MIR::TypeResolve& state, ::MIR::Function& fcn);
bool MIR_Optimise_DeTemporary(::MIR::TypeResolve& state, ::MIR::Function& fcn); // Eliminate useless temporaries
bool MIR_Optimise_UnifyTemporaries(::MIR::TypeResolve& state, ::MIR::Function& fcn);
bool MIR_Optimise_CommonStatements(::MIR::TypeResolve& state, ::MIR::Function& fcn);
bool MIR_Optimise_UnifyBlocks(::MIR::TypeResolve& state, ::MIR::Function& fcn);
bool MIR_Optimise_ConstPropagate(::MIR::TypeResolve& state, ::MIR::Function& fcn);
bool MIR_Optimise_DeadDropFlags(::MIR::TypeResolve& state, ::MIR::Function& fcn);
bool MIR_Optimise_DeadAssignments(::MIR::TypeResolve& state, ::MIR::Function& fcn);
bool MIR_Optimise_NoopRemoval(::MIR::TypeResolve& state, ::MIR::Function& fcn);
bool MIR_Optimise_GotoAssign(::MIR::TypeResolve& state, ::MIR::Function& fcn);
bool MIR_Optimise_UselessReborrows(::MIR::TypeResolve& state, ::MIR::Function& fcn);
bool MIR_Optimise_GarbageCollect_Partial(::MIR::TypeResolve& state, ::MIR::Function& fcn);
bool MIR_Optimise_GarbageCollect(::MIR::TypeResolve& state, ::MIR::Function& fcn);

enum {
    CHECKMODE_UNKNOWN,
    CHECKMODE_NONE,
    CHECKMODE_FINAL,
    CHECKMODE_PASS,
    CHECKMODE_ALL,
};

static int check_mode() {
    static int mode = CHECKMODE_UNKNOWN;
    if (mode == CHECKMODE_UNKNOWN) {
        const auto* n = getenv("MRUSTC_MIR_CHECK");
        if (n) {
            if (strcmp(n, "none") == 0) {
                mode = CHECKMODE_NONE;
            } else if (strcmp(n, "final") == 0) {
                mode = CHECKMODE_FINAL;
            } else if (strcmp(n, "pass") == 0) {
                mode = CHECKMODE_PASS;
            } else if (strcmp(n, "all") == 0) {
                mode = CHECKMODE_ALL;
            } else {
                WARNING(
                    Span(),
                    W0000,
                    "Unknown value for $MRUSTC_MIR_CHECK - '" << n << "'"
                                                              << ": options are 'none','final','pass','all'"
                );
            }
        }

        if (mode == CHECKMODE_UNKNOWN) {
            mode = CHECKMODE_FINAL;
        }
    }
    return mode;
}

static bool check_after_all() {
    return check_mode() >= CHECKMODE_ALL;
}

/// A minimum set of optimisations:
/// - Runs only the mandatory-inlining hook, not normal cost-based inlining
/// - Simplifies the call graph (by removing chained gotos)
/// - Sorts blocks into a rough flow order
void MIR_OptimiseMin(const StaticTraitResolve& resolve, const ::HIR::ItemPath& path, ::MIR::Function& fcn, const ::HIR::Function::args_t& args, const ::HIR::TypeRef& ret_type) {
    static Span sp;
    TRACE_FUNCTION_F(path);
    ::MIR::TypeResolve state {
        sp, resolve, FMT_CB(ss, ss << path;), ret_type, args, fcn
    };

    while (MIR_Optimise_Inlining(state, fcn, true)) {
        MIR_Cleanup(resolve, path, fcn, args, ret_type);
        //MIR_Dump_Fcn(::std::cout, fcn);
        if (check_after_all()) {
            MIR_Validate(resolve, path, fcn, args, ret_type);
        }
    }

    MIR_Optimise_BlockSimplify(state, fcn);
    MIR_Optimise_UnifyBlocks(state, fcn);

    //MIR_Optimise_GarbageCollect_Partial(state, fcn);

    // NOTE: No check here, this version of optimise is pretty reliable
    //if( check_mode() >= CHECKMODE_FINAL ) {
    //    MIR_Validate(resolve, path, fcn, args, ret_type);
    //}
    MIR_Optimise_GarbageCollect(state, fcn);
    //MIR_Validate_Full(resolve, path, fcn, args, ret_type);
    MIR_SortBlocks(resolve, path, fcn);

#if CHECK_AFTER_DONE > 1
    if (check_mode() >= CHECKMODE_FINAL) {
        MIR_Validate(resolve, path, fcn, args, ret_type);
    }
#endif
    return;
}

/// Perfom inlining only, using a list of monomorphised functions, then cleans up the flow graph
///
/// Returns true if any optimisation was performed
bool MIR_OptimiseInline(const StaticTraitResolve& resolve, const ::HIR::ItemPath& path, ::MIR::Function& fcn, const ::HIR::Function::args_t& args, const ::HIR::TypeRef& ret_type, const TransList& list, unsigned opt_level) {
    static Span sp;
    bool rv = false;
    TRACE_FUNCTION_FR(path, rv);
    ::MIR::TypeResolve state {
        sp, resolve, FMT_CB(ss, ss << path;), ret_type, args, fcn
    };

    while (MIR_Optimise_Inlining(state, fcn, false, &list)) {
        MIR_Cleanup(resolve, path, fcn, args, ret_type);
        if (check_after_all()) {
            MIR_Validate(resolve, path, fcn, args, ret_type);
        }
        rv = true;
    }

    if (rv) {
        MIR_Optimise(resolve, path, fcn, args, ret_type, opt_level, /*do_inline=*/false);
    }

    return rv;
}

void MIR_Optimise(const StaticTraitResolve& resolve, const ::HIR::ItemPath& path, ::MIR::Function& fcn, const ::HIR::Function::args_t& args, const ::HIR::TypeRef& ret_type, unsigned opt_level, bool do_inline /*=true*/, bool validate /*=true*/) {
    static Span sp;
    assert(opt_level > 0);
    TRACE_FUNCTION_F(path);
    ::MIR::TypeResolve state {
        sp, resolve, FMT_CB(ss, ss << path;), ret_type, args, fcn
    };

    bool change_happened;
    unsigned int pass_num = 0;
    do {
        MIR_ASSERT(state, pass_num < 100, "Too many MIR optimisation iterations");

        change_happened = false;
        TRACE_FUNCTION_FR("Pass " << pass_num, change_happened);

        // >> Simplify call graph (removes gotos to blocks with a single use)
        if (MIR_Optimise_BlockSimplify(state, fcn)) {
#if DUMP_AFTER_ALL
            if (debug_enabled()) {
                MIR_Dump_Fcn(::std::cout, fcn);
            }
#endif
            if (check_after_all()) {
                MIR_Validate(resolve, path, fcn, args, ret_type);
            }
            // NOTE: Don't set `change_happened`, as this is the first pass
        }
        //else { MIR_Validate(resolve, path, fcn, args, ret_type); }

        // >> Apply known constants
        if (MIR_Optimise_ConstPropagate(state, fcn)) {
#if DUMP_AFTER_ALL
            if (debug_enabled()) {
                MIR_Dump_Fcn(::std::cout, fcn);
            }
#endif
            if (check_after_all()) {
                MIR_Validate(resolve, path, fcn, args, ret_type);
            }
            change_happened = true;
        }

        // >> Attempt to remove useless temporaries
        if (MIR_Optimise_DeTemporary(state, fcn)) {
            // - Run until no changes
            while (MIR_Optimise_DeTemporary(state, fcn)) {
                if (check_after_all()) {
                    MIR_Validate(resolve, path, fcn, args, ret_type);
                }
            }
#if DUMP_AFTER_ALL
            if (debug_enabled()) {
                MIR_Dump_Fcn(::std::cout, fcn);
            }
#endif
            if (check_after_all()) {
                MIR_Validate(resolve, path, fcn, args, ret_type);
            }
            change_happened = true;
        }
        //else { MIR_Validate(resolve, path, fcn, args, ret_type); }

        // Level 2 adds the more expensive whole-local/dataflow transformations,
        // matching rustc's split between basic level-1 cleanup and its SROA/GVN/DSE tier.
        // >> Split apart aggregates that are never used such (Written once, never used directly)
        if (opt_level >= 2 && MIR_Optimise_SplitAggregates(state, fcn)) {
#if DUMP_AFTER_ALL
            if (debug_enabled()) {
                MIR_Dump_Fcn(::std::cout, fcn);
            }
#endif
            if (check_after_all()) {
                MIR_Validate(resolve, path, fcn, args, ret_type);
            }
            change_happened = true;
        }
        //else { MIR_Validate(resolve, path, fcn, args, ret_type); }

        // >> Replace values from composites if they're known
        //   - Undoes the inefficiencies from the `match (a, b) { ... }` pattern
        if (opt_level >= 2 && MIR_Optimise_PropagateKnownValues(state, fcn)) {
#if DUMP_AFTER_ALL
            if (debug_enabled()) {
                MIR_Dump_Fcn(::std::cout, fcn);
            }
#endif
            if (check_after_all()) {
                MIR_Validate(resolve, path, fcn, args, ret_type);
            }
            change_happened = true;
        }
        //else { MIR_Validate(resolve, path, fcn, args, ret_type); }

        // TODO: Convert `&mut *mut_foo` into `mut_foo` if the source is movable and not used afterwards

        // >> Propagate/remove dead assignments
        if (MIR_Optimise_PropagateSingleAssignments(state, fcn)) {
            // - Run until no changes
            while (MIR_Optimise_PropagateSingleAssignments(state, fcn)) {
            }
#if DUMP_AFTER_ALL
            if (debug_enabled()) {
                MIR_Dump_Fcn(::std::cout, fcn);
            }
#endif
            if (check_after_all()) {
                MIR_Validate(resolve, path, fcn, args, ret_type);
            }
            change_happened = true;
        }
        //else { MIR_Validate(resolve, path, fcn, args, ret_type); }

        // >> Move common statements (assignments) across gotos.
        //if( MIR_Optimise_CommonStatements(state, fcn) )
        //{
        //    if( check_after_all() ) {
        //        MIR_Validate(resolve, path, fcn, args, ret_type);
        //    }
        //    change_happened = true;
        //}

        // >> Combine Duplicate Blocks
        if (MIR_Optimise_UnifyBlocks(state, fcn)) {
#if DUMP_AFTER_ALL
            if (debug_enabled()) {
                MIR_Dump_Fcn(::std::cout, fcn);
            }
#endif
            if (check_after_all()) {
                MIR_Validate(resolve, path, fcn, args, ret_type);
            }
            change_happened = true;
        }
        // >> Remove assignments of unsed drop flags
        if (MIR_Optimise_DeadDropFlags(state, fcn)) {
#if DUMP_AFTER_ALL
            if (debug_enabled()) {
                MIR_Dump_Fcn(::std::cout, fcn);
            }
#endif
            if (check_after_all()) {
                MIR_Validate(resolve, path, fcn, args, ret_type);
            }
            change_happened = true;
        }
        // >> Remove assignments that are never read
        if (opt_level >= 2 && MIR_Optimise_DeadAssignments(state, fcn)) {
#if DUMP_AFTER_ALL
            if (debug_enabled()) {
                MIR_Dump_Fcn(::std::cout, fcn);
            }
#endif
            if (check_after_all()) {
                MIR_Validate(resolve, path, fcn, args, ret_type);
            }
            change_happened = true;
        }
        // >> Remove no-op assignments
        if (MIR_Optimise_NoopRemoval(state, fcn)) {
#if DUMP_AFTER_ALL
            if (debug_enabled()) {
                MIR_Dump_Fcn(::std::cout, fcn);
            }
#endif
            if (check_after_all()) {
                MIR_Validate(resolve, path, fcn, args, ret_type);
            }
            change_happened = true;
        }

        // >> Remove re-borrow operations that don't need to exist
        if (MIR_Optimise_UselessReborrows(state, fcn)) {
#if DUMP_AFTER_ALL
            if (debug_enabled()) {
                MIR_Dump_Fcn(::std::cout, fcn);
            }
#endif
            if (check_after_all()) {
                MIR_Validate(resolve, path, fcn, args, ret_type);
            }
            change_happened = true;
        }

        // >> If the first statement of a block is an assignment, and the last op of the previous is to that assignment's source, move up.
        if (MIR_Optimise_GotoAssign(state, fcn)) {
#if DUMP_AFTER_ALL
            if (debug_enabled()) {
                MIR_Dump_Fcn(::std::cout, fcn);
            }
#endif
            if (check_after_all()) {
                MIR_Validate(resolve, path, fcn, args, ret_type);
            }
            change_happened = true;
        }

        // >> Inline short functions
        if (do_inline && !change_happened) {
            if (MIR_Optimise_Inlining(state, fcn, /*minimal=*/false)) {
                // Apply cleanup again (as monomorpisation in inlining may have exposed a vtable call)
                MIR_Cleanup(resolve, path, fcn, args, ret_type);
                //MIR_Dump_Fcn(::std::cout, fcn);
#if DUMP_AFTER_ALL
                if (debug_enabled()) {
                    MIR_Dump_Fcn(::std::cout, fcn);
                }
#endif
                if (check_after_all()) {
                    MIR_Validate(resolve, path, fcn, args, ret_type);
                }
                change_happened = true;
            }
        }

        if (change_happened) {
#if DUMP_AFTER_PASS
            if (debug_enabled()) {
                MIR_Dump_Fcn(::std::cout, fcn);
            }
#endif
            if (check_mode() == CHECKMODE_PASS) { // NOTE: Skipped if CHECKMODE_ALL
                MIR_Validate(resolve, path, fcn, args, ret_type);
            }
        }
        //else { MIR_Validate(resolve, path, fcn, args, ret_type); }

        if (MIR_Optimise_GarbageCollect_Partial(state, fcn)) {
            change_happened = true;
#if DUMP_AFTER_ALL
            if (debug_enabled()) {
                MIR_Dump_Fcn(::std::cout, fcn);
            }
#endif
            if (check_after_all()) {
                MIR_Validate(resolve, path, fcn, args, ret_type);
            }
        }
        //else { MIR_Validate(resolve, path, fcn, args, ret_type); }

        pass_num += 1;
    } while (change_happened);

    // Run UnifyTemporaries last, then unify blocks, then run some
    // optimisations that might be affected

#if DUMP_AFTER_DONE
    if (debug_enabled()) {
        MIR_Dump_Fcn(::std::cout, fcn);
    }
#endif
    if (validate && check_mode() >= CHECKMODE_FINAL) {
        // DEFENCE: Run validation _before_ GC (so validation errors refer to the pre-gc numbers)
        MIR_Validate(resolve, path, fcn, args, ret_type);
    }
    // GC pass on blocks and variables
    // - Find unused blocks, then delete and rewrite all references.
    MIR_Optimise_GarbageCollect(state, fcn);

    //MIR_Validate_Full(resolve, path, fcn, args, ret_type);

    MIR_SortBlocks(resolve, path, fcn);
    if (validate && check_mode() >= CHECKMODE_FINAL) {
        MIR_Validate(resolve, path, fcn, args, ret_type);
    }
}

namespace {
    enum class ValUsage {
        Move,   // Moving read (even if T: Copy)
        Read,   // Non-moving read (e.g. indexing or deref, TODO: &move pointers?)
        Write,  // Mutation
        Borrow, // Any borrow
    };

    bool visit_mir_lvalues_inner(const ::MIR::LValue& lv, ValUsage u, ::std::function<bool(const ::MIR::LValue&, ValUsage)> cb) {
        for (const auto& w : lv.m_wrappers) {
            if (w.is_Index()) {
                if (cb(::MIR::LValue::new_Local(w.as_Index()), ValUsage::Read)) {
                    return true;
                }
            } else if (w.is_Deref()) {
                //u = ValUsage::Read;
            }
        }
        return cb(lv, u);
    }

    bool visit_mir_lvalue_mut(::MIR::LValue& lv, ValUsage u, ::std::function<bool(::MIR::LValue::MRef&, ValUsage)> cb) {
        auto lvr = ::MIR::LValue::MRef(lv);
        do {
            if (cb(lvr, u)) {
                return true;
            }
            // TODO: Use a TU_MATCH?
            if (lvr.is_Index()) {
                auto ilv = ::MIR::LValue::new_Local(lvr.as_Index());
                auto ilv_r = ::MIR::LValue::MRef(ilv);
                bool rv = cb(ilv_r, ValUsage::Read);
                assert(ilv.is_Local() && ilv.as_Local() == lvr.as_Index());
                if (rv) {
                    return true;
                }
            } else if (lvr.is_Field()) {
                // HACK: If "moving", use a "Read" value usage (covers some quirks)
                if (u == ValUsage::Move) {
                    u = ValUsage::Read;
                }
            } else if (lvr.is_Deref()) {
                // TODO: Is this right?
                if (u == ValUsage::Borrow) {
                    u = ValUsage::Read;
                }
            } else {
                // No change
            }
        } while (lvr.try_unwrap());
        return false;
    }

    bool visit_mir_lvalue_raw_mut(::MIR::LValue& lv, ValUsage u, ::std::function<bool(::MIR::LValue&, ValUsage)> cb) {
        return cb(lv, u);
    }

    bool visit_mir_lvalue_mut(::MIR::Param& p, ValUsage u, ::std::function<bool(::MIR::LValue&, ValUsage)> cb) {
        if (auto* e = p.opt_LValue()) {
            return visit_mir_lvalue_raw_mut(*e, u, cb);
        } else {
            return false;
        }
    }

    bool visit_mir_lvalues_mut(::MIR::RValue& rval, ::std::function<bool(::MIR::LValue&, ValUsage)> cb) {
        bool rv = false;
        TU_MATCH_HDRA( (rval), {)
        TU_ARMA(Use, se) {
                rv |= visit_mir_lvalue_raw_mut(se, ValUsage::Move, cb); // Can move
            }
            TU_ARMA(Constant, se) {
            }
            TU_ARMA(SizedArray, se) {
                rv |= visit_mir_lvalue_mut(se.val, ValUsage::Read, cb); // Has to be Read
            }
            TU_ARMA(Borrow, se) {
                rv |= visit_mir_lvalue_raw_mut(se.val, ValUsage::Borrow, cb);
            }
            TU_ARMA(Cast, se) {
                rv |= visit_mir_lvalue_raw_mut(se.val, ValUsage::Read, cb); // Also has to be read
            }
            TU_ARMA(BinOp, se) {
                rv |= visit_mir_lvalue_mut(se.val_l, ValUsage::Read, cb); // Same
                rv |= visit_mir_lvalue_mut(se.val_r, ValUsage::Read, cb);
            }
            TU_ARMA(UniOp, se) {
                rv |= visit_mir_lvalue_raw_mut(se.val, ValUsage::Read, cb);
            }
            TU_ARMA(DstMeta, se) {
                rv |= visit_mir_lvalue_raw_mut(se.val, ValUsage::Read, cb); // Reads
            }
            TU_ARMA(DstPtr, se) {
                rv |= visit_mir_lvalue_raw_mut(se.val, ValUsage::Read, cb);
            }
            TU_ARMA(MakeDst, se) {
                rv |= visit_mir_lvalue_mut(se.ptr_val, ValUsage::Move, cb);
                rv |= visit_mir_lvalue_mut(se.meta_val, ValUsage::Read, cb); // Note, metadata has to be Copy
            }
            TU_ARMA(Tuple, se) {
                for (auto& v : se.vals) {
                    rv |= visit_mir_lvalue_mut(v, ValUsage::Move, cb);
                }
            }
            TU_ARMA(Array, se) {
                for (auto& v : se.vals) {
                    rv |= visit_mir_lvalue_mut(v, ValUsage::Move, cb);
                }
            }
            TU_ARMA(UnionVariant, se) {
                rv |= visit_mir_lvalue_mut(se.val, ValUsage::Move, cb);
            }
            TU_ARMA(EnumVariant, se) {
                for (auto& v : se.vals) {
                    rv |= visit_mir_lvalue_mut(v, ValUsage::Move, cb);
                }
            }
            TU_ARMA(Struct, se) {
                for (auto& v : se.vals) {
                    rv |= visit_mir_lvalue_mut(v, ValUsage::Move, cb);
                }
            }
        }
        return rv;
    }

    bool visit_mir_lvalues(const ::MIR::RValue& rval, ::std::function<bool(const ::MIR::LValue&, ValUsage)> cb) {
        return visit_mir_lvalues_mut(const_cast<::MIR::RValue&>(rval), [&](auto& lv, auto u) {
            return cb(lv, u);
        });
    }

    bool visit_mir_lvalues_mut(::MIR::Statement& stmt, ::std::function<bool(::MIR::LValue&, ValUsage)> cb) {
        bool rv = false;
        TU_MATCH_HDRA( (stmt), {)
        TU_ARMA(Assign, e) {
                rv |= visit_mir_lvalues_mut(e.src, cb);
                rv |= visit_mir_lvalue_raw_mut(e.dst, ValUsage::Write, cb);
            }
            TU_ARMA(Asm, e) {
                for (auto& v : e.inputs) {
                    rv |= visit_mir_lvalue_raw_mut(v.second, ValUsage::Read, cb);
                }
                for (auto& v : e.outputs) {
                    rv |= visit_mir_lvalue_raw_mut(v.second, ValUsage::Write, cb);
                }
            }
            TU_ARMA(Asm2, e) {
                for (auto& p : e.params) {
                TU_MATCH_HDRA( (p), { )
                TU_ARMA(Const, v) {
                        }
                        TU_ARMA(Sym, v) {
                        }
                        TU_ARMA(Reg, v) {
                            if (v.input) {
                                rv |= visit_mir_lvalue_mut(*v.input, ValUsage::Read, cb);
                            }
                            if (v.output) {
                                rv |= visit_mir_lvalue_raw_mut(*v.output, ValUsage::Write, cb);
                            }
                        }
                }
                }
            }
            TU_ARMA(SetDropFlag, e) {
            }
            TU_ARMA(SaveDropFlag, e) {
                rv |= visit_mir_lvalue_raw_mut(e.slot, ValUsage::Write, cb);
            }
            TU_ARMA(LoadDropFlag, e) {
                rv |= visit_mir_lvalue_raw_mut(e.slot, ValUsage::Read, cb);
            }
            TU_ARMA(Drop, e) {
                // Well, it mutates...
                rv |= visit_mir_lvalue_raw_mut(e.slot, ValUsage::Write, cb);
            }
            TU_ARMA(ScopeEnd, e) {
            }
        }
        return rv;
    }

    bool visit_mir_lvalues(const ::MIR::Statement& stmt, ::std::function<bool(const ::MIR::LValue&, ValUsage)> cb) {
        return visit_mir_lvalues_mut(const_cast<::MIR::Statement&>(stmt), [&](auto& lv, auto im) {
            return cb(lv, im);
        });
    }

    bool visit_mir_lvalues_mut(::MIR::Terminator& term, ::std::function<bool(::MIR::LValue&, ValUsage)> cb) {
        bool rv = false;
        TU_MATCH_HDRA( (term), {)
        TU_ARMA(Incomplete, e) {
            }
            TU_ARMA(Return, e) {
            }
            TU_ARMA(Diverge, e) {
            }
            TU_ARMA(Goto, e) {
            }
            TU_ARMA(Panic, e) {
            }
            TU_ARMA(If, e) {
                rv |= visit_mir_lvalue_raw_mut(e.cond, ValUsage::Read, cb);
            }
            TU_ARMA(Switch, e) {
                rv |= visit_mir_lvalue_raw_mut(e.val, ValUsage::Read, cb);
            }
            TU_ARMA(SwitchValue, e) {
                rv |= visit_mir_lvalue_raw_mut(e.val, ValUsage::Read, cb);
            }
            TU_ARMA(Call, e) {
                if (e.fcn.is_Value()) {
                    rv |= visit_mir_lvalue_raw_mut(e.fcn.as_Value(), ValUsage::Read, cb);
                }
                for (auto& v : e.args) {
                    rv |= visit_mir_lvalue_mut(v, ValUsage::Move, cb);
                }
                rv |= visit_mir_lvalue_raw_mut(e.ret_val, ValUsage::Write, cb);
            }
        }
        return rv;
    }

    bool visit_mir_lvalues(const ::MIR::Terminator& term, ::std::function<bool(const ::MIR::LValue&, ValUsage)> cb) {
        return visit_mir_lvalues_mut(const_cast<::MIR::Terminator&>(term), [&](auto& lv, auto im) {
            return cb(lv, im);
        });
    }

    void visit_mir_lvalues_mut(::MIR::TypeResolve& state, ::MIR::Function& fcn, ::std::function<bool(::MIR::LValue&, ValUsage)> cb) {
        for (unsigned int block_idx = 0; block_idx < fcn.blocks.size(); block_idx++) {
            auto& block = fcn.blocks[block_idx];
            for (auto& stmt : block.statements) {
                state.set_cur_stmt(block_idx, (&stmt - &block.statements.front()));
                visit_mir_lvalues_mut(stmt, cb);
            }
            if (block.terminator.tag() == ::MIR::Terminator::TAGDEAD) {
                continue;
            }
            state.set_cur_stmt_term(block_idx);
            visit_mir_lvalues_mut(block.terminator, cb);
        }
    }

    void visit_mir_lvalues(::MIR::TypeResolve& state, const ::MIR::Function& fcn, ::std::function<bool(const ::MIR::LValue&, ValUsage)> cb) {
        visit_mir_lvalues_mut(state, const_cast<::MIR::Function&>(fcn), [&](auto& lv, auto im) {
            return cb(lv, im);
        });
    }

    struct ParamsSet: public MonomorphiserPP {
        ::HIR::PathParams impl_params;
        const ::HIR::PathParams* fcn_params;
        const ::HIR::TypeRef* self_ty;
        const ::HIR::GenericParams* impl_params_def;
        const ::HIR::GenericParams* fcn_params_def;

        ::HIR::PathParams fcn_params_tmp;

        explicit ParamsSet(HIR::TypeInterner& types)
            : MonomorphiserPP(types)
            , fcn_params(nullptr)
            , self_ty(nullptr)
            , impl_params_def(nullptr)
            , fcn_params_def(nullptr)
        {
        }

        const ::HIR::TypeRef* get_self_type() const override {
            return self_ty;
        }

        const ::HIR::PathParams* get_impl_params() const override {
            return &impl_params;
        }

        const ::HIR::PathParams* get_method_params() const override {
            return fcn_params;
        }

        const ::HIR::PathParams* get_hrb_params() const override {
            return nullptr;
        }

        bool has_unevaluated_values() const {
            const auto check = [](const ::HIR::PathParams& params) {
                return ::std::any_of(params.m_values.begin(), params.m_values.end(), [](const auto& value) {
                    return value.is_Unevaluated() || value.is_Infer();
                });
            };
            return check(impl_params) || (fcn_params && check(*fcn_params));
        }
    };

    const ::MIR::Function* get_called_mir(const ::MIR::TypeResolve& state, const TransList* list, const ::HIR::Path& path, ParamsSet& params) {
        MonomorphState out_params(state.m_resolve.m_crate.m_types);
        auto e = state.m_resolve.get_value(state.sp, path, out_params, /*sig_only*/ false, &params.impl_params_def);
        DEBUG(e.tag_str() << " " << out_params);
        params.fcn_params = out_params.get_method_params();
        params.impl_params = out_params.pp_impl == nullptr ? ::HIR::PathParams() : out_params.pp_impl == &out_params.pp_impl_data ? std::move(out_params.pp_impl_data) : out_params.pp_impl->clone();

        // If a TransList is avaliable, then all referenced functions must be in it.
        if (list) {
            const auto* trans_fcn = list->find_function(path);
            if (!trans_fcn) {
                MIR_BUG(state, "Enumeration failure - Function " << path << " not in TransList");
            }
            // TODO: Need identity params for most, but lifetime params need to be from the input.
            // Except, everything should already be monomorphised, so no identity required!
            //params.impl_params.m_lifetimes    = it->second->pp.pp_impl.m_lifetimes;
            //params.fcn_params_tmp.m_lifetimes = it->second->pp.pp_method.m_lifetimes;
            //params.fcn_params = &params.fcn_params_tmp;
            DEBUG("Found TransList " << path);
            DEBUG("impl_params = " << params.impl_params);
            DEBUG("fcn_params = " << *params.fcn_params);

            const auto& hir_fcn = *trans_fcn->ptr;
            if (trans_fcn->monomorphised.code) {
                //DEBUG("Found monomorphised - PP=" << params.impl_params << "," << *params.fcn_params);
                return &*trans_fcn->monomorphised.code;
            } else if (const auto* mir = hir_fcn.m_code.get_mir_opt()) {
                //DEBUG("Found concrete - PP=" << params.impl_params << "," << *params.fcn_params);
                MIR_ASSERT(state, hir_fcn.m_params.m_types.empty(), "Enumeration failure - Function had params, but wasn't monomorphised - " << path);
                // TODO: Check for trait methods too?
                return mir;
            } else {
                DEBUG("No MIR");
                MIR_ASSERT(state, !hir_fcn.m_code, "LowerMIR failure - No MIR but HIR is present?! - " << path);
                // External function (no MIR present)
                return nullptr;
            }
        }

        TU_MATCH_HDRA( (path.m_data), {)
        TU_ARMA(Generic, pe) {
                params.self_ty = nullptr;
            }
            TU_ARMA(UfcsKnown, pe) {
                params.self_ty = &pe.type;
            }
            TU_ARMA(UfcsInherent, pe) {
                params.self_ty = &pe.type;
            }
            TU_ARMA(UfcsUnknown, pe) {
                MIR_BUG(state, "UfcsUnknown hit - " << path);
            }
        }

        TU_MATCH_HDRA( (e), { )
        default:
            MIR_BUG(state, "MIR Call of " << e.tag_str() << " - " << path);
            TU_ARMA(NotFound, _) {
                return nullptr;
            }
            TU_ARMA(NotYetKnown, _) {
                return nullptr;
            }
            TU_ARMA(Function, f) {
                params.fcn_params_def = &f->m_params;
                return f->m_code.get_mir_opt();
            }
        }
        return nullptr;
    }

    void visit_terminator_target_mut(::MIR::Terminator& term, ::std::function<void(::MIR::BasicBlockId&)> cb) {
        TU_MATCH_HDRA( (term), {)
        TU_ARMA(Incomplete, e) {
            }
            TU_ARMA(Return, e) {
            }
            TU_ARMA(Diverge, e) {
            }
            TU_ARMA(Goto, e) {
                cb(e);
            }
            TU_ARMA(Panic, e) {
                cb(e.dst);
            }
            TU_ARMA(If, e) {
                cb(e.bb_true);
                cb(e.bb_false);
            }
            TU_ARMA(Switch, e) {
                for (auto& target : e.targets) {
                    cb(target);
                }
                if (e.valid_flag != ~0u) {
                    cb(e.invalid_target);
                }
            }
            TU_ARMA(SwitchValue, e) {
                for (auto& target : e.targets) {
                    cb(target);
                }
                cb(e.def_target);
            }
            TU_ARMA(Call, e) {
                cb(e.ret_block);
                cb(e.panic_block);
            }
        }
    }

    void visit_terminator_target(const ::MIR::Terminator& term, ::std::function<void(const ::MIR::BasicBlockId&)> cb) {
        visit_terminator_target_mut(const_cast<::MIR::Terminator&>(term), cb);
    }

    void visit_blocks_mut(::MIR::TypeResolve& state, ::MIR::Function& fcn, ::std::function<void(::MIR::BasicBlockId, ::MIR::BasicBlock&)> cb) {
        ::std::vector<bool> visited(fcn.blocks.size());
        ::std::vector<::MIR::BasicBlockId> to_visit;
        to_visit.push_back(0);
        while (to_visit.size() > 0) {
            auto bb = to_visit.back();
            to_visit.pop_back();
            if (visited[bb]) {
                continue;
            }
            visited[bb] = true;
            auto& block = fcn.blocks[bb];

            cb(bb, block);

            visit_terminator_target(block.terminator, [&](auto e) {
                if (!visited[e]) {
                    to_visit.push_back(e);
                }
            });
        }
    }

    void visit_blocks(::MIR::TypeResolve& state, const ::MIR::Function& fcn, ::std::function<void(::MIR::BasicBlockId, const ::MIR::BasicBlock&)> cb) {
        visit_blocks_mut(state, const_cast<::MIR::Function&>(fcn), [cb](auto id, auto& blk) {
            cb(id, blk);
        });
    }

    /// Convert a MIR::Param into a MIR::RValue
    MIR::RValue param_to_rvalue(MIR::Param param) {
        TU_MATCH_HDRA( (param), { )
        TU_ARMA(LValue, lv) {
                return mv$(lv);
            }
            TU_ARMA(Borrow, e) {
                return ::MIR::RValue::make_Borrow({e.type, false, mv$(e.val)});
            }
            TU_ARMA(Constant, c) {
                return mv$(c);
            }
        }
        throw std::runtime_error("Corrupted MIR::Param");
    }
} // namespace ""

// --------------------------------------------------------------------
// Performs basic simplications on the call graph (merging/removing blocks)
// --------------------------------------------------------------------
bool MIR_Optimise_BlockSimplify(::MIR::TypeResolve& state, ::MIR::Function& fcn) {
    bool changed = false;
    TRACE_FUNCTION_FR("", changed);

    struct H {
        static ::MIR::BasicBlockId get_new_target(const ::MIR::TypeResolve& state, ::MIR::BasicBlockId bb) {
            const auto& target = state.get_block(bb);
            if (target.statements.size() != 0) {
                return bb;
            } else if (!target.terminator.is_Goto()) {
                return bb;
            } else {
                // Make sure we don't infinite loop (TODO: What about mutual recursion?)
                if (bb == target.terminator.as_Goto()) {
                    return bb;
                }

                return get_new_target(state, target.terminator.as_Goto());
            }
        }
    };

    // >> Replace targets that point to a block that is just a goto
    for (auto& block : fcn.blocks) {
        visit_terminator_target_mut(block.terminator, [&](auto& e) {
            if (&fcn.blocks[e] != &block) {
                auto new_bb = H::get_new_target(state, e);
                if (new_bb != e) {
                    DEBUG("BB" << &block - fcn.blocks.data() << "/TERM: Rewrite bb reference " << e << " => " << new_bb);
                    e = new_bb;
                    changed = true;
                }
            }
        });

        // Handle chained switches of the same value
        // - Happens in libcore's atomics
        if (auto* te = block.terminator.opt_Switch()) {
            if (te->valid_flag != ~0u) {
                continue;
            }
            for (auto& t : te->targets) {
                auto idx = &t - &te->targets.front();
                // The block must be a terminator only, and be a switch over the same value.
                if (fcn.blocks[t].statements.empty() && fcn.blocks[t].terminator.is_Switch()) {
                    const auto& n_te = fcn.blocks[t].terminator.as_Switch();
                    if (n_te.valid_flag == ~0u && n_te.val == te->val) {
                        // If that's the case, then update this target with the equivalent from the new switch.
                        DEBUG("BB" << &block - fcn.blocks.data() << "/TERM: Update switch from BB" << t << " to BB" << n_te.targets[idx]);
                        t = n_te.targets[idx];
                        changed = true;
                    }
                }
            }
        }
    }

    // >> Unify sequential `ScopeEnd` statements
    for (auto& block : fcn.blocks) {
        if (block.statements.size() > 1) {
            for (auto it = block.statements.begin() + 1; it != block.statements.end();) {
                if ((it - 1)->is_ScopeEnd() && it->is_ScopeEnd()) {
                    auto& dst = (it - 1)->as_ScopeEnd();
                    const auto& src = it->as_ScopeEnd();
                    DEBUG("Unify " << *(it - 1) << " and " << *it);
                    for (auto v : src.slots) {
                        dst.slots.push_back(v);
                    }
                    ::std::sort(dst.slots.begin(), dst.slots.end());
                    it = block.statements.erase(it);
                    changed = true;
                } else {
                    ++it;
                }
            }
        }
    }

    // >> Merge blocks where a block goto-s to a single-use block.
    {
        ::std::vector<bool> visited(fcn.blocks.size());
        ::std::vector<unsigned int> uses(fcn.blocks.size());
        ::std::vector<::MIR::BasicBlockId> to_visit;
        to_visit.push_back(0);
        uses[0]++;
        while (to_visit.size() > 0) {
            auto bb = to_visit.back();
            to_visit.pop_back();
            if (visited[bb]) {
                continue;
            }
            visited[bb] = true;
            const auto& block = fcn.blocks[bb];

            visit_terminator_target(block.terminator, [&](const auto& e) {
                if (!visited[e]) {
                    to_visit.push_back(e);
                }
                uses[e]++;
            });
        }

        unsigned int i = 0;
        for (auto& block : fcn.blocks) {
            if (visited[i]) {
                while (block.terminator.is_Goto()) {
                    auto tgt = block.terminator.as_Goto();
                    if (uses[tgt] != 1) {
                        break;
                    }
                    if (tgt == i) {
                        break;
                    }
                    DEBUG("Append bb " << tgt << " to bb" << i);

                    assert(&fcn.blocks[tgt] != &block);
                    // Move contents of source block, then set the TAGDEAD terminator to Incomplete
                    auto src_block = mv$(fcn.blocks[tgt]);
                    fcn.blocks[tgt].terminator = ::MIR::Terminator::make_Incomplete({});

                    for (auto& stmt : src_block.statements) {
                        block.statements.push_back(mv$(stmt));
                    }
                    block.terminator = mv$(src_block.terminator);
                    changed = true;
                }
            }
            i++;
        }
    }

    // >> If a block GOTOs a block that is just a `RETURN` or `DIVERGE`, then change terminator
    for (auto& block : fcn.blocks) {
        state.set_cur_stmt_term(&block - &fcn.blocks.front());
        if (block.terminator.is_Goto()) {
            auto tgt = block.terminator.as_Goto();
            if (!fcn.blocks[tgt].statements.empty()) {
            } else if (fcn.blocks[tgt].terminator.is_Return()) {
                DEBUG(state << " -> Return");
                block.terminator = MIR::Terminator::make_Return({});
                changed = true;
            } else if (fcn.blocks[tgt].terminator.is_Diverge()) {
                DEBUG(state << " -> Diverge");
                block.terminator = MIR::Terminator::make_Diverge({});
                changed = true;
            } else {
                // No replace
            }
        }
    }

    // NOTE: Not strictly true, but these can't trigger other optimisations
    return false;
}

// --------------------------------------------------------------------
// If two temporaries don't overlap in lifetime (blocks in which they're valid), unify the two
// --------------------------------------------------------------------
bool MIR_Optimise_Inlining(::MIR::TypeResolve& state, ::MIR::Function& fcn, bool minimal, const TransList* list /*=nullptr*/) {
    bool inline_happened = false;
    TRACE_FUNCTION_FR("", inline_happened);

    struct InlineEvent {
        ::HIR::Path path;
        ::std::vector<size_t> bb_list;

        InlineEvent(::HIR::Path p)
            : path(::std::move(p))
        {
        }

        bool has_bb(size_t i) const {
            return ::std::find(this->bb_list.begin(), this->bb_list.end(), i) != this->bb_list.end();
        }

        void add_range(size_t start, size_t count) {
            for (size_t j = 0; j < count; j++) {
                this->bb_list.push_back(start + j);
            }
        }
    };

    ::std::vector<InlineEvent> inlined_functions;

    struct H {
        struct Source {
            unsigned bb_idx;
            unsigned stmt_idx;
            const ::MIR::Statement* stmt;

            Source(unsigned bb_idx, unsigned stmt_idx, const ::MIR::Statement* stmt = nullptr)
                : bb_idx(bb_idx)
                , stmt_idx(stmt_idx)
                , stmt(stmt)
            {
            }
        };

        static Source find_source(const ::MIR::Function& fcn, unsigned bb_idx, unsigned stmt_idx, const ::MIR::LValue& val) {
            if (!val.m_wrappers.empty()) {
                return Source(bb_idx, stmt_idx);
            }
            const auto& bb = fcn.blocks.at(bb_idx);
            while (stmt_idx--) {
                const auto& stmt = bb.statements[stmt_idx];
                if (stmt.is_Asm()) {
                    return Source(bb_idx, stmt_idx);
                }
                if (stmt.is_Assign()) {
                    const auto& se = stmt.as_Assign();
                    if (se.dst == val) {
                        return Source(bb_idx, stmt_idx, &stmt);
                    }
                }
            }
            return Source(bb_idx, 0);
        }

        /// Checks if the passed lvalue would optimise/expand to a constant value
        static bool value_is_const(const ::MIR::Function& fcn, unsigned bb_idx, unsigned stmt_idx, const ::MIR::LValue& val, const std::vector<::MIR::Param>& params) {
            if (val.m_root.is_Argument()) {
                auto a = val.m_root.as_Argument();
                return params[a].is_Constant() && !params[a].as_Constant().is_Const();
            }

            // Find the source of this lvalue, chase it backwards
            auto src = H::find_source(fcn, bb_idx, stmt_idx, val);
            if (src.stmt) {
                if (const auto* se = src.stmt->opt_Assign()) {
                    if (se->src.is_Use()) {
                        return value_is_const(fcn, src.bb_idx, src.stmt_idx, se->src.as_Use(), params);
                    }
                    if (const auto* rve = se->src.opt_BinOp()) {
                        return value_is_const(fcn, src.bb_idx, src.stmt_idx, rve->val_l, params) && value_is_const(fcn, src.bb_idx, src.stmt_idx, rve->val_r, params);
                    }
                }
            }

            return false;
        }

        static bool value_is_const(const ::MIR::Function& fcn, unsigned bb_idx, unsigned stmt_idx, const ::MIR::Param& val, const std::vector<::MIR::Param>& params) {
            if (val.is_LValue()) {
                return value_is_const(fcn, bb_idx, stmt_idx, val.as_LValue(), params);
            } else {
                return val.is_Constant() && !val.as_Constant().is_Const();
            }
        }

        static bool can_inline(const ::HIR::Path& path, const ::MIR::Function& fcn, const std::vector<::MIR::Param>& params, bool minimal) {
            // TODO: If the function is marked as `inline(always)`, then inline it regardless of the contents
            // TODO: If the function is marked as `inline(never)`, then don't inline
            // TODO: Take a monomorph helper so recursion can be detected

            if (minimal) {
                return false;
            }

            // TODO: Allow functions that are just a switch on an input.
            if (fcn.blocks.size() == 1) {
                return fcn.blocks[0].statements.size() < 10 && !fcn.blocks[0].terminator.is_Goto();
            } else if (fcn.blocks.size() == 2 && fcn.blocks[0].terminator.is_Call()) {
                const auto& blk0_te = fcn.blocks[0].terminator.as_Call();
                if (!fcn.blocks[1].terminator.is_Diverge()) {
                    return false;
                }
                if (fcn.blocks[0].statements.size() + fcn.blocks[1].statements.size() > 10) {
                    return false;
                }
                // Detect and avoid simple recursion.
                // - This won't detect mutual recursion - that also needs prevention.
                // TODO: This is the pre-monomorph path, but we're comparing with the post-monomorph path
                if (blk0_te.fcn.is_Path() && blk0_te.fcn.as_Path() == path) {
                    return false;
                }
                return true;
            } else if (fcn.blocks.size() == 3 && fcn.blocks[0].terminator.is_Call()) {
                const auto& blk0_te = fcn.blocks[0].terminator.as_Call();
                if (!(fcn.blocks[1].terminator.is_Diverge() || fcn.blocks[1].terminator.is_Return())) {
                    return false;
                }
                if (!(fcn.blocks[2].terminator.is_Diverge() || fcn.blocks[2].terminator.is_Return())) {
                    return false;
                }
                if (fcn.blocks[0].statements.size() + fcn.blocks[1].statements.size() + fcn.blocks[2].statements.size() > 10) {
                    return false;
                }
                // Detect and avoid simple recursion.
                // - This won't detect mutual recursion - that also needs prevention.
                // TODO: This is the pre-monomorph path, but we're comparing with the post-monomorph path
                if (blk0_te.fcn.is_Path() && blk0_te.fcn.as_Path() == path) {
                    return false;
                }
                return true;
            } else {
            }

            // TODO: If all inputs are known, then allow larger/complex functions (e.g. allow one call and any number of blocks?)
            // - Seen `min_by(const, const, fcn)` - that would be a trivial optimisation

            if (can_inline_Switch_wrapper(path, fcn, params)) {
                return true;
            }
            if (can_inline_SwitchValue_wrapper(path, fcn, params)) {
                return true;
            }
            return false;
        }

        /// Case: A Switch that has all distinct arms that just call a function AND the value is over (effectively) a literal
        static bool can_inline_Switch_wrapper(const ::HIR::Path& path, const ::MIR::Function& fcn, const std::vector<::MIR::Param>& params) {
            if (fcn.blocks.size() <= 1) {
                return false;
            }
            if (!fcn.blocks[0].terminator.is_Switch()) {
                return false;
            }
            const auto& te_switch = fcn.blocks[0].terminator.as_Switch();
            // Setup + Arms + Return + Panic
            // - Handles the atomic wrappers
            if (fcn.blocks.size() != te_switch.targets.size() + 3) {
                return false;
            }
            // Check for the switch value being an argument that is also a constant parameter being a Constant
            if (!value_is_const(fcn, 0, fcn.blocks[0].statements.size(), te_switch.val, params)) {
                return false;
            }
            // Check all arms of the switch are distinct
            for (const auto& tgt : te_switch.targets) {
                if (std::find(te_switch.targets.begin() + (1 + &tgt - te_switch.targets.data()), te_switch.targets.end(), tgt) != te_switch.targets.end()) {
                    return false;
                }
            }
            // Check for recursion
            for (size_t i = 1; i < fcn.blocks.size(); i++) {
                if (fcn.blocks[i].terminator.is_Call()) {
                    const auto& te = fcn.blocks[i].terminator.as_Call();
                    // Recursion, don't inline.
                    if (te.fcn.is_Path() && te.fcn.as_Path() == path) {
                        return false;
                    }
                    // Only intrinsic wrapper calls are proven safe before ordinary call paths are monomorphised.
                    if (!te.fcn.is_Intrinsic()) {
                        return false;
                    }
                }
            }
            return true;
        }

        /// Case: A SwitchValue that has all distinct arms that just call a function AND the value is over (effectively) a literal
        static bool can_inline_SwitchValue_wrapper(const ::HIR::Path& path, const ::MIR::Function& fcn, const std::vector<::MIR::Param>& params) {
            if (fcn.blocks.size() <= 1) {
                return false;
            }
            if (!fcn.blocks[0].terminator.is_SwitchValue()) {
                return false;
            }
            const auto& te_switch = fcn.blocks[0].terminator.as_SwitchValue();
            // Setup + Arms(+default) + Return + Panic
            // - Handles some code in crc32-fast that emits a 256-arm SwitchValue
            if (fcn.blocks.size() != te_switch.targets.size() + 1 + 3) {
                return false;
            }
            // Check for the switch value being an argument that is also a constant parameter being a Constant
            if (!value_is_const(fcn, 0, fcn.blocks[0].statements.size(), te_switch.val, params)) {
                return false;
            }

            // Check all arms of the switch are distinct
            if (std::find(te_switch.targets.begin(), te_switch.targets.end(), te_switch.def_target) != te_switch.targets.end()) {
                return false;
            }
            for (const auto& tgt : te_switch.targets) {
                if (std::find(te_switch.targets.begin() + (1 + &tgt - te_switch.targets.data()), te_switch.targets.end(), tgt) != te_switch.targets.end()) {
                    return false;
                }
            }

            // Check for recursion
            for (size_t i = 1; i < fcn.blocks.size(); i++) {
                if (fcn.blocks[i].terminator.is_Call()) {
                    const auto& te = fcn.blocks[i].terminator.as_Call();
                    // Recursion, don't inline.
                    if (te.fcn.is_Path() && te.fcn.as_Path() == path) {
                        return false;
                    }
                    // Only intrinsic wrapper calls are proven safe before ordinary call paths are monomorphised.
                    if (!te.fcn.is_Intrinsic()) {
                        return false;
                    }
                }
            }
            return true;
        }
    };

    // TODO: Can this use the code in `monomorphise.cpp`?
    struct Cloner: public ::MIR::Cloner {
        const ::StaticTraitResolve& m_resolve;
        const ::MIR::Terminator::Data_Call& te;
        ::std::vector<unsigned> copy_args; // Local indexes containing copies of Copy args
        ParamsSet params;
        unsigned int bb_base = ~0u;
        unsigned int var_base = ~0u;
        unsigned int df_base = ~0u;

        size_t tmp_end = 0;
        mutable ::std::vector<::MIR::Param> const_assignments;

        ::MIR::LValue retval;

        Cloner(const Span& sp, const ::StaticTraitResolve& resolve, ::MIR::Terminator::Data_Call& te)
            : ::MIR::Cloner(sp, resolve.m_crate.m_types)
            , m_resolve(resolve)
            , te(te)
            , params(resolve.m_crate.m_types)
            , copy_args(te.args.size(), ~0u)
        {
        }

        ::MIR::BasicBlockId map_bb_idx(::MIR::BasicBlockId idx) const override {
            return this->bb_base + idx;
        }

        virtual unsigned map_local(unsigned f) const {
            return this->var_base + f;
        }

        virtual unsigned map_drop_flag(unsigned f) const {
            return this->df_base + f;
        }

        const HIR::TypeRef& value_generic_type(HIR::GenericRef ce) const override {
            const HIR::GenericParams* p;
            switch (ce.group()) {
                case 0: // impl level
                    p = params.impl_params_def;
                    break;
                case 1: // method level
                    p = params.fcn_params_def;
                    break;
                default:
                    TODO(sp, "Typecheck const generics - look up the type");
            }
            ASSERT_BUG(sp, p, "No generic list for " << ce);
            ASSERT_BUG(sp, ce.idx() < p->m_values.size(), "Generic param index out of range");
            return p->m_values.at(ce.idx()).m_type;
        }

        const Monomorphiser& monomorphiser() const override {
            return params;
        }

        const StaticTraitResolve* resolve() const override {
            return &this->m_resolve;
        }

        ::MIR::BasicBlock clone_bb(const ::MIR::BasicBlock& src, unsigned src_idx, unsigned new_idx) const {
            ::MIR::BasicBlock rv;
            rv.statements.reserve(src.statements.size());
            for (const auto& stmt : src.statements) {
                DEBUG("BB" << src_idx << "->BB" << new_idx << "/" << rv.statements.size() << ": " << stmt);
                rv.statements.push_back(this->clone_stmt(stmt));
                DEBUG("-> " << rv.statements.back());
            }
            DEBUG("BB" << src_idx << "->BB" << new_idx << "/" << rv.statements.size() << ": " << src.terminator);
            if (src.terminator.is_Return()) {
                rv.statements.push_back(::MIR::Statement::make_Assign({this->te.ret_val.clone(), this->retval.clone()}));
                DEBUG("++ " << rv.statements.back());
            }
            rv.terminator = this->clone_term(src.terminator);
            DEBUG("-> " << rv.terminator);
            return rv;
        }

        ::MIR::Terminator clone_term(const ::MIR::Terminator& src) const override {
            if (src.is_Return()) {
                return ::MIR::Terminator::make_Goto(this->te.ret_block);
            } else if (src.is_Diverge()) {
                return ::MIR::Terminator::make_Goto(this->te.panic_block);
            } else {
                return ::MIR::Cloner::clone_term(src);
            }
        }

        ::MIR::LValue clone_lval(const ::MIR::LValue& src) const override {
            auto rv = ::MIR::Cloner::clone_lval(src);
            if (rv.m_root.is_Return()) {
                return this->retval.clone_wrapped(std::move(rv.m_wrappers));
            }
            if (rv.m_root.is_Argument()) {
                auto se = rv.m_root.as_Argument();
                const auto& arg = this->te.args.at(se);
                if (this->copy_args[se] != ~0u) {
                    return ::MIR::LValue(::MIR::LValue::Storage::new_Local(this->copy_args[se]), std::move(rv.m_wrappers));
                } else {
                    assert(!arg.is_Constant()); // Should have been handled in the above
                    return arg.as_LValue().clone_wrapped(std::move(rv.m_wrappers));
                }
            }
            return rv;
        }
    };

    for (unsigned int i = 0; i < fcn.blocks.size(); i++) {
        state.set_cur_stmt_term(i);
        if (auto* te = fcn.blocks[i].terminator.opt_Call()) {
            if (!te->fcn.is_Path()) {
                continue;
            }
            const auto& path = te->fcn.as_Path();
            DEBUG(state << fcn.blocks[i].terminator);

            for (const auto& e : inlined_functions) {
                if (path == e.path && e.has_bb(i)) {
                    MIR_BUG(state, "Recursive inline of " << path);
                }
            }

            Cloner cloner{state.sp, state.m_resolve, *te};
            const auto* called_mir = get_called_mir(state, list, path, cloner.params);
            if (!called_mir) {
                continue;
            }
            if (called_mir == &fcn) {
                DEBUG("Can't inline - recursion");
                continue;
            }
            if (cloner.params.has_unevaluated_values()) {
                DEBUG("Can't inline - const substitutions are not concrete");
                continue;
            }

            // Check the size of the target function.
            // Inline IF:
            // - First BB ends with a call and total count is 3
            // - Statement count smaller than 10
            if (!H::can_inline(path, *called_mir, te->args, minimal)) {
                DEBUG("Can't inline " << path);
                continue;
            }
            TRACE_FUNCTION_F("Inline " << path);

            // Allocate a temporary for the return value
            {
                cloner.retval = ::MIR::LValue::new_Local(fcn.locals.size());
                DEBUG("- Storing return value in " << cloner.retval);
                ::HIR::TypeRef tmp_ty;
                fcn.locals.push_back(state.get_lvalue_type(tmp_ty, te->ret_val));
                //fcn.local_names.push_back( "" );
            }

            // Monomorph locals and append
            cloner.var_base = fcn.locals.size();
            for (const auto& ty : called_mir->locals) {
                fcn.locals.push_back(cloner.monomorph(ty));
            }
            cloner.tmp_end = fcn.locals.size();

            cloner.df_base = fcn.drop_flags.size();
            fcn.drop_flags.insert(fcn.drop_flags.end(), called_mir->drop_flags.begin(), called_mir->drop_flags.end());
            cloner.bb_base = fcn.blocks.size();

            // Store all Copy lvalue arguments and Constants in variables
            for (size_t i = 0; i < te->args.size(); i++) {
                const auto& a = te->args[i];
                if (!a.is_LValue() || state.lvalue_is_copy(a.as_LValue())) {
                    cloner.copy_args[i] = cloner.tmp_end + cloner.const_assignments.size();
                    cloner.const_assignments.push_back(a.clone());
                    DEBUG("- Taking a copy of arg " << i << " (" << a << ") in Local(" << cloner.copy_args[i] << ")");
                }
            }

            // Append monomorphised copy of all blocks.
            // > Arguments replaced by input lvalues
            ::std::vector<::MIR::BasicBlock> new_blocks;
            new_blocks.reserve(called_mir->blocks.size());
            for (const auto& bb : called_mir->blocks) {
                new_blocks.push_back(cloner.clone_bb(bb, (&bb - called_mir->blocks.data()), fcn.blocks.size() + new_blocks.size()));
            }

            // > Append new temporaries
            DEBUG("- Insert argument lval assignments");
            for (auto& val : cloner.const_assignments) {
                ::HIR::TypeRef tmp;
                auto ty = val.is_Constant() ? state.get_const_type(val.as_Constant()) : state.get_lvalue_type(tmp, val.as_LValue());
                auto lv = ::MIR::LValue::new_Local(static_cast<unsigned>(fcn.locals.size()));
                fcn.locals.push_back(mv$(ty));
                auto rval = val.is_Constant() ? ::MIR::RValue(mv$(val.as_Constant())) : ::MIR::RValue(mv$(val.as_LValue()));
                auto stmt = ::MIR::Statement::make_Assign({mv$(lv), mv$(rval)});
                DEBUG("++ " << stmt);
                new_blocks[0].statements.insert(new_blocks[0].statements.begin(), mv$(stmt));
            }
            cloner.const_assignments.clear();

            // Record the inline event
            for (auto& e : inlined_functions) {
                if (e.has_bb(i)) {
                    e.add_range(cloner.bb_base, new_blocks.size());
                }
            }
            inlined_functions.push_back(InlineEvent(path.clone()));
            inlined_functions.back().add_range(cloner.bb_base, new_blocks.size());

            // Apply
            DEBUG("- Append new blocks");
            fcn.blocks.reserve(fcn.blocks.size() + new_blocks.size());
            for (auto& b : new_blocks) {
                fcn.blocks.push_back(mv$(b));
            }
            fcn.blocks[i].terminator = ::MIR::Terminator::make_Goto(cloner.bb_base);
            inline_happened = true;

            // TODO: Store the inlined path along with the start and end BBs, and then use that to detect recursive
            // inlining
            // - Recursive inlining should be an immediate panic.
        }
    }
    return inline_happened;
}

namespace {
    struct OptimiseStmtRef {
        unsigned bb_idx;
        unsigned stmt_idx;

        OptimiseStmtRef()
            : bb_idx(~0u)
            , stmt_idx(0)
        {
        }

        OptimiseStmtRef(unsigned b, unsigned s)
            : bb_idx(b)
            , stmt_idx(s)
        {
        }

        bool operator==(const OptimiseStmtRef& x) const {
            return bb_idx == x.bb_idx && stmt_idx == x.stmt_idx;
        }
    };

    ::std::ostream& operator<<(::std::ostream& os, const OptimiseStmtRef& x) {
        return os << "BB" << x.bb_idx << "/" << x.stmt_idx;
    }

    // Iterates the path between two positions, NOT visiting entry specified by `end`
    enum class IterPathRes {
        Abort,
        EarlyTrue,
        Complete,
    };

    IterPathRes iter_path(const ::MIR::Function& fcn, const OptimiseStmtRef& start, const OptimiseStmtRef& end, ::std::function<bool(OptimiseStmtRef, const ::MIR::Statement&)> cb_stmt, ::std::function<bool(OptimiseStmtRef, const ::MIR::Terminator&)> cb_term) {
        if (start.bb_idx == end.bb_idx) {
            assert(start.stmt_idx <= end.stmt_idx);
        }

        auto visted_bbs = ::std::set<unsigned>();
        // Loop while not equal (either not in the right block, or before the statement) to the end point
        for (auto ref = start; ref.bb_idx != end.bb_idx || ref.stmt_idx < end.stmt_idx;) {
            const auto& bb = fcn.blocks.at(ref.bb_idx);
            if (ref.stmt_idx < bb.statements.size()) {
                DEBUG(ref << " " << bb.statements.at(ref.stmt_idx));
                if (cb_stmt(ref, bb.statements.at(ref.stmt_idx))) {
                    DEBUG("> Early true");
                    return IterPathRes::EarlyTrue;
                }

                ref.stmt_idx++;
            } else {
                DEBUG(ref << " " << bb.terminator);
                if (cb_term(ref, bb.terminator)) {
                    DEBUG("> Early true");
                    return IterPathRes::EarlyTrue;
                }

                // If this is the end point, break out before checking the terminator for looping
                if (ref.bb_idx == end.bb_idx) {
                    // ^ don't need to check the statment index, this is the last "statement"
                    break;
                }

                // If this terminator is a Goto, follow it (tracking for loops)
                if (const auto* te = bb.terminator.opt_Goto()) {
                    // Possibly loop into the next block
                    if (!visted_bbs.insert(*te).second) {
                        DEBUG("> Loop abort");
                        return IterPathRes::Abort;
                    }
                    ref.stmt_idx = 0;
                    ref.bb_idx = *te;
                }
                // A call's panic edge cannot reach `end`, so only follow the
                // normal return edge while inspecting a path between two
                // positions.
                else if (const auto* te = bb.terminator.opt_Call()) {
                    // Possibly loop into the next block
                    if (!visted_bbs.insert(te->ret_block).second) {
                        DEBUG("> Loop abort");
                        return IterPathRes::Abort;
                    }
                    ref.stmt_idx = 0;
                    ref.bb_idx = te->ret_block;
                } else {
                    DEBUG("> Terminator abort");
                    return IterPathRes::Abort;
                }
            }
        }
        return IterPathRes::Complete;
    }

    ::std::function<bool(const ::MIR::LValue&, ValUsage)> check_invalidates_lvalue_cb(const ::MIR::LValue& val, bool is_copy, bool also_read = false) {
        bool has_index = ::std::any_of(val.m_wrappers.begin(), val.m_wrappers.end(), [](const auto& w) {
            return w.is_Index();
        });
        // Value is invalidated if it's used with ValUsage::Write or ValUsage::Borrow
        // - Same applies to any component of the lvalue
        return [&val, has_index, is_copy, also_read](const ::MIR::LValue& lv, ValUsage vu) {
            switch (vu) {
                    // - Ideally this would check if it DOES invalidate
                case ValUsage::Write:
                case ValUsage::Borrow:
                    // (Possibly) mutating use, check if it impacts the root or one of the indexes
                    if (lv.m_root == val.m_root) {
                        return true;
                    }
                    // If the desired lvalue has an index in it's wrappers, AND the current lvalue is a local
                    if (has_index && lv.m_root.is_Local()) {
                        // Search for any wrapper on `val` that Index(lv)
                        for (const auto& w : val.m_wrappers) {
                            if (w.is_Index() && w.as_Index() == lv.m_root.as_Local()) {
                                // This lvalue is changed, so the index is invalidated
                                return true;
                            }
                        }
                    }
                    break;
                case ValUsage::Move: // A move can invalidate
                    if (is_copy) {
                    } else if (lv.m_root == val.m_root) {
                        // Check if `lv`'s wrappers are a subset of `val`'s
                        auto l = std::min(lv.m_wrappers.size(), val.m_wrappers.size());
                        for (size_t i = 0; i < l; i++) {
                            // A wrapper differs, won't invalidate
                            if (lv.m_wrappers[i] != val.m_wrappers[i]) {
                                return false;
                            }
                        }
                        return true;
                    }
                    break;
                case ValUsage::Read:
                    if (also_read) {
                        // NOTE: A read of the same root is a read of this value (what if they're disjoint fields?)
                        if (lv.m_root == val.m_root) {
                            return true;
                        }
                    }
                    break;
            }
            return false;
        };
    }

    bool check_invalidates_lvalue(const ::MIR::Statement& stmt, const ::MIR::LValue& val, bool is_copy, bool also_read = false) {
        return visit_mir_lvalues(stmt, check_invalidates_lvalue_cb(val, is_copy, also_read));
    }

    bool check_invalidates_lvalue(const ::MIR::Terminator& term, const ::MIR::LValue& val, bool is_copy, bool also_read = false) {
        return visit_mir_lvalues(term, check_invalidates_lvalue_cb(val, is_copy, also_read));
    }
}

// --------------------------------------------------------------------
// Locates locals that are only set/used once, and replaces them with
//  their source IF the source isn't invalidated
// --------------------------------------------------------------------
bool MIR_Optimise_DeTemporary_SingleSetAndUse(::MIR::TypeResolve& state, ::MIR::Function& fcn) {
    bool changed = false;
    TRACE_FUNCTION_FR("", changed);

    // Find all single-use/single-write locals
    // - IF the usage is a RValue::Use, AND the usage destination is not invalidated between set/use
    //  - Replace initialisation destination with usage destination (delete usage statement)
    // - IF the source a Use/Constant, AND is not invalidated between set/use
    //  - Replace usage with the original source
    struct LocalUsage {
        unsigned n_write;
        unsigned n_read;
        unsigned n_borrow;
        OptimiseStmtRef set_loc;
        OptimiseStmtRef use_loc;

        LocalUsage()
            : n_write(0)
            , n_read(0)
            , n_borrow(0)
        {
        }
    };

    auto usage_info = ::std::vector<LocalUsage>(fcn.locals.size());

    // 1. Enumrate usage
    {
        auto get_cur_loc = [&state]() {
            return OptimiseStmtRef(state.get_cur_block(), state.get_cur_stmt_ofs());
        };
        auto visit_cb = [&](const ::MIR::LValue& lv, auto vu) {
            if (!lv.m_wrappers.empty()) {
                vu = ValUsage::Read;
            }
            for (const auto& w : lv.m_wrappers) {
                if (w.is_Index()) {
                    auto& slot = usage_info[w.as_Index()];
                    slot.n_read += 1;
                    slot.use_loc = get_cur_loc();
                    //DEBUG(lv << " index use");
                }
            }
            if (lv.m_root.is_Local()) {
                auto& slot = usage_info[lv.m_root.as_Local()];
                switch (vu) {
                    case ValUsage::Write:
                        slot.n_write += 1;
                        slot.set_loc = get_cur_loc();
                        //DEBUG(lv << " set");
                        break;
                    case ValUsage::Move:
                        slot.n_read += 1;
                        slot.use_loc = get_cur_loc();
                        //DEBUG(lv << " use");
                        break;
                    case ValUsage::Read:
                    case ValUsage::Borrow:
                        slot.n_borrow += 1;
                        //DEBUG(lv << " borrow");
                        break;
                }
            }
            return false;
        };
        visit_mir_lvalues(state, fcn, visit_cb);
    }

    // 2. Find any local with 1 write, 1 read, and no borrows
    for (size_t var_idx = 0; var_idx < fcn.locals.size(); var_idx++) {
        const auto& slot = usage_info[var_idx];
        auto this_var = ::MIR::LValue::new_Local(var_idx);
        //ASSERT_BUG(Span(), slot.n_write > 0, "Variable " << var_idx << " not written?");
        DEBUG("_" << var_idx << ": " << slot.n_write << "," << slot.n_read << "," << slot.n_borrow);
        if (slot.n_write == 1 && slot.n_read == 1 && slot.n_borrow == 0) {
            // Single-use variable, now check how we can eliminate it
            DEBUG("Single-use: _" << var_idx << " - Set " << slot.set_loc << ", Use " << slot.use_loc);

            auto& use_bb = fcn.blocks[slot.use_loc.bb_idx];
            auto& set_bb = fcn.blocks[slot.set_loc.bb_idx];

            auto set_loc_next = slot.set_loc;
            if (slot.set_loc.stmt_idx < set_bb.statements.size()) {
                set_loc_next.stmt_idx += 1;
            } else {
                set_loc_next.bb_idx = set_bb.terminator.as_Call().ret_block;
                set_loc_next.stmt_idx = 0;
            }

            // If usage is direct assignment of the original value.
            // - In this case, we can move the usage upwards
            if (slot.use_loc.stmt_idx < use_bb.statements.size() && TU_TEST2(use_bb.statements[slot.use_loc.stmt_idx], Assign, .src, Use, == this_var)) {
                // Move the usage up to original assignment (if destination isn't invalidated)
                const auto& dst = use_bb.statements[slot.use_loc.stmt_idx].as_Assign().dst;

                // TODO: If the destination slot was ever borrowed mutably, don't move.
                // - Maybe, if there's a drop skip? (as the drop could be &mut to the target value)

                // - Iterate the path(s) between the two statements to check if the destination would be invalidated
                //  > The iterate function doesn't (yet) support following BB chains, so assume invalidated if over a jump.
                // TODO: What if the set location is a call?
                bool invalidated = IterPathRes::Complete != iter_path(
                                                                fcn,
                                                                set_loc_next,
                                                                slot.use_loc,
                                                                // TODO: What about a mutable borrow?
                                                                [&](auto loc, const auto& stmt) -> bool {
                    return stmt.is_Drop() || check_invalidates_lvalue(stmt, dst, false, /*also_read=*/true);
                },
                                                                [&](auto loc, const auto& term) -> bool {
                    return check_invalidates_lvalue(term, dst, false, /*also_read=*/true);
                }
                                                            );
                if (!invalidated) {
                    // destination not dependent on any statements between the two, move.
                    if (slot.set_loc.stmt_idx < set_bb.statements.size()) {
                        auto& set_stmt = set_bb.statements[slot.set_loc.stmt_idx];
                        TU_MATCH_HDRA( (set_stmt), {)
                        TU_ARMA(Assign, se) {
                                MIR_ASSERT(state, se.dst == ::MIR::LValue::new_Local(var_idx), "Impossibility: Value set but isn't destination in " << set_stmt);
                                DEBUG("Move destination " << dst << " from " << use_bb.statements[slot.use_loc.stmt_idx] << " to " << set_stmt);
                                se.dst = dst.clone();
                                use_bb.statements[slot.use_loc.stmt_idx] = ::MIR::Statement();
                                changed = true;
                            }
                            TU_ARMA(Asm, se) {
                                // Initialised from an ASM statement, find the variable in the output parameters
                            }
                            TU_ARMA(Asm2, se) {
                                // Initialised from an ASM statement, find the variable in the output parameters
                                // TODO: Replace the output variable
                                for (auto& e : se.params) {
                                    if (const auto* ep = e.opt_Reg()) {
                                        if (ep->output) {
                                            if (*ep->output == ::MIR::LValue::new_Local(var_idx)) {
                                                DEBUG("Move destination " << dst << " from " << use_bb.statements[slot.use_loc.stmt_idx] << " to " << set_stmt);
                                                *ep->output = dst.clone();
                                                use_bb.statements[slot.use_loc.stmt_idx] = ::MIR::Statement();
                                                changed = true;
                                                break;
                                            }
                                        }
                                    }
                                }
                                if (!changed) {
                                    MIR_BUG(state, "Failed to find usage of _" << var_idx << " in asm! statement");
                                }
                            }
                            break;
                            default:
                                MIR_BUG(state, "Impossibility: Value set in " << set_stmt);
                        }
                    } else {
                        auto& set_term = set_bb.terminator;
                        MIR_ASSERT(state, set_term.is_Call(), "Impossibility: Value set using non-call");
                        auto& te = set_term.as_Call();
                        DEBUG("Move destination " << dst << " from " << use_bb.statements[slot.use_loc.stmt_idx] << " to " << set_term);
                        te.ret_val = dst.clone();
                        use_bb.statements[slot.use_loc.stmt_idx] = ::MIR::Statement();
                        changed = true;
                    }
                } else {
                    DEBUG("Destination invalidated");
                }
                continue;
            }

            // Can't move up, can we move down?
            // - If the source is an Assign(Use) then we can move down
            if (slot.set_loc.stmt_idx < set_bb.statements.size() && TU_TEST1(set_bb.statements[slot.set_loc.stmt_idx], Assign, .src.is_Use())) {
                auto& set_stmt = set_bb.statements[slot.set_loc.stmt_idx];
                const auto& src = set_stmt.as_Assign().src.as_Use();
                bool src_copy = src.m_wrappers.empty() && state.lvalue_is_copy(src);

                // Check if the source of initial assignment is invalidated in the meantime.
                auto use_loc_inc = slot.use_loc;
                use_loc_inc.stmt_idx += 1;
                bool invalidated = IterPathRes::Complete != iter_path(
                                                                fcn,
                                                                set_loc_next,
                                                                use_loc_inc,
                                                                // NOTE: If a mutable borrow happens, assume it invalidates the source
                                                                [&](auto loc, const auto& stmt) -> bool {
                    return check_invalidates_lvalue(stmt, src, src_copy) || TU_TEST2(stmt, Assign, .src, Borrow, .type != HIR::BorrowType::Shared);
                },
                                                                [&](auto loc, const auto& term) -> bool {
                    return check_invalidates_lvalue(term, src, src_copy);
                }
                                                            );
                DEBUG("invalidated = " << invalidated);
                // If this is a deref, and there are move ops between definition and use - then invalidate
                if (!invalidated && std::any_of(src.m_wrappers.begin(), src.m_wrappers.end(), [](const MIR::LValue::Wrapper& w) {
                    return w.is_Deref();
                })) {
                    // If there are any move ops between the set and the usage, invalidate
                    bool stop = false;
                    auto check_cb = [&](const MIR::LValue& lv, ValUsage vu) {
                        if (lv == this_var) {
                            stop = true;
                            return false;
                        }
                        if (stop) {
                            // Once the value is seen, ignore anything else
                            return false;
                        }
                        // If a move is seen, check if it's a move (and not a copy)
                        if (vu == ValUsage::Move) {
                            return !state.lvalue_is_copy(lv);
                        }
                        return false;
                    };
                    invalidated = IterPathRes::Complete != iter_path(fcn, set_loc_next, use_loc_inc, [&](auto loc, const auto& stmt) -> bool {
                        return visit_mir_lvalues(stmt, check_cb);
                    }, [&](auto loc, const auto& term) -> bool {
                        return (term.is_Call() && !visit_mir_lvalues(term, [&](const MIR::LValue& lv, ValUsage vu) {
                            return lv == this_var;
                        })) || visit_mir_lvalues(term, check_cb);
                    });
                    DEBUG("invalidated = " << invalidated);
                }
                if (!invalidated) {
                    // Update the usage site and replace.
                    auto replace_cb = [&](::MIR::LValue& slot, ValUsage vu) -> bool {
                        if (slot.m_root == this_var.m_root) {
                            if (src.m_wrappers.empty()) {
                                slot.m_root = src.m_root.clone();
                            } else if (slot.m_wrappers.empty()) {
                                slot = src.clone();
                            } else {
                                MIR_TODO(state, "Replace inner of " << slot << " with " << src);
                            }
                            return true;
                        }
                        return false;
                    };
                    if (slot.use_loc.stmt_idx < use_bb.statements.size()) {
                        auto& use_stmt = use_bb.statements[slot.use_loc.stmt_idx];
                        DEBUG("Replace " << this_var << " with " << src << " in BB" << slot.use_loc.bb_idx << "/" << slot.use_loc.stmt_idx << " " << use_stmt);
                        bool found = visit_mir_lvalues_mut(use_stmt, replace_cb);
                        if (!found) {
                            DEBUG("Can't find use of " << this_var << " in " << use_stmt);
                        } else {
                            set_stmt = ::MIR::Statement();
                            changed = true;
                        }
                    } else {
                        auto& use_term = use_bb.terminator;
                        DEBUG("Replace " << this_var << " with " << src << " in " << use_term);
                        bool found = visit_mir_lvalues_mut(use_term, replace_cb);
                        if (!found) {
                            DEBUG("Can't find use of " << this_var << " in " << use_term);
                        } else {
                            set_stmt = ::MIR::Statement();
                            changed = true;
                        }
                    }
                } else {
                    DEBUG("Source invalidated");
                }
                continue;
            }

            // TODO: If the source is a Borrow and the use is a Deref, then propagate forwards
            // - This would be a simpler version of a var more compliciated algorithm

            DEBUG("Can't replace:");
            if (slot.set_loc.stmt_idx < set_bb.statements.size()) {
                DEBUG("Set: " << set_bb.statements[slot.set_loc.stmt_idx]);
            } else {
                DEBUG("Set: " << set_bb.terminator);
            }
            if (slot.use_loc.stmt_idx < use_bb.statements.size()) {
                DEBUG("Use: " << use_bb.statements[slot.use_loc.stmt_idx]);
            } else {
                DEBUG("Use: " << use_bb.terminator);
            }
        }
    }

    return changed;
}

// Remove useless borrows (locals assigned with a borrow, and never used by value)
// ```
// _$1 = & _$0;
// (*_$1).1 = 0x0;
// ```
bool MIR_Optimise_DeTemporary_Borrows(::MIR::TypeResolve& state, ::MIR::Function& fcn) {
    bool changed = false;
    TRACE_FUNCTION_FR("", changed);

    // Find all single-assign borrows that are only ever used via Deref
    // - Direct drop is ignored for this purpose
    struct LocalUsage {
        unsigned n_write;
        unsigned n_other_read;
        unsigned n_deref_read;
        OptimiseStmtRef set_loc;
        ::std::vector<OptimiseStmtRef> drop_locs;

        LocalUsage()
            : n_write(0)
            , n_other_read(0)
            , n_deref_read(0)
        {
        }
    };

    auto usage_info = ::std::vector<LocalUsage>(fcn.locals.size());
    for (const auto& bb : fcn.blocks) {
        OptimiseStmtRef cur_loc;
        auto visit_cb = [&](const ::MIR::LValue& lv, auto vu) {
            if (lv.m_root.is_Local()) {
                auto& slot = usage_info[lv.m_root.as_Local()];
                // NOTE: This pass doesn't care about indexing, as we're looking for values that are borrows (which aren't valid indexes)
                // > Inner-most wrapper is Deref - it's a deref of this variable
                if (!lv.m_wrappers.empty() && lv.m_wrappers.front().is_Deref()) {
                    slot.n_deref_read++;
                    if (fcn.locals[lv.m_root.as_Local()]->is_Borrow()) {
                        DEBUG(lv << " deref use " << cur_loc);
                    }
                }
                // > Write with no wrappers - Assignment
                else if (lv.m_wrappers.empty() && vu == ValUsage::Write) {
                    slot.n_write++;
                    slot.set_loc = cur_loc;
                    //DEBUG(lv << " set");
                }
                // Anything else, count as a read
                else {
                    slot.n_other_read++;
                }
            }
            return false;
        };
        for (const auto& stmt : bb.statements) {
            cur_loc = OptimiseStmtRef(&bb - &fcn.blocks.front(), &stmt - &bb.statements.front());

            // If the statement is a drop of a local, then don't count that as a read
            // - But do record the location of the drop, so it can be deleted later on?
            if (stmt.is_Drop()) {
                const auto& drop_lv = stmt.as_Drop().slot;
                if (drop_lv.m_root.is_Local() && drop_lv.m_wrappers.empty()) {
                    auto& slot = usage_info[drop_lv.m_root.as_Local()];
                    slot.drop_locs.push_back(cur_loc);
                    continue;
                }
            }

            //DEBUG(cur_loc << ":" << stmt);
            visit_mir_lvalues(stmt, visit_cb);
        }
        cur_loc = OptimiseStmtRef(&bb - &fcn.blocks.front(), bb.statements.size());
        //DEBUG(cur_loc << ":" << bb.terminator);
        visit_mir_lvalues(bb.terminator, visit_cb);
    }

    // Look single-write/deref-only locals assigned with `_0 = Borrow`
    for (size_t var_idx = 0; var_idx < fcn.locals.size(); var_idx++) {
        const auto& slot = usage_info[var_idx];
        auto this_var = ::MIR::LValue::new_Local(var_idx);

        // This rule only applies to single-write variables, with no use other than via derefs
        if (slot.n_write != 1) {
            //DEBUG(this_var << " - Multi-assign, or use-by-value");
            continue;
        }
        if (slot.n_deref_read == 0) {
            //DEBUG(this_var << " - Not used");
            continue;
        }

        // Check that the source was a borrow statement
        auto& src_bb = fcn.blocks[slot.set_loc.bb_idx];
        if (!(slot.set_loc.stmt_idx < src_bb.statements.size() && TU_TEST1(src_bb.statements[slot.set_loc.stmt_idx], Assign, .src.is_Borrow()))) {
            DEBUG(this_var << " - Source is not a borrow op");
            continue;
        }
        const auto& src_borrow = src_bb.statements[slot.set_loc.stmt_idx].as_Assign().src.as_Borrow();
        const auto& src_lv = src_borrow.val;
        // Check that the borrow isn't too complex (if it's used multiple times)
        if (slot.n_deref_read > 1 && src_lv.m_wrappers.size() >= 2) {
            DEBUG(this_var << " - Source is too complex - " << src_lv);
            continue;
        }
        // If there are multiple derefs, don't expand. More than one deref makes determining invalidation VERY hard
        if (std::count_if(src_lv.m_wrappers.begin(), src_lv.m_wrappers.end(), [](const MIR::LValue::Wrapper& w) {
            return w.is_Deref();
        }) > 1) {
            DEBUG(this_var << " - Source is too complex (deref) - " << src_lv);
            continue;
        }
        // Keep the complexity down (when not used only once)
        if (slot.n_deref_read + slot.n_other_read > 1 && src_borrow.type != ::HIR::BorrowType::Shared) {
            DEBUG(this_var << " - Multi-use non-shared borrow, too complex to do");
            continue;
        }
        DEBUG(this_var << " - Borrow of " << src_lv << " at " << slot.set_loc << ", used " << slot.n_deref_read << " times (dropped {" << slot.drop_locs << "})");
        bool src_copy = state.lvalue_is_copy(src_lv);

        // Locate usage sites (by walking forwards) and check for invalidation
        auto cur_loc = slot.set_loc;
        cur_loc.stmt_idx++;
        unsigned num_replaced = 0;
        auto replace_cb = [&](::MIR::LValue& lv, auto _vu) {
            if (lv.m_root == this_var.m_root && !lv.m_wrappers.empty()) {
                ASSERT_BUG(Span(), !lv.m_wrappers.empty(), cur_loc << " " << lv);
                MIR_ASSERT(state, lv.m_wrappers.front().is_Deref(), "Use of a replacable value that isn't via a deref - " << lv);
                // Make a LValue reference, then overwrite it
                {
                    auto lvr = ::MIR::LValue::MRef(lv);
                    while (lvr.wrapper_count() > 1) {
                        lvr.try_unwrap();
                    }
                    DEBUG(this_var << " " << cur_loc << " - Replace " << lvr << " with " << src_lv << " in " << lv);
                    lvr.replace(src_lv.clone());
                }
                DEBUG("= " << lv);
                assert(lv.m_root != this_var.m_root);
                assert(num_replaced < slot.n_deref_read);
                num_replaced += 1;
            }
            return false;
        };
        for (bool stop = false; !stop;) {
            auto& cur_bb = fcn.blocks[cur_loc.bb_idx];
            for (; cur_loc.stmt_idx < cur_bb.statements.size(); cur_loc.stmt_idx++) {
                auto& stmt = cur_bb.statements[cur_loc.stmt_idx];
                DEBUG(cur_loc << " " << stmt);
                // Check for invalidation (actual check done before replacement)
                bool invalidates = check_invalidates_lvalue(stmt, src_lv, src_copy);
                if (invalidates) {
                    // Invalidated, stop here.
                    DEBUG(this_var << " - Source invalidated @ " << cur_loc << " in " << stmt);
                    stop = true;
                    break;
                }
                // Replace usage
                visit_mir_lvalues_mut(stmt, replace_cb);
                if (num_replaced == slot.n_deref_read) {
                    stop = true;
                    break;
                }
            }
            if (stop) {
                break;
            }
            // Replace usage
            visit_mir_lvalues_mut(cur_bb.terminator, replace_cb);
            if (num_replaced == slot.n_deref_read) {
                stop = true;
                break;
            }
            // Check for invalidation
            if (check_invalidates_lvalue(cur_bb.terminator, src_lv, src_copy)) {
                DEBUG(this_var << " - Source invalidated @ " << cur_loc << " in " << cur_bb.terminator);
                stop = true;
                break;
            }

            TU_MATCH_HDRA( (cur_bb.terminator), { )
            default:
                stop = true;
                break;
                // TODO: History is needed to avoid infinite loops from triggering infinite looping here.
                //TU_ARMA(Goto, e) {
                //    cur_pos.bb_idx = e;
                //    cur_pos.stmt_idx = 0;
                //    }
                // TODO: Fork state to handle multi-tagets
                // NOTE: `Call` can't work in the presense of unwinding, would need to traverse both paths
                //TU_ARMA(Call, e) {
                //    }
            }
        }

        // If the source was an inner deref, update its counts
        if (src_lv.m_root.is_Local() && !src_lv.m_wrappers.empty() && src_lv.m_wrappers.front().is_Deref()) {
            usage_info[src_lv.m_root.as_Local()].n_deref_read += num_replaced;
            if (num_replaced == slot.n_deref_read) {
                usage_info[src_lv.m_root.as_Local()].n_deref_read -= 1;
            }
        }

        // If all usage sites were updated, then remove the original assignment
        // - Since this code works with `&mut`, can't just leave the assignment for DCE when mut
        if (num_replaced == slot.n_deref_read + slot.n_other_read) {
            DEBUG(this_var << " - Erase " << slot.set_loc << " as it is no longer used (" << src_bb.statements[slot.set_loc.stmt_idx] << ")");
            src_bb.statements[slot.set_loc.stmt_idx] = ::MIR::Statement();
            for (const auto& drop_loc : slot.drop_locs) {
                DEBUG(this_var << " - Drop at " << drop_loc);
                fcn.blocks[drop_loc.bb_idx].statements[drop_loc.stmt_idx] = ::MIR::Statement();
            }
        } else {
            // The variable is still used, keep the source where it is
            DEBUG(this_var << " - Keep " << slot.set_loc);
        }

        // Any replacements? Then there was an actionable change
        if (num_replaced > 0) {
            changed = true;
            // Return as soon as a variable has been changed, as this can invalidate the slot information
            return changed;
        }
    }

    return changed;
}

// --------------------------------------------------------------------
// Replaces reborrows where the source is never used again (except maybe
// being dropped)
//
// _1 = & _0*;
// ...
// drop(_0);
// --------------------------------------------------------------------
bool MIR_Optimise_DeTemporary_ReborrowOfUnused(::MIR::TypeResolve& state, ::MIR::Function& fcn) {
    bool changed = false;
    TRACE_FUNCTION_FR("", changed);

    struct Poss {
        OptimiseStmtRef pos;
        MIR::LValue::Storage slot;
        MIR::LValue::Storage replace;
        bool used;

        Poss(OptimiseStmtRef pos, ::MIR::LValue::Storage slot, ::MIR::LValue::Storage replace)
            : pos(pos)
            , slot(mv$(slot))
            , replace(mv$(replace))
            , used(false)
        {
        }
    };

    ::std::vector<Poss> possible;
    // Locate reborrows with the same source/destination type
    // Source lvalue must be a local/argument
    for (const auto& blk : fcn.blocks) {
        for (const auto& stmt : blk.statements) {
            state.set_cur_stmt(&blk - fcn.blocks.data(), &stmt - blk.statements.data());

            if (!stmt.is_Assign()) {
                continue;
            }
            const auto& se = stmt.as_Assign();
            // Must be assigning to a local
            if (!se.dst.is_Local()) {
                continue;
            }
            // Soure must be a borrow
            if (!se.src.is_Borrow()) {
                continue;
            }
            const auto& re = se.src.as_Borrow();
            // Source must be `<local>*` or `<arg>*`
            if (!(re.val.m_root.is_Local() || re.val.m_root.is_Argument())) {
                continue;
            }
            if (!(re.val.m_wrappers.size() == 1 && re.val.m_wrappers[0].is_Deref())) {
                continue;
            }
            // Types must match (avoids decaying reborrows or raw pointer accesses)
            const auto& src_ty = re.val.m_root.is_Local() ? fcn.locals[re.val.m_root.as_Local()] : state.m_args[re.val.m_root.as_Argument()].second;
            const auto& dst_ty = fcn.locals[se.dst.as_Local()];
            if (src_ty != dst_ty) {
                continue;
            }

            // Record as a possible useless reborrow
            // - Depends on the usage of the source
            auto pos = OptimiseStmtRef(state.get_cur_block(), state.get_cur_stmt_ofs());
            DEBUG(state << "Possible " << se.dst << " = " << re.val);
            possible.push_back(Poss(pos, re.val.m_root.clone(), se.dst.m_root.clone()));
        }
    }
    if (possible.size() == 0) {
        return false;
    }
    // The borrow must not be within a loop
    {
        std::vector<unsigned int> incoming_edges(fcn.blocks.size());
        for (const auto& block : fcn.blocks) {
            visit_terminator_target(block.terminator, [&](const auto& target) {
                incoming_edges[target]++;
            });
        }
        std::vector<unsigned int> acyclic_blocks;
        acyclic_blocks.reserve(fcn.blocks.size());
        for (unsigned int i = 0; i < incoming_edges.size(); i++) {
            if (incoming_edges[i] == 0) {
                acyclic_blocks.push_back(i);
            }
        }
        for (size_t i = 0; i < acyclic_blocks.size(); i++) {
            visit_terminator_target(fcn.blocks[acyclic_blocks[i]].terminator, [&](const auto& target) {
                if (--incoming_edges[target] == 0) {
                    acyclic_blocks.push_back(target);
                }
            });
        }

        struct VisitState {
            const ::MIR::Function& fcn;
            std::vector<bool> visited;
            std::vector<unsigned> stack;

            VisitState(const ::MIR::Function& fcn)
                : fcn(fcn)
            {
            }

            bool does_block_loop(unsigned root_idx) {
                stack.clear();
                visited.clear();
                visited.resize(fcn.blocks.size());
                visited[root_idx] = true;
                stack.push_back(root_idx);
                while (!stack.empty()) {
                    auto bb_idx = stack.back();
                    stack.pop_back();
                    auto& bb = fcn.blocks[bb_idx];
                    bool is_loop = false;
                    visit_terminator_target(bb.terminator, [&](const ::MIR::BasicBlockId& idx) {
                        if (idx == root_idx) {
                            is_loop = true;
                        }
                        if (!visited[idx]) {
                            visited[idx] = true;
                            stack.push_back(idx);
                        }
                    });
                    if (is_loop) {
                        return true;
                    }
                }
                return false;
            }
        } vs{fcn};

        if (acyclic_blocks.size() != fcn.blocks.size()) {
            std::vector<bool> visited(fcn.blocks.size());
            std::vector<bool> loops(fcn.blocks.size());
            for (auto& poss : possible) {
                if (!visited[poss.pos.bb_idx]) {
                    visited[poss.pos.bb_idx] = true;
                    loops[poss.pos.bb_idx] = vs.does_block_loop(poss.pos.bb_idx);
                }
                poss.used |= loops[poss.pos.bb_idx];
            }
        }
    }

    // Must be the only use (apart from dropping) of the source lvalue
    ::std::unordered_map<uintptr_t, ::std::vector<size_t>> possible_by_source;
    for (size_t i = 0; i < possible.size(); i++) {
        possible_by_source[possible[i].slot.get_inner()].push_back(i);
    }
    for (const auto& blk : fcn.blocks) {
        for (const auto& stmt : blk.statements) {
            state.set_cur_stmt(&blk - fcn.blocks.data(), &stmt - blk.statements.data());
            auto pos = OptimiseStmtRef(state.get_cur_block(), state.get_cur_stmt_ofs());
            const ::MIR::LValue* dropped = stmt.is_Drop() ? &stmt.as_Drop().slot : nullptr;
            visit_mir_lvalues(stmt, [&](const ::MIR::LValue& lv, ValUsage /*vu*/) {
                if (!(lv.m_root.is_Local() || lv.m_root.is_Argument())) {
                    return false;
                }
                auto it = possible_by_source.find(lv.m_root.get_inner());
                if (it == possible_by_source.end()) {
                    return false;
                }
                if (dropped && dropped->m_wrappers.empty() && dropped->m_root.get_inner() == lv.m_root.get_inner()) {
                    DEBUG(state << lv.m_root << " Droped - " << stmt);
                    return false;
                }
                for (auto possible_idx : it->second) {
                    auto& p = possible[possible_idx];
                    if (!(pos == p.pos)) {
                        DEBUG(state << p.slot << " Used - " << stmt);
                        p.used = true;
                    }
                }
                return false;
            });
        }
        visit_mir_lvalues(blk.terminator, [&](const ::MIR::LValue& lv, ValUsage /*vu*/) {
            if (!(lv.m_root.is_Local() || lv.m_root.is_Argument())) {
                return false;
            }
            auto it = possible_by_source.find(lv.m_root.get_inner());
            if (it != possible_by_source.end()) {
                for (auto possible_idx : it->second) {
                    auto& p = possible[possible_idx];
                    DEBUG(state << p.slot << " Used - " << blk.terminator);
                    p.used = true;
                }
            }
            return false;
        });
    }

    // Remove any marked with `used=true` from the list
    {
        auto ne = std::remove_if(possible.begin(), possible.end(), [&](const Poss& p) {
            return p.used;
        });
        possible.erase(ne, possible.end());
    }
    if (possible.size() == 0) {
        return false;
    }
    // Rewrite and erase
    ::std::unordered_set<uintptr_t> source_slots;
    ::std::unordered_map<uintptr_t, uintptr_t> replacements;
    for (auto it = possible.rbegin(); it != possible.rend(); ++it) {
        const auto source = it->slot.get_inner();
        const auto destination = it->replace.get_inner();
        source_slots.insert(source);
        auto next = replacements.find(source);
        replacements[destination] = next == replacements.end() ? source : next->second;
        fcn.blocks[it->pos.bb_idx].statements[it->pos.stmt_idx] = ::MIR::Statement();
    }
    for (auto& blk : fcn.blocks) {
        for (auto& stmt : blk.statements) {
            state.set_cur_stmt(&blk - fcn.blocks.data(), &stmt - blk.statements.data());
            if (stmt.is_Drop() && stmt.as_Drop().slot.m_wrappers.empty() && (stmt.as_Drop().slot.m_root.is_Local() || stmt.as_Drop().slot.m_root.is_Argument()) && source_slots.count(stmt.as_Drop().slot.m_root.get_inner()) != 0) {
                DEBUG(state << stmt.as_Drop().slot.m_root << " Erase drop");
                stmt = ::MIR::Statement();
                continue;
            }
            visit_mir_lvalues_mut(stmt, [&](::MIR::LValue& lv, ValUsage /*vu*/) {
                if (lv.m_root.is_Local()) {
                    auto it = replacements.find(lv.m_root.get_inner());
                    if (it != replacements.end()) {
                        DEBUG(state << lv.m_root << " Replace");
                        lv.m_root = ::MIR::LValue::Storage::from_inner(it->second);
                    }
                }
                return false;
            });
        }

        visit_mir_lvalues_mut(blk.terminator, [&](::MIR::LValue& lv, ValUsage /*vu*/) {
            if (lv.m_root.is_Local()) {
                auto it = replacements.find(lv.m_root.get_inner());
                if (it != replacements.end()) {
                    DEBUG(state << lv.m_root << " Replace");
                    lv.m_root = ::MIR::LValue::Storage::from_inner(it->second);
                }
            }
            return false;
        });
    }
    changed = true;
    return changed;
}

// --------------------------------------------------------------------
// Replaces uses of stack slots with what they were assigned with (when
// possible)
// --------------------------------------------------------------------
bool MIR_Optimise_DeTemporary(::MIR::TypeResolve& state, ::MIR::Function& fcn) {
    bool changed = false;
    TRACE_FUNCTION_FR("", changed);

    changed |= MIR_Optimise_DeTemporary_SingleSetAndUse(state, fcn);
    if (changed) {
        return changed;
    }
    changed |= MIR_Optimise_DeTemporary_Borrows(state, fcn);
    if (changed) {
        return changed;
    }
    changed |= MIR_Optimise_DeTemporary_ReborrowOfUnused(state, fcn);

    // OLD ALGORITHM.
    for (unsigned int bb_idx = 0; bb_idx < fcn.blocks.size(); bb_idx++) {
        auto& bb = fcn.blocks[bb_idx];
        ::std::map<unsigned, unsigned> local_assignments; // Local number -> statement index
        // TODO: Keep track of what variables would invalidate a local (and compound on assignment)
        ::std::vector<unsigned> statements_to_remove; // List of statements that have to be removed

        // ----- Helper closures -----
        // > Check if a recorded assignment is no longer valid.
        auto cb_check_invalidate = [&](const ::MIR::LValue& lv, ValUsage vu) {
            for (auto it = local_assignments.begin(); it != local_assignments.end();) {
                bool invalidated = false;
                const auto& src_rvalue = bb.statements[it->second].as_Assign().src;

                // Destination invalidated?
                if (lv.m_root.is_Local() && it->first == lv.m_root.as_Local()) {
                    switch (vu) {
                        case ValUsage::Borrow:
                        case ValUsage::Write:
                            DEBUG(state << "> Mutate/Borrowed " << lv);
                            invalidated = true;
                            break;
                        default:
                            break;
                    }
                }
                // Source invalidated?
                else {
                    switch (vu) {
                        case ValUsage::Borrow: // Borrows are annoying, assume they invalidate anything used
                        case ValUsage::Write:  // Mutated? It's invalidated
                        case ValUsage::Move:   // Moved? Now invalid
                            visit_mir_lvalues(src_rvalue, [&](const ::MIR::LValue& s_lv, auto s_vu) {
                                //DEBUG("   " << s_lv << " ?= " << lv);
                                if (s_lv.m_root == lv.m_root) {
                                    DEBUG(state << "> Invalidates source of Local(" << it->first << ") - " << src_rvalue);
                                    invalidated = true;
                                    return true;
                                }
                                return false;
                            });
                            break;
                        case ValUsage::Read: // Read is Ok
                            break;
                    }
                }

                if (invalidated) {
                    it = local_assignments.erase(it);
                } else {
                    ++it;
                }
            }
            return false;
        };
        // ^^^ Check for invalidations
        auto cb_apply_replacements = [&](auto& top_lv, auto top_usage) {
            // NOTE: Visits only the top-level LValues
            // - The inner `visit_mir_lvalue_mut` handles sub-values

            // TODO: Handle partial moves (only delete assignment if the value is fully used)
            // > For now, don't do the replacement if it would delete the assignment UNLESS it's directly being used)

            // 2. Search for replacements
            if (top_lv.m_root.is_Local()) {
                bool top_level = top_lv.m_wrappers.empty();
                auto ilv = ::MIR::LValue::new_Local(top_lv.m_root.as_Local());
                auto it = local_assignments.find(top_lv.m_root.as_Local());
                if (it != local_assignments.end()) {
                    const auto& new_val = bb.statements[it->second].as_Assign().src.as_Use();
                    // - Copy? All is good.
                    if (state.lvalue_is_copy(ilv)) {
                        top_lv = new_val.clone_wrapped(top_lv.m_wrappers.begin(), top_lv.m_wrappers.end());
                        DEBUG(state << "> Replace (and keep) Local(" << it->first << ") with " << new_val);
                        changed = true;
                    }
                    // - Top-level (directly used) also good.
                    else if (top_level && top_usage == ValUsage::Move) {
                        // TODO: DstMeta/DstPtr _doesn't_ move, so shouldn't trigger this.
                        top_lv = new_val.clone();
                        DEBUG(state << "> Replace (and remove) Local(" << it->first << ") with " << new_val);
                        statements_to_remove.push_back(it->second);
                        local_assignments.erase(it);
                        changed = true;
                    }
                    // - Otherwise, remove the record.
                    else {
                        DEBUG(state << "> Non-copy value used within a LValue, remove record of Local(" << it->first << ")");
                        local_assignments.erase(it);
                    }
                }
            }
            // Return true to prevent recursion
            return true;
        };

        // ----- Top-level algorithm ------
        // - Find expressions matching the pattern `Local(N) = Use(...)`
        //  > Delete entry when destination is mutated
        //  > Delete entry when source is mutated or invalidated (moved)
        for (unsigned int stmt_idx = 0; stmt_idx < bb.statements.size(); stmt_idx++) {
            auto& stmt = bb.statements[stmt_idx];
            state.set_cur_stmt(bb_idx, stmt_idx);
            DEBUG(state << stmt);

            // - Check if this statement mutates or borrows a recorded local
            //  > (meaning that the slot isn't a temporary)
            // - Check if this statement mutates or moves the source
            //  > (thus making it invalid to move the source forwards)
            visit_mir_lvalues(stmt, cb_check_invalidate);

            // - Apply known relacements
            visit_mir_lvalues_mut(stmt, cb_apply_replacements);

            // - Check if this is a new assignment
            if (stmt.is_Assign() && stmt.as_Assign().dst.is_Local() && stmt.as_Assign().src.is_Use()) {
                const auto& dst_lv = stmt.as_Assign().dst;
                const auto& src_lv = stmt.as_Assign().src.as_Use();
                if (visit_mir_lvalues_inner(src_lv, ValUsage::Read, [&](const auto& lv, auto) {
                    return lv.m_root == dst_lv.m_root;
                })) {
                    DEBUG(state << "> Don't record, self-referrential");
                } else if (::std::any_of(src_lv.m_wrappers.begin(), src_lv.m_wrappers.end(), [](const auto& w) {
                    return w.is_Deref();
                })) {
                    DEBUG(state << "> Don't record, dereference");
                } else {
                    local_assignments.insert(::std::make_pair(stmt.as_Assign().dst.as_Local(), stmt_idx));
                    DEBUG(state << "> Record assignment");
                }
            }
        } // for(stmt in bb.statements)

        // TERMINATOR
        state.set_cur_stmt_term(bb_idx);
        DEBUG(state << bb.terminator);
        // > Check for invalidations (e.g. move of a source value)
        visit_mir_lvalues(bb.terminator, cb_check_invalidate);
        // > THEN check for replacements
        if (!bb.terminator.is_Switch()) {
            visit_mir_lvalues_mut(bb.terminator, cb_apply_replacements);
        }

        // Remove assignments
        ::std::sort(statements_to_remove.begin(), statements_to_remove.end());
        while (!statements_to_remove.empty()) {
            // TODO: Handle partial moves here?
            // TODO: Is there some edge case I'm missing where the assignment shouldn't be removed?
            // > It isn't removed if it's used as a Copy, so that's not a problem.
            bb.statements.erase(bb.statements.begin() + statements_to_remove.back());
            statements_to_remove.pop_back();

            changed = true;
        }
    }

    return changed;
}

// --------------------------------------------------------------------
// Detect common statements between all source arms of a block
// --------------------------------------------------------------------
bool MIR_Optimise_CommonStatements(::MIR::TypeResolve& state, ::MIR::Function& fcn) {
    bool changed = false;
    TRACE_FUNCTION_FR("", changed);

    for (size_t bb_idx = 0; bb_idx < fcn.blocks.size(); bb_idx++) {
        state.set_cur_stmt(bb_idx, 0);

        bool skip = false;
        ::std::vector<size_t> sources;
        // Find source blocks
        for (size_t bb2_idx = 0; bb2_idx < fcn.blocks.size() && !skip; bb2_idx++) {
            const auto& blk = fcn.blocks[bb2_idx];
            // TODO: Handle non-Goto branches? (e.g. calls)
            if (blk.terminator.is_Goto() && blk.terminator.as_Goto() == bb_idx) {
                if (blk.statements.empty()) {
                    DEBUG(state << " BB" << bb2_idx << " empty");
                    skip = true;
                    break;
                }
                if (!sources.empty()) {
                    if (blk.statements.back() != fcn.blocks[sources.front()].statements.back()) {
                        DEBUG(state << " BB" << bb2_idx << " doesn't end with " << fcn.blocks[sources.front()].statements.back() << " instead " << blk.statements.back());
                        skip = true;
                        break;
                    }
                }
                sources.push_back(bb2_idx);
            } else {
                visit_terminator_target(blk.terminator, [&](const auto& dst_idx) {
                    // If this terminator points to the current BB, don't attempt to merge
                    if (dst_idx == bb_idx) {
                        DEBUG(state << " BB" << bb2_idx << " doesn't end Goto - instead " << blk.terminator);
                        skip = true;
                    }
                });
            }
        }

        if (!skip && sources.size() > 1) {
            // TODO: Should this search for any common statements?

            // Found a common assignment, add to the start and remove from sources.
            auto stmt = ::std::move(fcn.blocks[sources.front()].statements.back());
            MIR_DEBUG(state, "Move common final statements from " << sources << " to " << bb_idx << " - " << stmt);
            for (auto idx : sources) {
                fcn.blocks[idx].statements.pop_back();
            }
            fcn.blocks[bb_idx].statements.insert(fcn.blocks[bb_idx].statements.begin(), ::std::move(stmt));
        }
    }
    return changed;
}

// --------------------------------------------------------------------
// If two temporaries don't overlap in lifetime (blocks in which they're valid), unify the two
// --------------------------------------------------------------------
bool MIR_Optimise_UnifyTemporaries(::MIR::TypeResolve& state, ::MIR::Function& fcn) {
    bool replacement_needed = false;
    TRACE_FUNCTION_FR("", replacement_needed);
    ::std::vector<bool> replacable(fcn.locals.size());
    // 1. Enumerate which (if any) temporaries share the same type
    {
        unsigned int n_found = 0;
        for (unsigned int tmpidx = 0; tmpidx < fcn.locals.size(); tmpidx++) {
            if (replacable[tmpidx]) {
                continue;
            }
            for (unsigned int i = tmpidx + 1; i < fcn.locals.size(); i++) {
                if (replacable[i]) {
                    continue;
                }
                if (fcn.locals[i] == fcn.locals[tmpidx]) {
                    replacable[i] = true;
                    replacable[tmpidx] = true;
                    n_found++;
                }
            }
        }
        if (n_found == 0) {
            return false;
        }
    }

    // TODO: Only calculate lifetimes for replacable locals
    auto lifetimes = MIR_Helper_GetLifetimes(state, fcn, /*dump_debug=*/true, /*mask=*/&replacable);
    ::std::vector<::MIR::ValueLifetime> slot_lifetimes = mv$(lifetimes.m_slots);

    // 2. Unify variables of the same type with distinct non-overlapping lifetimes
    ::std::map<unsigned int, unsigned int> replacements;
    ::std::vector<bool> visited(fcn.locals.size());
    for (unsigned int local_idx = 0; local_idx < fcn.locals.size(); local_idx++) {
        if (!replacable[local_idx]) {
            continue;
        }
        if (visited[local_idx]) {
            continue;
        }
        if (!slot_lifetimes[local_idx].is_used()) {
            continue;
        }
        visited[local_idx] = true;

        for (unsigned int i = local_idx + 1; i < fcn.locals.size(); i++) {
            if (!replacable[i]) {
                continue;
            }
            if (fcn.locals[i] != fcn.locals[local_idx]) {
                continue;
            }
            if (!slot_lifetimes[i].is_used()) {
                continue;
            }
            // Variables are of the same type, check if they overlap
            if (slot_lifetimes[local_idx].overlaps(slot_lifetimes[i])) {
                continue;
            }
            // They don't overlap, unify
            slot_lifetimes[local_idx].unify(slot_lifetimes[i]);
            replacements[i] = local_idx;
            replacement_needed = true;
            visited[i] = true;
        }
    }

    if (replacement_needed) {
        DEBUG("Replacing temporaries using {" << replacements << "}");
        visit_mir_lvalues_mut(state, fcn, [&](auto& lv, auto) {
            if (lv.m_root.is_Local()) {
                auto it = replacements.find(lv.m_root.as_Local());
                if (it != replacements.end()) {
                    MIR_DEBUG(state, lv << " => Local(" << it->second << ")");
                    lv.m_root = ::MIR::LValue::Storage::new_Local(it->second);
                    return true;
                }
            }
            return false;
        });

        // TODO: Replace in ScopeEnd too?
    }

    return replacement_needed;
}

// --------------------------------------------------------------------
// Combine identical blocks
// --------------------------------------------------------------------
bool MIR_Optimise_UnifyBlocks(::MIR::TypeResolve& state, ::MIR::Function& fcn) {
    bool changed = false;
    TRACE_FUNCTION_FR("", changed);

    struct H {
        static size_t block_hash(const ::MIR::BasicBlock& block) {
            size_t rv = block.statements.size();
            auto add = [&](size_t v) {
                rv ^= v + 0x9e3779b9 + (rv << 6) + (rv >> 2);
            };
            for (const auto& statement : block.statements) {
                add(statement.tag());
            }
            add(block.terminator.tag());
            visit_terminator_target(block.terminator, [&](const auto& target) {
                add(target);
            });
            return rv;
        }

        static bool blocks_equal(const ::MIR::BasicBlock& a, const ::MIR::BasicBlock& b) {
            if (a.statements.size() != b.statements.size()) {
                return false;
            }
            for (unsigned int i = 0; i < a.statements.size(); i++) {
                if (a.statements[i].tag() != b.statements[i].tag()) {
                    return false;
                }
                TU_MATCH_HDRA( (a.statements[i], b.statements[i]), {)
                TU_ARMA(Assign, ae, be) {
                        if (ae.dst != be.dst) {
                            return false;
                        }
                        if (ae.src != be.src) {
                            return false;
                        }
                    }
                    TU_ARMA(Asm, ae, be) {
                        if (ae.tpl != be.tpl) {
                            return false;
                        }
                        if (ae.outputs != be.outputs) {
                            return false;
                        }
                        if (ae.inputs != be.inputs) {
                            return false;
                        }
                        if (ae.clobbers != be.clobbers) {
                            return false;
                        }
                        if (ae.flags != be.flags) {
                            return false;
                        }
                    }
                    TU_ARMA(Asm2, ae, be) {
                        if (ae.lines != be.lines) {
                            return false;
                        }
                        if (!(ae.options == be.options)) {
                            return false;
                        }
                        if (ae.params != be.params) {
                            return false;
                        }
                    }
                    TU_ARMA(SetDropFlag, ae, be) {
                        if (ae.idx != be.idx) {
                            return false;
                        }
                        if (ae.new_val != be.new_val) {
                            return false;
                        }
                        if (ae.other != be.other) {
                            return false;
                        }
                    }
                    TU_ARMA(LoadDropFlag, ae, be) {
                        if (ae.idx != be.idx) {
                            return false;
                        }
                        if (ae.slot != be.slot) {
                            return false;
                        }
                        if (ae.bit_index != be.bit_index) {
                            return false;
                        }
                    }
                    TU_ARMA(SaveDropFlag, ae, be) {
                        if (ae.idx != be.idx) {
                            return false;
                        }
                        if (ae.slot != be.slot) {
                            return false;
                        }
                        if (ae.bit_index != be.bit_index) {
                            return false;
                        }
                    }
                    TU_ARMA(Drop, ae, be) {
                        if (ae.kind != be.kind) {
                            return false;
                        }
                        if (ae.flag_idx != be.flag_idx) {
                            return false;
                        }
                        if (ae.slot != be.slot) {
                            return false;
                        }
                    }
                    TU_ARMA(ScopeEnd, ae, be) {
                        if (ae.slots != be.slots) {
                            return false;
                        }
                    }
                }
            }
            if (a.terminator.tag() != b.terminator.tag()) {
                return false;
            }
            TU_MATCHA(
                (a.terminator, b.terminator),
                (ae, be),
                (Incomplete, ),
                (Return, ),
                (Diverge, ),
                (Goto, if (ae != be) return false;),
                (Panic, if (ae.dst != be.dst) return false;),
                (If, if (ae.cond != be.cond) return false; if (ae.bb_true != be.bb_true) return false; if (ae.bb_false != be.bb_false) return false;),
                (Switch, if (ae.val != be.val) return false; if (ae.targets != be.targets) return false;),
                (SwitchValue, if (ae.val != be.val) return false; if (ae.targets != be.targets) return false; if (ae.def_target != be.def_target) return false; if (ae.values != be.values) return false;),
                (Call, if (ae.ret_block != be.ret_block) return false; if (ae.panic_block != be.panic_block) return false; if (ae.ret_val != be.ret_val) return false; if (ae.args != be.args) return false;

                 if (ae.fcn.tag() != be.fcn.tag()) return false;
                 TU_MATCHA((ae.fcn, be.fcn), (af, bf), (Value, if (af != bf) return false;), (Path, if (af != bf) return false;), (Intrinsic, if (af.name != bf.name) return false; if (af.params != bf.params) return false;)))
            )
            return true;
        }
    };

    // Locate duplicate blocks and replace
    ::std::map<unsigned int, unsigned int> replacements;
    ::std::unordered_map<size_t, ::std::vector<unsigned int>> candidates;
    for (unsigned int bb_idx = 0; bb_idx < fcn.blocks.size(); bb_idx++) {
        if (fcn.blocks[bb_idx].terminator.tag() == ::MIR::Terminator::TAGDEAD) {
            continue;
        }
        if (fcn.blocks[bb_idx].terminator.is_Incomplete() && fcn.blocks[bb_idx].statements.size() == 0) {
            continue;
        }
        auto& bucket = candidates[H::block_hash(fcn.blocks[bb_idx])];
        bool found = false;
        for (auto candidate : bucket) {
            if (H::blocks_equal(fcn.blocks[candidate], fcn.blocks[bb_idx])) {
                replacements[bb_idx] = candidate;
                found = true;
                break;
            }
        }
        if (!found) {
            bucket.push_back(bb_idx);
        }
    }

    if (!replacements.empty()) {
        //MIR_TODO(state, "Unify blocks - " << replacements);
        DEBUG("Unify blocks (old: new) - " << replacements);
        auto patch_tgt = [&replacements](::MIR::BasicBlockId& tgt) {
            auto it = replacements.find(tgt);
            if (it != replacements.end()) {
                //DEBUG("BB" << tgt << " => BB" << it->second);
                tgt = it->second;
            }
        };
        for (auto& bb : fcn.blocks) {
            if (bb.terminator.tag() == ::MIR::Terminator::TAGDEAD) {
                continue;
            }
            visit_terminator_target_mut(bb.terminator, [&](auto& te) {
                patch_tgt(te);
            });
            //DEBUG("- " << bb.terminator);
        }

        for (const auto& r : replacements) {
            fcn.blocks[r.first] = ::MIR::BasicBlock{};
            //auto _ = mv$(fcn.blocks[r.first].terminator);
        }

        changed = true;
    }
    return changed;
}

// --------------------------------------------------------------------
// Propagate source values when a composite (tuple) is read
//
// TODO: Is this needed now that SplitAggregates exists?
// --------------------------------------------------------------------
bool MIR_Optimise_PropagateKnownValues(::MIR::TypeResolve& state, ::MIR::Function& fcn) {
    bool change_happend = false;
    TRACE_FUNCTION_FR("", change_happend);
    // 1. Determine reference counts for blocks (allows reversing up BB tree)
    ::std::vector<size_t> block_origins(fcn.blocks.size(), SIZE_MAX);
    {
        ::std::vector<unsigned int> block_uses(fcn.blocks.size());
        ::std::vector<bool> visited(fcn.blocks.size());
        ::std::vector<::MIR::BasicBlockId> to_visit;
        to_visit.push_back(0);
        block_uses[0]++;
        while (to_visit.size() > 0) {
            auto bb = to_visit.back();
            to_visit.pop_back();
            if (visited[bb]) {
                continue;
            }
            visited[bb] = true;
            const auto& block = fcn.blocks[bb];

            visit_terminator_target(block.terminator, [&](const auto& idx) {
                if (!visited[idx]) {
                    to_visit.push_back(idx);
                }
                if (block_uses[idx] == 0) {
                    block_origins[idx] = bb;
                } else {
                    block_origins[idx] = SIZE_MAX;
                }
                block_uses[idx]++;
            });
        }
    }

    // 2. Find any assignments (or function uses?) of the form FIELD(LOCAL, _)
    //  > Restricted to simplify logic (and because that's the inefficient pattern observed)
    // 3. Search backwards from that point until the referenced local is assigned
    auto get_field = [&](const ::MIR::LValue& slot_lvalue, unsigned field, size_t start_bb_idx, size_t start_stmt_idx) -> const ::MIR::LValue* {
        TRACE_FUNCTION_F(slot_lvalue << "." << field << " BB" << start_bb_idx << "/" << start_stmt_idx);
        bool slot_copy = state.lvalue_is_copy(slot_lvalue);
        // NOTE: An infinite loop is (theoretically) impossible.
        auto bb_idx = start_bb_idx;
        auto stmt_idx = start_stmt_idx;
        for (;;) {
            const auto& bb = fcn.blocks[bb_idx];
            while (stmt_idx--) {
                if (stmt_idx == bb.statements.size()) {
                    DEBUG("BB" << bb_idx << "/TERM - " << bb.terminator);
                    if (check_invalidates_lvalue(bb.terminator, slot_lvalue, slot_copy)) {
                        return nullptr;
                    }
                    continue;
                }
                const auto& stmt = bb.statements[stmt_idx];
                DEBUG("BB" << bb_idx << "/" << stmt_idx << " - " << stmt);
                if (const auto* se = stmt.opt_Assign()) {
                    if (se->dst == slot_lvalue) {
                        if (!se->src.is_Tuple()) {
                            return nullptr;
                        }
                        const auto& src_param = se->src.as_Tuple().vals.at(field);
                        DEBUG("> Found a source " << src_param);
                        // TODO: Support returning a Param
                        if (!src_param.is_LValue()) {
                            return nullptr;
                        }
                        const auto& src_lval = src_param.as_LValue();
                        bool src_copy = state.lvalue_is_copy(src_lval);
                        // Visit all statements between the start and here, checking for mutation of this value.
                        auto end_bb_idx = bb_idx;
                        auto end_stmt_idx = stmt_idx;
                        bb_idx = start_bb_idx;
                        stmt_idx = start_stmt_idx;
                        for (;;) {
                            const auto& bb = fcn.blocks[bb_idx];
                            while (stmt_idx--) {
                                if (bb_idx == end_bb_idx && stmt_idx == end_stmt_idx) {
                                    return &src_lval;
                                }
                                if (stmt_idx == bb.statements.size()) {
                                    DEBUG("BB" << bb_idx << "/TERM - " << bb.terminator);
                                    if (check_invalidates_lvalue(bb.terminator, src_lval, src_copy)) {
                                        // Invalidated: Return.
                                        return nullptr;
                                    }
                                    continue;
                                }
                                if (check_invalidates_lvalue(bb.statements[stmt_idx], src_lval, src_copy)) {
                                    // Invalidated: Return.
                                    return nullptr;
                                }
                            }
                            assert(block_origins[bb_idx] != SIZE_MAX);
                            bb_idx = block_origins[bb_idx];
                            stmt_idx = fcn.blocks[bb_idx].statements.size() + 1;
                        }
                        throw "";
                    }
                }

                // Check if the slot is invalidated (mutated)
                if (check_invalidates_lvalue(stmt, slot_lvalue, slot_copy)) {
                    return nullptr;
                }
            }
            if (block_origins[bb_idx] == SIZE_MAX) {
                break;
            }
            bb_idx = block_origins[bb_idx];
            stmt_idx = fcn.blocks[bb_idx].statements.size() + 1;
        }
        return nullptr;
    };
    for (auto& block : fcn.blocks) {
        size_t bb_idx = &block - &fcn.blocks.front();
        for (size_t i = 0; i < block.statements.size(); i++) {
            state.set_cur_stmt(bb_idx, i);
            DEBUG(state << block.statements[i]);
            visit_mir_lvalues_mut(block.statements[i], [&](::MIR::LValue& lv, auto vu) {
                if (vu == ValUsage::Read && lv.m_wrappers.size() > 1 && lv.m_wrappers.front().is_Field() && lv.m_root.is_Local()) {
                    auto field_index = lv.m_wrappers.front().as_Field();
                    auto inner_lv = ::MIR::LValue::new_Local(lv.m_root.as_Local());
                    auto outer_lv = ::MIR::LValue::new_Field(inner_lv.clone(), field_index);
                    // TODO: This value _must_ be Copy for this optimisation to work.
                    // - OR, it has to somehow invalidate the original tuple
                    DEBUG(state << "Locating origin of " << lv);
                    ::HIR::TypeRef tmp;
                    if (!state.m_resolve.type_is_copy(state.sp, state.get_lvalue_type(tmp, inner_lv))) {
                        DEBUG(state << "- not Copy, can't optimise");
                        return false;
                    }
                    const auto* source_lvalue = get_field(inner_lv, field_index, bb_idx, i);
                    if (source_lvalue) {
                        if (outer_lv != *source_lvalue) {
                            DEBUG(state << "Source is " << *source_lvalue);
                            lv = source_lvalue->clone_wrapped(lv.m_wrappers.begin() + 1, lv.m_wrappers.end());
                            change_happend = true;
                        } else {
                            DEBUG(state << "No change");
                        }
                        return false;
                    }
                }
                return false;
            });
        }
    }
    return change_happend;
}

// --------------------------------------------------------------------
// Propagate constants and eliminate known paths
// --------------------------------------------------------------------
bool MIR_Optimise_ConstPropagate(::MIR::TypeResolve& state, ::MIR::Function& fcn) {
#if DUMP_BEFORE_ALL || DUMP_BEFORE_CONSTPROPAGATE
    if (debug_enabled()) {
        MIR_Dump_Fcn(::std::cout, fcn);
    }
#endif
    bool changed = false;
    TRACE_FUNCTION_FR("", changed);
    auto make_float_arithmetic_result = [](FloatValue value, ::HIR::CoreType type) {
        if (float_value_is_nan(value)) {
            value = positive_nan_float_value();
        }
        return ::MIR::Constant::make_Float({value, type});
    };

    // - Remove calls to `size_of` and `align_of` (replace with value if known)
    for (auto& bb : fcn.blocks) {
        state.set_cur_stmt_term(bb);
        MIR_DEBUG(state, bb.terminator);
        if (!bb.terminator.is_Call()) {
            continue;
        }
        auto& te = bb.terminator.as_Call();
        if (!te.fcn.is_Intrinsic()) {
            continue;
        }
        const auto& tef = te.fcn.as_Intrinsic();
        if (tef.name == "size_of") {
            size_t size_val = 0;
            if (Target_GetSizeOf(state.sp, state.m_resolve, tef.params.m_types.at(0), size_val)) {
                DEBUG("size_of = " << size_val);
                auto val = ::MIR::Constant::make_Uint({U128(size_val), ::HIR::CoreType::Usize});
                bb.statements.push_back(::MIR::Statement::make_Assign({mv$(te.ret_val), mv$(val)}));
                bb.terminator = ::MIR::Terminator::make_Goto(te.ret_block);
                changed = true;
            }
        } else if (tef.name == "size_of_val") {
            size_t size_val = 0, tmp;
            if (Target_GetSizeAndAlignOf(state.sp, state.m_resolve, tef.params.m_types.at(0), size_val, tmp) && size_val != SIZE_MAX) {
                DEBUG("size_of_val = " << size_val);
                auto val = ::MIR::Constant::make_Uint({U128(size_val), ::HIR::CoreType::Usize});
                bb.statements.push_back(::MIR::Statement::make_Assign({mv$(te.ret_val), mv$(val)}));
                bb.terminator = ::MIR::Terminator::make_Goto(te.ret_block);
                changed = true;
            }
        } else if (tef.name == "align_of" || tef.name == "min_align_of") {
            size_t align_val = 0;
            if (Target_GetAlignOf(state.sp, state.m_resolve, tef.params.m_types.at(0), align_val)) {
                DEBUG("align_of = " << align_val);
                auto val = ::MIR::Constant::make_Uint({U128(align_val), ::HIR::CoreType::Usize});
                bb.statements.push_back(::MIR::Statement::make_Assign({mv$(te.ret_val), mv$(val)}));
                bb.terminator = ::MIR::Terminator::make_Goto(te.ret_block);
                changed = true;
            }
        } else if (tef.name == "min_align_of_val") {
            size_t align_val = 0;
            size_t size_val = 0;
            // Note: Trait object returns align_val = 0 (slice-based types have an alignment)
            if (Target_GetSizeAndAlignOf(state.sp, state.m_resolve, tef.params.m_types.at(0), size_val, align_val) && align_val > 0) {
                DEBUG("min_align_of_val = " << align_val);
                auto val = ::MIR::Constant::make_Uint({U128(align_val), ::HIR::CoreType::Usize});
                bb.statements.push_back(::MIR::Statement::make_Assign({mv$(te.ret_val), mv$(val)}));
                bb.terminator = ::MIR::Terminator::make_Goto(te.ret_block);
                changed = true;
            }
        }
        // NOTE: Quick special-case for bswap<u8/i8> (a no-op)
        else if (tef.name == "bswap" && (tef.params.m_types.at(0) == ::HIR::CoreType::U8 || tef.params.m_types.at(0) == ::HIR::CoreType::I8)) {
            DEBUG("bswap<u8> is a no-op");
            if (auto* e = te.args.at(0).opt_LValue()) {
                bb.statements.push_back(::MIR::Statement::make_Assign({mv$(te.ret_val), mv$(*e)}));
            } else {
                bb.statements.push_back(::MIR::Statement::make_Assign({mv$(te.ret_val), mv$(te.args.at(0).as_Constant())}));
            }
            bb.terminator = ::MIR::Terminator::make_Goto(te.ret_block);
            changed = true;
        } else if (tef.name == "mrustc_slice_len") {
            MIR_ASSERT(state, te.args.at(0).is_LValue(), "Argument to `mrustc_slice_len` must be a lvalue");
            auto& e = te.args.at(0).as_LValue();
            bb.statements.push_back(::MIR::Statement::make_Assign({mv$(te.ret_val), ::MIR::RValue::make_DstMeta({mv$(e)})}));
            bb.terminator = ::MIR::Terminator::make_Goto(te.ret_block);
            changed = true;
        } else if (tef.name == "needs_drop") {
            // Returns `true` if the actual type given as `T` requires drop glue;
            // returns `false` if the actual type provided for `T` implements `Copy`. (Either otherwise)
            // NOTE: libarena assumes that this returns `true` iff T doesn't require drop glue.
            const auto& ty = tef.params.m_types.at(0);
            // - Only expand at this stage if there's no generics, and no unbound paths
            if (!visit_ty_with(ty, [](const ::HIR::TypeRef& ty) -> bool {
                return ty->is_Generic() || TU_TEST1(*ty, Path, .binding.is_Unbound());
            })) {
                bool needs_drop = state.m_resolve.type_needs_drop_glue(state.sp, ty);
                bb.statements.push_back(::MIR::Statement::make_Assign({mv$(te.ret_val), ::MIR::RValue::make_Constant(::MIR::Constant::make_Bool({needs_drop}))}));
                bb.terminator = ::MIR::Terminator::make_Goto(te.ret_block);
                changed = true;
            }
        } else {
            // Ignore any other intrinsics
        }
    }

    // - Propage constants within BBs
    //  > Evaluate BinOp with known values
    //  > Understand intrinsics like overflowing_* (with correct semantics)
    //   > NOTE: No need to locally stitch blocks, next pass will do that
    // TODO: Use ValState to do full constant propagation across blocks

    // Remove redundant temporaries and evaluate known binops
    for (auto& bb : fcn.blocks) {
        auto bbidx = &bb - &fcn.blocks.front();

        ::std::map<::MIR::LValue, ::MIR::Constant> known_values;
        // Known enum variants
        ::std::map<::MIR::LValue, unsigned> known_values_var;
        ::std::map<unsigned, bool> known_drop_flags;

        auto check_lv = [&](const ::MIR::LValue& lv) -> ::MIR::Constant {
            auto it = known_values.find(lv);
            if (it != known_values.end()) {
                DEBUG(state << "Value " << lv << " known to be " << it->second);
                return it->second.clone();
            }

            // TODO: If the inner of the value is known,
            //   AND all indexes are known - expand
            //if( !lv.m_wrappers.empty() )
            //{
            //    it = known_values.find(lv.m_root);
            //    if( it != known_values.end() )
            //    {
            //        // TODO: Use HIR::Literal instead so composites can be handled.
            //        for(const auto& w : lv.m_wrappers)
            //        {
            //        }
            //    }
            //}

            // Reads of statics
            if (lv.m_wrappers.empty() && lv.m_root.is_Static()) {
                DEBUG("Read of a static - " << lv.m_root.as_Static());
                // Look up this static, and see if it's not mutable, and a primitive
                // - If the static is an immutable primitive: read and save
                MonomorphState ms(state.m_resolve.m_crate.m_types);
                auto v = state.m_resolve.get_value(state.sp, lv.m_root.as_Static(), ms);
                if (v.is_Static()) {
                    const auto& stat = *v.as_Static();
                    if (stat.m_value_generated && !stat.m_is_mut && state.m_resolve.type_is_interior_mutable(state.sp, stat.m_type) == HIR::Compare::Unequal) {
                        // Convert the encoded literal into a `MIR::Const`
                        const auto el = EncodedLiteralSlice(stat.m_value_res);
                        // Check the type
                        // - Primitives
                        if (stat.m_type->is_Primitive()) {
                            auto ty = stat.m_type->as_Primitive();
                            switch (ty) {
                                case HIR::CoreType::Char:
                                case HIR::CoreType::Usize:
                                case HIR::CoreType::U128:
                                case HIR::CoreType::U64:
                                case HIR::CoreType::U32:
                                case HIR::CoreType::U16:
                                case HIR::CoreType::U8:
                                    return ::MIR::Constant::make_Uint({el.read_uint(el.m_size), ty});
                                case HIR::CoreType::Bool:
                                    return ::MIR::Constant::make_Bool({el.read_uint(el.m_size) != 0});
                                case HIR::CoreType::Isize:
                                case HIR::CoreType::I128:
                                case HIR::CoreType::I64:
                                case HIR::CoreType::I32:
                                case HIR::CoreType::I16:
                                case HIR::CoreType::I8:
                                    return ::MIR::Constant::make_Int({el.read_sint(el.m_size), ty});
                                case HIR::CoreType::F16:
                                case HIR::CoreType::F32:
                                case HIR::CoreType::F64:
                                case HIR::CoreType::F128:
                                    return ::MIR::Constant::make_Float({el.read_float(el.m_size), ty});
                                case HIR::CoreType::Str:
                                    MIR_BUG(state, "Constant of type `str`?");
                            }
                        }
                        // - Pointers
                        if (stat.m_type->is_Borrow()) {
                            // TODO: Read the borrow, and store
                        }
                        // - Could traverse the static via the wrappers too?
                    }
                }
            }

            // Not a known value, and not a known composite
            // - Use a nullptr ItemAddr to indicate this
            return ::MIR::Constant::make_ItemAddr({});
        };
        auto check_param = [&](::MIR::Param& p) {
            if (const auto* pe = p.opt_LValue()) {
                auto nv = check_lv(*pe);
                if (nv.is_ItemAddr() && !nv.as_ItemAddr()) {
                    // ItemAddr with a nullptr inner means "no expansion"
                } else {
                    p = mv$(nv);
                    changed = true;
                }
            }
        };

        // Convert known indexes into field acceses
        auto edit_lval = [&](MIR::LValue& lv, ValUsage _vu) -> bool {
            for (auto& w : lv.m_wrappers) {
                if (w.is_Index()) {
                    auto it = known_values.find(MIR::LValue::new_Local(w.as_Index()));
                    if (it != known_values.find(lv) && !it->second.is_Const() && !it->second.is_Generic()) {
                        MIR_ASSERT(state, it->second.is_Uint(), "Indexing with non-Uint constant - " << it->second);
                        MIR_ASSERT(state, it->second.as_Uint().t == HIR::CoreType::Usize, "Indexing with non-usize constant - " << it->second);
                        auto idx = it->second.as_Uint().v;
                        MIR_ASSERT(state, idx < (1 << 30), "Known index is excessively large");
                        w = MIR::LValue::Wrapper::new_Field(idx.truncate_u64());
                        changed = true;
                    }
                }
            }

            // If a Deref of a known value is seen, replace with the source of that value.
            if (!lv.m_wrappers.empty() && lv.m_wrappers.front().is_Deref() && !lv.m_root.is_Static()) {
                auto ilv = MIR::LValue(lv.m_root.clone(), {});
                auto it = known_values.find(ilv);
                if (it != known_values.find(lv)) {
                    DEBUG("Known deref source: " << ilv << " == " << it->second);
                    //MIR_ASSERT(state, it->second.is_ItemAddr(), "Derefernce with known value not an ItemAddr - " << it->second);
                    if (it->second.is_ItemAddr() && it->second.as_ItemAddr().offset == U128(0)) {
                        lv.m_wrappers.erase(lv.m_wrappers.begin());
                        lv.m_root = MIR::LValue::Storage::new_Static(it->second.as_ItemAddr()->clone());
                        changed = true;
                    }
                }
            }
            return true;
        };

        for (auto& stmt : bb.statements) {
            auto stmtidx = &stmt - &bb.statements.front();
            state.set_cur_stmt(bbidx, stmtidx);

            visit_mir_lvalues_mut(stmt, edit_lval);

            // Scan statements forwards:
            // - If a known temporary is used as Param::LValue, replace LValue with the value
            // - If a UniOp has its input known, evaluate
            // - If a BinOp has both values known, evaluate
            if (auto* e = stmt.opt_Assign()) {
                struct H {
                    static S128 truncate_s(::HIR::CoreType ct, S128 v) {
                        // Truncate unsigned, then sign extend
                        auto u = H::truncate_u(ct, v.get_inner());
                        switch (ct) {
                            case ::HIR::CoreType::I8:
                                return sext(u, 8);
                            case ::HIR::CoreType::I16:
                                return sext(u, 16);
                            case ::HIR::CoreType::I32:
                                return sext(u, 32);
                            case ::HIR::CoreType::I64:
                                return sext(u, 64);
                            case ::HIR::CoreType::I128:
                                return v;
                            // usize/size - need to handle <64 pointer bits
                            case ::HIR::CoreType::Isize:
                                if (Target_GetPointerBits() < 64) {
                                    return sext(u, Target_GetPointerBits());
                                }
                                return v;
                            default:
                                // Invalid type for `Constant::Int` literal
                                break;
                        }
                        return v;
                    }

                    static S128 sext(U128 v, unsigned bits) {
                        if (v >> (bits - 1) != 0) {
                            return S128(v | (U128::max() << bits));
                        } else {
                            return S128(v);
                        }
                    }

                    static U128 truncate_u(::HIR::CoreType ct, U128 v) {
                        switch (ct) {
                            case ::HIR::CoreType::I8:
                            case ::HIR::CoreType::U8:
                                return v & U128(0xFF);
                            case ::HIR::CoreType::I16:
                            case ::HIR::CoreType::U16:
                                return v & U128(0xFFFF);
                            case ::HIR::CoreType::I32:
                            case ::HIR::CoreType::U32:
                                return v & U128(0xFFFFFFFF);
                            case ::HIR::CoreType::I64:
                            case ::HIR::CoreType::U64:
                                return v & U128(UINT64_MAX);
                            case ::HIR::CoreType::I128:
                            case ::HIR::CoreType::U128:
                                return v;
                            // usize/size - need to handle <64 pointer bits
                            case ::HIR::CoreType::Isize:
                            case ::HIR::CoreType::Usize:
                                if (Target_GetPointerBits() < 64) {
                                    return v & U128(UINT64_MAX >> (64 - Target_GetPointerBits()));
                                }
                                return v & U128(UINT64_MAX);
                            case ::HIR::CoreType::Char:
                                //MIR_BUG(state, "Invalid use of operator on char");
                                break;
                            default:
                                // Invalid type for Uint literal
                                break;
                        }
                        return v;
                    }
                };

                TU_MATCH_HDRA( (e->src), {)
                TU_ARMA(Use, se) {
                        auto nv = check_lv(se);
                        if (nv.is_ItemAddr() && !nv.as_ItemAddr()) {
                            // ItemAddr with a nullptr inner means "no expansion"
                        } else {
                            e->src = ::MIR::RValue::make_Constant(mv$(nv));
                            changed = true;
                        }
                    }
                    TU_ARMA(Constant, se) {
                        // Ignore (knowledge done below)
                    }
                    TU_ARMA(SizedArray, se) {
                        check_param(se.val);
                    }
                    TU_ARMA(Borrow, se) {
                        // Shared borrows of statics can be better represented with the ItemAddr constant
                        if (se.type == HIR::BorrowType::Shared && se.val.m_wrappers.empty() && se.val.m_root.is_Static()) {
                            e->src = ::MIR::RValue::make_Constant(::MIR::Constant::make_ItemAddr({box$(se.val.m_root.as_Static())}));
                            changed = true;
                        } else if (se.type == HIR::BorrowType::Unique) {
                            known_values.erase(se.val);
                            known_values_var.erase(se.val);
                        }
                    }
                    TU_ARMA(Cast, se) {
                        ::MIR::Constant new_value;

                        // If casting a number to a number, do the cast and
                        auto nv = check_lv(se.val);
                        if (!nv.is_ItemAddr()) {
                            if (const auto* te = se.type->opt_Primitive()) {
                                switch (*te) {
                                    case ::HIR::CoreType::U8:
                                    case ::HIR::CoreType::U16:
                                    case ::HIR::CoreType::U32:
                                    case ::HIR::CoreType::U64:
                                    case ::HIR::CoreType::U128:
                                    case ::HIR::CoreType::Usize:
                                        if (const auto* vp = nv.opt_Uint()) {
                                            new_value = ::MIR::Constant::make_Uint({H::truncate_u(*te, vp->v), *te});
                                        } else if (const auto* vp = nv.opt_Int()) {
                                            new_value = ::MIR::Constant::make_Uint({H::truncate_u(*te, vp->v.get_inner()), *te});
                                        } else if (const auto* vp = nv.opt_Bool()) {
                                            new_value = ::MIR::Constant::make_Uint({U128(vp->v ? 1u : 0u), *te});
                                        } else if (const auto* vp = nv.opt_Float()) {
                                            if (0.0 <= vp->v && vp->v <= UINT64_MAX) {
                                                new_value = ::MIR::Constant::make_Uint({H::truncate_u(*te, U128(static_cast<uint64_t>(vp->v))), *te});
                                            } else {
                                                // UB: Casting float out of range?
                                            }
                                        } else {
                                        }
                                        break;
                                    case ::HIR::CoreType::I8:
                                    case ::HIR::CoreType::I16:
                                    case ::HIR::CoreType::I32:
                                    case ::HIR::CoreType::I64:
                                    case ::HIR::CoreType::I128:
                                    case ::HIR::CoreType::Isize:
                                        if (const auto* vp = nv.opt_Uint()) {
                                            new_value = ::MIR::Constant::make_Int({H::truncate_s(*te, vp->v), *te});
                                        } else if (const auto* vp = nv.opt_Int()) {
                                            new_value = ::MIR::Constant::make_Int({H::truncate_s(*te, vp->v), *te});
                                        } else if (const auto* vp = nv.opt_Bool()) {
                                            new_value = ::MIR::Constant::make_Int({S128(vp->v ? 1 : 0), *te});
                                        } else {
                                        }
                                        break;
                                    case ::HIR::CoreType::F16:
                                    case ::HIR::CoreType::F32:
                                    case ::HIR::CoreType::F64:
                                    case ::HIR::CoreType::F128:
                                        // TODO: Cast to float
                                        break;
                                    case ::HIR::CoreType::Char:
                                        // TODO: Only `u8` can be casted to char
                                        break;
                                    case ::HIR::CoreType::Bool:
                                        break;
                                    case ::HIR::CoreType::Str:
                                        MIR_BUG(state, "Casting to str");
                                }
                            }
                        } else if (known_values_var.count(se.val)) {
                            auto variant_idx = known_values_var.at(se.val);
                            MIR_ASSERT(state, se.type->is_Primitive(), "Casting enum to non-primitive - " << se.type);

                            HIR::TypeRef tmp;
                            const auto& src_ty = state.get_lvalue_type(tmp, se.val);
                            const HIR::Enum& enm = *src_ty->as_Path().binding.as_Enum();
                            MIR_ASSERT(state, enm.is_value(), "Casting non-value enum to value");
                            auto v = enm.get_value(variant_idx);

                            const auto* repr = Target_GetTypeRepr(state.sp, state.m_resolve, src_ty);
                            MIR_ASSERT(state, repr && repr->variants.is_Values(), "Value enum without values repr - " << src_ty);
                            const auto& values = repr->variants.as_Values();
                            const auto& tag_ty = Target_GetInnerType(state.sp, state.m_resolve, *repr, values.field.index, values.field.sub_fields);
                            MIR_ASSERT(state, tag_ty->is_Primitive(), "Value enum with non-primitive tag - " << src_ty);

                            auto value = S128(U128(v));
                            switch (tag_ty->as_Primitive()) {
                                case ::HIR::CoreType::I8:
                                case ::HIR::CoreType::I16:
                                case ::HIR::CoreType::I32:
                                case ::HIR::CoreType::I64:
                                case ::HIR::CoreType::I128:
                                case ::HIR::CoreType::Isize:
                                    value = H::truncate_s(tag_ty->as_Primitive(), value);
                                    break;
                                default:
                                    value = S128(H::truncate_u(tag_ty->as_Primitive(), value.get_inner()));
                                    break;
                            }

                            auto ct = se.type->as_Primitive();
                            switch (ct) {
                                case ::HIR::CoreType::U8:
                                case ::HIR::CoreType::U16:
                                case ::HIR::CoreType::U32:
                                case ::HIR::CoreType::U64:
                                case ::HIR::CoreType::U128:
                                case ::HIR::CoreType::Usize:
                                    new_value = ::MIR::Constant::make_Uint({H::truncate_u(ct, value.get_inner()), ct});
                                    break;
                                case ::HIR::CoreType::I8:
                                case ::HIR::CoreType::I16:
                                case ::HIR::CoreType::I32:
                                case ::HIR::CoreType::I64:
                                case ::HIR::CoreType::I128:
                                case ::HIR::CoreType::Isize:
                                    new_value = ::MIR::Constant::make_Int({H::truncate_s(ct, value), ct});
                                    break;
                                case ::HIR::CoreType::F16:
                                case ::HIR::CoreType::F32:
                                case ::HIR::CoreType::F64:
                                case ::HIR::CoreType::F128:
                                    // TODO: Cast to float (can variants be casted to float?)
                                    break;
                                case ::HIR::CoreType::Char:
                                    // TODO: Only `u8` can be casted to char (what about a u8 discriminator?)
                                    break;
                                case ::HIR::CoreType::Bool:
                                    break;
                                case ::HIR::CoreType::Str:
                                    MIR_BUG(state, "Casting to str");
                            }
                        } else {
                        }

                        if (new_value != MIR::Constant()) {
                            DEBUG(state << " " << e->src << " = " << new_value);
                            e->src = mv$(new_value);
                            changed = true;
                        }
                    }
                    TU_ARMA(BinOp, se) {
                        check_param(se.val_l);
                        check_param(se.val_r);

                        if (se.val_l.is_Constant() && se.val_r.is_Constant()) {
                            const auto& val_l = se.val_l.as_Constant();
                            const auto& val_r = se.val_r.as_Constant();

                            if (val_l.is_Const() || val_r.is_Const()) {
                                // One of the arms is a named constant, can't check (they're not an actual value, just a
                                // reference to one)
                            } else if (val_l.is_Generic() || val_r.is_Generic()) {
                                // One of the arms is a generic, can't check either
                            } else {
                                ::MIR::Constant new_value;
                                switch (se.op) {
                                    // Note: f32's bit accuracy is different to f64, so they can't be considered equivalent in behaviour
                                    case ::MIR::eBinOp::EQ:
                                        if (!val_l.is_Float()) {
                                            new_value = ::MIR::Constant::make_Bool({val_l == val_r});
                                        }
                                        break;
                                    case ::MIR::eBinOp::NE:
                                        if (!val_l.is_Float()) {
                                            new_value = ::MIR::Constant::make_Bool({val_l != val_r});
                                        }
                                        break;
                                    case ::MIR::eBinOp::LT:
                                        if (!val_l.is_Float()) {
                                            new_value = ::MIR::Constant::make_Bool({val_l < val_r});
                                        }
                                        break;
                                    case ::MIR::eBinOp::LE:
                                        if (!val_l.is_Float()) {
                                            new_value = ::MIR::Constant::make_Bool({val_l <= val_r});
                                        }
                                        break;
                                    case ::MIR::eBinOp::GT:
                                        if (!val_l.is_Float()) {
                                            new_value = ::MIR::Constant::make_Bool({val_l > val_r});
                                        }
                                        break;
                                    case ::MIR::eBinOp::GE:
                                        if (!val_l.is_Float()) {
                                            new_value = ::MIR::Constant::make_Bool({val_l >= val_r});
                                        }
                                        break;

                                    case ::MIR::eBinOp::ADD:
                                        MIR_ASSERT(state, val_l.tag() == val_r.tag(), "Mismatched types for eBinOp::ADD - " << val_l << " + " << val_r);
                                TU_MATCH_HDRA( (val_l, val_r), {)
                                default:
                                    break;
                                            TU_ARMA(Float, le, re) {
                                                MIR_ASSERT(state, le.t == re.t, "Mismatched types for eBinOp::ADD - " << val_l << " / " << val_r);
                                                new_value = make_float_arithmetic_result(le.v + re.v, le.t);
                                            }
                                            TU_ARMA(Int, le, re) {
                                                MIR_ASSERT(state, le.t == re.t, "Mismatched types for eBinOp::ADD - " << val_l << " + " << val_r);
                                                new_value = ::MIR::Constant::make_Int({H::truncate_s(le.t, le.v + re.v), le.t});
                                            }
                                            TU_ARMA(Uint, le, re) {
                                                MIR_ASSERT(state, le.t == re.t, "Mismatched types for eBinOp::ADD - " << val_l << " + " << val_r);
                                                new_value = ::MIR::Constant::make_Uint({H::truncate_u(le.t, le.v + re.v), le.t});
                                            }
                                }
                                break;
                            case ::MIR::eBinOp::SUB:
                                MIR_ASSERT(state, val_l.tag() == val_r.tag(), "Mismatched types for eBinOp::SUB - " << val_l << " + " << val_r);
                                TU_MATCH_HDRA( (val_l, val_r), {)
                                default:
                                    break;
                                            TU_ARMA(Float, le, re) {
                                                MIR_ASSERT(state, le.t == re.t, "Mismatched types for eBinOp::SUB - " << val_l << " / " << val_r);
                                                new_value = make_float_arithmetic_result(le.v - re.v, le.t);
                                            }
                                            TU_ARMA(Int, le, re) {
                                                MIR_ASSERT(state, le.t == re.t, "Mismatched types for eBinOp::SUB - " << val_l << " - " << val_r);
                                                new_value = ::MIR::Constant::make_Int({H::truncate_s(le.t, le.v - re.v), le.t});
                                            }
                                            TU_ARMA(Uint, le, re) {
                                                MIR_ASSERT(state, le.t == re.t, "Mismatched types for eBinOp::SUB - " << val_l << " - " << val_r);
                                                new_value = ::MIR::Constant::make_Uint({H::truncate_u(le.t, le.v - re.v), le.t});
                                            }
                                }
                                break;
                            case ::MIR::eBinOp::MUL:
                                MIR_ASSERT(state, val_l.tag() == val_r.tag(), "Mismatched types for eBinOp::MUL - " << val_l << " * " << val_r);
                                TU_MATCH_HDRA( (val_l, val_r), {)
                                default:
                                    break;
                                            TU_ARMA(Float, le, re) {
                                                MIR_ASSERT(state, le.t == re.t, "Mismatched types for eBinOp::MUL - " << val_l << " / " << val_r);
                                                new_value = make_float_arithmetic_result(le.v * re.v, le.t);
                                            }
                                            TU_ARMA(Int, le, re) {
                                                MIR_ASSERT(state, le.t == re.t, "Mismatched types for eBinOp::MUL - " << val_l << " * " << val_r);
                                                new_value = ::MIR::Constant::make_Int({H::truncate_s(le.t, le.v * re.v), le.t});
                                            }
                                            TU_ARMA(Uint, le, re) {
                                                MIR_ASSERT(state, le.t == re.t, "Mismatched types for eBinOp::MUL - " << val_l << " * " << val_r);
                                                new_value = ::MIR::Constant::make_Uint({H::truncate_u(le.t, le.v * re.v), le.t});
                                            }
                                }
                                break;
                            case ::MIR::eBinOp::DIV:
                                MIR_ASSERT(state, val_l.tag() == val_r.tag(), "Mismatched types for eBinOp::DIV - " << val_l << " / " << val_r);
                                TU_MATCH_HDRA( (val_l, val_r), {)
                                default:
                                    break;
                                            TU_ARMA(Float, le, re) {
                                                MIR_ASSERT(state, le.t == re.t, "Mismatched types for eBinOp::DIV - " << val_l << " / " << val_r);
                                                new_value = make_float_arithmetic_result(le.v / re.v, le.t);
                                            }
                                            TU_ARMA(Int, le, re) {
                                                MIR_ASSERT(state, le.t == re.t, "Mismatched types for eBinOp::DIV - " << val_l << " / " << val_r);
                                                if (re.v == 0) {
                                                    DEBUG(state << "Const eval error: Constant division by zero");
                                                } else {
                                                    new_value = ::MIR::Constant::make_Int({H::truncate_s(le.t, le.v / re.v), le.t});
                                                }
                                            }
                                            TU_ARMA(Uint, le, re) {
                                                MIR_ASSERT(state, le.t == re.t, "Mismatched types for eBinOp::DIV - " << val_l << " / " << val_r);
                                                if (re.v == 0) {
                                                    DEBUG(state << "Const eval error: Constant division by zero");
                                                } else {
                                                    new_value = ::MIR::Constant::make_Uint({H::truncate_u(le.t, le.v / re.v), le.t});
                                                }
                                            }
                                }
                                break;
                            case ::MIR::eBinOp::MOD:
                                MIR_ASSERT(state, val_l.tag() == val_r.tag(), "Mismatched types for eBinOp::MOD - " << val_l << " % " << val_r);
                                TU_MATCH_HDRA( (val_l, val_r), {)
                                default:
                                    break;
                                            TU_ARMA(Int, le, re) {
                                                MIR_ASSERT(state, le.t == re.t, "Mismatched types for eBinOp::MOD - " << val_l << " % " << val_r);
                                                MIR_ASSERT(state, re.v != 0, "Const eval error: Constant division by zero");
                                                new_value = ::MIR::Constant::make_Int({H::truncate_s(le.t, le.v % re.v), le.t});
                                            }
                                            TU_ARMA(Uint, le, re) {
                                                MIR_ASSERT(state, le.t == re.t, "Mismatched types for eBinOp::MOD - " << val_l << " % " << val_r);
                                                MIR_ASSERT(state, re.v != 0, "Const eval error: Constant division by zero");
                                                new_value = ::MIR::Constant::make_Uint({H::truncate_u(le.t, le.v % re.v), le.t});
                                            }
                                }
                                break;

                            case ::MIR::eBinOp::BIT_AND:
                                MIR_ASSERT(state, val_l.tag() == val_r.tag(), "Mismatched types for eBinOp::BIT_AND - " << val_l << " & " << val_r);
                                TU_MATCH_HDRA( (val_l, val_r), {)
                                default:
                                    break;
                                            TU_ARMA(Bool, le, re) {
                                                new_value = ::MIR::Constant::make_Bool({le.v && re.v});
                                            }
                                            TU_ARMA(Int, le, re) {
                                                MIR_ASSERT(state, le.t == re.t, "Mismatched types for eBinOp::BIT_AND - " << val_l << " ^ " << val_r);
                                                new_value = ::MIR::Constant::make_Int({H::truncate_s(le.t, le.v & re.v), le.t});
                                            }
                                            TU_ARMA(Uint, le, re) {
                                                MIR_ASSERT(state, le.t == re.t, "Mismatched types for eBinOp::BIT_AND - " << val_l << " ^ " << val_r);
                                                new_value = ::MIR::Constant::make_Uint({H::truncate_u(le.t, le.v & re.v), le.t});
                                            }
                                }
                                break;
                            case ::MIR::eBinOp::BIT_OR:
                                MIR_ASSERT(state, val_l.tag() == val_r.tag(), "Mismatched types for eBinOp::BIT_OR - " << val_l << " | " << val_r);
                                TU_MATCH_HDRA( (val_l, val_r), {)
                                default:
                                    break;
                                            TU_ARMA(Bool, le, re) {
                                                new_value = ::MIR::Constant::make_Bool({le.v || re.v});
                                            }
                                            TU_ARMA(Int, le, re) {
                                                MIR_ASSERT(state, le.t == re.t, "Mismatched types for eBinOp::BIT_OR - " << val_l << " | " << val_r);
                                                new_value = ::MIR::Constant::make_Int({H::truncate_s(le.t, le.v | re.v), le.t});
                                            }
                                            TU_ARMA(Uint, le, re) {
                                                MIR_ASSERT(state, le.t == re.t, "Mismatched types for eBinOp::BIT_OR - " << val_l << " | " << val_r);
                                                new_value = ::MIR::Constant::make_Uint({H::truncate_u(le.t, le.v | re.v), le.t});
                                            }
                                }
                                break;
                            case ::MIR::eBinOp::BIT_XOR:
                                MIR_ASSERT(state, val_l.tag() == val_r.tag(), "Mismatched types for eBinOp::BIT_XOR - " << val_l << " ^ " << val_r);
                                TU_MATCH_HDRA( (val_l, val_r), {)
                                default:
                                    break;
                                            TU_ARMA(Bool, le, re) {
                                                new_value = ::MIR::Constant::make_Bool({le.v != re.v});
                                            }
                                            TU_ARMA(Int, le, re) {
                                                MIR_ASSERT(state, le.t == re.t, "Mismatched types for eBinOp::BIT_XOR - " << val_l << " ^ " << val_r);
                                                new_value = ::MIR::Constant::make_Int({H::truncate_s(le.t, le.v ^ re.v), le.t});
                                            }
                                            TU_ARMA(Uint, le, re) {
                                                MIR_ASSERT(state, le.t == re.t, "Mismatched types for eBinOp::BIT_XOR - " << val_l << " ^ " << val_r);
                                                new_value = ::MIR::Constant::make_Uint({H::truncate_u(le.t, le.v ^ re.v), le.t});
                                            }
                                }
                                break;

                            // --- Bit Shifts ---
                            case ::MIR::eBinOp::BIT_SHL: {
                                            U128 shift_len_r;
                                TU_MATCH_HDRA( (val_r), {)
                                default:
                                    MIR_BUG(state, "Mismatched types for eBinOp::BIT_SHL - " << val_l << " >> " << val_r);
                                                break;
                                                TU_ARMA(Int, re) {
                                                    shift_len_r = re.v.get_inner();
                                                }
                                                TU_ARMA(Uint, re) {
                                                    shift_len_r = re.v;
                                                }
                                }
                                MIR_ASSERT(state, shift_len_r <= 128, "Const eval error: Over-sized eBinOp::BIT_SHL - " << val_l << " << " << val_r);
                                auto shift_len = shift_len_r.truncate_u64();
                                TU_MATCH_HDRA( (val_l), {)
                                default:
                                    break;
                                                TU_ARMA(Int, le) {
                                                    new_value = ::MIR::Constant::make_Int({H::truncate_s(le.t, le.v << shift_len), le.t});
                                                }
                                                TU_ARMA(Uint, le) {
                                                    new_value = ::MIR::Constant::make_Uint({H::truncate_u(le.t, le.v << shift_len), le.t});
                                                }
                                }
                                } break;
                            case ::MIR::eBinOp::BIT_SHR:{
                                            U128 shift_len_r;
                                TU_MATCH_HDRA( (val_r), {)
                                default:
                                    MIR_BUG(state, "Mismatched types for eBinOp::BIT_SHR - " << val_l << " >> " << val_r);
                                                break;
                                                TU_ARMA(Int, re) {
                                                    shift_len_r = re.v.get_inner();
                                                }
                                                TU_ARMA(Uint, re) {
                                                    shift_len_r = re.v;
                                                }
                                }
                                MIR_ASSERT(state, shift_len_r <= 128, "Const eval error: Over-sized shift - " << val_l << " >> " << val_r);
                                auto shift_len = shift_len_r.truncate_u64();
                                TU_MATCH_HDRA( (val_l), {)
                                default:
                                    break;
                                                TU_ARMA(Int, le) {
                                                    new_value = ::MIR::Constant::make_Int({H::truncate_s(le.t, le.v >> shift_len), le.t});
                                                }
                                                TU_ARMA(Uint, le) {
                                                    new_value = ::MIR::Constant::make_Uint({H::truncate_u(le.t, le.v >> shift_len), le.t});
                                                }
                                }
                                } break;
                            // TODO: Other binary operations
                            // Could emit a TODO?
                            default:
                                break;
                                }

                                if (new_value != ::MIR::Constant()) {
                                    DEBUG(state << " " << e->src << " = " << new_value);
                                    e->src = mv$(new_value);
                                    changed = true;
                                }
                            }
                        } else {
                            ::MIR::Param new_value;
                            // No-ops
                            switch (se.op) {
                                // `foo + 0 == foo`
                                // `foo - 0 == foo`
                                case ::MIR::eBinOp::ADD:
                                case ::MIR::eBinOp::SUB:
                                    if (se.val_r.is_Constant() && se.val_r.as_Constant().is_Uint() && se.val_r.as_Constant().as_Uint().v == 0) {
                                        new_value = mv$(se.val_l);
                                    }
                                    break;
                                // `foo % 1 == 0`
                                case ::MIR::eBinOp::MOD:
                                    if (se.val_r.is_Constant() && se.val_r.as_Constant().is_Uint() && se.val_r.as_Constant().as_Uint().v == 1) {
                                        new_value = ::MIR::Constant::make_Uint({U128(0), se.val_r.as_Constant().as_Uint().t});
                                    }
                                    break;
                                // `foo / 1 == foo`
                                case ::MIR::eBinOp::DIV:
                                    if (se.val_r.is_Constant() && se.val_r.as_Constant().is_Uint() && se.val_r.as_Constant().as_Uint().v == 1) {
                                        new_value = mv$(se.val_l);
                                    }
                                    break;
                                // `foo * 0 == 0`
                                // `foo * 1 == foo`
                                // `0 * foo == 0`
                                // `1 * foo == foo`
                                case ::MIR::eBinOp::MUL:
                                    if (se.val_r.is_Constant() && se.val_r.as_Constant().is_Uint()) {
                                        auto& v = se.val_r.as_Constant().as_Uint();
                                        if (v.v == 0) {
                                            new_value = ::MIR::Constant::make_Uint({U128(0), v.t});
                                        } else if (v.v == 1) {
                                            new_value = mv$(se.val_l);
                                        } else {
                                        }
                                    }
                                    if (se.val_l.is_Constant() && se.val_l.as_Constant().is_Uint()) {
                                        auto& v = se.val_l.as_Constant().as_Uint();
                                        if (v.v == 0) {
                                            new_value = ::MIR::Constant::make_Uint({U128(0), v.t});
                                        } else if (v.v == 1) {
                                            new_value = mv$(se.val_r);
                                        } else {
                                        }
                                    }
                                    break;
                                default:
                                    break;
                            }
                            if (new_value != ::MIR::Param()) {
                                DEBUG(state << " " << e->src << " = " << new_value);
                            TU_MATCH_HDRA( (new_value), {)
                            TU_ARMA(LValue, v)   e->src = mv$(v);
                                    TU_ARMA(Borrow, _) throw "";
                                    TU_ARMA(Constant, v) e->src = mv$(v);
                            }
                            changed = true;
                            }
                        }
                    }
                    TU_ARMA(UniOp, se) {
                        auto it = known_values.find(se.val);
                        if (it != known_values.end()) {
                            const auto& val = it->second;
                            ::MIR::Constant new_value;
                            bool replace = false;
                            switch (se.op) {
                                case ::MIR::eUniOp::INV:
                            TU_MATCH_HDRA( (val), {)
                            TU_ARMA(Uint, ve) {
                                            auto val = ve.v;
                                            replace = true;
                                            switch (ve.t) {
                                                case ::HIR::CoreType::U8:
                                                case ::HIR::CoreType::U16:
                                                case ::HIR::CoreType::U32:
                                                case ::HIR::CoreType::Usize:
                                                case ::HIR::CoreType::U64:
                                                    val = H::truncate_u(ve.t, ~val);
                                                    break;
                                                case ::HIR::CoreType::U128:
                                                    replace = false;
                                                    break;
                                                case ::HIR::CoreType::Char:
                                                    MIR_BUG(state, "Invalid use of ! on char");
                                                    break;
                                                default:
                                                    // Invalid type for Uint literal
                                                    replace = false;
                                                    break;
                                            }
                                            new_value = ::MIR::Constant::make_Uint({val, ve.t});
                                        }
                                        TU_ARMA(Int, ve) {
                                            // ! is valid on Int, it inverts bits the same way as an uint
                                            auto val = ve.v;
                                            switch (ve.t) {
                                                case ::HIR::CoreType::I8:
                                                case ::HIR::CoreType::I16:
                                                case ::HIR::CoreType::I32:
                                                case ::HIR::CoreType::Isize:
                                                case ::HIR::CoreType::I64:
                                                    val = H::truncate_s(ve.t, ~val);
                                                    replace = true;
                                                    break;
                                                case ::HIR::CoreType::I128:
                                                    // TODO: Are there any cases where sign extension stops being correct here?
                                                    val = H::truncate_s(ve.t, ~val);
                                                    replace = true;
                                                    break;
                                                case ::HIR::CoreType::Char:
                                                    MIR_BUG(state, "Invalid use of ! on char");
                                                    break;
                                                default:
                                                    // Invalid type for Uint literal
                                                    replace = false;
                                                    break;
                                            }
                                            new_value = ::MIR::Constant::make_Int({val, ve.t});
                                        }
                                        TU_ARMA(Float, ve) {
                                            // Not valid?
                                        }
                                        TU_ARMA(Bool, ve) {
                                            new_value = ::MIR::Constant::make_Bool({!ve.v});
                                            replace = true;
                                        }
                                        TU_ARMA(Bytes, ve) {
                                        }
                                        TU_ARMA(StaticString, ve) {
                                        }
                                        TU_ARMA(Const, ve) {
                                            // TODO:
                                        }
                                        TU_ARMA(Generic, ve) {
                                        }
                                        TU_ARMA(Function, ve) {
                                        }
                                        TU_ARMA(ItemAddr, ve) {
                                        }
                            }
                            break;
                        case ::MIR::eUniOp::NEG:
                            TU_MATCHA( (val), (ve),
                            (Uint,
                                // Not valid?
                                ),
                            (Int,
                                new_value = ::MIR::Constant::make_Int({ -ve.v, ve.t });
                                replace = true;
                                ),
                            (Float,
                                if (!float_value_is_nan(ve.v)) {
                                        new_value = ::MIR::Constant::make_Float({-ve.v, ve.t});
                                        replace = true;
                                }
                                ),
                            (Bool,
                                // Not valid?
                                ),
                            (Bytes, ),
                            (StaticString, ),
                            (Const,
                                // TODO:
                                ),
                            (Generic,  ),
                            (Function, ),
                            (ItemAddr, )
                            )
                            break;
                            }
                            if (replace) {
                                DEBUG(state << " " << e->src << " = " << new_value);
                                e->src = mv$(new_value);
                                changed = true;
                            }
                        }
                    }
                    TU_ARMA(DstMeta, se) {
                    }
                    TU_ARMA(DstPtr, se) {
                    }
                    TU_ARMA(MakeDst, se) {
                        // NOTE: This disables any checks if the metadata isn't populated.
                        // This avoids issues with cleanup when optimise is run first
                        if (TU_TEST2(se.meta_val, Constant, , ItemAddr, .get() == nullptr)) {
                        } else {
                            check_param(se.ptr_val);
                            check_param(se.meta_val);
                        }
                    }
                    TU_ARMA(Tuple, se) {
                        for (auto& p : se.vals) {
                            check_param(p);
                        }
                    }
                    TU_ARMA(Array, se) {
                        for (auto& p : se.vals) {
                            check_param(p);
                        }
                    }
                    TU_ARMA(UnionVariant, se) {
                        check_param(se.val);
                    }
                    TU_ARMA(EnumVariant, se) {
                        for (auto& p : se.vals) {
                            check_param(p);
                        }
                    }
                    TU_ARMA(Struct, se) {
                        for (auto& p : se.vals) {
                            check_param(p);
                        }
                    }
                }
            } else if (const auto* se = stmt.opt_SetDropFlag()) {
                if (se->other == ~0u) {
                    known_drop_flags[se->idx] = se->new_val;
                } else {
                    auto it = known_drop_flags.find(se->other);
                    if (it != known_drop_flags.end()) {
                        known_drop_flags[se->idx] = se->new_val != it->second;
                    }
                }
            } else if (auto* se = stmt.opt_Drop()) {
                if (se->flag_idx != ~0u) {
                    auto it = known_drop_flags.find(se->flag_idx);
                    if (it != known_drop_flags.end()) {
                        if (it->second) {
                            se->flag_idx = ~0u;
                        } else {
                            // TODO: Delete drop
                            stmt = ::MIR::Statement::make_ScopeEnd({});
                        }
                    }
                }
            }
            // - If a known temporary is borrowed mutably or mutated somehow, clear its knowledge
            visit_mir_lvalues(stmt, [&known_values, &known_values_var](const ::MIR::LValue& lv, ValUsage vu) -> bool {
                if (vu == ValUsage::Write) {
                    known_values.erase(lv);
                    known_values_var.erase(lv);
                }
                return false;
            });
            // - Locate `temp = SOME_CONST` and record value
            if (const auto* e = stmt.opt_Assign()) {
                if (e->dst.is_Local()) {
                    // Known constant
                    if (const auto* ce = e->src.opt_Constant()) {
                        known_values.insert(::std::make_pair(e->dst.clone(), ce->clone()));
                        DEBUG(state << stmt);
                    }
                    // Known variant
                    else if (const auto* ce = e->src.opt_EnumVariant()) {
                        known_values_var.insert(::std::make_pair(e->dst.clone(), ce->index));
                        DEBUG(state << stmt);
                    }
                    // Propagate knowledge through Local=Local assignments
                    else if (const auto* ce = e->src.opt_Use()) {
                        if (ce->is_Local()) {
                            auto it1 = known_values.find(*ce);
                            auto it2 = known_values_var.find(*ce);
                            assert(!(it1 != known_values.end() && it2 != known_values_var.end()));
                            if (it1 != known_values.end()) {
                                known_values.insert(::std::make_pair(e->dst.clone(), it1->second.clone()));
                                DEBUG(state << stmt);
                            } else if (it2 != known_values_var.end()) {
                                known_values_var.insert(::std::make_pair(e->dst.clone(), it2->second));
                                DEBUG(state << stmt);
                            } else {
                                // Neither known, don't propagate
                            }
                        }
                    } else {
                        // No need to clear, the visit above this if block handles it.
                    }
                }
            }
        }

        state.set_cur_stmt_term(bbidx);
        visit_mir_lvalues_mut(bb.terminator, edit_lval);
        switch (bb.terminator.tag()) {
            case ::MIR::Terminator::TAGDEAD:
                throw "";
                TU_ARM(bb.terminator, Switch, te) {
                    auto it = known_values_var.find(te.val);
                    if (it != known_values_var.end()) {
                        MIR_ASSERT(state, it->second < te.targets.size(), "Terminator::Switch with known variant index out of bounds" << " (#" << it->second << " with " << bb.terminator << ")");
                        auto new_bb = te.targets.at(it->second);
                        DEBUG(state << "Convert " << bb.terminator << " into Goto(" << new_bb << ") because variant known to be #" << it->second);
                        bb.terminator = ::MIR::Terminator::make_Goto(new_bb);

                        changed = true;
                    }
                }
                break;
                TU_ARM(bb.terminator, If, te) {
                    auto it = known_values.find(te.cond);
                    if (it != known_values.end()) {
                        if (it->second.is_Const() || it->second.is_Generic()) {
                        } else {
                            MIR_ASSERT(state, it->second.is_Bool(), "Terminator::If with known value not Bool - " << it->second);
                            auto new_bb = (it->second.as_Bool().v ? te.bb_true : te.bb_false);
                            DEBUG(state << "Convert " << bb.terminator << " into Goto(" << new_bb << ") because condition known to be " << it->second);
                            bb.terminator = ::MIR::Terminator::make_Goto(new_bb);

                            changed = true;
                        }
                    }
                }
                break;
                TU_ARM(bb.terminator, Call, te) {
                    for (auto& a : te.args) {
                        check_param(a);
                    }
                }
                break;
            default:
                break;
        }
    }

    // - Remove based on known booleans within a single block
    //  > Eliminates `if false`/`if true` branches
    // TODO: Is this now defunct after the handling of Terminator::If above?
    for (auto& bb : fcn.blocks) {
        auto bbidx = &bb - &fcn.blocks.front();
        if (!bb.terminator.is_If()) {
            continue;
        }
        const auto& te = bb.terminator.as_If();

        // Restrict condition to being a temporary/variable
        if (te.cond.is_Local())
            ;
        else {
            continue;
        }

        auto has_cond = [&](const auto& lv, auto ut) -> bool {
            return lv == te.cond;
        };
        bool val_known = false;
        bool known_val;
        for (unsigned int i = bb.statements.size(); i--;) {
            if (bb.statements[i].is_Assign()) {
                const auto& se = bb.statements[i].as_Assign();
                // If the condition was mentioned, don't assume it has the same value
                // TODO: What if the condition is a field/index and something else is edited?
                if (visit_mir_lvalues(se.src, has_cond)) {
                    break;
                }

                if (se.dst != te.cond) {
                    continue;
                }
                if (se.src.is_Constant() && se.src.as_Constant().is_Bool()) {
                    val_known = true;
                    known_val = se.src.as_Constant().as_Bool().v;
                } else {
                    val_known = false;
                }
                break;
            } else {
                if (visit_mir_lvalues(bb.statements[i], has_cond)) {
                    break;
                }
            }
        }
        if (val_known) {
            DEBUG("bb" << bbidx << ": Condition known to be " << known_val);
            bb.terminator = ::MIR::Terminator::make_Goto(known_val ? te.bb_true : te.bb_false);
            changed = true;
        }
    }

    return changed;
}

// --------------------------------------------------------------------
// Split aggregated values that are never used by outer value into inner values
// --------------------------------------------------------------------
// NOTE: This is a generalised version of the old de-tuple pass (and fills part of MIR_Optimise_PropagateKnownValues)
//
// NOTE: This has a special case rule that disallowes borrows of the first field: Sometimes a borrow of the first
//       field is used as a proxy for the entire struct.
bool MIR_Optimise_SplitAggregates(::MIR::TypeResolve& state, ::MIR::Function& fcn) {
    bool changed = false;
    TRACE_FUNCTION_FR("", changed);

    // Find locals that are:
    // - Assigned once
    // - From a constructor
    // - And only ever used via a field access
    // Replace the construction with assignments of `n` locals instead (which can be optimised by further passes)

    struct Potential {
        size_t src_bb_idx;
        size_t src_stmt_idx;
        unsigned variant_idx;

        bool is_direct_used;
        unsigned n_write;
        std::vector<unsigned> replacements;

        Potential(size_t src_bb_idx, size_t src_stmt_idx, unsigned variant_idx = ~0u)
            : src_bb_idx(src_bb_idx)
            , src_stmt_idx(src_stmt_idx)
            , variant_idx(variant_idx)
            , is_direct_used(false)
            , n_write(0)
        {
        }
    };

    std::map<unsigned, Potential> potentials;

    // 1. Find locals created from constructors (struct/tuple)
    for (const auto& block : fcn.blocks) {
        size_t bb_idx = &block - &fcn.blocks.front();
        for (size_t i = 0; i < block.statements.size(); i++) {
            const auto& stmt = block.statements[i];
            if (const auto* se = stmt.opt_Assign()) {
                if (!se->dst.is_Local()) {
                    continue;
                }

                if (auto* sse = se->src.opt_Struct()) {
                    if (sse->vals.size() == 0) {
                        continue;
                    }
                } else if (auto* sse = se->src.opt_Tuple()) {
                    if (sse->vals.size() == 0) {
                        continue;
                    }
                }
                // NOTE: Arrays are eligable (as long as they're only accessed using field operator
                else if (auto* sse = se->src.opt_Array()) {
                    if (sse->vals.size() == 0) {
                        continue;
                    }
                }
                // Variants are allowed, they store the variant index for later checking
                else if (auto* sse = se->src.opt_EnumVariant()) {
                    if (sse->vals.size() == 0) {
                        continue;
                    }
                    DEBUG("> BB" << bb_idx << "/" << i << ": POSSIBLE " << stmt);
                    potentials.insert(std::make_pair(se->dst.as_Local(), Potential(bb_idx, i, sse->index)));
                    continue;
                }
                // NOTE: Union variants need special handling in the replacement
                else {
                    continue;
                }

                // Found a potential.
                DEBUG("> BB" << bb_idx << "/" << i << ": POSSIBLE " << stmt);
                potentials.insert(std::make_pair(se->dst.as_Local(), Potential(bb_idx, i)));
            }
        }
    }
    // - Nothing to do? return early
    if (potentials.empty()) {
        return false;
    }

    // 2. Check how the variables are used (allow one write, and no other direct usage)
    // - Removes any potentials that are invalidated.
    visit_mir_lvalues(state, fcn, [&](const MIR::LValue& lv, ValUsage vu) -> bool {
        if (lv.m_root.is_Local()) {
            // Is this one of the potentials?
            auto it = potentials.find(lv.m_root.as_Local());
            if (it != potentials.end()) {
                if (lv.m_wrappers.empty()) {
                    // NOTE: A single write is allowed (the assignment)
                    // - Any other would be a re-assignent or a drop
                    if (vu == ValUsage::Write) {
                        it->second.n_write += 1;
                    } else {
                        // Direct usage!
                        it->second.is_direct_used = true;
                    }
                } else if (lv.m_wrappers.front().is_Field()) {
                    // Field acess: allowed UNLESS it's a borrow of the first field
                    // TODO: Find out what code makes the assumption that `&foo.0` is a good stand-in for `&foo`
                    if (lv.m_wrappers.front().as_Field() == 0 && vu == ValUsage::Borrow) {
                        it->second.is_direct_used = true;
                    }
                } else if (lv.m_wrappers.front().is_Downcast()) {
                    // Downcast to a variant other than the variant it was constructed as, don't do anything.
                    // - For enums, this is an error (but here we don't know for sure). For unions it's valid behaviour
                    // A bare downcast uses the complete variant payload, so it cannot be replaced with a field local.
                    if (lv.m_wrappers.front().as_Downcast() != it->second.variant_idx || lv.m_wrappers.size() < 2 || !lv.m_wrappers[1].is_Field()) {
                        it->second.is_direct_used = true;
                    }
                } else {
                    // Index and deref are disallowed
                    it->second.is_direct_used = true;
                }

                // If invalidated, delete.
                if (it->second.is_direct_used || it->second.n_write > 1) {
                    const auto& stmt = fcn.blocks[it->second.src_bb_idx].statements[it->second.src_stmt_idx];
                    DEBUG(state << ": REMOVE BB" << it->second.src_bb_idx << "/" << it->second.src_stmt_idx << " " << stmt << " from " << lv /*<< " vu=" << vu*/);
                    potentials.erase(it);
                }
            }
        }
        return true;
    });
    // - All potentials removed? Return early
    if (potentials.empty()) {
        return false;
    }

    // 3. Explode sources into locals
    // NOTE: This needs to handle movement of indexes
    for (auto& p : potentials) {
        auto bb_idx = p.second.src_bb_idx;
        auto stmt_idx = p.second.src_stmt_idx;
        state.set_cur_stmt(bb_idx, stmt_idx);
        auto& block = fcn.blocks[bb_idx];

        DEBUG("- BB" << bb_idx << "/" << stmt_idx << ": " << block.statements[stmt_idx]);
        // Extract the list of values from the existing statement
        std::vector<MIR::Param> vals;
        {
            auto& src = block.statements[stmt_idx].as_Assign().src;
            if (auto* se = src.opt_Struct()) {
                vals = std::move(se->vals);
            } else if (auto* se = src.opt_Tuple()) {
                vals = std::move(se->vals);
            } else if (auto* se = src.opt_Array()) {
                vals = std::move(se->vals);
            } else if (auto* se = src.opt_EnumVariant()) {
                vals = std::move(se->vals);
            } else if (auto* se = src.opt_UnionVariant()) {
                vals.push_back(mv$(se->val));
            } else {
                MIR_BUG(state, "Unexpected rvalue type in SplitAggregates - " << src);
            }
        }
        MIR_ASSERT(state, vals.size() > 0, "Optimisation can't apply to empty lists");
        auto offset = vals.size() - 1;

        //for(size_t i = 0; i < block.statements.size(); i ++)
        //    DEBUG("> BB" << bb_idx << "/" << i << ": " << block.statements[i]);

        // Insert new statements as required
        if (offset > 0) {
            block.statements.resize(block.statements.size() + offset);
            // Move all elements [stmt_idx+1 .. ] up by `offset`
            // NOTE: move_backward's third argument is 'past-the-end'
            std::move_backward(block.statements.begin() + stmt_idx + 1, block.statements.end() - offset, block.statements.end());
        }

        // Create new statements (allocating new locals)
        auto new_local_base = fcn.locals.size();
        fcn.locals.resize(fcn.locals.size() + vals.size());
        p.second.replacements.resize(vals.size());
        for (size_t i = 0; i < vals.size(); i++) {
            // Allocate a new local
            auto new_local = static_cast<unsigned>(new_local_base + i);
            ::HIR::TypeRef tmp;
            fcn.locals[new_local] = state.get_param_type(tmp, vals[i]);
            p.second.replacements[i] = new_local;
            // Set the relevant statement to be an assignment to that new local
            block.statements[stmt_idx + i] = MIR::Statement::make_Assign({MIR::LValue::new_Local(new_local), param_to_rvalue(mv$(vals[i]))});
            DEBUG("+ BB" << bb_idx << "/" << (stmt_idx + i) << ": " << block.statements[stmt_idx + i]);
        }

        //for(size_t i = 0; i < block.statements.size(); i ++)
        //    DEBUG("> BB" << bb_idx << "/" << i << ": " << block.statements[i]);

        // If this replacement changed the number of statements in this block, update all existing references.
        if (offset > 0) {
            for (auto& other_p : potentials) {
                if (other_p.second.src_bb_idx == bb_idx && other_p.second.src_stmt_idx > stmt_idx) {
                    other_p.second.src_stmt_idx += offset;
                }
            }
        }
    }

    // 4. Replace all usages
    visit_mir_lvalues_mut(state, fcn, [&](MIR::LValue& lv, ValUsage vu) -> bool {
        if (lv.m_root.is_Local()) {
            // Is this one of the potentials?
            auto it = potentials.find(lv.m_root.as_Local());
            if (it != potentials.end()) {
                size_t ndel;
                size_t field_idx;
                if (it->second.variant_idx == ~0u) {
                    field_idx = lv.m_wrappers.front().as_Field();
                    ndel = 1;
                } else {
                    MIR_ASSERT(state, lv.m_wrappers[0].is_Downcast(), lv);
                    MIR_ASSERT(state, lv.m_wrappers[1].is_Field(), lv);
                    field_idx = lv.m_wrappers[1].as_Field();
                    ndel = 2;
                }
                auto new_wrappers = std::vector<MIR::LValue::Wrapper>(lv.m_wrappers.begin() + ndel, lv.m_wrappers.end());
                auto new_root = MIR::LValue::Storage::new_Local(it->second.replacements.at(field_idx));
                auto new_lv = MIR::LValue(mv$(new_root), mv$(new_wrappers));
                DEBUG(state << " " << lv << " -> " << new_lv);
                lv = mv$(new_lv);
            }
        }
        return true;
    });

    // If we reach this point, a replacement was done.
    changed = true;
    return true;
}

// --------------------------------------------------------------------
// Replace `tmp = RValue::Use()` where the temp is only used once
// --------------------------------------------------------------------
bool MIR_Optimise_PropagateSingleAssignments(::MIR::TypeResolve& state, ::MIR::Function& fcn) {
    bool replacement_happend;
    TRACE_FUNCTION_FR("", replacement_happend);

    // TODO: This requires kowing that doing so has no effect.
    // - Can use little heristics like a Call pointing to an assignment of its RV
    // - Count the read/write count of a variable, if it's 1,1 then this optimisation is correct.
    // - If the count is read=*,write=1 and the write is of an argument, replace with the argument.
    struct ValUse {
        unsigned int read = 0;
        unsigned int write = 0;
        unsigned int borrow = 0;
    };

    struct {
        ::std::vector<ValUse> local_uses;

        void use_lvalue(const ::MIR::LValue& lv, ValUsage ut) {
            for (const auto& w : lv.m_wrappers) {
                if (w.is_Index()) {
                    //local_uses[w.as_Index()].read += 1;
                    local_uses[w.as_Index()].borrow += 1;
                }
            }
            if (lv.m_root.is_Local()) {
                auto& vu = local_uses[lv.m_root.as_Local()];
                switch (ut) {
                    case ValUsage::Move:
                    case ValUsage::Read:
                        vu.read += 1;
                        break;
                    case ValUsage::Write:
                        vu.write += 1;
                        break;
                    case ValUsage::Borrow:
                        vu.borrow += 1;
                        break;
                }
            }
        }
    } val_uses = {::std::vector<ValUse>(fcn.locals.size())};

    visit_mir_lvalues(state, fcn, [&](const auto& lv, auto ut) {
        val_uses.use_lvalue(lv, ut);
        return false;
    });

    // --- Eliminate `tmp = Use(...)` (moves lvalues downwards)
    // > Find an assignment `tmp = Use(...)` where the temporary is only written and read once
    // > Locate the usage of this temporary
    //  - Stop on any conditional terminator
    // > Any lvalues in the source lvalue must not be mutated between the source assignment and the usage.
    //  - This includes mutation, borrowing, or moving.
    // > Replace usage with the inner of the original `Use`
    {
        // 1. Assignments (forward propagate)
        //::std::map< ::MIR::LValue::CRef, ::MIR::RValue>    replacements;
        ::std::vector<::std::pair<::MIR::LValue, ::MIR::RValue>> replacements;
        auto replacements_find = [&replacements](const ::MIR::LValue::CRef& lv) {
            return ::std::find_if(replacements.begin(), replacements.end(), [&](const auto& e) {
                return lv == e.first;
            });
        };
        for (const auto& block : fcn.blocks) {
            if (block.terminator.tag() == ::MIR::Terminator::TAGDEAD) {
                continue;
            }

            for (unsigned int stmt_idx = 0; stmt_idx < block.statements.size(); stmt_idx++) {
                state.set_cur_stmt(&block - &fcn.blocks.front(), stmt_idx);
                const auto& stmt = block.statements[stmt_idx];
                DEBUG(state << stmt);
                // > Assignment
                if (!stmt.is_Assign()) {
                    continue;
                }
                const auto& e = stmt.as_Assign();
                // > Of a temporary from with a RValue::Use
                if (e.dst.is_Local()) {
                    const auto& vu = val_uses.local_uses[e.dst.as_Local()];
                    DEBUG(" - VU " << e.dst << " R:" << vu.read << " W:" << vu.write << " B:" << vu.borrow);
                    // TODO: Allow write many?
                    // > Where the variable is written once and read once
                    if (!(vu.read == 1 && vu.write == 1 && vu.borrow == 0)) {
                        DEBUG("> Not a single read+write");
                        continue;
                    }
                } else {
                    continue;
                }
                bool only_one = false;
                if (e.src.is_Use()) {
                    // Keep the complexity down
                    const auto* srcp = &e.src.as_Use();
                    // If there are deref/index accesses, then only allow one statement
                    // - This is the lazy option, avoids needing to check for invalidation (could be a write through deref)
                    if (::std::any_of(srcp->m_wrappers.begin(), srcp->m_wrappers.end(), [](auto& w) {
                        return !w.is_Field() && !w.is_Downcast();
                    })) {
                        DEBUG("Non-field access");
                        only_one = true;
                        continue;
                    }
                    // TODO: Why is this limited to locals only?
                    if (!srcp->m_root.is_Local()) {
                        DEBUG("> Can't replace, not a local root");
                        continue;
                    }

                    if (replacements_find(*srcp) != replacements.end()) {
                        DEBUG("> Can't replace, source has pending replacement");
                        continue;
                    }
                } else {
                    // Not a use
                    continue;
                }
                bool src_is_lvalue = e.src.is_Use();
                DEBUG("- Locate usage");

                auto is_lvalue_usage = [&](const auto& lv, auto) {
                    return lv.m_root == e.dst.m_root;
                    //return lv == e.dst;
                };

                // Eligable for replacement
                // Find where this value is used
                // - Stop on a conditional block terminator
                // - Stop if any value mentioned in the source is mutated/invalidated
                bool stop = false;
                bool found = false;
                for (unsigned int si2 = stmt_idx + 1; si2 < block.statements.size(); si2++) {
                    state.set_cur_stmt(&block - &fcn.blocks.front(), si2);
                    const auto& stmt2 = block.statements[si2];
                    DEBUG(state << "[find usage] " << stmt2);

                    // Check for invalidation (done first, to avoid cases where the source is moved into a struct)
                    if (check_invalidates_lvalue(stmt2, e.src.as_Use(), false)) {
                        stop = true;
                        DEBUG("Source invalidated");
                        break;
                    }

                    // Usage found.
                    if (visit_mir_lvalues(stmt2, is_lvalue_usage)) {
                        // If the source isn't a Use, ensure that this is a Use
                        if (!src_is_lvalue) {
                            if (stmt2.is_Assign() && stmt2.as_Assign().src.is_Use()) {
                                // Good
                            } else {
                                // Bad, this has to stay a temporary
                                stop = true;
                                break;
                            }
                        }
                        found = true;
                        stop = true;
                        break;
                    }

                    if (only_one) {
                        stop = true;
                    }
                }
                if (!stop) {
                    if (check_invalidates_lvalue(block.terminator, e.src.as_Use(), false)) {
                        stop = true;
                        DEBUG("Source invalidated in terminator");
                    }
                }
                if (!stop) {
                    state.set_cur_stmt_term(&block - &fcn.blocks.front());
                    DEBUG(state << "[find usage] " << block.terminator);
                    if (src_is_lvalue) {
                        visit_mir_lvalues(block.terminator, [&](const auto& lv, auto vu) {
                            found |= is_lvalue_usage(lv, vu);
                            return found;
                        });
                    }
                    TU_MATCHA((block.terminator), (e), (Incomplete, ), (Return, ), (Diverge, ), (Goto, DEBUG("TODO: Chain");), (Panic, ), (If, stop = true;), (Switch, stop = true;), (SwitchValue, stop = true;), (Call, stop = true;))
                }
                // Schedule a replacement in a future pass
                if (found) {
                    DEBUG("> Schedule replace " << e.dst << " with " << e.src.as_Use());
                    replacements.push_back(::std::make_pair(e.dst.clone(), e.src.clone()));
                } else {
                    DEBUG("- Single-write/read " << e.dst << " not replaced - couldn't find usage");
                }
            } // for(stmt : block.statements)
        }

        DEBUG("replacements = " << replacements);

        // Apply replacements within replacements
        for (;;) {
            unsigned int inner_replaced_count = 0;
            for (auto& r : replacements) {
                visit_mir_lvalues_mut(r.second, [&](::MIR::LValue& lv, auto vu) {
                    if (vu == ValUsage::Read || vu == ValUsage::Move) {
                        visit_mir_lvalue_mut(lv, vu, [&](::MIR::LValue::MRef& lvr, auto vu) {
                            auto it = replacements_find(lvr);
                            if (it != replacements.end() && it->second.is_Use()) {
                                lvr.replace(it->second.as_Use().clone());
                                inner_replaced_count++;
                            }
                            return false;
                        });
                    }
                    return false;
                });
            }
            if (inner_replaced_count == 0) {
                break;
            }
        }
        DEBUG("replacements = " << replacements);

        // Apply replacements
        unsigned int replaced = 0;
        while (replaced < replacements.size()) {
            auto old_replaced = replaced;
            auto cb = [&](::MIR::LValue& lv, auto vu) {
                return visit_mir_lvalue_mut(lv, vu, [&](::MIR::LValue::MRef& lv, auto vu) {
                    if (vu == ValUsage::Read || vu == ValUsage::Move) {
                        auto it = replacements_find(lv);
                        if (it != replacements.end()) {
                            MIR_ASSERT(state, it->second.tag() != ::MIR::RValue::TAGDEAD, "Replacement of  " << lv << " fired twice");
                            MIR_ASSERT(state, it->second.is_Use(), "Replacing a lvalue with a rvalue - " << lv << " with " << it->second);
                            auto rval = ::std::move(it->second);
                            DEBUG("> Do replace " << lv << " => " << rval);
                            lv.replace(::std::move(rval.as_Use()));
                            replaced += 1;
                        }
                    }
                    return false;
                });
            };
            for (unsigned int block_idx = 0; block_idx < fcn.blocks.size(); block_idx++) {
                auto& block = fcn.blocks[block_idx];
                if (block.terminator.tag() == ::MIR::Terminator::TAGDEAD) {
                    continue;
                }
                for (auto& stmt : block.statements) {
                    state.set_cur_stmt(block_idx, (&stmt - &block.statements.front()));
                    DEBUG(state << stmt);
                    {
                        visit_mir_lvalues_mut(stmt, cb);
                    }
                }
                state.set_cur_stmt_term(block_idx);
                visit_mir_lvalues_mut(block.terminator, cb);
            }
            MIR_ASSERT(state, replaced > old_replaced, "Temporary eliminations didn't advance");
        }
        // Remove assignments of replaced values
        for (auto& block : fcn.blocks) {
            for (auto it = block.statements.begin(); it != block.statements.end();) {
                state.set_cur_stmt(&block - &fcn.blocks.front(), (it - block.statements.begin()));
                // If the statement was an assign of a replaced temporary, remove it.
                auto it2 = replacements.end();
                if (it->is_Assign() && (it2 = replacements_find(it->as_Assign().dst)) != replacements.end()) {
                    DEBUG(state << "Delete " << *it);
                    it = block.statements.erase(it);
                } else {
                    MIR_ASSERT(state, !(it->is_Assign() && it->as_Assign().src.tag() == ::MIR::RValue::TAGDEAD), "");
                    ++it;
                }
            }
        }
        replacement_happend = (replaced > 0);
    }
    // --- Eliminate `... = Use(tmp)` (propagate lvalues upwards)
    {
        DEBUG("- Move upwards");
        for (auto& block : fcn.blocks) {
            for (auto it = block.statements.begin(); it != block.statements.end(); ++it) {
                state.set_cur_stmt(&block - &fcn.blocks.front(), it - block.statements.begin());
                if (!it->is_Assign()) {
                    continue;
                }
                if (it->as_Assign().src.tag() == ::MIR::RValue::TAGDEAD) {
                    continue;
                }
                auto& to_replace_lval = it->as_Assign().dst;
                if (to_replace_lval.is_Local()) {
                    const auto& vu = val_uses.local_uses[to_replace_lval.as_Local()];
                    if (!(vu.read == 1 && vu.write == 1 && vu.borrow == 0)) {
                        continue;
                    }
                } else {
                    continue;
                }
                // ^^^  `tmp[1:1] = some_rvalue`

                // Find where it's used
                for (auto it2 = it + 1; it2 != block.statements.end(); ++it2) {
                    if (!it2->is_Assign()) {
                        continue;
                    }
                    if (it2->as_Assign().src.tag() == ::MIR::RValue::TAGDEAD) {
                        continue;
                    }
                    if (!it2->as_Assign().src.is_Use()) {
                        continue;
                    }
                    if (it2->as_Assign().src.as_Use() != to_replace_lval) {
                        continue;
                    }
                    const auto& new_dst_lval = it2->as_Assign().dst;
                    // `... = Use(to_replace_lval)`

                    // TODO: Ensure that the target isn't borrowed.
                    if (new_dst_lval.is_Local()) {
                        const auto& vu = val_uses.local_uses[new_dst_lval.as_Local()];
                        if (!(vu.read == 1 && vu.write == 1 && vu.borrow == 0)) {
                            break;
                        }
                    } else if (new_dst_lval.is_Return()) {
                        // Return, can't be borrowed?
                    } else {
                        break;
                    }

                    // Ensure that the target doesn't change in the intervening time.
                    bool was_invalidated = false;
                    for (auto it3 = it + 1; it3 != it2; it3++) {
                        // Closure returns `true` if the passed lvalue is a component of `new_dst_lval`
                        auto is_lvalue_in_val = [&](const auto& lv) {
                            // Don't care about indexing?
                            return lv.m_root == new_dst_lval.m_root;
                        };
                        if (visit_mir_lvalues(*it3, [&](const auto& lv, auto) {
                            return is_lvalue_in_val(lv);
                        })) {
                            was_invalidated = true;
                            break;
                        }
                    }

                    // Replacement is valid.
                    if (!was_invalidated) {
                        DEBUG(state << "Replace assignment of " << to_replace_lval << " with " << new_dst_lval);
                        it->as_Assign().dst = mv$(it2->as_Assign().dst);
                        block.statements.erase(it2);
                        replacement_happend = true;
                        break;
                    }
                }
            }
        }
    }

    // --- Function returns (reverse propagate)
    // > Find `tmp = <function call>` where the temporary is used 1:1
    // > Search the following block for `<anything> = Use(this_tmp)`
    // > Ensure that the target of the above assignment isn't used in the intervening statements
    // > Replace function call result value with target of assignment
    {
        DEBUG("- Returns");
        for (auto& block : fcn.blocks) {
            if (block.terminator.tag() == ::MIR::Terminator::TAGDEAD) {
                continue;
            }

            // If the terminator is a call that writes to a 1:1 value, replace the destination value with the eventual destination (if that value isn't used in the meantime)
            if (block.terminator.is_Call()) {
                // TODO: What if the destination located here is a 1:1 and its usage is listed to be replaced by the return value.
                auto& e = block.terminator.as_Call();
                if (!e.ret_val.is_Local()) {
                    continue;
                }
                const auto& vu = val_uses.local_uses[e.ret_val.as_Local()];
                if (!(vu.read == 1 && vu.write == 1 && vu.borrow == 0)) {
                    continue;
                }

                // Iterate the target block, looking for where this value is used.
                const ::MIR::LValue* new_dst = nullptr;
                auto& blk2 = fcn.blocks.at(e.ret_block);
                for (const auto& stmt : blk2.statements) {
                    // Find `RValue::Use( this_lvalue )`
                    if (stmt.is_Assign() && stmt.as_Assign().src.is_Use() && stmt.as_Assign().src.as_Use() == e.ret_val) {
                        new_dst = &stmt.as_Assign().dst;
                        break;
                    }
                }

                // Ensure that the new destination value isn't used before assignment
                if (new_dst) {
                    auto lvalue_impacts_dst = [&](const ::MIR::LValue& lv) -> bool {
                        // Returns true if the two lvalues share a common root
                        // TODO: Could restrict based on the presence of deref/field accesses?
                        // If `lv` is a local AND matches the index in `new_dst`, check for indexing
                        if (lv.is_Local()) {
                            for (const auto& w : new_dst->m_wrappers) {
                                if (w.is_Index() && w.as_Index() == lv.as_Local()) {
                                    return true;
                                }
                            }
                        }
                        return lv.m_root == new_dst->m_root;
                    };
                    for (auto it = blk2.statements.begin(); it != blk2.statements.end(); ++it) {
                        state.set_cur_stmt(&blk2 - &fcn.blocks.front(), it - blk2.statements.begin());
                        const auto& stmt = *it;
                        if (stmt.is_Assign() && stmt.as_Assign().src.is_Use() && stmt.as_Assign().src.as_Use() == e.ret_val) {
                            DEBUG(state << "- Replace function return " << e.ret_val << " with " << *new_dst);
                            e.ret_val = new_dst->clone();
                            // TODO: Invalidate the entry, instead of deleting?
                            it = blk2.statements.erase(it);
                            replacement_happend = true;
                            break;
                        }
                        if (visit_mir_lvalues(stmt, [&](const MIR::LValue& lv, ValUsage vu) {
                            return lv == *new_dst || (vu == ValUsage::Write && lvalue_impacts_dst(lv));
                        })) {
                            break;
                        }
                    }
                }
            }
        }
    }

    // Locate values that are written, but not read or borrowed
    // - Current implementation requires a single write (to avoid issues with drop)
    // - if T: Drop (or T: !Copy) then the write should become a drop
    {
        DEBUG("- Write-only");
        for (auto& block : fcn.blocks) {
            for (auto it = block.statements.begin(); it != block.statements.end(); ++it) {
                state.set_cur_stmt(&block - &fcn.blocks.front(), it - block.statements.begin());
                if (const auto& se = it->opt_Assign()) {
                    // Remove No-op assignments (assignment from a lvalue to itself)
                    if (const auto* src_e = se->src.opt_Use()) {
                        if (se->dst == *src_e) {
                            DEBUG(state << se->dst << " set to itself, removing write");
                            it = block.statements.erase(it) - 1;
                            continue;
                        }
                    }

                    // Remove assignments of locals that are never read
                    if (se->dst.is_Local()) {
                        const auto& vu = val_uses.local_uses[se->dst.as_Local()];
                        if (vu.write == 1 && vu.read == 0 && vu.borrow == 0) {
                            DEBUG(state << se->dst << " only written, removing write");
                            it = block.statements.erase(it) - 1;
                        }
                    }
                }
            }
            // NOTE: Calls can write values, but they also have side-effects
        }
    }

    // TODO: Run special case replacements for when there's `tmp/var = arg` and `rv = tmp/var`

    return replacement_happend;
}

// ----------------------------------------
// Clear all drop flags that are never read
// ----------------------------------------
bool MIR_Optimise_DeadDropFlags(::MIR::TypeResolve& state, ::MIR::Function& fcn) {
    bool removed_statement = false;
    TRACE_FUNCTION_FR("", removed_statement);
    ::std::vector<bool> used_drop_flags(fcn.drop_flags.size());
    {
        ::std::vector<bool> read_drop_flags(fcn.drop_flags.size());
        visit_blocks(state, fcn, [&read_drop_flags, &used_drop_flags](auto, const ::MIR::BasicBlock& block) {
            for (const auto& stmt : block.statements) {
                if (const auto* e = stmt.opt_SetDropFlag()) {
                    if (e->other != ~0u) {
                        read_drop_flags[e->other] = true;
                        used_drop_flags[e->other] = true;
                    }
                    used_drop_flags[e->idx] = true;
                } else if (const auto* e = stmt.opt_Drop()) {
                    if (e->flag_idx != ~0u) {
                        read_drop_flags[e->flag_idx] = true;
                        used_drop_flags[e->flag_idx] = true;
                    }
                } else if (const auto* e = stmt.opt_SaveDropFlag()) {
                    read_drop_flags[e->idx] = true;
                    used_drop_flags[e->idx] = true;
                } else if (const auto* e = stmt.opt_LoadDropFlag()) {
                    used_drop_flags[e->idx] = true;
                }
            }
            if (const auto* e = block.terminator.opt_Switch()) {
                if (e->valid_flag != ~0u) {
                    read_drop_flags[e->valid_flag] = true;
                    used_drop_flags[e->valid_flag] = true;
                }
            }
        });
        DEBUG("Un-read drop flags:" << FMT_CB(ss, for (size_t i = 0; i < read_drop_flags.size(); i++) if (!read_drop_flags[i] && used_drop_flags[i]) ss << " " << i;));
        visit_blocks_mut(state, fcn, [&read_drop_flags, &removed_statement](auto _id, auto& block) {
            for (auto it = block.statements.begin(); it != block.statements.end();) {
                if (it->is_SetDropFlag() && !read_drop_flags[it->as_SetDropFlag().idx]) {
                    removed_statement = true;
                    it = block.statements.erase(it);
                } else if (it->is_LoadDropFlag() && !read_drop_flags[it->as_LoadDropFlag().idx]) {
                    removed_statement = true;
                    it = block.statements.erase(it);
                } else {
                    ++it;
                }
            }
        });
    }

    // Find any drop flags that are never assigned with a value other than their default, then remove those dead assignments.
    {
        ::std::vector<bool> edited_drop_flags(fcn.drop_flags.size());
        visit_blocks(state, fcn, [&edited_drop_flags, &fcn](auto, const ::MIR::BasicBlock& block) {
            for (const auto& stmt : block.statements) {
                if (const auto* e = stmt.opt_SetDropFlag()) {
                    if (e->other != ~0u) {
                        // If the drop flag is set based on another, assume it's changed
                        edited_drop_flags[e->idx] = true;
                    } else if (e->new_val != fcn.drop_flags[e->idx]) {
                        // If the new value is not the default, it's changed
                        edited_drop_flags[e->idx] = true;
                    } else {
                        // Set to the default, doesn't change the 'edited' state
                    }
                }
            }
        });
        DEBUG("Un-edited drop flags:" << FMT_CB(ss, for (size_t i = 0; i < edited_drop_flags.size(); i++) if (!edited_drop_flags[i] && used_drop_flags[i]) ss << " " << i;));
        visit_blocks_mut(state, fcn, [&edited_drop_flags, &removed_statement, &fcn](auto _id, auto& block) {
            for (auto it = block.statements.begin(); it != block.statements.end();) {
                // If this is a SetDropFlag and the target flag isn't edited, remove
                if (const auto* e = it->opt_SetDropFlag()) {
                    if (!edited_drop_flags[e->idx]) {
                        assert(e->new_val == fcn.drop_flags[e->idx]);
                        removed_statement = true;
                        it = block.statements.erase(it);
                    } else {
                        ++it;
                    }
                } else {
                    ++it;
                }
            }
        });
    }

    return removed_statement;
}

// --------------------------------------------------------------------
// Remove unread assignments of locals (and replaced assignments of anything?)
// --------------------------------------------------------------------
bool MIR_Optimise_DeadAssignments(::MIR::TypeResolve& state, ::MIR::Function& fcn) {
    bool changed = false;
    TRACE_FUNCTION_FR("", changed);

    // Find any locals that are never read, and delete their assignments.

    // Per-local flag indicating that the particular local is read.
    ::std::vector<bool> read_locals(fcn.locals.size());
    ::std::vector<bool> dropped_locals(fcn.locals.size());
    for (const auto& bb : fcn.blocks) {
        auto cb = [&](const ::MIR::LValue& lv, ValUsage vu) {
            if (lv.m_root.is_Local()) {
                read_locals[lv.m_root.as_Local()] = true;
            }
            for (const auto& w : lv.m_wrappers) {
                if (w.is_Index()) {
                    read_locals[w.as_Index()] = true;
                }
            }
            return false;
        };
        for (const auto& stmt : bb.statements) {
            // If the assignment is to a local, then just consider the source (the target is writing to a local)
            if (stmt.is_Assign() && stmt.as_Assign().dst.is_Local()) {
                visit_mir_lvalues(stmt.as_Assign().src, cb);
            }
            // Record drops differently, allowing us to remove unused non-Copy items
            else if (stmt.is_Drop() && stmt.as_Drop().slot.is_Local()) {
                dropped_locals[stmt.as_Drop().slot.as_Local()] = true;
            }
            // For other statment types (e.g. asm) - record anything
            else {
                visit_mir_lvalues(stmt, cb);
            }
        }
        visit_mir_lvalues(bb.terminator, cb);
    }

    for (auto& bb : fcn.blocks) {
        for (auto it = bb.statements.begin(); it != bb.statements.end();) {
            state.set_cur_stmt(&bb - &fcn.blocks.front(), it - bb.statements.begin());

            // Remove drops of assigned values that will be removed
            if (it->is_Drop() && it->as_Drop().slot.is_Local()) {
                auto idx = it->as_Drop().slot.as_Local();
                if (!read_locals[idx] && fcn.locals[idx]->is_Borrow()) {
                    DEBUG(state << "Drop of unread value, remove - " << *it);
                    it = bb.statements.erase(it);
                    continue;
                }
            }

            // Not an assignment, ignore
            if (!(it->is_Assign() && it->as_Assign().dst.is_Local())) {
                ++it;
                continue;
            }
            auto idx = it->as_Assign().dst.as_Local();
            // Local was read, ignore it
            if (read_locals[idx]) {
                ++it;
                continue;
            }
            // If the local was dropped, then ignore IF it's not a borrow (TODO: Only if there's drop glue?)
            if (dropped_locals[idx] && !fcn.locals[idx]->is_Borrow()) {
                ++it;
                continue;
            }
            // Remove the assignment, as it's unused
            DEBUG(state << "Unread assignment, remove - " << *it);
            it = bb.statements.erase(it);
            changed = true;
        }
    }

    // Locate assignments of locals then find the next assignment or read.
    return changed;
}

// --------------------------------------------------------------------
// Eliminate no-operation assignments that may have appeared
// --------------------------------------------------------------------
bool MIR_Optimise_NoopRemoval(::MIR::TypeResolve& state, ::MIR::Function& fcn) {
    bool changed = false;
    TRACE_FUNCTION_FR("", changed);

    HIR::TypeRef tmp_ty;
    // Remove useless operations
    for (auto& bb : fcn.blocks) {
        // Multi-statement no-ops (round-trip casts, reboorrow+cast)
        for (auto it = bb.statements.begin(); it != bb.statements.end(); ++it) {
            state.set_cur_stmt(&bb - fcn.blocks.data(), it - bb.statements.begin());
            // `_0 = &mut *foo`, then `_1 = _0 as *mut T` where `foo: *mut T`
            // - Note: Accepts `_0 = &*foo; _1 = _0 as T` where `foo: T`
            if (it->is_Assign() && it->as_Assign().dst.is_Local() && it->as_Assign().src.is_Borrow() && it->as_Assign().src.as_Borrow().val.is_Deref()) {
                const auto& dst_lv = it->as_Assign().dst;
                auto src_lv = it->as_Assign().src.as_Borrow().val.clone_unwrapped();
                // Find the next use of this target lvalue
                for (auto it2 = it + 1; it2 != bb.statements.end(); ++it2) {
                    // If it's a cast back to the original type, then replace with a direct assignment of the original value
                    if (it2->is_Assign() && it2->as_Assign().src.is_Cast() && it2->as_Assign().src.as_Cast().val == dst_lv) {
                        const auto& dst_ty = it2->as_Assign().src.as_Cast().type;
                        HIR::TypeRef tmp;
                        const auto& orig_ty = state.get_lvalue_type(tmp, src_lv);
                        if (orig_ty == dst_ty) {
                            DEBUG(state << "Reborrow and cast back - " << *it << " and " << *it2);
                            it2->as_Assign().src = std::move(src_lv);
                            break;
                        }
                    }
                    if (check_invalidates_lvalue(*it2, src_lv, false)) {
                        break;
                    }
                }
            }

            // `_0 = foo as *const T; _1 = _0 as *mut T` where `foo: *mut T`
            // - Note: Accepts `_0 = foo as *const T; _1 = _0 as U` where `foo: U`
            if (it->is_Assign() && it->as_Assign().dst.is_Local() && it->as_Assign().src.is_Cast() && it->as_Assign().src.as_Cast().type->is_Pointer()) {
                const auto& dst_lv = it->as_Assign().dst;
                const auto& src_lv = it->as_Assign().src.as_Cast().val;
                // Find the next use of this target lvalue
                for (auto it2 = it + 1; it2 != bb.statements.end(); ++it2) {
                    // If it's a cast back to the original type, then replace with a direct assignment of the original value
                    if (it2->is_Assign() && it2->as_Assign().src.is_Cast() && it2->as_Assign().src.as_Cast().val == dst_lv) {
                        const auto& dst_ty = it2->as_Assign().src.as_Cast().type;
                        HIR::TypeRef tmp;
                        const auto& orig_ty = state.get_lvalue_type(tmp, src_lv);
                        if (orig_ty == dst_ty) {
                            DEBUG(state << "Round-trip pointer cast - " << *it << " and " << *it2);
                            it2->as_Assign().src = src_lv.clone();
                            break;
                        }
                    }
                    if (check_invalidates_lvalue(*it2, src_lv, false)) {
                        break;
                    }
                }
            }
        }

        for (auto it = bb.statements.begin(); it != bb.statements.end();) {
            state.set_cur_stmt(&bb - fcn.blocks.data(), it - bb.statements.begin());

            // Placeholder: Asm block with empty template and no inputs/outputs/flags
            if (*it == MIR::Statement::make_Asm({})) {
                DEBUG(state << "Empty ASM placeholder, remove - " << *it);
                it = bb.statements.erase(it);
                changed = true;

                continue;
            }

            // `Value = Use(Value)`
            if (it->is_Assign() && it->as_Assign().src.is_Use() && it->as_Assign().src.as_Use() == it->as_Assign().dst) {
                DEBUG(state << "Useless assignment, remove - " << *it);
                it = bb.statements.erase(it);
                changed = true;

                continue;
            }

            // `Value = Borrow(Deref(Value))`
            if (it->is_Assign() && it->as_Assign().src.is_Borrow() && it->as_Assign().src.as_Borrow().val.is_Deref() && it->as_Assign().src.as_Borrow().val.clone_unwrapped() == it->as_Assign().dst) {
                DEBUG(state << "Useless assignment (v = &*v), remove - " << *it);
                it = bb.statements.erase(it);
                changed = true;

                continue;
            }

            // Cast to the same type
            if (it->is_Assign() && it->as_Assign().src.is_Cast() && it->as_Assign().src.as_Cast().type == state.get_lvalue_type(tmp_ty, it->as_Assign().src.as_Cast().val)) {
                DEBUG(state << "No-op cast, replace with assignment - " << *it);
                auto v = mv$(it->as_Assign().src.as_Cast().val);
                it->as_Assign().src = MIR::RValue::make_Use({mv$(v)});
                changed = true;

                ++it;
                continue;
            }

            // Drop of Copy type
            if (it->is_Drop() && state.lvalue_is_copy(it->as_Drop().slot)) {
                DEBUG(state << "Drop of Copy type, remove - " << *it);
                it = bb.statements.erase(it);
                changed = true;

                continue;
            }

            ++it;
        }
    }

    return changed;
}

// --------------------------------------------------------------------
// If the first statement of a block is an assignment from a local, and all sources of that block assign to that local
// - Move the assigment backwards
// --------------------------------------------------------------------
bool MIR_Optimise_GotoAssign(::MIR::TypeResolve& state, ::MIR::Function& fcn) {
    bool changed = false;
    TRACE_FUNCTION_FR("", changed);

    // 1. Locate blocks that start with an elligable assignemnt
    // - Target must be "simple" (not a static, no wrappers)
    // - Source can be any lvalue? Restrict to locals for now (static/deref assignment is a side-effect)
    //   > Restrict to single-read locals? Or replace the trigger statement with a reversed copy?
    // 2. Check all source blocks, and see if they assign to that block
    // > Terminator must be: GOTO, or CALL <lv> = ... (with the non-panic arm)
    // 3. If more than half the source blocks assign the source, then move up
    // - Any IF/SWITCH/... terminator blocks the optimisation
    for (auto& dst_bb : fcn.blocks) {
        if (dst_bb.statements.empty()) {
            continue;
        }
        auto bb_idx = &dst_bb - fcn.blocks.data();
        state.set_cur_stmt(bb_idx, 0);
        auto& stmt = dst_bb.statements[0];
        if (!stmt.is_Assign()) {
            continue;
        }
        if (!stmt.as_Assign().src.is_Use()) {
            continue;
        }
        auto& dst = stmt.as_Assign().dst;
        auto& src = stmt.as_Assign().src.as_Use();

        if (!dst.m_wrappers.empty() || dst.m_root.is_Static()) {
            continue;
        }
        if (!src.is_Local()) {
            continue;
        }
        // Source must be a single-read local (so this assignment can be deleted)
        unsigned n_read = 0;
        unsigned n_borrow = 0;
        visit_mir_lvalues(state, fcn, [&](const auto& lv, auto vu) {
            if (lv.m_root == src.m_root) {
                switch (vu) {
                    case ValUsage::Read:
                    case ValUsage::Move:
                        n_read++;
                        break;
                    case ValUsage::Borrow:
                        n_borrow++;
                        break;
                    case ValUsage::Write:
                        // Don't care
                        break;
                }
            }
            for (const auto& w : lv.m_wrappers) {
                if (w.is_Index()) {
                    if (::MIR::LValue::new_Local(w.as_Index()) == src) {
                        n_read++;
                    }
                }
            }
            return true;
        });
        state.set_cur_stmt(bb_idx, 0);
        if (n_read > 1 || n_borrow > 0) {
            DEBUG(state << "Source " << src << " is read " << n_read << " times and borrowed " << n_borrow);
            continue;
        }
        DEBUG(state << "Eligible assignment (" << stmt << ")");

        // Find source blocks, check terminators/last
        std::vector<unsigned> sources;
        unsigned num_used = 0;
        for (const auto& src_bb : fcn.blocks) {
            unsigned bb_idx = &src_bb - fcn.blocks.data();
            bool used = false;
            visit_terminator_target(src_bb.terminator, [&](const auto& tgt) {
                if (tgt == state.get_cur_block()) {
                    used = true;
                    sources.push_back(bb_idx);
                }
            });
            if (used) {
                TU_MATCH_HDRA( (src_bb.terminator), { )
                TU_ARMA(Goto, e) {
                        if (src_bb.statements.empty()) {
                            DEBUG(state << "BB" << bb_idx << " empty");
                        } else if (TU_TEST1(src_bb.statements.back(), Assign, .dst == src)) {
                            DEBUG("BB" << bb_idx << "/" << src_bb.statements.size() << " " << src_bb.statements.back());
                            num_used += 1;
                        } else {
                            DEBUG("BB" << bb_idx << "/" << src_bb.statements.size() << " " << src_bb.statements.back() << " - Doesn't write");
                        }
                    }
                    TU_ARMA(Call, e) {
                        if (e.ret_block != state.get_cur_block()) {
                            DEBUG(state << "BB" << bb_idx << "/TERM " << src_bb.terminator << " - Not return block");
                        } else if (e.ret_val != src) {
                            DEBUG(state << "BB" << bb_idx << "/TERM " << src_bb.terminator << " - Doesn't write to source");
                        } else {
                            num_used += 1;
                        }
                    }
                    break;
                    default:
                        DEBUG(state << "BB" << bb_idx << "/TERM " << src_bb.terminator << " - Wrong terminator type");
                        break;
                }
            }
        }

        // TODO: Allow if one arm doesn't update?
        // - What if a call invalidates the target?
        if (num_used < sources.size()) {
            DEBUG(state << "- Not all sources set the value");
            continue;
        }

        changed = true;

        // Time to edit.
        // 1. Update all sources
        for (auto bb_idx : sources) {
            auto& src_bb = fcn.blocks[bb_idx];

            if (TU_TEST1(src_bb.terminator, Call, .ret_val == src)) {
                DEBUG("- Source block: BB" << bb_idx << " - term " << src_bb.terminator);
                src_bb.terminator.as_Call().ret_val = dst.clone();
            } else if (!src_bb.statements.empty() && TU_TEST1(src_bb.statements.back(), Assign, .dst == src)) {
                DEBUG("- Source block: BB" << bb_idx << " - tail " << src_bb.statements.back());
                src_bb.statements.back().as_Assign().dst = dst.clone();
            } else {
                MIR_TODO(state, "Handle copying assignment to source");
            }
            if (!src_bb.statements.empty()) {
                DEBUG("+- BB" << bb_idx << "/" << (src_bb.statements.size() - 1) << " " << src_bb.statements.back());
            }
            DEBUG("+- BB" << bb_idx << "/TERM " << src_bb.terminator);
        }
        // IF the value is `Copy` (i.e. the initial assignment could be expected to survive), then reverse the destination
        // - Can't do this, it's going to cause infinite recursion!
        if (false && state.lvalue_is_copy(dst)) {
            auto d = dst.clone();
            dst = mv$(src);
            src = mv$(d);
            DEBUG(state << "- Updated (" << stmt << ")");
        } else {
            stmt = MIR::Statement();
            DEBUG(state << "- Deleted");
        }
    }

    return changed;
}

// --------------------------------------------------------------------
// Find re-borrows of values that aren't otherwise used.
//
// - Look for `<local> = &[mut] *<local/arg>`
// - Check if the source is only ever used here (and in a drop)
// - If that's the case, replace usage with a move and delete the drop
//
// TODO: Could allow multiple uses if it's a shared borrow
// --------------------------------------------------------------------
bool MIR_Optimise_UselessReborrows(::MIR::TypeResolve& state, ::MIR::Function& fcn) {
    bool changed = false;
    TRACE_FUNCTION_FR("", changed);

    // TODO: This doesn't work if the assignment happens in a loop (can lead to multiple moves)
    // - Need to have a way of knowing if a block is a loop member

    return changed;
}

// --------------------------------------------------------------------
// Clear all unused blocks
// --------------------------------------------------------------------
bool MIR_Optimise_GarbageCollect_Partial(::MIR::TypeResolve& state, ::MIR::Function& fcn) {
    bool rv = false;
    TRACE_FUNCTION_FR("", rv);
    ::std::vector<bool> visited(fcn.blocks.size());
    visit_blocks(state, fcn, [&visited](auto bb, const auto& /*block*/) {
        assert(!visited[bb]);
        visited[bb] = true;
    });
    for (unsigned int i = 0; i < visited.size(); i++) {
        auto& blk = fcn.blocks[i];
        if (blk.terminator.is_Incomplete() && blk.statements.empty()) {
        } else if (visited[i]) {
        } else {
            DEBUG("CLEAR bb" << i);
            blk.statements.clear();
            blk.terminator = ::MIR::Terminator::make_Incomplete({});
            rv = true;
        }
    }
    return rv;
}

// --------------------------------------------------------------------
// Remove all unused temporaries and blocks
// --------------------------------------------------------------------
bool MIR_Optimise_GarbageCollect(::MIR::TypeResolve& state, ::MIR::Function& fcn) {
    ::std::vector<bool> used_locals(fcn.locals.size());
    ::std::vector<bool> used_dfs(fcn.drop_flags.size());
    ::std::vector<bool> visited(fcn.blocks.size());

    visit_blocks(state, fcn, [&](auto bb, const auto& block) {
        visited[bb] = true;

        auto assigned_lval = [&](const ::MIR::LValue& lv) {
            // TODO: Consume through indexing/field accesses
            for (const auto& w : lv.m_wrappers) {
                if (w.is_Field()) {
                } else {
                    return;
                }
            }
            if (lv.m_root.is_Local()) {
                used_locals[lv.m_root.as_Local()] = true;
            }
        };

        for (const auto& stmt : block.statements) {
            TU_IFLET(::MIR::Statement, stmt, Assign, e, assigned_lval(e.dst);)
            //else if( const auto* e = stmt.opt_Drop() )
            //{
            //    //if( e->flag_idx != ~0u )
            //    //    used_dfs.at(e->flag_idx) = true;
            //}
            else if (const auto* e = stmt.opt_Asm()) {
                for (const auto& val : e->outputs) {
                    assigned_lval(val.second);
                }
            }
            else if (const auto* e = stmt.opt_Asm2()) {
                for (const auto& p : e->params) {
                    if (p.is_Reg() && p.as_Reg().output) {
                        assigned_lval(*p.as_Reg().output);
                    }
                }
            }
            else if (const auto* e = stmt.opt_SetDropFlag()) {
                if (e->other != ~0u) {
                    used_dfs.at(e->other) = true;
                }
                used_dfs.at(e->idx) = true;
            }
            else if (const auto* e = stmt.opt_LoadDropFlag()) {
                used_dfs.at(e->idx) = true;
            }
        }

        if (const auto* te = block.terminator.opt_Call()) {
            assigned_lval(te->ret_val);
        } else if (const auto* te = block.terminator.opt_Switch()) {
            if (te->valid_flag != ~0u) {
                used_dfs.at(te->valid_flag) = true;
            }
        }
    });

    ::std::vector<unsigned int> block_rewrite_table;
    for (unsigned int i = 0, j = 0; i < fcn.blocks.size(); i++) {
        block_rewrite_table.push_back(visited[i] ? j++ : ~0u);
    }
    ::std::vector<unsigned int> local_rewrite_table;
    unsigned int n_locals = fcn.locals.size();
    for (unsigned int i = 0, j = 0; i < n_locals; i++) {
        if (!used_locals[i]) {
            fcn.locals.erase(fcn.locals.begin() + j);
        } else {
            DEBUG("_" << i << " => _" << j);
        }
        local_rewrite_table.push_back(used_locals[i] ? j++ : ~0u);
    }
    DEBUG("Deleted Locals:" << FMT_CB(ss, for (auto run : runs(used_locals)) if (!used_locals[run.first]) {
              ss << " " << run.first;
              if (run.second != run.first) {
                  ss << "-" << run.second;
              }
          }));
    ::std::vector<unsigned int> df_rewrite_table;
    unsigned int n_df = fcn.drop_flags.size();
    for (unsigned int i = 0, j = 0; i < n_df; i++) {
        if (!used_dfs[i]) {
            DEBUG("GC df" << i);
            // NOTE: Not erased until after rewriting
        }
        df_rewrite_table.push_back(used_dfs[i] ? j++ : ~0u);
    }

    auto it = fcn.blocks.begin();
    for (unsigned int i = 0; i < visited.size(); i++) {
        if (visited[i]) {
            auto lvalue_cb = [&](::MIR::LValue& lv, auto) {
                if (lv.m_root.is_Local()) {
                    auto e = lv.m_root.as_Local();
                    MIR_ASSERT(state, e < local_rewrite_table.size(), "Variable out of range - " << lv);
                    // If the table entry for this variable is !0, it wasn't marked as used
                    MIR_ASSERT(state, local_rewrite_table.at(e) != ~0u, "LValue " << lv << " incorrectly marked as unused");
                    lv.m_root = ::MIR::LValue::Storage::new_Local(local_rewrite_table.at(e));
                }
                for (auto& w : lv.m_wrappers) {
                    if (w.is_Index()) {
                        w = ::MIR::LValue::Wrapper::new_Index(local_rewrite_table.at(w.as_Index()));
                    }
                }
                return false;
            };
            ::std::vector<bool> to_remove_statements(it->statements.size());
            for (auto& stmt : it->statements) {
                auto stmt_idx = &stmt - &it->statements.front();
                state.set_cur_stmt(i, stmt_idx);

                if (stmt == ::MIR::Statement()) {
                    DEBUG(state << "Remove " << stmt << " - Pure default");
                    to_remove_statements[stmt_idx] = true;
                    continue;
                }

                if (auto* se = stmt.opt_Drop()) {
                    // If the drop flag was unset, either remove the drop or remove the drop flag reference
                    if (se->flag_idx != ~0u && df_rewrite_table[se->flag_idx] == ~0u) {
                        if (fcn.drop_flags.at(se->flag_idx)) {
                            DEBUG(state << "Remove flag from " << stmt << " - Flag never set and default true");
                            se->flag_idx = ~0u;
                        } else {
                            DEBUG(state << "Remove " << stmt << " - Flag never set and default false");
                            to_remove_statements[stmt_idx] = true;
                            continue;
                        }
                    }

                    // A local with no assignment in any reachable block is
                    // definitely uninitialized. Conditional assignments mark
                    // the local as used above and preserve its drop flag.
                    if (se->slot.is_Local() && local_rewrite_table[se->slot.as_Local()] == ~0u) {
                        DEBUG(state << "Remove " << stmt << " - Dropping non-set value");
                        to_remove_statements[stmt_idx] = true;
                        continue;
                    }
                }

                visit_mir_lvalues_mut(stmt, lvalue_cb);
                if (auto* se = stmt.opt_Drop()) {
                    // Rewrite drop flag indexes
                    if (se->flag_idx != ~0u) {
                        se->flag_idx = df_rewrite_table[se->flag_idx];
                    }
                } else if (auto* se = stmt.opt_SetDropFlag()) {
                    // Rewrite drop flag indexes OR delete
                    if (df_rewrite_table[se->idx] == ~0u) {
                        to_remove_statements[stmt_idx] = true;
                        continue;
                    }
                    se->idx = df_rewrite_table[se->idx];
                    if (se->other != ~0u) {
                        se->other = df_rewrite_table[se->other];
                    }
                } else if (auto* se = stmt.opt_LoadDropFlag()) {
                    se->idx = df_rewrite_table[se->idx];
                } else if (auto* se = stmt.opt_SaveDropFlag()) {
                    se->idx = df_rewrite_table[se->idx];
                } else if (auto* se = stmt.opt_ScopeEnd()) {
                    for (auto it = se->slots.begin(); it != se->slots.end();) {
                        if (local_rewrite_table.at(*it) == ~0u) {
                            it = se->slots.erase(it);
                        } else {
                            *it = local_rewrite_table.at(*it);
                            ++it;
                        }
                    }

                    if (se->slots.empty()) {
                        DEBUG(state << "Delete ScopeEnd (now empty)");
                        to_remove_statements[stmt_idx] = true;
                        continue;
                    }
                }
            }
            state.set_cur_stmt_term(i);
            // Rewrite and advance
            visit_mir_lvalues_mut(it->terminator, lvalue_cb);
            TU_MATCHA(
                (it->terminator),
                (e),
                (Incomplete, ),
                (Return, ),
                (Diverge, ),
                (Goto, e = block_rewrite_table[e];),
                (Panic, ),
                (If, e.bb_true = block_rewrite_table[e.bb_true]; e.bb_false = block_rewrite_table[e.bb_false];),
                (
                    Switch, for (auto& target : e.targets) target = block_rewrite_table[target]; if (e.valid_flag != ~0u) {
                        e.valid_flag = df_rewrite_table[e.valid_flag];
                        e.invalid_target = block_rewrite_table[e.invalid_target];
                    }
                ),
                (SwitchValue, for (auto& target : e.targets) target = block_rewrite_table[target]; e.def_target = block_rewrite_table[e.def_target];),
                (Call, e.ret_block = block_rewrite_table[e.ret_block]; e.panic_block = block_rewrite_table[e.panic_block];)
            )

            // Delete all statements flagged in a bitmap for deletion
            assert(it->statements.size() == to_remove_statements.size());
            auto new_end = ::std::remove_if(it->statements.begin(), it->statements.end(), [&](const auto& s) {
                size_t stmt_idx = (&s - &it->statements.front());
                return to_remove_statements[stmt_idx];
            });
            it->statements.erase(new_end, it->statements.end());
        }
        ++it;
    }

    auto new_blocks_end = ::std::remove_if(fcn.blocks.begin(), fcn.blocks.end(), [&](const auto& bb) {
        size_t i = &bb - &fcn.blocks.front();
        if (!visited[i]) {
            DEBUG("GC bb" << i);
        }
        return !visited[i];
    });
    fcn.blocks.erase(new_blocks_end, fcn.blocks.end());

    // Drop flags use vector<bool> proxy storage, so erase them by original and compacted index.
    for (unsigned int i = 0, j = 0; i < n_df; i++) {
        if (!used_dfs[i]) {
            fcn.drop_flags.erase(fcn.drop_flags.begin() + j);
        } else {
            j++;
        }
    }

    // TODO: Detect if any optimisations happened, and return true in that case
    return false;
}


/// Sort basic blocks to approximate program flow (helps when reading MIR)
void MIR_SortBlocks(const StaticTraitResolve& resolve, const ::HIR::ItemPath& path, ::MIR::Function& fcn) {
    ::std::vector<bool> visited(fcn.blocks.size());
    ::std::vector<::std::pair<unsigned, unsigned>> depths(fcn.blocks.size());

    struct Todo {
        size_t bb_idx;
        unsigned branch_count;
        unsigned level;
    };

    unsigned int branches = 0;
    ::std::vector<Todo> todo;
    todo.push_back(Todo{0, 0, 0});

    while (!todo.empty()) {
        auto info = todo.back();
        todo.pop_back();
        if (visited[info.bb_idx]) {
            continue;
        }

        visited[info.bb_idx] = true;
        depths[info.bb_idx] = ::std::make_pair(info.branch_count, info.level);
        const auto& bb = fcn.blocks[info.bb_idx];

        TU_MATCHA((bb.terminator), (te), (Incomplete, ), (Return, ), (Diverge, ), (Goto, todo.push_back(Todo{te, info.branch_count, info.level + 1});), (Panic, todo.push_back(Todo{te.dst, info.branch_count, info.level + 1});), (If, todo.push_back(Todo{te.bb_true, ++branches, info.level + 1}); todo.push_back(Todo{te.bb_false, ++branches, info.level + 1});), (Switch, for (auto dst : te.targets) todo.push_back(Todo{dst, ++branches, info.level + 1});), (SwitchValue, for (auto dst : te.targets) todo.push_back(Todo{dst, ++branches, info.level + 1}); todo.push_back(Todo{te.def_target, info.branch_count, info.level + 1});), (Call, todo.push_back(Todo{te.ret_block, info.branch_count, info.level + 1}); todo.push_back(Todo{te.panic_block, ++branches, info.level + 1});))
    }

    // Sort a list of block indexes by `depths`
    ::std::vector<size_t> idxes;
    idxes.reserve(fcn.blocks.size());
    for (size_t i = 0; i < fcn.blocks.size(); i++) {
        idxes.push_back(i);
    }
    ::std::sort(idxes.begin(), idxes.end(), [&](auto a, auto b) {
        return depths.at(a) < depths.at(b);
    });

    DEBUG(idxes);

    decltype(fcn.blocks) new_block_list;
    new_block_list.reserve(fcn.blocks.size());
    for (auto idx : idxes) {
        auto fix_bb_idx = [&](auto idx) {
            return ::std::find(idxes.begin(), idxes.end(), idx) - idxes.begin();
        };
        new_block_list.push_back(mv$(fcn.blocks[idx]));
        new_block_list.back().statements.shrink_to_fit(); // Save some memory
        visit_terminator_target_mut(new_block_list.back().terminator, [&](auto& te) {
            te = fix_bb_idx(te);
        });
    }
    fcn.blocks = mv$(new_block_list);
}

void MIR_OptimiseCrate(::HIR::Crate& crate, unsigned opt_level, bool enable_inlining) {
    ::MIR::OuterVisitor ov{crate, [opt_level, enable_inlining](const auto& res, const auto& p, auto& expr, const auto& args, const auto& ty) {
        //if( ! dynamic_cast<::HIR::ExprNode_Block*>(expr.get()) ) {
        //    return ;
        //}
        auto& mir = expr.get_mir_or_error_mut(Span());
        if (opt_level == 0) {
            MIR_OptimiseMin(res, p, mir, args, ty);
        } else {
            // The crate driver validates after this optimisation and its final cleanup.
            // Preserve explicitly requested diagnostic checks inside the optimiser.
            MIR_Optimise(res, p, mir, args, ty, opt_level, enable_inlining, /*validate=*/getenv("MRUSTC_MIR_CHECK") != nullptr);
        }
        // Run cleanup to handle now-monomoprhised inlined constants
        MIR_Cleanup(res, p, mir, args, ty);
    }};
    ov.visit_crate(crate);
}

void MIR_OptimiseCrate_Inlining(const ::HIR::Crate& crate, TransList& list, bool post_save, unsigned opt_level, bool enable_inlining) {
    TRACE_FUNCTION;

    ::StaticTraitResolve resolve{crate};

    // If running after HIR has been serialised, we can eliminate calls to `const_eval_select` without
    // impacting constant evaluation in downstream crates
    if (post_save) {
        // Visit every function in the monomorph list and raplce `const_eval_select` calls with calls to the runtime function
        for (auto& fcn_ent : list.m_functions) {
            auto& hir_fcn = *const_cast<::HIR::Function*>(fcn_ent.second->ptr);
            ::MIR::Function* fcn_p;
            if (fcn_ent.second->monomorphised.code) {
                DEBUG("Generic: " << fcn_ent.first);
                fcn_p = &*fcn_ent.second->monomorphised.code;
            } else if (hir_fcn.m_code.m_mir) {
                DEBUG("Concrete: " << fcn_ent.first);
                fcn_p = &hir_fcn.m_code.get_mir_or_error_mut(Span());
            } else {
                // Ignore, this is an external function reference.
                DEBUG("External: " << fcn_ent.first);
                continue;
            }

            auto& fcn = *fcn_p;
            for (auto& block : fcn.blocks) {
                if (auto* te = block.terminator.opt_Call()) {
                    if (te->fcn.is_Intrinsic() && te->fcn.as_Intrinsic().name == "const_eval_select") {
                        size_t n_args = te->fcn.as_Intrinsic().params.m_types.at(0)->as_Tuple().size();
                        const MIR::LValue arg = te->args.at(0).as_LValue().clone();
                        // Note: arg 1 is the constant function
                        const HIR::Path& fcn_path = *te->args.at(2).as_Constant().as_Function().p;

                        DEBUG(fcn_path);
                        te->fcn = fcn_path.clone();
                        te->args.clear();
                        te->args.reserve(n_args);
                        for (size_t i = 0; i < n_args; i++) {
                            te->args.push_back(MIR::LValue::new_Field(arg.clone(), i));
                        }
                    }
                }
            }
        }
    } else {
        for (const auto& fcn : list.m_functions) {
            DEBUG("FCN: " << fcn.first);
        }
    }

    if (!enable_inlining) {
        return;
    }

    // rustc level 4 removes analysis limits. Preserve a finite cap for normal
    // level-3 inlining, while level 4+ runs this monotonic pass to its fixed point.
    const size_t max_iterations = opt_level >= 4
        ? ::std::numeric_limits<size_t>::max()
        : 5;
    size_t num_iterations = 0;
    bool did_inline_on_pass;
    do {
        did_inline_on_pass = false;

        for (auto& fcn_ent : list.m_functions) {
            const auto& path = fcn_ent.first;
            //const auto& pp = fcn_ent.second->pp;
            auto& hir_fcn = *const_cast<::HIR::Function*>(fcn_ent.second->ptr);
            auto& mono_fcn = fcn_ent.second->monomorphised;

            ::std::string s = FMT(path);
            ::HIR::ItemPath ip(s);

            if (mono_fcn.code) {
                did_inline_on_pass |= MIR_OptimiseInline(resolve, ip, *mono_fcn.code, mono_fcn.arg_tys, mono_fcn.ret_ty, list, opt_level);

                MIR_Cleanup(resolve, ip, *mono_fcn.code, mono_fcn.arg_tys, mono_fcn.ret_ty);
            } else if (hir_fcn.m_code) {
                auto& mir = hir_fcn.m_code.get_mir_or_error_mut(Span());
                bool did_opt = MIR_OptimiseInline(resolve, ip, mir, hir_fcn.m_args, hir_fcn.m_return, list, opt_level);
                mir.trans_enum_state = ::MIR::EnumCachePtr(); // Clear MIR enum cache
                did_inline_on_pass |= did_opt;

                MIR_Cleanup(resolve, ip, mir, hir_fcn.m_args, hir_fcn.m_return);
            } else {
                // Extern, no optimisations
            }
        }
        num_iterations += 1;
    } while (did_inline_on_pass && num_iterations < max_iterations);

    if (did_inline_on_pass) {
        DEBUG("Stopped inlining after the level-specific maximum of " << max_iterations << " passes");
    }
}
