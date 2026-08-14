#include "hir_typeck_main_bindings.h"
#include "hir_typeck_main_bindings.h"

#include "hir_hir.h"
#include "hir_expr.h"
#include "wire_board.h"
#include "hir_visitor.h"
#include "hir_typeck_static.h"

#include <algorithm>

namespace {

    const HIRGenericParams& getParamsForItem(const Span& sp, const HIRCrate& crate, const HIRSimplePath& path, HIRVisitor::PathContext pc) {
        // Support for enum variants
        if (path.components().size() > 1) {
            const auto& pitem = crate.getTypeitemByPath(sp, path, false, true);
            if (pitem.is_Enum()) {
                return pitem.as_Enum().mParams;
            }
        }

        switch (pc) {
            case HIRVisitor::PathContext::VALUE: {
                const auto& item = crate.getValitemByPath(sp, path);

                TU_MATCH(
                    HIRValueItem,
                    (item),
                    (e),
                    (Import, BUG(sp, "Value path pointed to import - " << path << " = " << e.path);),
                    (Function, return e.mParams;),
                    (Constant, return e.mParams;),
                    (Static,
                     // TODO: Return an empty set?
                     BUG(sp, "Attepted to get parameters for static " << path);),
                    (StructConstructor, return getParamsForItem(sp, crate, e.ty, HIRVisitor::PathContext::TYPE);),
                    (StructConstant, return getParamsForItem(sp, crate, e.ty, HIRVisitor::PathContext::TYPE);)
                )
            } break;
            case HIRVisitor::PathContext::TRAIT:
                // TODO: treat PathContext::TRAIT differently
            case HIRVisitor::PathContext::TYPE: {
                const auto& item = crate.getTypeitemByPath(sp, path);

                TU_MATCH(HIRTypeItem, (item), (e), (Import, BUG(sp, "Type path pointed to import - " << path);), (TypeAlias, BUG(sp, "Type path pointed to type alias - " << path);), (TraitAlias, BUG(sp, "Type path pointed to trait alias - " << path);), (ExternType, static HIRGenericParams emptyParams; return emptyParams;), (Module, BUG(sp, "Type path pointed to module - " << path);), (Struct, return e.mParams;), (Enum, return e.mParams;), (Union, return e.mParams;), (Trait, return e.mParams;))
            } break;
        }
        throw "";
    }

    class Visitor: public HIRVisitor {
        HIRCrate& crate;
        StaticTraitResolve mResolve;

        const HIRTrait* currentTrait = nullptr;
        const HIRItemPath* mCurrentTraitPath = nullptr;

        HIRGenericParams* curParams = nullptr;
        unsigned curParamsLevel = 0;
        HIRItemPath* fcnPath = nullptr;
        HIRFunction* fcnPtr = nullptr;
        unsigned int fcnErasedCount = 0;

        ::std::vector<const HIRTypeData*> selfTypes;

        typedef ::std::vector<::std::pair<const HIRSimplePath*, const HIRTrait*>> tTraitImports;
        tTraitImports traits;

    public:
        Visitor(const WireBoard& wb, HIRCrate& crate)
            : HIRVisitor(nullptr, crate.types)
            , crate(crate)
            , mResolve(wb)
        {
        }

    private:
        struct ModTraitsGuard {
            Visitor* v;
            tTraitImports oldImports;

            ~ModTraitsGuard() {
                this->v->traits = mv$(this->oldImports);
            }
        };

        ModTraitsGuard pushModTraits(const HIRModule& mod) {
            static Span sp;
            DEBUG("");
            auto rv = ModTraitsGuard{this, mv$(this->traits)};
            for (const auto& traitPath : mod.traits) {
                DEBUG("- " << traitPath);
                traits.push_back(::std::make_pair(&traitPath, &this->crate.getTraitByPath(sp, traitPath)));
            }
            return rv;
        }

