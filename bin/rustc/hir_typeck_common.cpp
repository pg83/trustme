#include "hir_typeck_common.h"
#include "hir_path.h"
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
    const LList<const HIR::TypeData*>* curRecurseStack = nullptr;

    virtual typename W<HIR::TypeData>::T& getTyData(const HIR::TypeData* ty) const = 0;

    virtual bool visit_path_params(typename W<::HIR::PathParams>::T& tpl) {
        for (auto& ty : tpl.types) {
            if (visit_type(ty)) {
                return true;
            }
        }
        return false;
    }

    virtual bool visit_trait_path(typename W<::HIR::TraitPath>::T& tpl) {
        if (visit_path_params(tpl.mPath.mParams)) {
            return true;
        }
        for (auto& assoc : tpl.typeBounds) {
            visit_path_params(assoc.second.source_trait.mParams);
            if (visit_type(assoc.second.type)) {
                return true;
            }
        }
        for (auto& assoc : tpl.traitBounds) {
            visit_path_params(assoc.second.source_trait.mParams);
            for (auto& t : assoc.second.traits) {
                visit_trait_path(t);
            }
        }
        return false;
    }

    virtual bool visit_path(typename W<HIR::Path>::T& path) {
        TU_MATCH_HDRA((path.mData), {)
        TU_ARMA(Generic, e) {
                return visit_path_params(e.mParams);
            }
            TU_ARMA(UfcsInherent, e) {
                return visit_type(e.type) || visit_path_params(e.params);
            }
            TU_ARMA(UfcsKnown, e) {
                return visit_type(e.type) || visit_path_params(e.trait.mParams) || visit_path_params(e.params);
            }
            TU_ARMA(UfcsUnknown, e) {
                return visit_type(e.type) || visit_path_params(e.params);
            }
        }
        throw "";
    }

    virtual bool visit_type(const HIR::TypeData* ty) {
        if (curRecurseStack) {
            for (const auto* p : *curRecurseStack) {
                if (p == ty) {
                    return false;
                }
            }
        }

        struct _ {
            typedef LList<const HIR::TypeData*> stack_t;
            const stack_t*& dst;
            stack_t stack;

            _(const stack_t*& dst, const HIR::TypeData* ty)
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
                return visit_path(e.path);
            }
            TU_ARMA(TraitObject, e) {
                if (visit_trait_path(e.mTrait)) {
                    return true;
                }
                for (auto& trait : e.markers) {
                    if (visit_path_params(trait.mParams)) {
                        return true;
                    }
                }
                return false;
            }
            TU_ARMA(ErasedType, e) {
                for (auto& trait : e.traits) {
                    if (visit_trait_path(trait)) {
                        return true;
                    }
                }
                visit_path_params(e.use);
            TU_MATCH_HDRA( (e.inner), {)
            TU_ARMA(Fcn, ee) {
                        if (visit_path(ee.origin)) {
                            return true;
                        }
                    }
                    TU_ARMA(Known, ee) {
                        if (visit_type(ee)) {
                            return true;
                        }
                    }
                    TU_ARMA(Alias, ee) {
                        visit_path_params(ee.params);
                    }
            }
            return false;
            }
            TU_ARMA(Array, e) {
                return visit_type(e.inner);
            }
            TU_ARMA(Slice, e) {
                return visit_type(e.inner);
            }
            TU_ARMA(Tuple, e) {
                for (auto& ty : e) {
                    if (visit_type(ty)) {
                        return true;
                    }
                }
                return false;
            }
            TU_ARMA(Borrow, e) {
                return visit_type(e.inner);
            }
            TU_ARMA(Pointer, e) {
                return visit_type(e.inner);
            }
            TU_ARMA(NamedFunction, e) {
                return visit_path(e.path);
            }
            TU_ARMA(Function, e) {
                for (auto& ty : e.argTypes) {
                    if (visit_type(ty)) {
                        return true;
                    }
                }
                return visit_type(e.mRettype);
            }
            TU_ARMA(NodeType, e) {
                // These just have a node pointer, no visiting
            }
        }
        return false;
    }
};

struct TyVisitorCbConst: TyVisitor<WConst> {
    t_cb_visit_ty callback;

    const HIR::TypeData& getTyData(const HIR::TypeData* ty) const override {
        return *ty;
    }

    bool visit_type(const ::HIR::TypeData* ty) override {
        if (callback(ty)) {
            return true;
        }
        return TyVisitor::visit_type(ty);
    }
};

bool visit_ty_with(const ::HIR::TypeData* ty, t_cb_visit_ty callback) {
    TyVisitorCbConst v;
    v.callback = callback;
    return v.visit_type(ty);
}

