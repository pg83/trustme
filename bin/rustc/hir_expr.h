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
    Span mSpan;
    HIRTypeRef resType; // TODO: Replace this with an index into an ivar table
    //unsigned m_res_type_idx;
    HIRValueUsage usage = HIRValueUsage::Unknown;

    const Span& span() const {
        return mSpan;
    }

    virtual void visit(HIRExprVisitor& v) = 0;
    virtual unsigned int nodeKind() const = 0;

    HIRExprNode(Span sp);

    virtual ~HIRExprNode();

    const char* typeName() const;
};

struct HIRExprNodeBlock: public HIRExprNode {
    bool mIsUnsafe;
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
    std::vector<Param> mParams;

    HIRExprNodeAsm2(Span sp, AsmOptions options, std::vector<AsmLine> lines, std::vector<Param> params);

    static constexpr unsigned int kind = 4;
    unsigned int nodeKind() const override;
    void visit(HIRExprVisitor& nv) override;
};

struct HIRExprNodeReturn: public HIRExprNode {
    HIRExprNodeP mValue;

    HIRExprNodeReturn(Span sp, HIRExprNodeP value);

    static constexpr unsigned int kind = 5;
    unsigned int nodeKind() const override;
    void visit(HIRExprVisitor& nv) override;
};

/// @brief `foo = yield bar` generator yield statement
struct HIRExprNodeYield: public HIRExprNode {
    HIRExprNodeP mValue;

    HIRExprNodeYield(Span sp, HIRExprNodeP value);

    static constexpr unsigned int kind = 6;
    unsigned int nodeKind() const override;
    void visit(HIRExprVisitor& nv) override;
};

/// @brief Async Wait (the `.await` postfix operator)
struct HIRExprNodeAWait: public HIRExprNode {
    HIRExprNodeP mValue;

    HIRExprNodeAWait(Span sp, HIRExprNodeP value);

    static constexpr unsigned int kind = 7;
    unsigned int nodeKind() const override;
    void visit(HIRExprVisitor& nv) override;
};

struct HIRExprNodeLoop: public HIRExprNode {
    RcString label;
    HIRExprNodeP mCode;
    bool diverges = false;
    bool requireLabel = false;

    HIRExprNodeLoop(Span sp, RcString label, HIRExprNodeP code, bool requireLabel = false);

    static constexpr unsigned int kind = 8;
    unsigned int nodeKind() const override;
    void visit(HIRExprVisitor& nv) override;
};

struct HIRExprNodeLoopControl: public HIRExprNode {
    RcString label;
    bool isContinue;
    HIRExprNodeP mValue;

    const HIRExprNodeLoop* targetNode; // populated by expr_cs__enum.cpp

    HIRExprNodeLoopControl(Span sp, RcString label, bool cont, HIRExprNodeP value = {});

    static constexpr unsigned int kind = 9;
    unsigned int nodeKind() const override;
    void visit(HIRExprVisitor& nv) override;
};

struct HIRExprNodeLet: public HIRExprNode {
    HIRPattern pattern;
    HIRTypeRef mType;
    HIRExprNodeP mValue;
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
        HIRExprNodeP mCode;
    };

    HIRExprNodeP mValue;
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
    HIRExprNodeP mValue;

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
    HIRExprNodeP mValue;

    HIRExprNodeUniOp(Span sp, Op op, HIRExprNodeP value);

    static constexpr unsigned int kind = 14;
    unsigned int nodeKind() const override;
    void visit(HIRExprVisitor& nv) override;
};

struct HIRExprNodeBorrow: public HIRExprNode {
    HIRBorrowType mType;
    HIRExprNodeP mValue;

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
    HIRBorrowType mType;
    HIRExprNodeP mValue;

    HIRExprNodeRawBorrow(Span sp, HIRBorrowType bt, HIRExprNodeP value);

    static constexpr unsigned int kind = 16;
    unsigned int nodeKind() const override;
    void visit(HIRExprVisitor& nv) override;
};

struct HIRExprNodeCast: public HIRExprNode {
    HIRExprNodeP mValue;
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
    HIRExprNodeP mValue;
    HIRTypeRef dstType;
    bool isArrayToSliceAdjustment = false;

    HIRExprNodeUnsize(Span sp, HIRExprNodeP value, HIRTypeRef dstType);

    static constexpr unsigned int kind = 18;
    unsigned int nodeKind() const override;
    void visit(HIRExprVisitor& nv) override;
};

struct HIRExprNodeIndex: public HIRExprNode {
    HIRExprNodeP mValue;
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

    HIRExprNodeP mValue;
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

    Type mType;
    HIRExprNodeP place;
    HIRExprNodeP mValue;

    HIRExprNodeEmplace(Span sp, Type ty, HIRExprNodeP place, HIRExprNodeP val);

    static constexpr unsigned int kind = 21;
    unsigned int nodeKind() const override;
    void visit(HIRExprVisitor& nv) override;
};

struct HIRExprNodeTupleVariant: public HIRExprNode {
    // Path to variant/struct
    HIRGenericPath mPath;
    bool isStruct;
    ::std::vector<HIRExprNodeP> mArgs;

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
    HIRPath mPath;
    ::std::vector<HIRExprNodeP> mArgs;

