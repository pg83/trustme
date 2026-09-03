#include "hir_expand_main_bindings.h"

#include "hir_hir.h"
#include "hir_expr.h"
#include "settings.h"
#include "wire_board.h"
#include "hir_visitor.h"
#include "trans_target.h"
#include "hir_expr_state.h"
#include "hir_typeck_common.h"
#include "hir_typeck_static.h"
#include "hir_typeck_monomorph.h"
#include "hir_conv_constant_evaluation.h"

#include <std/alg/range.h>
#include <std/lib/vector.h>
#include <std/mem/obj_pool.h>

#include <algorithm>

using namespace stl;

namespace {
#include "hir_expand_crnode_tu.h"

    typedef std::vector<std::pair<HIRExprNodeClosure::Class, HIRTraitImpl>> outImplsClosureT;
    typedef std::vector<std::pair<const char*, HIRTraitImpl>> outTraitImplsT;

    struct ClosureScope {
        HIRExprNodeClosure& node;
        Vector<unsigned int> localVars;
        std::vector<HIRExprNodeClosure::AvuCache::Capture> capturedVars;

        ClosureScope(HIRExprNodeClosure& node);
    };

    struct CoroutineScope {
        enum : unsigned {
            STACK_MARKER_LOOP = ~0u,
        };

        CRNode node;

        Vector<unsigned> yieldStack;

        struct Var {
            Vector<unsigned> definedStack;
            Vector<unsigned> lastUsedStack;
            HIRValueUsage usage;
        };

        std::map<unsigned, Var> usedVariables;

        CoroutineScope(HIRExprNodeGenerator& node);

        CoroutineScope(HIRExprNodeAsyncBlock& node);
    };

#include "hir_expand_scope_tu.h"

    struct AnnotateExprVisitorMark: public HIRExprVisitor {
        const StaticTraitResolve& resolve_;
        const Vector<const HIRType*>& variableTypes;

        Vector<HIRValueUsage> usage;
        std::vector<Scope> closureStack;
        Vector<HIRExprNodeCallValue*> pendingCalls;
        bool ignoreVariableCapture;

        struct UsageGuard {
            AnnotateExprVisitorMark& parent;
            bool pop;

            UsageGuard(AnnotateExprVisitorMark& parent, bool pop);

            ~UsageGuard();
        };

        HIRValueUsage getUsage() const;

        UsageGuard pushUsage(HIRValueUsage u);

        AnnotateExprVisitorMark(const StaticTraitResolve& resolve, const Vector<const HIRType*>& variableTypes);

        void visitRoot(HIRExprPtr& rootPtr);

        void visitNodePtr(HIRExprNodeP& nodePtr) override;

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

        void visit(HIRExprNodeUniOp& node) override;

        void visit(HIRExprNodeBorrow& node) override;

        void visit(HIRExprNodeRawBorrow& node) override;

        void visit(HIRExprNodeBinOp& node) override;

        void visit(HIRExprNodeCast& node) override;

        void visit(HIRExprNodeUnsize& node) override;

        void visit(HIRExprNodeIndex& node) override;

        void visit(HIRExprNodeDeref& node) override;

        void visit(HIRExprNodeEmplace& node) override;

        void visit(HIRExprNodeField& node) override;

        void visit(HIRExprNodeTupleVariant& node) override;

        void visit(HIRExprNodeCallPath& node) override;

        void visit(HIRExprNodeCallValue& node) override;

        void visit(HIRExprNodeCallMethod& node) override;

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

        void visit(HIRExprNodeGeneratorWrapper&) override;

        void visit(HIRExprNodeAsyncBlock& node) override;

        void addVarDefClosure(const Span& sp, ClosureScope& e, unsigned int slot);

        void addVarDefGenerator(const Span& sp, CoroutineScope& scope, unsigned int slot);

        void addVarDef(const Span& sp, unsigned int slot);

        void addClosureDefFromPattern(const Span& sp, const HIRPattern& pat);

        void addDefsFromPattern(const Span& sp, const HIRPattern& pat);

        HIRValueUsage getUsageForPatternBinding(const Span& sp, const HIRPatternBinding& pb, const HIRType* ty) const;

        HIRValueUsage getUsageForPattern(const Span& sp, const HIRPattern& pat, const HIRType* outerTy) const;

        bool typeIsCopyHere(const Span& sp, const HIRType* type);

        HIRValueUsage getRealUsage(const Span& sp, unsigned slot, const Vector<RcString>& fields, HIRValueUsage usage);

        void markUsedVariableClosure(const Span& sp, ClosureScope& closureRec, unsigned slot, Vector<RcString> fields, HIRValueUsage usage);

        void markUsedVariableGenerator(const Span& sp, CoroutineScope& scope, unsigned int slot, Vector<RcString> fields, HIRValueUsage usage);

        void markUsedVariable(const Span& sp, unsigned int slot, Vector<RcString> fields, HIRValueUsage usage);

        void applyCoroutine(const Span& sp, bool isMove, bool isCoroutineClosureBody, HIRExprNodeGenerator::AvuCache& avuCache, std::map<unsigned, CoroutineScope::Var>& usedVariables);
    };

    struct AnnotateOuterVisitor: public HIRVisitor {
        StaticTraitResolve resolve_;

        AnnotateOuterVisitor(const WireBoard& wb);

        void visitExpr(HIRExprPtr& exp) override;

        void visitFunction(HIRItemPath p, HIRFunction& item) override;

        void visitStatic(HIRItemPath p, HIRStatic& item) override;

        void visitConstant(HIRItemPath p, HIRConstant& item) override;

        void visitEnum(HIRItemPath p, HIREnum& item) override;

        void visitTrait(HIRItemPath p, HIRTrait& item) override;

        void visitTypeImpl(HIRTypeImpl& impl) override;

        void visitTraitImpl(const HIRSimplePath& traitPath, HIRTraitImpl& impl) override;
    };

    struct ClosureTypeCallback {
        virtual std::pair<HIRSimplePath, HIRTypeItem*> create(const char* prefix, const char* suffix, HIRTypeItem item) = 0;
    };

    template <typename F>
    struct ClosureTypeCb final: ClosureTypeCallback {
        F f;

        explicit ClosureTypeCb(F f);

        std::pair<HIRSimplePath, HIRTypeItem*> create(const char* prefix, const char* suffix, HIRTypeItem item) override;
    };

    struct OutState {
        outImplsClosureT implsClosure;
        outTraitImplsT traitImpls;
        std::vector<std::unique_ptr<HIRTypeImpl>> implsType;

        ClosureTypeCallback* newType = nullptr;
        Vector<std::pair<const HIRExprNode*, HIRExprNode*>> mutableNodes;

        void pushNewImpls(const Span& sp, HIRCrate& crate);

        struct Counts {
            size_t closure;
            size_t traits;
            size_t type;
        };

        Counts saveCounts() const;

        void updateSourceModule(Counts c, const HIRSimplePath& path);
    };

    struct ClosureExprVisitorMutate: public HIRExprVisitorDef {
        const HIRType* closureType;
        const Vector<unsigned int>& localVars;
        const std::vector<HIRExprNodeClosure::AvuCache::Capture>& captures;

        const Monomorphiser& monomorphiser;
        ObjPool* pool;

        HIRExprNodeP replacement_;

        ClosureExprVisitorMutate(ObjPool* pool, const HIRType* closureType, const Vector<unsigned int>& localVars, const std::vector<HIRExprNodeClosure::AvuCache::Capture>& captures, const Monomorphiser& mcb);

        void visitPattern(const Span& sp, HIRPattern& pat) override;

        [[nodiscard]] const HIRType* visitType(const HIRType* ty) override;

        void visitPathParams(HIRPathParams& pp) override;

        void visit(HIRExprNodeArraySized& node) override;

        void visitNodePtr(HIRExprNodeP& nodePtr) override;

        void visit(HIRExprNodeClosure& node) override;

        void visit(HIRExprNodeGenerator& node) override;

        void visit(HIRExprNodeAsyncBlock& node) override;

        void visit(HIRExprNodeVariable& node) override;

        void visit(HIRExprNodeField& node) override;

        void visit(HIRExprNodeConstParam& node) override;

        HIRExprNodeP getSelf(const Span& sp) const;
    };

    struct AnonymousTypeMonomorph: public MonomorphiserNop {
        const Monomorphiser& pathMonomorphiser;
        bool allowUnextracted;

        AnonymousTypeMonomorph(const Monomorphiser& pathMonomorphiser, bool allowUnextracted);

        const HIRType* monomorphType(const Span& sp, const HIRType* ty, bool allowInfer) const override;
    };

    struct ClosureExprVisitorFixup: public HIRExprVisitorDef {
        const HIRCrate& crate;
        StaticTraitResolve resolve_;
        ObjPool* pool;
        const Monomorphiser& monomorphiser;
        const OutState* out;
        bool allowUnextracted;
        bool runEat;

        ClosureExprVisitorFixup(const WireBoard& wb, const HIRGenericParams* params, const Monomorphiser& monomorphiser, const OutState* out, bool allowUnextracted = false);

        void visitRoot(HIRExprPtr& root);

        void visitNodePtr(HIRExprNodeP& node) override;

        void visit(HIRExprNodeCast& node) override;

        void visit(HIRExprNodeCallValue& node) override;

        [[nodiscard]] const HIRType* visitType(const HIRType* ty) override;
    };

    struct H {
        static void fixFnParams(HIRExprPtr& code, const HIRType* selfTy, const HIRType* argsTy);

        static HIRTypeImpl makeFnfree(HIRTypeInterner& types, HIRGenericParams params, const HIRType* closureType, std::vector<std::pair<HIRPattern, const HIRType*>> args, const HIRType* retTy, HIRExprPtr code);

        static HIRTraitImpl makeFnonce(HIRGenericParams params, HIRPathParams traitParams, const HIRType* closureType, std::pair<HIRPattern, const HIRType*> argsArgent, const HIRType* retTy, HIRExprPtr code);

        static HIRTraitImpl makeFnmut(HIRTypeInterner& types, HIRGenericParams params, HIRPathParams traitParams, const HIRType* closureType, std::pair<HIRPattern, const HIRType*> argsArgent, const HIRType* retTy, HIRExprPtr code);

        static HIRTraitImpl makeFn(HIRTypeInterner& types, HIRGenericParams params, HIRPathParams traitParams, const HIRType* closureType, std::pair<HIRPattern, const HIRType*> argsArgent, const HIRType* retTy, HIRExprPtr code);
    };

    struct ClosureExprVisitorExtract: public HIRExprVisitorDef {
        const StaticTraitResolve& resolve_;
        ObjPool* pool;
        const HIRType* selfType;
        const Vector<const HIRType*>& variableTypes;
        const HIRExprPtr& exprPtr;

        OutState& out;
        const char* newTypeSuffix;
        bool isAsyncDropIntrinsic;

        struct ActiveNode {
            const void* node;
            const ActiveNode* parent;
        };

        const ActiveNode* activeNode = nullptr;

        struct ActiveNodeGuard {
            const ActiveNode*& head;
            ActiveNode entry;

            ActiveNodeGuard(const ActiveNode*& head, const void* node);

            ~ActiveNodeGuard();
        };

        struct FrozenMonomorph: public MonomorphiserNop {
            const HIRPathParams& sourceParams;

            FrozenMonomorph(HIRTypeInterner& types, const HIRPathParams& sourceParams);

            const HIRType* getType(const Span& sp, const HIRGenericRef& generic) const override;

            HIRConstGeneric getValue(const Span& sp, const HIRGenericRef& generic) const override;
        };

        struct DeferredExprFixup: public HIRExprVisitorDef {
            const Monomorphiser& monomorphiser;

            explicit DeferredExprFixup(const Monomorphiser& monomorphiser);

            void visitRoot(HIRExprPtr& root);

            const HIRType* visitType(const HIRType* type) override;
        };

        struct DeferredItemFixup: public HIRVisitor {
            const Monomorphiser& monomorphiser;

            explicit DeferredItemFixup(const Monomorphiser& monomorphiser);

            const HIRType* visitType(const HIRType* type) override;

            void visitExpr(HIRExprPtr& expr) override;

            void visitGeneratedStruct(HIRStruct& item);
        };

        struct DeferredFixup {
            HIRPathParams sourceParams;
            OutState::Counts first;
            OutState::Counts last;
            HIRStruct* structure;
            DeferredFixup* next;

            DeferredFixup(HIRPathParams sourceParams, OutState::Counts first, OutState::Counts last, HIRStruct* structure, DeferredFixup* next);
        };

        DeferredFixup* deferredFixups = nullptr;

        ClosureExprVisitorExtract(const StaticTraitResolve& resolve, const HIRType* selfType, const Vector<const HIRType*>& varTypes, const HIRExprPtr& exprPtr, OutState& out, const char* newTypeSuffix, bool isAsyncDropIntrinsic = false);

        void visitRoot(HIRExprNode& root);

        void deferFixups(const HIRPathParams& sourceParams, OutState::Counts first, HIRStruct& structure);

        void finishDeferredFixups();

        bool isActive(const void* node) const;

        template <typename Node>
        void extractReferencedNode(const Span& sp, const Node* constNode);

        void extractReferencedNodeTypes(const Span& sp, const HIRType* type);

        struct Monomorph: public Monomorphiser {
            const StaticTraitResolve& resolve;
            HIRGenericParams& params;
            HIRPathParams& constructorPathParams;
            bool frozen = false;
            std::set<const HIRGenericBound*> addedBounds;

            Monomorph(const StaticTraitResolve& resolve, HIRGenericParams& params, HIRPathParams& constructorPathParams);

            HIRPathParams freeze();

            const HIRType* getType(const Span& sp, const HIRGenericRef& ge) const override;

            HIRConstGeneric getValue(const Span& sp, const HIRGenericRef& ge) const override;

            const HIRType* monomorphType(const Span& sp, const HIRType* tpl, bool allowInfer = true) const override;

            void maybeMonomorphBound(const Span& sp, const HIRGenericBound& bound);

            void addBounds(const Span& sp, const StaticTraitResolve& resolve);

            template <typename T, typename U>
            static bool contains(const std::vector<T>& l, const U& v);

            template <typename T, typename U>
            static bool contains(const ThinVector<T>& l, const U& v);
            enum class TypeNeed {
                NoGenerics,
                UsesOthers,
                Required,
            };

            bool updateTypeNeed(TypeNeed& rv, const HIRType* t) const;

            TypeNeed typeBoundNeeded(const Span& sp, const HIRType* ty) const;

            TypeNeed typeBoundNeeded(const Span& sp, const HIRGenericPath& tp) const;

            TypeNeed typeBoundNeeded(const Span& sp, const HIRTraitPath& tp) const;

            bool boundNeeded(const Span& sp, const HIRGenericBound& b) const;

            HIRGenericBound monomorphBound(const Span& sp, const HIRGenericBound& b) const;
        };

        Monomorph createParams(const Span& sp, const StaticTraitResolve& resolve, HIRGenericParams& params, HIRPathParams& constructorPathParams) const;

        void visit(HIRExprNodeClosure& node) override;

        struct ExprVisitorGeneratorRewrite: public HIRExprVisitorDef {
            const Monomorph& monomorph;
            const std::map<unsigned, unsigned>& variableRewrites;

            HIRExprNodeP replacement_;

            ExprVisitorGeneratorRewrite(const Monomorph& monomorph, const std::map<unsigned, unsigned>& rewrites);

            [[nodiscard]] const HIRType* visitType(const HIRType* ty) override;

            void visitPathParams(HIRPathParams& pp) override;

            void visitNodePtr(HIRExprNodeP& nodePtr) override;

            void visit(HIRExprNodeVariable& node) override;

            void visit(HIRExprNodeConstParam& node) override;

            void visit(HIRExprNodeArraySized& node) override;

            void visit(HIRExprNodeClosure& node) override;

            void visit(HIRExprNodeGenerator& node) override;

            void visit(HIRExprNodeAsyncBlock& node) override;

            void visitPattern(const Span& sp, HIRPattern& pat) override;

            void visitPatternBinding(const Span& sp, HIRPatternBinding& binding);
        };

        struct CrVars {
            unsigned nArgs;
            std::map<unsigned, unsigned> variableRewrites;
            Vector<HIRValueUsage> captureUsages;
            Vector<const HIRType*> newLocals;
            std::vector<HIRVisEnt<const HIRType*>> structEnts;
            std::vector<HIRExprNodeP> captureNodes;

            void setArguments(const Span& sp, Vector<const HIRType*> args);
        };

        void setStateType(const Span& sp, CrVars& vars, const HIRType* stateType) const;

        CrVars coroutineVars(const Span& sp, const HIRExprNodeGenerator::AvuCache& avuCache, unsigned nArgs, const Monomorph& monomorphCb, bool useClone = false) const;

        void fixCoroutineVarTypes(const Span& sp, const HIRGenericParams& params, const Monomorph& monomorphCb, CrVars& vars) const;

        unsigned countCoroutineSuspensions(HIRExprNodeP& code, bool countYields, bool countAwaits) const;

        static HIREnum makeCoroutineStateEnum(unsigned suspensionCount);

        void visit(HIRExprNodeGenerator& node) override;

        void visit(HIRExprNodeAsyncBlock& node) override;

        void visitPattern(const Span& sp, HIRPattern& pat) override;

        void visit(HIRExprNodeLoop& node) override;

        void visit(HIRExprNodeYield& node) override;
    };

    struct ClosureOuterVisitor: public HIRVisitor {
        StaticTraitResolve resolve_;
        OutState out;

        const HIRSimplePath* curModPath;
        const HIRType* selfType = nullptr;

        ClosureOuterVisitor(const WireBoard& wb);

        void visitCrate(HIRCrate& crate) override;

        void visitModule(HIRItemPath p, HIRModule& mod) override;

        void visitExpr(HIRExprPtr& exp) override;

        [[nodiscard]] const HIRType* visitType(const HIRType* ty) override;

        void visitConstgeneric(HIRConstGeneric&) override;

        void visitFunction(HIRItemPath p, HIRFunction& item) override;

        void visitStatic(HIRItemPath p, HIRStatic& item) override;

        void visitConstant(HIRItemPath p, HIRConstant& item) override;

        void visitEnum(HIRItemPath p, HIREnum& item) override;

        void visitTrait(HIRItemPath p, HIRTrait& item) override;

        void visitTypeImpl(HIRTypeImpl& impl) override;

        void visitTraitImpl(const HIRSimplePath& traitPath, HIRTraitImpl& impl) override;
    };

    struct ErasedExprVisitorExtract: public HIRExprVisitorDef {
        const StaticTraitResolve& resolve_;

        ErasedExprVisitorExtract(const StaticTraitResolve& resolve);

        void visitRoot(HIRExprPtr& root);

        void visitNodePtr(HIRExprNodeP& nodePtr) override;

        [[nodiscard]] const HIRType* visitType(const HIRType* ty) override;
    };

    struct ErasedOuterVisitor: public HIRVisitor {
        StaticTraitResolve resolve_;

        ErasedOuterVisitor(const WireBoard& wb);

        void visitExpr(HIRExprPtr& exp) override;
    };

    struct ErasedOuterVisitorFixup: public HIRVisitor {
        StaticTraitResolve resolve_;

        ErasedOuterVisitorFixup(const WireBoard& wb);

        void visitParams(HIRGenericParams& params) override;

        [[nodiscard]] const HIRType* visitType(const HIRType* ty) override;
    };

    struct ReborrowExprVisitorMutate: public HIRExprVisitorDef {
        const HIRCrate& crate;

        void markUniquePlace(HIRExprNodeP& node);

        ReborrowExprVisitorMutate(const HIRCrate& crate);

        void visitNodePtr(HIRExprPtr& root);

        void visitNodePtr(HIRExprNodeP& node) override;

        HIRExprNodeP doReborrow(HIRExprNodeP nodePtr);

        void visit(HIRExprNodeCast& node) override;

        void visit(HIRExprNodeEmplace& node) override;

        void visit(HIRExprNodeAssign& node) override;

        void visit(HIRExprNodeCallPath& node) override;

        void visit(HIRExprNodeCallValue& node) override;

        void visit(HIRExprNodeCallMethod& node) override;

        void visit(HIRExprNodeMatch& node) override;

        void visit(HIRExprNodeArrayList& node) override;

        void visit(HIRExprNodeTuple& node) override;

        void visit(HIRExprNodeTupleVariant& node) override;

        void visit(HIRExprNodeStructLiteral& node) override;

        void visit(HIRExprNodeUnsize& node) override;

        void visit(HIRExprNodeClosure& node) override;

        void visit(HIRExprNodeGenerator& node) override;

        void visit(HIRExprNodeAsyncBlock& node) override;
    };

    struct ReborrowOuterVisitor: public HIRVisitor {
        const HIRCrate& crate;

        ReborrowOuterVisitor(const HIRCrate& crate);

        void visitExpr(HIRExprPtr& exp) override;

        void visitFunction(HIRItemPath p, HIRFunction& item) override;
    };

    struct UfcsExprVisitorMutate: public HIRExprVisitorDef {
        const HIRCrate& crate;
        const HIRTraitImpl* currentTraitImpl;
        StaticTraitResolve resolve_;
        HIRExprNodeP replacement_;
        HIRSimplePath langBox_;

        UfcsExprVisitorMutate(const WireBoard& wb, const HIRTraitImpl* currentTraitImpl = nullptr);

        void visitNodePtr(HIRExprPtr& root);

        void visitNodePtr(HIRExprNodeP& node) override;

        void visit(HIRExprNodeUse& node) override;

        void visit(HIRExprNodeCallValue& node) override;

        void visit(HIRExprNodeCallMethod& node) override;

        bool isBuiltinOperator(const Span& sp, TypeckPrimitiveOperator op, const char* langitem, const HIRType* tyL, const HIRType* tyR) const;

        bool isBuiltinOperator(const Span& sp, TypeckPrimitiveOperator op, const char* langitem, const HIRType* ty) const;

        void visit(HIRExprNodeAssign& node) override;

        void visit(HIRExprNodeBinOp& node) override;

        void visit(HIRExprNodeUniOp& node) override;

        void visit(HIRExprNodeUnsize& node) override;
    };

    struct UfcsOuterVisitor: public HIRVisitor {
        const WireBoard& wb;
        const HIRCrate& crate;
        const HIRTraitImpl* currentTraitImpl = nullptr;

        UfcsOuterVisitor(const WireBoard& wb);

        void visitExpr(HIRExprPtr& exp) override;

        void visitConstgeneric(HIRConstGeneric& c) override;

        [[nodiscard]] const HIRType* visitType(const HIRType* ty) override;

        void visitTraitImpl(const HIRSimplePath& traitPath, HIRTraitImpl& impl) override;

        void visitFunction(HIRItemPath p, HIRFunction& item) override;

        void visitStatic(HIRItemPath p, HIRStatic& item) override;

        void visitConstant(HIRItemPath p, HIRConstant& item) override;

        void visitEnum(HIRItemPath p, HIREnum& item) override;
    };

    struct VisitorImplTrait: public HIRVisitor {
        HIRTrait* targetTrait = nullptr;
        HIRTraitImpl* targetImpl = nullptr;

        const HIRType* selfTy = nullptr;
        const HIRSimplePath* traitPath = nullptr;
        const HIRPathParams* traitArgs = nullptr;
        const char* methodName = nullptr;
        const HIRGenericParams* methodParams = nullptr;
        unsigned varIndex = 0;
        Vector<const HIRType*> tys;

        explicit VisitorImplTrait(HIRTypeInterner& types);

        [[nodiscard]] const HIRType* visitType(const HIRType* ty) override;

        void handleMethod(const HIRSimplePath& traitPath, const HIRPathParams& traitArgs, const HIRType* selfTy, const RcString& name, HIRFunction& fcn);

        void visitTrait(HIRItemPath p, HIRTrait& tr) override;

        void visitTraitImpl(const HIRSimplePath& traitPath, HIRTraitImpl& impl) override;
    };

    struct VtableOuterVisitor: public HIRVisitor {
        using NewTypes = std::vector<std::pair<RcString, HIRVisEnt<HIRTypeItem>*>>;

        const WireBoard& wb;
        const HIRCrate& crate;
        const HIRItemPath* currentModulePath = nullptr;
        NewTypes* currentNewTypes = nullptr;
        HIRSimplePath langSized_;

        HIRSimplePath createType(bool isPublic, RcString name, HIRStruct value);

        VtableOuterVisitor(const WireBoard& wb);

        void visitModule(HIRItemPath p, HIRModule& mod) override;

        void visitTrait(HIRItemPath p, HIRTrait& tr) override;

        void visitTraitImpl(const HIRSimplePath& traitPath, HIRTraitImpl& impl) override;
    };

    struct FixupVisitor: public HIRVisitor {
        const HIRCrate& crate;

        FixupVisitor(const HIRCrate& crate);

        void visitStruct(HIRItemPath ip, HIRStruct& str);
    };

    struct StaticBorrowExprVisitorMark: public HIRExprVisitorDef {
        const StaticTraitResolve& resolve_;
        const HIRType* selfType;
        const HIRExprPtr& exprPtr;

        HIRSimplePath langRangeFull_;

        bool isConstant;
        bool allConstant_;
        bool promoteAllConstFnCalls;

        StaticBorrowExprVisitorMark(const StaticTraitResolve& resolve, const HIRType* selfType, const HIRExprPtr& exprPtr, bool promoteAllConstFnCalls = false);

        bool allConstant() const;

        void visitNodePtr(HIRExprPtr& root);

        void visitNodePtr(HIRExprNodeP& node) override;

        void visit(HIRExprNodeBorrow& node) override;

        void visit(HIRExprNodeArraySized& node) override;

        void visit(HIRExprNodeArrayList& node) override;

        void visit(HIRExprNodeStructLiteral& node) override;

        void visit(HIRExprNodeTupleVariant& node) override;

        void visit(HIRExprNodeTuple& node) override;

        void visit(HIRExprNodeLet& node) override;

        void visit(HIRExprNodeCallMethod& node) override;

        void visit(HIRExprNodeCallPath& node) override;

        void visit(HIRExprNodeDeref& node) override;

        void visit(HIRExprNodeField& node) override;

        void visit(HIRExprNodeIndex& node) override;

        void visit(HIRExprNodeCast& node) override;

        void visit(HIRExprNodeUnsize& node) override;

        void visit(HIRExprNodeBinOp& node) override;

        void visit(HIRExprNodeUniOp& node) override;

        void visit(HIRExprNodeBlock& node) override;

        void visit(HIRExprNodeConstBlock& node) override;

        void visit(HIRExprNodeLiteral& node) override;

        void visit(HIRExprNodeConstParam& node) override;

        void visit(HIRExprNodeUnitVariant& node) override;

        void visit(HIRExprNodePathValue& node) override;

        void visit(HIRExprNodeClosure& node) override;

        bool nodeIsConstant(HIRExprNodeP& node);

        bool candidateNeedsDrop(HIRExprNodeP& root) const;

        bool isMaybeInteriorMut(const HIRExprNode& node) const;
    };

    struct StaticBorrowOuterVisitorMark: public HIRVisitor {
        const HIRCrate& crate;
        StaticTraitResolve resolve_;

        const HIRType* selfType = nullptr;
        const HIRItemPath* currentModulePath;
        const HIRModule* currentModule;

        StaticBorrowOuterVisitorMark(const WireBoard& wb);

        void visitModule(HIRItemPath p, HIRModule& mod) override;

        void visitTrait(HIRItemPath p, HIRTrait& item) override;

        void visitTypeImpl(HIRTypeImpl& impl) override;

        void visitTraitImpl(const HIRSimplePath& traitPath, HIRTraitImpl& impl) override;

        void visitExpr(HIRExprPtr& exp) override;

        void visitConstgeneric(HIRConstGeneric& c) override;

        [[nodiscard]] const HIRType* visitType(const HIRType* ty) override;

        void visitFunction(HIRItemPath p, HIRFunction& item) override;

        void visitStatic(HIRItemPath p, HIRStatic& item) override;

        void visitConstant(HIRItemPath p, HIRConstant& item) override;

        void visitEnum(HIRItemPath p, HIREnum& item) override;
    };

    struct NewStaticCallback {
        virtual HIRSimplePath create(Span sp, const HIRType* type, HIRExprPtr value, HIRGenericParams generics, bool isConst) = 0;
    };

    template <typename F>
    struct NewStaticCb final: NewStaticCallback {
        F f;

        explicit NewStaticCb(F f);

        HIRSimplePath create(Span sp, const HIRType* type, HIRExprPtr value, HIRGenericParams generics, bool isConst) override;
    };

    struct StaticBorrowExprVisitorMutate: public HIRExprVisitorDef {
        const StaticTraitResolve& resolve_;
        const HIRType* selfType;
        NewStaticCallback& newStaticCb;
        const HIRExprPtr& exprPtr;

        HIRSimplePath langRangeFull_;

        StaticBorrowExprVisitorMutate(const StaticTraitResolve& resolve, const HIRType* selfType, NewStaticCallback& newStaticCb, const HIRExprPtr& exprPtr);

        void visitNodePtr(HIRExprPtr& root);

        void visitNodePtr(HIRExprNodeP& root) override;

        struct Monomorph: public Monomorphiser {
            const HIRGenericParams& params;
            unsigned ofsImplT;
            unsigned ofsItemT;
            unsigned ofsImplV;
            unsigned ofsItemV;

            Monomorph(HIRTypeInterner& types, const HIRGenericParams& params, unsigned ofsImplT, unsigned ofsItemT, unsigned ofsImplV, unsigned ofsItemV);

            const HIRType* getType(const Span& sp, const HIRGenericRef& ge) const override;

            HIRConstGeneric getValue(const Span& sp, const HIRGenericRef& ge) const override;
        };

        Monomorph createParams(const Span& sp, HIRGenericParams& params, HIRPathParams& constructorPathParams) const;

        HIRExprPtr extractNode(HIRExprNodeP& node, StaticTraitResolve& resolve, HIRGenericParams& paramsDef, HIRPathParams& constrParams, bool preserveGenericContext = false);

        struct MonomorphLifetimesStatic: public Monomorphiser {
            explicit MonomorphLifetimesStatic(HIRTypeInterner& types);

            const HIRType* getType(const Span& sp, const HIRGenericRef& g) const override;

            HIRConstGeneric getValue(const Span& sp, const HIRGenericRef& g) const override;
        };

        void visit(HIRExprNodeBorrow& node) override;

        void visit(HIRExprNodeConstBlock& node) override;
    };

    struct StaticBorrowOuterVisitor: public HIRVisitor, public NewStaticCallback {
        HIRCrate& crate;
        StaticTraitResolve resolve_;

        const HIRType* selfType = nullptr;
        const HIRItemPath* currentModulePath;
        HIRModule* currentModule;
        bool isConst;

        struct NewStatic {
            HIRSimplePath path;
            HIRStatic data;
            bool isConst;
        };

        std::map<HIRModule*, std::vector<NewStatic>> newStatics;

        StaticBorrowOuterVisitor(const WireBoard& wb);

        HIRSimplePath create(Span sp, const HIRType* ty, HIRExprPtr valExpr, HIRGenericParams generics, bool isConst) override;

        void visitCrate(HIRCrate& crate) override;

        void visitModule(HIRItemPath p, HIRModule& mod) override;

        void visitTrait(HIRItemPath p, HIRTrait& item) override;

        void visitTypeImpl(HIRTypeImpl& impl) override;

        void visitTraitImpl(const HIRSimplePath& traitPath, HIRTraitImpl& impl) override;

        void visitExpr(HIRExprPtr& exp) override;

        void visitConstgeneric(HIRConstGeneric& c) override;

        [[nodiscard]] const HIRType* visitType(const HIRType* ty) override;

        void visitFunction(HIRItemPath p, HIRFunction& item) override;

        void visitStatic(HIRItemPath p, HIRStatic& item) override;

        void visitConstant(HIRItemPath p, HIRConstant& item) override;

        void visitEnum(HIRItemPath p, HIREnum& item) override;
    };

    bool typeIsUseCloned(const StaticTraitResolve& resolve, const Span& sp, const HIRType* type) {
        const auto& trait = resolve.hirCrate().getLangItemPathOpt("use_cloned");
        return !trait.components().empty() && resolve.findImpl(sp, trait, HIRPathParams{}, type, [](SolverSelection) {
            return true;
        });
    }

    inline HIRExprNodeP closureMkExprnodep(HIRExprNode* en, const HIRType* ty) {
        en->resType = mv$(ty);
        return HIRExprNodeP(en);
    }

    template <typename K, typename V>
    std::map<K, V> makeMap1(K k1, V v1) {
        std::map<K, V> rv;
        rv.insert(std::make_pair(mv$(k1), mv$(v1)));
        return rv;
    }

    void OutState::pushNewImpls(const Span& sp, HIRCrate& crate) {
        auto checkState = [&crate](HIRTraitImpl& ti) {
            for (auto& m : ti.methods) {
                ASSERT_BUG(Span(), m.second.data.code.state, StringView("Missing expression state on ") << ti.type << StringView(" :: ") << m.first);
            }
        };
        auto pushTraitImpl = [&](const HIRSimplePath& p, std::unique_ptr<HIRTraitImpl> ptr) {
            checkState(*ptr);
            auto& traitImplListR = crate.allTraitImpls[p].getListForTypeMut(ptr->type);
            traitImplListR.push_back(ptr.get());
            auto& traitImplList = crate.traitImpls[p].getListForTypeMut(ptr->type);
            traitImplList.push_back(mv$(ptr));
        };
        for (auto& impl : this->implsClosure) {
            switch (impl.first) {
                case HIRExprNodeClosure::Class::Once:
                    DEBUG(StringView("impl") << impl.second.params.fmtArgs() << StringView(" FnOnce") << impl.second.traitArgs << StringView(" for ") << impl.second.type);
                    pushTraitImpl(crate.getLangItemPath(sp, "fn_once"), box$(impl.second));
                    break;
                case HIRExprNodeClosure::Class::Mut:
                    DEBUG(StringView("impl") << impl.second.params.fmtArgs() << StringView(" FnMut") << impl.second.traitArgs << StringView(" for ") << impl.second.type);
                    pushTraitImpl(crate.getLangItemPath(sp, "fn_mut"), box$(impl.second));
                    break;
                case HIRExprNodeClosure::Class::Shared:
                    DEBUG(StringView("impl") << impl.second.params.fmtArgs() << StringView(" Fn") << impl.second.traitArgs << StringView(" for ") << impl.second.type);
                    pushTraitImpl(crate.getLangItemPath(sp, "fn"), box$(impl.second));
                    break;
                case HIRExprNodeClosure::Class::NoCapture:
                    BUG(sp, StringView(""));
                    break;
                case HIRExprNodeClosure::Class::Unknown:
                    BUG(Span(), StringView("Encountered Unkown closure type in new impls"));
                    break;
            }
        }
        for (auto& ptr : this->implsType) {
            const auto& path = ptr->type->as_Path().path.data.as_Generic().path;
            DEBUG(StringView("Adding type impl") << ptr->params.fmtArgs() << StringView(" ") << ptr->type);
            crate.allTypeImpls.named[path].push_back(ptr.get());
            crate.typeImpls.named[path].push_back(mv$(ptr));
        }
        for (auto& impl : this->traitImpls) {
            checkState(impl.second);
            pushTraitImpl(crate.getLangItemPath(sp, impl.first), box$(impl.second));
        }
        this->implsClosure.resize(0);
        this->traitImpls.resize(0);
    }

    void fixDefiningOpaqueAliasNodeTypes(const StaticTraitResolve& resolve, const HIRSimplePath* definingAliases, size_t definingAliasCount, const HIRType* type) {
        visitTyWith(type, [&](const HIRType* candidate) {
            const auto* erased = candidate->opt_ErasedType();
            const auto* alias = erased ? erased->inner.opt_Alias() : nullptr;
            if (!alias || !alias->inner->type || !resolve.hirCrate().isOpaqueAliasNamedBy(*alias->inner, definingAliases, definingAliasCount)) {
                return false;
            }

            OpaqueAliasParamMonomorph monomorph{resolve.hirCrate().types, *alias->inner, alias->params};
            ClosureExprVisitorFixup fixup{resolve.board(), nullptr, monomorph, nullptr};
            alias->inner->type = fixup.visitType(alias->inner->type);
            return false;
        });
    }

    const HIRType* visitType(const Span& sp, const StaticTraitResolve& resolve, const HIRType* ty) {
        return resolve.revealOpaqueTypes(sp, ty);
    }

    inline HIRExprNodeP reborrowMkExprnodep(HIRExprNode* en, const HIRType* ty) {
        en->resType = mv$(ty);
        return HIRExprNodeP(en);
    }

    inline HIRExprNodeP mkExprnodep(HIRExprNode* en, const HIRType* ty) {
        en->resType = mv$(ty);
        return HIRExprNodeP(en);
    }

    static void checkConstFinalBorrow(const StaticTraitResolve& resolve, HIRExprNode& root) {
        HIRExprNode* node = &root;
        while (auto* block = cast<HIRExprNodeBlock>(node)) {
            if (!block->valueNode) {
                return;
            }
            node = &*block->valueNode;
        }
        auto* borrow = cast<HIRExprNodeBorrow>(node);
        if (!borrow) {
            return;
        }
        bool isMutable = borrow->type != HIRBorrowType::Shared;
        HIRExprNode* held = &*borrow->value;
        while (auto* deref = cast<HIRExprNodeDeref>(held)) {
            auto* inner = cast<HIRExprNodeBorrow>(&*deref->value);
            if (!inner) {
                break;
            }
            isMutable |= inner->type != HIRBorrowType::Shared;
            held = &*inner->value;
        }
        if (cast<HIRExprNodePathValue>(held)) {
            return;
        }
        if (isMutable) {
            ERROR(borrow->span(), E0000, StringView("A mutable reference is not allowed in the final value of a constant"));
        }
        if (resolve.typeIsInteriorMutable(borrow->span(), held->resType) == InteriorMutability::Yes) {
            ERROR(borrow->span(), E0000, StringView("A constant may not refer to interior mutable data - ") << held->resType);
        }
    }

    static HIRExprNodeP* staticBorrowPromotionRoot(HIRExprNodeP& value) {
        auto* root = &value;
        for (;;) {
            if (auto* index = cast<HIRExprNodeIndex>(root->get())) {
                root = &index->value;
                continue;
            }
            if (auto* unsize = cast<HIRExprNodeUnsize>(root->get())) {
                root = &unsize->value;
                continue;
            }
            return root;
        }
    }

    inline HIRExprNodeP ufcsMkExprnodep(HIRExprNode* en, const HIRType* ty) {
        en->resType = mv$(ty);
        return HIRExprNodeP(en);
    }
}

