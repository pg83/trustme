#pragma once

#include "hir_path.h"
#include "hir_type.h"
#include "hir_typeck_common.h"

#include <unordered_map>

class StaticTraitResolve;
struct WireBoard;

class HIRCrate;
class HIRFunction;
class HIRStatic;

// TODO: This is very similar to "hir_typeck/common.h" MonomorphState, except it owns its data
struct TransParams: public MonomorphiserPP {
    Span sp;
    const HIRGenericParams* gdefImpl;
    HIRPathParams ppMethod;
    HIRPathParams ppImpl;
    const HIRType* selfType;
    bool forceMonomorphisation;

    explicit TransParams(HIRTypeInterner& types);

    TransParams(HIRTypeInterner& types, const Span& sp);

    TransParams(TransParams&& x);

    TransParams& operator=(TransParams&& x);

    TransParams(const TransParams&) = delete;
    TransParams& operator=(const TransParams&) = delete;

    static TransParams newImpl(HIRTypeInterner& types, Span sp, const HIRType* ty, HIRPathParams implParams);

    const HIRType* maybeMonomorph(const ::StaticTraitResolve& resolve, const HIRType* p) const;

    const HIRType* monomorph(const ::StaticTraitResolve& resolve, const HIRType* p) const;
    HIRPath monomorph(const ::StaticTraitResolve& resolve, const HIRPath& p) const;
    HIRGenericPath monomorph(const ::StaticTraitResolve& resolve, const HIRGenericPath& p) const;
    HIRPathParams monomorph(const ::StaticTraitResolve& resolve, const HIRPathParams& p) const;

    bool hasTypes() const {
        return forceMonomorphisation || ppMethod.hasParams() || ppImpl.hasParams();
    }

    const HIRType* getSelfType() const override;

    const HIRPathParams* getImplParams() const override;

    const HIRPathParams* getMethodParams() const override;

    const HIRPathParams* getHrbParams() const override;
};

struct CachedFunction {
    const HIRType* retTy;
    HIRFunction::argsT argTys;
    MIRFunctionPointer code;
};

struct TransListFunction {
    const HIRPath* path;
    const HIRFunction* ptr;
    HIRFunction* mutPtr;
    TransParams pp;

    CachedFunction monomorphised;

    bool forcePrototype;

    TransListFunction(HIRTypeInterner& types, const HIRPath& path);
};

struct TransListStatic {
    const HIRStatic* ptr;
    TransParams pp;

    explicit TransListStatic(HIRTypeInterner& types);
};

struct TransListConst {
    const HIRConstant* ptr;
    TransParams pp;

    explicit TransListConst(HIRTypeInterner& types);
};

class TransList {
    const WireBoard* wb_ = nullptr;

    std::unordered_map<std::string, HIRPath> functionSymbols;
    std::unordered_map<std::string, HIRPath> staticSymbols;

    struct TypeEmissionState {
        const HIRType* canonical;
        bool hasPrototype;
        bool hasDefinition;
    };

    std::unordered_map<std::string, TypeEmissionState> typeSymbols;

public:
    TransList() = default;

    explicit TransList(const WireBoard& wb)
        : wb_(&wb)
    {
    }

    TransList(TransList&&) = default;
    TransList(const TransList&) = delete;
    TransList& operator=(TransList&&) = default;
    TransList& operator=(const TransList&) = delete;

    std::vector<HIRPath> roots;

    std::map<HIRPath, std::unique_ptr<TransListFunction>> functions;
    std::map<HIRPath, std::unique_ptr<TransListStatic>> statics;

    std::map<HIRPath, std::unique_ptr<TransListConst>> constants;
    std::map<HIRPath, TransParams> vtables;

    HIRTypeRefSet typeids;

    HIRTypeRefSet dropGlue;

    std::set<HIRGenericPath> constructors;

    HIRTypeRefSet autoCloneImpls;

    HIRTypeRefSet autoCloneFromImpls;

    HIRTypeRefSet autoFnptrImpls;

    std::set<HIRPath> traitObjectMethods;

    std::vector<std::unique_ptr<HIRStatic>> autoStatics;
    std::vector<std::unique_ptr<HIRFunction>> autoFunctions;

    std::vector<std::pair<const HIRType*, bool>> types;

    TransListFunction* addFunction(HIRTypeInterner& types, HIRPath p);
    TransListStatic* addStatic(HIRTypeInterner& types, HIRPath p);
    TransListConst* addConst(HIRTypeInterner& types, HIRPath p);
    TransListFunction* findFunction(const HIRPath& p);
    const TransListFunction* findFunction(const HIRPath& p) const;
    bool hasType(const HIRType* type, bool shallow) const;
    bool addType(const HIRType* type, bool shallow);
    void clearTypes();

    bool addVtable(HIRPath p, TransParams pp) {
        return vtables.insert(std::make_pair(mv$(p), mv$(pp))).second;
    }
};
