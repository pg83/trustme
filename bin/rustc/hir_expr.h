#pragma once

#include "span.h"
#include "hir_asm.h"
#include "hir_type.h"
#include "hir_pattern.h"
#include "hir_visitor.h"
#include "hir_typeck_common.h"

#include <memory>

typedef ::std::vector<::std::pair<const HIRSimplePath*, const HIRTrait*>> tTraitList;

// Indicates how a result is used
enum class HIRValueUsage {
    // Not yet known (defalt state)
    Unknown,
    // Value is borrowed (shared)
    Borrow,
    // Value is mutated or uniquely borrowed
    Mutate,
    // Value is moved
    Move,
};

::std::ostream& operator<<(::std::ostream& os, const HIRValueUsage& x);

class HIRGenericParams;

class HIRExprVisitor;

class HIRExprNode {
public:
    Span span_;
    HIRTypeRef resType; // TODO: Replace this with an index into an ivar table
    //unsigned m_res_type_idx;
    // Evaluation of this expression cannot complete normally.  This is
    // independent of `resType`: e.g. `(return, 0)` still has a tuple type.
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
    ::std::vector<HIRExprNodeP> nodes;
    HIRExprNodeP valueNode; // can be null

    HIRSimplePath localMod;
    tTraitList traits;

    HIRExprNodeBlock(Span sp);

    HIRExprNodeBlock(Span sp, bool isUnsafe, ::std::vector<HIRExprNodeP> nodes, HIRExprNodeP valueNode);

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
        ::std::string spec;
        HIRExprNodeP value;
    };

    ::std::string templateText;
    ::std::vector<ValRef> outputs;
    ::std::vector<ValRef> inputs;
    ::std::vector<::std::string> clobbers;
    ::std::vector<::std::string> flags;

    HIRExprNodeAsm(Span sp, ::std::string tplStr, ::std::vector<ValRef> outputs, ::std::vector<ValRef> inputs, ::std::vector<::std::string> clobbers, ::std::vector<::std::string> flags)
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

struct HIRExprNodeAsm2: public HIRExprNode {
    TAGGED_UNION(
        Param,
        Const,
        (Const, HIRExprNodeP),
        (Sym, HIRPath),
        (Label, struct { HIRExprNodeP code; }),
        (RegSingle,
         struct {
             AsmDirection dir;
             AsmRegisterSpec spec;
             HIRExprNodeP val;
         }),
        (Reg, struct {
            AsmDirection dir;
            AsmRegisterSpec spec;
            HIRExprNodeP valIn;
            HIRExprNodeP valOut;
        })
    );

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

/// @brief `foo = yield bar` generator yield statement
struct HIRExprNodeYield: public HIRExprNode {
    HIRExprNodeP value;

    HIRExprNodeYield(Span sp, HIRExprNodeP value);

    static constexpr unsigned int kind = 6;
    unsigned int nodeKind() const override;
    void visit(HIRExprVisitor& nv) override;
};

/// @brief Async Wait (the `.await` postfix operator)
struct HIRExprNodeAWait: public HIRExprNode {
    HIRExprNodeP value;
    /// Await the next item of an async iterator (`for await`): the value is the
    /// iterator itself, and the result is `Option<Item>`.
    bool isNext = false;

    HIRExprNodeAWait(Span sp, HIRExprNodeP value);

    static constexpr unsigned int kind = 7;
    unsigned int nodeKind() const override;
    void visit(HIRExprVisitor& nv) override;
};

/// @brief Copy, clone, or move (the `.use` postfix operator)
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

    const HIRExprNodeLoop* targetNode; // populated by expr_cs__enum.cpp

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
        /// Guard pattern, always set (but might be `true`/`false`)
        HIRPattern pat;
        /// Guard value
        HIRExprNodeP val;
        /// Indicates that this guard is an `if` (changes scoping rules, and tweaks how typecheck happens)
        bool isIf;
    };

    struct Arm {
        // Patterns, must be non-empty
        ::std::vector<HIRPattern> patterns;
        // A chained (&&) list of guards
        ::std::vector<Guard> guards;
        // Match arm body, required
        HIRExprNodeP code;
    };

    HIRExprNodeP value;
    ::std::vector<Arm> arms;
    bool isLetElse;

    HIRExprNodeMatch(Span sp, HIRExprNodeP val, ::std::vector<Arm> arms, bool isLetElse = false);

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
        Invert, // '!<expr>'
        Negate, // '-<expr>'
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

    /// <summary>
    /// Flag set by the first pass of SBC to both inform the second pass and change Lifetime Infer's behaviour
    /// </summary>
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

// Magical pointer unsizing operation:
// - `&[T; n] -> &[T]`
// - `&T -> &Trait`
// - `Box<T> -> Box<Trait>`
// NOTE: Also used for type ascription
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

