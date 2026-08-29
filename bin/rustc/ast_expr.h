#pragma once

#include "hir_asm.h"
#include "ast_attrs.h"
#include "ast_types.h"
#include "ast_pattern.h"
#include "ast_expr_ptr.h"
#include "parse_tokentree.h"

#include <std/mem/obj_pool.h>

#include <memory>
#include <vector>
#include <ostream>
#include <utility>

class ASTPattern;
class ASTNodeVisitor;

class ASTExprNode {
    ASTAttributeList attrs_;
    Span span_;
    stl::ObjPool* pool_ = nullptr;

public:
    virtual ~ASTExprNode() = 0;

    virtual void visit(ASTNodeVisitor& nv) = 0;
    virtual void print(std::ostream& os) const = 0;
    virtual ASTExprNodeP clone() const = 0;
    virtual unsigned int nodeKind() const = 0;

    void setPool(stl::ObjPool& pool) {
        pool_ = &pool;
    }

    stl::ObjPool& pool() const {
        return *pool_;
    }

    void setSpan(Span s) {
        span_ = std::move(s);
    }

    const Span& span() const {
        return span_;
    }

    void setAttrs(ASTAttributeList&& mi);

    ASTAttributeList& attrs() {
        return attrs_;
    }
};

template <typename T, typename... Args>
ASTExprNodeP makeAstExprNode(stl::ObjPool& pool, Args&&... args) {
    auto* node = pool.make<T>(std::forward<Args>(args)...);
    node->setPool(pool);
    return ASTExprNodeP(node);
}

struct ASTExprNodeBlock: public ASTExprNode {
    enum class Type {
        Bare,
        Unsafe,
        Const,
    };
    Type blockType;
    Ident label;
    std::shared_ptr<ASTModule> localMod;

    struct Line {
        bool hasSemicolon;
        ASTExprNodeP node;
    };

    std::vector<Line> nodes;

    ASTExprNodeBlock(std::vector<Line> nodes = {});

    ASTExprNodeBlock(ASTExprNodeP value);

    ASTExprNodeBlock(Type type, std::vector<Line> nodes, std::shared_ptr<ASTModule> localMod);

    void pushStmt(ASTExprNodeP node) {
        nodes.push_back({true, std::move(node)});
    }

    void pushTailExpr(ASTExprNodeP node) {
        nodes.push_back({false, std::move(node)});
    }

    static constexpr unsigned int kind = 1;
    unsigned int nodeKind() const override;
    void visit(ASTNodeVisitor& nv) override;
    void print(std::ostream& os) const override;
    ASTExprNodeP clone() const override;
};

struct ASTExprNodeAsyncBlock: public ASTExprNode {
    ASTExprNodeP inner;
    bool isMove;
    bool isUse;

    ASTExprNodeAsyncBlock(ASTExprNodeP inner, bool isMove, bool isUse);

    static constexpr unsigned int kind = 2;
    unsigned int nodeKind() const override;
    void visit(ASTNodeVisitor& nv) override;
    void print(std::ostream& os) const override;
    ASTExprNodeP clone() const override;
};

struct ASTExprNodeGeneratorBlock: public ASTExprNode {
    ASTExprNodeP inner;
    ASTType* returnType;
    bool isMove;

    bool isCoroutineClosureBody;

    bool isAsync;

    ASTExprNodeGeneratorBlock(ASTExprNodeP inner, ASTType* returnType, bool isMove, bool isCoroutineClosureBody, bool isAsync = false);

    static constexpr unsigned int kind = 3;
    unsigned int nodeKind() const override;
    void visit(ASTNodeVisitor& nv) override;
    void print(std::ostream& os) const override;
    ASTExprNodeP clone() const override;
};

struct ASTExprNodeTry: public ASTExprNode {
    ASTExprNodeP inner;

    ASTExprNodeTry(ASTExprNodeP inner);

    static constexpr unsigned int kind = 4;
    unsigned int nodeKind() const override;
    void visit(ASTNodeVisitor& nv) override;
    void print(std::ostream& os) const override;
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
    void print(std::ostream& os) const override;
    ASTExprNodeP clone() const override;
};

