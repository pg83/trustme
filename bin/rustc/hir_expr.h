#pragma once

#include "output.h"

#include "span.h"
#include "hir_asm.h"
#include "hir_type.h"
#include "hir_pattern.h"
#include "hir_visitor.h"
#include "hir_typeck_common.h"

#include <memory>

typedef std::vector<std::pair<const HIRSimplePath*, const HIRTrait*>> tTraitList;

enum class HIRValueUsage {
    Unknown,

    Borrow,

    Mutate,

    Move,
};

class HIRGenericParams;

class HIRExprVisitor;

class HIRExprNode {
public:
    Span span_;
    HIRTypeRef resType; // TODO: Replace this with an index into an ivar table

    bool diverges = false;
    HIRValueUsage usage = HIRValueUsage::Unknown;

    const Span& span() const {
        return span_;
    }

    virtual void visit(HIRExprVisitor& v) = 0;
    virtual unsigned int nodeKind() const = 0;

    HIRExprNode(Span sp);

    virtual ~HIRExprNode();

    const char* typeName() const;
};

struct HIRExprNodeBlock: public HIRExprNode {
    bool isUnsafe;
    std::vector<HIRExprNodeP> nodes;
    HIRExprNodeP valueNode;

    HIRSimplePath localMod;
    tTraitList traits;

    HIRExprNodeBlock(Span sp);

    HIRExprNodeBlock(Span sp, bool isUnsafe, std::vector<HIRExprNodeP> nodes, HIRExprNodeP valueNode);

    static constexpr unsigned int kind = 1;
    unsigned int nodeKind() const override;
    void visit(HIRExprVisitor& nv) override;
};

struct HIRExprNodeConstBlock: public HIRExprNode {
    HIRExprNodeP inner;

    HIRExprNodeConstBlock(Span sp, HIRExprNodeP inner);

    static constexpr unsigned int kind = 2;
    unsigned int nodeKind() const override;
    void visit(HIRExprVisitor& nv) override;
};

struct HIRExprNodeAsm: public HIRExprNode {
    struct ValRef {
        std::string spec;
        HIRExprNodeP value;
    };

    std::string templateText;
    std::vector<ValRef> outputs;
    std::vector<ValRef> inputs;
    std::vector<std::string> clobbers;
    std::vector<std::string> flags;

    HIRExprNodeAsm(Span sp, std::string tplStr, std::vector<ValRef> outputs, std::vector<ValRef> inputs, std::vector<std::string> clobbers, std::vector<std::string> flags)
        : HIRExprNode(mv$(sp))
        , templateText(mv$(tplStr))
        , outputs(mv$(outputs))
        , inputs(mv$(inputs))
        , clobbers(mv$(clobbers))
        , flags(mv$(flags))
    {
    }

    static constexpr unsigned int kind = 3;
    unsigned int nodeKind() const override;
    void visit(HIRExprVisitor& nv) override;
};

#include "hir_expr_tu.h"

struct HIRExprNodeAsm2: public HIRExprNode {
    using Param = HIRAsmParam;

    AsmOptions options;
    std::vector<AsmLine> lines;
    std::vector<Param> params;

    HIRExprNodeAsm2(Span sp, AsmOptions options, std::vector<AsmLine> lines, std::vector<Param> params);

    static constexpr unsigned int kind = 4;
    unsigned int nodeKind() const override;
    void visit(HIRExprVisitor& nv) override;
};

struct HIRExprNodeReturn: public HIRExprNode {
    HIRExprNodeP value;
    bool isTailCall;

    HIRExprNodeReturn(Span sp, HIRExprNodeP value, bool isTailCall = false);

    static constexpr unsigned int kind = 5;
    unsigned int nodeKind() const override;
    void visit(HIRExprVisitor& nv) override;
};

struct HIRExprNodeYield: public HIRExprNode {
    HIRExprNodeP value;

    HIRExprNodeYield(Span sp, HIRExprNodeP value);

