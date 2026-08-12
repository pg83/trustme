#pragma once

#include "hir_pattern.h"
#include "hir_type.h"
#include "span.h"
#include "hir_visitor.h"
#include "hir_typeck_common.h"
#include "hir_asm.h"
#include <memory>

namespace HIR {

    typedef ::std::vector<::std::pair<const ::HIR::SimplePath*, const ::HIR::Trait*>> t_trait_list;

    // Indicates how a result is used
    enum class ValueUsage {
        // Not yet known (defalt state)
        Unknown,
        // Value is borrowed (shared)
        Borrow,
        // Value is mutated or uniquely borrowed
        Mutate,
        // Value is moved
        Move,
    };

    ::std::ostream& operator<<(::std::ostream& os, const ValueUsage& x);

    class GenericParams;

    class ExprVisitor;

    class ExprNode {
    public:
        Span m_span;
        ::HIR::TypeRef m_res_type; // TODO: Replace this with an index into an ivar table
        //unsigned m_res_type_idx;
        ValueUsage m_usage = ValueUsage::Unknown;

        const Span& span() const {
            return m_span;
        }

        virtual void visit(ExprVisitor& v) = 0;
        virtual unsigned int node_kind() const = 0;

        ExprNode(Span sp);

        virtual ~ExprNode();

        const char* type_name() const;
    };

    struct ExprNodeBlock: public ExprNode {
        bool m_is_unsafe;
        ::std::vector<ExprNodeP> m_nodes;
        ExprNodeP m_value_node; // can be null

        ::HIR::SimplePath m_local_mod;
        t_trait_list m_traits;

        ExprNodeBlock(Span sp);

        ExprNodeBlock(Span sp, bool is_unsafe, ::std::vector<ExprNodeP> nodes, ExprNodeP value_node);

        static constexpr unsigned int kind = 1;
        unsigned int node_kind() const override;
        void visit(ExprVisitor& nv) override;
    };

    struct ExprNodeConstBlock: public ExprNode {
        ExprNodeP m_inner;

        ExprNodeConstBlock(Span sp, ExprNodeP inner);

        static constexpr unsigned int kind = 2;
        unsigned int node_kind() const override;
        void visit(ExprVisitor& nv) override;
    };

    struct ExprNodeAsm: public ExprNode {
        struct ValRef {
            ::std::string spec;
            ::HIR::ExprNodeP value;
        };

        ::std::string m_template;
        ::std::vector<ValRef> m_outputs;
        ::std::vector<ValRef> m_inputs;
        ::std::vector<::std::string> m_clobbers;
        ::std::vector<::std::string> m_flags;

        ExprNodeAsm(Span sp, ::std::string tpl_str, ::std::vector<ValRef> outputs, ::std::vector<ValRef> inputs, ::std::vector<::std::string> clobbers, ::std::vector<::std::string> flags)
            : ExprNode(mv$(sp))
            , m_template(mv$(tpl_str))
            , m_outputs(mv$(outputs))
            , m_inputs(mv$(inputs))
            , m_clobbers(mv$(clobbers))
            , m_flags(mv$(flags))
        {
        }

        static constexpr unsigned int kind = 3;
        unsigned int node_kind() const override;
        void visit(ExprVisitor& nv) override;
    };

    struct ExprNodeAsm2: public ExprNode {
        TAGGED_UNION(
            Param,
            Const,
            (Const, HIR::ExprNodeP),
            (Sym, HIR::Path),
            (RegSingle,
             struct {
                 AsmCommon::Direction dir;
                 AsmCommon::RegisterSpec spec;
                 HIR::ExprNodeP val;
             }),
            (Reg, struct {
                AsmCommon::Direction dir;
                AsmCommon::RegisterSpec spec;
                HIR::ExprNodeP val_in;
                HIR::ExprNodeP val_out;
            })
        );

        AsmCommon::Options m_options;
        std::vector<AsmCommon::Line> m_lines;
        std::vector<Param> m_params;

