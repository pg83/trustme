#include "mir_visit_crate_mir.h"

#include "hir_expr.h"
#include "wire_board.h"

// NOTE: This is left here to ensure that any expressions that aren't handled by higher code cause a failure
void MIROuterVisitor::visitExpr(HIRExprPtr& exp) {
    BUG(Span(), "visit_expr hit in OuterVisitor");
}

void MIROuterVisitor::visitType(HIRTypeRef& ty) {
    if (ty->is_Array()) {
        auto data = ty->cloneData();
        auto* e = data.opt_Array();
        this->visitType(e->inner);
        DEBUG("Array size " << ty);
        if (auto* se1 = e->size.opt_Unevaluated()) {
            if (auto* se = se1->opt_Unevaluated()) {
                cb(resolve_, HIRItemPath(""), *(*se)->expr, {}, resolve_.hirCrate().types.primitive(HIRCoreType::Usize));
            }
        }
        ty = resolve_.hirCrate().types.intern(mv$(data));
    } else {
        HIRVisitor::visitType(ty);
    }
}

void MIROuterVisitor::visitConstgeneric(HIRConstGeneric& value) {
    if (auto* unevaluated = value.opt_Unevaluated()) {
        auto& expr = *(**unevaluated).expr;
        cb(resolve_, HIRItemPath(""), expr, {}, expr->resType);
    }
}

void MIROuterVisitor::visitFunction(HIRItemPath p, HIRFunction& item) {
    auto _ = this->resolve_.setItemGenerics(item.mParams);
    if (item.mCode || item.mCode.mir) {
        DEBUG("Function code " << p);

        HIRTypeRef tmp;
        const auto& sp = item.mCode ? item.mCode->span() : Span();
        const auto& retTy = resolve_.fixTraitDefaultReturn(sp, p, item.returnType, tmp);
        cb(resolve_, p, item.mCode, item.mArgs, retTy);
    }
}

void MIROuterVisitor::visitStatic(HIRItemPath p, HIRStatic& item) {
    auto _ = this->resolve_.setItemGenerics(item.mParams);
    if (item.mValue) {
        DEBUG("`static` value " << p);
        cb(resolve_, p, item.mValue, {}, item.mType);
    }
}

void MIROuterVisitor::visitConstant(HIRItemPath p, HIRConstant& item) {
    auto _ = this->resolve_.setItemGenerics(item.mParams);
    if (item.mValue) {
        DEBUG("`const` value " << p);
        cb(resolve_, p, item.mValue, {}, item.mType);
    }
}

void MIROuterVisitor::visitStruct(HIRItemPath p, HIRStruct& item) {
    auto _ = this->resolve_.setImplGenerics(item.structMarkings.dstType, item.mParams);
    HIRVisitor::visitStruct(p, item);
}

void MIROuterVisitor::visitUnion(HIRItemPath p, HIRUnion& item) {
    auto _ = this->resolve_.setImplGenerics(MetadataType::Unknown, item.mParams);
    HIRVisitor::visitUnion(p, item);
}

void MIROuterVisitor::visitEnum(HIRItemPath p, HIREnum& item) {
    auto _ = this->resolve_.setImplGenerics(MetadataType::None, item.mParams);

    if (auto* e = item.mData.opt_Value()) {
        auto enumType = resolve_.hirCrate().types.primitive(HIREnum::getReprType(item.tagRepr));

        for (auto& var : e->variants) {
            if (var.expr) {
                cb(resolve_, p + var.name, var.expr, {}, enumType);
            }
        }
    }
}

void MIROuterVisitor::visitTrait(HIRItemPath p, HIRTrait& item) {
    auto _ = this->resolve_.setImplGenerics(MetadataType::TraitObject, item.mParams);
    HIRVisitor::visitTrait(p, item);
}

void MIROuterVisitor::visitTypeImpl(HIRTypeImpl& impl) {
    auto _ = this->resolve_.setImplGenerics(impl.mType, impl.mParams);
    HIRVisitor::visitTypeImpl(impl);
}

void MIROuterVisitor::visitInherentType(HIRItemPath p, HIRTypeAlias& item) {
    auto _ = this->resolve_.setItemGenerics(item.mParams);
    HIRVisitor::visitInherentType(p, item);
}

void MIROuterVisitor::visitTraitImpl(const HIRSimplePath& traitPath, HIRTraitImpl& impl) {
    auto _ = this->resolve_.setImplGenerics(impl.mType, impl.mParams);
    HIRVisitor::visitTraitImpl(traitPath, impl);
}

MIROuterVisitor::MIROuterVisitor(const WireBoard& wb, const HIRCrate& crate, cbT cb)
    : HIRVisitor(nullptr, crate.types)
    , resolve_(wb)
    , cb(cb)
{
}
