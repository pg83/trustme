#pragma once

#include "hir_asm.h"
#include "ast_attrs.h"
#include "ast_types.h"
#include "ast_pattern.h"
#include "ast_expr_ptr.h"
#include "parse_tokentree.h"

#include <memory> // unique_ptr
#include <vector>
#include <ostream>

class ASTPattern;
class ASTNodeVisitor;

class ASTExprNode {
    ASTAttributeList attrs_;
    Span span_;

public:
    virtual ~ASTExprNode() = 0;

    virtual void visit(ASTNodeVisitor& nv) = 0;
    virtual void print(::std::ostream& os) const = 0;
    virtual ASTExprNodeP clone() const = 0;
    virtual unsigned int nodeKind() const = 0;

    void setSpan(Span s) {
        span_ = ::std::move(s);
    }

    const Span& span() const {
        return span_;
    }

    void setAttrs(ASTAttributeList&& mi);

    ASTAttributeList& attrs() {
        return attrs_;
    }
};

struct ASTExprNodeBlock: public ASTExprNode {
    enum class Type {
        Bare,
        Unsafe,
        Const,
    };
    Type blockType;
    Ident label;
    ::std::shared_ptr<ASTModule> localMod;

    struct Line {
        bool hasSemicolon;
        ASTExprNodeP node;
    };

    ::std::vector<Line> nodes;

    ASTExprNodeBlock(::std::vector<Line> nodes = {});

    /// Shortcut for a block that returns a contained node
    ASTExprNodeBlock(ASTExprNodeP value);

    ASTExprNodeBlock(Type type, ::std::vector<Line> nodes, ::std::shared_ptr<ASTModule> localMod);

    void pushStmt(ASTExprNodeP node) {
        nodes.push_back({true, std::move(node)});
    }

    void pushTailExpr(ASTExprNodeP node) {
        nodes.push_back({false, std::move(node)});
    }

    static constexpr unsigned int kind = 1;
    unsigned int nodeKind() const override;
    void visit(ASTNodeVisitor& nv) override;
    void print(::std::ostream& os) const override;
    ASTExprNodeP clone() const override;
};

struct ASTExprNodeAsyncBlock: public ASTExprNode {
    ASTExprNodeP inner;
    bool isMove;
    bool isUse;    //< The block copies, clones, or moves each captured value

    ASTExprNodeAsyncBlock(ASTExprNodeP inner, bool isMove, bool isUse);

    static constexpr unsigned int kind = 2;
    unsigned int nodeKind() const override;
    void visit(ASTNodeVisitor& nv) override;
    void print(::std::ostream& os) const override;
    ASTExprNodeP clone() const override;
};

struct ASTExprNodeGeneratorBlock: public ASTExprNode {
    ASTExprNodeP inner;
    ASTType* returnType;
    bool isMove;
    // The inner coroutine synthesized for a coroutine-closure (`iter!`).
    bool isCoroutineClosureBody;
    /// `async gen`: the coroutine also awaits, and yields into an AsyncIterator.
    bool isAsync;

    ASTExprNodeGeneratorBlock(ASTExprNodeP inner, ASTType* returnType, bool isMove, bool isCoroutineClosureBody, bool isAsync = false);

    static constexpr unsigned int kind = 3;
    unsigned int nodeKind() const override;
    void visit(ASTNodeVisitor& nv) override;
    void print(::std::ostream& os) const override;
    ASTExprNodeP clone() const override;
};

struct ASTExprNodeTry: public ASTExprNode {
    ASTExprNodeP inner;

    ASTExprNodeTry(ASTExprNodeP inner);

    static constexpr unsigned int kind = 4;
    unsigned int nodeKind() const override;
    void visit(ASTNodeVisitor& nv) override;
    void print(::std::ostream& os) const override;
    ASTExprNodeP clone() const override;
};

struct ASTExprNodeMacro: public ASTExprNode {
    ASTPath path;
    RcString ident;
    ::TokenTree tokens;
    bool isBraced;
    Ident::Hygiene definitionHygiene;

    ASTExprNodeMacro(ASTPath name, RcString ident, ::TokenTree&& tokens, bool isBraced = false, Ident::Hygiene definitionHygiene = {});

    static constexpr unsigned int kind = 5;
    unsigned int nodeKind() const override;
    void visit(ASTNodeVisitor& nv) override;
    void print(::std::ostream& os) const override;
    ASTExprNodeP clone() const override;
};

