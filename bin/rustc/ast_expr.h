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
        ASTAttributeList mAttrs;
        Span mSpan;

    public:
        virtual ~ASTExprNode() = 0;

        virtual void visit(ASTNodeVisitor& nv) = 0;
        virtual void print(::std::ostream& os) const = 0;
        virtual ASTExprNodeP clone() const = 0;
        virtual unsigned int nodeKind() const = 0;

        void setSpan(Span s) {
            mSpan = ::std::move(s);
        }

        const Span& span() const {
            return mSpan;
        }

        void setAttrs(ASTAttributeList&& mi);

        ASTAttributeList& attrs() {
            return mAttrs;
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

        ASTExprNodeAsyncBlock(ASTExprNodeP inner, bool isMove);

        static constexpr unsigned int kind = 2;
        unsigned int nodeKind() const override;
        void visit(ASTNodeVisitor& nv) override;
        void print(::std::ostream& os) const override;
        ASTExprNodeP clone() const override;
    };

    struct ASTExprNodeGeneratorBlock: public ASTExprNode {
        ASTExprNodeP inner;
        bool isMove;

        ASTExprNodeGeneratorBlock(ASTExprNodeP inner, bool isMove);

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
        ASTPath mPath;
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
        std::vector<Param> mParams;

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
            YIELD,
            CONTINUE,
            BREAK,
            // `do yeet value` - a failed `?`
            YEET,
        } mType;

        Ident target;
        ASTExprNodeP mValue;

        ASTExprNodeFlow(Type type, Ident target, ASTExprNodeP value);

        static constexpr unsigned int kind = 8;
        unsigned int nodeKind() const override;
        void visit(ASTNodeVisitor& nv) override;
        void print(::std::ostream& os) const override;
        ASTExprNodeP clone() const override;
    };

    struct ASTExprNodeLetBinding: public ASTExprNode {
        ASTPattern pat;
        TypeRef mType;
        ASTExprNodeP mValue;
        ASTExprNodeP elseNode;
        bool isSuper;
        /// Allocated binding slots/indexes for the pattern in `let-else`
        ::std::pair<unsigned, unsigned> letelseSlots;

        ASTExprNodeLetBinding(ASTPattern pat, TypeRef type, ASTExprNodeP value, ASTExprNodeP elseArm = {}, bool isSuper = false);

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
        ASTExprNodeP mValue;

        ASTExprNodeAssign();

        ASTExprNodeAssign(Operation op, ASTExprNodeP slot, ASTExprNodeP value);

        static constexpr unsigned int kind = 10;
        unsigned int nodeKind() const override;
        void visit(ASTNodeVisitor& nv) override;
        void print(::std::ostream& os) const override;
        ASTExprNodeP clone() const override;
    };

    struct ASTExprNodeCallPath: public ASTExprNode {
        ASTPath mPath;
        ::std::vector<ASTExprNodeP> mArgs;

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
        ::std::vector<ASTExprNodeP> mArgs;

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
        ::std::vector<ASTExprNodeP> mArgs;

        ASTExprNodeCallObject(ASTExprNodeP val, ::std::vector<ASTExprNodeP>&& args);

        static constexpr unsigned int kind = 13;
        unsigned int nodeKind() const override;
        void visit(ASTNodeVisitor& nv) override;
        void print(::std::ostream& os) const override;
        ASTExprNodeP clone() const override;
    };

    struct ASTExprNodeLoop: public ASTExprNode {
        Ident label;
        ASTExprNodeP mCode;

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
        ASTExprNodeP mValue;
        ASTExprNodeP mCode;

        ASTExprNodeFor(Ident label, ASTPattern pattern, ASTExprNodeP val, ASTExprNodeP code);

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
        ASTExprNodeP mCode;

        ASTExprNodeWhile(Ident label, std::vector<ASTIfLetCondition> conditions, ASTExprNodeP code);

        static constexpr unsigned int kind = 16;
        unsigned int nodeKind() const override;
        void visit(ASTNodeVisitor& nv) override;
        void print(::std::ostream& os) const override;
        ASTExprNodeP clone() const override;
    };

    struct ASTExprNodeMatchArm {
        ASTAttributeList mAttrs;
        ::std::vector<ASTPattern> patterns;
        std::vector<ASTIfLetCondition> guard;

        ASTExprNodeP mCode;

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
        U128 mValue;

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
        FloatValue mValue;

        ASTExprNodeFloat(FloatValue value, enum eCoreType datatype);

        static constexpr unsigned int kind = 21;
        unsigned int nodeKind() const override;
        void visit(ASTNodeVisitor& nv) override;
        void print(::std::ostream& os) const override;
        ASTExprNodeP clone() const override;
    };

    // Literal boolean
    struct ASTExprNodeBool: public ASTExprNode {
        bool mValue;

        ASTExprNodeBool(bool value);

        static constexpr unsigned int kind = 22;
        unsigned int nodeKind() const override;
        void visit(ASTNodeVisitor& nv) override;
        void print(::std::ostream& os) const override;
        ASTExprNodeP clone() const override;
    };

    // Literal string
    struct ASTExprNodeString: public ASTExprNode {
        ::std::string mValue;
        /// Hygiene for format strings
        Ident::Hygiene mHygiene;

        ASTExprNodeString(::std::string value, Ident::Hygiene h = {});

        static constexpr unsigned int kind = 23;
        unsigned int nodeKind() const override;
        void visit(ASTNodeVisitor& nv) override;
        void print(::std::ostream& os) const override;
        ASTExprNodeP clone() const override;
    };

    // Literal byte string
    struct ASTExprNodeByteString: public ASTExprNode {
        ::std::string mValue;

        ASTExprNodeByteString(::std::string value);

        static constexpr unsigned int kind = 24;
        unsigned int nodeKind() const override;
        void visit(ASTNodeVisitor& nv) override;
        void print(::std::ostream& os) const override;
        ASTExprNodeP clone() const override;
    };

    // Literal C string
    struct ASTExprNodeCString: public ASTExprNode {
        ::std::string mValue;

        ASTExprNodeCString(::std::string value);

        static constexpr unsigned int kind = 25;
        unsigned int nodeKind() const override;
        void visit(ASTNodeVisitor& nv) override;
        void print(::std::ostream& os) const override;
        ASTExprNodeP clone() const override;
    };

    // Closure / Lambda
    struct ASTExprNodeClosure: public ASTExprNode {
        typedef ::std::vector<::std::pair<ASTPattern, TypeRef>> argsT;

        argsT mArgs;
        TypeRef returnType;
        ASTExprNodeP mCode;
        bool isMove;   //< The closure takes ownership of all values
        bool isPinned; //< The closure cannot be moved (this is for generators)

        ASTExprNodeClosure(argsT args, TypeRef rv, ASTExprNodeP code, bool isMove, bool isPinned)
            : mArgs(::std::move(args))
            , returnType(::std::move(rv))
            , mCode(::std::move(code))
            , isMove(isMove)
            , isPinned(isPinned)
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
        ASTPath mPath;
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
        ASTPath mPath;
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
        ASTExprNodeP mSize; // if non-NULL, it's a sized array
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
        ASTPath mPath;

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
        RcString mName;

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
        ASTExprNodeP mValue;

        ASTExprNodeDeref(ASTExprNodeP value);

        static constexpr unsigned int kind = 34;
        unsigned int nodeKind() const override;
        void visit(ASTNodeVisitor& nv) override;
        void print(::std::ostream& os) const override;
        ASTExprNodeP clone() const override;
    };

    // Type cast ('as')
    struct ASTExprNodeCast: public ASTExprNode {
        ASTExprNodeP mValue;
        TypeRef mType;

        ASTExprNodeCast(ASTExprNodeP value, TypeRef&& dstType);

        static constexpr unsigned int kind = 35;
        unsigned int nodeKind() const override;
        void visit(ASTNodeVisitor& nv) override;
        void print(::std::ostream& os) const override;
        ASTExprNodeP clone() const override;
    };

    // Type annotation (': _')
    struct ASTExprNodeTypeAnnotation: public ASTExprNode {
        ASTExprNodeP mValue;
        TypeRef mType;

        ASTExprNodeTypeAnnotation(ASTExprNodeP value, TypeRef&& dstType);

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

        Type mType;
        ASTExprNodeP left;
        ASTExprNodeP right;

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
            BOX,    // 'box <expr>'
            INVERT, // '!<expr>'
            NEGATE, // '-<expr>'
            QMARK,  // '<expr>?'
            AWait,  // `.await`
        };

        enum Type mType;
        ASTExprNodeP mValue;

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

        virtual bool isConst() const {
            return false;
        }

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