        ExprNodeAsm2(Span sp, AsmCommon::Options options, std::vector<AsmCommon::Line> lines, std::vector<Param> params);

        static constexpr unsigned int kind = 4;
        unsigned int node_kind() const override;
        void visit(ExprVisitor& nv) override;
    };

    struct ExprNodeReturn: public ExprNode {
        ::HIR::ExprNodeP m_value;

        ExprNodeReturn(Span sp, ::HIR::ExprNodeP value);

        static constexpr unsigned int kind = 5;
        unsigned int node_kind() const override;
        void visit(ExprVisitor& nv) override;
    };

    /// @brief `foo = yield bar` generator yield statement
    struct ExprNodeYield: public ExprNode {
        ::HIR::ExprNodeP m_value;

        ExprNodeYield(Span sp, ::HIR::ExprNodeP value);

        static constexpr unsigned int kind = 6;
        unsigned int node_kind() const override;
        void visit(ExprVisitor& nv) override;
    };

    /// @brief Async Wait (the `.await` postfix operator)
    struct ExprNodeAWait: public ExprNode {
        ::HIR::ExprNodeP m_value;

        ExprNodeAWait(Span sp, ::HIR::ExprNodeP value);

        static constexpr unsigned int kind = 7;
        unsigned int node_kind() const override;
        void visit(ExprVisitor& nv) override;
    };

    struct ExprNodeLoop: public ExprNode {
        RcString m_label;
        ::HIR::ExprNodeP m_code;
        bool m_diverges = false;
        bool m_require_label = false;

        ExprNodeLoop(Span sp, RcString label, ::HIR::ExprNodeP code, bool require_label = false);

        static constexpr unsigned int kind = 8;
        unsigned int node_kind() const override;
        void visit(ExprVisitor& nv) override;
    };

    struct ExprNodeLoopControl: public ExprNode {
        RcString m_label;
        bool m_continue;
        ::HIR::ExprNodeP m_value;

        const ExprNodeLoop* m_target_node; // populated by expr_cs__enum.cpp

        ExprNodeLoopControl(Span sp, RcString label, bool cont, ::HIR::ExprNodeP value = {});

        static constexpr unsigned int kind = 9;
        unsigned int node_kind() const override;
        void visit(ExprVisitor& nv) override;
    };

    struct ExprNodeLet: public ExprNode {
        ::HIR::Pattern m_pattern;
        ::HIR::TypeRef m_type;
        ::HIR::ExprNodeP m_value;
        bool m_is_super;

        ExprNodeLet(Span sp, ::HIR::Pattern pat, ::HIR::TypeRef ty, ::HIR::ExprNodeP val, bool is_super = false);

        static constexpr unsigned int kind = 10;
        unsigned int node_kind() const override;
        void visit(ExprVisitor& nv) override;
    };

    struct ExprNodeMatch: public ExprNode {
        struct Guard {
            /// Guard pattern, always set (but might be `true`/`false`)
            ::HIR::Pattern pat;
            /// Guard value
            ::HIR::ExprNodeP val;
            /// Indicates that this guard is an `if` (changes scoping rules, and tweaks how typecheck happens)
            bool is_if;
        };

        struct Arm {
            // Patterns, must be non-empty
            ::std::vector<::HIR::Pattern> m_patterns;
            // A chained (&&) list of guards
            ::std::vector<Guard> m_guards;
            // Match arm body, required
            ::HIR::ExprNodeP m_code;
        };

        ::HIR::ExprNodeP m_value;
        ::std::vector<Arm> m_arms;
        bool m_is_let_else;

        ExprNodeMatch(Span sp, ::HIR::ExprNodeP val, ::std::vector<Arm> arms, bool is_let_else = false);

        static constexpr unsigned int kind = 11;
        unsigned int node_kind() const override;
        void visit(ExprVisitor& nv) override;
    };

