#include "hir_typeck_expr_cs.h"

#include "output.h"
#include "hir_hir.h"
#include "hir_expr.h"
#include "settings.h"
#include "wire_board.h"
#include "hir_visitor.h"
#include "hir_expr_state.h"
#include "hir_typeck_static.h"
#include "hir_typeck_helpers.h"
#include "hir_typeck_monomorph.h"
#include "hir_typeck_expr_visit.h"
#include "hir_conv_main_bindings.h"
#include "hir_typeck_main_bindings.h"
#include "hir_conv_constant_evaluation.h"

#include <std/rng/mix.h>
#include <std/alg/defer.h>
#include <std/alg/range.h>
#include <std/lib/vector.h>
#include <std/mem/obj_pool.h>

#include <optional>
#include <algorithm>

using namespace stl;

#define NEWNODE(TY, SP, CLASS, ...) mkExprnodep(context.crate.pool->make<HIRExprNode##CLASS>(SP, ##__VA_ARGS__), TY)

namespace {
    struct IvarCoercionIndex;

    struct MonomorphEraseHrls: public Monomorphiser {
        explicit MonomorphEraseHrls(HIRTypeInterner& types);

        const HIRType* getType(const Span& sp, const HIRGenericRef& g) const override;

        HIRConstGeneric getValue(const Span& sp, const HIRGenericRef& g) const override;
    };

    struct ExprVisitorRevisit: public HIRExprVisitor {
        Context& context;
        bool completed;
        bool isFallback;
        const Vector<const HIRType*>* passStartIvars;
        const IvarCoercionIndex* coercionIndex;

        bool nodeDiverges(const HIRExprNode& node) const;

        ExprVisitorRevisit(Context& context, bool fallback = false, const Vector<const HIRType*>* passStartIvars = nullptr, const IvarCoercionIndex* coercionIndex = nullptr);

        bool nodeCompleted() const;

        void visit(HIRExprNodeBlock& node) override;

        void visit(HIRExprNodeConstBlock& node) override;

        void visit(HIRExprNodeAsm& node) override;

        void visit(HIRExprNodeAsm2& node) override;

        void visit(HIRExprNodeReturn& node) override;

        void visit(HIRExprNodeYield& node) override;

        void visit(HIRExprNodeAWait& node) override;

        void visit(HIRExprNodeUse& node) override;

        void visit(HIRExprNodeLet& node) override;

        void visit(HIRExprNodeLoop& node) override;

        void visit(HIRExprNodeLoopControl& node) override;

        void visit(HIRExprNodeMatch& node) override;

        void visit(HIRExprNodeAssign& node) override;

        void visit(HIRExprNodeBinOp& node) override;

        void visit(HIRExprNodeUniOp& node) override;

        void visit(HIRExprNodeBorrow& node) override;

        void visit(HIRExprNodeRawBorrow& node) override;

        void bad_cast(const Span& sp, const HIRType* srcTy, const HIRType* tgtTy, const char* where);

        void equateFunctionSignature(const Span& sp, const HIRTypeDataFunctionPointer& dst, const HIRTypeDataFunctionPointer& src);

        void visit(HIRExprNodeCast& node) override;

        void visit(HIRExprNodeUnsize& node) override;

        void visit(HIRExprNodeIndex& node) override;

        void visit(HIRExprNodeDeref& node) override;

        void visitEmplace129(HIRExprNodeEmplace& node);

        void visit(HIRExprNodeEmplace& node) override;

        void visit(HIRExprNodeTupleVariant& node) override;

        void visit(HIRExprNodeCallPath& node) override;

        enum class AsyncCallResult {
            NoSolution,
            Ambiguous,
            Proven,
        };

        AsyncCallResult callAsyncCallable(HIRExprNodeCallValue& node, const HIRType* ty, const HIRPathParams& traitPp);

        void visit(HIRExprNodeCallValue& node) override;

        void visit(HIRExprNodeCallMethod& node) override;

        void visit(HIRExprNodeField& node) override;

        void visit(HIRExprNodeLiteral& node) override;

        void visit(HIRExprNodeUnitVariant& node) override;

        void visit(HIRExprNodePathValue& node) override;

        void visit(HIRExprNodeVariable& node) override;

        void visit(HIRExprNodeConstParam& node) override;

        void visit(HIRExprNodeStructLiteral& node) override;

        void visit(HIRExprNodeTuple& node) override;

        void visit(HIRExprNodeArrayList& node) override;

        void visit(HIRExprNodeArraySized& node) override;

        void visit(HIRExprNodeClosure& node) override;

        void visit(HIRExprNodeGenerator& node) override;

        void visit(HIRExprNodeGeneratorWrapper& node) override;

        void visit(HIRExprNodeAsyncBlock& node) override;

        void noRevisit(HIRExprNode& node);
    };

    struct ExprVisitorApply: public HIRExprVisitorDef {
        Context& context;
        const HMTypeInferrence& ivars;
        HIRPathParams nopImpl;
        HIRPathParams nopItem;

        ExprVisitorApply(Context& context);

        void visitNodePtr(HIRExprPtr& nodePtr);

        void visitNodePtr(HIRExprNodeP& nodePtr) override;

        void visitPattern(const Span& sp, HIRPattern& pat) override;

        void visit(HIRExprNodeBlock& node) override;

        void visit(HIRExprNodeLet& node) override;

        void visit(HIRExprNodeClosure& node) override;

        void visit(HIRExprNodeGenerator& node) override;

        void visit(HIRExprNodeAsyncBlock& node) override;

        void visit(HIRExprNodeGeneratorWrapper& node) override;

        void visitCallcache(const Span& sp, HIRExprCallCache& cache);

        void visit(HIRExprNodeCallPath& node) override;

        void visit(HIRExprNodeCallMethod& node) override;

        void visit(HIRExprNodeCallValue& node) override;

        void visit(HIRExprNodePathValue& node) override;

        void visit(HIRExprNodeUnitVariant& node) override;

        void visit(HIRExprNodeStructLiteral& node) override;

        void visit(HIRExprNodeTupleVariant& node) override;

        void visit(HIRExprNodeLiteral& node) override;

        void visit(HIRExprNodeCast& node) override;

        void visit(HIRExprNodeUnsize& node) override;

        const HIRType* checkTypeResolvedTop(const Span& sp, const HIRType* ty) const;

        void checkTypeResolvedConstgeneric(const Span& sp, HIRConstGeneric& v, const HIRType* topType) const;

        void checkTypeResolvedPp(const Span& sp, HIRPathParams& pp, const HIRType* topType) const;

        void checkTypeResolvedPath(const Span& sp, HIRPath& path) const;

        void checkTypeResolvedPath(const Span& sp, HIRPath& path, const HIRType* topType) const;

        void checkTypeResolvedGenericpath(const Span& sp, HIRGenericPath& path) const;

        const HIRType* checkTypeResolved(const Span& sp, const HIRType* ty, const HIRType* topType) const;

        void checkTypesEqual(const Span& sp, const HIRType* l, const HIRType* r) const;
        void visit(HIRExprNodeArraySized& node) override;
    };

    struct ExprVisitorPrint: public HIRExprVisitor {
        const Context& context;
        ZeroCopyOutput& os;

        ExprVisitorPrint(const Context& context, ZeroCopyOutput& os);

        void visit(HIRExprNodeBlock& node) override;

        void visit(HIRExprNodeConstBlock& node) override;

        void visit(HIRExprNodeAsm& node) override;

        void visit(HIRExprNodeAsm2& node) override;

        void visit(HIRExprNodeReturn& node) override;

        void visit(HIRExprNodeYield& node) override;

        void visit(HIRExprNodeAWait& node) override;

        void visit(HIRExprNodeUse& node) override;

        void visit(HIRExprNodeLet& node) override;

        void visit(HIRExprNodeLoop& node) override;

        void visit(HIRExprNodeLoopControl& node) override;

        void visit(HIRExprNodeMatch& node) override;

        void visit(HIRExprNodeAssign& node) override;

        void visit(HIRExprNodeBinOp& node) override;

        void visit(HIRExprNodeUniOp& node) override;

        void visit(HIRExprNodeBorrow& node) override;

        void visit(HIRExprNodeRawBorrow& node) override;

        void visit(HIRExprNodeCast& node) override;

        void visit(HIRExprNodeUnsize& node) override;

        void visit(HIRExprNodeIndex& node) override;

        void visit(HIRExprNodeDeref& node) override;

        void visit(HIRExprNodeEmplace& node) override;

        void visit(HIRExprNodeTupleVariant& node) override;

        void visit(HIRExprNodeCallPath& node) override;

        void visit(HIRExprNodeCallValue& node) override;

        void visit(HIRExprNodeCallMethod& node) override;

        void visit(HIRExprNodeField& node) override;

        void visit(HIRExprNodeLiteral& node) override;

        void visit(HIRExprNodeUnitVariant& node) override;

        void visit(HIRExprNodePathValue& node) override;

        void visit(HIRExprNodeVariable& node) override;

        void visit(HIRExprNodeConstParam& node) override;

        void visit(HIRExprNodeStructLiteral& node) override;

        void visit(HIRExprNodeTuple& node) override;

        void visit(HIRExprNodeArrayList& node) override;

        void visit(HIRExprNodeArraySized& node) override;

        void visit(HIRExprNodeClosure& node) override;

        void visit(HIRExprNodeGenerator& node) override;

        void visit(HIRExprNodeGeneratorWrapper& node) override;

        void visit(HIRExprNodeAsyncBlock& node) override;

        HMTypeInferrence::FmtType fmtResTy(const HIRExprNode& n);

        void noRevisit(HIRExprNode& n);
    };

    struct ConstExprEquate {
        Context& context;
        const Span& sp;

        const HIRConstGeneric* getParam(const HIRConstGenericUnevaluated& value, unsigned int binding) const;

        const HIRConstGeneric* identity(const HIRConstGeneric& value) const;

        bool equateIdentity(const HIRConstGeneric& value, const HIRConstGeneric& other) const;

        bool equateLiteral(const HIRExprNodeLiteral& left, const HIRExprNodeLiteral& right) const;

        const EncodedLiteral* evaluatedPath(const HIRConstGenericUnevaluated& value, const HIRExprNodePathValue& node) const;

        bool equateLiteralEvaluated(const HIRExprNodeLiteral& literal, const EncodedLiteral& evaluated) const;

        bool equateLiteralPath(const HIRExprNodeLiteral& literal, const HIRConstGenericUnevaluated& pathValue, const HIRExprNodePathValue& path) const;

        bool equateEvaluated(const HIRConstGenericUnevaluated& value, const EncodedLiteral& evaluated) const;

        bool equatePath(const HIRConstGenericUnevaluated& leftValue, const HIRPath& left, const HIRConstGenericUnevaluated& rightValue, const HIRPath& right) const;

        bool equateNode(const HIRConstGenericUnevaluated& leftValue, const HIRExprNode& left, const HIRConstGenericUnevaluated& rightValue, const HIRExprNode& right) const;

        bool equate(const HIRConstGenericUnevaluated& left, const HIRConstGenericUnevaluated& right) const;
    };

    struct AssociatedStallCollector {
        Context& context;
        Vector<Context::Associated::StallDependency>& dependencies;
        Vector<const HIRType*> pending;
        Vector<const HIRType*> visited;
        bool hasRawInfer = false;

        void addType(const HIRType* type);

        void collect();
    };

    struct IvarCoercionEndpoint {
        const HIRType* other;
        SolverCoercionConstraint::Direction direction;
        SolverCoercionOp op;
        const Context::Coercion* obligation;
        unsigned alternativeGroup;
    };

    struct IvarCoercionRefs {
        Vector<const Context::Coercion*> coercions;
        Vector<IvarCoercionEndpoint> endpoints;
        Vector<const Context::Associated*> associated;
        Vector<const HIRExprNode*> revisits;
        Vector<const Context::Revisitor*> advancedRevisits;
    };

    struct IvarCoercionIndex {
        const Context& context;
        std::vector<IvarCoercionRefs> refs;

        void collectIvars(const HIRType* root, Vector<unsigned int>& out) const;
        static void deduplicate(Vector<unsigned int>& values);

        template <typename T>
        void addRefs(const Vector<unsigned int>& dependencies, Vector<T> IvarCoercionRefs::* member, T value);

        void addEndpoint(const Context::Coercion& obligation, const SolverDeferredCoercion& deferred, unsigned alternativeGroup);

        explicit IvarCoercionIndex(const Context& context);

        const IvarCoercionRefs& operator[](unsigned int index) const;
    };

    struct ActiveOperatorOutput {
        unsigned int index;
        const ActiveOperatorOutput* parent;
    };

    struct PossibleType {
        enum {
            CoerceTo,
            CoerceFrom,
            UnsizeTo,
            UnsizeFrom,
        } cls;

        enum class State {
            Concrete,
            Removed,
        } state;

        const HIRType* ty;

        static PossibleType concrete(decltype(cls) cls, const HIRType* ty);

        bool isActive() const;

        bool hasType() const;

        void remove();

        bool operator==(const PossibleType& o) const;

        ZeroCopyOutput& fmt(ZeroCopyOutput& os) const;

        bool isSource() const;

        bool isCoerce() const;
    };

    enum class PointerCoercionForm {
        MutableBorrow,
        SharedBorrow,
        MutableRaw,
        ConstRaw,
    };

    struct PointerCoercionShape {
        PointerCoercionForm form;
        const HIRType* inner;
    };

    std::optional<PointerCoercionShape> pointerCoercionShape(const HIRType* type);

    std::optional<PointerCoercionForm> pointerChainLub(PointerCoercionForm left, PointerCoercionForm right);

    struct RpitOriginMonomorph: public HIRMatchGenerics, public Monomorphiser {
        std::map<u32, const HIRType*> typeBindings;
        std::map<u32, HIRConstGeneric> valueBindings;

        explicit RpitOriginMonomorph(HIRTypeInterner& types);

        HIRCompare matchTy(const HIRGenericRef& generic, const HIRType* type, tCbResolveType resolve) override;

        HIRCompare matchVal(const HIRGenericRef& generic, const HIRConstGeneric& value) override;

        const HIRType* getType(const Span&, const HIRGenericRef& generic) const override;

        HIRConstGeneric getValue(const Span&, const HIRGenericRef& generic) const override;
    };

    struct ExprVisitorTagStaleIvars: public HIRExprVisitorDef {
        struct Mapper final: public MonomorphiserNop {
            mutable Vector<std::pair<unsigned, unsigned>> valueIndexes_;

            unsigned taggedIndex(Vector<std::pair<unsigned, unsigned>>& indexes, unsigned original) const;

            using MonomorphiserNop::MonomorphiserNop;

            HIRConstGeneric monomorphConstgeneric(const Span& sp, const HIRConstGeneric& value, bool allowInfer) const override;
        } mapper_;

        explicit ExprVisitorTagStaleIvars(HIRTypeInterner& types);

        [[nodiscard]] const HIRType* visitType(const HIRType* type) override;

        void visitPathParams(HIRPathParams& params) override;
    };

    struct ExprVisitorAddIvars: public HIRExprVisitorDef {
        Context& context;

        struct LocalImplTraitLowering: Monomorphiser {
            Context& context;
            mutable const HIRType* curSelf = nullptr;

            explicit LocalImplTraitLowering(Context& context);

            const HIRType* getType(const Span& sp, const HIRGenericRef& generic) const override;

            HIRConstGeneric getValue(const Span& sp, const HIRGenericRef& generic) const override;

            const HIRType* monomorphType(const Span& sp, const HIRType* type, bool allowInfer = true) const override;
        };

        ExprVisitorAddIvars(Context& context);

        const HIRType* innerVisitType(const HIRType* ty);

        void visitPathParams(HIRPathParams& pp) override;

        [[nodiscard]] const HIRType* visitType(const HIRType* ty) override;

        void visit(HIRExprNodeLet& node) override;
    };

    struct ExprVisitorEnum: public HIRExprVisitor {
        Context& context;
        const HIRType* retType;

        struct RetTarget {
            const HIRType* retType;
            const HIRType* resumeType;
            const HIRType* yieldType;

            RetTarget(const HIRType* retType);

            RetTarget(const HIRType* retType, const HIRType* resumeType, const HIRType* yieldType);
        };

        Vector<RetTarget> closureRetTypes;

        Vector<bool> innerCoerceEnabledStack;

        Vector<HIRExprNodeLoop*> loopBlocks;

        tTraitList traits;

        struct RevisitDefaultUnit: public Context::Revisitor {
            HIRExprNode* node;

            RevisitDefaultUnit(HIRExprNode* node);

            const Span& span(void) const;

            void fmt(ZeroCopyOutput& os) const;

            bool revisit(Context& context, bool isFallback);
        };

        ExprVisitorEnum(Context& context, tTraitList baseTraits, const HIRType* retType);

        void visit(HIRExprNodeBlock& node) override;

        void visit(HIRExprNodeConstBlock& node) override;

        void visit(HIRExprNodeAsm& node) override;

        void visit(HIRExprNodeAsm2& node) override;

        void visit(HIRExprNodeReturn& node) override;

        void visit(HIRExprNodeYield& node) override;

        void visit(HIRExprNodeAWait& node) override;

        void visit(HIRExprNodeUse& node) override;

        void visit(HIRExprNodeLoop& node) override;

        void visit(HIRExprNodeLoopControl& node) override;

        void visit(HIRExprNodeLet& node) override;

        void visit(HIRExprNodeMatch& node) override;

        void visit(HIRExprNodeAssign& node) override;

        void visit(HIRExprNodeBinOp& node) override;

        void visit(HIRExprNodeUniOp& node) override;

        void visit(HIRExprNodeBorrow& node) override;

        void visit(HIRExprNodeRawBorrow& node) override;

        void visit(HIRExprNodeCast& node) override;

        void visit(HIRExprNodeUnsize& node) override;

        void visit(HIRExprNodeIndex& node) override;

        void visit(HIRExprNodeDeref& node) override;

        void visit(HIRExprNodeEmplace& node) override;

        void addIvarsGenericPath(const Span& sp, HIRGenericPath& gp);

        void addIvarsPath(const Span& sp, HIRPath& path);

        const HIRType* getStructenumTy(const Span& sp, bool isStruct, HIRGenericPath& gp);

        void visit(HIRExprNodeTupleVariant& node) override;

        void visit(HIRExprNodeStructLiteral& node) override;

        void visit(HIRExprNodeUnitVariant& node) override;

        void visit(HIRExprNodeCallPath& node) override;

        void visit(HIRExprNodeCallValue& node) override;

        void visit(HIRExprNodeCallMethod& node) override;

        void visit(HIRExprNodeField& node) override;

        void visit(HIRExprNodeTuple& node) override;

        void visit(HIRExprNodeArrayList& node) override;

        void visit(HIRExprNodeArraySized& node) override;

        void visit(HIRExprNodeLiteral& node) override;

        void visit(HIRExprNodePathValue& node) override;

        void visit(HIRExprNodeVariable& node) override;

        void visit(HIRExprNodeConstParam& node) override;

        void visit(HIRExprNodeClosure& node) override;

        void visit(HIRExprNodeGenerator& node) override;

        void visit(HIRExprNodeGeneratorWrapper& node) override;

        void visit(HIRExprNodeAsyncBlock& node) override;

        bool nodeDiverges(const HIRExprNode& node) const;

        void inheritDivergence(HIRExprNode& node, const HIRExprNode& child) const;

        void pushTraits(const tTraitList& list);

        void popTraits(const tTraitList& list);

        void visitGenericPath(const Span& sp, HIRGenericPath& gp);

        void visitPath(const Span& sp, HIRPath& path);

        struct InnerCoerceGuard {
            ExprVisitorEnum& t;

            InnerCoerceGuard(ExprVisitorEnum& t);

            ~InnerCoerceGuard();
        };

        InnerCoerceGuard pushInnerCoerceScoped(bool val);

        void pushInnerCoerce(bool val);

        void popInnerCoerce();

        bool canCoerceInnerResult() const;

        void equateTypesInnerCoerce(const Span& sp, const HIRType* target, HIRExprNodeP& node);
    };

    inline HIRExprNodeP mkExprnodep(HIRExprNode* en, const HIRType* ty) {
        en->resType = mv$(ty);
        return HIRExprNodeP(en);
    }

    inline HIRSimplePath getParentPath(const HIRSimplePath& sp) {
        return sp.parent();
    }

    inline HIRGenericPath getParentPath(const HIRGenericPath& gp) {
        return HIRGenericPath(getParentPath(gp.path), gp.params.clone());
    }

    bool typeContainsImplPlaceholder(HIRTypeInterner& types, const HIRType* t) {
        struct V: public HIRVisitor {
            bool found = false;

            explicit V(HIRTypeInterner& types)
                : HIRVisitor(nullptr, types)
            {
            }

            void visitConstgeneric(const HIRConstGeneric& v) {
                if (v.is_Generic() && v.as_Generic().isPlaceholder()) {
                    found = true;
                }
            }

            void visitPathParams(HIRPathParams& pp) override {
                for (const auto& v : pp.values) {
                    visitConstgeneric(v);
                }
                HIRVisitor::visitPathParams(pp);
            }

            [[nodiscard]] const HIRType* visitType(const HIRType* ty) override {
                if (ty->is_Generic() && ty->as_Generic().isPlaceholder()) {
                    found = true;
                }
                if (const auto* e = ty->opt_Array()) {
                    if (const auto* ase = e->size.opt_Unevaluated()) {
                        visitConstgeneric(*ase);
                    }
                }
                return visitTypeDefaultViaHooks(ty);
            }
        } v(types);

        auto _discard = v.visitType(t);
        (void)_discard;
        return v.found;
    }

    void applyBoundsAsRules(Context& context, const Span& sp, const HIRGenericParams& paramsDef, const Monomorphiser& ms, bool isImplLevel);

    // TODO: Convert these to `Revisitor` instances

    void fixupPatternValuePaths(Context& context, const Span& sp, HIRPattern::Value& val) {
        if (auto* ve = val.opt_Named()) {
            if (ve->binding && ve->path.data.is_UfcsKnown()) {
                auto& pe = ve->path.data.as_UfcsKnown();
                pe.type = context.ivars.addIvars(pe.type);
                context.ivars.addIvarsParams(pe.trait.params);
                context.ivars.addIvarsParams(pe.params);
                context.addTraitBound(sp, pe.type, pe.trait.path, pe.trait.params.clone());
            } else if (ve->binding && ve->path.data.is_UfcsInherent()) {
                auto& pe = ve->path.data.as_UfcsInherent();
                pe.type = context.ivars.addIvars(pe.type);
                context.ivars.addIvarsParams(pe.params);
                context.ivars.addIvarsParams(pe.implParams);
            }
        }
    }

    void fixupPatternValuePaths(Context& context, const Span& sp, HIRPattern& pat) {
        switch (pat.data.tag()) {
            case HIRPatternData::TAG_Any: {
                break;
            }
            case HIRPatternData::TAG_Box: {
                auto& e = pat.data.as_Box();
                fixupPatternValuePaths(context, sp, *e.sub);
                break;
            }
            case HIRPatternData::TAG_Deref: {
                auto& e = pat.data.as_Deref();
                fixupPatternValuePaths(context, sp, *e.sub);
                break;
            }
            case HIRPatternData::TAG_Ref: {
                auto& e = pat.data.as_Ref();
                fixupPatternValuePaths(context, sp, *e.sub);
                break;
            }
            case HIRPatternData::TAG_Tuple: {
                auto& e = pat.data.as_Tuple();
                for (auto& subpat : e.subPatterns) {
                    fixupPatternValuePaths(context, sp, subpat);
                }
                break;
            }
            case HIRPatternData::TAG_SplitTuple: {
                auto& e = pat.data.as_SplitTuple();
                for (auto& subpat : e.leading) {
                    fixupPatternValuePaths(context, sp, subpat);
                }
                for (auto& subpat : e.trailing) {
                    fixupPatternValuePaths(context, sp, subpat);
                }
                break;
            }
            case HIRPatternData::TAG_PathValue: {
                break;
            }
            case HIRPatternData::TAG_PathTuple: {
                auto& e = pat.data.as_PathTuple();
                for (auto& subpat : e.leading) {
                    fixupPatternValuePaths(context, sp, subpat);
                }
                for (auto& subpat : e.trailing) {
                    fixupPatternValuePaths(context, sp, subpat);
                }
                break;
            }
            case HIRPatternData::TAG_PathNamed: {
                auto& e = pat.data.as_PathNamed();
                for (auto& subpat : e.subPatterns) {
                    fixupPatternValuePaths(context, sp, subpat.second);
                }
                break;
            }
            case HIRPatternData::TAG_Or: {
                auto& e = pat.data.as_Or();
                for (auto& subpat : e) {
                    fixupPatternValuePaths(context, sp, subpat);
                }
                break;
            }
            case HIRPatternData::TAG_Value: {
                auto& e = pat.data.as_Value();
                fixupPatternValuePaths(context, sp, e.val);
                break;
            }
            case HIRPatternData::TAG_Range: {
                auto& e = pat.data.as_Range();
                if (e.start) {
                    fixupPatternValuePaths(context, sp, *e.start);
                }
                if (e.end) {
                    fixupPatternValuePaths(context, sp, *e.end);
                }
                break;
            }
            case HIRPatternData::TAG_Slice: {
                auto& e = pat.data.as_Slice();
                for (auto& subpat : e.subPatterns) {
                    fixupPatternValuePaths(context, sp, subpat);
                }
                break;
            }
            case HIRPatternData::TAG_SplitSlice: {
                auto& e = pat.data.as_SplitSlice();
                for (auto& subpat : e.leading) {
                    fixupPatternValuePaths(context, sp, subpat);
                }
                for (auto& subpat : e.trailing) {
                    fixupPatternValuePaths(context, sp, subpat);
                }
                break;
            }
        }
    }

    template <typename F>
    void addCoerceBorrow(Context& context, HIRExprNodeP& origNodePtr, const HIRType* desBorrowInner, F cb) {
        auto borrowType = context.ivars.getType(origNodePtr->resType)->as_Borrow().type;

        HIRExprNodeP* nodePtrPtr = &origNodePtr;

        ASSERT_BUG(Span(), origNodePtr, StringView("Null node pointer passed to `add_coerce_borrow`"));
        while (auto* p = cast<HIRExprNodeBlock>(&**nodePtrPtr)) {
            DEBUG(StringView("- Moving into block"));
            BUG_ASSERT(p->valueNode);
            /* A block yields what its value yields, and that is what this checks.  One
               side may still hold a projection the other has had normalized, which is
               the same type written the other way - so ask again with both normalized
               before calling them different. */
            const auto blockYieldsItsValue = [&]() {
                if (context.ivars.typesEqual(p->resType, p->valueNode->resType)) {
                    return true;
                }
                return context.ivars.typesEqual(
                    context.resolve.expandAssociatedTypes(p->span(), p->resType),
                    context.resolve.expandAssociatedTypes(p->span(), p->valueNode->resType)
                );
            };
            ASSERT_BUG(p->span(), blockYieldsItsValue(), StringView("Block and result mismatch - ") << context.ivars.fmtType(p->resType) << StringView(" != ") << context.ivars.fmtType(p->valueNode->resType));
            p->resType = context.crate.types.borrow(borrowType, desBorrowInner);
            nodePtrPtr = &p->valueNode;
        }
        auto& nodePtr = *nodePtrPtr;
        const auto& srcType = context.ivars.getType(nodePtr->resType);

        if (auto* p = cast<HIRExprNodeBorrow>(&*nodePtr)) {
            nodePtr->resType = context.crate.types.borrow(borrowType, desBorrowInner);

            nodePtrPtr = &p->value;
        } else {
            DEBUG(StringView("- Coercion node isn't a borrow, adding one"));
            auto span = nodePtr->span();
            const auto& srcInnerTy = srcType->as_Borrow().inner;

            auto innerTyRef = context.crate.types.borrow(borrowType, desBorrowInner);

            nodePtr = NEWNODE(srcInnerTy, span, Deref, mv$(nodePtr));
            DEBUG(StringView("- Deref ") << static_cast<const void*>(&*nodePtr) << StringView(" -> ") << nodePtr->resType);
            auto* borrowNode = context.crate.pool->make<HIRExprNodeBorrow>(span, borrowType, mv$(nodePtr));
            nodePtr = mkExprnodep(borrowNode, mv$(innerTyRef));

            DEBUG(StringView("- Borrow ") << static_cast<const void*>(&*nodePtr) << StringView(" -> ") << nodePtr->resType);
            nodePtrPtr = &borrowNode->value;
        }

        cb(*nodePtrPtr);

        context.ivars.markChange();
    }

    enum CoerceResult {
        Unknown,
        Equality,
        Fail,
        Custom,
        Unsize,
    };

    // TODO: Add a (two?) callback(s) that handle type equalities (and possible equalities) so this function doesn't have to mutate the context
    CoerceResult checkUnsizeTys(const Context& context, const Span& sp, const HIRType* dstRaw, const HIRType* srcRaw, Context* contextMut, HIRExprNodeP* nodePtrPtr = nullptr) {
        const auto& dst = context.ivars.getType(dstRaw);
        const auto& src = context.ivars.getType(srcRaw);

        TRACE_FUNCTION_F(StringView("dst=") << dst << StringView(", src=") << src);
        if (context.ivars.typesEqual(dst, src)) {
            return CoerceResult::Equality;
        }

        if (const auto* destinationInfer = dst->opt_Infer(); destinationInfer && destinationInfer->index < context.ivarsSized.length() && context.ivarsSized[destinationInfer->index]) {
            return CoerceResult::Equality;
        }

        auto solverResponse = context.resolve.evaluateCoercionGoal(sp, dst, src, SolverCoercionOp::Unsizing, nodePtrPtr != nullptr);
        if (nodePtrPtr && solverResponse.reachedAutoderefLimit) {
            ERROR(sp, E0000, StringView("Reached the recursion limit while auto-dereferencing ") << src);
        }
        if (solverResponse.effects.certainty == SolverCertainty::Proven) {
            if (contextMut) {
                contextMut->applySolverResponse(sp, solverResponse.effects);
            }
            if (solverResponse.adjustment.kind == SolverCoercionAdjustmentKind::SourceAutoderef) {
                ASSERT_BUG(sp, nodePtrPtr && contextMut && !solverResponse.adjustment.sourceAutoderef.empty(), StringView("Source autoderef coercion has no adjustment target"));
                auto& nodePtr = *nodePtrPtr;
                addCoerceBorrow(*contextMut, nodePtr, solverResponse.adjustment.sourceAutoderef.back(), [&](auto& valueNode) -> void {
                    for (const auto* type : solverResponse.adjustment.sourceAutoderef) {
                        auto span = valueNode->span();
                        ASSERT_BUG(span, !valueNode->resType->is_Array(), StringView("Array->Slice shouldn't be in deref coercions"));
                        valueNode = HIRExprNodeP(context.crate.pool->make<HIRExprNodeDeref>(mv$(span), mv$(valueNode)));
                        DEBUG(StringView("- Deref ") << static_cast<const void*>(&*valueNode) << StringView(" -> ") << type);
                        valueNode->resType = type;
                        context.ivars.getType(valueNode->resType);
                    }
                });
                return solverResponse.adjustment.innerRelation == SolverCoercionRelation::Equality ? CoerceResult::Custom : CoerceResult::Unsize;
            }
            const auto relation = solverResponse.adjustment.innerRelation == SolverCoercionRelation::None ? solverResponse.relation : solverResponse.adjustment.innerRelation;
            return relation == SolverCoercionRelation::Equality ? CoerceResult::Equality : CoerceResult::Unsize;
        }
        if (solverResponse.effects.certainty == SolverCertainty::Ambiguous) {
            if (contextMut) {
                contextMut->applySolverResponse(sp, solverResponse.effects);
            }
            return CoerceResult::Unknown;
        }
        return CoerceResult::Equality;
    }

    CoerceResult checkCoerceTys(const Context& context, const Span& sp, const HIRType* dst, const HIRType* srcR, Context* contextMut = nullptr, HIRExprNodeP* nodePtrPtr = nullptr) {
        auto src = srcR;
        TRACE_FUNCTION_F(dst << StringView(" := ") << src);
        if (context.ivars.typesEqual(dst, src)) {
            return CoerceResult::Equality;
        }

        auto solverResponse = context.resolve.evaluateCoercionGoal(sp, dst, src, SolverCoercionOp::Coercion);
        if (nodePtrPtr && solverResponse.reachedAutoderefLimit) {
            ERROR(sp, E0000, StringView("Reached the recursion limit while auto-dereferencing ") << src);
        }
        if (solverResponse.effects.certainty == SolverCertainty::NoSolution) {
            return CoerceResult::Equality;
        }
        if (solverResponse.effects.certainty == SolverCertainty::Ambiguous) {
            if (contextMut) {
                contextMut->applySolverResponse(sp, solverResponse.effects);
            }
            return CoerceResult::Unknown;
        }
        if (contextMut) {
            contextMut->applySolverResponse(sp, solverResponse.effects);
        }

        const auto retagYieldingValue = [&]() {
            if (nodePtrPtr) {
                auto* valueNode = nodePtrPtr;
                while (auto* block = cast<HIRExprNodeBlock>(valueNode->get())) {
                    ASSERT_BUG(block->span(), block->valueNode, StringView("Coercion reached a non-yielding block"));
                    block->resType = dst;
                    valueNode = &block->valueNode;
                }
                (*valueNode)->resType = dst;
            }
            return CoerceResult::Custom;
        };
        const auto castYieldingValue = [&](const HIRType* type) {
            if (contextMut && nodePtrPtr) {
                auto* valueNode = nodePtrPtr;
                while (auto* block = cast<HIRExprNodeBlock>(valueNode->get())) {
                    ASSERT_BUG(block->span(), block->valueNode, StringView("Coercion reached a non-yielding block"));
                    block->resType = dst;
                    valueNode = &block->valueNode;
                }
                auto span = (*valueNode)->span();
                *valueNode = NEWNODE(type, span, Cast, mv$(*valueNode), type);
            }
            return CoerceResult::Custom;
        };
        const auto& adjustment = solverResponse.adjustment;
        switch (adjustment.kind) {
            case SolverCoercionAdjustmentKind::None:
                ASSERT_BUG(sp, solverResponse.relation == SolverCoercionRelation::Equality, StringView("Proven coercion has no adjustment plan"));
                return CoerceResult::Equality;
            case SolverCoercionAdjustmentKind::Never:
                return CoerceResult::Custom;
            case SolverCoercionAdjustmentKind::Retag:
                return retagYieldingValue();
            case SolverCoercionAdjustmentKind::Unsize:
                return CoerceResult::Unsize;
            case SolverCoercionAdjustmentKind::FunctionPointer:
                return castYieldingValue(dst);
            case SolverCoercionAdjustmentKind::RawPointer: {
                if (adjustment.intermediateType == nullptr) {
                    return adjustment.innerRelation == SolverCoercionRelation::Equality ? CoerceResult::Equality : CoerceResult::Unsize;
                }
                if (contextMut && nodePtrPtr) {
                    auto* valueNode = nodePtrPtr;
                    while (auto* block = cast<HIRExprNodeBlock>(valueNode->get())) {
                        ASSERT_BUG(block->span(), block->valueNode, StringView("Raw-pointer coercion reached a non-yielding block"));
                        block->resType = dst;
                        valueNode = &block->valueNode;
                    }
                    auto span = (*valueNode)->span();
                    *valueNode = NEWNODE(adjustment.intermediateType, span, Cast, mv$(*valueNode), adjustment.intermediateType);
                    if (adjustment.innerRelation == SolverCoercionRelation::Coercion) {
                        span = (*valueNode)->span();
                        *valueNode = NEWNODE(dst, span, Unsize, mv$(*valueNode), dst);
                    }
                    contextMut->ivars.markChange();
                }
                return CoerceResult::Custom;
            }
            case SolverCoercionAdjustmentKind::BorrowToPointer: {
                if (contextMut && nodePtrPtr) {
                    auto& nodePtr = *nodePtrPtr;
                    if (adjustment.innerRelation == SolverCoercionRelation::Coercion) {
                        ASSERT_BUG(sp, adjustment.intermediateType, StringView("Borrow-to-pointer unsize has no intermediate type"));
                        auto span = nodePtr->span();
                        nodePtr = NEWNODE(adjustment.intermediateType, span, Unsize, mv$(nodePtr), adjustment.intermediateType);
                    }
                    auto span = nodePtr->span();
                    nodePtr = NEWNODE(dst, span, Cast, mv$(nodePtr), dst);
                }
                return CoerceResult::Custom;
            }
            case SolverCoercionAdjustmentKind::Borrow: {
                HIRExprNodeP* adjustedNodePtr = nodePtrPtr;
                bool changed = false;
                if (adjustment.intermediateType != nullptr && contextMut && adjustedNodePtr) {
                    while (auto* block = cast<HIRExprNodeBlock>(adjustedNodePtr->get())) {
                        ASSERT_BUG(block->span(), block->valueNode, StringView("Borrow coercion reached a non-yielding block"));
                        block->resType = dst;
                        adjustedNodePtr = &block->valueNode;
                    }
                    const auto& intermediate = adjustment.intermediateType->as_Borrow();
                    auto& nodePtr = *adjustedNodePtr;
                    auto span = nodePtr->span();
                    nodePtr = NEWNODE(intermediate.inner, span, Deref, mv$(nodePtr));
                    nodePtr = NEWNODE(adjustment.intermediateType, span, Borrow, intermediate.type, mv$(nodePtr));
                    contextMut->ivars.markChange();
                    changed = true;
                }
                if (!adjustment.sourceAutoderef.empty()) {
                    ASSERT_BUG(sp, contextMut && adjustedNodePtr, StringView("Borrow coercion autoderef has no adjustment target"));
                    addCoerceBorrow(*contextMut, *adjustedNodePtr, adjustment.sourceAutoderef.back(), [&](auto& valueNode) -> void {
                        for (const auto* type : adjustment.sourceAutoderef) {
                            auto span = valueNode->span();
                            ASSERT_BUG(span, !valueNode->resType->is_Array(), StringView("Array->Slice shouldn't be in deref coercions"));
                            valueNode = HIRExprNodeP(context.crate.pool->make<HIRExprNodeDeref>(mv$(span), mv$(valueNode)));
                            valueNode->resType = type;
                        }
                    });
                    changed = true;
                }
                if (adjustment.innerRelation == SolverCoercionRelation::Coercion) {
                    if (!changed) {
                        return CoerceResult::Unsize;
                    }
                    if (contextMut && adjustedNodePtr) {
                        auto span = (*adjustedNodePtr)->span();
                        *adjustedNodePtr = NEWNODE(dst, span, Unsize, mv$(*adjustedNodePtr), dst);
                    }
                    return CoerceResult::Custom;
                }
                return changed ? CoerceResult::Custom : CoerceResult::Equality;
            }
            case SolverCoercionAdjustmentKind::SourceAutoderef:
                BUG(sp, StringView("Top-level coercion received an unsize-only source autoderef plan"));
        }
        UNREACHABLE();

    }

    bool checkCoerce(Context& context, const Context::Coercion& v) {
        if (!v.rightNodePtr) {
            const auto& sp = v.span();
            const auto* tyDst = context.getType(v.leftTy);
            const auto* tySrc = context.getType(v.sourceType());
            TRACE_FUNCTION_F(v << StringView(" - ") << context.ivars.fmtType(tyDst) << StringView(" := ") << context.ivars.fmtType(tySrc));
            const auto result = v.op == SolverCoercionOp::Coercion
                ? checkCoerceTys(context, sp, tyDst, tySrc, &context)
                : checkUnsizeTys(context, sp, tyDst, tySrc, &context);
            switch (result) {
                case CoerceResult::Fail:
                case CoerceResult::Unknown:
                    return false;
                case CoerceResult::Equality:
                    context.equateTypes(sp, tyDst, tySrc);
                    return true;
                case CoerceResult::Custom:
                case CoerceResult::Unsize:
                    return true;
            }
            UNREACHABLE();
        }
        HIRExprNodeP& nodePtr = *v.rightNodePtr;
        const auto& sp = v.span();
        const auto& tyDst = context.ivars.getType(v.leftTy);
        const auto& tySrc = context.ivars.getType(v.sourceType());
        TRACE_FUNCTION_FR(v << StringView(" - ") << context.ivars.fmtType(tyDst) << StringView(" := ") << context.ivars.fmtType(tySrc), v << StringView(" - ") << context.ivars.fmtType(v.leftTy) << StringView(" := ") << context.ivars.fmtType(nodePtr->resType));

        struct PendingSourceDeref: HIRExprVisitorDef {
            Context& context;
            bool found = false;

            explicit PendingSourceDeref(Context& context)
                : HIRExprVisitorDef(context.crate.types)
                , context(context)
            {
            }

            bool sameUnresolvedIvar(const HIRType* left, const HIRType* right) const {
                left = context.ivars.getType(left);
                right = context.ivars.getType(right);
                const auto* leftInfer = left->opt_Infer();
                const auto* rightInfer = right->opt_Infer();
                if (!leftInfer || !rightInfer) {
                    return false;
                }
                if (leftInfer->index == ~0u || rightInfer->index == ~0u) {
                    return left == right;
                }
                return leftInfer->index == rightInfer->index;
            }

            void visitNodePtr(HIRExprNodeP& node) override {
                if (!found) {
                    HIRExprVisitorDef::visitNodePtr(node);
                }
            }

            void visit(HIRExprNodeDeref& node) override {
                if (node.traitUsed == HIRExprNodeDeref::TraitUsed::Unknown) {
                    const auto* result = context.ivars.getType(node.resType);
                    const auto* infer = result->opt_Infer();
                    if (infer && !infer->isLit()) {
                        found = true;
                        return;
                    }
                }
                if (node.traitUsed == HIRExprNodeDeref::TraitUsed::Trait) {
                    found = std::any_of(context.linkAssoc.begin(), context.linkAssoc.end(), [&](const Context::Associated& rule) {
                        return rule.operatorKind == TypeckPrimitiveOperator::Deref && sameUnresolvedIvar(rule.leftTy, node.resType);
                    });
                }
                if (!found) {
                    HIRExprVisitorDef::visit(node);
                }
            }

            void visit(HIRExprNodeBlock& node) override {
                if (node.valueNode) {
                    visitNodePtr(node.valueNode);
                }
            }

            void visit(HIRExprNodeCallPath&) override {
            }

            void visit(HIRExprNodeCallValue&) override {
            }

            void visit(HIRExprNodeCallMethod&) override {
            }

            void visit(HIRExprNodeClosure&) override {
            }

            void visit(HIRExprNodeGenerator&) override {
            }

            void visit(HIRExprNodeGeneratorWrapper&) override {
            }

            void visit(HIRExprNodeAsyncBlock&) override {
            }
        } dependency{context};

        dependency.visitNodePtr(nodePtr);
        const bool hasPendingDerefTarget = dependency.found;
        if (hasPendingDerefTarget) {
            DEBUG(StringView("Deref target is pending - keep coercion"));
            return false;
        }

        const auto coercionResult = checkCoerceTys(context, sp, tyDst, tySrc, &context, &nodePtr);
        switch (coercionResult) {
            case CoerceResult::Fail:
                return false;
            case CoerceResult::Unknown:
                DEBUG(StringView("Unknown - keep"));
                return false;
            case CoerceResult::Custom:
                DEBUG(StringView("Custom - Completed"));
                return true;
            case CoerceResult::Equality: {
                DEBUG(StringView("Trigger equality - Completed"));
                context.equateTypes(sp, tyDst, tySrc);
                /* `!` equates with any type without changing it, so a diverging source
                   comes out of that equality still holding the type it diverged with
                   rather than the one the context asked for.  Upstream records the
                   asked-for type on such a node - the value never arrives, so no other
                   type can contradict it - and every later pass reads the node's type
                   back and compares it against the place it feeds. */
                const bool sourceDiverges = visitTyWith(tySrc, [](const HIRType* inner) {
                    return inner->is_Diverge();
                });
                if (sourceDiverges && context.getType(nodePtr->resType) != context.getType(tyDst)) {
                    DEBUG(StringView("Diverging source keeps the asked-for type ") << tyDst);
                    nodePtr->resType = tyDst;
                }
                return true;
            }
            case CoerceResult::Unsize:
                DEBUG(StringView("Add _Unsize ") << static_cast<const void*>(&*nodePtr) << StringView(" -> ") << tyDst);
                auto span = nodePtr->span();
                nodePtr = NEWNODE(tyDst, span, Unsize, mv$(nodePtr), tyDst);
                return true;
        }
        UNREACHABLE();
    }

    enum class AssociatedCheckResult {
        Complete,
        Retry,
        Stalled,
        Ambiguous,
    };

    bool typeNeedsFurtherInference(const Context& context, const HIRType* type) {
        bool pending = false;
        visitTyWith(type, [&](const HIRType* inner) {
            const auto* resolved = context.getType(inner);
            if (resolved != inner) {
                pending = typeNeedsFurtherInference(context, resolved);
                return pending;
            }
            if (resolved->is_Infer()) {
                pending = true;
                return true;
            }
            if (const auto* path = resolved->opt_Path(); path && path->binding.is_Unbound()) {
                pending = true;
                return true;
            }
            return false;
        });
        return pending;
    }

    bool pathParamsNeedFurtherInference(const Context& context, const HIRPathParams& params) {
        for (const auto& type : params.types) {
            if (typeNeedsFurtherInference(context, type)) {
                return true;
            }
        }
        return false;
    }

    bool coercionEndpointCanDetermineType(const Context& context, const IvarCoercionEndpoint& endpoint) {
        if (endpoint.direction != SolverCoercionConstraint::Direction::InputIsDestination) {
            return false;
        }
        const auto* other = context.getType(endpoint.other);
        const auto* infer = other->opt_Infer();
        return !infer || infer->isLit();
    }

    /* The result type of a node still waiting to be revisited is that node's to
       produce - a deref cannot say what it derefs to until the expression under it
       resolves.  Such a variable is not a coercion destination with nothing else to
       go on, however few coercion edges it has. */
    bool isResultOfPendingRevisit(const Context& context, const HIRType* type) {
        return std::any_of(context.toVisit.begin(), context.toVisit.end(), [&](const HIRExprNode* node) {
            return context.getType(node->resType) == type;
        });
    }

    AssociatedCheckResult checkAssociated(Context& context, const IvarCoercionIndex& coercionIndex, Context::Associated& v) {
        const auto& sp = v.span;

        TRACE_FUNCTION_F(v);
        auto normalizeConstParams = [&](HIRPathParams& params, const HIRGenericParams* paramsDef) {
            context.ivars.expandIvarsParams(params);
            if (paramsDef && params.values.size() == paramsDef->values.size()) {
                ConvertHIRConstantEvaluateMethodParams(sp, context.resolve.board(), context.crate, paramsDef, params);
            }
            for (auto& value : params.values) {
                ConvertHIRConstantEvaluateConstGeneric(sp, context.resolve.board(), context.crate, value);
            }
        };
        const auto& traitDef = context.crate.getTraitByPath(sp, v.trait);
        normalizeConstParams(v.params, &traitDef.params);
        normalizeConstParams(v.atyPp, nullptr);

        for (auto& ty : v.params.types) {
            auto revealed = context.revealOpaqueTypes(ty);
            if (revealed != ty) {
                ty = context.expandAssociatedTypes(sp, mv$(revealed));
            }
        }

        std::optional<const HIRType*> outputType;

        struct H {
            static bool unaryCanUseExpected(TypeckPrimitiveOperator op, const HIRType* type) {
                if (primitiveOperatorHasBuiltin(op, type)) {
                    return true;
                }
                const auto* infer = type->opt_Infer();
                if (!infer) {
                    return false;
                }
                return (op == TypeckPrimitiveOperator::Not && infer->tyClass == HIRInferClass::Integer) || (op == TypeckPrimitiveOperator::Neg && (infer->tyClass == HIRInferClass::Integer || infer->tyClass == HIRInferClass::Float));
            }
        };

        /* A nested coercion ivar guides candidate selection only when all of
         * its candidate-binding concrete source endpoints agree. Divergence
         * is not guidance. */
        const auto concreteCoercionSource = [&](const HIRType* type) {
            const auto* infer = context.getType(type)->opt_Infer();
            if (!infer || infer->index == ~0u || infer->index >= coercionIndex.refs.size()) {
                return static_cast<const HIRType*>(nullptr);
            }
            const HIRType* concrete = nullptr;
            for (const auto& endpoint : coercionIndex[infer->index].endpoints) {
                if (endpoint.direction != SolverCoercionConstraint::Direction::InputIsDestination || !coercionEndpointCanDetermineType(context, endpoint)) {
                    continue;
                }
                const auto* sourceType = context.getType(endpoint.other);
                if (!sourceType->is_Infer() && !sourceType->is_Diverge()) {
                    if (concrete && concrete != sourceType && !concrete->equalsIgnoringRegions(sourceType)) {
                        return static_cast<const HIRType*>(nullptr);
                    }
                    concrete = sourceType;
                }
            }
            return concrete;
        };
        const auto concreteCoercionTarget = [&](const HIRType* type) {
            const auto* infer = context.getType(type)->opt_Infer();
            if (!infer || infer->index == ~0u || infer->index >= coercionIndex.refs.size()) {
                return static_cast<const HIRType*>(nullptr);
            }
            const HIRType* concrete = nullptr;
            for (const auto& endpoint : coercionIndex[infer->index].endpoints) {
                if (endpoint.direction != SolverCoercionConstraint::Direction::InputIsSource || !coercionEndpointCanDetermineType(context, endpoint)) {
                    continue;
                }
                const auto* targetType = context.getType(endpoint.other);
                if (!targetType->is_Infer() && !targetType->is_Diverge()) {
                    if (concrete && concrete != targetType && !concrete->equalsIgnoringRegions(targetType)) {
                        return static_cast<const HIRType*>(nullptr);
                    }
                    concrete = targetType;
                }
            }
            return concrete;
        };

        bool hasSemanticOperatorImpl = false;
        bool sawCurrentOperatorImpl = false;
        bool currentOperatorImplHasBuiltinSignature = false;
        SolverResponse operatorResponse;
        const SolverImpl* operatorApplicable = nullptr;
        bool hasOperatorResponse = false;
        bool operatorProbeUsesOriginalInputs = true;
        if (v.operatorKind != TypeckPrimitiveOperator::None) {
            auto probeParams = v.params.clone();
            if (probeParams.types.size() == 1) {
                if (const auto* source = concreteCoercionSource(probeParams.types.front())) {
                    probeParams.types.front() = source;
                    operatorProbeUsesOriginalInputs = false;
                }
            }
            const SolverOperatorGoal operatorGoal{
                .operation = v.operatorKind,
                .outputName = v.name.c_str(),
                .outputParams = &v.atyPp,
                .currentImpl = context.currentTraitImpl,
            };
            context.resolve.probeTraitGoalMayApply(sp, v.trait, probeParams, v.implTy, [&](SolverMayApply probe) {
                auto& response = probe.effects;
                hasSemanticOperatorImpl = response.operatorSummary.hasSemanticImpl;
                sawCurrentOperatorImpl = response.operatorSummary.sawCurrentImpl;
                currentOperatorImplHasBuiltinSignature = response.operatorSummary.currentImplHasBuiltinSignature;
                const bool hasImpl = probe.candidate != nullptr;
                operatorApplicable = probe.candidate;
                operatorResponse = std::move(response);
                hasOperatorResponse = true;
                return hasImpl;
            }, {
                .assocName = v.name.c_str(),
                .assocType = v.name == "" ? nullptr : v.leftTy,
                .assocParams = v.name == "" ? nullptr : &v.atyPp,
                .allowInferInputs = true,
                .operatorGoal = &operatorGoal,
                .ambiguity = SolverAmbiguityPolicy::Report,
            });
        }

        const bool operatorTypesAreResolved = !context.ivars.typeContainsIvars(v.implTy) && !context.ivars.pathparamsContainIvars(v.params, /*only_unbound=*/false) && (v.name == "" || !context.ivars.typeContainsIvars(v.leftTy));

        if (v.isOperator && v.params.types.empty() && context.getType(v.implTy)->is_Diverge() && H::unaryCanUseExpected(v.operatorKind, context.getType(v.leftTy))) {
            return AssociatedCheckResult::Complete;
        }

        /* A concrete associated output can select Self through the trait's typed
         * response even when every input still contains inference variables. */
        const bool outputConstrainsSelf = v.isOperator && v.name != "" && !context.getType(v.leftTy)->is_Diverge() && !context.ivars.typeContainsIvars(v.leftTy);
        if (const auto* e = context.ivars.getType(v.implTy)->opt_Infer()) {
            const bool hasSelfCoercionGuidance = e->index != ~0u && e->index < coercionIndex.refs.size() && !coercionIndex[e->index].endpoints.empty();
            // TODO: ?
            if (!e->isLit() && v.params.types.empty() && !hasSelfCoercionGuidance && !outputConstrainsSelf) {
                return AssociatedCheckResult::Ambiguous;
            }

            if (!e->isLit() && !hasSelfCoercionGuidance && !outputConstrainsSelf) {
                return AssociatedCheckResult::Ambiguous;
            }
        }

        const auto currentOperatorUsesLanguagePrimitive = [&]() {
            if (!v.isOperator || v.operatorKind == TypeckPrimitiveOperator::None || (sawCurrentOperatorImpl && !currentOperatorImplHasBuiltinSignature)) {
                return false;
            }

            const auto& implTy = context.getType(v.implTy);
            if (v.params.types.size() == 0) {
                return primitiveOperatorHasBuiltin(v.operatorKind, implTy);
            }
            if (v.params.types.size() == 1) {
                return primitiveOperatorHasLanguageCandidate(v.operatorKind, implTy, context.getType(v.params.types.front()));
            }
            return false;
        };

        if (!hasSemanticOperatorImpl && operatorTypesAreResolved && currentOperatorUsesLanguagePrimitive()) {
            return AssociatedCheckResult::Complete;
        }

        ThinVector<SolverCoercionConstraint> coercionGoals;
        const auto appendCoercionGoals = [&](const HIRType* rawInput, unsigned typeIndex, bool isSelf) {
            const auto* input = context.getType(rawInput);
            const auto* infer = input->opt_Infer();
            if (!infer || infer->index == ~0u || infer->index >= coercionIndex.refs.size()) {
                return;
            }
            const bool inputRequiresSized = infer->index < context.ivarsSized.length() && context.ivarsSized[infer->index];
            const auto append = [&](const IvarCoercionEndpoint& endpoint) {
                auto* other = context.getType(endpoint.other);
                if (const auto* otherInfer = other->opt_Infer(); otherInfer && otherInfer->index != ~0u) {
                    other = endpoint.direction == SolverCoercionConstraint::Direction::InputIsDestination ? concreteCoercionSource(other) : concreteCoercionTarget(other);
                    if (!other) {
                        return;
                    }
                }
                coercionGoals.push_back(
                    SolverCoercionConstraint{
                        static_cast<unsigned>(typeIndex),
                        other,
                        endpoint.direction,
                        endpoint.op,
                        isSelf,
                        inputRequiresSized,
                        endpoint.op == SolverCoercionOp::Unsizing,
                        coercionEndpointCanDetermineType(context, endpoint),
                        endpoint.alternativeGroup,
                    }
                );
            };
            for (const auto& endpoint : coercionIndex[infer->index].endpoints) {
                if (endpoint.direction == SolverCoercionConstraint::Direction::InputIsDestination && context.getType(endpoint.other)->is_Diverge()) {
                    continue;
                }
                append(endpoint);
            }
        };
        appendCoercionGoals(v.implTy, 0, true);
        for (size_t typeIndex = 0; typeIndex < v.params.types.size(); typeIndex++) {
            appendCoercionGoals(v.params.types[typeIndex], static_cast<unsigned>(typeIndex), false);
        }
        const bool hasSelfCoercionGoal = std::any_of(coercionGoals.begin(), coercionGoals.end(), [](const SolverCoercionConstraint& constraint) {
            return constraint.isSelf;
        });
        const bool isFnAssociated = v.name != "" && (v.trait == context.resolve.langFn() || v.trait == context.resolve.langFnMut() || v.trait == context.resolve.langFnOnce());
        const auto registerLateClosureOutput = [&](const HIRType* implType) {
            if (!isFnAssociated) {
                return static_cast<const HIRType*>(nullptr);
            }
            const auto* closure = implType->is_NodeType() ? implType->as_NodeType().opt_Closure() : nullptr;
            const auto* expectedOutput = context.getType(v.leftTy);
            const bool divergingClosureOutput = closure
                && (*closure)->returnType->is_Infer()
                && (context.getType((*closure)->returnType)->is_Diverge() || context.usedNeverFallback((*closure)->returnType));
            if (divergingClosureOutput && !expectedOutput->is_Diverge() && !context.ivars.typeContainsIvars(expectedOutput)) {
                DEBUG(StringView("[check_associated] - apply late expected output ") << expectedOutput << StringView(" to diverging closure"));
                context.registerClosureReturnObligation(sp, *closure, expectedOutput);
                return (*closure)->returnType;
            }
            return static_cast<const HIRType*>(nullptr);
        };
        const HIRType* lateClosureReturn = registerLateClosureOutput(context.getType(v.implTy));

        /* The current primitive impl is redundant only after every operator
         * type is known. Before then its typed head carries inference effects. */
        const auto* excludedCurrentOperatorImpl = currentOperatorUsesLanguagePrimitive() && operatorTypesAreResolved ? context.currentTraitImpl : nullptr;

        SolverResponse response;
        const SolverImpl* responseApplicable = nullptr;
        bool hasResponse = false;
        if (hasOperatorResponse && operatorProbeUsesOriginalInputs && coercionGoals.empty()) {
            response = std::move(operatorResponse);
            responseApplicable = operatorApplicable;
            hasResponse = true;
        } else {
            context.resolve.probeTraitGoalMayApply(
                sp,
                v.trait,
                v.params,
                v.implTy,
                [&](SolverMayApply probe) {
                    response = std::move(probe.effects);
                    responseApplicable = probe.candidate;
                    hasResponse = true;
                    return true;
                },
                {
                    .assocName = v.name.c_str(),
                    .assocType = v.name == "" || lateClosureReturn ? nullptr : v.leftTy,
                    .assocParams = v.name == "" ? nullptr : &v.atyPp,
                    .allowInferInputs = true,
                    .excludedImpl = excludedCurrentOperatorImpl,
                    .coercions = coercionGoals.empty() ? nullptr : &coercionGoals,
                }
            );
        }
        if (!lateClosureReturn && hasResponse && responseApplicable) {
            lateClosureReturn = registerLateClosureOutput(responseApplicable->getImplType(context.crate.types));
            if (lateClosureReturn) {
                const auto isLateOutputRelation = [&](const HIRType* left, const HIRType* right) {
                    const auto matches = [&](const HIRType* actual, const HIRType* expected) {
                        return actual == expected || context.ivars.typesEqual(context.getType(actual), context.getType(expected));
                    };
                    return (matches(left, v.leftTy) && matches(right, lateClosureReturn))
                        || (matches(right, v.leftTy) && matches(left, lateClosureReturn));
                };
                SolverSlotValues retainedSlots;
                for (size_t i = 0; i < response.slots.typeInputs.size(); i++) {
                    if (!isLateOutputRelation(response.slots.typeInputs[i], response.slots.types[i])) {
                        retainedSlots.typeInputs.push_back(response.slots.typeInputs[i]);
                        retainedSlots.types.push_back(response.slots.types[i]);
                    }
                }
                retainedSlots.valueInputs = std::move(response.slots.valueInputs);
                retainedSlots.values = std::move(response.slots.values);
                response.slots = std::move(retainedSlots);

                ThinVector<SolverTypeEquality> retainedEqualities;
                for (const auto& equality : response.equalities) {
                    if (!isLateOutputRelation(equality.left, equality.right)) {
                        retainedEqualities.push_back(equality);
                    }
                }
                response.equalities = std::move(retainedEqualities);
            }
        }

        if (hasResponse) {
            /* An ambiguous operator response may already expose the selected
             * impl's input equalities while its nested obligations still
             * contain fresh candidate variables.  Detaching those obligations
             * would lose the relation to the expression coercion (for example,
             * &A: PartialEq<&B> becoming A: PartialEq<_>).  Apply the input
             * effects, but keep the operator rule stalled until its inputs are
             * concrete; the retry then materialises obligations from the exact
             * trait head. */
            const bool operatorInputsNeedInference = v.isOperator
                && (typeNeedsFurtherInference(context, v.implTy) || pathParamsNeedFurtherInference(context, v.params));
            if (response.certainty == SolverCertainty::Ambiguous && operatorInputsNeedInference && !response.obligations.empty()) {
                response.obligations.clear();
                context.applySolverResponse(sp, response);
                return AssociatedCheckResult::Stalled;
            }
            if (response.certainty == SolverCertainty::Ambiguous && !hasSelfCoercionGoal) {
                /* Preserve only relations involving literal-class inputs before
                 * returning ambiguity; final numeric fallback can then retry. */
                SolverResponse literalEffects;
                literalEffects.certainty = response.certainty;
                const auto isLiteralIvar = [&](const HIRType* type) {
                    const auto* infer = context.getType(type)->opt_Infer();
                    return infer && infer->isLit();
                };
                for (size_t i = 0; i < response.slots.typeInputs.size(); i++) {
                    if (isLiteralIvar(response.slots.typeInputs[i]) || isLiteralIvar(response.slots.types[i])) {
                        literalEffects.slots.typeInputs.push_back(response.slots.typeInputs[i]);
                        literalEffects.slots.types.push_back(response.slots.types[i]);
                    }
                }
                for (const auto& equality : response.equalities) {
                    if (isLiteralIvar(equality.left) || isLiteralIvar(equality.right)) {
                        literalEffects.equalities.push_back(equality);
                    }
                }
                context.applySolverResponse(sp, literalEffects);
                const auto* implType = context.getType(v.implTy);
                if (const auto* path = implType->opt_Path(); path && path->binding.is_Unbound()) {
                    return AssociatedCheckResult::Stalled;
                }
                if (const auto* infer = implType->opt_Infer(); infer && !infer->isLit()) {
                    return AssociatedCheckResult::Ambiguous;
                }
            }
            context.applySolverResponse(sp, response);
            if (!responseApplicable) {
                return response.certainty == SolverCertainty::Proven ? AssociatedCheckResult::Complete : AssociatedCheckResult::Ambiguous;
            }
            if (response.certainty == SolverCertainty::Ambiguous) {
                if (!response.obligations.empty()) {
                    return AssociatedCheckResult::Complete;
                }
                if (v.name != "" && (v.trait == context.resolve.langFn() || v.trait == context.resolve.langFnMut() || v.trait == context.resolve.langFnOnce())) {
                    const auto* implType = context.getType(v.implTy);
                    const auto* closure = implType->is_NodeType() ? implType->as_NodeType().opt_Closure() : nullptr;
                    if (closure && context.ivars.typesEqual(v.leftTy, (*closure)->returnType)) {
                        return AssociatedCheckResult::Complete;
                    }
                }
                if (v.name != "" && typeNeedsFurtherInference(context, v.leftTy)) {
                    return AssociatedCheckResult::Stalled;
                }
            }
            return AssociatedCheckResult::Complete;
        }

        const bool needsInference = typeNeedsFurtherInference(context, v.implTy) || pathParamsNeedFurtherInference(context, v.params) || (v.name != "" && (typeNeedsFurtherInference(context, v.leftTy) || pathParamsNeedFurtherInference(context, v.atyPp)));
        if (needsInference) {
            return AssociatedCheckResult::Stalled;
        }
        if (v.trait == context.resolve.langUnsize()) {
            ASSERT_BUG(sp, v.params.types.size() == 1, StringView("Incorrect number of parameters for Unsize"));
            const auto& srcTy = context.getType(v.implTy);
            const auto& dstTy = context.getType(v.params.types[0]);
            context.equateTypes(sp, dstTy, srcTy);
            return AssociatedCheckResult::Complete;
        }
        if (v.operatorKind != TypeckPrimitiveOperator::None && (v.params.types.size() == 0 ? primitiveOperatorHasBuiltin(v.operatorKind, context.getType(v.implTy)) : v.params.types.size() == 1 && primitiveOperatorHasBuiltin(v.operatorKind, context.getType(v.implTy), context.getType(v.params.types.at(0))))) {
            return AssociatedCheckResult::Complete;
        }
        if (v.name == "") {
            ERROR(sp, E0000, StringView("Failed to find an impl of ") << v.trait << context.ivars.fmt(v.params) << StringView(" for ") << context.ivars.fmtType(v.implTy));
        } else {
            ERROR(sp, E0000, StringView("Failed to find an impl of ") << v.trait << context.ivars.fmt(v.params) << StringView(" for ") << context.ivars.fmtType(v.implTy) << StringView(" with ") << v.name << StringView(" = ") << context.ivars.fmtType(v.leftTy));
        }
    }

    bool pathParamsHaveUntrackedConst(const HIRPathParams& params) {
        return std::any_of(params.values.begin(), params.values.end(), [](const auto& value) {
            return value.is_Infer() || value.is_Unevaluated();
        });
    }

    bool setAssociatedStall(Context& context, Context::Associated& rule) {
        rule.stalledOn.clear();

        const auto typeCanStall = [](const HIRType* type) {
            return (type->flags & (HIRType::HAS_UNEVALUATED_CONST | HIRType::HAS_DEFERRED_CONST)) == 0;
        };
        if (!typeCanStall(rule.implTy) || (rule.name != "" && !typeCanStall(rule.leftTy)) || pathParamsHaveUntrackedConst(rule.params) || pathParamsHaveUntrackedConst(rule.atyPp)) {
            return false;
        }
        for (const auto type : rule.params.types) {
            if (!typeCanStall(type)) {
                return false;
            }
        }
        for (const auto type : rule.atyPp.types) {
            if (!typeCanStall(type)) {
                return false;
            }
        }

        AssociatedStallCollector collector{context, rule.stalledOn};
        collector.addType(rule.implTy);
        if (rule.name != "") {
            collector.addType(rule.leftTy);
        }
        for (const auto type : rule.params.types) {
            collector.addType(type);
        }
        for (const auto type : rule.atyPp.types) {
            collector.addType(type);
        }
        collector.collect();

        if (collector.hasRawInfer || rule.stalledOn.empty()) {
            rule.stalledOn.clear();
            return false;
        }
        return true;
    }

    bool associatedStillStalled(const Context& context, const IvarCoercionIndex& coercionIndex, const Context::Associated& rule) {
        if (rule.stalledOn.empty()) {
            return false;
        }
        return std::all_of(rule.stalledOn.begin(), rule.stalledOn.end(), [&](const auto& dependency) {
            if (context.ivars.getType(dependency.index) != dependency.resolved) {
                return false;
            }
            if (dependency.index >= coercionIndex.refs.size()) {
                return true;
            }
            const auto& endpoints = coercionIndex[dependency.index].endpoints;
            return std::none_of(endpoints.begin(), endpoints.end(), [&](const auto& endpoint) {
                const auto* other = context.getType(endpoint.other);
                const auto* infer = other->opt_Infer();
                return !infer || infer->index == ~0u;
            });
        });
    }

    bool typeHasIndependentUnresolvedIvar(const Context& context, const HIRType* type, unsigned int exceptIndex, const ActiveOperatorOutput* active = nullptr);

    bool operatorOutputHasIndependentInput(const Context& context, unsigned int index, unsigned int exceptIndex, const ActiveOperatorOutput* active) {
        for (auto* entry = active; entry; entry = entry->parent) {
            if (entry->index == index) {
                return false;
            }
        }
        const ActiveOperatorOutput activeEntry{index, active};

        for (const auto& associated : context.linkAssoc) {
            if (!associated.isOperator || associated.name == "") {
                continue;
            }
            const auto* output = context.getType(associated.leftTy)->opt_Infer();
            if (!output || output->index != index) {
                continue;
            }
            bool hasIndependentInput = typeHasIndependentUnresolvedIvar(context, associated.implTy, exceptIndex, &activeEntry);
            for (const auto& type : associated.params.types) {
                hasIndependentInput |= typeHasIndependentUnresolvedIvar(context, type, exceptIndex, &activeEntry);
            }
            if (!hasIndependentInput) {
                return false;
            }
        }
        return true;
    }

    bool typeHasIndependentUnresolvedIvar(const Context& context, const HIRType* type, unsigned int exceptIndex, const ActiveOperatorOutput* active) {
        bool found = false;
        visitTyWith(type, [&](const HIRType* inner) {
            if (found) {
                return true;
            }
            const auto* resolved = context.getType(inner);
            if (resolved != inner) {
                found = typeHasIndependentUnresolvedIvar(context, resolved, exceptIndex, active);
                return true;
            }
            if (const auto* infer = resolved->opt_Infer()) {
                found = infer->index != exceptIndex && !infer->isLit() && operatorOutputHasIndependentInput(context, infer->index, exceptIndex, active);
            }
            return found;
        });
        return found;
    }

    bool typeDependsOnIvar(const Context& context, const HIRType* type, unsigned int index) {
        bool found = false;
        visitTyWith(type, [&](const HIRType* inner) {
            const auto* resolved = context.getType(inner);
            if (const auto* infer = resolved->opt_Infer(); infer && infer->index == index) {
                found = true;
                return true;
            }
            if (resolved != inner && typeDependsOnIvar(context, resolved, index)) {
                found = true;
                return true;
            }
            return false;
        });
        return found;
    }

    bool numericDefaultMustWait(const Context& context, unsigned int index) {
        for (const auto& coercion : context.linkCoerce) {
            const auto* source = coercion->sourceType();
            const bool destinationUsesIndex = typeDependsOnIvar(context, coercion->leftTy, index);
            const bool sourceUsesIndex = typeDependsOnIvar(context, source, index);
            if ((destinationUsesIndex && source->mayHaveAssociatedType() && typeHasIndependentUnresolvedIvar(context, source, index)) || (sourceUsesIndex && coercion->leftTy->mayHaveAssociatedType() && typeHasIndependentUnresolvedIvar(context, coercion->leftTy, index))) {
                return true;
            }
        }
        for (const auto& associated : context.linkAssoc) {
            bool usesIndex = typeDependsOnIvar(context, associated.implTy, index);
            for (const auto& type : associated.params.types) {
                usesIndex |= typeDependsOnIvar(context, type, index);
            }
            if (!usesIndex) {
                continue;
            }
            if (typeHasIndependentUnresolvedIvar(context, associated.implTy, index)) {
                return true;
            }
            for (const auto& type : associated.params.types) {
                if (typeHasIndependentUnresolvedIvar(context, type, index)) {
                    return true;
                }
            }
        }
        for (const auto* pending : context.toVisit) {
            if (pending->nodeKind() == HIRExprNodeIndex::kind) {
                const auto& node = static_cast<const HIRExprNodeIndex&>(*pending);
                if (!typeDependsOnIvar(context, node.cache.indexTy, index) && !typeDependsOnIvar(context, node.index->resType, index)) {
                    continue;
                }
                if (typeHasIndependentUnresolvedIvar(context, node.value->resType, index)) {
                    return true;
                }
            }
            if (pending->nodeKind() == HIRExprNodeCallMethod::kind) {
                const auto& node = static_cast<const HIRExprNodeCallMethod&>(*pending);
                const bool argumentUsesIndex = std::any_of(node.args.begin(), node.args.end(), [&](const auto& argument) {
                    return typeDependsOnIvar(context, argument->resType, index);
                });
                if (argumentUsesIndex && typeHasIndependentUnresolvedIvar(context, node.value->resType, index)) {
                    return true;
                }
            }
            if (pending->nodeKind() == HIRExprNodeCallValue::kind) {
                const auto& node = static_cast<const HIRExprNodeCallValue&>(*pending);
                const bool argumentUsesIndex = std::any_of(node.args.begin(), node.args.end(), [&](const auto& argument) {
                    return typeDependsOnIvar(context, argument->resType, index);
                });
                if (argumentUsesIndex && typeHasIndependentUnresolvedIvar(context, node.value->resType, index)) {
                    return true;
                }
            }
        }
        return false;
    }

    bool coercionCandidateIsInvalid(const Span& sp, Context& context, const IvarCoercionRefs& coercionRefs, const HIRType* tyL, const HIRType* newTy) {
        TRACE_FUNCTION_F(tyL << StringView(" <- ") << newTy);
        const auto ivarIdx = tyL->as_Infer().index;
        bool usedTy = false;

        struct Cb {
            bool& usedTy;
            const Span& sp;
            const Context& context;
            unsigned int ivarIdx;
            const HIRType* newTy;

            Cb(bool& usedTy, const Span& sp, const Context& context, unsigned int ivarIdx, const HIRType* newTy)
                : usedTy(usedTy)
                , sp(sp)
                , context(context)
                , ivarIdx(ivarIdx)
                , newTy(newTy)
            {
            }

            const HIRType* operator()(const HIRType* ty) {
                const auto* e = ty->opt_Infer();
                if (!e) {
                    return nullptr;
                }
                if (e->index == ivarIdx) {
                    usedTy = true;
                    return newTy;
                }
                const auto& rty = context.getType(ty);
                if (const auto* resolved = rty->opt_Infer(); resolved && resolved->index == e->index) {
                    return nullptr;
                }
                return cloneTyWith(context.crate.types, sp, rty, *this);
            }
        };

        Cb cb{usedTy, sp, context, ivarIdx, newTy};
        for (const auto* bound : coercionRefs.coercions) {
            usedTy = false;
            auto tL = cloneTyWith(context.crate.types, sp, bound->leftTy, cb);
            auto tR = cloneTyWith(context.crate.types, sp, bound->sourceType(), cb);
            if (!usedTy) {
                continue;
            }
            tL = context.expandAssociatedTypes(sp, mv$(tL));
            tR = context.expandAssociatedTypes(sp, mv$(tR));

            DEBUG(StringView("Check Coerce R") << bound->ruleIdx << StringView(" - ") << bound->leftTy << StringView(" := ") << bound->sourceType());
            DEBUG(StringView("Testing ") << tL << StringView(" := ") << tR);
            const auto response = context.resolve.evaluateCoercionGoal(sp, tL, tR, bound->op);
            if (response.effects.certainty == SolverCertainty::NoSolution) {
                DEBUG(StringView("Solver rejected coercion candidate"));
                return true;
            }
        }

        if (ivarIdx < context.ivarsSized.length() && context.ivarsSized[ivarIdx]) {
            if (context.resolve.typeIsSized(sp, newTy) == SolverCertainty::NoSolution) {
                DEBUG(StringView("Unsized type not valid here"));
                return true;
            }
        }

        for (const auto& endpoint : coercionRefs.endpoints) {
            if (endpoint.direction != SolverCoercionConstraint::Direction::InputIsSource) {
                continue;
            }
            const auto response = context.resolve.evaluateCoercionGoal(sp, endpoint.other, newTy, endpoint.op, endpoint.op == SolverCoercionOp::Unsizing);
            if (response.effects.certainty == SolverCertainty::NoSolution) {
                DEBUG(StringView("Solver rejected coercion target ") << endpoint.other << StringView(" <- ") << newTy);
                return true;
            }
        }

        return false;
    }

    bool finaliseIvarCoercions(Context& context, const IvarCoercionIndex& coercionIndex, unsigned int i, bool finalPhase = false, bool allowIdentityCommit = false, bool allowUnsizingIdentityCommit = false) {
        Span _span;
        const auto& sp = _span;

        const auto* tyL = context.ivars.getType(i);
        const auto& coercionRefs = coercionIndex[i];

        if (!((*tyL).is_Infer() && ((*tyL).as_Infer().index == i))) {
            return false;
        }

        if (coercionRefs.endpoints.empty()) {
            return false;
        }

        {
            bool allowUnsized = !(i < context.ivarsSized.length() ? context.ivarsSized[i] : false);

            std::vector<PossibleType> possibleTys;
            for (const auto& endpoint : coercionRefs.endpoints) {
                if (!coercionEndpointCanDetermineType(context, endpoint)) {
                    continue;
                }
                const auto cls = endpoint.direction == SolverCoercionConstraint::Direction::InputIsDestination
                    ? (endpoint.op == SolverCoercionOp::Coercion ? PossibleType::CoerceFrom : PossibleType::UnsizeFrom)
                    : (endpoint.op == SolverCoercionOp::Coercion ? PossibleType::CoerceTo : PossibleType::UnsizeTo);
                possibleTys.push_back(PossibleType::concrete(cls, context.getType(endpoint.other)));
            }
            const bool hasConcreteSource = std::any_of(possibleTys.begin(), possibleTys.end(), [](const auto& source) {
                return source.isSource() && !source.ty->is_Infer() && !source.ty->is_Diverge();
            });
            const bool hasInferenceBarrier = !coercionRefs.associated.empty()
                || !coercionRefs.revisits.empty()
                || !coercionRefs.advancedRevisits.empty()
                || (coercionRefs.endpoints.empty() && !coercionRefs.coercions.empty());

            if (hasInferenceBarrier && !finalPhase) {
                DEBUG(i << StringView(": unresolved obligation still owns this ivar"));
                return false;
            }

            DEBUG(i << StringView(": possible_tys = ") << possibleTys);
            // TODO: This [ideally] shouldn't happen?
            {
                for (size_t i = 0; i < possibleTys.size(); i++) {
                    if (possibleTys[i].isActive()) {
                        auto it = std::find(possibleTys.begin() + i + 1, possibleTys.end(), possibleTys[i]);
                        if (it != possibleTys.end()) {
                            it->remove();
                        }
                    }
                }
                auto newEnd = std::remove_if(possibleTys.begin(), possibleTys.end(), [](const PossibleType& x) {
                    return !x.isActive();
                });
                DEBUG(i << StringView(": ") << (possibleTys.end() - newEnd) << StringView(" duplicates"));
                possibleTys.resize(newEnd - possibleTys.begin());
            }

            if (!allowUnsized) {
                const auto newEnd = std::remove_if(possibleTys.begin(), possibleTys.end(), [&](const PossibleType& candidate) {
                    return candidate.hasType() && context.resolve.typeIsSized(sp, candidate.ty) == SolverCertainty::NoSolution;
                });
                DEBUG(i << StringView(": ") << (possibleTys.end() - newEnd) << StringView(" unsized possibilities"));
                possibleTys.erase(newEnd, possibleTys.end());
            }

            /* Joint unification is one rule in both phases.  Before
             * finalisation it is forced only when an endpoint occurs on both
             * sides of the coercion: that identity witness already relates
             * the ivar to the endpoint, so unifying every jointly-compatible
             * candidate propagates a constraint rather than selecting a
             * type.  Without such a witness the rule waits for finalisation,
             * when every producer has had a chance to add its endpoint.
             * A diverging source is the bottom of the coercion lattice: it
             * can flow to every destination but is never an equality endpoint
             * and therefore cannot determine the component's type. */
            const auto jointlyUnifyCandidates = [&](const auto& candidates, size_t minimumCandidates = 2) {
                Vector<const PossibleType*> equalityCandidates;
                for (const auto& candidate : candidates) {
                    if (candidate.hasType() && candidate.isSource() && candidate.ty->is_Diverge()) {
                        continue;
                    }
                    equalityCandidates.pushBack(&candidate);
                }
                const auto candidateCount = equalityCandidates.length();
                bool jointlyUnifiable = candidateCount >= minimumCandidates && std::all_of(equalityCandidates.begin(), equalityCandidates.end(), [](const auto* candidate) {
                    return candidate->hasType();
                });
                /* Structural unification of an outer coercion endpoint must
                 * not bind a nested coercion destination before that nested
                 * component resolves its own source.  For example, in the
                 * LUB of Box<_> (fed by a function item) and Box<dyn Fn>,
                 * binding the inner ivar to dyn Fn turns the required Box
                 * unsize into an invalid Box::new::<dyn Fn>.  Let the inner
                 * identity/LUB commit first; the next pass then sees the real
                 * Box<fn item> source and proves the outer unsizing normally.
                 * A direct ivar endpoint remains part of the joint component,
                 * and `!` remains bottom rather than an identity source. */
                const auto hasPendingNestedCoercionSource = [&](const HIRType* candidateType) {
                    if (context.getType(candidateType)->is_Infer()) {
                        return false;
                    }
                    bool pending = false;
                    visitTyWith(candidateType, [&](const HIRType* inner) {
                        const auto* infer = context.getType(inner)->opt_Infer();
                        if (!infer || infer->index == i || infer->index >= coercionIndex.refs.size()) {
                            return false;
                        }
                        pending = std::any_of(coercionIndex[infer->index].endpoints.begin(), coercionIndex[infer->index].endpoints.end(), [&](const auto& endpoint) {
                            return endpoint.direction == SolverCoercionConstraint::Direction::InputIsDestination
                                && coercionEndpointCanDetermineType(context, endpoint)
                                && !context.getType(endpoint.other)->is_Diverge();
                        });
                        return pending;
                    });
                    return pending;
                };
                if (jointlyUnifiable && std::any_of(equalityCandidates.begin(), equalityCandidates.end(), [&](const auto* candidate) {
                    return hasPendingNestedCoercionSource(candidate->ty);
                })) {
                    DEBUG(StringView("Nested coercion source must resolve before structural joint unification"));
                    jointlyUnifiable = false;
                }
                for (size_t lhs = 0; jointlyUnifiable && lhs < candidateCount; lhs++) {
                    for (size_t rhs = lhs + 1; rhs < candidateCount; rhs++) {
                        const auto* left = equalityCandidates[lhs]->ty;
                        const auto* right = equalityCandidates[rhs]->ty;
                        if (context.resolve.probeTypeRelation(sp, left, right) == SolverCertainty::NoSolution || (left->is_NamedFunction() && right->is_NamedFunction() && !context.ivars.typesEqual(left, right))) {
                            jointlyUnifiable = false;
                            break;
                        }
                    }
                }
                if (!jointlyUnifiable) {
                    return false;
                }
                DEBUG(StringView("All remaining coercion candidates jointly unify: ") << FMT_CB(os, for (const auto* candidate : equalityCandidates) os << *candidate << StringView(", ");));
                for (size_t lhs = 0; lhs < candidateCount; lhs++) {
                    for (size_t rhs = lhs + 1; rhs < candidateCount; rhs++) {
                        context.equateTypes(sp, equalityCandidates[lhs]->ty, equalityCandidates[rhs]->ty);
                    }
                    context.equateTypes(sp, tyL, equalityCandidates[lhs]->ty);
                }
                return true;
            };
            Vector<PossibleType> knownCandidates;
            for (const auto& candidate : possibleTys) {
                if (candidate.hasType()) {
                    knownCandidates.pushBack(candidate);
                }
            }
            const bool hasSourceDestinationIdentity = std::any_of(knownCandidates.begin(), knownCandidates.end(), [&](const auto& source) {
                return source.isSource() && std::any_of(knownCandidates.begin(), knownCandidates.end(), [&](const auto& destination) {
                    return !destination.isSource() && destination.hasType() && destination.ty == source.ty;
                });
            });
            if (hasSourceDestinationIdentity) {
                DEBUG(i << StringView(": source/destination identity witness in ") << FMT_CB(os, for (const auto& candidate : knownCandidates) os << candidate << StringView(", ");));
            }
            if (hasSourceDestinationIdentity && jointlyUnifyCandidates(knownCandidates)) {
                return true;
            }

            if (tyL->as_Infer().isLit() && !finalPhase) {
                DEBUG(i << StringView(": Literal ") << tyL);
                return false;
            }

            /* A concrete outgoing Coercion edge is a directed upper bound,
             * not an equality candidate.  In the final effect sweep, one
             * such bound determines the component when a concrete producer
             * exists and every obligation accepts the bound.  This is what
             * carries an actual expected-result coercion into an expression
             * LUB: `fn item -> match result -> fn pointer`, for example,
             * must select the pointer before the later identity phase can
             * freeze the result at the function-item type.  Unsizing edges
             * are deliberately excluded: the pointee of `Box<T> ->
             * Box<dyn Trait>` remains T while the outer coercion performs the
             * unsizing. */
            const HIRType* directedDestination = nullptr;
            bool uniqueDirectedDestination = true;
            for (const auto& endpoint : coercionRefs.endpoints) {
                if (endpoint.direction != SolverCoercionConstraint::Direction::InputIsSource || endpoint.op != SolverCoercionOp::Coercion) {
                    continue;
                }
                const auto* type = context.getType(endpoint.other);
                if (type->is_Infer()) {
                    continue;
                }
                if (directedDestination && !context.ivars.typesEqual(directedDestination, type)) {
                    uniqueDirectedDestination = false;
                    break;
                }
                directedDestination = type;
            }
            if (finalPhase
                && hasConcreteSource
                && uniqueDirectedDestination
                && directedDestination
                && !coercionCandidateIsInvalid(sp, context, coercionRefs, tyL, directedDestination)) {
                DEBUG(i << StringView(": Unique concrete coercion destination bounds the component: ") << directedDestination);
                context.equateTypes(sp, tyL, directedDestination);
                return true;
            }

            /* In the final effect sweep, jointly-unifiable source and
             * destination endpoints are one equality component.  Keep ivar
             * endpoints in this check: removing them first would turn
             * `S -> dest -> D` into a spurious single-source identity case.
             * This is also the general form of the old exact-endpoint case;
             * no endpoint is preferred or ranked. */
            if (finalPhase && jointlyUnifyCandidates(possibleTys)) {
                return true;
            }

            /* Identity commit is a finalisation rule, not an inference
             * heuristic.  Once all ordinary revisits and obligations have
             * stabilised and a separate final effect-only sweep found nothing
             * more to propagate, a coercion destination with no other
             * constraints takes its source type: dest := source.  Applying
             * this before that point would be unsound because a later
             * destination constraint could still require a real coercion.
             * Pending associated/revisit obligations are not alternative
             * types: identity propagates the source into those obligations,
             * and coercionCandidateIsInvalid must prove the coercion edges.
             * An outgoing coercion is propagation, not a competing constraint:
             * it receives the committed source type on the next pass.  If its
             * destination already makes that type impossible,
             * coercionCandidateIsInvalid rejects the commit.
             * Deferred destination effects whose source input is still open
             * are not competing choices; coercionCandidateIsInvalid must
             * nevertheless prove them before the commit. */
            Vector<const PossibleType*> identityCandidates;
            for (const auto& candidate : possibleTys) {
                if (candidate.hasType() && !(candidate.isSource() && candidate.ty->is_Diverge())) {
                    identityCandidates.pushBack(&candidate);
                }
            }
            if (finalPhase
                && allowIdentityCommit
                && std::none_of(coercionRefs.endpoints.begin(), coercionRefs.endpoints.end(), [&](const auto& endpoint) {
                    return endpoint.direction == SolverCoercionConstraint::Direction::InputIsSource
                        && coercionEndpointCanDetermineType(context, endpoint);
                })
                && identityCandidates.length() == 1
                && identityCandidates[0]->isSource()
                && (identityCandidates[0]->isCoerce() || allowUnsizingIdentityCommit)
                && !isResultOfPendingRevisit(context, tyL)
                && !coercionCandidateIsInvalid(sp, context, coercionRefs, tyL, identityCandidates[0]->ty)) {
                DEBUG(i << StringView(": Final unconstrained coercion destination takes its source type: ") << identityCandidates[0]->ty);
                context.equateTypes(sp, tyL, identityCandidates[0]->ty);
                return true;
            }

            // - TODO: Should this also remove &_ types? (maybe not, as they give information about borrow classes)
            size_t nIvars;
            size_t nSrcIvars;
            bool possiblyDiverge = false;
            {
                nSrcIvars = 0;
                auto newEnd = std::remove_if(possibleTys.begin(), possibleTys.end(), [&](const PossibleType& ent) {
                    // TODO: Should this remove Unbound associated types too?
                    if ((ent.ty)->is_Infer()) {
                        if (ent.isSource()) {
                            nSrcIvars += 1;
                        }
                        return true;
                    } else if ((ent.ty)->is_Diverge()) {
                        possiblyDiverge = true;
                        return true;
                    } else {
                        return false;
                    }
                });
                nIvars = possibleTys.end() - newEnd;
                possibleTys.erase(newEnd, possibleTys.end());
            }
            DEBUG(nIvars << StringView(" ivars (") << nSrcIvars << StringView(" src)"));
            const auto isFunctionSource = [](const PossibleType& possible) {
                return possible.isSource() && (((*possible.ty).is_NodeType() && ((*possible.ty).as_NodeType().is_Closure())) || possible.ty->is_NamedFunction() || possible.ty->is_Function());
            };
            const auto functionSourceCount = std::count_if(possibleTys.begin(), possibleTys.end(), isFunctionSource);
            if (functionSourceCount >= 2 && (finalPhase || (!hasInferenceBarrier && nSrcIvars == 0)) && std::all_of(possibleTys.begin(), possibleTys.end(), [&](const auto& possible) {
                return !possible.isSource() || isFunctionSource(possible);
            })) {
                /* A function pointer among the sources is already the join of this
                   lattice - a closure and a function item each coerce to one - so it is
                   the target, and the other sources take their signature from it.  Two
                   pointers that differ have no join, and the rule does not apply. */
                const HIRType* pointerSource = nullptr;
                bool pointerSourceUnique = true;
                for (const auto& possible : possibleTys) {
                    if (!possible.isSource() || !possible.ty->is_Function()) {
                        continue;
                    }
                    if (pointerSource && !context.ivars.typesEqual(pointerSource, possible.ty)) {
                        pointerSourceUnique = false;
                        break;
                    }
                    pointerSource = possible.ty;
                }
                const HIRType* newTy = pointerSource;
                if (pointerSource == nullptr) {
                    std::optional<HIRTypeDataFunctionPointer> target;
                    for (const auto& possible : possibleTys) {
                        if (!possible.isSource()) {
                            continue;
                        }
                        HIRTypeDataFunctionPointer candidate;
                        if (const auto* function = possible.ty->opt_NamedFunction()) {
                            candidate = function->decay(context.crate.types, sp);
                        } else if (const auto* closure = ((*possible.ty).is_NodeType() ? ((*possible.ty).as_NodeType().opt_Closure()) : nullptr)) {
                            candidate = HIRTypeDataFunctionPointer{false, false, RcString::newInterned(ABI_RUST), (*closure)->returnType, {}};
                            for (const auto& argument : (*closure)->args) {
                                candidate.argTypes.pushBack(argument.second);
                            }
                        } else {
                            BUG(sp, StringView(""));
                        }

                        if (target) {
                            target->isUnsafe |= candidate.isUnsafe;
                        } else {
                            target = std::move(candidate);
                        }
                    }
                    newTy = context.crate.types.function(std::move(*target));
                }
                if (newTy && pointerSourceUnique && !coercionCandidateIsInvalid(sp, context, coercionRefs, tyL, newTy)) {
                    context.equateTypes(sp, tyL, newTy);
                    return true;
                }
            }

            /* Pointer LUB is the coercion lattice, not a restrictiveness
             * ranking.  For one jointly-unifiable pointee T its form chains are
             *
             *     &mut T <= &T <= *const T
             *     &mut T <= *mut T <= *const T
             *
             * All source forms must be on one chain.  In particular, &T and
             * *mut T are incomparable here and leave the ivar ambiguous.
             * Pointees are unified independently.  If they do not unify as
             * written, each borrow pointee has one directed coercion chain
             * through proven Deref::Target steps (including [T; N] <= [T]).
             * The pointee LUB is the unique minimal common member of those
             * chains.  No common member, or incomparable minimal common
             * members, is ambiguity.  This applies source-autoderef effects
             * before the form LUB; it does not order arbitrary types. */
            if (finalPhase || (!hasInferenceBarrier && nSrcIvars == 0)) {
                struct PointerLubSource {
                    PointerCoercionShape shape;
                    ThinVector<const HIRType*> pointeeChain;
                    size_t selectedPointee = 0;
                };
                ThinVector<PointerLubSource> sourcePointers;
                std::optional<PointerCoercionForm> pointerLub;
                bool isPointerChain = true;
                for (const auto& possible : possibleTys) {
                    if (!possible.isSource()) {
                        continue;
                    }
                    const auto shape = pointerCoercionShape(possible.ty);
                    if (!shape) {
                        isPointerChain = false;
                        break;
                    }
                    PointerLubSource source{*shape};
                    source.pointeeChain.push_back(context.getType(shape->inner));
                    sourcePointers.push_back(std::move(source));
                    if (pointerLub) {
                        pointerLub = pointerChainLub(*pointerLub, shape->form);
                        if (!pointerLub) {
                            isPointerChain = false;
                            break;
                        }
                    } else {
                        pointerLub = shape->form;
                    }
                }
                const HIRType* pointeeLub = nullptr;
                bool pointeesUnifyDirectly = true;
                for (const auto& source : sourcePointers) {
                    if (!pointeeLub) {
                        pointeeLub = source.pointeeChain.front();
                        continue;
                    }
                    const auto* left = context.getType(pointeeLub);
                    const auto* right = source.pointeeChain.front();
                    if (left->tag() != right->tag() || context.resolve.probeTypeRelation(sp, left, right) == SolverCertainty::NoSolution) {
                        pointeesUnifyDirectly = false;
                        break;
                    }
                }

                if (isPointerChain && sourcePointers.size() >= 2 && !pointeesUnifyDirectly) {
                    const auto pointeesRelate = [&](const HIRType* left, const HIRType* right) {
                        left = context.getType(left);
                        right = context.getType(right);
                        return left->tag() == right->tag()
                            && context.resolve.probeTypeRelation(sp, left, right) != SolverCertainty::NoSolution;
                    };
                    for (auto& source : sourcePointers) {
                        bool chainTerminated = false;
                        for (unsigned depth = 0; depth < context.resolve.board().settings->recursionLimit; depth++) {
                            const auto step = context.resolve.autoderefStep(sp, source.pointeeChain.back());
                            if (step.result == TraitResolution::AutoderefResult::NoMatch) {
                                chainTerminated = true;
                                break;
                            }
                            if (step.result == TraitResolution::AutoderefResult::Ambiguous
                                || std::any_of(source.pointeeChain.begin(), source.pointeeChain.end(), [&](const auto* previous) {
                                    return pointeesRelate(previous, step.target);
                                })) {
                                isPointerChain = false;
                                break;
                            }
                            source.pointeeChain.push_back(context.getType(step.target));
                        }
                        if (!chainTerminated) {
                            isPointerChain = false;
                        }
                        if (!isPointerChain) {
                            break;
                        }
                    }

                    struct CommonPointee {
                        const HIRType* type;
                        Vector<size_t> positions;
                    };
                    ThinVector<CommonPointee> commonPointees;
                    if (isPointerChain) {
                        for (const auto& proposedBy : sourcePointers) {
                            for (const auto* proposed : proposedBy.pointeeChain) {
                                if (std::any_of(commonPointees.begin(), commonPointees.end(), [&](const auto& existing) {
                                    return pointeesRelate(existing.type, proposed);
                                })) {
                                    continue;
                                }
                                CommonPointee common{proposed};
                                for (const auto& source : sourcePointers) {
                                    const auto found = std::find_if(source.pointeeChain.begin(), source.pointeeChain.end(), [&](const auto* reachable) {
                                        return pointeesRelate(reachable, proposed);
                                    });
                                    if (found == source.pointeeChain.end()) {
                                        common.positions.clear();
                                        break;
                                    }
                                    common.positions.pushBack(static_cast<size_t>(found - source.pointeeChain.begin()));
                                }
                                if (!common.positions.empty()) {
                                    commonPointees.push_back(std::move(common));
                                }
                            }
                        }
                    }

                    std::optional<size_t> minimalCommon;
                    for (size_t candidate = 0; candidate < commonPointees.size(); candidate++) {
                        bool hasStrictlyLowerCommon = false;
                        for (size_t other = 0; other < commonPointees.size(); other++) {
                            if (candidate == other) {
                                continue;
                            }
                            bool noLater = true;
                            bool earlierSomewhere = false;
                            for (size_t source = 0; source < sourcePointers.size(); source++) {
                                noLater &= commonPointees[other].positions[source] <= commonPointees[candidate].positions[source];
                                earlierSomewhere |= commonPointees[other].positions[source] < commonPointees[candidate].positions[source];
                            }
                            if (noLater && earlierSomewhere) {
                                hasStrictlyLowerCommon = true;
                                break;
                            }
                        }
                        if (!hasStrictlyLowerCommon) {
                            if (minimalCommon) {
                                isPointerChain = false;
                                break;
                            }
                            minimalCommon = candidate;
                        }
                    }
                    if (!minimalCommon) {
                        isPointerChain = false;
                    } else if (isPointerChain) {
                        pointeeLub = commonPointees[*minimalCommon].type;
                        for (size_t source = 0; source < sourcePointers.size(); source++) {
                            sourcePointers[source].selectedPointee = commonPointees[*minimalCommon].positions[source];
                        }
                    }
                }
                if (isPointerChain && sourcePointers.size() >= 2) {
                    const HIRType* newTy = nullptr;
                    switch (*pointerLub) {
                        case PointerCoercionForm::MutableBorrow:
                            newTy = context.crate.types.borrow(HIRBorrowType::Unique, pointeeLub);
                            break;
                        case PointerCoercionForm::SharedBorrow:
                            newTy = context.crate.types.borrow(HIRBorrowType::Shared, pointeeLub);
                            break;
                        case PointerCoercionForm::MutableRaw:
                            newTy = context.crate.types.pointer(HIRBorrowType::Unique, pointeeLub);
                            break;
                        case PointerCoercionForm::ConstRaw:
                            newTy = context.crate.types.pointer(HIRBorrowType::Shared, pointeeLub);
                            break;
                    }
                    if (!coercionCandidateIsInvalid(sp, context, coercionRefs, tyL, newTy)) {
                        DEBUG(StringView("Pointer coercion LUB: ") << newTy);
                        for (const auto& source : sourcePointers) {
                            context.equateTypes(sp, pointeeLub, source.pointeeChain[source.selectedPointee]);
                        }
                        context.equateTypes(sp, tyL, newTy);
                        return true;
                    }
                }
            }

            DEBUG(i << StringView(": possible_tys = ") << possibleTys);
            for (auto it = possibleTys.begin(); it != possibleTys.end();) {
                bool removeOption = false;
                if (it->ty == tyL) {
                    removeOption = true;
                } else {
                }

                // TODO: Ivars have been removed, this sort of check should be moved elsewhere.
                if (!removeOption && tyL->as_Infer().tyClass == HIRInferClass::Integer) {
                    if (const auto* te = (it->ty)->opt_Primitive()) {
                    } else if (const auto* te = (it->ty)->opt_Path()) {
                    } else if (const auto* te = (it->ty)->opt_Infer()) {
                    } else {
                        removeOption = true;
                    }
                }

                it = (removeOption ? possibleTys.erase(it) : it + 1);
            }
            DEBUG(i << StringView(": possible_tys = ") << possibleTys);
            for (auto it = possibleTys.begin(); it != possibleTys.end();) {
                const bool removeOption = !(it->ty)->is_Infer() && coercionCandidateIsInvalid(sp, context, coercionRefs, tyL, it->ty);
                if (removeOption) {
                    DEBUG(StringView("- Remove ") << *it << StringView(" due to bounds"));
                }
                it = (removeOption ? possibleTys.erase(it) : it + 1);
            }

            /* Candidate validation is part of the coercion rule, not a
             * ranking step: an endpoint which is NoSolution for any pending
             * coercion is not viable.  Once those endpoints are removed, all
             * remaining endpoints form one equality component exactly when
             * they jointly unify.  This ordering matters for autoderef: both
             * the identity pointee and a later Deref::Target can be emitted,
             * while another argument rejects the identity pointee and relates
             * the target to the same ivar. */
            if (finalPhase && jointlyUnifyCandidates(possibleTys)) {
                return true;
            }

            /* Post-validation form of the final identity rule: if solver
             * validation has proved every other concrete endpoint
             * impossible, one remaining source is not a ranked winner.  It
             * is the only coercion effect left, so the unconstrained
             * destination takes that source type.  Do not discard unresolved
             * ivar endpoints to manufacture this case. */
            if (finalPhase
                && allowIdentityCommit
                && nIvars == 0
                && possibleTys.size() == 1
                && possibleTys[0].isSource()
                && (possibleTys[0].isCoerce() || allowUnsizingIdentityCommit)
                && !isResultOfPendingRevisit(context, tyL)) {
                DEBUG(i << StringView(": Final sole solver-viable source becomes the coercion destination: ") << possibleTys[0].ty);
                context.equateTypes(sp, tyL, possibleTys[0].ty);
                return true;
            }

            const bool hasDeferredDestination = std::any_of(coercionRefs.endpoints.begin(), coercionRefs.endpoints.end(), [](const auto& endpoint) {
                return endpoint.direction == SolverCoercionConstraint::Direction::InputIsSource;
            });
            DEBUG(i << StringView(": possible_tys = {") << possibleTys << StringView("} (") << nSrcIvars << StringView(" src ivars, possibly_diverge=") << possiblyDiverge << StringView(", deferred_destination=") << hasDeferredDestination << StringView(")"));
            /* Never-type fallback is the last language fallback in this
             * component.  A resolved non-bottom source may still arrive via
             * an ivar edge during either identity propagation phase. */
            const bool hasUnresolvedOwner = !coercionRefs.advancedRevisits.empty();
            if (finalPhase && allowUnsizingIdentityCommit && !hasUnresolvedOwner && nSrcIvars == 0 && possibleTys.empty() && possiblyDiverge && !hasDeferredDestination && context.crate.edition < ASTEdition::Rust2024) {
                auto unit = context.crate.types.unit();
                if (!coercionCandidateIsInvalid(sp, context, coercionRefs, tyL, unit)) {
                    DEBUG(StringView("Possibly `!` and no other options - never-type fallback to `()`"));
                    context.recordNeverFallback(i);
                    context.equateTypes(sp, tyL, unit);
                    return true;
                }
            }
        }

        return false;
    }

    inline HIRSimplePath getRuleParentPath(const HIRSimplePath& sp) {
        return sp.parent();
    }

    inline HIRGenericPath getRuleParentPath(const HIRGenericPath& gp) {
        return HIRGenericPath(gp.path.parent(), gp.params.clone());
    }

    bool visitCallPopulateCacheUfcsInherent(Context& context, const Span& sp, HIRPath& path, HIRExprCallCache& cache, const HIRFunction*& fcnPtr, const HIRTypeImpl* selectedImpl);

    void populateDefaults(const Span& sp, Context& context, const MonomorphStatePtr& ms, const HIRGenericParams& paramDefs, HIRPathParams& params) {
        for (size_t i = 0; i < paramDefs.types.size(); i++) {
            const auto& ty = params.types[i];
            const auto& typ = paramDefs.types[i];
            if (const auto* te = ty->opt_Infer()) {
                if (!typ.defaultValue->is_Infer()) {
                    auto defTy = ms.monomorphType(sp, typ.defaultValue);
                    DEBUG(StringView("Added default for ") << ty << StringView(": ") << defTy);
                    context.addIvarDefault(sp, te->index, defTy);
                }
            }
        }
    }

    template <typename T>
    void fix_param_count_(const Span& sp, Context& context, const HIRType* selfTy, bool useDefaults, const T& path, const HIRGenericParams& paramDefs, HIRPathParams& params) {
        if (params.types.size() == paramDefs.types.size()) {
        } else if (params.types.size() > paramDefs.types.size()) {
            while (params.types.size() > paramDefs.types.size() && params.values.size() < paramDefs.values.size() && params.types.back()->is_Infer()) {
                params.types.pop_back();
                params.values.push_back({});
                context.ivars.addIvars(params.values.back());
            }
            if (params.types.size() > paramDefs.types.size()) {
                ERROR(sp, E0000, StringView("Too many type parameters passed to ") << path);
            }
        } else {
            while (params.types.size() < paramDefs.types.size()) {
                const auto& typ = paramDefs.types[params.types.size()];
                if (useDefaults) {
                    if (typ.defaultValue->is_Infer()) {
                        ERROR(sp, E0000, StringView("Omitted type parameter with no default in ") << path);
                    } else if (monomorphiseTypeNeeded(typ.defaultValue)) {
                        auto cb = MonomorphStatePtr(context.crate.types, selfTy, nullptr, nullptr);
                        params.types.push_back(cb.monomorphType(sp, typ.defaultValue));
                    } else {
                        params.types.push_back(typ.defaultValue);
                    }
                } else {
                    params.types.push_back(context.ivars.newIvarTr());
                    // TODO: It's possible that the default could be added using `context.possible_equate_type_def` to give inferrence a fallback
                }
            }
        }

        if (params.values.size() == paramDefs.values.size()) {
        } else if (params.values.size() > paramDefs.values.size()) {
            ERROR(sp, E0000, StringView("Too many const parameters passed to ") << path);
        } else {
            while (params.values.size() < paramDefs.values.size()) {
                params.values.push_back({});
                context.ivars.addIvars(params.values.back());
            }
        }
    }

    void fixParamCount(const Span& sp, Context& context, const HIRType* selfTy, bool useDefaults, const HIRPath& path, const HIRGenericParams& paramDefs, HIRPathParams& params) {
        fix_param_count_(sp, context, selfTy, useDefaults, path, paramDefs, params);
    }

    void fixParamCount(const Span& sp, Context& context, const HIRType* selfTy, bool useDefaults, const HIRGenericPath& path, const HIRGenericParams& paramDefs, HIRPathParams& params) {
        fix_param_count_(sp, context, selfTy, useDefaults, path, paramDefs, params);
    }

    void applyBoundsAsRulesTrait(Context& context, const Span& sp, const HIRType* realType, const HIRTraitPath& traitPath) {
        if (traitPath.typeBounds.size() == 0) {
            context.addTraitBound(sp, realType, traitPath.path.path, traitPath.path.params.clone());
        }

        for (const auto& assoc : traitPath.typeBounds) {
            context.equateTypesAssoc(sp, assoc.second.type, assoc.second.sourceTrait.path, assoc.second.sourceTrait.params.clone(), realType, assoc.first.c_str(), assoc.second.atyParams.clone(), false);
        }
        for (const auto& assoc : traitPath.traitBounds) {
            auto atyTy = context.crate.types.path(HIRPath(realType, assoc.second.sourceTrait.clone(), assoc.first, assoc.second.atyParams.clone()), {});
            for (const auto& tr : assoc.second.traits) {
                applyBoundsAsRulesTrait(context, sp, atyTy, tr);
            }
        }
    }

    void applyBoundsAsRules(Context& context, const Span& sp, const HIRGenericParams& paramsDef, const Monomorphiser& ms, bool isImplLevel) {
        TRACE_FUNCTION;
        for (const auto& bound : paramsDef.bounds) {
            switch (bound.tag()) {
                case HIRGenericBound::TAG_TraitBound: {
                    auto& be = bound.as_TraitBound();
                    DEBUG(StringView("Bound ") << be.type << StringView(":  ") << be.trait);
                    auto realType = ms.monomorphType(sp, be.type);
                    auto realTrait = ms.monomorphTraitpath(sp, be.trait, false);
                    DEBUG(StringView("= (") << realType << StringView(": ") << realTrait << StringView(")"));
                    applyBoundsAsRulesTrait(context, sp, realType, realTrait);
                    break;
                }
                case HIRGenericBound::TAG_TypeEquality: {
                    auto& be = bound.as_TypeEquality();
                    auto realTypeLeft = context.expandAssociatedTypes(sp, ms.monomorphType(sp, be.type));
                    auto realTypeRight = context.expandAssociatedTypes(sp, ms.monomorphType(sp, be.otherType));
                    context.equateTypes(sp, realTypeLeft, realTypeRight);
                    break;
                }
            }
        }

        for (size_t i = 0; i < paramsDef.types.size(); i++) {
            if (paramsDef.types[i].isSized) {
                const HIRType* ty = context.crate.types.generic("", (isImplLevel ? 0 : 256) + i);
                context.requireSized(sp, ms.getType(Span(), ty->as_Generic()));
            }
        }
    }

    bool visitCallPopulateCacheUfcsInherent(Context& context, const Span& sp, HIRPath& path, HIRExprCallCache& cache, const HIRFunction*& fcnPtr, const HIRTypeImpl* selectedImpl) {
        auto& e = path.data.as_UfcsInherent();
        context.selectWellFormed(sp, e.type);
        auto lookupType = context.revealOpaqueTypes(e.type);
        lookupType = context.expandAssociatedTypes(sp, mv$(lookupType));
        e.type = lookupType;

        const HIRTypeImpl* implPtr = selectedImpl;
        HIRPathParams selectedParams = e.implParams.clone();
        SolverCertainty certainty;
        if (selectedImpl) {
            certainty = SolverCertainty::Proven;
        } else {
            auto selection = context.resolve.selectInherentImpl(sp, lookupType, e.item, InherentItemKind::Method, &e.implParams);
            certainty = selection.certainty;
            implPtr = selection.impl;
        }
        if (certainty == SolverCertainty::Ambiguous) {
            return false;
        }
        if (certainty == SolverCertainty::NoSolution || !implPtr) {
            ERROR(sp, E0000, StringView("Failed to locate function ") << path);
        }
        DEBUG(StringView("Found impl") << implPtr->params.fmtArgs() << StringView(" ") << implPtr->type);
        auto method = implPtr->methods.find(e.item);
        ASSERT_BUG(sp, method != implPtr->methods.end(), StringView("Selected inherent impl has no method ") << e.item);
        fcnPtr = &method->second.data;
        fixParamCount(sp, context, e.type, false, path, fcnPtr->params, e.params);
        cache.fcnParams = &fcnPtr->params;

        auto& implParams = e.implParams;
        implParams = std::move(selectedParams);
        ASSERT_BUG(sp, context.resolve.relateInherentImplHeader(sp, *implPtr, lookupType, implParams) != Unifier::Outcome::Mismatch, StringView("Selected inherent impl no longer matches ") << lookupType);

        const auto& fcnParams = e.params;
        // TODO: Use a copy of `MonomorphStatePtr` that calls `context.get_type`
        cache.monomorph.reset(new MonomorphStatePtr(context.crate.types, e.type, &implParams, &fcnParams));

        applyBoundsAsRules(context, sp, implPtr->params, *cache.monomorph, /*is_impl_level=*/true);

        {
            const HIRType* tmp;
            const auto* implTyM = cache.monomorph->maybeMonomorphType(sp, implPtr->type);

            context.equateTypes(sp, e.type, implTyM);
        }
        e.type = context.revealOpaqueTypes(e.type);

        return true;
    }

    bool resolveInherentPathValue(Context& context, HIRExprNodePathValue& node) {
        const auto& sp = node.span();
        auto& path = node.path;
        auto& inherent = path.data.as_UfcsInherent();
        context.selectWellFormed(sp, inherent.type);
        auto lookupType = context.revealOpaqueTypes(inherent.type);
        lookupType = context.expandAssociatedTypes(sp, std::move(lookupType));
        inherent.type = lookupType;

        auto selection = context.resolve.selectInherentImpl(sp, lookupType, inherent.item, InherentItemKind::Value, &inherent.implParams);
        if (selection.certainty == SolverCertainty::Ambiguous) {
            return false;
        }
        if (selection.certainty == SolverCertainty::NoSolution || !selection.impl) {
            ERROR(sp, E0000, StringView("Failed to locate associated value ") << path);
        }

        const auto& impl = *selection.impl;
        const HIRFunction* function = nullptr;
        const HIRConstant* constant = nullptr;
        if (auto method = impl.methods.find(inherent.item); method != impl.methods.end()) {
            function = &method->second.data;
        } else {
            constant = &impl.constants.at(inherent.item).data;
        }
        if (function) {
            fixParamCount(sp, context, inherent.type, false, path, function->params, inherent.params);
        } else {
            fixParamCount(sp, context, inherent.type, false, path, constant->params, inherent.params);
        }

        auto& implParams = inherent.implParams;
        ASSERT_BUG(sp, context.resolve.relateInherentImplHeader(sp, impl, lookupType, implParams) != Unifier::Outcome::Mismatch, StringView("Selected inherent value impl no longer matches ") << lookupType);

        auto monomorph = MonomorphStatePtr(context.crate.types, inherent.type, &implParams, &inherent.params);
        auto implType = monomorph.monomorphType(sp, impl.type);
        context.equateTypes(sp, inherent.type, implType);
        inherent.type = context.revealOpaqueTypes(inherent.type);
        applyBoundsAsRules(context, sp, impl.params, monomorph, true);

        if (function) {
            applyBoundsAsRules(context, sp, function->params, monomorph, false);
            auto type = context.crate.types.intern(HIRType::make_NamedFunction({path.clone(), function}));
            context.equateTypes(sp, node.resType, type);
        } else {
            const HIRType* temporary;
            const auto* type = monomorph.maybeMonomorphType(sp, constant->type);
            applyBoundsAsRules(context, sp, constant->params, monomorph, false);
            context.equateTypes(sp, node.resType, type);
        }
        return true;
    }
}

void Context::equateTypes(const Span& sp, const HIRType* li, const HIRType* ri) {
    const auto& liRes = this->ivars.getType(li);
    const auto& riRes = this->ivars.getType(ri);
    if (li == ri || liRes == riRes || liRes->equalsIgnoringRegions(riRes)) {
        DEBUG(li << StringView(" == ") << ri);
        return;
    }

    TRACE_FUNCTION_F(li << StringView(" == ") << ri);
    ASSERT_BUG(sp, !typeContainsImplPlaceholder(crate.types, ri), StringView("Type contained an impl placeholder parameter - ") << ri);
    ASSERT_BUG(sp, !typeContainsImplPlaceholder(crate.types, li), StringView("Type contained an impl placeholder parameter - ") << li);

    const auto& lT = this->expandAssociatedTypes(sp, this->ivars.getType(li));
    const auto& rT = this->expandAssociatedTypes(sp, this->ivars.getType(ri));

    if (lT->is_Diverge() && !rT->is_Infer()) {
        return;
    }
    if (rT->is_Diverge() && !lT->is_Infer()) {
        return;
    }
    equateTypesInner(sp, lT, rT);
}

void Context::equateTypesInner(const Span& sp, const HIRType* li, const HIRType* ri) {
    const auto& liRes = this->ivars.getType(li);
    const auto& riRes = this->ivars.getType(ri);
    if (li == ri || liRes == riRes || liRes->equalsIgnoringRegions(riRes)) {
        return;
    }

    const auto& lT = this->expandAssociatedTypes(sp, this->ivars.getType(li));
    const auto& rT = this->expandAssociatedTypes(sp, this->ivars.getType(ri));
    if (lT == rT || lT->equalsIgnoringRegions(rT)) {
        return;
    }

    const auto* lPath = lT->opt_Path();
    const auto* rPath = rT->opt_Path();
    const auto* lProjection = lPath ? lPath->path.data.opt_UfcsKnown() : nullptr;
    const auto* rProjection = rPath ? rPath->path.data.opt_UfcsKnown() : nullptr;
    const bool lRigidProjection = lPath && (lPath->binding.is_Unbound() || lPath->binding.is_Opaque());
    const bool rRigidProjection = rPath && (rPath->binding.is_Unbound() || rPath->binding.is_Opaque());
    const auto typesMayRelate = [&](const HIRType* left, const HIRType* right) {
        if (left == right || left->equalsIgnoringRegions(right)) {
            return true;
        }
        return this->ivars.getType(left)->is_Infer() || this->ivars.getType(right)->is_Infer();
    };
    const auto paramsMayRelate = [&](const HIRPathParams& left, const HIRPathParams& right) {
        if (left.types.size() != right.types.size() || left.values.size() != right.values.size()) {
            return false;
        }
        for (size_t i = 0; i < left.types.size(); i++) {
            if (left.types[i] != right.types[i] && !left.types[i]->equalsIgnoringRegions(right.types[i])) {
                return false;
            }
        }
        for (size_t i = 0; i < left.values.size(); i++) {
            if (left.values[i] != right.values[i]) {
                return false;
            }
        }
        return true;
    };
    if (lRigidProjection && rRigidProjection && lProjection && rProjection && lProjection->trait.path == rProjection->trait.path && lProjection->item == rProjection->item && (this->ivars.getType(lProjection->type)->is_Infer() || this->ivars.getType(rProjection->type)->is_Infer()) && typesMayRelate(lProjection->type, rProjection->type) && paramsMayRelate(lProjection->trait.params, rProjection->trait.params) && paramsMayRelate(lProjection->params, rProjection->params)) {
        struct DeferredRigidProjectionSelf final: Revisitor {
            Span sp;
            const HIRType* leftAlias;
            const HIRType* rightAlias;
            const HIRType* leftSelf;
            const HIRType* rightSelf;

            DeferredRigidProjectionSelf(Span sp, const HIRType* leftAlias, const HIRType* rightAlias, const HIRType* leftSelf, const HIRType* rightSelf)
                : sp(mv$(sp))
                , leftAlias(leftAlias)
                , rightAlias(rightAlias)
                , leftSelf(leftSelf)
                , rightSelf(rightSelf)
            {
            }

            const Span& span() const override {
                return sp;
            }

            void fmt(ZeroCopyOutput& os) const override {
                os << StringView("Deferred rigid projection self ") << leftSelf << StringView(" = ") << rightSelf;
            }

            bool revisit(Context& context, bool isFallback) override {
                const auto left = context.ivars.getType(leftSelf);
                const auto right = context.ivars.getType(rightSelf);
                if (!left->is_Infer() && !right->is_Infer()) {
                    context.equateTypes(sp, leftAlias, rightAlias);
                    return true;
                }
                if (!isFallback) {
                    return false;
                }
                context.equateTypes(sp, left, right);
                return true;
            }
        };

        this->addRevisitAdv(box$((DeferredRigidProjectionSelf(sp, lT, rT, lProjection->type, rProjection->type))));
        return;
    }

    auto bindInferToAlias = [&](const HIRType* infer, const HIRType* alias) {
        const auto* inferData = infer->opt_Infer();
        if (!inferData || inferData->isLit() || this->resolve.typeContainsIvars(alias) || visitTyWith(alias, [&](const HIRType* inner) {
            return inner == infer;
        })) {
            return false;
        }
        const auto ivarIdx = inferData->index;
        if (ivarIdx < ivarsSized.length() && ivarsSized[ivarIdx]) {
            this->requireSized(sp, alias);
        }
        this->ivars.setIvarTo(ivarIdx, alias);
        return true;
    };

    if (const auto* rE = rT->opt_Path()) {
        if (const auto* rpe = rE->path.data.opt_UfcsKnown()) {
            if (rE->binding.is_Unbound()) {
                if (bindInferToAlias(lT, rT)) {
                    return;
                }
                this->equateTypesAssoc(sp, lT, rpe->trait.path, rpe->trait.params.clone(), rpe->type, rpe->item.c_str(), rpe->params, false);
                return;
            }
        }
    }
    if (const auto* lE = lT->opt_Path()) {
        if (const auto* lpe = lE->path.data.opt_UfcsKnown()) {
            if (lE->binding.is_Unbound()) {
                if (bindInferToAlias(rT, lT)) {
                    return;
                }
                this->equateTypesAssoc(sp, rT, lpe->trait.path, lpe->trait.params.clone(), lpe->type, lpe->item.c_str(), lpe->params, false);
                return;
            }
        }
    }

    if (const auto* lErased = lT->opt_ErasedType()) {
        if (const auto* rErased = rT->opt_ErasedType()) {
            const auto* lAlias = lErased->inner.opt_Alias();
            const auto* rAlias = rErased->inner.opt_Alias();
            if (lAlias && rAlias && lAlias->inner == rAlias->inner) {
                ASSERT_BUG(sp, lAlias->params.types.size() == rAlias->params.types.size(), StringView("Opaque alias type argument count mismatch"));
                ASSERT_BUG(sp, lAlias->params.values.size() == rAlias->params.values.size(), StringView("Opaque alias const argument count mismatch"));
                for (size_t i = 0; i < lAlias->params.types.size(); i++) {
                    equateTypesInner(sp, lAlias->params.types[i], rAlias->params.types[i]);
                }
                for (size_t i = 0; i < lAlias->params.values.size(); i++) {
                    equateValues(sp, lAlias->params.values[i], rAlias->params.values[i]);
                }
                return;
            }
        }
    }

    auto equateErasedAlias = [&](const HIRTypeDataErasedType& erased, const auto& alias, const HIRType* hiddenType) {
        if (!resolve.isOpaqueAliasDefiningScope(*alias.inner)) {
            return false;
        }

        auto inserted = this->erasedTypeAliases.insert(std::make_pair(alias.inner.get(), Context::TaitEntry{alias.params, hiddenType}));
        if (!inserted.second) {
            equateTypesInner(sp, inserted.first->second.ourType, hiddenType);
            return true;
        }

        struct MonomorphErasedSelf: MonomorphiserNop {
            const HIRType* hiddenType;

            MonomorphErasedSelf(HIRTypeInterner& types, const HIRType* hiddenType)
                : MonomorphiserNop(types)
                , hiddenType(hiddenType)
            {
            }

            const HIRType* getType(const Span&, const HIRGenericRef& type) const override {
                if (type.binding == GENERICErasedSelf) {
                    return hiddenType;
                }
                return types.generic(type.name, type.binding);
            }
        } monomorph{crate.types, hiddenType};

        for (const auto& trait : erased.traits) {
            auto traitMono = monomorph.monomorphTraitpath(sp, trait, false);
            if (traitMono.typeBounds.empty()) {
                equateTypesAssoc(sp, crate.types.infer(), traitMono.path.path, traitMono.path.params.clone(), hiddenType, "", {}, false);
                continue;
            }
            for (const auto& aty : traitMono.typeBounds) {
                equateTypesAssoc(sp, aty.second.type, aty.second.sourceTrait.path, aty.second.sourceTrait.params.clone(), hiddenType, aty.first.c_str(), aty.second.atyParams, false);
            }
        }
        return true;
    };

    if (const auto* et = rT->opt_ErasedType()) {
        if (const auto* ee = et->inner.opt_Alias()) {
            if (!lT->is_Infer() && equateErasedAlias(*et, *ee, lT)) {
                return;
            }
        }
    }
    if (const auto* et = lT->opt_ErasedType()) {
        if (const auto* ee = et->inner.opt_Alias()) {
            if (equateErasedAlias(*et, *ee, rT)) {
                return;
            }
        }
    }

    const auto* lRevealed = revealOpaqueType(lT);
    const auto* rRevealed = revealOpaqueType(rT);
    if (lRevealed != lT || rRevealed != rT) {
        equateTypesInner(sp, lRevealed, rRevealed);
        return;
    }

    auto setIvar = [&](const HIRType* dst, const HIRType* src) {
        auto ivarIdx = dst->as_Infer().index;
        if (ivarIdx < ivarsSized.length() && ivarsSized[ivarIdx]) {
            this->requireSized(sp, src);
        }
        if (visitTyWith(src, [&](const HIRType* ity) {
            return ity == dst;
        })) {
            auto newSrc = cloneTyWith(crate.types, sp, src, [&](const HIRType* tpl) -> const HIRType* {
                if (tpl->is_Path() && tpl->as_Path().binding.is_Unbound()) {
                    if (visitTyWith(src, [&](const HIRType* ity) {
                        return ity == dst;
                    })) {
                        const auto& pe = tpl->as_Path().path.data.as_UfcsKnown();
                        const auto* outTy = this->ivars.newIvarTr();
                        this->equateTypesAssoc(sp, outTy, pe.trait.path, pe.trait.params.clone(), pe.type, pe.item.c_str(), pe.params, false);
                        return outTy;
                    } else {
                    }
                }
                return nullptr;
            });
            ASSERT_BUG(
                sp,
                !visitTyWith(
                    newSrc,
                    [&](const HIRType* ity) {
                return ity == dst;
            }
                ),
                StringView("")
            );
            this->ivars.setIvarTo(ivarIdx, std::move(newSrc));
        } else {
            this->ivars.setIvarTo(ivarIdx, src);
        }
    };

    DEBUG(StringView("- l_t = ") << lT << StringView(", r_t = ") << rT);
    if (const auto* rE = rT->opt_Infer()) {
        if (const auto* lE = lT->opt_Infer()) {
            // TODO: Unify sized flags

            if ((rE->index < ivarsSized.length() && ivarsSized[rE->index]) || (lE->index < ivarsSized.length() && ivarsSized[lE->index])) {
                this->requireSized(sp, lT);
                this->requireSized(sp, rT);
            }

            this->ivars.ivarUnify(lE->index, rE->index);
        } else {
            setIvar(rT, lT);
        }
    } else {
        if (/*const auto* l_e =*/lT->opt_Infer()) {
            setIvar(lT, rT);
        } else {
            auto equalityTypeparams = [&](const HIRPathParams& l, const HIRPathParams& r) {
                if (l.types.size() != r.types.size()) {
                    ERROR(sp, E0000, StringView("Type mismatch in path params (type count) `") << l << StringView("` and `") << r << StringView("`"));
                }
                for (unsigned int i = 0; i < l.types.size(); i++) {
                    this->equateTypesInner(sp, l.types[i], r.types[i]);
                }

                if (l.values.size() != r.values.size()) {
                    ERROR(sp, E0000, StringView("Type mismatch in path params (value count) `") << l << StringView("` and `") << r << StringView("`"));
                }
                for (unsigned int i = 0; i < l.values.size(); i++) {
                    this->equateValues(sp, l.values[i], r.values[i]);
                }
            };
            auto equalityPath = [&](const HIRPath& l, const HIRPath& r) -> bool {
                if (l.data.tag() != r.data.tag()) {
                    return false;
                }
                switch (l.data.tag()) {
                    case HIRPathData::TAG_Generic: {
                        auto& lpe = l.data.as_Generic();
                        auto& rpe = r.data.as_Generic();
                        if (lpe.path != rpe.path) {
                            return false;
                        }
                        equalityTypeparams(lpe.params, rpe.params);
                        break;
                    }
                    case HIRPathData::TAG_UfcsInherent: {
                        auto& lpe = l.data.as_UfcsInherent();
                        auto& rpe = r.data.as_UfcsInherent();
                        equalityTypeparams(lpe.params, rpe.params);
                        if (lpe.item != rpe.item) {
                            return false;
                        }
                        this->equateTypesInner(sp, lpe.type, rpe.type);
                        break;
                    }
                    case HIRPathData::TAG_UfcsKnown: {
                        auto& lpe = l.data.as_UfcsKnown();
                        auto& rpe = r.data.as_UfcsKnown();
                        if (lpe.trait.path != rpe.trait.path || lpe.item != rpe.item) {
                            return false;
                        }
                        equalityTypeparams(lpe.trait.params, rpe.trait.params);
                        equalityTypeparams(lpe.params, rpe.params);
                        this->equateTypesInner(sp, lpe.type, rpe.type);
                        break;
                    }
                    case HIRPathData::TAG_UfcsUnknown: {
                        auto& lpe = l.data.as_UfcsUnknown();
                        auto& rpe = r.data.as_UfcsUnknown();
                        // TODO: If the type is fully known, locate a suitable trait item
                        equalityTypeparams(lpe.params, rpe.params);
                        if (lpe.item != rpe.item) {
                            return false;
                        }
                        this->equateTypesInner(sp, lpe.type, rpe.type);
                        break;
                    }
                }
                return true;
            };

            // TODO: Should ! end up in an ivar?
            if (lT->is_Diverge() && rT->is_Diverge()) {
                return;
            } else if (rT->is_Diverge()) {
                if (const auto* rE = ri->opt_Infer()) {
                    this->ivars.setIvarTo(rE->index, lT);
                }
                return;
            } else {
            }

            if (lT->tag() != rT->tag()) {
                ERROR(sp, E0000, StringView("Type mismatch between ") << this->ivars.fmtType(lT) << StringView(" and ") << this->ivars.fmtType(rT));
            }
            switch ((*lT).tag()) {
                case HIRType::TAG_Infer: {
                    UNREACHABLE();
                }
                case HIRType::TAG_Diverge: {
                    break;
                }
                case HIRType::TAG_Primitive: {
                    auto& lE = (*lT).as_Primitive();
                    auto& rE = (*rT).as_Primitive();
                    if (lE != rE) {
                        ERROR(sp, E0000, StringView("Type mismatch between ") << lT << StringView(" and ") << rT);
                    }
                    break;
                }
                case HIRType::TAG_Path: {
                    auto& lE = (*lT).as_Path();
                    auto& rE = (*rT).as_Path();
                    if (!equalityPath(lE.path, rE.path)) {
                        ERROR(sp, E0000, StringView("Type mismatch between ") << lT << StringView(" and ") << rT);
                    }
                    break;
                }
                case HIRType::TAG_Generic: {
                    auto& lE = (*lT).as_Generic();
                    auto& rE = (*rT).as_Generic();
                    if (lE.binding != rE.binding) {
                        ERROR(sp, E0000, StringView("Type mismatch between ") << lT << StringView(" and ") << rT);
                    }
                    break;
                }
                case HIRType::TAG_TraitObject: {
                    auto& lE = (*lT).as_TraitObject();
                    auto& rE = (*rT).as_TraitObject();
                    if (lE.trait.path.path != rE.trait.path.path) {
                        ERROR(sp, E0000, StringView("Type mismatch between ") << lT << StringView(" and ") << rT);
                    }
                    equalityTypeparams(lE.trait.path.params, rE.trait.path.params);
                    for (auto itL = lE.trait.typeBounds.begin(), itR = rE.trait.typeBounds.begin(); itL != lE.trait.typeBounds.end(); itL++, itR++) {
                        if (itL->first != itR->first) {
                            ERROR(sp, E0000, StringView("Type mismatch between ") << lT << StringView(" and ") << rT << StringView(" - associated bounds differ"));
                        }
                        this->equateTypesInner(sp, itL->second.type, itR->second.type);
                    }
                    if (lE.markers.size() != rE.markers.size()) {
                        ERROR(sp, E0000, StringView("Type mismatch between ") << lT << StringView(" and ") << rT << StringView(" - trait counts differ"));
                    }
                    // TODO: Is this list sorted in any way? (if it's not sorted, this could fail when source does Send+Any instead of Any+Send)
                    for (unsigned int i = 0; i < lE.markers.size(); i++) {
                        auto& lP = lE.markers[i];
                        auto& rP = rE.markers[i];
                        if (lP.path != rP.path) {
                            ERROR(sp, E0000, StringView("Type mismatch between ") << lT << StringView(" and ") << rT);
                        }
                        equalityTypeparams(lP.params, rP.params);
                    }
                    break;
                }
                case HIRType::TAG_ErasedType: {
                    auto& lE = (*lT).as_ErasedType();
                    auto& rE = (*rT).as_ErasedType();
                    if (lE.inner.tag() != rE.inner.tag()) {
                        ERROR(sp, E0000, StringView("Type mismatch between ") << lT << StringView(" and ") << rT << StringView(" - different erased class"));
                    }
                    switch (lE.inner.tag()) {
                        case TypeDataErasedTypeInner::TAG_Fcn: {
                            auto& lee = lE.inner.as_Fcn();
                            auto& ree = rE.inner.as_Fcn();
                            ASSERT_BUG(sp, lee.origin != HIRSimplePath(), StringView("ErasedType ") << lT << StringView(" wasn't bound to its origin"));
                            ASSERT_BUG(sp, ree.origin != HIRSimplePath(), StringView("ErasedType ") << rT << StringView(" wasn't bound to its origin"));
                            if (!equalityPath(lee.origin, ree.origin)) {
                                ERROR(sp, E0000, StringView("Type mismatch between ") << lT << StringView(" and ") << rT << StringView(" - different source"));
                            }
                            break;
                        }
                        case TypeDataErasedTypeInner::TAG_Alias: {
                            auto& lee = lE.inner.as_Alias();
                            auto& ree = rE.inner.as_Alias();
                            if (lee.inner != ree.inner) {
                                ERROR(sp, E0000, StringView("Type mismatch between ") << lT << StringView(" and ") << rT << StringView(" - different source"));
                            }
                            equalityTypeparams(lee.params, ree.params);
                            break;
                        }
                        case TypeDataErasedTypeInner::TAG_Known: {
                            auto& lee = lE.inner.as_Known();
                            auto& ree = rE.inner.as_Known();
                            equateTypesInner(sp, lee, ree);
                            break;
                        }
                    }
                    break;
                }
                case HIRType::TAG_Array: {
                    auto& lE = (*lT).as_Array();
                    auto& rE = (*rT).as_Array();
                    this->equateTypesInner(sp, lE.inner, rE.inner);
                    if (lE.size != rE.size) {
                        if (lE.size.is_Unevaluated() || rE.size.is_Unevaluated()) {
                            if (!lE.size.is_Unevaluated()) {
                                BUG_ASSERT(lE.size.is_Known());
                                BUG_ASSERT(rE.size.is_Unevaluated());
                                this->equateValues(sp, freezeEncodedLiteral(*crate.pool, EncodedLiteral::makeUsize(lE.size.as_Known())), rE.size.as_Unevaluated());
                            } else if (!rE.size.is_Unevaluated()) {
                                BUG_ASSERT(lE.size.is_Unevaluated());
                                BUG_ASSERT(rE.size.is_Known());
                                this->equateValues(sp, lE.size.as_Unevaluated(), freezeEncodedLiteral(*crate.pool, EncodedLiteral::makeUsize(rE.size.as_Known())));
                            } else {
                                this->equateValues(sp, lE.size.as_Unevaluated(), rE.size.as_Unevaluated());
                            }
                        } else {
                            ERROR(sp, E0000, StringView("Type mismatch between ") << lT << StringView(" and ") << rT << StringView(" - sizes differ"));
                        }
                    }
                    break;
                }
                case HIRType::TAG_Slice: {
                    auto& lE = (*lT).as_Slice();
                    auto& rE = (*rT).as_Slice();
                    this->equateTypesInner(sp, lE.inner, rE.inner);
                    break;
                }
                case HIRType::TAG_Pattern: {
                    auto& lE = (*lT).as_Pattern();
                    auto& rE = (*rT).as_Pattern();
                    if (lE.pattern.alternatives.size() != rE.pattern.alternatives.size()) {
                        ERROR(sp, E0000, StringView("Type mismatch between ") << lT << StringView(" and ") << rT << StringView(" - pattern alternative counts differ"));
                    }
                    for (size_t i = 0; i < lE.pattern.alternatives.size(); i++) {
                        const auto& left = lE.pattern.alternatives[i];
                        const auto& right = rE.pattern.alternatives[i];
                        if (left.hasStart != right.hasStart || left.hasEnd != right.hasEnd || left.endInclusive != right.endInclusive) {
                            ERROR(sp, E0000, StringView("Type mismatch between ") << lT << StringView(" and ") << rT << StringView(" - pattern shapes differ"));
                        }
                        if (left.hasStart) {
                            this->equateValues(sp, left.start, right.start);
                        }
                        if (left.hasEnd) {
                            this->equateValues(sp, left.end, right.end);
                        }
                    }
                    this->equateTypesInner(sp, lE.inner, rE.inner);
                    break;
                }
                case HIRType::TAG_Tuple: {
                    auto& lE = (*lT).as_Tuple();
                    auto& rE = (*rT).as_Tuple();
                    if (lE.length() != rE.length()) {
                        ERROR(sp, E0000, StringView("Type mismatch between ") << lT << StringView(" and ") << rT << StringView(" - Tuples are of different length"));
                    }
                    for (unsigned int i = 0; i < lE.length(); i++) {
                        this->equateTypesInner(sp, lE[i], rE[i]);
                    }
                    break;
                }
                case HIRType::TAG_Borrow: {
                    auto& lE = (*lT).as_Borrow();
                    auto& rE = (*rT).as_Borrow();
                    if (lE.type != rE.type) {
                        ERROR(sp, E0000, StringView("Type mismatch between ") << lT << StringView(" and ") << rT << StringView(" - Borrow classes differ"));
                    }
                    this->equateTypesInner(sp, lE.inner, rE.inner);
                    break;
                }
                case HIRType::TAG_Pointer: {
                    auto& lE = (*lT).as_Pointer();
                    auto& rE = (*rT).as_Pointer();
                    if (lE.type != rE.type) {
                        ERROR(sp, E0000, StringView("Type mismatch between ") << lT << StringView(" and ") << rT << StringView(" - Pointer mutability differs"));
                    }
                    this->equateTypesInner(sp, lE.inner, rE.inner);
                    break;
                }
                case HIRType::TAG_NamedFunction: {
                    auto& lE = (*lT).as_NamedFunction();
                    auto& rE = (*rT).as_NamedFunction();
                    if (!equalityPath(lE.path, rE.path)) {
                        ERROR(sp, E0000, StringView("Type mismatch between ") << lT << StringView(" and ") << rT);
                    }
                    break;
                }
                case HIRType::TAG_Function: {
                    auto& lE = (*lT).as_Function();
                    auto& rE = (*rT).as_Function();
                    if (lE.isUnsafe != rE.isUnsafe || lE.isVariadic != rE.isVariadic || lE.trackCaller != rE.trackCaller || lE.abi != rE.abi || lE.argTypes.length() != rE.argTypes.length()) {
                        ERROR(sp, E0000, StringView("Type mismatch between ") << lT << StringView(" and ") << rT);
                    }
                    // TODO: HRLs
                    this->equateTypesInner(sp, lE.rettype, rE.rettype);
                    for (unsigned int i = 0; i < lE.argTypes.length(); i++) {
                        this->equateTypesInner(sp, lE.argTypes[i], rE.argTypes[i]);
                    }
                    break;
                }
                case HIRType::TAG_NodeType: {
                    auto& lE = (*lT).as_NodeType();
                    auto& rE = (*rT).as_NodeType();
                    if (lE != rE) {
                        ERROR(sp, E0000, StringView("Type mismatch between ") << lT << StringView(" and ") << rT);
                    }
                    break;
                }
            }
        }
    }
}

void Context::equateValues(const Span& sp, const HIRConstGeneric& rl, const HIRConstGeneric& rr) {
    const auto& l = this->ivars.getValue(rl);
    const auto& r = this->ivars.getValue(rr);
    if (l != r) {
        DEBUG(l << StringView(" != ") << r);
        if (l.is_Infer()) {
            if (r.is_Infer()) {
                this->ivars.ivarValUnify(l.as_Infer().index, r.as_Infer().index);
            } else {
                this->ivars.setIvarValTo(l.as_Infer().index, r.clone());
            }
        } else {
            if (r.is_Infer()) {
                this->ivars.setIvarValTo(r.as_Infer().index, l.clone());
            } else {
                auto normalizedL = l.clone();
                auto normalizedR = r.clone();

                struct ResolveIvars: HIRVisitor {
                    Context& context;

                    explicit ResolveIvars(Context& context)
                        : HIRVisitor(nullptr, context.crate.types)
                        , context(context)
                    {
                    }

                    void visitConstgeneric(HIRConstGeneric& value) override {
                        if (value.is_Infer()) {
                            const auto& resolved = context.ivars.getValue(value);
                            if (resolved != value) {
                                value = resolved.clone();
                            }
                        }
                        HIRVisitor::visitConstgeneric(value);
                    }

                    [[nodiscard]] const HIRType* visitType(const HIRType* type) override {
                        if (type->is_Infer()) {
                            const auto resolved = context.ivars.getType(type);
                            if (resolved != type) {
                                type = resolved;
                            }
                        }
                        return visitTypeDefaultViaHooks(type);
                    }
                } resolveIvars{*this};

                resolveIvars.visitConstgeneric(normalizedL);
                resolveIvars.visitConstgeneric(normalizedR);
                ConvertHIRConstantEvaluateConstGeneric(sp, resolve.board(), crate, normalizedL);
                ConvertHIRConstantEvaluateConstGeneric(sp, resolve.board(), crate, normalizedR);

                ConstExprEquate exprEquate{*this, sp};
                if (normalizedL == normalizedR) {
                } else if (exprEquate.equateIdentity(normalizedL, normalizedR)) {
                } else if (exprEquate.equateIdentity(normalizedR, normalizedL)) {
                } else if (normalizedL.is_Unevaluated() && normalizedR.is_Unevaluated() && exprEquate.equate(*normalizedL.as_Unevaluated(), *normalizedR.as_Unevaluated())) {
                } else if (normalizedL.is_Unevaluated() && normalizedR.is_Evaluated() && exprEquate.equateEvaluated(*normalizedL.as_Unevaluated(), *normalizedR.as_Evaluated())) {
                } else if (normalizedR.is_Unevaluated() && normalizedL.is_Evaluated() && exprEquate.equateEvaluated(*normalizedR.as_Unevaluated(), *normalizedL.as_Evaluated())) {
                } else {
                    // TODO: What about unevaluated values due to type inference?
                    ERROR(sp, E0000, StringView("Value mismatch between ") << normalizedL << StringView(" and ") << normalizedR);
                }
            }
        }
        DEBUG(l << StringView(" == ") << r);
    }
}

void Context::addBindingInner(const Span& sp, const HIRPatternBinding& pb, const HIRType* type) {
    BUG_ASSERT(pb.isValid());
    switch (pb.type) {
        case HIRPatternBinding::Type::Move:
            this->addVar(sp, pb.slot, pb.name, mv$(type));
            break;
        case HIRPatternBinding::Type::Ref:
            this->addVar(sp, pb.slot, pb.name, crate.types.borrow(HIRBorrowType::Shared, type));
            break;
        case HIRPatternBinding::Type::MutRef:
            this->addVar(sp, pb.slot, pb.name, crate.types.borrow(HIRBorrowType::Unique, type));
            break;
    }
}

void Context::handlePattern(const Span& sp, HIRPattern& pat, const HIRType* type, bool isIrrefutable /*=false*/) {
    TRACE_FUNCTION_F(StringView("pat = ") << pat << StringView(", type = ") << type);
    fixupPatternValuePaths(*this, sp, pat);

    if (pat.data.is_Any()) {
        // - TODO: Does this do auto-borrow too?
        for (const auto& pb : pat.bindings) {
            this->addBindingInner(sp, pb, type);
        }
        return;
    }

    {
        struct MatchErgonomicsRevisit: public Revisitor {
            Span sp;
            bool isIrrefutable;
            const HIRType* outerTy;
            HIRPattern& pattern;
            HIRPatternBinding::Type outerMode;
            bool nestedRoot;

            mutable Vector<const HIRType*> tempIvars;
            mutable std::optional<const HIRType*> possibleType;
            mutable const HIRPattern* possibleTypePattern = nullptr;

            MatchErgonomicsRevisit(Span sp, bool isIrrefutable, const HIRType* outer, HIRPattern& pat, HIRPatternBinding::Type bindingMode = HIRPatternBinding::Type::Move, bool nestedRoot = false)
                : sp(mv$(sp))
                , isIrrefutable(isIrrefutable)
                , outerTy(mv$(outer))
                , pattern(pat)
                , outerMode(bindingMode)
                , nestedRoot(nestedRoot)
            {
            }

            const Span& span() const override {
                return sp;
            }

            void fmt(ZeroCopyOutput& os) const override {
                os << StringView("MatchErgonomicsRevisit { ") << pattern << StringView(" : ") << outerTy << StringView(" }");
            }

            bool revisit(Context& context, bool isFallbackMode) override {
                TRACE_FUNCTION_F(StringView("Match ergonomics - ") << pattern << StringView(" : ") << outerTy << StringView(isFallbackMode ? " (fallback)" : ""));
                outerTy = context.expandAssociatedTypes(sp, mv$(outerTy));
                return this->revisitInnerReal(context, pattern, outerTy, outerMode, isFallbackMode, nestedRoot);
            }

            void collectInferenceDependencies(const Context& context, Vector<unsigned>& dependencies) const override {
                visitTyWith(outerTy, [&](const HIRType* inner) {
                    const auto* resolved = context.getType(inner);
                    if (const auto* infer = resolved->opt_Infer(); infer && infer->index != ~0u) {
                        dependencies.pushBack(infer->index);
                    }
                    return false;
                });
            }

            // TODO: Recurse into inner patterns, creating new revisitors?

            bool revisitInner(Context& context, HIRPattern& pattern, const HIRType* type, HIRPatternBinding::Type bindingMode) const {
                if (!revisitInnerReal(context, pattern, type, bindingMode, false, true)) {
                    DEBUG(StringView("Add revisit for ") << pattern << StringView(" : ") << type << StringView("(mode = ") << (int)bindingMode << StringView(")"));
                    context.addRevisitAdv(box$((MatchErgonomicsRevisit{sp, isIrrefutable, type, pattern, bindingMode, true})));
                }
                return true;
            }

            std::optional<const HIRType*> getPossibleTypeVal(Context& context, HIRPattern::Value& pv) const {
                switch (pv.tag()) {
                    case HIRPatternValue::TAG_Integer: {
                        auto& ve = pv.as_Integer();
                        if (ve.type == HIRCoreType::Str) {
                            return context.ivars.newIvarTr(HIRInferClass::Integer);
                        }
                        return context.crate.types.primitive(ve.type);
                    }
                    case HIRPatternValue::TAG_Float: {
                        auto& ve = pv.as_Float();
                        if (ve.type == HIRCoreType::Str) {
                            return context.ivars.newIvarTr(HIRInferClass::Float);
                        }
                        return context.crate.types.primitive(ve.type);
                    }
                    case HIRPatternValue::TAG_String: {
                        return context.crate.types.borrow(HIRBorrowType::Shared, context.crate.types.primitive(HIRCoreType::Str));
                    }
                    case HIRPatternValue::TAG_ByteString: {
                        auto& ve = pv.as_ByteString();
                        return context.crate.types.borrow(HIRBorrowType::Shared, context.crate.types.array(context.crate.types.primitive(HIRCoreType::U8), ve.v.size()));
                    }
                    case HIRPatternValue::TAG_Named: {
                        auto& ve = pv.as_Named();
                        DEBUG(StringView("TODO: Look up the path and get the type: ") << ve.path);
                        if (ve.binding) {
                            if (ve.path.data.is_UfcsKnown()) {
                                const auto& pe = ve.path.data.as_UfcsKnown();
                                auto ms = MonomorphStatePtr(context.crate.types, pe.type, &pe.trait.params, nullptr);
                                return ms.monomorphType(sp, ve.binding->type);
                            }
                            return ve.binding->type;
                        } else if (ve.path.data.is_Generic()) {
                            TODO(sp, StringView("Look up pattern value: ") << ve.path);
                        } else {
                            return std::nullopt;
                        }
                        break;
                    }
                }
                UNREACHABLE();
            }

            std::optional<const HIRType*> getPossibleTypeInner(Context& context, HIRPattern& pattern) const {
                std::optional<const HIRType*> possibleType;
                switch (pattern.data.tag()) {
                    case HIRPatternData::TAG_Any: {
                        break;
                    }
                    case HIRPatternData::TAG_Value: {
                        auto& pe = pattern.data.as_Value();
                        possibleType = getPossibleTypeVal(context, pe.val);
                        break;
                    }
                    case HIRPatternData::TAG_Range: {
                        auto& pe = pattern.data.as_Range();
                        if (pe.start) {
                            possibleType = getPossibleTypeVal(context, *pe.start);
                        }
                        if (!possibleType) {
                            if (pe.end) {
                                possibleType = getPossibleTypeVal(context, *pe.end);
                            }
                        } else {
                            // TODO: Check that the type from .end matches .start
                        }
                        break;
                    }
                    case HIRPatternData::TAG_Box: {
                        // TODO: Get type info (Box<_>) ?

                        break;
                    }
                    case HIRPatternData::TAG_Deref: {
                        break;
                    }
                    case HIRPatternData::TAG_Ref: {
                        BUG(sp, StringView("Match ergonomics - & pattern"));
                        break;
                    }
                    case HIRPatternData::TAG_Tuple: {
                        auto& e = pattern.data.as_Tuple();
                        if (tempIvars.length() != e.subPatterns.size()) {
                            for (size_t i = 0; i < e.subPatterns.size(); i++) {
                                tempIvars.pushBack(context.ivars.newIvarTr());
                            }
                        }
                        decltype(tempIvars) tuple;
                        for (const auto& ty : tempIvars) {
                            tuple.pushBack(ty);
                        }
                        possibleType = context.crate.types.tuple(std::move(tuple));
                        break;
                    }
                    case HIRPatternData::TAG_SplitTuple: {
                        break;
                    }
                    case HIRPatternData::TAG_Slice: {
                        auto& e = pattern.data.as_Slice();
                        possibleType = context.crate.types.array(context.ivars.newIvarTr(), e.subPatterns.size());
                        break;
                    }
                    case HIRPatternData::TAG_SplitSlice: {
                        break;
                    }
                    case HIRPatternData::TAG_PathValue: {
                        auto& e = pattern.data.as_PathValue();
                        switch (e.binding.tag()) {
                            case HIRPatternPathBinding::TAG_Unbound: {
                                auto& _ = e.binding.as_Unbound();
                                BUG(sp, StringView(""));
                                break;
                            }
                            case HIRPatternPathBinding::TAG_Struct: {
                                auto& be = e.binding.as_Struct();
                                auto& p = e.path.data.as_Generic();
                                BUG_ASSERT(be);
                                context.addIvarsParams(p.params);
                                possibleType = context.crate.types.path(p.clone(), HIRTypePathBinding(be));
                                break;
                            }
                            case HIRPatternPathBinding::TAG_Union: {
                                auto& be = e.binding.as_Union();
                                auto& p = e.path.data.as_Generic();
                                BUG_ASSERT(be);
                                context.addIvarsParams(p.params);
                                possibleType = context.crate.types.path(p.clone(), HIRTypePathBinding(be));
                                break;
                            }
                            case HIRPatternPathBinding::TAG_Enum: {
                                auto& be = e.binding.as_Enum();
                                auto& p = e.path.data.as_Generic();
                                BUG_ASSERT(be.ptr);
                                context.addIvarsParams(p.params);
                                possibleType = context.crate.types.path(getParentPath(p), HIRTypePathBinding(be.ptr));
                                break;
                            }
                        }
                        break;
                    }
                    case HIRPatternData::TAG_PathTuple: {
                        auto& e = pattern.data.as_PathTuple();
                        switch (e.binding.tag()) {
                            case HIRPatternPathBinding::TAG_Unbound: {
                                auto& _ = e.binding.as_Unbound();
                                BUG(sp, StringView(""));
                                break;
                            }
                            case HIRPatternPathBinding::TAG_Struct: {
                                auto& be = e.binding.as_Struct();
                                auto& p = e.path.data.as_Generic();
                                BUG_ASSERT(be);
                                context.addIvarsParams(p.params);
                                possibleType = context.crate.types.path(p.clone(), HIRTypePathBinding(be));
                                break;
                            }
                            case HIRPatternPathBinding::TAG_Union: {
                                auto& be = e.binding.as_Union();
                                auto& p = e.path.data.as_Generic();
                                BUG_ASSERT(be);
                                context.addIvarsParams(p.params);
                                possibleType = context.crate.types.path(p.clone(), HIRTypePathBinding(be));
                                break;
                            }
                            case HIRPatternPathBinding::TAG_Enum: {
                                auto& be = e.binding.as_Enum();
                                auto& p = e.path.data.as_Generic();
                                BUG_ASSERT(be.ptr);
                                context.addIvarsParams(p.params);
                                possibleType = context.crate.types.path(getParentPath(p), HIRTypePathBinding(be.ptr));
                                break;
                            }
                        }
                        break;
                    }
                    case HIRPatternData::TAG_PathNamed: {
                        auto& e = pattern.data.as_PathNamed();
                        switch (e.binding.tag()) {
                            case HIRPatternPathBinding::TAG_Unbound: {
                                auto& _ = e.binding.as_Unbound();
                                BUG(sp, StringView(""));
                                break;
                            }
                            case HIRPatternPathBinding::TAG_Struct: {
                                auto& be = e.binding.as_Struct();
                                auto& p = e.path.data.as_Generic();
                                BUG_ASSERT(be);
                                context.addIvarsParams(p.params);
                                possibleType = context.crate.types.path(p.clone(), HIRTypePathBinding(be));
                                break;
                            }
                            case HIRPatternPathBinding::TAG_Union: {
                                auto& be = e.binding.as_Union();
                                auto& p = e.path.data.as_Generic();
                                BUG_ASSERT(be);
                                context.addIvarsParams(p.params);
                                possibleType = context.crate.types.path(p.clone(), HIRTypePathBinding(be));
                                break;
                            }
                            case HIRPatternPathBinding::TAG_Enum: {
                                auto& be = e.binding.as_Enum();
                                auto& p = e.path.data.as_Generic();
                                BUG_ASSERT(be.ptr);
                                context.addIvarsParams(p.params);
                                possibleType = context.crate.types.path(getParentPath(p), HIRTypePathBinding(be.ptr));
                                break;
                            }
                        }
                        break;
                    }
                    case HIRPatternData::TAG_Or: {
                        auto& e = pattern.data.as_Or();
                        for (auto& subpat : e) {
                            possibleType = getPossibleTypeInner(context, subpat);
                            if (possibleType) {
                                break;
                            }
                        }
                        break;
                    }
                }
                return possibleType;
            }

            const std::optional<const HIRType*>& getPossibleType(Context& context, HIRPattern& pattern) const {
                if (!possibleType || possibleTypePattern != &pattern) {
                    possibleType = getPossibleTypeInner(context, pattern);
                    possibleTypePattern = &pattern;
                }
                return possibleType;
            }

            static bool hasMutableBinding(const HIRPattern& pattern) {
                for (const auto& binding : pattern.bindings) {
                    if (binding.type == HIRPatternBinding::Type::MutRef) {
                        return true;
                    }
                }
                switch (pattern.data.tag()) {
                    case HIRPatternData::TAG_Any: {
                        return false;
                    }
                    case HIRPatternData::TAG_Value: {
                        return false;
                    }
                    case HIRPatternData::TAG_Range: {
                        return false;
                    }
                    case HIRPatternData::TAG_Box: {
                        auto& e = pattern.data.as_Box();
                        return hasMutableBinding(*e.sub);
                    }
                    case HIRPatternData::TAG_Deref: {
                        auto& e = pattern.data.as_Deref();
                        return hasMutableBinding(*e.sub);
                    }
                    case HIRPatternData::TAG_Ref: {
                        auto& e = pattern.data.as_Ref();
                        return hasMutableBinding(*e.sub);
                    }
                    case HIRPatternData::TAG_Tuple: {
                        auto& e = pattern.data.as_Tuple();
                        for (const auto& sub : e.subPatterns) {
                            if (hasMutableBinding(sub)) {
                                return true;
                            }
                        }
                        return false;
                        break;
                    }
                    case HIRPatternData::TAG_SplitTuple: {
                        auto& e = pattern.data.as_SplitTuple();
                        for (const auto& sub : e.leading) {
                            if (hasMutableBinding(sub)) {
                                return true;
                            }
                        }
                        for (const auto& sub : e.trailing) {
                            if (hasMutableBinding(sub)) {
                                return true;
                            }
                        }
                        return false;
                        break;
                    }
                    case HIRPatternData::TAG_PathValue: {
                        return false;
                    }
                    case HIRPatternData::TAG_PathTuple: {
                        auto& e = pattern.data.as_PathTuple();
                        for (const auto& sub : e.leading) {
                            if (hasMutableBinding(sub)) {
                                return true;
                            }
                        }
                        for (const auto& sub : e.trailing) {
                            if (hasMutableBinding(sub)) {
                                return true;
                            }
                        }
                        return false;
                        break;
                    }
                    case HIRPatternData::TAG_PathNamed: {
                        auto& e = pattern.data.as_PathNamed();
                        for (const auto& sub : e.subPatterns) {
                            if (hasMutableBinding(sub.second)) {
                                return true;
                            }
                        }
                        return false;
                        break;
                    }
                    case HIRPatternData::TAG_Slice: {
                        auto& e = pattern.data.as_Slice();
                        for (const auto& sub : e.subPatterns) {
                            if (hasMutableBinding(sub)) {
                                return true;
                            }
                        }
                        return false;
                        break;
                    }
                    case HIRPatternData::TAG_SplitSlice: {
                        auto& e = pattern.data.as_SplitSlice();
                        if (e.extraBind.type == HIRPatternBinding::Type::MutRef) {
                            return true;
                        }
                        for (const auto& sub : e.leading) {
                            if (hasMutableBinding(sub)) {
                                return true;
                            }
                        }
                        for (const auto& sub : e.trailing) {
                            if (hasMutableBinding(sub)) {
                                return true;
                            }
                        }
                        return false;
                        break;
                    }
                    case HIRPatternData::TAG_Or: {
                        auto& e = pattern.data.as_Or();
                        for (const auto& sub : e) {
                            if (hasMutableBinding(sub)) {
                                return true;
                            }
                        }
                        return false;
                        break;
                    }
                }
                UNREACHABLE();
            }

            static bool directlyMatches(const HIRPattern& pattern, const HIRType* type) {
                auto matchesPath = [&](const HIRPath& patternPath, const HIRPattern::PathBinding& binding) {
                    const auto* actual = type->opt_Path();
                    if (!actual || !actual->path.data.is_Generic() || !patternPath.data.is_Generic()) {
                        return false;
                    }
                    const auto& actualPath = actual->path.data.as_Generic().path;
                    const auto& patternGeneric = patternPath.data.as_Generic();
                    return binding.is_Enum() ? actualPath == getParentPath(patternGeneric).path : actualPath == patternGeneric.path;
                };
                switch (pattern.data.tag()) {
                    case HIRPatternData::TAG_Any: {
                        return true;
                    }
                    case HIRPatternData::TAG_Deref: {
                        return true;
                    }
                    case HIRPatternData::TAG_Box: {
                        return type->is_Path();
                    }
                    case HIRPatternData::TAG_Ref: {
                        return type->is_Borrow();
                    }
                    case HIRPatternData::TAG_Tuple: {
                        return type->is_Tuple();
                    }
                    case HIRPatternData::TAG_SplitTuple: {
                        return type->is_Tuple();
                    }
                    case HIRPatternData::TAG_Slice: {
                        return type->is_Array() || type->is_Slice();
                    }
                    case HIRPatternData::TAG_SplitSlice: {
                        return type->is_Array() || type->is_Slice();
                    }
                    case HIRPatternData::TAG_PathValue: {
                        auto& e = pattern.data.as_PathValue();
                        return matchesPath(e.path, e.binding);
                    }
                    case HIRPatternData::TAG_PathTuple: {
                        auto& e = pattern.data.as_PathTuple();
                        return matchesPath(e.path, e.binding);
                    }
                    case HIRPatternData::TAG_PathNamed: {
                        auto& e = pattern.data.as_PathNamed();
                        return matchesPath(e.path, e.binding);
                    }
                    case HIRPatternData::TAG_Range: {
                        return type->is_Primitive();
                    }
                    case HIRPatternData::TAG_Value: {
                        auto& e = pattern.data.as_Value();
                        if (e.val.is_Named()) {
                            return true;
                        }
                        if (e.val.is_String()) {
                            return type->is_Primitive() && type->as_Primitive() == HIRCoreType::Str;
                        }
                        if (e.val.is_ByteString()) {
                            return type->is_Array() || type->is_Slice();
                        }
                        if (e.val.is_Integer() || e.val.is_Float()) {
                            return type->is_Primitive();
                        }
                        return false;
                    }
                    case HIRPatternData::TAG_Or: {
                        auto& e = pattern.data.as_Or();
                        return !e.empty() && directlyMatches(e.front(), type);
                    }
                }
                UNREACHABLE();
            }

            bool revisitInnerReal(Context& context, HIRPattern& pattern, const HIRType* type, HIRPatternBinding::Type bindingMode, bool isFallback, bool isNested) const {
                type = context.expandAssociatedTypes(sp, context.getType(type));

                TRACE_FUNCTION_F(pattern << StringView(" : ") << type);
                for (auto& pb : pattern.bindings) {
                    if (bindingMode != HIRPatternBinding::Type::Move && context.crate.edition >= ASTEdition::Rust2024 && !context.crate.featureEnabled("mut_ref") && (pb.isMutable || pb.type != HIRPatternBinding::Type::Move)) {
                        ERROR(sp, E0000, StringView("cannot bind `") << pb.name << StringView("` with `") << StringView(pb.type != HIRPatternBinding::Type::Move ? "ref" : "mut") << StringView("` within an implicitly-borrowing pattern"));
                    }
                    if (pb.type == HIRPatternBinding::Type::Move && (!pb.isMutable || context.crate.edition >= ASTEdition::Rust2024)) {
                        pb.type = bindingMode;
                    }
                    const HIRType* tmp;
                    const HIRType* bindingType = nullptr;
                    switch (pb.type) {
                        case HIRPatternBinding::Type::Move:
                            bindingType = type;
                            break;
                        case HIRPatternBinding::Type::MutRef:
                            bindingType = (tmp = context.crate.types.borrow(HIRBorrowType::Unique, type));
                            break;
                        case HIRPatternBinding::Type::Ref:
                            bindingType = (tmp = context.crate.types.borrow(HIRBorrowType::Shared, type));
                            break;
                        default:
                            TODO(sp, StringView("Assign variable type using mode ") << (int)bindingMode << StringView(" and ") << type);
                    }
                    BUG_ASSERT(bindingType);
                    context.equateTypes(sp, context.getVar(sp, pb.slot), bindingType);
                }

                if (pattern.data.is_Any()) {
                    return true;
                }

                if (type->is_Diverge()) {
                    return true;
                }

                if (auto* pe = pattern.data.opt_Ref()) {
                    if (bindingMode != HIRPatternBinding::Type::Move && context.crate.edition >= ASTEdition::Rust2024 && !context.crate.featureEnabled("ref_pat_eat_one_layer_2024") && !context.crate.featureEnabled("mut_ref")) {
                        ERROR(sp, E0000, StringView("cannot explicitly dereference within an implicitly-borrowing pattern - ") << pattern);
                    }

                    if (bindingMode != HIRPatternBinding::Type::Move && context.crate.edition >= ASTEdition::Rust2024 && context.crate.featureEnabled("ref_pat_eat_one_layer_2024")) {
                        if (pe->type == HIRBorrowType::Unique && bindingMode != HIRPatternBinding::Type::MutRef) {
                            ERROR(sp, E0000, StringView("cannot match an inherited shared reference with an `&mut` pattern"));
                        }
                        pe->isSkipped = true;
                        return this->revisitInner(context, *pe->sub, type, HIRPatternBinding::Type::Move);
                    }

                    auto innerTy = context.ivars.newIvarTr();
                    auto newTy = context.crate.types.borrow(pe->type, innerTy);
                    context.equateTypes(sp, type, newTy);

                    return this->revisitInner(context, *pe->sub, innerTy, HIRPatternBinding::Type::Move);
                }
                if (auto* pe = pattern.data.opt_Or()) {
                    bool rv = true;
                    for (auto& subpat : *pe) {
                        rv &= this->revisitInner(context, subpat, type, bindingMode);
                    }
                    return rv;
                }
                if (auto* pe = pattern.data.opt_Value(); pe && pe->val.is_Named()) {
                    const auto valueType = getPossibleTypeVal(context, pe->val);
                    ASSERT_BUG(sp, valueType, StringView("No type for named value pattern ") << pattern);
                    pattern.implicitDerefCount = 0;
                    context.equateTypes(sp, type, context.getType(*valueType));
                    return true;
                }

                unsigned nDeref = 0;
                HIRBorrowType bt = HIRBorrowType::Owned;
                const auto* ty = context.revealOpaqueType(type);
                while (const auto* te = ty->opt_Borrow()) {
                    DEBUG(StringView("bt ") << bt << StringView(", ") << te->type);
                    bt = std::min(bt, te->type);
                    ty = context.revealOpaqueType(te->inner);
                    nDeref++;
                }
                DEBUG(StringView("- ") << nDeref << StringView(" derefs of class ") << bt << StringView(" to get ") << ty);
                if (ty->is_Infer() || ((*ty).is_Path() && ((*ty).as_Path().binding.is_Unbound()))) {
                    const auto* infer = ty->opt_Infer();

                    const auto& possibleType = getPossibleType(context, pattern);
                    if (possibleType) {
                        DEBUG(StringView("n_deref = ") << nDeref << StringView(", possible_type = ") << *possibleType);
                        const HIRType* possibleTypeP = *possibleType;
                        for (size_t i = 0; i < nDeref && possibleTypeP; i++) {
                            if (const auto* te = possibleTypeP->opt_Borrow()) {
                                possibleTypeP = te->inner;
                            } else {
                                possibleTypeP = nullptr;
                            }
                        }
                        if (possibleTypeP) {
                            const auto* possibleType = possibleTypeP;
                            if (isFallback) {
                                /* A nested pattern can determine its input
                                 * only after every external obligation which
                                 * mentions that input has had its fallback
                                 * pass.  The pattern revisit itself is the
                                 * remaining fact, not a stored candidate. */
                                bool hasExternalInferenceOwner = false;
                                /* A slice pattern's shape is forced, not a guess about which
                                   type could match: N elements with no rest binding is an
                                   array of exactly N. There is nothing for an external
                                   obligation to decide differently, and waiting for one
                                   deadlocks when that obligation is itself waiting on this
                                   input - in `let Foo([a, b, c]) = Foo(x.into())` each side
                                   waits for the other. */
                                const bool shapeIsForced = pattern.data.is_Slice();
                                if (nestedRoot && !shapeIsForced && infer && infer->index != ~0u) {
                                    const IvarCoercionIndex obligations(context);
                                    if (infer->index < obligations.refs.size()) {
                                        const auto& refs = obligations[infer->index];
                                        /* Only a coercion edge is a rival reading of the
                                           input: it offers a type of its own.  A trait
                                           obligation names none - many types satisfy
                                           `T: Default` - and a node revisit does not choose
                                           among types either, it computes one from its own
                                           inputs and would already have done so if it
                                           could.  Waiting for those never ends, and when
                                           the node is waiting on this very input, both
                                           stop: `match text.parse() { Ok(42i32) =>` has
                                           nothing but the pattern to say what the call
                                           returns. */
                                        hasExternalInferenceOwner = !refs.coercions.empty();
                                    }
                                }
                                if (!nestedRoot || !hasExternalInferenceOwner) {
                                    const HIRType* fallbackType = possibleType;
                                    if (!isIrrefutable) {
                                        if (const auto* te2 = possibleType->opt_Array()) {
                                            const bool requiresSized = infer
                                                && infer->index < context.ivarsSized.length()
                                                && context.ivarsSized[infer->index];
                                            if (!requiresSized) {
                                                fallbackType = context.crate.types.slice(te2->inner);
                                            }
                                        }
                                    }
                                    DEBUG(StringView("Fallback equate ") << fallbackType);
                                    context.equateTypes(sp, ty, fallbackType);
                                }
                            }
                        }
                    }

                    return false;
                }
                if (ty->is_Primitive() && ty->as_Primitive() == HIRCoreType::Str) {
                }

                pattern.implicitDerefCount = nDeref;
                switch (bt) {
                    case HIRBorrowType::Owned:
                        break;
                    case HIRBorrowType::Unique:
                        switch (bindingMode) {
                            case HIRPatternBinding::Type::Move:
                            case HIRPatternBinding::Type::MutRef:
                                bindingMode = HIRPatternBinding::Type::MutRef;
                                break;
                            case HIRPatternBinding::Type::Ref:
                                break;
                        }
                        break;
                    case HIRBorrowType::Shared:
                        bindingMode = HIRPatternBinding::Type::Ref;
                        break;
                }

                if (!pattern.data.is_Deref() && !directlyMatches(pattern, ty)) {
                    HIRPattern::DerefKind derefKind;
                    const HIRType* target;
                    if (const auto* inner = context.resolve.typeIsOwnedBox(sp, ty)) {
                        derefKind = HIRPattern::DerefKind::Box;
                        target = inner;
                    } else {
                        const auto result = context.resolve.autoderefStep(sp, ty);
                        if (result.result == TraitResolution::AutoderefResult::Ambiguous) {
                            return false;
                        }
                        if (result.result == TraitResolution::AutoderefResult::NoMatch || !result.implType) {
                            ERROR(sp, E0000, StringView("Pattern ") << pattern << StringView(" cannot match ") << ty);
                        }
                        target = result.target;
                        context.equateTypes(sp, ty, *result.implType);
                        context.equateTypesAssoc(sp, target, context.crate.getLangItemPath(sp, "deref"), {}, ty, "Target", {}, true, TypeckPrimitiveOperator::Deref);
                        context.addTraitBound(sp, ty, context.crate.getLangItemPath(sp, "deref_pure"), {});
                        const bool unique = bindingMode == HIRPatternBinding::Type::MutRef || hasMutableBinding(pattern);
                        if (unique) {
                            context.addTraitBound(sp, ty, context.crate.getLangItemPath(sp, "deref_mut"), {});
                        }
                        derefKind = unique ? HIRPattern::DerefKind::Unique : HIRPattern::DerefKind::Shared;
                    }

                    HIRPattern inner(std::vector<HIRPatternBinding>{}, mv$(pattern.data));
                    pattern.data = HIRPattern::Data::make_Deref({derefKind, target, box$(mv$(inner))});
                    auto& deref = pattern.data.as_Deref();
                    return this->revisitInner(context, *deref.sub, target, bindingMode);
                }

                bool rv = false;
                switch (pattern.data.tag()) {
                    case HIRPatternData::TAG_Ref: {
                        BUG(sp, StringView("Match ergonomics - `&` pattern already handled"));
                        break;
                    }
                    case HIRPatternData::TAG_Or: {
                        BUG(sp, StringView("Match ergonomics - `|` pattern already handled"));
                        break;
                    }
                    case HIRPatternData::TAG_Any: {
                        rv = true;
                        break;
                    }
                    case HIRPatternData::TAG_Value: {
                        auto& pe = pattern.data.as_Value();
                        if (pe.val.is_String()) {
                            if (!(ty->is_Primitive() && ty->as_Primitive() == HIRCoreType::Str)) {
                                ASSERT_BUG(sp, pattern.implicitDerefCount >= 1, StringView(""));
                                pattern.implicitDerefCount -= 1;
                            }
                        } else if (pe.val.is_ByteString()) {
                            const auto& bytes = pe.val.as_ByteString().v;
                            if (const auto* array = ty->opt_Array()) {
                                context.equateTypes(sp, array->inner, context.crate.types.primitive(HIRCoreType::U8));
                                if (array->size.is_Known() && array->size.as_Known() != bytes.size()) {
                                    ERROR(sp, E0000, StringView("Byte string pattern has length ") << bytes.size() << StringView(", but is matching ") << ty);
                                }
                            } else if (const auto* slice = ty->opt_Slice()) {
                                context.equateTypes(sp, slice->inner, context.crate.types.primitive(HIRCoreType::U8));
                            } else {
                                ASSERT_BUG(sp, pattern.implicitDerefCount >= 1, StringView(""));
                                pattern.implicitDerefCount -= 1;
                            }
                        }
                        rv = true;
                        break;
                    }
                    case HIRPatternData::TAG_Range: {
                        rv = true;
                        break;
                    }
                    case HIRPatternData::TAG_Box: {
                        auto& pe = pattern.data.as_Box();
                        if (((*ty).is_Path() && (*ty).as_Path().path.data.is_Generic() && (*ty).as_Path().path.data.as_Generic().path == context.langBox)) {
                            const auto& path = ty->as_Path().path.data.as_Generic();
                            const auto& inner = path.params.types.at(0);
                            rv = this->revisitInner(context, *pe.sub, inner, bindingMode);
                        } else {
                            TODO(sp, StringView("Match ergonomics - box pattern - Non Box<T> type: ") << ty);
                        }
                        break;
                    }
                    case HIRPatternData::TAG_Deref: {
                        auto& pe = pattern.data.as_Deref();
                        if (const auto* inner = context.resolve.typeIsOwnedBox(sp, ty)) {
                            pe.kind = HIRPattern::DerefKind::Box;
                            pe.targetType = inner;
                            rv = this->revisitInner(context, *pe.sub, inner, bindingMode);
                            break;
                        }

                        const auto result = context.resolve.autoderefStep(sp, ty);
                        if (result.result == TraitResolution::AutoderefResult::Ambiguous) {
                            return false;
                        }
                        if (result.result == TraitResolution::AutoderefResult::NoMatch || !result.implType) {
                            ERROR(sp, E0000, StringView("Type ") << ty << StringView(" cannot be used in a deref pattern"));
                        }
                        context.equateTypes(sp, ty, *result.implType);
                        context.equateTypesAssoc(sp, result.target, context.crate.getLangItemPath(sp, "deref"), {}, ty, "Target", {}, true, TypeckPrimitiveOperator::Deref);
                        context.addTraitBound(sp, ty, context.crate.getLangItemPath(sp, "deref_pure"), {});

                        const bool unique = bindingMode == HIRPatternBinding::Type::MutRef || hasMutableBinding(*pe.sub);
                        if (unique) {
                            context.addTraitBound(sp, ty, context.crate.getLangItemPath(sp, "deref_mut"), {});
                        }
                        pe.kind = unique ? HIRPattern::DerefKind::Unique : HIRPattern::DerefKind::Shared;
                        pe.targetType = result.target;
                        rv = this->revisitInner(context, *pe.sub, result.target, bindingMode);
                        break;
                    }
                    case HIRPatternData::TAG_Tuple: {
                        auto& e = pattern.data.as_Tuple();
                        if (!ty->is_Tuple()) {
                            ERROR(sp, E0000, StringView("Matching a non-tuple with a tuple pattern - ") << ty);
                        }
                        const auto& te = ty->as_Tuple();
                        if (e.subPatterns.size() != te.length()) {
                            ERROR(sp, E0000, StringView("Tuple pattern with an incorrect number of fields, expected ") << e.subPatterns.size() << StringView("-tuple, got ") << ty);
                        }

                        rv = true;
                        for (unsigned int i = 0; i < e.subPatterns.size(); i++) {
                            rv &= this->revisitInner(context, e.subPatterns[i], te[i], bindingMode);
                        }
                        break;
                    }
                    case HIRPatternData::TAG_SplitTuple: {
                        auto& pe = pattern.data.as_SplitTuple();
                        if (!ty->is_Tuple()) {
                            ERROR(sp, E0000, StringView("Matching a non-tuple with a tuple pattern - ") << ty);
                        }
                        const auto& te = ty->as_Tuple();
                        if (pe.leading.size() + pe.trailing.size() > te.length()) {
                            ERROR(sp, E0000, StringView("Split-tuple pattern with an incorrect number of fields, expected at most ") << (pe.leading.size() + pe.trailing.size()) << StringView("-tuple, got ") << te.length());
                        }
                        pe.totalSize = te.length();
                        rv = true;
                        for (size_t i = 0; i < pe.leading.size(); i++) {
                            rv &= this->revisitInner(context, pe.leading[i], te[i], bindingMode);
                        }
                        for (size_t i = 0; i < pe.trailing.size(); i++) {
                            rv &= this->revisitInner(context, pe.trailing[i], te[te.length() - pe.trailing.size() + i], bindingMode);
                        }
                        break;
                    }
                    case HIRPatternData::TAG_Slice: {
                        auto& e = pattern.data.as_Slice();
                        const HIRType* sliceInner;
                        if (const auto* te = ty->opt_Slice()) {
                            sliceInner = te->inner;
                        } else if (const auto* te = ty->opt_Array()) {
                            sliceInner = te->inner;
                            context.equateTypes(sp, ty, context.crate.types.array(sliceInner, e.subPatterns.size()));
                        } else {
                            ERROR(sp, E0000, StringView("Matching a non-array/slice with a slice pattern - ") << ty);
                        }
                        rv = true;
                        for (auto& sub : e.subPatterns) {
                            rv |= this->revisitInner(context, sub, sliceInner, bindingMode);
                        }
                        break;
                    }
                    case HIRPatternData::TAG_SplitSlice: {
                        auto& pe = pattern.data.as_SplitSlice();
                        const HIRType* sliceInner;
                        if (const auto* te = ty->opt_Slice()) {
                            sliceInner = te->inner;
                        } else if (const auto* te = ty->opt_Array()) {
                            sliceInner = te->inner;
                        } else {
                            ERROR(sp, E0000, StringView("Matching a non-array/slice with a slice pattern - ") << ty);
                        }
                        rv = true;
                        for (auto& sub : pe.leading) {
                            rv |= this->revisitInner(context, sub, sliceInner, bindingMode);
                        }
                        if (pe.extraBind.isValid()) {
                            const HIRType* bindingTyInner = context.crate.types.slice(sliceInner);
                            // TODO: Do arrays get bound as arrays?
                            if (ty->is_Array()) {
                                size_t sizeSub = pe.leading.size() + pe.trailing.size();
                                bindingTyInner = context.crate.types.array(sliceInner, ty->as_Array().size.as_Known() - sizeSub);
                                //TODO(sp, StringView("SplitSlice extra bind with array: ") << pe.extra_bind << " on " << ty);
                            }
                            const HIRType* bindingTy;
                            if (pe.extraBind.type == HIRPatternBinding::Type::Move) {
                                pe.extraBind.type = bindingMode;
                            }
                            switch (pe.extraBind.type) {
                                case HIRPatternBinding::Type::Move:
                                    ASSERT_BUG(sp, ty->is_Array(), StringView("Non-array SplitSlize move bind"));
                                    bindingTy = mv$(bindingTyInner);
                                    break;
                                case HIRPatternBinding::Type::Ref:
                                    bindingTy = context.crate.types.borrow(HIRBorrowType::Shared, bindingTyInner);
                                    break;
                                case HIRPatternBinding::Type::MutRef:
                                    bindingTy = context.crate.types.borrow(HIRBorrowType::Unique, bindingTyInner);
                                    break;
                            }
                            context.equateTypes(sp, context.getVar(sp, pe.extraBind.slot), bindingTy);
                        }
                        for (auto& sub : pe.trailing) {
                            rv |= this->revisitInner(context, sub, sliceInner, bindingMode);
                        }
                        break;
                    }
                    case HIRPatternData::TAG_PathValue: {
                        auto& e = pattern.data.as_PathValue();
                        auto possibleType = getPossibleTypeInner(context, pattern);
                        ASSERT_BUG(sp, possibleType, StringView("No type for path pattern ") << pattern);
                        context.equateTypes(sp, ty, *possibleType);

                        switch (e.binding.tag()) {
                            case HIRPatternPathBinding::TAG_Unbound: {
                                UNREACHABLE();
                            }
                            case HIRPatternPathBinding::TAG_Struct: {
                                auto& be = e.binding.as_Struct();
                                const auto& str = *be;
                                ASSERT_BUG(sp, str.data.is_Unit(), StringView("PathValue used on non-unit struct variant"));
                                break;
                            }
                            case HIRPatternPathBinding::TAG_Union: {
                                BUG(sp, StringView("PathValue used for union"));
                                break;
                            }
                            case HIRPatternPathBinding::TAG_Enum: {
                                auto& be = e.binding.as_Enum();
                                const auto& enm = *be.ptr;
                                if (const auto* ee = enm.data.opt_Data()) {
                                    ASSERT_BUG(sp, be.varIdx < ee->size(), StringView(""));
                                    const auto& var = (*ee)[be.varIdx];
                                    ASSERT_BUG(sp, var.type == context.crate.types.unit(), StringView("EnumValue used on non-value enum variant"));
                                }
                                break;
                            }
                        }
                        rv = true;
                        break;
                    }
                    case HIRPatternData::TAG_PathTuple: {
                        auto& e = pattern.data.as_PathTuple();
                        auto possibleType = getPossibleTypeInner(context, pattern);
                        ASSERT_BUG(sp, possibleType, StringView("No type for tuple path pattern ") << pattern);
                        context.equateTypes(sp, ty, *possibleType);

                        const auto& sd = patternGetTuple(sp, e.path, e.binding);

                        auto ms = MonomorphStatePtr(context.crate.types, nullptr, &e.path.data.as_Generic().params, nullptr);
                        const HIRType* tmp;
                        auto maybeMonomorph = [&](const HIRType* fieldType) -> const HIRType* {
                            return (monomorphiseTypeNeeded(fieldType) ? (tmp = context.expandAssociatedTypes(sp, ms.monomorphType(sp, fieldType))) : fieldType);
                        };

                        e.totalSize = sd.size();

                        rv = true;
                        for (unsigned int i = 0; i < e.leading.size(); i++) {
                            /*const*/ auto& subPat = e.leading[i];
                            const auto& varTy = maybeMonomorph(sd[i].ent);
                            rv &= this->revisitInner(context, subPat, varTy, bindingMode);
                        }
                        for (unsigned int i = 0; i < e.trailing.size(); i++) {
                            /*const*/ auto& subPat = e.trailing[i];
                            const auto& varTy = maybeMonomorph(sd[sd.size() - e.trailing.size() + i].ent);
                            rv &= this->revisitInner(context, subPat, varTy, bindingMode);
                        }
                        break;
                    }
                    case HIRPatternData::TAG_PathNamed: {
                        auto& e = pattern.data.as_PathNamed();
                        auto possibleType = getPossibleTypeInner(context, pattern);
                        ASSERT_BUG(sp, possibleType, StringView("No type for named path pattern ") << pattern);
                        context.equateTypes(sp, ty, *possibleType);

                        if (e.subPatterns.empty()) {
                            // TODO: Check the field count?
                            rv = true;
                        } else {
                            const auto& sd = patternGetNamed(sp, e.path, e.binding);

                            auto ms = MonomorphStatePtr(context.crate.types, nullptr, &e.path.data.as_Generic().params, nullptr);
                            const HIRType* tmp;
                            auto maybeMonomorph = [&](const HIRType* fieldType) -> const HIRType* {
                                return (monomorphiseTypeNeeded(fieldType) ? (tmp = context.expandAssociatedTypes(sp, ms.monomorphType(sp, fieldType))) : fieldType);
                            };

                            rv = true;
                            for (auto& fieldPat : e.subPatterns) {
                                unsigned int fIdx = std::find_if(sd.begin(), sd.end(), [&](const HIRStructField& x) {
                                    return x.name == fieldPat.first;
                                }) - sd.begin();
                                if (fIdx == sd.size()) {
                                    ERROR(sp, E0000, StringView("Struct ") << e.path << StringView(" doesn't have a field ") << fieldPat.first);
                                }
                                const HIRType* fieldType = maybeMonomorph(sd[fIdx].ty);
                                rv &= this->revisitInner(context, fieldPat.second, fieldType, bindingMode);
                            }
                        }
                        break;
                    }
                }
                return rv;
            }

            static void createBindings(const Span& sp, Context& context, HIRPattern& pat) {
                for (const auto& pb : pat.bindings) {
                    context.addVar(sp, pb.slot, pb.name, context.ivars.newIvarTr());
                    // TODO: Ensure that there's no more bindings below this?
                }
                switch (pat.data.tag()) {
                    case HIRPatternData::TAG_Any: {
                        break;
                    }
                    case HIRPatternData::TAG_Value: {
                        break;
                    }
                    case HIRPatternData::TAG_Range: {
                        break;
                    }
                    case HIRPatternData::TAG_Box: {
                        auto& e = pat.data.as_Box();
                        createBindings(sp, context, *e.sub);
                        break;
                    }
                    case HIRPatternData::TAG_Deref: {
                        auto& e = pat.data.as_Deref();
                        createBindings(sp, context, *e.sub);
                        break;
                    }
                    case HIRPatternData::TAG_Ref: {
                        auto& e = pat.data.as_Ref();
                        createBindings(sp, context, *e.sub);
                        break;
                    }
                    case HIRPatternData::TAG_Tuple: {
                        auto& e = pat.data.as_Tuple();
                        for (auto& subpat : e.subPatterns) {
                            createBindings(sp, context, subpat);
                        }
                        break;
                    }
                    case HIRPatternData::TAG_SplitTuple: {
                        auto& e = pat.data.as_SplitTuple();
                        for (auto& subpat : e.leading) {
                            createBindings(sp, context, subpat);
                        }
                        for (auto& subpat : e.trailing) {
                            createBindings(sp, context, subpat);
                        }
                        break;
                    }
                    case HIRPatternData::TAG_Slice: {
                        auto& e = pat.data.as_Slice();
                        for (auto& sub : e.subPatterns) {
                            createBindings(sp, context, sub);
                        }
                        break;
                    }
                    case HIRPatternData::TAG_SplitSlice: {
                        auto& e = pat.data.as_SplitSlice();
                        for (auto& sub : e.leading) {
                            createBindings(sp, context, sub);
                        }
                        if (e.extraBind.isValid()) {
                            const auto& pb = e.extraBind;
                            context.addVar(sp, pb.slot, pb.name, context.ivars.newIvarTr());
                        }
                        for (auto& sub : e.trailing) {
                            createBindings(sp, context, sub);
                        }
                        break;
                    }
                    case HIRPatternData::TAG_PathValue: {
                        break;
                    }
                    case HIRPatternData::TAG_PathTuple: {
                        auto& e = pat.data.as_PathTuple();
                        for (auto& subpat : e.leading) {
                            createBindings(sp, context, subpat);
                        }
                        for (auto& subpat : e.trailing) {
                            createBindings(sp, context, subpat);
                        }
                        break;
                    }
                    case HIRPatternData::TAG_PathNamed: {
                        auto& e = pat.data.as_PathNamed();
                        for (auto& fieldPat : e.subPatterns) {
                            createBindings(sp, context, fieldPat.second);
                        }
                        break;
                    }
                    case HIRPatternData::TAG_Or: {
                        auto& e = pat.data.as_Or();
                        BUG_ASSERT(e.size() > 0);
                        createBindings(sp, context, e[0]);
                        // TODO: Ensure that the other arms have the same binding set
                        break;
                    }
                }
            }
        };

        MatchErgonomicsRevisit::createBindings(sp, *this, pat);
        DEBUG(StringView("Handle match ergonomics - ") << pat << StringView(" with ") << type);
        auto revisit = box$((MatchErgonomicsRevisit{sp, isIrrefutable, type, pat}));
        if (!revisit->revisit(*this, false)) {
            this->addRevisitAdv(mv$(revisit));
        }
        return;
    }

    this->handlePatternDirectInner(sp, pat, type);
}

void Context::handlePatternDirectInner(const Span& sp, HIRPattern& pat, const HIRType* type) {
    TRACE_FUNCTION_F(StringView("pat = ") << pat << StringView(", type = ") << type);
    for (const auto& pb : pat.bindings) {
        this->addBindingInner(sp, pb, type);
    }

    struct H {
        static void handleValue(Context& context, const Span& sp, const HIRType* type, HIRPattern::Value& val) {
            switch (val.tag()) {
                case HIRPattern::Value::TAG_Integer: {
                    auto& v = val.as_Integer();
                    DEBUG(StringView("Integer ") << v.type);
                    // TODO: Apply an ivar bound? (Require that this ivar be an integer?)
                    if (v.type != HIRCoreType::Str) {
                        context.equateTypes(sp, type, context.crate.types.primitive(v.type));
                    }
                    break;
                }
                case HIRPattern::Value::TAG_Float: {
                    auto& v = val.as_Float();
                    DEBUG(StringView("Float ") << v.type);
                    // TODO: Apply an ivar bound? (Require that this ivar be a float?)
                    if (v.type != HIRCoreType::Str) {
                        context.equateTypes(sp, type, context.crate.types.primitive(v.type));
                    }
                    break;
                }
                case HIRPattern::Value::TAG_String: {
                    context.equateTypes(sp, type, context.crate.types.borrow(HIRBorrowType::Shared, context.crate.types.primitive(HIRCoreType::Str)));
                    break;
                }
                case HIRPattern::Value::TAG_ByteString: {
                    // TODO: Check the type.
                    break;
                }
                case HIRPattern::Value::TAG_Named: {
                    auto& v = val.as_Named();
                    if (v.binding && v.path.data.is_UfcsKnown()) {
                        const auto& pe = v.path.data.as_UfcsKnown();
                        auto ms = MonomorphStatePtr(context.crate.types, pe.type, &pe.trait.params, nullptr);
                        context.equateTypes(sp, type, ms.monomorphType(sp, v.binding->type));
                    }
                    break;
                }
            }
        }

        static const HIRType* getPathType(Context& context, const Span& sp, HIRPath& path, const HIRPattern::PathBinding& binding) {
            switch (binding.tag()) {
                case HIRPatternPathBinding::TAG_Unbound: {
                    auto& _ = binding.as_Unbound();
                    BUG(sp, StringView(""));
                    break;
                }
                case HIRPatternPathBinding::TAG_Struct: {
                    auto& be = binding.as_Struct();
                    auto& p = path.data.as_Generic();
                    BUG_ASSERT(be);
                    context.addIvarsParams(p.params);
                    return context.crate.types.path(p.clone(), HIRTypePathBinding(be));
                }
                case HIRPatternPathBinding::TAG_Union: {
                    auto& be = binding.as_Union();
                    auto& p = path.data.as_Generic();
                    BUG_ASSERT(be);
                    context.addIvarsParams(p.params);
                    return context.crate.types.path(p.clone(), HIRTypePathBinding(be));
                }
                case HIRPatternPathBinding::TAG_Enum: {
                    auto& be = binding.as_Enum();
                    auto& p = path.data.as_Generic();
                    BUG_ASSERT(be.ptr);
                    context.addIvarsParams(p.params);
                    return context.crate.types.path(getParentPath(p), HIRTypePathBinding(be.ptr));
                }
            }
            UNREACHABLE();
        }
    };

    switch (pat.data.tag()) {
        case HIRPatternData::TAG_Any: {
            break;
        }
        case HIRPatternData::TAG_Value: {
            auto& e = pat.data.as_Value();
            H::handleValue(*this, sp, type, e.val);
            break;
        }
        case HIRPatternData::TAG_Range: {
            auto& e = pat.data.as_Range();
            if (e.start) {
                H::handleValue(*this, sp, type, *e.start);
            }
            if (e.end) {
                H::handleValue(*this, sp, type, *e.end);
            }
            break;
        }
        case HIRPatternData::TAG_Box: {
            auto& e = pat.data.as_Box();
            if (langBox == HIRSimplePath()) {
                ERROR(sp, E0000, StringView("Use of `box` pattern without the `owned_box` lang item"));
            }
            const auto& ty = this->getType(type);

            if (const auto* te = ty->opt_Path()) {
                if ((te->path.data.is_Generic() && (te->path.data.as_Generic().path == langBox))) {
                    const auto& inner = te->path.data.as_Generic().params.types.at(0);
                    this->handlePatternDirectInner(sp, *e.sub, inner);
                    break;
                }
            }

            auto inner = this->ivars.newIvarTr();
            this->handlePatternDirectInner(sp, *e.sub, inner);
            HIRGenericPath path{langBox, HIRPathParams(mv$(inner))};
            this->equateTypes(sp, type, crate.types.path(mv$(path), HIRTypePathBinding(&crate.getStructByPath(sp, langBox))));
            break;
        }
        case HIRPatternData::TAG_Deref: {
            auto& e = pat.data.as_Deref();
            const auto* ty = this->getType(type);
            if (const auto* inner = resolve.typeIsOwnedBox(sp, ty)) {
                e.kind = HIRPattern::DerefKind::Box;
                e.targetType = inner;
                this->handlePatternDirectInner(sp, *e.sub, inner);
                break;
            }
            const auto result = resolve.autoderefStep(sp, ty);
            if (result.result != TraitResolution::AutoderefResult::Match || !result.implType) {
                ERROR(sp, E0000, StringView("Type ") << ty << StringView(" cannot be used in a deref pattern"));
            }
            equateTypes(sp, ty, *result.implType);
            equateTypesAssoc(sp, result.target, crate.getLangItemPath(sp, "deref"), {}, ty, "Target", {}, true, TypeckPrimitiveOperator::Deref);
            addTraitBound(sp, ty, crate.getLangItemPath(sp, "deref_pure"), {});
            e.kind = HIRPattern::DerefKind::Shared;
            e.targetType = result.target;
            this->handlePatternDirectInner(sp, *e.sub, result.target);
            break;
        }
        case HIRPatternData::TAG_Ref: {
            auto& e = pat.data.as_Ref();
            const auto& ty = this->getType(type);
            if (const auto* te = ty->opt_Borrow()) {
                if (te->type != e.type) {
                    ERROR(sp, E0000, StringView("Pattern-type mismatch, &-ptr mutability mismatch"));
                }
                this->handlePatternDirectInner(sp, *e.sub, te->inner);
            } else {
                auto inner = this->ivars.newIvarTr();
                this->handlePatternDirectInner(sp, *e.sub, inner);
                this->equateTypes(sp, type, crate.types.borrow(e.type, inner));
            }
            break;
        }
        case HIRPatternData::TAG_Tuple: {
            auto& e = pat.data.as_Tuple();
            const auto& ty = this->getType(type);
            if (const auto* tep = ty->opt_Tuple()) {
                const auto& te = *tep;
                if (e.subPatterns.size() != te.length()) {
                    ERROR(sp, E0000, StringView("Tuple pattern with an incorrect number of fields, expected ") << e.subPatterns.size() << StringView("-tuple, got ") << ty);
                }

                for (unsigned int i = 0; i < e.subPatterns.size(); i++) {
                    this->handlePatternDirectInner(sp, e.subPatterns[i], te[i]);
                }
            } else {
                Vector<const HIRType*> subTypes;
                for (unsigned int i = 0; i < e.subPatterns.size(); i++) {
                    subTypes.pushBack(this->ivars.newIvarTr());
                    this->handlePatternDirectInner(sp, e.subPatterns[i], subTypes[i]);
                }
                this->equateTypes(sp, ty, crate.types.tuple(mv$(subTypes)));
            }
            break;
        }
        case HIRPatternData::TAG_SplitTuple: {
            auto& e = pat.data.as_SplitTuple();
            const auto& ty = this->getType(type);
            if (const auto* tep = ty->opt_Tuple()) {
                const auto& te = *tep;
                ASSERT_BUG(sp, e.leading.size() + e.trailing.size() <= te.length(), StringView("Invalid field count for split tuple pattern"));

                unsigned int tupIdx = 0;
                for (auto& subpat : e.leading) {
                    this->handlePatternDirectInner(sp, subpat, te[tupIdx++]);
                }
                tupIdx = te.length() - e.trailing.size();
                for (auto& subpat : e.trailing) {
                    this->handlePatternDirectInner(sp, subpat, te[tupIdx++]);
                }

                // TODO: Should this replace the pattern with a non-split?

                e.totalSize = te.length();
            } else {
                if (!ty->is_Infer()) {
                    ERROR(sp, E0000, StringView("Tuple pattern on non-tuple"));
                }

                Vector<const HIRType*> leadingTys;
                leadingTys.grow(e.leading.size());
                for (auto& subpat : e.leading) {
                    leadingTys.pushBack(this->ivars.newIvarTr());
                    this->handlePatternDirectInner(sp, subpat, leadingTys.back());
                }
                Vector<const HIRType*> trailingTys;
                for (auto& subpat : e.trailing) {
                    trailingTys.pushBack(this->ivars.newIvarTr());
                    this->handlePatternDirectInner(sp, subpat, trailingTys.back());
                }

                struct SplitTuplePatRevisit: public Revisitor {
                    Span sp;
                    const HIRType* outerTy;
                    Vector<const HIRType*> leadingTys;
                    Vector<const HIRType*> trailingTys;
                    unsigned int& patTotalSize;

                    SplitTuplePatRevisit(Span sp, const HIRType* outer, Vector<const HIRType*> leading, Vector<const HIRType*> trailing, unsigned int& patTotalSize)
                        : sp(mv$(sp))
                        , outerTy(mv$(outer))
                        , leadingTys(mv$(leading))
                        , trailingTys(mv$(trailing))
                        , patTotalSize(patTotalSize)
                    {
                    }

                    const Span& span() const override {
                        return sp;
                    }

                    void fmt(ZeroCopyOutput& os) const override {
                        os << StringView("SplitTuplePatRevisit { ") << outerTy << StringView(" = (") << leadingTys << StringView(", ..., ") << trailingTys << StringView(") }");
                    }

                    bool revisit(Context& context, bool isFallback) override {
                        const auto& ty = context.getType(outerTy);
                        if (ty->is_Infer()) {
                            return false;
                        } else if (const auto* tep = ty->opt_Tuple()) {
                            const auto& te = *tep;
                            if (te.length() < leadingTys.length() + trailingTys.length()) {
                                ERROR(sp, E0000, StringView("Tuple pattern too large for tuple"));
                            }
                            for (unsigned int i = 0; i < leadingTys.length(); i++) {
                                context.equateTypes(sp, te[i], leadingTys[i]);
                            }
                            unsigned int ofs = te.length() - trailingTys.length();
                            for (unsigned int i = 0; i < trailingTys.length(); i++) {
                                context.equateTypes(sp, te[ofs + i], trailingTys[i]);
                            }
                            patTotalSize = te.length();
                            return true;
                        } else {
                            ERROR(sp, E0000, StringView("Tuple pattern on non-tuple - ") << ty);
                        }
                    }
                };

                this->addRevisitAdv(box$((SplitTuplePatRevisit{sp, ty, mv$(leadingTys), mv$(trailingTys), e.totalSize})));
            }
            break;
        }
        case HIRPatternData::TAG_Slice: {
            auto& e = pat.data.as_Slice();
            const auto& ty = this->getType(type);
            switch ((*ty).tag()) {
                default:
                    ERROR(sp, E0000, StringView("Slice pattern on non-array/-slice - ") << ty);
                case HIRType::TAG_Slice: {
                    auto& te = (*ty).as_Slice();
                    for (auto& sub : e.subPatterns) {
                        this->handlePatternDirectInner(sp, sub, te.inner);
                    }
                    break;
                }
                case HIRType::TAG_Array: {
                    auto& te = (*ty).as_Array();
                    for (auto& sub : e.subPatterns) {
                        this->handlePatternDirectInner(sp, sub, te.inner);
                    }
                    break;
                }
                case HIRType::TAG_Infer: {
                    auto inner = this->ivars.newIvarTr();
                    for (auto& sub : e.subPatterns) {
                        this->handlePatternDirectInner(sp, sub, inner);
                    }

                    struct SlicePatRevisit: public Revisitor {
                        Span sp;
                        const HIRType* inner;
                        const HIRType* type;
                        unsigned int size;

                        SlicePatRevisit(Span sp, const HIRType* inner, const HIRType* type, unsigned int size)
                            : sp(mv$(sp))
                            , inner(mv$(inner))
                            , type(mv$(type))
                            , size(size)
                        {
                        }

                        const Span& span() const override {
                            return sp;
                        }

                        void fmt(ZeroCopyOutput& os) const override {
                            os << StringView("SlicePatRevisit { ") << inner << StringView(", ") << type << StringView(", ") << size;
                        }

                        bool revisit(Context& context, bool isFallback) override {
                            const auto& ty = context.getType(type);
                            switch ((*ty).tag()) {
                                default:
                                    ERROR(sp, E0000, StringView("Slice pattern on non-array/-slice - ") << ty);
                                case HIRType::TAG_Infer: {
                                    if (!isFallback) {
                                        return false;
                                    }
                                    /* A slice pattern without a rest binding matches exactly its
                                       own length, so once nothing else has determined the type it
                                       is an array of that length.  Committing to it is what lets
                                       the value being destructured resolve at all - here it is
                                       the only thing that can pick an `Into` target. */
                                    context.equateTypes(sp, ty, context.crate.types.array(inner, static_cast<u64>(size)));
                                    return true;
                                }
                                case HIRType::TAG_Slice: {
                                    auto& te = (*ty).as_Slice();
                                    context.equateTypes(sp, te.inner, inner);
                                    return true;
                                }
                                case HIRType::TAG_Array: {
                                    auto& te = (*ty).as_Array();
                                    if (te.size.as_Known() != size) {
                                        ERROR(sp, E0000, StringView("Slice pattern on an array if differing size"));
                                    }
                                    context.equateTypes(sp, te.inner, inner);
                                    return true;
                                }
                            }
                            UNREACHABLE();
                        }
                    };

                    this->addRevisitAdv(box$((SlicePatRevisit{sp, mv$(inner), ty, static_cast<unsigned int>(e.subPatterns.size())})));
                    break;
                }
            }
            break;
        }
        case HIRPatternData::TAG_SplitSlice: {
            auto& e = pat.data.as_SplitSlice();
            const HIRType* inner;
            unsigned int minLen = e.leading.size() + e.trailing.size();
            const auto& ty = this->getType(type);
            switch ((*ty).tag()) {
                default:
                    ERROR(sp, E0000, StringView("SplitSlice pattern on non-array/-slice - ") << ty);
                case HIRType::TAG_Slice: {
                    auto& te = (*ty).as_Slice();

                    // - TODO: Better new variable handling.
                    inner = te.inner;
                    if (e.extraBind.isValid()) {
                        this->addBindingInner(sp, e.extraBind, ty);
                    }
                    break;
                }
                case HIRType::TAG_Array: {
                    auto& te = (*ty).as_Array();
                    inner = te.inner;
                    if (te.size.as_Known() < minLen) {
                        ERROR(sp, E0000, StringView("Slice pattern on an array smaller than the pattern"));
                    }
                    unsigned extra_len = te.size.as_Known() - minLen;

                    if (e.extraBind.isValid()) {
                        this->addBindingInner(sp, e.extraBind, crate.types.array(inner, extra_len));
                    }
                    break;
                }
                case HIRType::TAG_Infer: {
                    inner = this->ivars.newIvarTr();
                    const HIRType* varTy;
                    if (e.extraBind.isValid()) {
                        varTy = this->ivars.newIvarTr();
                        this->addBindingInner(sp, e.extraBind, varTy);
                    }

                    struct SplitSlicePatRevisit: public Revisitor {
                        Span sp;
                        const HIRType* inner;
                        const HIRType* type;
                        const HIRType* varTy;
                        unsigned int minSize;

                        SplitSlicePatRevisit(Span sp, const HIRType* inner, const HIRType* type, const HIRType* varTy, unsigned int size)
                            : sp(mv$(sp))
                            , inner(mv$(inner))
                            , type(mv$(type))
                            , varTy(mv$(varTy))
                            , minSize(size)
                        {
                        }

                        const Span& span() const override {
                            return sp;
                        }

                        void fmt(ZeroCopyOutput& os) const override {
                            os << StringView("SplitSlice inner=") << inner << StringView(", outer=") << type << StringView(", binding=") << varTy << StringView(", ") << minSize;
                        }

                        bool revisit(Context& context, bool isFallback) override {
                            const auto& ty = context.getType(this->type);
                            switch ((*ty).tag()) {
                                default:
                                    ERROR(sp, E0000, StringView("Slice pattern on non-array/-slice - ") << ty);
                                case HIRType::TAG_Infer: {
                                    return false;
                                }
                                case HIRType::TAG_Slice: {
                                    auto& te = (*ty).as_Slice();
                                    context.equateTypes(this->sp, this->inner, te.inner);
                                    if (this->varTy != nullptr) {
                                        context.equateTypes(this->sp, this->varTy, ty);
                                    }
                                    break;
                                }
                                case HIRType::TAG_Array: {
                                    auto& te = (*ty).as_Array();
                                    context.equateTypes(this->sp, this->inner, te.inner);
                                    if (te.size.as_Known() < this->minSize) {
                                        ERROR(sp, E0000, StringView("Slice pattern on an array smaller than the pattern"));
                                    }
                                    unsigned extra_len = te.size.as_Known() - this->minSize;

                                    if (this->varTy != nullptr) {
                                        context.equateTypes(this->sp, this->varTy, context.crate.types.array(this->inner, extra_len));
                                    }
                                    break;
                                }
                            }
                            return true;
                        }
                    };

                    this->addRevisitAdv(box$((SplitSlicePatRevisit{sp, inner, ty, mv$(varTy), minLen})));
                    break;
                }
            }

            for (auto& sub : e.leading) {
                this->handlePatternDirectInner(sp, sub, inner);
            }
            for (auto& sub : e.trailing) {
                this->handlePatternDirectInner(sp, sub, inner);
            }
            break;
        }
        case HIRPatternData::TAG_PathValue: {
            auto& e = pat.data.as_PathValue();
            this->equateTypes(sp, type, H::getPathType(*this, sp, e.path, e.binding));
            switch (e.binding.tag()) {
                case HIRPatternPathBinding::TAG_Unbound: {
                    auto& _ = e.binding.as_Unbound();
                    BUG(sp, StringView(""));
                    break;
                }
                case HIRPatternPathBinding::TAG_Struct: {
                    BUG_ASSERT(e.binding.as_Struct()->data.is_Unit());
                    break;
                }
                case HIRPatternPathBinding::TAG_Union: {
                    BUG(sp, StringView("PathValue used for union"));
                    break;
                }
                case HIRPatternPathBinding::TAG_Enum: {
                    auto& be = e.binding.as_Enum();
                    if (const auto* ee = be.ptr->data.opt_Data()) {
                        ASSERT_BUG(sp, be.varIdx < ee->size(), StringView(""));
                        const auto& var = (*ee)[be.varIdx];
                        if (var.type->is_Tuple() && var.type->as_Tuple().length() == 0) {
                        } else {
                            // TODO: Error here due to invalid variant type
                        }
                    }
                    break;
                }
            }
            break;
        }
        case HIRPatternData::TAG_PathTuple: {
            auto& e = pat.data.as_PathTuple();
            this->equateTypes(sp, type, H::getPathType(*this, sp, e.path, e.binding));

            const auto& sd = patternGetTuple(sp, e.path, e.binding);

            auto ms = MonomorphStatePtr(crate.types, nullptr, &e.path.data.as_Generic().params, nullptr);
            const HIRType* tmp;
            auto maybeMonomorph = [&](const HIRType* ty) -> const HIRType* {
                if (monomorphiseTypeNeeded(ty)) {
                    return (tmp = ms.monomorphType(sp, ty));
                } else {
                    return ty;
                }
            };
            if (e.isSplit) {
                ASSERT_BUG(sp, e.leading.size() + e.trailing.size() <= sd.size(), StringView("PathTuple size mismatch, expected at most ") << sd.size() << StringView(" fields but got ") << e.leading.size() + e.trailing.size());
            } else {
                ASSERT_BUG(sp, e.leading.size() == sd.size(), StringView("PathTuple size mismatch, expected ") << sd.size() << StringView(" fields but got ") << e.leading.size());
                BUG_ASSERT(e.trailing.size() == 0);
            }
            e.totalSize = sd.size();

            for (size_t i = 0; i < e.leading.size(); i++) {
                /*const*/ auto& subPat = e.leading[i];
                this->handlePatternDirectInner(sp, subPat, maybeMonomorph(sd[i].ent));
            }
            for (size_t i = 0; i < e.trailing.size(); i++) {
                /*const*/ auto& subPat = e.trailing[i];
                this->handlePatternDirectInner(sp, subPat, maybeMonomorph(sd[sd.size() - e.trailing.size() + i].ent));
            }
            break;
        }
        case HIRPatternData::TAG_PathNamed: {
            auto& e = pat.data.as_PathNamed();
            this->equateTypes(sp, type, H::getPathType(*this, sp, e.path, e.binding));

            if (e.isWildcard()) {
                return;
            }

            const auto& sd = patternGetNamed(sp, e.path, e.binding);

            auto ms = MonomorphStatePtr(crate.types, nullptr, &e.path.data.as_Generic().params, nullptr);

            for (auto& fieldPat : e.subPatterns) {
                unsigned int fIdx = std::find_if(sd.begin(), sd.end(), [&](const auto& x) {
                    return x.name == fieldPat.first;
                }) - sd.begin();
                if (fIdx == sd.size()) {
                    ERROR(sp, E0000, StringView("Struct ") << e.path << StringView(" doesn't have a field ") << fieldPat.first);
                }
                const HIRType* fieldType = sd[fIdx].ty;
                if (monomorphiseTypeNeeded(fieldType)) {
                    auto fieldTypeMono = ms.monomorphType(sp, fieldType);
                    this->handlePatternDirectInner(sp, fieldPat.second, fieldTypeMono);
                } else {
                    this->handlePatternDirectInner(sp, fieldPat.second, fieldType);
                }
            }
            break;
        }
        case HIRPatternData::TAG_Or: {
            auto& e = pat.data.as_Or();
            for (auto& subpat : e) {
                this->handlePatternDirectInner(sp, subpat, type);
            }
            break;
        }
    }
}

Context::Coercion::Coercion(unsigned ruleIdx, const HIRType* leftTy, HIRExprNodeP* rightNodePtr)
    : ruleIdx(ruleIdx)
    , obligationSpan((*rightNodePtr)->span())
    , leftTy(leftTy)
    , rightNodePtr(rightNodePtr)
    , rightTy(nullptr)
    , op(SolverCoercionOp::Coercion)
{
}

Context::Coercion::Coercion(unsigned ruleIdx, const Span& span, const HIRType* leftTy, const HIRType* rightTy, SolverCoercionOp op)
    : ruleIdx(ruleIdx)
    , obligationSpan(span)
    , leftTy(leftTy)
    , rightNodePtr(nullptr)
    , rightTy(rightTy)
    , op(op)
{
}

const Span& Context::Coercion::span() const {
    return rightNodePtr ? (*rightNodePtr)->span() : obligationSpan;
}

const HIRType* Context::Coercion::sourceType() const {
    return rightNodePtr ? (*rightNodePtr)->resType : rightTy;
}

void Context::equateTypesCoerce(const Span& sp, const HIRType* l, HIRExprNodeP& nodePtr) {
    const auto* destination = this->ivars.getType(l);
    const auto* destinationInfer = destination->opt_Infer();
    const bool destinationRequiresSized = destinationInfer
        ? destinationInfer->index < this->ivarsSized.length() && this->ivarsSized[destinationInfer->index]
        : this->resolve.typeIsSized(sp, destination) == SolverCertainty::Proven;
    if (destinationRequiresSized) {
        this->requireSized(sp, nodePtr->resType);
    }
    this->linkCoerce.push_back(std::make_unique<Coercion>(this->nextRuleIdx++, l, &nodePtr));
    DEBUG(StringView("++ ") << *this->linkCoerce.back());
    this->ivars.markChange();
}

void Context::addCoercionObligation(const Span& sp, const HIRType* destination, const HIRType* source, SolverCoercionOp op) {
    const auto duplicate = std::any_of(linkCoerce.begin(), linkCoerce.end(), [&](const auto& obligation) {
        return !obligation->rightNodePtr && obligation->op == op && obligation->leftTy == destination && obligation->rightTy == source;
    });
    if (!duplicate) {
        linkCoerce.push_back(std::make_unique<Coercion>(nextRuleIdx++, sp, destination, source, op));
        DEBUG(StringView("++ ") << *linkCoerce.back());
        ivars.markChange();
    }
}

void Context::equateTypesAssoc(const Span& sp, const HIRType* l, const HIRSimplePath& trait, HIRPathParams pp, const HIRType* implTy, const char* name, const HIRPathParams& atyPp, bool isOp, TypeckPrimitiveOperator operatorKind) {
    const auto& traitDef = crate.getTraitByPath(sp, trait);
    auto monomorph = MonomorphStatePtr(crate.types, implTy, &pp, nullptr);
    while (pp.types.size() < traitDef.params.types.size()) {
        const auto& defaultType = traitDef.params.types[pp.types.size()].defaultValue;
        if (defaultType == nullptr || defaultType->is_Infer()) {
            break;
        }
        pp.types.push_back(monomorph.monomorphType(sp, defaultType));
    }

    const RcString ruleName(name);
    MonomorphEraseHrls eraseHrls(crate.types);
    const auto ruleLeftTy = eraseHrls.monomorphType(sp, l, true);
    auto ruleParams = eraseHrls.monomorphPathParams(sp, pp, true);
    const auto ruleImplTy = eraseHrls.monomorphType(sp, implTy, true);
    auto ruleAtyPp = eraseHrls.monomorphPathParams(sp, atyPp, true);
    const auto key = associatedIndexKey(ruleLeftTy, trait, ruleImplTy, ruleName, isOp, operatorKind);
    if (const auto* candidates = this->linkAssocIndex.find(key)) {
        for (const auto index : *candidates) {
            const auto& a = this->linkAssoc[index];
            if (ruleName != "" && a.leftTy != ruleLeftTy) {
                continue;
            }
            if (a.trait != trait) {
                continue;
            }
            if (a.params != ruleParams) {
                continue;
            }
            if (a.implTy != ruleImplTy) {
                continue;
            }
            if (a.atyPp != ruleAtyPp) {
                continue;
            }
            if (a.name != ruleName) {
                continue;
            }
            if (a.isOperator != isOp) {
                continue;
            }
            if (a.operatorKind != operatorKind) {
                continue;
            }

            DEBUG(StringView("(DUPLICATE ") << a << StringView(")"));
            return;
        }
    }
    visitTyWith(implTy, [&](const HIRType* ty) {
        if (const auto* path = ty->opt_Path()) {
            if (const auto* projection = path->path.data.opt_UfcsKnown()) {
                this->addTraitBound(sp, projection->type, projection->trait.path, projection->trait.params.clone());
            }
        }
        return false;
    });
    this->linkAssoc.push_back(Associated{this->nextRuleIdx++, sp, ruleLeftTy, trait.clone(), mv$(ruleParams), ruleImplTy, ruleName, mv$(ruleAtyPp), isOp, operatorKind});
    this->indexAssociated(this->linkAssoc.size() - 1);
    DEBUG(StringView("++ ") << this->linkAssoc.back());
    this->ivars.markChange();
}

u64 Context::associatedIndexKey(const HIRType* leftTy, const HIRSimplePath& trait, const HIRType* implTy, RcString name, bool isOperator, TypeckPrimitiveOperator operatorKind) {
    return mix(name == "" ? implTy : leftTy, implTy, trait.rawData()) ^ (static_cast<u64>(name.rawId()) << 8) ^ (static_cast<u64>(operatorKind) << 1) ^ isOperator;
}

u64 Context::associatedIndexKey(const Associated& rule) {
    return associatedIndexKey(rule.leftTy, rule.trait, rule.implTy, rule.name, rule.isOperator, rule.operatorKind);
}

void Context::indexAssociated(unsigned index) {
    const auto key = associatedIndexKey(linkAssoc[index]);
    auto* bucket = linkAssocIndex.find(key);
    if (!bucket) {
        bucket = linkAssocIndex.insert(key);
    }
    bucket->pushBack(index);
}

void Context::unindexAssociated(unsigned index, u64 key) {
    auto* bucket = linkAssocIndex.find(key);
    BUG_ASSERT(bucket);
    for (size_t i = 0; i < bucket->length(); i++) {
        if ((*bucket)[i] == index) {
            const auto replacement = bucket->popBack();
            if (i < bucket->length()) {
                bucket->mut(i) = replacement;
            }
            return;
        }
    }
    BUG_ASSERT(!"associated type rule is absent from its index");
}

void Context::storeAssociated(unsigned index, Associated rule, u64 oldKey) {
    const auto newKey = associatedIndexKey(rule);
    if (newKey != oldKey) {
        unindexAssociated(index, oldKey);
    }
    linkAssoc[index] = mv$(rule);
    if (newKey != oldKey) {
        indexAssociated(index);
    }
}

void Context::removeAssociated(unsigned index, u64 oldKey) {
    const auto& removed = linkAssoc[index];
    const bool unblocksCoercion = removed.operatorKind == TypeckPrimitiveOperator::Deref && std::any_of(linkCoerce.begin(), linkCoerce.end(), [&](const auto& coercion) {
        const auto* source = ivars.getType(coercion->sourceType());
        return visitTyWith(source, [&](const HIRType* inner) {
            return ivars.typesEqual(removed.leftTy, inner);
        });
    });
    unindexAssociated(index, oldKey);
    const auto last = static_cast<unsigned>(linkAssoc.size() - 1);
    if (index != last) {
        const auto movedKey = associatedIndexKey(linkAssoc[last]);
        unindexAssociated(last, movedKey);
        linkAssoc[index] = mv$(linkAssoc.back());
        indexAssociated(index);
    }
    linkAssoc.pop_back();
    if (unblocksCoercion) {
        ivars.markChange();
    }
}

void Context::selectWellFormed(const Span& sp, const HIRType* type) {
    visitTyWith(type, [&](const HIRType* inner) {
        const auto* path = inner->opt_Path();
        const auto* projection = path ? path->path.data.opt_UfcsKnown() : nullptr;
        if (!projection) {
            return false;
        }
        resolve.selectTraitGoal(sp, projection->trait.path, projection->trait.params, projection->type, [&](SolverSelection selection) {
            applySolverResponse(sp, selection.effects);
            equateTypes(sp, projection->type, selection.impl.getImplType(crate.types));
            auto responseParams = selection.impl.getTraitParams(crate.types);
            ASSERT_BUG(sp, projection->trait.params.types.size() == responseParams.types.size(), StringView("WF response type parameter count mismatch"));
            ASSERT_BUG(sp, projection->trait.params.values.size() == responseParams.values.size(), StringView("WF response const parameter count mismatch"));
            for (size_t i = 0; i < responseParams.types.size(); i++) {
                equateTypes(sp, projection->trait.params.types[i], responseParams.types[i]);
            }
            for (size_t i = 0; i < responseParams.values.size(); i++) {
                equateValues(sp, projection->trait.params.values[i], responseParams.values[i]);
            }
            return true;
        }, {.allowInferInputs = true, .ambiguity = SolverAmbiguityPolicy::Report});
        return false;
    });
}

void Context::addRevisit(HIRExprNode& node) {
    this->toVisit.pushBack(&node);
}

void Context::addRevisitAdv(std::unique_ptr<Revisitor> entPtr) {
    this->advRevisits.push_back(mv$(entPtr));
    this->ivars.markChange();
}

void Context::requireSized(const Span& sp, const HIRType* ty_) {
    const auto& ty = ivars.getType(ty_);
    TRACE_FUNCTION_F(ty_ << StringView(" -> ") << ty);
    const auto sized = resolve.typeIsSized(sp, ty);
    if (sized == SolverCertainty::NoSolution) {
        ERROR(sp, E0000, StringView("Unsized type not valid here - ") << ty);
    }
    if (const auto* e = ty->opt_Infer()) {
        switch (e->tyClass) {
            case HIRInferClass::Integer:
            case HIRInferClass::Float:
                break;
            default:
                // TODO: Flag for future checking
                ASSERT_BUG(sp, e->index != ~0u, StringView("Unbound ivar ") << ty);
                while (e->index >= ivarsSized.length()) {
                    ivarsSized.pushBack(false);
                }
                ivarsSized.mut(e->index) = true;
                break;
        }
    } else if (const auto* e = ty->opt_Path()) {
        const HIRGenericParams* paramsDef = nullptr;
        switch (e->binding.tag()) {
            case HIRTypePathBinding::TAG_Unbound: {
                // TODO: Add a trait check rule
                paramsDef = nullptr;
                break;
            }
            case HIRTypePathBinding::TAG_Opaque: {
                paramsDef = nullptr;
                break;
            }
            case HIRTypePathBinding::TAG_ExternType: {
                paramsDef = &emptyGenericParams;
                break;
            }
            case HIRTypePathBinding::TAG_Enum: {
                auto& pb = e->binding.as_Enum();
                paramsDef = &pb->params;
                break;
            }
            case HIRTypePathBinding::TAG_Union: {
                auto& pb = e->binding.as_Union();
                paramsDef = &pb->params;
                break;
            }
            case HIRTypePathBinding::TAG_Struct: {
                auto& pb = e->binding.as_Struct();
                paramsDef = &pb->params;

                switch (pb->structMarkings.dstType) {
                    case HIRStructMarkings::DstType::Possible:
                        if (sized != SolverCertainty::Proven) {
                            this->requireSized(sp, e->path.data.as_Generic().params.types.at(pb->structMarkings.unsizedParam));
                        }
                        break;
                    case HIRStructMarkings::DstType::Projection: {
                        const HIRType* tailTpl = nullptr;
                        switch (pb->data.tag()) {
                            case HIRStructData::TAG_Unit:
                                BUG(sp, StringView("Potentially-unsized unit struct ") << ty);
                            case HIRStructData::TAG_Tuple:
                                tailTpl = pb->data.as_Tuple().at(pb->structMarkings.unsizedField).ent;
                                break;
                            case HIRStructData::TAG_Named:
                                tailTpl = pb->data.as_Named().at(pb->structMarkings.unsizedField).ty;
                                break;
                        }
                        const auto& params = e->path.data.as_Generic().params;
                        auto tailTy = MonomorphStatePtr(crate.types, ty, &params, nullptr).monomorphType(sp, tailTpl);
                        tailTy = this->expandAssociatedTypes(sp, mv$(tailTy));
                        if (sized != SolverCertainty::Proven) {
                            this->requireSized(sp, tailTy);
                        }
                        break;
                    }
                    case HIRStructMarkings::DstType::None:
                    case HIRStructMarkings::DstType::Slice:
                    case HIRStructMarkings::DstType::TraitObject:
                        break;
                }
                break;
            }
        }

        if (paramsDef) {
            const auto& gpTys = e->path.data.as_Generic().params.types;
            for (size_t i = 0; i < gpTys.size(); i++) {
                if (paramsDef->types.at(i).isSized) {
                    this->requireSized(sp, gpTys[i]);
                }
            }
        }
    } else if (const auto* e = ty->opt_Tuple()) {
        for (const auto& ity : *e) {
            this->requireSized(sp, ity);
        }
    } else if (const auto* e = ty->opt_Array()) {
        this->requireSized(sp, e->inner);
    }
}

void Context::addIvarDefault(const Span& sp, unsigned int ivarIndex, const HIRType* type) {
    const auto* resolved = ivars.getType(ivarIndex);
    const auto* infer = resolved->opt_Infer();
    if (!infer || infer->index == ~0u) {
        DEBUG(StringView("IVar ") << ivarIndex << StringView(" is already ") << resolved);
        return;
    }
    const auto duplicate = std::any_of(ivarDefaults.begin(), ivarDefaults.end(), [&](const auto& existing) {
        return existing.index == infer->index && existing.type == type;
    });
    if (!duplicate) {
        ivarDefaults.push_back({infer->index, type});
    }
}

void Context::addVar(const Span& sp, unsigned int index, const RcString& name, const HIRType* type) {
    DEBUG(StringView("(") << index << StringView(" ") << name << StringView(" : ") << type << StringView(")"));
    BUG_ASSERT(index != ~0u);
    ASSERT_BUG(sp, type != nullptr, StringView("Unset ivar in variable type"));
    if (bindings.size() <= index) {
        const auto oldSize = bindings.size();
        bindings.resize(index + 1);
        for (auto i = oldSize; i < bindings.size(); i++) {
            bindings[i].ty = crate.types.unit();
        }
    }
    if (bindings[index].name == "") {
        bindings[index] = Binding{name, mv$(type)};
    } else {
        ASSERT_BUG(sp, bindings[index].name == name, StringView(""));
        this->equateTypes(sp, bindings[index].ty, type);
    }
}

const HIRType* Context::getVar(const Span& sp, unsigned int idx) const {
    if (idx < this->bindings.size()) {
        ASSERT_BUG(sp, this->bindings[idx].ty != nullptr, StringView("Local #") << idx << StringView(" `") << this->bindings[idx].name << StringView("` with no populated type"));
        return this->bindings[idx].ty;
    } else {
        BUG(sp, StringView("get_var - Binding index out of range - ") << idx << StringView(" >=") << this->bindings.size());
    }
}

HIRExprNodeP Context::createAutoderef(HIRExprNodeP valNode, const HIRType* tyDst) const {
    const auto& span = valNode->span();
    const auto& tySrc = valNode->resType;
    if (getType(tySrc)->is_Array()) {
        ASSERT_BUG(span, tyDst->is_Slice(), StringView("Array should only ever autoderef to Slice"));

        const auto borrowType = HIRBorrowType::Shared;
        auto tySrcBorrow = crate.types.borrow(borrowType, tySrc);
        auto tyDstBorrow = crate.types.borrow(borrowType, tyDst);
        auto tyDstBorrowCopy = tyDstBorrow;

        valNode = mkExprnodep(crate.pool->make<HIRExprNodeBorrow>(span, borrowType, mv$(valNode)), mv$(tySrcBorrow));
        auto* unsizeNode = crate.pool->make<HIRExprNodeUnsize>(span, mv$(valNode), mv$(tyDstBorrowCopy));
        unsizeNode->isArrayToSliceAdjustment = true;
        valNode = mkExprnodep(unsizeNode, mv$(tyDstBorrow));
        valNode = mkExprnodep(crate.pool->make<HIRExprNodeDeref>(span, mv$(valNode)), tyDst);
        DEBUG(StringView("- Array-to-slice adjustment ") << static_cast<const void*>(&*valNode) << StringView(" -> ") << valNode->resType);
    } else {
        valNode = mkExprnodep(crate.pool->make<HIRExprNodeDeref>(span, mv$(valNode)), mv$(tyDst));
        DEBUG(StringView("- Deref ") << static_cast<const void*>(&*valNode) << StringView(" -> ") << valNode->resType);
    }

    return valNode;
}

void Context::registerSolverObligation(const Span& sp, const HIRType* type, HIRTraitPath trait) {
    const auto* resolved = getType(type);
    const auto alreadyRegistered = std::any_of(solverObligations.begin(), solverObligations.end(), [&](const SolverObligation& obligation) {
        const auto* previous = getType(obligation.type);
        const auto* resolvedInfer = resolved->opt_Infer();
        const auto* previousInfer = previous->opt_Infer();
        const bool sameType = resolved == previous
            || resolved->equalsIgnoringRegions(previous)
            || (resolvedInfer && previousInfer && resolvedInfer->index == previousInfer->index);
        return sameType && obligation.trait == trait;
    });
    if (alreadyRegistered) {
        return;
    }
    solverObligations.push_back(SolverObligation{type, trait.clone()});
    if (trait.path.path == resolve.langSized() && !trait.path.params.hasParams() && trait.typeBounds.empty()) {
        requireSized(sp, type);
        return;
    }
    if (trait.typeBounds.empty()) {
        addTraitBound(sp, type, trait.path.path, std::move(trait.path.params));
        return;
    }
    for (auto& associated : trait.typeBounds) {
        equateTypesAssoc(sp, associated.second.type, trait.path.path, trait.path.params.clone(), type, associated.first.c_str(), associated.second.atyParams, false);
    }
}

void Context::registerClosureReturnObligation(const Span& sp, const HIRExprNodeClosure* closure, const HIRType* expected) {
    for (auto& obligation : closureReturnObligations) {
        if (obligation.closure != closure) {
            continue;
        }
        equateTypes(sp, obligation.expected, expected);
        resolve.setClosureReturnExpectation(closure, getType(obligation.expected));
        return;
    }
    closureReturnObligations.pushBack(ClosureReturnObligation{closure, std::move(expected)});
    resolve.setClosureReturnExpectation(closure, getType(expected));
    ivars.markChange();
}

const HIRType* Context::closureReturnExpectation(const HIRExprNodeClosure* closure) const {
    for (const auto& obligation : closureReturnObligations) {
        if (obligation.closure == closure) {
            return getType(obligation.expected);
        }
    }
    return nullptr;
}

void Context::recordNeverFallback(unsigned index) {
    while (index >= neverFallbackIvars.length()) {
        neverFallbackIvars.pushBack(false);
    }
    neverFallbackIvars.mut(index) = true;
}

bool Context::usedNeverFallback(const HIRType* type) const {
    const auto* infer = type->opt_Infer();
    if (!infer || infer->index == ~0u) {
        return false;
    }
    unsigned index = infer->index;
    for (size_t count = 0; count < ivars.ivars.size(); count++) {
        if (index < neverFallbackIvars.length() && neverFallbackIvars[index]) {
            return true;
        }
        if (index >= ivars.ivars.size() || !ivars.ivars[index].isAlias()) {
            return false;
        }
        index = ivars.ivars[index].alias;
    }
    BUG(Span(), StringView("Loop detected while checking never fallback for ivar ") << infer->index);
}

void Context::applySolverResponse(const Span& sp, const SolverResponse& response, std::vector<HIRExprNodeP>* coercionInputs) {
    ASSERT_BUG(sp, response.slots.typeInputs.size() == response.slots.types.size(), StringView("solver type slot response is malformed"));
    ASSERT_BUG(sp, response.slots.valueInputs.size() == response.slots.values.size(), StringView("solver value slot response is malformed"));
    const auto applyTypeEquality = [&](const HIRType* left, const HIRType* right) {
        const auto bindProvenProjection = [&](const HIRType* candidate, const HIRType* projection) {
            if (response.certainty != SolverCertainty::Proven) {
                return false;
            }
            const auto* infer = candidate->opt_Infer();
            const auto* path = projection->opt_Path();
            if (!infer || !infer->isLit() || !path || !path->path.data.is_UfcsKnown()) {
                return false;
            }
            /* The occurs check has to follow aliases: the projection carries whichever
               ivar the source wrote, which need not be the representative this side
               resolved to.  Comparing the types by pointer misses that and binds the
               variable to a type containing itself, and every later walk of it recurses
               until the stack is gone. */
            if (ivars.ivarOccursIn(infer->index, projection)) {
                return false;
            }
            if (infer->index < ivarsSized.length() && ivarsSized[infer->index]) {
                requireSized(sp, projection);
            }
            ivars.setIvarTo(infer->index, projection, true);
            return true;
        };

        const auto* resolvedLeft = ivars.getType(left);
        const auto* resolvedRight = ivars.getType(right);
        if (ivars.typesEqual(resolvedLeft, resolvedRight)) {
            return;
        }
        if (bindProvenProjection(resolvedLeft, resolvedRight) || bindProvenProjection(resolvedRight, resolvedLeft)) {
            return;
        }
        auto normalizedLeft = expandAssociatedTypes(sp, resolvedLeft);
        auto normalizedRight = expandAssociatedTypes(sp, resolvedRight);
        equateTypes(sp, normalizedLeft, normalizedRight);
    };
    for (size_t i = 0; i < response.slots.types.size(); i++) {
        applyTypeEquality(response.slots.typeInputs[i], response.slots.types[i]);
    }
    for (size_t i = 0; i < response.slots.values.size(); i++) {
        equateValues(sp, response.slots.valueInputs[i], response.slots.values[i]);
    }
    for (const auto& equality : response.equalities) {
        applyTypeEquality(equality.left, equality.right);
    }
    for (const auto& equality : response.valueEqualities) {
        equateValues(sp, equality.left, equality.right);
    }
    for (const auto& obligation : response.obligations) {
        registerSolverObligation(sp, obligation.type, obligation.trait.clone());
    }
    for (const auto& coercion : response.coercions) {
        if (coercion.sourceInput != ~0u) {
            if (coercionInputs && coercion.sourceInput < coercionInputs->size()) {
                equateTypesCoerce(sp, coercion.destination, (*coercionInputs)[coercion.sourceInput]);
            }
        } else {
            addCoercionObligation(sp, coercion.destination, coercion.source, coercion.op);
        }
    }
}

const HIRType* Context::expandAssociatedTypes(const Span& sp, const HIRType* input) {
    auto effects = makeCallable<SolverResponseCb>([&](SolverResponse response) {
        applySolverResponse(sp, response);
        return false;
    });
    return resolve.expandAssociatedTypes(sp, std::move(input), &effects);
}

void Context::expandAssociatedTypesParams(const Span& sp, HIRPathParams& params) {
    auto effects = makeCallable<SolverResponseCb>([&](SolverResponse response) {
        applySolverResponse(sp, response);
        return false;
    });
    resolve.expandAssociatedTypesParams(sp, params, &effects);
}

void Context::compactIvars(const Span& sp) {
    auto effects = makeCallable<SolverResponseCb>([&](SolverResponse response) {
        applySolverResponse(sp, response);
        return false;
    });
    resolve.compactIvars(ivars, &effects);
}

void processAssociatedRules(Context& context, const IvarCoercionIndex& coercionIndex) {
    DEBUG(StringView("--- Associated types"));
    unsigned int linkAssocIterLimit = context.linkAssoc.size() * 4;
    for (unsigned int i = 0; i < context.linkAssoc.size();) {
        const auto& indexedRule = context.linkAssoc[i];
        const auto indexedKey = context.associatedIndexKey(indexedRule);
        Context::Associated rule{
            indexedRule.ruleIdx,
            indexedRule.span,
            indexedRule.leftTy,
            indexedRule.trait.clone(),
            indexedRule.params.clone(),
            indexedRule.implTy,
            indexedRule.name,
            indexedRule.atyPp.clone(),
            indexedRule.isOperator,
            indexedRule.operatorKind,
        };
        rule.isAmbiguous = indexedRule.isAmbiguous;
        rule.stalledOn = indexedRule.stalledOn;

        DEBUG(StringView("- ") << rule);
        if (associatedStillStalled(context, coercionIndex, rule)) {
            context.storeAssociated(i, mv$(rule), indexedKey);
            i++;
            if (linkAssocIterLimit-- == 0) {
                DEBUG(StringView("link_assoc iteration limit exceeded"));
                break;
            }
            continue;
        }

        for (auto& ty : rule.params.types) {
            ty = context.expandAssociatedTypes(rule.span, mv$(ty));
        }
        if (rule.name != "") {
            rule.leftTy = context.expandAssociatedTypes(rule.span, mv$(rule.leftTy));
        }
        rule.implTy = context.expandAssociatedTypes(rule.span, mv$(rule.implTy));

        const auto result = checkAssociated(context, coercionIndex, rule);
        rule.isAmbiguous = result == AssociatedCheckResult::Ambiguous;

        if (result == AssociatedCheckResult::Complete) {
            DEBUG(StringView("- Consumed associated type rule ") << i << StringView("/") << context.linkAssoc.size() << StringView(" - ") << rule);
            context.removeAssociated(i, indexedKey);
        } else {
            if ((result == AssociatedCheckResult::Stalled || result == AssociatedCheckResult::Ambiguous) && setAssociatedStall(context, rule)) {
            } else {
                rule.stalledOn.clear();
            }
            context.storeAssociated(i, mv$(rule), indexedKey);
            i++;
        }

        if (linkAssocIterLimit-- == 0) {
            DEBUG(StringView("link_assoc iteration limit exceeded"));
            break;
        }
    }
}

void TypecheckCodeCS(const TypeckModuleState& ms, tArgs& args, const HIRType* resultType, HIRExprPtr& expr) {
    TRACE_FUNCTION;
    HIRExprNodeP rootPtr(expr.get());
    BUG_ASSERT(!ms.modPaths.empty());
    Context context{ms.wb, ms.implGenerics, ms.itemGenerics, ms.modPaths.back(), ms.currentTrait, ms.currentTraitImpl};
    if (resultType) {
        visitTyWith(resultType, [&](const HIRType* inner) {
            if (const auto* erased = inner->opt_ErasedType()) {
                if (const auto* fcn = erased->inner.opt_Fcn()) {
                    context.resolve.addDefiningFcnOrigin(fcn->origin);
                }
            }
            return false;
        });
    }
    for (const auto& path : expr.state->defineOpaque) {
        context.resolve.addDefiningOpaqueAlias(path);
    }

    TypecheckCodeCSEnumerateRules(context, ms, args, resultType, expr, rootPtr);

    const unsigned int MAX_ITERATIONS = 5000;
    unsigned int count = 0;
    while (context.takeChanged() /*&& context.has_rules()*/ && count < MAX_ITERATIONS) {
        TRACE_FUNCTION_F(StringView("=== PASS ") << count << StringView(" ==="));
        std::optional<IvarCoercionIndex> ivarCoercionIndex;
        if (!context.ivars.peekChanged()) {
            DEBUG(StringView("--- Coercion checking"));
            /* Settling one of these can make another ready, and the other may sit
               earlier in the list than the sweep has already reached.  Sweeping once
               leaves it for the next pass, and a chain ordered against the list settles
               one link per pass - each pass re-checking every obligation and revisit in
               the function.  Keep sweeping while the sweep still settles something; the
               decisions are the same ones in the same order, without a whole pass
               between each. */
            bool coercionSettled = true;
            while (coercionSettled) {
                coercionSettled = false;
                for (size_t i = 0; i < context.linkCoerce.size();) {
                    auto ent = mv$(context.linkCoerce[i]);
                    const auto& span = ent->span();
                    if (ent->rightNodePtr) {
                        auto& srcTy = (*ent->rightNodePtr)->resType;
                        srcTy = context.expandAssociatedTypes(span, mv$(srcTy)); // TODO: This was commented, why?
                    } else {
                        ent->rightTy = context.expandAssociatedTypes(span, mv$(ent->rightTy));
                    }
                    ent->leftTy = context.expandAssociatedTypes(span, mv$(ent->leftTy));
                    if (checkCoerce(context, *ent)) {
                        DEBUG(StringView("- Consumed coercion R") << ent->ruleIdx << StringView(" ") << ent->leftTy << StringView(" := ") << ent->sourceType());
                        context.linkCoerce.erase(context.linkCoerce.begin() + i);
                        coercionSettled = true;
                    } else {
                        context.linkCoerce[i] = mv$(ent);
                        ++i;
                    }
                }
            }
            if (!context.ivars.peekChanged()) {
                ivarCoercionIndex.emplace(context);
                processAssociatedRules(context, *ivarCoercionIndex);
            }
        }
        if (!context.ivars.peekChanged()) {
            Vector<const HIRType*> passStartIvars;
            passStartIvars.grow(context.ivars.ivars.size());
            for (unsigned int i = 0; i < context.ivars.ivars.size(); i++) {
                passStartIvars.pushBack(context.ivars.getType(i));
            }
            for (size_t i = 0; i < context.toVisit.length();) {
                HIRExprNode& node = *context.toVisit[i];
                ExprVisitorRevisit visitor{context, false, &passStartIvars, &*ivarCoercionIndex};
                DEBUG(StringView("> ") << static_cast<const void*>(&node) << StringView(" ") << typeid(node).name() << StringView(" -> ") << context.ivars.fmtType(node.resType));
                node.visit(visitor);
                if (visitor.nodeCompleted()) {
                    for (size_t j = i + 1; j < context.toVisit.length(); j++) {
                        context.toVisit.mut(j - 1) = context.toVisit[j];
                    }
                    context.toVisit.popBack();
                } else {
                    i++;
                }
            }
            {
                Vector<bool> advRevisitRemoveList;
                size_t len = context.advRevisits.size();
                for (size_t i = 0; i < len; i++) {
                    auto& ent = *context.advRevisits[i];
                    DEBUG(StringView("> ") << FMT_CB(os, ent.fmt(os)));
                    advRevisitRemoveList.pushBack(ent.revisit(context, /*is_fallback=*/false));
                }
                for (size_t i = len; i--;) {
                    if (advRevisitRemoveList[i]) {
                        context.advRevisits.erase(context.advRevisits.begin() + i);
                    }
                }
            }
        }

        if (!context.ivars.peekChanged()) {
            ivarCoercionIndex.emplace(context);
            processAssociatedRules(context, *ivarCoercionIndex);
            if (!context.ivars.peekChanged()) {
                ivarCoercionIndex.emplace(context);
            }
        }

        if (!context.ivars.peekChanged()) {
            DEBUG(StringView("--- IVar coercion effects"));
            for (unsigned int sourcePass = 0; sourcePass < 2; sourcePass++) {
                for (unsigned int i = 0; i < ivarCoercionIndex->refs.size(); i++) {
                    const bool hasConcreteSource = std::any_of((*ivarCoercionIndex)[i].endpoints.begin(), (*ivarCoercionIndex)[i].endpoints.end(), [&](const auto& endpoint) {
                        return endpoint.direction == SolverCoercionConstraint::Direction::InputIsDestination
                            && coercionEndpointCanDetermineType(context, endpoint)
                            && !context.ivars.typeContainsIvars(context.getType(endpoint.other));
                    });
                    if (hasConcreteSource != (sourcePass == 0)) {
                        continue;
                    }
                    finaliseIvarCoercions(context, *ivarCoercionIndex, i);
                }
            }
        }

        /* Final inference effects must precede fallback revisits: a fallback
         * revisit may diagnose an ambiguity which an identity commit (or an
         * exact joint effect) is specifically responsible for resolving. */
        if (!context.ivars.peekChanged()) {
            DEBUG(StringView("--- Final IVar effects"));
            for (unsigned int i = 0; i < ivarCoercionIndex->refs.size(); i++) {
                if (finaliseIvarCoercions(context, *ivarCoercionIndex, i, true, false)) {
                    break;
                }
            }
        }

        if (!context.ivars.peekChanged()) {
            DEBUG(StringView("--- Final IVar identity commits"));
            for (unsigned int i = 0; i < ivarCoercionIndex->refs.size(); i++) {
                if (finaliseIvarCoercions(context, *ivarCoercionIndex, i, true, true)) {
                    break;
                }
            }
        }

        /* An unsizing source is not an equality endpoint until ordinary
         * identity propagation has stabilised: an expected outer pointer can
         * still determine its pointee through a pending dereference/cast.
         * Only then may the no-op unsizing case use the same identity rule. */
        if (!context.ivars.peekChanged()) {
            DEBUG(StringView("--- Final IVar unsizing identity commits"));
            for (unsigned int i = 0; i < ivarCoercionIndex->refs.size(); i++) {
                if (finaliseIvarCoercions(context, *ivarCoercionIndex, i, true, true, true)) {
                    break;
                }
            }
        }

        if (!context.ivars.peekChanged()) {
            DEBUG(StringView("--- Node revisits (fallback)"));
            for (size_t i = 0; i < context.toVisit.length();) {
                HIRExprNode& node = *context.toVisit[i];
                ExprVisitorRevisit visitor{context, true, nullptr, &*ivarCoercionIndex};
                DEBUG(StringView("> ") << static_cast<const void*>(&node) << StringView(" ") << typeid(node).name() << StringView(" -> ") << context.ivars.fmtType(node.resType));
                node.visit(visitor);
                if (visitor.nodeCompleted()) {
                    for (size_t j = i + 1; j < context.toVisit.length(); j++) {
                        context.toVisit.mut(j - 1) = context.toVisit[j];
                    }
                    context.toVisit.popBack();
                } else {
                    i++;
                }
            }
            {
                Vector<bool> advRevisitRemoveList;
                size_t len = context.advRevisits.size();
                for (size_t i = 0; i < len; i++) {
                    auto& ent = *context.advRevisits[i];
                    DEBUG(StringView("> ") << FMT_CB(os, ent.fmt(os)));
                    advRevisitRemoveList.pushBack(ent.revisit(context, /*is_fallback=*/true));
                }
                for (size_t i = len; i--;) {
                    if (advRevisitRemoveList[i]) {
                        context.advRevisits.erase(context.advRevisits.begin() + i);
                    }
                }
            }
        }

        if (!context.ivars.peekChanged() && context.linkCoerce.empty()) {
            context.fallbackUnresolvedRpitType(rootPtr->span());
        }

        /* Generic and numeric defaults are language fallbacks, so they run
         * only after every final coercion effect and identity propagation has
         * had a chance to constrain the ivar through pending obligations. */
        if (!context.ivars.peekChanged()) {
            DEBUG(StringView("- Applying generic defaults"));
            for (size_t i = 0; i < context.ivarDefaults.size(); i++) {
                const auto* tyL = context.ivars.getType(context.ivarDefaults[i].index);
                const auto* infer = tyL->opt_Infer();
                if (!infer || infer->index == ~0u) {
                    continue;
                }
                bool firstForClass = true;
                const HIRType* uniqueDefault = nullptr;
                bool defaultsDisagree = false;
                for (size_t j = 0; j < context.ivarDefaults.size(); j++) {
                    const auto* member = context.ivars.getType(context.ivarDefaults[j].index);
                    const auto* memberInfer = member->opt_Infer();
                    if (!memberInfer || memberInfer->index != infer->index) {
                        continue;
                    }
                    if (j < i) {
                        firstForClass = false;
                        break;
                    }
                    const auto* candidate = context.getType(context.ivarDefaults[j].type);
                    if (uniqueDefault && !context.ivars.typesEqual(uniqueDefault, candidate)) {
                        defaultsDisagree = true;
                    } else {
                        uniqueDefault = candidate;
                    }
                }
                if (firstForClass && uniqueDefault && !defaultsDisagree) {
                    context.equateTypes(rootPtr->span(), tyL, uniqueDefault);
                }
            }
        }

        if (!context.ivars.peekChanged()) {
            bool appliedDefault = false;
            for (unsigned int i = 0; i < context.ivars.ivars.size(); i++) {
                if (!numericDefaultMustWait(context, i)) {
                    appliedDefault |= context.ivars.applyDefault(i);
                }
            }
            if (appliedDefault) {
                context.ivars.markChange();
            }
        }

        if (!context.ivars.peekChanged()) {
            DEBUG(StringView("--- Coercion consume"));
            if (!context.linkCoerce.empty()) {
                auto selected = std::find_if(context.linkCoerce.begin(), context.linkCoerce.end(), [&](const auto& coercion) {
                    return !context.getType(coercion->sourceType())->is_Diverge();
                });
                if (selected == context.linkCoerce.end()) {
                    selected = context.linkCoerce.begin();
                }
                auto ent = mv$(*selected);
                context.linkCoerce.erase(selected);

                const auto& sp = ent->span();
                const auto* srcTy = ent->sourceType();
                ent->leftTy = context.expandAssociatedTypes(sp, mv$(ent->leftTy));

                DEBUG(StringView("- Equate coercion R") << ent->ruleIdx << StringView(" ") << ent->leftTy << StringView(" := ") << srcTy);
                context.equateTypes(sp, ent->leftTy, srcTy);
            }
        }

        if (!context.ivars.peekChanged()) {
            DEBUG(StringView("- Applying defaults (unconditional)"));
            bool appliedDefault = false;
            for (unsigned int i = 0; i < context.ivars.ivars.size(); i++) {
                appliedDefault |= context.ivars.applyDefault(i);
            }
            if (appliedDefault) {
                context.ivars.markChange();
            }
        }

        count++;
        context.compactIvars(rootPtr->span());
    }
    if (count == MAX_ITERATIONS) {
        if (!context.hasRules()) {
            BUG(rootPtr->span(), StringView("Typecheck ran for too many iterations, max - ") << MAX_ITERATIONS);
        }
        WARNING(rootPtr->span(), W0000, StringView("Typecheck ran for too many iterations, max - ") << MAX_ITERATIONS);
    }

    if (context.hasRules()) {
        for (const auto& rule : context.linkAssoc) {
            if (!rule.isAmbiguous) {
                continue;
            }
            if (rule.name == "") {
                ERROR(rule.span, E0000, StringView("type annotations needed: cannot infer a type satisfying `") << context.ivars.fmtType(rule.implTy) << StringView(": ") << rule.trait << context.ivars.fmt(rule.params) << StringView("`"));
            } else {
                ERROR(rule.span, E0000, StringView("type annotations needed: cannot infer `") << context.ivars.fmtType(rule.leftTy) << StringView(" = <") << context.ivars.fmtType(rule.implTy) << StringView(" as ") << rule.trait << context.ivars.fmt(rule.params) << StringView(">::") << rule.name << StringView("`"));
            }
        }
        for (const auto& coercionP : context.linkCoerce) {
            const auto& coercion = *coercionP;
            const auto& sp = coercion.span();
            const auto* srcTy = coercion.sourceType();
            WARNING(sp, W0000, StringView("Spare Rule - ") << context.ivars.fmtType(coercion.leftTy) << StringView(" := ") << context.ivars.fmtType(srcTy));
        }
        for (const auto& rule : context.linkAssoc) {
            const auto& sp = rule.span;
            if (rule.name == "") {
                WARNING(sp, W0000, StringView("Spare Rule - ") << context.ivars.fmtType(rule.implTy) << StringView(" : ") << rule.trait << rule.params);
            } else {
                WARNING(sp, W0000, StringView("Spare Rule - ") << context.ivars.fmtType(rule.leftTy) << StringView(" = < ") << context.ivars.fmtType(rule.implTy) << StringView(" as ") << rule.trait << rule.params << StringView(" >::") << rule.name);
            }
        }
        // TODO: Print revisit rules and advanced revisit rules.
        for (const auto& node : context.toVisit) {
            const auto& sp = node->span();
            WARNING(sp, W0000, StringView("Spare rule - ") << FMT_CB(os, {
                                   ExprVisitorPrint ev(context, os);
                                   node->visit(ev);
                               }) << StringView(" -> ") << context.ivars.fmtType(node->resType));
        }
        for (const auto& adv : context.advRevisits) {
            WARNING(adv->span(), W0000, StringView("Spare Rule - ") << FMT_CB(os, adv->fmt(os)));
        }
        BUG(rootPtr->span(), StringView("Spare rules left after typecheck stabilised"));
    }

    DEBUG(StringView("root_ptr = ") << rootPtr->typeName() << StringView(" ") << rootPtr->resType);
    expr.reset(rootPtr.release());
    expr.bindings.grow(context.bindings.size());
    for (auto& binding : context.bindings) {
        expr.bindings.pushBack(binding.ty);
    }

    {
        DEBUG(StringView("==== VALIDATE ==== (") << count << StringView(" rounds)"));
        ExprVisitorApply visitor{context};
        visitor.visitNodePtr(expr);
    }

    {
        StaticTraitResolve staticResolve(ms.wb);
        staticResolve.setBothGenericsRaw(ms.implGenerics, ms.itemGenerics);

        DEBUG(StringView("=== Method const params ==="));

        struct VisitMethodConst: public HIRExprVisitorDef {
            const TypeckModuleState& ms;
            const StaticTraitResolve& staticResolve;

            VisitMethodConst(const TypeckModuleState& ms, const StaticTraitResolve& staticResolve)
                : HIRExprVisitorDef(ms.crate.types)
                , ms(ms)
                , staticResolve(staticResolve)
            {
            }

            void evaluateConstantParams(const Span& sp, HIRPath& path) {
                HIRPathParams* params = nullptr;
                switch (path.data.tag()) {
                    case HIRPathData::TAG_Generic: {
                        auto& e = path.data.as_Generic();
                        params = &e.params;
                        break;
                    }
                    case HIRPathData::TAG_UfcsKnown: {
                        auto& e = path.data.as_UfcsKnown();
                        params = &e.params;
                        break;
                    }
                    case HIRPathData::TAG_UfcsInherent: {
                        auto& e = path.data.as_UfcsInherent();
                        params = &e.params;
                        break;
                    }
                    case HIRPathData::TAG_UfcsUnknown: {
                        BUG(sp, StringView("Unresolved constant path ") << path);
                        break;
                    }
                }

                const bool hasUnevaluated = std::any_of(params->values.begin(), params->values.end(), [](const HIRConstGeneric& value) {
                    return value.is_Unevaluated();
                });
                if (!hasUnevaluated) {
                    return;
                }

                MonomorphState outParams(ms.crate.types);
                auto value = staticResolve.getValue(sp, path, outParams, /*signatureOnly=*/true, nullptr);
                const auto* constant = value.opt_Constant();
                ASSERT_BUG(sp, constant, StringView("Constant path resolved to ") << value.tagStr() << StringView(": ") << path);
                ConvertHIRConstantEvaluateMethodParams(sp, ms.wb, ms.crate, &(*constant)->params, *params);
            }

            void visit(HIRExprNodePathValue& node) override {
                HIRExprVisitorDef::visit(node);
                if (node.target == HIRExprNodePathValue::CONSTANT) {
                    evaluateConstantParams(node.span(), node.path);
                }
            }

            void visitPatternValue(const Span& sp, HIRPattern::Value& value) {
                if (auto* named = value.opt_Named()) {
                    evaluateConstantParams(sp, named->path);
                }
            }

            void visitPattern(const Span& sp, HIRPattern& pattern) override {
                HIRExprVisitorDef::visitPattern(sp, pattern);
                switch (pattern.data.tag()) {
                    default:
                        break;
                    case HIRPatternData::TAG_Value: {
                        auto& e = pattern.data.as_Value();
                        visitPatternValue(sp, e.val);
                        break;
                    }
                    case HIRPatternData::TAG_Range: {
                        auto& e = pattern.data.as_Range();
                        if (e.start) {
                            visitPatternValue(sp, *e.start);
                        }
                        if (e.end) {
                            visitPatternValue(sp, *e.end);
                        }
                        break;
                    }
                }
            }

            void visit(HIRExprNodeCallPath& node) override {
                HIRExprVisitorDef::visit(node);

                HIRPathParams* paramsPtr = nullptr;
                switch (node.path.data.tag()) {
                    case HIRPathData::TAG_Generic: {
                        auto& pe = node.path.data.as_Generic();
                        paramsPtr = &pe.params;
                        break;
                    }
                    case HIRPathData::TAG_UfcsKnown: {
                        auto& pe = node.path.data.as_UfcsKnown();
                        paramsPtr = &pe.params;
                        break;
                    }
                    case HIRPathData::TAG_UfcsInherent: {
                        auto& pe = node.path.data.as_UfcsInherent();
                        paramsPtr = &pe.params;
                        break;
                    }
                    case HIRPathData::TAG_UfcsUnknown: {
                        BUG(node.span(), StringView("Unresolved call path ") << node.path);
                        break;
                    }
                }

                const bool hasUnevaluated = std::any_of(paramsPtr->values.begin(), paramsPtr->values.end(), [](const HIRConstGeneric& value) {
                    return value.is_Unevaluated();
                });
                if (!hasUnevaluated) {
                    return;
                }

                TRACE_FUNCTION_FR(StringView("Call const params: ") << node.path, StringView("Call const params"));
                MonomorphState outParams(ms.crate.types);
                auto valRef = staticResolve.getValue(node.span(), node.path, outParams, /*signatureOnly=*/true, nullptr);
                const auto* fcn = valRef.opt_Function();
                ASSERT_BUG(node.span(), fcn, StringView("Call path resolved to ") << valRef.tagStr() << StringView(": ") << node.path);
                ConvertHIRConstantEvaluateMethodParams(node.span(), ms.wb, ms.crate, &(*fcn)->params, *paramsPtr);
            }

            void visit(HIRExprNodeCallMethod& node) override {
                HIRExprVisitorDef::visit(node);

                HIRPathParams* paramsPtr = nullptr;
                switch (node.methodPath.data.tag()) {
                    case HIRPathData::TAG_Generic: {
                        BUG(node.span(), StringView(""));
                        break;
                    }
                    case HIRPathData::TAG_UfcsUnknown: {
                        BUG(node.span(), StringView(""));
                        break;
                    }
                    case HIRPathData::TAG_UfcsKnown: {
                        auto& pe = node.methodPath.data.as_UfcsKnown();
                        paramsPtr = &pe.params;
                        break;
                    }
                    case HIRPathData::TAG_UfcsInherent: {
                        auto& pe = node.methodPath.data.as_UfcsInherent();
                        paramsPtr = &pe.params;
                        break;
                    }
                }
                BUG_ASSERT(paramsPtr);

                bool found = false;
                for (auto& v : paramsPtr->values) {
                    if (v.is_Unevaluated()) {
                        found = true;
                    }
                }
                if (found) {
                    TRACE_FUNCTION_FR(StringView("Method const params: ") << node.methodPath, StringView("Method const params"));
                    MonomorphState outParams(ms.crate.types);
                    auto valRef = staticResolve.getValue(node.span(), node.methodPath, outParams, /*signature_only=*/true, nullptr);
                    const HIRFunction& fcn = *valRef.as_Function();
                    const HIRGenericParams& gpDef = fcn.params;
                    ConvertHIRConstantEvaluateMethodParams(node.span(), ms.wb, ms.crate, &gpDef, *paramsPtr);
                }
            }
        } v(ms, staticResolve);

        expr->visit(v);
    }
}

/// TODO: If the function has multiple mismatched options, tell the caller to try again later?
bool visitCallPopulateCache(Context& context, const Span& sp, HIRPath& path, HIRExprCallCache& cache, const HIRTypeImpl* selectedInherentImpl);

bool visitCallPopulateCache(Context& context, const Span& sp, HIRPath& path, HIRExprCallCache& cache) {
    return visitCallPopulateCache(context, sp, path, cache, nullptr);
}

bool visitCallPopulateCache(Context& context, const Span& sp, HIRPath& path, HIRExprCallCache& cache, const HIRTypeImpl* selectedInherentImpl) {
    TRACE_FUNCTION_FR(path, path);
    BUG_ASSERT(cache.argTypes.length() == 0);

    const HIRFunction* fcnPtr = nullptr;

    struct Monomorph: public Monomorphiser {
        Context& context;
        const HIRType* selfTy;
        HIRPathParams implParams;
        bool hasImplParams;
        const HIRPathParams& fcnParams;
        const HIRPathParams hrlParams;

        Monomorph(Context& context, const HIRType* selfTy, const HIRPathParams* implParams, const HIRPathParams& fcnParams, HIRPathParams hrlParams)
            : Monomorphiser(context.crate.types)
            , context(context)
            , selfTy(selfTy)
            , implParams(implParams ? implParams->clone() : HIRPathParams())
            , hasImplParams(implParams != nullptr)
            , fcnParams(fcnParams)
            , hrlParams(std::move(hrlParams))
        {
        }

        const HIRType* getType(const Span& sp, const HIRGenericRef& e) const override {
            if (e.name == "Self" || e.isSelf()) {
                if (selfTy) {
                    return selfTy;
                } else {
                    TODO(sp, StringView("Handle 'Self' when monomorphising"));
                }
            } else if (e.binding < 256) {
                if (hasImplParams) {
                    auto idx = e.idx();
                    ASSERT_BUG(sp, idx < implParams.types.size(), StringView("Generic param (impl) out of input range - ") << e << StringView(" >= ") << implParams.types.size());
                    return context.getType(implParams.types[idx]);
                } else {
                    BUG(sp, StringView("Impl-level parameter on free function (") << e << StringView(")"));
                }
            } else if (e.binding < 512) {
                auto idx = e.idx();
                ASSERT_BUG(sp, idx < fcnParams.types.size(), StringView("Generic param out of input range - ") << e << StringView(" >= ") << fcnParams.types.size());
                return context.getType(fcnParams.types[idx]);
            } else if (e.group() == GENERICHrtb) {
                return context.crate.types.generic(e.name, e.binding);
            } else {
                BUG(sp, StringView("Generic binding out of total range (") << e << StringView(")"));
            }
        }

        HIRConstGeneric getValue(const Span& sp, const HIRGenericRef& e) const override {
            if (e.binding < 256) {
                ASSERT_BUG(sp, hasImplParams, StringView("Impl-level value parameter on free function (") << e << StringView(")"));
                auto idx = e.idx();
                ASSERT_BUG(sp, idx < implParams.values.size(), StringView("Generic value (impl) out of input range - ") << e << StringView(" >= ") << implParams.values.size());
                return context.ivars.getValue(implParams.values[idx]).clone();
            } else if (e.binding < 512) {
                auto idx = e.idx();
                ASSERT_BUG(sp, idx < fcnParams.values.size(), StringView("Generic value out of input range - ") << e << StringView(" >= ") << fcnParams.values.size());
                return context.ivars.getValue(fcnParams.values[idx]).clone();
            } else if (e.group() == GENERICHrtb) {
                return e;
            } else {
                BUG(sp, StringView("Generic value bounding out of total range (") << e << StringView(")"));
            }
        }
    };

    cache.topParams = nullptr;
    switch (path.data.tag()) {
        case HIRPathData::TAG_Generic: {
            auto& e = path.data.as_Generic();
            const auto& fcn = context.crate.getFunctionByPath(sp, e.path);
            fixParamCount(sp, context, nullptr, false, path, fcn.params, e.params);
            fcnPtr = &fcn;
            cache.fcnParams = &fcn.params;

            const auto& pathParams = e.params;
            cache.monomorph.reset(new Monomorph(context, nullptr, nullptr, pathParams, {}));
            break;
        }
        case HIRPathData::TAG_UfcsKnown: {
            auto& e = path.data.as_UfcsKnown();
            const auto& trait = context.crate.getTraitByPath(sp, e.trait.path);
            fixParamCount(sp, context, e.type, true, path, trait.params, e.trait.params);
            if (trait.values.count(e.item) == 0) {
                BUG(sp, StringView("Method '") << e.item << StringView("' of trait ") << e.trait.path << StringView(" doesn't exist"));
            }
            const auto& fcn = trait.values.at(e.item).as_Function();
            fixParamCount(sp, context, e.type, false, path, fcn.params, e.params);
            cache.fcnParams = &fcn.params;
            cache.topParams = &trait.params;

            context.addTraitBound(sp, e.type, e.trait.path, e.trait.params.clone());

            fcnPtr = &fcn;
            const HIRPathParams* implParams = &e.trait.params;
            HIRPathParams selectedImplParams;

            if (!monomorphiseTypeNeeded(e.type) && !monomorphisePathparamsNeeded(e.trait.params) && !context.resolve.typeContainsIvars(e.type) && !context.resolve.paramsContainIvars(e.trait.params)) {
                context.resolve.selectTraitGoal(sp, e.trait.path, e.trait.params, e.type, [&](SolverSelection selection) {
                    if (!selection.impl.traitImpl) {
                        return false;
                    }
                    auto method = selection.impl.traitImpl->methods.find(e.item);
                    if (method != selection.impl.traitImpl->methods.end() && method->second.data.traitReturnType) {
                        fcnPtr = &method->second.data;
                        cache.fcnParams = &fcnPtr->params;
                        cache.topParams = &selection.impl.traitImpl->params;
                        selectedImplParams = selection.impl.implParams.clone();
                        implParams = &selectedImplParams;
                    }
                    return true;
                }, {.valueName = e.item.c_str()});
            }

            cache.monomorph.reset(new Monomorph(context, e.type, implParams, e.params, {}));
            break;
        }
        case HIRPathData::TAG_UfcsUnknown: {
            // TODO: Eventually, the HIR `Resolve UFCS` pass will be removed, leaving this code responsible for locating the item.
            TODO(sp, StringView("Hit a UfcsUnknown (") << path << StringView(") - Is this an error?"));
            break;
        }
        case HIRPathData::TAG_UfcsInherent: {
            if (!visitCallPopulateCacheUfcsInherent(context, sp, path, cache, fcnPtr, selectedInherentImpl)) {
                return false;
            }
            break;
        }
    }

    BUG_ASSERT(fcnPtr);
    cache.fcn = fcnPtr;
    const auto& fcn = *fcnPtr;
    cache.monomorph->setConstevalState(context.resolve.board(), HIRItemPath(path));
    const auto& monomorph = *cache.monomorph;

    for (size_t i = 0; i < fcn.fixedArgCount(); i++) {
        const auto& arg = fcn.args[i];
        TRACE_FUNCTION_FR(StringView("ARG ") << path << StringView(" - ") << arg.first << StringView(": ") << arg.second, StringView("Arg ") << arg.first << StringView(" : ") << cache.argTypes.back());
        cache.argTypes.pushBack(monomorph.monomorphType(sp, arg.second, false));
    }
    {
        TRACE_FUNCTION_FR(StringView("RET ") << path << StringView(" - ") << fcn.returnType, StringView("Ret ") << cache.argTypes.back());
        auto returnType = monomorph.monomorphType(sp, fcn.returnType, false);
        context.noteRpitSelfReferences(returnType);
        if (const auto* traitCall = path.data.opt_UfcsKnown()) {
            if (const auto* erased = returnType->opt_ErasedType()) {
                if (const auto* origin = erased->inner.opt_Fcn()) {
                    auto name = RcString::newInterned(FMT(ATY_PREFIX_ERASED << traitCall->item << StringView("_") << origin->index));
                    const auto& trait = context.crate.getTraitByPath(sp, traitCall->trait.path);
                    if (trait.types.find(name) != trait.types.end()) {
                        returnType = context.crate.types.path(HIRPath(traitCall->type, traitCall->trait.clone(), name, traitCall->params.clone()), {});
                    }
                }
            }
        }
        cache.argTypes.pushBack(std::move(returnType));
    }

    if (cache.topParams) {
        applyBoundsAsRules(context, sp, *cache.topParams, monomorph, /*is_impl_level=*/true);
    }
    applyBoundsAsRules(context, sp, *cache.fcnParams, monomorph, /*is_impl_level=*/false);

    return true;
}

void TypecheckCodeCSEnumerateRules(Context& context, const TypeckModuleState& ms, tArgs& args, const HIRType* resultType, HIRExprPtr& expr, HIRExprNodeP& rootPtr) {
    TRACE_FUNCTION;
    const Span& sp = rootPtr->span();

    ExprVisitorTagStaleIvars(context.crate.types).visitNodePtr(rootPtr);

    DEBUG(StringView("args = ") << args);
    DEBUG(StringView("result_type = ") << resultType);
    for (auto& arg : args) {
        context.handlePattern(Span(), arg.first, arg.second);
    }

    struct M: public Monomorphiser {
        Context& context;
        HIRExprPtr& expr;
        mutable const HIRType* curSelf;
        mutable const HIRPathParams* hrls;

        M(Context& context, HIRExprPtr& expr)
            : Monomorphiser(context.crate.types)
            , context(context)
            , expr(expr)
            , curSelf(nullptr)
            , hrls(nullptr)
        {
        }

        const HIRType* getType(const Span& sp, const HIRGenericRef& g) const override {
            if (g.binding == GENERICErasedSelf && curSelf) {
                return curSelf;
            }
            return context.crate.types.generic(g.name, g.binding);
        }

        HIRConstGeneric getValue(const Span& sp, const HIRGenericRef& g) const override {
            return g;
        }

        const HIRType* monomorphType(const Span& sp, const HIRType* tpl, bool allowInfer = true) const override {
            if (const auto* e = tpl->opt_ErasedType()) {
                if (const auto* ee = e->inner.opt_Fcn()) {
                    while (expr.erasedTypes.length() <= ee->index) {
                        expr.erasedTypes.pushBack(nullptr);
                    }
                    ASSERT_BUG(sp, expr.erasedTypes[ee->index] == nullptr, StringView("Multiple-visits to erased type #") << ee->index);
                    expr.erasedTypes.mut(ee->index) = context.ivars.newIvarTr();
                    auto rv = expr.erasedTypes[ee->index];
                    context.addRpitType(ee->origin, ee->index, rv);

                    auto prevCurSelf = this->curSelf;
                    this->curSelf = rv;

                    DEBUG(tpl << StringView(" -> ") << rv);
                    auto prevHrls = this->hrls;
                    for (const auto& trait : e->traits) {
                        auto ppHrl = HIRPathParams();
                        this->hrls = &ppHrl;
                        if (trait.typeBounds.size() == 0) {
                            context.equateTypesAssoc(sp, context.crate.types.infer(), trait.path.path, this->monomorphPathParams(sp, trait.path.params, allowInfer), rv, "", {}, false);
                        } else {
                            for (const auto& aty : trait.typeBounds) {
                                auto atyCloned = this->monomorphType(sp, aty.second.type);
                                auto params = this->monomorphPathParams(sp, trait.path.params, allowInfer);
                                context.equateTypesAssoc(sp, std::move(atyCloned), trait.path.path, std::move(params), rv, aty.first.c_str(), aty.second.atyParams, false);
                            }
                        }
                        this->hrls = nullptr;
                    }

                    this->hrls = prevHrls;
                    this->curSelf = prevCurSelf;

                    return rv;
                }
            }

            return Monomorphiser::monomorphType(sp, tpl, allowInfer);
        }
    };

    const HIRType* newResTy = resultType ? M(context, expr).monomorphType(sp, resultType) : context.ivars.newIvarTr();
    for (size_t i = 0; i < expr.erasedTypes.length(); i++) {
        ASSERT_BUG(sp, expr.erasedTypes[i] != nullptr, StringView("Non-visited erased type #") << i);
    }

    if (true) {
        DEBUG(StringView("--- Pre-adding ivars"));
        ExprVisitorAddIvars visitor(context);
        rootPtr->resType = context.addIvars(rootPtr->resType);
        rootPtr->visit(visitor);
    }

    DEBUG(StringView("--- Enumerating"));
    ExprVisitorEnum visitor(context, ms.traits, newResTy);
    rootPtr->resType = context.addIvars(rootPtr->resType);
    rootPtr->visit(visitor);

    DEBUG(StringView("Return type = ") << newResTy << StringView(", root_ptr = ") << rootPtr->typeName() << StringView(" ") << rootPtr->resType);
    context.equateTypesCoerce(sp, newResTy, rootPtr);
}

Context::TaitEntry::TaitEntry(const HIRPathParams& p, const HIRType* t)
    : params(p.clone())
    , ourType(std::move(t))
{
}

Context::Context(const WireBoard& wb, const HIRGenericParams* implParams, const HIRGenericParams* itemParams, const HIRSimplePath& modPath, const HIRGenericPath* currentTrait, const HIRTraitImpl* currentTraitImpl)
    : crate(*wb.crate)
    , currentTraitImpl(currentTraitImpl)
    , ivars(wb.crate->types)
    , resolve(ivars, wb, implParams, itemParams, modPath, currentTrait)
    , nextRuleIdx(0)
    , linkAssocIndexPool(ObjPool::fromMemory())
    , linkAssocIndex(linkAssocIndexPool.mutPtr())
    , langBox(crate.getLangItemPathOpt("owned_box"))
{
    if (currentTraitImpl) {
        for (const auto& entry : currentTraitImpl->types) {
            visitTyWith(entry.second.data, [&](const HIRType* type) {
                const auto* erased = type->opt_ErasedType();
                const auto* alias = erased ? erased->inner.opt_Alias() : nullptr;
                if (alias && alias->inner->path.components().back().c_str()[0] == '#') {
                    resolve.addDefiningOpaqueAlias(alias->inner->path);
                }
                return false;
            });
        }
    }
}

const HIRType* Context::revealOpaqueType(const HIRType* type) const {
    type = ivars.getType(type);
    const size_t maxDepth = 1 + erasedTypeAliases.size() + rpitTypes.size() + crate.opaqueTypeDefiners.size();
    for (size_t depth = 0; depth < maxDepth; depth++) {
        const HIRType* hiddenType = nullptr;
        auto revealRpit = [&](const HIRPath& origin, unsigned int index) {
            bool matched = false;
            for (const auto& entry : rpitTypes) {
                if (entry.index != index) {
                    continue;
                }
                RpitOriginMonomorph monomorph(crate.types);
                if (monomorph.cmpPath(Span(), *entry.origin, origin, ivars.callbackResolveInfer()) == HIRCompare::Equal) {
                    ASSERT_BUG(Span(), !matched, StringView("Multiple RPIT origins match ") << origin << StringView(" at index ") << index);
                    matched = true;
                    hiddenType = monomorph.monomorphType(Span(), ivars.getType(entry.ourType));
                }
            }
        };

        if (const auto* erased = type->opt_ErasedType()) {
            if (const auto* alias = erased->inner.opt_Alias()) {
                if (!resolve.isOpaqueAliasDefiningScope(*alias->inner)) {
                    return type;
                }
                const auto it = erasedTypeAliases.find(alias->inner.get());
                if (it != erasedTypeAliases.end()) {
                    hiddenType = it->second.ourType;
                } else if (alias->inner->type) {
                    hiddenType = MonomorphStatePtr(crate.types, nullptr, &alias->params, nullptr).monomorphType(Span(), alias->inner->type);
                }
            } else if (const auto* fcn = erased->inner.opt_Fcn()) {
                revealRpit(fcn->origin, fcn->index);
            }
        } else if (const auto* path = type->opt_Path()) {
            if (const auto* projection = path->path.data.opt_UfcsKnown()) {
                for (const auto& entry : rpitTypes) {
                    const auto* origin = entry.origin->data.opt_UfcsKnown();
                    if (!origin) {
                        continue;
                    }
                    const auto expectedName = RcString::newInterned(FMT(ATY_PREFIX_ERASED << origin->item << StringView("_") << entry.index));
                    if (projection->item != expectedName) {
                        continue;
                    }
                    HIRPath projectedOrigin(projection->type, projection->trait.clone(), origin->item, projection->params.clone());
                    revealRpit(projectedOrigin, entry.index);
                    if (hiddenType) {
                        break;
                    }
                }
            }
        } else {
            return type;
        }
        if (!hiddenType) {
            return type;
        }
        const auto* revealed = ivars.getType(hiddenType);
        if (revealed == type) {
            return type;
        }
        type = revealed;
    }
    return type;
}

const HIRType* Context::revealOpaqueTypes(const HIRType* type) const {
    struct Visitor: HIRVisitor {
        const Context& context;

        explicit Visitor(const Context& context)
            : HIRVisitor(nullptr, context.crate.types)
            , context(context)
        {
        }

        [[nodiscard]] const HIRType* visitType(const HIRType* type) override {
            return HIRVisitor::visitType(context.revealOpaqueType(type));
        }
    } visitor{*this};

    return visitor.visitType(type);
}

void Context::addRpitType(const HIRPath& origin, unsigned int index, const HIRType* type) {
    for (const auto& entry : rpitTypes) {
        if (entry.index == index && *entry.origin == origin) {
            ASSERT_BUG(Span(), entry.ourType == type, StringView("RPIT hidden type registered twice for ") << origin << StringView("#") << index);
            return;
        }
    }
    rpitTypes.push_back(RpitEntry{&origin, index, type, false});
}

void Context::noteRpitSelfReferences(const HIRType* type) {
    visitTyWith(type, [this](const HIRType* inner) {
        const auto* erased = inner->opt_ErasedType();
        const auto* origin = erased ? erased->inner.opt_Fcn() : nullptr;
        if (!origin) {
            return false;
        }
        for (auto& entry : rpitTypes) {
            if (entry.index == origin->index && *entry.origin == origin->origin) {
                entry.selfReferenced = true;
                break;
            }
        }
        return false;
    });
}

bool Context::fallbackUnresolvedRpitType(const Span& sp) {
    for (const auto& entry : rpitTypes) {
        if (!entry.selfReferenced) {
            continue;
        }
        const auto* hiddenType = ivars.getType(entry.ourType);
        const auto* infer = hiddenType->opt_Infer();
        if (!infer || infer->tyClass != HIRInferClass::None) {
            continue;
        }
        DEBUG(StringView("RPIT fallback ") << *entry.origin << StringView("#") << entry.index << StringView(": ") << hiddenType << StringView(" -> ()"));
        equateTypes(sp, hiddenType, crate.types.unit());
        return true;
    }
    return false;
}

MonomorphEraseHrls::MonomorphEraseHrls(HIRTypeInterner& types)
    : Monomorphiser(types)
{
}

auto MonomorphEraseHrls::getType(const Span& sp, const HIRGenericRef& g) const -> const HIRType* {
    return types.generic(g.name, g.binding);
}

auto MonomorphEraseHrls::getValue(const Span& sp, const HIRGenericRef& g) const -> HIRConstGeneric {
    return g;
}

auto ExprVisitorRevisit::nodeDiverges(const HIRExprNode& node) const -> bool {
    return node.diverges || this->context.getType(node.resType)->is_Diverge();
}

ExprVisitorRevisit::ExprVisitorRevisit(Context& context, bool fallback, const Vector<const HIRType*>* passStartIvars, const IvarCoercionIndex* coercionIndex)
    : context(context)
    , completed(false)
    , isFallback(fallback)
    , passStartIvars(passStartIvars)
    , coercionIndex(coercionIndex)
{
}

auto ExprVisitorRevisit::nodeCompleted() const -> bool {
    return completed;
}

auto ExprVisitorRevisit::visit(HIRExprNodeBlock& node) -> void {
    BUG_ASSERT(!node.nodes.empty());
    const auto& lastNode = *node.nodes.back();
    const auto& lastTy = this->context.getType(lastNode.resType);

    DEBUG(StringView("_Block: last_ty = ") << lastTy);
    bool diverges = false;
    if (lastNode.diverges) {
        diverges = true;
    } else if (const auto* e = lastTy->opt_Infer()) {
        switch (e->tyClass) {
            case HIRInferClass::Integer:
            case HIRInferClass::Float:
                diverges = false;
                break;
            default:
                return;
        }
    } else if (lastTy->is_Diverge()) {
        diverges = true;
    } else {
        diverges = false;
    }
    if (diverges) {
        DEBUG(StringView("_Block: diverges, yield !"));
        this->context.equateTypes(node.span(), node.resType, context.crate.types.diverge());
    } else {
        DEBUG(StringView("_Block: doesn't diverge but doesn't yield a value, yield ()"));
        this->context.equateTypes(node.span(), node.resType, context.crate.types.unit());
    }
    node.diverges = diverges;
    this->completed = true;
}

auto ExprVisitorRevisit::visit(HIRExprNodeConstBlock& node) -> void {
    noRevisit(node);
}

auto ExprVisitorRevisit::visit(HIRExprNodeAsm& node) -> void {
    // TODO: Revisit for validation
    noRevisit(node);
}

auto ExprVisitorRevisit::visit(HIRExprNodeAsm2& node) -> void {
    // TODO: Revisit for validation
    noRevisit(node);
}

auto ExprVisitorRevisit::visit(HIRExprNodeReturn& node) -> void {
    noRevisit(node);
}

auto ExprVisitorRevisit::visit(HIRExprNodeYield& node) -> void {
    noRevisit(node);
}

auto ExprVisitorRevisit::visit(HIRExprNodeAWait& node) -> void {
    noRevisit(node);
}

auto ExprVisitorRevisit::visit(HIRExprNodeUse& node) -> void {
    noRevisit(node);
}

auto ExprVisitorRevisit::visit(HIRExprNodeLet& node) -> void {
    if (!node.value) {
        this->context.equateTypes(node.span(), node.resType, this->context.crate.types.unit());
        this->completed = true;
        return;
    }
    const auto* valueType = this->context.getType(node.value->resType);
    const auto* infer = valueType->opt_Infer();
    if (!node.value->diverges && infer && infer->tyClass == HIRInferClass::None) {
        return;
    }
    node.diverges = this->nodeDiverges(*node.value);
    this->context.equateTypes(node.span(), node.resType, node.diverges ? this->context.crate.types.diverge() : this->context.crate.types.unit());
    this->completed = true;
}

auto ExprVisitorRevisit::visit(HIRExprNodeLoop& node) -> void {
    noRevisit(node);
}

auto ExprVisitorRevisit::visit(HIRExprNodeLoopControl& node) -> void {
    noRevisit(node);
}

auto ExprVisitorRevisit::visit(HIRExprNodeMatch& node) -> void {
    noRevisit(node);
}

auto ExprVisitorRevisit::visit(HIRExprNodeAssign& node) -> void {
    noRevisit(node);
}

auto ExprVisitorRevisit::visit(HIRExprNodeBinOp& node) -> void {
    noRevisit(node);
}

auto ExprVisitorRevisit::visit(HIRExprNodeUniOp& node) -> void {
    noRevisit(node);
}

auto ExprVisitorRevisit::visit(HIRExprNodeBorrow& node) -> void {
    noRevisit(node);
}

auto ExprVisitorRevisit::visit(HIRExprNodeRawBorrow& node) -> void {
    noRevisit(node);
}

auto ExprVisitorRevisit::bad_cast(const Span& sp, const HIRType* srcTy, const HIRType* tgtTy, const char* where) -> void {
    ERROR(sp, E0000, StringView("Invalid cast [") << where << StringView("]:\n") << StringView("from ") << this->context.ivars.fmtType(srcTy) << StringView("\n") << StringView(" to  ") << this->context.ivars.fmtType(tgtTy));
}

auto ExprVisitorRevisit::equateFunctionSignature(const Span& sp, const HIRTypeDataFunctionPointer& dst, const HIRTypeDataFunctionPointer& src) -> void {
    this->context.equateTypes(sp, dst.rettype, src.rettype);
    for (size_t i = 0; i < dst.argTypes.length(); i++) {
        this->context.equateTypes(sp, dst.argTypes[i], src.argTypes[i]);
    }
}

auto ExprVisitorRevisit::visit(HIRExprNodeCast& node) -> void {
    const auto& sp = node.span();
    const auto& tgtTy = this->context.getType(node.resType);
    const auto& srcTy = this->context.getType(node.value->resType);

    TRACE_FUNCTION_F(srcTy << StringView(" as ") << tgtTy);
    if (this->context.ivars.typesEqual(srcTy, tgtTy)) {
        this->completed = true;
        return;
    }

    switch ((*tgtTy).tag()) {
        case HIRType::TAG_Infer: {
            DEBUG(StringView("- Target type is still _"));
            break;
        }
        case HIRType::TAG_Diverge: {
            BUG(sp, StringView(""));
            break;
        }
        case HIRType::TAG_Primitive: {
            auto& e = (*tgtTy).as_Primitive();
            if (e == HIRCoreType::Char) {
                if (this->isFallback) {
                    this->context.equateTypes(sp, srcTy, context.crate.types.primitive(HIRCoreType::U8));
                    this->completed = true;
                } else if (!this->context.getType(srcTy)->is_Infer()) {
                    this->completed = true;
                } else {
                }
            } else {
                this->completed = true;
            }
            break;
        }
        case HIRType::TAG_Path: {
            this->context.equateTypesCoerce(sp, tgtTy, node.value);
            this->completed = true;
            return;
        }
        case HIRType::TAG_Generic: {
            TODO(sp, StringView("_Cast Generic"));
            break;
        }
        case HIRType::TAG_TraitObject: {
            bad_cast(sp, srcTy, tgtTy, "dst");
            break;
        }
        case HIRType::TAG_ErasedType: {
            bad_cast(sp, srcTy, tgtTy, "dst");
            break;
        }
        case HIRType::TAG_Array: {
            this->context.equateTypesCoerce(sp, tgtTy, node.value);
            this->completed = true;
            return;
        }
        case HIRType::TAG_Slice: {
            bad_cast(sp, srcTy, tgtTy, "dst");
            break;
        }
        case HIRType::TAG_Pattern: {
            bad_cast(sp, srcTy, tgtTy, "dst");
            break;
        }
        case HIRType::TAG_Tuple: {
            bad_cast(sp, srcTy, tgtTy, "dst");
            break;
        }
        case HIRType::TAG_Borrow: {
            if (!this->isFallback && (srcTy->is_Infer() || (srcTy->is_Borrow() && this->context.getType(srcTy->as_Borrow().inner)->is_Infer()))) {
                return;
            }
            this->context.equateTypesCoerce(sp, tgtTy, node.value);
            this->completed = true;
            return;
        }
        case HIRType::TAG_Pointer: {
            auto& e = (*tgtTy).as_Pointer();
            const auto& ity = this->context.getType(e.inner);
            switch ((*srcTy).tag()) {
                default:
                    ERROR(sp, E0000, StringView("Invalid cast to pointer from ") << srcTy);
                case HIRType::TAG_Function:
                case HIRType::TAG_NamedFunction:
                    // TODO: What is the valid set? *const () and *const u8 at least are allowed
                    if (ity == context.crate.types.unit() || ity == HIRCoreType::U8 || ity == HIRCoreType::I8) {
                        this->completed = true;
                    } else if (ity->is_Infer()) {
                    } else {
                        // TODO: Only allow thin pointers? `c_void` is used in 1.74 libstd
                        this->completed = true;
                    }
                    break;
                case HIRType::TAG_Primitive: {
                    auto& sE = (*srcTy).as_Primitive();
                    switch (sE) {
                        case HIRCoreType::Bool:
                        case HIRCoreType::Char:
                        case HIRCoreType::Str:
                        case HIRCoreType::F32:
                        case HIRCoreType::F64:
                            ERROR(sp, E0000, StringView("Invalid cast to pointer from ") << srcTy);
                        default:
                            break;
                    }
                    this->completed = true;
                    break;
                }
                case HIRType::TAG_Infer: {
                    auto& sE = (*srcTy).as_Infer();
                    switch (sE.tyClass) {
                        case HIRInferClass::Float:
                            ERROR(sp, E0000, StringView("Invalid cast to pointer from floating point literal"));
                        case HIRInferClass::Integer:
                            this->context.equateTypes(sp, srcTy, context.crate.types.primitive(HIRCoreType::Usize));
                            this->completed = true;
                            break;
                        case HIRInferClass::None:
                            break;
                    }
                    break;
                }
                case HIRType::TAG_Borrow: {
                    auto& sE = (*srcTy).as_Borrow();
                    if (!(sE.type >= e.type)) {
                        ERROR(sp, E0000, StringView("Invalid cast from ") << srcTy << StringView(" to ") << tgtTy);
                    }
                    const auto& srcInner = this->context.getType(sE.inner);

                    if (srcInner->is_Array()) {
                        const auto* elementInfer = this->context.getType(srcInner->as_Array().inner)->opt_Infer();
                        if (elementInfer && (!this->isFallback || elementInfer->tyClass != HIRInferClass::None)) {
                            /* Cast checks follow inference and numeric fallback. A remaining
                               unconstrained element can then be related by the coercion cast
                               or, if coercion fails, by the array-to-element pointer cast. */
                            return;
                        }
                    }

                    // TODO: Wouldn't this be better served by a coercion point?

                    if (srcInner->is_Infer() || ity->is_Infer()) {
                        if (this->isFallback) {
                            this->context.equateTypes(sp, srcInner, ity);
                            this->completed = true;
                        }
                    } else if (srcInner->is_Array() && this->context.getType(srcInner->as_Array().inner) == ity) {
                        auto ty = context.crate.types.pointer(e.type, srcInner);
                        node.value = NEWNODE(ty, sp, Cast, mv$(node.value), ty);
                        this->completed = true;
                    } else {
                        auto response = this->context.resolve.evaluateCoercionGoal(sp, e.inner, sE.inner, SolverCoercionOp::Unsizing);
                        if (response.effects.certainty == SolverCertainty::Proven) {
                            this->context.applySolverResponse(sp, response.effects);
                            if (response.relation == SolverCoercionRelation::Coercion) {
                                auto ty = context.crate.types.borrow(e.type, e.inner);
                                node.value = NEWNODE(ty, sp, Unsize, mv$(node.value), ty);
                                this->context.addTraitBound(sp, sE.inner, this->context.resolve.langUnsize(), HIRPathParams(e.inner));
                            } else {
                                this->context.equateTypes(sp, e.inner, sE.inner);
                            }
                        } else if (response.effects.certainty == SolverCertainty::Ambiguous) {
                            return;
                        } else if (srcInner->is_Array() && this->context.resolve.typeIsSized(sp, ity) == SolverCertainty::Proven) {
                            this->context.equateTypes(sp, srcInner->as_Array().inner, ity);
                            auto ty = context.crate.types.pointer(e.type, srcInner);
                            node.value = NEWNODE(ty, sp, Cast, mv$(node.value), ty);
                        } else {
                            this->context.equateTypes(sp, e.inner, sE.inner);
                        }
                        this->completed = true;
                    }
                    break;
                }
                case HIRType::TAG_Pointer: {
                    auto& sE = (*srcTy).as_Pointer();

                    // TODO: In some rare cases, this ivar could be completely

                    const auto& dstInner = this->context.getType(e.inner);
                    const auto& srcInner = this->context.getType(sE.inner);
                    if (dstInner->is_Infer()) {
                        const auto index = dstInner->as_Infer().index;
                        const bool hasLiveCoercion = std::any_of(this->context.linkCoerce.begin(), this->context.linkCoerce.end(), [&](const auto& obligation) {
                            return typeDependsOnIvar(this->context, obligation->leftTy, index)
                                || typeDependsOnIvar(this->context, obligation->sourceType(), index);
                        });
                        if (this->isFallback && !hasLiveCoercion) {
                            this->context.equateTypes(sp, dstInner, srcInner);
                            this->completed = true;
                        }
                        return;
                    } else if (srcInner->is_Infer()) {
                        /* A pointer cast relates the two pointee types not at all.  The
                           source pointee is resolved by the operand's own constraints -
                           an integer literal keeps its own fallback - and never by the
                           cast's destination.  Wait for those constraints, then accept
                           the cast whatever the pointee turned out to be. */
                        if (!this->isFallback) {
                            return;
                        }
                    } else {
                    }
                    this->completed = true;
                    break;
                }
            }
            break;
        }
        case HIRType::TAG_Function: {
            auto& e = (*tgtTy).as_Function();
            switch ((*srcTy).tag()) {
                default:
                    bad_cast(sp, srcTy, tgtTy, "fcn src");
                case HIRType::TAG_Infer: {
                    auto& sE = (*srcTy).as_Infer();
                    if (sE.tyClass != HIRInferClass::None) {
                        bad_cast(sp, srcTy, tgtTy, "fcn src");
                    }
                    if (this->isFallback) {
                        this->context.equateTypes(sp, srcTy, tgtTy);
                        this->completed = true;
                    }
                    break;
                }
                case HIRType::TAG_NodeType: {
                    auto& sE = (*srcTy).as_NodeType();
                    if (const auto* const* snPp = sE.opt_Closure()) {
                        if ((*snPp)->args.size() != e.argTypes.length()) {
                            bad_cast(sp, srcTy, tgtTy, "fcn nargs");
                        }
                        this->context.equateTypes(sp, e.rettype, (*snPp)->returnType);
                        for (size_t i = 0; i < e.argTypes.length(); i++) {
                            this->context.equateTypes(sp, e.argTypes[i], (*snPp)->args[i].second);
                        }
                        this->completed = true;
                    } else {
                        bad_cast(sp, srcTy, tgtTy, "fcn src");
                    }
                    break;
                }
                case HIRType::TAG_Function: {
                    auto& sE = (*srcTy).as_Function();
                    if (sE.abi != e.abi || (sE.isUnsafe && sE.isUnsafe != e.isUnsafe) || sE.argTypes.length() != e.argTypes.length()) {
                        bad_cast(sp, srcTy, tgtTy, "fcn nargs");
                    }
                    equateFunctionSignature(sp, e, sE);
                    this->completed = true;
                    break;
                }
                case HIRType::TAG_NamedFunction: {
                    auto& f = (*srcTy).as_NamedFunction();
                    auto ft = context.expandAssociatedTypes(sp, context.crate.types.function(f.decay(context.crate.types, sp)));
                    const auto& sE = ft->as_Function();
                    if (sE.abi != e.abi || (sE.isUnsafe && sE.isUnsafe != e.isUnsafe) || sE.argTypes.length() != e.argTypes.length()) {
                        bad_cast(sp, srcTy, tgtTy, "fcn nargs");
                    }
                    equateFunctionSignature(sp, e, sE);
                    this->completed = true;
                    break;
                }
            }
            break;
        }
        case HIRType::TAG_NamedFunction: {
            BUG(sp, StringView("Attempting to cast to a named-function type - impossible"));
            break;
        }
        case HIRType::TAG_NodeType: {
            BUG(sp, StringView("Attempting to cast to a magic type type - impossible"));
            break;
        }
    }
}

auto ExprVisitorRevisit::visit(HIRExprNodeUnsize& node) -> void {
    noRevisit(node);
}

auto ExprVisitorRevisit::visit(HIRExprNodeIndex& node) -> void {
    const auto& langIndex = this->context.crate.getLangItemPath(node.span(), "index"); // TODO: Pre-load
    const auto& valTy = this->context.getType(node.value->resType);
    const auto& idxTy = this->context.getType(node.cache.indexTy);

    TRACE_FUNCTION_F(static_cast<const void*>(&node) << StringView(" Index: val=") << valTy << StringView(", idx=") << idxTy << StringView(""));
    unsigned int derefCount = 0;
    const HIRType* tmpType;
    const auto* currentTy = node.value->resType;
    Vector<const HIRType*> derefResTypes;

    // TODO: (CHECK) rustc doesn't use the index value type when finding the indexable item, trustme does.
    HIRPathParams traitPp;
    traitPp.types.push_back(idxTy);
    do {
        const auto& ty = this->context.getType(currentTy);
        DEBUG(StringView("(Index): (: ") << ty << StringView(")[: ") << traitPp.types[0] << StringView("]"));
        if (ty->is_Infer()) {
            return;
        }

        bool hasResponse = false;
        bool selected = false;
        this->context.resolve.probeTraitGoalMayApply(
            node.span(),
            langIndex,
            traitPp,
            ty,
            [&](SolverMayApply probe) {
            if (!probe.candidate) {
                return false;
            }
            hasResponse = true;
            context.applySolverResponse(node.span(), probe.effects);
            selected = true;
            return true;
        },
            {
                .assocName = "Output",
                .assocType = node.resType,
                .allowInferInputs = true,
                .ambiguity = SolverAmbiguityPolicy::Report,
            }
        );
        if (selected) {
            break;
        }
        if (hasResponse) {
            currentTy = nullptr;
            break;
        }

        derefCount += 1;
        currentTy = this->context.resolve.autoderef(node.span(), ty);
        if (currentTy) {
            derefResTypes.pushBack(currentTy);
        }
    } while (currentTy);

    if (currentTy) {
        DEBUG(StringView("Found impl on type ") << currentTy << StringView(" with ") << derefCount << StringView(" derefs"));
        BUG_ASSERT(derefCount == derefResTypes.length());
        for (const auto& tyR : derefResTypes) {
            auto ty = mv$(tyR);

            node.value = this->context.createAutoderef(mv$(node.value), mv$(ty));
            context.ivars.getType(node.value->resType);
        }

        completed = true;
    }
}

auto ExprVisitorRevisit::visit(HIRExprNodeDeref& node) -> void {
    const auto& ty = this->context.getType(node.value->resType);

    TRACE_FUNCTION_F(StringView("Deref: ty=") << ty);
    const auto& opTrait = this->context.crate.getLangItemPathOpt("deref");
    auto useBuiltin = [&](const HIRType* inner) {
        node.traitUsed = HIRExprNodeDeref::TraitUsed::Builtin;
        this->context.equateTypes(node.span(), node.resType, inner);
    };
    auto useTrait = [&]() {
        node.traitUsed = HIRExprNodeDeref::TraitUsed::Trait;
        this->context.equateTypesAssoc(node.span(), node.resType, opTrait, {}, node.value->resType, "Target", {}, true, TypeckPrimitiveOperator::Deref);
    };

    switch ((*ty).tag()) {
        default: {
            if (const auto* inner = this->context.resolve.typeIsOwnedBox(node.span(), ty)) {
                useBuiltin(inner);
            } else {
                ASSERT_BUG(node.span(), !opTrait.components().empty(), StringView("Deref trait missing for non-builtin dereference of ") << ty);
                useTrait();
            }
        } break;
        case HIRType::TAG_Infer: {
            return;
        }
        case HIRType::TAG_Borrow: {
            auto& e = (*ty).as_Borrow();
            useBuiltin(e.inner);
            break;
        }
        case HIRType::TAG_Pointer: {
            auto& e = (*ty).as_Pointer();
            // TODO: Figure out if this node is in an unsafe block.
            useBuiltin(e.inner);
            break;
        }
    }
    this->completed = true;
}

auto ExprVisitorRevisit::visitEmplace129(HIRExprNodeEmplace& node) -> void {
    const auto& sp = node.span();
    const auto& expTy = this->context.getType(node.resType);
    const auto& dataTy = this->context.getType(node.value->resType);
    const auto& placerTy = this->context.getType(node.place->resType);
    const auto& langBoxed = this->context.langBox;
    TRACE_FUNCTION_F(StringView("exp_ty=") << expTy << StringView(", data_ty=") << dataTy << StringView(", placer_ty") << placerTy);
    ASSERT_BUG(sp, node.type == HIRExprNodeEmplace::Type::Boxer, StringView("1.29 mode with non-box _Emplace node"));
    ASSERT_BUG(sp, placerTy == context.crate.types.unit(), StringView("1.29 mode with box in syntax - placer type is ") << placerTy);

    ASSERT_BUG(sp, !langBoxed.components().empty(), StringView("`owned_box` not present when `box` operator used"));

    const auto& str = this->context.crate.getStructByPath(sp, langBoxed);
    // TODO: Store this type to avoid having to construct it every pass
    auto p = HIRGenericPath(langBoxed, {dataTy});
    p.params.types.push_back(MonomorphStatePtr(context.crate.types, nullptr, &p.params, nullptr).monomorphType(sp, str.params.types.at(1).defaultValue));
    p.params.types.back() = this->context.addIvars(p.params.types.back());
    auto boxedTy = context.crate.types.path(mv$(p), &str);

    // TODO: is there anyting special about this node that might need revisits?

    context.equateTypes(sp, expTy, boxedTy);
    this->completed = true;
}

auto ExprVisitorRevisit::visit(HIRExprNodeEmplace& node) -> void {
    return visitEmplace129(node);
}

auto ExprVisitorRevisit::visit(HIRExprNodeTupleVariant& node) -> void {
    noRevisit(node);
}

auto ExprVisitorRevisit::visit(HIRExprNodeCallPath& node) -> void {
    if (!visitCallPopulateCache(this->context, node.span(), node.path, node.cache)) {
        DEBUG(StringView("- CallPath still ambiguous - trying again later"));
        return;
    }
    BUG_ASSERT(node.cache.argTypes.length() >= 1);
    unsigned int expArgc = node.cache.argTypes.length() - 1;
    if (node.args.size() != expArgc) {
        if (node.cache.fcn->variadic && node.args.size() > expArgc) {
        } else {
            ERROR(node.span(), E0000, StringView("Incorrect number of arguments to ") << node.path << StringView(" - exp ") << expArgc << StringView(" got ") << node.args.size());
        }
    }
    for (unsigned int i = 0; i < node.cache.argTypes.length() - 1; i++) {
        this->context.equateTypesCoerce(node.span(), node.cache.argTypes[i], node.args[i]);
    }
    this->context.equateTypes(node.span(), node.resType, node.cache.argTypes.back());
    this->context.requireSized(node.span(), node.resType);
    this->completed = true;
}

auto ExprVisitorRevisit::callAsyncCallable(HIRExprNodeCallValue& node, const HIRType* ty, const HIRPathParams& traitPp) -> AsyncCallResult {
    struct Candidate {
        const HIRSimplePath& trait;
        const char* future;
        HIRExprNodeCallValue::TraitUsed used;
    };

    const Candidate candidates[] = {
        {this->context.resolve.langAsyncFn(), "CallRefFuture", HIRExprNodeCallValue::TraitUsed::AsyncFn},
        {this->context.resolve.langAsyncFnMut(), "CallRefFuture", HIRExprNodeCallValue::TraitUsed::AsyncFnMut},
        {this->context.resolve.langAsyncFnOnce(), "CallOnceFuture", HIRExprNodeCallValue::TraitUsed::AsyncFnOnce},
    };
    for (const auto& candidate : candidates) {
        if (candidate.trait.components().empty()) {
            continue;
        }
        const HIRType* fcnArgsTup;
        const auto inspectImpl = [&](const SolverImpl& impl) {
            auto tup = impl.getTraitTyParam(context.crate.types, 0);
            if (!tup->is_Tuple()) {
                ERROR(node.span(), E0000, StringView("AsyncFn* expects a tuple argument, got ") << tup);
            }
            fcnArgsTup = mv$(tup);
        };
        bool ambiguous = false;
        const bool found = this->context.resolve.probeTraitGoalMayApply(
            node.span(),
            candidate.trait,
            traitPp,
            ty,
            [&](SolverMayApply probe) {
            if (!probe.candidate) {
                ambiguous |= probe.effects.certainty == SolverCertainty::Ambiguous;
                return false;
            }
            context.applySolverResponse(node.span(), probe.effects);
            if (probe.effects.certainty != SolverCertainty::Proven) {
                ambiguous = true;
                return false;
            }
            inspectImpl(*probe.candidate);
            return true;
        },
            {
                .assocName = candidate.future,
                .assocType = node.resType,
                .allowInferInputs = true,
            }
        );
        if (ambiguous) {
            return AsyncCallResult::Ambiguous;
        }
        if (!found) {
            continue;
        }
        DEBUG(StringView("-- Using ") << candidate.trait << StringView(" for ") << ty);
        node.argTypes = fcnArgsTup->as_Tuple();
        node.argTypes.pushBack(node.resType);
        node.traitUsed = candidate.used;
        return AsyncCallResult::Proven;
    }
    return AsyncCallResult::NoSolution;
}

auto ExprVisitorRevisit::visit(HIRExprNodeCallValue& node) -> void {
    node.value->resType = this->context.expandAssociatedTypes(node.span(), node.value->resType);
    const auto& tyO = this->context.getType(node.value->resType);

    TRACE_FUNCTION_F(StringView("CallValue: ty=") << tyO);
    if (tyO->is_Infer()) {
        return;
    }
    if (const auto* path = tyO->opt_Path()) {
        if (path->binding.is_Unbound() && path->path.data.is_UfcsKnown() && this->context.resolve.typeContainsIvars(tyO)) {
            return;
        }
    }
    const auto& langFnOnce = this->context.resolve.langFnOnce();

    HIRPathParams traitPp;
    {
        Vector<const HIRType*> argTypes;
        for (const auto& argTy : node.argIvars) {
            argTypes.pushBack(this->context.getType(argTy));
        }
        traitPp.types.push_back(context.crate.types.tuple(mv$(argTypes)));
    }

    unsigned int derefCount = 0;
    const HIRType* tmpType;
    const auto* ty = tyO;

    bool keepLooping = false;
    do {
        keepLooping = false;

        DEBUG(StringView("- ty = ") << ty);
        if (ty->is_NodeType() && ty->as_NodeType().is_Closure()) {
            const auto* nodeP = ty->as_NodeType().as_Closure();
            for (const auto& arg : nodeP->args) {
                node.argTypes.pushBack(arg.second);
            }
            node.argTypes.pushBack(nodeP->returnType);
            node.traitUsed = HIRExprNodeCallValue::TraitUsed::Unknown;
        } else if (ty->is_Function() || ty->is_NamedFunction()) {
            const HIRType* tmpFt;
            const auto* e = ty->opt_Function();
            if (!e) {
                tmpFt = context.crate.types.function(ty->as_NamedFunction().decay(context.crate.types, node.span()));
                tmpFt = this->context.expandAssociatedTypes(node.span(), tmpFt);
                e = &tmpFt->as_Function();
            }
            for (const auto& arg : e->argTypes) {
                node.argTypes.pushBack(arg);
            }
            if (e->isVariadic) {
                for (size_t i = e->argTypes.length(); i < node.args.size(); i++) {
                    node.argTypes.pushBack(node.argIvars[i]);
                }
            }
            node.argTypes.pushBack(e->rettype);
            node.traitUsed = HIRExprNodeCallValue::TraitUsed::Fn;
        } else if (ty->is_Infer()) {
            return;
        } else if (const auto* e = ty->opt_Borrow()) {
            derefCount++;
            ty = this->context.getType(e->inner);
            DEBUG(StringView("Deref -> ") << ty);
            keepLooping = true;
            continue;
        }
        // TODO: If autoderef is possible, do it and continue. Only look for impls once autoderef fails
        else {
            const HIRType* fcnArgsTup;
            const HIRType* fcnRet;

            // TODO: Use `find_trait_impls` instead of two different calls

            // TODO: Sometimes there's impls that just forward for wrappers, which can lead to incorrect rules

            bool found = false;
            bool ambiguous = false;
            const auto inspectImpl = [&](const SolverImpl& impl) {
                auto tup = impl.getTraitTyParam(context.crate.types, 0);
                if (!tup->is_Tuple()) {
                    ERROR(node.span(), E0000, StringView("FnOnce expects a tuple argument, got ") << tup);
                }
                fcnArgsTup = mv$(tup);

                fcnRet = impl.getType(context.crate.types, "Output", {});
            };
            this->context.resolve.probeTraitGoalMayApply(
                node.span(),
                langFnOnce,
                traitPp,
                ty,
                [&](SolverMayApply probe) {
                if (!probe.candidate) {
                    return false;
                }
                context.applySolverResponse(node.span(), probe.effects);
                if (probe.effects.certainty != SolverCertainty::Proven) {
                    ambiguous = true;
                    return false;
                }
                inspectImpl(*probe.candidate);
                found = true;
                return true;
            },
                {
                    .assocName = "Output",
                    .assocType = node.resType,
                    .allowInferInputs = true,
                }
            );
            if (ambiguous) {
                return;
            }
            if (found) {
                if (fcnRet == nullptr) {
                    fcnRet = context.crate.types.path(HIRPath(HIRPath::Data::make_UfcsKnown({ty, HIRGenericPath(langFnOnce, HIRPathParams(fcnArgsTup)), "Output", {}})), {});
                }
            } else if (const auto* e = ty->opt_Borrow()) {
                derefCount++;
                ty = this->context.getType(e->inner);
                DEBUG(StringView("Deref -> ") << ty);
                keepLooping = true;
                continue;
            } else {
                if (const auto* nextTyP = this->context.resolve.autoderef(node.span(), ty)) {
                    DEBUG(StringView("Deref (autoderef) ") << ty << StringView(" -> ") << nextTyP);
                    derefCount++;
                    ty = nextTyP;
                    keepLooping = true;
                    continue;
                }

                const auto asyncResult = this->callAsyncCallable(node, ty, traitPp);
                if (asyncResult == AsyncCallResult::Ambiguous) {
                    return;
                }
                if (asyncResult == AsyncCallResult::Proven) {
                    break;
                }

                ERROR(node.span(), E0000, StringView("Unable to find an implementation of Fn*") << traitPp << StringView(" for ") << this->context.ivars.fmtType(ty));
            }

            node.argTypes = fcnArgsTup->as_Tuple();
            node.argTypes.pushBack(mv$(fcnRet));
        }
    } while (keepLooping);

    if (derefCount > 0) {
        ty = tyO;
        while (derefCount-- > 0) {
            const auto* nextTy = this->context.resolve.autoderef(node.span(), ty);
            BUG_ASSERT(nextTy);
            ty = nextTy;
            node.value = this->context.createAutoderef(mv$(node.value), ty);
        }
    }

    ASSERT_BUG(node.span(), node.argTypes.length() == node.args.size() + 1, StringView("Malformed cache in CallValue: ") << node.argTypes.length() << StringView(" != 1+") << node.args.size());
    for (unsigned int i = 0; i < node.args.size(); i++) {
        this->context.equateTypes(node.span(), node.argTypes[i], node.argIvars[i]);
    }
    this->context.equateTypes(node.span(), node.resType, node.argTypes.back());
    this->completed = true;
}

auto ExprVisitorRevisit::visit(HIRExprNodeCallMethod& node) -> void {
    const auto& sp = node.span();

    const auto& ty = this->context.getType(node.value->resType);

    TRACE_FUNCTION_F(StringView("(CallMethod) {") << this->context.ivars.fmtType(ty) << StringView("}.") << node.method << node.params << StringView("(") << FMT_CB(os, for (const auto& argNode : node.args) os << this->context.ivars.fmtType(argNode->resType) << StringView(", ");) << StringView(")") << StringView(" -> ") << this->context.ivars.fmtType(node.resType));
    BUG_ASSERT(this->coercionIndex);
    const auto& methodCoercions = *this->coercionIndex;
    if (!this->isFallback) {
        bool hasPendingReceiverCoercion = false;
        if (this->passStartIvars) {
            hasPendingReceiverCoercion = visitTyWith(ty, [&](const HIRType* inner) {
                const auto* infer = inner->opt_Infer();
                return infer && infer->index < this->passStartIvars->length() && (*this->passStartIvars)[infer->index] != this->context.getType(inner);
            });
        }
        visitTyWith(ty, [&](const HIRType* inner) {
            if (hasPendingReceiverCoercion) {
                return true;
            }
            const auto* resolved = this->context.getType(inner);
            const auto* infer = resolved->opt_Infer();
            if (!infer) {
                return false;
            }
            if (infer->index < methodCoercions.refs.size()) {
                hasPendingReceiverCoercion = !methodCoercions[infer->index].coercions.empty();
            }
            return hasPendingReceiverCoercion;
        });
        if (hasPendingReceiverCoercion) {
            DEBUG(StringView("Receiver inference has a pending coercion, pausing method lookup"));
            return;
        }
    }

    // TODO: Obtain a list of avaliable methods at that level?
    const auto* resultType = this->context.getType(node.resType);
    const HIRType* contextualResult = resultType;
    if (const auto* infer = resultType->opt_Infer(); infer && !infer->isLit() && infer->index < methodCoercions.refs.size()) {
        const HIRType* destinationType = nullptr;
        bool destinationsAgree = true;
        for (const auto& endpoint : methodCoercions[infer->index].endpoints) {
            if (endpoint.direction != SolverCoercionConstraint::Direction::InputIsSource) {
                continue;
            }
            const auto* type = this->context.getType(endpoint.other);
            if (type->is_Infer()) {
                continue;
            }
            if (destinationType && !this->context.ivars.typesEqual(destinationType, type)) {
                destinationsAgree = false;
                break;
            }
            destinationType = type;
        }
        if (destinationsAgree && destinationType) {
            /* This context comes from the method result's real deferred
             * coercion edge.  It can constrain generic method selection, but
             * it must not replace the producer's natural result type: e.g.
             * NonNull::as_ptr returns *mut T which then coerces to an expected
             * *const T. */
            contextualResult = destinationType;
        }
    }
    ThinVector<const HIRType*> methodArgumentTypes;
    methodArgumentTypes.reserve(node.args.size());
    for (const auto& argument : node.args) {
        methodArgumentTypes.push_back(argument->resType);
    }
    ThinVector<TraitResolution::MethodCandidate> possibleMethods;
    SolverResponse deferredMethodEffects;
    const auto findMethod = [&](const RcString& method) {
        possibleMethods.clear();
        deferredMethodEffects = SolverResponse{};
        auto derefCount = this->context.resolve.autoderefFindMethod(node.span(), node.traits, node.traitParamIvars, node.traitParamTypeIvars, ty, method, node.params, methodArgumentTypes, contextualResult, this->isFallback, possibleMethods, &deferredMethodEffects);
        if ((derefCount == ~0u || possibleMethods.empty()) && contextualResult != resultType) {
            possibleMethods.clear();
            deferredMethodEffects = SolverResponse{};
            derefCount = this->context.resolve.autoderefFindMethod(node.span(), node.traits, node.traitParamIvars, node.traitParamTypeIvars, ty, method, node.params, methodArgumentTypes, resultType, this->isFallback, possibleMethods, &deferredMethodEffects);
        }
        return derefCount;
    };
    unsigned int derefCount = findMethod(node.method);
    if ((derefCount == ~0u || possibleMethods.empty()) && deferredMethodEffects.certainty != SolverCertainty::Ambiguous && node.method != node.fallbackMethod) {
        derefCount = findMethod(node.fallbackMethod);
        if (derefCount != ~0u && !possibleMethods.empty()) {
            node.method = node.fallbackMethod;
        }
    }
    if (derefCount == ~0u && deferredMethodEffects.certainty == SolverCertainty::Ambiguous) {
        this->context.applySolverResponse(sp, deferredMethodEffects, &node.args);
        return;
    }
    if (derefCount != ~0u) {
        DEBUG(StringView("possible_methods = ") << possibleMethods);
        if (possibleMethods.empty()) {
            ERROR(sp, E0000, StringView("No applicable methods for {") << this->context.ivars.fmtType(ty) << StringView("}.") << node.method);
            DEBUG(StringView("possible_methods = ") << possibleMethods);
        }
        BUG_ASSERT(!possibleMethods.empty());
        if (possibleMethods.size() != 1) {
            DEBUG(StringView("- Multiple options, deferring"));
            return;
        }
        auto& selectedMethod = possibleMethods.front();
        ASSERT_BUG(sp, selectedMethod.effects.certainty == SolverCertainty::Proven, StringView("Method selection received a non-proven candidate"));
        this->context.applySolverResponse(sp, selectedMethod.effects, &node.args);
        auto& adBorrow = selectedMethod.borrow;
        auto& fcnPath = selectedMethod.path;

        if (context.currentTraitImpl) {
            const auto* selected = fcnPath.data.opt_UfcsKnown();
            const auto* currentTrait = context.resolve.currentTraitPath();
            const auto* selectedSelf = selected ? context.ivars.getType(selected->type) : nullptr;
            const auto* currentSelf = context.ivars.getType(context.currentTraitImpl->type);
            if (selected && currentTrait && (selectedSelf == currentSelf || selectedSelf->equalsIgnoringRegions(currentSelf))) {
                HIRGenericPath exactTrait;
                const auto& trait = context.crate.getTraitByPath(sp, currentTrait->path);
                if (context.resolve.traitContainsMethod(sp, *currentTrait, trait, selectedSelf, node.method, exactTrait) && selected->trait.path == exactTrait.path && selected->trait.params.types.size() == exactTrait.params.types.size()) {
                    for (unsigned int i = 0; i < selected->trait.params.types.size(); i++) {
                        const auto* selectedParam = context.ivars.getType(selected->trait.params.types[i]);
                        if (const auto* infer = selectedParam->opt_Infer()) {
                            context.addIvarDefault(sp, infer->index, exactTrait.params.types[i]);
                        }
                    }
                }
            }
        }

        node.methodPath = mv$(fcnPath);

        ASSERT_BUG(sp, visitCallPopulateCache(this->context, node.span(), node.methodPath, node.cache, selectedMethod.inherentImpl), StringView("Selected method became ambiguous while populating its cache: ") << node.methodPath);
        DEBUG(StringView("> m_method_path = ") << node.methodPath);
        BUG_ASSERT(node.cache.argTypes.length() >= 1);

        if (node.args.size() + 1 != node.cache.argTypes.length() - 1) {
            ERROR(node.span(), E0000, StringView("Incorrect number of arguments to ") << node.methodPath << StringView(" - exp ") << node.cache.argTypes.length() - 2 << StringView(" got ") << node.args.size());
        }

        DEBUG(StringView("- fcn_path=") << node.methodPath);
        for (unsigned int i = 0; i < node.args.size(); i++) {
            DEBUG(StringView("> ARG ") << i << StringView(" : ") << node.cache.argTypes[1 + i]);
            this->context.equateTypesCoerce(sp, node.cache.argTypes[1 + i], node.args[i]);
        }
        DEBUG(StringView("> Ret : ") << node.cache.argTypes.back());
        this->context.equateTypes(sp, node.resType, node.cache.argTypes.back());

        if (derefCount > 0) {
            BUG_ASSERT(derefCount < (1 << 16));
            auto& nodePtr = node.value;
            const HIRType* curTy = nodePtr->resType;
            while (derefCount--) {
                auto span = nodePtr->span();
                auto sourceTy = curTy;
                auto result = this->context.resolve.autoderefStep(span, sourceTy);
                ASSERT_BUG(span, result.result == TraitResolution::AutoderefResult::Match, StringView("Selected autoderef step no longer has a unique response for ") << sourceTy);
                if (result.implType) {
                    this->context.equateTypes(span, sourceTy, *result.implType);
                    this->context.equateTypesAssoc(span, result.target, this->context.crate.getLangItemPath(span, "deref"), {}, sourceTy, "Target", {}, true, TypeckPrimitiveOperator::Deref);
                }
                curTy = result.target;
                auto ty = result.target;

                node.value = this->context.createAutoderef(mv$(node.value), mv$(ty));
            }
        }

        if (adBorrow == TraitResolution::AutoderefBorrow::RawShared) {
            const auto& srcTy = this->context.getType(node.value->resType);
            ASSERT_BUG(sp, srcTy->is_Pointer(), StringView("RawShared adjustment on ") << srcTy);
            auto ty = context.crate.types.pointer(HIRBorrowType::Shared, srcTy->as_Pointer().inner);
            DEBUG(StringView("- Raw cast (cmd) ") << static_cast<const void*>(&*node.value) << StringView(" -> ") << ty);
            auto span = node.value->span();
            node.value = NEWNODE(ty, span, Cast, mv$(node.value), ty);
        } else if (adBorrow == TraitResolution::AutoderefBorrow::PinShared) {
            auto ty = node.cache.argTypes[0];
            DEBUG(StringView("- Pin reborrow (cmd) ") << static_cast<const void*>(&*node.value) << StringView(" -> ") << ty);
            auto span = node.value->span();
            node.value = NEWNODE(ty, span, Unsize, mv$(node.value), ty);
        } else if (adBorrow != TraitResolution::AutoderefBorrow::None) {
            HIRBorrowType bt = HIRBorrowType::Shared;
            switch (adBorrow) {
                case TraitResolution::AutoderefBorrow::None:
                case TraitResolution::AutoderefBorrow::RawShared:
                case TraitResolution::AutoderefBorrow::PinShared:
                    UNREACHABLE();
                case TraitResolution::AutoderefBorrow::Shared:
                    bt = HIRBorrowType::Shared;
                    break;
                case TraitResolution::AutoderefBorrow::Unique:
                    bt = HIRBorrowType::Unique;
                    break;
                case TraitResolution::AutoderefBorrow::Owned:
                    bt = HIRBorrowType::Owned;
                    break;
            }

            auto ty = context.crate.types.borrow(bt, node.value->resType);
            DEBUG(StringView("- Ref (cmd) ") << static_cast<const void*>(&*node.value) << StringView(" -> ") << ty);
            auto span = node.value->span();
            node.value = NEWNODE(mv$(ty), span, Borrow, bt, mv$(node.value));
        } else {
        }

        this->context.equateTypes(sp, node.cache.argTypes[0], node.value->resType);

        this->completed = true;
    }
}

auto ExprVisitorRevisit::visit(HIRExprNodeField& node) -> void {
    const auto& fieldName = node.field;

    TRACE_FUNCTION_F(StringView("(Field) name=") << fieldName << StringView(", ty = ") << this->context.ivars.fmtType(node.value->resType));
    const HIRType* outType;

    unsigned int derefCount = 0;
    const HIRType* tmpType;
    const auto* currentTy = node.value->resType;
    Vector<const HIRType*> derefResTypes;

    // TODO: autoderef_find_field?
    do {
        const auto* ty = this->context.revealOpaqueType(currentTy);
        if (ty->is_Infer()) {
            DEBUG(StringView("Hit ivar, returning early"));
            return;
        }
        if (ty->is_Path() && ty->as_Path().binding.is_Unbound()) {
            DEBUG(StringView("Hit unbound path, returning early"));
            return;
        }
        if (const auto* fieldType = this->context.resolve.findField(node.span(), ty, fieldName)) {
            outType = fieldType;
            this->context.equateTypes(node.span(), node.resType, outType);
            break;
        }

        derefCount += 1;
        currentTy = this->context.resolve.autoderef(node.span(), ty);
        if (currentTy) {
            derefResTypes.pushBack(currentTy);
        }
    } while (currentTy);

    if (!currentTy) {
        ERROR(node.span(), E0000, StringView("Couldn't find the field ") << fieldName << StringView(" in ") << this->context.ivars.fmtType(node.value->resType));
    }

    BUG_ASSERT(derefCount == derefResTypes.length());
    for (unsigned int i = 0; i < derefResTypes.length(); i++) {
        auto ty = mv$(derefResTypes[i]);
        DEBUG(StringView("- Deref ") << static_cast<const void*>(&*node.value) << StringView(" -> ") << ty);
        if (node.value->resType->is_Array()) {
            BUG(node.span(), StringView("Field access from array/slice?"));
        }
        node.value = NEWNODE(mv$(ty), node.span(), Deref, mv$(node.value));
        context.ivars.getType(node.value->resType);
    }

    completed = true;
}

auto ExprVisitorRevisit::visit(HIRExprNodeLiteral& node) -> void {
    noRevisit(node);
}

auto ExprVisitorRevisit::visit(HIRExprNodeUnitVariant& node) -> void {
    noRevisit(node);
}

auto ExprVisitorRevisit::visit(HIRExprNodePathValue& node) -> void {
    if (!node.path.data.is_UfcsInherent()) {
        noRevisit(node);
    }
    completed = resolveInherentPathValue(context, node);
}

auto ExprVisitorRevisit::visit(HIRExprNodeVariable& node) -> void {
    noRevisit(node);
}

auto ExprVisitorRevisit::visit(HIRExprNodeConstParam& node) -> void {
    noRevisit(node);
}

auto ExprVisitorRevisit::visit(HIRExprNodeStructLiteral& node) -> void {
    noRevisit(node);
}

auto ExprVisitorRevisit::visit(HIRExprNodeTuple& node) -> void {
    noRevisit(node);
}

auto ExprVisitorRevisit::visit(HIRExprNodeArrayList& node) -> void {
    noRevisit(node);
}

auto ExprVisitorRevisit::visit(HIRExprNodeArraySized& node) -> void {
    if (cast<HIRExprNodeConstBlock>(node.val.get())) {
        completed = true;
        return;
    }
    if (const auto* path = cast<HIRExprNodePathValue>(node.val.get())) {
        if (path->target == HIRExprNodePathValue::CONSTANT) {
            completed = true;
            return;
        }
        if (!path->path.data.is_Generic()) {
            completed = true;
            return;
        }
    }

    bool requireCopy = false;
    if (node.size.is_Known()) {
        requireCopy = node.size.as_Known() > 1;
    } else {
        const auto& count = this->context.ivars.getValue(node.size.as_Unevaluated());
        if (count.is_Infer()) {
            return;
        }
        if (count.is_Evaluated()) {
            requireCopy = count.as_Evaluated()->readUsize(0) > 1;
        } else {
            requireCopy = true;
        }
    }

    if (requireCopy) {
        this->context.addTraitBound(node.span(), node.val->resType, this->context.resolve.langCopy(), {});
    }
    completed = true;
}

auto ExprVisitorRevisit::visit(HIRExprNodeClosure& node) -> void {
    noRevisit(node);
}

auto ExprVisitorRevisit::visit(HIRExprNodeGenerator& node) -> void {
    noRevisit(node);
}

auto ExprVisitorRevisit::visit(HIRExprNodeGeneratorWrapper& node) -> void {
    noRevisit(node);
}

auto ExprVisitorRevisit::visit(HIRExprNodeAsyncBlock& node) -> void {
    noRevisit(node);
}

auto ExprVisitorRevisit::noRevisit(HIRExprNode& node) -> void {
    BUG(node.span(), StringView("Node revisit unexpected - ") << typeid(node).name());
}

ExprVisitorApply::ExprVisitorApply(Context& context)
    : HIRExprVisitorDef(context.crate.types)
    , context(context)
    , ivars(context.ivars)
{
    nopImpl = context.resolve.implGenerics().makeNopParams(context.crate.types, 0);
    nopItem = context.resolve.itemGenerics().makeNopParams(context.crate.types, 1);
}

auto ExprVisitorApply::visitNodePtr(HIRExprPtr& nodePtr) -> void {
    auto& node = *nodePtr;
    const char* nodeTy = typeid(node).name();

    TRACE_FUNCTION_FR(static_cast<const void*>(&node) << StringView(" ") << static_cast<const void*>(&node) << StringView(" ") << nodeTy << StringView(" : ") << node.resType, nodeTy);
    node.resType = this->checkTypeResolvedTop(node.span(), node.resType);

    DEBUG(nodeTy << StringView(" : = ") << node.resType);
    nodePtr->visit(*this);

    for (auto& type : mutRange(nodePtr.bindings)) {
        type = this->checkTypeResolvedTop(node.span(), type);
    }

    for (auto& type : mutRange(nodePtr.erasedTypes)) {
        type = this->checkTypeResolvedTop(node.span(), type);
    }

    for (auto& ent : context.erasedTypeAliases) {
        auto t = ent.second.ourType;
        t = checkTypeResolved(node.span(), t, t);
        if (t->is_ErasedType() && t->as_ErasedType().inner.is_Alias() && t->as_ErasedType().inner.as_Alias().inner.get() == ent.first) {
            continue;
        }
        OpaqueAliasParamMonomorph aliasMonomorph{context.crate.types, *ent.first, ent.second.params};
        auto ty = aliasMonomorph.monomorphType(node.span(), t);
        {
            auto p = ent.first->generics.makeNopParams(context.crate.types, 0);
            MonomorphStatePtr(context.crate.types, nullptr, &p, nullptr).monomorphType(node.span(), ty);
        }
        if (ent.first->type == nullptr) {
            DEBUG(StringView("type ") << ent.first->path << StringView(" = ") << ty);
            ent.first->type = std::move(ty);
        } else {
            if (ent.first->type != ty) {
                ERROR(node.span(), E0000, StringView("Disagreement on type for ") << ent.first->path << StringView(": ") << ent.first->type << StringView(" or ") << ty);
            }
        }
    }
}

auto ExprVisitorApply::visitNodePtr(HIRExprNodeP& nodePtr) -> void {
    auto& node = *nodePtr;
    const char* nodeTy = typeid(node).name();
    TRACE_FUNCTION_FR(static_cast<const void*>(&nodePtr) << StringView(" ") << static_cast<const void*>(&node) << StringView(" ") << nodeTy << StringView(" : ") << node.resType, static_cast<const void*>(&node) << StringView(" ") << nodeTy);
    node.resType = this->checkTypeResolvedTop(node.span(), node.resType);
    DEBUG(nodeTy << StringView(" : = ") << node.resType);
    HIRExprVisitorDef::visitNodePtr(nodePtr);
}

auto ExprVisitorApply::visitPattern(const Span& sp, HIRPattern& pat) -> void {
    if (auto* deref = pat.data.opt_Deref()) {
        ASSERT_BUG(sp, deref->targetType, StringView("Untyped deref pattern"));
        deref->targetType = this->checkTypeResolvedTop(sp, deref->targetType);
    }
    switch (pat.data.tag()) {
        default:
            break;
        case HIRPatternData::TAG_Value: {
            auto& e = pat.data.as_Value();
            if (e.val.is_Named()) {
                auto& ve = e.val.as_Named();
                this->checkTypeResolvedPath(sp, ve.path);
            }
            break;
        }
        case HIRPatternData::TAG_Range: {
            auto& e = pat.data.as_Range();
            if (e.start && e.start->is_Named()) {
                this->checkTypeResolvedPath(sp, e.start->as_Named().path);
            }
            if (e.end && e.end->is_Named()) {
                this->checkTypeResolvedPath(sp, e.end->as_Named().path);
            }
            break;
        }
        case HIRPatternData::TAG_PathValue: {
            auto& e = pat.data.as_PathValue();
            this->checkTypeResolvedPath(sp, e.path);
            break;
        }
        case HIRPatternData::TAG_PathTuple: {
            auto& e = pat.data.as_PathTuple();
            this->checkTypeResolvedPath(sp, e.path);
            break;
        }
        case HIRPatternData::TAG_PathNamed: {
            auto& e = pat.data.as_PathNamed();
            this->checkTypeResolvedPath(sp, e.path);
            break;
        }
    }
    HIRExprVisitorDef::visitPattern(sp, pat);
}

auto ExprVisitorApply::visit(HIRExprNodeBlock& node) -> void {
    HIRExprVisitorDef::visit(node);
    if (node.valueNode) {
        checkTypesEqual(node.span(), node.resType, node.valueNode->resType);
    } else if (node.diverges) {
    } else {
        checkTypesEqual(node.span(), node.resType, context.crate.types.unit());
    }
}

auto ExprVisitorApply::visit(HIRExprNodeLet& node) -> void {
    node.type = this->checkTypeResolvedTop(node.span(), node.type);
    HIRExprVisitorDef::visit(node);
}

auto ExprVisitorApply::visit(HIRExprNodeClosure& node) -> void {
    for (auto& arg : node.args) {
        arg.second = this->checkTypeResolvedTop(node.span(), arg.second);
    }
    if (const auto* expected = context.closureReturnExpectation(&node)) {
        node.returnType = expected;
    }
    node.returnType = this->checkTypeResolvedTop(node.span(), node.returnType);
    HIRExprVisitorDef::visit(node);
}

auto ExprVisitorApply::visit(HIRExprNodeGenerator& node) -> void {
    node.returnType = this->checkTypeResolvedTop(node.span(), node.returnType);
    node.yieldTy = this->checkTypeResolvedTop(node.span(), node.yieldTy);
    node.resumeTy = this->checkTypeResolvedTop(node.span(), node.resumeTy);
    HIRExprVisitorDef::visit(node);
}

auto ExprVisitorApply::visit(HIRExprNodeAsyncBlock& node) -> void {
    node.returnType = this->checkTypeResolvedTop(node.span(), node.returnType);
    if (node.isAsyncGen) {
        node.yieldTy = this->checkTypeResolvedTop(node.span(), node.yieldTy);
    }
    HIRExprVisitorDef::visit(node);
}

auto ExprVisitorApply::visit(HIRExprNodeGeneratorWrapper& node) -> void {
    BUG(node.span(), StringView(""));
}

auto ExprVisitorApply::visitCallcache(const Span& sp, HIRExprCallCache& cache) -> void {
    for (auto& ty : mutRange(cache.argTypes)) {
        ty = this->checkTypeResolvedTop(sp, ty);
    }
}

auto ExprVisitorApply::visit(HIRExprNodeCallPath& node) -> void {
    this->visitCallcache(node.span(), node.cache);

    this->checkTypeResolvedPath(node.span(), node.path);
    HIRExprVisitorDef::visit(node);
}

auto ExprVisitorApply::visit(HIRExprNodeCallMethod& node) -> void {
    this->visitCallcache(node.span(), node.cache);

    this->checkTypeResolvedPath(node.span(), node.methodPath);
    const auto* methodType = context.crate.types.path(node.methodPath.clone(), {});
    this->checkTypeResolvedPp(node.span(), node.params, methodType);
    HIRExprVisitorDef::visit(node);
}

auto ExprVisitorApply::visit(HIRExprNodeCallValue& node) -> void {
    for (auto& ty : mutRange(node.argTypes)) {
        ty = this->checkTypeResolvedTop(node.span(), ty);
    }

    {
        const auto& ty = context.getType(node.value->resType);
        switch (node.traitUsed) {
            case HIRExprNodeCallValue::TraitUsed::AsyncFn:
            case HIRExprNodeCallValue::TraitUsed::AsyncFnMut:
            case HIRExprNodeCallValue::TraitUsed::AsyncFnOnce:
                HIRExprVisitorDef::visit(node);
                return;
            default:
                break;
        }
        if (ty->is_NodeType() && ty->as_NodeType().is_Closure()) {
            node.traitUsed = HIRExprNodeCallValue::TraitUsed::Unknown;
        } else if (/*const auto* e =*/ty->opt_Function()) {
            node.traitUsed = HIRExprNodeCallValue::TraitUsed::Fn;
        } else {
            HIRPathParams traitPp;
            {
                Vector<const HIRType*> argTypes;
                for (const auto& argTy : node.argIvars) {
                    argTypes.pushBack(this->context.getType(argTy));
                }
                traitPp.types.push_back(context.crate.types.tuple(mv$(argTypes)));
            }

            if (!this->context.resolve.langFn().components().empty() && this->context.resolve.selectTraitGoal(node.span(), this->context.resolve.langFn(), traitPp, ty, [&](SolverSelection) {
                return true;
            })) {
                DEBUG(StringView("-- Using Fn"));
                node.traitUsed = HIRExprNodeCallValue::TraitUsed::Fn;
            } else if (!this->context.resolve.langFnMut().components().empty() && this->context.resolve.selectTraitGoal(node.span(), this->context.resolve.langFnMut(), traitPp, ty, [&](SolverSelection) {
                return true;
            })) {
                DEBUG(StringView("-- Using FnMut"));
                node.traitUsed = HIRExprNodeCallValue::TraitUsed::FnMut;
            } else {
                DEBUG(StringView("-- Using FnOnce (default)"));
                node.traitUsed = HIRExprNodeCallValue::TraitUsed::FnOnce;
            }
        }
    }

    HIRExprVisitorDef::visit(node);
}

auto ExprVisitorApply::visit(HIRExprNodePathValue& node) -> void {
    this->checkTypeResolvedPath(node.span(), node.path);
}

auto ExprVisitorApply::visit(HIRExprNodeUnitVariant& node) -> void {
    this->checkTypeResolvedGenericpath(node.span(), node.path);
}

auto ExprVisitorApply::visit(HIRExprNodeStructLiteral& node) -> void {
    this->checkTypeResolvedGenericpath(node.span(), node.realPath);
    for (auto& ty : mutRange(node.valueTypes)) {
        if (ty != nullptr) {
            ty = this->checkTypeResolvedTop(node.span(), ty);
        }
    }

    const auto& sp = node.span();
    const auto& tyPath = node.realPath;
    const auto& ty = node.resType;
    ASSERT_BUG(sp, ty->is_Path(), StringView("Result type of _StructLiteral isn't Path"));
    const tStructFields* fieldsPtr = nullptr;
    {
        auto& tuMatch = ty->as_Path().binding;
        switch (tuMatch.tag()) {
            case HIRTypePathBinding::TAG_Unbound: {
                break;
            }
            case HIRTypePathBinding::TAG_Opaque: {
                break;
            }
            case HIRTypePathBinding::TAG_Enum: {
                auto& e = tuMatch.as_Enum();
                const auto& varName = tyPath.path.components().back();
                const auto& enm = *e;
                auto idx = enm.findVariant(varName);
                ASSERT_BUG(sp, idx != SIZE_MAX, StringView(""));
                ASSERT_BUG(sp, enm.data.is_Data(), StringView(""));
                const auto& var = enm.data.as_Data()[idx];

                const auto& str = *var.type->as_Path().binding.as_Struct();
                ASSERT_BUG(sp, var.isStruct, StringView("Struct literal for enum on non-struct variant"));
                fieldsPtr = &str.data.as_Named();
                break;
            }
            case HIRTypePathBinding::TAG_Union: {
                auto& e = tuMatch.as_Union();
                fieldsPtr = &e->variants;
                ASSERT_BUG(node.span(), node.values.size() > 0, StringView("Union with no values"));
                ASSERT_BUG(node.span(), node.values.size() == 1, StringView("Union with multiple values"));
                ASSERT_BUG(node.span(), !node.baseValue, StringView("Union can't have a base value"));
                break;
            }
            case HIRTypePathBinding::TAG_ExternType: {
                BUG(sp, StringView("ExternType in StructLiteral"));
                break;
            }
            case HIRTypePathBinding::TAG_Struct: {
                auto& e = tuMatch.as_Struct();
                if (e->data.is_Unit()) {
                    ASSERT_BUG(node.span(), node.values.size() == 0, StringView("Values provided for unit-like struct"));
                    ASSERT_BUG(node.span(), !node.baseValue, StringView("Values provided for unit-like struct"));
                    return;
                }
                if (e->data.is_Tuple()) {
                    ASSERT_BUG(node.span(), node.baseValue || !node.values.empty(), StringView("Tuple struct literal has no values or base"));
                    HIRExprVisitorDef::visit(node);
                    return;
                }

                ASSERT_BUG(node.span(), e->data.is_Named(), StringView("StructLiteral not pointing to a braced struct, instead ") << e->data.tagStr() << StringView(" - ") << ty);
                fieldsPtr = &e->data.as_Named();
                break;
            }
        }
    }
    ASSERT_BUG(node.span(), fieldsPtr, StringView("Didn't get field for path in _StructLiteral - ") << ty);

    HIRExprVisitorDef::visit(node);
}

auto ExprVisitorApply::visit(HIRExprNodeTupleVariant& node) -> void {
    this->checkTypeResolvedPp(node.span(), node.path.params, nullptr);
    for (auto& ty : mutRange(node.argTypes)) {
        if (ty != nullptr) {
            ty = this->checkTypeResolvedTop(node.span(), ty);
        }
    }

    HIRExprVisitorDef::visit(node);
}

auto ExprVisitorApply::visit(HIRExprNodeLiteral& node) -> void {
    const HIRType* literalType = node.resType;
    if (const auto* pattern = literalType->opt_Pattern()) {
        literalType = pattern->inner;
    }
    switch (node.data.tag()) {
        case HIRExprNodeLiteral::Data::TAG_Integer: {
            auto& e = node.data.as_Integer();
            ASSERT_BUG(node.span(), literalType->is_Primitive(), StringView("Integer _Literal didn't return primitive-backed type - ") << node.resType);
            e.type = literalType->as_Primitive();
            break;
        }
        case HIRExprNodeLiteral::Data::TAG_Float: {
            auto& e = node.data.as_Float();
            ASSERT_BUG(node.span(), literalType->is_Primitive(), StringView("Float Literal didn't return primitive-backed type - ") << node.resType);
            e.type = literalType->as_Primitive();
            break;
        }
        case HIRExprNodeLiteral::Data::TAG_Boolean: {
            break;
        }
        case HIRExprNodeLiteral::Data::TAG_ByteString: {
            break;
        }
        case HIRExprNodeLiteral::Data::TAG_CString: {
            break;
        }
        case HIRExprNodeLiteral::Data::TAG_String: {
            break;
        }
    }
}

auto ExprVisitorApply::visit(HIRExprNodeCast& node) -> void {
    node.dstType = this->checkTypeResolvedTop(node.span(), node.dstType);
    HIRExprVisitorDef::visit(node);
}

auto ExprVisitorApply::visit(HIRExprNodeUnsize& node) -> void {
    node.dstType = this->checkTypeResolvedTop(node.span(), node.dstType);
    HIRExprVisitorDef::visit(node);
}

auto ExprVisitorApply::checkTypeResolvedTop(const Span& sp, const HIRType* ty) const -> const HIRType* {
    ty = checkTypeResolved(sp, ty, ty);
    ty = this->context.expandAssociatedTypes(sp, mv$(ty));
    DEBUG(ty);
    return ty;
}

auto ExprVisitorApply::checkTypeResolvedConstgeneric(const Span& sp, HIRConstGeneric& v, const HIRType* topType) const -> void {
    if (v.is_Infer()) {
        auto val = ivars.getValue(v).clone();
        ASSERT_BUG(sp, !val.is_Infer(), StringView("Failure to infer ") << v << StringView(" in ") << topType);
        v = std::move(val);
    }
}

auto ExprVisitorApply::checkTypeResolvedPp(const Span& sp, HIRPathParams& pp, const HIRType* topType) const -> void {
    for (auto& ty : pp.types) {
        ty = checkTypeResolved(sp, ty, topType);
    }
    for (auto& val : pp.values) {
        checkTypeResolvedConstgeneric(sp, val, topType);
    }
}

auto ExprVisitorApply::checkTypeResolvedPath(const Span& sp, HIRPath& path) const -> void {
    auto tmp = context.crate.types.path(path.clone(), {});
    checkTypeResolvedPath(sp, path, tmp);
    switch (path.data.tag()) {
        case HIRPath::Data::TAG_Generic: {
            auto& pe = path.data.as_Generic();
            for (auto& ty : pe.params.types) {
                ty = this->context.expandAssociatedTypes(sp, mv$(ty));
            }
            break;
        }
        case HIRPath::Data::TAG_UfcsInherent: {
            auto& pe = path.data.as_UfcsInherent();
            pe.type = this->context.expandAssociatedTypes(sp, mv$(pe.type));
            for (auto& ty : pe.params.types) {
                ty = this->context.expandAssociatedTypes(sp, mv$(ty));
            }
            for (auto& ty : pe.implParams.types) {
                ty = this->context.expandAssociatedTypes(sp, mv$(ty));
            }
            break;
        }
        case HIRPath::Data::TAG_UfcsKnown: {
            auto& pe = path.data.as_UfcsKnown();
            pe.type = this->context.expandAssociatedTypes(sp, mv$(pe.type));
            for (auto& ty : pe.params.types) {
                ty = this->context.expandAssociatedTypes(sp, mv$(ty));
            }
            for (auto& ty : pe.trait.params.types) {
                ty = this->context.expandAssociatedTypes(sp, mv$(ty));
            }
            break;
        }
        case HIRPath::Data::TAG_UfcsUnknown: {
            UNREACHABLE();
        }
    }
}

auto ExprVisitorApply::checkTypeResolvedPath(const Span& sp, HIRPath& path, const HIRType* topType) const -> void {
    switch (path.data.tag()) {
        case HIRPath::Data::TAG_Generic: {
            auto& pe = path.data.as_Generic();
            checkTypeResolvedPp(sp, pe.params, topType);
            break;
        }
        case HIRPath::Data::TAG_UfcsInherent: {
            auto& pe = path.data.as_UfcsInherent();
            pe.type = checkTypeResolved(sp, pe.type, topType);
            checkTypeResolvedPp(sp, pe.params, topType);
            checkTypeResolvedPp(sp, pe.implParams, topType);
            break;
        }
        case HIRPath::Data::TAG_UfcsKnown: {
            auto& pe = path.data.as_UfcsKnown();
            pe.type = checkTypeResolved(sp, pe.type, topType);
            checkTypeResolvedPp(sp, pe.trait.params, topType);
            checkTypeResolvedPp(sp, pe.params, topType);
            break;
        }
        case HIRPath::Data::TAG_UfcsUnknown: {
            ERROR(sp, E0000, StringView("UfcsUnknown ") << path << StringView(" left in ") << topType);
            break;
        }
    }
}

auto ExprVisitorApply::checkTypeResolvedGenericpath(const Span& sp, HIRGenericPath& path) const -> void {
    auto tmp = context.crate.types.path(path.clone(), {});
    checkTypeResolvedPp(sp, path.params, tmp);
}

auto ExprVisitorApply::checkTypeResolved(const Span& sp, const HIRType* ty, const HIRType* topType) const -> const HIRType* {
    struct InnerVisitor: public HIRVisitor {
        struct ActiveType {
            const HIRType* type;
            const ActiveType* parent;
        };

        const ExprVisitorApply& parent;
        const Span& sp;
        const HIRType* topType;
        const ActiveType* activeTypes = nullptr;

        InnerVisitor(const ExprVisitorApply& parent, const Span& sp, const HIRType* topType)
            : HIRVisitor(nullptr, parent.context.crate.types)
            , parent(parent)
            , sp(sp)
            , topType(topType)
        {
        }

        void visitPath(HIRPath& path, HIRVisitor::PathContext pc) override {
            if (path.data.is_UfcsUnknown()) {
                ERROR(sp, E0000, StringView("UfcsUnknown ") << path << StringView(" left in ") << topType);
            }
            HIRVisitor::visitPath(path, pc);
        }

        void visitConstgeneric(HIRConstGeneric& v) override {
            if (v.is_Infer()) {
                auto val = parent.ivars.getValue(v).clone();
                DEBUG(v << StringView(" -> ") << val);
                v = std::move(val);
            }
            HIRVisitor::visitConstgeneric(v);
        }

        [[nodiscard]] const HIRType* visitType(const HIRType* ty) override {
            if (ty->is_Infer()) {
                auto newTy = parent.ivars.getType(ty);
                DEBUG(ty << StringView(" -> ") << newTy);
                ty = mv$(newTy);
                if (ty->is_Infer()) {
                    ERROR(sp, E0000, StringView("Failed to infer type ") << ty << StringView(" in ") << topType);
                }
            }

            for (auto* active = activeTypes; active; active = active->parent) {
                if (active->type == ty) {
                    return ty;
                }
            }
            const ActiveType activeType{ty, activeTypes};
            activeTypes = &activeType;
            ty = visitTypeDefaultViaHooks(ty);
            activeTypes = activeType.parent;

            if (ty->is_Array()) {
                auto data = ty->cloneData();
                auto& size = data.as_Array().size;
                if (size.is_Unevaluated()) {
                    if (size.as_Unevaluated().is_Evaluated()) {
                        DEBUG(StringView("Known size: ") << size);
                        size = size.as_Unevaluated().as_Evaluated()->readUsize(0);
                        ty = parent.context.crate.types.intern(std::move(data));
                    }
                }
            }
            return ty;
        }
    };

    InnerVisitor v(*this, sp, topType);
    return v.visitType(ty);
}

auto ExprVisitorApply::checkTypesEqual(const Span& sp, const HIRType* l, const HIRType* r) const -> void {
    DEBUG(sp << StringView(" - ") << l << StringView(" == ") << r);
    if (r->is_Diverge()) {
    } else if (l != r && !l->equalsIgnoringRegions(r)) {
        ERROR(sp, E0000, StringView("Type mismatch\n - ") << l << StringView("\n!= ") << r);
    } else {
    }
}

auto ExprVisitorApply::visit(HIRExprNodeArraySized& node) -> void {
    HIRExprVisitorDef::visit(node);
    if (node.size.is_Unevaluated() && node.size.as_Unevaluated().is_Infer()) {
        auto count = ivars.getValue(node.size.as_Unevaluated()).clone();
        ASSERT_BUG(node.span(), !count.is_Infer(), StringView("Failure to infer the length of ") << node.resType);
        if (count.is_Evaluated()) {
            node.size = HIRArraySize::make_Known(count.as_Evaluated()->readUsize(0));
        } else {
            node.size = HIRArraySize(std::move(count));
        }
    }
}

ExprVisitorPrint::ExprVisitorPrint(const Context& context, ZeroCopyOutput& os)
    : context(context)
    , os(os)
{
}

auto ExprVisitorPrint::visit(HIRExprNodeBlock& node) -> void {
    os << StringView("_Block {") << context.ivars.fmtType(node.nodes.back()->resType) << StringView("}");
}

auto ExprVisitorPrint::visit(HIRExprNodeConstBlock& node) -> void {
    noRevisit(node);
}

auto ExprVisitorPrint::visit(HIRExprNodeAsm& node) -> void {
    noRevisit(node);
}

auto ExprVisitorPrint::visit(HIRExprNodeAsm2& node) -> void {
    noRevisit(node);
}

auto ExprVisitorPrint::visit(HIRExprNodeReturn& node) -> void {
    noRevisit(node);
}

auto ExprVisitorPrint::visit(HIRExprNodeYield& node) -> void {
    noRevisit(node);
}

auto ExprVisitorPrint::visit(HIRExprNodeAWait& node) -> void {
    noRevisit(node);
}

auto ExprVisitorPrint::visit(HIRExprNodeUse& node) -> void {
    noRevisit(node);
}

auto ExprVisitorPrint::visit(HIRExprNodeLet& node) -> void {
    noRevisit(node);
}

auto ExprVisitorPrint::visit(HIRExprNodeLoop& node) -> void {
    noRevisit(node);
}

auto ExprVisitorPrint::visit(HIRExprNodeLoopControl& node) -> void {
    noRevisit(node);
}

auto ExprVisitorPrint::visit(HIRExprNodeMatch& node) -> void {
    noRevisit(node);
}

auto ExprVisitorPrint::visit(HIRExprNodeAssign& node) -> void {
    noRevisit(node);
}

auto ExprVisitorPrint::visit(HIRExprNodeBinOp& node) -> void {
    noRevisit(node);
}

auto ExprVisitorPrint::visit(HIRExprNodeUniOp& node) -> void {
    noRevisit(node);
}

auto ExprVisitorPrint::visit(HIRExprNodeBorrow& node) -> void {
    noRevisit(node);
}

auto ExprVisitorPrint::visit(HIRExprNodeRawBorrow& node) -> void {
    noRevisit(node);
}

auto ExprVisitorPrint::visit(HIRExprNodeCast& node) -> void {
    os << StringView("_Cast {") << context.ivars.fmtType(node.value->resType) << StringView("}");
}

auto ExprVisitorPrint::visit(HIRExprNodeUnsize& node) -> void {
    noRevisit(node);
}

auto ExprVisitorPrint::visit(HIRExprNodeIndex& node) -> void {
    os << StringView("_Index {") << fmtResTy(*node.value) << StringView("}[{") << fmtResTy(*node.index) << StringView("}]");
}

auto ExprVisitorPrint::visit(HIRExprNodeDeref& node) -> void {
    os << StringView("_Deref {") << fmtResTy(*node.value) << StringView("}");
}

auto ExprVisitorPrint::visit(HIRExprNodeEmplace& node) -> void {
    os << StringView("_Emplace(") << fmtResTy(*node.value) << StringView(" in ") << fmtResTy(*node.place) << StringView(")");
}

auto ExprVisitorPrint::visit(HIRExprNodeTupleVariant& node) -> void {
    noRevisit(node);
}

auto ExprVisitorPrint::visit(HIRExprNodeCallPath& node) -> void {
    noRevisit(node);
}

auto ExprVisitorPrint::visit(HIRExprNodeCallValue& node) -> void {
    os << StringView("_CallValue {") << fmtResTy(*node.value) << StringView("}(");
    for (const auto& arg : node.args) {
        os << StringView("{") << fmtResTy(*arg) << StringView("}, ");
    }
    os << StringView(")");
}

auto ExprVisitorPrint::visit(HIRExprNodeCallMethod& node) -> void {
    os << StringView("_CallMethod {") << fmtResTy(*node.value) << StringView("}.") << node.method << StringView("(");
    for (const auto& arg : node.args) {
        os << StringView("{") << fmtResTy(*arg) << StringView("}, ");
    }
    os << StringView(")");
}

auto ExprVisitorPrint::visit(HIRExprNodeField& node) -> void {
    os << StringView("_Field {") << fmtResTy(*node.value) << StringView("}.") << node.field;
}

auto ExprVisitorPrint::visit(HIRExprNodeLiteral& node) -> void {
    noRevisit(node);
}

auto ExprVisitorPrint::visit(HIRExprNodeUnitVariant& node) -> void {
    noRevisit(node);
}

auto ExprVisitorPrint::visit(HIRExprNodePathValue& node) -> void {
    noRevisit(node);
}

auto ExprVisitorPrint::visit(HIRExprNodeVariable& node) -> void {
    noRevisit(node);
}

auto ExprVisitorPrint::visit(HIRExprNodeConstParam& node) -> void {
    noRevisit(node);
}

auto ExprVisitorPrint::visit(HIRExprNodeStructLiteral& node) -> void {
    noRevisit(node);
}

auto ExprVisitorPrint::visit(HIRExprNodeTuple& node) -> void {
    noRevisit(node);
}

auto ExprVisitorPrint::visit(HIRExprNodeArrayList& node) -> void {
    noRevisit(node);
}

auto ExprVisitorPrint::visit(HIRExprNodeArraySized& node) -> void {
    noRevisit(node);
}

auto ExprVisitorPrint::visit(HIRExprNodeClosure& node) -> void {
    noRevisit(node);
}

auto ExprVisitorPrint::visit(HIRExprNodeGenerator& node) -> void {
    noRevisit(node);
}

auto ExprVisitorPrint::visit(HIRExprNodeGeneratorWrapper& node) -> void {
    noRevisit(node);
}

auto ExprVisitorPrint::visit(HIRExprNodeAsyncBlock& node) -> void {
    noRevisit(node);
}

auto ExprVisitorPrint::fmtResTy(const HIRExprNode& n) -> HMTypeInferrence::FmtType {
    return context.ivars.fmtType(n.resType);
}

auto ExprVisitorPrint::noRevisit(HIRExprNode& n) -> void {
    os << StringView("_") << typeid(n).name() << StringView(" {") << context.ivars.fmtType(n.resType) << StringView("}");
}

auto ConstExprEquate::getParam(const HIRConstGenericUnevaluated& value, unsigned int binding) const -> const HIRConstGeneric* {
    const HIRPathParams* params = nullptr;
    switch (binding >> 8) {
        case GENERICImpl:
            params = &value.paramsImpl;
            break;
        case GENERICItem:
            params = &value.paramsItem;
            break;
        default:
            return nullptr;
    }
    const unsigned int index = binding & 0xFF;
    return index < params->values.size() ? &params->values[index] : nullptr;
}

auto ConstExprEquate::identity(const HIRConstGeneric& value) const -> const HIRConstGeneric* {
    const auto* unevaluated = value.opt_Unevaluated();
    if (!unevaluated) {
        return nullptr;
    }

    const HIRExprNode* node = &**(*unevaluated)->expr;
    for (;;) {
        if (const auto* block = cast<const HIRExprNodeBlock>(node)) {
            if (!block->nodes.empty() || !block->valueNode) {
                return nullptr;
            }
            node = &*block->valueNode;
            continue;
        }
        if (const auto* block = cast<const HIRExprNodeConstBlock>(node)) {
            node = &*block->inner;
            continue;
        }
        break;
    }

    const auto* param = cast<const HIRExprNodeConstParam>(node);
    return param ? getParam(**unevaluated, param->binding) : nullptr;
}

auto ConstExprEquate::equateIdentity(const HIRConstGeneric& value, const HIRConstGeneric& other) const -> bool {
    const auto* replacement = identity(value);
    if (!replacement || *replacement == value) {
        return false;
    }
    context.equateValues(sp, *replacement, other);
    return true;
}

auto ConstExprEquate::equateLiteral(const HIRExprNodeLiteral& left, const HIRExprNodeLiteral& right) const -> bool {
    if (left.data.tag() != right.data.tag()) {
        return false;
    }
    switch (left.data.tag()) {
        case HIRExprLiteral::TAG_Integer: {
            auto& l = left.data.as_Integer();
            auto& r = right.data.as_Integer();
            return l.type == r.type && l.value == r.value;
        }
        case HIRExprLiteral::TAG_Float: {
            auto& l = left.data.as_Float();
            auto& r = right.data.as_Float();
            return l.type == r.type && l.value == r.value;
        }
        case HIRExprLiteral::TAG_Boolean: {
            auto& l = left.data.as_Boolean();
            auto& r = right.data.as_Boolean();
            return l == r;
        }
        case HIRExprLiteral::TAG_String: {
            auto& l = left.data.as_String();
            auto& r = right.data.as_String();
            return l == r;
        }
        case HIRExprLiteral::TAG_CString: {
            auto& l = left.data.as_CString();
            auto& r = right.data.as_CString();
            return l.v == r.v;
        }
        case HIRExprLiteral::TAG_ByteString: {
            auto& l = left.data.as_ByteString();
            auto& r = right.data.as_ByteString();
            return l.length() == r.length() && std::equal(l.begin(), l.end(), r.begin());
        }
    }
    UNREACHABLE();
}

auto ConstExprEquate::evaluatedPath(const HIRConstGenericUnevaluated& value, const HIRExprNodePathValue& node) const -> const EncodedLiteral* {
    MonomorphStatePtr monomorph(context.crate.types, value.selfType, &value.paramsImpl, &value.paramsItem);
    auto path = monomorph.monomorphPath(sp, node.path);
    StaticTraitResolve resolve(context.resolve.board());
    MonomorphState params(context.crate.types);
    auto resolved = resolve.getValue(sp, path, params);
    const auto* constant = resolved.opt_Constant();
    if (!constant) {
        return nullptr;
    }

    const auto& item = **constant;
    if (item.valueState == HIRConstant::ValueState::Known) {
        return &item.valueRes;
    }
    if (item.valueState == HIRConstant::ValueState::Generic) {
        auto cached = item.monomorphCache.find(path);
        return cached == item.monomorphCache.end() ? nullptr : &cached->second;
    }
    return nullptr;
}

auto ConstExprEquate::equateLiteralEvaluated(const HIRExprNodeLiteral& literal, const EncodedLiteral& evaluated) const -> bool {
    if (!evaluated.relocations.empty() || evaluated.bytes.empty()) {
        return false;
    }
    if (const auto* integer = literal.data.opt_Integer()) {
        return EncodedLiteralSlice(evaluated).readUint() == integer->value;
    }
    if (const auto* boolean = literal.data.opt_Boolean()) {
        return (EncodedLiteralSlice(evaluated).readUint() != 0) == *boolean;
    }
    return false;
}

auto ConstExprEquate::equateLiteralPath(const HIRExprNodeLiteral& literal, const HIRConstGenericUnevaluated& pathValue, const HIRExprNodePathValue& path) const -> bool {
    const auto* evaluated = evaluatedPath(pathValue, path);
    return evaluated && equateLiteralEvaluated(literal, *evaluated);
}

auto ConstExprEquate::equateEvaluated(const HIRConstGenericUnevaluated& value, const EncodedLiteral& evaluated) const -> bool {
    const HIRExprNode* node = &**value.expr;
    for (;;) {
        if (const auto* block = cast<const HIRExprNodeBlock>(node)) {
            if (!block->nodes.empty() || !block->valueNode) {
                return false;
            }
            node = &*block->valueNode;
            continue;
        }
        if (const auto* block = cast<const HIRExprNodeConstBlock>(node)) {
            node = &*block->inner;
            continue;
        }
        break;
    }

    const auto* path = cast<const HIRExprNodePathValue>(node);
    if (const auto* pathValue = path ? evaluatedPath(value, *path) : nullptr) {
        return EncodedLiteralSlice(*pathValue) == EncodedLiteralSlice(evaluated);
    }
    if (const auto* literal = cast<const HIRExprNodeLiteral>(node)) {
        return equateLiteralEvaluated(*literal, evaluated);
    }
    return false;
}

auto ConstExprEquate::equatePath(const HIRConstGenericUnevaluated& leftValue, const HIRPath& left, const HIRConstGenericUnevaluated& rightValue, const HIRPath& right) const -> bool {
    MonomorphStatePtr leftMonomorph(context.crate.types, leftValue.selfType, &leftValue.paramsImpl, &leftValue.paramsItem);
    MonomorphStatePtr rightMonomorph(context.crate.types, rightValue.selfType, &rightValue.paramsImpl, &rightValue.paramsItem);
    const auto leftPath = leftMonomorph.monomorphPath(sp, left);
    const auto rightPath = rightMonomorph.monomorphPath(sp, right);
    if (leftPath == rightPath || leftPath.equalsIgnoringRegions(rightPath)) {
        return true;
    }

    auto equateParams = [&](const HIRPathParams& leftParams, const HIRPathParams& rightParams) {
        if (leftParams.types.size() != rightParams.types.size() || leftParams.values.size() != rightParams.values.size()) {
            return false;
        }
        for (unsigned int i = 0; i < leftParams.types.size(); i++) {
            context.equateTypesInner(sp, leftParams.types[i], rightParams.types[i]);
        }
        for (unsigned int i = 0; i < leftParams.values.size(); i++) {
            context.equateValues(sp, leftParams.values[i], rightParams.values[i]);
        }
        return true;
    };

    if (leftPath.data.tag() != rightPath.data.tag()) {
        return false;
    }
    switch (leftPath.data.tag()) {
        case HIRPathData::TAG_Generic: {
            auto& l = leftPath.data.as_Generic();
            auto& r = rightPath.data.as_Generic();
            return l.path == r.path && equateParams(l.params, r.params);
        }
        case HIRPathData::TAG_UfcsInherent: {
            auto& l = leftPath.data.as_UfcsInherent();
            auto& r = rightPath.data.as_UfcsInherent();
            if (l.item != r.item || !equateParams(l.params, r.params)) {
                return false;
            }
            context.equateTypesInner(sp, l.type, r.type);
            return true;
        }
        case HIRPathData::TAG_UfcsKnown: {
            auto& l = leftPath.data.as_UfcsKnown();
            auto& r = rightPath.data.as_UfcsKnown();
            if (l.trait.path != r.trait.path || l.item != r.item || !equateParams(l.trait.params, r.trait.params) || !equateParams(l.params, r.params)) {
                return false;
            }
            context.equateTypesInner(sp, l.type, r.type);
            return true;
        }
        case HIRPathData::TAG_UfcsUnknown: {
            auto& l = leftPath.data.as_UfcsUnknown();
            auto& r = rightPath.data.as_UfcsUnknown();
            if (l.item != r.item || !equateParams(l.params, r.params)) {
                return false;
            }
            context.equateTypesInner(sp, l.type, r.type);
            return true;
        }
    }
    UNREACHABLE();
}

auto ConstExprEquate::equateNode(const HIRConstGenericUnevaluated& leftValue, const HIRExprNode& left, const HIRConstGenericUnevaluated& rightValue, const HIRExprNode& right) const -> bool {
    if (const auto* block = cast<const HIRExprNodeBlock>(&left)) {
        if (block->nodes.empty() && block->valueNode) {
            return equateNode(leftValue, *block->valueNode, rightValue, right);
        }
    }
    if (const auto* block = cast<const HIRExprNodeBlock>(&right)) {
        if (block->nodes.empty() && block->valueNode) {
            return equateNode(leftValue, left, rightValue, *block->valueNode);
        }
    }
    if (const auto* block = cast<const HIRExprNodeConstBlock>(&left)) {
        return equateNode(leftValue, *block->inner, rightValue, right);
    }
    if (const auto* block = cast<const HIRExprNodeConstBlock>(&right)) {
        return equateNode(leftValue, left, rightValue, *block->inner);
    }
    if (const auto* param = cast<const HIRExprNodeConstParam>(&left)) {
        if (const auto* value = getParam(leftValue, param->binding)) {
            if (const auto* unevaluated = value->opt_Unevaluated()) {
                return equateNode(**unevaluated, **(**unevaluated).expr, rightValue, right);
            }
        }
    }
    if (const auto* param = cast<const HIRExprNodeConstParam>(&right)) {
        if (const auto* value = getParam(rightValue, param->binding)) {
            if (const auto* unevaluated = value->opt_Unevaluated()) {
                return equateNode(leftValue, left, **unevaluated, **(**unevaluated).expr);
            }
        }
    }
    if (const auto* l = cast<const HIRExprNodeConstParam>(&left)) {
        const auto* r = cast<const HIRExprNodeConstParam>(&right);
        if (!r) {
            return false;
        }
        const auto* lParam = getParam(leftValue, l->binding);
        const auto* rParam = getParam(rightValue, r->binding);
        if (!lParam || !rParam) {
            return l->binding == r->binding;
        }
        context.equateValues(sp, *lParam, *rParam);
        return true;
    }
    if (const auto* l = cast<const HIRExprNodeLiteral>(&left)) {
        if (const auto* r = cast<const HIRExprNodeLiteral>(&right)) {
            return equateLiteral(*l, *r);
        }
        if (const auto* r = cast<const HIRExprNodePathValue>(&right)) {
            return equateLiteralPath(*l, rightValue, *r);
        }
        return false;
    }
    if (const auto* l = cast<const HIRExprNodePathValue>(&left)) {
        if (const auto* r = cast<const HIRExprNodePathValue>(&right)) {
            return l->target == r->target && equatePath(leftValue, l->path, rightValue, r->path);
        }
        if (const auto* r = cast<const HIRExprNodeLiteral>(&right)) {
            return equateLiteralPath(*r, leftValue, *l);
        }
        return false;
    }
    if (const auto* l = cast<const HIRExprNodeBinOp>(&left)) {
        const auto* r = cast<const HIRExprNodeBinOp>(&right);
        return r && l->op == r->op && equateNode(leftValue, *l->left, rightValue, *r->left) && equateNode(leftValue, *l->right, rightValue, *r->right);
    }
    if (const auto* l = cast<const HIRExprNodeUniOp>(&left)) {
        const auto* r = cast<const HIRExprNodeUniOp>(&right);
        return r && l->op == r->op && equateNode(leftValue, *l->value, rightValue, *r->value);
    }
    if (const auto* l = cast<const HIRExprNodeCast>(&left)) {
        const auto* r = cast<const HIRExprNodeCast>(&right);
        if (!r) {
            return false;
        }
        context.equateTypesInner(sp, l->dstType, r->dstType);
        return equateNode(leftValue, *l->value, rightValue, *r->value);
    }
    if (const auto* l = cast<const HIRExprNodeConstBlock>(&left)) {
        const auto* r = cast<const HIRExprNodeConstBlock>(&right);
        return r && equateNode(leftValue, *l->inner, rightValue, *r->inner);
    }
    if (const auto* l = cast<const HIRExprNodeCallPath>(&left)) {
        const auto* r = cast<const HIRExprNodeCallPath>(&right);
        if (!r || !equatePath(leftValue, l->path, rightValue, r->path) || l->args.size() != r->args.size()) {
            return false;
        }
        for (unsigned int i = 0; i < l->args.size(); i++) {
            if (!equateNode(leftValue, *l->args[i], rightValue, *r->args[i])) {
                return false;
            }
        }
        return true;
    }
    if (const auto* l = cast<const HIRExprNodeBlock>(&left)) {
        const auto* r = cast<const HIRExprNodeBlock>(&right);
        if (!r || l->nodes.size() != r->nodes.size() || static_cast<bool>(l->valueNode) != static_cast<bool>(r->valueNode)) {
            return false;
        }
        for (unsigned int i = 0; i < l->nodes.size(); i++) {
            if (!equateNode(leftValue, *l->nodes[i], rightValue, *r->nodes[i])) {
                return false;
            }
        }
        return !l->valueNode || equateNode(leftValue, *l->valueNode, rightValue, *r->valueNode);
    }
    return false;
}

auto ConstExprEquate::equate(const HIRConstGenericUnevaluated& left, const HIRConstGenericUnevaluated& right) const -> bool {
    return equateNode(left, **left.expr, right, **right.expr);
}

auto AssociatedStallCollector::addType(const HIRType* type) -> void {
    if (type->hasTypeInfer()) {
        pending.pushBack(type);
    }
}

auto AssociatedStallCollector::collect() -> void {
    while (!pending.empty() && !hasRawInfer) {
        const auto type = pending.back();
        pending.popBack();
        if (std::find(visited.begin(), visited.end(), type) != visited.end()) {
            continue;
        }
        visited.pushBack(type);

        visitTyWith(type, [&](const HIRType* inner) {
            const auto* infer = inner->opt_Infer();
            if (!infer) {
                return false;
            }
            if (infer->index == ~0u) {
                hasRawInfer = true;
                return true;
            }

            const auto resolved = context.ivars.getType(infer->index);
            if (const auto* resolvedInfer = resolved->opt_Infer()) {
                if (resolvedInfer->index == ~0u) {
                    hasRawInfer = true;
                    return true;
                }
                const auto existing = std::find_if(dependencies.begin(), dependencies.end(), [&](const auto& dependency) {
                    return dependency.index == resolvedInfer->index;
                });
                if (existing == dependencies.end()) {
                    dependencies.pushBack({resolvedInfer->index, resolved});
                }
            } else if (resolved->hasTypeInfer()) {
                pending.pushBack(resolved);
            }
            return false;
        });
    }
}

auto IvarCoercionIndex::collectIvars(const HIRType* root, Vector<unsigned int>& out) const -> void {
    Vector<const HIRType*> pending;
    pending.pushBack(root);
    Vector<const HIRType*> visited;
    while (!pending.empty()) {
        const auto type = pending.back();
        pending.popBack();
        if (std::find(visited.begin(), visited.end(), type) != visited.end()) {
            continue;
        }
        visited.pushBack(type);
        visitTyWith(type, [&](const HIRType* inner) {
            if (const auto* infer = inner->opt_Infer()) {
                out.pushBack(infer->index);
                const auto& resolved = context.getType(inner);
                if (resolved != inner) {
                    pending.pushBack(resolved);
                }
            }
            return false;
        });
    }
}

auto IvarCoercionIndex::deduplicate(Vector<unsigned int>& values) -> void {
    if (values.empty()) {
        return;
    }
    std::sort(values.mutBegin(), values.mutEnd());
    size_t write = 1;
    for (size_t read = 1; read < values.length(); ++read) {
        if (values[read] != values[write - 1]) {
            values.mut(write++) = values[read];
        }
    }
    while (values.length() > write) {
        values.popBack();
    }
}

template <typename T>
auto IvarCoercionIndex::addRefs(const Vector<unsigned int>& dependencies, Vector<T> IvarCoercionRefs::* member, T value) -> void {
    for (const auto index : dependencies) {
        if (index < refs.size()) {
            (refs[index].*member).pushBack(value);
        }
    }
}

auto IvarCoercionIndex::addEndpoint(const Context::Coercion& obligation, const SolverDeferredCoercion& rawDeferred, unsigned alternativeGroup) -> void {
    const auto* destination = context.getType(rawDeferred.destination);
    const auto* source = context.getType(rawDeferred.source);
    if (const auto* infer = destination->opt_Infer(); infer && infer->index != ~0u && !infer->isLit() && infer->index < refs.size()) {
        refs[infer->index].endpoints.pushBack(IvarCoercionEndpoint{
            source,
            SolverCoercionConstraint::Direction::InputIsDestination,
            rawDeferred.op,
            &obligation,
            alternativeGroup,
        });
    }
    if (const auto* infer = source->opt_Infer(); infer && infer->index != ~0u && !infer->isLit() && infer->index < refs.size()) {
        refs[infer->index].endpoints.pushBack(IvarCoercionEndpoint{
            destination,
            SolverCoercionConstraint::Direction::InputIsSource,
            rawDeferred.op,
            &obligation,
            alternativeGroup,
        });
    }
}

IvarCoercionIndex::IvarCoercionIndex(const Context& context)
    : context(context)
    , refs(context.ivars.ivars.size())
{
    Vector<unsigned int> dependencies;
    unsigned nextAlternativeGroup = 1;
    for (const auto& bound : context.linkCoerce) {
        dependencies.clear();
        collectIvars(bound->leftTy, dependencies);
        collectIvars(bound->sourceType(), dependencies);
        deduplicate(dependencies);
        addRefs(dependencies, &IvarCoercionRefs::coercions, static_cast<const Context::Coercion*>(bound.get()));

        const auto response = context.resolve.evaluateCoercionGoal(bound->span(), context.getType(bound->leftTy), context.getType(bound->sourceType()), bound->op);
        ThinVector<std::pair<unsigned, unsigned>> groups;
        for (const auto& deferred : response.deferred) {
            unsigned group = 0;
            if (deferred.alternativeGroup != 0) {
                const auto found = std::find_if(groups.begin(), groups.end(), [&](const auto& existing) {
                    return existing.first == deferred.alternativeGroup;
                });
                if (found == groups.end()) {
                    group = nextAlternativeGroup++;
                    groups.push_back({deferred.alternativeGroup, group});
                } else {
                    group = found->second;
                }
            }
            addEndpoint(*bound, deferred, group);
        }
    }

    for (const auto& rule : context.linkAssoc) {
        dependencies.clear();
        for (const auto& dependency : rule.stalledOn) {
            dependencies.pushBack(dependency.index);
        }
        deduplicate(dependencies);
        addRefs(dependencies, &IvarCoercionRefs::associated, &rule);
    }

    for (const auto* node : context.toVisit) {
        dependencies.clear();
        collectIvars(node->resType, dependencies);
        deduplicate(dependencies);
        addRefs(dependencies, &IvarCoercionRefs::revisits, node);
    }

    for (const auto& revisit : context.advRevisits) {
        dependencies.clear();
        revisit->collectInferenceDependencies(context, dependencies);
        deduplicate(dependencies);
        addRefs(dependencies, &IvarCoercionRefs::advancedRevisits, static_cast<const Context::Revisitor*>(revisit.get()));
    }
}

auto IvarCoercionIndex::operator[](unsigned int index) const -> const IvarCoercionRefs& {
    return refs.at(index);
}

auto PossibleType::concrete(decltype(cls) cls, const HIRType* ty) -> PossibleType {
    return PossibleType{cls, State::Concrete, ty};
}

auto PossibleType::isActive() const -> bool {
    return state != State::Removed;
}

auto PossibleType::hasType() const -> bool {
    return state == State::Concrete;
}

auto PossibleType::remove() -> void {
    state = State::Removed;
    ty = nullptr;
}

auto PossibleType::operator==(const PossibleType& o) const -> bool {
    return state == o.state && (!hasType() || ty == o.ty) && cls == o.cls;
}

auto PossibleType::fmt(ZeroCopyOutput& os) const -> ZeroCopyOutput& {
    switch (cls) {
        case CoerceTo:
            os << StringView("C-");
            break;
        case CoerceFrom:
            os << StringView("CD");
            break;
        case UnsizeTo:
            os << StringView("--");
            break;
        case UnsizeFrom:
            os << StringView("-D");
            break;
    }
    os << StringView(" ");
    if (hasType()) {
        os << ty;
    } else {
        os << StringView("<removed>");
    }
    return os;
}

auto PossibleType::isSource() const -> bool {
    return cls == CoerceFrom || cls == UnsizeFrom;
}

auto PossibleType::isCoerce() const -> bool {
    return cls == CoerceTo || cls == CoerceFrom;
}

namespace {
auto pointerCoercionShape(const HIRType* type) -> std::optional<PointerCoercionShape> {
    if (const auto* borrow = type->opt_Borrow()) {
        switch (borrow->type) {
            case HIRBorrowType::Unique:
                return PointerCoercionShape{PointerCoercionForm::MutableBorrow, borrow->inner};
            case HIRBorrowType::Shared:
                return PointerCoercionShape{PointerCoercionForm::SharedBorrow, borrow->inner};
            case HIRBorrowType::Owned:
                return std::nullopt;
        }
    }
    if (const auto* pointer = type->opt_Pointer()) {
        switch (pointer->type) {
            case HIRBorrowType::Unique:
                return PointerCoercionShape{PointerCoercionForm::MutableRaw, pointer->inner};
            case HIRBorrowType::Shared:
                return PointerCoercionShape{PointerCoercionForm::ConstRaw, pointer->inner};
            case HIRBorrowType::Owned:
                return std::nullopt;
        }
    }
    return std::nullopt;
}

auto pointerChainLub(PointerCoercionForm left, PointerCoercionForm right) -> std::optional<PointerCoercionForm> {
    const auto canCoerceTo = [](PointerCoercionForm source, PointerCoercionForm destination) {
        if (source == destination) {
            return true;
        }
        switch (source) {
            case PointerCoercionForm::MutableBorrow:
                return destination == PointerCoercionForm::SharedBorrow
                    || destination == PointerCoercionForm::MutableRaw
                    || destination == PointerCoercionForm::ConstRaw;
            case PointerCoercionForm::SharedBorrow:
                return destination == PointerCoercionForm::ConstRaw;
            case PointerCoercionForm::MutableRaw:
                return destination == PointerCoercionForm::ConstRaw;
            case PointerCoercionForm::ConstRaw:
                return false;
        }
        UNREACHABLE();
    };
    if (canCoerceTo(left, right)) {
        return right;
    }
    if (canCoerceTo(right, left)) {
        return left;
    }
    return std::nullopt;
}

}

ExprVisitorTagStaleIvars::ExprVisitorTagStaleIvars(HIRTypeInterner& types)
    : HIRExprVisitorDef(types)
    , mapper_(types)
{
}

[[nodiscard]] auto ExprVisitorTagStaleIvars::visitType(const HIRType* type) -> const HIRType* {
    return mapper_.monomorphType(Span(), type, true);
}

auto ExprVisitorTagStaleIvars::visitPathParams(HIRPathParams& params) -> void {
    params = mapper_.monomorphPathParams(Span(), params, true);
}

auto ExprVisitorTagStaleIvars::Mapper::taggedIndex(Vector<std::pair<unsigned, unsigned>>& indexes, unsigned original) const -> unsigned {
    for (const auto& entry : indexes) {
        if (entry.first == original) {
            return entry.second;
        }
    }
    const auto tagged = types.newAliasInputInfer();
    indexes.pushBack({original, tagged});
    return tagged;
}

auto ExprVisitorTagStaleIvars::Mapper::monomorphConstgeneric(const Span& sp, const HIRConstGeneric& value, bool allowInfer) const -> HIRConstGeneric {
    if (const auto* infer = value.opt_Infer(); infer && infer->index != ~0u && !isAliasInputInfer(infer->index)) {
        return HIRConstGeneric::make_Infer({taggedIndex(valueIndexes_, infer->index)});
    }
    return MonomorphiserNop::monomorphConstgeneric(sp, value, allowInfer);
}

ExprVisitorAddIvars::ExprVisitorAddIvars(Context& context)
    : HIRExprVisitorDef(context.crate.types)
    , context(context)
{
}

auto ExprVisitorAddIvars::innerVisitType(const HIRType* ty) -> const HIRType* {
    return rewriteTyWith(context.crate.types, ty, [this](const HIRType*, HIRType& data) -> const HIRType* {
        if (auto* te = data.opt_Path()) {
            if (te->path.data.is_Generic()) {
                auto& params = te->path.data.as_Generic().params;
                const HIRGenericParams* paramDefs = nullptr;
                switch (te->binding.tag()) {
                    case HIRTypePathBinding::TAG_Struct: {
                        auto& pbe = te->binding.as_Struct();
                        paramDefs = &pbe->params;
                        break;
                    }
                    case HIRTypePathBinding::TAG_Enum: {
                        auto& pbe = te->binding.as_Enum();
                        paramDefs = &pbe->params;
                        break;
                    }
                    case HIRTypePathBinding::TAG_Union: {
                        auto& pbe = te->binding.as_Union();
                        paramDefs = &pbe->params;
                        break;
                    }
                    case HIRTypePathBinding::TAG_ExternType: {
                        break;
                    }
                    case HIRTypePathBinding::TAG_Opaque: {
                        break;
                    }
                    case HIRTypePathBinding::TAG_Unbound: {
                        break;
                    }
                }
                if (paramDefs) {
                    populateDefaults(Span(), context, MonomorphStatePtr(context.crate.types, nullptr, &params, nullptr), *paramDefs, params);
                }
            }
        }
        return nullptr;
    });
}

auto ExprVisitorAddIvars::visitPathParams(HIRPathParams& pp) -> void {
    this->context.ivars.addIvarsParams(pp);
    for (auto& ty : pp.types) {
        ty = innerVisitType(ty);
    }
}

[[nodiscard]] auto ExprVisitorAddIvars::visitType(const HIRType* ty) -> const HIRType* {
    ty = this->context.addIvars(ty);
    ty = innerVisitType(ty);
    visitTyWith(ty, [&](const HIRType* inner) {
        if (const auto* path = inner->opt_Path()) {
            if (const auto* projection = path->path.data.opt_UfcsKnown()) {
                context.addTraitBound(Span(), projection->type, projection->trait.path, projection->trait.params.clone());
                const auto& trait = context.crate.getTraitByPath(Span(), projection->trait.path);
                auto monomorph = MonomorphStatePtr(context.crate.types, projection->type, &projection->trait.params, nullptr);
                applyBoundsAsRules(context, Span(), trait.params, monomorph, true);
            }
        }
        return false;
    });
    return ty;
}

auto ExprVisitorAddIvars::visit(HIRExprNodeLet& node) -> void {
    node.type = LocalImplTraitLowering(context).monomorphType(node.span(), node.type);
    HIRExprVisitorDef::visit(node);
}

ExprVisitorAddIvars::LocalImplTraitLowering::LocalImplTraitLowering(Context& context)
    : Monomorphiser(context.crate.types)
    , context(context)
{
}

auto ExprVisitorAddIvars::LocalImplTraitLowering::getType(const Span& sp, const HIRGenericRef& generic) const -> const HIRType* {
    if (generic.binding == GENERICErasedSelf && curSelf) {
        return curSelf;
    }
    return types.generic(generic.name, generic.binding);
}

auto ExprVisitorAddIvars::LocalImplTraitLowering::getValue(const Span& sp, const HIRGenericRef& generic) const -> HIRConstGeneric {
    return generic;
}

auto ExprVisitorAddIvars::LocalImplTraitLowering::monomorphType(const Span& sp, const HIRType* type, bool allowInfer) const -> const HIRType* {
    if (const auto* erased = type->opt_ErasedType()) {
        if (const auto* fcn = erased->inner.opt_Fcn(); fcn && fcn->origin == HIRSimplePath()) {
            auto result = context.ivars.newIvarTr();
            const auto* savedSelf = curSelf;
            curSelf = result;

            for (const auto& trait : erased->traits) {
                auto bound = this->monomorphTraitpath(sp, trait, allowInfer);
                if (bound.typeBounds.empty()) {
                    context.addTraitBound(sp, result, bound.path.path, mv$(bound.path.params));
                } else {
                    for (auto& associated : bound.typeBounds) {
                        context.equateTypesAssoc(sp, associated.second.type, bound.path.path, bound.path.params.clone(), result, associated.first.c_str(), associated.second.atyParams, false);
                    }
                }
            }
            if (erased->isSized) {
                context.requireSized(sp, result);
            }

            curSelf = savedSelf;
            return result;
        }
    }
    return Monomorphiser::monomorphType(sp, type, allowInfer);
}

ExprVisitorEnum::ExprVisitorEnum(Context& context, tTraitList baseTraits, const HIRType* retType)
    : context(context)
    , retType(retType)
    , traits(mv$(baseTraits))
{
}

auto ExprVisitorEnum::visit(HIRExprNodeBlock& node) -> void {
    TRACE_FUNCTION_FR(static_cast<const void*>(&node) << StringView(" { ... }"), static_cast<const void*>(&node) << StringView(" ") << this->context.getType(node.resType));
    this->context.resolve.addOpaqueAliasScope(node.localMod);

    bool diverges = false;
    node.diverges = false;
    this->pushTraits(node.traits);
    if (node.nodes.size() > 0) {
        this->pushInnerCoerce(false);
        for (unsigned int i = 0; i < node.nodes.size(); i++) {
            auto& snp = node.nodes[i];
            snp->resType = this->context.addIvars(snp->resType);
            snp->visit(*this);

            /* An expression statement's value is dropped and the statement puts no
               expectation on it.  Defaulting its type to `()` would be read back as an
               expectation - a method call in statement position would have every
               candidate whose return is not `()` rejected - so the statement's type is
               left to the expression's own constraints.  The block's own `()` result is
               established below, independently of the statements. */
            if (this->nodeDiverges(*snp)) {
                diverges = true;
            }
        }
        this->popInnerCoerce();
    }

    if (node.valueNode) {
        auto& snp = node.valueNode;
        DEBUG(StringView("Block yields final value"));
        snp->resType = this->context.addIvars(snp->resType);
        this->context.equateTypes(snp->span(), node.resType, snp->resType);
        this->context.requireSized(snp->span(), snp->resType);
        snp->visit(*this);
        node.diverges = diverges || this->nodeDiverges(*snp);
    } else if (node.nodes.size() > 0) {
        const auto& snp = node.nodes.back();
        bool defer = false;
        if (!diverges) {
            if (const auto* e = this->context.getType(snp->resType)->opt_Infer()) {
                switch (e->tyClass) {
                    case HIRInferClass::Integer:
                    case HIRInferClass::Float:
                        diverges = false;
                        break;
                    default:
                        defer = true;
                        break;
                }
            } else if (this->nodeDiverges(*snp)) {
                diverges = true;
            } else {
                diverges = false;
            }
        }

        if (defer) {
            DEBUG(StringView("Block final node returns _, derfer diverge check"));
            this->context.addRevisit(node);
        } else if (diverges) {
            DEBUG(StringView("Block diverges, yield !"));
            const auto* blockInfer = this->context.crate.edition < ASTEdition::Rust2024 ? this->context.getType(node.resType)->opt_Infer() : nullptr;
            if (const auto* i = blockInfer) {
                this->context.addCoercionObligation(node.span(), this->context.ivars.getType(i->index), this->context.crate.types.diverge(), SolverCoercionOp::Coercion);
                this->context.addRevisitAdv(std::make_unique<RevisitDefaultUnit>(&node));
            } else {
                this->context.equateTypes(node.span(), node.resType, this->context.crate.types.diverge());
            }
        } else {
            DEBUG(StringView("Block doesn't diverge but doesn't yield a value, yield ()"));
            this->context.equateTypes(node.span(), node.resType, this->context.crate.types.unit());
        }
        node.diverges = diverges;
    } else {
        DEBUG(StringView("Block is empty, yield ()"));
        this->context.equateTypes(node.span(), node.resType, this->context.crate.types.unit());
    }
    this->popTraits(node.traits);
}

auto ExprVisitorEnum::visit(HIRExprNodeConstBlock& node) -> void {
    TRACE_FUNCTION_F(static_cast<const void*>(&node) << StringView(" const { ... }"));
    node.inner->resType = this->context.addIvars(node.inner->resType);

    node.inner->visit(*this);
    node.diverges = this->nodeDiverges(*node.inner);
    this->context.equateTypes(node.span(), node.resType, node.inner->resType);
}

auto ExprVisitorEnum::visit(HIRExprNodeAsm& node) -> void {
    TRACE_FUNCTION_F(static_cast<const void*>(&node) << StringView(" llvm_asm! ..."));
    this->pushInnerCoerce(false);
    for (auto& v : node.outputs) {
        v.value->resType = this->context.addIvars(v.value->resType);
        v.value->visit(*this);
        this->inheritDivergence(node, *v.value);
    }
    for (auto& v : node.inputs) {
        v.value->resType = this->context.addIvars(v.value->resType);
        v.value->visit(*this);
        this->inheritDivergence(node, *v.value);
    }
    this->popInnerCoerce();
    // TODO: Revisit to check that the input are integers, and the outputs are integer lvalues
    this->context.equateTypes(node.span(), node.resType, this->context.crate.types.unit());
}

auto ExprVisitorEnum::visit(HIRExprNodeAsm2& node) -> void {
    TRACE_FUNCTION_F(static_cast<const void*>(&node) << StringView(" asm! ..."));
    bool hasLabel = false;
    this->pushInnerCoerce(false);
    for (auto& v : node.params) {
        switch (v.tag()) {
            case HIRAsmParam::TAG_Const: {
                auto& e = v.as_Const();
                e->resType = this->context.addIvars(e->resType);
                visitNodePtr(e);
                this->inheritDivergence(node, *e);
                break;
            }
            case HIRAsmParam::TAG_Sym: {
                break;
            }
            case HIRAsmParam::TAG_Label: {
                auto& e = v.as_Label();
                hasLabel = true;
                e.code->resType = this->context.addIvars(e.code->resType);
                visitNodePtr(e.code);
                this->context.equateTypes(e.code->span(), e.code->resType, this->context.crate.types.unit());
                break;
            }
            case HIRAsmParam::TAG_RegSingle: {
                auto& e = v.as_RegSingle();
                e.val->resType = this->context.addIvars(e.val->resType);
                visitNodePtr(e.val);
                this->inheritDivergence(node, *e.val);
                break;
            }
            case HIRAsmParam::TAG_Reg: {
                auto& e = v.as_Reg();
                if (e.valIn) {
                    e.valIn->resType = this->context.addIvars(e.valIn->resType);
                    visitNodePtr(e.valIn);
                    this->inheritDivergence(node, *e.valIn);
                }
                if (e.valOut) {
                    e.valOut->resType = this->context.addIvars(e.valOut->resType);
                    visitNodePtr(e.valOut);
                    this->inheritDivergence(node, *e.valOut);
                }
                break;
            }
        }
    }
    this->popInnerCoerce();
    // TODO: Revisit to check that the input are integers, and the outputs are integer lvalues
    if (node.options.noreturn && !hasLabel) {
        node.diverges = true;
        this->context.equateTypes(node.span(), node.resType, this->context.crate.types.diverge());
    } else {
        this->context.equateTypes(node.span(), node.resType, this->context.crate.types.unit());
    }
}

auto ExprVisitorEnum::visit(HIRExprNodeReturn& node) -> void {
    if (node.isTailCall && !cast<HIRExprNodeCallPath>(node.value.get()) && !cast<HIRExprNodeCallValue>(node.value.get())) {
        ERROR(node.span(), E0000, StringView("`become` requires a direct function call"));
    }
    node.value->resType = this->context.addIvars(node.value->resType);

    const auto* retTy = (!this->closureRetTypes.empty() ? this->closureRetTypes.back().retType : this->retType);
    this->context.equateTypesCoerce(node.span(), retTy, node.value);

    this->pushInnerCoerce(true);
    node.value->visit(*this);
    this->popInnerCoerce();
    node.diverges = true;
    this->context.equateTypes(node.span(), node.resType, this->context.crate.types.diverge());
}

auto ExprVisitorEnum::visit(HIRExprNodeYield& node) -> void {
    TRACE_FUNCTION_F(static_cast<const void*>(&node) << StringView(" yield ..."));
    node.value->resType = this->context.addIvars(node.value->resType);

    if (this->closureRetTypes.empty() || this->closureRetTypes.back().yieldType == nullptr) {
        ERROR(node.span(), E0000, StringView("`yield` outside a generator closure"));
    }
    const auto* retTy = this->closureRetTypes.back().yieldType;
    const auto* resumeTy = this->closureRetTypes.back().resumeType;
    this->context.equateTypesCoerce(node.span(), retTy, node.value);

    this->pushInnerCoerce(true);
    node.value->visit(*this);
    this->popInnerCoerce();
    this->inheritDivergence(node, *node.value);
    this->context.equateTypes(node.span(), node.resType, resumeTy);
}

auto ExprVisitorEnum::visit(HIRExprNodeAWait& node) -> void {
    TRACE_FUNCTION_F(static_cast<const void*>(&node) << StringView("(...).await"));
    node.value->resType = this->context.addIvars(node.value->resType);
    node.value->visit(*this);
    this->inheritDivergence(node, *node.value);
    if (node.isNext) {
        auto itemTy = this->context.ivars.newIvarTr();
        this->context.equateTypesAssoc(node.span(), itemTy, context.resolve.langAsyncIterator(), {}, node.value->resType, "Item", {});
        const auto& langOption = context.crate.getLangItemPath(node.span(), "Option");
        this->context.equateTypes(node.span(), node.resType, context.crate.types.path(HIRGenericPath(langOption, HIRPathParams(itemTy)), &context.crate.getEnumByPath(node.span(), langOption)));
        return;
    }
    this->context.equateTypesAssoc(node.span(), node.resType, context.resolve.langFuture(), {}, node.value->resType, "Output", {});
}

auto ExprVisitorEnum::visit(HIRExprNodeUse& node) -> void {
    node.value->resType = this->context.addIvars(node.value->resType);
    node.value->visit(*this);
    this->inheritDivergence(node, *node.value);
    this->context.equateTypes(node.span(), node.resType, node.value->resType);
}

auto ExprVisitorEnum::visit(HIRExprNodeLoop& node) -> void {
    auto _ = this->pushInnerCoerceScoped(false);
    this->loopBlocks.pushBack(&node);
    node.diverges = true;

    node.code->resType = this->context.addIvars(node.code->resType);
    this->context.equateTypes(node.span(), node.code->resType, this->context.crate.types.unit());
    node.code->visit(*this);

    this->loopBlocks.popBack();

    if (node.diverges) {
        this->context.equateTypes(node.span(), node.resType, this->context.crate.types.diverge());
        DEBUG(StringView("Loop diverged"));
    }
}

auto ExprVisitorEnum::visit(HIRExprNodeLoopControl& node) -> void {
    if (!node.isContinue) {
        HIRExprNodeLoop* loopNodePtr;
        if (node.label != "") {
            loopNodePtr = nullptr;
            for (size_t i = this->loopBlocks.length(); i-- > 0;) {
                if (this->loopBlocks[i]->label == node.label) {
                    loopNodePtr = this->loopBlocks[i];
                    break;
                }
            }
            if (!loopNodePtr) {
                ERROR(node.span(), E0000, StringView("Could not find loop '") << node.label << StringView(" for break"));
            }
        } else {
            loopNodePtr = nullptr;
            for (size_t i = this->loopBlocks.length(); i-- > 0;) {
                if (!this->loopBlocks[i]->requireLabel) {
                    loopNodePtr = this->loopBlocks[i];
                    break;
                }
            }
            if (!loopNodePtr) {
                ERROR(node.span(), E0000, StringView("Break statement with no active loop"));
            }
        }
        node.targetNode = loopNodePtr;

        DEBUG(StringView("Break out of loop ") << static_cast<const void*>(loopNodePtr));
        auto& loopNode = *loopNodePtr;
        loopNode.diverges = false;

        if (node.value) {
            node.value->resType = this->context.addIvars(node.value->resType);
            this->pushInnerCoerce(true);
            node.value->visit(*this);
            this->popInnerCoerce();
            this->context.equateTypesCoerce(node.span(), loopNode.resType, node.value);
            this->context.requireSized(node.span(), node.value->resType);
        } else {
            this->context.equateTypes(node.span(), loopNode.resType, this->context.crate.types.unit());
        }
    }
    node.diverges = true;
    this->context.equateTypes(node.span(), node.resType, this->context.crate.types.diverge());
}

auto ExprVisitorEnum::visit(HIRExprNodeLet& node) -> void {
    TRACE_FUNCTION_F(static_cast<const void*>(&node) << StringView(" let ") << node.pattern << StringView(": ") << node.type);
    node.type = this->context.addIvars(node.type);
    this->context.handlePattern(node.span(), node.pattern, node.type, true);

    bool deferResultType = false;
    bool diverges = false;
    if (node.value) {
        node.value->resType = this->context.addIvars(node.value->resType);
        if (node.type->is_Infer()) {
            this->context.equateTypes(node.span(), node.type, node.value->resType);
            this->pushInnerCoerce(true);
        } else {
            this->context.equateTypesCoerce(node.span(), node.type, node.value);
            this->pushInnerCoerce(true);
        }

        node.value->visit(*this);
        this->popInnerCoerce();

        const auto* valueType = this->context.getType(node.value->resType);
        const auto* infer = valueType->opt_Infer();
        if (!node.value->diverges && infer && infer->tyClass == HIRInferClass::None) {
            deferResultType = true;
        } else {
            diverges = this->nodeDiverges(*node.value);
        }
    }
    node.diverges = diverges;
    if (deferResultType) {
        this->context.addRevisit(node);
    } else {
        this->context.equateTypes(node.span(), node.resType, diverges ? this->context.crate.types.diverge() : this->context.crate.types.unit());
    }
}

auto ExprVisitorEnum::visit(HIRExprNodeMatch& node) -> void {
    TRACE_FUNCTION_F(static_cast<const void*>(&node) << StringView(" match ..."));
    auto valType = this->context.ivars.newIvarTr();
    const auto* armResultType = node.resType;

    {
        auto _ = this->pushInnerCoerceScoped(true);
        node.value->resType = this->context.addIvars(node.value->resType);

        node.value->visit(*this);
        this->inheritDivergence(node, *node.value);
        // TODO: If a coercion point (and ivar for the value) is placed here, it will allow `match &string { "..." ... }`

        this->context.equateTypes(node.span(), valType, node.value->resType);
    }

    for (auto& arm : node.arms) {
        TRACE_FUNCTION_F(StringView("ARM ") << arm.patterns);
        const bool unconditionallySelected = &arm == &node.arms.front() && std::any_of(arm.patterns.begin(), arm.patterns.end(), [](const HIRPattern& pattern) {
            return pattern.data.is_Any();
        });
        for (auto& pat : arm.patterns) {
            this->context.handlePattern(node.span(), pat, valType);
        }

        for (auto& c : arm.guards) {
            auto _ = this->pushInnerCoerceScoped(false);
            c.val->resType = this->context.addIvars(c.val->resType);

            if (c.isIf) {
                this->context.equateTypesCoerce(c.val->span(), this->context.crate.types.primitive(HIRCoreType::Bool), c.val);
                c.val->visit(*this);
            } else {
                c.val->visit(*this);
                this->context.handlePattern(node.span(), c.pat, c.val->resType);
            }
            if (unconditionallySelected && &c == &arm.guards.front()) {
                this->inheritDivergence(node, *c.val);
            }
        }

        arm.code->resType = this->context.addIvars(arm.code->resType);

        this->context.equateTypesCoerce(node.span(), armResultType, arm.code);
        arm.code->visit(*this);
    }

    if (node.arms.empty()) {
        DEBUG(StringView("Empty match"));
        this->context.equateTypes(node.span(), node.resType, this->context.crate.types.diverge());
    } else if (std::all_of(node.arms.begin(), node.arms.end(), [&](const HIRExprNodeMatch::Arm& arm) {
        return this->nodeDiverges(*arm.code);
    })) {
        DEBUG(StringView("Every arm diverges"));
        node.diverges = true;
    }
}

auto ExprVisitorEnum::visit(HIRExprNodeAssign& node) -> void {
    auto _ = this->pushInnerCoerceScoped(false);

    TRACE_FUNCTION_F(static_cast<const void*>(&node) << StringView("... = ..."));
    node.slot->resType = this->context.addIvars(node.slot->resType);
    node.value->resType = this->context.addIvars(node.value->resType);

    if (node.op == HIRExprNodeAssign::Op::None) {
        this->context.equateTypesCoerce(node.span(), node.slot->resType, node.value);
    } else {
        const char* langItem = nullptr;
        auto operatorKind = TypeckPrimitiveOperator::None;
        switch (node.op) {
            case HIRExprNodeAssign::Op::None:
                UNREACHABLE();
            case HIRExprNodeAssign::Op::Add:
                langItem = "add_assign";
                operatorKind = TypeckPrimitiveOperator::AddAssign;
                break;
            case HIRExprNodeAssign::Op::Sub:
                langItem = "sub_assign";
                operatorKind = TypeckPrimitiveOperator::SubAssign;
                break;
            case HIRExprNodeAssign::Op::Mul:
                langItem = "mul_assign";
                operatorKind = TypeckPrimitiveOperator::MulAssign;
                break;
            case HIRExprNodeAssign::Op::Div:
                langItem = "div_assign";
                operatorKind = TypeckPrimitiveOperator::DivAssign;
                break;
            case HIRExprNodeAssign::Op::Mod:
                langItem = "rem_assign";
                operatorKind = TypeckPrimitiveOperator::RemAssign;
                break;
            case HIRExprNodeAssign::Op::And:
                langItem = "bitand_assign";
                operatorKind = TypeckPrimitiveOperator::BitAndAssign;
                break;
            case HIRExprNodeAssign::Op::Or:
                langItem = "bitor_assign";
                operatorKind = TypeckPrimitiveOperator::BitOrAssign;
                break;
            case HIRExprNodeAssign::Op::Xor:
                langItem = "bitxor_assign";
                operatorKind = TypeckPrimitiveOperator::BitXorAssign;
                break;
            case HIRExprNodeAssign::Op::Shr:
                langItem = "shr_assign";
                operatorKind = TypeckPrimitiveOperator::ShrAssign;
                break;
            case HIRExprNodeAssign::Op::Shl:
                langItem = "shl_assign";
                operatorKind = TypeckPrimitiveOperator::ShlAssign;
                break;
        }
        BUG_ASSERT(langItem);
        const auto& traitPath = this->context.crate.getLangItemPathOpt(langItem);

        auto ty = this->context.ivars.newIvarTr();
        this->context.equateTypesCoerce(node.span(), ty, node.value);
        if (!traitPath.components().empty()) {
            this->context.equateTypesAssoc(node.span(), this->context.crate.types.infer(), traitPath, HIRPathParams(mv$(ty)), node.slot->resType, "", {}, true, operatorKind);
        } else if (operatorKind != TypeckPrimitiveOperator::ShlAssign && operatorKind != TypeckPrimitiveOperator::ShrAssign) {
            this->context.equateTypes(node.span(), node.slot->resType, ty);
        }
    }

    node.slot->visit(*this);
    this->inheritDivergence(node, *node.slot);

    auto _2 = this->pushInnerCoerceScoped(node.op == HIRExprNodeAssign::Op::None);
    node.value->visit(*this);
    this->inheritDivergence(node, *node.value);
    this->context.requireSized(node.span(), node.value->resType);

    this->context.equateTypes(node.span(), node.resType, this->context.crate.types.unit());
}

auto ExprVisitorEnum::visit(HIRExprNodeBinOp& node) -> void {
    auto _ = this->pushInnerCoerceScoped(false);

    TRACE_FUNCTION_F(static_cast<const void*>(&node) << StringView("... ") << HIRExprNodeBinOp::opname(node.op) << StringView(" ..."));
    node.left->resType = this->context.addIvars(node.left->resType);
    node.right->resType = this->context.addIvars(node.right->resType);

    const auto& leftTy = node.left->resType;
    const HIRType* rightTyInner = this->context.ivars.newIvarTr();
    const auto& rightTy = rightTyInner;
    this->context.equateTypesCoerce(node.span(), rightTyInner, node.right);

    node.left->visit(*this);
    {
        auto _2 = this->pushInnerCoerceScoped(true);
        node.right->visit(*this);
    }

    const bool leftDiverges = this->nodeDiverges(*node.left);
    const bool rightDiverges = this->nodeDiverges(*node.right);
    const bool diverges = leftDiverges || (rightDiverges && node.op != HIRExprNodeBinOp::Op::BoolAnd && node.op != HIRExprNodeBinOp::Op::BoolOr);
    node.diverges = diverges;
    const HIRType* operatorResultType = node.resType;
    if (diverges) {
        operatorResultType = this->context.ivars.newIvarTr();
        this->context.equateTypes(node.span(), node.resType, this->context.crate.types.diverge());
    }

    switch (node.op) {
        case HIRExprNodeBinOp::Op::CmpEqu:
        case HIRExprNodeBinOp::Op::CmpNEqu:
        case HIRExprNodeBinOp::Op::CmpLt:
        case HIRExprNodeBinOp::Op::CmpLtE:
        case HIRExprNodeBinOp::Op::CmpGt:
        case HIRExprNodeBinOp::Op::CmpGtE: {
            this->context.equateTypes(node.span(), operatorResultType, this->context.crate.types.primitive(HIRCoreType::Bool));

            const char* itemName = nullptr;
            switch (node.op) {
                case HIRExprNodeBinOp::Op::CmpEqu:
                    itemName = "eq";
                    break;
                case HIRExprNodeBinOp::Op::CmpNEqu:
                    itemName = "eq";
                    break;
                case HIRExprNodeBinOp::Op::CmpLt:
                    itemName = "partial_ord";
                    break;
                case HIRExprNodeBinOp::Op::CmpLtE:
                    itemName = "partial_ord";
                    break;
                case HIRExprNodeBinOp::Op::CmpGt:
                    itemName = "partial_ord";
                    break;
                case HIRExprNodeBinOp::Op::CmpGtE:
                    itemName = "partial_ord";
                    break;
                default:
                    break;
            }
            BUG_ASSERT(itemName);
            const auto& opTrait = this->context.crate.getLangItemPathOpt(itemName);

            auto operatorKind = node.op == HIRExprNodeBinOp::Op::CmpEqu || node.op == HIRExprNodeBinOp::Op::CmpNEqu ? TypeckPrimitiveOperator::Equal : TypeckPrimitiveOperator::Order;
            if (!opTrait.components().empty()) {
                this->context.equateTypesAssoc(node.span(), this->context.crate.types.infer(), opTrait, HIRPathParams(rightTy), leftTy, "", {}, true, operatorKind);
            } else {
                this->context.equateTypes(node.span(), leftTy, rightTy);
            }
            break;
        }

        case HIRExprNodeBinOp::Op::BoolAnd:
        case HIRExprNodeBinOp::Op::BoolOr:
            this->context.equateTypes(node.span(), operatorResultType, this->context.crate.types.primitive(HIRCoreType::Bool));
            this->context.equateTypes(node.span(), leftTy, this->context.crate.types.primitive(HIRCoreType::Bool));
            this->context.equateTypes(node.span(), rightTy, this->context.crate.types.primitive(HIRCoreType::Bool));
            break;
        default: {
            const char* itemName = nullptr;
            auto operatorKind = TypeckPrimitiveOperator::None;
            switch (node.op) {
                case HIRExprNodeBinOp::Op::CmpEqu:
                    UNREACHABLE();
                case HIRExprNodeBinOp::Op::CmpNEqu:
                    UNREACHABLE();
                case HIRExprNodeBinOp::Op::CmpLt:
                    UNREACHABLE();
                case HIRExprNodeBinOp::Op::CmpLtE:
                    UNREACHABLE();
                case HIRExprNodeBinOp::Op::CmpGt:
                    UNREACHABLE();
                case HIRExprNodeBinOp::Op::CmpGtE:
                    UNREACHABLE();
                case HIRExprNodeBinOp::Op::BoolAnd:
                    UNREACHABLE();
                case HIRExprNodeBinOp::Op::BoolOr:
                    UNREACHABLE();

                case HIRExprNodeBinOp::Op::Add:
                    itemName = "add";
                    operatorKind = TypeckPrimitiveOperator::Add;
                    break;
                case HIRExprNodeBinOp::Op::Sub:
                    itemName = "sub";
                    operatorKind = TypeckPrimitiveOperator::Sub;
                    break;
                case HIRExprNodeBinOp::Op::Mul:
                    itemName = "mul";
                    operatorKind = TypeckPrimitiveOperator::Mul;
                    break;
                case HIRExprNodeBinOp::Op::Div:
                    itemName = "div";
                    operatorKind = TypeckPrimitiveOperator::Div;
                    break;
                case HIRExprNodeBinOp::Op::Mod:
                    itemName = "rem";
                    operatorKind = TypeckPrimitiveOperator::Rem;
                    break;

                case HIRExprNodeBinOp::Op::And:
                    itemName = "bitand";
                    operatorKind = TypeckPrimitiveOperator::BitAnd;
                    break;
                case HIRExprNodeBinOp::Op::Or:
                    itemName = "bitor";
                    operatorKind = TypeckPrimitiveOperator::BitOr;
                    break;
                case HIRExprNodeBinOp::Op::Xor:
                    itemName = "bitxor";
                    operatorKind = TypeckPrimitiveOperator::BitXor;
                    break;

                case HIRExprNodeBinOp::Op::Shr:
                    itemName = "shr";
                    operatorKind = TypeckPrimitiveOperator::Shr;
                    break;
                case HIRExprNodeBinOp::Op::Shl:
                    itemName = "shl";
                    operatorKind = TypeckPrimitiveOperator::Shl;
                    break;
            }
            BUG_ASSERT(itemName);
            const auto& opTrait = this->context.crate.getLangItemPathOpt(itemName);

            if (!opTrait.components().empty()) {
                this->context.equateTypesAssoc(node.span(), operatorResultType, opTrait, HIRPathParams(rightTy), leftTy, "Output", {}, true, operatorKind);
            } else {
                this->context.equateTypes(node.span(), operatorResultType, leftTy);
                if (operatorKind != TypeckPrimitiveOperator::Shl && operatorKind != TypeckPrimitiveOperator::Shr) {
                    this->context.equateTypes(node.span(), leftTy, rightTy);
                }
            }
            break;
        }
    }
}

auto ExprVisitorEnum::visit(HIRExprNodeUniOp& node) -> void {
    auto _ = this->pushInnerCoerceScoped(false);

    TRACE_FUNCTION_F(static_cast<const void*>(&node) << StringView(" ") << HIRExprNodeUniOp::opname(node.op) << StringView("..."));
    node.value->resType = this->context.addIvars(node.value->resType);
    node.value->visit(*this);
    this->inheritDivergence(node, *node.value);

    const char* itemName = nullptr;
    auto operatorKind = TypeckPrimitiveOperator::None;
    switch (node.op) {
        case HIRExprNodeUniOp::Op::Invert:
            itemName = "not";
            operatorKind = TypeckPrimitiveOperator::Not;
            break;
        case HIRExprNodeUniOp::Op::Negate:
            itemName = "neg";
            operatorKind = TypeckPrimitiveOperator::Neg;
            break;
    }
    BUG_ASSERT(itemName);
    const HIRType* inputType = node.value->resType;
    if (this->context.getType(inputType)->is_Diverge()) {
        const HIRType* expectedType = node.resType;
        expectedType = this->context.getType(expectedType);
        if ((!expectedType->is_Infer() || expectedType->as_Infer().isLit()) && !expectedType->is_Diverge()) {
            inputType = expectedType;
        }
    }
    const auto& opTrait = this->context.crate.getLangItemPathOpt(itemName);
    if (!opTrait.components().empty()) {
        this->context.equateTypesAssoc(node.span(), node.resType, opTrait, HIRPathParams{}, inputType, "Output", {}, true, operatorKind);
    } else {
        this->context.equateTypes(node.span(), node.resType, inputType);
    }
}

auto ExprVisitorEnum::visit(HIRExprNodeBorrow& node) -> void {
    TRACE_FUNCTION_F(static_cast<const void*>(&node) << StringView(" &_ ..."));
    node.value->resType = this->context.addIvars(node.value->resType);

    this->context.equateTypes(node.span(), node.resType, this->context.crate.types.borrow(node.type, node.value->resType));

    node.value->visit(*this);
    this->inheritDivergence(node, *node.value);
}

auto ExprVisitorEnum::visit(HIRExprNodeRawBorrow& node) -> void {
    TRACE_FUNCTION_F(static_cast<const void*>(&node) << StringView(" &raw _ ..."));
    node.value->resType = this->context.addIvars(node.value->resType);

    this->context.equateTypes(node.span(), node.resType, this->context.crate.types.pointer(node.type, node.value->resType));

    node.value->visit(*this);
    this->inheritDivergence(node, *node.value);
}

auto ExprVisitorEnum::visit(HIRExprNodeCast& node) -> void {
    auto _ = this->pushInnerCoerceScoped(false);
    node.dstType = this->context.addIvars(node.dstType);

    TRACE_FUNCTION_F(static_cast<const void*>(&node) << StringView(" ... as ") << node.dstType);
    node.value->visit(*this);
    this->inheritDivergence(node, *node.value);

    this->context.equateTypes(node.span(), node.resType, node.dstType);
    // TODO: Only revisit if the cast type requires inferring.
    this->context.addRevisit(node);
}

auto ExprVisitorEnum::visit(HIRExprNodeUnsize& node) -> void {
    node.dstType = this->context.addIvars(node.dstType);
    node.value->visit(*this);
    this->inheritDivergence(node, *node.value);

    this->context.equateTypesCoerce(node.value->span(), node.dstType, node.value);
    this->context.equateTypes(node.span(), node.resType, node.dstType);
}

auto ExprVisitorEnum::visit(HIRExprNodeIndex& node) -> void {
    auto _ = this->pushInnerCoerceScoped(false);

    TRACE_FUNCTION_F(static_cast<const void*>(&node) << StringView(" ... [ ... ]"));
    node.value->resType = this->context.addIvars(node.value->resType);
    node.cache.indexTy = this->context.ivars.newIvarTr();
    node.index->resType = this->context.addIvars(node.index->resType);

    node.value->visit(*this);
    node.index->visit(*this);
    this->inheritDivergence(node, *node.value);
    this->inheritDivergence(node, *node.index);
    this->context.equateTypesCoerce(node.index->span(), node.cache.indexTy, node.index);

    this->context.addRevisit(node);
}

auto ExprVisitorEnum::visit(HIRExprNodeDeref& node) -> void {
    auto _ = this->pushInnerCoerceScoped(false);

    node.value->resType = this->context.addIvars(node.value->resType);

    node.value->visit(*this);
    this->inheritDivergence(node, *node.value);

    const auto& ty = this->context.getType(node.value->resType);
    const HIRType* inner = nullptr;
    if (const auto* e = ty->opt_Borrow()) {
        inner = e->inner;
    } else if (const auto* e = ty->opt_Pointer()) {
        inner = e->inner;
    } else if (const auto* e = this->context.resolve.typeIsOwnedBox(node.span(), ty)) {
        inner = e;
    }
    if (!inner) {
        this->context.addRevisit(node);
        return;
    }

    node.traitUsed = HIRExprNodeDeref::TraitUsed::Builtin;
    this->context.equateTypes(node.span(), node.resType, inner);
}

auto ExprVisitorEnum::visit(HIRExprNodeEmplace& node) -> void {
    auto _ = this->pushInnerCoerceScoped(false);
    TRACE_FUNCTION_F(static_cast<const void*>(&node) << StringView(" ... <- ... "));
    node.place->resType = this->context.addIvars(node.place->resType);
    node.value->resType = this->context.addIvars(node.value->resType);

    node.place->visit(*this);
    this->inheritDivergence(node, *node.place);
    auto _2 = this->pushInnerCoerceScoped(true);
    node.value->visit(*this);
    this->inheritDivergence(node, *node.value);

    this->context.addRevisit(node);
}

auto ExprVisitorEnum::addIvarsGenericPath(const Span& sp, HIRGenericPath& gp) -> void {
    for (auto& ty : gp.params.types) {
        ty = this->context.addIvars(ty);
    }
}

auto ExprVisitorEnum::addIvarsPath(const Span& sp, HIRPath& path) -> void {
    switch (path.data.tag()) {
        case HIRPath::Data::TAG_Generic: {
            auto& e = path.data.as_Generic();
            this->addIvarsGenericPath(sp, e);
            break;
        }
        case HIRPath::Data::TAG_UfcsKnown: {
            auto& e = path.data.as_UfcsKnown();
            e.type = this->context.addIvars(e.type);
            this->addIvarsGenericPath(sp, e.trait);
            for (auto& ty : e.params.types) {
                ty = this->context.addIvars(ty);
            }
            break;
        }
        case HIRPath::Data::TAG_UfcsUnknown: {
            TODO(sp, StringView("Hit a UfcsUnknown (") << path << StringView(") - Is this an error?"));
            break;
        }
        case HIRPath::Data::TAG_UfcsInherent: {
            auto& e = path.data.as_UfcsInherent();
            e.type = this->context.addIvars(e.type);
            for (auto& ty : e.params.types) {
                ty = this->context.addIvars(ty);
            }
            break;
        }
    }
}

auto ExprVisitorEnum::getStructenumTy(const Span& sp, bool isStruct, HIRGenericPath& gp) -> const HIRType* {
    if (isStruct) {
        const auto& e = this->context.crate.getTypeitemByPath(sp, gp.path);
        if (e.is_Struct()) {
            const auto& str = e.as_Struct();
            fixParamCount(sp, this->context, nullptr, false, gp, str.params, gp.params);

            return this->context.crate.types.path(gp.clone(), HIRTypePathBinding::make_Struct(&str));
        } else if (e.is_Union()) {
            const auto& u = e.as_Union();
            fixParamCount(sp, this->context, nullptr, false, gp, u.params, gp.params);

            return this->context.crate.types.path(gp.clone(), HIRTypePathBinding::make_Union(&u));
        } else {
            BUG(sp, StringView("Path ") << gp << StringView(" doesn't refer to a struct/union"));
        }
    } else {
        auto sPath = getRuleParentPath(gp.path);

        const auto& enm = this->context.crate.getEnumByPath(sp, sPath);
        fixParamCount(sp, this->context, nullptr, false, gp, enm.params, gp.params);

        return this->context.crate.types.path(HIRGenericPath(mv$(sPath), gp.params.clone()), HIRTypePathBinding::make_Enum(&enm));
    }
}

auto ExprVisitorEnum::visit(HIRExprNodeTupleVariant& node) -> void {
    const auto& sp = node.span();
    TRACE_FUNCTION_F(static_cast<const void*>(&node) << StringView(" ") << node.path << StringView("(...) [") << StringView(node.isStruct ? "struct" : "enum") << StringView("]"));
    node.diverges = false;
    for (auto& val : node.args) {
        val->resType = this->context.addIvars(val->resType);
    }
    this->context.ivars.addIvarsParams(node.path.params);

    const auto ty = this->getStructenumTy(node.span(), node.isStruct, node.path);
    this->context.equateTypes(node.span(), node.resType, ty);

    const tTupleFields* fieldsPtr = nullptr;
    const HIRGenericParams* generics = nullptr;
    {
        auto& tuMatch = ty->as_Path().binding;
        switch (tuMatch.tag()) {
            case HIRTypePathBinding::TAG_Unbound: {
                break;
            }
            case HIRTypePathBinding::TAG_Opaque: {
                break;
            }
            case HIRTypePathBinding::TAG_Enum: {
                auto& e = tuMatch.as_Enum();
                const auto& varName = node.path.path.components().back();
                const auto& enm = *e;
                generics = &enm.params;
                size_t idx = enm.findVariant(varName);
                ASSERT_BUG(sp, idx < enm.data.as_Data().size(), StringView("Unknown variant - ") << node.path);
                const auto& varTy = enm.data.as_Data()[idx].type;
                ASSERT_BUG(sp, varTy->as_Path().binding.is_Struct(), StringView("Pointed variant of TupleVariant (") << node.path << StringView(") isn't a Tuple"));
                ASSERT_BUG(sp, varTy->as_Path().binding.as_Struct() != nullptr, StringView("Pointed variant of TupleVariant (") << node.path << StringView(") isn't a Tuple"));
                const auto& str = *varTy->as_Path().binding.as_Struct();
                ASSERT_BUG(sp, str.data.is_Tuple(), StringView("Pointed variant of TupleVariant (") << node.path << StringView(") isn't a Tuple"));
                fieldsPtr = &str.data.as_Tuple();
                break;
            }
            case HIRTypePathBinding::TAG_Struct: {
                auto& e = tuMatch.as_Struct();
                ASSERT_BUG(sp, e->data.is_Tuple(), StringView("Pointed struct in TupleVariant (") << node.path << StringView(") isn't a Tuple"));
                fieldsPtr = &e->data.as_Tuple();
                generics = &e->params;
                break;
            }
            case HIRTypePathBinding::TAG_Union: {
                BUG(sp, StringView("TupleVariant pointing to a union"));
                break;
            }
            case HIRTypePathBinding::TAG_ExternType: {
                BUG(sp, StringView("TupleVariant pointing to a extern type"));
                break;
            }
        }
    }
    BUG_ASSERT(fieldsPtr);
    BUG_ASSERT(generics);
    const tTupleFields& fields = *fieldsPtr;
    if (fields.size() != node.args.size()) {
        ERROR(node.span(), E0000, StringView("Tuple variant constructor argument count doesn't match type - ") << node.path);
    }

    auto monomorphCb = MonomorphStatePtr(this->context.crate.types, ty, &node.path.params, nullptr);

    applyBoundsAsRules(this->context, sp, *generics, monomorphCb, /*is_impl_level=*/true);

    node.argTypes.zero(node.args.size());
    for (unsigned int i = 0; i < node.args.size(); i++) {
        const auto& desTyR = fields[i].ent;
        const auto* desTy = &desTyR;
        if (monomorphiseTypeNeeded(desTyR)) {
            node.argTypes.mut(i) = monomorphCb.monomorphType(sp, desTyR);
            desTy = &node.argTypes[i];
        }

        this->context.equateTypesCoerce(node.span(), *desTy, node.args[i]);
    }

    auto _ = this->pushInnerCoerceScoped(true);
    for (auto& val : node.args) {
        val->visit(*this);
        this->context.requireSized(node.span(), val->resType);
        node.diverges = node.diverges || this->nodeDiverges(*val);
    }
}

auto ExprVisitorEnum::visit(HIRExprNodeStructLiteral& node) -> void {
    const auto& sp = node.span();
    TRACE_FUNCTION_F(static_cast<const void*>(&node) << StringView(" ") << node.type << StringView(" (") << node.realPath << StringView(") {...} [") << StringView(node.isStruct ? "struct" : "enum") << StringView("]"));
    node.diverges = false;
    auto _ = this->pushInnerCoerceScoped(true);

    if (node.realPath == HIRGenericPath()) {
        node.type = this->context.addIvars(node.type);
    }

    for (auto& val : node.values) {
        val.second->resType = this->context.addIvars(val.second->resType);
    }
    if (node.baseValue) {
        node.baseValue->resType = this->context.addIvars(node.baseValue->resType);
    }

    if (node.realPath == HIRGenericPath()) {
        auto t = this->context.expandAssociatedTypes(sp, mv$(node.type));
        node.type = nullptr;
        if (node.isStruct) {
            ASSERT_BUG(sp, ((*t).is_Path() && ((*t).as_Path().path.data.is_Generic())), StringView("Struct literal with non-Generic path - ") << t);
            node.realPath = t->as_Path().path.data.as_Generic().clone();
        } else {
            ASSERT_BUG(sp, ((*t).is_Path() && ((*t).as_Path().path.data.is_UfcsInherent())), StringView("Enum struct literal with non-UfcsInherent path - ") << t);
            auto& it = t->as_Path().path.data.as_UfcsInherent().type;
            auto& name = t->as_Path().path.data.as_UfcsInherent().item;
            ASSERT_BUG(sp, ((*it).is_Path() && ((*it).as_Path().path.data.is_Generic())), StringView("Struct literal with non-Generic path - ") << t);
            node.realPath = it->as_Path().path.data.as_Generic().clone();
            node.realPath.path += name;
        }
    }
    auto& tyPath = node.realPath;

    const auto ty = this->getStructenumTy(node.span(), node.isStruct, tyPath);
    this->context.equateTypes(node.span(), node.resType, ty);
    if (node.baseValue) {
        this->context.equateTypes(node.span(), node.baseValue->resType, ty);
    }

    const tStructFields* fieldsPtr = nullptr;
    const HIRGenericParams* generics = nullptr;
    tStructFields tupleFields;
    {
        auto& tuMatch = ty->as_Path().binding;
        switch (tuMatch.tag()) {
            case HIRTypePathBinding::TAG_Unbound: {
                break;
            }
            case HIRTypePathBinding::TAG_Opaque: {
                break;
            }
            case HIRTypePathBinding::TAG_ExternType: {
                break;
            }
            case HIRTypePathBinding::TAG_Enum: {
                auto& e = tuMatch.as_Enum();
                const auto& varName = tyPath.path.components().back();
                const auto& enm = *e;
                auto idx = enm.findVariant(varName);
                ASSERT_BUG(sp, idx != SIZE_MAX, StringView(""));
                ASSERT_BUG(sp, enm.data.is_Data(), StringView(""));
                const auto& var = enm.data.as_Data()[idx];
                if (var.type == this->context.crate.types.unit()) {
                    ASSERT_BUG(node.span(), node.values.size() == 0, StringView("Values provided for unit-like variant"));
                    ASSERT_BUG(node.span(), !node.baseValue, StringView("Values provided for unit-like variant"));
                    return;
                }
                const auto& str = *var.type->as_Path().binding.as_Struct();

                ASSERT_BUG(sp, var.isStruct, StringView("Struct literal for enum on non-struct variant"));
                fieldsPtr = &str.data.as_Named();
                generics = &enm.params;
                break;
            }
            case HIRTypePathBinding::TAG_Union: {
                auto& e = tuMatch.as_Union();
                fieldsPtr = &e->variants;
                generics = &e->params;
                if (node.baseValue) {
                    ERROR(node.span(), E0000, StringView("Union can't have a base value"));
                }
                ASSERT_BUG(node.span(), node.values.size() > 0, StringView("Union literal with no values"));
                ASSERT_BUG(node.span(), node.values.size() == 1, StringView("Union literal with multiple values"));
                break;
            }
            case HIRTypePathBinding::TAG_Struct: {
                auto& e = tuMatch.as_Struct();
                if (e->data.is_Tuple() && !node.values.empty()) {
                    const auto& tuple = e->data.as_Tuple();
                    for (size_t i = 0; i < tuple.size(); i++) {
                        tupleFields.push_back(HIRStructField{RcString::newInterned(FMT(i)), tuple[i].publicity, tuple[i].ent, nullptr});
                    }
                    fieldsPtr = &tupleFields;
                    generics = &e->params;
                    break;
                }
                if (e->data.is_Unit() || e->data.is_Tuple()) {
                    ASSERT_BUG(node.span(), node.values.size() == 0, StringView("Values provided for ") << e->data.tagStr() << StringView("-like struct"));

                    if (node.baseValue) {
                        auto _ = this->pushInnerCoerceScoped(false);
                        node.baseValue->visit(*this);
                        this->inheritDivergence(node, *node.baseValue);
                    }
                    return;
                }

                ASSERT_BUG(node.span(), e->data.is_Named(), StringView("StructLiteral not pointing to a braced struct, instead ") << e->data.tagStr() << StringView(" - ") << ty);
                fieldsPtr = &e->data.as_Named();
                generics = &e->params;
                break;
            }
        }
    }
    ASSERT_BUG(node.span(), fieldsPtr, StringView(""));
    BUG_ASSERT(generics);
    const tStructFields& fields = *fieldsPtr;

    auto monomorphCb = MonomorphStatePtr(this->context.crate.types, ty, &tyPath.params, nullptr);

    node.valueTypes.zero(fields.size());

    for (auto& val : node.values) {
        const auto& name = val.first;
        auto it = std::find_if(fields.begin(), fields.end(), [&](const HIRStructField& v) -> bool {
            return v.name == name;
        });
        ASSERT_BUG(node.span(), it != fields.end(), StringView("Field '") << name << StringView("' not found in struct ") << tyPath);
        const auto& desTyR = it->ty;
        auto& desTyCache = node.valueTypes.mut(it - fields.begin());
        const auto* desTy = &desTyR;

        DEBUG(name << StringView(" : ") << desTyR);
        if (monomorphiseTypeNeeded(desTyR)) {
            if (desTyCache == nullptr) {
                desTyCache = monomorphCb.monomorphType(node.span(), desTyR);
            } else {
                // TODO: Is it an error when it's already populated?
            }
            desTy = &desTyCache;
        }
        this->context.equateTypesCoerce(node.span(), *desTy, val.second);
    }

    applyBoundsAsRules(context, node.span(), *generics, monomorphCb, /*is_impl_level=*/true);

    for (auto& val : node.values) {
        val.second->visit(*this);
        this->context.requireSized(node.span(), val.second->resType);
        node.diverges = node.diverges || this->nodeDiverges(*val.second);
    }
    if (node.baseValue) {
        auto _ = this->pushInnerCoerceScoped(false);
        node.baseValue->visit(*this);
        node.diverges = node.diverges || this->nodeDiverges(*node.baseValue);
    }
}

auto ExprVisitorEnum::visit(HIRExprNodeUnitVariant& node) -> void {
    const auto ty = this->getStructenumTy(node.span(), node.isStruct, node.path);
    this->context.equateTypes(node.span(), node.resType, ty);

    const HIRGenericParams* generics = nullptr;
    {
        auto& tuMatch = ty->as_Path().binding;
        switch (tuMatch.tag()) {
            case HIRTypePathBinding::TAG_Unbound: {
                break;
            }
            case HIRTypePathBinding::TAG_Opaque: {
                break;
            }
            case HIRTypePathBinding::TAG_Enum: {
                auto& e = tuMatch.as_Enum();
                generics = &e->params;
                break;
            }
            case HIRTypePathBinding::TAG_Union: {
                break;
            }
            case HIRTypePathBinding::TAG_ExternType: {
                break;
            }
            case HIRTypePathBinding::TAG_Struct: {
                auto& e = tuMatch.as_Struct();
                generics = &e->params;
                break;
            }
        }
    }
    ASSERT_BUG(node.span(), generics, StringView("Unit variant has invalid type ") << ty);
    auto monomorph = MonomorphStatePtr(this->context.crate.types, ty, &node.path.params, nullptr);
    applyBoundsAsRules(this->context, node.span(), *generics, monomorph, /*is_impl_level=*/true);
}

auto ExprVisitorEnum::visit(HIRExprNodeCallPath& node) -> void {
    this->visitPath(node.span(), node.path);
    TRACE_FUNCTION_F(static_cast<const void*>(&node) << StringView(" ") << node.path << StringView("(...)"));
    for (auto& val : node.args) {
        val->resType = this->context.addIvars(val->resType);
    }

    const bool cacheOk = visitCallPopulateCache(this->context, node.span(), node.path, node.cache);
    if (cacheOk) {
        BUG_ASSERT(node.cache.argTypes.length() >= 1);
        unsigned int expArgc = node.cache.argTypes.length() - 1;

        if (node.args.size() != expArgc) {
            if (node.cache.fcn->variadic && node.args.size() > expArgc) {
            } else {
                ERROR(node.span(), E0000, StringView("Incorrect number of arguments to ") << node.path << StringView(" - exp ") << expArgc << StringView(" got ") << node.args.size());
            }
        }

        // TODO: Figure out a way to disable coercions in desugared for loops (will speed up typecheck)

        for (unsigned int i = 0; i < node.cache.argTypes.length() - 1; i++) {
            this->context.equateTypesCoerce(node.span(), node.cache.argTypes[i], node.args[i]);
        }
        this->context.equateTypes(node.span(), node.resType, node.cache.argTypes.back());
    } else {
        this->context.addRevisit(node);
    }

    {
        auto _ = this->pushInnerCoerceScoped(true);
        for (auto& val : node.args) {
            val->visit(*this);
            this->inheritDivergence(node, *val);
        }
    }
    if (cacheOk) {
        this->context.requireSized(node.span(), node.resType);
    }
}

auto ExprVisitorEnum::visit(HIRExprNodeCallValue& node) -> void {
    node.value->resType = this->context.addIvars(node.value->resType);
    for (auto& val : node.args) {
        val->resType = this->context.addIvars(val->resType);
        node.argIvars.pushBack(this->context.ivars.newIvarTr());
    }

    {
        auto _ = this->pushInnerCoerceScoped(false);
        node.value->visit(*this);
    }
    this->inheritDivergence(node, *node.value);
    auto _ = this->pushInnerCoerceScoped(true);
    for (unsigned int i = 0; i < node.args.size(); i++) {
        auto& val = node.args[i];
        this->context.equateTypesCoerce(val->span(), node.argIvars[i], val);
        val->visit(*this);
        this->inheritDivergence(node, *val);
    }
    this->context.requireSized(node.span(), node.resType);

    this->context.addRevisit(node);
}

auto ExprVisitorEnum::visit(HIRExprNodeCallMethod& node) -> void {
    TRACE_FUNCTION_F(static_cast<const void*>(&node) << StringView(" (...).") << node.method << StringView("(...)"));
    node.value->resType = this->context.addIvars(node.value->resType);
    for (auto& val : node.args) {
        val->resType = this->context.addIvars(val->resType);
    }
    this->context.ivars.addIvarsParams(node.params);

    const RcString& methodName = node.method;
    const RcString& fallbackMethodName = node.fallbackMethod;
    tTraitList possibleTraits;
    unsigned int maxNumParams = 0;
    unsigned int maxNumValueParams = 0;
    auto visitTraitInner = [&methodName, &fallbackMethodName, &maxNumParams, &maxNumValueParams, &possibleTraits](const HIRSimplePath& p, const HIRTrait& tr, bool push) {
        auto it = tr.values.find(methodName);
        if (it == tr.values.end() && methodName != fallbackMethodName) {
            it = tr.values.find(fallbackMethodName);
        }
        if (it == tr.values.end()) {
            return;
        }
        if (!it->second.is_Function()) {
            return;
        }
        if (tr.params.types.size() > maxNumParams) {
            maxNumParams = tr.params.types.size();
        }
        if (tr.params.values.size() > maxNumValueParams) {
            maxNumValueParams = tr.params.values.size();
        }

        if (push) {
            DEBUG(StringView("Found method in ") << p << StringView(" (push)"));
            if (std::none_of(possibleTraits.begin(), possibleTraits.end(), [&](const auto& x) {
                return x.second == &tr;
            })) {
                possibleTraits.push_back(std::make_pair(&p, &tr));
            }
        } else {
            DEBUG(StringView("Found method in ") << p << StringView(" (no push)"));
        }
    };
    auto visitTrait = [&visitTraitInner](const HIRSimplePath& p, const HIRTrait& trait) {
        DEBUG(StringView("[visit_trait] ? ") << p);
        visitTraitInner(p, trait, true);
        for (const auto& pt : trait.allParentTraits) {
            visitTraitInner(pt.path.path, *pt.traitPtr, false);
        }
    };
    for (const auto& traitRef : ::reverse(traits)) {
        if (traitRef.first == nullptr) {
            break;
        }
        visitTrait(*traitRef.first, *traitRef.second);
    }
    if (context.resolve.currentTraitPath()) {
        const HIRSimplePath& tp = context.resolve.currentTraitPath()->path;
        const HIRTrait& tr = context.resolve.hirCrate().getTraitByPath(node.span(), tp);
        visitTrait(tp, tr);
    }
    node.traits = mv$(possibleTraits);
    node.traitParamTypeIvars = maxNumParams;
    node.traitParamIvars.grow(maxNumParams + maxNumValueParams);
    for (unsigned int i = 0; i < maxNumParams; i++) {
        node.traitParamIvars.pushBack(this->context.ivars.newIvar());
    }
    for (unsigned int i = 0; i < maxNumValueParams; i++) {
        node.traitParamIvars.pushBack(this->context.ivars.newIvarVal());
    }

    {
        auto _ = this->pushInnerCoerceScoped(false);
        node.value->visit(*this);
    }
    this->inheritDivergence(node, *node.value);
    auto _ = this->pushInnerCoerceScoped(true);
    for (auto& val : node.args) {
        val->visit(*this);
        this->inheritDivergence(node, *val);
    }
    this->context.requireSized(node.span(), node.resType);

    this->context.addRevisit(node);
}

auto ExprVisitorEnum::visit(HIRExprNodeField& node) -> void {
    auto _ = this->pushInnerCoerceScoped(false);
    TRACE_FUNCTION_F(static_cast<const void*>(&node) << StringView(" (...).") << node.field);
    node.value->resType = this->context.addIvars(node.value->resType);

    node.value->visit(*this);
    this->inheritDivergence(node, *node.value);

    this->context.addRevisit(node);
}

auto ExprVisitorEnum::visit(HIRExprNodeTuple& node) -> void {
    TRACE_FUNCTION_F(static_cast<const void*>(&node) << StringView(" (...,)"));
    node.diverges = false;
    for (auto& val : node.vals) {
        val->resType = this->context.addIvars(val->resType);
    }

    if (canCoerceInnerResult()) {
        DEBUG(StringView("Tuple inner coerce"));
        const auto& ty = this->context.getType(node.resType);
        if (const auto* e = ty->opt_Tuple()) {
            if (e->length() != node.vals.size()) {
                ERROR(node.span(), E0000, StringView("Tuple literal node count mismatches with return type"));
            }
        } else if (ty->is_Infer()) {
            Vector<const HIRType*> tupleTys;
            for (const auto& val : node.vals) {
                tupleTys.pushBack(this->context.ivars.newIvarTr());
            }
            this->context.equateTypes(node.span(), node.resType, this->context.crate.types.tuple(mv$(tupleTys)));
        } else {
            ERROR(node.span(), E0000, StringView("Tuple literal used where a non-tuple expected - ") << ty);
        }
        const auto& innerTys = this->context.getType(node.resType)->as_Tuple();
        BUG_ASSERT(innerTys.length() == node.vals.size());

        for (unsigned int i = 0; i < innerTys.length(); i++) {
            this->context.equateTypesCoerce(node.span(), innerTys[i], node.vals[i]);
        }
    } else {
        Vector<const HIRType*> tupleTys;
        for (const auto& val : node.vals) {
            tupleTys.pushBack(val->resType);
        }
        this->context.equateTypes(node.span(), node.resType, this->context.crate.types.tuple(mv$(tupleTys)));
    }

    for (auto& val : node.vals) {
        val->visit(*this);
        this->context.requireSized(node.span(), val->resType);
        node.diverges = node.diverges || this->nodeDiverges(*val);
    }
}

auto ExprVisitorEnum::visit(HIRExprNodeArrayList& node) -> void {
    TRACE_FUNCTION_F(static_cast<const void*>(&node) << StringView(" [...,]"));
    node.diverges = false;
    auto _ = this->pushInnerCoerceScoped(true);
    for (auto& val : node.vals) {
        val->resType = this->context.addIvars(val->resType);
    }

    auto arrayTy = this->context.crate.types.array(context.ivars.newIvarTr(), node.vals.size());
    this->context.equateTypes(node.span(), node.resType, arrayTy);
    const auto& innerTy = arrayTy->as_Array().inner;
    for (auto& val : node.vals) {
        this->equateTypesInnerCoerce(node.span(), innerTy, val);
    }

    for (auto& val : node.vals) {
        val->visit(*this);
        node.diverges = node.diverges || this->nodeDiverges(*val);
    }
}

auto ExprVisitorEnum::visit(HIRExprNodeArraySized& node) -> void {
    TRACE_FUNCTION_F(static_cast<const void*>(&node) << StringView(" [...; ") << node.size << StringView("]"));
    node.diverges = false;
    node.val->resType = this->context.addIvars(node.val->resType);

    if (node.size.is_Unevaluated()) {
        this->context.ivars.addIvars(node.size.as_Unevaluated());
    }

    auto ty = this->context.crate.types.array(context.ivars.newIvarTr(), node.size.clone());
    this->context.equateTypes(node.span(), node.resType, ty);
    const auto& innerTy = ty->as_Array().inner;
    this->equateTypesInnerCoerce(node.span(), innerTy, node.val);

    node.val->visit(*this);
    node.diverges = this->nodeDiverges(*node.val);
    this->context.addRevisit(node);
}

auto ExprVisitorEnum::visit(HIRExprNodeLiteral& node) -> void {
    const HIRType* ty;
    switch (node.data.tag()) {
        case HIRExprLiteral::TAG_Integer: {
            auto& e = node.data.as_Integer();
            DEBUG(StringView("_Literal (: ") << e.type << StringView(" = ") << e.value << StringView(")"));
            if (e.type != HIRCoreType::Str) {
                ty = this->context.crate.types.primitive(e.type);
            } else {
                ty = this->context.crate.types.infer(~0, HIRInferClass::Integer);
            }
            break;
        }
        case HIRExprLiteral::TAG_Float: {
            auto& e = node.data.as_Float();
            DEBUG(StringView("_Literal (: ") << node.resType << StringView(" = ") << e.value << StringView(")"));
            if (e.type != HIRCoreType::Str) {
                ty = this->context.crate.types.primitive(e.type);
            } else {
                ty = this->context.crate.types.infer(~0, HIRInferClass::Float);
            }
            break;
        }
        case HIRExprLiteral::TAG_Boolean: {
            auto& e = node.data.as_Boolean();
            DEBUG(StringView("_Literal ( ") << StringView(e ? "true" : "false") << StringView(")"));
            ty = this->context.crate.types.primitive(HIRCoreType::Bool);
            break;
        }
        case HIRExprLiteral::TAG_String: {
            // TODO: &'static
            DEBUG(StringView("_Literal (&str)"));
            ty = this->context.crate.types.borrow(HIRBorrowType::Shared, this->context.crate.types.primitive(HIRCoreType::Str));
            break;
        }
        case HIRExprLiteral::TAG_ByteString: {
            auto& e = node.data.as_ByteString();
            // TODO: &'static
            DEBUG(StringView("_Literal (&[u8])"));
            ty = this->context.crate.types.borrow(HIRBorrowType::Shared, this->context.crate.types.array(this->context.crate.types.primitive(HIRCoreType::U8), e.length()));
            break;
        }
        case HIRExprLiteral::TAG_CString: {
            DEBUG(StringView("_Literal (&CStr)"));
            auto p = context.crate.getLangItemPath(node.span(), "CStr");
            ty = this->context.crate.types.path(p, &context.crate.getStructByPath(node.span(), p));
            ty = this->context.crate.types.borrow(HIRBorrowType::Shared, ty);
            break;
        }
    }
    ty = this->context.addIvars(ty);
    this->context.equateTypes(node.span(), node.resType, ty);
}

auto ExprVisitorEnum::visit(HIRExprNodePathValue& node) -> void {
    const auto& sp = node.span();
    this->visitPath(sp, node.path);

    TRACE_FUNCTION_F(static_cast<const void*>(&node) << StringView(" ") << node.path);
    this->addIvarsPath(node.span(), node.path);

    switch (node.path.data.tag()) {
        case HIRPathData::TAG_Generic: {
            auto& e = node.path.data.as_Generic();
            switch (node.target) {
                case HIRExprNodePathValue::UNKNOWN:
                    BUG(sp, StringView("_PathValue with target=UNKNOWN and a Generic path - ") << e.path);
                case HIRExprNodePathValue::FUNCTION: {
                    const auto& f = this->context.crate.getFunctionByPath(sp, e.path);
                    fixParamCount(sp, this->context, nullptr, false, e, f.params, e.params);

                    auto ms = MonomorphStatePtr(this->context.crate.types, nullptr, nullptr, &e.params);
                    auto ty = this->context.crate.types.intern(HIRType::make_NamedFunction({node.path.clone(), &f}));

                    applyBoundsAsRules(this->context, sp, f.params, ms, /*is_impl_level=*/false);

                    DEBUG(StringView("> ") << node.path << StringView(" = ") << ty);
                    this->context.equateTypes(sp, node.resType, ty);
                } break;
                case HIRExprNodePathValue::STRUCT_CONSTR: {
                    const auto& s = this->context.crate.getStructByPath(sp, e.path);
                    fixParamCount(sp, this->context, nullptr, false, e, s.params, e.params);

                    auto ms = MonomorphStatePtr(this->context.crate.types, nullptr, &e.params, nullptr);
                    auto ty = this->context.crate.types.intern(HIRType::make_NamedFunction({node.path.clone(), &s}));

                    applyBoundsAsRules(this->context, sp, s.params, ms, /*is_impl_level=*/true);
                    this->context.equateTypes(sp, node.resType, ty);
                } break;
                case HIRExprNodePathValue::ENUM_VAR_CONSTR: {
                    const auto& varName = e.path.components().back();
                    auto enumPath = getRuleParentPath(e.path);
                    const auto& enm = this->context.crate.getEnumByPath(sp, enumPath);
                    fixParamCount(sp, this->context, nullptr, false, e, enm.params, e.params);
                    size_t idx = enm.findVariant(varName);
                    ASSERT_BUG(sp, idx != SIZE_MAX, StringView("Missing variant - ") << e.path);
                    ASSERT_BUG(sp, enm.data.is_Data(), StringView("Enum ") << enumPath << StringView(" isn't a data-holding enum"));

                    auto ms = MonomorphStatePtr(this->context.crate.types, nullptr, &e.params, nullptr);
                    auto ty = this->context.crate.types.intern(HIRType::make_NamedFunction({node.path.clone(), HIRTypeDataNamedFunctionTy::make_EnumConstructor({&enm, idx})}));
                    applyBoundsAsRules(this->context, sp, enm.params, ms, /*is_impl_level=*/true);
                    this->context.equateTypes(sp, node.resType, ty);
                } break;
                case HIRExprNodePathValue::STATIC: {
                    const auto& v = this->context.crate.getStaticByPath(sp, e.path);
                    DEBUG(StringView("static v.m_type = ") << v.type);
                    this->context.equateTypes(sp, node.resType, v.type);
                } break;
                case HIRExprNodePathValue::CONSTANT: {
                    const auto& v = this->context.crate.getConstantByPath(sp, e.path);
                    DEBUG(StringView("const") << v.params.fmtArgs() << StringView(" v.m_type = ") << v.type);
                    fixParamCount(sp, this->context, nullptr, false, e, v.params, e.params);

                    auto ms = MonomorphStatePtr(this->context.crate.types, nullptr, nullptr, &e.params);
                    applyBoundsAsRules(this->context, sp, v.params, ms, /*is_impl_level=*/false);

                    const HIRType* tmp;
                    const auto* ty = ms.maybeMonomorphType(sp, v.type);
                    this->context.equateTypes(sp, node.resType, ty);
                } break;
            }
            break;
        }
        case HIRPathData::TAG_UfcsUnknown: {
            BUG(sp, StringView("Encountered UfcsUnknown"));
            break;
        }
        case HIRPathData::TAG_UfcsKnown: {
            auto& e = node.path.data.as_UfcsKnown();
            const auto& trait = this->context.crate.getTraitByPath(sp, e.trait.path);
            fixParamCount(sp, this->context, e.type, true, e.trait, trait.params, e.trait.params);

            this->context.addTraitBound(sp, e.type, e.trait.path, e.trait.params.clone());

            auto it = trait.values.find(e.item);
            if (it == trait.values.end()) {
                ERROR(sp, E0000, StringView("`") << e.item << StringView("` is not a value member of trait ") << e.trait.path);
            }
            switch (it->second.tag()) {
                case HIRTraitValueItem::TAG_Constant: {
                    auto& ie = it->second.as_Constant();
                    fixParamCount(sp, this->context, e.type, false, node.path, ie.params, e.params);

                    auto ms = MonomorphStatePtr(this->context.crate.types, e.type, &e.trait.params, &e.params);
                    applyBoundsAsRules(this->context, sp, ie.params, ms, /*is_impl_level=*/false);

                    const HIRType* tmp;
                    const auto* ty = ms.maybeMonomorphType(sp, ie.type);
                    this->context.equateTypes(sp, node.resType, ty);
                    break;
                }
                case HIRTraitValueItem::TAG_Static: {
                    auto& ie = it->second.as_Static();
                    TODO(sp, StringView("Monomorpise associated static type - ") << ie.type);
                    break;
                }
                case HIRTraitValueItem::TAG_Function: {
                    auto& ie = it->second.as_Function();
                    fixParamCount(sp, this->context, e.type, false, node.path, ie.params, e.params);

                    auto ms = MonomorphStatePtr(this->context.crate.types, e.type, &e.trait.params, &e.params);
                    applyBoundsAsRules(this->context, sp, ie.params, ms, /*is_impl_level=*/false);

                    auto ty = this->context.crate.types.intern(HIRType::make_NamedFunction({node.path.clone(), &ie}));
                    this->context.equateTypes(node.span(), node.resType, ty);
                    break;
                }
            }
            break;
        }
        case HIRPathData::TAG_UfcsInherent: {
            if (!resolveInherentPathValue(this->context, node)) {
                this->context.addRevisit(node);
            }
            break;
        }
    }
}

auto ExprVisitorEnum::visit(HIRExprNodeVariable& node) -> void {
    TRACE_FUNCTION_F(static_cast<const void*>(&node) << StringView(" ") << node.name << StringView("{") << node.slot << StringView("}"));
    this->context.equateTypes(node.span(), node.resType, this->context.getVar(node.span(), node.slot));
}

auto ExprVisitorEnum::visit(HIRExprNodeConstParam& node) -> void {
    TRACE_FUNCTION_F(static_cast<const void*>(&node) << StringView(" ") << node.name << StringView("{") << node.binding << StringView("}"));
    this->context.equateTypes(node.span(), node.resType, this->context.resolve.getConstParamType(node.span(), node.binding));
}

auto ExprVisitorEnum::visit(HIRExprNodeClosure& node) -> void {
    TRACE_FUNCTION_F(static_cast<const void*>(&node) << StringView(" |...| ..."));
    for (auto& arg : node.args) {
        arg.second = this->context.addIvars(arg.second);
        this->context.handlePattern(node.span(), arg.first, arg.second);
    }
    node.returnType = this->context.addIvars(node.returnType);
    node.code->resType = this->context.addIvars(node.code->resType);
    this->context.requireSized(node.span(), node.returnType);

    Vector<const HIRType*> argTypes;
    for (auto& arg : node.args) {
        argTypes.pushBack(arg.second);
    }
    this->context.equateTypes(node.span(), node.resType, this->context.crate.types.closure(&node));

    this->context.equateTypesCoerce(node.span(), node.returnType, node.code);

    auto savedLoops = std::move(this->loopBlocks);

    auto _ = this->pushInnerCoerceScoped(true);
    this->closureRetTypes.pushBack(RetTarget(node.returnType));
    node.code->visit(*this);
    this->closureRetTypes.popBack();

    this->loopBlocks = std::move(savedLoops);
}

auto ExprVisitorEnum::visit(HIRExprNodeGenerator& node) -> void {
    TRACE_FUNCTION_F(static_cast<const void*>(&node) << StringView(" /*gen*/ || ..."));
    node.returnType = this->context.addIvars(node.returnType);
    node.yieldTy = this->context.addIvars(node.yieldTy);
    node.resumeTy = this->context.addIvars(node.resumeTy);
    node.code->resType = this->context.addIvars(node.code->resType);
    this->context.requireSized(node.span(), node.returnType);
    this->context.requireSized(node.span(), node.yieldTy);
    if (node.hasResumePattern) {
        this->context.handlePattern(node.span(), node.resumePattern, node.resumeTy);
    }

    this->context.equateTypes(node.span(), node.resType, this->context.crate.types.generator(&node));

    this->context.equateTypesCoerce(node.span(), node.returnType, node.code);
    // TODO: Save/clear/restore loop labels
    auto _ = this->pushInnerCoerceScoped(true);
    this->closureRetTypes.pushBack(RetTarget(node.returnType, node.resumeTy, node.yieldTy));
    node.code->visit(*this);
    this->closureRetTypes.popBack();
}

auto ExprVisitorEnum::visit(HIRExprNodeGeneratorWrapper& node) -> void {
    BUG(node.span(), StringView("ExprNode_GeneratorWrapper unexpected at this time"));
}

auto ExprVisitorEnum::visit(HIRExprNodeAsyncBlock& node) -> void {
    TRACE_FUNCTION_F(static_cast<const void*>(&node) << StringView(" async { ... }"));
    ASSERT_BUG(node.span(), node.code, StringView("empty async?"));
    node.returnType = this->context.revealOpaqueType(node.returnType);
    node.returnType = this->context.addIvars(node.returnType);
    node.code->resType = this->context.addIvars(node.code->resType);
    this->context.requireSized(node.span(), node.returnType);

    this->context.equateTypes(node.span(), node.resType, this->context.crate.types.asyncBlock(&node));
    this->context.equateTypesCoerce(node.span(), node.returnType, node.code);

    // TODO: Save/clear/restore loop labels
    auto _ = this->pushInnerCoerceScoped(true);
    if (node.isAsyncGen) {
        node.yieldTy = this->context.addIvars(node.yieldTy);
        this->closureRetTypes.pushBack(RetTarget(node.returnType, this->context.crate.types.unit(), node.yieldTy));
    } else {
        this->closureRetTypes.pushBack(RetTarget(node.returnType));
    }
    node.code->visit(*this);
    this->closureRetTypes.popBack();
}

auto ExprVisitorEnum::nodeDiverges(const HIRExprNode& node) const -> bool {
    return node.diverges || this->context.getType(node.resType)->is_Diverge();
}

auto ExprVisitorEnum::inheritDivergence(HIRExprNode& node, const HIRExprNode& child) const -> void {
    node.diverges = node.diverges || this->nodeDiverges(child);
}

auto ExprVisitorEnum::pushTraits(const tTraitList& list) -> void {
    this->traits.insert(this->traits.end(), list.begin(), list.end());
}

auto ExprVisitorEnum::popTraits(const tTraitList& list) -> void {
    this->traits.erase(this->traits.end() - list.size(), this->traits.end());
}

auto ExprVisitorEnum::visitGenericPath(const Span& sp, HIRGenericPath& gp) -> void {
    this->context.ivars.addIvarsParams(gp.params);
}

auto ExprVisitorEnum::visitPath(const Span& sp, HIRPath& path) -> void {
    switch (path.data.tag()) {
        case HIRPath::Data::TAG_Generic: {
            auto& e = path.data.as_Generic();
            this->visitGenericPath(sp, e);
            break;
        }
        case HIRPath::Data::TAG_UfcsKnown: {
            auto& e = path.data.as_UfcsKnown();
            e.type = this->context.addIvars(e.type);
            this->visitGenericPath(sp, e.trait);
            this->context.ivars.addIvarsParams(e.params);
            break;
        }
        case HIRPath::Data::TAG_UfcsUnknown: {
            TODO(sp, StringView("Hit a UfcsUnknown (") << path << StringView(") - Is this an error?"));
            break;
        }
        case HIRPath::Data::TAG_UfcsInherent: {
            auto& e = path.data.as_UfcsInherent();
            e.type = this->context.addIvars(e.type);
            this->context.ivars.addIvarsParams(e.params);
            this->context.ivars.addIvarsParams(e.implParams);
            break;
        }
    }
}

auto ExprVisitorEnum::pushInnerCoerceScoped(bool val) -> InnerCoerceGuard {
    DEBUG(StringView("inner_coerce PUSH (S) ") << val);
    this->innerCoerceEnabledStack.pushBack(val);
    return InnerCoerceGuard(*this);
}

auto ExprVisitorEnum::pushInnerCoerce(bool val) -> void {
    DEBUG(StringView("inner_coerce PUSH ") << val);
    this->innerCoerceEnabledStack.pushBack(val);
}

auto ExprVisitorEnum::popInnerCoerce() -> void {
    BUG_ASSERT(this->innerCoerceEnabledStack.length());
    this->innerCoerceEnabledStack.popBack();
    DEBUG(StringView("inner_coerce POP ") << canCoerceInnerResult());
}

auto ExprVisitorEnum::canCoerceInnerResult() const -> bool {
    if (this->innerCoerceEnabledStack.length() == 0) {
        return true;
    } else {
        return this->innerCoerceEnabledStack.back();
    }
}

auto ExprVisitorEnum::equateTypesInnerCoerce(const Span& sp, const HIRType* target, HIRExprNodeP& node) -> void {
    DEBUG(StringView("can_coerce_inner_result() = ") << canCoerceInnerResult());
    if (canCoerceInnerResult()) {
        this->context.equateTypesCoerce(sp, target, node);
    } else {
        this->context.equateTypes(sp, target, node->resType);
    }
}

ExprVisitorEnum::RetTarget::RetTarget(const HIRType* retType)
    : retType(retType)
    , resumeType(nullptr)
    , yieldType(nullptr)
{
}

ExprVisitorEnum::RetTarget::RetTarget(const HIRType* retType, const HIRType* resumeType, const HIRType* yieldType)
    : retType(retType)
    , resumeType(resumeType)
    , yieldType(yieldType)
{
}

ExprVisitorEnum::RevisitDefaultUnit::RevisitDefaultUnit(HIRExprNode* node)
    : node(node)
{
}

auto ExprVisitorEnum::RevisitDefaultUnit::span(void) const -> const Span& {
    return node->span();
}

auto ExprVisitorEnum::RevisitDefaultUnit::fmt(ZeroCopyOutput& os) const -> void {
    os << StringView("RevisitDefaultUnit(") << static_cast<const void*>(node) << StringView(": ") << node->resType << StringView(")");
}

auto ExprVisitorEnum::RevisitDefaultUnit::revisit(Context& context, bool isFallback) -> bool {
    DEBUG(StringView("is_fallback=") << isFallback);
    const auto& ty = context.getType(node->resType);
    if (const auto* i = ty->opt_Infer()) {
        if (i->tyClass != HIRInferClass::None) {
            return true;
        }
        if (isFallback) {
            const IvarCoercionIndex obligations(context);
            if (i->index < obligations.refs.size()) {
                const auto& refs = obligations[i->index];
                if (!refs.coercions.empty() || !refs.associated.empty()) {
                    return false;
                }
            }
            context.equateTypes(node->span(), ty, context.crate.types.unit());
            return true;
        }
        return false;
    } else {
        return true;
    }
}

ExprVisitorEnum::InnerCoerceGuard::InnerCoerceGuard(ExprVisitorEnum& t)
    : t(t)
{
}

ExprVisitorEnum::InnerCoerceGuard::~InnerCoerceGuard() {
    t.innerCoerceEnabledStack.popBack();
    DEBUG(StringView("inner_coerce POP (S) ") << t.canCoerceInnerResult());
}

RpitOriginMonomorph::RpitOriginMonomorph(HIRTypeInterner& types)
    : HIRMatchGenerics(types.objectPool())
    , Monomorphiser(types)
{
}

auto RpitOriginMonomorph::matchTy(const HIRGenericRef& generic, const HIRType* type, tCbResolveType resolve) -> HIRCompare {
    type = resolve.getType(Span(), type);
    auto inserted = typeBindings.emplace(generic.binding, type);
    if (inserted.second) {
        return HIRCompare::Equal;
    }
    const auto* existing = resolve.getType(Span(), inserted.first->second);
    return existing == type ? HIRCompare::Equal : HIRCompare::Unequal;
}

auto RpitOriginMonomorph::matchVal(const HIRGenericRef& generic, const HIRConstGeneric& value) -> HIRCompare {
    auto inserted = valueBindings.emplace(generic.binding, value.clone());
    return inserted.second || inserted.first->second == value ? HIRCompare::Equal : HIRCompare::Unequal;
}

auto RpitOriginMonomorph::getType(const Span&, const HIRGenericRef& generic) const -> const HIRType* {
    const auto it = typeBindings.find(generic.binding);
    return it == typeBindings.end() ? types.generic(generic.name, generic.binding) : it->second;
}

auto RpitOriginMonomorph::getValue(const Span&, const HIRGenericRef& generic) const -> HIRConstGeneric {
    const auto it = valueBindings.find(generic.binding);
    return it == valueBindings.end() ? HIRConstGeneric::make_Generic(generic) : it->second.clone();
}

template <>
void stl::output<ZeroCopyOutput, PossibleType>(ZeroCopyOutput& out, PossibleType value) {
    value.fmt(out);
}

template <>
void stl::output<ZeroCopyOutput, std::vector<PossibleType>>(ZeroCopyOutput& out, const std::vector<PossibleType>& values) {
    outCont(out, values);
}

template <>
void stl::output<ZeroCopyOutput, CoerceResult>(ZeroCopyOutput& out, CoerceResult value) {
    out << static_cast<int>(value);
}

template <>
void stl::output<ZeroCopyOutput, Context::Coercion>(ZeroCopyOutput& os, const Context::Coercion& v) {
    os << StringView("R") << v.ruleIdx << StringView(" ") << v.leftTy << StringView(" := ");
    if (v.rightNodePtr) {
        os << static_cast<const void*>(v.rightNodePtr) << StringView(" ") << static_cast<const void*>(&**v.rightNodePtr) << StringView(" (") << v.sourceType() << StringView(")");
    } else {
        os << v.sourceType() << StringView(" (type-only)");
    }
    return;
}

template <>
void stl::output<ZeroCopyOutput, Context::Associated>(ZeroCopyOutput& os, const Context::Associated& v) {
    os << StringView("R") << v.ruleIdx << StringView(" ");
    if (v.name == "") {
        os << StringView("req ty ") << v.implTy << StringView(" impl ") << v.trait << v.params;
    } else {
        os << v.leftTy << StringView(" = ") << StringView("< `") << v.implTy << StringView("` as `") << v.trait << v.params << StringView("` >::") << v.name << v.atyPp;
    }
    if (v.isOperator) {
        os << StringView(" - op");
    }
    return;
}
