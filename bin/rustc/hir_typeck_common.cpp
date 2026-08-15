#include "hir_typeck_common.h"

#include "hir_path.h"
#include "wire_board.h"
#include "trans_target.h"
#include "hir_conv_main_bindings.h"

template <typename I>
struct WConst {
    typedef const I T;
};

template <typename I>
struct WMut {
    typedef I T;
};

template <template <typename> class W>
struct TyVisitor {
    const LList<const HIRTypeData*>* curRecurseStack = nullptr;

    virtual typename W<HIRTypeData>::T& getTyData(const HIRTypeData* ty) const = 0;

    virtual bool visitPathParams(typename W<HIRPathParams>::T& tpl) {
        for (auto& ty : tpl.types) {
            if (visitType(ty)) {
                return true;
            }
        }
        return false;
    }

    virtual bool visitTraitPath(typename W<HIRTraitPath>::T& tpl) {
        if (visitPathParams(tpl.mPath.mParams)) {
            return true;
        }
        for (auto& assoc : tpl.typeBounds) {
            visitPathParams(assoc.second.sourceTrait.mParams);
            if (visitType(assoc.second.type)) {
                return true;
            }
        }
        for (auto& assoc : tpl.traitBounds) {
            visitPathParams(assoc.second.sourceTrait.mParams);
            for (auto& t : assoc.second.traits) {
                visitTraitPath(t);
            }
        }
        return false;
    }

    virtual bool visitPath(typename W<HIRPath>::T& path) {
        TU_MATCH_HDRA((path.mData), {)
        TU_ARMA(Generic, e) {
                return visitPathParams(e.mParams);
            }
            TU_ARMA(UfcsInherent, e) {
                return visitType(e.type) || visitPathParams(e.params);
            }
            TU_ARMA(UfcsKnown, e) {
                return visitType(e.type) || visitPathParams(e.trait.mParams) || visitPathParams(e.params);
            }
            TU_ARMA(UfcsUnknown, e) {
                return visitType(e.type) || visitPathParams(e.params);
            }
        }
        throw "";
    }

    virtual bool visitType(const HIRTypeData* ty) {
        if (curRecurseStack) {
            for (const auto* p : *curRecurseStack) {
                if (p == ty) {
                    return false;
                }
            }
        }

        struct _ {
            typedef LList<const HIRTypeData*> stack_t;
            const stack_t*& dst;
            stack_t stack;

            _(const stack_t*& dst, const HIRTypeData* ty)
                : dst(dst)
                , stack(dst, ty)
            {
                dst = &stack;
            }

            ~_() {
                dst = stack.prev;
            }
        } h(curRecurseStack, ty);

        TU_MATCH_HDRA( (this->getTyData(ty)), {)
        TU_ARMA(Infer, e) {
            }
            TU_ARMA(Diverge, e) {
            }
            TU_ARMA(Primitive, e) {
            }
            TU_ARMA(Generic, e) {
            }
            TU_ARMA(Path, e) {
                return visitPath(e.path);
            }
            TU_ARMA(TraitObject, e) {
                if (visitTraitPath(e.mTrait)) {
                    return true;
                }
                for (auto& trait : e.markers) {
                    if (visitPathParams(trait.mParams)) {
                        return true;
                    }
                }
                return false;
            }
            TU_ARMA(ErasedType, e) {
                for (auto& trait : e.traits) {
                    if (visitTraitPath(trait)) {
                        return true;
                    }
                }
                visitPathParams(e.use);
            TU_MATCH_HDRA( (e.inner), {)
            TU_ARMA(Fcn, ee) {
                        if (visitPath(ee.origin)) {
                            return true;
                        }
                    }
                    TU_ARMA(Known, ee) {
                        if (visitType(ee)) {
                            return true;
                        }
                    }
                    TU_ARMA(Alias, ee) {
                        visitPathParams(ee.params);
                    }
            }
            return false;
            }
            TU_ARMA(Array, e) {
                return visitType(e.inner);
            }
            TU_ARMA(Slice, e) {
                return visitType(e.inner);
            }
            TU_ARMA(Pattern, e) {
                return visitType(e.inner);
            }
            TU_ARMA(Tuple, e) {
                for (auto& ty : e) {
                    if (visitType(ty)) {
                        return true;
                    }
                }
                return false;
            }
            TU_ARMA(Borrow, e) {
                return visitType(e.inner);
            }
            TU_ARMA(Pointer, e) {
                return visitType(e.inner);
            }
            TU_ARMA(NamedFunction, e) {
                return visitPath(e.path);
            }
            TU_ARMA(Function, e) {
                for (auto& ty : e.argTypes) {
                    if (visitType(ty)) {
                        return true;
                    }
                }
                return visitType(e.mRettype);
            }
            TU_ARMA(NodeType, e) {
                // These just have a node pointer, no visiting
            }
        }
        return false;
    }
};

