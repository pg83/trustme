#include "hir_from_ast.h"
#include "common.h"
#include "hir_hir.h"
#include "hir_main_bindings.h"
#include "hir_conv_main_bindings.h"
#include "ast_ast.h"
#include "ast_expr.h" // For shortcut in array size handling
#include "ast_crate.h"
#include <std/mem/obj_pool.h>
#include "hir_visitor.h"
#include "macro_rules_macro_rules.h"
#include "hir_item_path.h"
#include <limits.h>
#include "hir_typeck_helpers.h" // monomorph
#include "trans_target.h"
#include <unordered_set>
#include "hir_expr_ptr.h"
#include "hir_expr.h"

::HIR::ExprPtr LowerHIRExpr(const ASTExpr& e);
::HIR::Module LowerHIRModule(const ASTModule& module, ::HIR::ItemPath path, ::std::vector<::HIR::SimplePath> traits = {});
::HIR::Function LowerHIRFunction(::HIR::ItemPath path, const ASTAttributeList& attrs, const ASTFunction& f, const ::HIR::TypeData* selfType);
::HIR::ValueItem LowerHIRStatic(::HIR::ItemPath p, const ASTAttributeList& attrs, const ASTStatic& e, const Span& sp, const RcString& name);
::HIR::PathParams LowerHIRPathParams(const Span& sp, const ASTPathParams& srcParams, bool allowAssoc);
::HIR::ConstGeneric LowerHIRConstGeneric(const ASTExprNode& nodeRef);
::HIR::TraitPath LowerHIRTraitPath(const Span& sp, const ASTPath& path, const ASTHigherRankedBounds& hrbs, bool allowBounds = false, ASTBoundConstness constness = ASTBoundConstness::Never);
::HIR::GenericParams LowerHIRHigherRankedBounds(const ASTHigherRankedBounds& hrbs);

::HIR::SimplePath pathSized;
::HIR::SimplePath pathPointeeSized;
::HIR::SimplePath pathMetadataSized;
RcString gCoreCrate;
RcString gCrateName;
::HIR::Crate* gCratePtr = nullptr;
const ASTCrate* gAstCratePtr;

namespace {
    ::HIR::BoundConstness LowerHIRBoundConstness(ASTBoundConstness v) {
        switch (v) {
        case ASTBoundConstness::Never: return ::HIR::BoundConstness::Never;
        case ASTBoundConstness::Always: return ::HIR::BoundConstness::Always;
        case ASTBoundConstness::Maybe: return ::HIR::BoundConstness::Maybe;
        }
        throw "Invalid bound constness";
    }
}

// --------------------------------------------------------------------
HIR::LifetimeRef LowerHIRLifetimeRef(const ASTLifetimeRef& r) {
    assert(r.binding() >= 0xFFF0 || r.binding() < 1024);
    return HIR::LifetimeRef(
        // TODO: names?
        r.binding()
    );
}

::HIR::Publicity LowerHIRVis(const ::HIR::SimplePath& modPath, const ASTVisibility& vis) {
    if (vis.isGlobal()) {
        return ::HIR::Publicity::newGlobal();
    }
    const auto* ap = &vis.visPath();
    return ::HIR::Publicity::newPriv(::HIR::SimplePath((ap->crate == "" ? gCrateName : ap->crate), ap->nodes));
}

::HIR::GenericParams LowerHIRGenericParams(const ASTGenericParams& gp, bool* selfIsSized) {
    ::HIR::GenericParams rv;

    for (const auto& param : gp.mParams) {
        TU_MATCH_HDRA( (param), {)
        TU_ARMA(None, _) {
            }
            TU_ARMA(Lifetime, lftDef) {
                rv.mLifetimes.push_back(HIR::LifetimeDef{lftDef.name().name});
            }
            TU_ARMA(Type, tp) {
                rv.types.push_back({tp.name(), LowerHIRType(tp.getDefault()), true});
            }
            TU_ARMA(Value, tp) {
                rv.values.push_back(HIR::ValueParamDef{tp.name().name, LowerHIRType(tp.type()), tp.defaultValue() ? LowerHIRConstGeneric(tp.defaultValue().node()) : ::HIR::ConstGeneric::make_Infer({})});
            }
        }
    }

    for (const auto& bound : gp.bounds) {
        TU_MATCH_HDRA( (bound), {)
        TU_ARMA(None, e) {
            }
            TU_ARMA(Lifetime, e) {
                rv.bounds.push_back(::HIR::GenericBound::make_Lifetime({LowerHIRLifetimeRef(e.test), LowerHIRLifetimeRef(e.bound)}));
            }
            TU_ARMA(TypeLifetime, e) {
                rv.bounds.push_back(::HIR::GenericBound::make_TypeLifetime({LowerHIRType(e.type), LowerHIRLifetimeRef(e.bound)}));
            }
            TU_ARMA(IsTrait, e) {
                //const auto& sp = e.span;
                auto type = LowerHIRType(e.type);

                // TODO: Check if this trait is `Sized` and ignore if it is? (It's a useless bound)

                if (!e.outerHrbs.empty() && !e.innerHrbs.empty()) {
                    // NOTE: rustc doesn't allow this (E0316)
                    TODO(bound.span, "Handle two layers of HRBs in a bound");
                }

                auto boundTraitPath = LowerHIRTraitPath(bound.span, e.trait, e.innerHrbs, /*allow_bounds=*/true, e.constness);
                auto tpBounds = mv$(boundTraitPath.traitBounds);
                boundTraitPath.traitBounds.clear();

                // 1.90 added some traits that imply ?Sized
                if (boundTraitPath.mPath.mPath == pathPointeeSized || boundTraitPath.mPath.mPath == pathMetadataSized) {
                    if (const auto* ge = type->opt_Generic()) {
                        if (ge->binding == GENERICSelf) {
                            *selfIsSized = false;
                        } else {
                            auto idx = ge->idx();
                            ASSERT_BUG(bound.span, idx < rv.types.size(), "Bounded type out of bounds: " << ge->binding << " " << type);
                            rv.types[idx].isSized = false;
                        }
                    }
                }

                rv.bounds.push_back(::HIR::GenericBound::make_TraitBound({box$(LowerHIRHigherRankedBounds(e.outerHrbs)), type, mv$(boundTraitPath), LowerHIRBoundConstness(e.constness)}));

                for (auto& bound : tpBounds) {
                    const auto& name = bound.first;
                    const auto& srcTrait = bound.second.sourceTrait;
                    const auto& params = bound.second.atyParams;
                    for (auto& trait : bound.second.traits) {
                        std::unique_ptr<HIR::GenericParams> hrls;
                        if (!e.outerHrbs.empty()) {
                            hrls = box$(LowerHIRHigherRankedBounds(e.outerHrbs));
                        }
                        if (!e.innerHrbs.empty()) {
                            hrls = box$(LowerHIRHigherRankedBounds(e.innerHrbs));
                        }
                        rv.bounds.push_back(::HIR::GenericBound::make_TraitBound({std::move(hrls), gCratePtr->types.path(::HIR::Path(type, srcTrait.clone(), name, params.clone()), {}), std::move(trait)}));
                    }
                    bound.second.traits.clear();
                }
            }
            TU_ARMA(MaybeTrait, e) {
                auto type = LowerHIRType(e.type);
                if (!type->is_Generic()) {
                    BUG(bound.span, "MaybeTrait on non-param - " << type);
                }
                const auto& ge = type->as_Generic();
                unsigned paramIdx;
                if (ge.binding == 0xFFFF) {
                    if (!selfIsSized) {
                        BUG(bound.span, "MaybeTrait on parameter on Self when not allowed");
                    }
                    paramIdx = 0xFFFF;
                } else {
                    paramIdx = ge.idx();
                    if (paramIdx >= rv.types.size()) {
                        BUG(bound.span, "MaybeTrait on parameter not in parameter list (#" << ge.binding << ")");
                    }
                }

                // Compare with list of known default traits (just Sized atm) and set a marker
                auto trait = LowerHIRGenericPath(bound.span, e.trait, FromASTPathClass::Type);
                if (trait.mPath == pathSized) {
                    if (paramIdx == 0xFFFF) {
                        assert(selfIsSized);
                        *selfIsSized = false;
                    } else {
                        assert(paramIdx < rv.types.size());
                        rv.types[paramIdx].isSized = false;
                    }
                } else {
                    ERROR(bound.span, E0000, "MaybeTrait on unknown trait " << trait.mPath);
                }
            }
            TU_ARMA(NotTrait, e) {
                TODO(bound.span, "Negative trait bounds");
            }
            TU_ARMA(Equality, e) {
                rv.bounds.push_back(::HIR::GenericBound::make_TypeEquality({LowerHIRType(e.type), LowerHIRType(e.replacement)}));
            }
        }
    }

    return rv;
}

::HIR::Path LowerHIRPatternPath(const Span& sp, const ASTPath& path, FromASTPathClass pc) {
    if (const auto* be = path.mBindings.type.binding.opt_TypeParameter()) {
        if (be->slot == GENERICSelf) {
            // HACK: Return `<Self>::` (to be expanded later on)
            return ::HIR::Path(gCratePtr->types.self(), "");
        }
    }
    return LowerHIRPath(sp, path, pc);
}

namespace {
    ::HIR::PatternBinding::Type convertBindingType(ASTPatternBinding::Type pbt) {
        switch (pbt) {
            case ASTPatternBinding::Type::MOVE:
                return ::HIR::PatternBinding::Type::Move;
            case ASTPatternBinding::Type::REF:
                return ::HIR::PatternBinding::Type::Ref;
            case ASTPatternBinding::Type::MUTREF:
                return ::HIR::PatternBinding::Type::MutRef;
        }
        throw "";
    }
}

::HIR::Pattern LowerHIRPattern(const ASTPattern& pat) {
    TRACE_FUNCTION_F("@" << pat.span() << " pat = " << pat);

    std::vector<::HIR::PatternBinding> bindings;
    for (const auto& pb : pat.bindings()) {
        bindings.push_back(::HIR::PatternBinding(pb.isMutable, convertBindingType(pb.mType), pb.mName.name, pb.slot));
    }

    struct H {
        static ::std::vector<::HIR::Pattern> lowerhirPatternvec(const ::std::vector<ASTPattern>& subPatterns) {
            ::std::vector<::HIR::Pattern> rv;
            for (const auto& sp : subPatterns) {
                rv.push_back(LowerHIRPattern(sp));
            }
            return rv;
        }

        static ::HIR::CoreType getIntType(const Span& sp, const ::eCoreType ct) {
            switch (ct) {
                case CORETYPE_ANY:
                    return ::HIR::CoreType::Str;

                case CORETYPE_I8:
                    return ::HIR::CoreType::I8;
                case CORETYPE_U8:
                    return ::HIR::CoreType::U8;
                case CORETYPE_I16:
                    return ::HIR::CoreType::I16;
                case CORETYPE_U16:
                    return ::HIR::CoreType::U16;
                case CORETYPE_I32:
                    return ::HIR::CoreType::I32;
                case CORETYPE_U32:
                    return ::HIR::CoreType::U32;
                case CORETYPE_I64:
                    return ::HIR::CoreType::I64;
                case CORETYPE_U64:
                    return ::HIR::CoreType::U64;

                case CORETYPE_INT:
                    return ::HIR::CoreType::Isize;
                case CORETYPE_UINT:
                    return ::HIR::CoreType::Usize;

                case CORETYPE_CHAR:
                    return ::HIR::CoreType::Char;

                case CORETYPE_BOOL:
                    return ::HIR::CoreType::Bool;

                default:
                    BUG(sp, "Unknown type for integer literal in pattern - " << ct);
            }
        }

        static ::HIR::CoreType getFloatType(const Span& sp, const ::eCoreType ct) {
            switch (ct) {
                case CORETYPE_ANY:
                    return ::HIR::CoreType::Str;
                case CORETYPE_F16:
                    return ::HIR::CoreType::F16;
                case CORETYPE_F32:
                    return ::HIR::CoreType::F32;
                case CORETYPE_F64:
                    return ::HIR::CoreType::F64;
                case CORETYPE_F128:
                    return ::HIR::CoreType::F128;
                default:
                    BUG(sp, "Unknown type for float literal in pattern - " << ct);
            }
        }

        static ::HIR::Pattern::Value lowerhirPatternValue(const Span& sp, const ASTPattern::Value& v) {
            TU_MATCH_HDRA((v), {)
            TU_ARMA(Invalid, e) {
                    BUG(sp, "Encountered Invalid value in Pattern");
                }
                TU_ARMA(Integer, e) {
                    return ::HIR::Pattern::Value::make_Integer({H::getIntType(sp, e.type), e.value});
                }
                TU_ARMA(Float, e) {
                    return ::HIR::Pattern::Value::make_Float({H::getFloatType(sp, e.type), e.value});
                }
                TU_ARMA(String, e) {
                    return ::HIR::Pattern::Value::make_String(e);
                }
                TU_ARMA(ByteString, e) {
                    return ::HIR::Pattern::Value::make_ByteString({e.v});
                }
                TU_ARMA(Named, e) {
                    return ::HIR::Pattern::Value::make_Named({LowerHIRPatternPath(sp, e, FromASTPathClass::Value), nullptr});
                }
            }
            throw "BUGCHECK: Reached end of LowerHIR_Pattern::H::lowerhir_pattern_value";
        }
    };

    TU_MATCH_HDRA( (pat.data()), {)
    TU_ARMA(MaybeBind, e) {
            BUG(pat.span(), "Encountered MaybeBind pattern");
        }
        TU_ARMA(Macro, e) {
            BUG(pat.span(), "Encountered Macro pattern");
        }
        TU_ARMA(Any, e)
        return ::HIR::Pattern{mv$(bindings), ::HIR::Pattern::Data::make_Any({})};
        TU_ARMA(Box, e)
        return ::HIR::Pattern{mv$(bindings), ::HIR::Pattern::Data::make_Box({box$(LowerHIRPattern(*e.sub))})};
        TU_ARMA(Ref, e)
        return ::HIR::Pattern{mv$(bindings), ::HIR::Pattern::Data::make_Ref({(e.mut ? ::HIR::BorrowType::Unique : ::HIR::BorrowType::Shared), box$(LowerHIRPattern(*e.sub))})};
        TU_ARMA(Tuple, e) {
            auto leading = H::lowerhirPatternvec(e.start);
            auto trailing = H::lowerhirPatternvec(e.end);

            if (e.hasWildcard) {
                return ::HIR::Pattern(mv$(bindings), ::HIR::Pattern::Data::make_SplitTuple({mv$(leading), mv$(trailing)}));
            } else {
                assert(trailing.size() == 0);
                return ::HIR::Pattern(mv$(bindings), ::HIR::Pattern::Data::make_Tuple({mv$(leading)}));
            }
        }
        ///
        /// Named tuple pattern
        ///
        TU_ARMA(StructTuple, e) {
            auto leading = H::lowerhirPatternvec(e.tupPat.start);
            auto trailing = H::lowerhirPatternvec(e.tupPat.end);

            if (!e.tupPat.hasWildcard) {
                assert(trailing.size() == 0);
            }

            return ::HIR::Pattern(
                mv$(bindings),
                ::HIR::Pattern::Data::make_PathTuple({
                    LowerHIRPatternPath(pat.span(), e.path, FromASTPathClass::Value),
                    ::HIR::Pattern::PathBinding(),
                    mv$(leading),
                    e.tupPat.hasWildcard,
                    mv$(trailing),
                    0 // Total size unknown still
                })
            );
        }
        ///
        /// Struct pattern
        ///
        TU_ARMA(Struct, e) {
            ::std::vector<::std::pair<RcString, ::HIR::Pattern>> subPatterns;
            for (const auto& sp : e.subPatterns) {
                subPatterns.push_back(::std::make_pair(sp.name, LowerHIRPattern(sp.pat)));
            }

            // No sub-patterns, no `..`, and the VALUE binding points to an enum variant
            if (e.subPatterns.empty() /*&& !e.is_exhaustive*/) {
                if (/*const auto* pbp =*/e.path.mBindings.value.binding.opt_EnumVar()) {
                    return ::HIR::Pattern{
                        mv$(bindings),
                        ::HIR::Pattern::Data::make_PathNamed(
                            {LowerHIRGenericPath(pat.span(), e.path, FromASTPathClass::Value),
                             //::HIR::Pattern::PathBinding::make_Enum({ pbp->hir, pbp->idx }),
                             ::HIR::Pattern::PathBinding(),
                             mv$(subPatterns),
                             e.isExhaustive}
                        )
                    };
                }
            }

            return ::HIR::Pattern(mv$(bindings), ::HIR::Pattern::Data::make_PathNamed({LowerHIRPatternPath(pat.span(), e.path, FromASTPathClass::Type), ::HIR::Pattern::PathBinding(), mv$(subPatterns), e.isExhaustive}));
        }

        TU_ARMA(Value, e) {
            if (e.end.is_Invalid()) {
                return ::HIR::Pattern{mv$(bindings), ::HIR::Pattern::Data::make_Value({H::lowerhirPatternValue(pat.span(), e.start)})};
            } else if (e.start.is_Invalid()) {
                return ::HIR::Pattern{mv$(bindings), ::HIR::Pattern::Data::make_Range({{}, box$(H::lowerhirPatternValue(pat.span(), e.end)), true})};
            } else {
                return ::HIR::Pattern{mv$(bindings), ::HIR::Pattern::Data::make_Range({box$(H::lowerhirPatternValue(pat.span(), e.start)), box$(H::lowerhirPatternValue(pat.span(), e.end)), true})};
            }
        }
        TU_ARMA(ValueLeftInc, e) {
            if (e.end.is_Invalid()) {
                return ::HIR::Pattern{mv$(bindings), ::HIR::Pattern::Data::make_Range({box$(H::lowerhirPatternValue(pat.span(), e.start)), {}, false})};
            }
            if (e.start.is_Invalid()) {
                return ::HIR::Pattern{mv$(bindings), ::HIR::Pattern::Data::make_Range({{}, box$(H::lowerhirPatternValue(pat.span(), e.end)), false})};
            }
            return ::HIR::Pattern{mv$(bindings), ::HIR::Pattern::Data::make_Range({box$(H::lowerhirPatternValue(pat.span(), e.start)), box$(H::lowerhirPatternValue(pat.span(), e.end)), false})};
        }
        TU_ARMA(Slice, e) {
            ::std::vector<::HIR::Pattern> leading;
            for (const auto& sp : e.subPats) {
                leading.push_back(LowerHIRPattern(sp));
            }
            return ::HIR::Pattern{mv$(bindings), ::HIR::Pattern::Data::make_Slice({mv$(leading)})};
        }
        TU_ARMA(SplitSlice, e) {
            ::std::vector<::HIR::Pattern> leading;
            for (const auto& sp : e.leading) {
                leading.push_back(LowerHIRPattern(sp));
            }

            ::std::vector<::HIR::Pattern> trailing;
            for (const auto& sp : e.trailing) {
                trailing.push_back(LowerHIRPattern(sp));
            }

            auto extraBind = e.extraBind.isValid() ? ::HIR::PatternBinding(false, convertBindingType(e.extraBind.mType), e.extraBind.mName.name, e.extraBind.slot) : ::HIR::PatternBinding();

            return ::HIR::Pattern{mv$(bindings), ::HIR::Pattern::Data::make_SplitSlice({mv$(leading), mv$(extraBind), mv$(trailing)})};
        }
        TU_ARMA(Or, e) {
            ::std::vector<::HIR::Pattern> subpats;
            for (const auto& sp : e) {
                subpats.push_back(LowerHIRPattern(sp));
            }
            return ::HIR::Pattern{mv$(bindings), ::HIR::Pattern::Data::make_Or(mv$(subpats))};
        }
    }
    throw "unreachable";
}

::HIR::ExprPtr LowerHIRExpr(const ::std::shared_ptr<ASTExprNode>& e) {
    if (e.get()) {
        return LowerHIRExprNode(*e);
    } else {
        return ::HIR::ExprPtr();
    }
}

::HIR::ExprPtr LowerHIRExpr(const ASTExpr& e) {
    if (e.isValid()) {
        return LowerHIRExprNode(e.node());
    } else {
        return ::HIR::ExprPtr();
    }
}

::HIR::SimplePath LowerHIRSimplePath(const Span& sp, const ASTPath& path, FromASTPathClass pc, bool allowFinalGeneric) {
    if (!allowFinalGeneric) {
        ASSERT_BUG(sp, path.cls.is_Absolute(), "Encountered non-Absolute path when creating ::HIR::SimplePath");
        if (path.cls.as_Absolute().nodes.size() > 0) {
            ASSERT_BUG(sp, path.cls.as_Absolute().nodes.back().args().isEmpty(), "Encountered path with parameters when creating ::HIR::SimplePath");
        }
    } else {
        ASSERT_BUG(sp, path.cls.is_Absolute(), "Encountered non-Absolute path when creating ::HIR::GenericPath");
    }

    const ASTAbsolutePath* ap = nullptr;
    switch (pc) {
        case FromASTPathClass::Value:
            ASSERT_BUG(sp, !path.mBindings.value.is_Unbound(), "Encountered unbound value path - " << path);
            ap = &path.mBindings.value.path;
            break;
        case FromASTPathClass::Type:
            ASSERT_BUG(sp, !path.mBindings.type.is_Unbound(), "Encountered unbound type path - " << path);
            ap = &path.mBindings.type.path;
            break;
        case FromASTPathClass::Macro:
            ASSERT_BUG(sp, !path.mBindings.macro.is_Unbound(), "Encountered unbound macro path - " << path);
            ap = &path.mBindings.macro.path;
            break;
    }
    assert(ap);
    return ::HIR::SimplePath((ap->crate == "" ? gCrateName : ap->crate), ap->nodes);
}