void HIRExpandAnnotateUsageExpr(const WireBoard& wb, const HIRCrate& crate, const HIRItemPath& ip, HIRExprPtr& exp) {
    TRACE_FUNCTION_F(ip);
    BUG_ASSERT(exp);
    StaticTraitResolve resolve{wb};
    resolve.setBothGenericsRaw(exp.state->implGenerics, exp.state->itemGenerics);
    AnnotateExprVisitorMark ev{resolve, exp.bindings};
    ev.visitRoot(exp);
}

void HIRExpandAnnotateUsage(const WireBoard& wb, HIRCrate& crate) {
    AnnotateOuterVisitor ov(wb);
    ov.visitCrate(crate);
}

#define NEWNODE(TY, CLASS, ...) closureMkExprnodep(pool->make<HIRExprNode##CLASS>(__VA_ARGS__), TY)

static auto indexMutableAnonymousNodes(HIRExprPtr& exp, OutState& out, HIRTypeInterner& types) -> void {
    struct NodeMutIndex final: HIRExprVisitorDef {
        OutState& out;

        NodeMutIndex(HIRTypeInterner& types, OutState& out)
            : HIRExprVisitorDef(types)
            , out(out)
        {
        }

        void visit(HIRExprNodeClosure& node) override {
            out.mutableNodes.pushBack(std::make_pair(&node, &node));
            HIRExprVisitorDef::visit(node);
        }

        void visit(HIRExprNodeGenerator& node) override {
            out.mutableNodes.pushBack(std::make_pair(&node, &node));
            HIRExprVisitorDef::visit(node);
        }

        void visit(HIRExprNodeAsyncBlock& node) override {
            out.mutableNodes.pushBack(std::make_pair(&node, &node));
            HIRExprVisitorDef::visit(node);
        }
    } nodeIndex{types, out};
    exp->visit(nodeIndex);
}

const HIRType* HIRExpandClosuresExpr(const WireBoard& wb, HIRCrate& crate, const HIRType* expTy, HIRExprPtr& exp) {
    Span sp;

    TRACE_FUNCTION;
    StaticTraitResolve resolve{wb};
    BUG_ASSERT(exp);
    resolve.setBothGenericsRaw(exp.state->implGenerics, exp.state->itemGenerics);

    const HIRType* selfType = nullptr; // TODO: Need to be able to get this?

    OutState out;
    indexMutableAnonymousNodes(exp, out, crate.types);
    auto newType = makeCallable<ClosureTypeCb>([&](const char* prefix, const char* suffix, auto s) -> auto {
        auto name = RcString::newInterned(FMT(prefix << StringView("C_") << ++wb.id));
        auto boxed = crate.pool->make<HIRVisEnt<HIRTypeItem>>(HIRVisEnt<HIRTypeItem>{HIRPublicity::newNone(), HIRTypeItem(mv$(s))});
        auto* retPtr = &boxed->ent;
        crate.newTypes.push_back(std::make_pair(name, boxed));
        return std::make_pair(HIRSimplePath(crate.crateName, {}) + name, retPtr);
    });
    out.newType = &newType;

    {
        ClosureExprVisitorExtract ev(resolve, selfType, exp.bindings, exp, out, "");
        ev.visitRoot(*exp);
    }

    {
        MonomorphiserNop mm(crate.types);
        ClosureExprVisitorFixup fixup{wb, nullptr, mm, &out};
        fixup.visitRoot(exp);
        expTy = fixup.visitType(expTy);
    }
    fixDefiningOpaqueAliasNodeTypes(resolve, exp.state->defineOpaque.data(), exp.state->defineOpaque.size(), expTy);

    for (auto& impl : out.implsType) {
        for (auto& m : impl->methods) {
            m.second.data.code.state = HIRExprStatePtr(crate.pool, *exp.state);
            m.second.data.code.state->stage = HIRExprState::Stage::Typecheck;
        }
        impl->srcModule = exp.state->modPath;
    }
    for (auto& impl : out.implsClosure) {
        for (auto& m : impl.second.methods) {
            m.second.data.code.state = HIRExprStatePtr(crate.pool, *exp.state);
            m.second.data.code.state->stage = HIRExprState::Stage::Typecheck;
        }
        impl.second.srcModule = exp.state->modPath;
    }
    for (auto& impl : out.traitImpls) {
        for (auto& m : impl.second.methods) {
            m.second.data.code.state = HIRExprStatePtr(crate.pool, *exp.state);
            m.second.data.code.state->stage = HIRExprState::Stage::Typecheck;
        }
        impl.second.srcModule = exp.state->modPath;
    }
    out.pushNewImpls(sp, crate);
    return expTy;
}

void HIRExpandClosures(const WireBoard& wb, HIRCrate& crate) {
    ClosureOuterVisitor ov(wb);
    ov.visitCrate(crate);

    struct ClosureOuterVisitorPass2: public HIRVisitor {
        StaticTraitResolve resolve_;

        ClosureOuterVisitorPass2(const WireBoard& wb)
            : HIRVisitor(nullptr, wb.crate->types)
            , resolve_(wb)
        {
        }

        void fixType(const HIRType* ty) const {
            visitTyWith(ty, [&](const HIRType* ty) -> bool {
                if (const auto* e = ty->opt_ErasedType()) {
                    if (const auto* ee = e->inner.opt_Alias()) {
                        if (ee->inner->type) {
                            OpaqueAliasParamMonomorph monomorph{resolve_.hirCrate().types, *ee->inner, ee->params};
                            ClosureExprVisitorFixup fixup{resolve_.board(), nullptr, monomorph, nullptr};
                            ee->inner->type = fixup.visitType(ee->inner->type);
                        }
                    }
                }
                return false;
            });
        }

        void visitTypeAlias(HIRItemPath p, HIRTypeAlias& item) override {
            HIRVisitor::visitTypeAlias(p, item);
            fixType(item.type);
        }

        void visitInherentType(HIRItemPath p, HIRTypeAlias& item) override {
            HIRVisitor::visitInherentType(p, item);
            fixType(item.type);
        }

        void visitTraitImpl(const HIRSimplePath& traitPath, HIRTraitImpl& impl) override {
            TRACE_FUNCTION_F(StringView("impl ") << traitPath << StringView(" for ") << impl.type);
            DEBUG(StringView("src module ") << impl.srcModule);
            DEBUG(StringView("src module ") << impl.srcModule);
            HIRVisitor::visitTraitImpl(traitPath, impl);
            for (auto& t : impl.types) {
                fixType(t.second.data);
            }
        }
    };

    ClosureOuterVisitorPass2(wb).visitCrate(crate);
}

#undef NEWNODE

void HIRExpandErasedType(const WireBoard& wb, HIRCrate& crate) {
    ErasedOuterVisitor ov(wb);
    ov.visitCrate(crate);

    ErasedOuterVisitorFixup ovFix(wb);
    ovFix.visitCrate(crate);
}

#define NEWNODE(TY, CLASS, ...) reborrowMkExprnodep(crate.pool->make<HIRExprNode##CLASS>(__VA_ARGS__), TY)

void HIRExpandReborrowsExpr(const WireBoard& wb, const HIRCrate& crate, HIRExprPtr& exp) {
    TRACE_FUNCTION;
    ReborrowExprVisitorMutate ev(crate);
    ev.visitNodePtr(exp);
}

void HIRExpandReborrows(const WireBoard& wb, HIRCrate& crate) {
    ReborrowOuterVisitor ov(crate);
    ov.visitCrate(crate);
}

#undef NEWNODE

#define NEWNODE(TY, CLASS, ...) mkExprnodep(resolve_.hirCrate().pool->make<HIRExprNode##CLASS>(__VA_ARGS__), TY)

void HIRExpandStaticBorrowConstantsMarkExpr(const WireBoard& wb, const HIRCrate& crate, const HIRItemPath& ip, HIRExprPtr& exp) {
    TRACE_FUNCTION_F(ip);
    StaticTraitResolve resolve(wb);

    // TODO: Get `Self` type
    StaticBorrowExprVisitorMark evm(resolve, nullptr, exp);
    evm.visitNodePtr(exp);
    if (!evm.allConstant()) {
        // TODO: How to determine if this is a static/const instead of a function?
    }
}

void HIRExpandStaticBorrowConstantsExpr(const WireBoard& wb, HIRCrate& crate, const HIRItemPath& ip, HIRExprPtr& exp) {
    TRACE_FUNCTION_F(ip);
    StaticTraitResolve resolve(wb);
    resolve.setBothGenericsRaw(exp.state->implGenerics, exp.state->itemGenerics);

    const HIRType* selfType = ip.getTopIp().ty;
    if (ip.getTopIp().wrapped) {
        const HIRPath& p = *ip.getTopIp().wrapped;
        if (const auto* e = p.data.opt_UfcsInherent()) {
            selfType = e->type;
        }
        if (const auto* e = p.data.opt_UfcsKnown()) {
            selfType = e->type;
        }
        DEBUG(StringView("self_type = NONE"));
    }
    auto callback = makeCallable<NewStaticCb>([&](Span sp, const HIRType* ty, HIRExprPtr valExpr, HIRGenericParams generics, bool isConst) -> HIRSimplePath {
        auto name = RcString::newInterned(FMT(StringView("lifted#C_") << ++wb.id));

        auto path = HIRSimplePath(crate.crateName, {name});
        auto newStatic = HIRStatic(
            HIRLinkage(),
            /*is_mut=*/false,
            mv$(ty),
            /*m_value=*/mv$(valExpr)
        );
        newStatic.params = mv$(generics);
        newStatic.isPromoted = true;
        newStatic.saveLiteral = true;

        struct Nvs: HIREvaluator::Newval {
            HIRCrate& crate;
            u32& id;

            Nvs(HIRCrate& crate, u32& id)
                : crate(crate)
                , id(id)
            {
            }

            HIRPath newStatic(const HIRType* type, EncodedLiteral value, size_t alignment) override {
                auto name = RcString::newInterned(FMT(StringView("lifted#C_") << ++id));
                auto path = HIRSimplePath() + name;
                auto newStatic = HIRStatic(
                    HIRLinkage(),
                    /*is_mut=*/false,
                    std::move(type),
                    /*m_value=*/HIRExprPtr()
                );
                newStatic.explicitAlignment = alignment;
                newStatic.valueGenerated = true;
                newStatic.isPromoted = true;
                newStatic.valueRes = std::move(value);
                crate.newValues.push_back(std::make_pair(name, crate.pool->make<HIRVisEnt<HIRValueItem>>(HIRVisEnt<HIRValueItem>{HIRPublicity::newNone(), HIRValueItem(crate.pool->make<HIRStatic>(std::move(newStatic)))})));

                auto& s = *crate.newValues.back().second->ent.as_Static();
                ASSERT_BUG(Span(), !s.value.state, StringView("ExprState set already"));
                s.value.state = HIRExprStatePtr(crate.pool, HIRExprState(crate.types, crate.rootModule, HIRSimplePath(crate.crateName)));
                s.value.state->stage = HIRExprState::Stage::Sbc;
                s.value.state->implGenerics = nullptr;
                s.value.state->itemGenerics = &s.params;
                return path;
            }
        } nvs{crate, wb.id};

        if (!newStatic.params.isGeneric()) {
            newStatic.value.state->stage = HIRExprState::Stage::Sbc;
            newStatic.valueRes = HIREvaluator(sp, wb, nvs).evaluateConstant(path, newStatic.value, newStatic.type);
            newStatic.valueGenerated = true;
        }

        DEBUG(path << StringView(" = ?"));
        auto vi = isConst ? HIRValueItem(crate.pool->make<HIRConstant>(HIRConstant{std::move(newStatic.params), std::move(newStatic.type), std::move(newStatic.value)})) : HIRValueItem(crate.pool->make<HIRStatic>(std::move(newStatic)));
        auto boxed = crate.pool->make<HIRVisEnt<HIRValueItem>>(HIRVisEnt<HIRValueItem>{HIRPublicity::newNone(), std::move(vi)});
        crate.newValues.push_back(std::make_pair(name, boxed));
        {
            auto& e = crate.newValues.back().second->ent;
            ASSERT_BUG(sp, e.is_Static() || e.is_Constant(), StringView(""));
            auto& p = e.is_Static() ? e.as_Static()->params : e.as_Constant()->params;
            auto& v = e.is_Static() ? e.as_Static()->value : e.as_Constant()->value;
            ASSERT_BUG(Span(), v.state, StringView(""));
            v.state->implGenerics = nullptr;
            v.state->itemGenerics = &p;
        }
        return path;
    });
    StaticBorrowExprVisitorMutate ev(resolve, selfType, callback, exp);
    ev.visitNodePtr(exp);
}

void HIRExpandStaticBorrowConstantsMark(const WireBoard& wb, HIRCrate& crate) {
    StaticBorrowOuterVisitorMark ov(wb);
    ov.visitCrate(crate);
}

void HIRExpandStaticBorrowConstants(const WireBoard& wb, HIRCrate& crate) {
    StaticBorrowOuterVisitor ov(wb);
    ov.visitCrate(crate);

    for (auto& newTyPair : crate.newTypes) {
        crate.rootModule.modItems.insert(mv$(newTyPair));
    }
    crate.newTypes.clear();
    for (auto& newValPair : crate.newValues) {
        crate.rootModule.valueItems.insert(mv$(newValPair));
    }
    crate.newValues.clear();
}

#undef NEWNODE

#define NEWNODE(TY, CLASS, ...) ufcsMkExprnodep(crate.pool->make<HIRExprNode##CLASS>(__VA_ARGS__), TY)

void HIRExpandUfcsEverythingExpr(const WireBoard& wb, const HIRCrate& crate, HIRExprPtr& exp, const HIRTraitImpl* currentTraitImpl) {
    TRACE_FUNCTION;
    UfcsExprVisitorMutate ev{wb, currentTraitImpl};
    ev.visitNodePtr(exp);
}

void HIRExpandUfcsEverything(const WireBoard& wb, HIRCrate& crate) {
    UfcsOuterVisitor ov(wb);
    ov.visitCrate(crate);
}

#undef NEWNODE

void HIRExpandVTables(const WireBoard& wb, HIRCrate& crate) {
    {
        VisitorImplTrait v(crate.types);
        v.visitCrate(crate);
    }

    VtableOuterVisitor ov(wb);
    ov.visitCrate(crate);

    FixupVisitor fv(crate);
    fv.visitCrate(crate);
}

#include "hir_expand_crnode_tu.cpp"
#include "hir_expand_scope_tu.cpp"

ClosureScope::ClosureScope(HIRExprNodeClosure& node)
    : node(node)
{
}

CoroutineScope::CoroutineScope(HIRExprNodeGenerator& node)
    : node(&node)
{
    yieldStack.pushBack(0);
}

CoroutineScope::CoroutineScope(HIRExprNodeAsyncBlock& node)
    : node(&node)
{
    yieldStack.pushBack(0);
}

auto AnnotateExprVisitorMark::getUsage() const -> HIRValueUsage {
    return (usage.empty() ? HIRValueUsage::Move : usage.back());
}

auto AnnotateExprVisitorMark::pushUsage(HIRValueUsage u) -> UsageGuard {
    if (getUsage() == u) {
        return UsageGuard(*this, false);
    } else {
        usage.pushBack(u);
        return UsageGuard(*this, true);
    }
}

AnnotateExprVisitorMark::AnnotateExprVisitorMark(const StaticTraitResolve& resolve, const Vector<const HIRType*>& variableTypes)
    : resolve_(resolve)
    , variableTypes(variableTypes)
    , ignoreVariableCapture(false)
{
}

auto AnnotateExprVisitorMark::visitRoot(HIRExprPtr& rootPtr) -> void {
    // HACK: Pre-visit all nodes to find closures, and mark those as !Copy

    {
        struct IV: public HIRExprVisitorDef {
            explicit IV(HIRTypeInterner& types)
                : HIRExprVisitorDef(types)
            {
            }

            void visit(HIRExprNodeClosure& node) override {
                node.isCopy = false;
                HIRExprVisitorDef::visit(node);
            }
        } iv(resolve_.hirCrate().types);

        rootPtr->visit(iv);
    }

    BUG_ASSERT(rootPtr);
    rootPtr->usage = this->getUsage();
    auto expectedSize = usage.length();
    rootPtr->visit(*this);
    BUG_ASSERT(usage.length() == expectedSize);

    for (auto* call : pendingCalls) {
        const auto* nodePp = (*call->value->resType).is_NodeType() ? ((*call->value->resType).as_NodeType().opt_Closure()) : nullptr;
        if (!nodePp || !*nodePp) {
            continue;
        }
        switch ((*nodePp)->cls) {
            case HIRExprNodeClosure::Class::Unknown:
                break;
            case HIRExprNodeClosure::Class::NoCapture:
            case HIRExprNodeClosure::Class::Shared:
                if (!resolve_.hirCrate().getLangItemPathOpt("fn").components().empty()) {
                    call->traitUsed = HIRExprNodeCallValue::TraitUsed::Fn;
                } else if (!resolve_.hirCrate().getLangItemPathOpt("fn_mut").components().empty()) {
                    call->traitUsed = HIRExprNodeCallValue::TraitUsed::FnMut;
                }
                break;
            case HIRExprNodeClosure::Class::Mut:
                call->traitUsed = !resolve_.hirCrate().getLangItemPathOpt("fn_mut").components().empty() ? HIRExprNodeCallValue::TraitUsed::FnMut : HIRExprNodeCallValue::TraitUsed::FnOnce;
                break;
            case HIRExprNodeClosure::Class::Once:
                call->traitUsed = HIRExprNodeCallValue::TraitUsed::FnOnce;
                break;
        }
    }
    pendingCalls.clear();
}

auto AnnotateExprVisitorMark::visitNodePtr(HIRExprNodeP& nodePtr) -> void {
    BUG_ASSERT(nodePtr);

    const auto& nodeRef = *nodePtr;
    const char* nodeTyname = typeid(nodeRef).name();

    TRACE_FUNCTION_FR(static_cast<const void*>(&*nodePtr) << StringView(" ") << nodeTyname << StringView(": ") << this->getUsage(), nodePtr->usage);
    nodePtr->usage = this->getUsage();

    auto expectedSize = usage.length();
    nodePtr->visit(*this);
    BUG_ASSERT(usage.length() == expectedSize);
}

auto AnnotateExprVisitorMark::visit(HIRExprNodeBlock& node) -> void {
    CoroutineScope* scope = closureStack.size() > 0 ? closureStack.back().opt_Coroutine() : nullptr;
    if (scope) {
        scope->yieldStack.pushBack(0);
    }

    auto _ = this->pushUsage(HIRValueUsage::Move);

    for (auto& subnode : node.nodes) {
        this->visitNodePtr(subnode);
    }
    if (node.valueNode) {
        this->visitNodePtr(node.valueNode);
    }

    if (scope) {
        scope = &closureStack.back().as_Coroutine();
        scope->yieldStack.popBack();
    }
}

auto AnnotateExprVisitorMark::visit(HIRExprNodeConstBlock& node) -> void {
    auto _ = this->pushUsage(HIRValueUsage::Move);
    this->visitNodePtr(node.inner);
}

auto AnnotateExprVisitorMark::visit(HIRExprNodeAsm& node) -> void {
    auto _ = this->pushUsage(HIRValueUsage::Move);
    for (auto& v : node.outputs) {
        this->visitNodePtr(v.value);
    }
    for (auto& v : node.inputs) {
        this->visitNodePtr(v.value);
    }
}

auto AnnotateExprVisitorMark::visit(HIRExprNodeAsm2& node) -> void {
    auto _ = this->pushUsage(HIRValueUsage::Move);
    for (auto& v : node.params) {
        switch (v.tag()) {
            case HIRAsmParam::TAG_Const: {
                auto& e = v.as_Const();
                visitNodePtr(e);
                break;
            }
            case HIRAsmParam::TAG_Sym: {
                break;
            }
            case HIRAsmParam::TAG_Label: {
                auto& e = v.as_Label();
                visitNodePtr(e.code);
                break;
            }
            case HIRAsmParam::TAG_RegSingle: {
                auto& e = v.as_RegSingle();
                visitNodePtr(e.val);
                break;
            }
            case HIRAsmParam::TAG_Reg: {
                auto& e = v.as_Reg();
                if (e.valIn) {
                    visitNodePtr(e.valIn);
                }
                if (e.valOut) {
                    visitNodePtr(e.valOut);
                }
                break;
            }
        }
    }
}

auto AnnotateExprVisitorMark::visit(HIRExprNodeReturn& node) -> void {
    auto _ = this->pushUsage(HIRValueUsage::Move);
    this->visitNodePtr(node.value);
}

auto AnnotateExprVisitorMark::visit(HIRExprNodeYield& node) -> void {
    auto _ = this->pushUsage(HIRValueUsage::Move);
    this->visitNodePtr(node.value);

    CoroutineScope* scope = closureStack.size() > 0 ? closureStack.back().opt_Coroutine() : nullptr;
    if (scope && scope->node.is_Generator()) {
        for (auto& value : mutRange(scope->yieldStack)) {
            if (value != CoroutineScope::STACK_MARKER_LOOP) {
                BUG_ASSERT(value < CoroutineScope::STACK_MARKER_LOOP - 1);
                value += 1;
            }
        }
    }
}

auto AnnotateExprVisitorMark::visit(HIRExprNodeAWait& node) -> void {
    auto _ = this->pushUsage(HIRValueUsage::Move);
    this->visitNodePtr(node.value);

    CoroutineScope* scope = closureStack.size() > 0 ? closureStack.back().opt_Coroutine() : nullptr;
    if (scope && scope->node.is_Async()) {
        for (auto& value : mutRange(scope->yieldStack)) {
            if (value != CoroutineScope::STACK_MARKER_LOOP) {
                BUG_ASSERT(value < CoroutineScope::STACK_MARKER_LOOP - 1);
                value += 1;
            }
        }
    }
}

auto AnnotateExprVisitorMark::visit(HIRExprNodeUse& node) -> void {
    const auto* type = node.value->resType;
    const auto innerUsage = resolve_.typeIsCopy(node.span(), type) || typeIsUseCloned(resolve_, node.span(), type) ? HIRValueUsage::Borrow : HIRValueUsage::Move;
    auto _ = pushUsage(innerUsage);
    this->visitNodePtr(node.value);
}

auto AnnotateExprVisitorMark::visit(HIRExprNodeLet& node) -> void {
    addDefsFromPattern(node.span(), node.pattern);
    if (node.value) {
        auto _ = this->pushUsage(this->getUsageForPattern(node.span(), node.pattern, node.type));
        this->visitNodePtr(node.value);
    }
}

auto AnnotateExprVisitorMark::visit(HIRExprNodeLoop& node) -> void {
    CoroutineScope* scope = closureStack.size() > 0 ? closureStack.back().opt_Coroutine() : nullptr;
    if (scope) {
        struct InnerVisitor: public HIRExprVisitorDef {
            bool hasYield = false;

            explicit InnerVisitor(HIRTypeInterner& types)
                : HIRExprVisitorDef(types)
            {
            }

            void visit(HIRExprNodeClosure&) override {
            }

            void visit(HIRExprNodeGenerator&) override {
            }

            void visit(HIRExprNodeAsyncBlock&) override {
            }

            void visit(HIRExprNodeYield& node) override {
                hasYield = true;
            }
        } v(resolve_.hirCrate().types);

        v.visitNodePtr(node.code);

        if (v.hasYield) {
            DEBUG(StringView("Loop with inner yield"));
            scope->yieldStack.pushBack(CoroutineScope::STACK_MARKER_LOOP);
        } else {
            DEBUG(StringView("Loop without inner yield"));
            scope = nullptr;
        }
    }

    auto _ = this->pushUsage(HIRValueUsage::Move);
    this->visitNodePtr(node.code);

    if (scope) {
        scope = &closureStack.back().as_Coroutine();
        scope->yieldStack.popBack();
    }
}

auto AnnotateExprVisitorMark::visit(HIRExprNodeLoopControl& node) -> void {
    if (node.value) {
        this->visitNodePtr(node.value);
    }
}

auto AnnotateExprVisitorMark::visit(HIRExprNodeMatch& node) -> void {
    {
        const auto& valTy = node.value->resType;
        HIRValueUsage vu = HIRValueUsage::Unknown;
        for (const auto& arm : node.arms) {
            for (const auto& pat : arm.patterns) {
                DEBUG(StringView("_Match: ") << pat);
                vu = std::max(vu, this->getUsageForPattern(node.span(), pat, valTy));
            }
        }
        if (vu == HIRValueUsage::Unknown) {
            DEBUG(StringView("No value usage for pattern arms (no arms?), default to borrow"));
            vu = HIRValueUsage::Borrow;
        }
        auto _ = this->pushUsage(vu);
        this->visitNodePtr(node.value);
    }

    auto _ = this->pushUsage(HIRValueUsage::Move);
    for (auto& arm : node.arms) {
        for (const auto& pat : arm.patterns) {
            addDefsFromPattern(node.span(), pat);
        }
        for (auto& c : arm.guards) {
            auto _ = this->pushUsage(this->getUsageForPattern(c.val->span(), c.pat, c.val->resType));
            this->visitNodePtr(c.val);
            addDefsFromPattern(node.span(), c.pat);
        }
        this->visitNodePtr(arm.code);
    }
}

auto AnnotateExprVisitorMark::visit(HIRExprNodeAssign& node) -> void {
    {
        auto _ = this->pushUsage(HIRValueUsage::Mutate);
        this->visitNodePtr(node.slot);
    }
    {
        auto _ = this->pushUsage(HIRValueUsage::Move);
        this->visitNodePtr(node.value);
    }
}

auto AnnotateExprVisitorMark::visit(HIRExprNodeUniOp& node) -> void {
    usage.pushBack(HIRValueUsage::Move);

    this->visitNodePtr(node.value);

    usage.popBack();
}

auto AnnotateExprVisitorMark::visit(HIRExprNodeBorrow& node) -> void {
    switch (node.type) {
        case HIRBorrowType::Shared:
            usage.pushBack(HIRValueUsage::Borrow);
            break;
        case HIRBorrowType::Unique:
            usage.pushBack(HIRValueUsage::Mutate);
            break;
        case HIRBorrowType::Owned:
            usage.pushBack(HIRValueUsage::Move);
            break;
    }

    this->visitNodePtr(node.value);

    usage.popBack();
}

auto AnnotateExprVisitorMark::visit(HIRExprNodeRawBorrow& node) -> void {
    switch (node.type) {
        case HIRBorrowType::Shared:
            usage.pushBack(HIRValueUsage::Borrow);
            break;
        case HIRBorrowType::Unique:
            usage.pushBack(HIRValueUsage::Mutate);
            break;
        case HIRBorrowType::Owned:
            usage.pushBack(HIRValueUsage::Move);
            break;
    }

    this->visitNodePtr(node.value);

    usage.popBack();
}

auto AnnotateExprVisitorMark::visit(HIRExprNodeBinOp& node) -> void {
    switch (node.op) {
        case HIRExprNodeBinOp::Op::CmpEqu:
        case HIRExprNodeBinOp::Op::CmpNEqu:
        case HIRExprNodeBinOp::Op::CmpLt:
        case HIRExprNodeBinOp::Op::CmpLtE:
        case HIRExprNodeBinOp::Op::CmpGt:
        case HIRExprNodeBinOp::Op::CmpGtE:
            usage.pushBack(HIRValueUsage::Borrow);
            break;
        default:
            usage.pushBack(HIRValueUsage::Move);
            break;
    }

    this->visitNodePtr(node.left);
    this->visitNodePtr(node.right);

    usage.popBack();
}

auto AnnotateExprVisitorMark::visit(HIRExprNodeCast& node) -> void {
    auto _ = pushUsage(HIRValueUsage::Move);
    this->visitNodePtr(node.value);
}

auto AnnotateExprVisitorMark::visit(HIRExprNodeUnsize& node) -> void {
    // TODO: Why does Unsize have a usage of Move?
    this->visitNodePtr(node.value);
}

auto AnnotateExprVisitorMark::visit(HIRExprNodeIndex& node) -> void {
    // TODO: Override to ::Borrow if Res: Copy and moving
    if (this->getUsage() == HIRValueUsage::Move && resolve_.typeIsCopy(node.span(), node.resType)) {
        auto _ = pushUsage(HIRValueUsage::Borrow);
        this->visitNodePtr(node.value);
    } else {
        this->visitNodePtr(node.value);
    }

    auto _ = pushUsage(HIRValueUsage::Move);
    this->visitNodePtr(node.index);
}

auto AnnotateExprVisitorMark::visit(HIRExprNodeDeref& node) -> void {
    if (this->getUsage() == HIRValueUsage::Move && resolve_.typeIsCopy(node.span(), node.resType)) {
        auto _ = pushUsage(HIRValueUsage::Borrow);
        this->visitNodePtr(node.value);
    } else if (node.value->resType->is_Pointer()) {
        auto _ = pushUsage(HIRValueUsage::Borrow);
        this->visitNodePtr(node.value);
    } else {
        this->visitNodePtr(node.value);
    }
}

auto AnnotateExprVisitorMark::visit(HIRExprNodeEmplace& node) -> void {
    if (node.type == HIRExprNodeEmplace::Type::Noop) {
        if (node.place) {
            this->visitNodePtr(node.place);
        }
        this->visitNodePtr(node.value);
    } else {
        auto _ = pushUsage(HIRValueUsage::Move);
        if (node.place) {
            this->visitNodePtr(node.place);
        }
        this->visitNodePtr(node.value);
    }
}

auto AnnotateExprVisitorMark::visit(HIRExprNodeField& node) -> void {
    bool isCopy = resolve_.typeIsCopy(node.span(), node.resType);

    DEBUG(StringView("ty = ") << node.resType << StringView(", is_copy=") << isCopy);
    bool savedIgnoreVariableCapure = ignoreVariableCapture;
    if (resolve_.hirCrate().edition >= ASTEdition::Rust2021) {
        if (!closureStack.empty() && !ignoreVariableCapture) {
            Vector<RcString> fields;
            fields.pushBack(node.field);

            auto* inner = node.value.get();
            while (auto* innerField = cast<HIRExprNodeField>(inner)) {
                fields.pushBack(innerField->field);
                inner = innerField->value.get();
            }
            if (auto* innerDeref = cast<HIRExprNodeDeref>(inner)) {
                if (innerDeref->value->resType->is_Borrow()) {
                    fields.pushBack(RcString());
                    inner = innerDeref->value.get();
                }
            }
            if (auto* innerVar = cast<HIRExprNodeVariable>(inner)) {
                std::reverse(fields.mutBegin(), fields.mutEnd());

                markUsedVariable(node.span(), innerVar->slot, fields, this->getUsage());

                ignoreVariableCapture = true;
            }
        }
    }

    if (this->getUsage() == HIRValueUsage::Move && isCopy) {
        auto _ = pushUsage(HIRValueUsage::Borrow);
        this->visitNodePtr(node.value);
    } else {
        this->visitNodePtr(node.value);
    }

    ignoreVariableCapture = savedIgnoreVariableCapure;
}

auto AnnotateExprVisitorMark::visit(HIRExprNodeTupleVariant& node) -> void {
    auto _ = pushUsage(HIRValueUsage::Move);

    for (auto& val : node.args) {
        this->visitNodePtr(val);
    }
}

auto AnnotateExprVisitorMark::visit(HIRExprNodeCallPath& node) -> void {
    auto _ = pushUsage(HIRValueUsage::Move);

    for (auto& val : node.args) {
        this->visitNodePtr(val);
    }
}

auto AnnotateExprVisitorMark::visit(HIRExprNodeCallValue& node) -> void {
    // TODO: Different usage based on trait.
    HIRValueUsage vu = HIRValueUsage::Borrow;

    if (const auto* nodePp = ((*node.value->resType).is_NodeType() ? ((*node.value->resType).as_NodeType().opt_Closure()) : nullptr)) {
        BUG_ASSERT(*nodePp);
        if ((*nodePp)->cls == HIRExprNodeClosure::Class::Unknown) {
            auto _ = pushUsage(HIRValueUsage::Move);
            this->visitNodePtr(node.value);
        }
        switch ((*nodePp)->cls) {
            case HIRExprNodeClosure::Class::Unknown:
                node.traitUsed = HIRExprNodeCallValue::TraitUsed::FnOnce;
                pendingCalls.pushBack(&node);
                break;
            case HIRExprNodeClosure::Class::NoCapture:
            case HIRExprNodeClosure::Class::Shared:
                if (!resolve_.hirCrate().getLangItemPathOpt("fn").components().empty()) {
                    node.traitUsed = HIRExprNodeCallValue::TraitUsed::Fn;
                } else if (!resolve_.hirCrate().getLangItemPathOpt("fn_mut").components().empty()) {
                    node.traitUsed = HIRExprNodeCallValue::TraitUsed::FnMut;
                } else {
                    node.traitUsed = HIRExprNodeCallValue::TraitUsed::FnOnce;
                }
                break;
            case HIRExprNodeClosure::Class::Mut:
                node.traitUsed = !resolve_.hirCrate().getLangItemPathOpt("fn_mut").components().empty() ? HIRExprNodeCallValue::TraitUsed::FnMut : HIRExprNodeCallValue::TraitUsed::FnOnce;
                break;
            case HIRExprNodeClosure::Class::Once:
                node.traitUsed = HIRExprNodeCallValue::TraitUsed::FnOnce;
                break;
        }
    }

    switch (node.traitUsed) {
        case HIRExprNodeCallValue::TraitUsed::Unknown:
            BUG(node.span(), StringView("Annotate usage when CallValue trait is unknown"));
            break;
        case HIRExprNodeCallValue::TraitUsed::Fn:
        case HIRExprNodeCallValue::TraitUsed::AsyncFn:
            vu = HIRValueUsage::Borrow;
            break;
        case HIRExprNodeCallValue::TraitUsed::FnMut:
        case HIRExprNodeCallValue::TraitUsed::AsyncFnMut:
            vu = HIRValueUsage::Mutate;
            break;
        case HIRExprNodeCallValue::TraitUsed::FnOnce:
        case HIRExprNodeCallValue::TraitUsed::AsyncFnOnce:
            vu = HIRValueUsage::Move;
            break;
    }
    {
        auto _ = pushUsage(vu);
        this->visitNodePtr(node.value);
    }

    auto _ = pushUsage(HIRValueUsage::Move);
    for (auto& val : node.args) {
        this->visitNodePtr(val);
    }
}

auto AnnotateExprVisitorMark::visit(HIRExprNodeCallMethod& node) -> void {
    {
        BUG_ASSERT(node.cache.fcn);
        HIRValueUsage vu = HIRValueUsage::Borrow;
        switch (node.cache.fcn->receiver) {
            case HIRFunction::Receiver::Free:
                BUG(node.span(), StringView("_CallMethod resolved to free function"));
            case HIRFunction::Receiver::Value:
            case HIRFunction::Receiver::Box:
            case HIRFunction::Receiver::Custom:
            case HIRFunction::Receiver::BorrowOwned:
                vu = HIRValueUsage::Move;
                break;
            case HIRFunction::Receiver::BorrowUnique:
                vu = HIRValueUsage::Mutate;
                break;
            case HIRFunction::Receiver::BorrowShared:
                vu = HIRValueUsage::Borrow;
                break;
        }
        auto _ = pushUsage(vu);
        this->visitNodePtr(node.value);
    }
    auto _ = pushUsage(HIRValueUsage::Move);
    for (auto& val : node.args) {
        this->visitNodePtr(val);
    }
}

auto AnnotateExprVisitorMark::visit(HIRExprNodeLiteral& node) -> void {
}

auto AnnotateExprVisitorMark::visit(HIRExprNodeUnitVariant& node) -> void {
}

auto AnnotateExprVisitorMark::visit(HIRExprNodePathValue& node) -> void {
}

auto AnnotateExprVisitorMark::visit(HIRExprNodeVariable& node) -> void {
    DEBUG(StringView("_Variable: #") << node.slot << StringView(" '") << node.name << StringView("' ") << node.usage);
    if (!closureStack.empty() && !ignoreVariableCapture) {
        markUsedVariable(node.span(), node.slot, {}, node.usage);
    }
}

auto AnnotateExprVisitorMark::visit(HIRExprNodeConstParam& node) -> void {
}

auto AnnotateExprVisitorMark::visit(HIRExprNodeStructLiteral& node) -> void {
    const auto& sp = node.span();
    const auto& tyPath = node.realPath;
    if (node.baseValue) {
        bool isMoved = false;
        const auto& tpb = node.baseValue->resType->as_Path().binding;
        const tStructFields* fieldsPtr;
        tStructFields tupleFields;
        if (tpb.is_Enum()) {
            const auto& enm = *tpb.as_Enum();
            auto idx = enm.findVariant(tyPath.path.components().back());
            ASSERT_BUG(sp, idx != SIZE_MAX, StringView(""));
            const auto& varTy = enm.data.as_Data()[idx].type;
            const auto& str = *varTy->as_Path().binding.as_Struct();
            ASSERT_BUG(sp, str.data.is_Named(), StringView(""));
            fieldsPtr = &str.data.as_Named();
        } else if (tpb.is_Union()) {
            fieldsPtr = &tpb.as_Union()->variants;
        } else {
            const auto& str = *tpb.as_Struct();
            if (str.data.is_Tuple()) {
                const auto& tuple = str.data.as_Tuple();
                for (size_t i = 0; i < tuple.size(); i++) {
                    tupleFields.push_back(HIRStructField{RcString::newInterned(FMT(i)), tuple[i].publicity, tuple[i].ent, nullptr});
                }
                fieldsPtr = &tupleFields;
            } else {
                ASSERT_BUG(sp, str.data.is_Named(), StringView(""));
                fieldsPtr = &str.data.as_Named();
            }
        }
        const auto& fields = *fieldsPtr;

        Vector<bool> providedMask;
        providedMask.zero(fields.size());
        for (const auto& fld : node.values) {
            unsigned idx = std::find_if(fields.begin(), fields.end(), [&](const HIRStructField& x) {
                return x.name == fld.first;
            }) - fields.begin();
            providedMask.mut(idx) = true;
        }

        const auto monomorphCb = MonomorphStatePtr(resolve_.hirCrate().types, nullptr, &tyPath.params, nullptr);
        for (unsigned int i = 0; i < fields.size(); i++) {
            if (!providedMask[i]) {
                const auto& tyO = fields[i].ty;
                const HIRType* tmp;
                const auto& tyM = monomorphiseTypeWithOpt(node.span(), tyO, monomorphCb);
                bool isCopy = resolve_.typeIsCopy(node.span(), tyM);
                if (!isCopy) {
                    DEBUG(StringView("- Field ") << i << StringView(" ") << fields[i].name << StringView(": ") << tyM << StringView(" moved"));
                    isMoved = true;
                }
            }
        }

        auto _ = pushUsage(isMoved ? HIRValueUsage::Move : HIRValueUsage::Borrow);
        this->visitNodePtr(node.baseValue);
    }

    auto _ = pushUsage(HIRValueUsage::Move);
    for (auto& fldVal : node.values) {
        this->visitNodePtr(fldVal.second);
    }
}