    static constexpr unsigned int kind = 6;
    unsigned int nodeKind() const override;
    void visit(HIRExprVisitor& nv) override;
};

struct HIRExprNodeAWait: public HIRExprNode {
    HIRExprNodeP value;

    bool isNext = false;

    HIRExprNodeAWait(Span sp, HIRExprNodeP value);

    static constexpr unsigned int kind = 7;
    unsigned int nodeKind() const override;
    void visit(HIRExprVisitor& nv) override;
};

struct HIRExprNodeUse: public HIRExprNode {
    HIRExprNodeP value;

    HIRExprNodeUse(Span sp, HIRExprNodeP value);

    static constexpr unsigned int kind = 40;
    unsigned int nodeKind() const override;
    void visit(HIRExprVisitor& nv) override;
};

struct HIRExprNodeLoop: public HIRExprNode {
    RcString label;
    HIRExprNodeP code;
    bool requireLabel = false;

    HIRExprNodeLoop(Span sp, RcString label, HIRExprNodeP code, bool requireLabel = false);

    static constexpr unsigned int kind = 8;
    unsigned int nodeKind() const override;
    void visit(HIRExprVisitor& nv) override;
};

struct HIRExprNodeLoopControl: public HIRExprNode {
    RcString label;
    bool isContinue;
    HIRExprNodeP value;

    const HIRExprNodeLoop* targetNode;

    HIRExprNodeLoopControl(Span sp, RcString label, bool cont, HIRExprNodeP value = {});

    static constexpr unsigned int kind = 9;
    unsigned int nodeKind() const override;
    void visit(HIRExprVisitor& nv) override;
};

struct HIRExprNodeLet: public HIRExprNode {
    HIRPattern pattern;
    HIRTypeRef type;
    HIRExprNodeP value;
    bool isSuper;

    HIRExprNodeLet(Span sp, HIRPattern pat, HIRTypeRef ty, HIRExprNodeP val, bool isSuper = false);

    static constexpr unsigned int kind = 10;
    unsigned int nodeKind() const override;
    void visit(HIRExprVisitor& nv) override;
};

struct HIRExprNodeMatch: public HIRExprNode {
    struct Guard {
        HIRPattern pat;

        HIRExprNodeP val;

        bool isIf;
    };

    struct Arm {
        std::vector<HIRPattern> patterns;

        std::vector<Guard> guards;

        HIRExprNodeP code;
    };

    HIRExprNodeP value;
    std::vector<Arm> arms;
    bool isLetElse;

    HIRExprNodeMatch(Span sp, HIRExprNodeP val, std::vector<Arm> arms, bool isLetElse = false);

    static constexpr unsigned int kind = 11;
    unsigned int nodeKind() const override;
    void visit(HIRExprVisitor& nv) override;
};

struct HIRExprNodeAssign: public HIRExprNode {
    enum class Op {
        None,
        Add,
        Sub,
        Mul,
        Div,
        Mod,
        And,
        Or,
        Xor,
        Shr,
        Shl,
    };

    static const char* opname(Op v);

    Op op;
    HIRExprNodeP slot;
    HIRExprNodeP value;

    HIRExprNodeAssign(Span sp, Op op, HIRExprNodeP slot, HIRExprNodeP value);

    static constexpr unsigned int kind = 12;
    unsigned int nodeKind() const override;
    void visit(HIRExprVisitor& nv) override;
};

struct HIRExprNodeBinOp: public HIRExprNode {
    enum class Op {
        CmpEqu,
        CmpNEqu,
        CmpLt,
        CmpLtE,
        CmpGt,
        CmpGtE,

        BoolAnd,
        BoolOr,

        Add,
        Sub,
        Mul,
        Div,
        Mod,
        And,
        Or,
        Xor,
        Shr,
        Shl,
    };

    static const char* opname(Op v);

    Op op;
    HIRExprNodeP left;
    HIRExprNodeP right;