::HIR::PathParams LowerHIRPathParams(const Span& sp, const ASTPathParams& srcParams, bool allowAssoc) {
    ::HIR::PathParams params;

    size_t numLft = 0;
    size_t numTy = 0;
    size_t numVal = 0;

    for (const auto& param : srcParams.entries) {
        TU_MATCH_HDRA( (param), {)
        TU_ARMA(Null, ty) {
            }
            TU_ARMA(Lifetime, lft) {
                numLft++;
            }
            TU_ARMA(Type, ty) {
                numTy++;
            }
            TU_ARMA(Value, iv) {
                numVal++;
            }
            TU_ARMA(AssociatedTyEqual, ty) {
            }
            TU_ARMA(AssociatedTyBound, ty) {
            }
        }
    }

    params.mLifetimes.reserveInit(numLft);
    params.types.reserveInit(numTy);
    params.values.reserveInit(numVal);
    for (const auto& param : srcParams.entries) {
        TU_MATCH_HDRA( (param), {)
        TU_ARMA(Null, ty) {
            }
            TU_ARMA(Lifetime, lft) {
                params.mLifetimes.push_back(LowerHIRLifetimeRef(lft));
            }
            TU_ARMA(Type, ty) {
                params.types.push_back(LowerHIRType(ty));
            }
            TU_ARMA(Value, iv) {
                ASSERT_BUG(sp, iv, "Value parameter with null node");
                params.values.push_back(LowerHIRConstGeneric(*iv));
            }
            TU_ARMA(AssociatedTyEqual, ty) {
                if (!allowAssoc) {
                    BUG(sp, "Encountered path parameters with associated type bounds where they are not allowed");
                }
            }
            TU_ARMA(AssociatedTyBound, ty) {
                if (!allowAssoc) {
                    BUG(sp, "Encountered path parameters with associated type bounds where they are not allowed");
                }
            }
        }
    }

    return params;
}

::HIR::ConstGeneric LowerHIRConstGeneric(const ASTExprNode& nodeRef) {
    const Span& sp = nodeRef.span();
    const ASTExprNode* nodeP = &nodeRef;
    if (const auto* e = cast<const ASTExprNodeBlock>(nodeP)) {
        if (e->nodes.size() == 1 && !e->nodes.back().hasSemicolon) {
            nodeP = e->nodes.back().node.get();
        }
    }
    // TODO: Explicitly handle each expected variant... or add a proper consteval expression
    if (const auto* e = cast<const ASTExprNodeNamedValue>(nodeP)) {
        if (e->mPath.isTrivial()) {
            const auto& b = e->mPath.mBindings.value.binding;
            ASSERT_BUG(sp, b.is_Generic(), "Trivial path not type parameter - " << e->mPath << " - " << b.tagStr());
            const auto& param = b.as_Generic();
            return HIR::GenericRef(e->mPath.asTrivial(), param.index);
        }
    }
    return std::make_unique<HIR::ConstGenericUnevaluated>(LowerHIRExprNode(nodeRef));
}

::HIR::GenericPath LowerHIRGenericPath(const Span& sp, const ASTPath& path, FromASTPathClass pc, bool allowAssoc) {
    if (const auto* e = path.cls.opt_Absolute()) {
        auto simpepath = LowerHIRSimplePath(sp, path, pc, /*allow_params*/ true);
        ::HIR::PathParams params = LowerHIRPathParams(sp, e->nodes.back().args(), allowAssoc);
        auto rv = ::HIR::GenericPath(mv$(simpepath), mv$(params));
        DEBUG(path << " => " << rv);
        return rv;
    } else {
        if (const auto* e = path.cls.opt_UFCS()) {
            DEBUG(path);
            if (!e->type) {
            }
            //else if( e->trait ) {
            //}
            else if (!e->nodes.empty()) {
            } else if (!e->type->mData.is_Path()) {
            } else {
                // HACK: `Self` replacement
                ASSERT_BUG(sp, pc == FromASTPathClass::Type, "`Self` used in value context");
                return LowerHIRGenericPath(sp, *e->type->mData.as_Path(), pc, false);
            }
        }

        BUG(sp, "Encountered non-Absolute path when creating ::HIR::GenericPath - " << path);
    }
}

::HIR::GenericParams LowerHIRHigherRankedBounds(const ASTHigherRankedBounds& hrbs) {
    HIR::GenericParams params;
    for (const auto& lftDef : hrbs.mLifetimes) {
        params.mLifetimes.push_back(HIR::LifetimeDef{lftDef.name().name});
    }
    return params;
}

::HIR::TraitPath LowerHIRTraitPath(const Span& sp, const ASTPath& path, const ASTHigherRankedBounds& hrbs, bool ignoreBounds /*=false*/, ASTBoundConstness constness /*=Never*/) {
    DEBUG(hrbs << " " << path);
    ::HIR::TraitPath rv{
        hrbs.empty() ? nullptr : box$(LowerHIRHigherRankedBounds(hrbs)), // m_hrtbs
        LowerHIRGenericPath(sp, path, FromASTPathClass::Type, /*allow_assoc=*/true),
        {},
        {},
        nullptr,
        LowerHIRBoundConstness(constness)
    };
    // Parenthesised Fn-trait syntax follows function lifetime-elision rules.
    if (!rv.hrtbs && path.nodes().back().args().isParen) {
        HIR::GenericParams params;
        rv.hrtbs = box$(params);
    }
    if (rv.hrtbs && path.nodes().back().args().isParen) {
        rv.lifetimeElision = true;
    }

    if (rv.hrtbs) {
        DEBUG("HRLS = " << rv.hrtbs->fmtArgs());
    } else {
        DEBUG("No HRLS");
    }

    struct H {
        static ::HIR::GenericPath findSourceTraitHir(const Span& sp, const ::HIR::GenericPath& path, const HIR::Trait& trait, const RcString& name, const Monomorphiser& ms) {
            auto it = trait.types.find(name);
            if (it != trait.types.end()) {
                return ms.monomorphGenericpath(sp, path, /*allow_infer=*/false);
            }
            auto selfTy = gCratePtr->types.self();
            auto cb = MonomorphStatePtr(gCratePtr->types, selfTy, &path.mParams, nullptr);
            for (const auto& st : trait.allParentTraits) {
                // NOTE: st.m_trait_ptr isn't populated yet
                const auto& t = gCratePtr->getTraitByPath(sp, st.mPath.mPath);

                auto it = t.types.find(name);
                if (it != t.types.end()) {
                    // Monomorphse into outer scope, then run the outer monomorph
                    auto p = cb.monomorphGenericpath(sp, st.mPath, /*allow_infer=*/false);
                    return ms.monomorphGenericpath(sp, p, /*allow_infer=*/false);
                }
            }
            return ::HIR::GenericPath();
        }

        static ::HIR::GenericPath findSourceTraitAst(const Span& sp, const ::HIR::GenericPath& path, const ASTTrait& trait, const RcString& name, const Monomorphiser& ms) {
            for (const auto& i : trait.items()) {
                if (i.data.is_Type() && i.name == name) {
                    // Return current path.
                    return ms.monomorphGenericpath(sp, path, /*allow_infer=*/false);
                }
            }

            auto selfTy = gCratePtr->types.self();
            auto cb = MonomorphStatePtr(gCratePtr->types, selfTy, &path.mParams, nullptr);
            for (const auto& st : trait.supertraits()) {
                auto b = LowerHIRTraitPath(sp, *st.ent.path, st.ent.hrbs, true, st.ent.constness);
                ASSERT_BUG(sp, st.ent.path->mBindings.type.binding.is_Trait(), "Not a trait: " << *st.ent.path);
                auto rv = H::findSourceTrait(sp, b.mPath, st.ent.path->mBindings.type.binding.as_Trait(), name, cb);
                if (rv != HIR::GenericPath()) {
                    return ms.monomorphGenericpath(sp, rv, /*allow_infer=*/false);
                }
            }
            return ::HIR::GenericPath();
        }

        static ::HIR::GenericPath findSourceTrait(const Span& sp, const ::HIR::GenericPath& path, const ASTPathBindingType& pb, const RcString& name, const Monomorphiser& ms) {
            TRACE_FUNCTION_F(path);
            if (pb.is_Trait()) {
                const auto& pbe = pb.as_Trait();
                if (pbe.hir) {
                    assert(pbe.hir);
                    return findSourceTraitHir(sp, path, *pbe.hir, name, ms);
                } else if (pbe.trait_) {
                    assert(pbe.trait_);
                    return findSourceTraitAst(sp, path, *pbe.trait_, name, ms);
                } else {
                    BUG(sp, "Unbound path");
                }
            } else if (pb.is_TraitAlias()) {
                const auto& pbe = pb.as_TraitAlias();
                if (pbe.hir) {
                    for (const auto& subTrait : pbe.hir->traits) {
                        auto p = ms.monomorphGenericpath(sp, subTrait.mPath);
                        const auto& t = gCratePtr->getTraitByPath(sp, p.mPath);
                        auto selfTy = gCratePtr->types.self();
                        auto rv = findSourceTraitHir(sp, p, t, name, MonomorphStatePtr(gCratePtr->types, selfTy, &p.mParams, nullptr));
                        if (rv != HIR::GenericPath()) {
                            return rv;
                        }
                    }
                    return HIR::GenericPath();
                } else if (pbe.trait_) {
                    auto selfTy = gCratePtr->types.self();
                    auto cb = MonomorphStatePtr(gCratePtr->types, selfTy, &path.mParams, nullptr);
                    for (const auto& st : pbe.trait_->traits) {
                        auto b = LowerHIRTraitPath(sp, *st.ent.path, st.ent.hrbs, true, st.ent.constness);
                        auto rv = H::findSourceTrait(sp, b.mPath, st.ent.path->mBindings.type.binding, name, cb);
                        if (rv != HIR::GenericPath()) {
                            return ms.monomorphGenericpath(sp, rv, /*allow_infer=*/false);
                        }
                    }
                    return HIR::GenericPath();
                } else {
                    BUG(sp, "Unbound path");
                }
            } else {
                BUG(sp, "Not a trait: " << path << " : " << pb.tagStr());
            }
        }

        static std::pair<RcString, HIR::PathParams> getAtyNode(const Span& sp, const ASTPathNode& pn) {
            auto args = LowerHIRPathParams(sp, pn.args(), false);
            if (args.hasParams()) {
                TODO(sp, "Handle ATYs with args");
            }
            return std::make_pair(pn.name(), std::move(args));
        }
    };

    for (const auto& e : path.nodes().back().args().entries) {
        ThinVector<HIR::LifetimeRef> lfts;
        TU_MATCH_HDRA( (e), {)
        TU_ARMA(Null, _) {
            }
            TU_ARMA(Lifetime, _) {
            }
            TU_ARMA(Type, _) {
            }
            TU_ARMA(Value, _) {
            }
            TU_ARMA(AssociatedTyEqual, assoc) {
                auto nameArgs = H::getAtyNode(sp, assoc.first);
                auto srcTrait = H::findSourceTrait(sp, rv.mPath, path.mBindings.type.binding, nameArgs.first, MonomorphiserNop(gCratePtr->types));
                DEBUG("src_trait = " << srcTrait << " for " << assoc.first);
                rv.typeBounds.insert(::std::make_pair(nameArgs.first, ::HIR::TraitPath::AtyEqual{std::move(srcTrait), std::move(nameArgs.second), LowerHIRType(assoc.second)}));
            }
            TU_ARMA(AssociatedTyBound, assoc) {
                if (!ignoreBounds) {
                    ERROR(sp, E0000, "Associated type trait bounds not allowed here - " << path);
                } else {
                    auto nameArgs = H::getAtyNode(sp, assoc.first);
                    auto srcTrait = H::findSourceTrait(sp, rv.mPath, path.mBindings.type.binding, nameArgs.first, MonomorphiserNop(gCratePtr->types));
                    DEBUG("src_trait = " << srcTrait << " for " << assoc.first);
                    //if(src_trait == ::HIR::GenericPath())
                    //    ERROR(sp, E0000, "Unable to find source trait for " << b->first << " in " << bound_trait_path.m_path);
                    auto it = rv.traitBounds.insert(std::make_pair(nameArgs.first, ::HIR::TraitPath::AtyBound{std::move(srcTrait), std::move(nameArgs.second), {}}));
                    for (const auto& trait : assoc.second) {
                        it.first->second.traits.push_back(LowerHIRTraitPath(sp, trait, {}, /*ignore_bounds*/ false));
                    }
                }
            }
        }
    }

    return rv;
}

::HIR::Path LowerHIRPath(const Span& sp, const ASTPath& path, FromASTPathClass pc) {
    TU_MATCH_HDRA( (path.cls), {)
    TU_ARMA(Invalid, e) {
            BUG(sp, "BUG: Encountered Invalid path in LowerHIR_Path");
        }
        TU_ARMA(Local, e) {
            TODO(sp, "What to do with Path::Class::Local in LowerHIR_Path - " << path);
        }
        TU_ARMA(Relative, e) {
            BUG(sp, "Encountered `Relative` path in LowerHIR_Path - " << path);
        }
        TU_ARMA(Self, e) {
            BUG(sp, "Encountered `Self` path in LowerHIR_Path - " << path);
        }
        TU_ARMA(Super, e) {
            BUG(sp, "Encountered `Super` path in LowerHIR_Path - " << path);
        }
        TU_ARMA(Absolute, e) {
            return ::HIR::Path(LowerHIRGenericPath(sp, path, pc));
        }
        TU_ARMA(UFCS, e) {
            if (e.nodes.size() == 0) {
                if (!(!e.trait || e.trait->isValid())) {
                    TODO(sp, "Handle UFCS w/ trait and no nodes - " << path);
                }
                auto type = LowerHIRType(*e.type);
                ASSERT_BUG(sp, type->is_Path(), "No nodes and non-Path type - " << path);
                return type->as_Path().path.clone();
            }
            if (e.nodes.size() > 1) {
                TODO(sp, "Handle UFCS with multiple nodes - " << path);
            }
            // - No associated type bounds allowed in UFCS paths
            auto params = LowerHIRPathParams(sp, e.nodes.front().args(), /*allow_assoc*/ false);
            /*if( ! e.trait )
        {
            auto type = LowerHIR_Type(*e.type);
            if( type->is_Generic() ) {
                BUG(sp, "Generics can't be used with UfcsInherent - " << path);
            }
            return ::HIR::Path(::HIR::Path::Data::make_UfcsInherent({
                mv$(type),
                e.nodes[0].name(),
                mv$(params)
                }));
        }
        else*/
            if (!e.trait || !e.trait->isValid()) {
                return ::HIR::Path(::HIR::Path::Data::make_UfcsUnknown({LowerHIRType(*e.type), e.nodes[0].name(), mv$(params)}));
            } else {
                return ::HIR::Path(::HIR::Path::Data::make_UfcsKnown({LowerHIRType(*e.type), LowerHIRGenericPath(sp, *e.trait, FromASTPathClass::Type), e.nodes[0].name(), mv$(params)}));
            }
        }
    }
    throw "BUGCHECK: Reached end of LowerHIR_Path";
}

namespace {
    struct ImplTraitSource {
        const ::HIR::ItemPath* path;
        const ::HIR::GenericParams* paramsOuter;
        const ::HIR::GenericParams* paramsInner = nullptr;

        ImplTraitSource(const ::HIR::ItemPath* path, const ::HIR::GenericParams* paramsOuter, const ::HIR::GenericParams* paramsInner = nullptr)
            : path(path)
            , paramsOuter(paramsOuter)
            , paramsInner(paramsInner)
        {
        }

        ImplTraitSource()
            : path(nullptr)
            , paramsOuter(nullptr)
        {
        }
    } gImplTraitSource;

    class TraitObjectLowering {
        const Span& mSpan;
        ::HIR::TypeData::Data_TraitObject& out;
        ::std::unordered_set<const void*> activeAliases;
        ::std::vector<::HIR::LifetimeDef> activeHrtbs;

        ::HIR::TraitPath rebaseBoundHrtbs(::HIR::TraitPath trait) const {
            if (activeHrtbs.empty() || !trait.hrtbs) {
                return trait;
            }

            ::HIR::PathParams shifted;
            shifted.mLifetimes.reserve(trait.hrtbs->mLifetimes.size());
            for (size_t i = 0; i < trait.hrtbs->mLifetimes.size(); i++) {
                const auto binding = ::HIR::GenericRef(
                    RcString(),
                    ::HIR::GENERICHrtb,
                    static_cast<uint16_t>(activeHrtbs.size() + i)).binding;
                shifted.mLifetimes.push_back(::HIR::LifetimeRef(binding));
            }
            return MonomorphHrlsOnly(gCratePtr->types, shifted).monomorphTraitpath(mSpan, trait, false, true);
        }

        void attachActiveHrtbs(::HIR::TraitPath& trait) const {
            if (activeHrtbs.empty()) {
                return;
            }

            ::HIR::GenericParams merged;
            merged.mLifetimes.reserve(activeHrtbs.size()
                + (trait.hrtbs ? trait.hrtbs->mLifetimes.size() : 0));
            for (const auto& lifetime : activeHrtbs) {
                merged.mLifetimes.push_back(lifetime);
            }
            if (trait.hrtbs) {
                ASSERT_BUG(mSpan,
                    trait.hrtbs->types.empty() && trait.hrtbs->values.empty() && trait.hrtbs->bounds.empty(),
                    "Non-lifetime parameters in higher-ranked trait bound");
                for (const auto& lifetime : trait.hrtbs->mLifetimes) {
                    merged.mLifetimes.push_back(lifetime);
                }
            }
            trait.hrtbs = box$(mv$(merged));
        }

        bool hasPrincipal() const {
            return !out.mTrait.mPath.mPath.components().empty();
        }

        void addTrait(::HIR::TraitPath trait, bool isMarker) {
            if (isMarker) {
                if (!trait.typeBounds.empty() || !trait.traitBounds.empty()) {
                    ERROR(mSpan, E0000, "Associated type bounds on auto trait " << trait.mPath);
                }
                out.markers.push_back(mv$(trait.mPath));
                return;
            }

            attachActiveHrtbs(trait);
            if (hasPrincipal()) {
                ERROR(mSpan, E0000, "Multiple data traits in trait object: "
                    << out.mTrait.mPath << " and " << trait.mPath);
            }
            out.mTrait = mv$(trait);
        }

        void applyAliasBounds(::HIR::TraitPath& aliasPath, bool hadPrincipal) {
            const bool addedPrincipal = !hadPrincipal && hasPrincipal();
            if ((!aliasPath.typeBounds.empty() || !aliasPath.traitBounds.empty()) && !addedPrincipal) {
                ERROR(mSpan, E0000, "Associated type bounds on trait alias without a data trait: " << aliasPath.mPath);
            }
            if (addedPrincipal) {
                for (auto& bound : aliasPath.typeBounds) {
                    out.mTrait.typeBounds.insert(::std::make_pair(bound.first, mv$(bound.second)));
                }
                for (auto& bound : aliasPath.traitBounds) {
                    out.mTrait.traitBounds.insert(::std::make_pair(bound.first, mv$(bound.second)));
                }
            }
        }

        struct ActiveAlias {
            ::std::unordered_set<const void*>& aliases;
            const void* key;

            ~ActiveAlias() {
                aliases.erase(key);
            }
        };

        ActiveAlias enterAlias(const void* key, const ::HIR::GenericPath& path) {
            if (!activeAliases.insert(key).second) {
                ERROR(mSpan, E0000, "Recursive trait alias in trait object: " << path);
            }
            return ActiveAlias{activeAliases, key};
        }

        struct ActiveHrtbs {
            ::std::vector<::HIR::LifetimeDef>& lifetimes;
            size_t oldSize;

            ~ActiveHrtbs() {
                lifetimes.resize(oldSize);
            }
        };

        ActiveHrtbs enterHrtbs(::HIR::TraitPath& path) {
            const size_t oldSize = activeHrtbs.size();
            if (path.hrtbs) {
                ASSERT_BUG(mSpan,
                    path.hrtbs->types.empty() && path.hrtbs->values.empty() && path.hrtbs->bounds.empty(),
                    "Non-lifetime parameters in higher-ranked trait alias");
                activeHrtbs.reserve(oldSize + path.hrtbs->mLifetimes.size());
                for (const auto& lifetime : path.hrtbs->mLifetimes) {
                    activeHrtbs.push_back(lifetime);
                }
                path.hrtbs.reset();
            }
            return ActiveHrtbs{activeHrtbs, oldSize};
        }

        void addAstPath(::HIR::TraitPath path, const ASTPathBindingType& binding) {
            if (const auto* trait = binding.opt_Trait()) {
                ASSERT_BUG(mSpan, trait->trait_ || trait->hir, "Null trait binding for " << path.mPath);
                addTrait(mv$(path), trait->trait_ ? trait->trait_->isMarker() : trait->hir->mIsMarker);
            } else if (const auto* alias = binding.opt_TraitAlias()) {
                expandAstAlias(mv$(path), *alias);
            } else {
                BUG(mSpan, "Not a trait or trait alias: " << path.mPath << " (" << binding.tagStr() << ")");
            }
        }

        void addHirPath(::HIR::TraitPath path) {
            const auto& item = gCratePtr->getTypeitemByPath(mSpan, path.mPath.mPath);
            if (const auto* trait = item.opt_Trait()) {
                addTrait(mv$(path), trait->mIsMarker);
            } else if (const auto* alias = item.opt_TraitAlias()) {
                expandHirAlias(mv$(path), *alias);
            } else {
                BUG(mSpan, "Trait alias expanded to non-trait path " << path.mPath << " (" << item.tagStr() << ")");
            }
        }

        void expandAstAlias(::HIR::TraitPath aliasPath, const ASTPathBindingType::Data_TraitAlias& binding) {
            const void* key = binding.trait_ ? static_cast<const void*>(binding.trait_) : static_cast<const void*>(binding.hir);
            ASSERT_BUG(mSpan, key, "Null trait alias binding for " << aliasPath.mPath);
            auto active = enterAlias(key, aliasPath.mPath);
            auto activeHrtbs = enterHrtbs(aliasPath);
            const bool hadPrincipal = hasPrincipal();

            if (binding.trait_) {
                bool traitRequiresSized = false;
                auto paramsDef = LowerHIRGenericParams(binding.trait_->params, &traitRequiresSized);
                auto params = ConvertHIRCompleteAliasParams(gCratePtr->types, mSpan, paramsDef, aliasPath.mPath, false);
                auto monomorph = MonomorphStatePtr(gCratePtr->types, nullptr, &params, nullptr);
                for (const auto& bound : binding.trait_->traits) {
                    auto trait = rebaseBoundHrtbs(LowerHIRTraitPath(bound.sp, *bound.ent.path, bound.ent.hrbs, false, bound.ent.constness));
                    addAstPath(
                        monomorph.monomorphTraitpath(mSpan, trait, false),
                        bound.ent.path->mBindings.type.binding);
                }
            } else {
                ASSERT_BUG(mSpan, binding.hir, "Null trait alias binding for " << aliasPath.mPath);
                expandHirAliasContents(aliasPath, *binding.hir);
            }

            applyAliasBounds(aliasPath, hadPrincipal);
        }

