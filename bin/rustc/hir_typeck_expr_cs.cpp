#include "hir_typeck_expr_cs.h"

#include "hir_hir.h"
#include "hir_expr.h"
#include "settings.h"
#include "wire_board.h"
#include "hir_visitor.h"
#include "hir_expr_state.h"
#include "hir_typeck_static.h"
#include "hir_typeck_helpers.h"
#include "hir_typeck_expr_visit.h"
#include "hir_conv_main_bindings.h"
#include "hir_typeck_main_bindings.h"

#include <std/rng/mix.h>
#include <std/alg/defer.h>
#include <std/mem/obj_pool.h>

#include <optional>
#include <algorithm>

using namespace stl;

namespace {
    struct MonomorphEraseHrls: public Monomorphiser {
        explicit MonomorphEraseHrls(HIRTypeInterner& types);

        HIRTypeRef getType(const Span& sp, const HIRGenericRef& g) const override;

        HIRConstGeneric getValue(const Span& sp, const HIRGenericRef& g) const override;
    };

    struct ExprVisitorRevisit: public HIRExprVisitor {
        Context& context;
        bool completed;
        bool isFallback;
        const Vector<const HIRTypeData*>* passStartIvars;

        bool nodeDiverges(const HIRExprNode& node) const;

        ExprVisitorRevisit(Context& context, bool fallback = false, const Vector<const HIRTypeData*>* passStartIvars = nullptr);

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

        void bad_cast(const Span& sp, const HIRTypeData* srcTy, const HIRTypeData* tgtTy, const char* where);

        void equateFunctionSignature(const Span& sp, const HIRTypeDataFunctionPointer& dst, const HIRTypeDataFunctionPointer& src);

        void visit(HIRExprNodeCast& node) override;

        void visit(HIRExprNodeUnsize& node) override;

        void visit(HIRExprNodeIndex& node) override;

        void visit(HIRExprNodeDeref& node) override;

        void visitEmplace129(HIRExprNodeEmplace& node);

        void visit(HIRExprNodeEmplace& node) override;

        void visit(HIRExprNodeTupleVariant& node) override;

        void visit(HIRExprNodeCallPath& node) override;

        bool callAsyncCallable(HIRExprNodeCallValue& node, HIRTypeRef ty, const HIRPathParams& traitPp);

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
        const Context& context;
        const HMTypeInferrence& ivars;
        HIRPathParams nopImpl;
        HIRPathParams nopItem;

        ExprVisitorApply(const Context& context);

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

        void checkTypeResolvedTop(const Span& sp, HIRTypeRef& ty) const;

        void checkTypeResolvedConstgeneric(const Span& sp, HIRConstGeneric& v, const HIRTypeData* topType) const;

        void checkTypeResolvedPp(const Span& sp, HIRPathParams& pp, const HIRTypeData* topType) const;

        void checkTypeResolvedPath(const Span& sp, HIRPath& path) const;

        void checkTypeResolvedPath(const Span& sp, HIRPath& path, const HIRTypeData* topType) const;

        void checkTypeResolvedGenericpath(const Span& sp, HIRGenericPath& path) const;

        void checkTypeResolved(const Span& sp, HIRTypeRef& ty, const HIRTypeData* topType) const;

        void checkTypesEqual(const Span& sp, const HIRTypeData* l, const HIRTypeData* r) const;
        void visit(HIRExprNodeArraySized& node) override;
    };

    struct ExprVisitorPrint: public HIRExprVisitor {
        const Context& context;
        std::ostream& os;

        ExprVisitorPrint(const Context& context, std::ostream& os);

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
        std::vector<Context::Associated::StallDependency>& dependencies;
        std::vector<HIRTypeRef> pending;
        std::vector<HIRTypeRef> visited;
        bool hasRawInfer = false;

        void addType(HIRTypeRef type);

        void collect();
    };

    struct IvarDependencyIndex {
        Context& context;
        std::vector<std::vector<unsigned int>> associatedTargets;
        std::vector<std::vector<unsigned int>> possibilityTargets;

        static void collectDirectIvars(const HIRTypeData* type, std::vector<unsigned int>& out);

        static void deduplicate(std::vector<unsigned int>& values);

        IvarDependencyIndex(Context& context);

        void disableDependents(unsigned int source);
    };

    struct IvarCoercionRefs {
        std::vector<const Context::Coercion*> coercions;
    };

    struct IvarCoercionIndex {
        const Context& context;
        std::vector<IvarCoercionRefs> refs;

        void collectIvars(const HIRTypeData* root, std::vector<unsigned int>& out) const;

        template <typename T>
        void addRefs(const std::vector<unsigned int>& dependencies, std::vector<T> IvarCoercionRefs::* member, T value);

        explicit IvarCoercionIndex(const Context& context);

        const IvarCoercionRefs& operator[](unsigned int index) const;
    };

    struct ActiveOperatorOutput {
        unsigned int index;
        const ActiveOperatorOutput* parent;
    };

    struct PossibleType {
        enum {
            Equal,
            CoerceTo,
            CoerceFrom,
            UnsizeTo,
            UnsizeFrom,
        } cls;

        enum class State {
            Concrete,
            Barrier,
            Removed,
        } state;

        const HIRTypeData* ty;

        static PossibleType concrete(decltype(cls) cls, const HIRTypeData* ty);

        static PossibleType barrier(decltype(cls) cls);

        bool isActive() const;

        bool hasType() const;

        void remove();

        Ordering ord(const PossibleType& o) const;

        bool operator<(const PossibleType& o) const;

        bool operator==(const PossibleType& o) const;

        std::ostream& fmt(std::ostream& os) const;

        bool isSource() const;

        bool isDest() const;

        static bool isSourceS(const PossibleType& self);

        static bool isDestS(const PossibleType& self);

        bool isCoerce() const;

        bool isUnsize() const;

        static bool isCoerceS(const PossibleType& self);

        static bool isUnsizeS(const PossibleType& self);
    };

    struct TypeRestrictiveOrdering {
        static const HIRTypeData* matchAndExtractPtrTy(const HIRTypeData* ptrTpl, const HIRTypeData* ty);

        static Ordering getOrderingInfer(const Span& sp, const HIRTypeData* r);

        static Ordering getOrderingTy(const Span& sp, const Context& context, const HIRTypeData* l, const HIRTypeData* r, bool& outUnordered);

        static Ordering getOrderingPtr(const Span& sp, const Context& context, const HIRTypeData* l, const HIRTypeData* r, bool& outUnordered, bool deep = true);
    };

    struct InfoOrdering {
        enum eInfoOrdering {
            Incompatible,
            Less,
            Same,
            More,
        };

        static bool isInfer(const HIRTypeData* ty);

        static bool compareScore(int& score, const HIRTypeData* tyL, const HIRTypeData* tyR);

        static eInfoOrdering compare(const HIRTypeData* tyL, const HIRTypeData* tyR);

        static eInfoOrdering compareTop(const Context& context, const HIRTypeData* tyL, const HIRTypeData* tyR, bool shouldDeref);
    };

    struct RpitOriginMonomorph: public HIRMatchGenerics, public Monomorphiser {
        std::map<u32, HIRTypeRef> typeBindings;
        std::map<u32, HIRConstGeneric> valueBindings;

        explicit RpitOriginMonomorph(HIRTypeInterner& types);

        HIRCompare matchTy(const HIRGenericRef& generic, const HIRTypeData* type, tCbResolveType resolve) override;

        HIRCompare matchVal(const HIRGenericRef& generic, const HIRConstGeneric& value) override;

        HIRTypeRef getType(const Span&, const HIRGenericRef& generic) const override;

        HIRConstGeneric getValue(const Span&, const HIRGenericRef& generic) const override;
    };

}

struct OwnedImplMatcher: public HIRMatchGenerics {
    HIRPathParams& implParams;

    OwnedImplMatcher(HIRTypeInterner& types, HIRPathParams& implParams);

    HIRCompare matchTy(const HIRGenericRef& g, const HIRTypeData* ty, tCbResolveType _resolve_cb) override;

    HIRCompare matchVal(const HIRGenericRef& g, const HIRConstGeneric& sz) override;
};

struct ExprVisitorTagStaleIvars: public HIRExprVisitorDef {
    struct Mapper final: public MonomorphiserNop {
        mutable Vector<std::pair<unsigned, unsigned>> valueIndexes_;

        unsigned taggedIndex(Vector<std::pair<unsigned, unsigned>>& indexes, unsigned original) const;

        using MonomorphiserNop::MonomorphiserNop;

        HIRConstGeneric monomorphConstgeneric(const Span& sp, const HIRConstGeneric& value, bool allowInfer) const override;
    } mapper_;

    explicit ExprVisitorTagStaleIvars(HIRTypeInterner& types);

    [[nodiscard]] HIRTypeRef visitType(HIRTypeRef type) override;

    void visitPathParams(HIRPathParams& params) override;
};

struct ExprVisitorAddIvars: public HIRExprVisitorDef {
    Context& context;

    struct LocalImplTraitLowering: Monomorphiser {
        Context& context;
        mutable const HIRTypeData* curSelf = nullptr;

        explicit LocalImplTraitLowering(Context& context);

        HIRTypeRef getType(const Span& sp, const HIRGenericRef& generic) const override;

        HIRConstGeneric getValue(const Span& sp, const HIRGenericRef& generic) const override;

        HIRTypeRef monomorphType(const Span& sp, const HIRTypeData* type, bool allowInfer = true) const override;
    };

    ExprVisitorAddIvars(Context& context);

    void innerVisitType(HIRTypeRef& ty);

    void visitPathParams(HIRPathParams& pp) override;

    [[nodiscard]] HIRTypeRef visitType(HIRTypeRef ty) override;

    void visit(HIRExprNodeLet& node) override;
};

struct ExprVisitorEnum: public HIRExprVisitor {
    Context& context;
    const HIRTypeData* retType;

    struct RetTarget {
        const HIRTypeData* retType;
        const HIRTypeData* resumeType;
        const HIRTypeData* yieldType;

        RetTarget(const HIRTypeData* retType);

        RetTarget(const HIRTypeData* retType, const HIRTypeData* resumeType, const HIRTypeData* yieldType);
    };

    std::vector<RetTarget> closureRetTypes;

    std::vector<bool> innerCoerceEnabledStack;

    std::vector<HIRExprNodeLoop*> loopBlocks;

    tTraitList traits;

    struct RevisitDefaultUnit: public Context::Revisitor {
        HIRExprNode* node;

        RevisitDefaultUnit(HIRExprNode* node);

        const Span& span(void) const;

        void fmt(std::ostream& os) const;

        bool revisit(Context& context, bool isFallback);
    };

    ExprVisitorEnum(Context& context, tTraitList baseTraits, const HIRTypeData* retType);

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

    HIRTypeRef getStructenumTy(const Span& sp, bool isStruct, HIRGenericPath& gp);

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

    void equateTypesInnerCoerce(const Span& sp, const HIRTypeData* target, HIRExprNodeP& node);
};

namespace {

    inline HIRExprNodeP mkExprnodep(HIRExprNode* en, HIRTypeRef ty) {
        en->resType = mv$(ty);
        return HIRExprNodeP(en);
    }

    inline HIRSimplePath getParentPath(const HIRSimplePath& sp) {
        return sp.parent();
    }

    inline HIRGenericPath getParentPath(const HIRGenericPath& gp) {
        return HIRGenericPath(getParentPath(gp.path), gp.params.clone());
    }

    bool typeContainsImplPlaceholder(HIRTypeInterner& types, const HIRTypeData* t) {
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

            [[nodiscard]] HIRTypeRef visitType(HIRTypeRef ty) override {
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
}

#define NEWNODE(TY, SP, CLASS, ...) mkExprnodep(context.crate.pool->make<HIRExprNode##CLASS>(SP, ##__VA_ARGS__), TY)

void applyBoundsAsRules(Context& context, const Span& sp, const HIRGenericParams& paramsDef, const Monomorphiser& ms, bool isImplLevel);

namespace {

    // TODO: Convert these to `Revisitor` instances
}

void Context::equateTypes(const Span& sp, const HIRTypeData* li, const HIRTypeData* ri) {
    const auto& liRes = this->ivars.getType(li);
    const auto& riRes = this->ivars.getType(ri);
    if (li == ri || liRes == riRes || liRes->equalsIgnoringRegions(riRes)) {
        return;
    }

    ASSERT_BUG(sp, !typeContainsImplPlaceholder(crate.types, ri), "Type contained an impl placeholder parameter - " << ri);
    ASSERT_BUG(sp, !typeContainsImplPlaceholder(crate.types, li), "Type contained an impl placeholder parameter - " << li);

    HIRTypeRef lTmp;
    HIRTypeRef rTmp;
    const auto& lT = this->expandAssociatedTypes(sp, this->ivars.getType(li), lTmp);
    const auto& rT = this->expandAssociatedTypes(sp, this->ivars.getType(ri), rTmp);

    if (lT->is_Diverge() && !rT->is_Infer()) {
        return;
    }
    if (rT->is_Diverge() && !lT->is_Infer()) {
        return;
    }

    equateTypesInner(sp, lT, rT);
}

void Context::equateTypesInner(const Span& sp, const HIRTypeData* li, const HIRTypeData* ri) {
    const auto& liRes = this->ivars.getType(li);
    const auto& riRes = this->ivars.getType(ri);
    if (li == ri || liRes == riRes || liRes->equalsIgnoringRegions(riRes)) {
        return;
    }

    HIRTypeRef lTmp;
    HIRTypeRef rTmp;
    const auto& lT = this->expandAssociatedTypes(sp, this->ivars.getType(li), lTmp);
    const auto& rT = this->expandAssociatedTypes(sp, this->ivars.getType(ri), rTmp);
    if (lT == rT || lT->equalsIgnoringRegions(rT)) {
        return;
    }

    const auto* lPath = lT->opt_Path();
    const auto* rPath = rT->opt_Path();
    const auto* lProjection = lPath ? lPath->path.data.opt_UfcsKnown() : nullptr;
    const auto* rProjection = rPath ? rPath->path.data.opt_UfcsKnown() : nullptr;
    const bool lRigidProjection = lPath && (lPath->binding.is_Unbound() || lPath->binding.is_Opaque());
    const bool rRigidProjection = rPath && (rPath->binding.is_Unbound() || rPath->binding.is_Opaque());
    const auto typesMayRelate = [&](const HIRTypeData* left, const HIRTypeData* right) {
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
            HIRTypeRef leftAlias;
            HIRTypeRef rightAlias;
            HIRTypeRef leftSelf;
            HIRTypeRef rightSelf;

            DeferredRigidProjectionSelf(Span sp, HIRTypeRef leftAlias, HIRTypeRef rightAlias, HIRTypeRef leftSelf, HIRTypeRef rightSelf)
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

            void fmt(std::ostream& os) const override {
                os << "Deferred rigid projection self " << leftSelf << " = " << rightSelf;
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

    auto bindInferToAlias = [&](const HIRTypeData* infer, const HIRTypeData* alias) {
        const auto* inferData = infer->opt_Infer();
        if (!inferData || inferData->isLit() || this->resolve.typeContainsIvars(alias) || visitTyWith(alias, [&](const HIRTypeData* inner) {
            return inner == infer;
        })) {
            return false;
        }
        const auto ivarIdx = inferData->index;
        if (ivarIdx < ivarsSized.size() && ivarsSized.at(ivarIdx)) {
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
                ASSERT_BUG(sp, lAlias->params.types.size() == rAlias->params.types.size(), "Opaque alias type argument count mismatch");
                ASSERT_BUG(sp, lAlias->params.values.size() == rAlias->params.values.size(), "Opaque alias const argument count mismatch");
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

    auto equateErasedAlias = [&](const HIRTypeDataErasedType& erased, const auto& alias, const HIRTypeData* hiddenType) {
        if (!resolve.isOpaqueAliasDefiningScope(*alias.inner)) {
            return false;
        }

        auto inserted = this->erasedTypeAliases.insert(std::make_pair(alias.inner.get(), Context::TaitEntry{alias.params, hiddenType}));
        if (!inserted.second) {
            equateTypesInner(sp, inserted.first->second.ourType, hiddenType);
            return true;
        }

        struct MonomorphErasedSelf: MonomorphiserNop {
            const HIRTypeData* hiddenType;

            MonomorphErasedSelf(HIRTypeInterner& types, const HIRTypeData* hiddenType)
                : MonomorphiserNop(types)
                , hiddenType(hiddenType)
            {
            }

            HIRTypeRef getType(const Span&, const HIRGenericRef& type) const override {
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

    auto setIvar = [&](const HIRTypeData* dst, const HIRTypeData* src) {
        auto ivarIdx = dst->as_Infer().index;
        if (ivarIdx < ivarsSized.size() && ivarsSized.at(ivarIdx)) {
            this->requireSized(sp, src);
        }
        if (visitTyWith(src, [&](const HIRTypeData* ity) {
            return ity == dst;
        })) {
            auto newSrc = cloneTyWith(crate.types, sp, src, [&](const HIRTypeData* tpl, HIRTypeRef& outTy) -> bool {
                if (tpl->is_Path() && tpl->as_Path().binding.is_Unbound()) {
                    if (visitTyWith(src, [&](const HIRTypeData* ity) {
                        return ity == dst;
                    })) {
                        const auto& pe = tpl->as_Path().path.data.as_UfcsKnown();
                        outTy = this->ivars.newIvarTr();
                        this->equateTypesAssoc(sp, outTy, pe.trait.path, pe.trait.params.clone(), pe.type, pe.item.c_str(), pe.params, false);
                        return true;
                    } else {
                    }
                }
                return false;
            });
            ASSERT_BUG(
                sp,
                !visitTyWith(
                    newSrc,
                    [&](const HIRTypeData* ity) {
                return ity == dst;
            }
                ),
                ""
            );
            this->ivars.setIvarTo(ivarIdx, std::move(newSrc));
        } else {
            this->ivars.setIvarTo(ivarIdx, src);
        }
    };

    if (const auto* rE = rT->opt_Infer()) {
        if (const auto* lE = lT->opt_Infer()) {
            // TODO: Unify sized flags

            if ((rE->index < ivarsSized.size() && ivarsSized.at(rE->index)) || (lE->index < ivarsSized.size() && ivarsSized.at(lE->index))) {
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
                    ERROR(sp, E0000, "Type mismatch in path params (type count) `" << l << "` and `" << r << "`");
                }
                for (unsigned int i = 0; i < l.types.size(); i++) {
                    this->equateTypesInner(sp, l.types[i], r.types[i]);
                }

                if (l.values.size() != r.values.size()) {
                    ERROR(sp, E0000, "Type mismatch in path params (value count) `" << l << "` and `" << r << "`");
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
            }

            else if (rT->is_Diverge()) {
                if (const auto* rE = ri->opt_Infer()) {
                    this->ivars.setIvarTo(rE->index, lT);
                }
                return;
            } else {
            }

            if (lT->tag() != rT->tag()) {
                ERROR(sp, E0000, "Type mismatch between " << this->ivars.fmtType(lT) << " and " << this->ivars.fmtType(rT));
            }
            switch ((*lT).tag()) {
                case HIRTypeData::TAG_Infer: {
                    UNREACHABLE();
                }
                case HIRTypeData::TAG_Diverge: {
                    break;
                }
                case HIRTypeData::TAG_Primitive: {
                    auto& lE = (*lT).as_Primitive();
                    auto& rE = (*rT).as_Primitive();
                    if (lE != rE) {
                        ERROR(sp, E0000, "Type mismatch between " << lT << " and " << rT);
                    }
                    break;
                }
                case HIRTypeData::TAG_Path: {
                    auto& lE = (*lT).as_Path();
                    auto& rE = (*rT).as_Path();
                    if (!equalityPath(lE.path, rE.path)) {
                        ERROR(sp, E0000, "Type mismatch between " << lT << " and " << rT);
                    }
                    break;
                }
                case HIRTypeData::TAG_Generic: {
                    auto& lE = (*lT).as_Generic();
                    auto& rE = (*rT).as_Generic();
                    if (lE.binding != rE.binding) {
                        ERROR(sp, E0000, "Type mismatch between " << lT << " and " << rT);
                    }
                    break;
                }
                case HIRTypeData::TAG_TraitObject: {
                    auto& lE = (*lT).as_TraitObject();
                    auto& rE = (*rT).as_TraitObject();
                    if (lE.trait.path.path != rE.trait.path.path) {
                        ERROR(sp, E0000, "Type mismatch between " << lT << " and " << rT);
                    }
                    equalityTypeparams(lE.trait.path.params, rE.trait.path.params);
                    for (auto itL = lE.trait.typeBounds.begin(), itR = rE.trait.typeBounds.begin(); itL != lE.trait.typeBounds.end(); itL++, itR++) {
                        if (itL->first != itR->first) {
                            ERROR(sp, E0000, "Type mismatch between " << lT << " and " << rT << " - associated bounds differ");
                        }
                        this->equateTypesInner(sp, itL->second.type, itR->second.type);
                    }
                    if (lE.markers.size() != rE.markers.size()) {
                        ERROR(sp, E0000, "Type mismatch between " << lT << " and " << rT << " - trait counts differ");
                    }
                    // TODO: Is this list sorted in any way? (if it's not sorted, this could fail when source does Send+Any instead of Any+Send)
                    for (unsigned int i = 0; i < lE.markers.size(); i++) {
                        auto& lP = lE.markers[i];
                        auto& rP = rE.markers[i];
                        if (lP.path != rP.path) {
                            ERROR(sp, E0000, "Type mismatch between " << lT << " and " << rT);
                        }
                        equalityTypeparams(lP.params, rP.params);
                    }
                    break;
                }
                case HIRTypeData::TAG_ErasedType: {
                    auto& lE = (*lT).as_ErasedType();
                    auto& rE = (*rT).as_ErasedType();
                    if (lE.inner.tag() != rE.inner.tag()) {
                        ERROR(sp, E0000, "Type mismatch between " << lT << " and " << rT << " - different erased class");
                    }
                    switch (lE.inner.tag()) {
                        case TypeDataErasedTypeInner::TAG_Fcn: {
                            auto& lee = lE.inner.as_Fcn();
                            auto& ree = rE.inner.as_Fcn();
                            ASSERT_BUG(sp, lee.origin != HIRSimplePath(), "ErasedType " << lT << " wasn't bound to its origin");
                            ASSERT_BUG(sp, ree.origin != HIRSimplePath(), "ErasedType " << rT << " wasn't bound to its origin");
                            if (!equalityPath(lee.origin, ree.origin)) {
                                ERROR(sp, E0000, "Type mismatch between " << lT << " and " << rT << " - different source");
                            }
                            break;
                        }
                        case TypeDataErasedTypeInner::TAG_Alias: {
                            auto& lee = lE.inner.as_Alias();
                            auto& ree = rE.inner.as_Alias();
                            if (lee.inner != ree.inner) {
                                ERROR(sp, E0000, "Type mismatch between " << lT << " and " << rT << " - different source");
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
                case HIRTypeData::TAG_Array: {
                    auto& lE = (*lT).as_Array();
                    auto& rE = (*rT).as_Array();
                    this->equateTypesInner(sp, lE.inner, rE.inner);
                    if (lE.size != rE.size) {
                        if (lE.size.is_Unevaluated() || rE.size.is_Unevaluated()) {
                            if (!lE.size.is_Unevaluated()) {
                                assert(lE.size.is_Known());
                                assert(rE.size.is_Unevaluated());
                                this->equateValues(sp, freezeEncodedLiteral(*crate.pool, EncodedLiteral::makeUsize(lE.size.as_Known())), rE.size.as_Unevaluated());
                            } else if (!rE.size.is_Unevaluated()) {
                                assert(lE.size.is_Unevaluated());
                                assert(rE.size.is_Known());
                                this->equateValues(sp, lE.size.as_Unevaluated(), freezeEncodedLiteral(*crate.pool, EncodedLiteral::makeUsize(rE.size.as_Known())));
                            } else {
                                this->equateValues(sp, lE.size.as_Unevaluated(), rE.size.as_Unevaluated());
                            }
                        } else {
                            ERROR(sp, E0000, "Type mismatch between " << lT << " and " << rT << " - sizes differ");
                        }
                    }
                    break;
                }
                case HIRTypeData::TAG_Slice: {
                    auto& lE = (*lT).as_Slice();
                    auto& rE = (*rT).as_Slice();
                    this->equateTypesInner(sp, lE.inner, rE.inner);
                    break;
                }
                case HIRTypeData::TAG_Pattern: {
                    auto& lE = (*lT).as_Pattern();
                    auto& rE = (*rT).as_Pattern();
                    if (lE.pattern.alternatives.size() != rE.pattern.alternatives.size()) {
                        ERROR(sp, E0000, "Type mismatch between " << lT << " and " << rT << " - pattern alternative counts differ");
                    }
                    for (size_t i = 0; i < lE.pattern.alternatives.size(); i++) {
                        const auto& left = lE.pattern.alternatives[i];
                        const auto& right = rE.pattern.alternatives[i];
                        if (left.hasStart != right.hasStart || left.hasEnd != right.hasEnd || left.endInclusive != right.endInclusive) {
                            ERROR(sp, E0000, "Type mismatch between " << lT << " and " << rT << " - pattern shapes differ");
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
                case HIRTypeData::TAG_Tuple: {
                    auto& lE = (*lT).as_Tuple();
                    auto& rE = (*rT).as_Tuple();
                    if (lE.size() != rE.size()) {
                        ERROR(sp, E0000, "Type mismatch between " << lT << " and " << rT << " - Tuples are of different length");
                    }
                    for (unsigned int i = 0; i < lE.size(); i++) {
                        this->equateTypesInner(sp, lE[i], rE[i]);
                    }
                    break;
                }
                case HIRTypeData::TAG_Borrow: {
                    auto& lE = (*lT).as_Borrow();
                    auto& rE = (*rT).as_Borrow();
                    if (lE.type != rE.type) {
                        ERROR(sp, E0000, "Type mismatch between " << lT << " and " << rT << " - Borrow classes differ");
                    }
                    this->equateTypesInner(sp, lE.inner, rE.inner);
                    break;
                }
                case HIRTypeData::TAG_Pointer: {
                    auto& lE = (*lT).as_Pointer();
                    auto& rE = (*rT).as_Pointer();
                    if (lE.type != rE.type) {
                        ERROR(sp, E0000, "Type mismatch between " << lT << " and " << rT << " - Pointer mutability differs");
                    }
                    this->equateTypesInner(sp, lE.inner, rE.inner);
                    break;
                }
                case HIRTypeData::TAG_NamedFunction: {
                    auto& lE = (*lT).as_NamedFunction();
                    auto& rE = (*rT).as_NamedFunction();
                    if (!equalityPath(lE.path, rE.path)) {
                        ERROR(sp, E0000, "Type mismatch between " << lT << " and " << rT);
                    }
                    break;
                }
                case HIRTypeData::TAG_Function: {
                    auto& lE = (*lT).as_Function();
                    auto& rE = (*rT).as_Function();
                    if (lE.isUnsafe != rE.isUnsafe || lE.isVariadic != rE.isVariadic || lE.trackCaller != rE.trackCaller || lE.abi != rE.abi || lE.argTypes.size() != rE.argTypes.size()) {
                        ERROR(sp, E0000, "Type mismatch between " << lT << " and " << rT);
                    }
                    // TODO: HRLs
                    this->equateTypesInner(sp, lE.rettype, rE.rettype);
                    for (unsigned int i = 0; i < lE.argTypes.size(); i++) {
                        this->equateTypesInner(sp, lE.argTypes[i], rE.argTypes[i]);
                    }
                    break;
                }
                case HIRTypeData::TAG_NodeType: {
                    auto& lE = (*lT).as_NodeType();
                    auto& rE = (*rT).as_NodeType();
                    if (lE != rE) {
                        ERROR(sp, E0000, "Type mismatch between " << lT << " and " << rT);
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

                    [[nodiscard]] HIRTypeRef visitType(HIRTypeRef type) override {
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
                    ERROR(sp, E0000, "Value mismatch between " << normalizedL << " and " << normalizedR);
                }
            }
        }
    }
}

void Context::addBindingInner(const Span& sp, const HIRPatternBinding& pb, HIRTypeRef type) {
    assert(pb.isValid());
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

namespace {
    void fixupPatternValuePaths(Context& context, const Span& sp, HIRPattern::Value& val) {
        if (auto* ve = val.opt_Named()) {
            if (ve->binding && ve->path.data.is_UfcsKnown()) {
                auto& pe = ve->path.data.as_UfcsKnown();
                context.ivars.addIvars(pe.type);
                context.ivars.addIvarsParams(pe.trait.params);
                context.ivars.addIvarsParams(pe.params);
                context.addTraitBound(sp, pe.type, pe.trait.path, pe.trait.params.clone());
            } else if (ve->binding && ve->path.data.is_UfcsInherent()) {
                auto& pe = ve->path.data.as_UfcsInherent();
                context.ivars.addIvars(pe.type);
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
}

void Context::handlePattern(const Span& sp, HIRPattern& pat, const HIRTypeData* type, bool isIrrefutable /*=false*/) {
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
            HIRTypeRef outerTy;
            HIRPattern& pattern;
            HIRPatternBinding::Type outerMode;

            mutable std::vector<HIRTypeRef> tempIvars;
            mutable std::optional<HIRTypeRef> possibleType;
            mutable const HIRPattern* possibleTypePattern = nullptr;

            MatchErgonomicsRevisit(Span sp, bool isIrrefutable, HIRTypeRef outer, HIRPattern& pat, HIRPatternBinding::Type bindingMode = HIRPatternBinding::Type::Move)
                : sp(mv$(sp))
                , isIrrefutable(isIrrefutable)
                , outerTy(mv$(outer))
                , pattern(pat)
                , outerMode(bindingMode)
            {
            }

            const Span& span() const override {
                return sp;
            }

            void fmt(std::ostream& os) const override {
                os << "MatchErgonomicsRevisit { " << pattern << " : " << outerTy << " }";
            }

            bool revisit(Context& context, bool isFallbackMode) override {
                outerTy = context.expandAssociatedTypes(sp, mv$(outerTy));
                return this->revisitInnerReal(context, pattern, outerTy, outerMode, isFallbackMode);
            }

            // TODO: Recurse into inner patterns, creating new revisitors?

            bool revisitInner(Context& context, HIRPattern& pattern, const HIRTypeData* type, HIRPatternBinding::Type bindingMode) const {
                if (!revisitInnerReal(context, pattern, type, bindingMode, false)) {
                    context.addRevisitAdv(box$((MatchErgonomicsRevisit{sp, isIrrefutable, type, pattern, bindingMode})));
                }
                return true;
            }

            std::optional<HIRTypeRef> getPossibleTypeVal(Context& context, HIRPattern::Value& pv) const {
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
                        if (ve.binding) {
                            if (ve.path.data.is_UfcsKnown()) {
                                const auto& pe = ve.path.data.as_UfcsKnown();
                                auto ms = MonomorphStatePtr(context.crate.types, pe.type, &pe.trait.params, nullptr);
                                return ms.monomorphType(sp, ve.binding->type);
                            }
                            return ve.binding->type;
                        } else if (ve.path.data.is_Generic()) {
                            TODO(sp, "Look up pattern value: " << ve.path);
                        } else {
                            return std::nullopt;
                        }
                        break;
                    }
                }
                UNREACHABLE();
            }

            std::optional<HIRTypeRef> getPossibleTypeInner(Context& context, HIRPattern& pattern) const {
                std::optional<HIRTypeRef> possibleType;
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
                        BUG(sp, "Match ergonomics - & pattern");
                        break;
                    }
                    case HIRPatternData::TAG_Tuple: {
                        auto& e = pattern.data.as_Tuple();
                        if (tempIvars.size() != e.subPatterns.size()) {
                            for (size_t i = 0; i < e.subPatterns.size(); i++) {
                                tempIvars.push_back(context.ivars.newIvarTr());
                            }
                        }
                        decltype(tempIvars) tuple;
                        for (const auto& ty : tempIvars) {
                            tuple.push_back(ty);
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
                                BUG(sp, "");
                                break;
                            }
                            case HIRPatternPathBinding::TAG_Struct: {
                                auto& be = e.binding.as_Struct();
                                auto& p = e.path.data.as_Generic();
                                assert(be);
                                context.addIvarsParams(p.params);
                                possibleType = context.crate.types.path(p.clone(), HIRTypePathBinding(be));
                                break;
                            }
                            case HIRPatternPathBinding::TAG_Union: {
                                auto& be = e.binding.as_Union();
                                auto& p = e.path.data.as_Generic();
                                assert(be);
                                context.addIvarsParams(p.params);
                                possibleType = context.crate.types.path(p.clone(), HIRTypePathBinding(be));
                                break;
                            }
                            case HIRPatternPathBinding::TAG_Enum: {
                                auto& be = e.binding.as_Enum();
                                auto& p = e.path.data.as_Generic();
                                assert(be.ptr);
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
                                BUG(sp, "");
                                break;
                            }
                            case HIRPatternPathBinding::TAG_Struct: {
                                auto& be = e.binding.as_Struct();
                                auto& p = e.path.data.as_Generic();
                                assert(be);
                                context.addIvarsParams(p.params);
                                possibleType = context.crate.types.path(p.clone(), HIRTypePathBinding(be));
                                break;
                            }
                            case HIRPatternPathBinding::TAG_Union: {
                                auto& be = e.binding.as_Union();
                                auto& p = e.path.data.as_Generic();
                                assert(be);
                                context.addIvarsParams(p.params);
                                possibleType = context.crate.types.path(p.clone(), HIRTypePathBinding(be));
                                break;
                            }
                            case HIRPatternPathBinding::TAG_Enum: {
                                auto& be = e.binding.as_Enum();
                                auto& p = e.path.data.as_Generic();
                                assert(be.ptr);
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
                                BUG(sp, "");
                                break;
                            }
                            case HIRPatternPathBinding::TAG_Struct: {
                                auto& be = e.binding.as_Struct();
                                auto& p = e.path.data.as_Generic();
                                assert(be);
                                context.addIvarsParams(p.params);
                                possibleType = context.crate.types.path(p.clone(), HIRTypePathBinding(be));
                                break;
                            }
                            case HIRPatternPathBinding::TAG_Union: {
                                auto& be = e.binding.as_Union();
                                auto& p = e.path.data.as_Generic();
                                assert(be);
                                context.addIvarsParams(p.params);
                                possibleType = context.crate.types.path(p.clone(), HIRTypePathBinding(be));
                                break;
                            }
                            case HIRPatternPathBinding::TAG_Enum: {
                                auto& be = e.binding.as_Enum();
                                auto& p = e.path.data.as_Generic();
                                assert(be.ptr);
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

            const std::optional<HIRTypeRef>& getPossibleType(Context& context, HIRPattern& pattern) const {
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

            static bool directlyMatches(const HIRPattern& pattern, const HIRTypeData* type) {
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

            bool revisitInnerReal(Context& context, HIRPattern& pattern, const HIRTypeData* type, HIRPatternBinding::Type bindingMode, bool isFallback) const {
                HIRTypeRef normalizedType;
                type = context.expandAssociatedTypes(sp, context.getType(type), normalizedType);

                for (auto& pb : pattern.bindings) {
                    if (bindingMode != HIRPatternBinding::Type::Move && context.crate.edition >= ASTEdition::Rust2024 && !context.crate.featureEnabled("mut_ref") && (pb.isMutable || pb.type != HIRPatternBinding::Type::Move)) {
                        ERROR(sp, E0000, "cannot bind `" << pb.name << "` with `" << (pb.type != HIRPatternBinding::Type::Move ? "ref" : "mut") << "` within an implicitly-borrowing pattern");
                    }
                    if (pb.type == HIRPatternBinding::Type::Move && (!pb.isMutable || context.crate.edition >= ASTEdition::Rust2024)) {
                        pb.type = bindingMode;
                    }
                    HIRTypeRef tmp;
                    const HIRTypeData* bindingType = nullptr;
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
                            TODO(sp, "Assign variable type using mode " << (int)bindingMode << " and " << type);
                    }
                    assert(bindingType);
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
                        ERROR(sp, E0000, "cannot explicitly dereference within an implicitly-borrowing pattern - " << pattern);
                    }

                    if (bindingMode != HIRPatternBinding::Type::Move && context.crate.edition >= ASTEdition::Rust2024 && context.crate.featureEnabled("ref_pat_eat_one_layer_2024")) {
                        if (pe->type == HIRBorrowType::Unique && bindingMode != HIRPatternBinding::Type::MutRef) {
                            ERROR(sp, E0000, "cannot match an inherited shared reference with an `&mut` pattern");
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
                    ASSERT_BUG(sp, valueType, "No type for named value pattern " << pattern);
                    pattern.implicitDerefCount = 0;
                    context.equateTypes(sp, type, context.getType(*valueType));
                    return true;
                }

                unsigned nDeref = 0;
                HIRBorrowType bt = HIRBorrowType::Owned;
                const auto* ty = context.revealOpaqueType(type);
                while (const auto* te = ty->opt_Borrow()) {
                    bt = std::min(bt, te->type);
                    ty = context.revealOpaqueType(te->inner);
                    nDeref++;
                }
                if (ty->is_Infer() || ((*ty).is_Path() && ((*ty).as_Path().binding.is_Unbound()))) {
                    // TODO: Don't do fallback if the ivar is marked as being hard blocked
                    if (const auto* te = ty->opt_Infer()) {
                        if (te->index < context.possibleIvarVals.size() && context.possibleIvarVals[te->index].forceDisable) {
                            MatchErgonomicsRevisit::disablePossibilitiesOnBindings(sp, context, pattern);
                            return false;
                        }
                    }

                    const auto& possibleType = getPossibleType(context, pattern);
                    if (possibleType) {
                        const HIRTypeData* possibleTypeP = *possibleType;
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
                                context.equateTypes(sp, ty, possibleType);
                            } else if (const auto* te = ty->opt_Infer()) {
                                if (const auto* te2 = possibleType->opt_Array()) {
                                    if (isIrrefutable) {
                                        context.possibleEquateIvar(sp, te->index, possibleType, Context::PossibleTypeSource::UnsizeTo);
                                    } else {
                                        auto t = context.crate.types.slice(te2->inner);
                                        context.possibleEquateIvar(sp, te->index, t, Context::PossibleTypeSource::UnsizeTo);
                                    }
                                } else {
                                    context.possibleEquateIvar(sp, te->index, possibleType, Context::PossibleTypeSource::UnsizeTo);
                                }
                            } else {
                            }
                        }
                    }

                    MatchErgonomicsRevisit::disablePossibilitiesOnBindings(sp, context, pattern, /*is_top_level=*/true);
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
                    HIRTypeRef target;
                    if (const auto* inner = context.resolve.typeIsOwnedBox(sp, ty)) {
                        derefKind = HIRPattern::DerefKind::Box;
                        target = inner;
                    } else {
                        std::optional<HIRTypeRef> implType;
                        const auto result = context.resolve.autoderefStep(sp, ty, target, &implType);
                        if (result == TraitResolution::AutoderefResult::Ambiguous) {
                            return false;
                        }
                        if (result == TraitResolution::AutoderefResult::NoMatch || !implType) {
                            ERROR(sp, E0000, "Pattern " << pattern << " cannot match " << ty);
                        }
                        context.equateTypes(sp, ty, *implType);
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
                        BUG(sp, "Match ergonomics - `&` pattern already handled");
                        break;
                    }
                    case HIRPatternData::TAG_Or: {
                        BUG(sp, "Match ergonomics - `|` pattern already handled");
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
                                ASSERT_BUG(sp, pattern.implicitDerefCount >= 1, "");
                                pattern.implicitDerefCount -= 1;
                            }
                        } else if (pe.val.is_ByteString()) {
                            const auto& bytes = pe.val.as_ByteString().v;
                            if (const auto* array = ty->opt_Array()) {
                                context.equateTypes(sp, array->inner, context.crate.types.primitive(HIRCoreType::U8));
                                if (array->size.is_Known() && array->size.as_Known() != bytes.size()) {
                                    ERROR(sp, E0000, "Byte string pattern has length " << bytes.size() << ", but is matching " << ty);
                                }
                            } else if (const auto* slice = ty->opt_Slice()) {
                                context.equateTypes(sp, slice->inner, context.crate.types.primitive(HIRCoreType::U8));
                            } else {
                                ASSERT_BUG(sp, pattern.implicitDerefCount >= 1, "");
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
                            TODO(sp, "Match ergonomics - box pattern - Non Box<T> type: " << ty);
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

                        HIRTypeRef target;
                        std::optional<HIRTypeRef> implType;
                        const auto result = context.resolve.autoderefStep(sp, ty, target, &implType);
                        if (result == TraitResolution::AutoderefResult::Ambiguous) {
                            return false;
                        }
                        if (result == TraitResolution::AutoderefResult::NoMatch || !implType) {
                            ERROR(sp, E0000, "Type " << ty << " cannot be used in a deref pattern");
                        }
                        context.equateTypes(sp, ty, *implType);
                        context.equateTypesAssoc(sp, target, context.crate.getLangItemPath(sp, "deref"), {}, ty, "Target", {}, true, TypeckPrimitiveOperator::Deref);
                        context.addTraitBound(sp, ty, context.crate.getLangItemPath(sp, "deref_pure"), {});

                        const bool unique = bindingMode == HIRPatternBinding::Type::MutRef || hasMutableBinding(*pe.sub);
                        if (unique) {
                            context.addTraitBound(sp, ty, context.crate.getLangItemPath(sp, "deref_mut"), {});
                        }
                        pe.kind = unique ? HIRPattern::DerefKind::Unique : HIRPattern::DerefKind::Shared;
                        pe.targetType = target;
                        rv = this->revisitInner(context, *pe.sub, target, bindingMode);
                        break;
                    }
                    case HIRPatternData::TAG_Tuple: {
                        auto& e = pattern.data.as_Tuple();
                        if (!ty->is_Tuple()) {
                            ERROR(sp, E0000, "Matching a non-tuple with a tuple pattern - " << ty);
                        }
                        const auto& te = ty->as_Tuple();
                        if (e.subPatterns.size() != te.size()) {
                            ERROR(sp, E0000, "Tuple pattern with an incorrect number of fields, expected " << e.subPatterns.size() << "-tuple, got " << ty);
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
                            ERROR(sp, E0000, "Matching a non-tuple with a tuple pattern - " << ty);
                        }
                        const auto& te = ty->as_Tuple();
                        if (pe.leading.size() + pe.trailing.size() > te.size()) {
                            ERROR(sp, E0000, "Split-tuple pattern with an incorrect number of fields, expected at most " << (pe.leading.size() + pe.trailing.size()) << "-tuple, got " << te.size());
                        }
                        pe.totalSize = te.size();
                        rv = true;
                        for (size_t i = 0; i < pe.leading.size(); i++) {
                            rv &= this->revisitInner(context, pe.leading[i], te[i], bindingMode);
                        }
                        for (size_t i = 0; i < pe.trailing.size(); i++) {
                            rv &= this->revisitInner(context, pe.trailing[i], te[te.size() - pe.trailing.size() + i], bindingMode);
                        }
                        break;
                    }
                    case HIRPatternData::TAG_Slice: {
                        auto& e = pattern.data.as_Slice();
                        const HIRTypeData* sliceInner;
                        if (const auto* te = ty->opt_Slice()) {
                            sliceInner = te->inner;
                        } else if (const auto* te = ty->opt_Array()) {
                            sliceInner = te->inner;
                            context.equateTypes(sp, ty, context.crate.types.array(sliceInner, e.subPatterns.size()));
                        } else {
                            ERROR(sp, E0000, "Matching a non-array/slice with a slice pattern - " << ty);
                        }
                        rv = true;
                        for (auto& sub : e.subPatterns) {
                            rv |= this->revisitInner(context, sub, sliceInner, bindingMode);
                        }
                        break;
                    }
                    case HIRPatternData::TAG_SplitSlice: {
                        auto& pe = pattern.data.as_SplitSlice();
                        const HIRTypeData* sliceInner;
                        if (const auto* te = ty->opt_Slice()) {
                            sliceInner = te->inner;
                        } else if (const auto* te = ty->opt_Array()) {
                            sliceInner = te->inner;
                        } else {
                            ERROR(sp, E0000, "Matching a non-array/slice with a slice pattern - " << ty);
                        }
                        rv = true;
                        for (auto& sub : pe.leading) {
                            rv |= this->revisitInner(context, sub, sliceInner, bindingMode);
                        }
                        if (pe.extraBind.isValid()) {
                            HIRTypeRef bindingTyInner = context.crate.types.slice(sliceInner);
                            // TODO: Do arrays get bound as arrays?
                            if (ty->is_Array()) {
                                size_t sizeSub = pe.leading.size() + pe.trailing.size();
                                bindingTyInner = context.crate.types.array(sliceInner, ty->as_Array().size.as_Known() - sizeSub);
                                //TODO(sp, "SplitSlice extra bind with array: " << pe.extra_bind << " on " << ty);
                            }
                            HIRTypeRef bindingTy;
                            if (pe.extraBind.type == HIRPatternBinding::Type::Move) {
                                pe.extraBind.type = bindingMode;
                            }
                            switch (pe.extraBind.type) {
                                case HIRPatternBinding::Type::Move:
                                    ASSERT_BUG(sp, ty->is_Array(), "Non-array SplitSlize move bind");
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
                        ASSERT_BUG(sp, possibleType, "No type for path pattern " << pattern);
                        context.equateTypes(sp, ty, *possibleType);

                        switch (e.binding.tag()) {
                            case HIRPatternPathBinding::TAG_Unbound: {
                                UNREACHABLE();
                            }
                            case HIRPatternPathBinding::TAG_Struct: {
                                auto& be = e.binding.as_Struct();
                                const auto& str = *be;
                                ASSERT_BUG(sp, str.data.is_Unit(), "PathValue used on non-unit struct variant");
                                break;
                            }
                            case HIRPatternPathBinding::TAG_Union: {
                                BUG(sp, "PathValue used for union");
                                break;
                            }
                            case HIRPatternPathBinding::TAG_Enum: {
                                auto& be = e.binding.as_Enum();
                                const auto& enm = *be.ptr;
                                if (const auto* ee = enm.data.opt_Data()) {
                                    ASSERT_BUG(sp, be.varIdx < ee->size(), "");
                                    const auto& var = (*ee)[be.varIdx];
                                    ASSERT_BUG(sp, var.type == context.crate.types.unit(), "EnumValue used on non-value enum variant");
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
                        ASSERT_BUG(sp, possibleType, "No type for tuple path pattern " << pattern);
                        context.equateTypes(sp, ty, *possibleType);

                        const auto& sd = patternGetTuple(sp, e.path, e.binding);

                        auto ms = MonomorphStatePtr(context.crate.types, nullptr, &e.path.data.as_Generic().params, nullptr);
                        HIRTypeRef tmp;
                        auto maybeMonomorph = [&](const HIRTypeData* fieldType) -> const HIRTypeData* {
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
                        ASSERT_BUG(sp, possibleType, "No type for named path pattern " << pattern);
                        context.equateTypes(sp, ty, *possibleType);

                        if (e.subPatterns.empty()) {
                            // TODO: Check the field count?
                            rv = true;
                        } else {
                            const auto& sd = patternGetNamed(sp, e.path, e.binding);

                            auto ms = MonomorphStatePtr(context.crate.types, nullptr, &e.path.data.as_Generic().params, nullptr);
                            HIRTypeRef tmp;
                            auto maybeMonomorph = [&](const HIRTypeData* fieldType) -> const HIRTypeData* {
                                return (monomorphiseTypeNeeded(fieldType) ? (tmp = context.expandAssociatedTypes(sp, ms.monomorphType(sp, fieldType))) : fieldType);
                            };

                            rv = true;
                            for (auto& fieldPat : e.subPatterns) {
                                unsigned int fIdx = std::find_if(sd.begin(), sd.end(), [&](const HIRStructField& x) {
                                    return x.name == fieldPat.first;
                                }) - sd.begin();
                                if (fIdx == sd.size()) {
                                    ERROR(sp, E0000, "Struct " << e.path << " doesn't have a field " << fieldPat.first);
                                }
                                const HIRTypeData* fieldType = maybeMonomorph(sd[fIdx].ty);
                                rv &= this->revisitInner(context, fieldPat.second, fieldType, bindingMode);
                            }
                        }
                        break;
                    }
                }
                return rv;
            }

            static void disablePossibilitiesOnBindings(const Span& sp, Context& context, const HIRPattern& pat, bool isTopLevel = false) {
                if (!isTopLevel) {
                    for (const auto& pb : pat.bindings) {
                        context.possibleEquateTypeUnknown(sp, context.getVar(sp, pb.slot), Context::IvarUnknownType::Bound);
                    }
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
                        disablePossibilitiesOnBindings(sp, context, *e.sub);
                        break;
                    }
                    case HIRPatternData::TAG_Deref: {
                        auto& e = pat.data.as_Deref();
                        disablePossibilitiesOnBindings(sp, context, *e.sub);
                        break;
                    }
                    case HIRPatternData::TAG_Ref: {
                        auto& e = pat.data.as_Ref();
                        disablePossibilitiesOnBindings(sp, context, *e.sub);
                        break;
                    }
                    case HIRPatternData::TAG_Tuple: {
                        auto& e = pat.data.as_Tuple();
                        for (auto& subpat : e.subPatterns) {
                            disablePossibilitiesOnBindings(sp, context, subpat);
                        }
                        break;
                    }
                    case HIRPatternData::TAG_SplitTuple: {
                        auto& e = pat.data.as_SplitTuple();
                        for (auto& subpat : e.leading) {
                            disablePossibilitiesOnBindings(sp, context, subpat);
                        }
                        for (auto& subpat : e.trailing) {
                            disablePossibilitiesOnBindings(sp, context, subpat);
                        }
                        break;
                    }
                    case HIRPatternData::TAG_Slice: {
                        auto& e = pat.data.as_Slice();
                        for (auto& sub : e.subPatterns) {
                            disablePossibilitiesOnBindings(sp, context, sub);
                        }
                        break;
                    }
                    case HIRPatternData::TAG_SplitSlice: {
                        auto& e = pat.data.as_SplitSlice();
                        for (auto& sub : e.leading) {
                            disablePossibilitiesOnBindings(sp, context, sub);
                        }
                        if (e.extraBind.isValid()) {
                            context.possibleEquateTypeUnknown(sp, context.getVar(sp, e.extraBind.slot), Context::IvarUnknownType::Bound);
                        }
                        for (auto& sub : e.trailing) {
                            disablePossibilitiesOnBindings(sp, context, sub);
                        }
                        break;
                    }
                    case HIRPatternData::TAG_PathValue: {
                        break;
                    }
                    case HIRPatternData::TAG_PathTuple: {
                        auto& e = pat.data.as_PathTuple();
                        for (auto& subpat : e.leading) {
                            disablePossibilitiesOnBindings(sp, context, subpat);
                        }
                        for (auto& subpat : e.trailing) {
                            disablePossibilitiesOnBindings(sp, context, subpat);
                        }
                        break;
                    }
                    case HIRPatternData::TAG_PathNamed: {
                        auto& e = pat.data.as_PathNamed();
                        for (auto& fieldPat : e.subPatterns) {
                            disablePossibilitiesOnBindings(sp, context, fieldPat.second);
                        }
                        break;
                    }
                    case HIRPatternData::TAG_Or: {
                        auto& e = pat.data.as_Or();
                        for (auto& subpat : e) {
                            disablePossibilitiesOnBindings(sp, context, subpat);
                        }
                        break;
                    }
                }
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
                        assert(e.size() > 0);
                        createBindings(sp, context, e[0]);
                        // TODO: Ensure that the other arms have the same binding set
                        break;
                    }
                }
            }
        };

        MatchErgonomicsRevisit::createBindings(sp, *this, pat);
        auto revisit = box$((MatchErgonomicsRevisit{sp, isIrrefutable, type, pat}));
        if (!revisit->revisit(*this, false)) {
            this->addRevisitAdv(mv$(revisit));
        }
        return;
    }

    this->handlePatternDirectInner(sp, pat, type);
}

void Context::handlePatternDirectInner(const Span& sp, HIRPattern& pat, const HIRTypeData* type) {
    for (const auto& pb : pat.bindings) {
        this->addBindingInner(sp, pb, type);
    }

    struct H {
        static void handleValue(Context& context, const Span& sp, const HIRTypeData* type, HIRPattern::Value& val) {
            switch (val.tag()) {
                case HIRPattern::Value::TAG_Integer: {
                    auto& v = val.as_Integer();
                    // TODO: Apply an ivar bound? (Require that this ivar be an integer?)
                    if (v.type != HIRCoreType::Str) {
                        context.equateTypes(sp, type, context.crate.types.primitive(v.type));
                    }
                    break;
                }
                case HIRPattern::Value::TAG_Float: {
                    auto& v = val.as_Float();
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

        static HIRTypeRef getPathType(Context& context, const Span& sp, HIRPath& path, const HIRPattern::PathBinding& binding) {
            switch (binding.tag()) {
                case HIRPatternPathBinding::TAG_Unbound: {
                    auto& _ = binding.as_Unbound();
                    BUG(sp, "");
                    break;
                }
                case HIRPatternPathBinding::TAG_Struct: {
                    auto& be = binding.as_Struct();
                    auto& p = path.data.as_Generic();
                    assert(be);
                    context.addIvarsParams(p.params);
                    return context.crate.types.path(p.clone(), HIRTypePathBinding(be));
                }
                case HIRPatternPathBinding::TAG_Union: {
                    auto& be = binding.as_Union();
                    auto& p = path.data.as_Generic();
                    assert(be);
                    context.addIvarsParams(p.params);
                    return context.crate.types.path(p.clone(), HIRTypePathBinding(be));
                }
                case HIRPatternPathBinding::TAG_Enum: {
                    auto& be = binding.as_Enum();
                    auto& p = path.data.as_Generic();
                    assert(be.ptr);
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
                ERROR(sp, E0000, "Use of `box` pattern without the `owned_box` lang item");
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
            HIRTypeRef target;
            std::optional<HIRTypeRef> implType;
            const auto result = resolve.autoderefStep(sp, ty, target, &implType);
            if (result != TraitResolution::AutoderefResult::Match || !implType) {
                ERROR(sp, E0000, "Type " << ty << " cannot be used in a deref pattern");
            }
            equateTypes(sp, ty, *implType);
            equateTypesAssoc(sp, target, crate.getLangItemPath(sp, "deref"), {}, ty, "Target", {}, true, TypeckPrimitiveOperator::Deref);
            addTraitBound(sp, ty, crate.getLangItemPath(sp, "deref_pure"), {});
            e.kind = HIRPattern::DerefKind::Shared;
            e.targetType = target;
            this->handlePatternDirectInner(sp, *e.sub, target);
            break;
        }
        case HIRPatternData::TAG_Ref: {
            auto& e = pat.data.as_Ref();
            const auto& ty = this->getType(type);
            if (const auto* te = ty->opt_Borrow()) {
                if (te->type != e.type) {
                    ERROR(sp, E0000, "Pattern-type mismatch, &-ptr mutability mismatch");
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
                if (e.subPatterns.size() != te.size()) {
                    ERROR(sp, E0000, "Tuple pattern with an incorrect number of fields, expected " << e.subPatterns.size() << "-tuple, got " << ty);
                }

                for (unsigned int i = 0; i < e.subPatterns.size(); i++) {
                    this->handlePatternDirectInner(sp, e.subPatterns[i], te[i]);
                }
            } else {
                std::vector<HIRTypeRef> subTypes;
                for (unsigned int i = 0; i < e.subPatterns.size(); i++) {
                    subTypes.push_back(this->ivars.newIvarTr());
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
                ASSERT_BUG(sp, e.leading.size() + e.trailing.size() <= te.size(), "Invalid field count for split tuple pattern");

                unsigned int tupIdx = 0;
                for (auto& subpat : e.leading) {
                    this->handlePatternDirectInner(sp, subpat, te[tupIdx++]);
                }
                tupIdx = te.size() - e.trailing.size();
                for (auto& subpat : e.trailing) {
                    this->handlePatternDirectInner(sp, subpat, te[tupIdx++]);
                }

                // TODO: Should this replace the pattern with a non-split?

                e.totalSize = te.size();
            } else {
                if (!ty->is_Infer()) {
                    ERROR(sp, E0000, "Tuple pattern on non-tuple");
                }

                std::vector<HIRTypeRef> leadingTys;
                leadingTys.reserve(e.leading.size());
                for (auto& subpat : e.leading) {
                    leadingTys.push_back(this->ivars.newIvarTr());
                    this->handlePatternDirectInner(sp, subpat, leadingTys.back());
                }
                std::vector<HIRTypeRef> trailingTys;
                for (auto& subpat : e.trailing) {
                    trailingTys.push_back(this->ivars.newIvarTr());
                    this->handlePatternDirectInner(sp, subpat, trailingTys.back());
                }

                struct SplitTuplePatRevisit: public Revisitor {
                    Span sp;
                    HIRTypeRef outerTy;
                    std::vector<HIRTypeRef> leadingTys;
                    std::vector<HIRTypeRef> trailingTys;
                    unsigned int& patTotalSize;

                    SplitTuplePatRevisit(Span sp, HIRTypeRef outer, std::vector<HIRTypeRef> leading, std::vector<HIRTypeRef> trailing, unsigned int& patTotalSize)
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

                    void fmt(std::ostream& os) const override {
                        os << "SplitTuplePatRevisit { " << outerTy << " = (" << leadingTys << ", ..., " << trailingTys << ") }";
                    }

                    bool revisit(Context& context, bool isFallback) override {
                        const auto& ty = context.getType(outerTy);
                        if (ty->is_Infer()) {
                            return false;
                        } else if (const auto* tep = ty->opt_Tuple()) {
                            const auto& te = *tep;
                            if (te.size() < leadingTys.size() + trailingTys.size()) {
                                ERROR(sp, E0000, "Tuple pattern too large for tuple");
                            }
                            for (unsigned int i = 0; i < leadingTys.size(); i++) {
                                context.equateTypes(sp, te[i], leadingTys[i]);
                            }
                            unsigned int ofs = te.size() - trailingTys.size();
                            for (unsigned int i = 0; i < trailingTys.size(); i++) {
                                context.equateTypes(sp, te[ofs + i], trailingTys[i]);
                            }
                            patTotalSize = te.size();
                            return true;
                        } else {
                            ERROR(sp, E0000, "Tuple pattern on non-tuple - " << ty);
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
                    ERROR(sp, E0000, "Slice pattern on non-array/-slice - " << ty);
                case HIRTypeData::TAG_Slice: {
                    auto& te = (*ty).as_Slice();
                    for (auto& sub : e.subPatterns) {
                        this->handlePatternDirectInner(sp, sub, te.inner);
                    }
                    break;
                }
                case HIRTypeData::TAG_Array: {
                    auto& te = (*ty).as_Array();
                    for (auto& sub : e.subPatterns) {
                        this->handlePatternDirectInner(sp, sub, te.inner);
                    }
                    break;
                }
                case HIRTypeData::TAG_Infer: {
                    auto inner = this->ivars.newIvarTr();
                    for (auto& sub : e.subPatterns) {
                        this->handlePatternDirectInner(sp, sub, inner);
                    }

                    struct SlicePatRevisit: public Revisitor {
                        Span sp;
                        HIRTypeRef inner;
                        HIRTypeRef type;
                        unsigned int size;

                        SlicePatRevisit(Span sp, HIRTypeRef inner, HIRTypeRef type, unsigned int size)
                            : sp(mv$(sp))
                            , inner(mv$(inner))
                            , type(mv$(type))
                            , size(size)
                        {
                        }

                        const Span& span() const override {
                            return sp;
                        }

                        void fmt(std::ostream& os) const override {
                            os << "SlicePatRevisit { " << inner << ", " << type << ", " << size;
                        }

                        bool revisit(Context& context, bool isFallback) override {
                            const auto& ty = context.getType(type);
                            switch ((*ty).tag()) {
                                default:
                                    ERROR(sp, E0000, "Slice pattern on non-array/-slice - " << ty);
                                case HIRTypeData::TAG_Infer: {
                                    return false;
                                }
                                case HIRTypeData::TAG_Slice: {
                                    auto& te = (*ty).as_Slice();
                                    context.equateTypes(sp, te.inner, inner);
                                    return true;
                                }
                                case HIRTypeData::TAG_Array: {
                                    auto& te = (*ty).as_Array();
                                    if (te.size.as_Known() != size) {
                                        ERROR(sp, E0000, "Slice pattern on an array if differing size");
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
            HIRTypeRef inner;
            unsigned int minLen = e.leading.size() + e.trailing.size();
            const auto& ty = this->getType(type);
            switch ((*ty).tag()) {
                default:
                    ERROR(sp, E0000, "SplitSlice pattern on non-array/-slice - " << ty);
                case HIRTypeData::TAG_Slice: {
                    auto& te = (*ty).as_Slice();

                    // - TODO: Better new variable handling.
                    inner = te.inner;
                    if (e.extraBind.isValid()) {
                        this->addBindingInner(sp, e.extraBind, ty);
                    }
                    break;
                }
                case HIRTypeData::TAG_Array: {
                    auto& te = (*ty).as_Array();
                    inner = te.inner;
                    if (te.size.as_Known() < minLen) {
                        ERROR(sp, E0000, "Slice pattern on an array smaller than the pattern");
                    }
                    unsigned extra_len = te.size.as_Known() - minLen;

                    if (e.extraBind.isValid()) {
                        this->addBindingInner(sp, e.extraBind, crate.types.array(inner, extra_len));
                    }
                    break;
                }
                case HIRTypeData::TAG_Infer: {
                    inner = this->ivars.newIvarTr();
                    HIRTypeRef varTy;
                    if (e.extraBind.isValid()) {
                        varTy = this->ivars.newIvarTr();
                        this->addBindingInner(sp, e.extraBind, varTy);
                    }

                    struct SplitSlicePatRevisit: public Revisitor {
                        Span sp;
                        HIRTypeRef inner;
                        HIRTypeRef type;
                        HIRTypeRef varTy;
                        unsigned int minSize;

                        SplitSlicePatRevisit(Span sp, HIRTypeRef inner, HIRTypeRef type, HIRTypeRef varTy, unsigned int size)
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

                        void fmt(std::ostream& os) const override {
                            os << "SplitSlice inner=" << inner << ", outer=" << type << ", binding=" << varTy << ", " << minSize;
                        }

                        bool revisit(Context& context, bool isFallback) override {
                            const auto& ty = context.getType(this->type);
                            switch ((*ty).tag()) {
                                default:
                                    ERROR(sp, E0000, "Slice pattern on non-array/-slice - " << ty);
                                case HIRTypeData::TAG_Infer: {
                                    return false;
                                }
                                case HIRTypeData::TAG_Slice: {
                                    auto& te = (*ty).as_Slice();
                                    context.equateTypes(this->sp, this->inner, te.inner);
                                    if (this->varTy != HIRTypeRef()) {
                                        context.equateTypes(this->sp, this->varTy, ty);
                                    }
                                    break;
                                }
                                case HIRTypeData::TAG_Array: {
                                    auto& te = (*ty).as_Array();
                                    context.equateTypes(this->sp, this->inner, te.inner);
                                    if (te.size.as_Known() < this->minSize) {
                                        ERROR(sp, E0000, "Slice pattern on an array smaller than the pattern");
                                    }
                                    unsigned extra_len = te.size.as_Known() - this->minSize;

                                    if (this->varTy != HIRTypeRef()) {
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
                    BUG(sp, "");
                    break;
                }
                case HIRPatternPathBinding::TAG_Struct: {
                    assert(e.binding.as_Struct()->data.is_Unit());
                    break;
                }
                case HIRPatternPathBinding::TAG_Union: {
                    BUG(sp, "PathValue used for union");
                    break;
                }
                case HIRPatternPathBinding::TAG_Enum: {
                    auto& be = e.binding.as_Enum();
                    if (const auto* ee = be.ptr->data.opt_Data()) {
                        ASSERT_BUG(sp, be.varIdx < ee->size(), "");
                        const auto& var = (*ee)[be.varIdx];
                        if (var.type->is_Tuple() && var.type->as_Tuple().size() == 0) {
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
            HIRTypeRef tmp;
            auto maybeMonomorph = [&](const HIRTypeData* ty) -> const HIRTypeData* {
                if (monomorphiseTypeNeeded(ty)) {
                    return (tmp = ms.monomorphType(sp, ty));
                } else {
                    return ty;
                }
            };
            if (e.isSplit) {
                ASSERT_BUG(sp, e.leading.size() + e.trailing.size() <= sd.size(), "PathTuple size mismatch, expected at most " << sd.size() << " fields but got " << e.leading.size() + e.trailing.size());
            } else {
                ASSERT_BUG(sp, e.leading.size() == sd.size(), "PathTuple size mismatch, expected " << sd.size() << " fields but got " << e.leading.size());
                assert(e.trailing.size() == 0);
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
                    ERROR(sp, E0000, "Struct " << e.path << " doesn't have a field " << fieldPat.first);
                }
                const HIRTypeData* fieldType = sd[fIdx].ty;
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

void Context::recordCoercionHint(const HIRTypeData* type, HIRExprNodeP& nodePtr) {
    auto* hintNode = nodePtr.get();
    while (const auto* block = cast<const HIRExprNodeBlock>(hintNode)) {
        if (!block->valueNode) {
            break;
        }
        hintNode = block->valueNode.get();
    }
    if (hintNode) {
        this->coercionHints[hintNode] = type;
    }
}

void Context::equateTypesCoerce(const Span& sp, const HIRTypeData* l, HIRExprNodeP& nodePtr) {
    this->ivars.getType(l);
    this->recordCoercionHint(l, nodePtr);
    this->linkCoerce.push_back(std::make_unique<Coercion>(Coercion{this->nextRuleIdx++, l, &nodePtr}));
    this->ivars.markChange();
}

void Context::possibleEquateTypeUnknown(const Span& sp, const HIRTypeData* ty, Context::IvarUnknownType src) {
    {
        auto& tuMatch = (*this->getType(ty));
        switch (tuMatch.tag()) {
            default:
                // TODO: Shadow sub-types too
                break;
            case HIRTypeData::TAG_Path: {
                auto& e = tuMatch.as_Path();
                switch (e.path.data.tag()) {
                    default:
                        // TODO: Ufcs?
                        break;
                    case HIRPathData::TAG_Generic: {
                        auto& pe = e.path.data.as_Generic();
                        for (const auto& sty : pe.params.types) {
                            this->possibleEquateTypeUnknown(sp, sty, src);
                        }
                        break;
                    }
                }
                break;
            }
            case HIRTypeData::TAG_Tuple: {
                auto& e = tuMatch.as_Tuple();
                for (const auto& sty : e) {
                    this->possibleEquateTypeUnknown(sp, sty, src);
                }
                break;
            }
            case HIRTypeData::TAG_Borrow: {
                auto& e = tuMatch.as_Borrow();
                this->possibleEquateTypeUnknown(sp, e.inner, src);
                break;
            }
            case HIRTypeData::TAG_Array: {
                auto& e = tuMatch.as_Array();
                this->possibleEquateTypeUnknown(sp, e.inner, src);
                break;
            }
            case HIRTypeData::TAG_Slice: {
                auto& e = tuMatch.as_Slice();
                this->possibleEquateTypeUnknown(sp, e.inner, src);
                break;
            }
            case HIRTypeData::TAG_NodeType: {
                auto& e = tuMatch.as_NodeType();
                if (e.is_Closure()) {
                    auto* nodeP = e.as_Closure();
                    for (const auto& aty : nodeP->args) {
                        this->possibleEquateTypeUnknown(sp, aty.second, src);
                    }
                    this->possibleEquateTypeUnknown(sp, nodeP->returnType, src);
                }
                break;
            }
            case HIRTypeData::TAG_Infer: {
                auto& e = tuMatch.as_Infer();
                this->possibleEquateIvarUnknown(sp, e.index, src);
                break;
            }
        }
    }
}

void Context::equateTypesAssoc(const Span& sp, const HIRTypeData* l, const HIRSimplePath& trait, HIRPathParams pp, const HIRTypeData* implTy, const char* name, const HIRPathParams& atyPp, bool isOp, TypeckPrimitiveOperator operatorKind) {
    const auto& traitDef = crate.getTraitByPath(sp, trait);
    auto monomorph = MonomorphStatePtr(crate.types, implTy, &pp, nullptr);
    while (pp.types.size() < traitDef.params.types.size()) {
        const auto& defaultType = traitDef.params.types[pp.types.size()].defaultValue;
        if (defaultType == HIRTypeRef() || defaultType->is_Infer()) {
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
            if (a.leftTy != ruleLeftTy) {
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

            return;
        }
    }
    visitTyWith(implTy, [&](const HIRTypeData* ty) {
        if (const auto* path = ty->opt_Path()) {
            if (const auto* projection = path->path.data.opt_UfcsKnown()) {
                this->addTraitBound(sp, projection->type, projection->trait.path, projection->trait.params.clone());
            }
        }
        return false;
    });
    this->linkAssoc.push_back(
        Associated{
            this->nextRuleIdx++,
            sp,
            ruleLeftTy,

            trait.clone(),
            mv$(ruleParams),
            ruleImplTy,
            ruleName,
            mv$(ruleAtyPp),
            isOp,
            operatorKind
        }
    );
    this->indexAssociated(this->linkAssoc.size() - 1);
    this->ivars.markChange();
}

u64 Context::associatedIndexKey(HIRTypeRef leftTy, const HIRSimplePath& trait, HIRTypeRef implTy, RcString name, bool isOperator, TypeckPrimitiveOperator operatorKind) {
    return mix(leftTy, implTy, trait.rawData()) ^ (static_cast<u64>(name.rawId()) << 8) ^ (static_cast<u64>(operatorKind) << 1) ^ isOperator;
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
    assert(bucket);
    for (size_t i = 0; i < bucket->length(); i++) {
        if ((*bucket)[i] == index) {
            const auto replacement = bucket->popBack();
            if (i < bucket->length()) {
                bucket->mut(i) = replacement;
            }
            return;
        }
    }
    assert(!"associated type rule is absent from its index");
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
    unindexAssociated(index, oldKey);
    const auto last = static_cast<unsigned>(linkAssoc.size() - 1);
    if (index != last) {
        const auto movedKey = associatedIndexKey(linkAssoc[last]);
        unindexAssociated(last, movedKey);
        linkAssoc[index] = mv$(linkAssoc.back());
        indexAssociated(index);
    }
    linkAssoc.pop_back();
}

void Context::selectWellFormed(const Span& sp, const HIRTypeData* type) {
    visitTyWith(type, [&](const HIRTypeData* inner) {
        const auto* path = inner->opt_Path();
        const auto* projection = path ? path->path.data.opt_UfcsKnown() : nullptr;
        if (!projection) {
            return false;
        }
        resolve.solveTraitGoal(sp, projection->trait.path, projection->trait.params, projection->type, [&](SolverResponse response) {
            if (response.certainty != SolverCertainty::Proven || !response.hasImpl || !response.impl || response.impl->ambiguousIdentity) {
                return false;
            }
            applySolverResponse(sp, response);
            auto impl = response.impl->legacy();
            equateTypes(sp, projection->type, impl.getImplType(crate.types));
            auto responseParams = impl.getTraitParams(crate.types);
            ASSERT_BUG(sp, projection->trait.params.types.size() == responseParams.types.size(), "WF response type parameter count mismatch");
            ASSERT_BUG(sp, projection->trait.params.values.size() == responseParams.values.size(), "WF response const parameter count mismatch");
            for (size_t i = 0; i < responseParams.types.size(); i++) {
                equateTypes(sp, projection->trait.params.types[i], responseParams.types[i]);
            }
            for (size_t i = 0; i < responseParams.values.size(); i++) {
                equateValues(sp, projection->trait.params.values[i], responseParams.values[i]);
            }
            return true;
        }, {.assocName = "", .allowInferInputs = true});
        return false;
    });
}

void Context::addRevisit(HIRExprNode& node) {
    this->toVisit.push_back(&node);
}

void Context::addRevisitAdv(std::unique_ptr<Revisitor> entPtr) {
    this->advRevisits.push_back(mv$(entPtr));
}

void Context::requireSized(const Span& sp, const HIRTypeData* ty_) {
    const auto& ty = ivars.getType(ty_);
    const auto sized = resolve.typeIsSized(sp, ty);
    if (sized == HIRCompare::Unequal) {
        ERROR(sp, E0000, "Unsized type not valid here - " << ty);
    }
    if (const auto* e = ty->opt_Infer()) {
        switch (e->tyClass) {
            case HIRInferClass::Integer:
            case HIRInferClass::Float:
                break;
            default:
                // TODO: Flag for future checking
                ASSERT_BUG(sp, e->index != ~0u, "Unbound ivar " << ty);
                if (e->index >= ivarsSized.size()) {
                    ivarsSized.resize(e->index + 1);
                }
                ivarsSized.at(e->index) = true;
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
                        if (sized != HIRCompare::Equal) {
                            this->requireSized(sp, e->path.data.as_Generic().params.types.at(pb->structMarkings.unsizedParam));
                        }
                        break;
                    case HIRStructMarkings::DstType::Projection: {
                        const HIRTypeData* tailTpl = nullptr;
                        switch (pb->data.tag()) {
                            case HIRStructData::TAG_Unit:
                                BUG(sp, "Potentially-unsized unit struct " << ty);
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
                        if (sized != HIRCompare::Equal) {
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

std::ostream& operator<<(std::ostream& os, const Context::PossibleTypeSource& x) {
    switch (x) {
        case Context::PossibleTypeSource::UnsizeTo:
            os << "UnsizeTo";
            break;
        case Context::PossibleTypeSource::CoerceTo:
            os << "CoerceTo";
            break;
        case Context::PossibleTypeSource::UnsizeFrom:
            os << "UnsizeFrom";
            break;
        case Context::PossibleTypeSource::CoerceFrom:
            os << "CoerceFrom";
            break;
    }
    return os;
}

Context::IVarPossible* Context::getIvarPossibilities(const Span& sp, unsigned int ivarIndex) {
    {
        const auto& realTy = ivars.getType(ivarIndex);
        if (!((*realTy).is_Infer() && ((*realTy).as_Infer().index == ivarIndex))) {
            return nullptr;
        }
    }

    return getPossibleIvarSink(ivarIndex);
}

Context::IVarPossible* Context::getPossibleIvarSink(unsigned index) {
    if (possibleIvarSink) {
        for (auto& captured : *possibleIvarSink) {
            if (captured.index == index) {
                return &captured.possibilities;
            }
        }
        possibleIvarSink->push_back(Associated::CapturedIvarPossible{index, {}});
        return &possibleIvarSink->back().possibilities;
    }

    if (index >= possibleIvarVals.size()) {
        possibleIvarVals.resize(index + 1);
    }
    return &possibleIvarVals[index];
}

void Context::possibleEquateIvar(const Span& sp, unsigned int ivarIndex, const HIRTypeData* rawT, PossibleTypeSource src) {
    const auto& t = this->ivars.getType(rawT);
    auto* entp = getIvarPossibilities(sp, ivarIndex);
    if (!entp) {
        return;
    }
    auto& ent = *entp;

    switch (src) {
        case PossibleTypeSource::UnsizeTo:
            ent.typesCoerceTo.push_back(IVarPossible::CoerceTy(t, false));
            break;
        case PossibleTypeSource::CoerceTo:
            ent.typesCoerceTo.push_back(IVarPossible::CoerceTy(t, true));
            break;
        case PossibleTypeSource::UnsizeFrom:
            ent.typesCoerceFrom.push_back(IVarPossible::CoerceTy(t, false));
            break;
        case PossibleTypeSource::CoerceFrom:
            ent.typesCoerceFrom.push_back(IVarPossible::CoerceTy(t, true));
            break;
    }

    if (!t->is_Infer()) {
        switch (src) {
            case PossibleTypeSource::UnsizeTo:
            case PossibleTypeSource::CoerceTo:
                possibleEquateTypeUnknown(sp, t, IvarUnknownType::To);
                break;
            case PossibleTypeSource::UnsizeFrom:
            case PossibleTypeSource::CoerceFrom:
                possibleEquateTypeUnknown(sp, t, IvarUnknownType::From);
                break;
        }
    }
}

void Context::possibleEquateIvarRawPointerFallback(const Span& sp, unsigned int ivarIndex, const HIRTypeData* rawType) {
    auto* possibilities = getIvarPossibilities(sp, ivarIndex);
    if (!possibilities) {
        return;
    }

    ASSERT_BUG(sp, !typeContainsImplPlaceholder(crate.types, rawType), "Type contained an impl placeholder parameter - " << rawType);
    const auto* type = ivars.getType(rawType);
    if (const auto* infer = type->opt_Infer(); infer && infer->index == ivarIndex) {
        return;
    }
    possibilities->rawPointerFallbacks.insert(type);
}

std::ostream& operator<<(std::ostream& os, const Context::IvarUnknownType& x) {
    switch (x) {
        case Context::IvarUnknownType::To:
            os << "To";
            break;
        case Context::IvarUnknownType::From:
            os << "From";
            break;
        case Context::IvarUnknownType::Bound:
            os << "Bound";
            break;
    }
    return os;
}

void Context::possibleEquateIvarUnknown(const Span& sp, unsigned int ivarIndex, IvarUnknownType src) {
    ASSERT_BUG(sp, ivars.getType(ivarIndex)->is_Infer(), "possible_equate_ivar_unknown on known ivar");

    auto& ent = *getPossibleIvarSink(ivarIndex);
    switch (src) {
        case IvarUnknownType::To:
            ent.forceNoTo = true;
            break;
        case IvarUnknownType::From:
            ent.forceNoFrom = true;
            break;
        case IvarUnknownType::Bound:
            ent.forceDisable = true;
            break;
    }
}

void Context::addVar(const Span& sp, unsigned int index, const RcString& name, HIRTypeRef type) {
    assert(index != ~0u);
    ASSERT_BUG(sp, type != HIRTypeRef(), "Unset ivar in variable type");
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
        ASSERT_BUG(sp, bindings[index].name == name, "");
        this->equateTypes(sp, bindings[index].ty, type);
    }
}

const HIRTypeData* Context::getVar(const Span& sp, unsigned int idx) const {
    if (idx < this->bindings.size()) {
        ASSERT_BUG(sp, this->bindings[idx].ty != HIRTypeRef(), "Local #" << idx << " `" << this->bindings[idx].name << "` with no populated type");
        return this->bindings[idx].ty;
    } else {
        BUG(sp, "get_var - Binding index out of range - " << idx << " >=" << this->bindings.size());
    }
}

HIRExprNodeP Context::createAutoderef(HIRExprNodeP valNode, HIRTypeRef tyDst) const {
    const auto& span = valNode->span();
    const auto& tySrc = valNode->resType;
    if (getType(tySrc)->is_Array()) {
        ASSERT_BUG(span, tyDst->is_Slice(), "Array should only ever autoderef to Slice");

        const auto borrowType = HIRBorrowType::Shared;
        auto tySrcBorrow = crate.types.borrow(borrowType, tySrc);
        auto tyDstBorrow = crate.types.borrow(borrowType, tyDst);
        auto tyDstBorrowCopy = tyDstBorrow;

        valNode = mkExprnodep(crate.pool->make<HIRExprNodeBorrow>(span, borrowType, mv$(valNode)), mv$(tySrcBorrow));
        auto* unsizeNode = crate.pool->make<HIRExprNodeUnsize>(span, mv$(valNode), mv$(tyDstBorrowCopy));
        unsizeNode->isArrayToSliceAdjustment = true;
        valNode = mkExprnodep(unsizeNode, mv$(tyDstBorrow));
        valNode = mkExprnodep(crate.pool->make<HIRExprNodeDeref>(span, mv$(valNode)), tyDst);
    } else {
        valNode = mkExprnodep(crate.pool->make<HIRExprNodeDeref>(span, mv$(valNode)), mv$(tyDst));
    }

    return valNode;
}

namespace {
    template <typename F>
    void addCoerceBorrow(Context& context, HIRExprNodeP& origNodePtr, const HIRTypeData* desBorrowInner, F cb) {
        auto borrowType = context.ivars.getType(origNodePtr->resType)->as_Borrow().type;

        HIRExprNodeP* nodePtrPtr = &origNodePtr;

        ASSERT_BUG(Span(), origNodePtr, "Null node pointer passed to `add_coerce_borrow`");
        while (auto* p = cast<HIRExprNodeBlock>(&**nodePtrPtr)) {
            assert(p->valueNode);
            ASSERT_BUG(p->span(), context.ivars.typesEqual(p->resType, p->valueNode->resType), "Block and result mismatch - " << context.ivars.fmtType(p->resType) << " != " << context.ivars.fmtType(p->valueNode->resType));
            p->resType = context.crate.types.borrow(borrowType, desBorrowInner);
            nodePtrPtr = &p->valueNode;
        }
        auto& nodePtr = *nodePtrPtr;
        const auto& srcType = context.ivars.getType(nodePtr->resType);

        if (auto* p = cast<HIRExprNodeBorrow>(&*nodePtr)) {
            nodePtr->resType = context.crate.types.borrow(borrowType, desBorrowInner);

            nodePtrPtr = &p->value;
        } else {
            auto span = nodePtr->span();
            const auto& srcInnerTy = srcType->as_Borrow().inner;

            auto innerTyRef = context.crate.types.borrow(borrowType, desBorrowInner);

            nodePtr = NEWNODE(srcInnerTy, span, Deref, mv$(nodePtr));
            auto* borrowNode = context.crate.pool->make<HIRExprNodeBorrow>(span, borrowType, mv$(nodePtr));
            nodePtr = mkExprnodep(borrowNode, mv$(innerTyRef));

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
    CoerceResult checkUnsizeTys(const Context& context, const Span& sp, const HIRTypeData* dstRaw, const HIRTypeData* srcRaw, Context* contextMut, HIRExprNodeP* nodePtrPtr = nullptr) {
        const auto& dst = context.ivars.getType(dstRaw);
        const auto& src = context.ivars.getType(srcRaw);

        if (context.ivars.typesEqual(dst, src)) {
            return CoerceResult::Equality;
        }

        if (src->is_Slice()) {
            if (dst->is_Slice() || dst->is_Infer()) {
                return CoerceResult::Equality;
            } else {
                return CoerceResult::Fail;
            }
        }

        if (dst->is_Infer() && src->is_Infer()) {
            if (dst->as_Infer().isLit() && src->as_Infer().isLit()) {
                return CoerceResult::Equality;
            }
            if (contextMut) {
                contextMut->possibleEquateIvar(sp, src->as_Infer().index, dst, Context::PossibleTypeSource::UnsizeTo);
                contextMut->possibleEquateIvar(sp, dst->as_Infer().index, src, Context::PossibleTypeSource::UnsizeFrom);
            }
            return CoerceResult::Unknown;
        } else if (const auto* dep = dst->opt_Infer()) {
            if (dep->isLit() && src->is_Primitive()) {
                return CoerceResult::Equality;
            }
            if (contextMut) {
                contextMut->possibleEquateIvar(sp, dep->index, src, Context::PossibleTypeSource::UnsizeFrom);
                if (src->is_NodeType() && src->as_NodeType().is_Closure()) {
                    contextMut->possibleEquateTypeUnknown(sp, src, Context::IvarUnknownType::To);
                }
            }
            return CoerceResult::Unknown;
        } else if (const auto* sep = src->opt_Infer()) {
            if (sep->isLit()) {
                if (!dst->is_TraitObject()) {
                    return CoerceResult::Equality;
                } else {
                }
            } else {
                if (contextMut) {
                    contextMut->possibleEquateIvar(sp, sep->index, dst, Context::PossibleTypeSource::UnsizeTo);
                }
                return CoerceResult::Unknown;
            }
        } else {
        }

        if (((*src).is_Path() && ((*src).as_Path().binding.is_Unbound()))) {
            return CoerceResult::Unknown;
        }
        if (((*dst).is_Path() && ((*dst).as_Path().binding.is_Unbound()))) {
            return CoerceResult::Unknown;
        }

        if (dst->is_Slice() && src->is_Array()) {
            if (contextMut) {
                contextMut->equateTypes(sp, dst->as_Slice().inner, src->as_Array().inner);
            }
            if (nodePtrPtr) {
                // TODO: Insert deref (instead of leading to a _Unsize op)
            } else {
            }
            return CoerceResult::Unsize;
        }

        if (!dst->is_TraitObject() && !src->is_Generic() && !src->is_Path() && !src->is_Borrow()) {
            return CoerceResult::Equality;
        }

        if (nodePtrPtr) {
            HIRTypeRef tmpTy;
            const HIRTypeData* outTyP = src;
            unsigned int count = 0;
            std::vector<HIRTypeRef> types;
            while ((outTyP = context.resolve.autoderef(sp, outTyP, tmpTy))) {
                const auto& outTy = context.ivars.getType(outTyP);
                count += 1;
                if (count > context.resolve.board().settings->recursionLimit) {
                    ERROR(sp, E0000, "Reached the recursion limit while auto-dereferencing " << src);
                }

                bool literalMatchesDestination = false;
                if (const auto* sep = outTy->opt_Infer()) {
                    if (!sep->isLit()) {
                        if (contextMut) {
                            HIRTypeRef tmpTy2;
                            const HIRTypeData* dTyP = dst;
                            for (unsigned int i = 0; (dTyP = context.resolve.autoderef(sp, dTyP, tmpTy2)) && i < count - 1; i++) {
                            }
                            if (dTyP) {
                                // TODO: This should be a `DerefTo` (can't do other unsizings?)
                                contextMut->possibleEquateIvar(sp, sep->index, dTyP, Context::PossibleTypeSource::UnsizeTo);
                            } else {
                            }
                        }
                        return CoerceResult::Unknown;
                    }
                    const auto* primitive = dst->opt_Primitive();
                    literalMatchesDestination = primitive && ((sep->tyClass == HIRInferClass::Integer && isInteger(*primitive)) || (sep->tyClass == HIRInferClass::Float && isFloat(*primitive)));
                    if (literalMatchesDestination && contextMut) {
                        contextMut->equateTypes(sp, dst, outTy);
                    }
                }

                if (((*outTy).is_Generic() && ((*outTy).as_Generic().isPlaceholder()))) {
                    return CoerceResult::Unknown;
                }

                if (((*outTy).is_Path() && ((*outTy).as_Path().binding.is_Unbound()))) {
                    return CoerceResult::Unknown;
                }

                types.push_back(literalMatchesDestination ? dst : outTy);

                if (!literalMatchesDestination && context.ivars.typesEqual(dst, outTy) == false) {
                    if (dst->tag() != outTy->tag()) {
                        continue;
                    }

                    if (dst->is_Slice()) {
                        if (contextMut) {
                            contextMut->equateTypes(sp, dst, outTy);
                        }
                    } else if (dst->is_Borrow()) {
                        continue;
                    } else {
                        if (dst->compareWithPlaceholders(sp, outTy, context.ivars.callbackResolveInfer()) == HIRCompare::Unequal) {
                            continue;
                        }
                        if (contextMut) {
                            contextMut->equateTypes(sp, dst, outTy);
                        }
                    }
                }

                if (contextMut && nodePtrPtr) {
                    auto& nodePtr = *nodePtrPtr;
                    addCoerceBorrow(*contextMut, nodePtr, types.back(), [&](auto& nodePtr) -> void {
                        assert(count == types.size());
                        for (unsigned int i = 0; i < types.size(); i++) {
                            auto span = nodePtr->span();
                            // TODO: Replace with a call to context.create_autoderef to handle cases where the below assertion would fire.
                            ASSERT_BUG(span, !nodePtr->resType->is_Array(), "Array->Slice shouldn't be in deref coercions");
                            auto ty = mv$(types[i]);
                            nodePtr = HIRExprNodeP(context.crate.pool->make<HIRExprNodeDeref>(mv$(span), mv$(nodePtr)));
                            nodePtr->resType = mv$(ty);
                            context.ivars.getType(nodePtr->resType);
                        }
                    });
                }

                return CoerceResult::Custom;
            }
        }

        if (const auto* dep = dst->opt_TraitObject()) {
            if (const auto* sep = src->opt_TraitObject()) {
                struct ProjectionMatcher: public HIRMatchGenerics {
                    const Context& context;

                    bool isDefiningOpaque(const HIRTypeData* type) const {
                        const auto* erased = type->opt_ErasedType();
                        const auto* alias = erased ? erased->inner.opt_Alias() : nullptr;
                        if (alias && context.resolve.isOpaqueAliasDefiningScope(*alias->inner)) {
                            return true;
                        }
                        const auto* function = erased ? erased->inner.opt_Fcn() : nullptr;
                        return function && context.revealOpaqueType(type)->is_Infer();
                    }

                    explicit ProjectionMatcher(const Context& context)
                        : HIRMatchGenerics(BorrowMatchedValues{})
                        , context(context)
                    {
                    }

                    HIRCompare cmpType(const Span& sp, const HIRTypeData* left, const HIRTypeData* right, tCbResolveType resolve) override {
                        const auto* resolvedLeft = left->is_Infer() ? resolve.getType(sp, left) : left;
                        const auto* resolvedRight = right->is_Infer() ? resolve.getType(sp, right) : right;
                        if (isDefiningOpaque(resolvedLeft) || isDefiningOpaque(resolvedRight)) {
                            return HIRCompare::Fuzzy;
                        }
                        return HIRMatchGenerics::cmpType(sp, left, right, resolve);
                    }

                    HIRCompare matchTy(const HIRGenericRef& generic, const HIRTypeData* type, tCbResolveType resolve) override {
                        const auto* resolved = type->is_Infer() ? resolve.getType(Span(), type) : type;
                        if (isDefiningOpaque(resolved) || resolved->is_Infer() || (resolved->is_Path() && resolved->as_Path().binding.is_Unbound())) {
                            return HIRCompare::Fuzzy;
                        }
                        if (const auto* other = resolved->opt_Generic()) {
                            if (*other == generic) {
                                return HIRCompare::Equal;
                            }
                            if (other->group() == GENERICPlaceholder || generic.group() == GENERICPlaceholder) {
                                return HIRCompare::Fuzzy;
                            }
                        }
                        return generic.group() == GENERICPlaceholder ? HIRCompare::Fuzzy : HIRCompare::Unequal;
                    }

                    HIRCompare matchVal(const HIRGenericRef& generic, const HIRConstGeneric& value) override {
                        if (value.is_Generic() && value.as_Generic() == generic) {
                            return HIRCompare::Equal;
                        }
                        if (value.is_Infer() || generic.group() == GENERICPlaceholder || (value.is_Generic() && value.as_Generic().group() == GENERICPlaceholder)) {
                            return HIRCompare::Fuzzy;
                        }
                        return HIRCompare::Unequal;
                    }
                } matcher{context};

                auto compareType = [&](const HIRTypeData* left, const HIRTypeData* right) {
                    return left->matchTestGenericsFuzz(sp, right, context.ivars.callbackResolveInfer(), matcher);
                };
                auto compareParams = [&](const HIRPathParams& left, const HIRPathParams& right) {
                    if (left.types.size() != right.types.size() || left.values.size() != right.values.size()) {
                        return HIRCompare::Unequal;
                    }
                    auto cmp = HIRCompare::Equal;
                    for (size_t i = 0; i < left.types.size(); i++) {
                        cmp &= compareType(left.types[i], right.types[i]);
                        if (cmp == HIRCompare::Unequal) {
                            return cmp;
                        }
                    }
                    for (size_t i = 0; i < left.values.size(); i++) {
                        const auto& leftValue = context.ivars.getValue(left.values[i]);
                        const auto& rightValue = context.ivars.getValue(right.values[i]);
                        if (leftValue == rightValue) {
                            continue;
                        }
                        if (leftValue.is_Infer() || rightValue.is_Infer() || (leftValue.is_Generic() && leftValue.as_Generic().isPlaceholder()) || (rightValue.is_Generic() && rightValue.as_Generic().isPlaceholder())) {
                            cmp = HIRCompare::Fuzzy;
                        } else {
                            return HIRCompare::Unequal;
                        }
                    }
                    return cmp;
                };
                auto compareProjected = [&](const HIRTraitPath& projected) {
                    auto cmp = compareParams(projected.path.params, dep->trait.path.params);
                    for (const auto& required : dep->trait.typeBounds) {
                        const HIRTraitPath::AtyEqual* source = nullptr;
                        if (const auto it = projected.typeBounds.find(required.first); it != projected.typeBounds.end()) {
                            source = &it->second;
                        } else if (const auto it = sep->trait.typeBounds.find(required.first); it != sep->trait.typeBounds.end() && it->second.sourceTrait.path == required.second.sourceTrait.path) {
                            source = &it->second;
                        }
                        if (!source) {
                            return HIRCompare::Unequal;
                        }
                        cmp &= compareParams(source->sourceTrait.params, required.second.sourceTrait.params);
                        cmp &= compareParams(source->atyParams, required.second.atyParams);
                        cmp &= compareType(source->type, required.second.type);
                    }
                    return cmp;
                };

                const HIRTraitPath* projected = nullptr;
                HIRTraitPath projectedStorage;
                auto projectedCmp = HIRCompare::Unequal;
                bool ambiguousProjection = false;
                if (dep->trait.path.path == HIRSimplePath()) {
                    projected = &dep->trait;
                    projectedCmp = HIRCompare::Equal;
                } else if (dep->trait.path.path == sep->trait.path.path) {
                    projected = &sep->trait;
                    projectedCmp = compareProjected(*projected);
                } else if (sep->trait.traitPtr) {
                    context.resolve.findNamedTraitInTrait(sp, dep->trait.path.path, dep->trait.path.params, *sep->trait.traitPtr, sep->trait.path.path, sep->trait.path.params, src, [&](const HIRTraitPath& parent) {
                        const auto cmp = compareProjected(parent);
                        if (cmp == HIRCompare::Unequal) {
                            return false;
                        }
                        if (!projected || (projectedCmp == HIRCompare::Fuzzy && cmp == HIRCompare::Equal)) {
                            projectedStorage = parent.clone();
                            projected = &projectedStorage;
                            projectedCmp = cmp;
                        } else if (projectedCmp == HIRCompare::Fuzzy && cmp == HIRCompare::Fuzzy && compareParams(projected->path.params, parent.path.params) == HIRCompare::Unequal) {
                            ambiguousProjection = true;
                        }
                        return cmp == HIRCompare::Equal;
                    });
                }
                if (!projected || projectedCmp == HIRCompare::Unequal || ambiguousProjection) {
                    return CoerceResult::Equality;
                }

                if (contextMut) {
                    for (size_t i = 0; i < dep->trait.path.params.types.size(); i++) {
                        contextMut->equateTypes(sp, dep->trait.path.params.types[i], projected->path.params.types[i]);
                    }
                    for (size_t i = 0; i < dep->trait.path.params.values.size(); i++) {
                        contextMut->equateValues(sp, dep->trait.path.params.values[i], projected->path.params.values[i]);
                    }
                    for (const auto& required : dep->trait.typeBounds) {
                        if (const auto it = projected->typeBounds.find(required.first); it != projected->typeBounds.end()) {
                            contextMut->equateTypes(sp, required.second.type, it->second.type);
                        } else {
                            contextMut->equateTypes(sp, required.second.type, sep->trait.typeBounds.at(required.first).type);
                        }
                    }
                }

                for (const auto& mt : dep->markers) {
                    bool found = false;
                    bool ambiguousMarker = false;
                    auto markerCmp = HIRCompare::Unequal;
                    HIRPathParams markerParams;
                    auto considerMarker = [&](const HIRPathParams& params) {
                        const auto cmp = compareParams(params, mt.params);
                        if (cmp == HIRCompare::Unequal) {
                            return false;
                        }
                        if (!found || (markerCmp == HIRCompare::Fuzzy && cmp == HIRCompare::Equal)) {
                            markerParams = params.clone();
                            markerCmp = cmp;
                            found = true;
                            ambiguousMarker = false;
                        } else if (markerCmp == HIRCompare::Fuzzy && cmp == HIRCompare::Fuzzy && compareParams(markerParams, params) == HIRCompare::Unequal) {
                            ambiguousMarker = true;
                        }
                        return cmp == HIRCompare::Equal;
                    };
                    for (const auto& omt : sep->markers) {
                        if (omt.path == mt.path && considerMarker(omt.params)) {
                            break;
                        }
                    }
                    if (markerCmp != HIRCompare::Equal && sep->trait.traitPtr) {
                        context.resolve.findNamedTraitInTrait(sp, mt.path, mt.params, *sep->trait.traitPtr, sep->trait.path.path, sep->trait.path.params, src, [&](const HIRTraitPath& parent) {
                            return considerMarker(parent.path.params);
                        });
                    }
                    if (!found || ambiguousMarker) {
                        return CoerceResult::Equality;
                    }
                    if (contextMut) {
                        for (size_t i = 0; i < mt.params.types.size(); i++) {
                            contextMut->equateTypes(sp, mt.params.types[i], markerParams.types[i]);
                        }
                        for (size_t i = 0; i < mt.params.values.size(); i++) {
                            contextMut->equateValues(sp, mt.params.values[i], markerParams.values[i]);
                        }
                    }
                }

                return CoerceResult::Unsize;
            } else {
                const auto& trait = dep->trait.path;

                if (trait.path != HIRSimplePath()) {
                    if (contextMut) {
                        for (const auto& tyb : dep->trait.typeBounds) {
                            contextMut->equateTypesAssoc(sp, tyb.second.type, trait.path, trait.params.clone(), src, tyb.first.c_str(), tyb.second.atyParams, false);
                        }
                        if (dep->trait.typeBounds.empty()) {
                            contextMut->addTraitBound(sp, src, trait.path, trait.params.clone());
                        }
                    } else {
                        if (!context.resolve.solveTraitGoal(sp, trait.path, trait.params, src, [](SolverResponse response) {
                            return response.hasImpl;
                        })) {
                            return CoerceResult::Equality;
                        }
                    }
                }

                if (contextMut) {
                    for (const auto& marker : dep->markers) {
                        contextMut->addTraitBound(sp, src, marker.path, marker.params.clone());
                    }
                } else {
                    // TODO: Should this check?
                }

                return CoerceResult::Unsize;
            }
        }

        struct H {
            static bool typeIsBounded(const HIRTypeData* ty) {
                if (ty->is_Generic()) {
                    return true;
                } else if (((*ty).is_Path() && ((*ty).as_Path().binding.is_Opaque()))) {
                    return true;
                } else {
                    return false;
                }
            }
        };

        if (H::typeIsBounded(src)) {
            bool selected = false;

            HIRPathParams pp{dst};
            context.resolve.solveTraitGoal(
                sp,
                context.resolve.langUnsize(),
                pp,
                src,
                [&](SolverResponse response) {
                if (!response.hasImpl || !response.impl || response.impl->ambiguousIdentity) {
                    return false;
                }
                selected = true;
                if (contextMut) {
                    contextMut->applySolverResponse(sp, response);
                }
                return true;
            },
                {
                    .allowInferInputs = true,
                }
            );
            if (selected) {
                return CoerceResult::Unsize;
            }
        }

        if (src->tag() == dst->tag()) {
            switch ((*src).tag()) {
                default:
                    return CoerceResult::Equality;
                case HIRTypeData::TAG_Path: {
                    auto& se = (*src).as_Path();
                    auto& de = (*dst).as_Path();
                    if (se.binding.tag() == de.binding.tag()) {
                        switch (se.binding.tag()) {
                            case HIRTypePathBinding::TAG_Unbound: {
                                break;
                            }
                            case HIRTypePathBinding::TAG_Opaque: {
                                break;
                            }
                            case HIRTypePathBinding::TAG_ExternType: {
                                auto& sbe = se.binding.as_ExternType();
                                auto& dbe = de.binding.as_ExternType();
                                if (sbe == dbe) {
                                    return CoerceResult::Equality;
                                }
                                break;
                            }
                            case HIRTypePathBinding::TAG_Enum: {
                                auto& sbe = se.binding.as_Enum();
                                auto& dbe = de.binding.as_Enum();
                                if (sbe == dbe) {
                                    return CoerceResult::Equality;
                                }
                                break;
                            }
                            case HIRTypePathBinding::TAG_Union: {
                                auto& sbe = se.binding.as_Union();
                                auto& dbe = de.binding.as_Union();
                                if (sbe == dbe) {
                                    return CoerceResult::Equality;
                                }
                                break;
                            }
                            case HIRTypePathBinding::TAG_Struct: {
                                auto& sbe = se.binding.as_Struct();
                                auto& dbe = de.binding.as_Struct();
                                if (sbe == dbe) {
                                    const auto& sm = sbe->structMarkings;
                                    if (sm.dstType == HIRStructMarkings::DstType::Possible || sm.dstType == HIRStructMarkings::DstType::Projection) {
                                        const auto& pSrc = se.path.data.as_Generic().params;
                                        const auto& pDst = de.path.data.as_Generic().params;
                                        HIRTypeRef srcTail;
                                        HIRTypeRef dstTail;
                                        const HIRTypeData* isrc;
                                        const HIRTypeData* idst;
                                        if (sm.dstType == HIRStructMarkings::DstType::Possible) {
                                            isrc = pSrc.types.at(sm.unsizedParam);
                                            idst = pDst.types.at(sm.unsizedParam);
                                        } else {
                                            const HIRTypeData* tailTpl = nullptr;
                                            switch (sbe->data.tag()) {
                                                case HIRStructData::TAG_Unit:
                                                    BUG(sp, "Potentially-unsized unit struct " << src);
                                                case HIRStructData::TAG_Tuple:
                                                    tailTpl = sbe->data.as_Tuple().at(sm.unsizedField).ent;
                                                    break;
                                                case HIRStructData::TAG_Named:
                                                    tailTpl = sbe->data.as_Named().at(sm.unsizedField).ty;
                                                    break;
                                            }
                                            srcTail = context.expandAssociatedTypes(sp, MonomorphStatePtr(context.crate.types, src, &pSrc, nullptr).monomorphType(sp, tailTpl));
                                            dstTail = context.expandAssociatedTypes(sp, MonomorphStatePtr(context.crate.types, dst, &pDst, nullptr).monomorphType(sp, tailTpl));
                                            isrc = srcTail;
                                            idst = dstTail;
                                        }
                                        auto rv = checkUnsizeTys(context, sp, idst, isrc, contextMut, nullptr);
                                        switch (rv) {
                                            case CoerceResult::Fail:
                                            case CoerceResult::Unknown:
                                                break;
                                            default:
                                                if (contextMut) {
                                                    if (sm.dstType == HIRStructMarkings::DstType::Possible) {
                                                        for (size_t i = 0; i < pSrc.types.size(); i++) {
                                                            if (i != sm.unsizedParam) {
                                                                contextMut->equateTypes(sp, pDst.types.at(i), pSrc.types[i]);
                                                            }
                                                        }
                                                        for (size_t i = 0; i < pSrc.values.size(); i++) {
                                                            contextMut->equateValues(sp, pDst.values.at(i), pSrc.values[i]);
                                                        }
                                                    } else {
                                                        auto equateField = [&](const HIRTypeData* fieldTpl) {
                                                            auto srcField = context.expandAssociatedTypes(sp, MonomorphStatePtr(context.crate.types, src, &pSrc, nullptr).monomorphType(sp, fieldTpl));
                                                            auto dstField = context.expandAssociatedTypes(sp, MonomorphStatePtr(context.crate.types, dst, &pDst, nullptr).monomorphType(sp, fieldTpl));
                                                            contextMut->equateTypes(sp, dstField, srcField);
                                                        };
                                                        switch (sbe->data.tag()) {
                                                            case HIRStructData::TAG_Unit:
                                                                break;
                                                            case HIRStructData::TAG_Tuple: {
                                                                const auto& fields = sbe->data.as_Tuple();
                                                                for (size_t i = 0; i < fields.size(); i++) {
                                                                    if (i != sm.unsizedField) {
                                                                        equateField(fields[i].ent);
                                                                    }
                                                                }
                                                                break;
                                                            }
                                                            case HIRStructData::TAG_Named: {
                                                                const auto& fields = sbe->data.as_Named();
                                                                for (size_t i = 0; i < fields.size(); i++) {
                                                                    if (i != sm.unsizedField) {
                                                                        equateField(fields[i].ty);
                                                                    }
                                                                }
                                                                break;
                                                            }
                                                        }
                                                    }
                                                }
                                                break;
                                        }
                                        return rv;
                                    } else {
                                        return CoerceResult::Equality;
                                    }
                                }
                                break;
                            }
                        }
                    }
                    break;
                }
            }
        }

        if (((*dst).is_Path() && ((*dst).as_Path().binding.is_Unbound()))) {
            return CoerceResult::Unknown;
        }

        // TODO: Determine if this unsizing could ever happen.
        return CoerceResult::Equality;
    }

    CoerceResult checkCoerceTys(const Context& context, const Span& sp, const HIRTypeData* dst, const HIRTypeData* srcR, Context* contextMut = nullptr, HIRExprNodeP* nodePtrPtr = nullptr) {
        auto src = srcR;
        if (context.ivars.typesEqual(dst, src)) {
            return CoerceResult::Equality;
        }
        if (((*dst).is_Infer() && ((*dst).as_Infer().isLit()))) {
            if (!src->is_Diverge()) {
                return CoerceResult::Equality;
            }
        }
        if (dst->is_Diverge()) {
            return CoerceResult::Equality;
        }
        if (((*src).is_Infer() && ((*src).as_Infer().isLit()))) {
            return CoerceResult::Equality;
        }

        // TODO: If the destination is bounded to be Sized, equate and return.

        if (dst->is_Infer() && src->is_Infer()) {
            if (contextMut) {
                contextMut->possibleEquateIvar(sp, src->as_Infer().index, dst, Context::PossibleTypeSource::CoerceTo);
                contextMut->possibleEquateIvar(sp, dst->as_Infer().index, src, Context::PossibleTypeSource::CoerceFrom);
            }
            return CoerceResult::Unknown;
        }

        struct H {
            static bool typeIsBounded(const HIRTypeData* ty) {
                if (ty->is_Generic()) {
                    return true;
                } else if (ty->is_Path() && (monomorphiseTypeNeeded(ty) || ty->as_Path().binding.is_Opaque())) {
                    return true;
                } else {
                    return false;
                }
            }

            static HIRTypeRef makePruned(Context& context, const HIRTypeData* ty) {
                const auto& binding = ty->as_Path().binding;
                const auto& sm = binding.as_Struct()->structMarkings;
                HIRGenericPath gp = ty->as_Path().path.data.as_Generic().clone();
                assert(sm.coerceParam != ~0u);
                gp.params.types.at(sm.coerceParam) = context.ivars.newIvarTr();
                return context.crate.types.path(mv$(gp), binding.as_Struct());
            }
        };

        const auto langCoerceUnsized = context.crate.getLangItemPathOpt("coerce_unsized"); // TODO: Pre-load

        const bool sameStructWithInference = [&]() {
            const auto* srcPath = src->opt_Path();
            const auto* dstPath = dst->opt_Path();
            if (!srcPath || !dstPath || !srcPath->binding.is_Struct() || !dstPath->binding.is_Struct() || srcPath->binding.as_Struct() != dstPath->binding.as_Struct()) {
                return false;
            }
            return context.resolve.typeContainsIvars(src) || context.resolve.typeContainsIvars(dst);
        }();

        // TODO: Should ErasedType be counted here? probably not.
        if (!sameStructWithInference && (H::typeIsBounded(src) || H::typeIsBounded(dst))) {
            if (!langCoerceUnsized.components().empty()) {
                HIRPathParams pp{dst};

                SolverCertainty certainty = SolverCertainty::NoSolution;
                context.resolve.solveTraitGoal(
                    sp,
                    langCoerceUnsized,
                    pp,
                    src,
                    [&](SolverResponse response) {
                    if (!response.hasImpl) {
                        return false;
                    }
                    certainty = response.certainty;
                    if (certainty == SolverCertainty::Proven && contextMut) {
                        contextMut->applySolverResponse(sp, response);
                    }
                    return certainty == SolverCertainty::Proven;
                },
                    {
                        .allowInferInputs = true,
                    }
                );
                if (certainty == SolverCertainty::Proven) {
                    return CoerceResult::Unsize;
                }
                if (certainty == SolverCertainty::Ambiguous) {
                    return CoerceResult::Unknown;
                }
            }
        }

        if (src->is_Infer() && ((*dst).is_Path() && (*dst).as_Path().binding.is_Struct() && (*dst).as_Path().binding.as_Struct()->structMarkings.coerceUnsized != HIRStructMarkings::Coerce::None)) {
            if (contextMut) {
                contextMut->possibleEquateIvar(sp, src->as_Infer().index, dst, Context::PossibleTypeSource::CoerceTo);
            }
            // TODO: Avoid needless loop return
            return CoerceResult::Unknown;
        }
        if (dst->is_Infer() && ((*src).is_Path() && (*src).as_Path().binding.is_Struct() && (*src).as_Path().binding.as_Struct()->structMarkings.coerceUnsized != HIRStructMarkings::Coerce::None)) {
            if (contextMut) {
                contextMut->possibleEquateIvar(sp, dst->as_Infer().index, src, Context::PossibleTypeSource::CoerceFrom);
            }
            // TODO: Avoid needless loop return
            return CoerceResult::Unknown;
        }
        if (((*dst).is_Path() && ((*dst).as_Path().binding.is_Struct())) && ((*src).is_Path() && ((*src).as_Path().binding.is_Struct()))) {
            const auto& spbe = src->as_Path().binding.as_Struct();
            const auto& dpbe = dst->as_Path().binding.as_Struct();
            if (spbe != dpbe) {
                // TODO: Error here? (equality in caller will cause an error)
                return CoerceResult::Equality;
            }
            const auto& sm = spbe->structMarkings;
            if (sm.coerceUnsized == HIRStructMarkings::Coerce::None) {
                const auto& ppDst = dst->as_Path().path.data.as_Generic().params;
                const auto& ppSrc = src->as_Path().path.data.as_Generic().params;
                ASSERT_BUG(sp, ppDst.types.size() == ppSrc.types.size(), "Struct type argument count mismatch");
                ASSERT_BUG(sp, ppDst.values.size() == ppSrc.values.size(), "Struct const argument count mismatch");

                if (context.ivars.pathparamsContainIvars(ppDst, false) || context.ivars.pathparamsContainIvars(ppSrc, false)) {
                    return CoerceResult::Equality;
                }

                HIRCompare fieldsCmp = HIRCompare::Equal;
                auto relateField = [&](const HIRTypeData* fieldTpl) {
                    auto srcField = context.expandAssociatedTypes(sp, MonomorphStatePtr(context.crate.types, src, &ppSrc, nullptr).monomorphType(sp, fieldTpl));
                    auto dstField = context.expandAssociatedTypes(sp, MonomorphStatePtr(context.crate.types, dst, &ppDst, nullptr).monomorphType(sp, fieldTpl));
                    fieldsCmp &= dstField->compareWithPlaceholders(sp, srcField, context.ivars.callbackResolveInfer());
                    if (contextMut && fieldsCmp != HIRCompare::Unequal) {
                        contextMut->equateTypes(sp, dstField, srcField);
                    }
                };
                switch (spbe->data.tag()) {
                    case HIRStructData::TAG_Unit:
                        break;
                    case HIRStructData::TAG_Tuple:
                        for (const auto& field : spbe->data.as_Tuple()) {
                            relateField(field.ent);
                        }
                        break;
                    case HIRStructData::TAG_Named:
                        for (const auto& field : spbe->data.as_Named()) {
                            relateField(field.ty);
                        }
                        break;
                }
                if (fieldsCmp == HIRCompare::Unequal) {
                    return CoerceResult::Equality;
                }
                for (size_t i = 0; i < ppSrc.values.size(); i++) {
                    if (contextMut) {
                        contextMut->equateValues(sp, ppDst.values[i], ppSrc.values[i]);
                    } else if (ppDst.values[i] != ppSrc.values[i]) {
                        return CoerceResult::Equality;
                    }
                }
                if (!contextMut && fieldsCmp == HIRCompare::Fuzzy) {
                    return CoerceResult::Unknown;
                }

                if (nodePtrPtr) {
                    HIRExprNodeP* valueNode = nodePtrPtr;
                    while (auto* block = cast<HIRExprNodeBlock>(valueNode->get())) {
                        block->resType = dst;
                        valueNode = &block->valueNode;
                    }
                    (*valueNode)->resType = dst;
                }
                return CoerceResult::Custom;
            }

            const auto& ppDst = dst->as_Path().path.data.as_Generic().params;
            const auto& ppSrc = src->as_Path().path.data.as_Generic().params;
            assert(ppDst.types.size() == ppSrc.types.size());
            for (size_t i = 0; i < ppSrc.types.size(); i++) {
                if (i == sm.coerceParam) {
                    continue;
                }
                if (contextMut) {
                    contextMut->equateTypes(sp, ppDst.types.at(i), ppSrc.types.at(i));
                }
            }
            assert(ppDst.values.size() == ppSrc.values.size());
            for (size_t i = 0; i < ppSrc.values.size(); i++) {
                TODO(sp, "Handle values in CoerceUnsized");
            }
            const auto& idst = ppDst.types.at(sm.coerceParam);
            const auto& isrc = ppSrc.types.at(sm.coerceParam);
            switch (sm.coerceUnsized) {
                case HIRStructMarkings::Coerce::None:
                    UNREACHABLE();
                case HIRStructMarkings::Coerce::Passthrough:
                    // TODO: Force emitting `_Unsize` instead of anything else
                    return checkCoerceTys(context, sp, idst, isrc, contextMut, nullptr);
                case HIRStructMarkings::Coerce::Pointer:
                    return checkUnsizeTys(context, sp, idst, isrc, contextMut, nullptr);
            }
        }

        const bool dstIsUnboundPath = ((*dst).is_Path() && ((*dst).as_Path().binding.is_Unbound()));
        const bool srcIsUnboundPath = ((*src).is_Path() && ((*src).as_Path().binding.is_Unbound()));
        if (dstIsUnboundPath || srcIsUnboundPath) {
            if (contextMut) {
                if (const auto* dep = dst->opt_Infer(); dep && srcIsUnboundPath) {
                    contextMut->possibleEquateIvar(sp, dep->index, src, Context::PossibleTypeSource::CoerceFrom);
                }
                if (const auto* sep = src->opt_Infer(); sep && dstIsUnboundPath) {
                    contextMut->possibleEquateIvar(sp, sep->index, dst, Context::PossibleTypeSource::CoerceTo);
                }
            }
            return CoerceResult::Unknown;
        }

        if (const auto* sep = src->opt_Infer()) {
            const auto& se = *sep;
            ASSERT_BUG(sp, !dst->is_Infer(), "Already handled?");

            if (contextMut) {
                contextMut->possibleEquateTypeUnknown(sp, dst, Context::IvarUnknownType::From);
            }

            if (dst->is_Pointer() || dst->is_Borrow()) {
                if (contextMut) {
                    contextMut->possibleEquateIvar(sp, se.index, dst, Context::PossibleTypeSource::CoerceTo);
                }
                return CoerceResult::Unknown;
            }

            // HACK: Composite types may hit issues with `!` within them, primtives don't have this issue?

            else if (dst->is_Primitive()) {
                return CoerceResult::Equality;
            } else {
                if (contextMut) {
                    contextMut->possibleEquateIvar(sp, se.index, dst, Context::PossibleTypeSource::UnsizeFrom);
                }
                return CoerceResult::Unknown;
            }
        } else if (src->is_Diverge()) {
            if (const auto* dep = dst->opt_Infer()) {
                if (contextMut) {
                    contextMut->possibleEquateIvar(sp, dep->index, src, Context::PossibleTypeSource::UnsizeFrom);
                }
                return CoerceResult::Unknown;
            } else {
                return CoerceResult::Custom;
            }
        } else if (const auto* sep = src->opt_Pointer()) {
            const auto& se = *sep;
            if (const auto* dep = dst->opt_Infer()) {
                if (contextMut) {
                    contextMut->possibleEquateIvar(sp, dep->index, src, Context::PossibleTypeSource::CoerceFrom);
                }
                return CoerceResult::Unknown;
            } else if (const auto* dep = dst->opt_Pointer()) {
                if (dep->type < se.type) {
                    if (nodePtrPtr) {
                        auto newType = context.crate.types.pointer(dep->type, se.inner);

                        // - TODO: Alter the block's result types
                        HIRExprNodeP* npp = nodePtrPtr;
                        while (auto* p = cast<HIRExprNodeBlock>(npp->get())) {
                            ASSERT_BUG(p->span(), context.ivars.typesEqual(p->resType, p->valueNode->resType), "Block and result mismatch - " << context.ivars.fmtType(p->resType) << " != " << context.ivars.fmtType(p->valueNode->resType));
                            if (!context.ivars.typesEqual(p->resType, src)) {
                                return CoerceResult::Unknown;
                            }
                            if (contextMut) {
                                p->resType = dst;
                            }
                            npp = &p->valueNode;
                            ASSERT_BUG(sp, *npp, "Null node pointer on block");
                        }
                        HIRExprNodeP& nodePtr = *npp;

                        if (contextMut) {
                            auto span = nodePtr->span();
                            nodePtr = NEWNODE(newType, span, Cast, mv$(nodePtr), newType);
                            context.ivars.getType(nodePtr->resType);

                            contextMut->ivars.markChange();
                        }

                        switch (checkUnsizeTys(context, sp, dep->inner, se.inner, contextMut, &nodePtr)) {
                            case CoerceResult::Fail:
                                return CoerceResult::Fail;
                            case CoerceResult::Unknown:
                                if (&nodePtr != nodePtrPtr) {
                                    if (contextMut) {
                                        contextMut->equateTypesCoerce(sp, dst, nodePtr);
                                    }
                                    return CoerceResult::Custom;
                                } else {
                                    return CoerceResult::Unknown;
                                }
                            case CoerceResult::Custom:
                                return CoerceResult::Custom;
                            case CoerceResult::Equality:
                                if (contextMut) {
                                    contextMut->equateTypes(sp, dep->inner, se.inner);
                                }
                                return CoerceResult::Custom;
                            case CoerceResult::Unsize:
                                if (contextMut) {
                                    auto span = nodePtr->span();
                                    nodePtr = NEWNODE(dst, span, Unsize, mv$(nodePtr), dst);
                                }
                                return CoerceResult::Custom;
                        }
                        UNREACHABLE();
                    } else {
                        //TODO(sp, "Borrow strength reduction with no node pointer - " << src << " -> " << dst);
                        return CoerceResult::Unsize;
                    }
                } else if (dep->type == se.type) {
                } else {
                    // TODO: return CoerceResult::Failed? (indicating that it failed outright, don't even try)
                    return CoerceResult::Equality;
                }
                ASSERT_BUG(sp, dep->type == se.type, "Pointer strength mismatch");

                return checkUnsizeTys(context, sp, dep->inner, se.inner, contextMut, nodePtrPtr);
            } else {
                // TODO: Error here? (leave to caller)
                return CoerceResult::Equality;
            }
        } else if (const auto* sep = src->opt_Borrow()) {
            const auto& se = *sep;
            if (const auto* dep = dst->opt_Infer()) {
                if (contextMut) {
                    contextMut->possibleEquateIvar(sp, dep->index, src, Context::PossibleTypeSource::CoerceFrom);
                }
                return CoerceResult::Unknown;
            } else if (const auto* dep = dst->opt_Pointer()) {
                if (!(dep->type <= se.type)) {
                    if (!contextMut) {
                        return CoerceResult::Fail;
                    }
                    ERROR(sp, E0000, "Type mismatch between " << dst << " and " << src << " - Mutability not compatible");
                }

                switch (checkUnsizeTys(context, sp, dep->inner, se.inner, contextMut, nodePtrPtr)) {
                    case CoerceResult::Fail:
                        return CoerceResult::Fail;
                    case CoerceResult::Unknown:
                        return CoerceResult::Unknown;
                    case CoerceResult::Custom:
                        return CoerceResult::Custom;
                    case CoerceResult::Equality:
                        if (nodePtrPtr && contextMut) {
                            auto& nodePtr = *nodePtrPtr;
                            {
                                auto span = nodePtr->span();
                                nodePtr = HIRExprNodeP(context.crate.pool->make<HIRExprNodeCast>(mv$(span), mv$(nodePtr), dst));
                                nodePtr->resType = dst;
                            }
                        }

                        if (contextMut) {
                            contextMut->equateTypes(sp, dep->inner, se.inner);
                        }
                        return CoerceResult::Custom;
                    case CoerceResult::Unsize:
                        if (nodePtrPtr && contextMut) {
                            auto& nodePtr = *nodePtrPtr;
                            auto dstB = context.crate.types.borrow(se.type, dep->inner);
                            {
                                auto span = nodePtr->span();
                                nodePtr = NEWNODE(dstB, span, Unsize, mv$(nodePtr), dstB);
                            }

                            {
                                auto span = nodePtr->span();
                                nodePtr = HIRExprNodeP(context.crate.pool->make<HIRExprNodeCast>(mv$(span), mv$(nodePtr), dst));
                                nodePtr->resType = dst;
                            }
                        }
                        return CoerceResult::Custom;
                }
                UNREACHABLE();
            } else if (const auto* dep = dst->opt_Borrow()) {
                if (dep->type == se.type && se.inner->is_Diverge() && contextMut && nodePtrPtr && *nodePtrPtr) {
                    HIRExprNodeP* borrowNodePtr = nodePtrPtr;
                    std::vector<HIRExprNodeBlock*> blocks;
                    while (auto* block = cast<HIRExprNodeBlock>(borrowNodePtr->get())) {
                        if (!block->valueNode) {
                            break;
                        }
                        blocks.push_back(block);
                        borrowNodePtr = &block->valueNode;
                    }
                    if (auto* borrow = cast<HIRExprNodeBorrow>(borrowNodePtr->get()); borrow && borrow->type == dep->type) {
                        contextMut->equateTypesCoerce(sp, dep->inner, borrow->value);
                        for (auto* block : blocks) {
                            block->resType = dst;
                        }
                        (*borrowNodePtr)->resType = dst;
                        return CoerceResult::Custom;
                    }
                }

                if (dep->type < se.type) {
                    if (nodePtrPtr) {
                        const auto innerTy = se.inner;
                        auto dstBt = dep->type;
                        auto newType = context.crate.types.borrow(dstBt, innerTy);

                        // - TODO: Alter the block's result types
                        {
                            HIRExprNodeP* npp = nodePtrPtr;
                            while (auto* p = cast<HIRExprNodeBlock>(npp->get())) {
                                if (!context.ivars.typesEqual(p->resType, src)) {
                                    return CoerceResult::Unknown;
                                }
                                npp = &p->valueNode;
                                ASSERT_BUG(sp, *npp, "Null node pointer in block");
                            }
                        }
                        HIRExprNodeP* npp = nodePtrPtr;
                        while (auto* p = cast<HIRExprNodeBlock>(npp->get())) {
                            ASSERT_BUG(p->span(), context.ivars.typesEqual(p->resType, p->valueNode->resType), "(borrow) Block and result mismatch - " << context.ivars.fmtType(p->resType) << " != " << context.ivars.fmtType(p->valueNode->resType));
                            ASSERT_BUG(p->span(), context.ivars.typesEqual(p->resType, src), "(borrow) Block and result mismatch - " << context.ivars.fmtType(p->resType) << " != " << context.ivars.fmtType(src));
                            if (contextMut) {
                                p->resType = dst;
                            }
                            npp = &p->valueNode;
                        }
                        HIRExprNodeP& nodePtr = *npp;

                        if (contextMut) {
                            auto span = nodePtr->span();
                            nodePtr = NEWNODE(innerTy, span, Deref, mv$(nodePtr));
                            context.ivars.getType(nodePtr->resType);
                            nodePtr = NEWNODE(mv$(newType), span, Borrow, dstBt, mv$(nodePtr));
                            context.ivars.getType(nodePtr->resType);

                            contextMut->ivars.markChange();
                        }

                        switch (checkUnsizeTys(context, sp, dep->inner, se.inner, contextMut, &nodePtr)) {
                            case CoerceResult::Fail:
                                return CoerceResult::Fail;
                            case CoerceResult::Unknown:
                                if (&nodePtr != nodePtrPtr) {
                                    if (contextMut) {
                                        contextMut->equateTypesCoerce(sp, dst, nodePtr);
                                    }
                                    return CoerceResult::Custom;
                                } else {
                                    return CoerceResult::Unknown;
                                }
                            case CoerceResult::Custom:
                                return CoerceResult::Custom;
                            case CoerceResult::Equality:
                                if (contextMut) {
                                    contextMut->equateTypes(sp, dep->inner, se.inner);
                                }
                                return CoerceResult::Custom;
                            case CoerceResult::Unsize:
                                if (contextMut) {
                                    auto span = nodePtr->span();
                                    nodePtr = NEWNODE(dst, span, Unsize, mv$(nodePtr), dst);
                                }
                                return CoerceResult::Custom;
                        }
                        UNREACHABLE();
                    } else {
                        //TODO(sp, "Borrow strength reduction with no node pointer - " << src << " -> " << dst);
                        return CoerceResult::Unsize;
                    }
                } else if (dep->type == se.type) {
                } else {
                    // TODO: return CoerceResult::Failed? (indicating that it failed outright, don't even try)
                    return CoerceResult::Equality;
                }
                ASSERT_BUG(sp, dep->type == se.type, "Borrow strength mismatch");

                return checkUnsizeTys(context, sp, dep->inner, se.inner, contextMut, nodePtrPtr);
            } else {
                // TODO: Error here?
                return CoerceResult::Equality;
            }
        } else if (src->is_NodeType() && src->as_NodeType().is_Closure()) {
            const auto* nodeP = src->as_NodeType().as_Closure();
            if (dst->is_ErasedType()) {
                std::vector<HIRTypeRef> closureArgs;
                closureArgs.reserve(nodeP->args.size());
                for (const auto& arg : nodeP->args) {
                    closureArgs.push_back(arg.second);
                }
                HIRPathParams desiredParams{context.crate.types.tuple(mv$(closureArgs))};

                std::vector<HIRTypeRef> expectedArgs;
                HIRTypeRef expectedOutput;
                const auto inspectExpectation = [&](ImplRef impl) {
                    auto params = impl.getTraitParams(context.crate.types);
                    if (params.types.size() != 1 || !params.types.front()->is_Tuple()) {
                        return false;
                    }
                    const auto& args = params.types.front()->as_Tuple();
                    if (args.size() != nodeP->args.size()) {
                        return false;
                    }
                    auto output = impl.getType(context.crate.types, "Output", {});
                    if (output == HIRTypeRef()) {
                        return false;
                    }

                    bool hasExpectation = false;
                    std::vector<HIRTypeRef> concreteArgs;
                    concreteArgs.reserve(args.size());
                    for (const auto& arg : args) {
                        if (typeContainsImplPlaceholder(context.crate.types, arg)) {
                            concreteArgs.push_back(HIRTypeRef());
                        } else {
                            concreteArgs.push_back(arg);
                            hasExpectation = true;
                        }
                    }
                    if (typeContainsImplPlaceholder(context.crate.types, output)) {
                        output = HIRTypeRef();
                    } else {
                        hasExpectation = true;
                    }
                    if (!hasExpectation) {
                        return false;
                    }

                    expectedArgs = mv$(concreteArgs);
                    expectedOutput = mv$(output);
                    return true;
                };
                const auto findExpectation = [&](const HIRSimplePath& trait) {
                    return context.resolve.solveTraitGoal(
                        sp,
                        trait,
                        desiredParams,
                        dst,
                        [&](SolverResponse response) {
                        return response.hasImpl && response.certainty == SolverCertainty::Proven && response.impl && !response.impl->ambiguousIdentity && inspectExpectation(response.impl->legacy());
                    },
                        {
                            .allowInferInputs = true,
                        }
                    );
                };
                const bool asyncExpectation = findExpectation(context.resolve.langAsyncFnOnce());
                const bool foundExpectation = asyncExpectation || findExpectation(context.resolve.langFnOnce());
                if (foundExpectation && contextMut) {
                    for (size_t i = 0; i < expectedArgs.size(); i++) {
                        if (expectedArgs[i] != HIRTypeRef()) {
                            contextMut->equateTypes(sp, nodeP->args[i].second, expectedArgs[i]);
                        }
                    }
                    if (expectedOutput != HIRTypeRef()) {
                        if (asyncExpectation) {
                            contextMut->equateTypesAssoc(sp, expectedOutput, context.resolve.langFuture(), {}, nodeP->returnType, "Output", {});
                        } else {
                            contextMut->equateTypes(sp, nodeP->returnType, expectedOutput);
                        }
                    }
                }
                return CoerceResult::Equality;
            } else if (dst->is_Function()) {
                const auto& de = dst->as_Function();
                if (nodePtrPtr) {
                    auto* coercedNodePtr = nodePtrPtr;
                    while (auto* block = cast<HIRExprNodeBlock>(coercedNodePtr->get())) {
                        ASSERT_BUG(block->span(), block->valueNode, "Closure coercion reached a non-yielding block");
                        ASSERT_BUG(block->span(), context.ivars.typesEqual(block->resType, block->valueNode->resType), "Block and result mismatch - " << context.ivars.fmtType(block->resType) << " != " << context.ivars.fmtType(block->valueNode->resType));
                        if (contextMut) {
                            block->resType = dst;
                        }
                        coercedNodePtr = &block->valueNode;
                    }

                    auto& nodePtr = *coercedNodePtr;
                    auto span = nodePtr->span();
                    if (de.abi != ABI_RUST) {
                        ERROR(span, E0000, "Cannot use closure for extern function pointer");
                    }
                    if (de.argTypes.size() != nodeP->args.size()) {
                        ERROR(span, E0000, "Mismatched argument count coercing closure to fn(...)");
                    }
                    if (contextMut) {
                        for (size_t i = 0; i < de.argTypes.size(); i++) {
                            contextMut->equateTypes(sp, de.argTypes[i], nodeP->args[i].second);
                        }
                        contextMut->equateTypes(sp, de.rettype, nodeP->returnType);
                        nodePtr = NEWNODE(dst, span, Cast, mv$(nodePtr), dst);
                    }
                }
                return CoerceResult::Custom;
            } else if (const auto* dep = dst->opt_Infer()) {
                if (contextMut) {
                    for (const auto& at : nodeP->args) {
                        contextMut->possibleEquateTypeUnknown(sp, at.second, Context::IvarUnknownType::To);
                    }
                    contextMut->possibleEquateTypeUnknown(sp, nodeP->returnType, Context::IvarUnknownType::Bound);
                    contextMut->possibleEquateIvar(sp, dep->index, src, Context::PossibleTypeSource::CoerceFrom);
                }
                return CoerceResult::Unknown;
            } else {
                return CoerceResult::Equality;
            }
        } else if (const auto* se = src->opt_NamedFunction()) {
            if (const auto* de = dst->opt_Function()) {
                auto ft = context.expandAssociatedTypes(sp, context.crate.types.function(se->decay(context.crate.types, sp)));
                const auto* se = &ft->as_Function();
                if (se->abi != de->abi) {
                    return CoerceResult::Equality;
                }

                if (se->isUnsafe != de->isUnsafe && se->isUnsafe) {
                    return CoerceResult::Equality;
                }
                if (se->argTypes.size() != de->argTypes.size()) {
                    return CoerceResult::Equality;
                }

                if (contextMut) {
                    auto& nodePtr = *nodePtrPtr;
                    auto span = nodePtr->span();

                    for (size_t i = 0; i < de->argTypes.size(); i++) {
                        contextMut->equateTypes(sp, de->argTypes[i], se->argTypes[i]);
                    }
                    contextMut->equateTypes(sp, de->rettype, se->rettype);
                    nodePtr = NEWNODE(dst, span, Cast, mv$(nodePtr), dst);
                }
                return CoerceResult::Custom;
            }

            else if (const auto* dep = dst->opt_Infer()) {
                if (contextMut) {
                    contextMut->possibleEquateIvar(sp, dep->index, src, Context::PossibleTypeSource::UnsizeFrom);
                }
                return CoerceResult::Unknown;
            } else {
                return CoerceResult::Equality;
            }
        } else if (const auto* se = src->opt_Function()) {
            if (const auto* de = dst->opt_Function()) {
                auto& nodePtr = *nodePtrPtr;
                auto span = nodePtr->span();
                if (se->abi != de->abi) {
                    return CoerceResult::Equality;
                }

                if (se->isUnsafe != de->isUnsafe && se->isUnsafe) {
                    return CoerceResult::Equality;
                }
                if (de->argTypes.size() != se->argTypes.size()) {
                    return CoerceResult::Equality;
                }
                if (contextMut) {
                    for (size_t i = 0; i < de->argTypes.size(); i++) {
                        contextMut->equateTypes(sp, de->argTypes[i], se->argTypes[i]);
                    }
                    contextMut->equateTypes(sp, de->rettype, se->rettype);
                    nodePtr = NEWNODE(dst, span, Cast, mv$(nodePtr), dst);
                }
                return CoerceResult::Custom;
            }

            else if (const auto* dep = dst->opt_Infer()) {
                if (contextMut) {
                    contextMut->possibleEquateIvar(sp, dep->index, src, Context::PossibleTypeSource::UnsizeFrom);
                }
                return CoerceResult::Unknown;
            } else {
                return CoerceResult::Equality;
            }
        } else {
            // TODO: ! should be handled above or in caller?
            return CoerceResult::Equality;
        }
    }

    bool checkCoerce(Context& context, const Context::Coercion& v) {
        HIRExprNodeP& nodePtr = *v.rightNodePtr;
        const auto& sp = nodePtr->span();
        const auto& tyDst = context.ivars.getType(v.leftTy);
        const auto& tySrc = context.ivars.getType(nodePtr->resType);
        const bool hasPendingDerefTarget = std::any_of(context.linkAssoc.begin(), context.linkAssoc.end(), [&](const Context::Associated& rule) {
            return rule.operatorKind == TypeckPrimitiveOperator::Deref && context.ivars.typesEqual(rule.leftTy, tySrc);
        });
        if (hasPendingDerefTarget) {
            return false;
        }

        switch (checkCoerceTys(context, sp, tyDst, tySrc, &context, &nodePtr)) {
            case CoerceResult::Fail:
                return false;
            case CoerceResult::Unknown:
                return false;
            case CoerceResult::Custom:
                return true;
            case CoerceResult::Equality:
                context.equateTypes(sp, tyDst, tySrc);
                return true;
            case CoerceResult::Unsize:
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

    bool typeNeedsFurtherInference(const Context& context, const HIRTypeData* type) {
        bool pending = false;
        visitTyWith(type, [&](const HIRTypeData* inner) {
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

    AssociatedCheckResult checkAssociated(Context& context, Context::Associated& v) {
        const auto& sp = v.span;

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

        std::optional<HIRTypeRef> outputType;

        struct H {
            static bool typeIsNum(const HIRTypeData* t) {
                switch ((*t).tag()) {
                    default:
                        return false;
                    case HIRTypeData::TAG_Primitive: {
                        auto& e = (*t).as_Primitive();
                        switch (e) {
                            case HIRCoreType::Str:
                            case HIRCoreType::Char:
                                return false;
                            default:
                                return true;
                        }
                        break;
                    }
                    case HIRTypeData::TAG_Infer: {
                        auto& e = (*t).as_Infer();
                        return e.isLit();
                    }
                }
                UNREACHABLE();
            }

            static bool unaryCanUseExpected(TypeckPrimitiveOperator op, const HIRTypeData* type) {
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

        const auto concreteCoercionSource = [&](const HIRTypeData* type) {
            const auto* infer = context.getType(type)->opt_Infer();
            if (!infer || infer->index == ~0u || infer->index >= context.possibleIvarVals.size()) {
                return static_cast<const HIRTypeData*>(nullptr);
            }
            const auto& possible = context.possibleIvarVals[infer->index];
            for (const auto& source : possible.typesCoerceFrom) {
                const auto* sourceType = context.getType(source.ty);
                if (!sourceType->is_Infer()) {
                    return sourceType;
                }
            }
            return static_cast<const HIRTypeData*>(nullptr);
        };

        bool hasSemanticOperatorImpl = false;
        bool sawCurrentOperatorImpl = false;
        bool currentOperatorImplHasBuiltinSignature = false;
        if (v.operatorKind != TypeckPrimitiveOperator::None) {
            auto probeParams = v.params.clone();
            if (probeParams.types.size() == 1) {
                if (const auto* source = concreteCoercionSource(probeParams.types.front())) {
                    probeParams.types.front() = source;
                }
            }
            const SolverOperatorGoal operatorGoal{
                .operation = v.operatorKind,
                .outputName = v.name.c_str(),
                .outputParams = &v.atyPp,
                .currentImpl = context.currentTraitImpl,
            };
            context.resolve.solveTraitGoal(sp, v.trait, probeParams, v.implTy, [&](SolverResponse response) {
                hasSemanticOperatorImpl = response.operatorSummary.hasSemanticImpl;
                sawCurrentOperatorImpl = response.operatorSummary.sawCurrentImpl;
                currentOperatorImplHasBuiltinSignature = response.operatorSummary.currentImplHasBuiltinSignature;
                return response.hasImpl;
            }, {.assocName = "", .operatorGoal = &operatorGoal});
        }

        if (v.name != "" && (v.operatorKind == TypeckPrimitiveOperator::Shl || v.operatorKind == TypeckPrimitiveOperator::Shr || v.operatorKind == TypeckPrimitiveOperator::ShlAssign || v.operatorKind == TypeckPrimitiveOperator::ShrAssign)) {
            const auto* valueTy = context.getType(v.implTy);
            if (const auto* borrow = valueTy->opt_Borrow()) {
                valueTy = context.getType(borrow->inner);
            }
            const auto* leftInfer = valueTy->opt_Infer();
            if (leftInfer && leftInfer->tyClass == HIRInferClass::Integer) {
                context.equateTypes(sp, v.leftTy, valueTy);
            }
        }

        if (v.isOperator && v.params.types.size() == 1 && !context.ivars.typeContainsIvars(v.implTy, /*only_unbound=*/true)) {
            if (const auto* borrow = context.getType(v.implTy)->opt_Borrow()) {
                const auto& valueTy = context.getType(borrow->inner);
                const auto& rightTy = context.getType(v.params.types.front());
                if (H::typeIsNum(valueTy) && !valueTy->is_Infer() && H::typeIsNum(rightTy)) {
                    if (v.name != "") {
                        context.equateTypes(sp, v.leftTy, valueTy);
                    }
                    const bool isComparison = v.operatorKind == TypeckPrimitiveOperator::Equal || v.operatorKind == TypeckPrimitiveOperator::Order;
                    if (!isComparison && primitiveOperatorLhsDeterminesRhs(v.operatorKind, valueTy)) {
                        context.equateTypes(sp, v.params.types.front(), valueTy);
                    }
                }
            }
        }

        const bool canContextualisePrimitiveRhs = v.isOperator && !hasSemanticOperatorImpl && (!sawCurrentOperatorImpl || currentOperatorImplHasBuiltinSignature) && v.params.types.size() == 1 && !context.ivars.typeContainsIvars(v.implTy, /*only_unbound=*/true) && primitiveOperatorLhsDeterminesRhs(v.operatorKind, context.getType(v.implTy)) && v.params.types.front()->is_Infer() && v.params.types.front()->as_Infer().index == ~0u;
        if (canContextualisePrimitiveRhs) {
            context.addIvars(v.params.types.front());
        }

        const bool primitiveTypesAreContextual = !context.ivars.typeContainsIvars(v.implTy, /*only_unbound=*/true) && !context.ivars.pathparamsContainIvars(v.params, /*only_unbound=*/true) && (v.name == "" || !context.ivars.typeContainsIvars(v.leftTy, /*only_unbound=*/true));

        if (v.isOperator && !hasSemanticOperatorImpl && primitiveTypesAreContextual) {
            if (v.params.types.size() == 0) {
                const auto& ty = context.getType(v.implTy);
                const auto& res = context.getType(v.leftTy);
                if (H::typeIsNum(ty)) {
                    context.equateTypes(sp, res, ty);
                }
            } else if (v.params.types.size() == 1) {
                const auto& left = v.implTy;
                const auto& right = v.params.types.at(0);
                const auto& res = v.leftTy;
                const auto& leftTy = context.getType(left);
                const auto& rightTy = context.getType(right);
                const bool primitiveOrLiteralPair = H::typeIsNum(leftTy) && H::typeIsNum(rightTy);
                const bool languagePrimitiveCandidate = primitiveOperatorHasLanguageCandidate(v.operatorKind, leftTy, rightTy);
                const bool isShiftOperator = v.operatorKind == TypeckPrimitiveOperator::Shl || v.operatorKind == TypeckPrimitiveOperator::Shr || v.operatorKind == TypeckPrimitiveOperator::ShlAssign || v.operatorKind == TypeckPrimitiveOperator::ShrAssign;
                if (primitiveOrLiteralPair || languagePrimitiveCandidate) {
                    if (v.name == "") {
                    } else {
                        context.equateTypes(sp, res, left);
                    }
                    if (isShiftOperator) {
                        if (rightTy->is_Infer() && rightTy->as_Infer().isLit() && !leftTy->is_Infer()) {
                            context.possibleEquateTypeUnknown(sp, right, Context::IvarUnknownType::To);
                            return AssociatedCheckResult::Stalled;
                        }
                    } else {
                        context.equateTypes(sp, left, right);
                    }
                    if (v.name != "" && context.getType(left)->is_Infer() && context.getType(right)->is_Infer() && context.getType(res)->is_Infer()) {
                        context.possibleEquateTypeUnknown(sp, right, Context::IvarUnknownType::To);
                        return AssociatedCheckResult::Stalled;
                    }
                }

                context.possibleEquateTypeUnknown(sp, right, Context::IvarUnknownType::To);
            } else {
                BUG(sp, "Associated type rule with `is_operator` set but an incorrect parameter count");
            }
        }

        if (v.name != "") {
            context.possibleEquateTypeUnknown(sp, v.leftTy, Context::IvarUnknownType::Bound);
        }

        if (v.isOperator && v.params.types.empty() && context.getType(v.implTy)->is_Diverge() && H::unaryCanUseExpected(v.operatorKind, context.getType(v.leftTy))) {
            return AssociatedCheckResult::Complete;
        }

        if (const auto* e = context.ivars.getType(v.implTy)->opt_Infer()) {
            const bool hasSelfCoercionGuidance = e->index != ~0u && e->index < context.possibleIvarVals.size() && (!context.possibleIvarVals[e->index].typesCoerceTo.empty() || !context.possibleIvarVals[e->index].typesCoerceFrom.empty());
            // TODO: ?
            if (!e->isLit() && v.params.types.empty() && !hasSelfCoercionGuidance) {
                return AssociatedCheckResult::Ambiguous;
            }

            if (!e->isLit() && !hasSelfCoercionGuidance) {
                for (const auto& t : v.params.types) {
                    context.possibleEquateTypeUnknown(sp, t, Context::IvarUnknownType::To);
                }
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

        ThinVector<SolverCoercionConstraint> coercionGoals;
        const auto appendCoercionGoals = [&](const HIRTypeData* rawInput, unsigned typeIndex, bool isSelf) {
            const auto* input = context.getType(rawInput);
            const auto* infer = input->opt_Infer();
            if (!infer || infer->index == ~0u || infer->index >= context.possibleIvarVals.size()) {
                return;
            }
            const auto append = [&](const Context::IVarPossible::CoerceTy& edge, SolverCoercionConstraint::Direction direction) {
                const auto* other = context.getType(edge.ty);
                if (const auto* otherInfer = other->opt_Infer(); otherInfer && otherInfer->index != ~0u) {
                    return;
                }
                coercionGoals.push_back(
                    SolverCoercionConstraint{
                        static_cast<unsigned>(typeIndex),
                        other,
                        direction,
                        edge.op == Context::IVarPossible::CoerceTy::Coercion ? SolverCoercionOp::Coercion : SolverCoercionOp::Unsizing,
                        isSelf,
                    }
                );
            };
            const auto& possible = context.possibleIvarVals[infer->index];
            for (const auto& edge : possible.typesCoerceFrom) {
                if (context.getType(edge.ty)->is_Diverge()) {
                    continue;
                }
                append(edge, SolverCoercionConstraint::Direction::InputIsDestination);
            }
            for (const auto& edge : possible.typesCoerceTo) {
                append(edge, SolverCoercionConstraint::Direction::InputIsSource);
            }
        };
        appendCoercionGoals(v.implTy, 0, true);
        for (size_t typeIndex = 0; typeIndex < v.params.types.size(); typeIndex++) {
            appendCoercionGoals(v.params.types[typeIndex], static_cast<unsigned>(typeIndex), false);
        }
        const bool hasSelfCoercionGoal = std::any_of(coercionGoals.begin(), coercionGoals.end(), [](const SolverCoercionConstraint& constraint) {
            return constraint.isSelf;
        });
        bool lateClosureOutput = false;
        if (v.name != "" && (v.trait == context.resolve.langFn() || v.trait == context.resolve.langFnMut() || v.trait == context.resolve.langFnOnce())) {
            const auto* implType = context.getType(v.implTy);
            const auto* closure = implType->is_NodeType() ? implType->as_NodeType().opt_Closure() : nullptr;
            const auto* expectedOutput = context.getType(v.leftTy);
            if (closure && (*closure)->returnType->is_Infer() && context.getType((*closure)->returnType)->is_Diverge() && !expectedOutput->is_Diverge() && !context.ivars.typeContainsIvars(expectedOutput)) {
                context.registerClosureReturnObligation(sp, *closure, expectedOutput);
                lateClosureOutput = true;
            }
        }

        SolverResponse response;
        bool hasResponse = false;
        context.resolve.solveTraitGoal(
            sp,
            v.trait,
            v.params,
            v.implTy,
            [&](SolverResponse value) {
            response = std::move(value);
            hasResponse = true;
            return true;
        },
            {
                .assocName = v.name.c_str(),
                .assocType = v.name == "" || lateClosureOutput ? nullptr : v.leftTy,
                .assocParams = v.name == "" ? nullptr : &v.atyPp,
                .allowInferInputs = true,
                .excludedImpl = currentOperatorUsesLanguagePrimitive() ? context.currentTraitImpl : nullptr,
                .coercions = coercionGoals.empty() ? nullptr : &coercionGoals,
            }
        );

        if (hasResponse) {
            ASSERT_BUG(sp, response.hasImpl && response.impl, "trait solver returned a response without an implementation");
            if (response.certainty == SolverCertainty::Ambiguous && !hasSelfCoercionGoal) {
                const auto* implType = context.getType(v.implTy);
                if (const auto* path = implType->opt_Path(); path && path->binding.is_Unbound()) {
                    return AssociatedCheckResult::Stalled;
                }
                if (const auto* infer = implType->opt_Infer(); infer && !infer->isLit()) {
                    return AssociatedCheckResult::Ambiguous;
                }
            }
            context.applySolverResponse(sp, response);
            if (response.impl->ambiguousIdentity) {
                return AssociatedCheckResult::Ambiguous;
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
            ASSERT_BUG(sp, v.params.types.size() == 1, "Incorrect number of parameters for Unsize");
            const auto& srcTy = context.getType(v.implTy);
            const auto& dstTy = context.getType(v.params.types[0]);
            context.equateTypes(sp, dstTy, srcTy);
            return AssociatedCheckResult::Complete;
        }
        if (v.operatorKind != TypeckPrimitiveOperator::None && (v.params.types.size() == 0 ? primitiveOperatorHasBuiltin(v.operatorKind, context.getType(v.implTy)) : v.params.types.size() == 1 && primitiveOperatorHasBuiltin(v.operatorKind, context.getType(v.implTy), context.getType(v.params.types.at(0))))) {
            return AssociatedCheckResult::Complete;
        }
        if (v.name == "") {
            ERROR(sp, E0000, "Failed to find an impl of " << v.trait << context.ivars.fmt(v.params) << " for " << context.ivars.fmtType(v.implTy));
        } else {
            ERROR(sp, E0000, "Failed to find an impl of " << v.trait << context.ivars.fmt(v.params) << " for " << context.ivars.fmtType(v.implTy) << " with " << v.name << " = " << context.ivars.fmtType(v.leftTy));
        }
    }

}

void Context::registerSolverObligation(const Span& sp, HIRTypeRef type, HIRTraitPath trait) {
    if (trait.typeBounds.empty()) {
        addTraitBound(sp, type, trait.path.path, std::move(trait.path.params));
        return;
    }
    for (auto& associated : trait.typeBounds) {
        equateTypesAssoc(sp, associated.second.type, trait.path.path, trait.path.params.clone(), type, associated.first.c_str(), associated.second.atyParams, false);
    }
}

void Context::registerClosureReturnObligation(const Span& sp, const HIRExprNodeClosure* closure, HIRTypeRef expected) {
    for (auto& obligation : closureReturnObligations) {
        if (obligation.closure != closure) {
            continue;
        }
        equateTypes(sp, obligation.expected, expected);
        return;
    }
    closureReturnObligations.pushBack(ClosureReturnObligation{closure, std::move(expected)});
    ivars.markChange();
}

const HIRTypeData* Context::closureReturnExpectation(const HIRExprNodeClosure* closure) const {
    for (const auto& obligation : closureReturnObligations) {
        if (obligation.closure == closure) {
            return getType(obligation.expected);
        }
    }
    return nullptr;
}

void Context::applySolverResponse(const Span& sp, const SolverResponse& response) {
    ASSERT_BUG(sp, response.slots.typeInputs.size() == response.slots.types.size(), "solver type slot response is malformed");
    ASSERT_BUG(sp, response.slots.valueInputs.size() == response.slots.values.size(), "solver value slot response is malformed");
    for (size_t i = 0; i < response.slots.types.size(); i++) {
        equateTypes(sp, response.slots.typeInputs[i], response.slots.types[i]);
    }
    for (size_t i = 0; i < response.slots.values.size(); i++) {
        equateValues(sp, response.slots.valueInputs[i], response.slots.values[i]);
    }
    for (const auto& equality : response.equalities) {
        equateTypes(sp, equality.left, equality.right);
    }
    for (const auto& equality : response.valueEqualities) {
        equateValues(sp, equality.left, equality.right);
    }
    for (const auto& obligation : response.obligations) {
        registerSolverObligation(sp, obligation.type, obligation.trait.clone());
    }
}

HIRTypeRef Context::expandAssociatedTypes(const Span& sp, HIRTypeRef input) const {
    auto& context = const_cast<Context&>(*this);
    auto effects = makeCallable<SolverResponseCb>([&](SolverResponse response) {
        context.applySolverResponse(sp, response);
        return false;
    });
    return resolve.expandAssociatedTypes(sp, std::move(input), &effects);
}

const HIRTypeData* Context::expandAssociatedTypes(const Span& sp, const HIRTypeData* input, HIRTypeRef& tmp) const {
    auto& context = const_cast<Context&>(*this);
    auto effects = makeCallable<SolverResponseCb>([&](SolverResponse response) {
        context.applySolverResponse(sp, response);
        return false;
    });
    return resolve.expandAssociatedTypes(sp, input, tmp, &effects);
}

void Context::expandAssociatedTypesParams(const Span& sp, HIRPathParams& params) const {
    auto& context = const_cast<Context&>(*this);
    auto effects = makeCallable<SolverResponseCb>([&](SolverResponse response) {
        context.applySolverResponse(sp, response);
        return false;
    });
    resolve.expandAssociatedTypesParams(sp, params, &effects);
}

void Context::compactIvars() {
    auto effects = makeCallable<SolverResponseCb>([&](SolverResponse response) {
        applySolverResponse(Span(), response);
        return false;
    });
    resolve.compactIvars(ivars, &effects);
}

namespace {
    bool pathParamsHaveUntrackedConst(const HIRPathParams& params) {
        return std::any_of(params.values.begin(), params.values.end(), [](const auto& value) {
            return value.is_Infer() || value.is_Unevaluated();
        });
    }

    bool setAssociatedStall(Context& context, Context::Associated& rule) {
        rule.stalledOn.clear();

        const auto typeCanStall = [](const HIRTypeRef type) {
            return (type->flags & (HIRTypeData::HAS_UNEVALUATED_CONST | HIRTypeData::HAS_DEFERRED_CONST)) == 0;
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

    bool associatedStillStalled(const Context& context, const Context::Associated& rule) {
        if (rule.stalledOn.empty()) {
            return false;
        }
        return std::all_of(rule.stalledOn.begin(), rule.stalledOn.end(), [&](const auto& dependency) {
            return context.ivars.getType(dependency.index) == dependency.resolved;
        });
    }

    void mergeAssociatedPossibilities(Context& context, const std::vector<Context::Associated::CapturedIvarPossible>& captured) {
        for (const auto& value : captured) {
            const auto resolved = context.ivars.getType(value.index);
            if (!resolved->is_Infer() || resolved->as_Infer().index != value.index) {
                continue;
            }
            if (value.index >= context.possibleIvarVals.size()) {
                context.possibleIvarVals.resize(value.index + 1);
            }
            context.possibleIvarVals[value.index].mergeFrom(value.possibilities);
        }
    }

    bool typeHasIndependentUnresolvedIvar(const Context& context, const HIRTypeData* type, unsigned int exceptIndex, const ActiveOperatorOutput* active = nullptr);

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

    bool typeHasIndependentUnresolvedIvar(const Context& context, const HIRTypeData* type, unsigned int exceptIndex, const ActiveOperatorOutput* active) {
        bool found = false;
        visitTyWith(type, [&](const HIRTypeData* inner) {
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

    bool typeDependsOnIvar(const Context& context, const HIRTypeData* type, unsigned int index) {
        bool found = false;
        visitTyWith(type, [&](const HIRTypeData* inner) {
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
        if (index >= context.possibleIvarVals.size()) {
            return false;
        }
        for (const auto& associated : context.linkAssoc) {
            if (!associated.isOperator) {
                continue;
            }
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
        return false;
    }

    bool coercionCandidateIsInvalid(const Span& sp, Context& context, const IvarCoercionRefs& coercionRefs, const HIRTypeData* tyL, const HIRTypeData* newTy) {
        const auto ivarIdx = tyL->as_Infer().index;
        bool usedTy = false;

        struct Cb {
            bool& usedTy;
            const Span& sp;
            const Context& context;
            unsigned int ivarIdx;
            const HIRTypeData* newTy;

            Cb(bool& usedTy, const Span& sp, const Context& context, unsigned int ivarIdx, const HIRTypeData* newTy)
                : usedTy(usedTy)
                , sp(sp)
                , context(context)
                , ivarIdx(ivarIdx)
                , newTy(newTy)
            {
            }

            bool operator()(const HIRTypeData* ty, HIRTypeRef& outTy) {
                const auto* e = ty->opt_Infer();
                if (!e) {
                    return false;
                }
                if (e->index == ivarIdx) {
                    outTy = newTy;
                    usedTy = true;
                    return true;
                }
                const auto& rty = context.getType(ty);
                if (const auto* resolved = rty->opt_Infer(); resolved && resolved->index == e->index) {
                    return false;
                }
                outTy = cloneTyWith(context.crate.types, sp, rty, *this);
                return true;
            }
        };

        Cb cb{usedTy, sp, context, ivarIdx, newTy};
        for (const auto* bound : coercionRefs.coercions) {
            usedTy = false;
            auto tL = cloneTyWith(context.crate.types, sp, bound->leftTy, cb);
            auto tR = cloneTyWith(context.crate.types, sp, (*bound->rightNodePtr)->resType, cb);
            if (!usedTy) {
                continue;
            }
            tL = context.expandAssociatedTypes(sp, mv$(tL));
            tR = context.expandAssociatedTypes(sp, mv$(tR));

            switch (checkCoerceTys(context, sp, tL, tR, nullptr, bound->rightNodePtr)) {
                case CoerceResult::Fail:
                    return true;
                case CoerceResult::Unsize:
                    break;
                case CoerceResult::Unknown:
                    break;
                case CoerceResult::Custom:
                    break;
                case CoerceResult::Equality:
                    if (tL->compareWithPlaceholders(sp, tR, context.ivars.callbackResolveInfer()) == HIRCompare::Unequal) {
                        return true;
                    }
                    break;
            }
        }

        if (ivarIdx < context.ivarsSized.size() && context.ivarsSized[ivarIdx]) {
            if (context.resolve.typeIsSized(sp, newTy) == HIRCompare::Unequal) {
                return true;
            }
        }

        for (const auto& pty : context.possibleIvarVals.at(tyL->as_Infer().index).typesCoerceTo) {
            HIRExprNodeP stubNode;
            CoerceResult res = CoerceResult::Unknown;
            switch (pty.op) {
                case Context::IVarPossible::CoerceTy::Coercion:
                    res = checkCoerceTys(context, sp, pty.ty, newTy, nullptr, &stubNode);
                    break;
                case Context::IVarPossible::CoerceTy::Unsizing:
                    res = checkUnsizeTys(context, sp, pty.ty, newTy, nullptr, &stubNode);
                    break;
            }
            switch (res) {
                case CoerceResult::Unsize:
                case CoerceResult::Unknown:
                case CoerceResult::Custom:
                    break;
                case CoerceResult::Fail:
                    return true;
                case CoerceResult::Equality:
                    if (pty.ty->compareWithPlaceholders(sp, newTy, context.ivars.callbackResolveInfer()) == HIRCompare::Unequal) {
                        return true;
                    }
                    break;
            }
        }

        return false;
    }

    enum class IvarPossFallbackType {
        None,
        Backwards,
        Assume,
        IgnoreWeakDisable,
        FinalOption,
    };

    std::ostream& operator<<(std::ostream& os, IvarPossFallbackType t) {
        switch (t) {
            case IvarPossFallbackType::None:
                os << "";
                break;
            case IvarPossFallbackType::Backwards:
                os << " backwards";
                break;
            case IvarPossFallbackType::Assume:
                os << " weak";
                break;
            case IvarPossFallbackType::IgnoreWeakDisable:
                os << " unblock";
                break;
            case IvarPossFallbackType::FinalOption:
                os << " final";
                break;
        }
        return os;
    }

    // TODO: Split the below into a common portion, and a "run" portion (which uses the fallback)

    bool checkIvarPoss(Context& context, const IvarCoercionIndex& coercionIndex, unsigned int i, Context::IVarPossible& ivarEnt, IvarPossFallbackType fallbackTy = IvarPossFallbackType::None) {
        Span _span;
        const auto& sp = _span;
        const bool honourDisable = (fallbackTy != IvarPossFallbackType::IgnoreWeakDisable);

        const auto* tyL = context.ivars.getType(i);
        const auto& coercionRefs = coercionIndex[i];

        if (!((*tyL).is_Infer() && ((*tyL).as_Infer().index == i))) {
            if (ivarEnt.hasRules()) {
                ivarEnt = Context::IVarPossible();
            } else {
            }
            return false;
        }

        if (!ivarEnt.hasRules()) {
            return false;
        }

        {
            for (const auto& t : ivarEnt.typesCoerceTo) {
                for (const auto& t2 : ivarEnt.typesCoerceFrom) {
                    // TODO: Compare such that &[_; 1] == &[u8; 1]? and `&[_]` == `&[T]`
                    if (t.ty == t2.ty && t.ty != tyL) {
                        context.equateTypes(sp, tyL, t.ty);
                        return true;
                    }
                }
            }
        }
        if (ivarEnt.forceDisable && fallbackTy != IvarPossFallbackType::FinalOption) {
            return false;
        }

        if (tyL->as_Infer().isLit()) {
            return false;
        }

        bool mayUseRawPointerFallback = false;

        {
            bool allowUnsized = !(i < context.ivarsSized.size() ? context.ivarsSized.at(i) : false);

            std::vector<PossibleType> possibleTys;
            bool addPlaceholders = (fallbackTy < IvarPossFallbackType::IgnoreWeakDisable);
            if (addPlaceholders && ivarEnt.forceNoFrom) {
                possibleTys.push_back(PossibleType::barrier(PossibleType::UnsizeFrom));
            }
            for (const auto& newTy : ivarEnt.typesCoerceFrom) {
                possibleTys.push_back(PossibleType::concrete(newTy.op == Context::IVarPossible::CoerceTy::Coercion ? PossibleType::CoerceFrom : PossibleType::UnsizeFrom, newTy.ty));
            }
            if (addPlaceholders && ivarEnt.forceNoTo) {
                possibleTys.push_back(PossibleType::barrier(PossibleType::UnsizeTo));
            }
            for (const auto& newTy : ivarEnt.typesCoerceTo) {
                possibleTys.push_back(PossibleType::concrete(newTy.op == Context::IVarPossible::CoerceTy::Coercion ? PossibleType::CoerceTo : PossibleType::UnsizeTo, newTy.ty));
            }

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
                possibleTys.resize(newEnd - possibleTys.begin());
            }

            // TODO: Rewrite ALL of the below (extract the helpers to somewhere useful)

            if (fallbackTy != IvarPossFallbackType::None && std::count_if(possibleTys.begin(), possibleTys.end(), PossibleType::isSourceS) == 1 && !ivarEnt.forceNoFrom) {
                const auto& ent = *std::find_if(possibleTys.begin(), possibleTys.end(), PossibleType::isSourceS);
                if (!context.ivars.typeContainsIvars(ent.ty) && !(ent.ty)->is_Diverge()) {
                    if (!coercionCandidateIsInvalid(sp, context, coercionRefs, tyL, ent.ty)) {
                        context.equateTypes(sp, tyL, ent.ty);
                        return true;
                    }
                }
            }
            if (fallbackTy == IvarPossFallbackType::IgnoreWeakDisable && possibleTys.size() == 1) {
                auto ent = possibleTys[0];
                if (!coercionCandidateIsInvalid(sp, context, coercionRefs, tyL, ent.ty)) {
                    context.equateTypes(sp, tyL, ent.ty);
                    return true;
                }
            }

            if (std::count_if(possibleTys.begin(), possibleTys.end(), PossibleType::isDestS) == 1 && std::count_if(possibleTys.begin(), possibleTys.end(), PossibleType::isSourceS) == 1 && !ivarEnt.forceNoFrom && !ivarEnt.forceNoTo) {
                const auto& entS = *std::find_if(possibleTys.begin(), possibleTys.end(), PossibleType::isSourceS);
                const auto& entD = *std::find_if(possibleTys.begin(), possibleTys.end(), PossibleType::isDestS);

                // TODO: And this ivar isn't Sized bounded?
                if (entS.isCoerce() && entD.isCoerce()) {
                    bool srcNoivars = !context.ivars.typeContainsIvars(entS.ty);
                    bool dstNoivars = !context.ivars.typeContainsIvars(entD.ty);
                    bool srcValid = !coercionCandidateIsInvalid(sp, context, coercionRefs, tyL, entS.ty);
                    bool dstValid = !coercionCandidateIsInvalid(sp, context, coercionRefs, tyL, entD.ty);

                    if (srcValid) {
                        if (srcNoivars) {
                            context.equateTypes(sp, tyL, entS.ty);
                            return true;
                        }
                    }
                    if (dstValid) {
                        if (dstNoivars) {
                            context.equateTypes(sp, tyL, entD.ty);
                            return true;
                        }
                    }

                    if (srcValid) {
                        context.equateTypes(sp, tyL, entS.ty);
                        return true;
                    }
                    if (dstValid) {
                        context.equateTypes(sp, tyL, entD.ty);
                        return true;
                    }
                }
            }

            // - Slight hack to speed up flow-down inference
            if (possibleTys.size() == 1 && possibleTys[0].isSource() && !ivarEnt.forceNoFrom) {
                const auto* tyP = possibleTys[0].ty;
                if ((tyP)->is_Diverge() && context.crate.edition < ASTEdition::Rust2024) {
                    tyP = context.crate.types.unit();
                }
                if (possibleTys[0].isUnsize()) {
                    HIRTypeRef tmpTy;

                    do {
                        if (!coercionCandidateIsInvalid(sp, context, coercionRefs, tyL, tyP)) {
                            break;
                        }
                    } while ((tyP = context.resolve.autoderef(sp, tyP, tmpTy)));
                    if (!tyP) {
                        tyP = possibleTys[0].ty;
                    }
                } else {
                }
                context.equateTypes(sp, tyL, tyP);
                return true;
            }

            // TODO: This shouldn't just return, instead the above null placeholders should be tested
            if (ivarEnt.forceNoTo || ivarEnt.forceNoFrom) {
                switch (fallbackTy) {
                    case IvarPossFallbackType::IgnoreWeakDisable:
                    case IvarPossFallbackType::FinalOption:
                        break;
                    default:
                        return false;
                }
            }

            ASSERT_BUG(
                sp,
                std::all_of(
                    possibleTys.begin(),
                    possibleTys.end(),
                    [](const PossibleType& ty) {
                return ty.hasType();
            }
                ),
                "Coercion barrier escaped into concrete type selection"
            );

            // - TODO: Should this also remove &_ types? (maybe not, as they give information about borrow classes)
            size_t nIvars;
            size_t nSrcIvars;
            size_t nDstIvars;
            bool possiblyDiverge = false;
            {
                nSrcIvars = 0;
                nDstIvars = 0;
                auto newEnd = std::remove_if(possibleTys.begin(), possibleTys.end(), [&](const PossibleType& ent) {
                    // TODO: Should this remove Unbound associated types too?
                    if ((ent.ty)->is_Infer()) {
                        if (ent.isSource()) {
                            nSrcIvars += 1;
                        } else {
                            nDstIvars += 1;
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

            const auto isFunctionSource = [](const PossibleType& possible) {
                return possible.isSource() && (((*possible.ty).is_NodeType() && ((*possible.ty).as_NodeType().is_Closure())) || possible.ty->is_NamedFunction());
            };
            const auto functionSourceCount = std::count_if(possibleTys.begin(), possibleTys.end(), isFunctionSource);
            if (functionSourceCount >= 2 && (fallbackTy == IvarPossFallbackType::FinalOption || nSrcIvars == 0) && std::all_of(possibleTys.begin(), possibleTys.end(), [&](const auto& possible) {
                return !possible.isSource() || isFunctionSource(possible);
            })) {
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
                            candidate.argTypes.push_back(argument.second);
                        }
                    } else {
                        BUG(sp, "");
                    }

                    if (target) {
                        target->isUnsafe |= candidate.isUnsafe;
                    } else {
                        target = std::move(candidate);
                    }
                }
                auto newTy = context.crate.types.function(std::move(*target));
                if (!coercionCandidateIsInvalid(sp, context, coercionRefs, tyL, newTy)) {
                    context.equateTypes(sp, tyL, newTy);
                    return true;
                }
            }

            // TODO: Do the oposite for the destination types (least permissive pointer, pick any Sized type)
            if (nSrcIvars == 0 || fallbackTy == IvarPossFallbackType::Assume) {
                const HIRTypeData* ptrTy = nullptr;
                if (std::any_of(possibleTys.begin(), possibleTys.end(), [&](const auto& ent) {
                    return ent.cls == PossibleType::CoerceFrom;
                })) {
                    for (const auto& ent : possibleTys) {
                        if (!ent.isSource()) {
                            continue;
                        }

                        bool unusedUnordered = false;
                        if (ptrTy == nullptr) {
                            ptrTy = ent.ty;
                        } else if (TypeRestrictiveOrdering::getOrderingPtr(sp, context, ent.ty, ptrTy, unusedUnordered, /*deep=*/false) == OrdLess) {
                            ptrTy = ent.ty;
                        } else {
                        }
                    }
                }

                for (const auto& ent : possibleTys) {
                    if (!ent.isSource()) {
                        continue;
                    }
                    const HIRTypeData* innerTy = (ptrTy ? TypeRestrictiveOrdering::matchAndExtractPtrTy(ptrTy, ent.ty) : ent.ty);
                    if (!innerTy) {
                        continue;
                    }

                    bool isMaxAccepting = false;
                    if ((innerTy)->is_Slice()) {
                        isMaxAccepting = true;
                    } else if (((*innerTy).is_Primitive() && ((*innerTy).as_Primitive() == HIRCoreType::Str))) {
                        isMaxAccepting = true;
                    } else {
                    }
                    if (isMaxAccepting) {
                        context.equateTypes(sp, tyL, ent.ty);
                        return true;
                    }
                }
            }

            if (std::all_of(possibleTys.begin(), possibleTys.end(), PossibleType::isCoerceS)) {
                size_t numDistinct = 0;
                for (const auto& ent : possibleTys) {
                    if (!ent.isDest()) {
                        continue;
                    }
                    if (((*ent.ty).is_Borrow() && ((*ent.ty).as_Borrow().inner->is_Infer()))) {
                        continue;
                    }
                    bool isDuplicate = false;
                    for (const auto& ent2 : possibleTys) {
                        if (&ent2 == &ent) {
                            break;
                        }
                        if (!ent2.isSource()) {
                            continue;
                        }
                        if (ent.ty == ent2.ty) {
                            isDuplicate = true;
                            break;
                        }
                        // TODO: Compare such that &[_; 1] == &[u8; 1]?
                    }
                    if (!isDuplicate) {
                        numDistinct += 1;
                    }
                }
                bool isUnordered = false;
                const HIRTypeData* destType = nullptr;
                for (const auto& ent : possibleTys) {
                    if (ent.isDest()) {
                        continue;
                    }
                    if (!destType) {
                        destType = ent.ty;
                        continue;
                    }

                    auto cmp = TypeRestrictiveOrdering::getOrderingPtr(sp, context, ent.ty, destType, isUnordered);
                    switch (cmp) {
                        case OrdLess:
                            break;
                        case OrdEqual:
                            break;
                        case OrdGreater:
                            destType = ent.ty;
                            isUnordered = false;
                            break;
                    }
                }
                // TODO: Unsized types? Don't pick an unsized if coercions are present?
                // TODO: If in a fallback mode, then don't require >1 (just require dest_type)
                if ((numDistinct > 1 || fallbackTy == IvarPossFallbackType::Assume) && destType && !isUnordered) {
                    context.equateTypes(sp, tyL, destType);
                    return true;
                }
            }

            if (std::all_of(possibleTys.begin(), possibleTys.end(), PossibleType::isCoerceS)) {
                size_t numDistinct = 0;
                for (const auto& ent : possibleTys) {
                    if (!ent.isSource()) {
                        continue;
                    }
                    if (((*ent.ty).is_Borrow() && ((*ent.ty).as_Borrow().inner->is_Infer()))) {
                        continue;
                    }
                    bool isDuplicate = false;
                    for (const auto& ent2 : possibleTys) {
                        if (&ent2 == &ent) {
                            break;
                        }
                        if (!ent2.isSource()) {
                            continue;
                        }
                        if (ent.ty == ent2.ty) {
                            isDuplicate = true;
                            break;
                        }
                        // TODO: Compare such that &[_; 1] == &[u8; 1]?
                    }
                    if (!isDuplicate) {
                        numDistinct += 1;
                    }
                }
                bool isUnordered = false;
                const HIRTypeData* destType = nullptr;
                for (const auto& ent : possibleTys) {
                    if (ent.isSource()) {
                        continue;
                    }
                    if (!destType) {
                        destType = ent.ty;
                        continue;
                    }

                    auto cmp = TypeRestrictiveOrdering::getOrderingPtr(sp, context, ent.ty, destType, isUnordered);
                    switch (cmp) {
                        case OrdLess:
                            destType = ent.ty;
                            isUnordered = false;
                            break;
                        case OrdEqual:
                            break;
                        case OrdGreater:
                            break;
                    }
                }
                // TODO: Unsized types? Don't pick an unsized if coercions are present?
                // TODO: If in a fallback mode, then don't require >1 (just require dest_type)
                if ((numDistinct > 1 || fallbackTy == IvarPossFallbackType::Assume) && destType && !isUnordered) {
                    context.equateTypes(sp, tyL, destType);
                    return true;
                }
            }

            // TODO: Remove any types that are covered by another type

            {
                for (auto it = possibleTys.begin(); it != possibleTys.end(); ++it) {
                    if (!it->isActive()) {
                        continue;
                    }
                    for (auto it2 = it + 1; it2 != possibleTys.end(); ++it2) {
                        if (!it2->isActive()) {
                            continue;
                        }
                        if (it->cls != it2->cls) {
                            continue;
                        }

                        switch (InfoOrdering::compareTop(context, it->ty, it2->ty, /*should_deref=*/it->isCoerce())) {
                            case InfoOrdering::Incompatible:
                                break;
                            case InfoOrdering::Less:
                                if (0) {
                                    case InfoOrdering::Same:
                                }
                                it->ty = it2->ty;
                                it2->remove();
                                break;
                            case InfoOrdering::More:
                                it2->remove();
                                break;
                        }
                    }
                }
                auto newEnd = std::remove_if(possibleTys.begin(), possibleTys.end(), [](const auto& e) {
                    return !e.isActive();
                });
                possibleTys.erase(newEnd, possibleTys.end());
            }

            // TODO: If in fallback mode, pick the most permissive option

            if (fallbackTy == IvarPossFallbackType::Assume) {
                if (std::all_of(possibleTys.begin(), possibleTys.end(), PossibleType::isCoerceS) && nIvars == 0) {
                    const HIRTypeData* destType = nullptr;
                    bool anyIvarPresent = false;
                    bool isUnordered = false;
                    for (const auto& ent : possibleTys) {
                        if (visitTyWith(ent.ty, [](const HIRTypeData* t) {
                            return t->is_Infer();
                        })) {
                            anyIvarPresent = true;
                        }
                        if (!destType) {
                            destType = ent.ty;
                            continue;
                        }

                        auto cmp = TypeRestrictiveOrdering::getOrderingPtr(sp, context, ent.ty, destType, isUnordered);
                        switch (cmp) {
                            case OrdLess:
                                destType = ent.ty;
                                isUnordered = false;
                                break;
                            case OrdEqual:
                                break;
                            case OrdGreater:
                                break;
                        }
                    }

                    if (destType && nIvars == 0 && anyIvarPresent == false && !((*destType).is_NodeType() && ((*destType).as_NodeType().is_Closure())) && !isUnordered) {
                        context.equateTypes(sp, tyL, destType);
                        return true;
                    }
                }
            }

            for (auto it = possibleTys.begin(); it != possibleTys.end();) {
                bool removeOption = false;
                if (it->ty == tyL) {
                    removeOption = true;
                } else if (!allowUnsized && context.resolve.typeIsSized(sp, it->ty) == HIRCompare::Unequal) {
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
            for (auto it = possibleTys.begin(); it != possibleTys.end();) {
                bool removeOption = false;
                for (const auto& otherOpt : possibleTys) {
                    if (&otherOpt == &*it) {
                        continue;
                    }
                    if (otherOpt.ty == it->ty) {
                        if (otherOpt.cls == it->cls) {
                            removeOption = true;
                            break;
                        }

                        // TODO: Ivars have been removed?
                        if (!(it->ty)->is_Infer() && otherOpt.isCoerce() == it->isCoerce() && otherOpt.isSource() != it->isSource()) {
                            // TODO: Possible duplicate with a check above...
                            context.equateTypes(sp, tyL, it->ty);
                            return true;
                        }
                        if (it->isSource() && otherOpt.isCoerce() == it->isCoerce()) {
                            removeOption = true;
                            break;
                        }
                    }
                }
                it = (removeOption ? possibleTys.erase(it) : it + 1);
            }

            for (auto it = possibleTys.begin(); it != possibleTys.end();) {
                bool removeOption = false;
                if (it->isSource() && !(it->ty)->is_Infer()) {
                    HIRTypeRef tmp, tmp2;
                    const auto* dty = it->ty;
                    auto srcBty = HIRBorrowType::Shared;
                    if (it->isCoerce()) {
                        if ((dty)->is_Borrow()) {
                            srcBty = (dty)->as_Borrow().type;
                        }
                        dty = context.resolve.autoderef(sp, dty, tmp);
                    }
                    if (dty) {
                        for (const auto& otherOpt : possibleTys) {
                            if (&otherOpt == &*it) {
                                continue;
                            }
                            if ((otherOpt.ty)->is_Infer()) {
                                continue;
                            }

                            const auto* oty = otherOpt.ty;
                            auto oBty = HIRBorrowType::Owned;
                            if (otherOpt.isCoerce()) {
                                if ((oty)->is_Borrow()) {
                                    oBty = (oty)->as_Borrow().type;
                                }
                                oty = context.resolve.autoderef(sp, oty, tmp2);
                            }
                            if (oBty > srcBty) {
                                break;
                            }
                            // TODO: Check if unsize is possible from `dty` to `oty`
                            if (oty) {
                                auto cmp = checkUnsizeTys(context, sp, oty, dty, nullptr);
                                if (cmp == CoerceResult::Equality) {
                                    //TODO(sp, "Impossibility for " << oty << " := " << dty);
                                } else if (cmp == CoerceResult::Unknown) {
                                } else {
                                    removeOption = true;
                                    break;
                                }
                            }
                        }
                    }
                }
                if (!removeOption && !(it->ty)->is_Infer() && coercionCandidateIsInvalid(sp, context, coercionRefs, tyL, it->ty)) {
                    removeOption = true;
                }
                it = (removeOption ? possibleTys.erase(it) : it + 1);
            }

            if (nSrcIvars == 0 && /*n_dst_ivars == 0 &&*/ possibleTys.empty() && possiblyDiverge && fallbackTy == IvarPossFallbackType::IgnoreWeakDisable) {
                if (context.crate.edition < ASTEdition::Rust2024) {
                    auto unit = context.crate.types.unit();
                    if (!coercionCandidateIsInvalid(sp, context, coercionRefs, tyL, unit)) {
                        context.equateTypes(sp, tyL, unit);
                        return true;
                    }
                }
                auto t = context.crate.types.diverge();
                if (!coercionCandidateIsInvalid(sp, context, coercionRefs, tyL, t)) {
                    context.equateTypes(sp, tyL, context.crate.types.diverge());
                    return true;
                }
            }

            for (const auto& e : possibleTys) {
                if (e.cls == PossibleType::CoerceFrom) {
                    HIRTypeRef tmp;
                    const auto* dty = context.resolve.autoderef(sp, e.ty, tmp);
                    if (dty && !(dty)->is_Infer()) {
                        for (const auto& e2 : possibleTys) {
                            if (e2.cls == PossibleType::UnsizeTo) {
                                if (context.ivars.typesEqual(dty, e2.ty)) {
                                    context.equateTypes(sp, tyL, e.ty);
                                    return true;
                                }
                            }
                        }
                    }
                }
            }

            if (possibleTys.size() == 1) {
                bool active = false;
                switch (fallbackTy) {
                    case IvarPossFallbackType::None:
                    case IvarPossFallbackType::Backwards:
                    case IvarPossFallbackType::IgnoreWeakDisable:
                        active = nIvars == 0;
                        break;
                    case IvarPossFallbackType::Assume:
                    case IvarPossFallbackType::FinalOption:
                        active = true;
                        break;
                }
                if (active) {
                    const auto* newTy = possibleTys[0].ty;
                    context.equateTypes(sp, tyL, newTy);
                    return true;
                }
            }
            if (!honourDisable) {
                if (nSrcIvars == 0 && std::count_if(possibleTys.begin(), possibleTys.end(), PossibleType::isSourceS) == 1) {
                    auto it = std::find_if(possibleTys.begin(), possibleTys.end(), PossibleType::isSourceS);
                    const auto* newTy = it->ty;
                    context.equateTypes(sp, tyL, newTy);
                    return true;
                }
                if (fallbackTy != IvarPossFallbackType::None && nDstIvars == 0 && std::count_if(possibleTys.begin(), possibleTys.end(), PossibleType::isDestS) == 1) {
                    auto it = std::find_if(possibleTys.begin(), possibleTys.end(), PossibleType::isDestS);
                    const auto* newTy = it->ty;
                    if (it->isCoerce()) {
                        context.equateTypes(sp, tyL, newTy);
                        return true;
                    } else {
                        // HACK: Work around failure in librustc
                    }
                }
            }
            if (possibleTys.size() > 0 && !honourDisable && nIvars == 0) {
                const auto* newTy = possibleTys.back().ty;
                context.equateTypes(sp, tyL, newTy);
                return true;
            }

            mayUseRawPointerFallback = possibleTys.empty() && nIvars == 0;
        }

        if (mayUseRawPointerFallback && fallbackTy == IvarPossFallbackType::FinalOption) {
            const HIRTypeData* selected = nullptr;
            bool conflicting = false;
            for (const auto& candidate : ivarEnt.rawPointerFallbacks) {
                const auto* type = context.getType(candidate);
                if (type == tyL || type->is_Infer() || coercionCandidateIsInvalid(sp, context, coercionRefs, tyL, type)) {
                    continue;
                }
                if (!selected) {
                    selected = type;
                } else if (!context.ivars.typesEqual(selected, type)) {
                    conflicting = true;
                    break;
                }
            }
            if (selected && !conflicting) {
                context.equateTypes(sp, tyL, selected);
                return true;
            }
        }

        return false;
    }
}

void TypecheckCodeCS(const TypeckModuleState& ms, tArgs& args, const HIRTypeData* resultType, HIRExprPtr& expr) {
    HIRExprNodeP rootPtr(expr.get());
    assert(!ms.modPaths.empty());
    Context context{ms.wb, ms.implGenerics, ms.itemGenerics, ms.modPaths.back(), ms.currentTrait, ms.currentTraitImpl};
    if (resultType) {
        visitTyWith(resultType, [&](const HIRTypeData* inner) {
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
        if (!context.ivars.peekChanged()) {
            for (size_t i = 0; i < context.linkCoerce.size();) {
                auto ent = mv$(context.linkCoerce[i]);
                const auto& span = (*ent->rightNodePtr)->span();
                auto& srcTy = (*ent->rightNodePtr)->resType;
                srcTy = context.expandAssociatedTypes(span, mv$(srcTy)); // TODO: This was commented, why?
                ent->leftTy = context.expandAssociatedTypes(span, mv$(ent->leftTy));
                if (checkCoerce(context, *ent)) {
                    context.linkCoerce.erase(context.linkCoerce.begin() + i);
                } else {
                    context.linkCoerce[i] = mv$(ent);
                    ++i;
                }
            }
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
                rule.stalledPossibilities = indexedRule.stalledPossibilities;

                if (associatedStillStalled(context, rule)) {
                    mergeAssociatedPossibilities(context, rule.stalledPossibilities);
                    context.storeAssociated(i, mv$(rule), indexedKey);
                    i++;
                    if (linkAssocIterLimit-- == 0) {
                        break;
                    }
                    continue;
                }

                for (auto& ty : rule.params.types) {
                    ty = context.expandAssociatedTypes(rule.span, mv$(ty));
                }
                if (rule.name != "") {
                    rule.leftTy = context.expandAssociatedTypes(rule.span, mv$(rule.leftTy));
                    // HACK: If the left type is `!`, remove the type bound
                }
                rule.implTy = context.expandAssociatedTypes(rule.span, mv$(rule.implTy));

                std::vector<Context::Associated::CapturedIvarPossible> capturedPossibilities;
                AssociatedCheckResult result;
                {
                    auto* previousSink = context.possibleIvarSink;
                    context.possibleIvarSink = &capturedPossibilities;
                    STD_DEFER {
                        context.possibleIvarSink = previousSink;
                    };
                    result = checkAssociated(context, rule);
                }
                mergeAssociatedPossibilities(context, capturedPossibilities);
                rule.isAmbiguous = result == AssociatedCheckResult::Ambiguous;

                if (result == AssociatedCheckResult::Complete) {
                    context.removeAssociated(i, indexedKey);
                } else {
                    if ((result == AssociatedCheckResult::Stalled || result == AssociatedCheckResult::Ambiguous) && setAssociatedStall(context, rule)) {
                        rule.stalledPossibilities = mv$(capturedPossibilities);
                    } else {
                        rule.stalledOn.clear();
                        rule.stalledPossibilities.clear();
                    }
                    context.storeAssociated(i, mv$(rule), indexedKey);
                    i++;
                }

                if (linkAssocIterLimit-- == 0) {
                    break;
                }
            }
        }
        if (!context.ivars.peekChanged()) {
            Vector<const HIRTypeData*> passStartIvars;
            passStartIvars.grow(context.ivars.ivars.size());
            for (unsigned int i = 0; i < context.ivars.ivars.size(); i++) {
                passStartIvars.pushBack(context.ivars.getType(i));
            }
            for (auto it = context.toVisit.begin(); it != context.toVisit.end();) {
                HIRExprNode& node = **it;
                ExprVisitorRevisit visitor{context, false, &passStartIvars};
                node.visit(visitor);
                if (visitor.nodeCompleted()) {
                    it = context.toVisit.erase(it);
                } else {
                    ++it;
                }
            }
            {
                std::vector<bool> advRevisitRemoveList;
                size_t len = context.advRevisits.size();
                for (size_t i = 0; i < len; i++) {
                    auto& ent = *context.advRevisits[i];
                    advRevisitRemoveList.push_back(ent.revisit(context, /*is_fallback=*/false));
                }
                for (size_t i = len; i--;) {
                    if (advRevisitRemoveList[i]) {
                        context.advRevisits.erase(context.advRevisits.begin() + i);
                    }
                }
            }
        }

        std::unique_ptr<IvarCoercionIndex> ivarCoercionIndex;
        if (!context.ivars.peekChanged()) {
            ivarCoercionIndex = std::make_unique<IvarCoercionIndex>(context);
        }

        if (!context.ivars.peekChanged()) {
            // TODO: De-duplicate this with the block ~80 lines below
            std::unique_ptr<IvarDependencyIndex> dependencyIndex;
            for (unsigned int sourcePass = 0; sourcePass < 2; sourcePass++) {
                for (unsigned int i = 0; i < context.possibleIvarVals.size(); i++) {
                    bool hasConcreteSource = false;
                    for (const auto& source : context.possibleIvarVals[i].typesCoerceFrom) {
                        if (!context.ivars.typeContainsIvars(context.getType(source.ty))) {
                            hasConcreteSource = true;
                            break;
                        }
                    }
                    if (hasConcreteSource != (sourcePass == 0)) {
                        continue;
                    }
                    if (checkIvarPoss(context, *ivarCoercionIndex, i, context.possibleIvarVals[i])) {
                        if (!dependencyIndex) {
                            dependencyIndex = std::make_unique<IvarDependencyIndex>(context);
                        }
                        dependencyIndex->disableDependents(i);
                    }
                }
            }
        }

        if (!context.ivars.peekChanged()) {
            for (unsigned int i = 0; i < context.possibleIvarVals.size(); i++) {
                if (checkIvarPoss(context, *ivarCoercionIndex, i, context.possibleIvarVals[i], IvarPossFallbackType::Backwards)) {
                    break;
                }
            }
        }

        if (!context.ivars.peekChanged()) {
            for (unsigned int i = 0; i < context.possibleIvarVals.size(); i++) {
                if (checkIvarPoss(context, *ivarCoercionIndex, i, context.possibleIvarVals[i], IvarPossFallbackType::Assume)) {
                    break;
                }
            }
        }

        if (!context.ivars.peekChanged()) {
            for (unsigned int i = 0; i < context.possibleIvarVals.size(); i++) {
                if (checkIvarPoss(context, *ivarCoercionIndex, i, context.possibleIvarVals[i], IvarPossFallbackType::IgnoreWeakDisable)) {
                    break;
                } else {
                }
            }
        }

        if (!context.ivars.peekChanged()) {
            for (auto it = context.toVisit.begin(); it != context.toVisit.end();) {
                HIRExprNode& node = **it;
                ExprVisitorRevisit visitor{context, true};
                node.visit(visitor);
                if (visitor.nodeCompleted()) {
                    it = context.toVisit.erase(it);
                } else {
                    ++it;
                }
            }
            {
                std::vector<bool> advRevisitRemoveList;
                size_t len = context.advRevisits.size();
                for (size_t i = 0; i < len; i++) {
                    auto& ent = *context.advRevisits[i];
                    advRevisitRemoveList.push_back(ent.revisit(context, /*is_fallback=*/true));
                }
                for (size_t i = len; i--;) {
                    if (advRevisitRemoveList[i]) {
                        context.advRevisits.erase(context.advRevisits.begin() + i);
                    }
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
            for (unsigned int i = 0; i < context.possibleIvarVals.size(); i++) {
                if (checkIvarPoss(context, *ivarCoercionIndex, i, context.possibleIvarVals[i], IvarPossFallbackType::FinalOption)) {
                    break;
                }
            }
        }

        if (!context.ivars.peekChanged()) {
            for (unsigned int i = 0; i < context.possibleIvarVals.size(); i++) {
                const auto& ent = context.possibleIvarVals[i];
                if (!ent.typesDefault.empty()) {
                    const auto& tyL = context.ivars.getType(i);

                    if (((*tyL).is_Infer() && ((*tyL).as_Infer().index == i))) {
                        if (ent.typesDefault.size() != 1) {
                            // TODO: Error?
                        } else {
                            context.equateTypes(rootPtr->span(), tyL, *ent.typesDefault.begin());
                        }
                    }
                }
            }
        }

        if (!context.ivars.peekChanged() && context.linkCoerce.empty()) {
            context.fallbackUnresolvedRpitType(rootPtr->span());
        }

        if (!context.ivars.peekChanged()) {
            if (!context.linkCoerce.empty()) {
                auto ent = mv$(context.linkCoerce.front());
                context.linkCoerce.erase(context.linkCoerce.begin());

                const auto& sp = (*ent->rightNodePtr)->span();
                auto& srcTy = (*ent->rightNodePtr)->resType;
                ent->leftTy = context.expandAssociatedTypes(sp, mv$(ent->leftTy));

                context.equateTypes(sp, ent->leftTy, srcTy);
            }
        }

        if (!context.ivars.peekChanged()) {
            bool appliedDefault = false;
            for (unsigned int i = 0; i < context.ivars.ivars.size(); i++) {
                appliedDefault |= context.ivars.applyDefault(i);
            }
            if (appliedDefault) {
                context.ivars.markChange();
            }
        }

        for (auto& ivarEnt : context.possibleIvarVals) {
            ivarEnt.reset();
        }

        count++;
        context.compactIvars();
    }
    if (count == MAX_ITERATIONS) {
        if (!context.hasRules()) {
            BUG(rootPtr->span(), "Typecheck ran for too many iterations, max - " << MAX_ITERATIONS);
        }
        WARNING(rootPtr->span(), W0000, "Typecheck ran for too many iterations, max - " << MAX_ITERATIONS);
    }

    if (context.hasRules()) {
        for (const auto& rule : context.linkAssoc) {
            if (!rule.isAmbiguous) {
                continue;
            }
            if (rule.name == "") {
                ERROR(rule.span, E0000, "type annotations needed: cannot infer a type satisfying `" << context.ivars.fmtType(rule.implTy) << ": " << rule.trait << context.ivars.fmt(rule.params) << "`");
            } else {
                ERROR(rule.span, E0000, "type annotations needed: cannot infer `" << context.ivars.fmtType(rule.leftTy) << " = <" << context.ivars.fmtType(rule.implTy) << " as " << rule.trait << context.ivars.fmt(rule.params) << ">::" << rule.name << "`");
            }
        }
        for (const auto& coercionP : context.linkCoerce) {
            const auto& coercion = *coercionP;
            const auto& sp = (**coercion.rightNodePtr).span();
            const auto& srcTy = (**coercion.rightNodePtr).resType;
            WARNING(sp, W0000, "Spare Rule - " << context.ivars.fmtType(coercion.leftTy) << " := " << context.ivars.fmtType(srcTy));
        }
        for (const auto& rule : context.linkAssoc) {
            const auto& sp = rule.span;
            if (rule.name == "") {
                WARNING(sp, W0000, "Spare Rule - " << context.ivars.fmtType(rule.implTy) << " : " << rule.trait << rule.params);
            } else {
                WARNING(sp, W0000, "Spare Rule - " << context.ivars.fmtType(rule.leftTy) << " = < " << context.ivars.fmtType(rule.implTy) << " as " << rule.trait << rule.params << " >::" << rule.name);
            }
        }
        // TODO: Print revisit rules and advanced revisit rules.
        for (const auto& node : context.toVisit) {
            const auto& sp = node->span();
            WARNING(sp, W0000, "Spare rule - " << FMT_CB(os, {
                                   ExprVisitorPrint ev(context, os);
                                   node->visit(ev);
                               }) << " -> " << context.ivars.fmtType(node->resType));
        }
        for (const auto& adv : context.advRevisits) {
            WARNING(adv->span(), W0000, "Spare Rule - " << FMT_CB(os, adv->fmt(os)));
        }
        BUG(rootPtr->span(), "Spare rules left after typecheck stabilised");
    }

    expr.reset(rootPtr.release());
    expr.bindings.reserve(context.bindings.size());
    for (auto& binding : context.bindings) {
        expr.bindings.push_back(binding.ty);
    }

    {
        ExprVisitorApply visitor{context};
        visitor.visitNodePtr(expr);
    }

    {
        StaticTraitResolve staticResolve(ms.wb);
        staticResolve.setBothGenericsRaw(ms.implGenerics, ms.itemGenerics);

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
                        BUG(sp, "Unresolved constant path " << path);
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
                ASSERT_BUG(sp, constant, "Constant path resolved to " << value.tagStr() << ": " << path);
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
                        BUG(node.span(), "Unresolved call path " << node.path);
                        break;
                    }
                }

                const bool hasUnevaluated = std::any_of(paramsPtr->values.begin(), paramsPtr->values.end(), [](const HIRConstGeneric& value) {
                    return value.is_Unevaluated();
                });
                if (!hasUnevaluated) {
                    return;
                }

                MonomorphState outParams(ms.crate.types);
                auto valRef = staticResolve.getValue(node.span(), node.path, outParams, /*signatureOnly=*/true, nullptr);
                const auto* fcn = valRef.opt_Function();
                ASSERT_BUG(node.span(), fcn, "Call path resolved to " << valRef.tagStr() << ": " << node.path);
                ConvertHIRConstantEvaluateMethodParams(node.span(), ms.wb, ms.crate, &(*fcn)->params, *paramsPtr);
            }

            void visit(HIRExprNodeCallMethod& node) override {
                HIRExprVisitorDef::visit(node);

                HIRPathParams* paramsPtr = nullptr;
                switch (node.methodPath.data.tag()) {
                    case HIRPathData::TAG_Generic: {
                        BUG(node.span(), "");
                        break;
                    }
                    case HIRPathData::TAG_UfcsUnknown: {
                        BUG(node.span(), "");
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
                assert(paramsPtr);

                bool found = false;
                for (auto& v : paramsPtr->values) {
                    if (v.is_Unevaluated()) {
                        found = true;
                    }
                }
                if (found) {
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

namespace {
    inline HIRSimplePath getRuleParentPath(const HIRSimplePath& sp) {
        return sp.parent();
    }

    inline HIRGenericPath getRuleParentPath(const HIRGenericPath& gp) {
        return HIRGenericPath(gp.path.parent(), gp.params.clone());
    }
}

bool visitCallPopulateCache(Context& context, const Span& sp, HIRPath& path, HIRExprCallCache& cache) __attribute__((warn_unused_result));
bool visitCallPopulateCacheUfcsInherent(Context& context, const Span& sp, HIRPath& path, HIRExprCallCache& cache, const HIRFunction*& fcnPtr);

bool inherentImplMatchesReceiver(Context& context, const Span& sp, const HIRTypeImpl& impl, const HIRTypeData* receiver, ThinVector<SolverTypeEquality>* equalities = nullptr) {
    HIRPathParams implParams;
    while (implParams.types.size() < impl.params.types.size()) {
        implParams.types.push_back(context.crate.types.infer());
    }
    implParams.values.resize(impl.params.values.size());

    OwnedImplMatcher matcher(context.crate.types, implParams);
    const auto match = impl.type->matchTestGenericsFuzz(sp, receiver, context.ivars.callbackResolveInfer(), matcher);
    if (match != HIRCompare::Fuzzy) {
        return match == HIRCompare::Equal;
    }

    const auto candidate = MonomorphStatePtr(context.crate.types, receiver, &implParams, nullptr).monomorphType(sp, impl.type, false);

    const bool hasDefiningOpaque = visitTyWith(receiver, [&](const HIRTypeData* type) {
        const auto* erased = type->opt_ErasedType();
        const auto* alias = erased ? erased->inner.opt_Alias() : nullptr;
        return alias && context.resolve.isOpaqueAliasDefiningScope(*alias->inner);
    });
    if (hasDefiningOpaque) {
        return true;
    }

    const auto matchRigid = [&](auto&& self, const HIRTypeData* left, const HIRTypeData* right) -> HIRCompare {
        const auto resolveCallerInfer = [&](const HIRTypeData* type) {
            const auto* infer = type->opt_Infer();
            return infer && infer->index == ~0u ? type : context.getType(type);
        };
        left = resolveCallerInfer(left);
        right = resolveCallerInfer(right);
        if (left == right) {
            return HIRCompare::Equal;
        }
        if (left->is_Infer() || right->is_Infer()) {
            const auto containsCandidateInfer = [](const HIRTypeData* type) {
                return visitTyWith(type, [](const HIRTypeData* inner) {
                    const auto* infer = inner->opt_Infer();
                    return infer && infer->index == ~0u;
                });
            };
            if (containsCandidateInfer(left) || containsCandidateInfer(right)) {
                return HIRCompare::Fuzzy;
            }
            if (equalities) {
                equalities->push_back(SolverTypeEquality{left, right});
            }
            return HIRCompare::Fuzzy;
        }
        const auto* leftPath = left->opt_Path();
        const auto* rightPath = right->opt_Path();
        if (!leftPath || !rightPath || leftPath->path.data.tag() != rightPath->path.data.tag()) {
            return left->compareWithPlaceholders(sp, right, context.ivars.callbackResolveInfer());
        }
        const auto matchParams = [&](const HIRPathParams& leftParams, const HIRPathParams& rightParams) {
            if (leftParams.types.size() != rightParams.types.size() || leftParams.values.size() != rightParams.values.size()) {
                return HIRCompare::Unequal;
            }
            auto result = HIRCompare::Equal;
            for (size_t i = 0; i < leftParams.types.size(); i++) {
                result &= self(self, leftParams.types[i], rightParams.types[i]);
                if (result == HIRCompare::Unequal) {
                    return result;
                }
            }
            for (size_t i = 0; i < leftParams.values.size(); i++) {
                if (leftParams.values[i] != rightParams.values[i]) {
                    result = HIRCompare::Fuzzy;
                }
            }
            return result;
        };
        if (const auto* leftGeneric = leftPath->path.data.opt_Generic()) {
            const auto& rightGeneric = rightPath->path.data.as_Generic();
            return leftGeneric->path == rightGeneric.path ? matchParams(leftGeneric->params, rightGeneric.params) : HIRCompare::Unequal;
        }
        if (const auto* leftProjection = leftPath->path.data.opt_UfcsKnown()) {
            const auto& rightProjection = rightPath->path.data.as_UfcsKnown();
            if (leftProjection->trait.path != rightProjection.trait.path || leftProjection->item != rightProjection.item) {
                return HIRCompare::Unequal;
            }
            auto result = self(self, leftProjection->type, rightProjection.type);
            result &= matchParams(leftProjection->trait.params, rightProjection.trait.params);
            result &= matchParams(leftProjection->params, rightProjection.params);
            return result;
        }
        return left->compareWithPlaceholders(sp, right, context.ivars.callbackResolveInfer());
    };
    if (matchRigid(matchRigid, candidate, receiver) == HIRCompare::Unequal) {
        return false;
    }

    const bool hasRigidOpaque = visitTyWith(receiver, [&](const HIRTypeData* type) {
        const auto* erased = type->opt_ErasedType();
        const auto* alias = erased ? erased->inner.opt_Alias() : nullptr;
        return alias && !context.resolve.isOpaqueAliasDefiningScope(*alias->inner);
    });
    if (!hasRigidOpaque) {
        return true;
    }

    return candidate->compareWithPlaceholders(sp, receiver, context.ivars.callbackResolveInfer()) != HIRCompare::Unequal;
}

void populateDefaults(const Span& sp, Context& context, const MonomorphStatePtr& ms, const HIRGenericParams& paramDefs, HIRPathParams& params) {
    for (size_t i = 0; i < paramDefs.types.size(); i++) {
        const auto& ty = params.types[i];
        const auto& typ = paramDefs.types[i];
        if (const auto* te = ty->opt_Infer()) {
            if (!typ.defaultValue->is_Infer()) {
                if (auto* ent = context.getIvarPossibilities(sp, te->index)) {
                    auto defTy = ms.monomorphType(sp, typ.defaultValue);
                    ent->typesDefault.insert(std::move(defTy));
                }
            }
        }
    }
}

template <typename T>
void fix_param_count_(const Span& sp, Context& context, const HIRTypeData* selfTy, bool useDefaults, const T& path, const HIRGenericParams& paramDefs, HIRPathParams& params) {
    if (params.types.size() == paramDefs.types.size()) {
    } else if (params.types.size() > paramDefs.types.size()) {
        while (params.types.size() > paramDefs.types.size() && params.values.size() < paramDefs.values.size() && params.types.back()->is_Infer()) {
            params.types.pop_back();
            params.values.push_back({});
            context.ivars.addIvars(params.values.back());
        }
        if (params.types.size() > paramDefs.types.size()) {
            ERROR(sp, E0000, "Too many type parameters passed to " << path);
        }
    } else {
        while (params.types.size() < paramDefs.types.size()) {
            const auto& typ = paramDefs.types[params.types.size()];
            if (useDefaults) {
                if (typ.defaultValue->is_Infer()) {
                    ERROR(sp, E0000, "Omitted type parameter with no default in " << path);
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
        ERROR(sp, E0000, "Too many const parameters passed to " << path);
    } else {
        while (params.values.size() < paramDefs.values.size()) {
            params.values.push_back({});
            context.ivars.addIvars(params.values.back());
        }
    }
}

void fixParamCount(const Span& sp, Context& context, const HIRTypeData* selfTy, bool useDefaults, const HIRPath& path, const HIRGenericParams& paramDefs, HIRPathParams& params) {
    fix_param_count_(sp, context, selfTy, useDefaults, path, paramDefs, params);
}

void fixParamCount(const Span& sp, Context& context, const HIRTypeData* selfTy, bool useDefaults, const HIRGenericPath& path, const HIRGenericParams& paramDefs, HIRPathParams& params) {
    fix_param_count_(sp, context, selfTy, useDefaults, path, paramDefs, params);
}

void applyBoundsAsRulesTrait(Context& context, const Span& sp, const HIRTypeData* realType, const HIRTraitPath& traitPath) {
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
    for (const auto& bound : paramsDef.bounds) {
        switch (bound.tag()) {
            case HIRGenericBound::TAG_TraitBound: {
                auto& be = bound.as_TraitBound();
                auto realType = ms.monomorphType(sp, be.type);
                auto realTrait = ms.monomorphTraitpath(sp, be.trait, false);
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
            HIRTypeRef ty = context.crate.types.generic("", (isImplLevel ? 0 : 256) + i);
            context.requireSized(sp, ms.getType(Span(), ty->as_Generic()));
        }
    }
}

/// TODO: If the function has multiple mismatched options, tell the caller to try again later?
bool visitCallPopulateCache(Context& context, const Span& sp, HIRPath& path, HIRExprCallCache& cache) {
    assert(cache.argTypes.size() == 0);

    const HIRFunction* fcnPtr = nullptr;

    struct Monomorph: public Monomorphiser {
        Context& context;
        const HIRTypeData* selfTy;
        HIRPathParams implParams;
        bool hasImplParams;
        const HIRPathParams& fcnParams;
        const HIRPathParams hrlParams;

        Monomorph(Context& context, const HIRTypeData* selfTy, const HIRPathParams* implParams, const HIRPathParams& fcnParams, HIRPathParams hrlParams)
            : Monomorphiser(context.crate.types)
            , context(context)
            , selfTy(selfTy)
            , implParams(implParams ? implParams->clone() : HIRPathParams())
            , hasImplParams(implParams != nullptr)
            , fcnParams(fcnParams)
            , hrlParams(std::move(hrlParams))
        {
        }

        HIRTypeRef getType(const Span& sp, const HIRGenericRef& e) const override {
            if (e.name == "Self" || e.isSelf()) {
                if (selfTy) {
                    return selfTy;
                } else {
                    TODO(sp, "Handle 'Self' when monomorphising");
                }
            } else if (e.binding < 256) {
                if (hasImplParams) {
                    auto idx = e.idx();
                    ASSERT_BUG(sp, idx < implParams.types.size(), "Generic param (impl) out of input range - " << e << " >= " << implParams.types.size());
                    return context.getType(implParams.types[idx]);
                } else {
                    BUG(sp, "Impl-level parameter on free function (" << e << ")");
                }
            } else if (e.binding < 512) {
                auto idx = e.idx();
                ASSERT_BUG(sp, idx < fcnParams.types.size(), "Generic param out of input range - " << e << " >= " << fcnParams.types.size());
                return context.getType(fcnParams.types[idx]);
            } else if (e.group() == GENERICHrtb) {
                return context.crate.types.generic(e.name, e.binding);
            } else {
                BUG(sp, "Generic binding out of total range (" << e << ")");
            }
        }

        HIRConstGeneric getValue(const Span& sp, const HIRGenericRef& e) const override {
            if (e.binding < 256) {
                ASSERT_BUG(sp, hasImplParams, "Impl-level value parameter on free function (" << e << ")");
                auto idx = e.idx();
                ASSERT_BUG(sp, idx < implParams.values.size(), "Generic value (impl) out of input range - " << e << " >= " << implParams.values.size());
                return context.ivars.getValue(implParams.values[idx]).clone();
            } else if (e.binding < 512) {
                auto idx = e.idx();
                ASSERT_BUG(sp, idx < fcnParams.values.size(), "Generic value out of input range - " << e << " >= " << fcnParams.values.size());
                return context.ivars.getValue(fcnParams.values[idx]).clone();
            } else if (e.group() == GENERICHrtb) {
                return e;
            } else {
                BUG(sp, "Generic value bounding out of total range (" << e << ")");
            }
        }
    };

    cache.topParams = nullptr;
    switch (path.data.tag()) {
        case HIRPathData::TAG_Generic: {
            auto& e = path.data.as_Generic();
            const auto& fcn = context.crate.getFunctionByPath(sp, e.path);
            fixParamCount(sp, context, HIRTypeRef(), false, path, fcn.params, e.params);
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
                BUG(sp, "Method '" << e.item << "' of trait " << e.trait.path << " doesn't exist");
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
                context.resolve.solveTraitGoal(sp, e.trait.path, e.trait.params, e.type, [&](SolverResponse response) {
                    if (!response.hasImpl || response.certainty != SolverCertainty::Proven || !response.impl || response.impl->ambiguousIdentity) {
                        return false;
                    }
                    auto selected = response.impl->legacy();
                    if (!selected.data.is_TraitImpl()) {
                        return false;
                    }
                    auto& implData = selected.data.as_TraitImpl();
                    auto method = implData.impl->methods.find(e.item);
                    if (method != implData.impl->methods.end() && method->second.data.traitReturnType) {
                        fcnPtr = &method->second.data;
                        cache.fcnParams = &fcnPtr->params;
                        cache.topParams = &implData.impl->params;
                        selectedImplParams = implData.implParams.clone();
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
            TODO(sp, "Hit a UfcsUnknown (" << path << ") - Is this an error?");
            break;
        }
        case HIRPathData::TAG_UfcsInherent: {
            if (!visitCallPopulateCacheUfcsInherent(context, sp, path, cache, fcnPtr)) {
                return false;
            }
            break;
        }
    }

    assert(fcnPtr);
    cache.fcn = fcnPtr;
    const auto& fcn = *fcnPtr;
    cache.monomorph->setConstevalState(context.resolve.board(), HIRItemPath(path));
    const auto& monomorph = *cache.monomorph;

    for (size_t i = 0; i < fcn.fixedArgCount(); i++) {
        const auto& arg = fcn.args[i];
        cache.argTypes.push_back(monomorph.monomorphType(sp, arg.second, false));
    }
    {
        auto returnType = monomorph.monomorphType(sp, fcn.returnType, false);
        context.noteRpitSelfReferences(returnType);
        if (const auto* traitCall = path.data.opt_UfcsKnown()) {
            if (const auto* erased = returnType->opt_ErasedType()) {
                if (const auto* origin = erased->inner.opt_Fcn()) {
                    auto name = RcString::newInterned(FMT(ATY_PREFIX_ERASED << traitCall->item << "_" << origin->index));
                    const auto& trait = context.crate.getTraitByPath(sp, traitCall->trait.path);
                    if (trait.types.find(name) != trait.types.end()) {
                        returnType = context.crate.types.path(HIRPath(traitCall->type, traitCall->trait.clone(), name, traitCall->params.clone()), {});
                    }
                }
            }
        }
        cache.argTypes.push_back(std::move(returnType));
    }

    if (cache.topParams) {
        applyBoundsAsRules(context, sp, *cache.topParams, monomorph, /*is_impl_level=*/true);
    }
    applyBoundsAsRules(context, sp, *cache.fcnParams, monomorph, /*is_impl_level=*/false);

    return true;
}

bool visitCallPopulateCacheUfcsInherent(Context& context, const Span& sp, HIRPath& path, HIRExprCallCache& cache, const HIRFunction*& fcnPtr) {
    auto& e = path.data.as_UfcsInherent();
    context.selectWellFormed(sp, e.type);
    auto lookupType = context.revealOpaqueTypes(e.type);
    lookupType = context.expandAssociatedTypes(sp, mv$(lookupType));
    e.type = lookupType;

    const HIRTypeImpl* implPtr = nullptr;
    ThinVector<SolverTypeEquality> implEqualities;
    unsigned int count = 0;
    context.crate.findTypeImpls(lookupType, context.ivars.callbackResolveInfer(), [&](const auto& impl) {
        ThinVector<SolverTypeEquality> candidateEqualities;
        const bool matches = inherentImplMatchesReceiver(context, sp, impl, lookupType, &candidateEqualities);
        if (!matches) {
            return false;
        }
        auto it = impl.methods.find(e.item);
        if (it == impl.methods.end()) {
            return false;
        }
        fcnPtr = &it->second.data;
        implPtr = &impl;
        implEqualities = std::move(candidateEqualities);
        count++;
        return false;
    });
    if (!fcnPtr) {
        ERROR(sp, E0000, "Failed to locate function " << path);
    }
    if (count > 1) {
        return false;
    }
    assert(implPtr);
    for (const auto& equality : implEqualities) {
        context.equateTypes(sp, equality.left, equality.right);
    }
    fixParamCount(sp, context, e.type, false, path, fcnPtr->params, e.params);
    cache.fcnParams = &fcnPtr->params;

    auto& implParams = e.implParams;
    if (implPtr->params.isGeneric()) {
        while (implParams.types.size() < implPtr->params.types.size()) {
            implParams.types.push_back(context.crate.types.infer());
        }
        implParams.values.resize(implPtr->params.values.size());
        OwnedImplMatcher matcher(context.crate.types, implParams);

        auto cmp = implPtr->type->matchTestGenericsFuzz(sp, lookupType, context.ivars.callbackResolveInfer(), matcher);
        if (cmp == HIRCompare::Fuzzy) {
            for (auto& ty : implParams.types) {
                if (ty->is_Infer() && ty->as_Infer().index == ~0u) {
                    ty = context.ivars.newIvarTr();
                }
            }

            context.ivars.addIvarsParams(implParams);

            // TODO: Use a copy of `MonomorphStatePtr` that calls `context.get_type`
            auto implMonomorphCb = MonomorphStatePtr(context.crate.types, e.type, &implParams, nullptr);
            auto implTyMono = implMonomorphCb.monomorphType(sp, implPtr->type, false);

            context.equateTypes(sp, implTyMono, e.type);
        } else if (cmp == HIRCompare::Unequal) {
            BUG(sp, "Failed to match inherent impl?!");
        } else {
            context.ivars.addIvarsParams(implParams);
        }

        for (auto& ty : implParams.types) {
            if (ty->is_Infer() && ty->as_Infer().index == ~0u) {
                ty = context.ivars.newIvarTr();
            }
        }
    }

    const auto& fcnParams = e.params;
    // TODO: Use a copy of `MonomorphStatePtr` that calls `context.get_type`
    cache.monomorph.reset(new MonomorphStatePtr(context.crate.types, e.type, &implParams, &fcnParams));

    applyBoundsAsRules(context, sp, implPtr->params, *cache.monomorph, /*is_impl_level=*/true);

    {
        HIRTypeRef tmp;
        const auto& implTyM = cache.monomorph->maybeMonomorphType(sp, tmp, implPtr->type);

        context.equateTypes(sp, e.type, implTyM);
    }
    e.type = context.revealOpaqueTypes(e.type);

    return true;
}

void TypecheckCodeCSEnumerateRules(Context& context, const TypeckModuleState& ms, tArgs& args, const HIRTypeData* resultType, HIRExprPtr& expr, HIRExprNodeP& rootPtr) {
    const Span& sp = rootPtr->span();

    ExprVisitorTagStaleIvars(context.crate.types).visitNodePtr(rootPtr);

    for (auto& arg : args) {
        context.handlePattern(Span(), arg.first, arg.second);
    }

    struct M: public Monomorphiser {
        Context& context;
        HIRExprPtr& expr;
        mutable const HIRTypeData* curSelf;
        mutable const HIRPathParams* hrls;

        M(Context& context, HIRExprPtr& expr)
            : Monomorphiser(context.crate.types)
            , context(context)
            , expr(expr)
            , curSelf(nullptr)
            , hrls(nullptr)
        {
        }

        HIRTypeRef getType(const Span& sp, const HIRGenericRef& g) const override {
            if (g.binding == GENERICErasedSelf && curSelf) {
                return curSelf;
            }
            return context.crate.types.generic(g.name, g.binding);
        }

        HIRConstGeneric getValue(const Span& sp, const HIRGenericRef& g) const override {
            return g;
        }

        HIRTypeRef monomorphType(const Span& sp, const HIRTypeData* tpl, bool allowInfer = true) const override {
            if (const auto* e = tpl->opt_ErasedType()) {
                if (const auto* ee = e->inner.opt_Fcn()) {
                    if (expr.erasedTypes.size() <= ee->index) {
                        expr.erasedTypes.resize(ee->index + 1);
                    }
                    ASSERT_BUG(sp, expr.erasedTypes[ee->index] == HIRTypeRef(), "Multiple-visits to erased type #" << ee->index);
                    expr.erasedTypes[ee->index] = context.ivars.newIvarTr();
                    auto rv = expr.erasedTypes[ee->index];
                    context.addRpitType(ee->origin, ee->index, rv);

                    auto prevCurSelf = this->curSelf;
                    this->curSelf = rv;

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

    HIRTypeRef newResTy = resultType ? M(context, expr).monomorphType(sp, resultType) : context.ivars.newIvarTr();
    for (size_t i = 0; i < expr.erasedTypes.size(); i++) {
        ASSERT_BUG(sp, expr.erasedTypes[i] != HIRTypeRef(), "Non-visited erased type #" << i);
    }

    if (true) {
        ExprVisitorAddIvars visitor(context);
        context.addIvars(rootPtr->resType);
        rootPtr->visit(visitor);
    }

    context.recordCoercionHint(newResTy, rootPtr);
    ExprVisitorEnum visitor(context, ms.traits, newResTy);
    context.addIvars(rootPtr->resType);
    rootPtr->visit(visitor);

    context.equateTypesCoerce(sp, newResTy, rootPtr);
}

Context::IVarPossible::CoerceTy::CoerceTy(HIRTypeRef ty, bool isCoerce)
    : op(isCoerce ? Coercion : Unsizing)
    , ty(ty)
{
}

void Context::IVarPossible::reset() {
    this->forceDisable = false;
    this->forceNoTo = false;
    this->forceNoFrom = false;
    this->typesCoerceTo.clear();
    this->typesCoerceFrom.clear();
    this->rawPointerFallbacks.clear();
}

bool Context::IVarPossible::hasRules() const {
    if (forceDisable) {
        return true;
    }
    if (forceNoTo || !typesCoerceTo.empty()) {
        return true;
    }
    if (forceNoFrom || !typesCoerceFrom.empty()) {
        return true;
    }
    if (!rawPointerFallbacks.empty()) {
        return true;
    }
    return false;
}

void Context::IVarPossible::mergeFrom(const IVarPossible& source) {
    forceDisable |= source.forceDisable;
    forceNoTo |= source.forceNoTo;
    forceNoFrom |= source.forceNoFrom;

    auto mergeCoercions = [](auto& destination, const auto& values) {
        for (const auto& value : values) {
            const auto found = std::find_if(destination.begin(), destination.end(), [&](const auto& existing) {
                return existing.op == value.op && existing.ty == value.ty;
            });
            if (found == destination.end()) {
                destination.push_back(value);
            }
        }
    };
    mergeCoercions(typesCoerceTo, source.typesCoerceTo);
    mergeCoercions(typesCoerceFrom, source.typesCoerceFrom);
    typesDefault.insert(source.typesDefault.begin(), source.typesDefault.end());
    rawPointerFallbacks.insert(source.rawPointerFallbacks.begin(), source.rawPointerFallbacks.end());
}

Context::TaitEntry::TaitEntry(const HIRPathParams& p, HIRTypeRef t)
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
            visitTyWith(entry.second.data, [&](const HIRTypeData* type) {
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

const HIRTypeData* Context::revealOpaqueType(const HIRTypeData* type) const {
    type = ivars.getType(type);
    const size_t maxDepth = 1 + erasedTypeAliases.size() + rpitTypes.size() + crate.opaqueTypeDefiners.size();
    for (size_t depth = 0; depth < maxDepth; depth++) {
        const HIRTypeData* hiddenType = nullptr;
        auto revealRpit = [&](const HIRPath& origin, unsigned int index) {
            for (const auto& entry : rpitTypes) {
                if (entry.index != index) {
                    continue;
                }
                RpitOriginMonomorph monomorph(crate.types);
                if (monomorph.cmpPath(Span(), *entry.origin, origin, ivars.callbackResolveInfer()) != HIRCompare::Unequal) {
                    hiddenType = monomorph.monomorphType(Span(), ivars.getType(entry.ourType));
                    break;
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
                    const auto expectedName = RcString::newInterned(FMT(ATY_PREFIX_ERASED << origin->item << "_" << entry.index));
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

HIRTypeRef Context::revealOpaqueTypes(const HIRTypeData* type) const {
    struct Visitor: HIRVisitor {
        const Context& context;

        explicit Visitor(const Context& context)
            : HIRVisitor(nullptr, context.crate.types)
            , context(context)
        {
        }

        [[nodiscard]] HIRTypeRef visitType(HIRTypeRef type) override {
            return HIRVisitor::visitType(context.revealOpaqueType(type));
        }
    } visitor{*this};

    return visitor.visitType(type);
}

void Context::addRpitType(const HIRPath& origin, unsigned int index, HIRTypeRef type) {
    for (const auto& entry : rpitTypes) {
        if (entry.index == index && *entry.origin == origin) {
            ASSERT_BUG(Span(), entry.ourType == type, "RPIT hidden type registered twice for " << origin << "#" << index);
            return;
        }
    }
    rpitTypes.push_back(RpitEntry{&origin, index, type, false});
}

void Context::noteRpitSelfReferences(const HIRTypeData* type) {
    visitTyWith(type, [this](const HIRTypeData* inner) {
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
        equateTypes(sp, hiddenType, crate.types.unit());
        return true;
    }
    return false;
}

const HIRTypeData* Context::coercionHint(const HIRExprNode& node) const {
    const auto it = coercionHints.find(&node);
    return it == coercionHints.end() ? nullptr : it->second;
}

std::ostream& operator<<(std::ostream& os, const Context::Coercion& v) {
    os << "R" << v.ruleIdx << " " << v.leftTy << " := " << v.rightNodePtr << " " << &**v.rightNodePtr << " (" << (*v.rightNodePtr)->resType << ")";
    return os;
}

std::ostream& operator<<(std::ostream& os, const Context::Associated& v) {
    os << "R" << v.ruleIdx << " ";
    if (v.name == "") {
        os << "req ty " << v.implTy << " impl " << v.trait << v.params;
    } else {
        os << v.leftTy << " = " << "< `" << v.implTy << "` as `" << v.trait << v.params << "` >::" << v.name << v.atyPp;
    }
    if (v.isOperator) {
        os << " - op";
    }
    return os;
}

MonomorphEraseHrls::MonomorphEraseHrls(HIRTypeInterner& types)
    : Monomorphiser(types)
{
}

auto MonomorphEraseHrls::getType(const Span& sp, const HIRGenericRef& g) const -> HIRTypeRef {
    return types.generic(g.name, g.binding);
}

auto MonomorphEraseHrls::getValue(const Span& sp, const HIRGenericRef& g) const -> HIRConstGeneric {
    return g;
}

auto ExprVisitorRevisit::nodeDiverges(const HIRExprNode& node) const -> bool {
    return node.diverges || this->context.getType(node.resType)->is_Diverge();
}

ExprVisitorRevisit::ExprVisitorRevisit(Context& context, bool fallback, const Vector<const HIRTypeData*>* passStartIvars)
    : context(context)
    , completed(false)
    , isFallback(fallback)
    , passStartIvars(passStartIvars)
{
}

auto ExprVisitorRevisit::nodeCompleted() const -> bool {
    return completed;
}

auto ExprVisitorRevisit::visit(HIRExprNodeBlock& node) -> void {
    assert(!node.nodes.empty());
    const auto& lastNode = *node.nodes.back();
    const auto& lastTy = this->context.getType(lastNode.resType);

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
                this->context.possibleEquateTypeUnknown(node.span(), node.resType, Context::IvarUnknownType::From);
                return;
        }
    } else if (lastTy->is_Diverge()) {
        diverges = true;
    } else {
        diverges = false;
    }
    if (diverges) {
        this->context.equateTypes(node.span(), node.resType, context.crate.types.diverge());
    } else {
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
        this->context.possibleEquateTypeUnknown(node.span(), node.resType, Context::IvarUnknownType::From);
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

auto ExprVisitorRevisit::bad_cast(const Span& sp, const HIRTypeData* srcTy, const HIRTypeData* tgtTy, const char* where) -> void {
    ERROR(
        sp,
        E0000,
        "Invalid cast [" << where << "]:\n"
                         << "from " << this->context.ivars.fmtType(srcTy) << "\n"
                         << " to  " << this->context.ivars.fmtType(tgtTy)
    );
}

auto ExprVisitorRevisit::equateFunctionSignature(const Span& sp, const HIRTypeDataFunctionPointer& dst, const HIRTypeDataFunctionPointer& src) -> void {
    this->context.equateTypes(sp, dst.rettype, src.rettype);
    for (size_t i = 0; i < dst.argTypes.size(); i++) {
        this->context.equateTypes(sp, dst.argTypes[i], src.argTypes[i]);
    }
}

auto ExprVisitorRevisit::visit(HIRExprNodeCast& node) -> void {
    const auto& sp = node.span();
    const auto& tgtTy = this->context.getType(node.resType);
    const auto& srcTy = this->context.getType(node.value->resType);

    if (this->context.ivars.typesEqual(srcTy, tgtTy)) {
        this->completed = true;
        return;
    }

    switch ((*tgtTy).tag()) {
        case HIRTypeData::TAG_Infer: {
            break;
        }
        case HIRTypeData::TAG_Diverge: {
            BUG(sp, "");
            break;
        }
        case HIRTypeData::TAG_Primitive: {
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
        case HIRTypeData::TAG_Path: {
            this->context.equateTypesCoerce(sp, tgtTy, node.value);
            this->completed = true;
            return;
        }
        case HIRTypeData::TAG_Generic: {
            TODO(sp, "_Cast Generic");
            break;
        }
        case HIRTypeData::TAG_TraitObject: {
            bad_cast(sp, srcTy, tgtTy, "dst");
            break;
        }
        case HIRTypeData::TAG_ErasedType: {
            bad_cast(sp, srcTy, tgtTy, "dst");
            break;
        }
        case HIRTypeData::TAG_Array: {
            this->context.equateTypesCoerce(sp, tgtTy, node.value);
            this->completed = true;
            return;
        }
        case HIRTypeData::TAG_Slice: {
            bad_cast(sp, srcTy, tgtTy, "dst");
            break;
        }
        case HIRTypeData::TAG_Pattern: {
            bad_cast(sp, srcTy, tgtTy, "dst");
            break;
        }
        case HIRTypeData::TAG_Tuple: {
            bad_cast(sp, srcTy, tgtTy, "dst");
            break;
        }
        case HIRTypeData::TAG_Borrow: {
            if (!this->isFallback && (srcTy->is_Infer() || (srcTy->is_Borrow() && this->context.getType(srcTy->as_Borrow().inner)->is_Infer()))) {
                return;
            }
            this->context.equateTypesCoerce(sp, tgtTy, node.value);
            this->completed = true;
            return;
        }
        case HIRTypeData::TAG_Pointer: {
            auto& e = (*tgtTy).as_Pointer();
            const auto& ity = this->context.getType(e.inner);
            switch ((*srcTy).tag()) {
                default:
                    ERROR(sp, E0000, "Invalid cast to pointer from " << srcTy);
                case HIRTypeData::TAG_Function:
                case HIRTypeData::TAG_NamedFunction:
                    // TODO: What is the valid set? *const () and *const u8 at least are allowed
                    if (ity == context.crate.types.unit() || ity == HIRCoreType::U8 || ity == HIRCoreType::I8) {
                        this->completed = true;
                    } else if (ity->is_Infer()) {
                    } else {
                        // TODO: Only allow thin pointers? `c_void` is used in 1.74 libstd
                        this->completed = true;
                    }
                    break;
                case HIRTypeData::TAG_Primitive: {
                    auto& sE = (*srcTy).as_Primitive();
                    switch (sE) {
                        case HIRCoreType::Bool:
                        case HIRCoreType::Char:
                        case HIRCoreType::Str:
                        case HIRCoreType::F32:
                        case HIRCoreType::F64:
                            ERROR(sp, E0000, "Invalid cast to pointer from " << srcTy);
                        default:
                            break;
                    }
                    this->completed = true;
                    break;
                }
                case HIRTypeData::TAG_Infer: {
                    auto& sE = (*srcTy).as_Infer();
                    switch (sE.tyClass) {
                        case HIRInferClass::Float:
                            ERROR(sp, E0000, "Invalid cast to pointer from floating point literal");
                        case HIRInferClass::Integer:
                            this->context.equateTypes(sp, srcTy, context.crate.types.primitive(HIRCoreType::Usize));
                            this->completed = true;
                            break;
                        case HIRInferClass::None:
                            break;
                    }
                    break;
                }
                case HIRTypeData::TAG_Borrow: {
                    auto& sE = (*srcTy).as_Borrow();
                    if (!(sE.type >= e.type)) {
                        ERROR(sp, E0000, "Invalid cast from " << srcTy << " to " << tgtTy);
                    }
                    const auto& srcInner = this->context.getType(sE.inner);

                    if (const auto* sEI = srcInner->opt_Infer()) {
                        this->context.possibleEquateIvar(sp, sEI->index, ity, Context::PossibleTypeSource::UnsizeTo);
                    }
                    if (const auto* dEI = ity->opt_Infer()) {
                        this->context.possibleEquateIvar(sp, dEI->index, sE.inner, Context::PossibleTypeSource::UnsizeFrom);
                        if (!this->isFallback) {
                            this->context.possibleEquateIvarUnknown(sp, dEI->index, Context::IvarUnknownType::From);
                        }
                    }

                    if (srcInner->is_Array()) {
                        if (const auto* sEI = this->context.getType(srcInner->as_Array().inner)->opt_Infer()) {
                            this->context.possibleEquateIvar(sp, sEI->index, ity, Context::PossibleTypeSource::UnsizeTo);
                            return;
                        }
                    }

                    // TODO: Wouldn't this be better served by a coercion point?

                    if (srcInner->is_Infer() || ity->is_Infer()) {
                    } else if (srcInner->is_Array() && srcInner->as_Array().inner == ity) {
                        auto ty = context.crate.types.pointer(e.type, srcInner);
                        node.value = NEWNODE(ty, sp, Cast, mv$(node.value), ty);
                        this->completed = true;
                    } else {
                        bool found = !this->context.resolve.langUnsize().components().empty() && this->context.resolve.solveTraitGoal(sp, this->context.resolve.langUnsize(), HIRPathParams(e.inner), sE.inner, [](SolverResponse response) {
                            return response.hasImpl;
                        });
                        if (found) {
                            auto ty = context.crate.types.borrow(e.type, e.inner);
                            node.value = NEWNODE(ty, sp, Unsize, mv$(node.value), ty);
                            this->context.addTraitBound(sp, sE.inner, this->context.resolve.langUnsize(), HIRPathParams(e.inner));
                        } else {
                            this->context.equateTypes(sp, e.inner, sE.inner);
                        }
                        this->completed = true;
                    }
                    break;
                }
                case HIRTypeData::TAG_Pointer: {
                    auto& sE = (*srcTy).as_Pointer();

                    // TODO: In some rare cases, this ivar could be completely

                    const auto& dstInner = this->context.getType(e.inner);
                    const auto& srcInner = this->context.getType(sE.inner);
                    if (dstInner->is_Infer()) {
                        this->context.possibleEquateIvarRawPointerFallback(sp, dstInner->as_Infer().index, srcInner);
                        return;
                    } else if (srcInner->is_Infer()) {
                        if (!this->isFallback) {
                            this->context.possibleEquateIvarRawPointerFallback(sp, srcInner->as_Infer().index, dstInner);
                            return;
                        }
                        auto srcIdx = srcInner->as_Infer().index;
                        if (srcIdx < this->context.possibleIvarVals.size()) {
                            const auto& poss = this->context.possibleIvarVals[srcIdx];
                            if (!poss.typesCoerceTo.empty() || !poss.typesCoerceFrom.empty()) {
                                return;
                            }
                        }
                        this->context.equateTypes(sp, dstInner, srcInner);
                    } else {
                    }
                    this->completed = true;
                    break;
                }
            }
            break;
        }
        case HIRTypeData::TAG_Function: {
            auto& e = (*tgtTy).as_Function();
            switch ((*srcTy).tag()) {
                default:
                    bad_cast(sp, srcTy, tgtTy, "fcn src");
                case HIRTypeData::TAG_Infer: {
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
                case HIRTypeData::TAG_NodeType: {
                    auto& sE = (*srcTy).as_NodeType();
                    if (const auto* const* snPp = sE.opt_Closure()) {
                        if ((*snPp)->args.size() != e.argTypes.size()) {
                            bad_cast(sp, srcTy, tgtTy, "fcn nargs");
                        }
                        this->context.equateTypes(sp, e.rettype, (*snPp)->returnType);
                        for (size_t i = 0; i < e.argTypes.size(); i++) {
                            this->context.equateTypes(sp, e.argTypes[i], (*snPp)->args[i].second);
                        }
                        this->completed = true;
                    } else {
                        bad_cast(sp, srcTy, tgtTy, "fcn src");
                    }
                    break;
                }
                case HIRTypeData::TAG_Function: {
                    auto& sE = (*srcTy).as_Function();
                    if (sE.abi != e.abi || (sE.isUnsafe && sE.isUnsafe != e.isUnsafe) || sE.argTypes.size() != e.argTypes.size()) {
                        bad_cast(sp, srcTy, tgtTy, "fcn nargs");
                    }
                    equateFunctionSignature(sp, e, sE);
                    this->completed = true;
                    break;
                }
                case HIRTypeData::TAG_NamedFunction: {
                    auto& f = (*srcTy).as_NamedFunction();
                    auto ft = context.expandAssociatedTypes(sp, context.crate.types.function(f.decay(context.crate.types, sp)));
                    const auto& sE = ft->as_Function();
                    if (sE.abi != e.abi || (sE.isUnsafe && sE.isUnsafe != e.isUnsafe) || sE.argTypes.size() != e.argTypes.size()) {
                        bad_cast(sp, srcTy, tgtTy, "fcn nargs");
                    }
                    equateFunctionSignature(sp, e, sE);
                    this->completed = true;
                    break;
                }
            }
            break;
        }
        case HIRTypeData::TAG_NamedFunction: {
            BUG(sp, "Attempting to cast to a named-function type - impossible");
            break;
        }
        case HIRTypeData::TAG_NodeType: {
            BUG(sp, "Attempting to cast to a magic type type - impossible");
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

    this->context.possibleEquateTypeUnknown(node.span(), node.resType, Context::IvarUnknownType::From);

    unsigned int derefCount = 0;
    HIRTypeRef tmpType;
    const auto* currentTy = node.value->resType;
    std::vector<HIRTypeRef> derefResTypes;

    // TODO: (CHECK) rustc doesn't use the index value type when finding the indexable item, trustme does.
    HIRPathParams traitPp;
    traitPp.types.push_back(idxTy);
    do {
        const auto& ty = this->context.getType(currentTy);
        if (ty->is_Infer()) {
            return;
        }

        bool hasResponse = false;
        bool selected = false;
        this->context.resolve.solveTraitGoal(
            node.span(),
            langIndex,
            traitPp,
            ty,
            [&](SolverResponse response) {
            if (!response.hasImpl || !response.impl) {
                return false;
            }
            hasResponse = true;
            if (response.impl->ambiguousIdentity) {
                return false;
            }
            context.applySolverResponse(node.span(), response);
            selected = true;
            return true;
        },
            {
                .assocName = "Output",
                .assocType = node.resType,
                .allowInferInputs = true,
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
        currentTy = this->context.resolve.autoderef(node.span(), ty, tmpType);
        if (currentTy) {
            derefResTypes.push_back(currentTy);
        }
    } while (currentTy);

    if (currentTy) {
        assert(derefCount == derefResTypes.size());
        for (auto& tyR : derefResTypes) {
            auto ty = mv$(tyR);

            node.value = this->context.createAutoderef(mv$(node.value), mv$(ty));
            context.ivars.getType(node.value->resType);
        }

        completed = true;
    }
}

auto ExprVisitorRevisit::visit(HIRExprNodeDeref& node) -> void {
    const auto& ty = this->context.getType(node.value->resType);

    const auto& opTrait = this->context.crate.getLangItemPathOpt("deref");
    auto useBuiltin = [&](const HIRTypeData* inner) {
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
                ASSERT_BUG(node.span(), !opTrait.components().empty(), "Deref trait missing for non-builtin dereference of " << ty);
                useTrait();
            }
        } break;
        case HIRTypeData::TAG_Infer: {
            this->context.possibleEquateTypeUnknown(node.span(), node.resType, Context::IvarUnknownType::From);
            return;
        }
        case HIRTypeData::TAG_Borrow: {
            auto& e = (*ty).as_Borrow();
            useBuiltin(e.inner);
            break;
        }
        case HIRTypeData::TAG_Pointer: {
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
    ASSERT_BUG(sp, node.type == HIRExprNodeEmplace::Type::Boxer, "1.29 mode with non-box _Emplace node");
    ASSERT_BUG(sp, placerTy == context.crate.types.unit(), "1.29 mode with box in syntax - placer type is " << placerTy);

    ASSERT_BUG(sp, !langBoxed.components().empty(), "`owned_box` not present when `box` operator used");

    const auto& str = this->context.crate.getStructByPath(sp, langBoxed);
    // TODO: Store this type to avoid having to construct it every pass
    auto p = HIRGenericPath(langBoxed, {dataTy});
    p.params.types.push_back(MonomorphStatePtr(context.crate.types, nullptr, &p.params, nullptr).monomorphType(sp, str.params.types.at(1).defaultValue));
    this->context.addIvars(p.params.types.back());
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
        return;
    }
    assert(node.cache.argTypes.size() >= 1);
    unsigned int expArgc = node.cache.argTypes.size() - 1;
    if (node.args.size() != expArgc) {
        if (node.cache.fcn->variadic && node.args.size() > expArgc) {
        } else {
            ERROR(node.span(), E0000, "Incorrect number of arguments to " << node.path << " - exp " << expArgc << " got " << node.args.size());
        }
    }
    for (unsigned int i = 0; i < node.cache.argTypes.size() - 1; i++) {
        this->context.equateTypesCoerce(node.span(), node.cache.argTypes[i], node.args[i]);
    }
    this->context.equateTypes(node.span(), node.resType, node.cache.argTypes.back());
    this->context.requireSized(node.span(), node.resType);
    this->completed = true;
}

auto ExprVisitorRevisit::callAsyncCallable(HIRExprNodeCallValue& node, HIRTypeRef ty, const HIRPathParams& traitPp) -> bool {
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
        HIRTypeRef fcnArgsTup;
        const auto inspectImpl = [&](ImplRef impl) {
            auto tup = impl.getTraitTyParam(context.crate.types, 0);
            if (!tup->is_Tuple()) {
                ERROR(node.span(), E0000, "AsyncFn* expects a tuple argument, got " << tup);
            }
            fcnArgsTup = mv$(tup);
        };
        const bool found = this->context.resolve.solveTraitGoal(
            node.span(),
            candidate.trait,
            traitPp,
            ty,
            [&](SolverResponse response) {
            if (!response.hasImpl || !response.impl || response.impl->ambiguousIdentity) {
                return false;
            }
            inspectImpl(response.impl->legacy());
            context.applySolverResponse(node.span(), response);
            return true;
        },
            {
                .assocName = candidate.future,
                .assocType = node.resType,
                .allowInferInputs = true,
            }
        );
        if (!found) {
            continue;
        }
        node.argTypes = fcnArgsTup->as_Tuple();
        node.argTypes.push_back(node.resType);
        node.traitUsed = candidate.used;
        return true;
    }
    return false;
}

auto ExprVisitorRevisit::visit(HIRExprNodeCallValue& node) -> void {
    node.value->resType = this->context.expandAssociatedTypes(node.span(), node.value->resType);
    const auto& tyO = this->context.getType(node.value->resType);

    this->context.possibleEquateTypeUnknown(node.span(), node.resType, Context::IvarUnknownType::Bound);
    for (const auto& argTy : node.argIvars) {
        this->context.possibleEquateTypeUnknown(node.span(), argTy, Context::IvarUnknownType::To);
    }

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
        std::vector<HIRTypeRef> argTypes;
        for (const auto& argTy : node.argIvars) {
            argTypes.push_back(this->context.getType(argTy));
        }
        traitPp.types.push_back(context.crate.types.tuple(mv$(argTypes)));
    }

    unsigned int derefCount = 0;
    HIRTypeRef tmpType;
    const auto* ty = tyO;

    bool keepLooping = false;
    do {
        keepLooping = false;

        if (ty->is_NodeType() && ty->as_NodeType().is_Closure()) {
            const auto* nodeP = ty->as_NodeType().as_Closure();
            for (const auto& arg : nodeP->args) {
                node.argTypes.push_back(arg.second);
            }
            node.argTypes.push_back(nodeP->returnType);
            node.traitUsed = HIRExprNodeCallValue::TraitUsed::Unknown;
        } else if (ty->is_Function() || ty->is_NamedFunction()) {
            HIRTypeRef tmpFt;
            const auto* e = ty->opt_Function();
            if (!e) {
                tmpFt = context.crate.types.function(ty->as_NamedFunction().decay(context.crate.types, node.span()));
                tmpFt = this->context.expandAssociatedTypes(node.span(), tmpFt);
                e = &tmpFt->as_Function();
            }
            for (const auto& arg : e->argTypes) {
                node.argTypes.push_back(arg);
            }
            if (e->isVariadic) {
                for (size_t i = e->argTypes.size(); i < node.args.size(); i++) {
                    node.argTypes.push_back(node.argIvars[i]);
                }
            }
            node.argTypes.push_back(e->rettype);
            node.traitUsed = HIRExprNodeCallValue::TraitUsed::Fn;
        } else if (ty->is_Infer()) {
            return;
        } else if (const auto* e = ty->opt_Borrow()) {
            derefCount++;
            ty = this->context.getType(e->inner);
            keepLooping = true;
            continue;
        }
        // TODO: If autoderef is possible, do it and continue. Only look for impls once autoderef fails
        else {
            HIRTypeRef fcnArgsTup;
            HIRTypeRef fcnRet;

            // TODO: Use `find_trait_impls` instead of two different calls

            // TODO: Sometimes there's impls that just forward for wrappers, which can lead to incorrect rules

            bool found = false;
            bool ambiguous = false;
            const auto inspectImpl = [&](ImplRef impl) {
                auto tup = impl.getTraitTyParam(context.crate.types, 0);
                if (!tup->is_Tuple()) {
                    ERROR(node.span(), E0000, "FnOnce expects a tuple argument, got " << tup);
                }
                fcnArgsTup = mv$(tup);

                fcnRet = impl.getType(context.crate.types, "Output", {});
            };
            this->context.resolve.solveTraitGoal(
                node.span(),
                langFnOnce,
                traitPp,
                ty,
                [&](SolverResponse response) {
                if (!response.hasImpl || !response.impl) {
                    return false;
                }
                if (response.impl->ambiguousIdentity) {
                    ambiguous = true;
                    return false;
                }
                inspectImpl(response.impl->legacy());
                context.applySolverResponse(node.span(), response);
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
                if (fcnRet == HIRTypeRef()) {
                    fcnRet = context.crate.types.path(HIRPath(HIRPath::Data::make_UfcsKnown({ty, HIRGenericPath(langFnOnce, HIRPathParams(fcnArgsTup)), "Output", {}})), {});
                }
            } else if (const auto* e = ty->opt_Borrow()) {
                derefCount++;
                ty = this->context.getType(e->inner);
                keepLooping = true;
                continue;
            } else {
                if (const auto* nextTyP = this->context.resolve.autoderef(node.span(), ty, tmpType)) {
                    derefCount++;
                    ty = nextTyP;
                    keepLooping = true;
                    continue;
                }

                if (this->callAsyncCallable(node, ty, traitPp)) {
                    break;
                }

                ERROR(node.span(), E0000, "Unable to find an implementation of Fn*" << traitPp << " for " << this->context.ivars.fmtType(ty));
            }

            node.argTypes = fcnArgsTup->as_Tuple();
            node.argTypes.push_back(mv$(fcnRet));
        }
    } while (keepLooping);

    if (derefCount > 0) {
        ty = tyO;
        while (derefCount-- > 0) {
            const auto* nextTy = this->context.resolve.autoderef(node.span(), ty, tmpType);
            assert(nextTy);
            ty = nextTy;
            node.value = this->context.createAutoderef(mv$(node.value), ty);
        }
    }

    ASSERT_BUG(node.span(), node.argTypes.size() == node.args.size() + 1, "Malformed cache in CallValue: " << node.argTypes.size() << " != 1+" << node.args.size());
    for (unsigned int i = 0; i < node.args.size(); i++) {
        this->context.equateTypes(node.span(), node.argTypes[i], node.argIvars[i]);
    }
    this->context.equateTypes(node.span(), node.resType, node.argTypes.back());
    this->completed = true;
}

auto ExprVisitorRevisit::visit(HIRExprNodeCallMethod& node) -> void {
    const auto& sp = node.span();

    const auto& ty = this->context.getType(node.value->resType);

    this->context.possibleEquateTypeUnknown(node.span(), node.resType, Context::IvarUnknownType::From);
    for (const auto& argNode : node.args) {
        this->context.possibleEquateTypeUnknown(node.span(), argNode->resType, Context::IvarUnknownType::To);
    }

    if (!this->isFallback) {
        bool hasPendingReceiverCoercion = false;
        if (this->passStartIvars) {
            hasPendingReceiverCoercion = visitTyWith(ty, [&](const HIRTypeData* inner) {
                const auto* infer = inner->opt_Infer();
                return infer && infer->index < this->passStartIvars->length() && (*this->passStartIvars)[infer->index] != this->context.getType(inner);
            });
        }
        visitTyWith(ty, [&](const HIRTypeData* inner) {
            if (hasPendingReceiverCoercion) {
                return true;
            }
            const auto* resolved = this->context.getType(inner);
            const auto* infer = resolved->opt_Infer();
            if (!infer) {
                return false;
            }
            if (infer->index < this->context.possibleIvarVals.size()) {
                const auto& possible = this->context.possibleIvarVals[infer->index];
                hasPendingReceiverCoercion = !possible.typesCoerceTo.empty() || !possible.typesCoerceFrom.empty();
            }
            for (const auto& coercion : this->context.linkCoerce) {
                auto containsReceiverIvar = [&](const HIRTypeData* type) {
                    return visitTyWith(this->context.getType(type), [&](const HIRTypeData* candidate) {
                        const auto* candidateInfer = this->context.getType(candidate)->opt_Infer();
                        return candidateInfer && candidateInfer->index == infer->index;
                    });
                };
                if (containsReceiverIvar(coercion->leftTy) || containsReceiverIvar((*coercion->rightNodePtr)->resType)) {
                    hasPendingReceiverCoercion = true;
                    break;
                }
            }
            return hasPendingReceiverCoercion;
        });
        if (hasPendingReceiverCoercion) {
            return;
        }
    }

    // TODO: Obtain a list of avaliable methods at that level?

    std::vector<std::pair<TraitResolution::AutoderefBorrow, HIRPath>> possibleMethods;
    unsigned int derefCount = this->context.resolve.autoderefFindMethod(node.span(), node.traits, node.traitParamIvars, node.traitParamTypeIvars, ty, node.method, this->context.getType(node.resType), this->isFallback, possibleMethods);
    if ((derefCount == ~0u || possibleMethods.empty()) && node.method != node.fallbackMethod) {
        possibleMethods.clear();
        derefCount = this->context.resolve.autoderefFindMethod(node.span(), node.traits, node.traitParamIvars, node.traitParamTypeIvars, ty, node.fallbackMethod, this->context.getType(node.resType), this->isFallback, possibleMethods);
        if (derefCount != ~0u && !possibleMethods.empty()) {
            node.method = node.fallbackMethod;
        }
    }
tryAgain:
    if (derefCount != ~0u) {
        // HACK: In fallback mode, remove inherent impls from bounded ivars
        if (ty->is_Infer() && this->isFallback) {
            auto newEnd = std::remove_if(possibleMethods.begin(), possibleMethods.end(), [](const auto& e) {
                return e.second.data.is_UfcsInherent();
            });
            if (newEnd != possibleMethods.begin()) {
                possibleMethods.erase(newEnd, possibleMethods.end());
            }
        }
        if (possibleMethods.empty()) {
            ERROR(sp, E0000, "No applicable methods for {" << this->context.ivars.fmtType(ty) << "}." << node.method);
        }
        if (possibleMethods.size() > 1) {
            // TODO: What do do when there's multiple possibilities?

            for (auto it1 = possibleMethods.begin(); it1 != possibleMethods.end(); ++it1) {
                if (it1->first != possibleMethods.front().first) {
                    it1 = possibleMethods.erase(it1) - 1;
                }
            }
            for (auto it1 = possibleMethods.begin(); it1 != possibleMethods.end(); ++it1) {
                if (!it1->second.data.is_UfcsKnown()) {
                    continue;
                }

                auto& e1 = it1->second.data.as_UfcsKnown();
                for (auto it2 = it1 + 1; it2 != possibleMethods.end(); ++it2) {
                    if (!it2->second.data.is_UfcsKnown()) {
                        continue;
                    }
                    if (it2->second == it1->second) {
                        it2 = possibleMethods.erase(it2) - 1;
                        continue;
                    }
                    const auto& e2 = it2->second.data.as_UfcsKnown();

                    // TODO: If the trait is the same, but the type differs, pick the first?
                    if (e1.trait == e2.trait) {
                        it2 = possibleMethods.erase(it2) - 1;
                        continue;
                    }
                    if (e1.type != e2.type) {
                        continue;
                    }
                    if (e1.trait.path != e2.trait.path) {
                        continue;
                    }
                    assert(!(e1.trait.params == e2.trait.params));

                    // TODO: If `Into<Foo>` and `Into<_>` is seen, we want to pick the solid type, BUT

                    {
                        auto& ivars = node.traitParamIvars;
                        unsigned int nParams = e1.trait.params.types.size();
                        ASSERT_BUG(sp, node.traitParamTypeIvars <= ivars.size(), "Invalid method ivar split");
                        ASSERT_BUG(sp, nParams <= node.traitParamTypeIvars, "Not enough type ivars for duplicate trait method");
                        HIRPathParams traitParams;
                        traitParams.types.reserve(nParams);
                        for (unsigned int i = 0; i < nParams; i++) {
                            traitParams.types.push_back(context.crate.types.infer(ivars[i], HIRInferClass::None));
                        }
                        const unsigned int nValues = e1.trait.params.values.size();
                        ASSERT_BUG(sp, nValues <= ivars.size() - node.traitParamTypeIvars, "Not enough value ivars for duplicate trait method");
                        traitParams.values.reserve(nValues);
                        for (unsigned int i = 0; i < nValues; i++) {
                            traitParams.values.push_back(HIRConstGeneric::make_Infer({ivars[node.traitParamTypeIvars + i]}));
                        }
                        if (e1.trait.params != traitParams) {
                            e1.trait.params = mv$(traitParams);
                        }
                    }

                    it2 = possibleMethods.erase(it2) - 1;
                }
            }

            if (possibleMethods.size() > 1 && this->context.resolve.currentTraitPath()) {
                const auto& tp = *this->context.resolve.currentTraitPath();
                auto found = possibleMethods.end();
                bool hadInherent = false;
                for (auto it = possibleMethods.begin(); it != possibleMethods.end(); ++it) {
                    hadInherent |= it->second.data.is_UfcsInherent();
                    if (it->second.data.is_UfcsKnown() && it->second.data.as_UfcsKnown().trait.path == tp.path) {
                        found = it;
                    }
                }
                if (!hadInherent && found != possibleMethods.end()) {
                    possibleMethods.erase(found + 1, possibleMethods.end());
                    possibleMethods.erase(possibleMethods.begin(), found);
                }
            }
        }
        assert(!possibleMethods.empty());
        if (possibleMethods.size() != 1 && possibleMethods.front().second.data.is_UfcsKnown()) {
            // TODO: If the type is fully known, then this is an error.
            return;
        }
        auto& adBorrow = possibleMethods.front().first;
        auto& fcnPath = possibleMethods.front().second;

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
                            if (auto* possible = context.getIvarPossibilities(sp, infer->index)) {
                                possible->typesDefault.insert(exactTrait.params.types[i]);
                            }
                        }
                    }
                }
            }
        }

        node.methodPath = mv$(fcnPath);
        switch (node.methodPath.data.tag()) {
            case HIRPath::Data::TAG_Generic: {
                break;
            }
            case HIRPath::Data::TAG_UfcsUnknown: {
                break;
            }
            case HIRPath::Data::TAG_UfcsKnown: {
                auto& e = node.methodPath.data.as_UfcsKnown();
                e.params = mv$(node.params);
                break;
            }
            case HIRPath::Data::TAG_UfcsInherent: {
                auto& e = node.methodPath.data.as_UfcsInherent();
                e.params = mv$(node.params);
                break;
            }
        }

        // TODO: If this is ambigious, and it's an inherent, and in fallback mode - fall down to the next trait method.
        if (!visitCallPopulateCache(this->context, node.span(), node.methodPath, node.cache)) {
            switch (node.methodPath.data.tag()) {
                case HIRPath::Data::TAG_Generic: {
                    break;
                }
                case HIRPath::Data::TAG_UfcsUnknown: {
                    break;
                }
                case HIRPath::Data::TAG_UfcsKnown: {
                    auto& e = node.methodPath.data.as_UfcsKnown();
                    node.params = mv$(e.params);
                    break;
                }
                case HIRPath::Data::TAG_UfcsInherent: {
                    auto& e = node.methodPath.data.as_UfcsInherent();
                    node.params = mv$(e.params);
                    break;
                }
            }
            if (this->isFallback && node.methodPath.data.is_UfcsInherent()) {
                unsigned nRemove = 1;
                while (nRemove < possibleMethods.size() && possibleMethods[nRemove].second.data.is_UfcsInherent()) {
                    nRemove += 1;
                }
                if (nRemove < possibleMethods.size()) {
                    possibleMethods.erase(possibleMethods.begin() + nRemove);
                    goto tryAgain;
                } else {
                }
            } else {
            }
            return;
        }
        assert(node.cache.argTypes.size() >= 1);

        if (node.args.size() + 1 != node.cache.argTypes.size() - 1) {
            ERROR(node.span(), E0000, "Incorrect number of arguments to " << node.methodPath << " - exp " << node.cache.argTypes.size() - 2 << " got " << node.args.size());
        }

        for (unsigned int i = 0; i < node.args.size(); i++) {
            this->context.equateTypesCoerce(sp, node.cache.argTypes[1 + i], node.args[i]);
        }
        this->context.equateTypes(sp, node.resType, node.cache.argTypes.back());

        if (derefCount > 0) {
            assert(derefCount < (1 << 16));
            auto& nodePtr = node.value;
            HIRTypeRef tmpTy;
            const HIRTypeData* curTy = nodePtr->resType;
            while (derefCount--) {
                auto span = nodePtr->span();
                auto sourceTy = curTy;
                std::optional<HIRTypeRef> implType;
                auto result = this->context.resolve.autoderefStep(span, sourceTy, tmpTy, &implType);
                ASSERT_BUG(span, result == TraitResolution::AutoderefResult::Match, "Selected autoderef step no longer has a unique response for " << sourceTy);
                if (implType) {
                    this->context.equateTypes(span, sourceTy, *implType);
                    this->context.equateTypesAssoc(span, tmpTy, this->context.crate.getLangItemPath(span, "deref"), {}, sourceTy, "Target", {}, true, TypeckPrimitiveOperator::Deref);
                }
                curTy = tmpTy;
                auto ty = tmpTy;

                node.value = this->context.createAutoderef(mv$(node.value), mv$(ty));
            }
        }

        if (adBorrow == TraitResolution::AutoderefBorrow::RawShared) {
            const auto& srcTy = this->context.getType(node.value->resType);
            ASSERT_BUG(sp, srcTy->is_Pointer(), "RawShared adjustment on " << srcTy);
            auto ty = context.crate.types.pointer(HIRBorrowType::Shared, srcTy->as_Pointer().inner);
            auto span = node.value->span();
            node.value = NEWNODE(ty, span, Cast, mv$(node.value), ty);
        } else if (adBorrow == TraitResolution::AutoderefBorrow::PinShared) {
            auto ty = node.cache.argTypes[0];
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

    this->context.possibleEquateTypeUnknown(node.span(), node.resType, Context::IvarUnknownType::Bound);

    HIRTypeRef outType;

    unsigned int derefCount = 0;
    HIRTypeRef tmpType;
    const auto* currentTy = node.value->resType;
    std::vector<HIRTypeRef> derefResTypes;

    // TODO: autoderef_find_field?
    do {
        const auto* ty = this->context.revealOpaqueType(currentTy);
        if (ty->is_Infer()) {
            return;
        }
        if (ty->is_Path() && ty->as_Path().binding.is_Unbound()) {
            return;
        }
        if (this->context.resolve.findField(node.span(), ty, fieldName, outType)) {
            this->context.equateTypes(node.span(), node.resType, outType);
            break;
        }

        derefCount += 1;
        currentTy = this->context.resolve.autoderef(node.span(), ty, tmpType);
        if (currentTy) {
            derefResTypes.push_back(currentTy);
        }
    } while (currentTy);

    if (!currentTy) {
        ERROR(node.span(), E0000, "Couldn't find the field " << fieldName << " in " << this->context.ivars.fmtType(node.value->resType));
    }

    assert(derefCount == derefResTypes.size());
    for (unsigned int i = 0; i < derefResTypes.size(); i++) {
        auto ty = mv$(derefResTypes[i]);
        if (node.value->resType->is_Array()) {
            BUG(node.span(), "Field access from array/slice?");
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
    noRevisit(node);
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
    BUG(node.span(), "Node revisit unexpected - " << typeid(node).name());
}

ExprVisitorApply::ExprVisitorApply(const Context& context)
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

    this->checkTypeResolvedTop(node.span(), node.resType);

    nodePtr->visit(*this);

    for (auto& ty : nodePtr.bindings) {
        this->checkTypeResolvedTop(node.span(), ty);
    }

    for (auto& ty : nodePtr.erasedTypes) {
        this->checkTypeResolvedTop(node.span(), ty);
    }

    for (auto& ent : context.erasedTypeAliases) {
        auto t = ent.second.ourType;
        checkTypeResolved(node.span(), t, t);
        if (t->is_ErasedType() && t->as_ErasedType().inner.is_Alias() && t->as_ErasedType().inner.as_Alias().inner.get() == ent.first) {
            continue;
        }
        OpaqueAliasParamMonomorph aliasMonomorph{context.crate.types, *ent.first, ent.second.params};
        auto ty = aliasMonomorph.monomorphType(node.span(), t);
        {
            auto p = ent.first->generics.makeNopParams(context.crate.types, 0);
            MonomorphStatePtr(context.crate.types, nullptr, &p, nullptr).monomorphType(node.span(), ty);
        }
        if (ent.first->type == HIRTypeRef()) {
            ent.first->type = std::move(ty);
        } else {
            if (ent.first->type != ty) {
                ERROR(node.span(), E0000, "Disagreement on type for " << ent.first->path << ": " << ent.first->type << " or " << ty);
            }
        }
    }
}

auto ExprVisitorApply::visitNodePtr(HIRExprNodeP& nodePtr) -> void {
    auto& node = *nodePtr;
    const char* nodeTy = typeid(node).name();
    this->checkTypeResolvedTop(node.span(), node.resType);
    HIRExprVisitorDef::visitNodePtr(nodePtr);
}

auto ExprVisitorApply::visitPattern(const Span& sp, HIRPattern& pat) -> void {
    if (auto* deref = pat.data.opt_Deref()) {
        ASSERT_BUG(sp, deref->targetType, "Untyped deref pattern");
        this->checkTypeResolvedTop(sp, deref->targetType);
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
    this->checkTypeResolvedTop(node.span(), node.type);
    HIRExprVisitorDef::visit(node);
}

auto ExprVisitorApply::visit(HIRExprNodeClosure& node) -> void {
    for (auto& arg : node.args) {
        this->checkTypeResolvedTop(node.span(), arg.second);
    }
    if (const auto* expected = context.closureReturnExpectation(&node)) {
        node.returnType = expected;
    }
    this->checkTypeResolvedTop(node.span(), node.returnType);
    HIRExprVisitorDef::visit(node);
}

auto ExprVisitorApply::visit(HIRExprNodeGenerator& node) -> void {
    this->checkTypeResolvedTop(node.span(), node.returnType);
    this->checkTypeResolvedTop(node.span(), node.yieldTy);
    this->checkTypeResolvedTop(node.span(), node.resumeTy);
    HIRExprVisitorDef::visit(node);
}

auto ExprVisitorApply::visit(HIRExprNodeAsyncBlock& node) -> void {
    this->checkTypeResolvedTop(node.span(), node.returnType);
    if (node.isAsyncGen) {
        this->checkTypeResolvedTop(node.span(), node.yieldTy);
    }
    HIRExprVisitorDef::visit(node);
}

auto ExprVisitorApply::visit(HIRExprNodeGeneratorWrapper& node) -> void {
    BUG(node.span(), "");
}

auto ExprVisitorApply::visitCallcache(const Span& sp, HIRExprCallCache& cache) -> void {
    for (auto& ty : cache.argTypes) {
        this->checkTypeResolvedTop(sp, ty);
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
    HIRExprVisitorDef::visit(node);
}

auto ExprVisitorApply::visit(HIRExprNodeCallValue& node) -> void {
    for (auto& ty : node.argTypes) {
        this->checkTypeResolvedTop(node.span(), ty);
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
                std::vector<HIRTypeRef> argTypes;
                for (const auto& argTy : node.argIvars) {
                    argTypes.push_back(this->context.getType(argTy));
                }
                traitPp.types.push_back(context.crate.types.tuple(mv$(argTypes)));
            }

            if (!this->context.resolve.langFn().components().empty() && this->context.resolve.solveTraitGoal(node.span(), this->context.resolve.langFn(), traitPp, ty, [&](SolverResponse response) {
                return response.hasImpl;
            })) {
                node.traitUsed = HIRExprNodeCallValue::TraitUsed::Fn;
            } else if (!this->context.resolve.langFnMut().components().empty() && this->context.resolve.solveTraitGoal(node.span(), this->context.resolve.langFnMut(), traitPp, ty, [&](SolverResponse response) {
                return response.hasImpl;
            })) {
                node.traitUsed = HIRExprNodeCallValue::TraitUsed::FnMut;
            } else {
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
    for (auto& ty : node.valueTypes) {
        if (ty != HIRTypeRef()) {
            this->checkTypeResolvedTop(node.span(), ty);
        }
    }

    const auto& sp = node.span();
    const auto& tyPath = node.realPath;
    const auto& ty = node.resType;
    ASSERT_BUG(sp, ty->is_Path(), "Result type of _StructLiteral isn't Path");
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
                ASSERT_BUG(sp, idx != SIZE_MAX, "");
                ASSERT_BUG(sp, enm.data.is_Data(), "");
                const auto& var = enm.data.as_Data()[idx];

                const auto& str = *var.type->as_Path().binding.as_Struct();
                ASSERT_BUG(sp, var.isStruct, "Struct literal for enum on non-struct variant");
                fieldsPtr = &str.data.as_Named();
                break;
            }
            case HIRTypePathBinding::TAG_Union: {
                auto& e = tuMatch.as_Union();
                fieldsPtr = &e->variants;
                ASSERT_BUG(node.span(), node.values.size() > 0, "Union with no values");
                ASSERT_BUG(node.span(), node.values.size() == 1, "Union with multiple values");
                ASSERT_BUG(node.span(), !node.baseValue, "Union can't have a base value");
                break;
            }
            case HIRTypePathBinding::TAG_ExternType: {
                BUG(sp, "ExternType in StructLiteral");
                break;
            }
            case HIRTypePathBinding::TAG_Struct: {
                auto& e = tuMatch.as_Struct();
                if (e->data.is_Unit()) {
                    ASSERT_BUG(node.span(), node.values.size() == 0, "Values provided for unit-like struct");
                    ASSERT_BUG(node.span(), !node.baseValue, "Values provided for unit-like struct");
                    return;
                }
                if (e->data.is_Tuple()) {
                    ASSERT_BUG(node.span(), node.baseValue || !node.values.empty(), "Tuple struct literal has no values or base");
                    HIRExprVisitorDef::visit(node);
                    return;
                }

                ASSERT_BUG(node.span(), e->data.is_Named(), "StructLiteral not pointing to a braced struct, instead " << e->data.tagStr() << " - " << ty);
                fieldsPtr = &e->data.as_Named();
                break;
            }
        }
    }
    ASSERT_BUG(node.span(), fieldsPtr, "Didn't get field for path in _StructLiteral - " << ty);

    HIRExprVisitorDef::visit(node);
}

auto ExprVisitorApply::visit(HIRExprNodeTupleVariant& node) -> void {
    this->checkTypeResolvedPp(node.span(), node.path.params, HIRTypeRef());
    for (auto& ty : node.argTypes) {
        if (ty != HIRTypeRef()) {
            this->checkTypeResolvedTop(node.span(), ty);
        }
    }

    HIRExprVisitorDef::visit(node);
}

auto ExprVisitorApply::visit(HIRExprNodeLiteral& node) -> void {
    const HIRTypeData* literalType = node.resType;
    if (const auto* pattern = literalType->opt_Pattern()) {
        literalType = pattern->inner;
    }
    switch (node.data.tag()) {
        case HIRExprNodeLiteral::Data::TAG_Integer: {
            auto& e = node.data.as_Integer();
            ASSERT_BUG(node.span(), literalType->is_Primitive(), "Integer _Literal didn't return primitive-backed type - " << node.resType);
            e.type = literalType->as_Primitive();
            break;
        }
        case HIRExprNodeLiteral::Data::TAG_Float: {
            auto& e = node.data.as_Float();
            ASSERT_BUG(node.span(), literalType->is_Primitive(), "Float Literal didn't return primitive-backed type - " << node.resType);
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
    this->checkTypeResolvedTop(node.span(), node.dstType);
    HIRExprVisitorDef::visit(node);
}

auto ExprVisitorApply::visit(HIRExprNodeUnsize& node) -> void {
    this->checkTypeResolvedTop(node.span(), node.dstType);
    HIRExprVisitorDef::visit(node);
}

auto ExprVisitorApply::checkTypeResolvedTop(const Span& sp, HIRTypeRef& ty) const -> void {
    checkTypeResolved(sp, ty, ty);
    ty = this->context.expandAssociatedTypes(sp, mv$(ty));
}

auto ExprVisitorApply::checkTypeResolvedConstgeneric(const Span& sp, HIRConstGeneric& v, const HIRTypeData* topType) const -> void {
    if (v.is_Infer()) {
        auto val = ivars.getValue(v).clone();
        ASSERT_BUG(sp, !val.is_Infer(), "Failure to infer " << v << " in " << topType);
        v = std::move(val);
    }
}

auto ExprVisitorApply::checkTypeResolvedPp(const Span& sp, HIRPathParams& pp, const HIRTypeData* topType) const -> void {
    for (auto& ty : pp.types) {
        checkTypeResolved(sp, ty, topType);
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

auto ExprVisitorApply::checkTypeResolvedPath(const Span& sp, HIRPath& path, const HIRTypeData* topType) const -> void {
    switch (path.data.tag()) {
        case HIRPath::Data::TAG_Generic: {
            auto& pe = path.data.as_Generic();
            checkTypeResolvedPp(sp, pe.params, topType);
            break;
        }
        case HIRPath::Data::TAG_UfcsInherent: {
            auto& pe = path.data.as_UfcsInherent();
            checkTypeResolved(sp, pe.type, topType);
            checkTypeResolvedPp(sp, pe.params, topType);
            checkTypeResolvedPp(sp, pe.implParams, topType);
            break;
        }
        case HIRPath::Data::TAG_UfcsKnown: {
            auto& pe = path.data.as_UfcsKnown();
            checkTypeResolved(sp, pe.type, topType);
            checkTypeResolvedPp(sp, pe.trait.params, topType);
            checkTypeResolvedPp(sp, pe.params, topType);
            break;
        }
        case HIRPath::Data::TAG_UfcsUnknown: {
            ERROR(sp, E0000, "UfcsUnknown " << path << " left in " << topType);
            break;
        }
    }
}

auto ExprVisitorApply::checkTypeResolvedGenericpath(const Span& sp, HIRGenericPath& path) const -> void {
    auto tmp = context.crate.types.path(path.clone(), {});
    checkTypeResolvedPp(sp, path.params, tmp);
}

auto ExprVisitorApply::checkTypeResolved(const Span& sp, HIRTypeRef& ty, const HIRTypeData* topType) const -> void {
    struct InnerVisitor: public HIRVisitor {
        struct ActiveType {
            const HIRTypeData* type;
            const ActiveType* parent;
        };

        const ExprVisitorApply& parent;
        const Span& sp;
        const HIRTypeData* topType;
        const ActiveType* activeTypes = nullptr;

        InnerVisitor(const ExprVisitorApply& parent, const Span& sp, const HIRTypeData* topType)
            : HIRVisitor(nullptr, parent.context.crate.types)
            , parent(parent)
            , sp(sp)
            , topType(topType)
        {
        }

        void visitPath(HIRPath& path, HIRVisitor::PathContext pc) override {
            if (path.data.is_UfcsUnknown()) {
                ERROR(sp, E0000, "UfcsUnknown " << path << " left in " << topType);
            }
            HIRVisitor::visitPath(path, pc);
        }

        void visitConstgeneric(HIRConstGeneric& v) override {
            if (v.is_Infer()) {
                auto val = parent.ivars.getValue(v).clone();
                v = std::move(val);
            }
            HIRVisitor::visitConstgeneric(v);
        }

        [[nodiscard]] HIRTypeRef visitType(HIRTypeRef ty) override {
            if (ty->is_Infer()) {
                auto newTy = parent.ivars.getType(ty);
                ty = mv$(newTy);
                if (ty->is_Infer()) {
                    ERROR(sp, E0000, "Failed to infer type " << ty << " in " << topType);
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
                        size = size.as_Unevaluated().as_Evaluated()->readUsize(0);
                        ty = parent.context.crate.types.intern(std::move(data));
                    }
                }
            }
            return ty;
        }
    };

    InnerVisitor v(*this, sp, topType);
    ty = v.visitType(ty);
}

auto ExprVisitorApply::checkTypesEqual(const Span& sp, const HIRTypeData* l, const HIRTypeData* r) const -> void {
    if (r->is_Diverge()) {
    } else if (l != r && !l->equalsIgnoringRegions(r)) {
        ERROR(sp, E0000, "Type mismatch\n - " << l << "\n!= " << r);
    } else {
    }
}

auto ExprVisitorApply::visit(HIRExprNodeArraySized& node) -> void {
    HIRExprVisitorDef::visit(node);
    if (node.size.is_Unevaluated() && node.size.as_Unevaluated().is_Infer()) {
        auto count = ivars.getValue(node.size.as_Unevaluated()).clone();
        ASSERT_BUG(node.span(), !count.is_Infer(), "Failure to infer the length of " << node.resType);
        if (count.is_Evaluated()) {
            node.size = HIRArraySize::make_Known(count.as_Evaluated()->readUsize(0));
        } else {
            node.size = HIRArraySize(std::move(count));
        }
    }
}

ExprVisitorPrint::ExprVisitorPrint(const Context& context, std::ostream& os)
    : context(context)
    , os(os)
{
}

auto ExprVisitorPrint::visit(HIRExprNodeBlock& node) -> void {
    os << "_Block {" << context.ivars.fmtType(node.nodes.back()->resType) << "}";
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
    os << "_Cast {" << context.ivars.fmtType(node.value->resType) << "}";
}

auto ExprVisitorPrint::visit(HIRExprNodeUnsize& node) -> void {
    noRevisit(node);
}

auto ExprVisitorPrint::visit(HIRExprNodeIndex& node) -> void {
    os << "_Index {" << fmtResTy(*node.value) << "}[{" << fmtResTy(*node.index) << "}]";
}

auto ExprVisitorPrint::visit(HIRExprNodeDeref& node) -> void {
    os << "_Deref {" << fmtResTy(*node.value) << "}";
}

auto ExprVisitorPrint::visit(HIRExprNodeEmplace& node) -> void {
    os << "_Emplace(" << fmtResTy(*node.value) << " in " << fmtResTy(*node.place) << ")";
}

auto ExprVisitorPrint::visit(HIRExprNodeTupleVariant& node) -> void {
    noRevisit(node);
}

auto ExprVisitorPrint::visit(HIRExprNodeCallPath& node) -> void {
    noRevisit(node);
}

auto ExprVisitorPrint::visit(HIRExprNodeCallValue& node) -> void {
    os << "_CallValue {" << fmtResTy(*node.value) << "}(";
    for (const auto& arg : node.args) {
        os << "{" << fmtResTy(*arg) << "}, ";
    }
    os << ")";
}

auto ExprVisitorPrint::visit(HIRExprNodeCallMethod& node) -> void {
    os << "_CallMethod {" << fmtResTy(*node.value) << "}." << node.method << "(";
    for (const auto& arg : node.args) {
        os << "{" << fmtResTy(*arg) << "}, ";
    }
    os << ")";
}

auto ExprVisitorPrint::visit(HIRExprNodeField& node) -> void {
    os << "_Field {" << fmtResTy(*node.value) << "}." << node.field;
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
    os << "_" << typeid(n).name() << " {" << context.ivars.fmtType(n.resType) << "}";
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
            return l == r;
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

auto AssociatedStallCollector::addType(HIRTypeRef type) -> void {
    if (type->hasTypeInfer()) {
        pending.push_back(type);
    }
}

auto AssociatedStallCollector::collect() -> void {
    while (!pending.empty() && !hasRawInfer) {
        const auto type = pending.back();
        pending.pop_back();
        if (std::find(visited.begin(), visited.end(), type) != visited.end()) {
            continue;
        }
        visited.push_back(type);

        visitTyWith(type, [&](const HIRTypeData* inner) {
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
                    dependencies.push_back({resolvedInfer->index, resolved});
                }
            } else if (resolved->hasTypeInfer()) {
                pending.push_back(resolved);
            }
            return false;
        });
    }
}

auto IvarDependencyIndex::collectDirectIvars(const HIRTypeData* type, std::vector<unsigned int>& out) -> void {
    visitTyWith(type, [&](const HIRTypeData* inner) {
        if (const auto* infer = inner->opt_Infer()) {
            out.push_back(infer->index);
        }
        return false;
    });
}

auto IvarDependencyIndex::deduplicate(std::vector<unsigned int>& values) -> void {
    std::sort(values.begin(), values.end());
    values.erase(std::unique(values.begin(), values.end()), values.end());
}

IvarDependencyIndex::IvarDependencyIndex(Context& context)
    : context(context)
    , associatedTargets(context.possibleIvarVals.size())
    , possibilityTargets(context.possibleIvarVals.size())
{
    for (const auto& rule : context.linkAssoc) {
        std::vector<unsigned int> sources;
        std::vector<unsigned int> targets;
        collectDirectIvars(rule.implTy, sources);
        if (rule.name != "") {
            collectDirectIvars(rule.leftTy, sources);
        }
        collectDirectIvars(rule.implTy, targets);
        for (const auto& type : rule.params.types) {
            collectDirectIvars(type, sources);
            collectDirectIvars(type, targets);
        }
        deduplicate(sources);
        deduplicate(targets);
        for (const auto source : sources) {
            if (source < associatedTargets.size()) {
                associatedTargets[source].insert(associatedTargets[source].end(), targets.begin(), targets.end());
            }
        }
    }

    for (size_t target = 0; target < context.possibleIvarVals.size(); target++) {
        const auto& possible = context.possibleIvarVals[target];
        std::vector<unsigned int> sources;
        for (const auto& type : possible.typesCoerceFrom) {
            collectDirectIvars(type.ty, sources);
        }
        for (const auto& type : possible.typesCoerceTo) {
            collectDirectIvars(type.ty, sources);
        }
        for (const auto& type : possible.rawPointerFallbacks) {
            collectDirectIvars(type, sources);
        }
        deduplicate(sources);
        for (const auto source : sources) {
            if (source < possibilityTargets.size()) {
                possibilityTargets[source].push_back(target);
            }
        }
    }

    for (auto& targets : associatedTargets) {
        deduplicate(targets);
    }
    for (auto& targets : possibilityTargets) {
        deduplicate(targets);
    }
}

auto IvarDependencyIndex::disableDependents(unsigned int source) -> void {
    if (source >= associatedTargets.size()) {
        return;
    }
    for (const auto rawTarget : associatedTargets[source]) {
        const auto& target = context.ivars.getType(rawTarget);
        if (const auto* infer = target->opt_Infer()) {
            if (infer->index < context.possibleIvarVals.size()) {
                context.possibleIvarVals[infer->index].forceDisable = true;
            }
        }
    }
    for (const auto target : possibilityTargets[source]) {
        context.possibleIvarVals[target].forceDisable = true;
    }
}

auto IvarCoercionIndex::collectIvars(const HIRTypeData* root, std::vector<unsigned int>& out) const -> void {
    std::vector<HIRTypeRef> pending{root};
    std::vector<HIRTypeRef> visited;
    while (!pending.empty()) {
        const auto type = pending.back();
        pending.pop_back();
        if (std::find(visited.begin(), visited.end(), type) != visited.end()) {
            continue;
        }
        visited.push_back(type);
        visitTyWith(type, [&](const HIRTypeData* inner) {
            if (const auto* infer = inner->opt_Infer()) {
                out.push_back(infer->index);
                const auto& resolved = context.getType(inner);
                if (resolved != inner) {
                    pending.push_back(resolved);
                }
            }
            return false;
        });
    }
}

template <typename T>
auto IvarCoercionIndex::addRefs(const std::vector<unsigned int>& dependencies, std::vector<T> IvarCoercionRefs::* member, T value) -> void {
    for (const auto index : dependencies) {
        if (index < refs.size()) {
            (refs[index].*member).push_back(value);
        }
    }
}

IvarCoercionIndex::IvarCoercionIndex(const Context& context)
    : context(context)
    , refs(context.possibleIvarVals.size())
{
    std::vector<unsigned int> dependencies;
    for (const auto& bound : context.linkCoerce) {
        dependencies.clear();
        collectIvars(bound->leftTy, dependencies);
        collectIvars((*bound->rightNodePtr)->resType, dependencies);
        IvarDependencyIndex::deduplicate(dependencies);
        addRefs(dependencies, &IvarCoercionRefs::coercions, static_cast<const Context::Coercion*>(bound.get()));
    }
}

auto IvarCoercionIndex::operator[](unsigned int index) const -> const IvarCoercionRefs& {
    return refs.at(index);
}

auto PossibleType::concrete(decltype(cls) cls, const HIRTypeData* ty) -> PossibleType {
    return PossibleType{cls, State::Concrete, ty};
}

auto PossibleType::barrier(decltype(cls) cls) -> PossibleType {
    return PossibleType{cls, State::Barrier, nullptr};
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

auto PossibleType::ord(const PossibleType& o) const -> Ordering {
    if (state != o.state) {
        return ::ord(static_cast<int>(state), static_cast<int>(o.state));
    }
    if (hasType() && ty != o.ty) {
        return ::ord(ty, o.ty);
    }
    if (cls != o.cls) {
        return ::ord(static_cast<int>(cls), static_cast<int>(o.cls));
    }
    return OrdEqual;
}

auto PossibleType::operator<(const PossibleType& o) const -> bool {
    return ord(o) == OrdLess;
}

auto PossibleType::operator==(const PossibleType& o) const -> bool {
    return ord(o) == OrdEqual;
}

auto PossibleType::fmt(std::ostream& os) const -> std::ostream& {
    switch (cls) {
        case Equal:
            os << "==";
            break;
        case CoerceTo:
            os << "C-";
            break;
        case CoerceFrom:
            os << "CD";
            break;
        case UnsizeTo:
            os << "--";
            break;
        case UnsizeFrom:
            os << "-D";
            break;
    }
    os << " ";
    if (hasType()) {
        os << ty;
    } else if (state == State::Barrier) {
        os << "<barrier>";
    } else {
        os << "<removed>";
    }
    return os;
}

auto PossibleType::isSource() const -> bool {
    return cls == CoerceFrom || cls == UnsizeFrom;
}

auto PossibleType::isDest() const -> bool {
    return cls == CoerceTo || cls == UnsizeTo;
}

auto PossibleType::isSourceS(const PossibleType& self) -> bool {
    return self.isSource();
}

auto PossibleType::isDestS(const PossibleType& self) -> bool {
    return self.isDest();
}

auto PossibleType::isCoerce() const -> bool {
    return cls == CoerceTo || cls == CoerceFrom;
}

auto PossibleType::isUnsize() const -> bool {
    return cls == UnsizeTo || cls == UnsizeFrom;
}

auto PossibleType::isCoerceS(const PossibleType& self) -> bool {
    return self.isCoerce();
}

auto PossibleType::isUnsizeS(const PossibleType& self) -> bool {
    return self.isUnsize();
}

auto TypeRestrictiveOrdering::matchAndExtractPtrTy(const HIRTypeData* ptrTpl, const HIRTypeData* ty) -> const HIRTypeData* {
    if (ty->tag() != ptrTpl->tag()) {
        return nullptr;
    }
    switch ((*ty).tag()) {
        case HIRTypeData::TAG_Borrow: {
            auto& te = (*ty).as_Borrow();
            if (te.type == ptrTpl->as_Borrow().type) {
                return te.inner;
            }
            break;
        }
        case HIRTypeData::TAG_Pointer: {
            auto& te = (*ty).as_Pointer();
            if (te.type == ptrTpl->as_Pointer().type) {
                return te.inner;
            }
            break;
        }
        case HIRTypeData::TAG_Path: {
            auto& te = (*ty).as_Path();
            if (te.binding == ptrTpl->as_Path().binding) {
                // TODO: Get inner
            }
            break;
        } break;
        default:
            break;
    }
    return nullptr;
}

auto TypeRestrictiveOrdering::getOrderingInfer(const Span& sp, const HIRTypeData* r) -> Ordering {
    switch ((*r).tag()) {
        default:
            return OrdLess;
        case HIRTypeData::TAG_Path: {
            auto& te = (*r).as_Path();
            if (te.binding.is_Opaque()) {
                return OrdLess;
            }
            if (te.binding.is_Unbound()) {
                return OrdEqual;
            }
            // TODO: Check if the type is concrete? (Check an unsizing param if present)
            return OrdLess;
        }
        case HIRTypeData::TAG_Borrow: {
            auto& _ = (*r).as_Borrow();
            return OrdEqual;
        }
        case HIRTypeData::TAG_Infer: {
            auto& _ = (*r).as_Infer();
            return OrdEqual;
        }
        case HIRTypeData::TAG_Pointer: {
            auto& _ = (*r).as_Pointer();
            return OrdEqual;
        }
    }
    UNREACHABLE();
}

auto TypeRestrictiveOrdering::getOrderingTy(const Span& sp, const Context& context, const HIRTypeData* l, const HIRTypeData* r, bool& outUnordered) -> Ordering {
    if (l == r) {
        return OrdEqual;
    }
    if (l->is_Infer()) {
        return getOrderingInfer(sp, r);
    }
    if (r->is_Infer()) {
        switch (getOrderingInfer(sp, l)) {
            case OrdLess:
                return OrdGreater;
            case OrdEqual:
                return OrdEqual;
            case OrdGreater:
                return OrdLess;
        }
    }
    if (l->is_Path()) {
        const auto& teL = l->as_Path();
        switch ((*r).tag()) {
            default:
                if (teL.binding.is_Unbound()) {
                    return OrdLess;
                }
                outUnordered = true;
                return OrdEqual;
            //TODO(sp, l << " with " << r << " - LHS is Path, RHS is " << r->tag_str());
            case HIRTypeData::TAG_Slice: {
                return OrdGreater;
            }
            case HIRTypeData::TAG_Path: {
                auto& teR = (*r).as_Path();
                if (teL.binding.is_Unbound() && teR.binding.is_Unbound()) {
                    return OrdEqual;
                }
                if (teL.binding.is_Unbound()) {
                    return OrdLess;
                }
                if (teR.binding.is_Unbound()) {
                    return OrdGreater;
                } else if (teR.binding.is_Opaque()) {
                    TODO(sp, l << " with " << r << " - LHS is Path, RHS is opaque type");
                } else if ((teR.binding.is_Struct() && (teR.binding.as_Struct()->structMarkings.canUnsize))) {
                    TODO(sp, l << " with " << r << " - LHS is Path, RHS is unsize-capable struct");
                } else {
                    return OrdEqual;
                }
                break;
            }
        }
    }
    if (r->is_Path()) {
        switch (getOrderingTy(sp, context, r, l, outUnordered)) {
            case OrdLess:
                return OrdGreater;
            case OrdEqual:
                return OrdEqual;
            case OrdGreater:
                return OrdLess;
        }
    }

    if (l->tag() == r->tag()) {
        return OrdEqual;
    } else {
        if (l->is_Slice() && r->is_Array()) {
            return OrdGreater;
        }
        if (l->is_Array() && r->is_Slice()) {
            return OrdLess;
        }

        if (l->is_Borrow() && !r->is_Borrow()) {
            return OrdGreater;
        }
        if (r->is_Borrow() && !l->is_Borrow()) {
            return OrdLess;
        }

        outUnordered = true;
        return OrdEqual;
        //TODO(sp, "Compare " << l << " and " << r);
    }
}

auto TypeRestrictiveOrdering::getOrderingPtr(const Span& sp, const Context& context, const HIRTypeData* l, const HIRTypeData* r, bool& outUnordered, bool deep) -> Ordering {
    Ordering cmp;
    static const HIRTypeData::Tag tagOrdering[] = {
        HIRTypeData::TAG_Pointer,
        HIRTypeData::TAG_Borrow,
        HIRTypeData::TAG_Path,
        HIRTypeData::TAG_Generic,
        HIRTypeData::TAG_ErasedType,
        HIRTypeData::TAG_Function,
        HIRTypeData::TAG_NamedFunction,
        HIRTypeData::TAG_NodeType,
    };
    static const HIRTypeData::Tag* tagOrderingEnd = &tagOrdering[sizeof(tagOrdering) / sizeof(tagOrdering[0])];
    if (l->tag() != r->tag()) {
        auto pL = std::find(tagOrdering, tagOrderingEnd, l->tag());
        auto pR = std::find(tagOrdering, tagOrderingEnd, r->tag());
        if (pL == tagOrderingEnd) {
            TODO(sp, "Type " << l << " not in ordering list");
        }
        if (pR == tagOrderingEnd) {
            TODO(sp, "Type " << r << " not in ordering list");
        }
        cmp = ord(static_cast<int>(pL - pR), 0);
    } else {
        if (l == r) {
            return OrdEqual;
        }
        switch ((*l).tag()) {
            default:
                BUG(sp, "Unexpected type class " << l << " in get_ordering_ty (" << r << ")");
                break;
            case HIRTypeData::TAG_Generic: {
                cmp = OrdEqual;
                break;
            }
            case HIRTypeData::TAG_NamedFunction: {
                cmp = OrdEqual;
                break;
            }
            case HIRTypeData::TAG_Path: {
                // TODO: Prevent this rule from applying?
                return OrdEqual;
            }
            case HIRTypeData::TAG_NodeType: {
                outUnordered = true;
                return OrdEqual;
            }
            case HIRTypeData::TAG_ErasedType: {
                outUnordered = true;
                return OrdEqual;
            }
            case HIRTypeData::TAG_Borrow: {
                auto& teL = (*l).as_Borrow();
                const auto& teR = r->as_Borrow();
                cmp = ord((int)teL.type, (int)teR.type);
                if (cmp == OrdEqual && deep) {
                    cmp = getOrderingTy(sp, context, context.ivars.getType(teL.inner), context.ivars.getType(teR.inner), outUnordered);
                }
                break;
            }
            case HIRTypeData::TAG_Pointer: {
                auto& teL = (*l).as_Pointer();
                const auto& teR = r->as_Pointer();
                cmp = ord((int)teR.type, (int)teL.type);
                if (cmp == OrdEqual && deep) {
                    cmp = getOrderingTy(sp, context, context.ivars.getType(teL.inner), context.ivars.getType(teR.inner), outUnordered);
                }
                break;
            }
        }
    }
    return cmp;
}

auto InfoOrdering::isInfer(const HIRTypeData* ty) -> bool {
    if (ty->is_Infer()) {
        return true;
    }
    if (((*ty).is_Path() && ((*ty).as_Path().binding.is_Unbound()))) {
        return true;
    }
    return false;
}

auto InfoOrdering::compareScore(int& score, const HIRTypeData* tyL, const HIRTypeData* tyR) -> bool {
    auto rv = compare(tyL, tyR);
    switch (rv) {
        case Incompatible:
            return Incompatible;
        case Less:
            score--;
            break;
        case Same:
            break;
        case More:
            score++;
            break;
    }
    return rv;
}

auto InfoOrdering::compare(const HIRTypeData* tyL, const HIRTypeData* tyR) -> eInfoOrdering {
    if (isInfer(tyL)) {
        if (isInfer(tyR)) {
            return Same;
        }
        return Less;
    } else {
        if (isInfer(tyR)) {
            return More;
        }
    }
    if (tyL->tag() != tyR->tag()) {
        return Incompatible;
    }
    switch ((*tyL).tag()) {
        default:
            return Incompatible;
        case HIRTypeData::TAG_NodeType: {
            auto& le = (*tyL).as_NodeType();
            auto& re = (*tyR).as_NodeType();
            if (le != re) {
                return Incompatible;
            }
            return Same;
        }
        case HIRTypeData::TAG_Tuple: {
            auto& le = (*tyL).as_Tuple();
            auto& re = (*tyR).as_Tuple();
            if (le.size() != re.size()) {
                return Incompatible;
            }
            int score = 0;
            for (size_t i = 0; i < le.size(); i++) {
                if (compareScore(score, le[i], re[i]) == Incompatible) {
                    return Incompatible;
                }
            }
            return (score == 0 ? Same : (score < 0 ? Less : More));
        }
    }
    UNREACHABLE();
}

auto InfoOrdering::compareTop(const Context& context, const HIRTypeData* tyL, const HIRTypeData* tyR, bool shouldDeref) -> eInfoOrdering {
    if (context.ivars.typesEqual(tyL, tyR)) {
        return Same;
    }
    if (isInfer(tyL)) {
        return Incompatible;
    }
    if (isInfer(tyR)) {
        return Incompatible;
    }
    if (tyL->tag() != tyR->tag()) {
        return Incompatible;
    }
    if (shouldDeref) {
        if (const auto* le = tyL->opt_Borrow()) {
            const auto& re = tyR->as_Borrow();
            if (le->type != re.type) {
                return Incompatible;
            }
            return compareTop(context, context.ivars.getType(le->inner), context.ivars.getType(re.inner), false);
        } else if (const auto* le = tyL->opt_Pointer()) {
            const auto& re = tyR->as_Pointer();
            if (le->type != re.type) {
                return Incompatible;
            }
            return compareTop(context, context.ivars.getType(le->inner), context.ivars.getType(re.inner), false);
        } else if (((*tyL).is_Path() && (*tyL).as_Path().binding.is_Struct() && (*tyL).as_Path().binding.as_Struct()->structMarkings.coerceUnsized != HIRStructMarkings::Coerce::None)) {
            const auto& le = tyL->as_Path();
            const auto& re = tyR->as_Path();
            if (le.binding != re.binding) {
                return Incompatible;
            }
            auto paramIdx = le.binding.as_Struct()->structMarkings.coerceParam;
            assert(paramIdx != ~0u);
            return compareTop(context, context.ivars.getType(le.path.data.as_Generic().params.types.at(paramIdx)), context.ivars.getType(re.path.data.as_Generic().params.types.at(paramIdx)), false);
        } else if (const auto* le = tyL->opt_NodeType()) {
            const auto& re = tyR->as_NodeType();
            return *le != re ? Incompatible : Same;
        } else {
            BUG(Span(), "Can't deref " << tyL << " / " << tyR);
        }
    }
    if (tyL->is_Slice()) {
        const auto& le = tyL->as_Slice();
        const auto& re = tyR->as_Slice();

        switch (compare(context.ivars.getType(le.inner), context.ivars.getType(re.inner))) {
            case Less:
                return Less;
            case More:
                return More;
            case Same:
            case Incompatible:
                return Same;
        }
        UNREACHABLE();
    }
    return Incompatible;
}

OwnedImplMatcher::OwnedImplMatcher(HIRTypeInterner& types, HIRPathParams& implParams)
    : HIRMatchGenerics(types.objectPool())
    , implParams(implParams)
{
}

auto OwnedImplMatcher::matchTy(const HIRGenericRef& g, const HIRTypeData* ty, tCbResolveType _resolve_cb) -> HIRCompare {
    assert(g.binding < implParams.types.size());
    auto& slot = implParams.types[g.binding];
    if (!(slot->is_Infer() && slot->as_Infer().index == ~0u)) {
        return slot->compareWithPlaceholders(Span(), ty, _resolve_cb);
    }
    slot = ty;
    return HIRCompare::Equal;
}

auto OwnedImplMatcher::matchVal(const HIRGenericRef& g, const HIRConstGeneric& sz) -> HIRCompare {
    assert(g.binding < implParams.values.size());
    ASSERT_BUG(Span(), implParams.values[g.binding] == HIRConstGeneric(), "TODO: Multiple values? " << implParams.values[g.binding] << " and " << sz);
    implParams.values[g.binding] = sz.clone();
    return HIRCompare::Equal;
}

ExprVisitorTagStaleIvars::ExprVisitorTagStaleIvars(HIRTypeInterner& types)
    : HIRExprVisitorDef(types)
    , mapper_(types)
{
}

[[nodiscard]] auto ExprVisitorTagStaleIvars::visitType(HIRTypeRef type) -> HIRTypeRef {
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

auto ExprVisitorAddIvars::innerVisitType(HIRTypeRef& ty) -> void {
    rewriteTyWith(context.crate.types, ty, [this](HIRTypeRef&, HIRTypeData& data) -> bool {
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
        return false;
    });
}

auto ExprVisitorAddIvars::visitPathParams(HIRPathParams& pp) -> void {
    this->context.ivars.addIvarsParams(pp);
    for (auto& ty : pp.types) {
        innerVisitType(ty);
    }
}

[[nodiscard]] auto ExprVisitorAddIvars::visitType(HIRTypeRef ty) -> HIRTypeRef {
    this->context.addIvars(ty);
    innerVisitType(ty);
    visitTyWith(ty, [&](const HIRTypeData* inner) {
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

auto ExprVisitorAddIvars::LocalImplTraitLowering::getType(const Span& sp, const HIRGenericRef& generic) const -> HIRTypeRef {
    if (generic.binding == GENERICErasedSelf && curSelf) {
        return curSelf;
    }
    return types.generic(generic.name, generic.binding);
}

auto ExprVisitorAddIvars::LocalImplTraitLowering::getValue(const Span& sp, const HIRGenericRef& generic) const -> HIRConstGeneric {
    return generic;
}

auto ExprVisitorAddIvars::LocalImplTraitLowering::monomorphType(const Span& sp, const HIRTypeData* type, bool allowInfer) const -> HIRTypeRef {
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

ExprVisitorEnum::ExprVisitorEnum(Context& context, tTraitList baseTraits, const HIRTypeData* retType)
    : context(context)
    , retType(retType)
    , traits(mv$(baseTraits))
{
}

auto ExprVisitorEnum::visit(HIRExprNodeBlock& node) -> void {
    this->context.resolve.addOpaqueAliasScope(node.localMod);

    bool diverges = false;
    node.diverges = false;
    this->pushTraits(node.traits);
    if (node.nodes.size() > 0) {
        this->pushInnerCoerce(false);
        for (unsigned int i = 0; i < node.nodes.size(); i++) {
            auto& snp = node.nodes[i];
            this->context.addIvars(snp->resType);
            snp->visit(*this);

            if (this->nodeDiverges(*snp)) {
                diverges = true;
            } else {
                this->context.addRevisitAdv(std::make_unique<RevisitDefaultUnit>(&*snp));
            }
        }
        this->popInnerCoerce();
    }

    if (node.valueNode) {
        auto& snp = node.valueNode;
        this->context.addIvars(snp->resType);
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
            this->context.addRevisit(node);
        } else if (diverges) {
            const auto* blockInfer = this->context.crate.edition < ASTEdition::Rust2024 ? this->context.getType(node.resType)->opt_Infer() : nullptr;
            if (const auto* i = blockInfer) {
                this->context.possibleEquateIvar(node.span(), i->index, this->context.crate.types.diverge(), Context::PossibleTypeSource::CoerceFrom);
                this->context.addRevisitAdv(std::make_unique<RevisitDefaultUnit>(&node));
            } else {
                this->context.equateTypes(node.span(), node.resType, this->context.crate.types.diverge());
            }
        } else {
            this->context.equateTypes(node.span(), node.resType, this->context.crate.types.unit());
        }
        node.diverges = diverges;
    } else {
        this->context.equateTypes(node.span(), node.resType, this->context.crate.types.unit());
    }
    this->popTraits(node.traits);
}

auto ExprVisitorEnum::visit(HIRExprNodeConstBlock& node) -> void {
    this->context.addIvars(node.inner->resType);

    node.inner->visit(*this);
    node.diverges = this->nodeDiverges(*node.inner);
    this->context.equateTypes(node.span(), node.resType, node.inner->resType);
}

auto ExprVisitorEnum::visit(HIRExprNodeAsm& node) -> void {
    this->pushInnerCoerce(false);
    for (auto& v : node.outputs) {
        this->context.addIvars(v.value->resType);
        v.value->visit(*this);
        this->inheritDivergence(node, *v.value);
    }
    for (auto& v : node.inputs) {
        this->context.addIvars(v.value->resType);
        v.value->visit(*this);
        this->inheritDivergence(node, *v.value);
    }
    this->popInnerCoerce();
    // TODO: Revisit to check that the input are integers, and the outputs are integer lvalues
    this->context.equateTypes(node.span(), node.resType, this->context.crate.types.unit());
}

auto ExprVisitorEnum::visit(HIRExprNodeAsm2& node) -> void {
    bool hasLabel = false;
    this->pushInnerCoerce(false);
    for (auto& v : node.params) {
        switch (v.tag()) {
            case HIRAsmParam::TAG_Const: {
                auto& e = v.as_Const();
                this->context.addIvars(e->resType);
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
                this->context.addIvars(e.code->resType);
                visitNodePtr(e.code);
                this->context.equateTypes(e.code->span(), e.code->resType, this->context.crate.types.unit());
                break;
            }
            case HIRAsmParam::TAG_RegSingle: {
                auto& e = v.as_RegSingle();
                this->context.addIvars(e.val->resType);
                visitNodePtr(e.val);
                this->inheritDivergence(node, *e.val);
                break;
            }
            case HIRAsmParam::TAG_Reg: {
                auto& e = v.as_Reg();
                if (e.valIn) {
                    this->context.addIvars(e.valIn->resType);
                    visitNodePtr(e.valIn);
                    this->inheritDivergence(node, *e.valIn);
                }
                if (e.valOut) {
                    this->context.addIvars(e.valOut->resType);
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
        ERROR(node.span(), E0000, "`become` requires a direct function call");
    }
    this->context.addIvars(node.value->resType);

    const auto* retTy = (this->closureRetTypes.size() > 0 ? this->closureRetTypes.back().retType : this->retType);
    this->context.equateTypesCoerce(node.span(), retTy, node.value);

    this->pushInnerCoerce(true);
    node.value->visit(*this);
    this->popInnerCoerce();
    node.diverges = true;
    this->context.equateTypes(node.span(), node.resType, this->context.crate.types.diverge());
}

auto ExprVisitorEnum::visit(HIRExprNodeYield& node) -> void {
    this->context.addIvars(node.value->resType);

    if (this->closureRetTypes.empty() || this->closureRetTypes.back().yieldType == nullptr) {
        ERROR(node.span(), E0000, "`yield` outside a generator closure");
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
    this->context.addIvars(node.value->resType);
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
    this->context.addIvars(node.value->resType);
    node.value->visit(*this);
    this->inheritDivergence(node, *node.value);
    this->context.equateTypes(node.span(), node.resType, node.value->resType);
}

auto ExprVisitorEnum::visit(HIRExprNodeLoop& node) -> void {
    auto _ = this->pushInnerCoerceScoped(false);
    this->loopBlocks.push_back(&node);
    node.diverges = true;

    this->context.addIvars(node.code->resType);
    this->context.equateTypes(node.span(), node.code->resType, this->context.crate.types.unit());
    node.code->visit(*this);

    this->loopBlocks.pop_back();

    if (node.diverges) {
        this->context.equateTypes(node.span(), node.resType, this->context.crate.types.diverge());
    }
}

auto ExprVisitorEnum::visit(HIRExprNodeLoopControl& node) -> void {
    if (!node.isContinue) {
        HIRExprNodeLoop* loopNodePtr;
        if (node.label != "") {
            auto it = std::find_if(this->loopBlocks.rbegin(), this->loopBlocks.rend(), [&](const auto& np) {
                return np->label == node.label;
            });
            if (it == this->loopBlocks.rend()) {
                ERROR(node.span(), E0000, "Could not find loop '" << node.label << " for break");
            }
            loopNodePtr = &**it;
        } else {
            loopNodePtr = nullptr;
            for (auto it = this->loopBlocks.rbegin(); it != this->loopBlocks.rend(); ++it) {
                if (!(*it)->requireLabel) {
                    loopNodePtr = *it;
                    break;
                }
            }
            if (!loopNodePtr) {
                ERROR(node.span(), E0000, "Break statement with no active loop");
            }
        }
        node.targetNode = loopNodePtr;

        auto& loopNode = *loopNodePtr;
        loopNode.diverges = false;

        if (node.value) {
            this->context.addIvars(node.value->resType);
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
    this->context.addIvars(node.type);
    this->context.handlePattern(node.span(), node.pattern, node.type, true);

    bool deferResultType = false;
    bool diverges = false;
    if (node.value) {
        this->context.addIvars(node.value->resType);
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
    auto valType = this->context.ivars.newIvarTr();
    const auto* armResultType = this->context.coercionHint(node);
    if (!armResultType) {
        armResultType = node.resType;
    }

    {
        auto _ = this->pushInnerCoerceScoped(true);
        this->context.addIvars(node.value->resType);

        node.value->visit(*this);
        this->inheritDivergence(node, *node.value);
        // TODO: If a coercion point (and ivar for the value) is placed here, it will allow `match &string { "..." ... }`

        this->context.equateTypes(node.span(), valType, node.value->resType);
    }

    for (auto& arm : node.arms) {
        const bool unconditionallySelected = &arm == &node.arms.front() && std::any_of(arm.patterns.begin(), arm.patterns.end(), [](const HIRPattern& pattern) {
            return pattern.data.is_Any();
        });
        for (auto& pat : arm.patterns) {
            this->context.handlePattern(node.span(), pat, valType);
        }

        for (auto& c : arm.guards) {
            auto _ = this->pushInnerCoerceScoped(false);
            this->context.addIvars(c.val->resType);

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

        this->context.addIvars(arm.code->resType);

        this->context.equateTypesCoerce(node.span(), armResultType, arm.code);
        arm.code->visit(*this);
    }

    if (node.arms.empty()) {
        this->context.equateTypes(node.span(), node.resType, this->context.crate.types.diverge());
    } else if (std::all_of(node.arms.begin(), node.arms.end(), [&](const HIRExprNodeMatch::Arm& arm) {
        return this->nodeDiverges(*arm.code);
    })) {
        node.diverges = true;
    }
}

auto ExprVisitorEnum::visit(HIRExprNodeAssign& node) -> void {
    auto _ = this->pushInnerCoerceScoped(false);

    this->context.addIvars(node.slot->resType);
    this->context.addIvars(node.value->resType);

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
        assert(langItem);
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

    this->context.addIvars(node.left->resType);
    this->context.addIvars(node.right->resType);

    const auto& leftTy = node.left->resType;
    HIRTypeRef rightTyInner = this->context.ivars.newIvarTr();
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
    const HIRTypeData* operatorResultType = node.resType;
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
            assert(itemName);
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
            assert(itemName);
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

    this->context.addIvars(node.value->resType);
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
    assert(itemName);
    const HIRTypeData* inputType = node.value->resType;
    if (this->context.getType(inputType)->is_Diverge()) {
        const HIRTypeData* expectedType = this->context.coercionHint(node);
        if (!expectedType) {
            expectedType = node.resType;
        }
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
    this->context.addIvars(node.value->resType);

    this->context.equateTypes(node.span(), node.resType, this->context.crate.types.borrow(node.type, node.value->resType));

    node.value->visit(*this);
    this->inheritDivergence(node, *node.value);
}

auto ExprVisitorEnum::visit(HIRExprNodeRawBorrow& node) -> void {
    this->context.addIvars(node.value->resType);

    this->context.equateTypes(node.span(), node.resType, this->context.crate.types.pointer(node.type, node.value->resType));

    node.value->visit(*this);
    this->inheritDivergence(node, *node.value);
}

auto ExprVisitorEnum::visit(HIRExprNodeCast& node) -> void {
    auto _ = this->pushInnerCoerceScoped(false);
    this->context.addIvars(node.dstType);

    node.value->visit(*this);
    this->inheritDivergence(node, *node.value);

    this->context.equateTypes(node.span(), node.resType, node.dstType);
    // TODO: Only revisit if the cast type requires inferring.
    this->context.addRevisit(node);
}

auto ExprVisitorEnum::visit(HIRExprNodeUnsize& node) -> void {
    this->context.addIvars(node.dstType);
    node.value->visit(*this);
    this->inheritDivergence(node, *node.value);

    this->context.equateTypesCoerce(node.value->span(), node.dstType, node.value);
    this->context.equateTypes(node.span(), node.resType, node.dstType);
}

auto ExprVisitorEnum::visit(HIRExprNodeIndex& node) -> void {
    auto _ = this->pushInnerCoerceScoped(false);

    this->context.addIvars(node.value->resType);
    node.cache.indexTy = this->context.ivars.newIvarTr();
    this->context.addIvars(node.index->resType);

    node.value->visit(*this);
    node.index->visit(*this);
    this->inheritDivergence(node, *node.value);
    this->inheritDivergence(node, *node.index);
    this->context.equateTypesCoerce(node.index->span(), node.cache.indexTy, node.index);

    this->context.addRevisit(node);
}

auto ExprVisitorEnum::visit(HIRExprNodeDeref& node) -> void {
    auto _ = this->pushInnerCoerceScoped(false);

    this->context.addIvars(node.value->resType);

    node.value->visit(*this);
    this->inheritDivergence(node, *node.value);

    const auto& ty = this->context.getType(node.value->resType);
    const HIRTypeData* inner = nullptr;
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
    this->context.addIvars(node.place->resType);
    this->context.addIvars(node.value->resType);

    node.place->visit(*this);
    this->inheritDivergence(node, *node.place);
    auto _2 = this->pushInnerCoerceScoped(true);
    node.value->visit(*this);
    this->inheritDivergence(node, *node.value);

    this->context.addRevisit(node);
}

auto ExprVisitorEnum::addIvarsGenericPath(const Span& sp, HIRGenericPath& gp) -> void {
    for (auto& ty : gp.params.types) {
        this->context.addIvars(ty);
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
            this->context.addIvars(e.type);
            this->addIvarsGenericPath(sp, e.trait);
            for (auto& ty : e.params.types) {
                this->context.addIvars(ty);
            }
            break;
        }
        case HIRPath::Data::TAG_UfcsUnknown: {
            TODO(sp, "Hit a UfcsUnknown (" << path << ") - Is this an error?");
            break;
        }
        case HIRPath::Data::TAG_UfcsInherent: {
            auto& e = path.data.as_UfcsInherent();
            this->context.addIvars(e.type);
            for (auto& ty : e.params.types) {
                this->context.addIvars(ty);
            }
            break;
        }
    }
}

auto ExprVisitorEnum::getStructenumTy(const Span& sp, bool isStruct, HIRGenericPath& gp) -> HIRTypeRef {
    if (isStruct) {
        const auto& e = this->context.crate.getTypeitemByPath(sp, gp.path);
        if (e.is_Struct()) {
            const auto& str = e.as_Struct();
            fixParamCount(sp, this->context, HIRTypeRef(), false, gp, str.params, gp.params);

            return this->context.crate.types.path(gp.clone(), HIRTypePathBinding::make_Struct(&str));
        } else if (e.is_Union()) {
            const auto& u = e.as_Union();
            fixParamCount(sp, this->context, HIRTypeRef(), false, gp, u.params, gp.params);

            return this->context.crate.types.path(gp.clone(), HIRTypePathBinding::make_Union(&u));
        } else {
            BUG(sp, "Path " << gp << " doesn't refer to a struct/union");
        }
    } else {
        auto sPath = getRuleParentPath(gp.path);

        const auto& enm = this->context.crate.getEnumByPath(sp, sPath);
        fixParamCount(sp, this->context, HIRTypeRef(), false, gp, enm.params, gp.params);

        return this->context.crate.types.path(HIRGenericPath(mv$(sPath), gp.params.clone()), HIRTypePathBinding::make_Enum(&enm));
    }
}

auto ExprVisitorEnum::visit(HIRExprNodeTupleVariant& node) -> void {
    const auto& sp = node.span();
    node.diverges = false;
    for (auto& val : node.args) {
        this->context.addIvars(val->resType);
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
                ASSERT_BUG(sp, idx < enm.data.as_Data().size(), "Unknown variant - " << node.path);
                const auto& varTy = enm.data.as_Data()[idx].type;
                ASSERT_BUG(sp, varTy->as_Path().binding.is_Struct(), "Pointed variant of TupleVariant (" << node.path << ") isn't a Tuple");
                ASSERT_BUG(sp, varTy->as_Path().binding.as_Struct() != nullptr, "Pointed variant of TupleVariant (" << node.path << ") isn't a Tuple");
                const auto& str = *varTy->as_Path().binding.as_Struct();
                ASSERT_BUG(sp, str.data.is_Tuple(), "Pointed variant of TupleVariant (" << node.path << ") isn't a Tuple");
                fieldsPtr = &str.data.as_Tuple();
                break;
            }
            case HIRTypePathBinding::TAG_Struct: {
                auto& e = tuMatch.as_Struct();
                ASSERT_BUG(sp, e->data.is_Tuple(), "Pointed struct in TupleVariant (" << node.path << ") isn't a Tuple");
                fieldsPtr = &e->data.as_Tuple();
                generics = &e->params;
                break;
            }
            case HIRTypePathBinding::TAG_Union: {
                BUG(sp, "TupleVariant pointing to a union");
                break;
            }
            case HIRTypePathBinding::TAG_ExternType: {
                BUG(sp, "TupleVariant pointing to a extern type");
                break;
            }
        }
    }
    assert(fieldsPtr);
    assert(generics);
    const tTupleFields& fields = *fieldsPtr;
    if (fields.size() != node.args.size()) {
        ERROR(node.span(), E0000, "Tuple variant constructor argument count doesn't match type - " << node.path);
    }

    auto monomorphCb = MonomorphStatePtr(this->context.crate.types, ty, &node.path.params, nullptr);

    applyBoundsAsRules(this->context, sp, *generics, monomorphCb, /*is_impl_level=*/true);

    node.argTypes.resize(node.args.size());
    for (unsigned int i = 0; i < node.args.size(); i++) {
        const auto& desTyR = fields[i].ent;
        const auto* desTy = &desTyR;
        if (monomorphiseTypeNeeded(desTyR)) {
            node.argTypes[i] = monomorphCb.monomorphType(sp, desTyR);
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
    node.diverges = false;
    auto _ = this->pushInnerCoerceScoped(true);

    if (node.realPath == HIRGenericPath()) {
        this->context.addIvars(node.type);
    }

    for (auto& val : node.values) {
        this->context.addIvars(val.second->resType);
    }
    if (node.baseValue) {
        this->context.addIvars(node.baseValue->resType);
    }

    if (node.realPath == HIRGenericPath()) {
        auto t = this->context.expandAssociatedTypes(sp, mv$(node.type));
        node.type = HIRTypeRef();
        if (node.isStruct) {
            ASSERT_BUG(sp, ((*t).is_Path() && ((*t).as_Path().path.data.is_Generic())), "Struct literal with non-Generic path - " << t);
            node.realPath = t->as_Path().path.data.as_Generic().clone();
        } else {
            ASSERT_BUG(sp, ((*t).is_Path() && ((*t).as_Path().path.data.is_UfcsInherent())), "Enum struct literal with non-UfcsInherent path - " << t);
            auto& it = t->as_Path().path.data.as_UfcsInherent().type;
            auto& name = t->as_Path().path.data.as_UfcsInherent().item;
            ASSERT_BUG(sp, ((*it).is_Path() && ((*it).as_Path().path.data.is_Generic())), "Struct literal with non-Generic path - " << t);
            node.realPath = it->as_Path().path.data.as_Generic().clone();
            node.realPath.path += name;
        }
    }
    auto& tyPath = node.realPath;

    const auto ty = this->getStructenumTy(node.span(), node.isStruct, tyPath);
    this->context.equateTypes(node.span(), node.resType, ty);
    if (const auto* expectedHint = this->context.coercionHint(node)) {
        const auto* expected = this->context.getType(expectedHint);
        const auto* actualPath = ty->opt_Path();
        const auto* expectedPath = expected->opt_Path();
        if (actualPath && expectedPath && actualPath->path.data.is_Generic() && expectedPath->path.data.is_Generic() && actualPath->path.data.as_Generic().path == expectedPath->path.data.as_Generic().path && ty->compareWithPlaceholders(sp, expected, this->context.ivars.callbackResolveInfer()) != HIRCompare::Unequal) {
            this->context.equateTypes(sp, ty, expected);
        }
    }
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
                ASSERT_BUG(sp, idx != SIZE_MAX, "");
                ASSERT_BUG(sp, enm.data.is_Data(), "");
                const auto& var = enm.data.as_Data()[idx];
                if (var.type == this->context.crate.types.unit()) {
                    ASSERT_BUG(node.span(), node.values.size() == 0, "Values provided for unit-like variant");
                    ASSERT_BUG(node.span(), !node.baseValue, "Values provided for unit-like variant");
                    return;
                }
                const auto& str = *var.type->as_Path().binding.as_Struct();

                ASSERT_BUG(sp, var.isStruct, "Struct literal for enum on non-struct variant");
                fieldsPtr = &str.data.as_Named();
                generics = &enm.params;
                break;
            }
            case HIRTypePathBinding::TAG_Union: {
                auto& e = tuMatch.as_Union();
                fieldsPtr = &e->variants;
                generics = &e->params;
                if (node.baseValue) {
                    ERROR(node.span(), E0000, "Union can't have a base value");
                }
                ASSERT_BUG(node.span(), node.values.size() > 0, "Union literal with no values");
                ASSERT_BUG(node.span(), node.values.size() == 1, "Union literal with multiple values");
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
                    ASSERT_BUG(node.span(), node.values.size() == 0, "Values provided for " << e->data.tagStr() << "-like struct");

                    if (node.baseValue) {
                        auto _ = this->pushInnerCoerceScoped(false);
                        node.baseValue->visit(*this);
                        this->inheritDivergence(node, *node.baseValue);
                    }
                    return;
                }

                ASSERT_BUG(node.span(), e->data.is_Named(), "StructLiteral not pointing to a braced struct, instead " << e->data.tagStr() << " - " << ty);
                fieldsPtr = &e->data.as_Named();
                generics = &e->params;
                break;
            }
        }
    }
    ASSERT_BUG(node.span(), fieldsPtr, "");
    assert(generics);
    const tStructFields& fields = *fieldsPtr;

    auto monomorphCb = MonomorphStatePtr(this->context.crate.types, ty, &tyPath.params, nullptr);

    node.valueTypes.resize(fields.size());

    for (auto& val : node.values) {
        const auto& name = val.first;
        auto it = std::find_if(fields.begin(), fields.end(), [&](const HIRStructField& v) -> bool {
            return v.name == name;
        });
        ASSERT_BUG(node.span(), it != fields.end(), "Field '" << name << "' not found in struct " << tyPath);
        const auto& desTyR = it->ty;
        auto& desTyCache = node.valueTypes[it - fields.begin()];
        const auto* desTy = &desTyR;

        if (monomorphiseTypeNeeded(desTyR)) {
            if (desTyCache == HIRTypeRef()) {
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
    ASSERT_BUG(node.span(), generics, "Unit variant has invalid type " << ty);
    auto monomorph = MonomorphStatePtr(this->context.crate.types, ty, &node.path.params, nullptr);
    applyBoundsAsRules(this->context, node.span(), *generics, monomorph, /*is_impl_level=*/true);
}

auto ExprVisitorEnum::visit(HIRExprNodeCallPath& node) -> void {
    this->visitPath(node.span(), node.path);
    for (auto& val : node.args) {
        this->context.addIvars(val->resType);
    }

    const bool cacheOk = visitCallPopulateCache(this->context, node.span(), node.path, node.cache);
    if (cacheOk) {
        assert(node.cache.argTypes.size() >= 1);
        unsigned int expArgc = node.cache.argTypes.size() - 1;

        if (node.args.size() != expArgc) {
            if (node.cache.fcn->variadic && node.args.size() > expArgc) {
            } else {
                ERROR(node.span(), E0000, "Incorrect number of arguments to " << node.path << " - exp " << expArgc << " got " << node.args.size());
            }
        }

        // TODO: Figure out a way to disable coercions in desugared for loops (will speed up typecheck)

        for (unsigned int i = 0; i < node.cache.argTypes.size() - 1; i++) {
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
    this->context.addIvars(node.value->resType);
    for (auto& val : node.args) {
        this->context.addIvars(val->resType);
        node.argIvars.push_back(this->context.ivars.newIvarTr());
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
    this->context.addIvars(node.value->resType);
    for (auto& val : node.args) {
        this->context.addIvars(val->resType);
    }
    for (auto& ty : node.params.types) {
        this->context.addIvars(ty);
    }

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
            if (std::none_of(possibleTraits.begin(), possibleTraits.end(), [&](const auto& x) {
                return x.second == &tr;
            })) {
                possibleTraits.push_back(std::make_pair(&p, &tr));
            }
        } else {
        }
    };
    auto visitTrait = [&visitTraitInner](const HIRSimplePath& p, const HIRTrait& trait) {
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
    node.traitParamIvars.reserve(maxNumParams + maxNumValueParams);
    for (unsigned int i = 0; i < maxNumParams; i++) {
        node.traitParamIvars.push_back(this->context.ivars.newIvar());
    }
    for (unsigned int i = 0; i < maxNumValueParams; i++) {
        node.traitParamIvars.push_back(this->context.ivars.newIvarVal());
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
    this->context.addIvars(node.value->resType);

    node.value->visit(*this);
    this->inheritDivergence(node, *node.value);

    this->context.addRevisit(node);
}

auto ExprVisitorEnum::visit(HIRExprNodeTuple& node) -> void {
    node.diverges = false;
    for (auto& val : node.vals) {
        this->context.addIvars(val->resType);
    }

    if (canCoerceInnerResult()) {
        const auto& ty = this->context.getType(node.resType);
        if (const auto* e = ty->opt_Tuple()) {
            if (e->size() != node.vals.size()) {
                ERROR(node.span(), E0000, "Tuple literal node count mismatches with return type");
            }
        } else if (ty->is_Infer()) {
            std::vector<HIRTypeRef> tupleTys;
            for (const auto& val : node.vals) {
                tupleTys.push_back(this->context.ivars.newIvarTr());
            }
            this->context.equateTypes(node.span(), node.resType, this->context.crate.types.tuple(mv$(tupleTys)));
        } else {
            ERROR(node.span(), E0000, "Tuple literal used where a non-tuple expected - " << ty);
        }
        const auto& innerTys = this->context.getType(node.resType)->as_Tuple();
        assert(innerTys.size() == node.vals.size());

        for (unsigned int i = 0; i < innerTys.size(); i++) {
            this->context.equateTypesCoerce(node.span(), innerTys[i], node.vals[i]);
        }
    } else {
        std::vector<HIRTypeRef> tupleTys;
        for (const auto& val : node.vals) {
            tupleTys.push_back(val->resType);
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
    node.diverges = false;
    auto _ = this->pushInnerCoerceScoped(true);
    for (auto& val : node.vals) {
        this->context.addIvars(val->resType);
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
    node.diverges = false;
    this->context.addIvars(node.val->resType);

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
    HIRTypeRef ty;
    switch (node.data.tag()) {
        case HIRExprLiteral::TAG_Integer: {
            auto& e = node.data.as_Integer();
            if (e.type != HIRCoreType::Str) {
                ty = this->context.crate.types.primitive(e.type);
            } else {
                ty = this->context.crate.types.infer(~0, HIRInferClass::Integer);
            }
            break;
        }
        case HIRExprLiteral::TAG_Float: {
            auto& e = node.data.as_Float();
            if (e.type != HIRCoreType::Str) {
                ty = this->context.crate.types.primitive(e.type);
            } else {
                ty = this->context.crate.types.infer(~0, HIRInferClass::Float);
            }
            break;
        }
        case HIRExprLiteral::TAG_Boolean: {
            auto& e = node.data.as_Boolean();
            ty = this->context.crate.types.primitive(HIRCoreType::Bool);
            break;
        }
        case HIRExprLiteral::TAG_String: {
            // TODO: &'static
            ty = this->context.crate.types.borrow(HIRBorrowType::Shared, this->context.crate.types.primitive(HIRCoreType::Str));
            break;
        }
        case HIRExprLiteral::TAG_ByteString: {
            auto& e = node.data.as_ByteString();
            // TODO: &'static
            ty = this->context.crate.types.borrow(HIRBorrowType::Shared, this->context.crate.types.array(this->context.crate.types.primitive(HIRCoreType::U8), e.size()));
            break;
        }
        case HIRExprLiteral::TAG_CString: {
            auto p = context.crate.getLangItemPath(node.span(), "CStr");
            ty = this->context.crate.types.path(p, &context.crate.getStructByPath(node.span(), p));
            ty = this->context.crate.types.borrow(HIRBorrowType::Shared, ty);
            break;
        }
    }
    this->context.addIvars(ty);
    this->context.equateTypes(node.span(), node.resType, ty);
}

auto ExprVisitorEnum::visit(HIRExprNodePathValue& node) -> void {
    const auto& sp = node.span();
    this->visitPath(sp, node.path);

    this->addIvarsPath(node.span(), node.path);

    switch (node.path.data.tag()) {
        case HIRPathData::TAG_Generic: {
            auto& e = node.path.data.as_Generic();
            switch (node.target) {
                case HIRExprNodePathValue::UNKNOWN:
                    BUG(sp, "_PathValue with target=UNKNOWN and a Generic path - " << e.path);
                case HIRExprNodePathValue::FUNCTION: {
                    const auto& f = this->context.crate.getFunctionByPath(sp, e.path);
                    fixParamCount(sp, this->context, HIRTypeRef(), false, e, f.params, e.params);

                    auto ms = MonomorphStatePtr(this->context.crate.types, nullptr, nullptr, &e.params);
                    auto ty = this->context.crate.types.intern(HIRTypeData::make_NamedFunction({node.path.clone(), &f}));

                    applyBoundsAsRules(this->context, sp, f.params, ms, /*is_impl_level=*/false);

                    this->context.equateTypes(sp, node.resType, ty);
                } break;
                case HIRExprNodePathValue::STRUCT_CONSTR: {
                    const auto& s = this->context.crate.getStructByPath(sp, e.path);
                    fixParamCount(sp, this->context, HIRTypeRef(), false, e, s.params, e.params);

                    auto ms = MonomorphStatePtr(this->context.crate.types, nullptr, &e.params, nullptr);
                    auto ty = this->context.crate.types.intern(HIRTypeData::make_NamedFunction({node.path.clone(), &s}));

                    applyBoundsAsRules(this->context, sp, s.params, ms, /*is_impl_level=*/true);
                    this->context.equateTypes(sp, node.resType, ty);
                } break;
                case HIRExprNodePathValue::ENUM_VAR_CONSTR: {
                    const auto& varName = e.path.components().back();
                    auto enumPath = getRuleParentPath(e.path);
                    const auto& enm = this->context.crate.getEnumByPath(sp, enumPath);
                    fixParamCount(sp, this->context, HIRTypeRef(), false, e, enm.params, e.params);
                    size_t idx = enm.findVariant(varName);
                    ASSERT_BUG(sp, idx != SIZE_MAX, "Missing variant - " << e.path);
                    ASSERT_BUG(sp, enm.data.is_Data(), "Enum " << enumPath << " isn't a data-holding enum");

                    auto ms = MonomorphStatePtr(this->context.crate.types, nullptr, &e.params, nullptr);
                    auto ty = this->context.crate.types.intern(HIRTypeData::make_NamedFunction({node.path.clone(), HIRTypeDataNamedFunctionTy::make_EnumConstructor({&enm, idx})}));
                    applyBoundsAsRules(this->context, sp, enm.params, ms, /*is_impl_level=*/true);
                    this->context.equateTypes(sp, node.resType, ty);
                } break;
                case HIRExprNodePathValue::STATIC: {
                    const auto& v = this->context.crate.getStaticByPath(sp, e.path);
                    this->context.equateTypes(sp, node.resType, v.type);
                } break;
                case HIRExprNodePathValue::CONSTANT: {
                    const auto& v = this->context.crate.getConstantByPath(sp, e.path);
                    fixParamCount(sp, this->context, HIRTypeRef(), false, e, v.params, e.params);

                    auto ms = MonomorphStatePtr(this->context.crate.types, nullptr, nullptr, &e.params);
                    applyBoundsAsRules(this->context, sp, v.params, ms, /*is_impl_level=*/false);

                    HIRTypeRef tmp;
                    const auto& ty = ms.maybeMonomorphType(sp, tmp, v.type);
                    this->context.equateTypes(sp, node.resType, ty);
                } break;
            }
            break;
        }
        case HIRPathData::TAG_UfcsUnknown: {
            BUG(sp, "Encountered UfcsUnknown");
            break;
        }
        case HIRPathData::TAG_UfcsKnown: {
            auto& e = node.path.data.as_UfcsKnown();
            const auto& trait = this->context.crate.getTraitByPath(sp, e.trait.path);
            fixParamCount(sp, this->context, e.type, true, e.trait, trait.params, e.trait.params);

            this->context.addTraitBound(sp, e.type, e.trait.path, e.trait.params.clone());

            auto it = trait.values.find(e.item);
            if (it == trait.values.end()) {
                ERROR(sp, E0000, "`" << e.item << "` is not a value member of trait " << e.trait.path);
            }
            switch (it->second.tag()) {
                case HIRTraitValueItem::TAG_Constant: {
                    auto& ie = it->second.as_Constant();
                    fixParamCount(sp, this->context, e.type, false, node.path, ie.params, e.params);

                    auto ms = MonomorphStatePtr(this->context.crate.types, e.type, &e.trait.params, &e.params);
                    applyBoundsAsRules(this->context, sp, ie.params, ms, /*is_impl_level=*/false);

                    HIRTypeRef tmp;
                    const auto& ty = ms.maybeMonomorphType(sp, tmp, ie.type);
                    this->context.equateTypes(sp, node.resType, ty);
                    break;
                }
                case HIRTraitValueItem::TAG_Static: {
                    auto& ie = it->second.as_Static();
                    TODO(sp, "Monomorpise associated static type - " << ie.type);
                    break;
                }
                case HIRTraitValueItem::TAG_Function: {
                    auto& ie = it->second.as_Function();
                    fixParamCount(sp, this->context, e.type, false, node.path, ie.params, e.params);

                    auto ms = MonomorphStatePtr(this->context.crate.types, e.type, &e.trait.params, &e.params);
                    applyBoundsAsRules(this->context, sp, ie.params, ms, /*is_impl_level=*/false);

                    auto ty = this->context.crate.types.intern(HIRTypeData::make_NamedFunction({node.path.clone(), &ie}));
                    this->context.equateTypes(node.span(), node.resType, ty);
                    break;
                }
            }
            break;
        }
        case HIRPathData::TAG_UfcsInherent: {
            auto& e = node.path.data.as_UfcsInherent();
            this->context.selectWellFormed(sp, e.type);
            auto lookupType = this->context.revealOpaqueTypes(e.type);
            lookupType = this->context.expandAssociatedTypes(sp, mv$(lookupType));
            e.type = lookupType;
            // TODO: Share code with visit_call_populate_cache

            const HIRFunction* fcnPtr = nullptr;
            const HIRConstant* constPtr = nullptr;
            const HIRTypeImpl* implPtr = nullptr;
            // TODO: Support mutiple matches here (if there's a fuzzy match) and retry if so
            unsigned int count = 0;
            this->context.crate.findTypeImpls(lookupType, context.ivars.callbackResolveInfer(), [&](const auto& impl) {
                if (!inherentImplMatchesReceiver(this->context, sp, impl, lookupType)) {
                    return false;
                }
                {
                    auto it = impl.methods.find(e.item);
                    if (it != impl.methods.end()) {
                        fcnPtr = &it->second.data;
                        implPtr = &impl;
                        count += 1;
                        return false;
                    }
                }
                {
                    auto it = impl.constants.find(e.item);
                    if (it != impl.constants.end()) {
                        constPtr = &it->second.data;
                        implPtr = &impl;
                        count += 1;
                        return false;
                    }
                }
                return false;
            });
            if (count == 0) {
                ERROR(sp, E0000, "Failed to locate associated value " << node.path);
            }
            if (count > 1) {
                TODO(sp, "Revisit _PathValue when UfcsInherent has multiple options - " << node.path);
            }

            assert(fcnPtr || constPtr);
            assert(implPtr);

            if (fcnPtr) {
                fixParamCount(sp, this->context, e.type, false, node.path, fcnPtr->params, e.params);
            } else {
                fixParamCount(sp, this->context, e.type, false, node.path, constPtr->params, e.params);
            }

            auto& implParams = e.implParams;
            if (implPtr->params.isGeneric()) {
                while (implParams.types.size() < implPtr->params.types.size()) {
                    implParams.types.push_back(this->context.crate.types.infer());
                }
                implParams.values.resize(implPtr->params.values.size());
                OwnedImplMatcher matcher(context.crate.types, implParams);
                bool r = implPtr->type->matchTestGenerics(sp, lookupType, this->context.ivars.callbackResolveInfer(), matcher);
                for (auto& ty : implParams.types) {
                    if (ty->is_Infer() && ty->as_Infer().index == ~0u) {
                        this->context.addIvars(ty);
                    }
                }
                if (!r) {
                    auto t = MonomorphStatePtr(this->context.crate.types, nullptr, &implParams, nullptr).monomorphType(sp, implPtr->type);
                    this->context.equateTypes(node.span(), t, e.type);
                }
            }

            {
                auto implType = MonomorphStatePtr(this->context.crate.types, e.type, &implParams, nullptr).monomorphType(sp, implPtr->type);
                this->context.equateTypes(node.span(), e.type, implType);
            }
            e.type = this->context.revealOpaqueTypes(e.type);

            if (fcnPtr) {
                const auto& fcnParams = e.params;
                // TODO: call `context.get_type` in this?
                auto ms = MonomorphStatePtr(this->context.crate.types, e.type, &implParams, &fcnParams);

                applyBoundsAsRules(this->context, sp, implPtr->params, ms, /*is_impl_level=*/true);
                applyBoundsAsRules(this->context, sp, fcnPtr->params, ms, /*is_impl_level=*/false);

                auto ty = this->context.crate.types.intern(HIRTypeData::make_NamedFunction({node.path.clone(), fcnPtr}));
                this->context.equateTypes(node.span(), node.resType, ty);
            } else {
                assert(constPtr);
                auto monomorphCb = MonomorphStatePtr(this->context.crate.types, e.type, &implParams, &e.params);

                HIRTypeRef tmp;
                const auto& ty = monomorphCb.maybeMonomorphType(sp, tmp, constPtr->type);

                applyBoundsAsRules(this->context, sp, implPtr->params, monomorphCb, /*is_impl_level=*/true);
                applyBoundsAsRules(this->context, sp, constPtr->params, monomorphCb, /*is_impl_level=*/false);
                this->context.equateTypes(node.span(), node.resType, ty);
            }
            break;
        }
    }
}

auto ExprVisitorEnum::visit(HIRExprNodeVariable& node) -> void {
    this->context.equateTypes(node.span(), node.resType, this->context.getVar(node.span(), node.slot));
}

auto ExprVisitorEnum::visit(HIRExprNodeConstParam& node) -> void {
    this->context.equateTypes(node.span(), node.resType, this->context.resolve.getConstParamType(node.span(), node.binding));
}

auto ExprVisitorEnum::visit(HIRExprNodeClosure& node) -> void {
    for (auto& arg : node.args) {
        this->context.addIvars(arg.second);
        this->context.handlePattern(node.span(), arg.first, arg.second);
    }
    this->context.addIvars(node.returnType);
    this->context.addIvars(node.code->resType);

    std::vector<HIRTypeRef> argTypes;
    for (auto& arg : node.args) {
        argTypes.push_back(arg.second);
    }
    this->context.equateTypes(node.span(), node.resType, this->context.crate.types.closure(&node));

    this->context.equateTypesCoerce(node.span(), node.returnType, node.code);

    auto savedLoops = std::move(this->loopBlocks);

    auto _ = this->pushInnerCoerceScoped(true);
    this->closureRetTypes.push_back(RetTarget(node.returnType));
    node.code->visit(*this);
    this->closureRetTypes.pop_back();

    this->loopBlocks = std::move(savedLoops);
}

auto ExprVisitorEnum::visit(HIRExprNodeGenerator& node) -> void {
    this->context.addIvars(node.returnType);
    this->context.addIvars(node.yieldTy);
    this->context.addIvars(node.resumeTy);
    this->context.addIvars(node.code->resType);
    if (node.hasResumePattern) {
        this->context.handlePattern(node.span(), node.resumePattern, node.resumeTy);
    }

    this->context.equateTypes(node.span(), node.resType, this->context.crate.types.generator(&node));

    this->context.equateTypesCoerce(node.span(), node.returnType, node.code);
    // TODO: Save/clear/restore loop labels
    auto _ = this->pushInnerCoerceScoped(true);
    this->closureRetTypes.push_back(RetTarget(node.returnType, node.resumeTy, node.yieldTy));
    node.code->visit(*this);
    this->closureRetTypes.pop_back();
}

auto ExprVisitorEnum::visit(HIRExprNodeGeneratorWrapper& node) -> void {
    BUG(node.span(), "ExprNode_GeneratorWrapper unexpected at this time");
}

auto ExprVisitorEnum::visit(HIRExprNodeAsyncBlock& node) -> void {
    ASSERT_BUG(node.span(), node.code, "empty async?");
    node.returnType = this->context.revealOpaqueType(node.returnType);
    this->context.addIvars(node.returnType);
    this->context.addIvars(node.code->resType);

    this->context.equateTypes(node.span(), node.resType, this->context.crate.types.asyncBlock(&node));
    this->context.equateTypesCoerce(node.span(), node.returnType, node.code);

    // TODO: Save/clear/restore loop labels
    auto _ = this->pushInnerCoerceScoped(true);
    if (node.isAsyncGen) {
        this->context.addIvars(node.yieldTy);
        this->closureRetTypes.push_back(RetTarget(node.returnType, this->context.crate.types.unit(), node.yieldTy));
    } else {
        this->closureRetTypes.push_back(RetTarget(node.returnType));
    }
    node.code->visit(*this);
    this->closureRetTypes.pop_back();
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
            this->context.addIvars(e.type);
            this->visitGenericPath(sp, e.trait);
            this->context.ivars.addIvarsParams(e.params);
            break;
        }
        case HIRPath::Data::TAG_UfcsUnknown: {
            TODO(sp, "Hit a UfcsUnknown (" << path << ") - Is this an error?");
            break;
        }
        case HIRPath::Data::TAG_UfcsInherent: {
            auto& e = path.data.as_UfcsInherent();
            this->context.addIvars(e.type);
            this->context.ivars.addIvarsParams(e.params);
            this->context.ivars.addIvarsParams(e.implParams);
            break;
        }
    }
}

auto ExprVisitorEnum::pushInnerCoerceScoped(bool val) -> InnerCoerceGuard {
    this->innerCoerceEnabledStack.push_back(val);
    return InnerCoerceGuard(*this);
}

auto ExprVisitorEnum::pushInnerCoerce(bool val) -> void {
    this->innerCoerceEnabledStack.push_back(val);
}

auto ExprVisitorEnum::popInnerCoerce() -> void {
    assert(this->innerCoerceEnabledStack.size());
    this->innerCoerceEnabledStack.pop_back();
}

auto ExprVisitorEnum::canCoerceInnerResult() const -> bool {
    if (this->innerCoerceEnabledStack.size() == 0) {
        return true;
    } else {
        return this->innerCoerceEnabledStack.back();
    }
}

auto ExprVisitorEnum::equateTypesInnerCoerce(const Span& sp, const HIRTypeData* target, HIRExprNodeP& node) -> void {
    if (canCoerceInnerResult()) {
        this->context.equateTypesCoerce(sp, target, node);
    } else {
        this->context.equateTypes(sp, target, node->resType);
    }
}

ExprVisitorEnum::RetTarget::RetTarget(const HIRTypeData* retType)
    : retType(retType)
    , resumeType(nullptr)
    , yieldType(nullptr)
{
}

ExprVisitorEnum::RetTarget::RetTarget(const HIRTypeData* retType, const HIRTypeData* resumeType, const HIRTypeData* yieldType)
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

auto ExprVisitorEnum::RevisitDefaultUnit::fmt(std::ostream& os) const -> void {
    os << "RevisitDefaultUnit(" << node << ": " << node->resType << ")";
}

auto ExprVisitorEnum::RevisitDefaultUnit::revisit(Context& context, bool isFallback) -> bool {
    const auto& ty = context.getType(node->resType);
    if (const auto* i = ty->opt_Infer()) {
        if (i->tyClass != HIRInferClass::None) {
            return true;
        }
        if (isFallback) {
            if (i->index < context.possibleIvarVals.size() && context.possibleIvarVals[i->index].hasRules()) {
                return false;
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
    t.innerCoerceEnabledStack.pop_back();
}

RpitOriginMonomorph::RpitOriginMonomorph(HIRTypeInterner& types)
    : HIRMatchGenerics(types.objectPool())
    , Monomorphiser(types)
{
}

auto RpitOriginMonomorph::matchTy(const HIRGenericRef& generic, const HIRTypeData* type, tCbResolveType resolve) -> HIRCompare {
    type = resolve.getType(Span(), type);
    auto inserted = typeBindings.emplace(generic.binding, type);
    if (inserted.second) {
        return HIRCompare::Equal;
    }
    return inserted.first->second->compareWithPlaceholders(Span(), type, resolve);
}

auto RpitOriginMonomorph::matchVal(const HIRGenericRef& generic, const HIRConstGeneric& value) -> HIRCompare {
    auto inserted = valueBindings.emplace(generic.binding, value.clone());
    return inserted.second || inserted.first->second == value ? HIRCompare::Equal : HIRCompare::Unequal;
}

auto RpitOriginMonomorph::getType(const Span&, const HIRGenericRef& generic) const -> HIRTypeRef {
    const auto it = typeBindings.find(generic.binding);
    return it == typeBindings.end() ? types.generic(generic.name, generic.binding) : it->second;
}

auto RpitOriginMonomorph::getValue(const Span&, const HIRGenericRef& generic) const -> HIRConstGeneric {
    const auto it = valueBindings.find(generic.binding);
    return it == valueBindings.end() ? HIRConstGeneric::make_Generic(generic) : it->second.clone();
}