    struct ExprNodeAssign: public ExprNode {
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

        Op m_op;
        ExprNodeP m_slot;
        ExprNodeP m_value;

        ExprNodeAssign(Span sp, Op op, ::HIR::ExprNodeP slot, ::HIR::ExprNodeP value);

        static constexpr unsigned int kind = 12;
        unsigned int node_kind() const override;
        void visit(ExprVisitor& nv) override;
    };

    struct ExprNodeBinOp: public ExprNode {
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

        Op m_op;
        ::HIR::ExprNodeP m_left;
        ::HIR::ExprNodeP m_right;

        ExprNodeBinOp(Span sp, Op op, ::HIR::ExprNodeP left, ::HIR::ExprNodeP right);

        static constexpr unsigned int kind = 13;
        unsigned int node_kind() const override;
        void visit(ExprVisitor& nv) override;
    };

    struct ExprNodeUniOp: public ExprNode {
        enum class Op {
            Invert, // '!<expr>'
            Negate, // '-<expr>'
        };

        static const char* opname(Op v);

        Op m_op;
        ::HIR::ExprNodeP m_value;

        ExprNodeUniOp(Span sp, Op op, ::HIR::ExprNodeP value);

        static constexpr unsigned int kind = 14;
        unsigned int node_kind() const override;
        void visit(ExprVisitor& nv) override;
    };

    struct ExprNodeBorrow: public ExprNode {
        ::HIR::BorrowType m_type;
        ::HIR::ExprNodeP m_value;

        /// <summary>
        /// Flag set by the first pass of SBC to both inform the second pass and change Lifetime Infer's behaviour
        /// </summary>
        bool m_is_valid_static_borrow_constant;

        ExprNodeBorrow(Span sp, ::HIR::BorrowType bt, ::HIR::ExprNodeP value);

        static constexpr unsigned int kind = 15;
        unsigned int node_kind() const override;
        void visit(ExprVisitor& nv) override;
    };

    struct ExprNodeRawBorrow: public ExprNode {
        ::HIR::BorrowType m_type;
        ::HIR::ExprNodeP m_value;

        ExprNodeRawBorrow(Span sp, ::HIR::BorrowType bt, ::HIR::ExprNodeP value);

        static constexpr unsigned int kind = 16;
        unsigned int node_kind() const override;
        void visit(ExprVisitor& nv) override;
    };

    struct ExprNodeCast: public ExprNode {
        ::HIR::ExprNodeP m_value;
        ::HIR::TypeRef m_dst_type;

        ExprNodeCast(Span sp, ::HIR::ExprNodeP value, ::HIR::TypeRef dst_type);

        static constexpr unsigned int kind = 17;
        unsigned int node_kind() const override;
        void visit(ExprVisitor& nv) override;
    };

    // Magical pointer unsizing operation:
    // - `&[T; n] -> &[T]`
    // - `&T -> &Trait`
    // - `Box<T> -> Box<Trait>`
    // NOTE: Also used for type ascription
    struct ExprNodeUnsize: public ExprNode {
        ::HIR::ExprNodeP m_value;
        ::HIR::TypeRef m_dst_type;
        bool m_is_array_to_slice_adjustment = false;

        ExprNodeUnsize(Span sp, ::HIR::ExprNodeP value, ::HIR::TypeRef dst_type);

        static constexpr unsigned int kind = 18;
        unsigned int node_kind() const override;
        void visit(ExprVisitor& nv) override;
    };

    struct ExprNodeIndex: public ExprNode {
        ::HIR::ExprNodeP m_value;
        ::HIR::ExprNodeP m_index;

        struct {
            ::HIR::TypeRef index_ty;
        } m_cache;

        ExprNodeIndex(Span sp, ::HIR::ExprNodeP val, ::HIR::ExprNodeP index);

        static constexpr unsigned int kind = 19;
        unsigned int node_kind() const override;
        void visit(ExprVisitor& nv) override;
    };