        void expandHirAliasContents(const ::HIR::TraitPath& aliasPath, const ::HIR::TraitAlias& alias) {
            auto params = ConvertHIRCompleteAliasParams(gCratePtr->types, mSpan, alias.mParams, aliasPath.mPath, false);
            auto monomorph = MonomorphStatePtr(gCratePtr->types, nullptr, &params, nullptr);
            for (const auto& bound : alias.traits) {
                auto trait = rebaseBoundHrtbs(bound.clone());
                addHirPath(monomorph.monomorphTraitpath(mSpan, trait, false));
            }
        }

        void expandHirAlias(::HIR::TraitPath aliasPath, const ::HIR::TraitAlias& alias) {
            auto active = enterAlias(&alias, aliasPath.mPath);
            auto activeHrtbs = enterHrtbs(aliasPath);
            const bool hadPrincipal = hasPrincipal();
            expandHirAliasContents(aliasPath, alias);
            applyAliasBounds(aliasPath, hadPrincipal);
        }

    public:
        TraitObjectLowering(const Span& span, ::HIR::TypeData::Data_TraitObject& out)
            : mSpan(span)
            , out(out)
        {
        }

        void add(const ::TypeTraitPath& bound) {
            auto path = LowerHIRTraitPath(mSpan, *bound.path, bound.hrbs, false, bound.constness);
            addAstPath(mv$(path), bound.path->mBindings.type.binding);
        }
    };
}

::HIR::TypeRef LowerHIRType(const ::TypeRef& ty) {
    TU_MATCH_HDRA( (ty.mData), {)
    TU_ARMA(None, e) {
            BUG(ty.span(), "TypeData::None");
        }
        TU_ARMA(Bang, e) {
            return gCratePtr->types.diverge();
        }
        TU_ARMA(Any, e) {
            return gCratePtr->types.infer();
        }
        TU_ARMA(Unit, e) {
            return gCratePtr->types.unit();
        }
        TU_ARMA(Macro, e) {
            BUG(ty.span(), "TypeData::Macro");
        }
        TU_ARMA(Primitive, e) {
            switch (e.coreType) {
                case CORETYPE_BOOL:
                    return gCratePtr->types.primitive(::HIR::CoreType::Bool);
                case CORETYPE_CHAR:
                    return gCratePtr->types.primitive(::HIR::CoreType::Char);
                case CORETYPE_STR:
                    return gCratePtr->types.primitive(::HIR::CoreType::Str);
                case CORETYPE_F16:
                    return gCratePtr->types.primitive(::HIR::CoreType::F16);
                case CORETYPE_F32:
                    return gCratePtr->types.primitive(::HIR::CoreType::F32);
                case CORETYPE_F64:
                    return gCratePtr->types.primitive(::HIR::CoreType::F64);
                case CORETYPE_F128:
                    return gCratePtr->types.primitive(::HIR::CoreType::F128);

                case CORETYPE_I8:
                    return gCratePtr->types.primitive(::HIR::CoreType::I8);
                case CORETYPE_U8:
                    return gCratePtr->types.primitive(::HIR::CoreType::U8);
                case CORETYPE_I16:
                    return gCratePtr->types.primitive(::HIR::CoreType::I16);
                case CORETYPE_U16:
                    return gCratePtr->types.primitive(::HIR::CoreType::U16);
                case CORETYPE_I32:
                    return gCratePtr->types.primitive(::HIR::CoreType::I32);
                case CORETYPE_U32:
                    return gCratePtr->types.primitive(::HIR::CoreType::U32);
                case CORETYPE_I64:
                    return gCratePtr->types.primitive(::HIR::CoreType::I64);
                case CORETYPE_U64:
                    return gCratePtr->types.primitive(::HIR::CoreType::U64);

                case CORETYPE_I128:
                    return gCratePtr->types.primitive(::HIR::CoreType::I128);
                case CORETYPE_U128:
                    return gCratePtr->types.primitive(::HIR::CoreType::U128);

                case CORETYPE_INT:
                    return gCratePtr->types.primitive(::HIR::CoreType::Isize);
                case CORETYPE_UINT:
                    return gCratePtr->types.primitive(::HIR::CoreType::Usize);
                case CORETYPE_ANY:
                    TODO(ty.span(), "TypeData::Primitive - CORETYPE_ANY");
                case CORETYPE_INVAL:
                    BUG(ty.span(), "TypeData::Primitive - CORETYPE_INVAL");
            }
        }
        TU_ARMA(Tuple, e) {
            ::HIR::TypeData::Data_Tuple v;
            for (const auto& st : e.innerTypes) {
                v.push_back(LowerHIRType(st));
            }
            return gCratePtr->types.tuple(mv$(v));
        }
        TU_ARMA(Borrow, e) {
            auto cl = (e.isMut ? ::HIR::BorrowType::Unique : ::HIR::BorrowType::Shared);
            return gCratePtr->types.borrow(cl, LowerHIRType(*e.inner), LowerHIRLifetimeRef(e.lifetime));
        }
        TU_ARMA(Pointer, e) {
            auto cl = (e.isMut ? ::HIR::BorrowType::Unique : ::HIR::BorrowType::Shared);
            return gCratePtr->types.pointer(cl, LowerHIRType(*e.inner));
        }
        TU_ARMA(Array, e) {
            auto inner = LowerHIRType(*e.inner);
            if (e.size) {
                // If the size expression is an unannotated or usize integer literal, don't bother converting the expression
                if (const auto* ptr = cast<const ASTExprNodeInteger>(&*e.size)) {
                    if (ptr->datatype == CORETYPE_UINT || ptr->datatype == CORETYPE_ANY) {
                        // TODO: Chage the HIR format to support very large arrays
                        if (ptr->mValue >= U128(UINT64_MAX)) {
                            ERROR(ty.span(), E0000, "Array size out of bounds - 0x" << ::std::hex << ptr->mValue << " > 0x" << UINT64_MAX << " in " << ::std::dec << ty);
                        }
                        return gCratePtr->types.array(inner, ptr->mValue.truncateU64());
                    }
                }
                if (const auto* ptr = cast<const ASTExprNodeNamedValue>(&*e.size)) {
                    if (ptr->mPath.isTrivial()) {
                        auto gr = HIR::GenericRef(ptr->mPath.asTrivial(), ptr->mPath.mBindings.value.binding.as_Generic().index);
                        return gCratePtr->types.array(inner, HIR::ConstGeneric(mv$(gr)));
                    }
                }

                return gCratePtr->types.array(inner, HIR::ConstGeneric::make_Unevaluated(std::make_unique<HIR::ConstGenericUnevaluated>(LowerHIRExpr(e.size))));
            } else {
                return gCratePtr->types.array(inner, HIR::ConstGeneric::make_Infer({}));
            }
        }
        TU_ARMA(Slice, e) {
            auto inner = LowerHIRType(*e.inner);
            return gCratePtr->types.slice(inner);
        }
        TU_ARMA(Path, e) {
            if (const auto* l = e->cls.opt_Local()) {
                unsigned int slot;
                // NOTE: TypeParameter is unused
                if (const auto* p = e->mBindings.type.binding.opt_TypeParameter()) {
                    slot = p->slot;
                } else {
                    BUG(ty.span(), "Unbound local encountered in " << *e);
                }
                return gCratePtr->types.generic(l->name, slot);
            } else if (e->mBindings.type.path.crate == CRATE_BUILTINS) {
                return LowerHIRType(TypeRef(ty.span(), coretypeFromstring(e->mBindings.type.path.nodes.back().c_str())));
            } else {
                return gCratePtr->types.path(LowerHIRPath(ty.span(), *e, FromASTPathClass::Type), {});
            }
        }
        TU_ARMA(TraitObject, e) {
            ::HIR::TypeData::Data_TraitObject v;
            if (e.lifetimes.empty()) {
                // Lifetime elision should have handled this?
            } else if (e.lifetimes.size() == 1) {
                v.lifetime = LowerHIRLifetimeRef(e.lifetimes[0]);
            } else {
                BUG(ty.span(), "Handle multiple lifetimes on a trait object - " << ty);
            }
            TraitObjectLowering lowering(ty.span(), v);
            for (const auto& t : e.traits) {
                DEBUG("t = " << *t.path);
                lowering.add(t);
            }
            // Sort markers so downstream can compare properly
            ::std::sort(v.markers.begin(), v.markers.end());
            v.markers.erase(::std::unique(v.markers.begin(), v.markers.end()), v.markers.end());
            return gCratePtr->types.intern(::HIR::TypeData::make_TraitObject(mv$(v)));
        }
        TU_ARMA(ErasedType, e) {
            ASSERT_BUG(ty.span(), e->traits.size() > 0, "ErasedType with no traits");

            // TODO: There can be associated type bounds, those need to be propagated

            ::std::vector<::HIR::TraitPath> traits;
            for (const auto& t : e->traits) {
                DEBUG("t = " << *t.path);
                // TODO: Handle ATY bounds
                traits.push_back(LowerHIRTraitPath(ty.span(), *t.path, t.hrbs, /*allow_aty_trait_bounds=*/true, t.constness));
            }
            bool isSized = true;
            for (const auto& t : e->maybeTraits) {
                auto tp = LowerHIRTraitPath(ty.span(), *t.path, t.hrbs, /*allow_aty_trait_bounds=*/true);
                if (tp.mPath.mPath == pathSized) {
                    isSized = false;
                } else {
                    TODO(ty.span(), "Optional trait (not Sized) - " << ty);
                }
            }
            std::vector<::HIR::LifetimeRef> lfts;
            for (const auto& lft : e->lifetimes) {
                lfts.push_back(LowerHIRLifetimeRef(lft));
            }
            ::HIR::TypeDataErasedTypeInner inner;
            if (gImplTraitSource.path) {
                if (gImplTraitSource.paramsInner && gImplTraitSource.paramsInner->isGeneric()) {
                    TODO(ty.span(), "Handle multi-layered generic erased type (used in a GAT)");
                }
                inner = ::HIR::TypeDataErasedTypeInner(::HIR::TypeDataErasedTypeInner::Data_Alias{gImplTraitSource.paramsOuter->makeNopParams(gCratePtr->types, 0), std::make_shared<HIR::TypeDataErasedTypeAliasInner>(*gImplTraitSource.path, *gImplTraitSource.paramsOuter)});
            } else {
                inner = ::HIR::TypeDataErasedTypeInner::Data_Fcn{::HIR::Path(::HIR::SimplePath()), 0}; // Populated in bind, could be populated now?
            }
            return gCratePtr->types.intern(::HIR::TypeData::make_ErasedType({isSized, mv$(traits), mv$(lfts), mv$(inner), e->use ? LowerHIRPathParams(ty.span(), *e->use, false) : HIR::PathParams(), e->use ? ::HIR::TypeDataErasedType::Use::Present : (e->isEdition2024OrLater ? ::HIR::TypeDataErasedType::Use::Omitted2024 : ::HIR::TypeDataErasedType::Use::OmittedOld)}));
        }
        TU_ARMA(Function, e) {
            HIR::GenericParams params;
            for (const auto& lftDef : e.info.hrbs.mLifetimes) {
                params.mLifetimes.push_back(HIR::LifetimeDef{lftDef.name().name});
            }
            ::std::vector<::HIR::TypeRef> args;
            for (const auto& arg : e.info.argTypes) {
                args.push_back(LowerHIRType(arg));
            }
            ::HIR::TypeDataFunctionPointer f{mv$(params), e.info.isUnsafe, e.info.isVariadic, RcString::newInterned(e.info.mAbi), LowerHIRType(*e.info.mRettype), mv$(args)};
            if (f.mAbi == "") {
                f.mAbi = RcString::newInterned(ABI_RUST);
            }
            return gCratePtr->types.function(mv$(f));
        }
        TU_ARMA(Generic, e) {
            assert(e.index < 0x10000);
            return gCratePtr->types.generic(e.name, e.index);
        }
    }
    throw "BUGCHECK: Reached end of LowerHIR_Type";
}

::HIR::TypeAlias LowerHIRTypeAlias(const HIR::ItemPath& p, const ASTTypeAlias& ta) {
    assert(!gImplTraitSource.path);
    auto params = LowerHIRGenericParams(ta.params(), nullptr);
    gImplTraitSource = ImplTraitSource(&p, &params);
    auto ty = LowerHIRType(ta.type());
    //if( auto* e = ty.data_mut().opt_ErasedType() ) {
    //    DEBUG("Flag type alias - " << &ty.data());
    //    e->m_inner = std::make_shared<HIR::TypeData_ErasedType_AliasInner>(p);
    //}
    gImplTraitSource = ImplTraitSource();
    return ::HIR::TypeAlias{std::move(params), ::std::move(ty)};
}

namespace {
    template <typename T>
    ::HIR::VisEnt<T> newVisent(HIR::Publicity pub, T v) {
        return ::HIR::VisEnt<T>{pub, mv$(v)};
    }

    ::HIR::SimplePath getParentModule(const ::HIR::ItemPath& p) {
        const ::HIR::ItemPath* parentIp = p.parent;
        assert(parentIp);
        while (parentIp->name && parentIp->name[0] == '#') {
            parentIp = parentIp->parent;
            assert(parentIp);
        }
        return parentIp->getSimplePath();
    }
}

::HIR::tStructFields LowerHIRStructFields(::HIR::ItemPath path, const ::HIR::GenericParams& params, const ::std::vector<ASTStructItem>& inFields, ::HIR::Module& outMod) {
    ::HIR::Struct::Data::Data_Named fields;
    for (const auto& field : inFields) {
        auto type = LowerHIRType(field.mType);
        ::std::unique_ptr<HIR::GenericPath> fieldDefault;
        if (field.defaultValue) {
            // NOTE: I'd love to have this be a `Constant`, but that would require duplicating the type and the params
            // meh. Lazy option is to just duplicate
            auto name = RcString::newInterned(FMT(path.getName() << "#default_" << field.mName));
            outMod.valueItems.insert(std::make_pair(name, ::std::make_unique<HIR::VisEnt<HIR::ValueItem>>(HIR::VisEnt<HIR::ValueItem>{HIR::Publicity::newGlobal(), HIR::ValueItem(HIR::Constant(params.clone(), type, LowerHIRExpr(field.defaultValue)))})));
            fieldDefault = std::make_unique<HIR::GenericPath>((*path.parent + name).getSimplePath(), params.makeNopParams(gCratePtr->types, 0));
        }
        fields.push_back(HIR::StructField{field.mName, LowerHIRVis(getParentModule(path), field.vis), std::move(type), std::move(fieldDefault)});
    }
    return fields;
}

::HIR::Struct LowerHIRStruct(const Span& sp, ::HIR::ItemPath path, const ASTStruct& ent, const ASTAttributeList& attrs, ::HIR::Module& outMod) {
    TRACE_FUNCTION_F(path);
    ::HIR::Struct::Data data;

    auto modPath = getParentModule(path);
    auto getVis = [&](const ASTVisibility& vis) {
        return LowerHIRVis(modPath, vis);
    };

    auto rv = ::HIR::Struct{LowerHIRGenericParams(ent.params(), nullptr), ::HIR::Struct::Repr::Rust, {}};

    TU_MATCH_HDRA( (ent.mData), {)
    TU_ARMA(Unit, e) {
            rv.mData = ::HIR::Struct::Data::make_Unit({});
        }
        TU_ARMA(Tuple, e) {
            ::HIR::Struct::Data::Data_Tuple fields;

            for (const auto& field : e.ents) {
                fields.push_back({getVis(field.vis), LowerHIRType(field.mType)});
            }

            rv.mData = ::HIR::Struct::Data::make_Tuple(mv$(fields));
        }
        TU_ARMA(Struct, e) {
            auto fields = LowerHIRStructFields(path, rv.mParams, e.ents, outMod);
            rv.mData = ::HIR::Struct::Data::make_Named(mv$(fields));
        }
    }

    // Determine the repr
    {
        switch (ent.markings.repr) {
            case ASTStruct::Markings::Repr::Rust:
                rv.repr = ::HIR::Struct::Repr::Rust;
                break;
            case ASTStruct::Markings::Repr::C:
                rv.repr = ::HIR::Struct::Repr::C;
                break;
            case ASTStruct::Markings::Repr::Simd:
                rv.repr = ::HIR::Struct::Repr::Simd;
                //ASSERT_BUG(sp, ent.m_markings.max_field_align == 0, "packed() on simd?");
                break;
            case ASTStruct::Markings::Repr::Transparent:
                rv.repr = ::HIR::Struct::Repr::Transparent;
                ASSERT_BUG(sp, ent.markings.maxFieldAlign == 0, "packed() on transparent?");
                break;
        }
        rv.forcedAlignment = ent.markings.alignValue;
        rv.maxFieldAlignment = ent.markings.maxFieldAlign;
    }

    // #[rustc_nonnull_optimization_guaranteed]
    // TODO: OR, it's equal to the `non_zero` lang item
    if(attrs.get("rustc_nonnull_optimization_guaranteed"))
    {
        //ent.m_markings.scalar_valid_start_set = true;
        // In 1.90 this no longer marks wrappers as nonzero; scalar limits carry
        // the layout information instead.
    }
    rv.structMarkings.isFundamental = attrs.has("fundamental");
    if(ent.markings.scalarValidStartSet)
    {
        if (ent.markings.scalarValidStart == U128(1)) {
            rv.structMarkings.isNonzero = true;
        } else {
            //TODO(sp, "Handle #[rustc_layout_scalar_valid_range_start(" << ent.m_markings.scalar_valid_start << ")]");
        }
    }
    // TODO: Store the scalar valid range information for downstream
    if( ent.markings.scalarValidStartSet || ent.markings.scalarValidEndSet )
    {
        const HIR::TypeData* ty = nullptr;
        const HIR::TypeData* ty2 = nullptr;
        if (const auto* d = rv.mData.opt_Named()) {
            switch (d->size()) {
                case 2:
                    ty2 = (*d)[1].ty;
                case 1:
                    ty = (*d)[0].ty;
                    break;
            }
        } else if (const auto* d = rv.mData.opt_Tuple()) {
            if (d->size() == 1) {
                ty = (*d)[0].ent;
            }
            //TODO: Ensure that the other fields are ZSTs
        } else {
            // Invalid
        }
        if (!ty) {
            ERROR(sp, E0000, "Invalid use of #[rustc_layout_scalar_valid_range_start] or #[rustc_layout_scalar_valid_range_end] on invalid struct");
        }
        if (ty2) {
            //TODO: Ensure that this second field is PhantomData
        }

        uint64_t TGT_PTR_MAX = TargetGetPointerBits() == 64 ? UINT64_MAX : UINT32_MAX;
        U128 min = U128(0), max = U128(UINT64_MAX, UINT64_MAX);
        bool ignore = false;
        if (ty->is_Pointer()) {
            min = U128(0);
            max = U128(TGT_PTR_MAX);
        } else {
            // Check the type
            ::HIR::CoreType ct = HIR::CoreType::Str;
            if (ty->is_Primitive()) {
                ct = ty->as_Primitive();
            }
            switch (ct) {
                case ::HIR::CoreType::U8:
                    max = U128(0xFF);
                    break;
                case ::HIR::CoreType::U16:
                    max = U128(UINT16_MAX);
                    break;
                case ::HIR::CoreType::U32:
                    max = U128(UINT32_MAX);
                    break;
                case ::HIR::CoreType::U64:
                    max = U128(UINT64_MAX);
                    break;
                case ::HIR::CoreType::U128:
                    break;
                case ::HIR::CoreType::Usize:
                    max = U128(TGT_PTR_MAX);
                    break;

                case ::HIR::CoreType::I8:    //max = 0x7F;     break;
                case ::HIR::CoreType::I16:   //max = INT16_MAX;   break;
                case ::HIR::CoreType::I32:   //max = INT32_MAX; break;
                case ::HIR::CoreType::I64:   //max = INT64_MAX;   break;
                case ::HIR::CoreType::I128:  //ignore = true;  break;
                case ::HIR::CoreType::Isize: //max = TGT_PTR_MAX/2+1;   break;
                    // Downstream treats this as unsigned
                    ignore = true;
                    break;

                default:
                    ignore = true;
                    //ERROR(sp, E0000, "Invalid use of #[rustc_layout_scalar_valid_range_start] or #[rustc_layout_scalar_valid_range_end] on invalid type (must be an integer or pointer) - " << *ty);
                    break;
            }
        }

        if (!ignore) {
            if (ent.markings.scalarValidStartSet) {
                if (ent.markings.scalarValidStart < min) {
                }
                //rv.m_struct_markings.bounded_min = true;
                //rv.m_struct_markings.bounded_min_value = ent.m_markings.scalar_valid_start;
            }
            if (ent.markings.scalarValidEndSet) {
                if (ent.markings.scalarValidEnd > max) {
                }
                rv.structMarkings.boundedMax = true;
                rv.structMarkings.boundedMaxValue = ent.markings.scalarValidEnd;
            }
        }
    }

    return rv;
}