struct TyVisitorCbConst: TyVisitor<WConst> {
    tCbVisitTy callback;

    const HIRTypeData& getTyData(const HIRTypeData* ty) const override {
        return *ty;
    }

    bool visitType(const HIRTypeData* ty) override {
        if (callback(ty)) {
            return true;
        }
        return TyVisitor::visitType(ty);
    }
};

bool visitTyWith(const HIRTypeData* ty, tCbVisitTy callback) {
    TyVisitorCbConst v;
    v.callback = callback;
    return v.visitType(ty);
}

bool visitTraitPathTysWith(const HIRTraitPath& path, tCbVisitTy callback) {
    TyVisitorCbConst v;
    v.callback = callback;
    return v.visitTraitPath(path);
}

bool visitPathTysWith(const HIRPath& path, tCbVisitTy callback) {
    TyVisitorCbConst v;
    v.callback = callback;
    return v.visitPath(path);
}

namespace {
    struct TyRewriter {
        HIRTypeInterner& types;
        tCbRewriteTy callback;
        ::std::vector<HIRTypeRef> stack;

        bool rewritePathParams(HIRPathParams& params) {
            for (auto& type : params.types) {
                if (rewriteType(type)) {
                    return true;
                }
            }
            return false;
        }

        bool rewriteTraitPath(HIRTraitPath& trait) {
            if (rewritePathParams(trait.mPath.mParams)) {
                return true;
            }
            for (auto& assoc : trait.typeBounds) {
                if (rewritePathParams(assoc.second.sourceTrait.mParams) || rewritePathParams(assoc.second.atyParams) || rewriteType(assoc.second.type)) {
                    return true;
                }
            }
            for (auto& assoc : trait.traitBounds) {
                if (rewritePathParams(assoc.second.sourceTrait.mParams) || rewritePathParams(assoc.second.atyParams)) {
                    return true;
                }
                for (auto& bound : assoc.second.traits) {
                    if (rewriteTraitPath(bound)) {
                        return true;
                    }
                }
            }
            return false;
        }

        bool rewritePath(HIRPath& path) {
            TU_MATCH_HDRA((path.mData), {)
            TU_ARMA(Generic, e) return rewritePathParams(e.mParams);
                TU_ARMA(UfcsInherent, e) return rewriteType(e.type) || rewritePathParams(e.params) || rewritePathParams(e.implParams);
                TU_ARMA(UfcsKnown, e) return rewriteType(e.type) || rewritePathParams(e.trait.mParams) || rewritePathParams(e.params);
                TU_ARMA(UfcsUnknown, e) return rewriteType(e.type) || rewritePathParams(e.params);
            }
            throw "";
        }