bool visit_trait_path_tys_with(const ::HIR::TraitPath& path, t_cb_visit_ty callback) {
    TyVisitorCbConst v;
    v.callback = callback;
    return v.visit_trait_path(path);
}

bool visit_path_tys_with(const ::HIR::Path& path, t_cb_visit_ty callback) {
    TyVisitorCbConst v;
    v.callback = callback;
    return v.visit_path(path);
}

namespace {
    struct TyRewriter {
        HIR::TypeInterner& types;
        t_cb_rewrite_ty callback;
        ::std::vector<HIR::TypeRef> stack;

        bool rewrite_path_params(HIR::PathParams& params) {
            for (auto& type : params.types) {
                if (rewrite_type(type)) return true;
            }
            return false;
        }

        bool rewrite_trait_path(HIR::TraitPath& trait) {
            if (rewrite_path_params(trait.mPath.mParams)) return true;
            for (auto& assoc : trait.typeBounds) {
                if (rewrite_path_params(assoc.second.source_trait.mParams)
                    || rewrite_path_params(assoc.second.atyParams)
                    || rewrite_type(assoc.second.type)) return true;
            }
            for (auto& assoc : trait.traitBounds) {
                if (rewrite_path_params(assoc.second.source_trait.mParams)
                    || rewrite_path_params(assoc.second.atyParams)) return true;
                for (auto& bound : assoc.second.traits) {
                    if (rewrite_trait_path(bound)) return true;
                }
            }
            return false;
        }

        bool rewrite_path(HIR::Path& path) {
            TU_MATCH_HDRA((path.mData), {)
            TU_ARMA(Generic, e) return rewrite_path_params(e.mParams);
            TU_ARMA(UfcsInherent, e) return rewrite_type(e.type) || rewrite_path_params(e.params) || rewrite_path_params(e.impl_params);
            TU_ARMA(UfcsKnown, e) return rewrite_type(e.type) || rewrite_path_params(e.trait.mParams) || rewrite_path_params(e.params);
            TU_ARMA(UfcsUnknown, e) return rewrite_type(e.type) || rewrite_path_params(e.params);
            }
            throw "";
        }

        bool rewrite_type(HIR::TypeRef& type) {
            if (!type || ::std::find(stack.begin(), stack.end(), type) != stack.end()) return false;
            const auto original = type;
            auto data = original->cloneData();
            HIR::TypeRef rewritten = original;
            const bool stop = callback(rewritten, data);
            if (rewritten != original) {
                type = rewritten;
                return stop;
            }

            stack.push_back(original);
            bool childStop = false;
            if (!stop) {
                TU_MATCH_HDRA((data), {)
                TU_ARMA(Infer, e) {}
                TU_ARMA(Diverge, e) {}
                TU_ARMA(Primitive, e) {}
                TU_ARMA(Generic, e) {}
                TU_ARMA(Path, e) childStop = rewrite_path(e.path);
                TU_ARMA(TraitObject, e) {
                    childStop = rewrite_trait_path(e.mTrait);
                    for (auto& marker : e.markers) if (!childStop) childStop = rewrite_path_params(marker.mParams);
                }
                TU_ARMA(ErasedType, e) {
                    for (auto& trait : e.traits) if (!childStop) childStop = rewrite_trait_path(trait);
                    if (!childStop) childStop = rewrite_path_params(e.use);
                    if (!childStop) {
                        TU_MATCH_HDRA((e.inner), {)
                        TU_ARMA(Fcn, inner) childStop = rewrite_path(inner.origin);
                        TU_ARMA(Known, inner) childStop = rewrite_type(inner);
                        TU_ARMA(Alias, inner) childStop = rewrite_path_params(inner.params);
                        }
                    }
                }
                TU_ARMA(Array, e) childStop = rewrite_type(e.inner);
                TU_ARMA(Slice, e) childStop = rewrite_type(e.inner);
                TU_ARMA(Tuple, e) for (auto& inner : e) if (!childStop) childStop = rewrite_type(inner);
                TU_ARMA(Borrow, e) childStop = rewrite_type(e.inner);
                TU_ARMA(Pointer, e) childStop = rewrite_type(e.inner);
                TU_ARMA(NamedFunction, e) childStop = rewrite_path(e.path);
                TU_ARMA(Function, e) {
                    for (auto& arg : e.argTypes) if (!childStop) childStop = rewrite_type(arg);
                    if (!childStop) childStop = rewrite_type(e.mRettype);
                }
                TU_ARMA(NodeType, e) {}
                }
            }
            stack.pop_back();
            type = types.intern(mv$(data));
            return stop || childStop;
        }
    };
}