::HIR::Enum LowerHIREnum(::HIR::ItemPath path, const ASTEnum& ent, const ASTAttributeList& attrs, ::std::function<void(RcString, ::HIR::Struct)> pushStruct, HIR::Module& outMod) {
    // 1. Figure out what sort of enum this is (value or data)
    bool hasData = false;
    for (const auto& var : ent.variants()) {
        if (var.mData.is_Tuple() || var.mData.is_Struct()) {
            hasData = true;
        } else {
            // Unit-like
            assert(var.mData.is_Unit());
        }
    }
    if (ent.markings.alignValue != 0) {
        hasData = true;
    }

    bool isReprC = ent.markings.isReprC;
    auto repr = ::HIR::Enum::Repr::Auto;
    switch (ent.markings.repr) {
        case ASTEnum::Markings::Repr::Rust:
            repr = ::HIR::Enum::Repr::Auto;
            break;
        case ASTEnum::Markings::Repr::U8:
            repr = ::HIR::Enum::Repr::U8;
            break;
        case ASTEnum::Markings::Repr::U16:
            repr = ::HIR::Enum::Repr::U16;
            break;
        case ASTEnum::Markings::Repr::U32:
            repr = ::HIR::Enum::Repr::U32;
            break;
        case ASTEnum::Markings::Repr::U64:
            repr = ::HIR::Enum::Repr::U64;
            break;
        case ASTEnum::Markings::Repr::U128:
            repr = ::HIR::Enum::Repr::U128;
            break;
        case ASTEnum::Markings::Repr::Usize:
            repr = ::HIR::Enum::Repr::Usize;
            break;
        case ASTEnum::Markings::Repr::I8:
            repr = ::HIR::Enum::Repr::I8;
            break;
        case ASTEnum::Markings::Repr::I16:
            repr = ::HIR::Enum::Repr::I16;
            break;
        case ASTEnum::Markings::Repr::I32:
            repr = ::HIR::Enum::Repr::I32;
            break;
        case ASTEnum::Markings::Repr::I64:
            repr = ::HIR::Enum::Repr::I64;
            break;
        case ASTEnum::Markings::Repr::I128:
            repr = ::HIR::Enum::Repr::I128;
            break;
        case ASTEnum::Markings::Repr::Isize:
            repr = ::HIR::Enum::Repr::Isize;
            break;
    }

    auto params = LowerHIRGenericParams(ent.params(), nullptr);

    ::HIR::Enum::Class data;
    if (ent.variants().size() > 0 && !hasData) {
        ::std::vector<::HIR::Enum::ValueVariant> variants;
        for (const auto& var : ent.variants()) {
            // TODO: Quick consteval on the expression?
            variants.push_back({var.mName, LowerHIRExpr(var.discriminantValue), U128(0)});
        }

        data = ::HIR::Enum::Class::make_Value({mv$(variants)});
    }
    // NOTE: empty enums are encoded as empty Data enums
    else {
        ::std::vector<::HIR::Enum::DataVariant> variants;
        const auto variantRepr = isReprC || repr != ::HIR::Enum::Repr::Auto ? ::HIR::Struct::Repr::C : ::HIR::Struct::Repr::Rust;
        for (const auto& var : ent.variants()) {
            if (var.mData.is_Unit() && ent.markings.alignValue == 0) {
                // TODO: Should this make its own unit-like struct?
                variants.push_back({var.mName, false, gCratePtr->types.unit()});
            } else {
                ::HIR::Struct::Data data;
                if (var.mData.is_Unit()) {
                    data = ::HIR::Struct::Data::make_Unit({});
                } else if (const auto* ve = var.mData.opt_Tuple()) {
                    ::HIR::Struct::Data::Data_Tuple fields;
                    for (const auto& field : ve->mItems) {
                        fields.push_back(newVisent(::HIR::Publicity::newGlobal(), LowerHIRType(field.mType)));
                    }
                    data = ::HIR::Struct::Data::make_Tuple(mv$(fields));
                } else if (const auto* ve = var.mData.opt_Struct()) {
                    auto fields = LowerHIRStructFields(path, params, ve->fields, outMod);
                    data = ::HIR::Struct::Data::make_Named(mv$(fields));
                } else {
                    throw "";
                }

                auto tyName = RcString::newInterned(FMT(path.name << "#" << var.mName));
                auto variantStruct = ::HIR::Struct{LowerHIRGenericParams(ent.params(), nullptr), variantRepr, mv$(data)};
                variantStruct.forcedAlignment = ent.markings.alignValue;
                pushStruct(tyName, mv$(variantStruct));
                auto tyIpath = path;
                tyIpath.name = tyName.c_str();
                auto tyPath = tyIpath.getFullPath();
                // Add type params
                tyPath.mData.as_Generic().mParams = params.makeNopParams(gCratePtr->types, 0);
                variants.push_back({var.mName, var.mData.is_Struct(), gCratePtr->types.path(mv$(tyPath), {})});
            }

            if (var.discriminantValue) {
                if (repr == ::HIR::Enum::Repr::Auto) {
                    ERROR(var.discriminantValue.node().span(), E0000, "Discrimiant value set on enum with no `repr` set");
                }
                variants.back().discriminantExpr = LowerHIRExpr(var.discriminantValue);
            }
        }

        switch (repr) {
            case ::HIR::Enum::Repr::Auto:
                break;
            default:
                // NOTE:
                // - librustc_llvm has `#[repr(C)] enum AttributePlace { Argument(u32), Function }`
                // - `rustc-1.19.0-src\src\vendor\idna\src\uts46.rs:33` has `#[repr(u16)]`
                //ERROR(Span(), E0000, "#[repr] not allowed on enums with data");

                // NOTE: We save the repr for use in `trans/target.cpp`
                // https://github.com/rust-lang/rfcs/blob/master/text/2195-really-tagged-unions.md
                // - `repr(int)` packs the tag into the variants (which can be more efficient for alignment, with `Variant(u8, u16)`)
                // - `repr(C,int)` has the tag before variants (so will be less alignment efficient)
                break;
        }

        data = ::HIR::Enum::Class::make_Data(mv$(variants));
    }

    return ::HIR::Enum{mv$(params), isReprC, repr, mv$(data)};
}

::HIR::Union LowerHIRUnion(::HIR::ItemPath path, const ASTUnion& f, const ASTAttributeList& attrs) {
    auto modPath = getParentModule(path);
    auto getVis = [&](const ASTVisibility& vis) {
        return LowerHIRVis(modPath, vis);
    };

    auto repr = ::HIR::Union::Repr::Rust;
    switch (f.markings.repr) {
        case ASTUnion::Markings::Repr::Rust:
            repr = ::HIR::Union::Repr::Rust;
            break;
        case ASTUnion::Markings::Repr::C:
            repr = ::HIR::Union::Repr::C;
            break;
        case ASTUnion::Markings::Repr::Transparent:
            repr = ::HIR::Union::Repr::Transparent;
            break;
    }

    ::HIR::Struct::Data::Data_Named variants;
    for (const auto& field : f.mVariants) {
        variants.push_back(HIR::StructField{field.mName, getVis(field.vis), LowerHIRType(field.mType), {}});
    }

    return ::HIR::Union{LowerHIRGenericParams(f.mParams, nullptr), repr, mv$(variants)};
}

::HIR::Trait LowerHIRTrait(
    ::HIR::SimplePath traitPath,
    const ASTTrait& f,
    const ASTAttributeList& attrs
) {
    TRACE_FUNCTION_F(traitPath);
    traitPath.updateCrateName(gCrateName);

    bool traitReqiresSized = false;
    auto params = LowerHIRGenericParams(f.params(), &traitReqiresSized);

    ::HIR::LifetimeRef lifetime;
    if (!f.lifetimes().empty()) {
        ASSERT_BUG(f.lifetimes()[0].sp, f.lifetimes().size() == 1, "");
        lifetime = LowerHIRLifetimeRef(f.lifetimes()[0].ent);
        DEBUG("Lifetime " << lifetime << " (" << f.lifetimes()[0].ent << " " << f.lifetimes()[0].ent.binding() << ")");
    }
    ::std::vector<::HIR::TraitPath> supertraits;
    for (const auto& st : f.supertraits()) {
        supertraits.push_back(LowerHIRTraitPath(st.sp, *st.ent.path, st.ent.hrbs, true, st.ent.constness));
        DEBUG("Supertrait " << supertraits.back());
    }
    ::HIR::Trait rv{mv$(params), mv$(lifetime), mv$(supertraits)};
    rv.isConst = attrs.has("const_trait");

    // HACK: Add a bound of Self: ThisTrait for parts of typeck (TODO: Remove this, it's evil)
    {
        auto thisTrait = ::HIR::GenericPath(traitPath);
        thisTrait.mParams = rv.mParams.makeNopParams(gCratePtr->types, 0);
        rv.mParams.bounds.push_back(::HIR::GenericBound::make_TraitBound({{}, gCratePtr->types.self(), {{}, mv$(thisTrait)}}));
    }

    for (const auto& item : f.items()) {
        auto traitIp = ::HIR::ItemPath(traitPath);
        auto itemPath = ::HIR::ItemPath(traitIp, item.name.c_str());

        TU_MATCH_HDRA( (item.data), {)
        default:
            BUG(item.span, "Encountered unexpected item type in trait");
            TU_ARMA(None, i) {
                // Ignore.
            }
            TU_ARMA(MacroInv, i) {
                // Ignore.
            }
            TU_ARMA(Type, i) {
                bool isSized = true;
                ::std::vector<::HIR::TraitPath> traitBounds;
                ::HIR::LifetimeRef lifetimeBound;
                auto gps = LowerHIRGenericParams(i.params(), &isSized);

                auto selfBounds = LowerHIRGenericParams(i.selfBounds, &isSized);
                for (auto& b : selfBounds.bounds) {
                TU_MATCH_HDRA( (b), {)
                TU_ARMA(TypeLifetime, be) {
                            ASSERT_BUG(item.span, be.type->as_Generic().binding == GENERICSelf, be.type);
                            lifetimeBound = mv$(be.validFor);
                        }
                        TU_ARMA(TraitBound, be) {
                            ASSERT_BUG(item.span, be.type->as_Generic().binding == GENERICSelf, be.type);
                            traitBounds.push_back(mv$(be.trait));
                        }
                        TU_ARMA(Lifetime, be) {
                            BUG(item.span, "Unexpected lifetime-lifetime bound on associated type");
                        }
                        TU_ARMA(TypeEquality, be) {
                            BUG(item.span, "Unexpected type equality bound on associated type");
                        }
                }
                }
                rv.types.insert(::std::make_pair(item.name, ::HIR::AssociatedType{mv$(gps), isSized, mv$(lifetimeBound), mv$(traitBounds), LowerHIRType(i.type())}));
            }
            TU_ARMA(Function, i) {
                auto fcn = LowerHIRFunction(itemPath, item.attrs, i, gCratePtr->types.self());
                if (rv.isConst) {
                    fcn.isConst = true;
                }
                fcn.saveCode = true;
                rv.values.insert(::std::make_pair(item.name, ::HIR::TraitValueItem::make_Function(mv$(fcn))));
            }
            TU_ARMA(Static, i) {
                if (i.sClass() == ASTStatic::CONST) {
                    rv.values.insert(::std::make_pair(item.name, ::HIR::TraitValueItem::make_Constant(::HIR::Constant(::HIR::GenericParams{}, LowerHIRType(i.type()), LowerHIRExpr(i.value())))));
                } else {
                    ::HIR::Linkage linkage;
                    rv.values.insert(::std::make_pair(item.name, ::HIR::TraitValueItem::make_Static(::HIR::Static(mv$(linkage), (i.sClass() == ASTStatic::MUT), LowerHIRType(i.type()), LowerHIRExpr(i.value())))));
                }
            }
        }
    }

    rv.mIsMarker = f.isMarker();
    rv.isCoinductive = rv.mIsMarker || attrs.has("rustc_coinductive");
    rv.isFundamental = attrs.has("fundamental");

    return rv;
}

::HIR::TraitAlias LowerHIRTraitAlias(const Span& sp, ::HIR::ItemPath p, const ASTTraitAlias& f) {
    bool traitReqiresSized = false;

    HIR::TraitAlias ta;
    ta.mParams = LowerHIRGenericParams(f.params, &traitReqiresSized);
    for (const auto& t : f.traits) {
        ta.traits.push_back(LowerHIRTraitPath(t.sp, *t.ent.path, t.ent.hrbs, false, t.ent.constness));
    }

    return ta;
}

::HIR::Function LowerHIRFunction(::HIR::ItemPath p, const ASTAttributeList& attrs, const ASTFunction& f, const ::HIR::TypeData* realSelfType) {
    static Span sp;

    TRACE_FUNCTION_F(p);

    ::std::vector<::std::pair<::HIR::Pattern, ::HIR::TypeRef>> args;
    for (const auto& arg : f.args()) {
        args.push_back(::std::make_pair(LowerHIRPattern(arg.pat), LowerHIRType(arg.ty)));
    }

    auto receiver = ::HIR::Function::Receiver::Free;

    if (args.size() > 0 && args.front().first.mBindings.size() > 0 && args.front().first.mBindings[0].mName == "self") {
        const auto& sp = f.args()[0].pat.span();
        auto& argSelfTy = args.front().second;

        struct Ivcr {
            const Span& sp;
            const ::HIR::TypeData* realSelfType;

            Ivcr(const Span& sp, const ::HIR::TypeData* realSelfType)
                : sp(sp)
                , realSelfType(realSelfType)
            {
            }

            bool isValidCustomReceiver(::HIR::TypeRef& ty) const {
                // - The path must include Self as a (the only?) type param.
                if (ty == gCratePtr->types.self()) {
                    return true;
                } else if (ty == realSelfType) {
                    ty = gCratePtr->types.self();
                    return true;
                } else if (ty->is_Path()) {
                    auto data = ty->cloneData();
                    auto& e = data.as_Path();
                    if (auto* pe = e.path.mData.opt_Generic()) {
                        if (pe->mParams.types.size() == 0) {
                            ERROR(sp, E0000, "Receiver type should have one type param - " << ty);
                        }
                        //if( pe->m_params.m_types.size() != 1 ) {
                        //   TODO(sp, "Receiver types with more than one param - " << arg_self_ty);
                        //}

                        // TODO: Allow if the type parm is a valid receiver it type too
                        // - In general, it's valid if there's a deref chain from this type to `self` (maybe could check that in a later pass, instead of erroring here)
                        if (isValidCustomReceiver(pe->mParams.types[0])) {
                            ty = gCratePtr->types.intern(mv$(data));
                            return true;
                        }
                    }
                    return false;
                } else if (ty->is_Borrow()) {
                    const auto& e = ty->as_Borrow();
                    auto inner = e.inner;
                    if (!isValidCustomReceiver(inner)) {
                        return false;
                    }
                    ty = gCratePtr->types.borrow(e.type, inner, e.lifetime);
                    return true;
                } else if (ty->is_Pointer()) {
                    const auto& e = ty->as_Pointer();
                    auto inner = e.inner;
                    if (!isValidCustomReceiver(inner)) {
                        return false;
                    }
                    ty = gCratePtr->types.pointer(e.type, inner);
                    return true;
                } else {
                    return false;
                }
            }
        } ivcr(sp, realSelfType);

        if (argSelfTy == gCratePtr->types.self() || argSelfTy == realSelfType) {
            receiver = ::HIR::Function::Receiver::Value;
        } else if (const auto* e = argSelfTy->opt_Borrow()) {
            if (e->inner == gCratePtr->types.self() || e->inner == realSelfType) {
                if (e->inner == realSelfType) {
                    argSelfTy = gCratePtr->types.borrow(e->type, gCratePtr->types.self(), e->lifetime);
                }
                switch (e->type) {
                    case ::HIR::BorrowType::Owned:
                        receiver = ::HIR::Function::Receiver::BorrowOwned;
                        break;
                    case ::HIR::BorrowType::Unique:
                        receiver = ::HIR::Function::Receiver::BorrowUnique;
                        break;
                    case ::HIR::BorrowType::Shared:
                        receiver = ::HIR::Function::Receiver::BorrowShared;
                        break;
                }
            } else {
                auto inner = e->inner;
                if (ivcr.isValidCustomReceiver(inner)) {
                    argSelfTy = gCratePtr->types.borrow(e->type, inner, e->lifetime);
                    receiver = ::HIR::Function::Receiver::Custom;
                }
            }
        } else if (const auto* e = argSelfTy->opt_Path()) {
            // Box - Compare with `owned_box` lang item
            if (const auto* pe = e->path.mData.opt_Generic()) {
                auto p = gCratePtr->getLangItemPathOpt("owned_box");
                if (pe->mPath == p) {
                    if (pe->mParams.types.size() >= 1 && (pe->mParams.types[0] == gCratePtr->types.self() || pe->mParams.types[0] == realSelfType)) {
                        if (pe->mParams.types[0] == realSelfType) {
                            auto data = argSelfTy->cloneData();
                            data.as_Path().path.mData.as_Generic().mParams.types[0] = gCratePtr->types.self();
                            argSelfTy = gCratePtr->types.intern(mv$(data));
                        }
                        receiver = ::HIR::Function::Receiver::Box;
                    }
                }
                // TODO: for other types, support arbitary structs/paths.
                if (receiver == ::HIR::Function::Receiver::Free) {
                    if (ivcr.isValidCustomReceiver(argSelfTy)) {
                        receiver = ::HIR::Function::Receiver::Custom;
                    }
                }
            }
        } else if (ivcr.isValidCustomReceiver(argSelfTy)) {
            receiver = ::HIR::Function::Receiver::Custom;
        } else {
        }

        if (receiver == ::HIR::Function::Receiver::Free) {
            ERROR(sp, E0000, "Unknown receiver type - " << argSelfTy);
        }
    }

    bool forceEmit = false;
    HIR::Function::Markings markings;
    switch (f.markings.inlineType) {
        case ASTFunction::Markings::Inline::Auto:
            markings.inlineType = ::HIR::Function::Markings::Inline::Auto;
            break;
        case ASTFunction::Markings::Inline::Never:
            markings.inlineType = ::HIR::Function::Markings::Inline::Never;
            break;
        case ASTFunction::Markings::Inline::Always:
            markings.inlineType = ::HIR::Function::Markings::Inline::Always;
            forceEmit = true;
            break;
        case ASTFunction::Markings::Inline::Normal:
            markings.inlineType = ::HIR::Function::Markings::Inline::Normal;
            forceEmit = true;
            break;
    }

    // #[rustc_legacy_const_generics] - Used to convert a literal argument into a const generic
    for (auto idx : f.markings.rustcLegacyConstGenerics) {
        ASSERT_BUG(attrs.get("rustc_legacy_const_generics")->span(), idx < args.size() + f.markings.rustcLegacyConstGenerics.size(), "#[rustc_legacy_const_generics(" << idx << ")] out of range (0.." << args.size() + f.markings.rustcLegacyConstGenerics.size() << ")");
        markings.rustcLegacyConstGenerics.push_back(idx);
    }
    // #[track_caller] - Provides caller information
    // NOTE: This can only be (cleanly) handled in the backend [where it sees fully monomorphised paths]
    if (attrs.get("track_caller")) {
        markings.trackCaller = true;
    }
    markings.isNaked = f.markings.isNaked;

    ::HIR::Linkage linkage;
    switch (f.markings.linkage) {
        case ASTLinkage::Default:
            break;
        case ASTLinkage::Weak:
            linkage.type = HIR::Linkage::Type::Weak;
            break;
        case ASTLinkage::ExternWeak:
            BUG(sp, "Invalid linkage on function");
    }
    linkage.section = f.markings.linkSection;

    // Convert #[link_name/no_mangle] attributes into the name
    if (gAstCratePtr->testHarness && f.code().isValid()) {
        // If we're making a test harness, and this item defines code, don't apply the linkage rules
    } else if (f.markings.linkName != "") {
        linkage.name = f.markings.linkName;
    } else if (attrs.get("rustc_std_internal_symbol")) {
        linkage.name = p.getName();
        linkage.type = ::HIR::Linkage::Type::Weak;
    } else if (attrs.get("no_mangle")) {
        linkage.name = p.getName();
    } else {
        // Leave linkage.name as empty
    }

    // If there's no code, mangle the name (According to the ABI) and set linkage.
    if (linkage.name == "" && !f.code().isValid()) {
        linkage.name = p.getName();
    }

    ::HIR::Function rv;
    rv.saveCode = forceEmit;
    rv.linkage = mv$(linkage);
    rv.receiver = receiver;
    if (receiver == HIR::Function::Receiver::Custom) {
        rv.receiverType = MonomorphiserNop(gCratePtr->types).monomorphType(f.args()[0].ty.span(), args.front().second, false);
        // Ensure that the reciever references `Self`
        ASSERT_BUG(
            f.args()[0].ty.span(),
            visitTyWith(
                *rv.receiverType,
                [](const HIR::TypeData* v) {
            return v->is_Generic() && v->as_Generic().isSelf();
        }
            ),
            *rv.receiverType
        );
    }
    rv.mAbi = RcString::newInterned(f.abi());
    rv.unsafe = f.isUnsafe();
    rv.isConst = f.isConst();
    rv.mParams = LowerHIRGenericParams(f.params(), nullptr); // TODO: If this is a method, then it can add the Self: Sized bound
    rv.mArgs = mv$(args);
    rv.variadic = f.isVariadic();
    rv.returnType = LowerHIRType(f.rettype());
    rv.mCode = LowerHIRExpr(f.code());
    rv.markings = markings;

    if (f.isAsync()) {
        //rv.m_markings.is_async = true;
        // Wrap the code in an async block
        if (rv.mCode) {
            auto* asyncNode = gCratePtr->pool->make<::HIR::ExprNodeAsyncBlock>(sp, rv.mCode.takeNode(), true);
            asyncNode->resType = gCratePtr->types.infer();
            rv.mCode = HIR::ExprPtr(::HIR::ExprNodeP(asyncNode));
        }
        // Make the return type be `impl Future<Output=Ret>`
        HIR::TraitPath futurePath;
        futurePath.mPath.mPath = gCratePtr->getLangItemPath(sp, "future_trait");
        futurePath.typeBounds.insert(std::make_pair(RcString::newInterned("Output"), ::HIR::TraitPath::AtyEqual{futurePath.mPath.clone(), {}, std::move(rv.returnType)}));
        rv.returnType = gCratePtr->types.intern(::HIR::TypeData::make_ErasedType(::HIR::TypeDataErasedType{true, ::makeVec1(std::move(futurePath)), {}, ::HIR::TypeDataErasedTypeInner::Data_Fcn{::HIR::Path(::HIR::SimplePath()), 0}}));
    }

    return rv;
}