    HIRExprNodeBinOp(Span sp, Op op, HIRExprNodeP left, HIRExprNodeP right);

    static constexpr unsigned int kind = 13;
    unsigned int nodeKind() const override;
    void visit(HIRExprVisitor& nv) override;
};

struct HIRExprNodeUniOp: public HIRExprNode {
    enum class Op {
        Invert,
        Negate,
    };

    static const char* opname(Op v);

    Op op;
    HIRExprNodeP value;

    HIRExprNodeUniOp(Span sp, Op op, HIRExprNodeP value);

    static constexpr unsigned int kind = 14;
    unsigned int nodeKind() const override;
    void visit(HIRExprVisitor& nv) override;
};

struct HIRExprNodeBorrow: public HIRExprNode {
    HIRBorrowType type;
    HIRExprNodeP value;

    bool isValidStaticBorrowConstant;

    HIRExprNodeBorrow(Span sp, HIRBorrowType bt, HIRExprNodeP value);

    static constexpr unsigned int kind = 15;
    unsigned int nodeKind() const override;
    void visit(HIRExprVisitor& nv) override;
};

struct HIRExprNodeRawBorrow: public HIRExprNode {
    HIRBorrowType type;
    HIRExprNodeP value;

    HIRExprNodeRawBorrow(Span sp, HIRBorrowType bt, HIRExprNodeP value);

    static constexpr unsigned int kind = 16;
    unsigned int nodeKind() const override;
    void visit(HIRExprVisitor& nv) override;
};

struct HIRExprNodeCast: public HIRExprNode {
    HIRExprNodeP value;
    HIRTypeRef dstType;

    HIRExprNodeCast(Span sp, HIRExprNodeP value, HIRTypeRef dstType);

    static constexpr unsigned int kind = 17;
    unsigned int nodeKind() const override;
    void visit(HIRExprVisitor& nv) override;
};

struct HIRExprNodeUnsize: public HIRExprNode {
    HIRExprNodeP value;
    HIRTypeRef dstType;
    bool isArrayToSliceAdjustment = false;

    HIRExprNodeUnsize(Span sp, HIRExprNodeP value, HIRTypeRef dstType);

    static constexpr unsigned int kind = 18;
    unsigned int nodeKind() const override;
    void visit(HIRExprVisitor& nv) override;
};

struct HIRExprNodeIndex: public HIRExprNode {
    HIRExprNodeP value;
    HIRExprNodeP index;

    struct {
        HIRTypeRef indexTy;
    } cache;

    HIRExprNodeIndex(Span sp, HIRExprNodeP val, HIRExprNodeP index);

    static constexpr unsigned int kind = 19;
    unsigned int nodeKind() const override;
    void visit(HIRExprVisitor& nv) override;
};

struct HIRExprNodeDeref: public HIRExprNode {
    enum class TraitUsed {
        Unknown,
        Builtin,
        Trait,
    };

    HIRExprNodeP value;
    TraitUsed traitUsed;

    HIRExprNodeDeref(Span sp, HIRExprNodeP val);

    static constexpr unsigned int kind = 20;
    unsigned int nodeKind() const override;
    void visit(HIRExprVisitor& nv) override;
};

struct HIRExprNodeEmplace: public HIRExprNode {
    enum class Type {
        Noop, // Hack to allow coercion - acts as a no-op node
        Placer,
        Boxer,
    };

    Type type;
    HIRExprNodeP place;
    HIRExprNodeP value;

    HIRExprNodeEmplace(Span sp, Type ty, HIRExprNodeP place, HIRExprNodeP val);

    static constexpr unsigned int kind = 21;
    unsigned int nodeKind() const override;
    void visit(HIRExprVisitor& nv) override;
};

struct HIRExprNodeTupleVariant: public HIRExprNode {
    HIRGenericPath path;
    bool isStruct;
    std::vector<HIRExprNodeP> args;

    std::vector<HIRTypeRef> argTypes;