bool rewrite_ty_with(::HIR::TypeInterner& types, ::HIR::TypeRef& ty, t_cb_rewrite_ty callback) {
    TyRewriter rewriter{types, mv$(callback), {}};
    return rewriter.rewrite_type(ty);
}

bool rewrite_path_tys_with(::HIR::TypeInterner& types, ::HIR::Path& path, t_cb_rewrite_ty callback) {
    TyRewriter rewriter{types, mv$(callback), {}};
    return rewriter.rewrite_path(path);
}

struct TyVisitorMonomorphNeeded: TyVisitor<WConst> {
    bool ignore_lifetimes;

    TyVisitorMonomorphNeeded(bool ignore_lifetimes)
        : ignore_lifetimes(ignore_lifetimes)
    {
    }

    const HIR::TypeData& getTyData(const HIR::TypeData* ty) const override {
        return *ty;
    }

    bool is_generic_lft(const ::HIR::LifetimeRef& lft) const {
        return lft.is_param() && (lft.binding >> 8) != 3;
    }

    bool visit_path_params(const ::HIR::PathParams& pp) override {
        if (!this->ignore_lifetimes) {
            for (const auto& lft : pp.mLifetimes) {
                if (is_generic_lft(lft)) {
                    return true;
                }
            }
        }
        for (const auto& v : pp.values) {
            if (v.is_Generic()) {
                return true;
            }
        }
        return TyVisitor::visit_path_params(pp);
    }

    bool visit_type(const ::HIR::TypeData* ty) override {
        if (ty->is_Generic()) {
            return true;
        }
        if (ty->is_Array() && ty->as_Array().size.is_Unevaluated() /*&& ty->as_Array().size.as_Unevaluated().*/) {
            return true;
        }
        if (!this->ignore_lifetimes) {
            if (ty->is_Borrow() && is_generic_lft(ty->as_Borrow().lifetime)) {
                return true;
            }
            if (ty->is_TraitObject() && is_generic_lft(ty->as_TraitObject().lifetime)) {
                return true;
            }
            if (ty->is_ErasedType()) {
                for (const auto& l : ty->as_ErasedType().lifetimeBounds) {
                    if (is_generic_lft(l)) {
                        return true;
                    }
                }
            }
        }
        return TyVisitor::visit_type(ty);
    }
};

bool monomorphise_pathparams_needed(const ::HIR::PathParams& tpl, bool ignore_lifetimes /*=false*/) {
    TyVisitorMonomorphNeeded v{ignore_lifetimes};
    return v.visit_path_params(tpl);
}

bool monomorphise_traitpath_needed(const ::HIR::TraitPath& tpl, bool ignore_lifetimes /*=false*/) {
    TyVisitorMonomorphNeeded v{ignore_lifetimes};
    return v.visit_trait_path(tpl);
}

bool monomorphise_path_needed(const ::HIR::Path& tpl, bool ignore_lifetimes /*=false*/) {
    TyVisitorMonomorphNeeded v{ignore_lifetimes};
    return v.visit_path(tpl);
}

bool monomorphise_type_needed(const ::HIR::TypeData* tpl, bool ignore_lifetimes /*=false*/) {
    return tpl->needs_monomorphisation(ignore_lifetimes);
}

