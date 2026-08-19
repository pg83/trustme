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
                return pitem.as_Enum().params;
            }
        }

        switch (pc) {
            case HIRVisitor::PathContext::VALUE: {
                const auto& item = crate.getValitemByPath(sp, path);

                switch (item.tag()) {
                    case HIRValueItem::TAG_Import: {
                        auto& e = item.as_Import();
                        BUG(sp, "Value path pointed to import - " << path << " = " << e.path);
                        break;
                    }
                    case HIRValueItem::TAG_Function: {
                        auto& e = item.as_Function();
                        return e.params;
                    }
                    case HIRValueItem::TAG_Constant: {
                        auto& e = item.as_Constant();
                        return e.params;
                    }
                    case HIRValueItem::TAG_Static: {
                        // TODO: Return an empty set?
                        BUG(sp, "Attepted to get parameters for static " << path);
                        break;
                    }
                    case HIRValueItem::TAG_StructConstructor: {
                        auto& e = item.as_StructConstructor();
                        return getParamsForItem(sp, crate, e.ty, HIRVisitor::PathContext::TYPE);
                    }
                    case HIRValueItem::TAG_StructConstant: {
                        auto& e = item.as_StructConstant();
                        return getParamsForItem(sp, crate, e.ty, HIRVisitor::PathContext::TYPE);
                    }
                }
            } break;
            case HIRVisitor::PathContext::TRAIT:
                // TODO: treat PathContext::TRAIT differently
            case HIRVisitor::PathContext::TYPE: {
                const auto& item = crate.getTypeitemByPath(sp, path);

                switch (item.tag()) {
                    case HIRTypeItem::TAG_Import: {
                        BUG(sp, "Type path pointed to import - " << path);
                        break;
                    }
                    case HIRTypeItem::TAG_TypeAlias: {
                        BUG(sp, "Type path pointed to type alias - " << path);
                        break;
                    }
                    case HIRTypeItem::TAG_TraitAlias: {
                        BUG(sp, "Type path pointed to trait alias - " << path);
                        break;
                    }
                    case HIRTypeItem::TAG_ExternType: {
                        static HIRGenericParams emptyParams; return emptyParams;
                        break;
                    }
                    case HIRTypeItem::TAG_Module: {
                        BUG(sp, "Type path pointed to module - " << path);
                        break;
                    }
                    case HIRTypeItem::TAG_Struct: {
                        auto& e = item.as_Struct();
                        return e.params;
                    }
                    case HIRTypeItem::TAG_Enum: {
                        auto& e = item.as_Enum();
                        return e.params;
                    }
                    case HIRTypeItem::TAG_Union: {
                        auto& e = item.as_Union();
                        return e.params;
                    }
                    case HIRTypeItem::TAG_Trait: {
                        auto& e = item.as_Trait();
                        return e.params;
                    }
                }
            } break;
        }
        throw "";
    }

    class Visitor: public HIRVisitor {
        HIRCrate& crate;
        StaticTraitResolve resolve_;

        const HIRTrait* currentTrait = nullptr;
        const HIRItemPath* currentTraitPath_ = nullptr;

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
            , resolve_(wb)
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
                switch (bound.tag()) {
                    case HIRGenericBound::TAG_TraitBound: {
                        auto& e = bound.as_TraitBound();
                        // TODO: Check for an implementation of this trait
                        DEBUG("TODO: Check bound " << e.type << " : " << e.trait.path);
                        break;
                    }
                    case HIRGenericBound::TAG_TypeEquality: {
                        auto& e = bound.as_TypeEquality();
                        // TODO: Check that two types are equal in this case
                        DEBUG("TODO: Check equality bound " << e.type << " == " << e.otherType);
                        break;
                    }
                }
            }
        }

    public:
        void visitPathParams(HIRPathParams& pp) override {
            static Span _sp;
            const Span& sp = _sp;

            HIRVisitor::visitPathParams(pp);
        }

        [[nodiscard]] HIRTypeRef visitType(HIRTypeRef ty) override {
            static Span _sp;
            const Span& sp = _sp;

            assert(ty);
            auto data = ty->cloneData();

            auto self = crate.types.self();
            if (data.is_ErasedType()) {
                selfTypes.push_back(self);
            }

            auto savedParams = std::make_pair(curParams, curParamsLevel);

            switch (data.tag()) {
                case HIRTypeData::TAG_Infer: {
                    break;
                }
                case HIRTypeData::TAG_Diverge: {
                    break;
                }
                case HIRTypeData::TAG_Primitive: {
                    break;
                }
                case HIRTypeData::TAG_Generic: {
                    break;
                }
                case HIRTypeData::TAG_Path: {
                    auto& e = data.as_Path();
                    this->visitPath(e.path, HIRVisitor::PathContext::TYPE);
                    break;
                }
                case HIRTypeData::TAG_TraitObject: {
                    auto& e = data.as_TraitObject();
                    if (e.trait.path != HIRSimplePath()) {
                        this->visitTraitPath(e.trait);
                    }
                    for (auto& marker : e.markers) {
                        this->visitGenericPath(marker, HIRVisitor::PathContext::TYPE);
                    }
                    break;
                }
                case HIRTypeData::TAG_ErasedType: {
                    auto& e = data.as_ErasedType();
                    {
                        auto& tuMatch = e.inner;
                        switch (tuMatch.tag()) {
                            case TypeDataErasedTypeInner::TAG_Known: {
                                auto& inner = tuMatch.as_Known();
                                inner = this->visitType(inner);
                                break;
                            }
                            case TypeDataErasedTypeInner::TAG_Alias: {
                                auto& inner = tuMatch.as_Alias();
                                this->visitPathParams(inner.params);
                                break;
                            }
                            case TypeDataErasedTypeInner::TAG_Fcn: {
                                auto& inner = tuMatch.as_Fcn();
                                if (inner.origin != HIRSimplePath()) this->visitPath(inner.origin, HIRVisitor::PathContext::VALUE);
                                break;
                            }
                        }
                    }
                    this->visitPathParams(e.use);
                    for (auto& trait : e.traits) this->visitTraitPath(trait);
                    break;
                }
                case HIRTypeData::TAG_Array: {
                    auto& e = data.as_Array();
                    e.inner = this->visitType(e.inner);
                    if (auto* size = e.size.opt_Unevaluated()) {
                        this->visitConstgeneric(*size);
                    }
                    break;
                }
                case HIRTypeData::TAG_Slice: {
                    auto& e = data.as_Slice();
                    e.inner = this->visitType(e.inner);
                    break;
                }
                case HIRTypeData::TAG_Tuple: {
                    auto& e = data.as_Tuple();
                    for (auto& inner : e) inner = this->visitType(inner);
                    break;
                }
                case HIRTypeData::TAG_Borrow: {
                    auto& e = data.as_Borrow();
                    e.inner = this->visitType(e.inner);
                    break;
                }
                case HIRTypeData::TAG_Pointer: {
                    auto& e = data.as_Pointer();
                    e.inner = this->visitType(e.inner);
                    break;
                }
                case HIRTypeData::TAG_NamedFunction: {
                    auto& e = data.as_NamedFunction();
                    this->visitPath(e.path, HIRVisitor::PathContext::VALUE);
                    break;
                }
                case HIRTypeData::TAG_Function: {
                    auto& e = data.as_Function();
                    for (auto& arg : e.argTypes) {
                        arg = this->visitType(arg);
                    }
                    e.rettype = this->visitType(e.rettype);
                    break;
                }
                case HIRTypeData::TAG_NodeType: {
                    break;
                }
            }

            curParams = savedParams.first;
            curParamsLevel = savedParams.second;

            if (data.is_ErasedType()) {
                selfTypes.pop_back();
            }

            return crate.types.intern(mv$(data));

            if (const auto* e = ty->opt_Path()) {
                switch (e->path.data.tag()) {
                    case HIRPath::Data::TAG_Generic: {
                        break;
                    }
                    case HIRPath::Data::TAG_UfcsUnknown: {
                        TODO(sp, "Should UfcsKnown be encountered here?");
                        break;
                    }
                    case HIRPath::Data::TAG_UfcsInherent: {
                        TRACE_FUNCTION_FR("UfcsInherent - " << ty, ty); resolve_.expandAssociatedTypes(sp, ty);
                        break;
                    }
                    case HIRPath::Data::TAG_UfcsKnown: {
                        TRACE_FUNCTION_FR("UfcsKnown - " << ty, ty); resolve_.expandAssociatedTypes(sp, ty);
                        break;
                    }
                }
            }
        }

        void visitGenericPath(HIRGenericPath& p, PathContext pc) override {
            static Span sp;
            TRACE_FUNCTION_F("p = " << p);
            const auto& params = getParamsForItem(sp, crate, p.path, pc);
            auto& args = p.params;

            checkParameters(sp, params, args);
            DEBUG("p = " << p);

            HIRVisitor::visitGenericPath(p, pc);
        }

    private:
        bool locateTraitItemInBounds(const Span& sp, HIRVisitor::PathContext pc, const HIRTypeData* tr, const HIRGenericParams& params, HIRPath::Data& pd) {
            for (const auto& b : params.bounds) {
                if (b.is_TraitBound()) {
                    auto& e = b.as_TraitBound();
                    DEBUG("- " << e.type << " : " << e.trait.path); if (e.type == tr) {
                        DEBUG(" - Match");
                        if (locateInTraitAndSet(sp, pc, e.trait.path, this->crate.getTraitByPath(sp, e.trait.path.path), pd)) {
                            return true;
                        }
                    }
                }
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
                pd = getUfcsKnown(mv$(pd.as_UfcsUnknown()), makeGenericPath(traitPath.path, trait), trait);
                return true;
            }
            // Search all supertraits
            for (const auto& pt : trait.allParentTraits) {
                if (locateItemInTrait(pc, *pt.traitPtr, pd)) {
                    pd = getUfcsKnown(mv$(pd.as_UfcsUnknown()), makeGenericPath(traitPath.path, trait), trait);
                    return true;
                }
            }
            return false;
        }

        bool setFromImpl(const HIRGenericPath& traitPath, const HIRTrait& trait, HIRPath::Data& pd) {
            auto& e = pd.as_UfcsUnknown();
            const auto& type = e.type;
            return this->crate.findTraitImpls(traitPath.path, type, HIRResolvePlaceholdersNop(), [&](const auto& impl) {
                DEBUG("FOUND impl" << impl.params.fmtArgs() << " " << traitPath.path << impl.traitArgs << " for " << impl.type);
                // TODO: Check bounds
                for (const auto& bound : impl.params.bounds) {
                    DEBUG("- TODO: Bound " << bound);
                    return false;
                }
                pd = getUfcsKnown(mv$(e), makeGenericPath(traitPath.path, trait), trait);
                return true;
            });
        }

        bool locateInTraitImplAndSet(HIRVisitor::PathContext pc, const HIRGenericPath& traitPath, const HIRTrait& trait, HIRPath::Data& pd) {
            auto& e = pd.as_UfcsUnknown();
            if (this->locateItemInTrait(pc, trait, pd)) {
                return this->setFromImpl(traitPath, trait, pd);
            } else {
                DEBUG("- Item " << e.item << " not in trait " << traitPath.path);
            }

            // Search supertraits (recursively)
            for (const auto& pt : trait.allParentTraits) {
                if (this->locateItemInTrait(pc, *pt.traitPtr, pd)) {
                    // TODO: Monomorphise params?
                    return setFromImpl(pt.path, *pt.traitPtr, pd);
                } else {
                }
            }
            return false;
        }

        HIRGenericPath makeGenericPath(HIRSimplePath sp, const HIRTrait& trait) {
            auto traitPathG = HIRGenericPath(mv$(sp));
            for (unsigned int i = 0; i < trait.params.types.size(); i++) {
                traitPathG.params.types.push_back(crate.types.generic(trait.params.types[i].name, i));
            }
            return traitPathG;
        }

        HIRGenericPath getCurrentTraitGp() const {
            assert(currentTraitPath_);
            assert(currentTrait);
            auto traitPath = HIRGenericPath(currentTraitPath_->getSimplePath());
            for (unsigned int i = 0; i < currentTrait->params.types.size(); i++) {
                traitPath.params.types.push_back(crate.types.generic(currentTrait->params.types[i].name, i));
            }
            return traitPath;
        }

        void visitPathUfcsUnknown(const Span& sp, HIRPath& p, HIRVisitor::PathContext pc) {
            TRACE_FUNCTION_FR("UfcsUnknown - p=" << p, p);
            auto& e = p.data.as_UfcsUnknown();

            e.type = this->visitType(e.type);
            this->visitPathParams(e.params);

            // Search for matching impls in current generic blocks
            if (resolve_.itemGenericsPtr() != nullptr && locateTraitItemInBounds(sp, pc, e.type, *resolve_.itemGenericsPtr(), p.data)) {
                return;
            }
            if (resolve_.implGenericsPtr() != nullptr && locateTraitItemInBounds(sp, pc, e.type, *resolve_.implGenericsPtr(), p.data)) {
                return;
            }

            if (const auto* te = e.type->opt_Generic()) {
                // If processing a trait, and the type is 'Self', search for the type/method on the trait
                // - TODO: This could be encoded by a `Self: Trait` bound in the generics, but that may have knock-on issues?
                if (te->name == "Self" && currentTrait) {
                    auto traitPath = this->getCurrentTraitGp();
                    if (this->locateInTraitAndSet(sp, pc, traitPath, *currentTrait, p.data)) {
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
                    p.data = mv$(newData);
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
                    for (unsigned int i = 0; i < trait.params.types.size(); i++) {
                        traitPath.params.types.push_back(crate.types.infer());
                    }

                    // TODO: Search supertraits
                    // TODO: Should impls be searched first, or item names?
                    // - Item names add complexity, but impls are slower
                    if (this->locateInTraitImplAndSet(pc, mv$(traitPath), trait, p.data)) {
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
            switch (p.data.tag()) {
                case HIRPath::Data::TAG_Generic: {
                    auto& e = p.data.as_Generic();
                    this->visitGenericPath(e, pc);
                    break;
                }
                case HIRPath::Data::TAG_UfcsKnown: {
                    auto& e = p.data.as_UfcsKnown();
                    e.type = this->visitType(e.type); selfTypes.push_back(e.type); this->visitGenericPath(e.trait, HIRVisitor::PathContext::TRAIT); selfTypes.pop_back();
                    // TODO: Locate impl block and check parameters
                    break;
                }
                case HIRPath::Data::TAG_UfcsInherent: {
                    auto& e = p.data.as_UfcsInherent();
                    e.type = this->visitType(e.type);
                    // TODO: Locate impl block and check parameters
                    break;
                }
                case HIRPath::Data::TAG_UfcsUnknown: {
                    BUG(Span(), "Encountered unknown-trait UFCS path during outer typeck - " << p);
                    break;
                }
            }
        }

        void visitParams(HIRGenericParams& params) override {
            TRACE_FUNCTION_F(params.fmtArgs());
            for (auto& tps : params.types) {
                tps.defaultValue = this->visitType(tps.defaultValue);
            }

            for (auto& bound : params.bounds) {
                switch (bound.tag()) {
                    case HIRGenericBound::TAG_TraitBound: {
                        auto& e = bound.as_TraitBound();
                        e.type = this->visitType(e.type);
                        selfTypes.push_back(e.type);
                        this->visitTraitPath(e.trait);
                        selfTypes.pop_back();
                        break;
                    }
                    case HIRGenericBound::TAG_TypeEquality: {
                        auto& e = bound.as_TypeEquality();
                        e.type = this->visitType(e.type);
                        e.otherType = this->visitType(e.otherType);
                        break;
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
            currentTraitPath_ = &p;

            auto _ = resolve_.setImplGenerics(MetadataType::TraitObject, item.params);
            auto self = crate.types.self();
            selfTypes.push_back(self);
            HIRVisitor::visitTrait(p, item);
            selfTypes.pop_back();

            currentTrait = nullptr;
        }

        void visitTraitAlias(HIRItemPath p, HIRTraitAlias& item) override {
            auto _ = resolve_.setImplGenerics(MetadataType::TraitObject, item.params);
            auto self = crate.types.self();
            selfTypes.push_back(self);
            HIRVisitor::visitTraitAlias(p, item);
            selfTypes.pop_back();
        }

        void visitStruct(HIRItemPath p, HIRStruct& item) override {
            auto _ = resolve_.setImplGenerics(item.structMarkings.dstType, item.params);
            HIRVisitor::visitStruct(p, item);
        }

        void visitUnion(HIRItemPath p, HIRUnion& item) override {
            auto _ = resolve_.setImplGenerics(MetadataType::None, item.params);
            HIRVisitor::visitUnion(p, item);
        }

        void visitEnum(HIRItemPath p, HIREnum& item) override {
            auto _ = resolve_.setImplGenerics(MetadataType::None, item.params);
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
            auto _ = resolve_.setItemGenerics(item.params);
            auto savedParams = std::make_pair(curParams, curParamsLevel);
            curParams = &item.params;
            curParamsLevel = 1;
            HIRVisitor::visitInherentType(p, item);
            curParams = savedParams.first;
            curParamsLevel = savedParams.second;
        }

        void visitTypeImpl(HIRTypeImpl& impl) override {
            TRACE_FUNCTION_F("impl " << impl.type);
            auto _ = resolve_.setImplGenerics(impl.type, impl.params);
            selfTypes.push_back(impl.type);

            // Pre-visit so lifetime elision can work
            {
                curParams = &impl.params;
                curParamsLevel = 0;
                impl.type = this->visitType(impl.type);
                curParams = nullptr;
            }

            HIRVisitor::visitTypeImpl(impl);
            // TODO: Check that the type is valid

            selfTypes.pop_back();
        }

        void visitTraitImpl(const HIRSimplePath& traitPath, HIRTraitImpl& impl) override {
            static Span sp;
            TRACE_FUNCTION_F("impl" << impl.params.fmtArgs() << " " << traitPath << impl.traitArgs << " for " << impl.type);
            auto _ = resolve_.setImplGenerics(impl.type, impl.params);
            selfTypes.push_back(impl.type);

            // Pre-visit so lifetime elision can work
            {
                curParams = &impl.params;
                curParamsLevel = 0;
                impl.type = this->visitType(impl.type);
                this->visitPathParams(impl.traitArgs);
                curParams = nullptr;
            }

            HIRVisitor::visitTraitImpl(traitPath, impl);
            selfTypes.pop_back();

            // TODO: Check that the type+trait is valid
            // - And fix bad elided liftimes (match annotations if they were elided)
            {
                const auto& trait = resolve_.hirCrate().getTraitByPath(sp, traitPath);
                for (auto& e : impl.methods) {
                    auto _ = resolve_.setItemGenerics(e.second.data.params);

                    const auto vIt = trait.values.find(e.first);
                    if (vIt == trait.values.end() || !vIt->second.is_Function()) {
                        ERROR(sp, E0000, "Trait " << traitPath << " doesn't have a method named " << e.first);
                    }
                    auto& implFcn = e.second.data;
                    const auto& traitFcn = vIt->second.as_Function();

                    auto fcnParams = traitFcn.params.makeNopParams(crate.types, 1);
                    MonomorphStatePtr ms{crate.types, impl.type, &impl.traitArgs, &fcnParams};
                    ms.setConstevalState(resolve_.board(), HIRItemPath(traitPath));
                    HIRTypeRef tmp;
                    auto maybeMonomorph = [&](const HIRTypeData* ty) -> const HIRTypeData* {
                        if (monomorphiseTypeNeeded(ty)) {
                            tmp = ms.monomorphType(sp, ty);
                            resolve_.expandAssociatedTypes(sp, tmp);
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
                    if (implFcn.params.types.size() != traitFcn.params.types.size()) {
                        failures.push_back(FMT("Mismatched type param count (expected " << traitFcn.params.types.size() << ", got " << implFcn.params.types.size() << ")"));
                    }
                    // Different logic for lifetimes, only want to check un-elided lifetimes
                    // - Well, elided lifetimes can overlap non-elided ones (as long as they're identical)
                    if (implFcn.params.values.size() != traitFcn.params.values.size()) {
                        failures.push_back(FMT("Mismatched const param count (expected " << traitFcn.params.values.size() << ", got " << implFcn.params.values.size() << ")"));
                    }
                    // -- Arguments
                    if (implFcn.args.size() != traitFcn.args.size()) {
                        failures.push_back(FMT("Mismatched argument count (expected " << traitFcn.args.size() << ", got " << implFcn.args.size() << ")"));
                    }
                    if (implFcn.receiver != traitFcn.receiver) {
                        failures.push_back(FMT("Receiver type")); //"(expected " << trait_fcn.m_receiver << ", got " << impl_fcn.m_receiver));
                    }
                    for (size_t i = 0; i < std::min(implFcn.args.size(), traitFcn.args.size()); i++) {
                        if (!(i == 0 && (traitFcn.receiver == HIRFunction::Receiver::Free || implFcn.receiver == HIRFunction::Receiver::Free))) {
                            // Check the type.
                            // - Also, fix lifetime elision?
                            const auto& expTy = maybeMonomorph(traitFcn.args[i].second);
                            /*const*/ auto& hasTy = implFcn.args[i].second;

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
                                if (const auto* pathP = tyP->path.data.opt_UfcsKnown()) {
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
                            if (const auto* pathP = tyP->path.data.opt_UfcsKnown()) {
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
                                                 os << "    fn " << e.first << traitFcn.params.fmtArgs() << "(";
                                                 for (const auto& a : traitFcn.args) {
                                                     os << a.first << ": " << maybeMonomorph(a.second) << ", ";
                                                 }
                                                 os << ")\n";
                                                 os << "    -> " << maybeMonomorph(traitFcn.returnType) << "\n";
                                                 os << "    " << traitFcn.params.fmtBounds();
                                             }
                                         )
                                      << "\n"
                                      << "Impl :\n"
                                      << FMT_CB(
                                             os,
                                             {
                                                 os << "    fn " << e.first << implFcn.params.fmtArgs() << "(";
                                                 for (const auto& a : implFcn.args) {
                                                     os << a.first << ": " << a.second << ", ";
                                                 }
                                                 os << ")\n";
                                                 os << "    -> " << implFcn.returnType << "\n";
                                                 os << "    " << implFcn.params.fmtBounds();
                                             }
                                         )
                                      << "\n"
                                      << "in impl" << impl.params.fmtArgs() << " " << traitPath << impl.traitArgs << " for " << impl.type
                        );
                    }
                    // HACK: Replace all types (which should be functionally identical) so lifetimes match
                    // - This is needed for monomorphisation to work properly?
                    // REF: rustc-1.29.0/src/vendor/serde/src/private/de.rs:1379
                    // Counter-ref: rustc-1.54.0
                    // Update AFTER the checks
                    // HACK: Clone the expected type, so the lifetimes match.
                    DEBUG("Updating < " << impl.type << " as " << traitPath << impl.traitArgs << " >::" << e.first);
                    if (!matchCb.rpitMapping.empty()) {
                        implFcn.traitReturnType = expRetTy1;
                        for (const auto& mapping : matchCb.rpitMapping) {
                            const auto name = RcString::newInterned(FMT(ATY_PREFIX_ERASED << e.first << "_" << mapping.first));
                            impl.types.insert(std::make_pair(name, HIRTraitImpl::ImplEnt<HIRTypeRef>{e.second.isSpecialisable, mapping.second}));
                        }
                    }
                    implFcn.returnType = expRetTy;
                    for (size_t i = 0; i < std::min(implFcn.args.size(), traitFcn.args.size()); i++) {
                        DEBUG("ARG" << i << "> " << traitFcn.args[i].second);
                        implFcn.args[i].second = resolve_.monomorphExpand(sp, traitFcn.args[i].second, ms);
                    }
                    DEBUG("Updated < " << impl.type << " as " << traitPath << impl.traitArgs << " >::" << e.first);

                    DEBUG(FMT_CB(os, {
                        os << "fn " << e.first << implFcn.params.fmtArgs() << "(";
                        for (const auto& a : implFcn.args) {
                            os << a.first << ": " << a.second << ", ";
                        }
                        os << ")";
                        os << implFcn.params.fmtBounds();
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
            TRACE_FUNCTION_F("impl " << traitPath << " for " << impl.type << " { }");
            auto _ = resolve_.setImplGenerics(impl.type, impl.params);
            selfTypes.push_back(impl.type);

            // Pre-visit so lifetime elision can work
            {
                curParams = &impl.params;
                curParamsLevel = 0;
                impl.type = this->visitType(impl.type);
                this->visitPathParams(impl.traitArgs);
                curParams = nullptr;
            }

            HIRVisitor::visitMarkerImpl(traitPath, impl);
            // TODO: Check that the type+trait is valid

            selfTypes.pop_back();
        }

        void visitFunction(HIRItemPath p, HIRFunction& item) override {
            TRACE_FUNCTION_F(p);

            if (resolve_.hirCrate().getLangItemPathOpt("sized").components().empty()) {
                ERROR(Span(), E0000, "requires `sized` lang_item");
            }

            auto _ = resolve_.setItemGenerics(item.params);
            // NOTE: Superfluous... except that it makes the params valid for the return type.
            visitParams(item.params);

            fcnPtr = &item;

            // Visit arguments
            // - Used to convert `impl Trait` in argument position into generics
            // - Done first so the path in return-position `impl Trait` is valid
            curParams = &item.params;
            curParamsLevel = 1;
            for (auto& arg : item.args) {
                TRACE_FUNCTION_F("ARG " << arg);
                arg.second = visitType(arg.second);
            }
            curParams = nullptr;

            // Visit return type (populates path for `impl Trait` in return position
            fcnPath = &p;
            fcnErasedCount = 0;
            {
                TRACE_FUNCTION_F("RET " << item.returnType);
                item.returnType = visitType(item.returnType);
            }
            fcnPath = nullptr;
            fcnPtr = nullptr;

            if (item.receiver == HIRFunction::Receiver::Custom) {
                ASSERT_BUG(Span(), item.receiverType, "Custom receiver without a receiver type");
                *item.receiverType = this->visitType(*item.receiverType);
            }
            HIRVisitor::visitFunction(p, item);
        }
    };
}

void TypecheckModuleLevel(const WireBoard& wb, HIRCrate& crate) {
    Visitor v{wb, crate};
    v.visitCrate(crate);
}