auto AnnotateExprVisitorMark::visit(HIRExprNodeTuple& node) -> void {
    auto _ = pushUsage(HIRValueUsage::Move);
    for (auto& val : node.vals) {
        this->visitNodePtr(val);
    }
}

auto AnnotateExprVisitorMark::visit(HIRExprNodeArrayList& node) -> void {
    auto _ = pushUsage(HIRValueUsage::Move);
    for (auto& val : node.vals) {
        this->visitNodePtr(val);
    }
}

auto AnnotateExprVisitorMark::visit(HIRExprNodeArraySized& node) -> void {
    auto _ = pushUsage(HIRValueUsage::Move);
    this->visitNodePtr(node.val);
}

auto AnnotateExprVisitorMark::visit(HIRExprNodeClosure& node) -> void {
    if (!node.code) {
        DEBUG(StringView("Already expanded (via consteval?)"));
        return;
    }

    closureStack.push_back(ClosureScope(node));

    for (const auto& arg : node.args) {
        addDefsFromPattern(node.span(), arg.first);
    }

    if (node.code) {
        auto _ = pushUsage(HIRValueUsage::Move);
        this->visitNodePtr(node.code);
    }

    auto scope = std::move(closureStack.back().as_Closure());
    closureStack.pop_back();

    if (node.cls == HIRExprNodeClosure::Class::Unknown) {
        DEBUG(StringView("> Class is still `Unknown`, set to `NoCapture`"));
        node.cls = HIRExprNodeClosure::Class::NoCapture;
        DEBUG(StringView("= Class is ") << static_cast<int>(node.cls));
    }

    if (node.isMove || node.isUse) {
        DEBUG(StringView("> Tagged with `") << (node.isUse ? "use" : "move") << StringView("` - upgrading all usage to `Move`"));
        for (auto& cap : scope.capturedVars) {
            if (cap.fields.length() > 0 && cap.fields[0] == "") {
                // TODO: Only clear if the value isn't `Copy`
                cap.fields.clear();
            }
            cap.usage = HIRValueUsage::Move;
        }
    } else {
        for (auto& cap : scope.capturedVars) {
            if (cap.usage == HIRValueUsage::Borrow) {
                const HIRType* tmpTy;
                const auto* capTyP = &variableTypes[cap.rootSlot];
                for (const auto& n : cap.fields) {
                    tmpTy = resolve_.getFieldType(node.span(), *capTyP, n);
                    tmpTy = resolve_.expandAssociatedTypes(node.span(), tmpTy);
                    capTyP = &tmpTy;
                }
                if ((*capTyP)->is_Borrow() && (*capTyP)->as_Borrow().type == HIRBorrowType::Shared) {
                    DEBUG(StringView("> Upgrade capture ") << cap.rootSlot << cap.fields << StringView(" to Move, as it's a shared borrow"));
                    cap.usage = HIRValueUsage::Move;
                }
            }
        }
    }

    {
        node.isCopy = true;
        for (auto& cap : scope.capturedVars) {
            switch (cap.usage) {
                case HIRValueUsage::Unknown:
                    BUG(node.span(), StringView("Usage of capture #") << cap.rootSlot << cap.fields << StringView(" is unknown"));
                case HIRValueUsage::Borrow:
                    break;
                case HIRValueUsage::Mutate:
                    node.isCopy = false;
                    break;
                case HIRValueUsage::Move: {
                    const auto* ty = &variableTypes[cap.rootSlot];
                    const HIRType* tmpTy;
                    for (const auto& fld : cap.fields) {
                        tmpTy = resolve_.getFieldType(node.span(), *ty, fld);
                        ty = &tmpTy;
                    }
                    if (!resolve_.typeIsCopy(node.span(), *ty)) {
                        node.isCopy = false;
                    }
                    break;
                }
            }
            DEBUG(StringView("> Copy closure"));
        }
    }

    if (!closureStack.empty()) {
        DEBUG(StringView("> Apply to parent"));
        for (const auto& v : scope.capturedVars) {
            markUsedVariable(node.span(), v.rootSlot, v.fields, v.usage);
        }
    }

    node.avuCache.capturedVars = std::move(scope.capturedVars);
    node.avuCache.localVars = std::move(scope.localVars);
}

auto AnnotateExprVisitorMark::visit(HIRExprNodeGenerator& node) -> void {
    if (!node.code) {
        DEBUG(StringView("Already expanded (via consteval?)"));
        return;
    }

    closureStack.push_back(CoroutineScope(node));

    if (node.hasResumePattern) {
        addDefsFromPattern(node.span(), node.resumePattern);
    }
    {
        auto _ = pushUsage(HIRValueUsage::Move);
        this->visitNodePtr(node.code);
    }

    auto ent = std::move(closureStack.back().as_Coroutine());
    closureStack.pop_back();

    applyCoroutine(node.span(), node.isMove, node.isCoroutineClosureBody, node.avuCache, ent.usedVariables);
}

auto AnnotateExprVisitorMark::visit(HIRExprNodeGeneratorWrapper&) -> void {
}

auto AnnotateExprVisitorMark::visit(HIRExprNodeAsyncBlock& node) -> void {
    if (!node.code) {
        DEBUG(StringView("Already expanded (via consteval?)"));
        return;
    }

    closureStack.push_back(CoroutineScope(node));

    {
        auto _ = pushUsage(HIRValueUsage::Move);
        this->visitNodePtr(node.code);
    }

    auto scope = std::move(closureStack.back().as_Coroutine());
    closureStack.pop_back();

    applyCoroutine(node.span(), node.isMove || node.isUse, false, node.avuCache, scope.usedVariables);
}

auto AnnotateExprVisitorMark::addVarDefClosure(const Span& sp, ClosureScope& e, unsigned int slot) -> void {
    auto it = std::lower_bound(e.localVars.begin(), e.localVars.end(), slot);
    if (it == e.localVars.end() || *it != slot) {
        const auto index = static_cast<size_t>(it - e.localVars.begin());
        e.localVars.pushBack(slot);
        for (size_t i = e.localVars.length() - 1; i > index; i--) {
            e.localVars.mut(i) = e.localVars[i - 1];
        }
        e.localVars.mut(index) = slot;
    }
}

auto AnnotateExprVisitorMark::addVarDefGenerator(const Span& sp, CoroutineScope& scope, unsigned int slot) -> void {
    auto& e = scope.usedVariables[slot];
    DEBUG(StringView("_Variable: #") << slot << StringView(" '?' stack=[") << scope.yieldStack << StringView("]"));
    e.definedStack = scope.yieldStack;
}

auto AnnotateExprVisitorMark::addVarDef(const Span& sp, unsigned int slot) -> void {
    BUG_ASSERT(closureStack.size() > 0);
    auto& ent = closureStack.back();
    switch (ent.tag()) {
        case Scope::TAG_None: {
            UNREACHABLE();
        }
        case Scope::TAG_Closure: {
            auto& e = ent.as_Closure();
            addVarDefClosure(sp, e, slot);
            break;
        }
        case Scope::TAG_Coroutine: {
            auto& e = ent.as_Coroutine();
            addVarDefGenerator(sp, e, slot);
            break;
        }
    }
}

auto AnnotateExprVisitorMark::addClosureDefFromPattern(const Span& sp, const HIRPattern& pat) -> void {
    for (const auto& pb : pat.bindings) {
        addVarDef(sp, pb.slot);
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
            addClosureDefFromPattern(sp, *e.sub);
            break;
        }
        case HIRPatternData::TAG_Deref: {
            auto& e = pat.data.as_Deref();
            addClosureDefFromPattern(sp, *e.sub);
            break;
        }
        case HIRPatternData::TAG_Ref: {
            auto& e = pat.data.as_Ref();
            addClosureDefFromPattern(sp, *e.sub);
            break;
        }
        case HIRPatternData::TAG_Tuple: {
            auto& e = pat.data.as_Tuple();
            for (const auto& subpat : e.subPatterns) {
                addClosureDefFromPattern(sp, subpat);
            }
            break;
        }
        case HIRPatternData::TAG_SplitTuple: {
            auto& e = pat.data.as_SplitTuple();
            for (const auto& subpat : e.leading) {
                addClosureDefFromPattern(sp, subpat);
            }
            for (const auto& subpat : e.trailing) {
                addClosureDefFromPattern(sp, subpat);
            }
            break;
        }
        case HIRPatternData::TAG_Slice: {
            auto& e = pat.data.as_Slice();
            for (const auto& sub : e.subPatterns) {
                addClosureDefFromPattern(sp, sub);
            }
            break;
        }
        case HIRPatternData::TAG_SplitSlice: {
            auto& e = pat.data.as_SplitSlice();
            for (const auto& sub : e.leading) {
                addClosureDefFromPattern(sp, sub);
            }
            for (const auto& sub : e.trailing) {
                addClosureDefFromPattern(sp, sub);
            }
            if (e.extraBind.isValid()) {
                addVarDef(sp, e.extraBind.slot);
            }
            break;
        }
        case HIRPatternData::TAG_PathValue: {
            break;
        }
        case HIRPatternData::TAG_PathTuple: {
            auto& e = pat.data.as_PathTuple();
            for (const auto& field : e.leading) {
                addClosureDefFromPattern(sp, field);
            }
            for (const auto& field : e.trailing) {
                addClosureDefFromPattern(sp, field);
            }
            break;
        }
        case HIRPatternData::TAG_PathNamed: {
            auto& e = pat.data.as_PathNamed();
            for (auto& fieldPat : e.subPatterns) {
                addClosureDefFromPattern(sp, fieldPat.second);
            }
            break;
        }
        case HIRPatternData::TAG_Or: {
            auto& e = pat.data.as_Or();
            BUG_ASSERT(e.size() > 0);
            addClosureDefFromPattern(sp, e.front());
            break;
        }
    }
}

auto AnnotateExprVisitorMark::addDefsFromPattern(const Span& sp, const HIRPattern& pat) -> void {
    if (!closureStack.empty()) {
        addClosureDefFromPattern(sp, pat);
    }
}

auto AnnotateExprVisitorMark::getUsageForPatternBinding(const Span& sp, const HIRPatternBinding& pb, const HIRType* ty) const -> HIRValueUsage {
    switch (pb.type) {
        case HIRPatternBinding::Type::Move:
            if (resolve_.typeIsCopy(sp, ty)) {
                return HIRValueUsage::Borrow;
            } else {
                return HIRValueUsage::Move;
            }
        case HIRPatternBinding::Type::MutRef:
            return HIRValueUsage::Mutate;
        case HIRPatternBinding::Type::Ref:
            return HIRValueUsage::Borrow;
    }
    UNREACHABLE();
}

auto AnnotateExprVisitorMark::getUsageForPattern(const Span& sp, const HIRPattern& pat, const HIRType* outerTy) const -> HIRValueUsage {
    const HIRType* revealedOuterTy = outerTy;
    revealedOuterTy = resolve_.revealOpaqueTypes(sp, revealedOuterTy);
    outerTy = revealedOuterTy;

    if (pat.bindings.size() > 0) {
        auto vu = HIRValueUsage::Borrow;
        for (const auto& pb : pat.bindings) {
            vu = std::max(vu, getUsageForPatternBinding(sp, pb, outerTy));
        }
        return vu;
    }

    const HIRType* typ = outerTy;
    for (size_t i = 0; i < pat.implicitDerefCount; i++) {
        typ = typ->as_Borrow().inner;
    }
    const HIRType* ty = typ;

    if (ty->is_Diverge()) {
        return HIRValueUsage::Borrow;
    }

    switch (pat.data.tag()) {
        case HIRPatternData::TAG_Any: {
            return HIRValueUsage::Borrow;
        }
        case HIRPatternData::TAG_Box: {
            auto& pe = pat.data.as_Box();
            const auto& sty = ty->as_Path().path.data.as_Generic().params.types.at(0);
            return getUsageForPattern(sp, *pe.sub, sty);
        }
        case HIRPatternData::TAG_Deref: {
            auto& pe = pat.data.as_Deref();
            ASSERT_BUG(sp, pe.kind != HIRPattern::DerefKind::Unknown && pe.targetType, StringView("Untyped deref pattern"));
            if (pe.kind == HIRPattern::DerefKind::Box) {
                return getUsageForPattern(sp, *pe.sub, pe.targetType);
            }
            return pe.kind == HIRPattern::DerefKind::Unique ? HIRValueUsage::Mutate : HIRValueUsage::Borrow;
        }
        case HIRPatternData::TAG_Ref: {
            auto& pe = pat.data.as_Ref();
            return getUsageForPattern(sp, *pe.sub, pe.isSkipped ? ty : ty->as_Borrow().inner);
        }
        case HIRPatternData::TAG_Tuple: {
            auto& pe = pat.data.as_Tuple();
            ASSERT_BUG(sp, ty->is_Tuple(), StringView("Tuple pattern with non-tuple type - ") << ty);
            const auto& subtys = ty->as_Tuple();
            BUG_ASSERT(pe.subPatterns.size() == subtys.length());
            auto rv = HIRValueUsage::Borrow;
            for (unsigned int i = 0; i < subtys.length(); i++) {
                rv = std::max(rv, getUsageForPattern(sp, pe.subPatterns[i], subtys[i]));
            }
            return rv;
        }
        case HIRPatternData::TAG_SplitTuple: {
            auto& pe = pat.data.as_SplitTuple();
            ASSERT_BUG(sp, ty->is_Tuple(), StringView("SplitTuple pattern with non-tuple type - ") << ty);
            const auto& subtys = ty->as_Tuple();
            BUG_ASSERT(pe.leading.size() + pe.trailing.size() <= subtys.length());
            auto rv = HIRValueUsage::Borrow;
            for (unsigned int i = 0; i < pe.leading.size(); i++) {
                rv = std::max(rv, getUsageForPattern(sp, pe.leading[i], subtys[i]));
            }
            for (unsigned int i = 0; i < pe.trailing.size(); i++) {
                rv = std::max(rv, getUsageForPattern(sp, pe.trailing[i], subtys[subtys.length() - pe.trailing.size() + i]));
            }
            return rv;
        }
        case HIRPatternData::TAG_PathValue: {
            return HIRValueUsage::Borrow;
        }
        case HIRPatternData::TAG_PathTuple: {
            auto& pe = pat.data.as_PathTuple();
            BUG_ASSERT(!pe.binding.is_Unbound());

            const auto& flds = patternGetTuple(sp, pe.path, pe.binding);
            if (pe.isSplit) {
                BUG_ASSERT(pe.leading.size() + pe.trailing.size() <= flds.size());
            } else {
                BUG_ASSERT(pe.leading.size() == flds.size());
                BUG_ASSERT(pe.trailing.size() == 0);
            }

            // TODO: Is it possible to avoid monomorphising here?
            BUG_ASSERT(pe.path.data.is_Generic());
            auto monomorphState = MonomorphStatePtr(resolve_.hirCrate().types, nullptr, &pe.path.data.as_Generic().params, nullptr);

            auto rv = HIRValueUsage::Borrow;
            for (unsigned int i = 0; i < pe.leading.size(); i++) {
                auto sty = resolve_.monomorphExpand(sp, flds[i].ent, monomorphState);
                rv = std::max(rv, getUsageForPattern(sp, pe.leading[i], sty));
            }
            for (unsigned int i = 0; i < pe.trailing.size(); i++) {
                auto sty = resolve_.monomorphExpand(sp, flds[flds.size() - pe.trailing.size() + i].ent, monomorphState);
                rv = std::max(rv, getUsageForPattern(sp, pe.trailing[i], sty));
            }
            return rv;
        }
        case HIRPatternData::TAG_PathNamed: {
            auto& pe = pat.data.as_PathNamed();
            BUG_ASSERT(!pe.binding.is_Unbound());

            if (pe.isWildcard()) {
                return HIRValueUsage::Borrow;
            }
            if (pe.subPatterns.empty()) {
                return HIRValueUsage::Borrow;
            }

            const auto& flds = patternGetNamed(sp, pe.path, pe.binding);
            auto monomorphState = MonomorphStatePtr(resolve_.hirCrate().types, nullptr, &pe.path.data.as_Generic().params, nullptr);

            auto rv = HIRValueUsage::Borrow;
            for (const auto& fldPat : pe.subPatterns) {
                auto fldIt = std::find_if(flds.begin(), flds.end(), [&](const HIRStructField& x) {
                    return x.name == fldPat.first;
                });
                ASSERT_BUG(sp, fldIt != flds.end(), StringView("Unable to find field ") << fldPat.first);

                auto sty = resolve_.monomorphExpand(sp, fldIt->ty, monomorphState);
                rv = std::max(rv, getUsageForPattern(sp, fldPat.second, sty));
            }
            return rv;
        }
        case HIRPatternData::TAG_Value: {
            return HIRValueUsage::Borrow;
        }
        case HIRPatternData::TAG_Range: {
            return HIRValueUsage::Borrow;
        }
        case HIRPatternData::TAG_Slice: {
            auto& pe = pat.data.as_Slice();
            const auto& innerTy = (ty->is_Array() ? ty->as_Array().inner : ty->as_Slice().inner);
            auto rv = HIRValueUsage::Borrow;
            for (const auto& pat : pe.subPatterns) {
                rv = std::max(rv, getUsageForPattern(sp, pat, innerTy));
            }
            return rv;
        }
        case HIRPatternData::TAG_SplitSlice: {
            auto& pe = pat.data.as_SplitSlice();
            const auto& innerTy = (ty->is_Array() ? ty->as_Array().inner : ty->as_Slice().inner);
            auto rv = HIRValueUsage::Borrow;
            for (const auto& pat : pe.leading) {
                rv = std::max(rv, getUsageForPattern(sp, pat, innerTy));
            }
            for (const auto& pat : pe.trailing) {
                rv = std::max(rv, getUsageForPattern(sp, pat, innerTy));
            }
            if (pe.extraBind.isValid()) {
                rv = std::max(rv, getUsageForPatternBinding(sp, pe.extraBind, innerTy));
            }
            return rv;
        }
        case HIRPatternData::TAG_Or: {
            auto& pe = pat.data.as_Or();
            auto rv = HIRValueUsage::Borrow;
            for (const auto& pat : pe) {
                rv = std::max(rv, getUsageForPattern(sp, pat, ty));
            }
            return rv;
        }
    }
    UNREACHABLE();
}

/* A closure is Copy when everything it captures is: a shared borrow always is, a
   mutable one never is, and a moved capture is exactly as Copy as what it moved.
   The solver cannot say - what a closure captures is worked out here, after
   typecheck, so it answers Copy for every closure - and taking that answer makes
   a closure that holds a String look copyable.  Whoever then consumes such a
   closure captures it by reference, and moving out through a reference is not
   something MIR can express. */
auto AnnotateExprVisitorMark::typeIsCopyHere(const Span& sp, const HIRType* type) -> bool {
    if (!type->is_NodeType() || !type->as_NodeType().is_Closure()) {
        return resolve_.typeIsCopy(sp, type);
    }
    for (const auto& capture : type->as_NodeType().as_Closure()->avuCache.capturedVars) {
        switch (capture.usage) {
            case HIRValueUsage::Borrow:
                continue;
            case HIRValueUsage::Mutate:
                return false;
            case HIRValueUsage::Unknown:
            case HIRValueUsage::Move:
                break;
        }
        const auto* captured = variableTypes[capture.rootSlot];
        for (const auto& name : capture.fields) {
            captured = resolve_.getFieldType(sp, captured, name);
        }
        if (!typeIsCopyHere(sp, captured)) {
            return false;
        }
    }
    return true;
}

auto AnnotateExprVisitorMark::getRealUsage(const Span& sp, unsigned slot, const Vector<RcString>& fields, HIRValueUsage usage) -> HIRValueUsage {
    if (usage == HIRValueUsage::Move) {
        const auto* ty = &variableTypes[slot];
        const HIRType* tmpTy;
        for (const auto& name : fields) {
            tmpTy = resolve_.getFieldType(sp, *ty, name);
            ty = &tmpTy;
        }

        if (typeIsCopyHere(sp, *ty)) {
            usage = HIRValueUsage::Borrow;
        } else if ((*ty)->is_Borrow() && (*ty)->as_Borrow().type == HIRBorrowType::Unique) {
            usage = HIRValueUsage::Mutate;
        } else {
        }
    }
    return usage;
}

auto AnnotateExprVisitorMark::markUsedVariableClosure(const Span& sp, ClosureScope& closureRec, unsigned slot, Vector<RcString> fields, HIRValueUsage usage) -> void {
    DEBUG(StringView("(") << slot << StringView(", [") << fields << StringView("], usage=") << usage << StringView(")"));
    const auto& closureDefs = closureRec.localVars;
    auto& closure = closureRec.node;

    if (std::binary_search(closureDefs.begin(), closureDefs.end(), slot)) {
        return;
    }
    usage = getRealUsage(sp, slot, fields, usage);

    HIRExprNodeClosure::AvuCache::Capture newEnt;
    newEnt.rootSlot = slot;
    newEnt.fields = std::move(fields);
    newEnt.usage = usage;

    auto its = std::equal_range(closureRec.capturedVars.begin(), closureRec.capturedVars.end(), newEnt, [](const HIRExprNodeClosure::AvuCache::Capture& a, const HIRExprNodeClosure::AvuCache::Capture& b) -> bool {
        if (a.rootSlot < b.rootSlot) {
            return true;
        } else if (a.rootSlot > b.rootSlot) {
            return false;
        } else {
            if (::ord(a.fields, b.fields) == OrdEqual) {
                return false;
            }
            auto prefixLen = std::min(a.fields.length(), b.fields.length());
            for (size_t i = 0; i < prefixLen; i++) {
                if (b.fields[i] != a.fields[i]) {
                    return b.fields[i] < a.fields[i];
                }
            }
            return false;
        }
    });
    if (its.first == its.second) {
        DEBUG(StringView("Insert"));
        closureRec.capturedVars.insert(its.first, newEnt);
    } else if (its.first->fields.length() <= newEnt.fields.length()) {
        DEBUG(StringView("new longer"));
        BUG_ASSERT(its.first->rootSlot == newEnt.rootSlot);
        for (size_t i = 0; i < its.first->fields.length(); i++) {
            BUG_ASSERT(its.first->fields[i] == newEnt.fields[i]);
        }
        ASSERT_BUG(sp, its.first + 1 == its.second, StringView("Prefix total match, but multiple matching entries?"));
        its.first->usage = std::max(its.first->usage, newEnt.usage);
    } else {
        DEBUG(StringView("new shorter or equal"));
        BUG_ASSERT(its.first->rootSlot == newEnt.rootSlot);
        BUG_ASSERT(its.first->fields.length() >= newEnt.fields.length());
        for (size_t i = 0; i < newEnt.fields.length(); i++) {
            BUG_ASSERT(its.first->fields[i] == newEnt.fields[i]);
        }

        if (its.first + 1 == its.second) {
        } else {
            BUG_ASSERT(its.first->fields.length() > newEnt.fields.length());
            for (auto it = its.first + 1; it != its.second; ++it) {
                its.first->usage = std::max(its.first->usage, it->usage);
            }
            closureRec.capturedVars.erase(its.first + 1, its.second);
        }
        its.first->usage = std::max(its.first->usage, newEnt.usage);
        if (its.first->fields.length() != newEnt.fields.length()) {
            its.first->fields = newEnt.fields;
        }
    }

    const char* capTypeName = "?";
    switch (usage) {
        case HIRValueUsage::Unknown:
            BUG(sp, StringView("Unknown usage of variable ") << slot);
        case HIRValueUsage::Borrow:
            capTypeName = "Borrow";
            closure.cls = std::max(closure.cls, HIRExprNodeClosure::Class::Shared);
            break;
        case HIRValueUsage::Mutate:
            capTypeName = "Mutate";
            closure.cls = std::max(closure.cls, HIRExprNodeClosure::Class::Mut);
            break;
        case HIRValueUsage::Move:
            // TODO: Why is this disabled? Maybe for efficiency? as copies are marked as ValueUsage::Borrow anyway
            if (typeIsCopyHere(sp, variableTypes[slot])) {
                capTypeName = "Borrow (copy)";
                closure.cls = std::max(closure.cls, HIRExprNodeClosure::Class::Shared);
            } else {
                capTypeName = "Move";
                closure.cls = std::max(closure.cls, HIRExprNodeClosure::Class::Once);
            }
            break;
    }
    DEBUG(StringView("Captured ") << slot << StringView(" - ") << variableTypes[slot] << StringView(" :: ") << capTypeName);
}

auto AnnotateExprVisitorMark::markUsedVariableGenerator(const Span& sp, CoroutineScope& scope, unsigned int slot, Vector<RcString> fields, HIRValueUsage usage) -> void {
    auto& e = scope.usedVariables[slot];
    e.lastUsedStack = scope.yieldStack;
    e.usage = std::max(e.usage, getRealUsage(sp, slot, fields, usage));
    DEBUG(StringView("Used #") << slot << StringView(" :: stack=[") << e.lastUsedStack << StringView("]"));
}

auto AnnotateExprVisitorMark::markUsedVariable(const Span& sp, unsigned int slot, Vector<RcString> fields, HIRValueUsage usage) -> void {
    BUG_ASSERT(closureStack.size() > 0);
    auto& ent = closureStack.back();
    switch (ent.tag()) {
        case Scope::TAG_None: {
            UNREACHABLE();
        }
        case Scope::TAG_Closure: {
            auto& e = ent.as_Closure();
            markUsedVariableClosure(sp, e, slot, fields, usage);
            break;
        }
        case Scope::TAG_Coroutine: {
            auto& e = ent.as_Coroutine();
            markUsedVariableGenerator(sp, e, slot, fields, usage);
            break;
        }
    }
}

auto AnnotateExprVisitorMark::applyCoroutine(const Span& sp, bool isMove, bool isCoroutineClosureBody, HIRExprNodeGenerator::AvuCache& avuCache, std::map<unsigned, CoroutineScope::Var>& usedVariables) -> void {
    if (isMove) {
        auto* parentClosure = !closureStack.empty() ? closureStack.back().opt_Closure() : nullptr;
        for (auto& cap : usedVariables) {
            if (cap.second.definedStack.empty() && (!isCoroutineClosureBody || !parentClosure || parentClosure->node.isMove || parentClosure->node.isUse || std::binary_search(parentClosure->localVars.begin(), parentClosure->localVars.end(), cap.first))) {
                cap.second.usage = HIRValueUsage::Move;
            }
        }
    } else {
        for (auto& cap : usedVariables) {
            if (cap.second.usage == HIRValueUsage::Borrow) {
                const HIRType* tmpTy;
                const auto* capTyP = &variableTypes[cap.first];
                if ((*capTyP)->is_Borrow() && (*capTyP)->as_Borrow().type == HIRBorrowType::Shared) {
                    DEBUG(StringView("> Upgrade capture #") << cap.first << StringView(" to Move, as it's a shared borrow"));
                    cap.second.usage = HIRValueUsage::Move;
                }
            }
        }
    }

    if (closureStack.size() > 0) {
        DEBUG(StringView("> Apply to parent"));
        for (const auto& cap : usedVariables) {
            if (cap.second.definedStack.empty()) {
                markUsedVariable(sp, cap.first, {}, cap.second.usage);
            }
        }
    }

    {
        size_t nCaps = 0;
        size_t nLocals = 0;
        for (const auto& cap : usedVariables) {
            if (cap.second.definedStack.empty()) {
                nCaps += 1;
            } else {
                nLocals += 1;
            }
        }
        avuCache.capturedVars.grow(nCaps);
        avuCache.localVars.grow(nLocals);
    }
    for (const auto& cap : usedVariables) {
        if (cap.second.definedStack.empty()) {
            avuCache.capturedVars.pushBack(std::make_pair(cap.first, cap.second.usage));
        } else {
            avuCache.localVars.pushBack(cap.first);
        }
    }
}

AnnotateExprVisitorMark::UsageGuard::UsageGuard(AnnotateExprVisitorMark& parent, bool pop)
    : parent(parent)
    , pop(pop)
{
}

AnnotateExprVisitorMark::UsageGuard::~UsageGuard() {
    if (pop) {
        parent.usage.popBack();
    }
}

AnnotateOuterVisitor::AnnotateOuterVisitor(const WireBoard& wb)
    : HIRVisitor(nullptr, wb.crate->types)
    , resolve_(wb)
{
}

auto AnnotateOuterVisitor::visitExpr(HIRExprPtr& exp) -> void {
    if (exp) {
        AnnotateExprVisitorMark ev{resolve_, exp.bindings};
        ev.visitRoot(exp);
    }
}

auto AnnotateOuterVisitor::visitFunction(HIRItemPath p, HIRFunction& item) -> void {
    auto _ = this->resolve_.setItemGenerics(item.params);
    DEBUG(StringView("Function ") << p);
    HIRVisitor::visitFunction(p, item);
}

auto AnnotateOuterVisitor::visitStatic(HIRItemPath p, HIRStatic& item) -> void {
    HIRVisitor::visitStatic(p, item);
}

auto AnnotateOuterVisitor::visitConstant(HIRItemPath p, HIRConstant& item) -> void {
    HIRVisitor::visitConstant(p, item);
}

auto AnnotateOuterVisitor::visitEnum(HIRItemPath p, HIREnum& item) -> void {
    auto _ = this->resolve_.setItemGenerics(item.params);
    HIRVisitor::visitEnum(p, item);
}

auto AnnotateOuterVisitor::visitTrait(HIRItemPath p, HIRTrait& item) -> void {
    auto _ = this->resolve_.setImplGenerics(MetadataType::TraitObject, item.params);
    HIRVisitor::visitTrait(p, item);
}

auto AnnotateOuterVisitor::visitTypeImpl(HIRTypeImpl& impl) -> void {
    TRACE_FUNCTION_F(StringView("impl ") << impl.type);
    auto _ = this->resolve_.setImplGenerics(impl.type, impl.params);

    HIRVisitor::visitTypeImpl(impl);
}

auto AnnotateOuterVisitor::visitTraitImpl(const HIRSimplePath& traitPath, HIRTraitImpl& impl) -> void {
    TRACE_FUNCTION_F(StringView("impl ") << traitPath << StringView(" for ") << impl.type);
    auto _ = this->resolve_.setImplGenerics(impl.type, impl.params);

    HIRVisitor::visitTraitImpl(traitPath, impl);
}

template <typename F>
ClosureTypeCb<F>::ClosureTypeCb(F f)
    : f(f)
{
}

template <typename F>
auto ClosureTypeCb<F>::create(const char* prefix, const char* suffix, HIRTypeItem item) -> std::pair<HIRSimplePath, HIRTypeItem*> {
    return f(prefix, suffix, mv$(item));
}

auto OutState::saveCounts() const -> Counts {
    return Counts{implsClosure.size(), traitImpls.size(), implsType.size()};
}

auto OutState::updateSourceModule(Counts c, const HIRSimplePath& path) -> void {
    for (auto i = c.closure; i < implsClosure.size(); i++) {
        if (implsClosure[i].second.srcModule == HIRSimplePath()) {
            implsClosure[i].second.srcModule = path;
        }
    }
    for (auto i = c.traits; i < traitImpls.size(); i++) {
        if (traitImpls[i].second.srcModule == HIRSimplePath()) {
            traitImpls[i].second.srcModule = path;
        }
    }
    for (auto i = c.type; i < implsType.size(); i++) {
        if (implsType[i]->srcModule == HIRSimplePath()) {
            implsType[i]->srcModule = path;
        }
    }
}

ClosureExprVisitorMutate::ClosureExprVisitorMutate(ObjPool* pool, const HIRType* closureType, const Vector<unsigned int>& localVars, const std::vector<HIRExprNodeClosure::AvuCache::Capture>& captures, const Monomorphiser& mcb)
    : HIRExprVisitorDef(mcb.typeInterner())
    , closureType(closureType)
    , localVars(localVars)
    , captures(captures)
    , monomorphiser(mcb)
    , pool(pool)
{
}

auto ClosureExprVisitorMutate::visitPattern(const Span& sp, HIRPattern& pat) -> void {
    for (auto& pb : pat.bindings) {
        auto bindingIt = std::find(localVars.begin(), localVars.end(), pb.slot);
        if (bindingIt != localVars.end()) {
            pb.slot = 1 + (bindingIt - localVars.begin());
        } else {
            BUG(sp, StringView("Pattern binds to non-local - ") << pb);
        }
    }

    if (auto* e = pat.data.opt_SplitSlice()) {
        if (e->extraBind.isValid()) {
            auto bindingIt = std::find(localVars.begin(), localVars.end(), e->extraBind.slot);
            if (bindingIt != localVars.end()) {
                e->extraBind.slot = 1 + (bindingIt - localVars.begin());
            } else {
                BUG(sp, StringView("Pattern (split slice extra) binds to non-local - ") << e->extraBind);
            }
        }
    }

    HIRExprVisitorDef::visitPattern(sp, pat);
}

[[nodiscard]] auto ClosureExprVisitorMutate::visitType(const HIRType* ty) -> const HIRType* {
    DEBUG(ty);
    return monomorphiser.monomorphType(Span(), ty, /*allow_infer=*/true);
}

auto ClosureExprVisitorMutate::visitPathParams(HIRPathParams& pp) -> void {
    pp = monomorphiser.monomorphPathParams(Span(), pp, /*allow_infer*/ false);
}

auto ClosureExprVisitorMutate::visit(HIRExprNodeArraySized& node) -> void {
    HIRExprVisitorDef::visit(node);
    node.size = monomorphiser.monomorphArraysize(node.span(), node.size);
}

auto ClosureExprVisitorMutate::visitNodePtr(HIRExprNodeP& nodePtr) -> void {
    BUG_ASSERT(nodePtr);
    auto& node = *nodePtr;
    const char* nodeTy = typeid(node).name();
    TRACE_FUNCTION_FR(StringView("[_Mutate] ") << static_cast<const void*>(&node) << StringView(" ") << nodeTy << StringView(" : ") << node.resType, nodeTy);
    node.visit(*this);

    if (replacement_) {
        nodePtr = mv$(replacement_);
    }

    nodePtr->resType = visitType(nodePtr->resType);
}

auto ClosureExprVisitorMutate::visit(HIRExprNodeClosure& node) -> void {
    BUG_ASSERT(!node.code);

    visitGenericPath(HIRVisitor::PathContext::VALUE, node.objPath);
    for (auto& subnode : node.captures) {
        visitNodePtr(subnode);
    }
}

auto ClosureExprVisitorMutate::visit(HIRExprNodeGenerator& node) -> void {
    BUG_ASSERT(!node.code);

    visitGenericPath(HIRVisitor::PathContext::VALUE, node.objPath);
    node.stateDataType = visitType(node.stateDataType);
    for (auto& subnode : node.captures) {
        visitNodePtr(subnode);
    }
}

auto ClosureExprVisitorMutate::visit(HIRExprNodeAsyncBlock& node) -> void {
    BUG_ASSERT(!node.code);

    visitGenericPath(HIRVisitor::PathContext::VALUE, node.objPath);
    node.stateDataType = visitType(node.stateDataType);
    for (auto& subnode : node.captures) {
        visitNodePtr(subnode);
    }
}

#ifdef NEWNODE
    #undef NEWNODE
#endif
#define NEWNODE(TY, CLASS, ...) closureMkExprnodep(pool->make<HIRExprNode##CLASS>(__VA_ARGS__), TY)

auto ClosureExprVisitorMutate::visit(HIRExprNodeVariable& node) -> void {
    {
        auto bindingIt = std::find(localVars.begin(), localVars.end(), node.slot);
        if (bindingIt != localVars.end()) {
            auto newSlot = 1 + bindingIt - localVars.begin();
            DEBUG(StringView("_Variable: #") << node.slot << StringView(" -> #") << newSlot);
            node.slot = newSlot;
            return;
        }
    }

    {
        auto bindingIt = std::find_if(captures.begin(), captures.end(), [&](const HIRExprNodeClosure::AvuCache::Capture& x) {
            return x.rootSlot == node.slot;
        });
        if (bindingIt != captures.end()) {
            ASSERT_BUG(node.span(), bindingIt->fields.empty(), StringView("Reached _Variable for a field capture"));
            replacement_ = NEWNODE(node.resType, Field, node.span(), getSelf(node.span()), RcString::newInterned(FMT(bindingIt - captures.begin())));
            if (bindingIt->usage != HIRValueUsage::Move) {
                auto bt = (bindingIt->usage == HIRValueUsage::Mutate ? HIRBorrowType::Unique : HIRBorrowType::Shared);

                replacement_->resType = visitType(replacement_->resType);
                replacement_->resType = monomorphiser.typeInterner().borrow(bt, replacement_->resType);
                replacement_ = NEWNODE(node.resType, Deref, node.span(), mv$(replacement_));
            }
            replacement_->usage = node.usage;
            DEBUG(StringView("_Variable: #") << node.slot << StringView(" -> capture"));
            return;
        }
    }

    BUG(node.span(), StringView("Encountered non-captured and unknown-origin variable - ") << node.name << StringView(" #") << node.slot);
}

#undef NEWNODE

#ifdef NEWNODE
    #undef NEWNODE
#endif
#define NEWNODE(TY, CLASS, ...) closureMkExprnodep(pool->make<HIRExprNode##CLASS>(__VA_ARGS__), TY)

auto ClosureExprVisitorMutate::visit(HIRExprNodeField& node) -> void {
    Vector<RcString> fields;
    fields.pushBack(node.field);

    auto* inner = node.value.get();
    while (auto* innerField = cast<HIRExprNodeField>(inner)) {
        fields.pushBack(innerField->field);
        inner = innerField->value.get();
    }
    if (auto* innerDeref = cast<HIRExprNodeDeref>(inner)) {
        fields.pushBack(RcString());
        inner = innerDeref->value.get();
    }
    if (auto* innerVar = cast<HIRExprNodeVariable>(inner)) {
        std::reverse(fields.mutBegin(), fields.mutEnd());

        auto bindingIt = std::find_if(captures.begin(), captures.end(), [&](const HIRExprNodeClosure::AvuCache::Capture& x) {
            return x.rootSlot == innerVar->slot && ::ord(x.fields, fields) == OrdEqual;
        });
        if (bindingIt != captures.end()) {
            replacement_ = NEWNODE(node.resType, Field, node.span(), getSelf(node.span()), RcString::newInterned(FMT(bindingIt - captures.begin())));
            if (bindingIt->usage != HIRValueUsage::Move) {
                auto bt = (bindingIt->usage == HIRValueUsage::Mutate ? HIRBorrowType::Unique : HIRBorrowType::Shared);

                replacement_->resType = visitType(replacement_->resType);
                replacement_->resType = monomorphiser.typeInterner().borrow(bt, replacement_->resType);
                replacement_ = NEWNODE(node.resType, Deref, node.span(), mv$(replacement_));
            }
            replacement_->usage = node.usage;
            DEBUG(StringView("_Field: #") << innerVar->slot << fields << StringView(" -> capture"));
            return;
        }
    }

    HIRExprVisitorDef::visit(node);
}

#undef NEWNODE