void _add_mod_ns_item(::HIR::Module& mod, RcString name, ::HIR::Publicity isPub, ::HIR::TypeItem ti) {
    mod.modItems.insert(::std::make_pair(mv$(name), ::makeUniquePtr(::HIR::VisEnt<::HIR::TypeItem>{isPub, mv$(ti)})));
}

void _add_mod_val_item(::HIR::Module& mod, RcString name, ::HIR::Publicity isPub, ::HIR::ValueItem ti) {
    mod.valueItems.insert(::std::make_pair(mv$(name), ::makeUniquePtr(::HIR::VisEnt<::HIR::ValueItem>{isPub, mv$(ti)})));
}

void _add_mod_mac_item(::HIR::Module& mod, RcString name, ::HIR::Publicity isPub, ::HIR::MacroItem ti) {
    mod.macroItems.insert(::std::make_pair(mv$(name), ::makeUniquePtr(::HIR::VisEnt<::HIR::MacroItem>{isPub, mv$(ti)})));
}

::HIR::ValueItem LowerHIRStatic(::HIR::ItemPath p, const ASTAttributeList& attrs, const ASTStatic& e, const Span& sp, const RcString& name) {
    TRACE_FUNCTION_F(p);

    if (e.sClass() == ASTStatic::CONST) {
        // Note: Empty names are allowed for `const _: ...`
        return ::HIR::ValueItem::make_Constant(::HIR::Constant(::HIR::GenericParams{}, LowerHIRType(e.type()), LowerHIRExpr(e.value())));
    } else {
        // Note: Empty names are allowed for `const _: ...`
        ASSERT_BUG(sp, name != "", "Empty constant name " << p);
        ::HIR::Linkage linkage;
        switch (e.markings.linkage) {
            case ASTLinkage::Default:
                break;
            case ASTLinkage::Weak:
                linkage.type = HIR::Linkage::Type::Weak;
                break;
            case ASTLinkage::ExternWeak:
                linkage.type = HIR::Linkage::Type::ExternWeak;
                break;
        }
        linkage.section = e.markings.linkSection;

        if (e.markings.linkName != "") {
            linkage.name = e.markings.linkName;
        }
        // If there's no code, demangle the name (TODO: By ABI) and set linkage.
        else if (linkage.name == "" && !e.value().isValid()) {
            linkage.name = name.c_str();
        }

        return ::HIR::ValueItem::make_Static(::HIR::Static(mv$(linkage), (e.sClass() == ASTStatic::MUT), LowerHIRType(e.type()), LowerHIRExpr(e.value())));
    }
}

::HIR::Module LowerHIRModule(const ASTModule& astMod, ::HIR::ItemPath path, ::std::vector<::HIR::SimplePath> traits) {
    TRACE_FUNCTION_F("path = " << path);
    ::HIR::Module mod{};

    mod.traits = mv$(traits);

    auto modPath = path.getSimplePath();
    auto getVis = [&](const ASTVisibility& vis) {
        return LowerHIRVis(modPath, vis);
    };

    // Populate trait list
    {
        struct Foo {
            HIR::Module& mod;

            Foo(HIR::Module& mod)
                : mod(mod)
            {
            }

            void pushTrait(::HIR::SimplePath sp) {
                if (::std::find(mod.traits.begin(), mod.traits.end(), sp) == mod.traits.end()) {
                    mod.traits.push_back(mv$(sp));
                }
            }

            void pushTraitAlias(const ASTPathBindingType::Data_TraitAlias& pbe) {
                if (pbe.trait_) {
                    pushTraitAliasAst(*pbe.trait_);
                } else if (pbe.hir) {
                    pushTraitAliasHir(*pbe.hir);
                } else {
                }
            }

            void pushTraitAliasAst(const ASTTraitAlias& ta) {
                for (const auto& e : ta.traits) {
                    if (const auto* pbe = e.ent.path->mBindings.type.binding.opt_TraitAlias()) {
                        pushTraitAlias(*pbe);
                    } else {
                        pushTrait(LowerHIRSimplePath(e.sp, *e.ent.path, FromASTPathClass::Type, true));
                    }
                }
            }

            void pushTraitAliasHir(const HIR::TraitAlias& ta) {
                for (const auto& p : ta.traits) {
                    if (const auto* tap = gCratePtr->getTypeitemByPath(Span(), p.mPath.mPath).opt_TraitAlias()) {
                        pushTraitAliasHir(*tap);
                    } else {
                        pushTrait(p.mPath.mPath);
                    }
                }
            }
        };

        Foo f{mod};
        for (const auto& traitPath : astMod.traits) {
            f.pushTrait(HIR::SimplePath((traitPath.crate == "" ? gCrateName : traitPath.crate), traitPath.nodes));
        }
        for (const auto& i : astMod.typeItems) {
            if (const auto* pbe = i.second.path.mBindings.type.binding.opt_TraitAlias()) {
                // TODO: Should expanding trait aliases instead be handled in expr_cs__enum.cpp?
                f.pushTraitAlias(*pbe);
            }
        }
    }

    for (unsigned int i = 0; i < astMod.anonMods().size(); i++) {
        const auto& submodPtr = astMod.anonMods()[i];
        if (submodPtr) {
            auto& submod = *submodPtr;
            auto name = RcString::newInterned(FMT("#" << i));
            auto itemPath = ::HIR::ItemPath(path, name.c_str());
            auto ti = ::HIR::TypeItem::make_Module(LowerHIRModule(submod, itemPath, mod.traits));
            _add_mod_ns_item(mod, mv$(name), ::HIR::Publicity::newPriv(modPath), mv$(ti));
        }
    }

    for (const auto& ip : astMod.mItems) {
        const auto& item = *ip;
        const auto& sp = item.span;
        auto itemPath = ::HIR::ItemPath(path, item.name.c_str());
        DEBUG(itemPath << " " << item.data.tagStr());
        TU_MATCH_HDRA( (item.data), {)
        TU_ARMA(None, e) {
            }
            TU_ARMA(Macro, e) {
                // NOTE: These are in `m_macros`
            }
            TU_ARMA(MacroInv, e) {
                // Valid.
                //BUG(sp, "Stray macro invocation in " << path);
            }
            TU_ARMA(GlobalAsm, e) {
                ::HIR::GlobalAssembly item;
                item.lines = std::move(e.lines);
                item.symbols.reserve(e.symbols.size());
                for (const ASTPath& s : e.symbols) {
                    item.symbols.push_back(LowerHIRPath(Span(), s, FromASTPathClass::Value));
                }
                item.options = e.options;
                gCratePtr->globalAsm.push_back(std::move(item));
            }
            TU_ARMA(ExternBlock, e) {
                if (e.items().size() > 0) {
                    TODO(sp, "Expand ExternBlock");
                }
                for (const auto& lib : e.libraries) {
                    gCratePtr->extLibs.push_back(::HIR::ExternLibrary{lib.libName});
                }
            }
            TU_ARMA(Impl, e) {
                // NOTE: impl blocks are handled in a second pass
            }
            TU_ARMA(NegImpl, e) {
                // NOTE: impl blocks are handled in a second pass
            }
            TU_ARMA(Use, e) {
                // Ignore - The index is used to add `Import`s
            }
            TU_ARMA(Module, e) {
                _add_mod_ns_item(mod, item.name, getVis(item.vis), LowerHIRModule(e, mv$(itemPath)));
            }
            TU_ARMA(Crate, e) {
                // All 'extern crate' items should be normalised into a list in the crate root
                // - If public, add a namespace import here referring to the root of the imported crate
                _add_mod_ns_item(mod, item.name, getVis(item.vis), ::HIR::TypeItem::make_Import({::HIR::SimplePath(e.name, {}), false, 0}));
            }
            TU_ARMA(Type, e) {
                if (e.type().mData.is_Any()) {
                    if (!e.params().mParams.empty() || !e.params().bounds.empty()) {
                        ERROR(item.span, E0000, "Generics on extern type");
                    }
                    _add_mod_ns_item(mod, item.name, getVis(item.vis), ::HIR::ExternType{});
                    break;
                }
                _add_mod_ns_item(mod, item.name, getVis(item.vis), ::HIR::TypeItem::make_TypeAlias(LowerHIRTypeAlias(itemPath, e)));
            }
            TU_ARMA(Struct, e) {
                /// Add value reference
                if (e.mData.is_Unit()) {
                    _add_mod_val_item(mod, item.name, getVis(item.vis), ::HIR::ValueItem::make_StructConstant({itemPath.getSimplePath()}));
                } else if (e.mData.is_Tuple()) {
                    _add_mod_val_item(mod, item.name, getVis(item.vis), ::HIR::ValueItem::make_StructConstructor({itemPath.getSimplePath()}));
                } else {
                }
                _add_mod_ns_item(mod, item.name, getVis(item.vis), LowerHIRStruct(ip->span, itemPath, e, item.attrs, mod));
            }
            TU_ARMA(Enum, e) {
                auto enm = LowerHIREnum(itemPath, e, item.attrs, [&](auto name, auto str) {
                    _add_mod_ns_item(mod, name, getVis(item.vis), mv$(str));
                }, mod);
                _add_mod_ns_item(mod, item.name, getVis(item.vis), mv$(enm));
            }
            TU_ARMA(Union, e) {
                _add_mod_ns_item(mod, item.name, getVis(item.vis), LowerHIRUnion(itemPath, e, item.attrs));
            }
            TU_ARMA(Trait, e) {
                _add_mod_ns_item(mod, item.name, getVis(item.vis), LowerHIRTrait(itemPath.getSimplePath(), e, item.attrs));
            }
            TU_ARMA(TraitAlias, e) {
                _add_mod_ns_item(mod, item.name, getVis(item.vis), LowerHIRTraitAlias(sp, itemPath, e));
            }
            TU_ARMA(Function, e) {
                _add_mod_val_item(mod, item.name, getVis(item.vis), LowerHIRFunction(itemPath, item.attrs, e, ::HIR::TypeRef{}));
            }
            TU_ARMA(Static, e) {
                _add_mod_val_item(mod, item.name, getVis(item.vis), LowerHIRStatic(itemPath, item.attrs, e, sp, item.name));
            }
        }
    }
    // Some explicit handling of mac
    for (auto& mac : const_cast<ASTModule&>(astMod).macros()) {
        if (mac.data || mac.vis.isGlobal()) {
            ASSERT_BUG(mac.span, mac.data, "Null macro - " << mac.name);
            ASSERT_BUG(mac.span, mac.data->rules.size() > 0, "Empty macro - " << mac.name);
            _add_mod_mac_item(mod, mac.name, getVis(mac.vis), std::move(mac.data));
        }
    }

    // Imports
    Span modSpan;
    for (const auto& ie : astMod.namespaceItems) {
        const auto& sp = modSpan;
        // TODO: Only transfer private imports if this module contains a `macro`?
        // - Well... sub-modules that contain a `macro` would also lead to the same import
        if (ie.second.isImport) { //&& ie.second.is_pub ) {
            auto hirPath = LowerHIRSimplePath(sp, ie.second.path, FromASTPathClass::Type);
            assert(hirPath.components().empty() || hirPath.components().back() != "");
            ::HIR::TypeItem ti;
            if (const auto* pb = ie.second.path.mBindings.type.binding.opt_EnumVar()) {
                DEBUG("Import NS " << ie.first << " = " << hirPath << " (Enum Variant)");
                ti = ::HIR::TypeItem::make_Import({mv$(hirPath), true, pb->idx});
            } else {
                DEBUG("Import NS " << ie.first << " = " << hirPath);
                ti = ::HIR::TypeItem::make_Import({mv$(hirPath), false, 0});
            }
            _add_mod_ns_item(mod, ie.first, getVis(ie.second.vis), mv$(ti));
        }
    }
    for (const auto& ie : astMod.valueItems) {
        const auto& sp = modSpan;
        // These have no purpose being in HIR (while unnamed traits can be used to bring traits into scope in downstream crates)
        if (ie.first.c_str()[0] == ' ') {
            continue;
        }
        // TODO: See code for `m_namespace_items` above
        if (ie.second.isImport) { //&& ie.second.is_pub ) {
            auto hirPath = LowerHIRSimplePath(sp, ie.second.path, FromASTPathClass::Value);
            assert(!hirPath.components().empty());
            assert(hirPath.components().back() != "");
            ::HIR::ValueItem vi;

            TU_MATCH_HDRA( (ie.second.path.mBindings.value.binding), {)
            default:
                DEBUG("Import VAL " << ie.first << " = " << hirPath);
                vi = ::HIR::ValueItem::make_Import({mv$(hirPath), false, 0});
                TU_ARMA(EnumVar, pb) {
                    DEBUG("Import VAL " << ie.first << " = " << hirPath << " (Enum Variant)");
                    vi = ::HIR::ValueItem::make_Import({mv$(hirPath), true, pb.idx});
                }
            }
            _add_mod_val_item(mod, ie.first, getVis(ie.second.vis), mv$(vi));
        }
    }

    for (const auto& ie : astMod.macroItems) {
        const auto& sp = modSpan;
        if (ie.first.c_str()[0] == ' ') {
            continue;
        }
        auto hirPath = LowerHIRSimplePath(sp, ie.second.path, FromASTPathClass::Macro);
        if (ie.second.isImport) {
            assert(!hirPath.components().empty());
            assert(hirPath.components().back() != "");

            DEBUG("Import MACRO " << ie.first << " = " << hirPath);
            auto mi = ::HIR::MacroItem::make_Import({mv$(hirPath)});
            _add_mod_mac_item(mod, ie.first, getVis(ie.second.vis), mv$(mi));
        } else {
            DEBUG("Defined MACRO " << ie.first << " = " << hirPath);
        }
    }

    return mod;
}

void LowerHIRModuleImpls(const ASTModule& astMod, ::HIR::Crate& hirCrate) {
    TRACE_FUNCTION_F(astMod.path());
    ::HIR::SimplePath modPath(gCrateName, astMod.path().nodes);

    // Sub-modules
    for (const auto& item : astMod.mItems) {
        if (const auto* e = item->data.opt_Module()) {
            LowerHIRModuleImpls(*e, hirCrate);
        }
    }
    for (const auto& submodPtr : astMod.anonMods()) {
        if (submodPtr) {
            LowerHIRModuleImpls(*submodPtr, hirCrate);
        }
    }

    //
    for (const auto& i : astMod.mItems) {
        if (!i->data.is_Impl()) {
            continue;
        }
        const auto& impl = i->data.as_Impl();
        const Span implSpan;
        auto params = LowerHIRGenericParams(impl.def().params(), nullptr);

        TRACE_FUNCTION_F("IMPL " << impl.def());

        if (impl.def().trait().ent.isValid()) {
            const auto& pb = impl.def().trait().ent.mBindings.type.binding;
            ASSERT_BUG(Span(), pb.is_Trait(), "Binding for trait path in impl isn't a Trait - " << impl.def().trait().ent);
            ASSERT_BUG(Span(), pb.as_Trait().trait_ || pb.as_Trait().hir, "Trait pointer for trait path in impl isn't set");
            bool isMarker = (pb.as_Trait().trait_ ? pb.as_Trait().trait_->isMarker() : pb.as_Trait().hir->mIsMarker);
            auto traitPath = LowerHIRGenericPath(impl.def().trait().sp, impl.def().trait().ent, FromASTPathClass::Type);
            auto traitName = mv$(traitPath.mPath);
            auto traitArgs = mv$(traitPath.mParams);

            if (!isMarker) {
                auto type = LowerHIRType(impl.def().type());

                ::HIR::ItemPath path(type, traitName, traitArgs);
                DEBUG("path = " << path);

                ::std::map<RcString, ::HIR::TraitImpl::ImplEnt<::HIR::Function>> methods;
                ::std::map<RcString, ::HIR::TraitImpl::ImplEnt<::HIR::Constant>> constants;
                ::std::map<RcString, ::HIR::TraitImpl::ImplEnt<::HIR::TypeRef>> types;

                for (const auto& item : impl.items()) {
                    ::HIR::ItemPath itemPath(path, item.name.c_str());
                    TU_MATCH_HDRA( (*item.data), {)
                    default:
                        BUG(item.sp, "Unexpected item type in trait impl - " << item.data->tagStr());
                        TU_ARMA(None, e) {
                        }
                        TU_ARMA(MacroInv, e) {
                        }
                        TU_ARMA(Static, e) {
                            if (e.sClass() == ASTStatic::CONST) {
                                // TODO: Check signature against the trait?
                                constants.insert(::std::make_pair(item.name, ::HIR::TraitImpl::ImplEnt<::HIR::Constant>{item.isSpecialisable, ::HIR::Constant(::HIR::GenericParams{}, LowerHIRType(e.type()), LowerHIRExpr(e.value()))}));
                            } else {
                                TODO(item.sp, "Associated statics in trait impl");
                            }
                        }
                        TU_ARMA(Type, e) {
                            DEBUG("- type " << item.name);
                            auto atyParams = LowerHIRGenericParams(e.params(), nullptr);
                            //ASSERT_BUG(Span(), aty_params.is_empty(), "TODO: GATs");

                            assert(!gImplTraitSource.path);
                            HIR::ItemPath ip1(modPath);
                            ::std::string name2 = ::std::string("#impl_") + ::std::to_string((uintptr_t)&impl) + "_" + item.name.c_str();
                            HIR::ItemPath ip2(ip1, name2.c_str());
                            gImplTraitSource = ImplTraitSource(&ip2, &params, &atyParams);

                            types.insert(::std::make_pair(item.name, ::HIR::TraitImpl::ImplEnt<::HIR::TypeRef>{item.isSpecialisable, LowerHIRType(e.type())}));

                            gImplTraitSource = ImplTraitSource();
                        }
                        TU_ARMA(Function, e) {
                            DEBUG("- method " << item.name);
                            auto fcn = LowerHIRFunction(itemPath, item.attrs, e, type);
                            if (impl.def().isConst()) {
                                fcn.isConst = true;
                            }
                            methods.insert(::std::make_pair(item.name, ::HIR::TraitImpl::ImplEnt<::HIR::Function>{item.isSpecialisable, mv$(fcn)}));
                        }
                    }
                }

                // Sorted later on
                auto hirImpl = ::std::make_unique<HIR::TraitImpl>(::HIR::TraitImpl{
                        mv$(params),
                        mv$(traitArgs),
                        mv$(type),

                        mv$(methods),
                        mv$(constants),
                        {}, // Statics
                        mv$(types),

                        modPath
                    });
                hirImpl->isConst = impl.def().isConst();
                hirCrate.traitImpls[mv$(traitName)].generic.push_back(mv$(hirImpl));
            } else if (impl.def().type().mData.is_None()) {
                // Ignore - These are encoded in the 'is_marker' field of the trait
            } else {
                auto type = LowerHIRType(impl.def().type());
                hirCrate.markerImpls[mv$(traitName)].generic.push_back(box$(
                    ::HIR::MarkerImpl{
                        mv$(params),
                        mv$(traitArgs),
                        true,
                        mv$(type),

                        modPath
                    }
                ));
            }
        } else {
            // Inherent impls
            auto type = LowerHIRType(impl.def().type());
            ::HIR::ItemPath path(type);

            auto getVis = [&](const ASTVisibility& vis) {
                return LowerHIRVis(modPath, vis);
            }; // TODO: Does this need to consume anon modules?

            ::std::map<RcString, ::HIR::TypeImpl::VisImplEnt<::HIR::Function>> methods;
            ::std::map<RcString, ::HIR::TypeImpl::VisImplEnt<::HIR::Constant>> constants;
            ::std::map<RcString, ::HIR::TypeImpl::VisImplEnt<::HIR::TypeAlias>> types;

            for (const auto& item : impl.items()) {
                ::HIR::ItemPath itemPath(path, item.name.c_str());
                TU_MATCH_HDRA( (*item.data), {)
                default:
                    BUG(item.sp, "Unexpected item type in inherent impl - " << item.data->tagStr());
                    TU_ARMA(None, e) {
                    }
                    TU_ARMA(MacroInv, e) {
                    }
                    TU_ARMA(Static, e) {
                        if (e.sClass() == ASTStatic::CONST) {
                            constants.insert(::std::make_pair(item.name, ::HIR::TypeImpl::VisImplEnt<::HIR::Constant>{getVis(item.vis), item.isSpecialisable, ::HIR::Constant(::HIR::GenericParams{}, LowerHIRType(e.type()), LowerHIRExpr(e.value()))}));
                        } else {
                            TODO(item.sp, "Associated statics in inherent impl");
                        }
                    }
                    TU_ARMA(Type, e) {
                        DEBUG("- type " << item.name);
                        auto atyParams = LowerHIRGenericParams(e.params(), nullptr);

                        assert(!gImplTraitSource.path);
                        gImplTraitSource = ImplTraitSource(&itemPath, &params, &atyParams);
                        auto atyType = LowerHIRType(e.type());
                        gImplTraitSource = ImplTraitSource();

                        types.insert(::std::make_pair(item.name, ::HIR::TypeImpl::VisImplEnt<::HIR::TypeAlias>{
                            getVis(item.vis),
                            item.isSpecialisable,
                            ::HIR::TypeAlias{mv$(atyParams), mv$(atyType)}
                        }));
                    }
                    TU_ARMA(Function, e) {
                        methods.insert(::std::make_pair(item.name, ::HIR::TypeImpl::VisImplEnt<::HIR::Function>{getVis(item.vis), item.isSpecialisable, LowerHIRFunction(itemPath, item.attrs, e, type)}));
                    }
                }
            }

            // Sorted later on
            hirCrate.typeImpls.generic.push_back(box$(
                ::HIR::TypeImpl{
                    mv$(params),
                    mv$(type),
                    mv$(methods),
                    mv$(constants),
                    mv$(types),

                    modPath
                }
            ));
        }
    }
    for (const auto& i : astMod.mItems) {
        if (!i->data.is_NegImpl()) {
            continue;
        }
        const auto& impl = i->data.as_NegImpl();

        auto params = LowerHIRGenericParams(impl.params(), nullptr);
        auto type = LowerHIRType(impl.type());
        auto trait = LowerHIRGenericPath(impl.trait().sp, impl.trait().ent, FromASTPathClass::Type);
        auto traitName = mv$(trait.mPath);
        auto traitArgs = mv$(trait.mParams);

        // Sorting done later
        hirCrate.markerImpls[mv$(traitName)].generic.push_back(box$(
            ::HIR::MarkerImpl{
                mv$(params),
                mv$(traitArgs),
                false,
                mv$(type),

                modPath
            }
        ));
    }
}

