#pragma once

#include <ostream>
#include <memory> // unique_ptr
#include <vector>

#include "parse_tokentree.h"
#include "ast_types.h"
#include "ast_pattern.h"
#include "ast_attrs.h"
#include "ast_expr_ptr.h"
#include "hir_asm.h"

namespace AST {

    class Pattern;
    class NodeVisitor;

    class ExprNode {
        AttributeList mAttrs;
        Span mSpan;

    public:
        virtual ~ExprNode() = 0;

        virtual void visit(NodeVisitor& nv) = 0;
        virtual void print(::std::ostream& os) const = 0;
        virtual ExprNodeP clone() const = 0;
        virtual unsigned int nodeKind() const = 0;

        void setSpan(Span s) {
            mSpan = ::std::move(s);
        }

        const Span& span() const {
            return mSpan;
        }

        void setAttrs(AttributeList&& mi);

        AttributeList& attrs() {
            return mAttrs;
        }
    };

    struct ExprNodeBlock: public ExprNode {
        enum class Type {
            Bare,
            Unsafe,
            Const,
        };
        Type blockType;
        Ident label;
        ::std::shared_ptr<AST::Module> localMod;

        struct Line {
            bool hasSemicolon;
            ExprNodeP node;
        };

        ::std::vector<Line> nodes;

        ExprNodeBlock(::std::vector<Line> nodes = {});

        /// Shortcut for a block that returns a contained node
        ExprNodeBlock(ExprNodeP value);

        ExprNodeBlock(Type type, ::std::vector<Line> nodes, ::std::shared_ptr<AST::Module> local_mod);

        void pushStmt(AST::ExprNodeP node) {
            nodes.push_back({true, std::move(node)});
        }

        void pushTailExpr(AST::ExprNodeP node) {
            nodes.push_back({false, std::move(node)});
        }

        static constexpr unsigned int kind = 1;
        unsigned int nodeKind() const override;
        void visit(NodeVisitor& nv) override;
        void print(::std::ostream& os) const override;
        ExprNodeP clone() const override;
    };

    struct ExprNodeAsyncBlock: public ExprNode {
        ExprNodeP inner;
        bool isMove;

        ExprNodeAsyncBlock(ExprNodeP inner, bool is_move);

        static constexpr unsigned int kind = 2;
        unsigned int nodeKind() const override;
        void visit(NodeVisitor& nv) override;
        void print(::std::ostream& os) const override;
        ExprNodeP clone() const override;
    };

    struct ExprNodeGeneratorBlock: public ExprNode {
        ExprNodeP inner;
        bool isMove;

        ExprNodeGeneratorBlock(ExprNodeP inner, bool is_move);

        static constexpr unsigned int kind = 3;
        unsigned int nodeKind() const override;
        void visit(NodeVisitor& nv) override;
        void print(::std::ostream& os) const override;
        ExprNodeP clone() const override;
    };

    struct ExprNodeTry: public ExprNode {
        ExprNodeP inner;

        ExprNodeTry(ExprNodeP inner);

        static constexpr unsigned int kind = 4;
        unsigned int nodeKind() const override;
        void visit(NodeVisitor& nv) override;
        void print(::std::ostream& os) const override;
        ExprNodeP clone() const override;
    };

    struct ExprNodeMacro: public ExprNode {
        AST::Path mPath;
        RcString ident;
        ::TokenTree tokens;
        bool isBraced;
        Ident::Hygiene definitionHygiene;

        ExprNodeMacro(AST::Path name, RcString ident, ::TokenTree&& tokens, bool is_braced = false, Ident::Hygiene definition_hygiene = {});

        static constexpr unsigned int kind = 5;
        unsigned int nodeKind() const override;
        void visit(NodeVisitor& nv) override;
        void print(::std::ostream& os) const override;
        ExprNodeP clone() const override;
    };

    // llvm_asm! macro
    struct ExprNodeAsm: public ExprNode {
        struct ValRef {
            ::std::string name;
            ExprNodeP value;
        };

        ::std::string text;
        ::std::vector<ValRef> output;
        ::std::vector<ValRef> input;
        ::std::vector<::std::string> clobbers;
        ::std::vector<::std::string> flags;