    // unary `*`
    struct ExprNodeDeref: public ExprNode {
        enum class TraitUsed {
            // Nodes created after type checking can retain the historical
            // structural behaviour. Source dereferences are resolved to one
            // of the two explicit choices below.
            Unknown,
            Builtin,
            Trait,
        };

        ::HIR::ExprNodeP m_value;
        TraitUsed m_trait_used;

        ExprNodeDeref(Span sp, ::HIR::ExprNodeP val);

        static constexpr unsigned int kind = 20;
        unsigned int node_kind() const override;
        void visit(ExprVisitor& nv) override;
    };

    /// `box` and `in`/`<-`
    struct ExprNodeEmplace: public ExprNode {
        /// This influences the ops trait used
        enum class Type {
            Noop, // Hack to allow coercion - acts as a no-op node
            Placer,
            Boxer,
        };

        Type m_type;
        ExprNodeP m_place;
        ExprNodeP m_value;

        ExprNodeEmplace(Span sp, Type ty, ::HIR::ExprNodeP place, ::HIR::ExprNodeP val);

        static constexpr unsigned int kind = 21;
        unsigned int node_kind() const override;
        void visit(ExprVisitor& nv) override;
    };

    struct ExprNodeTupleVariant: public ExprNode {
        // Path to variant/struct
        ::HIR::GenericPath m_path;
        bool m_is_struct;
        ::std::vector<ExprNodeP> m_args;

        // - Cache for typeck
        ::std::vector<::HIR::TypeRef> m_arg_types;

        ExprNodeTupleVariant(Span sp, ::HIR::GenericPath path, bool is_struct, ::std::vector<::HIR::ExprNodeP> args);

        static constexpr unsigned int kind = 22;
        unsigned int node_kind() const override;
        void visit(ExprVisitor& nv) override;
    };

    struct ExprCallCache {
        ::std::vector<::HIR::TypeRef> m_arg_types;
        const ::HIR::GenericParams* m_fcn_params;
        const ::HIR::GenericParams* m_top_params;
        const ::HIR::Function* m_fcn;

        ::std::unique_ptr<Monomorphiser> m_monomorph;
    };

    struct ExprNodeCallPath: public ExprNode {
        ::HIR::Path m_path;
        ::std::vector<ExprNodeP> m_args;

        // - Cache for typeck
        ExprCallCache m_cache;

        ExprNodeCallPath(Span sp, ::HIR::Path path, ::std::vector<::HIR::ExprNodeP> args);

        static constexpr unsigned int kind = 23;
        unsigned int node_kind() const override;
        void visit(ExprVisitor& nv) override;
    };

    struct ExprNodeCallValue: public ExprNode {
        ::HIR::ExprNodeP m_value;
        ::std::vector<ExprNodeP> m_args;

        // - Argument types used as coercion targets
        ::std::vector<::HIR::TypeRef> m_arg_ivars;

        // - Cache for typeck
        ::std::vector<::HIR::TypeRef> m_arg_types;

        // Indicates what trait should/is being used for this call
        // - Determined by typeck using the present trait bound (also adds borrows etc)
        // - If the called value is a closure, this stays a Unknown until closure expansion
        enum class TraitUsed {
            Unknown,
            Fn,
            FnMut,
            FnOnce,
        };
        TraitUsed m_trait_used = TraitUsed::Unknown;

        ExprNodeCallValue(Span sp, ::HIR::ExprNodeP val, ::std::vector<::HIR::ExprNodeP> args);

        static constexpr unsigned int kind = 24;
        unsigned int node_kind() const override;
        void visit(ExprVisitor& nv) override;
    };

    // TODO: Refactor to support efficient method chaining
    struct ExprNodeCallMethod: public ExprNode {
        /// @brief Method reciever value
        ::HIR::ExprNodeP m_value;
        /// @brief Method name
        RcString m_method;
        /// @brief Generic parameters to the method
        ::HIR::PathParams m_params;
        /// @brief Argument values
        ::std::vector<::HIR::ExprNodeP> m_args;