    HIRExprNodeTupleVariant(Span sp, HIRGenericPath path, bool isStruct, std::vector<HIRExprNodeP> args);

    static constexpr unsigned int kind = 22;
    unsigned int nodeKind() const override;
    void visit(HIRExprVisitor& nv) override;
};

struct HIRExprCallCache {
    std::vector<HIRTypeRef> argTypes;
    const HIRGenericParams* fcnParams;
    const HIRGenericParams* topParams;
    const HIRFunction* fcn;

    std::unique_ptr<Monomorphiser> monomorph;
};

struct HIRExprNodeCallPath: public HIRExprNode {
    HIRPath path;
    std::vector<HIRExprNodeP> args;

    HIRExprCallCache cache;

    HIRExprNodeCallPath(Span sp, HIRPath path, std::vector<HIRExprNodeP> args);

    static constexpr unsigned int kind = 23;
    unsigned int nodeKind() const override;
    void visit(HIRExprVisitor& nv) override;
};

struct HIRExprNodeCallValue: public HIRExprNode {
    HIRExprNodeP value;
    std::vector<HIRExprNodeP> args;

    std::vector<HIRTypeRef> argIvars;

    std::vector<HIRTypeRef> argTypes;

    enum class TraitUsed {
        Unknown,
        Fn,
        FnMut,
        FnOnce,

        AsyncFn,
        AsyncFnMut,
        AsyncFnOnce,
    };
    TraitUsed traitUsed = TraitUsed::Unknown;

    HIRExprNodeCallValue(Span sp, HIRExprNodeP val, std::vector<HIRExprNodeP> args);

    static constexpr unsigned int kind = 24;
    unsigned int nodeKind() const override;
    void visit(HIRExprVisitor& nv) override;
};

// TODO: Refactor to support efficient method chaining
struct HIRExprNodeCallMethod: public HIRExprNode {
    HIRExprNodeP value;

    RcString method;

    RcString fallbackMethod;

    HIRPathParams params;

    std::vector<HIRExprNodeP> args;

    HIRPath methodPath;

    HIRExprCallCache cache;

    tTraitList traits;

    std::vector<unsigned int> traitParamIvars;
    unsigned int traitParamTypeIvars = 0;

    HIRExprNodeCallMethod(Span sp, HIRExprNodeP val, RcString methodName, HIRPathParams params, std::vector<HIRExprNodeP> args, RcString fallbackMethod = {});

    static constexpr unsigned int kind = 25;
    unsigned int nodeKind() const override;
    void visit(HIRExprVisitor& nv) override;
};

struct HIRExprNodeField: public HIRExprNode {
    HIRExprNodeP value;
    RcString field;

    HIRExprNodeField(Span sp, HIRExprNodeP val, RcString field);

    static constexpr unsigned int kind = 26;
    unsigned int nodeKind() const override;
    void visit(HIRExprVisitor& nv) override;
};

struct HIRExprNodeLiteral: public HIRExprNode {
    using Data = HIRExprLiteral;

    Data data;

    HIRExprNodeLiteral(Span sp, Data data);

    static constexpr unsigned int kind = 27;
    unsigned int nodeKind() const override;
    void visit(HIRExprVisitor& nv) override;
};

struct HIRExprNodeUnitVariant: public HIRExprNode {
    HIRGenericPath path;
    bool isStruct;

    HIRExprNodeUnitVariant(Span sp, HIRGenericPath path, bool isStruct);

    static constexpr unsigned int kind = 28;
    unsigned int nodeKind() const override;
    void visit(HIRExprVisitor& nv) override;
};

struct HIRExprNodePathValue: public HIRExprNode {
    enum Target {
        UNKNOWN,
        FUNCTION,
        STRUCT_CONSTR,
        ENUM_VAR_CONSTR,
        STATIC,
        CONSTANT,
    };

    HIRPath path;
    Target target;

    HIRExprNodePathValue(Span sp, HIRPath path, Target target);

    static constexpr unsigned int kind = 29;
    unsigned int nodeKind() const override;
    void visit(HIRExprVisitor& nv) override;
};