// llvm_asm! macro
struct ASTExprNodeAsm: public ASTExprNode {
    struct ValRef {
        ::std::string name;
        ASTExprNodeP value;
    };

    ::std::string text;
    ::std::vector<ValRef> output;
    ::std::vector<ValRef> input;
    ::std::vector<::std::string> clobbers;
    ::std::vector<::std::string> flags;

    ASTExprNodeAsm(::std::string text, ::std::vector<ValRef> output, ::std::vector<ValRef> input, ::std::vector<::std::string> clobbers, ::std::vector<::std::string> flags);

    static constexpr unsigned int kind = 6;
    unsigned int nodeKind() const override;
    void visit(ASTNodeVisitor& nv) override;
    void print(::std::ostream& os) const override;
    ASTExprNodeP clone() const override;
};

// asm! macro
struct ASTExprNodeAsm2: public ASTExprNode {
    TAGGED_UNION(
        Param,
        Const,
        (Const, ASTExprNodeP),
        (Sym, ASTPath),
        (Label, struct { ASTExprNodeP code; }),
        (RegSingle,
         struct {
             AsmDirection dir;
             AsmRegisterSpec spec;
             ASTExprNodeP val;
         }),
        (Reg, struct {
            AsmDirection dir;
            AsmRegisterSpec spec;
            ASTExprNodeP valIn;
            ASTExprNodeP valOut;
        })
    );

    AsmOptions options;
    std::vector<AsmLine> lines;
    std::vector<Param> params;

    ASTExprNodeAsm2(AsmOptions options, std::vector<AsmLine> lines, std::vector<Param> params);

    static constexpr unsigned int kind = 7;
    unsigned int nodeKind() const override;
    void visit(ASTNodeVisitor& nv) override;
    void print(::std::ostream& os) const override;
    ASTExprNodeP clone() const override;
};

// Break/Continue/Return
struct ASTExprNodeFlow: public ASTExprNode {
    enum Type {
        RETURN,
        TAILCALL,
        YIELD,
        CONTINUE,
        BREAK,
        // `do yeet value` - a failed `?`
        YEET,
    } type;

    Ident target;
    ASTExprNodeP value;

    ASTExprNodeFlow(Type type, Ident target, ASTExprNodeP value);

    static constexpr unsigned int kind = 8;
    unsigned int nodeKind() const override;
    void visit(ASTNodeVisitor& nv) override;
    void print(::std::ostream& os) const override;
    ASTExprNodeP clone() const override;
};

struct ASTExprNodeLetBinding: public ASTExprNode {
    ASTPattern pat;
    ASTType* type;
    ASTExprNodeP value;
    ASTExprNodeP elseNode;
    bool isSuper;
    /// Allocated binding slots/indexes for the pattern in `let-else`
    ::std::pair<unsigned, unsigned> letelseSlots;

    ASTExprNodeLetBinding(ASTPattern pat, ASTType* type, ASTExprNodeP value, ASTExprNodeP elseArm = {}, bool isSuper = false);

    static constexpr unsigned int kind = 9;
    unsigned int nodeKind() const override;
    void visit(ASTNodeVisitor& nv) override;
    void print(::std::ostream& os) const override;
    ASTExprNodeP clone() const override;
};

struct ASTExprNodeAssign: public ASTExprNode {
    enum Operation {
        NONE,
        ADD,
        SUB,
        MUL,
        DIV,
        MOD,
        AND,
        OR,
        XOR,
        SHR,
        SHL,
    } op;

    ASTExprNodeP slot;
    ASTExprNodeP value;

    ASTExprNodeAssign();

    ASTExprNodeAssign(Operation op, ASTExprNodeP slot, ASTExprNodeP value);

    static constexpr unsigned int kind = 10;
    unsigned int nodeKind() const override;
    void visit(ASTNodeVisitor& nv) override;
    void print(::std::ostream& os) const override;
    ASTExprNodeP clone() const override;
};

struct ASTExprNodeCallPath: public ASTExprNode {
    ASTPath path;
    ::std::vector<ASTExprNodeP> args;

    ASTExprNodeCallPath(ASTPath&& path, ::std::vector<ASTExprNodeP>&& args);

