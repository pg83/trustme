#include "hir_typeck_expr_visit.h"

#include "hir_hir.h"
#include "hir_expr.h"
#include "hir_visitor.h"
#include "hir_expr_state.h"

void TypecheckCode(const TypeckModuleState& ms, tArgs& args, const HIRTypeData* resultType, HIRExprPtr& expr) {
    if (expr.state->stage < HIRExprState::Stage::Typecheck) {
        TypecheckCodeCS(ms, args, resultType, expr);
        expr.state->stage = HIRExprState::Stage::Typecheck;
    }
}

void TypeckModuleState::prepareFromPath(const HIRItemPath& ip) {
    static Span sp;
    ASSERT_BUG(sp, ip.parent, "prepare_from_path with too-short path - " << ip);

    struct H {
        static const HIRModule& getModForIp(const HIRCrate& crate, const HIRItemPath& ip) {
            if (ip.parent) {
                const auto& mod = H::getModForIp(crate, *ip.parent);
                return mod.modItems.at(ip.name)->ent.as_Module();
            } else {
                assert(ip.crateName);
                return (ip.crateName[0] ? crate.extCrates.at(ip.crateName).mData->mRootModule : crate.mRootModule);
            }
        }

        static void addTraitsFromMod(TypeckModuleState& ms, const HIRModule& mod) {
            // In-scope traits.
            ms.traits.clear();
            for (const auto& tp : mod.traits) {
                const auto& trait = ms.crate.getTraitByPath(sp, tp);
                ms.traits.push_back(::std::make_pair(&tp, &trait));
            }
        }
    };

    if (ip.parent->trait && ip.parent->ty) {
        // Trait impl
        TODO(sp, "prepare_from_path - Trait impl " << ip);
    } else if (ip.parent->trait) {
        // Trait definition
        const auto& trait = crate.getTraitByPath(sp, *ip.parent->trait);
        const auto& item = trait.values.at(ip.name);
            TU_MATCH_HDRA( (item), { )
            TU_ARMA(Function, e) {
                mItemGenerics = &e.mParams;
            }
            TU_ARMA(Constant, e) {
                mItemGenerics = &e.mParams;
            }
            TU_ARMA(Static, e) {
                mItemGenerics = nullptr;
            }
            }
    } else if (ip.parent->ty) {
        // Inherent impl
        TODO(sp, "prepare_from_path - Type impl " << ip);
    } else {
        // Namespace path
        const auto& mod = H::getModForIp(crate, *ip.parent);
        H::addTraitsFromMod(*this, mod);
        const auto& item = mod.valueItems.at(ip.name)->ent;
        mImplGenerics = nullptr;
            TU_MATCH_HDRA( (item), { )
            TU_ARMA(Constant, e) {
                mItemGenerics = &e.mParams;
            }
            TU_ARMA(Static, e) {
            }
            TU_ARMA(Function, e) {
                mItemGenerics = &e.mParams;
            }
            TU_ARMA(StructConstant, _e) BUG(sp, ip << " is StructConstant");
            TU_ARMA(StructConstructor, _e) BUG(sp, ip << " is StructConstructor");
            TU_ARMA(Import, _e) BUG(sp, ip << " is Import");
            }
    }
}

namespace {

    class OuterVisitor: public HIRVisitor {
        TypeckModuleState ms;

    public:
        OuterVisitor(HIRCrate& crate)
            : HIRVisitor(nullptr, crate.types)
            , ms(crate)
        {
        }

    public:
        void visitModule(HIRItemPath p, HIRModule& mod) override {
            ms.pushTraits(p, mod);
            HIRVisitor::visitModule(p, mod);
            ms.popTraits(mod);
        }

        // NOTE: This is left here to ensure that any expressions that aren't handled by higher code cause a failure
        void visitExpr(HIRExprPtr& exp) override {
            if (exp.mir) {
                return;
            }
            BUG(exp->mSpan, "Reached expression");
        }

        void visitTrait(HIRItemPath p, HIRTrait& item) override {
            HIRGenericPath traitGpath;
            traitGpath.mPath = p.getSimplePath();
            for (size_t i = 0; i < item.mParams.types.size(); i++) {
                traitGpath.mParams.types.push_back(ms.crate.types.generic(item.mParams.types[i].mName, i));
            }
            for (size_t i = 0; i < item.mParams.values.size(); i++) {
                traitGpath.mParams.values.push_back(HIRGenericRef(item.mParams.values[i].mName, i));
            }
            auto _1 = this->ms.setCurrentTrait(traitGpath);
            auto _ = this->ms.setImplGenerics(item.mParams);
            HIRVisitor::visitTrait(p, item);
        }

        void visitTypeImpl(HIRTypeImpl& impl) override {
            TRACE_FUNCTION_F("impl " << impl.mType);
            auto _ = this->ms.setImplGenerics(impl.mParams);

            const auto& mod = this->ms.crate.getModByPath(Span(), impl.srcModule);
            ms.pushTraits(impl.srcModule, mod);
            HIRVisitor::visitTypeImpl(impl);
            ms.popTraits(mod);
        }