        ExprNodeAsm(::std::string text, ::std::vector<ValRef> output, ::std::vector<ValRef> input, ::std::vector<::std::string> clobbers, ::std::vector<::std::string> flags);

        static constexpr unsigned int kind = 6;
        unsigned int nodeKind() const override;
        void visit(NodeVisitor& nv) override;
        void print(::std::ostream& os) const override;
        ExprNodeP clone() const override;
    };

    // asm! macro
    struct ExprNodeAsm2: public ExprNode {
        TAGGED_UNION(
            Param,
            Const,
            (Const, AST::ExprNodeP),
            (Sym, AST::Path),
            (RegSingle,
             struct {
                 AsmCommon::Direction dir;
                 AsmCommon::RegisterSpec spec;
                 AST::ExprNodeP val;
             }),
            (Reg, struct {
                AsmCommon::Direction dir;
                AsmCommon::RegisterSpec spec;
                AST::ExprNodeP valIn;
                AST::ExprNodeP valOut;
            })
        );

        AsmCommon::Options options;
        std::vector<AsmCommon::Line> lines;
        std::vector<Param> mParams;

        ExprNodeAsm2(AsmCommon::Options options, std::vector<AsmCommon::Line> lines, std::vector<Param> params);

        static constexpr unsigned int kind = 7;
        unsigned int nodeKind() const override;
        void visit(NodeVisitor& nv) override;
        void print(::std::ostream& os) const override;
        ExprNodeP clone() const override;
    };

    // Break/Continue/Return
    struct ExprNodeFlow: public ExprNode {
        enum Type {
            RETURN,
            YIELD,
            CONTINUE,
            BREAK,
            // `do yeet value` - a failed `?`
            YEET,
        } mType;

        Ident target;
        ExprNodeP mValue;

        ExprNodeFlow(Type type, Ident target, ExprNodeP value);

        static constexpr unsigned int kind = 8;
        unsigned int nodeKind() const override;
        void visit(NodeVisitor& nv) override;
        void print(::std::ostream& os) const override;
        ExprNodeP clone() const override;
    };

    struct ExprNodeLetBinding: public ExprNode {
        Pattern pat;
        TypeRef mType;
        ExprNodeP mValue;
        ExprNodeP elseNode;
        bool isSuper;
        /// Allocated binding slots/indexes for the pattern in `let-else`
        ::std::pair<unsigned, unsigned> letelseSlots;

        ExprNodeLetBinding(Pattern pat, TypeRef type, ExprNodeP value, ExprNodeP elseArm = {}, bool is_super = false);

        static constexpr unsigned int kind = 9;
        unsigned int nodeKind() const override;
        void visit(NodeVisitor& nv) override;
        void print(::std::ostream& os) const override;
        ExprNodeP clone() const override;
    };

    struct ExprNodeAssign: public ExprNode {
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

        ExprNodeP slot;
        ExprNodeP mValue;

        ExprNodeAssign();

        ExprNodeAssign(Operation op, ExprNodeP slot, ExprNodeP value);

        static constexpr unsigned int kind = 10;
        unsigned int nodeKind() const override;
        void visit(NodeVisitor& nv) override;
        void print(::std::ostream& os) const override;
        ExprNodeP clone() const override;
    };

    struct ExprNodeCallPath: public ExprNode {
        Path mPath;
        ::std::vector<ExprNodeP> mArgs;

        ExprNodeCallPath(Path&& path, ::std::vector<ExprNodeP>&& args);

        static constexpr unsigned int kind = 11;
        unsigned int nodeKind() const override;
        void visit(NodeVisitor& nv) override;
        void print(::std::ostream& os) const override;
        ExprNodeP clone() const override;
    };

    struct ExprNodeCallMethod: public ExprNode {
        ExprNodeP val;
        PathNode method;
        ::std::vector<ExprNodeP> mArgs;

        ExprNodeCallMethod(ExprNodeP obj, PathNode method, ::std::vector<ExprNodeP> args);

        static constexpr unsigned int kind = 12;
        unsigned int nodeKind() const override;
        void visit(NodeVisitor& nv) override;
        void print(::std::ostream& os) const override;
        ExprNodeP clone() const override;
    };

    // Call an object (Fn/FnMut/FnOnce)
    struct ExprNodeCallObject: public ExprNode {
        ExprNodeP val;
        ::std::vector<ExprNodeP> mArgs;