        bool rewriteType(HIRTypeRef& type) {
            if (!type || ::std::find(stack.begin(), stack.end(), type) != stack.end()) {
                return false;
            }
            const auto original = type;
            auto data = original->cloneData();
            HIRTypeRef rewritten = original;
            const bool stop = callback(rewritten, data);
            if (rewritten != original) {
                type = rewritten;
                return stop;
            }

            stack.push_back(original);
            bool childStop = false;
            if (!stop) {
                TU_MATCH_HDRA((data), {)
                TU_ARMA(Infer, e) {
                    }
                    TU_ARMA(Diverge, e) {
                    }
                    TU_ARMA(Primitive, e) {
                    }
                    TU_ARMA(Generic, e) {
                    }
                    TU_ARMA(Path, e) childStop = rewritePath(e.path);
                    TU_ARMA(TraitObject, e) {
                        childStop = rewriteTraitPath(e.mTrait);
                        for (auto& marker : e.markers) {
                            if (!childStop) {
                                childStop = rewritePathParams(marker.mParams);
                            }
                        }
                    }
                    TU_ARMA(ErasedType, e) {
                        for (auto& trait : e.traits) {
                            if (!childStop) {
                                childStop = rewriteTraitPath(trait);
                            }
                        }
                        if (!childStop) {
                            childStop = rewritePathParams(e.use);
                        }
                        if (!childStop) {
                        TU_MATCH_HDRA((e.inner), {)
                        TU_ARMA(Fcn, inner) childStop = rewritePath(inner.origin);
                                TU_ARMA(Known, inner) childStop = rewriteType(inner);
                                TU_ARMA(Alias, inner) childStop = rewritePathParams(inner.params);
                        }
                        }
                    }
                    TU_ARMA(Array, e) childStop = rewriteType(e.inner);
                    TU_ARMA(Slice, e) childStop = rewriteType(e.inner);
                    TU_ARMA(Pattern, e) childStop = rewriteType(e.inner);
                    TU_ARMA(Tuple, e) for (auto& inner : e) if (!childStop) childStop = rewriteType(inner);
                    TU_ARMA(Borrow, e) childStop = rewriteType(e.inner);
                    TU_ARMA(Pointer, e) childStop = rewriteType(e.inner);
                    TU_ARMA(NamedFunction, e) childStop = rewritePath(e.path);
                    TU_ARMA(Function, e) {
                        for (auto& arg : e.argTypes) {
                            if (!childStop) {
                                childStop = rewriteType(arg);
                            }
                        }
                        if (!childStop) {
                            childStop = rewriteType(e.mRettype);
                        }
                    }
                    TU_ARMA(NodeType, e) {
                    }
                }
            }
            stack.pop_back();
            type = types.intern(mv$(data));
            return stop || childStop;
        }
    };
}

bool rewriteTyWith(HIRTypeInterner& types, HIRTypeRef& ty, tCbRewriteTy callback) {
    TyRewriter rewriter{types, mv$(callback), {}};
    return rewriter.rewriteType(ty);
}

bool rewritePathTysWith(HIRTypeInterner& types, HIRPath& path, tCbRewriteTy callback) {
    TyRewriter rewriter{types, mv$(callback), {}};
    return rewriter.rewritePath(path);
}

struct TyVisitorMonomorphNeeded: TyVisitor<WConst> {
    const HIRTypeData& getTyData(const HIRTypeData* ty) const override {
        return *ty;
    }

    bool visitPathParams(const HIRPathParams& pp) override {
        for (const auto& v : pp.values) {
            if (v.is_Generic()) {
                return true;
            }
        }
        return TyVisitor::visitPathParams(pp);
    }

    bool visitType(const HIRTypeData* ty) override {
        if (ty->is_Generic()) {
            return true;
        }
        if (ty->is_Array() && ty->as_Array().size.is_Unevaluated() /*&& ty->as_Array().size.as_Unevaluated().*/) {
            return true;
        }
        return TyVisitor::visitType(ty);
    }
};

bool monomorphisePathparamsNeeded(const HIRPathParams& tpl) {
    TyVisitorMonomorphNeeded v{};
    return v.visitPathParams(tpl);
}

bool monomorphiseTraitpathNeeded(const HIRTraitPath& tpl) {
    TyVisitorMonomorphNeeded v{};
    return v.visitTraitPath(tpl);
}

bool monomorphisePathNeeded(const HIRPath& tpl) {
    TyVisitorMonomorphNeeded v{};
    return v.visitPath(tpl);
}

bool monomorphiseTypeNeeded(const HIRTypeData* tpl) {
    return tpl->needsMonomorphisation();
}