    // - Cache for typeck
    HIRExprCallCache cache;

    HIRExprNodeCallPath(Span sp, HIRPath path, ::std::vector<HIRExprNodeP> args);

    static constexpr unsigned int kind = 23;
    unsigned int nodeKind() const override;
    void visit(HIRExprVisitor& nv) override;
};

struct HIRExprNodeCallValue: public HIRExprNode {
    HIRExprNodeP mValue;
    ::std::vector<HIRExprNodeP> mArgs;

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
    HIRExprNodeP mValue;
    /// @brief Method name
    RcString method;
    /// @brief Generic parameters to the method
    HIRPathParams mParams;
    /// @brief Argument values
    ::std::vector<HIRExprNodeP> mArgs;

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
    HIRExprNodeP mValue;
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
             HIRCoreType mType; // if not an integer type, it's unknown
             U128 mValue;
         }),
        (Float,
         struct {
             HIRCoreType mType; // If not a float type, it's unknown
             FloatValue mValue;
         }),
        (Boolean, bool),
        (String, ::std::string),
        (CString, struct { ::std::string v; }),
        (ByteString, ::std::vector<char>)
    );

    Data mData;

    HIRExprNodeLiteral(Span sp, Data data);

    static constexpr unsigned int kind = 27;
    unsigned int nodeKind() const override;
    void visit(HIRExprVisitor& nv) override;
};

struct HIRExprNodeUnitVariant: public HIRExprNode {
    // Path to variant/struct
    HIRGenericPath mPath;
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

    HIRPath mPath;
    Target target;

    HIRExprNodePathValue(Span sp, HIRPath path, Target target);

    static constexpr unsigned int kind = 29;
    unsigned int nodeKind() const override;
    void visit(HIRExprVisitor& nv) override;
};

struct HIRExprNodeVariable: public HIRExprNode {
    RcString mName;
    unsigned int slot;

    HIRExprNodeVariable(Span sp, RcString name, unsigned int slot);

    static constexpr unsigned int kind = 30;
    unsigned int nodeKind() const override;
    void visit(HIRExprVisitor& nv) override;
};

struct HIRExprNodeConstParam: public HIRExprNode {
    RcString mName;
    unsigned int mBinding;

    HIRExprNodeConstParam(Span sp, RcString name, unsigned int binding);

    static constexpr unsigned int kind = 31;
    unsigned int nodeKind() const override;
    void visit(HIRExprVisitor& nv) override;
};

struct HIRExprNodeStructLiteral: public HIRExprNode {
    typedef ::std::vector<::std::pair<RcString, HIRExprNodeP>> tValues;

    HIRTypeRef mType;
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
    HIRArraySize mSize;

    HIRExprNodeArraySized(Span sp, HIRExprNodeP val, HIRExprPtr size);

    static constexpr unsigned int kind = 35;
    unsigned int nodeKind() const override;
    void visit(HIRExprVisitor& nv) override;
};

struct HIRExprNodeClosure: public HIRExprNode {
    typedef ::std::vector<::std::pair<HIRPattern, HIRTypeRef>> argsT;

    argsT mArgs;
    HIRTypeRef returnType;
    HIRExprNodeP mCode;
    bool isMove = false;

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

    HIRExprNodeClosure(Span sp, argsT args, HIRTypeRef rv, HIRExprNodeP code, bool isMove);

    static constexpr unsigned int kind = 36;
    unsigned int nodeKind() const override;
    void visit(HIRExprVisitor& nv) override;
};

::std::ostream& operator<<(::std::ostream& os, const HIRExprNodeClosure::AvuCache::Capture& x);

struct HIRExprNodeGenerator: public HIRExprNode {
    //ExprNodeClosure::args_t    m_args;
    HIRTypeRef returnType;
    HIRTypeRef resumeTy;
    HIRTypeRef yieldTy;
    HIRExprNodeP mCode;
    bool isMove;
    bool isPinned;

    // AnnotateValueUsage cache/information
    struct AvuCache {
        ::std::vector<unsigned int> localVars;
        ::std::vector<::std::pair<unsigned int, HIRValueUsage>> capturedVars;
    } avuCache;

    // Generated type information
    const HIRStruct* objPtr = nullptr;
    HIRGenericPath objPath;
    // Captured variables (used for emitting the constructor)
    ::std::vector<HIRExprNodeP> captures;
    // State data type (needed for initialising)
    HIRTypeRef stateDataType;

    HIRExprNodeGenerator(Span sp, HIRTypeRef rv, HIRTypeRef resumeTy, HIRTypeRef yieldTy, HIRExprNodeP code, bool isMove, bool isPinned);

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
    HIRTypeRef returnType;
    HIRTypeRef yieldTy;
    HIRExprNodeP mCode;

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
    HIRExprNodeP mCode;
    bool isMove;

    HIRExprNodeGenerator::AvuCache avuCache;

    // Generated type information
    const HIRStruct* objPtr = nullptr;
    HIRGenericPath objPath;
    // Captured variables (used for emitting the constructor)
    ::std::vector<HIRExprNodeP> captures;
    // State data type (needed for initialising)
    HIRTypeRef stateDataType;

    HIRExprNodeAsyncBlock(Span sp, HIRExprNodeP code, bool isMove);

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