auto ClosureExprVisitorMutate::visit(HIRExprNodeConstParam& node) -> void {
    node.binding = monomorphiser.getValue(node.span(), HIRGenericRef("", node.binding)).as_Generic().binding;
}

#ifdef NEWNODE
    #undef NEWNODE
#endif
#define NEWNODE(TY, CLASS, ...) closureMkExprnodep(pool->make<HIRExprNode##CLASS>(__VA_ARGS__), TY)

auto ClosureExprVisitorMutate::getSelf(const Span& sp) const -> HIRExprNodeP {
    HIRExprNodeP self;
    switch (closureType->as_NodeType().as_Closure()->cls) {
        case HIRExprNodeClosure::Class::Unknown:
        case HIRExprNodeClosure::Class::NoCapture:
        case HIRExprNodeClosure::Class::Shared:
            self = NEWNODE(closureType, Deref, sp, NEWNODE(monomorphiser.typeInterner().borrow(HIRBorrowType::Shared, closureType), Variable, sp, RcString("self"), 0));
            break;
        case HIRExprNodeClosure::Class::Mut:
            self = NEWNODE(closureType, Deref, sp, NEWNODE(monomorphiser.typeInterner().borrow(HIRBorrowType::Unique, closureType), Variable, sp, RcString("self"), 0));
            break;
        case HIRExprNodeClosure::Class::Once:
            self = NEWNODE(closureType, Variable, sp, RcString("self"), 0);
            break;
    }
    return self;
}

#undef NEWNODE

AnonymousTypeMonomorph::AnonymousTypeMonomorph(const Monomorphiser& pathMonomorphiser, bool allowUnextracted)
    : MonomorphiserNop(pathMonomorphiser.typeInterner())
    , pathMonomorphiser(pathMonomorphiser)
    , allowUnextracted(allowUnextracted)
{
}

auto AnonymousTypeMonomorph::monomorphType(const Span& sp, const HIRType* ty, bool allowInfer) const -> const HIRType* {
    if (const auto* e = ty->opt_NodeType()) {
        const HIRStruct* object = nullptr;
        const HIRGenericPath* path = nullptr;
        const char* kind = nullptr;
        switch (e->tag()) {
            case HIRTypeDataNodeType::TAG_Closure: {
                const auto* node = e->as_Closure();
                object = node->objPtr;
                path = &node->objPathBase;
                kind = "Closure";
                break;
            }
            case HIRTypeDataNodeType::TAG_Generator: {
                const auto* node = e->as_Generator();
                object = node->objPtr;
                path = &node->objPathBase;
                kind = "Generator";
                break;
            }
            case HIRTypeDataNodeType::TAG_Async: {
                const auto* node = e->as_Async();
                object = node->objPtr;
                path = &node->objPathBase;
                kind = "Async block";
                break;
            }
        }
        if (!object || *path == HIRGenericPath()) {
            ASSERT_BUG(sp, allowUnextracted, kind << StringView(" type was used before extraction"));
            return ty;
        }
        DEBUG(kind << StringView(": ") << *path);
        auto concretePath = pathMonomorphiser.monomorphGenericpath(sp, *path, false);
        DEBUG(ty << StringView(" -> ") << concretePath);
        return types.path(mv$(concretePath), HIRTypePathBinding::make_Struct(object));
    }

    auto rv = MonomorphiserNop::monomorphType(sp, ty, allowInfer);
    if (const auto* e = rv->opt_Path()) {
        if (e->binding.is_Unbound() && e->path.data.is_UfcsKnown()) {
            auto data = rv->cloneData();
            data.as_Path().binding = HIRTypePathBinding::make_Opaque({});
            rv = types.intern(mv$(data));
        }
    }
    return rv;
}

ClosureExprVisitorFixup::ClosureExprVisitorFixup(const WireBoard& wb, const HIRGenericParams* params, const Monomorphiser& monomorphiser, const OutState* out, bool allowUnextracted)
    : HIRExprVisitorDef(wb.crate->types)
    , crate(*wb.crate)
    , resolve_(wb)
    , pool(crate.pool)
    , monomorphiser(monomorphiser)
    , out(out)
    , allowUnextracted(allowUnextracted)
    , runEat(false)
{
    if (params) {
        resolve_.setImplGenericsRaw(MetadataType::None, *params);
        runEat = true;
    }
}

auto ClosureExprVisitorFixup::visitRoot(HIRExprPtr& root) -> void {
    root->visit(*this);
    root->resType = visitType(root->resType);

    for (auto& type : mutRange(root.bindings)) {
        type = visitType(type);
    }

    for (auto& type : mutRange(root.erasedTypes)) {
        type = visitType(type);
    }
}

auto ClosureExprVisitorFixup::visitNodePtr(HIRExprNodeP& node) -> void {
    node->visit(*this);
    node->resType = visitType(node->resType);
}

#ifdef NEWNODE
    #undef NEWNODE
#endif
#define NEWNODE(TY, CLASS, ...) closureMkExprnodep(pool->make<HIRExprNode##CLASS>(__VA_ARGS__), TY)

auto ClosureExprVisitorFixup::visit(HIRExprNodeCast& node) -> void {
    const Span& sp = node.span();
    if (((*node.value->resType).is_NodeType() && ((*node.value->resType).as_NodeType().is_Closure()))) {
        TRACE_FUNCTION_FR(StringView("_Cast: ") << static_cast<const void*>(&node) << StringView(" ") << node.value->resType, node.value->resType);
        const auto* srcNodep = node.value->resType->as_NodeType().as_Closure();
        ASSERT_BUG(sp, srcNodep, StringView(""));
        const auto& srcNode = *srcNodep;
        ASSERT_BUG(sp, node.resType->is_Function(), StringView("Cannot convert closure to non-fn type"));
        if (srcNode.cls != HIRExprNodeClosure::Class::NoCapture) {
            ERROR(sp, E0000, StringView("Cannot cast a closure with captures to a fn() type"));
        }

        // TODO: Store a path on the closure node to avoid issues with leakage.

        const auto& str = *srcNode.objPtr;
        auto closureType = crate.types.path(srcNode.objPath.clone(), &str);
        auto fnPath = HIRPath(mv$(closureType), RcString("call_free"));
        fnPath.data.as_UfcsInherent().implParams = srcNode.objPath.params.clone();

        const HIRFunction* fcnPtr = nullptr;
        if (out) {
            for (const auto& impl : out->implsType) {
                const auto& path = impl->type->as_Path().path.data.as_Generic().path;
                if (srcNode.objPath.path == path) {
                    fcnPtr = &impl->methods.begin()->second.data;
                }
            }
        } else {
            TODO(node.span(), StringView("Use get_value"));
        }
        ASSERT_BUG(node.span(), fcnPtr, StringView("No function found?"));

        auto resTy = crate.types.intern(HIRType::make_NamedFunction({fnPath.clone(), fcnPtr}));

        DEBUG(StringView("PathValue ") << fnPath);
        node.value = NEWNODE(mv$(resTy), PathValue, sp, mv$(fnPath), HIRExprNodePathValue::FUNCTION);
    }
    HIRExprVisitorDef::visit(node);
}

#undef NEWNODE

auto ClosureExprVisitorFixup::visit(HIRExprNodeCallValue& node) -> void {
    if (const auto* nodePp = ((*node.value->resType).is_NodeType() ? ((*node.value->resType).as_NodeType().opt_Closure()) : nullptr)) {
        switch ((*nodePp)->cls) {
            case HIRExprNodeClosure::Class::Unknown:
                BUG(node.span(), StringView("References an ::Unknown closure"));
            case HIRExprNodeClosure::Class::NoCapture:
            case HIRExprNodeClosure::Class::Shared:
                if (!resolve_.hirCrate().getLangItemPathOpt("fn").components().empty()) {
                    node.traitUsed = HIRExprNodeCallValue::TraitUsed::Fn;
                } else if (!resolve_.hirCrate().getLangItemPathOpt("fn_mut").components().empty()) {
                    node.traitUsed = HIRExprNodeCallValue::TraitUsed::FnMut;
                } else {
                    node.traitUsed = HIRExprNodeCallValue::TraitUsed::FnOnce;
                }
                break;
            case HIRExprNodeClosure::Class::Mut:
                node.traitUsed = !resolve_.hirCrate().getLangItemPathOpt("fn_mut").components().empty() ? HIRExprNodeCallValue::TraitUsed::FnMut : HIRExprNodeCallValue::TraitUsed::FnOnce;
                break;
            case HIRExprNodeClosure::Class::Once:
                node.traitUsed = HIRExprNodeCallValue::TraitUsed::FnOnce;
                break;
        }
    }

    HIRExprVisitorDef::visit(node);
}

[[nodiscard]] auto ClosureExprVisitorFixup::visitType(const HIRType* ty) -> const HIRType* {
    AnonymousTypeMonomorph m{monomorphiser, allowUnextracted};
    return m.monomorphType(Span(), ty, true);
}

auto H::fixFnParams(HIRExprPtr& code, const HIRType* selfTy, const HIRType* argsTy) -> void {
    // TODO: The self_ty here is wrong, the borrow needs to be included.
    if (code.bindings.length() == 0) {
        code.bindings.pushBack(selfTy);
        code.bindings.pushBack(argsTy);
    } else {
        BUG_ASSERT(code.bindings.length() >= 1);
        BUG_ASSERT(code.bindings[0]->is_Infer() && code.bindings[0]->as_Infer().index == ~0u);
        code.bindings.mut(0) = selfTy;
    }
}

auto H::makeFnfree(HIRTypeInterner& types, HIRGenericParams params, const HIRType* closureType, std::vector<std::pair<HIRPattern, const HIRType*>> args, const HIRType* retTy, HIRExprPtr code) -> HIRTypeImpl {
    BUG_ASSERT(code.bindings.length() > 0);
    code.bindings.mut(0) = types.unit();
    return HIRTypeImpl{std::move(params), std::move(closureType), makeMap1(RcString("call_free"), HIRTypeImpl::VisImplEnt<HIRFunction>{HIRPublicity::newGlobal(), false, HIRFunction(HIRFunction::Receiver::Free, HIRGenericParams{}, mv$(args), retTy, mv$(code))}), {}, {}, HIRSimplePath()};
}

auto H::makeFnonce(HIRGenericParams params, HIRPathParams traitParams, const HIRType* closureType, std::pair<HIRPattern, const HIRType*> argsArgent, const HIRType* retTy, HIRExprPtr code) -> HIRTraitImpl {
    auto tyOfSelf = closureType;
    fixFnParams(code, tyOfSelf, argsArgent.second);
    return HIRTraitImpl{mv$(params), mv$(traitParams), mv$(closureType), makeMap1(RcString("call_once"), HIRTraitImpl::ImplEnt<HIRFunction>{false, HIRFunction{HIRFunction::Receiver::Value, HIRGenericParams{}, makeVec2(std::make_pair(HIRPattern{HIRPatternBinding{false, HIRPatternBinding::Type::Move, RcString("self"), 0}, {}}, mv$(tyOfSelf)), mv$(argsArgent)), retTy, mv$(code)}}), {}, {}, makeMap1(RcString::newInterned("Output"), HIRTraitImpl::ImplEnt<const HIRType*>{false, mv$(retTy)}), HIRSimplePath()};
}

auto H::makeFnmut(HIRTypeInterner& types, HIRGenericParams params, HIRPathParams traitParams, const HIRType* closureType, std::pair<HIRPattern, const HIRType*> argsArgent, const HIRType* retTy, HIRExprPtr code) -> HIRTraitImpl {
    HIRGenericParams fcnParams;
    auto tyOfSelf = types.borrow(HIRBorrowType::Unique, closureType);
    fixFnParams(code, tyOfSelf, argsArgent.second);
    return HIRTraitImpl{mv$(params), mv$(traitParams), mv$(closureType), makeMap1(RcString("call_mut"), HIRTraitImpl::ImplEnt<HIRFunction>{false, HIRFunction{HIRFunction::Receiver::BorrowUnique, mv$(fcnParams), makeVec2(std::make_pair(HIRPattern{{false, HIRPatternBinding::Type::Move, RcString("self"), 0}, {}}, mv$(tyOfSelf)), mv$(argsArgent)), retTy, mv$(code)}}), {}, {}, {}, HIRSimplePath()};
}

auto H::makeFn(HIRTypeInterner& types, HIRGenericParams params, HIRPathParams traitParams, const HIRType* closureType, std::pair<HIRPattern, const HIRType*> argsArgent, const HIRType* retTy, HIRExprPtr code) -> HIRTraitImpl {
    auto tyOfSelf = types.borrow(HIRBorrowType::Shared, closureType);
    fixFnParams(code, tyOfSelf, argsArgent.second);
    return HIRTraitImpl{mv$(params), mv$(traitParams), mv$(closureType), makeMap1(RcString("call"), HIRTraitImpl::ImplEnt<HIRFunction>{false, HIRFunction{HIRFunction::Receiver::BorrowShared, HIRGenericParams{}, makeVec2(std::make_pair(HIRPattern{{false, HIRPatternBinding::Type::Move, RcString("self"), 0}, {}}, mv$(tyOfSelf)), mv$(argsArgent)), retTy, mv$(code)}}), {}, {}, {}, HIRSimplePath()};
}

ClosureExprVisitorExtract::ClosureExprVisitorExtract(const StaticTraitResolve& resolve, const HIRType* selfType, const Vector<const HIRType*>& varTypes, const HIRExprPtr& exprPtr, OutState& out, const char* newTypeSuffix, bool isAsyncDropIntrinsic)
    : HIRExprVisitorDef(resolve.hirCrate().types)
    , resolve_(resolve)
    , pool(resolve.hirCrate().pool)
    , selfType(selfType)
    , variableTypes(varTypes)
    , exprPtr(exprPtr)
    , out(out)
    , newTypeSuffix(newTypeSuffix)
    , isAsyncDropIntrinsic(isAsyncDropIntrinsic)
{
}

auto ClosureExprVisitorExtract::visitRoot(HIRExprNode& root) -> void {
    root.visit(*this);
    finishDeferredFixups();
}

auto ClosureExprVisitorExtract::deferFixups(const HIRPathParams& sourceParams, OutState::Counts first, HIRStruct& structure) -> void {
    auto last = out.saveCounts();
    deferredFixups = pool->make<DeferredFixup>(sourceParams.clone(), first, last, &structure, deferredFixups);
}

auto ClosureExprVisitorExtract::finishDeferredFixups() -> void {
    for (auto* pending = deferredFixups; pending; pending = pending->next) {
        FrozenMonomorph monomorph{resolve_.hirCrate().types, pending->sourceParams};
        DeferredItemFixup fixup{monomorph};
        fixup.visitGeneratedStruct(*pending->structure);
        for (auto i = pending->first.closure; i < pending->last.closure; i++) {
            fixup.visitTraitImpl(HIRSimplePath(), out.implsClosure[i].second);
        }
        for (auto i = pending->first.traits; i < pending->last.traits; i++) {
            fixup.visitTraitImpl(HIRSimplePath(), out.traitImpls[i].second);
        }
        for (auto i = pending->first.type; i < pending->last.type; i++) {
            fixup.visitTypeImpl(*out.implsType[i]);
        }
    }
    deferredFixups = nullptr;
}

auto ClosureExprVisitorExtract::isActive(const void* node) const -> bool {
    for (auto* entry = activeNode; entry; entry = entry->parent) {
        if (entry->node == node) {
            return true;
        }
    }
    return false;
}

template <typename Node>
auto ClosureExprVisitorExtract::extractReferencedNode(const Span& sp, const Node* constNode) -> void {
    if (constNode->objPathBase != HIRGenericPath()) {
        ASSERT_BUG(sp, constNode->objPtr, StringView("Extracted anonymous type has no type item"));
        return;
    }

    ASSERT_BUG(sp, !isActive(constNode), StringView("Cyclic anonymous type dependency"));
    HIRExprNode* mutableNode = nullptr;
    for (const auto& entry : out.mutableNodes) {
        if (entry.first == constNode) {
            mutableNode = entry.second;
            break;
        }
    }
    ASSERT_BUG(sp, mutableNode, StringView("Anonymous type is not owned by the expression being expanded"));
    auto& node = static_cast<Node&>(*mutableNode);
    ASSERT_BUG(sp, node.code, StringView("Anonymous type lost its body before extraction"));
    visit(node);
    ASSERT_BUG(sp, node.objPtr && node.objPathBase != HIRGenericPath(), StringView("Anonymous type extraction did not assign a path"));
}

auto ClosureExprVisitorExtract::extractReferencedNodeTypes(const Span& sp, const HIRType* type) -> void {
    visitTyWith(type, [this, &sp](const HIRType* candidate) {
        const auto* nodeType = candidate->opt_NodeType();
        if (!nodeType) {
            return false;
        }

        switch (nodeType->tag()) {
            case HIRTypeDataNodeType::TAG_Closure:
                extractReferencedNode(sp, nodeType->as_Closure());
                break;
            case HIRTypeDataNodeType::TAG_Generator:
                extractReferencedNode(sp, nodeType->as_Generator());
                break;
            case HIRTypeDataNodeType::TAG_Async:
                extractReferencedNode(sp, nodeType->as_Async());
                break;
        }
        return false;
    });
}

auto ClosureExprVisitorExtract::createParams(const Span& sp, const StaticTraitResolve& resolve, HIRGenericParams& params, HIRPathParams& constructorPathParams) const -> Monomorph {
    // TODO: How to get the bounds?

    return Monomorph(resolve, params, constructorPathParams);
}

#ifdef NEWNODE
    #undef NEWNODE
#endif
#define NEWNODE(TY, CLASS, ...) closureMkExprnodep(pool->make<HIRExprNode##CLASS>(__VA_ARGS__), TY)

auto ClosureExprVisitorExtract::visit(HIRExprNodeClosure& node) -> void {
    if (!node.code) {
        DEBUG(StringView("Already expanded (via consteval?)"));
        return;
    }

    const auto& sp = node.span();

    TRACE_FUNCTION_F(StringView("Extract closure - ") << node.resType << StringView(", track_caller=") << node.trackCaller);
    ASSERT_BUG(sp, node.objPath == HIRGenericPath(), StringView("Closure path already set? ") << node.objPath);

    ASSERT_BUG(sp, !isActive(&node), StringView("Cyclic closure type dependency"));
    ActiveNodeGuard activeGuard{activeNode, &node};

    HIRExprVisitorDef::visit(node);
    for (const auto& arg : node.args) {
        extractReferencedNodeTypes(sp, arg.second);
    }
    extractReferencedNodeTypes(sp, node.returnType);
    for (const auto bindingIdx : node.avuCache.localVars) {
        extractReferencedNodeTypes(sp, variableTypes[bindingIdx]);
    }
    for (const auto& binding : node.avuCache.capturedVars) {
        extractReferencedNodeTypes(sp, variableTypes[binding.rootSlot]);
    }
    const auto implCounts = out.saveCounts();

    if (node.cls == HIRExprNodeClosure::Class::Shared && resolve_.hirCrate().getLangItemPathOpt("fn").components().empty()) {
        node.cls = resolve_.hirCrate().getLangItemPathOpt("fn_mut").components().empty() ? HIRExprNodeClosure::Class::Once : HIRExprNodeClosure::Class::Mut;
    } else if (node.cls == HIRExprNodeClosure::Class::Mut && resolve_.hirCrate().getLangItemPathOpt("fn_mut").components().empty()) {
        node.cls = HIRExprNodeClosure::Class::Once;
    }

    // TODO: Fix up lifetimes somehow

    HIRGenericParams params;
    HIRPathParams constructorPathParams;
    // TODO: Don't create using all inputs, instead only use the parameters required by the body
    auto monomorphCb = createParams(sp, resolve_, params, constructorPathParams);

    Vector<const HIRType*> argsTyInner;
    for (const auto& arg : node.args) {
        DEBUG(StringView("> ARG ") << arg.second);
        argsTyInner.pushBack(monomorphCb.monomorphType(sp, arg.second));
    }
    const HIRType* argsTy = resolve_.hirCrate().types.tuple(mv$(argsTyInner));
    DEBUG(StringView("> Return type: ") << node.returnType);
    const HIRType* retType = monomorphCb.monomorphType(sp, node.returnType);

    DEBUG(StringView("args_ty = ") << argsTy << StringView(", ret_type = ") << retType);
    DEBUG(StringView("params = ") << params.fmtArgs());
    DEBUG(StringView("--- Mutate inner code"));
    ClosureExprVisitorMutate ev{pool, node.resType, node.avuCache.localVars, node.avuCache.capturedVars, monomorphCb};
    ev.visitNodePtr(node.code);

    DEBUG(StringView("--- Build locals and captures"));
    Vector<const HIRType*> localTypes;
    localTypes.pushBack(resolve_.hirCrate().types.infer());
    for (const auto bindingIdx : node.avuCache.localVars) {
        localTypes.pushBack(monomorphCb.monomorphType(sp, variableTypes[bindingIdx]));
    }
    std::vector<HIRVisEnt<const HIRType*>> captureTypes;
    std::vector<HIRExprNodeP> captureNodes;
    captureTypes.reserve(node.avuCache.capturedVars.size());
    captureNodes.reserve(node.avuCache.capturedVars.size());
    node.isCopy = true;
    for (const auto& binding : node.avuCache.capturedVars) {
        auto bindingType = binding.usage;

        const HIRType* tmpTy;
        const auto* capTyP = &variableTypes[binding.rootSlot];
        auto valNode = NEWNODE(*capTyP, Variable, sp, "", binding.rootSlot);
        for (const auto& n : binding.fields) {
            tmpTy = resolve_.getFieldType(sp, *capTyP, n);
            tmpTy = resolve_.expandAssociatedTypes(sp, tmpTy);
            capTyP = &tmpTy;
            if (n == "") {
                valNode = NEWNODE(*capTyP, Deref, sp, std::move(valNode));
            } else {
                valNode = NEWNODE(*capTyP, Field, sp, std::move(valNode), n);
            }
        }

        HIRBorrowType bt;
        const auto& capTy = *capTyP;
        auto tyMono = monomorphCb.monomorphType(sp, *capTyP);

        if (node.isUse && bindingType == HIRValueUsage::Move && !resolve_.typeIsCopy(sp, capTy)) {
            if (typeIsUseCloned(resolve_, sp, capTy)) {
                const auto& langClone = resolve_.hirCrate().getLangItemPath(sp, "clone");
                auto borrowTy = resolve_.hirCrate().types.borrow(HIRBorrowType::Shared, capTy);
                auto borrowNode = NEWNODE(borrowTy, Borrow, sp, HIRBorrowType::Shared, mv$(valNode));
                auto* cloneCall = pool->make<HIRExprNodeCallPath>(sp, HIRPath(capTy, HIRGenericPath(langClone), RcString("clone")), makeVec1(mv$(borrowNode)));
                valNode = closureMkExprnodep(cloneCall, capTy);
                cloneCall->cache.argTypes.clear();
                cloneCall->cache.argTypes.pushBack(borrowTy);
                cloneCall->cache.argTypes.pushBack(capTy);
            }
        }

        DEBUG(StringView("Binding _#") << binding.rootSlot << FMT_CB(ss, for (const auto& n : binding.fields) ss << StringView(".") << n) << StringView(" : ") << bindingType);
        DEBUG(capTy << StringView(" -> ") << tyMono);
        switch (bindingType) {
            case HIRValueUsage::Unknown:
                BUG(sp, StringView("ValueUsage::Unkown on ") << binding.rootSlot);
            case HIRValueUsage::Borrow:
                bt = HIRBorrowType::Shared;
                captureNodes.push_back(NEWNODE(resolve_.hirCrate().types.borrow(bt, capTy), Borrow, sp, bt, mv$(valNode)));
                tyMono = resolve_.hirCrate().types.borrow(bt, tyMono);
                break;
            case HIRValueUsage::Mutate:
                bt = HIRBorrowType::Unique;
                captureNodes.push_back(NEWNODE(resolve_.hirCrate().types.borrow(bt, capTy), Borrow, sp, bt, mv$(valNode)));
                tyMono = resolve_.hirCrate().types.borrow(bt, tyMono);
                break;
            case HIRValueUsage::Move:
                captureNodes.push_back(mv$(valNode));
                break;
        }
        captureTypes.push_back(HIRVisEnt<const HIRType*>{HIRPublicity::newNone(), mv$(tyMono)});
    }

    {
        ClosureExprVisitorFixup fixup{resolve_.board(), &params, monomorphCb, &out, true};
        for (size_t i = 0; i < captureTypes.size(); i++) {
            auto* tyMono = fixup.resolve_.expandAssociatedTypes(sp, captureTypes[i].ent);
            captureTypes[i].ent = fixup.visitType(tyMono);
        }
    }
    monomorphCb.addBounds(sp, resolve_);

    DEBUG(StringView("params = ") << params.fmtArgs() << params.fmtBounds());
    {
        StaticTraitResolve localResolve{resolve_.board()};
        localResolve.setImplGenericsRaw(MetadataType::None, params);

        for (const auto& v : captureTypes) {
            if (!localResolve.typeIsCopy(sp, v.ent)) {
                DEBUG(StringView("Non-copy capture: ") << v.ent);
                node.isCopy = false;
            }
            DEBUG(StringView("Copy closure"));
        }
    }

    auto implPathParams = params.makeNopParams(resolve_.hirCrate().types, 0);
    auto str = HIRStruct{params.clone(), HIRStruct::Repr::Rust, HIRStruct::Data::make_Tuple(mv$(captureTypes))};
    str.markings.isCopy = node.isCopy;
    HIRSimplePath closureStructPath;
    HIRTypeItem* closureStructPtr;
    std::tie(closureStructPath, closureStructPtr) = out.newType->create(CLOSURE_PATH_PREFIX, newTypeSuffix, mv$(str));
    auto& closureStructRef = closureStructPtr->as_Struct();

    node.objPtr = &closureStructRef;
    node.objPath = HIRGenericPath(closureStructPath, monomorphCb.freeze());
    node.objPathBase = node.objPath.clone();
    node.captures = mv$(captureNodes);
    DEBUG(StringView("-- Object name: ") << node.objPath);
    const HIRType* closureType = resolve_.hirCrate().types.path(HIRGenericPath(node.objPath.path.clone(), mv$(implPathParams)), HIRTypePathBinding::make_Struct(&closureStructRef));
    std::vector<HIRPattern> argsPatInner;
    for (const auto& arg : node.args) {
        argsPatInner.push_back(arg.first.clone());
        ev.visitPattern(sp, argsPatInner.back());
    }
    HIRPattern argsPat{HIRPatternBinding(), HIRPattern::Data::make_Tuple({mv$(argsPatInner)})};

    HIRExprPtr bodyCode{mv$(node.code)};
    bodyCode.bindings = mv$(localTypes);

    {
        DEBUG(StringView("-- Fixing types in body code"));
        ClosureExprVisitorFixup fixup{resolve_.board(), &params, monomorphCb, &out, true};
        fixup.visitRoot(bodyCode);

        DEBUG(StringView("-- Fixing types in signature"));
        argsTy = fixup.visitType(argsTy);
        retType = fixup.visitType(retType);
        // TODO: Replace erased types too
    }

    DEBUG(StringView("args_ty = ") << argsTy << StringView(", ret_type = ") << retType);
    const auto& langCopy = resolve_.hirCrate().getLangItemPathOpt("copy");
    if (node.isCopy && !langCopy.components().empty()) {
        auto& crate = resolve_.hirCrateMut();
        auto& v = crate.traitImpls[langCopy].getListForTypeMut(closureType);
        v.push_back(box$(
            HIRTraitImpl{
                params.clone(),
                {},
                closureType,
                {},
                {},
                {},
                {},
                /*source module*/ HIRSimplePath(resolve_.hirCrate().crateName, {})
            }
        ));
        crate.allTraitImpls[langCopy].getListForTypeMut(closureType).push_back(v.back().get());
    }

    HIRPathParams traitParams;
    traitParams.types.push_back(argsTy);
    switch (node.cls) {
        case HIRExprNodeClosure::Class::Unknown:
            node.cls = HIRExprNodeClosure::Class::NoCapture;
        case HIRExprNodeClosure::Class::NoCapture: {
            DEBUG(StringView("class=NoCapture"));

            struct H2 {
                static std::pair<HIRExprNodeClosure::Class, HIRTraitImpl> makeDispatch(HIRTypeInterner& types, ObjPool* pool, const Span& sp, HIRExprNodeClosure::Class c, HIRGenericParams params, HIRPathParams traitParams, const HIRType* closureType, const HIRType* argsTy, const HIRType* retType) {
                    const auto& argsTupInner = argsTy->as_Tuple();
                    std::vector<HIRExprNodeP> dispatchArgs;
                    Vector<const HIRType*> dispatchNodeArgsCache;
                    dispatchArgs.reserve(argsTupInner.length());
                    dispatchNodeArgsCache.grow(argsTupInner.length() + 1);
                    for (size_t i = 0; i < argsTupInner.length(); i++) {
                        const auto& ty = argsTupInner[i];
                        dispatchArgs.push_back(NEWNODE(ty, Field, sp, NEWNODE(argsTy, Variable, sp, RcString("arg"), 1), RcString::newInterned(FMT(i))));
                        dispatchNodeArgsCache.pushBack(ty);
                    }
                    dispatchNodeArgsCache.pushBack(retType);
                    auto path = HIRPath(closureType, RcString("call_free"));
                    path.data.as_UfcsInherent().implParams = closureType->as_Path().path.data.as_Generic().params.clone();
                    auto* dispatchCall = pool->make<HIRExprNodeCallPath>(sp, mv$(path), mv$(dispatchArgs));
                    HIRExprNodeP dispatchNode = closureMkExprnodep(dispatchCall, retType);
                    dispatchCall->cache.argTypes = mv$(dispatchNodeArgsCache);

                    auto argsArg = std::make_pair(HIRPattern{{false, HIRPatternBinding::Type::Move, RcString("args"), 1}, {}}, argsTy);
                    HIRTraitImpl fcn;
                    switch (c) {
                        case HIRExprNodeClosure::Class::Once:
                            fcn = H::makeFnonce(mv$(params), mv$(traitParams), closureType, mv$(argsArg), retType, mv$(dispatchNode));
                            break;
                        case HIRExprNodeClosure::Class::Mut:
                            fcn = H::makeFnmut(types, mv$(params), mv$(traitParams), closureType, mv$(argsArg), retType, mv$(dispatchNode));
                            break;
                        case HIRExprNodeClosure::Class::Shared:
                            fcn = H::makeFn(types, mv$(params), mv$(traitParams), closureType, mv$(argsArg), retType, mv$(dispatchNode));
                            break;
                        default:
                            UNREACHABLE();
                    }
                    return std::make_pair(c, mv$(fcn));
                }
            };

            if (!resolve_.hirCrate().getLangItemPathOpt("fn_once").components().empty()) {
                out.implsClosure.push_back(H2::makeDispatch(resolve_.hirCrate().types, pool, sp, HIRExprNodeClosure::Class::Once, params.clone(), traitParams.clone(), closureType, argsTy, retType));
            }
            if (!resolve_.hirCrate().getLangItemPathOpt("fn_mut").components().empty()) {
                out.implsClosure.push_back(H2::makeDispatch(resolve_.hirCrate().types, pool, sp, HIRExprNodeClosure::Class::Mut, params.clone(), traitParams.clone(), closureType, argsTy, retType));
            }
            if (!resolve_.hirCrate().getLangItemPathOpt("fn").components().empty()) {
                out.implsClosure.push_back(H2::makeDispatch(resolve_.hirCrate().types, pool, sp, HIRExprNodeClosure::Class::Shared, params.clone(), traitParams.clone(), closureType, argsTy, retType));
            }

            std::vector<std::pair<HIRPattern, const HIRType*>> argsSplit;
            argsSplit.reserve(node.args.size());
            for (size_t i = 0; i < node.args.size(); i++) {
                argsSplit.push_back(std::make_pair(mv$(argsPat.data.as_Tuple().subPatterns[i]), mv$(argsTy->as_Tuple()[i])));
            }
            out.implsType.push_back(box$(H::makeFnfree(resolve_.hirCrate().types, mv$(params), mv$(closureType), mv$(argsSplit), mv$(retType), std::move(bodyCode))));
        } break;
        case HIRExprNodeClosure::Class::Shared: {
            DEBUG(StringView("class=Shared"));
            const auto& langFn = resolve_.hirCrate().getLangItemPath(node.span(), "fn");
            const auto methodSelfTy = resolve_.hirCrate().types.borrow(HIRBorrowType::Shared, closureType);

            {
                auto* dispatchCall = pool->make<HIRExprNodeCallPath>(sp, HIRPath(closureType, HIRGenericPath(langFn, traitParams.clone()), RcString("call"), HIRPathParams()), makeVec2(NEWNODE(methodSelfTy, Borrow, sp, HIRBorrowType::Shared, NEWNODE(closureType, Variable, sp, RcString("self"), 0)), NEWNODE(argsTy, Variable, sp, RcString("arg"), 1)));
                auto dispatchNode = closureMkExprnodep(dispatchCall, retType);
                dispatchCall->cache.argTypes.clear();
                dispatchCall->cache.argTypes.pushBack(methodSelfTy);
                dispatchCall->cache.argTypes.pushBack(argsTy);
                dispatchCall->cache.argTypes.pushBack(retType);
                auto argsArg = std::make_pair(HIRPattern{{false, HIRPatternBinding::Type::Move, RcString("args"), 1}, {}}, argsTy);
                out.implsClosure.push_back(std::make_pair(HIRExprNodeClosure::Class::Once, H::makeFnonce(params.clone(), traitParams.clone(), closureType, mv$(argsArg), retType, mv$(dispatchNode))));
            }
            {
                auto selfTy = resolve_.hirCrate().types.borrow(HIRBorrowType::Unique, closureType);
                auto* dispatchCall = pool->make<HIRExprNodeCallPath>(sp, HIRPath(closureType, HIRGenericPath(langFn, traitParams.clone()), RcString("call"), HIRPathParams()), makeVec2(NEWNODE(methodSelfTy, Borrow, sp, HIRBorrowType::Shared, NEWNODE(closureType, Deref, sp, NEWNODE(mv$(selfTy), Variable, sp, RcString("self"), 0))), NEWNODE(argsTy, Variable, sp, RcString("arg"), 1)));
                auto dispatchNode = closureMkExprnodep(dispatchCall, retType);
                dispatchCall->cache.argTypes.clear();
                dispatchCall->cache.argTypes.pushBack(methodSelfTy);
                dispatchCall->cache.argTypes.pushBack(argsTy);
                dispatchCall->cache.argTypes.pushBack(retType);
                auto argsArg = std::make_pair(HIRPattern{{false, HIRPatternBinding::Type::Move, RcString("args"), 1}, {}}, argsTy);
                out.implsClosure.push_back(std::make_pair(HIRExprNodeClosure::Class::Mut, H::makeFnmut(resolve_.hirCrate().types, params.clone(), traitParams.clone(), closureType, mv$(argsArg), retType, mv$(dispatchNode))));
            }

            out.implsClosure.push_back(std::make_pair(HIRExprNodeClosure::Class::Shared, H::makeFn(resolve_.hirCrate().types, mv$(params), mv$(traitParams), mv$(closureType), std::make_pair(mv$(argsPat), mv$(argsTy)), mv$(retType), mv$(bodyCode))));
        } break;
        case HIRExprNodeClosure::Class::Mut: {
            DEBUG(StringView("class=Mut"));
            const auto& langFnMut = resolve_.hirCrate().getLangItemPath(node.span(), "fn_mut");
            const auto methodSelfTy = resolve_.hirCrate().types.borrow(HIRBorrowType::Unique, closureType);

            {
                auto* dispatchCall = pool->make<HIRExprNodeCallPath>(sp, HIRPath(closureType, HIRGenericPath(langFnMut, traitParams.clone()), RcString("call_mut"), HIRPathParams()), makeVec2(NEWNODE(methodSelfTy, Borrow, sp, HIRBorrowType::Unique, NEWNODE(closureType, Variable, sp, RcString("self"), 0)), NEWNODE(argsTy, Variable, sp, RcString("arg"), 1)));
                auto dispatchNode = closureMkExprnodep(dispatchCall, retType);
                dispatchCall->cache.argTypes.clear();
                dispatchCall->cache.argTypes.pushBack(methodSelfTy);
                dispatchCall->cache.argTypes.pushBack(argsTy);
                dispatchCall->cache.argTypes.pushBack(retType);
                auto argsArg = std::make_pair(HIRPattern{{false, HIRPatternBinding::Type::Move, RcString("args"), 1}, {}}, argsTy);
                out.implsClosure.push_back(std::make_pair(HIRExprNodeClosure::Class::Once, H::makeFnonce(params.clone(), traitParams.clone(), closureType, mv$(argsArg), retType, mv$(dispatchNode))));
            }

            out.implsClosure.push_back(std::make_pair(HIRExprNodeClosure::Class::Mut, H::makeFnmut(resolve_.hirCrate().types, mv$(params), mv$(traitParams), mv$(closureType), std::make_pair(mv$(argsPat), mv$(argsTy)), mv$(retType), mv$(bodyCode))));
        } break;
        case HIRExprNodeClosure::Class::Once:
            DEBUG(StringView("class=Once"));
            out.implsClosure.push_back(std::make_pair(HIRExprNodeClosure::Class::Once, H::makeFnonce(mv$(params), mv$(traitParams), mv$(closureType), std::make_pair(mv$(argsPat), mv$(argsTy)), mv$(retType), mv$(bodyCode))));
            break;
    }

    for (size_t i = implCounts.closure; i < out.implsClosure.size(); i++) {
        auto& ti = out.implsClosure[i];
        for (auto& m : ti.second.methods) {
            m.second.data.markings.trackCaller = node.trackCaller;
            if (!m.second.data.code.state) {
                m.second.data.code.state = exprPtr.state.clone(pool);
            }
        }
    }
    for (size_t i = implCounts.type; i < out.implsType.size(); i++) {
        auto& ti = out.implsType[i];
        for (auto& m : ti->methods) {
            m.second.data.markings.trackCaller = node.trackCaller;
            if (!m.second.data.code.state) {
                m.second.data.code.state = exprPtr.state.clone(pool);
            }
        }
    }
    deferFixups(constructorPathParams, implCounts, closureStructRef);
}

#undef NEWNODE

auto ClosureExprVisitorExtract::setStateType(const Span& sp, CrVars& vars, const HIRType* stateType) const -> void {
    const auto& langMaybeUninit = resolve_.hirCrate().getLangItemPath(sp, "maybe_uninit");
    const auto& unmMaybeUninit = resolve_.hirCrate().getUnionByPath(sp, langMaybeUninit);
    auto wrapped = resolve_.hirCrate().types.path(HIRGenericPath(langMaybeUninit, HIRPathParams(stateType)), &unmMaybeUninit);
    vars.structEnts.insert(vars.structEnts.begin(), HIRVisEnt<const HIRType*>{HIRPublicity::newNone(), wrapped});
}

