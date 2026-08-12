#pragma once

#include <vector>
#include <functional>
#include <type_traits>
#include "hir_typeck_static.h"
#include "mir_mir.h"

namespace HIR {
    class Crate;
    class TypeData;
    using TypeRef = const TypeData*;
    struct Pattern;
    struct SimplePath;
}

namespace MIR {

    class Function;
    struct LValue;
    class Constant;
    struct BasicBlock;
    class Terminator;
    class Statement;
    class RValue;
    class Param;

    typedef unsigned int BasicBlockId;

    struct CheckFailure: public ::std::exception {};

#define MIR_BUG(state, ...)                      \
    do {                                         \
        const char* __fcn = __FUNCTION__;        \
        (state).print_bug([&](auto& _os) {       \
            _os << __fcn << ": " << __VA_ARGS__; \
        });                                      \
        throw "";                                \
    } while (0)
#define MIR_ASSERT(state, cnd, ...)                                                                \
    do {                                                                                           \
        if (!(cnd))                                                                                \
            (state).print_bug([&](auto& _os) {                                                     \
                _os << __FILE__ << ":" << __LINE__ << " ASSERT " #cnd " failed - " << __VA_ARGS__; \
            });                                                                                    \
    } while (0)
#define MIR_TODO(state, ...)                                           \
    do {                                                               \
        (state).print_todo([&](auto& _os) {                            \
            _os << __FILE__ << ":" << __LINE__ << ": " << __VA_ARGS__; \
        });                                                            \
        throw "";                                                      \
    } while (0)
#define MIR_DEBUG(state, ...)                                     \
    do {                                                          \
        DEBUG(FMT_CB(_ss, (state).fmtPos(_ss);) << __VA_ARGS__); \
    } while (0)

    class TypeResolve {
    public:
        typedef ::std::vector<::std::pair<::HIR::Pattern, ::HIR::TypeRef>> argsT;

    private:
        const unsigned int STMT_TERM = ~0u;

    public:
        const Span& sp;
        const ::StaticTraitResolve& mResolve;
        const ::HIR::Crate& crate;

    private:
        ::FmtLambda mPath;

    public:
        const ::HIR::TypeData* retType;
        const argsT& mArgs;
        const ::MIR::Function& fcn;

        // If set, these override the list in `m_fcn`
        const ::HIR::TypeData* monomorphedRettype;
        const ::std::vector<::HIR::TypeRef>* monomorphedLocals;

    private:
        const ::HIR::SimplePath* mLangBox = nullptr;

        unsigned int bbIdx = 0;
        unsigned int stmt_idx = 0;

    public:
        TypeResolve(const Span& sp, const ::StaticTraitResolve& resolve, ::FmtLambda path, const ::HIR::TypeData* ret_type, const argsT& args, const ::MIR::Function& fcn);

        void set_cur_stmt(const ::MIR::BasicBlock& bb, const ::MIR::Statement& stmt);

        void set_cur_stmt(const ::MIR::BasicBlock& bb, unsigned int stmt_idx);

        void set_cur_stmt(unsigned int bbIdx, unsigned int stmt_idx);

        void set_cur_stmt_term(const ::MIR::BasicBlock& bb);

        void set_cur_stmt_term(unsigned int bbIdx);

        unsigned int getCurBlock() const {
            return bbIdx;
        }

        unsigned int getCurStmtOfs() const;

        void fmtPos(::std::ostream& os, bool include_path = false) const;

        void print_bug(::std::function<void(::std::ostream& os)> cb) const {
            print_msg("ERROR", cb);
        }

        void print_todo(::std::function<void(::std::ostream& os)> cb) const {
            print_msg("TODO", cb);
        }

        void print_msg(const char* tag, ::std::function<void(::std::ostream& os)> cb) const;

        const ::MIR::BasicBlock& getBlock(::MIR::BasicBlockId id) const;

        const ::HIR::TypeData* getStaticType(::HIR::TypeRef& tmp, const ::HIR::Path& path) const;
        const ::HIR::TypeData* getLvalueType(::HIR::TypeRef& tmp, const ::MIR::LValue& val, unsigned wrapper_skip_count = 0) const;

        const ::HIR::TypeData* getLvalueType(::HIR::TypeRef& tmp, const ::MIR::LValue::CRef& val) const {
            return getLvalueType(tmp, val.lv(), val.lv().wrappers.size() - val.wrapper_count());
        }

        const ::HIR::TypeData* getLvalueType(::HIR::TypeRef& tmp, const ::MIR::LValue::MRef& val) const {
            return getLvalueType(tmp, val.lv(), val.lv().wrappers.size() - val.wrapper_count());
        }