        void checkParameters(const Span& sp, const HIRGenericParams& paramDef, HIRPathParams& paramVals) {
            MonomorphStatePtr ms(crate.types, selfTypes.empty() ? nullptr : selfTypes.back(), &paramVals, nullptr);

            while (paramVals.types.size() < paramDef.types.size()) {
                unsigned int i = paramVals.types.size();
                const auto& tyDef = paramDef.types[i];
                if (tyDef.defaultValue->is_Infer()) {
                    ERROR(sp, E0000, "Unspecified parameter with no default - " << paramDef.fmtArgs() << " with " << paramVals);
                }

                // Replace and expand
                paramVals.types.push_back(ms.monomorphType(sp, tyDef.defaultValue));
                DEBUG("Add missing param (using default): " << paramVals.types.back());
            }

            if (paramVals.types.size() != paramDef.types.size()) {
                ERROR(sp, E0000, "Incorrect number of parameters - expected " << paramDef.types.size() << ", got " << paramVals.types.size());
            }

            for (unsigned int i = 0; i < paramVals.types.size(); i++) {
                if (paramVals.types[i] == HIRTypeRef()) {
                    // TODO: Why is this pulling in the default? Why not just leave it as-is

                    //if( param_def.m_types[i].m_default == ::HIR::mkType() )
                    // TODO: Monomorphise?
                    paramVals.types[i] = ms.monomorphType(sp, paramDef.types[i].defaultValue);
                    DEBUG("Update `_` param (using default): " << paramDef.types[i].defaultValue << " -> " << paramVals.types[i]);
                }
            }

            // TODO: Check generic bounds
            for (const auto& bound : paramDef.bounds) {
                TU_MATCH(
                    HIRGenericBound,
                    (bound),
                    (e),
                    (TraitBound,
                     // TODO: Check for an implementation of this trait
                     DEBUG("TODO: Check bound " << e.type << " : " << e.trait.mPath);),
                    (TypeEquality,
                     // TODO: Check that two types are equal in this case
                     DEBUG("TODO: Check equality bound " << e.type << " == " << e.otherType);)
                )
            }
        }

    public:
        void visitPathParams(HIRPathParams& pp) override {
            static Span _sp;
            const Span& sp = _sp;

            HIRVisitor::visitPathParams(pp);
        }

        void visitType(HIRTypeRef& ty) override {
            static Span _sp;
            const Span& sp = _sp;

            assert(ty);
            auto data = ty->cloneData();

            auto self = crate.types.self();
            if (data.is_ErasedType()) {
                selfTypes.push_back(self);
            }

            auto savedParams = std::make_pair(curParams, curParamsLevel);

            TU_MATCH_HDRA((data), {)
            TU_ARMA(Infer, e) {
                }
                TU_ARMA(Diverge, e) {
                }
                TU_ARMA(Primitive, e) {
                }
                TU_ARMA(Generic, e) {
                }
                TU_ARMA(Path, e) this->visitPath(e.path, HIRVisitor::PathContext::TYPE);
                TU_ARMA(TraitObject, e) {
                    if (e.mTrait.mPath != HIRSimplePath()) {
                        this->visitTraitPath(e.mTrait);
                    }
                    for (auto& marker : e.markers) {
                        this->visitGenericPath(marker, HIRVisitor::PathContext::TYPE);
                    }
                }
                TU_ARMA(ErasedType, e) {
                TU_MATCH_HDRA((e.inner), {)
                TU_ARMA(Known, inner) this->visitType(inner);
                        TU_ARMA(Alias, inner) this->visitPathParams(inner.params);
                        TU_ARMA(Fcn, inner) if (inner.origin != HIRSimplePath()) this->visitPath(inner.origin, HIRVisitor::PathContext::VALUE);
                }
                this->visitPathParams(e.use);
                for (auto& trait : e.traits) this->visitTraitPath(trait);
                }
                TU_ARMA(Array, e) {
                    this->visitType(e.inner);
                    if (auto* size = e.size.opt_Unevaluated()) {
                        this->visitConstgeneric(*size);
                    }
                }
                TU_ARMA(Slice, e) this->visitType(e.inner);
                TU_ARMA(Tuple, e) for (auto& inner : e) this->visitType(inner);
                TU_ARMA(Borrow, e) this->visitType(e.inner);
                TU_ARMA(Pointer, e) this->visitType(e.inner);
                TU_ARMA(NamedFunction, e) this->visitPath(e.path, HIRVisitor::PathContext::VALUE);
                TU_ARMA(Function, e) {
                    for (auto& arg : e.argTypes) {
                        this->visitType(arg);
                    }
                    this->visitType(e.mRettype);
                }
                TU_ARMA(NodeType, e) {
                }
            }

            curParams = savedParams.first;
            curParamsLevel = savedParams.second;

            if (data.is_ErasedType()) {
                selfTypes.pop_back();
            }

            ty = crate.types.intern(mv$(data));

            if (const auto* e = ty->opt_Path()) {
                TU_MATCH(HIRPath::Data, (e->path.mData), (pe), (Generic, ), (UfcsUnknown, TODO(sp, "Should UfcsKnown be encountered here?");), (UfcsInherent, TRACE_FUNCTION_FR("UfcsInherent - " << ty, ty); mResolve.expandAssociatedTypes(sp, ty);), (UfcsKnown, TRACE_FUNCTION_FR("UfcsKnown - " << ty, ty); mResolve.expandAssociatedTypes(sp, ty);))
            }
        }

