#pragma once

#include "hir_visitor.h"
#include "hir_typeck_static.h"

namespace MIR {

    class OuterVisitor: public ::HIR::Visitor {
    public:
        typedef ::std::function<void(const StaticTraitResolve& resolve, const ::HIR::ItemPath& ip, ::HIR::ExprPtr& expr, const ::HIR::Function::args_t& args, const ::HIR::TypeData* ret_type)> cb_t;

    private:
        StaticTraitResolve m_resolve;
        cb_t m_cb;

    public:
        OuterVisitor(const ::HIR::Crate& crate, cb_t cb);

        void visit_expr(::HIR::ExprPtr& exp) override;

        void visit_type(::HIR::TypeRef& ty) override;
        void visit_constgeneric(::HIR::ConstGeneric& value) override;

        // ------
        // Code-containing items
        // ------
        void visit_function(::HIR::ItemPath p, ::HIR::Function& item) override;
        void visit_static(::HIR::ItemPath p, ::HIR::Static& item) override;
        void visit_constant(::HIR::ItemPath p, ::HIR::Constant& item) override;
        void visit_struct(::HIR::ItemPath p, ::HIR::Struct& item) override;
        void visit_union(::HIR::ItemPath p, ::HIR::Union& item) override;
        void visit_enum(::HIR::ItemPath p, ::HIR::Enum& item) override;

        // Boilerplate
        void visit_trait(::HIR::ItemPath p, ::HIR::Trait& item) override;
        void visit_type_impl(::HIR::TypeImpl& impl) override;
        void visit_inherent_type(::HIR::ItemPath p, ::HIR::TypeAlias& item) override;
        void visit_trait_impl(const ::HIR::SimplePath& trait_path, ::HIR::TraitImpl& impl) override;
    };

} // namespace MIR
