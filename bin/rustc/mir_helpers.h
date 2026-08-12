#pragma once

#include "mir_mir.h"
#include "hir_typeck_static.h"

#include <vector>
#include <functional>
#include <type_traits>

namespace HIR {
    class Crate;
    class TypeData;
    using TypeRef = const TypeData*;
    struct Pattern;
    struct SimplePath;
}


    class MIRFunction;
    struct MIRLValue;
    class MIRConstant;
    struct MIRBasicBlock;
    class MIRTerminator;
    class MIRStatement;
    class MIRRValue;
    class MIRParam;

    typedef unsigned int MIRBasicBlockId;

    struct CheckFailure: public ::std::exception {};

#define MIR_BUG(state, ...)                      \
    do {                                         \
        const char* __fcn = __FUNCTION__;        \
        (state).printBug([&](auto& _os) {        \
            _os << __fcn << ": " << __VA_ARGS__; \
        });                                      \
        throw "";                                \
    } while (0)
#define MIR_ASSERT(state, cnd, ...)                                                                \
    do {                                                                                           \
        if (!(cnd))                                                                                \
            (state).printBug([&](auto& _os) {                                                      \
                _os << __FILE__ << ":" << __LINE__ << " ASSERT " #cnd " failed - " << __VA_ARGS__; \
            });                                                                                    \
    } while (0)
#define MIR_TODO(state, ...)                                           \
    do {                                                               \
        (state).printTodo([&](auto& _os) {                             \
            _os << __FILE__ << ":" << __LINE__ << ": " << __VA_ARGS__; \
        });                                                            \
        throw "";                                                      \
    } while (0)