HIRTypeRef Monomorphiser::monomorphType(const Span& sp, const HIRTypeData* tpl, bool allowInfer /*=true*/) const {
    TU_MATCH_HDRA( (*tpl), {)
    TU_ARMA(Infer, e) {
            ASSERT_BUG(sp, allowInfer, "Unexpected ivar seen - " << tpl);
            return tpl;
        }
        TU_ARMA(Diverge, e) {
            return tpl;
        }
        TU_ARMA(Primitive, e) {
            return tpl;
        }
        TU_ARMA(Path, e) {
            auto binding = e.binding.is_Opaque() ? HIRTypePathBinding() : e.binding.clone();
            return types.intern(HIRTypeData::make_Path({this->monomorphPath(sp, e.path, allowInfer), mv$(binding)}));
        }
        TU_ARMA(Generic, e) {
            return this->getType(sp, e);
        }
        TU_ARMA(TraitObject, e) {
            HIRTypeData::Data_TraitObject to;
            {
                to.mTrait = this->monomorphTraitpath(sp, e.mTrait, allowInfer);
                for (const auto& trait : e.markers) {
                    to.markers.push_back(this->monomorphGenericpath(sp, trait, allowInfer));
                }
            }
            return types.intern(HIRTypeData::make_TraitObject(mv$(to)));
        }
        TU_ARMA(ErasedType, e) {
            ::std::vector<HIRTraitPath> traits;
            traits.reserve(e.traits.size());
            for (const auto& trait : e.traits) {
                traits.push_back(this->monomorphTraitpath(sp, trait, allowInfer));
            }

            TypeDataErasedTypeInner inner;
        TU_MATCH_HDRA( (e.inner), {)
        TU_ARMA(Fcn, ee) {
                    inner = TypeDataErasedTypeInner::Data_Fcn{this->monomorphPath(sp, ee.origin, allowInfer), ee.index};
                }
                TU_ARMA(Alias, ee) {
                    inner = TypeDataErasedTypeInner::Data_Alias{this->monomorphPathParams(sp, ee.params, allowInfer), ee.inner};
                }
                TU_ARMA(Known, ee) {
                    inner = this->monomorphType(sp, ee, allowInfer);
                }
        }

        return types.intern(HIRTypeData::make_ErasedType(HIRTypeData::Data_ErasedType {
            e.isSized,
            mv$(traits),
            mv$(inner),
            this->monomorphPathParams(sp, e.use, allowInfer),
            e.usePresent
            }));
        }
        TU_ARMA(Array, e) {
            return types.intern(HIRTypeData::make_Array({this->monomorphType(sp, e.inner, allowInfer), this->monomorphArraysize(sp, e.size)}));
        }
        TU_ARMA(Slice, e) {
            return types.slice(this->monomorphType(sp, e.inner, allowInfer));
        }
        TU_ARMA(Pattern, e) {
            HIRTypePattern pattern;
            pattern.alternatives.reserve(e.pattern.alternatives.size());
            for (const auto& range : e.pattern.alternatives) {
                HIRTypePatternRange out{
                    range.hasStart,
                    range.hasStart ? this->monomorphConstgeneric(sp, range.start, allowInfer) : HIRConstGeneric(),
                    range.hasEnd,
                    range.hasEnd ? this->monomorphConstgeneric(sp, range.end, allowInfer) : HIRConstGeneric(),
                    range.endInclusive,
                };
                pattern.alternatives.push_back(mv$(out));
            }
            return types.intern(HIRTypeData::make_Pattern({this->monomorphType(sp, e.inner, allowInfer), mv$(pattern)}));
        }
        TU_ARMA(Tuple, e) {
            ::std::vector<HIRTypeRef> types;
            for (const auto& ty : e) {
                types.push_back(this->monomorphType(sp, ty, allowInfer));
            }
            return this->types.tuple(mv$(types));
        }
        TU_ARMA(Borrow, e) {
            return types.borrow(e.type, this->monomorphType(sp, e.inner, allowInfer));
        }
        TU_ARMA(Pointer, e) {
            return types.pointer(e.type, this->monomorphType(sp, e.inner, allowInfer));
        }
        TU_ARMA(NamedFunction, e) {
            return types.intern(
                HIRTypeData::make_NamedFunction(
                    HIRTypeData::Data_NamedFunction{
                        this->monomorphPath(sp, e.path, allowInfer),
                        e.def.clone() // Should this become `nullptr`? Or should the definition be fixed
                    }
                )
            );
        }
        TU_ARMA(Function, e) {
            HIRTypeDataFunctionPointer ft;
            ft.isUnsafe = e.isUnsafe;
            ft.isVariadic = e.isVariadic;
            ft.trackCaller = e.trackCaller;
            ft.mAbi = e.mAbi;
            ft.mRettype = this->monomorphType(sp, e.mRettype, allowInfer);
            for (const auto& arg : e.argTypes) {
                ft.argTypes.push_back(this->monomorphType(sp, arg, allowInfer));
            }
            return types.function(mv$(ft));
        }
        // Closures and generators are just passed through, needed for hackery in type checking (erasing HRLs)
        TU_ARMA(NodeType, e) {
            return tpl;
        }
    }
    throw "";
}