// unary `*`
struct HIRExprNodeDeref: public HIRExprNode {
    enum class TraitUsed {
        // Nodes created after type checking can retain the historical
        // structural behaviour. Source dereferences are resolved to one
        // of the two explicit choices below.
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

/// `box` and `in`/`<-`
struct HIRExprNodeEmplace: public HIRExprNode {
    /// This influences the ops trait used
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
    // Path to variant/struct
    HIRGenericPath path;
    bool isStruct;
    ::std::vector<HIRExprNodeP> args;

    // - Cache for typeck
    ::std::vector<HIRTypeRef> argTypes;

    HIRExprNodeTupleVariant(Span sp, HIRGenericPath path, bool isStruct, ::std::vector<HIRExprNodeP> args);

    static constexpr unsigned int kind = 22;
    unsigned int nodeKind() const override;
    void visit(HIRExprVisitor& nv) override;
};

struct HIRExprCallCache {
    ::std::vector<HIRTypeRef> argTypes;
    const HIRGenericParams* fcnParams;
    const HIRGenericParams* topParams;
    const HIRFunction* fcn;

    ::std::unique_ptr<Monomorphiser> monomorph;
};

struct HIRExprNodeCallPath: public HIRExprNode {
    HIRPath path;
    ::std::vector<HIRExprNodeP> args;

    // - Cache for typeck
    HIRExprCallCache cache;

    HIRExprNodeCallPath(Span sp, HIRPath path, ::std::vector<HIRExprNodeP> args);

    static constexpr unsigned int kind = 23;
    unsigned int nodeKind() const override;
    void visit(HIRExprVisitor& nv) override;
};

struct HIRExprNodeCallValue: public HIRExprNode {
    HIRExprNodeP value;
    ::std::vector<HIRExprNodeP> args;

    // - Argument types used as coercion targets
    ::std::vector<HIRTypeRef> argIvars;

    // - Cache for typeck
    ::std::vector<HIRTypeRef> argTypes;

    // Indicates what trait should/is being used for this call
    // - Determined by typeck using the present trait bound (also adds borrows etc)
    // - If the called value is a closure, this stays a Unknown until closure expansion
    enum class TraitUsed {
        Unknown,
        Fn,
        FnMut,
        FnOnce,
    };
    TraitUsed traitUsed = TraitUsed::Unknown;

    HIRExprNodeCallValue(Span sp, HIRExprNodeP val, ::std::vector<HIRExprNodeP> args);

    static constexpr unsigned int kind = 24;
    unsigned int nodeKind() const override;
    void visit(HIRExprVisitor& nv) override;
};

// TODO: Refactor to support efficient method chaining
struct HIRExprNodeCallMethod: public HIRExprNode {
    /// @brief Method reciever value
    HIRExprNodeP value;
    /// @brief Method name
    RcString method;
    /// @brief Generic parameters to the method
    HIRPathParams params;
    /// @brief Argument values
    ::std::vector<HIRExprNodeP> args;

    // - Set during typeck to the real path to the method
    HIRPath methodPath;
    // - Cache of argument/return types
    HIRExprCallCache cache;

    // - List of possible traits (in-scope traits that contain this method)
    tTraitList traits;
    // - A pool of ivars to use for searching for trait impls, with type
    // ivars first and const value ivars after them.
    ::std::vector<unsigned int> traitParamIvars;
    unsigned int traitParamTypeIvars = 0;

    HIRExprNodeCallMethod(Span sp, HIRExprNodeP val, RcString methodName, HIRPathParams params, ::std::vector<HIRExprNodeP> args);

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
    TAGGED_UNION(
        Data,
        Integer,
        (Integer,
         struct {
             HIRCoreType type; // if not an integer type, it's unknown
             U128 value;
         }),
        (Float,
         struct {
             HIRCoreType type; // If not a float type, it's unknown
             FloatValue value;
         }),
        (Boolean, bool),
        (String, ::std::string),
        (CString, struct { ::std::string v; }),
        (ByteString, ::std::vector<char>)
    );

    Data data;

    HIRExprNodeLiteral(Span sp, Data data);

    static constexpr unsigned int kind = 27;
    unsigned int nodeKind() const override;
    void visit(HIRExprVisitor& nv) override;
};

struct HIRExprNodeUnitVariant: public HIRExprNode {
    // Path to variant/struct
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
    typedef ::std::vector<::std::pair<RcString, HIRExprNodeP>> tValues;

    HIRTypeRef type;
    bool isStruct;
    /// Alternative to `m_base_value`, indicates that the struct's field defaults are to be used
    bool useDefaults;
    /// Base value (`..foo`)
    HIRExprNodeP baseValue;
    tValues values;

    /// Actual path extracted from the ASTType* (populated after inner UFCS expansion)
    HIRGenericPath realPath;
    /// Monomorphised types of each field.
    ::std::vector<HIRTypeRef> valueTypes;

    HIRExprNodeStructLiteral(Span sp, HIRTypeRef ty, bool isStruct, HIRExprNodeP baseValue, tValues values);

    HIRExprNodeStructLiteral(Span sp, HIRTypeRef ty, bool isStruct, bool, tValues values);