        const ::HIR::TypeData* getUnwrappedType(::HIR::TypeRef& tmp, const ::MIR::LValue::Wrapper& w, const ::HIR::TypeData* ty) const;
        const ::HIR::TypeData* getParamType(::HIR::TypeRef& tmp, const ::MIR::Param& val) const;

        ::HIR::TypeRef getConstType(const ::MIR::Constant& c) const;

        bool lvalue_is_copy(const ::MIR::LValue& val) const;
        const ::HIR::TypeData* is_type_owned_box(const ::HIR::TypeData* ty) const;

        /// @brief Handler for the `offset_of` intrinsic
        /// @param ty Type
        /// @param params Field names (must be Const::String)
        /// @return Offset in bytes
        size_t intrinsic_offset_of(const ::HIR::TypeData* ty, const ::std::vector<MIR::Param>& params) const;
        /// @brief Handler for the `type_name` intrinsic, strips out mrustc's helper comments
        /// @param ty Type
        /// @return Clean string form of the type
        std::string intrinsic_type_name(const ::HIR::TypeData* ty) const;

        friend ::std::ostream& operator<<(::std::ostream& os, const TypeResolve& x);
    };

    // --------------------------------------------------------------------
    // MIR_Helper_GetLifetimes
    // --------------------------------------------------------------------
    class ValueLifetime {
        ::std::vector<bool> statements;

    public:
        ValueLifetime(::std::vector<bool> stmts);

        bool valid_at(size_t ofs) const {
            return statements.at(ofs);
        }

        // true if this value is used at any point
        bool is_used() const;

        bool overlaps(const ValueLifetime& x) const;

        void unify(const ValueLifetime& x);
    };

    struct ValueLifetimes {
        ::std::vector<size_t> blockOffsets;
        ::std::vector<ValueLifetime> slots;

        bool slot_valid(unsigned idx, unsigned bbIdx, unsigned stmt_idx) const {
            return slots.at(idx).valid_at(blockOffsets[bbIdx] + stmt_idx);
        }
    };

    namespace visit {
        enum class ValUsage {
            Move,
            Read,
            Write,
            Borrow,
        };

        extern bool visit_mir_lvalue(const ::MIR::LValue& lv, ValUsage u, ::std::function<bool(const ::MIR::LValue&, ValUsage)> cb);
        extern bool visit_mir_lvalue(const ::MIR::Param& p, ValUsage u, ::std::function<bool(const ::MIR::LValue&, ValUsage)> cb);
        extern bool visit_mir_lvalues(const ::MIR::RValue& rval, ::std::function<bool(const ::MIR::LValue&, ValUsage)> cb);
        extern bool visit_mir_lvalues(const ::MIR::Statement& stmt, ::std::function<bool(const ::MIR::LValue&, ValUsage)> cb);
        extern bool visit_mir_lvalues(const ::MIR::Terminator& term, ::std::function<bool(const ::MIR::LValue&, ValUsage)> cb);

        extern void visit_terminator_target_mut(::MIR::Terminator& term, ::std::function<void(::MIR::BasicBlockId&)> cb);
        extern void visit_terminator_target(const ::MIR::Terminator& term, ::std::function<void(const ::MIR::BasicBlockId&)> cb);

        template <typename Inner>
        class DecMut {
        public:
            typedef Inner Type;
        };

        template <typename Inner>
        class DecConst {
        public:
            typedef const Inner Type;
        };

        template <template <typename> class Dec>
        class VisitorBase {
        public:
            using TypeVisitArg = ::std::conditional_t<
                ::std::is_const_v<typename Dec<int>::Type>,
                ::HIR::TypeRef,
                ::HIR::TypeRef&
            >;

            virtual void visit_type(TypeVisitArg t) {
                // NOTE: Doesn't recurse
            }

            virtual void visit_path(typename Dec<::HIR::Path>::Type& path) {
            TU_MATCH_HDRA((path.mData), {)
            TU_ARMA(Generic, e) {
                        visit_path_params(e.mParams);
                    }
                    TU_ARMA(UfcsInherent, e) {
                        visit_type(e.type);
                        visit_path_params(e.params);
                    }
                    TU_ARMA(UfcsKnown, e) {
                        visit_type(e.type);
                        visit_path_params(e.trait.mParams);
                        visit_path_params(e.params);
                    }
                    TU_ARMA(UfcsUnknown, e) {
                        visit_type(e.type);
                        visit_path_params(e.params);
                    }
            }
            }