struct HIRExprNodeVariable: public HIRExprNode {
    RcString name;
    unsigned int slot;

    HIRExprNodeVariable(Span sp, RcString name, unsigned int slot);

    static constexpr unsigned int kind = 30;
    unsigned int nodeKind() const override;
    void visit(HIRExprVisitor& nv) override;
};

struct HIRExprNodeConstParam: public HIRExprNode {
    RcString name;
    unsigned int binding;

    HIRExprNodeConstParam(Span sp, RcString name, unsigned int binding);

    static constexpr unsigned int kind = 31;
    unsigned int nodeKind() const override;
    void visit(HIRExprVisitor& nv) override;
};

struct HIRExprNodeStructLiteral: public HIRExprNode {
    typedef std::vector<std::pair<RcString, HIRExprNodeP>> tValues;

    HIRTypeRef type;
    bool isStruct;

    bool useDefaults;

    HIRExprNodeP baseValue;
    tValues values;

    HIRGenericPath realPath;

    std::vector<HIRTypeRef> valueTypes;

    HIRExprNodeStructLiteral(Span sp, HIRTypeRef ty, bool isStruct, HIRExprNodeP baseValue, tValues values);

    HIRExprNodeStructLiteral(Span sp, HIRTypeRef ty, bool isStruct, bool, tValues values);

    static constexpr unsigned int kind = 32;
    unsigned int nodeKind() const override;
    void visit(HIRExprVisitor& nv) override;
};

struct HIRExprNodeTuple: public HIRExprNode {
    std::vector<HIRExprNodeP> vals;

    HIRExprNodeTuple(Span sp, std::vector<HIRExprNodeP> vals);

    static constexpr unsigned int kind = 33;
    unsigned int nodeKind() const override;
    void visit(HIRExprVisitor& nv) override;
};

struct HIRExprNodeArrayList: public HIRExprNode {
    std::vector<HIRExprNodeP> vals;

    HIRExprNodeArrayList(Span sp, std::vector<HIRExprNodeP> vals);

    static constexpr unsigned int kind = 34;
    unsigned int nodeKind() const override;
    void visit(HIRExprVisitor& nv) override;
};

// TODO: Might want a second variant for dynamically-sized arrays
struct HIRExprNodeArraySized: public HIRExprNode {
    HIRExprNodeP val;
    HIRArraySize size;

    HIRExprNodeArraySized(Span sp, HIRExprNodeP val, HIRExprPtr size);
    HIRExprNodeArraySized(Span sp, HIRExprNodeP val, HIRArraySize size);

    static constexpr unsigned int kind = 35;
    unsigned int nodeKind() const override;
    void visit(HIRExprVisitor& nv) override;
};

struct HIRExprNodeClosure: public HIRExprNode {
    typedef std::vector<std::pair<HIRPattern, HIRTypeRef>> argsT;

    argsT args;
    HIRTypeRef returnType;
    HIRExprNodeP code;
    bool isMove = false;
    bool isUse = false;
    bool trackCaller = false;

    enum class Class {
        Unknown,
        NoCapture,
        Shared,
        Mut,
        Once,
    } cls = Class::Unknown;
    bool isCopy = true;

    struct AvuCache {
        std::vector<unsigned int> localVars;

        struct Capture {
            unsigned int rootSlot;

            std::vector<RcString> fields;
            HIRValueUsage usage;
        };

        std::vector<Capture> capturedVars;
    } avuCache;

    const HIRStruct* objPtr = nullptr;
    HIRGenericPath objPathBase;
    HIRGenericPath objPath;
    std::vector<HIRExprNodeP> captures;

    HIRExprNodeClosure(Span sp, argsT args, HIRTypeRef rv, HIRExprNodeP code, bool isMove, bool isUse);

    static constexpr unsigned int kind = 36;
    unsigned int nodeKind() const override;
    void visit(HIRExprVisitor& nv) override;
};

