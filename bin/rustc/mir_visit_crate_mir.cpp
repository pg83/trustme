#include "mir_visit_crate_mir.h"

#include "hir_expr.h"
#include "wire_board.h"

using namespace stl;

void MIROuterVisitor::visitExpr(HIRExprPtr& exp) {
    BUG(Span(), StringView("visit_expr hit in OuterVisitor"));
}

const HIRType* MIROuterVisitor::visitType(const HIRType* ty) {
    if (ty->is_Array()) {
        auto data = ty->cloneData();
        auto* e = data.opt_Array();
        e->inner = this->visitType(e->inner);
        DEBUG(StringView("Array size ") << ty);
        if (auto* se1 = e->size.opt_Unevaluated()) {
            if (auto* se = se1->opt_Unevaluated()) {
                cb.visit(resolve_, HIRItemPath(""), *(*se)->expr, {}, resolve_.hirCrate().types.primitive(HIRCoreType::Usize));
            }
        }
        return resolve_.hirCrate().types.intern(mv$(data));
    }
    return visitTypeDefaultViaHooks(ty);
}

void MIROuterVisitor::visitConstgeneric(HIRConstGeneric& value) {
    if (auto* unevaluated = value.opt_Unevaluated()) {
        auto& expr = *(**unevaluated).expr;
        cb.visit(resolve_, HIRItemPath(""), expr, {}, expr->resType);
    }
}

void MIROuterVisitor::visitFunction(HIRItemPath p, HIRFunction& item) {
    auto _ = this->resolve_.setItemGenerics(item.params);
    if (item.code || item.code.mir) {
        DEBUG(StringView("Function code ") << p);
        const HIRType* tmp;
        const auto& sp = item.code ? item.code->span() : Span();
        const auto* retTy = resolve_.fixTraitDefaultReturn(sp, p, item.returnType);
        cb.visit(resolve_, p, item.code, item.args, retTy);
    }
}

void MIROuterVisitor::visitStatic(HIRItemPath p, HIRStatic& item) {
    auto _ = this->resolve_.setItemGenerics(item.params);
    if (item.value) {
        DEBUG(StringView("`static` value ") << p);
        cb.visit(resolve_, p, item.value, {}, item.type);
    }
}

void MIROuterVisitor::visitConstant(HIRItemPath p, HIRConstant& item) {
    auto _ = this->resolve_.setItemGenerics(item.params);
    if (item.value) {
        DEBUG(StringView("`const` value ") << p);
        cb.visit(resolve_, p, item.value, {}, item.type);
    }
}

void MIROuterVisitor::visitStruct(HIRItemPath p, HIRStruct& item) {
    auto _ = this->resolve_.setImplGenerics(item.structMarkings.dstType, item.params);
    HIRVisitor::visitStruct(p, item);
}

void MIROuterVisitor::visitUnion(HIRItemPath p, HIRUnion& item) {
    auto _ = this->resolve_.setImplGenerics(MetadataType::Unknown, item.params);
    HIRVisitor::visitUnion(p, item);
}

void MIROuterVisitor::visitEnum(HIRItemPath p, HIREnum& item) {
    auto _ = this->resolve_.setImplGenerics(MetadataType::None, item.params);

    if (auto* e = item.data.opt_Value()) {
        auto enumType = resolve_.hirCrate().types.primitive(HIREnum::getReprType(item.tagRepr));

        for (auto& var : e->variants) {
            if (var.expr) {
                cb.visit(resolve_, p + var.name, var.expr, {}, enumType);
            }
        }
    }
}

void MIROuterVisitor::visitTrait(HIRItemPath p, HIRTrait& item) {
    auto _ = this->resolve_.setImplGenerics(MetadataType::TraitObject, item.params);
    HIRVisitor::visitTrait(p, item);
}

void MIROuterVisitor::visitTypeImpl(HIRTypeImpl& impl) {
    auto _ = this->resolve_.setImplGenerics(impl.type, impl.params);
    HIRVisitor::visitTypeImpl(impl);
}

void MIROuterVisitor::visitInherentType(HIRItemPath p, HIRTypeAlias& item) {
    auto _ = this->resolve_.setItemGenerics(item.params);
    HIRVisitor::visitInherentType(p, item);
}

void MIROuterVisitor::visitTraitImpl(const HIRSimplePath& traitPath, HIRTraitImpl& impl) {
    auto _ = this->resolve_.setImplGenerics(impl.type, impl.params);
    HIRVisitor::visitTraitImpl(traitPath, impl);
}

MIROuterVisitor::MIROuterVisitor(const WireBoard& wb, const HIRCrate& crate, MIRExprCallback& cb)
    : HIRVisitor(nullptr, crate.types)
    , resolve_(wb)
    , cb(cb)
{
}