        ExprNodeCallObject(ExprNodeP val, ::std::vector<ExprNodeP>&& args);

        static constexpr unsigned int kind = 13;
        unsigned int nodeKind() const override;
        void visit(NodeVisitor& nv) override;
        void print(::std::ostream& os) const override;
        ExprNodeP clone() const override;
    };

    struct ExprNodeLoop: public ExprNode {
        Ident label;
        ExprNodeP mCode;

        ExprNodeLoop();

        ExprNodeLoop(Ident label, ExprNodeP code);

        static constexpr unsigned int kind = 14;
        unsigned int nodeKind() const override;
        void visit(NodeVisitor& nv) override;
        void print(::std::ostream& os) const override;
        ExprNodeP clone() const override;
    };

    struct ExprNodeFor: public ExprNode {
        Ident label;
        AST::Pattern pattern;
        ExprNodeP mValue;
        ExprNodeP mCode;

        ExprNodeFor(Ident label, AST::Pattern pattern, ExprNodeP val, ExprNodeP code);

        static constexpr unsigned int kind = 15;
        unsigned int nodeKind() const override;
        void visit(NodeVisitor& nv) override;
        void print(::std::ostream& os) const override;
        ExprNodeP clone() const override;
    };

    struct IfLetCondition {
        ::std::unique_ptr<AST::Pattern> optPat;
        ExprNodeP value;
    };

    struct ExprNodeWhile: public ExprNode {
        Ident label;
        std::vector<IfLetCondition> conditions;
        ExprNodeP mCode;

        ExprNodeWhile(Ident label, std::vector<IfLetCondition> conditions, ExprNodeP code);

        static constexpr unsigned int kind = 16;
        unsigned int nodeKind() const override;
        void visit(NodeVisitor& nv) override;
        void print(::std::ostream& os) const override;
        ExprNodeP clone() const override;
    };

    struct ExprNodeMatchArm {
        AttributeList mAttrs;
        ::std::vector<Pattern> patterns;
        std::vector<IfLetCondition> guard;

        ExprNodeP mCode;

        ExprNodeMatchArm();

        ExprNodeMatchArm(::std::vector<Pattern> patterns, std::vector<IfLetCondition> guard, ExprNodeP code);
    };

    struct ExprNodeMatch: public ExprNode {
        ExprNodeP val;
        ::std::vector<ExprNodeMatchArm> arms;

        ExprNodeMatch(ExprNodeP val, ::std::vector<ExprNodeMatchArm> arms);

        static constexpr unsigned int kind = 17;
        unsigned int nodeKind() const override;
        void visit(NodeVisitor& nv) override;
        void print(::std::ostream& os) const override;
        ExprNodeP clone() const override;
    };

    struct ExprNodeIf: public ExprNode {
        struct Arm {
            std::vector<IfLetCondition> conditions;
            ExprNodeP body;
        };

        std::vector<Arm> arms;
        ExprNodeP elseNode;

        ExprNodeIf(std::vector<Arm> arms, ExprNodeP elseCode);

        static constexpr unsigned int kind = 18;
        unsigned int nodeKind() const override;
        void visit(NodeVisitor& nv) override;
        void print(::std::ostream& os) const override;
        ExprNodeP clone() const override;
    };

    /// Represents `_` in expression position
    struct ExprNodeWildcardPattern: public ExprNode {
        static constexpr unsigned int kind = 19;
        unsigned int nodeKind() const override;
        void visit(NodeVisitor& nv) override;
        void print(::std::ostream& os) const override;
        ExprNodeP clone() const override;
    };

    // Literal integer
    struct ExprNodeInteger: public ExprNode {
        enum eCoreType datatype;
        U128 mValue;

        ExprNodeInteger(U128 value, enum eCoreType datatype);

        static constexpr unsigned int kind = 20;
        unsigned int nodeKind() const override;
        void visit(NodeVisitor& nv) override;
        void print(::std::ostream& os) const override;
        ExprNodeP clone() const override;
    };

    // Literal float
    struct ExprNodeFloat: public ExprNode {
        enum eCoreType datatype;
        FloatValue mValue;

        ExprNodeFloat(FloatValue value, enum eCoreType datatype);