        void visitGenericPath(HIRGenericPath& p, PathContext pc) override {
            static Span sp;
            TRACE_FUNCTION_F("p = " << p);
            const auto& params = getParamsForItem(sp, crate, p.mPath, pc);
            auto& args = p.mParams;

            checkParameters(sp, params, args);
            DEBUG("p = " << p);

            HIRVisitor::visitGenericPath(p, pc);
        }

    private:
        bool locateTraitItemInBounds(const Span& sp, HIRVisitor::PathContext pc, const HIRTypeData* tr, const HIRGenericParams& params, HIRPath::Data& pd) {
            for (const auto& b : params.bounds) {
                TU_IFLET(HIRGenericBound, b, TraitBound, e, DEBUG("- " << e.type << " : " << e.trait.mPath); if (e.type == tr) {
                    DEBUG(" - Match");
                    if (locateInTraitAndSet(sp, pc, e.trait.mPath, this->crate.getTraitByPath(sp, e.trait.mPath.mPath), pd)) {
                        return true;
                    }
                });
                // -
            }
            return false;
        }

        static HIRPath::Data getUfcsKnown(HIRPath::Data::Data_UfcsUnknown e, HIRGenericPath traitPath, const HIRTrait& trait) {
            return HIRPath::Data::make_UfcsKnown({mv$(e.type), mv$(traitPath), mv$(e.item), mv$(e.params)});
        }

        static bool locateItemInTrait(HIRVisitor::PathContext pc, const HIRTrait& trait, HIRPath::Data& pd) {
            const auto& e = pd.as_UfcsUnknown();

            switch (pc) {
                case HIRVisitor::PathContext::VALUE:
                    if (trait.values.find(e.item) != trait.values.end()) {
                        return true;
                    }
                    break;
                case HIRVisitor::PathContext::TRAIT:
                    break;
                case HIRVisitor::PathContext::TYPE:
                    if (trait.types.find(e.item) != trait.types.end()) {
                        return true;
                    }
                    break;
            }
            return false;
        }

        bool locateInTraitAndSet(const Span& sp, HIRVisitor::PathContext pc, const HIRGenericPath& traitPath, const HIRTrait& trait, HIRPath::Data& pd) {
            if (locateItemInTrait(pc, trait, pd)) {
                pd = getUfcsKnown(mv$(pd.as_UfcsUnknown()), makeGenericPath(traitPath.mPath, trait), trait);
                return true;
            }
            // Search all supertraits
            for (const auto& pt : trait.allParentTraits) {
                if (locateItemInTrait(pc, *pt.traitPtr, pd)) {
                    pd = getUfcsKnown(mv$(pd.as_UfcsUnknown()), makeGenericPath(traitPath.mPath, trait), trait);
                    return true;
                }
            }
            return false;
        }

        bool setFromImpl(const HIRGenericPath& traitPath, const HIRTrait& trait, HIRPath::Data& pd) {
            auto& e = pd.as_UfcsUnknown();
            const auto& type = e.type;
            return this->crate.findTraitImpls(traitPath.mPath, type, HIRResolvePlaceholdersNop(), [&](const auto& impl) {
                DEBUG("FOUND impl" << impl.mParams.fmtArgs() << " " << traitPath.mPath << impl.traitArgs << " for " << impl.mType);
                // TODO: Check bounds
                for (const auto& bound : impl.mParams.bounds) {
                    DEBUG("- TODO: Bound " << bound);
                    return false;
                }
                pd = getUfcsKnown(mv$(e), makeGenericPath(traitPath.mPath, trait), trait);
                return true;
            });
        }

        bool locateInTraitImplAndSet(HIRVisitor::PathContext pc, const HIRGenericPath& traitPath, const HIRTrait& trait, HIRPath::Data& pd) {
            auto& e = pd.as_UfcsUnknown();
            if (this->locateItemInTrait(pc, trait, pd)) {
                return this->setFromImpl(traitPath, trait, pd);
            } else {
                DEBUG("- Item " << e.item << " not in trait " << traitPath.mPath);
            }

            // Search supertraits (recursively)
            for (const auto& pt : trait.allParentTraits) {
                if (this->locateItemInTrait(pc, *pt.traitPtr, pd)) {
                    // TODO: Monomorphise params?
                    return setFromImpl(pt.mPath, *pt.traitPtr, pd);
                } else {
                }
            }
            return false;
        }

        HIRGenericPath makeGenericPath(HIRSimplePath sp, const HIRTrait& trait) {
            auto traitPathG = HIRGenericPath(mv$(sp));
            for (unsigned int i = 0; i < trait.mParams.types.size(); i++) {
                traitPathG.mParams.types.push_back(crate.types.generic(trait.mParams.types[i].mName, i));
            }
            return traitPathG;
        }