#ifdef NEWNODE
    #undef NEWNODE
#endif
#define NEWNODE(TY, CLASS, ...) closureMkExprnodep(pool->make<HIRExprNode##CLASS>(__VA_ARGS__), TY)

auto ClosureExprVisitorExtract::coroutineVars(const Span& sp, const HIRExprNodeGenerator::AvuCache& avuCache, unsigned nArgs, const Monomorph& monomorphCb, bool useClone) const -> CrVars {
    CrVars rv;
    size_t nCaps = avuCache.capturedVars.length();
    size_t nLocals = avuCache.localVars.length();
    rv.nArgs = nArgs;
    rv.captureUsages.grow(nCaps);
    rv.newLocals.grow(nArgs + nCaps + nLocals);
    rv.structEnts.reserve(1 + nCaps);
    rv.captureNodes.reserve(nCaps);

    for (const auto& cap : avuCache.capturedVars) {
        unsigned index = nArgs + rv.newLocals.length();
        rv.variableRewrites.insert(std::make_pair(cap.first, index));
        rv.newLocals.pushBack(monomorphCb.monomorphType(sp, variableTypes[cap.first]));

        rv.captureUsages.pushBack(cap.second);
        auto sourceCapTy = variableTypes[cap.first];
        auto storedCapTy = monomorphCb.monomorphType(sp, sourceCapTy);
        rv.structEnts.push_back(HIRVisEnt<const HIRType*>{HIRPublicity::newNone(), storedCapTy});
        rv.captureNodes.push_back(HIRExprNodeP(pool->make<HIRExprNodeVariable>(sp, "", cap.first)));
        switch (cap.second) {
            case HIRValueUsage::Unknown:
                BUG(sp, StringView("Unexpected ValueUsage::Unknown on #") << cap.first);
            case HIRValueUsage::Move: {
                if (useClone && !resolve_.typeIsCopy(sp, sourceCapTy) && typeIsUseCloned(resolve_, sp, sourceCapTy)) {
                    rv.captureNodes.back()->resType = sourceCapTy;
                    const auto& langClone = resolve_.hirCrate().getLangItemPath(sp, "clone");
                    auto borrowTy = resolve_.hirCrate().types.borrow(HIRBorrowType::Shared, sourceCapTy);
                    auto borrowNode = NEWNODE(borrowTy, Borrow, sp, HIRBorrowType::Shared, std::move(rv.captureNodes.back()));
                    auto* cloneCall = pool->make<HIRExprNodeCallPath>(sp, HIRPath(sourceCapTy, HIRGenericPath(langClone), RcString("clone")), makeVec1(mv$(borrowNode)));
                    cloneCall->cache.argTypes.clear();
                    cloneCall->cache.argTypes.pushBack(borrowTy);
                    cloneCall->cache.argTypes.pushBack(sourceCapTy);
                    rv.captureNodes.back() = closureMkExprnodep(cloneCall, sourceCapTy);
                }
            } break;
            case HIRValueUsage::Borrow:
                rv.captureNodes.back()->resType = sourceCapTy;
                sourceCapTy = resolve_.hirCrate().types.borrow(HIRBorrowType::Shared, sourceCapTy);
                rv.structEnts.back().ent = resolve_.hirCrate().types.borrow(HIRBorrowType::Shared, rv.structEnts.back().ent);
                rv.captureNodes.back() = HIRExprNodeP(pool->make<HIRExprNodeBorrow>(sp, HIRBorrowType::Shared, std::move(rv.captureNodes.back())));
                break;
            case HIRValueUsage::Mutate:
                rv.captureNodes.back()->resType = sourceCapTy;
                sourceCapTy = resolve_.hirCrate().types.borrow(HIRBorrowType::Unique, sourceCapTy);
                rv.structEnts.back().ent = resolve_.hirCrate().types.borrow(HIRBorrowType::Unique, rv.structEnts.back().ent);
                rv.captureNodes.back() = HIRExprNodeP(pool->make<HIRExprNodeBorrow>(sp, HIRBorrowType::Unique, std::move(rv.captureNodes.back())));
                break;
        }
        rv.captureNodes.back()->resType = mv$(sourceCapTy);
    }
    for (const auto& slot : avuCache.localVars) {
        unsigned index = nArgs + rv.newLocals.length();
        rv.variableRewrites.insert(std::make_pair(slot, index));
        rv.newLocals.pushBack(monomorphCb.monomorphType(sp, variableTypes[slot]));
    }
    return rv;
}

#undef NEWNODE

auto ClosureExprVisitorExtract::fixCoroutineVarTypes(const Span& sp, const HIRGenericParams& params, const Monomorph& monomorphCb, CrVars& vars) const -> void {
    ClosureExprVisitorFixup fixup{resolve_.board(), &params, monomorphCb, &out, true};
    for (auto& type : mutRange(vars.newLocals)) {
        type = fixup.resolve_.expandAssociatedTypes(sp, type);
        type = fixup.visitType(type);
    }
    for (auto& field : vars.structEnts) {
        field.ent = fixup.resolve_.expandAssociatedTypes(sp, field.ent);
        field.ent = fixup.visitType(field.ent);
    }
}

auto ClosureExprVisitorExtract::countCoroutineSuspensions(HIRExprNodeP& code, bool countYields, bool countAwaits) const -> unsigned {
    struct Visitor: public HIRExprVisitorDef {
        bool countYields;
        bool countAwaits;
        unsigned count = 0;

        Visitor(HIRTypeInterner& types, bool countYields, bool countAwaits)
            : HIRExprVisitorDef(types)
            , countYields(countYields)
            , countAwaits(countAwaits)
        {
        }

        void visit(HIRExprNodeClosure&) override {
        }

        void visit(HIRExprNodeGenerator&) override {
        }

        void visit(HIRExprNodeAsyncBlock&) override {
        }

        void visit(HIRExprNodeYield& node) override {
            if (countYields) {
                count += 1;
            }
            HIRExprVisitorDef::visit(node);
        }

        void visit(HIRExprNodeAWait& node) override {
            if (countAwaits) {
                count += 1;
            }
            HIRExprVisitorDef::visit(node);
        }
    } visitor(resolve_.hirCrate().types, countYields, countAwaits);

    visitor.visitNodePtr(code);
    return visitor.count;
}

auto ClosureExprVisitorExtract::makeCoroutineStateEnum(unsigned suspensionCount) -> HIREnum {
    auto stateEnum = HIREnum{HIRGenericParams(), false, HIREnum::Repr(), HIREnum::Class::make_Value({})};
    auto& variants = stateEnum.data.as_Value().variants;
    variants.reserve(static_cast<size_t>(suspensionCount) + 3);
    variants.push_back(HIREnum::ValueVariant{RcString::newInterned("UNRESUMED"), HIRExprPtr(), U128(0)});
    variants.push_back(HIREnum::ValueVariant{RcString::newInterned("RETURNED"), HIRExprPtr(), U128(1)});
    variants.push_back(HIREnum::ValueVariant{RcString::newInterned("POISONED"), HIRExprPtr(), U128(2)});
    for (unsigned i = 0; i < suspensionCount; i++) {
        variants.push_back(HIREnum::ValueVariant{RcString(), HIRExprPtr(), U128(static_cast<u64>(i) + 3)});
    }
    return stateEnum;
}

auto ClosureExprVisitorExtract::visit(HIRExprNodeGenerator& node) -> void {
    if (!node.code) {
        return;
    }

    const auto& sp = node.span();

    TRACE_FUNCTION_F(StringView("Extract generator - ") << node.resType);
    ASSERT_BUG(sp, !isActive(&node), StringView("Cyclic generator type dependency"));
    ActiveNodeGuard activeGuard{activeNode, &node};

    HIRExprVisitorDef::visit(node);
    extractReferencedNodeTypes(sp, node.resumeTy);
    extractReferencedNodeTypes(sp, node.yieldTy);
    extractReferencedNodeTypes(sp, node.returnType);
    for (const auto slot : node.avuCache.localVars) {
        extractReferencedNodeTypes(sp, variableTypes[slot]);
    }
    for (const auto& capture : node.avuCache.capturedVars) {
        extractReferencedNodeTypes(sp, variableTypes[capture.first]);
    }
    const auto implCounts = out.saveCounts();

    HIRGenericParams params;
    HIRPathParams constructorPathParams;
    auto monomorphCb = createParams(sp, resolve_, params, constructorPathParams);

    auto resumeTy = monomorphCb.monomorphType(sp, node.resumeTy);
    auto yieldTy = monomorphCb.monomorphType(sp, node.yieldTy);
    auto returnTy = monomorphCb.monomorphType(sp, node.returnType);

    auto crVars = coroutineVars(node.span(), node.avuCache, 2, monomorphCb);
    fixCoroutineVarTypes(sp, params, monomorphCb, crVars);

    {
        TRACE_FUNCTION_F(StringView("-- Rewrite variables"));
        ExprVisitorGeneratorRewrite visitorRewrite(monomorphCb, crVars.variableRewrites);
        if (node.hasResumePattern) {
            visitorRewrite.visitPattern(sp, node.resumePattern);
        }
        visitorRewrite.visitNodePtr(node.code);
    }

    monomorphCb.addBounds(sp, resolve_);

    auto suspensionCount = countCoroutineSuspensions(node.code, true, false);
    auto stateIdxType = out.newType->create("gen_state_idx#", newTypeSuffix, makeCoroutineStateEnum(suspensionCount));
    auto stateIdxTy = resolve_.hirCrate().types.path(stateIdxType.first, &stateIdxType.second->as_Enum());

    auto stateStr = HIRStruct{params.clone(), HIRStruct::Repr::Rust, HIRStruct::Data::make_Tuple({})};
    stateStr.data.as_Tuple().push_back(HIRVisEnt<const HIRType*>{HIRPublicity::newNone(), stateIdxTy});
    HIRSimplePath stateStructPath;
    HIRTypeItem* stateStructPtr;
    std::tie(stateStructPath, stateStructPtr) = out.newType->create("gen_state#", newTypeSuffix, std::move(stateStr));
    auto stateType = resolve_.hirCrate().types.path(HIRGenericPath(stateStructPath, params.makeNopParams(resolve_.hirCrate().types, 0)), &stateStructPtr->as_Struct());
    DEBUG(StringView("state_type = ") << stateType);
    setStateType(sp, crVars, stateType);

    auto genStr = HIRStruct{params.clone(), HIRStruct::Repr::Rust, HIRStruct::Data::make_Tuple(mv$(crVars.structEnts))};
    genStr.markings.hasDropImpl = true;
    HIRSimplePath genStructPath;
    HIRTypeItem* genStructPtr;
    std::tie(genStructPath, genStructPtr) = out.newType->create(GENERATOR_PATH_PREFIX, newTypeSuffix, mv$(genStr));
    auto& genStructRef = genStructPtr->as_Struct();

    DEBUG(genStructPath << StringView(" -> args=") << params.fmtArgs() << StringView(" where ") << params.fmtBounds());
    node.objPtr = &genStructRef;
    node.objPath = HIRGenericPath(genStructPath, monomorphCb.freeze());
    node.objPathBase = node.objPath.clone();
    node.captures = std::move(crVars.captureNodes);
    node.stateDataType = resolve_.hirCrate().types.path(HIRGenericPath(stateStructPath, node.objPath.params.clone()), &stateStructPtr->as_Struct());

    auto langPin = resolve_.hirCrate().getLangItemPath(sp, "pin");
    auto langGeneratorState = resolve_.hirCrate().getLangItemPath(sp, "coroutine_state");

    auto selfArgTy = resolve_.hirCrate().types.path(HIRGenericPath(genStructPath, params.makeNopParams(resolve_.hirCrate().types, 0)), &genStructRef);
    selfArgTy = resolve_.hirCrate().types.borrow(HIRBorrowType::Unique, selfArgTy);
    Vector<const HIRType*> resumeArgs;
    resumeArgs.pushBack(selfArgTy);
    resumeArgs.pushBack(resumeTy);
    selfArgTy = resolve_.hirCrate().types.path(HIRGenericPath(langPin, HIRPathParams(selfArgTy)), &resolve_.hirCrate().getStructByPath(sp, langPin));
    resumeArgs.mut(0) = selfArgTy;
    crVars.setArguments(sp, std::move(resumeArgs));

    auto bodyNode = std::move(node.code);
    {
        DEBUG(StringView("-- Fixing types in body code"));
        ClosureExprVisitorFixup fixup{resolve_.board(), &params, monomorphCb, &out, true};
        fixup.visitNodePtr(bodyNode);
    }
    if (node.hasResumePattern) {
        auto* resumeValue = pool->make<HIRExprNodeVariable>(sp, RcString::newInterned("resume"), 1);
        resumeValue->resType = resumeTy;
        auto* initialiseResume = pool->make<HIRExprNodeLet>(sp, std::move(node.resumePattern), resumeTy, HIRExprNodeP(resumeValue));
        initialiseResume->resType = resolve_.hirCrate().types.unit();

        if (auto* block = cast<HIRExprNodeBlock>(bodyNode.get())) {
            block->nodes.insert(block->nodes.begin(), HIRExprNodeP(initialiseResume));
        } else {
            auto* wrappedBlock = pool->make<HIRExprNodeBlock>(sp);
            wrappedBlock->nodes.push_back(HIRExprNodeP(initialiseResume));
            wrappedBlock->valueNode = std::move(bodyNode);
            wrappedBlock->resType = wrappedBlock->valueNode->resType;
            bodyNode.reset(wrappedBlock);
        }
    }

    HIRFunction* fcnDropPtr;
    {
        HIRFunction fcnDrop;
        fcnDrop.receiver = HIRFunction::Receiver::BorrowUnique;
        auto dropSelfArgTy = resolve_.hirCrate().types.path(HIRGenericPath(genStructPath, params.makeNopParams(resolve_.hirCrate().types, 0)), &genStructRef);
        dropSelfArgTy = resolve_.hirCrate().types.borrow(HIRBorrowType::Unique, dropSelfArgTy);
        fcnDrop.args.push_back(std::make_pair(HIRPattern(), mv$(dropSelfArgTy)));
        fcnDrop.returnType = resolve_.hirCrate().types.unit();
        fcnDrop.code.reset(pool->make<HIRExprNodeTuple>(sp, std::vector<HIRExprNodeP>{}));
        fcnDrop.code->resType = resolve_.hirCrate().types.unit();
        fcnDrop.code.state = exprPtr.state.clone(pool);
        HIRTraitImpl dropImpl;
        dropImpl.params = params.clone();
        dropImpl.type = resolve_.hirCrate().types.path(HIRGenericPath(genStructPath, params.makeNopParams(resolve_.hirCrate().types, 0)), &genStructRef);
        dropImpl.methods.insert(std::make_pair(RcString::newInterned("drop"), HIRTraitImpl::ImplEnt<HIRFunction>{false, std::move(fcnDrop)}));
        fcnDropPtr = &dropImpl.methods.at("drop").data;
        out.traitImpls.push_back(std::make_pair("drop", std::move(dropImpl)));
    }

    HIRFunction fcnResume;
    fcnResume.markings.trackCaller = node.trackCaller;
    fcnResume.receiver = HIRFunction::Receiver::Custom;
    fcnResume.receiverType = selfArgTy;
    fcnResume.args.push_back(std::make_pair(HIRPattern(), selfArgTy));
    fcnResume.args.push_back(std::make_pair(node.hasResumePattern ? HIRPattern{HIRPatternBinding{false, HIRPatternBinding::Type::Move, RcString::newInterned("resume"), 1}, HIRPattern::Data::make_Any({})} : HIRPattern(), resumeTy));
    node.hasResumePattern = false;
    HIRPathParams retParams;
    retParams.types.push_back(yieldTy);
    retParams.types.push_back(returnTy);
    fcnResume.returnType = resolve_.hirCrate().types.path(HIRGenericPath(langGeneratorState, std::move(retParams)), &resolve_.hirCrate().getEnumByPath(sp, langGeneratorState));
    auto* v = pool->make<HIRExprNodeGeneratorWrapper>(sp, returnTy, yieldTy, mv$(bodyNode), false);
    v->captureUsages = std::move(crVars.captureUsages);
    v->resType = fcnResume.returnType;
    v->objPtr = node.objPtr;
    v->stateDataType = mv$(stateType);
    v->stateIdxEnum = mv$(stateIdxType.first);
    v->dropFcnPtr = fcnDropPtr;
    bodyNode.reset(v);
    fcnResume.code.reset(bodyNode.release());
    fcnResume.code.state = exprPtr.state.clone(pool);
    fcnResume.code.bindings = std::move(crVars.newLocals);

    HIRTraitImpl impl;
    impl.traitArgs.types.push_back(resumeTy);
    impl.type = resolve_.hirCrate().types.path(HIRGenericPath(genStructPath, params.makeNopParams(resolve_.hirCrate().types, 0)), &genStructRef);
    impl.types.insert(std::make_pair(RcString::newInterned("Yield"), HIRTraitImpl::ImplEnt<const HIRType*>{false, yieldTy}));
    impl.types.insert(std::make_pair(RcString::newInterned("Return"), HIRTraitImpl::ImplEnt<const HIRType*>{false, returnTy}));
    impl.methods.insert(std::make_pair(RcString::newInterned("resume"), HIRTraitImpl::ImplEnt<HIRFunction>{false, std::move(fcnResume)}));
    impl.params = std::move(params);
    out.traitImpls.push_back(std::make_pair("coroutine", std::move(impl)));
    deferFixups(constructorPathParams, implCounts, genStructRef);
}

auto ClosureExprVisitorExtract::visit(HIRExprNodeAsyncBlock& node) -> void {
    if (!node.code) {
        DEBUG(StringView("Already expanded (via consteval?)"));
        return;
    }

    const auto& sp = node.span();
    const bool isAsyncDropGlue = isAsyncDropIntrinsic;
    isAsyncDropIntrinsic = false;

    TRACE_FUNCTION_F(StringView("Extract async - ") << node.resType);
    ASSERT_BUG(sp, !isActive(&node), StringView("Cyclic async block type dependency"));
    ActiveNodeGuard activeGuard{activeNode, &node};

    HIRExprVisitorDef::visit(node);
    extractReferencedNodeTypes(sp, node.returnType);
    if (node.isAsyncGen) {
        extractReferencedNodeTypes(sp, node.yieldTy);
    }
    for (const auto slot : node.avuCache.localVars) {
        extractReferencedNodeTypes(sp, variableTypes[slot]);
    }
    for (const auto& capture : node.avuCache.capturedVars) {
        extractReferencedNodeTypes(sp, variableTypes[capture.first]);
    }
    const auto implCounts = out.saveCounts();

    HIRGenericParams params;
    HIRPathParams constructorPathParams;
    auto monomorphCb = createParams(sp, resolve_, params, constructorPathParams);

    auto returnTy = monomorphCb.monomorphType(sp, node.returnType);
    auto itemTy = node.isAsyncGen ? monomorphCb.monomorphType(sp, node.yieldTy) : nullptr;

    auto crVars = coroutineVars(node.span(), node.avuCache, 2, monomorphCb, node.isUse);
    fixCoroutineVarTypes(sp, params, monomorphCb, crVars);

    {
        ExprVisitorGeneratorRewrite visitorRewrite(monomorphCb, crVars.variableRewrites);
        visitorRewrite.visitNodePtr(node.code);
    }

    monomorphCb.addBounds(sp, resolve_);

    auto suspensionCount = countCoroutineSuspensions(node.code, node.isAsyncGen, true);
    auto stateIdxType = out.newType->create("async_state_idx#", newTypeSuffix, makeCoroutineStateEnum(suspensionCount));
    auto stateIdxTy = resolve_.hirCrate().types.path(stateIdxType.first, &stateIdxType.second->as_Enum());

    auto stateStr = HIRStruct{params.clone(), HIRStruct::Repr::Rust, HIRStruct::Data::make_Tuple({})};
    stateStr.data.as_Tuple().push_back(HIRVisEnt<const HIRType*>{HIRPublicity::newNone(), stateIdxTy});
    HIRSimplePath stateStructPath;
    HIRTypeItem* stateStructPtr;
    std::tie(stateStructPath, stateStructPtr) = out.newType->create("async_state#", newTypeSuffix, std::move(stateStr));
    auto stateType = resolve_.hirCrate().types.path(HIRGenericPath(stateStructPath, params.makeNopParams(resolve_.hirCrate().types, 0)), &stateStructPtr->as_Struct());

    setStateType(sp, crVars, stateType);

    auto genStr = HIRStruct{params.clone(), HIRStruct::Repr::Rust, HIRStruct::Data::make_Tuple(std::move(crVars.structEnts))};
    genStr.markings.hasDropImpl = true;
    genStr.structMarkings.isAsyncDropGlue = isAsyncDropGlue;
    HIRSimplePath genStructPath;
    HIRTypeItem* genStructPtr;
    std::tie(genStructPath, genStructPtr) = out.newType->create(PATH_PREFIX_FUTURE, newTypeSuffix, mv$(genStr));
    auto& genStructRef = genStructPtr->as_Struct();

    DEBUG(genStructPath << StringView(" -> args=") << params.fmtArgs() << StringView(" where ") << params.fmtBounds());
    node.objPtr = &genStructRef;
    node.objPath = HIRGenericPath(genStructPath, monomorphCb.freeze());
    node.objPathBase = node.objPath.clone();
    node.captures = std::move(crVars.captureNodes);
    node.stateDataType = resolve_.hirCrate().types.path(HIRGenericPath(stateStructPath, node.objPath.params.clone()), &stateStructPtr->as_Struct());

    auto selfArgTy = resolve_.hirCrate().types.path(HIRGenericPath(genStructPath, params.makeNopParams(resolve_.hirCrate().types, 0)), &genStructRef);
    selfArgTy = resolve_.hirCrate().types.borrow(HIRBorrowType::Unique, selfArgTy);
    auto langPin = resolve_.hirCrate().getLangItemPath(sp, "pin");
    selfArgTy = resolve_.hirCrate().types.path(HIRGenericPath(langPin, HIRPathParams(selfArgTy)), &resolve_.hirCrate().getStructByPath(sp, langPin));

    auto langContext = resolve_.hirCrate().getLangItemPath(sp, "Context");
    auto contextArgTy = resolve_.hirCrate().types.borrow(HIRBorrowType::Unique, resolve_.hirCrate().types.path(HIRGenericPath(langContext, HIRPathParams()), &resolve_.hirCrate().getStructByPath(sp, langContext)));
    Vector<const HIRType*> arguments;
    arguments.pushBack(selfArgTy);
    arguments.pushBack(contextArgTy);
    crVars.setArguments(sp, mv$(arguments));

    auto bodyNode = std::move(node.code);
    {
        DEBUG(StringView("-- Fixing types in body code"));
        ClosureExprVisitorFixup fixup{resolve_.board(), &params, monomorphCb, &out, true};
        fixup.visitNodePtr(bodyNode);
    }

    HIRFunction* fcnDropPtr;
    {
        HIRFunction fcnDrop;
        fcnDrop.receiver = HIRFunction::Receiver::BorrowUnique;
        auto dropSelfArgTy = resolve_.hirCrate().types.path(HIRGenericPath(genStructPath, params.makeNopParams(resolve_.hirCrate().types, 0)), &genStructRef);
        dropSelfArgTy = resolve_.hirCrate().types.borrow(HIRBorrowType::Unique, dropSelfArgTy);
        fcnDrop.args.push_back(std::make_pair(HIRPattern(), mv$(dropSelfArgTy)));
        fcnDrop.returnType = resolve_.hirCrate().types.unit();
        fcnDrop.code.reset(pool->make<HIRExprNodeTuple>(sp, std::vector<HIRExprNodeP>{}));
        fcnDrop.code->resType = resolve_.hirCrate().types.unit();
        fcnDrop.code.state = exprPtr.state.clone(pool);
        HIRTraitImpl dropImpl;
        dropImpl.params = params.clone();
        dropImpl.type = resolve_.hirCrate().types.path(HIRGenericPath(genStructPath, params.makeNopParams(resolve_.hirCrate().types, 0)), &genStructRef);
        dropImpl.methods.insert(std::make_pair(RcString::newInterned("drop"), HIRTraitImpl::ImplEnt<HIRFunction>{false, std::move(fcnDrop)}));
        fcnDropPtr = &dropImpl.methods.at("drop").data;
        out.traitImpls.push_back(std::make_pair("drop", std::move(dropImpl)));
    }

    HIRFunction fcnResume;
    fcnResume.receiver = HIRFunction::Receiver::Custom;
    fcnResume.receiverType = selfArgTy;
    fcnResume.args.push_back(std::make_pair(HIRPattern(), selfArgTy));
    fcnResume.args.push_back(std::make_pair(HIRPattern(), contextArgTy));
    HIRPathParams retParams;
    auto langPoll = resolve_.hirCrate().getLangItemPath(sp, "Poll");
    if (node.isAsyncGen) {
        auto langOption = resolve_.hirCrate().getLangItemPath(sp, "Option");
        retParams.types.push_back(resolve_.hirCrate().types.path(HIRGenericPath(langOption, HIRPathParams(itemTy)), &resolve_.hirCrate().getEnumByPath(sp, langOption)));
    } else {
        retParams.types.push_back(returnTy);
    }
    fcnResume.returnType = resolve_.hirCrate().types.path(HIRGenericPath(langPoll, std::move(retParams)), &resolve_.hirCrate().getEnumByPath(sp, langPoll));
    auto* v = pool->make<HIRExprNodeGeneratorWrapper>(sp, returnTy, node.isAsyncGen ? itemTy : resolve_.hirCrate().types.unit(), std::move(bodyNode), true);
    v->isAsyncGen = node.isAsyncGen;
    v->captureUsages = std::move(crVars.captureUsages);
    v->resType = fcnResume.returnType;
    v->objPtr = node.objPtr;
    v->stateDataType = mv$(stateType);
    v->stateIdxEnum = mv$(stateIdxType.first);
    v->dropFcnPtr = fcnDropPtr;
    fcnResume.code.reset(v);
    fcnResume.code.state = exprPtr.state.clone(pool);
    fcnResume.code.bindings = std::move(crVars.newLocals);

    HIRTraitImpl impl;
    impl.type = resolve_.hirCrate().types.path(HIRGenericPath(genStructPath, params.makeNopParams(resolve_.hirCrate().types, 0)), &genStructRef);
    if (node.isAsyncGen) {
        impl.types.insert(std::make_pair(RcString::newInterned("Item"), HIRTraitImpl::ImplEnt<const HIRType*>{false, itemTy}));
        impl.methods.insert(std::make_pair(RcString::newInterned("poll_next"), HIRTraitImpl::ImplEnt<HIRFunction>{false, std::move(fcnResume)}));
        impl.params = std::move(params);
        out.traitImpls.push_back(std::make_pair("async_iterator", std::move(impl)));
        deferFixups(constructorPathParams, implCounts, genStructRef);
        return;
    }
    impl.types.insert(std::make_pair(RcString::newInterned("Output"), HIRTraitImpl::ImplEnt<const HIRType*>{false, returnTy}));
    impl.methods.insert(std::make_pair(RcString::newInterned("poll"), HIRTraitImpl::ImplEnt<HIRFunction>{false, std::move(fcnResume)}));
    impl.params = std::move(params);
    out.traitImpls.push_back(std::make_pair("future_trait", std::move(impl)));
    deferFixups(constructorPathParams, implCounts, genStructRef);
}

auto ClosureExprVisitorExtract::visitPattern(const Span& sp, HIRPattern& pat) -> void {
}

auto ClosureExprVisitorExtract::visit(HIRExprNodeLoop& node) -> void {
    HIRExprVisitorDef::visit(node);
}

auto ClosureExprVisitorExtract::visit(HIRExprNodeYield& node) -> void {
    HIRExprVisitorDef::visit(node);
}

ClosureExprVisitorExtract::ActiveNodeGuard::ActiveNodeGuard(const ActiveNode*& head, const void* node)
    : head(head)
    , entry{node, head} {
    head = &entry;
}

ClosureExprVisitorExtract::ActiveNodeGuard::~ActiveNodeGuard()
{
    head = entry.parent;
}

ClosureExprVisitorExtract::FrozenMonomorph::FrozenMonomorph(HIRTypeInterner& types, const HIRPathParams& sourceParams)
    : MonomorphiserNop(types)
    , sourceParams(sourceParams)
{
}

auto ClosureExprVisitorExtract::FrozenMonomorph::getType(const Span& sp, const HIRGenericRef& generic) const -> const HIRType* {
    for (size_t i = 0; i < sourceParams.types.size(); i++) {
        const auto* source = sourceParams.types[i]->opt_Generic();
        if (source && *source == generic) {
            return types.generic(generic.name, i);
        }
    }
    return MonomorphiserNop::getType(sp, generic);
}

auto ClosureExprVisitorExtract::FrozenMonomorph::getValue(const Span& sp, const HIRGenericRef& generic) const -> HIRConstGeneric {
    for (size_t i = 0; i < sourceParams.values.size(); i++) {
        const auto* source = sourceParams.values[i].opt_Generic();
        if (source && *source == generic) {
            return HIRGenericRef(generic.name, i);
        }
    }
    return MonomorphiserNop::getValue(sp, generic);
}

ClosureExprVisitorExtract::DeferredExprFixup::DeferredExprFixup(const Monomorphiser& monomorphiser)
    : HIRExprVisitorDef(monomorphiser.typeInterner())
    , monomorphiser(monomorphiser)
{
}

auto ClosureExprVisitorExtract::DeferredExprFixup::visitRoot(HIRExprPtr& root) -> void {
    root->visit(*this);
    root->resType = visitType(root->resType);
    for (auto& type : mutRange(root.bindings)) {
        type = visitType(type);
    }
    for (auto& type : mutRange(root.erasedTypes)) {
        type = visitType(type);
    }
}

auto ClosureExprVisitorExtract::DeferredExprFixup::visitType(const HIRType* type) -> const HIRType* {
    AnonymousTypeMonomorph fixup{monomorphiser, false};
    return fixup.monomorphType(Span(), type, true);
}

ClosureExprVisitorExtract::DeferredItemFixup::DeferredItemFixup(const Monomorphiser& monomorphiser)
    : HIRVisitor(nullptr, monomorphiser.typeInterner())
    , monomorphiser(monomorphiser)
{
}

auto ClosureExprVisitorExtract::DeferredItemFixup::visitType(const HIRType* type) -> const HIRType* {
    AnonymousTypeMonomorph fixup{monomorphiser, false};
    return fixup.monomorphType(Span(), type, true);
}

auto ClosureExprVisitorExtract::DeferredItemFixup::visitExpr(HIRExprPtr& expr) -> void {
    if (expr) {
        DeferredExprFixup fixup{monomorphiser};
        fixup.visitRoot(expr);
    }
}

auto ClosureExprVisitorExtract::DeferredItemFixup::visitGeneratedStruct(HIRStruct& item) -> void {
    visitParams(item.params);
    switch (item.data.tag()) {
        case HIRStructData::TAG_Unit:
            break;
        case HIRStructData::TAG_Tuple:
            for (auto& field : item.data.as_Tuple()) {
                field.ent = visitType(field.ent);
            }
            break;
        case HIRStructData::TAG_Named:
            for (auto& field : item.data.as_Named()) {
                field.ty = visitType(field.ty);
            }
            break;
    }
}

ClosureExprVisitorExtract::DeferredFixup::DeferredFixup(HIRPathParams sourceParams, OutState::Counts first, OutState::Counts last, HIRStruct* structure, DeferredFixup* next)
    : sourceParams(mv$(sourceParams))
    , first(first)
    , last(last)
    , structure(structure)
    , next(next)
{
}

ClosureExprVisitorExtract::Monomorph::Monomorph(const StaticTraitResolve& resolve, HIRGenericParams& params, HIRPathParams& constructorPathParams)
    : Monomorphiser(resolve.hirCrate().types)
    , resolve(resolve)
    , params(params)
    , constructorPathParams(constructorPathParams)
{
}

auto ClosureExprVisitorExtract::Monomorph::freeze() -> HIRPathParams {
    frozen = true;
    return constructorPathParams.clone();
}

auto ClosureExprVisitorExtract::Monomorph::getType(const Span& sp, const HIRGenericRef& ge) const -> const HIRType* {
    size_t rv = SIZE_MAX;
    ASSERT_BUG(sp, constructorPathParams.types.size() == params.types.size(), StringView(""));
    for (size_t i = 0; i < constructorPathParams.types.size(); i++) {
        const auto& t = constructorPathParams.types[i];
        if (t->is_Generic() && t->as_Generic() == ge) {
            rv = i;
            DEBUG(StringView("Use param: ") << types.generic(params.types[rv].name, rv));
            break;
        }
    }
    if (rv == SIZE_MAX) {
        ASSERT_BUG(sp, !frozen, StringView("get_type would add a new param after freeze - ") << ge);
        rv = constructorPathParams.types.size();
        DEBUG(StringView("Add param: ") << types.generic(ge.name, rv) << StringView(" <- ") << ge);
        constructorPathParams.types.push_back(types.generic(ge.name, ge.binding));
        params.types.push_back(
            HIRTypeParamDef{
                ge.name,
                types.infer(),
                resolve.typeIsSized(sp, constructorPathParams.types.back()),
            }
        );
        params.paramKinds.pushBack(HIRGenericParamKind::Type);
    }
    ASSERT_BUG(sp, rv < params.types.size(), ge << StringView(" -> ") << rv << StringView(" < ") << params.types.size());
    return types.generic(params.types[rv].name, rv);
}

auto ClosureExprVisitorExtract::Monomorph::getValue(const Span& sp, const HIRGenericRef& ge) const -> HIRConstGeneric {
    size_t rv = SIZE_MAX;
    for (size_t i = 0; i < constructorPathParams.values.size(); i++) {
        const auto& v = constructorPathParams.values[i];
        if (v.is_Generic() && v.as_Generic() == ge) {
            rv = i;
            break;
        }
    }
    if (rv == SIZE_MAX) {
        ASSERT_BUG(sp, !frozen, StringView("get_value would add a new param after freeze - ") << ge);
        rv = constructorPathParams.values.size();
        constructorPathParams.values.push_back(ge);
        params.values.push_back(HIRValueParamDef());
        params.values.back().name = ge.name;
        params.paramKinds.pushBack(HIRGenericParamKind::Value);
    }
    return HIRConstGeneric::make_Generic({params.values[rv].name, static_cast<u32>(rv)});
}

auto ClosureExprVisitorExtract::Monomorph::monomorphType(const Span& sp, const HIRType* tpl, bool allowInfer) const -> const HIRType* {
    return Monomorphiser::monomorphType(sp, tpl, allowInfer);
}

auto ClosureExprVisitorExtract::Monomorph::maybeMonomorphBound(const Span& sp, const HIRGenericBound& bound) -> void {
    if (boundNeeded(sp, bound)) {
        if (addedBounds.insert(&bound).second) {
            DEBUG(StringView("-- Bound added ") << bound);
            auto newB = monomorphBound(sp, bound);
            params.bounds.push_back(mv$(newB));
        }
        DEBUG(StringView("-- Bound un-used ") << bound);
    }
}

auto ClosureExprVisitorExtract::Monomorph::addBounds(const Span& sp, const StaticTraitResolve& resolve) -> void {
    BUG_ASSERT(&resolve == &this->resolve);
    size_t l = SIZE_MAX;
    while (l != params.bounds.size()) {
        l = params.bounds.size();
        for (const auto& bound : resolve.implGenerics().bounds) {
            maybeMonomorphBound(sp, bound);
        }
        for (const auto& bound : resolve.itemGenerics().bounds) {
            maybeMonomorphBound(sp, bound);
        }
    }

    DEBUG(StringView("constructor_path_params = ") << constructorPathParams);
    for (size_t i = 0; i < constructorPathParams.types.size(); i++) {
        DEBUG(StringView("-- constructor_path_params Type: ") << constructorPathParams.types[i]);
        params.types[i].isSized = resolve.typeIsSized(sp, constructorPathParams.types[i]);
    }
    for (size_t i = 0; i < constructorPathParams.values.size(); i++) {
        DEBUG(StringView("-- constructor_path_params Value: ") << constructorPathParams.values[i]);
        params.values[i].type = resolve.getConstParamType(sp, constructorPathParams.values[i].as_Generic().binding);
    }
    DEBUG(StringView("params = ") << params.fmtArgs());
}

template <typename T, typename U>
auto ClosureExprVisitorExtract::Monomorph::contains(const std::vector<T>& l, const U& v) -> bool {
    return std::find(l.begin(), l.end(), v) != l.end();
}

template <typename T, typename U>
auto ClosureExprVisitorExtract::Monomorph::contains(const ThinVector<T>& l, const U& v) -> bool {
    return std::find(l.begin(), l.end(), v) != l.end();
}

auto ClosureExprVisitorExtract::Monomorph::updateTypeNeed(TypeNeed& rv, const HIRType* t) const -> bool {
    if (t->is_Generic()) {
        if (contains(constructorPathParams.types, t)) {
            if (rv == TypeNeed::NoGenerics) {
                rv = TypeNeed::Required;
            }
        } else {
            rv = TypeNeed::UsesOthers;
            return true;
        }
    }
    return false;
}

auto ClosureExprVisitorExtract::Monomorph::typeBoundNeeded(const Span& sp, const HIRType* ty) const -> TypeNeed {
    auto rv = TypeNeed::NoGenerics;
    visitTyWith(ty, [this, &rv](const HIRType* type) {
        return updateTypeNeed(rv, type);
    });
    return rv;
}

auto ClosureExprVisitorExtract::Monomorph::typeBoundNeeded(const Span& sp, const HIRGenericPath& tp) const -> TypeNeed {
    auto rv = TypeNeed::NoGenerics;
    for (const auto& ty : tp.params.types) {
        visitTyWith(ty, [this, &rv](const HIRType* type) {
            return updateTypeNeed(rv, type);
        });
    }
    return rv;
}

auto ClosureExprVisitorExtract::Monomorph::typeBoundNeeded(const Span& sp, const HIRTraitPath& tp) const -> TypeNeed {
    auto rv = TypeNeed::NoGenerics;
    visitTraitPathTysWith(tp, [this, &rv](const HIRType* type) {
        return updateTypeNeed(rv, type);
    });
    return rv;
}

auto ClosureExprVisitorExtract::Monomorph::boundNeeded(const Span& sp, const HIRGenericBound& b) const -> bool {
    switch (b.tag()) {
        case HIRGenericBound::TAG_TraitBound: {
            auto& e = b.as_TraitBound();
            if (typeBoundNeeded(sp, e.type) != TypeNeed::Required && typeBoundNeeded(sp, e.trait) != TypeNeed::Required) {
                return false;
            }
            return true;
        }
        case HIRGenericBound::TAG_TypeEquality: {
            TODO(sp, StringView(""));
            break;
        }
    }
    UNREACHABLE();
}