struct HIRExprNodeGenerator: public HIRExprNode {
    HIRTypeRef returnType;
    HIRTypeRef resumeTy;
    HIRPattern resumePattern;
    bool hasResumePattern;
    HIRTypeRef yieldTy;
    HIRExprNodeP code;
    bool isMove;
    bool isPinned;
    bool trackCaller = false;

    bool isCoroutineClosureBody;

    struct AvuCache {
        std::vector<unsigned int> localVars;
        std::vector<std::pair<unsigned int, HIRValueUsage>> capturedVars;
    } avuCache;

    const HIRStruct* objPtr = nullptr;
    HIRGenericPath objPathBase;
    HIRGenericPath objPath;

    std::vector<HIRExprNodeP> captures;

    HIRTypeRef stateDataType;

    HIRExprNodeGenerator(Span sp, HIRTypeRef rv, HIRTypeRef resumeTy, HIRPattern resumePattern, bool hasResumePattern, HIRTypeRef yieldTy, HIRExprNodeP code, bool isMove, bool isPinned, bool isCoroutineClosureBody);

    static constexpr unsigned int kind = 37;
    unsigned int nodeKind() const override;
    void visit(HIRExprVisitor& nv) override;
};

struct HIRExprNodeGeneratorWrapper: public HIRExprNode {
    bool isFuture;

    bool isAsyncGen = false;
    HIRTypeRef returnType;
    HIRTypeRef yieldTy;
    HIRExprNodeP code;

    const HIRStruct* objPtr = nullptr;
    HIRGenericPath objPath;

    HIRTypeRef stateDataType;
    HIRSimplePath stateIdxEnum;

    HIRFunction* dropFcnPtr = nullptr;

    std::vector<HIRValueUsage> captureUsages;

    HIRExprNodeGeneratorWrapper(Span sp, HIRTypeRef rv, HIRTypeRef yieldTy, HIRExprNodeP code, bool isFuture);

    static constexpr unsigned int kind = 38;
    unsigned int nodeKind() const override;
    void visit(HIRExprVisitor& nv) override;
};

struct HIRExprNodeAsyncBlock: public HIRExprNode {
    HIRTypeRef returnType;
    HIRExprNodeP code;
    bool isMove;
    bool isUse;

    bool isAsyncGen = false;
    HIRTypeRef yieldTy;

    HIRExprNodeGenerator::AvuCache avuCache;

    const HIRStruct* objPtr = nullptr;
    HIRGenericPath objPathBase;
    HIRGenericPath objPath;

    std::vector<HIRExprNodeP> captures;

    HIRTypeRef stateDataType;

    HIRExprNodeAsyncBlock(Span sp, HIRTypeRef returnType, HIRExprNodeP code, bool isMove, bool isUse);

    static constexpr unsigned int kind = 39;
    unsigned int nodeKind() const override;
    void visit(HIRExprVisitor& nv) override;
};

class HIRExprVisitor {
public:
    virtual ~HIRExprVisitor() = default;
    virtual void visitNodePtr(HIRExprNodeP& nodePtr);
    virtual void visitNode(HIRExprNode& node);
#define NV(nt) virtual void visit(nt& n) = 0;

    NV(HIRExprNodeBlock)
    NV(HIRExprNodeConstBlock)
    NV(HIRExprNodeAsm)
    NV(HIRExprNodeAsm2)
    NV(HIRExprNodeReturn)
    NV(HIRExprNodeYield)
    NV(HIRExprNodeAWait)
    NV(HIRExprNodeUse)
    NV(HIRExprNodeLet)
    NV(HIRExprNodeLoop)
    NV(HIRExprNodeLoopControl)
    NV(HIRExprNodeMatch)

    NV(HIRExprNodeAssign)
    NV(HIRExprNodeBinOp)
    NV(HIRExprNodeUniOp)
    NV(HIRExprNodeBorrow)
    NV(HIRExprNodeRawBorrow)
    NV(HIRExprNodeCast)
    NV(HIRExprNodeUnsize)
    NV(HIRExprNodeIndex)
    NV(HIRExprNodeDeref)
    NV(HIRExprNodeEmplace)