        HIRGenericPath getCurrentTraitGp() const {
            assert(mCurrentTraitPath);
            assert(currentTrait);
            auto traitPath = HIRGenericPath(mCurrentTraitPath->getSimplePath());
            for (unsigned int i = 0; i < currentTrait->mParams.types.size(); i++) {
                traitPath.mParams.types.push_back(crate.types.generic(currentTrait->mParams.types[i].mName, i));
            }
            return traitPath;
        }

        void visitPathUfcsUnknown(const Span& sp, HIRPath& p, HIRVisitor::PathContext pc) {
            TRACE_FUNCTION_FR("UfcsUnknown - p=" << p, p);
            auto& e = p.mData.as_UfcsUnknown();

            this->visitType(e.type);
            this->visitPathParams(e.params);

            // Search for matching impls in current generic blocks
            if (mResolve.itemGenericsPtr() != nullptr && locateTraitItemInBounds(sp, pc, e.type, *mResolve.itemGenericsPtr(), p.mData)) {
                return;
            }
            if (mResolve.implGenericsPtr() != nullptr && locateTraitItemInBounds(sp, pc, e.type, *mResolve.implGenericsPtr(), p.mData)) {
                return;
            }

            if (const auto* te = e.type->opt_Generic()) {
                // If processing a trait, and the type is 'Self', search for the type/method on the trait
                // - TODO: This could be encoded by a `Self: Trait` bound in the generics, but that may have knock-on issues?
                if (te->name == "Self" && currentTrait) {
                    auto traitPath = this->getCurrentTraitGp();
                    if (this->locateInTraitAndSet(sp, pc, traitPath, *currentTrait, p.mData)) {
                        // Success!
                        return;
                    }
                }
                ERROR(sp, E0000, "Failed to find impl with '" << e.item << "' for " << e.type);
                return;
            } else {
                // 1. Search for applicable inherent methods (COMES FIRST!)
                if (this->crate.findTypeImpls(e.type, HIRResolvePlaceholdersNop(), [&](const auto& impl) {
                    DEBUG("- matched inherent impl " << e.type);
                    // Search for item in this block
                    switch (pc) {
                        case HIRVisitor::PathContext::VALUE:
                            if (impl.methods.find(e.item) == impl.methods.end()) {
                                return false;
                            }
                            // Found it, just keep going (don't care about details here)
                            break;
                        case HIRVisitor::PathContext::TRAIT:
                            return false;
                        case HIRVisitor::PathContext::TYPE:
                            if (impl.types.find(e.item) == impl.types.end()) {
                                return false;
                            }
                            break;
                    }

                    return true;
                })) {
                    auto newData = HIRPath::Data::make_UfcsInherent({mv$(e.type), mv$(e.item), mv$(e.params)});
                    p.mData = mv$(newData);
                    DEBUG("- Resolved, replace with " << p);
                    return;
                }
                // 2. Search all impls of in-scope traits for this method on this type
                for (const auto& traitInfo : traits) {
                    const auto& trait = *traitInfo.second;

                    switch (pc) {
                        case HIRVisitor::PathContext::VALUE:
                            if (trait.values.find(e.item) == trait.values.end()) {
                                continue;
                            }
                            break;
                        case HIRVisitor::PathContext::TRAIT:
                        case HIRVisitor::PathContext::TYPE:
                            if (trait.types.find(e.item) == trait.types.end()) {
                                continue;
                            }
                            break;
                    }
                    DEBUG("- Trying trait " << *traitInfo.first);

                    auto traitPath = HIRGenericPath(*traitInfo.first);
                    for (unsigned int i = 0; i < trait.mParams.types.size(); i++) {
                        traitPath.mParams.types.push_back(crate.types.infer());
                    }

                    // TODO: Search supertraits
                    // TODO: Should impls be searched first, or item names?
                    // - Item names add complexity, but impls are slower
                    if (this->locateInTraitImplAndSet(pc, mv$(traitPath), trait, p.mData)) {
                        return;
                    }
                }
            }

            // Couldn't find it
            ERROR(sp, E0000, "Failed to find impl with '" << e.item << "' for " << e.type << " (in " << p << ")");
        }

    public:
        void visitExpr(HIRExprPtr& exp) override {
            // No-op
        }

