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
#include "trans_target.h"
#include <cmath>
#include <limits>
#include <unordered_map>
#include <unordered_set>
#include "trans_trans_list.h" // Note: This is included for inlining after enumeration and monomorph
#include "hir_expr.h" // The optimiser section accesses complete HIR expression nodes.

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
        unsigned partialIdx : 14; // If non-zero, it's a partial init/borrow

        VarState()
            : state(static_cast<unsigned>(ValState::Uninit))
            , partialIdx(0)
        {
        }

        VarState(ValState vs)
            : state(static_cast<unsigned>(vs))
            , partialIdx(0)
        {
        }
    };

    struct FcnState {
        VarState retval;
        std::vector<VarState> args;
        std::vector<VarState> locals;
        /// Mutable state flags for statics
        std::map<HIR::Path, VarState> staticMut;

        std::vector<VarState> inner;

        FcnState(size_t nArgs, size_t nLocals)
            : retval()
            , args(nArgs)
            , locals(nLocals)
        {
        }

        void checkInnerState(const ::MIR::TypeResolve& state, const MIR::LValue& lv, std::function<bool(ValState vs)> cb) const {
            const auto& valState = getState(state, lv);
            if (valState.partialIdx != 0) {
                // Recurse into all inner entries
            } else {
                if (!cb(static_cast<ValState>(valState.state))) {
                    // Error!
                    //MIR_BUG(state, "Borrow check failure: ");
                }
            }
        }

        const VarState& getStateRoot(const MIR::LValue::Storage& lvRoot) const {
            TU_MATCH_HDRA( (lvRoot), {)
            TU_ARMA(Return, e)
                return retval;
                TU_ARMA(Local, e)
                return locals.at(e);
                TU_ARMA(Argument, e)
                return args.at(e);
                TU_ARMA(Static, e) {
                    // TODO: If it's a static mut, return ValState::FullInit?
                    static const VarState vsStatic = VarState(ValState::Shared);
                    return vsStatic;
                }
            }
            throw "";
        }

        VarState& getStateRootMut(const MIR::LValue::Storage& lvRoot) {
            TU_MATCH_HDRA( (lvRoot), {)
            TU_ARMA(Return, e)
                return retval;
                TU_ARMA(Local, e)
                return locals.at(e);
                TU_ARMA(Argument, e)
                return args.at(e);
                TU_ARMA(Static, e) {
                    // TODO: If it's a static mut, return ValState::FullInit?
                    auto it = staticMut.find(e);
                    if (it == staticMut.end()) {
                        it = staticMut.insert(::std::make_pair(e.clone(), VarState(ValState::FullInit))).first;
                    }
                    return it->second;
                }
            }
            throw "";
        }

        const VarState& getState(const ::MIR::TypeResolve& state, const MIR::LValue& lv) const {
            const VarState* rv = &this->getStateRoot(lv.root);
            for (const auto& w : lv.wrappers) {
                if (rv->partialIdx == 0) {
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

        VarState* getStateMut(const ::MIR::TypeResolve& state, const MIR::LValue& lv, bool allowParent) {
            VarState* rv = &this->getStateRootMut(lv.root);
            for (const auto& w : lv.wrappers) {
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
                        if (allowParent) {
                            return rv;
                        }
                        return nullptr;
                    }
                }
            }
            return rv;
        }

        void setState(const ::MIR::TypeResolve& state, const MIR::LValue& lv, ValState target) {
            VarState* rv = &this->getStateRootMut(lv.root);
            for (const auto& w : lv.wrappers) {
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
            for (i = 0; i < lv.wrappers.size(); i++) {
                const auto& w = lv.wrappers[i];
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
                        rv = &this->inner[static_cast<size_t>(rv->partialIdx) + e];
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

        void moveLvalue(const ::MIR::TypeResolve& state, const MIR::LValue& lv) {
            // Must be `init` (or if Copy, `Shared`)
            if (state.lvalueIsCopy(lv)) {
                checkInnerState(state, lv, [&](const ValState vs) {
                    return vs == ValState::FullInit || vs == ValState::Shared;
                });
            } else {
                checkInnerState(state, lv, [&](const ValState vs) {
                    return vs == ValState::FullInit;
                });
                setState(state, lv, ValState::Uninit);
            }
        }

        void writeLvalue(const ::MIR::TypeResolve& state, const MIR::LValue& lv) {
            checkInnerState(state, lv, [&](const ValState vs) {
                return vs != ValState::Shared || vs != ValState::Frozen;
            });
            setState(state, lv, ValState::FullInit);
        }

        void borrowLvalue(const ::MIR::TypeResolve& state, ::HIR::BorrowType bt, const MIR::LValue& lv) {
            switch (bt) {
                case ::HIR::BorrowType::Owned:
                case ::HIR::BorrowType::Unique:
                    checkInnerState(state, lv, [&](const ValState vs) {
                        return vs == ValState::FullInit;
                    });
                    setState(state, lv, ValState::Frozen);
                    break;
                case ::HIR::BorrowType::Shared:
                    checkInnerState(state, lv, [&](const ValState vs) {
                        return vs == ValState::FullInit || vs == ValState::Shared;
                    });
                    setState(state, lv, ValState::Shared);
                    break;
            }
        }
    };

    struct StmtRef {
        MIR::BasicBlockId block;
        size_t stmtIdx;

        StmtRef(MIR::BasicBlockId bb, size_t stmt)
            : block(bb)
            , stmtIdx(stmt)
        {
        }
    };

    struct SubStmtRef {
        StmtRef stmt;
        size_t subIdx;

        SubStmtRef(StmtRef stmt, size_t subIdx)
            : stmt(stmt)
            , subIdx(subIdx)
        {
        }

        SubStmtRef(MIR::BasicBlockId bb, size_t stmt, size_t subIdx)
            : stmt(bb, stmt)
            , subIdx(subIdx)
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

        std::vector<LifetimeInfo> localLifetimes;

        struct LifetimeIvar {
            //unsigned    target_binding;
            std::vector<HIR::LifetimeRef> srcs;
            std::vector<HIR::LifetimeRef> dsts;
        };

        std::vector<LifetimeIvar> ivarLifetimes;

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
        void lifetimeAssign(const HIR::LifetimeRef& target, const HIR::LifetimeRef& src) {
            DEBUG(state << target << " = " << src);
            // One of these must be an ivar?
            // - Record the to/from for each.
            if (auto* iv = optIvar(target)) {
                iv->srcs.push_back(src);
            }
            if (auto* iv = optIvar(src)) {
                iv->dsts.push_back(target);
            }
        }

        void typeAssignPp(const HIR::PathParams& dst, const HIR::PathParams& src) {
            MIR_ASSERT(state, dst.mLifetimes.size() == src.mLifetimes.size(), "Param count error - " << dst << " == " << src);
            MIR_ASSERT(state, dst.types.size() == src.types.size(), "Param count error - " << dst << " == " << src);
            for (size_t i = 0; i < dst.mLifetimes.size(); i++) {
                lifetimeAssign(dst.mLifetimes[i], src.mLifetimes[i]);
            }
            for (size_t i = 0; i < dst.types.size(); i++) {
                typeAssign(dst.types[i], src.types[i]);
            }
        }

        void typeAssign(const HIR::TypeData* dstTy, const HIR::TypeData* srcTy) {
            MIR_ASSERT(state, dstTy->tag() == srcTy->tag(), dstTy << " != " << srcTy);
            TU_MATCH_HDRA( ((*dstTy), (*srcTy)),  { )
            TU_ARMA(Infer, de, se) MIR_BUG(state, "Unexpected infer - " << dstTy << ", " << srcTy);
                TU_ARMA(Generic, de, se) {
                }
                TU_ARMA(Diverge, de, se) {
                }
                TU_ARMA(Primitive, de, se) {
                }
                TU_ARMA(Borrow, de, se) {
                    lifetimeAssign(de.lifetime, se.lifetime);
                    typeAssign(de.inner, se.inner);
                }
                TU_ARMA(Pointer, de, se) {
                    typeAssign(de.inner, se.inner);
                }
                TU_ARMA(TraitObject, de, se) {
                    lifetimeAssign(de.lifetime, se.lifetime);
                    typeAssignPp(de.mTrait.mPath.mParams, se.mTrait.mPath.mParams);
                    // TODO: Markers
                }
                TU_ARMA(NodeType, de, se) MIR_BUG(state, "Unexpected NodeType");
                TU_ARMA(ErasedType, de, se) MIR_BUG(state, "Unexpected ErasedType");
                TU_ARMA(Path, de, se) {
                    MIR_ASSERT(state, de.binding == se.binding, dstTy << " != " << srcTy);
                    MIR_ASSERT(state, de.path.mData.tag() == se.path.mData.tag(), dstTy << " != " << srcTy);
                TU_MATCH_HDRA( (de.path.mData, se.path.mData), { )
                TU_ARMA(Generic, dpe, spe) {
                            typeAssignPp(dpe.mParams, spe.mParams);
                        }
                        TU_ARMA(UfcsInherent, dpe, spe) {
                            typeAssignPp(dpe.impl_params, spe.impl_params);
                            typeAssign(dpe.type, spe.type);
                            typeAssignPp(dpe.params, spe.params);
                        }
                        TU_ARMA(UfcsKnown, dpe, spe) {
                            typeAssignPp(dpe.trait.mParams, spe.trait.mParams);
                            typeAssign(dpe.type, spe.type);
                            typeAssignPp(dpe.params, spe.params);
                        }
                        TU_ARMA(UfcsUnknown, dpe, spe) MIR_BUG(state, "Unexpected UfcsUnknown - " << dstTy << ", " << srcTy);
                }
                }
                TU_ARMA(Array, de, se) {
                    typeAssign(de.inner, se.inner);
                }
                TU_ARMA(Slice, de, se) {
                    typeAssign(de.inner, se.inner);
                }
                TU_ARMA(Tuple, de, se) {
                    assert(de.size() == se.size());
                    for (size_t i = 0; i < de.size(); i++) {
                        typeAssign(de[i], se[i]);
                    }
                }
                TU_ARMA(NamedFunction, de, se) {
                    MIR_TODO(state, "NamedFunction MIR borrowcheck");
                }
                TU_ARMA(Function, de, se) {
                    MIR_ASSERT(state, de.argTypes.size() == se.argTypes.size(), "Arg count error");
                    for (size_t i = 0; i < de.argTypes.size(); i++) {
                        typeAssign(de.argTypes[i], se.argTypes[i]);
                    }
                    typeAssign(de.mRettype, se.mRettype);
                }
            }
        }

        void handleParam(const HIR::TypeData* target, const MIR::Param& param, size_t ofs) {
            if (const auto* b = param.opt_Borrow()) {
                HIR::TypeRef tmp;
                auto srcTy = state.getLvalueType(tmp, b->val);
                auto lft = borrowLvalue(ofs, b->type, b->val);
                typeAssign(target, state.crate.types.borrow(b->type, srcTy, lft));
            } else {
                HIR::TypeRef tmp;
                typeAssign(target, state.getParamType(tmp, param));
            }
        }

        void doAssign(const MIR::LValue& lv, const HIR::TypeData* srcTy) {
            HIR::TypeRef tmp;
            const auto& dstTy = state.getLvalueType(tmp, lv);
            typeAssign(dstTy, srcTy);
        }

        /// <summary>
        /// Borrow a lvalue, returning a lifetime reference created to point at the current position
        /// </summary>
        /// <param name="stmt_inner_ofs">Offset within the statement (e.g. argument index)</param>
        /// <param name="lv">LValue</param>
        HIR::LifetimeRef borrowLvalue(size_t stmtInnerOfs, HIR::BorrowType bt, const MIR::LValue& lv) {
            MIR::LValue::CRef lvr(lv);
            // Unwrap until a deref or the bottom value
            while (lvr.wrapper_count() > 0 && !lvr.is_Deref()) {
                lvr = lvr.innerRef();
            }

            TU_MATCH_HDRA( (lvr), { )
            TU_ARMA(Downcast, _)    throw "";
                TU_ARMA(Index, _) throw "";
                TU_ARMA(Field, _) throw "";

                TU_ARMA(Deref, _) {
                    HIR::TypeRef tmp;
                    const auto& innerTy = state.getLvalueType(tmp, lvr.innerRef());
                    if (const auto* tep = innerTy->opt_Borrow()) {
                        return tep->lifetime;
                    } else if (innerTy->is_Pointer()) {
                        // TODO: Return an unbound lifetime
                        return HIR::LifetimeRef::new_static();
                    } else {
                        MIR_BUG(state, "Unexpected type: " << innerTy);
                    }
                }

                TU_ARMA(Static, _)
                return HIR::LifetimeRef::new_static();
                TU_ARMA(Return, _)
                MIR_BUG(state, "Borrowing return slot");
                TU_ARMA(Local, _) {
                    // Allocate/find a local borrow reference for this slot
                    // - Record the entire lvalue for this borrow
                    return this->allocateLocal(SubStmtRef(state.getCurBlock(), state.getCurStmtOfs(), stmtInnerOfs), lv.clone());
                }
                TU_ARMA(Argument, _) {
                    // Allocate/find a local borrow reference for this slot
                    // - Record the entire lvalue for this borrow
                    return this->allocateLocal(SubStmtRef(state.getCurBlock(), state.getCurStmtOfs(), stmtInnerOfs), lv.clone());
                }
            }
            throw "";
        }

        HIR::LifetimeRef allocateIvar() {
            auto idx = ivarLifetimes.size();
            ivarLifetimes.push_back(LifetimeIvar());
            return HIR::LifetimeRef(static_cast<uint32_t>(idx + 0x14000));
        }

    private:
        HIR::LifetimeRef allocateLocal(SubStmtRef origin, MIR::LValue value) {
            DEBUG(state << "New local: " << value);
            auto idx = localLifetimes.size();
            localLifetimes.push_back(LifetimeInfo{origin, std::move(value)});
            assert(idx < (0x4000 - 0));
            return HIR::LifetimeRef(static_cast<uint32_t>(idx + 0x10000));
        }

        LifetimeIvar* optIvar(const HIR::LifetimeRef& lr) {
            if (0x14000 <= lr.binding) {
                return &ivarLifetimes.at(lr.binding - 0x14000);
            }
            return nullptr;
        }
    };
}

void MIRBorrowCheck(const StaticTraitResolve& resolve, const ::HIR::ItemPath& path, ::MIR::Function& fcn, const ::HIR::Function::argsT& args, const ::HIR::TypeData* ret_type) {
    static Span sp;
    TRACE_FUNCTION_F(path);
    ::MIR::TypeResolve state {
        sp, resolve, FMT_CB(ss, ss << path;), ret_type, args, fcn
    };

    DEBUG(FMT_CB(ss, MIRDumpFcn(ss, fcn)));

    BorrowState borrowState{state};

    // 0. Create liftime names/references for all borrows
    // - Each type instance gets its own new lifetime reference
    {
        TRACE_FUNCTION_FR("Fill", "Fill");

        struct LifetimeVisitor: public HIR::Visitor {
            const ::MIR::TypeResolve& state;
            BorrowState& borrowState;

            LifetimeVisitor(const ::MIR::TypeResolve& state, BorrowState& borrowState)
                : HIR::Visitor(nullptr, state.crate.types)
                , state(state)
                , borrowState(borrowState)
            {
            }

            void visitLifetimeRef(::HIR::LifetimeRef& lr) {
                if (lr.binding == ::HIR::LifetimeRef::UNKNOWN) {
                    lr = borrowState.allocateIvar();
                }
            }

            void visitPathParams(::HIR::PathParams& pp) override {
                for (auto& lr : pp.mLifetimes) {
                    visitLifetimeRef(lr);
                }
                HIR::Visitor::visitPathParams(pp);
            }

            void visitType(::HIR::TypeRef& t) override {
                auto data = t->cloneData();
                if (auto* te = data.opt_Borrow()) {
                    visitLifetimeRef(te->lifetime);
                } else if (auto* te = data.opt_TraitObject()) {
                    visitLifetimeRef(te->lifetime);
                } else if (data.is_ErasedType()) {
                    MIR_BUG(state, "Unexpected " << t);
                }
                HIR::Visitor::visitTypeData(data);
                t = state.crate.types.intern(mv$(data));
            };
        };

        struct V: public MIR::visit::VisitorMut {
            LifetimeVisitor lifetimes;

            V(const ::MIR::TypeResolve& state, BorrowState& borrowState)
                : lifetimes(state, borrowState)
            {
            }

            void visitType(::HIR::TypeRef& t) override {
                lifetimes.visitType(t);
            }
        } v{state, borrowState};

        v.visitFunction(state, fcn);
    }
    // - Run inference/assignment of lifetime references (between named lifetimes and borrows)
    {
        TRACE_FUNCTION_FR("Assign", "Assign");
        for (auto& blk : fcn.blocks) {
            for (auto& stmt : blk.statements) {
                state.setCurStmt(&blk - fcn.blocks.data(), &stmt - blk.statements.data());
                DEBUG(state << stmt);
                TU_MATCH_HDRA( (stmt), {)
                TU_ARMA(Assign, se) {
                    TU_MATCH_HDRA((se.src), {)
                    TU_ARMA(Use, rse) {
                                HIR::TypeRef tmp;
                                borrowState.doAssign(se.dst, state.getLvalueType(tmp, rse));
                            }
                            TU_ARMA(Borrow, rse) {
                                HIR::TypeRef tmp;
                                auto srcTy = state.getLvalueType(tmp, rse.val);
                                auto lft = borrowState.borrowLvalue(0, rse.type, rse.val);
                                borrowState.doAssign(se.dst, state.crate.types.borrow(rse.type, srcTy, lft));
                            }
                            TU_ARMA(Array, rse) {
                                HIR::TypeRef tmp;
                                const auto& dstTy = state.getLvalueType(tmp, se.dst)->as_Array().inner;
                                for (size_t i = 0; i < rse.vals.size(); i++) {
                                    borrowState.handleParam(dstTy, rse.vals[i], i);
                                }
                            }
                            TU_ARMA(SizedArray, rse) {
                                HIR::TypeRef tmp;
                                const auto& dstTy = state.getLvalueType(tmp, se.dst)->as_Array().inner;
                                borrowState.handleParam(dstTy, rse.val, 0);
                            }
                            TU_ARMA(Struct, rse) {
                                const auto& str = resolve.crate.getStructByPath(state.sp, rse.path.mPath);
                                MonomorphStatePtr ms(state.crate.types, nullptr, &rse.path.mParams, nullptr);
                                HIR::TypeRef tmp;
                                auto maybeMonomorph = [&](const auto& ty) -> const HIR::TypeData* {
                                    return resolve.monomorphExpandOpt(sp, tmp, ty, ms);
                                };
                                auto getFieldTy = [&](size_t fieldIndex) -> const HIR::TypeData* {
                            TU_MATCH_HDRA( (str.mData), {)
                            TU_ARMA(Unit, se) {
                                            MIR_BUG(state, "Field on unit-like struct - " << rse.path);
                                        }
                                        TU_ARMA(Tuple, se) {
                                            MIR_ASSERT(state, fieldIndex < se.size(), "Field index out of range in tuple-struct " << rse.path);
                                            return maybeMonomorph(se[fieldIndex].ent);
                                        }
                                        TU_ARMA(Named, se) {
                                            MIR_ASSERT(state, fieldIndex < se.size(), "Field index out of range in struct " << rse.path);
                                            return maybeMonomorph(se[fieldIndex].ty);
                                        }
                            }
                            throw "";
                                };
                                for (size_t i = 0; i < rse.vals.size(); i++) {
                                    borrowState.handleParam(getFieldTy(i), rse.vals[i], i);
                                }
                            }
                            TU_ARMA(EnumVariant, rse) {
                                const auto& enm = resolve.crate.getEnumByPath(state.sp, rse.path.mPath);
                                MonomorphStatePtr ms(state.crate.types, nullptr, &rse.path.mParams, nullptr);
                                HIR::TypeRef tmp;
                                //auto maybe_monomorph = [&](const auto& ty)->const HIR::TypeData* {
                                //    return resolve.monomorph_expand_opt(sp, tmp, ty, ms);
                                //};
                                if (rse.vals.size() > 0) {
                                    MIR_ASSERT(state, enm.mData.is_Data(), "");
                                    const auto& variants = enm.mData.as_Data();
                                    MIR_ASSERT(state, rse.index < variants.size(), "Variant index out of range for " << rse.path);
                                    const auto& variant = variants[rse.index];

                                    const auto& varTy = resolve.monomorphExpandOpt(sp, tmp, variant.type, MonomorphStatePtr(state.crate.types, nullptr, &rse.path.mParams, nullptr));
                                    const auto& str = *varTy->as_Path().binding.as_Struct();
                                    const auto& sPath = varTy->as_Path().path.mData.as_Generic();
                                    auto maybeMonomorph = [&](const HIR::TypeData* ty) -> const HIR::TypeData* {
                                        return resolve.monomorphExpandOpt(sp, tmp, ty, MonomorphStatePtr(state.crate.types, nullptr, &sPath.mParams, nullptr));
                                    };
                            TU_MATCH_HDRA( (str.mData), {)
                            TU_ARMA(Unit, se) {
                                        }
                                        TU_ARMA(Tuple, se) {
                                            MIR_ASSERT(state, se.size() == rse.vals.size(), "Field index out of range in tuple enum variant " << rse.path);
                                            for (size_t i = 0; i < rse.vals.size(); i++) {
                                                borrowState.handleParam(maybeMonomorph(se[i].ent), rse.vals[i], i);
                                            }
                                        }
                                        TU_ARMA(Named, se) {
                                            MIR_ASSERT(state, se.size() == rse.vals.size(), "Field index out of range in named enum variant " << rse.path);
                                            for (size_t i = 0; i < rse.vals.size(); i++) {
                                                borrowState.handleParam(maybeMonomorph(se[i].ty), rse.vals[i], i);
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
                                const auto& dstTy = state.getLvalueType(tmp, se.dst);
                                const auto& de = dstTy->as_Tuple();
                                MIR_ASSERT(state, de.size() == rse.vals.size(), "Tuple size and rvalue mismatch");
                                for (size_t i = 0; i < rse.vals.size(); i++) {
                                    borrowState.handleParam(de[i], rse.vals[i], i);
                                }
                            }
                            TU_ARMA(DstPtr, rse) {
                            }
                            TU_ARMA(DstMeta, rse) {
                                // TODO &'static for vtables
                            }
                            TU_ARMA(MakeDst, rse) {
                                HIR::TypeRef tmp;
                                const auto& dstTy = state.getLvalueType(tmp, se.dst);
                                if (dstTy->is_Borrow()) {
                                    if (rse.ptrVal.is_Borrow()) {
                                        // TODO: Make the borrow?
                                    } else {
                                        HIR::TypeRef tmp2;
                                        const auto& srcTy = state.getParamType(tmp2, rse.ptrVal);
                                        borrowState.lifetimeAssign(dstTy->as_Borrow().lifetime, srcTy->as_Borrow().lifetime);
                                    }
                                }
                            }
                            TU_ARMA(UniOp, rse) {
                            }
                            TU_ARMA(BinOp, rse) {
                            }
                            TU_ARMA(Constant, rse) {
                                borrowState.doAssign(se.dst, state.getConstType(rse));
                            }
                            TU_ARMA(Cast, rse) {
                                HIR::TypeRef tmp;
                                const auto& dstTy = state.getLvalueType(tmp, se.dst);
                                HIR::TypeRef tmp2;
                                const auto& srcTy = state.getLvalueType(tmp2, rse.val);
                                // Handle both being borrows
                                if (dstTy->is_Borrow() && srcTy->is_Borrow()) {
                                    borrowState.lifetimeAssign(dstTy->as_Borrow().lifetime, srcTy->as_Borrow().lifetime);
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
                    TU_ARMA(Asm, se) {
                    }
                    TU_ARMA(Asm2, se) {
                    }
                }
                // Note: Also need to pass through function calls, assignments, and structs
                // - Drop needs to be handled for anything with drop glue (as it counts as a use of contained borrows)
            }

            state.setCurStmtTerm(&blk - fcn.blocks.data());
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
                            const auto& ty = state.getLvalueType(tmp, fe);
                            const auto& fcn = ty->as_Function();
                            // TODO: HKTs
                            MIR_ASSERT(state, fcn.argTypes.size() == e.args.size(), "");
                            for (size_t i = 0; i < fcn.argTypes.size(); i++) {
                                borrowState.handleParam(fcn.argTypes[i], e.args[i], i);
                            }
                            borrowState.doAssign(e.retVal, fcn.mRettype);
                        }
                        TU_ARMA(Path, fe) {
                            HIR::TypeRef tmp;

                            MonomorphState ms(state.crate.types);
                            auto v = resolve.getValue(state.sp, fe, ms, true);
                            auto maybeMonomorph = [&](const ::HIR::TypeData* ty) -> const HIR::TypeData* {
                                return resolve.monomorphExpandOpt(state.sp, tmp, ty, ms);
                            };

                            const auto& fcn = *v.as_Function();
                            MIR_ASSERT(state, fcn.mArgs.size() <= e.args.size(), "");
                            for (size_t i = 0; i < fcn.mArgs.size(); i++) {
                                // Handle the param, unify types.
                                const auto& expTy = maybeMonomorph(fcn.mArgs[i].second);
                                DEBUG("ARG" << i << " " << expTy << " = " << e.args[i]);

                                borrowState.handleParam(expTy, e.args[i], i);
                            }
                            const auto& rvTy = maybeMonomorph(fcn.returnType);
                            DEBUG("RV" << " " << e.retVal << " = " << rvTy);
                            borrowState.doAssign(e.retVal, rvTy);
                        }
                }
                }
            }
        }
    }

    auto valStates = FcnState(args.size(), fcn.locals.size());

    // 1. Determine the lifetime (scope) of each variable (from assignment to last use)
    // - Needs to represent disjoint lifetimes (i.e. same variable assigned multiple times)
    // - Walk the graph, finding assignments of locals and tracking last use until next assignment
    //   > At next assignment (or drop/move), record that lifetime span (as start,path,end)

    // 2. Run full state tracking, including tracking of borrow sources.
    // TODO: Figure out the rest
}

void MIRBorrowCheckCrate(::HIR::Crate& crate) {
    ::MIR::OuterVisitor ov{crate, [&](const auto& res, const auto& p, ::HIR::ExprPtr& expr_ptr, const auto& args, const auto& ty) {
        MIRBorrowCheck(res, p, expr_ptr.getMirOrErrorMut(Span()), args, ty);
    }};
    ov.visitCrate(crate);
}


namespace {
    ::HIR::TypeRef getMetadataType(const ::MIR::TypeResolve& state, const ::HIR::TypeData* unsizedTy) {
        static Span sp;
        auto& types = state.crate.types;
        if (const auto* tep = unsizedTy->opt_TraitObject()) {
            const auto& trait_path = tep->mTrait;

            if (trait_path.mPath.mPath == ::HIR::SimplePath()) {
                return types.unit();
            } else {
                const auto& trait = *tep->mTrait.traitPtr;

                auto vtableTy = trait.getVtableType(state.sp, state.mResolve.crate, *tep);

                return types.borrow(HIR::BorrowType::Shared, vtableTy);
            }
        } else if (unsizedTy->is_Slice() || (unsizedTy->is_Primitive() && unsizedTy->as_Primitive() == HIR::CoreType::Str)) {
            return types.primitive(::HIR::CoreType::Usize);
        } else if (const auto* tep = unsizedTy->opt_Path()) {
            if (tep->binding.is_Struct()) {
                switch (tep->binding.as_Struct()->structMarkings.dst_type) {
                    case ::HIR::StructMarkings::DstType::None:
                        return ::HIR::TypeRef();
                    case ::HIR::StructMarkings::DstType::Possible: {
                        const auto& path = tep->path.mData.as_Generic();
                        const auto& str = *tep->binding.as_Struct();
                        auto monomorph = [&](const auto& tpl) {
                            auto rv = MonomorphStatePtr(types, nullptr, &path.mParams, nullptr).monomorphType(sp, tpl);
                            state.mResolve.expandAssociatedTypes(sp, rv);
                            return rv;
                        };
                        TU_MATCHA((str.mData), (se), (Unit, MIR_BUG(state, "Unit-like struct with DstType::Possible - " << unsizedTy);), (Tuple, return getMetadataType(state, monomorph(se.back().ent));), (Named, return getMetadataType(state, monomorph(se.back().ty));))
                        throw "";
                    }
                    case ::HIR::StructMarkings::DstType::Slice:
                        return types.primitive(::HIR::CoreType::Usize);
                    case ::HIR::StructMarkings::DstType::TraitObject:
                        return types.unit(); // TODO: Get the actual inner metadata type?
                }
            }
            return ::HIR::TypeRef();
        } else if (unsizedTy->is_Generic()) {
            ::HIR::Path p{unsizedTy, state.mResolve.mLangPointee, "Metadata"};
            auto rv = types.path(std::move(p), {});
            state.mResolve.expandAssociatedTypes(sp, rv);
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
void MIRValidateValState(::MIR::TypeResolve& state, const ::MIR::Function& fcn) {
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
            size_t mSize;
            std::vector<uint64_t> v;

            StateVec(size_t n = 0, State init = {})
                : mSize(n)
                , v((n + 31) / 32, uint64_t(init.v) * 0x5555555555555555ULL)
            {
                const auto used = n % 32;
                if (used != 0) {
                    v.back() |= ~((uint64_t(1) << (used * 2)) - 1);
                }
            }

            bool operator==(const StateVec& x) const {
                return mSize == x.mSize && v == x.v;
            }

            bool operator!=(const StateVec& x) const {
                return !(*this == x);
            }

            bool empty() const {
                return v.empty();
            }

            size_t size() const {
                return mSize;
            }

            class reference {
                uint64_t& slot;
                uint8_t bitOfs;
                State v;

                friend StateVec;

                reference(uint64_t& slot, uint8_t bitOfs)
                    : slot(slot)
                    , bitOfs(bitOfs)
                    , v((slot >> bitOfs) & 3)
                {
                }

            public:
                ~reference() {
                    slot = (slot & ~(uint64_t(3) << bitOfs)) | (uint64_t(v.v) << bitOfs);
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

        State retState = State::Invalid;
        StateVec args;
        StateVec locals;

        ValStates() {
        }

        ValStates(size_t nArgs, size_t nLocals)
            : args(nArgs, State::Valid)
            , locals(nLocals)
        {
        }

        explicit ValStates(const ValStates& v) = default;
        ValStates(ValStates&& v) = default;
        ValStates& operator=(const ValStates& v) = delete;
        ValStates& operator=(ValStates&& v) = default;

        void fmt(::std::ostream& os) const {
            os << "ValStates { ";
            switch (retState.v) {
                case State::Invalid:
                    break;
                case State::Either:
                    os << "?";
                case State::Valid:
                    os << "rv, ";
                    break;
            }
            auto fmtValRange = [&](const char* prefix, const StateVec& list) {
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
            fmtValRange("a", this->args);
            fmtValRange("_", this->locals);
            os << "}";
        }

        bool operator==(const ValStates& x) const {
            if (retState != x.retState) {
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

        bool merge(unsigned bbIdx, const ValStates& other) {
            DEBUG("bb" << bbIdx << " this=" << FMT_CB(ss, this->fmt(ss);) << ", other=" << FMT_CB(ss, other.fmt(ss);));
            if (*this == other) {
                return false;
            } else {
                bool rv = false;
                rv |= ValStates::mergeState(this->retState, other.retState);
                rv |= ValStates::mergeLists(this->args, other.args);
                rv |= ValStates::mergeLists(this->locals, other.locals);
                return rv;
            }
        }

        void markValidity(const ::MIR::TypeResolve& state, const ::MIR::LValue& lv, bool isValid) {
            if (!lv.wrappers.empty()) {
                return;
            }
            TU_MATCH_HDRA( (lv.root), {)
            TU_ARMA(Return, e) {
                    retState = isValid ? State::Valid : State::Invalid;
                }
                TU_ARMA(Argument, e) {
                    MIR_ASSERT(state, e < this->args.size(), "Argument index out of range " << lv);
                    DEBUG("arg$" << e << " = " << (isValid ? "Valid" : "Invalid"));
                    this->args[e] = isValid ? State::Valid : State::Invalid;
                }
                TU_ARMA(Local, e) {
                    MIR_ASSERT(state, e < this->locals.size(), "Local index out of range - " << lv);
                    DEBUG("_" << e << " = " << (isValid ? "Valid" : "Invalid"));
                    this->locals[e] = isValid ? State::Valid : State::Invalid;
                }
                TU_ARMA(Static, e) {
                }
            }
        }

        void ensureValid(const ::MIR::TypeResolve& state, const ::MIR::LValue& lv) {
            TU_MATCH_HDRA( (lv.root), {)
            TU_ARMA(Return, e) {
                    if (this->retState != State::Valid) {
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

            for(const auto& w : lv.wrappers)
            {
                if (w.is_Index()) {
                    if (this->locals[w.as_Index()] != State::Valid) {
                        MIR_BUG(state, "Use of non-valid lvalue - " << ::MIR::LValue::newLocal(w.as_Index()));
                    }
                }
            }
        }

        void moveVal(const ::MIR::TypeResolve& state, const ::MIR::LValue& lv) {
            ensureValid(state, lv);
            if (!state.lvalueIsCopy(lv)) {
                markValidity(state, lv, false);
            }
        }

        void moveVal(const ::MIR::TypeResolve& state, const ::MIR::Param& p) {
            if (const auto* e = p.opt_LValue()) {
                moveVal(state, *e);
            }
        }

    private:
        static bool mergeState(State& a, const State& b) {
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

        static bool mergeLists(StateVec& a, const StateVec& b) {
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

    ::std::vector<ValStates> blockStartStates(fcn.blocks.size());
    ::std::vector<bool> blockHasStartState(fcn.blocks.size());
    ::std::vector<bool> blockIsQueued(fcn.blocks.size());
    ::std::vector<unsigned int> toVisitBlocks;
    size_t nextBlockToVisit = 0;

    // TODO: Check that all used locals are also set (anywhere at all)

    auto addToVisit = [&](unsigned int idx, const ValStates& incoming) {
        auto& startState = blockStartStates.at(idx);
        bool changed;
        if (!blockHasStartState[idx]) {
            startState = ValStates(incoming);
            blockHasStartState[idx] = true;
            changed = true;
        } else {
            changed = startState.merge(idx, incoming);
        }
        if (changed && !blockIsQueued[idx]) {
            blockIsQueued[idx] = true;
            toVisitBlocks.push_back(idx);
        }
    };
    addToVisit(0, ValStates{state.mArgs.size(), fcn.locals.size()});
    while (nextBlockToVisit < toVisitBlocks.size()) {
        auto block = toVisitBlocks[nextBlockToVisit++];
        blockIsQueued[block] = false;
        assert(block < fcn.blocks.size());

        // 1. Copy the stable entry state. Incoming states are merged before a block is queued,
        // so each block is visited only when its entry state changes.
        auto valState = ValStates(blockStartStates.at(block));
        ASSERT_BUG(Span(), valState.locals.size() == fcn.locals.size(), "");
        DEBUG("BB" << block << " " << FMT_CB(ss, valState.fmt(ss);));

        // 2. Using the newly merged state, iterate statements checking the usage and updating state.
        const auto& bb = fcn.blocks[block];
        for (unsigned int stmtIdx = 0; stmtIdx < bb.statements.size(); stmtIdx++) {
            const auto& stmt = bb.statements[stmtIdx];
            state.setCurStmt(block, stmtIdx);

            DEBUG(state << stmt);
            switch (stmt.tag()) {
                case ::MIR::Statement::TAGDEAD:
                    throw "";
                case ::MIR::Statement::TAG_SetDropFlag:
                    MIR_ASSERT(state, stmt.as_SetDropFlag().idx < fcn.dropFlags.size(), "");
                    if (stmt.as_SetDropFlag().other != ~0u) {
                        MIR_ASSERT(state, stmt.as_SetDropFlag().other < fcn.dropFlags.size(), "");
                    }
                    break;
                case ::MIR::Statement::TAG_LoadDropFlag:
                    MIR_ASSERT(state, stmt.as_LoadDropFlag().idx < fcn.dropFlags.size(), "");
                    valState.ensureValid(state, stmt.as_LoadDropFlag().slot);
                    break;
                case ::MIR::Statement::TAG_SaveDropFlag:
                    MIR_ASSERT(state, stmt.as_SaveDropFlag().idx < fcn.dropFlags.size(), "");
                    valState.ensureValid(state, stmt.as_SaveDropFlag().slot);
                    break;
                case ::MIR::Statement::TAG_Asm:
                    for (const auto& v : stmt.as_Asm().inputs) {
                        valState.ensureValid(state, v.second);
                    }
                    for (const auto& v : stmt.as_Asm().outputs) {
                        valState.markValidity(state, v.second, true);
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
                                        valState.moveVal(state, *v.input);
                                    }
                                    if (v.output) {
                                        valState.markValidity(state, *v.output, true);
                                    }
                                }
                    }
                        }
                    }
                    break;
                case ::MIR::Statement::TAG_Assign:
                    // Destination must be valid
                    for (const auto& w : stmt.as_Assign().dst.wrappers) {
                        if (w.is_Deref()) {
                            // TODO: Check validity of the rest of the wrappers.
                        }
                        if (w.is_Index()) {
                            if (valState.locals[w.as_Index()] != ValStates::State::Valid) {
                                MIR_BUG(state, "Use of non-valid lvalue - " << ::MIR::LValue::newLocal(w.as_Index()));
                            }
                        }
                    }
                    // Check source (and invalidate sources)
                    TU_MATCH(
                        ::MIR::RValue,
                        (stmt.as_Assign().src),
                        (se),
                        (Use, valState.moveVal(state, se);),
                        (
                            Constant,
                            //(void)state.get_const_type(se);
                        ),
                        (SizedArray, valState.moveVal(state, se.val);),
                        (Borrow, valState.ensureValid(state, se.val);),
                        (
                            Cast,
                            // Well.. it's not exactly moved...
                            valState.ensureValid(state, se.val);
                            //val_state.move_val(state, se.val);
                        ),
                        (BinOp, valState.moveVal(state, se.valL); valState.moveVal(state, se.valR);),
                        (UniOp, valState.moveVal(state, se.val);),
                        (DstMeta, valState.ensureValid(state, se.val);),
                        (DstPtr, valState.ensureValid(state, se.val);),
                        (MakeDst,
                         //val_state.move_val(state, se.ptr_val);
                         if (const auto* e = se.ptrVal.opt_LValue()) valState.ensureValid(state, *e);
                         valState.moveVal(state, se.metaVal);),
                        (Tuple, for (const auto& v : se.vals) valState.moveVal(state, v);),
                        (Array, for (const auto& v : se.vals) valState.moveVal(state, v);),
                        (UnionVariant, valState.moveVal(state, se.val);),
                        (EnumVariant, for (const auto& v : se.vals) valState.moveVal(state, v);),
                        (Struct, for (const auto& v : se.vals) valState.moveVal(state, v);)
                    )
                    // Mark destination as valid
                    valState.markValidity(state, stmt.as_Assign().dst, true);
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
        state.setCurStmtTerm(block);
        DEBUG(state << bb.terminator);
        TU_MATCH_HDRA( (bb.terminator), { )
        TU_ARMA(Incomplete, e) {
                // Should be impossible here.
            }
            TU_ARMA(Return, e) {
                // Check if the return value has been set
                valState.ensureValid(state, ::MIR::LValue::newReturn());
                // Ensure that no other non-Copy values are valid
                for (unsigned int i = 0; i < valState.locals.size(); i++) {
                    if (valState.locals[i] == ValStates::State::Invalid) {
                    } else if (state.mResolve.typeIsCopy(state.sp, fcn.locals[i])) {
                    } else {
                        // TODO: Error, becuase this has just been leaked
                        // Can't error, as this doesn't know if the value has been partially moved out of (as this code doesn't track that detailed)
                        //MIR_BUG(state, "Value _" << i << ": " << fcn.locals[i] << " still valid?");
                    }
                }
            }
            TU_ARMA(UnwindResume, e) {
                // TODO: Ensure that cleanup has been performed.
            }
            TU_ARMA(UnwindTerminate, e) {
            }
            TU_ARMA(Unreachable, e) {
            }
            TU_ARMA(Goto, e) {
                // Push block with the new state
                addToVisit(e, valState);
            }
            TU_ARMA(If, e) {
                // Push blocks
                valState.ensureValid(state, e.cond);
                addToVisit(e.bbTrue, valState);
                addToVisit(e.bbFalse, valState);
            }
            TU_ARMA(Switch, e) {
                if (e.validFlag == ~0u) {
                    valState.ensureValid(state, e.val);
                } else {
                    MIR_ASSERT(state, e.validFlag < fcn.dropFlags.size(), "df" << e.validFlag << " out of range");
                    MIR_ASSERT(state, e.invalidTarget < fcn.blocks.size(), "Invalid conditional switch target");
                    addToVisit(e.invalidTarget, valState);
                }
                for (const auto& tgt : e.targets) {
                    addToVisit(tgt, valState);
                }
            }
            TU_ARMA(SwitchValue, e) {
                valState.ensureValid(state, e.val);
                for (const auto& tgt : e.targets) {
                    addToVisit(tgt, valState);
                }
                addToVisit(e.defTarget, valState);
            }
            TU_ARMA(Drop, e) {
                if (e.flagIdx == ~0u) {
                    valState.ensureValid(state, e.slot);
                } else {
                    MIR_ASSERT(state, e.flagIdx < fcn.dropFlags.size(), "");
                }
                valState.markValidity(state, e.slot, false);
                TU_IFLET(::MIR::UnwindAction, e.unwind, Cleanup, target,
                    addToVisit(target, valState);
                )
                addToVisit(e.target, valState);
            }
            TU_ARMA(Call, e) {
                if (e.fcn.is_Value()) {
                    valState.ensureValid(state, e.fcn.as_Value());
                }
                for (const auto& arg : e.args) {
                    valState.moveVal(state, arg);
                }
                TU_IFLET(::MIR::UnwindAction, e.unwind, Cleanup, target,
                    addToVisit(target, valState);
                )

                // TODO: If the function returns !, don't follow the ret_block
                valState.markValidity(state, e.retVal, true);
                addToVisit(e.retBlock, valState);
            }
        }
    }
}

void MIRValidate(const StaticTraitResolve& resolve, const ::HIR::ItemPath& path, const ::MIR::Function& fcn, const ::HIR::Function::argsT& args, const ::HIR::TypeData* ret_type) {
    TRACE_FUNCTION_F(path);
    Span sp;
    ::MIR::TypeResolve state {
        sp, resolve, FMT_CB(ss, ss << path;), ret_type, args, fcn
    };
    auto& types = resolve.crate.types;
    // Validation rules:

    if (debugEnabled()) {
        MIRDumpFcn(::std::cout, fcn, 0);
    }

    {
        HIR::TypeRef tySelf = types.self();
        HIR::PathParams emptyParamsI = resolve.implGenerics ? resolve.implGenerics->makeNopParams(types, 0) : HIR::PathParams();
        HIR::PathParams emptyParamsM = resolve.itemGenerics ? resolve.itemGenerics->makeNopParams(types, 1) : HIR::PathParams();
        MonomorphStatePtr m(types, tySelf, resolve.implGenerics ? &emptyParamsI : nullptr, resolve.itemGenerics ? &emptyParamsM : nullptr);
        for (const auto& ty : fcn.locals) {
            DEBUG("_" << (&ty - fcn.locals.data()) << ": " << ty);
            if (!monomorphiseTypeNeeded(ty)) {
                MIR_ASSERT(state, resolve.typeIsSized(sp, ty), "Local variable _" << (&ty - fcn.locals.data()) << ": " << ty << " isn't Sized");
            }
            m.monomorphType(sp, ty, /*allow_infer=*/false);
        }
    }

    {
        for (const auto& bb : fcn.blocks) {
            state.setCurStmtTerm(&bb - &fcn.blocks.front());
            MIR_ASSERT(state, bb.terminator.tag() != ::MIR::Terminator::TAGDEAD, "Moved terminator");
        }
    }
    // [CFA] = Control Flow Analysis
    // - [CFA] All code paths from bb0 must end with either a return or a diverge (or loop)
    //  - Requires checking the links between basic blocks, with a bitmap to catch loops/multipath
    {
        bool returns = false;
        ::std::vector<bool> visitedBbs(fcn.blocks.size());
        ::std::vector<unsigned int> toVisitBlocks;
        toVisitBlocks.push_back(0);
        while (toVisitBlocks.size() > 0) {
            auto block = toVisitBlocks.back();
            toVisitBlocks.pop_back();
            MIR_ASSERT(state, block < fcn.blocks.size(), "Ended up with BB out of range (" << block << " >= " << fcn.blocks.size() << ")");
            if (visitedBbs[block]) {
                continue;
            }
            visitedBbs[block] = true;

            state.setCurStmtTerm(block);

#define PUSH_BB(idx, desc)                                                     \
    do {                                                                       \
        if (!(idx < fcn.blocks.size()))                                        \
            MIR_BUG(state, "Invalid target block - " << desc << " bb" << idx); \
        if (visitedBbs[idx] == false) {                                       \
            toVisitBlocks.push_back(idx);                                    \
        }                                                                      \
    } while (0)
            TU_MATCH(
                ::MIR::Terminator,
                (fcn.blocks[block].terminator),
                (e),
                (Incomplete, MIR_BUG(state, "Encounterd `Incomplete` block in control flow - BB" << block);),
                (Return, returns = true;),
                (UnwindResume, ),
                (UnwindTerminate, ),
                (Unreachable, ),
                (Goto, PUSH_BB(e, "Goto");),
                (If, PUSH_BB(e.bbTrue, "If true"); PUSH_BB(e.bbFalse, "If false");),
                (Switch, for (unsigned int i = 0; i < e.targets.size(); i++) { PUSH_BB(e.targets[i], "Switch V" << i); }),
                (SwitchValue, for (unsigned int i = 0; i < e.targets.size(); i++) { PUSH_BB(e.targets[i], "SwitchValue " << i); } PUSH_BB(e.defTarget, "SwitchValue def");),
                (Drop, PUSH_BB(e.target, "Drop ret"); TU_IFLET(::MIR::UnwindAction, e.unwind, Cleanup, target, PUSH_BB(target, "Drop cleanup");)),
                (Call, PUSH_BB(e.retBlock, "Call ret"); TU_IFLET(::MIR::UnwindAction, e.unwind, Cleanup, target, PUSH_BB(target, "Call cleanup");))
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
        for (unsigned int bbIdx = 0; bbIdx < fcn.blocks.size(); bbIdx++) {
            const auto& bb = fcn.blocks[bbIdx];
            for (unsigned int stmtIdx = 0; stmtIdx < bb.statements.size(); stmtIdx++) {
                const auto& stmt = bb.statements[stmtIdx];
                state.setCurStmt(bbIdx, stmtIdx);
                DEBUG(state << stmt);

                switch (stmt.tag()) {
                    case ::MIR::Statement::TAGDEAD:
                        throw "";
                    case ::MIR::Statement::TAG_SetDropFlag:
                        break;
                    case ::MIR::Statement::TAG_SaveDropFlag:
                    case ::MIR::Statement::TAG_LoadDropFlag: {
                        const auto idx = stmt.is_SaveDropFlag() ? stmt.as_SaveDropFlag().idx : stmt.as_LoadDropFlag().idx;
                        MIR_ASSERT(state, idx < fcn.dropFlags.size(), "df" << idx << " out of range (nflags " << fcn.dropFlags.size() << ")");
                        const auto& slot = stmt.is_SaveDropFlag() ? stmt.as_SaveDropFlag().slot : stmt.as_LoadDropFlag().slot;
                        const auto bit = stmt.is_SaveDropFlag() ? stmt.as_SaveDropFlag().bitIndex : stmt.as_LoadDropFlag().bitIndex;
                        ::HIR::TypeRef slotTmp;
                        const auto& slotTy = state.getLvalueType(slotTmp, slot);
                        MIR_ASSERT(state, slotTy->is_Array(), "Save/Load Drop flag, slot not array: " << slotTy);
                        const auto& slotTyI = slotTy->as_Array();
                        MIR_ASSERT(state, slotTyI.inner == HIR::CoreType::U8, "Save/Load Drop flag, slot not u8 array: " << slotTy);
                        auto bytes = slotTyI.size.as_Known();
                        MIR_ASSERT(state, bit < bytes * 8, "Save/Load drop flag, bit index out of range " << bit << " >= " << bytes * 8);
                    } break;
                    case ::MIR::Statement::TAG_Assign: {
                        const auto& a = stmt.as_Assign();
                        ::HIR::TypeRef dstTmp;
                        const auto& dstTy = state.getLvalueType(dstTmp, a.dst);

                        auto checkTypes = [&](const auto& dstTy, const auto& srcTy) {
                            DEBUG("check_types: " << dstTy << " := " << srcTy);
                            if (srcTy == types.diverge()) {
                                // It's valid to assign to anything from a !
                            } else if (srcTy == dstTy || srcTy->equalsIgnoringRegions(dstTy)) {
                                // Types are equal, good.
                            } else {
                                MIR_BUG(
                                    state,
                                    "Type mismatch:\n"
                                        << " dst : " << dstTy << "\n"
                                        << " src : " << srcTy
                                );
                            }
                        };
                    TU_MATCH_HDRA( (a.src), {)
                    TU_ARMA(Use, e) {
                                ::HIR::TypeRef tmp;
                                checkTypes(dstTy, state.getLvalueType(tmp, e));
                            }
                            TU_ARMA(Constant, e) {
                                // TODO: Check constant types.
                        TU_MATCH_HDRA( (e), {)
                        TU_ARMA(Int, c) {
                                        bool good = false;
                                        if (dstTy->is_Primitive()) {
                                            switch (dstTy->as_Primitive()) {
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
                                            MIR_BUG(state, "Type mismatch, destination is " << dstTy << ", source is a signed integer");
                                        }
                                    }
                                    TU_ARMA(Uint, c) {
                                        bool good = false;
                                        if (dstTy->is_Primitive()) {
                                            switch (dstTy->as_Primitive()) {
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
                                            MIR_BUG(state, "Type mismatch, destination is " << dstTy << ", source is an unsigned integer");
                                        }
                                    }
                                    TU_ARMA(Float, c) {
                                        bool good = false;
                                        if (dstTy->is_Primitive()) {
                                            switch (dstTy->as_Primitive()) {
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
                                            MIR_BUG(state, "Type mismatch, destination is " << dstTy << ", source is a floating point value");
                                        }
                                    }
                                    TU_ARMA(Bool, c) {
                                        checkTypes(dstTy, types.primitive(::HIR::CoreType::Bool));
                                    }
                                    TU_ARMA(Bytes, c) {
                                        checkTypes(dstTy, types.borrow(::HIR::BorrowType::Shared, types.array(types.primitive(::HIR::CoreType::U8), c.size())));
                                    }
                                    TU_ARMA(StaticString, c) {
                                        checkTypes(dstTy, types.borrow(::HIR::BorrowType::Shared, types.primitive(::HIR::CoreType::Str)));
                                    }
                                    TU_ARMA(Const, c) {
                                        // TODO: Check result type against type of const
                                    }
                                    TU_ARMA(Generic, c) {
                                        // TODO: Check result type against type of const
                                    }
                                    TU_ARMA(Function, c) {
                                        MIR_ASSERT(state, dstTy->is_NamedFunction(), dstTy);
                                    }
                                    TU_ARMA(ItemAddr, c) {
                                        MonomorphState ms(types);
                                        auto v = state.mResolve.getValue(state.sp, *c, ms, /*sig_only=*/true);
                                        ::HIR::TypeRef tmp;
                            TU_MATCH_HDRA( (v), {)
                            TU_ARMA(NotFound, ve)
                                if( c->mData.is_UfcsInherent() && c->mData.as_UfcsInherent().item == "#type_id") {
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
                                                tmp = ms.monomorphType(state.sp, ve->mType);
                                                resolve.expandAssociatedTypes(state.sp, tmp);
                                                // TODO: Have a raw pointer flag
                                                if (const auto* te = dstTy->opt_Pointer()) {
                                                    checkTypes(te->inner, tmp);
                                                } else {
                                                    checkTypes(dstTy, types.borrow(::HIR::BorrowType::Shared, tmp));
                                                }
                                            }
                                            TU_ARMA(Function, ve) {
                                                MIR_ASSERT(state, dstTy->is_Function(), dstTy);
                                                // TODO: Check
                                            }
                                            TU_ARMA(EnumConstructor, ve) {
                                                MIR_ASSERT(state, dstTy->is_Function(), dstTy);
                                                // TODO: Check
                                            }
                                            TU_ARMA(StructConstructor, ve) {
                                                MIR_ASSERT(state, dstTy->is_Function(), dstTy);
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
                                if (e.isRaw) {
                                    checkTypes(dstTy, types.pointer(e.type, state.getLvalueType(tmp, e.val)));
                                } else {
                                    checkTypes(dstTy, types.borrow(e.type, state.getLvalueType(tmp, e.val)));
                                }
                            }
                            TU_ARMA(Cast, e) {
                                // Check return type
                                checkTypes(dstTy, e.type);

                                // TODO: Move this to a function shared by the HIR (typecheck validate) and here

                                ::HIR::TypeRef tmp;
                                const auto& srcTy = state.getLvalueType(tmp, e.val);
                                // Check suitability of source type (COMPLEX)
                        TU_MATCH_HDRA((*srcTy), {)
                        default:
                            MIR_BUG(state, "Invalid cast: " << dstTy << " from " << srcTy);
                                    // Path: Only value enums
                                    TU_ARMA(Path, sE) {
                                        MIR_ASSERT(state, sE.binding.is_Enum(), "Invalid cast: " << dstTy << " from " << srcTy);
                                        MIR_ASSERT(state, sE.binding.as_Enum()->isValue(), "Invalid cast: " << dstTy << " from " << srcTy);
                                        MIR_ASSERT(state, dstTy->is_Primitive(), "Invalid cast: " << dstTy << " from " << srcTy);
                                    }
                                    // Function pointers: can be casted to integers and to sized pointers
                                    TU_ARMA(Function, sE) {
                                        //TU_MATCH_HDRA((dst_ty.data()), {)
                                        //default:
                                        //    MIR_BUG(state, "Invalid cast: " << dst_ty << " from " << src_ty);
                                        //TU_ARMA(Primitive, d_e) {
                                        //    MIR_ASSERT(state, d_e == HIR::CoreType::Usize, "Invalid cast: " << dst_ty << " from " << src_ty);
                                        //    }
                                        //}
                                    }
                                    TU_ARMA(NamedFunction, sE) {
                                    }
                                    // Primitives: Can cast to thin pointers or to other primitives
                                    TU_ARMA(Primitive, sE) {
                                        MIR_ASSERT(state, sE != HIR::CoreType::Str, "Casting from `str` is invalid");
                            TU_MATCH_HDRA((*dstTy), {)
                            default:
                                MIR_BUG(state, "Invalid cast: " << dstTy << " from " << srcTy);
                                            TU_ARMA(Function, dE) {
                                                // Valid in MMIR generated from C
                                            }
                                            TU_ARMA(Pointer, dE) {
                                                switch (sE) {
                                                    case ::HIR::CoreType::Str:
                                                    case ::HIR::CoreType::Char:
                                                    case ::HIR::CoreType::F32:
                                                    case ::HIR::CoreType::F64:
                                                        MIR_BUG(state, "Invalid cast: " << dstTy << " from " << srcTy);
                                                        break;
                                                    default:
                                                        break;
                                                }
                                                auto dMeta = state.mResolve.metadataType(state.sp, dE.inner);
                                                MIR_ASSERT(state, dMeta == MetadataType::None || dMeta == MetadataType::Zero, "Casting primitive to invalid pointer type: " << dstTy << " from " << srcTy);
                                            }
                                            TU_ARMA(Primitive, dE) {
                                                MIR_ASSERT(state, dE != HIR::CoreType::Str, "Casting to `str` is invalid");
                                                if (dE == HIR::CoreType::Char)
                                                    MIR_ASSERT(state, sE == HIR::CoreType::U8, "Invalid cast: " << dstTy << " from " << srcTy);
                                            }
                            }
                                    }
                                    // Can cast to a matching raw pointer
                                    TU_ARMA(Borrow, sE) {
                                        MIR_ASSERT(state, dstTy->is_Pointer(), "Casting borrow to invalid type: " << dstTy << " from " << srcTy);
                                        MIR_ASSERT(state, dstTy->as_Pointer().type <= sE.type, "Casting borrow to invalid type: " << dstTy << " from " << srcTy);
                                        MIR_ASSERT(state,
                                            dstTy->as_Pointer().inner == sE.inner
                                                || dstTy->as_Pointer().inner->equalsIgnoringRegions(sE.inner),
                                            "Casting borrow to invalid type: " << dstTy << " from " << srcTy);
                                    }
                                    // Pointers: Can either be casted to another pointer, or to integers
                                    TU_ARMA(Pointer, sE) {
                                        auto sMeta = state.mResolve.metadataType(state.sp, sE.inner);
                            TU_MATCH_HDRA((*dstTy), {)
                            default:
                                MIR_BUG(state, "Invalid cast: " << dstTy << " from " << srcTy);
                                            TU_ARMA(Pointer, dE) {
                                                // Only valid if metadata matches, or destination is thin
                                                if (sE.inner != dE.inner) {
                                                    auto dMeta = state.mResolve.metadataType(state.sp, dE.inner);
                                                    if (dMeta != MetadataType::None && dMeta != MetadataType::Zero) {
                                                        if (dMeta != MetadataType::Unknown && sMeta != MetadataType::Unknown) {
                                                            MIR_ASSERT(state, dMeta == sMeta, "Casting has mismatched metadata: " << dstTy << " from " << srcTy << " (" << dMeta << " from " << sMeta << ")");
                                                        }
                                                    }
                                                }
                                            }
                                            TU_ARMA(Primitive, dE) {
                                                switch (dE) {
                                                    case ::HIR::CoreType::Str:
                                                    case ::HIR::CoreType::Char:
                                                    case ::HIR::CoreType::F32:
                                                    case ::HIR::CoreType::F64:
                                                        MIR_BUG(state, "Casting pointer to invalid type: " << dstTy << " from " << srcTy);
                                                        break;
                                                    default:
                                                        MIR_ASSERT(state, sMeta == MetadataType::None || sMeta == MetadataType::Zero, "Casting fat pointer to integer: " << dstTy << " from " << srcTy);
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
                                const auto& ty = state.getLvalueType(tmp, e.val);
                                const ::HIR::TypeData* ity = nullptr;
                                if ((ity = state.isTypeOwnedBox(ty)))
                                    ;
                                else if (ty->is_Borrow())
                                    ity = ty->as_Borrow().inner;
                                else if (ty->is_Pointer())
                                    ity = ty->as_Pointer().inner;
                                else {
                                    MIR_BUG(state, "DstMeta requires a &-ptr as input, got " << ty);
                                }
                                HIR::TypeRef resTy;
                                if (ity->is_Generic() || (ity->is_Path() && ity->as_Path().binding.is_Opaque()))
                                    ;
                                else if (ity->is_Array()) {
                                    resTy = state.crate.types.primitive(HIR::CoreType::Usize);
                                } else if (ity->is_Slice()) {
                                    resTy = state.crate.types.primitive(HIR::CoreType::Usize);
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
                                const auto& ty = state.getLvalueType(tmp, e.val);
                                const ::HIR::TypeData* ity = nullptr;
                                if ((ity = state.isTypeOwnedBox(ty)))
                                    ;
                                else if (ty->is_Borrow())
                                    ity = ty->as_Borrow().inner;
                                else if (ty->is_Pointer())
                                    ity = ty->as_Pointer().inner;
                                else {
                                    MIR_BUG(state, "DstPtr requires a &-ptr as input, got " << ty);
                                }
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
                                if (TU_TEST2(e.metaVal, Constant, , ItemAddr, .get() == nullptr)) {
                                    // TODO: Check the validity?
                                    // - Ensure that something is generic in either the destination or source
                                    //::HIR::TypeRef  tmp;
                                    //const auto& src_ty = state.get_param_type(tmp, e.ptr_val);
                                    //MIR_ASSERT(state, monomorphise_type_needed(src_ty), "MakeDst Unsize with known source - " << src_ty);
                                    break;
                                }
                                const ::HIR::TypeData* ity = nullptr;
                                if (const auto* te = dstTy->opt_Borrow())
                                    ity = te->inner;
                                else if (const auto* te = dstTy->opt_Pointer())
                                    ity = te->inner;
                                else {
                                    MIR_BUG(state, "MakeDst requires a pointer as output, got " << dstTy);
                                }
                                assert(ity);
                                auto meta = getMetadataType(state, ity);
                                if (meta == ::HIR::TypeRef()) {
                                    // In 1.90, this gets used for thin pointers too
                                    meta = types.unit();
                                }
// TODO: Check metadata type?
// > Borrows vs pointers are fun

                                // NOTE: Output type checked above.
                            }
                            TU_ARMA(Tuple, e) {
                                if (!dstTy->is_Tuple())
                                    MIR_BUG(state, "Tuple assigned slot of invalid type, " << dstTy);
                                const auto& dstItys = dstTy->as_Tuple();
                                if (dstItys.size() != e.vals.size())
                                    MIR_BUG(state, "Tuple assigned slot of invalid type, " << dstTy << " - expected " << e.vals.size() << " elements");
                                for (size_t i = 0; i < e.vals.size(); i++) {
                                    ::HIR::TypeRef tmp2;
                                    checkTypes(dstItys[i], state.getParamType(tmp2, e.vals[i]));
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
                case ::MIR::Statement::TAG_ScopeEnd:
                        // TODO: Mark listed values as descoped
                        break;
                }
            }

            state.setCurStmtTerm(bbIdx);
            DEBUG(state << bb.terminator);
            TU_MATCH_HDRA( (bb.terminator), {)
            TU_ARMA(Incomplete, e) {
                }
                TU_ARMA(Return, e) {
                    // TODO: Check if the function can return (i.e. if its return type isn't an empty type)
                }
                TU_ARMA(UnwindResume, e) {
                }
                TU_ARMA(UnwindTerminate, e) {
                }
                TU_ARMA(Unreachable, e) {
                }
                TU_ARMA(Goto, e) {
                }
                TU_ARMA(If, e) {
                    // Check that condition lvalue is a bool
                    ::HIR::TypeRef tmp;
                    const auto& ty = state.getLvalueType(tmp, e.cond);
                    if (ty != ::HIR::CoreType::Bool) {
                        MIR_BUG(state, "Type mismatch in `If` - expected bool, got " << ty);
                    }
                }
                TU_ARMA(Switch, e) {
                    // Check that the condition is an enum
                    MIR_ASSERT(state, (e.validFlag == ~0u) == (e.invalidTarget == ~0u), "Conditional switch flag/target mismatch");
                    if (e.validFlag != ~0u) {
                        MIR_ASSERT(state, e.validFlag < fcn.dropFlags.size(), "Conditional switch flag out of range");
                        MIR_ASSERT(state, e.invalidTarget < fcn.blocks.size(), "Conditional switch target out of range");
                    }
                }
                TU_ARMA(SwitchValue, e) {
                    // Check that the condition's type matches the values
                }
                TU_ARMA(Drop, e) {
                    if (e.slot.is_Deref()) {
                        HIR::TypeRef tmp;
                        const auto& ty = state.getLvalueType(tmp, e.slot, 1);
                        if (ty->is_Borrow()) {
                            MIR_ASSERT(state, ty->as_Borrow().type != HIR::BorrowType::Shared, "Dropping through non-owned pointer: " << ty);
                        }
                    }
                    MIR_ASSERT(state, e.target < fcn.blocks.size(), "Drop target out of range");
                    if (e.flagIdx != ~0u) {
                        MIR_ASSERT(state, e.flagIdx < fcn.dropFlags.size(), "Drop flag out of range");
                    }
                    TU_IFLET(::MIR::UnwindAction, e.unwind, Cleanup, target,
                        MIR_ASSERT(state, target < fcn.blocks.size(), "Drop cleanup target out of range");
                    )
                }
                TU_ARMA(Call, e) {
                    if (e.fcn.is_Value()) {
                        ::HIR::TypeRef tmp;
                        const auto& ty = state.getLvalueType(tmp, e.fcn.as_Value());
                        if (!ty->is_Function()) {
                            MIR_BUG(state, "Call Fcn::Value with non-function type - " << ty);
                        }
                        // NOTE: VTable functions use this, and have a little bit of type shenanigans going on
                    } else if (e.fcn.is_Path()) {
                        const auto& p = e.fcn.as_Path();

                        MonomorphState outParams(types);
                        outParams.setConstevalState(state.crate, HIR::ItemPath(p));
                        const auto& sig = state.mResolve.getValue(sp, p, outParams, /*sig_only=*/true);
                        MIR_ASSERT(state, sig.is_Function(), "Call Fcn::Path with non-function value - " << p << " is " << sig.tagStr());
                        const auto& fcn = *sig.as_Function();

                        ::HIR::TypeRef tmp1;
                        ::HIR::TypeRef tmp2;
                        auto maybeMonomorph = [&](const ::HIR::TypeData* ty) -> const ::HIR::TypeData* {
                            if (true || monomorphiseTypeNeeded(ty)) {
                                tmp2 = outParams.monomorphType(sp, ty);
                                state.mResolve.expandAssociatedTypes(sp, tmp2);
                                return tmp2;
                            } else {
                                return ty;
                            }
                        };
                        // Check arguments
                        if (fcn.variadic) {
                            MIR_ASSERT(state, e.args.size() >= fcn.mArgs.size(), "");
                        } else {
                            MIR_ASSERT(state, e.args.size() == fcn.mArgs.size(), "");
                        }
                        for (size_t i = 0; i < fcn.mArgs.size(); i++) {
                            const auto& inTy = state.getParamType(tmp1, e.args[i]);
                            const auto& expTy = maybeMonomorph(fcn.mArgs[i].second);
                            DEBUG("Arg " << i << " " << inTy << " ?= " << expTy);
                            if (inTy == types.diverge()) {
                                // It's valid to assign to anything from a !
                            } else if (inTy == expTy || inTy->equalsIgnoringRegions(expTy)) {
                                // Types are equal, good.
                            } else {
                                MIR_BUG(state, "Argument (" << i << ") type mismatch: input is " << inTy << ", but expected is " << expTy);
                            }
                        }
                        // Check return
                        const auto& slotTy = state.getLvalueType(tmp1, e.retVal);
                        const auto& expTy = maybeMonomorph(fcn.returnType);
                        DEBUG("Ret " << slotTy << " ?= " << expTy);
                        if (!expTy->is_Diverge()) {
                            MIR_ASSERT(state, slotTy == expTy || slotTy->equalsIgnoringRegions(expTy), "Return type mismatch: slot is " << slotTy << ", but return is " << expTy);
                        }
                    }
                    // Typecheck arguments and return value
                }
            }
        }
    }

    // [ValState] = Value state tracking (use after move, uninit, ...)
    MIRValidateValState(state, fcn);
}

// --------------------------------------------------------------------

void MIRCheckCrate(/*const*/ ::HIR::Crate& crate) {
    ::MIR::OuterVisitor ov(crate, [](const auto& res, const auto& p, auto& expr, const auto& args, const auto& ty) {
        MIRValidate(res, p, *expr.mir, args, ty);
    });
    ov.visitCrate(crate);
}


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

        bool isComposite() const {
            return index != 0 && index != ~0u;
        }

        bool isValid() const {
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
        State returnValue;
        ::std::vector<State> args;
        ::std::vector<State> locals;
        ::std::vector<bool> dropFlags;

        ::std::vector<::std::vector<State>> innerStates;

        ::std::vector<unsigned int> bbPath;

        ValueStates clone() const {
            struct H {
                static ::std::vector<State> cloneStateList(const ::std::vector<State>& l) {
                    ::std::vector<State> rv;
                    rv.reserve(l.size());
                    for (const auto& s : l) {
                        rv.push_back(State(s));
                    }
                    return rv;
                }
            };

            ValueStates rv;
            rv.returnValue = State(this->returnValue);
            rv.args = H::cloneStateList(this->args);
            rv.locals = H::cloneStateList(this->locals);
            rv.dropFlags = this->dropFlags;
            rv.innerStates.reserve(this->innerStates.size());
            for (const auto& isl : this->innerStates) {
                rv.innerStates.push_back(H::cloneStateList(isl));
            }
            rv.bbPath = this->bbPath;
            return *this;
        }

        bool isEquivalentTo(const ValueStates& x) const {
            struct H {
                static bool equal(const ValueStates& vssA, const State& a, const ValueStates& vssB, const State& b) {
                    if (a.index == 0) {
                        return b.index == 0;
                    }
                    if (a.index == ~0u) {
                        return b.index == ~0u;
                    }
                    if (b.index == 0 || b.index == ~0u) {
                        return false;
                    }

                    const auto& statesA = vssA.innerStates.at(a.index - 1);
                    const auto& statesB = vssB.innerStates.at(b.index - 1);
                    // NOTE: If there's two differen variants, this can happen.
                    if (statesA.size() != statesB.size()) {
                        return false;
                    }

                    for (size_t i = 0; i < statesA.size(); i++) {
                        if (!H::equal(vssA, statesA[i], vssB, statesB[i])) {
                            return false;
                        }
                    }
                    // If the above loop didn't early exit, the two states are equal
                    return true;
                }
            };

            if (this->dropFlags != x.dropFlags) {
                return false;
            }
            if (!H::equal(*this, returnValue, x, x.returnValue)) {
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

        StateFmt fmtState(const ::MIR::TypeResolve& mir_res, const ::MIR::LValue& lv) const {
            return StateFmt(*this, getLvalueState(mir_res, lv));
        }

        void ensureParamValid(const ::MIR::TypeResolve& mir_res, const ::MIR::Param& lv) const {
            if (const auto* e = lv.opt_LValue()) {
                this->ensureLvalueValid(mir_res, *e);
            }
        }

        void ensureLvalueValid(const ::MIR::TypeResolve& mir_res, const ::MIR::LValue& lv) const {
            const auto& vs = getLvalueState(mir_res, lv);
            ::std::vector<unsigned int> path;
            ensureValid(mir_res, lv, vs, path);
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

        InvalidReason findInvalidReason(const ::MIR::TypeResolve& mir_res, const ::MIR::LValue& rootLv) const {
            using ::MIR::visit::ValUsage;
            using ::MIR::visit::visitMirLvalues;

            ::HIR::TypeRef tmp;
            bool is_copy = mir_res.lvalueIsCopy(rootLv);
            size_t curStmt = mir_res.getCurStmtOfs();

            // Dump all statements
            if (true) {
                for (size_t i = 0; i < this->bbPath.size() - 1; i++) {
                    size_t bbIdx = this->bbPath[i];
                    const auto& bb = mir_res.fcn.blocks.at(bbIdx);

                    for (size_t stmtIdx = 0; stmtIdx < bb.statements.size(); stmtIdx++) {
                        DEBUG("BB" << bbIdx << "/" << stmtIdx << " - " << bb.statements[stmtIdx]);
                    }
                    DEBUG("BB" << bbIdx << "/TERM - " << bb.terminator);
                }

                {
                    size_t bbIdx = this->bbPath.back();
                    const auto& bb = mir_res.fcn.blocks.at(bbIdx);
                    for (size_t stmtIdx = 0; stmtIdx < curStmt; stmtIdx++) {
                        DEBUG("BB" << bbIdx << "/" << stmtIdx << " - " << bb.statements[stmtIdx]);
                    }
                }
            }

            if (!is_copy) {
                // Walk backwards through the BBs and find where it's used by value
                assert(this->bbPath.size() > 0);
                size_t bbIdx;
                size_t stmtIdx;

                bool wasMoved = false;
                size_t movedBb, movedStmt;
                auto visitCb = [&](const auto& lv, auto vu) {
                    // If this is a move that touches the slot of interest (in part or full)
                    // e.g. if `root_lv` is `_1.0` then `_1` and `_1.0*` should be handled, but `_1.1` should not
                    if (lv.isEitherSubset(rootLv) && vu == ValUsage::Move) {
                        wasMoved = true;
                        movedBb = bbIdx;
                        movedStmt = stmtIdx;
                        return false;
                    }
                    return false;
                };
                // Most recent block (incomplete)
                {
                    bbIdx = this->bbPath.back();
                    const auto& bb = mir_res.fcn.blocks.at(bbIdx);
                    for (stmtIdx = curStmt; stmtIdx-- && !wasMoved;) {
                        visitMirLvalues(bb.statements[stmtIdx], visitCb);
                    }
                }
                for (size_t i = this->bbPath.size() - 1; i-- && !wasMoved;) {
                    bbIdx = this->bbPath[i];
                    const auto& bb = mir_res.fcn.blocks.at(bbIdx);
                    stmtIdx = bb.statements.size();

                    visitMirLvalues(bb.terminator, visitCb);

                    for (stmtIdx = bb.statements.size(); stmtIdx-- && !wasMoved;) {
                        visitMirLvalues(bb.statements[stmtIdx], visitCb);
                    }
                }

                if (wasMoved) {
                    // Reason found, the value was moved
                    DEBUG("- Moved in BB" << movedBb << "/" << movedStmt);
                    return InvalidReason{InvalidReason::Moved, movedBb, movedStmt};
                }
            } else {
                // Walk backwards to find assignment (if none, it's never initialized)
                assert(this->bbPath.size() > 0);
                size_t bbIdx;
                size_t stmtIdx;

                bool assigned = false;
                auto visitCb = [&](const auto& lv, auto vu) {
                    if (lv.isEitherSubset(rootLv) && vu == ValUsage::Write) {
                        assigned = true;
                        //assigned_bb = this->bb_path[i];
                        //assigned_stmt = j;
                        return true;
                    }
                    return false;
                };

                // Most recent block (incomplete)
                {
                    bbIdx = this->bbPath.back();
                    const auto& bb = mir_res.fcn.blocks.at(bbIdx);
                    for (stmtIdx = curStmt; stmtIdx-- && !assigned;) {
                        visitMirLvalues(bb.statements[stmtIdx], visitCb);
                    }
                }
                for (size_t i = this->bbPath.size() - 1; i-- && !assigned;) {
                    bbIdx = this->bbPath[i];
                    const auto& bb = mir_res.fcn.blocks.at(bbIdx);
                    stmtIdx = bb.statements.size();

                    visitMirLvalues(bb.terminator, visitCb);

                    for (stmtIdx = bb.statements.size(); stmtIdx-- && !assigned;) {
                        visitMirLvalues(bb.statements[stmtIdx], visitCb);
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

        void ensureValid(const ::MIR::TypeResolve& mir_res, const ::MIR::LValue& rootLv, const State& vs, ::std::vector<unsigned int>& path) const {
            if (vs.isComposite()) {
                MIR_ASSERT(mir_res, vs.index - 1 < this->innerStates.size(), "");
                const auto& states = this->innerStates.at(vs.index - 1);

                path.push_back(0);
                for (const auto& innerVs : states) {
                    ensureValid(mir_res, rootLv, innerVs, path);
                    path.back()++;
                }
                path.pop_back();
            } else if (!vs.isValid()) {
                // Locate where it was invalidated.
                auto reason = findInvalidReason(mir_res, rootLv);
                MIR_BUG(mir_res, "Accessing invalidated lvalue - " << rootLv << " - " << FMT_CB(s, reason.fmt(s);) << " - field path=[" << path << "], BBs=[" << this->bbPath << "]");
            } else {
            }
        }

    public:
        void moveLvalue(const ::MIR::TypeResolve& mir_res, const ::MIR::LValue& lv) {
            this->ensureLvalueValid(mir_res, lv);

            if (mir_res.lvalueIsCopy(lv)) {
                // NOTE: Copy types aren't moved.
            } else {
                this->setLvalueState(mir_res, lv, State(false));
            }
        }

        void markLvalueValid(const ::MIR::TypeResolve& mir_res, const ::MIR::LValue& lv) {
            this->setLvalueState(mir_res, lv, State(true));
        }

        // Scan states and clear unused composite slots
        void garbageCollect() {
            struct Marker {
                ::std::vector<bool> used;

                void markFromState(const ValueStates& vss, const State& s) {
                    if (s.isComposite()) {
                        used.at(s.index - 1) = true;
                        for (const auto& is : vss.innerStates.at(s.index - 1)) {
                            markFromState(vss, is);
                        }

                        // TODO: Should this compact composites with all-equal inner states?
                    }
                }
            };

            Marker m;
            m.used.resize(this->innerStates.size(), false);

            m.markFromState(*this, this->returnValue);
            for (const auto& s : this->args) {
                m.markFromState(*this, s);
            }
            for (const auto& s : this->locals) {
                m.markFromState(*this, s);
            }
        }

    private:
        ::std::vector<State>& allocateCompositeInt(State& outState) {
            // 1. Search for an unused (empty) slot
            for (size_t i = 0; i < this->innerStates.size(); i++) {
                if (this->innerStates[i].size() == 0) {
                    outState = State(i);
                    return innerStates[i];
                }
            }
            // 2. If none avaliable, allocate a new slot
            auto idx = innerStates.size();
            innerStates.push_back({});
            outState = State(idx);
            return innerStates.back();
        }

        State allocateComposite(unsigned int nFields, const State& basis) {
            assert(nFields > 0);
            assert(!basis.isComposite());

            State rv;
            auto& subStates = allocateCompositeInt(rv);
            assert(subStates.size() == 0);

            subStates.reserve(nFields);
            while (nFields--) {
                subStates.push_back(State(basis));
            }

            return rv;
        }

    public:
        ::std::vector<State>& getComposite(const ::MIR::TypeResolve& mir_res, const State& vs) {
            MIR_ASSERT(mir_res, vs.index != 0, "No inner state");
            MIR_ASSERT(mir_res, vs.index - 1 < this->innerStates.size(), "Inner state index out of range - " << vs.index - 1 << " >= " << this->innerStates.size());
            return this->innerStates.at(vs.index - 1);
        }

        const ::std::vector<State>& getComposite(const ::MIR::TypeResolve& mir_res, const State& vs) const {
            MIR_ASSERT(mir_res, vs.index != 0, "No inner state");
            MIR_ASSERT(mir_res, vs.index - 1 < this->innerStates.size(), "Inner state index out of range - " << vs.index - 1 << " >= " << this->innerStates.size());
            return this->innerStates.at(vs.index - 1);
        }

        const State& getLvalueState(const ::MIR::TypeResolve& mir_res, const ::MIR::LValue& lv) const {
            const State* stateP = nullptr;
            TU_MATCHA((lv.root), (e), (Return, stateP = &returnValue;), (Argument, stateP = &args.at(e);), (Local, stateP = &locals.at(e);), (Static, static State stateOfStatic(true); return stateOfStatic;))

            for (const auto& w : lv.wrappers) {
                if (w.is_Index()) {
                    const auto& vsI = getLvalueState(mir_res, ::MIR::LValue::newLocal(w.as_Index()));
                    MIR_ASSERT(mir_res, vsI.isValid(), "Indexing with an invalidated value");
                }
            }
            for (const auto& w : lv.wrappers) {
                if (!stateP->isComposite()) {
                    // Not a composite, stop immediately
                    break;
                }
                const auto& vs = *stateP;
                stateP = nullptr;

                TU_MATCHA(
                    (w),
                    (e),
                    (Field, const auto& states = this->getComposite(mir_res, vs); MIR_ASSERT(mir_res, e < states.size(), "Field index out of range"); stateP = &states[e];),
                    (Deref,
                     //MIR_TODO(mir_res, "Deref with composite state - " << lv);
                     const auto& states = this->getComposite(mir_res, vs);
                     MIR_ASSERT(mir_res, states.size() == 2, "Deref on composite of invalid size - " << StateFmt(*this, vs));
                     stateP = &states[1];),
                    (Index, MIR_BUG(mir_res, "Indexing a composite state");),
                    (Downcast, const auto& states = this->getComposite(mir_res, vs); MIR_ASSERT(mir_res, states.size() == 1, "Downcast on composite of invalid size - " << StateFmt(*this, vs)); stateP = &states[0];)
                )
                assert(stateP);
            }
            return *stateP;
        }

        void clearState(const ::MIR::TypeResolve& mir_res, State& s) {
            if (s.isComposite()) {
                auto& subStates = this->getComposite(mir_res, s);
                for (auto& ss : subStates) {
                    this->clearState(mir_res, ss);
                }
                subStates.clear();
            }
        }

        void setLvalueState(const ::MIR::TypeResolve& mir_res, const ::MIR::LValue& lv, State newVs) {
            TRACE_FUNCTION_F(lv << " = " << StateFmt(*this, newVs) << " (from " << StateFmt(*this, getLvalueState(mir_res, lv)) << ")");
            State* stateP = nullptr;
            TU_MATCHA((lv.root), (e), (Return, stateP = &returnValue;), (Argument, stateP = &args.at(e);), (Local, stateP = &locals.at(e);), (Static, return;))

            for (const auto& w : lv.wrappers) {
                auto& curVs = *stateP;

                // If this is not a composite, and it matches the new state
                if (!curVs.isComposite() && curVs == newVs) {
                    // Early return
                    return;
                }

                stateP = nullptr;
                TU_MATCH_HDRA( (w), {)
                TU_ARMA(Field, e) {
                        // Current isn't a composite, we need to change that
                        if (!curVs.isComposite()) {
                            ::HIR::TypeRef tmp;
                            const auto& ty = mir_res.getLvalueType(tmp, lv, /*wrapper_skip_count=*/(1 + &lv.wrappers.back() - &w));
                            unsigned int nFields = 0;
                            if (const auto* e = ty->opt_Tuple()) {
                                nFields = e->size();
                            }
                            // TODO: Fixed-size arrays
                            else if (ty->is_Path() && ty->as_Path().binding.is_Struct()) {
                                const auto& e = ty->as_Path().binding.as_Struct();
                                TU_MATCHA((e->mData), (se), (Unit, nFields = 0;), (Tuple, nFields = se.size();), (Named, nFields = se.size();))
                            } else {
                                MIR_BUG(mir_res, "Unknown type being accessed with Field " << lv << ": " << ty);
                            }

                            curVs = State(this->allocateComposite(nFields, curVs));
                        }
                        // Get composite state and assign into it
                        auto& states = this->getComposite(mir_res, curVs);
                        MIR_ASSERT(mir_res, e < states.size(), "Field index out of range");
                        stateP = &states[e];
                    }
                    TU_ARMA(Deref, e) {
                        if (!curVs.isComposite()) {
                            curVs = State(this->allocateComposite(2, curVs));
                        }
                        // Get composite state and assign into it
                        auto& states = this->getComposite(mir_res, curVs);
                        MIR_ASSERT(mir_res, states.size() == 2, "Deref with invalid state list size");
                        stateP = &states[1];
                    }
                    TU_ARMA(Index, e) {
                        const auto& vsI = getLvalueState(mir_res, ::MIR::LValue::newLocal(e));
                        MIR_ASSERT(mir_res, !curVs.isComposite(), "");
                        MIR_ASSERT(mir_res, !vsI.isComposite(), "");

                        MIR_ASSERT(mir_res, curVs.isValid(), "Indexing an invalid value");
                        MIR_ASSERT(mir_res, vsI.isValid(), "Indexing with an invalid index");

                        // NOTE: Ignore
                        return;
                    }
                    TU_ARMA(Downcast, e) {
                        if (!curVs.isComposite()) {
                            curVs = State(this->allocateComposite(1, curVs));
                        }
                        // Get composite state and assign into it
                        auto& states = this->getComposite(mir_res, curVs);
                        MIR_ASSERT(mir_res, states.size() == 1, "Downcast on composite of invalid size - " << lv << " - " << StateFmt(*this, curVs));
                        stateP = &states[0];
                    }
                }
                MIR_ASSERT(mir_res, stateP, "No state result?");
            }
            this->clearState(mir_res, *stateP);
            *stateP = mv$(newVs);
        }
    };

    struct StateSet {
        ::std::vector<ValueStates> knownStateSets;

        bool addState(const ValueStates& stateSet) {
            for (const auto& s : this->knownStateSets) {
                if (s.isEquivalentTo(stateSet)) {
                    return false;
                }
            }
            this->knownStateSets.push_back(stateSet.clone());
            this->knownStateSets.back().bbPath = ::std::vector<unsigned int>();
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
        assert(x.s.index - 1 < x.vss.innerStates.size());
        const auto& is = x.vss.innerStates[x.s.index - 1];
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
        auto printVal = [&](auto tag, const State& s) {
            if (s.isComposite()) {
                os << tag << "=" << StateFmt(x, s);
            } else if (s.isValid()) {
                os << tag;
            } else {
            }
        };

        os << "ValueStates(path=[" << x.bbPath << "]";
        printVal(",rv", x.returnValue);
        for (unsigned int i = 0; i < x.args.size(); i++) {
            printVal(FMT_CB(ss, ss << ",a" << i;), x.args[i]);
        }
        for (unsigned int i = 0; i < x.locals.size(); i++) {
            printVal(FMT_CB(ss, ss << ",_" << i;), x.locals[i]);
        }
        for (unsigned int i = 0; i < x.dropFlags.size(); i++) {
            if (x.dropFlags[i]) {
                os << ",df" << i;
            }
        }
        os << ")";
        return os;
    }
}

// "Executes" the function, keeping track of drop flags and variable validities
void MIRValidateFullValState(::MIR::TypeResolve& mir_res, const ::MIR::Function& fcn) {
    // TODO: Use a timer to check elapsed CPU time in this function, and check on each iteration
    // - If more than `n` (10?) seconds passes on one function, warn and abort
    //ElapsedTimeCounter    timer;
    ::std::vector<unsigned> blockRefCounts(fcn.blocks.size());
    ::std::vector<StateSet> blockEntryStates(fcn.blocks.size());

    // Determine value lifetimes (BBs in which Copy values are valid)
    // - Used to mask out Copy value (prevents combinatorial explosion)
    auto lifetimes = MIRHelperGetLifetimes(mir_res, fcn, /*dump_debug=*/true);
    DEBUG(lifetimes.blockOffsets);

    ValueStates state;

    struct H {
        static ::std::vector<State> makeList(size_t n, bool pop) {
            ::std::vector<State> rv;
            rv.reserve(n);
            while (n--) {
                rv.push_back(State(pop));
            }
            return rv;
        }
    };

    state.args = H::makeList(mir_res.mArgs.size(), true);
    state.locals = H::makeList(fcn.locals.size(), false);
    state.dropFlags = fcn.dropFlags;

    blockRefCounts[0] = 1;
    for (const auto& blk : fcn.blocks) {
        MIR::visit::visitTerminatorTarget(blk.terminator, [&](const ::MIR::BasicBlockId& e) {
            blockRefCounts.at(e) += 1;
        });
    }

    ::std::vector<::std::pair<unsigned int, ValueStates>> todoQueue;
    todoQueue.push_back(::std::make_pair(0, mv$(state)));
    while (!todoQueue.empty()) {
        auto curBlock = todoQueue.back().first;
        auto state = mv$(todoQueue.back().second);
        todoQueue.pop_back();

        // Mask off any values which aren't valid in the first statement of this block
        {
            for (unsigned i = 0; i < state.locals.size(); i++) {
                /*if( !variables_copy[i] )
                {
                    // Not Copy, don't apply masking
                }
                else*/
                if (!state.locals[i].isValid()) {
                    // Already invalid
                } else if (lifetimes.slotValid(i, curBlock, 0)) {
                    // Expected to be valid in this block, leave as-is
                } else {
                    // Copy value not used at/after this block, mask to false
                    DEBUG("BB" << curBlock << " - _" << i << " - Outside lifetime, discard");
                    state.locals[i] = State(false);
                }
            }
        }

        // If this state already exists in the map, skip
        // - Note: The `block_ref_counts` check saves a tiny bit of time, but not a huge amount
        if (blockRefCounts[curBlock] > 1 && !blockEntryStates[curBlock].addState(state)) {
            DEBUG("BB" << curBlock << " - Nothing new");
            continue;
        }
        DEBUG("BB" << curBlock << " - " << state);
        state.bbPath.push_back(curBlock);

        const auto& blk = fcn.blocks.at(curBlock);
        for (size_t i = 0; i < blk.statements.size(); i++) {
            mir_res.setCurStmt(curBlock, i);

            DEBUG(mir_res << blk.statements[i] << " " << state);

            TU_MATCH_HDRA( (blk.statements[i]), {)
            TU_ARMA(Assign, se) {
                    TU_MATCHA(
                        (se.src),
                        (ve),
                        (Use, state.moveLvalue(mir_res, ve);),
                        (Constant, ),
                        (SizedArray, state.ensureParamValid(mir_res, ve.val);),
                        (Borrow, state.ensureLvalueValid(mir_res, ve.val);),
                        // Cast on primitives
                        (Cast, state.ensureLvalueValid(mir_res, ve.val);),
                        // Binary operation on primitives
                        (BinOp, state.ensureParamValid(mir_res, ve.valL); state.ensureParamValid(mir_res, ve.valR);),
                        // Unary operation on primitives
                        (UniOp, state.ensureLvalueValid(mir_res, ve.val);),
                        // Extract the metadata from a DST pointer
                        // NOTE: If used on an array, this yields the array size (for generics)
                        (DstMeta, state.ensureLvalueValid(mir_res, ve.val);),
                        // Extract the pointer from a DST pointer (as *const ())
                        (DstPtr, state.ensureLvalueValid(mir_res, ve.val);),
                        // Construct a DST pointer from a thin pointer and metadata
                        (MakeDst, state.ensureParamValid(mir_res, ve.ptrVal); state.ensureParamValid(mir_res, ve.metaVal);),
                        (Tuple, for (const auto& v : ve.vals) if (const auto* e = v.opt_LValue()) state.moveLvalue(mir_res, *e);),
                        // Array literal
                        (Array, for (const auto& v : ve.vals) if (const auto* e = v.opt_LValue()) state.moveLvalue(mir_res, *e);),
                        // Create a new instance of a union
                        (UnionVariant, if (const auto* e = ve.val.opt_LValue()) state.moveLvalue(mir_res, *e);),
                        (EnumVariant, for (const auto& v : ve.vals) if (const auto* e = v.opt_LValue()) state.moveLvalue(mir_res, *e);),
                        // Create a new instance of a struct (or enum)
                        (Struct, for (const auto& v : ve.vals) if (const auto* e = v.opt_LValue()) state.moveLvalue(mir_res, *e);)
                    )
                    state.markLvalueValid(mir_res, se.dst);
                }
                TU_ARMA(Asm, se) {
                    for (const auto& v : se.inputs) {
                        state.ensureLvalueValid(mir_res, v.second);
                    }
                    for (const auto& v : se.outputs) {
                        state.markLvalueValid(mir_res, v.second);
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
                                    state.ensureParamValid(mir_res, *v.input);
                                }
                                if (v.output) {
                                    state.markLvalueValid(mir_res, *v.output);
                                }
                            }
                    }
                    }
                }
                TU_ARMA(SetDropFlag, se) {
                    if (se.other == ~0u) {
                        state.dropFlags[se.idx] = se.newVal;
                    } else {
                        state.dropFlags[se.idx] = (se.newVal != state.dropFlags[se.other]);
                    }
                }
                TU_ARMA(LoadDropFlag, se) {
                    MIR_TODO(mir_res, "");
                }
                TU_ARMA(SaveDropFlag, se) {
                    MIR_TODO(mir_res, "");
                }
                TU_ARMA(ScopeEnd, se) {
                    // TODO: Mark all mentioned variables as invalid
                }
            }
        }

        state.garbageCollect();

        mir_res.setCurStmtTerm(curBlock);
        DEBUG(mir_res << " " << blk.terminator);
        // TODO: Don't clone/push if the state already exists in the target
        // 1. Check all targets, calling `add_state` and checking result.
        //  - Count number of true results (and which bbs they were)
        TU_MATCHA(
            (blk.terminator),
            (te),
            (Incomplete, ),
            (
                Return, state.ensureLvalueValid(mir_res, ::MIR::LValue::newReturn());
            ),
            (UnwindResume, ),
            (UnwindTerminate, ),
            (Unreachable, ),
            (Goto, // Jump to another block
             todoQueue.push_back(::std::make_pair(te, mv$(state)));),
            (If, state.ensureLvalueValid(mir_res, te.cond); todoQueue.push_back(::std::make_pair(te.bbTrue, state.clone())); todoQueue.push_back(::std::make_pair(te.bbFalse, mv$(state)));),
            (Switch, if (te.validFlag != ~0u && !state.dropFlags.at(te.validFlag)) { todoQueue.push_back(::std::make_pair(te.invalidTarget, mv$(state))); } else {
                state.ensureLvalueValid(mir_res, te.val);
                for (size_t i = 0; i < te.targets.size(); i++) {
                    todoQueue.push_back(::std::make_pair(te.targets[i], i == te.targets.size() - 1 ? mv$(state) : state.clone()));
                }
            }),
            (SwitchValue, state.ensureLvalueValid(mir_res, te.val); for (size_t i = 0; i < te.targets.size(); i++) { todoQueue.push_back(::std::make_pair(te.targets[i], state.clone())); } todoQueue.push_back(::std::make_pair(te.defTarget, mv$(state)));),
            (Drop, if (te.flagIdx == ~0u || state.dropFlags.at(te.flagIdx)) {
                if (te.kind == ::MIR::eDropKind::SHALLOW) {
                    const auto& vs = state.getLvalueState(mir_res, te.slot);
                    MIR_ASSERT(mir_res, vs.index != ~0u, "Shallow drop on fully-valid value - " << te.slot);
                    MIR_ASSERT(mir_res, vs.isComposite(), "Shallow drop on non-composite state - " << te.slot << " (state=" << StateFmt(state, vs) << ")");
                    const auto& subStates = state.getComposite(mir_res, vs);
                    MIR_ASSERT(mir_res, subStates.size() == 2, "Shallow drop of slot with incorrect state shape (state=" << StateFmt(state, vs) << ")");
                    MIR_ASSERT(mir_res, subStates[0].isValid(), "Shallow drop on deallocated Box - " << te.slot << " (state=" << StateFmt(state, vs) << ")");
                    state.setLvalueState(mir_res, te.slot, State(false));
                } else {
                    state.moveLvalue(mir_res, te.slot);
                }
            }
            TU_IFLET(::MIR::UnwindAction, te.unwind, Cleanup, target,
                todoQueue.push_back(::std::make_pair(target, state.clone()));
            )
            todoQueue.push_back(::std::make_pair(te.target, mv$(state)));),
            (Call, if (const auto* e = te.fcn.opt_Value()) { state.ensureLvalueValid(mir_res, *e); } for (auto& arg : te.args) {
                if (const auto* e = arg.opt_LValue()) {
                    state.moveLvalue(mir_res, *e);
                }
            } TU_IFLET(::MIR::UnwindAction, te.unwind, Cleanup, target,
                todoQueue.push_back(::std::make_pair(target, state.clone()));
            ) state.markLvalueValid(mir_res, te.retVal);
             todoQueue.push_back(::std::make_pair(te.retBlock, mv$(state)));)
        )
    }
}

void MIRValidateFull(const StaticTraitResolve& resolve, const ::HIR::ItemPath& path, const ::MIR::Function& fcn, const ::HIR::Function::argsT& args, const ::HIR::TypeData* ret_type) {
    TRACE_FUNCTION_F(path);
    Span sp;
    ::MIR::TypeResolve state {
        sp, resolve, FMT_CB(ss, ss << path;), ret_type, args, fcn
    };
    // Validation rules:

    MIRValidateFullValState(state, fcn);
}

// --------------------------------------------------------------------

void MIRCheckCrateFull(/*const*/ ::HIR::Crate& crate) {
    ::MIR::OuterVisitor ov(crate, [](const auto& res, const auto& p, auto& expr, const auto& args, const auto& ty) {
        MIRValidateFull(res, p, *expr.mir, args, ty);
    });
    ov.visitCrate(crate);
}


namespace {
    /// @brief Used to tell the constant replacement code that replacements should be available
    bool gIsPostMonomorph = false;
}

class MirMutator {
    ::MIR::Function& fcn;
    unsigned int curBlock;
    unsigned int curStmt;
    mutable ::std::vector<::MIR::Statement> new_statements;

public:
    MirMutator(::MIR::Function& fcn, unsigned int bb, unsigned int stmt)
        : fcn(fcn)
        , curBlock(bb)
        , curStmt(stmt)
    {
    }

    void updateState(::MIR::TypeResolve& state) {
        if (this->curStmt == fcn.blocks[this->curBlock].statements.size()) {
            state.setCurStmtTerm(this->curBlock);
        } else {
            state.setCurStmt(this->curBlock, this->curStmt);
        }
    }

    ::MIR::LValue newTemporary(::HIR::TypeRef ty) {
        auto rv = ::MIR::LValue::newLocal(static_cast<unsigned int>(fcn.locals.size()));
        fcn.locals.push_back(mv$(ty));
        return rv;
    }

    void pushStatement(::MIR::Statement stmt) {
        new_statements.push_back(mv$(stmt));
    }

    ::MIR::LValue inTemporary(::HIR::TypeRef ty, ::MIR::RValue val) {
        auto rv = this->newTemporary(mv$(ty));
        pushStatement(::MIR::Statement::make_Assign({rv.clone(), mv$(val)}));
        return rv;
    }

    decltype(new_statements.begin()) flushStmt() {
        auto rv = flush();
        this->curStmt += 1;
        return rv;
    }

    void flushBlock() {
        flush();
        fcn.blocks.at(curBlock).statements.shrink_to_fit();
        this->curStmt = 0;
        this->curBlock += 1;
    }

private:
    decltype(new_statements.begin()) flush() {
        auto& block = fcn.blocks.at(curBlock);
        assert(curStmt <= block.statements.size());
        auto it = block.statements.begin() + curStmt;
        if (new_statements.size() > 0) {
            DEBUG("flush - BB" << curBlock << "/" << curStmt);
            for (auto& stmt : new_statements) {
                DEBUG("- Push stmt @" << curStmt << ": " << stmt);
                it = block.statements.insert(it, mv$(stmt));
                ++it;
                curStmt += 1;
            }
            new_statements.clear();
        }
        return it;
    }
};

void MIRCleanupLValue(const ::MIR::TypeResolve& state, MirMutator& mutator, ::MIR::LValue& lval);

namespace {
    ::HIR::TypeRef getVtableType(const Span& sp, const ::StaticTraitResolve& resolve, const ::HIR::TypeData::Data_TraitObject& te) {
        return te.mTrait.traitPtr->getVtableType(sp, resolve.crate, te);
    }
}

const EncodedLiteral* MIRCleanupGetConstant(const MIR::TypeResolve& state, const ::HIR::Path& path, ::HIR::TypeRef& outTy, MonomorphState& params) {
    TRACE_FUNCTION_F(path);

    auto v = state.mResolve.getValue(state.sp, path, params);
    if (const auto* e = v.opt_Constant()) {
        const auto& hirConst = **e;
        outTy = params.monomorphType(state.sp, hirConst.mType);
        state.mResolve.expandAssociatedTypes(state.sp, outTy);
        switch (hirConst.valueState) {
            case HIR::Constant::ValueState::Known:
                return &hirConst.valueRes;
            case HIR::Constant::ValueState::Generic: {
                // Do some form of lookup of a pre-cached evaluated monomorphised constant
                // - Maybe on the `Constant` entry there can be a list of pre-monomorphised values
                auto it = hirConst.monomorphCache.find(path);
                if (it == hirConst.monomorphCache.end()) {
                    // Emit a bug if the cache is empty? (or if this is in the post-monomorph pass)
                    if (gIsPostMonomorph && !monomorphisePathNeeded(path)) {
                        //MIR_BUG(state, "Constant with Defer literal and no cached monomorphisation - " << path);
                        // NOTE: Dead code can trigger this :(
                        // - There's a check in hir/serialise.cpp that makes sure that this doesn't reach the saved MIR
                    }
                    DEBUG("Generic, but no cached monomorphisation: " << hirConst.monomorphCache.size() << " entries");
                    return nullptr;
                }
                return &it->second;
            }
            case HIR::Constant::ValueState::Unknown:
                MIR_ASSERT(state, monomorphisePathNeeded(path, true), "Unevaluated constant - " << path);
                return nullptr;
        }
        throw "";
    } else if (v.is_NotYetKnown()) {
        auto v = state.mResolve.getValue(state.sp, path, params, /*signature_only=*/true);
        if (const auto* e = v.opt_Constant()) {
            const auto& hirConst = **e;
            outTy = params.monomorphType(state.sp, hirConst.mType);
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
    const RcString rcstringVtable = RcString::newInterned("vtable#");

    bool typeAcceptsAllBitPatterns(const Span& sp, const StaticTraitResolve& resolve, const HIR::TypeData* ty) {
        if (const auto* primitive = ty->opt_Primitive()) {
            return *primitive != HIR::CoreType::Bool && *primitive != HIR::CoreType::Char && *primitive != HIR::CoreType::Str;
        }
        if (const auto* array = ty->opt_Array()) {
            return array->size.as_Known() == 0 || typeAcceptsAllBitPatterns(sp, resolve, array->inner);
        }
        if (ty->is_Tuple() || (ty->is_Path() && ty->as_Path().binding.is_Struct())) {
            const auto* repr = TargetGetTypeRepr(sp, resolve, ty);
            if (!repr) {
                return false;
            }
            for (const auto& field : repr->fields) {
                if (!typeAcceptsAllBitPatterns(sp, resolve, field.ty)) {
                    return false;
                }
            }
            return true;
        }
        return false;
    }

    ::MIR::Constant createVtable(HIR::TypeRef ty, const HIR::TraitPath& trait) {
        auto vtable_path = trait.hrtbs ? ::HIR::Path(mv$(ty), trait.hrtbs->clone(), trait.mPath.clone(), rcstringVtable) : ::HIR::Path(mv$(ty), trait.mPath.clone(), rcstringVtable);
        return ::MIR::Constant::make_ItemAddr(box$(vtable_path));
    }
}

::MIR::RValue MIRCleanupLiteralToRValue(const ::MIR::TypeResolve& state, MirMutator& mutator, EncodedLiteralSlice lit, ::HIR::TypeRef ty, const MonomorphState& params, ::HIR::Path path) {
    struct M: Monomorphiser {
        explicit M(HIR::TypeInterner& types): Monomorphiser(types) {}

        ::HIR::TypeRef getType(const Span& sp, const ::HIR::GenericRef& ty) const override {
            return types.generic(ty.name, ty.binding);
        }

        ::HIR::ConstGeneric getValue(const Span& sp, const ::HIR::GenericRef& val) const override {
            return HIR::ConstGeneric(val);
        }

        ::HIR::LifetimeRef getLifetime(const Span& sp, const ::HIR::GenericRef& lftRef) const override {
            return ::HIR::LifetimeRef();
        }
    } monomorphEraseLifetimes(state.crate.types);

    TRACE_FUNCTION_F(ty << " <= " << lit);
    TU_MATCH_HDRA( (*ty), {)
    default:
        if( path == ::HIR::GenericPath() )
            MIR_TODO(state, "Literal of type " << ty << " - " << lit);
        DEBUG("Unknown type " << ty << ", but a path was provided - Return ItemAddr " << path);
        return ::MIR::Constant::make_ItemAddr(box$(path));
        TU_ARMA(Tuple, te) {
            auto* repr = TargetGetTypeRepr(state.sp, state.mResolve, ty);
            MIR_ASSERT(state, repr, "No type repr, but encoded value available? " << ty);

            ::std::vector<::MIR::Param> lvals;
            lvals.reserve(repr->fields.size());

            for (const auto& fld : repr->fields) {
                auto rval = MIRCleanupLiteralToRValue(state, mutator, lit.slice(fld.offset), monomorphEraseLifetimes.monomorphType(state.sp, fld.ty), params, ::HIR::GenericPath());
                lvals.push_back(mutator.inTemporary(monomorphEraseLifetimes.monomorphType(state.sp, fld.ty), mv$(rval)));
            }

            return ::MIR::RValue::make_Tuple({mv$(lvals)});
        }
        TU_ARMA(Array, te) {
            size_t size = 0;
            MIR_ASSERT(state, TargetGetSizeOf(state.sp, state.mResolve, te.inner, size), "No size, but encoded value available? " << ty);
            auto count = te.size.as_Known();

            bool isAllSame;
            if (count > 1) {
                isAllSame = true;
                size_t ofs = size;
                auto element0 = lit.slice(0, size);
                for (unsigned int i = 1; i < count; i++) {
                    auto cur = lit.slice(ofs, size);
                    //DEBUG(element0 << " ?= " << cur);
                    if (element0 != cur) {
                        isAllSame = false;
                        break;
                    }
                    ofs += size;
                }
            } else {
                isAllSame = false;
            }

            // If all of the literals are the same value, then optimise into a count-based initialisation
            if (isAllSame) {
                auto rval = MIRCleanupLiteralToRValue(state, mutator, lit.slice(0, size), te.inner, params, ::HIR::GenericPath());
                auto dataLval = mutator.inTemporary(te.inner, mv$(rval));
                return ::MIR::RValue::make_SizedArray({mv$(dataLval), static_cast<unsigned int>(count)});
            } else {
                ::std::vector<::MIR::Param> lvals;
                lvals.reserve(te.size.as_Known());

                size_t ofs = 0;
                for (unsigned int i = 0; i < count; i++) {
                    auto rval = MIRCleanupLiteralToRValue(state, mutator, lit.slice(ofs, size), te.inner, params, ::HIR::GenericPath());
                    lvals.push_back(mutator.inTemporary(te.inner, mv$(rval)));
                    ofs += size;
                }

                return ::MIR::RValue::make_Array({mv$(lvals)});
            }
        }
        TU_ARMA(Path, te) {
            auto* repr = TargetGetTypeRepr(state.sp, state.mResolve, ty);
            MIR_ASSERT(state, repr, "No type repr, but encoded value available? " << ty);

            if (te.binding.is_Struct()) {
                ::std::vector<::MIR::Param> lvals;
                lvals.reserve(repr->fields.size());

                for (const auto& fld : repr->fields) {
                    auto rval = MIRCleanupLiteralToRValue(state, mutator, lit.slice(fld.offset), monomorphEraseLifetimes.monomorphType(state.sp, fld.ty), params, ::HIR::GenericPath());
                    lvals.push_back(mutator.inTemporary(monomorphEraseLifetimes.monomorphType(state.sp, fld.ty), mv$(rval)));
                }

                return ::MIR::RValue::make_Struct({te.path.mData.as_Generic().clone(), mv$(lvals)});
            } else if (te.binding.is_Enum()) {
                auto varInfo = repr->getEnumVariant(state.sp, state.mResolve, lit);
                unsigned varIdx = varInfo.first;
                bool hasTagField = varInfo.second;

                const auto& enm = *te.binding.as_Enum();

                std::vector<::MIR::Param> vals;
                if (enm.mData.is_Data()) {
                    const auto& fld = repr->fields.at(varIdx);

                    size_t baseOfs = fld.offset;
                    const auto* repr = TargetGetTypeRepr(state.sp, state.mResolve, fld.ty);
                    vals.reserve(repr->fields.size());

                    for (const auto& fld : repr->fields) {
                        if (hasTagField && &fld == &repr->fields.back()) {
                            continue;
                        }
                        auto rval = MIRCleanupLiteralToRValue(state, mutator, lit.slice(baseOfs + fld.offset), monomorphEraseLifetimes.monomorphType(state.sp, fld.ty), params, ::HIR::GenericPath());
                        vals.push_back(mutator.inTemporary(monomorphEraseLifetimes.monomorphType(state.sp, fld.ty), mv$(rval)));
                    }
                } else {
                    // Leave empty
                }
                return ::MIR::RValue::make_EnumVariant({te.path.mData.as_Generic().clone(), varIdx, mv$(vals)});
            } else if (te.binding.is_Union()) {
                unsigned varIdx = ~0u;
                const auto* repr = TargetGetTypeRepr(state.sp, state.mResolve, ty);
                MIR_ASSERT(state, repr, "");
                // TODO: Find a way of storing backing information that specifies the variant (maybe as a relocation?)

                if (varIdx == ~0u) {
                    for (const auto& e : repr->fields) {
                        // A byte array covering the entire structure - can just emit
                        if (e.ty->is_Array() && e.ty->as_Array().inner == ::HIR::CoreType::U8 && e.ty->as_Array().size.as_Known() == repr->size) {
                            DEBUG("Found an array covering the whole union");
                            varIdx = &e - &repr->fields.front();
                            break;
                        }
                    }
                }
                // MaybeUninit (1.39) - `union MaybeUninit<T> { uninit: (), data: T }`
                // - If the body is all zeroes, then emit `uninit` (as that's the default)
                // - Otherwise, emit the actual value
                if (varIdx == ~0u) {
                    if (repr->fields.size() == 2 && repr->fields[0].ty == state.crate.types.unit()) {
                        // If all zeroes, then emit the tuple field, otherwise the other field
                        bool isNonzero = false;
                        for (size_t i = 0; i < repr->size; i++) {
                            if (lit.slice(i, 1).readUint(1) != 0) {
                                isNonzero = true;
                                break;
                            }
                        }

                        varIdx = (isNonzero ? 1 : 0);
                    }
                }

                // If there's a POD field (pointer or integer) of size equal to the whole struct, use that
                if (varIdx == ~0u) {
                    for (const auto& e : repr->fields) {
                        if (e.ty->is_Pointer() || e.ty->is_Primitive()) {
                            // If there's a relocation, then we have to use a pointer field
                            if (lit.getReloc() && !e.ty->is_Pointer()) {
                                continue;
                            }

                            size_t fldSize = 0;
                            TargetGetSizeOf(state.sp, state.mResolve, e.ty, fldSize);
                            if (fldSize == repr->size) {
                                // Found a suitable field!
                                DEBUG("Found a covering field");
                                varIdx = &e - &repr->fields.front();
                                break;
                            }
                        }
                    }
                }

                // A full-size aggregate made entirely from unrestricted scalar types can
                // represent the storage without knowing which union field initialized it.
                if (varIdx == ~0u) {
                    const auto literalEnd = lit.ofs + lit.mSize;
                    const bool hasRelocation = std::any_of(lit.base.relocations.begin(), lit.base.relocations.end(), [&](const auto& relocation) {
                        return relocation.ofs < literalEnd && lit.ofs < relocation.ofs + relocation.len;
                    });
                    if (!hasRelocation) {
                        for (const auto& e : repr->fields) {
                            size_t fieldSize = 0;
                            if (TargetGetSizeOf(state.sp, state.mResolve, e.ty, fieldSize) && fieldSize == repr->size && typeAcceptsAllBitPatterns(state.sp, state.mResolve, e.ty)) {
                                DEBUG("Found an unrestricted covering field");
                                varIdx = &e - &repr->fields.front();
                                break;
                            }
                        }
                    }
                }

                if (varIdx == ~0u) {
                    MIR_TODO(state, "MIR_Cleanup_LiteralToRValue - " << path << ": " << ty << " = " << lit << " - Decode union into MIR");
                }
                auto innerRval = MIRCleanupLiteralToRValue(state, mutator, lit, repr->fields[varIdx].ty, params, mv$(path));
                auto innerLval = mutator.inTemporary(monomorphEraseLifetimes.monomorphType(state.sp, repr->fields[varIdx].ty), mv$(innerRval));
                return ::MIR::RValue::make_UnionVariant({te.path.mData.as_Generic().clone(), varIdx, mv$(innerLval)});
            } else {
                MIR_BUG(state, "Unexpected type for literal from " << path << " - " << ty << " (lit = " << lit << ")");
            }
        }
        TU_ARMA(Primitive, te) {
            switch (te) {
                case ::HIR::CoreType::Char:
                    return ::MIR::Constant::make_Uint({lit.readUint(4), te});
                case ::HIR::CoreType::Usize:
                    return ::MIR::Constant::make_Uint({lit.readUint(TargetGetPointerBits() / 8), te});
                case ::HIR::CoreType::U128:
                    return ::MIR::Constant::make_Uint({lit.readUint(16), te});
                case ::HIR::CoreType::U64:
                    return ::MIR::Constant::make_Uint({lit.readUint(8), te});
                case ::HIR::CoreType::U32:
                    return ::MIR::Constant::make_Uint({lit.readUint(4), te});
                case ::HIR::CoreType::U16:
                    return ::MIR::Constant::make_Uint({lit.readUint(2), te});
                case ::HIR::CoreType::U8:
                    return ::MIR::Constant::make_Uint({lit.readUint(1), te});

                case ::HIR::CoreType::Isize:
                    return ::MIR::Constant::make_Int({lit.readSint(TargetGetPointerBits() / 8), te});
                case ::HIR::CoreType::I128:
                    return ::MIR::Constant::make_Int({lit.readSint(16), te});
                case ::HIR::CoreType::I64:
                    return ::MIR::Constant::make_Int({lit.readSint(8), te});
                case ::HIR::CoreType::I32:
                    return ::MIR::Constant::make_Int({lit.readSint(4), te});
                case ::HIR::CoreType::I16:
                    return ::MIR::Constant::make_Int({lit.readSint(2), te});
                case ::HIR::CoreType::I8:
                    return ::MIR::Constant::make_Int({lit.readSint(1), te});

                case ::HIR::CoreType::F128:
                    return ::MIR::Constant::make_Float({lit.readFloat(16), te});
                case ::HIR::CoreType::F64:
                    return ::MIR::Constant::make_Float({lit.readFloat(8), te});
                case ::HIR::CoreType::F32:
                    return ::MIR::Constant::make_Float({lit.readFloat(4), te});
                case ::HIR::CoreType::F16:
                    return ::MIR::Constant::make_Float({lit.readFloat(2), te});
                case ::HIR::CoreType::Bool:
                    return ::MIR::Constant::make_Bool({lit.readUint(1) != 0});

                case ::HIR::CoreType::Str:
                    MIR_BUG(state, "Const of type `str` - " << path);
            }
            throw "";
        }
        TU_ARMA(Pointer, te) {
            if (lit.getReloc()) {
                // Share logic with `Borrow` below, but wrap returned value in a cast op
                auto tyBorrow = state.crate.types.borrow(te.type, te.inner);
                auto rval = MIRCleanupLiteralToRValue(state, mutator, lit, tyBorrow, params, mv$(path));
                auto lval = mutator.inTemporary(mv$(tyBorrow), mv$(rval));
                return ::MIR::RValue::make_Cast({mv$(lval), mv$(ty)});
            } else {
                auto v = lit.readUint(TargetGetPointerBits() / 8);
                auto lval = mutator.inTemporary(state.crate.types.primitive(::HIR::CoreType::Usize), ::MIR::RValue(::MIR::Constant::make_Uint({v, ::HIR::CoreType::Usize})));
                return ::MIR::RValue::make_Cast({mv$(lval), mv$(ty)});
            }
        }
        TU_ARMA(Borrow, te) {
            const auto* dataReloc = lit.getReloc();
            const auto data_ptr = lit.readUint(TargetGetPointerBits() / 8);
            MIR_ASSERT(state, data_ptr >= EncodedLiteral::PTR_BASE, "Bad pointer value - 0x" << std::hex << data_ptr);

            if (!dataReloc) {
                ::HIR::TypeRef ptrInner;
                const auto metadataType = state.mResolve.metadataType(state.sp, te.inner);
                if (metadataType == MetadataType::Slice) {
                    if (const auto* slice = te.inner->opt_Slice()) {
                        ptrInner = slice->inner;
                    } else {
                        MIR_ASSERT(state, te.inner == ::HIR::CoreType::Str, "Slice metadata on non-slice type " << te.inner);
                        ptrInner = state.crate.types.primitive(::HIR::CoreType::U8);
                    }
                } else {
                    ptrInner = te.inner;
                }

                auto addr = mutator.inTemporary(state.crate.types.primitive(::HIR::CoreType::Usize), ::MIR::Constant::make_Uint({data_ptr, ::HIR::CoreType::Usize}));
                auto ptrTy = state.crate.types.pointer(te.type, ptrInner);
                auto ptr = mutator.inTemporary(ptrTy, ::MIR::RValue::make_Cast({mv$(addr), ptrTy}));

                switch (metadataType) {
                    case MetadataType::None:
                        return ::MIR::RValue::make_Borrow({te.type, false, ::MIR::LValue::newDeref(mv$(ptr))});
                    case MetadataType::Slice: {
                        const auto ptrSize = TargetGetPointerBits() / 8;
                        auto size = ::MIR::Constant::make_Uint({lit.slice(ptrSize).readUint(ptrSize), ::HIR::CoreType::Usize});
                        return ::MIR::RValue::make_MakeDst({mv$(ptr), mv$(size)});
                    }
                    case MetadataType::TraitObject:
                    case MetadataType::Unknown:
                    case MetadataType::Zero:
                        MIR_TODO(state, "Integer-address borrow with metadata " << metadataType);
                }
            }

            const auto ofs = data_ptr - EncodedLiteral::PTR_BASE;
            if (dataReloc->p) {
                const auto& path = *dataReloc->p;
                auto ptrVal = ::MIR::Constant::make_ItemAddr({box$(params.monomorphPath(state.sp, path)), ofs});
                DEBUG("ptr_val = " << ptrVal);
                ::HIR::TypeRef tmp;
                const auto& srcTy = state.getStaticType(tmp, path);

                // Get the metadata type (for !Sized wrapper types)
                auto metaTy = state.mResolve.metadataType(state.sp, te.inner);
                switch (metaTy) {
                    case MetadataType::None:
                        // TODO: What if the type doesn't match? Emit a `_Cast foo as &Bar`?
                        if (srcTy != te.inner) {
                            auto srcRefTy = state.crate.types.borrow(te.type, srcTy);
                            auto srcPtrTy = state.crate.types.pointer(te.type, srcTy);
                            auto innerPtrTy = state.crate.types.pointer(te.type, te.inner);
                            auto srcTyRef = mutator.inTemporary(srcRefTy, mv$(ptrVal));
                            auto srcTyPtr = mutator.inTemporary(srcPtrTy, ::MIR::RValue::make_Cast({mv$(srcTyRef), srcPtrTy}));
                            auto innerLval = mutator.inTemporary(innerPtrTy, ::MIR::RValue::make_Cast({mv$(srcTyPtr), innerPtrTy}));
                            return ::MIR::RValue::make_Borrow({te.type, false, MIR::LValue::newDeref(mv$(innerLval))});
                        }
                        return mv$(ptrVal);
                    case MetadataType::Slice: {
                        const auto ptrSize = TargetGetPointerBits() / 8;
                        auto size = lit.slice(ptrSize).readUint(ptrSize);
                        auto sizeVal = ::MIR::Param(::MIR::Constant::make_Uint({size, ::HIR::CoreType::Usize}));
                        return ::MIR::RValue::make_MakeDst({::MIR::Param(mv$(ptrVal)), mv$(sizeVal)});
                        break;
                    }
                    case MetadataType::TraitObject: {
                        const auto* tep = te.inner->opt_TraitObject();
                        if (!tep) {
                            MIR_TODO(state, "Hidden vtable");
                        }

                        auto vtableVal = ::MIR::Param(createVtable(srcTy, tep->mTrait));

                        return ::MIR::RValue::make_MakeDst({::MIR::Param(mv$(ptrVal)), mv$(vtableVal)});
                        break;
                    }
                    case MetadataType::Unknown:
                        MIR_BUG(state, te.inner << " unknown metadata type");
                    case MetadataType::Zero:
                        MIR_TODO(state, "Zero metadata");
                }
            } else {
                // This is a borrow of a "string"
                MIR_ASSERT(state, ofs <= dataReloc->bytes.size(), "Offset out of range");
                auto s = dataReloc->bytes.begin() + ofs.truncateU64();
                auto e = dataReloc->bytes.end();

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
                    auto lval = mutator.inTemporary(state.crate.types.pointer(HIR::BorrowType::Shared, state.crate.types.slice(state.crate.types.primitive(::HIR::CoreType::U8))), mv$(ptr1));
                    // Cast to `*const T`
                    auto rawPtrTy = state.crate.types.pointer(HIR::BorrowType::Shared, te.inner);
                    auto lval2 = mutator.inTemporary(rawPtrTy, ::MIR::RValue::make_Cast({mv$(lval), rawPtrTy}));
                    // Reborrow as `&T`
                    return ::MIR::RValue::make_Borrow({::HIR::BorrowType::Shared, false, ::MIR::LValue::newDeref(mv$(lval2))});
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
            const auto* dataReloc = lit.getReloc();
            MIR_ASSERT(state, dataReloc, "Function with no relocation?!");
            MIR_ASSERT(state, dataReloc->p, "");
            return ::MIR::Constant::make_ItemAddr(box$(dataReloc->p->clone()));
        }
    }
    throw "";
}

::MIR::LValue MIRCleanupVirtualize(const Span& sp, const ::MIR::TypeResolve& state, MirMutator& mutator, ::MIR::LValue& receiverLvp, const ::HIR::Path::Data::Data_UfcsKnown& pe) {
    TRACE_FUNCTION_F("<" << pe.type << " as " << pe.trait << ">::" << pe.item << pe.params);

    assert(pe.type->is_TraitObject());
    const ::HIR::TypeData::Data_TraitObject& te = pe.type->as_TraitObject();
    assert(te.mTrait.traitPtr);
    const auto& trait = *te.mTrait.traitPtr;

    // 1. Get the vtable index for this function
    unsigned int vtableIdx = trait.getVtableValueIndex(pe.trait.mPath, pe.item);
    if (vtableIdx == 0) {
        BUG(sp, "Calling method '" << pe.item << "' from " << pe.trait << " through " << te.mTrait.mPath << " which isn't in the vtable");
    }

    // 2. Load from the vtable
    auto vtableTy = state.crate.types.pointer(::HIR::BorrowType::Shared, getVtableType(sp, state.mResolve, te));
    DEBUG("vtable_ty = " << vtableTy);

    // If the method is a by-value method, add a `&move`
    const auto& fnDef = state.crate.getTraitByPath(sp, pe.trait.mPath).values.at(pe.item).as_Function();
    if (fnDef.receiver == HIR::Function::Receiver::Value) {
        receiverLvp = mutator.inTemporary(state.crate.types.borrow(HIR::BorrowType::Owned, pe.type), MIR::RValue::make_Borrow({HIR::BorrowType::Owned, false, mv$(receiverLvp)}));
    }

    // Allocate a temporary for the vtable pointer itself
    auto vtableLv = mutator.newTemporary(mv$(vtableTy));
    auto fcnLval = ::MIR::LValue::newField(::MIR::LValue::newDeref(vtableLv.clone()), vtableIdx);
    ::HIR::TypeRef tmp;
    const auto& ty = state.getLvalueType(tmp, fcnLval);
    DEBUG("callable type " << ty);
    auto receiver = MonomorphHrlsOnly(state.crate.types, ty->as_Function().hrls.makeEmptyParams(true)).monomorphType(state.sp, ty->as_Function().argTypes.at(0));

    struct H {
        static ::MIR::LValue getUnitPtr(const ::MIR::TypeResolve& state, MirMutator& mutator, ::HIR::TypeRef ty, ::MIR::LValue lv, ::MIR::LValue& outInnerPtr) {
            if (ty->is_Path()) {
                const auto& te = ty->as_Path();
                MIR_ASSERT(state, te.binding.is_Struct(), "");
                const auto& tyPath = te.path.mData.as_Generic();
                const auto& str = *te.binding.as_Struct();
                ::HIR::TypeRef tmp;
                auto monomorph = [&](const auto& t) {
                    return MonomorphStatePtr(state.crate.types, nullptr, &tyPath.mParams, nullptr).monomorphType(state.sp, t);
                };
                ::std::vector<::MIR::Param> vals;
                TU_MATCH_HDRA( (str.mData), {)
                TU_ARMA(Unit, se) {
                    }
                    TU_ARMA(Tuple, se) {
                        for (unsigned int i = 0; i < se.size(); i++) {
                            auto val = ::MIR::LValue::newField((i == se.size() - 1 ? mv$(lv) : lv.clone()), i);
                            if (i == str.structMarkings.coerceUnsizedIndex) {
                                vals.push_back(H::getUnitPtr(state, mutator, monomorph(se[i].ent), mv$(val), outInnerPtr));
                            } else {
                                vals.push_back(mv$(val));
                            }
                        }
                    }
                    TU_ARMA(Named, se) {
                        for (unsigned int i = 0; i < se.size(); i++) {
                            auto val = ::MIR::LValue::newField((i == se.size() - 1 ? mv$(lv) : lv.clone()), i);
                            if (i == str.structMarkings.coerceUnsizedIndex) {
                                vals.push_back(H::getUnitPtr(state, mutator, monomorph(se[i].ty), mv$(val), outInnerPtr));
                            } else {
                                vals.push_back(mv$(val));
                            }
                        }
                    }
                }

                auto newPath = tyPath.clone();
                return mutator.inTemporary( mv$(ty), ::MIR::RValue::make_Struct({ mv$(newPath), mv$(vals) }) );
            } else if (ty->is_Borrow() || ty->is_Pointer()) {
                outInnerPtr = lv.clone();
                return mutator.inTemporary(state.crate.types.pointer(::HIR::BorrowType::Shared, state.crate.types.unit()), ::MIR::RValue::make_DstPtr({mv$(lv)}));
            } else {
                MIR_BUG(state, "Unexpected type coerce_unsize in receiver - " << ty);
            }
        }
    };

    ::MIR::LValue receiverPtr;
    ::MIR::LValue innerDynPtr;

    if (receiver->is_Path() && receiver->as_Path().binding.is_Struct() && receiver->as_Path().binding.as_Struct()->structMarkings.coerceUnsized != ::HIR::StructMarkings::Coerce::None) {
        // If the receiver is Box (or anything that implements CoerceUnsized), create a Foo<()> as the value.
        // - Requires de/restructuring the Box same as CoerceUnsized
        // - Can use the `coerce_unsized_index` field too
        receiverLvp = H::getUnitPtr(state, mutator, ::std::move(receiver), receiverLvp.clone(), innerDynPtr);
    } else if (receiver->is_Borrow() || receiver->is_Pointer()) {
        innerDynPtr = receiverLvp.clone();
        auto ptrRval = ::MIR::RValue::make_DstPtr({receiverLvp.clone()});

        auto ptrLv = mutator.newTemporary(state.crate.types.pointer(::HIR::BorrowType::Shared, state.crate.types.unit()));
        mutator.pushStatement(::MIR::Statement::make_Assign({ptrLv.clone(), mv$(ptrRval)}));
        receiverLvp = mv$(ptrLv);
    } else {
        // TODO: How to handle `Pin`?
        // - Locate the pointer (similar to unsized path?)
        MIR_TODO(state, "Handle virtual call through " << receiver);
    }

    // - Load the vtable and store it
    auto vtableRval = ::MIR::RValue::make_DstMeta({mv$(innerDynPtr)});
    mutator.pushStatement(::MIR::Statement::make_Assign({vtableLv.clone(), mv$(vtableRval)}));

    // Update the terminator with the new information.
    return fcnLval;
}

bool MIRCleanupUnsizeGetMetadata(const ::MIR::TypeResolve& state, MirMutator& mutator, const ::HIR::TypeData* dstTy, const ::HIR::TypeData* srcTy, const ::MIR::LValue& ptrValue, ::MIR::Param& outMetaVal, ::HIR::TypeRef& outMetaTy, bool& outSrcIsDst) {
    TU_MATCH_HDRA( (*dstTy), { )
    default:
        MIR_TODO(state, "Obtain metadata converting to " << dstTy);
        TU_ARMA(Generic, de) {
            // TODO: What should be returned to indicate "no conversion"
            return false;
        }
        TU_ARMA(Path, de) {
            // Source must be Path and Unsize
            if (de.binding.is_Opaque()) {
                return false;
            }

            MIR_ASSERT(state, srcTy->is_Path(), "Unsize to path from non-path - " << srcTy);
            const auto& se = srcTy->as_Path();
            MIR_ASSERT(state, de.binding.tag() == se.binding.tag(), "Unsize between mismatched types - " << dstTy << " and " << srcTy);
            MIR_ASSERT(state, de.binding.is_Struct(), "Unsize to non-struct - " << dstTy);
            MIR_ASSERT(state, de.binding.as_Struct() == se.binding.as_Struct(), "Unsize between mismatched types - " << dstTy << " and " << srcTy);
            const auto& str = *de.binding.as_Struct();
            MIR_ASSERT(state, str.structMarkings.unsizedField != ~0u, "Unsize on type that doesn't implement have a ?Sized field - " << dstTy);

            auto monomorphCbD = MonomorphStatePtr(state.crate.types, nullptr, &de.path.mData.as_Generic().mParams, nullptr);
            auto monomorphCbS = MonomorphStatePtr(state.crate.types, nullptr, &se.path.mData.as_Generic().mParams, nullptr);

            // Return GetMetadata on the inner type
        TU_MATCH_HDRA( (str.mData), {)
        TU_ARMA(Unit, se) {
                    MIR_BUG(state, "Unit-like struct Unsize is impossible - " << srcTy);
                }
                TU_ARMA(Tuple, se) {
                    const auto& tyTpl = se.at(str.structMarkings.unsizedField).ent;
                    auto tyD = monomorphCbD.monomorphType(state.sp, tyTpl, false);
                    auto tyS = monomorphCbS.monomorphType(state.sp, tyTpl, false);

                    return MIRCleanupUnsizeGetMetadata(state, mutator, tyD, tyS, ptrValue, outMetaVal, outMetaTy, outSrcIsDst);
                }
                TU_ARMA(Named, se) {
                    const auto& tyTpl = se.at(str.structMarkings.unsizedField).ty;
                    auto tyD = monomorphCbD.monomorphType(state.sp, tyTpl, false);
                    auto tyS = monomorphCbS.monomorphType(state.sp, tyTpl, false);

                    return MIRCleanupUnsizeGetMetadata(state, mutator, tyD, tyS, ptrValue, outMetaVal, outMetaTy, outSrcIsDst);
                }
        }
        throw "";
        }
        TU_ARMA(Slice, de) {
            // Source must be an array (or generic)
            if (srcTy->is_Array()) {
                const auto& inArray = srcTy->as_Array();
                if (!inArray.size.is_Known()) {
                    DEBUG("Array size not yet known - " << inArray.size);
                    return false;
                }
                outMetaTy = state.crate.types.primitive(::HIR::CoreType::Usize);
                outMetaVal = ::MIR::Constant::make_Uint({U128(inArray.size.as_Known()), ::HIR::CoreType::Usize});
                return true;
            } else if (srcTy->is_Generic() || (srcTy->is_Path() && srcTy->as_Path().binding.is_Opaque())) {
                // Defer until monomorphisation supplies the concrete source array.
                return false;
            } else {
                MIR_BUG(state, "Unsize to slice from non-array - " << srcTy);
            }
        }
        TU_ARMA(TraitObject, de) {
            // Obtain vtable type `::"path"::to::Trait#vtable`
            auto vtableTy = de.mTrait.mPath != HIR::SimplePath() ? de.mTrait.traitPtr->getVtableType(state.sp, state.crate, de) : state.crate.types.unit();
            outMetaTy = state.crate.types.pointer(::HIR::BorrowType::Shared, vtableTy);

            // If the data trait hasn't changed, return the vtable pointer
            if (const auto* se = srcTy->opt_TraitObject()) {
                outSrcIsDst = true;
                if (se->mTrait.traitPtr != de.mTrait.traitPtr) {
                    assert(se->mTrait.traitPtr);
                    const auto& trait = *se->mTrait.traitPtr;
                    auto vtableTy = trait.getVtableType(state.sp, state.crate, *se);
                    auto inMetaTy = state.crate.types.pointer(::HIR::BorrowType::Shared, vtableTy);

                    auto parentTraitField = trait.getVtableParentIndex(state.crate.types, state.sp, se->mTrait.mPath.mParams, de.mTrait.mPath);
                    MIR_ASSERT(state, parentTraitField != 0, "Unable to find parent trait for trait object upcast - " << se->mTrait.mPath << " in " << de.mTrait.mPath);
                    auto inMetaVal = mutator.inTemporary(mv$(inMetaTy), ::MIR::RValue::make_DstMeta({ptrValue.clone()}));
                    outMetaVal = MIR::LValue::newField(MIR::LValue::newDeref(mv$(inMetaVal)), parentTraitField);
                } else {
                    outMetaVal = mutator.inTemporary(outMetaTy, ::MIR::RValue::make_DstMeta({ptrValue.clone()}));
                }
            } else {
                MIR_ASSERT(state, state.mResolve.typeIsSized(state.sp, srcTy), "Attempting to get vtable for unsized type - " << srcTy);
                outMetaVal = createVtable(srcTy, de.mTrait);
            }
            return true;
        }
    }
    throw "";
}

::MIR::RValue MIRCleanupUnsize(const ::MIR::TypeResolve& state, MirMutator& mutator, const ::HIR::TypeData* dstTy, const ::HIR::TypeData* srcTyInner, ::MIR::LValue ptrValue) {
    const auto& dstTyInner = (dstTy->is_Borrow() ? dstTy->as_Borrow().inner : dstTy->as_Pointer().inner);

    ::HIR::TypeRef metaType;
    ::MIR::Param metaValue;
    bool sourceIsDst = false;
    if (MIRCleanupUnsizeGetMetadata(state, mutator, dstTyInner, srcTyInner, ptrValue, metaValue, metaType, sourceIsDst)) {
        // There is a case where the source is already a fat pointer. In that case the pointer of the new DST must be the source DST pointer
        if (sourceIsDst) {
            auto tyUnitPtr = state.crate.types.pointer(::HIR::BorrowType::Shared, state.crate.types.unit());
            auto thinPtrLval = mutator.inTemporary(mv$(tyUnitPtr), ::MIR::RValue::make_DstPtr({mv$(ptrValue)}));

            return ::MIR::RValue::make_MakeDst({mv$(thinPtrLval), mv$(metaValue)});
        } else {
            return ::MIR::RValue::make_MakeDst({mv$(ptrValue), mv$(metaValue)});
        }
    } else {
        // Re-emit the "unsize" pseudo-op
        return ::MIR::RValue::make_MakeDst({mv$(ptrValue), MIR::Constant::make_ItemAddr({})});
    }
}

::MIR::RValue MIRCleanupCoerceUnsized(const ::MIR::TypeResolve& state, MirMutator& mutator, const ::HIR::TypeData* dstTy, const ::HIR::TypeData* srcTy, ::MIR::LValue value) {
    TRACE_FUNCTION_F(dstTy << " <- " << srcTy << " ( " << value << " )");
    //  > Path -> Path = Unsize
    // (path being destination is otherwise invalid)
    if (dstTy->is_Path()) {
        MIR_ASSERT(state, srcTy->is_Path(), "CoerceUnsized to Path must have a Path source - " << srcTy << " to " << dstTy);
        const auto& dte = dstTy->as_Path();
        const auto& ste = srcTy->as_Path();

        // - Types must differ only by a single field, and be from the same definition
        MIR_ASSERT(state, dte.binding.is_Struct(), "Note, can't CoerceUnsized non-structs");
        MIR_ASSERT(state, dte.binding.tag() == ste.binding.tag(), "Note, can't CoerceUnsized mismatched structs - " << srcTy << " to " << dstTy);
        MIR_ASSERT(state, dte.binding.as_Struct() == ste.binding.as_Struct(), "Note, can't CoerceUnsized mismatched structs - " << srcTy << " to " << dstTy);
        const auto& str = *dte.binding.as_Struct();
        MIR_ASSERT(state, str.structMarkings.coerceUnsizedIndex != ~0u, "Struct " << srcTy << " doesn't impl CoerceUnsized");

        auto monomorphCbD = MonomorphStatePtr(state.crate.types, nullptr, &dte.path.mData.as_Generic().mParams, nullptr);
        auto monomorphCbS = MonomorphStatePtr(state.crate.types, nullptr, &ste.path.mData.as_Generic().mParams, nullptr);

        // - Destructure and restrucure with the unsized fields
        ::std::vector<::MIR::Param> ents;
        TU_MATCH_HDRA( (str.mData), {)
        TU_ARMA(Unit, se) {
                MIR_BUG(state, "Unit-like struct CoerceUnsized is impossible - " << srcTy);
            }
            TU_ARMA(Tuple, se) {
                ents.reserve(se.size());
                for (unsigned int i = 0; i < se.size(); i++) {
                    if (i == str.structMarkings.coerceUnsizedIndex) {
                        auto tyD = monomorphCbD.monomorphType(state.sp, se[i].ent, false);
                        auto tyS = monomorphCbS.monomorphType(state.sp, se[i].ent, false);

                        auto newRval = MIRCleanupCoerceUnsized(state, mutator, tyD, tyS, ::MIR::LValue::newField(value.clone(), i));
                        auto newLval = mutator.inTemporary(mv$(tyD), mv$(newRval));

                        ents.push_back(mv$(newLval));
                    } else if (state.mResolve.isTypePhantomData(se[i].ent)) {
                        auto tyD = monomorphCbD.monomorphType(state.sp, se[i].ent, false);

                        auto newRval = ::MIR::RValue::make_Struct({tyD->as_Path().path.mData.as_Generic().clone(), {}});
                        auto newLval = mutator.inTemporary(mv$(tyD), mv$(newRval));

                        ents.push_back(mv$(newLval));
                    } else {
                        ents.push_back(::MIR::LValue::newField(value.clone(), i));
                    }
                }
            }
            TU_ARMA(Named, se) {
                ents.reserve(se.size());
                for (unsigned int i = 0; i < se.size(); i++) {
                    if (i == str.structMarkings.coerceUnsizedIndex) {
                        auto tyD = monomorphCbD.monomorphType(state.sp, se[i].ty, false);
                        auto tyS = monomorphCbS.monomorphType(state.sp, se[i].ty, false);

                        auto newRval = MIRCleanupCoerceUnsized(state, mutator, tyD, tyS, ::MIR::LValue::newField(value.clone(), i));
                        auto newLval = mutator.newTemporary(mv$(tyD));
                        mutator.pushStatement(::MIR::Statement::make_Assign({newLval.clone(), mv$(newRval)}));

                        ents.push_back(mv$(newLval));
                    } else if (state.mResolve.isTypePhantomData(se[i].ty)) {
                        auto tyD = monomorphCbD.monomorphType(state.sp, se[i].ty, false);

                        auto newRval = ::MIR::RValue::make_Struct({tyD->as_Path().path.mData.as_Generic().clone(), {}});
                        auto newLval = mutator.inTemporary(mv$(tyD), mv$(newRval));

                        ents.push_back(mv$(newLval));
                    } else {
                        ents.push_back(::MIR::LValue::newField(value.clone(), i));
                    }
                }
            }
        }
        return ::MIR::RValue::make_Struct({ dte.path.mData.as_Generic().clone(), mv$(ents) });
    }

    if (dstTy->is_Borrow()) {
        MIR_ASSERT(state, srcTy->is_Borrow(), "CoerceUnsized to Borrow must have a Borrow source - " << srcTy << " to " << dstTy);
        const auto& ste = srcTy->as_Borrow();

        return MIRCleanupUnsize(state, mutator, dstTy, ste.inner, mv$(value));
    }

    // Pointer Coercion - Downcast and unsize
    if (dstTy->is_Pointer()) {
        MIR_ASSERT(state, srcTy->is_Pointer(), "CoerceUnsized to Pointer must have a Pointer source - " << srcTy << " to " << dstTy);
        const auto& dte = dstTy->as_Pointer();
        const auto& ste = srcTy->as_Pointer();

        if (dte.type == ste.type) {
            return MIRCleanupUnsize(state, mutator, dstTy, ste.inner, mv$(value));
        } else {
            MIR_ASSERT(state, dte.inner == ste.inner, "TODO: Can pointer CoerceUnsized unsize? " << srcTy << " to " << dstTy);
            MIR_ASSERT(state, dte.type < ste.type, "CoerceUnsize attempting to raise pointer type");

            return ::MIR::RValue::make_Cast({mv$(value), dstTy});
        }
    }

    MIR_BUG(state, "Unknown CoerceUnsized target " << dstTy << " from " << srcTy);
    throw "";
}

void MIRCleanupLValue(const ::MIR::TypeResolve& state, MirMutator& mutator, ::MIR::LValue& lval) {
    TU_MATCH_HDRA( (lval.root), {)
    TU_ARMA(Return, le) {
        }
        TU_ARMA(Argument, le) {
        }
        TU_ARMA(Local, le) {
        }
        TU_ARMA(Static, le) {
        }
    }

    for(size_t i = 0; i < lval.wrappers.size(); i ++)
    {
        if (!lval.wrappers[i].is_Deref()) {
            continue;
        }

        // If this is a deref of Box, unpack and deref the inner pointer
        ::HIR::TypeRef tmp;
        const auto& ty = state.getLvalueType(tmp, lval, lval.wrappers.size() - i);
        if (state.mResolve.isTypeOwnedBox(ty)) {
            unsigned numInjectedFldZeros = 0;

            // Handle Box by extracting it to its pointer.
            // - Locate (or remember) which field in Box is the pointer, and replace the inner by that field
            // > Dumb idea, assume it's always the first field. Keep accessing until located.

            auto typ = ty;
            while (typ->is_Path()) {
                const auto& te = typ->as_Path();
                MIR_ASSERT(state, te.binding.is_Struct(), "Box contained a non-struct");
                const auto& str = *te.binding.as_Struct();
                const ::HIR::TypeData* tyTpl = nullptr;
                TU_MATCH_HDRA( (str.mData), {)
                TU_ARMA(Unit, se) {
                        MIR_BUG(state, "Box contained a unit-like struct");
                    }
                    TU_ARMA(Tuple, se) {
                        MIR_ASSERT(state, se.size() > 0, "Box contained an empty tuple struct");
                        tyTpl = se[0].ent;
                    }
                    TU_ARMA(Named, se) {
                        MIR_ASSERT(state, se.size() > 0, "Box contained an empty named struct");
                        tyTpl = se[0].ty;
                    }
                }
                tmp = MonomorphStatePtr(state.crate.types, nullptr, &te.path.mData.as_Generic().mParams, nullptr).monomorphType(state.sp, tyTpl);
                typ = tmp;

                numInjectedFldZeros ++;
            }
            MIR_ASSERT(state, typ->is_Pointer(), "First non-path field in Box wasn't a pointer - " << typ);
            // We have reached the pointer. Good.

            // Inject all of the field zero accesses (before the deref)
            while (numInjectedFldZeros--) {
                lval.wrappers.insert(lval.wrappers.begin() + i, ::MIR::LValue::Wrapper::newField(0));
            }
        } else {
            // What about other types?
        }
    }
}

void MIRCleanupConstant(const ::MIR::TypeResolve& state, MirMutator& mutator, ::MIR::Constant& p) {
    if (auto* e = p.opt_Uint()) {
        switch (e->t) {
            // Constants use U128 storage; truncate usize values to the target pointer width.
            case ::HIR::CoreType::Usize:
                if (TargetGetCurSpec().arch.pointerBits == 32) {
                    e->v &= U128(0xFFFFFFFF);
                }
                break;
            default:
                break;
        }
    }
}

void MIRCleanupParam(const ::MIR::TypeResolve& state, MirMutator& mutator, ::MIR::Param& p) {
    TU_MATCH_HDRA( (p), { )
    TU_ARMA(LValue, e) {
            MIRCleanupLValue(state, mutator, e);
        }
        TU_ARMA(Borrow, e) {
            MIRCleanupLValue(state, mutator, e.val);
        }
        TU_ARMA(Constant, e) {
            MIRCleanupConstant(state, mutator, e);
        }
    }

    // Effectively a copy of the code that handles RValue::Constant below
    if( p.is_Constant() && p.as_Constant().is_Const() )
    {
        const auto& ce = p.as_Constant().as_Const();
        ::HIR::TypeRef cTy;
        MonomorphState params(state.crate.types);
        const auto* litPtr = MIRCleanupGetConstant(state, *ce.p, cTy, params);
        if (litPtr) {
            DEBUG("Replace constant " << *ce.p << " with " << *litPtr);
            auto newRval = MIRCleanupLiteralToRValue(state, mutator, *litPtr, cTy, params, mv$(*ce.p));
            if (auto* lv = newRval.opt_Use()) {
                p = ::MIR::Param::make_LValue(::std::move(*lv));
            } else if (auto* c = newRval.opt_Constant()) {
                MIRCleanupConstant(state, mutator, *c);
                p = ::MIR::Param::make_Constant(::std::move(*c));
            } else {
                auto tmpLv = mutator.inTemporary(mv$(cTy), mv$(newRval));
                p = ::MIR::Param::make_LValue(::std::move(tmpLv));
            }
        } else {
            DEBUG("No replacement for constant " << *ce.p);
        }
    }
}

void MIRCleanup(const StaticTraitResolve& resolve, const ::HIR::ItemPath& path, ::MIR::Function& fcn, const ::HIR::Function::argsT& args, const ::HIR::TypeData* ret_type) {
    Span sp;
    TRACE_FUNCTION_F(path);
    ::MIR::TypeResolve state {
        sp, resolve, FMT_CB(ss, ss << path;), ret_type, args, fcn
    };

    MirMutator mutator{fcn, 0, 0};
    for (auto& block : fcn.blocks) {
        for (auto it = block.statements.begin(); it != block.statements.end(); ++it) {
            mutator.updateState(state);
            auto& stmt = *it;

            // >> Detect use of `!` as a value
            ::HIR::TypeRef tmp;
            if (TU_TEST1(stmt, Assign, .src.is_Borrow()) && state.getLvalueType(tmp, stmt.as_Assign().src.as_Borrow().val)->is_Diverge()) {
                DEBUG(state << "Not killing block due to use of `!`, it's being borrowed");
            } else {
                if (::MIR::visit::visitMirLvalues(stmt, [&](const auto& lv, auto /*vu*/) {
                    return state.getLvalueType(tmp, lv)->is_Diverge();
                })) {
                    DEBUG(state << "Truncate entire block due to use of `!` as a value - " << stmt);
                    block.statements.erase(it, block.statements.end());
                    block.terminator = ::MIR::Terminator::make_Unreachable({});
                    break;
                }
            }
            // >> Elaborate Box dereferences in all LValues
            DEBUG(state << stmt);
            TU_MATCH_HDRA( (stmt), { )
            TU_ARMA(SetDropFlag, se) {
                }
                TU_ARMA(SaveDropFlag, se) {
                    MIRCleanupLValue(state, mutator, se.slot);
                }
                TU_ARMA(LoadDropFlag, se) {
                    MIRCleanupLValue(state, mutator, se.slot);
                }
                TU_ARMA(ScopeEnd, se) {
                }
                TU_ARMA(Asm, se) {
                    for (auto& v : se.inputs) {
                        MIRCleanupLValue(state, mutator, v.second);
                    }
                    for (auto& v : se.outputs) {
                        MIRCleanupLValue(state, mutator, v.second);
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
                                    MIRCleanupParam(state, mutator, *v.input);
                                }
                                if (v.output) {
                                    MIRCleanupLValue(state, mutator, *v.output);
                                }
                            }
                    }
                    }
                }
                TU_ARMA(Assign, se) {
                    MIRCleanupLValue(state, mutator, se.dst);
                TU_MATCH_HDRA( (se.src), {)
                TU_ARMA(Use, re) {
                            MIRCleanupLValue(state, mutator, re);
                        }
                        TU_ARMA(Constant, re) {
                            MIRCleanupConstant(state, mutator, re);
                        }
                        TU_ARMA(SizedArray, re) {
                            MIRCleanupParam(state, mutator, re.val);
                        }
                        TU_ARMA(Borrow, re) {
                            MIRCleanupLValue(state, mutator, re.val);
                        }
                        TU_ARMA(Cast, re) {
                            MIRCleanupLValue(state, mutator, re.val);
                        }
                        TU_ARMA(BinOp, re) {
                            MIRCleanupParam(state, mutator, re.valL);
                            MIRCleanupParam(state, mutator, re.valR);
                        }
                        TU_ARMA(UniOp, re) {
                            MIRCleanupLValue(state, mutator, re.val);
                        }
                        TU_ARMA(DstMeta, re) {
                            // DstMeta consumes the pointer represented by a Box, so expose
                            // its dereference to the Box elaboration pass before splitting it.
                            re.val.wrappers.push_back(::MIR::LValue::Wrapper::newDeref());
                            MIRCleanupLValue(state, mutator, re.val);
                            re.val.wrappers.pop_back();

                            // If the type is an array (due to a monomorpised generic?) then replace.
                            ::HIR::TypeRef tmp;
                            const auto& ty = state.getLvalueType(tmp, re.val);
                            const ::HIR::TypeData* ityP;
                            if (const auto* te = ty->opt_Borrow()) {
                                ityP = te->inner;
                            } else if (const auto* te = ty->opt_Pointer()) {
                                ityP = te->inner;
                            }
                            // NOTE: This can happen with calling a by-value method on a trait object, e.g. `<dyn Foo as FnOnce>::call_once`
                            // - That is handled with magic in trans, so needs magic here (for inlining)
                            else if (ty->is_TraitObject()) {
                                ityP = ty;
                                // Remove the deref so downstream doesn't need to care
                                MIR_ASSERT(state, !re.val.wrappers.empty() && re.val.wrappers.back().is_Deref(), "DstMeta on bare trait object with no deref: " << re.val);
                                re.val.wrappers.pop_back();
                            } else {
                                BUG(Span(), "Unexpected input type for DstMeta - " << ty);
                            }
                        }
                        TU_ARMA(DstPtr, re) {
                            // DstPtr consumes the pointer represented by a Box, so expose
                            // its dereference to the Box elaboration pass before splitting it.
                            re.val.wrappers.push_back(::MIR::LValue::Wrapper::newDeref());
                            MIRCleanupLValue(state, mutator, re.val);
                            re.val.wrappers.pop_back();

                            ::HIR::TypeRef tmp;
                            const auto& ty = state.getLvalueType(tmp, re.val);
                            const ::HIR::TypeData* ityP;
                            if (const auto* te = ty->opt_Borrow()) {
                                ityP = te->inner;
                            } else if (const auto* te = ty->opt_Pointer()) {
                                ityP = te->inner;
                            }
                            // NOTE: This can happen with calling a by-value method on a trait object, e.g. `<dyn Foo as FnOnce>::call_once`
                            // - That is handled with magic in trans, so needs magic here (for inlining)
                            else if (ty->is_TraitObject()) {
                                ityP = ty;
                                // Remove the deref so downstream doesn't need to care
                                MIR_ASSERT(state, !re.val.wrappers.empty() && re.val.wrappers.back().is_Deref(), "DstPtr on bare trait object with no deref: " << re.val);
                                re.val.wrappers.pop_back();
                            } else {
                                BUG(Span(), "Unexpected input type for DstMeta - " << ty);
                            }
                            (void)ityP; // TODO: What is this needed for?
                        }
                        TU_ARMA(MakeDst, re) {
                            MIRCleanupParam(state, mutator, re.ptrVal);
                            MIRCleanupParam(state, mutator, re.metaVal);
                        }
                        TU_ARMA(Tuple, re) {
                            for (auto& lv : re.vals) {
                                MIRCleanupParam(state, mutator, lv);
                            }
                        }
                        TU_ARMA(Array, re) {
                            for (auto& lv : re.vals) {
                                MIRCleanupParam(state, mutator, lv);
                            }
                        }
                        TU_ARMA(UnionVariant, re) {
                            MIRCleanupParam(state, mutator, re.val);
                        }
                        TU_ARMA(EnumVariant, re) {
                            for (auto& lv : re.vals) {
                                MIRCleanupParam(state, mutator, lv);
                            }
                        }
                        TU_ARMA(Struct, re) {
                            for (auto& lv : re.vals) {
                                MIRCleanupParam(state, mutator, lv);
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
                        MonomorphState params(state.crate.types);
                        ::HIR::TypeRef ty;
                        const auto* litPtr = MIRCleanupGetConstant(state, *ce->p, ty, params);
                        if (litPtr) {
                            DEBUG("Replace constant " << *ce->p << " with " << *litPtr);
                            se.src = MIRCleanupLiteralToRValue(state, mutator, *litPtr, mv$(ty), params, mv$(*ce->p));
                            if (auto* p = se.src.opt_Constant()) {
                                MIRCleanupConstant(state, mutator, *p);
                            }
                        } else {
                            DEBUG("No replacement for constant " << *ce->p);
                        }
                    }
                }

                // Fix up coercions
                if (auto* e = se.src.opt_MakeDst()) {
                    if (TU_TEST2(e->metaVal, Constant, , ItemAddr, .get() == nullptr)) {
                        ::HIR::TypeRef tmp, tmp2;
                        const auto& srcTy = state.getParamType(tmp, e->ptrVal);
                        const auto& dstTy = state.getLvalueType(tmp2, se.dst);
                        MIR_ASSERT(state, e->ptrVal.is_LValue(), "BUG: MakeDst with no metadata should be LValue");
                        se.src = MIRCleanupCoerceUnsized(state, mutator, dstTy, srcTy, mv$(e->ptrVal.as_LValue()));
                    }
                }

                if (auto* e = se.src.opt_MakeDst()) {
                    if (TU_TEST2(e->metaVal, Constant, , ItemAddr, .get() == nullptr)) {
                        // TODO: Check the validity?
                        // - Ensure that something is generic in either the destination or source
                        ::HIR::TypeRef tmp;
                        const auto& srcTy = state.getParamType(tmp, e->ptrVal);
                        MIR_ASSERT(state, monomorphiseTypeNeeded(srcTy), "MakeDst Unsize with known source - " << srcTy);
                    }
                }
            }

            //DEBUG(it - block.statements.begin());
            it = mutator.flushStmt();
            //DEBUG(it - block.statements.begin());
        }

        mutator.updateState(state);
        //state.set_cur_stmt_term( mutator.cur_block );

        TU_MATCH_HDRA( (block.terminator), {)
        TU_ARMA(Incomplete, e) {
            }
            TU_ARMA(Return, e) {
            }
            TU_ARMA(UnwindResume, e) {
            }
            TU_ARMA(UnwindTerminate, e) {
            }
            TU_ARMA(Unreachable, e) {
            }
            TU_ARMA(Goto, e) {
            }
            TU_ARMA(If, e) {
                MIRCleanupLValue(state, mutator, e.cond);
            }
            TU_ARMA(Switch, e) {
                MIRCleanupLValue(state, mutator, e.val);
            }
            TU_ARMA(SwitchValue, e) {
                MIRCleanupLValue(state, mutator, e.val);
            }
            TU_ARMA(Drop, e) {
                MIRCleanupLValue(state, mutator, e.slot);
            }
            TU_ARMA(Call, e) {
                MIRCleanupLValue(state, mutator, e.retVal);
                if (e.fcn.is_Value()) {
                    MIRCleanupLValue(state, mutator, e.fcn.as_Value());
                }
                for (auto& lv : e.args) {
                    MIRCleanupParam(state, mutator, lv);
                }
            }
        }

        // VTable calls
        if(auto* ep = block.terminator.opt_Call())
        {
            auto& e = *ep;
            if (auto* pathP = e.fcn.opt_Path()) {
                auto& path = *pathP;
                // Detect calling `<Trait as Trait>::method()` and replace with vtable call
                if (path.mData.is_UfcsKnown() && path.mData.as_UfcsKnown().type->is_TraitObject()) {
                    const auto& pe = path.mData.as_UfcsKnown();
                    const auto& te = pe.type->as_TraitObject();
                    // TODO: What if the method is from a supertrait?

                    if (te.mTrait.mPath == pe.trait || resolve.findNamedTraitInTrait(sp, pe.trait.mPath, pe.trait.mParams, *te.mTrait.traitPtr, te.mTrait.mPath.mPath, te.mTrait.mPath.mParams, pe.type, [](const auto&, auto) {
                        return true;
                    })) {
                        auto tgtLvalue = MIRCleanupVirtualize(sp, state, mutator, e.args.front().as_LValue(), pe);
                        e.fcn = mv$(tgtLvalue);
                    }
                }

                if (path.mData.is_UfcsKnown() && path.mData.as_UfcsKnown().type->is_Function()) {
                    const auto& pe = path.mData.as_UfcsKnown();
                    const auto& fcnTy = pe.type->as_Function();
                    if (pe.trait.mPath == resolve.mLangFn || pe.trait.mPath == resolve.mLangFnMut || pe.trait.mPath == resolve.mLangFnOnce) {
                        MIR_ASSERT(state, e.args.size() == 2, "Fn* call requires two arguments");
                        auto fcnLvalue = mv$(e.args[0].as_LValue());
                        auto argsLvalue = mv$(e.args[1].as_LValue());

                        DEBUG("Convert function pointer call");

                        e.args.clear();
                        e.args.reserve(fcnTy.argTypes.size());
                        for (unsigned int i = 0; i < fcnTy.argTypes.size(); i++) {
                            e.args.push_back(::MIR::LValue::newField(argsLvalue.clone(), i));
                        }
                        // If the trait is Fn/FnMut, dereference the input value.
                        if (pe.trait.mPath == resolve.mLangFnOnce) {
                            e.fcn = mv$(fcnLvalue);
                        } else {
                            e.fcn = ::MIR::LValue::newDeref(mv$(fcnLvalue));
                        }
                    }
                }
                if (path.mData.is_UfcsKnown() && path.mData.as_UfcsKnown().type->is_NamedFunction()) {
                    const auto& pe = path.mData.as_UfcsKnown();
                    const auto& fcnTy = pe.type->as_NamedFunction();
                    if (pe.trait.mPath == resolve.mLangFn || pe.trait.mPath == resolve.mLangFnMut || pe.trait.mPath == resolve.mLangFnOnce) {
                        auto nArgs = fcnTy.decay(state.crate.types, state.sp).argTypes.size();
                        MIR_ASSERT(state, e.args.size() == 2, "Fn* call requires two arguments");
                        auto fcnLvalue = mv$(e.args[0].as_LValue());
                        auto argsLvalue = mv$(e.args[1].as_LValue());

                        DEBUG("Convert named function pointer call");

                        e.args.clear();
                        e.args.reserve(nArgs);
                        for (unsigned int i = 0; i < nArgs; i++) {
                            e.args.push_back(::MIR::LValue::newField(argsLvalue.clone(), i));
                        }
                        TU_MATCH_HDRA( (fcnTy.def), {)
                        TU_ARMA(Function, ve) {
                                e.fcn = fcnTy.path.clone();
                            }
                            TU_ARMA(StructConstructor, ve) {
                                block.statements.push_back(::MIR::Statement::make_Assign({std::move(e.retVal), MIR::RValue::make_Struct({fcnTy.path.mData.as_Generic().clone(), std::move(e.args)})}));
                                block.terminator = MIR::Terminator::make_Goto(e.retBlock);
                            }
                            TU_ARMA(EnumConstructor, ve) {
                                auto enmPath = fcnTy.path.mData.as_Generic().clone();
                                enmPath.mPath.popComponent();
                                block.statements.push_back(::MIR::Statement::make_Assign({std::move(e.retVal), MIR::RValue::make_EnumVariant({std::move(enmPath), static_cast<unsigned>(ve.v), std::move(e.args)})}));
                                block.terminator = MIR::Terminator::make_Goto(e.retBlock);
                            }
                        }
                    }
                }
            }

            // NOTE: Would be nice to do this in `Lower_MIR` - but that confuses the validity checks
            if (e.fcn.is_Intrinsic() && e.fcn.as_Intrinsic().name == "read_via_copy") {
                // TODO: Replace with `res = *ptr;`
                block.statements.push_back(MIR::Statement::make_Assign({std::move(e.retVal), MIR::LValue::newDeref(std::move(e.args.at(0).as_LValue()))}));
                block.terminator = MIR::Terminator::make_Goto(e.retBlock);
            }
            if (e.fcn.is_Intrinsic() && e.fcn.as_Intrinsic().name == "write_via_move") {
                // TODO: Replace with `*ptr = arg;`
                block.statements.push_back(MIR::Statement::make_Assign({MIR::LValue::newDeref(std::move(e.args.at(0).as_LValue())), std::move(e.args.at(1).as_LValue())}));
                block.statements.push_back(MIR::Statement::make_Assign({std::move(e.retVal), MIR::RValue::make_Tuple({})}));
                block.terminator = MIR::Terminator::make_Goto(e.retBlock);
            }
        }

        mutator.flushBlock();
    }

}

void MIRCleanupCrate(::HIR::Crate& crate) {
    ::MIR::OuterVisitor ov{crate, [&](const auto& res, const auto& p, ::HIR::ExprPtr& expr_ptr, const auto& args, const auto& ty) {
        if (expr_ptr) {
            MIRCleanup(res, p, expr_ptr.getMirOrErrorMut(Span()), args, ty);
            MIRValidate(res, p, expr_ptr.getMirOrErrorMut(Span()), args, ty);
        }
    }};
    ov.visitCrate(crate);
}

void MIRCleanupSetPostMonomorph() {
    gIsPostMonomorph = true;
}



#define DUMP_BEFORE_ALL 1
#define DUMP_BEFORE_CONSTPROPAGATE 0
#define DUMP_AFTER_PASS 1
#define DUMP_AFTER_ALL 0

#define DUMP_AFTER_DONE 1
#define CHECK_AFTER_DONE 2 // 1 = Check before GC, 2 = check before and after GC

// ----
// List of optimisations avaliable
// ----
bool MIROptimiseBlockSimplify(::MIR::TypeResolve& state, ::MIR::Function& fcn);
bool MIROptimiseInlining(::MIR::TypeResolve& state, ::MIR::Function& fcn, bool minimal, const TransList* list = nullptr);
bool MIROptimiseSplitAggregates(::MIR::TypeResolve& state, ::MIR::Function& fcn);
bool MIROptimisePropagateSingleAssignments(::MIR::TypeResolve& state, ::MIR::Function& fcn);
bool MIROptimisePropagateKnownValues(::MIR::TypeResolve& state, ::MIR::Function& fcn);
bool MIROptimiseDeTemporary(::MIR::TypeResolve& state, ::MIR::Function& fcn); // Eliminate useless temporaries
bool MIROptimiseUnifyTemporaries(::MIR::TypeResolve& state, ::MIR::Function& fcn);
bool MIROptimiseCommonStatements(::MIR::TypeResolve& state, ::MIR::Function& fcn);
bool MIROptimiseUnifyBlocks(::MIR::TypeResolve& state, ::MIR::Function& fcn);
bool MIROptimiseConstPropagate(::MIR::TypeResolve& state, ::MIR::Function& fcn);
bool MIROptimiseDeadDropFlags(::MIR::TypeResolve& state, ::MIR::Function& fcn);
bool MIROptimiseDeadAssignments(::MIR::TypeResolve& state, ::MIR::Function& fcn);
bool MIROptimiseNoopRemoval(::MIR::TypeResolve& state, ::MIR::Function& fcn);
bool MIROptimiseGotoAssign(::MIR::TypeResolve& state, ::MIR::Function& fcn);
bool MIROptimiseUselessReborrows(::MIR::TypeResolve& state, ::MIR::Function& fcn);
bool MIROptimiseGarbageCollectPartial(::MIR::TypeResolve& state, ::MIR::Function& fcn);
bool MIROptimiseGarbageCollect(::MIR::TypeResolve& state, ::MIR::Function& fcn);

enum {
    CHECKMODE_UNKNOWN,
    CHECKMODE_NONE,
    CHECKMODE_FINAL,
    CHECKMODE_PASS,
    CHECKMODE_ALL,
};

static int checkMode() {
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

static bool checkAfterAll() {
    return checkMode() >= CHECKMODE_ALL;
}

/// A minimum set of optimisations:
/// - Runs only the mandatory-inlining hook, not normal cost-based inlining
/// - Simplifies the call graph (by removing chained gotos)
/// - Sorts blocks into a rough flow order
void MIROptimiseMin(const StaticTraitResolve& resolve, const ::HIR::ItemPath& path, ::MIR::Function& fcn, const ::HIR::Function::argsT& args, const ::HIR::TypeData* ret_type) {
    static Span sp;
    TRACE_FUNCTION_F(path);
    ::MIR::TypeResolve state {
        sp, resolve, FMT_CB(ss, ss << path;), ret_type, args, fcn
    };

    while (MIROptimiseInlining(state, fcn, true)) {
        MIRCleanup(resolve, path, fcn, args, ret_type);
        //MIR_Dump_Fcn(::std::cout, fcn);
        if (checkAfterAll()) {
            MIRValidate(resolve, path, fcn, args, ret_type);
        }
    }

    MIROptimiseBlockSimplify(state, fcn);
    MIROptimiseUnifyBlocks(state, fcn);

    //MIR_Optimise_GarbageCollect_Partial(state, fcn);

    // NOTE: No check here, this version of optimise is pretty reliable
    //if( check_mode() >= CHECKMODE_FINAL ) {
    //    MIR_Validate(resolve, path, fcn, args, ret_type);
    //}
    MIROptimiseGarbageCollect(state, fcn);
    //MIR_Validate_Full(resolve, path, fcn, args, ret_type);
    MIRSortBlocks(resolve, path, fcn);

#if CHECK_AFTER_DONE > 1
    if (checkMode() >= CHECKMODE_FINAL) {
        MIRValidate(resolve, path, fcn, args, ret_type);
    }
#endif
    return;
}

/// Perfom inlining only, using a list of monomorphised functions, then cleans up the flow graph
///
/// Returns true if any optimisation was performed
bool MIROptimiseInline(const StaticTraitResolve& resolve, const ::HIR::ItemPath& path, ::MIR::Function& fcn, const ::HIR::Function::argsT& args, const ::HIR::TypeData* ret_type, const TransList& list, unsigned optLevel) {
    static Span sp;
    bool rv = false;
    TRACE_FUNCTION_FR(path, rv);
    ::MIR::TypeResolve state {
        sp, resolve, FMT_CB(ss, ss << path;), ret_type, args, fcn
    };

    while (MIROptimiseInlining(state, fcn, false, &list)) {
        MIRCleanup(resolve, path, fcn, args, ret_type);
        if (checkAfterAll()) {
            MIRValidate(resolve, path, fcn, args, ret_type);
        }
        rv = true;
    }

    if (rv) {
        MIROptimise(resolve, path, fcn, args, ret_type, optLevel, /*do_inline=*/false);
    }

    return rv;
}

void MIROptimise(const StaticTraitResolve& resolve, const ::HIR::ItemPath& path, ::MIR::Function& fcn, const ::HIR::Function::argsT& args, const ::HIR::TypeData* ret_type, unsigned optLevel, bool doInline /*=true*/, bool validate /*=true*/) {
    static Span sp;
    assert(optLevel > 0);
    TRACE_FUNCTION_F(path);
    ::MIR::TypeResolve state {
        sp, resolve, FMT_CB(ss, ss << path;), ret_type, args, fcn
    };

    bool changeHappened;
    unsigned int passNum = 0;
    do {
        MIR_ASSERT(state, passNum < 100, "Too many MIR optimisation iterations");

        changeHappened = false;
        TRACE_FUNCTION_FR("Pass " << passNum, changeHappened);

        // >> Simplify call graph (removes gotos to blocks with a single use)
        if (MIROptimiseBlockSimplify(state, fcn)) {
#if DUMP_AFTER_ALL
            if (debugEnabled()) {
                MIRDumpFcn(::std::cout, fcn);
            }
#endif
            if (checkAfterAll()) {
                MIRValidate(resolve, path, fcn, args, ret_type);
            }
            // NOTE: Don't set `change_happened`, as this is the first pass
        }
        //else { MIR_Validate(resolve, path, fcn, args, ret_type); }

        // >> Apply known constants
        if (MIROptimiseConstPropagate(state, fcn)) {
#if DUMP_AFTER_ALL
            if (debugEnabled()) {
                MIRDumpFcn(::std::cout, fcn);
            }
#endif
            if (checkAfterAll()) {
                MIRValidate(resolve, path, fcn, args, ret_type);
            }
            changeHappened = true;
        }

        // >> Attempt to remove useless temporaries
        if (MIROptimiseDeTemporary(state, fcn)) {
            // - Run until no changes
            while (MIROptimiseDeTemporary(state, fcn)) {
                if (checkAfterAll()) {
                    MIRValidate(resolve, path, fcn, args, ret_type);
                }
            }
#if DUMP_AFTER_ALL
            if (debugEnabled()) {
                MIRDumpFcn(::std::cout, fcn);
            }
#endif
            if (checkAfterAll()) {
                MIRValidate(resolve, path, fcn, args, ret_type);
            }
            changeHappened = true;
        }
        //else { MIR_Validate(resolve, path, fcn, args, ret_type); }

        // Level 2 adds the more expensive whole-local/dataflow transformations,
        // matching rustc's split between basic level-1 cleanup and its SROA/GVN/DSE tier.
        // >> Split apart aggregates that are never used such (Written once, never used directly)
        if (optLevel >= 2 && MIROptimiseSplitAggregates(state, fcn)) {
#if DUMP_AFTER_ALL
            if (debugEnabled()) {
                MIRDumpFcn(::std::cout, fcn);
            }
#endif
            if (checkAfterAll()) {
                MIRValidate(resolve, path, fcn, args, ret_type);
            }
            changeHappened = true;
        }
        //else { MIR_Validate(resolve, path, fcn, args, ret_type); }

        // >> Replace values from composites if they're known
        //   - Undoes the inefficiencies from the `match (a, b) { ... }` pattern
        if (optLevel >= 2 && MIROptimisePropagateKnownValues(state, fcn)) {
#if DUMP_AFTER_ALL
            if (debugEnabled()) {
                MIRDumpFcn(::std::cout, fcn);
            }
#endif
            if (checkAfterAll()) {
                MIRValidate(resolve, path, fcn, args, ret_type);
            }
            changeHappened = true;
        }
        //else { MIR_Validate(resolve, path, fcn, args, ret_type); }

        // TODO: Convert `&mut *mut_foo` into `mut_foo` if the source is movable and not used afterwards

        // >> Propagate/remove dead assignments
        if (MIROptimisePropagateSingleAssignments(state, fcn)) {
            // - Run until no changes
            while (MIROptimisePropagateSingleAssignments(state, fcn)) {
            }
#if DUMP_AFTER_ALL
            if (debugEnabled()) {
                MIRDumpFcn(::std::cout, fcn);
            }
#endif
            if (checkAfterAll()) {
                MIRValidate(resolve, path, fcn, args, ret_type);
            }
            changeHappened = true;
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
        if (MIROptimiseUnifyBlocks(state, fcn)) {
#if DUMP_AFTER_ALL
            if (debugEnabled()) {
                MIRDumpFcn(::std::cout, fcn);
            }
#endif
            if (checkAfterAll()) {
                MIRValidate(resolve, path, fcn, args, ret_type);
            }
            changeHappened = true;
        }
        // >> Remove assignments of unsed drop flags
        if (MIROptimiseDeadDropFlags(state, fcn)) {
#if DUMP_AFTER_ALL
            if (debugEnabled()) {
                MIRDumpFcn(::std::cout, fcn);
            }
#endif
            if (checkAfterAll()) {
                MIRValidate(resolve, path, fcn, args, ret_type);
            }
            changeHappened = true;
        }
        // >> Remove assignments that are never read
        if (optLevel >= 2 && MIROptimiseDeadAssignments(state, fcn)) {
#if DUMP_AFTER_ALL
            if (debugEnabled()) {
                MIRDumpFcn(::std::cout, fcn);
            }
#endif
            if (checkAfterAll()) {
                MIRValidate(resolve, path, fcn, args, ret_type);
            }
            changeHappened = true;
        }
        // >> Remove no-op assignments
        if (MIROptimiseNoopRemoval(state, fcn)) {
#if DUMP_AFTER_ALL
            if (debugEnabled()) {
                MIRDumpFcn(::std::cout, fcn);
            }
#endif
            if (checkAfterAll()) {
                MIRValidate(resolve, path, fcn, args, ret_type);
            }
            changeHappened = true;
        }

        // >> Remove re-borrow operations that don't need to exist
        if (MIROptimiseUselessReborrows(state, fcn)) {
#if DUMP_AFTER_ALL
            if (debugEnabled()) {
                MIRDumpFcn(::std::cout, fcn);
            }
#endif
            if (checkAfterAll()) {
                MIRValidate(resolve, path, fcn, args, ret_type);
            }
            changeHappened = true;
        }

        // >> If the first statement of a block is an assignment, and the last op of the previous is to that assignment's source, move up.
        if (MIROptimiseGotoAssign(state, fcn)) {
#if DUMP_AFTER_ALL
            if (debugEnabled()) {
                MIRDumpFcn(::std::cout, fcn);
            }
#endif
            if (checkAfterAll()) {
                MIRValidate(resolve, path, fcn, args, ret_type);
            }
            changeHappened = true;
        }

        // >> Inline short functions
        if (doInline && !changeHappened) {
            if (MIROptimiseInlining(state, fcn, /*minimal=*/false)) {
                // Apply cleanup again (as monomorpisation in inlining may have exposed a vtable call)
                MIRCleanup(resolve, path, fcn, args, ret_type);
                //MIR_Dump_Fcn(::std::cout, fcn);
#if DUMP_AFTER_ALL
                if (debugEnabled()) {
                    MIRDumpFcn(::std::cout, fcn);
                }
#endif
                if (checkAfterAll()) {
                    MIRValidate(resolve, path, fcn, args, ret_type);
                }
                changeHappened = true;
            }
        }

        if (changeHappened) {
#if DUMP_AFTER_PASS
            if (debugEnabled()) {
                MIRDumpFcn(::std::cout, fcn);
            }
#endif
            if (checkMode() == CHECKMODE_PASS) { // NOTE: Skipped if CHECKMODE_ALL
                MIRValidate(resolve, path, fcn, args, ret_type);
            }
        }
        //else { MIR_Validate(resolve, path, fcn, args, ret_type); }

        if (MIROptimiseGarbageCollectPartial(state, fcn)) {
            changeHappened = true;
#if DUMP_AFTER_ALL
            if (debugEnabled()) {
                MIRDumpFcn(::std::cout, fcn);
            }
#endif
            if (checkAfterAll()) {
                MIRValidate(resolve, path, fcn, args, ret_type);
            }
        }
        //else { MIR_Validate(resolve, path, fcn, args, ret_type); }

        passNum += 1;
    } while (changeHappened);

    // Run UnifyTemporaries last, then unify blocks, then run some
    // optimisations that might be affected

#if DUMP_AFTER_DONE
    if (debugEnabled()) {
        MIRDumpFcn(::std::cout, fcn);
    }
#endif
    if (validate && checkMode() >= CHECKMODE_FINAL) {
        // DEFENCE: Run validation _before_ GC (so validation errors refer to the pre-gc numbers)
        MIRValidate(resolve, path, fcn, args, ret_type);
    }
    // GC pass on blocks and variables
    // - Find unused blocks, then delete and rewrite all references.
    MIROptimiseGarbageCollect(state, fcn);

    //MIR_Validate_Full(resolve, path, fcn, args, ret_type);

    MIRSortBlocks(resolve, path, fcn);
    if (validate && checkMode() >= CHECKMODE_FINAL) {
        MIRValidate(resolve, path, fcn, args, ret_type);
    }
}

namespace {
    enum class ValUsage {
        Move,   // Moving read (even if T: Copy)
        Read,   // Non-moving read (e.g. indexing or deref, TODO: &move pointers?)
        Write,  // Mutation
        Borrow, // Any borrow
    };

    bool visitMirLvaluesInner(const ::MIR::LValue& lv, ValUsage u, ::std::function<bool(const ::MIR::LValue&, ValUsage)> cb) {
        for (const auto& w : lv.wrappers) {
            if (w.is_Index()) {
                if (cb(::MIR::LValue::newLocal(w.as_Index()), ValUsage::Read)) {
                    return true;
                }
            } else if (w.is_Deref()) {
                //u = ValUsage::Read;
            }
        }
        return cb(lv, u);
    }

    bool visitMirLvalueMut(::MIR::LValue& lv, ValUsage u, ::std::function<bool(::MIR::LValue::MRef&, ValUsage)> cb) {
        auto lvr = ::MIR::LValue::MRef(lv);
        do {
            if (cb(lvr, u)) {
                return true;
            }
            // TODO: Use a TU_MATCH?
            if (lvr.is_Index()) {
                auto ilv = ::MIR::LValue::newLocal(lvr.as_Index());
                auto ilvR = ::MIR::LValue::MRef(ilv);
                bool rv = cb(ilvR, ValUsage::Read);
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
        } while (lvr.tryUnwrap());
        return false;
    }

    bool visitMirLvalueRawMut(::MIR::LValue& lv, ValUsage u, ::std::function<bool(::MIR::LValue&, ValUsage)> cb) {
        return cb(lv, u);
    }

    bool visitMirLvalueMut(::MIR::Param& p, ValUsage u, ::std::function<bool(::MIR::LValue&, ValUsage)> cb) {
        if (auto* e = p.opt_LValue()) {
            return visitMirLvalueRawMut(*e, u, cb);
        } else {
            return false;
        }
    }

    bool visitMirLvaluesMut(::MIR::RValue& rval, ::std::function<bool(::MIR::LValue&, ValUsage)> cb) {
        bool rv = false;
        TU_MATCH_HDRA( (rval), {)
        TU_ARMA(Use, se) {
                rv |= visitMirLvalueRawMut(se, ValUsage::Move, cb); // Can move
            }
            TU_ARMA(Constant, se) {
            }
            TU_ARMA(SizedArray, se) {
                rv |= visitMirLvalueMut(se.val, ValUsage::Read, cb); // Has to be Read
            }
            TU_ARMA(Borrow, se) {
                rv |= visitMirLvalueRawMut(se.val, ValUsage::Borrow, cb);
            }
            TU_ARMA(Cast, se) {
                rv |= visitMirLvalueRawMut(se.val, ValUsage::Read, cb); // Also has to be read
            }
            TU_ARMA(BinOp, se) {
                rv |= visitMirLvalueMut(se.valL, ValUsage::Read, cb); // Same
                rv |= visitMirLvalueMut(se.valR, ValUsage::Read, cb);
            }
            TU_ARMA(UniOp, se) {
                rv |= visitMirLvalueRawMut(se.val, ValUsage::Read, cb);
            }
            TU_ARMA(DstMeta, se) {
                rv |= visitMirLvalueRawMut(se.val, ValUsage::Read, cb); // Reads
            }
            TU_ARMA(DstPtr, se) {
                rv |= visitMirLvalueRawMut(se.val, ValUsage::Read, cb);
            }
            TU_ARMA(MakeDst, se) {
                rv |= visitMirLvalueMut(se.ptrVal, ValUsage::Move, cb);
                rv |= visitMirLvalueMut(se.metaVal, ValUsage::Read, cb); // Note, metadata has to be Copy
            }
            TU_ARMA(Tuple, se) {
                for (auto& v : se.vals) {
                    rv |= visitMirLvalueMut(v, ValUsage::Move, cb);
                }
            }
            TU_ARMA(Array, se) {
                for (auto& v : se.vals) {
                    rv |= visitMirLvalueMut(v, ValUsage::Move, cb);
                }
            }
            TU_ARMA(UnionVariant, se) {
                rv |= visitMirLvalueMut(se.val, ValUsage::Move, cb);
            }
            TU_ARMA(EnumVariant, se) {
                for (auto& v : se.vals) {
                    rv |= visitMirLvalueMut(v, ValUsage::Move, cb);
                }
            }
            TU_ARMA(Struct, se) {
                for (auto& v : se.vals) {
                    rv |= visitMirLvalueMut(v, ValUsage::Move, cb);
                }
            }
        }
        return rv;
    }

    bool visitMirLvalues(const ::MIR::RValue& rval, ::std::function<bool(const ::MIR::LValue&, ValUsage)> cb) {
        return visitMirLvaluesMut(const_cast<::MIR::RValue&>(rval), [&](auto& lv, auto u) {
            return cb(lv, u);
        });
    }

    bool visitMirLvaluesMut(::MIR::Statement& stmt, ::std::function<bool(::MIR::LValue&, ValUsage)> cb) {
        bool rv = false;
        TU_MATCH_HDRA( (stmt), {)
        TU_ARMA(Assign, e) {
                rv |= visitMirLvaluesMut(e.src, cb);
                rv |= visitMirLvalueRawMut(e.dst, ValUsage::Write, cb);
            }
            TU_ARMA(Asm, e) {
                for (auto& v : e.inputs) {
                    rv |= visitMirLvalueRawMut(v.second, ValUsage::Read, cb);
                }
                for (auto& v : e.outputs) {
                    rv |= visitMirLvalueRawMut(v.second, ValUsage::Write, cb);
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
                                rv |= visitMirLvalueMut(*v.input, ValUsage::Read, cb);
                            }
                            if (v.output) {
                                rv |= visitMirLvalueRawMut(*v.output, ValUsage::Write, cb);
                            }
                        }
                }
                }
            }
            TU_ARMA(SetDropFlag, e) {
            }
            TU_ARMA(SaveDropFlag, e) {
                rv |= visitMirLvalueRawMut(e.slot, ValUsage::Write, cb);
            }
            TU_ARMA(LoadDropFlag, e) {
                rv |= visitMirLvalueRawMut(e.slot, ValUsage::Read, cb);
            }
            TU_ARMA(ScopeEnd, e) {
            }
        }
        return rv;
    }

    bool visitMirLvalues(const ::MIR::Statement& stmt, ::std::function<bool(const ::MIR::LValue&, ValUsage)> cb) {
        return visitMirLvaluesMut(const_cast<::MIR::Statement&>(stmt), [&](auto& lv, auto im) {
            return cb(lv, im);
        });
    }

    bool visitMirLvaluesMut(::MIR::Terminator& term, ::std::function<bool(::MIR::LValue&, ValUsage)> cb) {
        bool rv = false;
        TU_MATCH_HDRA( (term), {)
        TU_ARMA(Incomplete, e) {
            }
            TU_ARMA(Return, e) {
            }
            TU_ARMA(UnwindResume, e) {
            }
            TU_ARMA(UnwindTerminate, e) {
            }
            TU_ARMA(Unreachable, e) {
            }
            TU_ARMA(Goto, e) {
            }
            TU_ARMA(If, e) {
                rv |= visitMirLvalueRawMut(e.cond, ValUsage::Read, cb);
            }
            TU_ARMA(Switch, e) {
                rv |= visitMirLvalueRawMut(e.val, ValUsage::Read, cb);
            }
            TU_ARMA(SwitchValue, e) {
                rv |= visitMirLvalueRawMut(e.val, ValUsage::Read, cb);
            }
            TU_ARMA(Drop, e) {
                rv |= visitMirLvalueRawMut(e.slot, ValUsage::Move, cb);
            }
            TU_ARMA(Call, e) {
                if (e.fcn.is_Value()) {
                    rv |= visitMirLvalueRawMut(e.fcn.as_Value(), ValUsage::Read, cb);
                }
                for (auto& v : e.args) {
                    rv |= visitMirLvalueMut(v, ValUsage::Move, cb);
                }
                rv |= visitMirLvalueRawMut(e.retVal, ValUsage::Write, cb);
            }
        }
        return rv;
    }

    bool visitMirLvalues(const ::MIR::Terminator& term, ::std::function<bool(const ::MIR::LValue&, ValUsage)> cb) {
        return visitMirLvaluesMut(const_cast<::MIR::Terminator&>(term), [&](auto& lv, auto im) {
            return cb(lv, im);
        });
    }

    void visitMirLvaluesMut(::MIR::TypeResolve& state, ::MIR::Function& fcn, ::std::function<bool(::MIR::LValue&, ValUsage)> cb) {
        for (unsigned int blockIdx = 0; blockIdx < fcn.blocks.size(); blockIdx++) {
            auto& block = fcn.blocks[blockIdx];
            for (auto& stmt : block.statements) {
                state.setCurStmt(blockIdx, (&stmt - &block.statements.front()));
                visitMirLvaluesMut(stmt, cb);
            }
            if (block.terminator.tag() == ::MIR::Terminator::TAGDEAD) {
                continue;
            }
            state.setCurStmtTerm(blockIdx);
            visitMirLvaluesMut(block.terminator, cb);
        }
    }

    void visitMirLvalues(::MIR::TypeResolve& state, const ::MIR::Function& fcn, ::std::function<bool(const ::MIR::LValue&, ValUsage)> cb) {
        visitMirLvaluesMut(state, const_cast<::MIR::Function&>(fcn), [&](auto& lv, auto im) {
            return cb(lv, im);
        });
    }

    struct ParamsSet: public MonomorphiserPP {
        ::HIR::PathParams impl_params;
        const ::HIR::PathParams* fcn_params;
        const ::HIR::TypeData* self_ty;
        const ::HIR::GenericParams* implParamsDef;
        const ::HIR::GenericParams* fcnParamsDef;

        ::HIR::PathParams fcnParamsTmp;

        explicit ParamsSet(HIR::TypeInterner& types)
            : MonomorphiserPP(types)
            , fcn_params(nullptr)
            , self_ty(nullptr)
            , implParamsDef(nullptr)
            , fcnParamsDef(nullptr)
        {
        }

        const ::HIR::TypeData* getSelfType() const override {
            return self_ty;
        }

        const ::HIR::PathParams* getImplParams() const override {
            return &impl_params;
        }

        const ::HIR::PathParams* getMethodParams() const override {
            return fcn_params;
        }

        const ::HIR::PathParams* getHrbParams() const override {
            return nullptr;
        }

        bool hasUnevaluatedValues() const {
            const auto check = [](const ::HIR::PathParams& params) {
                return ::std::any_of(params.values.begin(), params.values.end(), [](const auto& value) {
                    return value.is_Unevaluated() || value.is_Infer();
                });
            };
            return check(impl_params) || (fcn_params && check(*fcn_params));
        }
    };

    const ::MIR::Function* getCalledMir(const ::MIR::TypeResolve& state, const TransList* list, const ::HIR::Path& path, ParamsSet& params) {
        MonomorphState outParams(state.mResolve.crate.types);
        auto e = state.mResolve.getValue(state.sp, path, outParams, /*sig_only*/ false, &params.implParamsDef);
        DEBUG(e.tagStr() << " " << outParams);
        params.fcn_params = outParams.getMethodParams();
        params.impl_params = outParams.ppImpl == nullptr ? ::HIR::PathParams() : outParams.ppImpl == &outParams.ppImplData ? std::move(outParams.ppImplData) : outParams.ppImpl->clone();

        // If a TransList is avaliable, then all referenced functions must be in it.
        if (list) {
            const auto* transFcn = list->findFunction(path);
            if (!transFcn) {
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

            const auto& hirFcn = *transFcn->ptr;
            if (transFcn->monomorphised.code) {
                //DEBUG("Found monomorphised - PP=" << params.impl_params << "," << *params.fcn_params);
                return &*transFcn->monomorphised.code;
            } else if (const auto* mir = hirFcn.mCode.getMirOpt()) {
                //DEBUG("Found concrete - PP=" << params.impl_params << "," << *params.fcn_params);
                MIR_ASSERT(state, hirFcn.mParams.types.empty(), "Enumeration failure - Function had params, but wasn't monomorphised - " << path);
                // TODO: Check for trait methods too?
                return mir;
            } else {
                DEBUG("No MIR");
                MIR_ASSERT(state, !hirFcn.mCode, "LowerMIR failure - No MIR but HIR is present?! - " << path);
                // External function (no MIR present)
                return nullptr;
            }
        }

        TU_MATCH_HDRA( (path.mData), {)
        TU_ARMA(Generic, pe) {
                params.self_ty = nullptr;
            }
            TU_ARMA(UfcsKnown, pe) {
                params.self_ty = pe.type;
            }
            TU_ARMA(UfcsInherent, pe) {
                params.self_ty = pe.type;
            }
            TU_ARMA(UfcsUnknown, pe) {
                MIR_BUG(state, "UfcsUnknown hit - " << path);
            }
        }

        TU_MATCH_HDRA( (e), { )
        default:
            MIR_BUG(state, "MIR Call of " << e.tagStr() << " - " << path);
            TU_ARMA(NotFound, _) {
                return nullptr;
            }
            TU_ARMA(NotYetKnown, _) {
                return nullptr;
            }
            TU_ARMA(Function, f) {
                params.fcnParamsDef = &f->mParams;
                return f->mCode.getMirOpt();
            }
        }
        return nullptr;
    }

    void visitTerminatorTargetMut(::MIR::Terminator& term, ::std::function<void(::MIR::BasicBlockId&)> cb) {
        TU_MATCH_HDRA( (term), {)
        TU_ARMA(Incomplete, e) {
            }
            TU_ARMA(Return, e) {
            }
            TU_ARMA(UnwindResume, e) {
            }
            TU_ARMA(UnwindTerminate, e) {
            }
            TU_ARMA(Unreachable, e) {
            }
            TU_ARMA(Goto, e) {
                cb(e);
            }
            TU_ARMA(If, e) {
                cb(e.bbTrue);
                cb(e.bbFalse);
            }
            TU_ARMA(Switch, e) {
                for (auto& target : e.targets) {
                    cb(target);
                }
                if (e.validFlag != ~0u) {
                    cb(e.invalidTarget);
                }
            }
            TU_ARMA(SwitchValue, e) {
                for (auto& target : e.targets) {
                    cb(target);
                }
                cb(e.defTarget);
            }
            TU_ARMA(Drop, e) {
                cb(e.target);
                TU_IFLET(::MIR::UnwindAction, e.unwind, Cleanup, target, cb(target);)
            }
            TU_ARMA(Call, e) {
                cb(e.retBlock);
                TU_IFLET(::MIR::UnwindAction, e.unwind, Cleanup, target, cb(target);)
            }
        }
    }

    void visitTerminatorTarget(const ::MIR::Terminator& term, ::std::function<void(const ::MIR::BasicBlockId&)> cb) {
        visitTerminatorTargetMut(const_cast<::MIR::Terminator&>(term), cb);
    }

    void visitBlocksMut(::MIR::TypeResolve& state, ::MIR::Function& fcn, ::std::function<void(::MIR::BasicBlockId, ::MIR::BasicBlock&)> cb) {
        ::std::vector<bool> visited(fcn.blocks.size());
        ::std::vector<::MIR::BasicBlockId> toVisit;
        toVisit.push_back(0);
        while (toVisit.size() > 0) {
            auto bb = toVisit.back();
            toVisit.pop_back();
            if (visited[bb]) {
                continue;
            }
            visited[bb] = true;
            auto& block = fcn.blocks[bb];

            cb(bb, block);

            visitTerminatorTarget(block.terminator, [&](auto e) {
                if (!visited[e]) {
                    toVisit.push_back(e);
                }
            });
        }
    }

    void visitBlocks(::MIR::TypeResolve& state, const ::MIR::Function& fcn, ::std::function<void(::MIR::BasicBlockId, const ::MIR::BasicBlock&)> cb) {
        visitBlocksMut(state, const_cast<::MIR::Function&>(fcn), [cb](auto id, auto& blk) {
            cb(id, blk);
        });
    }

    /// Convert a MIR::Param into a MIR::RValue
    MIR::RValue paramToRvalue(MIR::Param param) {
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
bool MIROptimiseBlockSimplify(::MIR::TypeResolve& state, ::MIR::Function& fcn) {
    bool changed = false;
    TRACE_FUNCTION_FR("", changed);

    struct H {
        static ::MIR::BasicBlockId getNewTarget(const ::MIR::TypeResolve& state, ::MIR::BasicBlockId bb) {
            const auto& target = state.getBlock(bb);
            if (target.statements.size() != 0) {
                return bb;
            } else if (!target.terminator.is_Goto()) {
                return bb;
            } else {
                // Make sure we don't infinite loop (TODO: What about mutual recursion?)
                if (bb == target.terminator.as_Goto()) {
                    return bb;
                }

                return getNewTarget(state, target.terminator.as_Goto());
            }
        }
    };

    // >> Replace targets that point to a block that is just a goto
    for (auto& block : fcn.blocks) {
        visitTerminatorTargetMut(block.terminator, [&](auto& e) {
            if (&fcn.blocks[e] != &block) {
                auto newBb = H::getNewTarget(state, e);
                if (newBb != e) {
                    DEBUG("BB" << &block - fcn.blocks.data() << "/TERM: Rewrite bb reference " << e << " => " << newBb);
                    e = newBb;
                    changed = true;
                }
            }
        });

        // Handle chained switches of the same value
        // - Happens in libcore's atomics
        if (auto* te = block.terminator.opt_Switch()) {
            if (te->validFlag != ~0u) {
                continue;
            }
            for (auto& t : te->targets) {
                auto idx = &t - &te->targets.front();
                // The block must be a terminator only, and be a switch over the same value.
                if (fcn.blocks[t].statements.empty() && fcn.blocks[t].terminator.is_Switch()) {
                    const auto& nTe = fcn.blocks[t].terminator.as_Switch();
                    if (nTe.validFlag == ~0u && nTe.val == te->val) {
                        // If that's the case, then update this target with the equivalent from the new switch.
                        DEBUG("BB" << &block - fcn.blocks.data() << "/TERM: Update switch from BB" << t << " to BB" << nTe.targets[idx]);
                        t = nTe.targets[idx];
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
        ::std::vector<::MIR::BasicBlockId> toVisit;
        toVisit.push_back(0);
        uses[0]++;
        while (toVisit.size() > 0) {
            auto bb = toVisit.back();
            toVisit.pop_back();
            if (visited[bb]) {
                continue;
            }
            visited[bb] = true;
            const auto& block = fcn.blocks[bb];

            visitTerminatorTarget(block.terminator, [&](const auto& e) {
                if (!visited[e]) {
                    toVisit.push_back(e);
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
                    auto srcBlock = mv$(fcn.blocks[tgt]);
                    fcn.blocks[tgt].terminator = ::MIR::Terminator::make_Incomplete({});

                    for (auto& stmt : srcBlock.statements) {
                        block.statements.push_back(mv$(stmt));
                    }
                    block.terminator = mv$(srcBlock.terminator);
                    changed = true;
                }
            }
            i++;
        }
    }

    // >> If a block GOTOs a block that is just a `RETURN` or `DIVERGE`, then change terminator
    for (auto& block : fcn.blocks) {
        state.setCurStmtTerm(&block - &fcn.blocks.front());
        if (block.terminator.is_Goto()) {
            auto tgt = block.terminator.as_Goto();
            if (!fcn.blocks[tgt].statements.empty()) {
            } else if (fcn.blocks[tgt].terminator.is_Return()) {
                DEBUG(state << " -> Return");
                block.terminator = MIR::Terminator::make_Return({});
                changed = true;
            } else if (fcn.blocks[tgt].terminator.is_UnwindResume()) {
                DEBUG(state << " -> UnwindResume");
                block.terminator = MIR::Terminator::make_UnwindResume({});
                changed = true;
            } else if (fcn.blocks[tgt].terminator.is_UnwindTerminate()) {
                DEBUG(state << " -> UnwindTerminate");
                block.terminator = MIR::Terminator::make_UnwindTerminate({});
                changed = true;
            } else if (fcn.blocks[tgt].terminator.is_Unreachable()) {
                DEBUG(state << " -> Unreachable");
                block.terminator = MIR::Terminator::make_Unreachable({});
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
bool MIROptimiseInlining(::MIR::TypeResolve& state, ::MIR::Function& fcn, bool minimal, const TransList* list /*=nullptr*/) {
    bool inlineHappened = false;
    TRACE_FUNCTION_FR("", inlineHappened);

    struct InlineEvent {
        ::HIR::Path path;
        ::std::vector<size_t> bbList;

        InlineEvent(::HIR::Path p)
            : path(::std::move(p))
        {
        }

        bool hasBb(size_t i) const {
            return ::std::find(this->bbList.begin(), this->bbList.end(), i) != this->bbList.end();
        }

        void addRange(size_t start, size_t count) {
            for (size_t j = 0; j < count; j++) {
                this->bbList.push_back(start + j);
            }
        }
    };

    ::std::vector<InlineEvent> inlinedFunctions;

    struct H {
        struct Source {
            unsigned bbIdx;
            unsigned stmtIdx;
            const ::MIR::Statement* stmt;

            Source(unsigned bbIdx, unsigned stmtIdx, const ::MIR::Statement* stmt = nullptr)
                : bbIdx(bbIdx)
                , stmtIdx(stmtIdx)
                , stmt(stmt)
            {
            }
        };

        static Source findSource(const ::MIR::Function& fcn, unsigned bbIdx, unsigned stmtIdx, const ::MIR::LValue& val) {
            if (!val.wrappers.empty()) {
                return Source(bbIdx, stmtIdx);
            }
            const auto& bb = fcn.blocks.at(bbIdx);
            while (stmtIdx--) {
                const auto& stmt = bb.statements[stmtIdx];
                if (stmt.is_Asm()) {
                    return Source(bbIdx, stmtIdx);
                }
                if (stmt.is_Assign()) {
                    const auto& se = stmt.as_Assign();
                    if (se.dst == val) {
                        return Source(bbIdx, stmtIdx, &stmt);
                    }
                }
            }
            return Source(bbIdx, 0);
        }

        /// Checks if the passed lvalue would optimise/expand to a constant value
        static bool valueIsConst(const ::MIR::Function& fcn, unsigned bbIdx, unsigned stmtIdx, const ::MIR::LValue& val, const std::vector<::MIR::Param>& params) {
            if (val.root.is_Argument()) {
                auto a = val.root.as_Argument();
                return params[a].is_Constant() && !params[a].as_Constant().is_Const();
            }

            // Find the source of this lvalue, chase it backwards
            auto src = H::findSource(fcn, bbIdx, stmtIdx, val);
            if (src.stmt) {
                if (const auto* se = src.stmt->opt_Assign()) {
                    if (se->src.is_Use()) {
                        return valueIsConst(fcn, src.bbIdx, src.stmtIdx, se->src.as_Use(), params);
                    }
                    if (const auto* rve = se->src.opt_BinOp()) {
                        return valueIsConst(fcn, src.bbIdx, src.stmtIdx, rve->valL, params) && valueIsConst(fcn, src.bbIdx, src.stmtIdx, rve->valR, params);
                    }
                }
            }

            return false;
        }

        static bool valueIsConst(const ::MIR::Function& fcn, unsigned bbIdx, unsigned stmtIdx, const ::MIR::Param& val, const std::vector<::MIR::Param>& params) {
            if (val.is_LValue()) {
                return valueIsConst(fcn, bbIdx, stmtIdx, val.as_LValue(), params);
            } else {
                return val.is_Constant() && !val.as_Constant().is_Const();
            }
        }

        static bool canInline(const ::HIR::Path& path, const ::MIR::Function& fcn, const std::vector<::MIR::Param>& params, bool minimal) {
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
                const auto& blk0Te = fcn.blocks[0].terminator.as_Call();
                if (!(fcn.blocks[1].terminator.is_UnwindResume() || fcn.blocks[1].terminator.is_UnwindTerminate() || fcn.blocks[1].terminator.is_Unreachable())) {
                    return false;
                }
                if (fcn.blocks[0].statements.size() + fcn.blocks[1].statements.size() > 10) {
                    return false;
                }
                // Detect and avoid simple recursion.
                // - This won't detect mutual recursion - that also needs prevention.
                // TODO: This is the pre-monomorph path, but we're comparing with the post-monomorph path
                if (blk0Te.fcn.is_Path() && blk0Te.fcn.as_Path() == path) {
                    return false;
                }
                return true;
            } else if (fcn.blocks.size() == 3 && fcn.blocks[0].terminator.is_Call()) {
                const auto& blk0Te = fcn.blocks[0].terminator.as_Call();
                if (!(fcn.blocks[1].terminator.is_UnwindResume() || fcn.blocks[1].terminator.is_UnwindTerminate() || fcn.blocks[1].terminator.is_Unreachable() || fcn.blocks[1].terminator.is_Return())) {
                    return false;
                }
                if (!(fcn.blocks[2].terminator.is_UnwindResume() || fcn.blocks[2].terminator.is_UnwindTerminate() || fcn.blocks[2].terminator.is_Unreachable() || fcn.blocks[2].terminator.is_Return())) {
                    return false;
                }
                if (fcn.blocks[0].statements.size() + fcn.blocks[1].statements.size() + fcn.blocks[2].statements.size() > 10) {
                    return false;
                }
                // Detect and avoid simple recursion.
                // - This won't detect mutual recursion - that also needs prevention.
                // TODO: This is the pre-monomorph path, but we're comparing with the post-monomorph path
                if (blk0Te.fcn.is_Path() && blk0Te.fcn.as_Path() == path) {
                    return false;
                }
                return true;
            } else {
            }

            // TODO: If all inputs are known, then allow larger/complex functions (e.g. allow one call and any number of blocks?)
            // - Seen `min_by(const, const, fcn)` - that would be a trivial optimisation

            if (canInlineSwitchWrapper(path, fcn, params)) {
                return true;
            }
            if (canInlineSwitchValueWrapper(path, fcn, params)) {
                return true;
            }
            return false;
        }

        /// Case: A Switch that has all distinct arms that just call a function AND the value is over (effectively) a literal
        static bool canInlineSwitchWrapper(const ::HIR::Path& path, const ::MIR::Function& fcn, const std::vector<::MIR::Param>& params) {
            if (fcn.blocks.size() <= 1) {
                return false;
            }
            if (!fcn.blocks[0].terminator.is_Switch()) {
                return false;
            }
            const auto& teSwitch = fcn.blocks[0].terminator.as_Switch();
            // Setup + Arms + Return + Panic
            // - Handles the atomic wrappers
            if (fcn.blocks.size() != teSwitch.targets.size() + 3) {
                return false;
            }
            // Check for the switch value being an argument that is also a constant parameter being a Constant
            if (!valueIsConst(fcn, 0, fcn.blocks[0].statements.size(), teSwitch.val, params)) {
                return false;
            }
            // Check all arms of the switch are distinct
            for (const auto& tgt : teSwitch.targets) {
                if (std::find(teSwitch.targets.begin() + (1 + &tgt - teSwitch.targets.data()), teSwitch.targets.end(), tgt) != teSwitch.targets.end()) {
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
        static bool canInlineSwitchValueWrapper(const ::HIR::Path& path, const ::MIR::Function& fcn, const std::vector<::MIR::Param>& params) {
            if (fcn.blocks.size() <= 1) {
                return false;
            }
            if (!fcn.blocks[0].terminator.is_SwitchValue()) {
                return false;
            }
            const auto& teSwitch = fcn.blocks[0].terminator.as_SwitchValue();
            // Setup + Arms(+default) + Return + Panic
            // - Handles some code in crc32-fast that emits a 256-arm SwitchValue
            if (fcn.blocks.size() != teSwitch.targets.size() + 1 + 3) {
                return false;
            }
            // Check for the switch value being an argument that is also a constant parameter being a Constant
            if (!valueIsConst(fcn, 0, fcn.blocks[0].statements.size(), teSwitch.val, params)) {
                return false;
            }

            // Check all arms of the switch are distinct
            if (std::find(teSwitch.targets.begin(), teSwitch.targets.end(), teSwitch.defTarget) != teSwitch.targets.end()) {
                return false;
            }
            for (const auto& tgt : teSwitch.targets) {
                if (std::find(teSwitch.targets.begin() + (1 + &tgt - teSwitch.targets.data()), teSwitch.targets.end(), tgt) != teSwitch.targets.end()) {
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
        const ::StaticTraitResolve& mResolve;
        const ::MIR::Terminator::Data_Call& te;
        ::std::vector<unsigned> copyArgs; // Local indexes containing copies of Copy args
        ParamsSet params;
        unsigned int bbBase = ~0u;
        unsigned int varBase = ~0u;
        unsigned int dfBase = ~0u;

        size_t tmpEnd = 0;
        mutable ::std::vector<::MIR::Param> constAssignments;

        ::MIR::LValue retval;

        Cloner(const Span& sp, const ::StaticTraitResolve& resolve, ::MIR::Terminator::Data_Call& te)
            : ::MIR::Cloner(sp, resolve.crate.types)
            , mResolve(resolve)
            , te(te)
            , params(resolve.crate.types)
            , copyArgs(te.args.size(), ~0u)
        {
        }

        ::MIR::BasicBlockId mapBbIdx(::MIR::BasicBlockId idx) const override {
            return this->bbBase + idx;
        }

        virtual unsigned mapLocal(unsigned f) const {
            return this->varBase + f;
        }

        virtual unsigned mapDropFlag(unsigned f) const {
            return this->dfBase + f;
        }

        const HIR::TypeData* valueGenericType(HIR::GenericRef ce) const override {
            const HIR::GenericParams* p;
            switch (ce.group()) {
                case 0: // impl level
                    p = params.implParamsDef;
                    break;
                case 1: // method level
                    p = params.fcnParamsDef;
                    break;
                default:
                    TODO(sp, "Typecheck const generics - look up the type");
            }
            ASSERT_BUG(sp, p, "No generic list for " << ce);
            ASSERT_BUG(sp, ce.idx() < p->values.size(), "Generic param index out of range");
            return p->values.at(ce.idx()).mType;
        }

        const Monomorphiser& monomorphiser() const override {
            return params;
        }

        const StaticTraitResolve* resolve() const override {
            return &this->mResolve;
        }

        ::MIR::BasicBlock cloneBb(const ::MIR::BasicBlock& src, unsigned srcIdx, unsigned newIdx) const {
            ::MIR::BasicBlock rv;
            rv.isCleanup = src.isCleanup;
            rv.statements.reserve(src.statements.size());
            for (const auto& stmt : src.statements) {
                DEBUG("BB" << srcIdx << "->BB" << newIdx << "/" << rv.statements.size() << ": " << stmt);
                rv.statements.push_back(this->cloneStmt(stmt));
                DEBUG("-> " << rv.statements.back());
            }
            DEBUG("BB" << srcIdx << "->BB" << newIdx << "/" << rv.statements.size() << ": " << src.terminator);
            if (src.terminator.is_Return()) {
                rv.statements.push_back(::MIR::Statement::make_Assign({this->te.retVal.clone(), this->retval.clone()}));
                DEBUG("++ " << rv.statements.back());
            }
            rv.terminator = this->cloneTerm(src.terminator);
            DEBUG("-> " << rv.terminator);
            return rv;
        }

        ::MIR::Terminator cloneTerm(const ::MIR::Terminator& src) const override {
            if (src.is_Return()) {
                return ::MIR::Terminator::make_Goto(this->te.retBlock);
            } else if (src.is_UnwindResume()) {
                TU_MATCHA((this->te.unwind), (ue),
                    (Continue, return ::MIR::Terminator::make_UnwindResume({});),
                    (Cleanup, return ::MIR::Terminator::make_Goto(ue);),
                    (Terminate, return ::MIR::Terminator::make_UnwindTerminate({});),
                    (Unreachable, return ::MIR::Terminator::make_Unreachable({});)
                )
                throw "";
            } else {
                return ::MIR::Cloner::cloneTerm(src);
            }
        }

        ::MIR::LValue cloneLval(const ::MIR::LValue& src) const override {
            auto rv = ::MIR::Cloner::cloneLval(src);
            if (rv.root.is_Return()) {
                return this->retval.cloneWrapped(std::move(rv.wrappers));
            }
            if (rv.root.is_Argument()) {
                auto se = rv.root.as_Argument();
                const auto& arg = this->te.args.at(se);
                if (this->copyArgs[se] != ~0u) {
                    return ::MIR::LValue(::MIR::LValue::Storage::newLocal(this->copyArgs[se]), std::move(rv.wrappers));
                } else {
                    assert(!arg.is_Constant()); // Should have been handled in the above
                    return arg.as_LValue().cloneWrapped(std::move(rv.wrappers));
                }
            }
            return rv;
        }
    };

    for (unsigned int i = 0; i < fcn.blocks.size(); i++) {
        state.setCurStmtTerm(i);
        if (auto* te = fcn.blocks[i].terminator.opt_Call()) {
            if (!te->fcn.is_Path()) {
                continue;
            }
            const auto& path = te->fcn.as_Path();
            DEBUG(state << fcn.blocks[i].terminator);

            for (const auto& e : inlinedFunctions) {
                if (path == e.path && e.hasBb(i)) {
                    MIR_BUG(state, "Recursive inline of " << path);
                }
            }

            Cloner cloner{state.sp, state.mResolve, *te};
            const auto* calledMir = getCalledMir(state, list, path, cloner.params);
            if (!calledMir) {
                continue;
            }
            if (calledMir == &fcn) {
                DEBUG("Can't inline - recursion");
                continue;
            }
            if (cloner.params.hasUnevaluatedValues()) {
                DEBUG("Can't inline - const substitutions are not concrete");
                continue;
            }

            // Check the size of the target function.
            // Inline IF:
            // - First BB ends with a call and total count is 3
            // - Statement count smaller than 10
            if (!H::canInline(path, *calledMir, te->args, minimal)) {
                DEBUG("Can't inline " << path);
                continue;
            }
            TRACE_FUNCTION_F("Inline " << path);

            // Allocate a temporary for the return value
            {
                cloner.retval = ::MIR::LValue::newLocal(fcn.locals.size());
                DEBUG("- Storing return value in " << cloner.retval);
                ::HIR::TypeRef tmpTy;
                fcn.locals.push_back(state.getLvalueType(tmpTy, te->retVal));
                //fcn.local_names.push_back( "" );
            }

            // Monomorph locals and append
            cloner.varBase = fcn.locals.size();
            for (const auto& ty : calledMir->locals) {
                fcn.locals.push_back(cloner.monomorph(ty));
            }
            cloner.tmpEnd = fcn.locals.size();

            cloner.dfBase = fcn.dropFlags.size();
            fcn.dropFlags.insert(fcn.dropFlags.end(), calledMir->dropFlags.begin(), calledMir->dropFlags.end());
            cloner.bbBase = fcn.blocks.size();

            // Store all Copy lvalue arguments and Constants in variables
            for (size_t i = 0; i < te->args.size(); i++) {
                const auto& a = te->args[i];
                if (!a.is_LValue() || state.lvalueIsCopy(a.as_LValue())) {
                    cloner.copyArgs[i] = cloner.tmpEnd + cloner.constAssignments.size();
                    cloner.constAssignments.push_back(a.clone());
                    DEBUG("- Taking a copy of arg " << i << " (" << a << ") in Local(" << cloner.copyArgs[i] << ")");
                }
            }

            // Append monomorphised copy of all blocks.
            // > Arguments replaced by input lvalues
            ::std::vector<::MIR::BasicBlock> newBlocks;
            newBlocks.reserve(calledMir->blocks.size());
            for (const auto& bb : calledMir->blocks) {
                newBlocks.push_back(cloner.cloneBb(bb, (&bb - calledMir->blocks.data()), fcn.blocks.size() + newBlocks.size()));
            }

            // > Append new temporaries
            DEBUG("- Insert argument lval assignments");
            for (auto& val : cloner.constAssignments) {
                ::HIR::TypeRef tmp;
                auto ty = val.is_Constant() ? state.getConstType(val.as_Constant()) : state.getLvalueType(tmp, val.as_LValue());
                auto lv = ::MIR::LValue::newLocal(static_cast<unsigned>(fcn.locals.size()));
                fcn.locals.push_back(mv$(ty));
                auto rval = val.is_Constant() ? ::MIR::RValue(mv$(val.as_Constant())) : ::MIR::RValue(mv$(val.as_LValue()));
                auto stmt = ::MIR::Statement::make_Assign({mv$(lv), mv$(rval)});
                DEBUG("++ " << stmt);
                newBlocks[0].statements.insert(newBlocks[0].statements.begin(), mv$(stmt));
            }
            cloner.constAssignments.clear();

            // Record the inline event
            for (auto& e : inlinedFunctions) {
                if (e.hasBb(i)) {
                    e.addRange(cloner.bbBase, newBlocks.size());
                }
            }
            inlinedFunctions.push_back(InlineEvent(path.clone()));
            inlinedFunctions.back().addRange(cloner.bbBase, newBlocks.size());

            // Apply
            DEBUG("- Append new blocks");
            fcn.blocks.reserve(fcn.blocks.size() + newBlocks.size());
            for (auto& b : newBlocks) {
                fcn.blocks.push_back(mv$(b));
            }
            fcn.blocks[i].terminator = ::MIR::Terminator::make_Goto(cloner.bbBase);
            inlineHappened = true;

            // TODO: Store the inlined path along with the start and end BBs, and then use that to detect recursive
            // inlining
            // - Recursive inlining should be an immediate panic.
        }
    }
    return inlineHappened;
}

namespace {
    struct OptimiseStmtRef {
        unsigned bbIdx;
        unsigned stmtIdx;

        OptimiseStmtRef()
            : bbIdx(~0u)
            , stmtIdx(0)
        {
        }

        OptimiseStmtRef(unsigned b, unsigned s)
            : bbIdx(b)
            , stmtIdx(s)
        {
        }

        bool operator==(const OptimiseStmtRef& x) const {
            return bbIdx == x.bbIdx && stmtIdx == x.stmtIdx;
        }
    };

    ::std::ostream& operator<<(::std::ostream& os, const OptimiseStmtRef& x) {
        return os << "BB" << x.bbIdx << "/" << x.stmtIdx;
    }

    // Iterates the path between two positions, NOT visiting entry specified by `end`
    enum class IterPathRes {
        Abort,
        EarlyTrue,
        Complete,
    };

    IterPathRes iterPath(const ::MIR::Function& fcn, const OptimiseStmtRef& start, const OptimiseStmtRef& end, ::std::function<bool(OptimiseStmtRef, const ::MIR::Statement&)> cbStmt, ::std::function<bool(OptimiseStmtRef, const ::MIR::Terminator&)> cbTerm) {
        if (start.bbIdx == end.bbIdx) {
            assert(start.stmtIdx <= end.stmtIdx);
        }

        auto vistedBbs = ::std::set<unsigned>();
        // Loop while not equal (either not in the right block, or before the statement) to the end point
        for (auto ref = start; ref.bbIdx != end.bbIdx || ref.stmtIdx < end.stmtIdx;) {
            const auto& bb = fcn.blocks.at(ref.bbIdx);
            if (ref.stmtIdx < bb.statements.size()) {
                DEBUG(ref << " " << bb.statements.at(ref.stmtIdx));
                if (cbStmt(ref, bb.statements.at(ref.stmtIdx))) {
                    DEBUG("> Early true");
                    return IterPathRes::EarlyTrue;
                }

                ref.stmtIdx++;
            } else {
                DEBUG(ref << " " << bb.terminator);
                if (cbTerm(ref, bb.terminator)) {
                    DEBUG("> Early true");
                    return IterPathRes::EarlyTrue;
                }

                // If this is the end point, break out before checking the terminator for looping
                if (ref.bbIdx == end.bbIdx) {
                    // ^ don't need to check the statment index, this is the last "statement"
                    break;
                }

                // If this terminator is a Goto, follow it (tracking for loops)
                if (const auto* te = bb.terminator.opt_Goto()) {
                    // Possibly loop into the next block
                    if (!vistedBbs.insert(*te).second) {
                        DEBUG("> Loop abort");
                        return IterPathRes::Abort;
                    }
                    ref.stmtIdx = 0;
                    ref.bbIdx = *te;
                }
                // A call's panic edge cannot reach `end`, so only follow the
                // normal return edge while inspecting a path between two
                // positions.
                else if (const auto* te = bb.terminator.opt_Call()) {
                    // Possibly loop into the next block
                    if (!vistedBbs.insert(te->retBlock).second) {
                        DEBUG("> Loop abort");
                        return IterPathRes::Abort;
                    }
                    ref.stmtIdx = 0;
                    ref.bbIdx = te->retBlock;
                } else {
                    DEBUG("> Terminator abort");
                    return IterPathRes::Abort;
                }
            }
        }
        return IterPathRes::Complete;
    }

    ::std::function<bool(const ::MIR::LValue&, ValUsage)> checkInvalidatesLvalueCb(const ::MIR::LValue& val, bool is_copy, bool alsoRead = false) {
        bool hasIndex = ::std::any_of(val.wrappers.begin(), val.wrappers.end(), [](const auto& w) {
            return w.is_Index();
        });
        // Value is invalidated if it's used with ValUsage::Write or ValUsage::Borrow
        // - Same applies to any component of the lvalue
        return [&val, hasIndex, is_copy, alsoRead](const ::MIR::LValue& lv, ValUsage vu) {
            switch (vu) {
                    // - Ideally this would check if it DOES invalidate
                case ValUsage::Write:
                case ValUsage::Borrow:
                    // (Possibly) mutating use, check if it impacts the root or one of the indexes
                    if (lv.root == val.root) {
                        return true;
                    }
                    // If the desired lvalue has an index in it's wrappers, AND the current lvalue is a local
                    if (hasIndex && lv.root.is_Local()) {
                        // Search for any wrapper on `val` that Index(lv)
                        for (const auto& w : val.wrappers) {
                            if (w.is_Index() && w.as_Index() == lv.root.as_Local()) {
                                // This lvalue is changed, so the index is invalidated
                                return true;
                            }
                        }
                    }
                    break;
                case ValUsage::Move: // A move can invalidate
                    if (is_copy) {
                    } else if (lv.root == val.root) {
                        // Check if `lv`'s wrappers are a subset of `val`'s
                        auto l = std::min(lv.wrappers.size(), val.wrappers.size());
                        for (size_t i = 0; i < l; i++) {
                            // A wrapper differs, won't invalidate
                            if (lv.wrappers[i] != val.wrappers[i]) {
                                return false;
                            }
                        }
                        return true;
                    }
                    break;
                case ValUsage::Read:
                    if (alsoRead) {
                        // NOTE: A read of the same root is a read of this value (what if they're disjoint fields?)
                        if (lv.root == val.root) {
                            return true;
                        }
                    }
                    break;
            }
            return false;
        };
    }

    bool checkInvalidatesLvalue(const ::MIR::Statement& stmt, const ::MIR::LValue& val, bool is_copy, bool alsoRead = false) {
        return visitMirLvalues(stmt, checkInvalidatesLvalueCb(val, is_copy, alsoRead));
    }

    bool checkInvalidatesLvalue(const ::MIR::Terminator& term, const ::MIR::LValue& val, bool is_copy, bool alsoRead = false) {
        return visitMirLvalues(term, checkInvalidatesLvalueCb(val, is_copy, alsoRead));
    }
}

// --------------------------------------------------------------------
// Locates locals that are only set/used once, and replaces them with
//  their source IF the source isn't invalidated
// --------------------------------------------------------------------
bool MIROptimiseDeTemporarySingleSetAndUse(::MIR::TypeResolve& state, ::MIR::Function& fcn) {
    bool changed = false;
    TRACE_FUNCTION_FR("", changed);

    // Find all single-use/single-write locals
    // - IF the usage is a RValue::Use, AND the usage destination is not invalidated between set/use
    //  - Replace initialisation destination with usage destination (delete usage statement)
    // - IF the source a Use/Constant, AND is not invalidated between set/use
    //  - Replace usage with the original source
    struct LocalUsage {
        unsigned nWrite;
        unsigned nRead;
        unsigned nBorrow;
        OptimiseStmtRef setLoc;
        OptimiseStmtRef useLoc;

        LocalUsage()
            : nWrite(0)
            , nRead(0)
            , nBorrow(0)
        {
        }
    };

    auto usageInfo = ::std::vector<LocalUsage>(fcn.locals.size());

    // 1. Enumrate usage
    {
        auto getCurLoc = [&state]() {
            return OptimiseStmtRef(state.getCurBlock(), state.getCurStmtOfs());
        };
        auto visitCb = [&](const ::MIR::LValue& lv, auto vu) {
            if (!lv.wrappers.empty()) {
                vu = ValUsage::Read;
            }
            for (const auto& w : lv.wrappers) {
                if (w.is_Index()) {
                    auto& slot = usageInfo[w.as_Index()];
                    slot.nRead += 1;
                    slot.useLoc = getCurLoc();
                    //DEBUG(lv << " index use");
                }
            }
            if (lv.root.is_Local()) {
                auto& slot = usageInfo[lv.root.as_Local()];
                switch (vu) {
                    case ValUsage::Write:
                        slot.nWrite += 1;
                        slot.setLoc = getCurLoc();
                        //DEBUG(lv << " set");
                        break;
                    case ValUsage::Move:
                        slot.nRead += 1;
                        slot.useLoc = getCurLoc();
                        //DEBUG(lv << " use");
                        break;
                    case ValUsage::Read:
                    case ValUsage::Borrow:
                        slot.nBorrow += 1;
                        //DEBUG(lv << " borrow");
                        break;
                }
            }
            return false;
        };
        visitMirLvalues(state, fcn, visitCb);
    }

    // 2. Find any local with 1 write, 1 read, and no borrows
    for (size_t varIdx = 0; varIdx < fcn.locals.size(); varIdx++) {
        const auto& slot = usageInfo[varIdx];
        auto thisVar = ::MIR::LValue::newLocal(varIdx);
        //ASSERT_BUG(Span(), slot.n_write > 0, "Variable " << var_idx << " not written?");
        DEBUG("_" << varIdx << ": " << slot.nWrite << "," << slot.nRead << "," << slot.nBorrow);
        if (slot.nWrite == 1 && slot.nRead == 1 && slot.nBorrow == 0) {
            // Single-use variable, now check how we can eliminate it
            DEBUG("Single-use: _" << varIdx << " - Set " << slot.setLoc << ", Use " << slot.useLoc);

            auto& useBb = fcn.blocks[slot.useLoc.bbIdx];
            auto& setBb = fcn.blocks[slot.setLoc.bbIdx];

            auto setLocNext = slot.setLoc;
            if (slot.setLoc.stmtIdx < setBb.statements.size()) {
                setLocNext.stmtIdx += 1;
            } else {
                setLocNext.bbIdx = setBb.terminator.as_Call().retBlock;
                setLocNext.stmtIdx = 0;
            }

            // If usage is direct assignment of the original value.
            // - In this case, we can move the usage upwards
            if (slot.useLoc.stmtIdx < useBb.statements.size() && TU_TEST2(useBb.statements[slot.useLoc.stmtIdx], Assign, .src, Use, == thisVar)) {
                // Move the usage up to original assignment (if destination isn't invalidated)
                const auto& dst = useBb.statements[slot.useLoc.stmtIdx].as_Assign().dst;

                // TODO: If the destination slot was ever borrowed mutably, don't move.
                // - Maybe, if there's a drop skip? (as the drop could be &mut to the target value)

                // - Iterate the path(s) between the two statements to check if the destination would be invalidated
                //  > The iterate function doesn't (yet) support following BB chains, so assume invalidated if over a jump.
                // TODO: What if the set location is a call?
                bool invalidated = IterPathRes::Complete != iterPath(
                                                                fcn,
                                                                setLocNext,
                                                                slot.useLoc,
                                                                // TODO: What about a mutable borrow?
                                                                [&](auto loc, const auto& stmt) -> bool {
                    return checkInvalidatesLvalue(stmt, dst, false, /*also_read=*/true);
                },
                                                                [&](auto loc, const auto& term) -> bool {
                    return checkInvalidatesLvalue(term, dst, false, /*also_read=*/true);
                }
                                                            );
                if (!invalidated) {
                    // destination not dependent on any statements between the two, move.
                    if (slot.setLoc.stmtIdx < setBb.statements.size()) {
                        auto& setStmt = setBb.statements[slot.setLoc.stmtIdx];
                        TU_MATCH_HDRA( (setStmt), {)
                        TU_ARMA(Assign, se) {
                                MIR_ASSERT(state, se.dst == ::MIR::LValue::newLocal(varIdx), "Impossibility: Value set but isn't destination in " << setStmt);
                                DEBUG("Move destination " << dst << " from " << useBb.statements[slot.useLoc.stmtIdx] << " to " << setStmt);
                                se.dst = dst.clone();
                                useBb.statements[slot.useLoc.stmtIdx] = ::MIR::Statement();
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
                                            if (*ep->output == ::MIR::LValue::newLocal(varIdx)) {
                                                DEBUG("Move destination " << dst << " from " << useBb.statements[slot.useLoc.stmtIdx] << " to " << setStmt);
                                                *ep->output = dst.clone();
                                                useBb.statements[slot.useLoc.stmtIdx] = ::MIR::Statement();
                                                changed = true;
                                                break;
                                            }
                                        }
                                    }
                                }
                                if (!changed) {
                                    MIR_BUG(state, "Failed to find usage of _" << varIdx << " in asm! statement");
                                }
                            }
                            break;
                            default:
                                MIR_BUG(state, "Impossibility: Value set in " << setStmt);
                        }
                    } else {
                        auto& setTerm = setBb.terminator;
                        MIR_ASSERT(state, setTerm.is_Call(), "Impossibility: Value set using non-call");
                        auto& te = setTerm.as_Call();
                        DEBUG("Move destination " << dst << " from " << useBb.statements[slot.useLoc.stmtIdx] << " to " << setTerm);
                        te.retVal = dst.clone();
                        useBb.statements[slot.useLoc.stmtIdx] = ::MIR::Statement();
                        changed = true;
                    }
                } else {
                    DEBUG("Destination invalidated");
                }
                continue;
            }

            // Can't move up, can we move down?
            // - If the source is an Assign(Use) then we can move down
            if (slot.setLoc.stmtIdx < setBb.statements.size() && TU_TEST1(setBb.statements[slot.setLoc.stmtIdx], Assign, .src.is_Use())) {
                auto& setStmt = setBb.statements[slot.setLoc.stmtIdx];
                const auto& src = setStmt.as_Assign().src.as_Use();
                bool srcCopy = src.wrappers.empty() && state.lvalueIsCopy(src);

                // Check if the source of initial assignment is invalidated in the meantime.
                auto useLocInc = slot.useLoc;
                useLocInc.stmtIdx += 1;
                bool invalidated = IterPathRes::Complete != iterPath(
                                                                fcn,
                                                                setLocNext,
                                                                useLocInc,
                                                                // NOTE: If a mutable borrow happens, assume it invalidates the source
                                                                [&](auto loc, const auto& stmt) -> bool {
                    return checkInvalidatesLvalue(stmt, src, srcCopy) || TU_TEST2(stmt, Assign, .src, Borrow, .type != HIR::BorrowType::Shared);
                },
                                                                [&](auto loc, const auto& term) -> bool {
                    return checkInvalidatesLvalue(term, src, srcCopy);
                }
                                                            );
                DEBUG("invalidated = " << invalidated);
                // If this is a deref, and there are move ops between definition and use - then invalidate
                if (!invalidated && std::any_of(src.wrappers.begin(), src.wrappers.end(), [](const MIR::LValue::Wrapper& w) {
                    return w.is_Deref();
                })) {
                    // If there are any move ops between the set and the usage, invalidate
                    bool stop = false;
                    auto checkCb = [&](const MIR::LValue& lv, ValUsage vu) {
                        if (lv == thisVar) {
                            stop = true;
                            return false;
                        }
                        if (stop) {
                            // Once the value is seen, ignore anything else
                            return false;
                        }
                        // If a move is seen, check if it's a move (and not a copy)
                        if (vu == ValUsage::Move) {
                            return !state.lvalueIsCopy(lv);
                        }
                        return false;
                    };
                    invalidated = IterPathRes::Complete != iterPath(fcn, setLocNext, useLocInc, [&](auto loc, const auto& stmt) -> bool {
                        return visitMirLvalues(stmt, checkCb);
                    }, [&](auto loc, const auto& term) -> bool {
                        return (term.is_Call() && !visitMirLvalues(term, [&](const MIR::LValue& lv, ValUsage vu) {
                            return lv == thisVar;
                        })) || visitMirLvalues(term, checkCb);
                    });
                    DEBUG("invalidated = " << invalidated);
                }
                if (!invalidated) {
                    // Update the usage site and replace.
                    auto replaceCb = [&](::MIR::LValue& slot, ValUsage vu) -> bool {
                        if (slot.root == thisVar.root) {
                            if (src.wrappers.empty()) {
                                slot.root = src.root.clone();
                            } else if (slot.wrappers.empty()) {
                                slot = src.clone();
                            } else {
                                MIR_TODO(state, "Replace inner of " << slot << " with " << src);
                            }
                            return true;
                        }
                        return false;
                    };
                    if (slot.useLoc.stmtIdx < useBb.statements.size()) {
                        auto& useStmt = useBb.statements[slot.useLoc.stmtIdx];
                        DEBUG("Replace " << thisVar << " with " << src << " in BB" << slot.useLoc.bbIdx << "/" << slot.useLoc.stmtIdx << " " << useStmt);
                        bool found = visitMirLvaluesMut(useStmt, replaceCb);
                        if (!found) {
                            DEBUG("Can't find use of " << thisVar << " in " << useStmt);
                        } else {
                            setStmt = ::MIR::Statement();
                            changed = true;
                        }
                    } else {
                        auto& useTerm = useBb.terminator;
                        DEBUG("Replace " << thisVar << " with " << src << " in " << useTerm);
                        bool found = visitMirLvaluesMut(useTerm, replaceCb);
                        if (!found) {
                            DEBUG("Can't find use of " << thisVar << " in " << useTerm);
                        } else {
                            setStmt = ::MIR::Statement();
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
            if (slot.setLoc.stmtIdx < setBb.statements.size()) {
                DEBUG("Set: " << setBb.statements[slot.setLoc.stmtIdx]);
            } else {
                DEBUG("Set: " << setBb.terminator);
            }
            if (slot.useLoc.stmtIdx < useBb.statements.size()) {
                DEBUG("Use: " << useBb.statements[slot.useLoc.stmtIdx]);
            } else {
                DEBUG("Use: " << useBb.terminator);
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
bool MIROptimiseDeTemporaryBorrows(::MIR::TypeResolve& state, ::MIR::Function& fcn) {
    bool changed = false;
    TRACE_FUNCTION_FR("", changed);

    // Find all single-assign borrows that are only ever used via Deref
    // - Direct drop is ignored for this purpose
    struct LocalUsage {
        unsigned nWrite;
        unsigned nOtherRead;
        unsigned nDerefRead;
        OptimiseStmtRef setLoc;
        ::std::vector<OptimiseStmtRef> dropLocs;

        LocalUsage()
            : nWrite(0)
            , nOtherRead(0)
            , nDerefRead(0)
        {
        }
    };

    auto usageInfo = ::std::vector<LocalUsage>(fcn.locals.size());
    for (const auto& bb : fcn.blocks) {
        OptimiseStmtRef curLoc;
        auto visitCb = [&](const ::MIR::LValue& lv, auto vu) {
            if (lv.root.is_Local()) {
                auto& slot = usageInfo[lv.root.as_Local()];
                // NOTE: This pass doesn't care about indexing, as we're looking for values that are borrows (which aren't valid indexes)
                // > Inner-most wrapper is Deref - it's a deref of this variable
                if (!lv.wrappers.empty() && lv.wrappers.front().is_Deref()) {
                    slot.nDerefRead++;
                    if (fcn.locals[lv.root.as_Local()]->is_Borrow()) {
                        DEBUG(lv << " deref use " << curLoc);
                    }
                }
                // > Write with no wrappers - Assignment
                else if (lv.wrappers.empty() && vu == ValUsage::Write) {
                    slot.nWrite++;
                    slot.setLoc = curLoc;
                    //DEBUG(lv << " set");
                }
                // Anything else, count as a read
                else {
                    slot.nOtherRead++;
                }
            }
            return false;
        };
        for (const auto& stmt : bb.statements) {
            curLoc = OptimiseStmtRef(&bb - &fcn.blocks.front(), &stmt - &bb.statements.front());

            //DEBUG(cur_loc << ":" << stmt);
            visitMirLvalues(stmt, visitCb);
        }
        curLoc = OptimiseStmtRef(&bb - &fcn.blocks.front(), bb.statements.size());
        if (const auto* drop = bb.terminator.opt_Drop(); drop && drop->slot.root.is_Local() && drop->slot.wrappers.empty()) {
            usageInfo[drop->slot.root.as_Local()].dropLocs.push_back(curLoc);
        } else {
            visitMirLvalues(bb.terminator, visitCb);
        }
    }

    // Look single-write/deref-only locals assigned with `_0 = Borrow`
    for (size_t varIdx = 0; varIdx < fcn.locals.size(); varIdx++) {
        const auto& slot = usageInfo[varIdx];
        auto thisVar = ::MIR::LValue::newLocal(varIdx);

        // This rule only applies to single-write variables, with no use other than via derefs
        if (slot.nWrite != 1) {
            //DEBUG(this_var << " - Multi-assign, or use-by-value");
            continue;
        }
        if (slot.nDerefRead == 0) {
            //DEBUG(this_var << " - Not used");
            continue;
        }

        // Check that the source was a borrow statement
        auto& srcBb = fcn.blocks[slot.setLoc.bbIdx];
        if (!(slot.setLoc.stmtIdx < srcBb.statements.size() && TU_TEST1(srcBb.statements[slot.setLoc.stmtIdx], Assign, .src.is_Borrow()))) {
            DEBUG(thisVar << " - Source is not a borrow op");
            continue;
        }
        const auto& srcBorrow = srcBb.statements[slot.setLoc.stmtIdx].as_Assign().src.as_Borrow();
        const auto& srcLv = srcBorrow.val;
        // Check that the borrow isn't too complex (if it's used multiple times)
        if (slot.nDerefRead > 1 && srcLv.wrappers.size() >= 2) {
            DEBUG(thisVar << " - Source is too complex - " << srcLv);
            continue;
        }
        // If there are multiple derefs, don't expand. More than one deref makes determining invalidation VERY hard
        if (std::count_if(srcLv.wrappers.begin(), srcLv.wrappers.end(), [](const MIR::LValue::Wrapper& w) {
            return w.is_Deref();
        }) > 1) {
            DEBUG(thisVar << " - Source is too complex (deref) - " << srcLv);
            continue;
        }
        // Keep the complexity down (when not used only once)
        if (slot.nDerefRead + slot.nOtherRead > 1 && srcBorrow.type != ::HIR::BorrowType::Shared) {
            DEBUG(thisVar << " - Multi-use non-shared borrow, too complex to do");
            continue;
        }
        DEBUG(thisVar << " - Borrow of " << srcLv << " at " << slot.setLoc << ", used " << slot.nDerefRead << " times (dropped {" << slot.dropLocs << "})");
        bool srcCopy = state.lvalueIsCopy(srcLv);

        // Locate usage sites (by walking forwards) and check for invalidation
        auto curLoc = slot.setLoc;
        curLoc.stmtIdx++;
        unsigned numReplaced = 0;
        auto replaceCb = [&](::MIR::LValue& lv, auto _vu) {
            if (lv.root == thisVar.root && !lv.wrappers.empty()) {
                ASSERT_BUG(Span(), !lv.wrappers.empty(), curLoc << " " << lv);
                MIR_ASSERT(state, lv.wrappers.front().is_Deref(), "Use of a replacable value that isn't via a deref - " << lv);
                // Make a LValue reference, then overwrite it
                {
                    auto lvr = ::MIR::LValue::MRef(lv);
                    while (lvr.wrapper_count() > 1) {
                        lvr.tryUnwrap();
                    }
                    DEBUG(thisVar << " " << curLoc << " - Replace " << lvr << " with " << srcLv << " in " << lv);
                    lvr.replace(srcLv.clone());
                }
                DEBUG("= " << lv);
                assert(lv.root != thisVar.root);
                assert(numReplaced < slot.nDerefRead);
                numReplaced += 1;
            }
            return false;
        };
        for (bool stop = false; !stop;) {
            auto& curBb = fcn.blocks[curLoc.bbIdx];
            for (; curLoc.stmtIdx < curBb.statements.size(); curLoc.stmtIdx++) {
                auto& stmt = curBb.statements[curLoc.stmtIdx];
                DEBUG(curLoc << " " << stmt);
                // Check for invalidation (actual check done before replacement)
                bool invalidates = checkInvalidatesLvalue(stmt, srcLv, srcCopy);
                if (invalidates) {
                    // Invalidated, stop here.
                    DEBUG(thisVar << " - Source invalidated @ " << curLoc << " in " << stmt);
                    stop = true;
                    break;
                }
                // Replace usage
                visitMirLvaluesMut(stmt, replaceCb);
                if (numReplaced == slot.nDerefRead) {
                    stop = true;
                    break;
                }
            }
            if (stop) {
                break;
            }
            // Replace usage
            visitMirLvaluesMut(curBb.terminator, replaceCb);
            if (numReplaced == slot.nDerefRead) {
                stop = true;
                break;
            }
            // Check for invalidation
            if (checkInvalidatesLvalue(curBb.terminator, srcLv, srcCopy)) {
                DEBUG(thisVar << " - Source invalidated @ " << curLoc << " in " << curBb.terminator);
                stop = true;
                break;
            }

            TU_MATCH_HDRA( (curBb.terminator), { )
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
        if (srcLv.root.is_Local() && !srcLv.wrappers.empty() && srcLv.wrappers.front().is_Deref()) {
            usageInfo[srcLv.root.as_Local()].nDerefRead += numReplaced;
            if (numReplaced == slot.nDerefRead) {
                usageInfo[srcLv.root.as_Local()].nDerefRead -= 1;
            }
        }

        // If all usage sites were updated, then remove the original assignment
        // - Since this code works with `&mut`, can't just leave the assignment for DCE when mut
        if (numReplaced == slot.nDerefRead + slot.nOtherRead) {
            DEBUG(thisVar << " - Erase " << slot.setLoc << " as it is no longer used (" << srcBb.statements[slot.setLoc.stmtIdx] << ")");
            srcBb.statements[slot.setLoc.stmtIdx] = ::MIR::Statement();
            for (const auto& dropLoc : slot.dropLocs) {
                DEBUG(thisVar << " - Drop at " << dropLoc);
                auto& dropBb = fcn.blocks[dropLoc.bbIdx];
                MIR_ASSERT(state, dropLoc.stmtIdx == dropBb.statements.size() && dropBb.terminator.is_Drop(), "Recorded drop is no longer a terminator");
                auto target = dropBb.terminator.as_Drop().target;
                dropBb.terminator = ::MIR::Terminator::make_Goto(target);
            }
        } else {
            // The variable is still used, keep the source where it is
            DEBUG(thisVar << " - Keep " << slot.setLoc);
        }

        // Any replacements? Then there was an actionable change
        if (numReplaced > 0) {
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
bool MIROptimiseDeTemporaryReborrowOfUnused(::MIR::TypeResolve& state, ::MIR::Function& fcn) {
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
            state.setCurStmt(&blk - fcn.blocks.data(), &stmt - blk.statements.data());

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
            if (!(re.val.root.is_Local() || re.val.root.is_Argument())) {
                continue;
            }
            if (!(re.val.wrappers.size() == 1 && re.val.wrappers[0].is_Deref())) {
                continue;
            }
            // Types must match (avoids decaying reborrows or raw pointer accesses)
            const auto& srcTy = re.val.root.is_Local() ? fcn.locals[re.val.root.as_Local()] : state.mArgs[re.val.root.as_Argument()].second;
            const auto& dstTy = fcn.locals[se.dst.as_Local()];
            if (srcTy != dstTy) {
                continue;
            }

            // Record as a possible useless reborrow
            // - Depends on the usage of the source
            auto pos = OptimiseStmtRef(state.getCurBlock(), state.getCurStmtOfs());
            DEBUG(state << "Possible " << se.dst << " = " << re.val);
            possible.push_back(Poss(pos, re.val.root.clone(), se.dst.root.clone()));
        }
    }
    if (possible.size() == 0) {
        return false;
    }
    // The borrow must not be within a loop
    {
        std::vector<unsigned int> incomingEdges(fcn.blocks.size());
        for (const auto& block : fcn.blocks) {
            visitTerminatorTarget(block.terminator, [&](const auto& target) {
                incomingEdges[target]++;
            });
        }
        std::vector<unsigned int> acyclicBlocks;
        acyclicBlocks.reserve(fcn.blocks.size());
        for (unsigned int i = 0; i < incomingEdges.size(); i++) {
            if (incomingEdges[i] == 0) {
                acyclicBlocks.push_back(i);
            }
        }
        for (size_t i = 0; i < acyclicBlocks.size(); i++) {
            visitTerminatorTarget(fcn.blocks[acyclicBlocks[i]].terminator, [&](const auto& target) {
                if (--incomingEdges[target] == 0) {
                    acyclicBlocks.push_back(target);
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

            bool doesBlockLoop(unsigned rootIdx) {
                stack.clear();
                visited.clear();
                visited.resize(fcn.blocks.size());
                visited[rootIdx] = true;
                stack.push_back(rootIdx);
                while (!stack.empty()) {
                    auto bbIdx = stack.back();
                    stack.pop_back();
                    auto& bb = fcn.blocks[bbIdx];
                    bool isLoop = false;
                    visitTerminatorTarget(bb.terminator, [&](const ::MIR::BasicBlockId& idx) {
                        if (idx == rootIdx) {
                            isLoop = true;
                        }
                        if (!visited[idx]) {
                            visited[idx] = true;
                            stack.push_back(idx);
                        }
                    });
                    if (isLoop) {
                        return true;
                    }
                }
                return false;
            }
        } vs{fcn};

        if (acyclicBlocks.size() != fcn.blocks.size()) {
            std::vector<bool> visited(fcn.blocks.size());
            std::vector<bool> loops(fcn.blocks.size());
            for (auto& poss : possible) {
                if (!visited[poss.pos.bbIdx]) {
                    visited[poss.pos.bbIdx] = true;
                    loops[poss.pos.bbIdx] = vs.doesBlockLoop(poss.pos.bbIdx);
                }
                poss.used |= loops[poss.pos.bbIdx];
            }
        }
    }

    // Must be the only use (apart from dropping) of the source lvalue
    ::std::unordered_map<uintptr_t, ::std::vector<size_t>> possibleBySource;
    for (size_t i = 0; i < possible.size(); i++) {
        possibleBySource[possible[i].slot.getInner()].push_back(i);
    }
    for (const auto& blk : fcn.blocks) {
        for (const auto& stmt : blk.statements) {
            state.setCurStmt(&blk - fcn.blocks.data(), &stmt - blk.statements.data());
            auto pos = OptimiseStmtRef(state.getCurBlock(), state.getCurStmtOfs());
            visitMirLvalues(stmt, [&](const ::MIR::LValue& lv, ValUsage /*vu*/) {
                if (!(lv.root.is_Local() || lv.root.is_Argument())) {
                    return false;
                }
                auto it = possibleBySource.find(lv.root.getInner());
                if (it == possibleBySource.end()) {
                    return false;
                }
                for (auto possibleIdx : it->second) {
                    auto& p = possible[possibleIdx];
                    if (!(pos == p.pos)) {
                        DEBUG(state << p.slot << " Used - " << stmt);
                        p.used = true;
                    }
                }
                return false;
            });
        }
        const auto* dropped = blk.terminator.opt_Drop();
        visitMirLvalues(blk.terminator, [&](const ::MIR::LValue& lv, ValUsage /*vu*/) {
            if (!(lv.root.is_Local() || lv.root.is_Argument())) {
                return false;
            }
            auto it = possibleBySource.find(lv.root.getInner());
            if (it != possibleBySource.end()) {
                if (dropped && dropped->slot.wrappers.empty() && dropped->slot.root.getInner() == lv.root.getInner()) {
                    return false;
                }
                for (auto possibleIdx : it->second) {
                    auto& p = possible[possibleIdx];
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
    ::std::unordered_set<uintptr_t> sourceSlots;
    ::std::unordered_map<uintptr_t, uintptr_t> replacements;
    for (auto it = possible.rbegin(); it != possible.rend(); ++it) {
        const auto source = it->slot.getInner();
        const auto destination = it->replace.getInner();
        sourceSlots.insert(source);
        auto next = replacements.find(source);
        replacements[destination] = next == replacements.end() ? source : next->second;
        fcn.blocks[it->pos.bbIdx].statements[it->pos.stmtIdx] = ::MIR::Statement();
    }
    for (auto& blk : fcn.blocks) {
        for (auto& stmt : blk.statements) {
            state.setCurStmt(&blk - fcn.blocks.data(), &stmt - blk.statements.data());
            visitMirLvaluesMut(stmt, [&](::MIR::LValue& lv, ValUsage /*vu*/) {
                if (lv.root.is_Local()) {
                    auto it = replacements.find(lv.root.getInner());
                    if (it != replacements.end()) {
                        DEBUG(state << lv.root << " Replace");
                        lv.root = ::MIR::LValue::Storage::fromInner(it->second);
                    }
                }
                return false;
            });
        }

        if (auto* drop = blk.terminator.opt_Drop(); drop && drop->slot.wrappers.empty() && (drop->slot.root.is_Local() || drop->slot.root.is_Argument()) && sourceSlots.count(drop->slot.root.getInner()) != 0) {
            DEBUG(state << drop->slot.root << " Erase drop");
            auto target = drop->target;
            blk.terminator = ::MIR::Terminator::make_Goto(target);
        }
        visitMirLvaluesMut(blk.terminator, [&](::MIR::LValue& lv, ValUsage /*vu*/) {
            if (lv.root.is_Local()) {
                auto it = replacements.find(lv.root.getInner());
                if (it != replacements.end()) {
                    DEBUG(state << lv.root << " Replace");
                    lv.root = ::MIR::LValue::Storage::fromInner(it->second);
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
bool MIROptimiseDeTemporary(::MIR::TypeResolve& state, ::MIR::Function& fcn) {
    bool changed = false;
    TRACE_FUNCTION_FR("", changed);

    changed |= MIROptimiseDeTemporarySingleSetAndUse(state, fcn);
    if (changed) {
        return changed;
    }
    changed |= MIROptimiseDeTemporaryBorrows(state, fcn);
    if (changed) {
        return changed;
    }
    changed |= MIROptimiseDeTemporaryReborrowOfUnused(state, fcn);

    // OLD ALGORITHM.
    for (unsigned int bbIdx = 0; bbIdx < fcn.blocks.size(); bbIdx++) {
        auto& bb = fcn.blocks[bbIdx];
        ::std::map<unsigned, unsigned> localAssignments; // Local number -> statement index
        // TODO: Keep track of what variables would invalidate a local (and compound on assignment)
        ::std::vector<unsigned> statementsToRemove; // List of statements that have to be removed

        // ----- Helper closures -----
        // > Check if a recorded assignment is no longer valid.
        auto cbCheckInvalidate = [&](const ::MIR::LValue& lv, ValUsage vu) {
            for (auto it = localAssignments.begin(); it != localAssignments.end();) {
                bool invalidated = false;
                const auto& srcRvalue = bb.statements[it->second].as_Assign().src;

                // Destination invalidated?
                if (lv.root.is_Local() && it->first == lv.root.as_Local()) {
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
                            visitMirLvalues(srcRvalue, [&](const ::MIR::LValue& sLv, auto sVu) {
                                //DEBUG("   " << s_lv << " ?= " << lv);
                                if (sLv.root == lv.root) {
                                    DEBUG(state << "> Invalidates source of Local(" << it->first << ") - " << srcRvalue);
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
                    it = localAssignments.erase(it);
                } else {
                    ++it;
                }
            }
            return false;
        };
        // ^^^ Check for invalidations
        auto cbApplyReplacements = [&](auto& topLv, auto topUsage) {
            // NOTE: Visits only the top-level LValues
            // - The inner `visit_mir_lvalue_mut` handles sub-values

            // TODO: Handle partial moves (only delete assignment if the value is fully used)
            // > For now, don't do the replacement if it would delete the assignment UNLESS it's directly being used)

            // 2. Search for replacements
            if (topLv.root.is_Local()) {
                bool topLevel = topLv.wrappers.empty();
                auto ilv = ::MIR::LValue::newLocal(topLv.root.as_Local());
                auto it = localAssignments.find(topLv.root.as_Local());
                if (it != localAssignments.end()) {
                    const auto& newVal = bb.statements[it->second].as_Assign().src.as_Use();
                    // - Copy? All is good.
                    if (state.lvalueIsCopy(ilv)) {
                        topLv = newVal.cloneWrapped(topLv.wrappers.begin(), topLv.wrappers.end());
                        DEBUG(state << "> Replace (and keep) Local(" << it->first << ") with " << newVal);
                        changed = true;
                    }
                    // - Top-level (directly used) also good.
                    else if (topLevel && topUsage == ValUsage::Move) {
                        // TODO: DstMeta/DstPtr _doesn't_ move, so shouldn't trigger this.
                        topLv = newVal.clone();
                        DEBUG(state << "> Replace (and remove) Local(" << it->first << ") with " << newVal);
                        statementsToRemove.push_back(it->second);
                        localAssignments.erase(it);
                        changed = true;
                    }
                    // - Otherwise, remove the record.
                    else {
                        DEBUG(state << "> Non-copy value used within a LValue, remove record of Local(" << it->first << ")");
                        localAssignments.erase(it);
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
        for (unsigned int stmtIdx = 0; stmtIdx < bb.statements.size(); stmtIdx++) {
            auto& stmt = bb.statements[stmtIdx];
            state.setCurStmt(bbIdx, stmtIdx);
            DEBUG(state << stmt);

            // - Check if this statement mutates or borrows a recorded local
            //  > (meaning that the slot isn't a temporary)
            // - Check if this statement mutates or moves the source
            //  > (thus making it invalid to move the source forwards)
            visitMirLvalues(stmt, cbCheckInvalidate);

            // - Apply known relacements
            visitMirLvaluesMut(stmt, cbApplyReplacements);

            // - Check if this is a new assignment
            if (stmt.is_Assign() && stmt.as_Assign().dst.is_Local() && stmt.as_Assign().src.is_Use()) {
                const auto& dstLv = stmt.as_Assign().dst;
                const auto& srcLv = stmt.as_Assign().src.as_Use();
                if (visitMirLvaluesInner(srcLv, ValUsage::Read, [&](const auto& lv, auto) {
                    return lv.root == dstLv.root;
                })) {
                    DEBUG(state << "> Don't record, self-referrential");
                } else if (::std::any_of(srcLv.wrappers.begin(), srcLv.wrappers.end(), [](const auto& w) {
                    return w.is_Deref();
                })) {
                    DEBUG(state << "> Don't record, dereference");
                } else {
                    localAssignments.insert(::std::make_pair(stmt.as_Assign().dst.as_Local(), stmtIdx));
                    DEBUG(state << "> Record assignment");
                }
            }
        } // for(stmt in bb.statements)

        // TERMINATOR
        state.setCurStmtTerm(bbIdx);
        DEBUG(state << bb.terminator);
        // > Check for invalidations (e.g. move of a source value)
        visitMirLvalues(bb.terminator, cbCheckInvalidate);
        // > THEN check for replacements
        if (!bb.terminator.is_Switch()) {
            visitMirLvaluesMut(bb.terminator, cbApplyReplacements);
        }

        // Remove assignments
        ::std::sort(statementsToRemove.begin(), statementsToRemove.end());
        while (!statementsToRemove.empty()) {
            // TODO: Handle partial moves here?
            // TODO: Is there some edge case I'm missing where the assignment shouldn't be removed?
            // > It isn't removed if it's used as a Copy, so that's not a problem.
            bb.statements.erase(bb.statements.begin() + statementsToRemove.back());
            statementsToRemove.pop_back();

            changed = true;
        }
    }

    return changed;
}

// --------------------------------------------------------------------
// Detect common statements between all source arms of a block
// --------------------------------------------------------------------
bool MIROptimiseCommonStatements(::MIR::TypeResolve& state, ::MIR::Function& fcn) {
    bool changed = false;
    TRACE_FUNCTION_FR("", changed);

    for (size_t bbIdx = 0; bbIdx < fcn.blocks.size(); bbIdx++) {
        state.setCurStmt(bbIdx, 0);

        bool skip = false;
        ::std::vector<size_t> sources;
        // Find source blocks
        for (size_t bb2Idx = 0; bb2Idx < fcn.blocks.size() && !skip; bb2Idx++) {
            const auto& blk = fcn.blocks[bb2Idx];
            // TODO: Handle non-Goto branches? (e.g. calls)
            if (blk.terminator.is_Goto() && blk.terminator.as_Goto() == bbIdx) {
                if (blk.statements.empty()) {
                    DEBUG(state << " BB" << bb2Idx << " empty");
                    skip = true;
                    break;
                }
                if (!sources.empty()) {
                    if (blk.statements.back() != fcn.blocks[sources.front()].statements.back()) {
                        DEBUG(state << " BB" << bb2Idx << " doesn't end with " << fcn.blocks[sources.front()].statements.back() << " instead " << blk.statements.back());
                        skip = true;
                        break;
                    }
                }
                sources.push_back(bb2Idx);
            } else {
                visitTerminatorTarget(blk.terminator, [&](const auto& dstIdx) {
                    // If this terminator points to the current BB, don't attempt to merge
                    if (dstIdx == bbIdx) {
                        DEBUG(state << " BB" << bb2Idx << " doesn't end Goto - instead " << blk.terminator);
                        skip = true;
                    }
                });
            }
        }

        if (!skip && sources.size() > 1) {
            // TODO: Should this search for any common statements?

            // Found a common assignment, add to the start and remove from sources.
            auto stmt = ::std::move(fcn.blocks[sources.front()].statements.back());
            MIR_DEBUG(state, "Move common final statements from " << sources << " to " << bbIdx << " - " << stmt);
            for (auto idx : sources) {
                fcn.blocks[idx].statements.pop_back();
            }
            fcn.blocks[bbIdx].statements.insert(fcn.blocks[bbIdx].statements.begin(), ::std::move(stmt));
        }
    }
    return changed;
}

// --------------------------------------------------------------------
// If two temporaries don't overlap in lifetime (blocks in which they're valid), unify the two
// --------------------------------------------------------------------
bool MIROptimiseUnifyTemporaries(::MIR::TypeResolve& state, ::MIR::Function& fcn) {
    bool replacementNeeded = false;
    TRACE_FUNCTION_FR("", replacementNeeded);
    ::std::vector<bool> replacable(fcn.locals.size());
    // 1. Enumerate which (if any) temporaries share the same type
    {
        unsigned int nFound = 0;
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
                    nFound++;
                }
            }
        }
        if (nFound == 0) {
            return false;
        }
    }

    // TODO: Only calculate lifetimes for replacable locals
    auto lifetimes = MIRHelperGetLifetimes(state, fcn, /*dump_debug=*/true, /*mask=*/&replacable);
    ::std::vector<::MIR::ValueLifetime> slotLifetimes = mv$(lifetimes.slots);

    // 2. Unify variables of the same type with distinct non-overlapping lifetimes
    ::std::map<unsigned int, unsigned int> replacements;
    ::std::vector<bool> visited(fcn.locals.size());
    for (unsigned int localIdx = 0; localIdx < fcn.locals.size(); localIdx++) {
        if (!replacable[localIdx]) {
            continue;
        }
        if (visited[localIdx]) {
            continue;
        }
        if (!slotLifetimes[localIdx].isUsed()) {
            continue;
        }
        visited[localIdx] = true;

        for (unsigned int i = localIdx + 1; i < fcn.locals.size(); i++) {
            if (!replacable[i]) {
                continue;
            }
            if (fcn.locals[i] != fcn.locals[localIdx]) {
                continue;
            }
            if (!slotLifetimes[i].isUsed()) {
                continue;
            }
            // Variables are of the same type, check if they overlap
            if (slotLifetimes[localIdx].overlaps(slotLifetimes[i])) {
                continue;
            }
            // They don't overlap, unify
            slotLifetimes[localIdx].unify(slotLifetimes[i]);
            replacements[i] = localIdx;
            replacementNeeded = true;
            visited[i] = true;
        }
    }

    if (replacementNeeded) {
        DEBUG("Replacing temporaries using {" << replacements << "}");
        visitMirLvaluesMut(state, fcn, [&](auto& lv, auto) {
            if (lv.root.is_Local()) {
                auto it = replacements.find(lv.root.as_Local());
                if (it != replacements.end()) {
                    MIR_DEBUG(state, lv << " => Local(" << it->second << ")");
                    lv.root = ::MIR::LValue::Storage::newLocal(it->second);
                    return true;
                }
            }
            return false;
        });

        // TODO: Replace in ScopeEnd too?
    }

    return replacementNeeded;
}

// --------------------------------------------------------------------
// Combine identical blocks
// --------------------------------------------------------------------
bool MIROptimiseUnifyBlocks(::MIR::TypeResolve& state, ::MIR::Function& fcn) {
    bool changed = false;
    TRACE_FUNCTION_FR("", changed);

    struct H {
        static size_t blockHash(const ::MIR::BasicBlock& block) {
            size_t rv = block.statements.size();
            auto add = [&](size_t v) {
                rv ^= v + 0x9e3779b9 + (rv << 6) + (rv >> 2);
            };
            for (const auto& statement : block.statements) {
                add(statement.tag());
            }
            add(block.terminator.tag());
            visitTerminatorTarget(block.terminator, [&](const auto& target) {
                add(target);
            });
            return rv;
        }

        static bool blocksEqual(const ::MIR::BasicBlock& a, const ::MIR::BasicBlock& b) {
            if (a.isCleanup != b.isCleanup) {
                return false;
            }
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
                        if (ae.newVal != be.newVal) {
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
                        if (ae.bitIndex != be.bitIndex) {
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
                        if (ae.bitIndex != be.bitIndex) {
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
            return a.terminator == b.terminator;
        }
    };

    // Locate duplicate blocks and replace
    ::std::map<unsigned int, unsigned int> replacements;
    ::std::unordered_map<size_t, ::std::vector<unsigned int>> candidates;
    for (unsigned int bbIdx = 0; bbIdx < fcn.blocks.size(); bbIdx++) {
        if (fcn.blocks[bbIdx].terminator.tag() == ::MIR::Terminator::TAGDEAD) {
            continue;
        }
        if (fcn.blocks[bbIdx].terminator.is_Incomplete() && fcn.blocks[bbIdx].statements.size() == 0) {
            continue;
        }
        auto& bucket = candidates[H::blockHash(fcn.blocks[bbIdx])];
        bool found = false;
        for (auto candidate : bucket) {
            if (H::blocksEqual(fcn.blocks[candidate], fcn.blocks[bbIdx])) {
                replacements[bbIdx] = candidate;
                found = true;
                break;
            }
        }
        if (!found) {
            bucket.push_back(bbIdx);
        }
    }

    if (!replacements.empty()) {
        //MIR_TODO(state, "Unify blocks - " << replacements);
        DEBUG("Unify blocks (old: new) - " << replacements);
        auto patchTgt = [&replacements](::MIR::BasicBlockId& tgt) {
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
            visitTerminatorTargetMut(bb.terminator, [&](auto& te) {
                patchTgt(te);
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
bool MIROptimisePropagateKnownValues(::MIR::TypeResolve& state, ::MIR::Function& fcn) {
    bool changeHappend = false;
    TRACE_FUNCTION_FR("", changeHappend);
    // 1. Determine reference counts for blocks (allows reversing up BB tree)
    ::std::vector<size_t> blockOrigins(fcn.blocks.size(), SIZE_MAX);
    {
        ::std::vector<unsigned int> blockUses(fcn.blocks.size());
        ::std::vector<bool> visited(fcn.blocks.size());
        ::std::vector<::MIR::BasicBlockId> toVisit;
        toVisit.push_back(0);
        blockUses[0]++;
        while (toVisit.size() > 0) {
            auto bb = toVisit.back();
            toVisit.pop_back();
            if (visited[bb]) {
                continue;
            }
            visited[bb] = true;
            const auto& block = fcn.blocks[bb];

            visitTerminatorTarget(block.terminator, [&](const auto& idx) {
                if (!visited[idx]) {
                    toVisit.push_back(idx);
                }
                if (blockUses[idx] == 0) {
                    blockOrigins[idx] = bb;
                } else {
                    blockOrigins[idx] = SIZE_MAX;
                }
                blockUses[idx]++;
            });
        }
    }

    // 2. Find any assignments (or function uses?) of the form FIELD(LOCAL, _)
    //  > Restricted to simplify logic (and because that's the inefficient pattern observed)
    // 3. Search backwards from that point until the referenced local is assigned
    auto getField = [&](const ::MIR::LValue& slotLvalue, unsigned field, size_t startBbIdx, size_t startStmtIdx) -> const ::MIR::LValue* {
        TRACE_FUNCTION_F(slotLvalue << "." << field << " BB" << startBbIdx << "/" << startStmtIdx);
        bool slotCopy = state.lvalueIsCopy(slotLvalue);
        // NOTE: An infinite loop is (theoretically) impossible.
        auto bbIdx = startBbIdx;
        auto stmtIdx = startStmtIdx;
        for (;;) {
            const auto& bb = fcn.blocks[bbIdx];
            while (stmtIdx--) {
                if (stmtIdx == bb.statements.size()) {
                    DEBUG("BB" << bbIdx << "/TERM - " << bb.terminator);
                    if (checkInvalidatesLvalue(bb.terminator, slotLvalue, slotCopy)) {
                        return nullptr;
                    }
                    continue;
                }
                const auto& stmt = bb.statements[stmtIdx];
                DEBUG("BB" << bbIdx << "/" << stmtIdx << " - " << stmt);
                if (const auto* se = stmt.opt_Assign()) {
                    if (se->dst == slotLvalue) {
                        if (!se->src.is_Tuple()) {
                            return nullptr;
                        }
                        const auto& srcParam = se->src.as_Tuple().vals.at(field);
                        DEBUG("> Found a source " << srcParam);
                        // TODO: Support returning a Param
                        if (!srcParam.is_LValue()) {
                            return nullptr;
                        }
                        const auto& srcLval = srcParam.as_LValue();
                        bool srcCopy = state.lvalueIsCopy(srcLval);
                        // Visit all statements between the start and here, checking for mutation of this value.
                        auto endBbIdx = bbIdx;
                        auto endStmtIdx = stmtIdx;
                        bbIdx = startBbIdx;
                        stmtIdx = startStmtIdx;
                        for (;;) {
                            const auto& bb = fcn.blocks[bbIdx];
                            while (stmtIdx--) {
                                if (bbIdx == endBbIdx && stmtIdx == endStmtIdx) {
                                    return &srcLval;
                                }
                                if (stmtIdx == bb.statements.size()) {
                                    DEBUG("BB" << bbIdx << "/TERM - " << bb.terminator);
                                    if (checkInvalidatesLvalue(bb.terminator, srcLval, srcCopy)) {
                                        // Invalidated: Return.
                                        return nullptr;
                                    }
                                    continue;
                                }
                                if (checkInvalidatesLvalue(bb.statements[stmtIdx], srcLval, srcCopy)) {
                                    // Invalidated: Return.
                                    return nullptr;
                                }
                            }
                            assert(blockOrigins[bbIdx] != SIZE_MAX);
                            bbIdx = blockOrigins[bbIdx];
                            stmtIdx = fcn.blocks[bbIdx].statements.size() + 1;
                        }
                        throw "";
                    }
                }

                // Check if the slot is invalidated (mutated)
                if (checkInvalidatesLvalue(stmt, slotLvalue, slotCopy)) {
                    return nullptr;
                }
            }
            if (blockOrigins[bbIdx] == SIZE_MAX) {
                break;
            }
            bbIdx = blockOrigins[bbIdx];
            stmtIdx = fcn.blocks[bbIdx].statements.size() + 1;
        }
        return nullptr;
    };
    for (auto& block : fcn.blocks) {
        size_t bbIdx = &block - &fcn.blocks.front();
        for (size_t i = 0; i < block.statements.size(); i++) {
            state.setCurStmt(bbIdx, i);
            DEBUG(state << block.statements[i]);
            visitMirLvaluesMut(block.statements[i], [&](::MIR::LValue& lv, auto vu) {
                if (vu == ValUsage::Read && lv.wrappers.size() > 1 && lv.wrappers.front().is_Field() && lv.root.is_Local()) {
                    auto fieldIndex = lv.wrappers.front().as_Field();
                    auto innerLv = ::MIR::LValue::newLocal(lv.root.as_Local());
                    auto outerLv = ::MIR::LValue::newField(innerLv.clone(), fieldIndex);
                    // TODO: This value _must_ be Copy for this optimisation to work.
                    // - OR, it has to somehow invalidate the original tuple
                    DEBUG(state << "Locating origin of " << lv);
                    ::HIR::TypeRef tmp;
                    if (!state.mResolve.typeIsCopy(state.sp, state.getLvalueType(tmp, innerLv))) {
                        DEBUG(state << "- not Copy, can't optimise");
                        return false;
                    }
                    const auto* sourceLvalue = getField(innerLv, fieldIndex, bbIdx, i);
                    if (sourceLvalue) {
                        if (outerLv != *sourceLvalue) {
                            DEBUG(state << "Source is " << *sourceLvalue);
                            lv = sourceLvalue->cloneWrapped(lv.wrappers.begin() + 1, lv.wrappers.end());
                            changeHappend = true;
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
    return changeHappend;
}

// --------------------------------------------------------------------
// Propagate constants and eliminate known paths
// --------------------------------------------------------------------
bool MIROptimiseConstPropagate(::MIR::TypeResolve& state, ::MIR::Function& fcn) {
#if DUMP_BEFORE_ALL || DUMP_BEFORE_CONSTPROPAGATE
    if (debugEnabled()) {
        MIRDumpFcn(::std::cout, fcn);
    }
#endif
    bool changed = false;
    TRACE_FUNCTION_FR("", changed);
    auto makeFloatArithmeticResult = [](FloatValue value, ::HIR::CoreType type) {
        if (floatValueIsNan(value)) {
            value = positiveNanFloatValue();
        }
        return ::MIR::Constant::make_Float({value, type});
    };

    // - Remove calls to `size_of` and `align_of` (replace with value if known)
    for (auto& bb : fcn.blocks) {
        state.setCurStmtTerm(bb);
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
            size_t sizeVal = 0;
            if (TargetGetSizeOf(state.sp, state.mResolve, tef.params.types.at(0), sizeVal)) {
                DEBUG("size_of = " << sizeVal);
                auto val = ::MIR::Constant::make_Uint({U128(sizeVal), ::HIR::CoreType::Usize});
                bb.statements.push_back(::MIR::Statement::make_Assign({mv$(te.retVal), mv$(val)}));
                bb.terminator = ::MIR::Terminator::make_Goto(te.retBlock);
                changed = true;
            }
        } else if (tef.name == "size_of_val") {
            size_t sizeVal = 0, tmp;
            if (TargetGetSizeAndAlignOf(state.sp, state.mResolve, tef.params.types.at(0), sizeVal, tmp) && sizeVal != SIZE_MAX) {
                DEBUG("size_of_val = " << sizeVal);
                auto val = ::MIR::Constant::make_Uint({U128(sizeVal), ::HIR::CoreType::Usize});
                bb.statements.push_back(::MIR::Statement::make_Assign({mv$(te.retVal), mv$(val)}));
                bb.terminator = ::MIR::Terminator::make_Goto(te.retBlock);
                changed = true;
            }
        } else if (tef.name == "align_of" || tef.name == "min_align_of") {
            size_t alignVal = 0;
            if (TargetGetAlignOf(state.sp, state.mResolve, tef.params.types.at(0), alignVal)) {
                DEBUG("align_of = " << alignVal);
                auto val = ::MIR::Constant::make_Uint({U128(alignVal), ::HIR::CoreType::Usize});
                bb.statements.push_back(::MIR::Statement::make_Assign({mv$(te.retVal), mv$(val)}));
                bb.terminator = ::MIR::Terminator::make_Goto(te.retBlock);
                changed = true;
            }
        } else if (tef.name == "min_align_of_val") {
            size_t alignVal = 0;
            size_t sizeVal = 0;
            // Note: Trait object returns align_val = 0 (slice-based types have an alignment)
            if (TargetGetSizeAndAlignOf(state.sp, state.mResolve, tef.params.types.at(0), sizeVal, alignVal) && alignVal > 0) {
                DEBUG("min_align_of_val = " << alignVal);
                auto val = ::MIR::Constant::make_Uint({U128(alignVal), ::HIR::CoreType::Usize});
                bb.statements.push_back(::MIR::Statement::make_Assign({mv$(te.retVal), mv$(val)}));
                bb.terminator = ::MIR::Terminator::make_Goto(te.retBlock);
                changed = true;
            }
        }
        // NOTE: Quick special-case for bswap<u8/i8> (a no-op)
        else if (tef.name == "bswap" && (tef.params.types.at(0) == ::HIR::CoreType::U8 || tef.params.types.at(0) == ::HIR::CoreType::I8)) {
            DEBUG("bswap<u8> is a no-op");
            if (auto* e = te.args.at(0).opt_LValue()) {
                bb.statements.push_back(::MIR::Statement::make_Assign({mv$(te.retVal), mv$(*e)}));
            } else {
                bb.statements.push_back(::MIR::Statement::make_Assign({mv$(te.retVal), mv$(te.args.at(0).as_Constant())}));
            }
            bb.terminator = ::MIR::Terminator::make_Goto(te.retBlock);
            changed = true;
        } else if (tef.name == "mrustc_slice_len") {
            MIR_ASSERT(state, te.args.at(0).is_LValue(), "Argument to `mrustc_slice_len` must be a lvalue");
            auto& e = te.args.at(0).as_LValue();
            bb.statements.push_back(::MIR::Statement::make_Assign({mv$(te.retVal), ::MIR::RValue::make_DstMeta({mv$(e)})}));
            bb.terminator = ::MIR::Terminator::make_Goto(te.retBlock);
            changed = true;
        } else if (tef.name == "needs_drop") {
            // Returns `true` if the actual type given as `T` requires drop glue;
            // returns `false` if the actual type provided for `T` implements `Copy`. (Either otherwise)
            // NOTE: libarena assumes that this returns `true` iff T doesn't require drop glue.
            const auto& ty = tef.params.types.at(0);
            // - Only expand at this stage if there's no generics, and no unbound paths
            if (!visitTyWith(ty, [](const ::HIR::TypeData* ty) -> bool {
                return ty->is_Generic() || TU_TEST1(*ty, Path, .binding.is_Unbound());
            })) {
                bool needsDrop = state.mResolve.typeNeedsDropGlue(state.sp, ty);
                bb.statements.push_back(::MIR::Statement::make_Assign({mv$(te.retVal), ::MIR::RValue::make_Constant(::MIR::Constant::make_Bool({needsDrop}))}));
                bb.terminator = ::MIR::Terminator::make_Goto(te.retBlock);
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

        ::std::map<::MIR::LValue, ::MIR::Constant> knownValues;
        // Known enum variants
        ::std::map<::MIR::LValue, unsigned> knownValuesVar;
        ::std::map<unsigned, bool> knownDropFlags;

        auto checkLv = [&](const ::MIR::LValue& lv) -> ::MIR::Constant {
            auto it = knownValues.find(lv);
            if (it != knownValues.end()) {
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
            if (lv.wrappers.empty() && lv.root.is_Static()) {
                DEBUG("Read of a static - " << lv.root.as_Static());
                // Look up this static, and see if it's not mutable, and a primitive
                // - If the static is an immutable primitive: read and save
                MonomorphState ms(state.mResolve.crate.types);
                auto v = state.mResolve.getValue(state.sp, lv.root.as_Static(), ms);
                if (v.is_Static()) {
                    const auto& stat = *v.as_Static();
                    if (stat.valueGenerated && !stat.isMut && state.mResolve.typeIsInteriorMutable(state.sp, stat.mType) == HIR::Compare::Unequal) {
                        // Convert the encoded literal into a `MIR::Const`
                        const auto el = EncodedLiteralSlice(stat.valueRes);
                        // Check the type
                        // - Primitives
                        if (stat.mType->is_Primitive()) {
                            auto ty = stat.mType->as_Primitive();
                            switch (ty) {
                                case HIR::CoreType::Char:
                                case HIR::CoreType::Usize:
                                case HIR::CoreType::U128:
                                case HIR::CoreType::U64:
                                case HIR::CoreType::U32:
                                case HIR::CoreType::U16:
                                case HIR::CoreType::U8:
                                    return ::MIR::Constant::make_Uint({el.readUint(el.mSize), ty});
                                case HIR::CoreType::Bool:
                                    return ::MIR::Constant::make_Bool({el.readUint(el.mSize) != 0});
                                case HIR::CoreType::Isize:
                                case HIR::CoreType::I128:
                                case HIR::CoreType::I64:
                                case HIR::CoreType::I32:
                                case HIR::CoreType::I16:
                                case HIR::CoreType::I8:
                                    return ::MIR::Constant::make_Int({el.readSint(el.mSize), ty});
                                case HIR::CoreType::F16:
                                case HIR::CoreType::F32:
                                case HIR::CoreType::F64:
                                case HIR::CoreType::F128:
                                    return ::MIR::Constant::make_Float({el.readFloat(el.mSize), ty});
                                case HIR::CoreType::Str:
                                    MIR_BUG(state, "Constant of type `str`?");
                            }
                        }
                        // - Pointers
                        if (stat.mType->is_Borrow()) {
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
        auto checkParam = [&](::MIR::Param& p) {
            if (const auto* pe = p.opt_LValue()) {
                auto nv = checkLv(*pe);
                if (nv.is_ItemAddr() && !nv.as_ItemAddr()) {
                    // ItemAddr with a nullptr inner means "no expansion"
                } else {
                    p = mv$(nv);
                    changed = true;
                }
            }
        };

        // Convert known indexes into field acceses
        auto editLval = [&](MIR::LValue& lv, ValUsage _vu) -> bool {
            for (auto& w : lv.wrappers) {
                if (w.is_Index()) {
                    auto it = knownValues.find(MIR::LValue::newLocal(w.as_Index()));
                    if (it != knownValues.find(lv) && !it->second.is_Const() && !it->second.is_Generic()) {
                        MIR_ASSERT(state, it->second.is_Uint(), "Indexing with non-Uint constant - " << it->second);
                        MIR_ASSERT(state, it->second.as_Uint().t == HIR::CoreType::Usize, "Indexing with non-usize constant - " << it->second);
                        auto idx = it->second.as_Uint().v;
                        MIR_ASSERT(state, idx < (1 << 30), "Known index is excessively large");
                        w = MIR::LValue::Wrapper::newField(idx.truncateU64());
                        changed = true;
                    }
                }
            }

            // If a Deref of a known value is seen, replace with the source of that value.
            if (!lv.wrappers.empty() && lv.wrappers.front().is_Deref() && !lv.root.is_Static()) {
                auto ilv = MIR::LValue(lv.root.clone(), {});
                auto it = knownValues.find(ilv);
                if (it != knownValues.find(lv)) {
                    DEBUG("Known deref source: " << ilv << " == " << it->second);
                    //MIR_ASSERT(state, it->second.is_ItemAddr(), "Derefernce with known value not an ItemAddr - " << it->second);
                    if (it->second.is_ItemAddr() && it->second.as_ItemAddr().offset == U128(0)) {
                        lv.wrappers.erase(lv.wrappers.begin());
                        lv.root = MIR::LValue::Storage::newStatic(it->second.as_ItemAddr()->clone());
                        changed = true;
                    }
                }
            }
            return true;
        };

        for (auto& stmt : bb.statements) {
            auto stmtidx = &stmt - &bb.statements.front();
            state.setCurStmt(bbidx, stmtidx);

            visitMirLvaluesMut(stmt, editLval);

            // Scan statements forwards:
            // - If a known temporary is used as Param::LValue, replace LValue with the value
            // - If a UniOp has its input known, evaluate
            // - If a BinOp has both values known, evaluate
            if (auto* e = stmt.opt_Assign()) {
                struct H {
                    static S128 truncateS(::HIR::CoreType ct, S128 v) {
                        // Truncate unsigned, then sign extend
                        auto u = H::truncateU(ct, v.getInner());
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
                                if (TargetGetPointerBits() < 64) {
                                    return sext(u, TargetGetPointerBits());
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

                    static U128 truncateU(::HIR::CoreType ct, U128 v) {
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
                                if (TargetGetPointerBits() < 64) {
                                    return v & U128(UINT64_MAX >> (64 - TargetGetPointerBits()));
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
                        auto nv = checkLv(se);
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
                        checkParam(se.val);
                    }
                    TU_ARMA(Borrow, se) {
                        // Shared borrows of statics can be better represented with the ItemAddr constant
                        if (se.type == HIR::BorrowType::Shared && se.val.wrappers.empty() && se.val.root.is_Static()) {
                            e->src = ::MIR::RValue::make_Constant(::MIR::Constant::make_ItemAddr({box$(se.val.root.as_Static())}));
                            changed = true;
                        } else if (se.type == HIR::BorrowType::Unique) {
                            knownValues.erase(se.val);
                            knownValuesVar.erase(se.val);
                        }
                    }
                    TU_ARMA(Cast, se) {
                        ::MIR::Constant newValue;

                        // If casting a number to a number, do the cast and
                        auto nv = checkLv(se.val);
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
                                            newValue = ::MIR::Constant::make_Uint({H::truncateU(*te, vp->v), *te});
                                        } else if (const auto* vp = nv.opt_Int()) {
                                            newValue = ::MIR::Constant::make_Uint({H::truncateU(*te, vp->v.getInner()), *te});
                                        } else if (const auto* vp = nv.opt_Bool()) {
                                            newValue = ::MIR::Constant::make_Uint({U128(vp->v ? 1u : 0u), *te});
                                        } else if (const auto* vp = nv.opt_Float()) {
                                            // NaN fails both comparisons and is left unfolded
                                            if (FloatValue() <= vp->v && vp->v < FloatValue(18446744073709551616.0)) {
                                                newValue = ::MIR::Constant::make_Uint({H::truncateU(*te, U128(static_cast<uint64_t>(vp->v))), *te});
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
                                            newValue = ::MIR::Constant::make_Int({H::truncateS(*te, vp->v), *te});
                                        } else if (const auto* vp = nv.opt_Int()) {
                                            newValue = ::MIR::Constant::make_Int({H::truncateS(*te, vp->v), *te});
                                        } else if (const auto* vp = nv.opt_Bool()) {
                                            newValue = ::MIR::Constant::make_Int({S128(vp->v ? 1 : 0), *te});
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
                        } else if (knownValuesVar.count(se.val)) {
                            auto variantIdx = knownValuesVar.at(se.val);
                            MIR_ASSERT(state, se.type->is_Primitive(), "Casting enum to non-primitive - " << se.type);

                            HIR::TypeRef tmp;
                            const auto& srcTy = state.getLvalueType(tmp, se.val);
                            const HIR::Enum& enm = *srcTy->as_Path().binding.as_Enum();
                            MIR_ASSERT(state, enm.isValue(), "Casting non-value enum to value");
                            auto v = enm.getValue(variantIdx);

                            const auto* repr = TargetGetTypeRepr(state.sp, state.mResolve, srcTy);
                            MIR_ASSERT(state, repr && repr->variants.is_Values(), "Value enum without values repr - " << srcTy);
                            const auto& values = repr->variants.as_Values();
                            const auto& tagTy = TargetGetInnerType(state.sp, state.mResolve, *repr, values.field.index, values.field.subFields);
                            MIR_ASSERT(state, tagTy->is_Primitive(), "Value enum with non-primitive tag - " << srcTy);

                            auto value = S128(U128(v));
                            switch (tagTy->as_Primitive()) {
                                case ::HIR::CoreType::I8:
                                case ::HIR::CoreType::I16:
                                case ::HIR::CoreType::I32:
                                case ::HIR::CoreType::I64:
                                case ::HIR::CoreType::I128:
                                case ::HIR::CoreType::Isize:
                                    value = H::truncateS(tagTy->as_Primitive(), value);
                                    break;
                                default:
                                    value = S128(H::truncateU(tagTy->as_Primitive(), value.getInner()));
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
                                    newValue = ::MIR::Constant::make_Uint({H::truncateU(ct, value.getInner()), ct});
                                    break;
                                case ::HIR::CoreType::I8:
                                case ::HIR::CoreType::I16:
                                case ::HIR::CoreType::I32:
                                case ::HIR::CoreType::I64:
                                case ::HIR::CoreType::I128:
                                case ::HIR::CoreType::Isize:
                                    newValue = ::MIR::Constant::make_Int({H::truncateS(ct, value), ct});
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

                        if (newValue != MIR::Constant()) {
                            DEBUG(state << " " << e->src << " = " << newValue);
                            e->src = mv$(newValue);
                            changed = true;
                        }
                    }
                    TU_ARMA(BinOp, se) {
                        checkParam(se.valL);
                        checkParam(se.valR);

                        if (se.valL.is_Constant() && se.valR.is_Constant()) {
                            const auto& valL = se.valL.as_Constant();
                            const auto& valR = se.valR.as_Constant();

                            if (valL.is_Const() || valR.is_Const()) {
                                // One of the arms is a named constant, can't check (they're not an actual value, just a
                                // reference to one)
                            } else if (valL.is_Generic() || valR.is_Generic()) {
                                // One of the arms is a generic, can't check either
                            } else {
                                ::MIR::Constant newValue;
                                switch (se.op) {
                                    // Note: f32's bit accuracy is different to f64, so they can't be considered equivalent in behaviour
                                    case ::MIR::eBinOp::EQ:
                                        if (!valL.is_Float()) {
                                            newValue = ::MIR::Constant::make_Bool({valL == valR});
                                        }
                                        break;
                                    case ::MIR::eBinOp::NE:
                                        if (!valL.is_Float()) {
                                            newValue = ::MIR::Constant::make_Bool({valL != valR});
                                        }
                                        break;
                                    case ::MIR::eBinOp::LT:
                                        if (!valL.is_Float()) {
                                            newValue = ::MIR::Constant::make_Bool({valL < valR});
                                        }
                                        break;
                                    case ::MIR::eBinOp::LE:
                                        if (!valL.is_Float()) {
                                            newValue = ::MIR::Constant::make_Bool({valL <= valR});
                                        }
                                        break;
                                    case ::MIR::eBinOp::GT:
                                        if (!valL.is_Float()) {
                                            newValue = ::MIR::Constant::make_Bool({valL > valR});
                                        }
                                        break;
                                    case ::MIR::eBinOp::GE:
                                        if (!valL.is_Float()) {
                                            newValue = ::MIR::Constant::make_Bool({valL >= valR});
                                        }
                                        break;

                                    case ::MIR::eBinOp::ADD:
                                        MIR_ASSERT(state, valL.tag() == valR.tag(), "Mismatched types for eBinOp::ADD - " << valL << " + " << valR);
                                TU_MATCH_HDRA( (valL, valR), {)
                                default:
                                    break;
                                            TU_ARMA(Float, le, re) {
                                                MIR_ASSERT(state, le.t == re.t, "Mismatched types for eBinOp::ADD - " << valL << " / " << valR);
                                                newValue = makeFloatArithmeticResult(le.v + re.v, le.t);
                                            }
                                            TU_ARMA(Int, le, re) {
                                                MIR_ASSERT(state, le.t == re.t, "Mismatched types for eBinOp::ADD - " << valL << " + " << valR);
                                                newValue = ::MIR::Constant::make_Int({H::truncateS(le.t, le.v + re.v), le.t});
                                            }
                                            TU_ARMA(Uint, le, re) {
                                                MIR_ASSERT(state, le.t == re.t, "Mismatched types for eBinOp::ADD - " << valL << " + " << valR);
                                                newValue = ::MIR::Constant::make_Uint({H::truncateU(le.t, le.v + re.v), le.t});
                                            }
                                }
                                break;
                            case ::MIR::eBinOp::SUB:
                                MIR_ASSERT(state, valL.tag() == valR.tag(), "Mismatched types for eBinOp::SUB - " << valL << " + " << valR);
                                TU_MATCH_HDRA( (valL, valR), {)
                                default:
                                    break;
                                            TU_ARMA(Float, le, re) {
                                                MIR_ASSERT(state, le.t == re.t, "Mismatched types for eBinOp::SUB - " << valL << " / " << valR);
                                                newValue = makeFloatArithmeticResult(le.v - re.v, le.t);
                                            }
                                            TU_ARMA(Int, le, re) {
                                                MIR_ASSERT(state, le.t == re.t, "Mismatched types for eBinOp::SUB - " << valL << " - " << valR);
                                                newValue = ::MIR::Constant::make_Int({H::truncateS(le.t, le.v - re.v), le.t});
                                            }
                                            TU_ARMA(Uint, le, re) {
                                                MIR_ASSERT(state, le.t == re.t, "Mismatched types for eBinOp::SUB - " << valL << " - " << valR);
                                                newValue = ::MIR::Constant::make_Uint({H::truncateU(le.t, le.v - re.v), le.t});
                                            }
                                }
                                break;
                            case ::MIR::eBinOp::MUL:
                                MIR_ASSERT(state, valL.tag() == valR.tag(), "Mismatched types for eBinOp::MUL - " << valL << " * " << valR);
                                TU_MATCH_HDRA( (valL, valR), {)
                                default:
                                    break;
                                            TU_ARMA(Float, le, re) {
                                                MIR_ASSERT(state, le.t == re.t, "Mismatched types for eBinOp::MUL - " << valL << " / " << valR);
                                                newValue = makeFloatArithmeticResult(le.v * re.v, le.t);
                                            }
                                            TU_ARMA(Int, le, re) {
                                                MIR_ASSERT(state, le.t == re.t, "Mismatched types for eBinOp::MUL - " << valL << " * " << valR);
                                                newValue = ::MIR::Constant::make_Int({H::truncateS(le.t, le.v * re.v), le.t});
                                            }
                                            TU_ARMA(Uint, le, re) {
                                                MIR_ASSERT(state, le.t == re.t, "Mismatched types for eBinOp::MUL - " << valL << " * " << valR);
                                                newValue = ::MIR::Constant::make_Uint({H::truncateU(le.t, le.v * re.v), le.t});
                                            }
                                }
                                break;
                            case ::MIR::eBinOp::DIV:
                                MIR_ASSERT(state, valL.tag() == valR.tag(), "Mismatched types for eBinOp::DIV - " << valL << " / " << valR);
                                TU_MATCH_HDRA( (valL, valR), {)
                                default:
                                    break;
                                            TU_ARMA(Float, le, re) {
                                                MIR_ASSERT(state, le.t == re.t, "Mismatched types for eBinOp::DIV - " << valL << " / " << valR);
                                                newValue = makeFloatArithmeticResult(le.v / re.v, le.t);
                                            }
                                            TU_ARMA(Int, le, re) {
                                                MIR_ASSERT(state, le.t == re.t, "Mismatched types for eBinOp::DIV - " << valL << " / " << valR);
                                                if (re.v == 0) {
                                                    DEBUG(state << "Const eval error: Constant division by zero");
                                                } else {
                                                    newValue = ::MIR::Constant::make_Int({H::truncateS(le.t, le.v / re.v), le.t});
                                                }
                                            }
                                            TU_ARMA(Uint, le, re) {
                                                MIR_ASSERT(state, le.t == re.t, "Mismatched types for eBinOp::DIV - " << valL << " / " << valR);
                                                if (re.v == 0) {
                                                    DEBUG(state << "Const eval error: Constant division by zero");
                                                } else {
                                                    newValue = ::MIR::Constant::make_Uint({H::truncateU(le.t, le.v / re.v), le.t});
                                                }
                                            }
                                }
                                break;
                            case ::MIR::eBinOp::MOD:
                                MIR_ASSERT(state, valL.tag() == valR.tag(), "Mismatched types for eBinOp::MOD - " << valL << " % " << valR);
                                TU_MATCH_HDRA( (valL, valR), {)
                                default:
                                    break;
                                            TU_ARMA(Int, le, re) {
                                                MIR_ASSERT(state, le.t == re.t, "Mismatched types for eBinOp::MOD - " << valL << " % " << valR);
                                                MIR_ASSERT(state, re.v != 0, "Const eval error: Constant division by zero");
                                                newValue = ::MIR::Constant::make_Int({H::truncateS(le.t, le.v % re.v), le.t});
                                            }
                                            TU_ARMA(Uint, le, re) {
                                                MIR_ASSERT(state, le.t == re.t, "Mismatched types for eBinOp::MOD - " << valL << " % " << valR);
                                                MIR_ASSERT(state, re.v != 0, "Const eval error: Constant division by zero");
                                                newValue = ::MIR::Constant::make_Uint({H::truncateU(le.t, le.v % re.v), le.t});
                                            }
                                }
                                break;

                            case ::MIR::eBinOp::BIT_AND:
                                MIR_ASSERT(state, valL.tag() == valR.tag(), "Mismatched types for eBinOp::BIT_AND - " << valL << " & " << valR);
                                TU_MATCH_HDRA( (valL, valR), {)
                                default:
                                    break;
                                            TU_ARMA(Bool, le, re) {
                                                newValue = ::MIR::Constant::make_Bool({le.v && re.v});
                                            }
                                            TU_ARMA(Int, le, re) {
                                                MIR_ASSERT(state, le.t == re.t, "Mismatched types for eBinOp::BIT_AND - " << valL << " ^ " << valR);
                                                newValue = ::MIR::Constant::make_Int({H::truncateS(le.t, le.v & re.v), le.t});
                                            }
                                            TU_ARMA(Uint, le, re) {
                                                MIR_ASSERT(state, le.t == re.t, "Mismatched types for eBinOp::BIT_AND - " << valL << " ^ " << valR);
                                                newValue = ::MIR::Constant::make_Uint({H::truncateU(le.t, le.v & re.v), le.t});
                                            }
                                }
                                break;
                            case ::MIR::eBinOp::BIT_OR:
                                MIR_ASSERT(state, valL.tag() == valR.tag(), "Mismatched types for eBinOp::BIT_OR - " << valL << " | " << valR);
                                TU_MATCH_HDRA( (valL, valR), {)
                                default:
                                    break;
                                            TU_ARMA(Bool, le, re) {
                                                newValue = ::MIR::Constant::make_Bool({le.v || re.v});
                                            }
                                            TU_ARMA(Int, le, re) {
                                                MIR_ASSERT(state, le.t == re.t, "Mismatched types for eBinOp::BIT_OR - " << valL << " | " << valR);
                                                newValue = ::MIR::Constant::make_Int({H::truncateS(le.t, le.v | re.v), le.t});
                                            }
                                            TU_ARMA(Uint, le, re) {
                                                MIR_ASSERT(state, le.t == re.t, "Mismatched types for eBinOp::BIT_OR - " << valL << " | " << valR);
                                                newValue = ::MIR::Constant::make_Uint({H::truncateU(le.t, le.v | re.v), le.t});
                                            }
                                }
                                break;
                            case ::MIR::eBinOp::BIT_XOR:
                                MIR_ASSERT(state, valL.tag() == valR.tag(), "Mismatched types for eBinOp::BIT_XOR - " << valL << " ^ " << valR);
                                TU_MATCH_HDRA( (valL, valR), {)
                                default:
                                    break;
                                            TU_ARMA(Bool, le, re) {
                                                newValue = ::MIR::Constant::make_Bool({le.v != re.v});
                                            }
                                            TU_ARMA(Int, le, re) {
                                                MIR_ASSERT(state, le.t == re.t, "Mismatched types for eBinOp::BIT_XOR - " << valL << " ^ " << valR);
                                                newValue = ::MIR::Constant::make_Int({H::truncateS(le.t, le.v ^ re.v), le.t});
                                            }
                                            TU_ARMA(Uint, le, re) {
                                                MIR_ASSERT(state, le.t == re.t, "Mismatched types for eBinOp::BIT_XOR - " << valL << " ^ " << valR);
                                                newValue = ::MIR::Constant::make_Uint({H::truncateU(le.t, le.v ^ re.v), le.t});
                                            }
                                }
                                break;

                            // --- Bit Shifts ---
                            case ::MIR::eBinOp::BIT_SHL: {
                                            U128 shiftLenR;
                                TU_MATCH_HDRA( (valR), {)
                                default:
                                    MIR_BUG(state, "Mismatched types for eBinOp::BIT_SHL - " << valL << " >> " << valR);
                                                break;
                                                TU_ARMA(Int, re) {
                                                    shiftLenR = re.v.getInner();
                                                }
                                                TU_ARMA(Uint, re) {
                                                    shiftLenR = re.v;
                                                }
                                }
                                MIR_ASSERT(state, shiftLenR <= 128, "Const eval error: Over-sized eBinOp::BIT_SHL - " << valL << " << " << valR);
                                auto shiftLen = shiftLenR.truncateU64();
                                TU_MATCH_HDRA( (valL), {)
                                default:
                                    break;
                                                TU_ARMA(Int, le) {
                                                    newValue = ::MIR::Constant::make_Int({H::truncateS(le.t, le.v << shiftLen), le.t});
                                                }
                                                TU_ARMA(Uint, le) {
                                                    newValue = ::MIR::Constant::make_Uint({H::truncateU(le.t, le.v << shiftLen), le.t});
                                                }
                                }
                                } break;
                            case ::MIR::eBinOp::BIT_SHR:{
                                            U128 shiftLenR;
                                TU_MATCH_HDRA( (valR), {)
                                default:
                                    MIR_BUG(state, "Mismatched types for eBinOp::BIT_SHR - " << valL << " >> " << valR);
                                                break;
                                                TU_ARMA(Int, re) {
                                                    shiftLenR = re.v.getInner();
                                                }
                                                TU_ARMA(Uint, re) {
                                                    shiftLenR = re.v;
                                                }
                                }
                                MIR_ASSERT(state, shiftLenR <= 128, "Const eval error: Over-sized shift - " << valL << " >> " << valR);
                                auto shiftLen = shiftLenR.truncateU64();
                                TU_MATCH_HDRA( (valL), {)
                                default:
                                    break;
                                                TU_ARMA(Int, le) {
                                                    newValue = ::MIR::Constant::make_Int({H::truncateS(le.t, le.v >> shiftLen), le.t});
                                                }
                                                TU_ARMA(Uint, le) {
                                                    newValue = ::MIR::Constant::make_Uint({H::truncateU(le.t, le.v >> shiftLen), le.t});
                                                }
                                }
                                } break;
                            // TODO: Other binary operations
                            // Could emit a TODO?
                            default:
                                break;
                                }

                                if (newValue != ::MIR::Constant()) {
                                    DEBUG(state << " " << e->src << " = " << newValue);
                                    e->src = mv$(newValue);
                                    changed = true;
                                }
                            }
                        } else {
                            ::MIR::Param newValue;
                            // No-ops
                            switch (se.op) {
                                // `foo + 0 == foo`
                                // `foo - 0 == foo`
                                case ::MIR::eBinOp::ADD:
                                case ::MIR::eBinOp::SUB:
                                    if (se.valR.is_Constant() && se.valR.as_Constant().is_Uint() && se.valR.as_Constant().as_Uint().v == 0) {
                                        newValue = mv$(se.valL);
                                    }
                                    break;
                                // `foo % 1 == 0`
                                case ::MIR::eBinOp::MOD:
                                    if (se.valR.is_Constant() && se.valR.as_Constant().is_Uint() && se.valR.as_Constant().as_Uint().v == 1) {
                                        newValue = ::MIR::Constant::make_Uint({U128(0), se.valR.as_Constant().as_Uint().t});
                                    }
                                    break;
                                // `foo / 1 == foo`
                                case ::MIR::eBinOp::DIV:
                                    if (se.valR.is_Constant() && se.valR.as_Constant().is_Uint() && se.valR.as_Constant().as_Uint().v == 1) {
                                        newValue = mv$(se.valL);
                                    }
                                    break;
                                // `foo * 0 == 0`
                                // `foo * 1 == foo`
                                // `0 * foo == 0`
                                // `1 * foo == foo`
                                case ::MIR::eBinOp::MUL:
                                    if (se.valR.is_Constant() && se.valR.as_Constant().is_Uint()) {
                                        auto& v = se.valR.as_Constant().as_Uint();
                                        if (v.v == 0) {
                                            newValue = ::MIR::Constant::make_Uint({U128(0), v.t});
                                        } else if (v.v == 1) {
                                            newValue = mv$(se.valL);
                                        } else {
                                        }
                                    }
                                    if (se.valL.is_Constant() && se.valL.as_Constant().is_Uint()) {
                                        auto& v = se.valL.as_Constant().as_Uint();
                                        if (v.v == 0) {
                                            newValue = ::MIR::Constant::make_Uint({U128(0), v.t});
                                        } else if (v.v == 1) {
                                            newValue = mv$(se.valR);
                                        } else {
                                        }
                                    }
                                    break;
                                default:
                                    break;
                            }
                            if (newValue != ::MIR::Param()) {
                                DEBUG(state << " " << e->src << " = " << newValue);
                            TU_MATCH_HDRA( (newValue), {)
                            TU_ARMA(LValue, v)   e->src = mv$(v);
                                    TU_ARMA(Borrow, _) throw "";
                                    TU_ARMA(Constant, v) e->src = mv$(v);
                            }
                            changed = true;
                            }
                        }
                    }
                    TU_ARMA(UniOp, se) {
                        auto it = knownValues.find(se.val);
                        if (it != knownValues.end()) {
                            const auto& val = it->second;
                            ::MIR::Constant newValue;
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
                                                    val = H::truncateU(ve.t, ~val);
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
                                            newValue = ::MIR::Constant::make_Uint({val, ve.t});
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
                                                    val = H::truncateS(ve.t, ~val);
                                                    replace = true;
                                                    break;
                                                case ::HIR::CoreType::I128:
                                                    // TODO: Are there any cases where sign extension stops being correct here?
                                                    val = H::truncateS(ve.t, ~val);
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
                                            newValue = ::MIR::Constant::make_Int({val, ve.t});
                                        }
                                        TU_ARMA(Float, ve) {
                                            // Not valid?
                                        }
                                        TU_ARMA(Bool, ve) {
                                            newValue = ::MIR::Constant::make_Bool({!ve.v});
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
                                newValue = ::MIR::Constant::make_Int({ -ve.v, ve.t });
                                replace = true;
                                ),
                            (Float,
                                if (!floatValueIsNan(ve.v)) {
                                        newValue = ::MIR::Constant::make_Float({-ve.v, ve.t});
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
                                DEBUG(state << " " << e->src << " = " << newValue);
                                e->src = mv$(newValue);
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
                        if (TU_TEST2(se.metaVal, Constant, , ItemAddr, .get() == nullptr)) {
                        } else {
                            checkParam(se.ptrVal);
                            checkParam(se.metaVal);
                        }
                    }
                    TU_ARMA(Tuple, se) {
                        for (auto& p : se.vals) {
                            checkParam(p);
                        }
                    }
                    TU_ARMA(Array, se) {
                        for (auto& p : se.vals) {
                            checkParam(p);
                        }
                    }
                    TU_ARMA(UnionVariant, se) {
                        checkParam(se.val);
                    }
                    TU_ARMA(EnumVariant, se) {
                        for (auto& p : se.vals) {
                            checkParam(p);
                        }
                    }
                    TU_ARMA(Struct, se) {
                        for (auto& p : se.vals) {
                            checkParam(p);
                        }
                    }
                }
            } else if (const auto* se = stmt.opt_SetDropFlag()) {
                if (se->other == ~0u) {
                    knownDropFlags[se->idx] = se->newVal;
                } else {
                    auto it = knownDropFlags.find(se->other);
                    if (it != knownDropFlags.end()) {
                        knownDropFlags[se->idx] = se->newVal != it->second;
                    }
                }
            }
            // - If a known temporary is borrowed mutably or mutated somehow, clear its knowledge
            visitMirLvalues(stmt, [&knownValues, &knownValuesVar](const ::MIR::LValue& lv, ValUsage vu) -> bool {
                if (vu == ValUsage::Write) {
                    knownValues.erase(lv);
                    knownValuesVar.erase(lv);
                }
                return false;
            });
            // - Locate `temp = SOME_CONST` and record value
            if (const auto* e = stmt.opt_Assign()) {
                if (e->dst.is_Local()) {
                    // Known constant
                    if (const auto* ce = e->src.opt_Constant()) {
                        knownValues.insert(::std::make_pair(e->dst.clone(), ce->clone()));
                        DEBUG(state << stmt);
                    }
                    // Known variant
                    else if (const auto* ce = e->src.opt_EnumVariant()) {
                        knownValuesVar.insert(::std::make_pair(e->dst.clone(), ce->index));
                        DEBUG(state << stmt);
                    }
                    // Propagate knowledge through Local=Local assignments
                    else if (const auto* ce = e->src.opt_Use()) {
                        if (ce->is_Local()) {
                            auto it1 = knownValues.find(*ce);
                            auto it2 = knownValuesVar.find(*ce);
                            assert(!(it1 != knownValues.end() && it2 != knownValuesVar.end()));
                            if (it1 != knownValues.end()) {
                                knownValues.insert(::std::make_pair(e->dst.clone(), it1->second.clone()));
                                DEBUG(state << stmt);
                            } else if (it2 != knownValuesVar.end()) {
                                knownValuesVar.insert(::std::make_pair(e->dst.clone(), it2->second));
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

        state.setCurStmtTerm(bbidx);
        if (auto* te = bb.terminator.opt_Drop()) {
            if (te->flagIdx != ~0u) {
                auto it = knownDropFlags.find(te->flagIdx);
                if (it != knownDropFlags.end()) {
                    if (it->second) {
                        te->flagIdx = ~0u;
                    } else {
                        auto target = te->target;
                        bb.terminator = ::MIR::Terminator::make_Goto(target);
                    }
                    changed = true;
                }
            }
        }
        visitMirLvaluesMut(bb.terminator, editLval);
        switch (bb.terminator.tag()) {
            case ::MIR::Terminator::TAGDEAD:
                throw "";
                TU_ARM(bb.terminator, Switch, te) {
                    auto it = knownValuesVar.find(te.val);
                    if (it != knownValuesVar.end()) {
                        MIR_ASSERT(state, it->second < te.targets.size(), "Terminator::Switch with known variant index out of bounds" << " (#" << it->second << " with " << bb.terminator << ")");
                        auto newBb = te.targets.at(it->second);
                        DEBUG(state << "Convert " << bb.terminator << " into Goto(" << newBb << ") because variant known to be #" << it->second);
                        bb.terminator = ::MIR::Terminator::make_Goto(newBb);

                        changed = true;
                    }
                }
                break;
                TU_ARM(bb.terminator, If, te) {
                    auto it = knownValues.find(te.cond);
                    if (it != knownValues.end()) {
                        if (it->second.is_Const() || it->second.is_Generic()) {
                        } else {
                            MIR_ASSERT(state, it->second.is_Bool(), "Terminator::If with known value not Bool - " << it->second);
                            auto newBb = (it->second.as_Bool().v ? te.bbTrue : te.bbFalse);
                            DEBUG(state << "Convert " << bb.terminator << " into Goto(" << newBb << ") because condition known to be " << it->second);
                            bb.terminator = ::MIR::Terminator::make_Goto(newBb);

                            changed = true;
                        }
                    }
                }
                break;
                TU_ARM(bb.terminator, Call, te) {
                    for (auto& a : te.args) {
                        checkParam(a);
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

        auto hasCond = [&](const auto& lv, auto ut) -> bool {
            return lv == te.cond;
        };
        bool valKnown = false;
        bool knownVal;
        for (unsigned int i = bb.statements.size(); i--;) {
            if (bb.statements[i].is_Assign()) {
                const auto& se = bb.statements[i].as_Assign();
                // If the condition was mentioned, don't assume it has the same value
                // TODO: What if the condition is a field/index and something else is edited?
                if (visitMirLvalues(se.src, hasCond)) {
                    break;
                }

                if (se.dst != te.cond) {
                    continue;
                }
                if (se.src.is_Constant() && se.src.as_Constant().is_Bool()) {
                    valKnown = true;
                    knownVal = se.src.as_Constant().as_Bool().v;
                } else {
                    valKnown = false;
                }
                break;
            } else {
                if (visitMirLvalues(bb.statements[i], hasCond)) {
                    break;
                }
            }
        }
        if (valKnown) {
            DEBUG("bb" << bbidx << ": Condition known to be " << knownVal);
            bb.terminator = ::MIR::Terminator::make_Goto(knownVal ? te.bbTrue : te.bbFalse);
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
bool MIROptimiseSplitAggregates(::MIR::TypeResolve& state, ::MIR::Function& fcn) {
    bool changed = false;
    TRACE_FUNCTION_FR("", changed);

    // Find locals that are:
    // - Assigned once
    // - From a constructor
    // - And only ever used via a field access
    // Replace the construction with assignments of `n` locals instead (which can be optimised by further passes)

    struct Potential {
        size_t srcBbIdx;
        size_t srcStmtIdx;
        unsigned variantIdx;

        bool isDirectUsed;
        unsigned nWrite;
        std::vector<unsigned> replacements;

        Potential(size_t srcBbIdx, size_t srcStmtIdx, unsigned variantIdx = ~0u)
            : srcBbIdx(srcBbIdx)
            , srcStmtIdx(srcStmtIdx)
            , variantIdx(variantIdx)
            , isDirectUsed(false)
            , nWrite(0)
        {
        }
    };

    std::map<unsigned, Potential> potentials;

    // 1. Find locals created from constructors (struct/tuple)
    for (const auto& block : fcn.blocks) {
        size_t bbIdx = &block - &fcn.blocks.front();
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
                    DEBUG("> BB" << bbIdx << "/" << i << ": POSSIBLE " << stmt);
                    potentials.insert(std::make_pair(se->dst.as_Local(), Potential(bbIdx, i, sse->index)));
                    continue;
                }
                // NOTE: Union variants need special handling in the replacement
                else {
                    continue;
                }

                // Found a potential.
                DEBUG("> BB" << bbIdx << "/" << i << ": POSSIBLE " << stmt);
                potentials.insert(std::make_pair(se->dst.as_Local(), Potential(bbIdx, i)));
            }
        }
    }
    // - Nothing to do? return early
    if (potentials.empty()) {
        return false;
    }

    // 2. Check how the variables are used (allow one write, and no other direct usage)
    // - Removes any potentials that are invalidated.
    visitMirLvalues(state, fcn, [&](const MIR::LValue& lv, ValUsage vu) -> bool {
        if (lv.root.is_Local()) {
            // Is this one of the potentials?
            auto it = potentials.find(lv.root.as_Local());
            if (it != potentials.end()) {
                if (lv.wrappers.empty()) {
                    // NOTE: A single write is allowed (the assignment)
                    // - Any other would be a re-assignent or a drop
                    if (vu == ValUsage::Write) {
                        it->second.nWrite += 1;
                    } else {
                        // Direct usage!
                        it->second.isDirectUsed = true;
                    }
                } else if (lv.wrappers.front().is_Field()) {
                    // Field acess: allowed UNLESS it's a borrow of the first field
                    // TODO: Find out what code makes the assumption that `&foo.0` is a good stand-in for `&foo`
                    if (lv.wrappers.front().as_Field() == 0 && vu == ValUsage::Borrow) {
                        it->second.isDirectUsed = true;
                    }
                } else if (lv.wrappers.front().is_Downcast()) {
                    // Downcast to a variant other than the variant it was constructed as, don't do anything.
                    // - For enums, this is an error (but here we don't know for sure). For unions it's valid behaviour
                    // A bare downcast uses the complete variant payload, so it cannot be replaced with a field local.
                    if (lv.wrappers.front().as_Downcast() != it->second.variantIdx || lv.wrappers.size() < 2 || !lv.wrappers[1].is_Field()) {
                        it->second.isDirectUsed = true;
                    }
                } else {
                    // Index and deref are disallowed
                    it->second.isDirectUsed = true;
                }

                // If invalidated, delete.
                if (it->second.isDirectUsed || it->second.nWrite > 1) {
                    const auto& stmt = fcn.blocks[it->second.srcBbIdx].statements[it->second.srcStmtIdx];
                    DEBUG(state << ": REMOVE BB" << it->second.srcBbIdx << "/" << it->second.srcStmtIdx << " " << stmt << " from " << lv /*<< " vu=" << vu*/);
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
        auto bbIdx = p.second.srcBbIdx;
        auto stmtIdx = p.second.srcStmtIdx;
        state.setCurStmt(bbIdx, stmtIdx);
        auto& block = fcn.blocks[bbIdx];

        DEBUG("- BB" << bbIdx << "/" << stmtIdx << ": " << block.statements[stmtIdx]);
        // Extract the list of values from the existing statement
        std::vector<MIR::Param> vals;
        {
            auto& src = block.statements[stmtIdx].as_Assign().src;
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
            std::move_backward(block.statements.begin() + stmtIdx + 1, block.statements.end() - offset, block.statements.end());
        }

        // Create new statements (allocating new locals)
        auto newLocalBase = fcn.locals.size();
        fcn.locals.resize(fcn.locals.size() + vals.size());
        p.second.replacements.resize(vals.size());
        for (size_t i = 0; i < vals.size(); i++) {
            // Allocate a new local
            auto new_local = static_cast<unsigned>(newLocalBase + i);
            ::HIR::TypeRef tmp;
            fcn.locals[new_local] = state.getParamType(tmp, vals[i]);
            p.second.replacements[i] = new_local;
            // Set the relevant statement to be an assignment to that new local
            block.statements[stmtIdx + i] = MIR::Statement::make_Assign({MIR::LValue::newLocal(new_local), paramToRvalue(mv$(vals[i]))});
            DEBUG("+ BB" << bbIdx << "/" << (stmtIdx + i) << ": " << block.statements[stmtIdx + i]);
        }

        //for(size_t i = 0; i < block.statements.size(); i ++)
        //    DEBUG("> BB" << bb_idx << "/" << i << ": " << block.statements[i]);

        // If this replacement changed the number of statements in this block, update all existing references.
        if (offset > 0) {
            for (auto& otherP : potentials) {
                if (otherP.second.srcBbIdx == bbIdx && otherP.second.srcStmtIdx > stmtIdx) {
                    otherP.second.srcStmtIdx += offset;
                }
            }
        }
    }

    // 4. Replace all usages
    visitMirLvaluesMut(state, fcn, [&](MIR::LValue& lv, ValUsage vu) -> bool {
        if (lv.root.is_Local()) {
            // Is this one of the potentials?
            auto it = potentials.find(lv.root.as_Local());
            if (it != potentials.end()) {
                size_t ndel;
                size_t fieldIdx;
                if (it->second.variantIdx == ~0u) {
                    fieldIdx = lv.wrappers.front().as_Field();
                    ndel = 1;
                } else {
                    MIR_ASSERT(state, lv.wrappers[0].is_Downcast(), lv);
                    MIR_ASSERT(state, lv.wrappers[1].is_Field(), lv);
                    fieldIdx = lv.wrappers[1].as_Field();
                    ndel = 2;
                }
                auto new_wrappers = std::vector<MIR::LValue::Wrapper>(lv.wrappers.begin() + ndel, lv.wrappers.end());
                auto newRoot = MIR::LValue::Storage::newLocal(it->second.replacements.at(fieldIdx));
                auto newLv = MIR::LValue(mv$(newRoot), mv$(new_wrappers));
                DEBUG(state << " " << lv << " -> " << newLv);
                lv = mv$(newLv);
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
bool MIROptimisePropagateSingleAssignments(::MIR::TypeResolve& state, ::MIR::Function& fcn) {
    bool replacementHappend;
    TRACE_FUNCTION_FR("", replacementHappend);

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
        ::std::vector<ValUse> localUses;

        void useLvalue(const ::MIR::LValue& lv, ValUsage ut) {
            for (const auto& w : lv.wrappers) {
                if (w.is_Index()) {
                    //local_uses[w.as_Index()].read += 1;
                    localUses[w.as_Index()].borrow += 1;
                }
            }
            if (lv.root.is_Local()) {
                auto& vu = localUses[lv.root.as_Local()];
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
    } valUses = {::std::vector<ValUse>(fcn.locals.size())};

    visitMirLvalues(state, fcn, [&](const auto& lv, auto ut) {
        valUses.useLvalue(lv, ut);
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
        auto replacementsFind = [&replacements](const ::MIR::LValue::CRef& lv) {
            return ::std::find_if(replacements.begin(), replacements.end(), [&](const auto& e) {
                return lv == e.first;
            });
        };
        for (const auto& block : fcn.blocks) {
            if (block.terminator.tag() == ::MIR::Terminator::TAGDEAD) {
                continue;
            }

            for (unsigned int stmtIdx = 0; stmtIdx < block.statements.size(); stmtIdx++) {
                state.setCurStmt(&block - &fcn.blocks.front(), stmtIdx);
                const auto& stmt = block.statements[stmtIdx];
                DEBUG(state << stmt);
                // > Assignment
                if (!stmt.is_Assign()) {
                    continue;
                }
                const auto& e = stmt.as_Assign();
                // > Of a temporary from with a RValue::Use
                if (e.dst.is_Local()) {
                    const auto& vu = valUses.localUses[e.dst.as_Local()];
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
                bool onlyOne = false;
                if (e.src.is_Use()) {
                    // Keep the complexity down
                    const auto* srcp = &e.src.as_Use();
                    // If there are deref/index accesses, then only allow one statement
                    // - This is the lazy option, avoids needing to check for invalidation (could be a write through deref)
                    if (::std::any_of(srcp->wrappers.begin(), srcp->wrappers.end(), [](auto& w) {
                        return !w.is_Field() && !w.is_Downcast();
                    })) {
                        DEBUG("Non-field access");
                        onlyOne = true;
                        continue;
                    }
                    // TODO: Why is this limited to locals only?
                    if (!srcp->root.is_Local()) {
                        DEBUG("> Can't replace, not a local root");
                        continue;
                    }

                    if (replacementsFind(*srcp) != replacements.end()) {
                        DEBUG("> Can't replace, source has pending replacement");
                        continue;
                    }
                } else {
                    // Not a use
                    continue;
                }
                bool srcIsLvalue = e.src.is_Use();
                DEBUG("- Locate usage");

                auto isLvalueUsage = [&](const auto& lv, auto) {
                    return lv.root == e.dst.root;
                    //return lv == e.dst;
                };

                // Eligable for replacement
                // Find where this value is used
                // - Stop on a conditional block terminator
                // - Stop if any value mentioned in the source is mutated/invalidated
                bool stop = false;
                bool found = false;
                for (unsigned int si2 = stmtIdx + 1; si2 < block.statements.size(); si2++) {
                    state.setCurStmt(&block - &fcn.blocks.front(), si2);
                    const auto& stmt2 = block.statements[si2];
                    DEBUG(state << "[find usage] " << stmt2);

                    // Check for invalidation (done first, to avoid cases where the source is moved into a struct)
                    if (checkInvalidatesLvalue(stmt2, e.src.as_Use(), false)) {
                        stop = true;
                        DEBUG("Source invalidated");
                        break;
                    }

                    // Usage found.
                    if (visitMirLvalues(stmt2, isLvalueUsage)) {
                        // If the source isn't a Use, ensure that this is a Use
                        if (!srcIsLvalue) {
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

                    if (onlyOne) {
                        stop = true;
                    }
                }
                if (!stop) {
                    if (checkInvalidatesLvalue(block.terminator, e.src.as_Use(), false)) {
                        stop = true;
                        DEBUG("Source invalidated in terminator");
                    }
                }
                if (!stop) {
                    state.setCurStmtTerm(&block - &fcn.blocks.front());
                    DEBUG(state << "[find usage] " << block.terminator);
                    if (srcIsLvalue) {
                        visitMirLvalues(block.terminator, [&](const auto& lv, auto vu) {
                            found |= isLvalueUsage(lv, vu);
                            return found;
                        });
                    }
                    TU_MATCHA((block.terminator), (e),
                        (Incomplete, ), (Return, ), (UnwindResume, ), (UnwindTerminate, ), (Unreachable, ),
                        (Goto, DEBUG("TODO: Chain");),
                        (If, stop = true;), (Switch, stop = true;), (SwitchValue, stop = true;),
                        (Drop, stop = true;), (Call, stop = true;))
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
            unsigned int innerReplacedCount = 0;
            for (auto& r : replacements) {
                visitMirLvaluesMut(r.second, [&](::MIR::LValue& lv, auto vu) {
                    if (vu == ValUsage::Read || vu == ValUsage::Move) {
                        visitMirLvalueMut(lv, vu, [&](::MIR::LValue::MRef& lvr, auto vu) {
                            auto it = replacementsFind(lvr);
                            if (it != replacements.end() && it->second.is_Use()) {
                                lvr.replace(it->second.as_Use().clone());
                                innerReplacedCount++;
                            }
                            return false;
                        });
                    }
                    return false;
                });
            }
            if (innerReplacedCount == 0) {
                break;
            }
        }
        DEBUG("replacements = " << replacements);

        // Apply replacements
        unsigned int replaced = 0;
        while (replaced < replacements.size()) {
            auto oldReplaced = replaced;
            auto cb = [&](::MIR::LValue& lv, auto vu) {
                return visitMirLvalueMut(lv, vu, [&](::MIR::LValue::MRef& lv, auto vu) {
                    if (vu == ValUsage::Read || vu == ValUsage::Move) {
                        auto it = replacementsFind(lv);
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
            for (unsigned int blockIdx = 0; blockIdx < fcn.blocks.size(); blockIdx++) {
                auto& block = fcn.blocks[blockIdx];
                if (block.terminator.tag() == ::MIR::Terminator::TAGDEAD) {
                    continue;
                }
                for (auto& stmt : block.statements) {
                    state.setCurStmt(blockIdx, (&stmt - &block.statements.front()));
                    DEBUG(state << stmt);
                    {
                        visitMirLvaluesMut(stmt, cb);
                    }
                }
                state.setCurStmtTerm(blockIdx);
                visitMirLvaluesMut(block.terminator, cb);
            }
            MIR_ASSERT(state, replaced > oldReplaced, "Temporary eliminations didn't advance");
        }
        // Remove assignments of replaced values
        for (auto& block : fcn.blocks) {
            for (auto it = block.statements.begin(); it != block.statements.end();) {
                state.setCurStmt(&block - &fcn.blocks.front(), (it - block.statements.begin()));
                // If the statement was an assign of a replaced temporary, remove it.
                auto it2 = replacements.end();
                if (it->is_Assign() && (it2 = replacementsFind(it->as_Assign().dst)) != replacements.end()) {
                    DEBUG(state << "Delete " << *it);
                    it = block.statements.erase(it);
                } else {
                    MIR_ASSERT(state, !(it->is_Assign() && it->as_Assign().src.tag() == ::MIR::RValue::TAGDEAD), "");
                    ++it;
                }
            }
        }
        replacementHappend = (replaced > 0);
    }
    // --- Eliminate `... = Use(tmp)` (propagate lvalues upwards)
    {
        DEBUG("- Move upwards");
        for (auto& block : fcn.blocks) {
            for (auto it = block.statements.begin(); it != block.statements.end(); ++it) {
                state.setCurStmt(&block - &fcn.blocks.front(), it - block.statements.begin());
                if (!it->is_Assign()) {
                    continue;
                }
                if (it->as_Assign().src.tag() == ::MIR::RValue::TAGDEAD) {
                    continue;
                }
                auto& toReplaceLval = it->as_Assign().dst;
                if (toReplaceLval.is_Local()) {
                    const auto& vu = valUses.localUses[toReplaceLval.as_Local()];
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
                    if (it2->as_Assign().src.as_Use() != toReplaceLval) {
                        continue;
                    }
                    const auto& newDstLval = it2->as_Assign().dst;
                    // `... = Use(to_replace_lval)`

                    // TODO: Ensure that the target isn't borrowed.
                    if (newDstLval.is_Local()) {
                        const auto& vu = valUses.localUses[newDstLval.as_Local()];
                        if (!(vu.read == 1 && vu.write == 1 && vu.borrow == 0)) {
                            break;
                        }
                    } else if (newDstLval.is_Return()) {
                        // Return, can't be borrowed?
                    } else {
                        break;
                    }

                    // Ensure that the target doesn't change in the intervening time.
                    bool wasInvalidated = false;
                    for (auto it3 = it + 1; it3 != it2; it3++) {
                        // Closure returns `true` if the passed lvalue is a component of `new_dst_lval`
                        auto isLvalueInVal = [&](const auto& lv) {
                            // Don't care about indexing?
                            return lv.root == newDstLval.root;
                        };
                        if (visitMirLvalues(*it3, [&](const auto& lv, auto) {
                            return isLvalueInVal(lv);
                        })) {
                            wasInvalidated = true;
                            break;
                        }
                    }

                    // Replacement is valid.
                    if (!wasInvalidated) {
                        DEBUG(state << "Replace assignment of " << toReplaceLval << " with " << newDstLval);
                        it->as_Assign().dst = mv$(it2->as_Assign().dst);
                        block.statements.erase(it2);
                        replacementHappend = true;
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
                if (!e.retVal.is_Local()) {
                    continue;
                }
                const auto& vu = valUses.localUses[e.retVal.as_Local()];
                if (!(vu.read == 1 && vu.write == 1 && vu.borrow == 0)) {
                    continue;
                }

                // Iterate the target block, looking for where this value is used.
                const ::MIR::LValue* newDst = nullptr;
                auto& blk2 = fcn.blocks.at(e.retBlock);
                for (const auto& stmt : blk2.statements) {
                    // Find `RValue::Use( this_lvalue )`
                    if (stmt.is_Assign() && stmt.as_Assign().src.is_Use() && stmt.as_Assign().src.as_Use() == e.retVal) {
                        newDst = &stmt.as_Assign().dst;
                        break;
                    }
                }

                // Ensure that the new destination value isn't used before assignment
                if (newDst) {
                    auto lvalueImpactsDst = [&](const ::MIR::LValue& lv) -> bool {
                        // Returns true if the two lvalues share a common root
                        // TODO: Could restrict based on the presence of deref/field accesses?
                        // If `lv` is a local AND matches the index in `new_dst`, check for indexing
                        if (lv.is_Local()) {
                            for (const auto& w : newDst->wrappers) {
                                if (w.is_Index() && w.as_Index() == lv.as_Local()) {
                                    return true;
                                }
                            }
                        }
                        return lv.root == newDst->root;
                    };
                    for (auto it = blk2.statements.begin(); it != blk2.statements.end(); ++it) {
                        state.setCurStmt(&blk2 - &fcn.blocks.front(), it - blk2.statements.begin());
                        const auto& stmt = *it;
                        if (stmt.is_Assign() && stmt.as_Assign().src.is_Use() && stmt.as_Assign().src.as_Use() == e.retVal) {
                            DEBUG(state << "- Replace function return " << e.retVal << " with " << *newDst);
                            e.retVal = newDst->clone();
                            // TODO: Invalidate the entry, instead of deleting?
                            it = blk2.statements.erase(it);
                            replacementHappend = true;
                            break;
                        }
                        if (visitMirLvalues(stmt, [&](const MIR::LValue& lv, ValUsage vu) {
                            return lv == *newDst || (vu == ValUsage::Write && lvalueImpactsDst(lv));
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
                state.setCurStmt(&block - &fcn.blocks.front(), it - block.statements.begin());
                if (const auto& se = it->opt_Assign()) {
                    // Remove No-op assignments (assignment from a lvalue to itself)
                    if (const auto* srcE = se->src.opt_Use()) {
                        if (se->dst == *srcE) {
                            DEBUG(state << se->dst << " set to itself, removing write");
                            it = block.statements.erase(it) - 1;
                            continue;
                        }
                    }

                    // Remove assignments of locals that are never read
                    if (se->dst.is_Local()) {
                        const auto& vu = valUses.localUses[se->dst.as_Local()];
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

    return replacementHappend;
}

// ----------------------------------------
// Clear all drop flags that are never read
// ----------------------------------------
bool MIROptimiseDeadDropFlags(::MIR::TypeResolve& state, ::MIR::Function& fcn) {
    bool removedStatement = false;
    TRACE_FUNCTION_FR("", removedStatement);
    ::std::vector<bool> usedDropFlags(fcn.dropFlags.size());
    {
        ::std::vector<bool> readDropFlags(fcn.dropFlags.size());
        visitBlocks(state, fcn, [&readDropFlags, &usedDropFlags](auto, const ::MIR::BasicBlock& block) {
            for (const auto& stmt : block.statements) {
                if (const auto* e = stmt.opt_SetDropFlag()) {
                    if (e->other != ~0u) {
                        readDropFlags[e->other] = true;
                        usedDropFlags[e->other] = true;
                    }
                    usedDropFlags[e->idx] = true;
                } else if (const auto* e = stmt.opt_SaveDropFlag()) {
                    readDropFlags[e->idx] = true;
                    usedDropFlags[e->idx] = true;
                } else if (const auto* e = stmt.opt_LoadDropFlag()) {
                    usedDropFlags[e->idx] = true;
                }
            }
            if (const auto* e = block.terminator.opt_Drop()) {
                if (e->flagIdx != ~0u) {
                    readDropFlags[e->flagIdx] = true;
                    usedDropFlags[e->flagIdx] = true;
                }
            }
            if (const auto* e = block.terminator.opt_Switch()) {
                if (e->validFlag != ~0u) {
                    readDropFlags[e->validFlag] = true;
                    usedDropFlags[e->validFlag] = true;
                }
            }
        });
        DEBUG("Un-read drop flags:" << FMT_CB(ss, for (size_t i = 0; i < readDropFlags.size(); i++) if (!readDropFlags[i] && usedDropFlags[i]) ss << " " << i;));
        visitBlocksMut(state, fcn, [&readDropFlags, &removedStatement](auto _id, auto& block) {
            for (auto it = block.statements.begin(); it != block.statements.end();) {
                if (it->is_SetDropFlag() && !readDropFlags[it->as_SetDropFlag().idx]) {
                    removedStatement = true;
                    it = block.statements.erase(it);
                } else if (it->is_LoadDropFlag() && !readDropFlags[it->as_LoadDropFlag().idx]) {
                    removedStatement = true;
                    it = block.statements.erase(it);
                } else {
                    ++it;
                }
            }
        });
    }

    // Find any drop flags that are never assigned with a value other than their default, then remove those dead assignments.
    {
        ::std::vector<bool> editedDropFlags(fcn.dropFlags.size());
        visitBlocks(state, fcn, [&editedDropFlags, &fcn](auto, const ::MIR::BasicBlock& block) {
            for (const auto& stmt : block.statements) {
                if (const auto* e = stmt.opt_SetDropFlag()) {
                    if (e->other != ~0u) {
                        // If the drop flag is set based on another, assume it's changed
                        editedDropFlags[e->idx] = true;
                    } else if (e->newVal != fcn.dropFlags[e->idx]) {
                        // If the new value is not the default, it's changed
                        editedDropFlags[e->idx] = true;
                    } else {
                        // Set to the default, doesn't change the 'edited' state
                    }
                }
            }
        });
        DEBUG("Un-edited drop flags:" << FMT_CB(ss, for (size_t i = 0; i < editedDropFlags.size(); i++) if (!editedDropFlags[i] && usedDropFlags[i]) ss << " " << i;));
        visitBlocksMut(state, fcn, [&editedDropFlags, &removedStatement, &fcn](auto _id, auto& block) {
            for (auto it = block.statements.begin(); it != block.statements.end();) {
                // If this is a SetDropFlag and the target flag isn't edited, remove
                if (const auto* e = it->opt_SetDropFlag()) {
                    if (!editedDropFlags[e->idx]) {
                        assert(e->newVal == fcn.dropFlags[e->idx]);
                        removedStatement = true;
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

    return removedStatement;
}

// --------------------------------------------------------------------
// Remove unread assignments of locals (and replaced assignments of anything?)
// --------------------------------------------------------------------
bool MIROptimiseDeadAssignments(::MIR::TypeResolve& state, ::MIR::Function& fcn) {
    bool changed = false;
    TRACE_FUNCTION_FR("", changed);

    // Find any locals that are never read, and delete their assignments.

    // Per-local flag indicating that the particular local is read.
    ::std::vector<bool> readLocals(fcn.locals.size());
    ::std::vector<bool> droppedLocals(fcn.locals.size());
    for (const auto& bb : fcn.blocks) {
        auto cb = [&](const ::MIR::LValue& lv, ValUsage vu) {
            if (lv.root.is_Local()) {
                readLocals[lv.root.as_Local()] = true;
            }
            for (const auto& w : lv.wrappers) {
                if (w.is_Index()) {
                    readLocals[w.as_Index()] = true;
                }
            }
            return false;
        };
        for (const auto& stmt : bb.statements) {
            // If the assignment is to a local, then just consider the source (the target is writing to a local)
            if (stmt.is_Assign() && stmt.as_Assign().dst.is_Local()) {
                visitMirLvalues(stmt.as_Assign().src, cb);
            }
            // For other statment types (e.g. asm) - record anything
            else {
                visitMirLvalues(stmt, cb);
            }
        }
        if (const auto* drop = bb.terminator.opt_Drop(); drop && drop->slot.is_Local()) {
            droppedLocals[drop->slot.as_Local()] = true;
        } else {
            visitMirLvalues(bb.terminator, cb);
        }
    }

    for (auto& bb : fcn.blocks) {
        for (auto it = bb.statements.begin(); it != bb.statements.end();) {
            state.setCurStmt(&bb - &fcn.blocks.front(), it - bb.statements.begin());

            // Not an assignment, ignore
            if (!(it->is_Assign() && it->as_Assign().dst.is_Local())) {
                ++it;
                continue;
            }
            auto idx = it->as_Assign().dst.as_Local();
            // Local was read, ignore it
            if (readLocals[idx]) {
                ++it;
                continue;
            }
            // If the local was dropped, then ignore IF it's not a borrow (TODO: Only if there's drop glue?)
            if (droppedLocals[idx] && !fcn.locals[idx]->is_Borrow()) {
                ++it;
                continue;
            }
            // Remove the assignment, as it's unused
            DEBUG(state << "Unread assignment, remove - " << *it);
            it = bb.statements.erase(it);
            changed = true;
        }
        if (auto* drop = bb.terminator.opt_Drop(); drop && drop->slot.is_Local()) {
            auto idx = drop->slot.as_Local();
            if (!readLocals[idx] && fcn.locals[idx]->is_Borrow()) {
                auto target = drop->target;
                DEBUG(state << "Drop of unread value, replace with Goto(bb" << target << ")");
                bb.terminator = ::MIR::Terminator::make_Goto(target);
                changed = true;
            }
        }
    }

    // Locate assignments of locals then find the next assignment or read.
    return changed;
}

// --------------------------------------------------------------------
// Eliminate no-operation assignments that may have appeared
// --------------------------------------------------------------------
bool MIROptimiseNoopRemoval(::MIR::TypeResolve& state, ::MIR::Function& fcn) {
    bool changed = false;
    TRACE_FUNCTION_FR("", changed);

    HIR::TypeRef tmpTy;
    // Remove useless operations
    for (auto& bb : fcn.blocks) {
        // Multi-statement no-ops (round-trip casts, reboorrow+cast)
        for (auto it = bb.statements.begin(); it != bb.statements.end(); ++it) {
            state.setCurStmt(&bb - fcn.blocks.data(), it - bb.statements.begin());
            // `_0 = &mut *foo`, then `_1 = _0 as *mut T` where `foo: *mut T`
            // - Note: Accepts `_0 = &*foo; _1 = _0 as T` where `foo: T`
            if (it->is_Assign() && it->as_Assign().dst.is_Local() && it->as_Assign().src.is_Borrow() && it->as_Assign().src.as_Borrow().val.is_Deref()) {
                const auto& dstLv = it->as_Assign().dst;
                auto srcLv = it->as_Assign().src.as_Borrow().val.cloneUnwrapped();
                // Find the next use of this target lvalue
                for (auto it2 = it + 1; it2 != bb.statements.end(); ++it2) {
                    // If it's a cast back to the original type, then replace with a direct assignment of the original value
                    if (it2->is_Assign() && it2->as_Assign().src.is_Cast() && it2->as_Assign().src.as_Cast().val == dstLv) {
                        const auto& dstTy = it2->as_Assign().src.as_Cast().type;
                        HIR::TypeRef tmp;
                        const auto& origTy = state.getLvalueType(tmp, srcLv);
                        if (origTy == dstTy) {
                            DEBUG(state << "Reborrow and cast back - " << *it << " and " << *it2);
                            it2->as_Assign().src = std::move(srcLv);
                            break;
                        }
                    }
                    if (checkInvalidatesLvalue(*it2, srcLv, false)) {
                        break;
                    }
                }
            }

            // `_0 = foo as *const T; _1 = _0 as *mut T` where `foo: *mut T`
            // - Note: Accepts `_0 = foo as *const T; _1 = _0 as U` where `foo: U`
            if (it->is_Assign() && it->as_Assign().dst.is_Local() && it->as_Assign().src.is_Cast() && it->as_Assign().src.as_Cast().type->is_Pointer()) {
                const auto& dstLv = it->as_Assign().dst;
                const auto& srcLv = it->as_Assign().src.as_Cast().val;
                // Find the next use of this target lvalue
                for (auto it2 = it + 1; it2 != bb.statements.end(); ++it2) {
                    // If it's a cast back to the original type, then replace with a direct assignment of the original value
                    if (it2->is_Assign() && it2->as_Assign().src.is_Cast() && it2->as_Assign().src.as_Cast().val == dstLv) {
                        const auto& dstTy = it2->as_Assign().src.as_Cast().type;
                        HIR::TypeRef tmp;
                        const auto& origTy = state.getLvalueType(tmp, srcLv);
                        if (origTy == dstTy) {
                            DEBUG(state << "Round-trip pointer cast - " << *it << " and " << *it2);
                            it2->as_Assign().src = srcLv.clone();
                            break;
                        }
                    }
                    if (checkInvalidatesLvalue(*it2, srcLv, false)) {
                        break;
                    }
                }
            }
        }

        for (auto it = bb.statements.begin(); it != bb.statements.end();) {
            state.setCurStmt(&bb - fcn.blocks.data(), it - bb.statements.begin());

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
            if (it->is_Assign() && it->as_Assign().src.is_Borrow() && it->as_Assign().src.as_Borrow().val.is_Deref() && it->as_Assign().src.as_Borrow().val.cloneUnwrapped() == it->as_Assign().dst) {
                DEBUG(state << "Useless assignment (v = &*v), remove - " << *it);
                it = bb.statements.erase(it);
                changed = true;

                continue;
            }

            // Cast to the same type
            if (it->is_Assign() && it->as_Assign().src.is_Cast() && it->as_Assign().src.as_Cast().type == state.getLvalueType(tmpTy, it->as_Assign().src.as_Cast().val)) {
                DEBUG(state << "No-op cast, replace with assignment - " << *it);
                auto v = mv$(it->as_Assign().src.as_Cast().val);
                it->as_Assign().src = MIR::RValue::make_Use({mv$(v)});
                changed = true;

                ++it;
                continue;
            }

            ++it;
        }
        state.setCurStmtTerm(&bb - fcn.blocks.data());
        if (auto* drop = bb.terminator.opt_Drop(); drop && state.lvalueIsCopy(drop->slot)) {
            auto target = drop->target;
            DEBUG(state << "Drop of Copy type, replace with Goto(bb" << target << ")");
            bb.terminator = ::MIR::Terminator::make_Goto(target);
            changed = true;
        }
    }

    return changed;
}

// --------------------------------------------------------------------
// If the first statement of a block is an assignment from a local, and all sources of that block assign to that local
// - Move the assigment backwards
// --------------------------------------------------------------------
bool MIROptimiseGotoAssign(::MIR::TypeResolve& state, ::MIR::Function& fcn) {
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
    for (auto& dstBb : fcn.blocks) {
        if (dstBb.statements.empty()) {
            continue;
        }
        auto bbIdx = &dstBb - fcn.blocks.data();
        state.setCurStmt(bbIdx, 0);
        auto& stmt = dstBb.statements[0];
        if (!stmt.is_Assign()) {
            continue;
        }
        if (!stmt.as_Assign().src.is_Use()) {
            continue;
        }
        auto& dst = stmt.as_Assign().dst;
        auto& src = stmt.as_Assign().src.as_Use();

        if (!dst.wrappers.empty() || dst.root.is_Static()) {
            continue;
        }
        if (!src.is_Local()) {
            continue;
        }
        // Source must be a single-read local (so this assignment can be deleted)
        unsigned nRead = 0;
        unsigned nBorrow = 0;
        visitMirLvalues(state, fcn, [&](const auto& lv, auto vu) {
            if (lv.root == src.root) {
                switch (vu) {
                    case ValUsage::Read:
                    case ValUsage::Move:
                        nRead++;
                        break;
                    case ValUsage::Borrow:
                        nBorrow++;
                        break;
                    case ValUsage::Write:
                        // Don't care
                        break;
                }
            }
            for (const auto& w : lv.wrappers) {
                if (w.is_Index()) {
                    if (::MIR::LValue::newLocal(w.as_Index()) == src) {
                        nRead++;
                    }
                }
            }
            return true;
        });
        state.setCurStmt(bbIdx, 0);
        if (nRead > 1 || nBorrow > 0) {
            DEBUG(state << "Source " << src << " is read " << nRead << " times and borrowed " << nBorrow);
            continue;
        }
        DEBUG(state << "Eligible assignment (" << stmt << ")");

        // Find source blocks, check terminators/last
        std::vector<unsigned> sources;
        unsigned numUsed = 0;
        for (const auto& srcBb : fcn.blocks) {
            unsigned bbIdx = &srcBb - fcn.blocks.data();
            bool used = false;
            visitTerminatorTarget(srcBb.terminator, [&](const auto& tgt) {
                if (tgt == state.getCurBlock()) {
                    used = true;
                    sources.push_back(bbIdx);
                }
            });
            if (used) {
                TU_MATCH_HDRA( (srcBb.terminator), { )
                TU_ARMA(Goto, e) {
                        if (srcBb.statements.empty()) {
                            DEBUG(state << "BB" << bbIdx << " empty");
                        } else if (TU_TEST1(srcBb.statements.back(), Assign, .dst == src)) {
                            DEBUG("BB" << bbIdx << "/" << srcBb.statements.size() << " " << srcBb.statements.back());
                            numUsed += 1;
                        } else {
                            DEBUG("BB" << bbIdx << "/" << srcBb.statements.size() << " " << srcBb.statements.back() << " - Doesn't write");
                        }
                    }
                    TU_ARMA(Call, e) {
                        if (e.retBlock != state.getCurBlock()) {
                            DEBUG(state << "BB" << bbIdx << "/TERM " << srcBb.terminator << " - Not return block");
                        } else if (e.retVal != src) {
                            DEBUG(state << "BB" << bbIdx << "/TERM " << srcBb.terminator << " - Doesn't write to source");
                        } else {
                            numUsed += 1;
                        }
                    }
                    break;
                    default:
                        DEBUG(state << "BB" << bbIdx << "/TERM " << srcBb.terminator << " - Wrong terminator type");
                        break;
                }
            }
        }

        // TODO: Allow if one arm doesn't update?
        // - What if a call invalidates the target?
        if (numUsed < sources.size()) {
            DEBUG(state << "- Not all sources set the value");
            continue;
        }

        changed = true;

        // Time to edit.
        // 1. Update all sources
        for (auto bbIdx : sources) {
            auto& srcBb = fcn.blocks[bbIdx];

            if (TU_TEST1(srcBb.terminator, Call, .retVal == src)) {
                DEBUG("- Source block: BB" << bbIdx << " - term " << srcBb.terminator);
                srcBb.terminator.as_Call().retVal = dst.clone();
            } else if (!srcBb.statements.empty() && TU_TEST1(srcBb.statements.back(), Assign, .dst == src)) {
                DEBUG("- Source block: BB" << bbIdx << " - tail " << srcBb.statements.back());
                srcBb.statements.back().as_Assign().dst = dst.clone();
            } else {
                MIR_TODO(state, "Handle copying assignment to source");
            }
            if (!srcBb.statements.empty()) {
                DEBUG("+- BB" << bbIdx << "/" << (srcBb.statements.size() - 1) << " " << srcBb.statements.back());
            }
            DEBUG("+- BB" << bbIdx << "/TERM " << srcBb.terminator);
        }
        // IF the value is `Copy` (i.e. the initial assignment could be expected to survive), then reverse the destination
        // - Can't do this, it's going to cause infinite recursion!
        if (false && state.lvalueIsCopy(dst)) {
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
bool MIROptimiseUselessReborrows(::MIR::TypeResolve& state, ::MIR::Function& fcn) {
    bool changed = false;
    TRACE_FUNCTION_FR("", changed);

    // TODO: This doesn't work if the assignment happens in a loop (can lead to multiple moves)
    // - Need to have a way of knowing if a block is a loop member

    return changed;
}

// --------------------------------------------------------------------
// Clear all unused blocks
// --------------------------------------------------------------------
bool MIROptimiseGarbageCollectPartial(::MIR::TypeResolve& state, ::MIR::Function& fcn) {
    bool rv = false;
    TRACE_FUNCTION_FR("", rv);
    ::std::vector<bool> visited(fcn.blocks.size());
    visitBlocks(state, fcn, [&visited](auto bb, const auto& /*block*/) {
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
bool MIROptimiseGarbageCollect(::MIR::TypeResolve& state, ::MIR::Function& fcn) {
    ::std::vector<bool> usedLocals(fcn.locals.size());
    ::std::vector<bool> usedDfs(fcn.dropFlags.size());
    ::std::vector<bool> visited(fcn.blocks.size());

    visitBlocks(state, fcn, [&](auto bb, const auto& block) {
        visited[bb] = true;

        auto assignedLval = [&](const ::MIR::LValue& lv) {
            // TODO: Consume through indexing/field accesses
            for (const auto& w : lv.wrappers) {
                if (w.is_Field()) {
                } else {
                    return;
                }
            }
            if (lv.root.is_Local()) {
                usedLocals[lv.root.as_Local()] = true;
            }
        };

        for (const auto& stmt : block.statements) {
            TU_IFLET(::MIR::Statement, stmt, Assign, e, assignedLval(e.dst);)
            //else if( const auto* e = stmt.opt_Drop() )
            //{
            //    //if( e->flag_idx != ~0u )
            //    //    used_dfs.at(e->flag_idx) = true;
            //}
            else if (const auto* e = stmt.opt_Asm()) {
                for (const auto& val : e->outputs) {
                    assignedLval(val.second);
                }
            }
            else if (const auto* e = stmt.opt_Asm2()) {
                for (const auto& p : e->params) {
                    if (p.is_Reg() && p.as_Reg().output) {
                        assignedLval(*p.as_Reg().output);
                    }
                }
            }
            else if (const auto* e = stmt.opt_SetDropFlag()) {
                if (e->other != ~0u) {
                    usedDfs.at(e->other) = true;
                }
                usedDfs.at(e->idx) = true;
            }
            else if (const auto* e = stmt.opt_LoadDropFlag()) {
                usedDfs.at(e->idx) = true;
            }
        }

        if (const auto* te = block.terminator.opt_Call()) {
            assignedLval(te->retVal);
        } else if (const auto* te = block.terminator.opt_Drop()) {
            if (te->flagIdx != ~0u) {
                usedDfs.at(te->flagIdx) = true;
            }
        } else if (const auto* te = block.terminator.opt_Switch()) {
            if (te->validFlag != ~0u) {
                usedDfs.at(te->validFlag) = true;
            }
        }
    });

    ::std::vector<unsigned int> localRewriteTable;
    unsigned int nLocals = fcn.locals.size();
    for (unsigned int i = 0, j = 0; i < nLocals; i++) {
        if (!usedLocals[i]) {
            fcn.locals.erase(fcn.locals.begin() + j);
        } else {
            DEBUG("_" << i << " => _" << j);
        }
        localRewriteTable.push_back(usedLocals[i] ? j++ : ~0u);
    }
    DEBUG("Deleted Locals:" << FMT_CB(ss, for (auto run : runs(usedLocals)) if (!usedLocals[run.first]) {
              ss << " " << run.first;
              if (run.second != run.first) {
                  ss << "-" << run.second;
              }
          }));
    ::std::vector<unsigned int> dfRewriteTable;
    unsigned int nDf = fcn.dropFlags.size();
    for (unsigned int i = 0, j = 0; i < nDf; i++) {
        if (!usedDfs[i]) {
            DEBUG("GC df" << i);
            // NOTE: Not erased until after rewriting
        }
        dfRewriteTable.push_back(usedDfs[i] ? j++ : ~0u);
    }

    auto it = fcn.blocks.begin();
    for (unsigned int i = 0; i < visited.size(); i++) {
        if (visited[i]) {
            auto lvalueCb = [&](::MIR::LValue& lv, auto) {
                if (lv.root.is_Local()) {
                    auto e = lv.root.as_Local();
                    MIR_ASSERT(state, e < localRewriteTable.size(), "Variable out of range - " << lv);
                    // If the table entry for this variable is !0, it wasn't marked as used
                    MIR_ASSERT(state, localRewriteTable.at(e) != ~0u, "LValue " << lv << " incorrectly marked as unused");
                    lv.root = ::MIR::LValue::Storage::newLocal(localRewriteTable.at(e));
                }
                for (auto& w : lv.wrappers) {
                    if (w.is_Index()) {
                        w = ::MIR::LValue::Wrapper::newIndex(localRewriteTable.at(w.as_Index()));
                    }
                }
                return false;
            };
            ::std::vector<bool> toRemoveStatements(it->statements.size());
            for (auto& stmt : it->statements) {
                auto stmtIdx = &stmt - &it->statements.front();
                state.setCurStmt(i, stmtIdx);

                if (stmt == ::MIR::Statement()) {
                    DEBUG(state << "Remove " << stmt << " - Pure default");
                    toRemoveStatements[stmtIdx] = true;
                    continue;
                }

                visitMirLvaluesMut(stmt, lvalueCb);
                if (auto* se = stmt.opt_SetDropFlag()) {
                    // Rewrite drop flag indexes OR delete
                    if (dfRewriteTable[se->idx] == ~0u) {
                        toRemoveStatements[stmtIdx] = true;
                        continue;
                    }
                    se->idx = dfRewriteTable[se->idx];
                    if (se->other != ~0u) {
                        se->other = dfRewriteTable[se->other];
                    }
                } else if (auto* se = stmt.opt_LoadDropFlag()) {
                    se->idx = dfRewriteTable[se->idx];
                } else if (auto* se = stmt.opt_SaveDropFlag()) {
                    se->idx = dfRewriteTable[se->idx];
                } else if (auto* se = stmt.opt_ScopeEnd()) {
                    for (auto it = se->slots.begin(); it != se->slots.end();) {
                        if (localRewriteTable.at(*it) == ~0u) {
                            it = se->slots.erase(it);
                        } else {
                            *it = localRewriteTable.at(*it);
                            ++it;
                        }
                    }

                    if (se->slots.empty()) {
                        DEBUG(state << "Delete ScopeEnd (now empty)");
                        toRemoveStatements[stmtIdx] = true;
                        continue;
                    }
                }
            }
            state.setCurStmtTerm(i);
            if (auto* drop = it->terminator.opt_Drop()) {
                bool removeDrop = false;
                if (drop->flagIdx != ~0u && dfRewriteTable[drop->flagIdx] == ~0u) {
                    if (fcn.dropFlags.at(drop->flagIdx)) {
                        drop->flagIdx = ~0u;
                    } else {
                        removeDrop = true;
                    }
                }
                if (drop->slot.is_Local() && localRewriteTable[drop->slot.as_Local()] == ~0u) {
                    removeDrop = true;
                }
                if (removeDrop) {
                    auto target = drop->target;
                    it->terminator = ::MIR::Terminator::make_Goto(target);
                }
            }
            visitMirLvaluesMut(it->terminator, lvalueCb);
            if (auto* drop = it->terminator.opt_Drop()) {
                if (drop->flagIdx != ~0u) {
                    drop->flagIdx = dfRewriteTable[drop->flagIdx];
                }
            } else if (auto* sw = it->terminator.opt_Switch()) {
                if (sw->validFlag != ~0u) {
                    sw->validFlag = dfRewriteTable[sw->validFlag];
                }
            }

            // Delete all statements flagged in a bitmap for deletion
            assert(it->statements.size() == toRemoveStatements.size());
            auto newEnd = ::std::remove_if(it->statements.begin(), it->statements.end(), [&](const auto& s) {
                size_t stmtIdx = (&s - &it->statements.front());
                return toRemoveStatements[stmtIdx];
            });
            it->statements.erase(newEnd, it->statements.end());
        }
        ++it;
    }

    // Removing a Drop terminator also removes its unwind edge.  Recompute block
    // reachability after all such rewrites, otherwise the detached cleanup
    // subgraph is retained and can be sorted ahead of the real entry block.
    visited.assign(fcn.blocks.size(), false);
    visitBlocks(state, fcn, [&](auto bb, const auto&) {
        visited[bb] = true;
    });

    ::std::vector<unsigned int> blockRewriteTable;
    for (unsigned int i = 0, j = 0; i < fcn.blocks.size(); i++) {
        blockRewriteTable.push_back(visited[i] ? j++ : ~0u);
    }
    for (unsigned int i = 0; i < fcn.blocks.size(); i++) {
        if (!visited[i]) {
            continue;
        }
        visitTerminatorTargetMut(fcn.blocks[i].terminator, [&](auto& target) {
            MIR_ASSERT(state, target < blockRewriteTable.size(), "Block target out of range - bb" << target);
            MIR_ASSERT(state, blockRewriteTable[target] != ~0u, "Reachable block targets unreachable bb" << target);
            target = blockRewriteTable[target];
        });
    }

    auto newBlocksEnd = ::std::remove_if(fcn.blocks.begin(), fcn.blocks.end(), [&](const auto& bb) {
        size_t i = &bb - &fcn.blocks.front();
        if (!visited[i]) {
            DEBUG("GC bb" << i);
        }
        return !visited[i];
    });
    fcn.blocks.erase(newBlocksEnd, fcn.blocks.end());

    // Drop flags use vector<bool> proxy storage, so erase them by original and compacted index.
    for (unsigned int i = 0, j = 0; i < nDf; i++) {
        if (!usedDfs[i]) {
            fcn.dropFlags.erase(fcn.dropFlags.begin() + j);
        } else {
            j++;
        }
    }

    // TODO: Detect if any optimisations happened, and return true in that case
    return false;
}


/// Sort basic blocks to approximate program flow (helps when reading MIR)
void MIRSortBlocks(const StaticTraitResolve& resolve, const ::HIR::ItemPath& path, ::MIR::Function& fcn) {
    ::std::vector<bool> visited(fcn.blocks.size());
    ::std::vector<::std::pair<unsigned, unsigned>> depths(fcn.blocks.size());

    struct Todo {
        size_t bbIdx;
        unsigned branchCount;
        unsigned level;
    };

    unsigned int branches = 0;
    ::std::vector<Todo> todo;
    todo.push_back(Todo{0, 0, 0});

    while (!todo.empty()) {
        auto info = todo.back();
        todo.pop_back();
        if (visited[info.bbIdx]) {
            continue;
        }

        visited[info.bbIdx] = true;
        depths[info.bbIdx] = ::std::make_pair(info.branchCount, info.level);
        const auto& bb = fcn.blocks[info.bbIdx];

        TU_MATCHA((bb.terminator), (te),
            (Incomplete, ), (Return, ), (UnwindResume, ), (UnwindTerminate, ), (Unreachable, ),
            (Goto, todo.push_back(Todo{te, info.branchCount, info.level + 1});),
            (If, todo.push_back(Todo{te.bbTrue, ++branches, info.level + 1}); todo.push_back(Todo{te.bbFalse, ++branches, info.level + 1});),
            (Switch, for (auto dst : te.targets) todo.push_back(Todo{dst, ++branches, info.level + 1}); if (te.validFlag != ~0u) todo.push_back(Todo{te.invalidTarget, ++branches, info.level + 1});),
            (SwitchValue, for (auto dst : te.targets) todo.push_back(Todo{dst, ++branches, info.level + 1}); todo.push_back(Todo{te.defTarget, info.branchCount, info.level + 1});),
            (Drop, todo.push_back(Todo{te.target, info.branchCount, info.level + 1}); TU_IFLET(::MIR::UnwindAction, te.unwind, Cleanup, target, todo.push_back(Todo{target, ++branches, info.level + 1});)),
            (Call, todo.push_back(Todo{te.retBlock, info.branchCount, info.level + 1}); TU_IFLET(::MIR::UnwindAction, te.unwind, Cleanup, target, todo.push_back(Todo{target, ++branches, info.level + 1});)))
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

    decltype(fcn.blocks) newBlockList;
    newBlockList.reserve(fcn.blocks.size());
    for (auto idx : idxes) {
        auto fixBbIdx = [&](auto idx) {
            return ::std::find(idxes.begin(), idxes.end(), idx) - idxes.begin();
        };
        newBlockList.push_back(mv$(fcn.blocks[idx]));
        newBlockList.back().statements.shrink_to_fit(); // Save some memory
        visitTerminatorTargetMut(newBlockList.back().terminator, [&](auto& te) {
            te = fixBbIdx(te);
        });
    }
    fcn.blocks = mv$(newBlockList);
}

void MIROptimiseCrate(::HIR::Crate& crate, unsigned optLevel, bool enableInlining) {
    ::MIR::OuterVisitor ov{crate, [optLevel, enableInlining](const auto& res, const auto& p, auto& expr, const auto& args, const auto& ty) {
        //if( ! cast<::HIR::ExprNodeBlock>(expr.get()) ) {
        //    return ;
        //}
        auto& mir = expr.getMirOrErrorMut(Span());
        if (optLevel == 0) {
            MIROptimiseMin(res, p, mir, args, ty);
        } else {
            // The crate driver validates after this optimisation and its final cleanup.
            // Preserve explicitly requested diagnostic checks inside the optimiser.
            MIROptimise(res, p, mir, args, ty, optLevel, enableInlining, /*validate=*/getenv("MRUSTC_MIR_CHECK") != nullptr);
        }
        // Run cleanup to handle now-monomoprhised inlined constants
        MIRCleanup(res, p, mir, args, ty);
    }};
    ov.visitCrate(crate);
}

void MIROptimiseCrateInlining(const ::HIR::Crate& crate, TransList& list, bool postSave, unsigned optLevel, bool enableInlining) {
    TRACE_FUNCTION;

    ::StaticTraitResolve resolve{crate};

    // If running after HIR has been serialised, we can eliminate calls to `const_eval_select` without
    // impacting constant evaluation in downstream crates
    if (postSave) {
        // Visit every function in the monomorph list and raplce `const_eval_select` calls with calls to the runtime function
        for (auto& fcnEnt : list.functions) {
            auto& hirFcn = *const_cast<::HIR::Function*>(fcnEnt.second->ptr);
            ::MIR::Function* fcnP;
            if (fcnEnt.second->monomorphised.code) {
                DEBUG("Generic: " << fcnEnt.first);
                fcnP = &*fcnEnt.second->monomorphised.code;
            } else if (hirFcn.mCode.mir) {
                DEBUG("Concrete: " << fcnEnt.first);
                fcnP = &hirFcn.mCode.getMirOrErrorMut(Span());
            } else {
                // Ignore, this is an external function reference.
                DEBUG("External: " << fcnEnt.first);
                continue;
            }

            auto& fcn = *fcnP;
            for (auto& block : fcn.blocks) {
                if (auto* te = block.terminator.opt_Call()) {
                    if (te->fcn.is_Intrinsic() && te->fcn.as_Intrinsic().name == "const_eval_select") {
                        size_t nArgs = te->fcn.as_Intrinsic().params.types.at(0)->as_Tuple().size();
                        const MIR::LValue arg = te->args.at(0).as_LValue().clone();
                        // Note: arg 1 is the constant function
                        const HIR::Path& fcn_path = *te->args.at(2).as_Constant().as_Function().p;

                        DEBUG(fcn_path);
                        te->fcn = fcn_path.clone();
                        te->args.clear();
                        te->args.reserve(nArgs);
                        for (size_t i = 0; i < nArgs; i++) {
                            te->args.push_back(MIR::LValue::newField(arg.clone(), i));
                        }
                    }
                }
            }
        }
    } else {
        for (const auto& fcn : list.functions) {
            DEBUG("FCN: " << fcn.first);
        }
    }

    if (!enableInlining) {
        return;
    }

    // rustc level 4 removes analysis limits. Preserve a finite cap for normal
    // level-3 inlining, while level 4+ runs this monotonic pass to its fixed point.
    const size_t maxIterations = optLevel >= 4
        ? ::std::numeric_limits<size_t>::max()
        : 5;
    size_t numIterations = 0;
    bool didInlineOnPass;
    do {
        didInlineOnPass = false;

        for (auto& fcnEnt : list.functions) {
            const auto& path = fcnEnt.first;
            //const auto& pp = fcn_ent.second->pp;
            auto& hirFcn = *const_cast<::HIR::Function*>(fcnEnt.second->ptr);
            auto& monoFcn = fcnEnt.second->monomorphised;

            ::std::string s = FMT(path);
            ::HIR::ItemPath ip(s);

            if (monoFcn.code) {
                didInlineOnPass |= MIROptimiseInline(resolve, ip, *monoFcn.code, monoFcn.argTys, monoFcn.ret_ty, list, optLevel);

                MIRCleanup(resolve, ip, *monoFcn.code, monoFcn.argTys, monoFcn.ret_ty);
            } else if (hirFcn.mCode) {
                auto& mir = hirFcn.mCode.getMirOrErrorMut(Span());
                bool didOpt = MIROptimiseInline(resolve, ip, mir, hirFcn.mArgs, hirFcn.returnType, list, optLevel);
                mir.transEnumState = ::MIR::EnumCachePtr(); // Clear MIR enum cache
                didInlineOnPass |= didOpt;

                MIRCleanup(resolve, ip, mir, hirFcn.mArgs, hirFcn.returnType);
            } else {
                // Extern, no optimisations
            }
        }
        numIterations += 1;
    } while (didInlineOnPass && numIterations < maxIterations);

    if (didInlineOnPass) {
        DEBUG("Stopped inlining after the level-specific maximum of " << maxIterations << " passes");
    }
}