        static constexpr unsigned int kind = 21;
        unsigned int nodeKind() const override;
        void visit(NodeVisitor& nv) override;
        void print(::std::ostream& os) const override;
        ExprNodeP clone() const override;
    };

    // Literal boolean
    struct ExprNodeBool: public ExprNode {
        bool mValue;

        ExprNodeBool(bool value);

        static constexpr unsigned int kind = 22;
        unsigned int nodeKind() const override;
        void visit(NodeVisitor& nv) override;
        void print(::std::ostream& os) const override;
        ExprNodeP clone() const override;
    };

    // Literal string
    struct ExprNodeString: public ExprNode {
        ::std::string mValue;
        /// Hygiene for format strings
        Ident::Hygiene mHygiene;

        ExprNodeString(::std::string value, Ident::Hygiene h = {});

        static constexpr unsigned int kind = 23;
        unsigned int nodeKind() const override;
        void visit(NodeVisitor& nv) override;
        void print(::std::ostream& os) const override;
        ExprNodeP clone() const override;
    };

    // Literal byte string
    struct ExprNodeByteString: public ExprNode {
        ::std::string mValue;

        ExprNodeByteString(::std::string value);

        static constexpr unsigned int kind = 24;
        unsigned int nodeKind() const override;
        void visit(NodeVisitor& nv) override;
        void print(::std::ostream& os) const override;
        ExprNodeP clone() const override;
    };

    // Literal C string
    struct ExprNodeCString: public ExprNode {
        ::std::string mValue;

        ExprNodeCString(::std::string value);

        static constexpr unsigned int kind = 25;
        unsigned int nodeKind() const override;
        void visit(NodeVisitor& nv) override;
        void print(::std::ostream& os) const override;
        ExprNodeP clone() const override;
    };

    // Closure / Lambda
    struct ExprNodeClosure: public ExprNode {
        typedef ::std::vector<::std::pair<AST::Pattern, TypeRef>> argsT;

        argsT mArgs;
        TypeRef returnType;
        ExprNodeP mCode;
        bool isMove;   //< The closure takes ownership of all values
        bool isPinned; //< The closure cannot be moved (this is for generators)

        ExprNodeClosure(argsT args, TypeRef rv, ExprNodeP code, bool is_move, bool is_pinned)
            : mArgs(::std::move(args))
            , returnType(::std::move(rv))
            , mCode(::std::move(code))
            , isMove(is_move)
            , isPinned(is_pinned)
        {
        }

        static constexpr unsigned int kind = 26;
        unsigned int nodeKind() const override;
        void visit(NodeVisitor& nv) override;
        void print(::std::ostream& os) const override;
        ExprNodeP clone() const override;
    };

    // Literal structure
    struct ExprNodeStructLiteral: public ExprNode {
        struct Ent {
            AttributeList attrs;
            RcString name;
            ExprNodeP value;
        };

        typedef ::std::vector<Ent> tValues;
        Path mPath;
        ExprNodeP baseValue;
        tValues values;

        ExprNodeStructLiteral(Path path, ExprNodeP base_value, tValues&& values);

        static constexpr unsigned int kind = 27;
        unsigned int nodeKind() const override;
        void visit(NodeVisitor& nv) override;
        void print(::std::ostream& os) const override;
        ExprNodeP clone() const override;
    };

    // Struct literal pattern only
    // This implicitly has a `..` in it
    struct ExprNodeStructLiteralPattern: public ExprNode {
        typedef ::std::vector<ExprNodeStructLiteral::Ent> tValues;
        Path mPath;
        tValues values;

        ExprNodeStructLiteralPattern(Path path, tValues&& values);

        static constexpr unsigned int kind = 28;
        unsigned int nodeKind() const override;
        void visit(NodeVisitor& nv) override;
        void print(::std::ostream& os) const override;
        ExprNodeP clone() const override;
    };

    // Array
    struct ExprNodeArray: public ExprNode {
        ExprNodeP mSize; // if non-NULL, it's a sized array
        ::std::vector<ExprNodeP> values;

        ExprNodeArray(::std::vector<ExprNodeP> vals);

        ExprNodeArray(ExprNodeP val, ExprNodeP size);

        static constexpr unsigned int kind = 29;
        unsigned int nodeKind() const override;
        void visit(NodeVisitor& nv) override;
        void print(::std::ostream& os) const override;
        ExprNodeP clone() const override;
    };