HIRPath Monomorphiser::monomorphPath(const Span& sp, const HIRPath& tpl, bool allowInfer /*=true*/) const {
    TU_MATCH_HDRA( (tpl.mData), {)
    TU_ARMA(Generic, e2) {
            return HIRPath(this->monomorphGenericpath(sp, e2, allowInfer));
        }
        TU_ARMA(UfcsKnown, e2) {
            auto rv = HIRPath(HIRPath::Data::make_UfcsKnown({this->monomorphType(sp, e2.type, allowInfer), this->monomorphGenericpath(sp, e2.trait, allowInfer), e2.item, this->monomorphPathParams(sp, e2.params, allowInfer)}));
            return rv;
        }
        TU_ARMA(UfcsUnknown, e2) {
            return HIRPath::Data::make_UfcsUnknown({this->monomorphType(sp, e2.type, allowInfer), e2.item, this->monomorphPathParams(sp, e2.params, allowInfer)});
        }
        TU_ARMA(UfcsInherent, e2) {
            return HIRPath::Data::make_UfcsInherent({this->monomorphType(sp, e2.type, allowInfer), e2.item, this->monomorphPathParams(sp, e2.params, allowInfer), this->monomorphPathParams(sp, e2.implParams, allowInfer)});
        }
    }
    throw "";
}

HIRTraitPath Monomorphiser::monomorphTraitpath(const Span& sp, const HIRTraitPath& tpl, bool allowInfer) const {
    HIRTraitPath rv{this->monomorphGenericpath(sp, tpl.mPath, allowInfer), {}, {}, tpl.traitPtr, tpl.constness};

    for (const auto& assoc : tpl.typeBounds) {
        rv.typeBounds.insert(::std::make_pair(assoc.first, this->monomorphTpAtyEqual(sp, assoc.second, allowInfer)));
    }
    for (const auto& assoc : tpl.traitBounds) {
        auto v = HIRTraitPath::AtyBound{this->monomorphGenericpath(sp, assoc.second.sourceTrait, allowInfer), {}};
        for (const auto& trait : assoc.second.traits) {
            v.traits.push_back(monomorphTraitpath(sp, trait, allowInfer));
        }
        rv.traitBounds.insert(::std::make_pair(assoc.first, std::move(v)));
    }

    return rv;
}

HIRTraitPath::AtyEqual Monomorphiser::monomorphTpAtyEqual(const Span& sp, const HIRTraitPath::AtyEqual& tpl, bool allowInfer) const {
    return HIRTraitPath::AtyEqual{this->monomorphGenericpath(sp, tpl.sourceTrait, allowInfer), {}, this->monomorphType(sp, tpl.type, allowInfer)};
}

HIRConstGeneric Monomorphiser::monomorphConstgeneric(const Span& sp, const HIRConstGeneric& val, bool allowInfer) const {
    if (const auto* ge = val.opt_Generic()) {
        return this->getValue(sp, *ge);
    } else if (const auto* ge = val.opt_Unevaluated()) {
        auto rv = HIRConstGeneric(std::make_unique<HIRConstGenericUnevaluated>((*ge)->monomorph(sp, *this, true)));
        // TODO: Evaluate this constant (if possible), but that requires knowing the target type :/
        return rv;
    } else {
        return val.clone();
    }
}

HIRPathParams Monomorphiser::monomorphPathParams(const Span& sp, const HIRPathParams& tpl, bool allowInfer) const {
    HIRPathParams rv;

    rv.types.reserve(tpl.types.size());
    for (const auto& ty : tpl.types) {
        rv.types.push_back(this->monomorphType(sp, ty, allowInfer));
    }

    rv.values.reserve(tpl.values.size());
    for (const auto& val : tpl.values) {
        rv.values.push_back(monomorphConstgeneric(sp, val, allowInfer));
    }

    return rv;
}

HIRGenericPath Monomorphiser::monomorphGenericpath(const Span& sp, const HIRGenericPath& tpl, bool allowInfer) const {
    return HIRGenericPath(tpl.mPath, this->monomorphPathParams(sp, tpl.mParams, allowInfer));
}