::HIR::TypeRef Monomorphiser::monomorph_type(const Span& sp, const ::HIR::TypeData* tpl, bool allowInfer /*=true*/) const {
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
            auto binding = e.binding.is_Opaque() ? ::HIR::TypePathBinding() : e.binding.clone();
            auto hrtbs = e.hrtbs ? box$(e.hrtbs->clone()) : nullptr;
            return types.intern(::HIR::TypeData::make_Path({this->monomorph_path(sp, e.path, allowInfer), mv$(binding), mv$(hrtbs)}));
        }
        TU_ARMA(Generic, e) {
            return this->getType(sp, e);
        }
        TU_ARMA(TraitObject, e) {
            ::HIR::TypeData::Data_TraitObject to;
            if (e.mTrait.hrtbs) {
                to.mTrait.hrtbs = box$(e.mTrait.hrtbs->clone());
            }
            {
                auto _ = push_hrb(e.mTrait.hrtbs);
                to.mTrait = this->monomorph_traitpath(sp, e.mTrait, allowInfer, false);
                for (const auto& trait : e.markers) {
                    to.markers.push_back(this->monomorph_genericpath(sp, trait, allowInfer, false));
                }
            }
            to.lifetime = monomorph_lifetime(sp, e.lifetime);
            return types.intern(::HIR::TypeData::make_TraitObject(mv$(to)));
        }
        TU_ARMA(ErasedType, e) {
            ::std::vector<::HIR::TraitPath> traits;
            traits.reserve(e.traits.size());
            for (const auto& trait : e.traits) {
                traits.push_back(this->monomorph_traitpath(sp, trait, allowInfer, false));
            }
            ::std::vector<::HIR::LifetimeRef> lfts;
            for (const auto& lft : e.lifetimeBounds) {
                lfts.push_back(monomorph_lifetime(sp, lft));
            }

            HIR::TypeDataErasedTypeInner inner;
        TU_MATCH_HDRA( (e.inner), {)
        TU_ARMA(Fcn, ee) {
                    inner = ::HIR::TypeDataErasedTypeInner::Data_Fcn{this->monomorph_path(sp, ee.origin, allowInfer), ee.index};
                }
                TU_ARMA(Alias, ee) {
                    inner = ::HIR::TypeDataErasedTypeInner::Data_Alias{this->monomorph_path_params(sp, ee.params, allowInfer), ee.inner};
                }
                TU_ARMA(Known, ee) {
                    inner = this->monomorph_type(sp, ee, allowInfer);
                }
        }

        return types.intern(::HIR::TypeData::make_ErasedType(::HIR::TypeData::Data_ErasedType {
            e.isSized,
            mv$(traits),
            mv$(lfts),
            mv$(inner),
            this->monomorph_path_params(sp, e.use, allowInfer),
            e.usePresent
            }));
        }
        TU_ARMA(Array, e) {
            return types.intern(::HIR::TypeData::make_Array({this->monomorph_type(sp, e.inner, allowInfer), this->monomorph_arraysize(sp, e.size)}));
        }
        TU_ARMA(Slice, e) {
            return types.slice(this->monomorph_type(sp, e.inner, allowInfer));
        }
        TU_ARMA(Tuple, e) {
            ::std::vector<::HIR::TypeRef> types;
            for (const auto& ty : e) {
                types.push_back(this->monomorph_type(sp, ty, allowInfer));
            }
            return this->types.tuple(mv$(types));
        }
        TU_ARMA(Borrow, e) {
            return types.borrow(e.type, this->monomorph_type(sp, e.inner, allowInfer), monomorph_lifetime(sp, e.lifetime));
        }
        TU_ARMA(Pointer, e) {
            return types.pointer(e.type, this->monomorph_type(sp, e.inner, allowInfer));
        }
        TU_ARMA(NamedFunction, e) {
            return types.intern(::HIR::TypeData::make_NamedFunction(
                ::HIR::TypeData::Data_NamedFunction{
                    this->monomorph_path(sp, e.path, allowInfer),
                    e.def.clone() // Should this become `nullptr`? Or should the definition be fixed
                }
            ));
        }
        TU_ARMA(Function, e) {
            auto _ = push_hrb(e.hrls);
            ::HIR::TypeDataFunctionPointer ft;
            ft.hrls = e.hrls.clone();
            ft.is_unsafe = e.is_unsafe;
            ft.is_variadic = e.is_variadic;
            ft.mAbi = e.mAbi;
            ft.mRettype = this->monomorph_type(sp, e.mRettype, allowInfer);
            for (const auto& arg : e.argTypes) {
                ft.argTypes.push_back(this->monomorph_type(sp, arg, allowInfer));
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

::HIR::LifetimeRef Monomorphiser::monomorph_lifetime(const Span& sp, const ::HIR::LifetimeRef& lft) const {
    if (lft.is_param()) {
        HIR::GenericRef g{"", lft.binding};

        // Have a flag/stack here for current defined HRL batches (trait paths and function pointers), if in one then do the hack
        // - Otherwise, pass to `get_lifetime`
        if (g.group() == HIR::GENERICHrtb) {
            if (const auto* hrtb = hasHrb()) {
                // TODO: Ensure that the param is in range (has some issues with nested?)
                //ASSERT_BUG(sp, g.idx() < hrtb->m_lifetimes.size(), "Found HRTB out of range - " << g << " from for" << hrtb->fmt_args());
                return lft;
            }
        }

        return getLifetime(sp, g);
    } else {
        return lft;
    }
}

::HIR::Path Monomorphiser::monomorph_path(const Span& sp, const ::HIR::Path& tpl, bool allowInfer /*=true*/) const {
    TU_MATCH_HDRA( (tpl.mData), {)
    TU_ARMA(Generic, e2) {
            return ::HIR::Path(this->monomorph_genericpath(sp, e2, allowInfer, false));
        }
        TU_ARMA(UfcsKnown, e2) {
            auto _ = push_hrb(e2.hrtbs);
            auto rv = ::HIR::Path(::HIR::Path::Data::make_UfcsKnown({this->monomorph_type(sp, e2.type, allowInfer), this->monomorph_genericpath(sp, e2.trait, allowInfer, false), e2.item, this->monomorph_path_params(sp, e2.params, allowInfer), e2.hrtbs ? box$(e2.hrtbs->clone()) : nullptr}));
            return rv;
        }
        TU_ARMA(UfcsUnknown, e2) {
            return ::HIR::Path::Data::make_UfcsUnknown({this->monomorph_type(sp, e2.type, allowInfer), e2.item, this->monomorph_path_params(sp, e2.params, allowInfer)});
        }
        TU_ARMA(UfcsInherent, e2) {
            return ::HIR::Path::Data::make_UfcsInherent({this->monomorph_type(sp, e2.type, allowInfer), e2.item, this->monomorph_path_params(sp, e2.params, allowInfer), this->monomorph_path_params(sp, e2.impl_params, allowInfer)});
        }
    }
    throw "";
}

::HIR::TraitPath Monomorphiser::monomorph_traitpath(const Span& sp, const ::HIR::TraitPath& tpl, bool allowInfer, bool ignore_hrls) const {
    ::std::unique_ptr<PopOnDrop> _;
    if (tpl.hrtbs && !ignore_hrls) {
        _ = std::make_unique<PopOnDrop>(push_hrb(*tpl.hrtbs));
    }

    ::HIR::TraitPath rv{tpl.hrtbs ? box$(tpl.hrtbs->clone()) : nullptr, this->monomorph_genericpath(sp, tpl.mPath, allowInfer, true), {}, {}, tpl.traitPtr, tpl.constness};
    rv.lifetimeElision = tpl.lifetimeElision;

    for (const auto& assoc : tpl.typeBounds) {
        rv.typeBounds.insert(::std::make_pair(assoc.first, this->monomorph_tp_aty_equal(sp, assoc.second, allowInfer)));
    }
    for (const auto& assoc : tpl.traitBounds) {
        auto v = HIR::TraitPath::AtyBound{this->monomorph_genericpath(sp, assoc.second.source_trait, allowInfer, false), {}};
        for (const auto& trait : assoc.second.traits) {
            v.traits.push_back(monomorph_traitpath(sp, trait, allowInfer, false));
        }
        rv.traitBounds.insert(::std::make_pair(assoc.first, std::move(v)));
    }

    return rv;
}

::HIR::TraitPath::AtyEqual Monomorphiser::monomorph_tp_aty_equal(const Span& sp, const ::HIR::TraitPath::AtyEqual& tpl, bool allowInfer) const {
    return HIR::TraitPath::AtyEqual{this->monomorph_genericpath(sp, tpl.source_trait, allowInfer, false), {}, this->monomorph_type(sp, tpl.type, allowInfer)};
}

::HIR::ConstGeneric Monomorphiser::monomorph_constgeneric(const Span& sp, const ::HIR::ConstGeneric& val, bool allowInfer) const {
    if (const auto* ge = val.opt_Generic()) {
        return this->getValue(sp, *ge);
    } else if (const auto* ge = val.opt_Unevaluated()) {
        auto rv = HIR::ConstGeneric(std::make_unique<HIR::ConstGenericUnevaluated>((*ge)->monomorph(sp, *this, true)));
        // TODO: Evaluate this constant (if possible), but that requires knowing the target type :/
        return rv;
    } else {
        return val.clone();
    }
}

::HIR::PathParams Monomorphiser::monomorph_path_params(const Span& sp, const ::HIR::PathParams& tpl, bool allowInfer) const {
    ::HIR::PathParams rv;

    rv.mLifetimes.reserve(tpl.mLifetimes.size());
    for (const auto& lft : tpl.mLifetimes) {
        rv.mLifetimes.push_back(this->monomorph_lifetime(sp, lft));
    }

    rv.types.reserve(tpl.types.size());
    for (const auto& ty : tpl.types) {
        rv.types.push_back(this->monomorph_type(sp, ty, allowInfer));
    }

    rv.values.reserve(tpl.values.size());
    for (const auto& val : tpl.values) {
        rv.values.push_back(monomorph_constgeneric(sp, val, allowInfer));
    }

    return rv;
}

::HIR::GenericPath Monomorphiser::monomorph_genericpath(const Span& sp, const ::HIR::GenericPath& tpl, bool allowInfer, bool ignore_hrls) const {
    return ::HIR::GenericPath(tpl.mPath, this->monomorph_path_params(sp, tpl.mParams, allowInfer));
}

::HIR::ArraySize Monomorphiser::monomorph_arraysize(const Span& sp, const ::HIR::ArraySize& tpl) const {
    if (auto* se = tpl.opt_Unevaluated()) {
        HIR::ArraySize sz;
        if (se->is_Generic()) {
            sz = this->getValue(sp, se->as_Generic());
            DEBUG(tpl << " -> " << sz);
        } else if (se->is_Unevaluated()) {
            sz = HIR::ConstGeneric(std::make_unique<HIR::ConstGenericUnevaluated>(se->as_Unevaluated()->monomorph(sp, *this, true)));
        } else {
            sz = se->clone();
        }
        se = sz.opt_Unevaluated();
        assert(se);

        // Evaluate, if possible
        if (se->is_Unevaluated()) {
            if (this->constevalCrate) {
                ConvertHIRConstantEvaluateConstGeneric(sp, *this->constevalCrate, types.primitive(HIR::CoreType::Usize), sz.as_Unevaluated());
            } else {
                DEBUG("TODO: Evaluate unevaluated generic for array size - " << *se);
            }
        }

        if (const auto* e = se->opt_Evaluated()) {
            return (*e)->read_usize(0);
        }
        return sz;
    } else {
        return tpl.clone();
    }
}

struct CloneTyWithMonomorph: Monomorphiser {
    t_cb_clone_ty callback;

    explicit CloneTyWithMonomorph(HIR::TypeInterner& types): Monomorphiser(types) {}

    ::HIR::TypeRef getType(const Span& sp, const ::HIR::GenericRef& g) const override {
        return types.generic(g.name, g.binding);
    }

    ::HIR::ConstGeneric getValue(const Span& sp, const ::HIR::GenericRef& g) const override {
        return g;
    }

    ::HIR::LifetimeRef getLifetime(const Span& sp, const ::HIR::GenericRef& g) const override {
        return HIR::LifetimeRef(g.binding);
    }

    ::HIR::TypeRef monomorph_type(const Span& sp, const ::HIR::TypeData* ty, bool allowInfer = true) const {
        ::HIR::TypeRef rv;

        if (callback(ty, rv)) {
            //DEBUG(tpl << " => " << rv);
            return rv;
        }
        return Monomorphiser::monomorph_type(sp, ty, allowInfer);
    }
};

::HIR::PathParams clonePathParamsWith(::HIR::TypeInterner& types, const Span& sp, const ::HIR::PathParams& tpl, t_cb_clone_ty callback) {
    ::HIR::PathParams rv;
    for (const auto& v : tpl.mLifetimes) {
        rv.mLifetimes.push_back(v);
    }
    rv.types.reserve(tpl.types.size());
    for (const auto& ty : tpl.types) {
        rv.types.push_back(cloneTyWith(types, sp, ty, callback));
    }
    for (const auto& v : tpl.values) {
        rv.values.push_back(v.clone());
    }
    return rv;
}

::HIR::TypeRef cloneTyWith(::HIR::TypeInterner& types, const Span& sp, const ::HIR::TypeData* tpl, t_cb_clone_ty callback) {
    CloneTyWithMonomorph m(types);
    m.callback = std::move(callback);
    return m.monomorph_type(sp, tpl, true);
}

::HIR::TypeRef MonomorphiserPP::getType(const Span& sp, const ::HIR::GenericRef& ty) const /*override*/
{
    if (ty.is_self()) {
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

::HIR::ConstGeneric MonomorphiserPP::getValue(const Span& sp, const ::HIR::GenericRef& val) const /*override*/
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

::HIR::LifetimeRef MonomorphiserPP::getLifetime(const Span& sp, const ::HIR::GenericRef& lft_ref) const /*override*/
{
    // HACK: If no params are present at all, just return unchanged
    // - Note: Equality on PathParams ignores lifetimes, hence the second check
    if ((!this->getImplParams() || (*this->getImplParams() == HIR::PathParams() && this->getImplParams()->mLifetimes.empty())) && (!this->getMethodParams() || (*this->getMethodParams() == HIR::PathParams() && this->getMethodParams()->mLifetimes.empty())) && (!this->getHrbParams() || (*this->getHrbParams() == HIR::PathParams() && this->getHrbParams()->mLifetimes.empty()))) {
        DEBUG("Passthrough " << lft_ref);
        return HIR::LifetimeRef(lft_ref.binding);
    }

    switch (lft_ref.group()) {
        // HACK: Pass through when no lifetimes were recorded at all (e.g. a trait-declared lifetime in a default method body)
        case 0:
            if (const auto* p = this->getImplParams()) {
                if (p->mLifetimes.empty()) {
                    DEBUG("No impl lifetimes recorded - passthrough " << lft_ref);
                    return HIR::LifetimeRef(lft_ref.binding);
                }
                ASSERT_BUG(sp, lft_ref.idx() < p->mLifetimes.size(), "Lifetime param " << lft_ref << " out of range for (max " << p->mLifetimes.size() << ")");
                return p->mLifetimes[lft_ref.idx()];
            } else {
                BUG(sp, "Impl lifetime parameters were not expected (got " << lft_ref << ")");
            }
            break;
        case 1:
            if (const auto* p = this->getMethodParams()) {
                if (p->mLifetimes.empty()) {
                    DEBUG("No method lifetimes recorded - passthrough " << lft_ref);
                    return HIR::LifetimeRef(lft_ref.binding);
                }
                ASSERT_BUG(sp, lft_ref.idx() < p->mLifetimes.size(), "Lifetime param " << lft_ref << " out of range for (max " << p->mLifetimes.size() << ")");
                return p->mLifetimes[lft_ref.idx()];
            } else {
                BUG(sp, "Method lifetime parameters were not expected (got " << lft_ref << ")");
            }
            break;
        case 2: // Placeholders, just pass through
            DEBUG("Placeholder " << lft_ref);
            return HIR::LifetimeRef(lft_ref.binding);
        case 3: // HRLs
            if (const auto* p = this->getHrbParams()) {
                if (lft_ref.idx() >= p->mLifetimes.size()) {
                    DEBUG("HRL " << lft_ref << " out of range (max " << p->mLifetimes.size() << ") - passthrough");
                    return HIR::LifetimeRef(lft_ref.binding);
                }
                return p->mLifetimes[lft_ref.idx()];
            } else {
                BUG(sp, "Higher-ranked lifetime parameters were not expected (got " << lft_ref << ")");
                //DEBUG("No HRBs " << lft_ref);
                //return HIR::LifetimeRef(lft_ref.binding);
            }
            break;
        default:
            BUG(sp, "Unexpected lifetime param " << lft_ref);
    }
}

//t_cb_generic MonomorphState::get_cb(const Span& sp) const
//{
//    return monomorphise_type_get_cb(sp, this->self_ty, this->pp_impl, this->pp_method);
//}
::std::ostream& operator<<(::std::ostream& os, const MonomorphState& ms) {
    os << "MonomorphState {";
    if (ms.self_ty != HIR::TypeRef()) {
        os << " self=" << ms.self_ty;
    }
    if (ms.pp_impl) {
        os << " I=" << *ms.pp_impl;
    }
    if (ms.pp_method) {
        os << " M=" << *ms.pp_method;
    }
    //if(ms.pp_hrb)
    //    os << " H=" << *ms.pp_hrb;
    os << " }";
    return os;
}

void checkTypeClassPrimitive(const Span& sp, const ::HIR::TypeData* type, ::HIR::InferClass ic, ::HIR::CoreType ct) {
    switch (ic) {
        case ::HIR::InferClass::None:
            break;
        case ::HIR::InferClass::Float:
            switch (ct) {
                case ::HIR::CoreType::F16:
                case ::HIR::CoreType::F32:
                case ::HIR::CoreType::F64:
                case ::HIR::CoreType::F128:
                    break;
                default:
                    ERROR(sp, E0000, "Type unificiation of float literal with non-float - " << type);
            }
            break;
        case ::HIR::InferClass::Integer:
            switch (ct) {
                case ::HIR::CoreType::I8:
                case ::HIR::CoreType::U8:
                case ::HIR::CoreType::I16:
                case ::HIR::CoreType::U16:
                case ::HIR::CoreType::I32:
                case ::HIR::CoreType::U32:
                case ::HIR::CoreType::I64:
                case ::HIR::CoreType::U64:
                case ::HIR::CoreType::I128:
                case ::HIR::CoreType::U128:
                case ::HIR::CoreType::Isize:
                case ::HIR::CoreType::Usize:
                    break;
                default:
                    ERROR(sp, E0000, "Type unificiation of integer literal with non-integer - " << type);
            }
            break;
    }
}

namespace typeck {

bool primitive_operator_has_builtin(PrimitiveOperator op, const ::HIR::TypeData* left, const ::HIR::TypeData* right) {
    const auto* left_primitive = left->opt_Primitive();
    const auto* right_primitive = right->opt_Primitive();

    const auto same_numeric = [&]() {
        return left == right && left_primitive && (::HIR::is_integer(*left_primitive) || ::HIR::is_float(*left_primitive));
    };
    const auto same_bitwise = [&]() {
        return left == right && left_primitive && (::HIR::is_integer(*left_primitive) || *left_primitive == ::HIR::CoreType::Bool);
    };
    const auto shift = [&]() {
        return left_primitive && right_primitive && ::HIR::is_integer(*left_primitive) && ::HIR::is_integer(*right_primitive);
    };
    const auto comparison = [&]() {
        if (left != right) {
            return false;
        }
        return left->is_Pointer() || (left_primitive && *left_primitive != ::HIR::CoreType::Str);
    };

    switch (op) {
        case PrimitiveOperator::Add:
        case PrimitiveOperator::Sub:
        case PrimitiveOperator::Mul:
        case PrimitiveOperator::Div:
        case PrimitiveOperator::Rem:
        case PrimitiveOperator::AddAssign:
        case PrimitiveOperator::SubAssign:
        case PrimitiveOperator::MulAssign:
        case PrimitiveOperator::DivAssign:
        case PrimitiveOperator::RemAssign:
            return same_numeric();

        case PrimitiveOperator::BitAnd:
        case PrimitiveOperator::BitOr:
        case PrimitiveOperator::BitXor:
        case PrimitiveOperator::BitAndAssign:
        case PrimitiveOperator::BitOrAssign:
        case PrimitiveOperator::BitXorAssign:
            return same_bitwise();

        case PrimitiveOperator::Shl:
        case PrimitiveOperator::Shr:
        case PrimitiveOperator::ShlAssign:
        case PrimitiveOperator::ShrAssign:
            return shift();

        case PrimitiveOperator::Equal:
        case PrimitiveOperator::Order:
            return comparison();

        case PrimitiveOperator::None:
        case PrimitiveOperator::Not:
        case PrimitiveOperator::Neg:
        case PrimitiveOperator::Deref:
            return false;
    }
    throw "";
}
// For these binary language operations, once the left-hand type is known
// it also fixes an otherwise untyped right-hand operand. Shifts are
// deliberately excluded: their right-hand side need only be an integer
// and may have a different type.
bool primitive_operator_lhs_determines_rhs(PrimitiveOperator op, const ::HIR::TypeData* left) {
    const auto* primitive = left->opt_Primitive();
    const auto numeric = primitive && (::HIR::is_integer(*primitive) || ::HIR::is_float(*primitive));
    const auto bitwise = primitive && (::HIR::is_integer(*primitive) || *primitive == ::HIR::CoreType::Bool);
    const auto comparison = left->is_Pointer() || (primitive && *primitive != ::HIR::CoreType::Str);

    switch (op) {
        case PrimitiveOperator::Add:
        case PrimitiveOperator::Sub:
        case PrimitiveOperator::Mul:
        case PrimitiveOperator::Div:
        case PrimitiveOperator::Rem:
        case PrimitiveOperator::AddAssign:
        case PrimitiveOperator::SubAssign:
        case PrimitiveOperator::MulAssign:
        case PrimitiveOperator::DivAssign:
        case PrimitiveOperator::RemAssign:
            return numeric;

        case PrimitiveOperator::BitAnd:
        case PrimitiveOperator::BitOr:
        case PrimitiveOperator::BitXor:
        case PrimitiveOperator::BitAndAssign:
        case PrimitiveOperator::BitOrAssign:
        case PrimitiveOperator::BitXorAssign:
            return bitwise;

        case PrimitiveOperator::Equal:
        case PrimitiveOperator::Order:
            return comparison;

        case PrimitiveOperator::Shl:
        case PrimitiveOperator::Shr:
        case PrimitiveOperator::ShlAssign:
        case PrimitiveOperator::ShrAssign:
        case PrimitiveOperator::None:
        case PrimitiveOperator::Not:
        case PrimitiveOperator::Neg:
        case PrimitiveOperator::Deref:
            return false;
    }
    throw "";
}
// A binary language candidate is available either when both operands are
// already known to be valid primitive inputs, or when the known lhs
// determines the still-inferred rhs.
bool primitive_operator_has_language_candidate(PrimitiveOperator op, const ::HIR::TypeData* left, const ::HIR::TypeData* right) {
    return primitive_operator_has_builtin(op, left, right)
        || (right->is_Infer() && primitive_operator_lhs_determines_rhs(op, left));
}
bool primitive_operator_has_builtin(PrimitiveOperator op, const ::HIR::TypeData* value) {
    if (op == PrimitiveOperator::Deref) {
        return value->is_Borrow() || value->is_Pointer();
    }

    const auto* primitive = value->opt_Primitive();
    if (!primitive) {
        return false;
    }

    switch (op) {
        case PrimitiveOperator::Not:
            return *primitive == ::HIR::CoreType::Bool || ::HIR::is_integer(*primitive);
        case PrimitiveOperator::Neg:
            if (::HIR::is_float(*primitive)) {
                return true;
            }
            switch (*primitive) {
                case ::HIR::CoreType::Isize:
                case ::HIR::CoreType::I8:
                case ::HIR::CoreType::I16:
                case ::HIR::CoreType::I32:
                case ::HIR::CoreType::I64:
                case ::HIR::CoreType::I128:
                    return true;
                default:
                    return false;
            }
        case PrimitiveOperator::Deref:
            return false;
        default:
            return false;
    }
}
}