    static constexpr unsigned int kind = 11;
    unsigned int nodeKind() const override;
    void visit(ASTNodeVisitor& nv) override;
    void print(::std::ostream& os) const override;
    ASTExprNodeP clone() const override;
};

struct ASTExprNodeCallMethod: public ASTExprNode {
    ASTExprNodeP val;
    ASTPathNode method;
    ::std::vector<ASTExprNodeP> args;

    ASTExprNodeCallMethod(ASTExprNodeP obj, ASTPathNode method, ::std::vector<ASTExprNodeP> args);

    static constexpr unsigned int kind = 12;
    unsigned int nodeKind() const override;
    void visit(ASTNodeVisitor& nv) override;
    void print(::std::ostream& os) const override;
    ASTExprNodeP clone() const override;
};

// Call an object (Fn/FnMut/FnOnce)
struct ASTExprNodeCallObject: public ASTExprNode {
    ASTExprNodeP val;
    ::std::vector<ASTExprNodeP> args;

    ASTExprNodeCallObject(ASTExprNodeP val, ::std::vector<ASTExprNodeP>&& args);

    static constexpr unsigned int kind = 13;
    unsigned int nodeKind() const override;
    void visit(ASTNodeVisitor& nv) override;
    void print(::std::ostream& os) const override;
    ASTExprNodeP clone() const override;
};

struct ASTExprNodeLoop: public ASTExprNode {
    Ident label;
    ASTExprNodeP code;

    ASTExprNodeLoop();

    ASTExprNodeLoop(Ident label, ASTExprNodeP code);

    static constexpr unsigned int kind = 14;
    unsigned int nodeKind() const override;
    void visit(ASTNodeVisitor& nv) override;
    void print(::std::ostream& os) const override;
    ASTExprNodeP clone() const override;
};

struct ASTExprNodeFor: public ASTExprNode {
    Ident label;
    ASTPattern pattern;
    ASTExprNodeP value;
    ASTExprNodeP code;
    /// `for await`: the head is an async iterator, and each item is awaited.
    bool isAwait;

    ASTExprNodeFor(Ident label, ASTPattern pattern, ASTExprNodeP val, ASTExprNodeP code, bool isAwait = false);

    static constexpr unsigned int kind = 15;
    unsigned int nodeKind() const override;
    void visit(ASTNodeVisitor& nv) override;
    void print(::std::ostream& os) const override;
    ASTExprNodeP clone() const override;
};

struct ASTIfLetCondition {
    ::std::unique_ptr<ASTPattern> optPat;
    ASTExprNodeP value;
};

struct ASTExprNodeWhile: public ASTExprNode {
    Ident label;
    std::vector<ASTIfLetCondition> conditions;
    ASTExprNodeP code;

    ASTExprNodeWhile(Ident label, std::vector<ASTIfLetCondition> conditions, ASTExprNodeP code);

    static constexpr unsigned int kind = 16;
    unsigned int nodeKind() const override;
    void visit(ASTNodeVisitor& nv) override;
    void print(::std::ostream& os) const override;
    ASTExprNodeP clone() const override;
};

struct ASTExprNodeMatchArm {
    ASTAttributeList attrs;
    ::std::vector<ASTPattern> patterns;
    std::vector<ASTIfLetCondition> guard;

    ASTExprNodeP code;

    ASTExprNodeMatchArm();

    ASTExprNodeMatchArm(::std::vector<ASTPattern> patterns, std::vector<ASTIfLetCondition> guard, ASTExprNodeP code);
};

struct ASTExprNodeMatch: public ASTExprNode {
    ASTExprNodeP val;
    ::std::vector<ASTExprNodeMatchArm> arms;

    ASTExprNodeMatch(ASTExprNodeP val, ::std::vector<ASTExprNodeMatchArm> arms);

    static constexpr unsigned int kind = 17;
    unsigned int nodeKind() const override;
    void visit(ASTNodeVisitor& nv) override;
    void print(::std::ostream& os) const override;
    ASTExprNodeP clone() const override;
};

struct ASTExprNodeIf: public ASTExprNode {
    struct Arm {
        std::vector<ASTIfLetCondition> conditions;
        ASTExprNodeP body;
    };

    std::vector<Arm> arms;
    ASTExprNodeP elseNode;

    ASTExprNodeIf(std::vector<Arm> arms, ASTExprNodeP elseCode);