        void visitPath(HIRPath& p, HIRVisitor::PathContext pc) override {
            TU_MATCH(
                HIRPath::Data,
                (p.mData),
                (e),
                (Generic, this->visitGenericPath(e, pc);),
                (
                    UfcsKnown, this->visitType(e.type); selfTypes.push_back(e.type); this->visitGenericPath(e.trait, HIRVisitor::PathContext::TRAIT); selfTypes.pop_back();
                    // TODO: Locate impl block and check parameters
                ),
                (
                    UfcsInherent, this->visitType(e.type);
                    // TODO: Locate impl block and check parameters
                ),
                (UfcsUnknown, BUG(Span(), "Encountered unknown-trait UFCS path during outer typeck - " << p);)
            )
        }

        void visitParams(HIRGenericParams& params) override {
            TRACE_FUNCTION_F(params.fmtArgs());
            for (auto& tps : params.types) {
                this->visitType(tps.defaultValue);
            }

            for (auto& bound : params.bounds) {
                TU_MATCH_HDRA( (bound), {)
                TU_ARMA(TraitBound, e) {
                        this->visitType(e.type);
                        selfTypes.push_back(e.type);
                        this->visitTraitPath(e.trait);
                        selfTypes.pop_back();
                    }
                    //(NotTrait, e) {
                    //    ::HIR::ASTType*  type;
                    //    ::HIR::GenricPath    trait;
                    //    }),
                    TU_ARMA(TypeEquality, e) {
                        this->visitType(e.type);
                        this->visitType(e.otherType);
                    }
                }
            }
        }

        void visitModule(HIRItemPath p, HIRModule& mod) override {
            auto _ = this->pushModTraits(mod);
            HIRVisitor::visitModule(p, mod);
        }

        void visitTrait(HIRItemPath p, HIRTrait& item) override {
            currentTrait = &item;
            mCurrentTraitPath = &p;

            auto _ = mResolve.setImplGenerics(MetadataType::TraitObject, item.mParams);
            auto self = crate.types.self();
            selfTypes.push_back(self);
            HIRVisitor::visitTrait(p, item);
            selfTypes.pop_back();

            currentTrait = nullptr;
        }

        void visitTraitAlias(HIRItemPath p, HIRTraitAlias& item) override {
            auto _ = mResolve.setImplGenerics(MetadataType::TraitObject, item.mParams);
            auto self = crate.types.self();
            selfTypes.push_back(self);
            HIRVisitor::visitTraitAlias(p, item);
            selfTypes.pop_back();
        }

        void visitStruct(HIRItemPath p, HIRStruct& item) override {
            auto _ = mResolve.setImplGenerics(item.structMarkings.dstType, item.mParams);
            HIRVisitor::visitStruct(p, item);
        }

        void visitUnion(HIRItemPath p, HIRUnion& item) override {
            auto _ = mResolve.setImplGenerics(MetadataType::None, item.mParams);
            HIRVisitor::visitUnion(p, item);
        }

        void visitEnum(HIRItemPath p, HIREnum& item) override {
            auto _ = mResolve.setImplGenerics(MetadataType::None, item.mParams);
            HIRVisitor::visitEnum(p, item);
        }

        void visitAssociatedtype(HIRItemPath p, HIRAssociatedType& item) override {
            // Push `Self = <Self as CurTrait>::Type` for processing defaults in the bounds.
            auto pathAty = HIRPath(crate.types.self(), this->getCurrentTraitGp(), p.getName());
            auto tyAty = crate.types.path(mv$(pathAty), HIRTypePathBinding::make_Opaque({}));
            selfTypes.push_back(tyAty);

            HIRVisitor::visitAssociatedtype(p, item);

            selfTypes.pop_back();
        }

        void visitTypeAlias(HIRItemPath p, HIRTypeAlias& item) override {
            // Ignore type aliases, they don't have to typecheck.
        }

        void visitInherentType(HIRItemPath p, HIRTypeAlias& item) override {
            auto _ = mResolve.setItemGenerics(item.mParams);
            auto savedParams = std::make_pair(curParams, curParamsLevel);
            curParams = &item.mParams;
            curParamsLevel = 1;
            HIRVisitor::visitInherentType(p, item);
            curParams = savedParams.first;
            curParamsLevel = savedParams.second;
        }

        void visitTypeImpl(HIRTypeImpl& impl) override {
            TRACE_FUNCTION_F("impl " << impl.mType);
            auto _ = mResolve.setImplGenerics(impl.mType, impl.mParams);
            selfTypes.push_back(impl.mType);

            // Pre-visit so lifetime elision can work
            {
                curParams = &impl.mParams;
                curParamsLevel = 0;
                this->visitType(impl.mType);
                curParams = nullptr;
            }

            HIRVisitor::visitTypeImpl(impl);
            // TODO: Check that the type is valid

            selfTypes.pop_back();
        }

