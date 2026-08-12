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
        (state).printBug([&](auto& _os) {       \
            _os << __fcn << ": " << __VA_ARGS__; \
        });                                      \
        throw "";                                \
    } while (0)
#define MIR_ASSERT(state, cnd, ...)                                                                \
    do {                                                                                           \
        if (!(cnd))                                                                                \
            (state).printBug([&](auto& _os) {                                                     \
                _os << __FILE__ << ":" << __LINE__ << " ASSERT " #cnd " failed - " << __VA_ARGS__; \
            });                                                                                    \
    } while (0)
#define MIR_TODO(state, ...)                                           \
    do {                                                               \
        (state).printTodo([&](auto& _os) {                            \
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
        unsigned int stmtIdx = 0;

    public:
        TypeResolve(const Span& sp, const ::StaticTraitResolve& resolve, ::FmtLambda path, const ::HIR::TypeData* ret_type, const argsT& args, const ::MIR::Function& fcn);

        void setCurStmt(const ::MIR::BasicBlock& bb, const ::MIR::Statement& stmt);

        void setCurStmt(const ::MIR::BasicBlock& bb, unsigned int stmtIdx);

        void setCurStmt(unsigned int bbIdx, unsigned int stmtIdx);

        void setCurStmtTerm(const ::MIR::BasicBlock& bb);

        void setCurStmtTerm(unsigned int bbIdx);

        unsigned int getCurBlock() const {
            return bbIdx;
        }

        unsigned int getCurStmtOfs() const;

        void fmtPos(::std::ostream& os, bool includePath = false) const;

        void printBug(::std::function<void(::std::ostream& os)> cb) const {
            printMsg("ERROR", cb);
        }

        void printTodo(::std::function<void(::std::ostream& os)> cb) const {
            printMsg("TODO", cb);
        }

        void printMsg(const char* tag, ::std::function<void(::std::ostream& os)> cb) const;

        const ::MIR::BasicBlock& getBlock(::MIR::BasicBlockId id) const;

        const ::HIR::TypeData* getStaticType(::HIR::TypeRef& tmp, const ::HIR::Path& path) const;
        const ::HIR::TypeData* getLvalueType(::HIR::TypeRef& tmp, const ::MIR::LValue& val, unsigned wrapperSkipCount = 0) const;

        const ::HIR::TypeData* getLvalueType(::HIR::TypeRef& tmp, const ::MIR::LValue::CRef& val) const {
            return getLvalueType(tmp, val.lv(), val.lv().wrappers.size() - val.wrapper_count());
        }

        const ::HIR::TypeData* getLvalueType(::HIR::TypeRef& tmp, const ::MIR::LValue::MRef& val) const {
            return getLvalueType(tmp, val.lv(), val.lv().wrappers.size() - val.wrapper_count());
        }

        const ::HIR::TypeData* getUnwrappedType(::HIR::TypeRef& tmp, const ::MIR::LValue::Wrapper& w, const ::HIR::TypeData* ty) const;
        const ::HIR::TypeData* getParamType(::HIR::TypeRef& tmp, const ::MIR::Param& val) const;

        ::HIR::TypeRef getConstType(const ::MIR::Constant& c) const;

        bool lvalueIsCopy(const ::MIR::LValue& val) const;
        const ::HIR::TypeData* isTypeOwnedBox(const ::HIR::TypeData* ty) const;

        /// @brief Handler for the `offset_of` intrinsic
        /// @param ty Type
        /// @param params Field names (must be Const::String)
        /// @return Offset in bytes
        size_t intrinsicOffsetOf(const ::HIR::TypeData* ty, const ::std::vector<MIR::Param>& params) const;
        /// @brief Handler for the `type_name` intrinsic, strips out mrustc's helper comments
        /// @param ty Type
        /// @return Clean string form of the type
        std::string intrinsicTypeName(const ::HIR::TypeData* ty) const;

        friend ::std::ostream& operator<<(::std::ostream& os, const TypeResolve& x);
    };

    // --------------------------------------------------------------------
    // MIR_Helper_GetLifetimes
    // --------------------------------------------------------------------
    class ValueLifetime {
        ::std::vector<bool> statements;

    public:
        ValueLifetime(::std::vector<bool> stmts);

        bool validAt(size_t ofs) const {
            return statements.at(ofs);
        }

        // true if this value is used at any point
        bool isUsed() const;

        bool overlaps(const ValueLifetime& x) const;

        void unify(const ValueLifetime& x);
    };

    struct ValueLifetimes {
        ::std::vector<size_t> blockOffsets;
        ::std::vector<ValueLifetime> slots;

        bool slotValid(unsigned idx, unsigned bbIdx, unsigned stmtIdx) const {
            return slots.at(idx).validAt(blockOffsets[bbIdx] + stmtIdx);
        }
    };

    namespace visit {
        enum class ValUsage {
            Move,
            Read,
            Write,
            Borrow,
        };

        extern bool visitMirLvalue(const ::MIR::LValue& lv, ValUsage u, ::std::function<bool(const ::MIR::LValue&, ValUsage)> cb);
        extern bool visitMirLvalue(const ::MIR::Param& p, ValUsage u, ::std::function<bool(const ::MIR::LValue&, ValUsage)> cb);
        extern bool visitMirLvalues(const ::MIR::RValue& rval, ::std::function<bool(const ::MIR::LValue&, ValUsage)> cb);
        extern bool visitMirLvalues(const ::MIR::Statement& stmt, ::std::function<bool(const ::MIR::LValue&, ValUsage)> cb);
        extern bool visitMirLvalues(const ::MIR::Terminator& term, ::std::function<bool(const ::MIR::LValue&, ValUsage)> cb);

        extern void visitTerminatorTargetMut(::MIR::Terminator& term, ::std::function<void(::MIR::BasicBlockId&)> cb);
        extern void visitTerminatorTarget(const ::MIR::Terminator& term, ::std::function<void(const ::MIR::BasicBlockId&)> cb);

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

            virtual void visitType(TypeVisitArg t) {
                // NOTE: Doesn't recurse
            }

            virtual void visitPath(typename Dec<::HIR::Path>::Type& path) {
            TU_MATCH_HDRA((path.mData), {)
            TU_ARMA(Generic, e) {
                        visitPathParams(e.mParams);
                    }
                    TU_ARMA(UfcsInherent, e) {
                        visitType(e.type);
                        visitPathParams(e.params);
                    }
                    TU_ARMA(UfcsKnown, e) {
                        visitType(e.type);
                        visitPathParams(e.trait.mParams);
                        visitPathParams(e.params);
                    }
                    TU_ARMA(UfcsUnknown, e) {
                        visitType(e.type);
                        visitPathParams(e.params);
                    }
            }
            }

            virtual void visitGenericpath(typename Dec<::HIR::GenericPath>::Type& p) {
                visitPathParams(p.mParams);
            }

            virtual void visitPathParams(typename Dec<::HIR::PathParams>::Type& p) {
                for (auto& e : p.types) {
                    visitType(e);
                }
            }

            virtual bool visitLvalue(typename Dec<::MIR::LValue>::Type& lv, ValUsage u) = 0;

            virtual bool visitConst(typename Dec<::MIR::Constant>::Type& c) {
            TU_MATCH_HDRA( (c), {)
            default:
                break;
                    TU_ARMA(ItemAddr, e) {
                        visitPath(*e);
                    }
                    TU_ARMA(Const, e) {
                        visitPath(*e.p);
                    }
            }
            return false;
            }

            virtual bool visitParam(typename Dec<::MIR::Param>::Type& p, ValUsage u) {
            TU_MATCH_HDRA( (p), {)
            TU_ARMA(LValue, e) {
                        return visitLvalue(e, u);
                    }
                    TU_ARMA(Borrow, e) {
                        return visitLvalue(e.val, ValUsage::Borrow);
                    }
                    TU_ARMA(Constant, e) {
                        return visitConst(e);
                    }
            }
            throw "";
            }

            virtual bool visitRvalue(typename Dec<::MIR::RValue>::Type& rval) {
                bool rv = false;
            TU_MATCH_HDRA( (rval), {)
            TU_ARMA(Use, se) {
                        rv |= visitLvalue(se, ValUsage::Move);
                    }
                    TU_ARMA(Constant, se) {
                        rv |= visitConst(se);
                    }
                    TU_ARMA(SizedArray, se) {
                        rv |= visitParam(se.val, ValUsage::Read);
                    }
                    TU_ARMA(Borrow, se) {
                        rv |= visitLvalue(se.val, ValUsage::Borrow);
                    }
                    TU_ARMA(Cast, se) {
                        rv |= visitLvalue(se.val, ValUsage::Move);
                        visitType(se.type);
                    }
                    TU_ARMA(BinOp, se) {
                        rv |= visitParam(se.valL, ValUsage::Read);
                        rv |= visitParam(se.valR, ValUsage::Read);
                    }
                    TU_ARMA(UniOp, se) {
                        rv |= visitLvalue(se.val, ValUsage::Read);
                    }
                    TU_ARMA(DstMeta, se) {
                        rv |= visitLvalue(se.val, ValUsage::Read);
                    }
                    TU_ARMA(DstPtr, se) {
                        rv |= visitLvalue(se.val, ValUsage::Read);
                    }
                    TU_ARMA(MakeDst, se) {
                        rv |= visitParam(se.ptrVal, ValUsage::Move);
                        if (TU_TEST2(se.metaVal, Constant, , ItemAddr, .get() == nullptr)) {
                        } else {
                            rv |= visitParam(se.metaVal, ValUsage::Move);
                        }
                    }
                    TU_ARMA(Tuple, se) {
                        for (auto& v : se.vals) {
                            rv |= visitParam(v, ValUsage::Move);
                        }
                    }
                    TU_ARMA(Array, se) {
                        for (auto& v : se.vals) {
                            rv |= visitParam(v, ValUsage::Move);
                        }
                    }
                    TU_ARMA(UnionVariant, se) {
                        visitGenericpath(se.path);
                        rv |= visitParam(se.val, ValUsage::Move);
                    }
                    TU_ARMA(EnumVariant, se) {
                        visitGenericpath(se.path);
                        for (auto& v : se.vals) {
                            rv |= visitParam(v, ValUsage::Move);
                        }
                    }
                    TU_ARMA(Struct, se) {
                        visitGenericpath(se.path);
                        for (auto& v : se.vals) {
                            rv |= visitParam(v, ValUsage::Move);
                        }
                    }
            }
            return rv;
            }

            virtual bool visitStmt(typename Dec<::MIR::Statement>::Type& stmt) {
                bool rv = false;
            TU_MATCH_HDRA( (stmt), {)
            TU_ARMA(Assign, e) {
                        rv |= visitRvalue(e.src);
                        rv |= visitLvalue(e.dst, ValUsage::Write);
                    }
                    TU_ARMA(Asm, e) {
                        for (auto& v : e.inputs) {
                            rv |= visitLvalue(v.second, ValUsage::Read);
                        }
                        for (auto& v : e.outputs) {
                            rv |= visitLvalue(v.second, ValUsage::Write);
                        }
                    }
                    TU_ARMA(Asm2, e) {
                        for (auto& p : e.params) {
                    TU_MATCH_HDRA( (p), { )
                    TU_ARMA(Const, v)
                        rv |= visitConst(v);
                                TU_ARMA(Sym, v)
                                /*rv |= */ visitPath(v);
                                TU_ARMA(Reg, v) {
                                    if (v.input) {
                                        rv |= visitParam(*v.input, ValUsage::Read);
                                    }
                                    if (v.output) {
                                        rv |= visitLvalue(*v.output, ValUsage::Write);
                                    }
                                }
                    }
                        }
                    }
                    TU_ARMA(SetDropFlag, e) {
                    }
                    TU_ARMA(SaveDropFlag, e) {
                        rv |= visitLvalue(e.slot, ValUsage::Write);
                    }
                    TU_ARMA(LoadDropFlag, e) {
                        rv |= visitLvalue(e.slot, ValUsage::Read);
                    }
                    TU_ARMA(ScopeEnd, e) {
                    }
            }
            return rv;
            }

            virtual bool visitBlockId(typename Dec<::MIR::BasicBlockId>::Type& bbId) {
                return false;
            }

            virtual bool visitTerminator(typename Dec<::MIR::Terminator>::Type& term) {
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
                        visitBlockId(e);
                    }
                    TU_ARMA(If, e) {
                        rv |= visitLvalue(e.cond, ValUsage::Read);
                        rv |= visitBlockId(e.bbTrue);
                        rv |= visitBlockId(e.bbFalse);
                    }
                    TU_ARMA(Switch, e) {
                        rv |= visitLvalue(e.val, ValUsage::Read);
                        for (auto& target : e.targets) {
                            rv |= visitBlockId(target);
                        }
                        if (e.validFlag != ~0u) {
                            rv |= visitBlockId(e.invalidTarget);
                        }
                    }
                    TU_ARMA(SwitchValue, e) {
                        rv |= visitLvalue(e.val, ValUsage::Read);
                        for (auto& target : e.targets) {
                            rv |= visitBlockId(target);
                        }
                        rv |= visitBlockId(e.defTarget);
                    }
                    TU_ARMA(Drop, e) {
                        rv |= visitLvalue(e.slot, ValUsage::Move);
                        rv |= visitBlockId(e.target);
                        TU_IFLET(::MIR::UnwindAction, e.unwind, Cleanup, target, rv |= visitBlockId(target);)
                    }
                    TU_ARMA(Call, e) {
                TU_MATCH_HDRA( (e.fcn), {)
                TU_ARMA(Value, ce) {
                                rv |= visitLvalue(ce, ValUsage::Read);
                            }
                            TU_ARMA(Path, ce) {
                                visitPath(ce);
                            }
                            TU_ARMA(Intrinsic, ce) {
                                visitPathParams(ce.params);
                            }
                }
                for(auto& v : e.args)
                    rv |= visitParam(v, ValUsage::Read);
                rv |= visitLvalue(e.retVal, ValUsage::Write);
                rv |= visitBlockId(e.retBlock);
                TU_IFLET(::MIR::UnwindAction, e.unwind, Cleanup, target, rv |= visitBlockId(target);)
                    }
            }
            return rv;
            }

            virtual void visitFunction(::MIR::TypeResolve& state, typename Dec<::MIR::Function>::Type& fcn) {
                for (auto& t : fcn.locals) {
                    visitType(t);
                }

                for (unsigned int blockIdx = 0; blockIdx < fcn.blocks.size(); blockIdx++) {
                    auto& block = fcn.blocks[blockIdx];
                    for (auto& stmt : block.statements) {
                        state.setCurStmt(blockIdx, (&stmt - &block.statements.front()));
                        visitStmt(stmt);
                    }
                    if (block.terminator.tag() == ::MIR::Terminator::TAGDEAD) {
                        continue;
                    }
                    state.setCurStmtTerm(blockIdx);
                    visitTerminator(block.terminator);
                }
            }
        };

        class Visitor: public VisitorBase<DecConst> {
        public:
            virtual bool visitLvalue(const ::MIR::LValue& lv, ValUsage u) override;
        };

        class VisitorMut: public VisitorBase<DecMut> {
        public:
            virtual bool visitLvalue(::MIR::LValue& lv, ValUsage u) override;
        };
    } // namespace visit

} // namespace MIR

extern ::MIR::ValueLifetimes MIRHelperGetLifetimes(::MIR::TypeResolve& state, const ::MIR::Function& fcn, bool dumpDebug, const ::std::vector<bool>* mask = nullptr);