    static constexpr unsigned int kind = 18;
    unsigned int nodeKind() const override;
    void visit(ASTNodeVisitor& nv) override;
    void print(::std::ostream& os) const override;
    ASTExprNodeP clone() const override;
};

/// Represents `_` in expression position
struct ASTExprNodeWildcardPattern: public ASTExprNode {
    static constexpr unsigned int kind = 19;
    unsigned int nodeKind() const override;
    void visit(ASTNodeVisitor& nv) override;
    void print(::std::ostream& os) const override;
    ASTExprNodeP clone() const override;
};

// Literal integer
struct ASTExprNodeInteger: public ASTExprNode {
    enum eCoreType datatype;
    U128 value;

    ASTExprNodeInteger(U128 value, enum eCoreType datatype);

    static constexpr unsigned int kind = 20;
    unsigned int nodeKind() const override;
    void visit(ASTNodeVisitor& nv) override;
    void print(::std::ostream& os) const override;
    ASTExprNodeP clone() const override;
};

// Literal float
struct ASTExprNodeFloat: public ASTExprNode {
    enum eCoreType datatype;
    FloatValue value;

    ASTExprNodeFloat(FloatValue value, enum eCoreType datatype);

    static constexpr unsigned int kind = 21;
    unsigned int nodeKind() const override;
    void visit(ASTNodeVisitor& nv) override;
    void print(::std::ostream& os) const override;
    ASTExprNodeP clone() const override;
};

// Literal boolean
struct ASTExprNodeBool: public ASTExprNode {
    bool value;

    ASTExprNodeBool(bool value);

    static constexpr unsigned int kind = 22;
    unsigned int nodeKind() const override;
    void visit(ASTNodeVisitor& nv) override;
    void print(::std::ostream& os) const override;
    ASTExprNodeP clone() const override;
};

// Literal string
struct ASTExprNodeString: public ASTExprNode {
    ::std::string value;
    /// Hygiene for format strings
    Ident::Hygiene hygiene;

    ASTExprNodeString(::std::string value, Ident::Hygiene h = {});

    static constexpr unsigned int kind = 23;
    unsigned int nodeKind() const override;
    void visit(ASTNodeVisitor& nv) override;
    void print(::std::ostream& os) const override;
    ASTExprNodeP clone() const override;
};

// Literal byte string
struct ASTExprNodeByteString: public ASTExprNode {
    ::std::string value;

    ASTExprNodeByteString(::std::string value);

    static constexpr unsigned int kind = 24;
    unsigned int nodeKind() const override;
    void visit(ASTNodeVisitor& nv) override;
    void print(::std::ostream& os) const override;
    ASTExprNodeP clone() const override;
};

// Literal C string
struct ASTExprNodeCString: public ASTExprNode {
    ::std::string value;

    ASTExprNodeCString(::std::string value);

    static constexpr unsigned int kind = 25;
    unsigned int nodeKind() const override;
    void visit(ASTNodeVisitor& nv) override;
    void print(::std::ostream& os) const override;
    ASTExprNodeP clone() const override;
};

// A literal carrying a suffix that names no type: `0invalidSuffix`, `2.0f80`.
// The token is well-formed, so it parses; only lowering rejects it, which is
// what lets it sit inside `#[cfg(false)]` code.
struct ASTExprNodeSuffixedLiteral: public ASTExprNode {
    ::std::string text;

    ASTExprNodeSuffixedLiteral(::std::string text);

    static constexpr unsigned int kind = 40;
    unsigned int nodeKind() const override;
    void visit(ASTNodeVisitor& nv) override;
    void print(::std::ostream& os) const override;
    ASTExprNodeP clone() const override;
};

// Closure / Lambda
struct ASTExprNodeClosure: public ASTExprNode {
    typedef ::std::vector<::std::pair<ASTPattern, ASTType*>> argsT;

    argsT args;
    ASTType* returnType;
    ASTExprNodeP code;
    bool isMove;   //< The closure takes ownership of all values
    bool isUse;    //< The closure copies, clones, or moves each captured value
    bool isPinned; //< The closure cannot be moved (this is for generators)
    bool trackCaller;
    /// `for<'a> |x: &'a u8| ...` — lifetimes bound by the closure itself
    ASTHigherRankedBounds hrbs;