        void visitTraitImpl(const HIRSimplePath& traitPath, HIRTraitImpl& impl) override {
            static Span sp;
            TRACE_FUNCTION_F("impl" << impl.mParams.fmtArgs() << " " << traitPath << impl.traitArgs << " for " << impl.mType);
            auto _ = mResolve.setImplGenerics(impl.mType, impl.mParams);
            selfTypes.push_back(impl.mType);

            // Pre-visit so lifetime elision can work
            {
                curParams = &impl.mParams;
                curParamsLevel = 0;
                this->visitType(impl.mType);
                this->visitPathParams(impl.traitArgs);
                curParams = nullptr;
            }

            HIRVisitor::visitTraitImpl(traitPath, impl);
            selfTypes.pop_back();

            // TODO: Check that the type+trait is valid
            // - And fix bad elided liftimes (match annotations if they were elided)
            {
                const auto& trait = mResolve.hirCrate().getTraitByPath(sp, traitPath);
                for (auto& e : impl.methods) {
                    auto _ = mResolve.setItemGenerics(e.second.data.mParams);

                    const auto vIt = trait.values.find(e.first);
                    if (vIt == trait.values.end() || !vIt->second.is_Function()) {
                        ERROR(sp, E0000, "Trait " << traitPath << " doesn't have a method named " << e.first);
                    }
                    auto& implFcn = e.second.data;
                    const auto& traitFcn = vIt->second.as_Function();

                    auto fcnParams = traitFcn.mParams.makeNopParams(crate.types, 1);
                    MonomorphStatePtr ms{crate.types, impl.mType, &impl.traitArgs, &fcnParams};
                    HIRTypeRef tmp;
                    auto maybeMonomorph = [&](const HIRTypeData* ty) -> const HIRTypeData* {
                        if (monomorphiseTypeNeeded(ty)) {
                            tmp = ms.monomorphType(sp, ty);
                            mResolve.expandAssociatedTypes(sp, tmp);
                            return tmp;
                        } else {
                            return ty;
                        }
                    };

                    // Check signature
                    // - Includes fixing incorrectly elided lifetimes
                    // ```
                    // trait Foo<T> {
                    // }
                    // impl Foo<&Bar> for Baz {
                    //   fn foo(&self, bar: &Bar) { }
                    // }

                    std::vector<std::string> failures;
                    // -- Generics
                    if (implFcn.mParams.types.size() != traitFcn.mParams.types.size()) {
                        failures.push_back(FMT("Mismatched type param count (expected " << traitFcn.mParams.types.size() << ", got " << implFcn.mParams.types.size() << ")"));
                    }
                    // Different logic for lifetimes, only want to check un-elided lifetimes
                    // - Well, elided lifetimes can overlap non-elided ones (as long as they're identical)
                    if (implFcn.mParams.values.size() != traitFcn.mParams.values.size()) {
                        failures.push_back(FMT("Mismatched const param count (expected " << traitFcn.mParams.values.size() << ", got " << implFcn.mParams.values.size() << ")"));
                    }
                    // -- Arguments
                    if (implFcn.mArgs.size() != traitFcn.mArgs.size()) {
                        failures.push_back(FMT("Mismatched argument count (expected " << traitFcn.mArgs.size() << ", got " << implFcn.mArgs.size() << ")"));
                    }
                    if (implFcn.receiver != traitFcn.receiver) {
                        failures.push_back(FMT("Receiver type")); //"(expected " << trait_fcn.m_receiver << ", got " << impl_fcn.m_receiver));
                    }
                    for (size_t i = 0; i < std::min(implFcn.mArgs.size(), traitFcn.mArgs.size()); i++) {
                        if (!(i == 0 && (traitFcn.receiver == HIRFunction::Receiver::Free || implFcn.receiver == HIRFunction::Receiver::Free))) {
                            // Check the type.
                            // - Also, fix lifetime elision?
                            const auto& expTy = maybeMonomorph(traitFcn.mArgs[i].second);
                            /*const*/ auto& hasTy = implFcn.mArgs[i].second;

                            if (expTy != hasTy && !expTy->equalsIgnoringRegions(hasTy)) {
                                failures.push_back(FMT("Argument " << 1 + i << " mismatch - expected " << expTy << ", got " << hasTy));
                            }
                        }
                    }

                    // Handle `implTrait` in returns
                    // - Would need to re-create `exp_ret_ty` to keep the `impl Trait`, OR keep a non-erased/expanded copy of the type
                    // > The difference tends to be in lifetimes, so match the two types and update lifetimes?
                    struct MCB: public HIRMatchGenerics {
                        ::std::map<RcString, const HIRTypeData*> mapping;
                        ::std::map<unsigned int, const HIRTypeData*> rpitMapping;

                        HIRCompare cmpType(const Span& sp, const HIRTypeData* tyL, const HIRTypeData* tyR, tCbResolveType resolveCb) override {
                            if (const auto* erased = tyL->opt_ErasedType(); erased && erased->inner.is_Fcn()) {
                                const auto index = erased->inner.as_Fcn().index;
                                const auto inserted = rpitMapping.insert(std::make_pair(index, tyR));
                                if (!inserted.second) {
                                    return HIRMatchGenerics::cmpType(sp, inserted.first->second, tyR, resolveCb);
                                }
                                return HIRCompare::Equal;
                            }
                            // If the LHS is an ATY that starts with `erased#` then just accept it?
                            // - Also record the mapping
                            if (const auto* tyP = tyL->opt_Path()) {
                                if (const auto* pathP = tyP->path.mData.opt_UfcsKnown()) {
                                    if (pathP->item.compare(0, strlen(ATY_PREFIX_ERASED), ATY_PREFIX_ERASED) == 0) {
                                        mapping.insert(std::make_pair(pathP->item, tyR));
                                        return HIRCompare::Equal;
                                    }
                                }
                            }
                            return HIRMatchGenerics::cmpType(sp, tyL, tyR, resolveCb);
                        }

                        HIRCompare matchTy(const HIRGenericRef& g, const HIRTypeData* ty, tCbResolveType resolveCb) override {
                            return (!ty->is_Generic() || ty->as_Generic() != g) ? HIRCompare::Unequal : HIRCompare::Equal;
                        }

                        HIRCompare matchVal(const HIRGenericRef& g, const HIRConstGeneric& sz) override {
                            return (!sz.is_Generic() || sz.as_Generic() != g) ? HIRCompare::Unequal : HIRCompare::Equal;
                        }
                    } matchCb;

                    const auto& expRetTy1 = maybeMonomorph(traitFcn.returnType);
                    if (!expRetTy1->matchTestGenerics(sp, implFcn.returnType, HIRResolvePlaceholdersNop(), matchCb)) {
                        failures.push_back(
                            FMT("Mismatched return type:\n"
                                << "  Expected " << expRetTy1 << "\n"
                                << "  Found    " << implFcn.returnType)
                        );
                    }
                    HIRTypeRef expRetTyReal;
                    const auto& expRetTy = matchCb.mapping.empty() && matchCb.rpitMapping.empty() ? expRetTy1 : (expRetTyReal = cloneTyWith(crate.types, sp, expRetTy1, [&](const HIRTypeData* ref, HIRTypeRef& out) -> bool {
                        if (const auto* erased = ref->opt_ErasedType(); erased && erased->inner.is_Fcn()) {
                            const auto it = matchCb.rpitMapping.find(erased->inner.as_Fcn().index);
                            if (it != matchCb.rpitMapping.end()) {
                                out = it->second;
                                return true;
                            }
                        }
                        if (const auto* tyP = ref->opt_Path()) {
                            if (const auto* pathP = tyP->path.mData.opt_UfcsKnown()) {
                                auto it = matchCb.mapping.find(pathP->item);
                                if (it != matchCb.mapping.end()) {
                                    out = it->second;
                                    return true;
                                }
                            }
                        }
                        return false;
                    }));

                    //}

                    if (!failures.empty()) {
                        ERROR(
                            sp,
                            E0000,
                            "Method " << e.first << " doesn't match trait:\n"
                                      << FMT_CB(os, for (const auto& f : failures) os << "- " << f << "\n") << "Trait:\n"
                                      << FMT_CB(
                                             os,
                                             {
                                                 os << "    fn " << e.first << traitFcn.mParams.fmtArgs() << "(";
                                                 for (const auto& a : traitFcn.mArgs) {
                                                     os << a.first << ": " << maybeMonomorph(a.second) << ", ";
                                                 }
                                                 os << ")\n";
                                                 os << "    -> " << maybeMonomorph(traitFcn.returnType) << "\n";
                                                 os << "    " << traitFcn.mParams.fmtBounds();
                                             }
                                         )
                                      << "\n"
                                      << "Impl :\n"
                                      << FMT_CB(
                                             os,
                                             {
                                                 os << "    fn " << e.first << implFcn.mParams.fmtArgs() << "(";
                                                 for (const auto& a : implFcn.mArgs) {
                                                     os << a.first << ": " << a.second << ", ";
                                                 }
                                                 os << ")\n";
                                                 os << "    -> " << implFcn.returnType << "\n";
                                                 os << "    " << implFcn.mParams.fmtBounds();
                                             }
                                         )
                                      << "\n"
                                      << "in impl" << impl.mParams.fmtArgs() << " " << traitPath << impl.traitArgs << " for " << impl.mType
                        );
                    }
                    // HACK: Replace all types (which should be functionally identical) so lifetimes match
                    // - This is needed for monomorphisation to work properly?
                    // REF: rustc-1.29.0/src/vendor/serde/src/private/de.rs:1379
                    // Counter-ref: rustc-1.54.0
                    // Update AFTER the checks
                    // HACK: Clone the expected type, so the lifetimes match.
                    DEBUG("Updating < " << impl.mType << " as " << traitPath << impl.traitArgs << " >::" << e.first);
                    if (!matchCb.rpitMapping.empty()) {
                        implFcn.traitReturnType = expRetTy1;
                    }
                    implFcn.returnType = expRetTy;
                    for (size_t i = 0; i < std::min(implFcn.mArgs.size(), traitFcn.mArgs.size()); i++) {
                        DEBUG("ARG" << i << "> " << traitFcn.mArgs[i].second);
                        implFcn.mArgs[i].second = mResolve.monomorphExpand(sp, traitFcn.mArgs[i].second, ms);
                    }
                    DEBUG("Updated < " << impl.mType << " as " << traitPath << impl.traitArgs << " >::" << e.first);

                    DEBUG(FMT_CB(os, {
                        os << "fn " << e.first << implFcn.mParams.fmtArgs() << "(";
                        for (const auto& a : implFcn.mArgs) {
                            os << a.first << ": " << a.second << ", ";
                        }
                        os << ")";
                        os << implFcn.mParams.fmtBounds();
                    }));
                }
                for (const auto& e : impl.constants) {
                    const auto& vi = trait.values.at(e.first);
                    if (!vi.is_Constant()) {
                        ERROR(sp, E0000, "Trait " << traitPath << " doesn't have a constant named " << e.first);
                    }
                    const auto& implConst = e.second.data;
                    const auto& traitConst = vi.as_Constant();

                    // Check type
                }
                for (const auto& e : impl.statics) {
                    const auto& vi = trait.values.at(e.first);
                    if (!vi.is_Static()) {
                        ERROR(sp, E0000, "Trait " << traitPath << " doesn't have a static named " << e.first);
                    }
                    const auto& implStatic = e.second.data;
                    const auto& traitStatic = vi.as_Static();

                    // Check type
                }
                for (const auto& e : trait.types) {
                    const auto& traitType = trait.types.at(e.first);
                    const auto& implType = e.second;

                    // Check that the bounds fit
                }
            }
        }