struct ASTExprNodeAsm: public ASTExprNode {
    struct ValRef {
        std::string name;
        ASTExprNodeP value;
    };

    std::string text;
    std::vector<ValRef> output;
    std::vector<ValRef> input;
    std::vector<std::string> clobbers;
    std::vector<std::string> flags;

    ASTExprNodeAsm(std::string text, std::vector<ValRef> output, std::vector<ValRef> input, std::vector<std::string> clobbers, std::vector<std::string> flags);

    static constexpr unsigned int kind = 6;
    unsigned int nodeKind() const override;
    void visit(ASTNodeVisitor& nv) override;
    void print(std::ostream& os) const override;
    ASTExprNodeP clone() const override;
};

#include "ast_expr_tu.h"

struct ASTExprNodeAsm2: public ASTExprNode {
    using Param = ASTAsmParam;

    AsmOptions options;
    std::vector<AsmLine> lines;
    std::vector<Param> params;

    ASTExprNodeAsm2(AsmOptions options, std::vector<AsmLine> lines, std::vector<Param> params);

    static constexpr unsigned int kind = 7;
    unsigned int nodeKind() const override;
    void visit(ASTNodeVisitor& nv) override;
    void print(std::ostream& os) const override;
    ASTExprNodeP clone() const override;
};

struct ASTExprNodeFlow: public ASTExprNode {
    enum Type {
        RETURN,
        TAILCALL,
        YIELD,
        CONTINUE,
        BREAK,

        YEET,
    } type;

    Ident target;
    ASTExprNodeP value;

    ASTExprNodeFlow(Type type, Ident target, ASTExprNodeP value);

    static constexpr unsigned int kind = 8;
    unsigned int nodeKind() const override;
    void visit(ASTNodeVisitor& nv) override;
    void print(std::ostream& os) const override;
    ASTExprNodeP clone() const override;
};

struct ASTExprNodeLetBinding: public ASTExprNode {
    ASTPattern pat;
    ASTType* type;
    ASTExprNodeP value;
    ASTExprNodeP elseNode;
    bool isSuper;

    std::pair<unsigned, unsigned> letelseSlots;

    ASTExprNodeLetBinding(ASTPattern pat, ASTType* type, ASTExprNodeP value, ASTExprNodeP elseArm = {}, bool isSuper = false);

    static constexpr unsigned int kind = 9;
    unsigned int nodeKind() const override;
    void visit(ASTNodeVisitor& nv) override;
    void print(std::ostream& os) const override;
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
    void print(std::ostream& os) const override;
    ASTExprNodeP clone() const override;
};

struct ASTExprNodeCallPath: public ASTExprNode {
    ASTPath path;
    std::vector<ASTExprNodeP> args;

    ASTExprNodeCallPath(ASTPath&& path, std::vector<ASTExprNodeP>&& args);

    static constexpr unsigned int kind = 11;
    unsigned int nodeKind() const override;
    void visit(ASTNodeVisitor& nv) override;
    void print(std::ostream& os) const override;
    ASTExprNodeP clone() const override;
};

struct ASTExprNodeCallMethod: public ASTExprNode {
    ASTExprNodeP val;
    ASTPathNode method;
    std::vector<ASTExprNodeP> args;

    ASTExprNodeCallMethod(ASTExprNodeP obj, ASTPathNode method, std::vector<ASTExprNodeP> args);

    static constexpr unsigned int kind = 12;
    unsigned int nodeKind() const override;
    void visit(ASTNodeVisitor& nv) override;
    void print(std::ostream& os) const override;
    ASTExprNodeP clone() const override;
};

struct ASTExprNodeCallObject: public ASTExprNode {
    ASTExprNodeP val;
    std::vector<ASTExprNodeP> args;

    ASTExprNodeCallObject(ASTExprNodeP val, std::vector<ASTExprNodeP>&& args);

    static constexpr unsigned int kind = 13;
    unsigned int nodeKind() const override;
    void visit(ASTNodeVisitor& nv) override;
    void print(std::ostream& os) const override;
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
    void print(std::ostream& os) const override;
    ASTExprNodeP clone() const override;
};

