/*
 * MRustC - Rust Compiler
 * - By John Hodge (Mutabah/thePowersGang)
 *
 * hir_expand/erased_types.cpp
 * - HIR Expansion - Replace `impl Trait` with the real type
 */
#include "hir_visitor.h"
#include "hir_expr.h"
#include "hir_typeck_static.h"
#include <algorithm>
#include "hir_expand_main_bindings.h"

namespace {

    struct MonomorphCheckLft: public MonomorphiserNop {
        const HIR::TypeRef& tpl;

        MonomorphCheckLft(HIR::TypeInterner& types, const HIR::TypeRef& tpl)
            : MonomorphiserNop(types)
            , tpl(tpl)
        {
        }

        HIR::LifetimeRef monomorph_lifetime(const Span& sp, const HIR::LifetimeRef& lft) const override {
            //ASSERT_BUG(sp, lft.binding <= ::HIR::LifetimeRef::STATIC, "Found local/ivar lifetime - " << lft << "\n in " << tpl);
            return lft;
        }
    };

    void expand_erased_type(const Span& sp, const StaticTraitResolve& m_resolve, HIR::TypeRef& ty) {
        const auto& e = ty->as_ErasedType();

        HIR::TypeRef new_ty;
        TU_MATCH_HDRA( (e.m_inner), { )
        TU_ARMA(Fcn, ee) {
                MonomorphState monomorph_cb(m_resolve.m_crate.m_types);
                auto val = m_resolve.get_value(sp, ee.m_origin, monomorph_cb);
                if (val.is_NotYetKnown() && ee.m_origin.m_data.is_UfcsKnown()) {
                    const auto& v = ee.m_origin.m_data.as_UfcsKnown();
                    auto name = RcString::new_interned(FMT(ATY_PREFIX_ERASED << v.item << "_" << ee.m_index));
                    new_ty = m_resolve.m_crate.m_types.path(::HIR::Path(v.type, v.trait.clone(), name, v.params.clone()), {});
                } else {
                    ASSERT_BUG(sp, val.is_Function(), "ErasedType with Fcn type doesn't point at a function: " << ee.m_origin << ": " << val.tag_str());
                    const auto& fcn = *val.as_Function();
                    const auto& erased_types = fcn.m_code.m_erased_types;

                    ASSERT_BUG(sp, ee.m_index < erased_types.size(), "Erased type index out of range for " << ee.m_origin << " - " << ee.m_index << " >= " << erased_types.size());
                    const auto& tpl = erased_types[ee.m_index];

                    MonomorphCheckLft(m_resolve.m_crate.m_types, tpl).monomorph_type(sp, tpl);

                    new_ty = monomorph_cb.monomorph_type(sp, tpl);
                }
                m_resolve.expand_associated_types(sp, new_ty);
            }
            TU_ARMA(Alias, ee) {
                if (ee.inner->type == HIR::TypeRef()) {
                    ERROR(Span(), E0000, "Erased type alias " << ee.inner->type << " never set?");
                }
                MonomorphCheckLft(m_resolve.m_crate.m_types, ee.inner->type).monomorph_type(sp, ee.inner->type);
                new_ty = MonomorphStatePtr(m_resolve.m_crate.m_types, nullptr, &ee.params, nullptr).monomorph_type(sp, ee.inner->type);
                m_resolve.expand_associated_types(sp, new_ty);
            }
            TU_ARMA(Known, ee) {
                new_ty = ee;
            }
        }
        DEBUG("> " << ty << " => " << new_ty);
        ty = mv$(new_ty);
    }

