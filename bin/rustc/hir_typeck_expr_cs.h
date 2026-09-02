#pragma once

#include "span.h"
#include "output.h"
#include "hir_expr.h"
#include "hir_expr_ptr.h"
#include "hir_type_ref.h"
#include "hir_typeck_helpers.h"
#include "hir_typeck_expr_visit.h"

#include <std/lib/vector.h>

#include <algorithm>

struct Context {
    class Revisitor {
    public:
        virtual ~Revisitor() = default;
        virtual const Span& span() const = 0;
        virtual void fmt(stl::ZeroCopyOutput& os) const = 0;
        virtual bool revisit(Context& context, bool isFallback) = 0;
        virtual void collectInferenceDependencies(const Context&, stl::Vector<unsigned>&) const {
        }
    };

    struct Binding {
        RcString name;
        const HIRType* ty;
    };

    struct Coercion {
        unsigned ruleIdx;
        Span obligationSpan;
        const HIRType* leftTy;
        HIRExprNodeP* rightNodePtr;
        const HIRType* rightTy;
        SolverCoercionOp op;

        Coercion(unsigned ruleIdx, const HIRType* leftTy, HIRExprNodeP* rightNodePtr);
        Coercion(unsigned ruleIdx, const Span& span, const HIRType* leftTy, const HIRType* rightTy, SolverCoercionOp op);

        const Span& span() const;
        const HIRType* sourceType() const;
    };

    struct Associated {
        struct StallDependency {
            unsigned index;
            const HIRType* resolved;
        };

        unsigned ruleIdx;
        Span span;
        const HIRType* leftTy;

        HIRSimplePath trait;
        HIRPathParams params;
        const HIRType* implTy;
        RcString name;
        HIRPathParams atyPp;

        // HACK: operators are special - the result when both types are primitives is ALWAYS the lefthand side
        bool isOperator;
        TypeckPrimitiveOperator operatorKind;
        bool isAmbiguous = false;

        stl::Vector<StallDependency> stalledOn;
    };

    const HIRCrate& crate;
    const HIRTraitImpl* currentTraitImpl;

    std::vector<Binding> bindings;
    HMTypeInferrence ivars;
    TraitResolution resolve;

    unsigned nextRuleIdx;

    std::vector<std::unique_ptr<Coercion>> linkCoerce;
    std::vector<Associated> linkAssoc;
    stl::ObjPool::Ref linkAssocIndexPool;
    stl::IntMap<stl::Vector<unsigned>> linkAssocIndex;

    stl::Vector<HIRExprNode*> toVisit;

    std::vector<std::unique_ptr<Revisitor>> advRevisits;

    struct ClosureReturnObligation {
        const HIRExprNodeClosure* closure;
        const HIRType* expected;
    };

    stl::Vector<ClosureReturnObligation> closureReturnObligations;
    ThinVector<SolverObligation> solverObligations;
    stl::Vector<bool> neverFallbackIvars;

    HIRGenericParams emptyGenericParams;
    stl::Vector<bool> ivarsSized;

    struct IvarDefault {
        unsigned index;
        const HIRType* type;
    };
    ThinVector<IvarDefault> ivarDefaults;

    struct TaitEntry {
        HIRPathParams params;
        const HIRType* ourType;

        TaitEntry(const HIRPathParams& p, const HIRType* t);
    };

    std::map<HIRTypeDataErasedTypeAliasInner*, TaitEntry> erasedTypeAliases;

    struct RpitEntry {
        const HIRPath* origin;
        unsigned int index;
        const HIRType* ourType;
        bool selfReferenced;
    };

    std::vector<RpitEntry> rpitTypes;

    const HIRSimplePath langBox;

    Context(const WireBoard& wb, const HIRGenericParams* implParams, const HIRGenericParams* itemParams, const HIRSimplePath& modPath, const HIRGenericPath* currentTrait, const HIRTraitImpl* currentTraitImpl);

    bool takeChanged() {
        return ivars.takeChanged();
    }

    bool hasRules() const {
        return !(linkCoerce.empty() && linkAssoc.empty() && toVisit.empty() && advRevisits.empty());
    }

    inline const HIRType* addIvars(const HIRType* ty) {
        return ivars.addIvars(ty);
    }

