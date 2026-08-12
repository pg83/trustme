#pragma once

#include "hir_visitor.h"
#include "hir_typeck_static.h"

namespace MIR {

    class OuterVisitor: public ::HIR::Visitor {
    public:
        typedef ::std::function<void(const StaticTraitResolve& resolve, const ::HIR::ItemPath& ip, ::HIR::ExprPtr& expr, const ::HIR::Function::argsT& args, const ::HIR::TypeData* ret_type)> cbT;

    private:
        StaticTraitResolve mResolve;
        cbT cb;

    public:
        OuterVisitor(const ::HIR::Crate& crate, cbT cb);

        void visitExpr(::HIR::ExprPtr& exp) override;

        void visitType(::HIR::TypeRef& ty) override;
        void visitConstgeneric(::HIR::ConstGeneric& value) override;

        // ------
        // Code-containing items
        // ------
        void visitFunction(::HIR::ItemPath p, ::HIR::Function& item) override;
        void visitStatic(::HIR::ItemPath p, ::HIR::Static& item) override;
        void visitConstant(::HIR::ItemPath p, ::HIR::Constant& item) override;
        void visitStruct(::HIR::ItemPath p, ::HIR::Struct& item) override;
        void visitUnion(::HIR::ItemPath p, ::HIR::Union& item) override;
        void visitEnum(::HIR::ItemPath p, ::HIR::Enum& item) override;

        // Boilerplate
        void visitTrait(::HIR::ItemPath p, ::HIR::Trait& item) override;
        void visitTypeImpl(::HIR::TypeImpl& impl) override;
        void visitInherentType(::HIR::ItemPath p, ::HIR::TypeAlias& item) override;
        void visitTraitImpl(const ::HIR::SimplePath& trait_path, ::HIR::TraitImpl& impl) override;
    };

} // namespace MIR