    // Tuple
    struct ExprNodeTuple: public ExprNode {
        ::std::vector<ExprNodeP> values;

        ExprNodeTuple(::std::vector<ExprNodeP> vals);

        static constexpr unsigned int kind = 30;
        unsigned int nodeKind() const override;
        void visit(NodeVisitor& nv) override;
        void print(::std::ostream& os) const override;
        ExprNodeP clone() const override;
    };

    // Variable / Constant
    struct ExprNodeNamedValue: public ExprNode {
        Path mPath;

        ExprNodeNamedValue(Path path);

        static constexpr unsigned int kind = 31;
        unsigned int nodeKind() const override;
        void visit(NodeVisitor& nv) override;
        void print(::std::ostream& os) const override;
        ExprNodeP clone() const override;
    };

    // Field dereference
    struct ExprNodeField: public ExprNode {
        ExprNodeP obj;
        RcString mName;

        ExprNodeField(ExprNodeP obj, RcString name);

        static constexpr unsigned int kind = 32;
        unsigned int nodeKind() const override;
        void visit(NodeVisitor& nv) override;
        void print(::std::ostream& os) const override;
        ExprNodeP clone() const override;
    };

    struct ExprNodeIndex: public ExprNode {
        ExprNodeP obj;
        ExprNodeP idx;

        ExprNodeIndex(ExprNodeP obj, ExprNodeP idx);

        static constexpr unsigned int kind = 33;
        unsigned int nodeKind() const override;
        void visit(NodeVisitor& nv) override;
        void print(::std::ostream& os) const override;
        ExprNodeP clone() const override;
    };

    // Pointer dereference
    struct ExprNodeDeref: public ExprNode {
        ExprNodeP mValue;

        ExprNodeDeref(ExprNodeP value);

        static constexpr unsigned int kind = 34;
        unsigned int nodeKind() const override;
        void visit(NodeVisitor& nv) override;
        void print(::std::ostream& os) const override;
        ExprNodeP clone() const override;
    };

    // Type cast ('as')
    struct ExprNodeCast: public ExprNode {
        ExprNodeP mValue;
        TypeRef mType;

        ExprNodeCast(ExprNodeP value, TypeRef&& dst_type);

        static constexpr unsigned int kind = 35;
        unsigned int nodeKind() const override;
        void visit(NodeVisitor& nv) override;
        void print(::std::ostream& os) const override;
        ExprNodeP clone() const override;
    };

    // Type annotation (': _')
    struct ExprNodeTypeAnnotation: public ExprNode {
        ExprNodeP mValue;
        TypeRef mType;

        ExprNodeTypeAnnotation(ExprNodeP value, TypeRef&& dst_type);

        static constexpr unsigned int kind = 36;
        unsigned int nodeKind() const override;
        void visit(NodeVisitor& nv) override;
        void print(::std::ostream& os) const override;
        ExprNodeP clone() const override;
    };

    // Binary operation
    struct ExprNodeBinOp: public ExprNode {
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

        Type mType;
        ExprNodeP left;
        ExprNodeP right;

        ExprNodeBinOp(Type type, ExprNodeP left, ExprNodeP right);

        static constexpr unsigned int kind = 37;
        unsigned int nodeKind() const override;
        void visit(NodeVisitor& nv) override;
        void print(::std::ostream& os) const override;
        ExprNodeP clone() const override;
    };

    struct ExprNodeUniOp: public ExprNode {
        enum Type {
            REF,    // '& <expr>'
            REFMUT, // '&mut <expr>'
            RawBorrow,
            RawBorrowMut,
            BOX,    // 'box <expr>'
            INVERT, // '!<expr>'
            NEGATE, // '-<expr>'
            QMARK,  // '<expr>?'
            AWait,  // `.await`
        };

        enum Type mType;
        ExprNodeP mValue;

        ExprNodeUniOp(Type type, ExprNodeP value);

        static constexpr unsigned int kind = 38;
        unsigned int nodeKind() const override;
        void visit(NodeVisitor& nv) override;
        void print(::std::ostream& os) const override;
        ExprNodeP clone() const override;
    };