    static constexpr unsigned int kind = 32;
    unsigned int nodeKind() const override;
    void visit(HIRExprVisitor& nv) override;
};

struct HIRExprNodeTuple: public HIRExprNode {
    ::std::vector<HIRExprNodeP> vals;

    HIRExprNodeTuple(Span sp, ::std::vector<HIRExprNodeP> vals);

    static constexpr unsigned int kind = 33;
    unsigned int nodeKind() const override;
    void visit(HIRExprVisitor& nv) override;
};

struct HIRExprNodeArrayList: public HIRExprNode {
    ::std::vector<HIRExprNodeP> vals;

    HIRExprNodeArrayList(Span sp, ::std::vector<HIRExprNodeP> vals);

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
    typedef ::std::vector<::std::pair<HIRPattern, HIRTypeRef>> argsT;

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
    bool isCopy = true; // Assume that closures are Copy/Clone (for the purposes of typecheck) until AVU is run

    // - Cache between the AVU and ExpandClosures passes
    struct AvuCache {
        ::std::vector<unsigned int> localVars;

        struct Capture {
            // Variable binding index
            unsigned int rootSlot;
            // Fields used to access that variable
            std::vector<RcString> fields;
            HIRValueUsage usage;
        };

        ::std::vector<Capture> capturedVars;
    } avuCache;

    // - Path to the generated closure type
    const HIRStruct* objPtr = nullptr;
    HIRGenericPath objPathBase;
    HIRGenericPath objPath;
    ::std::vector<HIRExprNodeP> captures;

    HIRExprNodeClosure(Span sp, argsT args, HIRTypeRef rv, HIRExprNodeP code, bool isMove, bool isUse);

    static constexpr unsigned int kind = 36;
    unsigned int nodeKind() const override;
    void visit(HIRExprVisitor& nv) override;
};

::std::ostream& operator<<(::std::ostream& os, const HIRExprNodeClosure::AvuCache::Capture& x);

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
    // Move parent locals, but capture parent upvars through the parent closure.
    bool isCoroutineClosureBody;

    // AnnotateValueUsage cache/information
    struct AvuCache {
        ::std::vector<unsigned int> localVars;
        ::std::vector<::std::pair<unsigned int, HIRValueUsage>> capturedVars;
    } avuCache;

    // Generated type information
    const HIRStruct* objPtr = nullptr;
    HIRGenericPath objPathBase;
    HIRGenericPath objPath;
    // Captured variables (used for emitting the constructor)
    ::std::vector<HIRExprNodeP> captures;
    // State data type (needed for initialising)
    HIRTypeRef stateDataType;

    HIRExprNodeGenerator(Span sp, HIRTypeRef rv, HIRTypeRef resumeTy, HIRPattern resumePattern, bool hasResumePattern, HIRTypeRef yieldTy, HIRExprNodeP code, bool isMove, bool isPinned, bool isCoroutineClosureBody);

    static constexpr unsigned int kind = 37;
    unsigned int nodeKind() const override;
    void visit(HIRExprVisitor& nv) override;
};

/// <summary>
/// Top-level wrapper for the generator method
/// </summary>
struct HIRExprNodeGeneratorWrapper: public HIRExprNode {
    //ExprNodeClosure::args_t    m_args;
    bool isFuture;
    /// The future is an `async gen` body: it returns `Poll<Option<Item>>`, and
    /// a `yield` returns from it just as an `await` does.
    bool isAsyncGen = false;
    HIRTypeRef returnType;
    HIRTypeRef yieldTy;
    HIRExprNodeP code;

    // Generated type information
    const HIRStruct* objPtr = nullptr;
    HIRGenericPath objPath;

    HIRTypeRef stateDataType;
    HIRSimplePath stateIdxEnum;

    HIRFunction* dropFcnPtr = nullptr;

    ::std::vector<HIRValueUsage> captureUsages;

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

    /// `async gen`: the block yields items instead of resolving to one value,
    /// so it is an AsyncIterator and not a Future. `returnType` is then unit
    /// and `yieldTy` is the item type.
    bool isAsyncGen = false;
    HIRTypeRef yieldTy;

    HIRExprNodeGenerator::AvuCache avuCache;

    // Generated type information
    const HIRStruct* objPtr = nullptr;
    HIRGenericPath objPathBase;
    HIRGenericPath objPath;
    // Captured variables (used for emitting the constructor)
    ::std::vector<HIRExprNodeP> captures;
    // State data type (needed for initialising)
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
    NV(HIRExprNodeCast)   // Conversion
    NV(HIRExprNodeUnsize) // Coercion
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
    //NV(ExprNodeAsyncBlock)
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
    virtual void visitType(HIRTypeRef& ty);
    virtual void visitTraitPath(HIRTraitPath& p);
    virtual void visitPathParams(HIRPathParams& ty);
    virtual void visitPath(HIRVisitor::PathContext pc, HIRPath& ty);
    virtual void visitGenericPath(HIRVisitor::PathContext pc, HIRGenericPath& ty);
};

void HIRDumpExpr(::std::ostream& sink, const HIRExprPtr& expr);