HIRArraySize Monomorphiser::monomorphArraysize(const Span& sp, const HIRArraySize& tpl) const {
    if (auto* se = tpl.opt_Unevaluated()) {
        HIRArraySize sz;
        if (se->is_Generic()) {
            sz = this->getValue(sp, se->as_Generic());
            DEBUG(tpl << " -> " << sz);
        } else if (se->is_Unevaluated()) {
            sz = HIRConstGeneric(std::make_unique<HIRConstGenericUnevaluated>(se->as_Unevaluated()->monomorph(sp, *this, true)));
        } else {
            sz = se->clone();
        }
        se = sz.opt_Unevaluated();
        assert(se);

        // Evaluate, if possible
        if (se->is_Unevaluated()) {
            if (this->constevalWb) {
                ConvertHIRConstantEvaluateConstGeneric(sp, *this->constevalWb, *this->constevalWb->crate, types.primitive(HIRCoreType::Usize), sz.as_Unevaluated());
            } else {
                DEBUG("TODO: Evaluate unevaluated generic for array size - " << *se);
            }
        }

        if (const auto* e = se->opt_Evaluated()) {
            return (*e)->readUsize(0);
        }
        return sz;
    } else {
        return tpl.clone();
    }
}

struct CloneTyWithMonomorph: Monomorphiser {
    tCbCloneTy callback;

    explicit CloneTyWithMonomorph(HIRTypeInterner& types)
        : Monomorphiser(types)
    {
    }

    HIRTypeRef getType(const Span& sp, const HIRGenericRef& g) const override {
        return types.generic(g.name, g.binding);
    }

    HIRConstGeneric getValue(const Span& sp, const HIRGenericRef& g) const override {
        return g;
    }

    HIRTypeRef monomorphType(const Span& sp, const HIRTypeData* ty, bool allowInfer = true) const {
        HIRTypeRef rv;

        if (callback(ty, rv)) {
            return rv;
        }
        return Monomorphiser::monomorphType(sp, ty, allowInfer);
    }
};

HIRPathParams clonePathParamsWith(HIRTypeInterner& types, const Span& sp, const HIRPathParams& tpl, tCbCloneTy callback) {
    HIRPathParams rv;
    rv.types.reserve(tpl.types.size());
    for (const auto& ty : tpl.types) {
        rv.types.push_back(cloneTyWith(types, sp, ty, callback));
    }
    for (const auto& v : tpl.values) {
        rv.values.push_back(v.clone());
    }
    return rv;
}

HIRTypeRef cloneTyWith(HIRTypeInterner& types, const Span& sp, const HIRTypeData* tpl, tCbCloneTy callback) {
    CloneTyWithMonomorph m(types);
    m.callback = std::move(callback);
    return m.monomorphType(sp, tpl, true);
}

HIRTypeRef MonomorphiserPP::getType(const Span& sp, const HIRGenericRef& ty) const /*override*/
{
    if (ty.isSelf()) {
        if (const auto* s = this->getSelfType()) {
            return s;
        } else {
            BUG(sp, "Unexpected Self");
        }
    } else {
        switch (ty.group()) {
            case 0:
                if (const auto* p = this->getImplParams()) {
                    ASSERT_BUG(sp, ty.idx() < p->types.size(), "Type param " << ty << " out of range for (max " << p->types.size() << ")");
                    return p->types[ty.idx()];
                } else {
                    BUG(sp, "Impl parameters were not expected (got " << ty << ")");
                }
                break;
            case 1:
                if (const auto* p = this->getMethodParams()) {
                    ASSERT_BUG(sp, ty.idx() < p->types.size(), "Type param " << ty << " out of range for (max " << p->types.size() << ")");
                    return p->types[ty.idx()];
                } else {
                    BUG(sp, "Method parameters were not expected (got " << ty << ")");
                }
                break;
            default:
                BUG(sp, "Unexpected type param " << ty);
        }
    }
}