    NV(HIRExprNodeTupleVariant);
    NV(HIRExprNodeCallPath);
    NV(HIRExprNodeCallValue);
    NV(HIRExprNodeCallMethod);
    NV(HIRExprNodeField);

    NV(HIRExprNodeLiteral);
    NV(HIRExprNodeUnitVariant);
    NV(HIRExprNodePathValue);
    NV(HIRExprNodeVariable);
    NV(HIRExprNodeConstParam);

    NV(HIRExprNodeStructLiteral);
    NV(HIRExprNodeTuple);
    NV(HIRExprNodeArrayList);
    NV(HIRExprNodeArraySized);

    NV(HIRExprNodeClosure);
    NV(HIRExprNodeGenerator);
    NV(HIRExprNodeGeneratorWrapper);
    NV(HIRExprNodeAsyncBlock);
#undef NV
};

class HIRExprVisitorDef: public HIRExprVisitor {
    HIRTypeInterner& types;

public:
    explicit HIRExprVisitorDef(HIRTypeInterner& types);

    HIRTypeInterner& typeInterner() const {
        return types;
    }

#define NV(nt) virtual void visit(nt& n) override;

    virtual void visitNodePtr(HIRExprNodeP& nodePtr) override;

    NV(HIRExprNodeBlock)
    NV(HIRExprNodeConstBlock)

    NV(HIRExprNodeAsm)
    NV(HIRExprNodeAsm2)
    NV(HIRExprNodeReturn)
    NV(HIRExprNodeYield)
    NV(HIRExprNodeAWait)
    NV(HIRExprNodeUse)
    NV(HIRExprNodeLet)
    NV(HIRExprNodeLoop)
    NV(HIRExprNodeLoopControl)
    NV(HIRExprNodeMatch)

    NV(HIRExprNodeAssign)
    NV(HIRExprNodeBinOp)
    NV(HIRExprNodeUniOp)
    NV(HIRExprNodeBorrow)
    NV(HIRExprNodeRawBorrow)
    NV(HIRExprNodeCast)
    NV(HIRExprNodeUnsize)
    NV(HIRExprNodeIndex)
    NV(HIRExprNodeDeref)
    NV(HIRExprNodeEmplace)

    NV(HIRExprNodeTupleVariant);
    NV(HIRExprNodeCallPath);
    NV(HIRExprNodeCallValue);
    NV(HIRExprNodeCallMethod);
    NV(HIRExprNodeField);

    NV(HIRExprNodeLiteral);
    NV(HIRExprNodeUnitVariant);
    NV(HIRExprNodePathValue);
    NV(HIRExprNodeVariable);
    NV(HIRExprNodeConstParam);

    NV(HIRExprNodeStructLiteral);
    NV(HIRExprNodeTuple);
    NV(HIRExprNodeArrayList);
    NV(HIRExprNodeArraySized);

    NV(HIRExprNodeClosure);
    NV(HIRExprNodeGenerator);
    NV(HIRExprNodeGeneratorWrapper);
    NV(HIRExprNodeAsyncBlock);
#undef NV

    virtual void visitPattern(const Span& sp, HIRPattern& pat);

    [[nodiscard]] virtual HIRTypeRef visitType(HIRTypeRef ty);

    void updateType(HIRTypeRef& ty) {
        ty = visitType(ty);
    }

    virtual void visitTraitPath(HIRTraitPath& p);
    virtual void visitPathParams(HIRPathParams& ty);
    virtual void visitPath(HIRVisitor::PathContext pc, HIRPath& ty);
    virtual void visitGenericPath(HIRVisitor::PathContext pc, HIRGenericPath& ty);
};

void HIRDumpExpr(stl::ZeroCopyOutput& sink, const HIRExprPtr& expr);