        // - Set during typeck to the real path to the method
        ::HIR::Path m_method_path;
        // - Cache of argument/return types
        ExprCallCache m_cache;

        // - List of possible traits (in-scope traits that contain this method)
        t_trait_list m_traits;
        // - A pool of ivars to use for searching for trait impls, with type
        // ivars first and const value ivars after them.
        ::std::vector<unsigned int> m_trait_param_ivars;
        unsigned int m_trait_param_type_ivars = 0;

        ExprNodeCallMethod(Span sp, ::HIR::ExprNodeP val, RcString method_name, ::HIR::PathParams params, ::std::vector<::HIR::ExprNodeP> args);

        static constexpr unsigned int kind = 25;
        unsigned int node_kind() const override;
        void visit(ExprVisitor& nv) override;
    };

    struct ExprNodeField: public ExprNode {
        ::HIR::ExprNodeP m_value;
        RcString m_field;

        ExprNodeField(Span sp, ::HIR::ExprNodeP val, RcString field);

        static constexpr unsigned int kind = 26;
        unsigned int node_kind() const override;
        void visit(ExprVisitor& nv) override;
    };

    struct ExprNodeLiteral: public ExprNode {
        TAGGED_UNION(
            Data,
            Integer,
            (Integer,
             struct {
                 ::HIR::CoreType m_type; // if not an integer type, it's unknown
                 U128 m_value;
             }),
            (Float,
             struct {
                 ::HIR::CoreType m_type; // If not a float type, it's unknown
                 FloatValue m_value;
             }),
            (Boolean, bool),
            (String, ::std::string),
            (CString, struct { ::std::string v; }),
            (ByteString, ::std::vector<char>)
        );

        Data m_data;

        ExprNodeLiteral(Span sp, Data data);

        static constexpr unsigned int kind = 27;
        unsigned int node_kind() const override;
        void visit(ExprVisitor& nv) override;
    };

    struct ExprNodeUnitVariant: public ExprNode {
        // Path to variant/struct
        ::HIR::GenericPath m_path;
        bool m_is_struct;

        ExprNodeUnitVariant(Span sp, ::HIR::GenericPath path, bool is_struct);

        static constexpr unsigned int kind = 28;
        unsigned int node_kind() const override;
        void visit(ExprVisitor& nv) override;
    };

    struct ExprNodePathValue: public ExprNode {
        enum Target {
            UNKNOWN,
            FUNCTION,
            STRUCT_CONSTR,
            ENUM_VAR_CONSTR,
            STATIC,
            CONSTANT,
        };

        ::HIR::Path m_path;
        Target m_target;

        ExprNodePathValue(Span sp, ::HIR::Path path, Target target);

        static constexpr unsigned int kind = 29;
        unsigned int node_kind() const override;
        void visit(ExprVisitor& nv) override;
    };

    struct ExprNodeVariable: public ExprNode {
        RcString m_name;
        unsigned int m_slot;

        ExprNodeVariable(Span sp, RcString name, unsigned int slot);

        static constexpr unsigned int kind = 30;
        unsigned int node_kind() const override;
        void visit(ExprVisitor& nv) override;
    };

    struct ExprNodeConstParam: public ExprNode {
        RcString m_name;
        unsigned int m_binding;

        ExprNodeConstParam(Span sp, RcString name, unsigned int binding);

        static constexpr unsigned int kind = 31;
        unsigned int node_kind() const override;
        void visit(ExprVisitor& nv) override;
    };

    struct ExprNodeStructLiteral: public ExprNode {
        typedef ::std::vector<::std::pair<RcString, ExprNodeP>> t_values;

        ::HIR::TypeRef m_type;
        bool m_is_struct;
        /// Alternative to `m_base_value`, indicates that the struct's field defaults are to be used
        bool m_use_defaults;
        /// Base value (`..foo`)
        ::HIR::ExprNodeP m_base_value;
        t_values m_values;

