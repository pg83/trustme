#pragma once

#include "hir_hir.h"
#include "hir_item_path.h"

class StaticTraitResolve;

namespace HIR {

    // TODO: Split into Visitor and ItemVisitor
    class Visitor {
        StaticTraitResolve* mResolve;
        TypeInterner& types;

    public:
        Visitor(::StaticTraitResolve* resolve, TypeInterner& types);

        virtual ~Visitor();

    protected:
        TypeInterner& type_interner() const { return types; }
        void visitTypeData(TypeData& data);

    public:
        virtual void visitCrate(::HIR::Crate& crate);

        virtual void visitModule(ItemPath p, ::HIR::Module& mod);

        virtual void visitTypeImpl(::HIR::TypeImpl& impl);
        virtual void visitInherentType(ItemPath p, ::HIR::TypeAlias& item);
        virtual void visitTraitImpl(const ::HIR::SimplePath& trait_path, ::HIR::TraitImpl& impl);
        virtual void visitMarkerImpl(const ::HIR::SimplePath& trait_path, ::HIR::MarkerImpl& impl);

        // - Type Items
        virtual void visitTypeAlias(ItemPath p, ::HIR::TypeAlias& item);
        virtual void visitTraitAlias(::HIR::ItemPath p, ::HIR::TraitAlias& item);
        virtual void visitTrait(ItemPath p, ::HIR::Trait& item);
        virtual void visitStruct(ItemPath p, ::HIR::Struct& item);
        virtual void visitEnum(ItemPath p, ::HIR::Enum& item);
        virtual void visitUnion(ItemPath p, ::HIR::Union& item);
        virtual void visitAssociatedtype(ItemPath p, ::HIR::AssociatedType& item);
        // - Value Items
        virtual void visitFunction(ItemPath p, ::HIR::Function& item);
        virtual void visitStatic(ItemPath p, ::HIR::Static& item);
        virtual void visitConstant(ItemPath p, ::HIR::Constant& item);

        // - Misc
        virtual void visitParams(::HIR::GenericParams& params);
        virtual void visitGenericBound(::HIR::GenericBound& bound);
        virtual void visitPattern(::HIR::Pattern& pat);
        virtual void visitPatternVal(::HIR::Pattern::Value& val);

        virtual void visitType(::HIR::TypeRef& tr);
        virtual void visitConstgeneric(::HIR::ConstGeneric& c);

        enum class PathContext {
            TYPE,
            TRAIT,

            VALUE,
        };
        virtual void visitTraitPath(::HIR::TraitPath& p);
        virtual void visitPath(::HIR::Path& p, PathContext);
        virtual void visitPathParams(::HIR::PathParams& p);
        virtual void visitGenericPath(::HIR::GenericPath& p, PathContext);

        virtual void visitExpr(::HIR::ExprPtr& exp);
    };

} // namespace HIR
