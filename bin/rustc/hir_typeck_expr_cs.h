#pragma once

#include "span.h"
#include "hir_expr.h"
#include "hir_expr_ptr.h"
#include "hir_type_ref.h"
#include "hir_typeck_helpers.h"
#include "hir_typeck_expr_visit.h"

#include <std/lib/vector.h>

#include <algorithm>
#include <unordered_map>

struct Context {
    class Revisitor {
    public:
        virtual ~Revisitor() = default;
        virtual const Span& span() const = 0;
        virtual void fmt(::std::ostream& os) const = 0;
        virtual bool revisit(Context& context, bool isFallback) = 0;
    };

    struct Binding {
        RcString name;
        HIRTypeRef ty;
    };

    struct Coercion {
        unsigned ruleIdx;
        HIRTypeRef leftTy;
        HIRExprNodeP* rightNodePtr;

        friend ::std::ostream& operator<<(::std::ostream& os, const Coercion& v);
    };

    struct IVarPossible {
        struct CoerceTy {
            enum Op {
                Coercion,
                Unsizing,
            } op;

            HIRTypeRef ty;

            CoerceTy(HIRTypeRef ty, bool isCoerce);
        };

        bool forceDisable = false;
        bool forceNoTo = false;
        bool forceNoFrom = false;

        ::std::vector<CoerceTy> typesCoerceTo;

        ::std::vector<CoerceTy> typesCoerceFrom;

        HIRTypeRefSet typesDefault;

        HIRTypeRefSet rawPointerFallbacks;

        void reset();

        bool hasRules() const;

        void mergeFrom(const IVarPossible& source);
    };

    struct Associated {
        struct StallDependency {
            unsigned index;
            HIRTypeRef resolved;
        };

        struct CapturedIvarPossible {
            unsigned index;
            IVarPossible possibilities;
        };

        unsigned ruleIdx;
        Span span;
        HIRTypeRef leftTy;

        HIRSimplePath trait;
        HIRPathParams params;
        HIRTypeRef implTy;
        RcString name;
        HIRPathParams atyPp;

        // HACK: operators are special - the result when both types are primitives is ALWAYS the lefthand side
        bool isOperator;
        TypeckPrimitiveOperator operatorKind;
        bool isAmbiguous = false;

        ::std::vector<StallDependency> stalledOn;
        ::std::vector<CapturedIvarPossible> stalledPossibilities;

        friend ::std::ostream& operator<<(::std::ostream& os, const Associated& v);
    };

    const HIRCrate& crate;
    const HIRTraitImpl* currentTraitImpl;

    ::std::vector<Binding> bindings;
    HMTypeInferrence ivars;
    TraitResolution resolve;

    unsigned nextRuleIdx;

    ::std::vector<::std::unique_ptr<Coercion>> linkCoerce;

    ::std::unordered_map<const HIRExprNode*, HIRTypeRef> coercionHints;
    ::std::vector<Associated> linkAssoc;
    stl::ObjPool::Ref linkAssocIndexPool;
    stl::IntMap<stl::Vector<unsigned>> linkAssocIndex;

    ::std::vector<HIRExprNode*> toVisit;

    ::std::vector<::std::unique_ptr<Revisitor>> advRevisits;

    struct ClosureReturnObligation {
        const HIRExprNodeClosure* closure;
        HIRTypeRef expected;
    };

    stl::Vector<ClosureReturnObligation> closureReturnObligations;

    HIRGenericParams emptyGenericParams;
    ::std::vector<bool> ivarsSized;
    ::std::vector<IVarPossible> possibleIvarVals;
    ::std::vector<Associated::CapturedIvarPossible>* possibleIvarSink = nullptr;

    IVarPossible* getPossibleIvarSink(unsigned index);

    struct TaitEntry {
        HIRPathParams params;
        HIRTypeRef ourType;

        TaitEntry(const HIRPathParams& p, HIRTypeRef t);
    };

    ::std::map<HIRTypeDataErasedTypeAliasInner*, TaitEntry> erasedTypeAliases;

    struct RpitEntry {
        const HIRPath* origin;
        unsigned int index;
        HIRTypeRef ourType;
        bool selfReferenced;
    };

    ::std::vector<RpitEntry> rpitTypes;

    const HIRSimplePath langBox;

    Context(const WireBoard& wb, const HIRGenericParams* implParams, const HIRGenericParams* itemParams, const HIRSimplePath& modPath, const HIRGenericPath* currentTrait, const HIRTraitImpl* currentTraitImpl);

    bool takeChanged() {
        return ivars.takeChanged();
    }

    bool hasRules() const {
        return !(linkCoerce.empty() && linkAssoc.empty() && toVisit.empty() && advRevisits.empty());
    }

    inline void addIvars(HIRTypeRef& ty) {
        ivars.addIvars(ty);
    }

    void equateTypes(const Span& sp, const HIRTypeData* l, const HIRTypeData* r);
    void equateTypesInner(const Span& sp, const HIRTypeData* l, const HIRTypeData* r);