class IndexVisitor: public ::HIR::Visitor {
    const ::HIR::Crate& crate;
    Span nullSpan;

public:
    IndexVisitor(const ::HIR::Crate& crate)
        : ::HIR::Visitor(nullptr, crate.types)
        , crate(crate)
    {
    }

    void visitParams(::HIR::GenericParams& params) override {
        for (auto& bound : params.bounds) {
            if (auto* e = bound.opt_TraitBound()) {
                e->trait.traitPtr = &this->crate.getTraitByPath(nullSpan, e->trait.mPath.mPath);
            }
        }
    }
};

/// \brief Converts the AST into HIR format
///
/// - Removes all possibility for unexpanded macros
/// - Performs desugaring of for/if-let/while-let/...
::HIR::Crate* LowerHIRFromAST(stl::ObjPool* pool, ASTCrate& crate) {
    auto& rv = *pool->make<::HIR::Crate>(pool, crate.types);

    if (crate.crateType != ASTCrate::Type::Executable) {
        rv.crateName = crate.crateNameReal;
    } else {
        // Use a non-empty crate name that won't conflict with any libraries
        rv.crateName = "bin#";
    }
    rv.edition = crate.edition;
    rv.isNoCore = crate.loadStd == ASTCrate::LOAD_NONE;
    rv.noMain = crate.noMain;
    rv.features = crate.features;

    gCratePtr = &rv;
    gAstCratePtr = &crate;
    gCrateName = rv.crateName;
    gCoreCrate = crate.extCratenameCore;
    auto macros = std::map<RcString, HIR::MacroItem>();
    //auto& macros = rv.m_exported_macros;

    // - Extract exported macros
    {
        TRACE_FUNCTION_FR("macros", "macros");
        ::std::vector<ASTModule*> mods;
        mods.push_back(&crate.mRootModule);
        do {
            auto& mod = *mods.back();
            mods.pop_back();

            for (/*const*/ auto& mac : mod.macros()) {
                if (mac.data->exported) {
                    HIR::MacroItem mi;
                    if (&mod == &crate.mRootModule) {
                        mi = mv$(mac.data);
                    } else {
                        assert(mac.data);
                        assert(!mac.data->rules.empty());
                        auto pc = mod.path().nodes;
                        pc.push_back(mac.name);
                        mi = HIR::MacroItem::make_Import({::HIR::SimplePath(gCrateName, std::move(pc))});
                    }
                    ASSERT_BUG(Span(), macros.count(mac.name) == 0, "Duplicate export of: " << mac.name);
                    if (macros.count(mac.name) == 0) {
                        auto res = macros.insert(::std::make_pair(mac.name, mv$(mi)));
                        if (res.second) {
                            DEBUG("- Define " << mac.name << "!");
                            rv.exportedMacroNames.push_back(mac.name);
                        }
                        if (res.first->second.is_MacroRules()) {
                            ASSERT_BUG(Span(), !res.first->second.as_MacroRules()->rules.empty(), "Empty macro? - " << mac.name);
                        }
                    }

#if 1
                    for (auto& e : macros) {
                        if (e.second.is_MacroRules()) {
                            ASSERT_BUG(Span(), !e.second.as_MacroRules()->rules.empty(), "Empty macro? - " << e.first);
                        }
                    }
#endif
                } else {
                    DEBUG("- Non-exported " << mac.name << "!");
                }
            }

            for (auto& i : mod.mItems) {
                if (i->data.is_Module()) {
                    mods.push_back(&i->data.as_Module());
                }
            }
        } while (mods.size() > 0);

        for (const auto& mac : crate.mRootModule.macroImports) {
            if (mac.isPub || (mac.ref.is_MacroRules() && mac.ref.as_MacroRules()->exported)) {
                // Add to the re-export list
                auto path = ::HIR::SimplePath(mac.path.crate == "" ? gCrateName : mac.path.crate, mac.path.nodes);
                auto res = macros.insert(std::make_pair(mac.name, HIR::MacroItem::make_Import({path})));
                if (!res.second) {
                    DEBUG("Conflict in imported vs local macros: " << mac.name);
                } else {
                    DEBUG("Re-export " << mac.name << "! = " << path);
                    rv.exportedMacroNames.push_back(mac.name);
                }
            }
        }

        for (const auto& i : crate.mRootModule.macroItems) {
            if (i.second.vis.isGlobal()) {
                rv.exportedMacroNames.push_back(i.first);
            }
        }
    }
    // - Proc Macros
    if (crate.crateType == ASTCrate::Type::ProcMacro) {
        for (const auto& ent : crate.procMacros) {
            struct H {
                static ::HIR::ProcMacro::Ty cvtMacroTy(ASTProcMacroTy ast) {
                    switch (ast) {
                        case ASTProcMacroTy::Function:
                            return ::HIR::ProcMacro::Ty::Function;
                        case ASTProcMacroTy::Derive:
                            return ::HIR::ProcMacro::Ty::Derive;
                        case ASTProcMacroTy::Attribute:
                            return ::HIR::ProcMacro::Ty::Attribute;
                    }
                    throw "Invalid AST macro type";
                }
            };

            // Register under an invalid SimplePath
            ::HIR::ProcMacro::Ty ty = H::cvtMacroTy(ent.ty);
            macros.insert(std::make_pair(ent.name, ::HIR::ProcMacro{ty, ent.name, ::HIR::SimplePath(RcString(""), {ent.name}), ent.attributes}));
            rv.exportedMacroNames.push_back(ent.name);
            DEBUG("Export proc_macro " << ent.name);
        }
    } else {
        ASSERT_BUG(Span(), crate.procMacros.size() == 0, "Procedural macros defined in non proc-macro crate");
    }

    auto sp = Span();
    // - Store the lang item paths so conversion code can use them.
    for (const auto& langItemPath : crate.mLangItems) {
        assert(langItemPath.second.crate == "");
        rv.mLangItems.insert(::std::make_pair(langItemPath.first, HIR::SimplePath(gCrateName, langItemPath.second.nodes)));
        DEBUG("Defined language item '" << langItemPath.first << "' at " << langItemPath.second);
    }
    rv.extCratesOrdered = crate.externCratesOrd;
    for (auto& extCrate : crate.externCrates) {
        // Populate m_lang_items from loaded crates too
        for (const auto& lang : extCrate.second.hir->mLangItems) {
            const auto& name = lang.first;
            const auto& path = lang.second;
            auto irv = rv.mLangItems.insert(::std::make_pair(name, path));
            DEBUG("Load language item '" << lang.first << "' at " << lang.second << " from " << extCrate.first);
            if (irv.second == true) {
                // Doesn't yet exist, all good
            } else if (irv.first->second == path) {
                // Equal definitions, also good (TODO: How can this happen?)
            } else if (irv.first->second.components().empty() && path.components().empty()) {
                // Both are just markers, also good (e.g. #![needs_panic_runtime])
            } else {
                ERROR(sp, E0000, "Conflicting definitions of lang item '" << name << "'. " << path << " and " << irv.first->second);
            }
        }
        auto p1 = extCrate.second.filename.rfind('/');
        auto p2 = extCrate.second.filename.rfind('\\');
        auto p = (p1 == ::std::string::npos ? p2 : (p2 == ::std::string::npos ? p1 : ::std::max(p1, p2)));
        auto crateFile = (p == ::std::string::npos ? extCrate.second.filename : extCrate.second.filename.substr(p + 1));
        rv.extCrates.insert(::std::make_pair(extCrate.first, ::HIR::ExternCrate{extCrate.second.hir, crateFile, extCrate.second.filename}));
    }
    pathSized = rv.getLangItemPathOpt("sized");
    pathPointeeSized = rv.getLangItemPathOpt("pointee_sized");
    pathMetadataSized = rv.getLangItemPathOpt("metadata_sized");

    rv.mRootModule = LowerHIRModule(crate.mRootModule, ::HIR::ItemPath(rv.crateName));
    for (auto& e : macros) {
        if (e.second.is_MacroRules()) {
            ASSERT_BUG(Span(), !e.second.as_MacroRules()->rules.empty(), "Empty macro? - " << e.first);
        }
        rv.mRootModule.macroItems.insert(::std::make_pair(e.first, box$(HIR::VisEnt<HIR::MacroItem>{HIR::Publicity::newGlobal(), mv$(e.second)})));
    }

    LowerHIRModuleImpls(crate.mRootModule, rv);

    // Set all pointers in the HIR to the correct (now fixed) locations
    //IndexVisitor(rv).visit_crate( rv );

    // Macro fixups:
    // - Convert interpolated AST items to token sequences
    {
        struct H {
            static void fixMacroContents(std::vector<MacroExpansionEnt>& ruleContents) {
                for (auto it = ruleContents.begin(); it != ruleContents.end();) {
                    if (auto* tok = it->opt_Token()) {
                        //TODO: Can this share with `proc_macro`? Maybe a function on AST types to generate a token tree from the AST again.
                        struct NewToks {
                            std::vector<MacroExpansionEnt> out;

                            void emitFromString(const std::string& s) {
                                ::std::istringstream iss{s};
                                Lexer l{iss, ASTEdition::Rust2021, {}};
                                for (;;) {
                                    auto t = l.getToken();
                                    if (t == TOK_EOF) {
                                        break;
                                    }
                                    out.push_back(t);
                                }
                            }

                            void emitAst(const ASTExprNode& e) {
                                if (const auto* ep = cast<const ASTExprNodeInteger>(&e)) {
                                    out.push_back(Token(ep->mValue, ep->datatype));
                                } else if (const auto* ep = cast<const ASTExprNodeBool>(&e)) {
                                    out.push_back(ep->mValue ? TOK_RWORD_TRUE : TOK_RWORD_FALSE);
                                } else {
                                    throw std::runtime_error(FMT("Unknown node type: " << typeid(e).name()));
                                }
                            }

                            void emitPath(const ASTPath& path) {
                                ::std::stringstream ss;
                                ss << path;
                                emitFromString(ss.str());
                            }

                            void emitType(::TypeRef& ty) {
                                TU_MATCH_HDRA( (ty.mData), { )
                                default:
                                    TODO(Span(), "Convert interpolated macro fragment: " << ty);
                                    TU_ARMA(Path, p) {
                                        emitPath(*p);
                                    }
                                }
                            }

                            void emitTokentree(TokenTree& tt) {
                                if (tt.isToken()) {
                                    emitToken(tt.tok());
                                } else {
                                    for (size_t i = 0; i < tt.size(); i++) {
                                        emitTokentree(tt[i]);
                                    }
                                }
                            }

                            void emitToken(Token& tok) {
                                switch (tok.type()) {
                                    case TOK_INTERPOLATED_PATH:
                                    case TOK_INTERPOLATED_PATTERN:
                                    case TOK_INTERPOLATED_STMT:
                                    case TOK_INTERPOLATED_STMT_ITEM:
                                    case TOK_INTERPOLATED_BLOCK:
                                    case TOK_INTERPOLATED_ITEM:
                                    case TOK_INTERPOLATED_VIS:
                                        // Emit as a token tree with no separator
                                        TODO(Span(), "Convert interpolated macro fragment: " << tok);
                                        break;
                                    case TOK_INTERPOLATED_TYPE:
                                        emitType(tok.fragType());
                                        break;
                                    case TOK_INTERPOLATED_META: {
                                        auto& i = tok.fragMeta();
                                        for (const auto& e : i.name().elems) {
                                            if (&e != &i.name().elems.front()) {
                                                out.push_back(Token(TOK_DOUBLE_COLON));
                                            }
                                            out.push_back(Token(TOK_IDENT, e));
                                        }
                                        emitTokentree(i.dataMut());
                                        break;
                                    }
                                    case TOK_INTERPOLATED_EXPR:
                                        try {
                                            emitAst(tok.fragNode());
                                        } catch (const std::exception& e) {
                                            TODO(Span(), "Convert interpolated macro fragment: " << tok << " - " << e.what());
                                        }
                                        break;
                                    default:
                                        out.push_back(std::move(tok));
                                        return;
                                }
                            }
                        };

                        NewToks newToks;
                        switch (tok->type()) {
                            case TOK_INTERPOLATED_PATH:
                            case TOK_INTERPOLATED_TYPE:
                            case TOK_INTERPOLATED_PATTERN:
                            case TOK_INTERPOLATED_STMT:
                            case TOK_INTERPOLATED_STMT_ITEM:
                            case TOK_INTERPOLATED_BLOCK:
                            case TOK_INTERPOLATED_ITEM:
                            case TOK_INTERPOLATED_VIS:
                            case TOK_INTERPOLATED_META:
                            case TOK_INTERPOLATED_EXPR:
                                newToks.emitToken(*tok);
                                break;
                            default:
                                ++it;
                                continue;
                        }
                        if (newToks.out.size() == 0) {
                            it = ruleContents.erase(it);
                        } else {
                            const auto replacementCount = newToks.out.size();
                            *it = std::move(newToks.out.front());
                            it += 1;
                            if (replacementCount > 1) {
                                it = ruleContents.insert(it, std::move_iterator<decltype(newToks.out.begin())>(newToks.out.begin() + 1), std::move_iterator<decltype(newToks.out.begin())>(newToks.out.end()));
                                it += replacementCount - 1;
                            }
                        }
                    } else {
                        ++it;
                    }
                }
            }

            static void fixMacrosInMod(HIR::ItemPath path, HIR::Module& mod) {
                TRACE_FUNCTION_F(path);
                for (auto& mi : mod.modItems) {
                    if (auto* submodP = mi.second->ent.opt_Module()) {
                        fixMacrosInMod(path + mi.first, *submodP);
                    }
                }
                for (auto& mi : mod.macroItems) {
                    if (auto* mrpp = mi.second->ent.opt_MacroRules()) {
                        auto& mr = **mrpp;
                        if (mr.sourceCrate.size() == 0) {
                            mr.sourceCrate = gCrateName;
                        }
                        for (auto& rule : mr.rules) {
                            fixMacroContents(rule.contents);
                        }
                    }
                    if (const auto* i = mi.second->ent.opt_Import()) {
                        DEBUG(path << ": Import " << mi.first << " = " << i->path);
                        if (i->path.crateName() == CRATE_BUILTINS) {
                        } else if (const auto* i2 = gCratePtr->getMacroitemByPath(Span(), i->path).opt_Import()) {
                            BUG(Span(), "Attempted recusive import - " << i->path << " points at " << i2->path);
                        }
                    }
                }
            }
        };

        H::fixMacrosInMod(HIR::ItemPath(""), rv.mRootModule);
    }

    if (gCoreCrate == "") {
        gCoreCrate = gCrateName;
    }

    gCratePtr = nullptr;
    return &rv;
}


struct LowerHIRExprNodeVisitor: public ASTNodeVisitor {
    ::HIR::ExprNodeP mRv;

    struct LoopLabel {
        Ident source;
        RcString lowered;
        size_t macroDefinitionDepth;
    };
    struct MacroDefinition {
        unsigned int definitionId;
        Ident::Hygiene tokenHygiene;
        Ident::Hygiene definitionHygiene;
    };
    ::std::vector<LoopLabel> loopLabels;
    ::std::vector<MacroDefinition> macroDefinitions;
    unsigned nextLoopLabel = 0;

    // Used to track if a closure is a generator or a normal closure
    // - They have different HIR node types
    bool mHasYield = false;

    RcString enterLoopLabel(const Ident& source) {
        if (source.name == "") {
            return {};
        }
        auto lowered = RcString::newInterned(FMT("@label" << nextLoopLabel++));
        loopLabels.push_back(LoopLabel{source, lowered, macroDefinitions.size()});
        return lowered;
    }

    void leaveLoopLabel(const RcString& lowered) {
        if (lowered == "") {
            return;
        }
        assert(!loopLabels.empty());
        assert(loopLabels.back().lowered == lowered);
        loopLabels.pop_back();
    }

    RcString resolveLoopLabel(const Span& sp, const Ident& target) const {
        if (target.name == "") {
            return {};
        }
        auto targetHygiene = target.hygiene;
        size_t definitionDepth = macroDefinitions.size();
        for (auto it = loopLabels.rbegin(); it != loopLabels.rend(); ++it) {
            while (definitionDepth > it->macroDefinitionDepth) {
                const auto& definition = macroDefinitions[--definitionDepth];
                targetHygiene.leaveMacroDefinition(
                    definition.definitionId,
                    definition.tokenHygiene,
                    definition.definitionHygiene
                );
            }
            if (it->source.name == target.name && it->source.hygiene.isVisible(targetHygiene)) {
                return it->lowered;
            }
        }
        ERROR(sp, E0000, "Could not find loop label '" << target.name);
    }

    ::HIR::ExprNodeP lower(ASTExprNodeP& ep) {
        assert(ep);
        ep->visit(*this);
        ASSERT_BUG(ep->span(), mRv, ep.typeName() << " - Yielded a nullptr HIR node");
        mRv->resType = gCratePtr->types.infer();
        return std::move(mRv);
    }

    ::HIR::ExprNodeP lowerOpt(ASTExprNodeP& ep) {
        if (ep) {
            return lower(ep);
        } else {
            return nullptr;
        }
    }

    ::HIR::ExprNodeP lowerIsolated(ASTExprNodeP& ep) {
        ::std::vector<LoopLabel> outerLabels;
        outerLabels.swap(loopLabels);
        auto rv = lower(ep);
        assert(loopLabels.empty());
        outerLabels.swap(loopLabels);
        return rv;
    }

    virtual void visit(ASTExprNodeBlock& v) override {
        const size_t macroDefinitionBase = macroDefinitions.size();
        auto label = enterLoopLabel(v.label);
        auto rv = gCratePtr->pool->make<::HIR::ExprNodeBlock>(v.span());
        bool lastHasSemicolon = true;
        for (auto& n : v.nodes) {
            ASSERT_BUG(v.span(), n.node, "NULL node encountered in block");
            if (const auto* definition = cast<ASTExprNodeMacroDefinition>(n.node.get())) {
                macroDefinitions.push_back(MacroDefinition{
                    definition->definitionId,
                    definition->tokenHygiene,
                    definition->definitionHygiene
                });
                continue;
            }
            rv->nodes.push_back(lower(n.node));
            lastHasSemicolon = n.hasSemicolon;
        }
        leaveLoopLabel(label);
        // If the final node wasn't a statement (there wasn't a semicolon on it), then make that the value
        if (!rv->nodes.empty() && !lastHasSemicolon) {
            rv->valueNode = mv$(rv->nodes.back());
            rv->nodes.pop_back();
        }
        macroDefinitions.resize(macroDefinitionBase);

        if (v.localMod) {
            // TODO: Populate m_traits from the local module's import list
            rv->localMod = ::HIR::SimplePath(gCrateName, v.localMod->path().nodes);
        }

        switch (v.blockType) {
            case ASTExprNodeBlock::Type::Bare:
                break;
            case ASTExprNodeBlock::Type::Unsafe:
                rv->mIsUnsafe = true;
                break;
            case ASTExprNodeBlock::Type::Const:
                break;
        }

        if (label != "") {
            if (rv->valueNode) {
                auto* breakNode = gCratePtr->pool->make<::HIR::ExprNodeLoopControl>(v.span(), label, /*cont=*/false, ::std::move(rv->valueNode));
                rv->nodes.push_back(HIR::ExprNodeP(breakNode));
                rv->valueNode.reset();
            }
            auto* loop = gCratePtr->pool->make<::HIR::ExprNodeLoop>(v.span(), label, HIR::ExprNodeP(rv));
            loop->requireLabel = true;
            mRv.reset(loop);
        } else {
            mRv.reset(static_cast<::HIR::ExprNode*>(rv));
        }

        switch (v.blockType) {
            case ASTExprNodeBlock::Type::Bare:
                break;
            case ASTExprNodeBlock::Type::Unsafe:
                break;
            case ASTExprNodeBlock::Type::Const:
                mRv.reset(gCratePtr->pool->make<::HIR::ExprNodeConstBlock>(v.span(), std::move(mRv)));
                break;
        }
    }

    virtual void visit(ASTExprNodeAsyncBlock& v) override {
        mRv.reset(gCratePtr->pool->make<::HIR::ExprNodeAsyncBlock>(v.span(), lowerIsolated(v.inner), v.isMove));
    }

    virtual void visit(ASTExprNodeGeneratorBlock& v) override {
        // TODO: Wrap with something that provides an impl of Iterator
        // - `::core::iter::from_coroutine`
        mRv.reset(gCratePtr->pool->make<::HIR::ExprNodeGenerator>(
            v.span(),
            gCratePtr->types.infer(),
            gCratePtr->types.infer(),
            gCratePtr->types.infer(),
            lowerIsolated(v.inner),
            v.isMove,
            false
        ));
        mRv.reset(gCratePtr->pool->make<::HIR::ExprNodeCallPath>(v.span(), HIR::SimplePath(gCoreCrate, {"iter", "sources", "from_coroutine", "from_coroutine"}), makeVec1(mv$(mRv))));
    }

    virtual void visit(ASTExprNodeTry& v) override {
        TODO(v.span(), "Handle _Try");
    }

    virtual void visit(ASTExprNodeMacro& v) override {
        BUG(v.span(), "Hit ExprNode_Macro");
    }

    virtual void visit(ASTExprNodeMacroDefinition& v) override {
        BUG(v.span(), "Hit ExprNode_MacroDefinition outside a block");
    }

