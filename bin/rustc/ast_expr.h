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
        AttributeList m_attrs;
        Span m_span;

    public:
        virtual ~ExprNode() = 0;

        virtual void visit(NodeVisitor& nv) = 0;
        virtual void print(::std::ostream& os) const = 0;
        virtual ExprNodeP clone() const = 0;
        virtual unsigned int node_kind() const = 0;

        void set_span(Span s) {
            m_span = ::std::move(s);
        }

        const Span& span() const {
            return m_span;
        }

        void set_attrs(AttributeList&& mi);

        AttributeList& attrs() {
            return m_attrs;
        }
    };

    struct ExprNodeBlock: public ExprNode {
        enum class Type {
            Bare,
            Unsafe,
            Const,
        };
        Type m_block_type;
        Ident m_label;
        ::std::shared_ptr<AST::Module> m_local_mod;

        struct Line {
            bool has_semicolon;
            ExprNodeP node;
        };

        ::std::vector<Line> m_nodes;

        ExprNodeBlock(::std::vector<Line> nodes = {});

        /// Shortcut for a block that returns a contained node
        ExprNodeBlock(ExprNodeP value);

        ExprNodeBlock(Type type, ::std::vector<Line> nodes, ::std::shared_ptr<AST::Module> local_mod);

        void push_stmt(AST::ExprNodeP node) {
            m_nodes.push_back({true, std::move(node)});
        }

        void push_tail_expr(AST::ExprNodeP node) {
            m_nodes.push_back({false, std::move(node)});
        }

        static constexpr unsigned int kind = 1;
        unsigned int node_kind() const override;
        void visit(NodeVisitor& nv) override;
        void print(::std::ostream& os) const override;
        ExprNodeP clone() const override;
    };

    struct ExprNodeAsyncBlock: public ExprNode {
        ExprNodeP m_inner;
        bool m_is_move;

        ExprNodeAsyncBlock(ExprNodeP inner, bool is_move);

        static constexpr unsigned int kind = 2;
        unsigned int node_kind() const override;
        void visit(NodeVisitor& nv) override;
        void print(::std::ostream& os) const override;
        ExprNodeP clone() const override;
    };

    struct ExprNodeGeneratorBlock: public ExprNode {
        ExprNodeP m_inner;
        bool m_is_move;

        ExprNodeGeneratorBlock(ExprNodeP inner, bool is_move);

        static constexpr unsigned int kind = 3;
        unsigned int node_kind() const override;
        void visit(NodeVisitor& nv) override;
        void print(::std::ostream& os) const override;
        ExprNodeP clone() const override;
    };

    struct ExprNodeTry: public ExprNode {
        ExprNodeP m_inner;

        ExprNodeTry(ExprNodeP inner);

        static constexpr unsigned int kind = 4;
        unsigned int node_kind() const override;
        void visit(NodeVisitor& nv) override;
        void print(::std::ostream& os) const override;
        ExprNodeP clone() const override;
    };

    struct ExprNodeMacro: public ExprNode {
        AST::Path m_path;
        RcString m_ident;
        ::TokenTree m_tokens;
        bool m_is_braced;
        Ident::Hygiene m_definition_hygiene;

        ExprNodeMacro(AST::Path name, RcString ident, ::TokenTree&& tokens, bool is_braced = false, Ident::Hygiene definition_hygiene = {});

        static constexpr unsigned int kind = 5;
        unsigned int node_kind() const override;
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

        ::std::string m_text;
        ::std::vector<ValRef> m_output;
        ::std::vector<ValRef> m_input;
        ::std::vector<::std::string> m_clobbers;
        ::std::vector<::std::string> m_flags;

        ExprNodeAsm(::std::string text, ::std::vector<ValRef> output, ::std::vector<ValRef> input, ::std::vector<::std::string> clobbers, ::std::vector<::std::string> flags);

        static constexpr unsigned int kind = 6;
        unsigned int node_kind() const override;
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
                AST::ExprNodeP val_in;
                AST::ExprNodeP val_out;
            })
        );

        AsmCommon::Options m_options;
        std::vector<AsmCommon::Line> m_lines;
        std::vector<Param> m_params;

        ExprNodeAsm2(AsmCommon::Options options, std::vector<AsmCommon::Line> lines, std::vector<Param> params);

        static constexpr unsigned int kind = 7;
        unsigned int node_kind() const override;
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
        } m_type;

        Ident m_target;
        ExprNodeP m_value;

        ExprNodeFlow(Type type, Ident target, ExprNodeP value);

        static constexpr unsigned int kind = 8;
        unsigned int node_kind() const override;
        void visit(NodeVisitor& nv) override;
        void print(::std::ostream& os) const override;
        ExprNodeP clone() const override;
    };

    struct ExprNodeLetBinding: public ExprNode {
        Pattern m_pat;
        TypeRef m_type;
        ExprNodeP m_value;
        ExprNodeP m_else;
        bool m_is_super;
        /// Allocated binding slots/indexes for the pattern in `let-else`
        ::std::pair<unsigned, unsigned> m_letelse_slots;

        ExprNodeLetBinding(Pattern pat, TypeRef type, ExprNodeP value, ExprNodeP else_arm = {}, bool is_super = false);

        static constexpr unsigned int kind = 9;
        unsigned int node_kind() const override;
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
        } m_op;

        ExprNodeP m_slot;
        ExprNodeP m_value;

        ExprNodeAssign();

        ExprNodeAssign(Operation op, ExprNodeP slot, ExprNodeP value);

        static constexpr unsigned int kind = 10;
        unsigned int node_kind() const override;
        void visit(NodeVisitor& nv) override;
        void print(::std::ostream& os) const override;
        ExprNodeP clone() const override;
    };

    struct ExprNodeCallPath: public ExprNode {
        Path m_path;
        ::std::vector<ExprNodeP> m_args;

        ExprNodeCallPath(Path&& path, ::std::vector<ExprNodeP>&& args);

        static constexpr unsigned int kind = 11;
        unsigned int node_kind() const override;
        void visit(NodeVisitor& nv) override;
        void print(::std::ostream& os) const override;
        ExprNodeP clone() const override;
    };

    struct ExprNodeCallMethod: public ExprNode {
        ExprNodeP m_val;
        PathNode m_method;
        ::std::vector<ExprNodeP> m_args;

        ExprNodeCallMethod(ExprNodeP obj, PathNode method, ::std::vector<ExprNodeP> args);

        static constexpr unsigned int kind = 12;
        unsigned int node_kind() const override;
        void visit(NodeVisitor& nv) override;
        void print(::std::ostream& os) const override;
        ExprNodeP clone() const override;
    };

    // Call an object (Fn/FnMut/FnOnce)
    struct ExprNodeCallObject: public ExprNode {
        ExprNodeP m_val;
        ::std::vector<ExprNodeP> m_args;

        ExprNodeCallObject(ExprNodeP val, ::std::vector<ExprNodeP>&& args);

        static constexpr unsigned int kind = 13;
        unsigned int node_kind() const override;
        void visit(NodeVisitor& nv) override;
        void print(::std::ostream& os) const override;
        ExprNodeP clone() const override;
    };

    struct ExprNodeLoop: public ExprNode {
        Ident m_label;
        ExprNodeP m_code;

        ExprNodeLoop();

        ExprNodeLoop(Ident label, ExprNodeP code);

        static constexpr unsigned int kind = 14;
        unsigned int node_kind() const override;
        void visit(NodeVisitor& nv) override;
        void print(::std::ostream& os) const override;
        ExprNodeP clone() const override;
    };

    struct ExprNodeFor: public ExprNode {
        Ident m_label;
        AST::Pattern m_pattern;
        ExprNodeP m_value;
        ExprNodeP m_code;

        ExprNodeFor(Ident label, AST::Pattern pattern, ExprNodeP val, ExprNodeP code);

        static constexpr unsigned int kind = 15;
        unsigned int node_kind() const override;
        void visit(NodeVisitor& nv) override;
        void print(::std::ostream& os) const override;
        ExprNodeP clone() const override;
    };

    struct IfLet_Condition {
        ::std::unique_ptr<AST::Pattern> opt_pat;
        ExprNodeP value;
    };

    struct ExprNodeWhile: public ExprNode {
        Ident m_label;
        std::vector<IfLet_Condition> m_conditions;
        ExprNodeP m_code;

        ExprNodeWhile(Ident label, std::vector<IfLet_Condition> conditions, ExprNodeP code);

        static constexpr unsigned int kind = 16;
        unsigned int node_kind() const override;
        void visit(NodeVisitor& nv) override;
        void print(::std::ostream& os) const override;
        ExprNodeP clone() const override;
    };

    struct ExprNodeMatchArm {
        AttributeList m_attrs;
        ::std::vector<Pattern> m_patterns;
        std::vector<IfLet_Condition> m_guard;

        ExprNodeP m_code;

        ExprNodeMatchArm();

        ExprNodeMatchArm(::std::vector<Pattern> patterns, std::vector<IfLet_Condition> guard, ExprNodeP code);
    };

    struct ExprNodeMatch: public ExprNode {
        ExprNodeP m_val;
        ::std::vector<ExprNodeMatchArm> m_arms;

        ExprNodeMatch(ExprNodeP val, ::std::vector<ExprNodeMatchArm> arms);

        static constexpr unsigned int kind = 17;
        unsigned int node_kind() const override;
        void visit(NodeVisitor& nv) override;
        void print(::std::ostream& os) const override;
        ExprNodeP clone() const override;
    };

    struct ExprNodeIf: public ExprNode {
        struct Arm {
            std::vector<IfLet_Condition> m_conditions;
            ExprNodeP m_body;
        };

        std::vector<Arm> m_arms;
        ExprNodeP m_else;

        ExprNodeIf(std::vector<Arm> arms, ExprNodeP else_code);

        static constexpr unsigned int kind = 18;
        unsigned int node_kind() const override;
        void visit(NodeVisitor& nv) override;
        void print(::std::ostream& os) const override;
        ExprNodeP clone() const override;
    };

    /// Represents `_` in expression position
    struct ExprNodeWildcardPattern: public ExprNode {
        static constexpr unsigned int kind = 19;
        unsigned int node_kind() const override;
        void visit(NodeVisitor& nv) override;
        void print(::std::ostream& os) const override;
        ExprNodeP clone() const override;
    };

    // Literal integer
    struct ExprNodeInteger: public ExprNode {
        enum eCoreType m_datatype;
        U128 m_value;

        ExprNodeInteger(U128 value, enum eCoreType datatype);

        static constexpr unsigned int kind = 20;
        unsigned int node_kind() const override;
        void visit(NodeVisitor& nv) override;
        void print(::std::ostream& os) const override;
        ExprNodeP clone() const override;
    };

    // Literal float
    struct ExprNodeFloat: public ExprNode {
        enum eCoreType m_datatype;
        FloatValue m_value;

        ExprNodeFloat(FloatValue value, enum eCoreType datatype);

        static constexpr unsigned int kind = 21;
        unsigned int node_kind() const override;
        void visit(NodeVisitor& nv) override;
        void print(::std::ostream& os) const override;
        ExprNodeP clone() const override;
    };

    // Literal boolean
    struct ExprNodeBool: public ExprNode {
        bool m_value;

        ExprNodeBool(bool value);

        static constexpr unsigned int kind = 22;
        unsigned int node_kind() const override;
        void visit(NodeVisitor& nv) override;
        void print(::std::ostream& os) const override;
        ExprNodeP clone() const override;
    };

    // Literal string
    struct ExprNodeString: public ExprNode {
        ::std::string m_value;
        /// Hygiene for format strings
        Ident::Hygiene m_hygiene;

        ExprNodeString(::std::string value, Ident::Hygiene h = {});

        static constexpr unsigned int kind = 23;
        unsigned int node_kind() const override;
        void visit(NodeVisitor& nv) override;
        void print(::std::ostream& os) const override;
        ExprNodeP clone() const override;
    };

    // Literal byte string
    struct ExprNodeByteString: public ExprNode {
        ::std::string m_value;

        ExprNodeByteString(::std::string value);

        static constexpr unsigned int kind = 24;
        unsigned int node_kind() const override;
        void visit(NodeVisitor& nv) override;
        void print(::std::ostream& os) const override;
        ExprNodeP clone() const override;
    };

    // Literal C string
    struct ExprNodeCString: public ExprNode {
        ::std::string m_value;

        ExprNodeCString(::std::string value);

        static constexpr unsigned int kind = 25;
        unsigned int node_kind() const override;
        void visit(NodeVisitor& nv) override;
        void print(::std::ostream& os) const override;
        ExprNodeP clone() const override;
    };

    // Closure / Lambda
    struct ExprNodeClosure: public ExprNode {
        typedef ::std::vector<::std::pair<AST::Pattern, TypeRef>> args_t;

        args_t m_args;
        TypeRef m_return;
        ExprNodeP m_code;
        bool m_is_move;   //< The closure takes ownership of all values
        bool m_is_pinned; //< The closure cannot be moved (this is for generators)

        ExprNodeClosure(args_t args, TypeRef rv, ExprNodeP code, bool is_move, bool is_pinned)
            : m_args(::std::move(args))
            , m_return(::std::move(rv))
            , m_code(::std::move(code))
            , m_is_move(is_move)
            , m_is_pinned(is_pinned)
        {
        }

        static constexpr unsigned int kind = 26;
        unsigned int node_kind() const override;
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

        typedef ::std::vector<Ent> t_values;
        Path m_path;
        ExprNodeP m_base_value;
        t_values m_values;

        ExprNodeStructLiteral(Path path, ExprNodeP base_value, t_values&& values);

        static constexpr unsigned int kind = 27;
        unsigned int node_kind() const override;
        void visit(NodeVisitor& nv) override;
        void print(::std::ostream& os) const override;
        ExprNodeP clone() const override;
    };

    // Struct literal pattern only
    // This implicitly has a `..` in it
    struct ExprNodeStructLiteralPattern: public ExprNode {
        typedef ::std::vector<ExprNodeStructLiteral::Ent> t_values;
        Path m_path;
        t_values m_values;

        ExprNodeStructLiteralPattern(Path path, t_values&& values);

        static constexpr unsigned int kind = 28;
        unsigned int node_kind() const override;
        void visit(NodeVisitor& nv) override;
        void print(::std::ostream& os) const override;
        ExprNodeP clone() const override;
    };

    // Array
    struct ExprNodeArray: public ExprNode {
        ExprNodeP m_size; // if non-NULL, it's a sized array
        ::std::vector<ExprNodeP> m_values;

        ExprNodeArray(::std::vector<ExprNodeP> vals);

        ExprNodeArray(ExprNodeP val, ExprNodeP size);

        static constexpr unsigned int kind = 29;
        unsigned int node_kind() const override;
        void visit(NodeVisitor& nv) override;
        void print(::std::ostream& os) const override;
        ExprNodeP clone() const override;
    };

    // Tuple
    struct ExprNodeTuple: public ExprNode {
        ::std::vector<ExprNodeP> m_values;

        ExprNodeTuple(::std::vector<ExprNodeP> vals);

        static constexpr unsigned int kind = 30;
        unsigned int node_kind() const override;
        void visit(NodeVisitor& nv) override;
        void print(::std::ostream& os) const override;
        ExprNodeP clone() const override;
    };

    // Variable / Constant
    struct ExprNodeNamedValue: public ExprNode {
        Path m_path;

        ExprNodeNamedValue(Path path);

        static constexpr unsigned int kind = 31;
        unsigned int node_kind() const override;
        void visit(NodeVisitor& nv) override;
        void print(::std::ostream& os) const override;
        ExprNodeP clone() const override;
    };

    // Field dereference
    struct ExprNodeField: public ExprNode {
        ExprNodeP m_obj;
        RcString m_name;

        ExprNodeField(ExprNodeP obj, RcString name);

        static constexpr unsigned int kind = 32;
        unsigned int node_kind() const override;
        void visit(NodeVisitor& nv) override;
        void print(::std::ostream& os) const override;
        ExprNodeP clone() const override;
    };

    struct ExprNodeIndex: public ExprNode {
        ExprNodeP m_obj;
        ExprNodeP m_idx;

        ExprNodeIndex(ExprNodeP obj, ExprNodeP idx);

        static constexpr unsigned int kind = 33;
        unsigned int node_kind() const override;
        void visit(NodeVisitor& nv) override;
        void print(::std::ostream& os) const override;
        ExprNodeP clone() const override;
    };

    // Pointer dereference
    struct ExprNodeDeref: public ExprNode {
        ExprNodeP m_value;

        ExprNodeDeref(ExprNodeP value);

        static constexpr unsigned int kind = 34;
        unsigned int node_kind() const override;
        void visit(NodeVisitor& nv) override;
        void print(::std::ostream& os) const override;
        ExprNodeP clone() const override;
    };

    // Type cast ('as')
    struct ExprNodeCast: public ExprNode {
        ExprNodeP m_value;
        TypeRef m_type;

        ExprNodeCast(ExprNodeP value, TypeRef&& dst_type);

        static constexpr unsigned int kind = 35;
        unsigned int node_kind() const override;
        void visit(NodeVisitor& nv) override;
        void print(::std::ostream& os) const override;
        ExprNodeP clone() const override;
    };

    // Type annotation (': _')
    struct ExprNodeTypeAnnotation: public ExprNode {
        ExprNodeP m_value;
        TypeRef m_type;

        ExprNodeTypeAnnotation(ExprNodeP value, TypeRef&& dst_type);

        static constexpr unsigned int kind = 36;
        unsigned int node_kind() const override;
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

        Type m_type;
        ExprNodeP m_left;
        ExprNodeP m_right;

        ExprNodeBinOp(Type type, ExprNodeP left, ExprNodeP right);

        static constexpr unsigned int kind = 37;
        unsigned int node_kind() const override;
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

        enum Type m_type;
        ExprNodeP m_value;

        ExprNodeUniOp(Type type, ExprNodeP value);

        static constexpr unsigned int kind = 38;
        unsigned int node_kind() const override;
        void visit(NodeVisitor& nv) override;
        void print(::std::ostream& os) const override;
        ExprNodeP clone() const override;
    };

    // A lexical rib introduced by a block-local macro definition. Expansion
    // preserves it until local-variable and label resolution have crossed the
    // definition at the correct source position.
    struct ExprNodeMacroDefinition: public ExprNode {
        unsigned int m_definition_id;
        Ident::Hygiene m_token_hygiene;
        Ident::Hygiene m_definition_hygiene;

        ExprNodeMacroDefinition(unsigned int definition_id, Ident::Hygiene token_hygiene, Ident::Hygiene definition_hygiene);

        static constexpr unsigned int kind = 39;
        unsigned int node_kind() const override;
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