    void applySolverResponse(const Span& sp, const SolverResponse& response);
    void registerSolverObligation(const Span& sp, HIRTypeRef type, HIRTraitPath trait);
    void registerClosureReturnObligation(const Span& sp, const HIRExprNodeClosure* closure, HIRTypeRef expected);
    const HIRTypeData* closureReturnExpectation(const HIRExprNodeClosure* closure) const;
    HIRTypeRef expandAssociatedTypes(const Span& sp, HIRTypeRef input) const;
    const HIRTypeData* expandAssociatedTypes(const Span& sp, const HIRTypeData* input, HIRTypeRef& tmp) const;
    void expandAssociatedTypesParams(const Span& sp, HIRPathParams& params) const;
    void compactIvars();

    void equateTypesCoerce(const Span& sp, const HIRTypeData* l, HIRExprNodeP& nodePtr);
    void recordCoercionHint(const HIRTypeData* type, HIRExprNodeP& nodePtr);

    const HIRTypeData* coercionHint(const HIRExprNode& node) const;

    void equateTypesAssoc(const Span& sp, const HIRTypeData* l, const HIRSimplePath& trait, HIRPathParams params, const HIRTypeData* implTy, const char* name, const HIRPathParams& atyPp, bool isOp = false, TypeckPrimitiveOperator operatorKind = TypeckPrimitiveOperator::None);

    void equateValues(const Span& sp, const HIRConstGeneric& rl, const HIRConstGeneric& rr);

    static u64 associatedIndexKey(HIRTypeRef leftTy, const HIRSimplePath& trait, HIRTypeRef implTy, RcString name, bool isOperator, TypeckPrimitiveOperator operatorKind);
    static u64 associatedIndexKey(const Associated& rule);
    void indexAssociated(unsigned index);
    void unindexAssociated(unsigned index, u64 key);
    void storeAssociated(unsigned index, Associated rule, u64 oldKey);
    void removeAssociated(unsigned index, u64 oldKey);

    void requireSized(const Span& sp, const HIRTypeData* ty);

    void addTraitBound(const Span& sp, const HIRTypeData* implTy, const HIRSimplePath& trait, HIRPathParams params) {
        equateTypesAssoc(sp, crate.types.infer(), trait, mv$(params), implTy, "", {}, false);
    }

    void selectWellFormed(const Span& sp, const HIRTypeData* type);

    IVarPossible* getIvarPossibilities(const Span& sp, unsigned int ivarIndex);

    enum class IvarUnknownType {
        To,

        From,

        Bound,
    };

    void possibleEquateTypeUnknown(const Span& sp, const HIRTypeData* ty, IvarUnknownType srcTy);

    void possibleEquateTypeBounds(const Span& sp, const HIRTypeData* ty, ::std::vector<HIRTypeRef> t);

    enum class PossibleTypeSource {
        CoerceTo,
        UnsizeTo,
        CoerceFrom,
        UnsizeFrom,
    };

    void possibleEquateIvar(const Span& sp, unsigned int ivarIndex, const HIRTypeData* t, PossibleTypeSource srcTy);

    void possibleEquateIvarRawPointerFallback(const Span& sp, unsigned int ivarIndex, const HIRTypeData* type);

    void possibleEquateIvarUnknown(const Span& sp, unsigned int ivarIndex, IvarUnknownType srcTy);

    void handlePattern(const Span& sp, HIRPattern& pat, const HIRTypeData* type, bool isIrrefutable = false);
    void handlePatternDirectInner(const Span& sp, HIRPattern& pat, const HIRTypeData* type);
    void addBindingInner(const Span& sp, const HIRPatternBinding& pb, HIRTypeRef type);

    void addVar(const Span& sp, unsigned int index, const RcString& name, HIRTypeRef type);
    const HIRTypeData* getVar(const Span& sp, unsigned int idx) const;

    void addRevisit(HIRExprNode& node);
    void addRevisitAdv(::std::unique_ptr<Revisitor> ent);

    const HIRTypeData* getType(const HIRTypeData* ty) const {
        return ivars.getType(ty);
    }

    const HIRTypeData* revealOpaqueType(const HIRTypeData* type) const;
    HIRTypeRef revealOpaqueTypes(const HIRTypeData* type) const;

    void addRpitType(const HIRPath& origin, unsigned int index, HIRTypeRef type);
    void noteRpitSelfReferences(const HIRTypeData* type);
    bool fallbackUnresolvedRpitType(const Span& sp);

    HIRExprNodeP createAutoderef(HIRExprNodeP valNode, HIRTypeRef tyDst) const;

private:
    void addIvarsParams(HIRPathParams& params) {
        ivars.addIvarsParams(params);
    }
};

extern bool visitCallPopulateCache(Context& context, const Span& sp, HIRPath& path, HIRExprCallCache& cache) __attribute__((warn_unused_result));

extern void TypecheckCodeCSEnumerateRules(Context& context, const TypeckModuleState& ms, tArgs& args, const HIRTypeData* resultType, HIRExprPtr& expr, HIRExprNodeP& rootPtr);