    virtual void visit(ASTExprNodeAsm& v) override {
        ::std::vector<::HIR::ExprNodeAsm::ValRef> outputs;
        ::std::vector<::HIR::ExprNodeAsm::ValRef> inputs;
        for (auto& vr : v.output) {
            outputs.push_back(::HIR::ExprNodeAsm::ValRef{vr.name, lower(vr.value)});
        }
        for (auto& vr : v.input) {
            inputs.push_back(::HIR::ExprNodeAsm::ValRef{vr.name, lower(vr.value)});
        }

        mRv.reset(gCratePtr->pool->make<::HIR::ExprNodeAsm>(v.span(), v.text, mv$(outputs), mv$(inputs), v.clobbers, v.flags));
    }

    virtual void visit(ASTExprNodeAsm2& v) override {
        std::vector<::HIR::ExprNodeAsm2::Param> params;
        for (auto& p : v.mParams) {
            TU_MATCH_HDRA((p), {)
            TU_ARMA(Const, e) {
                    ASSERT_BUG(v.span(), e, "Missing node for ASM Const");
                    params.push_back(lower(e));
                }
                TU_ARMA(Sym, e) {
                    params.push_back(LowerHIRPath(v.span(), e, FromASTPathClass::Value));
                }
                TU_ARMA(RegSingle, e) {
                    params.push_back(
                        ::HIR::ExprNodeAsm2::Param::make_RegSingle({
                            e.dir,
                            e.spec.clone(),
                            e.val ? lower(e.val) : nullptr // e.g. `lateout(regname) _`
                        })
                    );
                }
                TU_ARMA(Reg, e) {
                    params.push_back(::HIR::ExprNodeAsm2::Param::make_Reg({e.dir, e.spec.clone(), e.valIn ? lower(e.valIn) : nullptr, e.valOut ? lower(e.valOut) : nullptr}));
                }
            }
        }
        mRv.reset(gCratePtr->pool->make<::HIR::ExprNodeAsm2>(v.span(), v.options, v.lines, mv$(params)));
    }

    virtual void visit(ASTExprNodeFlow& v) override {
        switch (v.mType) {
            case ASTExprNodeFlow::RETURN:
                if (v.mValue) {
                    mRv.reset(gCratePtr->pool->make<::HIR::ExprNodeReturn>(v.span(), lower(v.mValue)));
                } else {
                    mRv.reset(gCratePtr->pool->make<::HIR::ExprNodeReturn>(v.span(), ::HIR::ExprNodeP(gCratePtr->pool->make<::HIR::ExprNodeTuple>(v.span(), ::std::vector<HIR::ExprNodeP>{}))));
                }
                break;
            case ASTExprNodeFlow::YIELD:
                mHasYield = true;
                {
                    auto value = v.mValue ? lower(v.mValue) : ::HIR::ExprNodeP(gCratePtr->pool->make<::HIR::ExprNodeTuple>(v.span(), ::std::vector<HIR::ExprNodeP>{}));
                    mRv.reset(gCratePtr->pool->make<::HIR::ExprNodeYield>(v.span(), std::move(value)));
                }
                break;
            case ASTExprNodeFlow::CONTINUE:
            case ASTExprNodeFlow::BREAK: {
                auto val = v.mValue ? lower(v.mValue) : ::HIR::ExprNodeP();
                ASSERT_BUG(v.span(), !(v.mType == ASTExprNodeFlow::CONTINUE && val), "Continue with a value isn't allowed");
                auto target = resolveLoopLabel(v.span(), v.target);
                mRv.reset(gCratePtr->pool->make<::HIR::ExprNodeLoopControl>(v.span(), mv$(target), (v.mType == ASTExprNodeFlow::CONTINUE), mv$(val)));
            } break;
            case ASTExprNodeFlow::YEET:
                BUG(v.span(), "do yeet should have been desugared");
                break;
        }
    }

    virtual void visit(ASTExprNodeLetBinding& v) override {
        if (v.elseNode) {
            // Cannot be expanded in expand, as it needs `None` to have been resolved to the enum variant
            // So, it's expanded here - with the cooperation of `Resolve_Absolute` allocating some variable bindings for us
            auto pat = LowerHIRPattern(v.pat);
            auto type = LowerHIRType(v.mType);
            auto nodeValue = lower(v.mValue);
            auto nodeElse = lower(v.elseNode);

            auto base = v.letelseSlots.first;
            auto count = v.letelseSlots.second;
            DEBUG(pat);

            struct V: public HIR::Visitor {
                unsigned base;
                unsigned count;
                std::vector<HIR::PatternBinding> bindings;
                std::map<unsigned, unsigned> mapping;

                V(HIR::TypeInterner& types, unsigned base, unsigned count)
                    : HIR::Visitor(nullptr, types)
                    , base(base)
                    , count(count)
                {
                }

                void visitPattern(::HIR::Pattern& pat) override {
                    HIR::Visitor::visitPattern(pat);
                    for (size_t i = 0; i < pat.mBindings.size(); i++) {
                        this->handleBinding(pat.mBindings[i]);
                    }
                    // SplitSlice also defines bindings
                    if (auto* e = pat.mData.opt_SplitSlice()) {
                        if (e->extraBind.isValid()) {
                            this->handleBinding(e->extraBind);
                        }
                    }
                    // - SplitTuple doesn't?
                    //if(auto* e = pat.m_data.opt_SplitTuple() ) {
                    //    if( e->extra_bind.is_valid() ) {
                    //        this->handle_binding(e->extra_bind);
                    //    }
                    //}
                }

                void handleBinding(::HIR::PatternBinding& pb) {
                    auto it = mapping.find(pb.slot);
                    if (it == mapping.end()) {
                        ASSERT_BUG(Span(), bindings.size() < this->count, "Miscount of variables in `let-else` - only allocated " << this->count);
                        unsigned newIdx = base + bindings.size();

                        bindings.push_back(HIR::PatternBinding(pb));
                        bindings.back().mType = HIR::PatternBinding::Type::Move;
                        it = mapping.insert(std::make_pair(pb.slot, newIdx)).first;
                    }
                    pb.isMutable = false;
                    pb.slot = it->second;
                }
            } visitor(gCratePtr->types, base, count);

            visitor.visitPattern(pat);
            /*
             * ```
             * let (a,b,c,...) = match $value: $ty {
             *     $pat => (a,b,c,...),
             *     _ => { let _: ! = $else; },
             *     };
             * ```
             */
            std::vector<HIR::Pattern> newPats;
            std::vector<HIR::ExprNodeP> tupleVals;
            const auto bindingSlots = HIR::patternBindingSlots(pat, HIR::PatternBindingOrder::FirstCandidate);
            ASSERT_BUG(v.span(), bindingSlots.size() == visitor.bindings.size(), "let-else candidate omitted bindings");
            for (const auto slot : bindingSlots) {
                ASSERT_BUG(v.span(), base <= slot && slot - base < visitor.bindings.size(), "Invalid temporary let-else binding " << slot);
                auto& binding = visitor.bindings[slot - base];
                tupleVals.push_back(HIR::ExprNodeP(gCratePtr->pool->make<HIR::ExprNodeVariable>(v.span(), binding.mName, slot)));
                newPats.push_back(HIR::Pattern(std::move(binding), HIR::Pattern::Data{}));
            }

            std::vector<HIR::ExprNodeMatch::Arm> matchArms(2);
            // `$pat => (a,b,c,...),`
            matchArms[0].patterns.push_back(std::move(pat));
            matchArms[0].mCode.reset(gCratePtr->pool->make<HIR::ExprNodeTuple>(v.span(), std::move(tupleVals)));
            matchArms[1].patterns.push_back(HIR::Pattern());
            // `_ => loop { let _: ! = $else; },
            matchArms[1].mCode.reset(gCratePtr->pool->make<HIR::ExprNodeLet>(v.span(), HIR::Pattern(), gCratePtr->types.diverge(), std::move(nodeElse)));
            matchArms[1].mCode.reset(gCratePtr->pool->make<HIR::ExprNodeLoop>(v.span(), "", std::move(matchArms[1].mCode), /*require_label*/ true));
            // HACK: Just use the code as-is.
            //match_arms[1].m_code = std::move(node_else);
            // `match $value: $ty {`
            auto matchValue = type->is_Infer() // Only emit the `: $ty` part if the type was specified (not a `_`)
                                   ? std::move(nodeValue)
                                   : HIR::ExprNodeP(gCratePtr->pool->make<HIR::ExprNodeUnsize>(v.span(), std::move(nodeValue), std::move(type)));
            auto match = HIR::ExprNodeP(gCratePtr->pool->make<HIR::ExprNodeMatch>(v.span(), std::move(matchValue), std::move(matchArms), true));

            // `let (a,b,c,...) = ...`
            mRv.reset(gCratePtr->pool->make<::HIR::ExprNodeLet>(v.span(), HIR::Pattern(::std::vector<HIR::PatternBinding>(), HIR::Pattern::Data::make_Tuple({std::move(newPats)})), gCratePtr->types.infer(), std::move(match)));
        } else {
            mRv.reset(gCratePtr->pool->make<::HIR::ExprNodeLet>(v.span(), LowerHIRPattern(v.pat), LowerHIRType(v.mType), lowerOpt(v.mValue), v.isSuper));
        }
    }

    virtual void visit(ASTExprNodeAssign& v) override {
        struct H {
            static ::HIR::ExprNodeAssign::Op getOp(ASTExprNodeAssign::Operation o) {
                switch (o) {
                    case ASTExprNodeAssign::NONE:
                        return ::HIR::ExprNodeAssign::Op::None;
                    case ASTExprNodeAssign::ADD:
                        return ::HIR::ExprNodeAssign::Op::Add;
                    case ASTExprNodeAssign::SUB:
                        return ::HIR::ExprNodeAssign::Op::Sub;

                    case ASTExprNodeAssign::MUL:
                        return ::HIR::ExprNodeAssign::Op::Mul;
                    case ASTExprNodeAssign::DIV:
                        return ::HIR::ExprNodeAssign::Op::Div;
                    case ASTExprNodeAssign::MOD:
                        return ::HIR::ExprNodeAssign::Op::Mod;

                    case ASTExprNodeAssign::AND:
                        return ::HIR::ExprNodeAssign::Op::And;
                    case ASTExprNodeAssign::OR:
                        return ::HIR::ExprNodeAssign::Op::Or;
                    case ASTExprNodeAssign::XOR:
                        return ::HIR::ExprNodeAssign::Op::Xor;

                    case ASTExprNodeAssign::SHR:
                        return ::HIR::ExprNodeAssign::Op::Shr;
                    case ASTExprNodeAssign::SHL:
                        return ::HIR::ExprNodeAssign::Op::Shl;
                }
                throw "";
            }
        };

        mRv.reset(gCratePtr->pool->make<::HIR::ExprNodeAssign>(v.span(), H::getOp(v.op), lower(v.slot), lower(v.mValue)));
    }

    virtual void visit(ASTExprNodeBinOp& v) override {
        ::HIR::ExprNodeBinOp::Op op;
        switch (v.mType) {
            case ASTExprNodeBinOp::RANGE: {
                BUG(v.span(), "Unexpected RANGE binop");
                break;
            }
            case ASTExprNodeBinOp::RANGE_INC: {
                BUG(v.span(), "Unexpected RANGE_INC binop");
                break;
            }
            case ASTExprNodeBinOp::PLACE_IN:
                mRv.reset(gCratePtr->pool->make<::HIR::ExprNodeEmplace>(v.span(), ::HIR::ExprNodeEmplace::Type::Placer, lower(v.left), lower(v.right)));
                break;

            case ASTExprNodeBinOp::CMPEQU:
                op = ::HIR::ExprNodeBinOp::Op::CmpEqu;
                if (0) {
                    case ASTExprNodeBinOp::CMPNEQU:
                        op = ::HIR::ExprNodeBinOp::Op::CmpNEqu;
                }
                if (0) {
                    case ASTExprNodeBinOp::CMPLT:
                        op = ::HIR::ExprNodeBinOp::Op::CmpLt;
                }
                if (0) {
                    case ASTExprNodeBinOp::CMPLTE:
                        op = ::HIR::ExprNodeBinOp::Op::CmpLtE;
                }
                if (0) {
                    case ASTExprNodeBinOp::CMPGT:
                        op = ::HIR::ExprNodeBinOp::Op::CmpGt;
                }
                if (0) {
                    case ASTExprNodeBinOp::CMPGTE:
                        op = ::HIR::ExprNodeBinOp::Op::CmpGtE;
                }
                if (0) {
                    case ASTExprNodeBinOp::BOOLAND:
                        op = ::HIR::ExprNodeBinOp::Op::BoolAnd;
                }
                if (0) {
                    case ASTExprNodeBinOp::BOOLOR:
                        op = ::HIR::ExprNodeBinOp::Op::BoolOr;
                }
                if (0) {
                    case ASTExprNodeBinOp::BITAND:
                        op = ::HIR::ExprNodeBinOp::Op::And;
                }
                if (0) {
                    case ASTExprNodeBinOp::BITOR:
                        op = ::HIR::ExprNodeBinOp::Op::Or;
                }
                if (0) {
                    case ASTExprNodeBinOp::BITXOR:
                        op = ::HIR::ExprNodeBinOp::Op::Xor;
                }
                if (0) {
                    case ASTExprNodeBinOp::MULTIPLY:
                        op = ::HIR::ExprNodeBinOp::Op::Mul;
                }
                if (0) {
                    case ASTExprNodeBinOp::DIVIDE:
                        op = ::HIR::ExprNodeBinOp::Op::Div;
                }
                if (0) {
                    case ASTExprNodeBinOp::MODULO:
                        op = ::HIR::ExprNodeBinOp::Op::Mod;
                }
                if (0) {
                    case ASTExprNodeBinOp::ADD:
                        op = ::HIR::ExprNodeBinOp::Op::Add;
                }
                if (0) {
                    case ASTExprNodeBinOp::SUB:
                        op = ::HIR::ExprNodeBinOp::Op::Sub;
                }
                if (0) {
                    case ASTExprNodeBinOp::SHR:
                        op = ::HIR::ExprNodeBinOp::Op::Shr;
                }
                if (0) {
                    case ASTExprNodeBinOp::SHL:
                        op = ::HIR::ExprNodeBinOp::Op::Shl;
                }

                mRv.reset(gCratePtr->pool->make<::HIR::ExprNodeBinOp>(v.span(), op, lower(v.left), lower(v.right)));
                break;
        }
    }

    virtual void visit(ASTExprNodeUniOp& v) override {
        ::HIR::ExprNodeUniOp::Op op;
        switch (v.mType) {
            case ASTExprNodeUniOp::BOX: {
                mRv.reset(gCratePtr->pool->make<::HIR::ExprNodeEmplace>(v.span(), ::HIR::ExprNodeEmplace::Type::Boxer, ::HIR::ExprNodeP(gCratePtr->pool->make<::HIR::ExprNodeTuple>(v.span(), ::std::vector<HIR::ExprNodeP>{})), lower(v.mValue)));
            } break;
            case ASTExprNodeUniOp::QMARK:
                BUG(v.span(), "Encounterd question mark operator (should have been expanded in AST)");
                break;

            case ASTExprNodeUniOp::REF:
                mRv.reset(gCratePtr->pool->make<::HIR::ExprNodeBorrow>(v.span(), ::HIR::BorrowType::Shared, lower(v.mValue)));
                break;
            case ASTExprNodeUniOp::RawBorrow:
                mRv.reset(gCratePtr->pool->make<::HIR::ExprNodeRawBorrow>(v.span(), ::HIR::BorrowType::Shared, lower(v.mValue)));
                break;
            case ASTExprNodeUniOp::REFMUT:
                mRv.reset(gCratePtr->pool->make<::HIR::ExprNodeBorrow>(v.span(), ::HIR::BorrowType::Unique, lower(v.mValue)));
                break;
            case ASTExprNodeUniOp::RawBorrowMut:
                mRv.reset(gCratePtr->pool->make<::HIR::ExprNodeRawBorrow>(v.span(), ::HIR::BorrowType::Unique, lower(v.mValue)));
                break;

            case ASTExprNodeUniOp::AWait:
                mRv.reset(gCratePtr->pool->make<::HIR::ExprNodeAWait>(v.span(), lower(v.mValue)));
                break;

            case ASTExprNodeUniOp::INVERT:
                op = ::HIR::ExprNodeUniOp::Op::Invert;
                if (0) {
                    case ASTExprNodeUniOp::NEGATE:
                        op = ::HIR::ExprNodeUniOp::Op::Negate;
                }
                mRv.reset(gCratePtr->pool->make<::HIR::ExprNodeUniOp>(v.span(), op, lower(v.mValue)));
                break;
        }
    }

    virtual void visit(ASTExprNodeCast& v) override {
        mRv.reset(gCratePtr->pool->make<::HIR::ExprNodeCast>(v.span(), lower(v.mValue), LowerHIRType(v.mType)));
    }

    virtual void visit(ASTExprNodeTypeAnnotation& v) override {
        // TODO: Create a proper node for this
        // - Using `Unsize` works pretty well, but isn't quite "correct"
        mRv.reset(gCratePtr->pool->make<::HIR::ExprNodeUnsize>(v.span(), lower(v.mValue), LowerHIRType(v.mType)));
    }

    virtual void visit(ASTExprNodeCallPath& v) override {
        ::std::vector<::HIR::ExprNodeP> args;
        for (auto& arg : v.mArgs) {
            args.push_back(lower(arg));
        }

        if (const auto* e = v.mPath.cls.opt_Local()) {
            mRv.reset(gCratePtr->pool->make<::HIR::ExprNodeCallValue>(v.span(), ::HIR::ExprNodeP(gCratePtr->pool->make<::HIR::ExprNodeVariable>(v.span(), e->name, v.mPath.mBindings.value.binding.as_Variable().slot)), mv$(args)));
        } else {
            TU_MATCH_HDRA( (v.mPath.mBindings.value.binding), {)
            default:
                mRv.reset( gCratePtr->pool->make<::HIR::ExprNodeCallPath>( v.span(),
                    LowerHIRPath(v.span(), v.mPath, FromASTPathClass::Value),
                    mv$( args )
                    ) );
                TU_ARMA(Static, e) {
                    bool isConst = e.static_ ? e.static_->sClass() == ASTStatic::Class::CONST : (e.hir ? false : true) // If HIR Pointer is null, this is a HIR::Const
                        ;
                    mRv.reset(gCratePtr->pool->make<::HIR::ExprNodeCallValue>(v.span(), ::HIR::ExprNodeP(gCratePtr->pool->make<::HIR::ExprNodePathValue>(v.span(), LowerHIRPath(v.span(), v.mPath, FromASTPathClass::Value), isConst ? ::HIR::ExprNodePathValue::CONSTANT : ::HIR::ExprNodePathValue::STATIC)), mv$(args)));
                }
                //TU_ARMA(TypeAlias, e) {
                //    TODO(v.span(), "CallPath -> TupleVariant TypeAlias");
                //    }
                TU_ARMA(EnumVar, e) {
                    mRv.reset(gCratePtr->pool->make<::HIR::ExprNodeTupleVariant>(v.span(), LowerHIRGenericPath(v.span(), v.mPath, FromASTPathClass::Value), false, mv$(args)));
                }
                TU_ARMA(Struct, e) {
                    mRv.reset(gCratePtr->pool->make<::HIR::ExprNodeTupleVariant>(v.span(), LowerHIRGenericPath(v.span(), v.mPath, FromASTPathClass::Value), true, mv$(args)));
                }
            }
        }
    }

    virtual void visit(ASTExprNodeCallMethod& v) override {
        ::std::vector<::HIR::ExprNodeP> args;
        for (auto& arg : v.mArgs) {
            args.push_back(lower(arg));
        }

        mRv.reset(gCratePtr->pool->make<::HIR::ExprNodeCallMethod>(v.span(), lower(v.val), v.method.name(), LowerHIRPathParams(v.span(), v.method.args(), /*allow_assoc=*/false), mv$(args)));
    }

    virtual void visit(ASTExprNodeCallObject& v) override {
        ::std::vector<::HIR::ExprNodeP> args;
        for (auto& arg : v.mArgs) {
            args.push_back(lower(arg));
        }

        mRv.reset(gCratePtr->pool->make<::HIR::ExprNodeCallValue>(v.span(), lower(v.val), mv$(args)));
    }

    virtual void visit(ASTExprNodeLoop& v) override {
        auto label = enterLoopLabel(v.label);
        auto code = lower(v.mCode);
        leaveLoopLabel(label);
        mRv.reset(gCratePtr->pool->make<::HIR::ExprNodeLoop>(v.span(), mv$(label), mv$(code)));
    }

    void visit(ASTExprNodeFor& v) override {
        // NOTE: This should already be desugared (as a pass before resolve)
        BUG(v.span(), "Encountered still-sugared for loop");
    }

    ::std::vector<::HIR::ExprNodeMatch::Guard> ifletToGuards(std::vector<ASTIfLetCondition>& guards) {
        ::std::vector<::HIR::ExprNodeMatch::Guard> rv;
        rv.reserve(guards.size());
        for (auto& c : guards) {
            auto condPat = c.optPat ? LowerHIRPattern(*c.optPat) : HIR::Pattern{HIR::PatternBinding(), HIR::Pattern::Data::make_Value({::HIR::Pattern::Value::make_Integer({HIR::CoreType::Bool, U128(1)})})};
            auto condVal = lowerOpt(c.value);
            rv.push_back(::HIR::ExprNodeMatch::Guard{std::move(condPat), std::move(condVal), c.optPat ? false : true});
        }
        return rv;
    }

    virtual void visit(ASTExprNodeWhile& v) override {
        // Desugar to `loop { match () { _ if ... => { body }, _ => break, } }`
        auto label = enterLoopLabel(v.label);
        ::std::vector<::HIR::ExprNodeMatch::Arm> arms;
        arms.push_back(::HIR::ExprNodeMatch::Arm{makeVec1(::HIR::Pattern()), ifletToGuards(v.conditions), lower(v.mCode)});
        arms.push_back(::HIR::ExprNodeMatch::Arm{makeVec1(::HIR::Pattern()), {}, HIR::ExprNodeP(gCratePtr->pool->make<HIR::ExprNodeLoopControl>(v.span(), "", false, nullptr))});
        leaveLoopLabel(label);
        mRv.reset(gCratePtr->pool->make<HIR::ExprNodeLoop>(v.span(), mv$(label), HIR::ExprNodeP(gCratePtr->pool->make<HIR::ExprNodeMatch>(v.span(), HIR::ExprNodeP(gCratePtr->pool->make<HIR::ExprNodeTuple>(v.span(), ::std::vector<HIR::ExprNodeP>())), std::move(arms)))));
    }