    // A lexical rib introduced by a block-local macro definition. Expansion
    // preserves it until local-variable and label resolution have crossed the
    // definition at the correct source position.
    struct ExprNodeMacroDefinition: public ExprNode {
        unsigned int definitionId;
        Ident::Hygiene tokenHygiene;
        Ident::Hygiene definitionHygiene;

        ExprNodeMacroDefinition(unsigned int definition_id, Ident::Hygiene token_hygiene, Ident::Hygiene definition_hygiene);

        static constexpr unsigned int kind = 39;
        unsigned int nodeKind() const override;
        void visit(NodeVisitor& nv) override;
        void print(::std::ostream& os) const override;
        ExprNodeP clone() const override;
    };

    class NodeVisitor {
    public:
        virtual ~NodeVisitor() = default;

        void visit(ExprNodeP& cnode);

        virtual bool is_const() const {
            return false;
        }

#define NT(nt) \
    virtual void visit(nt& node) = 0 /*; \
        virtual void visit(const nt& node) = 0*/
        NT(ExprNodeBlock);
        NT(ExprNodeAsyncBlock);
        NT(ExprNodeGeneratorBlock);
        NT(ExprNodeTry);
        NT(ExprNodeMacro);
        NT(ExprNodeAsm);
        NT(ExprNodeAsm2);
        NT(ExprNodeFlow);
        NT(ExprNodeLetBinding);
        NT(ExprNodeAssign);
        NT(ExprNodeCallPath);
        NT(ExprNodeCallMethod);
        NT(ExprNodeCallObject);
        NT(ExprNodeLoop);
        NT(ExprNodeFor);
        NT(ExprNodeWhile);
        NT(ExprNodeMatch);
        NT(ExprNodeIf);

        NT(ExprNodeWildcardPattern);
        NT(ExprNodeInteger);
        NT(ExprNodeFloat);
        NT(ExprNodeBool);
        NT(ExprNodeString);
        NT(ExprNodeByteString);
        NT(ExprNodeCString);
        NT(ExprNodeClosure);
        NT(ExprNodeStructLiteral);
        NT(ExprNodeStructLiteralPattern);
        NT(ExprNodeArray);
        NT(ExprNodeTuple);
        NT(ExprNodeNamedValue);

        NT(ExprNodeField);
        NT(ExprNodeIndex);
        NT(ExprNodeDeref);
        NT(ExprNodeCast);
        NT(ExprNodeTypeAnnotation);
        NT(ExprNodeBinOp);
        NT(ExprNodeUniOp);
        NT(ExprNodeMacroDefinition);
#undef NT
    };

    class NodeVisitorDef: public NodeVisitor {
    public:
        void visit(ExprNodeP& cnode);

#define NT(nt) \
    virtual void visit(nt& node) override; /* \
        virtual void visit(const nt& node) override*/
        NT(ExprNodeBlock);
        NT(ExprNodeAsyncBlock);
        NT(ExprNodeGeneratorBlock);
        NT(ExprNodeTry);
        NT(ExprNodeMacro);
        NT(ExprNodeAsm);
        NT(ExprNodeAsm2);
        NT(ExprNodeFlow);
        NT(ExprNodeLetBinding);
        NT(ExprNodeAssign);
        NT(ExprNodeCallPath);
        NT(ExprNodeCallMethod);
        NT(ExprNodeCallObject);
        NT(ExprNodeLoop);
        NT(ExprNodeFor);
        NT(ExprNodeWhile);
        NT(ExprNodeMatch);
        NT(ExprNodeIf);

        NT(ExprNodeWildcardPattern);
        NT(ExprNodeInteger);
        NT(ExprNodeFloat);
        NT(ExprNodeBool);
        NT(ExprNodeString);
        NT(ExprNodeByteString);
        NT(ExprNodeCString);
        NT(ExprNodeClosure);
        NT(ExprNodeStructLiteral);
        NT(ExprNodeStructLiteralPattern);
        NT(ExprNodeArray);
        NT(ExprNodeTuple);
        NT(ExprNodeNamedValue);

        NT(ExprNodeField);
        NT(ExprNodeIndex);
        NT(ExprNodeDeref);
        NT(ExprNodeCast);
        NT(ExprNodeTypeAnnotation);
        NT(ExprNodeBinOp);
        NT(ExprNodeUniOp);
        NT(ExprNodeMacroDefinition);
#undef NT
    };

}
