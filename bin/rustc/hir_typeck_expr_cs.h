#pragma once

#include "span.h"
#include "hir_expr.h"
#include "hir_expr_ptr.h"
#include "hir_type_ref.h"
#include "hir_typeck_helpers.h"
#include "hir_typeck_expr_visit.h"

#include <algorithm>
#include <unordered_map>

// PLAN: Build up a set of conditions that are easier to solve
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
        //unsigned int ivar;
    };

    /// Inferrence variable equalities
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

        // Strong disable (depends on a trait impl)
        bool forceDisable = false;
        bool forceNoTo = false;
        bool forceNoFrom = false;

        // Possible types from trait impls (may introduce new types)
        // - This is union of all input bounds
        bool hasBounded = false;
        /// If the bounds include this ivar, mark differently (permits any incoming type, but types can be removed)
        /// - If an existing type isn't in the incoming set, it is removed
        /// - But any type in an incoming set is accepted (even if it doesn't already exist)
        bool boundsIncludeSelf = false;
        // Target types for coercion/unsizing (these types are known to exist in the function)
        ::std::vector<CoerceTy> typesCoerceTo;
        // Source types for coercion/unsizing (these types are known to exist in the function)
        ::std::vector<CoerceTy> typesCoerceFrom;
        // Possible default types (from generic defaults)
        ::std::set<HIRTypeRef> typesDefault;

        ::std::vector<HIRTypeRef> bounded;

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
        RcString name; // if "", no type is used (and left is ignored) - Just does trait selection
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

    ::std::vector<Binding> mBindings;
    HMTypeInferrence ivars;
    TraitResolution mResolve;

    unsigned nextRuleIdx;
    // NOTE: unique_ptr used to reduce copy costs of the list
    ::std::vector<::std::unique_ptr<Coercion>> linkCoerce;
    // Expected types are available while aggregate fields are enumerated,
    // before the corresponding coercion rules are solved.
    ::std::unordered_map<const HIRExprNode*, HIRTypeRef> coercionHints;
    ::std::vector<Associated> linkAssoc;
    /// Nodes that need revisiting (e.g. method calls when the receiver isn't known)
    ::std::vector<HIRExprNode*> toVisit;
    /// Callback-based revisits (e.g. for slice patterns handling slices/arrays)
    ::std::vector<::std::unique_ptr<Revisitor>> advRevisits;

    // Keep track of if an ivar is used in a context where it has to be Sized
    // - If it is, then we can discount any unsized possibilities
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

    const HIRSimplePath mLangBox;

    Context(const HIRCrate& crate, const HIRGenericParams* implParams, const HIRGenericParams* itemParams, const HIRSimplePath& modPath, const HIRGenericPath* currentTrait, const HIRTraitImpl* currentTraitImpl);

    void dump() const;

    bool takeChanged() {
        return ivars.takeChanged();
    }

    bool hasRules() const {
        return !(linkCoerce.empty() && linkAssoc.empty() && toVisit.empty() && advRevisits.empty());
    }

    inline void addIvars(HIRTypeRef& ty) {
        ivars.addIvars(ty);
    }

    // - Equate two types, with no possibility of coercion
    //  > Errors if the types are incompatible.
    //  > Forces types if one side is an infer
    void equateTypes(const Span& sp, const HIRTypeData* l, const HIRTypeData* r);
    void equateTypesInner(const Span& sp, const HIRTypeData* l, const HIRTypeData* r);
    // - Equate two types, allowing inferrence
    void equateTypesCoerce(const Span& sp, const HIRTypeData* l, HIRExprNodeP& nodePtr);
    void recordCoercionHint(const HIRTypeData* type, HIRExprNodeP& nodePtr);

    const HIRTypeData* coercionHint(const HIRExprNode& node) const;
    // - Equate a type to an associated type (if name == "", no equation is done, but trait is searched)
    void equateTypesAssoc(const Span& sp, const HIRTypeData* l, const HIRSimplePath& trait, HIRPathParams params, const HIRTypeData* implTy, const char* name, const HIRPathParams& atyPp, bool isOp = false, TypeckPrimitiveOperator operatorKind = TypeckPrimitiveOperator::None);

    bool isCurrentOperatorImpl(const ImplRef& impl) const;

    // A Deref implementation for a native pointer/reference receives `&Self`.
    // Dereferencing that receiver is the native step needed to recover `Self`,
    // not another dispatch through a potentially overlapping Deref impl.
    bool isCurrentNativeDerefReceiver(const HIRSimplePath& derefTrait, const HIRTypeData* operand) const;

    // Equate const generics (values)
    void equateValues(const Span& sp, const HIRConstGeneric& rl, const HIRConstGeneric& rr);

    /// Adds a `ty: Sized` bound to the contained ivars.
    void requireSized(const Span& sp, const HIRTypeData* ty);

    // - Add a trait bound (gets encoded as an associated type bound)
    void addTraitBound(const Span& sp, const HIRTypeData* implTy, const HIRSimplePath& trait, HIRPathParams params) {
        equateTypesAssoc(sp, crate.types.infer(), trait, mv$(params), implTy, "", {}, false);
    }

    /// Get the `possible_ivar_vals` entry for the given ivar index
    /// Returns `nullptr` if the ivar is already known
    IVarPossible* getIvarPossibilities(const Span& sp, unsigned int ivarIndex);

    enum class IvarUnknownType {
        /// Coercion to an unknown type (disables
        To,
        /// Coercion from an unknown type
        From,
        /// Bounded to be an unknown type (a strong disable)
        Bound,
    };
    /// Type is unknown (e.g. no used/results from a trait impl that can't be looked up)
    void possibleEquateTypeUnknown(const Span& sp, const HIRTypeData* ty, IvarUnknownType srcTy);
    /// Type must be one of the provided set
    void possibleEquateTypeBounds(const Span& sp, const HIRTypeData* ty, ::std::vector<HIRTypeRef> t);

    // ----
    // IVar possibilties
    // ----

    enum class PossibleTypeSource {
        CoerceTo,   //!< IVar must coerce to this type
        UnsizeTo,   //!< IVar must unsize to this type
        CoerceFrom, //!< IVar must coerce from this type
        UnsizeFrom, //!< IVar must unsize from this type
    };

    /// Default type
    //void possible_equate_ivar_def(unsigned int ivar_index, const ::HIR::TypeData* t);

    /// Record that the IVar may be this type (and what the source is)
    void possibleEquateIvar(const Span& sp, unsigned int ivarIndex, const HIRTypeData* t, PossibleTypeSource srcTy);
    /// Add a possible type for an ivar (which is used if only one possibility meets available bounds)
    void possibleEquateIvarBounds(const Span& sp, unsigned int ivarIndex, ::std::vector<HIRTypeRef> t);
    /// Record that the IVar is equated to an unknown type
    void possibleEquateIvarUnknown(const Span& sp, unsigned int ivarIndex, IvarUnknownType srcTy);

    // ----
    // Patterns and bindings
    // ----

    // - Add a pattern binding (forcing the type to match)
    void handlePattern(const Span& sp, HIRPattern& pat, const HIRTypeData* type, bool isIrrefutable = false);
    void handlePatternDirectInner(const Span& sp, HIRPattern& pat, const HIRTypeData* type);
    void addBindingInner(const Span& sp, const HIRPatternBinding& pb, HIRTypeRef type);

    void addVar(const Span& sp, unsigned int index, const RcString& name, HIRTypeRef type);
    const HIRTypeData* getVar(const Span& sp, unsigned int idx) const;

    // - Add a revisit entry
    void addRevisit(HIRExprNode& node);
    void addRevisitAdv(::std::unique_ptr<Revisitor> ent);

    const HIRTypeData* getType(const HIRTypeData* ty) const {
        return ivars.getType(ty);
    }

    /// Create an autoderef operation from val_node->m_res_type to ty_dst (handling implicit unsizing)
    HIRExprNodeP createAutoderef(HIRExprNodeP valNode, HIRTypeRef tyDst) const;

private:
    void addIvarsParams(HIRPathParams& params) {
        ivars.addIvarsParams(params);
    }
};

extern bool visitCallPopulateCache(Context& context, const Span& sp, HIRPath& path, HIRExprCallCache& cache) __attribute__((warnUnusedResult));

extern void TypecheckCodeCSEnumerateRules(Context& context, const TypeckModuleState& ms, tArgs& args, const HIRTypeData* resultType, HIRExprPtr& expr, HIRExprNodeP& rootPtr);