HIRConstGeneric MonomorphiserPP::getValue(const Span& sp, const HIRGenericRef& val) const /*override*/
{
    switch (val.group()) {
        case 0:
            if (const auto* p = this->getImplParams()) {
                ASSERT_BUG(sp, val.idx() < p->values.size(), "Value param " << val << " out of range for (max " << p->values.size() << ")");
                return p->values[val.idx()].clone();
            } else {
                BUG(sp, "Impl parameters were not expected (got " << val << ")");
            }
            break;
        case 1:
            if (const auto* p = this->getMethodParams()) {
                ASSERT_BUG(sp, val.idx() < p->values.size(), "Value param " << val << " out of range for (max " << p->values.size() << ")");
                return p->values[val.idx()].clone();
            } else {
                BUG(sp, "Method parameters were not expected (got " << val << ")");
            }
            break;
        default:
            BUG(sp, "Unexpected value param " << val);
    }
}

//t_cb_generic MonomorphState::get_cb(const Span& sp) const
//{
//}
::std::ostream& operator<<(::std::ostream& os, const MonomorphState& ms) {
    os << "MonomorphState {";
    if (ms.selfTy != HIRTypeRef()) {
        os << " self=" << ms.selfTy;
    }
    if (ms.ppImpl) {
        os << " I=" << *ms.ppImpl;
    }
    if (ms.ppMethod) {
        os << " M=" << *ms.ppMethod;
    }
    os << " }";
    return os;
}

void checkTypeClassPrimitive(const Span& sp, const HIRTypeData* type, HIRInferClass ic, HIRCoreType ct) {
    switch (ic) {
        case HIRInferClass::None:
            break;
        case HIRInferClass::Float:
            switch (ct) {
                case HIRCoreType::F16:
                case HIRCoreType::F32:
                case HIRCoreType::F64:
                case HIRCoreType::F128:
                    break;
                default:
                    ERROR(sp, E0000, "Type unificiation of float literal with non-float - " << type);
            }
            break;
        case HIRInferClass::Integer:
            switch (ct) {
                case HIRCoreType::I8:
                case HIRCoreType::U8:
                case HIRCoreType::I16:
                case HIRCoreType::U16:
                case HIRCoreType::I32:
                case HIRCoreType::U32:
                case HIRCoreType::I64:
                case HIRCoreType::U64:
                case HIRCoreType::I128:
                case HIRCoreType::U128:
                case HIRCoreType::Isize:
                case HIRCoreType::Usize:
                    break;
                default:
                    ERROR(sp, E0000, "Type unificiation of integer literal with non-integer - " << type);
            }
            break;
    }
}

bool primitiveOperatorHasBuiltin(TypeckPrimitiveOperator op, const HIRTypeData* left, const HIRTypeData* right) {
    const auto* leftPrimitive = left->opt_Primitive();
    const auto* rightPrimitive = right->opt_Primitive();

    const auto sameNumeric = [&]() {
        return left == right && leftPrimitive && (isInteger(*leftPrimitive) || isFloat(*leftPrimitive));
    };
    const auto sameBitwise = [&]() {
        return left == right && leftPrimitive && (isInteger(*leftPrimitive) || *leftPrimitive == HIRCoreType::Bool);
    };
    const auto shift = [&]() {
        return leftPrimitive && rightPrimitive && isInteger(*leftPrimitive) && isInteger(*rightPrimitive);
    };
    const auto comparison = [&]() {
        if (left != right) {
            return false;
        }
        return left->is_Pointer() || (leftPrimitive && *leftPrimitive != HIRCoreType::Str);
    };

    switch (op) {
        case TypeckPrimitiveOperator::Add:
        case TypeckPrimitiveOperator::Sub:
        case TypeckPrimitiveOperator::Mul:
        case TypeckPrimitiveOperator::Div:
        case TypeckPrimitiveOperator::Rem:
        case TypeckPrimitiveOperator::AddAssign:
        case TypeckPrimitiveOperator::SubAssign:
        case TypeckPrimitiveOperator::MulAssign:
        case TypeckPrimitiveOperator::DivAssign:
        case TypeckPrimitiveOperator::RemAssign:
            return sameNumeric();

        case TypeckPrimitiveOperator::BitAnd:
        case TypeckPrimitiveOperator::BitOr:
        case TypeckPrimitiveOperator::BitXor:
        case TypeckPrimitiveOperator::BitAndAssign:
        case TypeckPrimitiveOperator::BitOrAssign:
        case TypeckPrimitiveOperator::BitXorAssign:
            return sameBitwise();

        case TypeckPrimitiveOperator::Shl:
        case TypeckPrimitiveOperator::Shr:
        case TypeckPrimitiveOperator::ShlAssign:
        case TypeckPrimitiveOperator::ShrAssign:
            return shift();

        case TypeckPrimitiveOperator::Equal:
        case TypeckPrimitiveOperator::Order:
            return comparison();

        case TypeckPrimitiveOperator::None:
        case TypeckPrimitiveOperator::Not:
        case TypeckPrimitiveOperator::Neg:
        case TypeckPrimitiveOperator::Deref:
            return false;
    }
    throw "";
}