#define MIR_DEBUG(state, ...)                                    \
    do {                                                         \
        DEBUG(FMT_CB(_ss, (state).fmtPos(_ss);) << __VA_ARGS__); \
    } while (0)

    class MIRTypeResolve {
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
        const MIRFunction& fcn;

        // If set, these override the list in `m_fcn`
        const ::HIR::TypeData* monomorphedRettype;
        const ::std::vector<::HIR::TypeRef>* monomorphedLocals;

    private:
        const ::HIR::SimplePath* mLangBox = nullptr;

        unsigned int bbIdx = 0;
        unsigned int stmtIdx = 0;

    public:
        MIRTypeResolve(const Span& sp, const ::StaticTraitResolve& resolve, ::FmtLambda path, const ::HIR::TypeData* retType, const argsT& args, const MIRFunction& fcn);

        void setCurStmt(const MIRBasicBlock& bb, const MIRStatement& stmt);

        void setCurStmt(const MIRBasicBlock& bb, unsigned int stmtIdx);

        void setCurStmt(unsigned int bbIdx, unsigned int stmtIdx);

        void setCurStmtTerm(const MIRBasicBlock& bb);

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

        const MIRBasicBlock& getBlock(MIRBasicBlockId id) const;

        const ::HIR::TypeData* getStaticType(::HIR::TypeRef& tmp, const ::HIR::Path& path) const;
        const ::HIR::TypeData* getLvalueType(::HIR::TypeRef& tmp, const MIRLValue& val, unsigned wrapperSkipCount = 0) const;

        const ::HIR::TypeData* getLvalueType(::HIR::TypeRef& tmp, const MIRLValue::CRef& val) const {
            return getLvalueType(tmp, val.lv(), val.lv().wrappers.size() - val.wrapperCount());
        }

        const ::HIR::TypeData* getLvalueType(::HIR::TypeRef& tmp, const MIRLValue::MRef& val) const {
            return getLvalueType(tmp, val.lv(), val.lv().wrappers.size() - val.wrapperCount());
        }

        const ::HIR::TypeData* getUnwrappedType(::HIR::TypeRef& tmp, const MIRLValue::Wrapper& w, const ::HIR::TypeData* ty) const;
        const ::HIR::TypeData* getParamType(::HIR::TypeRef& tmp, const MIRParam& val) const;

        ::HIR::TypeRef getConstType(const MIRConstant& c) const;

        bool lvalueIsCopy(const MIRLValue& val) const;
        const ::HIR::TypeData* isTypeOwnedBox(const ::HIR::TypeData* ty) const;

        /// @brief Handler for the `offset_of` intrinsic
        /// @param ty Type
        /// @param params Field names (must be Const::String)
        /// @return Offset in bytes
        size_t intrinsicOffsetOf(const ::HIR::TypeData* ty, const ::std::vector<MIRParam>& params) const;
        /// @brief Handler for the `type_name` intrinsic, strips out mrustc's helper comments
        /// @param ty Type
        /// @return Clean string form of the type
        std::string intrinsicTypeName(const ::HIR::TypeData* ty) const;

        friend ::std::ostream& operator<<(::std::ostream& os, const MIRTypeResolve& x);
    };

    // --------------------------------------------------------------------
    // MIR_Helper_GetLifetimes
    // --------------------------------------------------------------------
    class MIRValueLifetime {
        ::std::vector<bool> statements;

    public:
        MIRValueLifetime(::std::vector<bool> stmts);

        bool validAt(size_t ofs) const {
            return statements.at(ofs);
        }

        // true if this value is used at any point
        bool isUsed() const;

        bool overlaps(const MIRValueLifetime& x) const;

        void unify(const MIRValueLifetime& x);
    };

    struct MIRValueLifetimes {
        ::std::vector<size_t> blockOffsets;
        ::std::vector<MIRValueLifetime> slots;

        bool slotValid(unsigned idx, unsigned bbIdx, unsigned stmtIdx) const {
            return slots.at(idx).validAt(blockOffsets[bbIdx] + stmtIdx);
        }
    };

    enum class MIRValUsage {
        Move,
        Read,
        Write,
        Borrow,
    };

    extern bool visitMirLvalue(const MIRLValue& lv, MIRValUsage u, ::std::function<bool(const MIRLValue&, MIRValUsage)> cb);
    extern bool visitMirLvalue(const MIRParam& p, MIRValUsage u, ::std::function<bool(const MIRLValue&, MIRValUsage)> cb);
    extern bool visitMirLvalues(const MIRRValue& rval, ::std::function<bool(const MIRLValue&, MIRValUsage)> cb);
    extern bool visitMirLvalues(const MIRStatement& stmt, ::std::function<bool(const MIRLValue&, MIRValUsage)> cb);
    extern bool visitMirLvalues(const MIRTerminator& term, ::std::function<bool(const MIRLValue&, MIRValUsage)> cb);

    extern void visitTerminatorTargetMut(MIRTerminator& term, ::std::function<void(MIRBasicBlockId&)> cb);
    extern void visitTerminatorTarget(const MIRTerminator& term, ::std::function<void(const MIRBasicBlockId&)> cb);

    template <typename Inner>
    class MIRDecMut {
    public:
        typedef Inner Type;
    };

    template <typename Inner>
    class MIRDecConst {
    public:
        typedef const Inner Type;
    };

    template <template <typename> class Dec>
    class MIRVisitorBase {
    public:
        using TypeVisitArg = ::std::conditional_t<::std::is_const_v<typename Dec<int>::Type>, ::HIR::TypeRef, ::HIR::TypeRef&>;

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

        virtual bool visitLvalue(typename Dec<MIRLValue>::Type& lv, MIRValUsage u) = 0;

        virtual bool visitConst(typename Dec<MIRConstant>::Type& c) {
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

        virtual bool visitParam(typename Dec<MIRParam>::Type& p, MIRValUsage u) {
            TU_MATCH_HDRA( (p), {)
            TU_ARMA(LValue, e) {
                    return visitLvalue(e, u);
                }
                TU_ARMA(Borrow, e) {
                    return visitLvalue(e.val, MIRValUsage::Borrow);
                }
                TU_ARMA(Constant, e) {
                    return visitConst(e);
                }
            }
            throw "";
        }

        virtual bool visitRvalue(typename Dec<MIRRValue>::Type& rval) {
            bool rv = false;
            TU_MATCH_HDRA( (rval), {)
            TU_ARMA(Use, se) {
                    rv |= visitLvalue(se, MIRValUsage::Move);
                }
                TU_ARMA(Constant, se) {
                    rv |= visitConst(se);
                }
                TU_ARMA(SizedArray, se) {
                    rv |= visitParam(se.val, MIRValUsage::Read);
                }
                TU_ARMA(Borrow, se) {
                    rv |= visitLvalue(se.val, MIRValUsage::Borrow);
                }
                TU_ARMA(Cast, se) {
                    rv |= visitLvalue(se.val, MIRValUsage::Move);
                    visitType(se.type);
                }
                TU_ARMA(BinOp, se) {
                    rv |= visitParam(se.valL, MIRValUsage::Read);
                    rv |= visitParam(se.valR, MIRValUsage::Read);
                }
                TU_ARMA(UniOp, se) {
                    rv |= visitLvalue(se.val, MIRValUsage::Read);
                }
                TU_ARMA(DstMeta, se) {
                    rv |= visitLvalue(se.val, MIRValUsage::Read);
                }
                TU_ARMA(DstPtr, se) {
                    rv |= visitLvalue(se.val, MIRValUsage::Read);
                }
                TU_ARMA(MakeDst, se) {
                    rv |= visitParam(se.ptrVal, MIRValUsage::Move);
                    if (TU_TEST2(se.metaVal, Constant, , ItemAddr, .get() == nullptr)) {
                    } else {
                        rv |= visitParam(se.metaVal, MIRValUsage::Move);
                    }
                }
                TU_ARMA(Tuple, se) {
                    for (auto& v : se.vals) {
                        rv |= visitParam(v, MIRValUsage::Move);
                    }
                }
                TU_ARMA(Array, se) {
                    for (auto& v : se.vals) {
                        rv |= visitParam(v, MIRValUsage::Move);
                    }
                }
                TU_ARMA(UnionVariant, se) {
                    visitGenericpath(se.path);
                    rv |= visitParam(se.val, MIRValUsage::Move);
                }
                TU_ARMA(EnumVariant, se) {
                    visitGenericpath(se.path);
                    for (auto& v : se.vals) {
                        rv |= visitParam(v, MIRValUsage::Move);
                    }
                }
                TU_ARMA(Struct, se) {
                    visitGenericpath(se.path);
                    for (auto& v : se.vals) {
                        rv |= visitParam(v, MIRValUsage::Move);
                    }
                }
            }
            return rv;
        }

        virtual bool visitStmt(typename Dec<MIRStatement>::Type& stmt) {
            bool rv = false;
            TU_MATCH_HDRA( (stmt), {)
            TU_ARMA(Assign, e) {
                    rv |= visitRvalue(e.src);
                    rv |= visitLvalue(e.dst, MIRValUsage::Write);
                }
                TU_ARMA(Asm, e) {
                    for (auto& v : e.inputs) {
                        rv |= visitLvalue(v.second, MIRValUsage::Read);
                    }
                    for (auto& v : e.outputs) {
                        rv |= visitLvalue(v.second, MIRValUsage::Write);
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
                                    rv |= visitParam(*v.input, MIRValUsage::Read);
                                }
                                if (v.output) {
                                    rv |= visitLvalue(*v.output, MIRValUsage::Write);
                                }
                            }
                    }
                    }
                }
                TU_ARMA(SetDropFlag, e) {
                }
                TU_ARMA(SaveDropFlag, e) {
                    rv |= visitLvalue(e.slot, MIRValUsage::Write);
                }
                TU_ARMA(LoadDropFlag, e) {
                    rv |= visitLvalue(e.slot, MIRValUsage::Read);
                }
                TU_ARMA(ScopeEnd, e) {
                }
            }
            return rv;
        }

        virtual bool visitBlockId(typename Dec<MIRBasicBlockId>::Type& bbId) {
            return false;
        }

        virtual bool visitTerminator(typename Dec<MIRTerminator>::Type& term) {
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
                    rv |= visitLvalue(e.cond, MIRValUsage::Read);
                    rv |= visitBlockId(e.bbTrue);
                    rv |= visitBlockId(e.bbFalse);
                }
                TU_ARMA(Switch, e) {
                    rv |= visitLvalue(e.val, MIRValUsage::Read);
                    for (auto& target : e.targets) {
                        rv |= visitBlockId(target);
                    }
                    if (e.validFlag != ~0u) {
                        rv |= visitBlockId(e.invalidTarget);
                    }
                }
                TU_ARMA(SwitchValue, e) {
                    rv |= visitLvalue(e.val, MIRValUsage::Read);
                    for (auto& target : e.targets) {
                        rv |= visitBlockId(target);
                    }
                    rv |= visitBlockId(e.defTarget);
                }
                TU_ARMA(Drop, e) {
                    rv |= visitLvalue(e.slot, MIRValUsage::Move);
                    rv |= visitBlockId(e.target);
                    TU_IFLET(MIRUnwindAction, e.unwind, Cleanup, target, rv |= visitBlockId(target);)
                }
                TU_ARMA(Call, e) {
                TU_MATCH_HDRA( (e.fcn), {)
                TU_ARMA(Value, ce) {
                            rv |= visitLvalue(ce, MIRValUsage::Read);
                        }
                        TU_ARMA(Path, ce) {
                            visitPath(ce);
                        }
                        TU_ARMA(Intrinsic, ce) {
                            visitPathParams(ce.params);
                        }
                }
                for(auto& v : e.args)
                    rv |= visitParam(v, MIRValUsage::Read);
                rv |= visitLvalue(e.retVal, MIRValUsage::Write);
                rv |= visitBlockId(e.retBlock);
                TU_IFLET(MIRUnwindAction, e.unwind, Cleanup, target, rv |= visitBlockId(target);)
                }
            }
            return rv;
        }

        virtual void visitFunction(MIRTypeResolve& state, typename Dec<MIRFunction>::Type& fcn) {
            for (auto& t : fcn.locals) {
                visitType(t);
            }

            for (unsigned int blockIdx = 0; blockIdx < fcn.blocks.size(); blockIdx++) {
                auto& block = fcn.blocks[blockIdx];
                for (auto& stmt : block.statements) {
                    state.setCurStmt(blockIdx, (&stmt - &block.statements.front()));
                    visitStmt(stmt);
                }
                if (block.terminator.tag() == MIRTerminator::TAGDEAD) {
                    continue;
                }
                state.setCurStmtTerm(blockIdx);
                visitTerminator(block.terminator);
            }
        }
    };

    class MIRVisitor: public MIRVisitorBase<MIRDecConst> {
    public:
        virtual bool visitLvalue(const MIRLValue& lv, MIRValUsage u) override;
    };

    class MIRVisitorMut: public MIRVisitorBase<MIRDecMut> {
    public:
        virtual bool visitLvalue(MIRLValue& lv, MIRValUsage u) override;
    };


extern MIRValueLifetimes MIRHelperGetLifetimes(MIRTypeResolve& state, const MIRFunction& fcn, bool dumpDebug, const ::std::vector<bool>* mask = nullptr);