            virtual void visit_genericpath(typename Dec<::HIR::GenericPath>::Type& p) {
                visit_path_params(p.mParams);
            }

            virtual void visit_path_params(typename Dec<::HIR::PathParams>::Type& p) {
                for (auto& e : p.types) {
                    visit_type(e);
                }
            }

            virtual bool visit_lvalue(typename Dec<::MIR::LValue>::Type& lv, ValUsage u) = 0;

            virtual bool visit_const(typename Dec<::MIR::Constant>::Type& c) {
            TU_MATCH_HDRA( (c), {)
            default:
                break;
                    TU_ARMA(ItemAddr, e) {
                        visit_path(*e);
                    }
                    TU_ARMA(Const, e) {
                        visit_path(*e.p);
                    }
            }
            return false;
            }

            virtual bool visit_param(typename Dec<::MIR::Param>::Type& p, ValUsage u) {
            TU_MATCH_HDRA( (p), {)
            TU_ARMA(LValue, e) {
                        return visit_lvalue(e, u);
                    }
                    TU_ARMA(Borrow, e) {
                        return visit_lvalue(e.val, ValUsage::Borrow);
                    }
                    TU_ARMA(Constant, e) {
                        return visit_const(e);
                    }
            }
            throw "";
            }

            virtual bool visit_rvalue(typename Dec<::MIR::RValue>::Type& rval) {
                bool rv = false;
            TU_MATCH_HDRA( (rval), {)
            TU_ARMA(Use, se) {
                        rv |= visit_lvalue(se, ValUsage::Move);
                    }
                    TU_ARMA(Constant, se) {
                        rv |= visit_const(se);
                    }
                    TU_ARMA(SizedArray, se) {
                        rv |= visit_param(se.val, ValUsage::Read);
                    }
                    TU_ARMA(Borrow, se) {
                        rv |= visit_lvalue(se.val, ValUsage::Borrow);
                    }
                    TU_ARMA(Cast, se) {
                        rv |= visit_lvalue(se.val, ValUsage::Move);
                        visit_type(se.type);
                    }
                    TU_ARMA(BinOp, se) {
                        rv |= visit_param(se.val_l, ValUsage::Read);
                        rv |= visit_param(se.val_r, ValUsage::Read);
                    }
                    TU_ARMA(UniOp, se) {
                        rv |= visit_lvalue(se.val, ValUsage::Read);
                    }
                    TU_ARMA(DstMeta, se) {
                        rv |= visit_lvalue(se.val, ValUsage::Read);
                    }
                    TU_ARMA(DstPtr, se) {
                        rv |= visit_lvalue(se.val, ValUsage::Read);
                    }
                    TU_ARMA(MakeDst, se) {
                        rv |= visit_param(se.ptr_val, ValUsage::Move);
                        if (TU_TEST2(se.meta_val, Constant, , ItemAddr, .get() == nullptr)) {
                        } else {
                            rv |= visit_param(se.meta_val, ValUsage::Move);
                        }
                    }
                    TU_ARMA(Tuple, se) {
                        for (auto& v : se.vals) {
                            rv |= visit_param(v, ValUsage::Move);
                        }
                    }
                    TU_ARMA(Array, se) {
                        for (auto& v : se.vals) {
                            rv |= visit_param(v, ValUsage::Move);
                        }
                    }
                    TU_ARMA(UnionVariant, se) {
                        visit_genericpath(se.path);
                        rv |= visit_param(se.val, ValUsage::Move);
                    }
                    TU_ARMA(EnumVariant, se) {
                        visit_genericpath(se.path);
                        for (auto& v : se.vals) {
                            rv |= visit_param(v, ValUsage::Move);
                        }
                    }
                    TU_ARMA(Struct, se) {
                        visit_genericpath(se.path);
                        for (auto& v : se.vals) {
                            rv |= visit_param(v, ValUsage::Move);
                        }
                    }
            }
            return rv;
            }