    void equateTypes(const Span& sp, const HIRType* l, const HIRType* r);
    void equateTypesInner(const Span& sp, const HIRType* l, const HIRType* r);

    void applySolverResponse(const Span& sp, const SolverResponse& response, std::vector<HIRExprNodeP>* coercionInputs = nullptr);
    void registerSolverObligation(const Span& sp, const HIRType* type, HIRTraitPath trait);
    void registerClosureReturnObligation(const Span& sp, const HIRExprNodeClosure* closure, const HIRType* expected);
    const HIRType* closureReturnExpectation(const HIRExprNodeClosure* closure) const;
    void recordNeverFallback(unsigned index);
    bool usedNeverFallback(const HIRType* type) const;
    const HIRType* expandAssociatedTypes(const Span& sp, const HIRType* input);
    void expandAssociatedTypesParams(const Span& sp, HIRPathParams& params);
    void compactIvars(const Span& sp);

    void equateTypesCoerce(const Span& sp, const HIRType* l, HIRExprNodeP& nodePtr);
    void addCoercionObligation(const Span& sp, const HIRType* destination, const HIRType* source, SolverCoercionOp op);

    void equateTypesAssoc(const Span& sp, const HIRType* l, const HIRSimplePath& trait, HIRPathParams params, const HIRType* implTy, const char* name, const HIRPathParams& atyPp, bool isOp = false, TypeckPrimitiveOperator operatorKind = TypeckPrimitiveOperator::None);

    void equateValues(const Span& sp, const HIRConstGeneric& rl, const HIRConstGeneric& rr);

    static u64 associatedIndexKey(const HIRType* leftTy, const HIRSimplePath& trait, const HIRType* implTy, RcString name, bool isOperator, TypeckPrimitiveOperator operatorKind);
    static u64 associatedIndexKey(const Associated& rule);
    void indexAssociated(unsigned index);
    void unindexAssociated(unsigned index, u64 key);
    void storeAssociated(unsigned index, Associated rule, u64 oldKey);
    void removeAssociated(unsigned index, u64 oldKey);

    void requireSized(const Span& sp, const HIRType* ty);

    void addTraitBound(const Span& sp, const HIRType* implTy, const HIRSimplePath& trait, HIRPathParams params) {
        equateTypesAssoc(sp, crate.types.infer(), trait, mv$(params), implTy, "", {}, false);
    }

    void selectWellFormed(const Span& sp, const HIRType* type);

    void addIvarDefault(const Span& sp, unsigned int ivarIndex, const HIRType* type);

    void handlePattern(const Span& sp, HIRPattern& pat, const HIRType* type, bool isIrrefutable = false);
    void handlePatternDirectInner(const Span& sp, HIRPattern& pat, const HIRType* type);
    void addBindingInner(const Span& sp, const HIRPatternBinding& pb, const HIRType* type);

    void addVar(const Span& sp, unsigned int index, const RcString& name, const HIRType* type);
    const HIRType* getVar(const Span& sp, unsigned int idx) const;

    void addRevisit(HIRExprNode& node);
    void addRevisitAdv(std::unique_ptr<Revisitor> ent);

    const HIRType* getType(const HIRType* ty) const {
        return ivars.getType(ty);
    }

    const HIRType* revealOpaqueType(const HIRType* type) const;
    const HIRType* revealOpaqueTypes(const HIRType* type) const;

    void addRpitType(const HIRPath& origin, unsigned int index, const HIRType* type);
    void noteRpitSelfReferences(const HIRType* type);
    bool fallbackUnresolvedRpitType(const Span& sp);

    HIRExprNodeP createAutoderef(HIRExprNodeP valNode, const HIRType* tyDst) const;

private:
    void addIvarsParams(HIRPathParams& params) {
        ivars.addIvarsParams(params);
    }
};

bool visitCallPopulateCache(Context& context, const Span& sp, HIRPath& path, HIRExprCallCache& cache) __attribute__((warn_unused_result));

void TypecheckCodeCS(const TypeckModuleState& ms, tArgs& args, const HIRType* resultType, HIRExprPtr& expr);
void TypecheckCodeCSEnumerateRules(Context& context, const TypeckModuleState& ms, tArgs& args, const HIRType* resultType, HIRExprPtr& expr, HIRExprNodeP& rootPtr);