auto ClosureExprVisitorExtract::Monomorph::monomorphBound(const Span& sp, const HIRGenericBound& b) const -> HIRGenericBound {
    switch (b.tag()) {
        case HIRGenericBound::TAG_TraitBound: {
            auto& e = b.as_TraitBound();
            return HIRGenericBound::make_TraitBound({this->monomorphType(sp, e.type), this->monomorphTraitpath(sp, e.trait, false), e.constness, e.isTrivial});
        }
        case HIRGenericBound::TAG_TypeEquality: {
            auto& e = b.as_TypeEquality();
            return HIRGenericBound::make_TypeEquality({this->monomorphType(sp, e.type), this->monomorphType(sp, e.otherType)});
        }
    }
    UNREACHABLE();
}

ClosureExprVisitorExtract::ExprVisitorGeneratorRewrite::ExprVisitorGeneratorRewrite(const Monomorph& monomorph, const std::map<unsigned, unsigned>& rewrites)
    : HIRExprVisitorDef(monomorph.typeInterner())
    , monomorph(monomorph)
    , variableRewrites(rewrites)
{
}

[[nodiscard]] auto ClosureExprVisitorExtract::ExprVisitorGeneratorRewrite::visitType(const HIRType* ty) -> const HIRType* {
    return monomorph.monomorphType(Span(), ty, /*allow_infer=*/true);
}

auto ClosureExprVisitorExtract::ExprVisitorGeneratorRewrite::visitPathParams(HIRPathParams& pp) -> void {
    pp = monomorph.monomorphPathParams(Span(), pp, /*allow_infer=*/true);
}

auto ClosureExprVisitorExtract::ExprVisitorGeneratorRewrite::visitNodePtr(HIRExprNodeP& nodePtr) -> void {
    HIRExprVisitorDef::visitNodePtr(nodePtr);
    if (replacement_) {
        nodePtr = std::move(replacement_);
    }
}

auto ClosureExprVisitorExtract::ExprVisitorGeneratorRewrite::visit(HIRExprNodeVariable& node) -> void {
    node.slot = variableRewrites.at(node.slot);
}

auto ClosureExprVisitorExtract::ExprVisitorGeneratorRewrite::visit(HIRExprNodeConstParam& node) -> void {
    node.binding = monomorph.getValue(node.span(), HIRGenericRef("", node.binding)).as_Generic().binding;
}

auto ClosureExprVisitorExtract::ExprVisitorGeneratorRewrite::visit(HIRExprNodeArraySized& node) -> void {
    HIRExprVisitorDef::visit(node);
    node.size = monomorph.monomorphArraysize(node.span(), node.size);
}

auto ClosureExprVisitorExtract::ExprVisitorGeneratorRewrite::visit(HIRExprNodeClosure& node) -> void {
    BUG_ASSERT(!node.code);
    visitGenericPath(HIRVisitor::PathContext::TYPE, node.objPath);

    for (auto& cap : node.captures) {
        visitNodePtr(cap);
    }
}

auto ClosureExprVisitorExtract::ExprVisitorGeneratorRewrite::visit(HIRExprNodeGenerator& node) -> void {
    BUG_ASSERT(!node.code);
    visitGenericPath(HIRVisitor::PathContext::TYPE, node.objPath);
    node.stateDataType = visitType(node.stateDataType);

    for (auto& cap : node.captures) {
        visitNodePtr(cap);
    }
}

auto ClosureExprVisitorExtract::ExprVisitorGeneratorRewrite::visit(HIRExprNodeAsyncBlock& node) -> void {
    BUG_ASSERT(!node.code);
    visitGenericPath(HIRVisitor::PathContext::TYPE, node.objPath);
    node.stateDataType = visitType(node.stateDataType);

    for (auto& cap : node.captures) {
        visitNodePtr(cap);
    }
}

auto ClosureExprVisitorExtract::ExprVisitorGeneratorRewrite::visitPattern(const Span& sp, HIRPattern& pat) -> void {
    HIRExprVisitorDef::visitPattern(sp, pat);
    for (auto& pb : pat.bindings) {
        visitPatternBinding(sp, pb);
    }
    if (auto* pe = pat.data.opt_SplitSlice()) {
        visitPatternBinding(sp, pe->extraBind);
    }
}

auto ClosureExprVisitorExtract::ExprVisitorGeneratorRewrite::visitPatternBinding(const Span& sp, HIRPatternBinding& binding) -> void {
    if (binding.isValid()) {
        ASSERT_BUG(sp, variableRewrites.count(binding.slot), StringView("Newly defined variable #") << binding.slot << StringView(" not in rewrite list?"));
        binding.slot = variableRewrites.at(binding.slot);
    }
}

auto ClosureExprVisitorExtract::CrVars::setArguments(const Span& sp, Vector<const HIRType*> args) -> void {
    ASSERT_BUG(sp, args.length() == nArgs, StringView("Expected ") << nArgs << StringView(" coroutine arguments, got ") << args.length());
    Vector<const HIRType*> locals;
    locals.grow(args.length() + newLocals.length());
    locals.append(args.begin(), args.end());
    locals.append(newLocals.begin(), newLocals.end());
    newLocals = mv$(locals);
}

ClosureOuterVisitor::ClosureOuterVisitor(const WireBoard& wb)
    : HIRVisitor(nullptr, wb.crate->types)
    , resolve_(wb)
    , curModPath(nullptr)
{
}

auto ClosureOuterVisitor::visitCrate(HIRCrate& crate) -> void {
    Span sp;

    unsigned int closureCount = 0;
    HIRSimplePath rootModPath(crate.crateName, {});
    curModPath = &rootModPath;
    auto newType = makeCallable<ClosureTypeCb>([&](const char* prefix, const char* suffix, auto s) -> auto {
        auto name = RcString::newInterned(FMT(prefix << StringView("I_") << suffix << (suffix[0] ? "_" : "") << closureCount));
        closureCount += 1;
        auto boxed = crate.pool->make<HIRVisEnt<HIRTypeItem>>(HIRVisEnt<HIRTypeItem>{HIRPublicity::newNone(), mv$(s)});
        auto* retPtr = &boxed->ent;
        crate.rootModule.modItems.insert(std::make_pair(name, boxed));
        return std::make_pair(HIRSimplePath(crate.crateName, {}) + name, retPtr);
    });
    out.newType = &newType;

    auto emptyCounts = out.saveCounts();

    HIRVisitor::visitCrate(crate);

    out.updateSourceModule(emptyCounts, rootModPath);

    out.pushNewImpls(sp, crate);
}

auto ClosureOuterVisitor::visitModule(HIRItemPath p, HIRModule& mod) -> void {
    auto saved = curModPath;
    auto path = p.getSimplePath();
    curModPath = &path;

    std::vector<std::pair<RcString, HIRVisEnt<HIRTypeItem>*>> newTypes;

    auto prevImpls = out.saveCounts();

    unsigned int closureCount = 0;
    auto savedNt = out.newType;
    auto newType = makeCallable<ClosureTypeCb>([&](const char* prefix, const char* suffix, auto s) -> auto {
        // TODO: Use a function on `mod` that adds a closure and makes the indexes be per suffix
        auto name = RcString::newInterned(FMT(prefix << suffix << (suffix[0] ? "_" : "") << closureCount));
        closureCount += 1;
        auto boxed = resolve_.hirCrate().pool->make<HIRVisEnt<HIRTypeItem>>(HIRVisEnt<HIRTypeItem>{HIRPublicity::newNone(), mv$(s)});
        auto* retPtr = &boxed->ent;
        newTypes.push_back(std::make_pair(name, boxed));
        return std::make_pair((p + name).getSimplePath(), retPtr);
    });
    out.newType = &newType;

    HIRVisitor::visitModule(p, mod);

    curModPath = saved;
    out.newType = savedNt;

    for (auto& e : newTypes) {
        DEBUG(p << StringView(": Push ") << e.first);
        mod.modItems.insert(mv$(e));
    }

    out.updateSourceModule(prevImpls, path);
}

auto ClosureOuterVisitor::visitExpr(HIRExprPtr& exp) -> void {
    BUG(Span(), StringView("visit_expr hit in ClosureOuterVisitor"));
}

[[nodiscard]] auto ClosureOuterVisitor::visitType(const HIRType* ty) -> const HIRType* {
    if (ty->is_Array()) {
        auto data = ty->cloneData();
        auto& e = data.as_Array();
        e.inner = this->visitType(e.inner);
        DEBUG(StringView("Array size ") << ty);
        if (e.size.is_Unevaluated()) {
        }
        return resolve_.hirCrate().types.intern(std::move(data));
    }
    return visitTypeDefaultViaHooks(ty);
}

auto ClosureOuterVisitor::visitConstgeneric(HIRConstGeneric&) -> void {
}

auto ClosureOuterVisitor::visitFunction(HIRItemPath p, HIRFunction& item) -> void {
    auto _ = this->resolve_.setItemGenerics(item.params);
    if (item.code) {
        BUG_ASSERT(curModPath);
        indexMutableAnonymousNodes(item.code, out, resolve_.hirCrate().types);

        DEBUG(StringView("Function code ") << p);
        {
            const bool isAsyncDropIntrinsic = !p.getTopIp().ty && p.getSimplePath() == resolve_.hirCrate().getLangItemPathOpt("async_drop_in_place");
            ClosureExprVisitorExtract ev(resolve_, selfType, item.code.bindings, item.code, out, p.name, isAsyncDropIntrinsic);
            ev.visitRoot(*item.code);
        }

        {
            MonomorphiserNop mm(resolve_.hirCrate().types);
            ClosureExprVisitorFixup fixup{resolve_.board(), nullptr, mm, &out};
            fixup.visitRoot(item.code);
        }
        fixDefiningOpaqueAliasNodeTypes(resolve_, item.code.state->defineOpaque.data(), item.code.state->defineOpaque.size(), item.returnType);
        DEBUG(StringView("Function code ") << p << StringView(" (none)"));
    }
}

auto ClosureOuterVisitor::visitStatic(HIRItemPath p, HIRStatic& item) -> void {
    if (item.value) {
        indexMutableAnonymousNodes(item.value, out, resolve_.hirCrate().types);
        auto _ = this->resolve_.setItemGenerics(item.params);
        ClosureExprVisitorExtract ev(resolve_, selfType, item.value.bindings, item.value, out, p.name);
        ev.visitRoot(*item.value);

        {
            MonomorphiserNop mm(resolve_.hirCrate().types);
            ClosureExprVisitorFixup fixup{resolve_.board(), nullptr, mm, &out};
            fixup.visitRoot(item.value);
            item.type = fixup.visitType(item.type);
        }
    }
}

auto ClosureOuterVisitor::visitConstant(HIRItemPath p, HIRConstant& item) -> void {
    if (item.value) {
        indexMutableAnonymousNodes(item.value, out, resolve_.hirCrate().types);
        auto _ = this->resolve_.setItemGenerics(item.params);
        ClosureExprVisitorExtract ev(resolve_, selfType, item.value.bindings, item.value, out, p.name);
        ev.visitRoot(*item.value);

        {
            MonomorphiserNop mm(resolve_.hirCrate().types);
            ClosureExprVisitorFixup fixup{resolve_.board(), nullptr, mm, &out};
            fixup.visitRoot(item.value);
            item.type = fixup.visitType(item.type);
        }
    }
}

auto ClosureOuterVisitor::visitEnum(HIRItemPath p, HIREnum& item) -> void {
}

auto ClosureOuterVisitor::visitTrait(HIRItemPath p, HIRTrait& item) -> void {
    auto _ = this->resolve_.setImplGenerics(MetadataType::TraitObject, item.params);
    const HIRType* self = resolve_.hirCrate().types.generic(RcString("Self"), 0xFFFF);
    selfType = self;
    HIRVisitor::visitTrait(p, item);
    selfType = nullptr;
}

auto ClosureOuterVisitor::visitTypeImpl(HIRTypeImpl& impl) -> void {
    TRACE_FUNCTION_F(StringView("impl ") << impl.type);
    selfType = impl.type;
    auto _ = this->resolve_.setImplGenerics(impl.type, impl.params);

    // TODO: Re-create m_new_type to store in the source module

    auto prevImpls = out.saveCounts();

    HIRVisitor::visitTypeImpl(impl);

    out.updateSourceModule(prevImpls, impl.srcModule);

    selfType = nullptr;
}

auto ClosureOuterVisitor::visitTraitImpl(const HIRSimplePath& traitPath, HIRTraitImpl& impl) -> void {
    TRACE_FUNCTION_F(StringView("impl ") << traitPath << StringView(" for ") << impl.type);
    selfType = impl.type;
    auto _ = this->resolve_.setImplGenerics(impl.type, impl.params);

    auto prevImpls = out.saveCounts();

    HIRVisitor::visitTraitImpl(traitPath, impl);

    out.updateSourceModule(prevImpls, impl.srcModule);

    selfType = nullptr;
}

ErasedExprVisitorExtract::ErasedExprVisitorExtract(const StaticTraitResolve& resolve)
    : HIRExprVisitorDef(resolve.hirCrate().types)
    , resolve_(resolve)
{
}

auto ErasedExprVisitorExtract::visitRoot(HIRExprPtr& root) -> void {
    root->visit(*this);
    root->resType = visitType(root->resType);
    for (auto& type : mutRange(root.bindings)) {
        type = visitType(type);
    }
    for (auto& type : mutRange(root.erasedTypes)) {
        type = visitType(type);
    }
}

auto ErasedExprVisitorExtract::visitNodePtr(HIRExprNodeP& nodePtr) -> void {
    BUG_ASSERT(nodePtr);
    nodePtr->visit(*this);
    nodePtr->resType = visitType(nodePtr->resType);
}

[[nodiscard]] auto ErasedExprVisitorExtract::visitType(const HIRType* ty) -> const HIRType* {
    Span sp;
    return ::visitType(sp, resolve_, ty);
}

ErasedOuterVisitor::ErasedOuterVisitor(const WireBoard& wb)
    : HIRVisitor(&resolve_, wb.crate->types)
    , resolve_(wb)
{
}

auto ErasedOuterVisitor::visitExpr(HIRExprPtr& exp) -> void {
    if (exp) {
        ErasedExprVisitorExtract ev(resolve_);
        ev.visitRoot(exp);
    }
}

ErasedOuterVisitorFixup::ErasedOuterVisitorFixup(const WireBoard& wb)
    : HIRVisitor(&resolve_, wb.crate->types)
    , resolve_(wb)
{
}

auto ErasedOuterVisitorFixup::visitParams(HIRGenericParams& params) -> void {
    for (auto& value : params.values) {
        value.type = visitType(value.type);
        visitConstgeneric(value.defaultValue);
    }
    for (auto& bound : params.bounds) {
        visitGenericBound(bound);
    }
}

[[nodiscard]] auto ErasedOuterVisitorFixup::visitType(const HIRType* ty) -> const HIRType* {
    Span sp;
    return ::visitType(sp, resolve_, ty);
}

auto ReborrowExprVisitorMutate::markUniquePlace(HIRExprNodeP& node) -> void {
    node->usage = HIRValueUsage::Mutate;
    if (auto* field = cast<HIRExprNodeField>(node.get())) {
        markUniquePlace(field->value);
    } else if (auto* index = cast<HIRExprNodeIndex>(node.get())) {
        markUniquePlace(index->value);
    } else if (auto* deref = cast<HIRExprNodeDeref>(node.get())) {
        markUniquePlace(deref->value);
    }
}

ReborrowExprVisitorMutate::ReborrowExprVisitorMutate(const HIRCrate& crate)
    : HIRExprVisitorDef(crate.types)
    , crate(crate)
{
}

auto ReborrowExprVisitorMutate::visitNodePtr(HIRExprPtr& root) -> void {
    const auto& nodeRef = *root;
    const char* nodeTy = typeid(nodeRef).name();

    root->visit(*this);

    auto np = root.takeNode();
    np = doReborrow(mv$(np));
    root.reset(np.release());
}

auto ReborrowExprVisitorMutate::visitNodePtr(HIRExprNodeP& node) -> void {
    const auto& nodeRef = *node;
    const char* nodeTy = typeid(nodeRef).name();
    TRACE_FUNCTION_FR(static_cast<const void*>(&*node) << StringView(" ") << nodeTy << StringView(" : ") << node->resType, nodeTy);
    BUG_ASSERT(node);
    node->visit(*this);
}

#ifdef NEWNODE
    #undef NEWNODE
#endif
#define NEWNODE(TY, CLASS, ...) reborrowMkExprnodep(crate.pool->make<HIRExprNode##CLASS>(__VA_ARGS__), TY)

auto ReborrowExprVisitorMutate::doReborrow(HIRExprNodeP nodePtr) -> HIRExprNodeP {
    BUG_ASSERT(nodePtr);
    if (const auto* e = nodePtr->resType->opt_Borrow()) {
        if (e->type == HIRBorrowType::Unique) {
            if (cast<HIRExprNodeIndex>(nodePtr.get()) || cast<HIRExprNodeVariable>(nodePtr.get()) || cast<HIRExprNodeField>(nodePtr.get()) || cast<HIRExprNodeDeref>(nodePtr.get())) {
                markUniquePlace(nodePtr);
                DEBUG(StringView("Insert reborrow - ") << nodePtr->span() << StringView(" - type=") << nodePtr->resType);
                auto sp = nodePtr->span();
                auto tyMut = nodePtr->resType;
                auto ty = e->inner;
                nodePtr = NEWNODE(mv$(tyMut), Borrow, sp, HIRBorrowType::Unique, NEWNODE(mv$(ty), Deref, sp, mv$(nodePtr)));
            } else if (auto p = cast<HIRExprNodeBlock>(nodePtr.get())) {
                if (p->valueNode) {
                    p->valueNode = doReborrow(mv$(p->valueNode));
                } else {
                    const auto* node = nodePtr.get();
                    DEBUG(StringView("Node ") << static_cast<const void*>(node) << StringView(" is a non-yielding block"));
                }
            } else {
                const auto* node = nodePtr.get();
                DEBUG(StringView("Node ") << static_cast<const void*>(node) << StringView(" ") << typeid(*node).name() << StringView(" cannot have a reborrow"));
            }
        }
    }
    return nodePtr;
}

#undef NEWNODE

auto ReborrowExprVisitorMutate::visit(HIRExprNodeCast& node) -> void {
    HIRExprVisitorDef::visit(node);
    node.value = doReborrow(mv$(node.value));
}

auto ReborrowExprVisitorMutate::visit(HIRExprNodeEmplace& node) -> void {
    HIRExprVisitorDef::visit(node);
    node.value = doReborrow(mv$(node.value));
}

auto ReborrowExprVisitorMutate::visit(HIRExprNodeAssign& node) -> void {
    HIRExprVisitorDef::visit(node);
    node.value = doReborrow(mv$(node.value));
}

auto ReborrowExprVisitorMutate::visit(HIRExprNodeCallPath& node) -> void {
    HIRExprVisitorDef::visit(node);
    for (auto& arg : node.args) {
        arg = doReborrow(mv$(arg));
    }
}

auto ReborrowExprVisitorMutate::visit(HIRExprNodeCallValue& node) -> void {
    HIRExprVisitorDef::visit(node);
    for (auto& arg : node.args) {
        arg = doReborrow(mv$(arg));
    }
}

auto ReborrowExprVisitorMutate::visit(HIRExprNodeCallMethod& node) -> void {
    HIRExprVisitorDef::visit(node);
    for (auto& arg : node.args) {
        arg = doReborrow(mv$(arg));
    }
}

auto ReborrowExprVisitorMutate::visit(HIRExprNodeMatch& node) -> void {
    HIRExprVisitorDef::visit(node);
    for (auto& arm : node.arms) {
        arm.code = doReborrow(std::move(arm.code));
    }
}

auto ReborrowExprVisitorMutate::visit(HIRExprNodeArrayList& node) -> void {
    HIRExprVisitorDef::visit(node);
    for (auto& arg : node.vals) {
        arg = doReborrow(mv$(arg));
    }
}

auto ReborrowExprVisitorMutate::visit(HIRExprNodeTuple& node) -> void {
    HIRExprVisitorDef::visit(node);
    for (auto& arg : node.vals) {
        arg = doReborrow(mv$(arg));
    }
}

auto ReborrowExprVisitorMutate::visit(HIRExprNodeTupleVariant& node) -> void {
    HIRExprVisitorDef::visit(node);
    for (auto& arg : node.args) {
        arg = doReborrow(mv$(arg));
    }
}

auto ReborrowExprVisitorMutate::visit(HIRExprNodeStructLiteral& node) -> void {
    HIRExprVisitorDef::visit(node);
    for (auto& arg : node.values) {
        arg.second = doReborrow(mv$(arg.second));
    }
}

auto ReborrowExprVisitorMutate::visit(HIRExprNodeUnsize& node) -> void {
    HIRExprVisitorDef::visit(node);
    node.value = doReborrow(mv$(node.value));
}

auto ReborrowExprVisitorMutate::visit(HIRExprNodeClosure& node) -> void {
    HIRExprVisitorDef::visit(node);
    for (auto& arg : node.captures) {
        arg = doReborrow(mv$(arg));
    }
}

auto ReborrowExprVisitorMutate::visit(HIRExprNodeGenerator& node) -> void {
    HIRExprVisitorDef::visit(node);
    for (auto& arg : node.captures) {
        arg = doReborrow(mv$(arg));
    }
}

auto ReborrowExprVisitorMutate::visit(HIRExprNodeAsyncBlock& node) -> void {
    HIRExprVisitorDef::visit(node);
    for (auto& arg : node.captures) {
        visitNodePtr(arg);
        arg = doReborrow(mv$(arg));
    }
}

ReborrowOuterVisitor::ReborrowOuterVisitor(const HIRCrate& crate)
    : HIRVisitor(nullptr, crate.types)
    , crate(crate)
{
}

auto ReborrowOuterVisitor::visitExpr(HIRExprPtr& exp) -> void {
    if (exp) {
        ReborrowExprVisitorMutate ev(crate);
        ev.visitNodePtr(exp);
    }
}

auto ReborrowOuterVisitor::visitFunction(HIRItemPath p, HIRFunction& item) -> void {
    if (item.code) {
        DEBUG(StringView("Function code ") << p);
        ReborrowExprVisitorMutate ev(crate);
        ev.visitNodePtr(item.code);
        DEBUG(StringView("Function code ") << p << StringView(" (none)"));
    }
}

StaticBorrowExprVisitorMark::StaticBorrowExprVisitorMark(const StaticTraitResolve& resolve, const HIRType* selfType, const HIRExprPtr& exprPtr, bool promoteAllConstFnCalls)
    : HIRExprVisitorDef(resolve.hirCrate().types)
    , resolve_(resolve)
    , selfType(selfType)
    , exprPtr(exprPtr)
    , isConstant(false)
    , allConstant_(false)
    , promoteAllConstFnCalls(promoteAllConstFnCalls)
{
    langRangeFull_ = resolve_.hirCrate().getLangItemPathOpt("range_full");
}

auto StaticBorrowExprVisitorMark::allConstant() const -> bool {
    return allConstant_;
}

auto StaticBorrowExprVisitorMark::visitNodePtr(HIRExprPtr& root) -> void {
    const auto& nodeRef = *root;
    const char* nodeTy = typeid(nodeRef).name();
    BUG_ASSERT(&root == &exprPtr);

    TRACE_FUNCTION_FR(static_cast<const void*>(&*root) << StringView(" ") << nodeTy << StringView(" : ") << root->resType, nodeTy);
    allConstant_ = true;
    root->visit(*this);
}

auto StaticBorrowExprVisitorMark::visitNodePtr(HIRExprNodeP& node) -> void {
    BUG_ASSERT(node);
#if defined(TRUSTME_DEBUG)
    const auto& nodeRef = *node;
    const char* nodeTy = typeid(nodeRef).name();
#endif
    isConstant = false;
    {
        TRACE_FUNCTION_FR(static_cast<const void*>(&*node) << StringView(" ") << nodeTy << StringView(" : ") << node->resType, nodeTy << StringView(" ") << isConstant << StringView(" A=") << allConstant_);
        node->visit(*this);
        if (!isConstant) {
            allConstant_ = false;
        }
    }
    isConstant = false;
}

auto StaticBorrowExprVisitorMark::visit(HIRExprNodeBorrow& node) -> void {
    auto savedAllConstant = allConstant_;
    allConstant_ = true;
    HIRExprVisitorDef::visit(node);
    const bool wholeValueIsConstant = allConstant_;

    if (const auto* inode = cast<HIRExprNodePathValue>(node.value.get())) {
        MonomorphState ms(resolve_.hirCrate().types);
        auto v = resolve_.getValue(node.span(), inode->path, ms, /*signature_only*/ true);
        if (v.is_Static()) {
            allConstant_ = savedAllConstant;
            isConstant = true;
            return;
        }
    }

    auto* valuePtrPtr = staticBorrowPromotionRoot(node.value);
    bool promotionRootIsConstant = wholeValueIsConstant;
    if (!promotionRootIsConstant && valuePtrPtr != &node.value) {
        promotionRootIsConstant = nodeIsConstant(*valuePtrPtr);
    }

    if (const auto* path = cast<HIRExprNodePathValue>(valuePtrPtr->get())) {
        MonomorphState ms(resolve_.hirCrate().types);
        if (resolve_.getValue(node.span(), path->path, ms, /*signature_only*/ true).is_Static()) {
            allConstant_ = savedAllConstant;
            isConstant = wholeValueIsConstant;
            return;
        }
    }

    if (promotionRootIsConstant) {
        {
            auto vpp = valuePtrPtr;
            while (auto* innerNode = cast<HIRExprNodeField>(vpp->get())) {
                vpp = &innerNode->value;
            }
            if (auto* innerNode = cast<HIRExprNodeDeref>(vpp->get())) {
                allConstant_ = savedAllConstant;
                isConstant = true;
                return;
            }
        }
        auto& valuePtr = *valuePtrPtr;

        bool isUnsized = false;
        bool isZst = ([&]() -> bool {
            // HACK: `Target_GetSizeOf` calls `Target_GetSizeAndAlignOf` which doesn't work on generic arrays (needs alignment)
            if (const auto* te = valuePtr->resType->opt_Array()) {
                if (te->size.is_Known() && te->size.as_Known() == 0) {
                    return true;
                }
            }
            size_t v = 1, unusedAlign = 0;
            TargetGetSizeAndAlignOf(valuePtr->span(), resolve_, valuePtr->resType, v, unusedAlign);
            isUnsized = (v == SIZE_MAX);
            return v == 0;
        })();

        if (!isUnsized && !candidateNeedsDrop(valuePtr) && (isZst || node.type == HIRBorrowType::Shared)) {
            DEBUG(StringView("-- Marking static"));
            node.isValidStaticBorrowConstant = true;

            isConstant = wholeValueIsConstant;
        }
    } else {
        if (cast<HIRExprNodePathValue>(node.value.get()) && node.value->usage == HIRValueUsage::Borrow) {
            isConstant = true;
        }
    }

    allConstant_ = savedAllConstant;
}

auto StaticBorrowExprVisitorMark::visit(HIRExprNodeArraySized& node) -> void {
    auto savedAllConstant = allConstant_;
    allConstant_ = true;
    HIRExprVisitorDef::visit(node);
    isConstant = allConstant_;
    allConstant_ = savedAllConstant;
}

auto StaticBorrowExprVisitorMark::visit(HIRExprNodeArrayList& node) -> void {
    auto savedAllConstant = allConstant_;
    allConstant_ = true;
    HIRExprVisitorDef::visit(node);
    isConstant = allConstant_;
    allConstant_ = savedAllConstant;
}

auto StaticBorrowExprVisitorMark::visit(HIRExprNodeStructLiteral& node) -> void {
    auto savedAllConstant = allConstant_;
    allConstant_ = true;
    HIRExprVisitorDef::visit(node);
    isConstant = allConstant_;
    allConstant_ = savedAllConstant;
}

auto StaticBorrowExprVisitorMark::visit(HIRExprNodeTupleVariant& node) -> void {
    auto savedAllConstant = allConstant_;
    allConstant_ = true;
    HIRExprVisitorDef::visit(node);
    isConstant = allConstant_;
    allConstant_ = savedAllConstant;
}

auto StaticBorrowExprVisitorMark::visit(HIRExprNodeTuple& node) -> void {
    auto savedAllConstant = allConstant_;
    allConstant_ = true;
    HIRExprVisitorDef::visit(node);
    isConstant = allConstant_;
    allConstant_ = savedAllConstant;
}

auto StaticBorrowExprVisitorMark::visit(HIRExprNodeLet& node) -> void {
    auto savedAllConstant = allConstant_;
    allConstant_ = true;
    HIRExprVisitorDef::visit(node);
    isConstant = allConstant_;
    allConstant_ = savedAllConstant;
}

auto StaticBorrowExprVisitorMark::visit(HIRExprNodeCallMethod& node) -> void {
    auto savedAllConstant = allConstant_;
    allConstant_ = true;
    HIRExprVisitorDef::visit(node);
    if (allConstant_) {
        MonomorphState msUnused(resolve_.hirCrate().types);
        auto v = resolve_.getValue(node.span(), node.methodPath, msUnused, true);
        const auto& function = *v.as_Function();
        DEBUG(node.methodPath << StringView(" is ") << (function.markings.isRustcPromotable ? "" : "NOT ") << StringView("promotable"));
        if (function.markings.isRustcPromotable || (promoteAllConstFnCalls && function.isConst)) {
            isConstant = !isMaybeInteriorMut(node);
        }
    }
    allConstant_ = savedAllConstant;
}

auto StaticBorrowExprVisitorMark::visit(HIRExprNodeCallPath& node) -> void {
    auto savedAllConstant = allConstant_;
    allConstant_ = true;
    HIRExprVisitorDef::visit(node);
    if (allConstant_) {
        MonomorphState msUnused(resolve_.hirCrate().types);
        auto v = resolve_.getValue(node.span(), node.path, msUnused, true);
        const auto& function = *v.as_Function();
        DEBUG(node.path << StringView(" is ") << (function.markings.isRustcPromotable ? "" : "NOT ") << StringView("promotable"));
        if (function.markings.isRustcPromotable || (promoteAllConstFnCalls && function.isConst)) {
            isConstant = !isMaybeInteriorMut(node);
        }
    }
    allConstant_ = savedAllConstant;
}

auto StaticBorrowExprVisitorMark::visit(HIRExprNodeDeref& node) -> void {
    HIRExprVisitorDef::visit(node);
    if (node.value->resType->is_Borrow()) {
        isConstant = allConstant_;
    }
}

auto StaticBorrowExprVisitorMark::visit(HIRExprNodeField& node) -> void {
    HIRExprVisitorDef::visit(node);
    isConstant = allConstant_;
}

auto StaticBorrowExprVisitorMark::visit(HIRExprNodeIndex& node) -> void {
    auto savedAllConstant = allConstant_;
    allConstant_ = true;
    HIRExprVisitorDef::visit(node);
    if (allConstant_) {
        const auto& ty = node.index->resType;
        DEBUG(StringView("_Index: ty = ") << ty);
        if (node.value->resType->is_Array() && node.value->resType->as_Array().size == 0) {
            isConstant = true;
        } else {
            if (ty->is_Path() && ty->as_Path().path.data.is_Generic() && ty->as_Path().path.data.as_Generic().path == langRangeFull_) {
                DEBUG(StringView("_Index: RangeFull - can be constant"));
                isConstant = !isMaybeInteriorMut(node);
            } else {
                isConstant = !isMaybeInteriorMut(node);
            }
        }
    }
    allConstant_ = savedAllConstant;
}

auto StaticBorrowExprVisitorMark::visit(HIRExprNodeCast& node) -> void {
    auto savedAllConstant = allConstant_;
    allConstant_ = true;
    HIRExprVisitorDef::visit(node);
    const auto* dstPrimitive = node.resType->opt_Primitive();
    const bool exposesAddress = dstPrimitive && isInteger(*dstPrimitive) && (node.value->resType->is_NamedFunction() || node.value->resType->is_Function() || node.value->resType->is_Pointer());
    isConstant = allConstant_ && !exposesAddress;
    allConstant_ = savedAllConstant;
}

auto StaticBorrowExprVisitorMark::visit(HIRExprNodeUnsize& node) -> void {
    auto savedAllConstant = allConstant_;
    allConstant_ = true;
    HIRExprVisitorDef::visit(node);
    isConstant = allConstant_;
    allConstant_ = savedAllConstant;
}

auto StaticBorrowExprVisitorMark::visit(HIRExprNodeBinOp& node) -> void {
    auto savedAllConstant = allConstant_;
    allConstant_ = true;
    HIRExprVisitorDef::visit(node);
    if (allConstant_) {
        if (node.left->resType == node.right->resType) {
            if (node.left->resType->is_Primitive()) {
                isConstant = true;
            }
        }
    }
    allConstant_ = savedAllConstant;
}

auto StaticBorrowExprVisitorMark::visit(HIRExprNodeUniOp& node) -> void {
    auto savedAllConstant = allConstant_;
    allConstant_ = true;
    HIRExprVisitorDef::visit(node);
    if (allConstant_) {
        if (node.value->resType->is_Primitive()) {
            isConstant = true;
        }
    }
    allConstant_ = savedAllConstant;
}

auto StaticBorrowExprVisitorMark::visit(HIRExprNodeBlock& node) -> void {
    HIRExprVisitorDef::visit(node);
    isConstant = allConstant_;
}

auto StaticBorrowExprVisitorMark::visit(HIRExprNodeConstBlock& node) -> void {
    HIRExprVisitorDef::visit(node);
    // TODO: Separate the const-valid and const-promotable flags

    checkConstFinalBorrow(resolve_, *node.inner);

    isConstant = allConstant_;
}

auto StaticBorrowExprVisitorMark::visit(HIRExprNodeLiteral& node) -> void {
    HIRExprVisitorDef::visit(node);
    isConstant = true;
}

auto StaticBorrowExprVisitorMark::visit(HIRExprNodeConstParam& node) -> void {
    HIRExprVisitorDef::visit(node);
    isConstant = true;
}

auto StaticBorrowExprVisitorMark::visit(HIRExprNodeUnitVariant& node) -> void {
    HIRExprVisitorDef::visit(node);
    isConstant = true;
}

auto StaticBorrowExprVisitorMark::visit(HIRExprNodePathValue& node) -> void {
    HIRExprVisitorDef::visit(node);
    MonomorphState ms(resolve_.hirCrate().types);
    auto v = resolve_.getValue(node.span(), node.path, ms, /*signature_only*/ true);
    switch (v.tag()) {
        case StaticTraitResolve::ValuePtr::TAG_Constant:
            if (!monomorphisePathNeeded(node.path)) {
                isConstant = !isMaybeInteriorMut(node);
                DEBUG(node.path << StringView(" m_is_constant=") << isConstant);
            }
            break;
        case StaticTraitResolve::ValuePtr::TAG_Function:
            isConstant = true;
            break;
        case StaticTraitResolve::ValuePtr::TAG_Static:
            if (!v.as_Static()->isMut && !monomorphisePathNeeded(node.path) && (v.as_Static()->valueGenerated || v.as_Static()->value) && resolve_.typeIsCopy(node.span(), node.resType)) {
                isConstant = !isMaybeInteriorMut(node);
                DEBUG(node.path << StringView(" m_is_constant=") << isConstant);
            }
            break;
        default:
            break;
    }
}

auto StaticBorrowExprVisitorMark::visit(HIRExprNodeClosure& node) -> void {
    auto savedAllConstant = allConstant_;
    HIRExprVisitorDef::visit(node);
    allConstant_ = savedAllConstant;

    if (node.avuCache.capturedVars.empty()) {
        isConstant = true;

        if (node.code) {
            for (auto idx : node.avuCache.localVars) {
                ASSERT_BUG(node.span(), idx < exprPtr.bindings.length(), StringView("Local variable #") << idx << StringView(" out of range: ") << exprPtr.bindings.length());
                if (monomorphiseTypeNeeded(exprPtr.bindings[idx])) {
                    isConstant = false;
                    break;
                }
            }
        } else {
        }
    }
}

auto StaticBorrowExprVisitorMark::nodeIsConstant(HIRExprNodeP& node) -> bool {
    const auto savedAllConstant = allConstant_;
    const auto savedIsConstant = isConstant;
    allConstant_ = true;
    isConstant = false;
    node->visit(*this);
    const bool rv = isConstant;
    allConstant_ = savedAllConstant;
    isConstant = savedIsConstant;
    return rv;
}

auto StaticBorrowExprVisitorMark::candidateNeedsDrop(HIRExprNodeP& root) const -> bool {
    struct Visitor: public HIRExprVisitorDef {
        const StaticTraitResolve& resolve;
        bool needsDrop = false;

        explicit Visitor(const StaticTraitResolve& resolve)
            : HIRExprVisitorDef(resolve.hirCrate().types)
            , resolve(resolve)
        {
        }

        void visitNodePtr(HIRExprNodeP& node) override {
            if (needsDrop) {
                return;
            }
            if (resolve.typeNeedsDropGlue(node->span(), node->resType)) {
                needsDrop = true;
                return;
            }
            HIRExprVisitorDef::visitNodePtr(node);
        }

        void visit(HIRExprNodeClosure&) override {
        }

        void visit(HIRExprNodeGenerator&) override {
        }

        void visit(HIRExprNodeGeneratorWrapper&) override {
        }

        void visit(HIRExprNodeAsyncBlock&) override {
        }
    } visitor(resolve_);

    visitor.visitNodePtr(root);
    return visitor.needsDrop;
}

auto StaticBorrowExprVisitorMark::isMaybeInteriorMut(const HIRExprNode& node) const -> bool {
    return resolve_.typeIsInteriorMutable(node.span(), node.resType) != InteriorMutability::No;
}

StaticBorrowOuterVisitorMark::StaticBorrowOuterVisitorMark(const WireBoard& wb)
    : HIRVisitor(nullptr, wb.crate->types)
    , crate(*wb.crate)
    , resolve_(wb)
    , currentModule(nullptr)
{
}

auto StaticBorrowOuterVisitorMark::visitModule(HIRItemPath p, HIRModule& mod) -> void {
    auto par = currentModule;
    auto parP = currentModulePath;
    currentModule = &mod;
    currentModulePath = &p;

    HIRVisitor::visitModule(p, mod);

    currentModule = par;
    currentModulePath = parP;
}

auto StaticBorrowOuterVisitorMark::visitTrait(HIRItemPath p, HIRTrait& item) -> void {
    auto self = crate.types.self();
    selfType = self;
    auto _ = resolve_.setImplGenerics(MetadataType::TraitObject, item.params);
    HIRVisitor::visitTrait(p, item);
    selfType = nullptr;
}