    void visit_type(const Span& sp, const StaticTraitResolve& resolve, ::HIR::TypeRef& ty) {
        TRACE_FUNCTION_FR(ty, ty);

        class V: public ::HIR::Visitor {
            const Span& sp;
            const StaticTraitResolve& m_resolve;
            bool clear_opaque;

        public:
            V(const Span& sp, const StaticTraitResolve& resolve)
                : ::HIR::Visitor(nullptr, resolve.m_crate.m_types)
                , sp(sp)
                , m_resolve(resolve)
                , clear_opaque(false)
            {
            }

            void visit_type(::HIR::TypeRef& ty) override {
                static const Span sp;
                auto saved_clear_opaque = this->clear_opaque;
                this->clear_opaque = false;
                if (ty->is_ErasedType()) {
                    TRACE_FUNCTION_FR(ty, ty);

                    expand_erased_type(sp, m_resolve, ty);

                    // Recurse (TODO: Cleanly prevent infinite recursion - TRACE_FUNCTION does crude prevention)
                    this->visit_type(ty);
                    this->clear_opaque = true;
                } else {
                    ::HIR::Visitor::visit_type(ty);
                    // If there was an erased type anywhere within this type, then clear an Opaque binding so EAT runs again
                    if (ty->is_Path()) {
                        // NOTE: This is both an optimisation, and avoids issues (if all types are cleared, the alias list in
                        // `StaticTraitResolve` ends up with un-expanded ATYs which leads to expansion not happening when it shoud.
                        if (this->clear_opaque && ty->as_Path().binding.is_Opaque()) {
                            auto data = ty->clone_data();
                            data.as_Path().binding = HIR::TypePathBinding::make_Unbound({});
                            ty = m_resolve.m_crate.m_types.intern(std::move(data));
                        }
                    }
                }
                this->clear_opaque |= saved_clear_opaque;
            }
        } v(sp, resolve);

        resolve.expand_associated_types(sp, ty);
        v.visit_type(ty);
        resolve.expand_associated_types(sp, ty);
    }

    class ExprVisitor_Extract: public ::HIR::ExprVisitorDef {
        const StaticTraitResolve& m_resolve;

    public:
        ExprVisitor_Extract(const StaticTraitResolve& resolve)
            : ::HIR::ExprVisitorDef(resolve.m_crate.m_types)
            , m_resolve(resolve)
        {
        }

        void visit_root(::HIR::ExprPtr& root) {
            root->visit(*this);
            visit_type(root->m_res_type);
            for (auto& ty : root.m_bindings) {
                visit_type(ty);
            }
            for (auto& ty : root.m_erased_types) {
                visit_type(ty);
            }
        }

        void visit_node_ptr(::HIR::ExprNodeP& node_ptr) override {
            assert(node_ptr);
            node_ptr->visit(*this);
            visit_type(node_ptr->m_res_type);
        }

        void visit_type(::HIR::TypeRef& ty) override {
            static Span sp;
            ::visit_type(sp, m_resolve, ty);
        }
    };

    class OuterVisitor: public ::HIR::Visitor {
        StaticTraitResolve m_resolve;

    public:
        OuterVisitor(const ::HIR::Crate& crate)
            : ::HIR::Visitor(&m_resolve, crate.m_types)
            , m_resolve(crate)
        {
        }

        void visit_expr(::HIR::ExprPtr& exp) override {
            if (exp) {
                ExprVisitor_Extract ev(m_resolve);
                ev.visit_root(exp);
            }
        }
    };

    class OuterVisitor_Fixup: public ::HIR::Visitor {
        StaticTraitResolve m_resolve;

    public:
        OuterVisitor_Fixup(const ::HIR::Crate& crate)
            : ::HIR::Visitor(&m_resolve, crate.m_types)
            , m_resolve(crate)
        {
        }

        void visit_type(::HIR::TypeRef& ty) override {
            static Span sp;
            ::visit_type(sp, m_resolve, ty);
        }
    };
}

void HIR_Expand_ErasedType(::HIR::Crate& crate) {
    OuterVisitor ov(crate);
    ov.visit_crate(crate);

    OuterVisitor_Fixup ov_fix(crate);
    ov_fix.visit_crate(crate);
}