struct ASTExprNodeFor: public ASTExprNode {
    Ident label;
    ASTPattern pattern;
    ASTExprNodeP value;
    ASTExprNodeP code;

    bool isAwait;

    ASTExprNodeFor(Ident label, ASTPattern pattern, ASTExprNodeP val, ASTExprNodeP code, bool isAwait = false);

    static constexpr unsigned int kind = 15;
    unsigned int nodeKind() const override;
    void visit(ASTNodeVisitor& nv) override;
    void print(std::ostream& os) const override;
    ASTExprNodeP clone() const override;
};

struct ASTIfLetCondition {
    std::unique_ptr<ASTPattern> optPat;
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
    void print(std::ostream& os) const override;
    ASTExprNodeP clone() const override;
};

struct ASTExprNodeMatchArm {
    ASTAttributeList attrs;
    std::vector<ASTPattern> patterns;
    std::vector<ASTIfLetCondition> guard;

    ASTExprNodeP code;

    ASTExprNodeMatchArm();

    ASTExprNodeMatchArm(std::vector<ASTPattern> patterns, std::vector<ASTIfLetCondition> guard, ASTExprNodeP code);
};

struct ASTExprNodeMatch: public ASTExprNode {
    ASTExprNodeP val;
    std::vector<ASTExprNodeMatchArm> arms;

    ASTExprNodeMatch(ASTExprNodeP val, std::vector<ASTExprNodeMatchArm> arms);

    static constexpr unsigned int kind = 17;
    unsigned int nodeKind() const override;
    void visit(ASTNodeVisitor& nv) override;
    void print(std::ostream& os) const override;
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
    void print(std::ostream& os) const override;
    ASTExprNodeP clone() const override;
};

struct ASTExprNodeWildcardPattern: public ASTExprNode {
    static constexpr unsigned int kind = 19;
    unsigned int nodeKind() const override;
    void visit(ASTNodeVisitor& nv) override;
    void print(std::ostream& os) const override;
    ASTExprNodeP clone() const override;
};

struct ASTExprNodeInteger: public ASTExprNode {
    enum eCoreType datatype;
    U128 value;

    ASTExprNodeInteger(U128 value, enum eCoreType datatype);

    static constexpr unsigned int kind = 20;
    unsigned int nodeKind() const override;
    void visit(ASTNodeVisitor& nv) override;
    void print(std::ostream& os) const override;
    ASTExprNodeP clone() const override;
};

struct ASTExprNodeFloat: public ASTExprNode {
    enum eCoreType datatype;
    FloatValue value;

    ASTExprNodeFloat(FloatValue value, enum eCoreType datatype);

    static constexpr unsigned int kind = 21;
    unsigned int nodeKind() const override;
    void visit(ASTNodeVisitor& nv) override;
    void print(std::ostream& os) const override;
    ASTExprNodeP clone() const override;
};

struct ASTExprNodeBool: public ASTExprNode {
    bool value;

    ASTExprNodeBool(bool value);

    static constexpr unsigned int kind = 22;
    unsigned int nodeKind() const override;
    void visit(ASTNodeVisitor& nv) override;
    void print(std::ostream& os) const override;
    ASTExprNodeP clone() const override;
};

struct ASTExprNodeString: public ASTExprNode {
    std::string value;

    Ident::Hygiene hygiene;

    ASTExprNodeString(std::string value, Ident::Hygiene h = {});

    static constexpr unsigned int kind = 23;
    unsigned int nodeKind() const override;
    void visit(ASTNodeVisitor& nv) override;
    void print(std::ostream& os) const override;
    ASTExprNodeP clone() const override;
};

struct ASTExprNodeByteString: public ASTExprNode {
    std::string value;

    ASTExprNodeByteString(std::string value);

    static constexpr unsigned int kind = 24;
    unsigned int nodeKind() const override;
    void visit(ASTNodeVisitor& nv) override;
    void print(std::ostream& os) const override;
    ASTExprNodeP clone() const override;
};