    ASTExprNodeClosure(argsT args, ASTType* rv, ASTExprNodeP code, bool isMove, bool isUse, bool isPinned, bool trackCaller = false)
        : args(::std::move(args))
        , returnType(::std::move(rv))
        , code(::std::move(code))
        , isMove(isMove)
        , isUse(isUse)
        , isPinned(isPinned)
        , trackCaller(trackCaller)
    {
    }

    static constexpr unsigned int kind = 26;
    unsigned int nodeKind() const override;
    void visit(ASTNodeVisitor& nv) override;
    void print(::std::ostream& os) const override;
    ASTExprNodeP clone() const override;
};

// Literal structure
struct ASTExprNodeStructLiteral: public ASTExprNode {
    struct Ent {
        ASTAttributeList attrs;
        RcString name;
        ASTExprNodeP value;
    };

    typedef ::std::vector<Ent> tValues;
    ASTPath path;
    ASTExprNodeP baseValue;
    tValues values;

    ASTExprNodeStructLiteral(ASTPath path, ASTExprNodeP baseValue, tValues&& values);

    static constexpr unsigned int kind = 27;
    unsigned int nodeKind() const override;
    void visit(ASTNodeVisitor& nv) override;
    void print(::std::ostream& os) const override;
    ASTExprNodeP clone() const override;
};

// Struct literal pattern only
// This implicitly has a `..` in it
struct ASTExprNodeStructLiteralPattern: public ASTExprNode {
    typedef ::std::vector<ASTExprNodeStructLiteral::Ent> tValues;
    ASTPath path;
    tValues values;

    ASTExprNodeStructLiteralPattern(ASTPath path, tValues&& values);

    static constexpr unsigned int kind = 28;
    unsigned int nodeKind() const override;
    void visit(ASTNodeVisitor& nv) override;
    void print(::std::ostream& os) const override;
    ASTExprNodeP clone() const override;
};

// Array
struct ASTExprNodeArray: public ASTExprNode {
    ASTExprNodeP size; // if non-NULL, it's a sized array
    ::std::vector<ASTExprNodeP> values;

    ASTExprNodeArray(::std::vector<ASTExprNodeP> vals);

    ASTExprNodeArray(ASTExprNodeP val, ASTExprNodeP size);

    static constexpr unsigned int kind = 29;
    unsigned int nodeKind() const override;
    void visit(ASTNodeVisitor& nv) override;
    void print(::std::ostream& os) const override;
    ASTExprNodeP clone() const override;
};

// Tuple
struct ASTExprNodeTuple: public ASTExprNode {
    ::std::vector<ASTExprNodeP> values;

    ASTExprNodeTuple(::std::vector<ASTExprNodeP> vals);

    static constexpr unsigned int kind = 30;
    unsigned int nodeKind() const override;
    void visit(ASTNodeVisitor& nv) override;
    void print(::std::ostream& os) const override;
    ASTExprNodeP clone() const override;
};

// Variable / Constant
struct ASTExprNodeNamedValue: public ASTExprNode {
    ASTPath path;

    ASTExprNodeNamedValue(ASTPath path);

    static constexpr unsigned int kind = 31;
    unsigned int nodeKind() const override;
    void visit(ASTNodeVisitor& nv) override;
    void print(::std::ostream& os) const override;
    ASTExprNodeP clone() const override;
};

// Field dereference
struct ASTExprNodeField: public ASTExprNode {
    ASTExprNodeP obj;
    RcString name;

    ASTExprNodeField(ASTExprNodeP obj, RcString name);

    static constexpr unsigned int kind = 32;
    unsigned int nodeKind() const override;
    void visit(ASTNodeVisitor& nv) override;
    void print(::std::ostream& os) const override;
    ASTExprNodeP clone() const override;
};

struct ASTExprNodeIndex: public ASTExprNode {
    ASTExprNodeP obj;
    ASTExprNodeP idx;

    ASTExprNodeIndex(ASTExprNodeP obj, ASTExprNodeP idx);

    static constexpr unsigned int kind = 33;
    unsigned int nodeKind() const override;
    void visit(ASTNodeVisitor& nv) override;
    void print(::std::ostream& os) const override;
    ASTExprNodeP clone() const override;
};

// Pointer dereference
struct ASTExprNodeDeref: public ASTExprNode {
    ASTExprNodeP value;