        /// Actual path extracted from the TypeRef (populated after inner UFCS expansion)
        ::HIR::GenericPath m_real_path;
        /// Monomorphised types of each field.
        ::std::vector<::HIR::TypeRef> m_value_types;

        ExprNodeStructLiteral(Span sp, ::HIR::TypeRef ty, bool is_struct, ::HIR::ExprNodeP base_value, t_values values);

        ExprNodeStructLiteral(Span sp, ::HIR::TypeRef ty, bool is_struct, bool, t_values values);

        static constexpr unsigned int kind = 32;
        unsigned int node_kind() const override;
        void visit(ExprVisitor& nv) override;
    };

    struct ExprNodeTuple: public ExprNode {
        ::std::vector<::HIR::ExprNodeP> m_vals;

        ExprNodeTuple(Span sp, ::std::vector<::HIR::ExprNodeP> vals);

        static constexpr unsigned int kind = 33;
        unsigned int node_kind() const override;
        void visit(ExprVisitor& nv) override;
    };

    struct ExprNodeArrayList: public ExprNode {
        ::std::vector<::HIR::ExprNodeP> m_vals;

        ExprNodeArrayList(Span sp, ::std::vector<::HIR::ExprNodeP> vals);

        static constexpr unsigned int kind = 34;
        unsigned int node_kind() const override;
        void visit(ExprVisitor& nv) override;
    };

    // TODO: Might want a second variant for dynamically-sized arrays
    struct ExprNodeArraySized: public ExprNode {
        ::HIR::ExprNodeP m_val;
        ::HIR::ArraySize m_size;

        ExprNodeArraySized(Span sp, ::HIR::ExprNodeP val, ::HIR::ExprPtr size);

        static constexpr unsigned int kind = 35;
        unsigned int node_kind() const override;
        void visit(ExprVisitor& nv) override;
    };

    struct ExprNodeClosure: public ExprNode {
        typedef ::std::vector<::std::pair<::HIR::Pattern, ::HIR::TypeRef>> args_t;

        args_t m_args;
        ::HIR::TypeRef m_return;
        ::HIR::ExprNodeP m_code;
        bool m_is_move = false;

        enum class Class {
            Unknown,
            NoCapture,
            Shared,
            Mut,
            Once,
        } m_class = Class::Unknown;
        bool m_is_copy = true; // Assume that closures are Copy/Clone (for the purposes of typecheck) until AVU is run

        // - Cache between the AVU and ExpandClosures passes
        struct AvuCache {
            ::std::vector<unsigned int> local_vars;

            struct Capture {
                // Variable binding index
                unsigned int root_slot;
                // Fields used to access that variable
                std::vector<RcString> fields;
                ::HIR::ValueUsage usage;
            };

            ::std::vector<Capture> captured_vars;
        } m_avu_cache;

        // Lifetime for captured borrows, filled by lifetime infer pass
        ::HIR::LifetimeRef m_capture_lifetime;
        // - Path to the generated closure type
        const ::HIR::Struct* m_obj_ptr = nullptr;
        ::HIR::GenericPath m_obj_path_base;
        ::HIR::GenericPath m_obj_path;
        ::std::vector<::HIR::ExprNodeP> m_captures;

        ExprNodeClosure(Span sp, args_t args, ::HIR::TypeRef rv, ::HIR::ExprNodeP code, bool is_move);

        static constexpr unsigned int kind = 36;
        unsigned int node_kind() const override;
        void visit(ExprVisitor& nv) override;
    };

    ::std::ostream& operator<<(::std::ostream& os, const ExprNodeClosure::AvuCache::Capture& x);

    struct ExprNodeGenerator: public ExprNode {
        //ExprNodeClosure::args_t    m_args;
        ::HIR::TypeRef m_return;
        ::HIR::TypeRef m_resume_ty;
        ::HIR::TypeRef m_yield_ty;
        ::HIR::ExprNodeP m_code;
        bool m_is_move;
        bool m_is_pinned;