auto StaticBorrowOuterVisitorMark::visitTypeImpl(HIRTypeImpl& impl) -> void {
    const auto& srcmod = crate.getModByPath(Span(), impl.srcModule);
    auto modIp = HIRItemPath(impl.srcModule);
    selfType = impl.type;
    currentModule = &srcmod;
    currentModulePath = &modIp;

    auto _ = resolve_.setImplGenerics(impl.type, impl.params);
    HIRVisitor::visitTypeImpl(impl);

    currentModule = nullptr;
    selfType = nullptr;
}

auto StaticBorrowOuterVisitorMark::visitTraitImpl(const HIRSimplePath& traitPath, HIRTraitImpl& impl) -> void {
    const auto& srcmod = crate.getModByPath(Span(), impl.srcModule);
    auto modIp = HIRItemPath(impl.srcModule);
    selfType = impl.type;
    currentModule = &srcmod;
    currentModulePath = &modIp;

    auto _ = resolve_.setImplGenerics(impl.type, impl.params);
    HIRVisitor::visitTraitImpl(traitPath, impl);

    currentModule = nullptr;
    selfType = nullptr;
}

auto StaticBorrowOuterVisitorMark::visitExpr(HIRExprPtr& exp) -> void {
    BUG(Span(), StringView("visit_expr hit in StaticBorrowOuterVisitor"));
}

auto StaticBorrowOuterVisitorMark::visitConstgeneric(HIRConstGeneric& c) -> void {
    if (auto* e = c.opt_Unevaluated()) {
        auto& ep = (*e)->expr;
        StaticBorrowExprVisitorMark ev(resolve_, selfType, *ep);
        ev.visitNodePtr(*ep);
    }
}

[[nodiscard]] auto StaticBorrowOuterVisitorMark::visitType(const HIRType* ty) -> const HIRType* {
    return visitTypeDefaultViaHooks(ty);
}

auto StaticBorrowOuterVisitorMark::visitFunction(HIRItemPath p, HIRFunction& item) -> void {
    if (item.code) {
        auto _ = resolve_.setItemGenerics(item.params);
        DEBUG(StringView("Function code ") << p);
        StaticBorrowExprVisitorMark ev(resolve_, selfType, item.code);
        ev.visitNodePtr(item.code);
        DEBUG(StringView("Function code ") << p << StringView(" (none)"));
    }
}

auto StaticBorrowOuterVisitorMark::visitStatic(HIRItemPath p, HIRStatic& item) -> void {
    if (item.value) {
        StaticBorrowExprVisitorMark ev(resolve_, selfType, item.value, true);
        ev.visitNodePtr(item.value);
    }
}

auto StaticBorrowOuterVisitorMark::visitConstant(HIRItemPath p, HIRConstant& item) -> void {
    if (item.value) {
        checkConstFinalBorrow(resolve_, *item.value);
        StaticBorrowExprVisitorMark ev(resolve_, selfType, item.value, true);
        ev.visitNodePtr(item.value);
    }
}

auto StaticBorrowOuterVisitorMark::visitEnum(HIRItemPath p, HIREnum& item) -> void {
    if (auto* e = item.data.opt_Value()) {
        auto _ = resolve_.setImplGenerics(MetadataType::None, item.params);
        for (auto& var : e->variants) {
            DEBUG(StringView("Enum value ") << p << StringView(" - ") << var.name);
            if (var.expr) {
                StaticBorrowExprVisitorMark ev(resolve_, selfType, var.expr, true);
                ev.visitNodePtr(var.expr);
            }
        }
    }
}

template <typename F>
NewStaticCb<F>::NewStaticCb(F f)
    : f(f)
{
}

template <typename F>
auto NewStaticCb<F>::create(Span sp, const HIRType* type, HIRExprPtr value, HIRGenericParams generics, bool isConst) -> HIRSimplePath {
    return f(sp, mv$(type), mv$(value), mv$(generics), isConst);
}

StaticBorrowExprVisitorMutate::StaticBorrowExprVisitorMutate(const StaticTraitResolve& resolve, const HIRType* selfType, NewStaticCallback& newStaticCb, const HIRExprPtr& exprPtr)
    : HIRExprVisitorDef(resolve.hirCrate().types)
    , resolve_(resolve)
    , selfType(selfType)
    , newStaticCb(newStaticCb)
    , exprPtr(exprPtr)
{
    langRangeFull_ = resolve_.hirCrate().getLangItemPathOpt("range_full");
}

auto StaticBorrowExprVisitorMutate::visitNodePtr(HIRExprPtr& root) -> void {
    root->visit(*this);
}

auto StaticBorrowExprVisitorMutate::visitNodePtr(HIRExprNodeP& root) -> void {
    root->visit(*this);
}

auto StaticBorrowExprVisitorMutate::createParams(const Span& sp, HIRGenericParams& params, HIRPathParams& constructorPathParams) const -> Monomorph {
    if (resolve_.hasSelf() && selfType) {
        ASSERT_BUG(sp, selfType, StringView("Missing self type (disagreement between m_resolve and StaticBorrowExprVisitorMutate)"));
        constructorPathParams.types.push_back(selfType);
        params.types.push_back(HIRTypeParamDef{RcString::newInterned("Super"), resolve_.hirCrate().types.infer(), false}); // TODO: Determine if parent Self is Sized
        params.paramKinds.pushBack(HIRGenericParamKind::Type);
    }
    unsigned ofsImplT = params.types.size();
    for (const auto& tyDef : resolve_.implGenerics().types) {
        unsigned i = &tyDef - &resolve_.implGenerics().types.front();
        constructorPathParams.types.push_back(resolve_.hirCrate().types.generic(tyDef.name, 0 * 256 + i));
        params.types.push_back(HIRTypeParamDef{tyDef.name, resolve_.hirCrate().types.infer(), tyDef.isSized});
    }
    unsigned ofsImplV = params.values.size();
    for (const auto& vDef : resolve_.implGenerics().values) {
        unsigned i = &vDef - &resolve_.implGenerics().values.front();
        constructorPathParams.values.push_back(HIRGenericRef(vDef.name, 0 * 256 + i));
        params.values.push_back(HIRValueParamDef{vDef.name, vDef.type});
    }
    unsigned ofsItemT = params.types.size();
    for (const auto& tyDef : resolve_.itemGenerics().types) {
        unsigned i = &tyDef - &resolve_.itemGenerics().types.front();
        constructorPathParams.types.push_back(resolve_.hirCrate().types.generic(tyDef.name, 1 * 256 + i));
        params.types.push_back(HIRTypeParamDef{tyDef.name, resolve_.hirCrate().types.infer(), tyDef.isSized});
    }
    unsigned ofsItemV = params.values.size();
    for (const auto& vDef : resolve_.itemGenerics().values) {
        unsigned i = &vDef - &resolve_.itemGenerics().values.front();
        constructorPathParams.values.push_back(HIRGenericRef(vDef.name, 1 * 256 + i));
        params.values.push_back(HIRValueParamDef{vDef.name, vDef.type});
    }
    for (size_t i = 0; i < resolve_.implGenerics().paramCount(); i++) {
        params.paramKinds.pushBack(resolve_.implGenerics().paramKindAt(i));
    }
    for (size_t i = 0; i < resolve_.itemGenerics().paramCount(); i++) {
        params.paramKinds.pushBack(resolve_.itemGenerics().paramKindAt(i));
    }

    DEBUG(StringView("impl_path_params = ") << params.makeNopParams(resolve_.hirCrate().types, 0) << StringView(" ofs_*_t=") << ofsItemT << StringView(",") << ofsImplT << StringView(",") << params.types.size() << StringView(" ofs_*_v=") << ofsItemV << StringView(",") << ofsImplV << StringView(",") << params.values.size());
    Monomorph monomorphCb(resolve_.hirCrate().types, params, ofsImplT, ofsItemT, ofsImplV, ofsItemV);

    auto monomorphBound = [&](const HIRGenericBound& b) -> HIRGenericBound {
        switch (b.tag()) {
            case HIRGenericBound::TAG_TraitBound: {
                auto& e = b.as_TraitBound();
                return HIRGenericBound::make_TraitBound({monomorphCb.monomorphType(sp, e.type), monomorphCb.monomorphTraitpath(sp, e.trait, false), e.constness, e.isTrivial});
            }
            case HIRGenericBound::TAG_TypeEquality: {
                auto& e = b.as_TypeEquality();
                return HIRGenericBound::make_TypeEquality({monomorphCb.monomorphType(sp, e.type), monomorphCb.monomorphType(sp, e.otherType)});
            }
        }
        UNREACHABLE();
    };
    for (const auto& bound : resolve_.implGenerics().bounds) {
        DEBUG(StringView("IMPL - ") << bound);
        params.bounds.push_back(monomorphBound(bound));
    }
    for (const auto& bound : resolve_.itemGenerics().bounds) {
        DEBUG(StringView("ITEM - ") << bound);
        params.bounds.push_back(monomorphBound(bound));
    }
    return monomorphCb;
}

auto StaticBorrowExprVisitorMutate::extractNode(HIRExprNodeP& node, StaticTraitResolve& resolve, HIRGenericParams& paramsDef, HIRPathParams& constrParams, bool preserveGenericContext) -> HIRExprPtr {
    auto monomorph = this->createParams(node->span(), paramsDef, constrParams);
    resolve.setItemGenericsRaw(paramsDef);

    struct V: public HIRExprVisitorDef {
        const Span sp;
        const StaticTraitResolve& resolve;
        const Monomorph& monomorph;
        bool isGeneric;
        std::map<unsigned, unsigned> bindingMapping;

        V(const StaticTraitResolve& resolve, const Monomorph& monomorph)
            : HIRExprVisitorDef(resolve.hirCrate().types)
            , resolve(resolve)
            , monomorph(monomorph)
            , isGeneric(false)
        {
        }

        [[nodiscard]] const HIRType* visitType(const HIRType* ty) override {
            if (monomorphiseTypeNeeded(ty)) {
                this->isGeneric = true;
                auto newTy = this->resolve.monomorphExpand(sp, ty, this->monomorph);
                DEBUG(ty << StringView(" -> ") << newTy);
                return newTy;
            }
            return ty;
        }

        void visitPathParams(HIRPathParams& pp) override {
            if (monomorphisePathparamsNeeded(pp)) {
                this->isGeneric = true;
                auto newPp = this->monomorph.monomorphPathParams(sp, pp, false);
                DEBUG(pp << StringView(" -> ") << newPp);
                pp = std::move(newPp);
                for (auto& ty : pp.types) {
                    ty = this->resolve.expandAssociatedTypes(sp, ty);
                }
            }
        }

        void visitPattern(const Span& sp, HIRPattern& pat) override {
            HIRExprVisitorDef::visitPattern(sp, pat);
            for (auto& pb : pat.bindings) {
                auto idx = static_cast<unsigned>(bindingMapping.size());
                bindingMapping.insert(std::make_pair(pb.slot, idx));
                pb.slot = idx;
            }

            if (auto* e = pat.data.opt_SplitSlice()) {
                if (e->extraBind.isValid()) {
                    auto idx = static_cast<unsigned>(bindingMapping.size());
                    bindingMapping.insert(std::make_pair(e->extraBind.slot, idx));
                    e->extraBind.slot = idx;
                }
            }
        }

        void visit(HIRExprNodeVariable& node) override {
            HIRExprVisitorDef::visit(node);
            ASSERT_BUG(node.span(), bindingMapping.count(node.slot) != 0, StringView(""));
            node.slot = bindingMapping.at(node.slot);
        }

        void visit(HIRExprNodeClosure& node) override {
            HIRExprVisitorDef::visit(node);
            if (node.code) {
                for (auto& l : mutRange(node.avuCache.localVars)) {
                    ASSERT_BUG(node.span(), bindingMapping.count(l) != 0, StringView(""));
                    l = bindingMapping.at(l);
                }
                BUG_ASSERT(node.avuCache.capturedVars.empty());
            }
        }

        void visit(HIRExprNodeConstParam& node) override {
            node.binding = monomorph.getValue(node.span(), HIRGenericRef("", node.binding)).as_Generic().binding;
            isGeneric = true;
        }

        void visit(HIRExprNodeArraySized& node) override {
            if (auto* n = node.size.opt_Unevaluated()) {
                if (auto* g = n->opt_Generic()) {
                    *g = monomorph.getValue(node.span(), *g).as_Generic();
                    isGeneric = true;
                }
            }
        }
    } v(resolve, monomorph);

    node->visit(v);
    node->resType = v.visitType(node->resType);
    if (!v.isGeneric && !preserveGenericContext) {
        paramsDef = HIRGenericParams();
        constrParams = HIRPathParams();
        DEBUG(StringView("Concrete static"));
        DEBUG(StringView("Generic static"));
    }

    ASSERT_BUG(node->span(), exprPtr.state, StringView(""));
    auto valExpr = HIRExprPtr(mv$(node));
    valExpr.state = exprPtr.state.clone(resolve_.hirCrate().pool);
    valExpr.state->stage = HIRExprState::Stage::Sbc;

    valExpr.bindings.zero(v.bindingMapping.size());
    for (auto& e : v.bindingMapping) {
        valExpr.bindings.mut(e.second) = monomorph.monomorphType(valExpr->span(), exprPtr.bindings[e.first]);
    }
    return valExpr;
}

#ifdef NEWNODE
    #undef NEWNODE
#endif
#define NEWNODE(TY, CLASS, ...) mkExprnodep(resolve_.hirCrate().pool->make<HIRExprNode##CLASS>(__VA_ARGS__), TY)

auto StaticBorrowExprVisitorMutate::visit(HIRExprNodeBorrow& node) -> void {
    HIRExprVisitorDef::visit(node);

    if (node.isValidStaticBorrowConstant) {
        node.isValidStaticBorrowConstant = false;

        auto* valuePtrPtr = staticBorrowPromotionRoot(node.value);
        auto& valuePtr = *valuePtrPtr;
        if (auto* innerNode = cast<HIRExprNodeDeref>(valuePtrPtr->get())) {
            BUG(node.span(), StringView("Unexpected inner being deref?"));
            return;
        }
        auto usage = valuePtr->usage;

        auto newResTy = valuePtr->resType;
        DEBUG(StringView("-- Creating static"));
        StaticTraitResolve resolve{resolve_.board()};
        HIRGenericParams paramsDef;
        HIRPathParams constrParams;
        auto valExpr = extractNode(valuePtr, resolve, paramsDef, constrParams);

        auto sp = valExpr->span();

        auto staticTy = MonomorphLifetimesStatic(resolve_.hirCrate().types).monomorphType(sp, valExpr->resType, /*allow_infer=*/false);
        staticTy = resolve.expandAssociatedTypes(sp, staticTy);

        auto path = newStaticCb.create(sp, mv$(staticTy), mv$(valExpr), mv$(paramsDef), false);
        DEBUG(StringView("> ") << path << constrParams);
        auto newNode = NEWNODE(std::move(newResTy), PathValue, sp, HIRGenericPath(mv$(path), mv$(constrParams)), HIRExprNodePathValue::STATIC);
        newNode->usage = usage;
        valuePtr = mv$(newNode);
    }
}

#undef NEWNODE

#ifdef NEWNODE
    #undef NEWNODE
#endif
#define NEWNODE(TY, CLASS, ...) mkExprnodep(resolve_.hirCrate().pool->make<HIRExprNode##CLASS>(__VA_ARGS__), TY)

auto StaticBorrowExprVisitorMutate::visit(HIRExprNodeConstBlock& node) -> void {
    HIRExprVisitorDef::visit(node);

    if (!cast<HIRExprNodePathValue>(node.inner.get())) {
        DEBUG(StringView("-- Creating const"));
        auto usage = node.inner->usage;

        StaticTraitResolve resolve{resolve_.board()};
        HIRGenericParams paramsDef;
        HIRPathParams constrParams;
        auto valExpr = extractNode(node.inner, resolve, paramsDef, constrParams, true);

        auto sp = valExpr->span();

        auto staticTy = MonomorphLifetimesStatic(resolve_.hirCrate().types).monomorphType(sp, valExpr->resType, /*allow_infer=*/false);
        staticTy = resolve.expandAssociatedTypes(sp, staticTy);

        DEBUG(StringView("ConstBlock: static_ty = ") << staticTy);
        auto m2 = MonomorphStatePtr(resolve_.hirCrate().types, nullptr, nullptr, &constrParams);
        auto newResTy = m2.monomorphType(sp, staticTy, false);

        DEBUG(StringView("ConstBlock: new_res_ty = ") << newResTy);
        auto path = newStaticCb.create(sp, mv$(staticTy), mv$(valExpr), mv$(paramsDef), true);
        DEBUG(StringView("> ") << path << constrParams);
        auto newNode = NEWNODE(std::move(newResTy), PathValue, sp, HIRGenericPath(std::move(path), mv$(constrParams)), HIRExprNodePathValue::CONSTANT);
        newNode->usage = usage;
        node.inner = mv$(newNode);
    }
}

#undef NEWNODE

StaticBorrowExprVisitorMutate::Monomorph::Monomorph(HIRTypeInterner& types, const HIRGenericParams& params, unsigned ofsImplT, unsigned ofsItemT, unsigned ofsImplV, unsigned ofsItemV)
    : Monomorphiser(types)
    , params(params)
    , ofsImplT(ofsImplT)
    , ofsItemT(ofsItemT)
    , ofsImplV(ofsImplV)
    , ofsItemV(ofsItemV)
{
}

auto StaticBorrowExprVisitorMutate::Monomorph::getType(const Span& sp, const HIRGenericRef& ge) const -> const HIRType* {
    unsigned i;
    if (ge.binding == 0xFFFF) {
        i = 0;
    } else if (ge.binding < 256) {
        i = ofsImplT + ge.idx();
    } else if (ge.binding < 2 * 256) {
        i = ofsItemT + ge.idx();
    } else {
        BUG(sp, StringView("Generic type ") << ge << StringView(" unknown"));
    }
    ASSERT_BUG(sp, i < params.types.size(), StringView("Item generic type binding OOR - ") << ge << StringView(" (") << i << StringView(" !< ") << params.types.size() << StringView(")"));
    return types.generic(params.types[i].name, 256 + i);
}

auto StaticBorrowExprVisitorMutate::Monomorph::getValue(const Span& sp, const HIRGenericRef& ge) const -> HIRConstGeneric {
    unsigned i;
    if (ge.binding == 0xFFFF) {
        BUG(sp, StringView("Binding 0xFFFF isn't valid for values"));
    } else if (ge.binding < 256) {
        i = ofsImplV + ge.idx();
    } else if (ge.binding < 2 * 256) {
        i = ofsItemV + ge.idx();
    } else {
        BUG(sp, StringView("Generic value ") << ge << StringView(" unknown"));
    }
    ASSERT_BUG(sp, i < params.values.size(), StringView("Item generic value binding OOR - ") << ge << StringView(" (") << i << StringView(" !< ") << params.values.size() << StringView(")"));
    return HIRGenericRef(params.values[i].name, 256 + i);
}

StaticBorrowExprVisitorMutate::MonomorphLifetimesStatic::MonomorphLifetimesStatic(HIRTypeInterner& types)
    : Monomorphiser(types)
{
}

auto StaticBorrowExprVisitorMutate::MonomorphLifetimesStatic::getType(const Span& sp, const HIRGenericRef& g) const -> const HIRType* {
    return types.generic(g.name, g.binding);
}

auto StaticBorrowExprVisitorMutate::MonomorphLifetimesStatic::getValue(const Span& sp, const HIRGenericRef& g) const -> HIRConstGeneric {
    return g;
}

StaticBorrowOuterVisitor::StaticBorrowOuterVisitor(const WireBoard& wb)
    : HIRVisitor(nullptr, wb.crate->types)
    , crate(*wb.crate)
    , resolve_(wb)
    , currentModule(nullptr)
{
}

auto StaticBorrowOuterVisitor::create(Span sp, const HIRType* ty, HIRExprPtr valExpr, HIRGenericParams generics, bool isConst) -> HIRSimplePath {
    ASSERT_BUG(sp, currentModule, StringView(""));
    auto& list = newStatics[currentModule];
    auto idx = list.size();
    auto name = RcString::newInterned(FMT((isConst ? "lifted#" : "const#") << idx));
    auto path = (*currentModulePath + name).getSimplePath();
    auto newStatic = HIRStatic(
        HIRLinkage(),
        /*is_mut=*/false,
        mv$(ty),
        /*m_value=*/mv$(valExpr)
    );
    newStatic.params = mv$(generics);
    newStatic.isPromoted = true;
    newStatic.saveLiteral = !newStatic.params.isGeneric();
    DEBUG(path << StringView(" = ") << newStatic.valueRes);
    list.push_back(NewStatic{path, std::move(newStatic), isConst});
    return path;
}

auto StaticBorrowOuterVisitor::visitCrate(HIRCrate& crate) -> void {
    HIRVisitor::visitCrate(crate);

    for (auto& modList : newStatics) {
        auto& mod = *modList.first;
        currentModule = &mod;

        HIRSimplePath modPath;
        if (!modList.second.empty()) {
            modPath = modList.second[0].path;
            modPath.popComponent();
        }

        struct Nvs: HIREvaluator::Newval {
            ObjPool& pool;
            HIRSimplePath currentModulePath;
            HIRModule& currentModule;
            size_t nextIdx;

            Nvs(ObjPool& pool, HIRSimplePath currentModulePath, HIRModule& currentModule, size_t idx)
                : pool(pool)
                , currentModulePath(std::move(currentModulePath))
                , currentModule(currentModule)
                , nextIdx(idx)
            {
            }

            HIRPath newStatic(const HIRType* type, EncodedLiteral value, size_t alignment) override {
                auto name = RcString::newInterned(FMT(StringView("lifted#") << nextIdx));
                nextIdx++;
                auto path = currentModulePath + name;
                auto newStatic = HIRStatic(
                    HIRLinkage(),
                    /*is_mut=*/false,
                    std::move(type),
                    /*m_value=*/HIRExprPtr()
                );
                newStatic.explicitAlignment = alignment;
                newStatic.valueGenerated = true;
                newStatic.isPromoted = true;
                newStatic.valueRes = std::move(value);
                currentModule.valueItems.insert(std::make_pair(name, pool.make<HIRVisEnt<HIRValueItem>>(HIRVisEnt<HIRValueItem>{HIRPublicity::newNone(), HIRValueItem(pool.make<HIRStatic>(std::move(newStatic)))})));
                return path;
            }
        } nvs{*crate.pool, modPath, mod, modList.second.size()};

        struct H {
            static HIRConstant toConst(HIRStatic& s) {
                HIRConstant rv{std::move(s.params), std::move(s.type), std::move(s.value)};
                rv.valueState = rv.params.isGeneric() ? HIRConstant::ValueState::Generic : HIRConstant::ValueState::Unknown;
                return rv;
            }
        };

        for (auto& newStaticPair : modList.second) {
            auto& newStatic = newStaticPair.data;
            auto newEnt = newStaticPair.isConst ? HIRValueItem(crate.pool->make<HIRConstant>(H::toConst(newStatic))) : HIRValueItem(crate.pool->make<HIRStatic>(std::move(newStaticPair.data)));
            auto inserted = mod.valueItems.insert(std::make_pair(newStaticPair.path.components().back(), crate.pool->make<HIRVisEnt<HIRValueItem>>(HIRVisEnt<HIRValueItem>{HIRPublicity::newNone(), std::move(newEnt)})));
            ASSERT_BUG(Span(), inserted.second, StringView("Duplicate promoted value ") << newStaticPair.path);
        }

        for (const auto& newStaticPair : modList.second) {
            Span sp;
            auto& value = mod.valueItems.at(newStaticPair.path.components().back())->ent;
            if (auto* newConstP = value.opt_Constant()) {
                auto* newConst = *newConstP;
                TRACE_FUNCTION_F(StringView("New constant ") << newStaticPair.path << newConst->params.fmtArgs());
                if (newConst->valueState == HIRConstant::ValueState::Unknown) {
                    newConst->value.state->stage = HIRExprState::Stage::Sbc;
                    newConst->valueRes = HIREvaluator(sp, this->resolve_.board(), nvs).evaluateConstant(newStaticPair.path, newConst->value, newConst->type);
                    newConst->valueState = HIRConstant::ValueState::Known;
                }
            } else {
                auto& newStatic = *value.as_Static();
                TRACE_FUNCTION_F(StringView("New static ") << newStaticPair.path << newStatic.params.fmtArgs());
                if (!newStatic.params.isGeneric() && !newStatic.valueGenerated) {
                    newStatic.value.state->stage = HIRExprState::Stage::Sbc;
                    newStatic.valueRes = HIREvaluator(sp, this->resolve_.board(), nvs).evaluateConstant(newStaticPair.path, newStatic.value, newStatic.type);
                    newStatic.valueGenerated = true;
                }
            }
        }
    }
}

auto StaticBorrowOuterVisitor::visitModule(HIRItemPath p, HIRModule& mod) -> void {
    auto par = currentModule;
    auto parP = currentModulePath;
    currentModule = &mod;
    currentModulePath = &p;

    HIRVisitor::visitModule(p, mod);

    currentModule = par;
    currentModulePath = parP;
}

auto StaticBorrowOuterVisitor::visitTrait(HIRItemPath p, HIRTrait& item) -> void {
    auto self = this->crate.types.self();
    selfType = self;
    auto _ = resolve_.setImplGenerics(MetadataType::TraitObject, item.params);
    HIRVisitor::visitTrait(p, item);
    selfType = nullptr;
}

auto StaticBorrowOuterVisitor::visitTypeImpl(HIRTypeImpl& impl) -> void {
    auto& srcmod = this->crate.getModByPathMut(Span(), impl.srcModule);
    auto modIp = HIRItemPath(impl.srcModule);
    selfType = impl.type;
    currentModule = &srcmod;
    currentModulePath = &modIp;

    auto _ = resolve_.setImplGenerics(impl.type, impl.params);
    HIRVisitor::visitTypeImpl(impl);

    currentModule = nullptr;
    selfType = nullptr;
}

auto StaticBorrowOuterVisitor::visitTraitImpl(const HIRSimplePath& traitPath, HIRTraitImpl& impl) -> void {
    auto& srcmod = this->crate.getModByPathMut(Span(), impl.srcModule);
    auto modIp = HIRItemPath(impl.srcModule);
    selfType = impl.type;
    currentModule = &srcmod;
    currentModulePath = &modIp;

    auto _ = resolve_.setImplGenerics(impl.type, impl.params);
    HIRVisitor::visitTraitImpl(traitPath, impl);

    currentModule = nullptr;
    selfType = nullptr;
}

auto StaticBorrowOuterVisitor::visitExpr(HIRExprPtr& exp) -> void {
    BUG(Span(), StringView("visit_expr hit in StaticBorrowOuterVisitor"));
}

auto StaticBorrowOuterVisitor::visitConstgeneric(HIRConstGeneric& c) -> void {
    if (auto* e = c.opt_Unevaluated()) {
        StaticBorrowExprVisitorMutate ev(resolve_, selfType, *this, *(*e)->expr);
        ev.visitNodePtr(*(*e)->expr);
    }
}

[[nodiscard]] auto StaticBorrowOuterVisitor::visitType(const HIRType* ty) -> const HIRType* {
    return visitTypeDefaultViaHooks(ty);
}

auto StaticBorrowOuterVisitor::visitFunction(HIRItemPath p, HIRFunction& item) -> void {
    if (item.code) {
        auto _ = resolve_.setItemGenerics(item.params);
        isConst = item.isConst;
        DEBUG(StringView("Function code ") << p);
        StaticBorrowExprVisitorMutate ev(resolve_, selfType, *this, item.code);
        ev.visitNodePtr(item.code);
        isConst = false;
        DEBUG(StringView("Function code ") << p << StringView(" (none)"));
    }
}

auto StaticBorrowOuterVisitor::visitStatic(HIRItemPath p, HIRStatic& item) -> void {
    if (item.value) {
        StaticBorrowExprVisitorMutate ev(resolve_, selfType, *this, item.value);
        ev.visitNodePtr(item.value);

        if (!item.isMut && resolve_.typeIsCopy(item.value->span(), item.type) && resolve_.typeIsInteriorMutable(item.value->span(), item.type) == InteriorMutability::No) {
            item.saveLiteral = true;
        }
    }
}

auto StaticBorrowOuterVisitor::visitConstant(HIRItemPath p, HIRConstant& item) -> void {
    if (item.value) {
        isConst = true;
        StaticBorrowExprVisitorMutate ev(resolve_, selfType, *this, item.value);
        ev.visitNodePtr(item.value);
        isConst = false;
    }
}

auto StaticBorrowOuterVisitor::visitEnum(HIRItemPath p, HIREnum& item) -> void {
    if (auto* e = item.data.opt_Value()) {
        auto _ = resolve_.setImplGenerics(MetadataType::None, item.params);
        for (auto& var : e->variants) {
            DEBUG(StringView("Enum value ") << p << StringView(" - ") << var.name);
            if (var.expr) {
                StaticBorrowExprVisitorMutate ev(resolve_, selfType, *this, var.expr);
                ev.visitNodePtr(var.expr);
            }
        }
    }
}

UfcsExprVisitorMutate::UfcsExprVisitorMutate(const WireBoard& wb, const HIRTraitImpl* currentTraitImpl)
    : HIRExprVisitorDef(wb.crate->types)
    , crate(*wb.crate)
    , currentTraitImpl(currentTraitImpl)
    , resolve_(wb)
{
    if (crate.langItems.count("owned_box") > 0) {
        langBox_ = crate.langItems.at("owned_box");
    }
}

auto UfcsExprVisitorMutate::visitNodePtr(HIRExprPtr& root) -> void {
    const auto& nodeRef = *root;
    const char* nodeTy = typeid(nodeRef).name();
    TRACE_FUNCTION_FR(static_cast<const void*>(&*root) << StringView(" ") << nodeTy << StringView(" : ") << root->resType, nodeTy);
    root->visit(*this);
    if (replacement_) {
        auto usage = root->usage;
        const auto* ptr = replacement_.get();
        DEBUG(StringView("=> REPLACE ") << static_cast<const void*>(ptr) << StringView(" ") << typeid(*ptr).name());
        root.reset(replacement_.release());
        root->usage = usage;
    }
}

auto UfcsExprVisitorMutate::visitNodePtr(HIRExprNodeP& node) -> void {
    const auto& nodeRef = *node;
    const char* nodeTy = typeid(nodeRef).name();
    TRACE_FUNCTION_FR(static_cast<const void*>(&*node) << StringView(" ") << nodeTy << StringView(" : ") << node->resType, nodeTy);
    BUG_ASSERT(node);
    node->visit(*this);
    if (replacement_) {
        auto usage = node->usage;
        const auto* ptr = replacement_.get();
        DEBUG(StringView("=> REPLACE ") << static_cast<const void*>(ptr) << StringView(" ") << typeid(*ptr).name());
        node = mv$(replacement_);
        node->usage = usage;
    }
}

#ifdef NEWNODE
    #undef NEWNODE
#endif
#define NEWNODE(TY, CLASS, ...) ufcsMkExprnodep(crate.pool->make<HIRExprNode##CLASS>(__VA_ARGS__), TY)

auto UfcsExprVisitorMutate::visit(HIRExprNodeUse& node) -> void {
    const auto& sp = node.span();
    HIRExprVisitorDef::visit(node);

    const auto* type = node.value->resType;
    if (resolve_.typeIsCopy(sp, type) || !typeIsUseCloned(resolve_, sp, type)) {
        replacement_ = mv$(node.value);
        return;
    }

    auto borrowType = crate.types.borrow(HIRBorrowType::Shared, type);
    auto borrowNode = NEWNODE(borrowType, Borrow, sp, HIRBorrowType::Shared, mv$(node.value));
    auto* cloneCall = crate.pool->make<HIRExprNodeCallPath>(sp, HIRPath(type, HIRGenericPath(crate.getLangItemPath(sp, "clone")), RcString("clone")), makeVec1(mv$(borrowNode)));
    replacement_ = ufcsMkExprnodep(cloneCall, node.resType);
    cloneCall->cache.argTypes.clear();
    cloneCall->cache.argTypes.pushBack(borrowType);
    cloneCall->cache.argTypes.pushBack(node.resType);
}

#undef NEWNODE

#ifdef NEWNODE
    #undef NEWNODE
#endif
#define NEWNODE(TY, CLASS, ...) ufcsMkExprnodep(crate.pool->make<HIRExprNode##CLASS>(__VA_ARGS__), TY)

auto UfcsExprVisitorMutate::visit(HIRExprNodeCallValue& node) -> void {
    const auto& sp = node.span();

    HIRExprVisitorDef::visit(node);
    const auto& tyVal = node.value->resType;

    if (tyVal->is_Function()) {
        return;
    }

    const HIRType* argTupType;
    {
        Vector<const HIRType*> argTypes;
        for (unsigned int i = 0; i < node.args.size(); i++) {
            argTypes.pushBack(node.args[i]->resType);
        }
        argTupType = crate.types.tuple(mv$(argTypes));
    }
    HIRPathParams traitArgs;
    traitArgs.types.push_back(argTupType);

    // TODO: You can call via &-ptrs, but that currently isn't handled in typeck
    if (const auto* nodePp = ((*node.value->resType).is_NodeType() ? ((*node.value->resType).as_NodeType().opt_Closure()) : nullptr)) {
        if (node.traitUsed == HIRExprNodeCallValue::TraitUsed::Unknown) {
            switch ((*nodePp)->cls) {
                case HIRExprNodeClosure::Class::Unknown:
                    BUG(sp, StringView("References an ::Unknown closure"));
                case HIRExprNodeClosure::Class::NoCapture:
                case HIRExprNodeClosure::Class::Shared:
                    if (!crate.getLangItemPathOpt("fn").components().empty()) {
                        node.traitUsed = HIRExprNodeCallValue::TraitUsed::Fn;
                    } else if (!crate.getLangItemPathOpt("fn_mut").components().empty()) {
                        node.traitUsed = HIRExprNodeCallValue::TraitUsed::FnMut;
                    } else {
                        node.traitUsed = HIRExprNodeCallValue::TraitUsed::FnOnce;
                    }
                    break;
                case HIRExprNodeClosure::Class::Mut:
                    node.traitUsed = !crate.getLangItemPathOpt("fn_mut").components().empty() ? HIRExprNodeCallValue::TraitUsed::FnMut : HIRExprNodeCallValue::TraitUsed::FnOnce;
                    break;
                case HIRExprNodeClosure::Class::Once:
                    node.traitUsed = HIRExprNodeCallValue::TraitUsed::FnOnce;
                    break;
            }
        }
    }

    const HIRType* selfArgType;
    HIRPath methodPath(HIRSimplePath{});
    switch (node.traitUsed) {
        case HIRExprNodeCallValue::TraitUsed::Fn:
            selfArgType = crate.types.borrow(HIRBorrowType::Shared, tyVal);
            node.value = NEWNODE(selfArgType, Borrow, sp, HIRBorrowType::Shared, mv$(node.value));
            methodPath = HIRPath(tyVal, HIRGenericPath(crate.getLangItemPath(sp, "fn"), mv$(traitArgs)), RcString::newInterned("call"), HIRPathParams());
            break;
        case HIRExprNodeCallValue::TraitUsed::FnMut:
            selfArgType = crate.types.borrow(HIRBorrowType::Unique, tyVal);
            node.value = NEWNODE(selfArgType, Borrow, sp, HIRBorrowType::Unique, mv$(node.value));
            methodPath = HIRPath(tyVal, HIRGenericPath(crate.getLangItemPath(sp, "fn_mut"), mv$(traitArgs)), RcString::newInterned("call_mut"), HIRPathParams());
            break;
        case HIRExprNodeCallValue::TraitUsed::FnOnce:
            selfArgType = tyVal;
            methodPath = HIRPath(tyVal, HIRGenericPath(crate.getLangItemPath(sp, "fn_once"), mv$(traitArgs)), RcString::newInterned("call_once"));
            break;
        case HIRExprNodeCallValue::TraitUsed::AsyncFn:
            selfArgType = crate.types.borrow(HIRBorrowType::Shared, tyVal);
            node.value = NEWNODE(selfArgType, Borrow, sp, HIRBorrowType::Shared, mv$(node.value));
            methodPath = HIRPath(tyVal, HIRGenericPath(crate.getLangItemPath(sp, "fn"), mv$(traitArgs)), RcString::newInterned("call"), HIRPathParams());
            break;
        case HIRExprNodeCallValue::TraitUsed::AsyncFnMut:
            selfArgType = crate.types.borrow(HIRBorrowType::Unique, tyVal);
            node.value = NEWNODE(selfArgType, Borrow, sp, HIRBorrowType::Unique, mv$(node.value));
            methodPath = HIRPath(tyVal, HIRGenericPath(crate.getLangItemPath(sp, "fn_mut"), mv$(traitArgs)), RcString::newInterned("call_mut"), HIRPathParams());
            break;
        case HIRExprNodeCallValue::TraitUsed::AsyncFnOnce:
            selfArgType = tyVal;
            methodPath = HIRPath(tyVal, HIRGenericPath(crate.getLangItemPath(sp, "fn_once"), mv$(traitArgs)), RcString::newInterned("call_once"));
            break;

        default:
            BUG(node.span(), StringView("Encountered CallValue with TraitUsed::Unknown, ty=") << node.value->resType);
    }
    BUG_ASSERT(selfArgType != nullptr);

    std::vector<HIRExprNodeP> args;
    args.reserve(2);
    args.push_back(mv$(node.value));
    args.push_back(NEWNODE(argTupType, Tuple, sp, mv$(node.args)));

    auto* replacement = crate.pool->make<HIRExprNodeCallPath>(sp, mv$(methodPath), mv$(args));
    replacement_ = ufcsMkExprnodep(replacement, mv$(node.resType));

    auto& argTypes = replacement->cache.argTypes;
    argTypes.pushBack(selfArgType);
    argTypes.pushBack(argTupType);
    argTypes.pushBack(replacement_->resType);
}

#undef NEWNODE

auto UfcsExprVisitorMutate::visit(HIRExprNodeCallMethod& node) -> void {
    const auto& sp = node.span();

    HIRExprVisitorDef::visit(node);

    std::vector<HIRExprNodeP> args;
    args.reserve(1 + node.args.size());
    args.push_back(mv$(node.value));
    for (auto& arg : node.args) {
        args.push_back(mv$(arg));
    }

    auto* replacement = crate.pool->make<HIRExprNodeCallPath>(sp, mv$(node.methodPath), mv$(args));
    replacement_ = ufcsMkExprnodep(replacement, mv$(node.resType));
    replacement->cache = mv$(node.cache);
}

auto UfcsExprVisitorMutate::isBuiltinOperator(const Span& sp, TypeckPrimitiveOperator op, const char* langitem, const HIRType* tyL, const HIRType* tyR) const -> bool {
    if (!primitiveOperatorHasBuiltin(op, tyL, tyR)) {
        return false;
    }

    if (!crate.isNoCore) {
        return true;
    }

    HIRPathParams traitParams(tyR);
    const auto& traitPath = crate.getLangItemPathOpt(langitem);
    if (traitPath.components().empty()) {
        return true;
    }
    return !resolve_.probeImplMayApply(sp, traitPath, traitParams, tyL, [&](SolverMayApply probe) {
        return !probe.candidate || !(currentTraitImpl && probe.candidate->traitImpl == currentTraitImpl);
    });
}