    ASTExprNodeDeref(ASTExprNodeP value);

    static constexpr unsigned int kind = 34;
    unsigned int nodeKind() const override;
    void visit(ASTNodeVisitor& nv) override;
    void print(::std::ostream& os) const override;
    ASTExprNodeP clone() const override;
};

// Type cast ('as')
struct ASTExprNodeCast: public ASTExprNode {
    ASTExprNodeP value;
    ASTType* type;

    ASTExprNodeCast(ASTExprNodeP value, ASTType*&& dstType);

    static constexpr unsigned int kind = 35;
    unsigned int nodeKind() const override;
    void visit(ASTNodeVisitor& nv) override;
    void print(::std::ostream& os) const override;
    ASTExprNodeP clone() const override;
};

// Type annotation (': _')
struct ASTExprNodeTypeAnnotation: public ASTExprNode {
    ASTExprNodeP value;
    ASTType* type;

    ASTExprNodeTypeAnnotation(ASTExprNodeP value, ASTType*&& dstType);

    static constexpr unsigned int kind = 36;
    unsigned int nodeKind() const override;
    void visit(ASTNodeVisitor& nv) override;
    void print(::std::ostream& os) const override;
    ASTExprNodeP clone() const override;
};

// Binary operation
struct ASTExprNodeBinOp: public ASTExprNode {
    enum Type {
        CMPEQU,
        CMPNEQU,
        CMPLT,
        CMPLTE,
        CMPGT,
        CMPGTE,

        RANGE,
        RANGE_INC,
        BOOLAND,
        BOOLOR,

        BITAND,
        BITOR,
        BITXOR,

        SHL,
        SHR,

        MULTIPLY,
        DIVIDE,
        MODULO,
        ADD,
        SUB,

        PLACE_IN, // `in PLACE { expr }` or `PLACE <- expr`
    };

    Type type;
    ASTExprNodeP left;
    ASTExprNodeP right;
    /// Set on a bound-less `RANGE` that was written as `(..)`. Parentheses are
    /// otherwise dropped, but a destructuring assignment needs them: `(..)` is a
    /// sub-pattern where a bare `..` is the enclosing pattern's rest.
    bool parenthesised = false;

    ASTExprNodeBinOp(Type type, ASTExprNodeP left, ASTExprNodeP right);

    static constexpr unsigned int kind = 37;
    unsigned int nodeKind() const override;
    void visit(ASTNodeVisitor& nv) override;
    void print(::std::ostream& os) const override;
    ASTExprNodeP clone() const override;
};

struct ASTExprNodeUniOp: public ASTExprNode {
    enum Type {
        REF,    // '& <expr>'
        REFMUT, // '&mut <expr>'
        RawBorrow,
        RawBorrowMut,
        /// `&pin const place` and `&pin mut place`, which pin the place:
        /// `Pin<&T>` and `Pin<&mut T>`.
        PinBorrow,
        PinBorrowMut,
        BOX,    // 'box <expr>'
        INVERT, // '!<expr>'
        NEGATE, // '-<expr>'
        QMARK,  // '<expr>?'
        AWait,  // `.await`
        /// Await the next item of an async iterator: `Option<Item>`, or nothing
        /// if the iterator is not ready. Only the `for await` desugaring makes
        /// this; there is no syntax for it.
        AWaitNext,
        USE,    // `.use`
    };

    enum Type type;
    ASTExprNodeP value;

    ASTExprNodeUniOp(Type type, ASTExprNodeP value);

    static constexpr unsigned int kind = 38;
    unsigned int nodeKind() const override;
    void visit(ASTNodeVisitor& nv) override;
    void print(::std::ostream& os) const override;
    ASTExprNodeP clone() const override;
};

// A lexical rib introduced by a block-local macro definition. Expansion
// preserves it until local-variable and label resolution have crossed the
// definition at the correct source position.
struct ASTExprNodeMacroDefinition: public ASTExprNode {
    unsigned int definitionId;
    Ident::Hygiene tokenHygiene;
    Ident::Hygiene definitionHygiene;

    ASTExprNodeMacroDefinition(unsigned int definitionId, Ident::Hygiene tokenHygiene, Ident::Hygiene definitionHygiene);

    static constexpr unsigned int kind = 39;
    unsigned int nodeKind() const override;
    void visit(ASTNodeVisitor& nv) override;
    void print(::std::ostream& os) const override;
    ASTExprNodeP clone() const override;
};

