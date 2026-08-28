#include "hir_typeck_expr_visit.h"

#include "hir_hir.h"
#include "hir_expr.h"
#include "wire_board.h"
#include "hir_visitor.h"
#include "hir_expr_state.h"

void TypecheckCode(const TypeckModuleState& ms, tArgs& args, const HIRTypeData* resultType, HIRExprPtr& expr) {
    if (expr.state->stage < HIRExprState::Stage::Typecheck) {
        TypecheckCodeCS(ms, args, resultType, expr);
        expr.state->stage = HIRExprState::Stage::Typecheck;
    }
}

void TypeckModuleState::prepareFromPath(const HIRItemPath& ip) {
    Span sp;
    ASSERT_BUG(sp, ip.parent, "prepare_from_path with too-short path - " << ip);

    struct H {
        static const HIRModule& getModForIp(const HIRCrate& crate, const HIRItemPath& ip) {
            if (ip.parent) {
                const auto& mod = H::getModForIp(crate, *ip.parent);
                return mod.modItems.at(ip.name)->ent.as_Module();
            } else {
                assert(ip.crateName);
                return (ip.crateName[0] ? crate.extCrates.at(ip.crateName).data->rootModule : crate.rootModule);
            }
        }

        static void addTraitsFromMod(TypeckModuleState& ms, const HIRModule& mod, const Span& sp) {
            ms.traits.clear();
            for (const auto& tp : mod.traits) {
                const auto& trait = ms.crate.getTraitByPath(sp, tp);
                ms.traits.push_back(::std::make_pair(&tp, &trait));
            }
        }
    };

    if (ip.parent->trait && ip.parent->ty) {
        TODO(sp, "prepare_from_path - Trait impl " << ip);
    } else if (ip.parent->trait) {
        const auto& trait = crate.getTraitByPath(sp, *ip.parent->trait);
        const auto& item = trait.values.at(ip.name);
        switch (item.tag()) {
            case HIRTraitValueItem::TAG_Function: {
                auto& e = item.as_Function();
                itemGenerics = &e.params;
                break;
            }
            case HIRTraitValueItem::TAG_Constant: {
                auto& e = item.as_Constant();
                itemGenerics = &e.params;
                break;
            }
            case HIRTraitValueItem::TAG_Static: {
                itemGenerics = nullptr;
                break;
            }
        }
    } else if (ip.parent->ty) {
        TODO(sp, "prepare_from_path - Type impl " << ip);
    } else {
        const auto& mod = H::getModForIp(crate, *ip.parent);
        H::addTraitsFromMod(*this, mod, sp);
        const auto& item = mod.valueItems.at(ip.name)->ent;
        implGenerics = nullptr;
        switch (item.tag()) {
            case HIRValueItem::TAG_Constant: {
                const auto& e = *item.as_Constant();
                itemGenerics = &e.params;
                break;
            }
            case HIRValueItem::TAG_Static: {
                break;
            }
            case HIRValueItem::TAG_Function: {
                const auto& e = *item.as_Function();
                itemGenerics = &e.params;
                break;
            }
            case HIRValueItem::TAG_StructConstant: {
                BUG(sp, ip << " is StructConstant");
                break;
            }
            case HIRValueItem::TAG_StructConstructor: {
                BUG(sp, ip << " is StructConstructor");
                break;
            }
            case HIRValueItem::TAG_Import: {
                BUG(sp, ip << " is Import");
                break;
            }
        }
    }
}

namespace {

    struct OuterVisitor: public HIRVisitor {
        TypeckModuleState ms;

        OuterVisitor(const WireBoard& wb, HIRCrate& crate);

        void visitModule(HIRItemPath p, HIRModule& mod) override;

        void visitExpr(HIRExprPtr& exp) override;

        void visitTrait(HIRItemPath p, HIRTrait& item) override;

        void visitTypeImpl(HIRTypeImpl& impl) override;

        void visitTraitImpl(const HIRSimplePath& traitPath, HIRTraitImpl& impl) override;

        void visitMarkerImpl(const HIRSimplePath& traitPath, HIRMarkerImpl& impl) override;

        [[nodiscard]] HIRTypeRef visitType(HIRTypeRef ty) override;