struct ASTExprNodeCString: public ASTExprNode {
    std::string value;

    ASTExprNodeCString(std::string value);

    static constexpr unsigned int kind = 25;
    unsigned int nodeKind() const override;
    void visit(ASTNodeVisitor& nv) override;
    void print(std::ostream& os) const override;
    ASTExprNodeP clone() const override;
};

struct ASTExprNodeSuffixedLiteral: public ASTExprNode {
    std::string text;

    ASTExprNodeSuffixedLiteral(std::string text);

    static constexpr unsigned int kind = 40;
    unsigned int nodeKind() const override;
    void visit(ASTNodeVisitor& nv) override;
    void print(std::ostream& os) const override;
    ASTExprNodeP clone() const override;
};

struct ASTExprNodeClosure: public ASTExprNode {
    typedef std::vector<std::pair<ASTPattern, ASTType*>> argsT;

    argsT args;
    ASTType* returnType;
    ASTExprNodeP code;
    bool isMove;
    bool isUse;
    bool isPinned;
    bool trackCaller;

    ASTHigherRankedBounds hrbs;

    ASTExprNodeClosure(argsT args, ASTType* rv, ASTExprNodeP code, bool isMove, bool isUse, bool isPinned, bool trackCaller = false)
        : args(std::move(args))
        , returnType(std::move(rv))
        , code(std::move(code))
        , isMove(isMove)
        , isUse(isUse)
        , isPinned(isPinned)
        , trackCaller(trackCaller)
    {
    }

    static constexpr unsigned int kind = 26;
    unsigned int nodeKind() const override;
    void visit(ASTNodeVisitor& nv) override;
    void print(std::ostream& os) const override;
    ASTExprNodeP clone() const override;
};

struct ASTExprNodeStructLiteral: public ASTExprNode {
    struct Ent {
        ASTAttributeList attrs;
        RcString name;
        ASTExprNodeP value;
    };

    typedef std::vector<Ent> tValues;
    ASTPath path;
    ASTExprNodeP baseValue;
    tValues values;

    ASTExprNodeStructLiteral(ASTPath path, ASTExprNodeP baseValue, tValues&& values);

    static constexpr unsigned int kind = 27;
    unsigned int nodeKind() const override;
    void visit(ASTNodeVisitor& nv) override;
    void print(std::ostream& os) const override;
    ASTExprNodeP clone() const override;
};

struct ASTExprNodeStructLiteralPattern: public ASTExprNode {
    typedef std::vector<ASTExprNodeStructLiteral::Ent> tValues;
    ASTPath path;
    tValues values;

    ASTExprNodeStructLiteralPattern(ASTPath path, tValues&& values);

    static constexpr unsigned int kind = 28;
    unsigned int nodeKind() const override;
    void visit(ASTNodeVisitor& nv) override;
    void print(std::ostream& os) const override;
    ASTExprNodeP clone() const override;
};

struct ASTExprNodeArray: public ASTExprNode {
    ASTExprNodeP size;
    std::vector<ASTExprNodeP> values;

    ASTExprNodeArray(std::vector<ASTExprNodeP> vals);

    ASTExprNodeArray(ASTExprNodeP val, ASTExprNodeP size);

    static constexpr unsigned int kind = 29;
    unsigned int nodeKind() const override;
    void visit(ASTNodeVisitor& nv) override;
    void print(std::ostream& os) const override;
    ASTExprNodeP clone() const override;
};

struct ASTExprNodeTuple: public ASTExprNode {
    std::vector<ASTExprNodeP> values;

    ASTExprNodeTuple(std::vector<ASTExprNodeP> vals);

    static constexpr unsigned int kind = 30;
    unsigned int nodeKind() const override;
    void visit(ASTNodeVisitor& nv) override;
    void print(std::ostream& os) const override;
    ASTExprNodeP clone() const override;
};

struct ASTExprNodeNamedValue: public ASTExprNode {
    ASTPath path;

    ASTExprNodeNamedValue(ASTPath path);

