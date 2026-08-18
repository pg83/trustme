#pragma once

#include "mir_mir.h"
#include "hir_typeck_static.h"

#include <vector>
#include <functional>
#include <type_traits>

class HIRCrate;
class HIRTypeData;
using HIRTypeRef = const HIRTypeData*;
struct HIRPattern;
struct HIRSimplePath;

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
    typedef ::std::vector<::std::pair<HIRPattern, HIRTypeRef>> argsT;

private:
    const unsigned int STMT_TERM = ~0u;

public:
    const Span& sp;
    const ::StaticTraitResolve& resolve;
    const HIRCrate& crate;

private:
    ::FmtLambda path_;

public:
    const HIRTypeData* retType;
    const argsT& args;
    const MIRFunction& fcn;

    // If set, these override the list in `m_fcn`
    const HIRTypeData* monomorphedRettype;
    const ::std::vector<HIRTypeRef>* monomorphedLocals;

private:
    const HIRSimplePath* langBox_ = nullptr;

    unsigned int bbIdx = 0;
    unsigned int stmtIdx = 0;

public:
    MIRTypeResolve(const Span& sp, const ::StaticTraitResolve& resolve, ::FmtLambda path, const HIRTypeData* retType, const argsT& args, const MIRFunction& fcn);

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

    const HIRTypeData* getStaticType(HIRTypeRef& tmp, const HIRPath& path) const;
    const HIRTypeData* getLvalueType(HIRTypeRef& tmp, const MIRLValue& val, unsigned wrapperSkipCount = 0) const;

    const HIRTypeData* getLvalueType(HIRTypeRef& tmp, const MIRLValue::CRef& val) const {
        return getLvalueType(tmp, val.lv(), val.lv().wrappers.size() - val.wrapperCount());
    }

    const HIRTypeData* getLvalueType(HIRTypeRef& tmp, const MIRLValue::MRef& val) const {
        return getLvalueType(tmp, val.lv(), val.lv().wrappers.size() - val.wrapperCount());
    }

    const HIRTypeData* getUnwrappedType(HIRTypeRef& tmp, const MIRLValue::Wrapper& w, const HIRTypeData* ty) const;
    const HIRTypeData* getParamType(HIRTypeRef& tmp, const MIRParam& val) const;

    HIRTypeRef getConstType(const MIRConstant& c) const;

    bool lvalueIsCopy(const MIRLValue& val) const;
    const HIRTypeData* isTypeOwnedBox(const HIRTypeData* ty) const;

    /// @brief Handler for the `offset_of` intrinsic
    /// @param ty Type
    /// @param params Field names (must be Const::String)
    /// @return Offset in bytes
    size_t intrinsicOffsetOf(const HIRTypeData* ty, const ::std::vector<MIRParam>& params) const;
    /// @brief Handler for the `type_name` intrinsic, strips out trustme's helper comments
    /// @param ty Type
    /// @return Clean string form of the type
    std::string intrinsicTypeName(const HIRTypeData* ty) const;

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
    using TypeVisitArg = ::std::conditional_t<::std::is_const_v<typename Dec<int>::Type>, HIRTypeRef, HIRTypeRef&>;

    virtual void visitType(TypeVisitArg t) {
        // NOTE: Doesn't recurse
    }

    virtual void visitPath(typename Dec<HIRPath>::Type& path) {
            switch (path.data.tag()) {
                case HIRPathData::TAG_Generic: {
                    auto& e = path.data.as_Generic();
                    visitPathParams(e.params);
                    break;
                }
                case HIRPathData::TAG_UfcsInherent: {
                    auto& e = path.data.as_UfcsInherent();
                    visitType(e.type);
                    visitPathParams(e.params);
                    break;
                }
                case HIRPathData::TAG_UfcsKnown: {
                    auto& e = path.data.as_UfcsKnown();
                    visitType(e.type);
                    visitPathParams(e.trait.params);
                    visitPathParams(e.params);
                    break;
                }
                case HIRPathData::TAG_UfcsUnknown: {
                    auto& e = path.data.as_UfcsUnknown();
                    visitType(e.type);
                    visitPathParams(e.params);
                    break;
                }
            }
    }

    virtual void visitGenericpath(typename Dec<HIRGenericPath>::Type& p) {
        visitPathParams(p.params);
    }

    virtual void visitPathParams(typename Dec<HIRPathParams>::Type& p) {
        for (auto& e : p.types) {
            visitType(e);
        }
    }

    virtual bool visitLvalue(typename Dec<MIRLValue>::Type& lv, MIRValUsage u) = 0;

    virtual bool visitConst(typename Dec<MIRConstant>::Type& c) {
            switch (c.tag()) {
default:
                break;
                case MIRConstant::TAG_ItemAddr: {
                    auto& e = c.as_ItemAddr();
                    visitPath(*e);
                    break;
                }
                case MIRConstant::TAG_Const: {
                    auto& e = c.as_Const();
                    visitPath(*e.p);
                    break;
                }
            }
            return false;
    }

    virtual bool visitParam(typename Dec<MIRParam>::Type& p, MIRValUsage u) {
            switch (p.tag()) {
                case MIRParam::TAG_LValue: {
                    auto& e = p.as_LValue();
                    return visitLvalue(e, u);
                }
                case MIRParam::TAG_Borrow: {
                    auto& e = p.as_Borrow();
                    return visitLvalue(e.val, MIRValUsage::Borrow);
                }
                case MIRParam::TAG_Constant: {
                    auto& e = p.as_Constant();
                    return visitConst(e);
                }
            }
            throw "";
    }

    virtual bool visitRvalue(typename Dec<MIRRValue>::Type& rval) {
        bool rv = false;
            switch (rval.tag()) {
                case MIRRValue::TAG_Use: {
                    auto& se = rval.as_Use();
                    rv |= visitLvalue(se, MIRValUsage::Move);
                    break;
                }
                case MIRRValue::TAG_Constant: {
                    auto& se = rval.as_Constant();
                    rv |= visitConst(se);
                    break;
                }
                case MIRRValue::TAG_SizedArray: {
                    auto& se = rval.as_SizedArray();
                    rv |= visitParam(se.val, MIRValUsage::Read);
                    break;
                }
                case MIRRValue::TAG_Borrow: {
                    auto& se = rval.as_Borrow();
                    rv |= visitLvalue(se.val, MIRValUsage::Borrow);
                    break;
                }
                case MIRRValue::TAG_Cast: {
                    auto& se = rval.as_Cast();
                    rv |= visitLvalue(se.val, MIRValUsage::Move);
                    visitType(se.type);
                    break;
                }
                case MIRRValue::TAG_BinOp: {
                    auto& se = rval.as_BinOp();
                    rv |= visitParam(se.valL, MIRValUsage::Read);
                    rv |= visitParam(se.valR, MIRValUsage::Read);
                    break;
                }
                case MIRRValue::TAG_UniOp: {
                    auto& se = rval.as_UniOp();
                    rv |= visitLvalue(se.val, MIRValUsage::Read);
                    break;
                }
                case MIRRValue::TAG_DstMeta: {
                    auto& se = rval.as_DstMeta();
                    rv |= visitLvalue(se.val, MIRValUsage::Read);
                    break;
                }
                case MIRRValue::TAG_DstPtr: {
                    auto& se = rval.as_DstPtr();
                    rv |= visitLvalue(se.val, MIRValUsage::Read);
                    break;
                }
                case MIRRValue::TAG_MakeDst: {
                    auto& se = rval.as_MakeDst();
                    rv |= visitParam(se.ptrVal, MIRValUsage::Move);
                    if ((se.metaVal.is_Constant() && se.metaVal.as_Constant().is_ItemAddr() && se.metaVal.as_Constant().as_ItemAddr().get() == nullptr)) {
                    } else {
                        rv |= visitParam(se.metaVal, MIRValUsage::Move);
                    }
                    break;
                }
                case MIRRValue::TAG_Tuple: {
                    auto& se = rval.as_Tuple();
                    for (auto& v : se.vals) {
                        rv |= visitParam(v, MIRValUsage::Move);
                    }
                    break;
                }
                case MIRRValue::TAG_Array: {
                    auto& se = rval.as_Array();
                    for (auto& v : se.vals) {
                        rv |= visitParam(v, MIRValUsage::Move);
                    }
                    break;
                }
                case MIRRValue::TAG_UnionVariant: {
                    auto& se = rval.as_UnionVariant();
                    visitGenericpath(se.path);
                    rv |= visitParam(se.val, MIRValUsage::Move);
                    break;
                }
                case MIRRValue::TAG_EnumVariant: {
                    auto& se = rval.as_EnumVariant();
                    visitGenericpath(se.path);
                    for (auto& v : se.vals) {
                        rv |= visitParam(v, MIRValUsage::Move);
                    }
                    break;
                }
                case MIRRValue::TAG_Struct: {
                    auto& se = rval.as_Struct();
                    visitGenericpath(se.path);
                    for (auto& v : se.vals) {
                        rv |= visitParam(v, MIRValUsage::Move);
                    }
                    break;
                }
            }
            return rv;
    }

    virtual bool visitStmt(typename Dec<MIRStatement>::Type& stmt) {
        bool rv = false;
            switch (stmt.tag()) {
                case MIRStatement::TAG_Assign: {
                    auto& e = stmt.as_Assign();
                    rv |= visitRvalue(e.src);
                    rv |= visitLvalue(e.dst, MIRValUsage::Write);
                    break;
                }
                case MIRStatement::TAG_Asm: {
                    auto& e = stmt.as_Asm();
                    for (auto& v : e.inputs) {
                        rv |= visitLvalue(v.second, MIRValUsage::Read);
                    }
                    for (auto& v : e.outputs) {
                        rv |= visitLvalue(v.second, MIRValUsage::Write);
                    }
                    break;
                }
                case MIRStatement::TAG_Asm2: {
                    auto& e = stmt.as_Asm2();
                    for (auto& p : e.params) {
                        switch (p.tag()) {
                            case MIRAsmParam::TAG_Const: {
                                auto& v = p.as_Const();
                                rv |= visitConst(v);
                                break;
                            }
                            case MIRAsmParam::TAG_Sym: {
                                auto& v = p.as_Sym();
                                visitPath(v);
                                break;
                            }
                            case MIRAsmParam::TAG_Reg: {
                                auto& v = p.as_Reg();
                                if (v.input) {
                                    rv |= visitParam(*v.input, MIRValUsage::Read);
                                }
                                if (v.output) {
                                    rv |= visitLvalue(*v.output, MIRValUsage::Write);
                                }
                                break;
                            }
                            case MIRAsmParam::TAG_Label: {
                                auto& v = p.as_Label();
                                rv |= visitBlockId(v);
                                break;
                            }
                        }
                    }
                    break;
                }
                case MIRStatement::TAG_SetDropFlag: {
                    break;
                }
                case MIRStatement::TAG_SaveDropFlag: {
                    auto& e = stmt.as_SaveDropFlag();
                    rv |= visitLvalue(e.slot, MIRValUsage::Write);
                    break;
                }
                case MIRStatement::TAG_LoadDropFlag: {
                    auto& e = stmt.as_LoadDropFlag();
                    rv |= visitLvalue(e.slot, MIRValUsage::Read);
                    break;
                }
                case MIRStatement::TAG_ScopeEnd: {
                    break;
                }
            }
            return rv;
    }

    virtual bool visitBlockId(typename Dec<MIRBasicBlockId>::Type& bbId) {
        return false;
    }

    virtual bool visitTerminator(typename Dec<MIRTerminator>::Type& term) {
        bool rv = false;
            switch (term.tag()) {
                case MIRTerminator::TAG_Incomplete: {
                    break;
                }
                case MIRTerminator::TAG_Return: {
                    break;
                }
                case MIRTerminator::TAG_UnwindResume: {
                    break;
                }
                case MIRTerminator::TAG_UnwindTerminate: {
                    break;
                }
                case MIRTerminator::TAG_Unreachable: {
                    break;
                }
                case MIRTerminator::TAG_Goto: {
                    auto& e = term.as_Goto();
                    visitBlockId(e);
                    break;
                }
                case MIRTerminator::TAG_If: {
                    auto& e = term.as_If();
                    rv |= visitLvalue(e.cond, MIRValUsage::Read);
                    rv |= visitBlockId(e.bbTrue);
                    rv |= visitBlockId(e.bbFalse);
                    break;
                }
                case MIRTerminator::TAG_Switch: {
                    auto& e = term.as_Switch();
                    rv |= visitLvalue(e.val, MIRValUsage::Read);
                    for (auto& target : e.targets) {
                        rv |= visitBlockId(target);
                    }
                    if (e.validFlag != ~0u) {
                        rv |= visitBlockId(e.invalidTarget);
                    }
                    break;
                }
                case MIRTerminator::TAG_SwitchValue: {
                    auto& e = term.as_SwitchValue();
                    rv |= visitLvalue(e.val, MIRValUsage::Read);
                    for (auto& target : e.targets) {
                        rv |= visitBlockId(target);
                    }
                    rv |= visitBlockId(e.defTarget);
                    break;
                }
                case MIRTerminator::TAG_Drop: {
                    auto& e = term.as_Drop();
                    rv |= visitLvalue(e.slot, MIRValUsage::Move);
                    rv |= visitBlockId(e.target);
                    if (e.unwind.is_Cleanup()) {
                        auto& target = e.unwind.as_Cleanup();
                        rv |= visitBlockId(target);
                    }
                    break;
                }
                case MIRTerminator::TAG_Call: {
                    auto& e = term.as_Call();
                    switch (e.fcn.tag()) {
                        case MIRCallTarget::TAG_Value: {
                            auto& ce = e.fcn.as_Value();
                            rv |= visitLvalue(ce, MIRValUsage::Read);
                            break;
                        }
                        case MIRCallTarget::TAG_Path: {
                            auto& ce = e.fcn.as_Path();
                            visitPath(ce);
                            break;
                        }
                        case MIRCallTarget::TAG_Intrinsic: {
                            auto& ce = e.fcn.as_Intrinsic();
                            visitPathParams(ce.params);
                            break;
                        }
                    }
                    for(auto& v : e.args)
                        rv |= visitParam(v, MIRValUsage::Read);
                    rv |= visitLvalue(e.retVal, MIRValUsage::Write);
                    rv |= visitBlockId(e.retBlock);
                    if (e.unwind.is_Cleanup()) {
                        auto& target = e.unwind.as_Cleanup();
                        rv |= visitBlockId(target);
                    }
                    break;
                }
                case MIRTerminator::TAG_TailCall: {
                    auto& e = term.as_TailCall();
                    switch (e.fcn.tag()) {
                        case MIRCallTarget::TAG_Value: {
                            auto& ce = e.fcn.as_Value();
                            rv |= visitLvalue(ce, MIRValUsage::Read);
                            break;
                        }
                        case MIRCallTarget::TAG_Path: {
                            auto& ce = e.fcn.as_Path();
                            visitPath(ce);
                            break;
                        }
                        case MIRCallTarget::TAG_Intrinsic: {
                            auto& ce = e.fcn.as_Intrinsic();
                            visitPathParams(ce.params);
                            break;
                        }
                    }
                    for (auto& v : e.args) {
                        rv |= visitParam(v, MIRValUsage::Move);
                    }
                    break;
                }
                case MIRTerminator::TAG_Asm2: {
                    auto& e = term.as_Asm2();
                    for (auto& p : e.params) {
                        switch (p.tag()) {
                            case MIRAsmParam::TAG_Const: {
                                auto& v = p.as_Const();
                                rv |= visitConst(v);
                                break;
                            }
                            case MIRAsmParam::TAG_Sym: {
                                auto& v = p.as_Sym();
                                visitPath(v);
                                break;
                            }
                            case MIRAsmParam::TAG_Reg: {
                                auto& v = p.as_Reg();
                                if (v.input) rv |= visitParam(*v.input, MIRValUsage::Read);
                                if (v.output) rv |= visitLvalue(*v.output, MIRValUsage::Write);
                                break;
                            }
                            case MIRAsmParam::TAG_Label: {
                                auto& v = p.as_Label();
                                rv |= visitBlockId(v);
                                break;
                            }
                        }
                    }
                    if (e.retBlock != ~0u) rv |= visitBlockId(e.retBlock);
                    break;
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
            if (block.terminator.isDead()) {
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