        void visitTraitImpl(const HIRSimplePath& traitPath, HIRTraitImpl& impl) override {
            TRACE_FUNCTION_F("impl " << traitPath << impl.traitArgs << " for " << impl.mType);
            auto traitGpath = HIRGenericPath(traitPath, impl.traitArgs.clone());
            auto _0 = this->ms.setCurrentTraitImpl(impl);
            auto _1 = this->ms.setCurrentTrait(traitGpath);
            auto _ = this->ms.setImplGenerics(impl.mParams);

            const auto& mod = this->ms.crate.getModByPath(Span(), impl.srcModule);
            ms.pushTraits(impl.srcModule, mod);
            ms.traits.push_back(::std::make_pair(&traitPath, &this->ms.crate.getTraitByPath(Span(), traitPath)));
            HIRVisitor::visitTraitImpl(traitPath, impl);
            ms.traits.pop_back();
            ms.popTraits(mod);
        }

        void visitMarkerImpl(const HIRSimplePath& traitPath, HIRMarkerImpl& impl) override {
            TRACE_FUNCTION_F("impl " << traitPath << " for " << impl.mType << " { }");
            auto _ = this->ms.setImplGenerics(impl.mParams);

            const auto& mod = this->ms.crate.getModByPath(Span(), impl.srcModule);
            ms.pushTraits(impl.srcModule, mod);
            HIRVisitor::visitMarkerImpl(traitPath, impl);
            ms.popTraits(mod);
        }

        void visitType(HIRTypeRef& ty) override {
            if (ty->is_Array()) {
                auto data = ty->cloneData();
                auto& e = data.as_Array();
                this->visitType(e.inner);
                DEBUG("Array size " << ty);
                tArgs tmp;
                if (auto* se = e.size.opt_Unevaluated()) {
                    if (se->is_Unevaluated()) {
                        TypecheckCode(ms, tmp, ms.crate.types.primitive(HIRCoreType::Usize), *se->as_Unevaluated()->expr);
                    }
                }
                ty = ms.crate.types.intern(std::move(data));
            } else {
                HIRVisitor::visitType(ty);
            }
        }

        // ------
        // Code-containing items
        // ------
        void visitFunction(HIRItemPath p, HIRFunction& item) override {
            auto _ = this->ms.setItemGenerics(item.mParams);
            if (item.mCode) {
                DEBUG("Function code " << p);
                TypecheckCode(ms, item.mArgs, item.returnType, item.mCode);
            } else {
                DEBUG("Function code " << p << " (none)");
            }
        }

        void visitStatic(HIRItemPath p, HIRStatic& item) override {
            if (item.mValue) {
                DEBUG("Static value " << p);
                tArgs tmp;
                TypecheckCode(ms, tmp, item.mType, item.mValue);
            }
        }

        void visitConstant(HIRItemPath p, HIRConstant& item) override {
            auto _ = this->ms.setItemGenerics(item.mParams);
            if (item.mValue) {
                DEBUG("Const value " << p);
                tArgs tmp;
                TypecheckCode(ms, tmp, item.mType, item.mValue);
            }
        }

        void visitEnum(HIRItemPath p, HIREnum& item) override {
            auto _ = this->ms.setItemGenerics(item.mParams);

            if (auto* e = item.mData.opt_Value()) {
                auto enumType = HIREnum::getReprType(item.tagRepr);

                for (auto& var : e->variants) {
                    DEBUG("Enum value " << p << " - " << var.name);
                    if (var.expr) {
                        tArgs tmp;
                        TypecheckCode(ms, tmp, ms.crate.types.primitive(enumType), var.expr);
                    }
                }
            }
        }
    };
}

void TypecheckExpressions(HIRCrate& crate) {
    OuterVisitor visitor{crate};
    visitor.visitCrate(crate);
}

TypeckModuleState::TypeckModuleState(const HIRCrate& crate)
    : crate(crate)
    , currentTrait(nullptr)
    , currentTraitImpl(nullptr)
    , mImplGenerics(nullptr)
    , mItemGenerics(nullptr)
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
    assert(!mImplGenerics);
    mImplGenerics = &gps;
    return NullOnDrop<const HIRGenericParams>(mImplGenerics);
}

TypeckModuleState::NullOnDrop<const HIRGenericParams> TypeckModuleState::setItemGenerics(const HIRGenericParams& gps) {
    assert(!mItemGenerics);
    mItemGenerics = &gps;
    return NullOnDrop<const HIRGenericParams>(mItemGenerics);
}

void TypeckModuleState::pushTraits(HIRItemPath p, const HIRModule& mod) {
    auto sp = Span();
    modPaths.push_back(p.getSimplePath());
    DEBUG("Module has " << mod.traits.size() << " in-scope traits");
    // - Push a NULL entry to prevent parent module import lists being searched
    traits.push_back(::std::make_pair(nullptr, nullptr));
    for (const auto& traitPath : mod.traits) {
        DEBUG("Push " << traitPath);
        traits.push_back(::std::make_pair(&traitPath, &this->crate.getTraitByPath(sp, traitPath)));
    }
}

void TypeckModuleState::popTraits(const HIRModule& mod) {
    DEBUG("Module has " << mod.traits.size() << " in-scope traits");
    for (unsigned int i = 0; i < mod.traits.size(); i++) {
        traits.pop_back();
    }
    traits.pop_back();
    modPaths.pop_back();
}