    static constexpr unsigned int kind = 31;
    unsigned int nodeKind() const override;
    void visit(ASTNodeVisitor& nv) override;
    void print(std::ostream& os) const override;
    ASTExprNodeP clone() const override;
};

struct ASTExprNodeField: public ASTExprNode {
    ASTExprNodeP obj;
    RcString name;

    ASTExprNodeField(ASTExprNodeP obj, RcString name);

    static constexpr unsigned int kind = 32;
    unsigned int nodeKind() const override;
    void visit(ASTNodeVisitor& nv) override;
    void print(std::ostream& os) const override;
    ASTExprNodeP clone() const override;
};

struct ASTExprNodeIndex: public ASTExprNode {
    ASTExprNodeP obj;
    ASTExprNodeP idx;

    ASTExprNodeIndex(ASTExprNodeP obj, ASTExprNodeP idx);

    static constexpr unsigned int kind = 33;
    unsigned int nodeKind() const override;
    void visit(ASTNodeVisitor& nv) override;
    void print(std::ostream& os) const override;
    ASTExprNodeP clone() const override;
};

struct ASTExprNodeDeref: public ASTExprNode {
    ASTExprNodeP value;

    ASTExprNodeDeref(ASTExprNodeP value);

    static constexpr unsigned int kind = 34;
    unsigned int nodeKind() const override;
    void visit(ASTNodeVisitor& nv) override;
    void print(std::ostream& os) const override;
    ASTExprNodeP clone() const override;
};

struct ASTExprNodeCast: public ASTExprNode {
    ASTExprNodeP value;
    ASTType* type;

    ASTExprNodeCast(ASTExprNodeP value, ASTType*&& dstType);

    static constexpr unsigned int kind = 35;
    unsigned int nodeKind() const override;
    void visit(ASTNodeVisitor& nv) override;
    void print(std::ostream& os) const override;
    ASTExprNodeP clone() const override;
};

struct ASTExprNodeTypeAnnotation: public ASTExprNode {
    ASTExprNodeP value;
    ASTType* type;

    ASTExprNodeTypeAnnotation(ASTExprNodeP value, ASTType*&& dstType);

    static constexpr unsigned int kind = 36;
    unsigned int nodeKind() const override;
    void visit(ASTNodeVisitor& nv) override;
    void print(std::ostream& os) const override;
    ASTExprNodeP clone() const override;
};

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

        PLACE_IN,
    };

    Type type;
    ASTExprNodeP left;
    ASTExprNodeP right;

    bool parenthesised = false;

    ASTExprNodeBinOp(Type type, ASTExprNodeP left, ASTExprNodeP right);

    static constexpr unsigned int kind = 37;
    unsigned int nodeKind() const override;
    void visit(ASTNodeVisitor& nv) override;
    void print(std::ostream& os) const override;
    ASTExprNodeP clone() const override;
};

struct ASTExprNodeUniOp: public ASTExprNode {
    enum Type {
        REF,
        REFMUT,
        RawBorrow,
        RawBorrowMut,

        PinBorrow,
        PinBorrowMut,
        BOX,
        INVERT,
        NEGATE,
        QMARK,
        AWait,

        AWaitNext,
        USE,
    };

    enum Type type;
    ASTExprNodeP value;

    ASTExprNodeUniOp(Type type, ASTExprNodeP value);

    static constexpr unsigned int kind = 38;
    unsigned int nodeKind() const override;
    void visit(ASTNodeVisitor& nv) override;
    void print(std::ostream& os) const override;
    ASTExprNodeP clone() const override;
};

struct ASTExprNodeMacroDefinition: public ASTExprNode {
    unsigned int definitionId;
    Ident::Hygiene tokenHygiene;
    Ident::Hygiene definitionHygiene;

    ASTExprNodeMacroDefinition(unsigned int definitionId, Ident::Hygiene tokenHygiene, Ident::Hygiene definitionHygiene);

    static constexpr unsigned int kind = 39;
    unsigned int nodeKind() const override;
    void visit(ASTNodeVisitor& nv) override;
    void print(std::ostream& os) const override;
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
