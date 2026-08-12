#include "hir_typeck_helpers.h"
#include "hir_conv_main_bindings.h"
#include "trait_solver_mode.h"
#include <algorithm>
#include <optional>
#include <unordered_map>
#include <std/mem/obj_pool.h>
#include <std/mem/obj_list.h>

namespace {
    // TODO: De-duplicate this with `static.cpp`
    const HIR::GenericParams empty_params;

    // Give every fresh placeholder in one active trait goal the same stable
    // spelling.  This makes a recurrence through independently-instantiated
    // blanket impls visible to the solver without changing the goal's actual
    // type data or inference state.
    class CanonicalizeTraitGoal final: public Monomorphiser {
        mutable ::std::vector<::std::pair<RcString, RcString>> placeholderNames;

        RcString canonicalPlaceholderName(const RcString& name) const {
            for (const auto& entry : placeholderNames) {
                if (entry.first == name) {
                    return entry.second;
                }
            }
            auto canonical = RcString::new_interned(
                FMT("#solver-placeholder-" << placeholderNames.size())
            );
            placeholderNames.push_back({name, canonical});
            return canonical;
        }

    public:
        explicit CanonicalizeTraitGoal(::HIR::TypeInterner& types)
            : Monomorphiser(types)
        {
        }

        ::HIR::TypeRef get_type(
            const Span&, const ::HIR::GenericRef& generic
        ) const override {
            return generic.is_placeholder()
                ? types.generic(
                    canonicalPlaceholderName(generic.name), generic.binding
                )
                : types.generic(generic.name, generic.binding);
        }

        ::HIR::ConstGeneric get_value(
            const Span&, const ::HIR::GenericRef& generic
        ) const override {
            return ::HIR::ConstGeneric(
                generic.is_placeholder()
                    ? ::HIR::GenericRef(
                        canonicalPlaceholderName(generic.name), generic.binding
                    )
                    : generic
            );
        }

        ::HIR::LifetimeRef get_lifetime(
            const Span&, const ::HIR::GenericRef& generic
        ) const override {
            return ::HIR::LifetimeRef(generic.binding);
        }

        const ::std::vector<::std::pair<RcString, RcString>>&
        placeholder_names() const {
            return placeholderNames;
        }
    };

    class InstantiateCanonicalTraitResponse final: public Monomorphiser {
        const ::std::vector<::std::pair<RcString, RcString>>& goalNames;
        const uint64_t instance;
        mutable ::std::vector<::std::pair<RcString, RcString>> freshNames;

        RcString instantiate_placeholder_name(const RcString& canonical) const {
            for (const auto& entry : goalNames) {
                if (entry.second == canonical) {
                    return entry.first;
                }
            }
            for (const auto& entry : freshNames) {
                if (entry.first == canonical) {
                    return entry.second;
                }
            }
            auto fresh = RcString(FMT(
                "solver_response_" << instance << "_" << freshNames.size()
            ));
            freshNames.push_back({canonical, fresh});
            return fresh;
        }

    public:
        InstantiateCanonicalTraitResponse(
            ::HIR::TypeInterner& types,
            const ::std::vector<::std::pair<RcString, RcString>>& goal_names,
            uint64_t instance
        )
            : Monomorphiser(types)
            , goalNames(goal_names)
            , instance(instance)
        {
        }

        ::HIR::TypeRef get_type(
            const Span&, const ::HIR::GenericRef& generic
        ) const override {
            return types.generic(
                generic.is_placeholder()
                    ? instantiate_placeholder_name(generic.name)
                    : generic.name,
                generic.binding
            );
        }

        ::HIR::ConstGeneric get_value(
            const Span&, const ::HIR::GenericRef& generic
        ) const override {
            return ::HIR::ConstGeneric(
                generic.is_placeholder()
                    ? ::HIR::GenericRef(
                        instantiate_placeholder_name(generic.name),
                        generic.binding
                    )
                    : generic
            );
        }

        ::HIR::LifetimeRef get_lifetime(
            const Span&, const ::HIR::GenericRef& generic
        ) const override {
            return ::HIR::LifetimeRef(generic.binding);
        }
    };

    // Canonical query variables created while evaluating a goal are
    // existential.  They must be instantiated as fresh variables in the
    // caller's inference table before a root response leaves the solver.
    // Placeholders already present in the input goal are universal and stay
    // unchanged.
    class InstantiateTraitResponseForCaller final: public Monomorphiser {
        HMTypeInferrence& ivars;
        const ::std::vector<::std::pair<RcString, RcString>>& goalNames;
        mutable ::std::vector<::std::pair<::HIR::GenericRef, ::HIR::TypeRef>> typeValues;
        mutable ::std::vector<::std::pair<::HIR::GenericRef, ::HIR::ConstGeneric>> values;

        bool is_goal_placeholder(const ::HIR::GenericRef& generic) const {
            for (const auto& entry : goalNames) {
                if (entry.first == generic.name) {
                    return true;
                }
            }
            return false;
        }

    public:
        InstantiateTraitResponseForCaller(
            ::HIR::TypeInterner& types,
            HMTypeInferrence& ivars,
            const ::std::vector<::std::pair<RcString, RcString>>& goal_names
        )
            : Monomorphiser(types)
            , ivars(ivars)
            , goalNames(goal_names)
        {
        }

        ::HIR::TypeRef get_type(
            const Span&, const ::HIR::GenericRef& generic
        ) const override {
            if (!generic.is_placeholder() || is_goal_placeholder(generic)) {
                return Monomorphiser::types.generic(generic.name, generic.binding);
            }
            for (const auto& entry : typeValues) {
                if (entry.first == generic) {
                    return entry.second;
                }
            }
            auto fresh = ivars.new_ivar_tr();
            typeValues.push_back({generic, fresh});
            return fresh;
        }

        ::HIR::ConstGeneric get_value(
            const Span&, const ::HIR::GenericRef& generic
        ) const override {
            if (!generic.is_placeholder() || is_goal_placeholder(generic)) {
                return ::HIR::ConstGeneric(generic);
            }
            for (const auto& entry : values) {
                if (entry.first == generic) {
                    return entry.second.clone();
                }
            }
            auto fresh = ::HIR::ConstGeneric::make_Infer({ivars.new_ivar_val()});
            values.push_back({generic, fresh.clone()});
            return fresh;
        }

        ::HIR::LifetimeRef get_lifetime(
            const Span&, const ::HIR::GenericRef& generic
        ) const override {
            return ::HIR::LifetimeRef(generic.binding);
        }
    };

    struct MatchHrls: public HIR::MatchGenerics, public Monomorphiser {
        ::HIR::PathParams hrls;

        MatchHrls(HIR::TypeInterner& types, const ::HIR::GenericParams* x)
            : MatchHrls(types, x ? *x : empty_params)
        {
        }

        MatchHrls(HIR::TypeInterner& types, const ::HIR::GenericParams& x)
            : Monomorphiser(types)
            , hrls(x.make_empty_params(true))
        {
        }

        virtual ::HIR::Compare match_ty(const ::HIR::GenericRef& g, const ::HIR::TypeData* ty, HIR::t_cb_resolve_type resolve_cb) {
            return (ty->is_Generic() && ty->as_Generic().binding == g.binding) ? ::HIR::Compare::Equal : ::HIR::Compare::Unequal;
        }

        virtual ::HIR::Compare match_val(const ::HIR::GenericRef& g, const ::HIR::ConstGeneric& sz) {
            return sz == g ? ::HIR::Compare::Equal : ::HIR::Compare::Unequal;
        }

        virtual ::HIR::Compare match_lft(const ::HIR::GenericRef& g, const ::HIR::LifetimeRef& lft) {
            if (!::HIR::MatchGenerics::has_hrb() && g.group() == ::HIR::GENERICHrtb) {
                ASSERT_BUG(Span(), g.idx() < hrls.mLifetimes.size(), "HRL index out of range");
                hrls.mLifetimes.at(g.idx()) = lft;
                return ::HIR::Compare::Equal;
            }
            return lft.binding == g.binding ? ::HIR::Compare::Equal : ::HIR::Compare::Unequal;
        }

        // Monomorphiser
        ::HIR::TypeRef get_type(const Span& sp, const ::HIR::GenericRef& g) const {
            return types.generic(g.name, g.binding);
        }

        ::HIR::ConstGeneric get_value(const Span& sp, const ::HIR::GenericRef& g) const {
            return g;
        }

        ::HIR::LifetimeRef get_lifetime(const Span& sp, const ::HIR::GenericRef& g) const {
            if (g.group() == ::HIR::GENERICHrtb) {
                return hrls.mLifetimes.at(g.idx());
            }
            return ::HIR::LifetimeRef(g.binding);
        }
    };

    HIR::PathParams get_hrls(HIR::TypeInterner& types, const Span& sp, const ::HIR::GenericParams* x, const ::HIR::PathParams& trait_pps, const ::HIR::PathParams& des_pps) {
        MatchHrls m{types, x};
        trait_pps.match_test_generics_fuzz(sp, des_pps, HIR::ResolvePlaceholdersNop(), m);
        return std::move(m.hrls);
    }

    HIR::PathParams get_hrls(HIR::TypeInterner& types, const Span& sp, const ::HIR::GenericParams& x, const ::HIR::PathParams& trait_pps, const ::HIR::PathParams& des_pps) {
        return get_hrls(types, sp, &x, trait_pps, des_pps);
    }

    HIR::PathParams get_hrls(HIR::TypeInterner& types, const Span& sp, const ::std::unique_ptr<::HIR::GenericParams>& x, const ::HIR::PathParams& trait_pps, const ::HIR::PathParams& des_pps) {
        return get_hrls(types, sp, x.get(), trait_pps, des_pps);
    }
}

// --------------------------------------------------------------------
// HMTypeInferrence
// --------------------------------------------------------------------
void HMTypeInferrence::dump() const {
    for (const auto& v : ivars) {
        auto i = &v - &ivars.front();
        if (v.is_alias()) {
            //DEBUG("#" << i << " = " << v.alias);
        } else {
            DEBUG("#" << i << " = " << v.type << FMT_CB(os, bool open = false; unsigned int i2 = 0; for (const auto& v2 : ivars) {
                      if (v2.is_alias() && v2.alias == i) {
                          if (!open) {
                              os << " { ";
                          }
                          open = true;
                          os << "#" << i2 << " ";
                      }
                      i2++;
                  } if (open) os << "}";));
        }
    }
    for (const auto& v : values) {
        auto i = &v - &values.front();
        if (v.is_alias()) {
        } else {
            DEBUG("V#" << i << " = " << *v.val << FMT_CB(os, bool open = false; for (const auto& v2 : values) {
                      auto i2 = &v2 - &values.front();
                      if (v2.is_alias() && v2.alias == i) {
                          if (!open) {
                              os << " { ";
                          }
                          open = true;
                          os << "#" << i2 << " ";
                      }
                  } if (open) os << "}";));
        }
    }
}

void HMTypeInferrence::checkForLoops() {
#if 1
    struct LoopChecker {
        ::std::vector<unsigned int> indexes;

        void checkTy(const HMTypeInferrence& ivars, const ::HIR::TypeData* ty) {
            visit_ty_with(ty, [&](const HIR::TypeData* t) {
                if (const auto* ep = t->opt_Infer()) {
                    const auto& e = *ep;
                    for (auto idx : indexes) {
                        ASSERT_BUG(Span(), e.index != idx, "Recursion in ivar #" << indexes.front() << " " << ivars.ivars[indexes.front()].type << " - loop with " << idx << " " << ivars.ivars[idx].type);
                    }
                    const auto& ivd = ivars.get_pointed_ivar(e.index);
                    assert(!ivd.is_alias());
                    if (!ivd.type->is_Infer()) {
                        indexes.push_back(e.index);
                        this->checkTy(ivars, ivd.type);
                        indexes.pop_back();
                    }
                }
                return false;
            });
        }
    };
#else
    struct LoopChecker {
        ::std::vector<unsigned int> indexes;

        void checkPathparams(const HMTypeInferrence& ivars, const ::HIR::PathParams& pp) {
            for (const auto& ty : pp.types) {
                this->checkTy(ivars, ty);
            }
        }

        void checkPath(const HMTypeInferrence& ivars, const ::HIR::Path& path) {
            TU_MATCH(::HIR::Path::Data, (path.mData), (pe), (Generic, this->checkPathparams(ivars, pe.mParams);), (UfcsKnown, this->checkTy(ivars, pe.type); this->checkPathparams(ivars, pe.trait.mParams); this->checkPathparams(ivars, pe.params);), (UfcsInherent, this->checkTy(ivars, pe.type); this->checkPathparams(ivars, pe.params);), (UfcsUnknown, BUG(Span(), "UfcsUnknown");))
        }

        void checkTy(const HMTypeInferrence& ivars, const ::HIR::TypeData* ty) {
            TU_MATCH_HDRA( (*ty), {)
            TU_ARMA(Infer, e) {
                    for (auto idx : indexes) {
                        ASSERT_BUG(Span(), e.index != idx, "Recursion in ivar #" << indexes.front() << " " << *ivars.ivars[indexes.front()].type << " - loop with " << idx << " " << *ivars.ivars[idx].type);
                    }
                    const auto& ivd = ivars.get_pointed_ivar(e.index);
                    assert(!ivd.is_alias());
                    if (!ivd.type->data().is_Infer()) {
                        indexes.push_back(e.index);
                        this->checkTy(ivars, *ivd.type);
                        indexes.pop_back();
                    }
                }
                TU_ARMA(Primitive, e) {
                }
                TU_ARMA(Diverge, e) {
                }
                TU_ARMA(Generic, e) {
                }
                TU_ARMA(Path, e) {
                    this->checkPath(ivars, e.path);
                }
                TU_ARMA(Borrow, e) {
                    this->checkTy(ivars, e.inner);
                }
                TU_ARMA(Pointer, e) {
                    this->checkTy(ivars, e.inner);
                }
                TU_ARMA(Slice, e) {
                    this->checkTy(ivars, e.inner);
                }
                TU_ARMA(Array, e) {
                    this->checkTy(ivars, e.inner);
                }
                TU_ARMA(Closure, e) {
                }
                TU_ARMA(Generator, e) {
                }
                TU_ARMA(Function, e) {
                    for (const auto& arg : e.argTypes) {
                        this->checkTy(ivars, arg);
                    }
                    this->checkTy(ivars, e.mRettype);
                }
                TU_ARMA(TraitObject, e) {
                    this->checkPathparams(ivars, e.mTrait.mPath.mParams);
                    for (const auto& aty : e.mTrait.typeBounds) {
                        this->checkTy(ivars, aty.second.type);
                    }
                    for (const auto& marker : e.markers) {
                        this->checkPathparams(ivars, marker.mParams);
                    }
                }
                TU_ARMA(ErasedType, e) {
                    this->checkPath(ivars, e.origin);
                    for (const auto& trait : e.traits) {
                        this->checkPathparams(ivars, trait.mPath.mParams);
                        for (const auto& aty : trait.typeBounds) {
                            this->checkTy(ivars, aty.second.type);
                        }
                    }
                }
                TU_ARMA(Tuple, e) {
                    for (const auto& st : e) {
                        this->checkTy(ivars, st);
                    }
                }
            }
        }
    };
#endif
    unsigned int i = 0;
    for (const auto& v : ivars) {
        if (!v.is_alias() && !v.type->is_Infer()) {
            DEBUG("- " << i << " " << v.type);
            (LoopChecker{{i}}).checkTy(*this, v.type);
        }
        i++;
    }
}

void HMTypeInferrence::compactIvars() {
    this->checkForLoops();

    unsigned int i = 0;
    for (auto& v : ivars) {
        if (!v.is_alias()) {
            auto old = v.type;
            this->expand_ivars(v.type);
            DEBUG("- " << i << " " << old << " -> " << v.type);
        } else {
            auto index = v.alias;
            unsigned int count = 0;
            assert(index < ivars.size());
            while (ivars.at(index).is_alias()) {
                index = ivars.at(index).alias;

                if (count >= ivars.size()) {
                    this->dump();
                    BUG(Span(), "Loop detected in ivar list when starting at " << v.alias << ", current is " << index);
                }
                count++;
            }
            v.alias = index;
        }
        i++;
    }
}

bool HMTypeInferrence::applyDefaults() {
    bool rv = false;
    for (auto& v : ivars) {
        if (!v.is_alias()) {
            if (const auto* e = v.type->opt_Infer()) {
                switch (e->ty_class) {
                    case ::HIR::InferClass::None:
                        break;
                    case ::HIR::InferClass::Integer:
                        rv = true;
                        DEBUG("- IVar " << e->index << " = i32");
                        v.type = types.primitive(::HIR::CoreType::I32);
                        break;
                    case ::HIR::InferClass::Float:
                        rv = true;
                        DEBUG("- IVar " << e->index << " = f64");
                        v.type = types.primitive(::HIR::CoreType::F64);
                        break;
                }
            }
        }
    }
    return rv;
}

void HMTypeInferrence::print_type(::std::ostream& os, const ::HIR::TypeData* tr, LList<const ::HIR::TypeData*> outer_stack) const {
    const auto& ty = this->get_type(tr);
    for (const auto* pty : outer_stack) {
        if (pty) {
            if (pty == ty) {
                os << "/*RECURSE*/";
                return;
            }
        }
    }
    auto stack = LList<const ::HIR::TypeData*>(&outer_stack, ty);

    auto print_traitpath = [&](const HIR::TraitPath& tp) {
        if (tp.hrtbs && !tp.hrtbs->is_empty()) {
            os << "for" << tp.hrtbs->fmt_args() << " ";
        }
        this->print_genericpath(os, tp.mPath, stack);
        // TODO: ATYs?
    };
    auto print_path = [&](const HIR::Path& path) {
        TU_MATCH_HDRA( (path.mData), {)
        TU_ARMA(Generic, pe) {
                this->print_genericpath(os, pe, stack);
            }
            TU_ARMA(UfcsKnown, pe) {
                os << "<";
                this->print_type(os, pe.type, stack);
                os << " as ";
                this->print_genericpath(os, pe.trait, stack);
                os << ">::" << pe.item;
                this->print_pathparams(os, pe.params, stack);
            }
            TU_ARMA(UfcsInherent, pe) {
                os << "<";
                this->print_type(os, pe.type, stack);
                os << ">::" << pe.item;
                this->print_pathparams(os, pe.params, stack);
            }
            TU_ARMA(UfcsUnknown, pe) {
                BUG(Span(), "UfcsUnknown");
            }
        }
    };

    TU_MATCH_HDRA( (*ty), {)
    TU_ARMA(Infer, e) {
            os << ty;
        }
        TU_ARMA(Primitive, e) {
            os << ty;
        }
        TU_ARMA(Diverge, e) {
            os << ty;
        }
        TU_ARMA(Generic, e) {
            os << ty;
        }
        TU_ARMA(Path, e) {
            print_path(e.path);
        }
        TU_ARMA(Borrow, e) {
            os << "&";
            if (e.lifetime != ::HIR::LifetimeRef()) {
                os << e.lifetime << " ";
            }
            switch (e.type) {
                case ::HIR::BorrowType::Shared:
                    os << "";
                    break;
                case ::HIR::BorrowType::Unique:
                    os << "mut ";
                    break;
                case ::HIR::BorrowType::Owned:
                    os << "move ";
                    break;
            }
            this->print_type(os, e.inner, stack);
        }
        TU_ARMA(Pointer, e) {
            switch (e.type) {
                case ::HIR::BorrowType::Shared:
                    os << "*const ";
                    break;
                case ::HIR::BorrowType::Unique:
                    os << "*mut ";
                    break;
                case ::HIR::BorrowType::Owned:
                    os << "*move ";
                    break;
            }
            this->print_type(os, e.inner, stack);
        }
        TU_ARMA(Slice, e) {
            os << "[";
            this->print_type(os, e.inner, stack);
            os << "]";
        }
        TU_ARMA(Array, e) {
            os << "[";
            this->print_type(os, e.inner, stack);
            os << "; " << e.size << "]";
        }
        TU_ARMA(NodeType, e) {
            e.fmt(os);
        TU_MATCH_HDRA((e), {)
        TU_ARMA(Closure, node_p) {
                    os << "(";
                    for (const auto& arg : node_p->mArgs) {
                        this->print_type(os, arg.second, stack);
                        os << ",";
                    }
                    os << ")->";
                    this->print_type(os, node_p->returnType, stack);
                }
                TU_ARMA(Generator, node_p) {
                }
                TU_ARMA(Async, node_p) {
                }
        }
        }
        TU_ARMA(NamedFunction, e) {
            os << "fn{";
            print_path(e.path);
            os << "}";
        }
        TU_ARMA(Function, e) {
            if (e.is_unsafe) {
                os << "unsafe ";
            }
            if (e.mAbi != "") {
                os << "extern \"" << e.mAbi << "\" ";
            }
            os << "fn(";
            for (const auto& arg : e.argTypes) {
                this->print_type(os, arg, stack);
                os << ",";
            }
            os << ")->";
            this->print_type(os, e.mRettype, stack);
        }
        TU_ARMA(TraitObject, e) {
            os << "dyn (";
            print_traitpath(e.mTrait);
            for (const auto& marker : e.markers) {
                os << "+";
                this->print_genericpath(os, marker, stack);
            }
            if (e.lifetime != ::HIR::LifetimeRef::new_static()) {
                os << "+" << e.lifetime;
            }
            os << ")";
        }
        TU_ARMA(ErasedType, e) {
            os << "impl ";
            for (const auto& tr : e.traits) {
                if (&tr != &e.traits[0]) {
                    os << "+";
                }
                print_traitpath(tr);
            }
            if (!e.lifetimeBounds.empty()) {
                for (const auto& lft : e.lifetimeBounds) {
                    os << "+" << lft;
                }
            }
            os << "+use";
            this->print_pathparams(os, e.use, outer_stack);
            os << "/*";
        TU_MATCH_HDRA( (e.inner), {)
        TU_ARMA(Fcn, ee) {
                    os << "fn ";
                    print_path(ee.origin);
                    os << "#" << ee.index;
                }
                TU_ARMA(Known, ee) {
                    print_type(os, ee, stack);
                }
                TU_ARMA(Alias, ee) {
                }
        }
        os << "*/";
        }
        TU_ARMA(Tuple, e) {
            os << "(";
            for (const auto& st : e) {
                this->print_type(os, st, stack);
                os << ",";
            }
            os << ")";
        }
    }
}

void HMTypeInferrence::print_genericpath(::std::ostream& os, const ::HIR::GenericPath& gp, LList<const ::HIR::TypeData*> stack) const {
    os << gp.mPath;
    this->print_pathparams(os, gp.mParams, stack);
}

void HMTypeInferrence::print_pathparams(::std::ostream& os, const ::HIR::PathParams& pps, LList<const ::HIR::TypeData*> stack) const {
    if (pps.has_params() || !pps.mLifetimes.empty()) {
        os << "<";
        for (const auto& pp_l : pps.mLifetimes) {
            os << pp_l;
            os << ",";
        }
        for (const auto& pp_t : pps.types) {
            this->print_type(os, pp_t, stack);
            os << ",";
        }
        for (const auto& pp_v : pps.values) {
            os << pp_v;
            os << ",";
        }
        os << ">";
    }
}

void HMTypeInferrence::expand_ivars(::HIR::TypeRef& type) {
    if (!type->has_type_infer()) {
        return;
    }
    if (::std::find(expandStack.begin(), expandStack.end(), type) != expandStack.end()) return;
    expandStack.push_back(type);
    struct Guard {
        ::std::vector<HIR::TypeRef>& stack;
        ~Guard() { stack.pop_back(); }
    } guard{expandStack};

    if (type->is_Infer()) {
        const auto& resolved = this->get_type(type);
        if (resolved != type) type = resolved;
        return;
    }

    auto data = type->cloneData();

    struct H {
        static void expand_ivars_path(/*const*/ HMTypeInferrence& self, ::HIR::Path& path) {
            TU_MATCH(::HIR::Path::Data, (path.mData), (e2), (Generic, self.expand_ivars_params(e2.mParams);), (UfcsKnown, self.expand_ivars(e2.type); self.expand_ivars_params(e2.trait.mParams); self.expand_ivars_params(e2.params);), (UfcsUnknown, self.expand_ivars(e2.type); self.expand_ivars_params(e2.params);), (UfcsInherent, self.expand_ivars(e2.type); self.expand_ivars_params(e2.params);))
        }
    };

    TU_MATCH_HDRA( (data), {)
    TU_ARMA(Infer, e) {}
        TU_ARMA(Diverge, e) {
        }
        TU_ARMA(Primitive, e) {
        }
        TU_ARMA(Path, e) {
            // Iterate all arguments
            H::expand_ivars_path(*this, e.path);
        }
        TU_ARMA(Generic, e) {
        }
        TU_ARMA(TraitObject, e) {
            this->expand_ivars_params(e.mTrait.mPath.mParams);
            for (auto& marker : e.markers) {
                this->expand_ivars_params(marker.mParams);
            }
            // TODO: Associated types
        }
        TU_ARMA(ErasedType, e) {
        TU_MATCH_HDRA( (e.inner), {)
        TU_ARMA(Fcn, ee) {
                    H::expand_ivars_path(*this, ee.origin);
                }
                TU_ARMA(Known, ee) {
                    this->expand_ivars(ee);
                }
                TU_ARMA(Alias, ee) {
                }
        }
        for(auto& trait : e.traits)
        {
                this->expand_ivars_params(trait.mPath.mParams);
                // TODO: Associated types
        }
        }
        TU_ARMA(Array, e) {
            this->expand_ivars(e.inner);
        }
        TU_ARMA(Slice, e) {
            this->expand_ivars(e.inner);
        }
        TU_ARMA(Tuple, e) {
            for (auto& ty : e) {
                this->expand_ivars(ty);
            }
        }
        TU_ARMA(Borrow, e) {
            this->expand_ivars(e.inner);
        }
        TU_ARMA(Pointer, e) {
            this->expand_ivars(e.inner);
        }
        TU_ARMA(NamedFunction, e) {
            H::expand_ivars_path(*this, e.path);
        }
        TU_ARMA(Function, e) {
            this->expand_ivars(e.mRettype);
            for (auto& ty : e.argTypes) {
                this->expand_ivars(ty);
            }
        }
    TU_ARMA(NodeType, e) {
        }
    }
    type = types.intern(std::move(data));
}

void HMTypeInferrence::expand_ivars_params(::HIR::PathParams& params) {
    for (auto& arg : params.types) {
        expand_ivars(arg);
    }
}

void HMTypeInferrence::addIvars(::HIR::TypeRef& type) {
    if (type->is_Infer() && type->as_Infer().index == ~0u) {
        type = new_ivar_tr(type->as_Infer().ty_class);
        this->mark_change();
        DEBUG("New ivar " << type);
        return;
    }

    auto data = type->cloneData();
    TU_MATCH_HDRA( (data), {)
    TU_ARMA(Infer, e) {
        }
        TU_ARMA(Diverge, e) {
        }
        TU_ARMA(Primitive, e) {
        }
        TU_ARMA(Path, e) {
            // Iterate all arguments
            TU_MATCH(::HIR::Path::Data, (e.path.mData), (e2), (Generic, this->addIvarsParams(e2.mParams);), (UfcsKnown, this->addIvars(e2.type); this->addIvarsParams(e2.trait.mParams); this->addIvarsParams(e2.params);), (UfcsUnknown, this->addIvars(e2.type); this->addIvarsParams(e2.params);), (UfcsInherent, this->addIvars(e2.type); this->addIvarsParams(e2.params);))
        }
        TU_ARMA(Generic, e) {
        }
        TU_ARMA(TraitObject, e) {
            // Iterate all paths
            this->addIvarsParams(e.mTrait.mPath.mParams);
            for (auto& aty : e.mTrait.typeBounds) {
                this->addIvars(aty.second.type);
            }
            for (auto& marker : e.markers) {
                this->addIvarsParams(marker.mParams);
            }
        }
        TU_ARMA(ErasedType, e) {
            if (type_contains_ivars(type, /*only_unbound=*/true)) {
                BUG(Span(), "ErasedType getting ivars added - " << type);
            }
        }
        TU_ARMA(Array, e) {
            addIvars(e.inner);
            if (e.size.is_Unevaluated()) {
                addIvars(e.size.as_Unevaluated());
            }
        }
        TU_ARMA(Slice, e) {
            addIvars(e.inner);
        }
        TU_ARMA(Tuple, e) {
            for (auto& ty : e) {
                addIvars(ty);
            }
        }
        TU_ARMA(Borrow, e) {
            addIvars(e.inner);
        }
        TU_ARMA(Pointer, e) {
            addIvars(e.inner);
        }
        TU_ARMA(NamedFunction, e) {
            // Shouldn't be possible?
            // Even if it is seen, it shouldn't have any empty ivars
        }
        TU_ARMA(Function, e) {
            addIvars(e.mRettype);
            for (auto& ty : e.argTypes) {
                addIvars(ty);
            }
        }
    TU_ARMA(NodeType, e) {
            // Shouldn't be possible
        }
    }
    type = types.intern(std::move(data));
}

void HMTypeInferrence::addIvars(::HIR::ConstGeneric& val) {
    if (val.is_Infer()) {
        if (val.as_Infer().index == ~0u) {
            val.as_Infer().index = new_ivar_val();
            this->mark_change();
            DEBUG("New ivar " << val);
        }
    }
}

void HMTypeInferrence::addIvarsParams(::HIR::PathParams& params) {
    for (auto& arg : params.types) {
        addIvars(arg);
    }
    for (auto& arg : params.values) {
        addIvars(arg);
    }
}

unsigned int HMTypeInferrence::new_ivar(HIR::InferClass ic /* = HIR::InferClass::None*/) {
    auto rv = ivars.size();
    ivars.emplace_back(types.infer(rv, ic));
    DEBUG("New type IVar " << rv);
    return rv;
}

::HIR::TypeRef HMTypeInferrence::new_ivar_tr(HIR::InferClass ic /* = HIR::InferClass::None*/) {
    return ivars.at(this->new_ivar(ic)).type;
}

unsigned int HMTypeInferrence::new_ivar_val() {
    values.push_back(IVarValue());
    values.back().val->as_Infer().index = values.size() - 1;
    return values.size() - 1;
}

void HMTypeInferrence::set_ivar_val_to(unsigned int slot, ::HIR::ConstGeneric val) {
    ASSERT_BUG(Span(), slot < values.size(), "slot " << slot << " >= " << values.size());
    ASSERT_BUG(Span(), !values[slot].is_alias(), "slot " << slot);
    if (*values[slot].val == val) {
        //DEBUG("Set ValIVar " << slot << " == " << val);
    } else {
        DEBUG("Set ValIVar " << slot << " = " << val);
        ASSERT_BUG(Span(), values[slot].val->is_Infer(), "slot " << slot << " - " << *values[slot].val);
        ASSERT_BUG(Span(), values[slot].val->as_Infer().index == slot, "slot " << slot << " - " << *values[slot].val);
        *values[slot].val = std::move(val);
    }
}

void HMTypeInferrence::ivar_val_unify(unsigned int left_slot, unsigned int right_slot) {
    Span sp;
    ASSERT_BUG(sp, left_slot < values.size(), "slot " << left_slot << " >= " << values.size());
    ASSERT_BUG(sp, right_slot < values.size(), "slot " << left_slot << " >= " << values.size());
    ASSERT_BUG(sp, !values[left_slot].is_alias(), "slot " << left_slot);
    ASSERT_BUG(sp, !values[right_slot].is_alias(), "slot " << right_slot);

    if (/*const auto* re =*/values[right_slot].val->opt_Infer()) {
        DEBUG("Set ValIVar " << right_slot << " = @" << left_slot);
        values[right_slot].alias = left_slot;
        values[right_slot].val.reset();

        this->mark_change();
    } else {
        BUG(sp, "Unifiying over a set value");
    }
}

//::HIR::TypeRef& HMTypeInferrence::get_type(::HIR::TypeRef& type)
//{
//    if(const auto* e = type->opt_Infer()) {
//        assert(e->index != ~0u);
//        return *get_pointed_ivar(e->index).type;
//    }
//    else {
//        return type;
//    }
//}

const ::HIR::TypeData* HMTypeInferrence::get_type(const ::HIR::TypeData* type) const {
    const auto* current = &type;
    for (size_t count = 0; count <= ivars.size(); count++) {
        const auto* e = (*current)->opt_Infer();
        if (!e) {
            return *current;
        }
        ASSERT_BUG(Span(), e->index != ~0u, "Encountered non-populated IVar");

        const auto* next = &get_pointed_ivar(e->index).type;
        if (*next == *current) {
            return *current;
        }
        current = next;
    }
    this->dump();
    BUG(Span(), "Loop detected while resolving type " << type);
}

::HIR::TypeRef& HMTypeInferrence::get_type(unsigned idx) {
    assert(idx != ~0u);
    auto* current = &get_pointed_ivar(idx).type;
    for (size_t count = 0; count <= ivars.size(); count++) {
        const auto* e = (*current)->opt_Infer();
        if (!e) {
            return *current;
        }
        auto* next = &get_pointed_ivar(e->index).type;
        if (*next == *current) {
            return *current;
        }
        current = next;
    }
    this->dump();
    BUG(Span(), "Loop detected while resolving type ivar " << idx);
}

const ::HIR::TypeData* HMTypeInferrence::get_type(unsigned idx) const {
    assert(idx != ~0u);
    const auto* current = &get_pointed_ivar(idx).type;
    for (size_t count = 0; count <= ivars.size(); count++) {
        const auto* e = (*current)->opt_Infer();
        if (!e) {
            return *current;
        }
        const auto* next = &get_pointed_ivar(e->index).type;
        if (*next == *current) {
            return *current;
        }
        current = next;
    }
    this->dump();
    BUG(Span(), "Loop detected while resolving type ivar " << idx);
}

void HMTypeInferrence::set_ivar_to(unsigned int slot, ::HIR::TypeRef type) {
    auto sp = Span();
    auto& root_ivar = this->get_pointed_ivar(slot);
    DEBUG("set_ivar_to(" << slot << " { " << root_ivar.type << " }, " << type << ")");

    // If the left type was '_', alias the right to it
    if (const auto* l_e = type->opt_Infer()) {
        assert(l_e->index != slot);
        if (l_e->ty_class != ::HIR::InferClass::None) {
            TU_MATCH_DEF(
                ::HIR::TypeData,
                ((*root_ivar.type)),
                (e),
                (ERROR(sp, E0000, "Type unificiation of literal with invalid type - " << root_ivar.type);),
                (Primitive, checkTypeClassPrimitive(sp, type, l_e->ty_class, e);),
                (Infer,
                 // Check for right having a ty_class
                 if (e.ty_class != ::HIR::InferClass::None && e.ty_class != l_e->ty_class) { ERROR(sp, E0000, "Unifying types with mismatching literal classes - " << type << " := " << root_ivar.type); })
            )
        }

#if 1
        // Alias `l_e.index` to this slot
        DEBUG("Set IVar " << l_e->index << " = @" << slot);
        auto& r_ivar = this->get_pointed_ivar(l_e->index);
        r_ivar.alias = slot;
        r_ivar.type = nullptr;
#else
        DEBUG("Set IVar " << slot << " = @" << l_e->index);
        root_ivar.alias = l_e->index;
        root_ivar.type = nullptr;
#endif
    } else if (root_ivar.type == type) {
        return;
    } else {
        // Erase (replace with blank) lifetimes
#if 1
        // TODO: Avoid needing to clone in all cases?
        struct MonomorphAddLifetimes: public Monomorphiser {
            explicit MonomorphAddLifetimes(HIR::TypeInterner& types): Monomorphiser(types) {}

            ::HIR::TypeRef get_type(const Span& sp, const ::HIR::GenericRef& g) const override {
                return types.generic(g.name, g.binding);
            }

            ::HIR::ConstGeneric get_value(const Span& sp, const ::HIR::GenericRef& g) const override {
                return g;
            }

            ::HIR::LifetimeRef get_lifetime(const Span& sp, const ::HIR::GenericRef& g) const override {
                return HIR::LifetimeRef();
            }

            ::HIR::LifetimeRef monomorph_lifetime(const Span& sp, const ::HIR::LifetimeRef& tpl) const override {
                if (tpl.is_param()) {
                    return Monomorphiser::monomorph_lifetime(sp, tpl);
                }
                return HIR::LifetimeRef();
            }

        };

        type = MonomorphAddLifetimes(types).monomorph_type(sp, type, true);
#else
        if (type->is_Borrow() && type->as_Borrow().lifetime != HIR::LifetimeRef()) {
            auto& t = type.get_unique();
            t.as_Borrow().lifetime = HIR::LifetimeRef();
        }
#endif

        // Otherwise, store left in right's slot
        DEBUG("Set IVar " << slot << " = " << type);
        if (const auto* e = root_ivar.type->opt_Infer()) {
            switch (e->ty_class) {
                case ::HIR::InferClass::None:
                    break;
                case ::HIR::InferClass::Integer:
                case ::HIR::InferClass::Float:
                    // `type` can't be an ivar, so it has to be a primitive (or an associated?)
                    if (const auto* l_e = type->opt_Primitive()) {
                        checkTypeClassPrimitive(sp, type, e->ty_class, *l_e);
                    } else if (type->is_Diverge()) {
                        // ... acceptable
                    } else {
                        BUG(sp, "Setting primitive to " << type);
                    }
                    break;
            }
        }
        else {
            BUG(sp, "Overwriting ivar " << slot << " (" << root_ivar.type << ") with " << type);
        }

        root_ivar.type = type;
    }

    this->mark_change();
}

void HMTypeInferrence::ivar_unify(unsigned int left_slot, unsigned int right_slot) {
    auto sp = Span();
    if (left_slot != right_slot) {
        auto& left_ivar = this->get_pointed_ivar(left_slot);

        // TODO: Assert that setting this won't cause a loop.
        auto& root_ivar = this->get_pointed_ivar(right_slot);

        if (const auto* re = root_ivar.type->opt_Infer()) {
            DEBUG("Class unify " << left_ivar.type << " <- " << root_ivar.type);

            if (re->ty_class != ::HIR::InferClass::None) {
                if (const auto* le = left_ivar.type->opt_Infer()) {
                    if (le->ty_class != ::HIR::InferClass::None && le->ty_class != re->ty_class) {
                        ERROR(sp, E0000, "Unifying types with mismatching literal classes - " << left_ivar.type << " := " << root_ivar.type);
                    }
                    if (le->ty_class == ::HIR::InferClass::None) {
                        left_ivar.type = types.infer(le->index, re->ty_class);
                    }
                } else if (const auto* le = left_ivar.type->opt_Primitive()) {
                    checkTypeClassPrimitive(sp, left_ivar.type, re->ty_class, *le);
                } else {
                    ERROR(sp, E0000, "Type unificiation of literal with invalid type - " << left_ivar.type);
                }
            } else {
            }
        } else {
            BUG(sp, "Unifying over a concrete type - " << root_ivar.type);
        }

        DEBUG("IVar " << root_ivar.type->as_Infer().index << " = @" << left_slot);
        root_ivar.alias = left_slot;
        root_ivar.type = nullptr;

        this->mark_change();
    }
}

const ::HIR::ConstGeneric& HMTypeInferrence::get_value(const ::HIR::ConstGeneric& val) const {
    if (val.is_Infer()) {
        return get_value(val.as_Infer().index);
    } else {
        return val;
    }
}

const ::HIR::ConstGeneric& HMTypeInferrence::get_value(unsigned slot) const {
    ASSERT_BUG(Span(), slot != ~0u, "HMTypeInferrence::get_value: Value generic ivar index not assigned");
    auto index = slot;
    // Limit the iteration count to the number of ivars
    for (unsigned int count = 0; count < values.size(); count++) {
        ASSERT_BUG(Span(), index < values.size(), "");
        auto& ent = values[index];
        if (!ent.is_alias()) {
            return *ent.val;
        }
        index = ent.alias;
    }
    this->dump();
    BUG(Span(), "Loop detected in ivar list when starting at " << slot << ", current is " << index);
}

HMTypeInferrence::IVar& HMTypeInferrence::get_pointed_ivar(unsigned int slot) const {
    auto index = slot;
    unsigned int count = 0;
    assert(index < ivars.size());
    while (ivars.at(index).is_alias()) {
        index = ivars.at(index).alias;

        if (count >= ivars.size()) {
            this->dump();
            BUG(Span(), "Loop detected in ivar list when starting at " << slot << ", current is " << index);
        }
        count++;
    }
    return const_cast<IVar&>(ivars.at(index));
}

bool HMTypeInferrence::pathparams_contain_ivars(const ::HIR::PathParams& pps, bool only_unbound) const {
    for (const auto& ty : pps.types) {
        if (this->type_contains_ivars(ty, only_unbound)) {
            return true;
        }
    }
    return false;
}

bool HMTypeInferrence::type_contains_ivars(const ::HIR::TypeData* ty, bool only_unbound) const {
    if (!ty->has_type_infer()) {
        return false;
    }
    TRACE_FUNCTION_F("ty = " << ty);
    auto path_contains_ivars = [this](const HIR::Path& path, bool only_unbound) {
        TU_MATCH(::HIR::Path::Data, (path.mData), (pe), (Generic, return this->pathparams_contain_ivars(pe.mParams, only_unbound);), (UfcsKnown, if (this->type_contains_ivars(pe.type, only_unbound)) return true; if (this->pathparams_contain_ivars(pe.trait.mParams, only_unbound)) return true; return this->pathparams_contain_ivars(pe.params, only_unbound);), (UfcsInherent, if (this->type_contains_ivars(pe.type, only_unbound)) return true; return this->pathparams_contain_ivars(pe.params, only_unbound);), (UfcsUnknown, BUG(Span(), "UfcsUnknown");))
        throw "";
    };
    //TU_MATCH(::HIR::TypeData, (this->get_type(ty).m_data), (e),
    TU_MATCH(::HIR::TypeData, (*ty), (e),
    (Infer,
        if( only_unbound ) {
        return e.index == ~0u;
        }
        return true;
        ),
    (Primitive, return false; ),
    (Diverge, return false; ),
    (Generic, return false; ),
    (Path,
        return path_contains_ivars(e.path, only_unbound);
        ),
    (Borrow,
        return type_contains_ivars(e.inner, only_unbound);
        ),
    (Pointer,
        return type_contains_ivars(e.inner, only_unbound);
        ),
    (Slice,
        return type_contains_ivars(e.inner, only_unbound);
        ),
    (Array,
        return type_contains_ivars(e.inner, only_unbound);
        ),
    (NodeType,
        return false;
        ),
    (NamedFunction,
        return path_contains_ivars(e.path, only_unbound);
        ),
    (Function,
        for(const auto& arg : e.argTypes)
            if( type_contains_ivars(arg, only_unbound) )
                return true;
        return type_contains_ivars(e.mRettype, only_unbound);
        ),
    (TraitObject,
        for(const auto& marker : e.markers)
            if( pathparams_contain_ivars(marker.mParams, only_unbound) )
                return true;
        return pathparams_contain_ivars(e.mTrait.mPath.mParams, only_unbound);
        ),
    (ErasedType,
        TU_MATCH_HDRA( (e.inner), {)
        TU_ARMA(Fcn, ee) {
            return path_contains_ivars(ee.origin, only_unbound);
}

TU_ARMA(Known, ee) {
    return type_contains_ivars(ee, only_unbound);
}

TU_ARMA(Alias, ee) {
    return false;
}
}
        ),
    (Tuple,
        for(const auto& st : e)
            if( type_contains_ivars(st, only_unbound) )
                return true;
        return false;
        )
    )
    throw "";
        }

        namespace {
            bool type_list_equal(const HMTypeInferrence& context, const ::std::vector<::HIR::TypeRef>& l, const ::std::vector<::HIR::TypeRef>& r) {
                if (l.size() != r.size()) {
                    return false;
                }

                for (unsigned int i = 0; i < l.size(); i++) {
                    if (!context.types_equal(l[i], r[i])) {
                        return false;
                    }
                }
                return true;
            }

            bool type_list_equal(const HMTypeInferrence& context, const ThinVector<::HIR::TypeRef>& l, const ThinVector<::HIR::TypeRef>& r) {
                if (l.size() != r.size()) {
                    return false;
                }

                for (unsigned int i = 0; i < l.size(); i++) {
                    if (!context.types_equal(l[i], r[i])) {
                        return false;
                    }
                }
                return true;
            }
        }

        bool HMTypeInferrence::pathparams_equal(const ::HIR::PathParams& pps_l, const ::HIR::PathParams& pps_r) const {
            return type_list_equal(*this, pps_l.types, pps_r.types);
        }

        bool HMTypeInferrence::types_equal(const ::HIR::TypeData* rl, const ::HIR::TypeData* rr) const {
            const auto& l = this->get_type(rl);
            const auto& r = this->get_type(rr);
            if (l->tag() != r->tag()) {
                return false;
            }

            struct H {
                static bool comparePath(const HMTypeInferrence& self, const ::HIR::Path& l, const ::HIR::Path& r) {
                    if (l.mData.tag() != r.mData.tag()) {
                        return false;
                    }
                    TU_MATCH(::HIR::Path::Data, (l.mData, r.mData), (lpe, rpe), (Generic, if (lpe.mPath != rpe.mPath) return false; return self.pathparams_equal(lpe.mParams, rpe.mParams);), (UfcsKnown, if (lpe.item != rpe.item) return false; if (!self.types_equal(lpe.type, rpe.type)) return false; if (!self.pathparams_equal(lpe.trait.mParams, rpe.trait.mParams)) return false; return self.pathparams_equal(lpe.params, rpe.params);), (UfcsInherent, if (lpe.item != rpe.item) return false; if (!self.types_equal(lpe.type, rpe.type)) return false; return self.pathparams_equal(lpe.params, rpe.params);), (UfcsUnknown, BUG(Span(), "UfcsUnknown");))
                    throw "";
                }
            };

    TU_MATCH(::HIR::TypeData, (*l, *r), (le, re),
    (Infer, return le.index == re.index; ),
    (Primitive, return le == re; ),
    (Diverge, return true; ),
    (Generic, return le.binding == re.binding; ),
    (Path,
        return H::comparePath(*this, le.path, re.path);
        ),
    (Borrow,
        if( le.type != re.type )
            return false;
        return types_equal(le.inner, re.inner);
        ),
    (Pointer,
        if( le.type != re.type )
            return false;
        return types_equal(le.inner, re.inner);
        ),
    (Slice,
        return types_equal(le.inner, re.inner);
        ),
    (Array,
        if( le.size != re.size )
            return false;
        return types_equal(le.inner, re.inner);
        ),
    (NodeType,
        return le == re;
        ),
    (NamedFunction,
        return H::comparePath(*this, le.path, re.path);
        ),
    (Function,
        if( le.is_unsafe != re.is_unsafe || le.mAbi != re.mAbi )
            return false;
        if( !type_list_equal(*this, le.argTypes, re.argTypes) )
            return false;
        return types_equal(le.mRettype, re.mRettype);
        ),
    (TraitObject,
        if( le.markers.size() != re.markers.size() )
            return false;
        for(unsigned int i = 0; i < le.markers.size(); i ++) {
        const auto& lm = le.markers[i];
        const auto& rm = re.markers[i];
        if (lm.mPath != rm.mPath) {
            return false;
        }
        if (!pathparams_equal(lm.mParams, rm.mParams)) {
            return false;
        }
        }
        if( le.mTrait.mPath.mPath != re.mTrait.mPath.mPath )
            return false;
        return pathparams_equal(le.mTrait.mPath.mParams, re.mTrait.mPath.mParams);
        ),
    (ErasedType,
        if( le.inner.tag() != re.inner.tag() )
            return false;
        TU_MATCH_HDRA( (le.inner, re.inner), {)
        TU_ARMA(Fcn, l,r) {
            ASSERT_BUG(Span(), l.origin != ::HIR::SimplePath(), "Erased type with unset origin");
            ASSERT_BUG(Span(), r.origin != ::HIR::SimplePath(), "Erased type with unset origin");
            return H::comparePath(*this, l.origin, r.origin);
        }

        TU_ARMA(Known, l, r) {
            return types_equal(l, r);
        }

        TU_ARMA(Alias, l, r) {
            if (l.inner.get() != r.inner.get()) { // Pointer comparison
                return false;
            }
            return pathparams_equal(l.params, r.params);
        }
        }
        ),
    (Tuple,
        return type_list_equal(*this, le, re);
        )
    )
    throw "";
        }

        // --------------------------------------------------------------------
        // TraitResolution
        // --------------------------------------------------------------------

        namespace {
            ::HIR::Compare compareValue(const Span& sp, const ::HIR::ConstGeneric& left_raw, const ::HIR::ConstGeneric& right_raw, const HMTypeInferrence& infer) {
                const auto& left = left_raw.is_Infer() ? infer.get_value(left_raw.as_Infer().index) : left_raw;
                const auto& right = right_raw.is_Infer() ? infer.get_value(right_raw.as_Infer().index) : right_raw;
                if (left == right) {
                    return ::HIR::Compare::Equal;
                }
                if (left.is_Infer() || right.is_Infer()) {
                    return ::HIR::Compare::Fuzzy;
                }
                if (left.is_Generic() && left.as_Generic().is_placeholder()) {
                    return ::HIR::Compare::Fuzzy;
                }
                if (right.is_Generic() && right.as_Generic().is_placeholder()) {
                    return ::HIR::Compare::Fuzzy;
                }
                //TODO(sp, "compare_value: " << left << " == " << right);
                return ::HIR::Compare::Unequal;
            }
        }

        ::HIR::Compare TraitResolution::comparePp(const Span& sp, const ::HIR::PathParams& left, const ::HIR::PathParams& right) const {
            ASSERT_BUG(sp, left.types.size() == right.types.size(), "Parameter count mismatch - `" << left << "` vs `" << right << "`");
            ASSERT_BUG(sp, left.values.size() == right.values.size(), "Parameter count mismatch - `" << left << "` vs `" << right << "`");
            ::HIR::Compare ord = ::HIR::Compare::Equal;
            for (unsigned int i = 0; i < left.types.size(); i++) {
                // TODO: Should allow fuzzy matches using placeholders (match_test_generics_fuzz works for that)
                // - Better solution is to remove the placeholders in method searching.
                ord &= left.types[i]->compareWithPlaceholders(sp, right.types[i], this->ivars.callbackResolveInfer());
                if (ord == ::HIR::Compare::Unequal) {
                    return ord;
                }
            }
            for (unsigned int i = 0; i < left.values.size(); i++) {
                ord &= compareValue(sp, left.values[i], right.values[i], this->ivars);
                if (ord == ::HIR::Compare::Unequal) {
                    return ord;
                }
            }
            return ord;
        }

        // -------------------------------------------------------------------------------------------------------------------
        //
        // -------------------------------------------------------------------------------------------------------------------
        bool TraitResolution::iterate_bounds_traits(const Span& sp, const HIR::TypeData* type, const HIR::SimplePath& trait, t_cb_bound cb) const {
            return iterate_bounds_traits(sp, type, [&](HIR::Compare cmp, const HIR::TypeData* t, const HIR::GenericPath& tr, const CachedBound& b) {
                if (tr.mPath != trait) {
                    return false;
                }
                return cb(cmp, t, tr, b);
            });
        }

        bool TraitResolution::iterate_bounds_traits(const Span& sp, const HIR::TypeData* type, t_cb_bound cb) const {
            for (const auto& b : traitBounds) {
                auto cmp = b.first.first->compareWithPlaceholders(sp, type, this->ivars.callbackResolveInfer());
                if (cmp == HIR::Compare::Unequal) {
                    continue;
                }
                if (cb(cmp, b.first.first, b.first.second, b.second)) {
                    return true;
                }
            }
            return false;
        }

        bool TraitResolution::iterate_bounds_traits(const Span& sp, t_cb_bound cb) const {
            for (const auto& b : traitBounds) {
                if (cb(HIR::Compare::Equal, b.first.first, b.first.second, b.second)) {
                    return true;
                }
            }
            return false;
        }

        bool TraitResolution::iterate_aty_bounds(const Span& sp, const ::HIR::Path::Data::Data_UfcsKnown& pe, ::std::function<bool(const ::HIR::TraitPath&)> cb) const {
            ::HIR::GenericPath trait_path;
            DEBUG("Checking ATY bounds on " << pe.trait << " :: " << pe.item);
            if (!this->trait_contains_type(sp, pe.trait, this->crate.get_trait_by_path(sp, pe.trait.mPath), pe.item.c_str(), trait_path)) {
                BUG(sp, "Cannot find associated type " << pe.item << " anywhere in trait " << pe.trait);
            }
            DEBUG("trait_path=" << trait_path);
            const auto& trait_ref = crate.get_trait_by_path(sp, trait_path.mPath);
            const auto& atyDef = trait_ref.types.find(pe.item)->second;

            for (const auto& bound : atyDef.traitBounds) {
                if (cb(bound)) {
                    return true;
                }
            }
            // Search `<Self as Trait>::Name` bounds on the trait itself
            for (const auto& bound : trait_ref.mParams.bounds) {
                if (!bound.is_TraitBound()) {
                    continue;
                }
                const auto& be = bound.as_TraitBound();

                if (!be.type->is_Path()) {
                    continue;
                }
                if (!be.type->as_Path().binding.is_Opaque()) {
                    continue;
                }

                const auto& beTypePe = be.type->as_Path().path.mData.as_UfcsKnown();
                if (beTypePe.type != crate.types.self()) {
                    continue;
                }
                if (beTypePe.trait.mPath != pe.trait.mPath) {
                    continue;
                }
                if (beTypePe.item != pe.item) {
                    continue;
                }

                if (cb(be.trait)) {
                    return true;
                }
            }

            return false;
        }

        bool TraitResolution::find_trait_impls_magic(const Span& sp, const ::HIR::SimplePath& trait, const ::HIR::PathParams& params, const ::HIR::TypeData* ty, t_cb_trait_impl_r callback) const {
            static ::HIR::PathParams null_params;
            static ::HIR::TraitPath::assocListT null_assoc;

            const auto langCoerceUnsized = this->crate.get_lang_item_path_opt("coerce_unsized");
            const auto langFnPtr = this->crate.get_lang_item_path_opt("fn_ptr_trait");
            const auto langTuple = this->crate.get_lang_item_path_opt("tuple_trait");

            const auto& type = this->ivars.get_type(ty);
            TRACE_FUNCTION_F("trait = " << trait << params << ", type = " << type);

            if (trait == mLangSized) {
                auto cmp = type_is_sized(sp, type);
                if (cmp != ::HIR::Compare::Unequal) {
                    return callback(ImplRef(type, &null_params, &null_assoc), cmp);
                } else {
                    return false;
                }
            }

            if (trait == mLangCopy) {
                auto cmp = this->type_is_copy(sp, type);
                if (cmp != ::HIR::Compare::Unequal) {
                    return callback(ImplRef(type, &null_params, &null_assoc), cmp);
                } else {
                    return false;
                }
            }

            if (!langFnPtr.components().empty() && trait == langFnPtr) {
                if (type->is_Function()) {
                    return callback(ImplRef(type, &null_params, &null_assoc), HIR::Compare::Equal);
                }
            }

            if (trait == mLangClone
                && (type->is_Tuple()
                    || type->is_Array()
                    || type->is_Function()
                    || type->is_NodeType()
                    || type->is_NamedFunction()
                    || TU_TEST1(*type, Path, .is_closure()))) {
                auto cmp = this->type_is_clone(sp, type);
                if (cmp != ::HIR::Compare::Unequal) {
                    return callback(ImplRef(type, &null_params, &null_assoc), cmp);
                } else {
                    return false;
                }
            }

            // - `DiscriminantKind`
            if (!mLangDiscriminantKind.components().empty() && trait == mLangDiscriminantKind) {
                static auto nameDiscriminant = RcString::new_interned("Discriminant");
                // TODO: This logic is near identical to the logic in `static.cpp` - can it be de-duplicated?

                if (type->is_Infer() || (type->is_Path() && type->as_Path().binding.is_Unbound())) {
                    // TODO: How to prevent EAT from expanding (or setting opaque) too early?
                    return callback(ImplRef(type, HIR::PathParams(), ::HIR::TraitPath::assocListT()), ::HIR::Compare::Fuzzy);
                } else if (type->is_Generic() || (type->is_Path() && type->as_Path().binding.is_Opaque())) {
                    ::HIR::TraitPath::assocListT assocList;
                    assocList.insert(std::make_pair(nameDiscriminant, HIR::TraitPath::AtyEqual{trait, {}, crate.types.path(HIR::Path(type, trait.clone(), nameDiscriminant), HIR::TypePathBinding::make_Opaque({}))}));
                    return callback(ImplRef(type, HIR::PathParams(), ::HIR::TraitPath::assocListT()), ::HIR::Compare::Equal);
                    //return false;
                } else if (type->is_Path() && type->as_Path().binding.is_Enum()) {
                    const auto& enm = *type->as_Path().binding.as_Enum();
                    HIR::TypeRef tag_ty = crate.types.primitive(enm.get_repr_type(enm.tagRepr));
                    ::HIR::TraitPath::assocListT assocList;
                    assocList.insert(std::make_pair(nameDiscriminant, HIR::TraitPath::AtyEqual{trait, {}, std::move(tag_ty)}));
                    return callback(ImplRef(type, {}, std::move(assocList)), ::HIR::Compare::Equal);
                } else {
                    ::HIR::TraitPath::assocListT assocList;
                    assocList.insert(std::make_pair(nameDiscriminant, HIR::TraitPath::AtyEqual{trait, {}, crate.types.primitive(HIR::CoreType::U8)}));
                    return callback(ImplRef(type, {}, std::move(assocList)), ::HIR::Compare::Equal);
                }
            }
            if (!mLangPointee.components().empty() && trait == mLangPointee) {
                static auto nameMetadata = RcString::new_interned("Metadata");
                // TODO: This logic is near identical to the logic in `static.cpp` - can it be de-duplicated?

                HIR::TypeRef meta_ty = crate.types.infer();
                bool has_meta_ty = false;
                if (type->is_Infer() || (type->is_Path() && type->as_Path().binding.is_Unbound())) {
                    return callback(ImplRef(type, HIR::PathParams(), ::HIR::TraitPath::assocListT()), ::HIR::Compare::Fuzzy);
                }
                // Generics (or opaque ATYs)
                else if (type->is_Generic() || (type->is_Path() && type->as_Path().binding.is_Opaque())) {
                    // If the type is `Sized` return `()` as the type
                    if (type_is_sized(sp, type) != HIR::Compare::Unequal) {
                        meta_ty = crate.types.unit();
                        has_meta_ty = true;
                    } else {
                        // Return unbounded
                        // - leave as `_`
                    }
                }
                // Trait object: `Metadata=DynMetadata<T>`
                else if (type->is_TraitObject()) {
                    meta_ty = crate.types.path(::HIR::Path(::HIR::GenericPath(this->crate.get_lang_item_path(sp, "dyn_metadata"), HIR::PathParams(type))), HIR::TypePathBinding::make_Struct(&crate.get_struct_by_path(sp, this->crate.get_lang_item_path(sp, "dyn_metadata"))));
                    has_meta_ty = true;
                }
                // Slice and str
                else if (type->is_Slice() || TU_TEST1(*type, Primitive, == HIR::CoreType::Str)) {
                    meta_ty = crate.types.primitive(HIR::CoreType::Usize);
                    has_meta_ty = true;
                }
                // Structs: Can delegate their metadata
                else if (type->is_Path() && type->as_Path().binding.is_Struct()) {
                    const auto& str = *type->as_Path().binding.as_Struct();
                    switch (str.structMarkings.dst_type) {
                        case HIR::StructMarkings::DstType::None:
                            meta_ty = crate.types.unit();
                            has_meta_ty = true;
                            break;
                        case HIR::StructMarkings::DstType::Possible:
                        case HIR::StructMarkings::DstType::TraitObject: {
                            const ::HIR::TypeData* tail_tpl = nullptr;
                            TU_MATCHA((str.mData), (se),
                                (Unit, BUG(sp, "Unsized unit struct in Pointee lookup - " << type);),
                                (Tuple, ASSERT_BUG(sp, !se.empty(), "Unsized tuple struct without fields - " << type); tail_tpl = se.back().ent;),
                                (Named, ASSERT_BUG(sp, !se.empty(), "Unsized struct without fields - " << type); tail_tpl = se.back().ty;)
                            )
                            ASSERT_BUG(sp, tail_tpl, "Missing unsized tail field for " << type);

                            const auto& path = type->as_Path().path.mData.as_Generic();
                            auto tail_ty = MonomorphStatePtr(crate.types, type, &path.mParams, nullptr).monomorph_type(sp, tail_tpl);
                            tail_ty = this->expand_associated_types(sp, std::move(tail_ty));

                            return this->find_trait_impls(sp, trait, params, tail_ty, [&](ImplRef impl, HIR::Compare cmp) {
                                ::HIR::TraitPath::assocListT assoc;
                                auto metadata_ty = impl.get_type(crate.types, "Metadata", {});
                                if (metadata_ty) {
                                    assoc.insert(std::make_pair(nameMetadata, HIR::TraitPath::AtyEqual{trait, {}, std::move(metadata_ty)}));
                                }
                                return callback(ImplRef(type, params.clone(), std::move(assoc)), cmp);
                            });
                        }
                        case HIR::StructMarkings::DstType::Slice:
                            meta_ty = crate.types.primitive(HIR::CoreType::Usize);
                            has_meta_ty = true;
                            break;
                    }
                } else {
                    meta_ty = crate.types.unit();
                    has_meta_ty = true;
                }
                DEBUG("<" << type << " as Pointee>::Metadata = " << meta_ty);
                ::HIR::TraitPath::assocListT assocList;
                if (has_meta_ty) {
                    assocList.insert(std::make_pair(RcString::new_interned("Metadata"), HIR::TraitPath::AtyEqual{trait, {}, mv$(meta_ty)}));
                }

                return callback(ImplRef(type, {}, std::move(assocList)), ::HIR::Compare::Equal);
            }
            // - `Tuple`
            if (!langTuple.components().empty() && trait == langTuple) {
                // Fuzzy impl for `_` and unbound ATYs
                if (type->is_Infer() || (type->is_Path() && type->as_Path().binding.is_Unbound())) {
                    return callback(ImplRef(type, HIR::PathParams(), ::HIR::TraitPath::assocListT()), ::HIR::Compare::Fuzzy);
                }
                // Impl for tuples
                if (type->is_Tuple()) {
                    return callback(ImplRef(type, {}, ::HIR::TraitPath::assocListT()), ::HIR::Compare::Equal);
                }
                // No impls for anything else
                return false;
            }

            // Magic Unsize impls to trait objects
            if (trait == mLangUnsize) {
                ASSERT_BUG(sp, params.types.size() == 1, "Unsize trait requires a single type param");
                const auto& dst_ty = this->ivars.get_type(params.types[0]);

                if (find_trait_impls_bound(sp, trait, params, type, callback)) {
                    return true;
                }

                bool rv = false;
                auto cb = [&](auto new_dst) {
                    ::HIR::PathParams real_params{mv$(new_dst)};
                    rv = callback(ImplRef(type, mv$(real_params), {}), ::HIR::Compare::Fuzzy);
                };
                //if( dst_ty->is_Infer() || type->is_Infer() )
                //{
                //    rv = callback( ImplRef(type.clone(), params.clone(), {}), ::HIR::Compare::Fuzzy );
                //    return rv;
                //}
                auto cmp = this->canUnsize(sp, dst_ty, type, cb);
                if (cmp == ::HIR::Compare::Equal) {
                    assert(!rv);
                    rv = callback(ImplRef(type, params.clone(), {}), ::HIR::Compare::Equal);
                }
                return rv;
            }

            // Magical CoerceUnsized impls for various types
            if (!langCoerceUnsized.components().empty() && trait == langCoerceUnsized) {
                if (find_trait_impls_bound(sp, trait, params, type, callback)) {
                    return true;
                }

                const auto& dst_ty = params.types.at(0);
                // - `*mut T => *const T`
                if (const auto* e = type->opt_Pointer()) {
                    if (const auto* de = dst_ty->opt_Pointer()) {
                        if (de->type < e->type) {
                            auto cmp = e->inner->compareWithPlaceholders(sp, de->inner, this->ivars.callbackResolveInfer());
                            if (cmp != ::HIR::Compare::Unequal) {
                                ::HIR::PathParams pp;
                                pp.types.push_back(dst_ty);
                                if (callback(ImplRef(type, mv$(pp), {}), cmp)) {
                                    return true;
                                }
                            }
                        }
                    }
                }
            } else if (trait == mLangPointeeSized) {
                if (find_trait_impls_bound(sp, trait, params, type, callback)) {
                    return true;
                }
                // Lowest level of sizedness: This _might_ be sized (i.e. it's not an extern type?)
                return callback(ImplRef(type, {}, ::HIR::TraitPath::assocListT()), ::HIR::Compare::Equal);
            } else if (trait == mLangMetaSized) {
                TODO(sp, "MetaSized");
                // Next level of sizedness: There's metadata that allows getting the size
                // - No difference to the above?
                //switch( this->metadata_type(sp, type) )
                //{
                //case MetadataType::Unknown:
                //    break;
                //case MetadataType::None:
                //case MetadataType::Slice:
                //case MetadataType::TraitObject:
                //case MetadataType::Zero:    // TODO: Does zero apply here?
                //    return found_cb( ImplRef(&null_hrls, type, &null_params, &null_assoc), false );
                //}
            }

            if (trait == mLangDestruct) {
                // Inidicates that something is droppable
                // - Applies to everything?
                if (find_trait_impls_bound(sp, trait, params, type, callback)) {
                    return true;
                }
                // Lowest level of sizedness: This _might_ be sized (i.e. it's not an extern type?)
                return callback(ImplRef(type, {}, ::HIR::TraitPath::assocListT()), ::HIR::Compare::Equal);
            }

            return false;
        }

        bool TraitResolution::find_trait_impls_types(const Span& sp, const ::HIR::SimplePath& trait, const ::HIR::PathParams& params, const ::HIR::TypeData* type, t_cb_trait_impl_r callback) const {
    TU_MATCH_HDRA( (*type), {)
    default:
        break;
        TU_ARMA(NodeType, e) {
        TU_MATCH_HDRA((e), {)
        // Magic impls of the Fn* traits for closure types
        TU_ARMA(Closure, node_p) {
                    DEBUG("Closure, " << trait << " ?= Fn*");
                    if (trait == mLangFn || trait == mLangFnMut || trait == mLangFnOnce) {
                        if (params.types.size() != 1) {
                            BUG(sp, "Fn* traits require a single tuple argument");
                        }
                        if (!params.types[0]->is_Tuple()) {
                            BUG(sp, "Fn* traits require a single tuple argument");
                        }

                        const auto& argsDes = params.types[0]->as_Tuple();
                        if (argsDes.size() != node_p->mArgs.size()) {
                            return false;
                        }

                        auto cmp = ::HIR::Compare::Equal;
                        ::std::vector<::HIR::TypeRef> args;
                        for (unsigned int i = 0; i < node_p->mArgs.size(); i++) {
                            const auto& at = node_p->mArgs[i].second;
                            args.push_back(at);
                            DEBUG(at << " ?= " << argsDes[i]);
                            cmp &= at->compareWithPlaceholders(sp, argsDes[i], this->ivars.callbackResolveInfer());
                        }
                        if (cmp != ::HIR::Compare::Unequal) {
                            // NOTE: This is a conditional "true", we know nothing about the move/mut-ness of this closure yet
                            // - Could we?
                            // - Not until after the first stage of typeck

                            DEBUG("Closure Fn* impl - cmp = " << cmp);

                            ::HIR::PathParams pp;
                            pp.types.push_back(crate.types.tuple(mv$(args)));
                            ::HIR::TraitPath::assocListT types;
                            types.insert(::std::make_pair("Output", ::HIR::TraitPath::AtyEqual{::HIR::GenericPath(mLangFnOnce, pp.clone()), {}, node_p->returnType}));
                            return callback(ImplRef(type, mv$(pp), mv$(types)), cmp);
                        } else {
                            DEBUG("Closure Fn* impl - cmp = Compare::Unequal");
                            return false;
                        }
                    }
                }
                TU_ARMA(Generator, node_p) {
                    if (trait == mLangGenerator) {
                        static const RcString rcstringYield = RcString::new_interned("Yield");
                        static const RcString rcstringReturn = RcString::new_interned("Return");
                        ::HIR::TraitPath::assocListT assoc;
                        assoc.insert(::std::make_pair(rcstringYield, ::HIR::TraitPath::AtyEqual{trait.clone(), {}, node_p->yieldTy}));
                        assoc.insert(::std::make_pair(rcstringReturn, ::HIR::TraitPath::AtyEqual{trait.clone(), {}, node_p->returnType}));
                        HIR::PathParams params;
                        params.types.push_back(node_p->resumeTy);
                        return callback(ImplRef(type, mv$(params), mv$(assoc)), ::HIR::Compare::Equal);
                    }
                }
                TU_ARMA(Async, node_p) {
                    if (trait == mLangFuture) {
                        static const RcString rcstringOutput = RcString::new_interned("Output");
                        ::HIR::TraitPath::assocListT assoc;
                        assoc.insert(::std::make_pair(rcstringOutput, ::HIR::TraitPath::AtyEqual{trait.clone(), {}, node_p->mCode->resType}));
                        return callback(ImplRef(type, {}, mv$(assoc)), ::HIR::Compare::Equal);
                    }
                }
        }
        }
        // Magic Fn* trait impls for function pointers
        TU_ARMA(Function, e) {
            if (trait == mLangFn || trait == mLangFnMut || trait == mLangFnOnce) {
                DEBUG("Fn* trait for fn pointer");
                if (params.types.size() != 1) {
                    BUG(sp, "Fn* traits require a single tuple argument");
                }
                if (!params.types[0]->is_Tuple()) {
                    BUG(sp, "Fn* traits require a single tuple argument");
                }
                const auto& argsDes = params.types[0]->as_Tuple();
                if (argsDes.size() != e.argTypes.size()) {
                    return false;
                }

                // NOTE: unsafe or non-rust ABI functions aren't valid
                if (e.mAbi != ABI_RUST || e.is_unsafe) {
                    DEBUG("- No magic impl, wrong ABI or unsafe in " << type);
                    return false;
                }
                DEBUG("- Magic impl of Fn* for " << type);

                auto cmp = ::HIR::Compare::Equal;
                ::std::vector<::HIR::TypeRef> args;
                for (unsigned int i = 0; i < e.argTypes.size(); i++) {
                    const auto& at = e.argTypes[i];
                    args.push_back(at);
                    cmp &= at->compareWithPlaceholders(sp, argsDes[i], this->ivars.callbackResolveInfer());
                }

                ::HIR::PathParams pp;
                pp.types.push_back(crate.types.tuple(mv$(args)));
                ::HIR::TraitPath::assocListT types;
                types.insert(::std::make_pair("Output", ::HIR::TraitPath::AtyEqual{::HIR::GenericPath(mLangFnOnce, pp.clone()), {}, e.mRettype}));
                auto hrls = get_hrls(crate.types, sp, e.hrls, pp, params);
                return callback(ImplRef(std::move(hrls), type, mv$(pp), mv$(types)), cmp);
            }
        }
        // Magic Fn* trait impls for function pointers
        TU_ARMA(NamedFunction, real_e) {
            if (trait == mLangFn || trait == mLangFnMut || trait == mLangFnOnce) {
                if (params.types.size() != 1) {
                    BUG(sp, "Fn* traits require a single tuple argument");
                }
                if (!params.types[0]->is_Tuple()) {
                    BUG(sp, "Fn* traits require a single tuple argument");
                }

                DEBUG("- Magic impl of Fn* for " << type);
                auto e = real_e.decay(crate.types, sp);
                DEBUG("> " << e.mRettype << " - " << e.argTypes);
                const auto& argsDes = params.types[0]->as_Tuple();
                if (argsDes.size() != e.argTypes.size()) {
                    return false;
                }

                // NOTE: unsafe or non-rust ABI functions aren't valid
                if (e.mAbi != ABI_RUST) {
                    DEBUG("- No magic impl, wrong ABI (`" << e.mAbi << "`): " << type);
                    return false;
                }
                if (e.is_unsafe) {
                    DEBUG("- No magic impl, unsafe function: " << type);
                    return false;
                }
                DEBUG("- Magic impl of Fn* for " << type);

                auto cmp = ::HIR::Compare::Equal;
                ::std::vector<::HIR::TypeRef> args;
                for (unsigned int i = 0; i < e.argTypes.size(); i++) {
                    const auto& at = e.argTypes[i];
                    args.push_back(at);
                    cmp &= at->compareWithPlaceholders(sp, argsDes[i], this->ivars.callbackResolveInfer());
                }

                ::HIR::PathParams pp;
                pp.types.push_back(crate.types.tuple(mv$(args)));
                ::HIR::TraitPath::assocListT types;
                types.insert(::std::make_pair("Output", ::HIR::TraitPath::AtyEqual{::HIR::GenericPath(mLangFnOnce, pp.clone()), {}, e.mRettype}));
                auto hrls = get_hrls(crate.types, sp, e.hrls, pp, params);
                return callback(ImplRef(std::move(hrls), type, mv$(pp), mv$(types)), cmp);
            }
        }
        // Magic index and unsize impls for Arrays
        // NOTE: The index impl for [T] is in libcore.
        TU_ARMA(Array, e) {
        }
    }
    return false;
        }

        bool TraitResolution::find_trait_impls_legacy(
            const Span& sp,
            const ::HIR::SimplePath& trait,
            const ::HIR::PathParams& params,
            const ::HIR::TypeData* ty,
            t_cb_trait_impl_r callback,
            bool magic_trait_impls /*=true*/,
            bool search_crate /*=true*/,
            bool search_bounds /*=true*/
        ) const {
            static ::HIR::PathParams null_params;
            static ::HIR::TraitPath::assocListT null_assoc;

            const auto& type = this->ivars.get_type(ty);
            TRACE_FUNCTION_F("trait = " << trait << params << ", type = " << type);


            //const auto& trait_index = this->m_crate.get_lang_item_path(sp, "index");
            //const auto& trait_indexmut = this->m_crate.get_lang_item_path(sp, "index_mut");

            if (magic_trait_impls) {
                if (find_trait_impls_magic(sp, trait, params, ty, callback)) {
                    return true;
                }
            }

            if (find_trait_impls_types(sp, trait, params, ty, callback)) {
                return true;
            }

            // Trait impls from complex bounds
    TU_MATCH_HDRA( (*type), {)
    default:
        break;
        // Trait objects automatically implement their own traits
        // - IF object safe (TODO)
        TU_ARMA(TraitObject, e) {
            if (trait == e.mTrait.mPath.mPath) {
                auto cmp = comparePp(sp, e.mTrait.mPath.mParams, params);
                if (cmp != ::HIR::Compare::Unequal) {
                    DEBUG("TraitObject impl params" << e.mTrait.mPath.mParams);
                    auto hrls = get_hrls(crate.types, sp, e.mTrait.hrtbs, e.mTrait.mPath.mParams, params);
                    return callback(ImplRef(std::move(hrls), type, &e.mTrait.mPath.mParams, &e.mTrait.typeBounds, e.mTrait.constness), cmp);
                }
            }
            // Markers too
            for (const auto& mt : e.markers) {
                if (trait == mt.mPath) {
                    auto cmp = comparePp(sp, mt.mParams, params);
                    if (cmp != ::HIR::Compare::Unequal) {
                        //auto hrls = get_hrls(sp, e.m_trait.m_hrtbs, e.m_trait.m_path.m_params, params);
                        return callback(ImplRef(HIR::PathParams(), type, &mt.mParams, &null_assoc), cmp);
                    }
                }
            }

            if (e.mTrait.mPath.mPath != HIR::SimplePath()) {
                // - Check if the desired trait is a supertrait of this.
                // NOTE: `params` (aka des_params) is not used (TODO)
                bool rv = false;
                bool is_supertrait = false;
                this->find_named_trait_in_trait(sp, trait, params, *e.mTrait.traitPtr, e.mTrait.mPath.mPath, e.mTrait.mPath.mParams, type, [&](const HIR::TraitPath& i_tp) {
                    // The above is just the monomorphised params and associated set. Comparison is still needed.
                    auto cmp = this->comparePp(sp, i_tp.mPath.mParams, params);
                    if (cmp != ::HIR::Compare::Unequal) {
                        // Invoke callback with a proper ImplRef
                        ::HIR::TraitPath::assocListT assocClone;
                        for (const auto& e : i_tp.typeBounds) {
                            assocClone.insert(::std::make_pair(e.first, e.second.clone()));
                        }
                        for (const auto& bound : e.mTrait.typeBounds) {
                            if (bound.second.source_trait.mPath == trait
                                && comparePp(
                                    sp,
                                    bound.second.source_trait.mParams,
                                    i_tp.mPath.mParams
                                ) != ::HIR::Compare::Unequal) {
                                assocClone.erase(bound.first);
                                assocClone.insert(::std::make_pair(
                                    bound.first, bound.second.clone()
                                ));
                            }
                        }
                        ASSERT_BUG(sp, !e.mTrait.hrtbs || !i_tp.hrtbs, "TODO: Handle two layers of HRTBs - " << e.mTrait << " and " << i_tp);
                        auto hrls = get_hrls(crate.types, sp, e.mTrait.hrtbs, i_tp.mPath.mParams, params);
                        auto ir = ImplRef(std::move(hrls), type, i_tp.mPath.mParams.clone(), mv$(assocClone));
                        DEBUG("TraitObject: - ir = " << ir);
                        is_supertrait = true;
                        rv = callback(mv$(ir), cmp);
                        return cmp == ::HIR::Compare::Equal; // Shortcut if perfect match
                    }
                    return false;
                });
                if (is_supertrait) {
                    return rv;
                }
            }
        } // TU_ARMA(TraitObject, e)
        TU_ARMA(ErasedType, e) {
            for (const auto& trait_path : e.traits) {
                if (trait == trait_path.mPath.mPath) {
                    auto cmp = comparePp(sp, trait_path.mPath.mParams, params);
                    if (cmp != ::HIR::Compare::Unequal) {
                        DEBUG("TraitObject impl params" << trait_path.mPath.mParams);
                        auto hrls = get_hrls(crate.types, sp, trait_path.hrtbs, trait_path.mPath.mParams, params);
                        return callback(ImplRef(std::move(hrls), type, &trait_path.mPath.mParams, &trait_path.typeBounds, trait_path.constness), cmp);
                    }
                }

                // - Check if the desired trait is a supertrait of this.
                // NOTE: `params` (aka des_params) is not used (TODO)
                bool rv = false;
                bool is_supertrait = false;
                this->find_named_trait_in_trait(sp, trait, params, *trait_path.traitPtr, trait_path.mPath.mPath, trait_path.mPath.mParams, type, [&](const HIR::TraitPath& i_tp) {
                    // The above is just the monomorphised params and associated set. Comparison is still needed.
                    auto cmp = this->comparePp(sp, i_tp.mPath.mParams, params);
                    if (cmp != ::HIR::Compare::Unequal) {
                        // Invoke callback with a proper ImplRef
                        ::HIR::TraitPath::assocListT assocClone;
                        for (const auto& e : i_tp.typeBounds) {
                            assocClone.insert(::std::make_pair(e.first, e.second.clone()));
                        }
                        // Existential equalities are stored on the principal
                        // bound even when the associated item is declared by
                        // a supertrait (e.g. `FnMut` carries `FnOnce::Output`).
                        // Project those equalities together with the
                        // supertrait candidate.
                        for (const auto& e : trait_path.typeBounds) {
                            if (e.second.source_trait.mPath == trait
                                && comparePp(
                                    sp,
                                    e.second.source_trait.mParams,
                                    i_tp.mPath.mParams
                                ) != ::HIR::Compare::Unequal) {
                                assocClone.erase(e.first);
                                assocClone.insert(::std::make_pair(
                                    e.first, e.second.clone()
                                ));
                            }
                        }
                        ASSERT_BUG(sp, !trait_path.hrtbs || !i_tp.hrtbs, "TODO: Handle two layers of HRTBs - " << trait_path << " and " << i_tp);
                        auto hrls = trait_path.hrtbs ? get_hrls(crate.types, sp, trait_path.hrtbs, i_tp.mPath.mParams, params) : get_hrls(crate.types, sp, i_tp.hrtbs, i_tp.mPath.mParams, params);
                        auto ir = ImplRef(std::move(hrls), type, i_tp.mPath.mParams.clone(), mv$(assocClone));
                        DEBUG("ErasedType: - ir = " << ir);
                        is_supertrait = true;
                        rv = callback(mv$(ir), cmp);
                        return cmp == HIR::Compare::Equal;
                    }
                    return false;
                });
                if (is_supertrait) {
                    return rv;
                }
            }
        } // TU_ARMA(ErasedType)
        // If the type in question is a magic placeholder, return a placeholder impl :)
        TU_ARMA(Generic, e) {
            if ((e.binding >> 8) == 2) {
                // TODO: This is probably going to break something in the future.
                DEBUG("- Magic impl for placeholder type");
                return callback(ImplRef(type, &null_params, &null_assoc), ::HIR::Compare::Fuzzy);
            }
        } // TU_ARMA(Generic)
        // If this type is an opaque UfcsKnown - check bounds
        TU_ARMA(Path, e) {
            if (e.binding.is_Opaque()) {
                ASSERT_BUG(sp, e.path.mData.is_UfcsKnown(), "Opaque bound type wasn't UfcsKnown - " << type);
                const auto& pe = e.path.mData.as_UfcsKnown();

                // TODO: Should Self here be `type` or `pe.type`
                // - Depends... if implicit it should be `type` (as it relates to the associated type), but if explicit it's referring to the trait
                auto monomorph_cb = MonomorphStatePtr(crate.types, pe.type, &pe.trait.mParams, &pe.params);
                auto rv = this->iterate_aty_bounds(sp, pe, [&](const HIR::TraitPath& bound) {
                    DEBUG("Bound on ATY: " << bound);
                    static const HIR::GenericParams empty_params;
                    const auto& hrls_def = (bound.hrtbs && !bound.hrtbs->is_empty()) ? *bound.hrtbs : empty_params;
                    auto pp_hrb = hrls_def.make_empty_params(true);
                    monomorph_cb.pp_hrb = &pp_hrb;
                    const auto& bParams = bound.mPath.mParams;
                    ::HIR::PathParams params_mono_o;
                    const ::HIR::PathParams* bParamsMono = &bParams;
                    if (monomorphise_pathparams_needed(bParams)) {
                        params_mono_o = monomorph_cb.monomorph_path_params(sp, bParams, false);
                        bParamsMono = &params_mono_o;
                    }
                    const bool params_need_normalisation = ::std::any_of(
                        bParamsMono->types.begin(), bParamsMono->types.end(),
                        [&](const auto& ty) { return this->has_associated_type(ty); }
                    );
                    if (params_need_normalisation) {
                        if (bParamsMono != &params_mono_o) {
                            params_mono_o = bParams.clone();
                            bParamsMono = &params_mono_o;
                        }
                        this->expand_associated_types_params(sp, params_mono_o);
                    }

                    ::HIR::TraitPath::assocListT bAtys;
                    for (const auto& aty : bound.typeBounds) {
                        bAtys.insert(::std::make_pair(aty.first, ::HIR::TraitPath::AtyEqual{monomorph_cb.monomorph_genericpath(sp, aty.second.source_trait, false), {}, monomorph_cb.monomorph_type(sp, aty.second.type)}));
                    }

                    if (bound.mPath.mPath == trait) {
                        auto cmp = this->comparePp(sp, *bParamsMono, params);
                        if (cmp != ::HIR::Compare::Unequal) {
                            if (bParamsMono == &params_mono_o) {
                                // TODO: assoc bounds
                                if (callback(ImplRef(type, mv$(params_mono_o), mv$(bAtys), bound.constness), cmp)) {
                                    return true;
                                }
                                params_mono_o = monomorph_cb.monomorph_path_params(sp, bParams, false);
                                if (params_need_normalisation) {
                                    this->expand_associated_types_params(sp, params_mono_o);
                                }
                            } else if (!bAtys.empty()) {
                                if (callback(ImplRef(type, bParamsMono->clone(), mv$(bAtys), bound.constness), cmp)) {
                                    return true;
                                }
                            } else {
                                auto hrls = get_hrls(crate.types, sp, bound.hrtbs, bound.mPath.mParams, params);
                                if (callback(ImplRef(std::move(hrls), type, &bound.mPath.mParams, &null_assoc, bound.constness), cmp)) {
                                    return true;
                                }
                            }
                        }
                    }
                    monomorph_cb.pp_hrb = nullptr;

                    bool rv = false;
                    bool ret = false;
                    this->find_named_trait_in_trait(sp, trait, params, *bound.traitPtr, bound.mPath.mPath, *bParamsMono, type, [&](const HIR::TraitPath& i_tp) {
                        auto cmp = this->comparePp(sp, i_tp.mPath.mParams, params);
                        DEBUG("Opaque Path: cmp=" << cmp << ", impl " << i_tp.mPath << " for " << type << " -- desired " << trait << params);
                        ASSERT_BUG(sp, !bound.hrtbs || !i_tp.hrtbs, "TODO: Handle two layers of HRTBs - " << bound.mPath << " and " << i_tp);
                        const HIR::GenericParams* hrtbs = bound.hrtbs ? bound.hrtbs.get() : i_tp.hrtbs.get();
                        auto hrls = get_hrls(crate.types, sp, hrtbs, i_tp.mPath.mParams, params);
                        auto ir = ImplRef(std::move(hrls), type, i_tp.mPath.mParams.clone(), {}, i_tp.constness);
                        rv |= (cmp != ::HIR::Compare::Unequal && callback(std::move(ir), cmp));
                        ret = true;
                        return false; // Continue
                    });
                    if (ret) {
                        // NOTE: Callback called in closure's return statement
                        return rv;
                    }
                    return false;
                });
                if (rv) {
                    return true;
                }
            }
        } // TU_ARMA(Path)
    } // TU_MATCH_HDRA

    // 1. Search generic params
    if( search_bounds && find_trait_impls_bound(sp, trait, params, type, callback) )
        return true;
    // 2. Search crate-level impls
    if( !search_crate )
        return false;
    return find_trait_impls_crate(sp, trait, params, type,  callback);
        }

class NextTraitGoalEvaluator {
        enum class Certainty {
            NoSolution,
            Ambiguous,
            Proven,
        };

        enum class CandidateSource {
            Builtin,
            ParamEnv,
            Other,
            TraitImpl,
        };

        enum class OrphanPerspective {
            Local,
            Remote,
        };

        enum class OrphanVisit {
            NonLocal,
            LocalKey,
            Uncovered,
        };

        struct Candidate {
            ImplRef impl;
            ::HIR::Compare head_match;
            Certainty certainty;
            const ::HIR::MarkerImpl* marker_impl;
            ::HIR::PathParams marker_impl_params;
            bool autoBuiltin;
            CandidateSource source;
            bool ambiguityBeyondHead = false;
            bool discarded = false;

            Candidate(
                ImplRef impl,
                ::HIR::Compare head_match,
                const ::HIR::MarkerImpl* marker_impl,
                ::HIR::PathParams marker_impl_params,
                bool autoBuiltin,
                CandidateSource source
            )
                : impl(::std::move(impl))
                , head_match(head_match)
                , certainty(Certainty::Ambiguous)
                , marker_impl(marker_impl)
                , marker_impl_params(::std::move(marker_impl_params))
                , autoBuiltin(autoBuiltin)
                , source(source)
            {
            }

            bool is_negative() const {
                return marker_impl && !marker_impl->is_positive;
            }

            bool is_positive_marker_impl() const {
                return marker_impl && marker_impl->is_positive;
            }
        };

        struct CandidateFrame {
            ::std::vector<Candidate*> candidates;
            ::std::vector<Candidate*> viable;
            size_t availableDepth = 0;
            bool encountered_overflow = false;

            CandidateFrame() {
                candidates.reserve(32);
                viable.reserve(32);
            }

            void clear(stl::ObjList<Candidate>& nodes) {
                for (auto* candidate : candidates) {
                    nodes.release(candidate);
                }
                candidates.clear();
                viable.clear();
                availableDepth = 0;
                encountered_overflow = false;
            }
        };

        static constexpr size_t ROOT_DEPTH = 128;
        static constexpr size_t OVERFLOW_DEPTH_DIVISOR = 4;

        struct GoalKey {
            size_t hash;
            ::HIR::SimplePath trait;
            ::HIR::PathParams params;
            ::HIR::TypeRef type;
            ::HIR::TraitPath::assocListT associated;

            GoalKey(
                size_t hash,
                const ::HIR::SimplePath& trait,
                const ::HIR::PathParams& params,
                const ::HIR::TypeData* type,
                const ::HIR::TraitPath::assocListT* associated
            )
                : hash(hash)
                , trait(trait)
                , params(params.clone())
                , type(type)
                , associated(cloneAssociated(associated))
            {
            }
        };

        struct CachedGoal {
            GoalKey goal;
            Certainty certainty;
            ImplRef response;
            ::HIR::Compare response_certainty = ::HIR::Compare::Fuzzy;
            bool has_response = false;

            CachedGoal(
                size_t hash,
                const ::HIR::SimplePath& trait,
                const ::HIR::PathParams& params,
                const ::HIR::TypeData* type,
                const ::HIR::TraitPath::assocListT* associated,
                Certainty certainty
            )
                : goal(hash, trait, params, type, associated)
                , certainty(certainty)
            {
            }
        };

        const TraitResolution& mResolve;
        const ::HIR::Crate& crate;
        const Span* mSpan = nullptr;
        bool coherenceMode = false;

        // Frames and candidates have stable pool-backed addresses.  Vectors
        // are pointer indexes only, so recursive growth never moves an ImplRef
        // or invalidates a parent candidate.
        stl::ObjList<Candidate> candidateNodes;
        ::std::vector<CandidateFrame*> frames;
        size_t frameDepth = 0;
        stl::ObjList<GoalKey> activeGoalNodes;
        stl::ObjList<CachedGoal> cachedGoalNodes;
        ::std::vector<GoalKey*> goalStack;
        ::std::vector<CachedGoal*> goalCache;
        ::std::unordered_multimap<size_t, GoalKey*> activeGoalIndex;
        ::std::unordered_multimap<size_t, CachedGoal*> goalCacheIndex;
        uint64_t responseInstanceCounter = 0;

        struct CanonicalGoal {
            ::HIR::PathParams params;
            ::HIR::TypeRef type;
            ::HIR::TraitPath::assocListT associated;

            CanonicalGoal(::HIR::PathParams params, ::HIR::TypeRef type)
                : params(::std::move(params))
                , type(type)
            {
            }
        };

        const Span& span() const {
            ASSERT_BUG(Span(), mSpan, "next-solver session used outside an evaluation");
            return *mSpan;
        }

        CanonicalGoal canonicalizeGoal(
            const ::HIR::PathParams& params,
            const ::HIR::TypeData* type,
            const ::HIR::TraitPath::assocListT* associated,
            CanonicalizeTraitGoal& canonicalizer
        ) const {
            auto canonicalParams = canonicalizer.monomorph_path_params(
                span(), params, true
            );
            const auto canonicalType = canonicalizer.monomorph_type(
                span(), type, true
            );
            CanonicalGoal result(
                ::std::move(canonicalParams), canonicalType
            );
            if (associated) {
                for (const auto& entry : *associated) {
                    result.associated.insert({
                        entry.first,
                        ::HIR::TraitPath::AtyEqual{
                            canonicalizer.monomorph_genericpath(
                                span(), entry.second.source_trait, true
                            ),
                            canonicalizer.monomorph_path_params(
                                span(), entry.second.atyParams, true
                            ),
                            canonicalizer.monomorph_type(
                                span(), entry.second.type, true
                            )
                        }
                    });
                }
            }
            return result;
        }

        ::std::optional<size_t> availableDepthForNested() {
            if (frameDepth == 0) {
                return ROOT_DEPTH;
            }
            auto& parent = *frames[frameDepth - 1];
            if (parent.availableDepth == 0) {
                parent.encountered_overflow = true;
                return {};
            }
            return parent.encountered_overflow
                ? parent.availableDepth / OVERFLOW_DEPTH_DIVISOR
                : parent.availableDepth - 1;
        }

        static bool is_environment_or_builtin(const ImplRef& impl) {
            return !impl.mData.is_TraitImpl();
        }

        bool params_have_unknown_types(const ::HIR::PathParams& params) const {
            for (const auto& type : params.types) {
                if (type_has_unknown(type)) {
                    return true;
                }
            }
            return false;
        }

        bool path_has_unknown_types(const ::HIR::Path& path) const {
            if (const auto* pe = path.mData.opt_Generic()) {
                return params_have_unknown_types(pe->mParams);
            }
            if (const auto* pe = path.mData.opt_UfcsInherent()) {
                return type_has_unknown(pe->type)
                    || params_have_unknown_types(pe->params)
                    || params_have_unknown_types(pe->impl_params);
            }
            if (const auto* pe = path.mData.opt_UfcsKnown()) {
                return type_has_unknown(pe->type)
                    || params_have_unknown_types(pe->trait.mParams)
                    || params_have_unknown_types(pe->params);
            }
            const auto& pe = path.mData.as_UfcsUnknown();
            return type_has_unknown(pe.type)
                || params_have_unknown_types(pe.params);
        }

        bool trait_path_has_unknown_types(const ::HIR::TraitPath& trait) const {
            if (params_have_unknown_types(trait.mPath.mParams)) {
                return true;
            }
            for (const auto& assoc : trait.typeBounds) {
                if (params_have_unknown_types(assoc.second.source_trait.mParams)
                    || params_have_unknown_types(assoc.second.atyParams)
                    || type_has_unknown(assoc.second.type)) {
                    return true;
                }
            }
            for (const auto& assoc : trait.traitBounds) {
                if (params_have_unknown_types(assoc.second.source_trait.mParams)
                    || params_have_unknown_types(assoc.second.atyParams)) {
                    return true;
                }
                for (const auto& bound : assoc.second.traits) {
                    if (trait_path_has_unknown_types(bound)) {
                        return true;
                    }
                }
            }
            return false;
        }

        bool value_has_unassigned_infer(
            const ::HIR::ConstGeneric& value
        ) const {
            if (const auto* infer = value.opt_Infer()) {
                return infer->index == ~0u;
            }
            if (const auto* unevaluated = value.opt_Unevaluated()) {
                return params_have_unassigned_infer((*unevaluated)->params_impl)
                    || params_have_unassigned_infer((*unevaluated)->params_item);
            }
            return false;
        }

        bool params_have_unassigned_infer(
            const ::HIR::PathParams& params
        ) const {
            for (const auto& type : params.types) {
                if (type_has_unassigned_infer(type)) {
                    return true;
                }
            }
            for (const auto& value : params.values) {
                if (value_has_unassigned_infer(value)) {
                    return true;
                }
            }
            return false;
        }

        bool path_has_unassigned_infer(const ::HIR::Path& path) const {
            if (const auto* pe = path.mData.opt_Generic()) {
                return params_have_unassigned_infer(pe->mParams);
            }
            if (const auto* pe = path.mData.opt_UfcsInherent()) {
                return type_has_unassigned_infer(pe->type)
                    || params_have_unassigned_infer(pe->params)
                    || params_have_unassigned_infer(pe->impl_params);
            }
            if (const auto* pe = path.mData.opt_UfcsKnown()) {
                return type_has_unassigned_infer(pe->type)
                    || params_have_unassigned_infer(pe->trait.mParams)
                    || params_have_unassigned_infer(pe->params);
            }
            const auto& pe = path.mData.as_UfcsUnknown();
            return type_has_unassigned_infer(pe.type)
                || params_have_unassigned_infer(pe.params);
        }

        bool trait_path_has_unassigned_infer(
            const ::HIR::TraitPath& trait
        ) const {
            if (params_have_unassigned_infer(trait.mPath.mParams)) {
                return true;
            }
            for (const auto& assoc : trait.typeBounds) {
                if (params_have_unassigned_infer(
                        assoc.second.source_trait.mParams
                    )
                    || params_have_unassigned_infer(assoc.second.atyParams)
                    || type_has_unassigned_infer(assoc.second.type)) {
                    return true;
                }
            }
            for (const auto& assoc : trait.traitBounds) {
                if (params_have_unassigned_infer(
                        assoc.second.source_trait.mParams
                    )
                    || params_have_unassigned_infer(assoc.second.atyParams)) {
                    return true;
                }
                for (const auto& bound : assoc.second.traits) {
                    if (trait_path_has_unassigned_infer(bound)) {
                        return true;
                    }
                }
            }
            return false;
        }

        bool type_has_unassigned_infer(
            const ::HIR::TypeData* input
        ) const {
            if (const auto* infer = input->opt_Infer()) {
                if (infer->index == ~0u) {
                    return true;
                }
                const auto* resolved = mResolve.resolve_type(input);
                return resolved != input
                    && type_has_unassigned_infer(resolved);
            }
            if (const auto* path = input->opt_Path()) {
                return path_has_unassigned_infer(path->path);
            }
            if (const auto* object = input->opt_TraitObject()) {
                if (trait_path_has_unassigned_infer(object->mTrait)) {
                    return true;
                }
                for (const auto& marker : object->markers) {
                    if (params_have_unassigned_infer(marker.mParams)) {
                        return true;
                    }
                }
                return false;
            }
            if (const auto* erased = input->opt_ErasedType()) {
                for (const auto& trait : erased->traits) {
                    if (trait_path_has_unassigned_infer(trait)) {
                        return true;
                    }
                }
                if (const auto* known = erased->inner.opt_Known()) {
                    return type_has_unassigned_infer(*known);
                }
                if (const auto* alias = erased->inner.opt_Alias()) {
                    return params_have_unassigned_infer(alias->params);
                }
                if (const auto* fcn = erased->inner.opt_Fcn()) {
                    return path_has_unassigned_infer(fcn->origin);
                }
                return false;
            }
            if (const auto* array = input->opt_Array()) {
                const auto* size = array->size.opt_Unevaluated();
                return type_has_unassigned_infer(array->inner)
                    || (size && value_has_unassigned_infer(*size));
            }
            if (const auto* slice = input->opt_Slice()) {
                return type_has_unassigned_infer(slice->inner);
            }
            if (const auto* tuple = input->opt_Tuple()) {
                for (const auto& field : *tuple) {
                    if (type_has_unassigned_infer(field)) {
                        return true;
                    }
                }
                return false;
            }
            if (const auto* borrow = input->opt_Borrow()) {
                return type_has_unassigned_infer(borrow->inner);
            }
            if (const auto* pointer = input->opt_Pointer()) {
                return type_has_unassigned_infer(pointer->inner);
            }
            if (const auto* named = input->opt_NamedFunction()) {
                return path_has_unassigned_infer(named->path);
            }
            if (const auto* fcn = input->opt_Function()) {
                for (const auto& arg : fcn->argTypes) {
                    if (type_has_unassigned_infer(arg)) {
                        return true;
                    }
                }
                return type_has_unassigned_infer(fcn->mRettype);
            }
            return false;
        }

        bool goal_has_unassigned_infer(
            const ::HIR::PathParams& params,
            const ::HIR::TypeData* type,
            const ::HIR::TraitPath::assocListT* associated
        ) const {
            if (params_have_unassigned_infer(params)
                || type_has_unassigned_infer(type)) {
                return true;
            }
            if (associated) {
                for (const auto& entry : *associated) {
                    if (params_have_unassigned_infer(
                            entry.second.source_trait.mParams
                        )
                        || params_have_unassigned_infer(entry.second.atyParams)
                        || type_has_unassigned_infer(entry.second.type)) {
                        return true;
                    }
                }
            }
            return false;
        }

        bool self_is_unresolved_projection_over_ivar(
            const ::HIR::TypeData* type
        ) const {
            const auto* path = type->opt_Path();
            return path
                && path->binding.is_Unbound()
                && path->path.mData.is_UfcsKnown()
                && mResolve.type_contains_ivars(type);
        }

        bool type_has_unknown(const ::HIR::TypeData* input) const {
            const auto& type = mResolve.resolve_type(input);
            if (type->is_Infer() || type->is_Generic()) {
                return true;
            }
            if (const auto* path = type->opt_Path()) {
                return path_has_unknown_types(path->path);
            }
            if (const auto* object = type->opt_TraitObject()) {
                if (trait_path_has_unknown_types(object->mTrait)) {
                    return true;
                }
                for (const auto& marker : object->markers) {
                    if (params_have_unknown_types(marker.mParams)) {
                        return true;
                    }
                }
                return false;
            }
            if (const auto* erased = type->opt_ErasedType()) {
                for (const auto& trait : erased->traits) {
                    if (trait_path_has_unknown_types(trait)) {
                        return true;
                    }
                }
                if (const auto* known = erased->inner.opt_Known()) {
                    return type_has_unknown(*known);
                }
                if (const auto* alias = erased->inner.opt_Alias()) {
                    return params_have_unknown_types(alias->params);
                }
                if (const auto* fcn = erased->inner.opt_Fcn()) {
                    return path_has_unknown_types(fcn->origin);
                }
                return false;
            }
            if (const auto* array = type->opt_Array()) {
                return type_has_unknown(array->inner);
            }
            if (const auto* slice = type->opt_Slice()) {
                return type_has_unknown(slice->inner);
            }
            if (const auto* tuple = type->opt_Tuple()) {
                for (const auto& field : *tuple) {
                    if (type_has_unknown(field)) {
                        return true;
                    }
                }
                return false;
            }
            if (const auto* borrow = type->opt_Borrow()) {
                return type_has_unknown(borrow->inner);
            }
            if (const auto* pointer = type->opt_Pointer()) {
                return type_has_unknown(pointer->inner);
            }
            if (const auto* named = type->opt_NamedFunction()) {
                return path_has_unknown_types(named->path);
            }
            if (const auto* fcn = type->opt_Function()) {
                for (const auto& arg : fcn->argTypes) {
                    if (type_has_unknown(arg)) {
                        return true;
                    }
                }
                return type_has_unknown(fcn->mRettype);
            }
            return false;
        }

        static bool type_has_candidate_placeholder(
            const ::HIR::TypeData* type
        ) {
            bool found = false;
            visit_ty_with(type, [&](const ::HIR::TypeData* inner) {
                if (const auto* generic = inner->opt_Generic()) {
                    found |= generic->group() == ::HIR::GENERICPlaceholder;
                }
                return found;
            });
            return found;
        }

        static bool type_has_ufcs_unknown(const ::HIR::TypeData* type) {
            if (!type) {
                return false;
            }
            return visit_ty_with(type, [](const ::HIR::TypeData* inner) {
                const auto* path = inner->opt_Path();
                return path && path->path.mData.is_UfcsUnknown();
            });
        }

        static bool params_have_candidate_placeholders(
            const ::HIR::PathParams& params
        ) {
            for (const auto& type : params.types) {
                if (type_has_candidate_placeholder(type)) {
                    return true;
                }
            }
            for (const auto& value : params.values) {
                if (value.is_Generic()
                    && value.as_Generic().group() == ::HIR::GENERICPlaceholder) {
                    return true;
                }
            }
            return false;
        }

        bool candidateHasPlaceholders(const Candidate& candidate) const {
            if (type_has_candidate_placeholder(candidate.impl.get_impl_type(crate.types))
                || params_have_candidate_placeholders(
                    candidate.impl.get_trait_params(crate.types)
                )) {
                return true;
            }
            if (const auto* trait_impl = candidate.impl.mData.opt_TraitImpl()) {
                if (params_have_candidate_placeholders(trait_impl->impl_params)) {
                    return true;
                }
            }
            return params_have_candidate_placeholders(
                candidate.marker_impl_params
            );
        }

        static bool params_need_response_constraints(
            const ::HIR::PathParams& params
        ) {
            for (const auto& type : params.types) {
                bool found = false;
                visit_ty_with(type, [&](const ::HIR::TypeData* inner) {
                    if (const auto* generic = inner->opt_Generic()) {
                        found |= generic->group() == ::HIR::GENERICPlaceholder;
                    } else if (const auto* infer = inner->opt_Infer()) {
                        found |= !infer->is_lit();
                    }
                    return found;
                });
                if (found) {
                    return true;
                }
            }
            for (const auto& value : params.values) {
                if (value.is_Infer()
                    || (value.is_Generic()
                        && value.as_Generic().group()
                            == ::HIR::GENERICPlaceholder)) {
                    return true;
                }
            }
            return false;
        }

        bool candidateNeedsResponseConstraints(
            const Candidate& candidate
        ) const {
            if (const auto* trait_impl = candidate.impl.mData.opt_TraitImpl()) {
                return params_need_response_constraints(trait_impl->impl_params);
            }
            return candidate.marker_impl
                && params_need_response_constraints(candidate.marker_impl_params);
        }

        OrphanVisit orphan_visit_resolved_type(
            const ::HIR::TypeData* type,
            OrphanPerspective perspective
        ) const {
            if (type->is_Infer() || type->is_Generic()) {
                return perspective == OrphanPerspective::Remote
                    ? OrphanVisit::LocalKey
                    : OrphanVisit::Uncovered;
            }

            if (const auto* path = type->opt_Path()) {
                const auto* generic = path->path.mData.opt_Generic();
                const bool concreteAdt = generic
                    && (path->binding.is_Struct()
                        || path->binding.is_Enum()
                        || path->binding.is_Union()
                        || path->binding.is_ExternType());
                if (!concreteAdt) {
                    if (type_has_unknown(type)) {
                        return perspective == OrphanPerspective::Remote
                            ? OrphanVisit::LocalKey
                            : OrphanVisit::Uncovered;
                    }
                    return OrphanVisit::NonLocal;
                }

                const bool local = perspective == OrphanPerspective::Local
                    && generic->mPath.crate_name() == crate.crateName;
                if (local) {
                    return OrphanVisit::LocalKey;
                }

                const auto* str_ptr = path->binding.opt_Struct();
                if (str_ptr && (*str_ptr)->structMarkings.is_fundamental) {
                    for (const auto& param : generic->mParams.types) {
                        const auto result = orphan_visit_type(param, perspective);
                        if (result != OrphanVisit::NonLocal) {
                            return result;
                        }
                    }
                }
                return OrphanVisit::NonLocal;
            }

            if (const auto* borrow = type->opt_Borrow()) {
                // References are fundamental even though raw pointers are not.
                return orphan_visit_type(borrow->inner, perspective);
            }

            if (const auto* object = type->opt_TraitObject()) {
                const auto& principal = object->mTrait.mPath.mPath;
                if (perspective == OrphanPerspective::Local
                    && principal != ::HIR::SimplePath()
                    && principal.crate_name() == crate.crateName) {
                    return OrphanVisit::LocalKey;
                }
                return OrphanVisit::NonLocal;
            }

            if (type->is_NodeType()) {
                return perspective == OrphanPerspective::Local
                    ? OrphanVisit::LocalKey
                    : OrphanVisit::NonLocal;
            }

            if (type->is_ErasedType() && type_has_unknown(type)) {
                return perspective == OrphanPerspective::Remote
                    ? OrphanVisit::LocalKey
                    : OrphanVisit::Uncovered;
            }

            // Primitive, tuple, array, slice, raw-pointer, function, opaque,
            // and foreign rigid types are non-local and cover their contents.
            return OrphanVisit::NonLocal;
        }

        OrphanVisit orphan_visit_type(
            const ::HIR::TypeData* input,
            OrphanPerspective perspective
        ) const {
            const auto& resolved = mResolve.resolve_type(input);
            const auto* path = resolved->opt_Path();
            const bool is_alias = path
                && (!path->path.mData.is_Generic()
                    || path->binding.is_Unbound()
                    || path->binding.is_Opaque());
            if (is_alias) {
                // rustc's orphan checker normalizes aliases lazily.  Keep a
                // rigid alias if normalization only produces a fresh type
                // variable; such an alias still carries coverage information.
                auto normalized = mResolve.expand_associated_types(span(), resolved);
                if (!(normalized->is_Infer() && !resolved->is_Infer())) {
                    return orphan_visit_resolved_type(normalized, perspective);
                }
            }
            return orphan_visit_resolved_type(resolved, perspective);
        }

        bool orphan_check_trait_ref(
            const ::HIR::PathParams& params,
            const ::HIR::TypeData* type,
            OrphanPerspective perspective
        ) const {
            const auto self_result = orphan_visit_type(type, perspective);
            if (self_result != OrphanVisit::NonLocal) {
                return self_result == OrphanVisit::LocalKey;
            }
            for (const auto& param : params.types) {
                const auto result = orphan_visit_type(param, perspective);
                if (result != OrphanVisit::NonLocal) {
                    return result == OrphanVisit::LocalKey;
                }
            }
            return false;
        }

        bool trait_ref_is_knowable(
            const ::HIR::SimplePath& trait,
            const ::HIR::PathParams& params,
            const ::HIR::TypeData* type
        ) const {
            if (orphan_check_trait_ref(
                    params, type, OrphanPerspective::Remote
                )) {
                return false;
            }

            const auto& trait_def = crate.get_trait_by_path(span(), trait);
            if (trait.crate_name() == crate.crateName
                || trait_def.isFundamental) {
                return true;
            }

            return orphan_check_trait_ref(
                params, type, OrphanPerspective::Local
            );
        }

        static size_t hash_mix(size_t state, size_t value) {
            // boost::hash_combine's avalanche step.  Equality never relies on
            // this fingerprint: hash collisions are resolved by goal_matches.
            return state ^ (value + 0x9e3779b97f4a7c15ULL + (state << 6) + (state >> 2));
        }

        static size_t hash_simple_path(const ::HIR::SimplePath& path) {
            size_t result = ::std::hash<RcString>()(path.crate_name());
            for (const auto& component : path.components()) {
                result = hash_mix(result, ::std::hash<RcString>()(component));
            }
            return result;
        }

        static size_t hash_type(const ::HIR::TypeData* type) {
            if (const auto* path = type->get_sort_path()) {
                return hash_mix(0x10, hash_simple_path(*path));
            }
            if (const auto* primitive = type->opt_Primitive()) {
                return hash_mix(0x20, static_cast<size_t>(*primitive));
            }
            if (const auto* generic = type->opt_Generic()) {
                return hash_mix(0x30, generic->binding);
            }
            if (const auto* infer = type->opt_Infer()) {
                return hash_mix(0x40, infer->index);
            }
            if (const auto* tuple = type->opt_Tuple()) {
                size_t result = hash_mix(0x50, tuple->size());
                for (const auto& field : *tuple) {
                    result = hash_mix(result, hash_type(field));
                }
                return result;
            }
            if (const auto* array = type->opt_Array()) {
                return hash_mix(0x60, hash_type(array->inner));
            }
            if (const auto* slice = type->opt_Slice()) {
                return hash_mix(0x70, hash_type(slice->inner));
            }
            if (const auto* borrow = type->opt_Borrow()) {
                return hash_mix(hash_mix(0x80, static_cast<size_t>(borrow->type)), hash_type(borrow->inner));
            }
            if (const auto* pointer = type->opt_Pointer()) {
                return hash_mix(hash_mix(0x90, static_cast<size_t>(pointer->type)), hash_type(pointer->inner));
            }
            if (const auto* trait_object = type->opt_TraitObject()) {
                return hash_mix(0xa0, hash_simple_path(trait_object->mTrait.mPath.mPath));
            }
            if (type->is_Diverge()) {
                return 0xb0;
            }
            // Function, erased, and compiler-generated node types are rare in
            // recursive solver tables.  A stable tag is sufficient; full
            // equality below still resolves every collision correctly.
            return 0xc0;
        }

        static size_t goal_hash(
            const ::HIR::SimplePath& trait,
            const ::HIR::PathParams& params,
            const ::HIR::TypeData* type,
            const ::HIR::TraitPath::assocListT* associated
        ) {
            size_t result = hash_simple_path(trait);
            result = hash_mix(result, params.types.size());
            for (const auto& param : params.types) {
                result = hash_mix(result, hash_type(param));
            }
            result = hash_mix(result, params.values.size());
            result = hash_mix(result, hash_type(type));
            if (associated && !associated->empty()) {
                result = hash_mix(result, associated->size());
                for (const auto& entry : *associated) {
                    result = hash_mix(result, ::std::hash<RcString>()(entry.first));
                    result = hash_mix(result, hash_simple_path(entry.second.source_trait.mPath));
                    result = hash_mix(result, hash_type(entry.second.type));
                }
            }
            return result;
        }

        static ::HIR::TraitPath::assocListT cloneAssociated(
            const ::HIR::TraitPath::assocListT* associated
        ) {
            ::HIR::TraitPath::assocListT result;
            if (associated) {
                for (const auto& entry : *associated) {
                    result.insert({entry.first, entry.second.clone()});
                }
            }
            return result;
        }

        ImplRef monomorph_impl_ref(
            const ImplRef& source,
            const Monomorphiser& monomorph
        ) const {
            auto monomorph_associated = [&](
                const ::HIR::TraitPath::assocListT* associated
            ) {
                ::HIR::TraitPath::assocListT result;
                if (associated) {
                    for (const auto& entry : *associated) {
                        result.insert({
                            entry.first,
                            monomorph.monomorph_tp_aty_equal(
                                span(), entry.second, true
                            )
                        });
                    }
                }
                return result;
            };

            ImplRef result;
            if (const auto* impl = source.mData.opt_TraitImpl()) {
                ASSERT_BUG(
                    span(),
                    impl->trait_ptr && impl->trait_path && impl->impl,
                    "Cannot monomorphise an invalid trait impl response"
                );
                result = ImplRef(
                    monomorph.monomorph_path_params(
                        span(), impl->impl_params, true
                    ),
                    *impl->trait_ptr,
                    *impl->trait_path,
                    *impl->impl
                );
            } else if (const auto* bounded = source.mData.opt_BoundedPtr()) {
                result = ImplRef(
                    monomorph.monomorph_path_params(
                        span(), bounded->hrls, true
                    ),
                    monomorph.monomorph_type(span(), bounded->type, true),
                    monomorph.monomorph_path_params(
                        span(), *bounded->trait_args, true
                    ),
                    monomorph_associated(bounded->assoc)
                );
            } else {
                const auto& owned = source.mData.as_Bounded();
                result = ImplRef(
                    monomorph.monomorph_path_params(
                        span(), owned.hrls, true
                    ),
                    monomorph.monomorph_type(span(), owned.type, true),
                    monomorph.monomorph_path_params(
                        span(), owned.trait_args, true
                    ),
                    monomorph_associated(&owned.assoc)
                );
            }
            if (source.is_ambiguous_identity()) {
                result.mark_ambiguous_identity();
            }
            return result;
        }

        static bool goal_matches(
            const GoalKey& goal,
            const ::HIR::SimplePath& trait,
            const ::HIR::PathParams& params,
            const ::HIR::TypeData* type,
            const ::HIR::TraitPath::assocListT* associated
        ) {
            if (goal.trait != trait || goal.params != params || goal.type != type) {
                return false;
            }
            if (!associated || associated->empty()) {
                return goal.associated.empty();
            }
            if (goal.associated.size() != associated->size()) {
                return false;
            }
            auto left = goal.associated.begin();
            auto right = associated->begin();
            for (; left != goal.associated.end(); ++left, ++right) {
                if (left->first != right->first
                    || left->second.source_trait != right->second.source_trait
                    || left->second.atyParams != right->second.atyParams
                    || left->second.type != right->second.type) {
                    return false;
                }
            }
            return true;
        }

        CachedGoal* find_cached_goal(
            size_t hash,
            const ::HIR::SimplePath& trait,
            const ::HIR::PathParams& params,
            const ::HIR::TypeData* type,
            const ::HIR::TraitPath::assocListT* associated
        ) const {
            const auto range = goalCacheIndex.equal_range(hash);
            for (auto it = range.first; it != range.second; ++it) {
                if (goal_matches(it->second->goal, trait, params, type, associated)) {
                    return it->second;
                }
            }
            return nullptr;
        }

        GoalKey* find_active_goal(
            size_t hash,
            const ::HIR::SimplePath& trait,
            const ::HIR::PathParams& params,
            const ::HIR::TypeData* type,
            const ::HIR::TraitPath::assocListT* associated
        ) const {
            const auto range = activeGoalIndex.equal_range(hash);
            for (auto it = range.first; it != range.second; ++it) {
                if (goal_matches(*it->second, trait, params, type, associated)) {
                    return it->second;
                }
            }
            return nullptr;
        }

        GoalKey* push_active_goal(
            size_t hash,
            const ::HIR::SimplePath& trait,
            const ::HIR::PathParams& params,
            const ::HIR::TypeData* type,
            const ::HIR::TraitPath::assocListT* associated
        ) {
            auto* goal = activeGoalNodes.make(hash, trait, params, type, associated);
            goalStack.push_back(goal);
            activeGoalIndex.emplace(hash, goal);
            return goal;
        }

        void pop_active_goal(GoalKey* goal) {
            assert(!goalStack.empty() && goalStack.back() == goal);
            const auto range = activeGoalIndex.equal_range(goal->hash);
            for (auto it = range.first; it != range.second; ++it) {
                if (it->second == goal) {
                    activeGoalIndex.erase(it);
                    goalStack.pop_back();
                    activeGoalNodes.release(goal);
                    return;
                }
            }
            assert(!"next-solver active goal missing from hash index");
            ::std::abort();
        }

        Certainty cacheGoal(
            size_t hash,
            const ::HIR::SimplePath& trait,
            const ::HIR::PathParams& params,
            const ::HIR::TypeData* type,
            const ::HIR::TraitPath::assocListT* associated,
            Certainty certainty
        ) {
            auto* goal = cachedGoalNodes.make(
                hash, trait, params, type, associated, certainty
            );
            goalCache.push_back(goal);
            goalCacheIndex.emplace(hash, goal);
            return certainty;
        }

        CachedGoal* cacheResponse(
            size_t hash,
            const ::HIR::SimplePath& trait,
            const ::HIR::PathParams& params,
            const ::HIR::TypeData* type,
            const ::HIR::TraitPath::assocListT* associated,
            ImplRef response,
            ::HIR::Compare response_certainty
        ) {
            auto* cached = find_cached_goal(
                hash, trait, params, type, associated
            );
            const auto certainty = response_certainty == ::HIR::Compare::Equal
                ? Certainty::Proven
                : Certainty::Ambiguous;
            if (!cached) {
                cached = cachedGoalNodes.make(
                    hash, trait, params, type, associated, certainty
                );
                goalCache.push_back(cached);
                goalCacheIndex.emplace(hash, cached);
            }
            cached->certainty = certainty;
            cached->response = ::std::move(response);
            cached->response_certainty = response_certainty;
            cached->has_response = true;
            return cached;
        }

        void clearGoalCache() {
            goalCacheIndex.clear();
            for (auto* goal : goalCache) {
                cachedGoalNodes.release(goal);
            }
            goalCache.clear();
        }

        static const ::HIR::PathParams& boundedHrls(const ImplRef& impl) {
            if (const auto* bounded = impl.mData.opt_BoundedPtr()) {
                return bounded->hrls;
            }
            return impl.mData.as_Bounded().hrls;
        }

        static const ::HIR::TraitPath::assocListT& boundedAssociated(
            const ImplRef& impl
        ) {
            if (const auto* bounded = impl.mData.opt_BoundedPtr()) {
                return *bounded->assoc;
            }
            return impl.mData.as_Bounded().assoc;
        }

        static bool associatedResponsesEqual(
            const ::HIR::TraitPath::assocListT& left,
            const ::HIR::TraitPath::assocListT& right
        ) {
            if (left.size() != right.size()) {
                return false;
            }
            auto li = left.begin();
            auto ri = right.begin();
            for (; li != left.end(); ++li, ++ri) {
                if (li->first != ri->first
                    || li->second.ord(ri->second) != OrdEqual) {
                    return false;
                }
            }
            return true;
        }

        bool is_same_impl(const ImplRef& left, const ImplRef& right) const {
            const auto* li = left.mData.opt_TraitImpl();
            const auto* ri = right.mData.opt_TraitImpl();
            if (li || ri) {
                return li && ri && li->impl == ri->impl && li->impl_params == ri->impl_params;
            }
            return left.get_impl_type(crate.types) == right.get_impl_type(crate.types)
                && left.get_trait_params(crate.types) == right.get_trait_params(crate.types)
                && boundedHrls(left) == boundedHrls(right)
                && associatedResponsesEqual(
                    boundedAssociated(left), boundedAssociated(right)
                );
        }

        bool param_env_candidate_is_non_global(const Candidate& candidate) const {
            if (candidate.source != CandidateSource::ParamEnv) {
                return false;
            }
            if (type_has_unknown(candidate.impl.get_impl_type(crate.types))
                || params_have_unknown_types(
                    candidate.impl.get_trait_params(crate.types)
                )) {
                return true;
            }
            for (const auto& associated : boundedAssociated(candidate.impl)) {
                if (params_have_unknown_types(
                        associated.second.source_trait.mParams
                    )
                    || params_have_unknown_types(associated.second.atyParams)
                    || type_has_unknown(associated.second.type)) {
                    return true;
                }
            }
            return false;
        }

        void push_candidate(
            size_t frame_index,
            ImplRef impl,
            ::HIR::Compare match,
            const ::HIR::MarkerImpl* marker_impl = nullptr,
            ::HIR::PathParams marker_impl_params = {},
            bool autoBuiltin = false,
            CandidateSource source = CandidateSource::Other
        ) {
            if (match == ::HIR::Compare::Unequal) {
                return;
            }
            auto& candidates = frames[frame_index]->candidates;
            for (size_t i = 0; i < candidates.size(); i++) {
                const bool same_source = candidates[i]->marker_impl == marker_impl
                    && candidates[i]->autoBuiltin == autoBuiltin
                    && candidates[i]->source == source;
                const bool same = marker_impl
                    ? same_source
                        && candidates[i]->marker_impl_params == marker_impl_params
                    : same_source && is_same_impl(candidates[i]->impl, impl);
                if (same) {
                    candidates[i]->head_match &= match;
                    return;
                }
            }
            candidates.push_back(candidateNodes.make(
                ::std::move(impl),
                match,
                marker_impl,
                ::std::move(marker_impl_params),
                autoBuiltin,
                source
            ));
        }

        void assembleCandidates(
            size_t frame_index,
            const ::HIR::SimplePath& trait,
            const ::HIR::PathParams& params,
            const ::HIR::TypeData* type
        ) {
            auto collect = [&](CandidateSource source) {
                return [&, source](ImplRef impl, ::HIR::Compare match) {
                    push_candidate(
                        frame_index, ::std::move(impl), match,
                        nullptr, {}, false, source
                    );
                    return false;
                };
            };

            // Candidate source is semantically significant: a non-global
            // ParamEnv predicate shadows builtin and impl candidates in the
            // next solver.  The legacy lookup flattened these sources into
            // the same bounded ImplRef, so collect each source independently.
            mResolve.find_trait_impls_magic(
                span(), trait, params, type,
                collect(CandidateSource::Builtin)
            );
            mResolve.find_trait_impls_legacy(
                span(), trait, params, type,
                collect(CandidateSource::Other), false, false, false
            );
            mResolve.find_trait_impls_bound(
                span(), trait, params, type,
                collect(CandidateSource::ParamEnv)
            );

            const auto& resolved_type = mResolve.resolve_type(type);
            const auto& trait_def = crate.get_trait_by_path(span(), trait);
            if (!trait_def.isMarker) {
                // Assemble impl heads without evaluating their where-clauses.
                // Those nested goals belong exclusively to evaluate_candidate.
                crate.find_trait_impls(
                    trait,
                    resolved_type,
                    mResolve.ivars.callbackResolveInfer(),
                    [&](const ::HIR::TraitImpl& impl) {
                        ::HIR::PathParams impl_params;
                        const auto match = mResolve.ftic_check_params(
                            span(),
                            trait,
                            &params,
                            resolved_type,
                            impl.mParams,
                            impl.traitArgs,
                            impl.mType,
                            impl_params,
                            false
                        );
                        if (match != ::HIR::Compare::Unequal) {
                            push_candidate(
                                frame_index,
                                ImplRef(
                                    ::std::move(impl_params),
                                    trait_def,
                                    trait,
                                    impl
                                ),
                                match,
                                nullptr,
                                {},
                                false,
                                CandidateSource::TraitImpl
                            );
                        }
                        return false;
                    }
                );
            } else {
                // Explicit positive and negative auto-trait impls are
                // candidates with polarity.  Only their heads are matched
                // here; their bounds are nested goals evaluated below.
                crate.find_auto_trait_impls(
                    trait,
                    resolved_type,
                    mResolve.ivars.callbackResolveInfer(),
                    [&](const ::HIR::MarkerImpl& impl) {
                        ::HIR::PathParams impl_params;
                        const auto match = mResolve.ftic_check_params(
                            span(),
                            trait,
                            &params,
                            resolved_type,
                            impl.mParams,
                            impl.traitArgs,
                            impl.mType,
                            impl_params,
                            false
                        );
                        if (match != ::HIR::Compare::Unequal) {
                            auto monomorph = MonomorphStatePtr(
                                crate.types, nullptr, &impl_params, nullptr
                            );
                            auto response_type = monomorph.monomorph_type(
                                span(), impl.mType, false
                            );
                            auto response_params = monomorph.monomorph_path_params(
                                span(), impl.traitArgs, false
                            );
                            push_candidate(
                                frame_index,
                                ImplRef(
                                    ::std::move(response_type),
                                    ::std::move(response_params),
                                    ::HIR::TraitPath::assocListT()
                                ),
                                match,
                                &impl,
                                ::std::move(impl_params),
                                false,
                                CandidateSource::TraitImpl
                            );
                        }
                        return false;
                    }
                );

                // The structural auto candidate is evaluated recursively in
                // evaluate_candidate, after explicit polarity is known.
                push_candidate(
                    frame_index,
                    ImplRef(
                        resolved_type,
                        params.clone(),
                        ::HIR::TraitPath::assocListT()
                    ),
                    mResolve.type_contains_ivars(resolved_type)
                        || mResolve.params_contain_ivars(params)
                        ? ::HIR::Compare::Fuzzy
                        : ::HIR::Compare::Equal,
                    nullptr,
                    {},
                    true,
                    CandidateSource::Builtin
                );
            }
        }

        ::HIR::TypeRef make_associated_projection(
            const ::HIR::TypeData* type,
            const ::HIR::GenericPath& source_trait,
            const RcString& name,
            const ::HIR::PathParams& associatedParams
        ) const {
            return crate.types.path(
                ::HIR::Path(
                    type,
                    source_trait.clone(),
                    name,
                    associatedParams.clone()
                ),
                ::HIR::TypePathBinding::make_Opaque({})
            );
        }

        ::HIR::TypeRef make_associated_projection(
            const ImplRef& impl,
            const ::HIR::GenericPath& source_trait,
            const RcString& name,
            const ::HIR::PathParams& associatedParams
        ) const {
            return make_associated_projection(
                impl.get_impl_type(crate.types), source_trait, name, associatedParams
            );
        }

        bool bindCandidatePlaceholders(
            Candidate& candidate,
            const ::HIR::TypeData* nested_type,
            const ::HIR::TraitPath::assocListT& associated,
            bool use_candidate_response = false
        ) {
            ::HIR::PathParams* candidateParams = nullptr;
            if (auto* trait_impl = candidate.impl.mData.opt_TraitImpl()) {
                candidateParams = &trait_impl->impl_params;
            } else if (candidate.marker_impl) {
                candidateParams = &candidate.marker_impl_params;
            }
            if (!candidateParams || associated.empty()) {
                return false;
            }

            class BindPlaceholders final: public ::HIR::MatchGenerics {
                const Span& mSpan;
                ::HIR::TypeInterner& types;
                ::HIR::PathParams& mParams;
                ::std::vector<::std::pair<::HIR::TypeRef, ::HIR::TypeRef>> mBindings;

                bool is_bindable(const ::HIR::TypeData* type) const {
                    if (const auto* generic = type->opt_Generic()) {
                        return generic->group() == ::HIR::GENERICPlaceholder;
                    }
                    if (const auto* infer = type->opt_Infer()) {
                        return !infer->is_lit();
                    }
                    return false;
                }

                ::std::optional<::HIR::Compare> bindType(
                    const ::HIR::TypeData* pattern,
                    const ::HIR::TypeData* value,
                    ::HIR::t_cb_resolve_type resolve
                ) {
                    for (const auto& binding : mBindings) {
                        if (binding.first == pattern) {
                            return binding.second->compareWithPlaceholders(
                                mSpan, value, resolve
                            );
                        }
                    }
                    if (!is_bindable(pattern)) {
                        return {};
                    }
                    bool is_parameter = false;
                    for (const auto& parameter : mParams.types) {
                        is_parameter |= visit_ty_with(
                            parameter,
                            [&](const ::HIR::TypeData* inner) {
                                return inner == pattern;
                            }
                        );
                    }
                    if (!is_parameter) {
                        return {};
                    }
                    if (pattern == value) {
                        return ::HIR::Compare::Equal;
                    }
                    for (auto& parameter : mParams.types) {
                        parameter = cloneTyWith(
                            types,
                            mSpan,
                            parameter,
                            [&](const ::HIR::TypeData* input,
                                ::HIR::TypeRef& output) {
                                if (input != pattern) {
                                    return false;
                                }
                                output = value;
                                return true;
                            }
                        );
                    }
                    mBindings.push_back({pattern, value});
                    changed = true;
                    return ::HIR::Compare::Equal;
                }

            public:
                bool changed = false;

                BindPlaceholders(
                    const Span& span,
                    ::HIR::TypeInterner& types,
                    ::HIR::PathParams& params
                )
                    : mSpan(span)
                    , types(types)
                    , mParams(params)
                {
                }

                ::HIR::Compare cmpType(
                    const Span& span,
                    const ::HIR::TypeData* pattern,
                    const ::HIR::TypeData* value,
                    ::HIR::t_cb_resolve_type resolve
                ) override {
                    if (auto result = bindType(pattern, value, resolve)) {
                        return *result;
                    }
                    return ::HIR::MatchGenerics::cmpType(
                        span, pattern, value, resolve
                    );
                }

                ::HIR::Compare match_ty(
                    const ::HIR::GenericRef& generic,
                    const ::HIR::TypeData* type,
                    ::HIR::t_cb_resolve_type resolve
                ) override {
                    const auto pattern = types.generic(
                        generic.name, generic.binding
                    );
                    if (auto result = bindType(pattern, type, resolve)) {
                        return *result;
                    }
                    return pattern->compareWithPlaceholders(
                        mSpan, type, resolve
                    );
                }

                ::HIR::Compare match_val(
                    const ::HIR::GenericRef& generic,
                    const ::HIR::ConstGeneric& value
                ) override {
                    if (value.is_Generic() && value.as_Generic() == generic) {
                        return ::HIR::Compare::Equal;
                    }
                    if (generic.group() == ::HIR::GENERICPlaceholder) {
                        for (auto& parameter : mParams.values) {
                            if (parameter.is_Generic()
                                && parameter.as_Generic() == generic) {
                                parameter = value.clone();
                                changed = true;
                                return ::HIR::Compare::Equal;
                            }
                        }
                    }
                    return ::HIR::Compare::Fuzzy;
                }
            } binder{span(), crate.types, *candidateParams};

            for (const auto& requirement : associated) {
                const auto saved = candidateParams->clone();
                auto candidateOutput = use_candidate_response
                    ? candidate.impl.get_type(
                        crate.types, requirement.first.c_str(), requirement.second.atyParams
                    )
                    : ::HIR::TypeRef();
                if (!use_candidate_response) {
                    // An impl parameter can occur only in a nested projection
                    // equality (for example `I: Iterator<Item = &'a T>`).
                    // Ask the solver for that projection's actual response so
                    // `T` is bound to the response, not to the alias syntax.
                    evaluate(
                        span(),
                        requirement.second.source_trait.mPath,
                        requirement.second.source_trait.mParams,
                        nested_type,
                        [&](ImplRef impl, ::HIR::Compare certainty) {
                            if (certainty != ::HIR::Compare::Equal
                                || impl.is_ambiguous_identity()) {
                                return false;
                            }
                            auto output = impl.get_type(
                                crate.types,
                                requirement.first.c_str(),
                                requirement.second.atyParams
                            );
                            if (output == ::HIR::TypeRef()) {
                                return false;
                            }
                            candidateOutput = ::std::move(output);
                            return true;
                        },
                        requirement.first.c_str(),
                        nullptr,
                        &requirement.second.atyParams
                    );
                }
                if (candidateOutput == ::HIR::TypeRef()) {
                    candidateOutput = make_associated_projection(
                        nested_type,
                        requirement.second.source_trait,
                        requirement.first,
                        requirement.second.atyParams
                    );
                }
                if (!use_candidate_response
                    && !type_has_ufcs_unknown(candidateOutput)) {
                    // rustc normalises a nested projection response before it
                    // is unified with the outer candidate.  In particular,
                    // `<&mut I as Iterator>::Item` first becomes
                    // `<I as Iterator>::Item` and then the ParamEnv equality
                    // `&T`; matching the unnormalised alias against
                    // `&placeholder` only reports a fuzzy relation and loses
                    // the constraint.  A candidate's own response is not a
                    // nested solver response: during Resolve UFCS Outer either
                    // form can still legally contain a local UfcsUnknown, and
                    // such a response must remain deferred until that pass
                    // resolves its trait path.
                    candidateOutput = mResolve.expand_associated_types(
                        span(), ::std::move(candidateOutput)
                    );
                }
                const auto match = (use_candidate_response
                    ? candidateOutput
                    : requirement.second.type)->match_test_generics_fuzz(
                    span(),
                    use_candidate_response
                        ? requirement.second.type
                        : candidateOutput,
                    mResolve.ivars.callbackResolveInfer(),
                    binder
                );
                if (match == ::HIR::Compare::Unequal) {
                    *candidateParams = saved.clone();
                }
            }

            if (binder.changed && candidate.marker_impl) {
                auto monomorph = MonomorphStatePtr(
                    crate.types, nullptr, &candidate.marker_impl_params, nullptr
                );
                auto& response = candidate.impl.mData.as_Bounded();
                response.type = monomorph.monomorph_type(
                    span(), candidate.marker_impl->mType, false
                );
                response.trait_args = monomorph.monomorph_path_params(
                    span(), candidate.marker_impl->traitArgs, false
                );
            }
            return binder.changed;
        }

        bool bindCandidateResponse(
            Candidate& candidate,
            const ::HIR::TypeData* nested_type,
            const ::HIR::PathParams& nested_params,
            const ImplRef& response
        ) {
            ::HIR::PathParams* candidateParams = nullptr;
            if (auto* trait_impl = candidate.impl.mData.opt_TraitImpl()) {
                candidateParams = &trait_impl->impl_params;
            } else if (candidate.marker_impl) {
                candidateParams = &candidate.marker_impl_params;
            }
            if (!candidateParams || response.is_ambiguous_identity()) {
                return false;
            }

            class BindResponse final: public ::HIR::MatchGenerics {
                const Span& mSpan;
                ::HIR::TypeInterner& types;
                ::HIR::PathParams& mParams;
                ::std::vector<::std::pair<::HIR::TypeRef, ::HIR::TypeRef>> mBindings;

                bool is_bindable(const ::HIR::TypeData* type) const {
                    if (const auto* generic = type->opt_Generic()) {
                        return generic->group() == ::HIR::GENERICPlaceholder;
                    }
                    if (const auto* infer = type->opt_Infer()) {
                        return !infer->is_lit();
                    }
                    return false;
                }

                ::std::optional<::HIR::Compare> bindType(
                    const ::HIR::TypeData* pattern,
                    const ::HIR::TypeData* value,
                    ::HIR::t_cb_resolve_type resolve
                ) {
                    for (const auto& binding : mBindings) {
                        if (binding.first == pattern) {
                            return binding.second->compareWithPlaceholders(
                                mSpan, value, resolve
                            );
                        }
                    }
                    if (!is_bindable(pattern)) {
                        return {};
                    }
                    bool is_parameter = false;
                    for (const auto& parameter : mParams.types) {
                        is_parameter |= visit_ty_with(
                            parameter,
                            [&](const ::HIR::TypeData* inner) {
                                return inner == pattern;
                            }
                        );
                    }
                    if (!is_parameter) {
                        return {};
                    }
                    if (pattern == value) {
                        return ::HIR::Compare::Equal;
                    }
                    for (auto& parameter : mParams.types) {
                        parameter = cloneTyWith(
                            types,
                            mSpan,
                            parameter,
                            [&](const ::HIR::TypeData* input,
                                ::HIR::TypeRef& output) {
                                if (input != pattern) {
                                    return false;
                                }
                                output = value;
                                return true;
                            }
                        );
                    }
                    mBindings.push_back({pattern, value});
                    changed = true;
                    return ::HIR::Compare::Equal;
                }

            public:
                bool changed = false;

                BindResponse(
                    const Span& span,
                    ::HIR::TypeInterner& types,
                    ::HIR::PathParams& params
                )
                    : mSpan(span)
                    , types(types)
                    , mParams(params)
                {
                }

                ::HIR::Compare cmpType(
                    const Span& span,
                    const ::HIR::TypeData* pattern,
                    const ::HIR::TypeData* value,
                    ::HIR::t_cb_resolve_type resolve
                ) override {
                    if (auto result = bindType(pattern, value, resolve)) {
                        return *result;
                    }
                    return ::HIR::MatchGenerics::cmpType(
                        span, pattern, value, resolve
                    );
                }

                ::HIR::Compare match_ty(
                    const ::HIR::GenericRef& generic,
                    const ::HIR::TypeData* value,
                    ::HIR::t_cb_resolve_type resolve
                ) override {
                    const auto pattern = types.generic(
                        generic.name, generic.binding
                    );
                    if (auto result = bindType(pattern, value, resolve)) {
                        return *result;
                    }
                    return pattern->compareWithPlaceholders(
                        mSpan, value, resolve
                    );
                }

                ::HIR::Compare match_val(
                    const ::HIR::GenericRef& generic,
                    const ::HIR::ConstGeneric& value
                ) override {
                    if (value.is_Generic() && value.as_Generic() == generic) {
                        return ::HIR::Compare::Equal;
                    }
                    if (generic.group() != ::HIR::GENERICPlaceholder) {
                        return ::HIR::Compare::Fuzzy;
                    }
                    for (auto& parameter : mParams.values) {
                        if (parameter.is_Generic()
                            && parameter.as_Generic() == generic) {
                            parameter = value.clone();
                            changed = true;
                            return ::HIR::Compare::Equal;
                        }
                    }
                    return ::HIR::Compare::Fuzzy;
                }
            } binder{span(), crate.types, *candidateParams};

            const auto saved = candidateParams->clone();
            auto match = nested_type->match_test_generics_fuzz(
                span(),
                response.get_impl_type(crate.types),
                mResolve.ivars.callbackResolveInfer(),
                binder
            );
            match &= nested_params.match_test_generics_fuzz(
                span(),
                response.get_trait_params(crate.types),
                mResolve.ivars.callbackResolveInfer(),
                binder
            );
            if (match == ::HIR::Compare::Unequal) {
                *candidateParams = saved.clone();
                return false;
            }

            if (binder.changed && candidate.marker_impl) {
                auto monomorph = MonomorphStatePtr(
                    crate.types, nullptr, &candidate.marker_impl_params, nullptr
                );
                auto& bounded = candidate.impl.mData.as_Bounded();
                bounded.type = monomorph.monomorph_type(
                    span(), candidate.marker_impl->mType, false
                );
                bounded.trait_args = monomorph.monomorph_path_params(
                    span(), candidate.marker_impl->traitArgs, false
                );
            }
            return binder.changed;
        }

        Certainty match_associated_types(
            const ::HIR::SimplePath& trait,
            const ImplRef& impl,
            const ::HIR::TraitPath::assocListT* associated
        ) {
            if (!associated || associated->empty()) {
                return Certainty::Proven;
            }

            Certainty result = Certainty::Proven;
            for (const auto& requirement : *associated) {
                const auto& aty = requirement.second;
                if (!impl.mData.is_TraitImpl() && aty.atyParams.has_params()) {
                    // Bounded candidates currently store non-GAT projections.
                    // They remain a valid but non-guiding response instead of
                    // being rejected or calling ImplRef's non-GAT assertion.
                    result = Certainty::Ambiguous;
                    continue;
                }
                auto output = impl.get_type(crate.types, requirement.first.c_str(), aty.atyParams);
                if (output == ::HIR::TypeRef()) {
                    if (aty.source_trait.mPath != trait) {
                        ::HIR::TraitPath::assocListT source_associated;
                        source_associated.insert({
                            requirement.first,
                            requirement.second.clone()
                        });
                        const auto source_result = solve_goal(
                            aty.source_trait.mPath,
                            aty.source_trait.mParams,
                            impl.get_impl_type(crate.types),
                            &source_associated
                        );
                        if (source_result == Certainty::NoSolution) {
                            return Certainty::NoSolution;
                        }
                        if (source_result == Certainty::Ambiguous) {
                            result = Certainty::Ambiguous;
                        }
                        continue;
                    }
                    if (impl.mData.is_TraitImpl()) {
                        result = Certainty::Ambiguous;
                        continue;
                    }
                    // A ParamEnv predicate without an explicit equality still
                    // has a canonical projection response.  This is what lets
                    // `T: Trait` prove a nested `T: Trait<Assoc = U>` while
                    // constraining U to `<T as Trait>::Assoc`.
                    output = make_associated_projection(
                        impl,
                        aty.source_trait,
                        requirement.first,
                        aty.atyParams
                    );
                }
                // The projection response may contain the very caller-owned
                // inference variable from the requested equality. That is an
                // exact response, not an ambiguous comparison of two ivars.
                const auto cmp = output == aty.type
                    ? ::HIR::Compare::Equal
                    : mResolve.compareTy(span(), output, aty.type);
                if (cmp == ::HIR::Compare::Unequal) {
                    return Certainty::NoSolution;
                }
                if (cmp == ::HIR::Compare::Fuzzy) {
                    result = Certainty::Ambiguous;
                }
            }
            return result;
        }

        Certainty evaluate_auto_builtin(
            const ::HIR::SimplePath& trait,
            const ::HIR::PathParams& params,
            const ::HIR::TypeData* type
        ) {
            auto combine = [](Certainty& result, Certainty nested) {
                if (nested == Certainty::NoSolution) {
                    result = Certainty::NoSolution;
                } else if (nested == Certainty::Ambiguous
                           && result == Certainty::Proven) {
                    result = Certainty::Ambiguous;
                }
            };
            auto evaluate_inner = [&](const ::HIR::TypeData* inner) {
                return solve_goal(trait, params, inner, nullptr);
            };

            TU_MATCH_HDRA((*type), {)
            default:
                return Certainty::Proven;
            TU_ARMA(Path, e) {
                if (const auto* pe = e.path.mData.opt_Generic()) {
                    ::HIR::TypeRef tmp;
                    auto monomorph = MonomorphStatePtr(
                        crate.types, nullptr, &pe->mParams, nullptr
                    );
                    auto evaluate_field = [&](const ::HIR::TypeData* field) {
                        const auto& field_type = monomorphise_type_needed(field)
                            ? (tmp = mResolve.expand_associated_types(
                                span(), monomorph.monomorph_type(span(), field)
                            ))
                            : field;
                        return evaluate_inner(field_type);
                    };

                    if (e.binding.is_Unbound() || e.binding.is_Opaque()) {
                        return Certainty::Ambiguous;
                    }
                    Certainty result = Certainty::Proven;
                    if (const auto* str_ptr = e.binding.opt_Struct()) {
                        const auto& str = **str_ptr;
                        TU_MATCH(
                            ::HIR::Struct::Data,
                            (str.mData),
                            (se),
                            (Unit, ),
                            (Tuple,
                             for (const auto& field : se) {
                                 combine(result, evaluate_field(field.ent));
                                 if (result == Certainty::NoSolution) {
                                     return result;
                                 }
                             }),
                            (Named,
                             for (const auto& field : se) {
                                 combine(result, evaluate_field(field.ty));
                                 if (result == Certainty::NoSolution) {
                                     return result;
                                 }
                             })
                        )
                    } else if (const auto* enm_ptr = e.binding.opt_Enum()) {
                        const auto& enm = **enm_ptr;
                        if (const auto* variants = enm.mData.opt_Data()) {
                            for (const auto& variant : *variants) {
                                combine(result, evaluate_field(variant.type));
                                if (result == Certainty::NoSolution) {
                                    return result;
                                }
                            }
                        }
                    } else if (const auto* unn_ptr = e.binding.opt_Union()) {
                        const auto& unn = **unn_ptr;
                        for (const auto& field : unn.mVariants) {
                            combine(result, evaluate_field(field.ty));
                            if (result == Certainty::NoSolution) {
                                return result;
                            }
                        }
                    } else if (e.binding.is_ExternType()) {
                        return Certainty::NoSolution;
                    }
                    return result;
                }
                if (e.path.mData.is_UfcsKnown()
                    && (e.binding.is_Unbound() || e.binding.is_Opaque())) {
                    return Certainty::Ambiguous;
                }
                return Certainty::Ambiguous;
            }
            TU_ARMA(Generic, e) {
                return evaluate_inner(type);
            }
            TU_ARMA(Tuple, e) {
                Certainty result = Certainty::Proven;
                for (const auto& field : e) {
                    combine(result, evaluate_inner(field));
                    if (result == Certainty::NoSolution) {
                        return result;
                    }
                }
                return result;
            }
            TU_ARMA(Array, e) {
                return evaluate_inner(e.inner);
            }
            }
            throw "";
        }

        Certainty evaluate_candidate(
            size_t frame_index,
            size_t candidateIndex,
            const ::HIR::SimplePath& trait,
            const ::HIR::TraitPath::assocListT* associated
        ) {
            auto* candidate = frames[frame_index]->candidates[candidateIndex];
            candidate->ambiguityBeyondHead = false;
            if (associated) {
                bindCandidatePlaceholders(
                    *candidate,
                    candidate->impl.get_impl_type(crate.types),
                    *associated,
                    true
                );
            }
            const bool environment_response_constraint =
                candidate->head_match == ::HIR::Compare::Fuzzy
                && is_environment_or_builtin(candidate->impl)
                && !candidateHasPlaceholders(*candidate);
            auto result = candidate->head_match == ::HIR::Compare::Equal
                    || environment_response_constraint
                ? Certainty::Proven : Certainty::Ambiguous;

            const bool autoBuiltin = candidate->autoBuiltin;
            const auto* marker_impl = candidate->marker_impl;
            if (autoBuiltin) {
                const auto& response = candidate->impl.mData.as_Bounded();
                const auto structural = evaluate_auto_builtin(
                    trait, response.trait_args, response.type
                );
                if (structural == Certainty::NoSolution) {
                    return Certainty::NoSolution;
                }
                if (structural == Certainty::Ambiguous) {
                    candidate->ambiguityBeyondHead = true;
                    result = Certainty::Ambiguous;
                }
            }

            const auto assocResult = match_associated_types(
                trait, candidate->impl, associated
            );
            if (assocResult == Certainty::NoSolution) {
                return Certainty::NoSolution;
            }
            if (assocResult == Certainty::Ambiguous) {
                candidate->ambiguityBeyondHead = true;
                result = Certainty::Ambiguous;
            }

            const auto* trait_impl = candidate->impl.mData.opt_TraitImpl();
            const ::HIR::GenericParams* impl_params_def = marker_impl
                ? &marker_impl->mParams
                : (trait_impl && trait_impl->impl
                    ? &trait_impl->impl->mParams
                    : nullptr);
            if (!impl_params_def) {
                return result;
            }

            for (const auto& bound : impl_params_def->bounds) {
                if (const auto* be = bound.opt_TraitBound()) {
                    ::HIR::TypeRef nested_type;
                    ::HIR::SimplePath nested_trait;
                    ::HIR::PathParams nested_params;
                    ::HIR::TraitPath::assocListT nested_associated;

                    // Candidate and response storage is pool-backed, so nested
                    // goals cannot relocate this parent slot.
                    auto monomorph_bound = [&](auto& ms) {
                        static const ::HIR::GenericParams no_hrbs;
                        const bool outer_present = be->hrtbs && !be->hrtbs->is_empty();
                        auto hrb_guard = ms.push_hrb(outer_present ? *be->hrtbs : no_hrbs);
                        auto boundType = ms.monomorph_type(span(), be->type);
                        auto boundTrait = ms.monomorph_traitpath(span(), be->trait, true);

                        // Unlike the legacy solver, keep higher-ranked
                        // lifetimes as placeholders while evaluating this
                        // nested goal. Erasing them to `'#omitted` here loses
                        // the universe boundary and makes leak checking
                        // impossible (`for<'b> 'b: 'a` appears unconstrained).
                        const auto hrl_params = outer_present
                            ? be->hrtbs->make_nop_params(
                                crate.types,
                                ::HIR::GENERICHrtb,
                                true
                            )
                            : (boundTrait.hrtbs
                                ? boundTrait.hrtbs->make_nop_params(
                                    crate.types,
                                    ::HIR::GENERICHrtb,
                                    true
                                )
                                : ::HIR::PathParams());
                        auto hrl_monomorph = MonomorphHrlsOnly(crate.types, hrl_params);
                        nested_type = hrl_monomorph.monomorph_type(span(), boundType, true);
                        nested_trait = boundTrait.mPath.mPath;
                        nested_params = hrl_monomorph.monomorph_path_params(
                            span(), boundTrait.mPath.mParams, true
                        );
                        for (const auto& aty : boundTrait.typeBounds) {
                            auto value = aty.second.clone();
                            value.type = hrl_monomorph.monomorph_type(span(), value.type, true);
                            value.atyParams = hrl_monomorph.monomorph_path_params(
                                span(), value.atyParams, true
                            );
                            nested_associated.insert({aty.first, ::std::move(value)});
                        }
                    };
                    if (marker_impl) {
                        auto ms = MonomorphStatePtr(
                            crate.types,
                            nullptr,
                            &candidate->marker_impl_params,
                            nullptr
                        );
                        monomorph_bound(ms);
                    } else {
                        auto ms = candidate->impl.get_cb_monomorph_traitimpl(crate.types, span(), {});
                        monomorph_bound(ms);
                    }

                    // An impl parameter may occur only in an associated-type
                    // equality of a nested goal.  Canonical solvers infer that
                    // parameter from the projection response of the nested
                    // goal; preserve the same response in our impl parameters
                    // before evaluating the goal itself.
                    if (bindCandidatePlaceholders(
                            *candidate, nested_type, nested_associated
                        )) {
                        nested_associated.clear();
                        if (marker_impl) {
                            auto ms = MonomorphStatePtr(
                                crate.types,
                                nullptr,
                                &candidate->marker_impl_params,
                                nullptr
                            );
                            monomorph_bound(ms);
                        } else {
                            auto ms = candidate->impl.get_cb_monomorph_traitimpl(
                                crate.types, span(), {}
                            );
                            monomorph_bound(ms);
                        }
                    }

                    // The certainty-only table is the fast path for the vast
                    // majority of nested obligations and also validates all
                    // associated-type constraints. Only an ambiguous goal
                    // whose response can still bind this candidate needs the
                    // more expensive canonical response assembly.
                    auto nested = solve_goal(
                        nested_trait,
                        nested_params,
                        nested_type,
                        &nested_associated
                    );
                    if (nested == Certainty::NoSolution) {
                        return Certainty::NoSolution;
                    }
                    if (nested == Certainty::Ambiguous
                        && candidateNeedsResponseConstraints(*candidate)) {
                        Certainty response_certainty = Certainty::NoSolution;
                        const bool has_response = evaluate(
                            span(),
                            nested_trait,
                            nested_params,
                            nested_type,
                            [&](ImplRef response, ::HIR::Compare certainty) {
                                bindCandidateResponse(
                                    *candidate,
                                    nested_type,
                                    nested_params,
                                    response
                                );
                                response_certainty = certainty
                                        == ::HIR::Compare::Equal
                                    ? Certainty::Proven
                                    : Certainty::Ambiguous;
                                return true;
                            },
                            "",
                            nullptr,
                            nullptr
                        );
                        if (!has_response) {
                            return Certainty::NoSolution;
                        }
                        nested = response_certainty;
                    }
                    if (nested == Certainty::Ambiguous) {
                        candidate->ambiguityBeyondHead = true;
                        result = Certainty::Ambiguous;
                    }
                } else if (const auto* equality = bound.opt_TypeEquality()) {
                    ::HIR::TypeRef left;
                    ::HIR::TypeRef right;
                    if (marker_impl) {
                        auto ms = MonomorphStatePtr(
                            crate.types,
                            nullptr,
                            &candidate->marker_impl_params,
                            nullptr
                        );
                        left = ms.monomorph_type(span(), equality->type);
                        right = ms.monomorph_type(span(), equality->other_type);
                    } else {
                        auto ms = candidate->impl.get_cb_monomorph_traitimpl(crate.types, span(), {});
                        left = ms.monomorph_type(span(), equality->type);
                        right = ms.monomorph_type(span(), equality->other_type);
                    }
                    const auto cmp = mResolve.compareTy(span(), left, right);
                    if (cmp == ::HIR::Compare::Unequal) {
                        return Certainty::NoSolution;
                    }
                    if (cmp == ::HIR::Compare::Fuzzy) {
                        candidate->ambiguityBeyondHead = true;
                        result = Certainty::Ambiguous;
                    }
                } else if (const auto* lifetime = bound.opt_Lifetime()) {
                    ::HIR::LifetimeRef test;
                    ::HIR::LifetimeRef valid_for;
                    if (marker_impl) {
                        auto ms = MonomorphStatePtr(
                            crate.types,
                            nullptr,
                            &candidate->marker_impl_params,
                            nullptr
                        );
                        test = ms.monomorph_lifetime(span(), lifetime->test);
                        valid_for = ms.monomorph_lifetime(
                            span(), lifetime->valid_for
                        );
                    } else {
                        auto ms = candidate->impl.get_cb_monomorph_traitimpl(
                            crate.types, span(), {}
                        );
                        test = ms.monomorph_lifetime(span(), lifetime->test);
                        valid_for = ms.monomorph_lifetime(
                            span(), lifetime->valid_for
                        );
                    }

                    // A higher-ranked placeholder is a fresh lifetime from a
                    // new universe.  It cannot be required to outlive an
                    // outer lifetime: accepting `for<'b> 'b: 'a` would let
                    // the bound variable leak out of its binder.  Ordinary
                    // region constraints stay deferred to lifetime inference.
                    if (test.is_hrl() && test != valid_for) {
                        return Certainty::NoSolution;
                    }
                }
            }
            return result;
        }

        Certainty solve_goal(
            const ::HIR::SimplePath& trait,
            const ::HIR::PathParams& params,
            const ::HIR::TypeData* type,
            const ::HIR::TraitPath::assocListT* associated
        ) {
            const auto availableDepth = availableDepthForNested();
            if (!availableDepth) {
                return Certainty::Ambiguous;
            }
            auto goal_type = type;
            auto goal_params = params.clone();
            if (goal_has_unassigned_infer(
                    goal_params, goal_type, associated
                )) {
                return Certainty::Ambiguous;
            }
            // Nested obligations are formed directly from monomorphised impl
            // bounds.  Their Self type can therefore still be a projection,
            // e.g. `<Option::IntoIter<T> as Iterator>::Item: IntoIterator`.
            // Candidate assembly operates on the normalized goal input, just
            // as it already does for trait arguments.
            goal_type = mResolve.expand_associated_types(span(), goal_type);
            for (auto& param : goal_params.types) {
                param = mResolve.expand_associated_types(
                    span(), ::std::move(param)
                );
            }
            if (goal_has_unassigned_infer(
                    goal_params, goal_type, associated
                )) {
                return Certainty::Ambiguous;
            }
            // rustc structurally normalises Self before candidate assembly.
            // An unresolved projection over a type variable normalises to an
            // inference variable and therefore forces ambiguity; treating the
            // projection syntax as rigid lets an unrelated fuzzy ParamEnv
            // predicate constrain its output.
            if (self_is_unresolved_projection_over_ivar(goal_type)) {
                return Certainty::Ambiguous;
            }
            const auto& resolved_type = mResolve.resolve_type(goal_type);
            // Candidate assembly must not use an unconstrained `Self` type to
            // guide inference.  A concrete associated-type equality does
            // constrain the goal, however, and may uniquely determine Self.
            bool associatedConstrainsSelf = false;
            if (associated) {
                for (const auto& entry : *associated) {
                    associatedConstrainsSelf |=
                        !type_has_unknown(entry.second.type);
                }
            }
            if (const auto* infer = resolved_type->opt_Infer()) {
                if (!infer->is_lit() && !associatedConstrainsSelf) {
                    return Certainty::Ambiguous;
                }
            }
            CanonicalizeTraitGoal canonicalizer(crate.types);
            const auto canonical = canonicalizeGoal(
                goal_params, resolved_type, associated, canonicalizer
            );
            const auto* canonicalAssociated = canonical.associated.empty()
                ? nullptr : &canonical.associated;
            const auto hash = goal_hash(
                trait, canonical.params, canonical.type, canonicalAssociated
            );
            if (const auto* cached = find_cached_goal(
                    hash,
                    trait,
                    canonical.params,
                    canonical.type,
                    canonicalAssociated
                )) {
                return cached->certainty;
            }
            if (find_active_goal(
                    hash,
                    trait,
                    canonical.params,
                    canonical.type,
                    canonicalAssociated
                )) {
                // Productive recursive traits prove their provisional goal;
                // ordinary trait cycles remain ambiguous.
                return crate.get_trait_by_path(span(), trait).isCoinductive
                    ? Certainty::Proven
                    : Certainty::Ambiguous;
            }

            auto* activeGoal = push_active_goal(
                hash,
                trait,
                canonical.params,
                canonical.type,
                canonicalAssociated
            );
            struct StackGuard {
                NextTraitGoalEvaluator& self;
                GoalKey* goal;
                ~StackGuard() { self.pop_active_goal(goal); }
            } guard{*this, activeGoal};
            auto cacheResult = [&](Certainty certainty) {
                return cacheGoal(
                    hash,
                    trait,
                    canonical.params,
                    canonical.type,
                    canonicalAssociated,
                    certainty
                );
            };

            const size_t frame_index = frameDepth++;
            if (frame_index == frames.size()) {
                frames.push_back(crate.pool->make<CandidateFrame>());
            }
            frames[frame_index]->clear(candidateNodes);
            frames[frame_index]->availableDepth = *availableDepth;
            struct FrameGuard {
                NextTraitGoalEvaluator& self;
                size_t index;
                ~FrameGuard() {
                    const bool encountered_overflow =
                        self.frames[index]->encountered_overflow;
                    self.frames[index]->clear(self.candidateNodes);
                    assert(self.frameDepth == index + 1);
                    self.frameDepth--;
                    if (encountered_overflow && index > 0) {
                        self.frames[index - 1]->encountered_overflow = true;
                    }
                }
            } frame_guard{*this, frame_index};

            try {
                assembleCandidates(frame_index, trait, goal_params, resolved_type);
            } catch (const TraitResolution::RecursionDetected&) {
                return cacheResult(Certainty::Ambiguous);
            }

            bool saw_ambiguous = false;
            bool suppress_auto_builtin = false;
            bool negative_proven = false;
            bool negative_ambiguous = false;
            Certainty autoBuiltinResult = Certainty::NoSolution;
            const size_t candidateCount = frames[frame_index]->candidates.size();
            for (size_t i = 0; i < candidateCount; i++) {
                const auto result = evaluate_candidate(frame_index, i, trait, associated);
                auto* candidate = frames[frame_index]->candidates[i];
                candidate->certainty = result;
                if (candidate->is_negative()) {
                    negative_proven |= result == Certainty::Proven;
                    negative_ambiguous |= result == Certainty::Ambiguous;
                    continue;
                }
                if (candidate->autoBuiltin) {
                    autoBuiltinResult = result;
                    continue;
                }
                suppress_auto_builtin |= candidate->is_positive_marker_impl()
                    && result != Certainty::NoSolution;
                if (result == Certainty::Proven) {
                    return cacheResult(Certainty::Proven);
                }
                saw_ambiguous |= result == Certainty::Ambiguous;
            }
            if (!suppress_auto_builtin && !negative_proven) {
                if (negative_ambiguous && autoBuiltinResult == Certainty::Proven) {
                    autoBuiltinResult = Certainty::Ambiguous;
                }
                if (autoBuiltinResult == Certainty::Proven) {
                    return cacheResult(Certainty::Proven);
                }
                saw_ambiguous |= autoBuiltinResult == Certainty::Ambiguous;
            }
            if (saw_ambiguous
                || mResolve.type_contains_ivars(resolved_type)
                || mResolve.params_contain_ivars(goal_params)
                || (coherenceMode
                    && !trait_ref_is_knowable(trait, goal_params, resolved_type))) {
                return cacheResult(Certainty::Ambiguous);
            }
            return cacheResult(Certainty::NoSolution);
        }

        Certainty match_root_associated(
            const ::HIR::SimplePath& trait,
            const ImplRef& impl,
            const char* assocName,
            const ::HIR::TypeData* assocType,
            const ::HIR::PathParams* assocParams
        ) const {
            if (!assocName || !assocName[0]) {
                return Certainty::Proven;
            }
            const static ::HIR::PathParams no_params;
            const auto& params = assocParams ? *assocParams : no_params;
            if (!impl.mData.is_TraitImpl() && params.has_params()) {
                return Certainty::Ambiguous;
            }
            auto output = impl.get_type(crate.types, assocName, params);
            if (output == ::HIR::TypeRef()) {
                if (impl.mData.is_TraitImpl()) {
                    return Certainty::Ambiguous;
                }
                if (!assocType) {
                    // A bare ParamEnv trait predicate does not normalize its
                    // associated type.  It only proves that the projection is
                    // well-formed, so the normalizes-to response is ambiguous.
                    return Certainty::Ambiguous;
                }
                output = make_associated_projection(
                    impl,
                    ::HIR::GenericPath(trait, impl.get_trait_params(crate.types)),
                    RcString::new_interned(assocName),
                    params
                );
            }
            if (!assocType) {
                return Certainty::Proven;
            }
            const auto cmp = mResolve.compareTy(span(), assocType, output);
            if (cmp == ::HIR::Compare::Unequal) {
                return Certainty::NoSolution;
            }
            // A normalizes-to goal with a caller inference variable has a
            // proven response plus an equality constraint. The caller applies
            // that constraint from the returned ImplRef; the unassigned
            // destination alone must not turn a unique response into `Maybe`.
            if (cmp == ::HIR::Compare::Fuzzy
                && mResolve.type_contains_ivars(assocType)
                && !mResolve.type_contains_ivars(output)
                && !type_has_candidate_placeholder(output)) {
                return Certainty::Proven;
            }
            return cmp == ::HIR::Compare::Equal
                ? Certainty::Proven
                : Certainty::Ambiguous;
        }

        ImplRef materialize_root_associated(
            ImplRef impl,
            const ::HIR::SimplePath& trait,
            const char* assocName,
            const ::HIR::PathParams* assocParams
        ) const {
            if (!assocName || !assocName[0] || impl.mData.is_TraitImpl()) {
                return impl;
            }
            const static ::HIR::PathParams no_params;
            const auto& item_params = assocParams ? *assocParams : no_params;
            if (impl.get_type(crate.types, assocName, item_params) != ::HIR::TypeRef()) {
                return impl;
            }

            auto type = impl.get_impl_type(crate.types);
            auto params = impl.get_trait_params(crate.types);
            ::HIR::TraitPath::assocListT associated;
            if (const auto* bounded = impl.mData.opt_BoundedPtr()) {
                for (const auto& entry : *bounded->assoc) {
                    associated.insert({entry.first, entry.second.clone()});
                }
            } else if (const auto* bounded = impl.mData.opt_Bounded()) {
                for (const auto& entry : bounded->assoc) {
                    associated.insert({entry.first, entry.second.clone()});
                }
            }

            const auto name = RcString::new_interned(assocName);
            auto source_trait = ::HIR::GenericPath(trait, params.clone());
            auto projection = make_associated_projection(
                type, source_trait, name, item_params
            );
            associated.erase(name);
            associated.insert({
                name,
                ::HIR::TraitPath::AtyEqual{
                    ::std::move(source_trait),
                    item_params.clone(),
                    ::std::move(projection)
                }
            });
            const bool ambiguousIdentity = impl.is_ambiguous_identity();
            auto result = ImplRef(
                ::std::move(type),
                ::std::move(params),
                ::std::move(associated)
            );
            if (ambiguousIdentity) {
                result.mark_ambiguous_identity();
            }
            return result;
        }

        bool responses_equal(
            const ImplRef& left,
            const ImplRef& right,
            const char* assocName,
            const ::HIR::PathParams* assocParams
        ) const {
            auto types_equal_after_normalization = [&](const ::HIR::TypeData* lhs,
                                                       const ::HIR::TypeData* rhs) {
                if (lhs == ::HIR::TypeRef() || rhs == ::HIR::TypeRef()) {
                    return lhs == rhs;
                }
                // TypeRef identity is structural equality after interning.
                // Avoid recursively normalising and re-interning the common
                // case where both canonical responses already share a type.
                if (lhs == rhs) {
                    return true;
                }
                auto normalized_lhs = mResolve.expand_associated_types(
                    span(), lhs
                );
                auto normalized_rhs = mResolve.expand_associated_types(
                    span(), rhs
                );
                if (normalized_lhs == ::HIR::TypeRef()
                    || normalized_rhs == ::HIR::TypeRef()) {
                    return normalized_lhs == normalized_rhs;
                }
                const auto* resolved_lhs = mResolve.resolve_type(normalized_lhs);
                const auto* resolved_rhs = mResolve.resolve_type(normalized_rhs);
                return resolved_lhs == resolved_rhs
                    || resolved_lhs->equals_ignoring_regions(resolved_rhs);
            };
            auto params_equal_after_normalization = [&](const ::HIR::PathParams& lhs,
                                                        const ::HIR::PathParams& rhs) {
                if (lhs.mLifetimes.size() != rhs.mLifetimes.size()
                    || lhs.types.size() != rhs.types.size()
                    || lhs.values.size() != rhs.values.size()) {
                    return false;
                }
                // Ordinary regions are deliberately erased when a type is
                // stored in an HM inference variable, and trait selection
                // defers their constraints to lifetime inference.  Thus a
                // ParamEnv proof for `Projection<'a>` and the same declared
                // GAT bound seen through `Projection<'#omitted>` are one
                // canonical solver response.  Higher-ranked leak checking is
                // performed while evaluating the candidate bounds above; it
                // must not be reintroduced here as response identity.
                for (size_t i = 0; i < lhs.types.size(); i++) {
                    if (!types_equal_after_normalization(
                            lhs.types[i], rhs.types[i]
                        )) {
                        return false;
                    }
                }
                for (size_t i = 0; i < lhs.values.size(); i++) {
                    if (lhs.values[i] != rhs.values[i]) {
                        return false;
                    }
                }
                return true;
            };

            if (!types_equal_after_normalization(
                    left.get_impl_type(crate.types), right.get_impl_type(crate.types)
                )
                || !params_equal_after_normalization(
                    left.get_trait_params(crate.types), right.get_trait_params(crate.types)
                )) {
                return false;
            }
            if (!assocName || !assocName[0]) {
                return true;
            }
            const static ::HIR::PathParams no_params;
            const auto& params = assocParams ? *assocParams : no_params;
            if ((!left.mData.is_TraitImpl() || !right.mData.is_TraitImpl())
                && params.has_params()) {
                return false;
            }
            return types_equal_after_normalization(
                left.get_type(crate.types, assocName, params),
                right.get_type(crate.types, assocName, params)
            );
        }

    public:
        NextTraitGoalEvaluator(
            const TraitResolution& resolve,
            const ::HIR::Crate& crate
        )
            : mResolve(resolve)
            , crate(crate)
            , candidateNodes(crate.pool)
            , activeGoalNodes(crate.pool)
            , cachedGoalNodes(crate.pool)
        {
            frames.reserve(16);
            goalStack.reserve(16);
            goalCache.reserve(64);
            activeGoalIndex.reserve(32);
            goalCacheIndex.reserve(128);
        }

        bool evaluate_overlap(
            const Span& callSpan,
            const ::HIR::SimplePath& trait,
            const ::HIR::TraitImpl& left,
            const ::HIR::TraitImpl& right
        ) {
            ASSERT_BUG(callSpan, !mSpan, "nested coherence overlap session");
            ASSERT_BUG(callSpan, !coherenceMode, "coherence mode leaked before overlap probe");
            ASSERT_BUG(callSpan, goalStack.empty(), "next-solver goal stack leaked before coherence probe");
            ASSERT_BUG(callSpan, activeGoalIndex.empty(), "next-solver active goal index leaked before coherence probe");
            ASSERT_BUG(callSpan, frameDepth == 0, "next-solver candidate frames leaked before coherence probe");
            clearGoalCache();
            mSpan = &callSpan;
            coherenceMode = true;
            struct SessionGuard {
                NextTraitGoalEvaluator& self;
                ~SessionGuard() {
                    assert(self.goalStack.empty());
                    assert(self.activeGoalIndex.empty());
                    self.clearGoalCache();
                    self.frameDepth = 0;
                    self.coherenceMode = false;
                    self.mSpan = nullptr;
                }
            } session_guard{*this};

            // Instantiate the first header with fresh inference variables, then
            // match the second header against it.  This is a unification of two
            // independently generic impls, not a one-way syntactic ordering.
            auto left_params = mResolve.make_fresh_impl_params(left.mParams);
            auto left_monomorph = MonomorphStatePtr(crate.types, nullptr, &left_params, nullptr);
            auto goal_type = left_monomorph.monomorph_type(callSpan, left.mType, true);
            auto goal_params = left_monomorph.monomorph_path_params(
                callSpan, left.traitArgs, true
            );

            ::HIR::PathParams right_params;
            const auto right_match = mResolve.ftic_check_params(
                callSpan,
                trait,
                &goal_params,
                goal_type,
                right.mParams,
                right.traitArgs,
                right.mType,
                right_params,
                false
            );
            if (right_match == ::HIR::Compare::Unequal) {
                return false;
            }

            const size_t frame_index = frameDepth++;
            if (frame_index == frames.size()) {
                frames.push_back(crate.pool->make<CandidateFrame>());
            }
            frames[frame_index]->clear(candidateNodes);
            frames[frame_index]->availableDepth = ROOT_DEPTH;
            struct FrameGuard {
                NextTraitGoalEvaluator& self;
                size_t index;
                ~FrameGuard() {
                    const bool encountered_overflow =
                        self.frames[index]->encountered_overflow;
                    self.frames[index]->clear(self.candidateNodes);
                    assert(self.frameDepth == index + 1);
                    self.frameDepth--;
                    if (encountered_overflow && index > 0) {
                        self.frames[index - 1]->encountered_overflow = true;
                    }
                }
            } frame_guard{*this, frame_index};

            const auto& trait_def = crate.get_trait_by_path(callSpan, trait);
            push_candidate(
                frame_index,
                ImplRef(::std::move(left_params), trait_def, trait, left),
                ::HIR::Compare::Equal,
                nullptr, {}, false, CandidateSource::TraitImpl
            );
            push_candidate(
                frame_index,
                ImplRef(::std::move(right_params), trait_def, trait, right),
                right_match,
                nullptr, {}, false, CandidateSource::TraitImpl
            );

            const auto& candidates = frames[frame_index]->candidates;
            ASSERT_BUG(callSpan, candidates.size() == 2, "coherence probe lost an impl candidate");
            const auto left_result = evaluate_candidate(frame_index, 0, trait, nullptr);
            if (left_result == Certainty::NoSolution) {
                return false;
            }
            const auto right_result = evaluate_candidate(frame_index, 1, trait, nullptr);
            return right_result != Certainty::NoSolution;
        }

        bool evaluate(
            const Span& callSpan,
            const ::HIR::SimplePath& trait,
            const ::HIR::PathParams& params,
            const ::HIR::TypeData* type,
            TraitResolution::t_cb_trait_impl_r callback,
            const char* assocName,
            const ::HIR::TypeData* assocType,
            const ::HIR::PathParams* assocParams
        ) {
            const bool outermost = mSpan == nullptr;
            if (outermost) {
                ASSERT_BUG(callSpan, goalStack.empty(), "next-solver goal stack leaked between evaluations");
                ASSERT_BUG(callSpan, activeGoalIndex.empty(), "next-solver active goal index leaked between evaluations");
                ASSERT_BUG(callSpan, frameDepth == 0, "next-solver candidate frames leaked between evaluations");
                clearGoalCache();
                mSpan = &callSpan;
            }
            struct SessionGuard {
                NextTraitGoalEvaluator& self;
                bool outermost;
                ~SessionGuard() {
                    if (outermost) {
                        assert(self.goalStack.empty());
                        assert(self.activeGoalIndex.empty());
                        self.clearGoalCache();
                        self.frameDepth = 0;
                        self.mSpan = nullptr;
                    }
                }
            } session_guard{*this, outermost};

            auto goal_type = type;
            auto goal_params = params.clone();
            auto emit_forced_ambiguity = [&]() {
                // Ordinary lookup cannot consume an identity response, while
                // extended solver callers use it to retain the original goal
                // without committing any candidate substitutions.
                if (!assocName) {
                    return false;
                }
                auto ambiguous = ImplRef(
                    goal_type,
                    goal_params.clone(),
                    ::HIR::TraitPath::assocListT()
                );
                ambiguous.mark_ambiguous_identity();
                return callback(
                    materialize_root_associated(
                        ::std::move(ambiguous),
                        trait,
                        assocName,
                        assocParams
                    ),
                    ::HIR::Compare::Fuzzy
                );
            };
            if (goal_has_unassigned_infer(
                    goal_params, goal_type, nullptr
                )) {
                return emit_forced_ambiguity();
            }
            goal_type = mResolve.expand_associated_types(span(), goal_type);
            for (auto& param : goal_params.types) {
                param = mResolve.expand_associated_types(
                    span(), ::std::move(param)
                );
            }
            if (self_is_unresolved_projection_over_ivar(goal_type)) {
                return emit_forced_ambiguity();
            }
            const auto& resolved_type = mResolve.resolve_type(goal_type);
            // Match rustc's forced-ambiguity response for a genuinely
            // unconstrained `Self` type.  A known associated output is an
            // input constraint and can legitimately select a unique response.
            const bool associatedConstrainsSelf =
                assocName && assocName[0] && assocType
                && !type_has_unknown(assocType);
            if (const auto* infer = resolved_type->opt_Infer()) {
                if (!infer->is_lit() && !associatedConstrainsSelf) {
                    return emit_forced_ambiguity();
                }
            }
            CanonicalizeTraitGoal canonicalizer(crate.types);
            const auto canonical = canonicalizeGoal(
                goal_params, resolved_type, nullptr, canonicalizer
            );
            // The associated output is not part of the response cache key,
            // but its placeholders are still inputs of this query.  Record
            // them so root response instantiation does not mistake them for
            // existential variables created by candidate evaluation.
            if (assocType) {
                canonicalizer.monomorph_type(span(), assocType, true);
            }
            if (assocParams) {
                canonicalizer.monomorph_path_params(span(), *assocParams, true);
            }
            const auto root_hash = goal_hash(
                trait, canonical.params, canonical.type, nullptr
            );
            auto instantiate_for_caller = [&](ImplRef response) {
                if (!outermost) {
                    return response;
                }
                InstantiateTraitResponseForCaller instantiator(
                    crate.types,
                    const_cast<HMTypeInferrence&>(mResolve.ivars),
                    canonicalizer.placeholder_names()
                );
                return monomorph_impl_ref(response, instantiator);
            };
            // Extended callers use an explicit empty associated-item name
            // when they need the canonical trait response itself. Cache that
            // completed response, not just its certainty: otherwise every
            // repeated nested obligation rebuilds the entire candidate graph.
            const bool cacheableResponse = assocName && !assocName[0];
            if (cacheableResponse) {
                if (const auto* cached = find_cached_goal(
                        root_hash,
                        trait,
                        canonical.params,
                        canonical.type,
                        nullptr
                    ); cached && cached->has_response) {
                    InstantiateCanonicalTraitResponse instantiator(
                        crate.types,
                        canonicalizer.placeholder_names(),
                        responseInstanceCounter++
                    );
                    auto response = monomorph_impl_ref(
                        cached->response, instantiator
                    );
                    return callback(
                        instantiate_for_caller(::std::move(response)),
                        cached->response_certainty
                    );
                }
            }
            auto emit_response = [&](ImplRef response, ::HIR::Compare certainty) {
                if (!cacheableResponse) {
                    return callback(
                        instantiate_for_caller(::std::move(response)), certainty
                    );
                }
                auto canonicalResponse = monomorph_impl_ref(
                    response, canonicalizer
                );
                auto* cached = cacheResponse(
                    root_hash,
                    trait,
                    canonical.params,
                    canonical.type,
                    nullptr,
                    ::std::move(canonicalResponse),
                    certainty
                );
                return callback(
                    instantiate_for_caller(::std::move(response)),
                    cached->response_certainty
                );
            };
            if (find_active_goal(
                    root_hash,
                    trait,
                    canonical.params,
                    canonical.type,
                    nullptr
                )) {
                static const ::HIR::TraitPath::assocListT no_associated;
                const bool coinductive = crate.get_trait_by_path(
                    span(), trait
                ).isCoinductive;
                return callback(
                    ImplRef(resolved_type, &goal_params, &no_associated),
                    coinductive
                        ? ::HIR::Compare::Equal
                        : ::HIR::Compare::Fuzzy
                );
            }
            auto* root_goal = push_active_goal(
                root_hash,
                trait,
                canonical.params,
                canonical.type,
                nullptr
            );
            struct RootStackGuard {
                NextTraitGoalEvaluator& self;
                GoalKey* goal;
                ~RootStackGuard() { self.pop_active_goal(goal); }
            } root_guard{*this, root_goal};

            const size_t frame_index = frameDepth++;
            if (frame_index == frames.size()) {
                frames.push_back(crate.pool->make<CandidateFrame>());
            }
            frames[frame_index]->clear(candidateNodes);
            frames[frame_index]->availableDepth = ROOT_DEPTH;
            struct FrameGuard {
                NextTraitGoalEvaluator& self;
                size_t index;
                ~FrameGuard() {
                    const bool encountered_overflow =
                        self.frames[index]->encountered_overflow;
                    self.frames[index]->clear(self.candidateNodes);
                    assert(self.frameDepth == index + 1);
                    self.frameDepth--;
                    if (encountered_overflow && index > 0) {
                        self.frames[index - 1]->encountered_overflow = true;
                    }
                }
            } frame_guard{*this, frame_index};

            try {
                assembleCandidates(
                    frame_index, trait, goal_params, resolved_type
                );
            } catch (const TraitResolution::RecursionDetected&) {
                return false;
            }
            auto& frame = *frames[frame_index];
            const size_t candidateCount = frame.candidates.size();
            DEBUG("next-solver assembled " << candidateCount
                  << " candidate(s) for " << type << ": " << trait << params);

            bool suppress_auto_builtin = false;
            bool negative_proven = false;
            bool negative_ambiguous = false;
            const ::HIR::TypeData* candidateAssocType = assocType;
            if (candidateAssocType) {
                if (const auto* erased = candidateAssocType->opt_ErasedType()) {
                    if (const auto* alias = erased->inner.opt_Alias();
                        alias && alias->inner->is_public_to(mResolve.visPath)) {
                        // A defining opaque is an output of alias-relate, not
                        // an input that can reject an otherwise valid impl.
                        // Return the projection response to the caller, which
                        // then equates it with this opaque and records its
                        // hidden type.
                        candidateAssocType = nullptr;
                    }
                }
            }
            ::HIR::TraitPath::assocListT root_associated;
            if (assocName && assocName[0] && candidateAssocType) {
                const static ::HIR::PathParams no_assoc_params;
                root_associated.insert({
                    RcString::new_interned(assocName),
                    ::HIR::TraitPath::AtyEqual{
                        ::HIR::GenericPath(trait, goal_params.clone()),
                        assocParams ? assocParams->clone() : no_assoc_params.clone(),
                        candidateAssocType
                    }
                });
            }
            for (size_t i = 0; i < candidateCount; i++) {
                auto certainty = evaluate_candidate(
                    frame_index,
                    i,
                    trait,
                    root_associated.empty() ? nullptr : &root_associated
                );
                auto* candidate = frame.candidates[i];
                if (!candidate->is_negative()) {
                    const auto assocCertainty = match_root_associated(
                        trait,
                        candidate->impl,
                        assocName,
                        candidateAssocType,
                        assocParams
                    );
                    if (assocCertainty == Certainty::NoSolution) {
                        certainty = Certainty::NoSolution;
                    } else if (assocCertainty == Certainty::Ambiguous
                               && certainty == Certainty::Proven) {
                        certainty = Certainty::Ambiguous;
                    }
                }
                candidate->certainty = certainty;
                DEBUG("next-solver candidate " << candidate->impl
                      << " => " << static_cast<unsigned>(certainty));
                if (candidate->is_negative()) {
                    negative_proven |= certainty == Certainty::Proven;
                    negative_ambiguous |= certainty == Certainty::Ambiguous;
                    continue;
                }
                suppress_auto_builtin |= candidate->is_positive_marker_impl()
                    && certainty != Certainty::NoSolution;
                if (certainty != Certainty::NoSolution) {
                    frame.viable.push_back(candidate);
                }
            }

            if (suppress_auto_builtin || negative_proven) {
                auto& viable = frame.viable;
                viable.erase(
                    ::std::remove_if(
                        viable.begin(), viable.end(),
                        [&](Candidate* candidate) {
                            return candidate->autoBuiltin;
                        }
                    ),
                    viable.end()
                );
            } else if (negative_ambiguous) {
                for (auto* candidate : frame.viable) {
                    if (candidate->autoBuiltin
                        && candidate->certainty == Certainty::Proven) {
                        candidate->certainty = Certainty::Ambiguous;
                    }
                }
            }

            if (frame.viable.empty()) {
                DEBUG("next-solver: no viable response");
                // solve_goal keeps an obligation ambiguous while inference
                // still occurs in its inputs.  The response-producing path
                // must preserve the same result: nested candidate evaluation
                // calls it specifically to recover constraints from an
                // ambiguous goal.  Returning false here would turn e.g.
                // `<_ as IntoIterator>::IntoIter: Iterator` into NoSolution
                // and incorrectly discard an enclosing `Zip` candidate.
                if (mResolve.type_contains_ivars(resolved_type)
                    || mResolve.params_contain_ivars(goal_params)) {
                    return emit_forced_ambiguity();
                }
                return false;
            }

            // rustc prefers all ParamEnv responses when any applicable
            // non-global where-bound exists. In particular, `T:
            // Pointee<Metadata = ()>` must retain the environment response
            // instead of normalising through the generic builtin fallback.
            const bool has_non_global_param_env = ::std::any_of(
                frame.viable.begin(), frame.viable.end(),
                [&](const Candidate* candidate) {
                    return param_env_candidate_is_non_global(*candidate);
                }
            );
            if (has_non_global_param_env) {
                auto& viable = frame.viable;
                viable.erase(
                    ::std::remove_if(
                        viable.begin(), viable.end(),
                        [](const Candidate* candidate) {
                            return candidate->source != CandidateSource::ParamEnv;
                        }
                    ),
                    viable.end()
                );
            }

            // A proven ParamEnv or builtin candidate shadows impl candidates.
            // This is the central next-solver candidate preference used for
            // projection normalization and dyn-object builtins.
            bool has_preferred_non_impl = false;
            for (const auto* candidate : frame.viable) {
                has_preferred_non_impl |= is_environment_or_builtin(candidate->impl)
                    && candidate->certainty == Certainty::Proven;
            }
            if (has_preferred_non_impl) {
                auto& viable = frame.viable;
                viable.erase(
                    ::std::remove_if(
                        viable.begin(), viable.end(),
                        [&](Candidate* candidate) {
                            return !is_environment_or_builtin(candidate->impl);
                        }
                    ),
                    viable.end()
                );
            }

            // Apply specialization only after nested goals have been probed.
            for (auto* candidate : frame.viable) {
                candidate->discarded = false;
            }
            for (size_t i = 0; i < frame.viable.size(); i++) {
                if (frame.viable[i]->discarded) {
                    continue;
                }
                for (size_t j = i + 1; j < frame.viable.size(); j++) {
                    if (frame.viable[j]->discarded) {
                        continue;
                    }
                    auto& left = frame.viable[i]->impl;
                    auto& right = frame.viable[j]->impl;
                    // Specialization only distinguishes different canonical
                    // responses.  If both candidates constrain the caller in
                    // exactly the same way, merge their certainties instead;
                    // a proven route must not be discarded behind an
                    // ambiguous, cyclic route to the same response.
                    if (responses_equal(left, right, assocName, assocParams)) {
                        continue;
                    }
                    if (!left.mData.is_TraitImpl() || !right.mData.is_TraitImpl()) {
                        continue;
                    }
                    // evaluate_overlap is itself the recursive overlap query.
                    // Re-entering either overlap implementation here makes a
                    // coinductive pair recurse without a solver cycle head.
                    // Keeping both responses is conservative: ambiguity is
                    // already sufficient to report that the impls may overlap.
                    if (coherenceMode
                        || !mResolve.impls_overlap(span(), left, right)) {
                        continue;
                    }
                    // A more-specific impl with an ambiguous where-clause
                    // cannot shadow the fallback: that nested goal may still
                    // fail.  Head ambiguity alone is inference guidance and
                    // remains eligible for specialization.
                    if (left.more_specific_than(crate.types, right)
                        && !frame.viable[i]->ambiguityBeyondHead) {
                        frame.viable[j]->discarded = true;
                    } else if (right.more_specific_than(crate.types, left)
                               && !frame.viable[j]->ambiguityBeyondHead) {
                        frame.viable[i]->discarded = true;
                        break;
                    }
                }
            }
            frame.viable.erase(
                ::std::remove_if(
                    frame.viable.begin(), frame.viable.end(),
                    [](const Candidate* candidate) { return candidate->discarded; }
                ),
                frame.viable.end()
            );

            bool one_response = true;
            for (size_t i = 1; i < frame.viable.size(); i++) {
                if (!responses_equal(
                        frame.viable.front()->impl,
                        frame.viable[i]->impl,
                        assocName,
                        assocParams
                    )) {
                    one_response = false;
                    break;
                }
            }

            if (one_response) {
                Candidate* selected = frame.viable.front();
                for (auto* candidate : frame.viable) {
                    if (candidate->certainty == Certainty::Proven) {
                        selected = candidate;
                        break;
                    }
                }
                const auto certainty = selected->certainty;
                DEBUG("next-solver: applying merged response "
                      << selected->impl << " certainty="
                      << static_cast<unsigned>(certainty));
                if (certainty != Certainty::Proven) {
                    return emit_response(
                        ::std::move(selected->impl),
                        ::HIR::Compare::Fuzzy
                    );
                }
                return emit_response(
                    materialize_root_associated(
                        ::std::move(selected->impl),
                        trait,
                        assocName,
                        assocParams
                    ),
                    ::HIR::Compare::Equal
                );
            }

            // Distinct canonical responses cannot guide inference.  Return a
            // single identity response for the original goal: exposing any
            // concrete candidate here lets a callback accidentally commit the
            // first candidate's substitutions despite the ambiguity.
            auto ambiguous = ImplRef(
                resolved_type,
                goal_params.clone(),
                ::HIR::TraitPath::assocListT()
            );
            ambiguous.mark_ambiguous_identity();
            return emit_response(
                materialize_root_associated(
                    ::std::move(ambiguous),
                    trait,
                    assocName,
                    assocParams
                ),
                ::HIR::Compare::Fuzzy
            );
        }
    };

TraitResolution::TraitResolution(
    const HMTypeInferrence& ivars,
    const ::HIR::Crate& crate,
    const ::HIR::GenericParams* impl_params,
    const ::HIR::GenericParams* item_params,
    const ::HIR::SimplePath& vis_path,
    const ::HIR::GenericPath* current_trait
)
    : TraitResolveCommon(crate)
    , mLangDeref(crate.get_lang_item_path_opt("deref"))
    , ivars(ivars)
    , coherenceIvars(crate.types)
    , visPath(vis_path)
    , currentTraitPath(current_trait)
    , currentTraitPtr(current_trait ? &crate.get_trait_by_path(Span(), current_trait->mPath) : nullptr)
{
    implGenerics = impl_params;
    itemGenerics = item_params;
    prep_indexes(Span());
}

TraitResolution::~TraitResolution() = default;

void TraitResolution::set_generic_context(
    const ::HIR::GenericParams* impl_params,
    const ::HIR::GenericParams* item_params
) {
    if (implGenerics == impl_params && itemGenerics == item_params) {
        return;
    }
    ASSERT_BUG(Span(), eatActiveStack.empty(), "changing trait environment during associated-type expansion");
    implGenerics = impl_params;
    itemGenerics = item_params;
    eatCache.clear();
    prep_indexes(Span());
}

::HIR::PathParams TraitResolution::make_fresh_impl_params(
    const ::HIR::GenericParams& params
) const {
    auto& mutIvars = const_cast<HMTypeInferrence&>(this->ivars);
    ::HIR::PathParams result;
    result.mLifetimes = ThinVector<::HIR::LifetimeRef>(params.mLifetimes.size());
    result.types.reserve(params.types.size());
    for (size_t i = 0; i < params.types.size(); i++) {
        result.types.push_back(mutIvars.new_ivar_tr());
    }
    result.values.reserve(params.values.size());
    for (size_t i = 0; i < params.values.size(); i++) {
        result.values.push_back(
            ::HIR::ConstGeneric::make_Infer({mutIvars.new_ivar_val()})
        );
    }
    return result;
}

bool TraitResolution::impls_overlap(
    const Span& sp,
    const ImplRef& left,
    const ImplRef& right
) const {
    const auto* left_impl = left.mData.opt_TraitImpl();
    const auto* right_impl = right.mData.opt_TraitImpl();
    if (!gTraitSolverConfig.coherence
        || !left_impl || !right_impl
        || !left_impl->impl || !right_impl->impl) {
        return left.overlaps_with(crate, right);
    }
    if (!left_impl->trait_path || !right_impl->trait_path
        || *left_impl->trait_path != *right_impl->trait_path) {
        return false;
    }
    if (left_impl->impl == right_impl->impl) {
        return true;
    }

    // The probe resolver is pool-owned and reused, while its inference table
    // is reset per overlap query.  No probe variable can escape into m_ivars.
    coherenceIvars.ivars.clear();
    coherenceIvars.values.clear();
    coherenceIvars.hasChanged = false;
    if (!coherenceResolve) {
        ASSERT_BUG(sp, crate.pool, "next-solver coherence requires the crate object pool");
        coherenceResolve = crate.pool->make<TraitResolution>(
            coherenceIvars,
            crate,
            implGenerics,
            itemGenerics,
            visPath,
            currentTraitPath
        );
    } else {
        coherenceResolve->set_generic_context(implGenerics, itemGenerics);
    }
    if (!coherenceResolve->nextSolver) {
        coherenceResolve->nextSolver = crate.pool->make<NextTraitGoalEvaluator>(
            *coherenceResolve, crate
        );
    }
    return coherenceResolve->nextSolver->evaluate_overlap(
        sp,
        *left_impl->trait_path,
        *left_impl->impl,
        *right_impl->impl
    );
}

bool TraitResolution::find_trait_impls_next(
    const Span& sp,
    const ::HIR::SimplePath& trait,
    const ::HIR::PathParams& params,
    const ::HIR::TypeData* type,
    t_cb_trait_impl_r callback,
    const char* assocName,
    const ::HIR::TypeData* assocType,
    const ::HIR::PathParams* assocParams
) const {
    TRACE_FUNCTION_F("trait = " << trait << params << ", type = " << type);
    if (!nextSolver) {
        ASSERT_BUG(sp, crate.pool, "next-solver requires the crate object pool");
        nextSolver = crate.pool->make<NextTraitGoalEvaluator>(*this, crate);
    }
    return nextSolver->evaluate(
        sp, trait, params, type, ::std::move(callback), assocName, assocType, assocParams
    );
}

bool TraitResolution::find_trait_impls(
    const Span& sp,
    const ::HIR::SimplePath& trait,
    const ::HIR::PathParams& params,
    const ::HIR::TypeData* type,
    t_cb_trait_impl_r callback,
    bool magic_trait_impls
) const {
    if (gTraitSolverConfig.globally && magic_trait_impls) {
        return find_trait_impls_next(
            sp, trait, params, type, ::std::move(callback)
        );
    }
    return find_trait_impls_legacy(
        sp, trait, params, type, ::std::move(callback), magic_trait_impls
    );
}

        // -------------------------------------------------------------------------------------------------------------------
        //
        // -------------------------------------------------------------------------------------------------------------------

        void TraitResolution::compactIvars(HMTypeInferrence& ivars) {
            ivars.checkForLoops();

            //m_ivars.compact_ivars([&](const ::HIR::TypeData* t)->auto{ return this->expand_associated_types(Span(), t.clone); });
            unsigned int i = 0;
            for (auto& v : ivars.ivars) {
                if (!v.is_alias()) {
                    ivars.expand_ivars(v.type);
                    // Don't expand unless it is needed
                    if (this->has_associated_type(v.type)) {
                        auto nt = this->expand_associated_types(Span(), v.type);
                        DEBUG("- " << i << " " << v.type << " -> " << nt);
                        v.type = nt;
                    }
                } else {
                    auto index = v.alias;
                    unsigned int count = 0;
                    assert(index < ivars.ivars.size());
                    while (ivars.ivars.at(index).is_alias()) {
                        index = ivars.ivars.at(index).alias;

                        if (count >= ivars.ivars.size()) {
                            this->ivars.dump();
                            BUG(Span(), "Loop detected in ivar list when starting at " << v.alias << ", current is " << index);
                        }
                        count++;
                    }
                    v.alias = index;
                }
                i++;
            }
        }

        bool TraitResolution::has_associated_type(const ::HIR::TypeData* input) const {
            if (!input->may_have_associated_type()) {
                return false;
            }
            struct H {
                static bool checkPathparams(const TraitResolution& r, const ::HIR::PathParams& pp) {
                    for (const auto& arg : pp.types) {
                        if (r.has_associated_type(arg)) {
                            return true;
                        }
                    }
                    return false;
                }

                static bool checkPath(const TraitResolution& r, const ::HIR::Path& p) {
                    TU_MATCH(::HIR::Path::Data, (p.mData), (e2), (Generic, return H::checkPathparams(r, e2.mParams);), (UfcsInherent, if (r.has_associated_type(e2.type)) return true; if (H::checkPathparams(r, e2.params)) return true; return false;), (UfcsKnown, if (r.has_associated_type(e2.type)) return true; if (H::checkPathparams(r, e2.trait.mParams)) return true; if (H::checkPathparams(r, e2.params)) return true; return false;), (UfcsUnknown, BUG(Span(), "Encountered UfcsUnknown - " << p);))
                    throw "";
                }
            };

            //TRACE_FUNCTION_F(input);
    TU_MATCH_HDRA( (*input), {)
    TU_ARMA(Infer, e) {
            const auto& ty = this->ivars.get_type(input);
            if (ty != input) {
                return this->has_associated_type(ty);
            }
            return false;
        }
        TU_ARMA(Diverge, e) {
            return false;
        }
        TU_ARMA(Primitive, e) {
            return false;
        }
        TU_ARMA(Path, e) {
            // Both states still need projection normalisation. `Opaque` means
            // that no rule was available at the previous attempt, not that
            // the projection can never become known.
            if (e.path.mData.is_UfcsKnown()
                && (e.binding.is_Unbound() || e.binding.is_Opaque())) {
                return true;
            }
            return H::checkPath(*this, e.path);
        }
        TU_ARMA(Generic, e) {
            return false;
        }
        TU_ARMA(TraitObject, e) {
            // Recurse?
            if (H::checkPathparams(*this, e.mTrait.mPath.mParams)) {
                return true;
            }
            for (const auto& m : e.markers) {
                if (H::checkPathparams(*this, m.mParams)) {
                    return true;
                }
            }
            return false;
        }
        TU_ARMA(ErasedType, e) {
        TU_MATCH_HDRA( (e.inner), {)
        TU_ARMA(Fcn, ee) {
                    if (H::checkPath(*this, ee.origin)) {
                        return true;
                    }
                }
                TU_ARMA(Known, ee) {
                    if (has_associated_type(ee)) {
                        return true;
                    }
                }
                TU_ARMA(Alias, ee) {
                }
        }
        for(const auto& m : e.traits) {
                if (H::checkPathparams(*this, m.mPath.mParams)) {
                    return true;
                }
        }
        return false;
        }
        TU_ARMA(Array, e) {
            return has_associated_type(e.inner);
        }
        TU_ARMA(Slice, e) {
            return has_associated_type(e.inner);
        }
        TU_ARMA(Tuple, e) {
            bool rv = false;
            for (const auto& sub : e) {
                rv |= has_associated_type(sub);
            }
            return rv;
        }
        TU_ARMA(Borrow, e) {
            return has_associated_type(e.inner);
        }
        TU_ARMA(Pointer, e) {
            return has_associated_type(e.inner);
        }
        TU_ARMA(NamedFunction, e) {
            return H::checkPath(*this, e.path);
        }
        TU_ARMA(Function, e) {
            // Recurse?
            return false;
        }
        TU_ARMA(NodeType, e) {
            // Recurse?
            return false;
        }
    }
    BUG(Span(), "Fell off the end of has_associated_type - input=" << input);
        }

        void TraitResolution::expand_associated_types_inplace(const Span& sp, ::HIR::TypeRef& input, LList<const ::HIR::TypeData*> stack) const {
            struct H {
                static void expand_associated_types_params(const Span& sp, const TraitResolution& res, ::HIR::PathParams& params, LList<const ::HIR::TypeData*> stack) {
                    for (auto& arg : params.types) {
                        res.expand_associated_types_inplace(sp, arg, stack);
                    }
                }

                static void expand_associated_types_tp(const Span& sp, const TraitResolution& res, ::HIR::TraitPath& input, LList<const ::HIR::TypeData*> stack) {
                    expand_associated_types_params(sp, res, input.mPath.mParams, stack);
                    for (auto& arg : input.typeBounds) {
                        expand_associated_types_params(sp, res, arg.second.source_trait.mParams, stack);
                        res.expand_associated_types_inplace(sp, arg.second.type, stack);
                    }
                    for (auto& arg : input.traitBounds) {
                        expand_associated_types_params(sp, res, arg.second.source_trait.mParams, stack);
                        for (auto& t : arg.second.traits) {
                            expand_associated_types_tp(sp, res, t, stack);
                        }
                    }
                }
            };

            for (const auto& ty : eatActiveStack) {
                if (input == ty) {
                    DEBUG("Recursive lookup, skipping - &input = " << &input);
                    return;
                }
            }
            //TRACE_FUNCTION_F(input);
    auto data = input->cloneData();
    TU_MATCH_HDRA( (data), {)
    TU_ARMA(Infer, e) {
            const auto* ty = this->ivars.get_type(input);
            if (ty != input) {
                input = ty;
                expand_associated_types_inplace(sp, input, stack);
                return;
            }
        }
        TU_ARMA(Diverge, e) {
        }
        TU_ARMA(Primitive, e) {
        }
        TU_ARMA(Path, e) {
        TU_MATCH_HDRA( (e.path.mData), {)
        TU_ARMA(Generic, pe) {
                    ConvertHIRConstantEvaluateMethodParams(sp, crate, visPath, implGenerics, itemGenerics, e.binding.get_generics(), pe.mParams);
                    H::expand_associated_types_params(sp, *this, pe.mParams, stack);
                }
                TU_ARMA(UfcsInherent, pe) {
                    expand_associated_types_inplace(sp, pe.type, stack);
                    H::expand_associated_types_params(sp, *this, pe.params, stack);
                    H::expand_associated_types_params(sp, *this, pe.impl_params, stack);
                    input = crate.types.intern(mv$(data));
                    if (this->expandAssociatedTypesInplaceUfcsInherent(sp, input, stack)) {
                        this->expand_associated_types_inplace(sp, input, stack);
                    }
                    return;
                }
                TU_ARMA(UfcsKnown, pe) {
                    struct D {
                        const TraitResolution& tr;
                        D(const TraitResolution& tr, ::HIR::TypeRef v)
                            : tr(tr)
                        {
                            tr.eatActiveStack.push_back(v);
                        }
                        ~D() {
                            tr.eatActiveStack.pop_back();
                        }
                        D(D&&) = delete;
                        D(const D&) = delete;
                    };
                    D _(*this, input);
                    // State stack to avoid infinite recursion
                    assert(eatActiveStack.size() > 0);
                    auto& prev_stack = stack;
                    LList<const ::HIR::TypeData*> stack(&prev_stack, eatActiveStack.back());

                    expand_associated_types_inplace(sp, pe.type, stack);
                    H::expand_associated_types_params(sp, *this, pe.params, stack);
                    H::expand_associated_types_params(sp, *this, pe.trait.mParams, stack);
                    input = crate.types.intern(mv$(data));
                    // Retry opaque projections too: equality bounds can be
                    // learned after an earlier normalisation attempt.
                    const bool was_unbound = input->as_Path().binding.is_Unbound();
                    const bool was_opaque = input->as_Path().binding.is_Opaque();
                    if (was_unbound || was_opaque) {
                        if (was_opaque) {
                            this->expandAssociatedTypesInplaceUfcsKnown(sp, input, stack);
                            return;
                        }

                        // Cache the result of this to avoid needing to do the full resolution too often.
                        // - This avoids VERY slow typechecking in 1.90's librustc_target
                        auto k = FMT(input);
                        auto it = eatCache.find(k);
                        if (it != eatCache.end()) {
                            if (input != it->second) {
                                this->expand_associated_types_inplace(sp, it->second, stack);
                            }
                            DEBUG("CACHED: " << input << " -> " << it->second);
                            input = it->second;
                        } else {
                            this->expandAssociatedTypesInplaceUfcsKnown(sp, input, stack);
                            if (input->is_Path()
                                && (input->as_Path().binding.is_Unbound()
                                    || input->as_Path().binding.is_Opaque())) {
                            } else {
                                DEBUG("CACHE+: " << k << " = " << input);
                                eatCache.insert(::std::make_pair(k, input));
                            }
                        }
                    }
                    return;
                }
                TU_ARMA(UfcsUnknown, pe) {
                    BUG(sp, "Encountered UfcsUnknown");
                }
        }
        }
        TU_ARMA(Generic, e) {
        }
        TU_ARMA(TraitObject, e) {
            // Recurse?
            H::expand_associated_types_tp(sp, *this, e.mTrait, stack);
            for (auto& m : e.markers) {
                H::expand_associated_types_params(sp, *this, m.mParams, stack);
            }
        }
        TU_ARMA(ErasedType, e) {
            // Recurse?
        }
        TU_ARMA(Array, e) {
            ConvertHIRConstantEvaluateArraySize(sp, crate, visPath, e.size);
            expand_associated_types_inplace(sp, e.inner, stack);
        }
        TU_ARMA(Slice, e) {
            expand_associated_types_inplace(sp, e.inner, stack);
        }
        TU_ARMA(Tuple, e) {
            for (auto& sub : e) {
                expand_associated_types_inplace(sp, sub, stack);
            }
        }
        TU_ARMA(Borrow, e) {
            expand_associated_types_inplace(sp, e.inner, stack);
        }
        TU_ARMA(Pointer, e) {
            expand_associated_types_inplace(sp, e.inner, stack);
        }
        TU_ARMA(NamedFunction, e) {
        TU_MATCH_HDRA( (e.path.mData), {)
        TU_ARMA(Generic, pe) {
                    //ConvertHIR_ConstantEvaluate_MethodParams(sp, m_crate, m_vis_path, m_impl_generics, m_item_generics, *e.binding.get_generics(), pe.m_params);
                    H::expand_associated_types_params(sp, *this, pe.mParams, stack);
                }
                TU_ARMA(UfcsInherent, pe) {
                    expand_associated_types_inplace(sp, pe.type, stack);
                    H::expand_associated_types_params(sp, *this, pe.params, stack);
                }
                TU_ARMA(UfcsKnown, pe) {
                    expand_associated_types_inplace(sp, pe.type, stack);
                    H::expand_associated_types_params(sp, *this, pe.params, stack);
                    H::expand_associated_types_params(sp, *this, pe.trait.mParams, stack);
                }
                TU_ARMA(UfcsUnknown, pe) {
                    BUG(sp, "Encountered UfcsUnknown");
                }
        }
        // TODO: Should this re-populate `def`? Not right now, assuming it's set once only
        }
        TU_ARMA(Function, e) {
            for (auto& ty : e.argTypes) {
                expand_associated_types_inplace(sp, ty, stack);
            }
            expand_associated_types_inplace(sp, e.mRettype, stack);
        }
        TU_ARMA(NodeType, e) {
            // Recurse? Nah.
        }
    }
            input = crate.types.intern(mv$(data));
        }

        bool TraitResolution::expandAssociatedTypesInplaceUfcsInherent(const Span& sp, ::HIR::TypeRef& input, LList<const ::HIR::TypeData*> stack) const {
            TRACE_FUNCTION_FR(input, input);
            ASSERT_BUG(sp, input->is_Path() && input->as_Path().path.mData.is_UfcsInherent(), input);

            const auto& pe = input->as_Path().path.mData.as_UfcsInherent();
            const ::HIR::TypeAlias* alias = nullptr;
            const ::HIR::GenericParams* impl_params_def = nullptr;
            const ::HIR::TypeImpl* selected_impl = nullptr;
            ::HIR::PathParams impl_params;
            ::HIR::Compare bestMatch = ::HIR::Compare::Unequal;
            static const ::HIR::PathParams no_trait_params;

            crate.find_type_impls(pe.type, ivars.callbackResolveInfer(), [&](const auto& impl) {
                const auto item_it = impl.types.find(pe.item);
                if (item_it == impl.types.end()) {
                    return false;
                }

                ::HIR::PathParams candidateParams;
                const auto match = this->ftic_check_params(
                    sp,
                    ::HIR::SimplePath(),
                    nullptr,
                    pe.type,
                    impl.mParams,
                    no_trait_params,
                    impl.mType,
                    candidateParams
                );
                if (match != ::HIR::Compare::Unequal
                    && (bestMatch == ::HIR::Compare::Unequal || match == ::HIR::Compare::Equal)) {
                    alias = &item_it->second.data;
                    impl_params_def = &impl.mParams;
                    selected_impl = &impl;
                    impl_params = mv$(candidateParams);
                    bestMatch = match;
                }
                return bestMatch == ::HIR::Compare::Equal;
            });

            if (!alias) {
                DEBUG("No inherent associated type candidate for " << input);
                return false;
            }

            ConvertHIRConstantEvaluateMethodParams(
                sp,
                crate,
                visPath,
                implGenerics,
                itemGenerics,
                impl_params_def,
                impl_params
            );
            if (inherentTypeConstraint) {
                auto selected_type = MonomorphStatePtr(crate.types, nullptr, &impl_params, nullptr).monomorph_type(sp, selected_impl->mType);
                inherentTypeConstraint(sp, pe.type, selected_type);
            }

            auto item_params = pe.params.clone();
            if (item_params.mLifetimes.empty()) {
                item_params.mLifetimes.resize(alias->mParams.mLifetimes.size());
            }
            if (item_params.mLifetimes.size() != alias->mParams.mLifetimes.size()
                || item_params.types.size() != alias->mParams.types.size()
                || item_params.values.size() != alias->mParams.values.size()) {
                ERROR(sp, E0000, "Incorrect generic arguments for inherent associated type " << input);
            }
            ConvertHIRConstantEvaluateMethodParams(
                sp,
                crate,
                visPath,
                implGenerics,
                itemGenerics,
                &alias->mParams,
                item_params
            );

            input = MonomorphStatePtr(crate.types, pe.type, &impl_params, &item_params).monomorph_type(sp, alias->mType);
            return true;
        }

        void TraitResolution::expandAssociatedTypesInplaceUfcsKnown(const Span& sp, ::HIR::TypeRef& input, LList<const ::HIR::TypeData*> stack) const {
            TRACE_FUNCTION_FR("input=" << input, input);
            auto data = input->cloneData();
            auto& builderE = data.as_Path();
            auto& builderPe = builderE.path.mData.as_UfcsKnown();

            expand_associated_types_inplace(sp, builderPe.type, stack);
            for (auto& ty : builderPe.trait.mParams.types) {
                expand_associated_types_inplace(sp, ty, stack);
            }
            input = crate.types.intern(mv$(data));
            const auto& e = input->as_Path();
            const auto& pe = e.path.mData.as_UfcsKnown();
            auto mark_opaque = [&]() {
                auto opaque_data = input->cloneData();
                opaque_data.as_Path().binding = ::HIR::TypePathBinding::make_Opaque({});
                input = crate.types.intern(mv$(opaque_data));
            };

            // Ignore unbounder infer literals
            if (pe.type->is_Infer() && !pe.type->as_Infer().is_lit()) {
                return;
            }
            // ATYs of placeholders are kept as unknown
            if (pe.type->is_Generic() && pe.type->as_Generic().is_placeholder()) {
                return;
            }

            // If there are impl params present, return early
            // TODO: There is still information available for placeholders (if the impl block is available)
            {
                auto cb = [](const ::HIR::TypeData* ty) {
                    return !(ty->is_Generic() && ty->as_Generic().is_placeholder());
                };
                bool has_impl_placeholders = false;
                if (!visit_ty_with(pe.type, cb)) {
                    has_impl_placeholders = true;
                }
                for (const auto& ty : pe.trait.mParams.types) {
                    if (!visit_ty_with(ty, cb)) {
                        has_impl_placeholders = true;
                    }
                }
                if (has_impl_placeholders) {
                    DEBUG("Has placeholder, skip");
                    // TODO: Why opaque? Like ivars, these could resolve in the future.
                    //DEBUG("Has placeholder, mark opaque.");
                    //e.binding = ::HIR::TypePathBinding::make_Opaque({});
                    return;
                }
            }

            // Search for the actual trait containing this associated type
            ::HIR::GenericPath trait_path;
            if (!this->trait_contains_type(sp, pe.trait, this->crate.get_trait_by_path(sp, pe.trait.mPath), pe.item.c_str(), trait_path)) {
                BUG(sp, "Cannot find associated type " << pe.item << " anywhere in trait " << pe.trait);
            }
            //pe.trait = mv$(trait_path);

            // Special type-specific rules
    TU_MATCH_HDRA( (*pe.type), {)
    default:
        // No special handling
    TU_ARMA(NodeType, te) {
        TU_MATCH_HDRA((te), {)
        // - If it's a closure, then the only trait impls are those generated by typeck
        TU_ARMA(Closure, node_p) {
                    if (pe.trait.mPath == mLangFn || pe.trait.mPath == mLangFnMut || pe.trait.mPath == mLangFnOnce) {
                        if (pe.item == "Output") {
                            input = node_p->returnType;
                            return;
                        } else {
                            ERROR(sp, E0000, "No associated type " << pe.item << " for trait " << pe.trait);
                        }
                    }
                    // TODO: Fall through? Maybe there's a generic impl that could match.
                }
                TU_ARMA(Generator, node_p) {
                    if (pe.trait.mPath == this->mLangGenerator) {
                        if (pe.item == "Return") {
                            input = node_p->returnType;
                            return;
                        } else if (pe.item == "Yield") {
                            input = node_p->yieldTy;
                            return;
                        } else {
                            ERROR(sp, E0000, "No associated type " << pe.item << " for trait " << pe.trait);
                        }
                    }
                    // Fall through for generic impls
                }
                TU_ARMA(Async, node_p) {
                    // TODO: `Future` impl
                }
        }
        }
        TU_ARMA(Function, te) {
            if (te.mAbi == ABI_RUST && !te.is_unsafe) {
                if (pe.trait.mPath == mLangFn || pe.trait.mPath == mLangFnMut || pe.trait.mPath == mLangFnOnce) {
                    if (pe.item == "Output") {
                        input = te.mRettype;
                        return;
                    } else {
                        ERROR(sp, E0000, "No associated type " << pe.item << " for trait " << pe.trait);
                    }
                }
            }
        }
        // If it's a TraitObject, then maybe we're asking for a bound
        TU_ARMA(TraitObject, te) {
            const auto& data_trait = te.mTrait.mPath;
            if (pe.trait.mPath == data_trait.mPath) {
                auto cmp = ::HIR::Compare::Equal;
                if (pe.trait.mParams.types.size() != data_trait.mParams.types.size()) {
                    cmp = ::HIR::Compare::Unequal;
                } else {
                    for (unsigned int i = 0; i < pe.trait.mParams.types.size(); i++) {
                        const auto& l = pe.trait.mParams.types[i];
                        const auto& r = data_trait.mParams.types[i];
                        cmp &= l->compareWithPlaceholders(sp, r, ivars.callbackResolveInfer());
                    }
                }
                if (cmp != ::HIR::Compare::Unequal) {
                    auto it = te.mTrait.typeBounds.find(pe.item);
                    if (it == te.mTrait.typeBounds.end()) {
                        // TODO: Mark as opaque and return.
                        // - Why opaque? It's not bounded, don't even bother
                        TODO(sp, "Handle unconstrained associate type " << pe.item << " from " << pe.type);
                    }

                    auto hrl_pps = te.mTrait.hrtbs ? te.mTrait.hrtbs->make_empty_params(true) : HIR::PathParams();
                    input = MonomorphHrlsOnly(crate.types, hrl_pps).monomorph_type(sp, it->second.type);
                    return;
                }
            }

            // - Check if the desired trait is a supertrait of this.
            // NOTE: `params` (aka des_params) is not used (TODO)
            bool is_supertrait = this->find_named_trait_in_trait(sp, pe.trait.mPath, pe.trait.mParams, *te.mTrait.traitPtr, data_trait.mPath, data_trait.mParams, pe.type, [&](const HIR::TraitPath& i_tp) {
                // The above is just the monomorphised params and associated set. Comparison is still needed.
                auto cmp = this->comparePp(sp, i_tp.mPath.mParams, pe.trait.mParams);
                if (cmp != ::HIR::Compare::Unequal) {
                    // Search for bounded types in this TraitPath (from `find_named_trait_in_trait` and in the original input TraitPath `te.m_trait`)
                    auto it = i_tp.typeBounds.find(pe.item);
                    if (it == i_tp.typeBounds.end()) {
                        // NOTE: (currently) there can only be one trait with this name, so if we found this trait and the item is present - good.
                        it = te.mTrait.typeBounds.find(pe.item);
                    }
                    if (it != te.mTrait.typeBounds.end()) {
                        // Remove HRLs (TODO: Match them? not really needed in this stage I think)
                        auto hrl_pps = te.mTrait.hrtbs ? te.mTrait.hrtbs->make_empty_params(true) : i_tp.hrtbs ? i_tp.hrtbs->make_empty_params(true) : HIR::PathParams();
                        input = MonomorphHrlsOnly(crate.types, hrl_pps).monomorph_type(sp, it->second.type);
                        return true;
                    }
                    return false;
                }
                return false;
            });
            if (is_supertrait) {
                return;
            }
        }
        // If it's a ErasedType, then maybe we're asking for a bound
        TU_ARMA(ErasedType, te) {
            DEBUG("- ErasedType");
            for (const auto& trait : te.traits) {
                const auto& trait_gp = trait.mPath;
                if (trait_path.mPath == trait_gp.mPath) {
                    auto cmp = ::HIR::Compare::Equal;
                    if (trait_path.mParams.types.size() != trait_gp.mParams.types.size()) {
                        cmp = ::HIR::Compare::Unequal;
                    } else {
                        for (unsigned int i = 0; i < trait_path.mParams.types.size(); i++) {
                            const auto& l = trait_path.mParams.types[i];
                            const auto& r = trait_gp.mParams.types[i];
                            cmp &= l->compareWithPlaceholders(sp, r, ivars.callbackResolveInfer());
                        }
                    }
                    if (cmp != ::HIR::Compare::Unequal) {
                        auto hrls = get_hrls(crate.types, sp, trait.hrtbs, trait_gp.mParams, trait_path.mParams);
                        {
                            auto it = trait.typeBounds.find(pe.item);
                            if (it != trait.typeBounds.end()) {
                                input = MonomorphHrlsOnly(crate.types, hrls).monomorph_type(sp, it->second.type);
                                return;
                            }
                        }
                        // Mark as opaque and return, and ensure that the bounds are added to the bounds cache
                        mark_opaque();
                        {
                            auto it = trait.traitBounds.find(pe.item);
                            if (it != trait.traitBounds.end()) {
                                for (const auto& bound : it->second.traits) {
                                    const_cast<TraitResolution&>(*this).prepIndexesAddTraitBound(sp, nullptr, input, bound.clone());
                                }
                            }
                        }
                        return;
                    }
                }

                // - Check if the desired trait is a supertrait of this.
                // NOTE: `params` (aka des_params) is not used (TODO)
                bool is_supertrait = this->find_named_trait_in_trait(sp, trait_path.mPath, trait_path.mParams, *trait.traitPtr, trait_gp.mPath, trait_gp.mParams, pe.type, [&](const HIR::TraitPath& i_tp) {
                    if (i_tp.hrtbs && !i_tp.hrtbs->is_empty() && trait.hrtbs && !trait.hrtbs->is_empty()) {
                        TODO(sp, "Nested HRTBs");
                    }
                    // The above is just the monomorphised params and associated set. Comparison is still needed.
                    auto cmp = this->comparePp(sp, i_tp.mPath.mParams, pe.trait.mParams);
                    if (cmp != ::HIR::Compare::Unequal) {
                        //auto hrls = get_hrls(sp, trait.m_hrtbs, i_tp.m_path.m_params, trait_path.m_params);
                        auto it = i_tp.typeBounds.find(pe.item);
                        if (it == i_tp.typeBounds.end()) {
                            // NOTE: (currently) there can only be one trait with this name, so if we found this trait and the item is present - good.
                            it = trait.typeBounds.find(pe.item);
                        }
                        if (it != trait.typeBounds.end()) {
                            auto hrls = get_hrls(crate.types, sp, (trait.hrtbs && !trait.hrtbs->is_empty()) ? trait.hrtbs.get() : i_tp.hrtbs.get(), i_tp.mPath.mParams, trait_path.mParams);
                            DEBUG("hrls = " << hrls);
                            input = MonomorphHrlsOnly(crate.types, hrls).monomorph_type(sp, it->second.type);
                            return true;
                        }
                        return false;
                    }
                    return false;
                });
                if (is_supertrait) {
                    return;
                }
            }
        }
    }

    // 1. Bounds
    bool rv = false;
    bool found_bound_with_no_type = false;
    enum class ResultType {
        Opaque,
        LeaveUnbound,
        Recurse,
    } result_type = ResultType::Opaque;

    if(!rv)
    {
        auto it = typeEqualities.find(input);
        if (it == typeEqualities.end()) {
            it = ::std::find_if(typeEqualities.begin(), typeEqualities.end(), [&](const auto& entry) {
                return entry.first->equals_ignoring_regions(input);
            });
        }
        if (it != typeEqualities.end()) {
            result_type = ResultType::Recurse;
            DEBUG("Equality: for" << it->second.hrbs.fmt_args());
            MatchHrls m{crate.types, &it->second.hrbs};
            input->match_test_generics_fuzz(sp, it->first, HIR::ResolvePlaceholdersNop(), m);
            input = MonomorphHrlsOnly(crate.types, m.hrls).monomorph_type(sp, it->second.ty);
            rv = true;
        }
    }
    if(!rv)
    {
        rv = this->iterate_bounds_traits(sp, pe.type, trait_path.mPath, [&](HIR::Compare cmp, const ::HIR::TypeData* boundType, const ::HIR::GenericPath& boundTrait, const CachedBound& boundInfo) -> bool {
            DEBUG("[expand_associated_types_inplace__UfcsKnown] Trait bound - " << boundType << " : " << boundTrait);
            // 2. Check if the trait (or any supertrait) includes pe.trait
            // TODO: If fuzzy, bail and leave unresolved?
            cmp &= boundTrait.compareWithPlaceholders(sp, trait_path, this->ivars.callbackResolveInfer());
            //if( cmp != HIR::Compare::Unequal ) {
            if (cmp == HIR::Compare::Equal) {
                auto it = boundInfo.assoc.find(pe.item);
                // 1. Check if the bounds include the desired item
                if (it == boundInfo.assoc.end()) {
                    // If not, assume it's opaque and return as such
                    // TODO: What happens if there's two bounds that overlap? 'F: FnMut<()>, F: FnOnce<(), Output=Bar>'
                    DEBUG("[expand_associated_types_inplace__UfcsKnown] Found impl for " << input << " but no bound on item");

                    // Flag so if no impl was found by the lower checks, it gets correctly set to Opaque (or left unbound)
                    found_bound_with_no_type = true;
                    if (cmp == HIR::Compare::Fuzzy) {
                        result_type = ResultType::LeaveUnbound;
                    } else {
                        result_type = ResultType::Opaque;
                    }
                    return false;
                } else {
                    result_type = ResultType::Recurse;
                    DEBUG("TraitBound");
                    input = it->second.type;
                }
                return true;
            }

            // - Didn't match
            return false;
        });
    }

    if( rv ) {
        assert(result_type == ResultType::Recurse); // Nothing else can happen without `rv` being false
        DEBUG("- Found replacement: " << input);
        this->expand_associated_types_inplace(sp, input, stack);
        return;
    }

    // If the type of this UfcsKnown is ALSO a UfcsKnown - Check if it's bounded by this trait with equality
    //  e.g. `<<Foo as Bar>::Baz as Trait2>::Type` may have an ATY bound `trait Bar { type Baz: Trait2<Type=...> }`
    // Use bounds on other associated types too (if `pe.type` was resolved to a fixed associated type)
    if(const auto* te_inner = pe.type->opt_Path())
    {
        if (const auto* pe_inner_p = te_inner->path.mData.opt_UfcsKnown()) {
            DEBUG("Checking inner bounds");
            const auto& pe_inner = *pe_inner_p;
            // TODO: Search for equality bounds on this associated type (pe_inner) that match the entire type (pe)
            // - Does simplification of complex associated types
            //
            ::HIR::GenericPath trait_path;
            if (!this->trait_contains_type(sp, pe_inner.trait, this->crate.get_trait_by_path(sp, pe_inner.trait.mPath), pe_inner.item.c_str(), trait_path)) {
                BUG(sp, "Cannot find associated type " << pe_inner.item << " anywhere in trait " << pe_inner.trait);
            }
            const auto& trait_ptr = this->crate.get_trait_by_path(sp, trait_path.mPath);
            const auto& assocTy = trait_ptr.types.at(pe_inner.item);

            // Resolve where Self=pe_inner.type (i.e. for the trait this inner UFCS is on)
            auto cbPlaceholdersTrait = MonomorphStatePtr(crate.types, pe_inner.type, &pe_inner.trait.mParams, &pe_inner.params);
            for (const auto& bound : assocTy.traitBounds) {
                auto it = bound.typeBounds.find(pe.item);
                if (it != bound.typeBounds.end()) {
                    auto source_trait = cbPlaceholdersTrait.monomorph_genericpath(sp, it->second.source_trait, false);
                    auto atyParams = cbPlaceholdersTrait.monomorph_path_params(sp, it->second.atyParams, false);
                    for (auto& t : source_trait.mParams.types) {
                        expand_associated_types_inplace(sp, t, stack);
                    }
                    for (auto& t : atyParams.types) {
                        expand_associated_types_inplace(sp, t, stack);
                    }
                    auto cmp = source_trait.compareWithPlaceholders(sp, pe.trait, ivars.callbackResolveInfer());
                    cmp &= atyParams.compareWithPlaceholders(sp, pe.params, ivars.callbackResolveInfer());
                    if (cmp == HIR::Compare::Equal) {
                        input = monomorphise_type_needed(it->second.type)
                            ? cbPlaceholdersTrait.monomorph_type(sp, it->second.type)
                            : it->second.type;
                        DEBUG("- Found replacement from " << source_trait << ": " << input);
                        this->expand_associated_types_inplace(sp, input, stack);
                        return;
                    }
                }

                auto boundTp = cbPlaceholdersTrait.monomorph_genericpath(sp, bound.mPath, false);
                for (auto& t : boundTp.mParams.types) {
                    expand_associated_types_inplace(sp, t, stack);
                }
                DEBUG("B " << bound.mPath);
                DEBUG("-> " << boundTp);

                // TODO: Find trait in this trait.
                const auto& boundTrait = crate.get_trait_by_path(sp, boundTp.mPath);
                bool replaced = this->find_named_trait_in_trait(sp, pe.trait.mPath, pe.trait.mParams, boundTrait, boundTp.mPath, boundTp.mParams, pe.type, [&](const HIR::TraitPath& tp) {
                    auto it = tp.typeBounds.find(pe.item);
                    if (it != tp.typeBounds.end()) {
                        input = it->second.type;
                        return true;
                    }
                    return false;
                });
                if (replaced) {
                    return;
                }
            }
            DEBUG("pe = " << pe.type << ", input = " << input);
        }
    }

    if (gTraitSolverConfig.globally) {
        bool normalized = false;
        bool ambiguous = false;
        this->find_trait_impls_next(
            sp,
            trait_path.mPath,
            trait_path.mParams,
            pe.type,
            [&](ImplRef impl, ::HIR::Compare certainty) {
                if (impl.is_ambiguous_identity()) {
                    ambiguous = true;
                    return true;
                }

                auto output = impl.get_type(crate.types, pe.item.c_str(), pe.params);
                if (output == ::HIR::TypeRef() || output == input) {
                    ambiguous = true;
                    return true;
                }
                input = ::std::move(output);
                normalized = true;
                ambiguous = certainty == ::HIR::Compare::Fuzzy;
                return true;
            },
            pe.item.c_str(),
            nullptr,
            &pe.params
        );
        if (normalized) {
            this->expand_associated_types_inplace(sp, input, stack);
            return;
        }
        if (ambiguous) {
            // A rigid unresolved projection is still a usable alias: method
            // lookup and associated-type bounds must be allowed to inspect it.
            // Only projections containing inference variables stay unbound,
            // because those are obligations the constraint loop must retry.
            if (!this->ivars.type_contains_ivars(input, false)) {
                mark_opaque();
            }
            return;
        }
    }

    if( this->find_trait_impls_magic(sp, trait_path.mPath, trait_path.mParams, pe.type, [&](auto impl, auto qual)->bool {
        DEBUG("[expand_associated_types__UfcsKnown] Found " << impl << " qual=" << qual);
        // If it's a fuzzy match, keep going (but count if a concrete hasn't been found)
        if (qual == ::HIR::Compare::Fuzzy) {
        } else {
            auto ty = impl.get_type(crate.types, pe.item.c_str(), pe.params);
            if (ty == ::HIR::TypeRef()) {
                DEBUG("Assuming that " << input << " is an opaque name");
                mark_opaque();
            } else {
                input = mv$(ty);
            }
        }
        return true;
        }) )
    {
        return;
    }

    if( this->find_trait_impls_types(sp, trait_path.mPath, trait_path.mParams, pe.type, [&](auto impl, auto qual)->bool {
        DEBUG("[expand_associated_types__UfcsKnown] Found " << impl << " qual=" << qual);
        // If it's a fuzzy match, keep going (but count if a concrete hasn't been found)
        if (qual == ::HIR::Compare::Fuzzy) {
        } else {
            auto ty = impl.get_type(crate.types, pe.item.c_str(), pe.params);
            if (ty == ::HIR::TypeRef()) {
                DEBUG("Assuming that " << input << " is an opaque name");
                mark_opaque();
            } else {
                input = mv$(ty);
            }
        }
        return true;
        }) )
    {
        return;
    }

    // 2. Crate-level impls
    DEBUG("Searching for impl");
    bool    canFuzz = true;
    unsigned int    count = 0;
    bool is_specialisable = false;
    bool is_bound = false;
    ImplRef bestImpl;
    auto cbFindImpl = [&](ImplRef impl, HIR::Compare qual)->bool {
        DEBUG("[expand_associated_types__UfcsKnown] Found " << impl << " qual=" << qual);
        // If it's a fuzzy match, keep going (but count if a concrete hasn't been found)
        if (qual == ::HIR::Compare::Fuzzy) {
            if (canFuzz) {
                count += 1;
                if (count == 1 && impl.get_impl_type(crate.types)->tag() == pe.type->tag()) {
                    bestImpl = mv$(impl);
                }
            }
            return false;
        } else {
            // If a fuzzy match could have been seen, ensure that best_impl is unsed
            if (canFuzz) {
                bestImpl = ImplRef();
                canFuzz = false;
            }

            // If the type is specialisable
            if (impl.type_is_specialisable(pe.item.c_str())) {
                // Check if this is more specific
                if (impl.more_specific_than(crate.types, bestImpl)) {
                    is_specialisable = true;
                    bestImpl = mv$(impl);
                }
                return false;
            } else {
                auto ty = impl.get_type(crate.types, pe.item.c_str(), pe.params);
                if (ty == ::HIR::TypeRef()) {
                    if (is_bound) {
                        return false;
                    } else {
                        if (pe.item.compare(0, strlen(ATY_PREFIX_ERASED), ATY_PREFIX_ERASED) == 0) {
                            DEBUG("Erased (ITIT), setting opaque");
                            mark_opaque();
                            return true;
                        } else {
                            ERROR(sp, E0000, "Couldn't find assocated type " << pe.item << " in impl of " << pe.trait << " for " << pe.type);
                        }
                    }
                }

                if (impl.has_magic_params()) {
                }

                // TODO: What if there's multiple impls?
                DEBUG("Converted UfcsKnown - " << e.path << " = " << ty);
                input = mv$(ty);
                return true;
            }
        }
        };

    rv = this->find_trait_impls_crate(sp, trait_path.mPath, trait_path.mParams, pe.type, cbFindImpl);
    if( !rv ) {
        is_bound = true;
        rv = find_trait_impls_bound(sp, trait_path.mPath, trait_path.mParams, pe.type, cbFindImpl);
    }
    if( !rv && bestImpl.is_valid() ) {
        if (canFuzz && count > 1) {
            // Fuzzy match with multiple choices - can't know yet
        } else if (is_specialisable) {
            if (!this->ivars.type_contains_ivars(input, false)) {
                DEBUG("Assuming opaque - specialisable impl");
                mark_opaque();
            } else {
                DEBUG("Derferring - specialisable impl (ivars present)");
            }
            return;
        } else {
            auto ty = bestImpl.get_type(crate.types, pe.item.c_str(), pe.params);
            if (ty == ::HIR::TypeRef()) {
                if (!this->ivars.type_contains_ivars(input, false)) {
                    DEBUG("Assuming opaque - best impl didn't have ATY");
                    mark_opaque();
                } else {
                    DEBUG("Derferring - best impl didn't have ATY (ivars present)");
                }
                return;
                //ERROR(sp, E0000, "Couldn't find assocated type " << pe.item << " in impl of " << pe.trait << " for " << pe.type);
            }

            // Try again later?
            if (bestImpl.has_magic_params()) {
                DEBUG("- Placeholder parameters present in impl, can't expand");
                return;
            }

            DEBUG("Converted UfcsKnown - " << e.path << " = " << ty);
            input = mv$(ty);
            rv = true;
        }
    }
    if( rv ) {
        expand_associated_types_inplace(sp, input, stack);
        return;
    }

    if( found_bound_with_no_type )
    {
        switch (result_type) {
            case ResultType::Opaque: {
                DEBUG("Assuming that " << input << " is an opaque name");
                mark_opaque();
                ASSERT_BUG(
                    sp,
                    visit_ty_with(
                        input,
                        [](const HIR::TypeData* ty) {
                    return ty->is_ErasedType() || ty->is_Infer();
                }
                    ) || monomorphise_type_needed(input),
                    "Set opaque on a non-generic type: " << input
                );

                DEBUG("- " << typeEqualities.size() << " replacements");
                for (const auto& v : typeEqualities) {
                    DEBUG(" > " << v.first << " = " << v.second);
                }

                auto a = typeEqualities.find(input);
                if (a == typeEqualities.end()) {
                    a = ::std::find_if(typeEqualities.begin(), typeEqualities.end(), [&](const auto& entry) {
                        return entry.first->equals_ignoring_regions(input);
                    });
                }
                if (a != typeEqualities.end()) {
                    DEBUG("- Replace to " << a->second << " from " << input);
                    input = a->second.ty;
                }
                this->expand_associated_types_inplace(sp, input, stack);
            } break;
            case ResultType::Recurse:
                assert(false);
                break;
            case ResultType::LeaveUnbound:
                DEBUG("- Keep as unbound: " << input);
                break;
        }
        return;
    }

    // If there are no ivars in this path, set its binding to Opaque
    if( !this->ivars.type_contains_ivars(input, false) ) {
        // TODO: If the type is a generic or an opaque associated, we can't know.
        // - If the trait contains any of the above, it's unknowable
        // - Otherwise, it's an error
        DEBUG("Assuming that " << input << " is an opaque name");
        mark_opaque();
        DEBUG("Couldn't resolve associated type for " << input << " (and won't ever be able to)");
    }
    else {
        DEBUG("Couldn't resolve associated type for " << input << " (will try again later)");
    }
        }

        // -------------------------------------------------------------------------------------------------------------------
        //
        // -------------------------------------------------------------------------------------------------------------------
        bool TraitResolution::find_named_trait_in_trait(const Span& sp, const ::HIR::SimplePath& des, const ::HIR::PathParams& des_params, const ::HIR::Trait& trait_ptr, const ::HIR::SimplePath& trait_path, const ::HIR::PathParams& pp, const ::HIR::TypeData* target_type, t_cb_find_trait callback) const {
            TRACE_FUNCTION_F(des << des_params << " in " << trait_path << pp);
            if (pp.types.size() != trait_ptr.mParams.types.size()) {
                BUG(sp, "Incorrect number of parameters for trait " << trait_path);
            }

            DEBUG(trait_ptr.allParentTraits);
            auto monomorph_cb = MonomorphStatePtr(crate.types, target_type, &pp, nullptr);
            for (const auto& pt : trait_ptr.allParentTraits) {
                auto pt_mono = monomorph_cb.monomorph_traitpath(sp, pt, false);
                for (auto& ty : pt_mono.mPath.mParams.types) {
                    ty = this->expand_associated_types(sp, mv$(ty));
                }
                for (auto& ty : pt_mono.typeBounds) {
                    ty.second.type = this->expand_associated_types(sp, mv$(ty.second.type));
                }

                //DEBUG(pt << " => " << pt_mono);
                if (pt.mPath.mPath == des) {
                    DEBUG("Found potential " << pt_mono);
                    // NOTE: Doesn't quite work...
                    //auto cmp = this->compare_pp(sp, pt_mono.m_path.m_params, des_params);
                    //if( cmp != ::HIR::Compare::Unequal )
                    //{
                    if (callback(pt_mono)) {
                        return true;
                    }
                    //}
                }
            }

            return false;
        }

        bool TraitResolution::find_trait_impls_bound(const Span& sp, const ::HIR::SimplePath& trait, const ::HIR::PathParams& params, const ::HIR::TypeData* type, t_cb_trait_impl_r callback) const {
            TRACE_FUNCTION_F("trait = " << trait << params << ", type = " << type);
            const ::HIR::Path::Data::Data_UfcsKnown* assocInfo = nullptr;
            if (const auto* e = type->opt_Path()) {
                assocInfo = e->path.mData.opt_UfcsKnown();
            }

            // If the type is a fully unknown type, then don't bother looking?
            // - Ah, but what if the prams provide sufficient information?
            // - TODO: Determine if the params could provide enough info to be worth checking for bounds.
            if (type->is_Infer() && !type->as_Infer().is_lit()) {
                return false;
            }

            // NOTE: Even if the type is completely unknown (unbound UFCS), search the bound list.

            // TODO: A bound can imply something via its associated types. How deep can this go?
            // E.g. `T: IntoIterator<Item=&u8>` implies `<T as IntoIterator>::IntoIter : Iterator<Item=&u8>`
            // > Would maybe want a list of all explicit and implied bounds instead.
            {
                bool rv = this->iterate_bounds_traits(sp, type, trait, [&](HIR::Compare cmp, const HIR::TypeData* boundTy, const ::HIR::GenericPath& boundTrait, const CachedBound& boundInfo) -> bool {
                    const auto& stored_params = boundTrait.mParams;
                    ::HIR::PathParams normalised_params;
                    const ::HIR::PathParams* bParams = &stored_params;
                    if (::std::any_of(
                            stored_params.types.begin(), stored_params.types.end(),
                            [&](const auto& ty) { return this->has_associated_type(ty); }
                        )) {
                        normalised_params = stored_params.clone();
                        this->expand_associated_types_params(sp, normalised_params);
                        bParams = &normalised_params;
                    }

                    DEBUG("[find_trait_impls_bound] " << boundTrait << " for " << boundTy << " cmp = " << cmp);

                    // Check against `params`
                    DEBUG("[find_trait_impls_bound] Checking params " << params << " vs " << *bParams);
                    auto ord = cmp;
                    ord &= this->comparePp(sp, *bParams, params);
                    if (ord == ::HIR::Compare::Unequal) {
                        DEBUG("[find_trait_impls_bound] - Mismatch");
                        return false;
                    }
                    if (ord == ::HIR::Compare::Fuzzy) {
                        DEBUG("[find_trait_impls_bound] - Fuzzy match");
                    }
                    DEBUG("[find_trait_impls_bound] Match for" << boundInfo.hrbs.fmt_args() << " " << boundTy << " : " << boundTrait);
                    // Hand off to the closure, and return true if it does
                    // TODO: The type bounds are only the types that are specified.
                    auto hrls = get_hrls(crate.types, sp, boundInfo.hrbs, *bParams, params);
                    if (callback(ImplRef(std::move(hrls), boundTy, &boundTrait.mParams, &boundInfo.assoc, boundInfo.constness), ord)) {
                        return true;
                    }

                    return false;
                });
                if (rv) {
                    return rv;
                }
            }

            if (assocInfo) {
                bool rv = this->iterate_bounds_traits(sp, assocInfo->type, assocInfo->trait.mPath, [&](HIR::Compare cmp, const HIR::TypeData* boundTy, const ::HIR::GenericPath& boundTrait, const CachedBound& boundInfo) -> bool {
                    // Check the trait params
                    cmp &= this->comparePp(sp, boundTrait.mParams, assocInfo->trait.mParams);
                    if (cmp == ::HIR::Compare::Fuzzy) {
                        //TODO(sp, "Handle fuzzy matches searching for associated type bounds");
                    } else if (cmp == ::HIR::Compare::Unequal) {
                        return false;
                    }
                    auto outer_ord = cmp;

                    const auto& trait_ref = *boundInfo.trait_ptr;
                    const auto& at = trait_ref.types.at(assocInfo->item);
                    for (const auto& bound : at.traitBounds) {
                        if (bound.mPath.mPath == trait) {
                            auto monomorph_cb = MonomorphStatePtr(crate.types, assocInfo->type, &assocInfo->trait.mParams, nullptr);

                            DEBUG("- Found an associated type bound for this trait via another bound");
                            ::HIR::Compare ord = outer_ord;
                            if (monomorphise_pathparams_needed(bound.mPath.mParams)) {
                                // TODO: Use a compare+callback method instead
                                auto bParamsMono = monomorph_cb.monomorph_path_params(sp, bound.mPath.mParams, false);
                                this->expand_associated_types_params(sp, bParamsMono);
                                ord &= this->comparePp(sp, bParamsMono, params);
                            } else {
                                ord &= this->comparePp(sp, bound.mPath.mParams, params);
                            }
                            if (ord == ::HIR::Compare::Unequal) {
                                return false;
                            }
                            if (ord == ::HIR::Compare::Fuzzy) {
                                DEBUG("Fuzzy match");
                            }

                            auto tp_mono = monomorph_cb.monomorph_traitpath(sp, bound, false);
                            if (tp_mono.hrtbs) {
                                auto p = tp_mono.hrtbs->make_empty_params(true);
                                tp_mono = MonomorphHrlsOnly(crate.types, p).monomorph_traitpath(sp, tp_mono, true, true);
                            }
                            // - Expand associated types
                            this->expand_associated_types_params(sp, tp_mono.mPath.mParams);
                            for (auto& ty : tp_mono.typeBounds) {
                                ty.second.type = this->expand_associated_types(sp, mv$(ty.second.type));
                            }
                            DEBUG("- tp_mono = " << tp_mono);
                            // TODO: Instead of using `type` here, build the real type
                            if (callback(ImplRef(type, mv$(tp_mono.mPath.mParams), mv$(tp_mono.typeBounds), tp_mono.constness), ord)) {
                                return true;
                            }
                        }
                    }
                    return false;
                });
                if (rv) {
                    return true;
                }
            }
            return false;
        }

        bool TraitResolution::find_trait_impls_crate(const Span& sp, const ::HIR::SimplePath& trait, const ::HIR::PathParams* params_ptr, const ::HIR::TypeData* type, t_cb_trait_impl_r callback) const {
            // TODO: Have a global cache of impls that don't reference either generics or ivars

            static ::HIR::TraitPath::assocListT null_assoc;
            TRACE_FUNCTION_F(trait << FMT_CB(ss, if (params_ptr) { ss << *params_ptr; } else { ss << "<?>"; }) << " for " << type);

            CanonicalizeTraitGoal canonicalizer(crate.types);
            const auto canonicalType = canonicalizer.monomorph_type(sp, type, true);
            ::HIR::PathParams canonicalParams;
            const bool has_params = params_ptr != nullptr;
            if (has_params) {
                canonicalParams = canonicalizer.monomorph_path_params(
                    sp, *params_ptr, true
                );
            }

            for (const auto& activeGoal : legacyTraitGoalStack) {
                if (!activeGoal.matches(
                        trait, canonicalParams, has_params, canonicalType
                    )) {
                    continue;
                }

                // rustc treats an inductive recursive trait predicate as
                // ambiguous, while productive recursive traits are proven.
                const auto cmp = crate.get_trait_by_path(sp, trait).isCoinductive
                    ? ::HIR::Compare::Equal
                    : ::HIR::Compare::Fuzzy;
                DEBUG("Legacy trait goal recurred: " << trait
                    << FMT_CB(ss, if (params_ptr) { ss << *params_ptr; } else { ss << "<?>"; })
                    << " for " << type << ", result=" << cmp);
                return callback(ImplRef(type, params_ptr, &null_assoc), cmp);
            }

            // rustc's legacy solver has a second cycle check for fresh input
            // types.  Exact goal equality is not sufficient here: a blanket
            // candidate can replace one unknown with a newly-created unknown
            // on every step (for example, tuple Distribution impls).  If the
            // current fresh goal is compatible with an older goal for the
            // same trait, further candidate search is ambiguous.
            const auto type_is_fresh = [&](const ::HIR::TypeData* ty) {
                if (ivars.type_contains_ivars(ty, false)) {
                    return true;
                }
                return visit_ty_with(ty, [](const ::HIR::TypeData* inner) {
                    return inner->is_Generic()
                        && inner->as_Generic().is_placeholder();
                });
            };
            bool has_fresh_inputs = !has_params || type_is_fresh(type);
            if (has_params && !has_fresh_inputs) {
                has_fresh_inputs = ivars.pathparams_contain_ivars(
                    *params_ptr, false
                );
                for (const auto& param : params_ptr->types) {
                    has_fresh_inputs = has_fresh_inputs || type_is_fresh(param);
                }
                for (const auto& param : params_ptr->values) {
                    has_fresh_inputs = has_fresh_inputs
                        || param.is_Infer()
                        || (param.is_Generic()
                            && param.as_Generic().is_placeholder());
                }
            }

            if (has_fresh_inputs) {
                const auto resolve = ivars.callbackResolveInfer();
                for (const auto& activeGoal : legacyTraitGoalStack) {
                    if (activeGoal.trait != trait) {
                        continue;
                    }
                    if (canonicalType->compareWithPlaceholders(
                            sp, activeGoal.type, resolve
                        ) == ::HIR::Compare::Unequal) {
                        continue;
                    }
                    if (has_params && activeGoal.has_params
                        && canonicalParams.compareWithPlaceholders(
                            sp, activeGoal.params, resolve
                        ) == ::HIR::Compare::Unequal) {
                        continue;
                    }

                    DEBUG("Fresh legacy trait goal matched an active goal: "
                        << trait
                        << FMT_CB(ss, if (params_ptr) { ss << *params_ptr; } else { ss << "<?>"; })
                        << " for " << type << ", result=Fuzzy");
                    return callback(
                        ImplRef(type, params_ptr, &null_assoc),
                        ::HIR::Compare::Fuzzy
                    );
                }
            }

            legacyTraitGoalStack.emplace_back(
                trait, canonicalParams, has_params, canonicalType
            );
            struct StackGuard {
                ::std::vector<LegacyTraitGoal>& stack;
                ~StackGuard() {
                    stack.pop_back();
                }
            } guard{legacyTraitGoalStack};

            // Handle auto traits (aka OIBITs)
            if (crate.get_trait_by_path(sp, trait).isMarker) {

                // NOTE: Expected behavior is for Ivars to return false
                // TODO: Should they return Compare::Fuzzy instead?
                if (type->is_Infer()) {
                    return callback(ImplRef(type, params_ptr, &null_assoc), ::HIR::Compare::Fuzzy);
                }

                const ::HIR::TraitMarkings* markings = nullptr;
                if (const auto* e = type->opt_Path()) {
                    if (TU_TEST1(e->path.mData, Generic, .mParams.types.size() == 0)) {
                        markings = e->binding.get_trait_markings();
                    }
                }

                // NOTE: `markings` is only set if there's no type params to a path type
                // - Cache populated after destructure
                if (markings) {
                    auto it = markings->autoImpls.find(trait);
                    if (it != markings->autoImpls.end()) {
                        if (!it->second.conditions.empty()) {
                            TODO(sp, "Conditional auto trait impl");
                        } else if (it->second.is_impled) {
                            return callback(ImplRef(type, params_ptr, &null_assoc), ::HIR::Compare::Equal);
                        } else {
                            return false;
                        }
                    }
                }

                // - Search for positive impls for this type
                DEBUG("- Search positive impls");
                bool positive_found = false;
                this->crate.find_auto_trait_impls(trait, type, this->ivars.callbackResolveInfer(), [&](const auto& impl) -> bool {
                    // Skip any negative impls on this pass
                    if (impl.is_positive != true) {
                        return false;
                    }

                    DEBUG("[find_trait_impls_crate] - Auto Pos Found impl" << impl.mParams.fmt_args() << " " << trait << impl.traitArgs << " for " << impl.mType << " " << impl.mParams.fmt_bounds());

                    // Compare with `params`
                    HIR::PathParams impl_params;
                    auto match = this->ftic_check_params(sp, trait, params_ptr, type, impl.mParams, impl.traitArgs, impl.mType, impl_params);
                    if (match == ::HIR::Compare::Unequal) {
                        // If any bound failed, return false (continue searching)
                        return false;
                    }

                    auto monomorph = MonomorphStatePtr(crate.types, nullptr, &impl_params, nullptr);
                    // TODO: Ensure that there are no-longer any magic params?

                    auto ty_mono = monomorph.monomorph_type(sp, impl.mType, false);
                    auto argsMono = monomorph.monomorph_path_params(sp, impl.traitArgs, false);
                    // NOTE: Auto traits can't have items, so no associated types

                    positive_found = true;
                    DEBUG("[find_trait_impls_crate] Auto Positive callback(args=" << argsMono << ")");
                    return callback(ImplRef(mv$(ty_mono), mv$(argsMono), {}), match);
                });
                if (positive_found) {
                    // A positive impl was found, so return true (callback should have been called)
                    return true;
                }

                // - Search for negative impls for this type
                DEBUG("- Search negative impls");
                bool negative_found = this->crate.find_auto_trait_impls(trait, type, this->ivars.callbackResolveInfer(), [&](const auto& impl) {
                    // Skip any positive impls
                    if (impl.is_positive != false) {
                        return false;
                    }
                    DEBUG("[find_trait_impls_crate] - Found auto neg impl" << impl.mParams.fmt_args() << " " << trait << impl.traitArgs << " for " << impl.mType << " " << impl.mParams.fmt_bounds());

                    // Compare with `params`
                    HIR::PathParams impl_params;
                    auto match = this->ftic_check_params(sp, trait, params_ptr, type, impl.mParams, impl.traitArgs, impl.mType, impl_params);
                    if (match == ::HIR::Compare::Unequal) {
                        // If any bound failed, return false (continue searching)
                        return false;
                    }

                    DEBUG("[find_trait_impls_crate] - Found neg impl");
                    return true;
                });
                if (negative_found) {
                    // A negative impl _was_ found, so return false
                    return false;
                }

                auto cmp = this->checkAutoTraitImplDestructure(sp, trait, params_ptr, type);
                if (cmp != ::HIR::Compare::Unequal) {
                    if (markings) {
                        ASSERT_BUG(sp, cmp == ::HIR::Compare::Equal, "Auto trait with no params returned a fuzzy match from destructure - " << trait << " for " << type);
                        markings->autoImpls.insert(::std::make_pair(trait, ::HIR::TraitMarkings::AutoMarking{{}, true}));
                    }
                    return callback(ImplRef(type, params_ptr, &null_assoc), cmp);
                } else {
                    if (markings) {
                        markings->autoImpls.insert(::std::make_pair(trait, ::HIR::TraitMarkings::AutoMarking{{}, false}));
                    }
                    return false;
                }
            }

            // TODO: Don't search if ALL types are unbounded ivar (what about a tuple of unbounded?)
            // If the type is an unbounded ivar, don't search.
#if 1
            if (type->is_Infer() && !type->as_Infer().is_lit()) {
                return false;
            }
#elif 0
            if (type->is_Infer() && !type->as_Infer().is_lit()) {
                return this->crate.find_trait_impls(trait, type, this->ivars.callbackResolveInfer(), [&](const auto& impl) {
                    HIR::PathParams impl_params;
                    // Fill all params with placeholders?
                    return callback(ImplRef(mv$(impl_params), trait, impl), HIR::Compare::Fuzzy);
                });
            }
#endif

            return this->crate.find_trait_impls(trait, type, this->ivars.callbackResolveInfer(), [&](const HIR::TraitImpl& impl) {
                DEBUG("[find_trait_impls_crate] Found impl" << impl.mParams.fmt_args() << " " << trait << impl.traitArgs << " for " << impl.mType << " " << impl.mParams.fmt_bounds());
                // Compare with `params`
                HIR::PathParams impl_params;
                auto match = this->ftic_check_params(sp, trait, params_ptr, type, impl.mParams, impl.traitArgs, impl.mType, impl_params);
                if (match == ::HIR::Compare::Unequal) {
                    // If any bound failed, return false (continue searching)
                    DEBUG("[find_trait_impls_crate] - Params mismatch");
                    return false;
                }
                DEBUG("[find_trait_impls_crate] - Found with impl_params=" << impl_params);

                return callback(ImplRef(mv$(impl_params), crate.get_trait_by_path(sp, trait), trait, impl), match);
            });
        }

        ::HIR::Compare TraitResolution::checkAutoTraitImplDestructure(const Span& sp, const ::HIR::SimplePath& trait, const ::HIR::PathParams* params_ptr, const ::HIR::TypeData* type) const {
            TRACE_FUNCTION_F("trait = " << trait << ", type = " << type);
            // HELPER: Search for an impl of this trait for an inner type, and return the match type
            auto type_impls_trait = [&](const auto& inner_ty) -> ::HIR::Compare {
                auto l_res = ::HIR::Compare::Unequal;
                this->find_trait_impls(sp, trait, *params_ptr, inner_ty, [&](auto, auto cmp) {
                    l_res = cmp;
                    return (cmp == ::HIR::Compare::Equal);
                });
                DEBUG("[check_auto_trait_impl_destructure] " << inner_ty << " - " << l_res);
                return l_res;
            };

            // - If the type is a path (struct/enum/...), search for impls for all contained types.
    TU_MATCH_HDRA( (*type), { )
    default:
        // Otherwise, there's no negative so it must be positive
        return ::HIR::Compare::Equal;
        TU_ARMA(Path, e) {
            ::HIR::Compare res = ::HIR::Compare::Equal;
        TU_MATCH_HDRA( (e.path.mData), {)
        TU_ARMA(Generic, pe) { //(
                    ::HIR::TypeRef tmp;
                    auto monomorph = MonomorphStatePtr(crate.types, nullptr, &pe.mParams, nullptr);
                    // HELPER: Get a possibily monomorphised version of the input type (stored in `tmp` if needed)
                    auto monomorph_get = [&](const auto& ty) -> const ::HIR::TypeData* {
                        if (monomorphise_type_needed(ty)) {
                            return (tmp = this->expand_associated_types(sp, monomorph.monomorph_type(sp, ty)));
                        } else {
                            return ty;
                        }
                    };

            TU_MATCH_HDRA( (e.binding), {)
            TU_ARMA(Opaque, tpb) {
                            BUG(sp, "Opaque binding on generic path - " << type);
                        }
                        TU_ARMA(Unbound, tpb) {
                            BUG(sp, "Unbound binding on generic path - " << type);
                        }
                        TU_ARMA(Struct, tpb) {
                            const auto& str = *tpb;

                            // TODO: Somehow store a ruleset for auto traits on the type
                            // - Map of trait->does_impl for local fields?
                            // - Problems occur with type parameters
                            TU_MATCH(
                                ::HIR::Struct::Data,
                                (str.mData),
                                (se),
                                (Unit, ),
                                (Tuple,
                                 for (const auto& fld : se) {
                                     const auto& fld_ty_mono = monomorph_get(fld.ent);
                                     DEBUG("Struct::Tuple " << fld_ty_mono);
                                     res &= type_impls_trait(fld_ty_mono);
                                     if (res == ::HIR::Compare::Unequal) {
                                         return ::HIR::Compare::Unequal;
                                     }
                                 }),
                                (Named, for (const auto& fld : se) {
                                    DEBUG(type << " FIELD '" << fld.name << "' " << fld.ty);
                                    const auto& fld_ty_mono = monomorph_get(fld.ty);
                                    DEBUG("Struct::Named '" << fld.name << "' " << fld_ty_mono);

                                    res &= type_impls_trait(fld_ty_mono);
                                    if (res == ::HIR::Compare::Unequal) {
                                        return ::HIR::Compare::Unequal;
                                    }
                                })
                            )
                        }
                        TU_ARMA(Enum, tpb) {
                            if (const auto* e = tpb->mData.opt_Data()) {
                                for (const auto& var : *e) {
                                    const auto& fld_ty_mono = monomorph_get(var.type);
                                    DEBUG("Enum '" << var.name << "'" << fld_ty_mono);
                                    res &= type_impls_trait(fld_ty_mono);
                                    if (res == ::HIR::Compare::Unequal) {
                                        return ::HIR::Compare::Unequal;
                                    }
                                }
                            }
                        }
                        TU_ARMA(Union, tpb) {
                            for (const auto& fld : tpb->mVariants) {
                                const auto& fld_ty_mono = monomorph_get(fld.ty);
                                DEBUG("Union '" << fld.name << "' " << fld_ty_mono);
                                res &= type_impls_trait(fld_ty_mono);
                                if (res == ::HIR::Compare::Unequal) {
                                    return ::HIR::Compare::Unequal;
                                }
                            }
                        }
                        TU_ARMA(ExternType, tpb) {
                            TODO(sp, "Check auto trait destructure on extern type " << type);
                        }
            }
            DEBUG("- Nothing failed, calling callback");
                }
                TU_ARMA(UfcsUnknown, pe) {
                    BUG(sp, "UfcsUnknown in typeck - " << type);
                }
                TU_ARMA(UfcsKnown, pe) {
                    // If unbound, use Fuzzy {
                    if (e.binding.is_Unbound()) {
                        DEBUG("- Unbound UfcsKnown, returning Fuzzy");
                        return ::HIR::Compare::Fuzzy;
                    }
                    // Otherwise, it's opaque. Check the bounds on the trait.
                    if (TU_TEST1(*pe.type, Generic, .binding >> 8 == 2)) {
                        DEBUG("- UfcsKnown of placeholder, returning Fuzzy");
                        return ::HIR::Compare::Fuzzy;
                    }
                    TODO(sp, "Check trait bounds for bound on " << type);
                }
                TU_ARMA(UfcsInherent, pe) {
                    TODO(sp, "Auto trait lookup on UFCS Inherent type");
                }
        }
        return res;
        }
        TU_ARMA(Generic, e) {
            auto l_res = ::HIR::Compare::Unequal;
            this->find_trait_impls(sp, trait, *params_ptr, type, [&](auto, auto cmp) {
                l_res = cmp;
                return (cmp == ::HIR::Compare::Equal);
            });
            return l_res;
        }
        TU_ARMA(Tuple, e) {
            ::HIR::Compare res = ::HIR::Compare::Equal;
            for (const auto& sty : e) {
                res &= type_impls_trait(sty);
                if (res == ::HIR::Compare::Unequal) {
                    return ::HIR::Compare::Unequal;
                }
            }
            return res;
        }
        TU_ARMA(Array, e) {
            return type_impls_trait(e.inner);
        }
    }
    throw "";
        }

        ::HIR::Compare TraitResolution::ftic_check_params(
            const Span& sp,
            const ::HIR::SimplePath& trait,
            const ::HIR::PathParams* params_ptr,
            const ::HIR::TypeData* type,
            const ::HIR::GenericParams& impl_params_def,
            const ::HIR::PathParams& impl_trait_args,
            const ::HIR::TypeData* impl_ty,
            /*Out->*/ HIR::PathParams& out_impl_params,
            bool evaluate_bounds /*=true*/
        ) const {
            TRACE_FUNCTION_FR("impl" << impl_params_def.fmt_args() << " " << trait << impl_trait_args << " for " << impl_ty, out_impl_params);

            class GetParams: public ::HIR::MatchGenerics {
                Span sp;
                HIR::PathParams& out_impl_params;

            public:
                GetParams(Span sp, HIR::PathParams& out_impl_params)
                    : sp(sp)
                    , out_impl_params(out_impl_params)
                {
                }

                ::HIR::Compare match_ty(const ::HIR::GenericRef& g, const ::HIR::TypeData* ty, ::HIR::t_cb_resolve_type resolve_cb) override {
                    assert(g.binding < out_impl_params.types.size());
                    if (out_impl_params.types[g.binding] == HIR::TypeRef()) {
                        DEBUG("[ftic_check_params] Param " << g.binding << " = " << ty);
                        out_impl_params.types[g.binding] = ty;
                        return ::HIR::Compare::Equal;
                    } else {
                        DEBUG("[ftic_check_params] Param " << g.binding << " " << out_impl_params.types[g.binding] << " == " << ty);
                        auto rv = out_impl_params.types[g.binding]->compareWithPlaceholders(sp, ty, resolve_cb);
                        // If the existing is an ivar, replace with this.
                        // - TODO: Store the least fuzzy option, or store all fuzzy options?
                        if (rv == ::HIR::Compare::Fuzzy && out_impl_params.types[g.binding]->is_Infer()) {
                            // The same impl parameter can be learned through more than one
                            // component of an impl header.  `Y = X` followed by `Y = &X`
                            // is not a fuzzy refinement: it would require the infinite type
                            // `X = &X`.  Treat that header as disjoint instead of replacing
                            // the first constraint and letting specialization pick it.
                            const auto& existing_resolved = resolve_cb.get_type(sp, out_impl_params.types[g.binding]);
                            const auto* existing_infer = existing_resolved->opt_Infer();
                            if (existing_infer && existing_infer->index != ~0u) {
                                const bool recursive = visit_ty_with(ty, [&](const ::HIR::TypeData* inner) {
                                    if (const auto* infer = inner->opt_Infer()) {
                                        if (infer->index == ~0u) {
                                            return false;
                                        }
                                        const auto& resolved = resolve_cb.get_type(sp, inner);
                                        const auto* resolved_infer = resolved->opt_Infer();
                                        return resolved_infer && resolved_infer->index == existing_infer->index;
                                    }
                                    return false;
                                });
                                if (recursive) {
                                    DEBUG("[ftic_check_params] Param " << g.binding << " would form an infinite type " << existing_resolved << " = " << ty);
                                    return ::HIR::Compare::Unequal;
                                }
                            }
                            DEBUG("[ftic_check_params] Param " << g.binding << " fuzzy, use " << ty);
                            out_impl_params.types[g.binding] = ty;
                        }
                        return rv;
                    }
                }

                ::HIR::Compare match_val(const ::HIR::GenericRef& g, const ::HIR::ConstGeneric& sz) override {
                    ASSERT_BUG(sp, g.binding < out_impl_params.values.size(), "Value generic " << g << " out of range (" << out_impl_params.values.size() << ")");
                    if (sz.is_Infer()) {
                        ASSERT_BUG(sp, sz.as_Infer().index != ~0u, "");
                    }
                    if (out_impl_params.values[g.binding] == HIR::ConstGeneric()) {
                        DEBUG("[ftic_check_params] Value param " << g.binding << " = " << sz);
                        out_impl_params.values[g.binding] = sz.clone();
                        return ::HIR::Compare::Equal;
                    } else {
                        if (out_impl_params.values[g.binding] == sz) {
                            return ::HIR::Compare::Equal;
                        }
                        if (out_impl_params.values[g.binding].is_Infer()) {
                            if (!sz.is_Infer()) {
                                DEBUG("[ftic_check_params] Value param " << g.binding << " fuzzy, use " << sz);
                                out_impl_params.values[g.binding] = sz.clone();
                            }
                            return ::HIR::Compare::Fuzzy;
                        }
                        if (sz.is_Infer()) {
                            return ::HIR::Compare::Fuzzy;
                        }
                        TODO(Span(), "PtrImplMatcher::match_val " << g << "(" << out_impl_params.values[g.binding] << ") with " << sz);
                    }
                }

                ::HIR::Compare match_lft(
                    const ::HIR::GenericRef& g,
                    const ::HIR::LifetimeRef& lifetime
                ) override {
                    // Region equality is deliberately not part of impl-head
                    // selection, but the candidate substitution must retain
                    // a higher-ranked placeholder. It marks a fresh universe
                    // and is needed later to reject leaking outlives bounds.
                    // Elided/inferred regions carry no such information.
                    if (g.group() != ::HIR::GENERICImpl
                        || lifetime.binding == ::HIR::LifetimeRef::UNKNOWN
                        || lifetime.binding == ::HIR::LifetimeRef::INFER) {
                        return ::HIR::Compare::Equal;
                    }
                    ASSERT_BUG(
                        sp,
                        g.binding < out_impl_params.mLifetimes.size(),
                        "Lifetime generic " << g << " out of range ("
                            << out_impl_params.mLifetimes.size() << ")"
                    );
                    auto& current = out_impl_params.mLifetimes[g.binding];
                    if (current.binding == ::HIR::LifetimeRef::UNKNOWN
                        || current.binding == ::HIR::LifetimeRef::INFER
                        || (!current.is_hrl() && lifetime.is_hrl())) {
                        current = lifetime;
                    }
                    return ::HIR::Compare::Equal;
                }
            };

            GetParams get_params{sp, out_impl_params};

            out_impl_params.mLifetimes.resize(impl_params_def.mLifetimes.size());
            out_impl_params.types.resize(impl_params_def.types.size());
            out_impl_params.values.resize(impl_params_def.values.size());

            // NOTE: If this type references an associated type, the match will incorrectly fail.
            // - HACK: match_test_generics_fuzz has been changed to return Fuzzy if there's a tag mismatch and the LHS is an Opaque path
            auto match = ::HIR::Compare::Equal;
            match &= impl_ty->match_test_generics_fuzz(sp, type, this->ivars.callbackResolveInfer(), get_params);
            if (params_ptr) {
                const auto& params = *params_ptr;
                match &= impl_trait_args.match_test_generics_fuzz(sp, params, this->ivars.callbackResolveInfer(), get_params);
                if (match == ::HIR::Compare::Unequal) {
                    DEBUG("- Failed to match parameters - " << impl_trait_args << "+" << impl_ty << " != " << params << "+" << type);
                    return ::HIR::Compare::Unequal;
                }
            } else {
                if (match == ::HIR::Compare::Unequal) {
                    DEBUG("- Failed to match type - " << impl_ty << " != " << type);
                    return ::HIR::Compare::Unequal;
                }
            }

            DEBUG("Matched params: " << out_impl_params);

            // Some impl blocks have type params used as part of type bounds.
            // - A rough idea is to have monomorph return a third class of generic for params that are not yet bound.
            //  - compare_with_placeholders gets called on both ivars and generics, so that can be used to replace it once known.
            ::HIR::PathParams placeholders;
            RcString placeholder_name;
            bool placeholders_needed = false;
            {
                for (const auto& ty : out_impl_params.types) {
                    if (ty == HIR::TypeRef()) {
                        placeholders_needed = true;
                    }
                }
                for (const auto& val : out_impl_params.values) {
                    if (val == HIR::ConstGeneric()) {
                        placeholders_needed = true;
                    }
                }
            }
            if (placeholders_needed) {
                // NOTE: Not using interning, because these are short-lived
                // - Also, adding an interned string is quite expensive
                placeholder_name = RcString(FMT(
                    "ph_" << &impl_params_def << "_"
                    << freshImplPlaceholderCounter++
                ));
                for (unsigned int i = 0; i < out_impl_params.types.size(); i++) {
                    if (out_impl_params.types[i] == HIR::TypeRef()) {
                        if (placeholders.types.size() == 0) {
                            placeholders.types.resize(out_impl_params.types.size());
                        }
                        placeholders.types[i] = crate.types.generic(placeholder_name, 2 * 256 + i);
                        DEBUG("Create placeholder type for " << i << " = " << placeholders.types[i]);
                    }
                }
                for (unsigned int i = 0; i < out_impl_params.values.size(); i++) {
                    if (out_impl_params.values[i] == HIR::ConstGeneric()) {
                        if (placeholders.values.size() == 0) {
                            placeholders.values.resize(out_impl_params.values.size());
                        }
                        placeholders.values[i] = ::HIR::GenericRef(placeholder_name, 2 * 256 + i);
                        DEBUG("Create placeholder value for " << i << " = " << placeholders.values[i]);
                    }
                }
                DEBUG("Placeholders (" << placeholder_name << "): " << placeholders);
            } else {
                DEBUG("Placeholders not needed");
            }

            if (!evaluate_bounds) {
                for (size_t i = 0; i < out_impl_params.types.size(); i++) {
                    if (out_impl_params.types[i] == HIR::TypeRef()) {
                        out_impl_params.types[i] = ::std::move(placeholders.types[i]);
                    }
                }
                for (size_t i = 0; i < out_impl_params.values.size(); i++) {
                    if (out_impl_params.values[i] == HIR::ConstGeneric()) {
                        out_impl_params.values[i] = ::std::move(placeholders.values[i]);
                    }
                }
                return match;
            }
            auto cbInfer = this->ivars.callbackResolveInfer();

            struct Matcher: public ::HIR::MatchGenerics, public Monomorphiser {
                Span sp;
                const HIR::PathParams& impl_params;
                RcString placeholder_name;
                ::HIR::PathParams& placeholders;

                Matcher(HIR::TypeInterner& types, Span sp, const HIR::PathParams& impl_params, RcString placeholder_name, ::HIR::PathParams& placeholders)
                    : Monomorphiser(types)
                    , sp(sp)
                    , impl_params(impl_params)
                    , placeholder_name(placeholder_name)
                    , placeholders(placeholders)
                {
                }

                ::HIR::Compare match_ty(const ::HIR::GenericRef& g, const ::HIR::TypeData* ty, ::HIR::t_cb_resolve_type resolve_cb) override {
                    if (const auto* e = ty->opt_Generic()) {
                        if (e->binding == g.binding && e->name == g.name) {
                            return ::HIR::Compare::Equal;
                        }
                    }
                    if (g.is_placeholder() && g.name == placeholder_name) {
                        auto i = g.idx();
                        ASSERT_BUG(sp, impl_params.types[i] == HIR::TypeRef(), "Placeholder to populated type returned - " << impl_params.types[i] << " vs " << ty);
                        auto& ph = placeholders.types[i];
                        // TODO: Only want to do this if ... what?
                        // - Problem: This can poison the output if the result was fuzzy
                        // - E.g. `Q: Borrow<V>` can equate Q and V
                        if (ph->is_Generic() && ph->as_Generic().binding == g.binding) {
                            DEBUG("[ftic_check_params:cb_match] Bind placeholder " << i << " to " << ty);
                            ph = ty;
                            return ::HIR::Compare::Equal;
                        } else {
                            DEBUG("[ftic_check_params:cb_match] Compare placeholder " << i << " " << ph << " == " << ty);
                            return ph->compareWithPlaceholders(sp, ty, resolve_cb);
                        }
                    } else {
                        if (g.is_placeholder()) {
                            DEBUG("[ftic_check_params:cb_match] External impl param " << g);
                            return ::HIR::Compare::Fuzzy;
                        }
                        // If the RHS is a non-literal ivar, return fuzzy
                        if (ty->is_Infer() && !ty->as_Infer().is_lit()) {
                            return ::HIR::Compare::Fuzzy;
                        }
                        // If the RHS is an unbound UfcsKnown, also fuzzy
                        if (ty->is_Path() && ty->as_Path().binding.is_Unbound()) {
                            return ::HIR::Compare::Fuzzy;
                        }
                        if (ty->is_Generic() && ty->as_Generic().is_placeholder()) {
                            return ::HIR::Compare::Fuzzy;
                        }
                        DEBUG("Unequal generic type - " << g << " != " << ty);
                        return ::HIR::Compare::Unequal;
                    }
                }

                ::HIR::Compare match_val(const ::HIR::GenericRef& g, const ::HIR::ConstGeneric& v) override {
                    if (const auto* e = v.opt_Generic()) {
                        if (e->binding == g.binding && e->name == g.name) {
                            return ::HIR::Compare::Equal;
                        }
                    }
                    if (g.is_placeholder() && g.name == placeholder_name) {
                        auto i = g.idx();
                        ASSERT_BUG(sp, impl_params.values[i] == HIR::ConstGeneric(), "Placeholder to populated value returned - " << impl_params.values[i] << " vs " << v);
                        auto& ph = placeholders.values[i];
                        if (ph.is_Generic() && ph.as_Generic().binding == g.binding) {
                            DEBUG("[ftic_check_params:cb_match] Bind placeholder " << i << " to " << v);
                            ph = v.clone();
                            return ::HIR::Compare::Equal;
                        } else {
                            DEBUG("[ftic_check_params:cb_match] Compare placeholder " << i << " " << ph << " == " << v);
                            TODO(Span(), "[ftic_check_params:cb_match] Compare placeholder " << i << " " << ph << " == " << v);
                            //return ph.compare_with_placeholders(sp, ty, resolve_cb);
                        }
                    } else {
                        if (g.is_placeholder()) {
                            DEBUG("[ftic_check_params:cb_match] External impl param " << g);
                            return ::HIR::Compare::Fuzzy;
                        }
                        // If the RHS is a non-literal ivar, return fuzzy
                        if (v.is_Infer()) {
                            return ::HIR::Compare::Fuzzy;
                        }
                        DEBUG("Unequal generic value - " << g << " != " << v);
                        return ::HIR::Compare::Unequal;
                    }
                }

                ::HIR::TypeRef get_type(const Span& sp, const ::HIR::GenericRef& ge) const override {
                    //if( ge.is_self() ) {
                    //    // TODO: `impl_type` or `des_type`
                    //    DEBUG("[find_impl__check_crate_raw] Self - " << impl_type << " or " << des_type);
                    //    //TODO(sp, "[find_impl__check_crate_raw] Self - " << impl_type << " or " << des_type);
                    //    return impl_type;
                    //}
                    ASSERT_BUG(sp, !ge.is_placeholder(), "[find_impl__check_crate_raw] Placeholder param seen - " << ge);
                    if (impl_params.types.at(ge.binding) != HIR::TypeRef()) {
                        return impl_params.types.at(ge.binding);
                    }
                    ASSERT_BUG(sp, placeholders.types.size() == impl_params.types.size(), "Placeholder size mismatch: " << placeholders.types.size() << " != " << impl_params.types.size());
                    return placeholders.types.at(ge.binding);
                }

                ::HIR::ConstGeneric get_value(const Span& sp, const ::HIR::GenericRef& val) const override {
                    ASSERT_BUG(sp, val.binding < 256, "Generic value binding in " << val << " out of range (>=256)");
                    ASSERT_BUG(sp, val.binding < impl_params.values.size(), "Generic value binding in " << val << " out of range (>= " << impl_params.values.size() << ")");
                    if (impl_params.values.at(val.binding) != HIR::ConstGeneric()) {
                        return impl_params.values.at(val.binding).clone();
                    }
                    ASSERT_BUG(sp, placeholders.values.size() == impl_params.values.size(), "Placeholder size mismatch: " << placeholders.values.size() << " != " << impl_params.values.size());
                    return placeholders.values.at(val.binding).clone();
                }

                ::HIR::LifetimeRef get_lifetime(const Span& sp, const ::HIR::GenericRef& g) const override {
                    ASSERT_BUG(sp, g.binding < 256, "Generic lifetime binding in " << g << " out of range (>=256)");
                    ASSERT_BUG(sp, g.binding < impl_params.mLifetimes.size(), "Generic lifetime binding in " << g << " out of range (>= " << impl_params.mLifetimes.size() << ")");
                    return impl_params.mLifetimes.at(g.binding);
                }
            };

            Matcher matcher{crate.types, sp, out_impl_params, placeholder_name, placeholders};

            //::std::vector<::HIR::TypeRef> saved_ph;
            //for(const auto& t : placeholders)
            //    saved_ph.push_back(t.clone());

            // Keep looping while placeholders are updated
            int loops = 0;
            HIR::PathParams last_placeholders;
            do {
                DEBUG(">> LOOP " << loops);
                ASSERT_BUG(sp, loops < 4, "Excessive iterations while resolving bound placeholders");
                loops += 1;
                last_placeholders = placeholders.clone();
                // Check bounds for this impl
                // - If a bound fails, then this can't be a valid impl
                for (const auto& bound : impl_params_def.bounds) {
            TU_MATCH_HDRA( (bound), {)
            TU_ARMA(Lifetime, be) {
                }
                TU_ARMA(TypeLifetime, be) {
                }
                TU_ARMA(TraitBound, be) {
                    DEBUG("Check bound " << be.type << " : " << be.trait);
                    static const HIR::GenericParams empty_params;
                    auto _ = matcher.push_hrb(be.hrtbs);
                    auto real_type = matcher.monomorph_type(sp, be.type, false);
                    auto real_trait = matcher.monomorph_traitpath(sp, be.trait, false);
                    // TODO: If `real_trait` has HRLs, replace them?
                    if (real_trait.hrtbs) {
                        auto p = real_trait.hrtbs->make_empty_params(true);
                        real_trait.hrtbs.reset();
                        real_trait = MonomorphHrlsOnly(crate.types, p).monomorph_traitpath(sp, real_trait, true);
                    }
                    real_type = this->expand_associated_types(sp, mv$(real_type));
                    for (auto& p : real_trait.mPath.mParams.types) {
                        p = this->expand_associated_types(sp, mv$(p));
                    }
                    for (auto& ab : real_trait.typeBounds) {
                        ab.second.type = this->expand_associated_types(sp, mv$(ab.second.type));
                    }
                    const auto& real_trait_path = real_trait.mPath;
                    DEBUG("- bound mono " << real_type << " : " << real_trait);
                    bool found_fuzzy_match = false;
                    // If the type is an unbound UFCS path, assume fuzzy
                    if (TU_TEST1(*real_type, Path, .binding.is_Unbound())) {
                        DEBUG("- Bounded type is unbound UFCS, assuming fuzzy match");
                        found_fuzzy_match = true;
                    }
                    // If the type is an ivar, but not a literal, assume fuzzy
                    if (TU_TEST1(*real_type, Infer, .is_lit() == false)) {
                        DEBUG("- Bounded type is an ivar, assuming fuzzy match");
                        found_fuzzy_match = true;
                    }
                    // NOTE: Save the placeholder state and restore if the result was Fuzzy
                    ::HIR::PathParams saved_ph = placeholders.clone();
                    ::HIR::PathParams fuzzy_ph;
                    unsigned num_fuzzy = 0;       //!< Number of detected fuzzy impls
                    bool fuzzy_compatible = true; //!< Indicates that the `fuzzy_ph` applies to all detected fuzzy impls
                    auto rv = this->find_trait_impls(sp, real_trait_path.mPath, real_trait_path.mParams, real_type, [&](auto impl, auto impl_cmp) {
                        // TODO: Save and restore placeholders if this isn't a full match
                        DEBUG("[ftic_check_params] impl_cmp = " << impl_cmp << ", impl = " << impl);
                        auto cmp = impl_cmp;
                        if (cmp == ::HIR::Compare::Fuzzy) {
                            // If the match was fuzzy, try again filling in with `cb_match`
                            auto i_ty = impl.get_impl_type(crate.types);
                            this->expand_associated_types_inplace(sp, i_ty, {});
                            auto i_tp = impl.get_trait_params(crate.types);
                            for (auto& t : i_tp.types) {
                                this->expand_associated_types_inplace(sp, t, {});
                            }
                            DEBUG("[ftic_check_params] " << real_type << " ?= " << i_ty);
                            cmp &= real_type->match_test_generics_fuzz(sp, i_ty, cbInfer, matcher);
                            DEBUG("[ftic_check_params] " << real_trait_path.mParams << " ?= " << i_tp);
                            cmp &= real_trait_path.mParams.match_test_generics_fuzz(sp, i_tp, cbInfer, matcher);
                            DEBUG("[ftic_check_params] - Re-check result: " << cmp);
                        }
                        for (const auto& assocBound : real_trait.typeBounds) {
                            ::HIR::TypeRef tmp;
                            const ::HIR::TypeData* ty;

                            tmp = impl.get_type(crate.types, assocBound.first.c_str(), assocBound.second.atyParams);
                            if (tmp == ::HIR::TypeRef()) {
                                // This bound isn't from this particular trait, go the slow way of using expand_associated_types
                                tmp = this->expand_associated_types(sp, crate.types.path(::HIR::Path(::HIR::Path::Data::Data_UfcsKnown{real_type, real_trait_path.clone(), assocBound.first, {}}), {}));
                                ty = tmp;
                            } else {
                                // Expand after extraction, just to make sure.
                                this->expand_associated_types_inplace(sp, tmp, {});
                                ty = this->ivars.get_type(tmp);
                            }
                            DEBUG("[ftic_check_params] - Compare " << ty << " and " << assocBound.second.type << ", matching generics");
                            // `ty` = Monomorphised actual type (< `be.type` as `be.trait` >::`assoc_bound.first`)
                            // `assoc_bound.second` = Desired type (monomorphised too)
                            auto cmpI = assocBound.second.type->match_test_generics_fuzz(sp, ty, cbInfer, matcher);
                            switch (cmpI) {
                                case ::HIR::Compare::Equal:
                                    DEBUG("Equal");
                                    break;
                                case ::HIR::Compare::Unequal:
                                    DEBUG("Assoc `" << assocBound.first << "` didn't match - " << ty << " != " << assocBound.second.type);
                                    cmp = ::HIR::Compare::Unequal;
                                    break;
                                case ::HIR::Compare::Fuzzy:
                                    // TODO: When a fuzzy match is encountered on a conditional bound, returning `false` can lead to an false negative (and a compile error)
                                    // BUT, returning `true` could lead to it being selected. (Is this a problem, should a later validation pass check?)
                                    DEBUG("[ftic_check_params] Fuzzy match assoc bound between " << ty << " and " << assocBound.second.type);
                                    cmp = ::HIR::Compare::Fuzzy;
                                    break;
                            }
                            if (cmp == ::HIR::Compare::Unequal) {
                                break;
                            }
                        }

                        DEBUG("[ftic_check_params] impl_cmp = " << impl_cmp << ", cmp = " << cmp);
                        if (cmp == ::HIR::Compare::Fuzzy) {
                            found_fuzzy_match |= true;
                            // `fuzzy_ph` is set (num_fuzzy > 0) then check if the PH set is equal, if not then flag not equal
                            if (num_fuzzy > 0 && fuzzy_ph != placeholders) {
                                DEBUG("Multiple fuzzy matches, placeholders mismatch: " << fuzzy_ph << " != " << placeholders);
                                fuzzy_compatible = false;
                            }
                            num_fuzzy += 1;

                            fuzzy_ph = ::std::move(placeholders);
                            // TODO: Should this do some form of reset?
                            placeholders.types.resize(fuzzy_ph.types.size());
                            placeholders.values.resize(fuzzy_ph.values.size());
                        }
                        if (cmp != ::HIR::Compare::Equal) {
                            // Restore placeholders
                            // - Maybe save the results for later?
                            DEBUG("[ftic_check_params] Restore placeholders: " << saved_ph);
                            DEBUG("[ftic_check_params] OVERWRITTEN placeholders: " << placeholders);
                            placeholders = saved_ph.clone();
                        }
                        // If the match isn't a concrete equal, return false (to keep searching)
                        return (cmp == ::HIR::Compare::Equal);
                    });
                    if (rv) {
                        DEBUG("- Bound " << real_type << " : " << real_trait_path << " matched");
                    } else if (found_fuzzy_match) {
                        DEBUG("- Bound " << real_type << " : " << real_trait_path << " fuzzed");
                        if (num_fuzzy == 0) {
                            DEBUG("No placeholders"); // `real_type` was infer
                        } else if (num_fuzzy == 1) {
                            DEBUG("Use placeholders " << fuzzy_ph);
                            placeholders = ::std::move(fuzzy_ph);
                        } else if (fuzzy_compatible) {
                            DEBUG("Multiple placeholders (" << num_fuzzy << "), but all equal " << fuzzy_ph);
                            placeholders = ::std::move(fuzzy_ph);
                        } else {
                            //
                            DEBUG("TODO: Multiple fuzzy matches (" << num_fuzzy << "), which placeholder set to use?");
                        }
                        match = ::HIR::Compare::Fuzzy;
                    } else if (TU_TEST1(*real_type, Infer, .ty_class == ::HIR::InferClass::None)) {
                        DEBUG("- Bound " << real_type << " : " << real_trait_path << " full infer type - make result fuzzy");
                        match = ::HIR::Compare::Fuzzy;
                    } else if (TU_TEST1(*real_type, Generic, .is_placeholder())) {
                        DEBUG("- Bound " << real_type << " : " << real_trait_path << " placeholder - make result fuzzy");
                        match = ::HIR::Compare::Fuzzy;
                    } else {
                        DEBUG("- Bound " << real_type << " : " << real_trait_path << " failed");
                        return ::HIR::Compare::Unequal;
                    }

                    //if( !rv ) {
                    //    placeholders = ::std::move(saved_ph);
                    //}
                }
                TU_ARMA(TypeEquality, be) {
                    TODO(sp, "Check bound " << be.type << " = " << be.other_type);
                }
            }
                }
            } while (placeholders != last_placeholders);

            for (size_t i = 0; i < out_impl_params.types.size(); i++) {
                if (out_impl_params.types[i] == HIR::TypeRef()) {
                    out_impl_params.types[i] = std::move(placeholders.types[i]);
                }
                ASSERT_BUG(sp, out_impl_params.types[i] != HIR::TypeRef(), "");
            }
            for (size_t i = 0; i < out_impl_params.values.size(); i++) {
                if (out_impl_params.values[i] == HIR::ConstGeneric()) {
                    out_impl_params.values[i] = std::move(placeholders.values[i]);
                }
                ASSERT_BUG(sp, out_impl_params.values[i] != HIR::ConstGeneric(), "");
            }

            for (size_t i = 0; i < impl_params_def.types.size(); i++) {
                if (impl_params_def.types.at(i).isSized) {
                    if (out_impl_params.types[i] != HIR::TypeRef()) {
                        auto cmp = type_is_sized(sp, out_impl_params.types[i]);
                        if (cmp == ::HIR::Compare::Unequal) {
                            DEBUG("- Sized bound failed for " << out_impl_params.types[i]);
                            return ::HIR::Compare::Unequal;
                        }
                    } else {
                        // TODO: Set match to fuzzy?
                    }
                }
            }

            return match;
        }

        namespace {
            bool trait_contains_method_inner(const ::HIR::Trait& trait_ptr, const RcString& name, const ::HIR::Function*& out_fcn_ptr) {
                auto it = trait_ptr.values.find(name);
                if (it != trait_ptr.values.end()) {
                    if (it->second.is_Function()) {
                        const auto& v = it->second.as_Function();
                        out_fcn_ptr = &v;
                        return true;
                    }
                }
                return false;
            }
        }

        const ::HIR::Function* TraitResolution::trait_contains_method(const Span& sp, const ::HIR::GenericPath& trait_path, const ::HIR::Trait& trait_ptr, const ::HIR::TypeData* self, const RcString& name, ::HIR::GenericPath& out_path) const {
            TRACE_FUNCTION_FR("trait_path=" << trait_path << ",name=" << name, out_path);
            const ::HIR::Function* rv = nullptr;

            if (trait_contains_method_inner(trait_ptr, name, rv)) {
                assert(rv);
                out_path = trait_path.clone();
                return rv;
            }

            auto monomorph_cb = MonomorphStatePtr(crate.types, self, &trait_path.mParams, nullptr);
            for (const auto& st : trait_ptr.allParentTraits) {
                if (trait_contains_method_inner(*st.traitPtr, name, rv)) {
                    assert(rv);
                    // TODO: HRLs
                    static ::HIR::GenericParams empty_hrtbs;
                    auto _h = monomorph_cb.push_hrb(st.hrtbs ? *st.hrtbs : empty_hrtbs);
                    out_path.mPath = st.mPath.mPath;
                    out_path.mParams = monomorph_cb.monomorph_path_params(sp, st.mPath.mParams, false);
                    return rv;
                }
            }
            return nullptr;
        }

        bool TraitResolution::trait_contains_type(const Span& sp, const ::HIR::GenericPath& trait_path, const ::HIR::Trait& trait_ptr, const char* name, ::HIR::GenericPath& out_path) const {
            TRACE_FUNCTION_FR(trait_path << " has " << name, out_path);

            auto it = trait_ptr.types.find(name);
            if (it != trait_ptr.types.end()) {
                DEBUG("- Found in cur");
                out_path = trait_path.clone();
                return true;
            }

            auto monomorph_cb = MonomorphStatePtr(crate.types, nullptr, &trait_path.mParams, nullptr);
            for (const auto& st : trait_ptr.allParentTraits) {
                if (st.traitPtr->types.count(name)) {
                    DEBUG("- Found in " << st);
                    out_path.mPath = st.mPath.mPath;
                    out_path.mParams = monomorph_cb.monomorph_path_params(sp, st.mPath.mParams, false);
                    return true;
                }
            }
            return false;
        }

        ::HIR::Compare TraitResolution::type_is_sized(const Span& sp, const ::HIR::TypeData* type) const {
            bool is_fuzzy = false;
            bool has_eq = false;
            if (!mLangSized.components().empty()) {
                has_eq = find_trait_impls(sp, mLangSized, ::HIR::PathParams{}, type, [&](auto, auto c) -> bool {
                    switch (c) {
                        case ::HIR::Compare::Equal:
                            return true;
                        case ::HIR::Compare::Fuzzy:
                            is_fuzzy = true;
                            return false;
                        case ::HIR::Compare::Unequal:
                            return false;
                    }
                    throw "";
                }, /*magic_trait_impls=*/false);
            }
            if (has_eq) {
                return ::HIR::Compare::Equal;
            } else if (is_fuzzy) {
                return ::HIR::Compare::Fuzzy;
            } else {
            }

    TU_MATCH_HDRA( (*type), {)
    default:
        // Any unknown - it's sized
    TU_ARMA(Infer, e) {
            switch (e.ty_class) {
                case ::HIR::InferClass::Integer:
                case ::HIR::InferClass::Float:
                    return ::HIR::Compare::Equal;
                default:
                    return ::HIR::Compare::Fuzzy;
            }
        }
        TU_ARMA(Primitive, e) {
            if (e == ::HIR::CoreType::Str) {
                return ::HIR::Compare::Unequal;
            }
        }
        TU_ARMA(Slice, e) {
            return ::HIR::Compare::Unequal;
        }
        TU_ARMA(Path, e) {
            // TODO: Check that only ?Sized parameters are !Sized
            TU_MATCHA(
                (e.binding),
                (pb),
                (
                    Unbound,
                    //
                ),
                (
                    Opaque,
                    // TODO: Check bounds
                ),
                (ExternType,
                 // Is it sized? No.
                 return ::HIR::Compare::Unequal;),
                (
                    Enum,
                    // HAS to be Sized
                ),
                (
                    Union,
                    // Pretty sure unions are Sized
                ),
                (Struct,
                 // Possibly not sized
                 switch (pb->structMarkings.dst_type) {
                     case ::HIR::StructMarkings::DstType::None:
                         break;
                     case ::HIR::StructMarkings::DstType::Possible:
                         // Check sized-ness of the unsized param
                         return type_is_sized(sp, e.path.mData.as_Generic().mParams.types.at(pb->structMarkings.unsized_param));
                     case ::HIR::StructMarkings::DstType::Slice:
                     case ::HIR::StructMarkings::DstType::TraitObject:
                         return ::HIR::Compare::Unequal;
                 })
            )
        }
        TU_ARMA(Generic, e) {
            switch (e.group()) {
                case 0:
                    return this->implGenerics->types.at(e.idx()).isSized ? ::HIR::Compare::Equal : ::HIR::Compare::Unequal;
                case 1:
                    return this->itemGenerics->types.at(e.idx()).isSized ? ::HIR::Compare::Equal : ::HIR::Compare::Unequal;
                default:
                    // Assume sized for anything else?
                    return ::HIR::Compare::Equal;
            }
        }
        TU_ARMA(ErasedType, e) {
            return e.isSized ? ::HIR::Compare::Equal : ::HIR::Compare::Unequal;
        }
        TU_ARMA(TraitObject, e) {
            return ::HIR::Compare::Unequal;
        }
    }
    return ::HIR::Compare::Equal;
        }

        ::HIR::Compare TraitResolution::type_is_copy(const Span& sp, const ::HIR::TypeData* ty) const {
            const auto& type = this->ivars.get_type(ty);
    TU_MATCH_HDRA( (*type), {)
    default: {
            bool is_fuzzy = false;
            bool has_eq = find_trait_impls(sp, mLangCopy, ::HIR::PathParams{}, ty, [&](auto, auto c) -> bool {
                switch (c) {
                    case ::HIR::Compare::Equal:
                        return true;
                    case ::HIR::Compare::Fuzzy:
                        is_fuzzy = true;
                        return false;
                    case ::HIR::Compare::Unequal:
                        return false;
                }
                throw "";
            }, /*magic_trait_impls=*/false);
            if (has_eq) {
                return ::HIR::Compare::Equal;
            } else if (is_fuzzy) {
                return ::HIR::Compare::Fuzzy;
            } else {
                if (type->is_Path() && type->as_Path().binding.is_Unbound()) {
                    return ::HIR::Compare::Fuzzy;
                }
                return ::HIR::Compare::Unequal;
            }
        }
        TU_ARMA(Infer, e) {
            switch (e.ty_class) {
                case ::HIR::InferClass::Integer:
                case ::HIR::InferClass::Float:
                    return ::HIR::Compare::Equal;
                default:
                    DEBUG("Fuzzy Copy impl for ivar?");
                    return ::HIR::Compare::Fuzzy;
            }
        }
        TU_ARMA(Generic, e) {
            // TODO: Store this result - or even pre-calculate it.
            return this->iterate_bounds_traits(
                       sp,
                       ty,
                       mLangCopy,
                       [&](HIR::Compare _cmp, const ::HIR::TypeData* beType, const ::HIR::GenericPath& beTrait, const CachedBound& info) -> bool {
                return true;
            }
                   )
                       ? ::HIR::Compare::Equal
                       : ::HIR::Compare::Unequal;
        }
        TU_ARMA(Primitive, e) {
            if (e == ::HIR::CoreType::Str) {
                return ::HIR::Compare::Unequal;
            }
            return ::HIR::Compare::Equal;
        }
        TU_ARMA(Borrow, e) {
            return e.type == ::HIR::BorrowType::Shared ? ::HIR::Compare::Equal : ::HIR::Compare::Unequal;
        }
        TU_ARMA(Pointer, e) {
            return ::HIR::Compare::Equal;
        }
        TU_ARMA(Tuple, e) {
            auto rv = ::HIR::Compare::Equal;
            for (const auto& sty : e) {
                rv &= type_is_copy(sp, sty);
            }
            return rv;
        }
        TU_ARMA(Slice, e) {
            return ::HIR::Compare::Unequal;
        }
        TU_ARMA(NamedFunction, e) {
            return ::HIR::Compare::Equal;
        }
        TU_ARMA(Function, e) {
            return ::HIR::Compare::Equal;
        }
        TU_ARMA(NodeType, e) {
            // NOTE: This isn't strictly true, we're leaving the actual checking up to the validate pass
            return ::HIR::Compare::Equal;
        }
        TU_ARMA(Array, e) {
            return type_is_copy(sp, e.inner);
        }
    }
    throw "";
        }

        ::HIR::Compare TraitResolution::type_is_clone(const Span& sp, const ::HIR::TypeData* ty) const {
            TRACE_FUNCTION_F(ty);
            const auto& type = this->ivars.get_type(ty);
    TU_MATCH_HDRA( (*type), {)
    default: {
            if (type->is_Path() && type->as_Path().is_closure()) {
                // If it was a closure, assume true (later code can check)
                return ::HIR::Compare::Equal;
            }
            bool is_fuzzy = false;
            bool has_eq = find_trait_impls(sp, mLangClone, ::HIR::PathParams{}, ty, [&](auto, auto c) -> bool {
                switch (c) {
                    case ::HIR::Compare::Equal:
                        return true;
                    case ::HIR::Compare::Fuzzy:
                        is_fuzzy = true;
                        return false;
                    case ::HIR::Compare::Unequal:
                        return false;
                }
                throw "";
            }, /*magic_trait_impls=*/false);
            if (has_eq) {
                return ::HIR::Compare::Equal;
            } else if (is_fuzzy) {
                return ::HIR::Compare::Fuzzy;
            } else {
                return ::HIR::Compare::Unequal;
            }
        }
        TU_ARMA(Infer, e) {
            switch (e.ty_class) {
                case ::HIR::InferClass::Integer:
                case ::HIR::InferClass::Float:
                    return ::HIR::Compare::Equal;
                default:
                    DEBUG("Fuzzy Clone impl for ivar?");
                    return ::HIR::Compare::Fuzzy;
            }
        }
        TU_ARMA(Generic, e) {
            // TODO: Store this result - or even pre-calculate it.
            return this->iterate_bounds_traits(
                       sp,
                       ty,
                       mLangClone,
                       [&](HIR::Compare _cmp, const ::HIR::TypeData* beType, const ::HIR::GenericPath& beTrait, const CachedBound& info) -> bool {
                return true;
            }
                   )
                       ? ::HIR::Compare::Equal
                       : ::HIR::Compare::Unequal;
        }
        TU_ARMA(Primitive, e) {
            if (e == ::HIR::CoreType::Str) {
                return ::HIR::Compare::Unequal;
            }
            return ::HIR::Compare::Equal;
        }
        TU_ARMA(Borrow, e) {
            return e.type == ::HIR::BorrowType::Shared ? ::HIR::Compare::Equal : ::HIR::Compare::Unequal;
        }
        TU_ARMA(Pointer, e) {
            return ::HIR::Compare::Equal;
        }
        TU_ARMA(Tuple, e) {
            auto rv = ::HIR::Compare::Equal;
            for (const auto& sty : e) {
                rv &= type_is_clone(sp, sty);
            }
            return rv;
        }
        TU_ARMA(Slice, e) {
            return ::HIR::Compare::Unequal;
        }
        TU_ARMA(NamedFunction, e) {
            return ::HIR::Compare::Equal;
        }
        TU_ARMA(Function, e) {
            return ::HIR::Compare::Equal;
        }
        TU_ARMA(NodeType, e) {
            // NOTE: This isn't strictly true, we're leaving the actual checking up to the validate pass
            // TODO: Determine captures earlier and check captures here
            return ::HIR::Compare::Equal;
        }
        TU_ARMA(Array, e) {
            return type_is_clone(sp, e.inner);
        }
    }
    throw "";
        }

        // Checks if a type can unsize to another
        // - Returns Compare::Equal if the unsize is possible and fully known
        // - Returns Compare::Fuzzy if the unsize is possible, but still unknown.
        // - Returns Compare::Unequal if the unsize is impossibe (for any reason)
        //
        // Closure is called `get_new_type` is true, and the unsize is possible
        //
        // usecases:
        // - Checking for an impl as part of impl selection (return True/False/Maybe with required match for Maybe)
        // - Checking for an impl as part of typeck (return True/False/Maybe with unsize possibility OR required equality)
        ::HIR::Compare TraitResolution::canUnsize(const Span& sp, const ::HIR::TypeData* dst_ty, const ::HIR::TypeData* src_ty, ::std::function<void(::HIR::TypeRef new_dst)>* new_type_callback, ::std::function<void(const ::HIR::TypeData* dst, const ::HIR::TypeData* src)>* infer_callback) const {
            TRACE_FUNCTION_F(dst_ty << " <- " << src_ty);

            // 1. Test for type equality
            {
                auto cmp = dst_ty->compareWithPlaceholders(sp, src_ty, ivars.callbackResolveInfer());
                if (cmp == ::HIR::Compare::Equal) {
                    return ::HIR::Compare::Unequal;
                }
            }

            // 2. If either side is an ivar, fuzzy.
            if (dst_ty->is_Infer() || src_ty->is_Infer()) {
                // Inform the caller that these two types could unsize to each other
                // - This allows the coercions code to move the coercion rule up
                if (infer_callback) {
                    (*infer_callback)(dst_ty, src_ty);
                }
                return ::HIR::Compare::Fuzzy;
            }

            {
                bool found_bound = this->iterate_bounds_traits(sp, src_ty, mLangUnsize, [&](HIR::Compare cmp, const ::HIR::TypeData* beType, const ::HIR::GenericPath& beTrait, const CachedBound& info) -> bool {
                    const auto& beDst = beTrait.mParams.types.at(0);

                    cmp &= dst_ty->compareWithPlaceholders(sp, beDst, ivars.callbackResolveInfer());
                    if (cmp == ::HIR::Compare::Unequal) {
                        return false;
                    }

                    if (cmp != ::HIR::Compare::Equal) {
                        TODO(sp, "Found bound " << dst_ty << "=" << beDst << " <- " << src_ty << "=" << beType);
                    }
                    return true;
                });
                if (found_bound) {
                    return ::HIR::Compare::Equal;
                }
            }

            // Associated types, check the bounds in the trait.
            if (src_ty->is_Path() && src_ty->as_Path().path.mData.is_UfcsKnown()) {
                ::HIR::Compare rv = ::HIR::Compare::Equal;
                const auto& pe = src_ty->as_Path().path.mData.as_UfcsKnown();
                auto monomorph_cb = MonomorphStatePtr(crate.types, pe.type, &pe.trait.mParams, nullptr);
                auto found_bound = this->iterate_aty_bounds(sp, pe, [&](const ::HIR::TraitPath& bound) {
                    if (bound.mPath.mPath != mLangUnsize) {
                        return false;
                    }
                    const auto& beDstTpl = bound.mPath.mParams.types.at(0);
                    ::HIR::TypeRef tmp_ty;
                    const auto& beDst = monomorph_cb.maybe_monomorph_type(sp, tmp_ty, beDstTpl);

                    auto cmp = dst_ty->compareWithPlaceholders(sp, beDst, ivars.callbackResolveInfer());
                    if (cmp == ::HIR::Compare::Unequal) {
                        return false;
                    }

                    if (cmp != ::HIR::Compare::Equal) {
                        DEBUG("[can_unsize] > Found bound (fuzzy) " << dst_ty << "=" << beDst << " <- " << src_ty);
                        rv = ::HIR::Compare::Fuzzy;
                    }
                    return true;
                });
                if (found_bound) {
                    return rv;
                }
            }

            // Struct<..., T, ...>: Unsize<Struct<..., U, ...>>
            if (dst_ty->is_Path() && src_ty->is_Path()) {
                bool dst_is_unsizable = dst_ty->as_Path().binding.is_Struct() && dst_ty->as_Path().binding.as_Struct()->structMarkings.canUnsize;
                bool src_is_unsizable = src_ty->as_Path().binding.is_Struct() && src_ty->as_Path().binding.as_Struct()->structMarkings.canUnsize;
                if (dst_is_unsizable || src_is_unsizable) {
                    DEBUG("Struct unsize? " << dst_ty << " <- " << src_ty);
                    const auto& str = *dst_ty->as_Path().binding.as_Struct();
                    const auto& dst_gp = dst_ty->as_Path().path.mData.as_Generic();
                    const auto& src_gp = src_ty->as_Path().path.mData.as_Generic();

                    if (dst_gp == src_gp) {
                        DEBUG("Can't Unsize, destination and source are identical");
                        return ::HIR::Compare::Unequal;
                    } else if (dst_gp.mPath == src_gp.mPath) {
                        DEBUG("Checking for Unsize " << dst_gp << " <- " << src_gp);
                        // Structures are equal, add the requirement that the ?Sized parameter also impl Unsize
                        const auto& dst_inner = ivars.get_type(dst_gp.mParams.types.at(str.structMarkings.unsized_param));
                        const auto& src_inner = ivars.get_type(src_gp.mParams.types.at(str.structMarkings.unsized_param));

                        auto cb = [&](auto d) {
                            assert(new_type_callback);

                            // Re-create structure with s/d
                            auto dst_gp_new = dst_gp.clone();
                            dst_gp_new.mParams.types.at(str.structMarkings.unsized_param) = mv$(d);
                            (*new_type_callback)(crate.types.path(::HIR::Path(mv$(dst_gp_new)), ::HIR::TypePathBinding::make_Struct(&str)));
                        };
                        if (new_type_callback) {
                            ::std::function<void(::HIR::TypeRef)> cbP = cb;
                            return this->canUnsize(sp, dst_inner, src_inner, &cbP, infer_callback);
                        } else {
                            return this->canUnsize(sp, dst_inner, src_inner, nullptr, infer_callback);
                        }
                    } else {
                        DEBUG("Can't Unsize, destination and source are different structs");
                        return ::HIR::Compare::Unequal;
                    }
                }
            }

            // (Trait) <- Foo
            if (const auto* de = dst_ty->opt_TraitObject()) {
                // TODO: Check if src_ty is !Sized
                // - Only allowed if the source is a trait object with the same data trait and lesser bounds

                DEBUG("TraitObject unsize? " << dst_ty << " <- " << src_ty);

                // (Trait) <- (Trait+Foo)
                if (const auto* se = src_ty->opt_TraitObject()) {
                    auto rv = ::HIR::Compare::Equal;

                    // Project the source principal to the requested
                    // supertrait.  A trait may contain the same supertrait
                    // with different substitutions, so compare the fully
                    // monomorphised parameters instead of only its path.
                    const ::HIR::TraitPath* projected = nullptr;
                    ::HIR::TraitPath projected_storage;
                    if (de->mTrait.mPath.mPath == se->mTrait.mPath.mPath) {
                        rv &= comparePp(
                            sp,
                            se->mTrait.mPath.mParams,
                            de->mTrait.mPath.mParams
                        );
                        projected = &se->mTrait;
                    } else if (se->mTrait.mPath.mPath != ::HIR::SimplePath()) {
                        find_named_trait_in_trait(
                            sp,
                            de->mTrait.mPath.mPath,
                            de->mTrait.mPath.mParams,
                            *se->mTrait.traitPtr,
                            se->mTrait.mPath.mPath,
                            se->mTrait.mPath.mParams,
                            src_ty,
                            [&](const ::HIR::TraitPath& parent) {
                                const auto cmp = comparePp(
                                    sp,
                                    parent.mPath.mParams,
                                    de->mTrait.mPath.mParams
                                );
                                if (cmp == ::HIR::Compare::Unequal) {
                                    return false;
                                }
                                rv &= cmp;
                                projected_storage = parent.clone();
                                projected = &projected_storage;
                                return cmp == ::HIR::Compare::Equal;
                            }
                        );
                    }
                    if (!projected || rv == ::HIR::Compare::Unequal) {
                        return ::HIR::Compare::Unequal;
                    }

                    // Every associated-type equality required by the
                    // destination object must also hold on the projected
                    // source supertrait.
                    for (const auto& required : de->mTrait.typeBounds) {
                        const auto source = projected->typeBounds.find(required.first);
                        if (source == projected->typeBounds.end()) {
                            return ::HIR::Compare::Unequal;
                        }
                        rv &= source->second.type->compareWithPlaceholders(
                            sp,
                            required.second.type,
                            ivars.callbackResolveInfer()
                        );
                        if (rv == ::HIR::Compare::Unequal) {
                            return rv;
                        }
                    }

                    // 2. Destination markers must be a strict subset
                    for (const auto& mt : de->markers) {
                        // TODO: Fuzzy match
                        bool found = false;
                        for (const auto& omt : se->markers) {
                            if (omt == mt) {
                                found = true;
                                break;
                            }
                        }
                        if (!found) {
                            // Return early.
                            return ::HIR::Compare::Unequal;
                        }
                    }

                    if (rv == ::HIR::Compare::Fuzzy && new_type_callback) {
                        // TODO: Inner type
                    }
                    return rv;
                }

                bool good;
                ::HIR::Compare total_cmp = ::HIR::Compare::Equal;

                ::HIR::TypeData::Data_TraitObject tmp_e;
                tmp_e.mTrait.mPath = de->mTrait.mPath.mPath;

                // Check data trait first.
                if (de->mTrait.mPath.mPath == ::HIR::SimplePath()) {
                    ASSERT_BUG(sp, de->markers.size() > 0, "TraitObject with no traits - " << dst_ty);
                    good = true;
                } else {
                    good = find_trait_impls(sp, de->mTrait.mPath.mPath, de->mTrait.mPath.mParams, src_ty, [&](const auto impl, auto cmp) {
                        if (cmp == ::HIR::Compare::Unequal) {
                            return false;
                        }

                        auto candidateCmp = cmp;
                        ::HIR::TypeData::Data_TraitObject candidateE;
                        candidateE.mTrait.mPath = de->mTrait.mPath.mPath;
                        candidateE.mTrait.mPath.mParams = impl.get_trait_params(crate.types);

                        // Associated types declared by a supertrait carry the
                        // declaring trait path.  Rebuild that path with the
                        // selected principal-trait response instead of mixing
                        // response parameters with the original goal.
                        auto remap_source_trait = [&](const ::HIR::GenericPath& source_trait) {
                            if (source_trait.mPath == de->mTrait.mPath.mPath) {
                                return ::HIR::GenericPath(
                                    source_trait.mPath,
                                    candidateE.mTrait.mPath.mParams.clone()
                                );
                            }

                            ::HIR::GenericPath result = source_trait.clone();
                            if (!de->mTrait.traitPtr) {
                                candidateCmp = ::HIR::Compare::Fuzzy;
                                return result;
                            }

                            auto goal_monomorph = MonomorphStatePtr(
                                crate.types, src_ty, &de->mTrait.mPath.mParams, nullptr
                            );
                            auto response_monomorph = MonomorphStatePtr(
                                crate.types,
                                src_ty,
                                &candidateE.mTrait.mPath.mParams,
                                nullptr
                            );
                            bool found = false;
                            bool found_equal = false;
                            for (const auto& parent : de->mTrait.traitPtr->allParentTraits) {
                                if (parent.mPath.mPath != source_trait.mPath) {
                                    continue;
                                }
                                auto goal_parent = goal_monomorph.monomorph_genericpath(
                                    sp, parent.mPath, false
                                );
                                const auto parent_cmp = comparePp(
                                    sp, goal_parent.mParams, source_trait.mParams
                                );
                                if (parent_cmp == ::HIR::Compare::Unequal
                                    || (found_equal && parent_cmp != ::HIR::Compare::Equal)) {
                                    continue;
                                }

                                auto response_parent = response_monomorph.monomorph_genericpath(
                                    sp, parent.mPath, false
                                );
                                if (!found || parent_cmp == ::HIR::Compare::Equal) {
                                    result = ::std::move(response_parent);
                                    found = true;
                                    found_equal = parent_cmp == ::HIR::Compare::Equal;
                                } else if (result != response_parent) {
                                    // Multiple fuzzy supertrait projections
                                    // are a legitimate ambiguous response.
                                    candidateCmp = ::HIR::Compare::Fuzzy;
                                }
                            }
                            if (!found) {
                                candidateCmp = ::HIR::Compare::Fuzzy;
                            } else if (!found_equal) {
                                candidateCmp = ::HIR::Compare::Fuzzy;
                            }
                            return result;
                        };

                        for (const auto& aty : de->mTrait.typeBounds) {
                            auto atyv = impl.get_type(crate.types, aty.first.c_str(), aty.second.atyParams);
                            if (atyv == ::HIR::TypeRef()) {
                                // Get the trait from which this associated type comes.
                                // Insert a UfcsKnown path for that
                                auto p = ::HIR::Path(
                                    src_ty,
                                    aty.second.source_trait.clone(),
                                    aty.first,
                                    aty.second.atyParams.clone()
                                );
                                // Run EAT
                                atyv = this->expand_associated_types(sp, crate.types.path(mv$(p), {}));
                            }

                            auto desired = this->expand_associated_types(
                                sp, aty.second.type
                            );
                            const auto atyCmp = compareTy(sp, atyv, desired);
                            if (atyCmp == ::HIR::Compare::Unequal) {
                                return false;
                            }
                            candidateCmp &= atyCmp;
                            candidateE.mTrait.typeBounds[aty.first] = ::HIR::TraitPath::AtyEqual{
                                remap_source_trait(aty.second.source_trait),
                                aty.second.atyParams.clone(),
                                mv$(atyv)
                            };
                        }

                        total_cmp &= candidateCmp;
                        tmp_e = ::std::move(candidateE);
                        return true;
                    });
                }

                // Then markers
                auto cb = [&](const auto impl, auto cmp) {
                    if (cmp == ::HIR::Compare::Unequal) {
                        return false;
                    }
                    total_cmp &= cmp;
                    tmp_e.markers.back().mParams = impl.get_trait_params(crate.types);
                    return true;
                };
                for (const auto& marker : de->markers) {
                    if (!good) {
                        break;
                    }
                    tmp_e.markers.push_back(marker.mPath);
                    good &= find_trait_impls(sp, marker.mPath, marker.mParams, src_ty, cb);
                }

                if (good && total_cmp == ::HIR::Compare::Fuzzy && new_type_callback) {
                    (*new_type_callback)(crate.types.intern(::HIR::TypeData::make_TraitObject(mv$(tmp_e))));
                }
                return total_cmp;
            }

            // [T] <- [T; n]
            if (const auto* de = dst_ty->opt_Slice()) {
                if (const auto* se = src_ty->opt_Array()) {
                    DEBUG("Array unsize? " << de->inner << " <- " << se->inner);
                    auto cmp = de->inner->compareWithPlaceholders(sp, se->inner, ivars.callbackResolveInfer());
                    // TODO: Indicate to caller that for this to be true, these two must be the same.
                    // - I.E. if true, equate these types
                    if (cmp == ::HIR::Compare::Fuzzy && new_type_callback) {
                        (*new_type_callback)(crate.types.slice(se->inner));
                    }
                    return cmp;
                }
            }

            DEBUG("Can't unsize, no rules matched");
            return ::HIR::Compare::Unequal;
        }

        const ::HIR::TypeData* TraitResolution::type_is_owned_box(const Span& sp, const ::HIR::TypeData* ty) const {
            if (const auto* e = ty->opt_Path()) {
                if (const auto* pe = e->path.mData.opt_Generic()) {
                    if (pe->mPath == mLangBox) {
                        return this->ivars.get_type(pe->mParams.types.at(0));
                    }
                }
            }
            return nullptr;
        }

        // -------------------------------------------------------------------------------------------------------------------
        //
        // -------------------------------------------------------------------------------------------------------------------
        TraitResolution::AutoderefResult TraitResolution::autoderefStep(
            const Span& sp,
            const ::HIR::TypeData* ty_in,
            ::HIR::TypeRef& target,
            ::std::optional<::HIR::TypeRef>* impl_type
        ) const {
            if (impl_type) {
                impl_type->reset();
            }

            const auto& ty = this->ivars.get_type(ty_in);
            if (ty->is_Infer()) {
                return AutoderefResult::NoMatch;
            } else if (const auto* e = ty->opt_Borrow()) {
                DEBUG("Deref " << ty << " into " << e->inner);
                target = this->ivars.get_type(e->inner);
                return AutoderefResult::Match;
            }
            // Array-to-slice is the final unsize step in an autoderef search.
            // create_autoderef materialises it as borrow -> pointer unsize -> deref.
            else if (const auto* e = ty->opt_Array()) {
                DEBUG("Deref " << ty << " into [" << e->inner << "]");
                target = crate.types.slice(e->inner);
                return AutoderefResult::Match;
            }
            // Shortcut, don't look up a Deref impl for primitives or slices
            else if (ty->is_Slice() || ty->is_Primitive() || ty->is_Tuple() || ty->is_Array()) {
                return AutoderefResult::NoMatch;
            } else {
                ::std::optional<::HIR::TypeRef> candidateTarget;
                ::std::optional<::HIR::TypeRef> candidateImplType;
                bool exact = false;
                bool ambiguous = false;

                this->find_trait_impls(sp, mLangDeref, ::HIR::PathParams{}, ty, [&](auto impl, auto match) {
                    auto found_target = impl.get_type(crate.types, "Target", {});
                    if (found_target == ::HIR::TypeRef()) {
                        found_target = crate.types.path(::HIR::Path(ty, mLangDeref, RcString::new_interned("Target")), ::HIR::TypePathBinding::make_Opaque({}));
                    } else {
                        this->expand_associated_types_inplace(sp, found_target, {});
                    }
                    auto found_impl_type = impl.get_impl_type(crate.types);

                    if (match == ::HIR::Compare::Equal) {
                        candidateTarget = found_target;
                        candidateImplType = found_impl_type;
                        exact = true;
                        return true;
                    }

                    if (candidateTarget) {
                        ambiguous = true;
                    } else {
                        candidateTarget = found_target;
                        candidateImplType = found_impl_type;
                    }
                    return false;
                });

                if (!exact && ambiguous) {
                    DEBUG("Ambiguous Deref impl for " << ty);
                    return AutoderefResult::Ambiguous;
                }
                if (!candidateTarget) {
                    return AutoderefResult::NoMatch;
                }

                target = *candidateTarget;
                if (impl_type) {
                    *impl_type = *candidateImplType;
                }
                DEBUG("Deref " << ty << " into " << target);
                return AutoderefResult::Match;
            }
        }

        const ::HIR::TypeData* TraitResolution::autoderef(const Span& sp, const ::HIR::TypeData* ty, ::HIR::TypeRef& tmp_type) const {
            return autoderefStep(sp, ty, tmp_type) == AutoderefResult::Match
                ? tmp_type
                : nullptr;
        }

        unsigned int TraitResolution::autoderefFindMethod(
            const Span& sp,
            const HIR::t_trait_list& traits,
            const ::std::vector<unsigned>& ivars,
            unsigned int type_ivar_count,
            const ::HIR::TypeData* top_ty,
            const RcString& method_name,
            /* Out -> */ ::std::vector<::std::pair<AutoderefBorrow, ::HIR::Path>>& possibilities
        ) const {
            try {
                TRACE_FUNCTION_F("{" << top_ty << "}." << method_name);
                unsigned int deref_count = 0;
                ::HIR::TypeRef tmp_type; // Temporary type used for handling Deref
                const auto& top_ty_r = this->ivars.get_type(top_ty);
                const auto* current_ty = top_ty_r;

                // Correct algorithm:
                // - Find any available method with a receiver type of `T`
                // - If no, try &T
                // - If no, try &mut T
                // - If no, try &move T
                // - If no, dereference T and try again
                auto cur_access = MethodAccess::Move; // Assume that the input value is movable
                auto collapseToMostSpecificSubtrait = [&]() {
                    if (!crate.feature_enabled("supertrait_item_shadowing")
                        || possibilities.size() < 2) {
                        return;
                    }

                    ::std::vector<::HIR::SimplePath> candidateTraits;
                    candidateTraits.reserve(possibilities.size());
                    for (const auto& possibility : possibilities) {
                        const auto* path = possibility.second.mData.opt_UfcsKnown();
                        if (!path) {
                            // RFC 3624 only collapses extension-trait picks.
                            return;
                        }
                        candidateTraits.push_back(path->trait.mPath);
                    }

                    const auto selected = crate.find_most_specific_trait(sp, candidateTraits);
                    if (selected) {
                        auto selected_possibility = mv$(possibilities[*selected]);
                        possibilities.clear();
                        possibilities.push_back(mv$(selected_possibility));
                    }
                };
                do {
                    const auto* ty = this->ivars.get_type(current_ty);
                    auto should_pause = [](const auto& ty) -> bool {
                        if (type_is_unbounded_infer(ty)) {
                            DEBUG("- Ivar" << ty << ", pausing");
                            return true;
                        }
                        if (ty->is_Path() && ty->as_Path().binding.is_Unbound()) {
                            DEBUG("- Unbound type path " << ty << ", pausing");
                            return true;
                        }
                        return false;
                    };
                    if (should_pause(ty)) {
                        return ~0u;
                    }
                    if (ty->is_Borrow() && should_pause(this->ivars.get_type(ty->as_Borrow().inner))) {
                        return ~0u;
                    }
                    // TODO: Pause on Box<_>?
                    DEBUG(deref_count << ": " << ty);

                    // Non-referenced
                    if (this->find_method(sp, traits, ivars, type_ivar_count, ty, method_name, cur_access, AutoderefBorrow::None, possibilities)) {
                        DEBUG("FOUND *{" << deref_count << "}, fcn_path = " << possibilities.back().second);
                    }

                    // Auto-ref
                    auto borrowTy = crate.types.borrow(::HIR::BorrowType::Shared, ty);
                    if (this->find_method(sp, traits, ivars, type_ivar_count, borrowTy, method_name, MethodAccess::Move, AutoderefBorrow::Shared, possibilities)) {
                        DEBUG("FOUND & *{" << deref_count << "}, fcn_path = " << possibilities.back().second);
                    }
                    borrowTy = crate.types.borrow(::HIR::BorrowType::Unique, ty);
                    if (cur_access >= MethodAccess::Unique && this->find_method(sp, traits, ivars, type_ivar_count, borrowTy, method_name, MethodAccess::Move, AutoderefBorrow::Unique, possibilities)) {
                        DEBUG("FOUND &mut *{" << deref_count << "}, fcn_path = " << possibilities.back().second);
                    }
                    borrowTy = crate.types.borrow(::HIR::BorrowType::Owned, ty);
                    if (cur_access >= MethodAccess::Move && this->find_method(sp, traits, ivars, type_ivar_count, borrowTy, method_name, MethodAccess::Move, AutoderefBorrow::Owned, possibilities)) {
                        DEBUG("FOUND &move *{" << deref_count << "}, fcn_path = " << possibilities.back().second);
                    }
                    if (!possibilities.empty()) {
                        collapseToMostSpecificSubtrait();
                        DEBUG("FOUND " << possibilities.size() << " options: " << possibilities);
                        return deref_count;
                    }

                    // Auto-dereference
                    deref_count += 1;
                    if (const auto* typ = this->type_is_owned_box(sp, ty)) {
                        // `cur_access` can stay as-is (Box can be moved out of)
                        current_ty = typ;
                    } else {
                        // TODO: Update `cur_access` based on the avaliable Deref impls
                        switch (this->autoderefStep(sp, ty, tmp_type)) {
                            case AutoderefResult::NoMatch:
                                current_ty = nullptr;
                                break;
                            case AutoderefResult::Match:
                                current_ty = tmp_type;
                                break;
                            case AutoderefResult::Ambiguous:
                                return ~0u;
                        }
                    }
                } while (current_ty);

                // No method found, return an empty list and return 0
                assert(possibilities.empty());
                return 0;
            } catch (const TraitResolution::RecursionDetected&) {
                DEBUG("Recursion detected, deferring");
                return ~0u;
            }
        }

        ::std::ostream& operator<<(::std::ostream& os, const TraitResolution::AutoderefBorrow& x) {
            switch (x) {
                case TraitResolution::AutoderefBorrow::None:
                    os << "None";
                    break;
                case TraitResolution::AutoderefBorrow::Shared:
                    os << "Shared";
                    break;
                case TraitResolution::AutoderefBorrow::Unique:
                    os << "Unique";
                    break;
                case TraitResolution::AutoderefBorrow::Owned:
                    os << "Owned";
                    break;
            }
            return os;
        }

        ::std::ostream& operator<<(::std::ostream& os, const TraitResolution::AllowedReceivers& x) {
            switch (x) {
                case TraitResolution::AllowedReceivers::All:
                    os << "All";
                    break;
                case TraitResolution::AllowedReceivers::AnyBorrow:
                    os << "AnyBorrow";
                    break;
                case TraitResolution::AllowedReceivers::SharedBorrow:
                    os << "SharedBorrow";
                    break;
                case TraitResolution::AllowedReceivers::Value:
                    os << "Value";
                    break;
                case TraitResolution::AllowedReceivers::Box:
                    os << "Box";
                    break;
            }
            return os;
        }

        ::std::ostream& operator<<(::std::ostream& os, const TraitResolution::MethodAccess& x) {
            switch (x) {
                case TraitResolution::MethodAccess::Shared:
                    os << "Shared";
                    break;
                case TraitResolution::MethodAccess::Unique:
                    os << "Unique";
                    break;
                case TraitResolution::MethodAccess::Move:
                    os << "Move";
                    break;
            }
            return os;
        }

        // Checks that a given real receiver type matches a desired receiver type (with the correct access)
        // Returns the matched `Self` type, or nothing if there's a mismatch.
        ::std::optional<::HIR::TypeRef> TraitResolution::checkMethodReceiver(const Span& sp, const ::HIR::Function& fcn, const ::HIR::TypeData* ty, TraitResolution::MethodAccess access) const {
            switch (fcn.receiver) {
                case ::HIR::Function::Receiver::Free:
                    // Free functions are never usable
                    return ::std::nullopt;
                case ::HIR::Function::Receiver::Value:
                    if (access >= TraitResolution::MethodAccess::Move) {
                        return this->ivars.get_type(ty);
                    }
                    break;
                case ::HIR::Function::Receiver::BorrowOwned:
                    if (!ty->is_Borrow())
                        ;
                    else if (ty->as_Borrow().type != ::HIR::BorrowType::Owned)
                        ;
                    else if (access < TraitResolution::MethodAccess::Move)
                        ;
                    else {
                        return this->ivars.get_type(ty->as_Borrow().inner);
                    }
                    break;
                case ::HIR::Function::Receiver::BorrowUnique:
                    if (!ty->is_Borrow())
                        ;
                    else if (ty->as_Borrow().type != ::HIR::BorrowType::Unique)
                        ;
                    else if (access < TraitResolution::MethodAccess::Unique)
                        ;
                    else {
                        return this->ivars.get_type(ty->as_Borrow().inner);
                    }
                    break;
                case ::HIR::Function::Receiver::BorrowShared:
                    if (!ty->is_Borrow())
                        ;
                    else if (ty->as_Borrow().type != ::HIR::BorrowType::Shared)
                        ;
                    else if (access < TraitResolution::MethodAccess::Shared)
                        ;
                    else {
                        return this->ivars.get_type(ty->as_Borrow().inner);
                    }
                    break;
                case ::HIR::Function::Receiver::Custom: {
                    const auto& receiver_type = fcn.mArgs.front().second;
                    ASSERT_BUG(
                        sp,
                        visit_ty_with(
                            receiver_type,
                            [](const HIR::TypeData* v) {
                        return v->is_Generic() && v->as_Generic().is_self();
                    }
                        ),
                        receiver_type
                    );
                    // TODO: Handle custom-receiver functions
                    // - match_test_generics, if it succeeds return the matched Self
                    {
                        struct GetSelf: public ::HIR::MatchGenerics {
                            ::std::optional<::HIR::TypeRef> detected_self_ty;

                            ::HIR::Compare match_ty(const ::HIR::GenericRef& g, const ::HIR::TypeData* ty, ::HIR::t_cb_resolve_type _resolve_cb) override {
                                if (g.is_self()) {
                                    detected_self_ty = ty;
                                }
                                return ::HIR::Compare::Equal;
                            }

                            ::HIR::Compare match_val(const ::HIR::GenericRef& g, const ::HIR::ConstGeneric& sz) override {
                                TODO(Span(), "GetSelf::match_val " << g << " with " << sz);
                            }
                        } getself;

                        if (receiver_type->match_test_generics(sp, ty, this->ivars.callbackResolveInfer(), getself)) {
                            ASSERT_BUG(sp, getself.detected_self_ty, "Unable to determine receiver type when matching " << receiver_type << " and " << ty);
                            return this->ivars.get_type(*getself.detected_self_ty);
                        }
                    }
                    return ::std::nullopt;
                }
                case ::HIR::Function::Receiver::Box:
                    if (const auto* ity = this->type_is_owned_box(sp, ty)) {
                        if (access < TraitResolution::MethodAccess::Move) {
                        } else {
                            return this->ivars.get_type(ity);
                        }
                    }
                    break;
            }
            return ::std::nullopt;
        }

        bool TraitResolution::find_method(const Span& sp, const HIR::t_trait_list& traits, const ::std::vector<unsigned>& ivars, unsigned int type_ivar_count, const ::HIR::TypeData* ty, const RcString& method_name, MethodAccess access, AutoderefBorrow borrowType, /* Out -> */ ::std::vector<::std::pair<AutoderefBorrow, ::HIR::Path>>& possibilities) const {
            bool rv = false;
            TRACE_FUNCTION_FR("ty=" << ty << ", name=" << method_name << ", access=" << access, rv << " " << possibilities);
            auto cbInfer = this->ivars.callbackResolveInfer();

            auto get_ivared_params = [&](const ::HIR::GenericParams& tpl) -> ::HIR::PathParams {
                unsigned int n_params = tpl.types.size();
                ASSERT_BUG(sp, type_ivar_count <= ivars.size(), "Invalid method ivar split: " << type_ivar_count << " type ivars in a pool of " << ivars.size());
                ASSERT_BUG(sp, n_params <= type_ivar_count, "Not enough type ivars allocated for method: " << n_params << " needed but " << type_ivar_count << " allocated by caller\ntpl = " << tpl.fmt_args());
                ::HIR::PathParams trait_params;
                trait_params.types.reserve(n_params);
                for (unsigned int i = 0; i < n_params; i++) {
                    trait_params.types.push_back(crate.types.infer(ivars[i], ::HIR::InferClass::None));
                    ASSERT_BUG(sp, this->ivars.get_type(trait_params.types.back())->as_Infer().index == ivars[i], "A method selection ivar was bound");
                }
                const unsigned int n_values = tpl.values.size();
                ASSERT_BUG(sp, n_values <= ivars.size() - type_ivar_count, "Not enough value ivars allocated for method: " << n_values << " needed but " << ivars.size() - type_ivar_count << " allocated by caller\ntpl = " << tpl.fmt_args());
                trait_params.values.reserve(n_values);
                for (unsigned int i = 0; i < n_values; i++) {
                    trait_params.values.push_back(::HIR::ConstGeneric::make_Infer({ivars[type_ivar_count + i]}));
                }
                return trait_params;
            };

            // 1. Search for inherent methods
            // - Inherent methods are searched first.
            // TODO: Have a cache of name+receiver_type to a list of types and impls
            // e.g. `len` `&Self` = `[T]`
            DEBUG("> Inherent methods");
            crate.inherentMethodCache.find(sp, method_name, ty, this->ivars.callbackResolveInfer(), [&](const HIR::TypeData* self_ty, const HIR::TypeImpl& impl) {
                if (!impl.methods.at(method_name).publicity.is_visible(this->visPath)) {
                    // Ignore method: Not visibile
                    return;
                }
                ::HIR::PathParams impl_params;
                auto cmp = ftic_check_params(sp, ::HIR::SimplePath(), nullptr, self_ty, impl.mParams, {}, impl.mType, impl_params);
                if (cmp != HIR::Compare::Unequal) {
                    DEBUG("Found `impl" << impl.mParams.fmt_args() << " " << impl.mType << "` fn " << method_name /* << " - " << top_ty*/);
                    possibilities.push_back(::std::make_pair(borrowType, ::HIR::Path(self_ty, method_name, {})));
                    DEBUG("++ " << possibilities.back());
                    rv = true;
                }
            });

            // TODO: Handle custom recievers by finding the bottom of a deref chain (or take the top-level reciever as an argument here?)

            // 3. Search generic bounds for a match
            // - If there is a bound on the receiver, then that bound is usable no-matter what
            DEBUG("> Bounds");
            bool found_bound = false;
            for (const auto& tb : traitBounds) {
                const auto& e_type = tb.first.first;
                const auto& e_trait_gp = tb.first.second;
                const auto& e_trait_info = tb.second;

                assert(e_trait_info.trait_ptr);
                // 1. Find the named method in the trait.
                ::HIR::GenericPath final_trait_path;
                const ::HIR::Function* fcn_ptr;
                if (!(fcn_ptr = this->trait_contains_method(sp, e_trait_gp, *e_trait_info.trait_ptr, e_type, method_name, final_trait_path))) {
                    DEBUG("- Method '" << method_name << "' missing");
                    continue;
                }
                DEBUG("- Found trait " << final_trait_path << " (bound)");

                // 2. Compare the receiver of the above to this type and the bound.
                if (auto self_ty = checkMethodReceiver(sp, *fcn_ptr, ty, access)) {
                    // HRLs - could be some in the path from `trait_contains_method`
                    // - Lazy option, just erase whatever we find
                    struct MonomorphEraseHrls: public Monomorphiser {
                        using Monomorphiser::Monomorphiser;

                        ::HIR::TypeRef get_type(const Span& sp, const ::HIR::GenericRef& ty) const override {
                            if (ty.group() == 3) {
                                return types.infer();
                            }
                            return types.generic(ty.name, ty.binding);
                        }

                        ::HIR::ConstGeneric get_value(const Span& sp, const ::HIR::GenericRef& val) const override {
                            if (val.group() == 3) {
                                return ::HIR::ConstGeneric();
                            }
                            return HIR::ConstGeneric(val);
                        }

                        ::HIR::LifetimeRef get_lifetime(const Span& sp, const ::HIR::GenericRef& lft_ref) const override {
                            if (lft_ref.group() == 3) {
                                return ::HIR::LifetimeRef();
                            }
                            return ::HIR::LifetimeRef(lft_ref.binding);
                        }
                    };

                    final_trait_path = MonomorphEraseHrls(crate.types).monomorph_genericpath(sp, final_trait_path, true);

                    // If the type is an unbounded ivar, don't check.
                    if (TU_TEST1(**self_ty, Infer, .is_lit() == false)) {
                        return false;
                    }
                    // TODO: Do a fuzzy match here?
                    auto cmp = (*self_ty)->compareWithPlaceholders(sp, e_type, cbInfer);
                    if (cmp == ::HIR::Compare::Equal) {
                        // TODO: Re-monomorphise final trait using `ty`?
                        // - Could collide with legitimate uses of `Self`

                        // Found the method, return the UFCS path for it
                        possibilities.push_back(::std::make_pair(borrowType, ::HIR::Path(::HIR::Path::Data::make_UfcsKnown({*self_ty, mv$(final_trait_path), method_name, {}}))));
                        DEBUG("++ " << possibilities.back());
                        rv = true;
                        found_bound = true;
                    } else if (cmp == ::HIR::Compare::Fuzzy) {
                        DEBUG("Fuzzy match checking bounded method - " << *self_ty << " != " << e_type);

                        // Found the method, return the UFCS path for it
                        possibilities.push_back(::std::make_pair(borrowType, ::HIR::Path(::HIR::Path::Data::make_UfcsKnown({*self_ty, mv$(final_trait_path), method_name, {}}))));
                        DEBUG("++ " << possibilities.back());
                        rv = true;
                        found_bound = true;
                    } else {
                        DEBUG("> Type mismatch - " << *self_ty << " != " << e_type);
                    }
                } else {
                    DEBUG("> Receiver mismatch");
                }
            }
            if (found_bound) {
                return rv;
            }

            // 2. Search the current trait (if in an impl block)
            if (currentTraitPath) {
                ::HIR::GenericPath final_trait_path;
                const ::HIR::Function* fcn_ptr;
                if ((fcn_ptr = this->trait_contains_method(sp, *currentTraitPath, *currentTraitPtr, ty, method_name, final_trait_path))) {
                    DEBUG("- Found trait " << final_trait_path << " (current)");
                    if (auto self_ty = checkMethodReceiver(sp, *fcn_ptr, ty, access)) {
                        // If the type is an unbounded ivar, don't check.
                        if (TU_TEST1(**self_ty, Infer, .is_lit() == false)) {
                            return false;
                        }

                        // Use the set of ivars we were given to populate the trait parameters
                        const auto& trait = crate.get_trait_by_path(sp, final_trait_path.mPath);
                        auto trait_params = get_ivared_params(trait.mParams);
                        //auto trait_params = std::move(final_trait_path.m_params);

                        try {
                            bool crate_impl_found = false;
                            // Method probing only establishes that some implementation of the
                            // trait can apply to the receiver.  The trait arguments are inference
                            // variables shared with the eventual call signature; constraining
                            // them to the first matching impl here makes impl iteration order
                            // decide calls whose arguments would otherwise disambiguate them.
                            find_trait_impls_crate(sp, final_trait_path.mPath, nullptr, *self_ty, [&](auto impl, auto cmp) {
                                DEBUG("[find_method] " << impl << ", cmp = " << cmp);
                                //magic_found = true;
                                crate_impl_found = true;
                                return true;
                            });
                            if (crate_impl_found) {
                                DEBUG("Found trait impl " << currentTraitPath->mPath << trait_params << " for " << *self_ty << " (" << this->ivars.fmt_type(*self_ty) << ")");
                                possibilities.push_back(::std::make_pair(borrowType, ::HIR::Path(*self_ty, ::HIR::GenericPath(final_trait_path.mPath, mv$(trait_params)), method_name, {})));
                                DEBUG("++ " << possibilities.back());
                                return true;
                            } else {
                            }
                        } catch (const TraitResolution::RecursionDetected&) {
                            DEBUG("Recursion detected, deferring");
                            return false;
                        }
                    }
                }
            }

            auto get_inner_type = [this, sp](const ::HIR::TypeData* ty, ::std::function<bool(const ::HIR::TypeData*)> cb) -> const ::HIR::TypeData* {
                if (cb(ty)) {
                    return ty;
                } else if (ty->is_Borrow()) {
                    const auto* ity = this->ivars.get_type(ty->as_Borrow().inner);
                    if (cb(ity)) {
                        return ity;
                    } else {
                        return nullptr;
                    }
                } else {
                    auto tp = this->type_is_owned_box(sp, ty);
                    if (tp && cb(tp)) {
                        return tp;
                    } else {
                        return nullptr;
                    }
                }
            };

            DEBUG("> Special cases");
            // 4. If the type is a trait object, search for methods on that trait object
            // - NOTE: This isnt mutually exclusive with the below set (an inherent impl of `(Trait)` is valid)
            if (const auto* ityp = get_inner_type(ty, [](const auto& t) {
                return t->is_TraitObject();
            })) {
                const auto& e = ityp->as_TraitObject();
                const auto& trait = this->crate.get_trait_by_path(sp, e.mTrait.mPath.mPath);

                bool found_trait_object = false;
                auto addTraitObjectMethod = [&](const ::HIR::Function& fcn, ::HIR::GenericPath final_trait_path) {
                    DEBUG("- Found trait " << final_trait_path << " (trait object)");
                    // - If the receiver is valid, then it's correct (no need to check the type again)
                    if (auto self_ty_p = checkMethodReceiver(sp, fcn, ty, access)) {
                        if (e.mTrait.hrtbs) {
                            auto pps = e.mTrait.hrtbs->make_empty_params(true);
                            final_trait_path.mParams = MonomorphHrlsOnly(crate.types, pps).monomorph_path_params(sp, final_trait_path.mParams, true);
                        }
                        possibilities.push_back(::std::make_pair(borrowType, ::HIR::Path(*self_ty_p, mv$(final_trait_path), method_name, {})));
                        DEBUG("++ " << possibilities.back());
                        rv = true;
                        found_trait_object = true;
                    }
                };

                const ::HIR::Function* fcn_ptr = nullptr;
                if (trait_contains_method_inner(trait, method_name, fcn_ptr)) {
                    assert(fcn_ptr);
                    addTraitObjectMethod(*fcn_ptr, e.mTrait.mPath.clone());
                } else {
                    const auto self_ty = crate.types.self();
                    auto monomorph_cb = MonomorphStatePtr(crate.types, self_ty, &e.mTrait.mPath.mParams, nullptr);
                    for (const auto& st : trait.allParentTraits) {
                        fcn_ptr = nullptr;
                        if (!trait_contains_method_inner(*st.traitPtr, method_name, fcn_ptr)) {
                            continue;
                        }
                        assert(fcn_ptr);
                        static ::HIR::GenericParams empty_hrtbs;
                        auto _h = monomorph_cb.push_hrb(st.hrtbs ? *st.hrtbs : empty_hrtbs);
                        auto final_trait_path = ::HIR::GenericPath(st.mPath.mPath, monomorph_cb.monomorph_path_params(sp, st.mPath.mParams, false));
                        addTraitObjectMethod(*fcn_ptr, std::move(final_trait_path));
                    }
                }

                // If the method was found on the trait object, prefer that over all others.
                if (found_trait_object) {
                    return rv;
                }
            }

            // 5. Mutually exclusive searches
            // - Erased type - `impl Trait`
            if (const auto* ityp = get_inner_type(ty, [](const auto& t) {
                return t->is_ErasedType();
            })) {
                const auto& e = ityp->as_ErasedType();
                for (const auto& trait_path : e.traits) {
                    const auto& trait = this->crate.get_trait_by_path(sp, trait_path.mPath.mPath);

                    ::HIR::GenericPath final_trait_path;
                    if (const auto* fcn_ptr = this->trait_contains_method(sp, trait_path.mPath, trait, crate.types.self(), method_name, final_trait_path)) {
                        DEBUG("- Found trait " << final_trait_path << " (erased type)");

                        if (auto self_ty_p = checkMethodReceiver(sp, *fcn_ptr, ty, access)) {
                            possibilities.push_back(::std::make_pair(borrowType, ::HIR::Path(*self_ty_p, mv$(final_trait_path), method_name, {})));
                            DEBUG("++ " << possibilities.back());
                            rv = true;
                        }
                    }
                }
            }
            // Generics: Nothing except the bounds (Which have already been checked)
            else if (get_inner_type(ty, [](const auto& t) {
                return t->is_Generic();
            })) {
            }
            // UfcsKnown paths: Can have trait bounds added by the definer
            else if (const auto* ityp = get_inner_type(ty, [](const auto& t) {
                return t->is_Path() && t->as_Path().path.mData.is_UfcsKnown();
            })) {
                const auto& e = ityp->as_Path().path.mData.as_UfcsKnown();
                DEBUG("UfcsKnown - Search associated type bounds in trait - " << e.trait);

                // UFCS known - Assuming that it's reached the maximum resolvable level (i.e. a type within is generic), search for trait bounds on the type

                // `Self` = `*.type`
                // `/*I:#*/` := `e.trait.m_params`
                auto monomorph_cb = MonomorphStatePtr(crate.types, e.type, &e.trait.mParams, &e.params);

                const auto& trait = this->crate.get_trait_by_path(sp, e.trait.mPath);
                const auto& assocTy = trait.types.at(e.item);
                // NOTE: The bounds here have 'Self' = the type
                for (const auto& bound : assocTy.traitBounds) {
                    ASSERT_BUG(sp, bound.traitPtr, "Pointer to trait " << bound.mPath << " not set in " << e.trait.mPath);
                    ::HIR::GenericPath final_trait_path;

                    auto ty_self = crate.types.path(::HIR::Path(crate.types.self(), bound.mPath.clone(), e.item), HIR::TypePathBinding::make_Opaque({}));
                    if (const auto* fcn_ptr = this->trait_contains_method(sp, bound.mPath, *bound.traitPtr, ty_self, method_name, final_trait_path)) {
                        DEBUG("- Found trait " << final_trait_path << " (UFCS Known, aty bounds)");

                        if (auto self_ty_p = checkMethodReceiver(sp, *fcn_ptr, ty, access)) {
                            if (*self_ty_p == ityp) {
                                auto pp_hrb = bound.hrtbs ? bound.hrtbs->make_empty_params(true) : HIR::PathParams();
                                monomorph_cb.pp_hrb = &pp_hrb;
                                final_trait_path = monomorph_cb.monomorph_genericpath(sp, final_trait_path, false);
                                DEBUG("- Monomorph to " << final_trait_path);

                                // Found the method, return the UFCS path for it
                                possibilities.push_back(::std::make_pair(borrowType, ::HIR::Path(*self_ty_p, mv$(final_trait_path), method_name, {})));
                                DEBUG("++ " << possibilities.back());
                                rv = true;
                            }
                        }
                    }
                }

                // Search `<Self as Trait>::Name` bounds on the trait itself
                for (const auto& bound : trait.mParams.bounds) {
                    if (!bound.is_TraitBound()) {
                        continue;
                    }
                    const auto& be = bound.as_TraitBound();

                    if (!be.type->is_Path()) {
                        continue;
                    }
                    if (!be.type->as_Path().binding.is_Opaque()) {
                        continue;
                    }

                    const auto& beTypePe = be.type->as_Path().path.mData.as_UfcsKnown();
                    if (beTypePe.type != crate.types.self()) {
                        continue;
                    }
                    if (beTypePe.trait.mPath != e.trait.mPath) {
                        continue;
                    }
                    if (beTypePe.item != e.item) {
                        continue;
                    }

                    // Found such a bound, now to test if it is useful

                    ::HIR::GenericPath final_trait_path;
                    if (const auto* fcn_ptr = this->trait_contains_method(sp, be.trait.mPath, *be.trait.traitPtr, crate.types.self(), method_name, final_trait_path)) {
                        DEBUG("- Found trait " << final_trait_path << " (UFCS Known, trait bounds)");

                        if (auto self_ty_p = checkMethodReceiver(sp, *fcn_ptr, ty, access)) {
                            if (*self_ty_p == ityp) {
                                if (monomorphise_pathparams_needed(final_trait_path.mParams)) {
                                    final_trait_path.mParams = monomorph_cb.monomorph_path_params(sp, final_trait_path.mParams, false);
                                    DEBUG("- Monomorph to " << final_trait_path);
                                }

                                // Found the method, return the UFCS path for it
                                possibilities.push_back(::std::make_pair(borrowType, ::HIR::Path(*self_ty_p, mv$(final_trait_path), method_name, {})));
                                DEBUG("++ " << possibilities.back());
                                rv = true;
                            }
                        }
                    }
                }
            } else {
            }

            // 6. Search for trait methods (using currently in-scope traits)
            DEBUG("> Trait methods");
            for (const auto& trait_ref : ::reverse(traits)) {
                if (trait_ref.first == nullptr) {
                    break;
                }

                ::HIR::GenericPath final_trait_path;
                const ::HIR::Function* fcn_ptr;
                if (!(fcn_ptr = this->trait_contains_method(sp, *trait_ref.first, *trait_ref.second, crate.types.self(), method_name, final_trait_path))) {
                    continue;
                }
                DEBUG("- Found trait " << final_trait_path << " (in scope)");

                if (auto self_ty_p = checkMethodReceiver(sp, *fcn_ptr, ty, access)) {
                    const auto& self_ty = *self_ty_p;
                    DEBUG("Search for impl of " << *trait_ref.first << " for " << self_ty);

                    // Use the set of ivars we were given to populate the trait parameters
                    ::HIR::PathParams trait_params = get_ivared_params(trait_ref.second->mParams);

                    // TODO: Re-monomorphise the trait path!

                    bool magic_found = false;
                    bool crate_impl_found = false;

                    crate_impl_found = find_trait_impls_magic(sp, *trait_ref.first, trait_params, self_ty, [&](auto impl, auto cmp) {
                        return true;
                    });

                    // NOTE: This just detects the presence of a trait impl, not the specifics
                    try {
                        // Keep this an existential probe over the trait arguments.  They are
                        // committed only after the method signature has constrained the shared
                        // inference variables (matching rustc's probe/confirm split).
                        find_trait_impls_crate(sp, *trait_ref.first, nullptr, self_ty, [&](auto impl, auto cmp) {
                            DEBUG("[find_method] " << impl << ", cmp = " << cmp);
                            magic_found = true;
                            crate_impl_found = true;
                            return true;
                        });
                    } catch (const TraitResolution::RecursionDetected&) {
                        DEBUG("Recursion detected, assuming good");
                        magic_found = true;
                        crate_impl_found = true;
                    }
                    if (crate_impl_found) {
                        DEBUG("Found trait impl " << *trait_ref.first << trait_params << " for " << self_ty << " (" << this->ivars.fmt_type(self_ty) << ")");
                        possibilities.push_back(::std::make_pair(borrowType, ::HIR::Path(self_ty, ::HIR::GenericPath(*trait_ref.first, mv$(trait_params)), method_name, {})));
                        DEBUG("++ " << possibilities.back());
                        rv = true;
                    }
                } else {
                    DEBUG("> Incorrect receiver");
                }
            }

            return rv;
        }

        unsigned int TraitResolution::autoderefFindField(const Span& sp, const ::HIR::TypeData* top_ty, const RcString& field_name, /* Out -> */ ::HIR::TypeRef& field_type) const {
            unsigned int deref_count = 0;
            ::HIR::TypeRef tmp_type; // Temporary type used for handling Deref
            const auto* current_ty = top_ty;
            if (const auto* e = this->ivars.get_type(top_ty)->opt_Borrow()) {
                current_ty = e->inner;
                deref_count += 1;
            }

            do {
                const auto& ty = this->ivars.get_type(current_ty);
                if (ty->is_Infer()) {
                    return ~0u;
                }
                if (ty->is_Path() && ty->as_Path().binding.is_Unbound()) {
                    return ~0u;
                }

                if (this->find_field(sp, ty, field_name, field_type)) {
                    return deref_count;
                }

                // 3. Dereference and try again
                deref_count += 1;
                current_ty = this->autoderef(sp, ty, tmp_type);
            } while (current_ty);

            if (/*const auto* e =*/this->ivars.get_type(top_ty)->opt_Borrow()) {
                const auto& ty = this->ivars.get_type(top_ty);

                if (find_field(sp, ty, field_name, field_type)) {
                    return 0;
                }
            }

            // Dereference failed! This is a hard error (hitting _ is checked above and returns ~0)
            this->ivars.dump();
            TODO(sp, "Error when no field could be found, but type is known - (: " << top_ty << ")." << field_name);
        }

        bool TraitResolution::find_field(const Span& sp, const ::HIR::TypeData* ty, const RcString& name, /* Out -> */ ::HIR::TypeRef& field_ty) const {
            if (const auto* e = ty->opt_Path()) {
        TU_MATCH_HDRA( (e->binding), {)
        TU_ARMA(Unbound, be) {
                // Wut?
                TODO(sp, "Handle TypePathBinding::Unbound - " << ty);
            }
            TU_ARMA(Opaque, be) {
                // Ignore, no fields on an opaque
            }
            TU_ARMA(Struct, be) {
                // Has fields!
                const auto& str = *be;
                const auto& params = e->path.mData.as_Generic().mParams;
                auto monomorph = MonomorphStatePtr(crate.types, ty, &params, nullptr);
            TU_MATCH_HDRA( (str.mData), {)
            TU_ARMA(Unit, se) {
                        // No fields on a unit struct
                    }
                    TU_ARMA(Tuple, se) {
                        for (unsigned int i = 0; i < se.size(); i++) {
                            DEBUG(i << ": " << se[i].publicity << ", " << this->visPath << " : " << se[i].ent);
                            if (se[i].publicity.is_visible(this->visPath) && FMT(i) == name) {
                                field_ty = monomorph.monomorph_type(sp, se[i].ent);
                                return true;
                            }
                        }
                    }
                    TU_ARMA(Named, se) {
                        for (const auto& fld : se) {
                            DEBUG(fld.name << ": " << fld.vis << ", " << this->visPath << " : " << fld.ty);
                            if (fld.vis.is_visible(this->visPath) && fld.name == name) {
                                field_ty = monomorph.monomorph_type(sp, fld.ty);
                                return true;
                            }
                        }
                    }
            }
            }
            TU_ARMA(Enum, be) {
                // No fields on enums either
            }
            TU_ARMA(ExternType, be) {
                // No fields on extern types
            }
            TU_ARMA(Union, be) {
                const auto& unm = *be;
                const auto& params = e->path.mData.as_Generic().mParams;
                auto monomorph = MonomorphStatePtr(crate.types, ty, &params, nullptr);

                for (const auto& fld : unm.mVariants) {
                    if (fld.vis.is_visible(this->visPath) && fld.name == name) {
                        field_ty = monomorph.monomorph_type(sp, fld.ty);
                        return true;
                    }
                }
            }
        }
            } else if (const auto* e = ty->opt_Tuple()) {
                for (unsigned int i = 0; i < e->size(); i++) {
                    if (FMT(i) == name) {
                        field_ty = (*e)[i];
                        return true;
                    }
                }
            } else {
            }
            return false;
        }

HMTypeInferrence::FmtType::FmtType(const HMTypeInferrence& ctxt, const ::HIR::TypeData* ty)
    : ctxt(ctxt)
    , ty(ty) {
}
HMTypeInferrence::FmtPP::FmtPP(const HMTypeInferrence& ctxt, const ::HIR::PathParams& pps)
    : ctxt(ctxt)
    , pps(pps) {
}
// Null only when alias != ~0

HMTypeInferrence::IVar::IVar(::HIR::TypeRef type)
    : alias(~0u)
    , type(type) {
}
HMTypeInferrence::IVarValue::IVarValue()
    : alias(~0u)
    , val(new ::HIR::ConstGeneric()) {
}
HMTypeInferrence::HMTypeInferrence(HIR::TypeInterner& types)
    : types(types)
    , hasChanged(false) {
}
bool HMTypeInferrence::take_changed() {
    bool rv = hasChanged;
    hasChanged = false;
    return rv;
}
void HMTypeInferrence::mark_change() {
    if (!hasChanged) {
        DEBUG("- CHANGE");
        hasChanged = true;
    }
}
HMTypeInferrence::ResolvePlaceholders::ResolvePlaceholders(const HMTypeInferrence& parent)
    : parent(parent) {
}
TraitResolution::LegacyTraitGoal::LegacyTraitGoal(
    const ::HIR::SimplePath& trait,
    const ::HIR::PathParams& params,
    bool has_params,
    const ::HIR::TypeData* type
)
    : trait(trait.clone())
    , params(params.clone())
    , type(type)
    , has_params(has_params) {
}
bool TraitResolution::LegacyTraitGoal::matches(
    const ::HIR::SimplePath& other_trait,
    const ::HIR::PathParams& other_params,
    bool other_has_params,
    const ::HIR::TypeData* other_type
) const {
    return trait == other_trait
        && has_params == other_has_params
        && (!has_params || params == other_params)
        && type == other_type;
}
/// Expand any located associated types in the input, operating in-place and returning the result
::HIR::TypeRef TraitResolution::expand_associated_types(const Span& sp, ::HIR::TypeRef input) const {
    expand_associated_types_inplace(sp, input, LList<const ::HIR::TypeData*>());
    return input;
}
const ::HIR::TypeData* TraitResolution::expand_associated_types(const Span& sp, const ::HIR::TypeData* input, ::HIR::TypeRef& tmp) const {
    if (this->has_associated_type(input)) {
        return (tmp = this->expand_associated_types(sp, input));
    } else {
        return input;
    }
}
void TraitResolution::expand_associated_types_params(const Span& sp, ::HIR::PathParams& params) const {
    for (auto& type : params.types) {
        if (this->has_associated_type(type)) {
            type = this->expand_associated_types(sp, type);
        }
    }
}

bool type_is_unbounded_infer(const ::HIR::TypeData* ty) {
    if (const auto* te = ty->opt_Infer()) {
        switch (te->ty_class) {
            case ::HIR::InferClass::Integer:
                return false;
            case ::HIR::InferClass::Float:
                return false;
            case ::HIR::InferClass::None:
                return true;
        }
    }
    return false;
}
::std::ostream& operator<<(::std::ostream& os, const HMTypeInferrence::FmtType& x) {
    x.ctxt.print_type(os, x.ty);
    return os;
}
::std::ostream& operator<<(::std::ostream& os, const HMTypeInferrence::FmtPP& x) {
    x.ctxt.print_pathparams(os, x.pps);
    return os;
}

const ::HIR::TypeData* HMTypeInferrence::ResolvePlaceholders::get_type(const Span& sp, const HIR::TypeData* ty) const {
    if (ty->is_Infer()) {
        return parent.get_type(ty);
    } else {
        return ty;
    }
}
const ::HIR::ConstGeneric& HMTypeInferrence::ResolvePlaceholders::get_val(const Span& sp, const HIR::ConstGeneric& v) const {
    if (v.is_Infer()) {
        return parent.get_value(v);
    } else {
        return v;
    }
}