// For these binary language operations, once the left-hand type is known
// it also fixes an otherwise untyped right-hand operand. Shifts are
// deliberately excluded: their right-hand side need only be an integer
// and may have a different type.
bool primitiveOperatorLhsDeterminesRhs(TypeckPrimitiveOperator op, const HIRTypeData* left) {
    const auto* primitive = left->opt_Primitive();
    const auto numeric = primitive && (isInteger(*primitive) || isFloat(*primitive));
    const auto bitwise = primitive && (isInteger(*primitive) || *primitive == HIRCoreType::Bool);
    const auto comparison = left->is_Pointer() || (primitive && *primitive != HIRCoreType::Str);

    switch (op) {
        case TypeckPrimitiveOperator::Add:
        case TypeckPrimitiveOperator::Sub:
        case TypeckPrimitiveOperator::Mul:
        case TypeckPrimitiveOperator::Div:
        case TypeckPrimitiveOperator::Rem:
        case TypeckPrimitiveOperator::AddAssign:
        case TypeckPrimitiveOperator::SubAssign:
        case TypeckPrimitiveOperator::MulAssign:
        case TypeckPrimitiveOperator::DivAssign:
        case TypeckPrimitiveOperator::RemAssign:
            return numeric;

        case TypeckPrimitiveOperator::BitAnd:
        case TypeckPrimitiveOperator::BitOr:
        case TypeckPrimitiveOperator::BitXor:
        case TypeckPrimitiveOperator::BitAndAssign:
        case TypeckPrimitiveOperator::BitOrAssign:
        case TypeckPrimitiveOperator::BitXorAssign:
            return bitwise;

        case TypeckPrimitiveOperator::Equal:
        case TypeckPrimitiveOperator::Order:
            return comparison;

        case TypeckPrimitiveOperator::Shl:
        case TypeckPrimitiveOperator::Shr:
        case TypeckPrimitiveOperator::ShlAssign:
        case TypeckPrimitiveOperator::ShrAssign:
        case TypeckPrimitiveOperator::None:
        case TypeckPrimitiveOperator::Not:
        case TypeckPrimitiveOperator::Neg:
        case TypeckPrimitiveOperator::Deref:
            return false;
    }
    throw "";
}

// A binary language candidate is available either when both operands are
// already known to be valid primitive inputs, or when the known lhs
// determines the still-inferred rhs.
bool primitiveOperatorHasLanguageCandidate(TypeckPrimitiveOperator op, const HIRTypeData* left, const HIRTypeData* right) {
    return primitiveOperatorHasBuiltin(op, left, right) || (right->is_Infer() && primitiveOperatorLhsDeterminesRhs(op, left));
}

bool primitiveOperatorHasBuiltin(TypeckPrimitiveOperator op, const HIRTypeData* value) {
    if (op == TypeckPrimitiveOperator::Deref) {
        return value->is_Borrow() || value->is_Pointer();
    }

    const auto* primitive = value->opt_Primitive();
    if (!primitive) {
        return false;
    }

    switch (op) {
        case TypeckPrimitiveOperator::Not:
            return *primitive == HIRCoreType::Bool || isInteger(*primitive);
        case TypeckPrimitiveOperator::Neg:
            if (isFloat(*primitive)) {
                return true;
            }
            switch (*primitive) {
                case HIRCoreType::Isize:
                case HIRCoreType::I8:
                case HIRCoreType::I16:
                case HIRCoreType::I32:
                case HIRCoreType::I64:
                case HIRCoreType::I128:
                    return true;
                default:
                    return false;
            }
        case TypeckPrimitiveOperator::Deref:
            return false;
        default:
            return false;
    }
}