        // AnnotateValueUsage cache/information
        struct AvuCache {
            ::std::vector<unsigned int> local_vars;
            ::std::vector<::std::pair<unsigned int, ::HIR::ValueUsage>> captured_vars;
        } m_avu_cache;

        // Generated type information
        const ::HIR::Struct* m_obj_ptr = nullptr;
        ::HIR::GenericPath m_obj_path;
        // Lifetime for captured borrows, filled by lifetime infer pass
        ::HIR::LifetimeRef m_capture_lifetime;
        // Captured variables (used for emitting the constructor)
        ::std::vector<::HIR::ExprNodeP> m_captures;
        // State data type (needed for initialising)
        ::HIR::TypeRef m_state_data_type;

        ExprNodeGenerator(
            Span sp,
            ::HIR::TypeRef rv,
            ::HIR::TypeRef resume_ty,
            ::HIR::TypeRef yield_ty,
            ::HIR::ExprNodeP code,
            bool is_move,
            bool is_pinned
        );

        static constexpr unsigned int kind = 37;
        unsigned int node_kind() const override;
        void visit(ExprVisitor& nv) override;
    };

    /// <summary>
    /// Top-level wrapper for the generator method
    /// </summary>
    struct ExprNodeGeneratorWrapper: public ExprNode {
        //ExprNodeClosure::args_t    m_args;
        bool m_is_future;
        ::HIR::TypeRef m_return;
        ::HIR::TypeRef m_yield_ty;
        ::HIR::ExprNodeP m_code;

        // Generated type information
        const ::HIR::Struct* m_obj_ptr = nullptr;
        ::HIR::GenericPath m_obj_path;

        ::HIR::TypeRef m_state_data_type;
        ::HIR::SimplePath m_state_idx_enum;

        ::HIR::Function* m_drop_fcn_ptr = nullptr;

        ::std::vector<HIR::ValueUsage> m_capture_usages;

        ExprNodeGeneratorWrapper(
            Span sp,
            ::HIR::TypeRef rv,
            ::HIR::TypeRef yield_ty,
            ::HIR::ExprNodeP code,
            bool is_future
        );

        static constexpr unsigned int kind = 38;
        unsigned int node_kind() const override;
        void visit(ExprVisitor& nv) override;
    };

    struct ExprNodeAsyncBlock: public ExprNode {
        ::HIR::ExprNodeP m_code;
        bool m_is_move;

        ExprNodeGenerator::AvuCache m_avu_cache;

        // Generated type information
        const ::HIR::Struct* m_obj_ptr = nullptr;
        ::HIR::GenericPath m_obj_path;
        // Lifetime for captured borrows, filled by lifetime infer pass
        ::HIR::LifetimeRef m_capture_lifetime;
        // Captured variables (used for emitting the constructor)
        ::std::vector<::HIR::ExprNodeP> m_captures;
        // State data type (needed for initialising)
        ::HIR::TypeRef m_state_data_type;

        ExprNodeAsyncBlock(Span sp, ::HIR::ExprNodeP code, bool is_move);

        static constexpr unsigned int kind = 39;
        unsigned int node_kind() const override;
        void visit(ExprVisitor& nv) override;
    };

    class ExprVisitor {
    public:
        virtual ~ExprVisitor() = default;
        virtual void visit_node_ptr(::HIR::ExprNodeP& node_ptr);
        virtual void visit_node(ExprNode& node);
#define NV(nt) virtual void visit(nt& n) = 0;

        NV(ExprNodeBlock)
        NV(ExprNodeConstBlock)
        NV(ExprNodeAsm)
        NV(ExprNodeAsm2)
        NV(ExprNodeReturn)
        NV(ExprNodeYield)
        NV(ExprNodeAWait)
        NV(ExprNodeLet)
        NV(ExprNodeLoop)
        NV(ExprNodeLoopControl)
        NV(ExprNodeMatch)