            virtual bool visit_stmt(typename Dec<::MIR::Statement>::Type& stmt) {
                bool rv = false;
            TU_MATCH_HDRA( (stmt), {)
            TU_ARMA(Assign, e) {
                        rv |= visit_rvalue(e.src);
                        rv |= visit_lvalue(e.dst, ValUsage::Write);
                    }
                    TU_ARMA(Asm, e) {
                        for (auto& v : e.inputs) {
                            rv |= visit_lvalue(v.second, ValUsage::Read);
                        }
                        for (auto& v : e.outputs) {
                            rv |= visit_lvalue(v.second, ValUsage::Write);
                        }
                    }
                    TU_ARMA(Asm2, e) {
                        for (auto& p : e.params) {
                    TU_MATCH_HDRA( (p), { )
                    TU_ARMA(Const, v)
                        rv |= visit_const(v);
                                TU_ARMA(Sym, v)
                                /*rv |= */ visit_path(v);
                                TU_ARMA(Reg, v) {
                                    if (v.input) {
                                        rv |= visit_param(*v.input, ValUsage::Read);
                                    }
                                    if (v.output) {
                                        rv |= visit_lvalue(*v.output, ValUsage::Write);
                                    }
                                }
                    }
                        }
                    }
                    TU_ARMA(SetDropFlag, e) {
                    }
                    TU_ARMA(SaveDropFlag, e) {
                        rv |= visit_lvalue(e.slot, ValUsage::Write);
                    }
                    TU_ARMA(LoadDropFlag, e) {
                        rv |= visit_lvalue(e.slot, ValUsage::Read);
                    }
                    TU_ARMA(ScopeEnd, e) {
                    }
            }
            return rv;
            }

            virtual bool visit_block_id(typename Dec<::MIR::BasicBlockId>::Type& bbId) {
                return false;
            }

            virtual bool visit_terminator(typename Dec<::MIR::Terminator>::Type& term) {
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
                        visit_block_id(e);
                    }
                    TU_ARMA(If, e) {
                        rv |= visit_lvalue(e.cond, ValUsage::Read);
                        rv |= visit_block_id(e.bbTrue);
                        rv |= visit_block_id(e.bbFalse);
                    }
                    TU_ARMA(Switch, e) {
                        rv |= visit_lvalue(e.val, ValUsage::Read);
                        for (auto& target : e.targets) {
                            rv |= visit_block_id(target);
                        }
                        if (e.valid_flag != ~0u) {
                            rv |= visit_block_id(e.invalid_target);
                        }
                    }
                    TU_ARMA(SwitchValue, e) {
                        rv |= visit_lvalue(e.val, ValUsage::Read);
                        for (auto& target : e.targets) {
                            rv |= visit_block_id(target);
                        }
                        rv |= visit_block_id(e.defTarget);
                    }
                    TU_ARMA(Drop, e) {
                        rv |= visit_lvalue(e.slot, ValUsage::Move);
                        rv |= visit_block_id(e.target);
                        TU_IFLET(::MIR::UnwindAction, e.unwind, Cleanup, target, rv |= visit_block_id(target);)
                    }
                    TU_ARMA(Call, e) {
                TU_MATCH_HDRA( (e.fcn), {)
                TU_ARMA(Value, ce) {
                                rv |= visit_lvalue(ce, ValUsage::Read);
                            }
                            TU_ARMA(Path, ce) {
                                visit_path(ce);
                            }
                            TU_ARMA(Intrinsic, ce) {
                                visit_path_params(ce.params);
                            }
                }
                for(auto& v : e.args)
                    rv |= visit_param(v, ValUsage::Read);
                rv |= visit_lvalue(e.ret_val, ValUsage::Write);
                rv |= visit_block_id(e.ret_block);
                TU_IFLET(::MIR::UnwindAction, e.unwind, Cleanup, target, rv |= visit_block_id(target);)
                    }
            }
            return rv;
            }

            virtual void visit_function(::MIR::TypeResolve& state, typename Dec<::MIR::Function>::Type& fcn) {
                for (auto& t : fcn.locals) {
                    visit_type(t);
                }

                for (unsigned int blockIdx = 0; blockIdx < fcn.blocks.size(); blockIdx++) {
                    auto& block = fcn.blocks[blockIdx];
                    for (auto& stmt : block.statements) {
                        state.set_cur_stmt(blockIdx, (&stmt - &block.statements.front()));
                        visit_stmt(stmt);
                    }
                    if (block.terminator.tag() == ::MIR::Terminator::TAGDEAD) {
                        continue;
                    }
                    state.set_cur_stmt_term(blockIdx);
                    visit_terminator(block.terminator);
                }
            }
        };

        class Visitor: public VisitorBase<DecConst> {
        public:
            virtual bool visit_lvalue(const ::MIR::LValue& lv, ValUsage u) override;
        };

        class VisitorMut: public VisitorBase<DecMut> {
        public:
            virtual bool visit_lvalue(::MIR::LValue& lv, ValUsage u) override;
        };
    } // namespace visit

} // namespace MIR

extern ::MIR::ValueLifetimes MIRHelperGetLifetimes(::MIR::TypeResolve& state, const ::MIR::Function& fcn, bool dumpDebug, const ::std::vector<bool>* mask = nullptr);