        void visitGlobalAssembly(HIRGlobalAssembly& item) override;

        void visitFunction(HIRItemPath p, HIRFunction& item) override;

        void visitStatic(HIRItemPath p, HIRStatic& item) override;

        void visitConstant(HIRItemPath p, HIRConstant& item) override;

        void visitEnum(HIRItemPath p, HIREnum& item) override;
    };
}

void TypecheckExpressions(const WireBoard& wb, HIRCrate& crate) {
    OuterVisitor visitor{wb, crate};
    visitor.visitCrate(crate);
}

TypeckModuleState::TypeckModuleState(const WireBoard& wb)
    : wb(wb)
    , crate(*wb.crate)
    , currentTrait(nullptr)
    , currentTraitImpl(nullptr)
    , implGenerics(nullptr)
    , itemGenerics(nullptr)
{
}

TypeckModuleState::NullOnDrop<const HIRGenericPath> TypeckModuleState::setCurrentTrait(const HIRGenericPath& p) {
    assert(!currentTrait);
    currentTrait = &p;
    return NullOnDrop<const HIRGenericPath>(currentTrait);
}

TypeckModuleState::NullOnDrop<const HIRTraitImpl> TypeckModuleState::setCurrentTraitImpl(const HIRTraitImpl& impl) {
    assert(!currentTraitImpl);
    currentTraitImpl = &impl;
    return NullOnDrop<const HIRTraitImpl>(currentTraitImpl);
}

TypeckModuleState::NullOnDrop<const HIRGenericParams> TypeckModuleState::setImplGenerics(const HIRGenericParams& gps) {
    assert(!implGenerics);
    implGenerics = &gps;
    return NullOnDrop<const HIRGenericParams>(implGenerics);
}

TypeckModuleState::NullOnDrop<const HIRGenericParams> TypeckModuleState::setItemGenerics(const HIRGenericParams& gps) {
    assert(!itemGenerics);
    itemGenerics = &gps;
    return NullOnDrop<const HIRGenericParams>(itemGenerics);
}

void TypeckModuleState::pushTraits(HIRItemPath p, const HIRModule& mod) {
    auto sp = Span();
    modPaths.push_back(p.getSimplePath());
    traits.push_back(::std::make_pair(nullptr, nullptr));
    for (const auto& traitPath : mod.traits) {
        traits.push_back(::std::make_pair(&traitPath, &this->crate.getTraitByPath(sp, traitPath)));
    }
}

void TypeckModuleState::popTraits(const HIRModule& mod) {
    for (unsigned int i = 0; i < mod.traits.size(); i++) {
        traits.pop_back();
    }
    traits.pop_back();
    modPaths.pop_back();
}

OuterVisitor::OuterVisitor(const WireBoard& wb, HIRCrate& crate)
    : HIRVisitor(nullptr, crate.types)
    , ms(wb)
{
}

auto OuterVisitor::visitModule(HIRItemPath p, HIRModule& mod) -> void {
    ms.pushTraits(p, mod);
    HIRVisitor::visitModule(p, mod);
    ms.popTraits(mod);
}

auto OuterVisitor::visitExpr(HIRExprPtr& exp) -> void {
    if (exp.mir) {
        return;
    }
    BUG(exp->span_, "Reached expression");
}

auto OuterVisitor::visitTrait(HIRItemPath p, HIRTrait& item) -> void {
    HIRGenericPath traitGpath;
    traitGpath.path = p.getSimplePath();
    for (size_t i = 0; i < item.params.types.size(); i++) {
        traitGpath.params.types.push_back(ms.crate.types.generic(item.params.types[i].name, i));
    }
    for (size_t i = 0; i < item.params.values.size(); i++) {
        traitGpath.params.values.push_back(HIRGenericRef(item.params.values[i].name, i));
    }
    auto _1 = this->ms.setCurrentTrait(traitGpath);
    auto _ = this->ms.setImplGenerics(item.params);
    HIRVisitor::visitTrait(p, item);
}