        NV(ExprNodeAssign)
        NV(ExprNodeBinOp)
        NV(ExprNodeUniOp)
        NV(ExprNodeBorrow)
        NV(ExprNodeRawBorrow)
        NV(ExprNodeCast)   // Conversion
        NV(ExprNodeUnsize) // Coercion
        NV(ExprNodeIndex)
        NV(ExprNodeDeref)
        NV(ExprNodeEmplace)

        NV(ExprNodeTupleVariant);
        NV(ExprNodeCallPath);
        NV(ExprNodeCallValue);
        NV(ExprNodeCallMethod);
        NV(ExprNodeField);

        NV(ExprNodeLiteral);
        NV(ExprNodeUnitVariant);
        NV(ExprNodePathValue);
        NV(ExprNodeVariable);
        NV(ExprNodeConstParam);

        NV(ExprNodeStructLiteral);
        NV(ExprNodeTuple);
        NV(ExprNodeArrayList);
        NV(ExprNodeArraySized);

        NV(ExprNodeClosure);
        NV(ExprNodeGenerator);
        NV(ExprNodeGeneratorWrapper);
        NV(ExprNodeAsyncBlock);
#undef NV
    };

    class ExprVisitorDef: public ExprVisitor {
        TypeInterner& m_types;

    public:
        explicit ExprVisitorDef(TypeInterner& types);
        TypeInterner& type_interner() const { return m_types; }

#define NV(nt) virtual void visit(nt& n) override;

        virtual void visit_node_ptr(::HIR::ExprNodeP& node_ptr) override;

        NV(ExprNodeBlock)
        NV(ExprNodeConstBlock)
        //NV(ExprNodeAsyncBlock)
        NV(ExprNodeAsm)
        NV(ExprNodeAsm2)
        NV(ExprNodeReturn)
        NV(ExprNodeYield)
        NV(ExprNodeAWait)
        NV(ExprNodeLet)
        NV(ExprNodeLoop)
        NV(ExprNodeLoopControl)
        NV(ExprNodeMatch)

        NV(ExprNodeAssign)
        NV(ExprNodeBinOp)
        NV(ExprNodeUniOp)
        NV(ExprNodeBorrow)
        NV(ExprNodeRawBorrow)
        NV(ExprNodeCast)
        NV(ExprNodeUnsize)
        NV(ExprNodeIndex)
        NV(ExprNodeDeref)
        NV(ExprNodeEmplace)

        NV(ExprNodeTupleVariant);
        NV(ExprNodeCallPath);
        NV(ExprNodeCallValue);
        NV(ExprNodeCallMethod);
        NV(ExprNodeField);

        NV(ExprNodeLiteral);
        NV(ExprNodeUnitVariant);
        NV(ExprNodePathValue);
        NV(ExprNodeVariable);
        NV(ExprNodeConstParam);

        NV(ExprNodeStructLiteral);
        NV(ExprNodeTuple);
        NV(ExprNodeArrayList);
        NV(ExprNodeArraySized);

        NV(ExprNodeClosure);
        NV(ExprNodeGenerator);
        NV(ExprNodeGeneratorWrapper);
        NV(ExprNodeAsyncBlock);
#undef NV

        virtual void visit_pattern(const Span& sp, ::HIR::Pattern& pat);
        virtual void visit_type(::HIR::TypeRef& ty);
        virtual void visit_trait_path(::HIR::TraitPath& p);
        virtual void visit_path_params(::HIR::PathParams& ty);
        virtual void visit_path(::HIR::Visitor::PathContext pc, ::HIR::Path& ty);
        virtual void visit_generic_path(::HIR::Visitor::PathContext pc, ::HIR::GenericPath& ty);
    };

}

void HIRDumpExpr(::std::ostream& sink, const ::HIR::ExprPtr& expr);