    virtual void visit(ASTExprNodeMatch& v) override {
        ::std::vector<::HIR::ExprNodeMatch::Arm> arms;

        for (auto& arm : v.arms) {
            ::HIR::ExprNodeMatch::Arm newArm{{}, ifletToGuards(arm.guard), lower(arm.mCode)};

            for (const auto& pat : arm.patterns) {
                newArm.patterns.push_back(LowerHIRPattern(pat));
            }

            arms.push_back(mv$(newArm));
        }

        mRv.reset(gCratePtr->pool->make<::HIR::ExprNodeMatch>(v.span(), lower(v.val), mv$(arms)));
    }

    virtual void visit(ASTExprNodeIf& v) override {
        ::std::vector<::HIR::ExprNodeMatch::Arm> arms;
        // Desugar to a `match`
        for (auto& arm : v.arms) {
            arms.push_back(::HIR::ExprNodeMatch::Arm{makeVec1(::HIR::Pattern()), ifletToGuards(arm.conditions), lower(arm.body)});
        }
        arms.push_back(::HIR::ExprNodeMatch::Arm{makeVec1(::HIR::Pattern()), {}, v.elseNode ? lower(v.elseNode) : HIR::ExprNodeP(gCratePtr->pool->make<HIR::ExprNodeTuple>(v.span(), ::std::vector<HIR::ExprNodeP>()))});

        mRv.reset(gCratePtr->pool->make<::HIR::ExprNodeMatch>(v.span(), HIR::ExprNodeP(gCratePtr->pool->make<HIR::ExprNodeTuple>(v.span(), ::std::vector<HIR::ExprNodeP>())), std::move(arms)));
    }

    virtual void visit(ASTExprNodeWildcardPattern& v) override {
        ERROR(v.span(), E0000, "`_` is only valid in expressions on the left-hand side of an assignment");
    }

    virtual void visit(ASTExprNodeInteger& v) override {
        struct H {
            static ::HIR::CoreType getType(Span sp, ::eCoreType ct) {
                switch (ct) {
                    case CORETYPE_ANY:
                        return ::HIR::CoreType::Str;

                    case CORETYPE_I8:
                        return ::HIR::CoreType::I8;
                    case CORETYPE_U8:
                        return ::HIR::CoreType::U8;
                    case CORETYPE_I16:
                        return ::HIR::CoreType::I16;
                    case CORETYPE_U16:
                        return ::HIR::CoreType::U16;
                    case CORETYPE_I32:
                        return ::HIR::CoreType::I32;
                    case CORETYPE_U32:
                        return ::HIR::CoreType::U32;
                    case CORETYPE_I64:
                        return ::HIR::CoreType::I64;
                    case CORETYPE_U64:
                        return ::HIR::CoreType::U64;
                    case CORETYPE_I128:
                        return ::HIR::CoreType::I128;
                    case CORETYPE_U128:
                        return ::HIR::CoreType::U128;

                    case CORETYPE_INT:
                        return ::HIR::CoreType::Isize;
                    case CORETYPE_UINT:
                        return ::HIR::CoreType::Usize;

                    case CORETYPE_CHAR:
                        return ::HIR::CoreType::Char;

                    default:
                        BUG(sp, "Unknown type for integer literal - " << coretypeName(ct));
                }
            }
        };

        if (v.datatype == CORETYPE_F32 || v.datatype == CORETYPE_F64) {
            DEBUG("Integer annotated as float, create float node");
            mRv.reset(gCratePtr->pool->make<::HIR::ExprNodeLiteral>(v.span(), ::HIR::ExprNodeLiteral::Data::make_Float({(v.datatype == CORETYPE_F32 ? ::HIR::CoreType::F32 : ::HIR::CoreType::F64), v.mValue.toDouble()})));
            return;
        }
        mRv.reset(gCratePtr->pool->make<::HIR::ExprNodeLiteral>(v.span(), ::HIR::ExprNodeLiteral::Data::make_Integer({H::getType(v.span(), v.datatype), v.mValue})));
    }

    virtual void visit(ASTExprNodeFloat& v) override {
        ::HIR::CoreType ct;
        switch (v.datatype) {
            case CORETYPE_ANY:
                ct = ::HIR::CoreType::Str;
                break;
            case CORETYPE_F16:
                ct = ::HIR::CoreType::F16;
                break;
            case CORETYPE_F32:
                ct = ::HIR::CoreType::F32;
                break;
            case CORETYPE_F64:
                ct = ::HIR::CoreType::F64;
                break;
            case CORETYPE_F128:
                ct = ::HIR::CoreType::F128;
                break;
            default:
                BUG(v.span(), "Unknown type for float literal - " << coretypeName(v.datatype));
        }
        mRv.reset(gCratePtr->pool->make<::HIR::ExprNodeLiteral>(v.span(), ::HIR::ExprNodeLiteral::Data::make_Float({ct, v.mValue})));
    }

    virtual void visit(ASTExprNodeBool& v) override {
        mRv.reset(gCratePtr->pool->make<::HIR::ExprNodeLiteral>(v.span(), ::HIR::ExprNodeLiteral::Data::make_Boolean(v.mValue)));
    }

    virtual void visit(ASTExprNodeString& v) override {
        mRv.reset(gCratePtr->pool->make<::HIR::ExprNodeLiteral>(v.span(), ::HIR::ExprNodeLiteral::Data::make_String(v.mValue)));
    }

    virtual void visit(ASTExprNodeByteString& v) override {
        ::std::vector<char> dat{v.mValue.begin(), v.mValue.end()};
        mRv.reset(gCratePtr->pool->make<::HIR::ExprNodeLiteral>(v.span(), ::HIR::ExprNodeLiteral::Data::make_ByteString(mv$(dat))));
    }

    virtual void visit(ASTExprNodeCString& v) override {
        mRv.reset(gCratePtr->pool->make<::HIR::ExprNodeLiteral>(v.span(), ::HIR::ExprNodeLiteral::Data::make_CString({v.mValue})));
    }

    virtual void visit(ASTExprNodeClosure& v) override {
        ::HIR::ExprNodeClosure::argsT args;
        for (const auto& arg : v.mArgs) {
            args.push_back(::std::make_pair(LowerHIRPattern(arg.first), LowerHIRType(arg.second)));
        }

        auto origHasYield = mHasYield;
        mHasYield = false;
        auto inner = lowerIsolated(v.mCode);
        auto hasYield = mHasYield;
        mHasYield = origHasYield;

        if (hasYield) {
            // NOTE: One argument could be present with yielding arguments?
            if (!args.empty()) {
                ERROR(v.span(), E0000, "Generator closures don't take arguments.");
            }
            mRv.reset(gCratePtr->pool->make<::HIR::ExprNodeGenerator>(
                v.span(),
                LowerHIRType(v.returnType),
                gCratePtr->types.infer(),
                gCratePtr->types.infer(),
                mv$(inner),
                v.isMove,
                v.isPinned
            ));
        } else {
            if (v.isPinned) {
                ERROR(v.span(), E0000, "Invalid use of `static` on non-yielding closure");
            }
            mRv.reset(gCratePtr->pool->make<::HIR::ExprNodeClosure>(v.span(), std::move(args), LowerHIRType(v.returnType), std::move(inner), v.isMove));
        }
    }

    virtual void visit(ASTExprNodeStructLiteral& v) override {
        if (v.mPath.mBindings.type.binding.is_Union()) {
            if (v.values.size() != 1) {
                ERROR(v.span(), E0000, "Union constructors can only specify a single field");
            }
            if (v.baseValue) {
                ERROR(v.span(), E0000, "Union constructors can't take a base value");
            }
        }

        ::HIR::ExprNodeStructLiteral::tValues values;
        for (auto& val : v.values) {
            values.push_back(::std::make_pair(val.name, lower(val.value)));
        }

        if (values.empty() && !v.baseValue) {
            enum class EmptyKind {
                None,
                Unit,
                Tuple,
            };

            if (const auto* binding = v.mPath.mBindings.type.binding.opt_EnumVar()) {
                EmptyKind kind = EmptyKind::None;
                if (binding->enum_) {
                    const auto& data = binding->enum_->variants().at(binding->idx).mData;
                    kind = data.is_Unit() ? EmptyKind::Unit
                        : data.is_Tuple() && data.as_Tuple().mItems.empty() ? EmptyKind::Tuple
                        : EmptyKind::None;
                } else if (binding->hir) {
                    const auto& enm = *binding->hir;
                    if (enm.mData.is_Value()) {
                        kind = EmptyKind::Unit;
                    } else {
                        const auto& var = enm.mData.as_Data().at(binding->idx);
                        if (var.type == gCratePtr->types.unit()) {
                            kind = EmptyKind::Unit;
                        } else {
                            const auto& str = *var.type->as_Path().binding.as_Struct();
                            kind = str.mData.is_Unit() ? EmptyKind::Unit
                                : str.mData.is_Tuple() && str.mData.as_Tuple().empty() ? EmptyKind::Tuple
                                : EmptyKind::None;
                        }
                    }
                }
                if (kind == EmptyKind::Unit) {
                    mRv.reset(gCratePtr->pool->make<::HIR::ExprNodeUnitVariant>(
                        v.span(), LowerHIRGenericPath(v.span(), v.mPath, FromASTPathClass::Type), false));
                    return;
                }
                if (kind == EmptyKind::Tuple) {
                    mRv.reset(gCratePtr->pool->make<::HIR::ExprNodeTupleVariant>(
                        v.span(), LowerHIRGenericPath(v.span(), v.mPath, FromASTPathClass::Type), false, ::std::vector<::HIR::ExprNodeP>{}));
                    return;
                }
            } else if (const auto* binding = v.mPath.mBindings.type.binding.opt_Struct()) {
                EmptyKind kind = EmptyKind::None;
                if (binding->struct_) {
                    const auto& data = binding->struct_->mData;
                    kind = data.is_Unit() ? EmptyKind::Unit
                        : data.is_Tuple() && data.as_Tuple().ents.empty() ? EmptyKind::Tuple
                        : EmptyKind::None;
                } else if (binding->hir) {
                    const auto& data = binding->hir->mData;
                    kind = data.is_Unit() ? EmptyKind::Unit
                        : data.is_Tuple() && data.as_Tuple().empty() ? EmptyKind::Tuple
                        : EmptyKind::None;
                }
                if (kind == EmptyKind::Unit) {
                    mRv.reset(gCratePtr->pool->make<::HIR::ExprNodeUnitVariant>(
                        v.span(), LowerHIRGenericPath(v.span(), v.mPath, FromASTPathClass::Type), true));
                    return;
                }
                if (kind == EmptyKind::Tuple) {
                    mRv.reset(gCratePtr->pool->make<::HIR::ExprNodeTupleVariant>(
                        v.span(), LowerHIRGenericPath(v.span(), v.mPath, FromASTPathClass::Type), true, ::std::vector<::HIR::ExprNodeP>{}));
                    return;
                }
            }
        }
        auto ty = LowerHIRType(::TypeRef(v.span(), v.mPath));
        if (v.mPath.mBindings.type.binding.is_EnumVar()) {
            ASSERT_BUG(v.span(), TU_TEST1(*ty, Path, .path.mData.is_Generic()), "Enum variant path not GenericPath: " << ty);
            auto data = ty->cloneData();
            auto& gp = data.as_Path().path.mData.as_Generic();
            auto varName = gp.mPath.popComponent();
            auto enumTy = gCratePtr->types.intern(mv$(data));
            ty = gCratePtr->types.path(::HIR::Path(enumTy, mv$(varName)), {});
        }
        mRv.reset(gCratePtr->pool->make<::HIR::ExprNodeStructLiteral>(v.span(), mv$(ty), !v.mPath.mBindings.type.binding.is_EnumVar(), lowerOpt(v.baseValue), mv$(values)));
    }

    virtual void visit(ASTExprNodeStructLiteralPattern& v) override {
        if (v.mPath.mBindings.type.binding.is_Union()) {
            if (v.values.size() != 1) {
                ERROR(v.span(), E0000, "Union constructors can only specify a single field");
            }
        }

        ::HIR::ExprNodeStructLiteral::tValues values;
        for (auto& val : v.values) {
            values.push_back(::std::make_pair(val.name, lower(val.value)));
        }
        auto ty = LowerHIRType(::TypeRef(v.span(), v.mPath));
        if (v.mPath.mBindings.type.binding.is_EnumVar()) {
            ASSERT_BUG(v.span(), TU_TEST1(*ty, Path, .path.mData.is_Generic()), "Enum variant path not GenericPath: " << ty);
            auto data = ty->cloneData();
            auto& gp = data.as_Path().path.mData.as_Generic();
            auto varName = gp.mPath.popComponent();
            auto enumTy = gCratePtr->types.intern(mv$(data));
            ty = gCratePtr->types.path(::HIR::Path(enumTy, mv$(varName)), {});
        }
        mRv.reset(gCratePtr->pool->make<::HIR::ExprNodeStructLiteral>(v.span(), mv$(ty), !v.mPath.mBindings.type.binding.is_EnumVar(), true, mv$(values)));
    }

    virtual void visit(ASTExprNodeArray& v) override {
        if (v.mSize) {
            mRv.reset(gCratePtr->pool->make<::HIR::ExprNodeArraySized>(
                v.span(),
                lower(v.values.at(0)),
                // TODO: Should this size be a full expression on its own?
                lower(v.mSize)
            ));
        } else {
            ::std::vector<::HIR::ExprNodeP> vals;
            for (auto& val : v.values) {
                vals.push_back(lower(val));
            }
            mRv.reset(gCratePtr->pool->make<::HIR::ExprNodeArrayList>(v.span(), mv$(vals)));
        }
    }

    virtual void visit(ASTExprNodeTuple& v) override {
        ::std::vector<::HIR::ExprNodeP> vals;
        for (auto& val : v.values) {
            vals.push_back(lower(val));
        }
        mRv.reset(gCratePtr->pool->make<::HIR::ExprNodeTuple>(v.span(), mv$(vals)));
    }

    virtual void visit(ASTExprNodeNamedValue& v) override {
        if (const auto* e = v.mPath.cls.opt_Local()) {
            TU_MATCH_HDRA( (v.mPath.mBindings.value.binding), {)
            default:
                BUG(v.span(), "Named value was a local, but wasn't bound to a known type - " << v.mPath);
                TU_ARMA(Generic, binding) {
                    mRv.reset(gCratePtr->pool->make<::HIR::ExprNodeConstParam>(v.span(), e->name, binding.index));
                }
                TU_ARMA(Variable, binding) {
                    mRv.reset(gCratePtr->pool->make<::HIR::ExprNodeVariable>(v.span(), e->name, binding.slot));
                }
            }
        } else {
            TU_MATCH_HDRA( (v.mPath.mBindings.value.binding), {)
            TU_ARMA(Struct, e) {
                    ASSERT_BUG(v.span(), e.struct_ || e.hir, "PathValue bound to a struct but pointer not set - " << v.mPath);
                    // Check the form and emit a PathValue if not a unit
                    bool isTupleConstructor = false;
                    if (e.struct_) {
                        if (e.struct_->mData.is_Struct()) {
                            ERROR(v.span(), E0000, "Named value referring to a struct that isn't tuple-like or unit-like - " << v.mPath);
                        }
                        isTupleConstructor = e.struct_->mData.is_Tuple();
                    } else {
                        const auto& str = *e.hir;
                        if (str.mData.is_Unit()) {
                            isTupleConstructor = false;
                        } else if (str.mData.is_Tuple()) {
                            isTupleConstructor = true;
                        } else {
                            ERROR(v.span(), E0000, "Named value referring to a struct that isn't tuple-like or unit-like - " << v.mPath);
                        }
                    }
                    if (isTupleConstructor) {
                        mRv.reset(gCratePtr->pool->make<::HIR::ExprNodePathValue>(v.span(), LowerHIRPath(v.span(), v.mPath, FromASTPathClass::Value), ::HIR::ExprNodePathValue::STRUCT_CONSTR));
                    } else {
                        mRv.reset(gCratePtr->pool->make<::HIR::ExprNodeUnitVariant>(v.span(), LowerHIRGenericPath(v.span(), v.mPath, FromASTPathClass::Value), true));
                    }
                }
                TU_ARMA(EnumVar, e) {
                    ASSERT_BUG(v.span(), e.enum_ || e.hir, "PathValue bound to an enum but pointer not set - " << v.mPath);
                    const auto& varName = v.mPath.nodes().back().name();
                    bool isTupleConstructor = false;
                    unsigned int varIdx;
                    if (e.enum_) {
                        const auto& enm = *e.enum_;
                        auto it = ::std::find_if(enm.variants().begin(), enm.variants().end(), [&](const auto& x) {
                            return x.mName == varName;
                        });
                        assert(it != enm.variants().end());

                        varIdx = static_cast<unsigned int>(it - enm.variants().begin());
                        if (it->mData.is_Struct()) {
                            ERROR(v.span(), E0000, "Named value referring to an enum that isn't tuple-like or unit-like - " << v.mPath);
                        }
                        isTupleConstructor = it->mData.is_Tuple() && it->mData.as_Tuple().mItems.size() > 0;
                    } else {
                        const auto& enm = *e.hir;
                        auto idx = enm.findVariant(varName);
                        assert(idx != SIZE_MAX);

                        varIdx = idx;
                        if (const auto* ee = enm.mData.opt_Data()) {
                            if (ee->at(idx).type == gCratePtr->types.unit()) {
                            }
                            // TODO: Assert that it's not a struct-like
                            else {
                                isTupleConstructor = true;
                            }
                        }
                    }
                    (void)varIdx; // TODO: Save time later by saving this.
                    if (isTupleConstructor) {
                        mRv.reset(gCratePtr->pool->make<::HIR::ExprNodePathValue>(v.span(), LowerHIRPath(v.span(), v.mPath, FromASTPathClass::Value), ::HIR::ExprNodePathValue::ENUM_VAR_CONSTR));
                    } else {
                        mRv.reset(gCratePtr->pool->make<::HIR::ExprNodeUnitVariant>(v.span(), LowerHIRGenericPath(v.span(), v.mPath, FromASTPathClass::Value), false));
                    }
                }
                TU_ARMA(Function, e) {
                    mRv.reset(gCratePtr->pool->make<::HIR::ExprNodePathValue>(v.span(), LowerHIRPath(v.span(), v.mPath, FromASTPathClass::Value), ::HIR::ExprNodePathValue::FUNCTION));
                }
                TU_ARMA(Static, e) {
                    if (e.static_) {
                        if (e.static_->sClass() != ASTStatic::CONST) {
                            mRv.reset(gCratePtr->pool->make<::HIR::ExprNodePathValue>(v.span(), LowerHIRPath(v.span(), v.mPath, FromASTPathClass::Value), ::HIR::ExprNodePathValue::STATIC));
                        } else {
                            mRv.reset(gCratePtr->pool->make<::HIR::ExprNodePathValue>(v.span(), LowerHIRPath(v.span(), v.mPath, FromASTPathClass::Value), ::HIR::ExprNodePathValue::CONSTANT));
                        }
                    } else if (e.hir) {
                        mRv.reset(gCratePtr->pool->make<::HIR::ExprNodePathValue>(v.span(), LowerHIRPath(v.span(), v.mPath, FromASTPathClass::Value), ::HIR::ExprNodePathValue::STATIC));
                    }
                    // HACK: If the HIR pointer is nullptr, then it refers to a `const
                    else {
                        mRv.reset(gCratePtr->pool->make<::HIR::ExprNodePathValue>(v.span(), LowerHIRPath(v.span(), v.mPath, FromASTPathClass::Value), ::HIR::ExprNodePathValue::CONSTANT));
                    }
                }
                break;
                default:
                    auto p = LowerHIRPath(v.span(), v.mPath, FromASTPathClass::Value);
                    ASSERT_BUG(v.span(), !p.mData.is_Generic(), "Unknown binding for PathValue but path is generic - " << v.mPath);
                    mRv.reset(gCratePtr->pool->make<::HIR::ExprNodePathValue>(v.span(), mv$(p), ::HIR::ExprNodePathValue::UNKNOWN));
            }
        }
    }

    virtual void visit(ASTExprNodeField& v) override {
        mRv.reset(gCratePtr->pool->make<::HIR::ExprNodeField>(v.span(), lower(v.obj), v.mName));
    }

    virtual void visit(ASTExprNodeIndex& v) override {
        mRv.reset(gCratePtr->pool->make<::HIR::ExprNodeIndex>(v.span(), lower(v.obj), lower(v.idx)));
    }

    virtual void visit(ASTExprNodeDeref& v) override {
        mRv.reset(gCratePtr->pool->make<::HIR::ExprNodeDeref>(v.span(), lower(v.mValue)));
    }
};

::HIR::ExprPtr LowerHIRExprNode(const ASTExprNode& e) {
    LowerHIRExprNodeVisitor v;

    const_cast<ASTExprNode*>(&e)->visit(v);

    if (!v.mRv) {
        BUG(e.span(), typeid(e).name() << " - Yielded a nullptr HIR node");
    }

    struct InitialiseResultTypes final: ::HIR::ExprVisitorDef {
        explicit InitialiseResultTypes(::HIR::TypeInterner& types)
            : ::HIR::ExprVisitorDef(types)
        {
        }

        void visitNodePtr(::HIR::ExprNodeP& node) override {
            node->resType = typeInterner().infer();
            node->visit(*this);
        }

        void visitType(::HIR::TypeRef&) override {
        }
    } initialise(gCratePtr->types);
    initialise.visitNodePtr(v.mRv);

    return ::HIR::ExprPtr(mv$(v.mRv));
}
