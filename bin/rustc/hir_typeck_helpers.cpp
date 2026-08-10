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
        mutable ::std::vector<::std::pair<RcString, RcString>> m_placeholder_names;

        RcString canonical_placeholder_name(const RcString& name) const {
            for (const auto& entry : m_placeholder_names) {
                if (entry.first == name) {
                    return entry.second;
                }
            }
            auto canonical = RcString::new_interned(
                FMT("#solver-placeholder-" << m_placeholder_names.size())
            );
            m_placeholder_names.push_back({name, canonical});
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
                ? m_types.generic(
                    canonical_placeholder_name(generic.name), generic.binding
                )
                : m_types.generic(generic.name, generic.binding);
        }

        ::HIR::ConstGeneric get_value(
            const Span&, const ::HIR::GenericRef& generic
        ) const override {
            return ::HIR::ConstGeneric(
                generic.is_placeholder()
                    ? ::HIR::GenericRef(
                        canonical_placeholder_name(generic.name), generic.binding
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

        virtual ::HIR::Compare match_ty(const ::HIR::GenericRef& g, const ::HIR::TypeRef& ty, HIR::t_cb_resolve_type resolve_cb) {
            return (ty->is_Generic() && ty->as_Generic().binding == g.binding) ? ::HIR::Compare::Equal : ::HIR::Compare::Unequal;
        }

        virtual ::HIR::Compare match_val(const ::HIR::GenericRef& g, const ::HIR::ConstGeneric& sz) {
            return sz == g ? ::HIR::Compare::Equal : ::HIR::Compare::Unequal;
        }

        virtual ::HIR::Compare match_lft(const ::HIR::GenericRef& g, const ::HIR::LifetimeRef& lft) {
            if (!::HIR::MatchGenerics::has_hrb() && g.group() == ::HIR::GENERIC_Hrtb) {
                ASSERT_BUG(Span(), g.idx() < hrls.m_lifetimes.size(), "HRL index out of range");
                hrls.m_lifetimes.at(g.idx()) = lft;
                return ::HIR::Compare::Equal;
            }
            return lft.binding == g.binding ? ::HIR::Compare::Equal : ::HIR::Compare::Unequal;
        }

        // Monomorphiser
        ::HIR::TypeRef get_type(const Span& sp, const ::HIR::GenericRef& g) const {
            return m_types.generic(g.name, g.binding);
        }

        ::HIR::ConstGeneric get_value(const Span& sp, const ::HIR::GenericRef& g) const {
            return g;
        }

        ::HIR::LifetimeRef get_lifetime(const Span& sp, const ::HIR::GenericRef& g) const {
            if (g.group() == ::HIR::GENERIC_Hrtb) {
                return hrls.m_lifetimes.at(g.idx());
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
    for (const auto& v : m_ivars) {
        auto i = &v - &m_ivars.front();
        if (v.is_alias()) {
            //DEBUG("#" << i << " = " << v.alias);
        } else {
            DEBUG("#" << i << " = " << v.type << FMT_CB(os, bool open = false; unsigned int i2 = 0; for (const auto& v2 : m_ivars) {
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
    for (const auto& v : m_values) {
        auto i = &v - &m_values.front();
        if (v.is_alias()) {
        } else {
            DEBUG("V#" << i << " = " << *v.val << FMT_CB(os, bool open = false; for (const auto& v2 : m_values) {
                      auto i2 = &v2 - &m_values.front();
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

void HMTypeInferrence::check_for_loops() {
#if 1
    struct LoopChecker {
        ::std::vector<unsigned int> m_indexes;

        void check_ty(const HMTypeInferrence& ivars, const ::HIR::TypeRef& ty) {
            visit_ty_with(ty, [&](const HIR::TypeRef& t) {
                if (const auto* ep = t->opt_Infer()) {
                    const auto& e = *ep;
                    for (auto idx : m_indexes) {
                        ASSERT_BUG(Span(), e.index != idx, "Recursion in ivar #" << m_indexes.front() << " " << ivars.m_ivars[m_indexes.front()].type << " - loop with " << idx << " " << ivars.m_ivars[idx].type);
                    }
                    const auto& ivd = ivars.get_pointed_ivar(e.index);
                    assert(!ivd.is_alias());
                    if (!ivd.type->is_Infer()) {
                        m_indexes.push_back(e.index);
                        this->check_ty(ivars, ivd.type);
                        m_indexes.pop_back();
                    }
                }
                return false;
            });
        }
    };
#else
    struct LoopChecker {
        ::std::vector<unsigned int> m_indexes;

        void check_pathparams(const HMTypeInferrence& ivars, const ::HIR::PathParams& pp) {
            for (const auto& ty : pp.m_types) {
                this->check_ty(ivars, ty);
            }
        }

        void check_path(const HMTypeInferrence& ivars, const ::HIR::Path& path) {
            TU_MATCH(::HIR::Path::Data, (path.m_data), (pe), (Generic, this->check_pathparams(ivars, pe.m_params);), (UfcsKnown, this->check_ty(ivars, pe.type); this->check_pathparams(ivars, pe.trait.m_params); this->check_pathparams(ivars, pe.params);), (UfcsInherent, this->check_ty(ivars, pe.type); this->check_pathparams(ivars, pe.params);), (UfcsUnknown, BUG(Span(), "UfcsUnknown");))
        }

        void check_ty(const HMTypeInferrence& ivars, const ::HIR::TypeRef& ty) {
            TU_MATCH_HDRA( (*ty), {)
            TU_ARMA(Infer, e) {
                    for (auto idx : m_indexes) {
                        ASSERT_BUG(Span(), e.index != idx, "Recursion in ivar #" << m_indexes.front() << " " << *ivars.m_ivars[m_indexes.front()].type << " - loop with " << idx << " " << *ivars.m_ivars[idx].type);
                    }
                    const auto& ivd = ivars.get_pointed_ivar(e.index);
                    assert(!ivd.is_alias());
                    if (!ivd.type->data().is_Infer()) {
                        m_indexes.push_back(e.index);
                        this->check_ty(ivars, *ivd.type);
                        m_indexes.pop_back();
                    }
                }
                TU_ARMA(Primitive, e) {
                }
                TU_ARMA(Diverge, e) {
                }
                TU_ARMA(Generic, e) {
                }
                TU_ARMA(Path, e) {
                    this->check_path(ivars, e.path);
                }
                TU_ARMA(Borrow, e) {
                    this->check_ty(ivars, e.inner);
                }
                TU_ARMA(Pointer, e) {
                    this->check_ty(ivars, e.inner);
                }
                TU_ARMA(Slice, e) {
                    this->check_ty(ivars, e.inner);
                }
                TU_ARMA(Array, e) {
                    this->check_ty(ivars, e.inner);
                }
                TU_ARMA(Closure, e) {
                }
                TU_ARMA(Generator, e) {
                }
                TU_ARMA(Function, e) {
                    for (const auto& arg : e.m_arg_types) {
                        this->check_ty(ivars, arg);
                    }
                    this->check_ty(ivars, e.m_rettype);
                }
                TU_ARMA(TraitObject, e) {
                    this->check_pathparams(ivars, e.m_trait.m_path.m_params);
                    for (const auto& aty : e.m_trait.m_type_bounds) {
                        this->check_ty(ivars, aty.second.type);
                    }
                    for (const auto& marker : e.m_markers) {
                        this->check_pathparams(ivars, marker.m_params);
                    }
                }
                TU_ARMA(ErasedType, e) {
                    this->check_path(ivars, e.m_origin);
                    for (const auto& trait : e.m_traits) {
                        this->check_pathparams(ivars, trait.m_path.m_params);
                        for (const auto& aty : trait.m_type_bounds) {
                            this->check_ty(ivars, aty.second.type);
                        }
                    }
                }
                TU_ARMA(Tuple, e) {
                    for (const auto& st : e) {
                        this->check_ty(ivars, st);
                    }
                }
            }
        }
    };
#endif
    unsigned int i = 0;
    for (const auto& v : m_ivars) {
        if (!v.is_alias() && !v.type->is_Infer()) {
            DEBUG("- " << i << " " << v.type);
            (LoopChecker{{i}}).check_ty(*this, v.type);
        }
        i++;
    }
}

void HMTypeInferrence::compact_ivars() {
    this->check_for_loops();

    unsigned int i = 0;
    for (auto& v : m_ivars) {
        if (!v.is_alias()) {
            auto old = v.type;
            this->expand_ivars(v.type);
            DEBUG("- " << i << " " << old << " -> " << v.type);
        } else {
            auto index = v.alias;
            unsigned int count = 0;
            assert(index < m_ivars.size());
            while (m_ivars.at(index).is_alias()) {
                index = m_ivars.at(index).alias;

                if (count >= m_ivars.size()) {
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

bool HMTypeInferrence::apply_defaults() {
    bool rv = false;
    for (auto& v : m_ivars) {
        if (!v.is_alias()) {
            if (const auto* e = v.type->opt_Infer()) {
                switch (e->ty_class) {
                    case ::HIR::InferClass::None:
                        break;
                    case ::HIR::InferClass::Integer:
                        rv = true;
                        DEBUG("- IVar " << e->index << " = i32");
                        v.type = m_types.primitive(::HIR::CoreType::I32);
                        break;
                    case ::HIR::InferClass::Float:
                        rv = true;
                        DEBUG("- IVar " << e->index << " = f64");
                        v.type = m_types.primitive(::HIR::CoreType::F64);
                        break;
                }
            }
        }
    }
    return rv;
}

void HMTypeInferrence::print_type(::std::ostream& os, const ::HIR::TypeRef& tr, LList<const ::HIR::TypeRef*> outer_stack) const {
    const auto& ty = this->get_type(tr);
    for (const auto* pty : outer_stack) {
        if (pty) {
            if (pty == &ty) {
                os << "/*RECURSE*/";
                return;
            }
        }
    }
    auto stack = LList<const ::HIR::TypeRef*>(&outer_stack, &ty);

    auto print_traitpath = [&](const HIR::TraitPath& tp) {
        if (tp.m_hrtbs && !tp.m_hrtbs->is_empty()) {
            os << "for" << tp.m_hrtbs->fmt_args() << " ";
        }
        this->print_genericpath(os, tp.m_path, stack);
        // TODO: ATYs?
    };
    auto print_path = [&](const HIR::Path& path) {
        TU_MATCH_HDRA( (path.m_data), {)
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
                    for (const auto& arg : node_p->m_args) {
                        this->print_type(os, arg.second, stack);
                        os << ",";
                    }
                    os << ")->";
                    this->print_type(os, node_p->m_return, stack);
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
            if (e.m_abi != "") {
                os << "extern \"" << e.m_abi << "\" ";
            }
            os << "fn(";
            for (const auto& arg : e.m_arg_types) {
                this->print_type(os, arg, stack);
                os << ",";
            }
            os << ")->";
            this->print_type(os, e.m_rettype, stack);
        }
        TU_ARMA(TraitObject, e) {
            os << "dyn (";
            print_traitpath(e.m_trait);
            for (const auto& marker : e.m_markers) {
                os << "+";
                this->print_genericpath(os, marker, stack);
            }
            if (e.m_lifetime != ::HIR::LifetimeRef::new_static()) {
                os << "+" << e.m_lifetime;
            }
            os << ")";
        }
        TU_ARMA(ErasedType, e) {
            os << "impl ";
            for (const auto& tr : e.m_traits) {
                if (&tr != &e.m_traits[0]) {
                    os << "+";
                }
                print_traitpath(tr);
            }
            if (!e.m_lifetime_bounds.empty()) {
                for (const auto& lft : e.m_lifetime_bounds) {
                    os << "+" << lft;
                }
            }
            os << "+use";
            this->print_pathparams(os, e.m_use, outer_stack);
            os << "/*";
        TU_MATCH_HDRA( (e.m_inner), {)
        TU_ARMA(Fcn, ee) {
                    os << "fn ";
                    print_path(ee.m_origin);
                    os << "#" << ee.m_index;
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

void HMTypeInferrence::print_genericpath(::std::ostream& os, const ::HIR::GenericPath& gp, LList<const ::HIR::TypeRef*> stack) const {
    os << gp.m_path;
    this->print_pathparams(os, gp.m_params, stack);
}

void HMTypeInferrence::print_pathparams(::std::ostream& os, const ::HIR::PathParams& pps, LList<const ::HIR::TypeRef*> stack) const {
    if (pps.has_params() || !pps.m_lifetimes.empty()) {
        os << "<";
        for (const auto& pp_l : pps.m_lifetimes) {
            os << pp_l;
            os << ",";
        }
        for (const auto& pp_t : pps.m_types) {
            this->print_type(os, pp_t, stack);
            os << ",";
        }
        for (const auto& pp_v : pps.m_values) {
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
    if (::std::find(m_expand_stack.begin(), m_expand_stack.end(), type) != m_expand_stack.end()) return;
    m_expand_stack.push_back(type);
    struct Guard {
        ::std::vector<HIR::TypeRef>& stack;
        ~Guard() { stack.pop_back(); }
    } guard{m_expand_stack};

    if (type->is_Infer()) {
        const auto& resolved = this->get_type(type);
        if (resolved != type) type = resolved;
        return;
    }

    auto data = type->clone_data();

    struct H {
        static void expand_ivars_path(/*const*/ HMTypeInferrence& self, ::HIR::Path& path) {
            TU_MATCH(::HIR::Path::Data, (path.m_data), (e2), (Generic, self.expand_ivars_params(e2.m_params);), (UfcsKnown, self.expand_ivars(e2.type); self.expand_ivars_params(e2.trait.m_params); self.expand_ivars_params(e2.params);), (UfcsUnknown, self.expand_ivars(e2.type); self.expand_ivars_params(e2.params);), (UfcsInherent, self.expand_ivars(e2.type); self.expand_ivars_params(e2.params);))
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
            this->expand_ivars_params(e.m_trait.m_path.m_params);
            for (auto& marker : e.m_markers) {
                this->expand_ivars_params(marker.m_params);
            }
            // TODO: Associated types
        }
        TU_ARMA(ErasedType, e) {
        TU_MATCH_HDRA( (e.m_inner), {)
        TU_ARMA(Fcn, ee) {
                    H::expand_ivars_path(*this, ee.m_origin);
                }
                TU_ARMA(Known, ee) {
                    this->expand_ivars(ee);
                }
                TU_ARMA(Alias, ee) {
                }
        }
        for(auto& trait : e.m_traits)
        {
                this->expand_ivars_params(trait.m_path.m_params);
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
            this->expand_ivars(e.m_rettype);
            for (auto& ty : e.m_arg_types) {
                this->expand_ivars(ty);
            }
        }
    TU_ARMA(NodeType, e) {
        }
    }
    type = m_types.intern(std::move(data));
}

void HMTypeInferrence::expand_ivars_params(::HIR::PathParams& params) {
    for (auto& arg : params.m_types) {
        expand_ivars(arg);
    }
}

void HMTypeInferrence::add_ivars(::HIR::TypeRef& type) {
    if (type->is_Infer() && type->as_Infer().index == ~0u) {
        type = new_ivar_tr(type->as_Infer().ty_class);
        this->mark_change();
        DEBUG("New ivar " << type);
        return;
    }

    auto data = type->clone_data();
    TU_MATCH_HDRA( (data), {)
    TU_ARMA(Infer, e) {
        }
        TU_ARMA(Diverge, e) {
        }
        TU_ARMA(Primitive, e) {
        }
        TU_ARMA(Path, e) {
            // Iterate all arguments
            TU_MATCH(::HIR::Path::Data, (e.path.m_data), (e2), (Generic, this->add_ivars_params(e2.m_params);), (UfcsKnown, this->add_ivars(e2.type); this->add_ivars_params(e2.trait.m_params); this->add_ivars_params(e2.params);), (UfcsUnknown, this->add_ivars(e2.type); this->add_ivars_params(e2.params);), (UfcsInherent, this->add_ivars(e2.type); this->add_ivars_params(e2.params);))
        }
        TU_ARMA(Generic, e) {
        }
        TU_ARMA(TraitObject, e) {
            // Iterate all paths
            this->add_ivars_params(e.m_trait.m_path.m_params);
            for (auto& aty : e.m_trait.m_type_bounds) {
                this->add_ivars(aty.second.type);
            }
            for (auto& marker : e.m_markers) {
                this->add_ivars_params(marker.m_params);
            }
        }
        TU_ARMA(ErasedType, e) {
            if (type_contains_ivars(type, /*only_unbound=*/true)) {
                BUG(Span(), "ErasedType getting ivars added - " << type);
            }
        }
        TU_ARMA(Array, e) {
            add_ivars(e.inner);
            if (e.size.is_Unevaluated()) {
                add_ivars(e.size.as_Unevaluated());
            }
        }
        TU_ARMA(Slice, e) {
            add_ivars(e.inner);
        }
        TU_ARMA(Tuple, e) {
            for (auto& ty : e) {
                add_ivars(ty);
            }
        }
        TU_ARMA(Borrow, e) {
            add_ivars(e.inner);
        }
        TU_ARMA(Pointer, e) {
            add_ivars(e.inner);
        }
        TU_ARMA(NamedFunction, e) {
            // Shouldn't be possible?
            // Even if it is seen, it shouldn't have any empty ivars
        }
        TU_ARMA(Function, e) {
            add_ivars(e.m_rettype);
            for (auto& ty : e.m_arg_types) {
                add_ivars(ty);
            }
        }
    TU_ARMA(NodeType, e) {
            // Shouldn't be possible
        }
    }
    type = m_types.intern(std::move(data));
}

void HMTypeInferrence::add_ivars(::HIR::ConstGeneric& val) {
    if (val.is_Infer()) {
        if (val.as_Infer().index == ~0u) {
            val.as_Infer().index = new_ivar_val();
            this->mark_change();
            DEBUG("New ivar " << val);
        }
    }
}

void HMTypeInferrence::add_ivars_params(::HIR::PathParams& params) {
    for (auto& arg : params.m_types) {
        add_ivars(arg);
    }
    for (auto& arg : params.m_values) {
        add_ivars(arg);
    }
}

unsigned int HMTypeInferrence::new_ivar(HIR::InferClass ic /* = HIR::InferClass::None*/) {
    auto rv = m_ivars.size();
    m_ivars.emplace_back(m_types.infer(rv, ic));
    DEBUG("New type IVar " << rv);
    return rv;
}

::HIR::TypeRef HMTypeInferrence::new_ivar_tr(HIR::InferClass ic /* = HIR::InferClass::None*/) {
    return m_ivars.at(this->new_ivar(ic)).type;
}

unsigned int HMTypeInferrence::new_ivar_val() {
    m_values.push_back(IVarValue());
    m_values.back().val->as_Infer().index = m_values.size() - 1;
    return m_values.size() - 1;
}

void HMTypeInferrence::set_ivar_val_to(unsigned int slot, ::HIR::ConstGeneric val) {
    ASSERT_BUG(Span(), slot < m_values.size(), "slot " << slot << " >= " << m_values.size());
    ASSERT_BUG(Span(), !m_values[slot].is_alias(), "slot " << slot);
    if (*m_values[slot].val == val) {
        //DEBUG("Set ValIVar " << slot << " == " << val);
    } else {
        DEBUG("Set ValIVar " << slot << " = " << val);
        ASSERT_BUG(Span(), m_values[slot].val->is_Infer(), "slot " << slot << " - " << *m_values[slot].val);
        ASSERT_BUG(Span(), m_values[slot].val->as_Infer().index == slot, "slot " << slot << " - " << *m_values[slot].val);
        *m_values[slot].val = std::move(val);
    }
}

void HMTypeInferrence::ivar_val_unify(unsigned int left_slot, unsigned int right_slot) {
    Span sp;
    ASSERT_BUG(sp, left_slot < m_values.size(), "slot " << left_slot << " >= " << m_values.size());
    ASSERT_BUG(sp, right_slot < m_values.size(), "slot " << left_slot << " >= " << m_values.size());
    ASSERT_BUG(sp, !m_values[left_slot].is_alias(), "slot " << left_slot);
    ASSERT_BUG(sp, !m_values[right_slot].is_alias(), "slot " << right_slot);

    if (/*const auto* re =*/m_values[right_slot].val->opt_Infer()) {
        DEBUG("Set ValIVar " << right_slot << " = @" << left_slot);
        m_values[right_slot].alias = left_slot;
        m_values[right_slot].val.reset();

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

const ::HIR::TypeRef& HMTypeInferrence::get_type(const ::HIR::TypeRef& type) const {
    const auto* current = &type;
    for (size_t count = 0; count <= m_ivars.size(); count++) {
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
    for (size_t count = 0; count <= m_ivars.size(); count++) {
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

const ::HIR::TypeRef& HMTypeInferrence::get_type(unsigned idx) const {
    assert(idx != ~0u);
    const auto* current = &get_pointed_ivar(idx).type;
    for (size_t count = 0; count <= m_ivars.size(); count++) {
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
                (Primitive, check_type_class_primitive(sp, type, l_e->ty_class, e);),
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
        struct Monomorph_AddLifetimes: public Monomorphiser {
            explicit Monomorph_AddLifetimes(HIR::TypeInterner& types): Monomorphiser(types) {}

            ::HIR::TypeRef get_type(const Span& sp, const ::HIR::GenericRef& g) const override {
                return m_types.generic(g.name, g.binding);
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

        type = Monomorph_AddLifetimes(m_types).monomorph_type(sp, type, true);
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
                        check_type_class_primitive(sp, type, e->ty_class, *l_e);
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
                        left_ivar.type = m_types.infer(le->index, re->ty_class);
                    }
                } else if (const auto* le = left_ivar.type->opt_Primitive()) {
                    check_type_class_primitive(sp, left_ivar.type, re->ty_class, *le);
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
    for (unsigned int count = 0; count < m_values.size(); count++) {
        ASSERT_BUG(Span(), index < m_values.size(), "");
        auto& ent = m_values[index];
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
    assert(index < m_ivars.size());
    while (m_ivars.at(index).is_alias()) {
        index = m_ivars.at(index).alias;

        if (count >= m_ivars.size()) {
            this->dump();
            BUG(Span(), "Loop detected in ivar list when starting at " << slot << ", current is " << index);
        }
        count++;
    }
    return const_cast<IVar&>(m_ivars.at(index));
}

bool HMTypeInferrence::pathparams_contain_ivars(const ::HIR::PathParams& pps, bool only_unbound) const {
    for (const auto& ty : pps.m_types) {
        if (this->type_contains_ivars(ty, only_unbound)) {
            return true;
        }
    }
    return false;
}

bool HMTypeInferrence::type_contains_ivars(const ::HIR::TypeRef& ty, bool only_unbound) const {
    if (!ty->has_type_infer()) {
        return false;
    }
    TRACE_FUNCTION_F("ty = " << ty);
    auto path_contains_ivars = [this](const HIR::Path& path, bool only_unbound) {
        TU_MATCH(::HIR::Path::Data, (path.m_data), (pe), (Generic, return this->pathparams_contain_ivars(pe.m_params, only_unbound);), (UfcsKnown, if (this->type_contains_ivars(pe.type, only_unbound)) return true; if (this->pathparams_contain_ivars(pe.trait.m_params, only_unbound)) return true; return this->pathparams_contain_ivars(pe.params, only_unbound);), (UfcsInherent, if (this->type_contains_ivars(pe.type, only_unbound)) return true; return this->pathparams_contain_ivars(pe.params, only_unbound);), (UfcsUnknown, BUG(Span(), "UfcsUnknown");))
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
        for(const auto& arg : e.m_arg_types)
            if( type_contains_ivars(arg, only_unbound) )
                return true;
        return type_contains_ivars(e.m_rettype, only_unbound);
        ),
    (TraitObject,
        for(const auto& marker : e.m_markers)
            if( pathparams_contain_ivars(marker.m_params, only_unbound) )
                return true;
        return pathparams_contain_ivars(e.m_trait.m_path.m_params, only_unbound);
        ),
    (ErasedType,
        TU_MATCH_HDRA( (e.m_inner), {)
        TU_ARMA(Fcn, ee) {
            return path_contains_ivars(ee.m_origin, only_unbound);
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
            return type_list_equal(*this, pps_l.m_types, pps_r.m_types);
        }

        bool HMTypeInferrence::types_equal(const ::HIR::TypeRef& rl, const ::HIR::TypeRef& rr) const {
            const auto& l = this->get_type(rl);
            const auto& r = this->get_type(rr);
            if (l->tag() != r->tag()) {
                return false;
            }

            struct H {
                static bool compare_path(const HMTypeInferrence& self, const ::HIR::Path& l, const ::HIR::Path& r) {
                    if (l.m_data.tag() != r.m_data.tag()) {
                        return false;
                    }
                    TU_MATCH(::HIR::Path::Data, (l.m_data, r.m_data), (lpe, rpe), (Generic, if (lpe.m_path != rpe.m_path) return false; return self.pathparams_equal(lpe.m_params, rpe.m_params);), (UfcsKnown, if (lpe.item != rpe.item) return false; if (!self.types_equal(lpe.type, rpe.type)) return false; if (!self.pathparams_equal(lpe.trait.m_params, rpe.trait.m_params)) return false; return self.pathparams_equal(lpe.params, rpe.params);), (UfcsInherent, if (lpe.item != rpe.item) return false; if (!self.types_equal(lpe.type, rpe.type)) return false; return self.pathparams_equal(lpe.params, rpe.params);), (UfcsUnknown, BUG(Span(), "UfcsUnknown");))
                    throw "";
                }
            };

    TU_MATCH(::HIR::TypeData, (*l, *r), (le, re),
    (Infer, return le.index == re.index; ),
    (Primitive, return le == re; ),
    (Diverge, return true; ),
    (Generic, return le.binding == re.binding; ),
    (Path,
        return H::compare_path(*this, le.path, re.path);
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
        return H::compare_path(*this, le.path, re.path);
        ),
    (Function,
        if( le.is_unsafe != re.is_unsafe || le.m_abi != re.m_abi )
            return false;
        if( !type_list_equal(*this, le.m_arg_types, re.m_arg_types) )
            return false;
        return types_equal(le.m_rettype, re.m_rettype);
        ),
    (TraitObject,
        if( le.m_markers.size() != re.m_markers.size() )
            return false;
        for(unsigned int i = 0; i < le.m_markers.size(); i ++) {
        const auto& lm = le.m_markers[i];
        const auto& rm = re.m_markers[i];
        if (lm.m_path != rm.m_path) {
            return false;
        }
        if (!pathparams_equal(lm.m_params, rm.m_params)) {
            return false;
        }
        }
        if( le.m_trait.m_path.m_path != re.m_trait.m_path.m_path )
            return false;
        return pathparams_equal(le.m_trait.m_path.m_params, re.m_trait.m_path.m_params);
        ),
    (ErasedType,
        if( le.m_inner.tag() != re.m_inner.tag() )
            return false;
        TU_MATCH_HDRA( (le.m_inner, re.m_inner), {)
        TU_ARMA(Fcn, l,r) {
            ASSERT_BUG(Span(), l.m_origin != ::HIR::SimplePath(), "Erased type with unset origin");
            ASSERT_BUG(Span(), r.m_origin != ::HIR::SimplePath(), "Erased type with unset origin");
            return H::compare_path(*this, l.m_origin, r.m_origin);
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
            ::HIR::Compare compare_value(const Span& sp, const ::HIR::ConstGeneric& left_raw, const ::HIR::ConstGeneric& right_raw, const HMTypeInferrence& infer) {
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

        ::HIR::Compare TraitResolution::compare_pp(const Span& sp, const ::HIR::PathParams& left, const ::HIR::PathParams& right) const {
            ASSERT_BUG(sp, left.m_types.size() == right.m_types.size(), "Parameter count mismatch - `" << left << "` vs `" << right << "`");
            ASSERT_BUG(sp, left.m_values.size() == right.m_values.size(), "Parameter count mismatch - `" << left << "` vs `" << right << "`");
            ::HIR::Compare ord = ::HIR::Compare::Equal;
            for (unsigned int i = 0; i < left.m_types.size(); i++) {
                // TODO: Should allow fuzzy matches using placeholders (match_test_generics_fuzz works for that)
                // - Better solution is to remove the placeholders in method searching.
                ord &= left.m_types[i]->compare_with_placeholders(sp, right.m_types[i], this->m_ivars.callback_resolve_infer());
                if (ord == ::HIR::Compare::Unequal) {
                    return ord;
                }
            }
            for (unsigned int i = 0; i < left.m_values.size(); i++) {
                ord &= compare_value(sp, left.m_values[i], right.m_values[i], this->m_ivars);
                if (ord == ::HIR::Compare::Unequal) {
                    return ord;
                }
            }
            return ord;
        }

        // -------------------------------------------------------------------------------------------------------------------
        //
        // -------------------------------------------------------------------------------------------------------------------
        bool TraitResolution::iterate_bounds_traits(const Span& sp, const HIR::TypeRef& type, const HIR::SimplePath& trait, t_cb_bound cb) const {
            return iterate_bounds_traits(sp, type, [&](HIR::Compare cmp, const HIR::TypeRef& t, const HIR::GenericPath& tr, const CachedBound& b) {
                if (tr.m_path != trait) {
                    return false;
                }
                return cb(cmp, t, tr, b);
            });
        }

        bool TraitResolution::iterate_bounds_traits(const Span& sp, const HIR::TypeRef& type, t_cb_bound cb) const {
            for (const auto& b : m_trait_bounds) {
                auto cmp = b.first.first->compare_with_placeholders(sp, type, this->m_ivars.callback_resolve_infer());
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
            for (const auto& b : m_trait_bounds) {
                if (cb(HIR::Compare::Equal, b.first.first, b.first.second, b.second)) {
                    return true;
                }
            }
            return false;
        }

        bool TraitResolution::iterate_aty_bounds(const Span& sp, const ::HIR::Path::Data::Data_UfcsKnown& pe, ::std::function<bool(const ::HIR::TraitPath&)> cb) const {
            ::HIR::GenericPath trait_path;
            DEBUG("Checking ATY bounds on " << pe.trait << " :: " << pe.item);
            if (!this->trait_contains_type(sp, pe.trait, this->m_crate.get_trait_by_path(sp, pe.trait.m_path), pe.item.c_str(), trait_path)) {
                BUG(sp, "Cannot find associated type " << pe.item << " anywhere in trait " << pe.trait);
            }
            DEBUG("trait_path=" << trait_path);
            const auto& trait_ref = m_crate.get_trait_by_path(sp, trait_path.m_path);
            const auto& aty_def = trait_ref.m_types.find(pe.item)->second;

            for (const auto& bound : aty_def.m_trait_bounds) {
                if (cb(bound)) {
                    return true;
                }
            }
            // Search `<Self as Trait>::Name` bounds on the trait itself
            for (const auto& bound : trait_ref.m_params.m_bounds) {
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

                const auto& be_type_pe = be.type->as_Path().path.m_data.as_UfcsKnown();
                if (be_type_pe.type != m_crate.m_types.self()) {
                    continue;
                }
                if (be_type_pe.trait.m_path != pe.trait.m_path) {
                    continue;
                }
                if (be_type_pe.item != pe.item) {
                    continue;
                }

                if (cb(be.trait)) {
                    return true;
                }
            }

            return false;
        }

        bool TraitResolution::find_trait_impls_magic(const Span& sp, const ::HIR::SimplePath& trait, const ::HIR::PathParams& params, const ::HIR::TypeRef& ty, t_cb_trait_impl_r callback) const {
            static ::HIR::PathParams null_params;
            static ::HIR::TraitPath::assoc_list_t null_assoc;

            const auto lang_CoerceUnsized = this->m_crate.get_lang_item_path_opt("coerce_unsized");
            const auto lang_FnPtr = this->m_crate.get_lang_item_path_opt("fn_ptr_trait");
            const auto lang_Tuple = this->m_crate.get_lang_item_path_opt("tuple_trait");

            const auto& type = this->m_ivars.get_type(ty);
            TRACE_FUNCTION_F("trait = " << trait << params << ", type = " << type);

            if (trait == m_lang_Sized) {
                auto cmp = type_is_sized(sp, type);
                if (cmp != ::HIR::Compare::Unequal) {
                    return callback(ImplRef(&type, &null_params, &null_assoc), cmp);
                } else {
                    return false;
                }
            }

            if (trait == m_lang_Copy) {
                auto cmp = this->type_is_copy(sp, type);
                if (cmp != ::HIR::Compare::Unequal) {
                    return callback(ImplRef(&type, &null_params, &null_assoc), cmp);
                } else {
                    return false;
                }
            }

            if (!lang_FnPtr.components().empty() && trait == lang_FnPtr) {
                if (type->is_Function()) {
                    return callback(ImplRef(&type, &null_params, &null_assoc), HIR::Compare::Equal);
                }
            }

            if (trait == m_lang_Clone) {
                auto cmp = this->type_is_clone(sp, type);
                if (cmp != ::HIR::Compare::Unequal) {
                    return callback(ImplRef(&type, &null_params, &null_assoc), cmp);
                } else {
                    return false;
                }
            }

            // - `DiscriminantKind`
            if (!m_lang_DiscriminantKind.components().empty() && trait == m_lang_DiscriminantKind) {
                static auto name_Discriminant = RcString::new_interned("Discriminant");
                // TODO: This logic is near identical to the logic in `static.cpp` - can it be de-duplicated?

                if (type->is_Infer() || (type->is_Path() && type->as_Path().binding.is_Unbound())) {
                    // TODO: How to prevent EAT from expanding (or setting opaque) too early?
                    return callback(ImplRef(type, HIR::PathParams(), ::HIR::TraitPath::assoc_list_t()), ::HIR::Compare::Fuzzy);
                } else if (type->is_Generic() || (type->is_Path() && type->as_Path().binding.is_Opaque())) {
                    ::HIR::TraitPath::assoc_list_t assoc_list;
                    assoc_list.insert(std::make_pair(name_Discriminant, HIR::TraitPath::AtyEqual{trait, {}, m_crate.m_types.path(HIR::Path(type, trait.clone(), name_Discriminant), HIR::TypePathBinding::make_Opaque({}))}));
                    return callback(ImplRef(type, HIR::PathParams(), ::HIR::TraitPath::assoc_list_t()), ::HIR::Compare::Equal);
                    //return false;
                } else if (type->is_Path() && type->as_Path().binding.is_Enum()) {
                    const auto& enm = *type->as_Path().binding.as_Enum();
                    HIR::TypeRef tag_ty = m_crate.m_types.primitive(enm.get_repr_type(enm.m_tag_repr));
                    ::HIR::TraitPath::assoc_list_t assoc_list;
                    assoc_list.insert(std::make_pair(name_Discriminant, HIR::TraitPath::AtyEqual{trait, {}, std::move(tag_ty)}));
                    return callback(ImplRef(type, {}, std::move(assoc_list)), ::HIR::Compare::Equal);
                } else {
                    ::HIR::TraitPath::assoc_list_t assoc_list;
                    assoc_list.insert(std::make_pair(name_Discriminant, HIR::TraitPath::AtyEqual{trait, {}, m_crate.m_types.primitive(HIR::CoreType::U8)}));
                    return callback(ImplRef(type, {}, std::move(assoc_list)), ::HIR::Compare::Equal);
                }
            }
            if (!m_lang_Pointee.components().empty() && trait == m_lang_Pointee) {
                static auto name_Metadata = RcString::new_interned("Metadata");
                // TODO: This logic is near identical to the logic in `static.cpp` - can it be de-duplicated?

                HIR::TypeRef meta_ty = m_crate.m_types.infer();
                bool has_meta_ty = false;
                if (type->is_Infer() || (type->is_Path() && type->as_Path().binding.is_Unbound())) {
                    return callback(ImplRef(type, HIR::PathParams(), ::HIR::TraitPath::assoc_list_t()), ::HIR::Compare::Fuzzy);
                }
                // Generics (or opaque ATYs)
                else if (type->is_Generic() || (type->is_Path() && type->as_Path().binding.is_Opaque())) {
                    // If the type is `Sized` return `()` as the type
                    if (type_is_sized(sp, type) != HIR::Compare::Unequal) {
                        meta_ty = m_crate.m_types.unit();
                        has_meta_ty = true;
                    } else {
                        // Return unbounded
                        // - leave as `_`
                    }
                }
                // Trait object: `Metadata=DynMetadata<T>`
                else if (type->is_TraitObject()) {
                    meta_ty = m_crate.m_types.path(::HIR::Path(::HIR::GenericPath(this->m_crate.get_lang_item_path(sp, "dyn_metadata"), HIR::PathParams(type))), HIR::TypePathBinding::make_Struct(&m_crate.get_struct_by_path(sp, this->m_crate.get_lang_item_path(sp, "dyn_metadata"))));
                    has_meta_ty = true;
                }
                // Slice and str
                else if (type->is_Slice() || TU_TEST1(*type, Primitive, == HIR::CoreType::Str)) {
                    meta_ty = m_crate.m_types.primitive(HIR::CoreType::Usize);
                    has_meta_ty = true;
                }
                // Structs: Can delegate their metadata
                else if (type->is_Path() && type->as_Path().binding.is_Struct()) {
                    const auto& str = *type->as_Path().binding.as_Struct();
                    switch (str.m_struct_markings.dst_type) {
                        case HIR::StructMarkings::DstType::None:
                            meta_ty = m_crate.m_types.unit();
                            has_meta_ty = true;
                            break;
                        case HIR::StructMarkings::DstType::Possible:
                        case HIR::StructMarkings::DstType::TraitObject: {
                            const ::HIR::TypeRef* tail_tpl = nullptr;
                            TU_MATCHA((str.m_data), (se),
                                (Unit, BUG(sp, "Unsized unit struct in Pointee lookup - " << type);),
                                (Tuple, ASSERT_BUG(sp, !se.empty(), "Unsized tuple struct without fields - " << type); tail_tpl = &se.back().ent;),
                                (Named, ASSERT_BUG(sp, !se.empty(), "Unsized struct without fields - " << type); tail_tpl = &se.back().ty;)
                            )
                            ASSERT_BUG(sp, tail_tpl, "Missing unsized tail field for " << type);

                            const auto& path = type->as_Path().path.m_data.as_Generic();
                            auto tail_ty = MonomorphStatePtr(m_crate.m_types, &type, &path.m_params, nullptr).monomorph_type(sp, *tail_tpl);
                            tail_ty = this->expand_associated_types(sp, std::move(tail_ty));

                            return this->find_trait_impls(sp, trait, params, tail_ty, [&](ImplRef impl, HIR::Compare cmp) {
                                ::HIR::TraitPath::assoc_list_t assoc;
                                auto metadata_ty = impl.get_type(m_crate.m_types, "Metadata", {});
                                if (metadata_ty) {
                                    assoc.insert(std::make_pair(name_Metadata, HIR::TraitPath::AtyEqual{trait, {}, std::move(metadata_ty)}));
                                }
                                return callback(ImplRef(type, params.clone(), std::move(assoc)), cmp);
                            });
                        }
                        case HIR::StructMarkings::DstType::Slice:
                            meta_ty = m_crate.m_types.primitive(HIR::CoreType::Usize);
                            has_meta_ty = true;
                            break;
                    }
                } else {
                    meta_ty = m_crate.m_types.unit();
                    has_meta_ty = true;
                }
                DEBUG("<" << type << " as Pointee>::Metadata = " << meta_ty);
                ::HIR::TraitPath::assoc_list_t assoc_list;
                if (has_meta_ty) {
                    assoc_list.insert(std::make_pair(RcString::new_interned("Metadata"), HIR::TraitPath::AtyEqual{trait, {}, mv$(meta_ty)}));
                }

                return callback(ImplRef(type, {}, std::move(assoc_list)), ::HIR::Compare::Equal);
            }
            // - `Tuple`
            if (!lang_Tuple.components().empty() && trait == lang_Tuple) {
                // Fuzzy impl for `_` and unbound ATYs
                if (type->is_Infer() || (type->is_Path() && type->as_Path().binding.is_Unbound())) {
                    return callback(ImplRef(type, HIR::PathParams(), ::HIR::TraitPath::assoc_list_t()), ::HIR::Compare::Fuzzy);
                }
                // Impl for tuples
                if (type->is_Tuple()) {
                    return callback(ImplRef(type, {}, ::HIR::TraitPath::assoc_list_t()), ::HIR::Compare::Equal);
                }
                // No impls for anything else
                return false;
            }

            // Magic Unsize impls to trait objects
            if (trait == m_lang_Unsize) {
                ASSERT_BUG(sp, params.m_types.size() == 1, "Unsize trait requires a single type param");
                const auto& dst_ty = this->m_ivars.get_type(params.m_types[0]);

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
                auto cmp = this->can_unsize(sp, dst_ty, type, cb);
                if (cmp == ::HIR::Compare::Equal) {
                    assert(!rv);
                    rv = callback(ImplRef(type, params.clone(), {}), ::HIR::Compare::Equal);
                }
                return rv;
            }

            // Magical CoerceUnsized impls for various types
            if (!lang_CoerceUnsized.components().empty() && trait == lang_CoerceUnsized) {
                if (find_trait_impls_bound(sp, trait, params, type, callback)) {
                    return true;
                }

                const auto& dst_ty = params.m_types.at(0);
                // - `*mut T => *const T`
                if (const auto* e = type->opt_Pointer()) {
                    if (const auto* de = dst_ty->opt_Pointer()) {
                        if (de->type < e->type) {
                            auto cmp = e->inner->compare_with_placeholders(sp, de->inner, this->m_ivars.callback_resolve_infer());
                            if (cmp != ::HIR::Compare::Unequal) {
                                ::HIR::PathParams pp;
                                pp.m_types.push_back(dst_ty);
                                if (callback(ImplRef(type, mv$(pp), {}), cmp)) {
                                    return true;
                                }
                            }
                        }
                    }
                }
            } else if (trait == m_lang_PointeeSized) {
                if (find_trait_impls_bound(sp, trait, params, type, callback)) {
                    return true;
                }
                // Lowest level of sizedness: This _might_ be sized (i.e. it's not an extern type?)
                return callback(ImplRef(type, {}, ::HIR::TraitPath::assoc_list_t()), ::HIR::Compare::Equal);
            } else if (trait == m_lang_MetaSized) {
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
                //    return found_cb( ImplRef(&null_hrls, &type, &null_params, &null_assoc), false );
                //}
            }

            if (trait == m_lang_Destruct) {
                // Inidicates that something is droppable
                // - Applies to everything?
                if (find_trait_impls_bound(sp, trait, params, type, callback)) {
                    return true;
                }
                // Lowest level of sizedness: This _might_ be sized (i.e. it's not an extern type?)
                return callback(ImplRef(type, {}, ::HIR::TraitPath::assoc_list_t()), ::HIR::Compare::Equal);
            }

            return false;
        }

        bool TraitResolution::find_trait_impls_types(const Span& sp, const ::HIR::SimplePath& trait, const ::HIR::PathParams& params, const ::HIR::TypeRef& type, t_cb_trait_impl_r callback) const {
    TU_MATCH_HDRA( (*type), {)
    default:
        break;
        TU_ARMA(NodeType, e) {
        TU_MATCH_HDRA((e), {)
        // Magic impls of the Fn* traits for closure types
        TU_ARMA(Closure, node_p) {
                    DEBUG("Closure, " << trait << " ?= Fn*");
                    if (trait == m_lang_Fn || trait == m_lang_FnMut || trait == m_lang_FnOnce) {
                        if (params.m_types.size() != 1) {
                            BUG(sp, "Fn* traits require a single tuple argument");
                        }
                        if (!params.m_types[0]->is_Tuple()) {
                            BUG(sp, "Fn* traits require a single tuple argument");
                        }

                        const auto& args_des = params.m_types[0]->as_Tuple();
                        if (args_des.size() != node_p->m_args.size()) {
                            return false;
                        }

                        auto cmp = ::HIR::Compare::Equal;
                        ::std::vector<::HIR::TypeRef> args;
                        for (unsigned int i = 0; i < node_p->m_args.size(); i++) {
                            const auto& at = node_p->m_args[i].second;
                            args.push_back(at);
                            DEBUG(at << " ?= " << args_des[i]);
                            cmp &= at->compare_with_placeholders(sp, args_des[i], this->m_ivars.callback_resolve_infer());
                        }
                        if (cmp != ::HIR::Compare::Unequal) {
                            // NOTE: This is a conditional "true", we know nothing about the move/mut-ness of this closure yet
                            // - Could we?
                            // - Not until after the first stage of typeck

                            DEBUG("Closure Fn* impl - cmp = " << cmp);

                            ::HIR::PathParams pp;
                            pp.m_types.push_back(m_crate.m_types.tuple(mv$(args)));
                            ::HIR::TraitPath::assoc_list_t types;
                            types.insert(::std::make_pair("Output", ::HIR::TraitPath::AtyEqual{::HIR::GenericPath(m_lang_FnOnce, pp.clone()), {}, node_p->m_return}));
                            return callback(ImplRef(type, mv$(pp), mv$(types)), cmp);
                        } else {
                            DEBUG("Closure Fn* impl - cmp = Compare::Unequal");
                            return false;
                        }
                    }
                }
                TU_ARMA(Generator, node_p) {
                    if (trait == m_lang_Generator) {
                        static const RcString rcstring_Yield = RcString::new_interned("Yield");
                        static const RcString rcstring_Return = RcString::new_interned("Return");
                        ::HIR::TraitPath::assoc_list_t assoc;
                        assoc.insert(::std::make_pair(rcstring_Yield, ::HIR::TraitPath::AtyEqual{trait.clone(), {}, node_p->m_yield_ty}));
                        assoc.insert(::std::make_pair(rcstring_Return, ::HIR::TraitPath::AtyEqual{trait.clone(), {}, node_p->m_return}));
                        HIR::PathParams params;
                        params.m_types.push_back(node_p->m_resume_ty);
                        return callback(ImplRef(type, mv$(params), mv$(assoc)), ::HIR::Compare::Equal);
                    }
                }
                TU_ARMA(Async, node_p) {
                    if (trait == m_lang_Future) {
                        static const RcString rcstring_Output = RcString::new_interned("Output");
                        ::HIR::TraitPath::assoc_list_t assoc;
                        assoc.insert(::std::make_pair(rcstring_Output, ::HIR::TraitPath::AtyEqual{trait.clone(), {}, node_p->m_code->m_res_type}));
                        return callback(ImplRef(type, {}, mv$(assoc)), ::HIR::Compare::Equal);
                    }
                }
        }
        }
        // Magic Fn* trait impls for function pointers
        TU_ARMA(Function, e) {
            if (trait == m_lang_Fn || trait == m_lang_FnMut || trait == m_lang_FnOnce) {
                DEBUG("Fn* trait for fn pointer");
                if (params.m_types.size() != 1) {
                    BUG(sp, "Fn* traits require a single tuple argument");
                }
                if (!params.m_types[0]->is_Tuple()) {
                    BUG(sp, "Fn* traits require a single tuple argument");
                }
                const auto& args_des = params.m_types[0]->as_Tuple();
                if (args_des.size() != e.m_arg_types.size()) {
                    return false;
                }

                // NOTE: unsafe or non-rust ABI functions aren't valid
                if (e.m_abi != ABI_RUST || e.is_unsafe) {
                    DEBUG("- No magic impl, wrong ABI or unsafe in " << type);
                    return false;
                }
                DEBUG("- Magic impl of Fn* for " << type);

                auto cmp = ::HIR::Compare::Equal;
                ::std::vector<::HIR::TypeRef> args;
                for (unsigned int i = 0; i < e.m_arg_types.size(); i++) {
                    const auto& at = e.m_arg_types[i];
                    args.push_back(at);
                    cmp &= at->compare_with_placeholders(sp, args_des[i], this->m_ivars.callback_resolve_infer());
                }

                ::HIR::PathParams pp;
                pp.m_types.push_back(m_crate.m_types.tuple(mv$(args)));
                ::HIR::TraitPath::assoc_list_t types;
                types.insert(::std::make_pair("Output", ::HIR::TraitPath::AtyEqual{::HIR::GenericPath(m_lang_FnOnce, pp.clone()), {}, e.m_rettype}));
                auto hrls = get_hrls(m_crate.m_types, sp, e.hrls, pp, params);
                return callback(ImplRef(std::move(hrls), type, mv$(pp), mv$(types)), cmp);
            }
        }
        // Magic Fn* trait impls for function pointers
        TU_ARMA(NamedFunction, real_e) {
            if (trait == m_lang_Fn || trait == m_lang_FnMut || trait == m_lang_FnOnce) {
                if (params.m_types.size() != 1) {
                    BUG(sp, "Fn* traits require a single tuple argument");
                }
                if (!params.m_types[0]->is_Tuple()) {
                    BUG(sp, "Fn* traits require a single tuple argument");
                }

                DEBUG("- Magic impl of Fn* for " << type);
                auto e = real_e.decay(m_crate.m_types, sp);
                DEBUG("> " << e.m_rettype << " - " << e.m_arg_types);
                const auto& args_des = params.m_types[0]->as_Tuple();
                if (args_des.size() != e.m_arg_types.size()) {
                    return false;
                }

                // NOTE: unsafe or non-rust ABI functions aren't valid
                if (e.m_abi != ABI_RUST) {
                    DEBUG("- No magic impl, wrong ABI (`" << e.m_abi << "`): " << type);
                    return false;
                }
                if (e.is_unsafe) {
                    DEBUG("- No magic impl, unsafe function: " << type);
                    return false;
                }
                DEBUG("- Magic impl of Fn* for " << type);

                auto cmp = ::HIR::Compare::Equal;
                ::std::vector<::HIR::TypeRef> args;
                for (unsigned int i = 0; i < e.m_arg_types.size(); i++) {
                    const auto& at = e.m_arg_types[i];
                    args.push_back(at);
                    cmp &= at->compare_with_placeholders(sp, args_des[i], this->m_ivars.callback_resolve_infer());
                }

                ::HIR::PathParams pp;
                pp.m_types.push_back(m_crate.m_types.tuple(mv$(args)));
                ::HIR::TraitPath::assoc_list_t types;
                types.insert(::std::make_pair("Output", ::HIR::TraitPath::AtyEqual{::HIR::GenericPath(m_lang_FnOnce, pp.clone()), {}, e.m_rettype}));
                auto hrls = get_hrls(m_crate.m_types, sp, e.hrls, pp, params);
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
            const ::HIR::TypeRef& ty,
            t_cb_trait_impl_r callback,
            bool magic_trait_impls /*=true*/,
            bool search_crate /*=true*/
        ) const {
            static ::HIR::PathParams null_params;
            static ::HIR::TraitPath::assoc_list_t null_assoc;

            const auto& type = this->m_ivars.get_type(ty);
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
            if (trait == e.m_trait.m_path.m_path) {
                auto cmp = compare_pp(sp, e.m_trait.m_path.m_params, params);
                if (cmp != ::HIR::Compare::Unequal) {
                    DEBUG("TraitObject impl params" << e.m_trait.m_path.m_params);
                    auto hrls = get_hrls(m_crate.m_types, sp, e.m_trait.m_hrtbs, e.m_trait.m_path.m_params, params);
                    return callback(ImplRef(std::move(hrls), &type, &e.m_trait.m_path.m_params, &e.m_trait.m_type_bounds), cmp);
                }
            }
            // Markers too
            for (const auto& mt : e.m_markers) {
                if (trait == mt.m_path) {
                    auto cmp = compare_pp(sp, mt.m_params, params);
                    if (cmp != ::HIR::Compare::Unequal) {
                        //auto hrls = get_hrls(sp, e.m_trait.m_hrtbs, e.m_trait.m_path.m_params, params);
                        return callback(ImplRef(HIR::PathParams(), &type, &mt.m_params, &null_assoc), cmp);
                    }
                }
            }

            if (e.m_trait.m_path.m_path != HIR::SimplePath()) {
                // - Check if the desired trait is a supertrait of this.
                // NOTE: `params` (aka des_params) is not used (TODO)
                bool rv = false;
                bool is_supertrait = false;
                this->find_named_trait_in_trait(sp, trait, params, *e.m_trait.m_trait_ptr, e.m_trait.m_path.m_path, e.m_trait.m_path.m_params, type, [&](const HIR::TraitPath& i_tp) {
                    // The above is just the monomorphised params and associated set. Comparison is still needed.
                    auto cmp = this->compare_pp(sp, i_tp.m_path.m_params, params);
                    if (cmp != ::HIR::Compare::Unequal) {
                        // Invoke callback with a proper ImplRef
                        ::HIR::TraitPath::assoc_list_t assoc_clone;
                        for (const auto& e : i_tp.m_type_bounds) {
                            assoc_clone.insert(::std::make_pair(e.first, e.second.clone()));
                        }
                        for (const auto& bound : e.m_trait.m_type_bounds) {
                            if (bound.second.source_trait.m_path == trait
                                && compare_pp(
                                    sp,
                                    bound.second.source_trait.m_params,
                                    i_tp.m_path.m_params
                                ) != ::HIR::Compare::Unequal) {
                                assoc_clone.erase(bound.first);
                                assoc_clone.insert(::std::make_pair(
                                    bound.first, bound.second.clone()
                                ));
                            }
                        }
                        ASSERT_BUG(sp, !e.m_trait.m_hrtbs || !i_tp.m_hrtbs, "TODO: Handle two layers of HRTBs - " << e.m_trait << " and " << i_tp);
                        auto hrls = get_hrls(m_crate.m_types, sp, e.m_trait.m_hrtbs, i_tp.m_path.m_params, params);
                        auto ir = ImplRef(std::move(hrls), type, i_tp.m_path.m_params.clone(), mv$(assoc_clone));
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
            for (const auto& trait_path : e.m_traits) {
                if (trait == trait_path.m_path.m_path) {
                    auto cmp = compare_pp(sp, trait_path.m_path.m_params, params);
                    if (cmp != ::HIR::Compare::Unequal) {
                        DEBUG("TraitObject impl params" << trait_path.m_path.m_params);
                        auto hrls = get_hrls(m_crate.m_types, sp, trait_path.m_hrtbs, trait_path.m_path.m_params, params);
                        return callback(ImplRef(std::move(hrls), &type, &trait_path.m_path.m_params, &trait_path.m_type_bounds), cmp);
                    }
                }

                // - Check if the desired trait is a supertrait of this.
                // NOTE: `params` (aka des_params) is not used (TODO)
                bool rv = false;
                bool is_supertrait = false;
                this->find_named_trait_in_trait(sp, trait, params, *trait_path.m_trait_ptr, trait_path.m_path.m_path, trait_path.m_path.m_params, type, [&](const HIR::TraitPath& i_tp) {
                    // The above is just the monomorphised params and associated set. Comparison is still needed.
                    auto cmp = this->compare_pp(sp, i_tp.m_path.m_params, params);
                    if (cmp != ::HIR::Compare::Unequal) {
                        // Invoke callback with a proper ImplRef
                        ::HIR::TraitPath::assoc_list_t assoc_clone;
                        for (const auto& e : i_tp.m_type_bounds) {
                            assoc_clone.insert(::std::make_pair(e.first, e.second.clone()));
                        }
                        // Existential equalities are stored on the principal
                        // bound even when the associated item is declared by
                        // a supertrait (e.g. `FnMut` carries `FnOnce::Output`).
                        // Project those equalities together with the
                        // supertrait candidate.
                        for (const auto& e : trait_path.m_type_bounds) {
                            if (e.second.source_trait.m_path == trait
                                && compare_pp(
                                    sp,
                                    e.second.source_trait.m_params,
                                    i_tp.m_path.m_params
                                ) != ::HIR::Compare::Unequal) {
                                assoc_clone.erase(e.first);
                                assoc_clone.insert(::std::make_pair(
                                    e.first, e.second.clone()
                                ));
                            }
                        }
                        ASSERT_BUG(sp, !trait_path.m_hrtbs || !i_tp.m_hrtbs, "TODO: Handle two layers of HRTBs - " << trait_path << " and " << i_tp);
                        auto hrls = trait_path.m_hrtbs ? get_hrls(m_crate.m_types, sp, trait_path.m_hrtbs, i_tp.m_path.m_params, params) : get_hrls(m_crate.m_types, sp, i_tp.m_hrtbs, i_tp.m_path.m_params, params);
                        auto ir = ImplRef(std::move(hrls), type, i_tp.m_path.m_params.clone(), mv$(assoc_clone));
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
                return callback(ImplRef(&type, &null_params, &null_assoc), ::HIR::Compare::Fuzzy);
            }
        } // TU_ARMA(Generic)
        // If this type is an opaque UfcsKnown - check bounds
        TU_ARMA(Path, e) {
            if (e.binding.is_Opaque()) {
                ASSERT_BUG(sp, e.path.m_data.is_UfcsKnown(), "Opaque bound type wasn't UfcsKnown - " << type);
                const auto& pe = e.path.m_data.as_UfcsKnown();

                // TODO: Should Self here be `type` or `pe.type`
                // - Depends... if implicit it should be `type` (as it relates to the associated type), but if explicit it's referring to the trait
                auto monomorph_cb = MonomorphStatePtr(m_crate.m_types, &pe.type, &pe.trait.m_params, &pe.params);
                auto rv = this->iterate_aty_bounds(sp, pe, [&](const HIR::TraitPath& bound) {
                    DEBUG("Bound on ATY: " << bound);
                    static const HIR::GenericParams empty_params;
                    const auto& hrls_def = (bound.m_hrtbs && !bound.m_hrtbs->is_empty()) ? *bound.m_hrtbs : empty_params;
                    auto pp_hrb = hrls_def.make_empty_params(true);
                    monomorph_cb.pp_hrb = &pp_hrb;
                    const auto& b_params = bound.m_path.m_params;
                    ::HIR::PathParams params_mono_o;
                    const ::HIR::PathParams* b_params_mono = &b_params;
                    if (monomorphise_pathparams_needed(b_params)) {
                        params_mono_o = monomorph_cb.monomorph_path_params(sp, b_params, false);
                        b_params_mono = &params_mono_o;
                    }
                    const bool params_need_normalisation = ::std::any_of(
                        b_params_mono->m_types.begin(), b_params_mono->m_types.end(),
                        [&](const auto& ty) { return this->has_associated_type(ty); }
                    );
                    if (params_need_normalisation) {
                        if (b_params_mono != &params_mono_o) {
                            params_mono_o = b_params.clone();
                            b_params_mono = &params_mono_o;
                        }
                        this->expand_associated_types_params(sp, params_mono_o);
                    }

                    ::HIR::TraitPath::assoc_list_t b_atys;
                    for (const auto& aty : bound.m_type_bounds) {
                        b_atys.insert(::std::make_pair(aty.first, ::HIR::TraitPath::AtyEqual{monomorph_cb.monomorph_genericpath(sp, aty.second.source_trait, false), {}, monomorph_cb.monomorph_type(sp, aty.second.type)}));
                    }

                    if (bound.m_path.m_path == trait) {
                        auto cmp = this->compare_pp(sp, *b_params_mono, params);
                        if (cmp != ::HIR::Compare::Unequal) {
                            if (b_params_mono == &params_mono_o) {
                                // TODO: assoc bounds
                                if (callback(ImplRef(type, mv$(params_mono_o), mv$(b_atys)), cmp)) {
                                    return true;
                                }
                                params_mono_o = monomorph_cb.monomorph_path_params(sp, b_params, false);
                                if (params_need_normalisation) {
                                    this->expand_associated_types_params(sp, params_mono_o);
                                }
                            } else if (!b_atys.empty()) {
                                if (callback(ImplRef(type, b_params_mono->clone(), mv$(b_atys)), cmp)) {
                                    return true;
                                }
                            } else {
                                auto hrls = get_hrls(m_crate.m_types, sp, bound.m_hrtbs, bound.m_path.m_params, params);
                                if (callback(ImplRef(std::move(hrls), &type, &bound.m_path.m_params, &null_assoc), cmp)) {
                                    return true;
                                }
                            }
                        }
                    }
                    monomorph_cb.pp_hrb = nullptr;

                    bool rv = false;
                    bool ret = false;
                    this->find_named_trait_in_trait(sp, trait, params, *bound.m_trait_ptr, bound.m_path.m_path, *b_params_mono, type, [&](const HIR::TraitPath& i_tp) {
                        auto cmp = this->compare_pp(sp, i_tp.m_path.m_params, params);
                        DEBUG("Opaque Path: cmp=" << cmp << ", impl " << i_tp.m_path << " for " << type << " -- desired " << trait << params);
                        ASSERT_BUG(sp, !bound.m_hrtbs || !i_tp.m_hrtbs, "TODO: Handle two layers of HRTBs - " << bound.m_path << " and " << i_tp);
                        const HIR::GenericParams* hrtbs = bound.m_hrtbs ? bound.m_hrtbs.get() : i_tp.m_hrtbs.get();
                        auto hrls = get_hrls(m_crate.m_types, sp, hrtbs, i_tp.m_path.m_params, params);
                        auto ir = ImplRef(std::move(hrls), type, i_tp.m_path.m_params.clone(), {});
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
    if( find_trait_impls_bound(sp, trait, params, type, callback) )
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
            bool auto_builtin;
            bool ambiguity_beyond_head = false;
            bool discarded = false;

            Candidate(
                ImplRef impl,
                ::HIR::Compare head_match,
                const ::HIR::MarkerImpl* marker_impl,
                ::HIR::PathParams marker_impl_params,
                bool auto_builtin
            )
                : impl(::std::move(impl))
                , head_match(head_match)
                , certainty(Certainty::Ambiguous)
                , marker_impl(marker_impl)
                , marker_impl_params(::std::move(marker_impl_params))
                , auto_builtin(auto_builtin)
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
            size_t available_depth = 0;
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
                available_depth = 0;
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
            ::HIR::TraitPath::assoc_list_t associated;

            GoalKey(
                size_t hash,
                const ::HIR::SimplePath& trait,
                const ::HIR::PathParams& params,
                const ::HIR::TypeRef& type,
                const ::HIR::TraitPath::assoc_list_t* associated
            )
                : hash(hash)
                , trait(trait)
                , params(params.clone())
                , type(type)
                , associated(clone_associated(associated))
            {
            }
        };

        struct CachedGoal {
            GoalKey goal;
            Certainty certainty;

            CachedGoal(
                size_t hash,
                const ::HIR::SimplePath& trait,
                const ::HIR::PathParams& params,
                const ::HIR::TypeRef& type,
                const ::HIR::TraitPath::assoc_list_t* associated,
                Certainty certainty
            )
                : goal(hash, trait, params, type, associated)
                , certainty(certainty)
            {
            }
        };

        const TraitResolution& m_resolve;
        const ::HIR::Crate& m_crate;
        const Span* m_span = nullptr;
        bool m_coherence_mode = false;

        // Frames and candidates have stable pool-backed addresses.  Vectors
        // are pointer indexes only, so recursive growth never moves an ImplRef
        // or invalidates a parent candidate.
        stl::ObjList<Candidate> m_candidate_nodes;
        ::std::vector<CandidateFrame*> m_frames;
        size_t m_frame_depth = 0;
        stl::ObjList<GoalKey> m_active_goal_nodes;
        stl::ObjList<CachedGoal> m_cached_goal_nodes;
        ::std::vector<GoalKey*> m_goal_stack;
        ::std::vector<CachedGoal*> m_goal_cache;
        ::std::unordered_multimap<size_t, GoalKey*> m_active_goal_index;
        ::std::unordered_multimap<size_t, CachedGoal*> m_goal_cache_index;

        struct CanonicalGoal {
            ::HIR::PathParams params;
            ::HIR::TypeRef type;
            ::HIR::TraitPath::assoc_list_t associated;

            CanonicalGoal(::HIR::PathParams params, ::HIR::TypeRef type)
                : params(::std::move(params))
                , type(type)
            {
            }
        };

        const Span& span() const {
            ASSERT_BUG(Span(), m_span, "next-solver session used outside an evaluation");
            return *m_span;
        }

        CanonicalGoal canonicalize_goal(
            const ::HIR::PathParams& params,
            const ::HIR::TypeRef& type,
            const ::HIR::TraitPath::assoc_list_t* associated
        ) const {
            CanonicalizeTraitGoal canonicalizer(m_crate.m_types);
            auto canonical_params = canonicalizer.monomorph_path_params(
                span(), params, true
            );
            const auto canonical_type = canonicalizer.monomorph_type(
                span(), type, true
            );
            CanonicalGoal result(
                ::std::move(canonical_params), canonical_type
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
                                span(), entry.second.aty_params, true
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

        ::std::optional<size_t> available_depth_for_nested() {
            if (m_frame_depth == 0) {
                return ROOT_DEPTH;
            }
            auto& parent = *m_frames[m_frame_depth - 1];
            if (parent.available_depth == 0) {
                parent.encountered_overflow = true;
                return {};
            }
            return parent.encountered_overflow
                ? parent.available_depth / OVERFLOW_DEPTH_DIVISOR
                : parent.available_depth - 1;
        }

        static bool is_environment_or_builtin(const ImplRef& impl) {
            return !impl.m_data.is_TraitImpl();
        }

        bool params_have_unknown_types(const ::HIR::PathParams& params) const {
            for (const auto& type : params.m_types) {
                if (type_has_unknown(type)) {
                    return true;
                }
            }
            return false;
        }

        bool path_has_unknown_types(const ::HIR::Path& path) const {
            if (const auto* pe = path.m_data.opt_Generic()) {
                return params_have_unknown_types(pe->m_params);
            }
            if (const auto* pe = path.m_data.opt_UfcsInherent()) {
                return type_has_unknown(pe->type)
                    || params_have_unknown_types(pe->params)
                    || params_have_unknown_types(pe->impl_params);
            }
            if (const auto* pe = path.m_data.opt_UfcsKnown()) {
                return type_has_unknown(pe->type)
                    || params_have_unknown_types(pe->trait.m_params)
                    || params_have_unknown_types(pe->params);
            }
            const auto& pe = path.m_data.as_UfcsUnknown();
            return type_has_unknown(pe.type)
                || params_have_unknown_types(pe.params);
        }

        bool trait_path_has_unknown_types(const ::HIR::TraitPath& trait) const {
            if (params_have_unknown_types(trait.m_path.m_params)) {
                return true;
            }
            for (const auto& assoc : trait.m_type_bounds) {
                if (params_have_unknown_types(assoc.second.source_trait.m_params)
                    || params_have_unknown_types(assoc.second.aty_params)
                    || type_has_unknown(assoc.second.type)) {
                    return true;
                }
            }
            for (const auto& assoc : trait.m_trait_bounds) {
                if (params_have_unknown_types(assoc.second.source_trait.m_params)
                    || params_have_unknown_types(assoc.second.aty_params)) {
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

        bool type_has_unknown(const ::HIR::TypeRef& input) const {
            const auto& type = m_resolve.resolve_type(input);
            if (type->is_Infer() || type->is_Generic()) {
                return true;
            }
            if (const auto* path = type->opt_Path()) {
                return path_has_unknown_types(path->path);
            }
            if (const auto* object = type->opt_TraitObject()) {
                if (trait_path_has_unknown_types(object->m_trait)) {
                    return true;
                }
                for (const auto& marker : object->m_markers) {
                    if (params_have_unknown_types(marker.m_params)) {
                        return true;
                    }
                }
                return false;
            }
            if (const auto* erased = type->opt_ErasedType()) {
                for (const auto& trait : erased->m_traits) {
                    if (trait_path_has_unknown_types(trait)) {
                        return true;
                    }
                }
                if (const auto* known = erased->m_inner.opt_Known()) {
                    return type_has_unknown(*known);
                }
                if (const auto* alias = erased->m_inner.opt_Alias()) {
                    return params_have_unknown_types(alias->params);
                }
                if (const auto* fcn = erased->m_inner.opt_Fcn()) {
                    return path_has_unknown_types(fcn->m_origin);
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
                for (const auto& arg : fcn->m_arg_types) {
                    if (type_has_unknown(arg)) {
                        return true;
                    }
                }
                return type_has_unknown(fcn->m_rettype);
            }
            return false;
        }

        static bool type_has_candidate_placeholder(
            const ::HIR::TypeRef& type
        ) {
            bool found = false;
            visit_ty_with(type, [&](const ::HIR::TypeRef& inner) {
                if (const auto* generic = inner->opt_Generic()) {
                    found |= generic->group() == ::HIR::GENERIC_Placeholder;
                }
                return found;
            });
            return found;
        }

        static bool params_have_candidate_placeholders(
            const ::HIR::PathParams& params
        ) {
            for (const auto& type : params.m_types) {
                if (type_has_candidate_placeholder(type)) {
                    return true;
                }
            }
            for (const auto& value : params.m_values) {
                if (value.is_Generic()
                    && value.as_Generic().group() == ::HIR::GENERIC_Placeholder) {
                    return true;
                }
            }
            return false;
        }

        bool candidate_has_placeholders(const Candidate& candidate) const {
            if (type_has_candidate_placeholder(candidate.impl.get_impl_type(m_crate.m_types))
                || params_have_candidate_placeholders(
                    candidate.impl.get_trait_params(m_crate.m_types)
                )) {
                return true;
            }
            if (const auto* trait_impl = candidate.impl.m_data.opt_TraitImpl()) {
                if (params_have_candidate_placeholders(trait_impl->impl_params)) {
                    return true;
                }
            }
            return params_have_candidate_placeholders(
                candidate.marker_impl_params
            );
        }

        OrphanVisit orphan_visit_resolved_type(
            const ::HIR::TypeRef& type,
            OrphanPerspective perspective
        ) const {
            if (type->is_Infer() || type->is_Generic()) {
                return perspective == OrphanPerspective::Remote
                    ? OrphanVisit::LocalKey
                    : OrphanVisit::Uncovered;
            }

            if (const auto* path = type->opt_Path()) {
                const auto* generic = path->path.m_data.opt_Generic();
                const bool concrete_adt = generic
                    && (path->binding.is_Struct()
                        || path->binding.is_Enum()
                        || path->binding.is_Union()
                        || path->binding.is_ExternType());
                if (!concrete_adt) {
                    if (type_has_unknown(type)) {
                        return perspective == OrphanPerspective::Remote
                            ? OrphanVisit::LocalKey
                            : OrphanVisit::Uncovered;
                    }
                    return OrphanVisit::NonLocal;
                }

                const bool local = perspective == OrphanPerspective::Local
                    && generic->m_path.crate_name() == m_crate.m_crate_name;
                if (local) {
                    return OrphanVisit::LocalKey;
                }

                const auto* str_ptr = path->binding.opt_Struct();
                if (str_ptr && (*str_ptr)->m_struct_markings.is_fundamental) {
                    for (const auto& param : generic->m_params.m_types) {
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
                const auto& principal = object->m_trait.m_path.m_path;
                if (perspective == OrphanPerspective::Local
                    && principal != ::HIR::SimplePath()
                    && principal.crate_name() == m_crate.m_crate_name) {
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
            const ::HIR::TypeRef& input,
            OrphanPerspective perspective
        ) const {
            const auto& resolved = m_resolve.resolve_type(input);
            const auto* path = resolved->opt_Path();
            const bool is_alias = path
                && (!path->path.m_data.is_Generic()
                    || path->binding.is_Unbound()
                    || path->binding.is_Opaque());
            if (is_alias) {
                // rustc's orphan checker normalizes aliases lazily.  Keep a
                // rigid alias if normalization only produces a fresh type
                // variable; such an alias still carries coverage information.
                auto normalized = m_resolve.expand_associated_types(span(), resolved);
                if (!(normalized->is_Infer() && !resolved->is_Infer())) {
                    return orphan_visit_resolved_type(normalized, perspective);
                }
            }
            return orphan_visit_resolved_type(resolved, perspective);
        }

        bool orphan_check_trait_ref(
            const ::HIR::PathParams& params,
            const ::HIR::TypeRef& type,
            OrphanPerspective perspective
        ) const {
            const auto self_result = orphan_visit_type(type, perspective);
            if (self_result != OrphanVisit::NonLocal) {
                return self_result == OrphanVisit::LocalKey;
            }
            for (const auto& param : params.m_types) {
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
            const ::HIR::TypeRef& type
        ) const {
            if (orphan_check_trait_ref(
                    params, type, OrphanPerspective::Remote
                )) {
                return false;
            }

            const auto& trait_def = m_crate.get_trait_by_path(span(), trait);
            if (trait.crate_name() == m_crate.m_crate_name
                || trait_def.m_is_fundamental) {
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

        static size_t hash_type(const ::HIR::TypeRef& type) {
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
                return hash_mix(0xa0, hash_simple_path(trait_object->m_trait.m_path.m_path));
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
            const ::HIR::TypeRef& type,
            const ::HIR::TraitPath::assoc_list_t* associated
        ) {
            size_t result = hash_simple_path(trait);
            result = hash_mix(result, params.m_types.size());
            for (const auto& param : params.m_types) {
                result = hash_mix(result, hash_type(param));
            }
            result = hash_mix(result, params.m_values.size());
            result = hash_mix(result, hash_type(type));
            if (associated && !associated->empty()) {
                result = hash_mix(result, associated->size());
                for (const auto& entry : *associated) {
                    result = hash_mix(result, ::std::hash<RcString>()(entry.first));
                    result = hash_mix(result, hash_simple_path(entry.second.source_trait.m_path));
                    result = hash_mix(result, hash_type(entry.second.type));
                }
            }
            return result;
        }

        static ::HIR::TraitPath::assoc_list_t clone_associated(
            const ::HIR::TraitPath::assoc_list_t* associated
        ) {
            ::HIR::TraitPath::assoc_list_t result;
            if (associated) {
                for (const auto& entry : *associated) {
                    result.insert({entry.first, entry.second.clone()});
                }
            }
            return result;
        }

        static bool goal_matches(
            const GoalKey& goal,
            const ::HIR::SimplePath& trait,
            const ::HIR::PathParams& params,
            const ::HIR::TypeRef& type,
            const ::HIR::TraitPath::assoc_list_t* associated
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
                    || left->second.aty_params != right->second.aty_params
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
            const ::HIR::TypeRef& type,
            const ::HIR::TraitPath::assoc_list_t* associated
        ) const {
            const auto range = m_goal_cache_index.equal_range(hash);
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
            const ::HIR::TypeRef& type,
            const ::HIR::TraitPath::assoc_list_t* associated
        ) const {
            const auto range = m_active_goal_index.equal_range(hash);
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
            const ::HIR::TypeRef& type,
            const ::HIR::TraitPath::assoc_list_t* associated
        ) {
            auto* goal = m_active_goal_nodes.make(hash, trait, params, type, associated);
            m_goal_stack.push_back(goal);
            m_active_goal_index.emplace(hash, goal);
            return goal;
        }

        void pop_active_goal(GoalKey* goal) {
            assert(!m_goal_stack.empty() && m_goal_stack.back() == goal);
            const auto range = m_active_goal_index.equal_range(goal->hash);
            for (auto it = range.first; it != range.second; ++it) {
                if (it->second == goal) {
                    m_active_goal_index.erase(it);
                    m_goal_stack.pop_back();
                    m_active_goal_nodes.release(goal);
                    return;
                }
            }
            assert(!"next-solver active goal missing from hash index");
            ::std::abort();
        }

        Certainty cache_goal(
            size_t hash,
            const ::HIR::SimplePath& trait,
            const ::HIR::PathParams& params,
            const ::HIR::TypeRef& type,
            const ::HIR::TraitPath::assoc_list_t* associated,
            Certainty certainty
        ) {
            auto* goal = m_cached_goal_nodes.make(
                hash, trait, params, type, associated, certainty
            );
            m_goal_cache.push_back(goal);
            m_goal_cache_index.emplace(hash, goal);
            return certainty;
        }

        void clear_goal_cache() {
            m_goal_cache_index.clear();
            for (auto* goal : m_goal_cache) {
                m_cached_goal_nodes.release(goal);
            }
            m_goal_cache.clear();
        }

        bool is_same_impl(const ImplRef& left, const ImplRef& right) const {
            const auto* li = left.m_data.opt_TraitImpl();
            const auto* ri = right.m_data.opt_TraitImpl();
            if (li || ri) {
                return li && ri && li->impl == ri->impl && li->impl_params == ri->impl_params;
            }
            return left.get_impl_type(m_crate.m_types) == right.get_impl_type(m_crate.m_types)
                && left.get_trait_params(m_crate.m_types) == right.get_trait_params(m_crate.m_types);
        }

        void push_candidate(
            size_t frame_index,
            ImplRef impl,
            ::HIR::Compare match,
            const ::HIR::MarkerImpl* marker_impl = nullptr,
            ::HIR::PathParams marker_impl_params = {},
            bool auto_builtin = false
        ) {
            if (match == ::HIR::Compare::Unequal) {
                return;
            }
            auto& candidates = m_frames[frame_index]->candidates;
            for (size_t i = 0; i < candidates.size(); i++) {
                const bool same_source = candidates[i]->marker_impl == marker_impl
                    && candidates[i]->auto_builtin == auto_builtin;
                const bool same = marker_impl
                    ? same_source
                        && candidates[i]->marker_impl_params == marker_impl_params
                    : same_source && is_same_impl(candidates[i]->impl, impl);
                if (same) {
                    candidates[i]->head_match &= match;
                    return;
                }
            }
            candidates.push_back(m_candidate_nodes.make(
                ::std::move(impl),
                match,
                marker_impl,
                ::std::move(marker_impl_params),
                auto_builtin
            ));
        }

        void assemble_candidates(
            size_t frame_index,
            const ::HIR::SimplePath& trait,
            const ::HIR::PathParams& params,
            const ::HIR::TypeRef& type
        ) {
            auto collect = [&](ImplRef impl, ::HIR::Compare match) {
                push_candidate(
                    frame_index, ::std::move(impl), match,
                    nullptr, {}, false
                );
                return false;
            };

            // Builtin, type-derived, and ParamEnv candidates do not have impl
            // where-clauses.  Keep their established construction logic, but
            // never enter the crate impl selector from this assembly pass.
            m_resolve.find_trait_impls_legacy(
                span(), trait, params, type, collect, true, false
            );

            // The old lookup intentionally stops at a trait-object/erased
            // builtin candidate.  Candidate assembly in the next solver must
            // still include environment candidates, notably the
            // blanket `Any` impl that overlaps the builtin dyn-object proof.
            const auto& resolved_type = m_resolve.resolve_type(type);
            if (resolved_type->is_TraitObject() || resolved_type->is_ErasedType()) {
                m_resolve.find_trait_impls_bound(span(), trait, params, resolved_type, collect);
            }

            const auto& trait_def = m_crate.get_trait_by_path(span(), trait);
            if (!trait_def.m_is_marker) {
                // Assemble impl heads without evaluating their where-clauses.
                // Those nested goals belong exclusively to evaluate_candidate.
                m_crate.find_trait_impls(
                    trait,
                    resolved_type,
                    m_resolve.m_ivars.callback_resolve_infer(),
                    [&](const ::HIR::TraitImpl& impl) {
                        ::HIR::PathParams impl_params;
                        const auto match = m_resolve.ftic_check_params(
                            span(),
                            trait,
                            &params,
                            resolved_type,
                            impl.m_params,
                            impl.m_trait_args,
                            impl.m_type,
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
                                false
                            );
                        }
                        return false;
                    }
                );
            } else {
                // Explicit positive and negative auto-trait impls are
                // candidates with polarity.  Only their heads are matched
                // here; their bounds are nested goals evaluated below.
                m_crate.find_auto_trait_impls(
                    trait,
                    resolved_type,
                    m_resolve.m_ivars.callback_resolve_infer(),
                    [&](const ::HIR::MarkerImpl& impl) {
                        ::HIR::PathParams impl_params;
                        const auto match = m_resolve.ftic_check_params(
                            span(),
                            trait,
                            &params,
                            resolved_type,
                            impl.m_params,
                            impl.m_trait_args,
                            impl.m_type,
                            impl_params,
                            false
                        );
                        if (match != ::HIR::Compare::Unequal) {
                            auto monomorph = MonomorphStatePtr(
                                m_crate.m_types, nullptr, &impl_params, nullptr
                            );
                            auto response_type = monomorph.monomorph_type(
                                span(), impl.m_type, false
                            );
                            auto response_params = monomorph.monomorph_path_params(
                                span(), impl.m_trait_args, false
                            );
                            push_candidate(
                                frame_index,
                                ImplRef(
                                    ::std::move(response_type),
                                    ::std::move(response_params),
                                    ::HIR::TraitPath::assoc_list_t()
                                ),
                                match,
                                &impl,
                                ::std::move(impl_params),
                                false
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
                        ::HIR::TraitPath::assoc_list_t()
                    ),
                    m_resolve.type_contains_ivars(resolved_type)
                        || m_resolve.params_contain_ivars(params)
                        ? ::HIR::Compare::Fuzzy
                        : ::HIR::Compare::Equal,
                    nullptr,
                    {},
                    true
                );
            }
        }

        ::HIR::TypeRef make_associated_projection(
            const ::HIR::TypeRef& type,
            const ::HIR::GenericPath& source_trait,
            const RcString& name,
            const ::HIR::PathParams& associated_params
        ) const {
            return m_crate.m_types.path(
                ::HIR::Path(
                    type,
                    source_trait.clone(),
                    name,
                    associated_params.clone()
                ),
                ::HIR::TypePathBinding::make_Opaque({})
            );
        }

        ::HIR::TypeRef make_associated_projection(
            const ImplRef& impl,
            const ::HIR::GenericPath& source_trait,
            const RcString& name,
            const ::HIR::PathParams& associated_params
        ) const {
            return make_associated_projection(
                impl.get_impl_type(m_crate.m_types), source_trait, name, associated_params
            );
        }

        bool bind_candidate_placeholders(
            Candidate& candidate,
            const ::HIR::TypeRef& nested_type,
            const ::HIR::TraitPath::assoc_list_t& associated,
            bool use_candidate_response = false
        ) {
            ::HIR::PathParams* candidate_params = nullptr;
            if (auto* trait_impl = candidate.impl.m_data.opt_TraitImpl()) {
                candidate_params = &trait_impl->impl_params;
            } else if (candidate.marker_impl) {
                candidate_params = &candidate.marker_impl_params;
            }
            if (!candidate_params || associated.empty()) {
                return false;
            }

            class BindPlaceholders final: public ::HIR::MatchGenerics {
                const Span& m_span;
                ::HIR::TypeInterner& m_types;
                ::HIR::PathParams& m_params;

            public:
                bool changed = false;

                BindPlaceholders(
                    const Span& span,
                    ::HIR::TypeInterner& types,
                    ::HIR::PathParams& params
                )
                    : m_span(span)
                    , m_types(types)
                    , m_params(params)
                {
                }

                ::HIR::Compare match_ty(
                    const ::HIR::GenericRef& generic,
                    const ::HIR::TypeRef& type,
                    ::HIR::t_cb_resolve_type resolve
                ) override {
                    if (const auto* other = type->opt_Generic()) {
                        if (*other == generic) {
                            return ::HIR::Compare::Equal;
                        }
                    }
                    if (generic.group() == ::HIR::GENERIC_Placeholder) {
                        for (auto& parameter : m_params.m_types) {
                            const auto* current = parameter->opt_Generic();
                            if (current && *current == generic) {
                                parameter = type;
                                changed = true;
                                return ::HIR::Compare::Equal;
                            }
                        }
                    }
                    return m_types.generic(generic.name, generic.binding)->compare_with_placeholders(
                        m_span, type, resolve
                    );
                }

                ::HIR::Compare match_val(
                    const ::HIR::GenericRef& generic,
                    const ::HIR::ConstGeneric& value
                ) override {
                    if (value.is_Generic() && value.as_Generic() == generic) {
                        return ::HIR::Compare::Equal;
                    }
                    if (generic.group() == ::HIR::GENERIC_Placeholder) {
                        for (auto& parameter : m_params.m_values) {
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
            } binder{span(), m_crate.m_types, *candidate_params};

            for (const auto& requirement : associated) {
                const auto saved = candidate_params->clone();
                auto candidate_output = use_candidate_response
                    ? candidate.impl.get_type(
                        m_crate.m_types, requirement.first.c_str(), requirement.second.aty_params
                    )
                    : ::HIR::TypeRef();
                if (!use_candidate_response) {
                    // An impl parameter can occur only in a nested projection
                    // equality (for example `I: Iterator<Item = &'a T>`).
                    // Ask the solver for that projection's actual response so
                    // `T` is bound to the response, not to the alias syntax.
                    evaluate(
                        span(),
                        requirement.second.source_trait.m_path,
                        requirement.second.source_trait.m_params,
                        nested_type,
                        [&](ImplRef impl, ::HIR::Compare certainty) {
                            if (certainty != ::HIR::Compare::Equal
                                || impl.is_ambiguous_identity()) {
                                return false;
                            }
                            auto output = impl.get_type(
                                m_crate.m_types,
                                requirement.first.c_str(),
                                requirement.second.aty_params
                            );
                            if (output == ::HIR::TypeRef()) {
                                return false;
                            }
                            candidate_output = ::std::move(output);
                            return true;
                        },
                        requirement.first.c_str(),
                        nullptr,
                        &requirement.second.aty_params
                    );
                }
                if (candidate_output == ::HIR::TypeRef()) {
                    candidate_output = make_associated_projection(
                        nested_type,
                        requirement.second.source_trait,
                        requirement.first,
                        requirement.second.aty_params
                    );
                }
                const auto match = (use_candidate_response
                    ? candidate_output
                    : requirement.second.type)->match_test_generics_fuzz(
                    span(),
                    use_candidate_response
                        ? requirement.second.type
                        : candidate_output,
                    m_resolve.m_ivars.callback_resolve_infer(),
                    binder
                );
                if (match == ::HIR::Compare::Unequal) {
                    *candidate_params = saved.clone();
                }
            }

            if (binder.changed && candidate.marker_impl) {
                auto monomorph = MonomorphStatePtr(
                    m_crate.m_types, nullptr, &candidate.marker_impl_params, nullptr
                );
                auto& response = candidate.impl.m_data.as_Bounded();
                response.type = monomorph.monomorph_type(
                    span(), candidate.marker_impl->m_type, false
                );
                response.trait_args = monomorph.monomorph_path_params(
                    span(), candidate.marker_impl->m_trait_args, false
                );
            }
            return binder.changed;
        }

        Certainty match_associated_types(
            const ::HIR::SimplePath& trait,
            const ImplRef& impl,
            const ::HIR::TraitPath::assoc_list_t* associated
        ) {
            if (!associated || associated->empty()) {
                return Certainty::Proven;
            }

            Certainty result = Certainty::Proven;
            for (const auto& requirement : *associated) {
                const auto& aty = requirement.second;
                if (!impl.m_data.is_TraitImpl() && aty.aty_params.has_params()) {
                    // Bounded candidates currently store non-GAT projections.
                    // They remain a valid but non-guiding response instead of
                    // being rejected or calling ImplRef's non-GAT assertion.
                    result = Certainty::Ambiguous;
                    continue;
                }
                auto output = impl.get_type(m_crate.m_types, requirement.first.c_str(), aty.aty_params);
                if (output == ::HIR::TypeRef()) {
                    if (aty.source_trait.m_path != trait) {
                        ::HIR::TraitPath::assoc_list_t source_associated;
                        source_associated.insert({
                            requirement.first,
                            requirement.second.clone()
                        });
                        const auto source_result = solve_goal(
                            aty.source_trait.m_path,
                            aty.source_trait.m_params,
                            impl.get_impl_type(m_crate.m_types),
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
                    if (impl.m_data.is_TraitImpl()) {
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
                        aty.aty_params
                    );
                }
                const auto cmp = m_resolve.compare_ty(span(), output, aty.type);
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
            const ::HIR::TypeRef& type
        ) {
            auto combine = [](Certainty& result, Certainty nested) {
                if (nested == Certainty::NoSolution) {
                    result = Certainty::NoSolution;
                } else if (nested == Certainty::Ambiguous
                           && result == Certainty::Proven) {
                    result = Certainty::Ambiguous;
                }
            };
            auto evaluate_inner = [&](const ::HIR::TypeRef& inner) {
                return solve_goal(trait, params, inner, nullptr);
            };

            TU_MATCH_HDRA((*type), {)
            default:
                return Certainty::Proven;
            TU_ARMA(Path, e) {
                if (const auto* pe = e.path.m_data.opt_Generic()) {
                    ::HIR::TypeRef tmp;
                    auto monomorph = MonomorphStatePtr(
                        m_crate.m_types, nullptr, &pe->m_params, nullptr
                    );
                    auto evaluate_field = [&](const ::HIR::TypeRef& field) {
                        const auto& field_type = monomorphise_type_needed(field)
                            ? (tmp = m_resolve.expand_associated_types(
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
                            (str.m_data),
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
                        if (const auto* variants = enm.m_data.opt_Data()) {
                            for (const auto& variant : *variants) {
                                combine(result, evaluate_field(variant.type));
                                if (result == Certainty::NoSolution) {
                                    return result;
                                }
                            }
                        }
                    } else if (const auto* unn_ptr = e.binding.opt_Union()) {
                        const auto& unn = **unn_ptr;
                        for (const auto& field : unn.m_variants) {
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
                if (e.path.m_data.is_UfcsKnown()
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
            size_t candidate_index,
            const ::HIR::SimplePath& trait,
            const ::HIR::TraitPath::assoc_list_t* associated
        ) {
            auto* candidate = m_frames[frame_index]->candidates[candidate_index];
            candidate->ambiguity_beyond_head = false;
            if (associated) {
                bind_candidate_placeholders(
                    *candidate,
                    candidate->impl.get_impl_type(m_crate.m_types),
                    *associated,
                    true
                );
            }
            const bool environment_response_constraint =
                candidate->head_match == ::HIR::Compare::Fuzzy
                && is_environment_or_builtin(candidate->impl)
                && !candidate_has_placeholders(*candidate);
            auto result = candidate->head_match == ::HIR::Compare::Equal
                    || environment_response_constraint
                ? Certainty::Proven : Certainty::Ambiguous;

            const bool auto_builtin = candidate->auto_builtin;
            const auto* marker_impl = candidate->marker_impl;
            if (auto_builtin) {
                const auto& response = candidate->impl.m_data.as_Bounded();
                const auto structural = evaluate_auto_builtin(
                    trait, response.trait_args, response.type
                );
                if (structural == Certainty::NoSolution) {
                    return Certainty::NoSolution;
                }
                if (structural == Certainty::Ambiguous) {
                    candidate->ambiguity_beyond_head = true;
                    result = Certainty::Ambiguous;
                }
            }

            const auto assoc_result = match_associated_types(
                trait, candidate->impl, associated
            );
            if (assoc_result == Certainty::NoSolution) {
                return Certainty::NoSolution;
            }
            if (assoc_result == Certainty::Ambiguous) {
                candidate->ambiguity_beyond_head = true;
                result = Certainty::Ambiguous;
            }

            const auto* trait_impl = candidate->impl.m_data.opt_TraitImpl();
            const ::HIR::GenericParams* impl_params_def = marker_impl
                ? &marker_impl->m_params
                : (trait_impl && trait_impl->impl
                    ? &trait_impl->impl->m_params
                    : nullptr);
            if (!impl_params_def) {
                return result;
            }

            for (const auto& bound : impl_params_def->m_bounds) {
                if (const auto* be = bound.opt_TraitBound()) {
                    ::HIR::TypeRef nested_type;
                    ::HIR::SimplePath nested_trait;
                    ::HIR::PathParams nested_params;
                    ::HIR::TraitPath::assoc_list_t nested_associated;

                    // Candidate and response storage is pool-backed, so nested
                    // goals cannot relocate this parent slot.
                    auto monomorph_bound = [&](auto& ms) {
                        static const ::HIR::GenericParams no_hrbs;
                        const bool outer_present = be->hrtbs && !be->hrtbs->is_empty();
                        auto hrb_guard = ms.push_hrb(outer_present ? *be->hrtbs : no_hrbs);
                        auto bound_type = ms.monomorph_type(span(), be->type);
                        auto bound_trait = ms.monomorph_traitpath(span(), be->trait, true);

                        const auto hrl_params = outer_present
                            ? be->hrtbs->make_empty_params(true)
                            : (bound_trait.m_hrtbs
                                ? bound_trait.m_hrtbs->make_empty_params(true)
                                : ::HIR::PathParams());
                        auto hrl_monomorph = MonomorphHrlsOnly(m_crate.m_types, hrl_params);
                        nested_type = hrl_monomorph.monomorph_type(span(), bound_type, true);
                        nested_trait = bound_trait.m_path.m_path;
                        nested_params = hrl_monomorph.monomorph_path_params(
                            span(), bound_trait.m_path.m_params, true
                        );
                        for (const auto& aty : bound_trait.m_type_bounds) {
                            auto value = aty.second.clone();
                            value.type = hrl_monomorph.monomorph_type(span(), value.type, true);
                            value.aty_params = hrl_monomorph.monomorph_path_params(
                                span(), value.aty_params, true
                            );
                            nested_associated.insert({aty.first, ::std::move(value)});
                        }
                    };
                    if (marker_impl) {
                        auto ms = MonomorphStatePtr(
                            m_crate.m_types,
                            nullptr,
                            &candidate->marker_impl_params,
                            nullptr
                        );
                        monomorph_bound(ms);
                    } else {
                        auto ms = candidate->impl.get_cb_monomorph_traitimpl(m_crate.m_types, span(), {});
                        monomorph_bound(ms);
                    }

                    // An impl parameter may occur only in an associated-type
                    // equality of a nested goal.  Canonical solvers infer that
                    // parameter from the projection response of the nested
                    // goal; preserve the same response in our impl parameters
                    // before evaluating the goal itself.
                    if (bind_candidate_placeholders(
                            *candidate, nested_type, nested_associated
                        )) {
                        nested_associated.clear();
                        if (marker_impl) {
                            auto ms = MonomorphStatePtr(
                                m_crate.m_types,
                                nullptr,
                                &candidate->marker_impl_params,
                                nullptr
                            );
                            monomorph_bound(ms);
                        } else {
                            auto ms = candidate->impl.get_cb_monomorph_traitimpl(
                                m_crate.m_types, span(), {}
                            );
                            monomorph_bound(ms);
                        }
                    }

                    const auto nested = solve_goal(
                        nested_trait, nested_params, nested_type, &nested_associated
                    );
                    if (nested == Certainty::NoSolution) {
                        return Certainty::NoSolution;
                    }
                    if (nested == Certainty::Ambiguous) {
                        candidate->ambiguity_beyond_head = true;
                        result = Certainty::Ambiguous;
                    }
                } else if (const auto* equality = bound.opt_TypeEquality()) {
                    ::HIR::TypeRef left;
                    ::HIR::TypeRef right;
                    if (marker_impl) {
                        auto ms = MonomorphStatePtr(
                            m_crate.m_types,
                            nullptr,
                            &candidate->marker_impl_params,
                            nullptr
                        );
                        left = ms.monomorph_type(span(), equality->type);
                        right = ms.monomorph_type(span(), equality->other_type);
                    } else {
                        auto ms = candidate->impl.get_cb_monomorph_traitimpl(m_crate.m_types, span(), {});
                        left = ms.monomorph_type(span(), equality->type);
                        right = ms.monomorph_type(span(), equality->other_type);
                    }
                    const auto cmp = m_resolve.compare_ty(span(), left, right);
                    if (cmp == ::HIR::Compare::Unequal) {
                        return Certainty::NoSolution;
                    }
                    if (cmp == ::HIR::Compare::Fuzzy) {
                        candidate->ambiguity_beyond_head = true;
                        result = Certainty::Ambiguous;
                    }
                }
                // Region/outlives bounds are external constraints.  They do
                // not choose a trait candidate and are intentionally retained
                // for the later lifetime phase rather than approximated here.
            }
            return result;
        }

        Certainty solve_goal(
            const ::HIR::SimplePath& trait,
            const ::HIR::PathParams& params,
            const ::HIR::TypeRef& type,
            const ::HIR::TraitPath::assoc_list_t* associated
        ) {
            const auto available_depth = available_depth_for_nested();
            if (!available_depth) {
                return Certainty::Ambiguous;
            }
            auto goal_type = type;
            auto goal_params = params.clone();
            for (auto& param : goal_params.m_types) {
                param = m_resolve.expand_associated_types(
                    span(), ::std::move(param)
                );
            }
            const auto& resolved_type = m_resolve.resolve_type(goal_type);
            // Candidate assembly must not use an unconstrained `Self` type to
            // guide inference.  rustc's next solver forces ambiguity here,
            // before it assembles any candidates.
            if (const auto* infer = resolved_type->opt_Infer()) {
                if (!infer->is_lit()) {
                    return Certainty::Ambiguous;
                }
            }
            const auto canonical = canonicalize_goal(
                goal_params, resolved_type, associated
            );
            const auto* canonical_associated = canonical.associated.empty()
                ? nullptr : &canonical.associated;
            const auto hash = goal_hash(
                trait, canonical.params, canonical.type, canonical_associated
            );
            if (const auto* cached = find_cached_goal(
                    hash,
                    trait,
                    canonical.params,
                    canonical.type,
                    canonical_associated
                )) {
                return cached->certainty;
            }
            if (find_active_goal(
                    hash,
                    trait,
                    canonical.params,
                    canonical.type,
                    canonical_associated
                )) {
                // Auto traits are coinductive; ordinary trait cycles do not
                // prove the goal but remain a possible fixed point.
                return m_crate.get_trait_by_path(span(), trait).m_is_marker
                    ? Certainty::Proven
                    : Certainty::Ambiguous;
            }

            auto* active_goal = push_active_goal(
                hash,
                trait,
                canonical.params,
                canonical.type,
                canonical_associated
            );
            struct StackGuard {
                NextTraitGoalEvaluator& self;
                GoalKey* goal;
                ~StackGuard() { self.pop_active_goal(goal); }
            } guard{*this, active_goal};
            auto cache_result = [&](Certainty certainty) {
                return cache_goal(
                    hash,
                    trait,
                    canonical.params,
                    canonical.type,
                    canonical_associated,
                    certainty
                );
            };

            const size_t frame_index = m_frame_depth++;
            if (frame_index == m_frames.size()) {
                m_frames.push_back(m_crate.m_pool->make<CandidateFrame>());
            }
            m_frames[frame_index]->clear(m_candidate_nodes);
            m_frames[frame_index]->available_depth = *available_depth;
            struct FrameGuard {
                NextTraitGoalEvaluator& self;
                size_t index;
                ~FrameGuard() {
                    const bool encountered_overflow =
                        self.m_frames[index]->encountered_overflow;
                    self.m_frames[index]->clear(self.m_candidate_nodes);
                    assert(self.m_frame_depth == index + 1);
                    self.m_frame_depth--;
                    if (encountered_overflow && index > 0) {
                        self.m_frames[index - 1]->encountered_overflow = true;
                    }
                }
            } frame_guard{*this, frame_index};

            try {
                assemble_candidates(frame_index, trait, goal_params, resolved_type);
            } catch (const TraitResolution::RecursionDetected&) {
                return cache_result(Certainty::Ambiguous);
            }

            bool saw_ambiguous = false;
            bool suppress_auto_builtin = false;
            bool negative_proven = false;
            bool negative_ambiguous = false;
            Certainty auto_builtin_result = Certainty::NoSolution;
            const size_t candidate_count = m_frames[frame_index]->candidates.size();
            for (size_t i = 0; i < candidate_count; i++) {
                const auto result = evaluate_candidate(frame_index, i, trait, associated);
                auto* candidate = m_frames[frame_index]->candidates[i];
                candidate->certainty = result;
                if (candidate->is_negative()) {
                    negative_proven |= result == Certainty::Proven;
                    negative_ambiguous |= result == Certainty::Ambiguous;
                    continue;
                }
                if (candidate->auto_builtin) {
                    auto_builtin_result = result;
                    continue;
                }
                suppress_auto_builtin |= candidate->is_positive_marker_impl()
                    && result != Certainty::NoSolution;
                if (result == Certainty::Proven) {
                    return cache_result(Certainty::Proven);
                }
                saw_ambiguous |= result == Certainty::Ambiguous;
            }
            if (!suppress_auto_builtin && !negative_proven) {
                if (negative_ambiguous && auto_builtin_result == Certainty::Proven) {
                    auto_builtin_result = Certainty::Ambiguous;
                }
                if (auto_builtin_result == Certainty::Proven) {
                    return cache_result(Certainty::Proven);
                }
                saw_ambiguous |= auto_builtin_result == Certainty::Ambiguous;
            }
            if (saw_ambiguous
                || m_resolve.type_contains_ivars(resolved_type)
                || m_resolve.params_contain_ivars(goal_params)
                || (m_coherence_mode
                    && !trait_ref_is_knowable(trait, goal_params, resolved_type))) {
                return cache_result(Certainty::Ambiguous);
            }
            return cache_result(Certainty::NoSolution);
        }

        Certainty match_root_associated(
            const ::HIR::SimplePath& trait,
            const ImplRef& impl,
            const char* assoc_name,
            const ::HIR::TypeRef* assoc_type,
            const ::HIR::PathParams* assoc_params
        ) const {
            if (!assoc_name || !assoc_name[0]) {
                return Certainty::Proven;
            }
            const static ::HIR::PathParams no_params;
            const auto& params = assoc_params ? *assoc_params : no_params;
            if (!impl.m_data.is_TraitImpl() && params.has_params()) {
                return Certainty::Ambiguous;
            }
            auto output = impl.get_type(m_crate.m_types, assoc_name, params);
            if (output == ::HIR::TypeRef()) {
                if (impl.m_data.is_TraitImpl()) {
                    return Certainty::Ambiguous;
                }
                if (!assoc_type) {
                    // A bare ParamEnv trait predicate does not normalize its
                    // associated type.  It only proves that the projection is
                    // well-formed, so the normalizes-to response is ambiguous.
                    return Certainty::Ambiguous;
                }
                output = make_associated_projection(
                    impl,
                    ::HIR::GenericPath(trait, impl.get_trait_params(m_crate.m_types)),
                    RcString::new_interned(assoc_name),
                    params
                );
            }
            if (!assoc_type) {
                return Certainty::Proven;
            }
            const auto cmp = m_resolve.compare_ty(span(), *assoc_type, output);
            if (cmp == ::HIR::Compare::Unequal) {
                return Certainty::NoSolution;
            }
            return cmp == ::HIR::Compare::Equal
                ? Certainty::Proven
                : Certainty::Ambiguous;
        }

        ImplRef materialize_root_associated(
            ImplRef impl,
            const ::HIR::SimplePath& trait,
            const char* assoc_name,
            const ::HIR::PathParams* assoc_params
        ) const {
            if (!assoc_name || !assoc_name[0] || impl.m_data.is_TraitImpl()) {
                return impl;
            }
            const static ::HIR::PathParams no_params;
            const auto& item_params = assoc_params ? *assoc_params : no_params;
            if (impl.get_type(m_crate.m_types, assoc_name, item_params) != ::HIR::TypeRef()) {
                return impl;
            }

            auto type = impl.get_impl_type(m_crate.m_types);
            auto params = impl.get_trait_params(m_crate.m_types);
            ::HIR::TraitPath::assoc_list_t associated;
            if (const auto* bounded = impl.m_data.opt_BoundedPtr()) {
                for (const auto& entry : *bounded->assoc) {
                    associated.insert({entry.first, entry.second.clone()});
                }
            } else if (const auto* bounded = impl.m_data.opt_Bounded()) {
                for (const auto& entry : bounded->assoc) {
                    associated.insert({entry.first, entry.second.clone()});
                }
            }

            const auto name = RcString::new_interned(assoc_name);
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
            const bool ambiguous_identity = impl.is_ambiguous_identity();
            auto result = ImplRef(
                ::std::move(type),
                ::std::move(params),
                ::std::move(associated)
            );
            if (ambiguous_identity) {
                result.mark_ambiguous_identity();
            }
            return result;
        }

        bool responses_equal(
            const ImplRef& left,
            const ImplRef& right,
            const char* assoc_name,
            const ::HIR::PathParams* assoc_params
        ) const {
            auto types_equal_after_normalization = [&](const ::HIR::TypeRef& lhs,
                                                       const ::HIR::TypeRef& rhs) {
                if (lhs == ::HIR::TypeRef() || rhs == ::HIR::TypeRef()) {
                    return lhs == rhs;
                }
                auto normalized_lhs = m_resolve.expand_associated_types(
                    span(), lhs
                );
                auto normalized_rhs = m_resolve.expand_associated_types(
                    span(), rhs
                );
                if (normalized_lhs == ::HIR::TypeRef()
                    || normalized_rhs == ::HIR::TypeRef()) {
                    return normalized_lhs == normalized_rhs;
                }
                return m_resolve.resolve_type(normalized_lhs)
                    == m_resolve.resolve_type(normalized_rhs);
            };
            auto params_equal_after_normalization = [&](const ::HIR::PathParams& lhs,
                                                        const ::HIR::PathParams& rhs) {
                if (lhs.m_lifetimes.size() != rhs.m_lifetimes.size()
                    || lhs.m_types.size() != rhs.m_types.size()
                    || lhs.m_values.size() != rhs.m_values.size()) {
                    return false;
                }
                for (size_t i = 0; i < lhs.m_lifetimes.size(); i++) {
                    if (lhs.m_lifetimes[i] != rhs.m_lifetimes[i]) {
                        return false;
                    }
                }
                for (size_t i = 0; i < lhs.m_types.size(); i++) {
                    if (!types_equal_after_normalization(
                            lhs.m_types[i], rhs.m_types[i]
                        )) {
                        return false;
                    }
                }
                for (size_t i = 0; i < lhs.m_values.size(); i++) {
                    if (lhs.m_values[i] != rhs.m_values[i]) {
                        return false;
                    }
                }
                return true;
            };

            if (!types_equal_after_normalization(
                    left.get_impl_type(m_crate.m_types), right.get_impl_type(m_crate.m_types)
                )
                || !params_equal_after_normalization(
                    left.get_trait_params(m_crate.m_types), right.get_trait_params(m_crate.m_types)
                )) {
                return false;
            }
            if (!assoc_name || !assoc_name[0]) {
                return true;
            }
            const static ::HIR::PathParams no_params;
            const auto& params = assoc_params ? *assoc_params : no_params;
            if ((!left.m_data.is_TraitImpl() || !right.m_data.is_TraitImpl())
                && params.has_params()) {
                return false;
            }
            return types_equal_after_normalization(
                left.get_type(m_crate.m_types, assoc_name, params),
                right.get_type(m_crate.m_types, assoc_name, params)
            );
        }

    public:
        NextTraitGoalEvaluator(
            const TraitResolution& resolve,
            const ::HIR::Crate& crate
        )
            : m_resolve(resolve)
            , m_crate(crate)
            , m_candidate_nodes(crate.m_pool)
            , m_active_goal_nodes(crate.m_pool)
            , m_cached_goal_nodes(crate.m_pool)
        {
            m_frames.reserve(16);
            m_goal_stack.reserve(16);
            m_goal_cache.reserve(64);
            m_active_goal_index.reserve(32);
            m_goal_cache_index.reserve(128);
        }

        bool evaluate_overlap(
            const Span& call_span,
            const ::HIR::SimplePath& trait,
            const ::HIR::TraitImpl& left,
            const ::HIR::TraitImpl& right
        ) {
            ASSERT_BUG(call_span, !m_span, "nested coherence overlap session");
            ASSERT_BUG(call_span, !m_coherence_mode, "coherence mode leaked before overlap probe");
            ASSERT_BUG(call_span, m_goal_stack.empty(), "next-solver goal stack leaked before coherence probe");
            ASSERT_BUG(call_span, m_active_goal_index.empty(), "next-solver active goal index leaked before coherence probe");
            ASSERT_BUG(call_span, m_frame_depth == 0, "next-solver candidate frames leaked before coherence probe");
            clear_goal_cache();
            m_span = &call_span;
            m_coherence_mode = true;
            struct SessionGuard {
                NextTraitGoalEvaluator& self;
                ~SessionGuard() {
                    assert(self.m_goal_stack.empty());
                    assert(self.m_active_goal_index.empty());
                    self.clear_goal_cache();
                    self.m_frame_depth = 0;
                    self.m_coherence_mode = false;
                    self.m_span = nullptr;
                }
            } session_guard{*this};

            // Instantiate the first header with fresh inference variables, then
            // match the second header against it.  This is a unification of two
            // independently generic impls, not a one-way syntactic ordering.
            auto left_params = m_resolve.make_fresh_impl_params(left.m_params);
            auto left_monomorph = MonomorphStatePtr(m_crate.m_types, nullptr, &left_params, nullptr);
            auto goal_type = left_monomorph.monomorph_type(call_span, left.m_type, true);
            auto goal_params = left_monomorph.monomorph_path_params(
                call_span, left.m_trait_args, true
            );

            ::HIR::PathParams right_params;
            const auto right_match = m_resolve.ftic_check_params(
                call_span,
                trait,
                &goal_params,
                goal_type,
                right.m_params,
                right.m_trait_args,
                right.m_type,
                right_params,
                false
            );
            if (right_match == ::HIR::Compare::Unequal) {
                return false;
            }

            const size_t frame_index = m_frame_depth++;
            if (frame_index == m_frames.size()) {
                m_frames.push_back(m_crate.m_pool->make<CandidateFrame>());
            }
            m_frames[frame_index]->clear(m_candidate_nodes);
            m_frames[frame_index]->available_depth = ROOT_DEPTH;
            struct FrameGuard {
                NextTraitGoalEvaluator& self;
                size_t index;
                ~FrameGuard() {
                    const bool encountered_overflow =
                        self.m_frames[index]->encountered_overflow;
                    self.m_frames[index]->clear(self.m_candidate_nodes);
                    assert(self.m_frame_depth == index + 1);
                    self.m_frame_depth--;
                    if (encountered_overflow && index > 0) {
                        self.m_frames[index - 1]->encountered_overflow = true;
                    }
                }
            } frame_guard{*this, frame_index};

            const auto& trait_def = m_crate.get_trait_by_path(call_span, trait);
            push_candidate(
                frame_index,
                ImplRef(::std::move(left_params), trait_def, trait, left),
                ::HIR::Compare::Equal
            );
            push_candidate(
                frame_index,
                ImplRef(::std::move(right_params), trait_def, trait, right),
                right_match
            );

            const auto& candidates = m_frames[frame_index]->candidates;
            ASSERT_BUG(call_span, candidates.size() == 2, "coherence probe lost an impl candidate");
            const auto left_result = evaluate_candidate(frame_index, 0, trait, nullptr);
            if (left_result == Certainty::NoSolution) {
                return false;
            }
            const auto right_result = evaluate_candidate(frame_index, 1, trait, nullptr);
            return right_result != Certainty::NoSolution;
        }

        bool evaluate(
            const Span& call_span,
            const ::HIR::SimplePath& trait,
            const ::HIR::PathParams& params,
            const ::HIR::TypeRef& type,
            TraitResolution::t_cb_trait_impl_r callback,
            const char* assoc_name,
            const ::HIR::TypeRef* assoc_type,
            const ::HIR::PathParams* assoc_params
        ) {
            const bool outermost = m_span == nullptr;
            if (outermost) {
                ASSERT_BUG(call_span, m_goal_stack.empty(), "next-solver goal stack leaked between evaluations");
                ASSERT_BUG(call_span, m_active_goal_index.empty(), "next-solver active goal index leaked between evaluations");
                ASSERT_BUG(call_span, m_frame_depth == 0, "next-solver candidate frames leaked between evaluations");
                clear_goal_cache();
                m_span = &call_span;
            }
            struct SessionGuard {
                NextTraitGoalEvaluator& self;
                bool outermost;
                ~SessionGuard() {
                    if (outermost) {
                        assert(self.m_goal_stack.empty());
                        assert(self.m_active_goal_index.empty());
                        self.clear_goal_cache();
                        self.m_frame_depth = 0;
                        self.m_span = nullptr;
                    }
                }
            } session_guard{*this, outermost};

            auto goal_type = type;
            auto goal_params = params.clone();
            for (auto& param : goal_params.m_types) {
                param = m_resolve.expand_associated_types(
                    span(), ::std::move(param)
                );
            }
            const auto& resolved_type = m_resolve.resolve_type(goal_type);
            // Match rustc's forced-ambiguity response for an unconstrained
            // `Self` type.  Returning the identity response is important: no
            // particular impl is allowed to constrain the caller here.
            if (const auto* infer = resolved_type->opt_Infer()) {
                if (!infer->is_lit()) {
                    // The legacy lookup callback has no representation for a
                    // canonical identity response and would treat it as a
                    // concrete impl.  Extended solver callers pass a non-null
                    // assoc_name (possibly empty) and understand the marker;
                    // ordinary lookup observes ambiguity as no selection.
                    if (!assoc_name) {
                        return false;
                    }
                    auto ambiguous = ImplRef(
                        resolved_type,
                        goal_params.clone(),
                        ::HIR::TraitPath::assoc_list_t()
                    );
                    ambiguous.mark_ambiguous_identity();
                    return callback(
                        materialize_root_associated(
                            ::std::move(ambiguous),
                            trait,
                            assoc_name,
                            assoc_params
                        ),
                        ::HIR::Compare::Fuzzy
                    );
                }
            }
            const auto canonical = canonicalize_goal(
                goal_params, resolved_type, nullptr
            );
            const auto root_hash = goal_hash(
                trait, canonical.params, canonical.type, nullptr
            );
            if (find_active_goal(
                    root_hash,
                    trait,
                    canonical.params,
                    canonical.type,
                    nullptr
                )) {
                static const ::HIR::TraitPath::assoc_list_t no_associated;
                const bool coinductive = m_crate.get_trait_by_path(
                    span(), trait
                ).m_is_marker;
                return callback(
                    ImplRef(&resolved_type, &goal_params, &no_associated),
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

            const size_t frame_index = m_frame_depth++;
            if (frame_index == m_frames.size()) {
                m_frames.push_back(m_crate.m_pool->make<CandidateFrame>());
            }
            m_frames[frame_index]->clear(m_candidate_nodes);
            m_frames[frame_index]->available_depth = ROOT_DEPTH;
            struct FrameGuard {
                NextTraitGoalEvaluator& self;
                size_t index;
                ~FrameGuard() {
                    const bool encountered_overflow =
                        self.m_frames[index]->encountered_overflow;
                    self.m_frames[index]->clear(self.m_candidate_nodes);
                    assert(self.m_frame_depth == index + 1);
                    self.m_frame_depth--;
                    if (encountered_overflow && index > 0) {
                        self.m_frames[index - 1]->encountered_overflow = true;
                    }
                }
            } frame_guard{*this, frame_index};

            try {
                assemble_candidates(
                    frame_index, trait, goal_params, resolved_type
                );
            } catch (const TraitResolution::RecursionDetected&) {
                return false;
            }
            auto& frame = *m_frames[frame_index];
            const size_t candidate_count = frame.candidates.size();
            DEBUG("next-solver assembled " << candidate_count
                  << " candidate(s) for " << type << ": " << trait << params);

            bool suppress_auto_builtin = false;
            bool negative_proven = false;
            bool negative_ambiguous = false;
            ::HIR::TraitPath::assoc_list_t root_associated;
            if (assoc_name && assoc_name[0] && assoc_type) {
                const static ::HIR::PathParams no_assoc_params;
                root_associated.insert({
                    RcString::new_interned(assoc_name),
                    ::HIR::TraitPath::AtyEqual{
                        ::HIR::GenericPath(trait, goal_params.clone()),
                        assoc_params ? assoc_params->clone() : no_assoc_params.clone(),
                        *assoc_type
                    }
                });
            }
            for (size_t i = 0; i < candidate_count; i++) {
                auto certainty = evaluate_candidate(
                    frame_index,
                    i,
                    trait,
                    root_associated.empty() ? nullptr : &root_associated
                );
                auto* candidate = frame.candidates[i];
                if (!candidate->is_negative()) {
                    const auto assoc_certainty = match_root_associated(
                        trait,
                        candidate->impl,
                        assoc_name,
                        assoc_type,
                        assoc_params
                    );
                    if (assoc_certainty == Certainty::NoSolution) {
                        certainty = Certainty::NoSolution;
                    } else if (assoc_certainty == Certainty::Ambiguous
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
                            return candidate->auto_builtin;
                        }
                    ),
                    viable.end()
                );
            } else if (negative_ambiguous) {
                for (auto* candidate : frame.viable) {
                    if (candidate->auto_builtin
                        && candidate->certainty == Certainty::Proven) {
                        candidate->certainty = Certainty::Ambiguous;
                    }
                }
            }

            if (frame.viable.empty()) {
                DEBUG("next-solver: no viable response");
                return false;
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
                    if (responses_equal(left, right, assoc_name, assoc_params)) {
                        continue;
                    }
                    if (!left.m_data.is_TraitImpl() || !right.m_data.is_TraitImpl()
                        || !m_resolve.impls_overlap(span(), left, right)) {
                        continue;
                    }
                    // A more-specific impl with an ambiguous where-clause
                    // cannot shadow the fallback: that nested goal may still
                    // fail.  Head ambiguity alone is inference guidance and
                    // remains eligible for specialization.
                    if (left.more_specific_than(m_crate.m_types, right)
                        && !frame.viable[i]->ambiguity_beyond_head) {
                        frame.viable[j]->discarded = true;
                    } else if (right.more_specific_than(m_crate.m_types, left)
                               && !frame.viable[j]->ambiguity_beyond_head) {
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
                        assoc_name,
                        assoc_params
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
                    return callback(
                        ::std::move(selected->impl),
                        ::HIR::Compare::Fuzzy
                    );
                }
                return callback(
                    materialize_root_associated(
                        ::std::move(selected->impl),
                        trait,
                        assoc_name,
                        assoc_params
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
                ::HIR::TraitPath::assoc_list_t()
            );
            ambiguous.mark_ambiguous_identity();
            return callback(
                materialize_root_associated(
                    ::std::move(ambiguous),
                    trait,
                    assoc_name,
                    assoc_params
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
    , m_lang_Deref(crate.get_lang_item_path_opt("deref"))
    , m_ivars(ivars)
    , m_coherence_ivars(crate.m_types)
    , m_vis_path(vis_path)
    , m_current_trait_path(current_trait)
    , m_current_trait_ptr(current_trait ? &crate.get_trait_by_path(Span(), current_trait->m_path) : nullptr)
{
    m_impl_generics = impl_params;
    m_item_generics = item_params;
    prep_indexes(Span());
}

TraitResolution::~TraitResolution() = default;

void TraitResolution::set_generic_context(
    const ::HIR::GenericParams* impl_params,
    const ::HIR::GenericParams* item_params
) {
    if (m_impl_generics == impl_params && m_item_generics == item_params) {
        return;
    }
    ASSERT_BUG(Span(), m_eat_active_stack.empty(), "changing trait environment during associated-type expansion");
    m_impl_generics = impl_params;
    m_item_generics = item_params;
    m_eat_cache.clear();
    prep_indexes(Span());
}

::HIR::PathParams TraitResolution::make_fresh_impl_params(
    const ::HIR::GenericParams& params
) const {
    auto& ivars = const_cast<HMTypeInferrence&>(m_ivars);
    ::HIR::PathParams result;
    result.m_lifetimes = ThinVector<::HIR::LifetimeRef>(params.m_lifetimes.size());
    result.m_types.reserve(params.m_types.size());
    for (size_t i = 0; i < params.m_types.size(); i++) {
        result.m_types.push_back(ivars.new_ivar_tr());
    }
    result.m_values.reserve(params.m_values.size());
    for (size_t i = 0; i < params.m_values.size(); i++) {
        result.m_values.push_back(
            ::HIR::ConstGeneric::make_Infer({ivars.new_ivar_val()})
        );
    }
    return result;
}

bool TraitResolution::impls_overlap(
    const Span& sp,
    const ImplRef& left,
    const ImplRef& right
) const {
    const auto* left_impl = left.m_data.opt_TraitImpl();
    const auto* right_impl = right.m_data.opt_TraitImpl();
    if (!gTraitSolverConfig.coherence
        || !left_impl || !right_impl
        || !left_impl->impl || !right_impl->impl) {
        return left.overlaps_with(m_crate, right);
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
    m_coherence_ivars.m_ivars.clear();
    m_coherence_ivars.m_values.clear();
    m_coherence_ivars.m_has_changed = false;
    if (!m_coherence_resolve) {
        ASSERT_BUG(sp, m_crate.m_pool, "next-solver coherence requires the crate object pool");
        m_coherence_resolve = m_crate.m_pool->make<TraitResolution>(
            m_coherence_ivars,
            m_crate,
            m_impl_generics,
            m_item_generics,
            m_vis_path,
            m_current_trait_path
        );
    } else {
        m_coherence_resolve->set_generic_context(m_impl_generics, m_item_generics);
    }
    if (!m_coherence_resolve->m_next_solver) {
        m_coherence_resolve->m_next_solver = m_crate.m_pool->make<NextTraitGoalEvaluator>(
            *m_coherence_resolve, m_crate
        );
    }
    return m_coherence_resolve->m_next_solver->evaluate_overlap(
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
    const ::HIR::TypeRef& type,
    t_cb_trait_impl_r callback,
    const char* assoc_name,
    const ::HIR::TypeRef* assoc_type,
    const ::HIR::PathParams* assoc_params
) const {
    TRACE_FUNCTION_F("trait = " << trait << params << ", type = " << type);
    if (!m_next_solver) {
        ASSERT_BUG(sp, m_crate.m_pool, "next-solver requires the crate object pool");
        m_next_solver = m_crate.m_pool->make<NextTraitGoalEvaluator>(*this, m_crate);
    }
    return m_next_solver->evaluate(
        sp, trait, params, type, ::std::move(callback), assoc_name, assoc_type, assoc_params
    );
}

bool TraitResolution::find_trait_impls(
    const Span& sp,
    const ::HIR::SimplePath& trait,
    const ::HIR::PathParams& params,
    const ::HIR::TypeRef& type,
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

        void TraitResolution::compact_ivars(HMTypeInferrence& m_ivars) {
            m_ivars.check_for_loops();

            //m_ivars.compact_ivars([&](const ::HIR::TypeRef& t)->auto{ return this->expand_associated_types(Span(), t.clone); });
            unsigned int i = 0;
            for (auto& v : m_ivars.m_ivars) {
                if (!v.is_alias()) {
                    m_ivars.expand_ivars(v.type);
                    // Don't expand unless it is needed
                    if (this->has_associated_type(v.type)) {
                        auto nt = this->expand_associated_types(Span(), v.type);
                        DEBUG("- " << i << " " << v.type << " -> " << nt);
                        v.type = nt;
                    }
                } else {
                    auto index = v.alias;
                    unsigned int count = 0;
                    assert(index < m_ivars.m_ivars.size());
                    while (m_ivars.m_ivars.at(index).is_alias()) {
                        index = m_ivars.m_ivars.at(index).alias;

                        if (count >= m_ivars.m_ivars.size()) {
                            this->m_ivars.dump();
                            BUG(Span(), "Loop detected in ivar list when starting at " << v.alias << ", current is " << index);
                        }
                        count++;
                    }
                    v.alias = index;
                }
                i++;
            }
        }

        bool TraitResolution::has_associated_type(const ::HIR::TypeRef& input) const {
            if (!input->may_have_associated_type()) {
                return false;
            }
            struct H {
                static bool check_pathparams(const TraitResolution& r, const ::HIR::PathParams& pp) {
                    for (const auto& arg : pp.m_types) {
                        if (r.has_associated_type(arg)) {
                            return true;
                        }
                    }
                    return false;
                }

                static bool check_path(const TraitResolution& r, const ::HIR::Path& p) {
                    TU_MATCH(::HIR::Path::Data, (p.m_data), (e2), (Generic, return H::check_pathparams(r, e2.m_params);), (UfcsInherent, if (r.has_associated_type(e2.type)) return true; if (H::check_pathparams(r, e2.params)) return true; return false;), (UfcsKnown, if (r.has_associated_type(e2.type)) return true; if (H::check_pathparams(r, e2.trait.m_params)) return true; if (H::check_pathparams(r, e2.params)) return true; return false;), (UfcsUnknown, BUG(Span(), "Encountered UfcsUnknown - " << p);))
                    throw "";
                }
            };

            //TRACE_FUNCTION_F(input);
    TU_MATCH_HDRA( (*input), {)
    TU_ARMA(Infer, e) {
            const auto& ty = this->m_ivars.get_type(input);
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
            if (e.path.m_data.is_UfcsKnown()
                && (e.binding.is_Unbound() || e.binding.is_Opaque())) {
                return true;
            }
            return H::check_path(*this, e.path);
        }
        TU_ARMA(Generic, e) {
            return false;
        }
        TU_ARMA(TraitObject, e) {
            // Recurse?
            if (H::check_pathparams(*this, e.m_trait.m_path.m_params)) {
                return true;
            }
            for (const auto& m : e.m_markers) {
                if (H::check_pathparams(*this, m.m_params)) {
                    return true;
                }
            }
            return false;
        }
        TU_ARMA(ErasedType, e) {
        TU_MATCH_HDRA( (e.m_inner), {)
        TU_ARMA(Fcn, ee) {
                    if (H::check_path(*this, ee.m_origin)) {
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
        for(const auto& m : e.m_traits) {
                if (H::check_pathparams(*this, m.m_path.m_params)) {
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
            return H::check_path(*this, e.path);
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

        void TraitResolution::expand_associated_types_inplace(const Span& sp, ::HIR::TypeRef& input, LList<const ::HIR::TypeRef*> stack) const {
            struct H {
                static void expand_associated_types_params(const Span& sp, const TraitResolution& res, ::HIR::PathParams& params, LList<const ::HIR::TypeRef*> stack) {
                    for (auto& arg : params.m_types) {
                        res.expand_associated_types_inplace(sp, arg, stack);
                    }
                }

                static void expand_associated_types_tp(const Span& sp, const TraitResolution& res, ::HIR::TraitPath& input, LList<const ::HIR::TypeRef*> stack) {
                    expand_associated_types_params(sp, res, input.m_path.m_params, stack);
                    for (auto& arg : input.m_type_bounds) {
                        expand_associated_types_params(sp, res, arg.second.source_trait.m_params, stack);
                        res.expand_associated_types_inplace(sp, arg.second.type, stack);
                    }
                    for (auto& arg : input.m_trait_bounds) {
                        expand_associated_types_params(sp, res, arg.second.source_trait.m_params, stack);
                        for (auto& t : arg.second.traits) {
                            expand_associated_types_tp(sp, res, t, stack);
                        }
                    }
                }
            };

            for (const auto& ty : m_eat_active_stack) {
                if (input == *ty) {
                    DEBUG("Recursive lookup, skipping - &input = " << &input);
                    return;
                }
            }
            //TRACE_FUNCTION_F(input);
    auto data = input->clone_data();
    TU_MATCH_HDRA( (data), {)
    TU_ARMA(Infer, e) {
            auto& ty = this->m_ivars.get_type(input);
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
        TU_MATCH_HDRA( (e.path.m_data), {)
        TU_ARMA(Generic, pe) {
                    ConvertHIR_ConstantEvaluate_MethodParams(sp, m_crate, m_vis_path, m_impl_generics, m_item_generics, e.binding.get_generics(), pe.m_params);
                    H::expand_associated_types_params(sp, *this, pe.m_params, stack);
                }
                TU_ARMA(UfcsInherent, pe) {
                    expand_associated_types_inplace(sp, pe.type, stack);
                    H::expand_associated_types_params(sp, *this, pe.params, stack);
                    input = m_crate.m_types.intern(mv$(data));
                    // TODO: only valid for enum variants? (and only in some contexts)
                    const auto& rebuilt_path = input->as_Path().path;
                    const auto& rebuilt_pe = rebuilt_path.m_data.as_UfcsInherent();
                    if (TU_TEST1(*rebuilt_pe.type, Path, .binding.is_Enum())) {
                        return;
                    }
                    TODO(sp, "Path - UfcsInherent - " << rebuilt_path);
                }
                TU_ARMA(UfcsKnown, pe) {
                    struct D {
                        const TraitResolution& m_tr;
                        D(const TraitResolution& tr, ::HIR::TypeRef v)
                            : m_tr(tr)
                        {
                            tr.m_eat_active_stack.push_back(box$(v));
                        }
                        ~D() {
                            m_tr.m_eat_active_stack.pop_back();
                        }
                        D(D&&) = delete;
                        D(const D&) = delete;
                    };
                    D _(*this, input);
                    // State stack to avoid infinite recursion
                    assert(m_eat_active_stack.size() > 0);
                    auto& prev_stack = stack;
                    LList<const ::HIR::TypeRef*> stack(&prev_stack, m_eat_active_stack.back().get());

                    expand_associated_types_inplace(sp, pe.type, stack);
                    H::expand_associated_types_params(sp, *this, pe.params, stack);
                    H::expand_associated_types_params(sp, *this, pe.trait.m_params, stack);
                    input = m_crate.m_types.intern(mv$(data));
                    // Retry opaque projections too: equality bounds can be
                    // learned after an earlier normalisation attempt.
                    const bool was_unbound = input->as_Path().binding.is_Unbound();
                    const bool was_opaque = input->as_Path().binding.is_Opaque();
                    if (was_unbound || was_opaque) {
                        if (was_opaque) {
                            this->expand_associated_types_inplace__UfcsKnown(sp, input, stack);
                            return;
                        }

                        // Cache the result of this to avoid needing to do the full resolution too often.
                        // - This avoids VERY slow typechecking in 1.90's librustc_target
                        auto k = FMT(input);
                        auto it = m_eat_cache.find(k);
                        if (it != m_eat_cache.end()) {
                            if (input != it->second) {
                                this->expand_associated_types_inplace(sp, it->second, stack);
                            }
                            DEBUG("CACHED: " << input << " -> " << it->second);
                            input = it->second;
                        } else {
                            this->expand_associated_types_inplace__UfcsKnown(sp, input, stack);
                            if (input->is_Path()
                                && (input->as_Path().binding.is_Unbound()
                                    || input->as_Path().binding.is_Opaque())) {
                            } else {
                                DEBUG("CACHE+: " << k << " = " << input);
                                m_eat_cache.insert(::std::make_pair(k, input));
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
            H::expand_associated_types_tp(sp, *this, e.m_trait, stack);
            for (auto& m : e.m_markers) {
                H::expand_associated_types_params(sp, *this, m.m_params, stack);
            }
        }
        TU_ARMA(ErasedType, e) {
            // Recurse?
        }
        TU_ARMA(Array, e) {
            ConvertHIR_ConstantEvaluate_ArraySize(sp, m_crate, m_vis_path, e.size);
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
        TU_MATCH_HDRA( (e.path.m_data), {)
        TU_ARMA(Generic, pe) {
                    //ConvertHIR_ConstantEvaluate_MethodParams(sp, m_crate, m_vis_path, m_impl_generics, m_item_generics, *e.binding.get_generics(), pe.m_params);
                    H::expand_associated_types_params(sp, *this, pe.m_params, stack);
                }
                TU_ARMA(UfcsInherent, pe) {
                    expand_associated_types_inplace(sp, pe.type, stack);
                    H::expand_associated_types_params(sp, *this, pe.params, stack);
                }
                TU_ARMA(UfcsKnown, pe) {
                    expand_associated_types_inplace(sp, pe.type, stack);
                    H::expand_associated_types_params(sp, *this, pe.params, stack);
                    H::expand_associated_types_params(sp, *this, pe.trait.m_params, stack);
                }
                TU_ARMA(UfcsUnknown, pe) {
                    BUG(sp, "Encountered UfcsUnknown");
                }
        }
        // TODO: Should this re-populate `def`? Not right now, assuming it's set once only
        }
        TU_ARMA(Function, e) {
            for (auto& ty : e.m_arg_types) {
                expand_associated_types_inplace(sp, ty, stack);
            }
            expand_associated_types_inplace(sp, e.m_rettype, stack);
        }
        TU_ARMA(NodeType, e) {
            // Recurse? Nah.
        }
    }
            input = m_crate.m_types.intern(mv$(data));
        }

        void TraitResolution::expand_associated_types_inplace__UfcsKnown(const Span& sp, ::HIR::TypeRef& input, LList<const ::HIR::TypeRef*> stack) const {
            TRACE_FUNCTION_FR("input=" << input, input);
            auto data = input->clone_data();
            auto& builder_e = data.as_Path();
            auto& builder_pe = builder_e.path.m_data.as_UfcsKnown();

            expand_associated_types_inplace(sp, builder_pe.type, stack);
            for (auto& ty : builder_pe.trait.m_params.m_types) {
                expand_associated_types_inplace(sp, ty, stack);
            }
            input = m_crate.m_types.intern(mv$(data));
            const auto& e = input->as_Path();
            const auto& pe = e.path.m_data.as_UfcsKnown();
            auto mark_opaque = [&]() {
                auto opaque_data = input->clone_data();
                opaque_data.as_Path().binding = ::HIR::TypePathBinding::make_Opaque({});
                input = m_crate.m_types.intern(mv$(opaque_data));
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
                auto cb = [](const ::HIR::TypeRef& ty) {
                    return !(ty->is_Generic() && ty->as_Generic().is_placeholder());
                };
                bool has_impl_placeholders = false;
                if (!visit_ty_with(pe.type, cb)) {
                    has_impl_placeholders = true;
                }
                for (const auto& ty : pe.trait.m_params.m_types) {
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
            if (!this->trait_contains_type(sp, pe.trait, this->m_crate.get_trait_by_path(sp, pe.trait.m_path), pe.item.c_str(), trait_path)) {
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
                    if (pe.trait.m_path == m_lang_Fn || pe.trait.m_path == m_lang_FnMut || pe.trait.m_path == m_lang_FnOnce) {
                        if (pe.item == "Output") {
                            input = node_p->m_return;
                            return;
                        } else {
                            ERROR(sp, E0000, "No associated type " << pe.item << " for trait " << pe.trait);
                        }
                    }
                    // TODO: Fall through? Maybe there's a generic impl that could match.
                }
                TU_ARMA(Generator, node_p) {
                    if (pe.trait.m_path == this->m_lang_Generator) {
                        if (pe.item == "Return") {
                            input = node_p->m_return;
                            return;
                        } else if (pe.item == "Yield") {
                            input = node_p->m_yield_ty;
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
            if (te.m_abi == ABI_RUST && !te.is_unsafe) {
                if (pe.trait.m_path == m_lang_Fn || pe.trait.m_path == m_lang_FnMut || pe.trait.m_path == m_lang_FnOnce) {
                    if (pe.item == "Output") {
                        input = te.m_rettype;
                        return;
                    } else {
                        ERROR(sp, E0000, "No associated type " << pe.item << " for trait " << pe.trait);
                    }
                }
            }
        }
        // If it's a TraitObject, then maybe we're asking for a bound
        TU_ARMA(TraitObject, te) {
            const auto& data_trait = te.m_trait.m_path;
            if (pe.trait.m_path == data_trait.m_path) {
                auto cmp = ::HIR::Compare::Equal;
                if (pe.trait.m_params.m_types.size() != data_trait.m_params.m_types.size()) {
                    cmp = ::HIR::Compare::Unequal;
                } else {
                    for (unsigned int i = 0; i < pe.trait.m_params.m_types.size(); i++) {
                        const auto& l = pe.trait.m_params.m_types[i];
                        const auto& r = data_trait.m_params.m_types[i];
                        cmp &= l->compare_with_placeholders(sp, r, m_ivars.callback_resolve_infer());
                    }
                }
                if (cmp != ::HIR::Compare::Unequal) {
                    auto it = te.m_trait.m_type_bounds.find(pe.item);
                    if (it == te.m_trait.m_type_bounds.end()) {
                        // TODO: Mark as opaque and return.
                        // - Why opaque? It's not bounded, don't even bother
                        TODO(sp, "Handle unconstrained associate type " << pe.item << " from " << pe.type);
                    }

                    auto hrl_pps = te.m_trait.m_hrtbs ? te.m_trait.m_hrtbs->make_empty_params(true) : HIR::PathParams();
                    input = MonomorphHrlsOnly(m_crate.m_types, hrl_pps).monomorph_type(sp, it->second.type);
                    return;
                }
            }

            // - Check if the desired trait is a supertrait of this.
            // NOTE: `params` (aka des_params) is not used (TODO)
            bool is_supertrait = this->find_named_trait_in_trait(sp, pe.trait.m_path, pe.trait.m_params, *te.m_trait.m_trait_ptr, data_trait.m_path, data_trait.m_params, pe.type, [&](const HIR::TraitPath& i_tp) {
                // The above is just the monomorphised params and associated set. Comparison is still needed.
                auto cmp = this->compare_pp(sp, i_tp.m_path.m_params, pe.trait.m_params);
                if (cmp != ::HIR::Compare::Unequal) {
                    // Search for bounded types in this TraitPath (from `find_named_trait_in_trait` and in the original input TraitPath `te.m_trait`)
                    auto it = i_tp.m_type_bounds.find(pe.item);
                    if (it == i_tp.m_type_bounds.end()) {
                        // NOTE: (currently) there can only be one trait with this name, so if we found this trait and the item is present - good.
                        it = te.m_trait.m_type_bounds.find(pe.item);
                    }
                    if (it != te.m_trait.m_type_bounds.end()) {
                        // Remove HRLs (TODO: Match them? not really needed in this stage I think)
                        auto hrl_pps = te.m_trait.m_hrtbs ? te.m_trait.m_hrtbs->make_empty_params(true) : i_tp.m_hrtbs ? i_tp.m_hrtbs->make_empty_params(true) : HIR::PathParams();
                        input = MonomorphHrlsOnly(m_crate.m_types, hrl_pps).monomorph_type(sp, it->second.type);
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
            for (const auto& trait : te.m_traits) {
                const auto& trait_gp = trait.m_path;
                if (trait_path.m_path == trait_gp.m_path) {
                    auto cmp = ::HIR::Compare::Equal;
                    if (trait_path.m_params.m_types.size() != trait_gp.m_params.m_types.size()) {
                        cmp = ::HIR::Compare::Unequal;
                    } else {
                        for (unsigned int i = 0; i < trait_path.m_params.m_types.size(); i++) {
                            const auto& l = trait_path.m_params.m_types[i];
                            const auto& r = trait_gp.m_params.m_types[i];
                            cmp &= l->compare_with_placeholders(sp, r, m_ivars.callback_resolve_infer());
                        }
                    }
                    if (cmp != ::HIR::Compare::Unequal) {
                        auto hrls = get_hrls(m_crate.m_types, sp, trait.m_hrtbs, trait_gp.m_params, trait_path.m_params);
                        {
                            auto it = trait.m_type_bounds.find(pe.item);
                            if (it != trait.m_type_bounds.end()) {
                                input = MonomorphHrlsOnly(m_crate.m_types, hrls).monomorph_type(sp, it->second.type);
                                return;
                            }
                        }
                        // Mark as opaque and return, and ensure that the bounds are added to the bounds cache
                        mark_opaque();
                        {
                            auto it = trait.m_trait_bounds.find(pe.item);
                            if (it != trait.m_trait_bounds.end()) {
                                for (const auto& bound : it->second.traits) {
                                    const_cast<TraitResolution&>(*this).prep_indexes__add_trait_bound(sp, nullptr, input, bound.clone());
                                }
                            }
                        }
                        return;
                    }
                }

                // - Check if the desired trait is a supertrait of this.
                // NOTE: `params` (aka des_params) is not used (TODO)
                bool is_supertrait = this->find_named_trait_in_trait(sp, trait_path.m_path, trait_path.m_params, *trait.m_trait_ptr, trait_gp.m_path, trait_gp.m_params, pe.type, [&](const HIR::TraitPath& i_tp) {
                    if (i_tp.m_hrtbs && !i_tp.m_hrtbs->is_empty() && trait.m_hrtbs && !trait.m_hrtbs->is_empty()) {
                        TODO(sp, "Nested HRTBs");
                    }
                    // The above is just the monomorphised params and associated set. Comparison is still needed.
                    auto cmp = this->compare_pp(sp, i_tp.m_path.m_params, pe.trait.m_params);
                    if (cmp != ::HIR::Compare::Unequal) {
                        //auto hrls = get_hrls(sp, trait.m_hrtbs, i_tp.m_path.m_params, trait_path.m_params);
                        auto it = i_tp.m_type_bounds.find(pe.item);
                        if (it == i_tp.m_type_bounds.end()) {
                            // NOTE: (currently) there can only be one trait with this name, so if we found this trait and the item is present - good.
                            it = trait.m_type_bounds.find(pe.item);
                        }
                        if (it != trait.m_type_bounds.end()) {
                            auto hrls = get_hrls(m_crate.m_types, sp, (trait.m_hrtbs && !trait.m_hrtbs->is_empty()) ? trait.m_hrtbs.get() : i_tp.m_hrtbs.get(), i_tp.m_path.m_params, trait_path.m_params);
                            DEBUG("hrls = " << hrls);
                            input = MonomorphHrlsOnly(m_crate.m_types, hrls).monomorph_type(sp, it->second.type);
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
        auto it = m_type_equalities.find(input);
        if (it == m_type_equalities.end()) {
            it = ::std::find_if(m_type_equalities.begin(), m_type_equalities.end(), [&](const auto& entry) {
                return entry.first->equals_ignoring_regions(input);
            });
        }
        if (it != m_type_equalities.end()) {
            result_type = ResultType::Recurse;
            DEBUG("Equality: for" << it->second.hrbs.fmt_args());
            MatchHrls m{m_crate.m_types, &it->second.hrbs};
            input->match_test_generics_fuzz(sp, it->first, HIR::ResolvePlaceholdersNop(), m);
            input = MonomorphHrlsOnly(m_crate.m_types, m.hrls).monomorph_type(sp, it->second.ty);
            rv = true;
        }
    }
    if(!rv)
    {
        rv = this->iterate_bounds_traits(sp, pe.type, trait_path.m_path, [&](HIR::Compare cmp, const ::HIR::TypeRef& bound_type, const ::HIR::GenericPath& bound_trait, const CachedBound& bound_info) -> bool {
            DEBUG("[expand_associated_types_inplace__UfcsKnown] Trait bound - " << bound_type << " : " << bound_trait);
            // 2. Check if the trait (or any supertrait) includes pe.trait
            // TODO: If fuzzy, bail and leave unresolved?
            cmp &= bound_trait.compare_with_placeholders(sp, trait_path, this->m_ivars.callback_resolve_infer());
            //if( cmp != HIR::Compare::Unequal ) {
            if (cmp == HIR::Compare::Equal) {
                auto it = bound_info.assoc.find(pe.item);
                // 1. Check if the bounds include the desired item
                if (it == bound_info.assoc.end()) {
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
        if (const auto* pe_inner_p = te_inner->path.m_data.opt_UfcsKnown()) {
            DEBUG("Checking inner bounds");
            const auto& pe_inner = *pe_inner_p;
            // TODO: Search for equality bounds on this associated type (pe_inner) that match the entire type (pe)
            // - Does simplification of complex associated types
            //
            ::HIR::GenericPath trait_path;
            if (!this->trait_contains_type(sp, pe_inner.trait, this->m_crate.get_trait_by_path(sp, pe_inner.trait.m_path), pe_inner.item.c_str(), trait_path)) {
                BUG(sp, "Cannot find associated type " << pe_inner.item << " anywhere in trait " << pe_inner.trait);
            }
            const auto& trait_ptr = this->m_crate.get_trait_by_path(sp, trait_path.m_path);
            const auto& assoc_ty = trait_ptr.m_types.at(pe_inner.item);

            // Resolve where Self=pe_inner.type (i.e. for the trait this inner UFCS is on)
            auto cb_placeholders_trait = MonomorphStatePtr(m_crate.m_types, &pe_inner.type, &pe_inner.trait.m_params, &pe_inner.params);
            for (const auto& bound : assoc_ty.m_trait_bounds) {
                auto it = bound.m_type_bounds.find(pe.item);
                if (it != bound.m_type_bounds.end()) {
                    auto source_trait = cb_placeholders_trait.monomorph_genericpath(sp, it->second.source_trait, false);
                    auto aty_params = cb_placeholders_trait.monomorph_path_params(sp, it->second.aty_params, false);
                    for (auto& t : source_trait.m_params.m_types) {
                        expand_associated_types_inplace(sp, t, stack);
                    }
                    for (auto& t : aty_params.m_types) {
                        expand_associated_types_inplace(sp, t, stack);
                    }
                    auto cmp = source_trait.compare_with_placeholders(sp, pe.trait, m_ivars.callback_resolve_infer());
                    cmp &= aty_params.compare_with_placeholders(sp, pe.params, m_ivars.callback_resolve_infer());
                    if (cmp == HIR::Compare::Equal) {
                        input = monomorphise_type_needed(it->second.type)
                            ? cb_placeholders_trait.monomorph_type(sp, it->second.type)
                            : it->second.type;
                        DEBUG("- Found replacement from " << source_trait << ": " << input);
                        this->expand_associated_types_inplace(sp, input, stack);
                        return;
                    }
                }

                auto bound_tp = cb_placeholders_trait.monomorph_genericpath(sp, bound.m_path, false);
                for (auto& t : bound_tp.m_params.m_types) {
                    expand_associated_types_inplace(sp, t, stack);
                }
                DEBUG("B " << bound.m_path);
                DEBUG("-> " << bound_tp);

                // TODO: Find trait in this trait.
                const auto& bound_trait = m_crate.get_trait_by_path(sp, bound_tp.m_path);
                bool replaced = this->find_named_trait_in_trait(sp, pe.trait.m_path, pe.trait.m_params, bound_trait, bound_tp.m_path, bound_tp.m_params, pe.type, [&](const HIR::TraitPath& tp) {
                    auto it = tp.m_type_bounds.find(pe.item);
                    if (it != tp.m_type_bounds.end()) {
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
            trait_path.m_path,
            trait_path.m_params,
            pe.type,
            [&](ImplRef impl, ::HIR::Compare certainty) {
                if (impl.is_ambiguous_identity()
                    || certainty == ::HIR::Compare::Fuzzy) {
                    ambiguous = true;
                    return true;
                }

                auto output = impl.get_type(m_crate.m_types, pe.item.c_str(), pe.params);
                if (output == ::HIR::TypeRef() || output == input) {
                    ambiguous = true;
                    return true;
                }
                input = ::std::move(output);
                normalized = true;
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
            if (!this->m_ivars.type_contains_ivars(input, false)) {
                mark_opaque();
            }
            return;
        }
    }

    if( this->find_trait_impls_magic(sp, trait_path.m_path, trait_path.m_params, pe.type, [&](auto impl, auto qual)->bool {
        DEBUG("[expand_associated_types__UfcsKnown] Found " << impl << " qual=" << qual);
        // If it's a fuzzy match, keep going (but count if a concrete hasn't been found)
        if (qual == ::HIR::Compare::Fuzzy) {
        } else {
            auto ty = impl.get_type(m_crate.m_types, pe.item.c_str(), pe.params);
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

    if( this->find_trait_impls_types(sp, trait_path.m_path, trait_path.m_params, pe.type, [&](auto impl, auto qual)->bool {
        DEBUG("[expand_associated_types__UfcsKnown] Found " << impl << " qual=" << qual);
        // If it's a fuzzy match, keep going (but count if a concrete hasn't been found)
        if (qual == ::HIR::Compare::Fuzzy) {
        } else {
            auto ty = impl.get_type(m_crate.m_types, pe.item.c_str(), pe.params);
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
    bool    can_fuzz = true;
    unsigned int    count = 0;
    bool is_specialisable = false;
    bool is_bound = false;
    ImplRef best_impl;
    auto cb_find_impl = [&](ImplRef impl, HIR::Compare qual)->bool {
        DEBUG("[expand_associated_types__UfcsKnown] Found " << impl << " qual=" << qual);
        // If it's a fuzzy match, keep going (but count if a concrete hasn't been found)
        if (qual == ::HIR::Compare::Fuzzy) {
            if (can_fuzz) {
                count += 1;
                if (count == 1 && impl.get_impl_type(m_crate.m_types)->tag() == pe.type->tag()) {
                    best_impl = mv$(impl);
                }
            }
            return false;
        } else {
            // If a fuzzy match could have been seen, ensure that best_impl is unsed
            if (can_fuzz) {
                best_impl = ImplRef();
                can_fuzz = false;
            }

            // If the type is specialisable
            if (impl.type_is_specialisable(pe.item.c_str())) {
                // Check if this is more specific
                if (impl.more_specific_than(m_crate.m_types, best_impl)) {
                    is_specialisable = true;
                    best_impl = mv$(impl);
                }
                return false;
            } else {
                auto ty = impl.get_type(m_crate.m_types, pe.item.c_str(), pe.params);
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

    rv = this->find_trait_impls_crate(sp, trait_path.m_path, trait_path.m_params, pe.type, cb_find_impl);
    if( !rv ) {
        is_bound = true;
        rv = find_trait_impls_bound(sp, trait_path.m_path, trait_path.m_params, pe.type, cb_find_impl);
    }
    if( !rv && best_impl.is_valid() ) {
        if (can_fuzz && count > 1) {
            // Fuzzy match with multiple choices - can't know yet
        } else if (is_specialisable) {
            if (!this->m_ivars.type_contains_ivars(input, false)) {
                DEBUG("Assuming opaque - specialisable impl");
                mark_opaque();
            } else {
                DEBUG("Derferring - specialisable impl (ivars present)");
            }
            return;
        } else {
            auto ty = best_impl.get_type(m_crate.m_types, pe.item.c_str(), pe.params);
            if (ty == ::HIR::TypeRef()) {
                if (!this->m_ivars.type_contains_ivars(input, false)) {
                    DEBUG("Assuming opaque - best impl didn't have ATY");
                    mark_opaque();
                } else {
                    DEBUG("Derferring - best impl didn't have ATY (ivars present)");
                }
                return;
                //ERROR(sp, E0000, "Couldn't find assocated type " << pe.item << " in impl of " << pe.trait << " for " << pe.type);
            }

            // Try again later?
            if (best_impl.has_magic_params()) {
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
                        [](const HIR::TypeRef& ty) {
                    return ty->is_ErasedType() || ty->is_Infer();
                }
                    ) || monomorphise_type_needed(input),
                    "Set opaque on a non-generic type: " << input
                );

                DEBUG("- " << m_type_equalities.size() << " replacements");
                for (const auto& v : m_type_equalities) {
                    DEBUG(" > " << v.first << " = " << v.second);
                }

                auto a = m_type_equalities.find(input);
                if (a == m_type_equalities.end()) {
                    a = ::std::find_if(m_type_equalities.begin(), m_type_equalities.end(), [&](const auto& entry) {
                        return entry.first->equals_ignoring_regions(input);
                    });
                }
                if (a != m_type_equalities.end()) {
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
    if( !this->m_ivars.type_contains_ivars(input, false) ) {
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
        bool TraitResolution::find_named_trait_in_trait(const Span& sp, const ::HIR::SimplePath& des, const ::HIR::PathParams& des_params, const ::HIR::Trait& trait_ptr, const ::HIR::SimplePath& trait_path, const ::HIR::PathParams& pp, const ::HIR::TypeRef& target_type, t_cb_find_trait callback) const {
            TRACE_FUNCTION_F(des << des_params << " in " << trait_path << pp);
            if (pp.m_types.size() != trait_ptr.m_params.m_types.size()) {
                BUG(sp, "Incorrect number of parameters for trait " << trait_path);
            }

            DEBUG(trait_ptr.m_all_parent_traits);
            auto monomorph_cb = MonomorphStatePtr(m_crate.m_types, &target_type, &pp, nullptr);
            for (const auto& pt : trait_ptr.m_all_parent_traits) {
                auto pt_mono = monomorph_cb.monomorph_traitpath(sp, pt, false);
                for (auto& ty : pt_mono.m_path.m_params.m_types) {
                    ty = this->expand_associated_types(sp, mv$(ty));
                }
                for (auto& ty : pt_mono.m_type_bounds) {
                    ty.second.type = this->expand_associated_types(sp, mv$(ty.second.type));
                }

                //DEBUG(pt << " => " << pt_mono);
                if (pt.m_path.m_path == des) {
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

        bool TraitResolution::find_trait_impls_bound(const Span& sp, const ::HIR::SimplePath& trait, const ::HIR::PathParams& params, const ::HIR::TypeRef& type, t_cb_trait_impl_r callback) const {
            TRACE_FUNCTION_F("trait = " << trait << params << ", type = " << type);
            const ::HIR::Path::Data::Data_UfcsKnown* assoc_info = nullptr;
            if (const auto* e = type->opt_Path()) {
                assoc_info = e->path.m_data.opt_UfcsKnown();
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
                bool rv = this->iterate_bounds_traits(sp, type, trait, [&](HIR::Compare cmp, const HIR::TypeRef& bound_ty, const ::HIR::GenericPath& bound_trait, const CachedBound& bound_info) -> bool {
                    const auto& stored_params = bound_trait.m_params;
                    ::HIR::PathParams normalised_params;
                    const ::HIR::PathParams* b_params = &stored_params;
                    if (::std::any_of(
                            stored_params.m_types.begin(), stored_params.m_types.end(),
                            [&](const auto& ty) { return this->has_associated_type(ty); }
                        )) {
                        normalised_params = stored_params.clone();
                        this->expand_associated_types_params(sp, normalised_params);
                        b_params = &normalised_params;
                    }

                    DEBUG("[find_trait_impls_bound] " << bound_trait << " for " << bound_ty << " cmp = " << cmp);

                    // Check against `params`
                    DEBUG("[find_trait_impls_bound] Checking params " << params << " vs " << *b_params);
                    auto ord = cmp;
                    ord &= this->compare_pp(sp, *b_params, params);
                    if (ord == ::HIR::Compare::Unequal) {
                        DEBUG("[find_trait_impls_bound] - Mismatch");
                        return false;
                    }
                    if (ord == ::HIR::Compare::Fuzzy) {
                        DEBUG("[find_trait_impls_bound] - Fuzzy match");
                    }
                    DEBUG("[find_trait_impls_bound] Match for" << bound_info.hrbs.fmt_args() << " " << bound_ty << " : " << bound_trait);
                    // Hand off to the closure, and return true if it does
                    // TODO: The type bounds are only the types that are specified.
                    auto hrls = get_hrls(m_crate.m_types, sp, bound_info.hrbs, *b_params, params);
                    if (callback(ImplRef(std::move(hrls), &bound_ty, &bound_trait.m_params, &bound_info.assoc), ord)) {
                        return true;
                    }

                    return false;
                });
                if (rv) {
                    return rv;
                }
            }

            if (assoc_info) {
                bool rv = this->iterate_bounds_traits(sp, assoc_info->type, assoc_info->trait.m_path, [&](HIR::Compare cmp, const HIR::TypeRef& bound_ty, const ::HIR::GenericPath& bound_trait, const CachedBound& bound_info) -> bool {
                    // Check the trait params
                    cmp &= this->compare_pp(sp, bound_trait.m_params, assoc_info->trait.m_params);
                    if (cmp == ::HIR::Compare::Fuzzy) {
                        //TODO(sp, "Handle fuzzy matches searching for associated type bounds");
                    } else if (cmp == ::HIR::Compare::Unequal) {
                        return false;
                    }
                    auto outer_ord = cmp;

                    const auto& trait_ref = *bound_info.trait_ptr;
                    const auto& at = trait_ref.m_types.at(assoc_info->item);
                    for (const auto& bound : at.m_trait_bounds) {
                        if (bound.m_path.m_path == trait) {
                            auto monomorph_cb = MonomorphStatePtr(m_crate.m_types, &assoc_info->type, &assoc_info->trait.m_params, nullptr);

                            DEBUG("- Found an associated type bound for this trait via another bound");
                            ::HIR::Compare ord = outer_ord;
                            if (monomorphise_pathparams_needed(bound.m_path.m_params)) {
                                // TODO: Use a compare+callback method instead
                                auto b_params_mono = monomorph_cb.monomorph_path_params(sp, bound.m_path.m_params, false);
                                this->expand_associated_types_params(sp, b_params_mono);
                                ord &= this->compare_pp(sp, b_params_mono, params);
                            } else {
                                ord &= this->compare_pp(sp, bound.m_path.m_params, params);
                            }
                            if (ord == ::HIR::Compare::Unequal) {
                                return false;
                            }
                            if (ord == ::HIR::Compare::Fuzzy) {
                                DEBUG("Fuzzy match");
                            }

                            auto tp_mono = monomorph_cb.monomorph_traitpath(sp, bound, false);
                            if (tp_mono.m_hrtbs) {
                                auto p = tp_mono.m_hrtbs->make_empty_params(true);
                                tp_mono = MonomorphHrlsOnly(m_crate.m_types, p).monomorph_traitpath(sp, tp_mono, true, true);
                            }
                            // - Expand associated types
                            this->expand_associated_types_params(sp, tp_mono.m_path.m_params);
                            for (auto& ty : tp_mono.m_type_bounds) {
                                ty.second.type = this->expand_associated_types(sp, mv$(ty.second.type));
                            }
                            DEBUG("- tp_mono = " << tp_mono);
                            // TODO: Instead of using `type` here, build the real type
                            if (callback(ImplRef(type, mv$(tp_mono.m_path.m_params), mv$(tp_mono.m_type_bounds)), ord)) {
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

        bool TraitResolution::find_trait_impls_crate(const Span& sp, const ::HIR::SimplePath& trait, const ::HIR::PathParams* params_ptr, const ::HIR::TypeRef& type, t_cb_trait_impl_r callback) const {
            // TODO: Have a global cache of impls that don't reference either generics or ivars

            static ::HIR::TraitPath::assoc_list_t null_assoc;
            TRACE_FUNCTION_F(trait << FMT_CB(ss, if (params_ptr) { ss << *params_ptr; } else { ss << "<?>"; }) << " for " << type);

            CanonicalizeTraitGoal canonicalizer(m_crate.m_types);
            const auto canonical_type = canonicalizer.monomorph_type(sp, type, true);
            ::HIR::PathParams canonical_params;
            const bool has_params = params_ptr != nullptr;
            if (has_params) {
                canonical_params = canonicalizer.monomorph_path_params(
                    sp, *params_ptr, true
                );
            }

            for (const auto& active_goal : m_legacy_trait_goal_stack) {
                if (!active_goal.matches(
                        trait, canonical_params, has_params, canonical_type
                    )) {
                    continue;
                }

                // rustc treats an inductive recursive trait predicate as
                // ambiguous, not proven.  Auto traits remain coinductive.
                const auto cmp = m_crate.get_trait_by_path(sp, trait).m_is_marker
                    ? ::HIR::Compare::Equal
                    : ::HIR::Compare::Fuzzy;
                DEBUG("Legacy trait goal recurred: " << trait
                    << FMT_CB(ss, if (params_ptr) { ss << *params_ptr; } else { ss << "<?>"; })
                    << " for " << type << ", result=" << cmp);
                return callback(ImplRef(&type, params_ptr, &null_assoc), cmp);
            }

            // rustc's legacy solver has a second cycle check for fresh input
            // types.  Exact goal equality is not sufficient here: a blanket
            // candidate can replace one unknown with a newly-created unknown
            // on every step (for example, tuple Distribution impls).  If the
            // current fresh goal is compatible with an older goal for the
            // same trait, further candidate search is ambiguous.
            const auto type_is_fresh = [&](const ::HIR::TypeRef& ty) {
                if (m_ivars.type_contains_ivars(ty, false)) {
                    return true;
                }
                return visit_ty_with(ty, [](const ::HIR::TypeRef& inner) {
                    return inner->is_Generic()
                        && inner->as_Generic().is_placeholder();
                });
            };
            bool has_fresh_inputs = !has_params || type_is_fresh(type);
            if (has_params && !has_fresh_inputs) {
                has_fresh_inputs = m_ivars.pathparams_contain_ivars(
                    *params_ptr, false
                );
                for (const auto& param : params_ptr->m_types) {
                    has_fresh_inputs = has_fresh_inputs || type_is_fresh(param);
                }
                for (const auto& param : params_ptr->m_values) {
                    has_fresh_inputs = has_fresh_inputs
                        || param.is_Infer()
                        || (param.is_Generic()
                            && param.as_Generic().is_placeholder());
                }
            }

            if (has_fresh_inputs) {
                const auto resolve = m_ivars.callback_resolve_infer();
                for (const auto& active_goal : m_legacy_trait_goal_stack) {
                    if (active_goal.trait != trait) {
                        continue;
                    }
                    if (canonical_type->compare_with_placeholders(
                            sp, active_goal.type, resolve
                        ) == ::HIR::Compare::Unequal) {
                        continue;
                    }
                    if (has_params && active_goal.has_params
                        && canonical_params.compare_with_placeholders(
                            sp, active_goal.params, resolve
                        ) == ::HIR::Compare::Unequal) {
                        continue;
                    }

                    DEBUG("Fresh legacy trait goal matched an active goal: "
                        << trait
                        << FMT_CB(ss, if (params_ptr) { ss << *params_ptr; } else { ss << "<?>"; })
                        << " for " << type << ", result=Fuzzy");
                    return callback(
                        ImplRef(&type, params_ptr, &null_assoc),
                        ::HIR::Compare::Fuzzy
                    );
                }
            }

            m_legacy_trait_goal_stack.emplace_back(
                trait, canonical_params, has_params, canonical_type
            );
            struct StackGuard {
                ::std::vector<LegacyTraitGoal>& stack;
                ~StackGuard() {
                    stack.pop_back();
                }
            } guard{m_legacy_trait_goal_stack};

            // Handle auto traits (aka OIBITs)
            if (m_crate.get_trait_by_path(sp, trait).m_is_marker) {

                // NOTE: Expected behavior is for Ivars to return false
                // TODO: Should they return Compare::Fuzzy instead?
                if (type->is_Infer()) {
                    return callback(ImplRef(&type, params_ptr, &null_assoc), ::HIR::Compare::Fuzzy);
                }

                const ::HIR::TraitMarkings* markings = nullptr;
                if (const auto* e = type->opt_Path()) {
                    if (TU_TEST1(e->path.m_data, Generic, .m_params.m_types.size() == 0)) {
                        markings = e->binding.get_trait_markings();
                    }
                }

                // NOTE: `markings` is only set if there's no type params to a path type
                // - Cache populated after destructure
                if (markings) {
                    auto it = markings->auto_impls.find(trait);
                    if (it != markings->auto_impls.end()) {
                        if (!it->second.conditions.empty()) {
                            TODO(sp, "Conditional auto trait impl");
                        } else if (it->second.is_impled) {
                            return callback(ImplRef(&type, params_ptr, &null_assoc), ::HIR::Compare::Equal);
                        } else {
                            return false;
                        }
                    }
                }

                // - Search for positive impls for this type
                DEBUG("- Search positive impls");
                bool positive_found = false;
                this->m_crate.find_auto_trait_impls(trait, type, this->m_ivars.callback_resolve_infer(), [&](const auto& impl) -> bool {
                    // Skip any negative impls on this pass
                    if (impl.is_positive != true) {
                        return false;
                    }

                    DEBUG("[find_trait_impls_crate] - Auto Pos Found impl" << impl.m_params.fmt_args() << " " << trait << impl.m_trait_args << " for " << impl.m_type << " " << impl.m_params.fmt_bounds());

                    // Compare with `params`
                    HIR::PathParams impl_params;
                    auto match = this->ftic_check_params(sp, trait, params_ptr, type, impl.m_params, impl.m_trait_args, impl.m_type, impl_params);
                    if (match == ::HIR::Compare::Unequal) {
                        // If any bound failed, return false (continue searching)
                        return false;
                    }

                    auto monomorph = MonomorphStatePtr(m_crate.m_types, nullptr, &impl_params, nullptr);
                    // TODO: Ensure that there are no-longer any magic params?

                    auto ty_mono = monomorph.monomorph_type(sp, impl.m_type, false);
                    auto args_mono = monomorph.monomorph_path_params(sp, impl.m_trait_args, false);
                    // NOTE: Auto traits can't have items, so no associated types

                    positive_found = true;
                    DEBUG("[find_trait_impls_crate] Auto Positive callback(args=" << args_mono << ")");
                    return callback(ImplRef(mv$(ty_mono), mv$(args_mono), {}), match);
                });
                if (positive_found) {
                    // A positive impl was found, so return true (callback should have been called)
                    return true;
                }

                // - Search for negative impls for this type
                DEBUG("- Search negative impls");
                bool negative_found = this->m_crate.find_auto_trait_impls(trait, type, this->m_ivars.callback_resolve_infer(), [&](const auto& impl) {
                    // Skip any positive impls
                    if (impl.is_positive != false) {
                        return false;
                    }
                    DEBUG("[find_trait_impls_crate] - Found auto neg impl" << impl.m_params.fmt_args() << " " << trait << impl.m_trait_args << " for " << impl.m_type << " " << impl.m_params.fmt_bounds());

                    // Compare with `params`
                    HIR::PathParams impl_params;
                    auto match = this->ftic_check_params(sp, trait, params_ptr, type, impl.m_params, impl.m_trait_args, impl.m_type, impl_params);
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

                auto cmp = this->check_auto_trait_impl_destructure(sp, trait, params_ptr, type);
                if (cmp != ::HIR::Compare::Unequal) {
                    if (markings) {
                        ASSERT_BUG(sp, cmp == ::HIR::Compare::Equal, "Auto trait with no params returned a fuzzy match from destructure - " << trait << " for " << type);
                        markings->auto_impls.insert(::std::make_pair(trait, ::HIR::TraitMarkings::AutoMarking{{}, true}));
                    }
                    return callback(ImplRef(&type, params_ptr, &null_assoc), cmp);
                } else {
                    if (markings) {
                        markings->auto_impls.insert(::std::make_pair(trait, ::HIR::TraitMarkings::AutoMarking{{}, false}));
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
                return this->m_crate.find_trait_impls(trait, type, this->m_ivars.callback_resolve_infer(), [&](const auto& impl) {
                    HIR::PathParams impl_params;
                    // Fill all params with placeholders?
                    return callback(ImplRef(mv$(impl_params), trait, impl), HIR::Compare::Fuzzy);
                });
            }
#endif

            return this->m_crate.find_trait_impls(trait, type, this->m_ivars.callback_resolve_infer(), [&](const HIR::TraitImpl& impl) {
                DEBUG("[find_trait_impls_crate] Found impl" << impl.m_params.fmt_args() << " " << trait << impl.m_trait_args << " for " << impl.m_type << " " << impl.m_params.fmt_bounds());
                // Compare with `params`
                HIR::PathParams impl_params;
                auto match = this->ftic_check_params(sp, trait, params_ptr, type, impl.m_params, impl.m_trait_args, impl.m_type, impl_params);
                if (match == ::HIR::Compare::Unequal) {
                    // If any bound failed, return false (continue searching)
                    DEBUG("[find_trait_impls_crate] - Params mismatch");
                    return false;
                }
                DEBUG("[find_trait_impls_crate] - Found with impl_params=" << impl_params);

                return callback(ImplRef(mv$(impl_params), m_crate.get_trait_by_path(sp, trait), trait, impl), match);
            });
        }

        ::HIR::Compare TraitResolution::check_auto_trait_impl_destructure(const Span& sp, const ::HIR::SimplePath& trait, const ::HIR::PathParams* params_ptr, const ::HIR::TypeRef& type) const {
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
        TU_MATCH_HDRA( (e.path.m_data), {)
        TU_ARMA(Generic, pe) { //(
                    ::HIR::TypeRef tmp;
                    auto monomorph = MonomorphStatePtr(m_crate.m_types, nullptr, &pe.m_params, nullptr);
                    // HELPER: Get a possibily monomorphised version of the input type (stored in `tmp` if needed)
                    auto monomorph_get = [&](const auto& ty) -> const ::HIR::TypeRef& {
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
                                (str.m_data),
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
                            if (const auto* e = tpb->m_data.opt_Data()) {
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
                            for (const auto& fld : tpb->m_variants) {
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
            const ::HIR::TypeRef& type,
            const ::HIR::GenericParams& impl_params_def,
            const ::HIR::PathParams& impl_trait_args,
            const ::HIR::TypeRef& impl_ty,
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

                ::HIR::Compare match_ty(const ::HIR::GenericRef& g, const ::HIR::TypeRef& ty, ::HIR::t_cb_resolve_type resolve_cb) override {
                    assert(g.binding < out_impl_params.m_types.size());
                    if (out_impl_params.m_types[g.binding] == HIR::TypeRef()) {
                        DEBUG("[ftic_check_params] Param " << g.binding << " = " << ty);
                        out_impl_params.m_types[g.binding] = ty;
                        return ::HIR::Compare::Equal;
                    } else {
                        DEBUG("[ftic_check_params] Param " << g.binding << " " << out_impl_params.m_types[g.binding] << " == " << ty);
                        auto rv = out_impl_params.m_types[g.binding]->compare_with_placeholders(sp, ty, resolve_cb);
                        // If the existing is an ivar, replace with this.
                        // - TODO: Store the least fuzzy option, or store all fuzzy options?
                        if (rv == ::HIR::Compare::Fuzzy && out_impl_params.m_types[g.binding]->is_Infer()) {
                            // The same impl parameter can be learned through more than one
                            // component of an impl header.  `Y = X` followed by `Y = &X`
                            // is not a fuzzy refinement: it would require the infinite type
                            // `X = &X`.  Treat that header as disjoint instead of replacing
                            // the first constraint and letting specialization pick it.
                            const auto& existing_resolved = resolve_cb.get_type(sp, out_impl_params.m_types[g.binding]);
                            const auto* existing_infer = existing_resolved->opt_Infer();
                            if (existing_infer && existing_infer->index != ~0u) {
                                const bool recursive = visit_ty_with(ty, [&](const ::HIR::TypeRef& inner) {
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
                            out_impl_params.m_types[g.binding] = ty;
                        }
                        return rv;
                    }
                }

                ::HIR::Compare match_val(const ::HIR::GenericRef& g, const ::HIR::ConstGeneric& sz) override {
                    ASSERT_BUG(sp, g.binding < out_impl_params.m_values.size(), "Value generic " << g << " out of range (" << out_impl_params.m_values.size() << ")");
                    if (sz.is_Infer()) {
                        ASSERT_BUG(sp, sz.as_Infer().index != ~0u, "");
                    }
                    if (out_impl_params.m_values[g.binding] == HIR::ConstGeneric()) {
                        DEBUG("[ftic_check_params] Value param " << g.binding << " = " << sz);
                        out_impl_params.m_values[g.binding] = sz.clone();
                        return ::HIR::Compare::Equal;
                    } else {
                        if (out_impl_params.m_values[g.binding] == sz) {
                            return ::HIR::Compare::Equal;
                        }
                        if (out_impl_params.m_values[g.binding].is_Infer()) {
                            if (!sz.is_Infer()) {
                                DEBUG("[ftic_check_params] Value param " << g.binding << " fuzzy, use " << sz);
                                out_impl_params.m_values[g.binding] = sz.clone();
                            }
                            return ::HIR::Compare::Fuzzy;
                        }
                        if (sz.is_Infer()) {
                            return ::HIR::Compare::Fuzzy;
                        }
                        TODO(Span(), "PtrImplMatcher::match_val " << g << "(" << out_impl_params.m_values[g.binding] << ") with " << sz);
                    }
                }
            };

            GetParams get_params{sp, out_impl_params};

            out_impl_params.m_lifetimes.resize(impl_params_def.m_lifetimes.size());
            out_impl_params.m_types.resize(impl_params_def.m_types.size());
            out_impl_params.m_values.resize(impl_params_def.m_values.size());

            // NOTE: If this type references an associated type, the match will incorrectly fail.
            // - HACK: match_test_generics_fuzz has been changed to return Fuzzy if there's a tag mismatch and the LHS is an Opaque path
            auto match = ::HIR::Compare::Equal;
            match &= impl_ty->match_test_generics_fuzz(sp, type, this->m_ivars.callback_resolve_infer(), get_params);
            if (params_ptr) {
                const auto& params = *params_ptr;
                match &= impl_trait_args.match_test_generics_fuzz(sp, params, this->m_ivars.callback_resolve_infer(), get_params);
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
                for (const auto& ty : out_impl_params.m_types) {
                    if (ty == HIR::TypeRef()) {
                        placeholders_needed = true;
                    }
                }
                for (const auto& val : out_impl_params.m_values) {
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
                    << m_fresh_impl_placeholder_counter++
                ));
                for (unsigned int i = 0; i < out_impl_params.m_types.size(); i++) {
                    if (out_impl_params.m_types[i] == HIR::TypeRef()) {
                        if (placeholders.m_types.size() == 0) {
                            placeholders.m_types.resize(out_impl_params.m_types.size());
                        }
                        placeholders.m_types[i] = m_crate.m_types.generic(placeholder_name, 2 * 256 + i);
                        DEBUG("Create placeholder type for " << i << " = " << placeholders.m_types[i]);
                    }
                }
                for (unsigned int i = 0; i < out_impl_params.m_values.size(); i++) {
                    if (out_impl_params.m_values[i] == HIR::ConstGeneric()) {
                        if (placeholders.m_values.size() == 0) {
                            placeholders.m_values.resize(out_impl_params.m_values.size());
                        }
                        placeholders.m_values[i] = ::HIR::GenericRef(placeholder_name, 2 * 256 + i);
                        DEBUG("Create placeholder value for " << i << " = " << placeholders.m_values[i]);
                    }
                }
                DEBUG("Placeholders (" << placeholder_name << "): " << placeholders);
            } else {
                DEBUG("Placeholders not needed");
            }

            if (!evaluate_bounds) {
                for (size_t i = 0; i < out_impl_params.m_types.size(); i++) {
                    if (out_impl_params.m_types[i] == HIR::TypeRef()) {
                        out_impl_params.m_types[i] = ::std::move(placeholders.m_types[i]);
                    }
                }
                for (size_t i = 0; i < out_impl_params.m_values.size(); i++) {
                    if (out_impl_params.m_values[i] == HIR::ConstGeneric()) {
                        out_impl_params.m_values[i] = ::std::move(placeholders.m_values[i]);
                    }
                }
                return match;
            }
            auto cb_infer = m_ivars.callback_resolve_infer();

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

                ::HIR::Compare match_ty(const ::HIR::GenericRef& g, const ::HIR::TypeRef& ty, ::HIR::t_cb_resolve_type resolve_cb) override {
                    if (const auto* e = ty->opt_Generic()) {
                        if (e->binding == g.binding && e->name == g.name) {
                            return ::HIR::Compare::Equal;
                        }
                    }
                    if (g.is_placeholder() && g.name == placeholder_name) {
                        auto i = g.idx();
                        ASSERT_BUG(sp, impl_params.m_types[i] == HIR::TypeRef(), "Placeholder to populated type returned - " << impl_params.m_types[i] << " vs " << ty);
                        auto& ph = placeholders.m_types[i];
                        // TODO: Only want to do this if ... what?
                        // - Problem: This can poison the output if the result was fuzzy
                        // - E.g. `Q: Borrow<V>` can equate Q and V
                        if (ph->is_Generic() && ph->as_Generic().binding == g.binding) {
                            DEBUG("[ftic_check_params:cb_match] Bind placeholder " << i << " to " << ty);
                            ph = ty;
                            return ::HIR::Compare::Equal;
                        } else {
                            DEBUG("[ftic_check_params:cb_match] Compare placeholder " << i << " " << ph << " == " << ty);
                            return ph->compare_with_placeholders(sp, ty, resolve_cb);
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
                        ASSERT_BUG(sp, impl_params.m_values[i] == HIR::ConstGeneric(), "Placeholder to populated value returned - " << impl_params.m_values[i] << " vs " << v);
                        auto& ph = placeholders.m_values[i];
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
                    if (impl_params.m_types.at(ge.binding) != HIR::TypeRef()) {
                        return impl_params.m_types.at(ge.binding);
                    }
                    ASSERT_BUG(sp, placeholders.m_types.size() == impl_params.m_types.size(), "Placeholder size mismatch: " << placeholders.m_types.size() << " != " << impl_params.m_types.size());
                    return placeholders.m_types.at(ge.binding);
                }

                ::HIR::ConstGeneric get_value(const Span& sp, const ::HIR::GenericRef& val) const override {
                    ASSERT_BUG(sp, val.binding < 256, "Generic value binding in " << val << " out of range (>=256)");
                    ASSERT_BUG(sp, val.binding < impl_params.m_values.size(), "Generic value binding in " << val << " out of range (>= " << impl_params.m_values.size() << ")");
                    if (impl_params.m_values.at(val.binding) != HIR::ConstGeneric()) {
                        return impl_params.m_values.at(val.binding).clone();
                    }
                    ASSERT_BUG(sp, placeholders.m_values.size() == impl_params.m_values.size(), "Placeholder size mismatch: " << placeholders.m_values.size() << " != " << impl_params.m_values.size());
                    return placeholders.m_values.at(val.binding).clone();
                }

                ::HIR::LifetimeRef get_lifetime(const Span& sp, const ::HIR::GenericRef& g) const override {
                    ASSERT_BUG(sp, g.binding < 256, "Generic lifetime binding in " << g << " out of range (>=256)");
                    ASSERT_BUG(sp, g.binding < impl_params.m_lifetimes.size(), "Generic lifetime binding in " << g << " out of range (>= " << impl_params.m_lifetimes.size() << ")");
                    return impl_params.m_lifetimes.at(g.binding);
                }
            };

            Matcher matcher{m_crate.m_types, sp, out_impl_params, placeholder_name, placeholders};

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
                for (const auto& bound : impl_params_def.m_bounds) {
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
                    if (real_trait.m_hrtbs) {
                        auto p = real_trait.m_hrtbs->make_empty_params(true);
                        real_trait.m_hrtbs.reset();
                        real_trait = MonomorphHrlsOnly(m_crate.m_types, p).monomorph_traitpath(sp, real_trait, true);
                    }
                    real_type = this->expand_associated_types(sp, mv$(real_type));
                    for (auto& p : real_trait.m_path.m_params.m_types) {
                        p = this->expand_associated_types(sp, mv$(p));
                    }
                    for (auto& ab : real_trait.m_type_bounds) {
                        ab.second.type = this->expand_associated_types(sp, mv$(ab.second.type));
                    }
                    const auto& real_trait_path = real_trait.m_path;
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
                    auto rv = this->find_trait_impls(sp, real_trait_path.m_path, real_trait_path.m_params, real_type, [&](auto impl, auto impl_cmp) {
                        // TODO: Save and restore placeholders if this isn't a full match
                        DEBUG("[ftic_check_params] impl_cmp = " << impl_cmp << ", impl = " << impl);
                        auto cmp = impl_cmp;
                        if (cmp == ::HIR::Compare::Fuzzy) {
                            // If the match was fuzzy, try again filling in with `cb_match`
                            auto i_ty = impl.get_impl_type(m_crate.m_types);
                            this->expand_associated_types_inplace(sp, i_ty, {});
                            auto i_tp = impl.get_trait_params(m_crate.m_types);
                            for (auto& t : i_tp.m_types) {
                                this->expand_associated_types_inplace(sp, t, {});
                            }
                            DEBUG("[ftic_check_params] " << real_type << " ?= " << i_ty);
                            cmp &= real_type->match_test_generics_fuzz(sp, i_ty, cb_infer, matcher);
                            DEBUG("[ftic_check_params] " << real_trait_path.m_params << " ?= " << i_tp);
                            cmp &= real_trait_path.m_params.match_test_generics_fuzz(sp, i_tp, cb_infer, matcher);
                            DEBUG("[ftic_check_params] - Re-check result: " << cmp);
                        }
                        for (const auto& assoc_bound : real_trait.m_type_bounds) {
                            ::HIR::TypeRef tmp;
                            const ::HIR::TypeRef* ty_p;

                            tmp = impl.get_type(m_crate.m_types, assoc_bound.first.c_str(), assoc_bound.second.aty_params);
                            if (tmp == ::HIR::TypeRef()) {
                                // This bound isn't from this particular trait, go the slow way of using expand_associated_types
                                tmp = this->expand_associated_types(sp, m_crate.m_types.path(::HIR::Path(::HIR::Path::Data::Data_UfcsKnown{real_type, real_trait_path.clone(), assoc_bound.first, {}}), {}));
                                ty_p = &tmp;
                            } else {
                                // Expand after extraction, just to make sure.
                                this->expand_associated_types_inplace(sp, tmp, {});
                                ty_p = &this->m_ivars.get_type(tmp);
                            }
                            const auto& ty = *ty_p;
                            DEBUG("[ftic_check_params] - Compare " << ty << " and " << assoc_bound.second.type << ", matching generics");
                            // `ty` = Monomorphised actual type (< `be.type` as `be.trait` >::`assoc_bound.first`)
                            // `assoc_bound.second` = Desired type (monomorphised too)
                            auto cmp_i = assoc_bound.second.type->match_test_generics_fuzz(sp, ty, cb_infer, matcher);
                            switch (cmp_i) {
                                case ::HIR::Compare::Equal:
                                    DEBUG("Equal");
                                    break;
                                case ::HIR::Compare::Unequal:
                                    DEBUG("Assoc `" << assoc_bound.first << "` didn't match - " << ty << " != " << assoc_bound.second.type);
                                    cmp = ::HIR::Compare::Unequal;
                                    break;
                                case ::HIR::Compare::Fuzzy:
                                    // TODO: When a fuzzy match is encountered on a conditional bound, returning `false` can lead to an false negative (and a compile error)
                                    // BUT, returning `true` could lead to it being selected. (Is this a problem, should a later validation pass check?)
                                    DEBUG("[ftic_check_params] Fuzzy match assoc bound between " << ty << " and " << assoc_bound.second.type);
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
                            placeholders.m_types.resize(fuzzy_ph.m_types.size());
                            placeholders.m_values.resize(fuzzy_ph.m_values.size());
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

            for (size_t i = 0; i < out_impl_params.m_types.size(); i++) {
                if (out_impl_params.m_types[i] == HIR::TypeRef()) {
                    out_impl_params.m_types[i] = std::move(placeholders.m_types[i]);
                }
                ASSERT_BUG(sp, out_impl_params.m_types[i] != HIR::TypeRef(), "");
            }
            for (size_t i = 0; i < out_impl_params.m_values.size(); i++) {
                if (out_impl_params.m_values[i] == HIR::ConstGeneric()) {
                    out_impl_params.m_values[i] = std::move(placeholders.m_values[i]);
                }
                ASSERT_BUG(sp, out_impl_params.m_values[i] != HIR::ConstGeneric(), "");
            }

            for (size_t i = 0; i < impl_params_def.m_types.size(); i++) {
                if (impl_params_def.m_types.at(i).m_is_sized) {
                    if (out_impl_params.m_types[i] != HIR::TypeRef()) {
                        auto cmp = type_is_sized(sp, out_impl_params.m_types[i]);
                        if (cmp == ::HIR::Compare::Unequal) {
                            DEBUG("- Sized bound failed for " << out_impl_params.m_types[i]);
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
                auto it = trait_ptr.m_values.find(name);
                if (it != trait_ptr.m_values.end()) {
                    if (it->second.is_Function()) {
                        const auto& v = it->second.as_Function();
                        out_fcn_ptr = &v;
                        return true;
                    }
                }
                return false;
            }
        }

        const ::HIR::Function* TraitResolution::trait_contains_method(const Span& sp, const ::HIR::GenericPath& trait_path, const ::HIR::Trait& trait_ptr, const ::HIR::TypeRef& self, const RcString& name, ::HIR::GenericPath& out_path) const {
            TRACE_FUNCTION_FR("trait_path=" << trait_path << ",name=" << name, out_path);
            const ::HIR::Function* rv = nullptr;

            if (trait_contains_method_inner(trait_ptr, name, rv)) {
                assert(rv);
                out_path = trait_path.clone();
                return rv;
            }

            auto monomorph_cb = MonomorphStatePtr(m_crate.m_types, &self, &trait_path.m_params, nullptr);
            for (const auto& st : trait_ptr.m_all_parent_traits) {
                if (trait_contains_method_inner(*st.m_trait_ptr, name, rv)) {
                    assert(rv);
                    // TODO: HRLs
                    static ::HIR::GenericParams empty_hrtbs;
                    auto _h = monomorph_cb.push_hrb(st.m_hrtbs ? *st.m_hrtbs : empty_hrtbs);
                    out_path.m_path = st.m_path.m_path;
                    out_path.m_params = monomorph_cb.monomorph_path_params(sp, st.m_path.m_params, false);
                    return rv;
                }
            }
            return nullptr;
        }

        bool TraitResolution::trait_contains_type(const Span& sp, const ::HIR::GenericPath& trait_path, const ::HIR::Trait& trait_ptr, const char* name, ::HIR::GenericPath& out_path) const {
            TRACE_FUNCTION_FR(trait_path << " has " << name, out_path);

            auto it = trait_ptr.m_types.find(name);
            if (it != trait_ptr.m_types.end()) {
                DEBUG("- Found in cur");
                out_path = trait_path.clone();
                return true;
            }

            auto monomorph_cb = MonomorphStatePtr(m_crate.m_types, nullptr, &trait_path.m_params, nullptr);
            for (const auto& st : trait_ptr.m_all_parent_traits) {
                if (st.m_trait_ptr->m_types.count(name)) {
                    DEBUG("- Found in " << st);
                    out_path.m_path = st.m_path.m_path;
                    out_path.m_params = monomorph_cb.monomorph_path_params(sp, st.m_path.m_params, false);
                    return true;
                }
            }
            return false;
        }

        ::HIR::Compare TraitResolution::type_is_sized(const Span& sp, const ::HIR::TypeRef& type) const {
            bool is_fuzzy = false;
            bool has_eq = false;
            if (!m_lang_Sized.components().empty()) {
                has_eq = find_trait_impls(sp, m_lang_Sized, ::HIR::PathParams{}, type, [&](auto, auto c) -> bool {
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
                 switch (pb->m_struct_markings.dst_type) {
                     case ::HIR::StructMarkings::DstType::None:
                         break;
                     case ::HIR::StructMarkings::DstType::Possible:
                         // Check sized-ness of the unsized param
                         return type_is_sized(sp, e.path.m_data.as_Generic().m_params.m_types.at(pb->m_struct_markings.unsized_param));
                     case ::HIR::StructMarkings::DstType::Slice:
                     case ::HIR::StructMarkings::DstType::TraitObject:
                         return ::HIR::Compare::Unequal;
                 })
            )
        }
        TU_ARMA(Generic, e) {
            switch (e.group()) {
                case 0:
                    return this->m_impl_generics->m_types.at(e.idx()).m_is_sized ? ::HIR::Compare::Equal : ::HIR::Compare::Unequal;
                case 1:
                    return this->m_item_generics->m_types.at(e.idx()).m_is_sized ? ::HIR::Compare::Equal : ::HIR::Compare::Unequal;
                default:
                    // Assume sized for anything else?
                    return ::HIR::Compare::Equal;
            }
        }
        TU_ARMA(ErasedType, e) {
            return e.m_is_sized ? ::HIR::Compare::Equal : ::HIR::Compare::Unequal;
        }
        TU_ARMA(TraitObject, e) {
            return ::HIR::Compare::Unequal;
        }
    }
    return ::HIR::Compare::Equal;
        }

        ::HIR::Compare TraitResolution::type_is_copy(const Span& sp, const ::HIR::TypeRef& ty) const {
            const auto& type = this->m_ivars.get_type(ty);
    TU_MATCH_HDRA( (*type), {)
    default: {
            bool is_fuzzy = false;
            bool has_eq = find_trait_impls(sp, m_lang_Copy, ::HIR::PathParams{}, ty, [&](auto, auto c) -> bool {
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
                       m_lang_Copy,
                       [&](HIR::Compare _cmp, const ::HIR::TypeRef& be_type, const ::HIR::GenericPath& be_trait, const CachedBound& info) -> bool {
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

        ::HIR::Compare TraitResolution::type_is_clone(const Span& sp, const ::HIR::TypeRef& ty) const {
            TRACE_FUNCTION_F(ty);
            const auto& type = this->m_ivars.get_type(ty);
    TU_MATCH_HDRA( (*type), {)
    default: {
            if (type->is_Path() && type->as_Path().is_closure()) {
                // If it was a closure, assume true (later code can check)
                return ::HIR::Compare::Equal;
            }
            bool is_fuzzy = false;
            bool has_eq = find_trait_impls(sp, m_lang_Clone, ::HIR::PathParams{}, ty, [&](auto, auto c) -> bool {
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
                       m_lang_Clone,
                       [&](HIR::Compare _cmp, const ::HIR::TypeRef& be_type, const ::HIR::GenericPath& be_trait, const CachedBound& info) -> bool {
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
        ::HIR::Compare TraitResolution::can_unsize(const Span& sp, const ::HIR::TypeRef& dst_ty, const ::HIR::TypeRef& src_ty, ::std::function<void(::HIR::TypeRef new_dst)>* new_type_callback, ::std::function<void(const ::HIR::TypeRef& dst, const ::HIR::TypeRef& src)>* infer_callback) const {
            TRACE_FUNCTION_F(dst_ty << " <- " << src_ty);

            // 1. Test for type equality
            {
                auto cmp = dst_ty->compare_with_placeholders(sp, src_ty, m_ivars.callback_resolve_infer());
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
                bool found_bound = this->iterate_bounds_traits(sp, src_ty, m_lang_Unsize, [&](HIR::Compare cmp, const ::HIR::TypeRef& be_type, const ::HIR::GenericPath& be_trait, const CachedBound& info) -> bool {
                    const auto& be_dst = be_trait.m_params.m_types.at(0);

                    cmp &= dst_ty->compare_with_placeholders(sp, be_dst, m_ivars.callback_resolve_infer());
                    if (cmp == ::HIR::Compare::Unequal) {
                        return false;
                    }

                    if (cmp != ::HIR::Compare::Equal) {
                        TODO(sp, "Found bound " << dst_ty << "=" << be_dst << " <- " << src_ty << "=" << be_type);
                    }
                    return true;
                });
                if (found_bound) {
                    return ::HIR::Compare::Equal;
                }
            }

            // Associated types, check the bounds in the trait.
            if (src_ty->is_Path() && src_ty->as_Path().path.m_data.is_UfcsKnown()) {
                ::HIR::Compare rv = ::HIR::Compare::Equal;
                const auto& pe = src_ty->as_Path().path.m_data.as_UfcsKnown();
                auto monomorph_cb = MonomorphStatePtr(m_crate.m_types, &pe.type, &pe.trait.m_params, nullptr);
                auto found_bound = this->iterate_aty_bounds(sp, pe, [&](const ::HIR::TraitPath& bound) {
                    if (bound.m_path.m_path != m_lang_Unsize) {
                        return false;
                    }
                    const auto& be_dst_tpl = bound.m_path.m_params.m_types.at(0);
                    ::HIR::TypeRef tmp_ty;
                    const auto& be_dst = monomorph_cb.maybe_monomorph_type(sp, tmp_ty, be_dst_tpl);

                    auto cmp = dst_ty->compare_with_placeholders(sp, be_dst, m_ivars.callback_resolve_infer());
                    if (cmp == ::HIR::Compare::Unequal) {
                        return false;
                    }

                    if (cmp != ::HIR::Compare::Equal) {
                        DEBUG("[can_unsize] > Found bound (fuzzy) " << dst_ty << "=" << be_dst << " <- " << src_ty);
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
                bool dst_is_unsizable = dst_ty->as_Path().binding.is_Struct() && dst_ty->as_Path().binding.as_Struct()->m_struct_markings.can_unsize;
                bool src_is_unsizable = src_ty->as_Path().binding.is_Struct() && src_ty->as_Path().binding.as_Struct()->m_struct_markings.can_unsize;
                if (dst_is_unsizable || src_is_unsizable) {
                    DEBUG("Struct unsize? " << dst_ty << " <- " << src_ty);
                    const auto& str = *dst_ty->as_Path().binding.as_Struct();
                    const auto& dst_gp = dst_ty->as_Path().path.m_data.as_Generic();
                    const auto& src_gp = src_ty->as_Path().path.m_data.as_Generic();

                    if (dst_gp == src_gp) {
                        DEBUG("Can't Unsize, destination and source are identical");
                        return ::HIR::Compare::Unequal;
                    } else if (dst_gp.m_path == src_gp.m_path) {
                        DEBUG("Checking for Unsize " << dst_gp << " <- " << src_gp);
                        // Structures are equal, add the requirement that the ?Sized parameter also impl Unsize
                        const auto& dst_inner = m_ivars.get_type(dst_gp.m_params.m_types.at(str.m_struct_markings.unsized_param));
                        const auto& src_inner = m_ivars.get_type(src_gp.m_params.m_types.at(str.m_struct_markings.unsized_param));

                        auto cb = [&](auto d) {
                            assert(new_type_callback);

                            // Re-create structure with s/d
                            auto dst_gp_new = dst_gp.clone();
                            dst_gp_new.m_params.m_types.at(str.m_struct_markings.unsized_param) = mv$(d);
                            (*new_type_callback)(m_crate.m_types.path(::HIR::Path(mv$(dst_gp_new)), ::HIR::TypePathBinding::make_Struct(&str)));
                        };
                        if (new_type_callback) {
                            ::std::function<void(::HIR::TypeRef)> cb_p = cb;
                            return this->can_unsize(sp, dst_inner, src_inner, &cb_p, infer_callback);
                        } else {
                            return this->can_unsize(sp, dst_inner, src_inner, nullptr, infer_callback);
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
                    if (de->m_trait.m_path.m_path == se->m_trait.m_path.m_path) {
                        rv &= compare_pp(
                            sp,
                            se->m_trait.m_path.m_params,
                            de->m_trait.m_path.m_params
                        );
                        projected = &se->m_trait;
                    } else if (se->m_trait.m_path.m_path != ::HIR::SimplePath()) {
                        find_named_trait_in_trait(
                            sp,
                            de->m_trait.m_path.m_path,
                            de->m_trait.m_path.m_params,
                            *se->m_trait.m_trait_ptr,
                            se->m_trait.m_path.m_path,
                            se->m_trait.m_path.m_params,
                            src_ty,
                            [&](const ::HIR::TraitPath& parent) {
                                const auto cmp = compare_pp(
                                    sp,
                                    parent.m_path.m_params,
                                    de->m_trait.m_path.m_params
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
                    for (const auto& required : de->m_trait.m_type_bounds) {
                        const auto source = projected->m_type_bounds.find(required.first);
                        if (source == projected->m_type_bounds.end()) {
                            return ::HIR::Compare::Unequal;
                        }
                        rv &= source->second.type->compare_with_placeholders(
                            sp,
                            required.second.type,
                            m_ivars.callback_resolve_infer()
                        );
                        if (rv == ::HIR::Compare::Unequal) {
                            return rv;
                        }
                    }

                    // 2. Destination markers must be a strict subset
                    for (const auto& mt : de->m_markers) {
                        // TODO: Fuzzy match
                        bool found = false;
                        for (const auto& omt : se->m_markers) {
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
                tmp_e.m_trait.m_path = de->m_trait.m_path.m_path;

                // Check data trait first.
                if (de->m_trait.m_path.m_path == ::HIR::SimplePath()) {
                    ASSERT_BUG(sp, de->m_markers.size() > 0, "TraitObject with no traits - " << dst_ty);
                    good = true;
                } else {
                    good = find_trait_impls(sp, de->m_trait.m_path.m_path, de->m_trait.m_path.m_params, src_ty, [&](const auto impl, auto cmp) {
                        if (cmp == ::HIR::Compare::Unequal) {
                            return false;
                        }

                        auto candidate_cmp = cmp;
                        ::HIR::TypeData::Data_TraitObject candidate_e;
                        candidate_e.m_trait.m_path = de->m_trait.m_path.m_path;
                        candidate_e.m_trait.m_path.m_params = impl.get_trait_params(m_crate.m_types);

                        // Associated types declared by a supertrait carry the
                        // declaring trait path.  Rebuild that path with the
                        // selected principal-trait response instead of mixing
                        // response parameters with the original goal.
                        auto remap_source_trait = [&](const ::HIR::GenericPath& source_trait) {
                            if (source_trait.m_path == de->m_trait.m_path.m_path) {
                                return ::HIR::GenericPath(
                                    source_trait.m_path,
                                    candidate_e.m_trait.m_path.m_params.clone()
                                );
                            }

                            ::HIR::GenericPath result = source_trait.clone();
                            if (!de->m_trait.m_trait_ptr) {
                                candidate_cmp = ::HIR::Compare::Fuzzy;
                                return result;
                            }

                            auto goal_monomorph = MonomorphStatePtr(
                                m_crate.m_types, &src_ty, &de->m_trait.m_path.m_params, nullptr
                            );
                            auto response_monomorph = MonomorphStatePtr(
                                m_crate.m_types,
                                &src_ty,
                                &candidate_e.m_trait.m_path.m_params,
                                nullptr
                            );
                            bool found = false;
                            bool found_equal = false;
                            for (const auto& parent : de->m_trait.m_trait_ptr->m_all_parent_traits) {
                                if (parent.m_path.m_path != source_trait.m_path) {
                                    continue;
                                }
                                auto goal_parent = goal_monomorph.monomorph_genericpath(
                                    sp, parent.m_path, false
                                );
                                const auto parent_cmp = compare_pp(
                                    sp, goal_parent.m_params, source_trait.m_params
                                );
                                if (parent_cmp == ::HIR::Compare::Unequal
                                    || (found_equal && parent_cmp != ::HIR::Compare::Equal)) {
                                    continue;
                                }

                                auto response_parent = response_monomorph.monomorph_genericpath(
                                    sp, parent.m_path, false
                                );
                                if (!found || parent_cmp == ::HIR::Compare::Equal) {
                                    result = ::std::move(response_parent);
                                    found = true;
                                    found_equal = parent_cmp == ::HIR::Compare::Equal;
                                } else if (result != response_parent) {
                                    // Multiple fuzzy supertrait projections
                                    // are a legitimate ambiguous response.
                                    candidate_cmp = ::HIR::Compare::Fuzzy;
                                }
                            }
                            if (!found) {
                                candidate_cmp = ::HIR::Compare::Fuzzy;
                            } else if (!found_equal) {
                                candidate_cmp = ::HIR::Compare::Fuzzy;
                            }
                            return result;
                        };

                        for (const auto& aty : de->m_trait.m_type_bounds) {
                            auto atyv = impl.get_type(m_crate.m_types, aty.first.c_str(), aty.second.aty_params);
                            if (atyv == ::HIR::TypeRef()) {
                                // Get the trait from which this associated type comes.
                                // Insert a UfcsKnown path for that
                                auto p = ::HIR::Path(
                                    src_ty,
                                    aty.second.source_trait.clone(),
                                    aty.first,
                                    aty.second.aty_params.clone()
                                );
                                // Run EAT
                                atyv = this->expand_associated_types(sp, m_crate.m_types.path(mv$(p), {}));
                            }

                            auto desired = this->expand_associated_types(
                                sp, aty.second.type
                            );
                            const auto aty_cmp = compare_ty(sp, atyv, desired);
                            if (aty_cmp == ::HIR::Compare::Unequal) {
                                return false;
                            }
                            candidate_cmp &= aty_cmp;
                            candidate_e.m_trait.m_type_bounds[aty.first] = ::HIR::TraitPath::AtyEqual{
                                remap_source_trait(aty.second.source_trait),
                                aty.second.aty_params.clone(),
                                mv$(atyv)
                            };
                        }

                        total_cmp &= candidate_cmp;
                        tmp_e = ::std::move(candidate_e);
                        return true;
                    });
                }

                // Then markers
                auto cb = [&](const auto impl, auto cmp) {
                    if (cmp == ::HIR::Compare::Unequal) {
                        return false;
                    }
                    total_cmp &= cmp;
                    tmp_e.m_markers.back().m_params = impl.get_trait_params(m_crate.m_types);
                    return true;
                };
                for (const auto& marker : de->m_markers) {
                    if (!good) {
                        break;
                    }
                    tmp_e.m_markers.push_back(marker.m_path);
                    good &= find_trait_impls(sp, marker.m_path, marker.m_params, src_ty, cb);
                }

                if (good && total_cmp == ::HIR::Compare::Fuzzy && new_type_callback) {
                    (*new_type_callback)(m_crate.m_types.intern(::HIR::TypeData::make_TraitObject(mv$(tmp_e))));
                }
                return total_cmp;
            }

            // [T] <- [T; n]
            if (const auto* de = dst_ty->opt_Slice()) {
                if (const auto* se = src_ty->opt_Array()) {
                    DEBUG("Array unsize? " << de->inner << " <- " << se->inner);
                    auto cmp = de->inner->compare_with_placeholders(sp, se->inner, m_ivars.callback_resolve_infer());
                    // TODO: Indicate to caller that for this to be true, these two must be the same.
                    // - I.E. if true, equate these types
                    if (cmp == ::HIR::Compare::Fuzzy && new_type_callback) {
                        (*new_type_callback)(m_crate.m_types.slice(se->inner));
                    }
                    return cmp;
                }
            }

            DEBUG("Can't unsize, no rules matched");
            return ::HIR::Compare::Unequal;
        }

        const ::HIR::TypeRef* TraitResolution::type_is_owned_box(const Span& sp, const ::HIR::TypeRef& ty) const {
            if (const auto* e = ty->opt_Path()) {
                if (const auto* pe = e->path.m_data.opt_Generic()) {
                    if (pe->m_path == m_lang_Box) {
                        return &this->m_ivars.get_type(pe->m_params.m_types.at(0));
                    }
                }
            }
            return nullptr;
        }

        // -------------------------------------------------------------------------------------------------------------------
        //
        // -------------------------------------------------------------------------------------------------------------------
        TraitResolution::AutoderefResult TraitResolution::autoderef_step(
            const Span& sp,
            const ::HIR::TypeRef& ty_in,
            ::HIR::TypeRef& target,
            ::std::optional<::HIR::TypeRef>* impl_type
        ) const {
            if (impl_type) {
                impl_type->reset();
            }

            const auto& ty = this->m_ivars.get_type(ty_in);
            if (ty->is_Infer()) {
                return AutoderefResult::NoMatch;
            } else if (const auto* e = ty->opt_Borrow()) {
                DEBUG("Deref " << ty << " into " << e->inner);
                target = this->m_ivars.get_type(e->inner);
                return AutoderefResult::Match;
            }
            // Array-to-slice is the final unsize step in an autoderef search.
            // create_autoderef materialises it as borrow -> pointer unsize -> deref.
            else if (const auto* e = ty->opt_Array()) {
                DEBUG("Deref " << ty << " into [" << e->inner << "]");
                target = m_crate.m_types.slice(e->inner);
                return AutoderefResult::Match;
            }
            // Shortcut, don't look up a Deref impl for primitives or slices
            else if (ty->is_Slice() || ty->is_Primitive() || ty->is_Tuple() || ty->is_Array()) {
                return AutoderefResult::NoMatch;
            } else {
                ::std::optional<::HIR::TypeRef> candidate_target;
                ::std::optional<::HIR::TypeRef> candidate_impl_type;
                bool exact = false;
                bool ambiguous = false;

                this->find_trait_impls(sp, m_lang_Deref, ::HIR::PathParams{}, ty, [&](auto impl, auto match) {
                    auto found_target = impl.get_type(m_crate.m_types, "Target", {});
                    if (found_target == ::HIR::TypeRef()) {
                        found_target = m_crate.m_types.path(::HIR::Path(ty, m_lang_Deref, RcString::new_interned("Target")), ::HIR::TypePathBinding::make_Opaque({}));
                    } else {
                        this->expand_associated_types_inplace(sp, found_target, {});
                    }
                    auto found_impl_type = impl.get_impl_type(m_crate.m_types);

                    if (match == ::HIR::Compare::Equal) {
                        candidate_target = found_target;
                        candidate_impl_type = found_impl_type;
                        exact = true;
                        return true;
                    }

                    if (candidate_target) {
                        ambiguous = true;
                    } else {
                        candidate_target = found_target;
                        candidate_impl_type = found_impl_type;
                    }
                    return false;
                });

                if (!exact && ambiguous) {
                    DEBUG("Ambiguous Deref impl for " << ty);
                    return AutoderefResult::Ambiguous;
                }
                if (!candidate_target) {
                    return AutoderefResult::NoMatch;
                }

                target = *candidate_target;
                if (impl_type) {
                    *impl_type = *candidate_impl_type;
                }
                DEBUG("Deref " << ty << " into " << target);
                return AutoderefResult::Match;
            }
        }

        const ::HIR::TypeRef* TraitResolution::autoderef(const Span& sp, const ::HIR::TypeRef& ty, ::HIR::TypeRef& tmp_type) const {
            return autoderef_step(sp, ty, tmp_type) == AutoderefResult::Match
                ? &tmp_type
                : nullptr;
        }

        unsigned int TraitResolution::autoderef_find_method(
            const Span& sp,
            const HIR::t_trait_list& traits,
            const ::std::vector<unsigned>& ivars,
            unsigned int type_ivar_count,
            const ::HIR::TypeRef& top_ty,
            const RcString& method_name,
            /* Out -> */ ::std::vector<::std::pair<AutoderefBorrow, ::HIR::Path>>& possibilities
        ) const {
            try {
                TRACE_FUNCTION_F("{" << top_ty << "}." << method_name);
                unsigned int deref_count = 0;
                ::HIR::TypeRef tmp_type; // Temporary type used for handling Deref
                const auto& top_ty_r = this->m_ivars.get_type(top_ty);
                const auto* current_ty = &top_ty_r;

                // Correct algorithm:
                // - Find any available method with a receiver type of `T`
                // - If no, try &T
                // - If no, try &mut T
                // - If no, try &move T
                // - If no, dereference T and try again
                auto cur_access = MethodAccess::Move; // Assume that the input value is movable
                auto collapse_to_most_specific_subtrait = [&]() {
                    if (!m_crate.feature_enabled("supertrait_item_shadowing")
                        || possibilities.size() < 2) {
                        return;
                    }

                    ::std::vector<::HIR::SimplePath> candidate_traits;
                    candidate_traits.reserve(possibilities.size());
                    for (const auto& possibility : possibilities) {
                        const auto* path = possibility.second.m_data.opt_UfcsKnown();
                        if (!path) {
                            // RFC 3624 only collapses extension-trait picks.
                            return;
                        }
                        candidate_traits.push_back(path->trait.m_path);
                    }

                    const auto selected = m_crate.find_most_specific_trait(sp, candidate_traits);
                    if (selected) {
                        auto selected_possibility = mv$(possibilities[*selected]);
                        possibilities.clear();
                        possibilities.push_back(mv$(selected_possibility));
                    }
                };
                do {
                    const auto& ty = this->m_ivars.get_type(*current_ty);
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
                    if (ty->is_Borrow() && should_pause(this->m_ivars.get_type(ty->as_Borrow().inner))) {
                        return ~0u;
                    }
                    // TODO: Pause on Box<_>?
                    DEBUG(deref_count << ": " << ty);

                    // Non-referenced
                    if (this->find_method(sp, traits, ivars, type_ivar_count, ty, method_name, cur_access, AutoderefBorrow::None, possibilities)) {
                        DEBUG("FOUND *{" << deref_count << "}, fcn_path = " << possibilities.back().second);
                    }

                    // Auto-ref
                    auto borrow_ty = m_crate.m_types.borrow(::HIR::BorrowType::Shared, ty);
                    if (this->find_method(sp, traits, ivars, type_ivar_count, borrow_ty, method_name, MethodAccess::Move, AutoderefBorrow::Shared, possibilities)) {
                        DEBUG("FOUND & *{" << deref_count << "}, fcn_path = " << possibilities.back().second);
                    }
                    borrow_ty = m_crate.m_types.borrow(::HIR::BorrowType::Unique, ty);
                    if (cur_access >= MethodAccess::Unique && this->find_method(sp, traits, ivars, type_ivar_count, borrow_ty, method_name, MethodAccess::Move, AutoderefBorrow::Unique, possibilities)) {
                        DEBUG("FOUND &mut *{" << deref_count << "}, fcn_path = " << possibilities.back().second);
                    }
                    borrow_ty = m_crate.m_types.borrow(::HIR::BorrowType::Owned, ty);
                    if (cur_access >= MethodAccess::Move && this->find_method(sp, traits, ivars, type_ivar_count, borrow_ty, method_name, MethodAccess::Move, AutoderefBorrow::Owned, possibilities)) {
                        DEBUG("FOUND &move *{" << deref_count << "}, fcn_path = " << possibilities.back().second);
                    }
                    if (!possibilities.empty()) {
                        collapse_to_most_specific_subtrait();
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
                        switch (this->autoderef_step(sp, ty, tmp_type)) {
                            case AutoderefResult::NoMatch:
                                current_ty = nullptr;
                                break;
                            case AutoderefResult::Match:
                                current_ty = &tmp_type;
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
        ::std::optional<::HIR::TypeRef> TraitResolution::check_method_receiver(const Span& sp, const ::HIR::Function& fcn, const ::HIR::TypeRef& ty, TraitResolution::MethodAccess access) const {
            switch (fcn.m_receiver) {
                case ::HIR::Function::Receiver::Free:
                    // Free functions are never usable
                    return ::std::nullopt;
                case ::HIR::Function::Receiver::Value:
                    if (access >= TraitResolution::MethodAccess::Move) {
                        return this->m_ivars.get_type(ty);
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
                        return this->m_ivars.get_type(ty->as_Borrow().inner);
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
                        return this->m_ivars.get_type(ty->as_Borrow().inner);
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
                        return this->m_ivars.get_type(ty->as_Borrow().inner);
                    }
                    break;
                case ::HIR::Function::Receiver::Custom: {
                    const auto& receiver_type = fcn.m_args.front().second;
                    ASSERT_BUG(
                        sp,
                        visit_ty_with(
                            receiver_type,
                            [](const HIR::TypeRef& v) {
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

                            ::HIR::Compare match_ty(const ::HIR::GenericRef& g, const ::HIR::TypeRef& ty, ::HIR::t_cb_resolve_type _resolve_cb) override {
                                if (g.is_self()) {
                                    detected_self_ty = ty;
                                }
                                return ::HIR::Compare::Equal;
                            }

                            ::HIR::Compare match_val(const ::HIR::GenericRef& g, const ::HIR::ConstGeneric& sz) override {
                                TODO(Span(), "GetSelf::match_val " << g << " with " << sz);
                            }
                        } getself;

                        if (receiver_type->match_test_generics(sp, ty, this->m_ivars.callback_resolve_infer(), getself)) {
                            ASSERT_BUG(sp, getself.detected_self_ty, "Unable to determine receiver type when matching " << receiver_type << " and " << ty);
                            return this->m_ivars.get_type(*getself.detected_self_ty);
                        }
                    }
                    return ::std::nullopt;
                }
                case ::HIR::Function::Receiver::Box:
                    if (const auto* ity = this->type_is_owned_box(sp, ty)) {
                        if (access < TraitResolution::MethodAccess::Move) {
                        } else {
                            return this->m_ivars.get_type(*ity);
                        }
                    }
                    break;
            }
            return ::std::nullopt;
        }

        bool TraitResolution::find_method(const Span& sp, const HIR::t_trait_list& traits, const ::std::vector<unsigned>& ivars, unsigned int type_ivar_count, const ::HIR::TypeRef& ty, const RcString& method_name, MethodAccess access, AutoderefBorrow borrow_type, /* Out -> */ ::std::vector<::std::pair<AutoderefBorrow, ::HIR::Path>>& possibilities) const {
            bool rv = false;
            TRACE_FUNCTION_FR("ty=" << ty << ", name=" << method_name << ", access=" << access, rv << " " << possibilities);
            auto cb_infer = m_ivars.callback_resolve_infer();

            auto get_ivared_params = [&](const ::HIR::GenericParams& tpl) -> ::HIR::PathParams {
                unsigned int n_params = tpl.m_types.size();
                ASSERT_BUG(sp, type_ivar_count <= ivars.size(), "Invalid method ivar split: " << type_ivar_count << " type ivars in a pool of " << ivars.size());
                ASSERT_BUG(sp, n_params <= type_ivar_count, "Not enough type ivars allocated for method: " << n_params << " needed but " << type_ivar_count << " allocated by caller\ntpl = " << tpl.fmt_args());
                ::HIR::PathParams trait_params;
                trait_params.m_types.reserve(n_params);
                for (unsigned int i = 0; i < n_params; i++) {
                    trait_params.m_types.push_back(m_crate.m_types.infer(ivars[i], ::HIR::InferClass::None));
                    ASSERT_BUG(sp, m_ivars.get_type(trait_params.m_types.back())->as_Infer().index == ivars[i], "A method selection ivar was bound");
                }
                const unsigned int n_values = tpl.m_values.size();
                ASSERT_BUG(sp, n_values <= ivars.size() - type_ivar_count, "Not enough value ivars allocated for method: " << n_values << " needed but " << ivars.size() - type_ivar_count << " allocated by caller\ntpl = " << tpl.fmt_args());
                trait_params.m_values.reserve(n_values);
                for (unsigned int i = 0; i < n_values; i++) {
                    trait_params.m_values.push_back(::HIR::ConstGeneric::make_Infer({ivars[type_ivar_count + i]}));
                }
                return trait_params;
            };

            // 1. Search for inherent methods
            // - Inherent methods are searched first.
            // TODO: Have a cache of name+receiver_type to a list of types and impls
            // e.g. `len` `&Self` = `[T]`
            DEBUG("> Inherent methods");
            m_crate.m_inherent_method_cache.find(sp, method_name, ty, m_ivars.callback_resolve_infer(), [&](const HIR::TypeRef& self_ty, const HIR::TypeImpl& impl) {
                if (!impl.m_methods.at(method_name).publicity.is_visible(this->m_vis_path)) {
                    // Ignore method: Not visibile
                    return;
                }
                ::HIR::PathParams impl_params;
                auto cmp = ftic_check_params(sp, ::HIR::SimplePath(), nullptr, self_ty, impl.m_params, {}, impl.m_type, impl_params);
                if (cmp != HIR::Compare::Unequal) {
                    DEBUG("Found `impl" << impl.m_params.fmt_args() << " " << impl.m_type << "` fn " << method_name /* << " - " << top_ty*/);
                    possibilities.push_back(::std::make_pair(borrow_type, ::HIR::Path(self_ty, method_name, {})));
                    DEBUG("++ " << possibilities.back());
                    rv = true;
                }
            });

            // TODO: Handle custom recievers by finding the bottom of a deref chain (or take the top-level reciever as an argument here?)

            // 3. Search generic bounds for a match
            // - If there is a bound on the receiver, then that bound is usable no-matter what
            DEBUG("> Bounds");
            bool found_bound = false;
            for (const auto& tb : m_trait_bounds) {
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
                if (auto self_ty = check_method_receiver(sp, *fcn_ptr, ty, access)) {
                    // HRLs - could be some in the path from `trait_contains_method`
                    // - Lazy option, just erase whatever we find
                    struct MonomorphEraseHrls: public Monomorphiser {
                        using Monomorphiser::Monomorphiser;

                        ::HIR::TypeRef get_type(const Span& sp, const ::HIR::GenericRef& ty) const override {
                            if (ty.group() == 3) {
                                return m_types.infer();
                            }
                            return m_types.generic(ty.name, ty.binding);
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

                    final_trait_path = MonomorphEraseHrls(m_crate.m_types).monomorph_genericpath(sp, final_trait_path, true);

                    // If the type is an unbounded ivar, don't check.
                    if (TU_TEST1(**self_ty, Infer, .is_lit() == false)) {
                        return false;
                    }
                    // TODO: Do a fuzzy match here?
                    auto cmp = (*self_ty)->compare_with_placeholders(sp, e_type, cb_infer);
                    if (cmp == ::HIR::Compare::Equal) {
                        // TODO: Re-monomorphise final trait using `ty`?
                        // - Could collide with legitimate uses of `Self`

                        // Found the method, return the UFCS path for it
                        possibilities.push_back(::std::make_pair(borrow_type, ::HIR::Path(::HIR::Path::Data::make_UfcsKnown({*self_ty, mv$(final_trait_path), method_name, {}}))));
                        DEBUG("++ " << possibilities.back());
                        rv = true;
                        found_bound = true;
                    } else if (cmp == ::HIR::Compare::Fuzzy) {
                        DEBUG("Fuzzy match checking bounded method - " << *self_ty << " != " << e_type);

                        // Found the method, return the UFCS path for it
                        possibilities.push_back(::std::make_pair(borrow_type, ::HIR::Path(::HIR::Path::Data::make_UfcsKnown({*self_ty, mv$(final_trait_path), method_name, {}}))));
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
            if (m_current_trait_path) {
                ::HIR::GenericPath final_trait_path;
                const ::HIR::Function* fcn_ptr;
                if ((fcn_ptr = this->trait_contains_method(sp, *m_current_trait_path, *m_current_trait_ptr, ty, method_name, final_trait_path))) {
                    DEBUG("- Found trait " << final_trait_path << " (current)");
                    if (auto self_ty = check_method_receiver(sp, *fcn_ptr, ty, access)) {
                        // If the type is an unbounded ivar, don't check.
                        if (TU_TEST1(**self_ty, Infer, .is_lit() == false)) {
                            return false;
                        }

                        // Use the set of ivars we were given to populate the trait parameters
                        const auto& trait = m_crate.get_trait_by_path(sp, final_trait_path.m_path);
                        auto trait_params = get_ivared_params(trait.m_params);
                        //auto trait_params = std::move(final_trait_path.m_params);

                        try {
                            bool crate_impl_found = false;
                            // Method probing only establishes that some implementation of the
                            // trait can apply to the receiver.  The trait arguments are inference
                            // variables shared with the eventual call signature; constraining
                            // them to the first matching impl here makes impl iteration order
                            // decide calls whose arguments would otherwise disambiguate them.
                            find_trait_impls_crate(sp, final_trait_path.m_path, nullptr, *self_ty, [&](auto impl, auto cmp) {
                                DEBUG("[find_method] " << impl << ", cmp = " << cmp);
                                //magic_found = true;
                                crate_impl_found = true;
                                return true;
                            });
                            if (crate_impl_found) {
                                DEBUG("Found trait impl " << m_current_trait_path->m_path << trait_params << " for " << *self_ty << " (" << m_ivars.fmt_type(*self_ty) << ")");
                                possibilities.push_back(::std::make_pair(borrow_type, ::HIR::Path(*self_ty, ::HIR::GenericPath(final_trait_path.m_path, mv$(trait_params)), method_name, {})));
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

            auto get_inner_type = [this, sp](const ::HIR::TypeRef& ty, ::std::function<bool(const ::HIR::TypeRef&)> cb) -> const ::HIR::TypeRef* {
                if (cb(ty)) {
                    return &ty;
                } else if (ty->is_Borrow()) {
                    const auto& ity = this->m_ivars.get_type(ty->as_Borrow().inner);
                    if (cb(ity)) {
                        return &ity;
                    } else {
                        return nullptr;
                    }
                } else {
                    auto tp = this->type_is_owned_box(sp, ty);
                    if (tp && cb(*tp)) {
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
                const auto& e = (*ityp)->as_TraitObject();
                const auto& trait = this->m_crate.get_trait_by_path(sp, e.m_trait.m_path.m_path);

                bool found_trait_object = false;
                auto add_trait_object_method = [&](const ::HIR::Function& fcn, ::HIR::GenericPath final_trait_path) {
                    DEBUG("- Found trait " << final_trait_path << " (trait object)");
                    // - If the receiver is valid, then it's correct (no need to check the type again)
                    if (auto self_ty_p = check_method_receiver(sp, fcn, ty, access)) {
                        if (e.m_trait.m_hrtbs) {
                            auto pps = e.m_trait.m_hrtbs->make_empty_params(true);
                            final_trait_path.m_params = MonomorphHrlsOnly(m_crate.m_types, pps).monomorph_path_params(sp, final_trait_path.m_params, true);
                        }
                        possibilities.push_back(::std::make_pair(borrow_type, ::HIR::Path(*self_ty_p, mv$(final_trait_path), method_name, {})));
                        DEBUG("++ " << possibilities.back());
                        rv = true;
                        found_trait_object = true;
                    }
                };

                const ::HIR::Function* fcn_ptr = nullptr;
                if (trait_contains_method_inner(trait, method_name, fcn_ptr)) {
                    assert(fcn_ptr);
                    add_trait_object_method(*fcn_ptr, e.m_trait.m_path.clone());
                } else {
                    const auto self_ty = m_crate.m_types.self();
                    auto monomorph_cb = MonomorphStatePtr(m_crate.m_types, &self_ty, &e.m_trait.m_path.m_params, nullptr);
                    for (const auto& st : trait.m_all_parent_traits) {
                        fcn_ptr = nullptr;
                        if (!trait_contains_method_inner(*st.m_trait_ptr, method_name, fcn_ptr)) {
                            continue;
                        }
                        assert(fcn_ptr);
                        static ::HIR::GenericParams empty_hrtbs;
                        auto _h = monomorph_cb.push_hrb(st.m_hrtbs ? *st.m_hrtbs : empty_hrtbs);
                        auto final_trait_path = ::HIR::GenericPath(st.m_path.m_path, monomorph_cb.monomorph_path_params(sp, st.m_path.m_params, false));
                        add_trait_object_method(*fcn_ptr, std::move(final_trait_path));
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
                const auto& e = (*ityp)->as_ErasedType();
                for (const auto& trait_path : e.m_traits) {
                    const auto& trait = this->m_crate.get_trait_by_path(sp, trait_path.m_path.m_path);

                    ::HIR::GenericPath final_trait_path;
                    if (const auto* fcn_ptr = this->trait_contains_method(sp, trait_path.m_path, trait, m_crate.m_types.self(), method_name, final_trait_path)) {
                        DEBUG("- Found trait " << final_trait_path << " (erased type)");

                        if (auto self_ty_p = check_method_receiver(sp, *fcn_ptr, ty, access)) {
                            possibilities.push_back(::std::make_pair(borrow_type, ::HIR::Path(*self_ty_p, mv$(final_trait_path), method_name, {})));
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
                return t->is_Path() && t->as_Path().path.m_data.is_UfcsKnown();
            })) {
                const auto& e = (*ityp)->as_Path().path.m_data.as_UfcsKnown();
                DEBUG("UfcsKnown - Search associated type bounds in trait - " << e.trait);

                // UFCS known - Assuming that it's reached the maximum resolvable level (i.e. a type within is generic), search for trait bounds on the type

                // `Self` = `*.type`
                // `/*I:#*/` := `e.trait.m_params`
                auto monomorph_cb = MonomorphStatePtr(m_crate.m_types, &e.type, &e.trait.m_params, &e.params);

                const auto& trait = this->m_crate.get_trait_by_path(sp, e.trait.m_path);
                const auto& assoc_ty = trait.m_types.at(e.item);
                // NOTE: The bounds here have 'Self' = the type
                for (const auto& bound : assoc_ty.m_trait_bounds) {
                    ASSERT_BUG(sp, bound.m_trait_ptr, "Pointer to trait " << bound.m_path << " not set in " << e.trait.m_path);
                    ::HIR::GenericPath final_trait_path;

                    auto ty_self = m_crate.m_types.path(::HIR::Path(m_crate.m_types.self(), bound.m_path.clone(), e.item), HIR::TypePathBinding::make_Opaque({}));
                    if (const auto* fcn_ptr = this->trait_contains_method(sp, bound.m_path, *bound.m_trait_ptr, ty_self, method_name, final_trait_path)) {
                        DEBUG("- Found trait " << final_trait_path << " (UFCS Known, aty bounds)");

                        if (auto self_ty_p = check_method_receiver(sp, *fcn_ptr, ty, access)) {
                            if (*self_ty_p == *ityp) {
                                auto pp_hrb = bound.m_hrtbs ? bound.m_hrtbs->make_empty_params(true) : HIR::PathParams();
                                monomorph_cb.pp_hrb = &pp_hrb;
                                final_trait_path = monomorph_cb.monomorph_genericpath(sp, final_trait_path, false);
                                DEBUG("- Monomorph to " << final_trait_path);

                                // Found the method, return the UFCS path for it
                                possibilities.push_back(::std::make_pair(borrow_type, ::HIR::Path(*self_ty_p, mv$(final_trait_path), method_name, {})));
                                DEBUG("++ " << possibilities.back());
                                rv = true;
                            }
                        }
                    }
                }

                // Search `<Self as Trait>::Name` bounds on the trait itself
                for (const auto& bound : trait.m_params.m_bounds) {
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

                    const auto& be_type_pe = be.type->as_Path().path.m_data.as_UfcsKnown();
                    if (be_type_pe.type != m_crate.m_types.self()) {
                        continue;
                    }
                    if (be_type_pe.trait.m_path != e.trait.m_path) {
                        continue;
                    }
                    if (be_type_pe.item != e.item) {
                        continue;
                    }

                    // Found such a bound, now to test if it is useful

                    ::HIR::GenericPath final_trait_path;
                    if (const auto* fcn_ptr = this->trait_contains_method(sp, be.trait.m_path, *be.trait.m_trait_ptr, m_crate.m_types.self(), method_name, final_trait_path)) {
                        DEBUG("- Found trait " << final_trait_path << " (UFCS Known, trait bounds)");

                        if (auto self_ty_p = check_method_receiver(sp, *fcn_ptr, ty, access)) {
                            if (*self_ty_p == *ityp) {
                                if (monomorphise_pathparams_needed(final_trait_path.m_params)) {
                                    final_trait_path.m_params = monomorph_cb.monomorph_path_params(sp, final_trait_path.m_params, false);
                                    DEBUG("- Monomorph to " << final_trait_path);
                                }

                                // Found the method, return the UFCS path for it
                                possibilities.push_back(::std::make_pair(borrow_type, ::HIR::Path(*self_ty_p, mv$(final_trait_path), method_name, {})));
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
                if (!(fcn_ptr = this->trait_contains_method(sp, *trait_ref.first, *trait_ref.second, m_crate.m_types.self(), method_name, final_trait_path))) {
                    continue;
                }
                DEBUG("- Found trait " << final_trait_path << " (in scope)");

                if (auto self_ty_p = check_method_receiver(sp, *fcn_ptr, ty, access)) {
                    const auto& self_ty = *self_ty_p;
                    DEBUG("Search for impl of " << *trait_ref.first << " for " << self_ty);

                    // Use the set of ivars we were given to populate the trait parameters
                    ::HIR::PathParams trait_params = get_ivared_params(trait_ref.second->m_params);

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
                        DEBUG("Found trait impl " << *trait_ref.first << trait_params << " for " << self_ty << " (" << m_ivars.fmt_type(self_ty) << ")");
                        possibilities.push_back(::std::make_pair(borrow_type, ::HIR::Path(self_ty, ::HIR::GenericPath(*trait_ref.first, mv$(trait_params)), method_name, {})));
                        DEBUG("++ " << possibilities.back());
                        rv = true;
                    }
                } else {
                    DEBUG("> Incorrect receiver");
                }
            }

            return rv;
        }

        unsigned int TraitResolution::autoderef_find_field(const Span& sp, const ::HIR::TypeRef& top_ty, const RcString& field_name, /* Out -> */ ::HIR::TypeRef& field_type) const {
            unsigned int deref_count = 0;
            ::HIR::TypeRef tmp_type; // Temporary type used for handling Deref
            const auto* current_ty = &top_ty;
            if (const auto* e = this->m_ivars.get_type(top_ty)->opt_Borrow()) {
                current_ty = &e->inner;
                deref_count += 1;
            }

            do {
                const auto& ty = this->m_ivars.get_type(*current_ty);
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

            if (/*const auto* e =*/this->m_ivars.get_type(top_ty)->opt_Borrow()) {
                const auto& ty = this->m_ivars.get_type(top_ty);

                if (find_field(sp, ty, field_name, field_type)) {
                    return 0;
                }
            }

            // Dereference failed! This is a hard error (hitting _ is checked above and returns ~0)
            this->m_ivars.dump();
            TODO(sp, "Error when no field could be found, but type is known - (: " << top_ty << ")." << field_name);
        }

        bool TraitResolution::find_field(const Span& sp, const ::HIR::TypeRef& ty, const RcString& name, /* Out -> */ ::HIR::TypeRef& field_ty) const {
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
                const auto& params = e->path.m_data.as_Generic().m_params;
                auto monomorph = MonomorphStatePtr(m_crate.m_types, &ty, &params, nullptr);
            TU_MATCH_HDRA( (str.m_data), {)
            TU_ARMA(Unit, se) {
                        // No fields on a unit struct
                    }
                    TU_ARMA(Tuple, se) {
                        for (unsigned int i = 0; i < se.size(); i++) {
                            DEBUG(i << ": " << se[i].publicity << ", " << this->m_vis_path << " : " << se[i].ent);
                            if (se[i].publicity.is_visible(this->m_vis_path) && FMT(i) == name) {
                                field_ty = monomorph.monomorph_type(sp, se[i].ent);
                                return true;
                            }
                        }
                    }
                    TU_ARMA(Named, se) {
                        for (const auto& fld : se) {
                            DEBUG(fld.name << ": " << fld.vis << ", " << this->m_vis_path << " : " << fld.ty);
                            if (fld.vis.is_visible(this->m_vis_path) && fld.name == name) {
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
                const auto& params = e->path.m_data.as_Generic().m_params;
                auto monomorph = MonomorphStatePtr(m_crate.m_types, &ty, &params, nullptr);

                for (const auto& fld : unm.m_variants) {
                    if (fld.vis.is_visible(this->m_vis_path) && fld.name == name) {
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