auto OuterVisitor::visitTypeImpl(HIRTypeImpl& impl) -> void {
    auto _ = this->ms.setImplGenerics(impl.params);

    const auto& mod = this->ms.crate.getModByPath(Span(), impl.srcModule);
    ms.pushTraits(impl.srcModule, mod);
    HIRVisitor::visitTypeImpl(impl);
    ms.popTraits(mod);
}

auto OuterVisitor::visitTraitImpl(const HIRSimplePath& traitPath, HIRTraitImpl& impl) -> void {
    auto traitGpath = HIRGenericPath(traitPath, impl.traitArgs.clone());
    auto _0 = this->ms.setCurrentTraitImpl(impl);
    auto _1 = this->ms.setCurrentTrait(traitGpath);
    auto _ = this->ms.setImplGenerics(impl.params);

    const auto& mod = this->ms.crate.getModByPath(Span(), impl.srcModule);
    ms.pushTraits(impl.srcModule, mod);
    ms.traits.push_back(::std::make_pair(&traitPath, &this->ms.crate.getTraitByPath(Span(), traitPath)));
    HIRVisitor::visitTraitImpl(traitPath, impl);
    ms.traits.pop_back();
    ms.popTraits(mod);
}

auto OuterVisitor::visitMarkerImpl(const HIRSimplePath& traitPath, HIRMarkerImpl& impl) -> void {
    auto _ = this->ms.setImplGenerics(impl.params);

    const auto& mod = this->ms.crate.getModByPath(Span(), impl.srcModule);
    ms.pushTraits(impl.srcModule, mod);
    HIRVisitor::visitMarkerImpl(traitPath, impl);
    ms.popTraits(mod);
}

[[nodiscard]] auto OuterVisitor::visitType(HIRTypeRef ty) -> HIRTypeRef {
    if (ty->is_Array()) {
        auto data = ty->cloneData();
        auto& e = data.as_Array();
        e.inner = this->visitType(e.inner);
        tArgs tmp;
        if (auto* se = e.size.opt_Unevaluated()) {
            if (se->is_Unevaluated()) {
                TypecheckCode(ms, tmp, ms.crate.types.primitive(HIRCoreType::Usize), *se->as_Unevaluated()->expr);
            }
        }
        return ms.crate.types.intern(std::move(data));
    }
    return HIRVisitor::visitType(ty);
}

auto OuterVisitor::visitGlobalAssembly(HIRGlobalAssembly& item) -> void {
    for (auto& operand : item.operands) {
        if (auto* value = operand.opt_Const()) {
            ASSERT_BUG(item.span, value->value.is_Unevaluated(), "global_asm const operand was evaluated before type checking");
            auto& expr = *value->value.as_Unevaluated()->expr;
            tArgs args;
            TypecheckCode(ms, args, nullptr, expr);
            value->type = expr->resType;
            if (!value->type->is_Primitive() || !isInteger(value->type->as_Primitive())) {
                ERROR(item.span, E0000, "global_asm const operand must have an integer type, got " << value->type);
            }
        }
    }
}

auto OuterVisitor::visitFunction(HIRItemPath p, HIRFunction& item) -> void {
    auto _ = this->ms.setItemGenerics(item.params);
    if (item.code) {
        TypecheckCode(ms, item.args, item.traitReturnType.value_or(item.returnType), item.code);
    } else {
    }
}

auto OuterVisitor::visitStatic(HIRItemPath p, HIRStatic& item) -> void {
    if (item.value) {
        tArgs tmp;
        TypecheckCode(ms, tmp, item.type, item.value);
    }
}

auto OuterVisitor::visitConstant(HIRItemPath p, HIRConstant& item) -> void {
    auto _ = this->ms.setItemGenerics(item.params);
    if (item.value) {
        tArgs tmp;
        TypecheckCode(ms, tmp, item.type, item.value);
    }
}

auto OuterVisitor::visitEnum(HIRItemPath p, HIREnum& item) -> void {
    auto _ = this->ms.setItemGenerics(item.params);

    if (auto* e = item.data.opt_Value()) {
        auto enumType = HIREnum::getReprType(item.tagRepr);

        for (auto& var : e->variants) {
            if (var.expr) {
                tArgs tmp;
                TypecheckCode(ms, tmp, ms.crate.types.primitive(enumType), var.expr);
            }
        }
    }
}
