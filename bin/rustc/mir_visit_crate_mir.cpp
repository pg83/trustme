#include "mir_visit_crate_mir.h"
#include "hir_expr.h"

// NOTE: This is left here to ensure that any expressions that aren't handled by higher code cause a failure
void MIR::OuterVisitor::visitExpr(::HIR::ExprPtr& exp) {
    BUG(Span(), "visit_expr hit in OuterVisitor");
}

void MIR::OuterVisitor::visitType(::HIR::TypeRef& ty) {
    if (ty->is_Array()) {
        auto data = ty->cloneData();
        auto* e = data.opt_Array();
        this->visitType(e->inner);
        DEBUG("Array size " << ty);
        if (auto* se1 = e->size.opt_Unevaluated()) {
            if (auto* se = se1->opt_Unevaluated()) {
                cb(mResolve, ::HIR::ItemPath(""), *(*se)->expr, {}, mResolve.crate.types.primitive(::HIR::CoreType::Usize));
            }
        }
        ty = mResolve.crate.types.intern(mv$(data));
    } else {
        ::HIR::Visitor::visitType(ty);
    }
}

void MIR::OuterVisitor::visitConstgeneric(::HIR::ConstGeneric& value) {
    if (auto* unevaluated = value.opt_Unevaluated()) {
        auto& expr = *(**unevaluated).expr;
        cb(mResolve, ::HIR::ItemPath(""), expr, {}, expr->resType);
    }
}

void MIR::OuterVisitor::visitFunction(::HIR::ItemPath p, ::HIR::Function& item) {
    auto _ = this->mResolve.setItemGenerics(item.mParams);
    if (item.mCode || item.mCode.mir) {
        DEBUG("Function code " << p);

        ::HIR::TypeRef tmp;
        const auto& sp = item.mCode ? item.mCode->span() : Span();
        const auto& ret_ty = mResolve.fixTraitDefaultReturn(sp, p, item.returnType, tmp);
        cb(mResolve, p, item.mCode, item.mArgs, ret_ty);
    }
}

void MIR::OuterVisitor::visitStatic(::HIR::ItemPath p, ::HIR::Static& item) {
    auto _ = this->mResolve.setItemGenerics(item.mParams);
    if (item.mValue) {
        DEBUG("`static` value " << p);
        cb(mResolve, p, item.mValue, {}, item.mType);
    }
}

void MIR::OuterVisitor::visitConstant(::HIR::ItemPath p, ::HIR::Constant& item) {
    auto _ = this->mResolve.setItemGenerics(item.mParams);
    if (item.mValue) {
        DEBUG("`const` value " << p);
        cb(mResolve, p, item.mValue, {}, item.mType);
    }
}

void MIR::OuterVisitor::visitStruct(::HIR::ItemPath p, ::HIR::Struct& item) {
    auto _ = this->mResolve.setImplGenerics(item.structMarkings.dst_type, item.mParams);
    ::HIR::Visitor::visitStruct(p, item);
}

void MIR::OuterVisitor::visitUnion(::HIR::ItemPath p, ::HIR::Union& item) {
    auto _ = this->mResolve.setImplGenerics(MetadataType::Unknown, item.mParams);
    ::HIR::Visitor::visitUnion(p, item);
}

void MIR::OuterVisitor::visitEnum(::HIR::ItemPath p, ::HIR::Enum& item) {
    auto _ = this->mResolve.setImplGenerics(MetadataType::None, item.mParams);

    if (auto* e = item.mData.opt_Value()) {
        auto enumType = mResolve.crate.types.primitive(::HIR::Enum::getReprType(item.tagRepr));

        for (auto& var : e->variants) {
            if (var.expr) {
                cb(mResolve, p + var.name, var.expr, {}, enumType);
            }
        }
    }
}

void MIR::OuterVisitor::visitTrait(::HIR::ItemPath p, ::HIR::Trait& item) {
    auto _ = this->mResolve.setImplGenerics(MetadataType::TraitObject, item.mParams);
    ::HIR::Visitor::visitTrait(p, item);
}

void MIR::OuterVisitor::visitTypeImpl(::HIR::TypeImpl& impl) {
    auto _ = this->mResolve.setImplGenerics(impl.mType, impl.mParams);
    ::HIR::Visitor::visitTypeImpl(impl);
}

void MIR::OuterVisitor::visitInherentType(::HIR::ItemPath p, ::HIR::TypeAlias& item) {
    auto _ = this->mResolve.setItemGenerics(item.mParams);
    ::HIR::Visitor::visitInherentType(p, item);
}

void MIR::OuterVisitor::visitTraitImpl(const ::HIR::SimplePath& trait_path, ::HIR::TraitImpl& impl) {
    auto _ = this->mResolve.setImplGenerics(impl.mType, impl.mParams);
    ::HIR::Visitor::visitTraitImpl(trait_path, impl);
}

namespace MIR {

OuterVisitor::OuterVisitor(const ::HIR::Crate& crate, cbT cb)
    : HIR::Visitor(nullptr, crate.types)
    , mResolve(crate)
    , cb(cb) {
}
}