        void visitMarkerImpl(const HIRSimplePath& traitPath, HIRMarkerImpl& impl) override {
            TRACE_FUNCTION_F("impl " << traitPath << " for " << impl.mType << " { }");
            auto _ = mResolve.setImplGenerics(impl.mType, impl.mParams);
            selfTypes.push_back(impl.mType);

            // Pre-visit so lifetime elision can work
            {
                curParams = &impl.mParams;
                curParamsLevel = 0;
                this->visitType(impl.mType);
                this->visitPathParams(impl.traitArgs);
                curParams = nullptr;
            }

            HIRVisitor::visitMarkerImpl(traitPath, impl);
            // TODO: Check that the type+trait is valid

            selfTypes.pop_back();
        }

        void visitFunction(HIRItemPath p, HIRFunction& item) override {
            TRACE_FUNCTION_F(p);

            if (mResolve.hirCrate().getLangItemPathOpt("sized").components().empty()) {
                ERROR(Span(), E0000, "requires `sized` lang_item");
            }

            auto _ = mResolve.setItemGenerics(item.mParams);
            // NOTE: Superfluous... except that it makes the params valid for the return type.
            visitParams(item.mParams);

            fcnPtr = &item;

            // Visit arguments
            // - Used to convert `impl Trait` in argument position into generics
            // - Done first so the path in return-position `impl Trait` is valid
            curParams = &item.mParams;
            curParamsLevel = 1;
            for (auto& arg : item.mArgs) {
                TRACE_FUNCTION_F("ARG " << arg);
                visitType(arg.second);
            }
            curParams = nullptr;

            // Visit return type (populates path for `impl Trait` in return position
            fcnPath = &p;
            fcnErasedCount = 0;
            {
                TRACE_FUNCTION_F("RET " << item.returnType);
                visitType(item.returnType);
            }
            fcnPath = nullptr;
            fcnPtr = nullptr;

            if (item.receiver == HIRFunction::Receiver::Custom) {
                ASSERT_BUG(Span(), item.receiverType, "Custom receiver without a receiver type");
                this->visitType(*item.receiverType);
            }
            HIRVisitor::visitFunction(p, item);
        }
    };
}

void TypecheckModuleLevel(const WireBoard& wb, HIRCrate& crate) {
    Visitor v{wb, crate};
    v.visitCrate(crate);
}