class ASTNodeVisitor {
public:
    virtual ~ASTNodeVisitor() = default;

    void visit(ASTExprNodeP& cnode);

    virtual bool isConst() const;

#define NT(nt) \
    virtual void visit(nt& node) = 0 /*; \
        virtual void visit(const nt& node) = 0*/
    NT(ASTExprNodeBlock);
    NT(ASTExprNodeAsyncBlock);
    NT(ASTExprNodeGeneratorBlock);
    NT(ASTExprNodeTry);
    NT(ASTExprNodeMacro);
    NT(ASTExprNodeAsm);
    NT(ASTExprNodeAsm2);
    NT(ASTExprNodeFlow);
    NT(ASTExprNodeLetBinding);
    NT(ASTExprNodeAssign);
    NT(ASTExprNodeCallPath);
    NT(ASTExprNodeCallMethod);
    NT(ASTExprNodeCallObject);
    NT(ASTExprNodeLoop);
    NT(ASTExprNodeFor);
    NT(ASTExprNodeWhile);
    NT(ASTExprNodeMatch);
    NT(ASTExprNodeIf);

    NT(ASTExprNodeWildcardPattern);
    NT(ASTExprNodeInteger);
    NT(ASTExprNodeFloat);
    NT(ASTExprNodeBool);
    NT(ASTExprNodeString);
    NT(ASTExprNodeByteString);
    NT(ASTExprNodeCString);
    NT(ASTExprNodeSuffixedLiteral);
    NT(ASTExprNodeClosure);
    NT(ASTExprNodeStructLiteral);
    NT(ASTExprNodeStructLiteralPattern);
    NT(ASTExprNodeArray);
    NT(ASTExprNodeTuple);
    NT(ASTExprNodeNamedValue);

    NT(ASTExprNodeField);
    NT(ASTExprNodeIndex);
    NT(ASTExprNodeDeref);
    NT(ASTExprNodeCast);
    NT(ASTExprNodeTypeAnnotation);
    NT(ASTExprNodeBinOp);
    NT(ASTExprNodeUniOp);
    NT(ASTExprNodeMacroDefinition);
#undef NT
};

class ASTNodeVisitorDef: public ASTNodeVisitor {
public:
    void visit(ASTExprNodeP& cnode);

#define NT(nt) \
    virtual void visit(nt& node) override; /* \
        virtual void visit(const nt& node) override*/
    NT(ASTExprNodeBlock);
    NT(ASTExprNodeAsyncBlock);
    NT(ASTExprNodeGeneratorBlock);
    NT(ASTExprNodeTry);
    NT(ASTExprNodeMacro);
    NT(ASTExprNodeAsm);
    NT(ASTExprNodeAsm2);
    NT(ASTExprNodeFlow);
    NT(ASTExprNodeLetBinding);
    NT(ASTExprNodeAssign);
    NT(ASTExprNodeCallPath);
    NT(ASTExprNodeCallMethod);
    NT(ASTExprNodeCallObject);
    NT(ASTExprNodeLoop);
    NT(ASTExprNodeFor);
    NT(ASTExprNodeWhile);
    NT(ASTExprNodeMatch);
    NT(ASTExprNodeIf);

    NT(ASTExprNodeWildcardPattern);
    NT(ASTExprNodeInteger);
    NT(ASTExprNodeFloat);
    NT(ASTExprNodeBool);
    NT(ASTExprNodeString);
    NT(ASTExprNodeByteString);
    NT(ASTExprNodeCString);
    NT(ASTExprNodeSuffixedLiteral);
    NT(ASTExprNodeClosure);
    NT(ASTExprNodeStructLiteral);
    NT(ASTExprNodeStructLiteralPattern);
    NT(ASTExprNodeArray);
    NT(ASTExprNodeTuple);
    NT(ASTExprNodeNamedValue);

    NT(ASTExprNodeField);
    NT(ASTExprNodeIndex);
    NT(ASTExprNodeDeref);
    NT(ASTExprNodeCast);
    NT(ASTExprNodeTypeAnnotation);
    NT(ASTExprNodeBinOp);
    NT(ASTExprNodeUniOp);
    NT(ASTExprNodeMacroDefinition);
#undef NT
};
