#pragma once

#include "hir_pattern.h"
#include "hir_type.h"
#include "span.h"
#include "hir_visitor.h"
#include "hir_typeck_common.h"
#include "hir_asm.h"
#include <memory>

namespace HIR {

    typedef ::std::vector<::std::pair<const ::HIR::SimplePath*, const ::HIR::Trait*>> tTraitList;

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
        Span mSpan;
        ::HIR::TypeRef resType; // TODO: Replace this with an index into an ivar table
        //unsigned m_res_type_idx;
        ValueUsage usage = ValueUsage::Unknown;

        const Span& span() const {
            return mSpan;
        }

        virtual void visit(ExprVisitor& v) = 0;
        virtual unsigned int nodeKind() const = 0;

        ExprNode(Span sp);

        virtual ~ExprNode();

        const char* type_name() const;
    };

    struct ExprNodeBlock: public ExprNode {
        bool isUnsafe;
        ::std::vector<ExprNodeP> nodes;
        ExprNodeP valueNode; // can be null

        ::HIR::SimplePath localMod;
        tTraitList traits;

        ExprNodeBlock(Span sp);

        ExprNodeBlock(Span sp, bool is_unsafe, ::std::vector<ExprNodeP> nodes, ExprNodeP value_node);

        static constexpr unsigned int kind = 1;
        unsigned int nodeKind() const override;
        void visit(ExprVisitor& nv) override;
    };

    struct ExprNodeConstBlock: public ExprNode {
        ExprNodeP inner;

        ExprNodeConstBlock(Span sp, ExprNodeP inner);

        static constexpr unsigned int kind = 2;
        unsigned int nodeKind() const override;
        void visit(ExprVisitor& nv) override;
    };

    struct ExprNodeAsm: public ExprNode {
        struct ValRef {
            ::std::string spec;
            ::HIR::ExprNodeP value;
        };

        ::std::string templateText;
        ::std::vector<ValRef> outputs;
        ::std::vector<ValRef> inputs;
        ::std::vector<::std::string> clobbers;
        ::std::vector<::std::string> flags;

        ExprNodeAsm(Span sp, ::std::string tplStr, ::std::vector<ValRef> outputs, ::std::vector<ValRef> inputs, ::std::vector<::std::string> clobbers, ::std::vector<::std::string> flags)
            : ExprNode(mv$(sp))
            , templateText(mv$(tplStr))
            , outputs(mv$(outputs))
            , inputs(mv$(inputs))
            , clobbers(mv$(clobbers))
            , flags(mv$(flags))
        {
        }

        static constexpr unsigned int kind = 3;
        unsigned int nodeKind() const override;
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

        AsmCommon::Options options;
        std::vector<AsmCommon::Line> lines;
        std::vector<Param> mParams;

        ExprNodeAsm2(Span sp, AsmCommon::Options options, std::vector<AsmCommon::Line> lines, std::vector<Param> params);

        static constexpr unsigned int kind = 4;
        unsigned int nodeKind() const override;
        void visit(ExprVisitor& nv) override;
    };

    struct ExprNodeReturn: public ExprNode {
        ::HIR::ExprNodeP mValue;

        ExprNodeReturn(Span sp, ::HIR::ExprNodeP value);

        static constexpr unsigned int kind = 5;
        unsigned int nodeKind() const override;
        void visit(ExprVisitor& nv) override;
    };

    /// @brief `foo = yield bar` generator yield statement
    struct ExprNodeYield: public ExprNode {
        ::HIR::ExprNodeP mValue;

        ExprNodeYield(Span sp, ::HIR::ExprNodeP value);

        static constexpr unsigned int kind = 6;
        unsigned int nodeKind() const override;
        void visit(ExprVisitor& nv) override;
    };

    /// @brief Async Wait (the `.await` postfix operator)
    struct ExprNodeAWait: public ExprNode {
        ::HIR::ExprNodeP mValue;

        ExprNodeAWait(Span sp, ::HIR::ExprNodeP value);

        static constexpr unsigned int kind = 7;
        unsigned int nodeKind() const override;
        void visit(ExprVisitor& nv) override;
    };

    struct ExprNodeLoop: public ExprNode {
        RcString label;
        ::HIR::ExprNodeP mCode;
        bool diverges = false;
        bool requireLabel = false;

        ExprNodeLoop(Span sp, RcString label, ::HIR::ExprNodeP code, bool require_label = false);

        static constexpr unsigned int kind = 8;
        unsigned int nodeKind() const override;
        void visit(ExprVisitor& nv) override;
    };

    struct ExprNodeLoopControl: public ExprNode {
        RcString label;
        bool isContinue;
        ::HIR::ExprNodeP mValue;

        const ExprNodeLoop* targetNode; // populated by expr_cs__enum.cpp

        ExprNodeLoopControl(Span sp, RcString label, bool cont, ::HIR::ExprNodeP value = {});

        static constexpr unsigned int kind = 9;
        unsigned int nodeKind() const override;
        void visit(ExprVisitor& nv) override;
    };

    struct ExprNodeLet: public ExprNode {
        ::HIR::Pattern pattern;
        ::HIR::TypeRef mType;
        ::HIR::ExprNodeP mValue;
        bool isSuper;

        ExprNodeLet(Span sp, ::HIR::Pattern pat, ::HIR::TypeRef ty, ::HIR::ExprNodeP val, bool is_super = false);

        static constexpr unsigned int kind = 10;
        unsigned int nodeKind() const override;
        void visit(ExprVisitor& nv) override;
    };

    struct ExprNodeMatch: public ExprNode {
        struct Guard {
            /// Guard pattern, always set (but might be `true`/`false`)
            ::HIR::Pattern pat;
            /// Guard value
            ::HIR::ExprNodeP val;
            /// Indicates that this guard is an `if` (changes scoping rules, and tweaks how typecheck happens)
            bool isIf;
        };

        struct Arm {
            // Patterns, must be non-empty
            ::std::vector<::HIR::Pattern> patterns;
            // A chained (&&) list of guards
            ::std::vector<Guard> guards;
            // Match arm body, required
            ::HIR::ExprNodeP mCode;
        };

        ::HIR::ExprNodeP mValue;
        ::std::vector<Arm> arms;
        bool isLetElse;

        ExprNodeMatch(Span sp, ::HIR::ExprNodeP val, ::std::vector<Arm> arms, bool is_let_else = false);

        static constexpr unsigned int kind = 11;
        unsigned int nodeKind() const override;
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

        Op op;
        ExprNodeP slot;
        ExprNodeP mValue;

        ExprNodeAssign(Span sp, Op op, ::HIR::ExprNodeP slot, ::HIR::ExprNodeP value);

        static constexpr unsigned int kind = 12;
        unsigned int nodeKind() const override;
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

        Op op;
        ::HIR::ExprNodeP left;
        ::HIR::ExprNodeP right;

        ExprNodeBinOp(Span sp, Op op, ::HIR::ExprNodeP left, ::HIR::ExprNodeP right);

        static constexpr unsigned int kind = 13;
        unsigned int nodeKind() const override;
        void visit(ExprVisitor& nv) override;
    };

    struct ExprNodeUniOp: public ExprNode {
        enum class Op {
            Invert, // '!<expr>'
            Negate, // '-<expr>'
        };

        static const char* opname(Op v);

        Op op;
        ::HIR::ExprNodeP mValue;

        ExprNodeUniOp(Span sp, Op op, ::HIR::ExprNodeP value);

        static constexpr unsigned int kind = 14;
        unsigned int nodeKind() const override;
        void visit(ExprVisitor& nv) override;
    };

    struct ExprNodeBorrow: public ExprNode {
        ::HIR::BorrowType mType;
        ::HIR::ExprNodeP mValue;

        /// <summary>
        /// Flag set by the first pass of SBC to both inform the second pass and change Lifetime Infer's behaviour
        /// </summary>
        bool isValidStaticBorrowConstant;

        ExprNodeBorrow(Span sp, ::HIR::BorrowType bt, ::HIR::ExprNodeP value);

        static constexpr unsigned int kind = 15;
        unsigned int nodeKind() const override;
        void visit(ExprVisitor& nv) override;
    };

    struct ExprNodeRawBorrow: public ExprNode {
        ::HIR::BorrowType mType;
        ::HIR::ExprNodeP mValue;

        ExprNodeRawBorrow(Span sp, ::HIR::BorrowType bt, ::HIR::ExprNodeP value);

        static constexpr unsigned int kind = 16;
        unsigned int nodeKind() const override;
        void visit(ExprVisitor& nv) override;
    };

    struct ExprNodeCast: public ExprNode {
        ::HIR::ExprNodeP mValue;
        ::HIR::TypeRef dstType;

        ExprNodeCast(Span sp, ::HIR::ExprNodeP value, ::HIR::TypeRef dst_type);

        static constexpr unsigned int kind = 17;
        unsigned int nodeKind() const override;
        void visit(ExprVisitor& nv) override;
    };

    // Magical pointer unsizing operation:
    // - `&[T; n] -> &[T]`
    // - `&T -> &Trait`
    // - `Box<T> -> Box<Trait>`
    // NOTE: Also used for type ascription
    struct ExprNodeUnsize: public ExprNode {
        ::HIR::ExprNodeP mValue;
        ::HIR::TypeRef dstType;
        bool isArrayToSliceAdjustment = false;

        ExprNodeUnsize(Span sp, ::HIR::ExprNodeP value, ::HIR::TypeRef dst_type);

        static constexpr unsigned int kind = 18;
        unsigned int nodeKind() const override;
        void visit(ExprVisitor& nv) override;
    };

    struct ExprNodeIndex: public ExprNode {
        ::HIR::ExprNodeP mValue;
        ::HIR::ExprNodeP index;

        struct {
            ::HIR::TypeRef indexTy;
        } cache;

        ExprNodeIndex(Span sp, ::HIR::ExprNodeP val, ::HIR::ExprNodeP index);

        static constexpr unsigned int kind = 19;
        unsigned int nodeKind() const override;
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

        ::HIR::ExprNodeP mValue;
        TraitUsed traitUsed;

        ExprNodeDeref(Span sp, ::HIR::ExprNodeP val);

        static constexpr unsigned int kind = 20;
        unsigned int nodeKind() const override;
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

        Type mType;
        ExprNodeP place;
        ExprNodeP mValue;

        ExprNodeEmplace(Span sp, Type ty, ::HIR::ExprNodeP place, ::HIR::ExprNodeP val);

        static constexpr unsigned int kind = 21;
        unsigned int nodeKind() const override;
        void visit(ExprVisitor& nv) override;
    };

    struct ExprNodeTupleVariant: public ExprNode {
        // Path to variant/struct
        ::HIR::GenericPath mPath;
        bool isStruct;
        ::std::vector<ExprNodeP> mArgs;

        // - Cache for typeck
        ::std::vector<::HIR::TypeRef> argTypes;

        ExprNodeTupleVariant(Span sp, ::HIR::GenericPath path, bool is_struct, ::std::vector<::HIR::ExprNodeP> args);

        static constexpr unsigned int kind = 22;
        unsigned int nodeKind() const override;
        void visit(ExprVisitor& nv) override;
    };

    struct ExprCallCache {
        ::std::vector<::HIR::TypeRef> argTypes;
        const ::HIR::GenericParams* fcnParams;
        const ::HIR::GenericParams* topParams;
        const ::HIR::Function* fcn;

        ::std::unique_ptr<Monomorphiser> monomorph;
    };

    struct ExprNodeCallPath: public ExprNode {
        ::HIR::Path mPath;
        ::std::vector<ExprNodeP> mArgs;

        // - Cache for typeck
        ExprCallCache cache;

        ExprNodeCallPath(Span sp, ::HIR::Path path, ::std::vector<::HIR::ExprNodeP> args);

        static constexpr unsigned int kind = 23;
        unsigned int nodeKind() const override;
        void visit(ExprVisitor& nv) override;
    };

    struct ExprNodeCallValue: public ExprNode {
        ::HIR::ExprNodeP mValue;
        ::std::vector<ExprNodeP> mArgs;

        // - Argument types used as coercion targets
        ::std::vector<::HIR::TypeRef> argIvars;

        // - Cache for typeck
        ::std::vector<::HIR::TypeRef> argTypes;

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

        ExprNodeCallValue(Span sp, ::HIR::ExprNodeP val, ::std::vector<::HIR::ExprNodeP> args);

        static constexpr unsigned int kind = 24;
        unsigned int nodeKind() const override;
        void visit(ExprVisitor& nv) override;
    };

    // TODO: Refactor to support efficient method chaining
    struct ExprNodeCallMethod: public ExprNode {
        /// @brief Method reciever value
        ::HIR::ExprNodeP mValue;
        /// @brief Method name
        RcString method;
        /// @brief Generic parameters to the method
        ::HIR::PathParams mParams;
        /// @brief Argument values
        ::std::vector<::HIR::ExprNodeP> mArgs;

        // - Set during typeck to the real path to the method
        ::HIR::Path methodPath;
        // - Cache of argument/return types
        ExprCallCache cache;

        // - List of possible traits (in-scope traits that contain this method)
        tTraitList traits;
        // - A pool of ivars to use for searching for trait impls, with type
        // ivars first and const value ivars after them.
        ::std::vector<unsigned int> traitParamIvars;
        unsigned int traitParamTypeIvars = 0;

        ExprNodeCallMethod(Span sp, ::HIR::ExprNodeP val, RcString method_name, ::HIR::PathParams params, ::std::vector<::HIR::ExprNodeP> args);

        static constexpr unsigned int kind = 25;
        unsigned int nodeKind() const override;
        void visit(ExprVisitor& nv) override;
    };

    struct ExprNodeField: public ExprNode {
        ::HIR::ExprNodeP mValue;
        RcString field;

        ExprNodeField(Span sp, ::HIR::ExprNodeP val, RcString field);

        static constexpr unsigned int kind = 26;
        unsigned int nodeKind() const override;
        void visit(ExprVisitor& nv) override;
    };

    struct ExprNodeLiteral: public ExprNode {
        TAGGED_UNION(
            Data,
            Integer,
            (Integer,
             struct {
                 ::HIR::CoreType mType; // if not an integer type, it's unknown
                 U128 mValue;
             }),
            (Float,
             struct {
                 ::HIR::CoreType mType; // If not a float type, it's unknown
                 FloatValue mValue;
             }),
            (Boolean, bool),
            (String, ::std::string),
            (CString, struct { ::std::string v; }),
            (ByteString, ::std::vector<char>)
        );

        Data mData;

        ExprNodeLiteral(Span sp, Data data);

        static constexpr unsigned int kind = 27;
        unsigned int nodeKind() const override;
        void visit(ExprVisitor& nv) override;
    };

    struct ExprNodeUnitVariant: public ExprNode {
        // Path to variant/struct
        ::HIR::GenericPath mPath;
        bool isStruct;

        ExprNodeUnitVariant(Span sp, ::HIR::GenericPath path, bool is_struct);

        static constexpr unsigned int kind = 28;
        unsigned int nodeKind() const override;
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

        ::HIR::Path mPath;
        Target target;

        ExprNodePathValue(Span sp, ::HIR::Path path, Target target);

        static constexpr unsigned int kind = 29;
        unsigned int nodeKind() const override;
        void visit(ExprVisitor& nv) override;
    };

    struct ExprNodeVariable: public ExprNode {
        RcString mName;
        unsigned int slot;

        ExprNodeVariable(Span sp, RcString name, unsigned int slot);

        static constexpr unsigned int kind = 30;
        unsigned int nodeKind() const override;
        void visit(ExprVisitor& nv) override;
    };

    struct ExprNodeConstParam: public ExprNode {
        RcString mName;
        unsigned int mBinding;

        ExprNodeConstParam(Span sp, RcString name, unsigned int binding);

        static constexpr unsigned int kind = 31;
        unsigned int nodeKind() const override;
        void visit(ExprVisitor& nv) override;
    };

    struct ExprNodeStructLiteral: public ExprNode {
        typedef ::std::vector<::std::pair<RcString, ExprNodeP>> tValues;

        ::HIR::TypeRef mType;
        bool isStruct;
        /// Alternative to `m_base_value`, indicates that the struct's field defaults are to be used
        bool useDefaults;
        /// Base value (`..foo`)
        ::HIR::ExprNodeP baseValue;
        tValues values;

        /// Actual path extracted from the TypeRef (populated after inner UFCS expansion)
        ::HIR::GenericPath realPath;
        /// Monomorphised types of each field.
        ::std::vector<::HIR::TypeRef> valueTypes;

        ExprNodeStructLiteral(Span sp, ::HIR::TypeRef ty, bool is_struct, ::HIR::ExprNodeP base_value, tValues values);

        ExprNodeStructLiteral(Span sp, ::HIR::TypeRef ty, bool is_struct, bool, tValues values);

        static constexpr unsigned int kind = 32;
        unsigned int nodeKind() const override;
        void visit(ExprVisitor& nv) override;
    };

    struct ExprNodeTuple: public ExprNode {
        ::std::vector<::HIR::ExprNodeP> vals;

        ExprNodeTuple(Span sp, ::std::vector<::HIR::ExprNodeP> vals);

        static constexpr unsigned int kind = 33;
        unsigned int nodeKind() const override;
        void visit(ExprVisitor& nv) override;
    };

    struct ExprNodeArrayList: public ExprNode {
        ::std::vector<::HIR::ExprNodeP> vals;

        ExprNodeArrayList(Span sp, ::std::vector<::HIR::ExprNodeP> vals);

        static constexpr unsigned int kind = 34;
        unsigned int nodeKind() const override;
        void visit(ExprVisitor& nv) override;
    };

    // TODO: Might want a second variant for dynamically-sized arrays
    struct ExprNodeArraySized: public ExprNode {
        ::HIR::ExprNodeP val;
        ::HIR::ArraySize mSize;

        ExprNodeArraySized(Span sp, ::HIR::ExprNodeP val, ::HIR::ExprPtr size);

        static constexpr unsigned int kind = 35;
        unsigned int nodeKind() const override;
        void visit(ExprVisitor& nv) override;
    };

    struct ExprNodeClosure: public ExprNode {
        typedef ::std::vector<::std::pair<::HIR::Pattern, ::HIR::TypeRef>> argsT;

        argsT mArgs;
        ::HIR::TypeRef returnType;
        ::HIR::ExprNodeP mCode;
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
            ::std::vector<unsigned int> local_vars;

            struct Capture {
                // Variable binding index
                unsigned int rootSlot;
                // Fields used to access that variable
                std::vector<RcString> fields;
                ::HIR::ValueUsage usage;
            };

            ::std::vector<Capture> capturedVars;
        } avuCache;

        // Lifetime for captured borrows, filled by lifetime infer pass
        ::HIR::LifetimeRef captureLifetime;
        // - Path to the generated closure type
        const ::HIR::Struct* objPtr = nullptr;
        ::HIR::GenericPath objPathBase;
        ::HIR::GenericPath objPath;
        ::std::vector<::HIR::ExprNodeP> captures;

        ExprNodeClosure(Span sp, argsT args, ::HIR::TypeRef rv, ::HIR::ExprNodeP code, bool is_move);

        static constexpr unsigned int kind = 36;
        unsigned int nodeKind() const override;
        void visit(ExprVisitor& nv) override;
    };

    ::std::ostream& operator<<(::std::ostream& os, const ExprNodeClosure::AvuCache::Capture& x);

    struct ExprNodeGenerator: public ExprNode {
        //ExprNodeClosure::args_t    m_args;
        ::HIR::TypeRef returnType;
        ::HIR::TypeRef resumeTy;
        ::HIR::TypeRef yieldTy;
        ::HIR::ExprNodeP mCode;
        bool isMove;
        bool isPinned;

        // AnnotateValueUsage cache/information
        struct AvuCache {
            ::std::vector<unsigned int> local_vars;
            ::std::vector<::std::pair<unsigned int, ::HIR::ValueUsage>> capturedVars;
        } avuCache;

        // Generated type information
        const ::HIR::Struct* objPtr = nullptr;
        ::HIR::GenericPath objPath;
        // Lifetime for captured borrows, filled by lifetime infer pass
        ::HIR::LifetimeRef captureLifetime;
        // Captured variables (used for emitting the constructor)
        ::std::vector<::HIR::ExprNodeP> captures;
        // State data type (needed for initialising)
        ::HIR::TypeRef stateDataType;

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
        unsigned int nodeKind() const override;
        void visit(ExprVisitor& nv) override;
    };

    /// <summary>
    /// Top-level wrapper for the generator method
    /// </summary>
    struct ExprNodeGeneratorWrapper: public ExprNode {
        //ExprNodeClosure::args_t    m_args;
        bool isFuture;
        ::HIR::TypeRef returnType;
        ::HIR::TypeRef yieldTy;
        ::HIR::ExprNodeP mCode;

        // Generated type information
        const ::HIR::Struct* objPtr = nullptr;
        ::HIR::GenericPath objPath;

        ::HIR::TypeRef stateDataType;
        ::HIR::SimplePath stateIdxEnum;

        ::HIR::Function* dropFcnPtr = nullptr;

        ::std::vector<HIR::ValueUsage> captureUsages;

        ExprNodeGeneratorWrapper(
            Span sp,
            ::HIR::TypeRef rv,
            ::HIR::TypeRef yield_ty,
            ::HIR::ExprNodeP code,
            bool is_future
        );

        static constexpr unsigned int kind = 38;
        unsigned int nodeKind() const override;
        void visit(ExprVisitor& nv) override;
    };

    struct ExprNodeAsyncBlock: public ExprNode {
        ::HIR::ExprNodeP mCode;
        bool isMove;

        ExprNodeGenerator::AvuCache avuCache;

        // Generated type information
        const ::HIR::Struct* objPtr = nullptr;
        ::HIR::GenericPath objPath;
        // Lifetime for captured borrows, filled by lifetime infer pass
        ::HIR::LifetimeRef captureLifetime;
        // Captured variables (used for emitting the constructor)
        ::std::vector<::HIR::ExprNodeP> captures;
        // State data type (needed for initialising)
        ::HIR::TypeRef stateDataType;

        ExprNodeAsyncBlock(Span sp, ::HIR::ExprNodeP code, bool is_move);

        static constexpr unsigned int kind = 39;
        unsigned int nodeKind() const override;
        void visit(ExprVisitor& nv) override;
    };

    class ExprVisitor {
    public:
        virtual ~ExprVisitor() = default;
        virtual void visit_node_ptr(::HIR::ExprNodeP& nodePtr);
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
        TypeInterner& types;

    public:
        explicit ExprVisitorDef(TypeInterner& types);
        TypeInterner& type_interner() const { return types; }

#define NV(nt) virtual void visit(nt& n) override;

        virtual void visit_node_ptr(::HIR::ExprNodeP& nodePtr) override;

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