auto UfcsExprVisitorMutate::isBuiltinOperator(const Span& sp, TypeckPrimitiveOperator op, const char* langitem, const HIRType* ty) const -> bool {
    if (!primitiveOperatorHasBuiltin(op, ty)) {
        return false;
    }

    if (!crate.isNoCore) {
        return true;
    }

    const auto& traitPath = crate.getLangItemPathOpt(langitem);
    if (traitPath.components().empty()) {
        return true;
    }
    return !resolve_.probeImplMayApply(sp, traitPath, HIRPathParams(), ty, [&](SolverMayApply probe) {
        return !probe.candidate || !(currentTraitImpl && probe.candidate->traitImpl == currentTraitImpl);
    });
}

#ifdef NEWNODE
    #undef NEWNODE
#endif
#define NEWNODE(TY, CLASS, ...) ufcsMkExprnodep(crate.pool->make<HIRExprNode##CLASS>(__VA_ARGS__), TY)

auto UfcsExprVisitorMutate::visit(HIRExprNodeAssign& node) -> void {
    const auto& sp = node.span();
    HIRExprVisitorDef::visit(node);

    const auto& tySlot = node.slot->resType;
    const auto& tyVal = node.value->resType;

    const char* langitem = nullptr;
    const char* opname = nullptr;
    auto operatorKind = TypeckPrimitiveOperator::None;
#define _(opname) case HIRExprNodeAssign::Op::opname
    switch (node.op) {
        _(None)
            : ASSERT_BUG(sp, resolve_.typesEqualResolvingOpaque(sp, tySlot, tyVal), StringView("Types must equal for non-operator assignment, ") << tySlot << StringView(" != ") << tyVal);
        return;
        _(Shr)
            : {
            langitem = "shr_assign";
            opname = "shr_assign";
            operatorKind = TypeckPrimitiveOperator::ShrAssign;
        }
        if (0)
        {
            _(Shl)
                : {
                langitem = "shl_assign";
                opname = "shl_assign";
                operatorKind = TypeckPrimitiveOperator::ShlAssign;
            }
        }
        if (isBuiltinOperator(sp, operatorKind, langitem, tySlot, tyVal)) {
            return;
        }
        break;

        _(And)
            : {
            langitem = "bitand_assign";
            opname = "bitand_assign";
            operatorKind = TypeckPrimitiveOperator::BitAndAssign;
        }
        if (0) {
            _(Or)
                : {
                langitem = "bitor_assign";
                opname = "bitor_assign";
                operatorKind = TypeckPrimitiveOperator::BitOrAssign;
            }
        }
        if (0) {
            _(Xor)
                : {
                langitem = "bitxor_assign";
                opname = "bitxor_assign";
                operatorKind = TypeckPrimitiveOperator::BitXorAssign;
            }
        }
        if (isBuiltinOperator(sp, operatorKind, langitem, tySlot, tyVal)) {
            return;
        }
        break;

        _(Add)
            : {
            langitem = "add_assign";
            opname = "add_assign";
            operatorKind = TypeckPrimitiveOperator::AddAssign;
        }
        if (0) {
            _(Sub)
                : {
                langitem = "sub_assign";
                opname = "sub_assign";
                operatorKind = TypeckPrimitiveOperator::SubAssign;
            }
        }
        if (0) {
            _(Mul)
                : {
                langitem = "mul_assign";
                opname = "mul_assign";
                operatorKind = TypeckPrimitiveOperator::MulAssign;
            }
        }
        if (0) {
            _(Div)
                : {
                langitem = "div_assign";
                opname = "div_assign";
                operatorKind = TypeckPrimitiveOperator::DivAssign;
            }
        }
        if (0) {
            _(Mod)
                : {
                langitem = "rem_assign";
                opname = "rem_assign";
                operatorKind = TypeckPrimitiveOperator::RemAssign;
            }
        }
        if (isBuiltinOperator(sp, operatorKind, langitem, tySlot, tyVal)) {
            return;
        }
        break;
    }
#undef _
    BUG_ASSERT(langitem);
    BUG_ASSERT(opname);

    HIRGenericPath trait{crate.getLangItemPath(node.span(), langitem), HIRPathParams(tyVal)};

    auto slotTypeRefmut = crate.types.borrow(HIRBorrowType::Unique, tySlot);
    std::vector<HIRExprNodeP> args;
    args.push_back(NEWNODE(slotTypeRefmut, Borrow, sp, HIRBorrowType::Unique, mv$(node.slot)));
    args.push_back(mv$(node.value));
    auto* replacement = crate.pool->make<HIRExprNodeCallPath>(sp, HIRPath(tySlot, mv$(trait), RcString::newInterned(opname), HIRPathParams()), mv$(args));
    replacement_ = ufcsMkExprnodep(replacement, mv$(node.resType));

    auto& argTypes = replacement->cache.argTypes;
    argTypes.pushBack(slotTypeRefmut);
    argTypes.pushBack(tyVal);
    argTypes.pushBack(crate.types.unit());
}

#undef NEWNODE

#ifdef NEWNODE
    #undef NEWNODE
#endif
#define NEWNODE(TY, CLASS, ...) ufcsMkExprnodep(crate.pool->make<HIRExprNode##CLASS>(__VA_ARGS__), TY)

auto UfcsExprVisitorMutate::visit(HIRExprNodeBinOp& node) -> void {
    const auto& sp = node.span();
    HIRExprVisitorDef::visit(node);

    const auto& tyL = node.left->resType;
    const auto& tyR = node.right->resType;

    const char* langitem = nullptr;
    const char* method = nullptr;
    bool isComparison = false;
    auto operatorKind = TypeckPrimitiveOperator::None;
    switch (node.op) {
        case HIRExprNodeBinOp::Op::CmpEqu:
            langitem = "eq";
            method = "eq";
            isComparison = true;
            operatorKind = TypeckPrimitiveOperator::Equal;
            break;
        case HIRExprNodeBinOp::Op::CmpNEqu:
            langitem = "eq";
            method = "ne";
            isComparison = true;
            operatorKind = TypeckPrimitiveOperator::Equal;
            break;
        case HIRExprNodeBinOp::Op::CmpLt:
            langitem = "partial_ord";
            method = "lt";
            isComparison = true;
            operatorKind = TypeckPrimitiveOperator::Order;
            break;
        case HIRExprNodeBinOp::Op::CmpLtE:
            langitem = "partial_ord";
            method = "le";
            isComparison = true;
            operatorKind = TypeckPrimitiveOperator::Order;
            break;
        case HIRExprNodeBinOp::Op::CmpGt:
            langitem = "partial_ord";
            method = "gt";
            isComparison = true;
            operatorKind = TypeckPrimitiveOperator::Order;
            break;
        case HIRExprNodeBinOp::Op::CmpGtE:
            langitem = "partial_ord";
            method = "ge";
            isComparison = true;
            operatorKind = TypeckPrimitiveOperator::Order;
            break;

        case HIRExprNodeBinOp::Op::Xor:
            langitem = method = "bitxor";
            operatorKind = TypeckPrimitiveOperator::BitXor;
            if (0) {
                case HIRExprNodeBinOp::Op::Or:
                    langitem = method = "bitor";
                    operatorKind = TypeckPrimitiveOperator::BitOr;
            }
            if (0) {
                case HIRExprNodeBinOp::Op::And:
                    langitem = method = "bitand";
                    operatorKind = TypeckPrimitiveOperator::BitAnd;
            }
            break;

        case HIRExprNodeBinOp::Op::Shr:
            langitem = method = "shr";
            operatorKind = TypeckPrimitiveOperator::Shr;
            if (0) {
                case HIRExprNodeBinOp::Op::Shl:
                    langitem = method = "shl";
                    operatorKind = TypeckPrimitiveOperator::Shl;
            }
            break;

        case HIRExprNodeBinOp::Op::Add:
            langitem = method = "add";
            operatorKind = TypeckPrimitiveOperator::Add;
            if (0) {
                case HIRExprNodeBinOp::Op::Sub:
                    langitem = method = "sub";
                    operatorKind = TypeckPrimitiveOperator::Sub;
            }
            if (0) {
                case HIRExprNodeBinOp::Op::Mul:
                    langitem = method = "mul";
                    operatorKind = TypeckPrimitiveOperator::Mul;
            }
            if (0) {
                case HIRExprNodeBinOp::Op::Div:
                    langitem = method = "div";
                    operatorKind = TypeckPrimitiveOperator::Div;
            }
            if (0) {
                case HIRExprNodeBinOp::Op::Mod:
                    langitem = method = "rem";
                    operatorKind = TypeckPrimitiveOperator::Rem;
            }
            break;

        case HIRExprNodeBinOp::Op::BoolAnd:
        case HIRExprNodeBinOp::Op::BoolOr:
            ASSERT_BUG(sp, tyL == crate.types.primitive(HIRCoreType::Bool) || tyL->is_Diverge(), StringView("Boolean operator requires bool"));
            ASSERT_BUG(sp, tyR == crate.types.primitive(HIRCoreType::Bool) || tyR->is_Diverge(), StringView("Boolean operator requires bool"));
            return;
    }

    if (isBuiltinOperator(sp, operatorKind, langitem, tyL, tyR)) {
        return;
    }

    if (isComparison) {
        HIRPathParams traitParams;
        traitParams.types.push_back(tyR);
        HIRGenericPath trait{crate.getLangItemPath(node.span(), langitem), mv$(traitParams)};
        HIRPathParams fcnParams;

        auto tyLRef = crate.types.borrow(HIRBorrowType::Shared, tyL);
        auto tyRRef = crate.types.borrow(HIRBorrowType::Shared, tyR);

        std::vector<HIRExprNodeP> args;
        auto spLeft = node.left->span();
        auto spRight = node.right->span();
        args.push_back(NEWNODE(tyLRef, Borrow, spLeft, HIRBorrowType::Shared, mv$(node.left)));
        args.push_back(NEWNODE(tyRRef, Borrow, spRight, HIRBorrowType::Shared, mv$(node.right)));

        auto* replacement = crate.pool->make<HIRExprNodeCallPath>(sp, HIRPath(tyL, mv$(trait), RcString::newInterned(method), mv$(fcnParams)), mv$(args));
        replacement_ = ufcsMkExprnodep(replacement, mv$(node.resType));

        auto& argTypes = replacement->cache.argTypes;
        argTypes.pushBack(tyLRef);
        argTypes.pushBack(tyRRef);
        argTypes.pushBack(crate.types.primitive(HIRCoreType::Bool));
        return;
    }

    BUG_ASSERT(langitem);
    BUG_ASSERT(method);

    HIRPathParams traitParams;
    traitParams.types.push_back(tyR);
    HIRGenericPath trait{crate.getLangItemPath(node.span(), langitem), mv$(traitParams)};

    std::vector<HIRExprNodeP> args;
    args.push_back(mv$(node.left));
    args.push_back(mv$(node.right));

    auto* replacement = crate.pool->make<HIRExprNodeCallPath>(sp, HIRPath(tyL, mv$(trait), RcString::newInterned(method)), mv$(args));
    replacement_ = ufcsMkExprnodep(replacement, mv$(node.resType));

    auto& argTypes = replacement->cache.argTypes;
    argTypes.pushBack(tyL);
    argTypes.pushBack(tyR);
    argTypes.pushBack(replacement_->resType);
}

#undef NEWNODE

auto UfcsExprVisitorMutate::visit(HIRExprNodeUniOp& node) -> void {
    const auto& sp = node.span();
    HIRExprVisitorDef::visit(node);

    const auto& tyVal = node.value->resType;

    const char* langitem = nullptr;
    const char* method = nullptr;
    auto operatorKind = TypeckPrimitiveOperator::None;
    switch (node.op) {
        case HIRExprNodeUniOp::Op::Invert:
            langitem = method = "not";
            operatorKind = TypeckPrimitiveOperator::Not;
            break;
        case HIRExprNodeUniOp::Op::Negate:
            langitem = method = "neg";
            operatorKind = TypeckPrimitiveOperator::Neg;
            break;
    }
    BUG_ASSERT(langitem);
    BUG_ASSERT(method);

    if (tyVal->is_Diverge()) {
        return;
    }
    if (isBuiltinOperator(sp, operatorKind, langitem, tyVal)) {
        return;
    }

    HIRGenericPath trait{crate.getLangItemPath(node.span(), langitem), {}};

    std::vector<HIRExprNodeP> args;
    args.push_back(mv$(node.value));

    auto* replacement = crate.pool->make<HIRExprNodeCallPath>(sp, HIRPath(tyVal, mv$(trait), RcString::newInterned(method)), mv$(args));
    replacement_ = ufcsMkExprnodep(replacement, mv$(node.resType));

    auto& argTypes = replacement->cache.argTypes;
    argTypes.pushBack(tyVal);
    argTypes.pushBack(replacement_->resType);
}

auto UfcsExprVisitorMutate::visit(HIRExprNodeUnsize& node) -> void {
    HIRExprVisitorDef::visit(node);

    const auto* srcBorrow = node.value->resType->opt_Borrow();
    const auto* dstBorrow = node.resType->opt_Borrow();
    auto* borrowNode = cast<HIRExprNodeBorrow>(node.value.get());
    if (node.isArrayToSliceAdjustment) {
        const Span& sp = node.span();
        ASSERT_BUG(sp, borrowNode && srcBorrow && dstBorrow, StringView("Malformed array-to-slice adjustment"));
        ASSERT_BUG(sp, srcBorrow->inner->is_Array() && dstBorrow->inner->is_Slice(), StringView("Invalid array-to-slice adjustment types"));

        HIRBorrowType bt = HIRBorrowType::Shared;
        switch (node.usage) {
            case HIRValueUsage::Unknown:
                BUG(sp, StringView("Unknown usage type of _Unsize value"));
                break;
            case HIRValueUsage::Borrow:
                bt = HIRBorrowType::Shared;
                break;
            case HIRValueUsage::Mutate:
                bt = HIRBorrowType::Unique;
                break;
            case HIRValueUsage::Move:
                bt = HIRBorrowType::Shared;
                break;
        }

        if (srcBorrow->type != bt) {
            borrowNode->type = bt;
            borrowNode->resType = crate.types.borrow(bt, srcBorrow->inner);
            node.resType = crate.types.borrow(bt, dstBorrow->inner);
            node.dstType = node.resType;
        }
    }
}

UfcsOuterVisitor::UfcsOuterVisitor(const WireBoard& wb)
    : HIRVisitor(nullptr, wb.crate->types)
    , wb(wb)
    , crate(*wb.crate) {
}

auto UfcsOuterVisitor::visitExpr(HIRExprPtr& exp) -> void {
    BUG(Span(), StringView("visit_expr hit in UfcsOuterVisitor"));
}

auto UfcsOuterVisitor::visitConstgeneric(HIRConstGeneric& c) -> void {
    if (auto* e = c.opt_Unevaluated()) {
        UfcsExprVisitorMutate ev(wb, currentTraitImpl);
        ev.visitNodePtr(*(*e)->expr);
    }
}

[[nodiscard]] auto UfcsOuterVisitor::visitType(const HIRType* ty) -> const HIRType* {
    return visitTypeDefaultViaHooks(ty);
}

auto UfcsOuterVisitor::visitTraitImpl(const HIRSimplePath& traitPath, HIRTraitImpl& impl) -> void {
    const auto* previousImpl = currentTraitImpl;
    currentTraitImpl = &impl;
    HIRVisitor::visitTraitImpl(traitPath, impl);
    currentTraitImpl = previousImpl;
}

auto UfcsOuterVisitor::visitFunction(HIRItemPath p, HIRFunction& item) -> void {
    if (item.code) {
        DEBUG(StringView("Function code ") << p);
        UfcsExprVisitorMutate ev(wb, currentTraitImpl);
        ev.visitNodePtr(item.code);
        DEBUG(StringView("Function code ") << p << StringView(" (none)"));
    }
}

auto UfcsOuterVisitor::visitStatic(HIRItemPath p, HIRStatic& item) -> void {
    if (item.value) {
        UfcsExprVisitorMutate ev(wb, currentTraitImpl);
        ev.visitNodePtr(item.value);
    }
}

auto UfcsOuterVisitor::visitConstant(HIRItemPath p, HIRConstant& item) -> void {
    if (item.value) {
        UfcsExprVisitorMutate ev(wb, currentTraitImpl);
        ev.visitNodePtr(item.value);
    }
}

auto UfcsOuterVisitor::visitEnum(HIRItemPath p, HIREnum& item) -> void {
    if (auto* e = item.data.opt_Value()) {
        for (auto& var : e->variants) {
            DEBUG(StringView("Enum value ") << p << StringView(" - ") << var.name);
            if (var.expr)
            {
                UfcsExprVisitorMutate ev(wb, currentTraitImpl);
                ev.visitNodePtr(var.expr);
            }
        }
    }
}

VisitorImplTrait::VisitorImplTrait(HIRTypeInterner& types)
    : HIRVisitor(nullptr, types)
{
}

[[nodiscard]] auto VisitorImplTrait::visitType(const HIRType* ty) -> const HIRType* {
    if (methodName) {
        ty = HIRVisitor::visitType(ty);

        if (const auto* e = ty->opt_ErasedType()) {
            if (!e->inner.is_Fcn()) {
                return ty;
            }
            const HIRPath& origin = e->inner.as_Fcn().origin;
            // TODO: Do a stricter check, but this is probably good enough for now?

            if (origin.data.is_Generic() && origin.data.as_Generic().path.components().back() != methodName) {
                return ty;
            }
            if (origin.data.is_UfcsKnown() && origin.data.as_UfcsKnown().item != methodName) {
                return ty;
            }
            if (origin.data.is_UfcsInherent() && origin.data.as_UfcsInherent().item != methodName) {
                return ty;
            }
            auto tyName = RcString::newInterned(FMT(ATY_PREFIX_ERASED << methodName << StringView("_") << varIndex));
            varIndex += 1;

            if (targetImpl) {
                DEBUG(StringView("Add to impl: ") << tyName << StringView(" = ") << ty);
                targetImpl->types.insert(std::make_pair(tyName, HIRTraitImpl::ImplEnt<const HIRType*>{false, std::move(ty)}));
            } else if (targetTrait) {
                std::vector<HIRTraitPath> traits;
                for (const auto& t : e->traits) {
                    traits.push_back(t.clone());
                }
                targetTrait->types.insert(std::make_pair(tyName, HIRAssociatedType{methodParams->clone(), e->isSized, std::move(traits), typeInterner().infer()}));
                const auto& f = e->inner.as_Fcn();
                BUG_ASSERT(methodName == f.origin.data.as_UfcsKnown().item);
                const auto& fcn = targetTrait->values.at(f.origin.data.as_UfcsKnown().item);
                if (fcn.as_Function().code) {
                    const auto& t = fcn.as_Function().code.erasedTypes[f.index];
                    tys.pushBack(t);
                } else {
                    tys.pushBack(std::move(ty));
                }
                DEBUG(StringView("Add to trait: ") << tyName << StringView(" = ") << tys.back());
            } else {
                BUG(Span(), StringView("Neither target impl nor target trait set"));
            }

            ty = typeInterner().path(HIRPath(selfTy ? selfTy : typeInterner().self(), HIRGenericPath(traitPath->clone(), traitArgs->clone()), tyName, methodParams->makeNopParams(typeInterner(), 1)), {});
        }
    }
    return ty;
}

auto VisitorImplTrait::handleMethod(const HIRSimplePath& traitPath, const HIRPathParams& traitArgs, const HIRType* selfTy, const RcString& name, HIRFunction& fcn) -> void {
    TRACE_FUNCTION_F(traitPath << traitArgs << StringView(" for ") << selfTy << StringView(" : ") << name);
    this->traitPath = &traitPath;
    this->traitArgs = &traitArgs;
    this->selfTy = selfTy;
    methodName = name.c_str();
    methodParams = &fcn.params;
    varIndex = 0;
    DEBUG(StringView("-> ") << fcn.returnType);
    if (fcn.traitReturnType) {
        *fcn.traitReturnType = visitType(*fcn.traitReturnType);
        fcn.traitReturnType.reset();
    } else {
        fcn.returnType = visitType(fcn.returnType);
    }

    DEBUG(StringView("-> ") << fcn.returnType);
    if (targetTrait && varIndex > 0 && fcn.code) {
        for (size_t i = 0; i < varIndex; i++) {
            auto tyName = RcString::newInterned(FMT(ATY_PREFIX_ERASED << methodName << StringView("_") << i));
            auto& ty = targetTrait->types.at(tyName);
            ty.defaultValue = tys[i];
            ty.hasDefault = true;
        }
    }

    tys.clear();
    methodName = nullptr;
}

auto VisitorImplTrait::visitTrait(HIRItemPath p, HIRTrait& tr) -> void {
    auto self = typeInterner().self();
    auto path = p.getSimplePath();
    auto params = tr.params.makeNopParams(typeInterner(), 0);
    targetTrait = &tr;
    for (auto& v : tr.values) {
        if (auto* f = v.second.opt_Function()) {
            handleMethod(path, params, self, v.first, *f);
        }
    }
    targetTrait = nullptr;
}

auto VisitorImplTrait::visitTraitImpl(const HIRSimplePath& traitPath, HIRTraitImpl& impl) -> void {
    targetImpl = &impl;
    for (auto& v : impl.methods) {
        handleMethod(traitPath, impl.traitArgs, impl.type, v.first, v.second.data);
    }
    targetImpl = nullptr;
}

auto VtableOuterVisitor::createType(bool isPublic, RcString name, HIRStruct value) -> HIRSimplePath {
    BUG_ASSERT(currentModulePath);
    BUG_ASSERT(currentNewTypes);
    auto boxed = crate.pool->make<HIRVisEnt<HIRTypeItem>>(HIRVisEnt<HIRTypeItem>{(isPublic ? HIRPublicity::newGlobal() : HIRPublicity::newNone()), HIRTypeItem(mv$(value))});
    auto result = (*currentModulePath + name).getSimplePath();
    currentNewTypes->push_back(std::make_pair(mv$(name), boxed));
    return result;
}

VtableOuterVisitor::VtableOuterVisitor(const WireBoard& wb)
    : HIRVisitor(nullptr, wb.crate->types)
    , wb(wb)
    , crate(*wb.crate)
{
    langSized_ = crate.getLangItemPathOpt("sized");
}

auto VtableOuterVisitor::visitModule(HIRItemPath p, HIRModule& mod) -> void {
    auto savedPath = currentModulePath;
    auto savedTypes = currentNewTypes;

    NewTypes newTypes;
    currentModulePath = &p;
    currentNewTypes = &newTypes;

    HIRVisitor::visitModule(p, mod);
    for (auto& i : newTypes) {
        mod.modItems.insert(mv$(i));
    }

    currentModulePath = savedPath;
    currentNewTypes = savedTypes;
}

auto VtableOuterVisitor::visitTrait(HIRItemPath p, HIRTrait& tr) -> void {
    Span sp;

    TRACE_FUNCTION_F(p);
    StaticTraitResolve resolve{wb};
    resolve.setImplGenericsRaw(MetadataType::Unknown, tr.params);
    HIRGenericPath traitPath(p.getSimplePath(), tr.params.makeNopParams(crate.types, 0));

    std::unordered_map<std::string, unsigned int> assocTypeIndexes;

    struct Foo {
        HIRTypeInterner& types;
        HIRTrait* traitPtr;
        HIRGenericParams paramsDef;
        bool hasConflict = false;

        Foo(HIRTypeInterner& types, HIRTrait& rootTrait)
            : types(types)
            , traitPtr(&rootTrait)
        {
        }

        void addTypesFromTrait(const HIRGenericPath& path, const HIRTrait& tr, const HIRTraitPath::assocListT& assoc) {
            for (const auto& ty : tr.types) {
                bool isKnown = false;
                for (const auto& ent : assoc) {
                    if (ent.first == ty.first) {
                        DEBUG(ty.first << StringView(" = ") << ent.second.type);
                        isKnown = true;
                        break;
                    }
                }
                if (!isKnown) {
                    auto i = paramsDef.types.size();
                    DEBUG(ty.first << StringView(" #") << i << StringView(" (from ") << path << StringView(")"));
                    auto rv = traitPtr->typeIndexes.insert(std::make_pair(ty.first, static_cast<unsigned>(i)));
                    if (rv.second == false) {
                        DEBUG(StringView("Conflicting ATY name ") << ty.first);
                        rv.first->second = UINT_MAX;
                        this->hasConflict = true;
                    } else {
                        paramsDef.types.push_back(HIRTypeParamDef{RcString::newInterned(FMT(StringView("a#") << ty.first)), types.infer(), ty.second.isSized});
                        paramsDef.paramKinds.pushBack(HIRGenericParamKind::Type);
                    }
                }
            }
        }
    };

    Foo visitor{crate.types, tr};
    for (const auto& tp : tr.params.types) {
        visitor.paramsDef.types.push_back(HIRTypeParamDef{tp.name, crate.types.infer(), tp.isSized});
    }
    for (const auto& vp : tr.params.values) {
        visitor.paramsDef.values.push_back(HIRValueParamDef{vp.name, vp.type});
    }
    for (size_t i = 0; i < tr.params.paramCount(); i++) {
        visitor.paramsDef.paramKinds.pushBack(tr.params.paramKindAt(i));
    }
    visitor.addTypesFromTrait(traitPath, tr, {});
    for (const auto& st : tr.allParentTraits) {
        BUG_ASSERT(st.traitPtr);
        visitor.addTypesFromTrait(st.path, *st.traitPtr, st.typeBounds);
    }
    bool hasConflictingAtyName = visitor.hasConflict;
    auto args = std::move(visitor.paramsDef);

    struct VtableConstruct {
        HIRTypeInterner& types;
        const Span& sp;
        const VtableOuterVisitor* outer;
        const StaticTraitResolve* resolvePtr;
        HIRTrait* traitPtr;
        tStructFields fields;

        bool addEntsFromTrait(const HIRTrait& tr, const HIRGenericPath& traitPath, Vector<bool>* supertraitFlags) {
            TRACE_FUNCTION_F(traitPath);

            struct M: public Monomorphiser {
                HIRTrait* traitPtr;
                const HIRPathParams* traitParams;

                explicit M(HIRTypeInterner& types)
                    : Monomorphiser(types)
                {
                }

                const HIRType* getType(const Span& sp, const HIRGenericRef& g) const override {
                    if (g.group() == 0 && g.idx() < traitParams->types.size()) {
                        return traitParams->types[g.idx()];
                    }
                    return types.generic(g.name, g.binding);
                }

                HIRConstGeneric getValue(const Span& sp, const HIRGenericRef& g) const override {
                    if (g.group() == 0 && g.idx() < traitParams->values.size()) {
                        return traitParams->values[g.idx()].clone();
                    }
                    return g;
                }

                const HIRType* monomorphType(const Span& sp, const HIRType* t, bool allowInfer = true) const override {
                    if (t->is_Path() && t->as_Path().path.data.is_UfcsKnown()) {
                        const auto& pe = t->as_Path().path.data.as_UfcsKnown();
                        bool isSelf = (pe.type == types.self());
                        auto it = traitPtr->typeIndexes.find(pe.item);
                        bool hasItem = (it != traitPtr->typeIndexes.end());
                        // TODO: Check the trait against m_type_indexes
                        if (isSelf /*&& pe.trait == trait_path*/ && hasItem) {
                            DEBUG(StringView("[clone_cb] t=") << t << StringView(" -> ") << it->second);
                            return types.generic(RcString::newInterned(FMT(StringView("a#") << pe.item)), it->second);
                            DEBUG(StringView("[clone_cb] t=") << t << StringView("(") << isSelf << hasItem << StringView(")"));
                        }
                    }
                    return Monomorphiser::monomorphType(sp, t, allowInfer);
                }
            } m(types);

            m.traitPtr = traitPtr;
            m.traitParams = &traitPath.params;
            auto cloneSelfCb = [this](const auto& t) -> const HIRType* {
                if (t == types.self()) {
                    return types.unit();
                }
                return nullptr;
            };
            for (auto& vi : tr.values) {
                switch (vi.second.tag()) {
                    case HIRTraitValueItem::TAG_Function: {
                        auto& ve = vi.second.as_Function();
                        if (ve.receiver == HIRFunction::Receiver::Free) {
                            break;
                        }
                        if (std::any_of(ve.params.bounds.begin(), ve.params.bounds.end(), [&](const auto& b) {
                            return b.is_TraitBound() && b.as_TraitBound().type == types.self() && b.as_TraitBound().trait.path.path == outer->langSized_;
                        })) {
                            DEBUG(StringView("- '") << vi.first << StringView("' Skip where `Self: Sized`"));
                            break;
                        }
                        if (ve.params.isGeneric()) {
                            DEBUG(StringView("- '") << vi.first << StringView("' NOT object safe (generic), not creating vtable"));
                            return false;
                        }
                        const HIRType* tmp;

                        HIRTypeDataFunctionPointer ft;
                        ft.isUnsafe = ve.unsafe;
                        ft.isVariadic = ve.variadic;
                        ft.trackCaller = ve.markings.trackCaller;
                        ft.abi = ve.abi;
                        ft.rettype = resolvePtr->monomorphExpand(sp, ve.returnType, m);
                        ft.argTypes.grow(ve.args.size());
                        ft.argTypes.pushBack(cloneTyWith(types, sp, resolvePtr->monomorphExpandOpt(sp, ve.args[0].second, m), cloneSelfCb));
                        if (ve.receiver == HIRFunction::Receiver::Value) {
                            ft.argTypes.mut(0) = types.borrow(HIRBorrowType::Owned, ft.argTypes[0]);
                        }
                        for (unsigned int i = 1; i < ve.args.size(); i++) {
                            ft.argTypes.pushBack(resolvePtr->monomorphExpand(sp, ve.args[i].second, m));
                        }
                        const HIRType* fcnType = types.function(mv$(ft));

                        if (visitTyWith(fcnType, [&](const auto& t) {
                            return (t == types.self());
                        })) {
                            DEBUG(StringView("- '") << vi.first << StringView("' NOT object safe (uses Self), not creating vtable - ") << fcnType);
                            return false;
                        }

                        traitPtr->valueIndexes.insert(std::make_pair(vi.first, std::make_pair(static_cast<unsigned int>(fields.size()), traitPath.clone())));
                        DEBUG(StringView("- '") << vi.first << StringView("' is @") << fields.size());
                        fields.push_back(HIRStructField{vi.first, HIRPublicity::newGlobal(), mv$(fcnType), {}});
                        break;
                    }
                    case HIRTraitValueItem::TAG_Static: {
                        if (vi.first != "vtable#") {
                            TODO(Span(), StringView("Associated static in vtable"));
                        }
                        break;
                    }
                    case HIRTraitValueItem::TAG_Constant: {
                        //TODO(Span(), StringView("Associated const in vtable"));
                        break;
                    }
                }
            }
            if (supertraitFlags) {
                supertraitFlags->grow(tr.allParentTraits.size());
                for (const auto& st : tr.allParentTraits) {
                    auto self = types.self();
                    auto stMono = MonomorphStatePtr(types, self, &traitPath.params, nullptr).monomorphTraitpath(sp, st, false);
                    supertraitFlags->pushBack(addEntsFromTrait(*st.traitPtr, stMono.path, nullptr));
                }
            }
            return true;
        }
    };

    VtableConstruct vtc{crate.types, sp, this, &resolve, &tr, {}};
    HIRTypeDataFunctionPointer ft;
    ft.isUnsafe = false;
    ft.isVariadic = false;
    ft.abi = RcString::newInterned(ABI_RUST);
    ft.rettype = crate.types.unit();
    ft.argTypes.pushBack(crate.types.pointer(HIRBorrowType::Owned, crate.types.unit()));
    vtc.fields.push_back({RcString::newInterned("#drop_glue"), HIRPublicity::newNone(), crate.types.function(mv$(ft)), {}});
    vtc.fields.push_back({RcString::newInterned("#size"), HIRPublicity::newNone(), crate.types.primitive(HIRCoreType::Usize), {}});
    vtc.fields.push_back({RcString::newInterned("#align"), HIRPublicity::newNone(), crate.types.primitive(HIRCoreType::Usize), {}});
    Vector<bool> supertraitFlags;
    if (!vtc.addEntsFromTrait(tr, traitPath, &supertraitFlags) || hasConflictingAtyName) {
        tr.valueIndexes.clear();
        tr.typeIndexes.clear();
        return;
    }
    tr.vtableParentTraitsStart = vtc.fields.size();
    for (size_t i = 0; i < tr.allParentTraits.size(); i++) {
        const auto& pt = tr.allParentTraits[i];
        auto parentVtableSpath = pt.path.path;
        parentVtableSpath.updateLastComponent(RcString::newInterned(FMT(parentVtableSpath.components().back().c_str() << StringView("#vtable"))));
        auto parentVtablePath = HIRGenericPath(mv$(parentVtableSpath), pt.path.params.clone());
        auto ty = true || supertraitFlags[i] ? crate.types.borrow(HIRBorrowType::Shared, crate.types.path(mv$(parentVtablePath), {})) : crate.types.unit();
        vtc.fields.push_back({RcString::newInterned(FMT(StringView("#parent_") << i)), HIRPublicity::newNone(), mv$(ty), {}});
    }
    auto fields = mv$(vtc.fields);

    HIRPathParams params;
    {
        unsigned int i = 0;
        for (const auto& tp : tr.params.types) {
            params.types.push_back(crate.types.generic(tp.name, i));
            i++;
        }
        for (const auto& ty : tr.typeIndexes) {
            DEBUG(StringView("ATY ") << ty.first << StringView(" : ") << ty.second);
            auto tyDef = tr.getAtyDef(ty.first);
            ASSERT_BUG(sp, tyDef.first, StringView("Unable to find ATY defintiion"));
            auto atyParams = tyDef.first->generics.makeNopParams(crate.types, 1);
            HIRPath path(crate.types.self(), traitPath.clone(), ty.first, std::move(atyParams));
            params.types.push_back(crate.types.path(mv$(path), {}));
        }
    }
    {
        unsigned int i = 0;
        for (const auto& vp : tr.params.values) {
            params.values.push_back(HIRGenericRef(vp.name, i));
            i++;
        }
    }
    ASSERT_BUG(sp, args.types.size() == params.types.size() && args.values.size() == params.values.size(), StringView("Count mismatch args=") << args.fmtArgs() << StringView(" params=") << params);
    // TODO: Would like to have access to the publicity marker
    auto itemPath = createType(true, RcString::newInterned(FMT(p.getName() << StringView("#vtable"))), HIRStruct(std::move(args), HIRStruct::Repr::C, HIRStruct::Data(mv$(fields))));
    tr.vtablePath = itemPath;
    DEBUG(StringView("Vtable structure created - ") << itemPath);
    HIRGenericPath path(mv$(itemPath), std::move(params));

    tr.values.insert(std::make_pair(RcString::newInterned("vtable#"), HIRTraitValueItem(HIRStatic(HIRLinkage(), false, crate.types.path(mv$(path), {}), {}))));
}

auto VtableOuterVisitor::visitTraitImpl(const HIRSimplePath& traitPath, HIRTraitImpl& impl) -> void {
    Span sp;

    TRACE_FUNCTION_F(StringView("impl ") << traitPath << StringView(" for ") << impl.type);
    HIRVisitor::visitTraitImpl(traitPath, impl);
}

FixupVisitor::FixupVisitor(const HIRCrate& crate)
    : HIRVisitor(nullptr, crate.types)
    , crate(crate)
{
}

auto FixupVisitor::visitStruct(HIRItemPath ip, HIRStruct& str) -> void {
    Span sp;
    auto p = std::strchr(ip.name, '#');
    if (p && std::strcmp(p, "#vtable") == 0) {
        auto traitPath = ip.parent->getSimplePath();
        traitPath += RcString::newInterned(ip.name, p - ip.name);
        const auto& trait = crate.getTraitByPath(sp, traitPath);

        auto& fields = str.data.as_Named();
        for (size_t i = 0; i < trait.allParentTraits.size(); i++) {
            const auto& pt = trait.allParentTraits[i];
            const auto& parentTrait = *pt.traitPtr;
            auto& fldTy = fields[trait.vtableParentTraitsStart + i].ty;

            DEBUG(pt << StringView(" ") << fldTy);
            if (parentTrait.vtablePath == HIRSimplePath()) {
                fldTy = crate.types.unit();
            } else {
                auto borrowData = fldTy->cloneData();
                auto& borrow = borrowData.as_Borrow();
                auto pathData = borrow.inner->cloneData();
                auto& te = pathData.as_Path();
                auto& vtableGpath = te.path.data.as_Generic();
                te.binding = &crate.getStructByPath(sp, vtableGpath.path);

                for (const auto& atyIdx : parentTrait.typeIndexes) {
                    if (vtableGpath.params.types.size() <= atyIdx.second) {
                        vtableGpath.params.types.resize(atyIdx.second + 1);
                    }
                    auto& slot = vtableGpath.params.types[atyIdx.second];
                    auto it = pt.typeBounds.find(atyIdx.first);
                    if (it != pt.typeBounds.end()) {
                        slot = it->second.type;
                    } else if (trait.typeIndexes.count(atyIdx.first) != 0) {
                        slot = crate.types.generic(RcString(), trait.typeIndexes.at(atyIdx.first));
                    } else {
                        const HIRGenericPath* gp = nullptr;
                        for (const auto& pptraitPath : parentTrait.allParentTraits) {
                            if (pptraitPath.traitPtr->types.count(atyIdx.first) != 0) {
                                DEBUG(StringView("Found ") << atyIdx.first << StringView(" in ") << pptraitPath);
                                gp = &pptraitPath.path;
                            }
                        }
                        ASSERT_BUG(sp, gp, StringView("Failed to a find trait that defined ") << atyIdx.first << StringView(" in ") << pt.path.path);

                        auto gpMono = MonomorphStatePtr(crate.types, nullptr, &pt.path.params, nullptr).monomorphGenericpath(sp, *gp);
                        const HIRTraitPath* p = nullptr;
                        for (const auto& pt : trait.allParentTraits) {
                            if (pt.path == gpMono) {
                                p = &pt;
                            }
                        }
                        ASSERT_BUG(sp, p, StringView("Failed to find ") << gpMono << StringView(" in parent trait list for ") << traitPath);
                        auto it = p->typeBounds.find(atyIdx.first);
                        ASSERT_BUG(sp, it != p->typeBounds.end(), StringView("Failed to find ") << atyIdx.first << StringView(" in ") << *p);
                        slot = it->second.type;
                    }
                }
                borrow.inner = crate.types.intern(std::move(pathData));
                fldTy = crate.types.intern(std::move(borrowData));
            }
        }
    }
}
